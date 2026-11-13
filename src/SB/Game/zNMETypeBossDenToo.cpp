#include "zNMETypeBossDenToo.h"
#include "zEntCruiseBubble.h"
#include "zCam.h"
#include "xDebug.h"

U32 g_hash_dentanim[31];
char* g_strz_dentanim[31] = {
    "Unknown",   "Idle01",     "Idle02",     "Idle03",     "Idle04",     "Move01",   "LeapPrep",
    "LeapDone",  "LeapLoop01", "LeapLoop02", "LeapLoop03", "LeapLoop04", "Attack01", "Attack02",
    "Attack03",  "Attack04",   "Taunt01",    "Taunt02",    "Taunt03",    "Taunt04",  "Victory01",
    "Victory02", "Victory03",  "Victory04",  "Damage01",   "Damage02",   "Damage03", "Damage04",
    "Defeat01",  "Defeat02",   "Defeat03",
};

// NME::GlobalData is defined (static) in zNMECommon.cpp, which comes later in zWadNME.cpp.
namespace NME
{
    namespace GlobalData
    {
        extern S32 flg_wonder;
    } // namespace GlobalData
} // namespace NME

namespace DennisToo
{
    namespace GlobalData
    {
        static S32 allowEvasion = 1;
        static S32 lifeIsWar = 1;
    } // namespace GlobalData
} // namespace DennisToo

namespace DennisToo
{
    namespace Config
    {
        NMECfgCommon cfg_common_Dennis = {
            { -1.0f },
            { 10.0f, -5.0f },
            { 1.0f, 3.0f, 0.1f, 2.0f * PI },
        };

        NMECfgDenToo cfg_hoffride_s1 = {
            { 12 },
            { 9.0f, 9980.0f, 9985.0f, 9990.0f, 9995.0f },
            {},
            { 25.0f, 4, 5.25f },
            { 25.0f, 5, 5.5f, 25.0f, 0.0f },
            { 3.0f, 16.0f, 3 },
        };

        NMECfgDenToo cfg_hoffride_s2 = {
            { 12 },
            { 9.0f, 9980.0f, 9985.0f, 9990.0f, 9995.0f },
            {},
            { 25.0f, 5, 6.75f },
            { 25.0f, 7, 5.5f, 25.0f, 0.0f },
            { 2.0f, 12.0f, 2 },
        };

        NMECfgDenToo cfg_hoffride_s3 = {
            { 12 },
            { 9.0f, 9980.0f, 9985.0f, 9990.0f, 9995.0f },
            {},
            { 25.0f, 6, 8.0f },
            { 25.0f, 11, 5.5f, 25.0f, 0.0f },
            { 1.5f, 8.0f, 1 },
        };

        // Indexed by en_dentstage.
        NMECfgDenToo* cfg_hoffStages[DENT_STAGE_NOMORE] = {
            &cfg_hoffride_s1, &cfg_hoffride_s1, &cfg_hoffride_s1,
            &cfg_hoffride_s2, &cfg_hoffride_s3, &cfg_hoffride_s1,
        };
    } // namespace Config

    namespace Camera
    {
        static const zBossCam_Binary::config cfg_bossCam = {
            { 6.0f, 4.0f, 1.25f },
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

    namespace CamRepel
    {
        static const xVec3 pos_center = { 0.0f, 4.0f, 30.0f };
    } // namespace CamRepel

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

        PopGasEmitter gasSplash = {
            NPAR_TYP_POPGAS,
            2.0f,
            { 0x00, 0xC8, 0xFF, 0x96 },
            { 0.25f, 0.3f },
            { 0.0f, -50.0f, 0.0f },
            0.7853982f,
            82,
            0,
            50,
            20,
            { 0.5f, 25.0f, 0.45f, 2.5f, 12.0f },
        };
    } // namespace Evade

    namespace Attack
    {
        static const xVec3 vec_boneOffset = { 0.0f, 0.0f, 0.0f };
    } // namespace Attack

    namespace HealthBar
    {
        NMEHealthMeter::MeterData mtr_health = {
            { 1, 0, { 0, 0 } },
            { { -1, 12, 12, 12 }, "particle_cloud", 0.14f, 0.25f },
            { { -1, 12, 12, 12 } },
        };
    } // namespace HealthBar

    namespace BlurFX
    {
        BlurData jumpBlurLG = {
            { 1, 0, 0, 0 },
            { 0.3f, 1.0f, 0.5f },
            { 0, 2.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0.15f, 100.0f, 16 },
        };

        BlurData jumpBlurSM = {
            { 1, 0, 0, 0 },
            { 0.1f, 1.0f, 0.5f },
            { 0, 2.0f, 1.0f, 1.0f, 1.0f, 0.2f, 0.15f, 100.0f, 16 },
        };
    } // namespace BlurFX

    NMEHelpBound::BndData bnd_info = {
        k_XBOUNDTYPE_BOX,
        { 0, 0, { 0, 0 } },
        { 1.5f, { 0.0f, 3.0f, 0.0f } },
        { { 1.0f, 3.0f, 1.0f }, { 0.0f, 1.5f, 0.0f } },
    };

    namespace OneLiners
    {
        zStreamedSound vo_data[3] = {
            { "ONEL_BD2_BOSS_HIT_PLAYER", 10.0f, 0x1080, 0.0f, (iSndGroupHandle)-1 },
            { "ONEL_BD2_HIT_BOSS", 10.0f, 0x2580, 0.0f, (iSndGroupHandle)-1 },
            { NULL, 0.0f, 0, 0.0f, (iSndGroupHandle)-1 },
        };
    } // namespace OneLiners
} // namespace DennisToo

namespace Swimmer
{
    namespace World
    {
        const xVec3 vel_wind = { 0.0f, 0.0f, -30.0f };
    } // namespace World

    namespace SeaFog
    {
        const xVec3 pos_emitLeft[2] = { { 7.0f, 7.0f, 100.0f }, { 65.0f, 9.0f, 25.0f } };
        const xVec3 pos_emitRight[2] = { { -8.0f, 7.0f, 100.0f }, { -65.0f, 9.0f, 25.0f } };

        ExhaustCurve curve_data[3] = {
            { 0.0f, { 0xFF, 0xFF, 0xFF, 0x00 }, 0.0f },
            { 0.25f, { 0xFF, 0xFF, 0xFF, 0xFF }, 1.0f },
            { 1.0f, { 0xFF, 0xFF, 0xFF, 0x00 }, 0.5f },
        };

        ExhaustEmitter sea_fog = {
            NPAR_TYP_SEAFOG_NOROT,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 2.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 4.0f, 0.0f },
            { 1, 0, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, { 0.0f, 0.0f, -30.0f } },
            { curve_data, 3, 0.4f, 20.0f },
        };
    } // namespace SeaFog

    namespace SeaMist
    {
        const xVec3 pos_emitLeft[2] = { { 6.0f, 7.0f, 20.0f }, { 6.0f, 2.0f, -20.0f } };
        const xVec3 pos_emitRight[2] = { { -6.0f, 7.0f, 20.0f }, { -6.0f, 2.0f, -20.0f } };

        SparkyCurve curve_data[2] = {
            { 0.0f, { 0x1E, 0x50, 0x87, 0xFF }, 1.0f },
            { 1.0f, { 0x1E, 0x50, 0x87, 0x00 }, 0.5f },
        };

        SparkyEmitter sea_mist = {
            NPAR_TYP_SEAMIST,
            { 0, 0, { 0, 0 } },
            { 9.0f, 0.0f },
            { 0.15f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.35f, 0.5f },
            { 1, 3, 10.0f, -10.0f },
            { 10.0f, -1.0f, 10.0f, 5.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 2, 1.0f, 4.0f },
        };
    } // namespace SeaMist

