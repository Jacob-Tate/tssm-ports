# Nintendo Switch (homebrew)

The port builds as a Switch homebrew program, `tssm.nro`, for consoles that run
custom firmware (Atmosphere). It has run on one console: the build of
2026-10-29, and the build of 2026-11-09, which played and imported saves but
froze on Quit (changed since; untested). The builds after that compile, link
and pack but wait for their own run on the console; "Stubs and gaps" below
says what is still unchecked.

Using custom firmware online can get a console banned. Play offline (airplane
mode, or an emuMMC setup), except for co-op, which needs Wi-Fi: read
"Keeping the console safe on Wi-Fi" below first.

This branch builds the co-op program, `tssm-coop.nro`: the same game with
home-network co-op, as a separate file beside `tssm.nro` (section "Co-op on
the home network").

## For players

You need:

1. a Switch running Atmosphere;
2. `tssm.nro`;
3. your own copy of the game's Xbox files (the same files the PC and Android
   builds use).

Copy them to the SD card:

| Path on the SD card | What |
|---|---|
| `/switch/tssm/tssm.nro` | the program |
| `/switch/tssm/assets/` | the game's Xbox files, laid out as the Android build's `assets` folder (the folder with `boot.HIP`, `FMV/`, the level folders) |

The program writes everything else into the same folder:

| Path | What |
|---|---|
| `/switch/tssm/config.ini` | settings, written with the defaults on the first run |
| `/switch/tssm/saves/` | saves (docs/SAVES.md) |
| `/switch/tssm/mods/` | mods (docs/MODDING.md) |
| `/switch/tssm/last_run.log` | the log of the last run (`last_run.prev.log`: the one before) |
| `/switch/tssm/crash.log` | crash reports |
| `/switch/tssm/coop-log/` | the co-op program's own `last_run.log`, `last_run.prev.log` and `crash.log` |

Nothing goes into the console's own save system: that belongs to the game used
to start the Homebrew Menu.

**Start it with full memory.** Hold R while you start any game from the HOME
menu (title takeover); the Homebrew Menu opens instead, then pick TSSM. Opened
from the Album, homebrew runs as a small applet with a fraction of the memory;
the game says so on screen and + goes back to the Homebrew Menu (X there tries
anyway, and will most likely run out of memory).

**Quitting.** **Quit game** in the settings window (below) returns to the
Homebrew Menu. Progress since the last save is lost: the game's own
save-on-exit does not run, as on PC. HOME, then Close software, also ends the
game, as for any title.

### The settings window

- **- and +** (hold -, press +) open and close it. Docked with no keyboard
  nothing else opens it, so there is no setting to turn it off. In handheld
  mode three fingers on the screen also open it, and so does `[gui] key` on a
  USB keyboard.
- **B** chooses and **A** goes back, by position as in the game (below);
  **L** and **R** change the tab; the d-pad or the left stick moves. In
  handheld mode a tap works as well.
- It fills the screen in both modes, and its text is larger than on a PC.
- What does nothing on the Switch is not shown: the window mode, the choice of
  renderer (it shows OpenGL 3), the Open with key (`[gui] key` stays in
  config.ini), buttons that would open a folder, a file or a web link, the
  keyboard column of the Controls tab. Nothing in it types text either: that
  would open the system keyboard.

If a level runs below about 45 frames a second on the console, the SpongeBall
still keeps its full speed: a change from the original game, which slows the
ball at those rates while the falling blocks of the SpongeBall levels do not
slow (`src/SB/Core/pc/PORTING.md`, known divergences). At 60 nothing changes.

### Controls

Joy-Cons (attached or both detached), a Pro Controller, or handheld mode. One
sideways Joy-Con has no right stick, so no camera.

The face buttons follow the Xbox layout the port plays with everywhere, **by
position** (decided 2026-10-29), and everything on screen names them by the
letter **printed** on the Switch:

