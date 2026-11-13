#include "xCamera.h"

#include "xstransvc.h"
#include "xMath.h"
#include "xMathInlines.h"
#include "xScene.h"
#include "xCollide.h"
#include "xCollideFast.h"
#include "xScrFx.h"
#include "zGlobals.h"
#include "zMain.h"
#include "zEntSimpleObj.h"
#include "zPlatform.h"
#include "zGrid.h"

#include "iMath.h"

#include <PowerPC_EABI_Support/MSL_C/MSL_Common/cmath>
#include <PowerPC_EABI_Support/MSL_C/MSL_Common/cstring>

struct dualCamInfo
{
    xSweptSphere playerToCameraSWS;
    xBound cameraBound;
};

static S32 sCamCollis;
S32 xcam_collis_owner_disable;
S32 xcam_do_collis = 1;
F32 xcam_collis_radius = 0.4f;
F32 xcam_collis_stiffness = 0.3f;
static RpAtomic* sInvisWallHack;
extern xMat4x3 sCameraFXMatOld;
extern cameraFX sCameraFX[10];
extern cameraFXTableEntry sCameraFXTable[3];

static void xCameraFXInit();
void add_camera_tweaks();

static inline void xCam_cyltoworld(xVec3* v, const xMat4x3* tgt_mat, F32 d, F32 h, F32 p, U32 flags)
{
    if (flags & 0x10)
    {
        v->y = h;
    }
    else
    {
        v->y = h + tgt_mat->pos.y;
    }

    if (flags & 0x20)
    {
        v->x = d * isin(p) + tgt_mat->pos.x;
        v->z = d * icos(p) + tgt_mat->pos.z;
    }
    else
    {
        p += xatan2(tgt_mat->at.x, tgt_mat->at.z);

        v->x = d * isin(p) + tgt_mat->pos.x;
        v->z = d * icos(p) + tgt_mat->pos.z;
    }
}

static void _xCameraUpdate(xCamera* cam, F32 dt);

void xCameraInit(xCamera* cam, U32 width, U32 height)
{
    cam->lo_cam = globals.screen->icam;
    cam->fov = 75.0;
    cam->bound.sph.center.x = 0.0f;
    cam->bound.sph.center.y = 0.0f;
    cam->bound.sph.center.z = 0.0f;
    cam->bound.sph.r = 0.5f;
    cam->tgt_mat = NULL;
    cam->tgt_omat = NULL;
    cam->tgt_bound = NULL;
    cam->sc = NULL;
    cam->tran_accum.x = 0.0f;
    cam->tran_accum.y = 0.0f;
    cam->tran_accum.z = 0.0f;
}

void xCameraExit(xCamera* cam)
{
    if (cam->lo_cam)
    {
        iCameraDestroy(cam->lo_cam);
        cam->lo_cam = NULL;
    }
}

void xCameraReset(xCamera* cam, F32 d, F32 h, F32 pitch)
{
    sInvisWallHack = (RpAtomic*)xSTFindAsset(0xB8895D14, NULL);

    xMat4x3Identity(&cam->mat);

    cam->omat = cam->mat;
    cam->focus.x = 0.0f;
    cam->focus.y = 0.0f;
    cam->focus.z = 10.0f;
    cam->tran_accum.x = 0.0f;
    cam->tran_accum.y = 0.0f;
    cam->tran_accum.z = 0.0f;
    cam->flags = 0;

    F32 goal_p = 3.1415927f;

    if (cam->tgt_mat)
    {
        goal_p += xatan2(cam->tgt_mat->at.x, cam->tgt_mat->at.z);
    }

    xCameraMove(cam, 0x2E, d, h, goal_p, 0.0f, 0.66666669f, 0.66666669f);

    cam->pitch_goal = pitch;
    cam->pitch_cur = pitch;
    cam->roll_cur = 0.0f;

    xMat3x3Euler(&cam->mat, cam->yaw_cur, cam->pitch_cur, cam->roll_cur);

    cam->omat = cam->mat;
    cam->yaw_ct = 1.0f;
    cam->yaw_cd = 1.0f;
    cam->yaw_ccv = 0.65f;
    cam->yaw_csv = 1.0f;
    cam->pitch_ct = 1.0f;
    cam->pitch_cd = 1.0f;
    cam->pitch_ccv = 0.7f;
    cam->pitch_csv = 1.0f;
    cam->roll_ct = 1.0f;
    cam->roll_cd = 1.0f;
    cam->roll_ccv = 0.7f;
    cam->roll_csv = 1.0f;
    cam->flags |= 0x80;

    xcam_do_collis = 1;
    xcam_collis_owner_disable = 0;
    cam->smoothOutwardSlidePos = 10.0f;
}

static void xCam_buildbasis(xCamera* cam)
{
    if (cam->tgt_mat)
    {
        F32 d2d;
        F32 dx__ = cam->mat.pos.x - cam->tgt_mat->pos.x;
        F32 dz__ = cam->mat.pos.z - cam->tgt_mat->pos.z;

        F32 dist2 = SQR(dx__) + SQR(dz__);
        F32 dist_inv;

        if ((F32)iabs(dist2 - 1.0f) <= 0.00001f)
        {
            cam->mbasis.at.x = dx__;
            cam->mbasis.at.z = dz__;

            d2d = 1.0f;
        }
        else if ((F32)iabs(dist2) <= 0.00001f)
        {
            cam->mbasis.at.x = 0.0f;
            cam->mbasis.at.z = 0.0f;

            d2d = 0.0f;
        }
        else
        {
            d2d = xsqrt(dist2);
            dist_inv = 1.0f / d2d;

            cam->mbasis.at.x = dx__ * dist_inv;
            cam->mbasis.at.z = dz__ * dist_inv;
        }

        if (d2d < 0.00001f)
        {
            cam->mbasis.at.x = cam->mat.at.x;
            cam->mbasis.at.z = cam->mat.at.z;

            dist2 = xsqrt(SQR(cam->mbasis.at.x) + SQR(cam->mbasis.at.z));

            if (dist2 > 0.001f)
            {
                cam->mbasis.at.x *= 1.0f / dist2;
                cam->mbasis.at.z *= 1.0f / dist2;
            }
            else
            {
                cam->mbasis.at.x = isin(cam->pcur);
                cam->mbasis.at.z = icos(cam->pcur);
            }
        }

        cam->mbasis.at.y = 0.0f;
        cam->mbasis.up.x = 0.0f;
        cam->mbasis.up.y = 1.0f;
        cam->mbasis.up.z = 0.0f;
        cam->mbasis.right.x = cam->mbasis.at.z;
        cam->mbasis.right.y = 0.0f;
        cam->mbasis.right.z = -cam->mbasis.at.x;
    }
}

