#include "zNMECommon.h"
#include "zNMETypeBossDenToo.h"
#include "zNMETypeTiki.h"
#include "zNMEGoalCommon.h"
#include "zNMEMessenger.h"
#include "zNPCMgr.h"
#include "zGlobals.h"
#include "zRewards.h"
#include "xordarray.h"
#include "zNMESupport.h"
#include "zNMEShrapnel.h"
#include "zNMERumble.h"
#include "zNMEZapGun.h"
#include "zSurface.h"
#include "zGoo.h"
#include "zUICustom.h"
#include "zTalkBox.h"
#include "xstransvc.h"
#include "xScreenFade.h"
#include "zEntTeleportBox.h"

#include <string.h>

// The PS2 DWARF lists Common::Config's data first in this file (before NME::GlobalData).
namespace Common
{
    namespace Config
    {
        static const NMECfgCommon cfg_common = {
            { 0.5f },
            { 10.0f, -5.0f },
            { 1.0f, 3.0f, 0.1f, PI },
        };
        static NMEShadParms cfg_shadparm_common = { { 2.0f, "shadow_round", NULL },
                                                    { 0 },
                                                    { 35.0f, 20.0f } };
    } // namespace Config
} // namespace Common

namespace NME
{
    namespace GlobalData
    {
        static S32 isConversation;
        WAD_STATIC S32 flg_wonder;
        static S32 flg_doBeDifferent;
    } // namespace GlobalData

    namespace Shadowing
    {
        namespace EntShad
        {
            static const xVec3 vec_offset = { 0.0f, 1.4f, 0.0f };
            static const xVec3 dir_theSunzRays = { 0.25f, -0.93541437f, 0.25f };
        } // namespace EntShad
    } // namespace Shadowing

    namespace DamageTable
    {
        // Damage dealt by each player attack, per player powerup level.
        static const S32 dmg_table[PLYR_ATAK_NOMORE][ePowerupLevelCount] = {
            { 1, 1, 1, 0 }, // PLYR_ATAK_NONE
            { 0, 1, 2, 0 }, // PLYR_ATAK_SB_KARATE
            { 0, 2, 5, 0 }, // PLYR_ATAK_SB_BASH
            { 0, 3, 7, 0 }, // PLYR_ATAK_SB_BOWL
            { 0, 4, 8, 0 }, // PLYR_ATAK_SB_GUITAR
            { 0, 1, 3, 0 }, // PLYR_ATAK_PAT_BELLY
            { 0, 2, 4, 0 }, // PLYR_ATAK_PAT_CARTWHEEL
            { 0, 3, 6, 0 }, // PLYR_ATAK_PAT_FLOP
            { 0, 4, 8, 0 }, // PLYR_ATAK_PAT_THROW
            { 100, 100, 100, 0 }, // PLYR_ATAK_SLIDE
        };
    } // namespace DamageTable

    // No GC symbol: inlined into its only caller, zMODEnemy::ScenePrepare. Only the tweak names are
    // built; the debug tweak registrations compile to nothing here.
    static void AddTweaks(char* prefix_root)
    {
        char prefix[128];
        char prefix_base[128];
        char strbuf[128];
        S32 i;
        // not in the DWARF: j, the attack index of the damage-table loop below
        S32 j;

        sprintf(prefix_base, "%s|%s", prefix_root, "Common");

        sprintf(prefix, "%s|%s|", prefix_base, "Universal");
        sprintf(prefix, "%s|%s|", prefix_base, "Profile");
        sprintf(prefix, "%s|%s|", prefix_base, "Profile|Timestep");
        sprintf(prefix, "%s|%s|", prefix_base, "Profile|Rendering");
        sprintf(prefix, "%s|%s|", prefix_base, "Profile|ExtraRendering");
        sprintf(prefix, "%s|%s|", prefix_base, "Collision");
        sprintf(prefix, "%s|%s|", prefix_base, "Animation");
        sprintf(prefix, "%s|%s|", prefix_base, "Shadows");
        sprintf(prefix, "%s|%s|", prefix_base, "Shadows|Simple");
        sprintf(prefix, "%s|%s|", prefix_base, "Shadows|EntShadow");
        sprintf(prefix, "%s|%s|", prefix_base, "Effects|DeathBubbles");

        sprintf(strbuf, "%s|%s", prefix_base, "DamageTable");
        for (i = 1; i < 4; i++)
        {
            sprintf(prefix, "%s|%s %d|", strbuf, "Level", i);

            // Reconstructed: the per-attack tweaks of DamageTable::dmg_table[j][i], whose
            // registration is compiled out. The empty loop still leaves j's 'li 1' that retail's
            // ScenePrepare copies for its constant flag stores.
            for (j = 1; j < PLYR_ATAK_NOMORE; j++)
            {
            }
        }
    }
} // namespace NME

// Fogger::Cheat data of zNMETypeStdFogger.cpp (defined static there, later in the wad).
namespace Fogger
{
    namespace Cheat
    {
        extern const xVec3 scl_cheat;
    } // namespace Cheat
} // namespace Fogger

extern zNMESoundTable* gzSoundTableList[58];

static zMODEnemy* g_mod_enemy;

// No GC symbol (always inlined); the name is ours. Is any enemy of this type in the scene? The
// count is re-read in the loop condition and the type comes from the SelfType() accessor, as
// retail's registers need.
inline S32 zNMECommon_TypeInScene(en_npctyp ntyp)
{
    for (S32 i = 0; i < g_mod_enemy->nmelist.cnt; i++)
    {
        if (((zNMECommon*)g_mod_enemy->nmelist.list[i])->SelfType() == ntyp)
        {
            return 1;
        }
    }

    return 0;
}

// No GC symbol: inlined into zNMECentral_Startup (its unused copy is dead-stripped).
void ZNME_Common_Startup()
{
    static zMODEnemy mod_enemy;

    g_mod_enemy = &mod_enemy;
    zNMECentral_RegisterModule(&mod_enemy, 'NMEC');
    ZNME_Swimmer_Startup();
}

void zMODEnemy::ModulePrepUse()
{
    memset(&nmelist, 0, sizeof(nmelist));
}

// Constant flag stores as in the PS2 demo. Retail's flag value register comes from the inlined
// AddTweaks' attack loop; see there.
void zMODEnemy::ScenePrepare()
{
    memset(&nmelist, 0, sizeof(nmelist));

    NME::AddTweaks("NME");

    flg_doBackdoor = 1;
    flg_firstTimestep = 1;
}

void zMODEnemy::SceneFinish()
{
    // g_mod_config is zNMEConfig.cpp's module (same wad).
    g_mod_config->cfgFactory->DestroyAll();

    XOrdDone(&nmelist, 0);
}

static S32 NME_OrdComp_byType(void* vkey, void* vitem)
{
    en_npctyp nt_key = (en_npctyp)((zNMECommon*)vkey)->itemType;
    en_npctyp nt_item = (en_npctyp)((zNMECommon*)vitem)->itemType;

    return nt_key - nt_item;
}

void zMODEnemy::ScenePostInit()
{
    st_XORDEREDARRAY* reallist = zNPCMgr_GetNPCList();

    if (reallist->cnt > 0)
    {
        XOrdInit(&nmelist, reallist->cnt, 0);
        XOrdCopy(&nmelist, reallist);
        XOrdSort(&nmelist, NME_OrdComp_byType);
    }
    else
    {
        nmelist.cnt = 0;
        nmelist.max = 0;
    }

    en_npctyp typeNeedsHUD[5] = { NME_TYPE_FROGFISH, NME_TYPE_DENNIS_V1, NME_TYPE_DENNIS_V2,
                                  NME_TYPE_NEPTUNE, NME_TYPE_UNKNOWN };
    S32 num_nme;
    S32 i;
    zNMECommon* nme;

    flg_needHUDRender = 0;
    // 'nt' is not in the PS2 DWARF; reusing 'i' here also changes the regalloc of the last loop.
    for (en_npctyp* nt = typeNeedsHUD; *nt != NME_TYPE_UNKNOWN; nt++)
    {
        if (zNMECommon_TypeInScene(*nt))
        {
            flg_needHUDRender = 1;
            break;
        }
    }

    // Not in the PS2 DWARF: retail keeps this address in a register across the calls.
    NMEShadParms* shadparm = &Common::Config::cfg_shadparm_common;
    if (shadparm->simpshad.nam_shadowTexture == NULL)
    {
        shadparm->simpshad.rast_shadow = NULL;
    }
    else
    {
        shadparm->simpshad.rast_shadow = NMEC_FindRWRaster(shadparm->simpshad.nam_shadowTexture);
    }

    SetLightKit(globals.player.pPlayerLightKit);

    num_nme = nmelist.cnt;
    for (i = 0; i < num_nme; i++)
    {
        nme = (zNMECommon*)nmelist.list[i];
        nme->CreateConfig(NULL);
        nme->SelfSetup();
        nme->PostInit();
    }
}

void zMODEnemy::SceneSetup()
{
    S32 num_nme = nmelist.cnt;

    for (S32 i = 0; i < num_nme; i++)
    {
        ((zNMECommon*)nmelist.list[i])->Setup();
    }

    for (S32 i = 0; i < num_nme; i++)
    {
        ((zNMECommon*)nmelist.list[i])->Reset();
    }

    zNMETiki_InitStacking(globals.sceneCur);
    zNMETiki_InitFX(globals.sceneCur);
}

