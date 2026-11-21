#!/usr/bin/env python3
"""How much of TSSM compiles -- and links -- for the PC port.

The GameCube build has report.json, which scores bytes against the retail DOL.
A port has nothing to compare bytes against, so this answers the questions it
can: does each unit compile against the PC platform layer, and when the units
that do compile are linked, what is still undefined?

Wad-aware. TSSM compiles its game code as unity "wads" (xWad1.cpp #includes a
few dozen .cpp files) and the PC build compiles the same units (CMakeLists.txt
reads them out of configure.py), so a unit here is a wad. Each diagnostic is
attributed to the file it names, so `--files` says which included .cpp or
header the work is in, counted once per location however many wads include it.

It reads the build directory's compile_commands.json, so it measures exactly
what CMake compiles: configure a build directory first (build-debug.bat, or
`cmake -S . -B build-pc -G Ninja ...`). Each unit is compiled again here, to an
object of its own, with no error limit.

Usage:
    tools/pcprogress.py                       summary + per-unit table
    tools/pcprogress.py --errors 3            plus the first 3 errors of each unit
    tools/pcprogress.py --files               errors per source file (deduplicated)
    tools/pcprogress.py --link                plus a link of what compiled: the
                                              undefined symbols, grouped
    tools/pcprogress.py --json out.json       everything, machine-readable
    tools/pcprogress.py zWad1 iFile           only units matching these fragments
    --build DIR   the CMake build directory (default build-pc)
    --jobs N      parallel compiles (default: CPU count)

Adapted from treedome's tools/pcprogress.py and tools/pclink.py (CC0).
"""

import argparse
import collections
import concurrent.futures
import json
import os
import pathlib
import re
import shlex
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# The targets CMakeLists.txt builds from this repository's own sources, in the
# order they are reported. librw and SDL are third-party and not measured.
TARGETS = [
    ("game", "CMakeFiles/tssm_game.dir/"),
    ("platform", "CMakeFiles/tssm_platform.dir/"),
    ("rw", "CMakeFiles/tssm_rw.dir/"),
    ("exe", "CMakeFiles/tssm.dir/"),
]

DIAG = re.compile(r"^(?P<file>.*?):(?P<line>\d+):(?P<col>\d+): (?P<kind>error|fatal error|warning): (?P<msg>.*)$")

# Pointer width. clang targeting the MSVC ABI accepts `(U32)pointer` with a
# warning (Microsoft does), so a 64-bit Windows build compiles code that
# truncates pointers; on Linux and Android the same cast is an error. These are
# switched back on (after the build's -Wno-everything) and counted apart from
# the errors, so the number means the same thing on every host -- treedome's
# policy, for the same reason. They are what a 64-bit build has to look at;
# a 32-bit build does not raise them for U32 at all.
WIDTH_WARNINGS = ["pointer-to-int-cast", "void-pointer-to-int-cast", "int-to-pointer-cast"]
DRIVER_ERR = re.compile(r"^clang(?:\+\+)?(?:\.exe)?: error: (?P<msg>.*)$")


def rel(path, cwd):
    p = pathlib.Path(path)
    if not p.is_absolute():
        p = pathlib.Path(cwd) / p
    try:
        return p.resolve().relative_to(ROOT).as_posix()
    except (ValueError, OSError):
        return p.as_posix()


def split_command(entry):
    if "arguments" in entry:
        return list(entry["arguments"])
    # compile_commands.json from CMake on Windows: a command line. shlex in
    # POSIX mode eats backslashes, so protect them first. An escaped quote
    # (-DTSSM_GUI_BRANCH="\"pc ...\"") is a literal quote in the argument.
    cmd = entry["command"].replace('\\"', "\x01").replace("\\", "\x00")
    args = shlex.split(cmd, posix=True)
    return [a.replace("\x00", "\\").replace("\x01", '"') for a in args]


def prepare(entry, objdir):
    """The unit's compile command, writing to our own object, no error limit."""
    args = split_command(entry)
    out = []
    skip = 0
    for i, a in enumerate(args):
        if skip:
            skip -= 1
            continue
        if a in ("-o", "-MT", "-MF"):
            skip = 1
            continue
        if a in ("-MD", "-MMD"):
            continue
        out.append(a)
    unit_rel = entry["output"]
    obj = objdir / (pathlib.Path(unit_rel).as_posix().replace("CMakeFiles/", "").replace("/", "__"))
    # The source is the last argument CMake writes; keep it last.
    src = out.pop()
    out += ["-ferror-limit=0", "-fno-color-diagnostics", "-fno-caret-diagnostics"]
    out += ["-W" + w for w in WIDTH_WARNINGS]
    out += ["-o", str(obj), src]
    return out, obj


