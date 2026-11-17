#!/usr/bin/env python3
"""Builds a HOME Menu banner for the 3DS .cia from the player's own game files.

The banner shows the Patty Wagon with SpongeBob and Patrick in one of the car's animations (by
default its nitro pose, the "Boosting" state; --anim picks another, such as
patty_wagon_drive.anm) and plays the first clip of the Extras menu's Macho Sound Pack. Nothing of
the game is in this script or in the repository: it reads the player's extracted Xbox disc and
writes the banner's parts into a folder of their choosing. Keep that folder (and any .cia made
from it) to yourself: it holds the game's model, textures and voice. The script refuses an
OUT_DIR inside a git work tree, so the game's files cannot end up in a commit by accident.

    python3 tools/3ds/banner_from_game_files.py GAME_FILES OUT_DIR
            --pycgfx PYCGFX_DIR --pycgfx-python PYCGFX_VENV_PYTHON --bannertool BANNERTOOL
            [--anim NAME.anm] [--skinned | --no-seam-fix] [--no-preview] [--two-sided-sheets off]
            [--scale S --offset X,Y,Z | --fit [--top-margin T --bottom-margin B]]

Then build the .cia with it: -DTSSM_3DS_BANNER=OUT_DIR/banner.bnr (or banner_flat.bnr) on
the first CMake run, or makerom's -banner OUT_DIR/banner.bnr (docs/3DS.md).

GAME_FILES is the folder with BB/, MN/, PL/ ... (your own copy of the game's Xbox files, the
same folder that goes to the SD card's 3ds/tssm/files). OUT_DIR gets:

    banner_sound.wav       the clip, fitted for a banner: 16-bit stereo, at most 3 seconds
    banner_sound_full.wav  the clip as the game has it (decoded, mono), for comparison
    banner_model.gltf/.bin the car and its animation, for pycgfx (glTF 2.0, Y up)
    banner_preview_*.png   pictures of the banner model (the rigid parts that go into the
                           CGFX, with their skirts over the seams, or the skin with --skinned)
                           from the HOME Menu's banner camera
    banner_flat.png        256x128 picture for a flat banner (bannertool -i), the fallback
    banner.cgfx            with --pycgfx: the 3D banner model (CGFX, at most 512 KB, its
                           textures and lookup tables on 128-byte boundaries); back faces
                           culled, the car's single sheets (the propeller, the flag's cloth,
                           the windshield and a small flat piece under the front) given back
                           faces
    banner.bnr             with --bannertool: the banner (CBMD with the CGFX, BCWAV)
    banner_flat.bnr        with --bannertool: the flat fallback banner with the same sound
    banner_report.txt      what was found, sizes against the limits

Needs Python 3.8+ (python3; on Windows python or py) and the standard library only. The CGFX
step runs pycgfx (MIT, github.com/skyfloogle/pycgfx, tested at commit 6c2e0cd; it needs Python
3.10+ with gltflib and Pillow, so it gets its own interpreter) through
tools/3ds/pycgfx_aligned.py, which puts the textures and lookup tables on 128-byte
boundaries; the .bnr step runs bannertool (GPL). Made and tested with the US Xbox files (it
reads MN/mnui_US.hip).

Placement: the HOME Menu draws the banner only between its bars (about rows 40 to 212 of the
top screen), so the default scale and offset keep the whole loop inside them, the top 15 % of
that height clear; the report checks every frame of the loop against it.

Banner limits it keeps to: the CGFX at most 0x80000 bytes (the HOME Menu does not load a
bigger one); its textures' and lookup tables' data on 128-byte boundaries in the file (the
HOME Menu froze on a banner whose textures and lookup table were 32 bytes past one; pycgfx
alone aligns them to 16 only), checked after the build; the sound stereo and at most 3 seconds
(longer clips play garbled or stop the model loading); textures at most 256x256, RGBA4 in the
CGFX; the flat picture 256x128.
"""

import argparse
import base64
import copy
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import wave
import zlib

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import modkit  # noqa: E402  (tools/modkit.py: the HIP/HOP reader)

CGFX_LIMIT = 0x80000
# Where the CGFX's textures and lookup tables must start in the file: the HOME Menu froze on a
# banner whose textures and lookup table were 32 bytes past a multiple of 128, and showed the
# same CGFX with them moved onto 128 (on a console, 2026-11-16).
CGFX_DATA_ALIGN = 128
PYCGFX_ALIGNED = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pycgfx_aligned.py")
SOUND_LIMIT_SEC = 3.0
TEX_SIZE = 128  # the car's two 256x256 textures, halved: 32 KB each as RGBA4
# The clip is mastered near full scale (its peak is 31,971 of 32,767); 3 dB down keeps it from
# being louder than other titles' banners.
SOUND_GAIN_DB = -3.0


def log(report, line):
    print(line)
    report.append(line)


# ---------------------------------------------------------------------------
# Packages


class Package:
    def __init__(self, path):
        self.path = path
        self.data, self.assets, _ = modkit.read_package(path)
        self.byid = {a.id: a for a in self.assets}
        self.byname = {}
        for a in self.assets:
            self.byname.setdefault(a.name.lower(), a)

    def bytes(self, a):
        return modkit.asset_bytes(self.data, a)


def find_file(game, rel):
    """rel with forward slashes; the disc's file names differ in case between folders."""
    cur = game
    for part in rel.split("/"):
        names = {n.lower(): n for n in os.listdir(cur)}
        if part.lower() not in names:
            raise SystemExit("not found in the game files: %s (looked in %s)" % (rel, cur))
        cur = os.path.join(cur, names[part.lower()])
    return cur


def text_asset(pkg, name):
    a = pkg.byname.get(name.lower())
    return modkit.text_of(pkg.bytes(a)) if a is not None and a.type == "TEXT" else None


def group_items(pkg, aid):
    d = pkg.bytes(pkg.byid[aid])
    n = struct.unpack_from("<H", d, 8)[0]
    return list(struct.unpack_from("<%dI" % n, d, 12))


# ---------------------------------------------------------------------------
# The sound: Extras > Macho Sound Pack > first clip


def find_macho_clip(game, report):
    """(SNDI entry fields, ADPCM bytes, names) of the Macho pack's first clip, found the way the
    Extras screen finds it (zMenuSB.cpp, zUICustomExtrasSound): the group "EXTRAS SOUNDS GROUP
    ALL" holds one group per pack, in the order of the "EXTRAS DESCRIPTION 02-nn" texts; each
    pack's group holds its clips' sound effects (SDFX), each of which plays a sound group (SGRP)
    naming the stream (SNDS)."""
    mnui = Package(find_file(game, "MN/mnui.HIP"))
    mnuc = Package(find_file(game, "MN/mnuc.HIP"))
    text = Package(find_file(game, "MN/mnui_US.hip"))

    pack = None
    for n in range(1, 21):
        t = text_asset(text, "EXTRAS DESCRIPTION 02-%02d" % n)
        if t is not None and "macho" in t.lower():
            pack = n
            break
    if pack is None:
        raise SystemExit("no Macho Sound Pack among the Extras texts (MN/mnui_US.hip)")
    title = text_asset(text, "EXTRAS SOUND %02d-01" % pack)

    packs = group_items(mnui, modkit.parse_id("AC9A6C39"))  # EXTRAS SOUNDS GROUP ALL
    sdfx_id = group_items(mnui, packs[pack - 1])[0]
    both = {}
    both.update(mnuc.byid)
    both.update(mnui.byid)

    def owner(aid):
        return mnui if aid in mnui.byid else mnuc

    sdfx = both[sdfx_id]
    sgrp_id = struct.unpack_from("<I", owner(sdfx_id).bytes(sdfx), 8)[0]
    sgrp = both[sgrp_id]
    snd_id = struct.unpack_from("<I", owner(sgrp_id).bytes(sgrp), 32)[0]
    snd = both[snd_id]
    log(report, "sound: Extras > %s pack %d, clip 1 \"%s\"" % (
        text_asset(text, "EXTRAS DESCRIPTION 02-%02d" % pack), pack, title))
    log(report, "  SDFX %08X %s -> SGRP %08X %s -> %s %08X %s (%d bytes, %s)" % (
        sdfx_id, sdfx.name, sgrp_id, sgrp.name, snd.type, snd_id, snd.name, snd.size,
        os.path.relpath(owner(snd_id).path, game)))

    spkg = owner(snd_id)
    entry = None
    for a in spkg.assets:
        if a.type != "SNDI":
            continue
        d = spkg.bytes(a)
        ns, nst, nc = struct.unpack_from("<III", d, 0)
        n = ns + nst + nc
        if len(d) != 12 + 44 * n:
            continue
        for i in range(n):
            f = struct.unpack_from("<HHIIHHHHIII", d, 12 + 44 * i)
            if f[9] == snd_id:
                entry = dict(format=f[0], channels=f[1], rate=f[2], block_align=f[4],
                             bits=f[5], data_size=f[8])
    if entry is None:
        raise SystemExit("no SNDI entry for %08X" % snd_id)
    data = spkg.bytes(snd)[:entry["data_size"]]
    log(report, "  format 0x%X (%s), %d ch, %d Hz, block %d, %d bytes of data" % (
        entry["format"], "Xbox ADPCM" if entry["format"] == 0x69 else "PCM", entry["channels"],
        entry["rate"], entry["block_align"], entry["data_size"]))
    return entry, data, dict(title=title, sound=snd.name, sound_id="%08X" % snd_id,
                             package=os.path.relpath(spkg.path, game).replace("\\", "/"))


IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55,
            60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
            337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
            1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
            5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
            20350, 22385, 24623, 27086, 29794, 32767]
IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


def decode_xbox_adpcm(src, channels, block_align):
    """The port's decoder (src/SB/Core/pc/iSndXbox.cpp): 36-byte blocks per channel, the
    header's sample then 63 codes; four-byte words interleaved by channel. Returns per-channel
    lists of samples."""
    per = block_align // channels
    fpb = (per - 4) * 2
    out = [[] for _ in range(channels)]
    for off in range(0, len(src) - block_align + 1, block_align):
        blk = src[off:off + block_align]
        for c in range(channels):
            pred = struct.unpack_from("<h", blk, 4 * c)[0]
            index = min(blk[4 * c + 2], 88)
            o = out[c]
            o.append(pred)
            for k in range(fpb - 1):
                word = (k // 8) * channels + c
                byte = 4 * channels + word * 4 + (k % 8) // 2
                nib = (blk[byte] >> 4) if (k & 1) else (blk[byte] & 0xF)
                step = IMA_STEP[index]
                diff = step >> 3
                if nib & 1:
                    diff += step >> 2
                if nib & 2:
                    diff += step >> 1
                if nib & 4:
                    diff += step
                pred = pred - diff if nib & 8 else pred + diff
                pred = -32768 if pred < -32768 else (32767 if pred > 32767 else pred)
                index = max(0, min(88, index + IMA_INDEX[nib]))
                o.append(pred)
    return out


def write_wav(path, chans, rate):
    n = len(chans[0])
    frames = bytearray()
    for i in range(n):
        for c in chans:
            frames += struct.pack("<h", c[i])
    with wave.open(path, "wb") as w:
        w.setnchannels(len(chans))
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(frames))