void zMODEnemy::ScenePostSetup()
{
    S32 num_nme = nmelist.cnt;

    for (S32 i = 0; i < num_nme; i++)
    {
        ((zNMECommon*)nmelist.list[i])->PostSetup();
    }
}

void zMODEnemy::SceneReset()
{
    S32 num_nme = nmelist.cnt;
    S32 i;

    for (i = 0; i < num_nme; i++)
    {
        ((zNMECommon*)nmelist.list[i])->Reset();
    }

    NME::GlobalData::isConversation = 0;
    NME::GlobalData::flg_wonder = 0;

    zNMETiki_InitStacking(globals.sceneCur);

    flg_doBackdoor = 1;
    flg_firstTimestep = 1;
}

void zMODEnemy::Timestep(F32 dt)
{
    S32 num_nme;
    S32 i;
    zNMECommon* nme;
    en_npctyp nt;

    if (flg_doBackdoor)
    {
        DoBackdoor();

        flg_doBackdoor = 0;
    }

    if (flg_firstTimestep)
    {
        SetLightKit(globals.player.pPlayerLightKit);
        flg_firstTimestep = 0;
    }

    dt = (dt <= (1.0f / 28.0f)) ? dt : (1.0f / 30.0f);

    SomethingWonderfulUpdate();

    if (NME::GlobalData::flg_doBeDifferent)
    {
        NME::GlobalData::flg_doBeDifferent = 0;
        tmr_doBeDifferent = 5.0f * (0.25f * (xurand() - 0.5f)) + 5.0f;
    }

    if (tmr_doBeDifferent < 0.0f)
    {
        NME::GlobalData::flg_doBeDifferent = 1;
    }

    tmr_doBeDifferent = MAX(-1.0f, tmr_doBeDifferent - dt);

    static F32 frank = xurand();
    static F32 tmr_lifecycle = 0.0f;
    static F32 val_phase = 0.0f;

    if (tmr_lifecycle < 0.0f)
    {
        tmr_lifecycle = 0.0f;
    }

    tmr_lifecycle += dt;
    if (tmr_lifecycle > 1.0f)
    {
        tmr_lifecycle = xfmod(tmr_lifecycle, 1.0f);
    }

    // Retail computes this and discards the result (frank and val_phase are never used either).
    isin(PI);

    num_nme = nmelist.cnt;
    for (i = 0; i < num_nme; i++)
    {
        nme = (zNMECommon*)nmelist.list[i];

        if (!(nme->baseFlags & 0x1) || (nme->baseFlags & 0x40))
        {
            continue;
        }
        if (nme->model != NULL && (nme->model->Flags & 0x400))
        {
            continue;
        }
        if (!nme->runtimeData.flg_activeOn)
        {
            continue;
        }

        nt = nme->SelfType();
        if (nt >= NME_TYPE_TIKI && nt <= NME_TYPE_TIKI_STONE)
        {
            nme->Process(dt);
        }
        else
        {
            nme->UpdateWrapper(dt);
        }

        if (nme->flg_newtime)
        {
            nme->NewTime(dt);
        }
    }

    zNMETiki_Timestep(dt);
}

void zMODEnemy::PostTimestep(F32 dt)
{
    S32 num_nme;
    S32 i;
    zNMECommon* nme;

    dt = (dt <= (1.0f / 28.0f)) ? dt : (1.0f / 30.0f);

    num_nme = nmelist.cnt;
    for (i = 0; i < num_nme; i++)
    {
        nme = (zNMECommon*)nmelist.list[i];

        if (!nme->flg_postproc)
        {
            continue;
        }
        if (!(nme->baseFlags & 0x1) || (nme->baseFlags & 0x40))
        {
            continue;
        }
        if (nme->model != NULL && (nme->model->Flags & 0x400))
        {
            continue;
        }
        if (!nme->runtimeData.flg_activeOn)
        {
            continue;
        }

        nme->PostProcess(dt);
    }

    ZapGun::NMEZapomatic::LaserUVScroll(dt);
}

void zMODEnemy::RenderToBuckets()
{
    S32 num_nme = nmelist.cnt;

    for (S32 i = 0; i < num_nme; i++)
    {
        zNMECommon* nme = (zNMECommon*)nmelist.list[i];

        if (!xEntIsVisible(nme))
        {
            continue;
        }
        if (!(nme->baseFlags & 0x1) || (nme->baseFlags & 0x40))
        {
            continue;
        }
        if (nme->model != NULL && (nme->model->Flags & 0x400))
        {
            continue;
        }
        if (!nme->runtimeData.flg_activeOn)
        {
            continue;
        }

        nme->Render();
    }
}

void zMODEnemy::PostRender()
{
    S32 num_nme = nmelist.cnt;

    for (S32 i = 0; i < num_nme; i++)
    {
        zNMECommon* nme = (zNMECommon*)nmelist.list[i];

        if (!nme->flg_xtrarend)
        {
            continue;
        }

        nme->flg_xtrarend = 0;

        if (!xEntIsVisible(nme))
        {
            continue;
        }
        if (!(nme->baseFlags & 0x1) || (nme->baseFlags & 0x40))
        {
            continue;
        }
        if (nme->model != NULL && (nme->model->Flags & 0x400))
        {
            continue;
        }
        if (!nme->runtimeData.flg_activeOn)
        {
            continue;
        }

        nme->RenderExtra();
    }
}

void zMODEnemy::HudRender()
{
    S32 num_nme = nmelist.cnt;

    for (S32 i = 0; i < num_nme; i++)
    {
        zNMECommon* nme = (zNMECommon*)nmelist.list[i];

        if (!nme->flg_hudrend)
        {
            continue;
        }

        nme->flg_hudrend = 0;

        if (!xEntIsVisible(nme))
        {
            continue;
        }
        if (!(nme->baseFlags & 0x1) || (nme->baseFlags & 0x40))
        {
            continue;
        }
        if (nme->model != NULL && (nme->model->Flags & 0x400))
        {
            continue;
        }
        if (!nme->runtimeData.flg_activeOn)
        {
            continue;
        }

        nme->RenderHud();
    }
}

void zMODEnemy::SomethingWonderfulUpdate() const
{
    S32 flg_wonder = 0;

    if (globals.player.s->Health < 1)
    {
        flg_wonder |= 0x2;
    }

    if (globals.player.ControlOff & ~(CONTROL_OWNER_CRUISE_BUBBLE | CONTROL_OWNER_SPRINGBOARD))
    {
        flg_wonder |= 0x4;
    }

    if (xScreenFade_FadeInProgress())
    {
        flg_wonder |= 0x100;
    }

    if (globals.cmgr != NULL && globals.cmgr->csn != NULL)
    {
        flg_wonder |= 0x1;
    }

    if (NME::GlobalData::isConversation)
    {
        flg_wonder |= 0x8;
    }

    if (zEntTeleportBox_playerIn())
    {
        flg_wonder |= 0x80;
    }

    NME::GlobalData::flg_wonder = flg_wonder;
}

U8 zNMECommon_IsConversation()
{
    return NME::GlobalData::isConversation != 0;
}

S32 zNMECommon_MaxShadowNeededLater()
{
    S32 cnt_shadPool = 0;
    S32 cnt_shadFixed = 0;
    st_XORDEREDARRAY* reallist = zNPCMgr_GetNPCList();
    S32 num_nme = reallist->cnt;

    for (S32 i = 0; i < num_nme; i++)
    {
        zNMECommon* nme = (zNMECommon*)reallist->list[i];
        S32 flg_shadpool = nme->WannaPoolShadow();

        if (flg_shadpool & 0x2)
        {
            cnt_shadPool++;
        }
        else if (nme->entShadow != NULL)
        {
            cnt_shadFixed++;
        }
    }

    S32 num_shadTotal = MIN(cnt_shadPool * 2, 20);
    num_shadTotal += cnt_shadFixed;
    return num_shadTotal;
}

// No GC symbol: inlined into Common_Init, its only caller.
inline void zNMECommon::ShadowPrep()
{
    S32 useCachePools;
    xEntShadow* es;

    shadow_cache_fudge_factor = 1.5f;

    useCachePools = WannaPoolShadow();
    if (useCachePools != 0)
    {
        flg_misc |= useCachePools;
        simpShadow = NULL;
        entShadow = NULL;
    }
    else
    {
        simpShadow = Gimme_SimpShadowCache();
        if (simpShadow != NULL)
        {
            xShadowSimple_CacheInit_Expert(simpShadow, NULL, 0);
            simpShadow->tol_movement = 0.25f;
        }

        entShadow = Gimme_ShadowData();
        if (entShadow != NULL)
        {
            shadow_strength = 0.25f;
            es = entShadow;
            es->pos = asset->pos + NME::Shadowing::EntShad::vec_offset;
            es->vec = NME::Shadowing::EntShad::dir_theSunzRays;
            es->shadowModel = NULL;
            es->flg_castOnOneDFF = 1;
            es->flg_castOnAllDFF = 0;
            es->flg_disableEnvCast = 0;
            es->dst_cast = 10.0f;
            es->radius[0] = 10.5f;
            es->radius[1] = 1.5f;
        }
        else
        {
            shadow_strength = 0.0f;
        }
    }
}

