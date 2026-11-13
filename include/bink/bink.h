#ifndef __BINK_H__
#define __BINK_H__

#include <PowerPC_EABI_Support\MSL_C\MSL_Common\size_t.h>

#define BINKSURFACE8P 0
#define BINKSURFACE24 1
#define BINKSURFACE24R 2
#define BINKSURFACE32 3
#define BINKSURFACE32R 4
#define BINKSURFACE32A 5
#define BINKSURFACE32RA 6
#define BINKSURFACE4444 7
#define BINKSURFACE5551 8
#define BINKSURFACE555 9
#define BINKSURFACE565 10
#define BINKSURFACE655 11
#define BINKSURFACE664 12
#define BINKSURFACEYUY2 13
#define BINKSURFACEUYVY 14
#define BINKSURFACEYV12 15
#define BINKSURFACEMASK 15

#define BINKCOPYALL 0x80000000L

struct BINKRECT
{
    S32 Left, Top, Width, Height;
};

#define BINKMAXDIRTYRECTS 8

struct BINK;
struct BINKSND;

// RAD Bink SDK types; layouts from the Ratatouille GC DWARF.
struct BINKIO
{
    U32 (*ReadHeader)(BINKIO*, S32, void*, U32); // 0x0
    U32 (*ReadFrame)(BINKIO*, U32, S32, void*, U32); // 0x4
    U32 (*GetBufferSize)(BINKIO*, U32); // 0x8
    void (*SetInfo)(BINKIO*, void*, U32, U32, U32); // 0xC
    U32 (*Idle)(BINKIO*); // 0x10
    void (*Close)(BINKIO*); // 0x14
    BINK* bink; // 0x18
    U32 ReadError; // 0x1C
    U32 DoingARead; // 0x20
    U32 BytesRead; // 0x24
    U32 Working; // 0x28
    U32 TotalTime; // 0x2C
    U32 ForegroundTime; // 0x30
    U32 IdleTime; // 0x34
    U32 ThreadTime; // 0x38
    U32 BufSize; // 0x3C
    U32 BufHighUsed; // 0x40
    U32 CurBufSize; // 0x44
    U32 CurBufUsed; // 0x48
    U8 iodata[160]; // 0x4C
    void (*suspend_callback)(BINKIO*); // 0xEC
    S32 (*try_suspend_callback)(BINKIO*); // 0xF0
    void (*resume_callback)(BINKIO*); // 0xF4
    void (*idle_on_callback)(BINKIO*); // 0xF8
    U32 callback_control[16]; // 0xFC
};

struct BUNDLEPOINTERS
{
    void* typeptr; // 0x0
    void* type16ptr; // 0x4
    void* colorptr; // 0x8
    void* bits2ptr; // 0xC
    void* motionXptr; // 0x10
    void* motionYptr; // 0x14
    void* dctptr; // 0x18
    void* mdctptr; // 0x1C
    void* patptr; // 0x20
};

// RAD Bink SDK movie handle (size 0x394).
struct BINK
{
    U32 Width; // 0x0
    U32 Height; // 0x4
    U32 Frames; // 0x8
    U32 FrameNum; // 0xC
    U32 LastFrameNum; // 0x10
    U32 FrameRate; // 0x14
    U32 FrameRateDiv; // 0x18
    U32 ReadError; // 0x1C
    U32 OpenFlags; // 0x20
    U32 BinkType; // 0x24
    U32 Size; // 0x28
    U32 FrameSize; // 0x2C
    U32 SndSize; // 0x30
    BINKRECT FrameRects[BINKMAXDIRTYRECTS]; // 0x34
    S32 NumRects; // 0xB4
    U32 PlaneNum; // 0xB8
    void* YPlane[2]; // 0xBC
    void* APlane[2]; // 0xC4
    U32 YWidth; // 0xCC
    U32 YHeight; // 0xD0
    U32 UVWidth; // 0xD4
    U32 UVHeight; // 0xD8
    void* MaskPlane; // 0xDC
    U32 MaskPitch; // 0xE0
    U32 MaskLength; // 0xE4
    U32 LargestFrameSize; // 0xE8
    U32 InternalFrames; // 0xEC
    S32 NumTracks; // 0xF0
    U32 Highest1SecRate; // 0xF4
    U32 Highest1SecFrame; // 0xF8
    S32 Paused; // 0xFC
    U32 BackgroundThread; // 0x100
    void* compframe; // 0x104
    void* preloadptr; // 0x108
    U32* frameoffsets; // 0x10C
    BINKIO bio; // 0x110
    U8* ioptr; // 0x24C
    U32 iosize; // 0x250
    U32 decompwidth; // 0x254
    U32 decompheight; // 0x258
    S32* trackindexes; // 0x25C
    U32* tracksizes; // 0x260
    U32* tracktypes; // 0x264
    S32* trackIDs; // 0x268
    U32 numrects; // 0x26C
    U32 playedframes; // 0x270
    U32 firstframetime; // 0x274
    U32 startframetime; // 0x278
    U32 startblittime; // 0x27C
    U32 startsynctime; // 0x280
    U32 startsyncframe; // 0x284
    U32 twoframestime; // 0x288
    U32 entireframetime; // 0x28C
    U32 slowestframetime; // 0x290
    U32 slowestframe; // 0x294
    U32 slowest2frametime; // 0x298
    U32 slowest2frame; // 0x29C
    U32 soundon; // 0x2A0
    U32 videoon; // 0x2A4
    U32 totalmem; // 0x2A8
    U32 timevdecomp; // 0x2AC
    U32 timeadecomp; // 0x2B0
    U32 timeblit; // 0x2B4
    U32 timeopen; // 0x2B8
    U32 fileframerate; // 0x2BC
    U32 fileframeratediv; // 0x2C0
    U32 runtimeframes; // 0x2C4
    S32 rtindex; // 0x2C8
    U32* rtframetimes; // 0x2CC
    U32* rtadecomptimes; // 0x2D0
    U32* rtvdecomptimes; // 0x2D4
    U32* rtblittimes; // 0x2D8
    U32* rtreadtimes; // 0x2DC
    U32* rtidlereadtimes; // 0x2E0
    U32* rtthreadreadtimes; // 0x2E4
    U32 lastblitflags; // 0x2E8
    U32 lastdecompframe; // 0x2EC
    U32 lastresynctime; // 0x2F0
    U32 doresync; // 0x2F4
    U32 skipcount; // 0x2F8
    U32 toofewskipstomask; // 0x2FC
    U32 playingtracks; // 0x300
    U32 soundskips; // 0x304
    BINKSND* bsnd; // 0x308
    U32 skippedlastblit; // 0x30C
    U32 skipped_this_frame; // 0x310
    U32 skippedblits; // 0x314
    BUNDLEPOINTERS bunp; // 0x318
    U32 skipped_in_a_row; // 0x33C
    U32 big_sound_skip_adj; // 0x340
    U32 big_sound_skip_reduce; // 0x344
    U32 last_time_almost_empty; // 0x348
    U32 last_read_count; // 0x34C
    U32 last_sound_count; // 0x350
    U32 snd_callback_buffer[16]; // 0x354
};