static void xCam_worldtocyl(F32& d, F32& h, F32& p, const xMat4x3* tgt_mat, const xVec3* v,
                            U32 flags)
{
    F32 lx, lz;

    F32 dx__ = v->x - tgt_mat->pos.x;
    F32 dz__ = v->z - tgt_mat->pos.z;

    F32 dist2 = SQR(dx__) + SQR(dz__);
    F32 dist_inv;

    if ((F32)iabs(dist2 - 1.0f) <= 0.00001f)
    {
        lx = dx__;
        lz = dz__;
        d = 1.0f;
    }
    else if ((F32)iabs(dist2) <= 0.00001f)
    {
        d = lz = lx = 0.0f;
    }
    else
    {
        d = xsqrt(dist2);

        dist_inv = 1.0f / d;

        lx = dx__ * dist_inv;
        lz = dz__ * dist_inv;
    }

    if (flags & 0x10)
    {
        h = v->y;
    }
    else
    {
        h = v->y - tgt_mat->pos.y;
    }

    p = xatan2(lx, lz);

    if (!(flags & 0x20))
    {
        p = xDangleClamp(p - xatan2(tgt_mat->at.x, tgt_mat->at.z));
    }
}

static void xCam_CorrectD(xCamera* cam, F32 dtg, F32 dcv, F32 dt)
{
    F32 dv = 1.4285715f * (2.0f * dtg - dcv * dt);
    dv -= dcv;
    dv *= dt;

    F32 dx = cam->mbasis.at.x * dv;
    F32 dz = cam->mbasis.at.z * dv;

    cam->mat.pos.x += dx;
    cam->mat.pos.z += dz;
}

static void xCam_CorrectH(xCamera* cam, F32 htg, F32 hcv, F32 dt)
{
    hcv = 0.15f * hcv;

    F32 hv = 1.4285715f * (htg - hcv * dt);
    hv -= hcv;
    hv *= dt;

    cam->mat.pos.y += hv;
}

static void xCam_CorrectP(xCamera* cam, F32 ptg, F32 pcv, F32 dt)
{
    pcv = 0.15f * pcv;

    F32 pv = 2.5f * (2.0f * ptg - pcv * dt);
    pv -= pcv;
    pv *= dt;

    F32 dx = cam->mbasis.right.x * pv;
    F32 dz = cam->mbasis.right.z * pv;

    cam->mat.pos.x += dx;
    cam->mat.pos.z += dz;
}

static void xCam_DampP(xCamera* cam, F32 pcv, F32 dt)
{
    pcv = (-0.1f / SECS_PER_VBLANK) * pcv;

    F32 pv = dt * pcv * dt;

    F32 dx = cam->mbasis.right.x * pv;
    F32 dz = cam->mbasis.right.z * pv;

    cam->mat.pos.x += dx;
    cam->mat.pos.z += dz;
}

static void xCam_CorrectYaw(xCamera* cam, F32 ytg, F32 ycv, F32 dt)
{
    F32 yv = (1.0f / cam->yaw_ct) * (2.0f * cam->yaw_cd * ytg - ycv * dt);
    yv -= ycv;
    yv *= cam->yaw_csv * dt;

    cam->yaw_cur += yv;
}

static void xCam_CorrectPitch(xCamera* cam, F32 ptg, F32 pcv, F32 dt)
{
    F32 pv = (1.0f / cam->pitch_ct) * (2.0f * cam->pitch_cd * ptg - pcv * dt);
    pv -= pcv;
    pv *= cam->pitch_csv * dt;

    cam->pitch_cur += pv;
}

static void xCam_CorrectRoll(xCamera* cam, F32 rtg, F32 rcv, F32 dt)
{
    F32 rv = (1.0f / cam->roll_ct) * (2.0f * cam->roll_cd * rtg - rcv * dt);
    rv -= rcv;
    rv *= cam->roll_csv * dt;

    cam->roll_cur += rv;
}

void DualCamHitsCameraEnt(xScene*, xRay3* ray, xQCData* qcd, xEnt* ent, void* data)
{
    dualCamInfo* pDualCamData = (dualCamInfo*)data;
    U8 usePartialCollision = FALSE;

    switch (ent->baseType)
    {
    case eBaseTypeStatic:
        usePartialCollision = zEntSimpleObj_UsePartialCameraCollision((zEntSimpleObj*)ent);
        break;
    case eBaseTypePlatform:
        usePartialCollision = zPlatform_UsePartialCameraCollision((zPlatform*)ent);
        break;
    }

    if (usePartialCollision)
    {
        xCollis tempCollis;

        tempCollis.flags = 0;
        xBoundHitsBound(&pDualCamData->cameraBound, &ent->bound, &tempCollis);

        if (!(tempCollis.flags & k_HIT_IT))
        {
            return;
        }

        tempCollis.flags = 0;
        xBoundHitsModel(&pDualCamData->cameraBound, ent->camcollModel, &tempCollis, 1,
                        DEG2RAD(22.5f));

        if (!(tempCollis.flags & k_HIT_IT))
        {
            return;
        }
    }

    if (ent->camcollModel && ent->chkby & 0x10 && xQuickCullIsects(qcd, &ent->bound.qcd))
    {
        if (!xEntIsVisible(ent))
        {
            if (ent->model->Data != sInvisWallHack)
            {
                return;
            }

            if (ent->collLev != 5)
            {
                if (ent->bound.type == XBOUND_TYPE_BOX)
                {
                    xSweptSphereToBox(pDualCamData->playerToCameraSWS, ent->bound.box.box);
                    return;
                }
                else if (ent->bound.type == XBOUND_TYPE_OBB)
                {
                    xSweptSphereToOBB(&pDualCamData->playerToCameraSWS, &ent->bound.box.box,
                                      ent->bound.mat);
                    return;
                }
                else
                {
                    return;
                }
            }
        }

        U32 hitBound = 0;

        switch (ent->bound.type)
        {
        case XBOUND_TYPE_SPHERE:
        {
            F32 oldrad = ent->bound.sph.r;

            ent->bound.sph.r += pDualCamData->playerToCameraSWS.radius;

            hitBound = xRayHitsSphereFast(ray, &ent->bound.sph);

            ent->bound.sph.r = oldrad;

            break;
        }
        case XBOUND_TYPE_BOX:
        {
            xBox tmpbox;
            tmpbox.upper.x = ent->bound.box.box.upper.x + pDualCamData->playerToCameraSWS.radius;
            tmpbox.upper.y = ent->bound.box.box.upper.y + pDualCamData->playerToCameraSWS.radius;
            tmpbox.upper.z = ent->bound.box.box.upper.z + pDualCamData->playerToCameraSWS.radius;
            tmpbox.lower.x = ent->bound.box.box.lower.x - pDualCamData->playerToCameraSWS.radius;
            tmpbox.lower.y = ent->bound.box.box.lower.y - pDualCamData->playerToCameraSWS.radius;
            tmpbox.lower.z = ent->bound.box.box.lower.z - pDualCamData->playerToCameraSWS.radius;

            hitBound = xRayHitsBoxFast(ray, &tmpbox);

            break;
        }
        case XBOUND_TYPE_OBB:
        {
            xBox tmpbox;
            xRay3 lr;
            xMat3x3 mn;

            F32 len = xVec3Length(&ent->bound.mat->right);

            xMat3x3Normalize(&mn, ent->bound.mat);
            xMat4x3Tolocal(&lr.origin, ent->bound.mat, &ray->origin);
            xMat3x3Tolocal(&lr.dir, &mn, &ray->dir);

            lr.max_t = ray->max_t / len;
            lr.min_t = ray->min_t / len;
            lr.flags = ray->flags;

            tmpbox.upper.x =
                ent->bound.box.box.upper.x + pDualCamData->playerToCameraSWS.radius / len;
            tmpbox.upper.y =
                ent->bound.box.box.upper.y + pDualCamData->playerToCameraSWS.radius / len;
            tmpbox.upper.z =
                ent->bound.box.box.upper.z + pDualCamData->playerToCameraSWS.radius / len;
            tmpbox.lower.x =
                ent->bound.box.box.lower.x - pDualCamData->playerToCameraSWS.radius / len;
            tmpbox.lower.y =
                ent->bound.box.box.lower.y - pDualCamData->playerToCameraSWS.radius / len;
            tmpbox.lower.z =
                ent->bound.box.box.lower.z - pDualCamData->playerToCameraSWS.radius / len;

            hitBound = xRayHitsBoxFast(&lr, &tmpbox);

            break;
        }
        }

        if (hitBound)
        {
            xModelInstance* collmod = ent->camcollModel;

            xSweptSphereToModel(&pDualCamData->playerToCameraSWS, collmod->Data, collmod->Mat);
        }
    }
}