| Switch button (position) | Does what the Xbox's ... does | Shown as |
|---|---|---|
| B (bottom) | A: jump, accept | B |
| A (right) | B: SpongeBowl, Cartwheel, back | A |
| Y (left) | X: Karate Spin, Star Spin | Y |
| X (top) | Y: Bash, swing | X |
| ZL, ZR | LT, RT | ZL, ZR |
| L, R | white, black | L, R |
| -, + | Back, Start | -, + |

So a prompt that says "B Choose" means the bottom button, and "A Return" the
right one. Where the labels come from (`iPadSwitchLabels`,
`src/SB/Core/pc/iPadTokens.h`):

- **The game's prompts** draw the disc's `pad_button*` pictures, which are the
  Xbox's coloured A, B, X and Y. The Switch set replaces them with pictures
  drawn in code (`iPadPrompts.cpp`; no art is shipped): a dark grey round
  button with the printed letter in white, and dark pills for ZL, ZR, L, R, -
  and +, the way the Switch's own buttons look. Of the three ways to show the
  right letter this is the most legible and the least surprising:
  - swapping which Xbox picture is shown would put the right letter on the
    wrong colour: the red B (the Xbox's "back") on the button that jumps and
    accepts, the green A on the one that goes back;
  - tinting the disc's pictures grey keeps the Xbox's letter, which is baked
    into each picture, so it still needs the swap, and leaves an Xbox-shaped
    letter on a dimmed, low-contrast button;
  - the drawn set is the one the PlayStation, GameCube and keyboard pictures
    already use, so it is drawn the same way on every backend, its white
    letter on dark grey reads at the prompts' small size, and a rebound
    button still shows the button that presses it.
- **Sentences that spell a button out**: "Press the START button" and the
  controller-missing message say +; "Pull" says "Press" (ZR clicks); TR02's
  Sonic Wave race, which kept the PlayStation 2's "Press X ... press L1", says
  B and ZR. They are rewritten in memory, never in the files.
- **The settings window**: the Controls tab names each game button by the Switch
  button in its place (the row config.ini calls `a` is "B"), and the move
  layout reads "Karate Spin on Y"; the built-in file browser's hints, the
  settings combo ("- and +"), the input test's drawing and the on-screen
  touch controls (`input.touch_controls = on`; grey B/A/Y/X, ZL, ZR, R, -
  and +) say the same.
- **The applet-mode screen** (`switch/iSwitch.cpp`) reads libnx's buttons,
  which are named by the printed letter, so its "Press X to try anyway" is the
  top button.

Nintendo's own convention (A on the right accepts) is one constant away
(`kSwitchPrintedLetters` in `src/SB/Core/pc/iPadHostSDL.cpp`); the labels
follow either way. The PC and Android builds use Nintendo's convention for a
Switch Pro Controller plugged into them (bindings name the printed letter, so
A on the right jumps; `tests/input_test.cpp`), and keep their own prompts.
`[pad]` in config.ini rebinds anything (docs/CONTROLS.md); its names are the
Xbox's (`a` is the bottom button).

To see the Switch's labels on a PC, start it with `TSSM_PAD_LABELS=switch`
(PC only): the game's prompts and the overlay's labels become the Switch's,
with or without a controller, and nothing else changes (the bindings and the
controller type stay the PC's).

HOME and Capture belong to the system. The game's clock stops while the HOME
menu is open, while a system applet is in front and while the console sleeps.

## Co-op on the home network (tssm-coop.nro)

`tssm-coop.nro` is the co-op version of the game: online co-op and co-op on
the home network (docs/LAN.txt), with a PC, a phone or another Switch. It is a
program of its own; `tssm.nro` stays the normal game, without co-op. In the
Homebrew Menu it is "TSSM co-op (port)", with a red CO-OP band on its icon,
and its version ends in the co-op version code (`0.9-021cbdd5`, say): the
Co-op tab shows the same eight letters and digits, and every device in a game
needs the same ones.

**Where it goes:** `/switch/tssm-coop.nro`, beside `tssm.nro`. It uses the
normal game's folder, `/switch/tssm/`: the same game files, the same saves and
the same `config.ini`. So progress made in either program is there in the
other. What is its own:

- its log, in `/switch/tssm/coop-log/`, so that starting one program never
  replaces the other's last log, and a crash report goes with the right
  program;
- the `[coop]` lines in `config.ini` (your name, the last host's address, the
  tab's switches). The normal game leaves lines it does not know as they are
  when it saves its settings, so the two share the file. It mentions each such
  line once at start in its own log ("unknown setting ... ignored");
- `coop_files.cache` in the saves folder: the co-op program's record of the
  game files it has checked, so the check is quick after the first time.

Only one of the two may be open at a time. A program suspended in title mode
and another started from the Album could otherwise share the folder, so each
holds the save folder's one-copy claim (a lock file held open, "One copy at a
time" below) while it runs, and the second says the game is already open and
closes. Saves are written the same
way in both: a new file, then the old one replaced, backups kept.

**Starting it:** as the game: title takeover (hold R while starting a game),
then "TSSM co-op (port)".

**Playing:** open the settings window (hold -, press +), Co-op tab.

- **Joining:** the list "Games on this network" shows the games other devices
  host. Pick one with the controller or a tap, then enter the host's code on
  the **code pad**: 32 keys, moved over with the stick or the D-pad and
  pressed with the bottom button, or tapped. The console's keyboard opens for
  the code field as well. "Join by address" stays below, for the internet or
  when the list stays empty. With Wi-Fi off, the list says so.
- **Hosting:** "Host on this network" shows a code, large, for the other player
  to type. "Copy" is not offered: the Switch has no clipboard.
- **HOME and sleep:** the game pauses, and its network stops with it. Back
  within about 10 seconds, the game goes on; longer, the session ends and a
  joining player joins again by themselves for a minute. After sleep the
  console's Wi-Fi takes a few seconds to come back; co-op makes its network
  connections again by itself.

### Keeping the console safe on Wi-Fi

Co-op needs the Switch on Wi-Fi. A Switch with custom firmware that connects to
Nintendo's servers can be banned from Nintendo's online services (the eShop,
online play, updates), for good. That is about the console, not the game:

- **The game contacts nothing but the other player's device.** Only numeric
  addresses on your home network (or the one you type to join over the
  internet); no names are looked up, nothing signs in, and nothing is sent to
  Nintendo or anyone else. The questions "is there a game here?" go only to
  your home network's broadcast address.
- **The console's own system software does** contact Nintendo whenever it can:
  update checks, error reports, news, a signed-in account. With Wi-Fi on, that
  happens in the background, whichever program runs.

So before turning Wi-Fi on for co-op, make sure the console cannot reach
Nintendo: run the game from an emuMMC (the custom-firmware system on the SD
card), never sign into a Nintendo Account or the eShop there, and block
Nintendo's servers on it (Atmosphere's DNS blocking in `/atmosphere/hosts/`,
or a blocking DNS server in the Internet settings), and check that the block
works. Airplane Mode (Flight Mode in some regions) alone does not help:
co-op needs Wi-Fi, and a Switch can turn Wi-Fi back on inside it. If you are
not sure the console is
protected, play co-op on a PC and a phone only.

## Building

Everything comes from devkitPro. The build runs in devkitPro's MSYS2 shell
(Windows) or any shell with `DEVKITPRO` set (Linux, macOS, the
`devkitpro/devkita64` Docker image).

1. **devkitPro** with the Switch packages:
   `pacman -S switch-dev switch-cmake switch-mesa switch-libdrm_nouveau switch-glad switch-pkg-config`
2. **SDL3**: devkitPro's SDL fork, branch `switch-sdl-3.4` (tested at
   `c329016c0e338ab4397ad930f83330c9fe058348`; upstream SDL's Switch port is
   only for licensed developers), built static with the local patches:

       git clone --depth 1 --branch switch-sdl-3.4 https://github.com/devkitPro/SDL.git <sdl>
       sh tools/switch/build_sdl.sh <sdl> <sdl build dir> <prefix>

   `build_sdl.sh` applies every `tools/switch/sdl/*.patch` not yet applied, so
   running it again on the same clone is how a new patch gets in. `0002` (touch
   events) came after the first builds: a prefix built before it has no touch.

3. **FFmpeg** for the movies: the same decoder-only LGPL configuration as
   Android, static, from FFmpeg's own source (tested: n9.0.2). Paths without
   spaces:

       sh tools/switch/build_ffmpeg.sh <ffmpeg source> <work dir> <prefix>

4. **The port**:

       sh tools/switch/build_port.sh <build dir> <prefix>

   This makes `<build dir>/tssm-coop.nro`, and `tssm-coop.elf` beside it
   (keep the two together: crash reports are offsets into that ELF).

`build_port.sh` says why it passes what it passes. The CMake side is the
`TSSM_SWITCH` branch of `CMakeLists.txt`: SDL3 from the prefix
(`TSSM_SDL=system`), librw's OpenGL 3 backend on Mesa through EGL, no Direct3D
or Vulkan, no host tools or unit tests. The .nro carries a RomFS with the
port's own files only (`dist/switch/romfs` and the libraries' licenses), the
NACP name "TSSM (port)" and a 256x256 icon: `TSSM_ICON_DIR`'s `icon.jpg` when
one was made (docs/ICON.md), otherwise `dist/switch/default_icon.jpg` (a plain
sponge block drawn by `tools/switch/draw_default_icon.ps1`). The version shown
is `TSSM_SWITCH_VERSION` (0.9). CMake caches it, so a build directory
configured with an older default keeps that one until it is configured again
or given `-DTSSM_SWITCH_VERSION=0.9`.

