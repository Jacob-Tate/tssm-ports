#include "zNMEHazard.h"
#include "xordarray.h"
#include "xRand.h"
#include "xModel.h"
#include "xMathInlines.h"
#include "xVec3Inlines.h"
#include "xFXHighDynamicRange.h"
#include "zRenderState.h"
#include "xDebug.h"

#include <string.h>

extern _tagGameVidMode gVidMode;

// __sinit_zWadNME_cpp ('hazards[0].typ_hazard = 0') comes from BfBB's two zNMEHazard constructors
// and the initializer below. Retail places it after every other function of the unit, which the
// invented out-of-line ~xListItem() (xListItem.h) gives.
namespace GlobalData
{
    static zNMEHazard hazards[128] = { HAZ_TYP_UNKNOWN };
    static st_XORDEREDARRAY workinglistofhazards;
    static S32 cnt_activehaz;
    static zNMEHazardType* haztypeHandlers[HAZ_TYP_NOMORE] = {};
    static zNMEHazard* haz_uvAnimQue[HAZ_TYP_NOMORE] = {};
} // namespace GlobalData

struct zMODHazard : zModule
{
    void ModulePrepUse();
    void ScenePrepare();
    void SceneFinish();
    void SceneReset();
    void ScenePostInit();
    void Timestep(F32 dt);
    void Render()
    {
        RenderGenerally(1);
    }
    void PostRender()
    {
        RenderGenerally(0);
    }
    void RenderGenerally(S32 doOpaqueStuff);
};

// No GC symbol: inlined into zNMECentral_Startup (its unused copy is dead-stripped).
void zNMEHazard_Startup()
{
    static zMODHazard mod_Hazard;

    zNMECentral_RegisterModule(&mod_Hazard, 'NHAZ');
    HAZ_RegisterTypes();
}

// No GC symbol (always inlined into zNMECentral_Shutdown).
inline void zNMEHazard_Shutdown()
{
    XOrdDone(&GlobalData::workinglistofhazards, 0);
}

void zMODHazard::ModulePrepUse()
{
    flg_updateInCinematic = 1;
    XOrdInit(&GlobalData::workinglistofhazards, 0x80, 0);
}

void zMODHazard::ScenePrepare()
{
    S32 i;

    memset(GlobalData::hazards, 0, sizeof(GlobalData::hazards));
    XOrdReset(&GlobalData::workinglistofhazards);
    GlobalData::cnt_activehaz = 0;
    for (i = 0; i < HAZ_TYP_NOMORE; i++)
    {
        GlobalData::haz_uvAnimQue[i] = NULL;
    }
    for (i = 0; i < HAZ_TYP_NOMORE; i++)
    {
        GlobalData::haztypeHandlers[i]->ScenePrepare();
    }
}

void zMODHazard::SceneFinish()
{
    SceneReset();
    for (S32 i = 0; i < HAZ_TYP_NOMORE; i++)
    {
        GlobalData::haztypeHandlers[i]->SceneFinish();
    }
    memset(GlobalData::hazards, 0, sizeof(GlobalData::hazards));
    GlobalData::cnt_activehaz = 0;
}

void zMODHazard::SceneReset()
{
    for (S32 i = 0; i < 128; i++)
    {
        if (GlobalData::hazards[i].flg_hazard)
        {
            GlobalData::hazards[i].Kill();
        }
    }
    GlobalData::cnt_activehaz = 0;
}

void zMODHazard::ScenePostInit()
{
    HAZ_PostInit_Shrapnel();
    HAZ_TypeTweaks("NMEHazard");
    HAZ_PostInit_Sounds();

    for (S32 i = 0; i < HAZ_TYP_NOMORE; i++)
    {
        GlobalData::haztypeHandlers[i]->ScenePostInit();
    }
}

static S32 HAZ_ord_sorttest(void* vkey, void* vitem)
{
    zNMEHazard* key = (zNMEHazard*)vkey;
    zNMEHazard* item = (zNMEHazard*)vitem;

    if (key->typ_hazard < item->typ_hazard)
    {
        return -1;
    }
    else if (key->typ_hazard > item->typ_hazard)
    {
        return 1;
    }
    else if ((SPtr)vkey < (SPtr)vitem)
    {
        return -1;
    }
    return 1;
}