def unit_name(entry):
    src = rel(entry["file"], entry["directory"])
    return src


def compile_unit(job):
    group, entry, args, obj = job
    if obj.exists():
        obj.unlink()
    proc = subprocess.run(args, cwd=entry["directory"], capture_output=True, text=True,
                          errors="replace")
    errors = []
    width = []
    for line in (proc.stderr or "").splitlines():
        m = DIAG.match(line)
        if m and m.group("kind") == "warning":
            if any("-W" + w + "]" in m.group("msg") for w in WIDTH_WARNINGS):
                width.append({"file": rel(m.group("file"), entry["directory"]),
                              "line": int(m.group("line")), "msg": m.group("msg")})
            continue
        if m:
            errors.append({
                "file": rel(m.group("file"), entry["directory"]),
                "line": int(m.group("line")),
                "kind": m.group("kind"),
                "msg": m.group("msg"),
            })
            continue
        m = DRIVER_ERR.match(line)
        if m:
            errors.append({"file": "<driver>", "line": 0, "kind": "error", "msg": m.group("msg")})
    ok = proc.returncode == 0 and obj.exists()
    return {
        "group": group,
        "unit": unit_name(entry),
        "ok": ok,
        "returncode": proc.returncode,
        "errors": errors,
        "width": width,
        "obj": str(obj) if ok else None,
    }


def area(path):
    if path.startswith("src/SB/Core/pc/"):
        return "pc"
    if path.startswith("src/SB/Core/x/") or path.startswith("src/SB/Game/"):
        return "game"
    if path.startswith("include/"):
        return "include"
    return "other"


def parse_ninja_link(builddir):
    """FLAGS, LINK_FLAGS and LINK_LIBRARIES of the tssm executable."""
    text = (builddir / "build.ninja").read_text(errors="replace")
    m = re.search(r"^build tssm\.exe: .*?$\n((?:  .*\n)+)", text, re.M)
    if m is None:
        m = re.search(r"^build tssm: .*?$\n((?:  .*\n)+)", text, re.M)
    if m is None:
        return None
    vars_ = {}
    for line in m.group(1).splitlines():
        k, _, v = line.strip().partition(" = ")
        vars_[k] = v
    return vars_


SYMBOL_CLASSES = [
    ("Dolphin SDK", re.compile(r"\b(GX|OS|VI|CARD|DVD|PAD|ARQ?|DC|PSMTX|C_MTX|__OS)[A-Z_]")),
    ("FMOD", re.compile(r"\b(FSOUND|FMUSIC)_")),
    ("Bink", re.compile(r"\bBink[A-Z]")),
    ("RenderWare", re.compile(r"\b(_?R[wpxt][A-Z]|_rw|_rp|_RwDl|_rpDl)")),
    ("MSL/CodeWarrior runtime", re.compile(r"\b(__construct|__destroy|__register_global|__cvt_|__ctype_map|__upper_map)")),
]


def symbol_name(demangled):
    """`float __cdecl iCameraGetFOV(struct RwCamera *)` -> `iCameraGetFOV`;
    `struct RwCamera *zHud::HudCamera` -> `zHud::HudCamera`. lld prints
    demangled names; the parameters must not decide the class."""
    s = demangled.strip()
    depth = 0
    cut = len(s)
    # the first '(' outside template brackets starts the parameter list
    for i, ch in enumerate(s):
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif ch == "(" and depth == 0:
            cut = i
            break
    head = s[:cut].strip()
    return re.split(r"[ *&]", head)[-1] if head else s


def classify(sym):
    name = symbol_name(sym)
    for cls, pat in SYMBOL_CLASSES:
        if pat.search(name):
            return cls
    base = name.split("::")[-1]
    if re.match(r"i[A-Z]", name) or re.match(r"i[A-Z]", base):
        return "platform layer (i*)"
    return "game code (x*/z*)"


