#include "zNMETypeBossDennis.h"
#include "zNMESupport.h"
#include "zEntCruiseBubble.h"
#include "zNMEHealthMeter.h"
#include "xGroup.h"
#include "xDebug.h"

U32 g_hash_bossanim[14];
char* g_strz_bossanim[14] = {
    "Unknown", "Idle",   "Move",        "Idle_Alert",  "Move_Alert",  "Taunt",     "Victory",
    "Damage",  "Defeat", "Attack_Prep", "Attack_Loop", "Attack_Done", "Leap_Loop", "Leap_Done",
};
U32 g_hash_dennanim[31];
char* g_strz_dennanim[31] = {
    "Unknown",   "Idle01",     "Idle02",     "Idle03",     "Idle04",     "Move01",   "LeapPrep",
    "LeapDone",  "LeapLoop01", "LeapLoop02", "LeapLoop03", "LeapLoop04", "Attack01", "Attack02",
    "Attack03",  "Attack04",   "Taunt01",    "Taunt02",    "Taunt03",    "Taunt04",  "Victory01",
    "Victory02", "Victory03",  "Victory04",  "Damage01",   "Damage02",   "Damage03", "Damage04",
    "Defeat01",  "Defeat02",   "Defeat03",
};

namespace Dennis
{
    namespace Config
    {
        NMECfgCommon cfg_common_Dennis = {
            { -1.0f },
            { 10.0f, -5.0f },
            { 1.0f, 3.0f, 0.1f, 2.0f * PI },
        };

        NMECfgDennis cfg_junkyard_s1 = {
            { 16 },
            { { 9.5f, 10.0f }, 9980.0f, 9985.0f, 9990.0f, 9995.0f },
            { 9.0f, 1, 0.95f, 0.0f },
            { 3.0f, 16.0f, 3 },
        };

        NMECfgDennis cfg_junkyard_s2 = {
            { 16 },
            { { 10.0f, 12.5f }, 9980.0f, 9985.0f, 9990.0f, 9995.0f },
            { 12.0f, 1, 0.95f, 0.0f },
            { 2.0f, 12.0f, 2 },
        };

        NMECfgDennis cfg_junkyard_s3 = {
            { 16 },
            { { 10.0f, 14.5f }, 9980.0f, 9985.0f, 9990.0f, 9995.0f },
            { 15.0f, 1, 0.95f, 3.0f },
            { 1.5f, 8.0f, 1 },
        };

        // Indexed by en_denstage.
        NMECfgDennis* cfg_junkStages[DEN_STAGE_NOMORE] = {
            &cfg_junkyard_s1, &cfg_junkyard_s1, &cfg_junkyard_s1,
            &cfg_junkyard_s2, &cfg_junkyard_s3, &cfg_junkyard_s1,
        };
    } // namespace Config
} // namespace Dennis

namespace Dennis
{
    namespace Camera
    {
        static const zBossCam_Binary::config cfg_bossCam = {
            { 6.0f, 3.5f, 2.0f },
            { 1.2f, 6.2f, -1.0f },
            { 1.0f, 0.2f, 1.5f },
            10.0f,
            10.0f,
            10.0f,
            15.0f,
            50.0f,
            -0.2f,
        };
    } // namespace Camera

    namespace Evade
    {
        namespace Ribbon
        {
            static const xFXRibbon::curve_node curvedata[10] = {
                { 0.0f, { 0xFF, 0x32, 0x84, 0xFF }, 1.0f },
                { 0.11f, { 0xFF, 0x32, 0x84, 0xFF }, 1.2f },
                { 0.22f, { 0xFF, 0x32, 0x84, 0xFF }, 2.0f },
                { 0.33f, { 0xFF, 0x32, 0x84, 0xFF }, 1.6f },
                { 0.44f, { 0xFF, 0x32, 0x84, 0xFF }, 1.2f },
                { 0.55f, { 0xFF, 0x32, 0x84, 0xFF }, 1.0f },
                { 0.66f, { 0xFF, 0x32, 0x84, 0xFF }, 0.8f },
                { 0.77f, { 0xFF, 0x32, 0x84, 0xFF }, 0.6f },
                { 0.88f, { 0xFF, 0x32, 0x84, 0xFF }, 0.4f },
                { 1.0f, { 0xFF, 0x32, 0x84, 0xFF }, 0.0f },
            };

            NMEHelpRib::RibData rib_normal = {
                { 0, 1, 0, 0 },
                { "lightning", (xFXRibbon::curve_node*)curvedata, 10, 1.24f, 0.5f },
                { 1, 1.0f, 1.0f },
                { NULL, 0 },
            };
        } // namespace Ribbon
    } // namespace Evade

    namespace Attack
    {
        static const xVec3 vec_boneOffset = { 0.0f, -0.7f, 1.25f };
    } // namespace Attack

    namespace MoveBlur
    {
        BlurData jumpBlur = {
            { 1, 0, 0, 0 },
            { 0.3f, 1.0f, 0.5f },
            { 0, 2.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0.15f, 100.0f, 16 },
        };
    } // namespace MoveBlur

    NMEHelpBound::BndData bnd_info = {
        k_XBOUNDTYPE_BOX,
        { 0, 0, { 0, 0 } },
        { 1.5f, { 0.0f, 3.0f, 0.0f } },
        { { 2.0f, 3.0f, 2.0f }, { 0.0f, 1.5f, 1.0f } },
    };

    namespace HealthBar
    {
        NMEHealthMeter::MeterData mtr_health = {
            { 1, 0, { 0, 0 } },
            { { -1, 16, 16, 16 }, "particle_cloud", 0.14f, 0.25f },
            { { -1, 10, 10, 10 } },
        };
    } // namespace HealthBar

    namespace OneLiners
    {
        zStreamedSound vo_data[4] = {
            { "ONEL_BD1_BOSS_HIT_PLAYER", 10.0f, 0x1080, 0.0f, (iSndGroupHandle)-1 },
            { "ONEL_BD1_HIT_PLAYER", 10.0f, 0x1080, 0.0f, (iSndGroupHandle)-1 },
            { "ONEL_BD1_HIT_BOSS", 10.0f, 0x1400, 0.0f, (iSndGroupHandle)-1 },
            { NULL, 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
        };
    } // namespace OneLiners
} // namespace Dennis

// flg_wonder is a static of zNMECommon.cpp, which comes later in zWadNME.cpp; VisionTarget needs
// this forward declaration (MWCC keeps the later static's internal linkage).
namespace NME
{
    namespace GlobalData
    {
        extern S32 flg_wonder;
    } // namespace GlobalData
} // namespace NME

namespace Dennis
{
    namespace GlobalData
    {
        static S32 allowEvasion = 1;
        static S32 lifeIsWar = 1;
        static S32 useTooCloseEvasion = 1;
    } // namespace GlobalData
} // namespace Dennis

// No GC symbol: inlined into zNMECentral_Startup (its unused copy is dead-stripped).
void ZNME_Boss_Startup()
{
    for (S32 i = 0; i < 14; i++)
    {
        g_hash_bossanim[i] = xStrHash(g_strz_bossanim[i]);
    }
    for (S32 i = 0; i < 31; i++)
    {
        g_hash_dennanim[i] = xStrHash(g_strz_dennanim[i]);
    }
    for (S32 i = 0; i < 31; i++)
    {
        g_hash_dentanim[i] = xStrHash(g_strz_dentanim[i]);
    }
}

void zNMEDennis::CreateConfig(const char* name)
{
    zNMECommon::CreateConfig(name);
    cfg_common = &Dennis::Config::cfg_common_Dennis;
    cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_UNKNOWN];
}