// No symbol (always inlined); the name is ours. zMODHazard::Timestep clamps pam_interp through
// an inline helper: the PS2 DWARF lists no local for the unclamped value.
static inline F32 HAZ_clamp(F32 pam)
{
    return CLAMP(pam, 0.0f, 1.0f);
}

void zMODHazard::Timestep(F32 dt)
{
    st_XORDEREDARRAY* hazlist;

    if (GlobalData::cnt_activehaz < 1)
    {
        return;
    }

    hazlist = &GlobalData::workinglistofhazards;
    XOrdReset(hazlist);
    for (S32 i = 0; i < 128; i++)
    {
        zNMEHazard* haz = &GlobalData::hazards[i];
        if (haz->flg_hazard & 1)
        {
            XOrdAppend(hazlist, haz);
        }
    }
    XOrdSort(hazlist, HAZ_ord_sorttest);

    for (S32 i = 0; i < hazlist->cnt; i++)
    {
        zNMEHazard* haz = (zNMEHazard*)hazlist->list[i];

        if (haz->flg_hazard & 4)
        {
            haz->Discard();
            continue;
        }

        if (!(haz->flg_hazard & 2))
        {
            continue;
        }

        if (haz->flg_hazard & 0x80)
        {
            haz->pam_interp = HAZ_clamp(1.0f - haz->tmr_remain / haz->tym_lifespan);

            if (haz->mdl_hazard != NULL && (haz->flg_hazard & 0x4000) && (haz->flg_hazard & 0x8000))
            {
                haz->TypData_RotMatApply(&haz->custdata.typical.mat_rotDelta);
            }

            GlobalData::haztypeHandlers[haz->typ_hazard]->Update(haz, dt);

            if (haz->mdl_hazard != NULL)
            {
                xVec3Copy((xVec3*)&haz->mdl_hazard->Mat->pos, &haz->pos_hazard);
            }
        }

        if (!(haz->flg_hazard & 0x20))
        {
            haz->flg_hazard &= ~8;
        }
        haz->flg_hazard &= ~0x60;

        haz->tmr_remain = MAX(-1.0f, haz->tmr_remain - dt);

        if ((haz->flg_hazard & 0x1000) && haz->tmr_remain < 0.0f)
        {
            if (haz->flg_hazard & 0x10000000)
            {
                haz->flg_hazard |= 4;
            }
            else
            {
                haz->flg_hazard |= 0x10000000;
                haz->tmr_remain = 0.0f;
            }
        }

        if (haz->flg_hazard & 4)
        {
            haz->Discard();
            continue;
        }

        if (haz->flg_hazard & 0x100000)
        {
            GlobalData::haz_uvAnimQue[haz->typ_hazard] = haz;
        }
    }

    XOrdReset(hazlist);

    for (S32 i = 0; i < HAZ_TYP_NOMORE; i++)
    {
        if (GlobalData::haz_uvAnimQue[i] != NULL)
        {
            if (GlobalData::haztypeHandlers[GlobalData::haz_uvAnimQue[i]->typ_hazard]->uvAnimInfo !=
                NULL)
            {
                GlobalData::haztypeHandlers[GlobalData::haz_uvAnimQue[i]->typ_hazard]
                    ->uvAnimInfo->Update(dt, NULL);
            }
            GlobalData::haz_uvAnimQue[i] = NULL;
        }
    }
}

namespace HAZAlphaDiscard
{
    static U8 g_startAlphaDiscard;
    static U8 g_lastAlphaDiscard;

    // No symbol (always inlined); the name is ours. GameCube version of the PS2 build's GS alpha
    // test update: discard pixels below the reference alpha (and test Z after texturing) when
    // ref is non-zero, otherwise turn the alpha test off.
    inline void Set(U8 ref)
    {
        if (ref != g_lastAlphaDiscard)
        {
            if (ref)
            {
                RwGameCubeSetAlphaCompare(7, 0, 0, 6, ref);
                _rwDlRenderStateSetZCompLoc(FALSE);
            }
            else
            {
                RwGameCubeSetAlphaCompare(6, 0, 0, 7, 0);
                _rwDlRenderStateSetZCompLoc(TRUE);
            }
            g_lastAlphaDiscard = ref;
        }
    }
} // namespace HAZAlphaDiscard

