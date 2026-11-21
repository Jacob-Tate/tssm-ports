"""Struct layouts out of the decomp's own headers, for gen.py.

    python tools/gcswap/layouts.py Struct [Struct...]     print the leaves

Compiles one translation unit that includes spec.HEADERS and takes the sizeof
of every struct the spec names, for a 32-bit target (the layout the Xbox and
GameCube packers wrote, and the one the port's 32-bit build reads in place),
with clang's -fdump-record-layouts. The dump gives every field's offset; this
turns a struct into its leaves, (offset, width, name) for every scalar, with
arrays expanded and nested structs flattened.

A union becomes its members' leaves when they agree (every member puts a field
of the same width at the same offset, or nothing there). When they do not, the
union is one leaf of kind ":union" and spec.UNIONS must say how to choose.
A bitfield is a leaf of kind ":bits" (width 0): gen.py refuses it unless the
spec says what to do, because MSB-first and LSB-first packing differ.
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)


def find_clang():
    if os.environ.get("CLANGXX"):
        return os.environ["CLANGXX"]
    for p in (r"C:\Program Files\LLVM\bin\clang++.exe", "/usr/bin/clang++", "/usr/local/bin/clang++"):
        if os.path.exists(p):
            return p
    return "clang++"


SCALARS = {
    "bool": 1, "_Bool": 1, "char": 1, "signed char": 1, "unsigned char": 1,
    "short": 2, "unsigned short": 2,
    "int": 4, "unsigned int": 4, "long": 4, "unsigned long": 4, "float": 4,
    "long long": 8, "unsigned long long": 8, "double": 8,
}

LINE = re.compile(r"^\s*(\d+)(?::(\d+)-(\d+))?\s\|(\s*)(.*)$")


def clang_args(tu):
    pc = os.path.join(ROOT, "src", "SB", "Core", "pc")
    sb = os.path.join(ROOT, "src", "SB")
    inc = [os.path.join(pc, "compat"), pc, os.path.join(pc, "rwsdk"),
           os.path.join(ROOT, "include"), os.path.join(ROOT, "include", "bink"),
           os.path.join(ROOT, "include", "inline"), os.path.join(sb, "Core", "x"),
           os.path.join(sb, "Game")]
    target = "i686-pc-windows-msvc" if os.name == "nt" else "i686-linux-gnu"
    args = [find_clang(), "--target=" + target, "-std=c++17", "-fsyntax-only", "-w",
            "-DPLATFORM_PC", "-DNON_MATCHING", "-funsigned-char", "-fno-sized-deallocation", "-Wno-register",
            "-include", os.path.join(sb, "SBPrefix.h")]
    args += ["-I" + i for i in inc]
    args += ["-Xclang", "-fdump-record-layouts", "-Xclang", "-fdump-record-layouts-canonical", tu]
    return args


def strip_record(t):
    for kw in ("struct ", "class ", "union "):
        if t.startswith(kw):
            return t[len(kw):]
    return t


class Record:
    def __init__(self, name, kind):
        self.name = name
        self.kind = kind
        self.size = None
        self.lines = []  # (offset, bitfield, depth, type, field)


def parse(text):
    records = {}
    cur = None
    for raw in text.splitlines():
        if raw.startswith("*** Dumping AST Record Layout"):
            cur = None
            continue
        m = LINE.match(raw)
        if m is None:
            s = re.search(r"\[sizeof=(\d+)", raw)
            if s and cur is not None:
                cur.size = int(s.group(1))
                records.setdefault(cur.name, cur)
                cur = None
            continue
        off, b0, b1, indent, rest = m.groups()
        depth = len(indent) // 2
        rest = rest.strip()
        if cur is None:
            kind, _, name = rest.partition(" ")
            cur = Record(name.strip(), kind)
            continue
        if rest.endswith(" (base)") or rest.endswith(" (primary base)") or rest.endswith(" (virtual base)"):
            typ, field = rest.rsplit(" (", 1)[0], "(base)"
        elif "vftable pointer" in rest or "vtable pointer" in rest or "vbtable pointer" in rest:
            typ, field = "vptr", "(vptr)"
        elif "(anonymous" in rest and rest.rstrip().endswith(")"):
            # An anonymous struct or union: "union X::(anonymous at path:24:5)",
            # whose path may hold spaces. It has no field name.
            typ, field = rest.strip(), "(anon)"
        else:
            typ, _, field = rest.rpartition(" ")
        cur.lines.append((int(off), (int(b0), int(b1)) if b0 is not None else None, depth,
                          typ.strip(), field))
    return records


class Layouts:
    def __init__(self, headers, structs, extra_source=""):
        fd, tu = tempfile.mkstemp(suffix=".cpp", prefix="gcswap_layouts_")
        with os.fdopen(fd, "w") as f:
            for h in headers:
                f.write('#include "%s"\n' % h)
            f.write(extra_source)
            f.write("unsigned gcswap_sizes[] = {\n")
            for s in structs:
                f.write("    sizeof(%s),\n" % s)
            f.write("};\n")
        try:
            r = subprocess.run(clang_args(tu), capture_output=True, text=True)
        finally:
            os.unlink(tu)
        if r.returncode != 0:
            raise SystemExit("clang failed:\n" + r.stderr[-6000:])
        self.records = parse(r.stdout)

    def find(self, name):
        rec = self.records.get(name)
        if rec is None:
            hits = [r for k, r in self.records.items() if k.endswith("::" + name)]
            if len(hits) == 1:
                rec = hits[0]
        if rec is None:
            raise SystemExit("no layout for %s (add its header to spec.HEADERS)" % name)
        return rec

    def size(self, name):
        return self.find(name).size

    def field(self, struct, member):
        """(offset, width) of a member, by its dotted path below the struct."""
        want = struct + "." + member
        leaves = [(off, width, what.split("|")[0]) for off, width, what in self.leaves(struct)]
        for off, width, what in leaves:
            if what == want or what.endswith("(base)." + member) or what.split(".(base)")[-1] == "." + member:
                return off, width
        for off, width, what in leaves:
            if what.rsplit(".", 1)[-1] == member:
                return off, width
        raise SystemExit("%s has no member %s" % (struct, member))

    def leaves(self, name):
        rec = self.find(name)
        out = []
        self._flatten(rec, 0, len(rec.lines), 1, name, out, (name, 0, name))
        return out

    @staticmethod
    def _q(what, owner):
        """A leaf's name, then "|" and its name in the innermost named struct
        around it ("zUITextAsset.(base).color|zUIAsset.color"), which the
        spec's field rules may use instead."""
        if owner[2] and not what.startswith(owner[2]):
            return what
        return "%s|%s%s" % (what, owner[0], what[len(owner[2]):])

    @staticmethod
    def width(typ):
        t = typ.replace("const ", "").replace("volatile ", "").strip()
        if t.endswith("*") or "(*)" in t or t == "vptr" or t.endswith("&"):
            return 4
        if t in SCALARS:
            return SCALARS[t]
        if t.startswith("enum "):
            return 4
        return None

    @staticmethod
    def array(typ):
        m = re.match(r"^(.*?)((?:\[\d+\])+)$", typ)
        if not m:
            return None
        n = 1
        for d in re.findall(r"\[(\d+)\]", m.group(2)):
            n *= int(d)
        return m.group(1).strip(), n

    def _flatten(self, rec, start, end, depth, path, out, owner):
        """owner: the innermost named struct around these lines, its offset
        and its path, which a union's rule and a leaf's qualified name use."""
        i = start
        while i < end:
            off, bits, d, typ, field = rec.lines[i]
            if d != depth:
                i += 1
                continue
            j = i + 1
            while j < end and rec.lines[j][2] > depth:
                j += 1
            what = "%s.%s" % (path, field)
            if bits is not None and bits == (0, 7):
                # A bitfield that fills its byte is that byte on either
                # packing: the first field is the first byte both ways.
                out.append((off, 1, self._q(what, owner)))
            elif bits is not None:
                out.append((off, 0, what + ":bits"))
            else:
                self._member(rec, off, typ, what, i, j, depth, out, owner)
            i = j

    def _member(self, rec, off, typ, what, i, j, depth, out, owner):
        w = self.width(typ)
        if w is not None:
            out.append((off, w, self._q(what, owner)))
            return
        arr = self.array(typ)
        if arr is not None:
            elem, n = arr
            ew = self.width(elem)
            if ew is not None:
                for k in range(n):
                    out.append((off + k * ew, ew, self._q("%s[%d]" % (what, k), owner)))
                return
            sub = self.find(strip_record(elem))
            part = []
            self._flatten(sub, 0, len(sub.lines), 1, "", part, (sub.name, 0, ""))
            for k in range(n):
                out += [(off + k * sub.size + o, wd, "%s[%d]%s" % (what, k, nm)) for o, wd, nm in part]
            return
        if typ.startswith("union "):
            self._union(rec, off, typ, what, i, j, depth, out, owner)
            return
        if typ.startswith(("struct ", "class ")):
            if "(anonymous" not in typ:
                owner = (strip_record(typ), off, what)
            self._flatten(rec, i + 1, j, depth + 1, what, out, owner)
            return
        raise SystemExit("%s: cannot lay out type %r" % (what, typ))

    def _union(self, rec, off, typ, what, i, j, depth, out, owner):
        members = []
        k = i + 1
        while k < j:
            moff, bits, d, mtyp, mfield = rec.lines[k]
            m_end = k + 1
            while m_end < j and rec.lines[m_end][2] > depth + 1:
                m_end += 1
            if d == depth + 1:
                part = []
                if bits is not None and bits == (0, 7):
                    part.append((moff, 1, "%s.%s" % (what, mfield)))
                elif bits is not None:
                    part.append((moff, 0, "%s.%s:bits" % (what, mfield)))
                else:
                    self._member(rec, moff, mtyp, "%s.%s" % (what, mfield), k, m_end, depth + 1, part,
                                 owner)
                members.append(part)
            k = m_end
        # The members agree when any two of their leaves that overlap are the
        # same field: same offset, same width. A byte against a word does not.
        merged = {}
        ok = True
        for part in members:
            for o, wd, nm in part:
                if wd == 0 or nm.endswith(":union"):
                    ok = False
                    continue
                if o in merged and merged[o][0] == wd:
                    continue
                for o2, (w2, _) in merged.items():
                    if o < o2 + w2 and o2 < o + wd:
                        ok = False
                merged.setdefault(o, (wd, nm))
        if ok:
            out += sorted((o, wd, nm) for o, (wd, nm) in merged.items())
        else:
            name = strip_record(typ)
            r = self.records.get(name)
            size = r.size if r is not None and r.size is not None else 0
            if size == 0:
                end = off
                for o, bits, d, t, f in rec.lines[i + 1:j]:
                    end = max(end, o + (self.width(t) or 0))
                size = end - off
            # Named for the struct the union is in, and its offset there.
            out.append((off, size, "%s:union@%s@%X" % (what, owner[0], off - owner[1])))


if __name__ == "__main__":
    import spec
    names = sys.argv[1:]
    L = Layouts(spec.HEADERS, names, getattr(spec, "EXTRA_SOURCE", ""))
    for n in names:
        print("%s: 0x%X bytes" % (n, L.size(n)))
        for off, w, what in L.leaves(n):
            print("  +%03X %d %s" % (off, w, what))