// No symbol in any build (always inlined into Common_Init); the name is ours. The enemy's links are
// stored at the end of its asset, padded to a 4-byte boundary: step to the end of the asset, then
// back by the padded size of the link table.
static inline xLinkAsset* NMELinksFromAsset(zNMECommon* npc)
{
    if (npc->linkCount < 1)
    {
        return NULL;
    }

    U8* addr = (U8*)npc->nmeass;
    addr += npc->siz_nmeass;

    U32 siz_links = npc->linkCount * sizeof(xLinkAsset);
    siz_links = ALIGN(siz_links, 4);
    return (xLinkAsset*)(addr - siz_links);
}

void zNMECommon::Common_Init(xEntAsset* entasset)
{
    U32 bufsize;
    void* info;
    S32 i;

    link = NMELinksFromAsset(this);

    ShadowPrep();

    CollisionDefaults();
    visUpdate = TellMeVisOnOff;

    drv_data = Gimme_DriveData();
    frame = Gimme_xEntFrame();
    navnet = Gimme_NavNet();
    if (navnet != NULL)
    {
        navnet->nme_owner = this;
    }

    memset(&runtimeData, 0, sizeof(NMERuntime));
    RuntimeDefaults();

    if (npc_asset != NULL && npc_asset->settings != 0)
    {
        info = xSTFindAsset(npc_asset->settings, &bufsize);
        local_parameters_size = bufsize - 4;
        local_parameters = &((xNPCSettingsIN2Asset*)info)->param;
    }
    else
    {
        local_parameters_size = 0;
        local_parameters = NULL;
    }

    i = itemType - NME_TYPE_COMMON;
    snd_table = gzSoundTableList[i];
    for (i = 0; i < 10; i++)
    {
        sndID[i] = (iSndHandle)-1;
    }
}

void zNMECommon::Setup()
{
    const char* shrapnelName;

    get_parameter("Shrapnel", &shrapnelName, NULL);
    shrapnelAsset = NULL;
    if (shrapnelName)
    {
        shrapnelAsset = (zShrapnelAsset*)xSTFindAsset(xStrHash(shrapnelName), NULL);
    }
}

void zNMECommon::CreateConfig(const char* name)
{
    cfg_common = &Common::Config::cfg_common;
    cfg_shadparm = &Common::Config::cfg_shadparm_common;
}

S32 zNMECommon::SysEvent(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                         xBase* toParamWidget, U32 toParamWidgetID, S32* handled)
{
    static NMEMsg msg;
    zNMECommon* npc = (zNMECommon*)to;

    memset(&msg, 0, sizeof(NMEMsg));
    msg.msgid = NME_MID_SYSEVENT;
    msg.infotype = NME_MDAT_SYSEVENT;
    msg.from = npc->id;
    msg.sendto = npc->id;

    // Not in the PS2 DWARF (BfBB's zNPCCommon::SysEvent has it).
    NMESysEvent* se = &msg.sysevent;
    se->doLinkEvents = 1;
    se->handled = 0;
    se->to = to;
    se->from = from;
    se->toEvent = toEvent;
    se->toParamWidget = toParamWidget;
    se->toParamWidgetID = toParamWidgetID;

    if (toParam == NULL)
    {
        se->toParam[0] = 0.0f;
        se->toParam[1] = 0.0f;
        se->toParam[2] = 0.0f;
        se->toParam[3] = 0.0f;
    }
    else
    {
        se->toParam[0] = toParam[0];
        se->toParam[1] = toParam[1];
        se->toParam[2] = toParam[2];
        se->toParam[3] = toParam[3];
    }

    NMEMessage(&msg);

    *handled = msg.sysevent.handled;
    return se->doLinkEvents;
}

// Default handling of system events for every enemy type. Returns 0 for events it ignores.
S32 zNMECommon::DfltSysEventMail(NMESysEvent* sysmail)
{
    S32 handled = 1;

    switch (sysmail->toEvent)
    {
    case eEventUnknown:
        break;
    case eEventSceneEnd:
    {
        xPsyche* psy = psy_self;
        if (psy != NULL)
        {
            psy->GoalNone(1, 1);
        }
        break;
    }
    case eEventSceneBegin:
    case eEventRoomBegin:
    case eEventRoomEnd:
        break;
    case eEventReset:
        Reset();
        break;
    case eEventVisible:
        xEntShow(this);
        break;
    case eEventInvisible:
        xEntHide(this);
        break;
    case eEventMount:
    case eEventDismount:
    case eEventNPCSpecial_PlatformSnap:
    case eEventNPCSpecial_PlatformFall:
        break;
    case eEventHit_Melee:
        Damage(DMGTYP_SIDE, xglobals->___player_ent_dont_use_directly, NULL, -1,
               ePowerupLevelNormal);
        break;
    case eEventHit_BubbleBowl:
        Damage(DMGTYP_BUBBOWL, xglobals->___player_ent_dont_use_directly, NULL, -1,
               ePowerupLevelNormal);
        break;
    case eEventHit_BubbleBash:
        Damage(DMGTYP_BELOW, xglobals->___player_ent_dont_use_directly, NULL, -1,
               ePowerupLevelNormal);
        break;
    case eEventHit_BubbleBounce:
        Damage(DMGTYP_ABOVE, xglobals->___player_ent_dont_use_directly, NULL, -1,
               ePowerupLevelNormal);
        break;
    case eEventHit_PatrickSlam:
        Damage(DMGTYP_ABOVE, xglobals->___player_ent_dont_use_directly, NULL, -1,
               ePowerupLevelNormal);
        break;
    case eEventHit_Throw:
        Damage(DMGTYP_HITBYTOSS, xglobals->___player_ent_dont_use_directly, NULL, -1,
               ePowerupLevelNormal);
        break;
    case eEventHit_Cartwheel:
        Damage(DMGTYP_CARTWHEEL, xglobals->___player_ent_dont_use_directly, NULL, -1,
               ePowerupLevelNormal);
        break;
    case eEventHit_Cruise:
    {
        xVec3 vec_hit = globals.oldSkoolCamera.mat.at * 1.0f;
        Damage(DMGTYP_CRUISEBUBBLE, xglobals->___player_ent_dont_use_directly, &vec_hit, -1,
               ePowerupLevelNormal);
        break;
    }
    case eEventHit:
        ConvertHitEvent(sysmail);
        break;
    case eEventSetLightKit:
        xModelSetLightKit(model, (xLightKit*)sysmail->toParamWidget);
        break;
    case eEventSetUpdateDistance:
        if (globals.updateMgr != NULL)
        {
#ifdef PLATFORM_PC
            // PLATFORM_PC: the cull manager knows each entity by its xEnt* (zSceneSetup passes
            // the scene's xEnt list) and finds this one by comparing pointers. A host compiler
            // puts an enemy's vptr at offset 0, ahead of its xEnt, so `this` as a void* matched
            // nothing and the event was ignored; the xEnt* (the cast adjusts) is what it knows.
            if (sysmail->toParam[0] <= 0.0f)
            {
                xUpdateCull_SetCB(globals.updateMgr, (xEnt*)this, xUpdateCull_AlwaysTrueCB, NULL);
            }
            else
            {
                FloatAndVoid dist;
                dist.f = SQR(sysmail->toParam[0]);
                xUpdateCull_SetCB(globals.updateMgr, (xEnt*)this, xUpdateCull_DistanceSquaredCB,
                                  dist.v);
            }
#else
            if (sysmail->toParam[0] <= 0.0f)
            {
                xUpdateCull_SetCB(globals.updateMgr, this, xUpdateCull_AlwaysTrueCB, NULL);
            }
            else
            {
                FloatAndVoid dist;
                dist.f = SQR(sysmail->toParam[0]);
                xUpdateCull_SetCB(globals.updateMgr, this, xUpdateCull_DistanceSquaredCB, dist.v);
            }
#endif
        }
        break;
    case eEventLaunchShrapnel:
    {
        en_npctyp ntyp = SelfType();
        // Not in the PS2 DWARF: the shrapnel asset of this enemy type.
        zShrapnelAsset* shrap = zNMEShrapnelRetrieveShrapnel(ntyp);
        if (shrap != NULL && shrap->initCB != NULL)
        {
            xVec3 currVel = {};
            shrap->initCB(shrap, model, &currVel, NULL);
        }
        break;
    }
    case eEventDeath:
        break;
    case eEventNPCKillQuietly:
        Damage(DMGTYP_KILLEVENT, sysmail->from, NULL, -1, ePowerupLevelNormal);
        break;
    case eEventDestroy:
    case eEventKill:
        Damage(DMGTYP_INSTAKILL, sysmail->from, NULL, -1, ePowerupLevelNormal);
        break;
    case eEventNPCRespawn:
        Respawn(NULL, NULL, NULL);
        break;
    case eEventNPCPatrolOn:
        runtimeData.flg_enablePatrol = 1;
        break;
    case eEventNPCPatrolOff:
        runtimeData.flg_enablePatrol = 0;
        break;
    case eEventNPCWanderOn:
        runtimeData.flg_enableWander = 1;
        break;
    case eEventNPCWanderOff:
        runtimeData.flg_enableWander = 0;
        break;
    case eEventNPCDetectOn:
        runtimeData.flg_enableDetect = 1;
        break;
    case eEventNPCDetectOff:
        runtimeData.flg_enableDetect = 0;
        break;
    case eEventNPCChaseOn:
    case eEventNPCChaseOff:
        break;
    case eEventNPCDetectAlways:
        runtimeData.overrideDetect = ALLOW_ALWAYS;
        break;
    case eEventNPCDetectNever:
        runtimeData.overrideDetect = ALLOW_NEVER;
        break;
    case eEventNPCDetectNormal:
        runtimeData.overrideDetect = runtimeData.orig.overrideDetect;
        break;
    case eEventAttackOn:
    case eEventAttackOff:
    case eEventNPCAttack:
        break;
    case eEventNPCTakeNoDamageOn:
        runtimeData.flg_takeNoDamage = 1;
        break;
    case eEventNPCTakeNoDamageOff:
        runtimeData.flg_takeNoDamage = 0;
        break;
    case eEventNPCFightOn:
        runtimeData.overrideAttack = ALLOW_ALWAYS;
        break;
    case eEventNPCFightOff:
        runtimeData.overrideAttack = ALLOW_NEVER;
        break;
    case eEventNPCFightDefault:
        runtimeData.overrideAttack = runtimeData.orig.overrideAttack;
        break;
    case eEventNPCForceConverseStart:
        break;
    case eEventNPCCheerForMe:
        zNMEMsg_SendMsg(NME_MID_CELEBRATE, this, -1.0f);
        break;
    case eEventNPCGoToSleep:
    case eEventNPCWakeUp:
        break;
    case eEventNPCSetActiveOn:
        if (!runtimeData.flg_activeOn)
        {
            runtimeData.flg_activeOn = 1;
            chkby = runtimeData.restore.chkby;
            penby = runtimeData.restore.penby;
            runtimeData.restore.chkby = 0;
            runtimeData.restore.penby = 0;
            if (psy_self != NULL)
            {
                runtimeData.restore.gid_compare = (en_npcgol)psy_self->GIDOfActive();
            }
        }
        break;
    case eEventNPCSetActiveOff:
        runtimeData.flg_activeOn = 0;
        runtimeData.restore.chkby = chkby;
        runtimeData.restore.penby = penby;
        chkby = 0;
        penby = 0;
        break;
    case eEventDuploWaveBegin:
    case eEventDuploWaveComplete:
    case eEventDuploNPCBorn:
    case eEventDuploNPCKilled:
    case eEventDuploExpiredMaxNPC:
    case eEventDuploPause:
    case eEventDuploResume:
    case eEventDuploKillKids:
    case eEventNMESetMovepointGroup:
        break;
    case eEventNMESetMovepointPath:
    {
        // Not in the PS2 DWARF (nav_goto, nav): retail reads toParamWidget before the
        // Gimme_NavNet call (as the PS2 build does), so both are probably the locals of an
        // unknown inline helper.
        zMovePoint* nav_goto = (zMovePoint*)sysmail->toParamWidget;
        zNMENavNet* nav = Gimme_NavNet();
        if (nav != NULL)
        {
            nav->MvptReset(nav_goto);
            tmr_scary = 1.0f;
        }
        break;
    }
    default:
        handled = 0;
        break;
    }

    return handled;
}

