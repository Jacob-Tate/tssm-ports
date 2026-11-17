# Nintendo 3DS (homebrew)

**Still in progress.** The 3DS port runs the whole game, but it has been played on a
console only a little: expect rough edges, and see *Known gaps* at the end.

The port builds as a homebrew program for the **New 3DS, New 3DS XL and New 2DS XL**
running custom firmware (Luma3DS), in two packages made from the same program:

- `tssm.cia`, installed with a CIA installer (such as FBI) and started from the HOME Menu;
- `tssm.3dsx`, started from the Homebrew Launcher.

Both use the same folder on the SD card, so the settings and saves are the same in
either. The top screen shows the game at 400 x 240, drawn with citro3d, in stereoscopic 3D
when the 3D slider is up; the bottom screen holds the settings menu.

An original 3DS, 3DS XL or 2DS is not supported and will not be: it has a third of the
CPU and half the memory the game gets on a New 3DS. The game checks at start and says so
on the screen.

## Playing

### What you need

1. A New 3DS, New 3DS XL or New 2DS XL running Luma3DS.
2. `tssm.cia` or `tssm.3dsx` (or both): the prebuilt *TSSM Dehydrated* ones from this
   repository's Releases page, or your own, built as described under *Building*.
3. Your own copy of the game's **Xbox** files (the same files the PC, Android and Switch
   builds use).
4. For sound, the console's DSP firmware dump, `sdmc:/3ds/dspfirm.cdc`. Make it once:
   open the Rosalina menu (L + Down + SELECT), Miscellaneous options, Dump DSP firmware.
   Without it the game runs silent and the log says so.

### Installing

**The .cia.** Copy `tssm.cia` anywhere on the SD card (for example `/cias/`), open a CIA
installer such as FBI, find the file and choose *Install CIA*. The HOME Menu then has
"The SpongeBob SquarePants Movie Videogame" ("TSSM Dehydrated" for the prebuilt one) with
a sponge block icon (or an icon made from your own game files: see *An icon from your own
game files*). A newer `tssm.cia` installs
over the old one the same way; the settings and saves are files on the SD card
and stay. To remove it: System Settings, Data Management, Nintendo 3DS, Software (or FBI's
Titles, Delete Title). The `.cia` file itself can be deleted once installed.

**The .3dsx** goes in `/3ds/tssm/tssm.3dsx` and starts from the Homebrew Launcher. For a
quick test it can also be sent over the network: in the Homebrew Launcher press Y (the
network loader), then on the PC run `3dslink -a <console IP> tssm.3dsx`.

### The game's files

Put the SD card in a PC and copy the files of your Xbox game disc into the SD card's
`3ds/tssm/files` folder (make the folder if it is not there), as they are at the top of
the disc: `boot.HIP`, `font.HIP`, `plat.HIP`, the level folders (`B1`, `BB`, `JK`, `MN`,
...) and `FMV`. Not the folder above them, and not a disc image. Copy all of them: a level
whose `.HIP` or `.HOP` is missing stops the game when it is entered, with the file's name
on the bottom screen. A missing movie is skipped.

If the game does not find the files (no folder, an empty one, or no readable `font.HIP`
and `boot.HIP` in it) it says so before anything else: the top screen names the folder,
the bottom screen says what is missing (and, when it sees one, a disc image or the files
one folder too far down). START closes the game.

Everything the game writes goes into the same folder:

| Path on the SD card | What |
|---|---|
| `/3ds/tssm/files/` | the game's Xbox files (you copy these) |
| `/3ds/tssm/tssm.3dsx` | the .3dsx, when you use it |
| `/3ds/tssm/config.ini` | settings, written with the defaults on the first run |
| `/3ds/tssm/saves/` | saves (`docs/SAVES.md`) |
| `/3ds/tssm/mods/` | mods (`docs/MODDING.md`) |
| `/3ds/tssm/import/`, `/3ds/tssm/export/` | saves to import, and exported saves |
| `/3ds/tssm/last_run.log` | the log of the last run (`last_run.prev.log`: the one before) |
| `/3ds/tssm/crash.log` | crash reports |

### Controls

The buttons follow the Xbox layout the port uses everywhere, **by position**, as on the
Switch: the bottom button does what the Xbox's A does, and so on round. Everything on
screen names a button by the letter **printed** on the 3DS.

| 3DS control | Does what the Xbox's ... does | In the game |
|---|---|---|
| B (bottom) | A | jump, double jump; menus: accept; car: drive |
| A (right) | B | SpongeBowl, Cartwheel; menus: back; bungee: let go |
| Y (left) | X | Karate Spin, Star Spin, Smash; menus: options |
| X (top) | Y | Bash, swing; skip a cutscene |
| ZL | LT | camera: tap to centre, hold for the close-up; shows the HUD |
| ZR | RT | talk, pick up and throw, Sonic Wave; car: nitro |
| L, R | white, black | nothing in normal play (R also wakes the menus) |
| SELECT | Back | upgrade window |
| START | Start | pause |
| D-pad | D-pad | walk; menus |
| Circle Pad | left stick | move |
| C-Stick | right stick | camera |
| touch screen | - | only the settings menu |

So a prompt that says "B Choose" means the bottom button, and "A Return" the right one.
The game's button pictures are drawn with the 3DS's letters.

The Circle Pad and the C-Stick do not quite reach the range SDL scales them by, so 85% of
that range counts as full tilt; the dead zone is the Xbox release's (26% of full tilt per
axis). These are the `3ds` preset's defaults (`input.preset = auto` picks it); `[pad]` in
`config.ini` rebinds anything (`docs/CONTROLS.md`), with the Xbox's names (`a` is the
bottom button, `lt` ZL, `white` L).

### The settings menu (bottom screen)

**START + SELECT** (together, in either order) opens the settings menu on the bottom
screen, and closes it again. While it is closed the bottom screen says "START + SELECT:
menu" for 10 seconds after the game starts, then fades to black; a tap on the bottom
screen brings the line back for another 10 seconds. `[gui] bottom_hint_seconds` in
`config.ini` changes the 10 (-1 keeps the line, 0 never shows it). The game keeps running
on the top screen while the menu is open, but takes no input from the buttons.

- **Buttons**, by position as in the game: the D-pad or the Circle Pad moves, **B**
  chooses, **A** goes back (and closes a box), **L** and **R** change the tab. In the file
  browser **X** goes up a folder and L and R move a page.
- **Touch:** tap anything; drag a scroll bar (a stylus helps: the text is small).
- **Tabs:** Settings, Controls, Saves, Mods, About. **Resume** closes the menu; **Quit
  game** asks, then returns to the HOME Menu (the .cia) or the Homebrew Launcher (the
  .3dsx), the same way HOME's Close does (progress since the last save is lost).