void zMODHazard::RenderGenerally(S32 doOpaqueStuff)
{
    S32 i;
    st_XORDEREDARRAY* hazlist;
    _SDRenderState old_rendstat;
    zNMEHazard* haz;
    S32 resetBlend;
    S32 resetHDR;

    if (GlobalData::cnt_activehaz < 1)
    {
        return;
    }

    hazlist = &GlobalData::workinglistofhazards;

    for (i = 0; i < 128; i++)
    {
        if (!(GlobalData::hazards[i].flg_hazard & 1))
        {
            continue;
        }
        if (!(GlobalData::hazards[i].flg_hazard & 2))
        {
            continue;
        }
        if (!(GlobalData::hazards[i].flg_hazard & 0x100))
        {
            continue;
        }
        if (GlobalData::hazards[i].flg_hazard & 0xc)
        {
            continue;
        }
        if (doOpaqueStuff && !(GlobalData::hazards[i].flg_hazard & 0x200))
        {
            continue;
        }
        if (!doOpaqueStuff && (GlobalData::hazards[i].flg_hazard & 0x200))
        {
            continue;
        }
        XOrdAppend(hazlist, &GlobalData::hazards[i]);
    }

    XOrdSort(hazlist, HAZ_ord_sorttest);

    old_rendstat = zRenderStateCurrent();
    if (doOpaqueStuff)
    {
        zRenderState(SDRS_NMEHazardOpaque);
    }
    else
    {
        zRenderState(SDRS_NMEHazardAlpha);
    }

    for (i = 0; i < hazlist->cnt; i++)
    {
        haz = (zNMEHazard*)hazlist->list[i];

        resetBlend = 0;
        if (haz->flg_hazard & 0x400)
        {
            resetBlend = 1;
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
        }

        resetHDR = 0;
        if (haz->flg_hazard & 0x3000000)
        {
            xFXHighDynamicRangeBrighten(true);
            resetHDR = 1;
            resetBlend = 1;
            HAZAlphaDiscard::Set(1);
            if (haz->flg_hazard & 0x1000000)
            {
                xFXHighDynamicRangeSetGlowClamp(xFXHighDynamicRangeGetBackgroundGlow(), false);
                RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
            }
            else
            {
                xFXHighDynamicRangeSetGlowClamp(xFXHighDynamicRangeGetBackgroundGlow(), false);
                RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
            }
        }
        else
        {
            HAZAlphaDiscard::Set(HAZAlphaDiscard::g_startAlphaDiscard);
        }

        if (!(haz->flg_hazard & 0x200))
        {
            RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLFRONT);
            GlobalData::haztypeHandlers[haz->typ_hazard]->Render(haz);
            RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
            GlobalData::haztypeHandlers[haz->typ_hazard]->Render(haz);
        }
        else
        {
            GlobalData::haztypeHandlers[haz->typ_hazard]->Render(haz);
        }

        if (resetHDR)
        {
            xFXHighDynamicRangeBrighten(false);
            xFXHighDynamicRangeResetGlowClamp();
        }

        if (resetBlend)
        {
            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
        }

        RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
        haz->flg_hazard &= ~0x10;
    }

    XOrdReset(hazlist);
    zRenderState(old_rendstat);
}

// The PS2 DWARF lists only da_haz and i; the block-scope 'haz' is needed to get retail's NULL
// preload (da_haz as the result) with a strength-reduced &hazards[i].
zNMEHazard* HAZ_Acquire()
{
    zNMEHazard* da_haz = NULL;

    for (S32 i = 0; i < 128; i++)
    {
        zNMEHazard* haz = &GlobalData::hazards[i];
        if (!(haz->flg_hazard & 1))
        {
            memset(haz, 0, sizeof(zNMEHazard));
            haz->flg_hazard = 1;
            GlobalData::cnt_activehaz++;
            da_haz = haz;
            break;
        }
    }

    return da_haz;
}

void HAZ_Iterate(bool (*fp)(zNMEHazard&, void*), void* context, S32 flag_filter)
{
    zNMEHazard* haz = GlobalData::hazards;
    zNMEHazard* end = GlobalData::hazards + 128;
    S32 rc;

    for (; haz != end; haz++)
    {
        if (haz->flg_hazard & 0xc)
        {
            continue;
        }
        if ((haz->flg_hazard & 3) != 3)
        {
            continue;
        }
        if ((flag_filter & haz->flg_hazard) != flag_filter)
        {
            continue;
        }
        rc = fp(*haz, context);
        if (!rc)
        {
            break;
        }
    }
}

