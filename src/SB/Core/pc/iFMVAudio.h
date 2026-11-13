#ifndef IFMVAUDIO_H
#define IFMVAUDIO_H

#include <types.h>

// The audio device a movie plays through, which is deliberately NOT the game's.
//
// The game's mixer stays open during a movie: iFMVPlay pauses the game's
// voices around it (xFMVPauseSound, as the console does), so the mixer runs on
// but plays nothing. The voice table behind iSndHost.h is also the wrong shape
// for a movie: that interface hands a backend a finished sample and asks it to
// play it, where a movie produces audio a fragment at a time for as long as it
// runs.
//
// So a movie opens its own output, owns it for its duration, and closes it
// before the game's voices are unpaused. Nothing is shared between the two.
//
// Queued() is what makes the picture line up with the sound. A movie is paced
// on its AUDIO clock -- the amount that has actually left the device -- because
// that clock is the one the player hears and cannot be adjusted without them
// noticing. The video is fitted to it. Pacing the other way round, on a frame
// timer, drifts against the device's real rate and ends with lips out of sync.

// FALSE if there is no device or the format is refused, which is not fatal:
// iFMV.cpp then plays the movie silently on a wall clock rather than not at all.
S32 iFMVAudioOpen(U32 sample_rate, U32 channels);
void iFMVAudioClose();

// The movie's level, 0..1, from two places that multiply: the Settings tab's
// (master times movies, whenever it moves) and the game's own, which the console
// gives its movie player once at the start (xFMVGetBinkCompliantVolume) and which
// is set here before iFMVAudioOpen. Both on the thread that plays the movie. It
// applies to the frames handed over from then on, so a change is heard when what
// is already queued has played. At 1 and 1 the frames go through untouched.
void iFMVAudioSetVolume(F32 volume);
void iFMVAudioSetGameVolume(F32 volume);

// Hands over interleaved 16-bit frames. Returns how many were accepted, which
// is fewer than asked for when the device's buffers are full -- the caller
// keeps the rest and offers them again.
U32 iFMVAudioWrite(const S16* frames, U32 num_frames);

// Sample frames submitted but not yet played. The audio clock is everything
// written minus this.
U32 iFMVAudioQueued();

// Names the backend, for the startup log.
const char* iFMVAudioName();

#endif