On this branch the .nro is the co-op program: `tssm-coop.nro`, the NACP name
"TSSM co-op (port)", the version `TSSM_SWITCH_VERSION` plus a dash and the
co-op version code (`tools/switch/coop_nacp.cmake` reads it from the
`coop_id.h` the build makes), and the icon `TSSM_ICON_DIR`'s `icon-coop.jpg`,
else `dist/switch/default_icon_coop.jpg`. `tools/switch/make_coop_icon.ps1`
makes either from the plain icon by adding the CO-OP band.

The FFmpeg in a .nro is linked statically. Its notice in the About tab says
so; the LGPL's terms for a static link (offering the object files for
relinking) have to be met before any release.

Mesa, libdrm_nouveau and libnx are linked statically too. devkitPro installs no
license files for them, so `dist/switch/licenses` holds their notes (MIT, MIT,
ISC), which the About tab and the RomFS's `licenses/` carry. Mesa's and
libdrm's notes point to the source for the copyright lines; the exact lines
replace those pointers before any release. newlib and the GCC runtime are in
the image as well; their notices are part of the same pre-release check.

## Testing on a console

Start the Homebrew Menu by title takeover, press Y (netloader), then on the PC:

    nxlink -s -a <console IP> tssm.nro

With `-s` the game's output streams back to the PC as it runs, as well as into
`last_run.log`. Arguments after `--` reach the game's command line.