// Invented helper (no symbol in any build): the type handler of a hazard type. As an inline's
// returned load it becomes a temporary that ConfigHelper's hth coalesces into, which FlingBlob's
// EndOfLifespan needs.
static inline zNMEHazardType* HAZ_TypeHandler(en_haztyp haztype)
{
    return GlobalData::haztypeHandlers[haztype];
}

S32 zNMEHazard::ConfigHelper(en_haztyp haztype)
{
    // not in the DWARF: the handler pointer is used twice (Configure and the warning flag).
    zNMEHazardType* hth;
    S32 result;

    typ_hazard = haztype;
    hth = HAZ_TypeHandler(typ_hazard);
    result = hth->Configure(this);
    if (!result)
    {
        hth->flg_warnedOfBadConfigure = 1;
        flg_hazard |= 4;
    }

    return result;
}

S32 zNMEHazard::Reconfigure(en_haztyp haztype)
{
    HAZNotify* noter = cb_notify;
    zNMECommon* npc_old = npc_owner;
    xVec3 pos_old;
    S32 result;

    xVec3Copy(&pos_old, &pos_hazard);
    GlobalData::haztypeHandlers[typ_hazard]->Cleanup(this);
    memset(this, 0, sizeof(zNMEHazard));
    flg_hazard = 0x21;

    result = ConfigHelper(haztype);
    if (result)
    {
        PosSet(&pos_old);
        cb_notify = noter;
        npc_owner = npc_old;
        if (cb_notify != NULL)
        {
            cb_notify->Notify(HAZ_NOTE_RECONFIG, this);
        }
    }

    return result;
}

// No GC symbol (always inlined into the hazard types' Configure functions). The en_haztyp
// parameter is unused (PS2 symbol GrabModel__10zNMEHazardF9en_haztyp; callers pass their typ_hazard).
inline S32 zNMEHazard::GrabModel(en_haztyp)
{
    zNMEHazardType* hth;
    RpAtomic* raw_model;

    if (mdl_hazard != NULL)
    {
        if (mdl_hazard != NULL)
        {
            xModelInstanceFree(mdl_hazard);
        }
        mdl_hazard = NULL;
    }

    hth = GlobalData::haztypeHandlers[typ_hazard];
    raw_model = hth->rawModel;
    if (raw_model == NULL)
    {
        return 0;
    }

    mdl_hazard = xModelInstanceAlloc(raw_model, NULL, 0, 0, NULL);
    if (mdl_hazard == NULL)
    {
        flg_hazard &= ~0x100;
        return 0;
    }

    xVec3 ang_orient = { 0.0f, 0.0f, 0.0f };
    xMat4x3 frame;

    xMat3x3Euler(&frame, &ang_orient);
    frame.pos = g_O3;
    frame.flags = 0;
    xModelSetFrame(mdl_hazard, &frame);

    if (flg_hazard & 0x800)
    {
        mdl_hazard->LightKit = NULL;
    }
    else
    {
        mdl_hazard->LightKit = xglobals->___player_ent_dont_use_directly->model->LightKit;
    }

    return mdl_hazard != NULL;
}

void zNMEHazard::Discard()
{
    if (flg_hazard & 1)
    {
        if (cb_notify != NULL)
        {
            cb_notify->Notify(HAZ_NOTE_DISCARD, this);
        }
        GlobalData::haztypeHandlers[typ_hazard]->Cleanup(this);
        GlobalData::cnt_activehaz--;
        GlobalData::cnt_activehaz &= ~(GlobalData::cnt_activehaz >> 31);
    }
}

// 'if (pos)' rather than 'pos != NULL': CW folds only the former when the argument is the address
// of a local (retail zNMEGoalPopAttack::PopGunUpdate inlines Start without the check).
void zNMEHazard::Start(const xVec3* pos, F32 tym)
{
    if (tym > 0.0f)
    {
        tmr_remain = tym;
    }
    tym_lifespan = tmr_remain;
    if (pos)
    {
        PosSet(pos);
    }
    flg_hazard |= 0x1a;
}

