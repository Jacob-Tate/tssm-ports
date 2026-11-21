# The app icon

The repository carries no art but its own default icon, a plain sponge block
drawn for the port. Like the game's own files, a picture of yours used as the
app icon stays on the machine that builds: it is never committed, and neither
is anything made from it. Without one, every build has the default: the sponge
block (PC: `dist/pc/default_icon.ico` and `default_icon-64.png`, which
`tools/make_icons.ps1` made from `dist/switch/default_icon.jpg`, the same icon
the released Windows zips carry; Android: `android/app/src/main/res`; Switch:
`dist/switch/default_icon.jpg`).

## Making the icons

`tools/make_icons.ps1` turns any picture into what each platform takes. It
runs on Windows with nothing to install (WPF reads and writes the files; the
scaling is its own):

```
powershell -ExecutionPolicy Bypass -File tools\make_icons.ps1 -Image D:\art\logo.png -OutDir D:\art\icons [-Background "#1E8FD0"]
```

The output folder must be outside the repository; the script refuses one
inside a git work tree. Transparent margins are trimmed first, so the visible
picture is what gets centred. It writes:

| file | for |
| --- | --- |
| `tssm.ico` | Windows: 16, 24, 32, 48, 64, 256 px, the picture on a transparent square |
| `icon-64.png` | an SDL window icon (platforms without an executable icon) |
| `android/res/` | Android: `mipmap-*dpi/ic_launcher.png` (48 dp, legacy launchers), `mipmap-*dpi/ic_launcher_foreground.png` (108 dp, the picture inside the adaptive icon's 66 dp safe circle), `mipmap-anydpi-v26/ic_launcher.xml`, `values/tssm_icon.xml` (the background colour layer) |
| `icon.jpg` | Switch: 256x256 JPEG on the background colour, for `nx_create_nro` |
| `play-512.png` | a Play Store style 512 px icon, for reference |
| `preview.png` | the adaptive icon under a circle, rounded-square and squircle mask, the legacy and Switch icons, and every `.ico` size, on light and dark |

`-Background` is the colour wherever a platform needs an opaque icon (the
adaptive background layer, the Switch JPEG, the Play icon). The default,
`#1E8FD0`, is a sea blue.

## Using them

**Windows (and every PC build).** `cmake -DTSSM_ICON_DIR=<folder>` (or
`TSSM_ICON_DIR` set in the environment for `build-debug.bat` /
`build-release.bat`); empty (the default), the build uses `dist/pc`. The icon
goes to two places:

- `tssm.exe` carries the `.ico` as its first icon resource, which Explorer
  shows. The `.ico` is copied into the build directory and the resource
  script is written there.
- Every window: the launcher's and the game's (each renderer's, the OpenGL one
  once librw has made it) get the icon from `iWindowSetIcon` (`iWindow.h`):
  `SDL_SetWindowIcon` with the 64-pixel PNG (`icon-64.png`, or
  `default_icon-64.png`), compiled into the program as
  `tssm_window_icon.inc`, and on Windows the executable's own icon sent to the
  window at the two sizes the title bar and the taskbar ask for, which is
  sharper than one picture scaled. A window does not rely on the icon SDL
  registers its window class with.

The setting stays in the build directory's CMake cache: unsetting the
environment variable does not remove it; `cmake -DTSSM_ICON_DIR=` (or a new
build directory) does, and the build goes back to the default.

**Android.** `gradlew assembleDebug -Ptssm.icon=<folder>` adds
`<folder>/android/res` to the debug and release resources, which override
`main`'s launcher icon (`@mipmap/ic_launcher`, which the manifest and the
long-press shortcut use). Without the property the APK has the default icon.
docs/ANDROID.md.

**Switch.** The Switch build passes `TSSM_SWITCH_ICON` (any 256x256 JPEG) to
`nx_create_nro` when that is set, else `TSSM_ICON_DIR`'s `icon.jpg` when there
is one, and otherwise the port's own default, `dist/switch/default_icon.jpg` (a
plain sponge block drawn from shapes by `tools/switch/draw_default_icon.ps1`,
registered in `tools/check_icon_art.py`):

```cmake
if(TSSM_SWITCH_ICON)
  set(_nro_icon "${TSSM_SWITCH_ICON}")
elseif(TSSM_ICON_DIR AND EXISTS "${TSSM_ICON_DIR}/icon.jpg")
  set(_nro_icon "${TSSM_ICON_DIR}/icon.jpg")
else()
  set(_nro_icon "${CMAKE_CURRENT_SOURCE_DIR}/dist/switch/default_icon.jpg")
endif()
nx_create_nro(tssm ... ICON "${_nro_icon}" ...)
```

The configure step prints which one it took (`Switch: icon ...`).

## The guard

`.gitignore` ignores the generator's file names and an in-tree `local-assets/`
or `icons/` folder. `tools/check_icon_art.py` fails when an image other than
the repository's own defaults (listed, with their exact content, in the
script; `dist/pc`'s two files among them) is staged: by extension or by its first bytes whatever its name,
anything named like the generator's output, and anything byte-identical to a
file in `TSSM_ICON_DIR` or `--art-dir`.

```
python tools/check_icon_art.py                  # what is staged
python tools/check_icon_art.py --range A..B     # every commit in a range
python tools/check_icon_art.py --tree           # everything tracked at HEAD
```

It works as a pre-commit hook: `.git/hooks/pre-commit` containing
`exec python tools/check_icon_art.py`.