- **Settings:** the frame rate (30, or 60 for light scenes), field of view, the glow and
  the screen warp, 3D (Auto, Always or Off) and 3D depth (*3D*, below), the four volumes,
  the logo movies, the save folder and the menu's text size. What does nothing on the 3DS
  (the renderer, window and render size, anti-aliasing, vertical sync, rumble, touch
  controls, keyboard and mouse) is left out.

The game sees START and SELECT a moment late: each is held back for up to 120 ms in case
the other follows. If the menu ever misbehaves, `[gui] overlay = off` in `config.ini`
turns it off: the bottom screen stays dark and START and SELECT go straight to the game.

**Saves.** Put a save in `/3ds/tssm/import/` (with a card reader, or FTP): a Dolphin
`.gci`, a GameShark `.gcs`, a memory card image (`.raw`, `.gcp`), or the `.zip` that
**Every save as a .zip** writes on the PC, the phone or the Switch. Then Saves,
**Import...**: choose the file, pick the slots, Import. A save it replaces is kept as a
backup. **Export** beside a slot, **Every save as a .zip** and **Every save into a GCI
folder** write into `/3ds/tssm/export/`. A file too big for the memory free at the time is
refused with a message (a 16 MB Dolphin card fits); anything over 64 MB is no save.

### 3D (the slider)

The 3D slider turns stereoscopic 3D on and sets how deep it is; all the way down is 2D.
The world is drawn for each eye; the HUD, the menus, text and the movies stay at the
screen's depth. The point the eyes meet at follows SpongeBob (or Patrick), so he sits at
the screen and the world goes in behind him; through a fly-through or a cutscene it holds
where it was. The title screen, the pause menu, the options and the galleries are flat. A
New 2DS XL has no slider and is always 2D.

The settings menu's Settings tab has a **3D** group:

- **3D:** **Auto** (the default) turns 3D off by itself for a while where the game would
  stutter. **Always** keeps 3D on even where it runs slowly. **Off** is always 2D. With
  Auto and Always the slider still sets how deep the 3D is, and all the way down is still
  2D.
- **3D depth** (0 to 100): how deep the 3D is with the slider at the top.

Both take effect at the next frame, easing in or out as the slider does, and are saved to
`config.ini` with the other settings (the depth when you let go of its slider). In
`config.ini` they are two lines (the game adds both, at their defaults, to a `config.ini`
from an older build; nothing else in the file changes):

    [video]
    stereo = auto
    stereo_depth = 50

`stereo` is `auto`, `always` or `off`, in capitals or not (`Always` and `OFF` work too);
any other word is taken as `auto`, and the log says so. `stereo_depth` is the menu's 3D
depth.

**Auto (the speed guard).** The second eye about doubles the GPU's work. In Auto, when a
busy place keeps losing frames in 3D (not one stutter or two, but about one frame in seven
running late over two seconds, or the GPU over its time for a frame), the game eases 3D out
there by itself and plays in 2D, then eases 3D back in once its frame times say a 3D frame
would fit again: after 3 seconds the first time, twice as long after each later drop (at
most a minute; back to 3 seconds when a level loads). The log says each time 3D goes off
and comes back. **Always** never turns 3D off: it stays on, and the game runs slower where
it has to.

### Drawing effects (`[3ds] effects`)

Every effect is on by default (`effects = all`). `none` is the fastest: it leaves out the
shadows, the fog, the glow, the heat warp and the loading-screen picture. A list picks
some:

    [3ds]
    effects = fog,rtt

| Word | Turns on |
|---|---|
| `none` | nothing |
| `fog` | the fog |
| `rtt` | pictures drawn into textures (render to texture) |
| `shadow` | the characters' shadows (and `rtt`) |
| `glow` | the HDR glow (and `rtt` and `copy`) |
| `warp` | the heat warp (and `copy`) |
| `copy` | screen copies (needed by the glow and the warp) |
| `orthoz` | the depth range of parallel cameras (the treasure chest on the pause and Extras screens) |
| `selftest` | a GPU self-test at start-up, for testing (each step needs its effect on) |
| `all` | all of the above but `selftest` (the default) |

Words go in any case, separated by commas, spaces or `+`; an unknown one is named in the
log and ignored. If the game freezes with one on, set `effects = none` and look at
`last_run.log`; the log says near the start which effects a run has.

### Movies and sound

Movies (`FMV/*.bik`) play their own sound beside the game's, and the picture follows the
sound: on the 3DS a frame that is already late is skipped. Movies need the FFmpeg build
step below; without it they are skipped. The game's own sounds pause while a movie plays,
as on the console.

### Sleep and the HOME Menu

Closing the lid or pressing HOME pauses the game: its clock stops, and the sound stops with
it. When you come back it carries on from the same moment. Back from the HOME Menu the
sound starts again by itself. After a sleep the sound simply carries on; if it has not
come back after about a second, the game starts it again. (An earlier build could hang
after the lid was opened, both screens black and no button working, while it restarted the
sound after the sleep.)

### If something goes wrong

- `last_run.log` (and `last_run.prev.log`) in `/3ds/tssm/` say what happened. Closing the
  game writes a `tssm: 3ds: quit:` line before each step, so a console that goes wrong
  while closing leaves the step it was in as the last line. Sleep and wake are written the
  same way, each line saved to the SD card at once, so if the screens stay black after the
  lid is opened, the log's last line is the step the game stopped at.
- A crash writes the registers and the code addresses on the stack to `crash.log`;
  Luma3DS's own exception screen and dump (`/luma/dumps/arm11/`) follow. Both packages run
  at the address the ELF was linked at, so the addresses are `tssm.elf`'s:

      arm-none-eabi-addr2line -f -C -e tssm.elf 0x2a5f10

- To read the log while the game runs: `3dslink -s` streams the game's output back (only
  for a .3dsx sent with 3dslink; Windows Firewall blocks that incoming connection by
  default), or `TSSM_3DS_LOG_SERVER=on` in `test-env.txt` (*Test settings*, below) makes
  the game serve `last_run.log` on TCP port 28772. Otherwise the game opens no network
  connection at all (unless 3dslink started it).

## Building

Everything but the two `.cia` tools comes from devkitPro. The build runs in devkitPro's
MSYS2 shell (Windows) or any shell with `DEVKITPRO` set (Linux, macOS, the
`devkitpro/devkitarm` Docker image).

1. **devkitPro** with the 3DS packages:
   `pacman -S 3ds-dev 3ds-cmake 3ds-pkg-config` (devkitARM, libctru, citro3d, citro2d and
   the 3DS tools: 3dsxtool, smdhtool, 3dslink, picasso, tex3ds). Tested with devkitARM's
   GCC 16.1.
