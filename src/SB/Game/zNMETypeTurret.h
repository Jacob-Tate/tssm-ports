#ifndef ZNMETYPETURRET_H
#define ZNMETYPETURRET_H

#include "zNMECommon.h"

enum en_turranim
{
    TURR_ANIM_UNKNOWN,
    TURR_ANIM_INACTIVE,
    TURR_ANIM_ACTIVE,
    TURR_ANIM_SHOOT_ONCE,
    TURR_ANIM_SHOOT_PREP,
    TURR_ANIM_SHOOT_LOOP,
    TURR_ANIM_SHOOT_DONE,
    TURR_ANIM_TAUNT,
    TURR_ANIM_RELOAD,
    TURR_ANIM_DAMAGE,
    TURR_ANIM_POPUP,
    TURR_ANIM_POPDOWN,
    TURR_ANIM_NOMORE,
    TURR_ANIM_FORCE = 0x7fffffff
};

extern U32 g_hash_turranim[TURR_ANIM_NOMORE];
extern char* g_strz_turranim[TURR_ANIM_NOMORE];

// Type-specific asset data that trails the common zNMEAsset (offset 0x60). The PS2 DWARF
// has no type for it, so the name is ours and only the fields read so far are defined.
struct zNMEAssetTurret : zNMEAsset
{
    // Sweep arc in degrees (real name unknown); read as a float by zNMETurBarrel::CalcEndPoints.
    F32 unk_60;
    U32 aid_mvptTarget;
    // Flags (real name unknown); 0x1: the barrel turret retargets the player after each sweep
    // (zNMEGoalTurrBTurn::SubStateUpdate).
    U32 unk_68;
    U32 unk_6C[2]; // total size 0x74, as checked by ValidTurret (zNMEAsset.cpp)
};

struct zNMETurret : zNMECommon
{
    zNMETurret()
    {
    }
    void Init(xEntAsset* asset);
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);
    xAnimTable* CreateAnimTable(xAnimTable* table);
    zMovePoint* MvptFirstTarget();
};

void ZNME_Turret_Startup();

#endif
