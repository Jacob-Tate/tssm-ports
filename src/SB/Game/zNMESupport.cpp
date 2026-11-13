#include "zNMESupport.h"
#include "xstransvc.h"
#include "xString.h"
#include "xSnd.h"
#include "xQuickCull.h"
#include "zMovePoint.h"
#include "zGlobals.h"
#include "xScene.h"
#include "xRay3.h"
#include "iMath.h"
#include "iDraw.h"
#include "xFXHighDynamicRange.h"
#include "xMath3.h"
#include "zRenderState.h"
#include "xDebug.h"

#include <string.h>
#include "xRand.h"

extern zNMESoundTable* gzSoundTableList[58];

// No GC symbol: inlined into zNMECentral_Startup (its unused copy is dead-stripped).
void zNMESupport_Startup()
{
    static zMODSupport mod_support;

    zNMECentral_RegisterModule(&mod_support, 'NSPR');
}

void zMODSupport::ScenePrepare()
{
}

void zMODSupport::SceneFinish()
{
}

// No symbol (always inlined into zMODSupport::ScenePostInit; the name is ours). j is declared
// before i to match retail's register assignment.
static inline void zNMESupport_SoundTablesPostInit()
{
    S32 j;
    S32 i;

    for (i = 0; i < 58; i++)
    {
        zNMESoundTable* table = gzSoundTableList[i];

        for (j = 0; j < 10; j++)
        {
            if (table->assetNames[j] != NULL)
            {
                table->sndGroupHandle[j] = xSndMgrGetSoundGroup(xStrHash(table->assetNames[j]));
            }
            else
            {
                table->sndGroupHandle[j] = (iSndGroupHandle)-1;
            }
            table->timer[j] = 0.0f;
        }
    }

    // Tiki tables: repeat delays for eNMESound_TikiStack (7) and eNMESound_Death (4).
    for (i = 2; i < 7; i++)
    {
        gzSoundTableList[i]->time[7] = 0.15f;
        gzSoundTableList[i]->time[4] = 0.075f;
    }
}

void zMODSupport::ScenePostInit()
{
    zNMESupport_SoundTablesPostInit();
}

void zMODSupport::SceneReset()
{
}

void zMODSupport::Timestep(F32 dt)
{
}

void NMETarget::PosGet(xVec3* pos)
{
    switch (typ_target)
    {
    case NME_TGT_NONE:
        break;
    case NME_TGT_PLYR:
    case NME_TGT_ENT:
    case NME_TGT_BASE:
        NMEC_pos_ofBase(bas_target, pos);
        break;
    case NME_TGT_POS:
        xVec3Copy(pos, &pos_target);
        break;
    case NME_TGT_MVPT:
        xVec3Copy(pos, zMovePointGetPos(nav_target));
        break;
    }
}

void NMETarget::PosProjected(xVec3* pos, F32 tym_project)
{
    PosGet(pos);
}

F32 NMETarget::DirGet(xVec3* dir, xVec3* pos_from, S32 flg_flat)
{
    PosGet(dir);
    xVec3SubFrom(dir, pos_from);
    if (flg_flat)
    {
        dir->y = 0.0f;
    }

    F32 dst = xVec3Length(dir);
    if (dst > 0.001f)
    {
        F32 dst_inv = 1.0f / dst;
        xVec3SMulBy(dir, dst_inv);
    }
    else if (dst > 0.0f)
    {
        xVec3SMulBy(dir, 100000.0f);
        dst = xVec3Normalize(dir, dir);
    }
    else
    {
        xVec3Copy(dir, &g_Y3);
        dst = 1.0f;
    }

    return dst;
}

F32 NMETarget::DirVecGet(xVec3* vec, xVec3* pos_from, S32 flg_flat)
{
    PosGet(vec);
    xVec3SubFrom(vec, pos_from);
    if (flg_flat)
    {
        vec->y = 0.0f;
    }
    return xVec3Length2(vec);
}