def envelope(x, rate, win_ms=10):
    w = max(1, rate * win_ms // 1000)
    return [math.sqrt(sum(v * v for v in x[i:i + w]) / len(x[i:i + w]))
            for i in range(0, len(x), w)], w


def fit_sound(mono, rate, report):
    """Trims the clip for a banner: leading silence off, then, if it is longer than the
    limit, the cut goes in the last pause between words before the limit (so no word is cut in
    half) with a short fade; stereo by doubling the channel."""
    env, w = envelope(mono, rate)
    peak = max(env) or 1.0
    quiet = peak * 0.03
    first = next(i for i, e in enumerate(env) if e > quiet)
    start = max(0, first * w - rate // 50)  # keep 20 ms before the first sound
    last = len(env) - 1 - next(i for i, e in enumerate(reversed(env)) if e > quiet)
    end = min(len(mono), (last + 1) * w + rate // 10)
    limit = int(SOUND_LIMIT_SEC * rate) - rate // 100  # a hair under 3 s
    cut_note = "nothing cut but silence"
    if end - start > limit:
        # The last pause (>= 60 ms under the quiet level) that ends before the limit.
        best = None
        run = 0
        for i in range(start // w, (start + limit) // w):
            if env[i] <= quiet * 2:
                run += 1
                if run * w >= rate * 60 // 1000:
                    best = (i - run + 1) * w + (run * w) // 2
            else:
                run = 0
        if best is not None and best - start > rate:  # keep at least a second
            end = best
            cut_note = "cut in the pause at %.2f s of the clip" % (best / rate)
        else:
            end = start + limit
            cut_note = "cut at the limit, %.2f s of the clip" % (end / rate)
    gain = 10.0 ** (SOUND_GAIN_DB / 20.0)
    out = [int(round(v * gain)) for v in mono[start:end]]
    fade = min(len(out), rate * 120 // 1000 if "limit" in cut_note else rate * 30 // 1000)
    for i in range(fade):
        out[len(out) - fade + i] = int(out[len(out) - fade + i] * (1.0 - (i + 1) / fade))
    log(report, "  clip %.3f s; kept %.3f s .. %.3f s (%.3f s): %s" % (
        len(mono) / rate, start / rate, end / rate, len(out) / rate, cut_note))
    log(report, "  level %+.1f dB: peak %d -> %d of 32767" % (
        SOUND_GAIN_DB, max(abs(v) for v in mono[start:end]), max(abs(v) for v in out)))
    return out, dict(full_sec=len(mono) / rate, start_sec=start / rate, end_sec=end / rate,
                     kept_sec=len(out) / rate, note=cut_note)


# ---------------------------------------------------------------------------
# RenderWare: the clump (frames, HAnim, geometry, skin) and the Xbox textures


def rw_chunks(b, o, end):
    while o + 12 <= end:
        t, s, v = struct.unpack_from("<III", b, o)
        yield t, o + 12, s
        o += 12 + s


def rw_child(b, o, s, t):
    for ct, co, cs in rw_chunks(b, o, o + s):
        if ct == t:
            return co, cs
    return None, None


class Clump:
    def __init__(self, b):
        co, cs = rw_child(b, 0, len(b), 0x10)
        # Frame list
        fo, fs = rw_child(b, co, cs, 0x0E)
        so, ss = rw_child(b, fo, fs, 0x01)
        nf = struct.unpack_from("<I", b, so)[0]
        self.frames = []
        for i in range(nf):
            r = struct.unpack_from("<9f3fiI", b, so + 4 + 56 * i)
            self.frames.append(dict(rot=r[0:9], pos=r[9:12], parent=r[12]))
        self.hanim = None
        exts = [c for c in rw_chunks(b, so + ss, fo + fs) if c[0] == 0x03]
        self.frame_bone_id = [None] * nf
        for i, (_, eo, es) in enumerate(exts):
            ho, hs = rw_child(b, eo, es, 0x11E)
            if ho is None:
                continue
            ver, bid, nn = struct.unpack_from("<Iii", b, ho)
            self.frame_bone_id[i] = bid
            if nn > 0:
                nodes = [struct.unpack_from("<iii", b, ho + 20 + 12 * k) for k in range(nn)]
                self.hanim = [dict(id=n[0], index=n[1], flags=n[2]) for n in nodes]
        # Geometry
        go, gs = rw_child(b, co, cs, 0x1A)
        geo_o, geo_s = rw_child(b, go, gs, 0x0F)
        so, ss = rw_child(b, geo_o, geo_s, 0x01)
        flags, nt, nv, nm = struct.unpack_from("<IiiI", b, so)
        if flags & 0x01000000:
            raise SystemExit("native (platform) geometry: not handled")
        p = so + 16
        if flags & 0x08:
            p += 4 * nv  # prelit colours
        nsets = (flags >> 16) & 0xFF or (2 if flags & 0x80 else (1 if flags & 0x04 else 0))
        self.uv = [struct.unpack_from("<2f", b, p + 8 * i) for i in range(nv)]
        p += 8 * nv * nsets
        self.tris = []
        for i in range(nt):
            v1, v0, mat, v2 = struct.unpack_from("<4H", b, p + 8 * i)
            self.tris.append((v0, v1, v2, mat))
        p += 8 * nt
        p += 16  # bounding sphere
        _has_v, has_n = struct.unpack_from("<II", b, p)
        p += 8
        self.pos = [struct.unpack_from("<3f", b, p + 12 * i) for i in range(nv)]
        p += 12 * nv
        self.nrm = [struct.unpack_from("<3f", b, p + 12 * i) for i in range(nv)] if has_n \
            else [(0.0, 1.0, 0.0)] * nv
        # Materials: texture names
        mo, ms = rw_child(b, geo_o, geo_s, 0x08)
        self.textures = []
        for t, o2, s2 in rw_chunks(b, mo, mo + ms):
            if t != 0x07:
                continue
            to, ts = rw_child(b, o2, s2, 0x06)
            name = ""
            if to is not None:
                strs = [c for c in rw_chunks(b, to, to + ts) if c[0] == 0x02]
                name = b[strs[0][1]:strs[0][1] + strs[0][2]].split(b"\0")[0].decode("latin-1")
            self.textures.append(name)
        # Skin
        eo, es = rw_child(b, geo_o, geo_s, 0x03)
        ko, ks = rw_child(b, eo, es, 0x116)
        nb, nu, mw = b[ko], b[ko + 1], b[ko + 2]
        p = ko + 4 + nu
        self.bone_idx = [tuple(b[p + 4 * i:p + 4 * i + 4]) for i in range(nv)]
        p += 4 * nv
        self.bone_w = [struct.unpack_from("<4f", b, p + 16 * i) for i in range(nv)]
        p += 16 * nv
        self.inv_bind = []
        for i in range(nb):
            m = list(struct.unpack_from("<16f", b, p + 64 * i))
            m[3] = m[7] = m[11] = 0.0  # RwMatrix flags and padding
            m[15] = 1.0
            self.inv_bind.append(m)
        self.num_bones = nb


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
            co = o + (bs - 8)
            c0, c1, bits = struct.unpack_from("<HHI", data, co)
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


def read_xbox_texture(b):
    """(name, w, h, RGBA list) of the first texture of an Xbox RW texture dictionary."""
    for t, o, s in rw_chunks(b, 0, len(b)):
        if t != 0x16:
            continue
        for t2, o2, s2 in rw_chunks(b, o, o + s):
            if t2 != 0x15:
                continue
            so, ss = rw_child(b, o2, s2, 0x01)
            plat = struct.unpack_from("<I", b, so)[0]
            if plat != 5:
                raise SystemExit("not an Xbox texture (platform %d)" % plat)
            name = b[so + 8:so + 40].split(b"\0")[0].decode("latin-1")
            w, h = struct.unpack_from("<HH", b, so + 80)
            comp = b[so + 87]
            data = b[so + 92:]
            kind = {0x0C: 1, 0x0E: 3, 0x0F: 5}.get(comp)
            if kind is None:
                raise SystemExit("texture %s: uncompressed Xbox texture (0x%X) not handled" %
                                 (name, comp))
            return name, w, h, decode_dxt(data, w, h, kind)
    raise SystemExit("no texture in the dictionary")


def downscale(px, w, h, nw, nh):
    fx, fy = w // nw, h // nh
    out = []
    for y in range(nh):
        for x in range(nw):
            acc = [0, 0, 0, 0]
            for yy in range(fy):
                row = (y * fy + yy) * w + x * fx
                for xx in range(fx):
                    p = px[row + xx]
                    for k in range(4):
                        acc[k] += p[k]
            n = fx * fy
            out.append(tuple(a // n for a in acc))
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
# Heavy Iron's skeletal animation (SKB1, src/SB/Core/pc/iAnimSKB.cpp)


class SKB:
    def __init__(self, b):
        self.magic, _, self.bones, self.tcount, self.kcount = struct.unpack_from("<IIHHI", b, 0)
        self.scale = struct.unpack_from("<3f", b, 16)
        p = 28
        self.keys = [struct.unpack_from("<H4h3h", b, p + 16 * i) for i in range(self.kcount)]
        p += 16 * self.kcount
        self.times = list(struct.unpack_from("<%df" % self.tcount, b, p))
        p += 4 * self.tcount
        # One row of key offsets per interval between times.
        rows = max(1, self.tcount - 1)
        self.offsets = list(struct.unpack_from("<%dH" % (rows * self.bones), b, p))

    @property
    def duration(self):
        return self.times[-1]

    def eval(self, time):
        """[(quat xyzw, tran)] per bone, local to the parent."""
        times = self.times
        time = min(max(time, 0.0), times[-1])
        tidx = 0
        while tidx + 1 < self.tcount - 1 and times[tidx + 1] < time:
            tidx += 1
        out = []
        sx, sy, sz = self.scale
        for bone in range(self.bones):
            if self.tcount == 1:
                k = self.keys[2 * bone]
                q = [v / 32767.0 for v in k[1:5]]
                out.append((q, (sx * k[5], sy * k[6], sz * k[7])))
                continue
            ki = self.offsets[tidx * self.bones + bone]
            k0, k1 = self.keys[ki], self.keys[ki + 1]
            t0, t1 = times[k0[0]], times[k1[0]]
            lerp = 0.0 if t1 == t0 else (time - t0) / (t1 - t0)
            lerp = min(max(lerp, 0.0), 1.0)
            q0 = [v / 32767.0 for v in k0[1:5]]
            q1 = [v / 32767.0 for v in k1[1:5]]
            q = slerp(q0, q1, lerp)
            t = tuple(s * (a + (b - a) * lerp) for s, a, b in zip((sx, sy, sz), k0[5:8], k1[5:8]))
            out.append((q, t))
        return out


def qnorm(q):
    n = math.sqrt(sum(v * v for v in q)) or 1.0
    return [v / n for v in q]


def slerp(a, b, t):
    d = sum(x * y for x, y in zip(a, b))
    if d < 0:
        b = [-v for v in b]
        d = -d
    if d > 0.9995:
        return qnorm([x + (y - x) * t for x, y in zip(a, b)])
    th = math.acos(min(1.0, d))
    s = math.sin(th)
    wa, wb = math.sin((1 - t) * th) / s, math.sin(t * th) / s
    return [wa * x + wb * y for x, y in zip(a, b)]


# Matrices: RenderWare's row-vector 4x4 (v' = v M), as 16 floats row-major.

def m_from_qt(q, t):
    x, y, z, w = q
    # The rotation matrix's rows for row vectors (the transpose of the column-vector form).
    return [1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y), 0,
            2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x), 0,
            2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y), 0,
            t[0], t[1], t[2], 1]


def m_mul(a, b):
    return [sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4)) for r in range(4) for c in range(4)]


def m_point(m, v):
    x, y, z = v
    return (x * m[0] + y * m[4] + z * m[8] + m[12], x * m[1] + y * m[5] + z * m[9] + m[13],
            x * m[2] + y * m[6] + z * m[10] + m[14])


def m_dir(m, v):
    x, y, z = v
    return (x * m[0] + y * m[4] + z * m[8], x * m[1] + y * m[5] + z * m[9],
            x * m[2] + y * m[6] + z * m[10])


def m_inv(m):
    """Inverse of a row-vector affine matrix (v' = v A + t  ->  v = (v' - t) A^-1)."""
    a = [[m[r * 4 + c] for c in range(3)] for r in range(3)]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1])
           - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
           + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    inv = [[(a[(c + 1) % 3][(r + 1) % 3] * a[(c + 2) % 3][(r + 2) % 3]
             - a[(c + 1) % 3][(r + 2) % 3] * a[(c + 2) % 3][(r + 1) % 3]) / det
            for c in range(3)] for r in range(3)]
    t = m[12:15]
    it = [-sum(t[k] * inv[k][c] for k in range(3)) for c in range(3)]
    return [inv[0][0], inv[0][1], inv[0][2], 0, inv[1][0], inv[1][1], inv[1][2], 0,
            inv[2][0], inv[2][1], inv[2][2], 0, it[0], it[1], it[2], 1]


