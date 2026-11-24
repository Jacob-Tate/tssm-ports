#!/usr/bin/env python3
"""Build the single-file Windows "friend build": TSSM-Coop.exe (+ a short README.txt).

TSSM-Coop.exe is the small launcher (tools/friends, target tssm-friend-launcher) with the game appended:
tssm.exe, every DLL in bin\\, the Visual C++ runtime DLLs they import (so nobody has to install anything)
and friend.ini with the co-op server address. See tools/friends/launcher.cpp for what happens when it runs.

Usage (after a Release build of the game, which fills bin\\):
    python -I tools/package_friends.py [--server HOST[:PORT]] [--bin bin] [--launcher PATH] [--out DIR]
"""

import argparse
import ctypes
import glob
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from ctypes import wintypes

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ARCHIVE_MAGIC = b"TSSMPK01"
TRAILER_MAGIC = b"TSSMLNCH"
COMPRESS_ALGORITHM_LZMS = 5
DEFAULT_SERVER = "10.0.0.5:7865"

# Visual C++ runtime DLLs that get shipped app-local (only those actually imported). The Universal CRT
# (ucrtbase, api-ms-win-crt-*) is part of Windows 10/11 and is not shipped.
CRT_RE = re.compile(r"^(msvcp140.*|vcruntime140.*|concrt140|vcomp140|vccorlib140)\.dll$", re.I)
DEBUG_CRT_RE = re.compile(r"^(msvcp\d+d|vcruntime\d+d(_\d)?|ucrtbased|vcomp\d+d|concrt\d+d|msvcr\d+d|"
                          r"msvcp\d+_\d+d|vccorlib\d+d)(\.dll)?$", re.I)

README = """\
TSSM Co-op - The SpongeBob SquarePants Movie (PC port), single-file version
===========================================================================

TSSM-Coop.exe is the game in one file. You need Windows 10 or 11 (64-bit) and your own copy of the
game's files. They are not included.

Your game files
  Xbox release: extract the disc image with extract-xiso or xdvdfs, and pick the folder that holds
  boot.HIP, font.HIP and the FMV folder.
  GameCube release: in Dolphin, right-click the game > Extract Entire Disc, and pick the "files" folder.

Use
  1. Put TSSM-Coop.exe anywhere (Desktop, a USB stick, ...) and double-click it. The first start
     unpacks the game (a few seconds); later starts are instant.
     If Windows SmartScreen warns about it: More info > Run anyway.
  2. The game's own launcher asks for the folder with your game files. Pick it; it is remembered.
  3. Co-op: press F1 (on a controller: hold Back and press Start) and open the Co-op tab.
     - Find the section "Co-op server (no port forwarding)". The server is filled in already.
     - The host presses "Host on the server". The tab shows a room code (4 characters) and a join
       code. The host tells the friend both.
     - The friend types the room code and the join code and presses "Join on the server".
     No router settings are needed.

Good to know
  - Everyone must use the same TSSM-Coop.exe. A different version is refused.
  - Both players need the same release of the game files (Xbox or GameCube, same region).
  - The host's game runs the world and the host's save is the one that counts.
  - Something went wrong? Send %APPDATA%\\tssm\\last_run.log.

Updating
  Replace TSSM-Coop.exe with the newer one. Your settings and saves are kept; the old unpacked
  version is removed automatically.

Where things are stored (all in your user profile, nothing next to TSSM-Coop.exe)
  %LOCALAPPDATA%\\TSSM-Coop\\   the unpacked game
  %APPDATA%\\TSSM-Coop\\        settings (config.ini)
  %APPDATA%\\tssm\\             your game saves and the log (last_run.log)

Removing everything
  Hold Shift while starting TSSM-Coop.exe (or run "TSSM-Coop.exe --uninstall"). A window lists the
  two TSSM-Coop folders above and asks for confirmation. Your saves in %APPDATA%\\tssm, your game
  files and TSSM-Coop.exe itself are not touched. Then delete TSSM-Coop.exe.

Command line options of the game can be given to TSSM-Coop.exe as usual.
"""


# ---------------------------------------------------------------------------------------------- PE imports

