#!/usr/bin/env python3
"""Makes the 3DS icon (48x48 and 24x24) from the player's own game files.

The Xbox game's DEFAULT.XBE carries two small pictures the Xbox dashboard shows: the title
image (the movie's logo in a life ring) and the save image (a Krabby Patty). This script
reads the one you choose from your own copy of the game and writes the icon the HOME Menu and
the Homebrew Launcher show, in the two sizes a 3DS icon has. Nothing of the game is in this
script or in the repository. Keep the icons (and any .3dsx or .cia built with them) to
yourself: they are the game's art. The script refuses an OUT_DIR inside a git work tree, so
they cannot end up in a commit by accident.

    python3 tools/3ds/icon_from_game_files.py GAME_FILES OUT_DIR [--image title|save]

Then build with it: -DTSSM_3DS_ICON=OUT_DIR on the first CMake run (docs/3DS.md); both the
.3dsx and the .cia get it.

GAME_FILES is the folder of your Xbox game files with DEFAULT.XBE in it (the file at the top
of the disc, next to boot.HIP), or the path of DEFAULT.XBE itself. OUT_DIR gets:

    icon-48.png    the icon (48x48), for the .3dsx and the .cia
    icon-24.png    the small icon (24x24), for the .cia
    icon-full.png  the picture as the game has it, for comparison

Needs Python 3.8+ (python3; on Windows python or py) and the standard library only.
"""

import argparse
import os
import struct
import sys
import zlib

# The XBE's two pictures, by their section names; each is an XPR0 resource with one texture.
IMAGES = {
    "title": ("$$XTIMAGE", "the title image (the movie's logo in a life ring)"),
    "save": ("$$XSIMAGE", "the save image (a Krabby Patty)"),
}
SIZES = (48, 24)


def fail(text):
    raise SystemExit("icon_from_game_files: " + text)


# ---------------------------------------------------------------------------
# The XBE and its sections


def xbe_section(xbe, name):
    """The bytes of the XBE section called `name`, found through the XBE's header."""
    if xbe[:4] != b"XBEH" or len(xbe) < 0x178:
        fail("this is not an Xbox program (DEFAULT.XBE): it does not start with XBEH")
    base, = struct.unpack_from("<I", xbe, 0x104)
    count, headers = struct.unpack_from("<II", xbe, 0x11C)

    def at(va, size=1):
        off = va - base
        if off < 0 or off + size > len(xbe):
            fail("DEFAULT.XBE looks damaged (an address points outside the file)")
        return off

    found = []
    for i in range(count):
        h = at(headers + i * 0x38, 0x38)
        raw_addr, raw_size, name_addr = struct.unpack_from("<III", xbe, h + 0x0C)
        n = at(name_addr)
        end = xbe.find(b"\0", n, n + 64)
        sname = xbe[n:end if end >= 0 else n + 64].decode("latin-1")
        found.append(sname)
        if sname == name:
            if raw_addr + raw_size > len(xbe):
                fail("DEFAULT.XBE looks damaged (section %s goes past the end)" % name)
            return xbe[raw_addr:raw_addr + raw_size]
    fail("DEFAULT.XBE has no %s section (it has: %s); is it this game's?" %
         (name, ", ".join(found)))


# ---------------------------------------------------------------------------
# The XPR0 resource and its texture

# Xbox texture formats (the D3DFORMAT byte of the texture's Format word) this script reads.
FMT_ARGB8_SWIZZLED = 0x06
FMT_ARGB8_LINEAR = 0x12
FMT_DXT = {0x0C: 1, 0x0E: 3, 0x0F: 5}  # DXT1, DXT3 (DXT2), DXT5 (DXT4)