void SweptSphereHitsCameraEnt(xScene*, xRay3* ray, xQCData* qcd, xEnt* ent, void* data)
{
    dualCamInfo* pDualCamData = (dualCamInfo*)data;

    if (ent->camcollModel && ent->chkby & 0x10 && xQuickCullIsects(qcd, &ent->bound.qcd))
    {
        if (!xEntIsVisible(ent))
        {
            if (ent->model->Data != sInvisWallHack)
            {
                return;
            }

            if (ent->collLev != 5)
            {
                if (ent->bound.type == XBOUND_TYPE_BOX)
                {
                    xSweptSphereToBox(pDualCamData->playerToCameraSWS, ent->bound.box.box);
                    return;
                }
                else if (ent->bound.type == XBOUND_TYPE_OBB)
                {
                    xSweptSphereToOBB(&pDualCamData->playerToCameraSWS, &ent->bound.box.box,
                                      ent->bound.mat);
                    return;
                }
                else
                {
                    return;
                }
            }
        }

        U32 hitBound = 0;

        switch (ent->bound.type)
        {
        case XBOUND_TYPE_SPHERE:
        {
            F32 oldrad = ent->bound.sph.r;

            ent->bound.sph.r += pDualCamData->playerToCameraSWS.radius;

            hitBound = xRayHitsSphereFast(ray, &ent->bound.sph);

            ent->bound.sph.r = oldrad;

            break;
        }
        case XBOUND_TYPE_BOX:
        {
            xBox tmpbox;
            tmpbox.upper.x = ent->bound.box.box.upper.x + pDualCamData->playerToCameraSWS.radius;
            tmpbox.upper.y = ent->bound.box.box.upper.y + pDualCamData->playerToCameraSWS.radius;
            tmpbox.upper.z = ent->bound.box.box.upper.z + pDualCamData->playerToCameraSWS.radius;
            tmpbox.lower.x = ent->bound.box.box.lower.x - pDualCamData->playerToCameraSWS.radius;
            tmpbox.lower.y = ent->bound.box.box.lower.y - pDualCamData->playerToCameraSWS.radius;
            tmpbox.lower.z = ent->bound.box.box.lower.z - pDualCamData->playerToCameraSWS.radius;

            hitBound = xRayHitsBoxFast(ray, &tmpbox);

            break;
        }
        case XBOUND_TYPE_OBB:
        {
            xBox tmpbox;
            xRay3 lr;
            xMat3x3 mn;

            F32 len = xVec3Length(&ent->bound.mat->right);

            xMat3x3Normalize(&mn, ent->bound.mat);
            xMat4x3Tolocal(&lr.origin, ent->bound.mat, &ray->origin);
            xMat3x3Tolocal(&lr.dir, &mn, &ray->dir);

            lr.max_t = ray->max_t / len;
            lr.min_t = ray->min_t / len;
            lr.flags = ray->flags;

            tmpbox.upper.x =
                ent->bound.box.box.upper.x + pDualCamData->playerToCameraSWS.radius / len;
            tmpbox.upper.y =
                ent->bound.box.box.upper.y + pDualCamData->playerToCameraSWS.radius / len;
            tmpbox.upper.z =
                ent->bound.box.box.upper.z + pDualCamData->playerToCameraSWS.radius / len;
            tmpbox.lower.x =
                ent->bound.box.box.lower.x - pDualCamData->playerToCameraSWS.radius / len;
            tmpbox.lower.y =
                ent->bound.box.box.lower.y - pDualCamData->playerToCameraSWS.radius / len;
            tmpbox.lower.z =
                ent->bound.box.box.lower.z - pDualCamData->playerToCameraSWS.radius / len;

            hitBound = xRayHitsBoxFast(&lr, &tmpbox);

            break;
        }
        }

        if (hitBound)
        {
            xModelInstance* collmod = ent->camcollModel;

            xSweptSphereToModel(&pDualCamData->playerToCameraSWS, collmod->Data, collmod->Mat);
        }
    }
}