S32 NMETarget::HaveLOS(xVec3* pos_from, F32 dst_max, xCollis* colrec)
{
    xCollis* coll;
    xCollis lcl_collis;

    if (colrec != NULL)
    {
        coll = colrec;
    }
    else
    {
        coll = &lcl_collis;
        memset(coll, 0, sizeof(xCollis));
        coll->flags = 0x1f00;
    }

    xVec3 pos_tgt;
    PosGet(&pos_tgt);

    xRay3 ray;
    ray.min_t = 0.0f;
    ray.max_t = dst_max;
    xVec3Sub(&ray.dir, &pos_tgt, pos_from);
    xVec3Normalize(&ray.dir, &ray.dir);
    xVec3Copy(&ray.origin, pos_from);
    ray.flags = XRAY3_USE_MAX;

    xRayHitsScene(xglobals->sceneCur, &ray, coll);

    if (!(coll->flags & 1))
    {
        return 1;
    }

    if (coll->dist > dst_max)
    {
        return 1;
    }

    xEnt* ent;
    switch (typ_target)
    {
    case NME_TGT_PLYR:
    case NME_TGT_ENT:
        ent = ent_target;
        break;
    default:
        ent = NULL;
        break;
    }

    if (ent != NULL && coll->oid != 0 && ent->id == coll->oid)
    {
        return 1;
    }

    return 0;
}

S32 NMETarget::InRange(xVec3* pos_from, F32 dst_range, S32 flg_flat)
{
    xVec3 vec;
    PosGet(&vec);
    xVec3SubFrom(&vec, pos_from);
    if (flg_flat)
    {
        vec.y = 0.0f;
    }
    if (xVec3Length2(&vec) < SQ(dst_range))
    {
        return 1;
    }
    return 0;
}

S32 NMETarget::InCylinder(xVec3* pos_from, F32 rad, F32 hyt, F32 off)
{
    xVec3 vec;
    PosGet(&vec);
    xVec3SubFrom(&vec, pos_from);

    F32 upper = hyt + off;
    F32 lower = upper - hyt;

    S32 inrange;
    if (vec.y > upper)
    {
        inrange = 0;
    }
    else if (vec.y < lower)
    {
        inrange = 0;
    }
    else if (xVec3Length2(&vec) > SQ(rad))
    {
        inrange = 0;
    }
    else
    {
        inrange = 1;
    }

    return inrange;
}

S32 NMETarget::InWedge(xVec3* pos_from, F32 rad, F32 hyt, F32 off, xVec3* dir_wedge, F32 ang)
{
    S32 inwedge = InCylinder(pos_from, rad, hyt, off);
    if (inwedge)
    {
        xVec3 dir_flat = *dir_wedge;
        dir_flat.y = 0.0f;
        if (dir_wedge->y != 0.0f)
        {
            xVec3Normalize(&dir_flat, &dir_flat);
        }

        if (ang > 0.0f)
        {
            xVec3 dir_tgt;
            DirVecGet(&dir_tgt, pos_from, 1);
            xVec3Normalize(&dir_tgt, &dir_tgt);
            F32 dot = xVec3Dot(&dir_tgt, &dir_flat);
            if (dot < icos(0.5f * ang))
            {
                inwedge = 0;
            }
        }
        else
        {
            inwedge = 1;
        }
    }

    return inwedge;
}

S32 NMETarget::HitByBound(xBound* bnd, xCollis* colrec)
{
    S32 hit = 0;

    switch (typ_target)
    {
    case NME_TGT_PLYR:
        hit = NMEC_chk_hitPlyr(bnd, colrec);
        break;
    case NME_TGT_ENT:
    case NME_TGT_BASE:
        hit = NMEC_chk_hitBase(bas_target, bnd, colrec);
        break;
    case NME_TGT_POS:
        hit = NMEC_PointInBound(&pos_target, bnd, colrec);
        break;
    }

    return hit;
}

