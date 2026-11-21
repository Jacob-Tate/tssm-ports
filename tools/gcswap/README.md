# gcswap

Byte-swap rules for GameCube assets, so the PC port can read a GameCube
package the way it reads an Xbox one. The runtime is
`src/SB/Core/pc/iGCSwap.cpp`; its table, `iGCSwapTable.inc`, is generated here.
The method is the BfBB PC port's (CC0); the types, structs and rules are TSSM's.

| file | what it does |
| --- | --- |
| `layouts.py` | struct layouts from the decomp's own headers: one translation unit compiled 32-bit with clang's `-fdump-record-layouts`, turned into each struct's scalar fields |
| `spec.py` | which structs each asset type and DYNA subtype is made of, and the hand-decided cases |
| `gen.py` | writes `src/SB/Core/pc/iGCSwapTable.inc` from the two |

The check is `tssm_gcswaptest` (`src/SB/Core/pc/tests/gcswaptest.cpp`, a CMake
target when `TSSM_BUILD_TESTS` is on).

## Changing a rule

```sh
python tools/gcswap/layouts.py xEntAsset        # the fields gen.py sees for a struct
python tools/gcswap/gen.py                      # regenerate the table (needs clang++)
build-*/tssm_gcswaptest <GameCube files> <Xbox files>
build-*/tssm_gcswaptest <GameCube files> <Xbox files> --type DYNA:ui:text --dump 2
```

`<GameCube files>` is an extracted disc's `files/` folder (Dolphin: right-click
the game, Properties, Filesystem, Extract Entire Disc). `<Xbox files>` is an
extracted Xbox disc, the folder the port's `[assets] path` names. Both are
your own copies; nothing here ships or writes game files.

## What the test checks

- Every package's platform, from its PLAT chunk (`iGCPackage.h`): `GC` on
  every GameCube package, `BX` on every Xbox one.
- Every asset of every GameCube package, swapped in a buffer that ends where an
  inaccessible page begins, with a pattern in front: a read or write past the
  end stops the test at the faulty access, and a write before the start is
  reported.
- The same asset cut short at about 30 lengths, and with `--fuzz N` N copies
  with bytes and words damaged (huge counts and offsets in either byte order):
  each must stay inside its bytes too. A malformed asset ends in
  `IGCSWAP_MALFORMED`, never in an access outside it.
- With an Xbox folder, each converted asset against the Xbox asset of the same
  id in the package of the same path. Per type: `exact`; `near` (only floats a
  few units in the last place apart, or 16-bit values off by one or two: the
  two packers rounded differently); `layout` (some word of the Xbox asset is a
  byte order of the GameCube word that the rule did not produce: a rule is
  wrong there); `content` (the two releases shipped different values, or
  different sizes). `--dump N` prints every differing word of the first N.
- Cutscenes (CSN) are cut differently on the two discs, so each is checked
  against its own disc's CTOC (the converted headers must match) and its
  animations for the Xbox's `SKB1` magic.

The test passes when every GameCube asset converts (or is read as it is), no
copy is touched outside its bytes, and no layout difference is found.

## State

Against the retail GameCube (USA, Rev 1) and Xbox (USA) discs: 138 GameCube
and 136 Xbox packages, all named by their PLAT chunk (the GameCube's
`plat.HIP` holds no assets). Of 58,971 GameCube assets, 49,258 convert and
9,713 are read as they are; 0 unknown, 0 malformed, 0 layout differences. Of the same-size pairs, the differences left are content:

- floats and animation keys rounded differently (`near`): most of SIMP's angles
  and scales, and ANIM's keys (0x7FFF against 0x7FFE);
- different values: LODT's distances (half the Xbox's on the GameCube), TEXT
  wording, PIPT order and count, timings in a few PLAT, SCRP, TRIG and ui:text
  assets, uninitialized pad (0xCD) in SHRP particle fragments, and the
  RenderWare library stamp in the JSPINFO block marks.

Cutscenes: 28 of 28 CSN headers equal their CTOC entries; 3,686 animations in
483 time chunks carry the Xbox magic.

## Rules decided by hand

- `EITHER`: fields the GameCube packer left little-endian, decided per asset
  by which byte order reads as a plausible value: `xEnvAsset.loldHeight`,
  `xParSysAsset.parFlags`, `zFragParticleAsset.flags`, and SURF's
  `damage_timer` and `damage_bounce`.
- `AS_IS`: the ui widgets' `zUIAsset.color` (four bytes, though the decomp
  declares a U32), and the one SPLN's xBaseAsset header.
- Unions whose members disagree (`UNIONS`): entity motion by `type`, particle
  emitter shapes by `emit_type`, particle generator attachment and volume, and
  JSPINFO's collision triangles (`v.rawIdx`, a u32 in version 5).
- Hand-written walkers in `iGCSwap.cpp`: ANIM (SKB1), the ATBL effects, CRDT
  (decrypted, swapped, encrypted again), CTOC, CSN (header, JDELTAMODELs, and
  every time chunk's records), DEST, DSCO, JAW (the table; the curves are
  little-endian already), JSPINFO (version 5; the per-vertex data after the
  strip vectors is bytes), MINF and PLYR.
- Read as they are: MODL, RWTX, TEXS and the JSP clumps (librw reads the
  GameCube's native formats), SNDI, SND, SNDS and CSSS (the sound layer reads
  the GameCube's table), RAW, and FLY, which the GameCube disc ships
  little-endian.

A type or DYNA subtype with no rule (none in retail; a mod could carry one)
is `IGCSWAP_UNKNOWN`. The DYNA subtypes the game registers but neither disc
uses (`Enemy:SB`, `hud:image`, `ui:text:user string`, the rumble box emitter,
the function generator, the lens flares) have no rule yet, because there is no
Xbox twin to check one against.
