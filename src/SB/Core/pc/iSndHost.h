#ifndef ISNDHOST_H
#define ISNDHOST_H

#include <types.h>

// PC-only, and the same shape as iPadHost.h for the same reason: the GameCube
// has one audio API that is always present, so its iSnd.cpp talks to AX and
// MIX directly. A host has several possible audio libraries and none of them is
// guaranteed to exist at build time, so the part of iSnd that would touch a
// device is behind this seam and the part with the game's semantics in it --
// the voice table, the handles, the priority rules, the sound lookup -- is not.
//
// This is deliberately NOT where the game's model lives. A backend is told
// "start this many samples at this rate on this voice" and nothing about
// streams, categories, owners or asset IDs.
//
// TSSM-ADAPTED: treedome's seam plus a reverb send per voice, a grouped pause
// and fed voices (the end of this file), for TSSM's per-channel cave reverb
// and its streamed cutscene tracks.

#define ISNDHOST_MAX_VOICES 64

struct iSndHostSample
{
    // The samples themselves, interleaved, or NULL when the caller could not
    // find them. A backend that can play is expected to fall back to timing the
    // voice out silently rather than refusing it: the game waits on sounds it
    // cannot hear just as hard as on ones it can, and one missing asset must
    // not stall a cutscene.
    //
    // The memory belongs to the caller and stays alive until the voice is
    // stopped or released, which is what iSnd's sample cache guarantees. A
    // backend must not free it, and must not read it after iSndHostStop.
    const void* data;
    U32 bytes;

    // 1 or 2, and 8 or 16. Every asset the disc ships is 16-bit mono, but the
    // table says so per entry rather than by convention, and a soundtrack
    // override is routinely stereo -- so the backend is told rather than
    // assuming, and must read both channels when there are two.
    U32 channels;
    U32 bits;

    U32 sample_rate;
    U32 num_samples;

    // Loops restart at loop_start rather than ending. num_samples still gives
    // the length of one pass, which is what a silent backend paces on.
    bool looping;
    U32 loop_start;

    // Where a loop turns round, when that is not the end of the data. Zero
    // means the end, which is every sound the disc ships. A soundtrack override
    // sets it: the file is the same music with a proper ending on it, and the
    // game wants the console's seam rather than that ending. Ignored when the
    // voice is not looping, and ignored if it is past num_samples.
    U32 loop_end;
};

// The environmental reverb, as I3DL2 gives it. A backend that has no reverb
// ignores this; one that has must not change what the dry path does, because
// the game mixes and pans every voice itself and expects to hear that mix.
//
// The four level fields are in millibels -- hundredths of a decibel -- and two
// of them are relative to `room`, which is how I3DL2 defines them: the early
// level is room + reflections, the late level is room + reverb. Kept in the
// interface's own units rather than converted here, so that a set of
// parameters in this port can be read straight against the ones a console
// build pushes onto its stack.
//
// This is the seam's one deliberate exception to knowing nothing about the
// game's model, and it is the smallest one available: the alternative is an
// effect id, which would put a reverb design into every backend, or a GameCube
// DSP preset index, which would mean nothing to any of them.
struct iSndHostReverb
{
    S32 room;              // mB, the room effect's level
    S32 room_hf;           // mB, its extra attenuation at high frequency
    F32 room_rolloff_factor;

    F32 decay_time;        // seconds, RT60
    F32 decay_hf_ratio;    // >1 means high frequencies ring longer

    S32 reflections;       // mB, relative to room
    F32 reflections_delay; // seconds, from the direct sound

    S32 reverb;            // mB, relative to room
    F32 reverb_delay;      // seconds, from the first reflection

    F32 diffusion;         // per cent
    F32 density;           // per cent
    F32 hf_reference;      // Hz, where the HF fields start to apply
};

void iSndHostInit();
void iSndHostExit();