// Turns a system hit event into Damage(): the damage type and hit direction depend on what hit us.
void zNMECommon::ConvertHitEvent(NMESysEvent* sysmail)
{
    // Not in the PS2 DWARF (BfBB's zNPCCommon::ConvertHitEvent has it): retail computes each
    // length once and divides by it.
    F32 mag;

    if (sysmail->from == NULL)
    {
        Damage(DMGTYP_WOUNDEVENT, NULL, &globals.oldSkoolCamera.mat.at, sysmail->toParam[0],
               ePowerupLevelNormal);
        return;
    }

    switch (sysmail->from->baseType)
    {
    case eBaseTypePlayer:
    {
        U32 mvinf = zEntPlayer_MoveInfo();
        en_npcdmg what;

        if (mvinf & 0x8)
        {
            what = DMGTYP_BELOW;
        }
        else if (mvinf & 0x10)
        {
            what = DMGTYP_ABOVE;
        }
        else if (mvinf & 0x20)
        {
            what = DMGTYP_SIDE;
        }
        else
        {
            what = DMGTYP_SIDE;
        }

        xVec3 vec_hit = *xEntGetPos(xglobals->___player_ent_dont_use_directly) - *xEntGetPos(this);
        mag = vec_hit.length();
        if (mag < 0.00001f)
        {
            Damage(what, sysmail->from, NULL, -1, ePowerupLevelNormal);
        }
        else
        {
            vec_hit /= mag;
            Damage(what, sysmail->from, &vec_hit, -1, ePowerupLevelNormal);
        }
        break;
    }
    case eBaseTypeCruiseBubble:
        if (sysmail->toParam != NULL)
        {
            xVec3 pos_cruiser;
            pos_cruiser.x = sysmail->toParam[0];
            pos_cruiser.y = sysmail->toParam[1];
            pos_cruiser.z = sysmail->toParam[2];

            xVec3 vec_hit = pos_cruiser - *xEntGetPos(this);
            mag = vec_hit.length();
            if (mag < 0.00001f)
            {
                Damage(DMGTYP_CRUISEBUBBLE, sysmail->from, NULL, -1, ePowerupLevelNormal);
            }
            else
            {
                vec_hit /= mag;
                Damage(DMGTYP_CRUISEBUBBLE, sysmail->from, &vec_hit, -1, ePowerupLevelNormal);
            }
        }
        break;
    case eBaseTypeBoulder:
    {
        xEntBoulder* bould = (xEntBoulder*)sysmail->from;
        en_npcdmg what;

        if (zEntPlayerGetBBowl() != NULL && zEntPlayerGetBBowl() == bould)
        {
            what = DMGTYP_BUBBOWL;
        }
        else
        {
            what = DMGTYP_BOULDER;
        }

        xVec3 vec_hit = *xEntGetPos(bould) - *xEntGetPos(this);
        mag = vec_hit.length();
        if (mag < 0.00001f)
        {
            Damage(what, sysmail->from, NULL, -1, ePowerupLevelNormal);
        }
        else
        {
            vec_hit /= mag;
            Damage(what, sysmail->from, &vec_hit, -1, ePowerupLevelNormal);
        }
        break;
    }
    default:
    {
        xVec3 vec_hit = globals.oldSkoolCamera.mat.at;
        Damage(DMGTYP_WOUNDEVENT, sysmail->from, &vec_hit, sysmail->toParam[0],
               ePowerupLevelNormal);
        break;
    }
    }
}

void zNMECommon::Damage(en_npcdmg dmgtype, const xBase* who, const xVec3* vec_hit,
                        S32 amt_dmgCaller, en_plyrpup pup_fromCaller)
{
    static NMEMsg msg;
    S32 doAlways = 0;
    NMEDamageInfo* dmg;

    switch (dmgtype)
    {
    case DMGTYP_INSTAKILL:
    case DMGTYP_WOUNDEVENT:
    case DMGTYP_KILLEVENT:
    case DMGTYP_REFLECTED:
        doAlways = 1;
        break;
    }

    if (doAlways || (!runtimeData.flg_takeNoDamage && tmr_invuln < 0.0f && IsPresent()))
    {
        tmr_invuln = cfg_common->damage.tym_invuln;

        memset(&msg, 0, sizeof(NMEMsg));
        msg.from = id;
        msg.sendto = id;
        msg.msgid = NME_MID_DAMAGE;
        msg.infotype = NME_MDAT_DAMAGE;

        dmg = &msg.dmgdata;
        dmg->dmg_from = (xBase*)who;
        dmg->dmg_type = dmgtype;
        if (amt_dmgCaller < 0)
        {
            dmg->amt_damage = NMEC_DmgAmtForAttack(dmgtype, &dmg->pup_player);
        }
        else
        {
            dmg->amt_damage = amt_dmgCaller;
            dmg->pup_player = pup_fromCaller;
        }

        if (vec_hit != NULL)
        {
            xVec3Copy(&dmg->vec_dmghit, vec_hit);
        }
        else
        {
            xVec3Copy(&dmg->vec_dmghit, &g_O3);
        }

        switch (dmgtype)
        {
        case DMGTYP_SLIDE:
        case DMGTYP_HITBYTOSS:
        case DMGTYP_CARTWHEEL:
        case DMGTYP_CRUISEBUBBLE:
        case DMGTYP_BUBBOWL:
        case DMGTYP_BELOW:
        case DMGTYP_SIDE:
            SndPlayFromTable(5); // eNMESound_Ouch
            break;
        }

        zNMEMsg_SendMsg(&msg, this, -1.0f);
    }
}

S32 zNMECommon::Respawn(const xVec3* pos, zMovePoint* mvptFirst, zMovePoint* mvptSpawnRef)
{
    NMEMsg msg;

    memset(&msg, 0, sizeof(NMEMsg));
    msg.msgid = NME_MID_RESPAWN;
    msg.from = id;
    msg.sendto = id;
    msg.infotype = NME_MDAT_SPAWN;

    if (pos != NULL)
    {
        msg.spawning.pos_spawn = *pos;
    }
    else
    {
        msg.spawning.pos_spawn = asset->pos;
    }

    if (mvptFirst != NULL)
    {
        msg.spawning.nav_firstMovepoint = mvptFirst;
        msg.spawning.nav_spawnReference = mvptSpawnRef;
    }
    else
    {
        msg.spawning.nav_spawnReference = msg.spawning.nav_firstMovepoint = MvptFirstFollow();
    }

    msg.spawning.spawnSuccess = 0;
    zNMEMsg_SendMsg(&msg, this, -1.0f);

    return msg.spawning.spawnSuccess;
}

