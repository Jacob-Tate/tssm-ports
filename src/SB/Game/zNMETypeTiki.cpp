#include "zNMETypeTiki.h"

#include "xBound.h"
#include "xDebug.h"
#include "xEvent.h"
#include "xSnd.h"
#include "xVec3.h"
#include "xVec3Inlines.h"
#include "zGlobals.h"
#include "zGrid.h"
#include "zMovePoint.h"
#include "zNMEParFX.h"
#include "zParEmitter.h"
#include "xBehaveMgr.h"
#include "xMath.h"
#include "xParEmitter.h"
#include "zNMESupport.h"
#include "zNMEShrapnel.h"
#include "zNMERumble.h"
#include "zEntButton.h"
#include "zThrown.h"
#include "zLightning.h"
#include "zParPTank.h"
#include "xMemMgr.h"
#include "xScene.h"
#include "zGoo.h"
#include "zSurface.h"

U32 g_hash_tikianim[2] = { 0, 0 };
char* g_strz_tikianim[2] = { "Unknown", "Idle01" };

static zNMETiki* orphanList;
static S32 numTikisOnScreen;
static S32 whichTikiToAnimate = -1;
static F32 sLoveyIconDist = 6.1f;
static F32 sLoveyIconOffset = -1.375f;
static F32 timeSinceLastExplode = 100.0f;
static F32 g_tmr_talkytiki = -1.0f;
static zParEmitter* cloudEmitter;
static xParEmitterCustomSettings loveyEmitterInfo;
static xParEmitterCustomSettings thunderEmitterInfo;
static RwRaster* sHelmetRast;
// Unreferenced, but retail's .bss keeps it after thunderEmitterInfo.
static _tagLightningAdd sThunderLightningInfo;

namespace Tiki
{
    namespace Thunder
    {
        const xVec3 vec_lower = { -0.7f, 0.0f, -0.7f };
        const xVec3 vec_upper = { 0.7f, 1.4f, 0.7f };
    } // namespace Thunder

    namespace Stone
    {
        const xVec3 vec_lower = { -0.7f, 0.0f, -0.7f };
        const xVec3 vec_upper = { 0.7f, 1.4f, 0.7f };
    } // namespace Stone
} // namespace Tiki

namespace FX_ThunGlow
{
    const F32 alf_range[2] = { 0.1f, 0.18f };
    const F32 fac_color[2] = { 0.25f, 0.75f };
} // namespace FX_ThunGlow

// Before zNMETiki_ThunderLaunchHazard's static, as in retail's .rodata.
namespace FX_LitFuse
{
    const S32 idx_fuseBones[4] = { 2, 3, 4, 5 };
    const F32 tym_walkSparks[4] = { 0.3f, 0.55f, 0.75f, 1.5f };
    const F32 tym_walkSmoke[4] = { 0.25f, 0.5f, 0.75f, 1.5f };
    const xVec3 vec_tipoff[4] = {
        { 0.7f, 1.0f, 0.0f },
        { 0.34f, 0.0f, 0.0f },
        { 0.4f, 0.0f, 0.0f },
        { 0.4f, 0.0f, 0.0f },
    };

    namespace Sparks
    {
        SparkyCurve curve_data[2] = {
            { 0.0f, { 0xFF, 0xFF, 0xFF, 0x7F }, 1.0f },
            { 1.0f, { 0xFF, 0xFF, 0xFF, 0x7F }, 0.0f },
        };

        const SparkyEmitter sparks = {
            NPAR_TYP_SPARKY,
            { 0, 0, { 0, 0 } },
            { 0.0f, 0.0f },
            { 0.02f, 0.25f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.05f, 0.1f },
            { 10, 10, 0.1f, -0.1f },
            { 2.0f, -4.0f, 3.0f, 0.5f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data, 2, 1.0f, 0.25f },
        };
    } // namespace Sparks

    namespace Smoke
    {
        static ExhaustCurve curve_data_redProgression[7] = {
            { 0.0f, { 0xF7, 0x6E, 0x32, 0x7F }, 0.0f },
            { 0.25f, { 0xC8, 0xC8, 0x0F, 0x7F }, 1.0f },
            { 0.33f, { 0xBE, 0x74, 0x0C, 0x7F }, 0.5f },
            { 0.5f, { 0x64, 0x64, 0x09, 0x7F }, 0.75f },
            { 0.66f, { 0xB3, 0x9D, 0x7E, 0x7F }, 0.25f },
            { 0.75f, { 0x92, 0x88, 0x7A, 0x7F }, 0.5f },
            { 1.0f, { 0x50, 0x47, 0x3B, 0x7F }, 0.0f },
        };

        static ExhaustCurve curve_data_purpleProgression[7] = {
            { 0.0f, { 0x82, 0x46, 0xB9, 0x7F }, 0.0f },
            { 0.25f, { 0x9B, 0x55, 0xA5, 0x7F }, 1.0f },
            { 0.33f, { 0x32, 0x28, 0x64, 0x7F }, 0.5f },
            { 0.5f, { 0x82, 0x55, 0x8C, 0x7F }, 0.75f },
            { 0.66f, { 0x9B, 0x73, 0xAF, 0x7F }, 0.25f },
            { 0.75f, { 0xAF, 0x8C, 0xD2, 0x7F }, 0.5f },
            { 1.0f, { 0x32, 0x28, 0x3C, 0x7F }, 0.0f },
        };

        static ExhaustCurve curve_data_glueProgression[9] = {
            { 0.0f, { 0xFA, 0xFA, 0x00, 0x7F }, 0.0f },
            { 0.12f, { 0xFF, 0x00, 0xFA, 0x7F }, 1.0f },
            { 0.18f, { 0x8E, 0x58, 0xF9, 0x7F }, 0.5f },
            { 0.25f, { 0xAB, 0x5A, 0xF9, 0x7F }, 1.25f },
            { 0.3f, { 0x0C, 0xD2, 0x9F, 0x7F }, 0.75f },
            { 0.5f, { 0x34, 0x6D, 0xF6, 0x7F }, 1.0f },
            { 0.65f, { 0x34, 0x00, 0x43, 0x7F }, 0.75f },
            { 0.76f, { 0xB4, 0x00, 0x00, 0x7F }, 0.35f },
            { 1.0f, { 0x64, 0x64, 0x64, 0x7F }, 0.0f },
        };

        const ExhaustEmitter smoke = {
            NPAR_TYP_EXHAUST,
            { 0, 0, { 0, 0 } },
            { -4.0f, 0.0f, 3.0f },
            { 0.5f, 0.5f },
            { 0.25f, { 0.1f, 0.9f }, 0x20, { 0, 0, 0 } },
            { 0.36f, 0.31f },
            { 1, 4, 0.05f, 0.05f },
            { 0.2f, 0.4f, 0.0f, 0.1f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f } },
            { curve_data_glueProgression, 9, 1.0f, 1.0f },
        };
    } // namespace Smoke
} // namespace FX_LitFuse

static S32 loveyIdleCB(xGoal* rawgoal, void*, en_trantype* trantype, F32 dt, void*);
static S32 loveyPatrolCB(xGoal* rawgoal, void*, en_trantype* trantype, F32 dt, void*);
static S32 quietIdleCB(xGoal* rawgoal, void*, en_trantype* trantype, F32 dt, void*);
static S32 quietHideCB(xGoal* rawgoal, void*, en_trantype* trantype, F32 dt, void*);
static S32 thunderIdleCB(xGoal* rawgoal, void*, en_trantype* trantype, F32 dt, void*);
static S32 thunderCountCB(xGoal* rawgoal, void*, en_trantype* trantype, F32 dt, void*);
static S32 tikiDyingCB(xGoal* rawgoal, void*, en_trantype* trantype, F32 dt, void*);
static S32 tikiDeadCB(xGoal* rawgoal, void*, en_trantype* trantype, F32 dt, void*);
static void genericTikiRender(xEnt* ent);
static void loveyTikiRender(xEnt* ent);

// Inlined into Damage on GC (no symbol, absent from the PS2 DWARF); the name is ours.
static inline S32 NMEDamageTypeInList(en_npcdmg damtype, en_npcdmg* list)
{
    S32 found = 0;

    while (*list != DMGTYP_UNDECIDED)
    {
        if (damtype == *list)
        {
            found = 1;
            break;
        }
        list++;
    }

    return found;
}

// No GC or PS2 symbol (always inlined into loveyTikiRender); the name is ours. Squared XZ-plane
// distance.
static inline F32 TikiXZDstSq(const xVec3* pos_a, const xVec3* pos_b)
{
    xVec3 delta;

    xVec3Sub(&delta, pos_a, pos_b);
    delta.y = 0.0f;
    return xVec3Length2(&delta);
}

void ZNME_Tiki_Startup()
{
    S32 i;

    for (i = 0; i < 2; i++)
    {
        g_hash_tikianim[i] = xStrHash(g_strz_tikianim[i]);
    }
}

static void TikiFX_Setup_Thunder();
static void TikiFX_Setup_Lovey();

void zNMETiki_InitFX(zScene* zsc)
{
    if (zNMECommon_TypeInScene(NME_TYPE_TIKI_THUNDER))
    {
        TikiFX_Setup_Thunder();
    }

    if (zNMECommon_TypeInScene(NME_TYPE_TIKI_LOVEY))
    {
        TikiFX_Setup_Lovey();
    }
}

// No GC symbol (always inlined into zMODEnemy::SceneReset): stacks every tiki on its parents and
// drops the unparented ones down to their landing height.
inline void zNMETiki_InitStacking(zScene* zsc)
{
    S32 i;
    zNMECommon* npc;
    en_npctyp type;
    zNMETiki* tiki;
    F32 dh;

    for (i = 0; i < zsc->num_npcs; i++)
    {
        npc = (zNMECommon*)zsc->npcs[i];
        type = (en_npctyp)npc->itemType;

        if (type >= NME_TYPE_TIKI && type <= NME_TYPE_TIKI_STONE)
        {
            tiki = (zNMETiki*)npc;

            if (type != NME_TYPE_TIKI_LOVEY)
            {
                tiki->FindParents(zsc);

                if (tiki->numParents == 0)
                {
                    dh = tiki->landHt - tiki->bound.box.box.lower.y;
                    tiki->bound.box.box.lower.y += dh;
                    tiki->bound.box.box.upper.y += dh;
                    tiki->bound.box.center.y += dh;
                    tiki->model->Mat->pos.y += dh;
                }
            }

            tiki->tikiFlag &= ~0x1;
        }
    }

    orphanList = NULL;
}