A crash writes the registers and the call chain to `crash.log` (and the log) as
offsets into the image:

    tssm:   #3  [tssm.elf+0x2a5f10]

    aarch64-none-elf-addr2line -f -C -e tssm.elf 0x2a5f10

Atmosphere also writes its own report to `sd:/atmosphere/crash_reports/`.

The co-op program's log (`/switch/tssm/coop-log/last_run.log`) names the
partner, so it is served on TCP 28772 only to a test: when the program was
sent with nxlink, or with the co-op test kit armed (below). Started from the
Homebrew Menu, nothing on the console listens for it.

### The co-op test kit

`--coop-test` on the command line
(`nxlink -a <console IP> tssm-coop.nro --coop-test`) runs a test with files of
its own, and only while `/switch/tssm/coop-test/armed` exists (an empty file;
delete it to disarm). Without that file the argument is ignored and the log
says so. Armed, the program takes from `/switch/tssm/coop-test/`:

| File | What |
|---|---|
| `config.ini` | the test's settings (`[coop] mode = host` or `join`, `address`, `name`, `[game] boot`); written with the defaults if missing |
| `saves/`, `mods/` | the test's saves and mods (none: the game files alone) |
| `password.txt` | the join code or password, first line; never on the command line, never in the log |
| `env.txt` | test settings, one `TSSM_NAME=value` a line (`TSSM_COOP_LOG=5` for the cost line, say); only names starting `TSSM_`; the folder's own five (config, saves, mods, log, password) cannot be changed here |
| `last_run.log`, `crash.log` | the test's log |

