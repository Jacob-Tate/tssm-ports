#ifndef IMODAUDIO_H
#define IMODAUDIO_H

#include <types.h>

#include <string>
#include <vector>

// PC-only: a mod's replacement sound, decoded to what the game's sound tables
// can describe. New code.
//
// The Xbox SNDI entry is a WAVEFORMATEX, and the sound layer already plays
// 16-bit PCM entries (treedome's iSndData reads both PCM and Xbox ADPCM), so a
// replacement is decoded once, when its package opens, to interleaved 16-bit
// little-endian PCM at the file's own rate, mono or stereo. iModPackage puts the
// samples in place of the SND/SNDS asset and rewrites the SNDI entry.
//
// WAV (PCM 8/16/24/32-bit, float) is read here. With FFmpeg in the build -- the
// one the movies use -- anything FFmpeg decodes is accepted too: OGG Vorbis,
// Opus, FLAC, MP3 and so on. More than two channels are mixed down to stereo.

struct iModPcm
{
    std::vector<U8> bytes; // interleaved S16LE
    U16 channels; // 1 or 2
    U32 rate;
};

// False, with the reason in *error, when the file cannot be decoded.
bool iModDecodeAudio(const char* path, iModPcm* out, std::string* error);

// "WAV" or "WAV + FFmpeg", for the startup log.
const char* iModAudioDecoders();

#endif
