#ifndef ZNMECOMMON_H
#define ZNMECOMMON_H

#include "zNMECentral.h"
#include "zNPCTypes.h"
#include "xNPCBasic.h"
#include "xDynAsset.h"
#include "xListItem.h"
#include "xEntDrive.h"
#include "xBehaveMgr.h"
#include "xEnt.h"
#include "xEvent.h"
#include "xSFX.h"

#include "iSnd.h"

#include "zNPCSndTable.h"
#include "zMovePoint.h"
#include "zShrapnel.h"
#include "zNPC.h"
#include "zEnt.h"
#include "zNMENPCWrapper.h"
#include "zGlobals.h"
#include "zGame.h"

struct xEntNPCAsset
{
    S32 npcFlags;
    S32 npcModel;
    S32 npcProps;
    U32 movepoint;
    U32 taskWidgetPrime;
    U32 taskWidgetSecond;
};

// struct xEntNPCAssetIN : xEntNPCAsset
// {
//     U32 navigation_mesh_id;
//     U32 settings;
// };

// The asset named by xEntNPCAssetIN::settings: a parameter count followed by the parameter
// list. Not in the PS2 DWARF; name and layout from the Ratatouille GC DWARF (zNPCCommon::init).
struct xNPCSettingsIN2Asset
{
    U32 count;
    xModelAssetParam param;
};

enum en_allow
{
    ALLOW_NEVER,
    ALLOW_NORMAL,
    ALLOW_ALWAYS,
    ALLOW_NOMORE
};

enum en_npcgol
{
    NME_GOAL_UNKNOWN,
    NME_GOAL_CRIT_IDLE = 0x4e474300,
    NME_GOAL_CRIT_PATROL,
    NME_GOAL_CRIT_DYING,
    NME_GOAL_CRIT_DEAD,
    NME_GOAL_CRIT_BATTACK,
    NME_GOAL_CRIT_JATTACK,
    NME_GOAL_CRIT_JDYING,
    NME_GOAL_TURR_IDLE = 0x4e474700,
    NME_GOAL_TURR_RELOAD,
    NME_GOAL_TURR_HURT,
    NME_GOAL_TURR_DEAD,
    NME_GOAL_TURR_TREADY,
    NME_GOAL_TURR_TTURN,
    NME_GOAL_TURR_TSHOOT,
    NME_GOAL_TURR_PDORMANT,
    NME_GOAL_TURR_PALERT,
    NME_GOAL_TURR_PPATALPHA,
    NME_GOAL_TURR_BIDLE,
    NME_GOAL_TURR_BTURN,
    NME_GOAL_TURR_BSHOOT,
    NME_GOAL_TURR_BHURT,
    NME_GOAL_TURR_SPIRAL,
    NME_GOAL_IDLE = 0x4e474e00,
    NME_GOAL_PATROL,
    NME_GOAL_WANDER,
    NME_GOAL_FIDGET,
    NME_GOAL_WAITING,
    NME_GOAL_DEAD,
    NME_GOAL_NOMANLAND,
    NME_GOAL_LIMBO,
    NME_GOAL_DEV_ANIMVIEW = 0x4e474400,
    NME_GOAL_DEV_HEROMODE,
    NME_GOAL_TIKI_IDLE = 0x4e475400,
    NME_GOAL_TIKI_PATROL,
    NME_GOAL_TIKI_HIDE,
    NME_GOAL_TIKI_COUNT,
    NME_GOAL_TIKI_DYING,
    NME_GOAL_TIKI_DEAD,
    NME_GOAL_AFTERLIFE = 0x4e475300,
    NME_GOAL_SPAWN,
    NME_GOAL_WOUND,
    NME_GOAL_SPOOKED,
    NME_GOAL_NOTICE,
    NME_GOAL_SCAREWAIT,
    NME_GOAL_SCARE,
    NME_GOAL_TAUNT,
    NME_GOAL_EVILPAT = 0x4e475000,
    NME_GOAL_STUNNED,
    NME_GOAL_PATCARRY,
    NME_GOAL_PATTWIRL,
    NME_GOAL_PATTHROW,
    NME_GOAL_TRIGGER_NORMAL = 0x4e475800,
    NME_GOAL_TRIGGER_SCARY,
    NME_GOAL_TRIGGER_DETECT,
    NME_GOAL_TRIGGER_ALERT,
    NME_GOAL_TRIGGER_BATTLE,
    NME_GOAL_TRIGGER_WOUND,
    NME_GOAL_TRIGGER_ATTACK,
    NME_GOAL_TRIGGER_VINIVICIVIDI,
    NME_GOAL_FOGGER_AWARE = 0x4e474500,
    NME_GOAL_FOGGER_BATTLE,
    NME_GOAL_FOGGER_ATTACK,
    NME_GOAL_SLAMMER_AWARE,
    NME_GOAL_SLAMMER_BATTLE,
    NME_GOAL_SLAMMER_ATTACK,
    NME_GOAL_SPINNER_AWARE,
    NME_GOAL_SPINNER_BATTLE,
    NME_GOAL_FLINGER_NORMAL,
    NME_GOAL_FLINGER_AWARE,
    NME_GOAL_FLINGER_BATTLE,
    NME_GOAL_FLINGER_ATTACK,
    NME_GOAL_FLINGER_BOING,
    NME_GOAL_FLINGER_FLEE,
    NME_GOAL_FLINGER_PANIC,
    NME_GOAL_FLINGER_MOVE,
    NME_GOAL_POPPER_NORMAL,
    NME_GOAL_POPPER_AWARE,
    NME_GOAL_POPPER_WOUND,
    NME_GOAL_POPPER_EVADE,
    NME_GOAL_POPPER_BATTLE,
    NME_GOAL_POPPER_ATTACK,
    NME_GOAL_ZAP_NORMAL,
    NME_GOAL_ZAP_AWARE,
    NME_GOAL_ZAP_BATTLE,
    NME_GOAL_ZAP_WOUND,
    NME_GOAL_ZAP_ZAP,
    NME_GOAL_ZAP_MOVE,
    NME_GOAL_MERV_NORMAL,
    NME_GOAL_MERV_AWARE,
    NME_GOAL_MERV_BATTLE,
    NME_GOAL_MERV_ZAP,
    NME_GOAL_MERV_BOMB,
    NME_GOAL_MERV_BOWL,
    NME_GOAL_MERV_WOUND,
    NME_GOAL_MERV_MOVE,
    NME_GOAL_BUCK_RUNNING,
    NME_GOAL_BUCK_BIRTHING,
    NME_GOAL_BUCK_DYING,
    NME_GOAL_BUCK_DEAD,
    NME_GOAL_DENNIS_NORMAL,
    NME_GOAL_DENNIS_EVADE,
    NME_GOAL_DENNIS_BATTLE,
    NME_GOAL_DENNIS_ATTACK,
    NME_GOAL_DENNIS_TAUNT,
    NME_GOAL_DENNIS_DAMAGE,
    NME_GOAL_DENNIS_DEAD,
    NME_GOAL_DENTOO_NORMAL,
    NME_GOAL_DENTOO_EVADE,
    NME_GOAL_DENTOO_BATTLE,
    NME_GOAL_DENTOO_ATTACK,
    NME_GOAL_DENTOO_TAUNT,
    NME_GOAL_DENTOO_DAMAGE,
    NME_GOAL_DENTOO_DEAD,
    NME_GOAL_SBBAT_IDLE,
    NME_GOAL_SBBAT_DEAD,
    NME_GOAL_NOMORE,
    NME_GOAL_FORCE = 0x7fffffff
};