S32 zNMEDennis::DfltVulnFlags()
{
    return 0xc07b0007;
}

void zNMEDennis::SelfSetup()
{
    xBehaveMgr* bmgr;
    xPsyche* psy;

    bmgr = xBehaveMgr_GetSelf();
    psy_self = bmgr->Subscribe(this, 0);

    psy = psy_self;
    psy->BrainBegin();

    psy->AddGoal(NME_GOAL_DENNIS_NORMAL, NULL);
    psy->AddGoal(NME_GOAL_DENNIS_BATTLE, NULL);
    psy->AddGoal(NME_GOAL_DENNIS_EVADE, NULL);
    psy->AddGoal(NME_GOAL_DENNIS_ATTACK, NULL);
    psy->AddGoal(NME_GOAL_DENNIS_TAUNT, NULL);
    psy->AddGoal(NME_GOAL_DENNIS_DAMAGE, NULL);
    psy->AddGoal(NME_GOAL_DENNIS_DEAD, NULL);

    psy->BrainEnd();
    psy->SetSafety(NME_GOAL_DENNIS_NORMAL);

    psy_self->cb_notice = &tellMeWhen;
}

zMovePoint* zNMEDennis::MvptFirstFollow()
{
    zMovePoint* nav;

    if (((zNMEAssetDennis*)nmeass)->aid_mvptFollow == 0)
    {
        return NULL;
    }

    nav = zMovePoint_From_xAssetID(((zNMEAssetDennis*)nmeass)->aid_mvptFollow);
    return nav;
}

U32 zNMEDennis::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
{
    S32 idx = 0;
    S32 rnd = xurand() * 3.99f;

    switch (gid)
    {
    case NME_GOAL_DENNIS_DEAD:
    case NME_GOAL_DEV_ANIMVIEW:
        idx = 1;
        break;
    case NME_GOAL_DENNIS_NORMAL:
    case NME_GOAL_DENNIS_BATTLE:
        idx = rnd + 1;
        break;
    case NME_GOAL_DENNIS_EVADE:
        if (gspot == NME_GSPOT_START)
        {
            idx = 6;
        }
        else if (gspot == NME_GSPOT_FINISH)
        {
            idx = 7;
        }
        else if (gspot == NME_GSPOT_LOOP)
        {
            idx = rnd + 8;
        }
        break;
    case NME_GOAL_DENNIS_ATTACK:
        idx = rnd + 12;
        break;
    case NME_GOAL_DENNIS_TAUNT:
        if (globals.player.s->Health < 1)
        {
            idx = rnd + 20;
        }
        else
        {
            idx = rnd + 16;
        }
        break;
    case NME_GOAL_DENNIS_DAMAGE:
        if (pts_health < 1)
        {
            idx = (S32)(xurand() * 2.99f) + 28;
        }
        else
        {
            idx = rnd + 24;
        }
        break;
    default:
        idx = rnd + 1;
        break;
    }

    return g_hash_dennanim[idx];
}

S32 zNMEDennis::GoalPick(xGoal* goal, S32& gid_would, en_trantype& trantype)
{
    S32 altered = 1;

    switch (gid_would)
    {
    case NME_GOAL_TRIGGER_NORMAL:
        gid_would = NME_GOAL_DENNIS_NORMAL;
        break;
    case NME_GOAL_TRIGGER_ALERT:
    case NME_GOAL_TRIGGER_BATTLE:
        gid_would = NME_GOAL_DENNIS_BATTLE;
        break;
    case NME_GOAL_TRIGGER_ATTACK:
        gid_would = NME_GOAL_DENNIS_ATTACK;
        break;
    case NME_GOAL_TRIGGER_VINIVICIVIDI:
        gid_would = NME_GOAL_DENNIS_TAUNT;
        break;
    case NME_GOAL_TRIGGER_WOUND:
        gid_would = NME_GOAL_DENNIS_DAMAGE;
        break;
    default:
        altered = 0;
        break;
    }

    return altered;
}

xAnimTable* zNMEDennis::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMEDennis", 0);
    }

    CreateDennisAndSBBatAnimTable(table);
    return table;
}

void CreateDennisAndSBBatAnimTable(xAnimTable* table)
{
    xAnimTableNewState(table, g_strz_dennanim[1], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[2], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[3], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[4], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_dennanim[5], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_dennanim[16], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[17], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[18], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[19], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_dennanim[20], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[21], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[22], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[23], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_dennanim[24], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[25], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[26], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[27], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_dennanim[28], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[29], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[30], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_dennanim[12], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[13], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[14], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[15], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_dennanim[6], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[7], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_dennanim[8], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[9], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[10], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dennanim[11], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
}

void zNMEDennis::Init(xEntAsset* asset)
{
    zNMECommon::Init(asset);

    cam_boss.cfg = Dennis::Camera::cfg_bossCam;
    cam_boss.create();
    cam_boss.cfg_common.priority = 5;
    cam_boss.cfg_common.blend_time = 1.0f;
    cruise_bubble::xCamLaunchCam = &cam_boss;

    moveBlur.create();

    zNMEParFX_PartyInvite(NPAR_TYP_POPGAS, NULL, 0);

    oneLiners.InitWithSounds(Dennis::OneLiners::vo_data, 3);
}

// No symbol (always inlined into Setup; the name is ours): sets up the health meter's rasters,
// fade times and stage health.
static inline void DenMeterSetup()
{
    NMEHealthMeter::MeterData* meter = &Dennis::HealthBar::mtr_health;
    NMECfgDennis* cfg_a;
    NMECfgDennis* cfg_b;
    NMECfgDennis* cfg_c;

    meter->runtime.rast_inner = NMEC_FindRWRaster("fx_boss_bar");
    meter->runtime.rast_outer = NMEC_FindRWRaster("fx_boss_graph");
    meter->runtime.rast_solid_bar = NMEC_FindRWRaster("fx_boss_solid_bar");
    meter->runtime.rast_broken_bar = NMEC_FindRWRaster("fx_boss_broken_bar");

    meter->runtime.rast_logo = NMEC_FindRWRaster("uii_boss_meter_b101");
    if (meter->runtime.rast_logo == NULL)
    {
        meter->runtime.rast_logo = NMEC_FindRWRaster("uii_boss_meter_b201");
    }
    if (meter->runtime.rast_logo == NULL)
    {
        meter->runtime.rast_logo = NMEC_FindRWRaster("uii_boss_meter_b301");
    }
    if (meter->runtime.rast_logo == NULL)
    {
        meter->runtime.rast_logo = NMEC_FindRWRaster("uii_boss_meter_b401");
    }

    meter->cfginfo.tym_fadeIn = 0.14f;
    meter->cfginfo.tym_fadeOut = 0.25f;

    cfg_a = Dennis::Config::cfg_junkStages[DEN_STAGE_A];
    cfg_b = Dennis::Config::cfg_junkStages[DEN_STAGE_B];
    cfg_c = Dennis::Config::cfg_junkStages[DEN_STAGE_C];

    meter->cfginfo.stageHealth[1] = cfg_a->health.pts_healthMax;
    meter->cfginfo.stageHealth[2] = cfg_b->health.pts_healthMax;
    meter->cfginfo.stageHealth[3] = cfg_c->health.pts_healthMax;
    meter->status.healthCurr[1] = cfg_a->health.pts_healthMax;
    meter->status.healthCurr[2] = cfg_b->health.pts_healthMax;
    meter->status.healthCurr[3] = cfg_c->health.pts_healthMax;
}

void zNMEDennis::Setup()
{
    zNPCMgr* mgr;
    const char* nam_sbbat;
    U32 aid_sbbat;

    zNMECommon::Setup();

    Dennis::Evade::Ribbon::rib_normal.FindTexture();

    if (itemType == NME_TYPE_DENNIS_V1)
    {
        mgr = zNPCMgrSelf();
        nam_sbbat = "SB_NPC_01";
        aid_sbbat = xStrHash(nam_sbbat);
        npc_sbbat = (zNMESBBat*)mgr->FindNPC(aid_sbbat);
    }

    cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_UNKNOWN];
    pts_health = cfg_dennis->health.pts_healthMax;
    mod_camWant = DEN_CAMMOD_NORMAL;
    tmr_nextLeap = -1.0f;
    stageCurr = DEN_STAGE_UNKNOWN;

    cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_INTRO];
    pts_health = cfg_dennis->health.pts_healthMax;
    mod_camWant = DEN_CAMMOD_NORMAL;
    tmr_nextLeap = -1.0f;
    stageCurr = DEN_STAGE_INTRO;

    psy_self->GoalSet(NME_GOAL_DENNIS_NORMAL, 0);

    DenMeterSetup();
}

