# Saves: formats, import and export

How The SpongeBob SquarePants Movie keeps a save, on the GameCube, the Xbox and
in the PC port, and how the port moves saves between them. Everything below is read
out of the decompiled code (`src/SB/Core/x/xsavegame.cpp`, `xserializer.cpp`,
`src/SB/Game/zSaveLoad.cpp`, `src/SB/Core/gc/isavegame.cpp`) and the Dolphin
SDK's CARD sources in `src/dolphin/src/card`.

**The short version.** A save is the same bytes on both machines: the game's
own *payload*. The GameCube wraps it in a memory-card file (a banner, an icon,
a comment and a directory entry); the PC port stores the payload alone. So
importing a GameCube save strips the wrapper and exporting one puts it back,
and nothing in the game state is translated.

The Xbox and the PlayStation 2 keep the same payload in the opposite byte
order, which does need translating, field by field (section 6). The Xbox then
signs the file (section 7); the PlayStation 2 keeps it bare in a folder on the
memory card, beside an icon (section 8). A PlayStation 3 plays the PlayStation
2 game on a virtual memory card and moves the game's folder in and out of it
as a signed `.psv` file (section 9).

| | GameCube | PC port |
|---|---|---|
| where | memory card, Slot A or B | the save folder, folder A or B (and C..J with the PC menus) |
| files | `SpongeBobMovie01`..`03` | `SpongeBobMovie01`..`03` |
| file contents | 0x4000-byte head (title, comment, banner, icon, footer) + payload, padded to 8 KB blocks | payload |
| exported as | `.gci` (Dolphin), `.gcs`, or a whole card image (`.raw`/`.gcp`) | the same `.gci`; or every save in one `.zip` |

## 1. The payload

Built by `xsavegame.cpp`'s save clients and handed to `iSGSaveFile`. Every
integer is **big-endian** in a canonical payload (the console's order; see
section 3 for how the PC keeps it so).

### Header (16 bytes)

| offset | size | field | value |
|---|---|---|---|
| 0x00 | 4 | magic | `GDAT` |
| 0x04 | 4 | version | 1 (`xSG_ld_readhead` refuses anything else) |
| 0x08 | 4 | size | bytes in the payload, header included (`totamt`) |
| 0x0C | 4 | checksum | CRC of bytes 0x10..size (below) |

The CRC is `xUtil_crc_update` from 0xFFFFFFFF: polynomial 0x04C11DB7, most
significant bit first, no reflection, no final xor (CRC-32/MPEG-2; "123456789"
gives 0x0376E6E7). `xSG_ld_validate` compares it on every load.

### Client blocks

From 0x10 to `size`, back to back:

| offset | size | field |
|---|---|---|
| +0 | 4 | tag (four characters, a big-endian integer) |
| +4 | 4 | room: bytes the block occupies after this header (`maxamt`) |
| +8 | 4 | used: bytes of it the client wrote (`realamt`) |
| +12 | room | data, then 0xBF up to `room` |

`xSG_ld_findcltblk` walks them by `room` and hands each to the load client with
the same tag. A save made by `zSaveLoad_SaveGame` or `zSaveLoad_DoAutoSave`
has, in this order:

| tag | room | contents |
|---|---|---|
| `LEDR` | 0x100 | the leader (below), then `--TakeMeToYourLeader--` (22 bytes); used = 0x6E |
| `ROOM` | 2 x `xSGWriteStrLen` of the last scene name (4 or 12) | the scene to resume in, 4 characters (`HB01`) |
| `PREF` | 40 | S32 sound mode, F32 music volume, F32 effects volume, S32 vibration, S32 subtitles; used = 20 |
| `SVID` | 4 | S32 1: the serializer's version (`xSER_xsgclt_ldproc_ver`; anything else and the game state is skipped) |
| serializer clients | their buffer size | the game state as a bit stream (below), one block per client, in tag order |
| `SFIL` | what is left of the serializer's buffer + 8 | `RyanNeil`, then fill |