    // The body sprays below have no code in the GC build (zMODSwimmer::Timestep only runs the sea
    // fog and mist), but their data is still in retail's .rodata/.data, in the PS2 DWARF order.
    namespace NeckSpray
    {
        const xVec3 pos_emitLeft[2] = { { 3.0f, 4.0f, 19.0f }, { 14.0f, 3.0f, 14.0f } };
        const xVec3 pos_emitRight[2] = { { -3.0f, 4.0f, 19.0f }, { -14.0f, 3.0f, 14.0f } };

        SparkyCurve curve_data[2] = {
            { 0.0f, { 0x1E, 0x50, 0x87, 0xFF }, 1.0f },
            { 1.0f, { 0x1E, 0x50, 0x87, 0x00 }, 0.5f },
        };

        SparkyEmitter spray_neck = {
            NPAR_TYP_SEAMIST,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f },
            { 0.2f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.5f, 0.5f },
            { 4, 0, 0.5f, 0.5f },
            { 10.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, -30.0f } },
            { curve_data, 2, 0.5f, 2.0f },
        };
    } // namespace NeckSpray

    namespace ArmSpray
    {
        const xVec3 pos_emitLeft[2] = { { 12.0f, 2.0f, 14.0f }, { 14.0f, 2.0f, -8.0f } };
        const xVec3 pos_emitRight[2] = { { -12.0f, 2.0f, 14.0f }, { -14.0f, 2.0f, -8.0f } };

        ExhaustCurve curve_data[2] = {
            { 0.0f, { 0x1E, 0x50, 0x87, 0xFF }, 1.0f },
            { 1.0f, { 0x1E, 0x50, 0x87, 0x00 }, 0.5f },
        };

        ExhaustEmitter spray_arm = {
            NPAR_TYP_SEASPRAY,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 1.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.35f, 0.4f },
            { 5, 0, 0.0f, 0.0f },
            { 7.5f, 4.0f, 0.0f, 0.0f, 0.0f, 0.0f, { 0.0f, -10.0f, -10.0f } },
            { curve_data, 2, 1.0f, 1.0f },
        };
    } // namespace ArmSpray

    namespace PitSpray
    {
        const xVec3 pos_emitLeft[2] = { { 9.5f, 2.0f, 6.0f }, { 8.0f, 2.0f, 1.0f } };
        const xVec3 pos_emitRight[2] = { { -3.0f, 2.0f, 19.0f }, { -8.0f, 2.0f, 1.0f } };

        ExhaustCurve curve_data[2] = {
            { 0.0f, { 0x1E, 0x50, 0x87, 0xFF }, 1.0f },
            { 1.0f, { 0x1E, 0x50, 0x87, 0x00 }, 0.5f },
        };

        ExhaustEmitter spray_pit = {
            NPAR_TYP_SEASPRAY,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 1.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.35f, 0.4f },
            { 5, 0, 0.0f, 0.0f },
            { 3.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, { 0.0f, -10.0f, -10.0f } },
            { curve_data, 2, 1.0f, 1.0f },
        };
    } // namespace PitSpray

    namespace HipSpray
    {
        const xVec3 pos_emitLeft[2] = { { 9.5f, 2.0f, 6.0f }, { 8.0f, 2.0f, 1.0f } };
        const xVec3 pos_emitRight[2] = { { -3.0f, 2.0f, 19.0f }, { -8.0f, 2.0f, 1.0f } };

        ExhaustCurve curve_data[2] = {
            { 0.0f, { 0x1E, 0x50, 0x87, 0xFF }, 1.0f },
            { 1.0f, { 0x1E, 0x50, 0x87, 0x00 }, 0.5f },
        };

        ExhaustEmitter spray_hip = {
            NPAR_TYP_SEASPRAY,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 1.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.35f, 0.4f },
            { 5, 0, 0.0f, 0.0f },
            { 3.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, { 0.0f, -10.0f, -10.0f } },
            { curve_data, 2, 1.0f, 1.0f },
        };
    } // namespace HipSpray

    namespace KneeSpray
    {
        const xVec3 pos_emitLeft[2] = { { 9.5f, 2.0f, 6.0f }, { 8.0f, 2.0f, 1.0f } };
        const xVec3 pos_emitRight[2] = { { -3.0f, 2.0f, 19.0f }, { -8.0f, 2.0f, 1.0f } };

        ExhaustCurve curve_data[2] = {
            { 0.0f, { 0x1E, 0x50, 0x87, 0xFF }, 1.0f },
            { 1.0f, { 0x1E, 0x50, 0x87, 0x00 }, 0.5f },
        };

        ExhaustEmitter spray_knee = {
            NPAR_TYP_SEASPRAY,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 1.0f },
            { 1.0f, 1.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.35f, 0.4f },
            { 1, 0, 0.0f, 0.0f },
            { 3.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, { 0.0f, -10.0f, -10.0f } },
            { curve_data, 2, 1.0f, 1.0f },
        };
    } // namespace KneeSpray

    namespace FeetSpray
    {
        const xVec3 pos_emit = { 0.0f, 5.0f, -50.0f };
        const xVec3 dir_emit = { 0.0f, 0.0f, -1.0f };

        ExhaustCurve curve_data[2] = {
            { 0.0f, { 0x1E, 0x50, 0x87, 0xFF }, 1.0f },
            { 1.0f, { 0x1E, 0x50, 0x87, 0x00 }, 0.5f },
        };

        ExhaustEmitter spray_feet = {
            NPAR_TYP_SEASPRAY,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f, 4.0f },
            { 2.0f, 2.0f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 2.0f, 0.5f },
            { 1, 0, 7.0f, -7.0f },
            { 10.0f, 1.0f, 5.0f, 10.0f, 6.0f, -2.2f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 2, 1.0f, 1.0f },
        };
    } // namespace FeetSpray
} // namespace Swimmer

void zNMEDenToo::CreateConfig(const char* name)
{
    zNMECommon::CreateConfig(name);
    cfg_common = &DennisToo::Config::cfg_common_Dennis;
    cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_UNKNOWN];
}

DenTooMeter::DenTooMeter()
{
}

void DenTooMeter::setup()
{
}

void DenTooMeter::update(F32 dt)
{
}

S32 zNMEDenToo::DfltVulnFlags()
{
    return 0xc07b0007;
}

void zNMEDenToo::SelfSetup()
{
    xBehaveMgr* bmgr;
    xPsyche* psy;

    bmgr = xBehaveMgr_GetSelf();
    psy_self = bmgr->Subscribe(this, 0);

    psy = psy_self;
    psy->BrainBegin();

    psy->AddGoal(NME_GOAL_DENTOO_NORMAL, NULL);
    psy->AddGoal(NME_GOAL_DENTOO_BATTLE, NULL);
    psy->AddGoal(NME_GOAL_DENTOO_EVADE, NULL);
    psy->AddGoal(NME_GOAL_DENTOO_ATTACK, NULL);
    psy->AddGoal(NME_GOAL_DENTOO_TAUNT, NULL);
    psy->AddGoal(NME_GOAL_DENTOO_DAMAGE, NULL);
    psy->AddGoal(NME_GOAL_DENTOO_DEAD, NULL);

    psy->BrainEnd();
    psy->SetSafety(NME_GOAL_DENTOO_NORMAL);
}

zMovePoint* zNMEDenToo::MvptFirstFollow()
{
    zMovePoint* nav;

    if (((zNMEAssetDenToo*)nmeass)->aid_mvptFollow == 0)
    {
        return NULL;
    }

    nav = zMovePoint_From_xAssetID(((zNMEAssetDenToo*)nmeass)->aid_mvptFollow);
    return nav;
}