static void TikiFX_Setup_Thunder()
{
    cloudEmitter = zParEmitterFind("PAREMIT_THUNDER_CLOUD");
    if (cloudEmitter == NULL)
    {
        cloudEmitter = zParEmitterFind("PAREMIT_CLOUD");
    }

    thunderEmitterInfo.custom_flags = 0xf5e;
    thunderEmitterInfo.vel.x = 0.0f;
    thunderEmitterInfo.vel.y = -0.3f;
    thunderEmitterInfo.vel.z = 0.0f;
    thunderEmitterInfo.vel_angle_variation = DEG2RAD(270);
    thunderEmitterInfo.rate.set(100.0f, 100.0f, 1.0f, 0);
    thunderEmitterInfo.life.set(2.0f, 2.0f, 1.0f, 0);
    thunderEmitterInfo.size_birth.set(0.8f, 0.8f, 1.0f, 0);
    thunderEmitterInfo.size_death.set(0.0f, 0.0f, 1.0f, 0);
    thunderEmitterInfo.color_birth[0].set(75.0f, 75.0f, 1.0f, 0);
    thunderEmitterInfo.color_birth[1].set(75.0f, 75.0f, 1.0f, 0);
    thunderEmitterInfo.color_birth[2].set(75.0f, 75.0f, 1.0f, 0);
    thunderEmitterInfo.color_birth[3].set(0.0f, 0.0f, 1.0f, 0);
    thunderEmitterInfo.color_death[0].set(0.0f, 0.0f, 1.0f, 0);
    thunderEmitterInfo.color_death[1].set(0.0f, 0.0f, 1.0f, 0);
    thunderEmitterInfo.color_death[2].set(0.0f, 0.0f, 1.0f, 0);
    thunderEmitterInfo.color_death[3].set(255.0f, 255.0f, 1.0f, 0);

    zNMEParFX_PartyInvite(NPAR_TYP_SPARKY, NULL, 0);
    zNMEParFX_PartyInvite(NPAR_TYP_EXHAUST, NULL, 0);
}

static void TikiFX_Setup_Lovey()
{
    char* nam_texture;

    loveyEmitterInfo.custom_flags = 0xf5e;

    loveyEmitterInfo.vel.x = 0.0f;
    loveyEmitterInfo.vel.y = -0.5f;
    loveyEmitterInfo.vel.z = 0.0f;
    loveyEmitterInfo.vel_angle_variation = DEG2RAD(90);
    loveyEmitterInfo.rate.set(100.0f, 100.0f, 1.0f, 0);
    loveyEmitterInfo.life.set(2.0f, 2.0f, 1.0f, 0);
    loveyEmitterInfo.size_birth.set(0.5f, 0.5f, 1.0f, 0);
    loveyEmitterInfo.size_death.set(0.3f, 0.3f, 1.0f, 0);
    loveyEmitterInfo.color_birth[0].set(255.0f, 255.0f, 1.0f, 0);
    loveyEmitterInfo.color_birth[1].set(175.0f, 175.0f, 1.0f, 0);
    loveyEmitterInfo.color_birth[2].set(175.0f, 175.0f, 1.0f, 0);
    loveyEmitterInfo.color_birth[3].set(255.0f, 255.0f, 1.0f, 0);
    loveyEmitterInfo.color_death[0].set(255.0f, 255.0f, 1.0f, 0);
    loveyEmitterInfo.color_death[1].set(175.0f, 175.0f, 1.0f, 0);
    loveyEmitterInfo.color_death[2].set(175.0f, 175.0f, 1.0f, 0);
    loveyEmitterInfo.color_death[3].set(0.0f, 0.0f, 1.0f, 0);

    nam_texture = "target";
    sHelmetRast = NMEC_FindRWRaster(nam_texture);
}

// No GC symbol (inlined into zNMETiki_ExplodeFX). Not in the PS2 DWARF, where the same code is
// inlined into zNMEGoalTikiDying::Exit.
inline void zNMETiki_ThunderLaunchHazard(zNMETiki* tiki)
{
    static const xVec3 vec_offset = { 0.0f, 0.25f, 0.0f };

    zNMEHazard* haz = HAZ_Acquire();
    if (haz == NULL)
    {
        return;
    }

    if (!haz->ConfigHelper(HAZ_TYP_TIKIBOOM))
    {
        haz->Discard();
    }
    else
    {
        haz->npc_owner = tiki;
        xVec3 pos = vec_offset + tiki->bound.sph.center;
        haz->Start(&pos, -1.0f);
    }
}

void zNMETiki_ExplodeFX(zNMETiki* tiki)
{
    if (tiki->death_explosion != NULL && tiki->death_explosion->initCB != NULL)
    {
        tiki->death_explosion->initCB(tiki->death_explosion, tiki->model, NULL, NULL);
    }

    tiki->SndPlayFromTable(4); // eNMESound_Death

    if (tiki->itemType == NME_TYPE_TIKI_THUNDER)
    {
        zNMETiki_ThunderLaunchHazard(tiki);
    }
}