static void _xCameraUpdate(xCamera* cam, F32 dt)
{
    if (!cam->tgt_mat)
        return;

    static F32 last_dt = SECS_PER_VBLANK;

    xCam_worldtocyl(cam->dcur, cam->hcur, cam->pcur, cam->tgt_mat, &cam->mat.pos, cam->flags);

    F32 wcvx = cam->mat.pos.x - cam->omat.pos.x;
    F32 wcvy = cam->mat.pos.y - cam->omat.pos.y;
    F32 wcvz = cam->mat.pos.z - cam->omat.pos.z;
    F32 m = 1.0f / last_dt;
    wcvx *= m;
    wcvy *= m;
    wcvz *= m;

    cam->omat.pos = cam->mat.pos;

    xCam_buildbasis(cam);

    F32 dcv = wcvx * cam->mbasis.at.x + wcvz * cam->mbasis.at.z;
    F32 hcv = wcvy;
    F32 pcv = wcvx * cam->mbasis.right.x + wcvz * cam->mbasis.right.z;
    wcvx *= dt;
    wcvy *= dt;
    wcvz *= dt;

    cam->mat.pos.x += wcvx;
    cam->mat.pos.y += wcvy;
    cam->mat.pos.z += wcvz;

    if (cam->flags & 0x1)
    {
        F32 tnext = cam->tmr - dt;
        if (tnext <= 0.0f)
        {
            cam->flags &= ~0x1;
            cam->tmr = 0.0f;
            cam->omat.pos = cam->mat.pos;
        }
        else
        {
            F32 dtg = cam->dgoal - cam->dcur;
            F32 htg = cam->hgoal - cam->hcur;
            F32 ptg = (cam->dgoal + cam->dcur) * xDangleClamp(cam->pgoal - cam->pcur) * 0.5f;
            F32 dsv, hsv, psv;
            if (tnext <= cam->tm_dec)
            {
                F32 T_inv = 1.0f / cam->tmr;
                dsv = (2.0f * dtg - dcv * dt) * T_inv;
                hsv = (2.0f * htg - hcv * dt) * T_inv;
                psv = (2.0f * ptg - pcv * dt) * T_inv;
            }
            else if (tnext <= cam->tm_acc)
            {
                F32 T_inv = 1.0f / (2.0f * cam->tmr - dt - cam->tm_dec);
                dsv = (2.0f * dtg - dcv * dt) * T_inv;
                hsv = (2.0f * htg - hcv * dt) * T_inv;
                psv = (2.0f * ptg - pcv * dt) * T_inv;
            }
            else
            {
                F32 it = cam->tm_acc + (cam->tmr - dt) - cam->tm_dec;
                F32 ot = 2.0f / (cam->tmr + cam->tm_acc - cam->tm_dec);
                F32 T_inv = 1.0f / (cam->tmr - cam->tm_acc);
                dsv = (2.0f * dtg - (dtg * ot + cam->depv) * 0.5f * it - dcv * dt) * T_inv;
                hsv = (2.0f * htg - (htg * ot + cam->hepv) * 0.5f * it - hcv * dt) * T_inv;
                psv = (2.0f * ptg - (ptg * ot + cam->pepv) * 0.5f * it - pcv * dt) * T_inv;
            }
            // dpv and vax/vay/vaz are never read; both PS2 DWARFs list them here.
            F32 dpv, hpv, ppv;
            F32 vax, vay, vaz;
            F32 s = dsv - dcv;
            vax = cam->mbasis.at.x * s;
            vaz = cam->mbasis.at.z * s;
            hpv = hsv - hcv;
            ppv = psv - pcv;
            vay = hpv;
            vax += cam->mbasis.right.x * ppv;
            vay += cam->mbasis.right.y * ppv;
            vaz += cam->mbasis.right.z * ppv;
            vax *= dt;
            vay *= dt;
            vaz *= dt;
            cam->mat.pos.x += vax;
            cam->mat.pos.y += vay;
            cam->mat.pos.z += vaz;
            cam->tmr = tnext;
        }
    }
    else
    {
        if (cam->flags & 0x2)
        {
            if (!xeq(cam->dcur, cam->dgoal, 1e-5f))
            {
                F32 dtg = cam->dgoal - cam->dcur;
                xCam_CorrectD(cam, dtg, dcv, dt);
            }
        }
        else if (cam->dmax > cam->dmin)
        {
            if (cam->dcur < cam->dmin)
            {
                F32 dtg = cam->dmin - cam->dcur;
                xCam_CorrectD(cam, dtg, dcv, dt);
            }
            else if (cam->dcur > cam->dmax)
            {
                F32 dtg = cam->dmax - cam->dcur;
                xCam_CorrectD(cam, dtg, dcv, dt);
            }
        }

        if (cam->flags & 0x4)
        {
            if (!xeq(cam->hcur, cam->hgoal, 1e-5f))
            {
                F32 htg = cam->hgoal - cam->hcur;
                xCam_CorrectH(cam, htg, hcv, dt);
            }
        }
        else if (cam->hmax > cam->hmin)
        {
            if (cam->hcur < cam->hmin)
            {
                F32 htg = cam->hmin - cam->hcur;
                xCam_CorrectH(cam, htg, hcv, dt);
            }
            else if (cam->hcur > cam->hmax)
            {
                F32 htg = cam->hmax - cam->hcur;
                xCam_CorrectH(cam, htg, hcv, dt);
            }
        }

        if (cam->flags & 0x8)
        {
            if (!xeq(cam->pcur, cam->pgoal, 1e-5f))
            {
                F32 ptg = cam->dcur * xDangleClamp(cam->pgoal - cam->pcur);
                xCam_CorrectP(cam, ptg, pcv, dt);
            }
        }
        else if (cam->pmax > cam->pmin)
        {
            F32 dphi = xDangleClamp(cam->pmax - cam->pcur);
            F32 dplo = xDangleClamp(cam->pmin - cam->pcur);
            if (dplo > 0.0f && (dphi > 0.0f || xabs(dplo) <= xabs(dphi)))
            {
                F32 ptg = (1e-5f + dplo) * cam->dcur;
                xCam_CorrectP(cam, ptg, pcv, dt);
            }
            else if (dphi < 0.0f)
            {
                F32 ptg = (dphi - 1e-5f) * cam->dcur;
                xCam_CorrectP(cam, ptg, pcv, dt);
            }
            else
            {
                xCam_DampP(cam, pcv, dt);
            }
        }
        else
        {
            xCam_DampP(cam, pcv, dt);
        }
    }

    if (cam->flags & 0x80)
    {
        xVec3 oeu, eu;
        xMat3x3GetEuler(&cam->mat, &eu);
        xMat3x3GetEuler(&cam->omat, &oeu);

        F32 m = 1.0f / last_dt;
        F32 ycv = m * xDangleClamp(eu.x - oeu.x);
        F32 pcv = m * xDangleClamp(eu.y - oeu.y);
        F32 rcv = m * xDangleClamp(eu.z - oeu.z);
        ycv *= cam->yaw_ccv;
        pcv *= cam->pitch_ccv;
        rcv *= cam->roll_ccv;

        xMat3x3Copy(&cam->omat, &cam->mat);
        cam->yaw_cur += ycv * dt;
        cam->pitch_cur += pcv * dt;
        cam->roll_cur += rcv * dt;

        if (cam->flags & 0x40)
        {
            F32 tnext = cam->ltmr - dt;
            if (tnext <= 0.0f)
            {
                cam->flags &= ~0x40;
                cam->ltmr = 0.0f;
            }
            else
            {
                F32 ytg = xDangleClamp(cam->yaw_goal - cam->yaw_cur);
                F32 ptg = xDangleClamp(cam->pitch_goal - cam->pitch_cur);
                F32 rtg = xDangleClamp(cam->roll_goal - cam->roll_cur);
                F32 ysv, psv, rsv;
                if (tnext <= cam->ltm_dec)
                {
                    F32 T_inv = 1.0f / cam->ltmr;
                    ysv = (2.0f * ytg - ycv * dt) * T_inv;
                    psv = (2.0f * ptg - pcv * dt) * T_inv;
                    rsv = (2.0f * rtg - rcv * dt) * T_inv;
                }
                else if (tnext <= cam->ltm_acc)
                {
                    F32 T_inv = 1.0f / (2.0f * cam->ltmr - dt - cam->ltm_dec);
                    ysv = (2.0f * ytg - ycv * dt) * T_inv;
                    psv = (2.0f * ptg - pcv * dt) * T_inv;
                    rsv = (2.0f * rtg - rcv * dt) * T_inv;
                }
                else
                {
                    F32 it = cam->ltm_acc + (cam->ltmr - dt) - cam->ltm_dec;
                    F32 ot = 2.0f / (cam->ltmr + cam->ltm_acc - cam->ltm_dec);
                    F32 T_inv = 1.0f / (cam->ltmr - cam->ltm_acc);
                    ysv = (2.0f * ytg - (ytg * ot + cam->yaw_epv) * 0.5f * it - ycv * dt) * T_inv;
                    psv = (2.0f * ptg - (ptg * ot + cam->pitch_epv) * 0.5f * it - pcv * dt) * T_inv;
                    rsv = (2.0f * rtg - (rtg * ot + cam->roll_epv) * 0.5f * it - rcv * dt) * T_inv;
                }
                F32 ypv = ysv - ycv;
                F32 ppv = psv - pcv;
                F32 rpv = rsv - rcv;
                cam->yaw_cur += ypv * dt;
                cam->pitch_cur += ppv * dt;
                cam->roll_cur += rpv * dt;
                xMat3x3Euler(&cam->mat, cam->yaw_cur, cam->pitch_cur, cam->roll_cur);
                cam->ltmr = tnext;
            }
        }
        else
        {
            if (!xeq(cam->yaw_cur, cam->yaw_goal, 1e-5f))
            {
                F32 ytg = xDangleClamp(cam->yaw_goal - cam->yaw_cur);
                xCam_CorrectYaw(cam, ytg, ycv, dt);
            }

            if (!xeq(cam->pitch_cur, cam->pitch_goal, 1e-5f))
            {
                F32 ptg = xDangleClamp(cam->pitch_goal - cam->pitch_cur);
                xCam_CorrectPitch(cam, ptg, pcv, dt);
            }

            if (!xeq(cam->roll_cur, cam->roll_goal, 1e-5f))
            {
                F32 rtg = xDangleClamp(cam->roll_goal - cam->roll_cur);
                xCam_CorrectRoll(cam, rtg, rcv, dt);
            }

            xMat3x3Euler(&cam->mat, cam->yaw_cur, cam->pitch_cur, cam->roll_cur);
        }
    }
    else
    {
        xQuatFromMat(&cam->orn_cur, &cam->mat);

        xQuat oq;
        xQuatFromMat(&oq, &cam->omat);

        xQuat qdiff_o_c;
        xQuatDiff(&qdiff_o_c, &oq, &cam->orn_cur);

        xRot rot_cv;
        xQuatToAxisAngle(&qdiff_o_c, &rot_cv.axis, &rot_cv.angle);
        rot_cv.angle *= m;
        rot_cv.angle = 0.0f; // sic: discards the scaled angle

        xMat3x3Copy(&cam->omat, &cam->mat);

        xVec3 f;
        xMat3x3RMulVec(&f, cam->tgt_mat, &cam->focus);
        xVec3AddTo(&f, &cam->tgt_mat->pos);

        // Normalized XZ direction from the camera to the target
        F32 atx, aty, atz;
        F32 dist;
        {
            F32 dx__ = cam->tgt_mat->pos.x - cam->mat.pos.x;
            F32 dz__ = cam->tgt_mat->pos.z - cam->mat.pos.z;
            F32 dist2 = SQR(dx__) + SQR(dz__);

            if (xeq(dist2, 1.0f, 1e-5f))
            {
                atx = dx__;
                atz = dz__;
                dist = 1.0f;
            }
            else if (xeq(dist2, 0.0f, 1e-5f))
            {
                atx = 0.0f;
                atz = 0.0f;
                dist = 0.0f;
            }
            else
            {
                dist = xsqrt(dist2);
                F32 dist_inv = 1.0f / dist;
                atx = dx__ * dist_inv;
                atz = dz__ * dist_inv;
            }
        }
        aty = 0.0f;

        if (cam->tgt_mat->at.x * atx + cam->tgt_mat->at.y * aty + cam->tgt_mat->at.z * atz < 0.0f)
        {
            F32 mpx = f.x - cam->tgt_mat->pos.x;
            F32 mpy = f.y - cam->tgt_mat->pos.y;
            F32 mpz = f.z - cam->tgt_mat->pos.z;
            F32 s = (mpx * atx + mpy * aty + mpz * atz) * -2.0f;
            mpx = atx * s;
            mpy = aty * s;
            mpz = atz * s;
            f.x += mpx;
            f.y += mpy;
            f.z += mpz;
        }

        xMat3x3 des_mat;
        xMat3x3LookAt(&des_mat, &f, &cam->mat.pos);

        xMat3x3 latgt;
        xMat3x3LookAt(&latgt, &cam->tgt_mat->pos, &cam->mat.pos);

        F32 ang_dist = xacos(latgt.at.x * des_mat.at.x + latgt.at.y * des_mat.at.y +
                             latgt.at.z * des_mat.at.z);

        if (ang_dist > 0.5235988f) // 30 degrees
        {
            xQuat a;
            xQuatFromMat(&a, &latgt);

            xQuat b;
            xQuatFromMat(&b, &des_mat);

            xQuat o;
            F32 s = PI - ang_dist;
            if (s < 1.5707964f) // 90 degrees
            {
                if (s > 0.08726646f) // 5 degrees
                {
                    xQuatSlerp(&o, &a, &b, s / ang_dist);
                }
                else
                {
                    o = a;
                }
            }
            else
            {
                xQuatSlerp(&o, &a, &b, 0.5235988f / ang_dist);
            }

            xQuatToMat(&o, &des_mat);
        }

        xQuat desq;
        xQuatFromMat(&desq, &des_mat);

        xCameraLook(cam, 0, &desq, 0.25f, 0.0f, 0.0f);

        xQuat difq;
        xQuatConj(&difq, &cam->orn_cur);
        xQuatMul(&difq, &difq, &desq);

        xQuat newq;
        xQuatSlerp(&newq, &cam->orn_cur, &desq, (0.425f / SECS_PER_VBLANK) * dt);
        xQuatToMat(&newq, &cam->mat);
    }

    while (xcam_do_collis && sCamCollis)
    {
        dualCamInfo dualCamInfo;
        xSweptSphere& sws = dualCamInfo.playerToCameraSWS;

        xBoundMakeSphere(dualCamInfo.cameraBound, cam->mat.pos, 1.8f);

        xVec3 tgtpos;
        tgtpos.x = cam->tgt_mat->pos.x;
        tgtpos.y = 0.7f + cam->tgt_mat->pos.y;
        tgtpos.z = cam->tgt_mat->pos.z;

        F32 swSphereRadius = 0.07f;
        F32 farClipUnused;
        iCameraGetNearFarClip(swSphereRadius, farClipUnused);

        xSweptSpherePrepare(&sws, &tgtpos, &cam->mat.pos, swSphereRadius);
        xSweptSphereToEnv(&sws, globals.sceneCur->env);

        xRay3 ray;
        xVec3Copy(&ray.origin, &sws.start);
        xVec3Sub(&ray.dir, &sws.end, &sws.start);

        ray.max_t = xVec3Length(&ray.dir);

        F32 one_len = 1.0f / MAX(ray.max_t, 1e-5f);
        xVec3SMul(&ray.dir, &ray.dir, one_len);

        ray.flags = 0x800;
        if (!(ray.flags & 0x400))
        {
            ray.flags |= 0x400;
            ray.min_t = 0.0f;
        }

        F32 dx = cam->mat.pos.x - tgtpos.x;
        F32 dy = cam->mat.pos.y - tgtpos.y;
        F32 dz = cam->mat.pos.z - tgtpos.z;
        F32 dist = xsqrt(dx * dx + dy * dy + dz * dz);
        if (dist > 1e10f)
        {
            ray.max_t = 0.0f;
        }

        xRayHitsGrid(&colls_grid, globals.sceneCur, &ray, DualCamHitsCameraEnt, &sws.qcd,
                     &dualCamInfo);
        xRayHitsGrid(&colls_oso_grid, globals.sceneCur, &ray, DualCamHitsCameraEnt, &sws.qcd,
                     &dualCamInfo);

        if (sws.curdist != sws.dist)
        {
            F32 stopdist = MAX(sws.curdist, 0.6f);
            stopdist = MIN(cam->smoothOutwardSlidePos, stopdist);
            cam->mat.pos.x = stopdist * ray.dir.x + ray.origin.x;
            cam->mat.pos.y = stopdist * ray.dir.y + ray.origin.y;
            cam->mat.pos.z = stopdist * ray.dir.z + ray.origin.z;
            cam->smoothOutwardSlidePos = stopdist;
        }
        else if (cam->smoothOutwardSlidePos < sws.curdist)
        {
            cam->smoothOutwardSlidePos += 0.15f;
            cam->smoothOutwardSlidePos = MIN(cam->smoothOutwardSlidePos, sws.curdist);
            cam->mat.pos.x = cam->smoothOutwardSlidePos * ray.dir.x + ray.origin.x;
            cam->mat.pos.y = cam->smoothOutwardSlidePos * ray.dir.y + ray.origin.y;
            cam->mat.pos.z = cam->smoothOutwardSlidePos * ray.dir.z + ray.origin.z;
        }

        break;
    }

    last_dt = dt;

    iCameraUpdatePos(cam->lo_cam, &cam->mat);
}