// Visibility callback (xEnt::visUpdate).
void zNMECommon::TellMeVisOnOff(xEnt* ent)
{
    zNMECommon* npc = (zNMECommon*)ent;

    if (xEntIsVisible(npc))
    {
        npc->penby = npc->ColPenByFlags();
    }
    else
    {
        npc->penby = 0;
    }
}

// No GC symbol: a plain function (out of line in the PS2 build) that is auto-inlined into
// ShadChkStatus, its only caller; the unreferenced copy is dead-stripped by the linker.
void zNMECommon::FullShadAcquire()
{
    xEntShadow* es;

    if (entShadow == NULL && (flg_misc & 0x2))
    {
        entShadow = NMEC_EntShadowCacheReserve();
        if (entShadow == NULL)
        {
            shadow_strength = 0.0f;
        }
        else
        {
            shadow_strength = 0.25f;
            es = entShadow;
            es->pos = asset->pos + NME::Shadowing::EntShad::vec_offset;
            es->vec = NME::Shadowing::EntShad::dir_theSunzRays;
            es->shadowModel = NULL;
            es->flg_castOnOneDFF = 1;
            es->flg_castOnAllDFF = 0;
            es->dst_cast = 10.0f;
            es->radius[0] = 10.5f;
            es->radius[1] = 1.5f;
        }
    }
}

// Pools the shadows by distance to the camera: none when far, a simple shadow at medium range
// and an entity shadow as well when close.
void zNMECommon::ShadChkStatus()
{
    xVec3 vec_toCam;
    F32 ds2 = NMEC_ds2_toCam((xVec3*)&model->Mat->pos, &vec_toCam);

    if (ds2 > SQR(cfg_shadparm->shadrad.rad_noShadow))
    {
        if (simpShadow != NULL && (flg_misc & 0x2))
        {
            NMEC_SimpShadowCacheRelease(simpShadow);
            simpShadow = NULL;
        }

        if (entShadow != NULL && (flg_misc & 0x2))
        {
            NMEC_EntShadowCacheRelease(entShadow);
            entShadow = NULL;
            shadow_strength = 0.0f;
        }
    }
    else if (ds2 > SQR(cfg_shadparm->shadrad.rad_complexShadow))
    {
        if (entShadow != NULL && (flg_misc & 0x2))
        {
            NMEC_EntShadowCacheRelease(entShadow);
            entShadow = NULL;
            shadow_strength = 0.0f;
        }

        if (simpShadow == NULL && (flg_misc & 0x2))
        {
            simpShadow = NMEC_SimpShadowCacheReserve();
            if (simpShadow != NULL)
            {
                xShadowSimple_CacheInit_Expert(simpShadow, cfg_shadparm->simpshad.rast_shadow, 0);
                simpShadow->tol_movement = 0.25f;
                simpShadow->radiusOptional = cfg_shadparm->simpshad.rad_shadow;
            }
        }
    }
    else
    {
        if (simpShadow == NULL && (flg_misc & 0x2))
        {
            simpShadow = NMEC_SimpShadowCacheReserve();
            if (simpShadow != NULL)
            {
                xShadowSimple_CacheInit_Expert(simpShadow, cfg_shadparm->simpshad.rast_shadow, 0);
                simpShadow->tol_movement = 0.25f;
                simpShadow->radiusOptional = cfg_shadparm->simpshad.rad_shadow;
            }
        }

        FullShadAcquire();
    }
}

void zNMECommon::ShadStufIAmDead()
{
    if (simpShadow != NULL && (flg_misc & 0x2))
    {
        NMEC_SimpShadowCacheRelease(simpShadow);
        simpShadow = NULL;
    }

    if (entShadow != NULL && (flg_misc & 0x2))
    {
        NMEC_EntShadowCacheRelease(entShadow);
        entShadow = NULL;
        shadow_strength = 0.0f;
    }
}

// ds2, str and alpha are not in the PS2 DWARF, but retail keeps all three in registers. 13
// compiled-out placeholders keep GC/2.6 from auto-inlining this (>= 30 statements), as retail calls
// it from 10 Process functions.
void zNMECommon::ShadStufUpdate()
{
    xASSERT_PLACEHOLDER();

    xVec3 vec_toCam;

    if (flg_misc & 0x2)
    {
        ShadChkStatus();
    }

    if (simpShadow == NULL && entShadow == NULL)
    {
        return;
    }

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    F32 ds2 = NMEC_ds2_toCam((xVec3*)&model->Mat->pos, &vec_toCam);

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    F32 str = LERP(CLAMP(ds2 / 1225.0f, 0.0f, 1.0f), 0.35f, 0.0f);

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    shadow_strength = str;

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (simpShadow != NULL)
    {
        U8 alpha = CLAMP(255.0f * str, 0.0f, 255.0f);
        simpShadow->alpha = alpha;
        if (alpha != 0 && entShadow == NULL && xEntIsVisible(this))
        {
            xShadowSimple_Add_Expert(simpShadow, (xMat4x3*)model->Mat, alpha,
                                     cfg_shadparm->simpshad.rad_shadow, 10.0f, 1.0f);
        }

        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
    }

    if (entShadow != NULL)
    {
        entShadow->pos = *(xVec3*)&model->Mat->pos + NME::Shadowing::EntShad::vec_offset;
    }

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
}

// Inlined (no symbol; the name is ours): lets the current goal review the collisions first.
static inline S32 NMEC_GoalCollReview(zNMECommon* npc)
{
    if (npc->psy_self == NULL)
    {
        return 0;
    }

    zNMEGoalCommon* goal = (zNMEGoalCommon*)npc->psy_self->GetCurGoal();
    if (goal == NULL)
    {
        return 0;
    }
    if (!(goal->flg_npcgable & 0x1))
    {
        return 0;
    }

    return goal->CollReview(NULL);
}

