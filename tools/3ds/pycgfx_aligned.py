#!/usr/bin/env python3
"""Runs pycgfx with the CGFX's texture and lookup-table data on 128-byte boundaries.

    python -E -s pycgfx_aligned.py PYCGFX_DIR in.gltf [out.cgfx]

with pycgfx's own Python (3.10 or later, with gltflib and Pillow); banner_from_game_files.py
runs it so, from PYCGFX_DIR. PYCGFX_DIR is pycgfx's folder (github.com/skyfloogle/pycgfx, MIT;
tested at commit 6c2e0cd), which is used as it is: this script changes nothing in it.

Why: pycgfx puts each blob of the CGFX's IMAG block (vertices, indices, textures, lookup
tables) on a 16-byte boundary only. The HOME Menu froze, every time the title was selected,
on a banner whose two textures and lookup table started 32 bytes past a 128-byte boundary;
the same CGFX with 96 zero bytes before them, so that all three start on a multiple of 128,
shows and moves properly (both on a console, 2026-11-16). bannertool's own banners have their
texture on 128 too.

What changes: only pycgfx's main.write(), replaced here by a copy that puts the start of the
IMAG data, and each texture's pixels and each lookup table's commands, on 128 in the file,
with zero bytes before them. The rest is laid out as pycgfx lays it out, so a CGFX whose
textures and lookup tables were already on 128 comes out byte for byte the same.
"""
import os
import sys

ALIGN = 128


def aligned_writer():
    """pycgfx's main.write() (as of 6c2e0cd) with the IMAG data on ALIGN."""
    from cgfx.luts import generate_lut_commands
    from cgfx.shared import StringTable

    class AlignedIMAG(StringTable):
        """pycgfx's IMAG table, with zero bytes before each blob in `aligned` so that it starts
        on ALIGN from the table's start (write() puts that start on ALIGN in the file)."""

        def __init__(self, aligned):
            super().__init__()
            self.aligned = {self.correct(b) for b in aligned}
            self.chunks = []

        def add(self, s):
            s = self.correct(s)
            if s in self.table:
                return
            if s in self.aligned and self.total % ALIGN:
                pad = -self.total % ALIGN
                self.chunks.append(b"\0" * pad)
                self.total += pad
            self.table[s] = self.total
            self.chunks.append(s)
            self.total += len(s)

        def write(self):
            return b"".join(self.chunks) + b"\0" * self.padding

    def write(cgfx):
        aligned = []  # the textures' pixels and the lookup tables' commands, as IMAG holds them
        for name in cgfx.data.textures:
            image = getattr(cgfx.data.textures[name], "pixel_based_image", None)
            if image is not None:
                aligned.append(image.data)
        for name in cgfx.data.lookup_tables:
            tables = cgfx.data.lookup_tables[name].tables
            aligned += [generate_lut_commands(tables[t].lut) for t in tables]
        strings = StringTable()
        imag = AlignedIMAG(aligned)
        offset = cgfx.prepare(0, strings, imag)
        offset = strings.prepare(offset)
        if not imag.empty():
            # More zero bytes after the strings (the end of the DATA block), so that the IMAG
            # data after its 8-byte header starts on ALIGN. strings.prepare() left it on 16.
            pad = -(offset + 8) % ALIGN
            strings.padding += pad
            strings.total += pad
            offset += pad
        # From here as pycgfx's write().
        cgfx.data.section_size = offset - cgfx.data.offset
        if not imag.empty():
            cgfx.header.nr_blocks = 2
            offset += 8  # IMAG header
        offset = imag.prepare(offset)
        cgfx.header.file_size = offset
        data = cgfx.write(strings, imag)
        data += strings.write()
        if not imag.empty():
            data += b"IMAG" + imag.size().to_bytes(4, "little") + imag.write()
        if len(data) > 0x80000:
            print(f"WARNING: CGFX is too big ({len(data)} bytes, max is {0x80000} bytes)")
        return data

    return write


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__.split("\n\n")[1])
    pycgfx_dir = os.path.abspath(sys.argv.pop(1))
    if not os.path.isfile(os.path.join(pycgfx_dir, "main.py")):
        sys.exit("pycgfx_aligned: no main.py in %s (pycgfx's folder)" % pycgfx_dir)
    sys.path.insert(0, pycgfx_dir)
    sys.dont_write_bytecode = True  # no __pycache__ for main.py in pycgfx's folder
    import main as pycgfx  # pycgfx's main.py

    pycgfx.write = aligned_writer()  # main() looks write() up in its module
    sys.argv[0] = os.path.join(pycgfx_dir, "main.py")
    pycgfx.main()  # pycgfx's own arguments: in.gltf [out.cgfx]


if __name__ == "__main__":
    main()