void xCameraUpdate(xCamera* cam, F32 dt)
{
    S32 i;
    S32 num_updates;
    F32 sdt;

    num_updates = std::ceilf(144.0f * dt);

    sdt = dt / num_updates;

    for (i = 0; i < num_updates; i++)
    {
        sCamCollis = (i == num_updates - 1);

        _xCameraUpdate(cam, sdt);
    }
}

void xCameraSetScene(xCamera* cam, xScene* sc)
{
    cam->sc = sc;

    iCameraAssignEnv(cam->lo_cam, sc->env->geom);
}

void xCameraSetTargetMatrix(xCamera* cam, xMat4x3* mat)
{
    cam->tgt_mat = mat;
}

void xCameraSetTargetOMatrix(xCamera* cam, xMat4x3* mat)
{
    cam->tgt_omat = mat;
}

void xCameraDoCollisions(S32 do_collis, S32 owner)
{
    xcam_collis_owner_disable &= ~(1 << owner);
    xcam_collis_owner_disable |= !do_collis << owner;

    xcam_do_collis = (xcam_collis_owner_disable == 0);
}

void xCameraMove(xCamera* cam, U32 flags, F32 dgoal, F32 hgoal, F32 pgoal, F32 tm, F32 tm_acc,
                 F32 tm_dec)
{
    cam->flags = (cam->flags & ~0x3E) | (flags & 0x3E);
    cam->dgoal = dgoal;
    cam->hgoal = hgoal;
    cam->pgoal = pgoal;

    if (tm <= 0.0f)
    {
        if (cam->tgt_mat)
        {
            cam->dcur = dgoal;
            cam->hcur = hgoal;
            cam->pcur = pgoal;

            xCam_cyltoworld(&cam->mat.pos, cam->tgt_mat, dgoal, hgoal, pgoal, cam->flags);

            cam->omat.pos = cam->mat.pos;
            cam->yaw_cur = cam->yaw_goal = cam->pcur + ((cam->pcur >= PI) ? -PI : PI);
        }
    }
    else
    {
        cam->flags |= 0x1;
        cam->tm_acc = tm - tm_acc;
        cam->tm_dec = tm_dec;
        cam->tmr = tm;

        F32 s = 1.0f / (tm - 0.5f * (tm_acc - tm_dec));

        cam->depv = s * (dgoal - cam->dcur);
        cam->hepv = s * (hgoal - cam->hcur);
        cam->pepv = xDangleClamp(pgoal - cam->pcur) * s * 0.5f * (dgoal + cam->dcur);
    }
}