The player's own `config.ini`, saves and log are not read or written by a
test; the game files in `/switch/tssm/assets/` are only read.

## How it works

### Memory below 4 GB

The game keeps pointers in 32-bit fields, so the port's game arena (128 MB,
`iMemMgr.cpp`) and RenderWare's heap (`iLowHeap.cpp`) must sit below 4 GB. PC
and Android reserve them with `VirtualAlloc`/`mmap` hints. Horizon has no
`mmap`, and under Atmosphere's default 39-bit address-space layout for
homebrew the kernel places the heap, alias and stack regions itself, nowhere in
particular.

`switch/iSwitchLowMem.cpp` takes heap pages and maps them at a free address
below 4 GB that it picks itself, with `svcMapProcessCodeMemory` and then
`svcSetProcessMemoryPermission` (read/write): the calls hbloader makes for
every .nro's own data. When the stack region is below 4 GB (the 36-bit layout,
which `override_any_app_address_space=36_bit` in Atmosphere's
`override_config.ini` gives), `svcMapMemory` into it is the second choice. A
block that could not be put low comes back from the heap, and the game refuses
to start with a message naming the address. `tools/switch/memtest` is a
standalone .nro that tries these calls and prints what the console allows.

Switch memory is real from the moment it is mapped (on PC and Android an
untouched reservation costs nothing), so RenderWare's heap defaults to 64 MB
there instead of 256 MB (`TSSM_RWHEAP_MB` still overrides it).

### The platform layer

| File | What |
|---|---|
| `src/SB/Core/pc/iHostSwitch.cpp` | the iHost seam (paths, files, time, threads, diagnostics) |
| `src/SB/Core/pc/switch/iSwitch.cpp` | start-up (RomFS, nxlink, the applet-mode screen, `TSSM_ASSETS`), the applet hook, the quit, the exception handler, the error applet |
| `src/SB/Core/pc/switch/iSwitchLowMem.cpp` | memory below 4 GB |

Notes:

- **Files.** The SD card's `rename` does not replace an existing file, which
  every save and config write relies on; `iHostRenameReplace` moves the old file
  to `.bak`, renames the new one in, then deletes the `.bak`. Paths are
  unprefixed (`/switch/tssm/...`), which libnx resolves against the SD card.
- **The log.** newlib has no `pipe()`, so the tee that copies stdout and stderr
  into `last_run.log` is a device in newlib's table for descriptors 1 and 2
  (`iLog.cpp`); it also writes to the nxlink socket. The PC can pull
  `last_run.log` from TCP 28772 only when nxlink started the game: started
  from the Homebrew Menu, nothing listens (the log says `log server off`).
- **One copy at a time.** A program in title mode can be suspended (HOME)
  while another runs from the Album's Homebrew Menu, so two could share
  `/switch/tssm`. The save folder's claim (`iHostClaimInstance`, and the
  log's) is a lock file in `/switch/tssm/tmp/` opened for writing and held
  until the exit (`iSwitchClaimFile`); `userAppExit` closes it before the
  Homebrew Menu takes the process over. Horizon is expected to refuse a second
  open for writing (`TargetLocked`, 2002-0007), and the second program then
  says the game is already open and closes; from the Album, where the error
  applet does not show, it goes back to the Homebrew Menu and its log says
  why. Untested: only the console can show that the second open is refused.
  Any other result is logged and the game goes on as before.