// No GC symbol (always inlined into AnimPick); the name is ours.
static inline S32 LeapCrossesArena(zNMEDenToo* npc, zMovePoint* nav)
{
    if (nav == NULL)
    {
        return 0;
    }

    xVec3 pos_nav = *nav->pos;
    xVec3 pos_boss = *(xVec3*)&npc->model->Mat->pos;
    return pos_nav.z * pos_boss.z < 0.0f;
}

U32 zNMEDenToo::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
{
    S32 idx = 0;
    S32 rnd = xurand() * 3.99f;

    switch (gid)
    {
    case NME_GOAL_DENTOO_DEAD:
    case NME_GOAL_DEV_ANIMVIEW:
        idx = 1;
        break;
    case NME_GOAL_DENTOO_NORMAL:
    case NME_GOAL_DENTOO_BATTLE:
        idx = rnd + 1;
        break;
    case NME_GOAL_DENTOO_EVADE:
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
            if (nav_evade == NULL)
            {
                idx = rnd + 8;
            }
            else
            {
                S32 across = LeapCrossesArena(this, nav_evade);
                rnd = xurand() * 1.99f;
                if (across)
                {
                    idx = rnd + 10;
                }
                else
                {
                    idx = rnd + 8;
                }
            }
        }
        break;
    case NME_GOAL_DENTOO_ATTACK:
        idx = rnd + 12;
        break;
    case NME_GOAL_DENTOO_TAUNT:
        if (globals.player.s->Health < 1)
        {
            idx = rnd + 20;
        }
        else
        {
            idx = rnd + 16;
        }
        break;
    case NME_GOAL_DENTOO_DAMAGE:
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

    return g_hash_dentanim[idx];
}

S32 zNMEDenToo::GoalPick(xGoal* goal, S32& gid_would, en_trantype& trantype)
{
    S32 altered = 1;

    switch (gid_would)
    {
    case NME_GOAL_TRIGGER_NORMAL:
        gid_would = NME_GOAL_DENTOO_NORMAL;
        break;
    case NME_GOAL_TRIGGER_ALERT:
    case NME_GOAL_TRIGGER_BATTLE:
        gid_would = NME_GOAL_DENTOO_BATTLE;
        break;
    case NME_GOAL_TRIGGER_ATTACK:
        gid_would = NME_GOAL_DENTOO_ATTACK;
        break;
    case NME_GOAL_TRIGGER_VINIVICIVIDI:
        gid_would = NME_GOAL_DENTOO_TAUNT;
        break;
    case NME_GOAL_TRIGGER_WOUND:
        gid_would = NME_GOAL_DENTOO_DAMAGE;
        break;
    default:
        altered = 0;
        break;
    }

    return altered;
}

xAnimTable* zNMEDenToo::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMEDenToo", 0);
    }

    xAnimTableNewState(table, g_strz_dentanim[1], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[2], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[3], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[4], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[5], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_dentanim[16], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[17], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[18], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[19], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_dentanim[20], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[21], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[22], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[23], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_dentanim[24], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[25], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[26], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[27], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_dentanim[28], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[29], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[30], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_dentanim[12], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[13], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[14], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[15], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_dentanim[6], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[7], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    xAnimTableNewState(table, g_strz_dentanim[8], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[9], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[10], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_dentanim[11], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    return table;
}

void zNMEDenToo::Init(xEntAsset* asset)
{
    zNMECommon::Init(asset);

    cam_boss.cfg = DennisToo::Camera::cfg_bossCam;
    cam_boss.create();
    cam_boss.cfg_common.priority = 5;
    cam_boss.cfg_common.blend_time = 1.0f;
    cam_boss.fun_moveover = BossCamOverride;
    cam_boss.ctxt_moveover = this;
    cruise_bubble::xCamLaunchCam = &cam_boss;

    oneLiners.InitWithSounds(DennisToo::OneLiners::vo_data, 2);

    blur_jumpShort.create();
    blur_jumpLong.create();

    zNMEParFX_PartyInvite(NPAR_TYP_POPGAS, NULL, 0);
}

// No symbol (always inlined into Setup; the name is ours): sets up the health meter's rasters,
// fade times and stage health.
static inline void DenTooMeterSetup()
{
    NMEHealthMeter::MeterData* meter = &DennisToo::HealthBar::mtr_health;
    NMECfgDenToo* cfg_a;
    NMECfgDenToo* cfg_b;
    NMECfgDenToo* cfg_c;

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

    cfg_a = DennisToo::Config::cfg_hoffStages[DENT_STAGE_A];
    cfg_b = DennisToo::Config::cfg_hoffStages[DENT_STAGE_B];
    cfg_c = DennisToo::Config::cfg_hoffStages[DENT_STAGE_C];

    meter->cfginfo.stageHealth[1] = cfg_a->health.pts_healthMax;
    meter->cfginfo.stageHealth[2] = cfg_b->health.pts_healthMax;
    meter->cfginfo.stageHealth[3] = cfg_c->health.pts_healthMax;
    meter->status.healthCurr[1] = cfg_a->health.pts_healthMax;
    meter->status.healthCurr[2] = cfg_b->health.pts_healthMax;
    meter->status.healthCurr[3] = cfg_c->health.pts_healthMax;
}

void zNMEDenToo::Setup()
{
    zNMECommon::Setup();

    DennisToo::Evade::Ribbon::rib_normal.FindTexture();

    cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_UNKNOWN];
    pts_health = cfg_dentoo->health.pts_healthMax;
    mod_camWant = DENT_CAMMOD_NORMAL;
    tmr_nextLeap = -1.0f;
    stageCurr = DENT_STAGE_UNKNOWN;

    cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_INTRO];
    pts_health = cfg_dentoo->health.pts_healthMax;
    mod_camWant = DENT_CAMMOD_NORMAL;
    tmr_nextLeap = -1.0f;
    stageCurr = DENT_STAGE_INTRO;

    psy_self->GoalSet(NME_GOAL_DENTOO_NORMAL, 0);

    DenTooMeterSetup();
}

void zNMEDenToo::Reset()
{
    xPsyche* psy;

    zNMECommon::Reset();

    xBase* b = NULL;
    if (((zNMEAssetDenToo*)nmeass)->aid_mvptGroup != 0)
    {
        b = zSceneFindObject(((zNMEAssetDenToo*)nmeass)->aid_mvptGroup);
    }
    grp_mvpt = (xGroup*)b;
    if (grp_mvpt == NULL)
    {
        // The result is not used (as in zNMEPopper::Reset).
        MvptFirstFollow();
    }
    nav_evade = PickEvadePoint(NULL);

    psy = psy_self;

    pts_health = cfg_dentoo->health.pts_healthMax;
    flg_vuln = DfltVulnFlags();

    mod_camCurr = DENT_CAMMOD_UNKNOWN;
    cam_boss.attach(xglobals->___player_ent_dont_use_directly);
    cam_boss.cfg_common.priority = 5;
    cam_boss.cfg_common.blend_time = 1.0f;
    cruise_bubble::Cam_Launch = NULL;
    cruise_bubble::Cam_Return = NULL;

    DennisToo::HealthBar::mtr_health.status.healthCurr[1] =
        DennisToo::Config::cfg_hoffStages[DENT_STAGE_A]->health.pts_healthMax;
    DennisToo::HealthBar::mtr_health.status.healthCurr[2] =
        DennisToo::Config::cfg_hoffStages[DENT_STAGE_B]->health.pts_healthMax;
    DennisToo::HealthBar::mtr_health.status.healthCurr[3] =
        DennisToo::Config::cfg_hoffStages[DENT_STAGE_C]->health.pts_healthMax;

    flg_firstTimestep = 1;

    cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_UNKNOWN];
    pts_health = cfg_dentoo->health.pts_healthMax;
    mod_camWant = DENT_CAMMOD_NORMAL;
    tmr_nextLeap = -1.0f;
    stageCurr = DENT_STAGE_UNKNOWN;

    psy->GoalSet(NME_GOAL_DENTOO_NORMAL, 0);

    tmr_nextLeap = -1.0f;
    cnt_attacksTilEvade = -1;
}