void xCameraMove(xCamera* cam, const xVec3& loc)
{
    // xVec3::operator= needs to be inlined? And use lwz/stw for some reason?
    cam->omat.pos = cam->mat.pos = loc;
    cam->flags &= ~0x3E;
    cam->tm_acc = cam->tm_dec = cam->tmr = 0.0f;
}

void xCameraMove(xCamera* cam, const xVec3& loc, F32 maxSpeed)
{
    xVec3 var;
    F32 len;

    xVec3Sub(&var, &loc, &cam->mat.pos);
    len = xVec3Length(&var);

    if (len > maxSpeed)
    {
        xVec3SMul(&var, &var, maxSpeed / len);
        xVec3Add(&cam->mat.pos, &cam->mat.pos, &var);
    }
    else
    {
        cam->mat.pos = loc;
    }

    cam->omat.pos = cam->mat.pos;
    cam->flags &= ~0x3E;
    cam->tm_acc = cam->tm_dec = cam->tmr = 0.0f;
}

void xCameraFOV(xCamera* cam, F32 fov, F32 maxSpeed, F32 dt)
{
    F32 speed = maxSpeed * dt;

    if (cam->fov != fov)
    {
        if (speed)
        {
            F32 len = fov - cam->fov;

            if (iabs(len) > speed)
            {
                len *= speed / len;
                cam->fov += len;
            }
            else
            {
                cam->fov = fov;
            }
        }
        else
        {
            cam->fov = fov;
        }
    }
}