def pe_imports(path):
    """Names of the DLLs a PE file imports (normal and delay-load tables), lower-cased."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"MZ":
        raise ValueError(f"{path}: not a PE file")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError(f"{path}: not a PE file")
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    dd = opt + (112 if magic == 0x20B else 96)
    sections = []
    sec = opt + opt_size
    for i in range(nsec):
        vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, sec + i * 40 + 8)
        sections.append((va, max(vsize, rawsize), raw))

    def rva_to_off(rva):
        for va, size, raw in sections:
            if va <= rva < va + size:
                return raw + rva - va
        return None

    def cstr(rva):
        off = rva_to_off(rva)
        end = data.index(b"\0", off)
        return data[off:end].decode("ascii", "replace").lower()

    names = set()
    # import directory (entry 1): 20-byte descriptors, name RVA at +12
    rva, _ = struct.unpack_from("<II", data, dd + 8)
    if rva:
        off = rva_to_off(rva)
        while off is not None:
            name_rva = struct.unpack_from("<I", data, off + 12)[0]
            if name_rva == 0:
                break
            names.add(cstr(name_rva))
            off += 20
    # delay-load directory (entry 13): 32-byte descriptors, name RVA at +4
    rva, _ = struct.unpack_from("<II", data, dd + 13 * 8)
    if rva:
        off = rva_to_off(rva)
        while off is not None:
            attrs, name_rva = struct.unpack_from("<II", data, off)[0:2]
            if name_rva == 0:
                break
            names.add(cstr(name_rva))
            off += 32
    return names


# ------------------------------------------------------------------------------------------- VC++ runtime

def vs_roots():
    vswhere = os.path.expandvars(r"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe")
    if not os.path.isfile(vswhere):
        return []
    out = subprocess.run([vswhere, "-all", "-products", "*", "-property", "installationPath"],
                         capture_output=True, text=True).stdout
    return [line.strip() for line in out.splitlines() if line.strip()]


def find_crt_dll(name):
    candidates = []
    for root in vs_roots():
        candidates += glob.glob(os.path.join(root, "VC", "Redist", "MSVC", "*", "x64", "Microsoft.VC*.CRT", name))
    if not candidates:
        sys.exit(f"can't find the Visual C++ runtime DLL {name} (install the VS 'C++ x64 tools')")

    def toolset(path):  # .../MSVC/14.52.37025/x64/Microsoft.VC143.CRT/name.dll -> (14, 52, 37025)
        version = os.path.normpath(path).split(os.sep)[-4]
        return tuple(int(x) if x.isdigit() else 0 for x in version.split("."))

    return max(candidates, key=toolset)  # newest toolset, across all VS installs


def find_cmake():
    for root in vs_roots():
        p = os.path.join(root, "Common7", "IDE", "CommonExtensions", "Microsoft", "CMake", "CMake", "bin", "cmake.exe")
        if os.path.isfile(p):
            return p, root
    p = shutil.which("cmake")
    return (p, (vs_roots() or [None])[0]) if p else (None, None)


def build_launcher(launcher):
    cmake, root = find_cmake()
    if not cmake:
        sys.exit("cmake not found (install it, or the VS 'C++ CMake tools')")
    src = os.path.join(ROOT, "tools", "friends")
    out = os.path.dirname(launcher)
    lines = ["@echo off"]
    if root:
        lines.append(f'call "{os.path.join(root, "VC", "Auxiliary", "Build", "vcvars64.bat")}" >nul || exit /b 1')
    lines.append(f'"{cmake}" -S "{src}" -B "{out}" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1')
    lines.append(f'"{cmake}" --build "{out}" || exit /b 1')
    with tempfile.TemporaryDirectory() as tmp:
        bat = os.path.join(tmp, "build_launcher.bat")
        with open(bat, "w", newline="\r\n") as f:
            f.write("\n".join(lines) + "\n")
        if subprocess.run(["cmd.exe", "/c", bat]).returncode != 0 or not os.path.isfile(launcher):
            sys.exit("building the launcher failed")


# ---------------------------------------------------------------------------------------------- archive

def collect(bin_dir, server):
    """Returns [(archive path, source path or bytes)] for everything the game needs at run time."""
    info = os.path.join(bin_dir, "BUILD-INFO.txt")
    exe = os.path.join(bin_dir, "tssm.exe")
    if not os.path.isfile(exe):
        sys.exit(f"{exe} is missing: build the game in Release first.")
    text = open(info, encoding="utf-8", errors="replace").read() if os.path.isfile(info) else ""
    if not text.lstrip().lower().startswith("release"):
        sys.exit(f"{info} says: {text.strip() or '(missing)'}\n"
                 "The friend build must be a Release build (the Debug build needs the debug runtime). "
                 "Build Release into bin\\ and try again.")

    files = [("tssm.exe", exe)]
    dlls = sorted(glob.glob(os.path.join(bin_dir, "*.dll")), key=str.lower)
    for dll in dlls:
        files.append((os.path.basename(dll), dll))
    present = {name.lower() for name, _ in files}

    # Imports of the exe and the shipped DLLs, then transitively of the runtime DLLs we add.
    queue = [exe] + dlls
    seen_imports = set()
    while queue:
        path = queue.pop()
        for imp in pe_imports(path):
            seen_imports.add(imp)
            if DEBUG_CRT_RE.match(imp):
                sys.exit(f"{os.path.basename(path)} imports the debug runtime {imp}: that is a Debug build "
                         "and won't run on other PCs. Build Release.")
            if CRT_RE.match(imp) and imp not in present:
                source = find_crt_dll(imp)
                files.append((imp, source))
                present.add(imp)
                queue.append(source)
    crt = sorted(n for n, _ in files if CRT_RE.match(n))
    print("runtime DLLs shipped:", ", ".join(crt) if crt else "(none imported)")

    files.append(("friend.ini", ("[friend]\r\nserver = %s\r\n" % server).encode("ascii")))
    for name, source in files:
        if isinstance(source, str) and not os.path.isfile(source):
            sys.exit(f"missing: {source}")
    return files


def build_archive(files):
    parts = [ARCHIVE_MAGIC, struct.pack("<I", len(files))]
    for name, source in files:
        if isinstance(source, bytes):
            data = source
        else:
            with open(source, "rb") as f:
                data = f.read()
        encoded = name.encode("utf-8")
        parts += [struct.pack("<H", len(encoded)), encoded, struct.pack("<Q", len(data)), data]
    return b"".join(parts)


def lzms_compress(data):
    cabinet = ctypes.WinDLL("cabinet")
    cabinet.CreateCompressor.argtypes = [wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    cabinet.CreateCompressor.restype = wintypes.BOOL
    cabinet.Compress.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                                 ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    cabinet.Compress.restype = wintypes.BOOL
    cabinet.CloseCompressor.argtypes = [ctypes.c_void_p]
    handle = ctypes.c_void_p()
    if not cabinet.CreateCompressor(COMPRESS_ALGORITHM_LZMS, None, ctypes.byref(handle)):
        raise ctypes.WinError()
    try:
        source = ctypes.create_string_buffer(data, len(data))
        capacity = len(data) + len(data) // 8 + (1 << 20)
        out = ctypes.create_string_buffer(capacity)
        size = ctypes.c_size_t()
        if not cabinet.Compress(handle, source, len(data), out, capacity, ctypes.byref(size)):
            raise ctypes.WinError()
        return out.raw[:size.value]
    finally:
        cabinet.CloseCompressor(handle)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--server", default=os.environ.get("TSSM_FRIEND_SERVER") or DEFAULT_SERVER,
                        help="co-op server HOST[:PORT] pre-filled for friends (env TSSM_FRIEND_SERVER; "
                             f"default {DEFAULT_SERVER})")
    parser.add_argument("--bin", default=os.path.join(ROOT, "bin"))
    parser.add_argument("--launcher", default=os.path.join(ROOT, "build", "friends-launcher", "tssm-friend-launcher.exe"))
    parser.add_argument("--out", default=os.path.join(ROOT, "build", "package", "friends"))
    args = parser.parse_args()

    if not re.fullmatch(r"[A-Za-z0-9._-]+(:\d{1,5})?", args.server):
        sys.exit(f"--server must look like host or host:port, not {args.server!r}")
    if not os.path.isfile(args.launcher):
        print("building the launcher...")
        build_launcher(args.launcher)

    files = collect(args.bin, args.server)
    archive = build_archive(files)
    build_id = hashlib.sha256(archive).hexdigest()[:16]
    print(f"{len(files)} files, {len(archive) / 1e6:.1f} MB; compressing (LZMS)...")
    compressed = lzms_compress(archive)

    if os.path.isdir(args.out):
        shutil.rmtree(args.out)
    os.makedirs(args.out)
    exe = os.path.join(args.out, "TSSM-Coop.exe")
    with open(args.launcher, "rb") as f:
        stub = f.read()
    with open(exe, "wb") as f:
        f.write(stub)
        f.write(compressed)
        f.write(struct.pack("<QQ", len(stub), len(compressed)))
        f.write(build_id.encode("ascii"))
        f.write(TRAILER_MAGIC)
    with open(os.path.join(args.out, "README.txt"), "w", newline="\r\n") as f:
        f.write(README)
    print(f"{exe}: {os.path.getsize(exe) / 1e6:.1f} MB, build id {build_id}, server {args.server}")


if __name__ == "__main__":
    main()