// Placeholder for dead-stripped code, not recovered code: retail's .rodata keeps two unreferenced
// templates from the stripped NMETarget code here. Their owners are unknown.
static void __deadstripped2_zNMESupport()
{
    xVec3 unknown1 = {};
    F32 unknown2[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
}

// Structure and names follow BfBB's NPCLaser::Render. pos_vtx is an RwV3d worked on through
// (xVec3*) casts, as HI does elsewhere; retail keeps it in memory, which an xVec3 local does not.
void NMELaser::Render(xVec3* pos_src, xVec3* pos_tgt)
{
    S32 i;
    RxObjSpace3DVertex* vtx_horz;
    RxObjSpace3DVertex* vtx_vert;
    xVec3 pos_beg;
    xVec3 pos_end;
    xVec3 dir_axis;
    xVec3 dir_horz;
    xVec3 dir_vert;
    F32 rat;
    F32 v;
    _SDRenderState old_rendstat;
    S32 bgg;
    static RxObjSpace3DVertex laser_vtxbuf[2][14];

    xVec3Copy(&pos_beg, pos_src);
    xVec3Copy(&pos_end, pos_tgt);

    xVec3Sub(&dir_axis, &pos_end, &pos_beg);
    xVec3Normalize(&dir_axis, &dir_axis);

    xVec3Cross(&dir_horz, &globals.oldSkoolCamera.mat.at, &dir_axis);

    F32 ds2 = xVec3Length2(&dir_horz);
    if (ds2 < 1e-5f)
    {
        xVec3Copy(&dir_horz, &g_X3);
    }
    else
    {
        xVec3SMulBy(&dir_horz, 1.0f / xsqrt(ds2));
    }

    xVec3Cross(&dir_vert, &dir_horz, &dir_axis);

    vtx_horz = laser_vtxbuf[0];
    vtx_vert = laser_vtxbuf[1];

    for (i = 0; i <= 6; i++)
    {
        rat = (F32)i / 6.0f;
        F32 rad = LERP(rat, radius[0], radius[1]);

        xVec3 pos_lerp;
        pos_lerp.x = LERP(rat, pos_beg.x, pos_end.x);
        pos_lerp.y = LERP(rat, pos_beg.y, pos_end.y);
        pos_lerp.z = LERP(rat, pos_beg.z, pos_end.z);

        U8 cr = LERP(rat, rgba[0].red, rgba[1].red);
        U8 cg = LERP(rat, rgba[0].green, rgba[1].green);
        U8 cb = LERP(rat, rgba[0].blue, rgba[1].blue);
        U8 ca = LERP(rat, rgba[0].alpha, rgba[1].alpha);

        v = uv_base[1] + (1.0f - rat);

        RwV3d pos_vtx;

        xVec3SMul((xVec3*)&pos_vtx, &dir_horz, rad);
        xVec3AddTo((xVec3*)&pos_vtx, &pos_lerp);
        RwIm3DVertexSetPos(&vtx_horz[0], pos_vtx.x, pos_vtx.y, pos_vtx.z);
        RwIm3DVertexSetRGBA(&vtx_horz[0], cr, cg, cb, ca);
        RwIm3DVertexSetUV(&vtx_horz[0], 0.0f, v);

        xVec3SMul((xVec3*)&pos_vtx, &dir_horz, -rad);
        xVec3AddTo((xVec3*)&pos_vtx, &pos_lerp);
        RwIm3DVertexSetPos(&vtx_horz[1], pos_vtx.x, pos_vtx.y, pos_vtx.z);
        RwIm3DVertexSetRGBA(&vtx_horz[1], cr, cg, cb, ca);
        RwIm3DVertexSetUV(&vtx_horz[1], 1.0f, v);

        vtx_horz += 2;

        xVec3SMul((xVec3*)&pos_vtx, &dir_vert, rad);
        xVec3AddTo((xVec3*)&pos_vtx, &pos_lerp);
        RwIm3DVertexSetPos(&vtx_vert[0], pos_vtx.x, pos_vtx.y, pos_vtx.z);
        RwIm3DVertexSetRGBA(&vtx_vert[0], cr, cg, cb, ca);
        RwIm3DVertexSetUV(&vtx_vert[0], 0.0f, v);

        xVec3SMul((xVec3*)&pos_vtx, &dir_vert, -rad);
        xVec3AddTo((xVec3*)&pos_vtx, &pos_lerp);
        RwIm3DVertexSetPos(&vtx_vert[1], pos_vtx.x, pos_vtx.y, pos_vtx.z);
        RwIm3DVertexSetRGBA(&vtx_vert[1], cr, cg, cb, ca);
        RwIm3DVertexSetUV(&vtx_vert[1], 1.0f, v);

        vtx_vert += 2;
    }

    old_rendstat = zRenderStateCurrent();
    if (old_rendstat == SDRS_Unknown)
    {
        old_rendstat = SDRS_Default;
    }

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, rast_laser);

    if (flg_render & 0x1)
    {
        zRenderState(SDRS_OpaqueModels);
        RwIm3DTransform(laser_vtxbuf[0], 14, NULL,
                        rwIM3D_VERTEXUV | rwIM3D_ALLOPAQUE | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA);
        RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
        RwIm3DTransform(laser_vtxbuf[1], 14, NULL,
                        rwIM3D_VERTEXUV | rwIM3D_ALLOPAQUE | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA);
        RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
    }

    zRenderState(SDRS_LaserBeam);

    if (flg_render & 0x8)
    {
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    }
    else
    {
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    }

    if (flg_render & 0x4)
    {
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    }
    else
    {
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    }

    if (flg_render & 0x2)
    {
        if (HDR_brightening != TRUE)
        {
            HDR_brightening = TRUE;
            iDrawSetFBMSK(0x00000000, 0xFFFFFFFF, true);
        }

        bgg = xFXHighDynamicRangeGetBackgroundGlow();
        xFXHighDynamicRangeSetGlowClamp(bgg, false);

        RwIm3DTransform(laser_vtxbuf[0], 14, NULL,
                        rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA);
        RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
        RwIm3DTransform(laser_vtxbuf[1], 14, NULL,
                        rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA);
        RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);

        if (HDR_brightening != FALSE)
        {
            HDR_brightening = FALSE;
            iDrawSetFBMSK(0xFF000000, 0xFFFFFFFF, true);
        }
        xFXHighDynamicRangeResetGlowClamp();
    }
    else if (!(flg_render & 0x1))
    {
        RwIm3DTransform(laser_vtxbuf[0], 14, NULL,
                        rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA);
        RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
        RwIm3DTransform(laser_vtxbuf[1], 14, NULL,
                        rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA);
        RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
    }

    RwIm3DEnd();
    zRenderState(old_rendstat);
}