void xCameraLook(xCamera* cam, U32 flags, const xQuat* orn_goal, F32 tm, F32 tm_acc, F32 tm_dec)
{
    cam->flags = (cam->flags & ~0xF80) | (flags & 0xF80);
    cam->orn_goal = *orn_goal;

    if (tm <= 0.0f)
    {
        if (cam->tgt_mat)
        {
            xQuatToMat(orn_goal, &cam->mat);
            xMat3x3Copy(&cam->omat, &cam->mat);
        }
    }
    else
    {
        cam->flags |= 0x40;
        cam->ltm_acc = tm - tm_acc;
        cam->ltm_dec = tm_dec;
        cam->ltmr = tm;

        xQuatDiff(&cam->orn_diff, &cam->orn_cur, orn_goal);

        F32 s = 1.0f / (tm - 0.5f * (tm_acc - tm_dec));

        cam->orn_epv = s * xQuatGetAngle(&cam->orn_diff);
    }
}

void xCameraLookYPR(xCamera* cam, U32 flags, F32 yaw, F32 pitch, F32 roll, F32 tm, F32 tm_acc,
                    F32 tm_dec)
{
    cam->flags = (cam->flags & ~0xF80) | (flags & 0xF80) | 0x80;
    cam->yaw_goal = yaw;
    cam->pitch_goal = pitch;
    cam->roll_goal = roll;

    if (tm <= 0.0f)
    {
        if (cam->tgt_mat)
        {
            cam->yaw_cur = yaw;
            cam->pitch_cur = pitch;
            cam->roll_cur = roll;

            xMat3x3Euler(&cam->mat, yaw, pitch, roll);

            *(xMat3x3*)&cam->omat = *(xMat3x3*)&cam->mat;
        }
    }
    else
    {
        cam->flags |= 0x40;
        cam->ltm_acc = tm - tm_acc;
        cam->ltm_dec = tm_dec;
        cam->ltmr = tm;

        F32 s = 1.0f / (tm - 0.5f * (tm_acc - tm_dec));

        cam->yaw_epv = s * xDangleClamp(yaw - cam->yaw_cur);
        cam->pitch_epv = s * xDangleClamp(pitch - cam->pitch_cur);
        cam->roll_epv = s * xDangleClamp(roll - cam->roll_cur);
    }
}

void xCameraRotate(xCamera* cam, const xMat3x3& m, F32 time, F32 accel, F32 decl)
{
    xVec3 eu;

    cam->flags = cam->flags & ~0xF80 | 0x80;

    xMat3x3GetEuler(&m, &eu);

    cam->yaw_goal = eu.x;
    cam->pitch_goal = eu.y;
    cam->roll_goal = eu.z;

    if (0.0f == time)
    {
        cam->yaw_cur = eu.x;
        cam->pitch_cur = eu.y;
        cam->roll_cur = eu.z;
    }

    *(xMat3x3*)&cam->mat = m;

    if (0.0f == time)
    {
        *(xMat3x3*)&cam->omat = m;
    }

    if (0.0f == time)
    {
        cam->ltm_acc = cam->ltm_dec = cam->ltmr = 0.0f;
    }
    else
    {
        cam->ltm_acc = accel;
        cam->ltm_dec = decl;
        cam->ltmr = time;
    }

    cam->yaw_epv = cam->pitch_epv = cam->roll_epv = 0.0f;
}

void xCameraRotate(xCamera* cam, const xVec3& v, F32 roll, F32 time, F32 accel, F32 decl)
{
    cam->yaw_goal = xatan2(v.x, v.z);
    cam->pitch_goal = -xasin(v.y);
    cam->roll_goal = roll;

    if (0.0f == time)
    {
        cam->yaw_cur = cam->yaw_goal;
        cam->pitch_cur = cam->pitch_goal;
        cam->roll_cur = cam->roll_goal;
    }

    cam->flags = (cam->flags & ~0xF80) | 0x80;

    xMat3x3Euler(&cam->mat, cam->yaw_goal, cam->pitch_goal, cam->roll_goal);

    if (0.0f == time)
    {
        *(xMat3x3*)&cam->omat = *(xMat3x3*)&cam->mat;
    }

    if (0.0f == time)
    {
        cam->ltm_acc = cam->ltm_dec = cam->ltmr = 0.0f;
    }
    else
    {
        cam->ltm_acc = accel;
        cam->ltm_dec = decl;
        cam->ltmr = time;
    }

    cam->yaw_epv = cam->pitch_epv = cam->roll_epv = 0.0f;
}