en_nmesimp zNMECommon::Common_SimpStat()
{
    S32 gid_curr;

    if (psy_self == NULL)
    {
        return NME_SIMP_HAPPY;
    }

    gid_curr = psy_self->GIDOfActive();

    switch (gid_curr)
    {
    case NME_GOAL_CRIT_IDLE:
    case NME_GOAL_CRIT_PATROL:
        return NME_SIMP_HAPPY;
    case NME_GOAL_CRIT_DYING:
    case NME_GOAL_CRIT_DEAD:
        return NME_SIMP_DEAD;
    case NME_GOAL_CRIT_BATTACK:
    case NME_GOAL_CRIT_JATTACK:
        return NME_SIMP_BIGBULLY;
    case NME_GOAL_CRIT_JDYING:
        return NME_SIMP_DEAD;
    case NME_GOAL_TURR_IDLE:
    case NME_GOAL_TURR_RELOAD:
    case NME_GOAL_TURR_HURT:
    case NME_GOAL_TURR_DEAD:
        return NME_SIMP_HAPPY;
    case NME_GOAL_TURR_TREADY:
        return NME_SIMP_HAPPY;
    case NME_GOAL_TURR_TTURN:
    case NME_GOAL_TURR_TSHOOT:
        return NME_SIMP_BIGBULLY;
    case NME_GOAL_TURR_PDORMANT:
        return NME_SIMP_IGNORANT;
    case NME_GOAL_TURR_PALERT:
        return NME_SIMP_WARY;
    case NME_GOAL_TURR_PPATALPHA:
        return NME_SIMP_BIGBULLY;
    case NME_GOAL_TURR_BIDLE:
    case NME_GOAL_TURR_BTURN:
    case NME_GOAL_TURR_BSHOOT:
    case NME_GOAL_TURR_BHURT:
        return NME_SIMP_HAPPY;
    case NME_GOAL_TURR_SPIRAL:
        return NME_SIMP_HAPPY;
    case NME_GOAL_IDLE:
    case NME_GOAL_PATROL:
    case NME_GOAL_WANDER:
    case NME_GOAL_FIDGET:
    case NME_GOAL_WAITING:
        return NME_SIMP_IGNORANT;
    case NME_GOAL_DEAD:
    case NME_GOAL_NOMANLAND:
    case NME_GOAL_LIMBO:
        return NME_SIMP_DEAD;
    case NME_GOAL_DEV_ANIMVIEW:
    case NME_GOAL_DEV_HEROMODE:
        return NME_SIMP_IGNORANT;
    case NME_GOAL_TIKI_IDLE:
    case NME_GOAL_TIKI_PATROL:
    case NME_GOAL_TIKI_HIDE:
    case NME_GOAL_TIKI_COUNT:
    case NME_GOAL_TIKI_DYING:
    case NME_GOAL_TIKI_DEAD:
        return NME_SIMP_HAPPY;
    case NME_GOAL_AFTERLIFE:
    case NME_GOAL_SPAWN:
        return NME_SIMP_DEAD;
    case NME_GOAL_WOUND:
        return NME_SIMP_BIGBULLY;
    case NME_GOAL_SPOOKED:
        return NME_SIMP_WARY;
    case NME_GOAL_NOTICE:
        return NME_SIMP_WARYDETECT;
    case NME_GOAL_SCAREWAIT:
    case NME_GOAL_SCARE:
        return NME_SIMP_BIGBULLY;
    case NME_GOAL_TAUNT:
        return NME_SIMP_BIGBULLY;
    case NME_GOAL_EVILPAT:
    case NME_GOAL_STUNNED:
    case NME_GOAL_PATCARRY:
    case NME_GOAL_PATTWIRL:
    case NME_GOAL_PATTHROW:
        return NME_SIMP_CONFUSED;
    case NME_GOAL_TRIGGER_NORMAL:
    case NME_GOAL_TRIGGER_SCARY:
    case NME_GOAL_TRIGGER_DETECT:
    case NME_GOAL_TRIGGER_ALERT:
    case NME_GOAL_TRIGGER_BATTLE:
    case NME_GOAL_TRIGGER_WOUND:
    case NME_GOAL_TRIGGER_ATTACK:
    case NME_GOAL_TRIGGER_VINIVICIVIDI:
        return NME_SIMP_HAPPY;
    case NME_GOAL_FOGGER_AWARE:
        return NME_SIMP_WARY;
    case NME_GOAL_FOGGER_BATTLE:
    case NME_GOAL_FOGGER_ATTACK:
        return NME_SIMP_BIGBULLY;
    case NME_GOAL_SLAMMER_AWARE:
        return NME_SIMP_WARY;
    case NME_GOAL_SLAMMER_BATTLE:
    case NME_GOAL_SLAMMER_ATTACK:
        return NME_SIMP_BIGBULLY;
    case NME_GOAL_SPINNER_AWARE:
        return NME_SIMP_WARY;
    case NME_GOAL_SPINNER_BATTLE:
        return NME_SIMP_BIGBULLY;
    case NME_GOAL_FLINGER_NORMAL:
        return NME_SIMP_IGNORANT;
    case NME_GOAL_FLINGER_AWARE:
        return NME_SIMP_WARY;
    case NME_GOAL_FLINGER_BATTLE:
    case NME_GOAL_FLINGER_ATTACK:
    case NME_GOAL_FLINGER_BOING:
    case NME_GOAL_FLINGER_FLEE:
    case NME_GOAL_FLINGER_PANIC:
        return NME_SIMP_BIGBULLY;
    case NME_GOAL_FLINGER_MOVE:
        return NME_SIMP_HAPPY;
    case NME_GOAL_POPPER_NORMAL:
        return NME_SIMP_IGNORANT;
    case NME_GOAL_POPPER_AWARE:
        return NME_SIMP_WARY;
    case NME_GOAL_POPPER_WOUND:
    case NME_GOAL_POPPER_EVADE:
    case NME_GOAL_POPPER_BATTLE:
    case NME_GOAL_POPPER_ATTACK:
        return NME_SIMP_BIGBULLY;
    case NME_GOAL_ZAP_NORMAL:
        return NME_SIMP_IGNORANT;
    case NME_GOAL_ZAP_AWARE:
        return NME_SIMP_WARY;
    case NME_GOAL_ZAP_BATTLE:
    case NME_GOAL_ZAP_WOUND:
    case NME_GOAL_ZAP_ZAP:
        return NME_SIMP_BIGBULLY;
    case NME_GOAL_ZAP_MOVE:
        return NME_SIMP_HAPPY;
    case NME_GOAL_MERV_NORMAL:
        return NME_SIMP_IGNORANT;
    case NME_GOAL_MERV_AWARE:
        return NME_SIMP_WARY;
    case NME_GOAL_MERV_BATTLE:
    case NME_GOAL_MERV_ZAP:
    case NME_GOAL_MERV_BOMB:
    case NME_GOAL_MERV_BOWL:
    case NME_GOAL_MERV_WOUND:
        return NME_SIMP_BIGBULLY;
    case NME_GOAL_MERV_MOVE:
        return NME_SIMP_HAPPY;
    case NME_GOAL_BUCK_RUNNING:
    case NME_GOAL_BUCK_BIRTHING:
    case NME_GOAL_BUCK_DYING:
    case NME_GOAL_BUCK_DEAD:
        return NME_SIMP_HAPPY;
    case NME_GOAL_DENNIS_NORMAL:
    case NME_GOAL_DENNIS_EVADE:
    case NME_GOAL_DENNIS_BATTLE:
    case NME_GOAL_DENNIS_ATTACK:
    case NME_GOAL_DENNIS_TAUNT:
    case NME_GOAL_DENNIS_DAMAGE:
    case NME_GOAL_DENNIS_DEAD:
    case NME_GOAL_DENTOO_NORMAL:
    case NME_GOAL_DENTOO_EVADE:
    case NME_GOAL_DENTOO_BATTLE:
    case NME_GOAL_DENTOO_ATTACK:
    case NME_GOAL_DENTOO_TAUNT:
    case NME_GOAL_DENTOO_DAMAGE:
    case NME_GOAL_DENTOO_DEAD:
    case NME_GOAL_SBBAT_IDLE:
    case NME_GOAL_SBBAT_DEAD:
        return NME_SIMP_HAPPY;
    }

    return NME_SIMP_HAPPY;
}

void zNMECommon::RunCollision(F32 dt)
{
    xEntCollis collisData;

    memset(&collisData, 0, sizeof(xEntCollis));
    collis = &collisData;
    collis->chk = flg_colCheck;
    collis->pen = flg_penCheck;

    xEntCollide(this, xglobals->sceneCur, dt);

    if (collisData.idx > 6 && !NMEC_GoalCollReview(this))
    {
        CollideReview();
    }

    collis = NULL;
}

// No GC symbol: inlined into Common_Collide (its unused copy is dead-stripped).
S32 zNMECommon::ReviewCollSurfs(const xEntCollis* npccol, zNMECommon* npc)
{
    S32 badsurf = 0;
    S32 i;
    const xCollis* colrec;
    F32 goodep;
    xSurface* surf;

    for (i = 6; i < npccol->idx; i++)
    {
        colrec = &npccol->colls[i];

        surf = zSurfaceGetSurface(colrec);
        if (surf != NULL && surf->state == 0 && zSurfaceGetDamageType(surf) != 0)
        {
            badsurf++;
        }
        else if (colrec->optr != NULL && zGooIs((xEnt*)colrec->optr, goodep, 0))
        {
            badsurf++;
        }

        if (badsurf != 0)
        {
            if (npc != NULL)
            {
                npc->Damage(DMGTYP_DAMAGE_SURFACE, NULL, NULL, -1, ePowerupLevelNormal);
            }
            break;
        }
    }

    return badsurf;
}

// No GC symbol: auto-inlined into its only caller zNMEGoalWound::CollReview (dead-stripped).
// Reflects vel off the summed depenetration normal of the environment collisions, scaled by
// (1 + fac_elastic).
S32 zNMECommon::ReviewCollBounce(const xEntCollis* npccol, xVec3* vel, F32 fac_elastic)
{
    xVec3 vec_depen;
    S32 i;
    const xCollis* colrec;

    if (npccol->idx <= 6)
    {
        return 0;
    }

    memset(&vec_depen, 0, sizeof(xVec3));
    for (i = 6; i < npccol->idx; i++)
    {
        colrec = &npccol->colls[i];
        vec_depen += colrec->norm * xVec3Dot(&colrec->depen, &colrec->norm);
    }

    // not in the DWARF: the length is computed once and used for the threshold test and the
    // normalisation.
    F32 len = vec_depen.length();
    if (len < 0.001f)
    {
        return 0;
    }

    xVec3 dir_effnorm = vec_depen * (1.0f / len);
    xVec3 vel_perp = dir_effnorm * xVec3Dot(vel, &dir_effnorm);
    *vel -= vel_perp * (1.0f + fac_elastic);

    return 1;
}

void zNMECommon::Common_Collide()
{
    xEntCollis* npccol = collis;

    if (!(npccol->colls[0].flags & k_HIT_IT) && (flg_move & 0x2))
    {
        colFreq = -1;
    }

    if (drv_data != NULL)
    {
        drv_data->ReviewCollide(npccol);
    }

    ReviewCollSurfs(npccol, this);
}

// No GC symbol (always inlined); BfBB's NPCC_xBoundAway: moves the bound out of the way of a
// scene ray test.
static inline void NMEC_xBoundAway(xBound* bnd)
{
    if (bnd->type == XBOUND_TYPE_SPHERE)
    {
        bnd->box.center.y -= 1000000.0f;
    }
    else if (bnd->type == XBOUND_TYPE_BOX)
    {
        bnd->box.center.y -= 1000000.0f;
    }
}

// No GC symbol (always inlined); BfBB's NPCC_xBoundBack: undoes NMEC_xBoundAway.
static inline void NMEC_xBoundBack(xBound* bnd)
{
    if (bnd->type == XBOUND_TYPE_SPHERE)
    {
        bnd->box.center.y += 1000000.0f;
    }
    else if (bnd->type == XBOUND_TYPE_BOX)
    {
        bnd->box.center.y += 1000000.0f;
    }
}

S32 zNMECommon::HaveLOSToPos(xVec3* pos, F32 dist, xScene* xscn, xBase* tgt, xCollis* colCallers)
{
    S32 result;
    xRay3 ray = {};
    xVec3 mypos = {};
    xCollis* colrec;

    if (colCallers != NULL)
    {
        colrec = colCallers;
    }
    else
    {
        static xCollis localCollis = { k_HIT_0xF00 | k_HIT_CALC_HDNG };
        memset(&localCollis, 0, sizeof(localCollis));
        localCollis.flags = k_HIT_0xF00 | k_HIT_CALC_HDNG;
        colrec = &localCollis;
    }

    xVec3Copy(&mypos, xEntGetCenter(this));

    NMEC_xBoundAway(&bound);

    ray.min_t = 0.0f;
    ray.max_t = dist;
    xVec3Sub(&ray.dir, pos, &mypos);
    xVec3Normalize(&ray.dir, &ray.dir);
    xVec3Copy(&ray.origin, &mypos);
    ray.flags = XRAY3_USE_MIN | XRAY3_USE_MAX;
    xRayHitsScene(xscn, &ray, colrec);

    NMEC_xBoundBack(&bound);

    if (!(colrec->flags & k_HIT_IT))
    {
        result = 1;
    }
    else if (colrec->dist > dist)
    {
        result = 1;
    }
    else if (tgt != NULL && colrec->oid != 0)
    {
        if (tgt->id == colrec->oid)
        {
            result = 1;
        }
        else
        {
            result = 0;
        }
    }
    else
    {
        result = 0;
    }

    return result;
}

