#ifndef IAUDIOSDLTEST_H
#define IAUDIOSDLTEST_H

#include <types.h>

// PC-only test hook for unattended runs: what leads to the audio restart of
// iAudioSDL.h (a playback device's removal, a device that stops taking audio,
// the app put away), at a chosen moment.
//
//   TSSM_TEST_AUDIO_RESTART=<entry>[,<entry>...]
//
//   entry    <moment>[+<ms>][*<count>[/<gap>]][@<how>]
//
//   moment   f<n>              presented frame n (iCapture.h's count)
//            movie[/<name>]    a movie's sound opening: the first movie, or the
//                              first whose file name contains <name> (any case)
//            drained[/<name>]  that movie's sound running out; the last frames
//                              of its picture are still to play
//            closed[/<name>]   that movie closed, the game carrying on
//            load[/<name>]     a game package (.HIP/.HOP) opening after the
//                              first frame, or the first whose path contains
//                              <name>
//   +<ms>    that many milliseconds after the moment (0)
//   *<count>[/<gap>]  that many times, <gap> ms apart (once; 100 ms)
//   @<how>   what is simulated, each time (event):
//            event   SDL_EVENT_AUDIO_DEVICE_REMOVED for the (physical) device
//                    the game plays on, pushed on the game thread inside
//                    SDL_PumpEvents as SDL pushes its own, and nothing else:
//                    SDL's devices untouched. It restarts the audio at once
//                    where iAudioSDL.cpp restarts on a removed device, which
//                    is not on a driver that moves the game's devices itself
//                    (WASAPI); there two seconds later, since SDL still holds
//                    the device the event names (iAudioSDL.cpp,
//                    CheckRemoved). drop and stall restart at once on every
//                    driver.
//            lost    what SDL's WASAPI backend does when Windows reports the
//                    device the game plays on gone (WASAPI_DisconnectDevice,
//                    from a thread standing in for Windows' notification
//                    thread): SDL takes the device as lost and sends the event
//                    itself, from the game thread's next SDL_PumpEvents.
//            swap[:<name>][~<ms>|~r]
//                    lost, and then what SDL does when Windows names a new
//                    default device: SDL_DefaultAudioDeviceChanged on SDL's
//                    WASAPI management thread, which moves the game's devices
//                    onto it -- the first other playback device, or the first
//                    whose name contains <name>; ~<ms> waits that long between
//                    the two (0), ~r until the game thread has begun a restart
//                    (iAudioSDLRestarts; at most three seconds), so that SDL's
//                    move and the game's restart overlap. On WASAPI that is
//                    the restart two seconds after the loss, since SDL still
//                    holds the lost device until the new default comes. The
//                    game's devices are set to gain 0 first, so the other
//                    device plays silence.
//            swapfail[~<ms>|~r]
//                    lost, and then a new default that SDL cannot open, as
//                    when Windows names one that another program holds in
//                    exclusive mode: a playback device whose open fails at
//                    once ("TSSM test: a default that will not open") is
//                    added to SDL's list and made the default, and SDL,
//                    unable to open it, leaves the game's devices on the lost
//                    one (SDL_DefaultAudioDeviceChanged). ~<ms> and ~r as for
//                    swap. The added device lasts until SDL's audio starts
//                    again.
//            default[:<name>][~r]  the new default alone, with no loss (a
//                    device plugged in and made the default); ~r waits for the
//                    next restart, whatever starts it (one an entry due at the
//                    same moment, listed before this one, starts counts, even
//                    if it has already begun). Gain 0 the same way.
//            other[:<name>]    the loss of a playback device the game does not
//                    play on (the first other one, or by name).
//            drop    SDL_EVENT_AUDIO_DEVICE_REMOVED for each of the game's own
//                    devices (the logical ones it opened), pushed on the game
//                    thread inside SDL_PumpEvents: what SDL sends when it gives
//                    up on a device the game opened and will not move it.
//            stall   iAudioSDLRestart(iAUDIOSDL_STALLED) from the test's
//                    thread: what the mixer or the movie asks for when its
//                    device has taken no audio for a second.
//            alllost every playback device SDL knows taken as lost, as lost
//                    does to one (no device left, and no new default): what
//                    SDL sees when the only output goes away. The game's
//                    devices are set to gain 0 first.
//            bg[~<ms>]  what Android does to the game thread when the app is
//                    put away and brought back: SDL_EVENT_WILL_ENTER_BACKGROUND
//                    and _DID_ENTER_BACKGROUND pushed on the game thread inside
//                    SDL_PumpEvents (iAudioSDL.cpp closes every device there),
//                    the thread held there for <ms> (1000) as Android's
//                    block-on-pause holds it, then _WILL_ and
//                    _DID_ENTER_FOREGROUND (a restart at the next poll).
//                    AAudio's own pause and resume are not part of it.
//
//   Paired with default~r (e.g. movie/story1+2000@stall,movie/story1+2000@
//   default:steam~r), drop and stall start a restart while SDL moves the
//   game's devices onto a new default, whatever the restart rule is.
//
// lost, swap, swapfail, default, other and alllost need SDL's internals, which
// only a Windows build with SDL from third_party has (iAudioSDLTestSDL.c);
// elsewhere they are done as event, and the log says so. While the variable is set,
// every audio device event SDL sends is logged with the device's name, and
// each entry is logged when it fires ("tssm: test: audio: ...").
//
// While it is set the game thread also logs, about once a second, what the
// sound is doing ("tssm: test: audio: level ..."): the mixer's callbacks since
// the last line, the loudest sample it sent SDL in that time, and the device
// it plays on; and while a movie is open, the frames its device has taken
// since it last opened (or "stand-in" while it has none), what is queued, and
// the device. A device is named by the physical device it is on, with
// " [lost]" when SDL has taken that one as lost and the game's sound goes to
// its stand-in, which swallows it and keeps time (SDL internals only). A
// device that comes back after a restart shows as callbacks and taken frames
// moving again on a device that is not [lost]; level sound under a movie shows
// as a mixer peak above zero while the movie plays.
//
// Nothing outside the process changes: no Windows setting, device, default or
// volume. A device "lost" is lost only to SDL's bookkeeping in this process.
// Where SDL moves the game's devices itself (WASAPI), the game restarts its
// audio two seconds later only if SDL left its sound on the lost device (lost
// alone, swapfail, swap with ~r), and SDL's audio starting again brings the
// device back; after a swap SDL completed, the device stays lost until SDL's
// audio starts again (the game's next run, or a restart something else
// starts). Nothing happens unless the variable is set.
//
// Examples: TSSM_TEST_AUDIO_RESTART=movie/story1+3000@swap:Steam (three seconds
// into story1.bik, as when the default device goes and Windows picks another);
// =movie+500*6/300 (six restarts 300 ms apart from half a second into the first
// logo, which is the quick-restart hold); =load/bb02+200@lost.