// xBinaryCamera (BfBB's last xCamera.cpp functions): unused in TSSM and dead-stripped, but retail
// still compiled it, as xCamera's .sdata2 opens with the literals its update creates. The body
// follows BfBB's in the TSSM form of zBossCam_Binary::update.
static void bound_sphere_xz(xVec3& Q1, xVec3& Q2, const xVec3& center, F32 radius, const xVec3& dir,
                            F32 dist)
{
    F32 ratio = radius / dist;
    F32 side = ratio * xsqrt(dist * dist - radius * radius);
    F32 along = radius * ratio;
    F32 along_x = along * dir.x;
    F32 along_z = along * dir.z;
    F32 side_x = side * dir.x;
    F32 side_z = side * dir.z;

    Q1.x = along_x + (center.x + side_z);
    Q1.y = center.y;
    Q1.z = along_z + (center.z - side_x);

    Q2.x = along_x + (center.x - side_z);
    Q2.y = center.y;
    Q2.z = along_z + (center.z + side_x);
}

void xBinaryCamera::init()
{
    camera = NULL;
    s1 = s2 = NULL;
}

void xBinaryCamera::start(xCamera& camera)
{
    this->camera = &camera;

    xQuatFromMat(&cam_dir, &camera.mat);

    stick_offset = 0.0f;
}

void xBinaryCamera::stop()
{
    camera = NULL;
}

void xBinaryCamera::update(F32 dt)
{
    xVec3& A = camera->mat.pos;
    const xVec3& B = *s1;
    const xVec3& C = *s2;

    xVec3 CA = { 0.0f, 0.0f, 0.0f };
    CA.x = A.x - C.x;
    CA.z = A.z - C.z;

    F32 dCA = CA.length();
    if (dCA < 0.01f)
    {
        CA.assign(A.x - B.x, 0.0f, A.z - B.z).right_normalize();
        dCA = 0.01f;
    }
    else
    {
        CA /= dCA;
    }

    F32 yaw_start = xatan2(B.x - A.x, B.z - A.z);
    F32 yaw_end;
    if (dCA > s2_radius)
    {
        xVec3 Q1, Q2;
        bound_sphere_xz(Q1, Q2, C, s2_radius, CA, dCA);
        F32 yaw_Q1 = xatan2(Q1.x - A.x, Q1.z - A.z);
        F32 yaw_Q2 = xatan2(Q2.x - A.x, Q2.z - A.z);
        F32 dyaw1 = xrmod(yaw_Q1 - yaw_start + PI) - PI;
        F32 dyaw2 = xrmod(yaw_Q2 - yaw_start + PI) - PI;

        F32 fov = 0.5f * (0.017453292f * xCameraGetFOV(camera)) + cfg.margin_angle;
        fov = range_limit(fov, 0.0f, PI);
        F32 max_dyaw = 0.5f * xabs(dyaw2 - dyaw1);
        if (max_dyaw > fov)
        {
            yaw_end = xatan2(C.x - B.x, C.z - B.z);
        }
        else if (dyaw1 >= dyaw2)
        {
            yaw_end = xatan2(C.x - B.x, C.z - B.z);
        }
        else if (dyaw1 >= -fov)
        {
            if (dyaw2 <= fov)
            {
                yaw_end = yaw_start;
            }
            else
            {
                yaw_end = yaw_start + (dyaw2 - fov);
            }
        }
        else
        {
            yaw_end = yaw_start + (dyaw1 + fov);
        }
    }
    else
    {
        yaw_end = xatan2(C.x - B.x, C.z - B.z);
    }

    F32 sstick = 1.0f - xexp(-cfg.stick_speed * dt);
    analog_data& stick = globals.pad0->analog[1];

    stick_offset.x += (cfg.stick_yaw_vel * stick.offset.x * dt - stick_offset.x) * sstick;
    yaw_end += stick_offset.x;

    F32 yaw_diff = xrmod(yaw_end - yaw_start + PI) - PI;
    F32 max_yaw_diff = cfg.max_yaw_vel * dt;
    if (xabs(yaw_diff) > max_yaw_diff)
    {
        if (yaw_diff < 0.0f)
        {
            if (max_yaw_diff > 0.0f)
            {
                max_yaw_diff = -max_yaw_diff;
            }
        }
        else
        {
            if (max_yaw_diff < 0.0f)
            {
                max_yaw_diff = -max_yaw_diff;
            }
        }
        yaw_end = yaw_start + max_yaw_diff;
    }

    stick_offset.y += (stick.offset.y - stick_offset.y) * sstick;

    F32 d, h, hf;
    if (stick_offset.y > 0.0f)
    {
        F32 s = stick_offset.y;
        d = xlerp(cfg.zone_rest.distance, cfg.zone_below.distance, s);
        h = xlerp(cfg.zone_rest.height, cfg.zone_below.height, s);
        hf = xlerp(cfg.zone_rest.height_focus, cfg.zone_below.height_focus, s);
    }
    else
    {
        F32 s = -stick_offset.y;
        d = xlerp(cfg.zone_rest.distance, cfg.zone_above.distance, s);
        h = xlerp(cfg.zone_rest.height, cfg.zone_above.height, s);
        hf = xlerp(cfg.zone_rest.height_focus, cfg.zone_above.height_focus, s);
    }

    xVec3 end_loc = { 0.0f, 0.0f, 0.0f };
    end_loc.x = B.x - d * isin(yaw_end);
    end_loc.y = B.y + h;
    end_loc.z = B.z - d * icos(yaw_end);

    F32 sloc = 1.0f - xexp(-cfg.move_speed * dt);

    xVec3 cam_loc = { 0.0f, 0.0f, 0.0f };
    cam_loc.x = xlerp(A.x, end_loc.x, sloc);
    cam_loc.y = xlerp(A.y, end_loc.y, sloc);
    cam_loc.z = xlerp(A.z, end_loc.z, sloc);

    xVec3 heading = { 0.0f, 0.0f, 0.0f };
    heading.x = B.x - end_loc.x;
    heading.y = B.y - end_loc.y + hf;
    heading.z = B.z - end_loc.z;

    if (heading.length2() >= 0.001f)
    {
        heading /= heading.length();

        xQuat end_dir;
        xMat3x3 mat;
        xMat3x3LookVec(&mat, &heading.invert());
        xQuatFromMat(&end_dir, &mat);

        xQuatSlerp(&cam_dir, &cam_dir, &end_dir, 1.0f - xexp(-cfg.turn_speed * dt));
    }

    xMat3x3 mat;
    xQuatToMat(&cam_dir, &mat);

    xCameraMove(camera, cam_loc);
    xCameraRotate(camera, mat, 0.0f, 0.0f, 0.0f);

    render_debug();
}