- **Threads.** libnx's default thread stack is small; every thread made without
  a larger stack gets 2 MB (`pthread_create` is wrapped at link time). The main
  thread has hbloader's 1 MB.
- **Co-op's network thread** is the program's only `std::thread`. Its start is
  wrapped at link time too, and the thread moves itself to cores 1 and 2, off
  core 0, the game thread's: a join's key derivation and the first check of
  the game files would otherwise take the game's own core. The log says where
  it runs (`co-op's network thread: cores 1 and 2 ...`), or that the system
  refused and it stays where it started.
- **Sockets.** They come up at boot with 6 BSD service sessions instead of
  libnx's 3 (the log says how many it got; with 3 it still works). Co-op never
  blocks in a socket call: it polls without waiting and sleeps 2 ms between
  polls, so it holds no session while it waits. Co-op's addresses come from
  nifm (the console's one current network). The applet hook only raises flags
  for co-op (`iNetSock.h`): after sleep the network thread makes every co-op
  socket again, bound to the same port, and after the HOME menu or an applet
  it makes again those that report an error; a socket whose calls fail as dead
  is made again each second until it works.
- **Quitting.** hbloader runs the .nro inside its own process and, when it
  returns, loads the Homebrew Menu into the same memory, so nothing of the
  game may be left running or mapped. Every `exit()` goes through
  `iSwitchQuit` (`exit` is wrapped at link time): it stops the pull-log server
  (port 28772; polled with a stop flag, because a blocked `accept` can keep
  `socketExit` waiting for ever), the SDL audio thread, librw's GL context and
  SDL itself (window, EGL and with it Mesa's GPU session, controllers), then
  runs `exit()` as usual. Co-op stops right after the log server: a goodbye to
  the other player and its network thread joined, while the sockets are
  certainly up. libnx's `userAppExit` then closes the sockets and the
  RomFS, unmaps the low memory last and returns. Each step writes a
  `tssm: switch: quit:` line to `last_run.log` first, synced, so a quit that
  stops halfway shows where. A step that takes more than 10 seconds makes a
  watchdog write it to `crash.log` and end the process. An exit that did not
  come through the quit (the abort handler's `_exit`, `exit()` on another
  thread), or memory that would not unmap, ends the process instead of
  returning: the console goes to HOME (untested) rather than handing the
  Homebrew Menu memory the game still holds. HOME, then Close software, is the system
  ending the process, which needs none of this. Holding that exit
  (`appletLockExit`, not done) would bring the system's request in as an SDL
  quit, and the quit would then have to end the process instead of returning.
- **Touch.** SDL's Switch touch driver sent no finger-down or finger-up events;
  `tools/switch/sdl/0002-switch-touch-events.patch` fixes it. The port turns on
  SDL's touch-to-mouse there, so a tap is a click in the settings window; the
  game itself ignores that mouse.
- **HOME and sleep.** SDL's Switch video driver sends no background or
  foreground events, so an applet hook stands in for them: the game clock stops
  (`iTimeSuspend`) and on return starts again and the audio devices restart
  (`iAudioSDLRestart`), as Android's foreground does. Like Android's, that
  restart replaces one still pending and is not held back, so a device that
  stalled before HOME cannot keep the sound off for up to 30 seconds after it.
- **Graphics.** librw's GL3 device on Mesa (switch-mesa 20.1, nouveau) through
  SDL3's EGL: desktop OpenGL 3.3 core first, OpenGL ES 3.1 if that fails. The
  ImGui overlay uses librw's GL loader (glad).
- **The speed work (docs/RENDERING.md) on this driver.** Read from the code, not
  yet run. Vertex arrays, the stream buffers and the shadows before the frame
  use core OpenGL 3.3, nothing newer. The savings for phones ask first and step
  aside: `EXT_multisampled_render_to_texture` is only tried on OpenGL ES with
  `TSSM_GPU_MSRTT=1` (off by default since it lost depth on a phone) and
  nouveau does not offer it, so the frame takes the ordinary path; glad loads
  up to 3.3 here, so `glInvalidateFramebuffer` is not loaded and the discard is
  skipped; drawing the end of a frame single-sampled is for ES only. On the
  desktop context what stays on from the phone work is the glow reading the
  frame in place of a copy and the stencil cleared with the depth, both plain
  OpenGL.
- **Sound devices.** SDL's driver here is called `switch`, which is not on
  `iAudioSDL.cpp`'s list of drivers that move devices, so a removed device means
  a restart, as on Android. The system has one output and SDL reports no removal,
  so this path is not expected to run. HOME and sleep restart the devices anyway.
- **The frame pacer.** The cap, and the movies' ceiling of 60 (which the default
  cap of 60 already meets), work as on a PC: sleep to the deadline, with vsync
  on top. Android's pacing by the display alone is not turned on here; it may
  be worth trying if a console run shows a dropped frame at a steady interval.
- **The compiler.** GCC builds with `-fno-lifetime-dse` (`CMakeLists.txt`):
  it would otherwise treat what an object's memory held before its constructor
  as dead. On another GCC target it deleted `zLoadingScreen::Init`'s `memset`s. A build
  of this code with and without the flag, compared function by function
  (2026-11-06), showed nothing of that kind here: `zLoadingScreen::Init` came out
  the same, and no game function was found to have lost a store the game needs.
  The 208 functions that differ are destructors storing a vtable into a dying
  object, exception clean-up that never runs, code the linker can now no longer
  drop, stores merged differently, register and spill changes, and factory
  functions that wrote extra zeros into memory `xMemAlloc` had already zeroed.
  No damage was found in the builds before the flag; this comes from reading the
  differences, and not every library function's were read.
  `-fwrapv` and `-fno-delete-null-pointer-checks` are
  not used: clang makes the same assumptions on PC and Android, and neither
  covers the pointer subtraction fixed in `xParticleBatch.cpp`.
- **Dialogs.** SDL's Switch port has no file dialogs and no URL opening (its
  stubs answer "unsupported"), so the GUI's own file browser
  (`gui/iGuiFilePicker.cpp`, docs/GUI.md "The built-in file browser") answers
  every folder and file request instead: the launcher's **Browse...**, the
  Mods tab's folder, and the Saves tab's Import. It is driven by the
  controller (d-pad, the accept button opens or chooses, the back button
  returns or cancels, the top button goes up, L/R page), by touch on the
  handheld screen (tap, drag to scroll; with the touch patch above) or both.
