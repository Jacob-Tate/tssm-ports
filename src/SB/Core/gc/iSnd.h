#ifndef ISND_H
#define ISND_H

#include <types.h>
#include "iTime.h"

struct xSndVoiceInfo;
struct tag_xFile;
struct _AXVPB;

struct iSndVol
{
    S16 volL;
    S16 volR;
};

typedef struct FSOUND_STREAM FSOUND_STREAM;
typedef struct FMUSIC_MODULE FMUSIC_MODULE;

// GC version (FMOD based): the stream from FSOUND_Stream_Create at 0x0, the FMOD channel
// (-1 = none) at 0x4. The PS2 build has { flags, vol, lastStreamBuffer } here.
struct iSndInfo
{
    FSOUND_STREAM* stream;
    S32 channel;
};

// GC sound info structures (FMOD based); names and layouts from the Ratatouille GC DWARF.

// One sound of a sound info asset. Size: 0x8. Entries are sorted by ID (iSndLookup does a
// binary search).
struct gcWavInfo
{
    U32 uAssetID;
    U8 uFlags; // 0x2: streamed
    U8 uAudioSampleIndex;
    U8 uFSBIndex;
    U8 uSoundInfoIndex; // index into sinfoArray
};

struct iSndFileInfo : gcWavInfo
{
    bool isStreamed()
    {
        return (uFlags >> 1) & 1;
    }

    bool isLooping()
    {
        return uFlags & 1;
    }
};

// FMOD GameCube DSP-ADPCM sample header. Size: 0x60
struct FSOUND_GCADPCM_HDR
{
    U32 num_samples;
    U32 num_adpcm_nibbles;
    U32 sample_rate;
    U16 loop_flag;
    U16 format;
    U32 sa;
    U32 ea;
    U32 ca;
    U16 coef[16];
    U16 gain;
    U16 pred_scale;
    U16 yn1;
    U16 yn2;
    U16 loop_pred_scale;
    U16 loop_yn1;
    U16 loop_yn2;
    U16 pad[11];
};

// Cutscene audio header. Size: 0x64
struct sDSPADPCM : FSOUND_GCADPCM_HDR
{
    U32 uAssetID;
};

// Header of a 'SNDI' sound info asset, one per loaded sound info (sinfoArray). Size: 0x20
struct gcSoundInfoAsset
{
    U32 uSoundInfoID;
    U32 uSumFSBSize;
    FMUSIC_MODULE* pFMusicMod;
    S32* pFSBFileArray; // nFSBFiles offsets, relocated on load
    gcWavInfo* pWavInfoArray;
    sDSPADPCM* pCutsceneAudioHeaders;
    U16 nWavFiles;
    U16 nSounds;
    U16 nStreams; // nonzero: loadSoundInfoAsset calls FSOUND_Stream_SetBufferSize(500)
    U8 nFSBFiles;
    U8 nCutsceneAudioHeaders;
};

// GC only: a stream voice waiting for its turn to be opened (pTempStreams).
struct iSndTempStream
{
    xSndVoiceInfo* vp;
    U32 mode; // FSOUND_Stream_Open mode
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

enum iSndGroupHandle
{
};

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

// enum eSNDCurrentPlayer
// {
//     eCurrentPlayerSpongeBob,
//     eCurrentPlayerPatrick,
//     eCurrentPlayerCar,
//     eCurrentPlayerSpongePat,
//     eCurrentPlayerCount
// };

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

#endif
