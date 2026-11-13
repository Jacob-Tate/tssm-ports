#ifndef ZSPOTLIGHT_H
#define ZSPOTLIGHT_H

#include <types.h>
#include <rwcore.h>
#include <rpworld.h>

#include "xBase.h"
#include "xColor.h"
#include "xDynAsset.h"
#include "xEnt.h"
#include "xLightVolume.h"
#include "xMath3.h"
#include "xModel.h"
#include "zBase.h"
#include "zScene.h"

// Size: 0x48
struct zSpotlightAsset : xDynAsset
{
    U32 flags;
    U32 attach_to;
    U32 target;
    U8 attach_bone;
    U8 target_bone;
    U8 pad1;
    U8 pad2;
    F32 radius;
    F32 view_angle;
    F32 max_dist;
    struct
    {
        xColor_tag color;
    } light;
    struct
    {
        xColor_tag color;
    } aura;
    struct
    {
        U32 texture;
        xColor_tag color;
        F32 size_min;
        F32 size_max;
        U8 glow_min;
        U8 glow_max;
        U8 pad1;
        U8 pad2;
    } flare;
};

// Size: 0xE0
struct zSpotlight : xBase
{
    struct light_volume_type : xLightVolume
    {
        RpAtomic* atomic;
        xColor_tag color;
        bool inside; // uint8 in the PS2 DWARF, which has no bool type
        xMat4x3* mat;

        light_volume_type(RpAtomic* atomic, xMat4x3& mat) : atomic(atomic), mat(&mat)
        {
            create();
        }

        // Defined in zSpotlight.cpp, where it uses the file-local move_atomic.
        void render();
    };

    zSpotlightAsset* asset; // 0x10
    S32 flags; // 0x14
    xEnt* attach_ent; // 0x18
    xEnt* target_ent; // 0x1C
    xVec3 target_loc; // 0x20
    xMat4x3 mat; // 0x30
    xMat4x3 volumeMat; // 0x70
    F32 volumeScale; // 0xB0
    F32 target_dist; // 0xB4
    F32 slope; // 0xB8
    F32 view_cos; // 0xBC
    xVec3 end_loc; // 0xC0
    F32 end_radius; // 0xCC
    RwRaster* raster_flare; // 0xD0
    RpAtomic* aura_atomic; // 0xD4
    light_volume_type* light_volume; // 0xD8

    static void load(xBase& data, xDynAsset& asset, size_t);
    static void scene_exit();
    static void setup_all();
    static void reset_all();
    static void update_all(F32 dt);
    static void early_render();
    static void render_all();
    static void render_auras();
    static void cb_dispatch(xBase* from, xBase* to, U32 event, const F32* argf,
                            xBase* toParamWidget, U32 toParamWidgetID);

    void load(const zSpotlightAsset& a)
    {
        asset = (zSpotlightAsset*)&a;
        xBaseInit(this, asset);
        baseType = eBaseTypeSpotlight;
        eventFunc = cb_dispatch;

        if (linkCount != 0)
        {
            link = (xLinkAsset*)(asset + 1);
        }
    }
    void setup();
    void reset();
    void update(F32 dt);
    void activate();
    void deactivate()
    {
        if (!(flags & 0x1))
        {
            return;
        }

        flags &= ~0x1;

        if (light_volume->active())
        {
            light_volume->deactivate();
        }
    }
    void reload_asset();
    void load_attach_to(U32 id);
    void load_target(U32 id)
    {
        xBase* b = zSceneFindObject(id);

        if (b != NULL && (b->baseFlags & 0x20) && ((xEnt*)b)->model != NULL)
        {
            target_ent = (xEnt*)b;
        }
        else
        {
            target_ent = NULL;
        }
    }
    void load_textures();
    void update_position()
    {
        if (attach_ent == NULL)
        {
            return;
        }

        S32 attach_bone = asset->attach_bone;
        if (attach_bone < 0 || attach_bone > attach_ent->model->BoneCount)
        {
            attach_bone = 0;
        }

        xModelGetBoneMat(mat, *attach_ent->model, attach_bone);

        if (target_ent == NULL)
        {
            target_dist = asset->max_dist;
            target_loc.x = mat.at.x * target_dist + mat.pos.x;
            target_loc.y = mat.at.y * target_dist + mat.pos.y;
            target_loc.z = mat.at.z * target_dist + mat.pos.z;
        }
        else
        {
            S32 target_bone = asset->target_bone;
            if (target_bone < 0 || target_bone > target_ent->model->BoneCount)
            {
                target_bone = 0;
            }

            xModelGetBoneLocation(target_loc, *target_ent->model, target_bone);
            target_dist = xMat3x3LookVec4(mat, target_loc.x - mat.pos.x, target_loc.y - mat.pos.y,
                                          target_loc.z - mat.pos.z, mat.right);
        }

        end_loc.x = mat.at.x * asset->max_dist + mat.pos.x;
        end_loc.y = mat.at.y * asset->max_dist + mat.pos.y;
        end_loc.z = mat.at.z * asset->max_dist + mat.pos.z;
    }
    void add_flare();
    void add_aura();
    void render_aura();
    void refresh_light_volume();
    void dispatch(xBase* from, U32 event, const F32* argf, xBase* toParamWidget,
                  U32 toParamWidgetID);
};

#endif
