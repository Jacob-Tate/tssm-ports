"""Generate src/SB/Core/pc/iGCSwapTable.inc from spec.py and the decomp's headers.

    python tools/gcswap/gen.py

The output is a swap function per struct -- one gcsSwap16/32/64 per scalar at
the offset clang gives it in the 32-bit layout (layouts.py) -- one per asset
type that walks the type's steps, and iGCSwapGenerated, which dispatches on the
packer's type code. iGCSwap.cpp includes it.

After the method of the BfBB PC port's tools/gcswap (CC0); the data is TSSM's.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layouts
import spec

OUT = os.path.join(layouts.ROOT, "src", "SB", "Core", "pc", "iGCSwapTable.inc")
DYNA_BASE = 0x10  # sizeof(xDynAsset)
SWAP = {2: "gcsSwap16", 4: "gcsSwap32", 8: "gcsSwap64"}


def xstrhash(s):
    h = 0
    for ch in s.encode("ascii"):
        h = (((ch - (ch & (ch >> 1) & 0x20)) & 0xFF) + h * 0x83) & 0xFFFFFFFF
    return h


def fourcc(t):
    return "0x%08X" % int.from_bytes(t.encode("ascii"), "big")


def ident(t):
    return re.sub(r"[^A-Za-z0-9_]", "_", t.strip())


def short(what):
    """A leaf's name without the anonymous-member noise, for comments."""
    return what.split("|")[0].replace(".(anon)", "").replace(".(base)", "")


def qualified(what):
    """The leaf's name in the innermost named struct around it."""
    q = what.split("|")[1] if "|" in what else what
    return q.split(":")[0].replace(".(anon)", "").replace(".(base)", "")