2. **The source and its submodules**:

       git clone --branch 3ds <this repository's address> tssm
       cd tssm
       git submodule update --init --depth 1

3. **SDL 3**: upstream SDL's own 3DS port, from this repository's `third_party/SDL`,
   built static, with the patches in `tools/3ds/sdl/` applied to that source first (they
   put time limits on the 3DS audio driver's waits, so a wake from sleep cannot leave it
   waiting for ever):

       sh tools/3ds/build_sdl.sh third_party/SDL <sdl build dir> <prefix>

   The script applies the patches itself and skips one that is already applied, so git
   shows `third_party/SDL` as modified afterwards; that is expected.

4. **FFmpeg** (optional, for the movies): the decoder-only LGPL build the Switch and
   Android use, for armv6k, from an FFmpeg release (tested: n9.0.2), installed into the
   same prefix:

       sh tools/3ds/build_ffmpeg.sh <ffmpeg source> <work dir> <prefix>

   None of the three paths may contain spaces (FFmpeg's configure); build into a plain
   folder.
5. **The port**:

       sh tools/3ds/build_port.sh <build dir> <prefix> [-DTSSM_ICON_DIR=<icons>]

   This makes `<build dir>/tssm.3dsx`, `tssm.cia` (step 6) and `tssm.elf` beside them.
   Keep the ELF with them: crash reports are addresses in that `tssm.elf`.
   `build_port.sh` runs CMake's configure step only when the build folder is new, so the
   `-D` settings here and in step 6 count on the first run only: pass them then, or
   delete the build folder first. The build applies the port's patches inside
   `third_party/librw`, so git shows that submodule as modified afterwards; that is
   expected.
6. **For `tssm.cia`**: makerom (Project_CTR, tested v0.19.0) and bannertool (tested 1.2.0,
   the diasurgical fork), which devkitPro does not have. CMake finds them on the `PATH`, in
   the folder `TSSM_CTR_TOOLS` names (a CMake variable or an environment variable), or as
   `-DTSSM_MAKEROM=<makerom>` and `-DTSSM_BANNERTOOL=<bannertool>`; without them it says so
   and makes the .3dsx alone. The target is `tssm_cia`, part of the default build.
   bannertool cuts the HOME Menu name at 32 characters; with Python 3 on the `PATH` (or
   `-DTSSM_PYTHON=<python>`) `tools/3ds/smdh_names.py` writes it whole. devkitPro's MSYS2
   shell has no Python of its own: pass a Windows one, for example
   `-DTSSM_PYTHON=/c/Python314/python.exe`, on the first run (or delete the build folder
   first; see step 5).

On Windows, run all of it from devkitPro's MSYS2 shell; its CMake wants MSYS paths
(`/e/...`, not `E:/...`). GCC writes large temporary files to `TMP`/`TEMP`: point them at
a drive with room if the system drive is short of space.

`build_port.sh` says why it passes what it passes. The CMake side is the `TSSM_3DS` branch
of `CMakeLists.txt` (`CMAKE_SYSTEM_NAME` `Nintendo3DS`, from devkitPro's `3DS.cmake`):
SDL 3 from the prefix, the citro3d renderer (`TSSM_RENDER_BACKENDS=C3D`), the settings
menu on the bottom screen (`TSSM_GUI`), FFmpeg's movie decoder when the prefix has it, no
host tools or unit tests. The .3dsx carries a RomFS with the libraries' licences only, an
SMDH with the name "The SpongeBob SquarePants Movie Videogame" (`TSSM_3DS_TITLE` replaces
it), the description "Homebrew port <version>" (`TSSM_3DS_VERSION`), the author "aidp2"
(`TSSM_3DS_AUTHOR`) and a 48x48
icon: the one made from your own game files with `TSSM_3DS_ICON` (see *An icon from your
own game files*), or else `TSSM_ICON_DIR`'s `icon-48.png` when one was made
(`docs/ICON.md`), otherwise libctru's default.

### The .cia

`tssm.cia` is the same `tssm.elf` and RomFS as the .3dsx, packed by makerom with
`tools/3ds/tssm.rsf`, an SMDH made by bannertool (name and description "The SpongeBob
SquarePants Movie Videogame", publisher "aidp2" (`TSSM_3DS_AUTHOR`), region free, New 3DS
only, no system save data; `tools/3ds/smdh_names.py` then writes the whole name, which
bannertool cuts at 32 characters) and a banner. The banner picture, the icon and the
banner's sound are the port's own, made by `tools/3ds/draw_cia_art.ps1` (a sponge block
and the name) and `tools/3ds/make_chime.py` (a two-note chime) into `dist/3ds/cia_*`;
nothing of the game's or anyone's logo goes in, and `tools/check_icon_art.py` knows the
pictures. A banner and an icon made from your own game files can replace the plain ones:
see *An animated HOME Menu banner from your own game files* and *An icon from your own
game files*.

| RSF setting | Value | Why |
|---|---|---|
| UniqueId | `0xF7553` (title ID `000400000F755300`) | in the 0xF0000-0xFFFFF range homebrew uses; the same for every build, so a new one installs over the old |
| ProductCode | `CTR-H-TSSM` | the usual homebrew form |
| Logo | `Homebrew`, in the ExeFS (`-exefslogo`) | makerom's built-in community splash, nothing of the game's. The HOME Menu reads a title's logo when it starts it; a .cia with none stops there with an error screen |
| SystemModeExt | `124MB` | the New 3DS mode the Homebrew Launcher gives a .3dsx: the same heap and linear heap |
| SystemMode | `64MB` | an old 3DS gets only as far as the "not supported" screen |
| CpuSpeed, EnableL2Cache | `804MHz`, on | what the .3dsx asks for at start |
| ReleaseKernel | 2.44 | puts the linear heap where the .3dsx has it |
| StackSize | `0x3000` | only the start-up's: libctru moves the main thread to its 1 MB stack |
| MaxCpu, CanAccessCore2 | `0x9E`, on | core 2 for SDL's audio thread on a New 3DS; the system core's 30% if core 2 is refused |
| FileSystemAccess | DirectSdmc, DirectSdmcWrite | everything is in `sdmc:/3ds/tssm`, as for the .3dsx |
| SaveDataSize | 0 | saves are files on the SD card |
| ServiceAccessControl | APT:U, fs:USER, hid:USER, ir:rst, gsp::Gpu, dsp::DSP, soc:U, cfg:u, ptm:sysm | what the program uses |
| EnableCrypt | off | homebrew CIAs are not encrypted; Luma3DS runs them |

At run time only where closing goes differs: libctru's exit returns an installed title to
the HOME Menu and a .3dsx to the Homebrew Launcher. The log's start says which it is, and
the settings menu's Quit names that menu. makerom v0.19.0 stores the title version 0.1.0
as 0.0.0; the SMDH's description carries the real one.

### Building the TSSM Dehydrated release

The prebuilt `tssm.3dsx` and `tssm.cia` on this repository's Releases page, *TSSM
Dehydrated*, are this build with five settings: the name "TSSM Dehydrated"
(`TSSM_3DS_TITLE`: the SMDH's short and long title in the Homebrew Launcher and the HOME
Menu, the About tab's heading and the game-files screen), the release's version
(`TSSM_3DS_VERSION`, 0.9.1 for TSSM Dehydrated 0.9.1), author "aidp2", the port's own
icon for both files (`dist/3ds/cia_icon.png`) and the banner picture
`dist/3ds/cia_banner_dehydrated.png` with the port's chime (`TSSM_3DS_BANNER` takes a
256x128 PNG as well as a .bnr). `tools/3ds/draw_cia_art.ps1 -OutDir dist\3ds -Subtitle
Dehydrated -BannerFile cia_banner_dehydrated.png -BannerOnly` draws that picture. Nothing
of the game is in either file: the RomFS holds the licence texts only. The .cia keeps the
title ID `000400000F755300`, so it installs over an earlier build's .cia (the settings and
saves are files on the SD card and stay).

The program keeps source file names (in its assert messages; `build_ffmpeg.sh` maps
FFmpeg's folders out itself and names them by their role in FFmpeg's configure line), so
build in a work folder whose path has no spaces and nothing private in it, from a
checkout of this branch inside it. In devkitPro's MSYS2 shell, with makerom and
bannertool on the `PATH` and FFmpeg n9.0.2's source extracted into the work folder:

    R=/c/tssm-rel/3ds     # the work folder
    S=$R/src              # this branch, checked out with its submodules (Building, step 2)
    cd $S
    MAP="-ffile-prefix-map=$(cygpath -m $R)/= -ffile-prefix-map=$R/="
    CFLAGS="$MAP" CXXFLAGS="$MAP" sh tools/3ds/build_sdl.sh third_party/SDL $R/sdl $R/prefix
    sh tools/3ds/build_ffmpeg.sh $R/FFmpeg-n9.0.2 $R/ffmpeg $R/prefix
    CFLAGS="$MAP" CXXFLAGS="$MAP" sh tools/3ds/build_port.sh $R/build $R/prefix \
      -DTSSM_PYTHON=/c/Python314/python.exe \
      "-DTSSM_3DS_TITLE=TSSM Dehydrated" -DTSSM_3DS_VERSION=0.9.1 -DTSSM_3DS_AUTHOR=aidp2 \
      -DTSSM_3DS_ICON=$S/dist/3ds/cia_icon.png \
      -DTSSM_3DS_BANNER=$S/dist/3ds/cia_banner_dehydrated.png

`-DTSSM_PYTHON` names any Python 3 (step 6). On Linux or macOS, `MAP` is
`-ffile-prefix-map=$R/=`. The files are `$R/build/tssm.3dsx` and `$R/build/tssm.cia`;
keep `$R/build/tssm.elf` for crash reports. Before sharing them, look for your own folder
and user names in both files, and in the .cia's program, which makerom compresses: unpack
it with ctrtool (Project_CTR, tested v1.3.0). `x/exefs/icon.bin` is the SMDH (names,
publisher, icon), and the RomFS holds the `licenses/` texts only:

    cd $R/build && mkdir -p x
    ctrtool --contents=x/c tssm.cia
    ctrtool -t ncch --exefsdir=x/exefs --romfsdir=x/romfs x/c.0000.*
    arm-none-eabi-strings -a tssm.3dsx x/exefs/code.bin | grep -i -e <your user name> -e Users
    find x/romfs -type f

### An animated HOME Menu banner from your own game files

The `.cia` built above has a plain banner: the port's own sponge picture and a short chime,
because nothing from the game can be in this repository. If you want the HOME Menu to show
the game instead, `tools/3ds/banner_from_game_files.py` makes a banner from **your own copy**
of the game files: the Patty Wagon with SpongeBob and Patrick, moving in 3D on the top
screen, with the first clip of the Extras menu's Macho Sound Pack (trimmed to under 3
seconds). It also makes a flat version, a still picture of the car with the same sound, to
use if the 3D one does not show. The icon is made separately (next section).