void zNMEHazard::PosSet(const xVec3* pos)
{
    if (pos)
    {
        xVec3Copy(&pos_hazard, pos);
    }
    if (mdl_hazard != NULL)
    {
        xVec3Copy((xVec3*)&mdl_hazard->Mat->pos, &pos_hazard);
    }
}

// spd is not in the PS2 DWARF, but both builds compute the speed once and use it in both branches.
// 5 compiled-out placeholders (conditions unknown) keep GC/2.6 from auto-inlining this (>= 30
// statements), as retail calls it.
S32 zNMEHazard::ColTestCapsule(const xBound* bnd_tgt, const xVec3* pos, F32 radiusCaller,
                               const xVec3* vel, F32 tym, S32 isPlayer)
{
    xVec3 pos_from;

    radiusCaller = CLAMP(radiusCaller, 0.1f, 25.0f);

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (pos != NULL)
    {
        pos_from = *pos;
    }
    else
    {
        pos_from = pos_hazard;
    }
    xVec3 pos_dest = pos_from + *vel * tym;

    xASSERT_PLACEHOLDER();

    xSweptSphere sws;
    memset(&sws, 0, sizeof(xSweptSphere));
    xSweptSpherePrepare(&sws, &pos_from, &pos_dest, radiusCaller);
    S32 rc = xSweptSphereToBound(&sws, bnd_tgt);
    if (!rc)
    {
        return 0;
    }

    xSweptSphereGetResults(&sws);

    F32 spd = xVec3Length(vel);
    if (spd < 0.00001f)
    {
        if (spd * tym < sws.dist)
        {
            return 0;
        }

        xASSERT_PLACEHOLDER();
        GlobalData::haztypeHandlers[typ_hazard]->CollideResponse(this, isPlayer ? 0x10 : 0x8, &sws,
                                                                 0.0f);
        return 1;
    }

    F32 dst_effective = sws.curdist;
    if (isPlayer && bnd_tgt->type == XBOUND_TYPE_SPHERE)
    {
        dst_effective += bnd_tgt->sph.r;
    }

    F32 tym_inFuture = dst_effective / spd;
    xASSERT_PLACEHOLDER();
    GlobalData::haztypeHandlers[typ_hazard]->CollideResponse(this, isPlayer ? 0x10 : 0x8, &sws,
                                                             tym_inFuture);
    return 1;
}

void zNMEHazard::TypData_RotMatSet(const xMat3x3* mat_rot)
{
    xMat4x3* frame = xModelGetFrame(mdl_hazard);
    xMat3x3Copy(frame, mat_rot);
    xModelSetFrame(mdl_hazard, frame);
}

void zNMEHazard::OrientToDir(const xVec3* vec_path, S32 doTheTwist)
{
    F32 mag;
    xMat3x3 mat_rot;
    xVec3 dir;

    mag = xVec3Length(vec_path);
    if (mag < 0.00001f)
    {
        return;
    }

    xVec3SMul(&dir, vec_path, -1.0f / mag);
    xMat3x3LookVec(&mat_rot, &dir);

    if (doTheTwist)
    {
        F32 tym_live = tym_lifespan - tmr_remain;
        F32 ang_spun = 2 * PI * tym_live;
        xMat3x3 mat_spiral;

        xMat3x3RotC(&mat_spiral, mat_rot.at.x, mat_rot.at.y, mat_rot.at.z, ang_spun);
        xMat3x3Mul(&mat_rot, &mat_rot, &mat_spiral);
        TypData_RotMatSet(&mat_rot);
    }

    TypData_RotMatSet(&mat_rot);
}

// No GC symbol: retail inlines it into every lobbed hazard's Update and dead-strips it (its
// colrec is a plain function static, unlike the weak statics of inline functions).
void zNMEHazard::PreCollide(F32 acc_grav, F32 tym_collDelay)
{
    HAZLob* hazlob = &custdata.lobdata;
    xParabola* parab;
    S32 rc;
    static xCollis colrec;

    tmr_remain += 5.0f;

    parab = hazlob->parabinfo;

    xVec3Copy(&parab->initPos, &pos_hazard);
    xVec3Copy(&parab->initVel, &hazlob->vel);
    parab->gravity = acc_grav;
    parab->minTime = MIN(tym_collDelay, tmr_remain);
    parab->maxTime = tmr_remain;

    memset(&colrec, 0, sizeof(xCollis));
    rc = xParabolaHitsEnv(parab, xglobals->sceneCur->env, &colrec);
    if (rc)
    {
        flg_hazard |= 0x20000;
        xVec3Copy(&hazlob->dir_normal, &colrec.norm);
        parab->maxTime = colrec.dist;
        tmr_remain = colrec.dist;
    }
    else
    {
        flg_hazard &= ~0x20000;
        xVec3Copy(&hazlob->dir_normal, &g_Y3);
    }

    tym_lifespan = tmr_remain;
}

