# Frame pacing (PC)

How a frame is laid out on the PC, what the frame-rate cap does, the frame time the game runs on
(2026-11-21), and what was measured when the pacing was reworked (2026-11-18).

## A frame

Every frame presents exactly once, in `RwCameraShowRaster` (`src/SB/Core/pc/rw/camera.cpp`), and
the port's per-frame host work hangs off that, because the loop itself is retail's:

1. the game's update (`zGameLoop`), which reads the controllers early on (`xPadUpdate` ->
   `iPadUpdate` -> `iPadHostPoll`). The read pumps SDL's queue itself (`SDL_PumpEvents`, which
   runs Windows' message loop and refreshes the controllers' and the keyboard's state) and takes
   only the controller events, so it reads input as fresh as it can be, however long the game
   works before it (a scene's reset does 10 to 20 ms of it). The keyboard read right after it
   uses that pump;
2. the render;
3. the window's pump (`iWindowPump`, `src/SB/Core/pc/iWindowSDL.cpp`): `SDL_PumpEvents` again,
   then the window's own events (closing, resizing, Alt+Enter), and the close button;
4. the flip (`showRaster`), waiting for the display only when `video.vsync` is on;
5. the pacer's sleep to the cap's deadline (`iWindowPaceFrame`).

Two pumps a frame. A movie's pass has its present's, and the skip button's read once a skip is
allowed. Before, the keyboard read pumped as well.

One pump a frame, at the controllers' read, was tried and dropped. It saved about 3 us a frame,
the outside stalls below lasted as long either way, and the present, which every port shares,
would have had to know whether the frame had already pumped.

## The pacer

The deadline moves on one period at a time from where it was, never from the clock, so the rate
does not drift. A frame whose own work ran past its deadline goes at once; less than a period
late, the deadlines stay where they were (the next frame has that much less time), more than a
period late they are dropped and pacing starts again from now, so a slow stretch is never paid
for with a burst of frames that do not sleep. Under `[extras] frame_lock = gamecube` every late
frame starts again from now, because the console counts its retraces from the last picture shown.
These rules are the same on Android.

The sleep is a timer to just short of the deadline, then a spin on the clock to it. On Windows
the timer is a high-resolution waitable timer (Windows 10 1803 and later; an ordinary waitable
timer before that). How far short it aims follows how late the timer has been waking: the margin
rises at once to a late wake plus 0.1 ms and decays by 1/64 on every sleep, within 0.2 to 4 ms; a
wake later than 4 ms counts as 4 ms. Android keeps its single `SDL_DelayNS` (its sleep is precise,
and spinning keeps a phone's big core awake).

## The game's frame time

The game moves its timers and animations by each frame's time, measured from one pass of its loop
to the next (`zGameLoop`), as on the console. A frame that comes on time gets a steady time
instead (`iWindowGameFrameTime`, `src/SB/Core/pc/iSteadyFrame.h`):

- **On time** is within a quarter of the period that paced the frame: the sleep's (1/60 s at a cap
  of 60), one refresh where vsync holds the rate (Android at the display's rate), or two NTSC
  retraces under `[extras] frame_lock = gamecube`.
- Every such frame gets the same time: the average of the on-time frames over about 256 frames
  (4 s at 60), a ten-thousandth long. One frame's wobble and the next one's add up to whole
  periods, so they cancel out of the average; the short frame right after one that kept its
  measured time is left out, since without the late one it would pull the average down.
- **What that gives too much or too little is paid back.** A frame within the quarter is not
  always on time: a scene 10 to 25% over budget is within it, and so is the short frame after a
  late one that kept its measured time. So the steady time given less the measured time (the
  ten-thousandth aside) is kept as a running total, and once it passes three eighths of a period
  it goes into that frame's time at once. Three eighths rather than a quarter because on a screen
  0.1% slower than the cap the average's own settling adds up to a quarter of a frame, which
  should not come back as a jump.
- **A scene over budget gets its own steady time.** When the total passed three eighths with
  every frame since within the quarter, the frames were running at another rate rather than
  wobbling about the average, and the average moves to that rate at once (the total over those
  frames), instead of following it over several seconds and paying back every few frames.
- A frame more than a quarter off keeps its measured time, as a slow frame on the console takes
  two retraces. Under the 30 fps hold a late frame keeps it too, since the hold never follows it
  with a short one. With no cap (`framerate = off`) nothing paces the frames, and every frame keeps
  its measured time.
- The pacing and the pictures do not change: frames come at the cap or the display's rate as
  before. The game's clock runs about 6 ms a minute ahead of the wall clock (the ten-thousandth),
  and is otherwise never more than three eighths of a frame off it (3 ms at 120, 6 ms at 60),
  under any load.

**Why.** On the GameCube and the Xbox every frame is one NTSC retrace, 1001/60000 s. The game's
animations are made at 30 frames a second, so at exactly 60 or 120 their lengths are whole
frames, and an enemy leaves its looping hurt or death animation when `time + dt >= duration` sees
the end one frame ahead (`zNMETypeStdPopper.cpp`, `zNMETypeStdMervyn.cpp` and other enemies). With
measured times that end fell right on a frame boundary and microseconds of wobble decided it:
about one end in five played the animation once more (a Popper death 0.6 s longer, a Merv death
1 s longer, a Popper that stayed up and could be hit again). The console's frame is a thousandth
longer than 1/60 s, so there the end comes just inside a frame and is always seen (Dolphin: 0 of
164 ends over all the runs, the 9 scripted ones in the table below and the ones mashing the
button). With every on-time frame the same, the check sees the time the next frame moves by, and the
ten-thousandth puts each end just inside a frame, so it is seen on the frame the console sees it.

**Not chosen.** Pacing every cap on the console's clock (59.94 at a cap of 60, 119.88 at 120)
catches the ends too, but on a fixed-refresh screen with vsync it shows one picture twice about
every 17 s at 60 Hz and every 8 s at 120 Hz, and it changes nothing where the display paces the
game (Android). A steady time of the console's own length (1001/60000 s at 60) runs the game 0.1%
fast: about 37 ms between a 40 s cutscene's picture and its sound.

There is no setting: the frame rate is what it was, so a cap of 60 is still exactly 60.

**Measured (2026-11-21).** The test PC had no working screen, so vsync was not measured against a
real display: the Android-style pacing and the screens slower than the cap were emulated with a
wait for a fake refresh grid with up to 0.3 ms of wake jitter (scratch code only). Scripted
Popper and Merv fights in PT04 (Xbox files) and PT05 (GameCube files), struck every 90 frames,
x64 Release, 640x480; before is 76adaed run in the same hour. This table is of the first version
(57e9f30), which had no payback; the payback leaves on-time frames as they were, and a re-run
of cap 60, cap 120, the Android-style pacing and the 59.94 Hz screen (12 runs) gave the same:
0 extra loops in 168 ends, the same lengths.

| Setting | Runs (Xbox + GameCube files) | Extra loops / ends, before | Extra loops / ends, now | Lengths now (frames) |
|---|---|---|---|---|
| cap 60, vsync on | 5 + 5 | 2 / 111 (8 runs) | **0 / 137** | Popper 37, Merv 59, as in Dolphin |
| cap 120 | 5 + 5 | 8 / 53 (4 runs) | **0 / 140** | 75, 119 |
| display rate, vsync paces (as on Android; 120 Hz emulated) | 2 + 2 | 9 / 27 (earlier runs) | **0 / 56** | 75, 119 |
| `display` on the test PC (the backstop's 75 fps) | 2 + 2 | 12 / 53 (earlier runs) | **0 / 55** | 47, 74 |
| 59.94 Hz screen at a cap of 60 (emulated) | 2 + 2 | - | **0 / 53** | 37, 59 |
| 119.88 Hz screen at a cap of 120 (emulated) | 2 + 2 | - | **0 / 55** | 75, 119 |
| no cap (about 1500 fps) | 1 + 1 | 6 / 49 (earlier runs) | 1 / 20 | not covered |
| GameCube original in Dolphin (the 9 scripted runs) | 9 | | 0 / 122 | 37, 59 |

- **Pacing.** Unchanged: the same frame times before and after. On a modelled fixed-refresh screen
  fed the logged present times, runs without an outside stall repeat 0.8 to 1.4 pictures a minute
  at 60.000 Hz before and after, and 1.8 to 2.0 at 120.000 Hz.
- **The game's clock.** 99.9 to 100% of the frames got the steady time; the clock ran 2 to 3 ms
  ahead of the wall clock in 30 s at 60 and 120, 0.4 ms on the emulated 59.94 Hz screen.
- **Cutscene sync** (BB02's opening cutscene, 38.6 s): the picture moved 1.4 to 6.3 ms against its
  sound over the cutscene (cap 60 and 120, the emulated vsync pacing and slower screens, and
  1920x1080 with 4x MSAA).
- **story1.bik** follows its own audio clock: 2.5 ms against the wall, the picture 7 ms behind its
  timestamp at 120 (every 30 fps frame shown for 2 passes).
- **The 30 fps hold** (BB01, `frame_lock = gamecube`): 29.97 fps, every frame steady, the clock
  4.3 ms ahead in 43 s.
- **`tssm_steadyframetest`** checks the same on made-up frames: no extra loop and the console's
  frame at 60, 120, the 30 fps hold, a 59.94 Hz screen, late frames, and vsync waking the game up
  to 0.5 ms late (at 1 ms, at most one end in a thousand loops); the measured times at 60 do loop.
  It also runs scenes 10 to 20% over budget at 120 and 60 (one with a frame past the quarter now
  and then) and heavy work with a hitch every 2 s: the clock within three eighths of a frame of
  the wall clock at every frame, at most 6 paybacks in a loaded stretch, and no more extra loops
  than the measured times. The first version's arithmetic fails 8 of those checks (up to 767 ms
  behind, and 53 ms a minute ahead with the hitches).

**Under load (2026-11-21).** A scratch spin made every frame's work last a set time, as a scene
over budget would; one build, with the game's frame time switched between the measured time
(76adaed), the first version (57e9f30) and this one. BB02's opening cutscene (38.6 s), the
load from about 5 s to 28 s; the Popper and Merv fights loaded throughout.

| Case | Measured time | First version | Now |
|---|---|---|---|
| Cutscene, cap 120, frames at 115% of budget (104 fps): picture against its sound, furthest | -1.6 ms | **-317 ms** | +4.7 ms |
| the game's speed over any 0.25 s | 100% | 88.7 to 113.1% | 99.5 to 100.1% |
| Cutscene, cap 60, 50 fps (as a busy Switch): picture against sound, furthest | +3.9 ms | **-758 ms** | +4.7 ms |
| the game's speed over any 0.25 s | 100% | 83.8 to 118.4% | 98.8 to 101.3% |
| Cutscene, cap 120, work at 80% of budget and a 4 ms hitch every 2 s: picture against sound at the end | -2.2 ms | **+35.4 ms** | +5.0 ms |
| Fights at 115% of budget at 120: the game's clock against the wall, worst | 0 | 320 ms behind | 3.1 ms |
| extra loops / ends | 0 / 28 | 0 / 28 (lengths 66 to 71 frames) | 0 / 28 (66 and 104, every time) |
| Fights at 50 fps at a cap of 60: extra loops / ends | 0 / 28 | | 0 / 27 (31 and 49) |

The steady time is ahead of the wall clock by its ten-thousandth (3.9 ms over the cutscene); the
rest of "now" is within three eighths of a frame. In the cutscenes the payback came 4 to 6 times,
each one frame given about half a frame more or less than the others (a payback is the one
place the steady time steps, onto the scene's own rate when the load comes or goes); everywhere
else it moved by microseconds. At 1440x1080 with 4x MSAA and no load the
cutscene's picture moved 6.2 ms against its sound, as before. `regress --quick` passes 22 of 22.

## A stream's start

A stream (music, ambience, some voices) is kept three seconds ahead of the mixer, decoded. The
frame it starts in decodes one second. After that, each frame adds at least an eighth of a second,
in whole reads, until the stream is three seconds ahead (`ISND_STREAM_MUST_SEC`,
`ISND_STREAM_STEP_DIV`, `src/SB/Core/pc/iSnd.cpp`). A read is 8192 sample frames of Xbox ADPCM, or
as many sample frames of 16-bit PCM (a mod's sounds) or GameCube DSP-ADPCM as the buffers hold.
`TSSM_SNDMIX=1` logs each stream's lead once a second.

## Measuring

`TSSM_PERF=<seconds>` and `TSSM_PERF_CSV=<file>` (`src/SB/Core/pc/iPerf.h`) write one row per
frame. Besides the phases (update, render, present, swap, pace), the last seven columns are the
host's side: `pumps`, `pump_ms`, `pump_max_ms` (SDL event pumps in the frame; the window's is
timed with its events' handling), `pad_ms` (the frame's start to the controllers' read),
`input_age_ms` (how old the pump's state was then: about 1 us by design, since the read follows
its own pump, so it is not a measure of input latency), `pace_want_ms` (the sleep asked for) and
`pace_late_ms` (how far past its deadline the pacer let the frame go: the sleep's overshoot, or
the frame's overrun when it did not sleep).

An interval (`interval_ms`, flip to flip) is the previous frame's pace plus this frame's update,
render, present and swap. With the pacer on its deadline, a frame that does more work than the
one before it shows up as a longer interval, so the slow frames are split by which of those grew.

## What was measured (2026-11-18)

1920x1080, 4x MSAA, a 120 fps cap, borderless, `backend = auto` (D3D9 in this build), x64 Debug;
BB02, SC02, DE01 and TR01 booted straight into, each with a scripted walk; 30 s measured per run
(3600 frames); vsync off and on; three runs of each build, the two builds alternating. The
display was 3840x2160 at 60 Hz, where D3D9's windowed flip does not wait for the display even
with vsync on, so both settings ran at the cap: a vsync that waits was not measured. A frame held
by one is late and goes at once, without a sleep, in either build.

Before the change:

- **Three event pumps a frame** (the present, the pad read, the keyboard read), 7 us in all in a
  quiet frame.
- **The pacer** (`SDL_DelayPrecise`, 1 ms steps) let frames go up to 0.5 to 1.0 ms past their
  deadline, 0.1 to 0.25 ms at the 99th percentile.
- **Most slow frames were the frame's own work**: the update phase, and D3D9's flip waiting 0.4 to
  2 ms.
- **The biggest in-game spikes were sound decoding on the game thread** (a stall sampler on the
  game thread): a stream's start (music, some voices) decoded three seconds of Xbox ADPCM at once,
  and a sound's first use all of it, unoptimised in a Debug build: 3 to 7 ms, and part of the 14 to
  22 ms frames in SC02 and TR01.

The change: the pacer as above; `iSndXbox.cpp` and `iSndDsp.cpp` are optimised in a Debug build
too (integer code, the same samples); a stream's start as above. This table was measured with one
event pump a frame (see "A frame"), before the pumps went back to two; the gains are the pacer's
and the sound's.

Before / after, pooled over the runs with no outside stall (a frame that waited more than 30 ms
on the flip, on the event pump or blocked in the update phase without running; 2 to 3 of the 3
runs, 1 of 3 for TR01 with vsync on):

| Scene | vsync | median ms | 1% low fps | 0.1% low fps | frames over 12.5 ms | worst ms |
|---|---|---|---|---|---|---|
| BB02 | off | 8.33 / 8.33 | 105.4 / 107.1 | 92.0 / 101.5 | 0 / 0 | 12.2 / 10.1 |
| BB02 | on | 8.33 / 8.32 | 105.1 / 106.8 | 88.8 / 99.5 | 1 / 0 | 12.7 / 10.7 |
| SC02 | off | 8.33 / 8.32 | 86.3 / 95.4 | 44.8 / 50.6 | 22 / 10 | 23.6 / 21.1 |
| SC02 | on | 8.33 / 8.32 | 86.6 / 94.8 | 45.5 / 50.7 | 14 / 9 | 23.6 / 21.1 |
| DE01 | off | 8.33 / 8.33 | 104.6 / 107.6 | 83.6 / 101.8 | 1 / 0 | 17.5 / 10.0 |
| DE01 | on | 8.33 / 8.33 | 107.8 / 108.0 | 95.6 / 102.0 | 1 / 0 | 12.5 / 10.2 |
| TR01 | off | 8.33 / 8.32 | 86.0 / 90.7 | 35.8 / 38.9 | 11 / 6 | 30.5 / 29.0 |
| TR01 | on | 8.33 / 8.33 | 87.0 / 90.0 | 32.9 / 35.2 | 5 / 3 | 31.0 / 29.2 |

The pacer now leaves frames 0.04 to 0.12 ms past their deadline at the 99th percentile, and
the pacer's CPU time is the same as before (0.31 ms a frame).

Measured again with two pumps a frame and the margin as it is now: two runs of each of the three
builds (before, one pump, two pumps), with the TV's audio dropping two to five times in every run,
so with the frames within 2 s of each outside stall left out. Two pumps cost 0.007 ms a frame
(one: 0.004, three: 0.007). The 1% and 0.1% lows of the two-pump build were at or above the
one-pump build's in 7 of the 8 sets (not DE01 with vsync on) and above the old build's in 7 (not
TR01 with vsync on), but the audio drops spread these numbers more than the two newer builds
differ. The pacer left frames 0.04 to 0.11 ms past their deadline at the 99th percentile (old
build 0.12 to 0.27), at 0.30 to 0.31 ms of CPU a frame (old build 0.30 to 0.33).

A mod's 16-bit PCM in place of SC02's ambience stream: one read a frame kept it 1.00 s ahead
(median), the eighth of a second a frame 3.09 s, the same as the Xbox ADPCM streams.

What is left:

- **A scene's reset.** The walk falls out of SC02 and TR01 three times a run, and the game's own
  reset (`zSceneReset`: the enemies' resets and their ray casts) is 10 to 20 ms of unoptimised
  game code in that frame. The original game's work, the same in both builds; it sets the 0.1%
  lows there.
- **D3D9's flip**, which waits 0.4 to 2 ms a frame, and now and then 50 to 100 ms (outside
  stalls).
- **An audio device coming and going** (a TV's HDMI audio, here every 15 to 20 s in some runs):
  each time, the event pump stalls for 0.2 to 1.8 s inside SDL -- SDL's main-thread handling of
  the lost device waits on the device's lock (`SDL_AudioDeviceDisconnected_OnMainThread`) while
  its thread is held up in WASAPI, Windows' message loop (`PeekMessage`) waits, and SDL's HID
  layer enumerates every device again (`HIDAPI_UpdateDeviceList`). Both builds alike: 10.7 s of
  such stalls in three runs each. Not the port's code, and not shorter with fewer pumps a frame:
  the stall is the length of SDL's and Windows' work.