def wad_files(unit_path):
    """The .cpp files a unit compiles: itself and, for a wad, what it includes."""
    src = ROOT / unit_path
    files = [src]
    try:
        text = src.read_text(errors="replace")
    except OSError:
        return files
    for inc in re.findall(r'^\s*#include\s+"([^"]+\.cpp)"', text, re.M):
        for d in (src.parent, ROOT / "src/SB/Core/pc"):
            p = d / inc
            if p.exists():
                files.append(p)
                break
    return files


def defined_in_failed(names, results):
    """Which undefined names a unit that did not compile appears to define.

    A heuristic over the source text (a definition starts at column 0 and
    names the symbol), good enough to separate "missing because its unit did
    not compile" from "missing everywhere"."""
    owner = {}
    texts = []
    for r in results:
        if r["ok"]:
            continue
        for f in wad_files(r["unit"]):
            try:
                texts.append((r["unit"], f.read_text(errors="replace")))
            except OSError:
                pass
    for name in names:
        base = name.split("::")[-1]
        if not re.match(r"^[A-Za-z_]\w*$", base):
            continue
        pat = re.compile(r"^(?![ \t#/])[^;\n]*\b" + re.escape(base) + r"\b\s*(\(|\[|=|;)", re.M)
        for unit, text in texts:
            if pat.search(text):
                owner[name] = unit
                break
    return owner