def hanim_parents(nodes):
    """Parent node index of each HAnim node (-1 for the root), from the push/pop flags, the
    way iModelAnimMatrices walks them."""
    parents = []
    stack = [-1]  # iModelAnimMatrices starts its stack with the identity
    parent = -1
    for i, n in enumerate(nodes):
        if n["flags"] & 2:  # PUSH
            stack.append(parent)
        parents.append(parent)
        if n["flags"] & 1:  # POP
            parent = stack.pop()
        else:
            parent = i
    return parents


def pose(nodes, local, unit=False):
    """World matrices (row-vector) of every bone from the local (quat, tran) list. With `unit`
    the keys' quaternions are normalized first, as they are in the glTF (and so the CGFX)."""
    parents = hanim_parents(nodes)
    world = []
    for i, (q, t) in enumerate(local):
        m = m_from_qt(qnorm(q) if unit else q, t)
        world.append(m if parents[i] < 0 else m_mul(m, world[parents[i]]))
    return world


# ---------------------------------------------------------------------------
# The scene: car, placement for the banner camera


# pycgfx's banner-camera.gltf: at z = 0 the 400x240 top screen spans 40 x 24 units.
BANNER_CAM = dict(pos=(0.0, 1.0, 44.786), yfov=0.523599, aspect=5.0 / 3.0, znear=26.5)
# The HOME Menu draws the banner only between its bars, about rows 40 to 212 of the 240-row top
# screen (reported from hardware tests by another homebrew banner); the camera above sees the
# whole screen, so a model that fills it loses its top under the upper bar.
VISIBLE_ROWS = (40.0, 212.0)
# The default placement (scale and translation of the banner's root, after the yaw and tip), from
# --fit with its default margins: over all 64 frames of the loop the model's top keeps 15 % of
# the visible height below the upper bar, its bottom 5 % above the lower bar, and it is centred
# left to right.
BANNER_SCALE = 4.1695
BANNER_OFFSET = (-1.4404, -5.0729, -0.0511)
FIT_TOP, FIT_BOTTOM, FIT_SIDE = 0.15, 0.05, 0.05


DEFAULT_ANIM = "patty_wagon_fast.anm"
# zCar::CreateAnimTable's states (src/SB/Game/zCar.cpp); the car's ATBL names them by xStrHash.
CAR_STATES = ("Idle01", "Drive", "DriveSlippy", "Boosting", "Reversing", "Crash", "SpinLeft",
              "SpinRight", "Success", "Failed", "Death", "Jump", "JumpLandSquash")


def str_hash(s):
    """xStrHash (src/SB/Core/x/xString.cpp)."""
    h = 0
    for ch in s.encode("latin-1"):
        h = (h * 131 + (ch - (ch & (ch >> 1) & 0x20))) & 0xFFFFFFFF
    return h


def car_states(pkg):
    """{animation file name (lower case): [state, ...]} from the car's anim table (ATBL; its
    name is shared with an empty ANIM asset, so it is found by type): which zCar state plays
    which file, a lean blend's three files (left, middle, right) each named with its place."""
    names = {str_hash(n): n for n in CAR_STATES}
    out = {}
    for a in pkg.assets:
        if a.type != "ATBL" or a.name.lower() != "patty_wagon_bind.atbl":
            continue
        b = pkg.bytes(a)
        _, nraw, nfiles, nstates, _ = struct.unpack_from("<5I", b, 0)
        raw = struct.unpack_from("<%dI" % nraw, b, 20)
        o = 20 + 4 * nraw
        files = []
        for i in range(nfiles):
            nx, ny, rd = struct.unpack_from("<HHI", b, o + 32 * i + 12)
            files.append(struct.unpack_from("<%dI" % (nx * ny), b, rd))
        o += 32 * nfiles
        for i in range(nstates):
            sid, fi = struct.unpack_from("<II", b, o + 28 * i)
            idx = files[fi]
            for k, r in enumerate(idx):
                asset = pkg.byid.get(raw[r])
                if asset is None:
                    continue
                where = ""
                if len(idx) == 3:
                    where = " (leaning %s)" % ("left", "straight", "right")[k]
                elif len(idx) > 1:
                    where = " (blend %d of %d)" % (k + 1, len(idx))
                out.setdefault(asset.name.lower(), []).append(
                    names.get(sid, "%08X" % sid) + " state" + where)
    return out


def load_car(game, report, anim_name=DEFAULT_ANIM):
    ply2 = Package(find_file(game, "PL/PLY2.HIP"))

    def get(name, typ):
        a = ply2.byname.get(name.lower())
        if a is None or a.type != typ:
            raise SystemExit("PL/PLY2.HIP has no %s %s" % (typ, name))
        log(report, "  %s %08X %s (%d bytes)" % (a.type, a.id, a.name, a.size))
        return ply2.bytes(a)

    log(report, "model: PL/PLY2.HIP (the driving levels' player package)")
    clump = Clump(get("patty_wagon_bind.dff", "MODL"))
    if not anim_name.lower().startswith("patty_wagon_"):
        raise SystemExit("--anim takes one of the car's animations (patty_wagon_*.anm in "
                         "PL/PLY2.HIP), not %s" % anim_name)
    anim = SKB(get(anim_name, "ANIM"))
    states = car_states(ply2).get(anim_name.lower())
    textures = {}
    for name in clump.textures:
        tname, w, h, px = read_xbox_texture(get(name + ".RW3", "RWTX"))
        textures[name] = (w, h, px)
    log(report, "  %d vertices, %d triangles, %d bones, textures %s" % (
        len(clump.pos), len(clump.tris), clump.num_bones,
        ", ".join("%s %dx%d" % (n, t[0], t[1]) for n, t in textures.items())))
    log(report, "  animation: %s (the car's %s, zCar.cpp), %d bones, %d keys, %.3f s" % (
        anim_name, ", ".join(states) if states else "anim table does not use it",
        anim.bones, anim.kcount, anim.duration))
    if anim.bones != len(clump.hanim):
        raise SystemExit("animation has %d bones, the model %d" % (anim.bones, len(clump.hanim)))
    return clump, anim, textures


# The model faces +Z (yaw 0 is head-on, 90 drives to the right of the picture). -50 has the car
# coming out of the picture to the left, SpongeBob and Patrick in front of the flag that streams
# back. The banner camera looks straight along -Z from just above the ground, so the car is tipped
# towards it (pitch) to show the two over the bun.
CAR_YAW_DEG = -50.0
CAR_PITCH_DEG = 24.0


def view_rot(yaw, pitch):
    """3x3 (rows) of Rx(pitch) * Ry(yaw), column vectors; and its quaternion (xyzw)."""
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    m = [[cy, 0.0, sy], [sp * sy, cp, -sp * cy], [-cp * sy, sp, cp * cy]]
    qy = (0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2))
    qx = (math.sin(pitch / 2), 0.0, 0.0, math.cos(pitch / 2))
    # qx * qy
    x1, y1, z1, w1 = qx
    x2, y2, z2, w2 = qy
    q = (w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
         w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2)
    return m, q


def apply_rot(m, p):
    return tuple(m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] for i in range(3))


def placement(clump, anim, aspect=BANNER_CAM["aspect"], yfov=BANNER_CAM["yfov"], fill=0.88):
    """Scale, yaw and offset that put the car, over its whole animation, in the banner
    camera's view: three-quarter from the front, the projected outline filling `fill` of the
    picture's width or height and centred in it."""
    pts = []
    for i in range(6):
        pts += skin_points(clump, pose(clump.hanim, anim.eval(anim.duration * i / 5)))
    rot, _ = view_rot(math.radians(CAR_YAW_DEG), math.radians(CAR_PITCH_DEG))
    r = [apply_rot(rot, p) for p in pts]
    lo = [min(p[k] for p in r) for k in range(3)]
    hi = [max(p[k] for p in r) for k in range(3)]
    centre = [(lo[k] + hi[k]) / 2 for k in range(3)]
    f = 1.0 / math.tan(yfov / 2)
    cam = BANNER_CAM["pos"]

    def bounds(scale, ox, oy):
        xs, ys = [], []
        near = BANNER_CAM["znear"]
        for p in r:
            x = (p[0] - centre[0]) * scale + ox - cam[0]
            y = (p[1] - centre[1]) * scale + oy - cam[1]
            d = cam[2] - (p[2] - centre[2]) * scale
            if d < near:
                return -9.0, 9.0, -9.0, 9.0  # in front of the near plane: too big
            xs.append(x * f / aspect / d)
            ys.append(y * f / d)
        return min(xs), max(xs), min(ys), max(ys)

    ox, oy = cam[0], cam[1]
    lo_s, hi_s = 0.01, 100.0
    for _ in range(40):
        s = (lo_s + hi_s) / 2
        for _ in range(3):  # centre at this scale (perspective moves the centre)
            x0, x1, y0, y1 = bounds(s, ox, oy)
            ox -= (x0 + x1) / 2 * aspect * (cam[2]) / f
            oy -= (y0 + y1) / 2 * (cam[2]) / f
        x0, x1, y0, y1 = bounds(s, ox, oy)
        if max(x1 - x0, y1 - y0) <= 2 * fill:
            lo_s = s
        else:
            hi_s = s
    s = lo_s
    for _ in range(3):
        x0, x1, y0, y1 = bounds(s, ox, oy)
        ox -= (x0 + x1) / 2 * aspect * cam[2] / f
        oy -= (y0 + y1) / 2 * cam[2] / f
    # The offset applies after scale and rotation: undo the centre in that frame.
    offset = (ox - centre[0] * s, oy - centre[1] * s, -centre[2] * s)
    return s, rot, offset