F32 zNMECommon::MatQRotTowards(F32 dt, const xVec3* dir_face, F32 spd_turnrate)
{
    xMat3x3 tmpmat;
    xVec3 dir = -*dir_face;
    xQuat quat;
    xQuat qold;
    F32 qdot;
    F32 rotang;

    xMat3x3LookVec(&tmpmat, &dir);

    spd_turnrate = (spd_turnrate > 0.0f) ? spd_turnrate : PI;

    xQuatFromMat(&quat, &tmpmat);
    xQuatFromMat(&qold, (xMat3x3*)model->Mat);
    xQuatNormalize(&quat, &quat);
    xQuatNormalize(&qold, &qold);

    qdot = xQuatDot(&quat, &qold);
    if (qdot < 0.0f)
    {
        xQuatFlip(&quat, &quat);
        qdot = -qdot;
    }

    if (qdot > 1.0f)
    {
        qdot = 1.0f;
    }

    rotang = 2.0f * xacos(qdot);
    if (rotang <= spd_turnrate * dt)
    {
        *(xMat3x3*)model->Mat = tmpmat;
    }
    else
    {
        xQuatSlerp(&quat, &qold, &quat, (spd_turnrate * dt) / rotang);
        xQuatToMat(&quat, (xMat3x3*)model->Mat);
    }

    return qdot;
}

F32 zNMECommon::MatTurnTowardPos(const xVec3* pos, F32 dt, F32 spd_turnrate, S32 flags)
{
    xVec3 dir = *pos - *(xVec3*)&model->Mat->pos;
    F32 dist;

    if (flags)
    {
        dir.y = 0.0f;
    }

    dist = dir.length();
    if (dist < 0.001f)
    {
        return dist;
    }

    dir /= dist;
    MatQRotTowards(dt, &dir, spd_turnrate);

    return dist;
}

F32 zNMECommon::MatTurnAntiPos(const xVec3* pos, F32 dt, F32 spd_turnrate, S32 flags)
{
    xVec3 dir = *(xVec3*)&model->Mat->pos - *pos;
    F32 dist;

    if (flags)
    {
        dir.y = 0.0f;
    }

    dist = dir.length();
    if (dist < 0.001f)
    {
        return dist;
    }

    dir /= dist;
    MatQRotTowards(dt, &dir, spd_turnrate);

    return dist;
}

S32 zNMECommon::MatMoveToPos(const xVec3* pos_tobe, F32 dt, F32 spd, S32 doTurn, F32 spd_turnrate)
{
    xVec3 pos_dest = *pos_tobe;
    xVec3 dir_dest = pos_dest - *(xVec3*)&model->Mat->pos;
    F32 dist;
    F32 dst_step;

    if (flg_move & 0x2)
    {
        dir_dest.y = 0.0f;
    }

    dist = dir_dest.length();
    dst_step = MAX(0.00001f, spd * dt);

    if (dist < dst_step)
    {
        if (flg_move & 0x2)
        {
            pos_dest.y = model->Mat->pos.y;
        }

        model->Mat->pos.x = pos_dest.x;
        model->Mat->pos.y = pos_dest.y;
        model->Mat->pos.z = pos_dest.z;
        return 1;
    }

    dir_dest /= dist;

    if (doTurn)
    {
        MatQRotTowards(dt, &dir_dest, spd_turnrate);
    }

    xVec3 delta = dir_dest * dst_step;
    model->Mat->pos.x += delta.x;
    model->Mat->pos.y += delta.y;
    model->Mat->pos.z += delta.z;
    return 0;
}

// Retail calls AnimStart at all 128 call sites. The body has 28 statements, so two compiled-out
// placeholders (conditions unknown) keep GC/2.6 from auto-inlining it (>= 30 statements).
S32 zNMECommon::AnimStart(U32 animID, S32 forceRestart)
{
    static S32 dumptable = 0;
    xAnimTransition* da_tran;

    if (dumptable)
    {
        dumptable = 0;
        xASSERT_PLACEHOLDER();
    }

    // not in the DWARF (neither is BfBB's zNPCCommon::AnimStart's, which has it)
    xAnimState* state = AnimCurState();
    if (state != NULL && state->ID == animID && !forceRestart)
    {
        return state->ID;
    }

    da_tran = model->Anim->Table->TransitionList;
    while (da_tran != NULL)
    {
        if (da_tran->Dest->ID == animID)
        {
            break;
        }
        da_tran = da_tran->Next;
    }

    if (da_tran != NULL)
    {
        xAnimPlayStartTransition(model->Anim, da_tran);
    }
    else
    {
        da_tran = AnimMakeSmackTran(animID);
        xASSERT_PLACEHOLDER();
        if (da_tran != NULL)
        {
            xAnimPlayStartTransition(model->Anim, da_tran);
        }
    }

    state = AnimCurState();
    if (state->UserFlags & 0x2000000)
    {
        // not in the DWARF: retail reads the single before the xurand call
        xAnimSingle* single = AnimCurSingle();
        single->CurrentSpeed = state->Speed * (0.25f * (xurand() - 0.5f)) + state->Speed;
    }

    if (da_tran != NULL)
    {
        return da_tran->Dest->ID;
    }

    return 0;
}

// No GC symbol: auto-inlined into AnimStart, its only caller (out of line in the PS2 demo).
xAnimTransition* zNMECommon::AnimMakeSmackTran(U32 animID) const
{
    xAnimTransition* atran;
    // not in the DWARF: the destination state found in the table
    xAnimState* ast = xAnimTableGetStateID(model->Anim->Table, animID);

    if (ast == NULL)
    {
        return NULL;
    }

    atran = xAnimTempTransitionAlloc(NULL);
    if (atran == NULL)
    {
        return NULL;
    }

    atran->Dest = ast;
    atran->Callback = NULL;
    atran->SrcTime = 0.0f;
    atran->DestTime = 0.0f;
    if (ast->UserFlags & 0x4000000)
    {
        atran->BlendRecip = 0.0f;
    }
    else
    {
        atran->BlendRecip = 5.0f;
    }
    atran->BlendOffset = NULL;

    return atran;
}

// The body is empty in retail (a lone blr). 30 compiled-out placeholders keep GC/2.6 from
// auto-inlining it (>= 30 statements), as retail calls it from all 14 CreateAnimTable callers. HI's
// real body (BfBB's NPCC_BuildStandardAnimTran loops) cannot be recovered.
void NMEC_BuildStandardAnimTran(xAnimTable* table, char** names, S32* anims, S32 flags,
                                F32 blendTime)
{
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

xAnimTable* zNMECommon::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMECommon", 0);
    }

    xAnimTableNewState(table, "Idle01", 0x110, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    return table;
}

// No GC symbol: always inlined into its callers (the Zap/Merv/Fling move goals).
F32 zNMECommon::ThrottleAdjust(F32 dt, F32 spd_want, F32 accel)
{
    F32 acc;

    if (accel < 0.0f)
    {
        acc = dt * cfg_common->movement.acc_move;
    }
    else
    {
        acc = dt * accel;
    }

    if (xeq(spd_want, spd_throttle, acc))
    {
        spd_throttle = spd_want;
    }
    else if (spd_want > spd_throttle)
    {
        spd_throttle += acc;
        spd_throttle = CLAMP(spd_throttle, 0.0f, spd_want);
    }
    else
    {
        spd_throttle -= acc;
        spd_throttle = MAX(spd_want, spd_throttle);
    }

    return spd_throttle;
}

void zNMECommon::DieTheGoodDeath()
{
    DeathFXKick();
}

void zNMEFragCB(zFrag* frag, zFragAsset* asset)
{
    if (asset->type == eFragProjectile)
    {
        frag->info.projectile.scale = Fogger::Cheat::scl_cheat.x;
    }
}

void zNMECommon::DeathFXKick()
{
    zShrapnelAsset* shrp = shrapnelAsset;

    if (shrp == NULL)
    {
        shrp = zNMEShrapnelRetrieveShrapnel(SelfType());
    }

    if (shrp != NULL)
    {
        xVec3 vel = {};

        if (zUICustomExtrasManager::IsCheatEnabled(eExtrasScreenCheats_TinyFoggers) &&
            (itemType == NME_TYPE_FOGGER_V1 || itemType == NME_TYPE_FOGGER_V2 ||
             itemType == NME_TYPE_FOGGER_V3))
        {
            shrp->initCB(shrp, model, &vel, zNMEFragCB);
        }
        else
        {
            shrp->initCB(shrp, model, &vel, NULL);
        }
    }

    SndPlayFromTable(4); // eNMESound_Death
    PoofOfBubbles();
    zNMERumblePlay(this);
}

void zNMECommon::GiveReward() const
{
    globals.player.rewardsMgr->GiveReward((en_npctyp)itemType, *xEntGetCenter(this));
}