// Placeholder for dead-stripped code (same form as BfBB's __deadstripped_zNPCTypeDuplotron):
// retail keeps blink_vtxbuf, the vertex buffer of BfBB's NPCBlinker::Render, and two unreferenced
// {0, 0, 1} templates from the stripped blinker code. The body is unknown.
void __deadstripped_zNMESupport()
{
    xVec3 unknown1 = { 0.0f, 0.0f, 1.0f };
    xVec3 unknown2 = { 0.0f, 0.0f, 1.0f };
    static RxObjSpace3DVertex blink_vtxbuf[2][14];
}

// Unused (no Firework code survives), but retail's .bss keeps it.
static Firework g_fireworks[32];

F32 NMEC_aimVary(xVec3* dir_aim, const xVec3* pos_src, const xVec3* pos_tgt, F32 dst_vary,
                 S32 flg_vary, xVec3* pos_aimPoint)
{
    xVec3 dir_toReal = *pos_tgt - *pos_src;
    if (flg_vary & 0x10)
    {
        dir_toReal.y = 0.0f;
    }

    F32 fv = xVec3Length(&dir_toReal);
    if (fv < 0.001f)
    {
        if (fv > 0.0f)
        {
            xVec3SMulBy(&dir_toReal, 100000.0f);
            xVec3Normalize(&dir_toReal, &dir_toReal);
        }
        else
        {
            *dir_aim = g_X3;
        }

        if (pos_aimPoint)
        {
            *pos_aimPoint = *pos_tgt;
        }

        return fv;
    }
    else
    {
        xVec3SMulBy(&dir_toReal, 1.0f / fv);

        F32 mag_vary;
        F32 mag_updown = 0.0f;
        if (flg_vary & 8)
        {
            mag_vary = dst_vary;
        }
        else
        {
            mag_vary = dst_vary * (2.0f * (xurand() - 0.5f));
            if ((flg_vary & 1) && (flg_vary & 2))
            {
                mag_updown = 2.0f * (xurand() - 0.5f);
            }
            else if (flg_vary == 1)
            {
                mag_updown = xurand();
            }
            else if (flg_vary == 2)
            {
                mag_updown = -xurand();
            }
            else
            {
                mag_updown = 0.0f;
            }
            mag_updown *= dst_vary;
        }

        if (flg_vary & 0x20)
        {
            mag_vary = -xabs(mag_vary);
        }
        if (flg_vary & 0x40)
        {
            mag_vary = xabs(mag_vary);
        }

        xVec3 dir_left;
        xVec3Cross(&dir_left, &g_Y3, &dir_toReal);
        xVec3 vec_offset = dir_left * mag_vary;
        vec_offset += g_Y3 * mag_updown;
        xVec3 pos_tgtFake = *pos_tgt + vec_offset;
        xVec3 dir_toFake = pos_tgtFake - *pos_src;

        F32 dst_toFake = xVec3Normalize(dir_aim, &dir_toFake);
        if (pos_aimPoint)
        {
            *pos_aimPoint = pos_tgtFake;
        }

        if (flg_vary & 4)
        {
            return dst_toFake;
        }
    }

    return fv;
}

