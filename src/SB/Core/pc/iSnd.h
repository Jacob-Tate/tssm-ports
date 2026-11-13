#ifndef ISND_H
#define ISND_H

// PC iSnd.h: gc/iSnd.h's interface over the Xbox sound tables.
//
// Shared code sees three things of the platform's sound structs, and those keep
// their console shape: xSndVoiceInfo embeds an iSndInfo and xSndMgr_SetInaudible
// clears its `channel`; iSndLookup hands back an iSndFileInfo that xSndMgr keeps
// in the voice and reads `uFlags` (0x2 streamed, 0x1 looping), isStreamed() and
// isLooping() from for as long as the voice lives. What is behind them -- the
// console's FMOD stream handle, the FSB bank indices of gcWavInfo, the DSP-ADPCM
// headers of gcSoundInfoAsset -- describes data the Xbox disc does not have, and
// is replaced by the PC's (iSnd.cpp). iSndGroupHandle is an integer (below).

#include <types.h>
#include "iTime.h"

struct xSndVoiceInfo;
struct tag_xFile;
struct _AXVPB;
struct sDSPADPCM;

struct iSndVol
{
    S16 volL;
    S16 volR;
};

// PC: the platform half of a voice. `channel` is the voice's own index in
// gSnd.voice while a host voice plays it, and -1 otherwise. Voices are copied
// between slots by value (xSndMgr_SetInaudible / SetAudible), so iSnd.cpp
// keeps the host state in its own table, keyed by slot, and treats `channel`
// as the game's bookkeeping only.
struct iSndInfo
{
    S32 channel;
};

// PC: one sound of a loaded sound table, as iSndLookup returns it. xSndMgr
// holds the pointer for the voice's life, so it points into the table, which
// lives until iSndSceneExit (as gc/iSnd.cpp's pWavInfoArray does).
struct iSndFileInfo
{
    U32 uAssetID;
    U8 uFlags; // 0x2: streamed (an SNDS asset), 0x1: looping (the table's loop flag)
    U8 uTableIndex; // which loaded table
    U16 uEntryIndex; // which entry of it

    bool isStreamed()
    {
        return (uFlags >> 1) & 1;
    }

    bool isLooping()
    {
        return uFlags & 1;
    }
};

// Size: 0x20
// This was not in dwarf data
struct vinfo
{
    _AXVPB* voice;
    S32 buffer[4];
    U32 _0x14;
    S32 buffer2[2];
};

// not in dwarf data,
enum isound_effect
{
    iSND_EFFECT_NONE,
    iSND_EFFECT_CAVE
};

// PORT: an empty enum on the console, holding a pointer to the sound group's
// asset (xSndMgrGetSoundGroup casts xSTFindAsset's answer to it) or -1. C++ does
// not allow a pointer cast to an enum, so on PC it is an integer. 32 bits, not
// a pointer's width: it shares a union with a U32 hash in two asset structs
// (xEntBoulder, xSoundFXAsset), whose layout is the file's. Asset memory is in
// the low-4 GB game arena, so the pointer fits.
typedef U32 iSndGroupHandle;

struct zNMESoundTable
{
    iSndGroupHandle sndGroupHandle[10];
    char* assetNames[10];
    F32 timer[10];
    F32 time[10];
    S32 flags[10];

    // No GC symbol (always inlined into zNMECommon::SndPlayFromTable; the name is ours): entry
    // idx exists and its repeat timer has run out.
    S32 IsReady(S32 idx) const
    {
        if (sndGroupHandle[idx] == -1)
        {
            return 0;
        }
        return iTimeGetGame() >= timer[idx];
    }
};

enum iSndHandle
{
};

void arq_callback(long);
void iSndExit();

void iSndPause(xSndVoiceInfo* vp, U32 pause);
void iSndSetEnvironmentalEffect(S32 effectType);
void iSndInit();
void iSndCalcVol(xSndVoiceInfo* xSndVoiceInfo, vinfo* vinfo);
void iSndCalcVol3d(xSndVoiceInfo* xSndVoiceInfo, vinfo* vinfo);
void iSndVolUpdate(xSndVoiceInfo* info, vinfo* vinfo);
void iSndUpdateSounds();
void iSndUpdate();
void iSndSuspendCD(U32);
void iSndSuspend();
void iSndResume();
void iSndKill();
void iSndMessWithEA(sDSPADPCM* param1);
U32 SampleToNybbleAddress(U32 sample);
void iSndInitSceneLoaded();
S32 iSndIsPlaying(U32 assetID);
S32 iSndIsPlaying(U32 assetID, U32 parid);
void iSndWaitForDeadSounds();
void iSndSceneExit();
void sndloadcb(tag_xFile* tag);
S32 iSndLoadSounds(void*);
void iSndSetExternalCallback(void* func_ptr);
void iSndAXFree(_AXVPB** param1);
void iSndStartStereo(iSndHandle id1, iSndHandle id2);
bool iSndPlay(xSndVoiceInfo* vp);
void iSndStop(xSndVoiceInfo* vp);
void iSndSetVol(xSndVoiceInfo* vp, F32 vol);
void iSndSetPitch(xSndVoiceInfo* vp, F32 pitch);
F32 iSndGetVol(U32 snd);
void iSndStereo(U32 stereo);
void iSndEnableEffects(bool enable);
U8 iSndIsReady(iSndHandle id);
void iSndSetMode();
xSndVoiceInfo* iSndGetFreeVoiceInfo(U32 priority, U32 flags);
iSndFileInfo* iSndLookup(U32 id);

// ---------------------------------------------------------------------------
// PC-only

// stubs/fmod.cpp: FSOUND_AUXFX_ShutdownEffect, which xSndMgrStopSounds calls
// when every sound stops, turns the reverb off until a voice asks for it again.
void iSndEffectShutdown();

// pc/iCutscene.cpp: the next block of the current cutscene's track `assetID`,
// once the chunk holding it has been read in; NULL until then. It advances the
// track (xCutscene::SndChannelReq), so each block comes back once. The block
// points into the cutscene's read buffer and is good until the next chunk
// swap: copy it at once. gc/iCutscene.cpp's iCSSoundGetData is the console's
// form, which FMOD's stream callback calls as the stream runs dry.
void* iCSSoundPull(U32 assetID, U32* size);

#endif