def loop_points(clump, anim, parts, frames=None):
    """The shipped model's points (the rigid parts, or the skin with parts None), turned by the
    banner's yaw and tip, at every 60 Hz frame of the loop: [(frame, [points])]."""
    frames = frames or max(1, int(round(anim.duration * 60)))
    rot, _ = view_rot(math.radians(CAR_YAW_DEG), math.radians(CAR_PITCH_DEG))
    return [(k, [apply_rot(rot, p) for p in posed_mesh(clump, anim, anim.duration * k / frames,
                                                         parts)[0]])
            for k in range(frames)]


def screen_box(pts, scale, offset, w=400, h=240):
    """Top-screen pixel box (x0, x1, y0, y1) of turned points placed at scale and offset."""
    cam = BANNER_CAM["pos"]
    f = 1.0 / math.tan(BANNER_CAM["yfov"] / 2)
    a = BANNER_CAM["aspect"]
    x0 = y0 = 1e30
    x1 = y1 = -1e30
    for p in pts:
        d = cam[2] - (p[2] * scale + offset[2])
        if d < BANNER_CAM["znear"]:
            raise SystemExit("the model reaches in front of the banner camera's near plane")
        x = ((p[0] * scale + offset[0] - cam[0]) * f / a / d * 0.5 + 0.5) * w
        y = (0.5 - (p[1] * scale + offset[1] - cam[1]) * f / d * 0.5) * h
        x0, x1, y0, y1 = min(x0, x), max(x1, x), min(y0, y), max(y1, y)
    return x0, x1, y0, y1


def fit_placement(loop, top=FIT_TOP, bottom=FIT_BOTTOM, side=FIT_SIDE):
    """Scale and offset that put the whole loop between the HOME Menu's bars: the top `top`, the
    bottom `bottom` of the visible height clear, centred left to right with at least `side` of
    the width clear, as large as that allows (through the camera's perspective)."""
    pts = [p for _, ps in loop for p in ps]
    lo = [min(p[k] for p in pts) for k in range(3)]
    hi = [max(p[k] for p in pts) for k in range(3)]
    c = [(lo[k] + hi[k]) / 2 for k in range(3)]
    vis = VISIBLE_ROWS[1] - VISIBLE_ROWS[0]
    want_y0, want_y1 = VISIBLE_ROWS[0] + top * vis, VISIBLE_ROWS[1] - bottom * vis
    want_w = 400 * (1 - 2 * side)
    upp = 2 * math.tan(BANNER_CAM["yfov"] / 2) * BANNER_CAM["pos"][2] / 240  # units per pixel at z 0
    s = 1.0
    off = [-c[0] * s, BANNER_CAM["pos"][1] - c[1] * s, -c[2] * s]
    for _ in range(12):
        x0, x1, y0, y1 = screen_box(pts, s, off)
        k = min((want_y1 - want_y0) / (y1 - y0), want_w / (x1 - x0))
        # Grow or shrink about the box's centre, then move the box to where it is wanted.
        s *= k
        off = [-c[0] * s, BANNER_CAM["pos"][1] - c[1] * s, -c[2] * s]
        for _ in range(4):
            x0, x1, y0, y1 = screen_box(pts, s, off)
            off[0] -= ((x0 + x1) / 2 - 200) * upp
            if (want_y1 - want_y0) / (y1 - y0) <= want_w / (x1 - x0):
                off[1] += ((y0 + y1) / 2 - (want_y0 + want_y1) / 2) * upp
            else:  # width-bound: the top margin decides the height
                off[1] += (y0 - want_y0) * upp
    return s, tuple(off)


def fit_check(loop, scale, offset, report, top=FIT_TOP):
    """Logs the loop's box against the visible area over every frame; True when it all fits
    with `top` of the visible height clear above."""
    vis = VISIBLE_ROWS[1] - VISIBLE_ROWS[0]
    boxes = [(k, screen_box(ps, scale, offset)) for k, ps in loop]
    x0 = min(b[0] for _, b in boxes)
    x1 = max(b[1] for _, b in boxes)
    y0, f0 = min((b[2], k) for k, b in boxes)
    y1, f1 = max((b[3], k) for k, b in boxes)
    ok = y0 >= VISIBLE_ROWS[0] + top * vis - 0.5 and y1 <= VISIBLE_ROWS[1] and x0 >= 0 and x1 <= 400
    log(report, "  fit over all %d frames: x %.1f..%.1f of 400, y %.1f (frame %d)..%.1f (frame %d); "
        "visible rows %g..%g: top %.1f px clear (%.1f %% of %g), bottom %.1f px (%.1f %%), "
        "sides %.1f / %.1f px%s" % (
            len(boxes), x0, x1, y0, f0, y1, f1, VISIBLE_ROWS[0], VISIBLE_ROWS[1],
            y0 - VISIBLE_ROWS[0], 100 * (y0 - VISIBLE_ROWS[0]) / vis, vis,
            VISIBLE_ROWS[1] - y1, 100 * (VISIBLE_ROWS[1] - y1) / vis, x0, 400 - x1,
            "" if ok else "  ** OUTSIDE THE VISIBLE AREA OR THE TOP MARGIN **"))
    return ok


def skin_points(clump, world):
    return skin_mesh(clump, world)[0]


def skin_mesh(clump, world):
    """The skin's positions and unit normals at the bones' `world` matrices: smooth skinning,
    as the game draws the car."""
    mats = [m_mul(clump.inv_bind[i], world[i]) for i in range(clump.num_bones)]
    pts, nrms = [], []
    for v, nv, idx, w in zip(clump.pos, clump.nrm, clump.bone_idx, clump.bone_w):
        p, d = [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]
        for i, wt in zip(idx, w):
            if wt > 0:
                a, b = m_point(mats[i], v), m_dir(mats[i], nv)
                for k in range(3):
                    p[k] += wt * a[k]
                    d[k] += wt * b[k]
        ln = math.sqrt(sum(x * x for x in d)) or 1.0
        pts.append(tuple(p))
        nrms.append(tuple(x / ln for x in d))
    return pts, nrms


def rigid_choice(clump, anim, samples=32):
    """The first bone choice: each triangle rides the bone, among those its corners
    are weighted to, that keeps its corners closest to the skin (the largest distance at
    `samples` moments of the loop). {triangle: bone}."""
    gl0 = pose(clump.hanim, anim.eval(0.0), unit=True)
    pts0 = skin_points(clump, pose(clump.hanim, anim.eval(0.0)))
    inv0 = [m_inv(m) for m in gl0]
    moments = []  # (skin positions, each bone's frame-0-to-now matrix)
    for k in range(1, samples):
        local = anim.eval(anim.duration * k / samples)
        gl = pose(clump.hanim, local, unit=True)
        moments.append((skin_points(clump, pose(clump.hanim, local)),
                        [m_mul(inv0[i], gl[i]) for i in range(len(gl))]))
    nv = len(clump.pos)
    dom = [max(range(4), key=lambda k: clump.bone_w[v][k]) for v in range(nv)]
    dom = [clump.bone_idx[v][dom[v]] for v in range(nv)]

    def error(tri, bone):
        worst = 0.0
        for skin, move in moments:
            for v in tri:
                p = m_point(move[bone], pts0[v])
                worst = max(worst, sum((x - y) ** 2 for x, y in zip(p, skin[v])))
        return worst

    choice = {}
    for t, (a, b, c, m) in enumerate(clump.tris):
        ds = [dom[a], dom[b], dom[c]]
        first = max(set(ds), key=ds.count)  # the bone most corners follow, kept on a tie
        cands = sorted({i for v in (a, b, c) for i, w in zip(clump.bone_idx[v], clump.bone_w[v])
                        if w > 0} - {first})
        choice[t] = min([first] + cands, key=lambda i: error((a, b, c), i))
    return choice


# Seams between the rigid parts. Where two neighbouring triangles ride different bones, the corner
# they share has a copy on each bone, and the copies drift apart as the bones move differently
# from the skin's blend: a crack the background shows through. Two steps close it. The bone
# choice also weighs how far each seam opens (SEAM_DRIFT_WEIGHT trades a triangle's own distance
# from the skin against that). And each part carries a skirt: copies of its neighbours' triangles
# along its border (whole, or the strip of them that is needed), riding its own bone, reaching as
# far as the two parts ever pull apart there plus SEAM_MARGIN_PX, so whichever way a seam opens,
# a skirt fills it. Nothing new for the HOME Menu: more triangles on the same bones, one bone each.
SEAM_MARGIN_PX = 1.0
SEAM_MIN_OPEN_PX = 0.05  # borders that never open more than this get no skirt
SEAM_CAP_PX = 32.0
SEAM_DRIFT_WEIGHT = 0.5
SEAM_MIN_PIECE_PX2 = 0.01  # skirt pieces smaller than this on the screen are left out


class LoopMotion:
    """The game's skin and each bone's frame-0-to-now motion at every 60 Hz frame of the loop,
    the skin at frame 0, and the mesh's connectivity (corners at the same frame-0 position are
    one, across texture seams)."""

    def __init__(self, clump, anim, frames=None):
        self.clump = clump
        self.frames = frames or max(1, int(round(anim.duration * 60)))
        self.pts0, self.nrm0 = skin_mesh(clump, pose(clump.hanim, anim.eval(0.0)))
        self.gl0 = pose(clump.hanim, anim.eval(0.0), unit=True)
        inv0 = [m_inv(m) for m in self.gl0]
        self.skin, self.move = [], []
        for k in range(self.frames):
            local = anim.eval(anim.duration * k / self.frames)
            self.skin.append(skin_points(clump, pose(clump.hanim, local)))
            gl = pose(clump.hanim, local, unit=True)
            self.move.append([m_mul(inv0[i], gl[i]) for i in range(len(gl))])
        key = {}
        self.wid = [key.setdefault(tuple(round(x, 5) for x in p), len(key)) for p in self.pts0]
        self.edges = {}  # welded edge -> triangles
        for t, (a, b, c, m) in enumerate(clump.tris):
            for u, v in ((a, b), (b, c), (c, a)):
                self.edges.setdefault(tuple(sorted((self.wid[u], self.wid[v]))), []).append(t)
        self._traj = {}

    def traj(self, v, b):
        """Corner v on bone b at every frame."""
        k = (v, b)
        if k not in self._traj:
            p0 = self.pts0[v]
            self._traj[k] = [m_point(mv[b], p0) for mv in self.move]
        return self._traj[k]

    def drift(self, v, b):
        return max(math.dist(p, s[v]) for p, s in zip(self.traj(v, b), self.skin))

    def opening(self, v, a, b):
        if a == b:
            return 0.0
        return max(math.dist(p, q) for p, q in zip(self.traj(v, a), self.traj(v, b)))


