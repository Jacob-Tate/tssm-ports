# Frame pacing (PC)

How a frame is laid out on the PC, what the frame-rate cap does, and what was measured when the
pacing was reworked (2026-11-18).

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