def xpr_texture(res, what):
    """(width, height, RGBA list) of the first texture in an XPR0 resource."""
    if res[:4] != b"XPR0" or len(res) < 32:
        fail("%s is not in the expected form (no XPR0 resource)" % what)
    total, header = struct.unpack_from("<II", res, 4)
    common, data, _lock, fmt, size = struct.unpack_from("<5I", res, 12)
    if (common >> 16) & 7 != 4:
        fail("%s holds no texture (resource type %d)" % (what, (common >> 16) & 7))
    kind = (fmt >> 8) & 0xFF
    if size:  # a linear texture's size word: width-1 in bits 0-11, height-1 in bits 12-23
        w, h = (size & 0xFFF) + 1, ((size >> 12) & 0xFFF) + 1
    else:     # a power-of-two texture: log2 sizes in the format word
        w, h = 1 << ((fmt >> 20) & 15), 1 << ((fmt >> 24) & 15)
    start = header + data
    pix = res[start:min(total, len(res))]
    if kind == FMT_ARGB8_SWIZZLED:
        if len(pix) < w * h * 4:
            fail("%s is cut short" % what)
        px = [(pix[i + 2], pix[i + 1], pix[i], pix[i + 3]) for i in range(0, w * h * 4, 4)]
        return w, h, unswizzle(px, w, h)
    if kind == FMT_ARGB8_LINEAR:
        # rows can be padded: the pitch is (bits 24-31 + 1) * 64 bytes
        pitch = max(((size >> 24) + 1) * 64, w * 4)
        if len(pix) < pitch * (h - 1) + w * 4:
            fail("%s is cut short" % what)
        return w, h, [(pix[r + i + 2], pix[r + i + 1], pix[r + i], pix[r + i + 3])
                      for r in range(0, pitch * h, pitch) for i in range(0, w * 4, 4)]
    if kind in FMT_DXT:
        need = (w // 4) * (h // 4) * (8 if FMT_DXT[kind] == 1 else 16)
        if len(pix) < need:
            fail("%s is cut short" % what)
        return w, h, decode_dxt(pix, w, h, FMT_DXT[kind])
    fail("%s uses a picture format this script does not read (0x%02X)" % (what, kind))


def unswizzle(px, w, h):
    """Xbox swizzled (Morton order: x and y bits interleaved, x lowest) to rows."""
    out = [None] * (w * h)
    xb, yb = w.bit_length() - 1, h.bit_length() - 1
    for y in range(h):
        for x in range(w):
            i, bit, k = 0, 0, 0
            while k < max(xb, yb):
                if k < xb:
                    i |= ((x >> k) & 1) << bit
                    bit += 1
                if k < yb:
                    i |= ((y >> k) & 1) << bit
                    bit += 1
                k += 1
            out[y * w + x] = px[i]
    return out


def dxt_colors(c0, c1, four):
    def rgb(c):
        return ((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31
    a, b = rgb(c0), rgb(c1)
    if four or c0 > c1:
        return [a + (255,), b + (255,),
                tuple((2 * a[i] + b[i]) // 3 for i in range(3)) + (255,),
                tuple((a[i] + 2 * b[i]) // 3 for i in range(3)) + (255,)]
    return [a + (255,), b + (255,), tuple((a[i] + b[i]) // 2 for i in range(3)) + (255,),
            (0, 0, 0, 0)]


def decode_dxt(data, w, h, kind):
    px = [(0, 0, 0, 255)] * (w * h)
    bs = 8 if kind == 1 else 16
    o = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            alpha = None
            if kind == 3:
                bits = int.from_bytes(data[o:o + 8], "little")
                alpha = [((bits >> (4 * i)) & 15) * 17 for i in range(16)]
            elif kind == 5:
                a0, a1 = data[o], data[o + 1]
                bits = int.from_bytes(data[o + 2:o + 8], "little")
                if a0 > a1:
                    tab = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
                else:
                    tab = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
                alpha = [tab[(bits >> (3 * i)) & 7] for i in range(16)]
            c0, c1, bits = struct.unpack_from("<HHI", data, o + bs - 8)
            cols = dxt_colors(c0, c1, kind != 1)
            for i in range(16):
                x, y = bx + (i & 3), by + (i >> 2)
                if x < w and y < h:
                    c = cols[(bits >> (2 * i)) & 3]
                    if alpha is not None:
                        c = c[:3] + (alpha[i],)
                    px[y * w + x] = c
            o += bs
    return px


# ---------------------------------------------------------------------------
# Framing, scaling and PNG


def content_box(px, w, h):
    """The square around what stands out from the picture's border colour (the title image's
    ring on its dark background), so the icon is filled by it; the whole picture when nothing
    stands out or the picture has see-through parts."""
    if any(p[3] < 255 for p in px):
        return 0, 0, w
    edge = [px[x] for x in range(w)] + [px[(h - 1) * w + x] for x in range(w)] + \
           [px[y * w] for y in range(h)] + [px[y * w + w - 1] for y in range(h)]
    bg = [sorted(p[k] for p in edge)[len(edge) // 2] for k in range(3)]
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        for x in range(w):
            p = px[y * w + x]
            if sum(abs(p[k] - bg[k]) for k in range(3)) > 96:
                x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
    if x1 < 0:
        return 0, 0, min(w, h)
    side = max(x1 - x0 + 1, y1 - y0 + 1)
    side = min(min(w, h), side + 2)  # a pixel of the background around it
    cx, cy = (x0 + x1 + 1) / 2, (y0 + y1 + 1) / 2
    left = int(round(min(max(cx - side / 2, 0), w - side)))
    top = int(round(min(max(cy - side / 2, 0), h - side)))
    return left, top, side


def area_weights(src0, src_len, dst_len):
    """For each output column (or row), the (source index, weight) pairs of an area filter."""
    scale = src_len / dst_len
    out = []
    for d in range(dst_len):
        a, b = d * scale, (d + 1) * scale
        ws = []
        i = int(a)
        while i < b and i < src_len:
            cover = min(b, i + 1) - max(a, i)
            if cover > 1e-9:
                ws.append((src0 + i, cover / scale))
            i += 1
        out.append(ws)
    return out


def scale_area(px, w, left, top, side, n):
    """The square (left, top, side) of the picture scaled to n x n by averaging the area each
    output pixel covers; colours weighted by their opacity, so see-through edges stay clean."""
    xs, ys = area_weights(left, side, n), area_weights(top, side, n)
    out = []
    for yw in ys:
        for xw in xs:
            r = g = b = a = 0.0
            for sy, wy in yw:
                row = sy * w
                for sx, wx in xw:
                    p = px[row + sx]
                    k = wx * wy * p[3]
                    r += p[0] * k
                    g += p[1] * k
                    b += p[2] * k
                    a += wx * wy * p[3]
            if a <= 0:
                out.append((0, 0, 0, 0))
            else:
                out.append((int(r / a + 0.5), int(g / a + 0.5), int(b / a + 0.5),
                            min(255, int(a + 0.5))))
    return out


def png_bytes(px, w, h):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            raw += bytes(px[y * w + x])

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


# ---------------------------------------------------------------------------


def git_work_tree(path):
    """The git work tree that holds `path` (a folder there or not yet made), or None."""
    p = os.path.abspath(path)
    while True:
        if os.path.exists(os.path.join(p, ".git")):
            return p
        up = os.path.dirname(p)
        if up == p:
            return None
        p = up


def find_xbe(game_files):
    if os.path.isfile(game_files):
        return game_files
    if not os.path.isdir(game_files):
        fail("%s does not exist" % game_files)
    for name in os.listdir(game_files):
        if name.lower() == "default.xbe":
            return os.path.join(game_files, name)
    fail("there is no DEFAULT.XBE in %s. It is the file at the top of the Xbox disc, next to "
         "boot.HIP: copy it from your disc into that folder (or give its path)." % game_files)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        epilog="Example (from the repository's folder): python3 tools/3ds/icon_from_game_files.py "
               "$HOME/tssm-files $HOME/tssm-icon. Then -DTSSM_3DS_ICON=$HOME/tssm-icon for the "
               ".3dsx and the .cia (docs/3DS.md). The icon is made from your copy of the game: "
               "do not share it, or a .3dsx or .cia built with it.")
    ap.add_argument("game_files", help="the folder with your game files and DEFAULT.XBE in it, "
                                       "or the path of DEFAULT.XBE")
    ap.add_argument("out_dir", help="where the icons go (made if missing; not inside a git "
                                    "work tree)")
    ap.add_argument("--image", choices=sorted(IMAGES), default="title",
                    help="which picture: title = %s (the default), save = %s" %
                         (IMAGES["title"][1], IMAGES["save"][1]))
    ap.add_argument("--whole", action="store_true",
                    help="use the whole picture as it is, without fitting the icon to what "
                         "stands out from its background")
    args = ap.parse_args()
    out_dir = os.path.abspath(args.out_dir)
    tree = git_work_tree(out_dir)
    if tree:
        fail("%s is inside the git work tree %s: the icon is the game's art, so it must not go "
             "where a commit can pick it up. Choose a folder outside any repository." %
             (out_dir, tree))
    xbe_path = find_xbe(os.path.abspath(args.game_files))
    with open(xbe_path, "rb") as f:
        xbe = f.read()
    section, what = IMAGES[args.image]
    w, h, px = xpr_texture(xbe_section(xbe, section), what)
    print("%s: %s, %dx%d" % (os.path.basename(xbe_path), what, w, h))
    if args.whole:
        left, top, side = (w - min(w, h)) // 2, (h - min(w, h)) // 2, min(w, h)
    else:
        left, top, side = content_box(px, w, h)
    if side < max(SIZES):
        print("  note: the picture is smaller than the icon (%d pixels); it is enlarged" % side)
    print("  icon from the square at %d,%d, %d pixels across" % (left, top, side))
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "icon-full.png"), "wb") as f:
        f.write(png_bytes(px, w, h))
    for n in SIZES:
        path = os.path.join(out_dir, "icon-%d.png" % n)
        with open(path, "wb") as f:
            f.write(png_bytes(scale_area(px, w, left, top, side, n), n, n))
        print("  wrote %s" % path)
    print("Build with -DTSSM_3DS_ICON=%s (docs/3DS.md). Keep these icons, and what you build "
          "with them, to yourself." % out_dir)


if __name__ == "__main__":
    main()
