#ifndef ZNMESUPPORT_H
#define ZNMESUPPORT_H

#include "zNMECentral.h"
#include "xFX.h"
#include "xBound.h"
#include "xEnt.h"
#include "xQuickCull.h"
#include "xstransvc.h"
#include "zNMECommon.h"
#include "zGrid.h"

#include <string.h>

#include <rwcore.h>

struct zMODSupport : zModule
{
    void ScenePrepare();
    void SceneFinish();
    void ScenePostInit();
    void SceneReset();
    void Timestep(F32 dt);
};

struct xBase;
struct xEnt;
struct xCollis;
struct zMovePoint;
struct zNMECommon;

// PS2 DWARF (zWadNME)
enum en_npctgt
{
    NME_TGT_NONE,
    NME_TGT_PLYR,
    NME_TGT_ENT,
    NME_TGT_BASE,
    NME_TGT_POS,
    NME_TGT_MVPT,
    NME_TGT_NOMORE,
    NME_TGT_FORCEINT = 0x7fffffff
};

// Size: 0x14 (PS2 DWARF)
struct NMETarget
{
    en_npctgt typ_target;
    union
    {
        xEnt* ent_target;
        xBase* bas_target;
        xVec3 pos_target;
        zMovePoint* nav_target;
    };
    zNMECommon* npc_owner;

    void PosGet(xVec3* pos);
    void PosProjected(xVec3* pos, F32 tym_project);
    // No PS2 DWARF for DirGet, HaveLOS, InWedge and HitByBound: parameter names follow the
    // neighbouring methods (InWedge's first three floats are passed on to InCylinder).
    F32 DirGet(xVec3* dir, xVec3* pos_from, S32 flg_flat);
    F32 DirVecGet(xVec3* vec, xVec3* pos_from, S32 flg_flat);
    S32 HaveLOS(xVec3* pos_from, F32 dst_max, xCollis* colrec);
    S32 InRange(xVec3* pos_from, F32 dst_range, S32 flg_flat);
    S32 InCylinder(xVec3* pos_from, F32 rad, F32 hyt, F32 off);
    S32 InWedge(xVec3* pos_from, F32 rad, F32 hyt, F32 off, xVec3* dir_wedge, F32 ang);
    S32 HitByBound(xBound* bnd, xCollis* colrec);
};

// PS2 DWARF (zWadNME). No Firework code survives in either build; only the g_fireworks pool of
// zNMESupport.cpp is left in the GC .bss.
enum en_fwstate
{
    FW_STAT_UNUSED,
    FW_STAT_READY,
    FW_STAT_FLIGHT,
    FW_STAT_BOOM,
    FW_STAT_DONE,
    FW_STAT_NOMORE,
    FW_STAT_FORCE = 0x7fffffff
};

enum en_fwstyle
{
    FW_STYL_DEFAULT,
    FW_STYL_JULY4TH,
    FW_STYL_XMAS,
    FW_STYL_STPATS,
    FW_STYL_VALENTINE,
    FW_STYL_NOMORE,
    FW_STYL_FORCE = 0x7fffffff
};

// Size: 0x24 (PS2 DWARF)
struct Firework
{
    en_fwstate fwstate : 8;
    en_fwstyle fwstyle : 8;
    S32 flg_firework : 16;
    F32 tym_lifespan;
    F32 tmr_remain;
    xVec3 pos;
    xVec3 vel;
};

F32 NMEC_ds2_toCam(const xVec3* pos_from, xVec3* delta);
S32 NMEC_LineHitsBound(const xVec3* a, const xVec3* b, xBound* bnd, xCollis* callers_colrec);
F32 NMEC_aimVary(xVec3* dir_aim, const xVec3* pos_src, const xVec3* pos_tgt, F32 dst_vary,
                 S32 flg_vary, xVec3* pos_aimPoint);
S32 NMEC_pos_ofBase(const xBase* tgt, xVec3* pos);
S32 NMEC_bnd_ofBase(const xBase* tgt, xBound* bnd);
S32 NMEC_PointInBound(const xVec3* pos, xBound* bnd, xCollis* colrec);
S32 NMEC_chk_hitBase(xBase* tgt, xBound* bnd, xCollis* collide);

// Inlined through NMEC_chk_hitPlyr (no GC symbol); BfBB's NPCC_chk_hitEnt.
inline S32 NMEC_chk_hitEnt(xEnt* tgt, xBound* bnd, xCollis* collide)
{
    xCollis* colrec;
    xCollis lcl_collide;

    if (collide != NULL)
    {
        colrec = collide;
    }
    else
    {
        colrec = &lcl_collide;
        memset(colrec, 0, sizeof(xCollis));
    }

    colrec->optr = tgt;
    colrec->oid = tgt->id;
    colrec->flags = 0x1f00;

    xQuickCullForEverything(&bnd->qcd);
    xBoundHitsBound(bnd, &tgt->bound, colrec);

    return (colrec->flags & 1) ? 1 : 0;
}

