#ifndef ZASSETTYPES_H
#define ZASSETTYPES_H

#include <types.h>
#include <rwcore.h>
#include <rpworld.h>

#include "xAnim.h"
#include "xBase.h"
#include "xVec3.h"
#include "xSplineAsset.h"

namespace zAsset
{
    struct spline_path : xBaseAsset
    {
        U8 exclusive; // 0x8
        U8 used; // 0x9
        U8 has_hover; // 0xa
        U16 forwards_count; // 0xc
        U16 backwards_count; // 0xe
        F32 speed; // 0x10
        F32 hover_time; // 0x14
        xVec3 hover_point; // 0x18
        // init() replaces the asset ids in place with the resolved pointers; the id arrays
        // follow the struct (0x30).
        union
        {
            struct
            {
                U32 spline_id; // 0x24
                U32* forward_ids; // 0x28
                U32* backward_ids; // 0x2c
            };
            struct
            {
                x::NURBS* spline; // 0x24
                spline_path** forward_paths; // 0x28
                spline_path** backward_paths; // 0x2c
            };
        };

        static void* init(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize);
    };
} // namespace zAsset

struct zAnimFxSoundGroup
{
    U32 ID;
    U8 count;
};

struct xAnimAssetTable
{
    U32 Magic;
    U32 NumRaw;
    U32 NumFiles;
    U32 NumStates;
    U32 ConstructFunc;
};

struct xAnimAssetFile
{
    U32 FileFlags;
    F32 Duration;
    F32 TimeOffset;
    U16 NumAnims[2];
    void** RawData;
    S32 Physics;
    S32 StartPose;
    S32 EndPose;
};

struct xAnimAssetState
{
    U32 StateID;
    U32 FileIndex;
    U32 EffectCount;
    U32 EffectOffset;
    F32 Speed;
    U32 SubStateID;
    U32 SubStateCount;
};

struct xAnimAssetEffect
{
    U32 StateID;
    F32 StartTime;
    F32 EndTime;
    U32 UserDataSize;
    U8 Flags;
    U8 EffectType;
    U8 Probability;
    U8 PAD;
};

extern AnimTableList animTable[8];

void zAssetStartup();
void zAssetShutdown();
void TextureRW3_Unload(void* userdata, U32 assetid);
void* RWTX_Read(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize);

#endif
