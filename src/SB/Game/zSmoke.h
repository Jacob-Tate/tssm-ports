#ifndef ZSMOKE_H
#define ZSMOKE_H

#include "xBase.h"
#include "xDynAsset.h"
#include "xMath3.h"
#include "iColor.h"

// total size: 0xA0
struct zSmokeEmitter : xBase
{
    // total size: 0x80
    struct asset_type : xDynAsset
    {
        U32 flags; // 0x10
        struct
        {
            U32 attach_to; // 0x14
            xVec3 loc; // 0x18
            xVec3 dir; // 0x24
            xVec3 scale; // 0x30
        } region;
        U32 texture; // 0x3C
        U16 texture_rows; // 0x40
        U16 texture_columns; // 0x42
        F32 rate; // 0x44
        F32 life_min; // 0x48
        F32 life_max; // 0x4C
        F32 size_min; // 0x50
        F32 size_max; // 0x54
        F32 vel_min; // 0x58
        F32 vel_max; // 0x5C
        F32 growth; // 0x60
        xVec3 vel_dir; // 0x64
        F32 vel_dir_vary; // 0x70
        F32 wind; // 0x74
        xColor_tag color_birth; // 0x78
        xColor_tag color_death; // 0x7C
    };

    S32 system_id; // 0x10
    asset_type* asset; // 0x14
    xMat4x3 emit_mat; // 0x20
    F32 emitted; // 0x60
    S32 flags; // 0x64
    xVec3 vel_dir; // 0x68
    S32 max_frame; // 0x74
    F32 du; // 0x78
    F32 dv; // 0x7C
    F32 age_rate_min; // 0x80
    F32 age_rate_range; // 0x84
    F32 dr; // 0x88
    F32 dg; // 0x8C
    F32 db; // 0x90
    F32 da; // 0x94

    static void cb_dispatch(xBase*, xBase* to, U32 event, const F32*, xBase*, U32);
    void update(F32 dt);
    void refresh_mat();
    void load(const asset_type& a);
    static void load(xBase& data, xDynAsset& asset, size_t);
};

void zSmokeSceneEnter();
void zSmokeSceneExit();
void zSmokeUpdate(F32 dt);

#endif