void zNMEDennis::Reset()
{
    xPsyche* psy;

    zNMECommon::Reset();

    MvptGroupFind();
    nav_evade = PickEvadePoint(NULL);

    psy = psy_self;

    pts_health = cfg_dennis->health.pts_healthMax;
    flg_vuln = DfltVulnFlags();

    mod_camCurr = DEN_CAMMOD_UNKNOWN;
    cam_boss.attach(xglobals->___player_ent_dont_use_directly);
    cam_boss.cfg_common.priority = 5;
    cam_boss.cfg_common.blend_time = 1.0f;
    cruise_bubble::Cam_Launch = NULL;
    cruise_bubble::Cam_Return = NULL;

    Dennis::HealthBar::mtr_health.status.healthCurr[1] =
        Dennis::Config::cfg_junkStages[DEN_STAGE_A]->health.pts_healthMax;
    Dennis::HealthBar::mtr_health.status.healthCurr[2] =
        Dennis::Config::cfg_junkStages[DEN_STAGE_B]->health.pts_healthMax;
    Dennis::HealthBar::mtr_health.status.healthCurr[3] =
        Dennis::Config::cfg_junkStages[DEN_STAGE_C]->health.pts_healthMax;

    flg_firstTimestep = 1;

    cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_UNKNOWN];
    pts_health = cfg_dennis->health.pts_healthMax;
    mod_camWant = DEN_CAMMOD_NORMAL;
    tmr_nextLeap = -1.0f;
    stageCurr = DEN_STAGE_UNKNOWN;

    psy->GoalSet(NME_GOAL_DENNIS_NORMAL, 0);

    tmr_nextLeap = -1.0f;
    cnt_attacksTilEvade = -1;
}

void zNMEDennis::Destroy()
{
    cruise_bubble::Cam_Launch = NULL;
    cruise_bubble::Cam_Return = NULL;
    cruise_bubble::xCamLaunchCam = &cam_boss;
    cruise_bubble::xCamLaunchCam->destroy();

    zNMEBoss::Destroy();
}

S32 zNMEDennis::IsAlive()
{
    if (stageCurr == DEN_STAGE_A)
    {
        return 1;
    }
    if (stageCurr == DEN_STAGE_B)
    {
        return 1;
    }
    if (stageCurr == DEN_STAGE_C)
    {
        return 1;
    }
    return 0;
}

void zNMEDennis::Process(F32 dt)
{
    NMEHealthMeter::MeterData* meter;

    if (flg_firstTimestep)
    {
        flg_firstTimestep = 0;
        cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_A];
        pts_health = cfg_dennis->health.pts_healthMax;
        mod_camWant = DEN_CAMMOD_BINARY;
        tmr_nextLeap = -1.0f;
        stageCurr = DEN_STAGE_A;
    }

    psy_self->Timestep(dt, NULL);

    if (IsAlive())
    {
        ShadStufUpdate();
    }
    else if (simpShadow != NULL || entShadow != NULL)
    {
        ShadStufIAmDead();
    }

    if (IsAlive())
    {
        meter = &Dennis::HealthBar::mtr_health;
        if (stageCurr == DEN_STAGE_A)
        {
            meter->status.healthCurr[1] = pts_health;
        }
        else if (stageCurr == DEN_STAGE_B)
        {
            meter->status.healthCurr[2] = pts_health;
        }
        else if (stageCurr == DEN_STAGE_C)
        {
            meter->status.healthCurr[3] = pts_health;
        }

        flg_newtime = 1;
        flg_postproc = 1;
        flg_xtrarend = 0;
        flg_hudrend = 1;

        if (cnt_attacksTilEvade < 0)
        {
            tmr_nextLeap = -1.0f;
        }
        tmr_nextLeap = MAX(-1.0f, tmr_nextLeap - dt);

        BossCamSet(mod_camWant, false);
    }

    zNMECommon::Process(dt);
}

void zNMEDennis::BUpdate(xVec3* pos)
{
    Dennis::bnd_info.UpdateBound(&bound, this, pos);
}

void zNMEDennis::NewTime(F32 dt)
{
    if (npc_sbbat != NULL)
    {
        npc_sbbat->UpdateWrapper(dt);
    }
}

void zNMEDennis::PostProcess(F32 dt)
{
    cam_boss.set_targets(*xEntGetPos(xglobals->___player_ent_dont_use_directly), *xEntGetPos(this),
                         5.0f);
}

void zNMEDennis::RenderHud()
{
    zNMEDennis::MeterRender();
}