def seam_choice(motion, start, weight=SEAM_DRIFT_WEIGHT, rounds=8):
    """The bone choice with the seams weighed in: each triangle in turn takes the bone (among
    those its corners are weighted to and its neighbours' bones) that minimises `weight` times
    its corners' largest distance from the skin over the loop plus, for each edge it shares
    with a triangle on another bone, how far that edge's ends ever pull apart (game units);
    repeated until no triangle changes. Returns ({triangle: bone}, rounds)."""
    clump = motion.clump
    nbr = [[] for _ in clump.tris]
    for e, ts in sorted(motion.edges.items()):
        for t in ts:
            ends = tuple(v for v in clump.tris[t][:3] if motion.wid[v] in e)[:2]
            nbr[t] += [(u, ends) for u in ts if u != t]
    weighted = [{i for v in tri[:3] for i, w in zip(clump.bone_idx[v], clump.bone_w[v]) if w > 0}
                for tri in clump.tris]
    bone = dict(start)
    memo = {}

    def drift(t, b):
        if (t, b) not in memo:
            memo[(t, b)] = max(motion.drift(v, b) for v in clump.tris[t][:3])
        return memo[(t, b)]

    def cost(t, b):
        c = weight * drift(t, b)
        for u, ends in nbr[t]:
            if bone[u] != b:
                c += max(motion.opening(v, b, bone[u]) for v in ends)
        return c

    for n in range(rounds):
        changed = 0
        for t in range(len(clump.tris)):
            cands = sorted(weighted[t] | {bone[u] for u, _ in nbr[t]})
            best = min(cands, key=lambda b: cost(t, b))
            if cost(t, best) < cost(t, bone[t]) - 1e-12:
                bone[t] = best
                changed += 1
        if not changed:
            return bone, n + 1
    return bone, rounds


def screen_xy(p, place, w=400, h=240):
    """Top-screen pixel of a model point placed by (scale, rotation, offset)."""
    scale, rot, offset = place
    x, y, z = apply_rot(rot, p)
    x, y, z = x * scale + offset[0], y * scale + offset[1], z * scale + offset[2]
    cam = BANNER_CAM["pos"]
    f = 1.0 / math.tan(BANNER_CAM["yfov"] / 2)
    d = cam[2] - z
    return (((x - cam[0]) * f / BANNER_CAM["aspect"] / d * 0.5 + 0.5) * w,
            (0.5 - (y - cam[1]) * f / d * 0.5) * h)


def clip_triangle(d, r):
    """The part of a triangle where its corner values d (interpolated across it) are below r,
    as triangles of barycentric corners."""
    corners = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
    poly = []
    for i in range(3):
        j = (i + 1) % 3
        if d[i] < r:
            poly.append(corners[i])
        if (d[i] < r) != (d[j] < r):
            t = (r - d[i]) / (d[j] - d[i])
            poly.append(tuple(corners[i][k] + (corners[j][k] - corners[i][k]) * t for k in range(3)))
    return [(poly[0], poly[k], poly[k + 1]) for k in range(1, len(poly) - 1)]


def seam_skirts(motion, bone, place, margin=SEAM_MARGIN_PX, min_open=SEAM_MIN_OPEN_PX,
                cap=SEAM_CAP_PX):
    """Skirt pieces [(triangle, bone, barycentric corners, or None for the whole triangle)]:
    each bone's part gets its neighbours' triangles out to a distance (top-screen pixels along
    the surface at frame 0, from the part's own corners) of `margin` plus how far the two parts
    ever pull apart on the screen there (from the part's nearest corner)."""
    import heapq
    clump, wid = motion.clump, motion.wid
    scr0 = {}
    for v, p in enumerate(motion.pts0):
        scr0.setdefault(wid[v], screen_xy(p, place))
    adj = {}
    for (a, b, c, m) in clump.tris:
        for u, v in ((a, b), (b, c), (c, a)):
            L = math.dist(scr0[wid[u]], scr0[wid[v]])
            adj.setdefault(wid[u], {})[wid[v]] = L
            adj.setdefault(wid[v], {})[wid[u]] = L
    memo = {}

    def open_px(v, a, b):
        k = (v,) + tuple(sorted((a, b)))
        if k not in memo:
            memo[k] = 0.0 if a == b else max(
                math.dist(screen_xy(p, place), screen_xy(q, place))
                for p, q in zip(motion.traj(v, a), motion.traj(v, b)))
        return memo[k]

    by_bone = {}
    for t, b in sorted(bone.items()):
        by_bone.setdefault(b, []).append(t)
    out = []
    for a, own in sorted(by_bone.items()):
        dist, seed, heap = {}, {}, []
        for t in own:
            for v in clump.tris[t][:3]:
                if wid[v] not in dist:
                    dist[wid[v]], seed[wid[v]] = 0.0, v
                    heap.append((0.0, wid[v]))
        heapq.heapify(heap)
        while heap:
            d, w = heapq.heappop(heap)
            if d > dist[w] or d > cap + margin:
                continue
            for x, L in sorted(adj[w].items()):
                if d + L < dist.get(x, 1e30):
                    dist[x], seed[x] = d + L, seed[w]
                    heapq.heappush(heap, (d + L, x))
        own = set(own)
        for u, tri in enumerate(clump.tris):
            if u in own:
                continue
            ds = [dist.get(wid[v], 1e30) for v in tri[:3]]
            if min(ds) > cap + margin:
                continue
            op = max(open_px(seed[wid[v]], a, bone[u]) for v, d in zip(tri[:3], ds) if d < 1e29)
            if op < min_open:
                continue  # the two parts never pull apart here
            r = margin + min(op, cap)
            if min(ds) >= r:
                continue
            if max(ds) < r:
                out.append((u, a, None))
                continue
            sp = [scr0[wid[v]] for v in tri[:3]]
            for piece in clip_triangle(ds, r):
                q = [tuple(sum(l[i] * sp[i][k] for i in range(3)) for k in range(2)) for l in piece]
                area = abs((q[1][0] - q[0][0]) * (q[2][1] - q[0][1]) -
                           (q[1][1] - q[0][1]) * (q[2][0] - q[0][0])) / 2
                if area >= SEAM_MIN_PIECE_PX2:
                    out.append((u, a, piece))
    return out


def seam_report(motion, bone, place, copies, rounds, report):
    """Logs how the parts meet: edges between parts, how far they open, the skirts."""
    clump = motion.clump
    worst, n_open, n_border = 0.0, 0, 0
    for e, ts in motion.edges.items():
        bs = sorted({bone[t] for t in ts})
        if len(bs) < 2:
            continue
        n_border += 1
        ends = [v for v in clump.tris[ts[0]][:3] if motion.wid[v] in e]
        o = max(math.dist(screen_xy(p, place), screen_xy(q, place))
                for v in ends for x in bs for y in bs if x < y
                for p, q in zip(motion.traj(v, x), motion.traj(v, y)))
        worst = max(worst, o)
        n_open += o >= SEAM_MIN_OPEN_PX
    log(report, "  seams: bone choice settled in %d rounds; %d edges between parts, %d of them "
        "open >= %g px in the loop (the widest %.1f px); skirts (margin %g px): %d whole "
        "triangles, %d pieces" % (rounds, n_border, n_open, SEAM_MIN_OPEN_PX, worst,
                                  SEAM_MARGIN_PX, sum(1 for c in copies if c[2] is None),
                                  sum(1 for c in copies if c[2] is not None)))


def rigid_parts(clump, anim, place=None, seams=True, motion=None, report=None):
    """The skin split into rigid parts, one per (bone, material), for the CGFX (skinning mode
    0: each part rides one bone). Its corners are the skin's positions at the loop's first
    frame (the game's own smoothly skinned pose) moved into the bone's space at the same frame,
    so at frame 0 every part sits exactly where the game draws the skin; seams open only as the
    bones move away from it. Each triangle rides one bone: with `seams`, seam_choice() starting
    from rigid_choice(), and given `place` (scale, rotation, offset) each part also gets its
    skirt (seam_skirts()); without `seams`, rigid_choice() and no skirts (--no-seam-fix)."""
    choice = rigid_choice(clump, anim)
    copies = []
    if seams:
        motion = motion or LoopMotion(clump, anim)
        choice, rounds = seam_choice(motion, choice)
        if place is not None:
            copies = seam_skirts(motion, choice, place)
            if report is not None:
                seam_report(motion, choice, place, copies, rounds, report)
    if motion is not None:
        pts0, nrm0, gl0 = motion.pts0, motion.nrm0, motion.gl0
    else:
        pts0, nrm0 = skin_mesh(clump, pose(clump.hanim, anim.eval(0.0)))
        gl0 = pose(clump.hanim, anim.eval(0.0), unit=True)
    groups = {}
    for t in range(len(clump.tris)):
        groups.setdefault((choice[t], clump.tris[t][3]), []).append((t, None))
    for u, a, piece in copies:
        groups.setdefault((a, clump.tris[u][3]), []).append((u, piece))
    parts = []
    for (bone, m), tris in sorted(groups.items()):
        inv = m_inv(gl0[bone])
        remap, pos, nrm, uv, idx = {}, [], [], [], []
        for t, piece in tris:
            vs = clump.tris[t][:3]
            for k in range(3):
                if piece is None:
                    mix = ((vs[k], 1.0),)
                else:
                    mix = tuple((v, l) for v, l in zip(vs, piece[k]) if abs(l) > 1e-9)
                # A skirt corner on one of the triangle's corners is that corner; one between
                # them is shared by the pieces that meet there.
                key = mix[0][0] if len(mix) == 1 else tuple(sorted((v, round(l, 6)) for v, l in mix))
                if key not in remap:
                    remap[key] = len(pos)
                    p = tuple(sum(l * pts0[v][i] for v, l in mix) for i in range(3))
                    d = m_dir(inv, tuple(sum(l * nrm0[v][i] for v, l in mix) for i in range(3)))
                    ln = math.sqrt(sum(x * x for x in d)) or 1.0
                    pos.append(m_point(inv, p))
                    nrm.append(tuple(x / ln for x in d))
                    uv.append(tuple(sum(l * clump.uv[v][i] for v, l in mix) for i in range(2)))
                idx.append(remap[key])
        # src: the skin triangle each of the part's triangles is (or, in the skirt, is a piece of)
        parts.append(dict(bone=bone, material=m, pos=pos, nrm=nrm, uv=uv, idx=idx,
                          src=[t for t, _ in tris]))
    return parts


def posed_mesh(clump, anim, time, parts=None):
    """(positions, normals, uvs, triangles (a, b, c, material)) at `time`: the rigid parts on
    their bones, as the CGFX carries them; with no parts, the game's smooth skin."""
    if parts is None:
        pts, nrms = skin_mesh(clump, pose(clump.hanim, anim.eval(time)))
        return pts, nrms, clump.uv, clump.tris
    world = pose(clump.hanim, anim.eval(time), unit=True)
    pts, nrms, uvs, tris = [], [], [], []
    for part in parts:
        mtx, base, ix = world[part["bone"]], len(pts), part["idx"]
        pts += [m_point(mtx, p) for p in part["pos"]]
        nrms += [m_dir(mtx, d) for d in part["nrm"]]
        uvs += part["uv"]
        tris += [(base + ix[k], base + ix[k + 1], base + ix[k + 2], part["material"])
                 for k in range(0, len(ix), 3)]
    return pts, nrms, uvs, tris


