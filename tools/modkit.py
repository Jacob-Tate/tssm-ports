#!/usr/bin/env python3
"""Tools for making mods for the PC port (docs/MODDING.md).

Reads the game's packages (.HIP/.HOP) from your own extracted Xbox or GameCube
disc and writes nothing but what you ask for. Python 3 standard library only.

A GameCube package (its PLAT chunk says "GC") is said so, and read in its own
layout: TEXT is big-endian there, and its textures carry no names on the disc
(the port names each after its asset, less ".RW3", which is what `textures`
prints for them). `extract` writes a GameCube asset's bytes only with --raw:
they are in the GameCube's layout, and a file in a mod's assets/ folder is
always read in the Xbox layout, even inside a GameCube package.

    modkit.py list <package> [--type TEXT]        every asset: ID, type, size, layer, name
    modkit.py textures <package>                   the textures in it, by RenderWare name
    modkit.py find <assets folder> <what>          which packages have an asset whose ID,
                                                   name, texture name or text matches
    modkit.py extract <package> <ID|name> [out] [--raw]
                                                   one asset's bytes; a TEXT asset is
                                                   written as .txt (what assets/ takes)
    modkit.py new <mods folder> <Folder> [--name N] [--author A]
                                                   a new mod: mod.ini and the folders

<package> is a path. IDs are eight hex digits, as the port's log prints them.
"""

import argparse
import os
import struct
import sys

LAYERS = ["DEFAULT", "TEXTURE", "TEXTURE_STRM", "BSP", "MODEL", "ANIMATION", "VRAM", "SRAM",
          "SNDTOC", "CUTSCENE", "CUTSCENETOC", "JSPINFO"]


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


class Asset:
    def __init__(self, aid, atype, off, size, pad, flags):
        self.id = aid
        self.type = atype
        self.off = off
        self.size = size
        self.pad = pad
        self.flags = flags
        self.align = 0
        self.name = ""
        self.layer = -1


def platform_of(b):
    """The id in the package's PLAT chunk ("BX" Xbox, "GC" GameCube), or ""."""
    def walk(o, end, depth):
        while o + 8 <= end:
            cid, size = b[o:o + 4], be32(b, o + 4)
            body = o + 8
            if size > end - body:
                return ""
            if cid == b"PACK" and depth == 0:
                return walk(body, body + size, 1)
            if cid == b"PLAT":
                return b[body:body + size].split(b"\0")[0].decode("latin-1")
            o = body + size
        return ""
    return walk(8, min(len(b), 4096), 0) if b[:4] == b"HIPA" else ""


def is_gamecube(b):
    return platform_of(b) == "GC"


_said = set()


def say_gamecube(path, b):
    """One line on stderr for each GameCube package read."""
    if is_gamecube(b) and path not in _said:
        _said.add(path)
        sys.stderr.write("%s: a GameCube package (read in the GameCube's layout)\n" % path)


def read_package(path):
    """(bytes, [Asset], [(layer type, [Asset])]) -- the container's chunk ids and
    sizes are big-endian on every platform; asset payloads are the console's own
    (is_gamecube)."""
    with open(path, "rb") as f:
        b = f.read()
    if b[:4] != b"HIPA":
        raise ValueError("%s is not a HIP/HOP package" % path)
    assets, layers, byid = [], [], {}

    def walk(o, end):
        while o + 8 <= end:
            cid, size = b[o:o + 4], be32(b, o + 4)
            body = o + 8
            if cid == b"HIPA":
                o += 8
                continue
            if cid in (b"PACK", b"DICT", b"ATOC", b"LTOC", b"STRM"):
                walk(body, body + size)
            elif cid == b"AHDR":
                a = Asset(*struct.unpack_from(">I4sIiiI", b, body))
                a.type = a.type.decode("latin-1")
                p = body + 24
                if b[p:p + 4] == b"ADBG":
                    a.align = be32(b, p + 8)
                    n = b.index(b"\0", p + 12)
                    a.name = b[p + 12:n].decode("latin-1")
                byid[a.id] = a
                assets.append(a)
            elif cid == b"LHDR":
                ltype, count = struct.unpack_from(">Ii", b, body)
                ids = struct.unpack_from(">%dI" % count, b, body + 8)
                members = [byid[i] for i in ids if i in byid]
                for m in members:
                    m.layer = len(layers)
                layers.append((ltype, members))
            o = body + size

    walk(0, len(b))
    return b, assets, layers


