#!/usr/bin/env python3
"""Fails when an image other than the repository's own defaults is about to be
committed, or already is: the guard for icon art (docs/ICON.md).

The repository carries no art. A picture used as the app icon, and everything
tools/make_icons.ps1 makes from it, stays outside every repository, the same
as the game's files. This catches a copy that got in anyway:

  * any image (by extension or by its first bytes, whatever its name) that is
    not one of the defaults listed below with the exact content listed;
  * any file named like make_icons.ps1's output (tssm.ico, icon-64.png, ...);
  * any file whose content is byte-identical to a file in an art folder
    (--art-dir, and TSSM_ICON_DIR from the environment).

    python tools/check_icon_art.py                   what is staged (git add)
    python tools/check_icon_art.py --range A..B      every commit in A..B
    python tools/check_icon_art.py --tree [REV]      everything tracked at REV

As a pre-commit hook, .git/hooks/pre-commit (or the worktree's):

    #!/bin/sh
    exec python tools/check_icon_art.py

Exit status: 0 clean, 1 something found, 2 git failed.
"""

import argparse
import hashlib
import os
import subprocess
import sys

# The images the repository has, path -> blob id. A default icon that changes
# changes here too, in the same commit, after a look at the new picture.
DEFAULTS = {
    # The decomp's own documentation.
    "assets/objdiff.png": "080e335ba0921822df1a5425753bd26670f9cdea",
    # The Android app's default launcher icon: a plain sponge
    # block drawn for the port.
    "android/app/src/main/res/mipmap-mdpi/ic_launcher.png": "6f2ea1ec9ea3261254d9ed14159092ab7654bc56",
    "android/app/src/main/res/mipmap-hdpi/ic_launcher.png": "1b9b1e9c9768fa58ef08df142a67ad8dcf5fd241",
    "android/app/src/main/res/mipmap-xhdpi/ic_launcher.png": "1fd12d2a74307e6e9a20ecc6fe7616003366d96b",
    "android/app/src/main/res/mipmap-xxhdpi/ic_launcher.png": "5bc215089d6afeef73bfb195a9a210db47132cac",
    "android/app/src/main/res/mipmap-xxxhdpi/ic_launcher.png": "8a555867776a1fe38a7a30127ff99cc5a7793bb9",
    # The Switch .nro's default icon: the same plain sponge block,
    # drawn by tools/switch/draw_default_icon.ps1.
    "dist/switch/default_icon.jpg": "88c7a1acb6728499fb0d06ed997ca640b2644af6",
    # The PC build's default icon: the same sponge block, made by
    # tools/make_icons.ps1 from dist/switch/default_icon.jpg (the switch branch);
    # byte for byte what the releases' tssm.exe carries.
    "dist/pc/default_icon.ico": "2675ce9b25f41895f063b98328c64e14bb727a9a",
    "dist/pc/default_icon-64.png": "8172b8c9f987fa8511f608e1c40f1cadfc09aa41",
}

IMAGE_EXTENSIONS = {
    ".png", ".jpg", ".jpeg", ".jfif", ".ico", ".icns", ".cur", ".webp", ".bmp", ".dib",
    ".gif", ".tif", ".tiff", ".tga", ".avif", ".heic", ".heif", ".jxl", ".psd", ".xcf",
    ".svg", ".svgz", ".xpm", ".ppm", ".pgm", ".pbm", ".dds", ".ktx", ".ktx2", ".exr",
}

# What make_icons.ps1 writes, by name.
GENERATED_NAMES = {
    "tssm.ico", "icon-64.png", "icon.jpg", "play-512.png", "preview.png",
    "ic_launcher_foreground.png", "tssm_icon.xml",
}

ZERO = "0" * 40


def image_magic(head):
    """The kind of image these first bytes start, or None."""
    if head.startswith(b"\x89PNG\r\n\x1a\n"):
        return "PNG"
    if head.startswith(b"\xff\xd8\xff"):
        return "JPEG"
    if head.startswith((b"GIF87a", b"GIF89a")):
        return "GIF"
    if head[:4] == b"RIFF" and head[8:12] == b"WEBP":
        return "WebP"
    if head[:4] in (b"\x00\x00\x01\x00", b"\x00\x00\x02\x00") and len(head) >= 6 and 0 < head[4] < 64 and head[5] == 0:
        return "ICO/CUR"
    if head.startswith(b"icns"):
        return "ICNS"
    if head.startswith(b"BM") and len(head) >= 14 and head[6:10] == b"\x00\x00\x00\x00":
        return "BMP"
    if head.startswith((b"II*\x00", b"MM\x00*")):
        return "TIFF"
    if head[4:12] in (b"ftypavif", b"ftypheic", b"ftypheix", b"ftypmif1"):
        return "AVIF/HEIF"
    if head.startswith(b"8BPS"):
        return "PSD"
    if head.startswith(b"\x00\x00\x00\x0cJXL ") or head.startswith(b"\xff\x0a"):
        return "JPEG XL"
    if head.startswith(b"DDS "):
        return "DDS"
    return None


def git(args, data=None):
    try:
        res = subprocess.run(["git"] + args, input=data, capture_output=True, check=False)
    except OSError as e:
        print(f"check_icon_art: cannot run git: {e}", file=sys.stderr)
        sys.exit(2)
    if res.returncode != 0:
        print(f"check_icon_art: git {' '.join(args)} failed:\n{res.stderr.decode(errors='replace')}",
              file=sys.stderr)
        sys.exit(2)
    return res.stdout


