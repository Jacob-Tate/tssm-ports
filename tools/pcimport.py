#!/usr/bin/env python3
"""Where the files in src/SB/Core/pc came from, and whether they still match.

The PC layer has three kinds of file (src/SB/Core/pc/PORTING.md lists them all):

  treedome   copied from treedome, the BfBB PC port (CC0), with the BfBB names
             in it renamed (BFBB_ -> TSSM_ and so on, RENAMES below). Nothing
             else is changed in a file listed in TREEDOME, so `--check` can
             re-derive it from the treedome checkout and say when it differs.
  gc         copied unchanged from src/SB/Core/gc. Their sha1s are in
             src/SB/Core/pc/VERBATIM.txt; `--check` says when the gc original
             has moved on (the copy then needs re-taking, or a reason not to).
  adapted    started from one of the two and then edited, or written new. Not
             checked; PORTING.md says what each one is.

Usage:
    tools/pcimport.py --check                    compare everything, list drift
    tools/pcimport.py --import-treedome [names]  (re)copy treedome files
    tools/pcimport.py --import-gc [names]        (re)copy gc files, rewrite VERBATIM.txt
    tools/pcimport.py --record-shadowed          re-record SHADOWED.txt (after reconciling)
    --treedome DIR    the treedome checkout (default: a folder treedome-ref
                      two levels above the repository's root)

Importing overwrites. Run --check first and look at what it would replace.
"""

import argparse
import hashlib
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PC = ROOT / "src/SB/Core/pc"
GC = ROOT / "src/SB/Core/gc"

# The treedome commit the TREEDOME files were taken from.
TREEDOME_COMMIT = "ae391db"

# Applied in order, to the text of every TREEDOME file. Only names that are
# about which game this is: comments that state a fact about BfBB ("BFBB's
# Xbox assets carry no BSP") keep saying BFBB, because they are about BfBB.
RENAMES = [
    (re.compile(r"BFBB_"), "TSSM_"),
    (re.compile(r"bfbb_"), "tssm_"),
    (re.compile(r'"BFBB"'), '"TSSM"'),
    (re.compile(r"\bbfbb\b"), "tssm"),
    (re.compile(r"(?<=\\n)bfbb\b"), "tssm"),  # "\nbfbb: ..." in a printf
    (re.compile(r"SpongeBob SquarePants: Battle for Bikini Bottom"), "The SpongeBob SquarePants Movie"),
]

# Copied from treedome with RENAMES and nothing else. Paths are relative to
# src/SB/Core/pc in both trees. A file that had to be edited for TSSM beyond
# the renames is in TREEDOME_ADAPTED instead, and is imported the same way but
# not checked afterwards.
TREEDOME = [
    # compat: CodeWarrior extensions on top of the host's C library
    "compat/_stdfloatmath.h",
    "compat/cmath",
    "compat/intrin.h",
    "compat/math.h",
    "compat/mem.h",
    "compat/string.h",
    "intrin.cpp",
    # the host seam (iHost.h and its two backends are adapted: see below)
    "iStub.h",
    # window and render size
    "iWindow.h",
    "iWindowSDL.cpp",
    "iWindowNull.cpp",
    "iScreen.h",
    "iScreen.cpp",
    # config.ini
    "iConfig.h",
    "iConfigEdit.h",
    "iConfigEdit.cpp",
    "iConfigTable.h",
    # input devices
    "iPadStick.h",
    "iPadStick.cpp",
    "iCameraLook.h",
    "iCameraLook.cpp",
    "iTouchOverlay.h",
    "iTouchOverlay.cpp",
    # audio device (the seam and both backends are adapted, below)
    "iSndReverb.h",
    "iSndReverb.cpp",
    # movies
    "iFMVAudio.h",
    "iFMVAudioNull.cpp",
    "iFMVDecoder.h",
    "iFMVDecoderFFmpeg.cpp",
    "iFMVDecoderNull.cpp",
    "iRasterFill.h",
    # screen passes and frame capture
    "iGlow.h",
    "iDistort.h",
    "iSnapshot.h",
    "iFrameGrab.h",
    "iSaveThumb.h",
    # scene boot switch
    "iBoot.h",
    "iBoot.cpp",
    # the animated-UV pipeline (TSSM's gc/iFX.cpp is the same 2 functions on GX)
    "iFX.cpp",
]

# The RenderWare C API on librw. Every file of treedome's rw/ directory.
TREEDOME_RW_DIRS = ["rw"]

