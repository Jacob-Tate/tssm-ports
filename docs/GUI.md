# The PC port's GUI: launcher and in-game overlay

The PC port of The SpongeBob SquarePants Movie has its own interface for everything the console never
had to ask: which renderer, which window mode, where the game's files are, which key does what. It is
drawn with [Dear ImGui](https://github.com/ocornut/imgui) (MIT), pinned as the `third_party/imgui`
submodule at release **v1.92.9b**. Dear ImGui runs on Windows, Linux and Android, and it handles touch.
wxWidgets, which the BfBB port's configurator used, cannot run on Android.

There are two parts, and they never run at the same time:

- **The launcher** is a small window shown before the game's own window opens. You use it to choose
  the folder with the game's files and the settings that only apply at startup.
- **The overlay** is drawn over the running game. It has Settings, Controls, Saves, Mods and About
  tabs.

Both read and write `config.ini` through the port's config layer (`iConfig.h`). A setting changed in
the GUI and a setting typed into the file by hand are the same thing.

## For players

### Opening and closing the overlay

| Action | Opens or closes the overlay |
|---|---|
| **F1** | yes. The key is set by `[gui] key`. |
| **Back + Start** on a controller | yes. Hold Back, then press Start. Turn it off with `[gui] pad_combo`. |
| **Three fingers** touching the screen together | yes. They must land within a quarter of a second. |
| **Esc** | closes only |
| **Resume** | closes only |

On the Switch - and + open it (or three fingers, or `[gui] key` on a USB keyboard), B chooses, A
goes back and L/R change the tab; docs/SWITCH.md, "The settings window", has the rest.

On the 3DS the overlay is on the bottom screen, the game stays on the top one, and START + SELECT
(in either order) open and close it; B chooses, A goes back, L/R change the tab, and a tap works too.
docs/3DS.md, "The settings menu (bottom screen)", has the rest.

The game does not take input while the overlay is open.

The overlay's own controls:

- **Mouse:** click as usual.
- **Keyboard:** arrow keys, Enter and Esc.
- **Controller:** the D-pad or left stick to move, A to choose, B to go back.
- **Touch:** tap as with a mouse.

The game keeps running behind the overlay, but it gets no input until the overlay closes. Buttons held
when the overlay closes stay hidden from the game until you let them go. Without this, the Start of
Back+Start would reach the game as a fresh press and open the pause menu.

**Quit game** asks first, then closes the game the same way the window's close button does.

### What applies when

Most settings take effect immediately:

- window or borderless
- vertical sync
- the frame-rate cap
- field of view
- HUD placement
- the glow and the screen warp (`docs/RENDERING.md`)
- camera speed
- stick deadzone
- the controls

These settings take effect at the next start, and are tagged **restart**:

- the renderer
- the aspect ratio and the render size
- anti-aliasing
- the Direct3D 9 path
- exclusive fullscreen
- touch controls
- intro movies
- the game and save folders
- the mod folder

### The tabs

- **Settings:**
  - **Display:** renderer, window mode, aspect ratio, render size, anti-aliasing, vsync, frame-rate
    cap, field of view, glow and screen warp. Three of them say what they cost, in a line under the
    control:
    - **Frame rate cap** above 60 (a number, or "Display rate" on a 90, 120 or 144 Hz screen) says
      how many times the work of 60 that is. The game draws and simulates every frame, so the work
      per second goes up with the rate, and so do a phone's heat and battery use. Measured on a
      120 Hz phone at 2340x1080 with 4x anti-aliasing, four minutes at 120 took the chip from 37 to
      49.5 C and still rising; at 60 it levelled off at 41 C. "No cap" says the same. Movies play
      at 60 at most whatever the cap (`iWindowSetFrameRateCeiling`): they are 30 frames a second
      or less, and above 60 the player only showed the same picture again.
      Below 60 (the "30" choice, or a device that cannot keep up), the game moves and checks
      everything in bigger steps, as the original does. One thing is changed on purpose: on a
      device that cannot keep about 45 frames a second the SpongeBall keeps its full speed. The
      original game slows it at those rates, while the falling blocks of the SpongeBall levels do
      not slow, so the player fell through them; the port gives the ball the steps it would have
      had at 60 (`src/SB/Core/pc/PORTING.md`, known divergences). At a steady 60 and faster nothing
      changes.
      On Android, with vsync on and the cap at or above the screen's rate ("Display rate" is),
      the screen's own refresh paces the frames, and the cap's timer only stands behind it in case
      vsync does not wait (`iWindowPaceFrame`): two clocks at nearly the same rate drift against
      each other, and a frame that fitted was then shown a refresh late. On a desktop, where a
      variable-refresh monitor or the compositor does not hold the rate, and under the screen's
      rate anywhere, the timer paces as before.
    - **Anti-aliasing** says when the GPU gave fewer samples than asked (most phones give 4x for
      8x), and that it costs GPU time in every frame otherwise.
    - **Render size** says that a bigger picture costs more GPU time in every frame.
  - **Audio:** master, music, effects and movie volumes, all live. Music is the game's music,
    Sound effects its effects and speech, Movies the movies and the cutscenes, whose tracks are one
    mix of speech, music and effects. Master scales all of them. The game's own music and effects
    volumes, in its options menu, stay separate from these and apply on top; its effects volume
    covers the movies and cutscenes as well, as on the console.
  - **Game:** intro movies and the save folder. (In the launcher, touch controls too.)
  - **Game files:** the folder, with a folder picker and a check of what is in it.
  - **This window:** overlay size, the key that opens it, the controller shortcut, when the launcher
    shows, and where `config.ini` is.
- **Controls:** the connected controllers and what controller 1 is read as, then a table of every
  button the game reads (and the keyboard's stick rows) with what it does, its controller binding and
  its keyboard and mouse binding, then the `[input]` settings. `docs/CONTROLS.md` has the controls
  themselves.
  - Click a binding for its menu: press a new button or key in its place, add another one beside
    it, remove one of several, type it in config.ini's grammar (chords `rt+rb`, "not" `!rb`), unbind
    it, or put the default back.
  - `*` marks a binding changed from the default, and `!` an input that also presses another
    button (its tooltip says which).
  - The settings are the input layer's own: controller type, move layout (Karate Spin on X or B),
    button pictures, keyboard alongside a controller, stick response, deadzone, trigger press point,
    camera and mouse speed and inversion, rumble, touch controls and the input test. Each applies at
    once except touch controls (restart).
  - While the overlay is open the mouse is the overlay's: the mouse camera lets go of it, and takes
    it back when the overlay closes.
- **Saves:** the save folder, and every slot of folders A and B (or of A to J) with its level,
  progress, when it was saved, its backups, and which slot the game in progress autosaves to.
  - **Import...** opens the system's file dialog for a Dolphin `.gci`, a GameShark `.gcs`, a memory
    card image (`.raw`, `.gcp`), a `.zip` from Export, or a save file from another install. Every save
    found is listed with where it would go; each can be left out or sent to another folder and slot.
    A slot that already holds a save is only replaced with **Replace** ticked (the old save becomes
    backup 1). Importing over the slot the game autosaves to asks first: the game's next autosave
    would write over it.
  - **Export...** beside a slot writes that save as a `.gci` (Dolphin's Import, or a console's memory
    card manager). **Every save as a .zip** carries them all to another install; **Every save into a
    GCI folder** writes them as Dolphin names them, as Dolphin's GCI-folder memory card keeps them.
    The region (North America or Europe) can be chosen for saves that never saw a card.
  - **Backups** lists a slot's last three saves; **Restore** puts one back (what is there becomes
    backup 1). **Delete** empties a slot, keeping the save as backup 1.
  - `docs/SAVES.md` has the formats.
  - **Where there is no system file dialog** (the Switch; or `[gui] file_picker = builtin` /
    `TSSM_FILE_PICKER=builtin` on a PC) the GUI's own file browser stands in, see "The built-in file
    browser" below: Import starts it in the import folder, and the exports need no dialog at all.
    **Export** writes the slot's `.gci` into the export folder under the name Dolphin gives it,
    **Every save as a .zip** writes `tssm-saves-YYYYMMDD-HHMMSS.zip` there, and **Every save into a
    GCI folder** a new `gci-YYYYMMDD-HHMMSS/` folder. Nothing is written over: a second export of
    the same name gets `-2`, `-3`. The status line says where each file went, and the tab says where
    the two folders are.
- **Mods:** the mods folder, with **Open**, **Rescan** and the **Use mods** switch, then the mods in
  load order: each switched on or off, moved with the arrows or by dragging its name, with what it
  replaces and how many conflicts it has; a click shows its description and counts. Then the
  conflicts (what, which mod is used, over which) and the mod log, with a filter, **Copy** and
  **Open mods.log**. Every change is written to `mods/mods.ini` at once. The single mod folder of
  `[assets] mod` is kept at the bottom. `docs/MODDING.md` is the modder's guide.
- **About:** the version, the renderer and library versions in use, and the full license text of
  every third-party component.

### The built-in file browser

`iGuiFilePicker.cpp`. The system's dialogs come through SDL, and SDL has none on the Switch (its
stubs answer "unsupported"). There, and wherever a system dialog fails at run time, the GUI's own
browser answers the same `iGui::PickFolder` / `PickOpenFile` / `PickSaveFile` request, so the tabs
that ask do not know which one answered.