You need:

- **Python 3.8 or later** (`python3`; on Windows `python` or `py`) for the script.
- **pycgfx** (MIT licence, <https://github.com/skyfloogle/pycgfx>, tested at commit
  6c2e0cd), which turns the model into the HOME Menu's 3D banner format, with its two
  Python packages, gltflib (MIT) and Pillow (MIT-CMU), in a venv of **Python 3.10 or
  later** (pycgfx does not run on older ones). The script runs it through
  `tools/3ds/pycgfx_aligned.py`, which changes only where pycgfx puts the textures and
  lookup tables in the file: on 128-byte boundaries (pycgfx puts them on 16). A HOME Menu
  froze, each time the title was selected, on a banner whose textures were 32 bytes past
  such a boundary, and showed the same banner properly with them on it. pycgfx's own files
  are not changed.
- **bannertool** (1.2.0, the diasurgical fork; GPL) and **makerom** (Project_CTR; MIT), the
  same two tools the `.cia` already needs (step 6 above).

None of these are in this repository; you fetch them yourself. The script was made and
tested with the **US** Xbox game files (it reads `MN/mnui_US.hip`); with another
language's files it stops with "not found in the game files".

The steps, run from this repository's folder:

1. Get pycgfx and its packages (once):

       git clone https://github.com/skyfloogle/pycgfx <pycgfx>
       git -C <pycgfx> checkout 6c2e0cd
       python3 -m venv <pycgfx>/venv
       <pycgfx>/venv/bin/pip install gltflib pillow

   On Windows the venv's programs are `<pycgfx>\venv\Scripts\pip` and
   `<pycgfx>\venv\Scripts\python.exe`. Check that the venv's Python is 3.10 or later
   (`python3 --version`).