# Taken from treedome and then edited for TSSM; PORTING.md says how.
TREEDOME_ADAPTED = [
    "rw/engine_start.cpp",
    "rw/texture.cpp",
    "rw/engine.cpp",
    "rw/renderstate.cpp",
    "iConfig.cpp",
    "rw/stream.cpp",
    "rw/ptank.cpp",
    "iConfigTable.cpp",
    "iPadBindTable.cpp",
    "iPadTouch.cpp",
    # the TSSM input layer: TSSM's rows, presets and move layout
    "iConfig.cpp",
    "iPadHost.h",
    "iPadHostNull.cpp",
    "iPadHostSDL.cpp",
    "iPadKeyboardSDL.cpp",
    "iPadLayout.h",
    "iPadLayout.cpp",
    "iPadKeyboard.h",
    "iPadBind.h",
    "iPadBind.cpp",
    "iPadTokens.h",
    "iPadTokens.cpp",
    "iPadTouch.h",
    "iFile.h",
    "iFile.cpp",
    "iTime.cpp",
    "iFMV.cpp",
    "isavegame.h",
    "isavegame.cpp",
    # iHostSetFileTime and iHostCommandLine added, for the save tools
    "iHost.h",
    "iHostWin32.cpp",
    "iHostPosix.cpp",
    # the audio seam: a reverb send per voice, a grouped pause, fed voices
    "iSndHost.h",
    "iSndHostSDL.cpp",
    "iSndHostNull.cpp",
    # the movie device closes and opens again with the game's (iAudioSDL.h)
    "iFMVAudioSDL.cpp",
]

# Adapted AND renamed: pc file <- treedome file.
TREEDOME_ADAPTED_RENAMED = {
    "tssm_main.cpp": "bfbb_main.cpp",
}

# Copied unchanged from src/SB/Core/gc: the platform modules with no GameCube
# in them, and the headers of the interfaces
# whose declarations need nothing GameCube-specific.
GC_VERBATIM = [
    "iAnim.h",
    "iAnim.cpp",
    "iAnimSKB.cpp",
    "iCollide.h",
    "iCollideFast.h",
    "iColor.h",
    "iCutscene.h",
    "iDebug.h",
    "iEnv.h",
    "iEnv.cpp",
    "iFX.h",
    "iLight.h",
    "iLight.cpp",
    "iMath.h",
    "iMath.cpp",
    "iMath3.h",
    "iMath3.cpp",
    "iModel.h",
    "iMorph.h",
    "iMorph.cpp",
    "iParMgr.h",
    "iParMgr.cpp",
    "iScrFX.h",
    "iAsync.h",
    "iARAMTmp.h",
    "iMemMgr.h",
    "iPad.h",
    "ngcrad3d.h",
]


# PC files that stand in for a SHARED file, or were adapted from one, and so
# have to be looked at again when that file changes. The hashes in
# src/SB/Core/pc/SHADOWED.txt are the shared files as they were when the PC
# file was last brought in line; `--check` names any that have moved on, and
# `--record-shadowed` re-records them once the PC file has been reconciled.
#
#   compat/types.h   replaces include/types.h on the PC include path
#   rwsdk/*.h        a merge of include/rwsdk (TSSM) with treedome's PC arms
#   the rest         adapted from the gc file of the same name
SHADOWS = {"compat/types.h": "include/types.h"}
for _h in ["rpcollis.h", "rphanim.h", "rpmatfx.h", "rpptank.h", "rpskin.h", "rpusrdat.h",
           "rpworld.h", "rtanim.h", "rtintsec.h", "rtquat.h", "rtskinsp.h", "rtslerp.h",
           "rwcore.h", "rwplcore.h"]:
    SHADOWS["rwsdk/" + _h] = "include/rwsdk/" + _h
for _f in ["iAnimSKB.h", "iWad.h", "iWad.cpp", "iCamera.cpp", "iModel.cpp", "iCutscene.cpp", "iScrFX.cpp", "iSnd.h",
           "iSnd.cpp", "iFMV.h", "iFMV.cpp", "isavegame.h", "isavegame.cpp", "iTRC.h", "iTRC.cpp",
           "iDraw.h", "iDraw.cpp", "iPad.cpp", "iSystem.h", "iSystem.cpp", "iMemMgr.cpp",
           "iTime.h", "iTime.cpp", "iFile.h", "iFile.cpp", "iARAMTmp.cpp", "iAsync.cpp",
           "iException.h", "iException.cpp"]:
    SHADOWS[_f] = "src/SB/Core/gc/" + _f


def norm(data):
    # Line endings depend on the checkout (core.autocrlf), not on the file.
    return data.replace(b"\r\n", b"\n")


def sha1(path):
    return hashlib.sha1(norm(path.read_bytes())).hexdigest()


def renamed(text):
    for pattern, repl in RENAMES:
        text = pattern.sub(repl, text)
    return text


def treedome_files(treedome):
    """(relative path, is_checked) for every treedome-derived file."""
    out = [(p, True) for p in TREEDOME]
    src = treedome / "src/SB/Core/pc"
    for d in TREEDOME_RW_DIRS:
        for f in sorted((src / d).rglob("*")):
            rel = f.relative_to(src).as_posix()
            if f.is_file() and rel not in TREEDOME_ADAPTED:
                out.append((rel, True))
    out += [(p, False) for p in TREEDOME_ADAPTED]
    out += [(p, False) for p in TREEDOME_ADAPTED_RENAMED]
    return out


def read_text(path):
    return path.read_bytes().decode("utf-8", errors="surrogateescape")


def write_text(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.encode("utf-8", errors="surrogateescape"))