- **Moving saves.** Saves to import go in `/switch/tssm/import/` (the game
  makes the folder; **Import...** opens the browser there): a Dolphin `.gci`,
  a `.gcs`, a memory card image (`.raw`, `.gcp`) or a `.zip` from **Every save
  as a .zip**. Exports go to `/switch/tssm/export/` without a question:
  **Export** beside a slot writes the `.gci` under the name Dolphin gives it,
  **Every save as a .zip** `tssm-saves-YYYYMMDD-HHMMSS.zip`, **Every save into
  a GCI folder** a new `gci-YYYYMMDD-HHMMSS/`; the tab's status line names the
  file. A save replaced by an import is kept as backup 1 of its slot, as on
  every platform. "Open the save folder" is not shown (nothing to open it
  with), nor are the Mods tab's "Open" buttons; it offers **Make the mods
  folder** while there is none.

## Stubs and gaps

What is known not to work or not to exist yet, with the place to change it:

- **One console so far.** The build of 2026-10-29 ran on a console (title
  takeover) with nothing wrong reported except save import, which had no
  dialog (fixed since by the built-in browser). The build of 2026-11-09 played
  and imported saves there, and froze on Quit. The quit, the touch fix and the
  settings window's Switch changes after it have not been on a console yet.
  Claims below marked untested are still untested.