// Like retail (and the PS2 code) it ignores 'ent' and always measures to the player.
en_vis zNMEDennis::VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const
{
    NMECfgDennis* cfg;
    F32 dy;
    F32 rad_nearenuf;
    en_plyrpup pup;
    xVec3 dir;
    F32 dst;

    switch (stageCurr)
    {
    case DEN_STAGE_A:
    case DEN_STAGE_B:
    case DEN_STAGE_C:
        break;
    default:
        return VIS_NOBODY_AROUND;
    }

    if (Dennis::GlobalData::allowEvasion && tmr_nextLeap < 0.0f)
    {
        return VIS_OH_DEAR_GAHD;
    }

    if (NME::GlobalData::flg_wonder)
    {
        return VIS_NOBODY_AROUND;
    }

    cfg = cfg_dennis;
    XZVecToPos(&dir, xEntGetPos(xglobals->___player_ent_dont_use_directly), &dy);
    dst = xVec3Length(&dir);

    if (dst > cfg->ranges.rad_awaregrey)
    {
        return VIS_NOBODY_AROUND;
    }

    if (Dennis::GlobalData::useTooCloseEvasion && Dennis::GlobalData::allowEvasion)
    {
        rad_nearenuf = cfg->ranges.rad_tooclose[0];
        pup = zEntPlayer_PowerupLevel(PLYR_ATAK_PAT_THROW);
        if (pup > ePowerupLevelNormal)
        {
            rad_nearenuf = cfg->ranges.rad_tooclose[1];
        }
        if (dst < rad_nearenuf)
        {
            return VIS_OH_DEAR_GAHD;
        }
    }

    if (Dennis::GlobalData::lifeIsWar)
    {
        if (dst < cfg->ranges.rad_lob)
        {
            xVec3SMulBy(&dir, 1.0f / dst);
            return (en_vis)(xVec3Dot(&dir, (xVec3*)&model->Mat->at) < 0.96f);
        }

        if (dst < cfg->ranges.rad_lobgrey)
        {
            xVec3SMulBy(&dir, 1.0f / dst);
            if (xVec3Dot(&dir, (xVec3*)&model->Mat->at) < 0.96f)
            {
                return VIS_HORROR_FLIK;
            }
            return VIS_GET_BACK_HERE;
        }
    }

    if (dst < cfg->ranges.rad_aware)
    {
        return VIS_I_SEEE_YOUUU;
    }

    if (dst < cfg->ranges.rad_awaregrey)
    {
        return VIS_GREY_AND_FOGGY;
    }

    return VIS_NOBODY_AROUND;
}

// No GC symbol (always inlined into Process).
inline void zNMEDennis::BossCamSet(en_dbcmode mod_desired, bool forceCut)
{
    en_dbcmode currCamMode = mod_camCurr;

    if (currCamMode != mod_desired)
    {
        switch (currCamMode)
        {
        case DEN_CAMMOD_NORMAL:
            break;
        case DEN_CAMMOD_BINARY:
            cruise_bubble::Cam_Launch = NULL;
            cruise_bubble::Cam_Return = NULL;
            zCamRemove(cam_boss, false);
            break;
        }

        switch (mod_desired)
        {
        case DEN_CAMMOD_NORMAL:
            break;
        case DEN_CAMMOD_BINARY:
            cruise_bubble::Cam_Launch = cruise_bubble::xCamLaunch;
            cruise_bubble::Cam_Return = cruise_bubble::xCamReturn;
            zCamAdd(cam_boss, forceCut);
            break;
        }

        mod_camCurr = mod_desired;
    }
}

void zNMEDennis::MeterRender()
{
    Dennis::HealthBar::mtr_health.Render();
}

S32 zNMEDennis::TypeHandleMail(NMEMsg* mail)
{
    S32 handled;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        handled = DenMailDamage(&mail->dmgdata);
        break;
    case NME_MID_SYSEVENT:
        handled = DenMailSys(&mail->sysevent);
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

S32 zNMEDennis::DenMailDamage(NMEDamageInfo* dmgmail)
{
    S32 handled = 1;

    switch (dmgmail->dmg_type)
    {
    case DMGTYP_KILLEVENT:
    {
        xPsyche* psy = psy_self;
        xGoal* goal = psy->FindGoal(NME_GOAL_DENNIS_DEAD);
        ((zNMEGoalDenDead*)goal)->stylinDyin = DIE_MUFFLED;
        psy->GoalSet(NME_GOAL_DENNIS_DEAD, 0);
        cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_OUTRO];
        pts_health = cfg_dennis->health.pts_healthMax;
        mod_camWant = DEN_CAMMOD_NORMAL;
        tmr_nextLeap = -1.0f;
        stageCurr = DEN_STAGE_OUTRO;
        break;
    }
    case DMGTYP_FRIENDLYFIRE:
        break;
    case DMGTYP_ABOVE:
    case DMGTYP_BELOW:
    case DMGTYP_SIDE:
    case DMGTYP_INSTAKILL:
    case DMGTYP_WOUNDEVENT:
    case DMGTYP_NMEATTACK:
    case DMGTYP_HITBYTOSS:
    case DMGTYP_NMEKNOCKED:
    case DMGTYP_ROPE:
    case DMGTYP_CRUISEBUBBLE:
    case DMGTYP_REFLECTED:
    case DMGTYP_BOULDER:
    case DMGTYP_BUBBOWL:
    case DMGTYP_THUNDER_TIKI_EXPLOSION:
    case DMGTYP_DAMAGE_SURFACE:
    case DMGTYP_BUNGEED:
    case DMGTYP_SURFACE:
    case DMGTYP_CARTWHEEL:
    case DMGTYP_SLIDE:
    case DMGTYP_EXPLOSION:
    case DMGTYP_BBOWLZAP:
    {
        en_plyrpup pup;
        S32 amt_dmg = NMEC_DmgAmtForAttack(dmgmail->dmg_type, &pup);
        pts_health -= amt_dmg;
        if (pts_health < 0)
        {
            pts_health = 0;
        }

        if (pts_health < 1)
        {
            switch (stageCurr)
            {
            case DEN_STAGE_A:
                zEntEvent(this, this, eEventBossStageEndedA);
                zEntEvent(this, this, eEventBossStageEnded);
                break;
            case DEN_STAGE_B:
                zEntEvent(this, this, eEventBossStageEndedB);
                zEntEvent(this, this, eEventBossStageEnded);
                break;
            case DEN_STAGE_C:
                zEntEvent(this, this, eEventBossStageEndedC);
                zEntEvent(this, this, eEventBossStageEnded);
                break;
            }
        }

        if (pts_health < 1 && stageCurr == DEN_STAGE_C)
        {
            xGoal* goal = psy_self->FindGoal(NME_GOAL_DENNIS_DEAD);
            ((zNMEGoalDenDead*)goal)->stylinDyin = DIE_MUFFLED;
            psy_self->GoalSet(NME_GOAL_DENNIS_DEAD, 0);
            cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_OUTRO];
            pts_health = cfg_dennis->health.pts_healthMax;
            mod_camWant = DEN_CAMMOD_NORMAL;
            tmr_nextLeap = -1.0f;
            stageCurr = DEN_STAGE_OUTRO;
        }
        else
        {
            psy_self->GoalSet(NME_GOAL_DENNIS_DAMAGE, 0);
        }
        break;
    }
    default:
        handled = 0;
        break;
    }

    return handled;
}

