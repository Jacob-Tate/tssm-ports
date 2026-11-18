#!/usr/bin/env python3
"""Writes a title version into a .cia's TMD and ticket.

    cia_version.py <file.cia> <major.minor.micro>

makerom v0.19.0 loses the minor number of the version it is given (its -minor
and -ver store it outside the version), so CMakeLists.txt gives makerom the
major and the micro and then runs this: 0.9.1 becomes the TMD's and the
ticket's 0 << 10 | 9 << 4 | 1. AM compares that number when one build is
installed over another. No hash covers it, and a homebrew .cia's signatures
are not checked (Luma3DS), so nothing else changes.

The layout is 3dbrew's "CIA", "Ticket" and "Title metadata".
"""

import struct
import sys


def align64(n):
    return (n + 63) & ~63


# A signed block's signature type -> its signature and padding, with the
# type word: where the block's own data begins.
SIGNATURE_BYTES = {
    0x10000: 4 + 0x200 + 0x3C,
    0x10001: 4 + 0x100 + 0x3C,
    0x10002: 4 + 0x3C + 0x40,
    0x10003: 4 + 0x200 + 0x3C,
    0x10004: 4 + 0x100 + 0x3C,
    0x10005: 4 + 0x3C + 0x40,
}


def encode(text):
    parts = text.split(".")
    if not 1 <= len(parts) <= 3 or not all(p.isdigit() for p in parts):
        raise SystemExit(f"cia_version.py: {text!r} is not major.minor.micro")
    nums = [int(p) for p in parts] + [0] * (3 - len(parts))
    if nums[0] > 63 or nums[1] > 63 or nums[2] > 15:
        raise SystemExit(f"cia_version.py: {text} does not fit a title version (63.63.15)")
    return nums[0] << 10 | nums[1] << 4 | nums[2]


def places(data):
    """The offsets of the ticket's and the TMD's version fields."""
    header, ctype, fmt, certs, ticket, tmd = struct.unpack_from("<IHHIII", data, 0)
    if header != 0x2020 or ctype != 0 or fmt != 0:
        raise SystemExit("cia_version.py: not a CIA header")
    ticket_at = align64(header) + align64(certs)
    tmd_at = ticket_at + align64(ticket)
    out = []
    for at, field in ((ticket_at, 0xA6), (tmd_at, 0x9C)):
        sig = struct.unpack_from(">I", data, at)[0]
        if sig not in SIGNATURE_BYTES:
            raise SystemExit(f"cia_version.py: signature type {sig:#x} at {at:#x} is not one")
        out.append(at + SIGNATURE_BYTES[sig] + field)
    return out


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    path, text = sys.argv[1], sys.argv[2]
    version = encode(text)
    with open(path, "r+b") as f:
        head = f.read(0x10000)
        for at in places(head):
            f.seek(at)
            f.write(struct.pack(">H", version))
    with open(path, "rb") as f:
        head = f.read(0x10000)
    got = [struct.unpack_from(">H", head, at)[0] for at in places(head)]
    if got != [version, version]:
        raise SystemExit(f"cia_version.py: read back {got}, wanted {version}")
    print(f"{path}: title version {text} ({version}) in the ticket and the TMD")


if __name__ == "__main__":
    main()