enum en_nmesimp
{
    NME_SIMP_HAPPY,
    NME_SIMP_DEAD,
    NME_SIMP_IGNORANT,
    NME_SIMP_WARYDETECT,
    NME_SIMP_WARY,
    NME_SIMP_WORRIED,
    NME_SIMP_CONFUSED,
    NME_SIMP_BIGBULLY,
    NPC_STAT_NOMORE
};

enum en_haztyp
{
    HAZ_TYP_UNKNOWN,
    HAZ_TYP_EXPLODE,
    HAZ_TYP_IMPACT,
    HAZ_TYP_IMPACTWET,
    HAZ_TYP_IMPACTBOOM,
    HAZ_TYP_DAZED,
    HAZ_TYP_SPAWNBALL_XX,
    HAZ_TYP_SPAWNBALL_GG,
    HAZ_TYP_SPAWNBALL_TR,
    HAZ_TYP_SPAWNBALL_PT,
    HAZ_TYP_SPAWNGROW,
    HAZ_TYP_BUCKOBOOM_V1,
    HAZ_TYP_TIKIBOOM,
    HAZ_TYP_SLAMWAVE_V1,
    HAZ_TYP_SLAMWAVE_V2,
    HAZ_TYP_SLAMWAVE_V3,
    HAZ_TYP_FLING_V1,
    HAZ_TYP_FLING_V2,
    HAZ_TYP_FLING_V3,
    HAZ_TYP_FLING_FROGFISH,
    HAZ_TYP_SPITPUDDLE_V1,
    HAZ_TYP_SPITPUDDLE_V2,
    HAZ_TYP_SPITPUDDLE_V3,
    HAZ_TYP_ICECREAMSPLAT,
    HAZ_TYP_SPITSTEAM,
    HAZ_TYP_SPITSPLASH,
    HAZ_TYP_POPPER_V1,
    HAZ_TYP_POPPER_V2,
    HAZ_TYP_POPPER_V3,
    HAZ_TYP_MERVBOMB_V1,
    HAZ_TYP_MERVBOMB_V2,
    HAZ_TYP_MERVBOMB_V3,
    HAZ_TYP_MERVLET_V1,
    HAZ_TYP_MERVLET_V2,
    HAZ_TYP_MERVLET_V3,
    HAZ_TYP_ZAPBALL_V1,
    HAZ_TYP_ZAPBALL_V2,
    HAZ_TYP_ZAPBOOM_V1,
    HAZ_TYP_ZAPBOOM_V2,
    HAZ_TYP_TURBALL_V1,
    HAZ_TYP_TURBALL_V2,
    HAZ_TYP_TURBALL_V3,
    HAZ_TYP_TURBOOM,
    HAZ_TYP_GOLFBALL,
    HAZ_TYP_GOLFBOOM,
    HAZ_TYP_GOLFJUNK_A,
    HAZ_TYP_GOLFJUNK_B,
    HAZ_TYP_GOLFJUNK_C,
    HAZ_TYP_GOLFJUNK_D,
    HAZ_TYP_GOLFJUNK_E,
    HAZ_TYP_DENNIS_KNIFE,
    HAZ_TYP_DENNIS_KNIFEBOOM,
    HAZ_TYP_TUNEBORN,
    HAZ_TYP_TUNEBANG,
    HAZ_TYP_TUNEBOOM,
    HAZ_TYP_TUNEFLYT,
    HAZ_TYP_SWIMWAKE,
    HAZ_TYP_BOWWAVE,
    HAZ_TYP_SPLASH_RING,
    HAZ_TYP_SPLISH_DROP,
    HAZ_TYP_VISSPLASH_WAVE,
    HAZ_TYP_VISSPLASH_DROP,
    HAZ_TYP_VISSPLASH_WATER,
    HAZ_TYP_VISSPLASH_CHOC,
    HAZ_TYP_VISSPLASH_LAVA,
    HAZ_TYP_VISSPLASH_BILE,
    HAZ_TYP_VISSPLASH_OIL,
    HAZ_TYP_VISSPLASH_SAUCE,
    HAZ_TYP_VISSPLASH_TOXIC,
    HAZ_TYP_REFLECT_V1,
    HAZ_TYP_PLYRBANG_V1,
    HAZ_TYP_POWERUP_EXPLOSION_V1,
    HAZ_TYP_NOMORE,
    HAZ_TYP_FORCE = 0x7fffffff
};

enum en_npcdmg
{
    DMGTYP_UNDECIDED,
    DMGTYP_ABOVE,
    DMGTYP_BELOW,
    DMGTYP_SIDE,
    DMGTYP_INSTAKILL,
    DMGTYP_WOUNDEVENT,
    DMGTYP_KILLEVENT,
    DMGTYP_NMEATTACK,
    DMGTYP_HITBYTOSS,
    DMGTYP_NMEKNOCKED,
    DMGTYP_ROPE,
    DMGTYP_CRUISEBUBBLE,
    DMGTYP_FRIENDLYFIRE,
    DMGTYP_REFLECTED,
    DMGTYP_BOULDER,
    DMGTYP_BUBBOWL,
    DMGTYP_THUNDER_TIKI_EXPLOSION,
    DMGTYP_DAMAGE_SURFACE,
    DMGTYP_BUNGEED,
    DMGTYP_SURFACE,
    DMGTYP_CARTWHEEL,
    DMGTYP_CARSMASH,
    DMGTYP_SLIDE,
    DMGTYP_EXPLOSION,
    DMGTYP_BBOWLZAP,
    DMGTYP_NOMORE,
    DMGTYP_FORCEINT = 0x7fffffff
};