def link(builddir, results, outdir):
    vars_ = parse_ninja_link(builddir)
    if vars_ is None:
        return {"error": "no tssm link step in build.ninja"}
    objs = [r["obj"] for r in results if r["ok"]]
    cxx = None
    cc = json.loads((builddir / "compile_commands.json").read_text())
    for e in cc:
        cxx = split_command(e)[0]
        break
    libs = [a for a in shlex.split(vars_.get("LINK_LIBRARIES", "").replace("\\", "/"))
            if not re.match(r"tssm_(game|platform|rw)\.lib$", a)]
    out = outdir / "linktest.exe"
    args = ([cxx] + shlex.split(vars_.get("FLAGS", "")) + objs + ["-o", str(out)] +
            shlex.split(vars_.get("LINK_FLAGS", "").replace("\\", "/")) + libs +
            ["-Xlinker", "/errorlimit:0",
             # A duplicate symbol stops lld-link before it reports anything
             # undefined. Measured with /force:multiple, so both lists come
             # out; the duplicates are reported as found either way, and the
             # game build itself does not use the flag.
             "-Xlinker", "/force:multiple"])
    proc = subprocess.run(args, cwd=builddir, capture_output=True, text=True, errors="replace")
    text = (proc.stdout or "") + (proc.stderr or "")
    undefined = collections.OrderedDict()
    duplicate = collections.OrderedDict()
    current = None
    for line in text.splitlines():
        m = re.search(r"error: undefined symbol: (.*)$", line)
        if m:
            current = undefined.setdefault(m.group(1).strip(), [])
            continue
        m = re.search(r"(?:error|warning): duplicate symbol: (.*?)( in .*)?$", line)
        if m:
            current = duplicate.setdefault(m.group(1).strip(), [])
            continue
        m = re.search(r">>> (?:referenced by|defined at) (.*)$", line)
        if m and current is not None:
            current.append(m.group(1).strip())
    return {
        "returncode": proc.returncode,
        "objects": len(objs),
        "undefined": {k: v for k, v in undefined.items()},
        "duplicate": {k: v for k, v in duplicate.items()},
        "raw_tail": text.splitlines()[-5:],
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default="build-pc")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--errors", type=int, default=0)
    ap.add_argument("--files", action="store_true")
    ap.add_argument("--link", action="store_true")
    ap.add_argument("--json")
    ap.add_argument("filters", nargs="*")
    args = ap.parse_args()

    builddir = pathlib.Path(args.build)
    if not builddir.is_absolute():
        builddir = ROOT / builddir
    ccpath = builddir / "compile_commands.json"
    if not ccpath.exists():
        sys.exit(f"{ccpath} not found -- configure a build directory first (see the docstring)")
    entries = json.loads(ccpath.read_text())

    objdir = builddir / "pcprogress"
    objdir.mkdir(exist_ok=True)

    def output_rel(e):
        # CMake writes `output` relative to the build directory or absolute,
        # depending on the version.
        out = pathlib.Path(e["output"])
        if out.is_absolute():
            try:
                out = out.relative_to(builddir)
            except ValueError:
                pass
        return out.as_posix()

    jobs = []
    for group, prefix in TARGETS:
        for e in entries:
            if not output_rel(e).startswith(prefix):
                continue
            e = dict(e)
            e["output"] = output_rel(e)
            name = unit_name(e)
            if args.filters and not any(f.lower() in name.lower() for f in args.filters):
                continue
            cmd, obj = prepare(e, objdir)
            jobs.append((group, e, cmd, obj))

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        results = list(ex.map(compile_unit, jobs))

    order = {g: i for i, (g, _) in enumerate(TARGETS)}
    results.sort(key=lambda r: (order[r["group"]], r["unit"]))

    # --- per unit
    print(f"{'unit':<52} {'ok':>4} {'errors':>7} {'game':>6} {'pc':>5} {'incl':>5} {'width':>6}  first error")
    for r in results:
        by_area = collections.Counter(area(e["file"]) for e in r["errors"])
        first = r["errors"][0] if r["errors"] else None
        first_s = f"{first['file'].split('/')[-1]}:{first['line']}: {first['msg'][:60]}" if first else ""
        print(f"{r['unit']:<52} {'yes' if r['ok'] else 'NO':>4} {len(r['errors']):>7} "
              f"{by_area['game']:>6} {by_area['pc']:>5} {by_area['include']:>5} {len(r['width']):>6}  {first_s}")
        for e in r["errors"][: args.errors]:
            print(f"      {e['file']}:{e['line']}: {e['msg']}")

    # --- summary
    print()
    for group, _ in TARGETS:
        rs = [r for r in results if r["group"] == group]
        if not rs:
            continue
        ok = sum(1 for r in rs if r["ok"])
        n_err = sum(len(r["errors"]) for r in rs)
        n_w = sum(len(r["width"]) for r in rs)
        print(f"{group:<9} {ok:>3} / {len(rs):<3} units compile   {n_err:>6} errors, {n_w:>5} pointer-width "
              f"casts (with repeats across units)")

    # --- per file, deduplicated by location
    seen = set()
    per_file = collections.Counter()
    per_msg = collections.Counter()
    for r in results:
        for e in r["errors"]:
            key = (e["file"], e["line"], e["msg"])
            if key in seen:
                continue
            seen.add(key)
            per_file[e["file"]] += 1
            per_msg[re.sub(r"'[^']*'", "'X'", e["msg"])] += 1
    wseen = set()
    w_per_file = collections.Counter()
    for r in results:
        for w in r["width"]:
            key = (w["file"], w["line"], w["msg"])
            if key not in wseen:
                wseen.add(key)
                w_per_file[w["file"]] += 1
    print(f"\n{len(seen)} distinct error sites in {len(per_file)} files; "
          f"{len(wseen)} distinct pointer-width casts in {len(w_per_file)} files")
    if args.files:
        print("\nerrors per file (distinct sites):")
        for f, n in per_file.most_common():
            print(f"  {n:>5}  {f}")
        print("\npointer-width casts per file (distinct sites):")
        for f, n in w_per_file.most_common():
            print(f"  {n:>5}  {f}")
        print("\nmost common messages (identifiers replaced by X):")
        for m, n in per_msg.most_common(25):
            print(f"  {n:>5}  {m}")

    linkres = None
    if args.link:
        linkres = link(builddir, results, objdir)
        print()
        if "error" in linkres:
            print("link:", linkres["error"])
        else:
            und = linkres["undefined"]
            dup = linkres["duplicate"]
            owner = defined_in_failed([symbol_name(s) for s in und], results)
            linkres["defined_in_failed_unit"] = {s: owner.get(symbol_name(s)) for s in und}
            linkres["class"] = {s: classify(s) for s in und}
            pending = [s for s in und if owner.get(symbol_name(s)) is None]
            print(f"link of {linkres['objects']} compiled objects: {len(und)} undefined symbols, "
                  f"{len(dup)} duplicate symbols")
            print(f"  {len(und) - len(pending):>5}  defined (by the source text) in a unit that did not compile")
            classes = collections.Counter(classify(s) for s in pending)
            for c, n in classes.most_common():
                print(f"  {n:>5}  {c}")
            if args.errors or args.files:
                print("\nundefined, not explained by a failed unit:")
                for s in sorted(pending, key=lambda s: (classify(s), symbol_name(s))):
                    print(f"  {classify(s):<26} {symbol_name(s)}")

    if args.json:
        data = {"build": str(builddir), "units": results, "link": linkres}
        pathlib.Path(args.json).write_text(json.dumps(data, indent=1))


if __name__ == "__main__":
    main()
