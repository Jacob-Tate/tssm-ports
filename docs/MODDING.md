# Modding the PC port

The PC port of The SpongeBob SquarePants Movie loads mods from a `mods` folder. Each mod is a
folder of loose files. The game's own files are never changed, so removing a mod (or switching
it off) always gives back the original game.

A mod can replace:

- **Files:** a whole package, an INI file, a movie.
- **Assets:** any single asset of any package, chosen by its ID or by its name. Text assets can be
  written as plain `.txt` files.
- **Textures:** chosen by name, from a PNG, DDS, TGA or BMP image.
- **Sounds:** chosen by ID or name, from a WAV file. If the build has FFmpeg (the one that plays the
  movies), OGG, MP3, FLAC and other formats work too.
- **Movies:** chosen by name, in any format FFmpeg reads.
- **Settings in the game's INI files:** a small overlay changes only the keys it names.

Several mods can be installed at once. A load order decides which one wins when two of them
replace the same thing, and every such clash is reported.

The code is in `src/SB/Core/pc/iMod*.{h,cpp}`; `iMod.h` is the overview and holds the API the
in-game mod manager (the overlay's Mods tab, `docs/GUI.md`) uses.

## Where mods go

    <game folder>/
      tssm.exe
      config.ini
      mods/
        mods.ini            load order and on/off switches
        mods.log            what the last run replaced (rewritten at every start)
        MyMod/
          mod.ini
          files/ assets/ textures/ sounds/ fmv/ ini/

The game looks for the `mods` folder in this order and uses the first one that exists:

1. `TSSM_MODS_DIR`, an environment variable;
2. `[assets] mods_folder` in `config.ini`;
3. `mods/` beside the executable;
4. `mods/` in the per-user data folder: `%APPDATA%/tssm/mods` on Windows,
   `~/.local/share/tssm/mods` on Linux.

On Android, step 3 is the app's external files folder instead,
`Android/data/io.github.tssmport.game/files/mods` (beside the imported game files and the log,
reachable from a computer over USB), followed by the app's internal `files/mods` and
`files/tssm/mods`. docs/ANDROID.md.

`[assets] mods = off` in `config.ini` (or the environment variable `TSSM_MODS=0`) switches all mods
off without uninstalling any of them.

A subfolder of `mods/` counts as a mod if it has a `mod.ini`, or at least one of the content folders
below. Anything else in there is ignored.

## mod.ini

    [mod]
    name = Solid Colours
    author = you
    version = 1.0
    description = Makes the title card magenta.
    enabled = 1
    priority = 0

Every key is optional. Without a `name`, the mod is shown under its folder's name.

- **`enabled`** is only the starting value. Once `mods.ini` lists the mod, the value in `mods.ini`
  is used instead.
- **`priority`** places a mod that `mods.ini` does not list yet. A higher number loads later, so it
  wins over lower ones.

## Load order: mods.ini

    [order]
    SolidColours = 1
    BetterText = 0
    BigTextures = 1

- **Order:** mods are applied from top to bottom, so a mod further down wins.
- **Values:** `1` turns a mod on, `0` turns it off.
- **Keys:** each key is a folder name.
- **New mods:** a folder that is not listed is added after the listed ones, ordered by its
  `priority` and then by folder name.
- **Removed mods:** a listed folder that no longer exists is skipped.

The in-game mod manager writes this file. You can also edit it by hand while the game is closed.

## Conflicts and the log

A conflict is two enabled mods replacing the same thing, for example:

- the same file;
- the same texture name;
- the same asset, even when one mod names it by ID and the other by name.

Conflicts are allowed: the mod later in the load order is used. Each conflict is printed once:

    tssm: mod: conflict: texture 'ui_titlecard' -- 'Mod B' wins over 'Mod A' (later in the load order)

Every replacement is logged in three places:

- standard output;
- `mods/mods.log`;
- the mod manager's log view.

For example:

    tssm: mod: FONT_US.HIP: 1 asset(s) replaced, some moved to the end of the package
    tssm: mod:   20D7D45E TEXT 'startup_loading_text' <- Sample Mod: assets/20D7D45E.txt (16 -> 28 bytes, layer moved)
    tssm: mod: texture 'ui_titlecard' <- .../SampleMod/textures/ui_titlecard.png (256x256, was 256x256)
    tssm: mod: movie logonicg <- Sample Mod: fmv/LogoNicG.movie

If a mod seems to do nothing, check the log first.

## What a mod can contain

### files/ — whole files

`files/<path>` replaces `<asset folder>/<path>`. The path is matched without regard to case, for
example:

- `files/boot.HIP`
- `files/sb04.ini`
- `files/FMV/LogoHI.bik`
- `files/MN/mnus.HOP`

A replacement package can come from the Xbox release or the GameCube one, and the two can be mixed:
each package's PLAT chunk says which console wrote it. A GameCube package is converted to the Xbox
layout when it is opened. Its textures and models are read in the GameCube's own formats, and its
texture names, which the GameCube disc leaves out, are taken from the asset names. The log says
`tssm: GameCube package <name>: N asset(s) converted, ...`. A GameCube package that cannot be
converted (a table of contents that does not hold together, an asset of a kind the port has no rule
for, or one that does not fit its own sizes) is refused with a log line saying why, and so is a
package from the PS2 release; the original is used then.

Every replacement package, Xbox or GameCube, is checked before the game reads it: a file that is
not a package at all (empty, or damaged at the start), or one whose table of contents does not hold
together (cut short, a damaged chunk or count, an asset past the end of the file), is refused with
a log line such as `tssm: mod: JK/jk01.HIP cannot be read: ...; using the original`. The game itself
checks none of this and would crash reading such a file. Damage inside an Xbox asset's own data is
not caught. Inside a GameCube package, the assets that are converted are checked as they are; of
those read as they are, a model has to hold together as a RenderWare clump or the package is
refused, and the rest (textures, fly-throughs, sounds) are checked as far as their readers check
them. A model that still does not read, from either release, ends the game with a message naming
it (`tssm: FATAL -- a game file could not be read: model ...`) instead of a crash.

A GameCube package's sounds play as they do on the console: its sound table holds the GameCube's
FSB3 banks of DSP-ADPCM, which the port reads in place and decodes, and its cutscenes' voice tracks
are DSP-ADPCM too. Xbox and GameCube packages can be mixed in one game, each with its own sounds.
Not yet supported: a `sounds/` replacement of a sound inside a GameCube sound table. It is logged,
and the original sound is kept. Replace the whole package with an Xbox one if a sound has to
change.

The game itself can also run from a GameCube disc ("The game's files" in
`src/SB/Core/pc/README.md`); every package of it is then a GameCube package, and Xbox packages in
mods work beside them.

Only the packages a mod ships are read this way. Code patches (Action Replay or Gecko codes, a
changed `main.dol`) cannot be applied: the port runs its own build of the game code.

The older single-folder setting, `[assets] mod` (or `TSSM_MOD`), still works. That folder copies
the asset folder's layout, and its files win over every mod's.

### assets/ — single assets of any package

    assets/<ID>.<anything>              that asset ID in any package
    assets/<name>.<anything>            the asset with that name in any package
    assets/<package>/<ID or name>.*     only in that package

The file names work like this:

- **ID:** eight hexadecimal digits (`1D327E54` or `0x1D327E54`), the same as the log and `modkit`
  print them.
- **Name:** the asset's name, matched without regard to case. The file's last extension is
  ignored, and so is any extension on the asset's name. So `ui_loading_bkg.bin` matches the asset
  `ui_loading_bkg`, `startup_loading_text.txt` matches `startup_loading_text`, and
  `genenemy_throw.bin` matches `genenemy_throw.WAV`.
- **Package:** the package's file name without its extension, any case: `boot`, `font_US`,
  `mnus_US`, `bb01`.

The file's bytes replace the asset's bytes exactly. There is one exception: a `.txt` file that
replaces a TEXT asset is converted into a TEXT asset for you. Write UTF-8; characters up to
U+00FF become the game's 8-bit characters, and a trailing newline is dropped. The game's markup,
such as `{i:text_button_center_01}`, works as it does in the original text.

You can only replace an asset that exists. Adding new assets is not supported yet, and an empty
file is ignored with a log line.

**Always the Xbox layout.** A file in `assets/` is read in the Xbox release's layout, even when it
replaces an asset of a GameCube package: the port converts the GameCube package's own assets, not
a mod's replacement. So take the bytes from the Xbox disc's package (`modkit extract` refuses a
GameCube asset unless you add `--raw`). A `.txt` file for a TEXT asset works with either release.

**Text length.** Most text is laid out in text boxes, and a longer replacement simply takes more
room on screen. A few strings, though, are copied into fixed-size buffers by the game's own code,
and a replacement of the wrong length crashes the game. The one found so far:

- **`startup_loading_text`** (`Loading...`, in `font_US.hip`) must be **10 or 11 characters**.
  `zMainShowProgressBar` copies it into a 12-byte buffer, so a longer text overflows the buffer.
  It also splits the text at `progress / 10`, which reaches 10, so a shorter text makes it copy a
  negative length.

If the game crashes soon after a package with a replaced text loads, try a replacement the same
length as the original.

Replacement assets can be bigger than the originals. They are put in place as follows:

1. A replacement that fits in the old asset's space is written there.
2. A bigger one moves the asset's whole layer to the end of the package.
3. In both cases the package's table of contents is rewritten to match.

All of this happens in memory when the package is opened, and the file on disk is not changed.
Every reader sees the same, modded package:

- the packer;
- streamed textures;
- sounds;
- cutscenes.

If a mod replaces an asset that the package's sound table (SNDI) describes, the size in its table
entry is updated to match.

### textures/ — images by texture name

    textures/<texture name>.png   (or .dds, .tga, .bmp; subfolders are allowed and ignored)

**Naming.** The name is the texture's RenderWare name, which is usually the asset's name without
`.RW3`. To list a package's texture names:

    python tools/modkit.py textures <assets>/MN/mnus.HOP

To find which package uses a texture:

    python tools/modkit.py find <assets> titlecard

A texture name used by several assets is replaced everywhere it appears.

**Formats.** Each image is decoded when a texture with its name is loaded, and replaces the
texture's pixels. The game's filtering, wrapping and every reference to the texture stay as they
were.

- **PNG:** any colour type, decoded to RGBA.
- **DDS:** DXT1, DXT3 or DXT5, or uncompressed 24/32-bit RGB(A). Only the top level is used.
- **TGA and BMP:** through librw's readers.

**Size.** Use power-of-two sizes. They do not have to match the original's size: a 512×512
replacement for a 128×128 texture works.

**Limitations.**

- Replacement textures have no mipmaps yet, so very distant surfaces may shimmer a little.
- 12 of the game's 2,995 textures have an empty RenderWare name and cannot be replaced this way.
  Replace their RWTX asset through `assets/` instead.

**Button prompts.** The on-screen button prompts are drawn from textures in `font.HIP`:
`pad_button1`–`4`, `pad_button_start`, `pad_button_select`, `pad_button_L1`/`L2`/`R1`/`R2`,
`pad_button_L_analog`, `pad_button_R_analog` and `pad_button_upgrade`. A button-glyph pack, such as
keyboard or PlayStation symbols, is therefore a texture mod.

### sounds/ — sound effects and music by ID or name

    sounds/<ID or name>.wav            any package
    sounds/<package>/<ID or name>.ogg  one package

These name the game's `SND` and `SNDS` assets, which are sound effects and streamed music. Names
work as in `assets/`: `genenemy_throw.wav` replaces the asset `genenemy_throw.WAV`.

**Decoding.**

- The file is decoded when its package opens, to 16-bit PCM at the file's own sample rate.
- Mono stays mono. More than two channels are mixed down to stereo.
- The sound's SNDI entry is rewritten to describe PCM (format tag 1).
- The loop flag is kept.

**Formats.**

- WAV (PCM 8/16/24/32-bit or float) always works.
- With FFmpeg in the build, anything FFmpeg decodes also works: OGG Vorbis, Opus, FLAC, MP3, and so
  on.

**Limitations.**

- Cutscene voice tracks live inside the cutscene (CSN) assets and are not replaced this way.
- PCM is about 3.5 times the size of the Xbox ADPCM it replaces, so very long music tracks take
  more memory.
- A sound in a GameCube package's table (above, "files/") is not replaced yet: the log says so,
  and the original plays.

### fmv/ — movies by name

    fmv/<movie name>.<anything>        e.g. fmv/LogoNicG.mp4, fmv/story1.webm

The name is the one the game asks for, without its folder or extension, matched without regard to
case. The movie names are listed in `src/SB/Game/zFMV.cpp`:

- the logos: `LogoNicG`, `LogoNicM`, `LogoTHQ`, `LogoHI`;
- the attract-mode movie: `SBAttr`;
- the story movies: `story1` to `story12`;
- the power-up movies: `ps_*` and `pp_*`;
- the extras: `promoSB1`, `promoAva`, `promoFOP`, `promoJmy`, `promoTak`, `promo6`, `MakingOf`,
  `Trailer1` to `Trailer3`.

Five of the extras have no file on the Xbox disc: `promoFOP`, `promoJmy`, `promo6`, `MakingOf` and
`Trailer1`. A mod can supply them.

The extension does not matter: FFmpeg reads the file's contents to tell what it is. That means any
container and codec FFmpeg decodes will play, for example MP4/H.264, WebM/VP9 and Bink. The movie is
scaled to the screen with its aspect ratio kept. The first audio track is played.

`files/FMV/<name>.bik` works too, as a plain file replacement. If both exist, the mod later in the
load order wins.

### ini/ — overlays on the game's INI files

    ini/sb04.ini       (also explode.ini, water.ini)

Unlike a file in `files/`, an overlay changes only the keys it names:

    # Sample: start rich, don't take damage.
    G.InitialShinyCount = 1234
    G.TakeDamage = 0
    -G.CheatAlwaysPortal          # a leading minus removes a key
    [Section]                     # keys inside a section are matched only in that section
    Speed = 2

The rules:

- **Replacing a key.** Every line of that key in the original is removed. The overlay's lines go
  where the first of them was. Give a key several times to set a repeated key such as
  `LanguageSetting`.
- **New keys** are added at the end of their section. **New sections** are added at the end of the
  file.
- **Everything else in the file** (comments, blank lines, other keys) is kept as it was.
- **Several mods** with overlays on the same file are applied in load order, so the last one wins
  for each key.
- **Comments:** lines starting with `#` or `;` in an overlay are comments. The game's own INI uses
  `#`.

The log shows each change, with the old value:

    tssm: mod: sb04.ini <- Sample Mod: ini/sb04.ini
    tssm: mod:   G.InitialShinyCount = 1234 (was 0)

## Tools

    python tools/modkit.py list <package> [--type TEXT]     every asset: ID, type, size, layer, name (and the text of TEXT)
    python tools/modkit.py textures <package>               texture names and sizes
    python tools/modkit.py find <assets folder> <what>      search IDs, names, texture names and text everywhere
    python tools/modkit.py extract <package> <ID|name> [out]  one asset; TEXT comes out as .txt
    python tools/modkit.py new <mods folder> <Folder>       a mod skeleton with mod.ini

modkit reads the Xbox and the GameCube release's packages. A GameCube package is said so on the
error output; its text is read big-endian, its textures (unnamed on the disc) are listed under the
names the port gives them, and `extract` writes a GameCube asset other than TEXT only with `--raw`
(see "Always the Xbox layout" above).

For example, to change the title screen's prompt:

    python tools/modkit.py find C:/Games/TSSM-Xbox "START button"
    #   MN\mnus_US.hip  715B6D70 TEXT PRESS START TEXT  text "Press the START button"
    python tools/modkit.py new mods MyFirstMod
    echo Press START, pal!> "mods/MyFirstMod/assets/mnus_US/PRESS START TEXT.txt"

## Example: the sample mod

The mod system was checked with this mod on the Windows x86 build (2026-10-23):

    mods/SampleMod/
      mod.ini
      textures/ui_titlecard.png                 a solid magenta 256x256 PNG
      textures/buttons/pad_button_start.dds     a solid green 32x32 DXT1 DDS
      assets/mnus_US/PRESS START TEXT.txt       "Press START - modded!"
      assets/03BEDDB5.txt                       text_menu_continue, longer than its slot
      assets/font_US/startup_loading_text.txt   "Modding..." (10 characters, see above)
      sounds/genenemy_throw.wav                 a 0.5 s 660 Hz tone (boot.HIP)
      fmv/LogoNicG.movie                        another logo movie under the first logo's name
      ini/sb04.ini                              G.InitialShinyCount = 1234, and a new key

In the run:

- The first logo movie showed the replacement.
- The title screen showed the magenta card and "PRESS START - MODDED!".
- With scripted START and A presses, the game went on into the main menu and the save prompt.
- The log listed every replacement, including the moved TEXT layer in `font_US.hip` (120 assets)
  and the SNDI entry rewritten as 22,050 Hz PCM.

## Changing mods while the game runs

The mod manager (the overlay's Mods tab: F1, or Back + Start on a controller) can do these things
while the game runs:

- turn mods on and off, or all of them with **Use mods** (`[assets] mods`);
- reorder them, with the arrows or by dragging a name;
- rescan the folder;
- show the conflicts and the log (filter, copy, open `mods.log`).

Each change is written to `mods.ini` at once.

**What updates when.** The index is rebuilt at once, but a change only affects what is loaded
afterwards. A package that is already loaded keeps its old assets until the game loads it again,
for example on the next level change. `iModRestartAdvised()` reports when a restart is needed to
see a change everywhere.

## Testing

`tssm_modtest` is built next to `tssm.exe`. It runs the mod system without the game:

    tssm_modtest <your asset folder> <a scratch folder>

It checks:

- INI overlays;
- sound decoding;
- the package rewriting, against real packages: every asset of every layer is read back the way
  the packer reads it and compared;
- the mod manager.

It prints `N checks, 0 failed` and exits with 0 when everything passes. The package tests are
skipped when no asset folder is given.