- **Memory below 4 GB** (`iSwitchLowMem.cpp`) works on that one console:
  the kernel took the addresses we chose for `svcMapProcessCodeMemory`. The
  quit unmaps it (`iSwitchReleaseAllLow`), as hbloader unmaps each .nro's own
  read/write data; that the kernel allows it is untested. If it refuses, the
  log says `unmap failed` and the process ends instead of returning to the
  Homebrew Menu.
- **The quit** (Notes above) is untested on a console. A good one ends the log
  with `quit: exit(0): back to the Homebrew Menu`, `stopping the log server`,
  `stopping co-op`, `stopping audio`, `destroying the GL context`, `stopping SDL`, `exiting`,
  the `widen: at exit` report, `closing sockets and the RomFS`, `releasing
  the save folder's claim`, two `low memory ... released` and `returning to
  the Homebrew Menu`.
- **Touch** (the SDL patch, the settings window's taps and the three-finger
  toggle) is untested on a console.
- **Co-op** has not run on a console. Unchecked there: whether the console
  sends and receives the home network's broadcasts (if not, the list stays
  empty and joining by address still works); csrng (if it cannot start, the
  kernel's generator is used and the log says so); the six sessions; the
  network thread's core; the sockets after sleep; the keyboard and the code
  pad with Joy-Cons and touch; the cost of co-op on the console's CPU and the
  memory it takes inside the game's pools; that a save made in
  `tssm-coop.nro` loads in `tssm.nro`; that the second of two open programs
  is refused by the claim (One copy at a time, above).
- **The exception handler** is libnx's documented hook; that hbloader's process
  reaches it is untested.
- **Folder and file dialogs**: SDL has none; the built-in browser stands in
  (Notes above). Tested on a PC with `TSSM_FILE_PICKER=builtin` (controller
  presses, taps, drags and captures through `TSSM_GUI_SCRIPT`), not yet on a
  console. Its accept and back buttons are the overlay's, which ImGui takes
  by position (the bottom button, B, accepts; the right one, A, goes back), as
  the game does. There is no on-screen keyboard: nothing in the browser types
  a name.
- **Docked/handheld switch mid-game**: the window is always the screen's size
  (windowed, so SDL resizes it on a dock change; untested), the render size is
  chosen at boot from the display SDL reports, and the final blit scales; the
  render targets are not re-created. SDL makes a new window surface there, and
  librw now sets the swap interval only when it changes, so with `video.vsync =
  off` a dock change may turn vsync back on until the next start. Harmless.
- **The launcher** (`gui/iGuiLauncher.cpp`) shows only when the game files are
  not found (`[gui] launcher = always` counts as auto here), at the screen's
  size; it draws with SDL_Renderer on Mesa, which is untested, and its folder
  picker is the built-in browser (above).
- **Prompts not checked on a console**: the Switch labels were checked on a
  PC with `TSSM_PAD_LABELS=switch` (Controls above).
- **File times**: libnx has no `utimes`, so an imported memory-card save keeps
  today's date (`iHostSetFileTime` returns false).
- **Time zone**: whether newlib's `localtime_r` gives the console's zone or UTC
  is unchecked (the save list's dates show it).
- **Not done yet**: shader warm-up at boot (Mesa compiles each shader on its
  first draw, every launch), CPU boost while loading
  (`appletSetCpuBoostMode`), keeping the screen awake during movies
  (`appletSetMediaPlaybackState`), holding the HOME-menu exit while a save is
  written (`appletLockExit`), a measured peak memory, frame rate.
- **The frame metrics** (`TSSM_PERF`, `rw/perf.cpp`) build here but have no
  per-thread clock or per-thread report: newlib has neither, so their CPU columns
  read as wall time. Nothing runs them unless a build sets that variable.
- **Turning a saving off** on the console takes a rebuild: `TSSM_GPU_OPT`,
  `TSSM_GL_VAO` and the rest are environment variables, and the Homebrew Menu
  has no way to set one.
- **Licences** for a static link: FFmpeg's relinking terms, and the exact
  Mesa, libdrm_nouveau and newlib notices (Building, above).
- **No CI.** A job in the `devkitpro/devkita64` image would keep the target
  building.