void zNMEDenToo::Destroy()
{
    cruise_bubble::Cam_Launch = NULL;
    cruise_bubble::Cam_Return = NULL;
    cruise_bubble::xCamLaunchCam = &cam_boss;
    cruise_bubble::xCamLaunchCam->destroy();

    zNMEBoss::Destroy();
}

S32 zNMEDenToo::IsAlive()
{
    if (stageCurr == DENT_STAGE_A)
    {
        return 1;
    }
    if (stageCurr == DENT_STAGE_B)
    {
        return 1;
    }
    if (stageCurr == DENT_STAGE_C)
    {
        return 1;
    }
    return 0;
}

void zNMEDenToo::Process(F32 dt)
{
    NMEHealthMeter::MeterData* meter;

    if (flg_firstTimestep)
    {
        flg_firstTimestep = 0;
        cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_A];
        pts_health = cfg_dentoo->health.pts_healthMax;
        mod_camWant = DENT_CAMMOD_BINARY;
        tmr_nextLeap = -1.0f;
        stageCurr = DENT_STAGE_A;
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
        meter = &DennisToo::HealthBar::mtr_health;
        if (stageCurr == DENT_STAGE_A)
        {
            meter->status.healthCurr[1] = pts_health;
        }
        else if (stageCurr == DENT_STAGE_B)
        {
            meter->status.healthCurr[2] = pts_health;
        }
        else if (stageCurr == DENT_STAGE_C)
        {
            meter->status.healthCurr[3] = pts_health;
        }

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

void zNMEDenToo::PostProcess(F32 dt)
{
    cam_boss.set_targets(*xEntGetPos(xglobals->___player_ent_dont_use_directly), *xEntGetPos(this),
                         5.0f);
}

void zNMEDenToo::BUpdate(xVec3* pos)
{
    DennisToo::bnd_info.UpdateBound(&bound, this, pos);
}

void zNMEDenToo::RenderHud()
{
    MeterRender();
}

en_vis zNMEDenToo::VisionTarget(xEnt* ent, en_defcon defcon, S32 flags) const
{
    NMECfgDenToo* cfg;
    F32 dy;
    xVec3 dir;
    F32 dst;

    switch (stageCurr)
    {
    case DENT_STAGE_A:
    case DENT_STAGE_B:
    case DENT_STAGE_C:
        break;
    default:
        return VIS_NOBODY_AROUND;
    }

    if (DennisToo::GlobalData::allowEvasion && tmr_nextLeap < 0.0f)
    {
        return VIS_OH_DEAR_GAHD;
    }

    if (NME::GlobalData::flg_wonder)
    {
        return VIS_NOBODY_AROUND;
    }

    cfg = cfg_dentoo;
    XZVecToPos(&dir, xEntGetPos(xglobals->___player_ent_dont_use_directly), &dy);
    dst = dir.length();

    if (dst > cfg->ranges.rad_awaregrey)
    {
        return VIS_NOBODY_AROUND;
    }

    if (DennisToo::GlobalData::allowEvasion && dst < cfg->ranges.rad_tooclose)
    {
        return VIS_OH_DEAR_GAHD;
    }

    if (DennisToo::GlobalData::lifeIsWar)
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

void zNMEDenToo::RespondToHittingPlayer()
{
    sndID[9] = xSndMgrPlay(snd_table->sndGroupHandle[9], 0, NULL, NULL,
                           xglobals->___player_ent_dont_use_directly, NULL, NULL);
    oneLiners.PlaySound(0);
}

// No GC symbol (always inlined into Process).
inline void zNMEDenToo::BossCamSet(en_dtbcmode mod_desired, bool forceCut)
{
    en_dtbcmode currCamMode = mod_camCurr;

    if (currCamMode != mod_desired)
    {
        switch (currCamMode)
        {
        case DENT_CAMMOD_NORMAL:
            break;
        case DENT_CAMMOD_BINARY:
            cruise_bubble::Cam_Launch = NULL;
            cruise_bubble::Cam_Return = NULL;
            zCamRemove(cam_boss, false);
            break;
        }

        switch (mod_desired)
        {
        case DENT_CAMMOD_NORMAL:
            break;
        case DENT_CAMMOD_BINARY:
            cruise_bubble::Cam_Launch = cruise_bubble::xCamLaunch;
            cruise_bubble::Cam_Return = cruise_bubble::xCamReturn;
            zCamAdd(cam_boss, forceCut);
            break;
        }

        mod_camCurr = mod_desired;
    }
}

void zNMEDenToo::BossCamOverride(zBossCam_Binary* cam, F32 dt, void* ctxt)
{
    zNMEDenToo* boss = (zNMEDenToo*)ctxt;

    if (boss != NULL && cam == &boss->cam_boss)
    {
        boss->BossCamRepel(dt);
    }
}

// No GC symbol (always inlined into BossCamOverride). Keeps the binary camera out of a sphere
// around the arena center.
inline void zNMEDenToo::BossCamRepel(F32 dt)
{
    if (mod_camCurr != DENT_CAMMOD_BINARY)
    {
        return;
    }

    xVec3 vec_cam = cam_boss.mat.pos - DennisToo::CamRepel::pos_center;
    F32 dst = vec_cam.length();
    if (dst > 13.5f || dst < 0.00001f)
    {
        return;
    }

    xVec3 dir_cam = vec_cam * (1.0f / dst);
    xVec3 pos_tobe = DennisToo::CamRepel::pos_center;
    pos_tobe += dir_cam * 13.5f;
    cam_boss.mat.pos = pos_tobe;
}

void zNMEDenToo::MeterRender()
{
    DennisToo::HealthBar::mtr_health.Render();
}

S32 zNMEDenToo::TypeHandleMail(NMEMsg* mail)
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

S32 zNMEDenToo::DenMailDamage(NMEDamageInfo* dmgmail)
{
    S32 handled = 1;

    switch (dmgmail->dmg_type)
    {
    case DMGTYP_KILLEVENT:
    {
        xPsyche* psy = psy_self;
        xGoal* goal = psy->FindGoal(NME_GOAL_DENTOO_DEAD);
        ((zNMEGoalDentDead*)goal)->stylinDyin = DIE_MUFFLED;
        psy->GoalSet(NME_GOAL_DENTOO_DEAD, 0);
        cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_OUTRO];
        pts_health = cfg_dentoo->health.pts_healthMax;
        mod_camWant = DENT_CAMMOD_NORMAL;
        tmr_nextLeap = -1.0f;
        stageCurr = DENT_STAGE_OUTRO;
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
            case DENT_STAGE_A:
                zEntEvent(this, this, eEventBossStageEnded);
                zEntEvent(this, this, eEventBossStageEndedA);
                break;
            case DENT_STAGE_B:
                zEntEvent(this, this, eEventBossStageEnded);
                zEntEvent(this, this, eEventBossStageEndedB);
                break;
            case DENT_STAGE_C:
                zEntEvent(this, this, eEventBossStageEnded);
                zEntEvent(this, this, eEventBossStageEndedC);
                break;
            }
        }

        if (pts_health < 1 && stageCurr == DENT_STAGE_C)
        {
            xGoal* goal = psy_self->FindGoal(NME_GOAL_DENTOO_DEAD);
            ((zNMEGoalDentDead*)goal)->stylinDyin = DIE_MUFFLED;
            psy_self->GoalSet(NME_GOAL_DENTOO_DEAD, 0);
            cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_OUTRO];
            pts_health = cfg_dentoo->health.pts_healthMax;
            mod_camWant = DENT_CAMMOD_NORMAL;
            tmr_nextLeap = -1.0f;
            stageCurr = DENT_STAGE_OUTRO;
        }
        else
        {
            psy_self->GoalSet(NME_GOAL_DENTOO_DAMAGE, 0);
        }
        break;
    }
    default:
        handled = 0;
        break;
    }

    return handled;
}