S32 NMEC_chk_hitBase(xBase* tgt, xBound* bnd, xCollis* collide)
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

    xBound bnd_tgt;
    memset(&bnd_tgt, 0, sizeof(xBound));

    if (!NMEC_bnd_ofBase(tgt, &bnd_tgt))
    {
        return 0;
    }

    colrec->optr = tgt;
    colrec->oid = tgt->id;
    colrec->flags = 0x1f00;

    xQuickCullForEverything(&bnd->qcd);
    xBoundHitsBound(bnd, &bnd_tgt, colrec);

    return colrec->flags & 1;
}

// The compiled-out assert (condition unknown) keeps this from being auto-inlined into
// NMETarget::HitByBound, which calls it in retail.
S32 NMEC_PointInBound(const xVec3* pos, xBound* bnd, xCollis* colrec)
{
    S32 inside = 0;

    xASSERT_PLACEHOLDER();

    if (bnd->type == XBOUND_TYPE_SPHERE)
    {
        if ((*pos - bnd->sph.center).length2() < SQ(bnd->sph.r))
        {
            inside = 1;
        }
    }
    else if (bnd->type == XBOUND_TYPE_BOX)
    {
        inside = 1;
        if (pos->x < bnd->box.box.lower.x)
        {
            inside = 0;
        }
        else if (pos->y < bnd->box.box.lower.y)
        {
            inside = 0;
        }
        else if (pos->z < bnd->box.box.lower.z)
        {
            inside = 0;
        }
        else if (pos->x > bnd->box.box.upper.x)
        {
            inside = 0;
        }
        else if (pos->y > bnd->box.box.upper.y)
        {
            inside = 0;
        }
        else if (pos->z > bnd->box.box.upper.z)
        {
            inside = 0;
        }
    }

    if (colrec != NULL)
    {
        if (inside)
        {
            colrec->flags = 1;
        }
        else
        {
            colrec->flags = 0;
        }
    }

    return inside;
}

S32 NMEC_bnd_ofBase(const xBase* tgt, xBound* bnd)
{
    S32 retval = 1;

    switch (tgt->baseType)
    {
    case eBaseTypeCamera:
    case eBaseTypeDoor:
    case eBaseTypeVolume:
    case eBaseTypeEGenerator:
        retval = 0;
        break;
    case eBaseTypePlayer:
    case eBaseTypePickup:
    case eBaseTypePlatform:
    case eBaseTypeStatic:
    case eBaseTypeDynamic:
    case eBaseTypeBubble:
    case eBaseTypePendulum:
    case eBaseTypeHangable:
    case eBaseTypeButton:
    case eBaseTypeDestructObj:
    case eBaseTypeNPC:
    case eBaseTypeBoulder:
        *bnd = ((xEnt*)tgt)->bound;
        break;
    default:
        retval = 0;
        break;
    case eBaseTypeCruiseBubble:
        break;
    }

    return retval;
}

S32 NMEC_pos_ofBase(const xBase* tgt, xVec3* pos)
{
    S32 known = 1;

    switch (tgt->baseType)
    {
    case eBaseTypeCamera:
        xVec3Copy(pos, &globals.oldSkoolCamera.mat.pos);
        break;
    case eBaseTypeDoor:
    case eBaseTypeVolume:
    case eBaseTypeEGenerator:
        known = 0;
        break;
    case eBaseTypePlayer:
    case eBaseTypePickup:
    case eBaseTypePlatform:
    case eBaseTypeStatic:
    case eBaseTypeDynamic:
    case eBaseTypeBubble:
    case eBaseTypePendulum:
    case eBaseTypeHangable:
    case eBaseTypeButton:
    case eBaseTypeDestructObj:
    case eBaseTypeNPC:
    case eBaseTypeBoulder:
        xVec3Copy(pos, xEntGetPos((xEnt*)tgt));
        break;
    case eBaseTypeCruiseBubble:
        known = 0;
        break;
    default:
        known = 0;
        break;
    }

    return known;
}

// No GC symbol: inlined into zNMEGoalTurrSpiral::EmitSteam (its unused copy is dead-stripped).
S32 NMEC_LineHitsBound(const xVec3* a, const xVec3* b, xBound* bnd, xCollis* callers_colrec)
{
    xCollis* colrec;
    xCollis local_colrec;
    F32 len;
    xRay3 ray;

    if (callers_colrec != NULL)
    {
        colrec = callers_colrec;
    }
    else
    {
        colrec = &local_colrec;
        memset(colrec, 0, sizeof(xCollis));
    }

    xVec3 vec = *b - *a;
    len = xVec3Length(&vec);
    if (len < 0.001f)
    {
        len = 0.001f;
    }
    xVec3Copy(&ray.origin, a);
    xVec3SMul(&ray.dir, &vec, 1.0f / len);

    ray.min_t = 0.1f;
    ray.max_t = len;
    ray.flags = 0xc00;

    xRayHitsBound(&ray, bnd, colrec);
    return colrec->flags & 1;
}