// Placeholder for dead-stripped data, not recovered code: retail's .rodata keeps two
// {1, 0, 0, 0} templates here that nothing references; their owners are unknown.
static void __deadstripped_zNMETypeTiki()
{
    F32 unknown1[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    F32 unknown2[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
}

static void zNMETiki_PickTikisToAnimate()
{
    xCollis* coll;
    xCollis* cend;

    coll = xglobals->___player_ent_dont_use_directly->collis->colls;
    cend = coll + xglobals->___player_ent_dont_use_directly->collis->idx;
    for (; coll < cend; coll++)
    {
        if (!(coll->flags & k_HIT_IT))
        {
            continue;
        }

        zNMECommon* npc = (zNMECommon*)coll->optr;
        if (npc == NULL || npc->baseType != eBaseTypeNPC)
        {
            continue;
        }

        if ((en_npctyp)npc->itemType >= NME_TYPE_TIKI &&
            (en_npctyp)npc->itemType <= NME_TYPE_TIKI_STONE)
        {
            ((zNMETiki*)npc)->tikiFlag &= ~0x180;
        }
    }

    zNMETiki* currOrphan = orphanList;
    while (currOrphan)
    {
        if (!currOrphan->_isCulled)
        {
            currOrphan->tikiFlag &= ~0x180;
        }

        currOrphan = currOrphan->nextOrphan;
        if (currOrphan && currOrphan == currOrphan->nextOrphan)
        {
            currOrphan = NULL;
        }
    }

    zNMECommon* npc = (zNMECommon*)globals.player.carry.grabbed;
    if (npc && npc->baseType == eBaseTypeNPC && (en_npctyp)npc->itemType >= NME_TYPE_TIKI &&
        (en_npctyp)npc->itemType <= NME_TYPE_TIKI_STONE)
    {
        ((zNMETiki*)npc)->tikiFlag &= ~0x180;
    }

    if ((xrand_RandomInt32() & 0x3f) == 0)
    {
        whichTikiToAnimate = (S32)(numTikisOnScreen * xurand()) + 1;
    }
    else
    {
        whichTikiToAnimate = -1;
    }

    numTikisOnScreen = 0;
}

// No GC symbol (inlined into zNMETiki_Timestep); body as the PS2 build's and BfBB's.
static void zNMETiki_ReparentOrphans()
{
    zNMETiki* holder;

    if (globals.sceneCur->sceneID == 'BB04')
    {
        while (orphanList != NULL)
        {
            orphanList->tikiFlag |= 0x3;
            orphanList->RemoveFromFamily();
            holder = orphanList->nextOrphan;
            orphanList->nextOrphan = NULL;
            orphanList = holder;
        }
    }
    else
    {
        while (orphanList != NULL)
        {
            if (orphanList->SelfType() != NME_TYPE_TIKI_LOVEY)
            {
                orphanList->FindParents(globals.sceneCur);
            }
            holder = orphanList->nextOrphan;
            orphanList->nextOrphan = NULL;
            orphanList = holder;
        }
    }
}

// No GC symbol (inlined into zMODEnemy::Timestep and dead-stripped); it lives here because
// PickTikisToAnimate and the timers are file statics. Named as BfBB's zNPCTiki_Timestep.
void zNMETiki_Timestep(F32 dt)
{
    zNMETiki_PickTikisToAnimate();
    zNMETiki_ReparentOrphans();

    timeSinceLastExplode += dt;
    g_tmr_talkytiki = MAX(-1.0f, g_tmr_talkytiki - dt);
}

xAnimTable* zNMETiki::CreateAnimTable(xAnimTable* table)
{
    char** names = g_strz_tikianim;

    if (!table)
    {
        table = xAnimTableNew("zNMETiki", 0);
    }

    xAnimTableNewState(table, names[1], 0x110, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    return table;
}

void zNMETiki::Reset()
{
    zNMECommon::Reset();

    xVec3Add(&bound.box.box.upper, &origLocalBound.box.box.upper, (xVec3*)&model->Mat->pos);
    xVec3Add(&bound.box.box.lower, &origLocalBound.box.box.lower, (xVec3*)&model->Mat->pos);
    xVec3Add(&bound.box.center, &origLocalBound.box.center, (xVec3*)&model->Mat->pos);

    RestoreColFlags();
    flg_vuln = DfltVulnFlags();

    timeToLive = 0.0f;
    tikiFlag = 0;
    for (S32 i = 0; i < 4; i++)
    {
        parents[i] = NULL;
    }
    numParents = 0;
    contactParent = -1;

    for (S32 i = 0; i < 4; i++)
    {
        children[i] = NULL;
    }
    numChildren = 0;
    vel = 0.0f;
    nonTikiParent = NULL;

    en_npctyp ntyp = (en_npctyp)itemType;
    switch (ntyp)
    {
    case NME_TYPE_TIKI_QUIET:
        break;
    case NME_TYPE_TIKI_LOVEY:
        t1 = 0.0f;
        t2 = -0.25f;
        t3 = -0.25f;
        xVec3Copy(&v1, (xVec3*)&model->Mat->pos);
        break;
    case NME_TYPE_TIKI_THUNDER:
        t1 = xurand();
        t2 = -0.5f;
        t3 = -0.5f;
        break;
    }

    if (itemType == NME_TYPE_TIKI_LOVEY)
    {
        hovdat.tmr_hovBubDelay = 24.0f * (0.25f * (xurand() - 0.5f)) + 24.0f;
        hovdat.tmr_hovBubEmit = 0.5f;
    }

    if (itemType == NME_TYPE_TIKI_THUNDER)
    {
        model->RedMultiplier = 1.0f;
        model->BlueMultiplier = 1.0f;
        model->GreenMultiplier = 1.0f;
        // Retail tests the type a second time here (both GC and PS2 code reload it).
        if (itemType == NME_TYPE_TIKI_THUNDER)
        {
            model->Alpha = FX_ThunGlow::alf_range[0];
        }
    }
    else
    {
        model->RedMultiplier = 1.0f;
        model->BlueMultiplier = 1.0f;
        model->GreenMultiplier = 1.0f;
    }

    psy_self->GoalSet(NME_GOAL_TIKI_IDLE, 1);
    xModelSetScale(model, 0.0f);

    if (navnet != NULL)
    {
        navnet->MvptReset(NULL);
    }

    if (rigidAnim != NULL)
    {
        xMat4x3Copy(rigidAnim, &g_I3);
    }
}

void zNMETiki::Setup()
{
    zNMECommon::Setup();

    if (model->BoneCount == 0)
    {
        rigidAnim = (xMat4x3*)xMemAlloc(gActiveHeap, 2 * sizeof(xMat4x3), 0);
        xMat4x3Copy(rigidAnim, &g_I3);
    }
    else
    {
        rigidAnim = NULL;
    }
}

void zNMETiki::Init(xEntAsset* entass)
{
    zNMECommon::Init(entass);

    flags |= 0x40;
    baseFlags |= 0x10;

    InitTikiBoundInfo();

    nextOrphan = NULL;

    if ((en_npctyp)itemType == NME_TYPE_TIKI_LOVEY)
    {
        render = loveyTikiRender;
    }
    else
    {
        render = genericTikiRender;
    }

    update = NULL;

    if (model->Anim)
    {
        tikiAnim = model->Anim->Single->State->Data->RawData[0];
        tikiAnimTime = model->Anim->Single->Time;
        model->Anim = NULL;
        tikiFlag &= ~0x180;
    }
    else
    {
        tikiAnim = NULL;
        tikiFlag &= ~0x180;
        tikiFlag |= 0x80;
    }

    bound.type = XBOUND_TYPE_BOX;

    for (xModelInstance* mdl = model; mdl; mdl = mdl->Next)
    {
        mdl->Scale.x = 0.0f;
        mdl->Scale.y = 0.0f;
        mdl->Scale.z = 0.0f;
    }
}

void zNMETiki::InitTikiBoundInfo()
{
    xBox* bbb = &bound.box.box;
    en_npctyp ntyp = (en_npctyp)itemType;
    xModelInstance* nextModel;

    if (ntyp == NME_TYPE_TIKI_THUNDER)
    {
        bbb->upper = Tiki::Thunder::vec_upper;
        bbb->lower = Tiki::Thunder::vec_lower;
    }
    else if (ntyp == NME_TYPE_TIKI_STONE)
    {
        bbb->upper = Tiki::Stone::vec_upper;
        bbb->lower = Tiki::Stone::vec_lower;
    }
    else
    {
        xBoxForModel(*bbb, model, false);

        for (nextModel = model->Next; nextModel; nextModel = nextModel->Next)
        {
            // Not PS2 DWARF locals.
            xVec3 prevLower;
            xVec3 prevUpper;

            xVec3Copy(&prevLower, &bound.box.box.lower);
            xVec3Copy(&prevUpper, &bound.box.box.upper);

            // Retail bug: every pass re-measures model->Next instead of nextModel.
            xBoxForModel(bound.box.box, model->Next, false);

            if (bound.box.box.lower.x > prevLower.x)
            {
                bound.box.box.lower.x = prevLower.x;
            }
            if (bound.box.box.lower.y > prevLower.y)
            {
                bound.box.box.lower.y = prevLower.y;
            }
            if (bound.box.box.lower.z > prevLower.z)
            {
                bound.box.box.lower.z = prevLower.z;
            }
            if (bound.box.box.upper.x < prevUpper.x)
            {
                bound.box.box.upper.x = prevUpper.x;
            }
            if (bound.box.box.upper.y < prevUpper.y)
            {
                bound.box.box.upper.y = prevUpper.y;
            }
            if (bound.box.box.upper.z < prevUpper.z)
            {
                bound.box.box.upper.z = prevUpper.z;
            }
        }
    }

    origLocalBound.type = XBOUND_TYPE_BOX;
    xVec3Sub(&origLocalBound.box.box.lower, &bound.box.box.lower, (xVec3*)&model->Mat->pos);
    xVec3Sub(&origLocalBound.box.box.upper, &bound.box.box.upper, (xVec3*)&model->Mat->pos);
    xVec3Add(&origLocalBound.box.center, &origLocalBound.box.box.lower,
             &origLocalBound.box.box.upper);
    xVec3SMulBy(&origLocalBound.box.center, 0.5f);
}

zNMENavNet* zNMETiki::Gimme_NavNet()
{
    if (itemType != NME_TYPE_TIKI_LOVEY)
    {
        return NULL;
    }

    if (navnet)
    {
        return navnet;
    }

    void* mem = xMemAlloc(gActiveHeap, sizeof(zNMENavNet), 0);
    navnet = (zNMENavNet*)mem;
    navnet->nme_owner = this;

    return navnet;
}

zMovePoint* zNMETiki::MvptFirstFollow()
{
    zMovePoint* nav;

    if (((zNMEAssetTiki*)nmeass)->aid_mvptFollow == 0)
    {
        return NULL;
    }

    nav = zMovePoint_From_xAssetID(((zNMEAssetTiki*)nmeass)->aid_mvptFollow);
    return nav;
}

void zNMETiki::Damage(en_npcdmg damtype, const xBase* who, const xVec3* vec_hit, S32 amt_dmgCaller,
                      en_plyrpup pup_fromCaller)
{
    en_npcdmg stoneAcceptsThese[11] = {
        DMGTYP_THUNDER_TIKI_EXPLOSION,
        DMGTYP_CRUISEBUBBLE,
        DMGTYP_EXPLOSION,
        DMGTYP_NMEKNOCKED,
        DMGTYP_DAMAGE_SURFACE,
        DMGTYP_KILLEVENT,
        DMGTYP_WOUNDEVENT,
        DMGTYP_INSTAKILL,
        DMGTYP_CARSMASH,
        DMGTYP_BUBBOWL,
        DMGTYP_UNDECIDED,
    };
    en_plyrpup pup;

    if ((en_npctyp)itemType == NME_TYPE_TIKI_STONE)
    {
        if (!NMEDamageTypeInList(damtype, stoneAcceptsThese))
        {
            return;
        }

        if (damtype == DMGTYP_BUBBOWL)
        {
            pup = zEntPlayer_PowerupLevel(PLYR_ATAK_SB_BOWL);
            if (pup <= ePowerupLevelNormal)
            {
                return;
            }
        }
    }

    if (flg_vuln)
    {
        psy_self->GoalSet(NME_GOAL_TIKI_DYING, 0);
        flg_vuln = 0;
    }
}

S32 zNMETiki::TypeHandleMail(NMEMsg* mail)
{
    if (mail->msgid == NME_MID_DAMAGE)
    {
        zNMETiki::Damage(mail->dmgdata.dmg_type, mail->dmgdata.dmg_from, &mail->dmgdata.vec_dmghit,
                         mail->dmgdata.amt_damage, mail->dmgdata.pup_player);
        return 1;
    }

    return 0;
}

S32 zNMETiki::SetCarryState(en_carystat cs)
{
    S32 i;

    if ((en_npctyp)itemType == NME_TYPE_TIKI_LOVEY)
    {
        return 0;
    }

    switch (cs)
    {
    case zNMECARRY_ATTEMPTPICKUP:
        if (numChildren)
        {
            for (i = 0; i < 4; i++)
            {
                if (children[i] != NULL &&
                    children[i]->bound.box.box.lower.y < bound.box.box.upper.y + 0.2f)
                {
                    return 0;
                }
            }

            return 1;
        }

        return 1;
    case zNMECARRY_PICKUP:
        tikiFlag |= 0x10;
        landHt = FLOAT_MIN;
        RemoveFromFamily();
        return 1;
    case zNMECARRY_NONE:
        if (tikiFlag & 0x10)
        {
            tikiFlag &= ~0x10;

            if (frame != NULL)
            {
                frame->mat.up.x = 0.0f;
                frame->mat.up.y = 1.0f;
                frame->mat.up.z = 0.0f;
            }

            model->Mat->up.x = 0.0f;
            model->Mat->up.y = 1.0f;
            model->Mat->up.z = 0.0f;

            if ((en_npctyp)itemType == NME_TYPE_TIKI_STONE)
            {
                tikiFlag |= 0x25;
                bound.type = XBOUND_TYPE_BOX;
            }
            else
            {
                Damage(DMGTYP_SIDE, NULL, NULL, -1, ePowerupLevelNormal);
            }

            return 1;
        }

        return 0;
    case zNMECARRY_TWIRL:
        break;
    }

    return 0;
}

void zNMETiki::SelfSetup()
{
    xBehaveMgr* bmgr;
    xPsyche* psy;
    xGoal* goal;

    bmgr = xBehaveMgr_GetSelf();
    psy_self = bmgr->Subscribe(this, 0);
    psy = psy_self;
    psy->BrainBegin();

    goal = psy->AddGoal(NME_GOAL_TIKI_IDLE, NULL);
    ((zNMEGoalCommon*)goal)->flg_npcgauto = 0;

    switch ((en_npctyp)itemType)
    {
    case NME_TYPE_TIKI_LOVEY:
        goal->fun_process = loveyIdleCB;
        goal->cbdata = NULL;
        goal = psy->AddGoal(NME_GOAL_TIKI_PATROL, NULL);
        goal->fun_process = loveyPatrolCB;
        goal->cbdata = NULL;
        break;
    case NME_TYPE_TIKI_QUIET:
        goal->fun_process = quietIdleCB;
        goal->cbdata = NULL;
        goal = psy->AddGoal(NME_GOAL_TIKI_HIDE, NULL);
        goal->fun_process = quietHideCB;
        goal->cbdata = NULL;
        break;
    case NME_TYPE_TIKI_THUNDER:
        goal->fun_process = thunderIdleCB;
        goal->cbdata = NULL;
        goal = psy->AddGoal(NME_GOAL_TIKI_COUNT, NULL);
        goal->fun_process = thunderCountCB;
        goal->cbdata = NULL;
        break;
    case NME_TYPE_TIKI_STONE:
        break;
    }

    goal = psy->AddGoal(NME_GOAL_TIKI_DYING, NULL);
    goal->fun_process = tikiDyingCB;
    goal->cbdata = NULL;

    goal = psy->AddGoal(NME_GOAL_TIKI_DEAD, NULL);
    goal->fun_process = tikiDeadCB;
    goal->cbdata = NULL;

    psy->BrainEnd();
    psy->SetSafety(NME_GOAL_TIKI_IDLE);
}

void zNMETiki::Process(F32 dt)
{
    BUpdate(xEntGetPos(this));

    ProcSubContract(dt);

    if (model->BoneCount)
    {
        UpdateAnimBound();
    }

    BUpdate(xEntGetPos(this));
}

// No GC symbol: inlined into its only caller (Process) and dead-stripped.
void zNMETiki::UpdateAnimBound()
{
    xModelInstance* minst_root = model;

    minst_root->combinedAnimBound.upper.x = -1e38f;

    if (model->Scale.x != 0.0f)
    {
        for (xModelInstance* minst = model; minst != NULL; minst = minst->Next)
        {
            xMat3x3 mat_orig;
            xMat3x3Copy(&mat_orig, (xMat3x3*)minst->Mat);

            minst->Mat->right.x *= minst->Scale.x;
            minst->Mat->right.y *= minst->Scale.x;
            minst->Mat->right.z *= minst->Scale.x;

            minst->Mat->up.x *= minst->Scale.y;
            minst->Mat->up.y *= minst->Scale.y;
            minst->Mat->up.z *= minst->Scale.y;

            minst->Mat->at.x *= minst->Scale.z;
            minst->Mat->at.y *= minst->Scale.z;
            minst->Mat->at.z *= minst->Scale.z;

            iModelAnimBoundWorld(&minst->animBound, &minst_root->combinedAnimBound, minst->Data,
                                 minst->Mat, minst->Mat + 1);

            xMat3x3Copy((xMat3x3*)minst->Mat, &mat_orig);
        }
    }
    else
    {
        for (xModelInstance* minst = model; minst != NULL; minst = minst->Next)
        {
            iModelAnimBoundWorld(&minst->animBound, &minst_root->combinedAnimBound, minst->Data,
                                 minst->Mat, minst->Mat + 1);
        }
    }
}

void zNMETiki::ProcSubContract(F32 dt)
{
    U32 i;

    zThrown_NMEUpdate(this, xglobals->sceneCur, dt);

    if (numChildren != 0 && !(tikiFlag & 0x8))
    {
        return;
    }

    tikiFlag &= ~0x8;

    if ((tikiFlag & 0x600) == 0x400)
    {
        zNMECommon::Process(dt);
        return;
    }

    if (tikiAnim != NULL && !(tikiFlag & 0x180))
    {
        F32 duration = iAnimDuration(tikiAnim);

        tikiAnimTime += dt;
        if (tikiAnimTime >= duration)
        {
            if (xrand_GenRandInt32() & 1)
            {
                tikiAnimTime -= duration;
            }
            else
            {
                tikiAnimTime = 0.0f;
                tikiFlag &= ~0x180;
                tikiFlag |= 0x80;
            }
        }

        xQuat* q0 = (xQuat*)g_iAnim_workBuffer;
        xVec3* t0 = (xVec3*)(q0 + 0x41);

        iAnimEval(tikiAnim, tikiAnimTime, 0, t0, q0);

        if (model->BoneCount)
        {
            iModelAnimMatrices(model->Data, q0, t0, model->Mat + 1);
        }
        else
        {
            iModelQuatToMat(q0 + 1, t0 + 1, (RwMatrixTag*)rigidAnim);
        }
    }

    // Declared here: the PS2 DWARF lists dy after the animation block's locals and before dh.
    F32 dy;

    HoverBubbles(dt);

    // Not in the PS2 DWARF (kept in registers there); name from BfBB's zNPCTiki::Process.
    xVec3 pos_save;
    xVec3Copy(&pos_save, (xVec3*)&model->Mat->pos);

    if (numParents != 0)
    {
        for (i = 0; i < 4; i++)
        {
            if (parents[i] != NULL && !(updatedParents & (1 << i)))
            {
                parents[i]->tikiFlag |= 0x8;
                parents[i]->Process(dt);
            }
        }
    }

    if ((tikiFlag & 0x600) == 0x200)
    {
        RemoveFromFamily();
        tikiFlag &= ~0x600;
        tikiFlag |= 0x400;
        return;
    }

    if (tikiFlag & 0x4)
    {
        if (nextOrphan == NULL)
        {
            nextOrphan = orphanList;
            orphanList = this;
        }
        else
        {
            xprintf("what the ...\n");
        }
    }
    tikiFlag &= ~0x4;

    if (psy_self != NULL)
    {
        psy_self->Timestep(dt, NULL);
    }

    zNMECommon::Process(dt);

    if (numParents != 0)
    {
        for (i = 0; i < 4; i++)
        {
            if (i != contactParent && parents[i] != NULL)
            {
                if (contactParent == -1)
                {
                    contactParent = i;
                }
                else if (parents[i]->bound.box.box.upper.y >
                         parents[contactParent]->bound.box.box.upper.y)
                {
                    contactParent = i;
                }
            }
        }
    }

    if (tikiFlag & 0x1)
    {
        vel -= 10.0f * dt;

        F32 dh = dt * vel;
        model->Mat->pos.y += dh;
        bound.box.box.lower.y += dh;
        bound.box.box.upper.y += dh;
        bound.box.center.y += dh;

        if (numParents != 0)
        {
            if (bound.box.box.lower.y < parents[contactParent]->bound.box.box.upper.y)
            {
                tikiFlag &= ~0x1;
                if (vel < -0.1f)
                {
                    SndPlayFromTable(7); // eNMESound_TikiStack
                }
                vel = 0.0f;
            }
        }
        else if (bound.box.box.lower.y < landHt)
        {
            F32 dh = landHt - bound.box.box.lower.y;
            bound.box.box.lower.y += dh;
            bound.box.box.upper.y += dh;
            bound.box.center.y += dh;
            model->Mat->pos.y += dh;

            tikiFlag &= ~0x1;
            if (tikiFlag & 0x2)
            {
                Damage(DMGTYP_DAMAGE_SURFACE, NULL, NULL, -1, ePowerupLevelNormal);
            }

            if (vel < -0.1f)
            {
                SndPlayFromTable(7); // eNMESound_TikiStack
            }
            vel = 0.0f;
        }
    }

    if (numParents != 0)
    {
        model->Mat->pos.x += parents[contactParent]->deltaPos.x;
        model->Mat->pos.z += parents[contactParent]->deltaPos.z;

        if (!(tikiFlag & 0x1))
        {
            dy = parents[contactParent]->bound.box.box.upper.y - bound.box.box.lower.y;
            bound.box.box.lower.y += dy;
            bound.box.box.upper.y += dy;
            bound.box.center.y += dy;
            model->Mat->pos.y += dy;
        }
    }
    else if (nonTikiParent != NULL)
    {
        if (!(nonTikiParent->chkby & 0x10) || !(nonTikiParent->penby & 0x10))
        {
            nonTikiParent = NULL;
            tikiFlag |= 0x5;
            landHt = FLOAT_MIN;
        }
        else
        {
            // Not in the PS2 DWARF (kept in registers there); name from BfBB's zNPCTiki::Process.
            xVec3 delta;
            xVec3Copy(&delta, (xVec3*)&nonTikiParent->model->Mat->pos);
            xVec3AddScaled(&delta, (xVec3*)&nonTikiParent->model->Mat->right, nonTikiParentDisp.x);
            xVec3AddScaled(&delta, (xVec3*)&nonTikiParent->model->Mat->up, nonTikiParentDisp.y);
            xVec3AddScaled(&delta, (xVec3*)&nonTikiParent->model->Mat->at, nonTikiParentDisp.z);
            xVec3SubFrom(&delta, (xVec3*)&model->Mat->pos);

            if (tikiFlag & 0x1)
            {
                delta.y = 0.0f;
            }

            xVec3AddTo(&bound.box.box.lower, &delta);
            xVec3AddTo(&bound.box.box.upper, &delta);
            xVec3AddTo(&bound.box.center, &delta);
            xVec3AddTo((xVec3*)&model->Mat->pos, &delta);

            if (!(tikiFlag & 0x1) && nonTikiParent->baseType == eBaseTypeButton)
            {
                zEntButton_Hold((_zEntButton*)nonTikiParent, 0x2000);
            }
        }
    }

    if (model->Scale.x)
    {
        // Not in the PS2 DWARF (kept in registers there); names from BfBB's zNPCTiki::Process.
        xVec3 scaledUpper;
        xVec3 scaledLower;
        xVec3 scaledCenter;

        xVec3SMul(&scaledUpper, &origLocalBound.box.box.upper, model->Scale.x);
        xVec3SMul(&scaledLower, &origLocalBound.box.box.lower, model->Scale.x);
        xVec3SMul(&scaledCenter, &origLocalBound.box.center, model->Scale.x);

        xVec3Add(&bound.box.box.upper, (xVec3*)&model->Mat->pos, &scaledUpper);
        xVec3Add(&bound.box.box.lower, (xVec3*)&model->Mat->pos, &scaledLower);
        xVec3Add(&bound.box.center, (xVec3*)&model->Mat->pos, &scaledCenter);
    }
    else
    {
        xVec3Add(&bound.box.box.upper, (xVec3*)&model->Mat->pos, &origLocalBound.box.box.upper);
        xVec3Add(&bound.box.box.lower, (xVec3*)&model->Mat->pos, &origLocalBound.box.box.lower);
        xVec3Add(&bound.box.center, (xVec3*)&model->Mat->pos, &origLocalBound.box.center);
    }

    xVec3Sub(&deltaPos, (xVec3*)&model->Mat->pos, &pos_save);

    if (numChildren != 0)
    {
        for (i = 0; i < 4; i++)
        {
            if (children[i] != NULL)
            {
                children[i]->ParentUpdated(this);
            }
        }
    }

    updatedParents = 0;

    if (frame != NULL)
    {
        frame->mode = 0x30000;
    }
}

S32 zNMETiki::SysEvent(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                       xBase* toParamWidget, U32 toParamWidgetID, S32* handled)
{
    switch (toEvent)
    {
    case eEventNPCRespawn:
        if ((tikiFlag & 0x600) != 0x400 && death_explosion != NULL &&
            death_explosion->initCB != NULL)
        {
            death_explosion->initCB(death_explosion, model, NULL, NULL);
        }

        RemoveFromFamily();
        Reset();

        if (itemType != NME_TYPE_TIKI_LOVEY)
        {
            tikiFlag |= 0x5;
            tikiFlag &= ~0x8;
            vel = 0.0f;
            landHt = FLOAT_MIN;
        }

        *handled = 1;
        break;
    case eEventVisible:
    case eEventFastVisible:
        if ((tikiFlag & 0x600) != 0x400)
        {
            xEntShow(this);

            if (toParam != NULL)
            {
                S32 code = (S32)(0.5f + toParam[0]);
                if (code == 'M')
                {
                    zFXPopOn(*this, toParam[1], toParam[2]);
                }
            }
        }

        *handled = 1;
        break;
    case eEventInvisible:
    case eEventFastInvisible:
        if (xEntIsVisible(this) && toParam != NULL)
        {
            S32 code = (S32)(0.5f + toParam[0]);
            if (code == 'M')
            {
                zFXPopOff(*this, toParam[1], toParam[2]);
            }
        }

        xEntHide(this);
        *handled = 1;
        break;
    case eEventCollisionOn:
        if ((tikiFlag & 0x600) != 0x400)
        {
            RestoreColFlags();
        }

        *handled = 1;
        break;
    case eEventCollisionOff:
        chkby = 0;
        *handled = 1;
        break;
    case eEventCollision_Visible_On:
        if ((tikiFlag & 0x600) != 0x400)
        {
            RestoreColFlags();
            xEntShow(this);

            if (toParam != NULL)
            {
                S32 code = (S32)(0.5f + toParam[0]);
                if (code == 'M')
                {
                    zFXPopOn(*this, toParam[1], toParam[2]);
                }
            }
        }

        *handled = 1;
        break;
    case eEventCollision_Visible_Off:
        chkby = 0;

        if (xEntIsVisible(this) && toParam != NULL)
        {
            S32 code = (S32)(0.5f + toParam[0]);
            if (code == 'M')
            {
                zFXPopOff(*this, toParam[1], toParam[2]);
            }
        }

        xEntHide(this);
        *handled = 1;
        break;
    case eEventHit:
        Damage(DMGTYP_INSTAKILL, from, NULL, -1, ePowerupLevelNormal);
        *handled = 1;
        break;
    }

    if (!*handled)
    {
        return zNMECommon::SysEvent(from, to, toEvent, toParam, toParamWidget, toParamWidgetID,
                                    handled);
    }

    return 1;
}

void zNMETiki::Render()
{
    render(this);
}

void zNMETiki::RenderExtra()
{
    if (rigidAnim)
    {
        xMat4x3Copy((xMat4x3*)model->Mat, &rigidAnim[1]);
    }
}

void zNMETiki::BUpdate(xVec3* pos)
{
    xBoundUpdate(&bound);
    zGridUpdateEnt(this);
}

void zNMETiki::FindParents(zScene* zsc)
{
    xRay3 ray;
    xCollis c = { k_HIT_0x100,
                  0,
                  NULL,
                  NULL,
                  0.0f,
                  0.0f,
                  { 0.0f, 0.0f, 0.0f },
                  { 0.0f, 0.0f, 0.0f },
                  { 0.0f, 0.0f, 0.0f },
                  { 0.0f, 0.0f, 0.0f },
                  { 0.0f, 0.0f, 0.0f } };
    xSurface* surf;
    zSurfaceProps* prop;
    F32 oldLower;
    F32 oldUpper;

    xVec3Copy(&ray.origin, (xVec3*)&model->Mat->pos);
    ray.origin.y = bound.box.box.upper.y;
    ray.dir.x = 0.0f;
    ray.dir.y = -1.0f;
    ray.dir.z = 0.0f;
    ray.max_t = 300.0f;
    ray.min_t = 0.0f;
    ray.flags = 0xc00;

    tikiFlag &= ~0x2;
    xRayHitsTikiLandableScene(globals.sceneCur, &ray, &c);

    if (c.flags & k_HIT_IT)
    {
        if (c.optr != NULL)
        {
            nonTikiParent = (xEnt*)c.optr;

            // not in the DWARF: the PS2 build keeps the offset in registers (its xVec3Dot calls
            // are inlined there).
            xVec3 temp;
            xVec3Sub(&temp, &ray.origin, (xVec3*)&((xEnt*)c.optr)->model->Mat->pos);
            temp.y -= c.dist;

            if (zGooIs(nonTikiParent))
            {
                tikiFlag |= 0x2;
            }

            if (nonTikiParent->miscflags & 0x20)
            {
                tikiFlag |= 0x2;
            }

            surf = zSurfaceGetSurface(&c);
            if (surf != NULL && surf->state == 0 && surf->moprops != NULL)
            {
                prop = (zSurfaceProps*)surf->moprops;
                if (prop->asset != NULL && prop->asset->game_damage_type != 0)
                {
                    tikiFlag |= 0x2;
                }
            }

            nonTikiParentDisp.x = xVec3Dot(&temp, (xVec3*)&nonTikiParent->model->Mat->right);
            nonTikiParentDisp.y = xVec3Dot(&temp, (xVec3*)&nonTikiParent->model->Mat->up);
            nonTikiParentDisp.z = xVec3Dot(&temp, (xVec3*)&nonTikiParent->model->Mat->at);

            // not in the DWARF: the squared scale of the parent's matrix.
            F32 mag2 = xVec3Length2((xVec3*)&nonTikiParent->model->Mat->right);
            if (mag2 > 0.00001f)
            {
                xVec3SMulBy(&nonTikiParentDisp, 1.0f / mag2);
            }
        }
        else
        {
            nonTikiParent = NULL;
            xVec3Init(&nonTikiParentDisp, 0.0f, 0.0f, 0.0f);
        }

        landHt = ray.origin.y;
        landHt -= c.dist;
    }
    else
    {
        landHt = FLOAT_MIN;
    }

    if (tikiFlag & 0x20)
    {
        oldLower = bound.box.box.lower.y;
        oldUpper = bound.box.box.upper.y;

        bound.box.box.lower.y = oldLower / 2;
        bound.box.box.lower.y += 0.5f * bound.box.box.upper.y;
        bound.box.box.upper.y = bound.box.box.lower.y + oldUpper - oldLower;
    }

    for (S32 i = 0; i < zsc->num_npcs; i++)
    {
        zNMECommon* npc = (zNMECommon*)zsc->npcs[i];
        if (npc == this)
        {
            continue;
        }

        en_npctyp type = npc->SelfType();
        if (type < NME_TYPE_TIKI || type > NME_TYPE_TIKI_STONE)
        {
            continue;
        }

        zNMETiki* tiki = (zNMETiki*)npc;
        if (tiki->tikiFlag & 0x600)
        {
            continue;
        }

        if (tiki->tikiFlag & 0x10)
        {
            continue;
        }

        if (tiki->bound.box.box.upper.y < landHt)
        {
            continue;
        }

        if (bound.box.box.upper.x > tiki->bound.box.box.lower.x + 0.1f &&
            tiki->bound.box.box.upper.x > bound.box.box.lower.x + 0.1f &&
            bound.box.box.upper.z > tiki->bound.box.box.lower.z + 0.1f &&
            tiki->bound.box.box.upper.z > bound.box.box.lower.z + 0.1f &&
            0.75f * tiki->bound.box.box.upper.y + 0.25f * tiki->bound.box.box.lower.y <
                bound.box.box.lower.y + 0.00001f &&
            tiki->bound.box.box.upper.y <
                0.75f * bound.box.box.lower.y + 0.25f * bound.box.box.upper.y + 0.00001f)
        {
            U8 couldBe = TRUE;
            U8 i;

            for (i = 0; i < 4; i++)
            {
                if (tiki == parents[i])
                {
                    couldBe = FALSE;
                    break;
                }

                if (parents[i] != NULL &&
                    parents[i]->bound.box.box.upper.x > tiki->bound.box.box.lower.x + 0.1f &&
                    tiki->bound.box.box.upper.x > parents[i]->bound.box.box.lower.x + 0.1f &&
                    parents[i]->bound.box.box.upper.z > tiki->bound.box.box.lower.z + 0.1f &&
                    tiki->bound.box.box.upper.z > parents[i]->bound.box.box.lower.z + 0.1f)
                {
                    if (0.75f * parents[i]->bound.box.box.upper.y +
                                0.25f * parents[i]->bound.box.box.lower.y <
                            tiki->bound.box.box.lower.y + 0.00001f &&
                        parents[i]->bound.box.box.upper.y <
                            0.75f * tiki->bound.box.box.lower.y +
                                0.25f * tiki->bound.box.box.upper.y + 0.00001f)
                    {
                        // This parent sits below the new one: drop it.
                        parents[i]->RemoveChild(this);
                        parents[i] = NULL;
                        numParents--;
                        if (i == contactParent)
                        {
                            contactParent = -1;
                            tikiFlag |= 0x1;
                        }
                    }
                    else if (0.75f * tiki->bound.box.box.upper.y +
                                     0.25f * tiki->bound.box.box.lower.y <
                                 parents[i]->bound.box.box.lower.y + 0.00001f &&
                             tiki->bound.box.box.upper.y <
                                 0.75f * parents[i]->bound.box.box.lower.y +
                                     0.25f * parents[i]->bound.box.box.upper.y + 0.00001f)
                    {
                        // The new one sits below an existing parent.
                        couldBe = FALSE;
                    }
                }
            }

            if (couldBe && tiki->numChildren < 4 && numParents < 4)
            {
                i = 0;
                while (parents[i] != NULL)
                {
                    i++;
                }
                parents[i] = tiki;
                tiki->AddChild(this);
                numParents++;
            }
        }
    }

    if (tikiFlag & 0x20)
    {
        bound.box.box.lower.y = oldLower;
        bound.box.box.upper.y = oldUpper;
        tikiFlag &= ~0x20;
    }

    if (numParents == 0)
    {
        tikiFlag |= 0x1;
    }
    else
    {
        tikiFlag &= ~0x2;
        nonTikiParent = NULL;
    }
}

// No GC symbol: inlined into its only caller (ProcSubContract) and dead-stripped.
void zNMETiki::HoverBubbles(F32 dt)
{
    if (itemType != NME_TYPE_TIKI_LOVEY)
    {
        return;
    }

    if ((tikiFlag & 0x600) == 0x400)
    {
        return;
    }

    if (XYZDstSqToPlayer(NULL) > 144.0f)
    {
        return;
    }

    hovdat.tmr_hovBubDelay = MAX(-1.0f, hovdat.tmr_hovBubDelay - dt);
    if (hovdat.tmr_hovBubDelay < 0.0f)
    {
        HoverBubStream(dt);

        hovdat.tmr_hovBubEmit = MAX(-1.0f, hovdat.tmr_hovBubEmit - dt);
        if (hovdat.tmr_hovBubEmit < 0.0f)
        {
            hovdat.tmr_hovBubDelay = 24.0f * (0.25f * (xurand() - 0.5f)) + 24.0f;
            hovdat.tmr_hovBubEmit = 0.5f;
        }
    }
}

namespace FX_HovBub
{
    const xVec3 vec_offset = { 0.0f, 0.0f, 0.0f };
    const xVec3 dir_orient = { 0.0f, -1.0f, 0.0f };
} // namespace FX_HovBub

// Compiled-out asserts (conditions unknown). With them this function is not auto-inlined, so
// ProcSubContract inlines HoverBubbles and calls this one, as in retail.
void zNMETiki::HoverBubStream(F32 dt)
{
    xVec3 pos_emit;
    xMat4x3* mat_root = (xMat4x3*)model->Mat;
    xVec3 dir_temp;
    F32 len;
    xVec3 dir_emit;

    pos_emit = FX_HovBub::vec_offset;
    xMat4x3Toworld(&pos_emit, mat_root, &pos_emit);

    dir_temp = FX_HovBub::dir_orient;
    len = xVec3Length(&dir_temp);
    if (len > 0.00001f)
    {
        xVec3SMulBy(&dir_temp, 1.0f / len);
    }
    else
    {
        dir_temp = g_Z3;
    }

    dir_emit = dir_temp;
    xMat3x3RMulVec(&dir_emit, mat_root, &dir_emit);

    HoverBubCone(&pos_emit, &dir_emit, 2, 0.45f, 0.05f, 3.0f, 0.25f, 0.55f);

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
}

void zNMETiki::HoverBubCone(xVec3* pos_emitBase, xVec3* dir_emit, S32 num_bub, F32 rad_cone,
                            F32 rad_emitJitter, F32 spd_emitBase, F32 spd_emitRand, F32 scl_bub)
{
    xVec3* buffer;
    xVec3 dir_axis;
    xVec3 dir_perp;
    xVec3 dir_orth;
    F32 darc;
    xVec3* buf_vel;
    xVec3 pos_base;
    S32 i;
    F32 ang_curr;
    F32 rad_jitr;
    F32 rad_curr;
    xVec3 dir_dest;
    F32 spd_emit;

    if (num_bub < 1)
    {
        return;
    }

    buffer = (xVec3*)xMemPushTemp(num_bub * sizeof(xVec3) * 2);
    if (buffer == NULL)
    {
        return;
    }

    dir_axis = *dir_emit;

    dir_perp.x = dir_emit->y - dir_emit->z;
    dir_perp.y = dir_emit->z - dir_emit->x;
    dir_perp.z = dir_emit->x - dir_emit->y;
    xVec3Normalize(&dir_perp, &dir_perp);

    xVec3Cross(&dir_orth, &dir_perp, dir_emit);

    darc = 2 * PI / num_bub;
    buf_vel = buffer + num_bub;
    pos_base = *pos_emitBase;

    for (i = 0; i < num_bub; i++)
    {
        ang_curr = -PI + darc * (i + xurand());
        F32 cos_ang = icos(ang_curr); // not in the DWARF: icos/isin are called once and reused
        F32 sin_ang = isin(ang_curr); // not in the DWARF (see cos_ang)

        rad_jitr = rad_emitJitter * xurand();
        buffer[i] = pos_base;
        buffer[i] += dir_perp * (cos_ang * rad_jitr);
        buffer[i] += dir_orth * (sin_ang * rad_jitr);

        rad_curr = rad_cone * xurand();
        dir_dest = dir_axis;
        dir_dest += dir_perp * (cos_ang * rad_curr);
        dir_dest += dir_orth * (sin_ang * rad_curr);
        dir_dest.normalize();

        spd_emit = spd_emitBase + spd_emitRand * xurand();
        buf_vel[i] = dir_dest * spd_emit;
    }

    zParPTankSpawnBubbles(buffer, buf_vel, MAX(0, num_bub), scl_bub);
    xMemPopTemp(buffer);
}

// No GC symbol (always inlined into the lovey tiki callbacks); the name is from BfBB.
static inline void loveyFloat(zNMETiki* tiki, F32 dt)
{
    xVec3 up_vec;

    xVec3Init(&up_vec, 0.0f, 1.0f, 0.0f);

    F32 amplitude = icos(tiki->t1 * 4.0f) * 0.2f;

    xVec3Copy((xVec3*)&tiki->model->Mat->pos, &tiki->v1);
    xVec3AddScaled((xVec3*)&tiki->model->Mat->pos, &up_vec, amplitude);

    tiki->t1 += dt;
    if (tiki->t1 > 6.2831855f)
    {
        tiki->t1 -= 6.2831855f;
    }

    tiki->t2 += 0.25f;
    if (tiki->t2 > 0.2501f)
    {
        tiki->t2 = -0.25f;
        tiki->t3 += 0.25f;
        if (tiki->t3 > 0.2501f)
        {
            tiki->t3 = -0.25f;
        }
    }
}

static S32 loveyIdleCB(xGoal* rawgoal, void*, en_trantype* trantype, F32 dt, void*)
{
    zNMEGoalTikiIdle* goal = (zNMEGoalTikiIdle*)rawgoal;
    zNMETiki* tiki = (zNMETiki*)rawgoal->GetOwner();
    S32 nextgoal = 0;
    zNMENavNet* navnet = tiki->navnet;

    if (navnet->nav_curr != NULL)
    {
        goal->tmr_wait = MAX(-1.0f, goal->tmr_wait - dt);

        if (goal->tmr_wait < 0.0f || !navnet->nav_curr->on)
        {
            navnet->MvptCycle();

            if (navnet->nav_dest != NULL)
            {
                *trantype = GOAL_TRAN_SET;
                nextgoal = NME_GOAL_TIKI_PATROL;
            }
            else
            {
                goal->tmr_wait = zMovePointGetDelay(navnet->nav_curr);
            }
        }
    }

    loveyFloat(tiki, dt);

    return nextgoal;
}

static S32 loveyPatrolCB(xGoal* rawgoal, void*, en_trantype* trantype, F32 dt, void*)
{
    zNMEGoalTikiPatrol* goal = (zNMEGoalTikiPatrol*)rawgoal;
    zNMETiki* tiki = (zNMETiki*)rawgoal->GetOwner();
    S32 nextgoal = 0;
    xVec3 delta; // not in the PS2 DWARF

    xVec3AddScaled(&tiki->v1, &goal->vel, dt);
    xVec3Sub(&delta, &goal->dest_pos, &tiki->v1);

    if (xVec3Dot(&delta, &goal->vel) < 0.0f)
    {
        xVec3Copy(&tiki->v1, &goal->dest_pos);
        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TIKI_IDLE;
    }

    loveyFloat(tiki, dt);

    return nextgoal;
}

// Retail keeps the multiply by 1.0f in the growth step, as in quietHideCB (see there).
static S32 quietIdleCB(xGoal* rawgoal, void*, en_trantype* trantype, F32 dt, void*)
{
    zNMETiki* tiki = (zNMETiki*)rawgoal->GetOwner();
    S32 nextgoal = 0;
    F32 scale;
    F32 scl_full = 1.0f; // not in the DWARF: constant-propagated (see quietHideCB)

    scale = xVec3Dist2((xVec3*)&tiki->model->Mat->pos,
                       (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos);

    if (scale < 25.0f)
    {
        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TIKI_HIDE;

        tiki->SndPlayFromTable(8); // eNMESound_MervZapperLaserL

        if (tiki->snd_table->sndGroupHandle[9] != -1)
        {
            xSndMgrStop(tiki->sndID[9]);
            tiki->sndID[9] = (iSndHandle)-1;
        }
    }
    else
    {
        if (scale < 50.0f)
        {
            tiki->tikiFlag &= ~0x180;
        }

        if (tiki->tikiFlag & 0x40)
        {
            scale = 0.833f * dt * scl_full + tiki->model->Scale.x;

            if (!xEntIsVisible(tiki))
            {
                xScreenWarp::emit(*xEntGetPos(tiki), 2.0f, 0.5f, xScreenWarp::TYPE_SHOCKWAVE, 1.0f,
                                  1.0f, 0);
                xEntShow(tiki);
            }

            tiki->RestoreColFlags();

            if (scale > 1.0f)
            {
                tiki->tikiFlag &= ~0x40;
                xModelSetScale(tiki->model, 0.0f);

                if (tiki->snd_table->sndGroupHandle[9] != -1)
                {
                    xSndMgrStop(tiki->sndID[9]);
                    tiki->sndID[9] = (iSndHandle)-1;
                }
            }
            else
            {
                xModelSetScale(tiki->model, scale);
            }
        }
    }

    return nextgoal;
}

// Retail keeps the multiply by 1.0f in 'Scale.x - (2.0f * dt) * 1.0f', so the 1.0f is a propagated
// value, not a literal (which would be folded away): a named full-scale local.
static S32 quietHideCB(xGoal* rawgoal, void*, en_trantype* trantype, F32 dt, void*)
{
    zNMETiki* tiki = (zNMETiki*)rawgoal->GetOwner();
    S32 nextgoal = 0;
    F32 scale;
    F32 scl_full = 1.0f; // not in the DWARF: constant-propagated (see above)

    if (xVec3Dist2((xVec3*)&tiki->model->Mat->pos,
                   (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos) > 25.0f)
    {
        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TIKI_IDLE;

        tiki->SndPlayFromTable(9);

        if (tiki->snd_table->sndGroupHandle[8] != -1)
        {
            xSndMgrStop(tiki->sndID[8]);
            tiki->sndID[8] = (iSndHandle)-1;
        }
    }
    else
    {
        if (!(tiki->tikiFlag & 0x40))
        {
            tiki->tikiFlag |= 0x40;
            xModelSetScale(tiki->model, scl_full);
            xScreenWarp::emit(*(xVec3*)&tiki->model->Mat->pos, 2.0f, 0.5f,
                              xScreenWarp::TYPE_SHOCKWAVE, 1.0f, 1.0f, 0);
        }

        scale = tiki->model->Scale.x - 2.0f * dt * scl_full;
        if (scale < 0.00001f)
        {
            scale = 0.00001f;
            xEntHide(tiki);
            tiki->RestoreColFlags();

            if (tiki->snd_table->sndGroupHandle[8] != -1)
            {
                xSndMgrStop(tiki->sndID[8]);
                tiki->sndID[8] = (iSndHandle)-1;
            }
        }

        xModelSetScale(tiki->model, scale);
    }

    return nextgoal;
}

static S32 thunderIdleCB(xGoal* rawgoal, void*, en_trantype* trantype, F32 dt, void*)
{
    zNMEGoalTikiIdle* goal = (zNMEGoalTikiIdle*)rawgoal;
    zNMETiki* tiki = (zNMETiki*)rawgoal->GetOwner();
    F32 factor;

    tiki->t1 -= 0.75f * dt;
    if (tiki->t1 < 0.0f)
    {
        tiki->t1 += 1.0f;
    }

    factor = (tiki->t1 * tiki->t1 + 1.0f) - tiki->t1;
    tiki->model->GreenMultiplier = tiki->model->BlueMultiplier = tiki->model->RedMultiplier =
        SMOOTH(factor, FX_ThunGlow::fac_color[0], FX_ThunGlow::fac_color[1]);
    tiki->model->Alpha = SMOOTH(factor, FX_ThunGlow::alf_range[0], FX_ThunGlow::alf_range[1]);

    goal->tmr_wait -= dt;
    if (goal->tmr_wait < 0.0f)
    {
        if (NMEC_DstSq(xEntGetCenter(xglobals->___player_ent_dont_use_directly),
                       xEntGetCenter(tiki), NULL) < SQ(1.4f))
        {
            if (globals.player.SlideTrackSliding & 1)
            {
                tiki->Damage(DMGTYP_SIDE, NULL, NULL, -1, ePowerupLevelNormal);
                return 0;
            }

            tiki->t1 = 0.0f;
            tiki->SndPlayFromTable(8); // eNMESound_MervZapperLaserL
            *trantype = GOAL_TRAN_SET;
            return NME_GOAL_TIKI_COUNT;
        }
    }

    return 0;
}

// No GC or PS2 symbol (always inlined); the name is ours. Used by the sparks and smoke halves of
// EmitFuseFX: finds the fuse bone the burn point is on, its tip offset and the fraction along that
// segment. The span always comes from tym_walkSparks, even for the smoke walk (as in retail).
static inline S32 FuseWalk(F32 rat, F32 tym_total, const F32* tym_walk, xVec3* tipoff, F32* frac)
{
    F32 pam;
    S32 i;
    S32 idx;
    F32 tym_prev;
    F32 tym_span;
    F32 lerp;

    pam = CLAMP(rat, 0.0f, 1.0f);
    idx = -1;
    tym_prev = 0.0f;
    tym_span = 1.0f;

    for (i = 0; i < 4; i++)
    {
        if (pam > tym_walk[i])
        {
            tym_prev = tym_walk[i];
        }
        else
        {
            idx = FX_LitFuse::idx_fuseBones[i];
            *tipoff = FX_LitFuse::vec_tipoff[i];
            tym_span = MIN(1.0f, FX_LitFuse::tym_walkSparks[i]) - tym_prev;
            break;
        }
    }

    lerp = pam * tym_total - tym_prev;
    lerp = CLAMP(lerp, 0.0f, tym_span);
    // Stored through frac in both branches and clamped in place; a '?:' allocates registers
    // differently from retail.
    if (tym_span < 1e-5f)
    {
        *frac = 0.0f;
    }
    else
    {
        *frac = lerp / tym_span;
    }
    *frac = CLAMP(*frac, 0.0f, 1.0f);

    return idx;
}

// No PS2 symbol (inlined into thunderCountCB there); weak on GC.
inline void EmitFuseFX(zNMETiki* tiki, F32 dt, F32 rat, F32 tym_total)
{
    RwMatrix* mat = tiki->model->Mat;
    xVec3 tipoff;
    S32 idx;
    F32 frac;

    idx = FuseWalk(rat, tym_total, FX_LitFuse::tym_walkSparks, &tipoff, &frac);

    if (idx > 0)
    {
        RwMatrix* bone = &tiki->model->Mat[idx];
        xVec3 pos;
        xVec3 dir;

        if (idx == 2)
        {
            pos = tipoff;
        }
        else
        {
            pos.x = LERP(frac, g_O3.x, tipoff.x);
            pos.y = LERP(frac, g_O3.y, tipoff.y);
            pos.z = LERP(frac, g_O3.z, tipoff.z);
        }

        xMat4x3Toworld(&pos, (xMat4x3*)bone, &pos);
        xMat4x3Toworld(&pos, (xMat4x3*)mat, &pos);

        dir = *(xVec3*)&bone->at;
        xMat3x3RMulVec(&dir, (xMat3x3*)mat, &dir);

        FX_LitFuse::Sparks::sparks.EmitRing(&pos, &dir, -100000.0f, 1.0f, -1);
        zFX_SpawnBubbleTrail(&pos, 1);
    }

    idx = FuseWalk(rat, tym_total, FX_LitFuse::tym_walkSmoke, &tipoff, &frac);

    if (idx > 0)
    {
        RwMatrix* bone = &tiki->model->Mat[idx];
        xVec3 pos;

        if (idx == 2)
        {
            pos = tipoff;
        }
        else
        {
            pos.x = LERP(frac, g_O3.x, tipoff.x);
            pos.y = LERP(frac, g_O3.y, tipoff.y);
            pos.z = LERP(frac, g_O3.z, tipoff.z);
        }

        xMat4x3Toworld(&pos, (xMat4x3*)bone, &pos);
        xMat4x3Toworld(&pos, (xMat4x3*)mat, &pos);

        FX_LitFuse::Smoke::smoke.EmitRing(&pos, &g_Y3, -100000.0f, 1.0f, -1, NULL);
    }
}

static S32 thunderCountCB(xGoal* rawgoal, void*, en_trantype* trantype, F32 dt, void*)
{
    zNMEGoalTikiCount* goal = (zNMEGoalTikiCount*)rawgoal;
    zNMETiki* tiki = (zNMETiki*)rawgoal->GetOwner();
    S32 nextgoal = 0;
    F32 tym_total;
    F32 rat;
    F32 cycle;
    F32 bounce;

    goal->tmr_count = MAX(-1.0f, goal->tmr_count - dt);

    if (goal->tmr_count < 0.0f)
    {
        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TIKI_DYING;
    }

    if (!goal->beingCarried && globals.player.carry.grabbed == tiki)
    {
        goal->beingCarried = 1;
        goal->tmr_count += 3.0f;
        goal->tmr_count = CLAMP(goal->tmr_count, 0.0f, 6.0f);
    }

    tiki->tikiFlag &= ~0x180;

    if (goal->beingCarried)
    {
        tym_total = 6.0f;
    }
    else
    {
        tym_total = 3.0f;
    }
    rat = CLAMP(goal->tmr_count / tym_total, 0.0f, 1.0f);
    cycle = xDangleClamp(7.0f * (PI * rat));
    bounce = xabs(icos(cycle));

    tiki->model->GreenMultiplier = tiki->model->BlueMultiplier = tiki->model->RedMultiplier =
        (FX_ThunGlow::fac_color[1] - FX_ThunGlow::fac_color[0]) * EASE(bounce) +
        FX_ThunGlow::fac_color[0];
    tiki->model->Alpha = (FX_ThunGlow::alf_range[1] - FX_ThunGlow::alf_range[0]) * EASE(bounce) +
                         FX_ThunGlow::alf_range[0];

    EmitFuseFX(tiki, dt, rat, tym_total);

    return nextgoal;
}

static S32 tikiDyingCB(xGoal* rawgoal, void*, en_trantype* trantype, F32 dt, void*)
{
    rawgoal->GetOwner();
    zNMEGoalTikiDying* goal = (zNMEGoalTikiDying*)rawgoal;
    S32 nextgoal = 0;

    goal->tmr_dying = (-1.0f > goal->tmr_dying - dt) ? -1.0f : goal->tmr_dying - dt;

    if (goal->tmr_dying < 0.0f)
    {
        *trantype = GOAL_TRAN_SET;
        nextgoal = NME_GOAL_TIKI_DEAD;
    }

    return nextgoal;
}

static S32 tikiDeadCB(xGoal* rawgoal, void*, en_trantype*, F32, void*)
{
    rawgoal->GetOwner();
    return 0;
}

static void genericTikiRender(xEnt* ent)
{
    F32 dot;
    xMat4x3 camMat;

    ent->_isCulled = 0;

    if (!ent->model || !xEntIsVisible(ent))
    {
        ent->_isCulled = 1;
        return;
    }

    dot = 2.0f;

    if ((globals.player.ControlOff & 0x23f3) == 0)
    {
        xVec3 tmp; // not in the PS2 DWARF

        camMat = zCamGetMatrix();
        xVec3Sub(&tmp, &ent->bound.box.center, &camMat.pos);
        dot = xVec3Dot(&tmp, &camMat.at);
    }

    if (dot < 0.0f)
    {
        ent->_isCulled = 1;
    }

    if (dot < 1.5f)
    {
        ent->model->Alpha -= 3.0f * globals.update_dt;
    }
    else
    {
        ent->model->Alpha += 3.0f * globals.update_dt;
        if (ent->model->Alpha > 1.0f)
        {
            ent->model->Alpha = 1.0f;
        }
    }

    if (ent->model->Alpha < 0.0f)
    {
        ent->model->Alpha = 0.0f;
        return;
    }

    if (((zNMETiki*)ent)->rigidAnim)
    {
        ((zNMETiki*)ent)->flg_xtrarend = 1;
        ((zNMETiki*)ent)->rigidAnim[1] = *(xMat4x3*)ent->model->Mat;
        xMat4x3Mul((xMat4x3*)ent->model->Mat, ((zNMETiki*)ent)->rigidAnim,
                   (xMat4x3*)ent->model->Mat);
    }

    xModelRender(ent->model);
}

static void loveyTikiRender(xEnt* ent)
{
    xModelInstance* model = ent->model;
    F32 factor;

    if (model == NULL || !xEntIsVisible(ent) || model->Flags & 0x400)
    {
        return;
    }

    factor = 1.0f - TikiXZDstSq((xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos,
                                (xVec3*)&model->Mat->pos) /
                        25.0f;
    if (gCurrentPlayer != eCurrentPlayerSpongeBob)
    {
        factor = -1.0f;
    }

    xShadowCache cache;
    cache.polyCount = 0;

    if (factor > 0.0f)
    {
        if (factor > 1.0f)
        {
            factor = 1.0f;
        }

        xVec3 center; // not in the PS2 DWARF

        xVec3Copy(&center, (xVec3*)&ent->model->Mat->pos);
        center.y += sLoveyIconOffset;

        static S32 doFancyShadow = 1;
        if (doFancyShadow)
        {
            NMEC_RenderProjTextureFaceCamera(sHelmetRast, factor, &center, 0.7f,
                                             sLoveyIconDist + sLoveyIconOffset, &cache, 1, ent);
        }
    }

    ent->_isCulled = 0;

    S32 shadowResult = 0;
    S32 alphaTooLow = 0;
    xVec3 shadVec;
    shadVec.x = model->Mat->pos.x;
    shadVec.y = model->Mat->pos.y - 10.0f;
    shadVec.z = model->Mat->pos.z;

    if (!xModelSphereCullPlusShadow(iModelWorldBoundSphere(model->Data, model->Mat, NULL), &shadVec,
                                    &shadowResult))
    {
        F32 dot = 2.0f;
        xMat4x3 camMat;

        if ((globals.player.ControlOff & 0x23f3) == 0)
        {
            xVec3 tmp; // not in the PS2 DWARF

            camMat = zCamGetMatrix();
            xVec3Sub(&tmp, &ent->bound.box.center, &camMat.pos);
            dot = xVec3Dot(&tmp, &camMat.at);
        }

        if (dot < 1.5f)
        {
            model->Alpha -= 3.0f * globals.update_dt;
            if (model->Alpha < 0.0f)
            {
                model->Alpha = 0.0f;
                alphaTooLow = 1;
            }
        }
        else
        {
            model->Alpha += 3.0f * globals.update_dt;
            if (model->Alpha > 1.0f)
            {
                model->Alpha = 1.0f;
            }
        }

        for (xModelInstance* curr = model->Next; curr != NULL; curr = curr->Next)
        {
            curr->Alpha = model->Alpha;
        }

        if (alphaTooLow)
        {
            return;
        }

        xModelRender(model);
    }
    else
    {
        ent->_isCulled = 1;
    }

    if ((factor < 0.5f || cache.polyCount == 0) && shadowResult == 0 && ent->simpShadow != NULL)
    {
        static S32 doSimpleShadow = 1;
        if (doSimpleShadow)
        {
            xShadowSimple_Add(ent->simpShadow, ent, 0.667f, 1.0f);
        }
    }
}

S32 zNMEGoalTikiIdle::Enter(F32 dt, void* updCtxt)
{
    zNMETiki* tiki = (zNMETiki*)GetOwner();

    // Retail bug carried over from BfBB: this compares against the BfBB NPC type
    // NPC_TYPE_TIKI_THUNDER ('NTT3') instead of NME_TYPE_TIKI_THUNDER, so it is never true.
    if (tiki->itemType == 'NTT3')
    {
        tmr_wait = 0.1f;
    }
    else if (!tiki->navnet)
    {
        tmr_wait = 2.0f;
    }
    else if (!tiki->navnet->nav_dest)
    {
        tmr_wait = 2.0f;
    }
    else
    {
        tmr_wait = zMovePointGetDelay(tiki->navnet->nav_dest);
    }

    return 0;
}

S32 zNMEGoalTikiPatrol::Enter(F32 dt, void* updCtxt)
{
    zNMETiki* tiki = (zNMETiki*)GetOwner();

    xVec3Copy(&dest_pos, zMovePointGetPos(tiki->navnet->nav_dest));
    xVec3Sub(&vel, &dest_pos, &tiki->v1);
    xVec3Normalize(&vel, &vel);
    xVec3SMulBy(&vel, 1.0f);

    return 0;
}

S32 zNMEGoalTikiHide::Enter(F32 dt, void* updCtxt)
{
    zNMETiki* tiki = (zNMETiki*)GetOwner();

    tiki->flg_vuln = 0;

    return 0;
}

S32 zNMEGoalTikiHide::Exit(F32 dt, void* updCtxt)
{
    zNMETiki* tiki = (zNMETiki*)GetOwner();

    tiki->flg_vuln = tiki->DfltVulnFlags();

    return 0;
}

S32 zNMEGoalTikiCount::Enter(F32 dt, void* updCtxt)
{
    zNMETiki* tiki = (zNMETiki*)GetOwner();

    tmr_count = 3.0f;
    beingCarried = 0;

    tiki->t1 = 0.0f;

    return 0;
}

S32 zNMEGoalTikiDying::Enter(F32 dt, void* updCtxt)
{
    zNMETiki* tiki = (zNMETiki*)GetOwner();

    tmr_dying = tiki->timeToLive;
    tiki->flg_vuln = 0;

    if (globals.player.carry.grabbed == tiki)
    {
        globals.player.carry.grabbed = NULL;
    }

    // The retail code stops sound slot 8 twice.
    if (tiki->snd_table->sndGroupHandle[8] != -1)
    {
        xSndMgrStop(tiki->sndID[8]);
        tiki->sndID[8] = (iSndHandle)-1;
    }
    if (tiki->snd_table->sndGroupHandle[8] != -1)
    {
        xSndMgrStop(tiki->sndID[8]);
        tiki->sndID[8] = (iSndHandle)-1;
    }
    if (tiki->snd_table->sndGroupHandle[9] != -1)
    {
        xSndMgrStop(tiki->sndID[9]);
        tiki->sndID[9] = (iSndHandle)-1;
    }

    return 0;
}

S32 zNMEGoalTikiDying::Exit(F32 dt, void* updCtxt)
{
    zNMETiki* tiki = (zNMETiki*)GetOwner();

    zNMETiki_ExplodeFX(tiki);

    // not in the DWARF (only tiki and vel): holds the retrieved asset across the NULL test
    zShrapnelAsset* shrap = zNMEShrapnelRetrieveShrapnel(tiki->SelfType());
    if (shrap != NULL)
    {
        xVec3 vel = {};
        shrap->initCB(shrap, tiki->model, &vel, NULL);
    }

    zNMERumblePlay(tiki);

    return 0;
}

S32 zNMEGoalTikiDead::Enter(F32 dt, void* updCtxt)
{
    zNMETiki* tiki = (zNMETiki*)GetOwner();

    tiki->tikiFlag &= ~0x600;
    tiki->tikiFlag |= 0x200;

    xEntHide(tiki);

    tiki->chkby = XENT_COLLTYPE_NONE;
    tiki->penby = XENT_COLLTYPE_NONE;

    tiki->GiveReward();

    zEntEvent(tiki->id, eEventDeath);

    return 0;
}

S32 zNMEGoalTikiDead::Exit(F32 dt, void* updCtxt)
{
    zNMETiki* tiki = (zNMETiki*)GetOwner();

    xEntShow(tiki);

    tiki->RestoreColFlags();

    tiki->flg_vuln = tiki->DfltVulnFlags();

    return 0;
}