enum en_plyratak
{
    PLYR_ATAK_NONE,
    PLYR_ATAK_SB_KARATE,
    PLYR_ATAK_SB_BASH,
    PLYR_ATAK_SB_BOWL,
    PLYR_ATAK_SB_GUITAR,
    PLYR_ATAK_PAT_BELLY,
    PLYR_ATAK_PAT_CARTWHEEL,
    PLYR_ATAK_PAT_FLOP,
    PLYR_ATAK_PAT_THROW,
    PLYR_ATAK_SLIDE,
    PLYR_ATAK_NOMORE
};

enum en_plyrpup
{
    ePowerupLevelDisabled,
    ePowerupLevelNormal,
    ePowerupLevelLevelMax,
    ePowerupLevelHealthMax,
    ePowerupLevelCount
};

enum en_carystat
{
    zNMECARRY_NONE,
    zNMECARRY_PICKUP,
    zNMECARRY_TWIRL,
    zNMECARRY_THROW,
    zNMECARRY_ATTEMPTPICKUP,
    zNMECARRY_FORCEINT = 0x7fffffff
};

enum en_npcmsg
{
    NME_MID_NONE,
    NME_MID_SYSEVENT,
    NME_MID_RESPAWN,
    NME_MID_DAMAGE,
    NME_MID_EXPLOSION,
    NME_MID_BUNNYHOP,
    NME_MID_NEWDEST,
    NME_MID_DTRON_NMEEMIT,
    NME_MID_DTRON_NMEAVAIL,
    NME_MID_DTRON_ISDEAD,
    NME_MID_CHAT_REQUEST,
    NME_MID_CHAT_DECLINE,
    NME_MID_CHAT_ACCEPT,
    NME_MID_CHAT_DONE,
    NME_MID_CHAT_ABORT,
    NME_MID_TALKSTART,
    NME_MID_TALKON,
    NME_MID_TALKOFF,
    NME_MID_SAWPLYR,
    NME_MID_NEEDMEDIC,
    NME_MID_UNDERATTACK,
    NME_MID_NMEDIED,
    NME_MID_PLYRSPATULA,
    NME_MID_BECOMESCARED,
    NME_MID_NOLONGERSCARED,
    NME_MID_CELEBRATE,
    NME_MID_STUN,
    NME_MID_SCRIPTBEGIN,
    NME_MID_SCRIPTEND,
    NME_MID_SCRIPTHALT,
    NME_MID_NOMORE,
    NME_MID_FORCE = 0x7fffffff
};

enum en_msgdata
{
    NME_MDAT_BLANK,
    NME_MDAT_SYSEVENT,
    NME_MDAT_BLAST,
    NME_MDAT_CHAT,
    NME_MDAT_SPAWN,
    NME_MDAT_TARGET,
    NME_MDAT_DAMAGE,
    NME_MDAT_STUN,
    NME_MDAT_SCRIPT,
    NME_MDAT_MOUNT,
    NME_MDAT_AREANOTIFY,
    NME_MDAT_NOMORE,
    NME_MDAT_FORCE = 0x7fffffff
};

enum en_nmevar
{
    NME_VAR_NONE,
    NME_VAR_1,
    NME_VAR_2,
    NME_VAR_3,
    NME_VAR_4,
    NME_VAR_5,
    NME_VAR_6,
    NME_VAR_7,
    NME_VAR_8,
    NME_VAR_9,
    NME_VAR_NOMORE,
    NME_FORCE = 0x7fffffff
};

// zNMETypeCommon.cpp
en_nmevar NMEVariantIdx(en_npctyp ntyp);

enum en_npcgspot
{
    NME_GSPOT_START = 0x20,
    NME_GSPOT_RESUME,
    NME_GSPOT_LOOP,
    NME_GSPOT_FINISH,
    NME_GSPOT_STARTALT,
    NME_GSPOT_ALTA,
    NME_GSPOT_ALTB,
    NME_GSPOT_PATROLPAUSE,
    NME_GSPOT_NEXT,
    NME_GSPOT_NOMORE,
    NME_GSPOT_FORCEINT = 0x7fffffff
};

enum en_vis
{
    VIS_PREPARE_TO_DIE,
    VIS_HORROR_FLIK,
    VIS_OH_DEAR_GAHD,
    VIS_GET_BACK_HERE,
    VIS_I_SEEE_YOUUU,
    VIS_GREY_AND_FOGGY,
    VIS_LIKE_A_BIRDIE,
    VIS_EARTHWORM_JIM,
    VIS_NOBODY_AROUND,
    VIS_NOMORE
};

enum en_defcon
{
    NME_DEFCON_NORMAL,
    NME_DEFCON_AWARE,
    NME_DEFCON_BATTLE,
    NME_DEFCON_NOMORE
};

enum en_smnote
{
    SM_NOTE_NMEDIED,
    SM_NOTE_NMESTANDBY,
    SM_NOTE_NMEALIVE,
    SM_NOTE_NMELIVINGINSIN,
    SM_NOTE_DUPPAUSE,
    SM_NOTE_DUPRESUME,
    SM_NOTE_DUPSETDELAY,
    SM_NOTE_DUPDEAD,
    SM_NOTE_KILLKIDS,
    SM_NOTE_NOMORE,
    SM_NOTE_FORCE = 0x7fffffff
};

struct zNMEAsset : xDynAsset
{
    xEntAsset ent_asset;
};

// TSSM's counterpart of BfBB's zNPCSettings (PS2 DWARF: 0x1C, no basis type or duplo fields).
struct zNMESettings : xDynAsset
{
    char allowDetect;
    char allowPatrol;
    char allowWander;
    char reduceCollide;
    char useNavSplines;
    char pad[3];
    char allowChase;
    char allowAttack;
    char assumeLOS;
    char assumeFOV;
};

struct Physics
{
    F32 acc_grav;
    F32 spd_maxFall;
};