// Acquires a device voice, or -1 if none is free. Lower `priority` is more
// important, matching AXAcquireVoice, which the GameCube path feeds from the
// game's 0-255 priority shifted right by three.
S32 iSndHostAcquire(U32 priority);
void iSndHostRelease(S32 voice);

void iSndHostStart(S32 voice, const iSndHostSample* sample);
void iSndHostStop(S32 voice);
void iSndHostPause(S32 voice, bool paused);

// Linear 0..1 per side. The GameCube path converts to AX's log scale inside
// iSnd; a backend gets the linear value because that is what audio libraries
// take, and converting twice would be lossy.
void iSndHostSetVol(S32 voice, F32 left, F32 right);

// 1.0 is the sample's own rate.
void iSndHostSetPitch(S32 voice, F32 pitch);

bool iSndHostIsPlaying(S32 voice);

// Once per frame, from iSndUpdate. A backend that finishes voices on its own
// clock retires them here.
void iSndHostUpdate();

// Applies an environmental reverb, or removes it when `params` is NULL. Called
// from the game thread when a scene loads, which is the only time the game
// changes it.
//
// TSSM: the reverb is a send, not an effect on the whole mix. The console's
// FMOD applies it per channel (gc/iSnd.cpp's iSndPlayDone raises a voice's aux
// level only for the categories xSndMgr marks with 0x80000), so music and the
// menus stay dry while a cave echoes. A voice reaches it only through
// iSndHostSetSend.
void iSndHostSetReverb(const iSndHostReverb* params);

// TSSM: whether this voice feeds the reverb. Off when a voice is acquired.
void iSndHostSetSend(S32 voice, bool send);

// TSSM: a level on top of iSndHostSetVol's for a whole group of voices, which
// is what the Settings tab's volumes are (iSnd.cpp). A voice is in the effects
// group until told otherwise. 1 changes nothing, down to the bit, and is where
// every group starts. Any thread, and no lock: a backend's mixer must not wait
// for this, and moves to the new level by itself, smoothly.
//
// The reverb's send bus counts as the effects group, because only effects
// reach it (xSndMgrDoesEffects), and is scaled after the reverb, so that the
// tail follows the level too.
enum iSndHostGroup
{
    ISNDHOST_GROUP_EFFECTS = 0,
    ISNDHOST_GROUP_MUSIC,
    ISNDHOST_GROUP_MOVIE,
    ISNDHOST_GROUP_COUNT
};

void iSndHostSetGroup(S32 voice, iSndHostGroup group);
void iSndHostSetGroupGain(iSndHostGroup group, F32 gain);

// TSSM: pauses or resumes several voices under one lock, so that a stereo pair
// started as two voices begins on the same output frame. Voices that are not
// playing are skipped, as by iSndHostPause.
void iSndHostPauseGroup(const S32* voices, S32 count, bool paused);

// ---------------------------------------------------------------------------
// Fed voices (TSSM)
//
// A voice whose samples arrive while it plays, for audio that is not one
// block in memory: a cutscene's track comes off the disc a time chunk at a
// time, inside the same reads as its animation (gc/iSnd.cpp feeds FMOD a
// stream from a callback for the same reason).
//
// The backend keeps a ring of `capacity` frames. iSndHostFeed copies into it
// and takes what fits. When the ring runs dry the voice holds -- it plays
// silence and its position does not move -- until more arrives, so a late
// chunk is heard late rather than not at all. It plays until stopped, or,
// after iSndHostFeedEnd, until what was fed has been played.
//
// Frames are 16-bit PCM, `channels` interleaved. The voice starts paused when
// `paused` is set (iSndHostPause or iSndHostPauseGroup starts it), so a track
// can be primed before its first frame is due.
void iSndHostStartFeed(S32 voice, U32 channels, U32 sample_rate, U32 capacity, bool paused);
U32 iSndHostFeed(S32 voice, const S16* frames, U32 count);
U32 iSndHostFeedQueued(S32 voice);
void iSndHostFeedEnd(S32 voice);

// Names the backend that was linked in, for the startup log.
const char* iSndHostName();

#endif