class Gen:
    def __init__(self):
        names = set(spec.EXTRA_STRUCTS)
        for steps in list(spec.TYPES.values()) + list(spec.DYNA.values()):
            for s in steps:
                names |= self.step_structs(s)
        names |= set(getattr(spec, "EXTRA_BOUNDED", []))
        for rule in spec.UNIONS.values():
            for case in rule.get("cases", {}).values():
                if isinstance(case, str) and case not in ("u32", "bytes"):
                    names.add(case)
        names |= {"xLinkAsset", "xDynAsset"}
        self.L = layouts.Layouts(spec.HEADERS, sorted(names), getattr(spec, "EXTRA_SOURCE", ""))
        self.structs = {}
        self.fields = {}

    @staticmethod
    def step_structs(step):
        if isinstance(step, str):
            return {step}
        if step[0] == "array" and step[1] not in ("u16", "u32", "u8"):
            return {step[1]}
        if step[0] in ("tagged", "select"):
            cases = step[2] if not isinstance(step[2], str) else getattr(spec, step[2])
            return {c for c in cases.values() if isinstance(c, str)}
        return set()

    def swap_lines(self, struct, leaves, skip_below=0, indent="    "):
        out = []
        for off, width, what in leaves:
            if off < skip_below:
                continue
            key = short(what)
            qkey = qualified(what)
            if key in spec.AS_IS or qkey in spec.AS_IS:
                out.append("%s// +0x%X %s: as on the disc (spec.AS_IS)" % (indent, off, qkey))
                continue
            either = spec.EITHER.get(key) or spec.EITHER.get(qkey)
            if either is not None:
                out.append("%sgcsSwap32Either%s(p + 0x%X); // %s: see spec.EITHER" % (
                    indent, either.capitalize(), off, qkey))
                continue
            if what.endswith(":bits"):
                raise SystemExit("%s is a bitfield and needs a rule" % key)
            if ":union@" in what:
                _, owner, rel = what.split("@")
                out += self.union_lines(struct, owner, int(rel, 16), off, width, key, indent)
                continue
            if width == 1:
                continue
            out.append("%s%s(p + 0x%X);" % (indent, SWAP[width], off))
        return out

    def union_lines(self, top, struct, rel, off, size, what, indent):
        """The union at `off` in the struct being swapped, which is at `rel`
        in its own struct `struct`; spec.UNIONS is keyed by (struct, rel)."""
        rule = spec.UNIONS.get((struct, rel))
        if rule is None and (top, off) in spec.UNIONS:
            rule, struct, rel = spec.UNIONS[(top, off)], top, off
        if rule is None:
            raise SystemExit("%s: the union at 0x%X (%s) needs a rule in spec.UNIONS" % (struct, rel, what))
        if "fixed" in rule:
            return ["%s%s(p + 0x%X); // %s, spec.UNIONS" % (indent, SWAP[w], off + o, what)
                    for o, w in rule["fixed"] if w > 1]
        sel_rel, sel_width = self.L.field(struct, rule["selector"])
        if sel_rel >= rel:
            raise SystemExit("%s: the selector of the union at 0x%X comes after it" % (struct, rel))
        sel_off = off - rel + sel_rel
        read = {1: "gcsRead8", 2: "gcsRead16", 4: "gcsRead32"}[sel_width]
        out = ["%sswitch (%s(p + 0x%X)) // %s.%s" % (indent, read, sel_off, struct, rule["selector"]),
               "%s{" % indent]
        for value, member in sorted(rule["cases"].items()):
            out.append("%scase %d:" % (indent, value))
            if member == "u32":
                out.append("%s    for (U32 w = 0; w + 4 <= 0x%X; w += 4)" % (indent, size))
                out.append("%s        gcsSwap32(p + 0x%X + w);" % (indent, off))
            elif member == "bytes":
                pass
            elif isinstance(member, list):
                for o, w in member:
                    out.append("%s    %s(p + 0x%X);" % (indent, SWAP[w], off + o))
            else:
                if self.L.size(member) > size:
                    raise SystemExit("%s is larger than the union in %s" % (member, struct))
                out.append("%s    %s(p + 0x%X);" % (indent, self.struct_fn(member), off))
            out.append("%s    break;" % indent)
        default = rule.get("default", "u32")
        out.append("%sdefault:" % indent)
        if default == "u32":
            out.append("%s    for (U32 w = 0; w + 4 <= 0x%X; w += 4)" % (indent, size))
            out.append("%s        gcsSwap32(p + 0x%X + w);" % (indent, off))
        out.append("%s    break;" % indent)
        out.append("%s}" % indent)
        return out

    def struct_fn(self, name, skip_below=0, bounded=False):
        """The C++ function that swaps struct `name` at p. The bounded form
        takes the bytes present, n, and swaps only the fields that fit: the
        tail of an asset older and shorter than its struct."""
        key = (name, skip_below, bounded)
        fn = "gcs_%s%s%s" % (ident(name), "_from%X" % skip_below if skip_below else "",
                             "_upto" if bounded else "")
        if key in self.structs:
            return fn
        self.structs[key] = None
        size = self.L.size(name)
        body = []
        for leaf in self.L.leaves(name):
            lines = self.swap_lines(name, [leaf], skip_below)
            if bounded and lines and not lines[0].lstrip().startswith("//"):
                need = leaf[0] + leaf[1]
                lines = ["    if (n >= 0x%X)" % need, "    {"] + ["    " + l for l in lines] + ["    }"]
            body += lines
        sig = "U8* p, U32 n" if bounded else "U8* p"
        unused = ""
        if not any(l.strip() and not l.strip().startswith("//") for l in body):
            unused += "    (void)p;\n"
        if bounded and not any("n >=" in l for l in body):
            unused += "    (void)n;\n"
        self.structs[key] = "static void %s(%s) // %s, 0x%X bytes\n{\n%s%s%s}\n" % (
            fn, sig, name, size, "\n".join(body), "\n" if body else "", unused)
        return fn

    def fields_fn(self, name, size, leaves, skip_below=0):
        fn = "gcs_fields_%s" % ident(name)
        if fn not in self.fields:
            body = []
            for o, w in leaves:
                if o >= skip_below and w > 1:
                    body.append("    %s(p + 0x%X);" % (SWAP[w], o))
            if not body:
                body = ["    (void)p;"]
            self.fields[fn] = "static void %s(U8* p) // 0x%X bytes, from spec.py\n{\n%s\n}\n" % (
                fn, size, "\n".join(body))
        return fn

    def count_expr(self, count, fixed):
        if isinstance(count, int):
            return str(count)
        sname, mname = count.split(".", 1)
        moff, msize = self.L.field(sname, mname)
        if sname not in fixed:
            raise SystemExit("%s is not at a fixed offset" % sname)
        return "gcsRead%d(p + 0x%X)" % (msize * 8, fixed[sname] + moff)

    def steps(self, steps, lines, fixed, skip_below=0):
        at = 0
        known = True
        for step in steps:
            if isinstance(step, str) or step[0] == "fields":
                if isinstance(step, str):
                    size = self.L.size(step)
                    fn = self.struct_fn(step, skip_below)
                    name = step
                    lines.append("    if (c + 0x%X > end) // an older, shorter asset: what is there" % size)
                    lines.append("    {")
                    lines.append("        if (c < end) %s(p + c, end - c);" %
                                 self.struct_fn(step, skip_below, True))
                    lines.append("        return 1;")
                    lines.append("    }")
                else:
                    _, name, size, leaves = step
                    fn = self.fields_fn(name, size, leaves, skip_below)
                    lines.append("    if (c + 0x%X > end) return 0;" % size)
                lines.append("    %s(p + c);" % fn)
                lines.append("    c += 0x%X;" % size)
                if known:
                    fixed[name] = at
                    at += size
                skip_below = 0
            elif step[0] == "array":
                _, elem, count = step
                esize = {"u8": 1, "u16": 2, "u32": 4}.get(elem) or self.L.size(elem)
                if count == "rest":
                    lines.append("    for (; c + 0x%X <= end; c += 0x%X)" % (esize, esize))
                    lines.append("        %s;" % self.elem_call(elem))
                else:
                    lines.append("    {")
                    lines.append("        U32 n = %s;" % self.count_expr(count, fixed))
                    lines.append("        if (n > (end - c) / 0x%X) return 0;" % esize)
                    lines.append("        for (U32 k = 0; k < n; k++, c += 0x%X)" % esize)
                    lines.append("            %s;" % self.elem_call(elem))
                    lines.append("    }")
                known = False
            elif step[0] == "tagged":
                _, count, cases = step
                if isinstance(cases, str):
                    cases = getattr(spec, cases)
                lines.append("    {")
                lines.append("        U32 n = %s;" % self.count_expr(count, fixed))
                lines.append("        for (U32 k = 0; k < n; k++)")
                lines.append("        {")
                lines.append("            if (c + 4 > end) return 0;")
                lines.append("            switch (gcsReadBE32(p + c))")
                lines.append("            {")
                for tag, case in sorted(cases.items()):
                    if isinstance(case, int):
                        lines.append("            case %d: // the header and words" % tag)
                        lines.append("                if (c + 0x%X > end) return 0;" % case)
                        lines.append("                gcsSwap32(p + c);")
                        lines.append("                for (U32 w = 8; w < 0x%X; w += 4)" % case)
                        lines.append("                    gcsSwap32(p + c + w);")
                        lines.append("                c += 0x%X;" % case)
                    else:
                        size = self.L.size(case)
                        lines.append("            case %d: // %s" % (tag, case))
                        lines.append("                if (c + 0x%X > end) return 0;" % size)
                        lines.append("                %s(p + c);" % self.struct_fn(case))
                        lines.append("                c += 0x%X;" % size)
                    lines.append("                break;")
                lines.append("            default:")
                lines.append("                return 0;")
                lines.append("            }")
                lines.append("        }")
                lines.append("    }")
                known = False
            elif step[0] == "select":
                # One struct, picked by a field of a struct placed earlier;
                # nothing for a value with no case.
                _, sel, cases = step
                lines.append("    switch (%s)" % self.count_expr(sel, fixed))
                lines.append("    {")
                for value, case in sorted(cases.items()):
                    size = self.L.size(case)
                    lines.append("    case %d: // %s" % (value, case))
                    lines.append("        if (c + 0x%X > end) return 0;" % size)
                    lines.append("        %s(p + c);" % self.struct_fn(case))
                    lines.append("        c += 0x%X;" % size)
                    lines.append("        break;")
                lines.append("    default:")
                lines.append("        break;")
                lines.append("    }")
                known = False
            elif step[0] == "words":
                if skip_below:
                    lines.append("    if (c < 0x%X) c = 0x%X;" % (skip_below, skip_below))
                lines.append("    for (; c + 4 <= end; c += 4)")
                lines.append("        gcsSwap32(p + c);")
                known = False
            elif step[0] == "bytes":
                lines.append("    c = end; // bytes, nothing to swap")
                known = False
            elif step[0] == "custom":
                lines.append("    return %s(p, end, c);" % step[1])
                return
            else:
                raise SystemExit("unknown step %r" % (step,))
        lines.append("    (void)c;")
        lines.append("    return 1;")

    def elem_call(self, elem):
        if elem == "u8":
            return "(void)0"
        if elem == "u16":
            return "gcsSwap16(p + c)"
        if elem == "u32":
            return "gcsSwap32(p + c)"
        return "%s(p + c)" % self.struct_fn(elem)

    def type_fn(self, t, steps):
        lines = ["static S32 gcs_type_%s(U8* p, U32 size)" % ident(t), "{"]
        if t in spec.NOBASE:
            lines.append("    U32 end = size;")
        elif t in spec.LINKS_FIRST:
            lines.append("    U32 end = size; // its links are not at the end; the custom step swaps them")
        else:
            lines.append("    U32 end;")
            lines.append("    if (!gcsSwapLinks(p, size, &end)) return 0;")
        lines.append("    U32 c = 0;")
        self.steps(steps, lines, {})
        lines.append("}")
        return "\n".join(lines) + "\n"

    def dyna_fn(self):
        lines = ["static S32 gcs_type_DYNA(U8* p, U32 size)", "{",
                 "    U32 end;",
                 "    if (size < 0x%X) return 0;" % DYNA_BASE,
                 "    if (!gcsSwapLinks(p, size, &end)) return 0;",
                 "    if (end < 0x%X) return 0;" % DYNA_BASE,
                 "    %s(p);" % self.struct_fn("xDynAsset"),
                 "    U32 c = 0;",
                 "    switch (gcsRead32(p + 0x8))",
                 "    {"]
        for name, steps in spec.DYNA.items():
            h = name if isinstance(name, int) else xstrhash(name)
            label = spec.DYNA_NAMES.get(h, "?") if isinstance(name, int) else name
            lines.append("    case 0x%08XU: // %s" % (h, label))
            lines.append("    {")
            body = []
            self.steps(steps, body, {}, DYNA_BASE)
            lines += ["    " + l for l in body]
            lines.append("    }")
        lines.append("    }")
        lines.append("    return -1; // a subtype with no rule")
        lines.append("}")
        return "\n".join(lines) + "\n"

    def run(self):
        self.struct_fn("xLinkAsset")
        for name in spec.EXTRA_STRUCTS:
            self.struct_fn(name)
        for name in getattr(spec, "EXTRA_BOUNDED", []):
            self.struct_fn(name, 0, True)
        types = []
        for t, steps in spec.TYPES.items():
            types.append((t, self.type_fn(t, steps)))
        types.append(("DYNA", self.dyna_fn()))

        out = ["// Generated by tools/gcswap/gen.py from tools/gcswap/spec.py and the",
               "// decomp's headers (32-bit layouts). Do not edit; change the spec and",
               "// regenerate.", ""]
        emitted = sorted(self.structs.items(), key=lambda kv: (kv[0][0], kv[0][1], kv[0][2]))
        out += [text.splitlines()[0].split(" //")[0] + ";" for _, text in emitted]
        out.append("")
        out += [text for _, text in emitted]
        out += [self.fields[k] for k in sorted(self.fields)]
        out += [fn for _, fn in types]
        out.append("static S32 iGCSwapGenerated(U32 type, U8* p, U32 size)")
        out.append("{")
        out.append("    switch (type)")
        out.append("    {")
        for t, _ in sorted(types):
            out.append("    case %s: return gcs_type_%s(p, size); // '%s'" % (fourcc(t), ident(t), t))
        out.append("    }")
        out.append("    return -1;")
        out.append("}")
        out.append("")
        out.append("static bool iGCSwapGeneratedKnows(U32 type)")
        out.append("{")
        out.append("    switch (type)")
        out.append("    {")
        for t, _ in sorted(types):
            out.append("    case %s: // '%s'" % (fourcc(t), t))
        out.append("        return true;")
        out.append("    }")
        out.append("    return false;")
        out.append("}")
        out.append("")
        out.append("// The types read as they are: RenderWare streams, sound data, and the")
        out.append("// ones the GameCube disc ships little-endian already.")
        out.append("static bool iGCSwapGeneratedAsIs(U32 type)")
        out.append("{")
        out.append("    switch (type)")
        out.append("    {")
        for t in sorted(spec.AS_IS_TYPES):
            out.append("    case %s: // '%s'" % (fourcc(t), t))
        out.append("        return true;")
        out.append("    }")
        out.append("    return false;")
        out.append("}")
        text = "\n".join(out) + "\n"
        with open(OUT, "w", newline="\n") as f:
            f.write(text)
        print("%s: %d struct swaps, %d types, %d DYNA subtypes, %d types as they are" % (
            os.path.relpath(OUT, layouts.ROOT), len(self.structs), len(types), len(spec.DYNA),
            len(spec.AS_IS_TYPES)))


if __name__ == "__main__":
    Gen().run()