The serializer clients are `PLYR` (0x148 bytes: zMain's size table), `CNTR`
(400) and one block of 400 bytes for each scene visited, tagged with the scene
id (`BB01`, `HB01`, ...). The serializer's buffer is 0x148 + 127 x 400 = 51128
bytes in all and `SFIL` pads to it, so a payload is always
**51524 + 12 x (serializer clients) bytes**: about 51.5 KB, 7 card blocks
(`zMenu.cpp` checks for 0xCC00 bytes of room at boot).

### The leader

The first 0x74 bytes of a payload are the header, the `LEDR` block header and
this struct (`st_XSAVEGAME_LEADER`, 0x58 bytes on the console);
`xSG_grab_leaders` reads just them to fill the save screen.

| offset | size | field |
|---|---|---|
| 0x00 | 64 | label: the level's name as the game showed it (`zSceneGetLevelName`, localised) |
| 0x40 | 4 | progress: percent, `InvGoofyGoober * 100 / 68` capped at 100 |
| 0x44 | 4 | (padding) |
| 0x48 | 8 | play time: an `iTime`, always 0 in TSSM |
| 0x50 | 1 | thumbnail index: the world, counted from 0 (`zSceneGetLevelIndex`) |
| 0x51 | 7 | (padding) |

### The bit stream

`xSerial` packs each client's state into an array of S32 words: stream bit *k*
is bit *k* mod 32 of word *k* / 32. `Write_b1`/`Write_bn` add the low bits of
an integer, least significant first. `Write(U8/S16/S32/U32/F32)` adds the
value's bytes in **memory order**, bits 0..7 of each, so on the console a
multi-byte value goes in most significant byte first. The words are then saved
as they lie in memory: big-endian.

What each client writes depends on the objects in its scene (`zSceneSave`:
counters, timers, teleport boxes, race timers, pickups, buttons...), so the
stream cannot be read without the scene's object list. This is why the PC port
keeps the stream itself in the console's layout (section 3) instead of
converting saves after the fact.

## 2. The GameCube's wrapper

`gc/isavegame.cpp`'s `iSGSaveFile` writes each save as one card file.

### The directory entry (a `.gci`'s first 64 bytes)

`CARDDir` (`src/dolphin/include/dolphin/CARDPriv.h`), as `CARDCreate`,
`CARDSetStatus` and `iSG_upd_icostat` leave it for this game:

| offset | size | field | TSSM |
|---|---|---|---|
| 0x00 | 4 | game code | `GGVE` (North America; `GGVP` Europe) |
| 0x04 | 2 | maker | `78` |
| 0x06 | 1 | padding | 0xFF |
| 0x07 | 1 | banner format | 0x02: RGB5A3, icon animation looping |
| 0x08 | 32 | file name | `SpongeBobMovie01`..`03`, zero-filled |
| 0x28 | 4 | time | seconds since 2000-01-01 on the console's (local) clock |
| 0x2C | 4 | icon address | 0x40: the banner, then the icon |
| 0x30 | 2 | icon formats | 0x0002: frame 0 RGB5A3, frame 1 none |
| 0x32 | 2 | icon speeds | 0x0002: frame 0 middle, frame 1 the end |
| 0x34 | 1 | permission | 0x04 (public) |
| 0x35 | 1 | copy count | 0 |
| 0x36 | 2 | first block | wherever the card put it |
| 0x38 | 2 | length | blocks: 2 + payload rounded up to 8 KB (9 for a typical save) |
| 0x3A | 2 | padding | 0xFFFF |
| 0x3C | 4 | comment address | 0 |

### The file

| offset | size | contents |
|---|---|---|
| 0x0000 | 32 | `The SpongeBob Movie` (the comment's first line) |
| 0x0020 | 32 | the save's date, `MM/DD/YYYY HH:MM:SS` (`DD.MM.YYYY` on PAL) |
| 0x0040 | 0x1800 | banner, 96x32 RGB5A3 (from the disc's `SBGCBanner.tpl`) |
| 0x1840 | 0x800 | icon, 32x32 RGB5A3 (`SBGCIcon.tpl`) |
| 0x2040 | 0x200 | zeros |
| 0x2240 | 0x200 | `SPONGEBOB2:PATRICK'SREVENGE::RyanNeilDan`, zero-filled |
| 0x2440 | | zeros to 0x4000 |
| 0x4000 | size | the payload |
| | | zeros to the end of the block |

Before loading, `iSG_curKosher` checks the title at 0 and the footer at 0x2240.
`iSGFileSize` reports the file's length less 0x4000 and `iSGLoadFile` reads from
0x4000; the payload's own size field says where it ends.

### Files and cards

- **`.gci`** (Dolphin's export, and its GCI-folder memory cards): the directory
  entry, then the file's blocks.
- **`.gcs`** (GameShark): a 0x110-byte header starting `GCSAVE`, then the same.
- **Raw card image** (`.raw` from Dolphin, `.gcp` from GCMM): the card's bytes.
  Block 0 is the card's ID (size in Mbit at 0x22), blocks 1 and 2 two copies of
  the directory (127 entries; check code at 0x1FFA, checksum and its inverse at
  0x1FFC/0x1FFE), blocks 3 and 4 two copies of the block map (checksums at 0 and
  2, check code at 4, then the next block of each block, 0xFFFF for the last).
  The SDK uses the copy with the newer check code of those whose checksum
  holds (`__CARDCheckSum`: 16-bit sums of the big-endian words and of their
  complements).

## 3. The PC port

`src/SB/Core/pc/isavegame.cpp` writes each save as its payload, in the
console's byte order, with the console's file name: the save file on PC **is**
a GameCube payload.

The game builds the payload with `memcpy`, so on a little-endian host it comes
out in the host's order. Two things put it in the console's:

1. **The bit stream** (shared code, `src/SB/Core/x/xserializer.cpp`,
   `XSER_ELEMENT_BYTE`, PLATFORM_PC only): `xSerial` takes a multi-byte value's
   bytes most significant first on a little-endian host, as the console does,
   so the stream carries the same bits on both. This is the only shared change;
   the GameCube build compiles the original lines.
2. **The rest, at the platform edge** (`iSavePayloadConvert` in
   `iSaveFormat.cpp`): `iSGSaveFile` converts the game's buffer to the
   console's order before writing it, and `iSGLoadFile`/`iSGReadLeader` convert
   what they read back to the host's. The header and block headers, the
   leader's fields, PREF and SVID are swapped as the integers they are, the
   serializer blocks as the S32 words they are stored as, ROOM and SFIL left as
   characters. The leader is moved between the console's layout and the host's
   (offsets from the host's own `st_XSAVEGAME_LEADER`, so a host that aligns an
   S64 differently still reads it). The CRC is recomputed for the converted
   bytes when the original's held, and made to fail when it did not, so the
   game's own check (`xSG_ld_validate`) accepts and refuses the same saves on
   both machines.

`iSGIsGameCorrupt` (the save list's "damaged save") checks a file's header: the
magic, version 1, and a size that is the file's. Like the console's check it
leaves the CRC to the load.

### The save folder

| path | what |
|---|---|
| `<root>/SpongeBobMovie01`..`03` | folder A: the console's Memory Card Slot A |
| `<root>/second/...` | folder B: Slot B |
| `<root>/more/2`..`9/...` | folders C..J, reached by the PC save screen only |
| `<folder>/SpongeBobMovie01.tga` | the still shown beside a save (PC menus on) |
| `<folder>/SpongeBobMovie01.gchead` | the directory entry and 0x4000-byte head a save was imported with, plus the size and CRC of the payload they belong to |
| `<folder>/backup/SpongeBobMovie01.1`..`.3` | the three versions before the current one, `.1` the newest |

`<root>` is `--save-dir` if given; else `TSSM_SAVE_DIR` if set; else config.ini's
`[game] save_folder`; else the per-user data folder's `tssm/saves`
(`%APPDATA%\tssm\saves` on Windows, `~/.local/share/tssm/saves` on Linux), or
`tssm/mods/<name>/saves` while a mod is in use.

Every write goes to a temporary file that is renamed over the old one, so a
crash leaves the old save or the new one, never half of one. The temporary is
flushed to the disk (`FlushFileBuffers`, `fsync`) before the rename: without
that, a power cut or a system crash just after a save can bring the new name
back with nothing behind it. On Windows a virus scanner, the search indexer or
a sync client that has the file open for a moment makes the rename fail with a
sharing error; the rename is tried again every 50 ms for up to two seconds
(`iHostRenameReplace`), so a scan no longer costs the save. Before a save is
replaced -- by the game, an import or a restore -- the old one is copied to
`backup/<name>.1` (and `.1` to `.2`, `.2` to `.3`).

When the game starts it checks every slot (`iSaveStoreRecover`, from
`iSGStartup`) and writes what each folder holds to the log:

```
tssm: saves: folder A (Memory Card Slot 1): A1 0% MNUS, A2 35% TR02 (from a card), A3 1% BB02 (from a card)
tssm: saves: folder B (Memory Card Slot 2): no saves
```

A save file the game could not use -- empty, cut short, zeroed, or failing its
checksum: what a crash in the middle of a save can leave -- gets the newest
good version back: the save that was being written, if it is whole (`<name>.tmp`),
else the newest good backup. The damaged file is kept as
`backup/<name>.damaged` (then `.damaged2`...), outside the three backups. The
test is the game's own (the header as `iSGIsGameCorrupt` reads it, the
checksum as `xSG_ld_validate` does), so a save the game can load is never
damaged by this; and a slot with no save file stays empty even when it has
backups (a deleted save is the player's to restore), unless a whole first save
into it was left as the temporary. Beside a good save, a whole temporary no
older than it is the newer save -- the game stopped between writing it and
renaming it into place -- and goes in, with the save it replaces as backup 1;
a cut-off temporary is reported and left, and the save is untouched. The
GameCube's boot check for corrupt card files (`iSGCheckForCorruptFiles`)
reports nothing on the PC: its screen came up black and waited for a button,
and its delete went to CARD stubs.

Loading from the title screen goes straight to the saves when only one of
folders A and B holds any, as the Xbox release (one hard disk) does; the
"Memory Card Slot 1 / 2" choice appears only when both do (or neither does).
With the choice, picking the empty folder said "NO GAME SAVES PRESENT ON THE
XBOX HARD DISK" while every save was in the other. Back from the saves returns
to the main menu. Saving still asks which folder. When the load screen does
find a folder empty, its message (`LD NOGAMES TXT`, rewritten in place by
`iSaveText.cpp`) names the slot instead of the Xbox's hard disk -- "No game
saves on Memory Card Slot 2" -- and the other slot when that one holds saves.

**One copy of the game per save folder.** A second `tssm.exe` started on the
same save folder brings the first one's window to the front and exits (or,
when it cannot, says the game is already running). Two copies cannot share a
folder: SDL hands controller input to a window that is not in front, so the
copy behind played along with every press on whatever screen it was showing,
and both saved to the same files. The claim is the process's for as long as it
runs and ends with it however it ends, so a crashed game never blocks the next
start; a hung one does, and the message says to end it in Task Manager. Only
the copy holding the folder repairs or writes its saves. A run with a save
folder of its own (`TSSM_SAVE_DIR`, as `tools/regress.py` gives each run) is
not affected by the game being played; and while another copy of the game is
the window in front, a copy behind it leaves the controllers alone
(`iPadHostSDL.cpp`).

The `.gchead` beside an imported save is what lets it go back to a card exactly
as it came: while the payload is still the one it came with, exporting it gives
the original `.gci` byte for byte. Once the game has saved over it, an export
keeps that card's region, banner and icon and takes a new date.

## 4. Import and export

### Command line

```
tssm --saves [--all]
tssm --import-save FILE [--slot S] [--pick N] [--force] [--dry-run]
tssm --export-save S FILE.gci | FOLDER          [--region E|P]
tssm --export-save all FILE.zip | FOLDER        [--region E|P]
tssm --import-save FOLDER ...                    (an Xbox save: section 7)
tssm --export-save S | all FOLDER --xbox emu|real ...
tssm --import-save FILE|FOLDER ...               (a PlayStation 2 save: section 8)
tssm --export-save S | all DEST --ps2 auto|psu|psv|folder|card ...
tssm --export-save S | all DEST --psv standin|real ...   (a PlayStation 3 .psv: section 9)
tssm --check-save FILE
tssm --restore-save S [N]
tssm --delete-save S
tssm --save-dir DIR ...
tssm --saves-help
```

A slot `S` is a folder letter and a game number: `A1`..`A3`, `B1`..`B3`
(`1`..`3` alone is folder A). The command runs from `iSystemInit` straight
after config.ini is read and the process exits: no window, no assets, no game.
Exit status 0 is success, 1 failure, 2 a command line not understood.
`--import-save` (except with `--dry-run`), `--restore-save` and `--delete-save`
change the saves and take the save folder's claim first, as the game does: with
the game running on the same folder they refuse ("the game is running (process
N) with the saves in ...; close it and run --delete-save again", exit 1) rather
than change slots behind it. The others only read and run beside the game.

- **Import** takes, told apart by their contents: a `.gci`, a `.gcs`, a raw
  card image (every SpongeBob Movie file on it; other games' files are
  skipped), a zip from `--export-save all`, a bare payload (a save file
  from another install's save folder), an Xbox save, which is a folder
  (section 7), or a PlayStation 2 save in any of its forms (section 8). Each
  save is checked -- the card head's
  strings, the payload's magic, version, block structure, CRC and serializer
  version -- and goes to the slot its file name gives (`SpongeBobMovie02` to
  `A2`), or the one `--slot` names. `--slot B` keeps the numbers but uses
  folder B. A slot that already holds a save is left alone unless `--force`
  is given, and then its save becomes backup 1. `--check-save` (or
  `--dry-run`) says what would happen and writes nothing.
- **Export** to a `.gci` writes what the console would have written; to a
  folder, the same named as Dolphin names it (`78-GGVE-SpongeBobMovie01.gci`).
  `all` to a folder writes folder A's saves into it and folder B's into `B/`
  beside them (one card cannot hold two `SpongeBobMovie01`s). `all` to a
  `.zip` writes every save as `A/78-GGVE-SpongeBobMovie01.gci`, ... -- the file
  to move to another PC or Android install, where `--import-save` puts every
  save back in its own slot. `--region P` writes `GGVP` (and the PAL date).

In Dolphin, a `.gci` goes in through Tools > Memory Card Manager (Import GCI),
or straight into a GCI-folder card (`GC/USA/Card A` in Dolphin's user folder).
Saves come out the same way (Export GCI). The tools here only read the
GameCube card images and `.gci` files they are given. The one card file ever
written to is a PlayStation 2 one an export is pointed at, and that is copied
first (section 8).

### The C++ interface

`src/SB/Core/pc/iSaveStore.h`, which the in-game Saves tab
(`gui/iGuiSaves.cpp`, `docs/GUI.md`) and the command line both call:
`iSaveStoreList` (every slot with its label, progress, level, scene, date,
size, still, whether it came off a card, its backups), `iSaveStoreImport` and
`iSaveStoreImportData` (a file, or bytes from a file picker; a look-only pass
reports each save's level and progress as well as where it would go), the
`iSaveStoreExport*` family (a slot as a `.gci` in memory or on disk; every save
as a zip; every save into a Dolphin GCI folder, `iSaveStoreExportFolder`),
`iSaveStoreBackups`, `iSaveStoreRestore`, `iSaveStoreDelete`, and
`iSaveStoreAutoSaveSlot`, the slot the game in progress autosaves to.
`iSaveFormat.h` underneath is the formats alone, with no file system.

A save imported into the slot the game is autosaving to is overwritten at the
next autosave (the imported one stays as backup 1). The Saves tab marks that
slot and asks before importing or restoring over it.

## 5. Testing

`save_selftest` (`src/SB/Core/pc/tests/save_selftest.cpp`; `ctest` in the
build folder, or run it with a scratch folder) compiles the game's own
`xsavegame.cpp`, `xserializer.cpp`, `xutil.cpp` and `xordarray.cpp` with the
port's `isavegame.cpp` and save store, and saves and loads through the game's
code. It checks that

- the bit stream of a client written with `Write_b1`, `Write(U32)`,
  `Write(S16)`, `Write(F32)`, `Write(U8)`, `Write(S32)` and `Write_bn` is laid
  out exactly as a reference following the console's rule lays it out, and
  every header, PREF word and leader field is big-endian;
- the game loads its save back with every value intact, reads the leaders the
  way the save screen does, refuses a save with one flipped bit and lists a
  cut-short one as damaged;
- a `.gci` export has the directory entry and head described above; export,
  import, export is byte for byte the same (twice round); a `.gci` from a card
  (another region, other art, another date) comes back exactly as it went in;
  another game's `.gci` and one whose save fails its CRC are refused;
- a raw card image holding this game's file and another's imports this game's
  only, and reads with one damaged directory copy;
- a zip of every save restores every slot in an empty save folder;
- backups rotate, restore and undo a delete;
- the command line does all of the above;
- what a crash during a save can leave is put right at the next start: an
  empty, zeroed or cut-short save, or one failing its checksum, comes back from
  the whole temporary or the newest good backup and is kept as `.damaged`; a
  good save beside a half-written temporary, a deleted save, and a damaged one
  with nothing to put back are left as they are; a whole temporary newer than
  the good save beside it goes in, the save it replaces becoming backup 1 (an
  older one is left); the load screen's choice of folder
  (`iSGOnlyTargetWithGames`) is skipped exactly when one folder holds saves,
  and its "no saves" hook is told which folder has them; and the save folder
  can be claimed by one process only.

In the game itself (the 32-bit build, which reaches the menus; scripted with
`TSSM_INPUT`): New Game, "create a game save", Memory Card Slot 1, first file
writes `SpongeBobMovie01` (51560 bytes: `LEDR`, `ROOM` `MNUS`, `PREF`, `SVID`,
`CNTR`, `MNUS`, `PLYR`, `SFIL`), which `--check-save` passes. Exported to a
`.gci` and imported into the second file, it is listed on the Load Saved Game
screen beside the original (`0% ... (9 BLOCKS)`), and loading it starts the
game (`tssm: load ...SpongeBobMovie02: ok`).

Not tested, for want of the files: a `.gci` written by a real console or
Dolphin (the format is taken from the decompiled code and the SDK, and the
test's reference is built from the same reading), and a real `.gcs`.

`xsave_selftest` (`src/SB/Core/pc/tests/xsave_selftest.cpp`, also in `ctest`)
covers sections 6 and 7 without the game. Everything it needs it makes itself:
a made-up save, made-up packages in both consoles' byte order, a made-up XBE
and a made-up `keys.bin` (bytes 1 to 32). It checks that

- SHA-1 gives FIPS 180-1's answers and HMAC-SHA1 all seven of RFC 2202's;
- the three save names give the folders `7B317197822D`, `...2E` and `...2F`,
  and the `.xbx` files are 48 and 88 bytes;
- the title key and the signature, emulator and real, equal HMAC written out
  from its definition and the values another program computes;
- a save written by a reference encoder following `xSerial`'s rule for the
  GameCube converts to exactly the bytes the same encoder writes for the Xbox,
  and back, with GameCube packages and with Xbox ones; a word swap does not;
  a missing package and a cut-short save are refused with the reason, and a
  block longer than its level saves is converted with a note and comes back
  exactly;
- an exported save has the folder, the two files, the title's three files and
  the signature described above, for both signatures and every kind of
  destination; a save already there is not replaced unasked; and it imports
  byte for byte from the save's folder, the game's, an emulator's data folder,
  a zip and a bare file, whatever its signature;
- what is missing (the XBE, the keys file, a keys file of the wrong size) is
  said plainly, and no message holds key bytes;
- the command line does the same.

Given the player's own folders (`TSSM_TEST_ASSETS`, `TSSM_TEST_GC_ASSETS`) it
also reads their `DEFAULT.XBE` (the name and both pictures' sizes) and scans
every package of both discs: only level packages and `MN/mnuc.HIP` hold saved
objects, and the two discs agree level for level. Given a folder of real saves
(`TSSM_TEST_SAVES`) it converts each there and back, byte for byte, with both
discs' packages. Done so with a save from a real Xbox and one from a real
PlayStation 2 card (kept outside the repository), both at 100%: every block of
both reads as the layout of section 6 says, the Xbox one imports, and exporting
it again gives its payload and its `.xbx` files back byte for byte. One block
of the Xbox save (`TT01`) holds 22 bits past the end of what the level saves;
they are kept as they are, and the import says so.

Exported saves have been loaded by the Xbox game itself, in Cxbx-Reloaded
(NTSC): with no `keys.bin` it lists and loads emulator-signature exports made
from a PlayStation 2 save, a GameCube save and a real Xbox's save, with the
right progress; given a made-up `keys.bin` it lists and loads the export
signed with that same file and calls the emulator-signature one damaged, as a
console with real keys would. Not tested: a real Xbox, xemu, a real
`keys.bin`, and the European game.

The same test covers section 8, again with nothing but what it makes itself:

- the folder's and files' names (with the trailing space, and as a host keeps
  them), the card's clock both ways, `icon.sys` field by field, the filler,
  and the port's own icon as a well-formed icon file; the icon found in a
  made-up package by its id, and not taken from one that lacks it or is cut;
- the card's error-correcting code against five known answers worked out from
  PCSX2's own tables, and that any flipped bit changes it;
- a `.psu` and a `.psv` written and read back; a cut one and a `.max` of noise
  refused;
- a memory card image formatted by the test, with and without spare bytes:
  another game's folder and then this game's written and read back; a file
  replaced by a longer and a shorter one with the free space coming out right;
  only the pages of the new files, their entries and the table changing; a
  write that does not fit refused with the card left bit for bit as it was; a
  flipped bit caught by its page's code; a damaged table refused;
- through the store: a slot exported as a `.psu` (the four files, the save's
  CRC running to the end of the file, its date), added to and imported back
  byte for byte; into a card file, with the copy made first and the other
  game's save intact; into PCSX2's folder card, with the entry files and the
  index it expects, an existing index kept; the game's icon taken from a
  made-up `PLAT.HIP` or from a save; and each of those imported;
- the command line does the same.

Given a PlayStation 2 disc's files (`TSSM_TEST_PS2_ASSETS`) it finds the
game's icon in `PLAT.HIP`; given real PlayStation 2 saves among
`TSSM_TEST_SAVES` (a `.max`, a `.psv`, a `.psu`, a card) it reads each, compares
the game's `icon.sys` with the port's byte for byte, checks the filler's shape
and the icon, converts each save there and back, and puts the folder through a
`.psu` and a card image. Done so with a real save from a PlayStation 2 card
(as an Action Replay Max file) and one a PlayStation 3 exported, both kept
outside the repository.

Exported saves have been loaded by the PlayStation 2 game itself, in PCSX2
(NTSC): out of a folder-type card with the game's icon and out of a new card
file with the port's own, saves made from an Xbox save, a GameCube save and
the port's own all list and load with the right progress, and the game saves
over one on a card the port made. Not tested: a console (a `.psu` through
uLaunchELF), and the European game.

Section 9 is in the same test, with made-up key files only (the texts
`MADE-UP PSV KEYn` and `MADE-UP PSV IV n`):

- AES-128 gives FIPS 197's answers (Appendices B and C.1) and SP 800-38A's
  first ECB and CBC blocks;
- a `.psv` written from a folder has the layout of section 9 field by field
  (the size at 0x40 equal to the same folder's `.psu`), reads back as the same
  folder, and written again from what was read is the same bytes;
- the stand-in signature and one made with a made-up key file are exactly
  what the public description gives, written out separately in the test; only
  the signature's 20 bytes differ between them; only the right key file checks
  one; another seed, or any changed byte, gives another;
- key files of 0, 1, 16, 31, 33 and 64 bytes, and one of zeros, are refused
  with the reason, and nothing is written;
- through the store and the command line: a slot exported as a `.psv` (into
  a file, or into a folder under the PS3's name, PAL too), added to, replaced,
  all three of a folder in one; the real signature refused without a key
  file; the import saying stand-in, real, not recognised or unknown as the
  key file at hand allows, and importing a `.psv` with a damaged signature
  byte for byte; a new `.VM2` card made, written to again (copied first) and
  imported; no message and no file written holding any of a key file's bytes,
  as bytes or as hex.

`psv_sign_check` (`src/SB/Core/pc/tests/psv_sign_check.py`, in `ctest` when
there is a Python) checks the same from outside: its own AES-128 (from FIPS
197, checked against its answers) and Python's HMAC, `.psv` files built from
the description with made-up contents (three icons named apart, too), signed
by `xsave_selftest --psv-sign` with two made-up key texts and three key files
of random bytes made at run time and with the stand-in: every byte but the
signature is the layout built there, the signature is the one worked out
there, and nothing printed or written holds a key.

Given a real `.psv` among `TSSM_TEST_SAVES` it is also written again from
what was read (the same file but for the signature), imported, and exported
both into a copy of itself (the same file but for the signature) and to a new
`.psv` (the differences are listed in section 9). Done so with one a
PlayStation 3 exported, kept outside the repository. Not tested: a PS3
reading anything the port wrote, a real key file (none was looked for or
used), and a `.VM2` from a PS3.

## 6. The PlayStation 2 and Xbox payload

The PlayStation 2 and Xbox releases build the same payload with the same game
code. Their processors are little-endian, so everything in it is the other way
round. `iSaveLE.h` converts between the two; the PlayStation 2's and the Xbox's
payloads are the same bytes.

| part | GameCube (canonical) | PlayStation 2, Xbox |
|---|---|---|
| header, block headers | big-endian words | little-endian words: the file starts `TADG`, the tags read `RDEL`, `MOOR`, `FERP`, `DIVS`, `RYLP`, `RTNC`, `LIFS`, and a level's id backwards (`10BB`) |
| leader | progress (S32 at 0x40) and play time (S64 at 0x48) big-endian | the same offsets, little-endian |
| `PREF`, `SVID` | big-endian words | little-endian words |
| `ROOM`, `SFIL`, the leader's label and `--TakeMeToYourLeader--` | bytes | the same bytes |
| serializer blocks | stored as big-endian words; a value wider than a byte goes into the stream most significant byte first | stored as little-endian words; the value goes in **least significant byte first** |
| CRC | over 0x10..`size` | the same CRC, over these bytes; the file must end at `size` |

Two things an Xbox writes differently are left as they are, in both
directions, so a save goes there and back byte for byte: the four bytes after
the leader's progress (file offset 0x60) are `52 00 00 00` in every Xbox save
and zero on the other consoles (nothing reads them), and a new game's sound
mode in `PREF` is 1 on the Xbox and 2 elsewhere. A save made here and exported
to an Xbox therefore differs from one the Xbox game made at the same point in
those two places and the CRC; the Xbox game loads both.

The last-but-one line is why swapping words is not a conversion. The stream is
not byte aligned (every block starts with one bit), and only the values written
with `Write(S16/S32/U32/F32)` change: their bytes swap places in the stream,
while single bits, bit fields and U8s stay where they are. One bit followed by
the S32 0x11223344 is `88 66 44 23` on the GameCube and `89 66 44 22` on the
Xbox. So each such value has to be found. Where they are:

| block | the stream, in order | values to turn |
|---|---|---|
| `PLYR` | 1 bit; U32 manliness points, tokens, chests in all, chests opened; S16 skill points spent; 20 x (U32 chests, U32 pickups); 10 x U8 power-ups | 44 U32, 1 S16 (1505 bits) |
| `CNTR` | 1 bit; for each task counter: 1 bit, U8 state, S16 count; S32 cheat flags | one S16 per counter, 1 S32 (114 counters on every disc: 2883 bits) |
| a level (`BB01`) | 1 bit; F32, F32; then every saved object of the level, by kind | below |

A level's objects are saved in the order `zSceneInit` lists them: by kind, in
the order of `zScene.cpp`'s table, then its dynamic types. Every object of a
kind writes the same thing, so the layout is the count of saved objects of each
kind:

| kind (asset type) | written | saved when |
|---|---|---|
| trigger `TRIG`, simple object `SIMP`, platform `PLAT`, pendulum `PEND`, hangable `HANG`, EGenerator `EGEN` | 2 bits | the asset's flags have 2 |
| move point `MVPT`, group `GRUP`, portal `PORT`, camera `CAM `, surface `SURF`, volume `VOLU`, conditional `COND`, env `ENV `, dispatcher `DPAT`, fog `FOG `, light `LITE`, script `SCRP` | 1 bit | flags have 2 |
| pickup `PKUP` | 11 bits | flags have 2 |
| button `BUTN` | 4 bits | flags have 2 |
| timer `TIMR` | 1 bit, U8, **F32** | flags have 2 |
| counter `CNTR` | 1 bit, U8, **S16** | flags have 2 |
| task box (`DYNA` game_object:task_box) | U8 | the asset's `persistent` byte (at 0x10) is set |
| flythrough (game_object:Flythrough) | 1 bit | always (`zCameraFly_Setup`) |
| camera tweak (game_object:Camera_Tweak) | 1 bit | flags have 2 |
| teleport box (game_object:Teleport) | 3 bits, **U32** | always (`zEntTeleportBox_Init`) |
| race timer (game_object:RaceTimer) | **F32** | always (`zRaceTimer::ResetTimer`) |

The objects are the level's own package's (`BB/bb01.HIP`, with its `.HOP` and
`_US.HIP`) and those of the packages that stay loaded beside it; of those only
`MN/mnuc.HIP` holds any (one counter, and the task counter group). The flags
are in the console's byte order, which the package's `PLAT` chunk gives. Both
discs' packages give the same counts for every level, so either set of files
converts a save from any console.

That is the one thing a conversion needs that a GameCube import does not: **the
game's own files for every level the save has visited**, from the asset folder
(`[assets] path`). A level entered but not left yet has an all-zero block and
needs nothing. If a package is missing the message names it. If a block holds
data past the end of what its level saves, the save was probably made with
another copy of that level (another release of the disc, or a mod's; the one
real case seen, in a save from a real Xbox, is not explained): it is converted
all the same -- the values this copy knows are turned, the rest is kept bit
for bit -- and the import says which level it was. A save that is refused
(a bad CRC) carries only its reason.

## 7. Xbox saves

The Xbox release saves to the hard disk only: three saves, no memory unit.

```
E:\UDATA\54510027\                   the game's folder: its title ID
    TitleMeta.xbx        88 bytes    "TitleName=The SpongeBob SquarePants Movie"
    TitleImage.xbx       67,584      the dashboard's pictures, copied out of the
    SaveImage.xbx        4,096       game's program (DEFAULT.XBE) when it starts
    7B317197822D\                    one save
        SaveMeta.xbx     48 bytes    "Name=SpongeBobMovie01"
        GameData.xsv     payload + 20
    7B317197822E\  7B317197822F\     SpongeBobMovie02 and 03
```

- The two `.xbx` text files are UTF-16LE with a byte-order mark and CR LF.
- A save's folder is named from its name: fold the name's characters as
  H = (H x 65536 + c) mod (2^48 - 59) and write H as 12 upper-case hex digits.
- `GameData.xsv` is the little-endian payload of section 6, then a 20-byte
  **signature**. The load screen's date is the file's own.

### The signature, and the two the port can write

The signature is HMAC-SHA1 over the payload. Its key is the title's signature
key, which a console works out when the game starts: HMAC-SHA1 of a 16-byte
field in the game's certificate (inside `DEFAULT.XBE`), keyed with the
console's *certificate key*, first 16 bytes. The game loads a save only if the
signature is the one it would make itself. The certificate key is in the
console, not in the game, and not in this port. So an export offers two
signatures:

| | what it needs | what loads it |
|---|---|---|
| **emulator** | `DEFAULT.XBE` from your own copy of the Xbox game | Cxbx-Reloaded (as it runs with no `keys.bin`: its certificate key is then all zeros), and other copies of this port |
| **real** | `DEFAULT.XBE`, and your `keys.bin` from your own Xbox: the 32-byte file Cxbx-Reloaded reads (the EEPROM key, then the certificate key) | a real Xbox, xemu, and Cxbx-Reloaded when it has the same `keys.bin` |

The port ships no key, never asks for one to be typed, and never shows one: it
reads the two files where you say they are and uses them in memory. It cannot
tell whether a `keys.bin` is the right one; a save signed with a wrong one is
simply not listed by the console.

**Importing needs neither.** The port reads an Xbox save whatever its
signature and checks the payload's own CRC, as it does for a GameCube save. It
says which signature a save carries when it can tell (emulator; real, matching
your `keys.bin`; not recognised; not checked, with no `DEFAULT.XBE` at hand).
The files it tells by are config.ini's, or the ones `--xbe` and `--keys` name
on an `--import-save` or `--check-save` line.

### Using it

```
tssm --import-save FOLDER [--slot S] [--pick N] [--force] [--dry-run] [--xbe FILE] [--keys FILE]
tssm --check-save  FOLDER [--xbe FILE] [--keys FILE]
tssm --export-save S   FOLDER --xbox emu|real [--xbe FILE] [--keys FILE] [--as N] [--replace]
tssm --export-save all FOLDER --xbox emu|real [--xbe FILE] [--keys FILE] [--slot B] [--replace]
```

- **Import.** `FOLDER` is a save's own folder (`7B317197822D`), the game's
  (`54510027`), a whole `UDATA`, or Cxbx-Reloaded's data folder; or give a
  `.zip` of any of them (stored or packed the usual way, as Windows and 7-Zip
  make one; not one with a password), or a `GameData.xsv` (the `SaveMeta.xbx`
  beside it says which save it is, and only that folder is read). Each save goes to the slot of its number, as a
  GameCube save does; `--check-save FOLDER` lists them, `--pick` chooses one.
  The save keeps its file's date.
- **Export.** `--xbox emu` or `--xbox real` picks the signature. `DEFAULT.XBE`
  is found beside the game's files when those are the Xbox ones; otherwise
  name it with `--xbe`, or once in config.ini (`[game] xbox_xbe`). `--keys`
  (or `[game] xbox_keys`) names your `keys.bin`. An Xbox holds three saves, as
  one save folder does: a slot becomes the save of its own number unless
  `--as N` says another, and `all` writes folder A's three (or `--slot B`'s),
  each under its own number (`--as` is one slot's).
  A save of that name already there is left alone unless `--replace` is given.
  The Saves tab checks the two files as soon as they are chosen: a file that
  is not this game's Xbox program, or not a `keys.bin`, is said in red under
  its row and the export stays off until it is put right.
- **Where it goes.** Under Cxbx-Reloaded's data folder (the one holding
  `EmuDisk`) the save goes where the emulator keeps the hard disk:
  `EmuDisk\Partition1\UDATA\54510027\`. A folder named `UDATA` gets `54510027`
  inside it, one named `54510027` is used as it is, and any other folder gets
  `UDATA\54510027\` with the saves inside: copy that `UDATA` to the `E:` drive
  of a real Xbox. The game's three title files are written when they are not
  there yet, made from your `DEFAULT.XBE` (the port holds none of the game's
  pictures).

The Saves tab has the same: **Import an Xbox save folder...**, and under
**Export to an Xbox** the choice "Cxbx-Reloaded and other copies of this port"
or "A real Xbox or xemu (needs your keys.bin from your own Xbox)", a Browse
button for each file, and each slot's **Export...** offering an Xbox save
beside the `.gci`.

In C++ (`iSaveStore.h`): `iSaveStoreImport` takes the folder;
`iSaveStoreReadFolder` gathers one into bytes for `iSaveStoreImportData`;
`iSaveStoreExportXbox` and `iSaveStoreExportXboxFolder` write, with
`iSaveXboxExportOptions` (signature, the two paths, the save number, replace);
`iSaveStoreXboxProgram` and `iSaveStoreXboxKeys` say where the two files would
be read from, or what is missing; `iSaveStoreToLE`/`iSaveStoreFromLE` are the
payload conversion alone. Underneath, with no file system: `iSaveLE.h` (field
maps, the package scan, the conversion), `iSaveXbox.h` (names, the `.xbx`
files, the XBE's certificate and pictures, the signature) and `iSaveSHA1.h`.

## 8. PlayStation 2 saves

On a PlayStation 2 memory card the game keeps one folder, for up to three
saves:

```
BASLUS-20904SBMovie /                the name really ends in a space
    BASLUS-20904SBMovie    992 bytes filler, named like the folder
    icon.sys               964       the title and the browser's lighting
    SpongeIcon01.ico       83,544    the icon the console's browser shows
    SpongeBobMovie01..03   payload   the saves: section 6's little-endian
                                     payload and nothing else
```

- The PAL game's folder (and filler) is `BESLES-52895SBMovie `.
- A save file is exactly the payload: no head, no signature. Its CRC is
  checked by the game over the whole file, so the file must be exactly the
  header's size, which is what an export writes.
- `icon.sys` is Sony's standard file: `PS2D`, the title "SpongeBob - The
  Movie" in full-width Shift-JIS with its line break after 22 bytes, four
  background colours, three lights and the ambient light, and
  `SpongeIcon01.ico` named for the list, copy and delete icons. The port
  writes it byte for byte as the game does.
- The game writes the three files beside the saves only when one of them is
  missing; so does an export.
- Dates are not in the payload. The card's directory keeps each file's times
  on Japan's clock (UTC + 9), which is what the game's load screen shows.

**The icon and the filler are the game's own data, and the port carries
neither.** For the icon an export uses, in this order: the one already in the
destination; the game's own, from wherever you say it is (`--ps2-icon`, or
`[game] ps2_icon`): `PLAT.HIP` from your own PlayStation 2 disc (the asset
`SpongeIcon`; the Xbox and GameCube discs do not have it), or any save the
game made; `PLAT.HIP` in the game's files in use, if they are the PlayStation
2's; else an icon the port makes from its own art, the sponge block of the app
icon as a slab (`iSavePS2OwnIcon`). The game's filler holds twenty lines of its
programmers' text followed by `0xBF` bytes; the port's has the lines empty.
Nothing reads them: the game only looks for the file.

### The forms it travels in

| form | read | written | what it is |
|---|---|---|---|
| `.psu` | yes | yes | uLaunchELF's (EMS) file: the card's own 512-byte directory entries, for the folder, `.` and `..`, then for each file, whose bytes follow in whole kilobytes. A real console takes it with uLaunchELF, and tools like mymc put it on a card image |
| a memory card file | yes | yes | PCSX2's `.ps2` (8,650,752 bytes for 8 MB), a PlayStation 3's `.VM2` (8,388,608) or any raw dump, with or without the spare bytes |
| PCSX2's folder-type card | yes | yes | a host folder named like a card file |
| `.max` | yes | no | Action Replay Max: the files packed with LZARI |
| `.psv` | yes | yes | what a PlayStation 3 exports from a virtual card, signed (section 9) |
| the folder itself | yes | no | any copy of the game's folder holding `SpongeBobMovie01`..`03` |

`.cbs` (CodeBreaker) is not read: it is zlib-packed and scrambled with a key
of the tool's own, and the port has neither an inflater nor business holding
that key. Unpack such a save to `.psu` with a save tool first.

**A memory card image.** Pages of 512 bytes, two to a cluster, sixteen to an
erase block. A file "with ECC" has 16 spare bytes after each page: three
error-correcting code bytes for each 128-byte quarter (one of column parity,
two of line parity; `iSavePS2Ecc`), then four zeros. Page 0 is the superblock
(`Sony PS2 Memory Card Format `, the geometry, where the allocatable clusters
start and how many there are, the root directory's cluster, and the list of
clusters holding the table of the allocation table's clusters). The allocation
table has one 32-bit entry for each allocatable cluster: the top bit for "in
use", the rest the next cluster of the chain (`0x7FFFFFFF` the last).
Directories are chains of clusters with two 512-byte entries in each: mode,
length (bytes, or entries for a folder), created, first cluster, modified,
attributes, and the name at 0x40; a folder's first two are `.` and `..`.

Writing to one adds or replaces files in the game's folder (made if it is not
there): each file's clusters are taken from the free ones, its old ones given
back, the entries and the table updated, and every page written gets its code
bytes. Every other file and folder on the card is left as it is. All of it is
done to a copy in memory, read back and compared, and **before the card file
is replaced a copy of it as it was is written beside it** (`<name>.bak`, then
`.bak2`, ...; no new one when the newest already is the card as it is); if
that copy cannot be made, nothing is written. A card PCSX2
has just made is blank (all `0xFF`) until a game or the console's browser
formats it, and is refused with that reason; a new name ending `.ps2` makes a
new formatted card instead. Importing only reads.

**PCSX2's folder-type card** is a folder (named like a card file, for example
`Mcd001.ps2`) holding `_pcsx2_superblock` (the card's first erase block; PCSX2
shows nothing of a card whose block is not formatted, so an export formats an
empty one) and each save folder as a host folder. Names a host cannot keep are
changed the way PCSX2 changes them (a trailing space becomes `_`:
`BASLUS-20904SBMovie_`), and the real name is kept in a copy of the card's
directory entry: `_pcsx2_meta_directory` for the folder, `_pcsx2_meta/<file>`
for a file. `_pcsx2_index` is YAML listing the folder's (`$ROOT`) and each
file's times, and the files' order; the port adds its files to it and keeps
what is there (the times are the card's clock read as UTC). Close PCSX2 before
exporting into a card it has open.

### Using it

```
tssm --import-save FILE|FOLDER [--slot S] [--pick N] [--force] [--dry-run]
tssm --export-save S   DEST --ps2 auto|psu|folder|card [--ps2-icon PATH] [--pal] [--as N] [--replace]
tssm --export-save all DEST --ps2 auto|psu|folder|card [--ps2-icon PATH] [--pal] [--slot B] [--replace]
```

- **Import.** A `.psu`, `.max`, `.psv` or card file is told by its contents; a
  folder may be a PCSX2 folder card, the game's folder inside one, or any
  folder holding the save files. Each save goes to the slot of its number
  (`SpongeBobMovie02` to `A2`), with `--slot`, `--pick` and `--force` as for
  the other consoles, and keeps the date the card gave it. Like an Xbox save
  it needs the game's own files to convert (section 6).
- **Export.** `--ps2 psu` writes a `.psu` (into a folder: one named
  `BASLUS-20904SBMovie.psu`); `--ps2 card` writes into the card file `DEST`;
  `--ps2 folder` into the folder card `DEST`; `--ps2 auto` goes by what `DEST`
  is (an existing card file; a folder holding `_pcsx2_superblock`; a new name
  ending `.ps2`, which becomes a new card; anything else a `.psu`). A slot
  becomes the save of its own number unless `--as N` says another, and `all`
  writes folder A's three (or `--slot B`'s) in one go. The game's folder at
  the destination is added to: a `.psu` or a card that already holds saves
  keeps them, and a save of the same number is left alone unless `--replace`
  is given (with `all`, that save alone: the others are written and each
  says what became of it). `--pal` writes the European game's folder.

The Saves tab has the same: **Import...** takes the files, **Import a save
folder (Xbox, PCSX2)...** the folders, and **Export to a PlayStation 2** has
the three destinations, the icon's Browse row, the replace and PAL boxes, and
a folder's saves; each slot's **Export...** offers a PlayStation 2 save too.

In C++ (`iSaveStore.h`): `iSaveStoreImport` and `iSaveStoreImportData` take
every form; `iSaveStoreExportPS2` and `iSaveStoreExportPS2Folder` write, with
`iSavePS2ExportOptions` (the kind, PAL, the icon's place, the save number,
replace); `iSaveStorePS2Icon` says which icon would be written;
`iSaveStorePS2Folders` gives the game's folder out of a file's bytes.
Underneath, with no file system (`iSavePS2.h`): the names, the card's clock,
`icon.sys`, the filler and the port's icon, the icon check and the icon out of
a package, `.psu` and `.psv` both ways (section 9), `.max` read, and the card image:
`iSavePS2CardFormat`, `iSavePS2CardList`, `iSavePS2CardReadFolder`,
`iSavePS2CardWriteFolder`, `iSavePS2CardCheck` and `iSavePS2Ecc`.

## 9. PlayStation 3 saves

A PlayStation 3 that plays PlayStation 2 discs keeps their saves on virtual
memory cards on its hard disk, and its memory card utility moves one game's
folder in and out of a card as a `.psv` file (on a USB stick, for example).
The port reads and writes both: the `.psv`, and the virtual card itself
(`.VM2`, below).

### The .psv

All numbers little-endian; the layout is the one a PlayStation 3 export of
this game has, field for field.

| at | bytes | what |
|---|---|---|
| 0x00 | 4 | `00 56 53 50` ("\0VSP") |
| 0x04 | 4 | 0 |
| 0x08 | 20 | the seed the signature is made from |
| 0x1C | 20 | the signature |
| 0x30 | 8 | 0 |
| 0x38 | 4 | `0x2C`: the size of what follows, up to 0x64 |
| 0x3C | 4 | 2: a PlayStation 2 save |
| 0x40 | 4 | the folder's size as a `.psu` (section 8): 512 bytes for each entry -- the folder, `.`, `..` and each file -- and each file in whole kilobytes (141,824 for the folder with one save) |
| 0x44 | 8 | where `icon.sys` is in this file, and its size |
| 0x4C, 0x54, 0x5C | 8 each | the same for the three icons `icon.sys` names (list, copy, delete); here all three are `SpongeIcon01.ico` |
| 0x64 | 4 | the number of files |
| 0x68 | 0x38 | the folder's entry: created and modified (8 bytes each, the card's clock), the number of entries (the files and `.` and `..`), its mode (`0x8427`), its name (32 bytes) |
| 0xA0 | 0x3C each | each file's entry: created, modified, size, mode (`0x8497`), name (32 bytes), where its bytes are |
| then | | each file's bytes, one after another in the folder's order (the filler, `icon.sys`, the icon, the saves), nothing between them and nothing after |

The file is named after the folder: its first 12 characters, then the rest in
hex, then `.PSV`: `BASLUS-2090453424D6F76696520.PSV` (`SBMovie ` is
`53424D6F76696520`), and `BESLES-5289553424D6F76696520.PSV` for the European
game.

The seed is written as the bytes 00 to 13, as PC tools write it (the PS3
export here has that seed too); a `.psv` that is added to keeps its own.
The folder's modified time is the newest of its files'.

Against the PS3 export of this game: written again from what was read, it is
the same file but for the signature. Imported into a slot and exported into a
copy of itself, the same again. Exported to a new `.psv`, the differences are
the signature, the filler's twenty lines of text (the port's are empty,
section 8) and the times of the folder and of the three files beside the save,
which the port gives the save's own date (the PS3 export has them one second
earlier). Not known: whether a PS3 counts the size at 0x40 the same way for a
folder with two saves (an odd number of entries); the export here has one.

### The signature, and the two the port can write

A PS3 takes a `.psv` only with the signature it would make itself. As the
tools that re-sign `.psv` files describe it: the seed and 44 zero bytes (64 in
all) are decrypted with AES-128 in CBC mode, under a key and an IV that the
PS3 holds; the first 20 bytes of the result are the key of an HMAC-SHA1 over
the whole file, with the signature's own 20 bytes counted as zeros. The key
and the IV are in the PS3, not in the game, and not in this port. So an export
offers two signatures, as the Xbox's does:

| | what it needs | what takes it |
|---|---|---|
| **stand-in signature** | nothing | other copies of this port, and PC tools that read `.psv` files. A PS3 needs it re-signed first, for example by a save tool on a modded PS3 |
| **real signature from your own file** | your PS3 key file (below) | a PS3, as publicly described. **Not yet confirmed on a PS3** |

The stand-in is the same computation with every key value zero (as a key file
of 32 zero bytes would give), which is how the port knows its own again.

### The PS3 key file

A file you provide, of exactly 32 bytes, raw (not text, not hex):

| bytes | what |
|---|---|
| 0 to 15 | the AES-128 key the PS3 signs PlayStation 2 `.psv` files with |
| 16 to 31 | the 16-byte IV that goes with it |

Some tools keep that key as two 16-byte values to be combined (XOR) first;
this file holds the combined one, the key the AES step uses. A file of any
other size is refused, with its size as the reason; one of only zeros is
refused too (it would only be the stand-in again). The port ships no key,
never asks for one to be typed, and never shows, logs or saves one: it reads
the file where you say it is (`--psv-keys`, or `[game] psv_keys`), uses it in
memory and clears it. It cannot tell whether the file holds the right values;
with the file at hand, importing a `.psv` a PS3 made says "real signature
(matches your PS3 key file)" when it does.

**Importing needs no key file.** The port reads a `.psv` whatever its
signature and checks each save's own CRC. It says which signature the file
carries, as far as it can tell: "stand-in signature (a PS3 needs it
re-signed)"; "real signature (matches your PS3 key file)"; "signature not
recognised" (a key file is at hand and it is neither); or "signature unknown"
(not the stand-in, and no key file to tell more by; when a key file is set
but cannot be used, the message says why, for example its size).

### A PS3's virtual memory card (.VM2)

The virtual card itself is a `.VM2` file: an 8 MB PlayStation 2 card image
without the spare bytes (8,388,608 bytes), which section 8's card code reads
and writes as it is. Importing takes the game's saves out of it; exporting
into one adds to its folder, after copying it to `.bak` (`--ps2 card`, or
`--ps2 auto`); a new name ending `.vm2` makes a new formatted card without
spare bytes. Not tested: a PS3 reading a card the port made or wrote to, and a
`.VM2` that came from a PS3.

### Using it

```
tssm --import-save FILE.psv | CARD.VM2 [--psv-keys FILE] [--slot S] [--pick N] [--force] [--dry-run]
tssm --check-save  FILE.psv [--psv-keys FILE]
tssm --export-save S   DEST --psv standin|real [--psv-keys FILE] [--ps2-icon PATH] [--pal] [--as N] [--replace]
tssm --export-save all DEST --psv standin|real [--psv-keys FILE] [--ps2-icon PATH] [--pal] [--slot B] [--replace]
tssm --export-save S   CARD.VM2 --ps2 card|auto [--replace] ...
```

- **Export.** `--psv standin` or `--psv real` picks the signature; `--ps2 psv`
  is the stand-in too, and `--ps2 auto` writes a `.psv` when `DEST` is one or
  is a new name ending `.psv`. `DEST` a folder gets the PS3's file name inside
  it. A `.psv` there already is added to: a save of the same number is left
  alone unless `--replace` is given. The icon and the PAL folder are as for a
  PlayStation 2 export (section 8). With `--psv real`, a missing key file or
  one of the wrong size stops the export before anything is written.
  `--psv-keys` goes with `--psv real` only; with any other export it is
  refused, so a key file is never given and then passed over.
- **Import.** A `.psv` and a `.VM2` are told by their contents, like the other
  PlayStation 2 forms; `--psv-keys` (or `[game] psv_keys`) is only for saying
  which signature a `.psv` carries.

The Saves tab has the same: **Import...** takes `.psv` and `.VM2` files;
**Export to a PlayStation 3** has the choice "Stand-in signature (this port
and PC save tools; a PS3 needs it re-signed)" or "Real signature from your own
file (not yet confirmed on a PS3)", a Browse/Clear row for the key file when
the second is chosen (a file that is not one is said in red under it and the
export stays off), replace and PAL boxes, and a folder's saves as one `.psv`;
each slot's **Export...** offers a PlayStation 3 save too. A `.VM2` is a
memory card file to **Export to a PlayStation 2**.

In C++ (`iSaveStore.h`): `iSaveStoreExportPS2` and `iSaveStoreExportPS2Folder`
with `iSavePS2ExportOptions` (`kind` `ISS_PS2_PSV`, `psvSignature`
`ISS_PSV_STANDIN` or `ISS_PSV_REAL`, `psvKeys`); `iSaveStorePsvKeys` and
`iSaveStorePsvKeysUsable` say where the key file would be read from and
whether it serves; `iSaveImportOptions::psvKeys` and `iSaveStorePsvSignature`
tell a `.psv`'s signature. Underneath, with no file system: `iSavePS2.h`
(`iSavePS2PsvName`, `iSavePS2PsvBuild`, `iSavePS2PsvSign`, `iSavePS2PsvVerify`,
`iSavePS2PsvKeysParse`) and `iSaveAES.h` (AES-128, one block at a time).