struct NMECfgCommon
{
    struct Damage
    {
        F32 tym_invuln;
    } damage;
    Physics physics;
    struct Movement
    {
        F32 spd_move;
        F32 acc_move;
        F32 dst_deviant;
        F32 spd_turnrate;
    } movement;
};

struct SimpShadParm
{
    F32 rad_shadow;
    char* nam_shadowTexture;
    RwRaster* rast_shadow;
};

struct FullShadParm
{
    S32 tobeDetermined;
};

struct ShadowRadii
{
    F32 rad_noShadow;
    F32 rad_complexShadow;
};

struct NMEShadParms
{
    SimpShadParm simpshad;
    FullShadParm fullshad;
    ShadowRadii shadrad;
};

struct OriginalBackupData
{
    struct
    {
        S32 flg_enableWander : 1;
        S32 flg_enablePatrol : 1;
        S32 flg_enableDetect : 1;
        S32 flg_activeOn : 1;
        S32 flg_takeNoDamage : 1;
        S32 flg_unused : 27;
    };
    union
    {
        en_allow overrideDetect;
        S32 alignmeproperly;
    };
    union
    {
        en_allow overrideAttack;
        S32 alignmeproperlyToo;
    };
};

struct Restore
{
    U8 chkby;
    U8 penby;
    U8 padpad[2];
    union
    {
        en_npcgol gid_compare;
        S32 gid_COMPARE;
    };
};

struct NMERuntime
{
    struct
    {
        S32 flg_enableWander : 1;
        S32 flg_enablePatrol : 1;
        S32 flg_enableDetect : 1;
        S32 flg_activeOn : 1;
        S32 flg_takeNoDamage : 1;
        S32 flg_unused : 27;
    };
    union
    {
        en_allow overrideDetect;
        S32 alignmeproperly;
    };
    union
    {
        en_allow overrideAttack;
        S32 alignmeproperlyToo;
    };
    OriginalBackupData orig;
    Restore restore;
};

struct zNMEDriver
{
    xEnt* ent_driver;
    F32 tym_mount;
    xMat4x3 mat_parLast;
    xMat4x3 mat_ownerLast;
    struct
    {
        S32 flg_inContact : 1;
        S32 flg_matchOrient : 1;
        S32 flg_unused : 30;
    };

    void VroomVroom(F32 dt, xMat4x3* mat_owner);
    void WheelTurnsYou_C(xMat4x3* mat_owner);
    void ReviewCollide(const xEntCollis* npccol);

    // No GC symbol (always inlined into ReviewCollide); BfBB's zNPCCommon::IsMountableType.
    S32 IsMountableType(en_ZBASETYPE type)
    {
        switch (type)
        {
        case eBaseTypePlatform:
            return 1;
        default:
            return 0;
        }
    }

    // No GC symbol (always inlined into zNMECommon::Common_Reset); the name is not known.
    void Reset()
    {
        ent_driver = NULL;
        tym_mount = -1.0f;
    }
};

struct zNMECommon;

struct zNMENavNet
{
    zMovePoint* nav_past;
    zMovePoint* nav_curr;
    zMovePoint* nav_dest;
    zMovePoint* nav_lead;
    xSpline3* spl_mvptspline;
    F32 len_mvptspline;
    F32 dst_curspline;
    zNMECommon* nme_owner;

    S32 MvptCycle();
    // No GC symbol (always inlined); BfBB's zNPCCommon::MvptReset. Defined in zWadNME.h.
    inline void MvptReset(zMovePoint* nav_goto);
};

struct zNMEArena
{
    S32 flg_arena;
    xVec3 pos_arena;
    F32 rad_arena;
    zMovePoint* nav_arena;
    zMovePoint* nav_refer_dest;
    zMovePoint* nav_refer_curr;

    zMovePoint* NextBestNav(zNMECommon* npc, zMovePoint* nav_from);
    S32 Cycle(zNMECommon* npc, S32 peek);

    // No GC symbol (always inlined into Cycle); BfBB's NPCArena::SyncHomeFromNav.
    void SyncHomeFromNav()
    {
        flg_arena = 0;

        if (nav_arena != NULL)
        {
            xVec3Copy(&pos_arena, nav_arena->PosGet());
            rad_arena = nav_arena->RadiusArena();
        }
        else
        {
            xVec3Copy(&pos_arena, &g_O3);
            rad_arena = -1.0f;
        }
    }

    // No GC symbols (always inlined into the *_SeePlyr callbacks); BfBB's NPCArena has the
    // same helpers. IncludesNPC and NeedToCycle are defined after zNMECommon below.
    S32 IsReady()
    {
        return rad_arena > 0.0f;
    }
    // No GC symbol (always inlined into zNMEGoalFogBattle::CalcEvadePos); BfBB's
    // NPCArena::Radius.
    F32 Radius(F32 rat)
    {
        return rat * rad_arena;
    }
    // No GC symbol (always inlined into zNMEGoalFogBattle::SubStateUpdate); BfBB's
    // NPCArena::PctFromHome: squared XZ distance from home over the squared radius.
    F32 PctFromHome(const xVec3* pos)
    {
        if (!IsReady())
        {
            return -1.0f;
        }

        xVec3 dif = *pos - pos_arena;
        dif.y = 0.0f;
        return xVec3Length2(&dif) / SQR(rad_arena);
    }
    // No GC symbol (always inlined); BfBB's NPCArena::IncludesPos. The squared distance is held in
    // a local (HI's usual 'dsq'), which gives zNMEGoalSpinBattle::Process retail's float registers.
    S32 IncludesPos(xVec3* pos, F32 rad_thresh, xVec3* vec)
    {
        if (!IsReady())
        {
            return 0;
        }

        xVec3 delt = *pos - pos_arena;
        if (vec != NULL)
        {
            *vec = delt;
        }

        delt.y = 0.0f;
        F32 dsq = xVec3Length2(&delt);
        return dsq <= SQR(rad_arena + rad_thresh);
    }
    inline S32 IncludesNPC(zNMECommon* npc, F32 rad_thresh, xVec3* vec);
    inline S32 NeedToCycle(zNMECommon* npc);
};

struct NMESysEvent
{
    S32 doLinkEvents;
    S32 handled;
    xBase* from;
    xBase* to;
    union
    {
        U32 toEvent;
        en_xEventTags toEvent_asEnum;
    };
    F32 toParam[4];
    xBase* toParamWidget;
    U32 toParamWidgetID;
};