- **Which one:** `[gui] file_picker` = `auto` (the default: the system's dialog, and the built-in
  browser from the first time a system file dialog fails, for the rest of the run; a failed folder
  dialog -- Android's SDL has none -- hands over only that request), `system` or `builtin`.
  `TSSM_FILE_PICKER` overrides the setting. The Switch and the 3DS always use the built-in one.
- **Folders:** saves to import are looked for in `<base>/import/`, exports go to `<base>/export/`,
  both made when first needed. `<base>` is `/switch/tssm` on the Switch, `/3ds/tssm` on the 3DS
  (`sdmc:/3ds/tssm`, shown without the `sdmc:`, whose top the browser knows), the per-user data
  folder's `tssm` elsewhere (`%APPDATA%/tssm` on Windows), or `TSSM_FILE_PICKER_DIR` when that is
  set.
- **Opening a file or a folder:** a list of the folder's folders, then the files the dialog's
  filter takes (Import: `.gci .gcs .raw .gcp .zip`; **Every file** shows the rest), sorted by name.
  A long name is cut in the middle in the list and shown whole under it. **Up**, **SD card** (the
  top of the drive on a PC) and **Import folder** (the folder it started in) move about. A folder
  picker (the launcher's **Browse...**, the Mods tab's single mod folder) lists only folders and
  has **Choose this folder**.
- **Controller:** the d-pad or stick moves, the accept button (the button that presses buttons
  everywhere else in the overlay) opens a folder or chooses a file, the back button returns to the
  folder before or, in the first folder, cancels; the top face button goes up a folder, the left
  one chooses the folder shown in a folder picker, and L and R move a page. The line at the bottom
  names the buttons as the controller in hand prints them. Keyboard: arrows, Enter, Backspace,
  Page Up/Down, Esc cancels.
- **Touch and mouse:** a tap opens or chooses; dragging the list scrolls it (the row under the
  finger is not chosen when it lets go), and so does the scroll bar.
- **Saving a file** asks nothing: the file goes into the export folder under the name the caller
  suggests, made unique.

### The launcher

The launcher shows on its own when the game's files are not where `config.ini` says. On a first run,
that turns the "files not found" error into a folder picker.

- **Browse...** opens the system's own folder picker (through SDL3).
- The line under the folder says whether it is right. It also catches the usual mistakes:
  - the folder one level above the game's files
  - a disc image instead of extracted files
  - another console's release
- **Play** saves the settings and starts the game.
- **Quit** ends without starting the game.

`[gui] launcher` controls when it appears:

- `auto` (the default): only when the files are missing.
- `always`: on every start.
- `never`: never; the old error message stays.

### config.ini

| Setting | Default | Meaning |
|---|---|---|
| `[gui] launcher` | `auto` | `auto`, `always` or `never`; see above |
| `[gui] key` | `F1` | the key that opens the overlay. Any name SDL knows, such as `F1`, `F10` or `Insert`. |
| `[gui] pad_combo` | `on` | whether Back+Start opens the overlay |
| `[gui] scale` | `auto` | the overlay's size: `auto`, or a factor from 0.5 to 4 |
| `[gui] file_picker` | `auto` | `auto`, `system` or `builtin`: which file and folder picker Import, Export and Browse use; see "The built-in file browser" |
| `[audio] master_volume`, `music_volume`, `sfx_volume`, `movie_volume` | `100` | 0 to 100. At 100 the sound is exactly what it would be without them. They count in a build without the overlay too. See "Panels other modules fill" below. |

`auto` scale takes the display's own scaling (150%, 200%), raises it for a big window so the text can be
read from a sofa, and lowers it to fit a small one.

## For developers

### Files

All of the GUI is under `src/SB/Core/pc/gui/` and is port-only. The table below describes each file;
`src/SB/Core/pc/PORTING.md` lists them too.

| File | What |
|---|---|
| `iGui.h` | the interface for the rest of the platform layer: the launcher, the overlay's init, shutdown and open state, `iGuiFilterPad`, panel and audio registration |
| `iGui.cpp` | the overlay's ImGui context, toggling, the SDL event watch, the input filter and the window with its tabs |
| `iGuiRender.h/.cpp` | the renderer: the ImGui backend for the running librw device, and the hook on librw's present |
| `iGuiLauncher.cpp` | the launcher, on SDL_Renderer |
| `iGuiSettings.cpp` | the Settings tab, also used section by section by the launcher |
| `iGuiControls.cpp` | the Controls tab, on the input layer's binding API (`iPadControls.h`) |
| `iGuiSaves.cpp` | the Saves tab, on the save store (`iSaveStore.h`) |
| `iGuiMods.cpp` | the Mods tab, on the mod system (`iMod.h`) |
| `iGuiTabs.cpp` | the About tab |
| `iGuiUtil.cpp` | the look, scaling, the system's folder and file dialogs, whole-file reads and writes through SDL, the asset-folder check and the PNG writer |
| `iGuiFilePicker.h/.cpp` | the built-in file browser, for where the system has no file dialog (the Switch, the 3DS) |
| `iGuiSelfTest.cpp` | `TSSM_GUI_SELFTEST` |
| `iGuiNull.cpp` | the whole interface as no-ops, built with `TSSM_GUI=OFF`, except that config.ini's volumes still reach the audio listener |

Four existing port files call into it:

- `iSystem.cpp`:
  - `iGuiLauncherRun()` right after `iConfigLoad()`, before anything takes a setting the launcher can
    change.
  - `iGuiInit()` once RenderWare has started and before the first present.
  - `iGuiShutdown()` in `iSystemExit`.
- `iPad.cpp`: `host = iGuiFilterPad(port, host)` after the host poll.
- `iConfigTable.cpp`: the `[gui]` and `[audio]` rows.
- `CMakeLists.txt`: the `tssm_imgui` library, the GUI sources, the generated notices file, and SDL's
  renderer and dialogs turned on in the vendored SDL.

No shared decomp source changes, and the GameCube build is untouched.

### Drawing the overlay: the present hook

The game renders into a virtual screen at the render size. Each librw device stretches that into the
window's back buffer as the first half of presenting it. The overlay is drawn **after the stretch and
before the flip**, for two reasons:

- It is drawn at the window's own resolution, so text is sharp even at a 640x480 render size on a 4K
  screen.
- It can use the black bars beside a 4:3 picture.

Only librw's GL3 and Vulkan devices have a hook at that point (`setPresentOverlay`). So `iGuiRender.cpp`
takes over the one function pointer every present goes through, `rw::engine->device.showRaster`. librw
itself is not modified. While the overlay is closed, the hook calls librw's own present and returns,
which is the closed overlay's whole cost.

While the overlay is open, each backend does the following:

- **GL3:** sets librw's `setPresentOverlay` to ImGui's OpenGL 3 renderer for this one present, lets
  librw present, and then puts the touch controls' hook back (`iTouchOverlayInstall`). Under the open
  overlay the touch controls are not drawn, and the game is not taking input anyway.
  - The GLSL version comes from the context librw actually got (`gl3Caps`). On Android, ImGui is built
    with `IMGUI_IMPL_OPENGL_ES3`.
- **D3D9:** does the stretch itself, as librw does: `resolveVirtualScreen`, then `StretchRect` into the
  back buffer letterboxed. Then it draws the overlay with ImGui's DX9 renderer and calls `Present`.
- **D3D11:** puts the game's picture into ImGui's background draw list: a view of
  `virtualScreenTexture()`, drawn with blending off and placed with librw's letterbox arithmetic. ImGui's
  DX11 renderer then draws it and the overlay into the back buffer, and the hook calls `Present` with
  the same vsync flag librw would have used.
- **C3D (the 3DS):** the overlay has the bottom screen to itself, which shows nothing of the game's
  (`3ds/c3d/c3doverlay.cpp`, `rwc3d.h`'s overlay calls).
  - **Where:** a 240x320 RGBA8 target of its own in VRAM (300 KB), made once in `iGuiInit` while
    VRAM is still free, its output the bottom screen. RGBA8 like the screen's own buffer, so the
    transfer never widens 16-bit colour. No depth (`TSSM_3DS_GUI_DEPTH=1` adds a D16 buffer).
  - **How:** the device's own 2D program (`vs_im2d`) and vertex format: ImGui's vertices are turned
    into `rw::c3d::Im2DVertex` on the CPU, straight into the frame ring, with the indices after
    them; one combiner stage (the vertex colour, its alpha times texture 0's); blending, no depth
    test, no culling; each command's clip rectangle as the scissor, turned into the sideways target's
    pixels the way the top screen's viewport is. Texture units 1 and 2 are not touched.
  - **Textures:** `ImGuiBackendFlags_RendererHasTextures` with ImGui's atlas in `ImTextureFormat_Alpha8`:
    an A8 texture in linear memory (512x128 is 64 KB), written by the CPU in place at the present
    (the frame before was waited for when this one opened) and freed only after the GPU is done.
  - **When:** at the present, inside the game's frame, before librw's present: one `C3D_FrameEnd`
    sends both screens. Room in the frame ring and the command buffer is taken first
    (`overlayBegin`), so no split of the frame can land inside the overlay; the hard split and the
    read-backs keep the bottom screen's output off. A frame whose draw data hashes the same as the
    last one drawn draws nothing at all, and the screen keeps its picture: most open frames.
    `TSSM_3DS_GUI_ALWAYS=1` draws every one, for measuring.
  - **Closed:** the bottom screen shows one line, drawn at start-up, when the overlay closes and after
    the HOME menu or sleep (the APT hook's count, `i3DSResumeCount`); no other frame costs anything.
  - **Open and idle:** an ImGui frame is built only from an input until four frames after it (and
    every frame while a button is held), otherwise every two seconds (`iGui.cpp`, `Frame`): a built
    frame costs the 3DS's CPU several milliseconds.
  - **Errors:** the error box shuts citro3d down (the target and the textures with it) and takes the
    bottom screen with libctru's console, as before.
- **Vulkan:** there is no ImGui renderer for it here yet. The overlay logs that it cannot draw and stays
  off.

Keeping librw's state cache valid:

- Each ImGui renderer saves and restores the device state it changes: a D3D9 state block, and a full
  save and restore in the D3D11 and GL3 renderers.
- The render target and depth surface the hook binds are put back to the ones librw had bound.
- Under GL3, librw is also told to forget its GL state cache.

**D3D9 device resets.** librw resets the device in two cases:

- when the window's client area stops matching the back buffer (`beginUpdate`)
- when the device is lost (`showRaster`)

A reset fails while anything in `D3DPOOL_DEFAULT` is still alive, and ImGui's DX9 vertex buffers and
font texture live there. So they are released wherever a reset can follow: the overlay closing, any
window size, display or fullscreen change (the event watch), and a lost device. ImGui makes them again
on the next frame it draws. A lost device is always left to the present behind the hook.

That present is `rw/devicereset.cpp`'s (`iDeviceReset.h`), in front of librw's: it decides when librw
may reset at all, and has a lost device reset at the next camera update once it can be. The event
watch alone is not enough either: a window restored from another process changes before its events are
pumped, and the reset can come first. So that file also calls `iGuiRenderReleaseVolatile`, through
`iDeviceResetSetReleaseHook`, right before every reset it lets through and whenever it holds one back
from a window that is not minimised (librw reads the window again, and a size that arrives in between
still resets). See `docs/RENDERING.md`, "Direct3D 9 device resets".

### Events and input

SDL has one event queue per process, and it already has two readers. `iWindowPump` takes the window's
range of events and `iPadHostPoll` takes the controllers' range. The GUI does not read the queue: it
watches it with `SDL_AddEventWatch`, which sees every event as it is queued, during whichever
`SDL_PumpEvents` queued it. The queue is left untouched for its owners. Only events on the main thread
are handled.

- **The toggle key, three-finger tap and window changes** are seen by the watch.
- **ImGui's events** (keys, text, mouse, touch-as-mouse) are passed to `ImGui_ImplSDL3_ProcessEvent`
  only while the overlay is open.
- **Controllers.** ImGui's SDL3 backend runs in manual gamepad mode. It is handed the controllers the
  input backend already has open, so it neither opens nor closes devices itself.
- **The game's input.** `iGuiFilterPad` runs inside `iPadUpdate`:
  - It notices the physical Back+Start, not the bindings, so rebinding Start cannot make the overlay
    unreachable.
  - While the overlay is open, it answers a connected pad with nothing pressed. The game sees no
    input, and does not see a missing controller either.
  - After the overlay closes, it masks the buttons that were held at that moment until each one is
    released.
  - On the 3DS, START or SELECT going down alone is held back from the game for up to 120 ms
    (`TSSM_3DS_COMBO_MS`): the game acts on each press by itself (pause, the upgrade window), and the
    other button may follow. If it does, the overlay opens and the game sees neither; if not, the
    button reaches the game late, or for one frame when it was a tap already let go. The game bits
    held back are the ones the `start` and `back` bindings set, so a rebinding is followed.
- **The 3DS's bottom screen.** SDL gets a window there (`iWindow3DS.cpp`) for ImGui's SDL3 backend;
  SDL's 3DS driver makes no framebuffer for it. SDL's touch-to-mouse is off, and the event watch hands
  the bottom screen's finger events to ImGui as touch input itself. ImGui's IME hook is cleared, so
  nothing can call `SDL_StartTextInput`, which opens the system keyboard there.

### Scale and touch

- `iGui::AutoScale` takes `SDL_GetWindowDisplayScale` over the pixel density, raises it to at least
  `window height / 1080`, and caps it so a window of about 900x620 units fits. The result is rounded to
  eighths. `[gui] scale` overrides it.
- The overlay's font is ImGui's scalable default (`AddFontDefaultVector`) at 16 px times the scale.
- On Android, `ImGuiConfigFlags_IsTouchScreen` and larger touch padding are set.
- On the 3DS the scale is 0.8125 (13-pixel text on the 320x240 screen), with tighter padding, no
  rounding, a 14-pixel scroll bar, and every line of a tab wrapped at the screen's edge. `[gui]
  scale` goes from 0.625 to 1 there.

### Panels other modules fill

The Controls, Saves and Mods tabs belong to the modules that own those things. A module replaces the
built-in panel with its own:

```cpp
#include "gui/iGui.h"

static void DrawSavesPanel(void*) { /* any ImGui calls */ }
...
iGuiSetPanel(IGUI_PANEL_SAVES, DrawSavesPanel, NULL);
```

The draw function runs inside the tab, between `ImGui::NewFrame` and `ImGui::Render`. Passing NULL puts
the placeholder back.

The built-in panels are the real ones, each on its module's interface:

- **Controls** on `iPadControls.h`: rows, get/set/add/remove/reset, capture, labels, who uses an
  input, the `[input]` settings checked against the settings table, live state. `iGui.cpp` calls
  `iPadControlsSetMouseCapture(false)` while the overlay is open.
- **Saves** on `iSaveStore.h`: `iSaveStoreList`, a look-only `iSaveStoreImportData` pass to fill the
  import dialog and one call per save (`pick`) to import, `iSaveStoreExportGCIData`,
  `iSaveStoreExportZipData`, `iSaveStoreExportFolder`, `iSaveStoreBackups`, `iSaveStoreRestore`,
  `iSaveStoreDelete` and `iSaveStoreAutoSaveSlot` (the slot the game autosaves to, from the game's
  own autosave cache). Files go in and out through SDL's streams, so a path the system's dialog
  answers with works whatever it is (an Android `content://` URI included). An export adds `.zip`
  or `.gci` to a file name typed without one, but never to a URI: the dialog has made that exact
  document, and a URI with an extension added names another one, which the game may not write.
- **Mods** on `iMod.h`: `iModCount`/`iModGet`, `iModSetEnabled`, `iModMove`, `iModSaveList`,
  `iModRescan`, `iModInit` (again, for the global switch), the conflicts and the log.

The audio sliders are stored in `[audio]` and handed to the sound system through
`iGuiSetAudioListener(fn)`. The function is called once when it is registered and again whenever a
slider moves, with volumes from 0 to 1. `iSnd.cpp` registers it at `iSndInit`, before any movie can play:

- The mixer (`iSndHostSDL.cpp`) has a level per group of voices (`iSndHostSetGroupGain`): music (the
  game's sound category 3), movies (category 4, a cutscene's track) and effects (the rest), each set
  to that slider times Master. The reverb's bus takes the effects level after the reverb, so its tail
  follows the slider at once.
- The levels are atomics. The game thread sets one and the mixer reads it once a block, so neither
  waits for the other. The mixer moves to a new level by itself, full scale in 20 ms at most,
  whatever the size of its blocks, so a jump cannot click. A voice stays where it would be: silent
  voices still advance and still end when they should.
- A movie's sound (`iFMVAudioSDL.cpp`) is scaled on the way into its stream, on the game thread, by
  Movies times Master and the same 20 ms ramp. A change is heard once what is already queued has
  played, at most 160 ms.
- The game's own volumes sit above all that, untouched: `xSndMgr` still gives each voice its level
  times its category's (`iApplyMix`), and a movie starts at `xFMVGetBinkCompliantVolume`, the
  console's level for it.
- At 100 all of this multiplies by exactly 1, which leaves every sample as it was.

### The launcher

The launcher runs before the game has a window or a device, so it opens its own window and draws with
SDL's renderer through ImGui's SDL_Renderer backend. SDL picks the renderer: Direct3D 11 or 12 on
Windows, OpenGL or Vulkan elsewhere, OpenGL ES on Android. The launcher is taken down completely before
`iSystemInit` continues, including `SDL_QuitSubSystem` of the video and gamepad subsystems it started.

The folder picker is `SDL_ShowOpenFolderDialog`. Its answer can arrive on another thread, and it is
handed to the frame under a mutex.

The folder check is `iGui::CheckAssetFolder`:

- `boot.HIP` and `font.HIP` must be present, in any letter case.
- The `PLAT` id in `boot.HIP` must be the Xbox's.
- The folder above the game's files, and disc images, are recognised and explained.

If `TSSM_ASSETS` is set but points at the wrong folder while `[assets] path` is right, **Play** drops
the variable for that run.

### Build

- `TSSM_GUI` (ON by default) builds the GUI. OFF builds `iGuiNull.cpp` instead, and nothing else
  changes.
- `tssm_imgui` builds the ImGui core plus these backends:
  - `sdl3` and `sdlrenderer3`, always
  - `dx9` for `D3D9`
  - `dx11` for `D3D11` (links `d3dcompiler`)
  - `opengl3` for `GL3`
- The vendored SDL is built with `SDL_RENDER` and `SDL_DIALOG` on. An installed SDL3 normally has both.
- The About tab's version line is `git describe` at configure time.
- The About tab's notices are generated at configure time into `gui/iGuiNotices.inc` in the build directory, from each
  component's own license file: this repository's `LICENSE` (CC0), ImGui, SDL, librw, FFmpeg's
  `copyright` from vcpkg, and Vulkan's when that backend is built.

### Debug switches

All of these are environment variables.

| Variable | What it does |
|---|---|
| `TSSM_GUI_OPEN=1` | open the overlay at the first frame |
| `TSSM_GUI_TAB=<settings\|controls\|saves\|mods\|about\|all>` | bring that tab to the front. `all` steps through every tab, one capture each; a list (`saves,mods`) steps through those. |
| `TSSM_GUI_OPEN_AT=<n>` | open the overlay at the n-th present instead of at startup, so a capture shows it over the game (with `TSSM_INPUT` to get there) |
| `TSSM_GUI_IMPORT=<file>` | open the Saves tab's import dialog for that file the first time the tab is drawn, as if it had been picked |
| `TSSM_GUI_CAPTURE=<file.png>` | write the window's back buffer, with the game's picture and the overlay exactly as presented, after the overlay has been open for N frames. A `%s` in the name becomes the tab's name, or `launcher`. Also opens the overlay. |
| `TSSM_GUI_CAPTURE_FRAMES=<n>` | N for the capture above. Default 30. |
| `TSSM_GUI_EXIT=1` | exit with code 0 once the captures are written |
| `TSSM_GUI_SELFTEST=<frames>` | right after `iGuiInit`, present that many frames of a test picture through `RwCameraShowRaster` with the overlay open, then exit |
| `TSSM_GUI_SELFTEST_RESIZE=<w>x<h>` | resize the window a third of the way into the selftest. Under D3D9 this is a device reset with the overlay open; with a different shape it shows the letterbox. |
| `TSSM_GUI_SELFTEST_PAD=1` | attach an SDL virtual controller and play a scripted set of presses through the input backend and `iGuiFilterPad`, logging what the game would see |
| `TSSM_GUI_SELFTEST_CONTINUE=1` | go on into the game after the selftest instead of exiting |
| `TSSM_GUI_LAUNCHER=<auto\|always\|never>` | override `[gui] launcher` |
| `TSSM_GUI_LAUNCHER_PLAY=<n>` | press Play after n frames, if the folder is good |
| `TSSM_FILE_PICKER=<auto\|system\|builtin>` | override `[gui] file_picker` |
| `TSSM_FILE_PICKER_DIR=<folder>` | the folder holding `import/` and `export/` for the built-in file browser |
| `TSSM_GUI_SCRIPT=<steps>` | play input into the open overlay: steps apart by spaces or commas, each `<frame>:<action>`, the frame counted from the overlay opening. Actions: `a b x y` (face buttons by position, `a` the bottom one), `up down left right`, `l r`, `enter esc backspace tab space kup kdown`, `click:X:Y`, `drag:X:Y:X2:Y2`, `wheel:X:Y:N`, `tab:<name>`, `shot:<name>`, `quit`. With `TSSM_GUI_SELFTEST` this drives a tab without a game running. |
| `TSSM_GUI_SCRIPT_SHOTS=<file.png>` | where `shot:<name>` writes; `%s` becomes the name |
| `TSSM_GUI_PAD_HINTS=<xbox\|playstation\|switch>` | the built-in file browser's button hints for that controller, with none plugged in (for a capture) |
| `TSSM_PAD_LABELS=switch` | (PC only) every label as the Switch build shows it: the game's prompts, the Controls tab, the hints (docs/SWITCH.md, Controls) |
| `TSSM_PAD_LABELS=3ds` | (PC only) the same for the 3DS's letters, and the combo named START + SELECT |
| `TSSM_3DS_COMBO_MS=<ms>` | (3DS) how long START or SELECT alone is held back from the game; 0 turns the hold-back off. Default 120. |
| `TSSM_3DS_GUI_ALWAYS=1` | (3DS) draw the bottom screen every open frame, not only when its picture changed |
| `TSSM_3DS_GUI_DEPTH=1` | (3DS) give the bottom screen's target a depth buffer |
| `TSSM_C3D_STATS=1` | (3DS) also a line a second while the overlay draws: ImGui's time a frame, the conversion's, frames drawn and unchanged, draws, indices, ring bytes, textures |

On the 3DS `TSSM_GUI_CAPTURE` and `shot:` write one 400x480 picture of both screens as the console
shows them: the top one above, the bottom one centred below it. `[gui] overlay = off` in config.ini
turns the overlay off there.

Example: every tab on OpenGL, letterboxed, written into a scratch folder:

```
set TSSM_GUI_SELFTEST=500
set TSSM_GUI_SELFTEST_RESIZE=1280x720
set TSSM_GUI_TAB=all
set TSSM_GUI_CAPTURE=C:\scratch\gl3_%s.png
set TSSM_GUI_EXIT=1
tssm.exe          (with [video] backend = gl3, width = 640, height = 480, mode = windowed)
```

### Known limits

- **Vulkan has no overlay yet.** It needs ImGui's Vulkan renderer recording into the render pass that
  librw's `drawPresentOverlay` uses.
- **Android has not been built or tested yet.** The CMake arm (GLES 3) is written. These still need a
  device:
  - the launcher (Android has one window, and the launcher closes it before the game opens its own)
  - the folder picker
  - the three-finger tap
- **Only the game's input is paused, not the game.** Opening the overlay mid-level leaves the level
  running, with SpongeBob standing still. Pausing the game itself would need a call into the game's
  pause path.
- **The file dialogs** are SDL's, or the built-in browser where SDL has none (the Switch). On Android
  the folder export (GCI folder) needs a folder path the system's picker may not give; the zip and
  `.gci` exports and every import go through SDL's streams and do not. The built-in browser cannot
  type a file name: it saves under a name it makes.
- A debug build's Direct3D 11 device asks for the D3D11 debug layer (`D3D11_CREATE_DEVICE_DEBUG` in
  librw), which fails on a machine without the Windows "Graphics Tools" feature. A release build is not
  affected. This is librw's behaviour, not the GUI's.
