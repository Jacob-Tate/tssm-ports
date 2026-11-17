#ifndef IAUDIOSDL_H
#define IAUDIOSDL_H

#include <types.h>

// The SDL audio subsystem as the port's two audio devices share it: the game's
// mixer (iSndHostSDL.cpp) and the movie device (iFMVAudioSDL.cpp).
//
// **A device that SDL has lost is only got back by closing every device and
// taking SDL_INIT_AUDIO down to zero.** On Android SDL has one playback device,
// the default, and nothing to migrate to: when that device fails, SDL keeps it
// as a "zombie" that consumes audio and plays none, sends no event for the
// devices opened on it, and pretends to open it again when asked (SDL_audio.c,
// OpenPhysicalAudioDevice). And when the AAudio stream it paused in the
// background will not start again, SDL drops the error (SDL_aaudio.c,
// ResumeOneDevice) and the device simply never asks for audio again. Either way
// the sound is gone until the game restarts, which is what a phone did after
// being put away and brought back.
//
// So this file restarts the devices itself:
//
// - In the background no device is open. On SDL_EVENT_WILL_ENTER_BACKGROUND,
//   which SDL sends on the game thread just before it pauses the audio and
//   blocks, every user closes; on SDL_EVENT_DID_ENTER_FOREGROUND they open
//   again from scratch. Nothing SDL did to a paused stream can matter, and no
//   sound plays while the game is hidden.
// - A playback device removed (SDL_EVENT_AUDIO_DEVICE_REMOVED): a restart when
//   SDL will not move the game off it. That is one of the game's own devices
//   given up on, and on a driver that cannot move devices (Android's), any
//   playback device. Where SDL moves both of the game's devices onto the new
//   default itself (Windows, macOS, PulseAudio, PipeWire), a removal restarts
//   nothing unless, two seconds later, SDL has left the game's sound going
//   nowhere: no playback device is left, or SDL still holds a removed device,
//   which means the game's sound is still on it (SDL could not open the new
//   default, or none was named).
// - A user that sees its device stop asking for audio (a stall with no event)
//   asks for a restart with iAudioSDLRestart.
// - A restart that cannot open a device is tried again later, after 1, 2, 4,
//   8, 16 and then every 30 seconds, if a device has ever been open.
// - A device that keeps failing as soon as it opens is not restarted every
//   frame: from the fourth restart within two seconds of the one before, the
//   next waits 1, 2, 4... up to 30 seconds (the foreground excepted).
//
// A user is a function that closes everything it has, including its
// SDL_INIT_AUDIO reference (`up` false), or opens it again (`up` true; true if
// it has what it needs). Users are only ever called on the game thread.
typedef bool (*iAudioSDLUser)(bool up);

// `opened`: whether the user has just opened its device, which tells this file
// that the machine has audio to lose.
void iAudioSDLAttach(iAudioSDLUser user, bool opened);
void iAudioSDLDetach(iAudioSDLUser user);

// From the game thread, often: carries out a restart that is due.
void iAudioSDLPoll();

// Reasons for iAudioSDLRestart, for the log.
enum iAudioSDLReason
{
    iAUDIOSDL_NONE = 0,
    iAUDIOSDL_FOREGROUND,
    iAUDIOSDL_REMOVED,
    iAUDIOSDL_STALLED,
    iAUDIOSDL_RETRY
};

// Asks for every user to be closed and opened again at the next poll. Any
// thread.
void iAudioSDLRestart(iAudioSDLReason why);

// The restarts carried out so far, for tests and the log.
U32 iAudioSDLRestarts();

// One line of a close or open of every device ("tssm: audio: step: ..."),
// from a user while this file closes or opens it, and nothing at any other
// time. On the 3DS each line is synced to the card at once, so a close or
// open that never ends shows the step it stopped in.
void iAudioSDLStep(const char* fmt, ...);

#endif