S32 zNMEDenToo::DenMailSys(NMESysEvent* sysmail)
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
            cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_INTRO];
            pts_health = cfg_dentoo->health.pts_healthMax;
            mod_camWant = DENT_CAMMOD_NORMAL;
            tmr_nextLeap = -1.0f;
            stageCurr = DENT_STAGE_INTRO;
        }
        else if (gotoStage == 1)
        {
            cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_A];
            pts_health = cfg_dentoo->health.pts_healthMax;
            mod_camWant = DENT_CAMMOD_BINARY;
            tmr_nextLeap = -1.0f;
            stageCurr = DENT_STAGE_A;
        }
        else if (gotoStage == 2)
        {
            cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_B];
            pts_health = cfg_dentoo->health.pts_healthMax;
            mod_camWant = DENT_CAMMOD_BINARY;
            tmr_nextLeap = -1.0f;
            stageCurr = DENT_STAGE_B;
        }
        else if (gotoStage == 3)
        {
            cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_C];
            pts_health = cfg_dentoo->health.pts_healthMax;
            mod_camWant = DENT_CAMMOD_BINARY;
            tmr_nextLeap = -1.0f;
            stageCurr = DENT_STAGE_C;
        }
        else if (gotoStage == 4)
        {
            cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_OUTRO];
            pts_health = cfg_dentoo->health.pts_healthMax;
            mod_camWant = DENT_CAMMOD_NORMAL;
            tmr_nextLeap = -1.0f;
            stageCurr = DENT_STAGE_OUTRO;
        }
        else
        {
            cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_UNKNOWN];
            pts_health = cfg_dentoo->health.pts_healthMax;
            mod_camWant = DENT_CAMMOD_NORMAL;
            tmr_nextLeap = -1.0f;
            stageCurr = DENT_STAGE_UNKNOWN;
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

// No GC symbol: the PS2 demo's zNMEDenToo::PickEvade, inlined on GC into its only caller,
// PickEvadePoint.
zMovePoint* zNMEDenToo::PickEvade(zMovePoint* nav_from)
{
    U32 num;
    xVec3 pos_plyr;
    F32 dst_best;
    zMovePoint* nav_best;
    U32 i;
    xBase* baseitem;
    zMovePoint* mvpt;
    xVec3 pos_mvpt;

    num = xGroupGetCount(grp_mvpt);
    if (num < 1)
    {
        return NULL;
    }

    pos_plyr = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
    dst_best = -1.0f;
    nav_best = NULL;

    for (i = 0; i < num; i++)
    {
        F32 dst;

        baseitem = xGroupGetItemPtr(grp_mvpt, i);
        if (baseitem->baseType != eBaseTypeMovePoint || baseitem == nav_from)
        {
            continue;
        }

        mvpt = (zMovePoint*)baseitem;
        pos_mvpt = *mvpt->PosGet();
        if (XZDstSqToPos(&pos_mvpt, NULL, NULL) < 2.25f)
        {
            continue;
        }

        dst = XZDstSq(&pos_plyr, mvpt->PosGet(), NULL, NULL);
        if (dst_best < 0.0f || dst > dst_best)
        {
            dst_best = dst;
            nav_best = mvpt;
        }
    }

    return nav_best;
}

zMovePoint* zNMEDenToo::PickEvadePoint(zMovePoint* nav_from)
{
    zMovePoint* nav_there;

    if (grp_mvpt == NULL)
    {
        if (nav_from != NULL)
        {
            xMovePointGetNext(nav_from, NULL, (xMovePoint**)&nav_there, NULL);
            if (nav_there != NULL)
            {
                return nav_there;
            }
            return nav_from;
        }
        return MvptFirstFollow();
    }

    return PickEvade(NULL);
}