struct NMEBlastInfo
{
    xVec3 pos_blast;
    F32 rad_blast;
    F32 spd_expand;
};

struct NMEChatInfo
{
    xVec3 pos_chat;
    F32 tym_chat;
};

struct NMESpawnInfo
{
    xVec3 pos_spawn;
    zMovePoint* nav_firstMovepoint;
    zMovePoint* nav_spawnReference;
    S32 spawnSuccess;
};

struct NMETargetInfo
{
    xBase* bas_tgt;
    xVec3 pos_tgt;
};

struct NMEDamageInfo
{
    en_npcdmg dmg_type;
    xBase* dmg_from;
    xVec3 vec_dmghit;
    S32 amt_damage;
    union
    {
        S32 fac_powerup;
        en_plyrpup pup_player;
    };
};

struct NMEStunInfo
{
    F32 tym_stuntime;
    en_carystat carrystate;
    S32 allowStun;
};

struct NMEScriptInfo
{
    U32 aid_playanim;
};

struct NMEMountInfo
{
    xEnt* ent_toMount;
    xCollis* col_forMount;
};

struct NMEDestInfo
{
    xVec3 pos_there;
    zMovePoint* nav_there;
};

struct NMEAreaInfo
{
    zNMECommon* npc_origin;
    xVec3 pos_origin;
};

struct NMEMsg
{
    en_npcmsg msgid;
    U32 sendto;
    U32 from;
    en_msgdata infotype;
    union
    {
        NMESysEvent sysevent;
        NMEBlastInfo blastarea;
        NMEChatInfo chatter;
        NMESpawnInfo spawning;
        NMETargetInfo target;
        NMEDamageInfo dmgdata;
        NMEStunInfo stundata;
        NMEScriptInfo scriptdata;
        NMEMountInfo mountdata;
        NMEDestInfo destdata;
        NMEAreaInfo areadata;
    };
    void* attached;
    NMEMsg* next;
    F32 tmr_delay;
};

struct zMODEnemy : zModule
{
    st_XORDEREDARRAY nmelist;
    struct
    {
        S32 flg_doBackdoor : 1;
        S32 flg_firstTimestep : 1;
        S32 flg_available : 30;
    };
    F32 tmr_doBeDifferent;

    void ModulePrepUse();
    void ScenePrepare();
    void SceneFinish();
    void ScenePostInit();
    void SceneSetup();
    void ScenePostSetup();
    void SceneReset();
    void Timestep(F32 dt);
    void PostTimestep(F32 dt);
    void RenderToBuckets();
    void PostRender();
    void HudRender();
    // No GC symbol: inlined into its only caller, Timestep (its unused copy is dead-stripped).
    void SomethingWonderfulUpdate() const;