2. Make the banner from your game files (the folder with `BB/`, `MN/`, `PL/` ... that you
   copy to the SD card's `3ds/tssm/files`) into a folder **outside** this repository (the
   script refuses a folder inside one, so the game's files cannot end up in a commit):

       python3 tools/3ds/banner_from_game_files.py <game files> <banner folder> \
           --pycgfx <pycgfx> --pycgfx-python <pycgfx>/venv/bin/python \
           --bannertool <bannertool>

   `<bannertool>` is its path, or just `bannertool` if it is on the `PATH`; the same goes
   for the Python after `--pycgfx-python`. The script checks both before it starts.
   It takes under a minute. The folder then holds `banner.bnr` (the 3D banner),
   `banner_flat.bnr` (the flat one), `banner_preview_0.png` to `_3.png` (how the 3D banner
   will look, the HOME Menu's bars darkened), `banner_report.txt` and the pieces they were
   made from.
3. Build the `.cia` with it: add `-DTSSM_3DS_BANNER=<banner folder>/banner.bnr` when the
   build folder is first made,

       sh tools/3ds/build_port.sh <build dir> <prefix> -DTSSM_3DS_BANNER=<banner folder>/banner.bnr

   or, for a build folder that already exists,
   `cmake -DTSSM_3DS_BANNER=<banner folder>/banner.bnr <build dir>` and build again. The
   build says `3DS: tssm.cia's banner: ...` and puts that banner in `tssm.cia` instead of
   the plain one. Use `banner_flat.bnr` the same way for the flat banner, and
   `-DTSSM_3DS_BANNER=` (empty) to go back to the plain banner. Without the option nothing
   changes. If you run makerom yourself, give it `-banner <banner folder>/banner.bnr`.

   Keep `<build dir>` outside this repository's folder, or under its `build/` folder: the
   build copies the banner into it, and the `.cia` holds it. (`.gitignore` also ignores
   `.cia`, `.3dsx`, `.bnr`, `.cgfx`, `.icn` and `.smdh` files anywhere, but a build folder
   elsewhere in the clone would still hold the game's files.) On Windows, step 2 runs in a
   Windows Python with Windows paths (devkitPro's shell has no Python), while this step runs
   in devkitPro's shell, whose CMake wants MSYS paths (see *Building*): write the banner's
   path as `/c/...`, not `C:\...`.

**Keep it to yourself.** The banner folder, the `.bnr` files and any `.cia` built with them
contain the game's model, textures and voice from your copy: do not share or upload them.

The script keeps the 3D banner to what the HOME Menu loads: the model file at most 512 KB,
its textures and lookup tables on 128-byte boundaries (it checks the file pycgfx made and
stops before making the `.bnr` if one is not), the sound stereo and at most 3 seconds.

**Tried on a console:** the car driving (`--anim patty_wagon_drive.anm --fit`, below)
shows properly on a New 3DS XL's HOME Menu. **Not yet tried on a console:** the default 3D
banner (about 800 KB) is bigger than any banner a console has shown so far (about 700 KB).
If the HOME Menu shows no banner for the game, or its sound is garbled, build with
`banner_flat.bnr` instead.

Options:

- `--anim`: which of the car's animations it plays. The default is the boost
  (`patty_wagon_fast.anm`); `--anim patty_wagon_drive.anm` is the car driving and
  `--anim patty_wagon_idle.anm` the car standing. Add `--fit` with these two, because the
  default size and position are set for the boost.
- `--fit`: works out the size and position that keep the whole animation between the HOME
  Menu's top and bottom bars, centred, and prints them (`--top-margin` and
  `--bottom-margin` set how much room is left; `--scale` and `--offset` set them by hand).
- `--no-seam-fix` leaves out the small overlaps that hide the joins between the model's
  moving parts (smaller, but thin cracks can show as it moves); `--no-preview` skips the
  pictures; `--skinned` is for experiments only (that kind of banner has been reported to
  freeze the HOME Menu).
- `--two-sided-sheets off` leaves out the back sides the script gives the car's thin
  single-layer pieces: the propeller, the flag's cloth, the windshield and a small flat
  piece under the front. The game shows these from both sides, but the banner hides the
  back of every face, so without a back side of their own they vanish when they turn away
  (the flag seen from the car's right, the propeller from behind). With `off` the car is
  made as earlier versions of this script made it, without those back sides.

### An icon from your own game files

The `.cia`'s icon is the port's plain sponge block, and the `.3dsx` has libctru's default
icon, because nothing from the game can be in this repository.
`tools/3ds/icon_from_game_files.py` makes the icon from **your own copy** of the game
instead, from one of the two small pictures the Xbox dashboard showed for the game, which
its `DEFAULT.XBE` carries:

- the **title image** (the default): the movie's logo in a life ring. The script fits the
  icon to the ring and the logo, leaving out most of the dark green around them
  (`--whole` keeps the whole picture);
- the **save image** (`--image save`): a Krabby Patty.

It needs Python 3.8 or later and nothing else, and like the banner script it will not
write into a folder inside a git repository.

1. Make the icon. `<game files>` is the folder with `DEFAULT.XBE` in it, the file at the
   top of the Xbox disc next to `boot.HIP` (the SD card does not need it, so if your copy
   of the files lacks it, copy it from the disc, or give the path of `DEFAULT.XBE` itself):

       python3 tools/3ds/icon_from_game_files.py <game files> <icon folder>

   or, for the Krabby Patty,

       python3 tools/3ds/icon_from_game_files.py <game files> <icon folder> --image save

   The folder then holds `icon-48.png` (the icon), `icon-24.png` (the small icon the
   HOME Menu uses in some places) and `icon-full.png` (the picture as the game has it).
2. Build with it: add `-DTSSM_3DS_ICON=<icon folder>` when the build folder is first made,

       sh tools/3ds/build_port.sh <build dir> <prefix> -DTSSM_3DS_ICON=<icon folder>

   or, for a build folder that already exists,
   `cmake -DTSSM_3DS_ICON=<icon folder> <build dir>` and build again (on Windows, an MSYS
   path: `/c/...`). Both the `.3dsx` and the `.cia` get the icon; the build checks the
   pictures' sizes and says `3DS: icon ...`. It works together with `-DTSSM_3DS_BANNER`.
   The option also takes a single 48x48 PNG picture instead of the folder.
   `-DTSSM_3DS_ICON=` (empty) goes back to the usual icons; without the option nothing
   changes.

**Keep it to yourself.** The icon folder and any `.3dsx` or `.cia` built with it contain
the game's art from your copy: do not share or upload them, and keep the build folder
outside this repository's folder (or under its `build/` folder).

## Developer tools

None of these are needed to play or to build the game; they are for working on the port.

### Test settings (`test-env.txt`)

The console has no environment variables, so the test switches the PC build reads from
its environment (`src/SB/Core/pc/README.md`) are read on the 3DS from
`/3ds/tssm/test-env.txt`, one `NAME=VALUE` a line, for **one launch only**: the game
renames the file `test-env.last.txt` as soon as it has read it, so the next launch starts
as the player set it up, and `config.ini` is never changed by a test. For example:

    TSSM_BOOT=BB02
    TSSM_INTRO_MOVIES=off
    TSSM_EXIT_AFTER=1800

| Switch | What |
|---|---|
| `TSSM_BOOT=<level>`, `TSSM_INTRO_MOVIES=on/off` | start in a level, skip the logo movies (`TSSM_BOOT=` empty starts at the menu) |
| `TSSM_EXIT_AFTER=<frames>`, `TSSM_TEST_CLOSE=<frame>` | quit after that many frames; close at a frame as HOME's Close does |
| `TSSM_INPUT`, `TSSM_CAPTURE_FRAMES`, `TSSM_CAPTURE_DIR`, `TSSM_FIXED_STEP=30` | the PC's scripted input, frame captures (the `captures/` folder must exist) and a fixed time step, so frame N of a 3DS run and a PC run show the same moment |
| `TSSM_3DS_LOG_SERVER=on` | serve `last_run.log` on TCP port 28772 while the game runs |
| `TSSM_SND_METER=1` | a line a second with the level of each sound source |
| `TSSM_TEST_AUDIO_RESTART=f<frame>*<count>/<gap ms>@home~<ms>` (also `@sleep`, `@cancel`, `@stuck`) | stand-ins for an emulator, which has no sleep and no HOME: HOME, a sleep, HOME ended by the system's cancel of the DSP, or a stuck sound channel, `ms` long, `count` times from that frame (for example `f400*34/3000@home~1200`; `src/SB/Core/pc/iAudioSDLTest.h`) |
| `TSSM_C3D_STATS=1` | a line a second of renderer counts (draws, memory, VRAM) and a `frame times` line (CPU, GPU and waits); `TSSM_C3D_STATS=<n>` writes them every n seconds |
| `TSSM_C3D_STEREO=off/auto/always/force[:<depth>]`, `TSSM_C3D_STEREO_GUARD=on/off` | 3D for one run, over `[video] stereo`: `force` takes the slider as at its top, with the speed guard off unless `TSSM_C3D_STEREO_GUARD=on` |
| `TSSM_C3D_PIPE=off`, `TSSM_C3D_FAST=off`, `TSSM_C3D_REFRESH=off`, `TSSM_C3D_SKINTRIM=off`, `TSSM_C3D_EYESKIP=off`, `TSSM_C3D_SAVEVRAM=off`, `TSSM_AUDIO_CORE=1` | the speed work left out, for measuring: frame pipelining, the per-draw caches, changed vertices rewritten in place, the shorter skinning path, the right eye's skipped shadow pass, the 3D starting picture's copy in VRAM (*The renderer*, below); SDL's audio thread left on core 1 |
| `TSSM_C3D_FOG`, `_RTT`, `_SHADOW`, `_GLOW`, `_WARP`, `_COPY`, `_ORTHOZ`, `_HUDZ`, `_SELFTEST` (`on`/`off`) | one effect on or off for a run, over `[3ds] effects` |
| `TSSM_C3D_SKIN=gpu/cpu/bind/off`, `TSSM_C3D_LIGHT=off`, `TSSM_C3D_ENV=off` | skinning on the GPU (default) or the CPU; lighting and environment maps off, for measuring |
| `TSSM_C3D_AB=<frame>:<settings>,...` | switch those modes at given frames of one run (settings joined by `+`, for example `400:skin=cpu+fog=off`); each switch logs the stretch that ended (frames, fps, frame times) |
| `TSSM_C3D_BENCH=1` | a draw micro-benchmark at the 30th frame (`=2` adds its variants: what each part of a draw costs) |
| `TSSM_3DS_GUI=off`, `TSSM_3DS_COMBO_MS=<ms>`, `TSSM_3DS_GUI_EVERY=2` | the settings menu off for a run; the START/SELECT hold-back; build the menu every other frame |

### The speed test (`tssm-spike.3dsx`)

`-DTSSM_3DS_SPIKE=ON`, in a build directory of its own, builds `tssm-spike.3dsx` instead
of the game: the same game with nothing drawn (librw's NULL renderer), which measures the
console and then plays four levels by itself with a scripted controller, timing the
game's own work in every frame (`src/SB/Core/pc/iSpike.cpp`, `iD0Stats.h`). It answers
whether the New 3DS is fast enough for the game logic (the rule it applies: more than
about 25-30 ms a frame rules out 30 fps). It needs `/3ds/tssm/files/` with the levels it
plays (BB02, TR01, GG02, JK01 by default, plus the boot, font, menu and player files;
copying the whole game folder works), runs for about ten minutes, shows GO, BORDERLINE or
NO-GO on the top screen and writes `/3ds/tssm/spike-report.txt`, which it also sends to
whoever connects to TCP port 28772 while its last screen is up. `/3ds/tssm/spike.ini`
(optional) changes the run: `levels = BB02,TR01`, `seconds = 60`, `sdmb = 64`,
`volume = 50`, `movie = Story1.bik`. Timings in an emulator mean nothing.

### Console probes (`TSSM_3DS_PROBE`)

A probe is a build of the game that tests one thing on a console by itself and logs it:
`-DTSSM_3DS_PROBE=<n>` at configure time (1-11 or 14-16; 0, the default, is the game). Only
`3ds/i3DS.cpp` differs, so the probes share one build directory. A probe sets its own
effect switches (neither `config.ini` nor `test-env.txt` changes them), boots BB02 with no
intro movies, plays by itself and quits at frame 1500, logs to
`/3ds/tssm/probe-<n>-<name>.log`, saves into `/3ds/tssm/probe-saves`, and names itself on
the bottom screen.

| n | Name | Tests |
|---|---|---|
| 1 | `rtt` | render to texture and the start-up self-test's read-back |
| 2 | `overlay` | the settings menu, opened at frame 300 and drawn every frame |
| 3 | `fog` | BB02's fog |
| 4 | `shadow` | BB02's shadows |
| 5 | `copy` | screen copies (the self-test's copy steps) |
| 6 | `glow` | the HDR glow (the police car's siren, frames 150-250) |
| 7 | `warp` | a heat warp at frame 1000 |
| 8 | `orthoz` | the HUD's models under a parallel camera |
| 9 | `exitloop` | quits and starts itself again up to 20 times, checking each time that the console freed the run before |
| 10 | `fps` | BB02, then GG02, with every effect on, each measured over 900 frames |
| 11 | `leakladder` | like 9, one more layer of the program per run (from bare libctru up to the full game), to find the first layer whose quit is not freed |
| 14 | `speed` | the speed survey: ten runs (BB02's busy and calm spots, GG02, TR01, JK01, BB01, the opening fly-through, the title screen, the pause menu) with every effect on, each frame's time split part by part |
| 15 | `speed` | probe 14's ten runs again, then three that turn each speed change (*The renderer*, below) off and on |
| 16 | `speed` | probe 15's runs, then three in 3D (BB02, GG02, BB01) that turn each part of the 3D work off and on |

Probes 9-11 and 14-16 restart themselves through Luma3DS's loader and keep their files in
a folder of their own under `/3ds/tssm/` (`exitloop/`, `fps-effects/`, `leakladder/`,
`speed14/`, `speed15/`, `speed16/`) with a summary log beside it (`exitloop.log`,
`fps-effects.log`, `leakladder.log`, `speed14.log` ...). Each run quits through the game's
normal quit, and the next run's first line says whether the console freed it. Probes 14-16
take about 25, 35 and 50 minutes; in an emulator, which cannot restart them, a file
`only-run.txt` in their folder holding a run's number runs that one alone.
`tools/3ds/leakcontrol` builds `probe-11-control.3dsx`, a small plain-libctru program
(nothing of the port's) that makes the same start-up check, for comparison:

    sh tools/3ds/leakcontrol/build.sh <output folder>

## How it works

### Three compiler traps

- **`int32_t` is `long`.** devkitARM's newlib types `int32_t` and `uint32_t` as `long`,
  unlike every other target. The game's `S32`/`U32` (`include/types.h`) and librw's
  `int32`/`uint32` (`tools/librw/3ds_int32.patch`) are spelled out as `int` on the 3DS, as
  on the GameCube, so they stay the same types as RenderWare's `RwInt32`.
- **Short enums.** devkitARM sizes an enum to its values unless told otherwise; the
  GameCube build's enums are int-sized. The port, the game and librw are compiled with
  `-fno-short-enums`. libctru, SDL and FFmpeg keep devkitPro's short enums, so the files
  that include their headers (`3ds/i3DS.cpp`, the c3d backend, `iFMVDecoderFFmpeg.cpp`,
  `iModAudio.cpp`) are compiled their way. `3ds/iLayout3DS.cpp` and `3ds/c3d/c3dlayout.h`
  check at compile time and at start-up that both sides lay out the shared structs the
  same; the movie decoder also checks FFmpeg's struct layout and skips movies, with a log
  line, if it disagrees.
- **Dead stores before a constructor.** GCC treats whatever an object's storage held
  before its constructor ran as dead and deletes the stores (`-flifetime-dse`). The game
  zeroes memory and then constructs an object into it, counting on the zeros
  (`zLoadingScreen::Init`); the 3DS build compiles C++ with `-fno-lifetime-dse`.

### The platform layer

| File | What |
|---|---|
| `src/SB/Core/pc/iHost3DS.cpp` | the iHost seam (paths, files, time, diagnostics) |
| `src/SB/Core/pc/3ds/i3DS.cpp` | start-up (RomFS, 804 MHz, 3dslink, the log server, the New 3DS check), the memory report, HOME and sleep, the exception handler, the error screen, the quit, and the probes |
| `src/SB/Core/pc/iWindow3DS.cpp` | the window seam: SDL's video opened and pumped, frame pacing |
| `src/SB/Core/pc/3ds/iLayout3DS.cpp` | the compile-time layout checks |
| `src/SB/Core/pc/3ds/c3d/` | `rw::c3d`, librw's render device on citro3d (below) |

- **Memory.** 32-bit pointers, as in the Windows x86 build. The New 3DS mode gives about
  83 MB of heap and 32 MB of linear (GPU) memory, so the pools are smaller than on the PC:
  the game arena 28 MB (+2 MB pad; `TSSM_ARENA_MB`), the sound cache 6 MB
  (`TSSM_SNDCACHE`), the main thread's stack 1 MB.
- **Files.** Paths are `sdmc:/3ds/tssm/...`. The SD card's rename does not replace a file
  atomically, so saves are replaced through a `.bak` (put back at the next start if the
  power went in between). libctru's SD layer has no read cache, so every file the game
  reads gets a 128 KB buffer.
- **Sockets never wait.** libctru's socket calls share one session with the system, so a
  call that waits there holds up every other thread's; every socket is non-blocking.
- **HOME and sleep.** SDL's 3DS video driver sends no background or foreground events, so
  an APT hook stops the game clock while the HOME Menu is open or the console sleeps. Back
  from the HOME Menu the audio devices restart; after a sleep they stay open, as libctru
  stops and starts the DSP itself, and the mixer's one-second check restarts them if the
  sound does not come back. Sleep, wake, the DSP's own sleep and wake and each step of an
  audio restart are logged and synced to the SD card as they happen.
- **Sound.** SDL's 3DS audio (ndsp) with the port's own mixer. SDL asks for its audio
  thread on core 1 with a 30% time limit; the game makes it on core 2 of a New 3DS
  instead, which is the game's alone (on core 1 if core 2 is refused; `TSSM_AUDIO_CORE=1`
  keeps SDL's choice). The log's first audio line names the mixer's core. The game holds
  ndsp itself from start-up to the quit, so an audio restart closes and opens SDL's device
  only, never the DSP (ndsp's start and stop wait for the DSP with no time limit). If the
  system cancels the DSP's wake after HOME, the next audio restart lets go of ndsp and
  takes it again, so the DSP starts afresh. SDL's driver is patched (`tools/3ds/sdl/`):
  its waits wake every 50 ms, its close is bounded, and it resets its channel after such
  a cancel. `TSSM_3DS_NDSP_HOLD=off` leaves ndsp to SDL alone.
- **Closing.** Every `exit()` goes through one quit path (`-Wl,--wrap=exit`) that stops,
  one logged step at a time, everything that runs beside the main thread (the audio
  thread and ndsp, the log server, citro3d after a wait for the GPU, SDL, the sockets, the
  RomFS) before libctru frees the heaps, and gives no system service anything of the
  game's it could keep after the game has gone (the DSP and GPU cache flushes are made by
  the game itself). The log's start says how much memory earlier programs left held;
  with two or more held it asks for a restart.

### The renderer (citro3d)

librw draws through a device per platform; the 3DS's is `rw::c3d` in
`src/SB/Core/pc/3ds/c3d` (`rwc3d.h` is the part the rest of the port sees).
`tools/librw/c3d_platform.patch` adds only the hooks librw calls.

| File | What |
|---|---|
| `c3ddevice.cpp` | librw's `Device`: the camera (the screen is mounted sideways, so the picture is turned in the projection), clears, the present, render states, Im2D, Im3D, lights |
| `c3dsubmit.cpp` | citro3d itself: the frame, the top screen's target, the frame ring, deferred frees, programs, uniforms, draws, texture storage, read-backs, screen copies |
| `c3draster.cpp` | textures: the Xbox textures decoded, packed and tiled; movie frames |
| `c3drtt.cpp` | render to texture (the shadows, the glow), and the start-up self-test |
| `c3dpipe.cpp`, `c3dskin.cpp` | the object pipelines (default, UV transform, MatFX, skin) and skinning |
| `c3dstereo.cpp` | stereoscopic 3D: the slider, the eye shift (`rw/stereomath.h`), the point the eyes meet at, the speed guard (the right eye itself is sent in `c3dsubmit.cpp`) |
| `c3doverlay.cpp` | the settings menu on the bottom screen (Dear ImGui) |
| `c3dab.cpp`, `c3dbench.cpp` | the measuring switches and the micro-benchmark (*Test settings*) |
| `shaders/` | the vertex shaders (picasso) |

- **Textures.** The PICA has no DXT and no palettes, so each texture is decoded on the CPU
  as it loads, packed as RGB565, RGBA5551 or RGBA4, and kept at half size (the font atlases
  keep their size). Movie frames are kept at half size too.
- **Skinning** is in the vertex shader: four bones a vertex from a palette of 20; a model
  that uses more bones is split as it loads. `TSSM_C3D_SKIN=cpu` skins on the CPU instead,
  with the same result.
- **Fog** is computed per vertex, as in the PC's OpenGL renderer, and applied by a texture
  ramp and one combiner stage. **Shadows**, the **glow** and the **heat warp** are the
  game's and the PC port's own passes, drawn through camera textures and GX screen copies.
- **The HUD's models** (the treasure chest, the N2O icon) sit at nearly one depth under a
  parallel camera; on the console's 24-bit floats their faces would all get the same
  depth, so they get their depth from a narrow window near the camera (`TSSM_C3D_HUDZ`).
- **Limits** worth knowing when changing it: a vertex shader must write all four parts of
  every output it declares (a partial write locks the GPU up on a console, though not in
  an emulator), and picasso fails silently above 8 programs in one binary (there are 7).
  A frame that is never presented (while a level change is pending) is split into pieces
  sent to the GPU before the 2 MB command buffer fills.
- **Stereoscopic 3D.** The right eye is not recorded a second time: at the present, once
  the left eye's picture has gone to its framebuffer, the frame's command lists are sent
  to the GPU again with the other eye's shift (one uniform the vertex shaders add in clip
  space), into the same colour and depth buffers. So the CPU pays little for it and no
  second set of buffers is needed (a 3D frame that does not clear its colour keeps a 375
  KB copy of its starting picture for the right eye, in VRAM when there is room), while
  the GPU's work about doubles; the right eye leaves out the shadow texture's pass, which
  both eyes share. Only the game's main camera in play is shifted; the HUD, text, menus,
  movies and camera textures are the same in both eyes. A frame that cannot be sent again
  (a hard split or a read-back inside it, or no room left to send it again) is shown as
  one picture, and the log counts them. On the Windows build (OpenGL), `TSSM_TEST_EYE=L`
  or `R` with `TSSM_C3D_STEREO=force:<depth>:<distance>` draws one eye with the same
  shift, as a reference picture (`tools/librw/gl3_testeye.patch` adds the hook).
- **Speed.** Frame pipelining: when the GPU is still drawing the frame before, the next
  one is recorded into a second command buffer and frame ring, and its GPU work is queued
  as soon as the frame before is done, so a frame takes about the longer of the CPU's and
  the GPU's time instead of nearly their sum. Per-draw caches skip lighting and
  render-state work that did not change, and a model whose vertices change (face morphs,
  particles) has them rewritten in place instead of being made again. The skinned models'
  vertex shader takes a shorter path (the other three bones are skipped where a vertex has
  one). The sound mixer's loops work on a stretch of samples at a time, with the same
  output bit for bit. *Test settings* above has the switches that leave each out. In the
  code's comments and the logs, L2, L3 and L4 name three of these steps (flushing only
  what a frame wrote for the GPU, off by default and switched on only for measuring; the
  per-draw caches; frame pipelining), and speed2 and speed3 are the two rounds of this
  work that probes 15 and 16 measure (speed3: the right eye's shadow pass left out, the
  shorter skinning path, the starting picture's copy in VRAM).
- **Not drawn on the 3DS:** the PC's extras (MSAA, per-pixel lighting, shadow maps, toon
  shading, outlines, the Xbox glow and distortion), point and spot lights, and Im3D's
  lighting. Each is named once in the log.

## Known gaps

- **Played on a console mostly in scripted runs.** On a New 3DS XL the console probes
  14-16 (*Developer tools*) have played BB01, BB02, GG02, TR01 and JK01, the opening
  fly-through, the title screen and the pause menu with every effect on: 27 to 30 fps in 2D
  (the fly-through about 25). With 3D forced on at depth 50, BB02's busy street keeps about
  29 fps, GG02 about 21 and BB01 about 23; with the speed guard (Auto, in its earlier and
  quicker form), every place measured stayed at 27 to 30 fps, the guard playing stretches
  in 2D (most of the time in GG02 and BB01). Playing by hand, the settings menu, the crash
  handler and the error screen are still to be checked on a console.
- **The quit.** Earlier builds' quits could leave the game's process held by the system
  until the console restarted, and after three or four in one power-on the console could
  stop with a kernel panic at the next start. Since the quit was reworked (*The platform
  layer*, Closing), the probes 14-16 have made 39 launches on a console, each run quitting
  and starting the next, and none started with a program held. The game still reports
  held programs at start and asks for a restart when there are two or more.
- **The animated banner** (made from your own game files, above) has not been tried on a
  console in its default form; `banner_flat.bnr` is the fallback.
- **The icon made from your own game files** has been checked in the built `.3dsx` and
  `.cia` only, not yet on a console's HOME Menu or Homebrew Launcher.
- **Input:** the real Circle Pad's and C-Stick's reach and dead zone have been checked only
  in the emulator and the input test.
- **Time zone and file times:** the clock is taken as local time (unchecked), and an
  imported memory-card save keeps today's date (libctru has no `utimes`).
- **This branch started from an early copy of the Switch port** and still carries that
  copy (`docs/SWITCH.md`, `src/SB/Core/pc/switch`, `tools/switch`). For the Switch, use
  the `switch` branch.
- **Licences when you share a build:** the RomFS carries libctru's and citro3d's licences
  (`dist/3ds/licenses`, with citro3d's copyright line; libctru names no holder), and
  `libctru.txt` names newlib's and the GCC runtime's. newlib's notices are not in the
  RomFS: pass them on beside the files. FFmpeg is linked into the program statically; the
  LGPL allows that because anyone can rebuild the program from this source with a changed
  FFmpeg, so share the source of the FFmpeg you built with beside the files too. The
  releases do both (THIRD-PARTY-NOTICES.txt and FFmpeg's source tarball).