S32 zNMEGoalDentNormal::Enter(F32 dt, void* updCtxt)
{
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalDentNormal::Exit(F32 dt, void* updCtxt)
{
    zNMEDenToo* npc = (zNMEDenToo*)psyche->clt_owner;

    switch (npc->stageCurr)
    {
    case DENT_STAGE_A:
        zEntEvent(npc, npc, eEventBossStageBegan);
        zEntEvent(npc, npc, eEventBossStageBeganA);
        break;
    case DENT_STAGE_B:
        zEntEvent(npc, npc, eEventBossStageBegan);
        zEntEvent(npc, npc, eEventBossStageBeganB);
        break;
    case DENT_STAGE_C:
        zEntEvent(npc, npc, eEventBossStageBegan);
        zEntEvent(npc, npc, eEventBossStageBeganC);
        break;
    }

    return 0;
}

S32 zNMEGoalDentNormal::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEDenToo* npc = (zNMEDenToo*)psyche->clt_owner;
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
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalDentBattle::Enter(F32 dt, void* updCtxt)
{
    tmr_tossDelay = -1.0f;
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalDentBattle::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEDenToo* npc = (zNMEDenToo*)psyche->clt_owner;
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
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalDentBattle::ChkExitCases(zNMEDenToo* npc, S32& nextgoal, en_trantype& trantype)
{
    en_vis vis;

    if (NMEC_TauntStillValid(npc))
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
        nextgoal = NME_GOAL_DENTOO_EVADE;
        return 1;
    }

    if (vis != VIS_HORROR_FLIK && vis != VIS_NOBODY_AROUND && tmr_tossDelay < 0.0f)
    {
        tmr_tossDelay = npc->cfg_dentoo->battle.tym_reload;
        trantype = GOAL_TRAN_PUSH;
        nextgoal = NME_GOAL_TRIGGER_ATTACK;
        npc->GoalPick(this, nextgoal, trantype);
        return 1;
    }

    return 0;
}

S32 zNMEGoalDentAttack::Enter(F32 dt, void* updCtxt)
{
    zNMEDenToo* npc = (zNMEDenToo*)psyche->clt_owner;

    npc->cnt_attacksTilEvade--;
    if (npc->cnt_attacksTilEvade < 0)
    {
        npc->cnt_attacksTilEvade = -1;
    }

    alt_throw = !alt_throw;
    idx_shot = NumKnivesToThrow(npc);
    pos_tgtBasis = *xEntGetCenter(xglobals->___player_ent_dont_use_directly);

    DoAutoAnim(NME_GSPOT_START, 0);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalDentAttack::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEDenToo* npc = (zNMEDenToo*)psyche->clt_owner;
    en_vis vis;
    S32 i;

    F32 tym_animCurr = npc->AnimTimeCurrent();
    if (tym_animCurr + dt >= npc->AnimDuration(NULL))
    {
        *trantype = GOAL_TRAN_POP;
        return 1;
    }

    vis = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_BATTLE, 0);
    if (vis == VIS_OH_DEAR_GAHD)
    {
        npc->nav_evade = npc->PickEvadePoint(npc->nav_evade);
        *trantype = GOAL_TRAN_SET;
        return NME_GOAL_DENTOO_EVADE;
    }

    for (i = 0; i < 3; i++)
    {
        ThrowUpdate(npc);
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

// No GC symbols (always inlined into zNMEGoalDentAttack::Enter/Process); the PS2 build has both
// here.
inline S32 zNMEGoalDentAttack::NumKnivesToThrow(zNMEDenToo* npc)
{
    NMECfgDenToo* cfg = npc->cfg_dentoo;

    if (cfg != NULL)
    {
        return cfg->throwToes.num_salvo;
    }
    return 1;
}

inline void zNMEGoalDentAttack::ThrowUpdate(zNMEDenToo* npc)
{
    S32 num_knives;
    S32 didThrow;

    if (idx_shot < 1)
    {
        return;
    }
    if (npc->AnimTimeCurrent() < 0.85f)
    {
        return;
    }
    if (npc->AnimTimeCurrent() > 1.0f)
    {
        return;
    }

    num_knives = NumKnivesToThrow(npc);
    didThrow = ThrowKnife_Toes(npc, num_knives - idx_shot);
    if (didThrow)
    {
        idx_shot--;
    }
}

S32 zNMEGoalDentAttack::ThrowKnife_Toes(zNMEDenToo* npc, S32 idx)
{
    NMECfgDenToo* cfg = npc->cfg_dentoo;
    zNMEHazard* haz;
    S32 rc;
    xVec3 pos_launch;

    haz = HAZ_Acquire();
    if (haz == NULL)
    {
        return 0;
    }

    rc = haz->ConfigHelper(HAZ_TYP_DENNIS_KNIFE);
    if (rc)
    {
        haz->npc_owner = npc;
    }
    else
    {
        haz->Discard();
        return 0;
    }

    pos_launch = ((xMat4x3*)&npc->model->Mat[30])->pos;
    pos_launch += DennisToo::Attack::vec_boneOffset;
    xMat4x3Toworld(&pos_launch, (xMat4x3*)npc->model->Mat, &pos_launch);

    xVec3 dir_aim = pos_tgtBasis - pos_launch;
    F32 dst_aim = dir_aim.length();
    if (dst_aim < 0.25f)
    {
        dst_aim = 1.5f;
        dir_aim = ((xMat4x3*)npc->model->Mat)->at;
    }
    else
    {
        dir_aim *= 1.0f / dst_aim;
    }

    F32 dst_near = cfg->throwToes.dst_nearTgt;
    F32 hyt_elevate = cfg->throwToes.hyt_elevate;
    F32 dst_step = (dst_aim - dst_near) / MAX(1, cfg->throwToes.num_salvo - 1);
    F32 dst_thisThrow = dst_step * idx + dst_near;
    // Two steps, as in zNMEGoalDenAttack::GolfBomb: a one-line MAX changes what gets inlined.
    F32 tym = dst_thisThrow / cfg->throwToes.spd_proj;
    tym = MAX(0.25f, tym);

    xVec3 pos_tgt = pos_launch;
    pos_tgt += g_Y3 * hyt_elevate;
    pos_tgt += dir_aim * dst_thisThrow;
    haz->custdata.lobdata.pos_tgt = pos_tgt;

    haz->Start(&pos_launch, tym);

    return 1;
}

// No symbol (always inlined into Enter); the name is ours, as zNMEGoalScare::ParabSetup.
inline void zNMEGoalDentEvade::ParabSetup(zNMEDenToo* npc)
{
    xVec3 vel;
    xVec3 dir = *npc->nav_evade->pos - *xEntGetPos(npc);

    vel = dir;
    vel *= 1.0f / 1.2f;
    vel.y += 25.0f * (0.5f * 1.2f);

    parab->initPos = *xEntGetPos(npc);
    parab->initVel = vel;
    parab->gravity = 25.0f;
    parab->minTime = 0.0f;
    parab->maxTime = 1.2f;
    tmr_leaping = 1.2f;
}

// No symbol (always inlined into Enter); the name is ours, after zNMETongueSpin::RibbonStart.
inline void zNMEGoalDentEvade::RibbonStart()
{
    NMEHelpRib::RibData* ribhelp = &DennisToo::Evade::Ribbon::rib_normal;

    if (ribhelp->flags.enable || ribhelp->flags.dev_enable)
    {
        ribbon = zFXRibbonPoolNew();
        if (ribbon != NULL)
        {
            ribhelp->ConfigRibbon(ribbon);
            cnt_skipRibbonJoint = ribhelp->updinfo.frq_ribJoint;
        }
    }
}

S32 zNMEGoalDentEvade::Enter(F32 dt, void* updCtxt)
{
    zNMEDenToo* npc = (zNMEDenToo*)psyche->clt_owner;

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

    RibbonStart();

    if (LeapCrossesArena(npc, npc->nav_evade))
    {
        npc->blur_jumpLong.activate(DennisToo::BlurFX::jumpBlurLG.parms.tym_lifespan,
                                    DennisToo::BlurFX::jumpBlurLG.parms.alf_master,
                                    DennisToo::BlurFX::jumpBlurLG.cfg_blur, npc->model);
    }
    else
    {
        npc->blur_jumpShort.activate(DennisToo::BlurFX::jumpBlurSM.parms.tym_lifespan,
                                     DennisToo::BlurFX::jumpBlurSM.parms.alf_master,
                                     DennisToo::BlurFX::jumpBlurSM.cfg_blur, npc->model);
    }

    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalDentEvade::Exit(F32 dt, void* updCtxt)
{
    zNMEDenToo* npc = (zNMEDenToo*)psyche->clt_owner;
    NMECfgDenToo* cfg;

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

    cfg = npc->cfg_dentoo;
    npc->tmr_nextLeap = cfg->battle.tym_leapDelay;
    npc->cnt_attacksTilEvade = cfg->battle.num_atakPerLeap;

    if (npc->blur_jumpLong.activity != NULL)
    {
        npc->blur_jumpLong.set_lifetime(0.0f, DennisToo::BlurFX::jumpBlurLG.parms.tym_fade);
    }

    if (npc->blur_jumpShort.activity != NULL)
    {
        npc->blur_jumpShort.set_lifetime(0.0f, DennisToo::BlurFX::jumpBlurSM.parms.tym_fade);
    }

    return 0;
}

S32 zNMEGoalDentEvade::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEDenToo* npc = (zNMEDenToo*)psyche->clt_owner;
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

        if (npc->AnimTimeCurrent() >= 0.2f)
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

        if (npc->blur_jumpShort.activity != NULL)
        {
            npc->blur_jumpShort.move(npc->bound.sph.center);
        }
        if (npc->blur_jumpLong.activity != NULL)
        {
            npc->blur_jumpLong.move(npc->bound.sph.center);
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

    if (arrivedAtDest && parab != NULL)
    {
        SplashLanding(npc);
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

void zNMEGoalDentEvade::SplashLanding(zNMEDenToo* npc)
{
    if (parab != NULL)
    {
        xVec3 pos;
        xParabolaEvalPos(parab, &pos, parab->maxTime);

        xVec3 dir_drift = pos - parab->initPos;
        F32 len = dir_drift.length();
        if (len > 0.00001f)
        {
            dir_drift *= 1.0f / len;
        }
        else
        {
            dir_drift = g_Y3;
        }

        F32 remain = npc->AnimTimeRemain(NULL);
        F32 dur = npc->AnimDuration(NULL);
        F32 spd_drift = 5.0f * CLAMP(remain / MAX(0.00001f, dur), 0.0f, 1.0f);
        xVec3 vel_drift = dir_drift * spd_drift;

        DennisToo::Evade::gasSplash.Fountain((xVec3*)&npc->model->Mat->pos, &vel_drift);
    }
}

// No GC symbol: inlined into Process (the PS2 build has it out of line here).
S32 zNMEGoalDentEvade::UpdateAnim(zNMEDenToo* npc, F32 dt)
{
    F32 tym_animPlusTime;
    F32 tym_dur;
    S32 arrived;

    tym_animPlusTime = npc->AnimTimeCurrent();
    tym_dur = npc->AnimDuration(NULL);
    tym_animPlusTime += dt;
    arrived = 0;
    switch (status)
    {
    case 0:
        if (tym_animPlusTime >= tym_dur)
        {
            DoAutoAnim(NME_GSPOT_LOOP, 0);
            status = 1;
        }
        break;
    case 1:
        if (tmr_leaping <= 0.15f)
        {
            DoAutoAnim(NME_GSPOT_FINISH, 0);
            status = 2;
        }
        break;
    case 2:
        if (tym_animPlusTime >= tym_dur)
        {
            arrived = 1;
        }
        break;
    }

    return arrived;
}

S32 zNMEGoalDentEvade::PathUpdate(zNMEDenToo* npc, F32 dt)
{
    if (parab)
    {
        return PathParabola(npc, dt);
    }
    return PathDirect(npc, dt);
}

// dt is unused, as in retail.
S32 zNMEGoalDentEvade::PathParabola(zNMEDenToo* npc, F32 dt)
{
    S32 arrived;
    F32 tym_inLeap;
    xVec3 pos;
    xVec3 vel;
    xMat3x3 mat_rot;
    F32 mag;

    // The test goes through a 0/1 value, as in zNMEGoalDenEvade::PathParabola and BfBB's robot
    // goals.
    arrived = 0;
    if ((tmr_leaping < 0.0f) ? 1 : 0)
    {
        arrived = 1;
    }

    tym_inLeap = MAX(0.0f, tmr_leaping);
    tym_inLeap = 1.2f - tym_inLeap;
    tym_inLeap = CLAMP(tym_inLeap, 0.0f, 1.2f);

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

S32 zNMEGoalDentEvade::PathDirect(zNMEDenToo* npc, F32 dt)
{
    return npc->MatMoveToPos(npc->nav_evade->pos, dt, 5.0f, 1, -1.0f);
}

void zNMEGoalDentEvade::RibbonLoop(zNMEDenToo* npc, F32 dt)
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
    ribhelp = &DennisToo::Evade::Ribbon::rib_normal;
    if (ribhelp->flags.dev_enable)
    {
        ribhelp->ConfigRibbon(rib);
    }

    alpha = ribhelp->updinfo.alf_master;
    scale = npc->bound.sph.r * ribhelp->updinfo.scl_master;

    // Emits from the last bone. Retail draws a random number here and discards it.
    xrand_RandomInt32();
    S32 idx = CLAMP(npc->model->BoneCount + 1, 1, npc->model->BoneCount);

    xMat4x3* mat = (xMat4x3*)&npc->model->Mat[idx];
    xVec3 pos_emit = mat->pos + *(xVec3*)&npc->model->Mat->pos;

    xVec3 dir_up = (xrand_RandomInt32() & 0x800000) ? mat->right : mat->up;
    if (xrand_RandomInt32() & 0x800000)
    {
        dir_up = mat->at;
    }

    rib->insert(pos_emit, dir_up, scale, alpha, 0);
}

S32 zNMEGoalDentEvade::GoalHandleMail(NMEMsg* mail)
{
    S32 snarfed = 1;
    zNMEDenToo* npc;
    xPsyche* psy;
    en_plyrpup pup;
    S32 amt_dmg;
    xGoal* goal;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        npc = (zNMEDenToo*)psyche->clt_owner;
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
            case DENT_STAGE_A:
                zEntEvent(npc, npc, eEventBossStageEnded);
                zEntEvent(npc, npc, eEventBossStageEndedA);
                break;
            case DENT_STAGE_B:
                zEntEvent(npc, npc, eEventBossStageEnded);
                zEntEvent(npc, npc, eEventBossStageEndedB);
                break;
            case DENT_STAGE_C:
                zEntEvent(npc, npc, eEventBossStageEnded);
                zEntEvent(npc, npc, eEventBossStageEndedC);
                break;
            }
        }

        if (npc->pts_health < 1 && npc->stageCurr == DENT_STAGE_C)
        {
            goal = psy->FindGoal(NME_GOAL_DENTOO_DEAD);
            ((zNMEGoalDentDead*)goal)->stylinDyin = DIE_MUFFLED;
            npc->psy_self->GoalSet(NME_GOAL_DENTOO_DEAD, 0);
            npc->cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_OUTRO];
            npc->pts_health = npc->cfg_dentoo->health.pts_healthMax;
            npc->mod_camWant = DENT_CAMMOD_NORMAL;
            npc->tmr_nextLeap = -1.0f;
            npc->stageCurr = DENT_STAGE_OUTRO;
        }
        break;
    default:
        snarfed = 0;
        break;
    }

    return snarfed;
}

S32 zNMEGoalDentTaunt::Enter(F32 dt, void* updCtxt)
{
    zNMEDenToo* npc = (zNMEDenToo*)psyche->clt_owner;

    DoAutoAnim(NME_GSPOT_START, 0);
    npc->SndPlayFromTable(1);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalDentTaunt::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEDenToo* npc = (zNMEDenToo*)psyche->clt_owner;
    en_vis vis;
    S32 nextgoal;

    vis = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_BATTLE, 0);
    if (vis == VIS_OH_DEAR_GAHD)
    {
        npc->nav_evade = npc->PickEvadePoint(npc->nav_evade);
        *trantype = GOAL_TRAN_SET;
        return NME_GOAL_DENTOO_EVADE;
    }

    if (!NMEC_TauntStillValid(npc))
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

S32 zNMEGoalDentDamage::Enter(F32 dt, void* updCtxt)
{
    zNMEDenToo* npc = (zNMEDenToo*)psyche->clt_owner;

    DoAutoAnim(NME_GSPOT_START, 0);
    npc->oneLiners.PlaySound(1);
    return zNMEGoalCommon::Enter(dt, updCtxt);
}

S32 zNMEGoalDentDamage::Exit(F32 dt, void* updCtxt)
{
    ((zNMEDenToo*)psyche->clt_owner)->tmr_nextLeap = -1.0f;
    return 0;
}

S32 zNMEGoalDentDamage::Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    zNMEDenToo* npc = (zNMEDenToo*)psyche->clt_owner;
    S32 nextgoal = 0;
    S32 doTran;
    en_vis vis;

    doTran = ChkExistence(npc, dt, nextgoal, *trantype);
    if (doTran)
    {
        return nextgoal;
    }

    if (npc->pts_health > 0)
    {
        vis = npc->VisionTarget(xglobals->___player_ent_dont_use_directly, NME_DEFCON_BATTLE, 0);
        if (vis == VIS_OH_DEAR_GAHD)
        {
            npc->nav_evade = npc->PickEvadePoint(npc->nav_evade);
            *trantype = GOAL_TRAN_SET;
            nextgoal = NME_GOAL_DENTOO_EVADE;
            return nextgoal;
        }
    }

    return zNMEGoalCommon::Process(trantype, dt, ctxt);
}