// Inlined (no GC symbol); BfBB's NPCC_chk_hitPlyr. Retail's stack layout in HitByBound needs this
// extra inline level.
inline S32 NMEC_chk_hitPlyr(xBound* bnd, xCollis* collide)
{
    return NMEC_chk_hitEnt(xglobals->___player_ent_dont_use_directly, bnd, collide);
}

// No GC symbol (always inlined; zNMEStandard::DuploOwner); BfBB's NPCC_DstSq.
inline F32 NMEC_DstSq(const xVec3* pos_a, const xVec3* pos_b, xVec3* dir)
{
    xVec3 tmp;

    if (dir == NULL)
    {
        dir = &tmp;
    }

    xVec3Sub(dir, pos_b, pos_a);

    return xVec3Length2(dir);
}

// No symbol (always inlined; the name is ours): the XZ counterpart of NMEC_DstSq, with the same
// optional delta and dy out-pointers as zNMECommon::XZDstSqToPos.
inline F32 XZDstSq(const xVec3* pos_a, const xVec3* pos_b, xVec3* dir, F32* dy)
{
    xVec3 tmp;

    if (dir == NULL)
    {
        dir = &tmp;
    }

    xVec3Sub(dir, pos_b, pos_a);
    if (dy != NULL)
    {
        *dy = dir->y;
    }
    dir->y = 0.0f;

    return xVec3Length2(dir);
}

// No symbol (always inlined): BfBB's NPCC_MakePerp, used the same way by the hazards' Recon*
// functions.
inline void NMEC_MakePerp(xVec3* dir_perp, const xVec3* dir_axis)
{
    dir_perp->x = dir_axis->y - dir_axis->z;
    dir_perp->y = dir_axis->z - dir_axis->x;
    dir_perp->z = dir_axis->x - dir_axis->y;

    xVec3Normalize(dir_perp, dir_perp);
}

RwRaster* NMEC_FindRWRaster(RwTexture* txtr);
RwRaster* NMEC_FindRWRaster(U32 hashid);
RwRaster* NMEC_FindRWRaster(const char* txtrname);
RwTexture* NMEC_FindRWTexture(const char* txtrname);
void NMEC_GenSmooth(xVec3** pos_base, xVec3** pos_mid);

// No symbol (always inlined; the name is ours): finds an asset by ID, NULL when it has no size.
inline void* NMEC_FindAsset(U32 aid)
{
    U32 size = 0;
    void* asset = xSTFindAsset(aid, &size);
    return size ? asset : NULL;
}

// Size: 0x28 (PS2 DWARF)
struct NMELaser
{
    RwRaster* rast_laser;
    F32 radius[2];
    F32 uv_scroll[2];
    RwRGBA rgba[2];
    S32 flg_render;
    F32 uv_base[2];

    void Render(xVec3* pos_src, xVec3* pos_tgt);

    // No GC symbol (always inlined; BfBB's NPCLaser::UVScrollUpdate).
    void UVScrollUpdate(F32 dt)
    {
        for (S32 i = 0; i < 2; i++)
        {
            uv_base[i] += dt * uv_scroll[i];
            while (uv_base[i] > 1.0f)
            {
                uv_base[i] -= 1.0f;
            }
            while (uv_base[i] < -0.0f)
            {
                uv_base[i] += 1.0f;
            }
        }
    }
};

namespace NMEHelpRib
{
    // Ribbon helper data used by NMEs, hazards and cinematic FX tables (size 0x4C).
    struct RibData
    {
        struct Flags
        {
            S8 enable;
            S8 makeAdditive; // 0: alpha blend, 1: additive, 2: additive + flag 0x4
            S8 dev_enable; // re-apply config every update (tweaking)
            S8 unused;
        } flags;
        struct CfgInfo
        {
            char* str_texture;
            xFXRibbon::curve_node* curvedata;
            S32 curvedata_size;
            F32 lifetime;
            F32 pivot;
            xFXRibbon::config_type ignore_local_ribcfg;
        } cfginfo;
        struct UpdateInfo
        {
            S32 frq_ribJoint;
            F32 alf_master;
            F32 scl_master;
        } updinfo;
        struct TxtrXtra
        {
            RwTexture* txtr_texture;
            U32 aid_texture;
        } txtrxtra;

