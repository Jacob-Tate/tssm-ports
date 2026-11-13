#ifndef XCUTSCENE_H
#define XCUTSCENE_H

#include "xFile.h"
#include "iSnd.h"
#include "xModel.h"

// GC: 0x40 per track (PS2 DWARF has 60-byte names)
struct xCutsceneAudioTrack
{
    U32 uLeftSoundId;
    U32 uRightSoundId;
    char szLeftSound[28];
    char szRightSound[28];
};

struct xCutsceneAnimHdr
{
    U32 RootIndex;
    F32 Translate[3];
};

struct xCutsceneModelHack
{
    xModelPipe* pipe;
};

struct xCutsceneInfo
{
    U32 Magic;
    U32 AssetID;
    U32 NumData;
    U32 NumTime;
    U32 MaxModel;
    U32 MaxBufEven;
    U32 MaxBufOdd;
    U32 HeaderSize;
    U32 VisCount;
    U32 VisSize;
    U32 BreakCount;
    U32 pad;
    xCutsceneAudioTrack AudioTracks[32];
};

struct xCutsceneData
{
    U32 DataType;
    U32 AssetID;
    U32 ChunkSize;
    union
    {
        U32 FileOffset;
        void* DataPtr;
    };
};

#define XCUTSCENEDATA_TYPE_RW_MODEL 1
#define XCUTSCENEDATA_TYPE_ANIMATION 2
#define XCUTSCENEDATA_TYPE_CAMERA 3
#define XCUTSCENEDATA_TYPE_MORPHTARGET 4
#define XCUTSCENEDATA_TYPE_SOUND 5
#define XCUTSCENEDATA_TYPE_JDELTAMODEL 6
#define XCUTSCENEDATA_TYPE_JDELTAANIM 7
#define XCUTSCENEDATA_TYPE_6 6

struct xCutsceneBreak
{
    F32 Time;
    S32 Index;
};

struct xCutsceneMphFrame
{
    F32 Time;
    U32 Flags;
};

struct xCutsceneMphRun
{
    S32 VertIdx;
    S32 VertCt;
};

struct xCutsceneTime
{
    F32 StartTime;
    F32 EndTime;
    U32 NumData;
    U32 ChunkIndex;
};

// GC: vtable at 0x0 (__vt__9XCSNNosey: CanRenderNow, UpdatedAnimated)
struct XCSNNosey
{
    virtual void CanRenderNow()
    {
    }

    virtual void UpdatedAnimated(RpAtomic*, RwMatrixTag*, U32, U32, U32&)
    {
    }

    void* userdata;
    S32 flg_nosey;
};

struct xCutscene
{
    xCutsceneInfo* Info;
    xCutsceneData* Data;
    U32* TimeChunkOffs;
    U32* Visibility;
    xCutsceneBreak* BreakList;
    xCutsceneTime* Play;
    xCutsceneTime* Stream;
    U32 Waiting;
    U32 BadReadPause;
    F32 BadReadSpeed;
    void* RawBuf;
    void* AlignBuf;
    F32 Time;
    F32 CamTime;
    U32 PlayIndex;
    U32 Ready;
    S32 DataLoading;
    U32 GotData;
    U32 ShutDownWait;
    F32 PlaybackSpeed;
    U32 Opened;
    tag_xFile File;
    S32 AsyncID;
    void* MemBuf;
    void* MemCurr;
    U32 SndStarted;
    U32 SndNumChannel;
    U32 SndChannelReq[2];
    U32 SndAssetID[2];
    iSndHandle SndHandle[2];
    U32 SubTitlesID;
    XCSNNosey* cb_nosey;
    xCutsceneModelHack* ModelHack;
    U8 started;
    U8 useDefaultTrack;

    void NoseySet(XCSNNosey* nosey);
};

struct CutsceneShadowModel
{
    RpAtomic* model;
    RwMatrixTag* animMat;
    U32 shadowBits;
};

struct xEnt;

extern U32 gFrameCount;

void xCutscene_Init(void* toc);
xCutscene* xCutscene_CurrentCutscene();
void xCutscene_Render(xCutscene* csn, xEnt**, S32*, F32*);
U32 iCSFileOpen(xCutscene* csn);
xCutscene* xCutscene_Create(U32 id, U32 subtitlesID);
S32 xCutscene_Destroy(xCutscene* csn);
S32 xCutscene_LoadStart(xCutscene* csn);
S32 xCutscene_Update(xCutscene* csn, F32 dt);
void xCutscene_SetSpeed(xCutscene* csn, F32 speed);

#endif