S32 zNMEGoalDentDamage::ChkExistence(zNMEDenToo* npc, F32 dt, S32& nextgoal, en_trantype& trantype)
{
    F32 tym_animCurr = npc->AnimTimeCurrent();
    if (tym_animCurr + dt >= npc->AnimDuration(NULL))
    {
        if (npc->pts_health < 1)
        {
            if (npc->stageCurr == DENT_STAGE_A)
            {
                npc->cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_B];
                npc->pts_health = npc->cfg_dentoo->health.pts_healthMax;
                npc->mod_camWant = DENT_CAMMOD_BINARY;
                npc->tmr_nextLeap = -1.0f;
                npc->stageCurr = DENT_STAGE_B;
            }
            else if (npc->stageCurr == DENT_STAGE_B)
            {
                npc->cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_C];
                npc->pts_health = npc->cfg_dentoo->health.pts_healthMax;
                npc->mod_camWant = DENT_CAMMOD_BINARY;
                npc->tmr_nextLeap = -1.0f;
                npc->stageCurr = DENT_STAGE_C;
            }
            else if (npc->stageCurr == DENT_STAGE_C)
            {
                npc->cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_OUTRO];
                npc->pts_health = npc->cfg_dentoo->health.pts_healthMax;
                npc->mod_camWant = DENT_CAMMOD_NORMAL;
                npc->tmr_nextLeap = -1.0f;
                npc->stageCurr = DENT_STAGE_OUTRO;
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

S32 zNMEGoalDentDamage::GoalHandleMail(NMEMsg* mail)
{
    S32 snarfed = 1;
    zNMEDenToo* npc;
    NMEDamageInfo* dmgmail;
    xPsyche* psy;
    en_plyrpup pup;
    S32 amt_dmg;
    xGoal* goal;

    switch (mail->msgid)
    {
    case NME_MID_DAMAGE:
        npc = (zNMEDenToo*)psyche->clt_owner;
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
            case DENT_STAGE_A:
                zEntEvent(npc, npc, eEventBossStageEnded);
                zEntEvent(npc, npc, eEventBossStageEndedA);
                break;
            case DENT_STAGE_B:
                zEntEvent(npc, npc, eEventBossStageEnded);
                zEntEvent(npc, npc, eEventBossStageEndedB);
                break;
            case DENT_STAGE_C:
                zEntEvent(npc, npc, eEventBossStageEnded);
                zEntEvent(npc, npc, eEventBossStageEndedC);
                break;
            }
        }

        if (npc->pts_health < 1 && npc->stageCurr == DENT_STAGE_C)
        {
            goal = psy->FindGoal(NME_GOAL_DENTOO_DEAD);
            ((zNMEGoalDentDead*)goal)->stylinDyin = DIE_MUFFLED;
            psy->GoalSet(NME_GOAL_DENTOO_DEAD, 0);
            npc->cfg_dentoo = DennisToo::Config::cfg_hoffStages[DENT_STAGE_OUTRO];
            npc->pts_health = npc->cfg_dentoo->health.pts_healthMax;
            npc->mod_camWant = DENT_CAMMOD_NORMAL;
            npc->tmr_nextLeap = -1.0f;
            npc->stageCurr = DENT_STAGE_OUTRO;
        }
        break;
    default:
        snarfed = 0;
        break;
    }

    return snarfed;
}