F32 NMEC_ds2_toCam(const xVec3* pos_from, xVec3* delta)
{
    xVec3 delt = {};
    xVec3Sub(&delt, &globals.cam->mat.pos, pos_from);
    F32 retval = xVec3Length2(&delt);
    if (delta)
    {
        xVec3Copy(delta, &delt);
    }
    return retval;
}

// Placeholder for dead-stripped code, not recovered code: retail's .rodata keeps six unreferenced
// templates here, in the same order as BfBB's zNPCSupport. The third is NMEC_rotHPB's mat_rot
// (below); the owners of the others are unknown.
static void __deadstripped3_zNMESupport()
{
    xVec3 unknown1 = { 0.0f, 0.0f, 1.0f };
    xVec3 unknown2 = {};
}

// Dead-stripped in retail (no caller survives); body from BfBB's NPCC_rotHPB, whose mat_rot
// template is the third of the unreferenced templates above.
void NMEC_rotHPB(xMat3x3* mat, F32 heading, F32 pitch, F32 bank)
{
    xMat3x3 mat_rot = {};

    xMat3x3RotZ(mat, bank);
    xMat3x3RotX(&mat_rot, -pitch);
    xMat3x3Mul(mat, mat, &mat_rot);
    xMat3x3RotY(&mat_rot, heading);
    xMat3x3Mul(mat, mat, &mat_rot);
}

// Placeholder for dead-stripped code: the three unreferenced templates after NMEC_rotHPB's
// mat_rot, as in BfBB. Their owners are unknown.
static void __deadstripped4_zNMESupport()
{
    xMat3x3 unknown4 = {};
    xVec3 unknown5 = {};
    xMat3x3 unknown6 = {};
}

// NMEC_FindRWTexture is defined ahead of its address-order slot so that the NMEC_FindRWRaster
// overloads can inline it, as the target does.
RwTexture* NMEC_FindRWTexture(const char* txtrname)
{
    return (RwTexture*)xSTFindAsset(xStrHash(txtrname), NULL);
}

RwRaster* NMEC_FindRWRaster(const char* txtrname)
{
    return NMEC_FindRWRaster(NMEC_FindRWTexture(txtrname));
}

// Tests txtr itself instead of forwarding to the RwTexture* overload, as retail's inlined copies
// do (Ratatouille's equivalent xFXRingFindRWRaster has the same local).
RwRaster* NMEC_FindRWRaster(U32 hashid)
{
    RwTexture* txtr = (RwTexture*)xSTFindAsset(hashid, NULL);
    if (txtr != NULL)
    {
        return txtr->raster;
    }
    return NULL;
}

RwRaster* NMEC_FindRWRaster(RwTexture* txtr)
{
    if (txtr != NULL)
    {
        return txtr->raster;
    }
    return NULL;
}

// The compiled-out asserts (conditions unknown) keep this from being auto-inlined, as retail
// calls it.
void NMEC_GenSmooth(xVec3** pos_base, xVec3** pos_mid)
{
    S32 i;
    F32 u;
    F32 u3;
    static F32 prepute[4][4];
    static const F32 yews[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    static S32 init = 0;

    if (!init)
    {
        init = 1;
        for (i = 0; i < 4; i++)
        {
            u = yews[i];
            u3 = u * u * u;
            prepute[i][0] = -0.5f * u3 + u * u + -0.5f * u;
            prepute[i][1] = 1.5f * u3 + -2.5f * (u * u) + 1.0f;
            prepute[i][2] = -1.5f * u3 + 2.0f * (u * u) + 0.5f * u;
            prepute[i][3] = 0.5f * u3 + -0.5f * (u * u);
        }
    }

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    for (i = 0; i < 4; i++)
    {
        xVec3SMul(pos_mid[i], pos_base[0], prepute[i][0]);
        xVec3AddScaled(pos_mid[i], pos_base[1], prepute[i][1]);
        xVec3AddScaled(pos_mid[i], pos_base[2], prepute[i][2]);
        xVec3AddScaled(pos_mid[i], pos_base[3], prepute[i][3]);
    }

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
}