// iCapture.cpp: read the variable (the game thread, before the window opens).
void iAudioSDLTestInit();

// Whether TSSM_TEST_AUDIO_RESTART is set with at least one valid entry.
bool iAudioSDLTestOn();

// iCapture.cpp: every presented frame (1 is the first), on the game thread.
void iAudioSDLTestFrame(S32 frame);

// iFMV.cpp: the movie at `path` has opened its sound (0), run out of sound
// (1) or closed (2).
void iAudioSDLTestMovie(const char* path, S32 what);

// iFile.cpp: a game package is opening.
void iAudioSDLTestLoad(const char* path);

// The users' side of the level lines, on the game thread (with SDL audio
// only: iSndHostSDL.cpp, iFMVAudioSDL.cpp).
//
// The mixer: callbacks so far, the loudest sample sent to SDL since the last
// call (the meter runs only once iSndHostSDLTestMeter has turned it on), and
// the logical device it plays on (0: none).
void iSndHostSDLTestMeter(bool on);
void iSndHostSDLTestStats(U32* callbacks, float* peak, U32* device);

// The movie: false when no movie has its sound open. Otherwise the frames its
// device has taken since it last opened, the frames queued, whether it is on
// the stand-in queue (no device), and the logical device (0: none).
bool iFMVAudioSDLTestStats(U32* taken, U32* queued, bool* standin, U32* device);

#endif