S32 zNMEDennis::DenMailSys(NMESysEvent* sysmail)
{
    S32 handled = 1;
    S32 gotoStage;

    switch (sysmail->toEvent)
    {
    case eEventNMESetMovepointGroup:
        grp_mvpt = (xGroup*)sysmail->toParamWidget;
        if (grp_mvpt == NULL)
        {
            MvptFirstFollow();
        }
        break;
    case eEventBossStageSet:
        gotoStage = (S32)(sysmail->toParam[0] + 0.1f);
        if (gotoStage <= 0)
        {
            cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_INTRO];
            pts_health = cfg_dennis->health.pts_healthMax;
            mod_camWant = DEN_CAMMOD_NORMAL;
            tmr_nextLeap = -1.0f;
            stageCurr = DEN_STAGE_INTRO;
        }
        else if (gotoStage == 1)
        {
            cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_A];
            pts_health = cfg_dennis->health.pts_healthMax;
            mod_camWant = DEN_CAMMOD_BINARY;
            tmr_nextLeap = -1.0f;
            stageCurr = DEN_STAGE_A;
        }
        else if (gotoStage == 2)
        {
            cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_B];
            pts_health = cfg_dennis->health.pts_healthMax;
            mod_camWant = DEN_CAMMOD_BINARY;
            tmr_nextLeap = -1.0f;
            stageCurr = DEN_STAGE_B;
        }
        else if (gotoStage == 3)
        {
            cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_C];
            pts_health = cfg_dennis->health.pts_healthMax;
            mod_camWant = DEN_CAMMOD_BINARY;
            tmr_nextLeap = -1.0f;
            stageCurr = DEN_STAGE_C;
        }
        else if (gotoStage == 4)
        {
            cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_OUTRO];
            pts_health = cfg_dennis->health.pts_healthMax;
            mod_camWant = DEN_CAMMOD_NORMAL;
            tmr_nextLeap = -1.0f;
            stageCurr = DEN_STAGE_OUTRO;
        }
        else
        {
            cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_UNKNOWN];
            pts_health = cfg_dennis->health.pts_healthMax;
            mod_camWant = DEN_CAMMOD_NORMAL;
            tmr_nextLeap = -1.0f;
            stageCurr = DEN_STAGE_UNKNOWN;
        }
        break;
    case eEventBossStageBegan:
    case eEventBossStageEnded:
    case eEventBossStageBeganA:
    case eEventBossStageEndedA:
    case eEventBossStageBeganB:
    case eEventBossStageEndedB:
    case eEventBossStageBeganC:
    case eEventBossStageEndedC:
    default:
        handled = DfltSysEventMail(sysmail);
        break;
    }

    return handled;
}

zMovePoint* zNMEDennis::PickEvade(zMovePoint* nav_from)
{
    U32 num;
    xVec3 dir_plyr;
    F32 dst_retreat;
    F32 dst_charge;
    zMovePoint* nav_retreat;
    zMovePoint* nav_charge;
    U32 i;
    xBase* baseitem;
    zMovePoint* mvpt;
    zMovePoint* nav_goto;
    F32 dst;

    num = xGroupGetCount(grp_mvpt);
    if (num < 1)
    {
        return NULL;
    }

    dst = XZDstSqToPlayer(&dir_plyr, NULL);
    if (dst < 0.00001f)
    {
        dir_plyr = *(xVec3*)&model->Mat->at;
    }
    else
    {
        dst = xsqrt(dst);
        dir_plyr /= dst;
    }

    dst_retreat = -1.0f;
    dst_charge = -1.0f;
    nav_retreat = NULL;
    nav_charge = NULL;

    for (i = 0; i < num; i++)
    {
        xVec3 dir;

        baseitem = xGroupGetItemPtr(grp_mvpt, i);
        if (baseitem->baseType != eBaseTypeMovePoint || baseitem == nav_from)
        {
            continue;
        }

        mvpt = (zMovePoint*)baseitem;
        XZVecToPos(&dir, mvpt->pos, NULL);
        dst = xsqrt(xVec3Length2(&dir));
        if (dst < 1.0f)
        {
            continue;
        }

        dir /= dst;
        if (xVec3Dot(&dir, &dir_plyr) > 0.5f)
        {
            if (dst_charge < 0.0f || dst < dst_charge)
            {
                dst_charge = dst;
                nav_charge = mvpt;
            }
        }
        else
        {
            if (dst_retreat < 0.0f || dst < dst_retreat)
            {
                dst_retreat = dst;
                nav_retreat = mvpt;
            }
        }
    }

    if (dst_retreat > 0.0f)
    {
        nav_goto = nav_retreat;
    }
    else if (dst_charge > 0.0f)
    {
        nav_goto = nav_charge;
    }
    else
    {
        nav_goto = NULL;
    }

    return nav_goto;
}

void DennisLetMeKnow::Notice(en_psynote note, xGoal* goal, void* data)
{
}