typedef struct BINK* HBINK;

// BinkOpen flags (RAD Bink SDK)
#define BINKSNDTRACK 0x00004000L
#define BINKNOSKIP 0x00080000L
#define BINKNOFILLIOBUF 0x00200000L
#define BINKFILEHANDLE 0x00800000L

typedef S32 (*BINKSNDOPEN)(struct BINKSND* BnkSnd, U32 freq, S32 bits, S32 chans, U32 flags,
                           HBINK bink);
typedef BINKSNDOPEN (*BINKSNDSYSOPEN)(U32 param);

// GameCube AX sound system: ARAM allocator callbacks passed to BinkOpenAX.
struct RADARAMCALLBACKS
{
    void* (*aram_malloc)(U32 bytes);
    void (*aram_free)(void* ptr);
};

struct RAD3DIMAGE
{
    int a;
    int b;
    unsigned int c;
    int d;
    int e;
    void* f;
    int g;
    int h;
};

typedef struct RAD3DIMAGE* HRAD3DIMAGE;

#ifdef __cplusplus
extern "C" {
#endif

extern int BinkDoFrame(HBINK bnk);
extern int BinkCopyToBuffer(HBINK bnk, void* dest, int destpitch, unsigned int destheight,
                            unsigned int destx, unsigned int desty, unsigned int flags);
extern int Lock_RAD_3D_image(HRAD3DIMAGE rad_image, void* out_pixel_buffer,
                             unsigned int* out_buffer_pitch, unsigned int* arg3);
extern void Unlock_RAD_3D_image(HRAD3DIMAGE rad_image);
extern void Blit_RAD_3D_image(HRAD3DIMAGE Image, float, float, float, float, float);
extern void RADSetAudioMemory(void* (*malloc)(size_t), void (*free)(void*));
extern void RADSetMemory(void* (*malloc)(size_t), void (*free)(void*));
extern HBINK BinkOpen(const char* name, U32 flags);
extern S32 BinkSetSoundSystem(BINKSNDSYSOPEN open, U32 param);
extern BINKSNDOPEN BinkOpenAX(U32 param);
extern void BinkSetMemory(void* (*malloc)(U32 bytes), void (*free)(void* ptr));
extern S32 BinkPause(HBINK bink, S32 pause);
extern void BinkGetError();
extern void BinkSetVolume(HBINK bink, unsigned int, int volume);
extern HRAD3DIMAGE Open_RAD_3D_image(HBINK bink, unsigned int, unsigned int, unsigned int);
extern void Close_RAD_3D_image(HRAD3DIMAGE rad_image);
extern void BinkGoto(HBINK bink, int frame, unsigned int);
extern int BinkWait(HBINK bink);
extern int BinkCopyToBufferRect(HBINK bnk, void* dest, int destpitch, unsigned int destheight,
                                unsigned int destx, unsigned int desty, unsigned int srcx,
                                unsigned int srcy, unsigned int srcw, unsigned int srch,
                                unsigned int flags);
extern unsigned int BinkGetTrackID(HBINK bnk, unsigned int trackindex);
extern void BinkSetSoundTrack(unsigned int total_tracks, unsigned int* tracks);
extern void BinkNextFrame(HBINK bink);
extern void BinkClose(HBINK bink);

#ifdef __cplusplus
}
#endif

#endif