def asset_bytes(b, a):
    return b[a.off:a.off + a.size]


def port_texture_name(asset_name):
    """What the port names a GameCube texture the disc leaves unnamed: the asset's
    name less ".RW3" (iGCPackage.h)."""
    n = asset_name
    if n.upper().endswith(".RW3"):
        n = n[:-4]
    return n[:31]


def txd_textures(data, asset_name=""):
    """[(name, width, height)] of a RenderWare texture dictionary (RWTX/TEXS),
    Xbox or GameCube."""
    out = []

    def chunks(o, end):
        while o + 12 <= end:
            ctype, size, _ = struct.unpack_from("<III", data, o)
            yield ctype, o + 12, min(size, end - o - 12)
            o += 12 + size

    for ctype, body, size in chunks(0, len(data)):
        if ctype != 0x16:  # rwID_TEXDICTIONARY
            continue
        for ctype2, body2, size2 in chunks(body, body + size):
            if ctype2 != 0x15:  # rwID_TEXTURENATIVE
                continue
            for ctype3, body3, size3 in chunks(body2, body2 + size2):
                if ctype3 == 0x01 and size3 >= 0x60 and data[body3:body3 + 4] == b"\0\0\0\x06":
                    # the GameCube's struct, big-endian: name at 0x18, size at 0x5C
                    name = data[body3 + 0x18:body3 + 0x38].split(b"\0")[0].decode("latin-1")
                    if name in ("", "temp") and asset_name:
                        name = port_texture_name(asset_name)
                    w, h = struct.unpack_from(">HH", data, body3 + 0x5C)
                    out.append((name, w, h))
                elif ctype3 == 0x01 and size3 >= 84:  # its STRUCT
                    name = data[body3 + 8:body3 + 40].split(b"\0")[0].decode("latin-1")
                    w, h = struct.unpack_from("<HH", data, body3 + 80)
                    out.append((name, w, h))
                break
    return out


def text_of(data, gamecube=False):
    if len(data) < 4:
        return ""
    n = struct.unpack_from(">I" if gamecube else "<I", data, 0)[0]
    return data[4:4 + n].decode("latin-1")


def parse_id(s):
    t = s[2:] if s.lower().startswith("0x") else s
    if len(t) == 8:
        try:
            return int(t, 16)
        except ValueError:
            return None
    return None


def find_asset(assets, what):
    aid = parse_id(what)
    for a in assets:
        if aid is not None and a.id == aid:
            return a
    w = what.lower()
    for a in assets:
        if a.name.lower() == w or os.path.splitext(a.name)[0].lower() == w:
            return a
    return None


def cmd_list(args):
    b, assets, layers = read_package(args.package)
    say_gamecube(args.package, b)
    gc = is_gamecube(b)
    for a in assets:
        if args.type and a.type.strip() != args.type.strip():
            continue
        layer = LAYERS[layers[a.layer][0]] if 0 <= a.layer and layers[a.layer][0] < len(LAYERS) else "-"
        extra = ""
        if a.type == "TEXT":
            extra = "  \"%s\"" % text_of(asset_bytes(b, a), gc)[:60]
        print("%08X  %s  %9d  %-12s %s%s" % (a.id, a.type, a.size, layer, a.name, extra))


def cmd_textures(args):
    b, assets, _ = read_package(args.package)
    say_gamecube(args.package, b)
    for a in assets:
        if a.type in ("RWTX", "TEXS"):
            for name, w, h in txd_textures(asset_bytes(b, a), a.name):
                print("%-32s %4dx%-4d  (asset %08X %s, %s)" % (name, w, h, a.id, a.type, a.name))