        void FindTexture()
        {
            if (cfginfo.str_texture == NULL)
            {
                txtrxtra.txtr_texture = NULL;
            }
            else if (cfginfo.str_texture[0] == '\0')
            {
                txtrxtra.txtr_texture = NULL;
            }
            else
            {
                txtrxtra.txtr_texture = NMEC_FindRWTexture(cfginfo.str_texture);
            }
        }

        void ConfigRibbon(xFXRibbon* ribbon)
        {
            xFXRibbon::config_type* ribcfg = &cfginfo.ignore_local_ribcfg;

            if (txtrxtra.aid_texture != 0)
            {
                ribcfg->raster = NMEC_FindRWRaster(txtrxtra.aid_texture);
            }
            else
            {
                ribcfg->raster = NMEC_FindRWRaster(txtrxtra.txtr_texture);
            }

            ribcfg->curve = cfginfo.curvedata;
            ribcfg->curve_size = cfginfo.curvedata_size;
            ribcfg->flags = 1;

            if (flags.makeAdditive == 2)
            {
                ribcfg->flags |= 4;
                ribcfg->blend_src = rwBLENDSRCALPHA;
                ribcfg->blend_dst = rwBLENDONE;
            }
            else if (flags.makeAdditive)
            {
                ribcfg->blend_src = rwBLENDSRCALPHA;
                ribcfg->blend_dst = rwBLENDONE;
            }
            else
            {
                ribcfg->blend_src = rwBLENDSRCALPHA;
                ribcfg->blend_dst = rwBLENDINVSRCALPHA;
            }

            ribcfg->life_time = cfginfo.lifetime;
            ribcfg->pivot = cfginfo.pivot;

            ribbon->set_config(ribcfg);
        }
    };
} // namespace NMEHelpRib

struct xBound;
struct zNMECommon;

namespace NMEHelpBound
{
    // Bound helper data (size 0x30, PS2 DWARF).
    struct BndData
    {
        struct Flags
        {
            S8 useModelBind;
            S8 drawBound;
            S8 unused[2];
        };
        struct SphereInfo
        {
            F32 rad_bound;
            xVec3 vec_offset;
        };
        struct BoxInfo
        {
            xVec3 dim_bound;
            xVec3 vec_offset;
        };

        xBoundType useBoundType;
        Flags flags;
        SphereInfo sphere;
        BoxInfo box;

        // The three per-type bound setups have no GC symbol (always inlined into UpdateBound; the
        // names are ours). Retail's stack layout shows they were separate inline functions.
        void UpdateBoxBound(xBound* bound, xVec3* pos)
        {
            const xVec3 ext = box.dim_bound * 0.5f;
            const xVec3 ctr = box.vec_offset + *pos;
            bound->type = k_XBOUNDTYPE_BOX;
            bound->box.center = ctr;
            bound->box.box.lower = ctr - ext;
            bound->box.box.upper = ctr + ext;
        }

        void UpdateOBBBound(xBound* bound)
        {
            const xVec3 ext = box.dim_bound * 0.5f;
            const xVec3 ctr = box.vec_offset;
            bound->type = k_XBOUNDTYPE_OBB;
            bound->box.center = ctr;
            bound->box.box.lower = ctr - ext;
            bound->box.box.upper = ctr + ext;
        }

        void UpdateSphereBound(xBound* bound, zNMECommon* npc, xVec3* pos)
        {
            bound->sph.r = sphere.rad_bound;
            bound->type = k_XBOUNDTYPE_SPHERE;
            bound->sph.center = *pos;
            bound->sph.center += ((xMat4x3*)npc->model->Mat)->right * sphere.vec_offset.x;
            bound->sph.center += ((xMat4x3*)npc->model->Mat)->up * sphere.vec_offset.y;
            bound->sph.center += ((xMat4x3*)npc->model->Mat)->at * sphere.vec_offset.z;
        }

        // Weak in retail, in its own .text section (the PS2 demo inlines it into every BUpdate).
        void UpdateBound(xBound* bound, zNMECommon* npc, xVec3* pos)
        {
            bound->type = useBoundType;
            bound->mat = (xMat4x3*)npc->model->Mat;

            if (flags.useModelBind)
            {
                xEntDefaultBoundUpdate(npc, pos);
            }
            else if (useBoundType == k_XBOUNDTYPE_BOX)
            {
                UpdateBoxBound(bound, pos);
            }
            else if (useBoundType == k_XBOUNDTYPE_OBB)
            {
                UpdateOBBBound(bound);
            }
            else if (useBoundType == k_XBOUNDTYPE_SPHERE)
            {
                UpdateSphereBound(bound, npc, pos);
            }

            xBoundUpdate(bound);
            zGridUpdateEnt(npc);
            if (flags.drawBound)
            {
                xBoundDraw(bound);
            }
        }
    };
} // namespace NMEHelpBound

void zNMESupport_Startup();

#endif