    // No GC symbol (always inlined into Timestep); the name is ours. Runs one fixed-length
    // (PAL/NTSC frame) update of every enemy except Mindy.
    void DoBackdoor()
    {
        S32 num_nme = nmelist.cnt;

        for (S32 i = 0; i < num_nme; i++)
        {
            zNMENPCWrapper* npc = (zNMENPCWrapper*)nmelist.list[i];
            en_npctyp nt = npc->SelfType();

            if (nt == NME_TYPE_MINDY)
            {
                continue;
            }

            if (nt >= NME_TYPE_TIKI && nt <= NME_TYPE_TIKI_STONE)
            {
                npc->Process(gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            }
            else
            {
                npc->UpdateWrapper(gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
            }
        }
    }

    // No GC symbol (always inlined: ScenePostInit, Timestep); the name is ours.
    void SetLightKit(xLightKit* lkit)
    {
        S32 num_nme = nmelist.cnt;

        for (S32 i = 0; i < num_nme; i++)
        {
#ifdef PLATFORM_PC
            // The list holds whole enemies. A host compiler puts their vptr at
            // offset 0, ahead of the xEnt, so the xEnt is reached by a cast that
            // adjusts, not one from void*.
            xModelSetLightKit(((zNMENPCWrapper*)nmelist.list[i])->model, lkit);
#else
            xModelSetLightKit(((xEnt*)nmelist.list[i])->model, lkit);
#endif
        }
    }
};

// No GC symbol (always inlined); defined in zNMECommon.cpp (it needs the file-static
// g_mod_enemy), declared here for zMODSwimmer::ScenePostInit, which comes earlier in zWadNME.
inline S32 zNMECommon_TypeInScene(en_npctyp ntyp);

S32 NMEC_DmgAmtForAttack(en_npcdmg dmgtyp, en_plyrpup* pup_level);
S32 DmgAmtForPlayerAttack(en_plyratak plyratak, en_plyrpup pup);
void NMEC_BuildStandardAnimTran(xAnimTable* table, char** names, S32* anims, S32 flags,
                                F32 blendTime);

// Virtual order must match __vt__10zNMECommon (new virtuals follow the zNMENPCWrapper ones).
struct zNMECommon : zNMENPCWrapper
{
    zNMECommon()
    {
    }
    zNMEAsset* nmeass; // 0x130
    S32 siz_nmeass;
    xPsyche* psy_self; // 0x138
    const NMECfgCommon* cfg_common;
    NMEShadParms* cfg_shadparm;
    S32 flg_vuln; // 0x144
    S32 flg_move;
    S32 flg_misc;
    S32 flg_able;
    F32 spd_throttle;
    NMERuntime runtimeData; // 0x158
    zNMEDriver* drv_data; // 0x178
    zNMENavNet* navnet;
    // The PS2 DWARF dump lists tmr_scary and tmr_lastAlert after this union, but its type
    // table puts them at 0x184/0x188, inside tmr_common (DuploOwner stores npc_duplodude at
    // 0x18C; DfltSysEventMail sets tmr_scary at 0x184).
    union
    {
        F32 tmr_common[3]; // 0x180
        struct
        {
            F32 tmr_invuln;
            F32 tmr_scary;
            F32 tmr_lastAlert;
        };
    };
    zNMECommon* npc_duplodude; // 0x18C
    zNMESoundTable* snd_table; // 0x190
    iSndHandle sndID[10]; // 0x194
    zShrapnelAsset* shrapnelAsset; // 0x1BC

    // zNMENPCWrapper overrides
    xAnimTable* CreateAnimTable(xAnimTable* table);
    void Init(xEntAsset* entasset)
    {
        zNMENPCWrapper::Init(entasset);
        Common_Init(entasset);
    }
    // No GC symbol; the name is ours. The PS2 line table puts all six stores on one line of
    // Common_Init, so they were one inline call, and it keeps Common_Init under GC/2.6's
    // 30-statement limit, which makes retail call the Init functions out of line.
    void CollisionDefaults()
    {
        collType = XENT_COLLTYPE_NPC;
        collLev = 4;
        bound.type = XBOUND_TYPE_SPHERE;
        moreFlags |= 0x10;
        collis = NULL;
        colFreq = -1;
    }
    // No GC symbol; inlined into zNMETurBarrel::Init (and, per the PS2 demo, zNMEStandard::Init).
    // The name is ours: sets the default runtime flags and saves them as the originals that
    // Common_Reset restores.
    void RuntimeDefaults()
    {
        runtimeData.flg_enableWander = 1;
        runtimeData.flg_enablePatrol = 1;
        runtimeData.flg_enableDetect = 1;
        runtimeData.flg_activeOn = 1;
        runtimeData.flg_takeNoDamage = 0;
        runtimeData.overrideDetect = ALLOW_NORMAL;
        runtimeData.overrideAttack = ALLOW_NORMAL;
        runtimeData.flg_unused = 0;

        runtimeData.orig.flg_enableWander = runtimeData.flg_enableWander;
        runtimeData.orig.flg_enablePatrol = runtimeData.flg_enablePatrol;
        runtimeData.orig.flg_enableDetect = runtimeData.flg_enableDetect;
        runtimeData.orig.flg_activeOn = runtimeData.flg_activeOn;
        runtimeData.orig.flg_takeNoDamage = runtimeData.flg_takeNoDamage;
        runtimeData.orig.overrideDetect = runtimeData.overrideDetect;
        runtimeData.orig.overrideAttack = runtimeData.overrideAttack;
    }
    void Setup();
    // Weak in the target (inline). Common_Reset is defined in zWadNME.h (it needs zNMEGoalDead).
    void Reset()
    {
        zNMENPCWrapper::Reset();
        Common_Reset();
    }
    void BUpdate(xVec3* pos)
    {
        xEntDefaultBoundUpdate(this, pos);
    }
    S32 SysEvent(xBase* from, xBase* to, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                 U32 toParamWidgetID, S32* handled);
    void Process(F32 dt)
    {
        for (S32 i = 0; i < 3; i++)
        {
            tmr_common[i] = MAX(-1.0f, tmr_common[i] - dt);
        }
    }
    // Weak in the target (GC only; called by zNMEFrogfish::Process). Same body as Process.
    void Common_TimerUpd(F32 dt)
    {
        for (S32 i = 0; i < 3; i++)
        {
            tmr_common[i] = MAX(-1.0f, tmr_common[i] - dt);
        }
    }
    void Destroy()
    {
        SelfDestroy();
    }
    void Render()
    {
        xEntRender(this);
    }
    void SelfDestroy()
    {
        xBehaveMgr* bmgr = xBehaveMgr_GetSelf();
        if (psy_self)
        {
            bmgr->UnSubscribe(psy_self);
        }
        psy_self = NULL;
    }

    // new virtuals
    virtual void CreateConfig(const char* name);
    virtual void ProcessInvis(F32 dt)
    {
    }
    virtual void PostProcess(F32 dt)
    {
    }
    virtual S32 TypeHandleMail(NMEMsg* mail)
    {
        return 0;
    }
    virtual U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
    {
        return 0;
    }
    virtual S32 GoalPick(xGoal* goal, S32& nextgoal, en_trantype& trantype)
    {
        return 0;
    }
    virtual S32 IsAlive()
    {
        return 1;
    }
    virtual S32 IsHealthy()
    {
        return IsAlive();
    }
    virtual S32 IsPresent()
    {
        return runtimeData.flg_activeOn && xEntIsVisible(this) && IsAlive();
    }
    virtual en_nmesimp SimpleStatus()
    {
        return Common_SimpStat();
    }
    virtual F32 HealthRatio()
    {
        en_nmesimp simp = SimpleStatus();
        return (simp == NME_SIMP_DEAD) ? 0.0f : 1.0f;
    }
    virtual void Boo(const zNMECommon* npc)
    {
    }
    virtual void Damage(en_npcdmg dmgtype, const xBase* who, const xVec3* vec_hit,
                        S32 amt_dmgCaller, en_plyrpup pup_fromCaller);
    virtual en_vis VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const
    {
        return VIS_NOBODY_AROUND;
    }
    virtual void DuploOwner(zNMECommon* duper)
    {
        npc_duplodude = duper;
    }
    virtual void DuploNotice(en_smnote note, void* data)
    {
    }
    virtual void DieTheGoodDeath();
    virtual void DeathFXKick();
    virtual void GiveReward() const;
    virtual void SpeakBegin()
    {
    }
    virtual void SpeakEnd()
    {
    }
    virtual void SpeakStart(U32 sound, U32 param_2, S32 param_3)
    {
    }
    virtual void SpeakStop()
    {
    }
    virtual S32 CanRope()
    {
        return flg_vuln & 0x1000000;
    }
    virtual S32 SetCarryState(en_carystat state)
    {
        return 0;
    }
    // zNMENPCWrapper override declared among the new virtuals (an override keeps its base slot):
    // retail emits this weak copy between SetCarryState and MvptFirstFollow.
    void CollideReview()
    {
    }
    virtual zMovePoint* MvptFirstFollow()
    {
        return NULL;
    }
    virtual zMovePoint* MvptFirstTarget()
    {
        return NULL;
    }
    virtual xEntFrame* Gimme_xEntFrame()
    {
        return NULL;
    }
    virtual zNMEDriver* Gimme_DriveData()
    {
        return NULL;
    }
    virtual xEntShadow* Gimme_ShadowData()
    {
        return NULL;
    }
    virtual zNMENavNet* Gimme_NavNet()
    {
        return NULL;
    }
    virtual zNMEArena* Gimme_NMEArena()
    {
        return NULL;
    }
    virtual xShadowSimpleCache* Gimme_SimpShadowCache()
    {
        return NULL;
    }
    virtual S32 WannaPoolShadow()
    {
        return 0;
    }

    // No GC symbol (always inlined into many callers; the name is ours). Plays entry idx of
    // snd_table and returns its handle (-1 when nothing was played). Defined in zWadNME.h (it
    // needs globals).
    inline iSndHandle SndPlayFromTable(S32 idx);
    void PoofOfBubbles() const;
    S32 LaunchProjectile(en_haztyp haztyp, F32 spd_proj, F32 dst_minRange, const xVec3* pos_launch,
                         F32 tym_predictMax, F32 hyt_offset, F32 rad_min, F32 rad_max);
    F32 ThrottleAdjust(F32 dt, F32 spd_want, F32 accel);
    // No GC symbol and no PS2 DWARF entry (always inlined, e.g. into zNMEGoalPatrol::Process);
    // named after BfBB's zNPCCommon::ThrottleAccel, whose shape the callers match (acc and spd_top
    // are computed before the throttle is updated).
    F32 ThrottleAccel(F32 dt, S32 speedup, F32 pct_max)
    {
        F32 acc = dt * cfg_common->movement.acc_move;
        F32 spd_top = pct_max * cfg_common->movement.spd_move;

        if (speedup)
        {
            spd_throttle += acc;
        }
        else
        {
            spd_throttle -= acc;
        }

        spd_throttle = CLAMP(spd_throttle, 0.0f, spd_top);

        return spd_throttle;
    }
    xAnimTransition* AnimMakeSmackTran(U32 animID) const;
    S32 AnimStart(U32 animID, S32 forceRestart);
    // Weak in the target. The tikis have no animation.
    xAnimSingle* AnimCurSingle() const
    {
        if (model->Anim == NULL && itemType >= NME_TYPE_TIKI && itemType <= NME_TYPE_TIKI_STONE)
        {
            return NULL;
        }
        return model->Anim->Single;
    }
    // No GC symbols (always inlined); BfBB's zNPCCommon::AnimCurState/AnimCurStateID.
    xAnimState* AnimCurState() const
    {
        if (model->Anim == NULL && itemType >= NME_TYPE_TIKI && itemType <= NME_TYPE_TIKI_STONE)
        {
            return NULL;
        }
        return model->Anim->Single->State;
    }
    // No GC symbol (always inlined); body as BfBB's zNPCCommon::AnimCurStateID (if/else form; a
    // ?: return colours callers' registers differently, e.g. zNMEGoalSpinBattle::PursueAndDestroy).
    U32 AnimCurStateID() const
    {
        xAnimState* state = AnimCurState();
        if (state != NULL)
        {
            return state->ID;
        }
        else
        {
            return 0;
        }
    }
    // No GC symbol (always inlined); BfBB's zNPCCommon::AnimTimeCurrent.
    F32 AnimTimeCurrent() const
    {
        return model->Anim->Single->Time;
    }
    // Weak in the target; BfBB's zNPCCommon::AnimDuration, but TSSM returns early for a given
    // state (retail returns ast->Data->Duration twice; no PS2 DWARF entry).
    F32 AnimDuration(xAnimState* ast) const
    {
        if (ast != NULL)
        {
            return ast->Data->Duration;
        }
        ast = AnimCurState();
        return (ast != NULL) ? ast->Data->Duration : 0.0f;
    }
    // No GC symbol (always inlined); BfBB's zNPCCommon::AnimTimeRemain.
    F32 AnimTimeRemain(xAnimState* ast) const
    {
        return AnimDuration(ast) - AnimTimeCurrent();
    }
    // No GC symbol (always inlined); BfBB's zNPCCommon::AnimFindState.
    xAnimState* AnimFindState(U32 animID)
    {
        return xAnimTableGetStateID(model->Anim->Table, animID);
    }
    // No symbol in TSSM or the PS2 DWARF (always inlined); BfBB's zNPCCommon::Pos.
    xVec3* Pos()
    {
        return (xVec3*)&model->Mat->pos;
    }
    // No symbol in TSSM or the PS2 DWARF (always inlined); BfBB's zNPCCommon::BonePos.
    RwV3d* BonePos(S32 bone) const
    {
        return &model->Mat[bone].pos;
    }
    // No symbol in TSSM or the PS2 DWARF (always inlined; one PS2 source line in each caller, and
    // retail keeps the source pointer across the component stores while reloading model). Name
    // from BfBB's zNPCCommon::MatPosSet, which also NULL-checks pos; TSSM's callers have no check.
    void MatPosSet(const xVec3* pos)
    {
        model->Mat->pos.x = pos->x;
        model->Mat->pos.y = pos->y;
        model->Mat->pos.z = pos->z;
    }
    // Weak in the target (GC only, no PS2 symbol; called by zNMEFrogfish::Process). The name is
    // the GC symbol's, the parameter name is ours.
    void MatSet(xMat3x3* mat)
    {
        *(xMat3x3*)model->Mat = *mat;
    }
    S32 MatMoveToPos(const xVec3* pos_tobe, F32 dt, F32 spd, S32 doTurn, F32 spd_turnrate);
    F32 MatQRotTowards(F32 dt, const xVec3* dir_face, F32 spd_turnrate);
    F32 MatTurnTowardPos(const xVec3* pos, F32 dt, F32 spd_turnrate, S32 flags);
    F32 MatTurnAntiPos(const xVec3* pos, F32 dt, F32 spd_turnrate, S32 flags);
    // Weak in the target; XZDstSqToPlayer is defined in zWadNME.h (it needs xglobals). The
    // direct model->Mat access (not xEntGetPos) lets zNMEGoalFogBattle::SubStateUpdate reuse its
    // model load in the inlined CheckSpot, as retail does.
    void XZVecToPos(xVec3* vec, const xVec3* pos, F32* dy) const
    {
        xVec3Sub(vec, pos, (xVec3*)&model->Mat->pos);
        if (dy != NULL)
        {
            *dy = vec->y;
        }
        vec->y = 0.0f;
    }
    // No GC symbol (always inlined); BfBB's zNPCCommon::XYZVecToPos. Retail reuses the caller's
    // model load here, which only happens with a direct model->Mat access (not xEntGetPos).
    void XYZVecToPos(xVec3* vec, const xVec3* pos)
    {
        xVec3Sub(vec, pos, (xVec3*)&model->Mat->pos);
    }
    // No GC symbol (always inlined); BfBB's zNPCCommon::XYZDstSqToPos.
    F32 XYZDstSqToPos(const xVec3* pos, xVec3* vec)
    {
        xVec3 dir;

        if (vec == NULL)
        {
            vec = &dir;
        }

        XYZVecToPos(vec, pos);
        return xVec3Length2(vec);
    }
    // No symbol in TSSM or the PS2 DWARF (always inlined); name unknown. The spline movers evaluate
    // the position once before testing flg_move, which needs this test one inline level deeper than
    // the caller.
    F32 DstSqToPos(const xVec3* pos)
    {
        return (flg_move & 0x2) ? XZDstSqToPos(pos, NULL, NULL) : XYZDstSqToPos(pos, NULL);
    }
    // No GC symbol (always inlined); BfBB's zNPCCommon::XZDstSqToPos.
    F32 XZDstSqToPos(const xVec3* pos, xVec3* vec, F32* dy) const
    {
        xVec3 dir;

        if (vec == NULL)
        {
            vec = &dir;
        }

        XZVecToPos(vec, pos, dy);
        return xVec3Length2(vec);
    }
    inline F32 XZDstSqToPlayer(xVec3* vec, F32* dy) const;
    // No GC symbol (always inlined); BfBB's zNPCCommon::XYZVecToPos / XYZDstSqToPos (the
    // turret barrel's dust test stores the delta to a stack vector and sums unfused squares).
    void XYZVecToPos(xVec3* vec, const xVec3* pos) const
    {
        xVec3Sub(vec, pos, xEntGetPos(this));
    }
    F32 XYZDstSqToPos(const xVec3* pos, xVec3* vec) const
    {
        xVec3 dir;

        if (vec == NULL)
        {
            vec = &dir;
        }

        XYZVecToPos(vec, pos);
        return xVec3Length2(vec);
    }
    inline F32 XYZDstSqToPlayer(xVec3* vec) const;
    // Weak in the target (zNMETypeCommon.h section).
    void MatFacePlayer(F32 dt, F32 spd_turnrate, S32 flags)
    {
        MatTurnTowardPos(xEntGetPos(xglobals->___player_ent_dont_use_directly), dt, spd_turnrate,
                         flags);
    }
    void MatFaceAntiPlayer(F32 dt, F32 spd_turnrate, S32 flags)
    {
        MatTurnAntiPos(xEntGetPos(xglobals->___player_ent_dont_use_directly), dt, spd_turnrate,
                       flags);
    }
    // No GC symbol (always inlined); BfBB's zNPCCommon::ModelAtomicFind.
    xModelInstance* ModelAtomicFind(S32 index, S32 idx_prev, xModelInstance* mdl_prev)
    {
        xModelInstance* da_atomic = NULL;
        xModelInstance* minst = mdl_prev;
        S32 midx = 0;

        if (minst != NULL && idx_prev >= 0)
        {
            midx = idx_prev;
        }
        else
        {
            minst = model;
        }

        while (minst != NULL)
        {
            if (midx == index)
            {
                da_atomic = minst;
                break;
            }
            minst = minst->Next;
            midx++;
        }

        return da_atomic;
    }
    // No GC symbol (always inlined); BfBB's zNPCCommon::ModelAtomicShow.
    xModelInstance* ModelAtomicShow(S32 index, xModelInstance* mdl)
    {
        xModelInstance* minst = mdl ? mdl : ModelAtomicFind(index, -1, NULL);
        if (!minst)
        {
            return minst;
        }

        minst->Flags |= 0x2;
        minst->Flags |= 0x1;

        return minst;
    }
    S32 HaveLOSToPos(xVec3* pos, F32 dist, xScene* xscn, xBase* tgt, xCollis* colCallers);
    S32 ReviewCollBounce(const xEntCollis* npccol, xVec3* vel, F32 fac_elastic);
    S32 ReviewCollSurfs(const xEntCollis* npccol, zNMECommon* npc);
    void RunCollision(F32 dt);
    void Common_Collide();
    en_nmesimp Common_SimpStat();
    void ApplyGlowDamp();
    void ShadStufUpdate();
    void ShadStufIAmDead();
    void ShadChkStatus();
    void FullShadAcquire();
    inline void ShadowPrep();
    static void TellMeVisOnOff(xEnt* ent);
    S32 Respawn(const xVec3* pos, zMovePoint* mvptFirst, zMovePoint* mvptSpawnRef);
    void ConvertHitEvent(NMESysEvent* sysmail);
    S32 DfltSysEventMail(NMESysEvent* sysmail);
    // Weak in the target (GC only; the PS2 build has no such function).
    S32 DfltHandleMail(NMEMsg* mail)
    {
        if (mail->msgid == NME_MID_SYSEVENT)
        {
            return DfltSysEventMail(&mail->sysevent);
        }
        return 0;
    }
    // No GC symbol (always inlined); defined in zWadNME.h (it needs zNMEGoalCommon).
    inline S32 NMEMessage(NMEMsg* mail);
    void Common_Init(xEntAsset* entasset);
    // No GC symbol (always inlined into Reset); defined in zWadNME.h.
    inline void Common_Reset();
};

// zEntPlayer.cpp
void VillainNoticesPlayer(zNMECommon* nme);
S32 zNMECommon_MaxShadowNeededLater();
U8 zNMECommon_IsConversation();
U32 xSndIsPlaying(U32 assetID, U32 parid);

en_plyrpup zEntPlayer_PowerupLevel(en_plyratak forThisAttack);

void ZNME_Common_Startup();

inline F32 zNMECommon::XZDstSqToPlayer(xVec3* vec, F32* dy) const
{
    return XZDstSqToPos(xEntGetPos(xglobals->___player_ent_dont_use_directly), vec, dy);
}

inline F32 zNMECommon::XYZDstSqToPlayer(xVec3* vec) const
{
    return XYZDstSqToPos(xEntGetPos(xglobals->___player_ent_dont_use_directly), vec);
}

inline S32 zNMEArena::IncludesNPC(zNMECommon* npc, F32 rad_thresh, xVec3* vec)
{
    return IncludesPos(xEntGetPos(npc), rad_thresh, vec);
}

inline S32 zNMEArena::NeedToCycle(zNMECommon* npc)
{
    S32 rc;

    if (IsReady() && !IncludesNPC(npc, 0.0f, NULL) && !(flg_arena & 0x4))
    {
        return 2;
    }

    if (npc->navnet->nav_dest != nav_refer_dest || npc->navnet->nav_curr != nav_refer_curr)
    {
        return 1;
    }

    if (!IsReady() && !(flg_arena & 0x4))
    {
        rc = 2;
    }
    else
    {
        rc = 0;
    }

    return rc;
}

#endif