def packages(folder):
    for root, _, files in os.walk(folder):
        for f in sorted(files):
            if f.lower().endswith((".hip", ".hop")):
                yield os.path.join(root, f)


def cmd_find(args):
    aid = parse_id(args.what)
    w = args.what.lower()
    for path in packages(args.folder):
        try:
            b, assets, _ = read_package(path)
        except ValueError:
            continue
        rel = os.path.relpath(path, args.folder)
        gc = is_gamecube(b)
        for a in assets:
            hit = []
            if aid is not None and a.id == aid:
                hit.append("id")
            if w in a.name.lower():
                hit.append("name")
            if a.type == "TEXT" and w in text_of(asset_bytes(b, a), gc).lower():
                hit.append("text \"%s\"" % text_of(asset_bytes(b, a), gc)[:60])
            if a.type in ("RWTX", "TEXS"):
                for name, tw, th in txd_textures(asset_bytes(b, a), a.name):
                    if w in name.lower():
                        hit.append("texture %s %dx%d" % (name, tw, th))
            if hit:
                print("%-24s %08X %s %-32s %s%s" % (rel, a.id, a.type, a.name, ", ".join(hit),
                                                    "  (GameCube)" if gc else ""))


def cmd_extract(args):
    b, assets, _ = read_package(args.package)
    a = find_asset(assets, args.asset)
    if a is None:
        sys.exit("no asset %s in %s" % (args.asset, args.package))
    data = asset_bytes(b, a)
    gc = is_gamecube(b)
    say_gamecube(args.package, b)
    if a.type == "TEXT":
        out = args.out or (a.name or "%08X" % a.id) + ".txt"
        with open(out, "w", encoding="utf-8", newline="") as f:
            f.write(text_of(data, gc))
    elif gc and not args.raw:
        sys.exit("%08X %s is in the GameCube's layout; a file in a mod's assets/ folder is read in "
                 "the Xbox layout, so these bytes would not work there. Extract it from the Xbox "
                 "disc's package, or add --raw to write the GameCube bytes anyway." % (a.id, a.type))
    else:
        out = args.out or (a.name or "%08X" % a.id) + ".bin"
        with open(out, "wb") as f:
            f.write(data)
    print("%08X %s %d bytes -> %s" % (a.id, a.type, len(data), out))


def cmd_new(args):
    root = os.path.join(args.mods, args.folder)
    for d in ("files", "assets", "textures", "sounds", "fmv", "ini"):
        os.makedirs(os.path.join(root, d), exist_ok=True)
    ini = os.path.join(root, "mod.ini")
    if not os.path.exists(ini):
        with open(ini, "w", encoding="utf-8") as f:
            f.write("[mod]\nname = %s\nauthor = %s\nversion = 1.0\n"
                    "description = \nenabled = 1\npriority = 0\n"
                    % (args.name or args.folder, args.author or ""))
    print("made", root)


def main():
    # Text holds the game's 8-bit characters (button pictures among them), which
    # a Windows console's code page may not have.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="backslashreplace")
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("list")
    s.add_argument("package")
    s.add_argument("--type")
    s.set_defaults(fn=cmd_list)
    s = sub.add_parser("textures")
    s.add_argument("package")
    s.set_defaults(fn=cmd_textures)
    s = sub.add_parser("find")
    s.add_argument("folder")
    s.add_argument("what")
    s.set_defaults(fn=cmd_find)
    s = sub.add_parser("extract")
    s.add_argument("package")
    s.add_argument("asset")
    s.add_argument("out", nargs="?")
    s.add_argument("--raw", action="store_true",
                   help="write a GameCube asset's bytes as they are (not usable in assets/)")
    s.set_defaults(fn=cmd_extract)
    s = sub.add_parser("new")
    s.add_argument("mods")
    s.add_argument("folder")
    s.add_argument("--name")
    s.add_argument("--author")
    s.set_defaults(fn=cmd_new)
    args = p.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