S32 zNMEGoalDenNormal::Enter(F32 dt, void* updCtxt)
{
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalDenNormal::Exit(F32 dt, void* updCtxt)
{
    zNMEDennis* npc = (zNMEDennis*)psyche->clt_owner;

    switch (npc->stageCurr)
    {
    case DEN_STAGE_A:
        zEntEvent(npc, npc, eEventBossStageBegan);
        zEntEvent(npc, npc, eEventBossStageBeganA);
        break;
    case DEN_STAGE_B:
        zEntEvent(npc, npc, eEventBossStageBegan);
        zEntEvent(npc, npc, eEventBossStageBeganB);
        break;
    case DEN_STAGE_C:
        zEntEvent(npc, npc, eEventBossStageBegan);
        zEntEvent(npc, npc, eEventBossStageBeganC);
        break;
    }

    return 0;
}

// No GC symbol (inlined); the name is ours. The bat copies Dennis's goal animation.
static inline void DEN_BatMimicAnim(zNMEDennis* npc, xGoal* goal, en_npcgspot gspot)
{
    zNMESBBat* bat = npc->npc_sbbat;

    if (bat != NULL)
    {
        bat->MimicBossAnim(goal, gspot);
    }
}

S32 zNMEGoalDenNormal::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEDennis* npc = (zNMEDennis*)psyche->clt_owner;
    S32 nextgoal = 0;
    en_vis vis;

    vis = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_NORMAL, 0);
    if (vis <= VIS_I_SEEE_YOUUU)
    {
        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TRIGGER_ALERT;
        npc->GoalPick(this, nextgoal, *trantype);
        return nextgoal;
    }

    if (bul_entered || bul_resumed)
    {
        DoAutoAnim(NME_GSPOT_START, 0);
        DEN_BatMimicAnim(npc, this, NME_GSPOT_START);
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalDenBattle::Enter(F32 dt, void* updCtxt)
{
    tmr_tossDelay = -1.0f;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

// No GC symbol (always inlined); the name is ours. Taunt while the player is dead, or once the
// player's damage flash has run out away from Dennis.
static inline S32 DEN_PlayerTauntable(zNMEDennis* npc)
{
    if (globals.player.s->Health < 1)
    {
        return 1;
    }
    if (globals.player.DamageTimer < 0.25f)
    {
        return 0;
    }

    if (npc->XZDstSqToPlayer(NULL, NULL) < 100.0f)
    {
        return 0;
    }
    return 1;
}

S32 zNMEGoalDenBattle::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEDennis* npc = (zNMEDennis*)psyche->clt_owner;
    S32 nextgoal = 0;
    S32 doTran;

    doTran = ChkExitCases(npc, nextgoal, *trantype);
    if (doTran)
    {
        return nextgoal;
    }

    tmr_tossDelay = MAX(-1.0f, tmr_tossDelay - dt);
    npc->MatFacePlayer(dt, PI, 1);

    if (bul_entered || bul_resumed)
    {
        DoAutoAnim(NME_GSPOT_START, 0);
        DEN_BatMimicAnim(npc, this, NME_GSPOT_START);
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalDenBattle::ChkExitCases(zNMEDennis* npc, S32& nextgoal, en_trantype& trantype)
{
    en_vis vis;

    if (DEN_PlayerTauntable(npc))
    {
        if (globals.player.s->Health < 1)
        {
            trantype = GOAL_TRAN_SET;
        }
        else
        {
            trantype = GOAL_TRAN_PUSH;
        }
        nextgoal = NME_GOAL_TRIGGER_VINIVICIVIDI;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    vis = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_BATTLE, 0);
    if (vis == VIS_OH_DEAR_GAHD)
    {
        npc->nav_evade = npc->PickEvadePoint(npc->nav_evade);
        trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_DENNIS_EVADE;
        return 1;
    }

    if (vis != VIS_HORROR_FLIK && tmr_tossDelay < 0.0f)
    {
        tmr_tossDelay = npc->cfg_dennis->battle.tym_reload;
        trantype = GOAL_TRAN_PUSH;
        nextgoal = NME_GOAL_TRIGGER_ATTACK;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    return 0;
}

S32 zNMEGoalDenAttack::Enter(F32 dt, void* updCtxt)
{
    zNMEDennis* npc = (zNMEDennis*)psyche->clt_owner;
    NMECfgDennis* cfg = npc->cfg_dennis;

    npc->cnt_attacksTilEvade--;
    if (npc->cnt_attacksTilEvade < 0)
    {
        npc->cnt_attacksTilEvade = -1;
    }

    idx_shot = cfg->attack.num_salvo;
    pos_tgtBasis = *xEntGetCenter(xglobals->___player_ent_dont_use_directly);

    DoAutoAnim(NME_GSPOT_START, 0);
    DEN_BatMimicAnim(npc, this, NME_GSPOT_START);

    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalDenAttack::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEDennis* npc = (zNMEDennis*)psyche->clt_owner;

    F32 tym_animCurr = npc->AnimTimeCurrent();
    if (tym_animCurr + dt >= npc->AnimDuration(NULL))
    {
        *trantype = GOAL_TRAN_POP;
        return 1;
    }

    ThrowUpdate(npc);
    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

void zNMEGoalDenAttack::ThrowUpdate(zNMEDennis* npc)
{
    if (idx_shot < 1)
    {
        return;
    }
    if (npc->AnimTimeCurrent() < npc->cfg_dennis->attack.tym_shoot)
    {
        return;
    }
    if (GolfBomb(npc, npc->cfg_dennis->attack.num_salvo - idx_shot))
    {
        idx_shot--;
    }
}

// idx is unused (the PS2 demo has no such parameter).
S32 zNMEGoalDenAttack::GolfBomb(zNMEDennis* npc, S32 idx)
{
    NMECfgDennis* cfg = npc->cfg_dennis;
    zNMEHazard* haz;
    S32 rc;
    xVec3 pos_tgt;
    xVec3 pos_launch;

    haz = HAZ_Acquire();
    if (haz == NULL)
    {
        return 0;
    }

    rc = haz->ConfigHelper(HAZ_TYP_GOLFBALL);
    if (rc)
    {
        haz->npc_owner = npc;
    }
    else
    {
        haz->Discard();
        return 0;
    }

    pos_tgt = *xEntGetCenter(xglobals->___player_ent_dont_use_directly);
    haz->custdata.lobdata.pos_tgt = pos_tgt;

    pos_launch = ((xMat4x3*)&npc->model->Mat[30])->pos;
    pos_launch += Dennis::Attack::vec_boneOffset;
    xMat4x3Toworld(&pos_launch, (xMat4x3*)npc->model->Mat, &pos_launch);

    F32 tym = xsqrt(XZDstSq(&pos_launch, &pos_tgt, NULL, NULL)) / cfg->attack.spd_proj;
    tym = MAX(0.25f, tym);
    haz->Start(&pos_launch, tym);

    return 1;
}

// No symbol (always inlined into Enter); the name is ours, as zNMEGoalDentEvade::ParabSetup.
inline void zNMEGoalDenEvade::ParabSetup(zNMEDennis* npc)
{
    xVec3 vel;
    xVec3 dir = *npc->nav_evade->pos - *xEntGetPos(npc);

    vel = dir;
    vel *= 1.0f / 1.25f;
    vel.y += 20.0f * (0.5f * 1.25f);

    parab->initPos = *xEntGetPos(npc);
    parab->initVel = vel;
    parab->gravity = 20.0f;
    parab->minTime = 0.0f;
    parab->maxTime = 1.25f;
    tmr_leaping = 1.25f;
}

S32 zNMEGoalDenEvade::Enter(F32 dt, void* updCtxt)
{
    NMEHelpRib::RibData* ribhelp;
    zNMEDennis* npc = (zNMEDennis*)psyche->clt_owner;

    if (npc->nav_evade == NULL)
    {
        npc->nav_evade = npc->PickEvadePoint(NULL);
    }

    npc->flg_colCheck = 0;
    npc->flg_penCheck = 0;

    status = 0;
    DoAutoAnim(NME_GSPOT_START, 0);

    parab = NMEC_ParabReserve();
    if (parab != NULL)
    {
        ParabSetup(npc);
    }

    ribhelp = &Dennis::Evade::Ribbon::rib_normal;
    if (ribhelp->flags.enable || ribhelp->flags.dev_enable)
    {
        ribbon = zFXRibbonPoolNew();
        if (ribbon != NULL)
        {
            ribhelp->ConfigRibbon(ribbon);
            cnt_skipRibbonJoint = ribhelp->updinfo.frq_ribJoint;
        }
    }

    npc->moveBlur.activate(Dennis::MoveBlur::jumpBlur.parms.tym_lifespan,
                           Dennis::MoveBlur::jumpBlur.parms.alf_master,
                           Dennis::MoveBlur::jumpBlur.cfg_blur, npc->model);

    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalDenEvade::Exit(F32 dt, void* updCtxt)
{
    zNMEDennis* npc = (zNMEDennis*)psyche->clt_owner;
    NMECfgDennis* cfg;

    if (ribbon != NULL)
    {
        zFXRibbonPoolDelete(ribbon);
        ribbon = NULL;
    }

    if (parab != NULL)
    {
        NMEC_ParabRelease(parab);
    }
    parab = NULL;

    npc->RestoreColFlags();

    cfg = npc->cfg_dennis;
    npc->tmr_nextLeap = cfg->battle.tym_leapDelay;
    npc->cnt_attacksTilEvade = cfg->battle.num_atakPerLeap;

    if (npc->moveBlur.activity != NULL)
    {
        npc->moveBlur.set_lifetime(0.0f, Dennis::MoveBlur::jumpBlur.parms.tym_fade);
    }

    return 0;
}

S32 zNMEGoalDenEvade::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEDennis* npc = (zNMEDennis*)psyche->clt_owner;
    S32 animsComplete = UpdateAnim(npc, dt);
    S32 arrivedAtDest = 0;
    S32 itHasBegun = 0;
    xVec3 pos;
    S32 nextgoal;

    if (parab == NULL)
    {
        PathUpdate(npc, dt);
        tmr_leaping = MAX(-1.0f, tmr_leaping - dt);
    }
    else if (status == 0)
    {
        xParabolaEvalPos(parab, &pos, 1.0f);
        npc->MatTurnTowardPos(&pos, dt, 2.0f * PI, 1);

        if (npc->model->Anim->Single->Time >= 0.15f)
        {
            arrivedAtDest = PathUpdate(npc, dt);
            itHasBegun = 1;
        }
    }
    else if (status == 1)
    {
        arrivedAtDest = PathUpdate(npc, dt);
    }
    else if (status == 2)
    {
        arrivedAtDest = PathUpdate(npc, dt);
        npc->MatFacePlayer(dt, 4.0f * PI, 1);
    }

    if (status != 0 || itHasBegun)
    {
        tmr_leaping = MAX(-1.0f, tmr_leaping - dt);

        if (npc->itemType == NME_TYPE_DENNIS_V1)
        {
            if (npc->moveBlur.activity != NULL)
            {
                npc->moveBlur.move(npc->bound.sph.center);
            }
        }
        else
        {
            if (npc->moveBlur.activity != NULL)
            {
                npc->moveBlur.move(npc->bound.sph.center);
            }
            if (npc->moveBlurLong.activity != NULL)
            {
                npc->moveBlurLong.move(npc->bound.sph.center);
            }
        }
    }

    if (animsComplete && arrivedAtDest)
    {
        nextgoal = NME_GOAL_TRIGGER_BATTLE;
        *trantype = GOAL_TRAN_SET;
        npc->GoalPick(this, nextgoal, *trantype);
        return nextgoal;
    }

    if (status == 1)
    {
        RibbonLoop(npc, dt);
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// The compiled-out asserts (conditions unknown) keep this from being auto-inlined into Process,
// which calls it in retail (zNMEGoalDentEvade's copy is inlined).
S32 zNMEGoalDenEvade::UpdateAnim(zNMEDennis* npc, F32 dt)
{
    F32 tym_animPlusTime;
    S32 arrived;

    F32 tym_animCurr = npc->AnimTimeCurrent();
    F32 tym_animDur = npc->AnimDuration(NULL);
    tym_animPlusTime = tym_animCurr + dt;
    arrived = 0;

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    switch (status)
    {
    case 0:
        xASSERT_PLACEHOLDER();
        if (tym_animPlusTime >= tym_animDur)
        {
            DoAutoAnim(NME_GSPOT_LOOP, 0);
            DEN_BatMimicAnim(npc, this, NME_GSPOT_LOOP);
            status = 1;
            xASSERT_PLACEHOLDER();
        }
        break;
    case 1:
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        if (tmr_leaping <= 0.25f)
        {
            DoAutoAnim(NME_GSPOT_FINISH, 0);
            DEN_BatMimicAnim(npc, this, NME_GSPOT_FINISH);
            status = 2;
            xASSERT_PLACEHOLDER();
        }
        break;
    case 2:
        xASSERT_PLACEHOLDER();
        if (tym_animPlusTime >= tym_animDur)
        {
            arrived = 1;
        }
        break;
    }

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    return arrived;
}

S32 zNMEGoalDenEvade::PathUpdate(zNMEDennis* npc, F32 dt)
{
    if (parab)
    {
        return PathParabola(npc, dt);
    }
    return PathDirect(npc, dt);
}

// dt is unused, as in retail.
S32 zNMEGoalDenEvade::PathParabola(zNMEDennis* npc, F32 dt)
{
    S32 arrived;
    F32 tym_inLeap;
    xVec3 pos;
    xVec3 vel;
    xMat3x3 mat_rot;
    F32 mag;

    // The test goes through a 0/1 value, as in BfBB's robot goals.
    arrived = 0;
    if ((tmr_leaping < 0.0f) ? 1 : 0)
    {
        arrived = 1;
    }

    tym_inLeap = MAX(0.0f, tmr_leaping);
    tym_inLeap = 1.25f - tym_inLeap;
    tym_inLeap = CLAMP(tym_inLeap, 0.0f, 1.25f);

    xParabolaEvalPos(parab, &pos, tym_inLeap);
    xParabolaEvalVel(parab, &vel, tym_inLeap);

    npc->model->Mat->pos.x = pos.x;
    npc->model->Mat->pos.y = pos.y;
    npc->model->Mat->pos.z = pos.z;

    if (status != 2)
    {
        vel.y = 0.0f;
        mag = xVec3Length(&vel);
        if (mag > 0.00001f)
        {
            xVec3 dir = vel * (1.0f / -mag);
            xMat3x3LookVec(&mat_rot, &dir);
            *(xMat3x3*)npc->model->Mat = mat_rot;
        }
    }

    return arrived;
}

S32 zNMEGoalDenEvade::PathDirect(zNMEDennis* npc, F32 dt)
{
    return npc->MatMoveToPos(npc->nav_evade->pos, dt, 5.0f, 1, -1.0f);
}

void zNMEGoalDenEvade::RibbonLoop(zNMEDennis* npc, F32 dt)
{
    xFXRibbon* rib;
    NMEHelpRib::RibData* ribhelp;
    F32 scale;
    F32 alpha;

    if (ribbon == NULL)
    {
        return;
    }

    cnt_skipRibbonJoint--;
    if (cnt_skipRibbonJoint > 0)
    {
        return;
    }

    rib = ribbon;
    ribhelp = &Dennis::Evade::Ribbon::rib_normal;
    if (ribhelp->flags.dev_enable)
    {
        ribhelp->ConfigRibbon(rib);
    }

    alpha = ribhelp->updinfo.alf_master;
    scale = npc->bound.sph.r * ribhelp->updinfo.scl_master;

    // Emits from the last bone. Retail draws a random number here and discards it.
    xrand_RandomInt32();
    S32 idx = CLAMP(npc->model->BoneCount + 1, 1, npc->model->BoneCount);

    xMat4x3* mat = &xModelGetFrame(npc->model)[idx];
    xVec3 pos_emit = mat->pos + xModelGetFrame(npc->model)->pos;

    xVec3 dir_up = (xrand_RandomInt32() & 0x800000) ? mat->right : mat->up;
    if (xrand_RandomInt32() & 0x800000)
    {
        dir_up = mat->at;
    }

    rib->insert(pos_emit, dir_up, scale, alpha, 0);
}

S32 zNMEGoalDenEvade::GoalHandleMail(NMEMsg* mail)
{
    S32 snarfed = 1;
    zNMEDennis* npc;
    xPsyche* psy;
    en_plyrpup pup;
    S32 amt_dmg;
    xGoal* goal;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        npc = (zNMEDennis*)psyche->clt_owner;
        psy = npc->psy_self;

        amt_dmg = NMEC_DmgAmtForAttack(mail->dmgdata.dmg_type, &pup);
        npc->pts_health -= amt_dmg;
        if (npc->pts_health < 0)
        {
            npc->pts_health = 0;
        }

        if (npc->pts_health < 1)
        {
            switch (npc->stageCurr)
            {
            case DEN_STAGE_A:
                zEntEvent(npc, npc, eEventBossStageEndedA);
                zEntEvent(npc, npc, eEventBossStageEnded);
                break;
            case DEN_STAGE_B:
                zEntEvent(npc, npc, eEventBossStageEndedB);
                zEntEvent(npc, npc, eEventBossStageEnded);
                break;
            case DEN_STAGE_C:
                zEntEvent(npc, npc, eEventBossStageEndedC);
                zEntEvent(npc, npc, eEventBossStageEnded);
                break;
            }
        }

        if (npc->pts_health < 1 && npc->stageCurr == DEN_STAGE_C)
        {
            goal = psy->FindGoal(NME_GOAL_DENNIS_DEAD);
            ((zNMEGoalDenDead*)goal)->stylinDyin = DIE_MUFFLED;
            psy->GoalSet(NME_GOAL_DENNIS_DEAD, 0);
            npc->cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_OUTRO];
            npc->pts_health = npc->cfg_dennis->health.pts_healthMax;
            npc->mod_camWant = DEN_CAMMOD_NORMAL;
            npc->tmr_nextLeap = -1.0f;
            npc->stageCurr = DEN_STAGE_OUTRO;
        }
        break;
    default:
        snarfed = 0;
        break;
    }

    return snarfed;
}

S32 zNMEGoalDenTaunt::Enter(F32 dt, void* updCtxt)
{
    zNMEDennis* npc = (zNMEDennis*)psyche->clt_owner;

    DoAutoAnim(NME_GSPOT_START, 0);
    DEN_BatMimicAnim(npc, this, NME_GSPOT_START);

    npc->SndPlayFromTable(1); // eNMESound_Taunt

    if (npc->stageCurr == DEN_STAGE_A)
    {
        npc->oneLiners.PlaySound(0);
    }
    else
    {
        npc->oneLiners.PlaySound(1);
    }

    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalDenTaunt::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEDennis* npc = (zNMEDennis*)psyche->clt_owner;
    S32 nextgoal;

    if (!DEN_PlayerTauntable(npc))
    {
        if (psyche->IndexInStack(GetID()) == 0)
        {
            *trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_BATTLE;
            npc->GoalPick(this, nextgoal, *trantype);
            return nextgoal;
        }

        *trantype = GOAL_TRAN_POP;
        return 1;
    }

    npc->MatFacePlayer(dt, PI, 1);

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalDenDamage::Enter(F32 dt, void* updCtxt)
{
    zNMEDennis* npc = (zNMEDennis*)psyche->clt_owner;

    DoAutoAnim(NME_GSPOT_START, 0);
    DEN_BatMimicAnim(npc, this, NME_GSPOT_START);
    npc->oneLiners.PlaySound(2);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalDenDamage::Exit(F32 dt, void* ctxt)
{
    zNMEDennis& npc = *(zNMEDennis*)this->psyche->clt_owner;
    npc.tmr_nextLeap = -1.0f;
    return 0;
}

S32 zNMEGoalDenDamage::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEDennis* npc = (zNMEDennis*)psyche->clt_owner;
    S32 nextgoal = 0;
    S32 doTran;

    doTran = ChkExistence(npc, dt, nextgoal, *trantype);
    if (doTran)
    {
        return nextgoal;
    }

    npc->MatFacePlayer(dt, PI, 1);

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalDenDamage::ChkExistence(zNMEDennis* npc, F32 dt, S32& nextgoal, en_trantype& trantype)
{
    F32 tym_animCurr = npc->AnimTimeCurrent();
    if (tym_animCurr + dt >= npc->AnimDuration(NULL))
    {
        if (npc->pts_health < 1)
        {
            if (npc->stageCurr == DEN_STAGE_A)
            {
                npc->cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_B];
                npc->pts_health = npc->cfg_dennis->health.pts_healthMax;
                npc->mod_camWant = DEN_CAMMOD_BINARY;
                npc->tmr_nextLeap = -1.0f;
                npc->stageCurr = DEN_STAGE_B;
            }
            else if (npc->stageCurr == DEN_STAGE_B)
            {
                npc->cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_C];
                npc->pts_health = npc->cfg_dennis->health.pts_healthMax;
                npc->mod_camWant = DEN_CAMMOD_BINARY;
                npc->tmr_nextLeap = -1.0f;
                npc->stageCurr = DEN_STAGE_C;
            }
            else if (npc->stageCurr == DEN_STAGE_C)
            {
                npc->cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_OUTRO];
                npc->pts_health = npc->cfg_dennis->health.pts_healthMax;
                npc->mod_camWant = DEN_CAMMOD_NORMAL;
                npc->tmr_nextLeap = -1.0f;
                npc->stageCurr = DEN_STAGE_OUTRO;
            }

            trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_NORMAL;
            npc->GoalPick(this, nextgoal, trantype);
            return 1;
        }

        if (psyche->IndexInStack(GetID()) == 0)
        {
            trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_TRIGGER_BATTLE;
            npc->GoalPick(this, nextgoal, trantype);
            return 1;
        }

        trantype = GOAL_TRAN_POP;
        nextgoal = 1;
        return 1;
    }

    return 0;
}

S32 zNMEGoalDenDamage::GoalHandleMail(NMEMsg* mail)
{
    S32 snarfed = 1;
    zNMEDennis* npc;
    NMEDamageInfo* dmgmail;
    xPsyche* psy;
    en_plyrpup pup;
    S32 amt_dmg;
    xGoal* goal;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        npc = (zNMEDennis*)psyche->clt_owner;
        dmgmail = &mail->dmgdata;
        psy = npc->psy_self;

        if (dmgmail->dmg_type != DMGTYP_REFLECTED)
        {
            break;
        }

        amt_dmg = NMEC_DmgAmtForAttack(dmgmail->dmg_type, &pup);
        npc->pts_health -= amt_dmg;
        if (npc->pts_health < 0)
        {
            npc->pts_health = 0;
        }

        if (npc->pts_health < 1)
        {
            switch (npc->stageCurr)
            {
            case DEN_STAGE_A:
                zEntEvent(npc, npc, eEventBossStageEndedA);
                zEntEvent(npc, npc, eEventBossStageEnded);
                break;
            case DEN_STAGE_B:
                zEntEvent(npc, npc, eEventBossStageEndedB);
                zEntEvent(npc, npc, eEventBossStageEnded);
                break;
            case DEN_STAGE_C:
                zEntEvent(npc, npc, eEventBossStageEndedC);
                zEntEvent(npc, npc, eEventBossStageEnded);
                break;
            }
        }

        if (npc->pts_health < 1 && npc->stageCurr == DEN_STAGE_C)
        {
            goal = psy->FindGoal(NME_GOAL_DENNIS_DEAD);
            ((zNMEGoalDenDead*)goal)->stylinDyin = DIE_MUFFLED;
            psy->GoalSet(NME_GOAL_DENNIS_DEAD, 0);
            npc->cfg_dennis = Dennis::Config::cfg_junkStages[DEN_STAGE_OUTRO];
            npc->pts_health = npc->cfg_dennis->health.pts_healthMax;
            npc->mod_camWant = DEN_CAMMOD_NORMAL;
            npc->tmr_nextLeap = -1.0f;
            npc->stageCurr = DEN_STAGE_OUTRO;
        }
        break;
    default:
        snarfed = 0;
        break;
    }

    return snarfed;
}

S32 zNMEGoalDenDead::Enter(F32 dt, void* ctxt)
{
    if (((zNMEDennis*)psyche->clt_owner)->npc_sbbat != NULL)
    {
        ((zNMEDennis*)psyche->clt_owner)->npc_sbbat->psy_self->GoalSet(NME_GOAL_SBBAT_DEAD, 0);
    }

    return zNMEGoalDead::Enter(dt, ctxt);
}

S32 zNMEGoalDenDead::Exit(F32 dt, void* ctxt)
{
    if (((zNMEDennis*)psyche->clt_owner)->npc_sbbat != NULL)
    {
        ((zNMEDennis*)psyche->clt_owner)->npc_sbbat->psy_self->GoalSet(NME_GOAL_SBBAT_IDLE, 0);
    }

    return zNMEGoalDead::Exit(dt, ctxt);
}