def is_text(path):
    return path.suffix.lower() not in (".ico", ".png", ".bin", ".spv")


def import_treedome(treedome, names):
    src = treedome / "src/SB/Core/pc"
    for rel, checked in treedome_files(treedome):
        if names and not any(n in rel for n in names):
            continue
        s = src / TREEDOME_ADAPTED_RENAMED.get(rel, rel)
        d = PC / rel
        # An adapted file is taken once, as the starting point, and never
        # overwritten after that: the edits are the point of it.
        if not checked and d.exists():
            print("adapted, kept ->", rel)
            continue
        if is_text(s):
            write_text(d, renamed(read_text(s)))
        else:
            d.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(s, d)
        print("treedome ->", rel)


def write_verbatim():
    lines = [
        "# Files copied unchanged from src/SB/Core/gc/ (tools/pcimport.py --import-gc).",
        "#",
        "# A copy rather than a shared include path, so the two builds can never see",
        "# each other's platform layer -- but a change to the gc original does not",
        "# reach the copy. `tools/pcimport.py --check` compares these hashes with the",
        "# gc files and names any that have moved on.",
        "#",
        "# sha1  name",
    ]
    for name in GC_VERBATIM:
        lines.append(f"{sha1(GC / name)}  {name}")
    (PC / "VERBATIM.txt").write_text("\n".join(lines) + "\n", newline="\n")


def import_gc(names):
    for name in GC_VERBATIM:
        if names and not any(n in name for n in names):
            continue
        shutil.copyfile(GC / name, PC / name)
        print("gc ->", name)
    write_verbatim()


def write_shadowed():
    lines = [
        "# Shared files the PC layer stands in for or was adapted from, as they were",
        "# when the PC file was last reconciled with them (tools/pcimport.py",
        "# --record-shadowed). `--check` names any that have changed since.",
        "#",
        "# sha1  shared file  ->  pc file",
    ]
    for pc, shared in SHADOWS.items():
        lines.append(f"{sha1(ROOT / shared)}  {shared}  ->  {pc}")
    (PC / "SHADOWED.txt").write_text("\n".join(lines) + "\n", newline="\n")


def check_shadowed():
    bad = 0
    recorded = {}
    st = PC / "SHADOWED.txt"
    if st.exists():
        for line in st.read_text().splitlines():
            if line and not line.startswith("#"):
                h, shared, _, pc = line.split()
                recorded[pc] = h
    for pc, shared in SHADOWS.items():
        if not (PC / pc).exists():
            print(f"MISSING   shadow    {pc}")
            bad += 1
        elif sha1(ROOT / shared) != recorded.get(pc):
            print(f"MOVED-ON  shadow    {pc} ({shared} changed since it was reconciled)")
            bad += 1
    return bad


def check(treedome):
    bad = 0
    src = treedome / "src/SB/Core/pc"
    for rel, checked in treedome_files(treedome):
        d = PC / rel
        if not d.exists():
            print(f"MISSING   treedome  {rel}")
            bad += 1
            continue
        if not checked:
            continue
        s = src / rel
        if not s.exists():
            print(f"GONE      treedome  {rel} (no longer in the treedome checkout)")
            continue
        want = renamed(read_text(s)).encode("utf-8", errors="surrogateescape") if is_text(s) else s.read_bytes()
        if norm(d.read_bytes()) != norm(want):
            print(f"DIFFERS   treedome  {rel}")
            bad += 1

    recorded = {}
    vt = PC / "VERBATIM.txt"
    if vt.exists():
        for line in vt.read_text().splitlines():
            if line and not line.startswith("#"):
                h, name = line.split(None, 1)
                recorded[name] = h
    for name in GC_VERBATIM:
        d = PC / name
        if not d.exists():
            print(f"MISSING   gc        {name}")
            bad += 1
            continue
        if sha1(d) != recorded.get(name):
            print(f"EDITED    gc        {name} (the pc copy is not what VERBATIM.txt records)")
            bad += 1
        if sha1(GC / name) != recorded.get(name):
            print(f"DRIFTED   gc        {name} (src/SB/Core/gc/{name} changed since the copy)")
            bad += 1

    bad += check_shadowed()

    print(f"{bad} problem(s)")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--treedome", default=str(ROOT.parent.parent / "treedome-ref"))
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--import-treedome", action="store_true")
    ap.add_argument("--import-gc", action="store_true")
    ap.add_argument("--record-shadowed", action="store_true")
    ap.add_argument("names", nargs="*")
    args = ap.parse_args()
    treedome = pathlib.Path(args.treedome)

    if args.import_treedome:
        if not (treedome / "src/SB/Core/pc").is_dir():
            sys.exit(f"no treedome checkout at {treedome}")
        import_treedome(treedome, args.names)
    if args.import_gc:
        import_gc(args.names)
    if args.record_shadowed:
        write_shadowed()
    if args.check or not (args.import_treedome or args.import_gc or args.record_shadowed):
        sys.exit(check(treedome))


if __name__ == "__main__":
    main()