def parse_raw(out):
    """(path, blob) pairs from 'git diff --raw -z' output; skips gitlinks."""
    pairs = []
    fields = out.split(b"\0")
    i = 0
    while i < len(fields):
        meta = fields[i]
        if not meta.startswith(b":"):
            i += 1
            continue
        parts = meta[1:].split(b" ")
        new_mode, new_sha = parts[1].decode(), parts[3].decode()
        path = fields[i + 1].decode("utf-8", errors="surrogateescape")
        i += 2
        if new_mode == "160000" or new_sha == ZERO:
            continue
        pairs.append((path, new_sha))
    return pairs


def staged():
    return parse_raw(git(["diff", "--cached", "--raw", "-z", "--no-renames", "--no-abbrev",
                          "--diff-filter=ACMRT"]))


def in_range(rng):
    seen = set()
    pairs = []
    for commit in git(["rev-list", rng]).decode().split():
        out = git(["diff-tree", "-r", "-m", "--root", "--raw", "-z", "--no-renames", "--no-abbrev",
                   "--diff-filter=ACMRT", commit])
        for p in parse_raw(out):
            if p not in seen:
                seen.add(p)
                pairs.append(p)
    return pairs


def tracked(rev):
    pairs = []
    for entry in git(["ls-tree", "-r", "-z", rev]).split(b"\0"):
        if not entry:
            continue
        meta, path = entry.split(b"\t", 1)
        mode, kind, sha = meta.decode().split()
        if kind == "blob":
            pairs.append((path.decode("utf-8", errors="surrogateescape"), sha))
    return pairs


def heads(shas):
    """The first 16 bytes of each blob."""
    shas = sorted(set(shas))
    if not shas:
        return {}
    out = git(["cat-file", "--batch"], ("\n".join(shas) + "\n").encode())
    result = {}
    pos = 0
    for _ in shas:
        nl = out.index(b"\n", pos)
        header = out[pos:nl].split()
        sha, size = header[0].decode(), int(header[2])
        result[sha] = out[nl + 1:nl + 1 + min(size, 16)]
        pos = nl + 1 + size + 1
    return result


def art_blobs(dirs):
    """Blob id -> file, for every file under the art folders."""
    found = {}
    for d in dirs:
        if not d or not os.path.isdir(d):
            continue
        for root, _, files in os.walk(d):
            for name in files:
                path = os.path.join(root, name)
                try:
                    with open(path, "rb") as f:
                        data = f.read()
                except OSError:
                    continue
                # As the file is, and with CRLF turned to LF: core.autocrlf
                # stores a text file (the generator writes CRLF XML) that way.
                for body in {data, data.replace(b"\r\n", b"\n")}:
                    sha = hashlib.sha1(b"blob %d\0" % len(body) + body).hexdigest()
                    found[sha] = path
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--range", metavar="A..B", help="check every commit in a revision range")
    mode.add_argument("--tree", nargs="?", const="HEAD", metavar="REV",
                      help="check every file tracked at REV (default HEAD)")
    ap.add_argument("--art-dir", action="append", default=[], metavar="DIR",
                    help="a folder of icon art; any file identical to one in it fails "
                         "(TSSM_ICON_DIR from the environment is added)")
    args = ap.parse_args()

    if args.range:
        pairs, what = in_range(args.range), f"files added or changed in {args.range}"
    elif args.tree:
        pairs, what = tracked(args.tree), f"files tracked at {args.tree}"
    else:
        pairs, what = staged(), "staged files"

    art_dirs = list(args.art_dir)
    if os.environ.get("TSSM_ICON_DIR"):
        art_dirs.append(os.environ["TSSM_ICON_DIR"])
    art = art_blobs(art_dirs)

    blob_heads = heads(sha for _, sha in pairs)
    problems = []
    for path, sha in pairs:
        name = path.rsplit("/", 1)[-1]
        ext = os.path.splitext(name)[1].lower()
        kind = image_magic(blob_heads.get(sha, b""))
        # A default is a default, also when an art folder holds a copy of it
        # (the release's icon folder is made from the same picture).
        if DEFAULTS.get(path) == sha:
            continue
        if sha in art:
            problems.append(f"{path}: identical to {art[sha]}")
        elif name.lower() in GENERATED_NAMES or "tssm-logo" in name.lower():
            problems.append(f"{path}: named like tools/make_icons.ps1 output")
        elif ext in IMAGE_EXTENSIONS or kind:
            if DEFAULTS.get(path) == sha:
                continue
            why = "a default image with different content" if path in DEFAULTS else "not a default image"
            problems.append(f"{path}: {kind or ext[1:].upper()} image, {why}")

    if problems:
        print(f"check_icon_art: {len(problems)} of {len(pairs)} {what} may be icon art, "
              "which must not be committed (docs/ICON.md):")
        for p in problems:
            print(f"  {p}")
        if not args.range and not args.tree:
            print("Unstage them: git restore --staged <path>.")
        print("A new default image goes into DEFAULTS in tools/check_icon_art.py.")
        return 1
    print(f"check_icon_art: {len(pairs)} {what}, no icon art.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