S32 zNMECommon::LaunchProjectile(en_haztyp haztyp, F32 spd_proj, F32 dst_minRange,
                                 const xVec3* pos_launch, F32 tym_predictMax, F32 hyt_offset,
                                 F32 rad_min, F32 rad_max)
{
    zNMEHazard* haz;
    xVec3 dir;
    xVec3 pos_tgt;
    F32 tym_predict;
    F32 dst_tgt;

    haz = HAZ_Acquire();
    if (haz == NULL)
    {
        return 0;
    }

    if (haz->ConfigHelper(haztyp))
    {
        haz->npc_owner = this;
    }
    else
    {
        haz->Discard();
        return 0;
    }

    if (rad_min > 0.0f)
    {
        if (rad_min >= 0.0f)
        {
            haz->custdata.typical.rad_min = rad_min;
        }
        if (rad_max >= 0.0f)
        {
            haz->custdata.typical.rad_max = rad_max;
        }
        haz->custdata.typical.rad_cur = haz->custdata.typical.rad_min;
    }

    dst_tgt = xsqrt(XYZDstSqToPlayer(NULL));
    tym_predict = dst_tgt / MAX(spd_proj, 0.25f);
    tym_predict = MIN(tym_predict, tym_predictMax);
    zEntPlayer_PredictPos(&pos_tgt, tym_predict, 1.0f, 1);

    if (XZDstSqToPlayer(NULL, NULL) < XZDstSqToPos(&pos_tgt, NULL, NULL))
    {
        xVec3Copy(&pos_tgt, xEntGetPos(xglobals->___player_ent_dont_use_directly));
    }
    pos_tgt.y += hyt_offset;

    dst_tgt = xsqrt(XZDstSqToPos(&pos_tgt, &dir, NULL));
    if (dst_tgt < 0.00001f)
    {
        xVec3SMul(&pos_tgt, (xVec3*)&model->Mat->at, dst_minRange);
        xVec3AddTo(&pos_tgt, (xVec3*)&model->Mat->pos);
        dst_tgt = dst_minRange;
    }
    else
    {
        dir /= dst_tgt;
        if (dst_tgt < dst_minRange || xVec3Dot(&dir, (xVec3*)&model->Mat->at) < 0.4f)
        {
            xVec3SMul(&pos_tgt, &dir, dst_minRange);
            xVec3AddTo(&pos_tgt, (xVec3*)&model->Mat->pos);
            dst_tgt = dst_minRange;
        }
    }

    xVec3Copy(&haz->custdata.lobdata.pos_tgt, &pos_tgt);
    tym_predict = MAX(0.25f, dst_tgt / spd_proj);
    haz->Start(pos_launch, tym_predict);
    return 1;
}

en_nmevar NMEVariantIdx(en_npctyp ntyp)
{
    en_nmevar nvar = NME_VAR_NONE;

    switch (ntyp)
    {
    case NME_TYPE_TURBARREL_V1:
    case NME_TYPE_FOGGER_V1:
    case NME_TYPE_SLAMMER_V1:
    case NME_TYPE_FLINGER_V1:
    case NME_TYPE_SPINNER_V1:
    case NME_TYPE_POPPER_V1:
    case NME_TYPE_ZAPPER_V1:
    case NME_TYPE_MERVYN_V1:
    case NME_TYPE_BUCKOTRON_V1:
    case NME_TYPE_DENNIS_V1:
        nvar = NME_VAR_1;
        break;
    case NME_TYPE_TURBARREL_V2:
    case NME_TYPE_FOGGER_V2:
    case NME_TYPE_SLAMMER_V2:
    case NME_TYPE_FLINGER_V2:
    case NME_TYPE_SPINNER_V2:
    case NME_TYPE_POPPER_V2:
    case NME_TYPE_ZAPPER_V2:
    case NME_TYPE_MERVYN_V2:
    case NME_TYPE_BUCKOTRON_V2:
    case NME_TYPE_DENNIS_V2:
        nvar = NME_VAR_2;
        break;
    case NME_TYPE_TURBARREL_V3:
    case NME_TYPE_FOGGER_V3:
    case NME_TYPE_SLAMMER_V3:
    case NME_TYPE_FLINGER_V3:
    case NME_TYPE_SPINNER_V3:
    case NME_TYPE_POPPER_V3:
    case NME_TYPE_ZAPPER_V3:
    case NME_TYPE_MERVYN_V3:
    case NME_TYPE_BUCKOTRON_V3:
        nvar = NME_VAR_3;
        break;
    case NME_TYPE_BUCKOTRON_V4:
        nvar = NME_VAR_4;
        break;
    case NME_TYPE_BUCKOTRON_V5:
        nvar = NME_VAR_5;
        break;
    case NME_TYPE_BUCKOTRON_V6:
        nvar = NME_VAR_6;
        break;
    case NME_TYPE_BUCKOTRON_V7:
        nvar = NME_VAR_7;
        break;
    default:
        nvar = NME_VAR_NONE;
        break;
    }

    return nvar;
}

S32 NMEC_DmgAmtForAttack(en_npcdmg dmgtyp, en_plyrpup* pup_level)
{
    en_plyratak plyratak = PLYR_ATAK_NONE;
    S32 notPlayerAttack = 0;
    en_plyrpup pup;

    switch (dmgtyp)
    {
    case DMGTYP_SIDE:
        if (gCurrentPlayer == eCurrentPlayerSpongeBob)
        {
            plyratak = PLYR_ATAK_SB_KARATE;
        }
        else
        {
            plyratak = PLYR_ATAK_PAT_BELLY;
        }
        break;
    case DMGTYP_ABOVE:
        plyratak = PLYR_ATAK_PAT_FLOP;
        break;
    case DMGTYP_BELOW:
        plyratak = PLYR_ATAK_SB_BASH;
        break;
    case DMGTYP_INSTAKILL:
        return 1000000;
    case DMGTYP_WOUNDEVENT:
        return 0;
    case DMGTYP_KILLEVENT:
        return 1000000;
    case DMGTYP_HITBYTOSS:
        plyratak = PLYR_ATAK_PAT_THROW;
        break;
    case DMGTYP_NMEATTACK:
    case DMGTYP_ROPE:
        notPlayerAttack = 1;
        break;
    case DMGTYP_CRUISEBUBBLE:
        plyratak = PLYR_ATAK_SB_GUITAR;
        break;
    case DMGTYP_FRIENDLYFIRE:
        notPlayerAttack = 1;
        break;
    case DMGTYP_REFLECTED:
        if (gCurrentPlayer == eCurrentPlayerSpongeBob)
        {
            plyratak = PLYR_ATAK_SB_KARATE;
        }
        else if (gCurrentPlayer == eCurrentPlayerPatrick)
        {
            plyratak = PLYR_ATAK_PAT_BELLY;
        }
        else
        {
            plyratak = PLYR_ATAK_SB_KARATE;
        }
        break;
    case DMGTYP_BOULDER:
        notPlayerAttack = 1;
        break;
    case DMGTYP_BUBBOWL:
        plyratak = PLYR_ATAK_SB_BOWL;
        break;
    case DMGTYP_BBOWLZAP:
        notPlayerAttack = 1;
        break;
    case DMGTYP_THUNDER_TIKI_EXPLOSION:
        return 3;
    case DMGTYP_DAMAGE_SURFACE:
    case DMGTYP_SURFACE:
        return 1000000;
    case DMGTYP_BUNGEED:
        notPlayerAttack = 1;
        break;
    case DMGTYP_CARTWHEEL:
        plyratak = PLYR_ATAK_PAT_CARTWHEEL;
        break;
    case DMGTYP_SLIDE:
        plyratak = PLYR_ATAK_SLIDE;
        break;
    case DMGTYP_NMEKNOCKED:
    case DMGTYP_EXPLOSION:
        notPlayerAttack = 1;
        break;
    }

    if (notPlayerAttack)
    {
        return 1;
    }

    pup = zEntPlayer_PowerupLevel(plyratak);
    if (pup_level != NULL)
    {
        *pup_level = pup;
    }

    return DmgAmtForPlayerAttack(plyratak, pup);
}

S32 DmgAmtForPlayerAttack(en_plyratak plyratak, en_plyrpup pup)
{
    return NME::DamageTable::dmg_table[plyratak][pup];
}

// No GC symbol: always inlined (zNMEGoalJellyDying::Enter/Exit, DeathFXKick).
void zNMECommon::PoofOfBubbles() const
{
    if (xModelIsPreinstanced(model))
    {
        xSphere emitSphere;
        xVec3 pos_emit;

        xBoundGetSphere(emitSphere, bound);
        for (S32 i = 0; i < 32; i++)
        {
            xSphereRandomPointOnSphere(&emitSphere, pos_emit);
            zFX_SpawnBubbleTrail(&pos_emit, 3);
        }
    }
    else
    {
        S32 num_vert;
        S32 stride; // not in the PS2 DWARF, which lists no third local in this scope
        xVec3 pos_emit;

        num_vert = model->Data->geometry->numVertices;
        if (num_vert < 1)
        {
            return;
        }

        stride = MAX(1, (S32)(num_vert / 32.0f));
        for (S32 i = 0; i < num_vert; i += stride)
        {
            iModelVertEval(model->Data, i, 1, model->Mat, NULL, &pos_emit);
            zFX_SpawnBubbleTrail(&pos_emit, 3);
        }
    }
}