S32 zNMEGoalDentDead::Enter(F32 dt, void* updCtxt)
{
    return zNMEGoalDead::Enter(dt, updCtxt);
}

S32 zNMEGoalDentDead::Exit(F32 dt, void* updCtxt)
{
    return zNMEGoalDead::Exit(dt, updCtxt);
}

// Retail keeps the swimmer module statics with zNMETypeBossDenToo.cpp's data (.bss/.sbss).
static Swimmer::zMODSwimmer* g_mod_swimmer;

// No GC symbol: inlined into zNMECentral_Startup through ZNME_Common_Startup; the name is ours.
void ZNME_Swimmer_Startup()
{
    static Swimmer::zMODSwimmer mod_swimmer;

    g_mod_swimmer = &mod_swimmer;
    zNMECentral_RegisterModule(&mod_swimmer, 'Swim');
}

inline void Swimmer::zMODSwimmer::ModulePrepUse()
{
}

void Swimmer::zMODSwimmer::ScenePrepare()
{
    flg_swimEnabled = 0;
    flg_skipRuntime = 0;
    flg_updateInCinematic = 1;
}

void Swimmer::zMODSwimmer::SceneFinish()
{
    flg_swimEnabled = 0;
}

void Swimmer::zMODSwimmer::ScenePostInit()
{
    if (zNMECommon_TypeInScene(NME_TYPE_DENNIS_V2))
    {
        flg_swimEnabled = 1;
    }
    else
    {
        flg_skipRuntime = 1;
        flg_swimEnabled = 0;
        return;
    }

    zNMEParFX_PartyInvite(NPAR_TYP_SEAMIST, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_SEAFOG, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_SEAFOG_NOROT, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_SEASPRAY, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_SEASPLASH, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_SEASPARKLE, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_SEAFOAM, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_SEARAINBOW, NULL, 0);
}

void Swimmer::zMODSwimmer::Timestep(F32 dt)
{
    if (flg_swimEnabled)
    {
        WorldFog_Upd(dt);
        RainyMist_Upd(dt);
        NeckSpray_Upd(dt);
        ArmSpray_Upd(dt);
        PitSpray_Upd(dt);
        HipSpray_Upd(dt);
        KneeSpray_Upd(dt);
    }
}

inline void Swimmer::zMODSwimmer::WorldFog_Upd(F32 dt)
{
    tmr_delaySeaFog = MAX(-1.0f, tmr_delaySeaFog - dt);
    if (tmr_delaySeaFog < 0.0f)
    {
        tmr_spraySeaFog = MAX(-1.0f, tmr_spraySeaFog - dt);
        if (tmr_spraySeaFog < 0.0f)
        {
            tmr_delaySeaFog = 0.07f * xurand() + 0.05f;
            tmr_spraySeaFog = 0.05f;
        }
        else
        {
            SeaFog::sea_fog.EmitLineUp(&SeaFog::pos_emitLeft[0], &SeaFog::pos_emitLeft[1], NULL,
                                       1.0f, -1, -100000.0f);
            SeaFog::sea_fog.EmitLineUp(&SeaFog::pos_emitRight[0], &SeaFog::pos_emitRight[1], NULL,
                                       1.0f, -1, -100000.0f);
        }
    }
}

inline void Swimmer::zMODSwimmer::RainyMist_Upd(F32 dt)
{
    tmr_delaySeaMist = MAX(-1.0f, tmr_delaySeaMist - dt);
    if (tmr_delaySeaMist < 0.0f)
    {
        tmr_spraySeaMist = MAX(-1.0f, tmr_spraySeaMist - dt);
        if (tmr_spraySeaMist < 0.0f)
        {
            tmr_delaySeaMist = 0.25f * xurand() + 0.25f;
            tmr_spraySeaMist = 0.75f;
        }
        else
        {
            SeaMist::sea_mist.DenSeaItRain(&SeaMist::pos_emitLeft[0], &SeaMist::pos_emitLeft[1],
                                           &World::vel_wind, -100000.0f, 1.0f, -1);
            SeaMist::sea_mist.DenSeaItRain(&SeaMist::pos_emitRight[0], &SeaMist::pos_emitRight[1],
                                           &World::vel_wind, -100000.0f, 1.0f, -1);
        }
    }
}

// The body-spray emitters are empty (PS2 DWARF: 8-byte functions; no GC symbol).
inline void Swimmer::zMODSwimmer::NeckSpray_Upd(F32)
{
}

inline void Swimmer::zMODSwimmer::ArmSpray_Upd(F32)
{
}

inline void Swimmer::zMODSwimmer::PitSpray_Upd(F32)
{
}

inline void Swimmer::zMODSwimmer::HipSpray_Upd(F32)
{
}

inline void Swimmer::zMODSwimmer::KneeSpray_Upd(F32)
{
}