# ---------------------------------------------------------------------------
# Two-sided sheets. The game draws the car without face culling: zEntPlayer_Render draws it with
# the model buckets off (src/SB/Game/zEntPlayer.cpp:10230-10233) under
# zRenderState(SDRS_OpaqueModels) (src/SB/Game/zScene.cpp:3167; the player is drawn at 3209),
# which leaves rwCULLMODECULLNONE, so both sides of every face show. The CGFX keeps pycgfx's
# back-face culling: the car's closed parts look the same either way, but its single sheets (the
# propeller, the flag's cloth, the windshield and a small flat piece under the front) vanish from
# behind. A sheet is an open piece of the skin that is near flat: every face within 60 degrees of
# the piece's mean (the car's other open pieces are curved).
# The sheets get back faces of their own: each of their triangles once more, wound the other way,
# with its normals turned round, on the same bone with the same UVs. Culling none in the CGFX
# would light those back faces black (they would keep the front's normals) and still leave the
# glTF one-sided in a viewer.
SHEET_MIN_DOT = 0.5  # cos 60 degrees: a sheet's faces against its mean face normal


def sheet_triangles(clump, report):
    """{triangle} of the car's single sheets: the pieces of the skin (triangles joined by shared
    edges, corners at the same position being one) that have a border (an edge only one triangle
    has) and whose faces' normals (in the bind pose) are all within 60 degrees of the piece's
    mean (SHEET_MIN_DOT). A triangle that already has a reversed twin (two-sided already) is
    left out."""
    wid, key = [], {}
    for p in clump.pos:
        wid.append(key.setdefault(tuple(round(x, 5) for x in p), len(key)))
    edges = {}
    for t, (a, b, c, m) in enumerate(clump.tris):
        for u, v in ((a, b), (b, c), (c, a)):
            edges.setdefault(tuple(sorted((wid[u], wid[v]))), []).append(t)

    def bone_of(t):
        w = {}
        for v in clump.tris[t][:3]:
            for i, x in zip(clump.bone_idx[v], clump.bone_w[v]):
                w[i] = w.get(i, 0.0) + x
        return max(sorted(w), key=lambda i: w[i])

    def wound(a, b, c):  # the same for the same corners in the same turning order
        return min((a, b, c), (b, c, a), (c, a, b))

    faces = {wound(*(wid[v] for v in tri[:3])) for tri in clump.tris}

    def face_normal(t):
        a, b, c = (clump.pos[v] for v in clump.tris[t][:3])
        u, v = [b[i] - a[i] for i in range(3)], [c[i] - a[i] for i in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        ln = math.sqrt(sum(x * x for x in n)) or 1.0
        return tuple(x / ln for x in n)

    seen, sheet = set(), set()
    for start in range(len(clump.tris)):
        if start in seen:
            continue
        piece, todo = set(), [start]
        seen.add(start)
        while todo:
            t = todo.pop()
            piece.add(t)
            a, b, c = clump.tris[t][:3]
            for u, v in ((a, b), (b, c), (c, a)):
                for n in edges[tuple(sorted((wid[u], wid[v])))]:
                    if n not in seen:
                        seen.add(n)
                        todo.append(n)
        bones = sorted({bone_of(t) for t in piece})
        border = sum(1 for ts in edges.values() if len(ts) == 1 and ts[0] in piece)
        twins = {t for t in piece if wound(*(wid[v] for v in clump.tris[t][2::-1])) in faces}
        if not border:
            continue
        ns = [face_normal(t) for t in piece]
        mean = [sum(n[i] for n in ns) for i in range(3)]
        ml = math.sqrt(sum(x * x for x in mean)) or 1.0
        flat = min(sum(n[i] * mean[i] for i in range(3)) / ml for n in ns)
        if flat < SHEET_MIN_DOT:
            continue
        sheet |= piece - twins
        log(report, "  sheet (bone%s %s): %d triangles, %d border edges, flatness %.2f, %d with a "
            "reversed twin: %d back faces" % ("s" if len(bones) > 1 else "",
                                              ",".join(map(str, bones)), len(piece), border, flat,
                                              len(twins), len(piece - twins)))
    return sheet


def back_faces(count, tris, sheet):
    """For triangles (a, b, c, src) over `count` corners: (origin, the triangles with a back face
    after them for each one whose src is in `sheet`). The back faces' corners are new ones
    numbered from `count` on, the k-th standing for corner origin[k] (the caller gives it the
    same place and UV and the normal turned round); they are wound a, c, b and shared as their
    originals are."""
    origin, copy_of, out = [], {}, list(tris)
    for a, b, c, src in tris:
        if src in sheet:
            for v in (a, b, c):
                if v not in copy_of:
                    copy_of[v] = count + len(origin)
                    origin.append(v)
            out.append((copy_of[a], copy_of[c], copy_of[b], src))
    return origin, out


def two_sided(parts, clump, sheet):
    """The rigid parts (or with parts None, a copy of the clump: the skin, for --skinned) with
    back faces for the sheet's triangles, the skirt pieces of them too. Returns (parts or skin,
    back faces added)."""
    def turned(nrm, origin):
        return nrm + [tuple(-x for x in nrm[v]) for v in origin]

    if parts is None:
        skin = copy.copy(clump)
        tris = [(a, b, c, t) for t, (a, b, c, m) in enumerate(clump.tris)]
        origin, tris = back_faces(len(clump.pos), tris, sheet)
        for name in ("pos", "uv", "bone_idx", "bone_w"):
            setattr(skin, name, getattr(clump, name) + [getattr(clump, name)[v] for v in origin])
        skin.nrm = turned(clump.nrm, origin)
        skin.tris = [(a, b, c, clump.tris[t][3]) for a, b, c, t in tris]
        return skin, len(skin.tris) - len(clump.tris)
    out, added = [], 0
    for part in parts:
        ix = part["idx"]
        tris = [(ix[k], ix[k + 1], ix[k + 2], part["src"][k // 3]) for k in range(0, len(ix), 3)]
        origin, tris = back_faces(len(part["pos"]), tris, sheet)
        added += len(tris) - len(ix) // 3
        out.append(dict(part, pos=part["pos"] + [part["pos"][v] for v in origin],
                        nrm=turned(part["nrm"], origin),
                        uv=part["uv"] + [part["uv"][v] for v in origin],
                        idx=[v for t in tris for v in t[:3]], src=[t[3] for t in tris]))
    return out, added


# ---------------------------------------------------------------------------
# glTF


class GltfBuilder:
    def __init__(self):
        self.bin = bytearray()
        self.views, self.accessors = [], []

    def add(self, fmt, rows, comp, typ, target=None, minmax=False):
        while len(self.bin) % 4:
            self.bin.append(0)
        off = len(self.bin)
        for r in rows:
            self.bin += struct.pack("<" + fmt, *r) if isinstance(r, (tuple, list)) else \
                struct.pack("<" + fmt, r)
        view = dict(buffer=0, byteOffset=off, byteLength=len(self.bin) - off)
        if target:
            view["target"] = target
        self.views.append(view)
        acc = dict(bufferView=len(self.views) - 1, componentType=comp, count=len(rows), type=typ)
        if minmax:
            cols = list(zip(*[r if isinstance(r, (tuple, list)) else (r,) for r in rows]))
            acc["min"] = [min(c) for c in cols]
            acc["max"] = [max(c) for c in cols]
        self.accessors.append(acc)
        return len(self.accessors) - 1


def build_gltf(clump, anim, textures, place, out_dir, parts, report, fps=60, anim_label="boost"):
    """The glTF for pycgfx: the rigid `parts` on their bones, or with parts None the skin."""
    scale, _, offset = place
    _, root_q = view_rot(math.radians(CAR_YAW_DEG), math.radians(CAR_PITCH_DEG))
    g = GltfBuilder()
    nodes_h = clump.hanim
    parents = hanim_parents(nodes_h)
    n = len(nodes_h)

    # Images (halved) and materials, one per texture.
    images, gl_textures, materials = [], [], []
    for i, name in enumerate(clump.textures):
        w, h, px = textures[name]
        small = downscale(px, w, h, TEX_SIZE, TEX_SIZE) if w > TEX_SIZE else px
        uri = "data:image/png;base64," + base64.b64encode(png_bytes(small, TEX_SIZE, TEX_SIZE)).decode()
        images.append(dict(name=name, uri=uri))
        gl_textures.append(dict(source=i))
        materials.append(dict(name=name, pbrMetallicRoughness=dict(
            baseColorTexture=dict(index=i), metallicFactor=0.0, roughnessFactor=1.0)))

    # Nodes: 0 = placement root, then the bones in HAnim order.
    rest = anim.eval(0.0)
    gl_nodes = [dict(name="banner_root", children=[],
                     translation=list(offset), scale=[scale] * 3,
                     rotation=list(root_q))]
    for i in range(n):
        q, t = rest[i]
        gl_nodes.append(dict(name="bone%02d" % i, rotation=list(qnorm(q)), translation=list(t),
                             children=[]))
    for i in range(n):
        (gl_nodes[0] if parents[i] < 0 else gl_nodes[1 + parents[i]])["children"].append(1 + i)

    meshes = []
    if parts is not None:
        for part in parts:
            bone, m = part["bone"], part["material"]
            prim = dict(attributes=dict(
                POSITION=g.add("3f", part["pos"], 5126, "VEC3", 34962, True),
                NORMAL=g.add("3f", part["nrm"], 5126, "VEC3", 34962),
                TEXCOORD_0=g.add("2f", part["uv"], 5126, "VEC2", 34962)),
                indices=g.add("H", part["idx"], 5123, "SCALAR", 34963), material=m)
            meshes.append(dict(name="bone%02d_%s" % (bone, clump.textures[m]), primitives=[prim]))
            gl_nodes[1 + bone].setdefault("mesh", len(meshes) - 1)
            if gl_nodes[1 + bone]["mesh"] != len(meshes) - 1:
                # A second material on the same bone: a child node with no transform.
                gl_nodes.append(dict(name="bone%02d_m%d" % (bone, m), mesh=len(meshes) - 1))
                gl_nodes[1 + bone]["children"].append(len(gl_nodes) - 1)
        skins = []
    else:
        prims = []
        for m in range(len(clump.textures)):
            idx = [v for (a, b, c, mm) in clump.tris if mm == m for v in (a, b, c)]
            if not idx:
                continue
            prims.append(dict(attributes=None, indices=g.add("H", idx, 5123, "SCALAR", 34963),
                              material=m))
        attrs = dict(
            POSITION=g.add("3f", clump.pos, 5126, "VEC3", 34962, True),
            NORMAL=g.add("3f", clump.nrm, 5126, "VEC3", 34962),
            TEXCOORD_0=g.add("2f", clump.uv, 5126, "VEC2", 34962),
            JOINTS_0=g.add("4B", clump.bone_idx, 5121, "VEC4", 34962),
            WEIGHTS_0=g.add("4f", clump.bone_w, 5126, "VEC4", 34962))
        for p in prims:
            p["attributes"] = attrs
        meshes.append(dict(name="patty_wagon", primitives=prims))
        ibm = []
        for i in range(n):
            mtx = clump.inv_bind[i]
            ibm.append(tuple(mtx))  # row-vector row-major = column-vector column-major
        skins = [dict(joints=[1 + i for i in range(n)], skeleton=1,
                      inverseBindMatrices=g.add("16f", ibm, 5126, "MAT4"))]
        gl_nodes.append(dict(name="patty_wagon", mesh=0, skin=0))
        gl_nodes[0]["children"].append(len(gl_nodes) - 1)

    # Animation: the bones' local rotation and translation, sampled.
    steps = max(2, int(round(anim.duration * fps)) + 1)
    times = [anim.duration * i / (steps - 1) for i in range(steps)]
    samples = [anim.eval(t) for t in times]
    t_acc = g.add("f", times, 5126, "SCALAR", None, True)
    channels, samplers = [], []
    for i in range(n):
        qs = []
        prev = None
        for s in samples:
            q = qnorm(s[i][0])
            if prev is not None and sum(a * b for a, b in zip(q, prev)) < 0:
                q = [-v for v in q]
            qs.append(tuple(q))
            prev = q
        ts = [tuple(s[i][1]) for s in samples]
        # A channel that never leaves the rest pose (the node's own TRS) is left out: the
        # CGFX's animation is most of its size, and its limit is 512 KB.
        for path, vals, fmt, typ in (("rotation", qs, "4f", "VEC4"),
                                     ("translation", ts, "3f", "VEC3")):
            if all(max(abs(a - b) for a, b in zip(v, vals[0])) < 1e-4 for v in vals):
                continue
            samplers.append(dict(input=t_acc, output=g.add(fmt, vals, 5126, typ),
                                 interpolation="LINEAR"))
            channels.append(dict(sampler=len(samplers) - 1, target=dict(node=1 + i, path=path)))

    for nd in gl_nodes:
        if "children" in nd and not nd["children"]:
            del nd["children"]
    doc = dict(asset=dict(version="2.0", generator="banner_from_game_files.py"),
               scene=0, scenes=[dict(nodes=[0])], nodes=gl_nodes, meshes=meshes,
               materials=materials, textures=gl_textures, images=images,
               samplers=[dict(magFilter=9729, minFilter=9729)],
               animations=[dict(name=anim_label, channels=channels, samplers=samplers)],
               buffers=[dict(uri="banner_model.bin", byteLength=len(g.bin))],
               bufferViews=g.views, accessors=g.accessors)
    for t in gl_textures:
        t["sampler"] = 0
    if skins:
        doc["skins"] = skins
    with open(os.path.join(out_dir, "banner_model.bin"), "wb") as f:
        f.write(g.bin)
    with open(os.path.join(out_dir, "banner_model.gltf"), "w") as f:
        json.dump(doc, f)
    log(report, "glTF: %s, %d meshes, %d nodes, %d animation samples (%d Hz) over %.3f s, "
        "%d animated channels of %d, %d bytes of buffer" % (
            "skinned" if parts is None else "rigid parts on the bones (frame 0 = the game's skin)",
            len(meshes), len(gl_nodes),
            steps, fps, anim.duration, len(channels), 2 * n, len(g.bin)))


# ---------------------------------------------------------------------------
# Preview: a small software renderer from the banner camera


def render(clump, mesh, textures, place, w, h, aspect, bg, yfov=None, cull="none"):
    """Draws a posed_mesh() from the banner camera; cull "back" leaves out back faces, as the
    CGFX does (cull "none" draws both sides, as the game does)."""
    scale, rot, offset = place
    pts, nrms, uvs, tris = mesh

    def place_p(p):
        x, y, z = apply_rot(rot, p)
        return (x * scale + offset[0], y * scale + offset[1], z * scale + offset[2])
    cam = BANNER_CAM["pos"]
    f = 1.0 / math.tan((yfov or BANNER_CAM["yfov"]) / 2)
    light = (0.4, 0.7, 0.6)
    ln = math.sqrt(sum(x * x for x in light))
    light = tuple(x / ln for x in light)
    sp, sh = [], []
    for p, nv in zip(pts, nrms):
        x, y, z = place_p(p)
        z -= cam[2]
        y -= cam[1]
        x -= cam[0]
        d = -z
        sp.append(((x * f / aspect / d * 0.5 + 0.5) * w, (0.5 - y * f / d * 0.5) * h, d))
        nx, ny, nz = apply_rot(rot, nv)
        ln = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
        sh.append(0.55 + 0.45 * max(0.0, (nx * light[0] + ny * light[1] + nz * light[2]) / ln))
    zb = [1e30] * (w * h)
    img = [bg(x, y) for y in range(h) for x in range(w)]
    for (a, b, c, m) in tris:
        tw, th, tpx = textures[clump.textures[m]]
        A, B, C = sp[a], sp[b], sp[c]
        minx, maxx = max(0, int(min(A[0], B[0], C[0]))), min(w - 1, int(max(A[0], B[0], C[0])) + 1)
        miny, maxy = max(0, int(min(A[1], B[1], C[1]))), min(h - 1, int(max(A[1], B[1], C[1])) + 1)
        area = (B[0] - A[0]) * (C[1] - A[1]) - (B[1] - A[1]) * (C[0] - A[0])
        # Front faces are counter-clockwise on the y-up screen: area < 0 here (y down).
        if area == 0 or (cull == "back" and area > 0) or minx > maxx or miny > maxy:
            continue
        ua, ub, uc = uvs[a], uvs[b], uvs[c]
        ia, ib_, ic = 1 / A[2], 1 / B[2], 1 / C[2]
        for y in range(miny, maxy + 1):
            py = y + 0.5
            for x in range(minx, maxx + 1):
                px_ = x + 0.5
                w0 = ((B[0] - px_) * (C[1] - py) - (B[1] - py) * (C[0] - px_)) / area
                w1 = ((C[0] - px_) * (A[1] - py) - (C[1] - py) * (A[0] - px_)) / area
                w2 = 1 - w0 - w1
                if w0 < 0 or w1 < 0 or w2 < 0:
                    continue
                iz = w0 * ia + w1 * ib_ + w2 * ic
                z = 1 / iz
                k = y * w + x
                if z >= zb[k]:
                    continue
                u = (w0 * ua[0] * ia + w1 * ub[0] * ib_ + w2 * uc[0] * ic) * z
                v = (w0 * ua[1] * ia + w1 * ub[1] * ib_ + w2 * uc[1] * ic) * z
                t = tpx[(int(v * th) % th) * tw + int(u * tw) % tw]
                if t[3] < 128:
                    continue
                zb[k] = z
                s = w0 * sh[a] + w1 * sh[b] + w2 * sh[c]
                img[k] = (min(255, int(t[0] * s)), min(255, int(t[1] * s)),
                          min(255, int(t[2] * s)), 255)
    return img


def sea_bg(w, h):
    def bg(x, y):
        t = y / (h - 1)
        return (int(60 + 80 * (1 - t)), int(150 + 70 * (1 - t)), int(210 + 40 * (1 - t)), 255)
    return bg


def home_bg(x, y):
    return (235, 235, 235, 255)


# ---------------------------------------------------------------------------
# The CGFX as written: where its textures and lookup tables start


def cgfx_data_positions(b):
    """(kind, name, file position, length) of each texture's pixels and each lookup table's
    commands in a CGFX. The DATA block's dictionaries start at 0x1C, 8 bytes each (a count and
    a self-relative offset of the DICT): textures at 0x24, lookup-table sets at 0x2C. A DICT's
    entries are 16 bytes from its +0x1C, with self-relative offsets of the name at +8 and of
    the object at +12. A texture's image is at +56 of its TXOB, the pixels' length and offset
    at +8 and +12 of the image; a set's tables are a DICT at +24 of the LUTS, a table's
    commands' length and offset at +12 and +16 of it."""
    def i32(o):
        if not 0 <= o <= len(b) - 4:
            raise ValueError("offset 0x%x outside the CGFX" % o)
        return struct.unpack_from("<i", b, o)[0]

    def rel(o):
        v = i32(o)
        return o + v if v else None

    def entries(o):
        out = []
        d = rel(o + 4)
        for k in range(i32(o) if d is not None else 0):
            node = d + 12 + 16 * (k + 1)
            name = rel(node + 8)
            out.append((b[name:b.index(b"\0", name)].decode("latin-1"), rel(node + 12)))
        return out

    if b[:4] != b"CGFX" or b[0x14:0x18] != b"DATA":
        raise ValueError("not a CGFX")
    found = []
    for name, tex in entries(0x24):
        if i32(tex) != 0x20000011:  # an image texture (a reference texture has no pixels)
            continue
        img = rel(tex + 56)
        found.append(("texture", name, rel(img + 12), i32(img + 8)))
    for _, luts in entries(0x2C):
        for name, table in entries(luts + 24):
            found.append(("lookup table", name, rel(table + 16), i32(table + 12)))
    for kind, name, pos, n in found:
        if pos is None or pos + n > len(b):
            raise ValueError("%s %s: data outside the CGFX" % (kind, name))
    return found


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


def find_tool(value, option):
    """A tool given by its path, or by its bare name when it is on the PATH."""
    if os.path.dirname(value):
        path = os.path.abspath(value)
        if not os.path.isfile(path):
            raise SystemExit("%s: %s not found" % (option, path))
        return path
    path = shutil.which(value)
    if path is None:
        raise SystemExit("%s: %s is not on the PATH; give its full path" % (option, value))
    return path


def main():
    ap = argparse.ArgumentParser(
        description=__doc__.split("\n\n")[0],
        epilog="Example (from the repository's folder): python3 "
               "tools/3ds/banner_from_game_files.py $HOME/tssm-files $HOME/tssm-banner --pycgfx "
               "$HOME/pycgfx --pycgfx-python $HOME/pycgfx/venv/bin/python --bannertool bannertool. "
               "Then -DTSSM_3DS_BANNER=$HOME/tssm-banner/banner.bnr for the .cia (docs/3DS.md). "
               "The banner is made from your copy of the game: do not share it or the .cia.")
    ap.add_argument("game_files", help="the folder with your game files (BB/, MN/, PL/ ...)")
    ap.add_argument("out_dir", help="where the banner's files go (made if missing; not inside "
                                    "a git work tree)")
    ap.add_argument("--pycgfx", help="pycgfx's folder (main.py); makes banner.cgfx")
    ap.add_argument("--pycgfx-python", default=sys.executable,
                    help="the Python (3.10 or later) that has gltflib and Pillow for pycgfx: "
                         "its path, or its name if it is on the PATH")
    ap.add_argument("--bannertool", help="bannertool's executable (its path, or just bannertool "
                                         "if it is on the PATH); makes the .bnr files")
    ap.add_argument("--skinned", action="store_true",
                    help="keep the skin (smooth skinning) instead of rigid parts on the bones; "
                         "for trying only (smooth skinning is reported to freeze the HOME Menu)")
    ap.add_argument("--no-preview", action="store_true", help="skip the preview pictures")
    ap.add_argument("--two-sided-sheets", choices=("on", "off"), default="on",
                    help="back faces for the car's single sheets (the propeller, the flag's "
                         "cloth, the windshield and a small flat piece under the front), which "
                         "the game shows from both sides and the CGFX's back-face culling would "
                         "hide from behind (default on; off makes the CGFX of earlier versions "
                         "of this script)")
    ap.add_argument("--no-seam-fix", action="store_true",
                    help="each triangle's bone chosen alone and no skirts over the seams "
                         "between the parts (smaller CGFX; cracks can show while it moves)")
    ap.add_argument("--anim", default=DEFAULT_ANIM,
                    help="the car's animation (PL/PLY2.HIP), e.g. patty_wagon_drive.anm (the "
                         "Drive state) or patty_wagon_idle.anm (Idle01); default %(default)s "
                         "(Boosting). The default --scale/--offset are the boost's: with "
                         "another animation use --fit")
    ap.add_argument("--scale", type=float, default=BANNER_SCALE,
                    help="the model's scale on the banner (default %(default)s)")
    ap.add_argument("--offset", default=",".join("%g" % v for v in BANNER_OFFSET),
                    help="the model's X,Y,Z offset on the banner, after the scale (default "
                         "%(default)s); Y up, Z towards the camera")
    ap.add_argument("--fit", action="store_true",
                    help="instead of --scale/--offset, fit the whole loop between the HOME "
                         "Menu's bars with the margins below and use (and print) that")
    ap.add_argument("--top-margin", type=float, default=FIT_TOP,
                    help="--fit: share of the visible height kept clear above (default %(default)s)")
    ap.add_argument("--bottom-margin", type=float, default=FIT_BOTTOM,
                    help="--fit: share of the visible height kept clear below (default %(default)s)")
    args = ap.parse_args()
    args.offset = tuple(float(v) for v in args.offset.split(","))
    if len(args.offset) != 3:
        raise SystemExit("--offset takes X,Y,Z")
    for k in ("game_files", "out_dir", "pycgfx"):
        if getattr(args, k):
            setattr(args, k, os.path.abspath(getattr(args, k)))
    # Tools: a bare name (such as bannertool or python) is looked up on the PATH; checked now so
    # a wrong one stops the run before the work, not at its end.
    if args.pycgfx:
        args.pycgfx_python = find_tool(args.pycgfx_python, "--pycgfx-python")
    if args.bannertool:
        args.bannertool = find_tool(args.bannertool, "--bannertool")
    tree = git_work_tree(args.out_dir)
    if tree:
        raise SystemExit("%s is inside the git work tree %s: the banner holds the game's model, "
                         "textures and voice, so it must not go where a commit can pick it up. "
                         "Choose a folder outside any repository." % (args.out_dir, tree))
    os.makedirs(args.out_dir, exist_ok=True)
    out = lambda name: os.path.join(args.out_dir, name)  # noqa: E731
    report = []

    # Sound
    entry, data, names = find_macho_clip(args.game_files, report)
    if entry["format"] == 0x69:
        chans = decode_xbox_adpcm(data, entry["channels"], entry["block_align"])
    else:
        raw = struct.unpack("<%dh" % (len(data) // 2), data[:len(data) // 2 * 2])
        chans = [list(raw[c::entry["channels"]]) for c in range(entry["channels"])]
    rate = entry["rate"]
    write_wav(out("banner_sound_full.wav"), chans, rate)
    mono = chans[0] if len(chans) == 1 else [sum(v) // len(v) for v in zip(*chans)]
    fitted, fit = fit_sound(mono, rate, report)
    write_wav(out("banner_sound.wav"), [fitted, fitted], rate)
    log(report, "  banner_sound.wav: 16-bit stereo %d Hz, %.3f s, %d bytes of samples "
        "(limit: stereo, %.1f s)" % (rate, len(fitted) / rate, len(fitted) * 4, SOUND_LIMIT_SEC))

    # Model
    clump, anim, textures = load_car(args.game_files, report, args.anim)
    place_flat = placement(clump, anim, aspect=2.0, yfov=0.50, fill=0.92)
    seams = not args.no_seam_fix
    motion = LoopMotion(clump, anim) if seams and not args.skinned else None
    parts = None if args.skinned else rigid_parts(clump, anim, seams=seams, motion=motion)
    loop = loop_points(clump, anim, parts)
    rot, _ = view_rot(math.radians(CAR_YAW_DEG), math.radians(CAR_PITCH_DEG))
    if args.fit:
        scale, offset = fit_placement(loop, args.top_margin, args.bottom_margin)
        how = "--fit (top %g, bottom %g of the visible height clear)" % (
            args.top_margin, args.bottom_margin)
    else:
        scale, offset = args.scale, args.offset
        how = "--scale/--offset"
    if motion is not None:
        # The skirts are sized in pixels at this placement; with --fit, the placement is then
        # fitted again to the parts with their skirts (a skirt can reach a little past the
        # outline), which changes the scale by well under 1 %.
        parts = rigid_parts(clump, anim, place=(scale, rot, offset), motion=motion, report=report)
        loop = loop_points(clump, anim, parts)
        if args.fit:
            scale, offset = fit_placement(loop, args.top_margin, args.bottom_margin)
    place = (scale, rot, offset)
    log(report, "placement (%s): scale %.4f, offset %.4f, %.4f, %.4f" % ((how, scale) + tuple(offset)))
    if not fit_check(loop, scale, offset, report, args.top_margin if args.fit else FIT_TOP):
        log(report, "  the model leaves the HOME Menu's visible area: try --fit")
    model = clump  # with --skinned, the skin with the sheets' back faces
    if args.two_sided_sheets == "on":
        log(report, "two-sided sheets (the CGFX culls back faces, the game none):")
        sheet = sheet_triangles(clump, report)
        drawn, added = two_sided(parts, clump, sheet)
        if parts is None:
            model = drawn
        else:
            parts = drawn
        log(report, "  %d back faces for %d sheet triangles%s" % (
            added, len(sheet), " and their skirt pieces" if parts is not None and seams else ""))
    label = os.path.splitext(args.anim)[0].lower()
    label = label[len("patty_wagon_"):] if label.startswith("patty_wagon_") else label
    build_gltf(model, anim, textures, place, args.out_dir, parts, report,
               anim_label="boost" if label == "fast" else label)

    if not args.no_preview:
        # The 3D banner's look on the top screen (400x240): the model that goes into the CGFX
        # (its rigid parts, or the skin with --skinned) at four of its 60 Hz frames, back faces
        # culled as the CGFX culls them, with the HOME Menu's bars shaded.
        frames = len(loop)
        shown = (0, frames // 3, frames // 2, frames * 2 // 3)
        for i, frame in enumerate(shown):
            mesh = posed_mesh(model, anim, anim.duration * frame / frames, parts)
            img = render(model, mesh, textures, place, 400, 240, 5 / 3, home_bg, cull="back")
            for y in range(240):
                if VISIBLE_ROWS[0] <= y + 0.5 <= VISIBLE_ROWS[1]:
                    continue
                for x in range(400):
                    r, g, b, _ = img[y * 400 + x]
                    img[y * 400 + x] = (r // 3, g // 3, b // 3, 255)
            with open(out("banner_preview_%d.png" % i), "wb") as f:
                f.write(png_bytes(img, 400, 240))
        log(report, "preview: banner_preview_0..%d.png (400x240, the banner camera, frames %s "
            "of %d of the %s; the HOME Menu's bars darkened)" % (
                len(shown) - 1, ", ".join(map(str, shown)), frames,
                "skin" if parts is None else "rigid parts"))
    # The flat fallback: 256x128 (2:1), the camera's view a little wider; a still picture, so
    # it shows the game's own smooth skin.
    img = render(clump, posed_mesh(clump, anim, anim.duration * 0.33), textures, place_flat,
                 256, 128, 2.0, sea_bg(256, 128), yfov=0.50)
    with open(out("banner_flat.png"), "wb") as f:
        f.write(png_bytes(img, 256, 128))
    log(report, "flat: banner_flat.png (256x128)")

    if args.pycgfx:
        # pycgfx with its writer changed to put the textures and lookup tables on 128-byte
        # boundaries (CGFX_DATA_ALIGN); pycgfx's own files are used as they are.
        r = subprocess.run([args.pycgfx_python, "-E", "-s", PYCGFX_ALIGNED, args.pycgfx,
                            out("banner_model.gltf"), out("banner.cgfx")],
                           cwd=args.pycgfx, capture_output=True, text=True)
        sys.stdout.write(r.stdout)
        sys.stderr.write(r.stderr)
        if r.returncode != 0:
            raise SystemExit("pycgfx failed")
        size = os.path.getsize(out("banner.cgfx"))
        log(report, "CGFX: banner.cgfx %d bytes (limit %d)%s" % (
            size, CGFX_LIMIT, "" if size <= CGFX_LIMIT else "  ** TOO BIG **"))
        if size > CGFX_LIMIT:
            raise SystemExit("the CGFX is over the HOME Menu's limit")
        with open(out("banner.cgfx"), "rb") as f:
            cgfx = f.read()
        try:
            placed = cgfx_data_positions(cgfx)
        except (ValueError, TypeError, IndexError, struct.error) as e:
            raise SystemExit("banner.cgfx: cannot read where its textures and lookup tables "
                             "are (%s); not making the .bnr" % e)
        if not any(kind == "texture" for kind, _, _, _ in placed):
            raise SystemExit("banner.cgfx: no textures found in it; not making the .bnr")
        starts = []
        bad = []
        for kind, name, pos, _ in placed:
            past = pos % CGFX_DATA_ALIGN
            starts.append("%s %s at 0x%x%s" % (kind, name, pos,
                                                " ** %d past **" % past if past else ""))
            if past:
                bad.append("the %s %s (at 0x%x, %d past)" % (kind, name, pos, past))
        log(report, "  where its data starts (each a multiple of %d needed): %s" % (
            CGFX_DATA_ALIGN, ", ".join(starts)))
        if bad:
            raise SystemExit(
                "banner.cgfx: %s not on a %d-byte boundary in the file; the HOME Menu has frozen "
                "on such a banner, so no .bnr is made. pycgfx_aligned.py should have put them "
                "there: check that --pycgfx is pycgfx at commit 6c2e0cd."
                % (", ".join(bad), CGFX_DATA_ALIGN))
    if args.bannertool:
        if args.pycgfx:
            subprocess.run([args.bannertool, "makebanner", "-ci", out("banner.cgfx"),
                            "-a", out("banner_sound.wav"), "-o", out("banner.bnr")], check=True)
            log(report, "banner.bnr: %d bytes" % os.path.getsize(out("banner.bnr")))
        subprocess.run([args.bannertool, "makebanner", "-i", out("banner_flat.png"),
                        "-a", out("banner_sound.wav"), "-o", out("banner_flat.bnr")], check=True)
        log(report, "banner_flat.bnr: %d bytes" % os.path.getsize(out("banner_flat.bnr")))

    with open(out("banner_report.txt"), "w") as f:
        f.write("\n".join(report) + "\n")


if __name__ == "__main__":
    main()
