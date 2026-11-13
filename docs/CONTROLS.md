# Controls: The SpongeBob SquarePants Movie, PC port

This file describes every control the game reads and what each one does. It also describes how each
button was established and how to change the controls.

The port runs the GameCube code against the Xbox release's files. Every button prompt those files
draw is an Xbox button. So the buttons below have the Xbox controller's names: A, B, X, Y, LT, RT,
white, black, Back and Start. The port then maps each of them onto whatever controller or keyboard
is in use.

The code is in `src/SB/Core/pc/iPad*`. The test that checks it is `tssm_input_test` (see
[Testing](#testing)).

## At a glance

These are the defaults, with `input.layout = xbox`, the default for every pad except a GameCube one.

| Game button | Xbox pad | PlayStation | Switch | GameCube adapter | Keyboard and mouse |
|---|---|---|---|---|---|
| **A** | A | Cross | A | A | Space |
| **B** | B | Circle | B | B | right mouse, Backspace |
| **X** | X | Square | X | X | left mouse, F |
| **Y** | Y | Triangle | Y | Y | Q |
| **LT** | LT | L2 | ZL | L | middle mouse, R |
| **RT** | RT | R2 | ZR | R | E |
| **white** | LB | L1 | L | Z + L | Z |
| **black** | RB | R1 | R | Z + R | C |
| **Back** | Back (View) | Share (Create) | − | Z | Tab |
| **Start** | Start (Menu) | Options | + | Start | Esc, Enter |
| **D-pad** | D-pad | D-pad | D-pad | D-pad | arrow keys |
| **Left stick** | left stick | left stick | left stick | control stick | W A S D (Ctrl held: walk) |
| **Right stick** | right stick | right stick | right stick | C-stick | mouse, I J K L |

Notes on the table:

- A pad's A, B, X and Y are found by the **letter printed on the button**. On a Switch pad, A is
  the right-hand button, not the bottom one.
- A PlayStation pad uses the usual equivalence: cross is A, circle is B, square is X and triangle is
  Y. That is where those buttons sit on an Xbox pad anyway.

### What each button does

| Game button | On foot | Everywhere else |
|---|---|---|
| **A** | Jump. Press again in the air to double jump. Jump into a special wall and press A again to wall jump. | Menus: accept or choose. Talk boxes: next, or yes. Car: drive. Bungee: dive. Cutscenes and fly-throughs: skip (A or Y). |
| **B** | SpongeBowl (SpongeBob). Cartwheel (Patrick). | Menus: back, cancel or no. Talk boxes: quit. Bungee: let go. Car: horn. Trigger points that ask for the action button. |
| **X** | Karate Spin (SpongeBob). Star Spin, and Smash in the air (Patrick). | Menus: options and upgrade description. Car: brake and reverse. Screen adjust: reset. |
| **Y** | Bash. Swing (ice blocks, gaps). | Menus: previous. Talk box yes/no questions: no. Cutscenes: skip. Screen adjust: undo. |
| **LT** | Camera. A tap swings it behind the player. Holding it for half a second toggles the close-up camera. It also shows the HUD. | |
| **RT** | Talk and activate (Mindy, challenge pads, switch points). Pick up and throw (Patrick; hold to Macho Throw). Sonic Wave (SpongeBob's guitar; press again to home in). | Car: nitro. Bus stops: call the bus. |
| **white** | Not used in normal play. | It sets L2, which only the debug flight reads. |
| **black** | Not used in normal play. | It sets R2, which only picks the stick that the zCam cameras are handed (no TSSM camera reads it), and Z, which wakes the menus. |
| **Back** | Upgrade window. | |
| **Start** | Pause. | Title: "Press Start". |
| **D-pad** | Walks, at full speed. | Menus and talk boxes. |
| **Left stick** | Move: the further you push, the faster you go. Slides: push forward or back to speed up or slow down. | Car, boulder and bungee: steer. Menus: works as the D-pad, repeating every 0.35 s. |
| **Right stick** | Camera: orbit and tilt. | |

**Cheat codes** work as on the consoles. Pause, hold LT and RT, then enter the code with X and Y.

### The other move layout

Set `input.layout = gamecube` to put the moves where the GameCube release has them:

- B does Karate Spin, Star Spin, Smash and car reverse.
- X does SpongeBowl, Cartwheel and the car horn.

Nothing else moves. The menus still accept on A, go back on B and open options on X. The game's
prompts follow the layout: "To do a Karate Spin, press the [B] button".

With `input.layout = auto`, the default, a GameCube controller gets the gamecube layout and
everything else gets the xbox one.

## How each button was established

Three sources were read end to end. All the evidence below can be reproduced from the decomp and the
Xbox disc; the throwaway scanners are not part of the repository.

### 1. The code the port runs: the GameCube release

`gc/iPad.cpp` turns the GameCube pad into the game's `_tagxPad::on` bits:

| GameCube | Bit (`xPad.h` name) | |
|---|---|---|
| A | `XPAD_BUTTON_X` 0x10000 | |
| B | `XPAD_BUTTON_TRIANGLE` 0x80000 | |
| X | `XPAD_BUTTON_O` 0x20000 | |
| Y | `XPAD_BUTTON_SQUARE` 0x40000 | |
| L, or L trigger at least 0x18 after `PADClamp` | `L1` 0x100 | Z + L gives `L2` 0x200 |
| R, or R trigger at least 0x18 after `PADClamp` | `R1` 0x1000 | Z + R gives `R2` 0x2000 |
| Z | `Z` 0x100000 | set whenever Z is held |
| Start | `START` 0x1 | |
| D-pad | `UP` 0x10, `DOWN` 0x40, `LEFT` 0x80, `RIGHT` 0x20 | |
| stick, C-stick | `analog1`, `analog2` | ±127 after the clamp; see [Analog values](#analog-values) |

`xPadUpdate` (`x/xPad.cpp`) derives `pressed` and `released` from these bits. In menus it also turns
the sticks into D-pad presses.

Every read of the pad in the game code was listed (`globals.pad0` and `mPad`, by mask). The meaning of
each read is above. The ones that matter for the Xbox question are the face-button reads:

| Reads | Where |
|---|---|
| `TRIANGLE` | `BubbleSpinCheck1/2` (Karate Spin), `PatrickAttackCheckMelee1/2`, `PatrickStunCheckButtslam/Headbutt` (Star Spin, Smash), melee in `zEntPlayer_Update`, car brake/reverse, cheat-flight descend; **and** menu cancel (`zUI` → `PadPressTriangle`), talk-box quit, bungee dismount, memory-card retry |
| `O` | `BbowlCheck` and the bowl/cartwheel bookkeeping (`gBowlButton`, `gCartwheelButton`), `PatCartwheelCheck`, car horn, trigger action (`zPlayer::CollideTrigger`); **and** menu options (`PadPressO`), screen-adjust reset, cheat entry |

### 2. What the Xbox files show

The prompts are TEXT assets that include a picture by name:

- `button_spinattack_text` is `{i:button_picture_04}`.
- `button_picture_04` is `{tex:pad_button4;...}`.
- `pad_button4` is a 32×32 texture in font.HIP.

Decoding the textures shows what each picture is:

| Texture | Xbox disc | GameCube disc |
|---|---|---|
| `pad_button1` | A | A |
| `pad_button2` | Y | Y |
| `pad_button3` | B | B |
| `pad_button4` | X | X |
| `pad_button_L1`, `pad_button_R1` | left and right trigger | L, R |
| `pad_button_L2`, `pad_button_R2` | white, black | "L2", Z |
| `pad_button_start`, `pad_button_select` | Start, Back | Start, "Select" |
| `pad_button_L_analog`, `pad_button_R_analog` | left and right stick | control stick, C-stick |

The prompts on each disc point at these pictures:

| Prompt | Xbox | GameCube |
|---|---|---|
| jump, double jump, car forward, bungee dive, menu accept | A | A |
| Karate Spin, Star Spin, Smash, car reverse | **X** | **B** |
| SpongeBowl, Cartwheel | **B** | **X** |
| Bash, swing, menu "previous" | Y | Y |
| menu cancel / no / return, bungee exit | B | B |
| menu options, upgrade description, screen-adjust reset | X | X |
| talk, pick up, throw, Sonic Wave, switch characters, nitro | right trigger ("**Pull**") | R ("Press") |
| upgrade window (`button_select_text`) | **Back** | **Z** |

So the two releases agree about the menus and disagree about the moves.

The UI link data (which pad event a menu reacts to) is the same on both discs, apart from link order:

- cancel is `PadPressTriangle`;
- options is `PadPressO`.

The exception is the upgrade window. The GameCube opens it on `PadPressE` (Z); the Xbox opens it on
`PadPressSelect` (Back).

### 3. What the Xbox executable does

`DEFAULT.XBE` was disassembled. Only the headers and code were read; it was never run. Its pad layer
(the function at 0x1A6A00 that fills `_tagxPad::on` from XInput) sets:

| Xbox | Bits |
|---|---|
| A | `X` 0x10000 |
| **B** | **`O`** 0x20000 |
| **X** | **`TRIANGLE`** 0x80000 |
| Y | `SQUARE` 0x40000 |
| black | `Z` + `R2` (0x102000) |
| white | `L2` + 0x200000 (0x200200; no TSSM code reads 0x200000) |
| left trigger, analog > 128 | `L1` |
| right trigger, analog > 128 | `R1` |
| Back | `SELECT` 0x2 |
| Start | `START` |
| D-pad | `UP`/`DOWN`/`LEFT`/`RIGHT` |
| left stick click | 0x400 (nothing reads it) |
| right stick click | 0x4000 (nothing reads it) |

So the Xbox swapped B and X in the pad layer. It then swapped them back where a menu or dialog reads
them:

| Swapped back | How |
|---|---|
| `zUI::DoUpdate` | `O` raises `PadPressTriangle` and `TRIANGLE` raises `PadPressO`. Back raises `PadPressTriangle` as well as `PadPressSelect`. |
| `zUIMgrUpdate` | wakes the menus on Start or Select, where the GameCube uses Start or Z |
| talk-box quit | reads `O` |
| bungee dismount | reads `O` |
| cheat entry | reads `TRIANGLE` and `SQUARE` |

Every move read kept its bit, and those moves moved with the swap:

- Karate Spin, Patrick's melee and the car brake (`TRIANGLE`, now X);
- SpongeBowl, Cartwheel, the car horn and the trigger action (`O`, now B);
- the taxi (`TRIANGLE` | `SQUARE`, now X or Y).

One prompt came out wrong. Screen adjust's reset reads `O`, which the Xbox moved to B, but its
prompt still shows X.

### The result

The port keeps the GameCube pad layer, which is letter for letter: A→`X`, B→`TRIANGLE`, X→`O`,
Y→`SQUARE`.

The **move** reads in the game code are wrapped in `XPAD_MOVE(mask)` (`x/xPad.h`):

- Off the PC it is the mask itself, so the GameCube build is byte-identical. A full GameCube build
  with the change passes the DOL hash check, sha1 `f772db7f…`.
- On the PC it is `iPadLayoutButton`, which trades `TRIANGLE` and `O` under the xbox layout.

That reproduces the Xbox release's behaviour wherever its prompts are right:

- 19 reads in `zEntPlayer.cpp`, 3 in `zEntPlayerCar.cpp`, 2 in `zGameState.cpp`, 1 in
  `zPlayer.cpp` and 2 in `zTaxi.cpp` are wrapped.
- The menus, talk boxes, bungee dismount and cheat entry are not, because the Xbox swapped those
  back.
- Screen adjust is not, so its reset is on X, as its prompt says.

The other rows:

- **white** and **black** set what the Xbox sets.
- **Back** sets `SELECT`, plus `Z`.
  - The Xbox data opens the upgrade window on `PadPressSelect`.
  - The GameCube code wakes the menus on Start or Z, not Select, so Back carries Z to wake them.
  - `zUI::DoUpdate` tests Select before Z, so the press still arrives as Select.

Verified in the game: our build, run with a scripted pad, reaches gameplay in Bikini Bottom.

- With the xbox layout, X does a Karate Spin, B a SpongeBowl, and A jumps.
- With the gamecube layout, B does the Karate Spin and X the SpongeBowl.
- The menus accept on A and go back on B in both.

## Analog values

The game sees each stick as two signed bytes, ±127, down and right positive.

| Value | Where | Meaning |
|---|---|---|
| 32 (`G.AnalogMin`) | player, camera drive | below this the stick is idle |
| 110 (`G.AnalogMax`) | player, slides | full speed; the D-pad sets the stick to this |
| 40 .. 127 | `xPadNormalizeAnalog` | the normalised stick the game uses for direction and magnitude |
| ±50 | `xPadUpdate` in menus | the stick acts as the D-pad, repeating every 0.35 s |
| 32 .. 110 | `zCameraUpdate` | the C-stick turns and tilts the camera at a rate proportional to how far past 32 it is |
| ±45 | screen adjust | |

How far a host stick has to move to produce those numbers is `input.stick_curve`:

| Curve | Behaviour | Source |
|---|---|---|
| `xbox` (default) | Each axis on its own. Nothing up to 26% of the travel, full at 70%, straight in between. | The Xbox release's own curve, from the conversion its pad layer calls for both sticks: constants 0.26, 0.70, 1/0.44 and 128 |
| `gamecube` | A round deadzone (XInput's 24% and 27%), then the stick is shaped onto the GameCube's octagon and clamped as `PADClamp` and `iPadConvStick` do. Full speed comes a little past half way. | treedome |
| `linear` | A round deadzone (15%), then straight to the rim | |

`input.deadzone` overrides the curve's deadzone.

**Triggers.** Analog on every modern pad. The game reads only a click, and the click comes at
`input.trigger_threshold` (30% by default):

- The GameCube code clicks at `triggerLeft >= 0x18` after `PADClamp`. That is raw 54 of a full pull of
  180, or 30%.
- The Xbox release clicks at 50%.

The analog value is kept for the input test and a settings screen.

**Rumble.** This is the GameCube's on/off motor driven as a pulse train (`iPadStartRumble`), on both
motors. `input.rumble` scales it, and 0 turns it off. The game's own vibration option still applies.

## Controllers

`input.preset` says what the controller is. With `auto`, the default, it is taken from what SDL
reports:

| Preset | Controllers | What changes |
|---|---|---|
| `xbox` | Xbox, Xbox 360/One/Series, XInput clones, anything unrecognised | the base rows |
| `playstation` | DualShock 3/4, DualSense | the same rows; PlayStation glyphs |
| `switch` | Switch Pro, Joy-Cons | the same rows, found by letter (A is the right-hand button); Switch glyphs |
| `gamecube` | GameCube adapters | three shoulders where the game wants four (see below); GameCube glyphs; layout `auto` becomes gamecube |

The **gamecube** preset uses Z as a modifier, as the console does:

- L alone is LT, and R alone is RT.
- Z + L is white, and Z + R is black.
- Z alone is Back (the upgrade window), which is where the GameCube release's own prompt puts it.

**Hot-plug.** Controllers can be plugged in and out at any time. A controller that arrives takes the
lowest free slot, and the log names it. When the controller on port 0 changes, the bindings and the
layout are re-read for the new kind. `input.controller = 1..4` pins port 0 to one slot.

Controllers SDL does not know can be given a layout with a `gamecontrollerdb.txt` beside the
executable.

**Touch.** `input.touch_controls` (on by default on Android) adds on-screen controls:

- a stick;
- A, B, X and Y;
- LT and RT;
- Back and black;
- Start.

A drag on the right half of the screen turns the camera.

## Keyboard and mouse

`input.keyboard`:

- `always` (the default): keys and mouse play alongside a controller.
- `fallback`: only while no controller is on port 0.
- `off`.

With no controller at all, port 0 is the keyboard, and the game never shows its "no controller" screen.

**The mouse turns the camera** (`input.mouse_camera`, on by default) while the game window has focus.
The window holds the cursor (SDL relative mode) and releases it when focus goes.

- The camera turns at a rate proportional to how far the C-stick is past 32 (`zCameraUpdate`).
  So a mouse movement becomes a C-stick deflection past 32 in proportion to the distance: a slow drag
  and a quick flick turn the camera as far as the hand moved.
- Moving the mouse right looks right. Moving it down looks down.
- `input.mouse_sensitivity` scales it. `input.camera_invert_x` and `input.camera_invert_y` flip the
  mouse and the right stick together.

**Keys are positions, not characters.** "w" means the key above "s", so on an AZERTY keyboard WASD
stays under the left hand as ZQSD.

**Reserved keys:**

- Alt+Enter and F11 switch between windowed and full screen.
- F1 and Back + Start open the overlay, when the GUI is built in.

## Changing the controls

Everything is in config.ini, which is written beside the executable on the first run. A settings
screen writes the same lines.

```ini
[input]
preset = auto            ; xbox, playstation, switch, gamecube
layout = auto            ; xbox: Karate Spin on X; gamecube: on B
button_icons = auto      ; prompt pictures: auto, xbox, playstation, switch, gamecube, keyboard, off
keyboard = always        ; always, fallback, off
stick_curve = xbox       ; xbox, gamecube, linear
deadzone = auto          ; or a percentage
trigger_threshold = auto ; 30, or a percentage
camera_sensitivity = 1.0
camera_invert_x = off
camera_invert_y = off
mouse_camera = on
mouse_sensitivity = 1.0
rumble = 100
test = off               ; log, overlay
```

`[pad]` and `[keyboard]` hold one line per game button. Left of the `=` is the game button, as the
prompts show it. Right of it is what presses it:

| Form | Example | Meaning |
|---|---|---|
| one input | `a = space` | |
| `,` | `x = mouse1, f` | either input, on its own |
| `+` | `black = rt+rb` | both at once |
| `!` | `rt = rt+!rb` | held, and the other one not held |
| nothing | `white =` | the button can never be pressed |

- One input may appear in several rows.
- A row takes up to 4 alternatives of up to 3 inputs each.
- A typo costs only that row. The log names the row and the unknown input, and the row is left unbound.

The pad's inputs are positions, as SDL names them:

| Inputs | |
|---|---|
| `a`, `b`, `x`, `y` | bottom, right, left, top |
| `lb`, `rb`, `lt`, `rt`, `ls`, `rs` | bumpers, triggers, stick clicks |
| `back`, `start` | |
| `dpup`, `dpdown`, `dpleft`, `dpright` | |
| `misc1` | share or capture |
| `paddle1` .. `paddle4` | the paddles |
| `touchpad` | |
| `ltclick`, `rtclick` | the GameCube triggers' digital clicks |

The keyboard's inputs:

| Inputs | |
|---|---|
| letters and digits | |
| `space`, `enter`, `tab`, `escape`, `backspace` | |
| `shift`, `ctrl`, `alt`, and their `l`/`r` forms | |
| `up`, `down`, `left`, `right` | |
| `f1` .. `f12` | |
| `numpad0` .. `numpad9` and the other keypad keys | |
| `mouse1` .. `mouse5`, `wheelup`, `wheeldown` | the mouse |

The `[pad]` lines are written commented out, so they keep following `input.preset`. Uncomment one to
take that button over. Examples:

```ini
[pad]
x = x, rb               ; Karate Spin on RB too
black = paddle1         ; black on an Elite paddle
[keyboard]
a = space, k
move_up = w, up         ; the arrow keys also move (they are the D-pad already)
walk = lshift
```

Rows that are not game buttons:

- `move_up`, `move_down`, `move_left`, `move_right`: the left stick.
- `camera_up`, `camera_down`, `camera_left`, `camera_right`: the right stick.
- `walk`: held, the move rows give half deflection, which the game walks at.

They are bound on the keyboard by default and may be bound on a pad too.

The overlay's Controls tab (F1, or Back + Start; `docs/GUI.md`) edits all of this on screen. It is
built on `src/SB/Core/pc/iPadControls.h`, which offers:

- the rows;
- reading, setting, adding, removing and resetting a binding, with the reason when one is refused;
- capturing the next key, mouse button or pad input;
- labels for people ("Left mouse", "Cross", "ZR");
- the rows an input is already used by;
- the `[input]` settings, checked against their domain;
- the live state of port 0;
- releasing the mouse while the overlay is open.

## On-screen prompts

The prompts show what to press on the device in hand (`src/SB/Core/pc/iPadPrompts.cpp`):

- **Glyphs follow the device and the binding.** `input.button_icons = auto` uses the device last used:
  - keyboard: keycaps ("SPC", "LMB", "E");
  - PlayStation: cross, circle, square and triangle;
  - Switch: the Switch's letters;
  - GameCube: the GameCube's buttons, with chords drawn as "Z+L";
  - Xbox pad: the disc's own pictures, rearranged if a face button was rebound.

  The glyphs are drawn by the port and swapped into the `pad_button*` textures; the disc's files are
  never touched.
- **The six move prompts follow the layout.** Their TEXT assets are rewritten in place in memory.
- **"Pull" becomes "Press"** for a key or a GameCube R.

Checked in the game: the main menu's "Choose" prompt draws "SPC" with the keyboard and a cross with
`button_icons = playstation`. The dialog's "Return" draws "RMB".

**Limits:**

- A keycap holds four characters.
- Text that names the console in words ("do not turn off your Xbox console") is not rewritten here.

## Testing

**The input test** shows what the game actually reads. Turn it on with `TSSM_INPUT_TEST=1` or
`[input] test = log`. Each frame in which anything changed prints one line:

```
tssm: input    395 connected on=020000 [X] new=[X] stick=(   0,-127) cstick=( -53,   0) lt=  0% rt=100% from=pad (Xbox Series X Controller)
```

The line shows:

- the frame;
- the game's bits by button name, and the ones newly pressed;
- both sticks in the game's units;
- the triggers' pull;
- where the input came from.

`TSSM_INPUT_TEST=overlay` also draws the controller on screen as the game sees it (OpenGL and Vulkan).

**`tssm_input_test`** is built beside `tssm.exe`. It needs no game data, no window and no device.

- It drives SDL's own virtual controllers (an Xbox Series, a DualSense, a Switch Pro and a GameCube
  adapter): attached, pressed and unplugged through SDL's real gamepad code.
- It also drives a stand-in keyboard and mouse.
- It runs the game's own `xPadUpdate` and checks what the game sees, 127 checks in all:
  - every button on every preset;
  - press and release edges;
  - the trigger threshold;
  - the stick curves, sensitivity and inversion;
  - the keyboard alongside a pad;
  - the mouse camera;
  - the GameCube chords;
  - hot-plug;
  - rebinding through the API, with the refusals;
  - the move layout;
  - the prompt rewrites and glyph choices;
  - the menu stick-to-D-pad repeat.

```
build-*/tssm_input_test [scratch folder]
```

## Not mapped, on purpose

| What | Why |
|---|---|
| Left and right stick clicks | The Xbox sets bits for them that no TSSM code reads. Bind them to anything. |
| The GameCube's B + X + Start reset | On a PC it would quit the game from a button combination. |
| Debug flight (`CheatFlyToggle` with Z and L2) and D-pad character switching (`CheatPlayerSwitch`) | They exist only with the debug cheats on. They read white, black and the D-pad as the console did. |
| Memory card screens (`zMainMemCardSpaceQuery`) | The PC save code never reports a card error, so they never show. |