// GlobalData (hazards, haztypeHandlers, ...) is defined in zNMEHazardTypes.cpp, which this
// unity wad includes first.

S32 zNMEHazard::StaggeredCollide()
{
    HAZCollide* hazcol = &custdata.collide;
    static xCollis colrec;

    if (--hazcol->cnt_skipcol > 0)
    {
        return 0;
    }

    hazcol->cnt_skipcol = (xrand_RandomInt32() & 0x800000) ? 6 : 5;

    memset(&colrec, 0, sizeof(colrec));

    switch (hazcol->idx_rotateCol)
    {
    case HAZ_COLTYP_STAT:
        if (hazcol->flg_collide & 1)
        {
            StagColGeneral(1);
        }
        break;
    case HAZ_COLTYP_DYN:
        if (hazcol->flg_collide & 2)
        {
            StagColGeneral(2);
        }
        break;
    case HAZ_COLTYP_NME:
        if (hazcol->flg_collide & 4)
        {
            StagColGeneral(4);
        }
        break;
    }

    hazcol->idx_rotateCol = (en_hazcol)(hazcol->idx_rotateCol + 1);
    if (hazcol->idx_rotateCol >= HAZ_COLTYP_NOMORE)
    {
        hazcol->idx_rotateCol = HAZ_COLTYP_STAT;
    }

    return 0;
}

void zNMEHazard::StagColGeneral(S32 who)
{
    xParabola* parab;
    F32 tym_chkAhead;
    F32 tym_alive;
    F32 tym_ahead;
    xVec3 pos_here;
    xVec3 pos_there;
    xSweptSphere sws;
    S32 rc;
    F32 spd_avg;

    parab = custdata.collide.parabinfo;
    // One statement per PS2 line-table entry.
    tym_chkAhead = 30.0f * (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
    tym_ahead = (gVidMode == eGameVidModePAL ? 0.02f : (1.0f / 60.0f));
    tym_alive = tym_lifespan - tmr_remain;
    tym_alive = MIN(tym_alive, tym_lifespan);
    tym_chkAhead = MIN(tym_alive + tym_chkAhead, tym_lifespan);
    tym_ahead = tym_chkAhead + tym_ahead;
    pos_here = pos_hazard;

    xParabolaEvalPos(parab, &pos_here, tym_alive);
    xParabolaEvalPos(parab, &pos_there, tym_ahead);

    memset(&sws, 0, sizeof(xSweptSphere));
    xSweptSpherePrepare(&sws, &pos_here, &pos_there, 0.1f);

    rc = 0;
    if (who & 3)
    {
        rc = xSweptSphereToStatDyn(&sws, xglobals->sceneCur, NULL, 8, 0);
    }
    else if (who & 4)
    {
        rc = xSweptSphereToNPC(&sws, xglobals->sceneCur, NULL, 8, 0);
    }

    if (rc)
    {
        xSweptSphereGetResults(&sws);

        // not in the DWARF: retail builds two stack xVec3 for the xParabolaEvalVel results (no
        // known inline helper owns them).
        xVec3 vel_here;
        xVec3 vel_there;

        xParabolaEvalVel(parab, &vel_here, tym_alive);
        xParabolaEvalVel(parab, &vel_there, tym_ahead);

        spd_avg = 0.5f * (xVec3Length(&vel_here) + xVec3Length(&vel_there));
        if (spd_avg < 0.00001f)
        {
            if (!(spd_avg * (tym_ahead - tym_alive) < sws.dist))
            {
                GlobalData::haztypeHandlers[typ_hazard]->CollideResponse(this, who, &sws, 0.0f);
            }
        }
        else
        {
            GlobalData::haztypeHandlers[typ_hazard]->CollideResponse(this, who, &sws,
                                                                     sws.curdist / spd_avg);
        }
    }
}
