#ifndef ZNPC_H
#define ZNPC_H

#include <types.h>

#include "xEnt.h"
#include "xFactory.h"
#include "xModel.h"
#include "xMovePoint.h"
#include "xMath3.h"

#include "iSnd.h"

enum zHitSource
{
    zHS_EVENT,
    zHS_GENERAL,
    zHS_PROJECTILE,
    zHS_EXPLOSION,
    zHS_LASER,
    zHS_ENERGY,
    zHS_FIRE,
    zHS_SURFACE,
    zHS_MELEE_HIGH,
    zHS_MELEE_MID,
    zHS_MELEE_LOW,
    zHS_MELEE_UP,
    zHS_MELEE_BACK,
    zHS_MELEE_DIZZY,
    zHS_THROW,
    zHS_WATER,
    zHS_DEATHPLANE,
    zHS_INCREDI,
    zHS_KNOCKBACK,
    zHS_LASERBEAM,
    zHS_INFINITE_FALL,
    zHS_COUNT,
    zHS_FORCE_INT = 0xffffffff
};

struct zHitSourceMapEntry
{
    char* name;
    zHitSource sourcetype;
};

// Defined in zHitParameters.cpp (zWad2, .rodata); terminated by an entry with a NULL name.
extern const zHitSourceMapEntry zHitSourceMap[22];

// Only known from mangled names (the PS2 DWARF has no definition). zHitTargetCCs[7] suggests seven
// values, whose names are unknown.
enum zHitTarget
{
};

struct xEntNPCAssetIN;
struct zAnimFxSoundGroup;
struct xAnimTable;

namespace zNPC
{
    struct base : xEnt, xFactoryInst
    {
        S16 bound_bone;
        U16 sound_id_offset;
        U16 global_parameters_size;
        U16 local_parameters_size;
        U32 type;
        xModelAssetParam* global_parameters;
        xModelAssetParam* local_parameters;
        union
        {
            xMovePoint* movepoint;
            U32 movepoint_asset_id;
        };
        xEntNPCAssetIN* npc_asset;
        xModelAssetInfo* model_asset;
        F32 shadow_strength;
        F32 shadow_cache_fudge_factor;

        // The vtable pointer sits here (0x10C, before bound_offset): CW places it where the
        // first virtual is declared. Order must match __vt__Q24zNPC4base.
        virtual ~base()
        {
        }
        virtual void init_npc(xEntAsset* asset)
        {
        }
        virtual void update_npc(F32 dt);
        virtual void update_bounds();
        virtual void setup()
        {
        }
        virtual void scene_setup()
        {
        }
        virtual void render_extra()
        {
        }
        virtual bool system_event(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                                  xBase* toParamWidget, U32 toParamWidgetID)
        {
            return true;
        }
        virtual iSndHandle play_sound(zAnimFxSoundGroup* sound);
        virtual void destroy()
        {
        }
        virtual xAnimTable* CreateAnimTable(xAnimTable* table)
        {
            return NULL;
        }
        virtual void change_bounds(F32 scale, xVec3, bool use_bounding_box);

        xVec3 bound_offset;

        bool get_parameter(const char* name, const char** value, const char* default_value);
        bool get_parameter(const char* name, bool* value, bool default_value);

        // Always inlined in TSSM (out of line in the Incredibles prototype DWARF).
        xVec3 get_bone_world_position(S32 bone)
        {
            if (bone == -1)
            {
                return *(xVec3*)&model->Mat->pos;
            }

            xVec3 position;
            xMat4x3Toworld(&position, (xMat4x3*)model->Mat, (xVec3*)&model->Mat[bone + 1].pos);
            return position;
        }
    };

    // Factory creator for each NPC class. Every T has a user-declared default constructor, which
    // gives retail's order of the create_npc<T> instantiations.
    template <class T> xFactoryInst* create_npc(S32 who, RyzMemGrow* grow, void*)
    {
        return new (who, grow) T;
    }

    void destroy_npc(xFactoryInst* inst);
    xAnimTable* create_anim_table(U32 brain_id);
} // namespace zNPC

#endif
