#ifndef ISNDMETER_H
#define ISNDMETER_H

#include <types.h>

// PC-only: sound levels in the log, for checking audio where nobody listens
// (an emulator, a console run). TSSM_SND_METER=1 turns it on; otherwise every
// call returns at once.
//
// Each source counts the frames it hands on and the loudest sample among them.
// Once a second the game thread prints one line: per source the frames and the
// peak (0..1), plus whatever clock the caller adds (a movie's audio clock
// against its picture, a cutscene's track against its time).
//
// Sources are counted from any thread: the movie and the game's sound from the
// game thread, the mixes and outputs from the audio threads.

enum iSndMeterSource
{
    iSNDMETER_MOVIE, // decoded movie audio accepted by the movie's device
    iSNDMETER_MOVIE_OUT, // what the movie's device mixed (SDL's post-mix)
    iSNDMETER_CUTSCENE, // cutscene track PCM fed to its voice
    iSNDMETER_STREAM, // streamed sound (music, dialog) fed to its voice
    iSNDMETER_MIX_FEEDS, // frames the mixer took from fed voices
    iSNDMETER_MIX_SOUNDS, // frames the mixer took from sound voices
    iSNDMETER_OUT, // the game mixer's output

    iSNDMETER_COUNT
};

bool iSndMeterOn();

// Counts `frames` interleaved frames of `channels` and their peak.
void iSndMeterS16(S32 source, const S16* data, U32 frames, U32 channels);
void iSndMeterF32(S32 source, const float* data, U32 frames, U32 channels);

// Counts frames without a level (data the caller does not have to hand).
void iSndMeterFrames(S32 source, U32 frames);

// Game thread: prints the line when a second has passed since the last one
// and returns true. `clock` (may be NULL) is appended.
bool iSndMeterTick(const char* clock);

#endif
