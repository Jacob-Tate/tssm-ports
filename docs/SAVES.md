# Saves: formats, import and export

How The SpongeBob SquarePants Movie keeps a save, on the GameCube and in the PC
port, and how the port moves saves between the two. Everything below is read
out of the decompiled code (`src/SB/Core/x/xsavegame.cpp`, `xserializer.cpp`,
`src/SB/Game/zSaveLoad.cpp`, `src/SB/Core/gc/isavegame.cpp`) and the Dolphin
SDK's CARD sources in `src/dolphin/src/card`.

**The short version.** A save is the same bytes on both machines: the game's
own *payload*. The GameCube wraps it in a memory-card file (a banner, an icon,
a comment and a directory entry); the PC port stores the payload alone. So
importing a GameCube save strips the wrapper and exporting one puts it back,
and nothing in the game state is translated.

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
  skipped), a zip from `--export-save all`, or a bare payload (a save file
  from another install's save folder). Each save is checked -- the card head's
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
Saves come out the same way (Export GCI). The tools here only read the card
images and `.gci` files they are given; nothing writes to a memory card.

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
test's reference is built from the same reading), and a real `.gcs`. Xbox
saves (the release the port's assets come from) are a different wrapper again
and are not read.
