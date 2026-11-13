#!/usr/bin/env python3
"""Write the name texts of an SMDH (the 3DS icon file) in place, in all 16 languages.

bannertool's makesmdh cuts the short name at 32 characters, although the SMDH has room
for 64 (short), 128 (long) and 64 (publisher) UTF-16 characters. The .cia step runs
this after makesmdh so the HOME Menu shows the whole name; everything else in the file
(icon, flags, region, ratings) is left as bannertool wrote it.

    smdh_names.py <file.icn> --short TEXT [--long TEXT] [--publisher TEXT]
"""
import argparse
import sys

TITLES = 0x8          # 16 application titles follow the 8-byte header
TITLE_SIZE = 0x200    # each: short 0x80, long 0x100, publisher 0x80 bytes
FIELDS = {"short": (0x000, 0x40), "long": (0x080, 0x80), "publisher": (0x180, 0x40)}


def put(b, base, field, text):
    off, chars = FIELDS[field]
    data = text.encode("utf-16le")
    if len(data) // 2 > chars:
        sys.exit("smdh_names: the %s name is longer than %d characters: %r" % (field, chars, text))
    b[base + off:base + off + chars * 2] = data + bytes(chars * 2 - len(data))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("smdh")
    ap.add_argument("--short", required=True)
    ap.add_argument("--long")
    ap.add_argument("--publisher")
    a = ap.parse_args()
    b = bytearray(open(a.smdh, "rb").read())
    if b[:4] != b"SMDH" or len(b) < TITLES + 16 * TITLE_SIZE:
        sys.exit("smdh_names: %s is not an SMDH file" % a.smdh)
    for i in range(16):
        base = TITLES + i * TITLE_SIZE
        put(b, base, "short", a.short)
        if a.long is not None:
            put(b, base, "long", a.long)
        if a.publisher is not None:
            put(b, base, "publisher", a.publisher)
    open(a.smdh, "wb").write(bytes(b))


if __name__ == "__main__":
    main()
