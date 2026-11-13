#include "zFX.h"

#include "xMath.h"
#include "xMath3.h"
#include "xVec3Inlines.h"
#include "xMemMgr.h"
#include "xstransvc.h"
#include "iParMgr.h"

#include "zParPTank.h"
#include "zGoo.h"
#include "xClimate.h"
#include "xSnd.h"

#include <rpskin.h>

#include <stdio.h>
#include <string.h>
#include "xCam.h"

U32 gFXSurfaceFlags;

static const xFXRing sPatrickStunRing[3] = {
    { 0x741b0566,
      1.0f,
      { 0.0f, 0.0f, 0.0f },
      0.0f,
      0.5f,
      4.0f,
      PI / 2.0f,
      PI / -2.0f,
      0.5f,
      -0.25f,
      { 50, 50, 200, 255 },
      32,
      3,
      1,
      NULL,
      NULL },
    { 0x741b0566,
      1.5f,
      { 0.0f, 0.0f, 0.0f },
      0.0f,
      0.75f,
      5.0f,
      PI / 2.0f,
      PI / -2.0f,
      0.375f,
      -0.1875f,
      { 50, 50, 150, 255 },
      32,
      2,
      1,
      NULL,
      NULL },
    { 0x741b0566,
      2.0f,
      { 0.0f, 0.0f, 0.0f },
      0.0f,
      1.0f,
      6.0f,
      PI / 2.0f,
      PI / -2.0f,
      0.25f,
      -0.125f,
      { 50, 50, 100, 255 },
      32,
      1,
      1,
      NULL,
      NULL },
};
static const xFXRing sThunderRing[1] = {
    { 0x741b0566,
      0.5f,
      { 0.0f, 0.0f, 0.0f },
      0.0f,
      0.0f,
      5.0f,
      PI / 2.0f,
      PI / -2.0f,
      0.75f,
      -0.6f,
      { 255, 255, 255, 255 },
      32,
      2,
      1,
      NULL,
      NULL },
};
static const xFXRing sHammerRing[1] = {
    { 0x741b0566,
      0.75f,
      { 0.0f, 0.0f, 0.0f },
      0.0f,
      0.4f,
      2.4f,
      PI / 2.0f,
      PI / -2.0f,
      0.6f,
      -0.6f,
      { 255, 255, 255, 127 },
      32,
      2,
      1,
      NULL,
      NULL },
};
static const xFXRing sPorterRing[2] = {
    { 0x741b0566,
      1.2f,
      { 0.0f, 0.0f, 0.0f },
      0.1f,
      0.0f,
      3.0f,
      PI / 2.0f,
      PI / -2.0f,
      0.6f,
      -0.6f,
      { 180, 180, 45, 127 },
      16,
      2,
      1,
      NULL,
      NULL },
    { 0x741b0566,
      0.5f,
      { 0.0f, 0.25f, 0.0f },
      0.0f,
      1.5f,
      -3.0f,
      PI / -2.0f,
      PI / 2.0f,
      1.2f,
      -2.4f,
      { 64, 128, 128, 127 },
      8,
      2,
      1,
      NULL,
      NULL },
};
static const xFXRing sMuscleArmRing[1] = {
    { 0x741b0566,
      3.0f,
      { 0.0f, 0.0f, 0.0f },
      0.0f,
      0.0f,
      90.0f,
      PI / 2.0f,
      0.0f,
      0.6f,
      30.0f,
      { 255, 255, 255, 160 },
      48,
      1,
      1,
      NULL,
      NULL },
};
static const F32 defaultGooTimes[4] = { 0.00001f, 2.0f, 15.0f, 2.0f };
static const F32 defaultGooWarbc[4] = { 0.25f, 2.0f, 0.25f, 1.2f };
zFXGooInstance zFXGooInstances[24];
// Defined with the popper code below, as in the PS2 build's data order.
namespace
{
    extern const U16 popper_weight_sizes[8];
    extern popper_data poppers[8];
    extern popper_data instantPopper;
    extern entrail_data* entrails;
    extern U32 entrails_size;

    void emit_popper_bubbles(popper_data& popper, S32 emit, F32 scale, F32 vel_add);
    void update_popper(popper_data& popper, F32 dt);
    U8 setup_popper_emitter(popper_data& popper);
    void eval_tri(xVec3* vert, xVec3* norm, const xMat4x3* mat, const RpGeometry* geom,
                  const RpTriangle* tri);
    inline void destroy_popper(popper_data& popper);
} // namespace

static void update_entrails(F32 dt);
RpAtomic* zFXGooRenderAtomic(RpAtomic* atomic);

static void on_spawn_bubble_wall(const tweak_info& tweak)
{
    zFX_SpawnBubbleWall();
}

inline void init_poppers()
{
    popper_data* it = poppers;
    popper_data* end = poppers + (sizeof(poppers) / sizeof(poppers[0]));
    for (S32 i = 0; it != end; ++it, ++i)
    {
        it->weight = (F32*)xMemAllocSize(popper_weight_sizes[i] * sizeof(F32));
        it->max_faces = popper_weight_sizes[i];
    }

    instantPopper.weight = (F32*)xMemAllocSize(0x800 * sizeof(F32));
    instantPopper.max_faces = 0x800;
}

inline void reset_poppers()
{
    popper_data* it = poppers;
    popper_data* end = poppers + (sizeof(poppers) / sizeof(poppers[0]));
    for (; it != end; ++it)
    {
        it->state = STATE_NONE;
        it->ent = NULL;
    }
}

inline void update_poppers(F32 dt)
{
    popper_data* it = poppers;
    popper_data* end = poppers + (sizeof(poppers) / sizeof(poppers[0]));
    for (; it != end; ++it)
    {
        if (it->state != STATE_NONE)
        {
            update_popper(*it, dt);
            if (it->time >= it->end_time && it->state != STATE_CONSTANT)
            {
                destroy_popper(*it);
            }
        }
    }
}

inline void reset_entrails()
{
    entrail_data* it = entrails;
    entrail_data* end = entrails + entrails_size;
    for (; it != end; ++it)
    {
        it->reset();
    }
}

static void setup_entrails(zScene& s);

// No symbol in TSSM (auto-inlined into zGameSetup and dead-stripped). A plain function, not
// 'inline': retail's cb_spawn_bubble_wall is an ordinary local static, not a weak one.
void zFX_SceneEnter(RpWorld* world)
{
    xFXanimUV2PSetTexture(NULL);
    xFX_SceneEnter(world);
    zFXGoo_SceneEnter();
    reset_poppers();
    setup_entrails(*globals.sceneCur);

    // Only registered with the debug tweak menu (xDebugAddTweak), which retail compiles out.
    static tweak_callback cb_spawn_bubble_wall =
        tweak_callback::create_change(on_spawn_bubble_wall);
}

static ztextbox* sGooTimerTextBox;
static RwTexture* g_txtr_gooFrozen;
static iSndHandle sGooSndHandle = (iSndHandle)-1;
static iSndGroupHandle sGooSndGrpHandle = (iSndGroupHandle)-1;
xEnt* g_entForGoo[24];

inline void zFXGoo_SceneEnter()
{
    S32 i;
    zFXGooInstance* goo = zFXGooInstances;
    for (i = 0; i < 24; i++, goo++)
    {
        memset(goo, 0, sizeof(zFXGooInstance));
        goo->state = zFXGooStateInactive;
    }

    // xStrHash("FREEZY_TIMER_TEXTBOX")
    sGooTimerTextBox = (ztextbox*)zSceneFindObject(0xCB37A3A4);
    sGooSndGrpHandle = xSndMgrGetSoundGroup(xStrHash("Ffruit_crackle_grp"));
    sGooSndHandle = (iSndHandle)-1;
}

inline void zFXGoo_SceneReset()
{
    S32 i;
    zFXGooInstance* goo = zFXGooInstances;
    for (i = 0; i < 24; i++, goo++)
    {
        if (goo->state != zFXGooStateInactive)
        {
            goo->state = zFXGooStateNormal;
        }
    }
}

inline void zFXGoo_SceneExit()
{
    S32 i;
    zFXGooInstance* goo = zFXGooInstances;
    for (i = 0; i < 24; i++, goo++)
    {
        if (goo->state != zFXGooStateInactive && goo->geom2 != NULL)
        {
            RpGeometryDestroy(goo->geom2);
        }
        memset(goo, 0, sizeof(zFXGooInstance));
        goo->state = zFXGooStateInactive;
    }
}

inline void zFXGooUpdate(F32 dt)
{
    S32 i;
    zFXGooInstance* goo = zFXGooInstances;
    for (i = 0; i < 24; i++, goo++)
    {
        if (goo->state != zFXGooStateInactive)
        {
            zFXGooUpdateInstance(goo, dt);
        }
    }
}

inline F32 zFXGooFreezeTimeLeft()
{
    zFXGooInstance* goo = zFXGooInstances;
    zFXGooInstance* end = goo + 24;
    F32 maxTime = 0.0f;
    for (; goo != end; goo++)
    {
        F32 time = 0.0f;
        switch (goo->state)
        {
        case zFXGooStateFreezing:
            time += goo->state_time[zFXGooStateFrozen];
        case zFXGooStateFrozen:
            time += goo->state_time[zFXGooStateMelting];
        case zFXGooStateMelting:
            time += goo->timer - goo->time;
            break;
        }

        if (maxTime < time)
        {
            maxTime = time;
        }
    }
    return maxTime;
}

void zFX_GameInit()
{
    init_poppers();
}

void zFX_SceneExit(RpWorld* world)
{
    xFX_SceneExit(world);
    zFXGoo_SceneExit();
    reset_poppers();
    entrails = NULL;
    entrails_size = 0;
}

void zFX_SceneReset()
{
    zFXGoo_SceneReset();
    reset_poppers();
    reset_entrails();
}

void zFXPatrickStun(const xVec3* pos)
{
    xFXRingCreate(pos, &sPatrickStunRing[0]);
    xFXRingCreate(pos, &sPatrickStunRing[1]);
    xFXRingCreate(pos, &sPatrickStunRing[2]);
}

void zFXGooEnable(xModelInstance* gooModel, S32 freezeGroup)
{
    S32 i;
    RpAtomic* atomic = gooModel->Data;
    zFXGooInstance* goo = zFXGooInstances;

    g_txtr_gooFrozen = NULL;

    for (i = 0; i < 24; i++, goo++)
    {
        if (goo->state == zFXGooStateInactive)
        {
            break;
        }
        if (goo->atomic == atomic)
        {
            return;
        }
    }

    if (i == 24)
    {
        return;
    }

    goo->freezeGroup = freezeGroup;

    RpGeometry* geom = RpAtomicGetGeometry(atomic);
    S32 numverts = geom->numVertices;
    S32 numtris = geom->numTriangles;

    if (geom->preLitLum == NULL)
    {
        goo->geom2 = RpGeometryCreate(numverts, numtris, 0x7E);

        RpMorphTarget* mtp2 = goo->geom2->morphTarget;
        RwV3d* ov = geom->morphTarget->verts;
        RwV3d* nv = mtp2->verts;
        RwV3d* on = geom->morphTarget->normals;
        RwV3d* nn = mtp2->normals;
        RwRGBA* nc = goo->geom2->preLitLum;
        RwTexCoords* ouv = geom->texCoords[0];
        RwTexCoords* nuv = goo->geom2->texCoords[0];

        for (i = 0; i < numverts; i++)
        {
            *nv++ = *ov++;
            *nn++ = *on++;
            nc->red = 255;
            nc->green = 255;
            nc->blue = 255;
            nc->alpha = 255;
            nc++;
            *nuv++ = *ouv++;
        }

        RpTriangle* ot = geom->triangles;
        RpTriangle* nt = goo->geom2->triangles;
        for (i = 0; i < numtris; i++, ot++, nt++)
        {
            RwUInt16 v1, v2, v3;
            RpGeometryTriangleGetVertexIndices(geom, ot, &v1, &v2, &v3);
            RpGeometryTriangleSetVertexIndices(goo->geom2, nt, v1, v2, v3);

            RpMaterial* mp = RpGeometryTriangleGetMaterial(geom, ot);
            RpGeometryTriangleSetMaterial(goo->geom2, nt, mp);
        }

        RwSphere boundingSphere;
        RpMorphTargetCalcBoundingSphere(mtp2, &boundingSphere);
        mtp2->boundingSphere = boundingSphere;

        RpGeometryUnlock(goo->geom2);
        RpAtomicSetGeometry(atomic, goo->geom2, 0);
        geom = goo->geom2;
    }

    xVec3* orig_verts = (xVec3*)xMemAllocSize(numverts * sizeof(xVec3));
    RwRGBA* orig_colors = (RwRGBA*)xMemAllocSize(numverts * sizeof(RwRGBA));
    // F32* as in the Ratatouille GC DWARF (the instance field is RwTexCoords*)
    F32* orig_uvs = (F32*)xMemAllocSize(numverts * sizeof(RwTexCoords));
    // Int literals in the copies: with sizeof at both the allocation and the memcpy, GC/2.6 merges
    // the two products and keeps them in saved registers, while retail recomputes them.
    memcpy(orig_verts, geom->morphTarget->verts, numverts * 12);
    memcpy(orig_colors, geom->preLitLum, numverts * 4);
    memcpy(orig_uvs, geom->texCoords[0], numverts * 8);

    RpAtomicSetRenderCallBack(atomic, zFXGooRenderAtomic);

    goo->atomic = atomic;
    goo->orig_verts = orig_verts;
    goo->orig_colors = orig_colors;
    goo->pipeFlags = gooModel->Pipe.Flags;
    goo->orig_uvs = (RwTexCoords*)orig_uvs;

    memcpy(goo->warbc, defaultGooWarbc, sizeof(defaultGooWarbc));
    goo->w0 = goo->warbc[0];
    goo->w2 = goo->warbc[2];
    memcpy(goo->state_time, defaultGooTimes, sizeof(defaultGooTimes));

    goo->state = zFXGooStateNormal;
    goo->time = 0.0f;
    goo->min = 3.0f;
    goo->max = 4.0f;
    goo->ref_parentPos = NULL;

    if (gCurEnv != NULL)
    {
        gCurEnv->easset->climateFlags = 2;
        gCurEnv->easset->climateStrengthMin = 0.0f;
        gCurEnv->easset->climateStrengthMax = 0.0f;
    }
}

void zFXGooUpdateInstance(zFXGooInstance* goo, F32 dt)
{
    zFXGooState old_state = goo->state;
    S32 s;

    if (goo->state != zFXGooStateNormal)
    {
        goo->time += dt;
        if (goo->time >= goo->timer)
        {
            goo->state = (zFXGooState)((goo->state + 1) & 0x3);
            goo->timer = (goo->time + goo->state_time[goo->state]) - (goo->time - goo->timer);
        }
    }

    // Retail bug: the comparison sits inside xabs, so this tests the bool result.
    if (xabs(goo->state_time[goo->state] < 1e-5f))
    {
        goo->state_time[goo->state] = 1e-5f;
    }

    goo->alpha = 1.0f - (goo->timer - goo->time) / goo->state_time[goo->state];
    goo->alpha = CLAMP(goo->alpha, 0.0f, 1.0f);

    switch (goo->state)
    {
    case zFXGooStateNormal:
        if (old_state == zFXGooStateMelting)
        {
            zGooMeltFinished(goo->atomic);
        }
        goo->alpha = 0.0f;
        break;
    case zFXGooStateFreezing:
    {
        F32 rate = xpow(1.1f, 60.0f * dt);
        goo->min *= rate;
        goo->max *= rate;
        break;
    }
    case zFXGooStateFrozen:
        goo->alpha = 1.0f;
        break;
    case zFXGooStateMelting:
    {
        if (old_state != zFXGooStateMelting)
        {
            xEnt* ent = zGooFindEnt(goo->atomic);
            if (ent != NULL)
            {
                zEntEvent(ent, eEventGooMeltStart);
                if (!xSndMgrIsPlaying(sGooSndGrpHandle))
                {
                    sGooSndHandle = xSndMgrPlay(sGooSndGrpHandle, 0, (xVec3*)&ent->model->Mat->pos,
                                                NULL, NULL, NULL, NULL);
                }
            }
        }

        goo->alpha = 1.0f - goo->alpha;
        F32 rate = xpow(0.9090909f, 60.0f * dt);
        goo->min *= rate;
        goo->max *= rate;
        break;
    }
    }

    goo->warbc[0] = goo->w0 * (1.0f - goo->alpha);
    goo->warbc[2] = goo->w2 * (1.0f - goo->alpha);
    F32 tmp = xpow(1.0f - goo->alpha, 1.5f);
    goo->warb_time += tmp * dt;

    if (goo->alpha < 1.0f && goo->atomic != NULL)
    {
        RpGeometry* geom = RpAtomicGetGeometry(goo->atomic);
        if (geom != NULL && RpGeometryLock(geom, 2))
        {
            F32 t = goo->warb_time;
            xVec3* ov = goo->orig_verts;
            RwV3d* nv = geom->morphTarget->verts;
            for (s = 0; s < geom->numVertices; s++, ov++, nv++)
            {
                F32 a = xfmod(goo->warbc[1] * (ov->x + t), 2 * PI);
                F32 b = xfmod(goo->warbc[3] * (ov->z + t), 2 * PI);
                a = goo->warbc[0] * isin(a) + ov->y;
                nv->y = goo->warbc[2] * isin(b) + a;
            }

            RpGeometryUnlock(geom);
        }
    }

    if (sGooTimerTextBox != NULL)
    {
        F32 freeze_time = zFXGooFreezeTimeLeft();
        if (freeze_time > 0.0f)
        {
            // not in the DWARF: the clamped counter value for the "%02d" text.
            S32 len = freeze_time;
            if (len > 99)
            {
                len = 99;
            }

            static char counter_text[] = "##:##";
            sprintf(counter_text, "%02d", len);
            sGooTimerTextBox->set_text(counter_text);
            sGooTimerTextBox->activate(false);
        }
        else
        {
            sGooTimerTextBox->deactivate();
            xClimateSetSnow(0.0f);

            xEnt* ent = zGooFindEnt(goo->atomic);
            if (ent != NULL)
            {
                ent->model->Pipe.Flags = goo->pipeFlags;
            }
        }
    }
}

RpAtomic* zFXGooRenderAtomic(RpAtomic* atomic)
{
    if (g_txtr_gooFrozen == NULL)
    {
        g_txtr_gooFrozen = (RwTexture*)xSTFindAsset(0x13401f, NULL);
    }

    gAtomicRenderCallBack(atomic);

    S32 i;
    zFXGooInstance* goo = zFXGooInstances;
    for (i = 0; i < 24; i++, goo++)
    {
        if (goo->state != zFXGooStateInactive && goo->atomic == atomic)
        {
            break;
        }
    }

    xVec3 vec_parMotion;
    if (goo->ref_parentPos != NULL)
    {
        vec_parMotion = *goo->ref_parentPos;
    }
    else
    {
        vec_parMotion = g_O3;
    }

    if (i != 24 && goo->state != zFXGooStateInactive && goo->state != zFXGooStateNormal)
    {
        RwIm3DVertex* Im3DBuffer = gRenderBuffer.m_vertex;
        U32 Im3DBufferPos = 0;

        if (g_txtr_gooFrozen != NULL)
        {
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER,
                             (void*)RwTextureGetRaster(g_txtr_gooFrozen));
        }
        else
        {
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
        }
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);

        RpGeometry* geom = RpAtomicGetGeometry(atomic);
        xVec3* vert = (xVec3*)geom->morphTarget->verts;
        xVec3* normals = (xVec3*)geom->morphTarget->normals;
        RwRGBA* color = geom->preLitLum;

        if (xabs(goo->max - goo->min) < 1e-5f)
        {
            goo->max += 1e-5f;
        }

        F32 A = (-255.0f * goo->alpha) / (goo->max - goo->min);
        F32 B = (255.0f * goo->alpha * goo->min) / (goo->max - goo->min);

        U8* alpha = (U8*)xMemPushTemp(geom->numVertices);

        S32 j;
        for (j = 0; j < geom->numVertices; j++)
        {
            xVec3 vec;
            xVec3Sub(&vec, &vert[j], &goo->center);
            F32 d2 = A * xVec3Length2(&vec) + B;
            alpha[j] = CLAMP(d2, 0.0f, 255.0f);
        }

        RpTriangle* tri = geom->triangles;
        for (j = 0; j < geom->numTriangles; j++, tri++)
        {
            if (Im3DBufferPos > 0x1dd)
            {
                if (RwIm3DTransform(Im3DBuffer, Im3DBufferPos, NULL, 0x19) != NULL)
                {
                    RwIm3DRenderPrimitive(rwPRIMTYPETRILIST);
                    RwIm3DEnd();
                }
                Im3DBufferPos = 0;
            }

            RwIm3DVertex* imv = &Im3DBuffer[Im3DBufferPos];

            xVec3 v0, v1, v2;
            xVec3 n0, n1, n2;
            xVec3 resultV0, resultV1, resultV2;

            v0 = vert[tri->vertIndex[0]];
            v1 = vert[tri->vertIndex[1]];
            v2 = vert[tri->vertIndex[2]];

            v0 += vec_parMotion;
            v1 += vec_parMotion;
            v2 += vec_parMotion;

            n0 = normals[tri->vertIndex[0]];
            n1 = normals[tri->vertIndex[1]];
            n2 = normals[tri->vertIndex[2]];

            resultV0 = v0 + n0 * 0.02f;
            resultV1 = v1 + n1 * 0.02f;
            resultV2 = v2 + n2 * 0.02f;

            RwIm3DVertexSetPos(&imv[0], resultV0.x, resultV0.y, resultV0.z);
            RwIm3DVertexSetPos(&imv[1], resultV1.x, resultV1.y, resultV1.z);
            RwIm3DVertexSetPos(&imv[2], resultV2.x, resultV2.y, resultV2.z);

            RwIm3DVertexSetRGBA(&imv[0], color[tri->vertIndex[0]].red,
                                color[tri->vertIndex[0]].green, color[tri->vertIndex[0]].blue,
                                alpha[tri->vertIndex[0]]);
            RwIm3DVertexSetRGBA(&imv[1], color[tri->vertIndex[1]].red,
                                color[tri->vertIndex[1]].green, color[tri->vertIndex[1]].blue,
                                alpha[tri->vertIndex[1]]);
            RwIm3DVertexSetRGBA(&imv[2], color[tri->vertIndex[2]].red,
                                color[tri->vertIndex[2]].green, color[tri->vertIndex[2]].blue,
                                alpha[tri->vertIndex[2]]);

            imv[0].u = goo->orig_uvs[tri->vertIndex[0]].u;
            imv[1].u = goo->orig_uvs[tri->vertIndex[1]].u;
            imv[2].u = goo->orig_uvs[tri->vertIndex[2]].u;
            imv[0].v = goo->orig_uvs[tri->vertIndex[0]].v;
            imv[1].v = goo->orig_uvs[tri->vertIndex[1]].v;
            imv[2].v = goo->orig_uvs[tri->vertIndex[2]].v;

            Im3DBufferPos += 3;
        }

        if (RwIm3DTransform(Im3DBuffer, Im3DBufferPos, NULL, 0x19) != NULL)
        {
            RwIm3DRenderPrimitive(rwPRIMTYPETRILIST);
            RwIm3DEnd();
        }
        xMemPopTemp(alpha);
    }

    return atomic;
}

void zFXUpdate(F32 dt)
{
    zFXGooUpdate(dt);
    update_poppers(dt);
    update_entrails(dt);
    xFXUpdate(dt);
}

void zFXGooFreeze(RpAtomic* atomic, const xVec3* center, xVec3* ref_parPosVec)
{
    S32 i;
    zFXGooInstance* goo = zFXGooInstances;
    S32 freezeGroup = -1;

    for (i = 0; i < 24; i++, goo++)
    {
        if (goo->state == zFXGooStateNormal && goo->atomic == atomic)
        {
            freezeGroup = goo->freezeGroup;
            break;
        }
    }

    if (freezeGroup == -1)
    {
        return;
    }

    xClimateSetSnow(1.0f);

    goo = zFXGooInstances;
    for (i = 0; i < 24; i++, goo++)
    {
        if (goo->freezeGroup == freezeGroup)
        {
            goo->state = zFXGooStateFreezing;
            goo->time = 0.0f;
            goo->timer = goo->time + goo->state_time[goo->state];

            goo->center = *center;
            goo->min = 3.0f;
            goo->max = 4.0f;
            goo->ref_parentPos = ref_parPosVec;

            xEnt* ent = zGooFindEnt(goo->atomic);
            if (ent != NULL)
            {
                goo->ref_parentPos = (xVec3*)&ent->model->Mat->pos;
                zEntEvent(ent, eEventGooFreezeStart);
                ent->model->Pipe.Flags &= ~0x400000;
                if (!xSndMgrIsPlaying(sGooSndGrpHandle))
                {
                    sGooSndHandle = xSndMgrPlay(sGooSndGrpHandle, 0, (xVec3*)&ent->model->Mat->pos,
                                                NULL, NULL, NULL, NULL);
                }
            }

            if (ref_parPosVec != NULL)
            {
                goo->pos_parentOnFreeze = *ref_parPosVec;
            }
            else
            {
                goo->pos_parentOnFreeze = g_O3;
            }
        }
    }
}

void zFXGooEventSetWarb(xEnt* ent, const F32* warb)
{
    S32 i;
    zFXGooInstance* goo = zFXGooInstances;
    for (i = 0; i < 24; i++, goo++)
    {
        if (goo->state == zFXGooStateInactive)
        {
            continue;
        }
        if (goo->atomic == ent->model->Data)
        {
            break;
        }
    }

    if (i == 24)
    {
        return;
    }

    memcpy(goo->warbc, warb, sizeof(goo->warbc));
    goo->w0 = goo->warbc[0];
    goo->w2 = goo->warbc[2];
}

void zFXGooEventSetFreezeDuration(xEnt* ent, F32 duration)
{
    S32 i;
    zFXGooInstance* goo = zFXGooInstances;
    S32 freezeGroup = -1;
    for (i = 0; i < 24; i++, goo++)
    {
        if (goo->state == zFXGooStateInactive)
        {
            continue;
        }
        if (goo->atomic == ent->model->Data)
        {
            freezeGroup = goo->freezeGroup;
            break;
        }
    }

    if (freezeGroup == -1)
    {
        return;
    }

    goo = zFXGooInstances;
    for (i = 0; i < 24; i++, goo++)
    {
        if (goo->freezeGroup == freezeGroup)
        {
            goo->state_time[zFXGooStateFrozen] = duration;
        }
    }
}

void zFXGooEventMelt(xEnt* ent)
{
    S32 i;
    zFXGooInstance* goo = zFXGooInstances;
    S32 freezeGroup = -1;
    for (i = 0; i < 24; i++, goo++)
    {
        if (goo->state != zFXGooStateFrozen)
        {
            continue;
        }
        if (goo->atomic == ent->model->Data)
        {
            freezeGroup = goo->freezeGroup;
            break;
        }
    }

    if (freezeGroup == -1)
    {
        return;
    }

    goo = zFXGooInstances;
    for (i = 0; i < 24; i++, goo++)
    {
        if (goo->freezeGroup == freezeGroup)
        {
            goo->timer = 0.0f;
        }
    }
}

xVec3 zfx_bubblehit_pos_rnd = { 0.25f, 0.25f, 0.25f };
xVec3 zfx_bubblehit_vel_rnd = { 6.0f, 6.0f, 6.0f };
F32 zfx_bubblehit_vel_scale = 1.0f;
xVec3 bubbletrail_pos_rnd = { 0.25f, 0.25f, 0.25f };
xVec3 bubbletrail_vel_rnd = { 0.25f, 0.25f, 0.25f };
F32 bubbletrail_vel_scale;

void zFX_SpawnBubbleHit(const xVec3* pos, U32 num)
{
    zFX_SpawnBubbleHit(pos, num, &zfx_bubblehit_pos_rnd, &zfx_bubblehit_vel_rnd,
                       zfx_bubblehit_vel_scale);
}

void zFX_SpawnBubbleHit(const xVec3* pos, U32 num, const xVec3* pos_rnd, const xVec3* vel_rnd,
                        F32 vel_scale)
{
    if (num == 0)
    {
        return;
    }

    if (pos_rnd == NULL)
    {
        pos_rnd = &zfx_bubblehit_pos_rnd;
    }
    if (vel_rnd == NULL)
    {
        vel_rnd = &zfx_bubblehit_vel_rnd;
    }

    xVec3* posbuf = (xVec3*)xMemPushTemp(num * 2 * sizeof(xVec3));
    if (posbuf == NULL)
    {
        return;
    }

    xVec3* velbuf = posbuf + num;
    xVec3* pp = posbuf;
    xVec3* vp = velbuf;
    for (S32 j = 0; j < (S32)num; j++, pp++, vp++)
    {
        *pp = *pos;
        pp->x += pos_rnd->x * (xurand() - 0.5f);
        pp->y += pos_rnd->y * (xurand() - 0.5f);
        pp->z += pos_rnd->z * (xurand() - 0.5f);

        vp->x = xurand() - 0.5f;
        vp->y = xurand() - 0.5f;
        vp->z = xurand() - 0.5f;
        xVec3NormalizeFast(vp, vp);
        *vp *= vel_scale;

        vp->x += vel_rnd->x * (xurand() - 0.5f);
        vp->y += vel_rnd->y * (xurand() - 0.5f);
        vp->z += vel_rnd->z * (xurand() - 0.5f);
    }

    zParPTankSpawnBubbles(posbuf, velbuf, num, 1.0f);
    xMemPopTemp(posbuf);
}

void zFX_SpawnBubbleTrail(const xVec3* pos, U32 num)
{
    zFX_SpawnBubbleTrail(pos, num, &bubbletrail_pos_rnd, &bubbletrail_vel_rnd);
}

void zFX_SpawnBubbleTrail(const xVec3* pos, U32 num, const xVec3* pos_rnd, const xVec3* vel_rnd)
{
    if (num < 1)
    {
        return;
    }

    if (pos_rnd == NULL)
    {
        pos_rnd = &bubbletrail_pos_rnd;
    }
    if (vel_rnd == NULL)
    {
        vel_rnd = &bubbletrail_vel_rnd;
    }

    xVec3* posbuf = (xVec3*)xMemPushTemp(num * 2 * sizeof(xVec3));
    if (posbuf == NULL)
    {
        return;
    }

    xVec3* velbuf = posbuf + num;
    xVec3* pp = posbuf;
    xVec3* vp = velbuf;
    for (S32 j = 0; j < (S32)num; j++, pp++, vp++)
    {
        *pp = *pos;
        pp->x += pos_rnd->x * (xurand() - 0.5f);
        pp->y += pos_rnd->y * (xurand() - 0.5f);
        pp->z += pos_rnd->z * (xurand() - 0.5f);

        vp->x = vel_rnd->x * (xurand() - 0.5f);
        vp->y = vel_rnd->y * (xurand() - 0.5f);
        vp->z = vel_rnd->z * (xurand() - 0.5f);
    }

    zParPTankSpawnBubbles(posbuf, velbuf, num, 1.0f);
    xMemPopTemp(posbuf);
}

void zFX_SpawnBubbleTrail(const xVec3* p1, const xVec3* p2, U32 num, const xVec3* pos_rnd,
                          const xVec3* vel_rnd)
{
    if (pos_rnd == NULL)
    {
        pos_rnd = &bubbletrail_pos_rnd;
    }
    if (vel_rnd == NULL)
    {
        vel_rnd = &bubbletrail_vel_rnd;
    }

    xVec3* posbuf = (xVec3*)xMemPushTemp(num * 2 * sizeof(xVec3));
    if (posbuf == NULL)
    {
        return;
    }

    xVec3* velbuf = posbuf + num;
    xVec3 offset = *p2 - *p1;
    xVec3* pp = posbuf;
    xVec3* vp = velbuf;
    for (S32 j = 0; j < (S32)num; j++, pp++, vp++)
    {
        *pp = *p1 + offset * xurand();

        pp->x += pos_rnd->x * (xurand() - 0.5f);
        pp->y += pos_rnd->y * (xurand() - 0.5f);
        pp->z += pos_rnd->z * (xurand() - 0.5f);
        vp->x = vel_rnd->x * (xurand() - 0.5f);
        vp->y = vel_rnd->y * (xurand() - 0.5f);
        vp->z = vel_rnd->z * (xurand() - 0.5f);
    }

    zParPTankSpawnBubbles(posbuf, velbuf, num, 1.0f);
    xMemPopTemp(posbuf);
}

// 't' is not in the PS2 DWARF, but its line table has the random factor on a line of its own
// and both lerps reuse the single xurand() result.
void zFX_SpawnBubbleTrail(const xVec3* p1, const xVec3* p2, const xVec3* vel1, const xVec3* vel2,
                          U32 num, const xVec3* pos_rnd, const xVec3* vel_rnd, F32 scale)
{
    if (pos_rnd == NULL)
    {
        pos_rnd = &bubbletrail_pos_rnd;
    }
    if (vel_rnd == NULL)
    {
        vel_rnd = &bubbletrail_vel_rnd;
    }

    xVec3* posbuf = (xVec3*)xMemPushTemp(num * 2 * sizeof(xVec3));
    if (posbuf == NULL)
    {
        return;
    }

    xVec3* velbuf = posbuf + num;
    xVec3 offset = *p2 - *p1;
    xVec3 vel_offset = *vel2 - *vel1;
    xVec3* pp = posbuf;
    xVec3* vp = velbuf;
    for (S32 j = 0; j < (S32)num; j++, pp++, vp++)
    {
        F32 t = xurand();
        *pp = *p1 + offset * t;
        *vp = *vel1 + vel_offset * t;

        pp->x += pos_rnd->x * (xurand() - 0.5f);
        pp->y += pos_rnd->y * (xurand() - 0.5f);
        pp->z += pos_rnd->z * (xurand() - 0.5f);
        vp->x += vel_rnd->x * (xurand() - 0.5f);
        vp->y += vel_rnd->y * (xurand() - 0.5f);
        vp->z += vel_rnd->z * (xurand() - 0.5f);
    }

    zParPTankSpawnBubbles(posbuf, velbuf, num, scale);
    xMemPopTemp(posbuf);
}

void zFX_SpawnBubbleMenuTrail(const xVec3* pos, U32 num, const xVec3* pos_rnd, const xVec3* vel_rnd)
{
    if (pos_rnd == NULL)
    {
        pos_rnd = &bubbletrail_pos_rnd;
    }
    if (vel_rnd == NULL)
    {
        vel_rnd = &bubbletrail_vel_rnd;
    }

    xVec3* posbuf = (xVec3*)xMemPushTemp(num * 2 * sizeof(xVec3));
    if (posbuf == NULL)
    {
        return;
    }

    xVec3* velbuf = posbuf + num;
    xVec3* pp = posbuf;
    xVec3* vp = velbuf;
    for (S32 j = 0; j < (S32)num; j++, pp++, vp++)
    {
        *pp = *pos;
        pp->x += pos_rnd->x * (xurand() - 0.5f);
        pp->y += pos_rnd->y * (xurand() - 0.5f);
        pp->z += pos_rnd->z * (xurand() - 0.5f);
        vp->x = vel_rnd->x * (xurand() - 0.5f);
        vp->y = vel_rnd->y * (xurand() - 0.5f);
        vp->z = vel_rnd->z * (xurand() - 0.5f);
    }

    zParPTankSpawnMenuBubbles(posbuf, velbuf, num);
    xMemPopTemp(posbuf);
}

static const xVec3 bubblewall_scale = { 2.4f, 2.4f, 2.4f };
static const xVec3 bubblewall_velscale = { 1.0f, 0.5f, 0.5f };

inline void zFX_SpawnBubbleWall()
{
    RwCamera* camera = RwCameraGetCurrentCamera();
    if (camera == NULL)
    {
        return;
    }

    RwMatrix* mat = RwFrameGetMatrix(RwCameraGetFrame(camera));
    xVec3* posbuf = (xVec3*)xMemPushTemp(100 * 2 * sizeof(xVec3));
    if (posbuf == NULL)
    {
        return;
    }

    xVec3* velbuf = posbuf + 100;
    xVec3* pp = posbuf;
    xVec3* vp = velbuf;
    for (U32 i = 0; i < 50; i++, pp++, vp++)
    {
        pp->x = (xurand() - 0.5f + mat->pos.x) + bubblewall_scale.x * (xurand() - 0.5f);
        pp->y = (xurand() - 0.5f + mat->pos.y) + bubblewall_scale.y * (xurand() - 0.5f);
        pp->z = (xurand() - 0.5f + mat->pos.z) + bubblewall_scale.z * (xurand() - 0.5f);

        xVec3 scaled;
        xVec3ScaleC(&scaled, (xVec3*)&mat->at, 1.2f, 1.2f, 1.2f);
        xVec3Add(pp, pp, &scaled);

        vp->x = bubblewall_velscale.x * (xurand() - 0.5f);
        vp->y = bubblewall_velscale.y * (xurand() - 0.5f);
        vp->z = bubblewall_velscale.z * (xurand() - 0.5f);
    }

    zParPTankSpawnBubbles(posbuf, velbuf, 50, 1.0f);
    xMemPopTemp(posbuf);
}

void zFX_SpawnBubbleSlam(const xVec3* pos, U32 num, F32 rang, F32 bvel, F32 rvel)
{
    xVec3* posbuf = (xVec3*)xMemPushTemp(num * 2 * sizeof(xVec3));
    if (posbuf == NULL)
    {
        return;
    }

    xVec3* velbuf = posbuf + num;
    xVec3* pp = posbuf;
    xVec3* vp = velbuf;
    for (U32 j = 0; j < num; j++, pp++, vp++)
    {
        *pp = *pos;
        pp->y += 0.2f;

        F32 ang = (2.0f * PI * j) / num;
        ang += rang * (xurand() - 0.5f);
        vp->x = bvel * icos(ang);
        vp->y = 0.0f;
        vp->z = bvel * isin(ang);

        vp->x += rvel * (xurand() - 0.5f);
        vp->y += 0.25f * rvel * (xurand() - 0.5f);
        vp->z += rvel * (xurand() - 0.5f);
    }

    zParPTankSpawnBubbles(posbuf, velbuf, num, 1.0f);
    xMemPopTemp(posbuf);
}

void zFX_SpawnBubbleRing(const xVec3* pos, U32 num, F32 rang, F32 bvel, F32 rvel, xVec3* norm)
{
    xVec3* posbuf = (xVec3*)xMemPushTemp(num * 2 * sizeof(xVec3));
    if (posbuf == NULL)
    {
        return;
    }

    xVec3* velbuf = posbuf + num;
    xVec3* pp = posbuf;
    xVec3* vp = velbuf;
    for (U32 j = 0; j < num; j++, pp++, vp++)
    {
        *pp = *pos;

        F32 ang = (2.0f * PI * j) / num;
        ang += rang * (xurand() - 0.5f);
        vp->x = bvel * icos(ang);
        vp->y = 0.0f;
        vp->z = bvel * isin(ang);

        vp->x += rvel * (xurand() - 0.5f);
        vp->y += 0.25f * rvel * (xurand() - 0.5f);
        vp->z += rvel * (xurand() - 0.5f);

        xMat3x3 upMat;
        xMat3x3LookVec(&upMat, &g_Y3);
        xMat3x3Tolocal(vp, &upMat, vp);

        xMat3x3 normMat;
        xMat3x3LookVec(&normMat, norm);
        xMat3x3RMulVec(vp, &normMat, vp);
    }

    zParPTankSpawnBubbles(posbuf, velbuf, num, 1.0f);
    xMemPopTemp(posbuf);
}

// 'r' is not in the PS2 DWARF, but retail computes the square root once for both terms.
void zFX_SpawnBubbleBlast(const xVec3* pos, U32 num, F32 radius, F32 blast_vel, F32 rand_vel)
{
    // Not in the DWARF order (buffer, itl, itv, end): declaring end last changes the generated code
    // (GC/2.6 numbers named locals in reverse declaration order).
    xVec3* end;
    xVec3* buffer = (xVec3*)xMemPushTemp(num * 2 * sizeof(xVec3));
    if (buffer == NULL)
    {
        return;
    }

    end = buffer + num;
    xVec3* itl = buffer;
    xVec3* itv = end;
    for (; itl != end; itl++, itv++)
    {
        F32 ang = 2.0f * PI * xurand();
        F32 uz = 2.0f * xurand() - 1.0f;
        F32 r = xsqrt(1.0f - uz * uz);
        itv->assign(r * icos(ang), r * isin(ang), uz);
        *itl = *pos + *itv * radius;
        *itv *= blast_vel;

        xVec3 rvel = { xurand() - 0.5f, xurand() - 0.5f, xurand() - 0.5f };
        *itv += rvel * rand_vel;
    }

    zParPTankSpawnBubbles(buffer, end, num, 1.0f);
    xMemPopTemp(buffer);
}

namespace
{
    const U16 popper_weight_sizes[8] = { 256, 256, 512, 512, 1024, 1024, 2048, 4096 };
    popper_data poppers[8];
    popper_data instantPopper;
    entrail_type entrail_types[7] = {
        { "hovering_platform_wheel_bind",
          0,
          70.0f,
          25.0f,
          { 0.0f, 0.5f, 0.0f },
          { 0.4f, 0.4f, 0.4f },
          { 0.0f, -3.0f, 0.0f },
          { 1.5f, 0.0f, 1.5f } },
        { "plat_support_double_bind",
          0,
          70.0f,
          25.0f,
          { 0.0f, -2.0f, 0.0f },
          { 0.4f, 0.4f, 0.4f },
          { 0.0f, -3.0f, 0.0f },
          { 1.5f, 0.0f, 1.5f } },
        { "goofy_goober_ride_bind",
          31,
          75.0f,
          65.0f,
          { 0.0f, 0.0f, 0.0f },
          { 0.4f, 0.4f, 0.4f },
          { 2.5f, 0.0f, 0.0f },
          { 2.0f, 0.0f, 2.0f } },
        { "goofy_goober_ride_bind",
          8,
          75.0f,
          75.0f,
          { 0.0f, 0.0f, 0.0f },
          { 0.5f, 1.2f, -0.75f },
          { 0.0f, 1.0f, 1.0f },
          { 0.0f, 0.0f, 0.5f } },
        { "goofy_goober_ride_bind",
          17,
          40.0f,
          60.0f,
          { 0.0f, 0.5f, 0.0f },
          { 1.0f, 0.0f, 1.0f },
          { 3.0f, -1.0f, -2.0f },
          { 0.0f, 0.0f, 1.5f } },
        { "plankton_bind",
          35,
          25.0f,
          35.0f,
          { 0.0f, 0.0f, 0.0f },
          { 0.0f, 0.0f, 0.0f },
          { 0.0f, -0.5f, 0.0f },
          { 0.0f, -0.25f, 0.0f } },
        { "plankton_bind",
          36,
          25.0f,
          40.0f,
          { 0.0f, 0.0f, 0.0f },
          { 0.0f, 0.0f, 0.0f },
          { 0.0f, -1.0f, 0.0f },
          { 0.0f, -0.25f, 0.0f } },
    };
    entrail_data* entrails;
    U32 entrails_size;

    inline void entrail_data::reset()
    {
        flags = 0;
        emitted = 0.0f;
    }

    inline void emit_popper_bubbles_immediate(popper_data& popper)
    {
        F32 count = popper.rate;
        count = count * (0.5f * popper.area);
        if ((S32)count > 0)
        {
            emit_popper_bubbles(popper, (S32)count, 1.0f, 0.0f);
        }
    }

    inline popper_data* find_popper(xEnt* ent)
    {
        popper_data* it = poppers;
        popper_data* end = poppers + (sizeof(poppers) / sizeof(poppers[0]));
        for (; it != end; ++it)
        {
            if (it->ent == ent)
            {
                return it;
            }
        }
        return NULL;
    }

    inline popper_data* find_free_popper(S32 facesNeeded)
    {
        popper_data* found = NULL;
        popper_data* end = poppers + (sizeof(poppers) / sizeof(poppers[0]));
        for (popper_data* it = poppers; it != end; ++it)
        {
            if (it->state == STATE_NONE && it->max_faces >= facesNeeded)
            {
                if (it->ent == NULL)
                {
                    return it;
                }
                found = it;
            }
        }
        return found;
    }

    inline S32 count_faces(xModelInstance* model)
    {
        S32 total = 0;
        for (; model != NULL; model = model->Next)
        {
            total += model->Data->geometry->numTriangles;
        }
        return total;
    }

    inline F32 get_triangle_area(const xVec3& a, const xVec3& b, const xVec3& c)
    {
        xVec3 ab = { b.x - a.x, b.y - a.y, b.z - a.z };
        xVec3 ac = { c.x - a.x, c.y - a.y, c.z - a.z };
        xVec3 abXac = { ab.y * ac.z - ac.y * ab.z, ab.z * ac.x - ac.z * ab.x,
                        ab.x * ac.y - ac.x * ab.y };

        return 0.5f * abXac.length();
    }

    inline bool model_is_preinstanced(RpAtomic* atomic)
    {
        RpGeometry* geom = atomic->geometry;
        if (geom == NULL)
        {
            return true;
        }

        return !(geom->morphTarget != NULL && geom->morphTarget->verts != NULL);
    }

    inline bool validate_popper(const xEnt& ent)
    {
        if (ent.model == NULL || ent.model->Data == NULL)
        {
            return false;
        }

        if (model_is_preinstanced(ent.model->Data))
        {
            return false;
        }

        xVec3 scale = ent.model->Scale;
        for (xModelInstance* model = ent.model->Next; model != NULL; model = model->Next)
        {
            if (!(xabs(scale.x - model->Scale.x) <= 0.1f) ||
                !(xabs(scale.y - model->Scale.y) <= 0.1f) ||
                !(xabs(scale.z - model->Scale.z) <= 0.1f))
            {
                return false;
            }
        }

        return true;
    }

    inline popper_data* grab_popper(xEnt& ent)
    {
        popper_data* popper = find_popper(&ent);
        if (popper != NULL)
        {
            if (popper->faces == count_faces(ent.model))
            {
                return popper;
            }
        }
        else
        {
            popper = find_free_popper(count_faces(ent.model));
            if (popper == NULL)
            {
                return NULL;
            }
            popper->ent = &ent;
        }

        if (!setup_popper_emitter(*popper))
        {
            popper->ent = NULL;
            return NULL;
        }

        return popper;
    }

    inline void set_popper_alpha(popper_data& popper, F32 alpha)
    {
        xEntShow(popper.ent);
        xModelInstance* model = popper.ent->model;
        model->Alpha = alpha;
        model->Flags |= 0x4000;
        model->Pipe.Flags = (model->Pipe.Flags & ~0x30000) | 0x20000;
    }

    inline void destroy_popper(popper_data& popper)
    {
        switch (popper.state)
        {
        case STATE_CONSTANT:
            break;
        case STATE_ON:
            xModelSetScale(popper.ent->model, popper.model_scale);
            break;
        case STATE_OFF:
        {
            xEntHide(popper.ent);
            xModelInstance* model = popper.ent->model;
            model->Alpha = 1.0f;
            model->Flags &= (U16)~0x4000;
            model->Pipe.Flags = (model->Pipe.Flags & ~0x30000) | popper.pipe_flags;
            break;
        }
        }

        popper.state = STATE_NONE;
    }

    U8 setup_popper_emitter(popper_data& popper)
    {
        popper.faces = count_faces(popper.ent->model);
        if (popper.faces <= 0)
        {
            return FALSE;
        }
        if (popper.faces > popper.max_faces)
        {
            return FALSE;
        }

        popper.area = 0.0f;
        F32* weight = popper.weight;
        popper.atomic_size = 0;

        for (xModelInstance* model = popper.ent->model; model != NULL; model = model->Next)
        {
            if (popper.atomic_size >= 4)
            {
                break;
            }

            popper.atomic[popper.atomic_size] = model->Data;

            RpGeometry* geom = model->Data->geometry;
            xVec3* vert = (xVec3*)geom->morphTarget->verts;
            RpTriangle* tri = geom->triangles;
            F32* end = weight + geom->numTriangles;
            for (; weight != end; weight++)
            {
                xVec3& a = vert[tri->vertIndex[0]];
                xVec3& b = vert[tri->vertIndex[1]];
                xVec3& c = vert[tri->vertIndex[2]];
                popper.area += get_triangle_area(a, b, c);
                tri++;
                *weight = popper.area;
            }

            popper.atomic_size++;
        }

        if (popper.atomic_size == 0)
        {
            return FALSE;
        }

        return TRUE;
    }

    void SkinXformVertAndNormal(xVec3* vert, xVec3* norm, const xVec3* in_vert,
                                const xVec3* in_norm, const xMat4x3* mat, const xMat4x3* skinmat,
                                const F32* wt, const U32* idx, const U16* vert_index, U32 count)
    {
        U32 catMatFlags[2] = { 0, 0 };
        xMat4x3* catmat = (xMat4x3*)xMemScratchPush(256 * sizeof(xMat4x3), 0);
        const xMat4x3* rootmat = mat;

        for (; count != 0; count--)
        {
            U16 i = *vert_index;
            const xVec3* cur_in_vert = &in_vert[i];
            const xVec3* cur_in_norm = &in_norm[i];
            U32 cur_idx = idx[i];
            const F32* cur_wt = &wt[i * 4];

            for (U32 i = 0; i < 4; i++)
            {
                // Retail: midx >> 5 can exceed catMatFlags' two words for bone indices >= 64.
                U32 midx = (cur_idx >> (i * 8)) & 0xff;
                if (!(catMatFlags[midx >> 5] & (1 << (midx & 0x1f))))
                {
                    const xMat4x3* pMatrix = rootmat + 1;
                    xMat4x3Mul(&catmat[midx], &skinmat[midx], &pMatrix[midx]);
                    catMatFlags[midx >> 5] |= 1 << (midx & 0x1f);
                }
            }

            const F32* fwt = cur_wt;
            U32 wtidx = cur_idx;
            U32 maxwt = 4;
            xVec3 accumV = { 0.0f, 0.0f, 0.0f };
            while (*fwt && maxwt != 0)
            {
                xVec3 v;
                xMat4x3Toworld(&v, &catmat[wtidx & 0xff], cur_in_vert);
                v *= *fwt;
                accumV += v;
                wtidx >>= 8;
                fwt++;
                maxwt--;
            }
            xMat4x3Toworld(vert, rootmat, &accumV);

            accumV = 0.0f;
            fwt = cur_wt;
            wtidx = cur_idx;
            maxwt = 4;
            while (*fwt && maxwt != 0)
            {
                xVec3 v;
                // not in the DWARF: retail colours the bone matrix like a named local; written
                // inline, the address permutes the loop registers.
                const xMat4x3* bonemat = &catmat[wtidx & 0xff];
                xMat3x3RMulVec(&v, bonemat, cur_in_norm);
                v *= *fwt;
                accumV += v;
                wtidx >>= 8;
                fwt++;
                maxwt--;
            }
            xMat3x3RMulVec(norm, rootmat, &accumV);

            vert++;
            norm++;
            vert_index++;
        }

        xMemScratchPop(catmat);
    }

    void eval_tri(xVec3* vert, xVec3* norm, const xMat4x3* mat, const RpGeometry* geom,
                  const RpTriangle* tri)
    {
        RpSkin* skin = RpSkinGeometryGetSkin((RpGeometry*)geom);
        RpMorphTarget* mt = geom->morphTarget;
        const xVec3* in_vert = (const xVec3*)mt->verts;
        const xVec3* in_norm = (const xVec3*)mt->normals;

        if (skin != NULL)
        {
            const xMat4x3* skinmat = (const xMat4x3*)RpSkinGetSkinToBoneMatrices(skin);
            const F32* vert_bone_weight = (const F32*)RpSkinGetVertexBoneWeights(skin);
            const U32* vert_bone_index = RpSkinGetVertexBoneIndices(skin);

            SkinXformVertAndNormal(vert, norm, in_vert, in_norm, mat, skinmat, vert_bone_weight,
                                   vert_bone_index, tri->vertIndex, 3);
            return;
        }

        xMat4x3Toworld(&vert[0], mat, &in_vert[tri->vertIndex[0]]);
        xMat4x3Toworld(&vert[1], mat, &in_vert[tri->vertIndex[1]]);
        xMat4x3Toworld(&vert[2], mat, &in_vert[tri->vertIndex[2]]);

        xMat3x3RMulVec(&norm[0], mat, &in_norm[tri->vertIndex[0]]);
        norm[0].up_normalize();
        xMat3x3RMulVec(&norm[1], mat, &in_norm[tri->vertIndex[1]]);
        norm[1].up_normalize();
        xMat3x3RMulVec(&norm[2], mat, &in_norm[tri->vertIndex[2]]);
        norm[2].up_normalize();
    }

    inline void random_point_on_triangle(xVec3& loc, xVec3& norm, const xVec3* v, const xVec3* n)
    {
        F32 s = xurand();
        F32 t = xurand();
        if (s + t > 1.0f)
        {
            s = 1.0f - s;
            t = 1.0f - t;
        }

        loc.x = v[0].x + s * (v[1].x - v[0].x) + t * (v[2].x - v[0].x);
        loc.y = v[0].y + s * (v[1].y - v[0].y) + t * (v[2].y - v[0].y);
        loc.z = v[0].z + s * (v[1].z - v[0].z) + t * (v[2].z - v[0].z);
        norm.x = n[0].x + s * (n[1].x - n[0].x) + t * (n[2].x - n[0].x);
        norm.y = n[0].y + s * (n[1].y - n[0].y) + t * (n[2].y - n[0].y);
        norm.z = n[0].z + s * (n[1].z - n[0].z) + t * (n[2].z - n[0].z);
        norm.up_normalize();
    }

    void random_surface_point(xVec3& loc, xVec3& norm, const popper_data& popper)
    {
        const xMat4x3* mat = (const xMat4x3*)popper.ent->model->Mat;
        S32 which = popper.find_weight(popper.area * xurand());

        RpGeometry* geom = NULL;
        RpAtomic* const* atomic = popper.atomic;
        RpAtomic* const* end = atomic + popper.atomic_size;
        for (; atomic != end; atomic++)
        {
            geom = (*atomic)->geometry;
            S32 size = geom->numTriangles;
            if (which < size)
            {
                break;
            }
            which -= size;
        }

        xVec3 v[3];
        xVec3 n[3];
        eval_tri(v, n, mat, geom, &geom->triangles[which]);
        random_point_on_triangle(loc, norm, v, n);
    }

    void emit_popper_bubbles(popper_data& popper, S32 emit, F32 scale, F32 vel_add)
    {
        // Retail bug: this should be ||; with && a NULL ent dereferences ent->model.
        if (popper.ent == NULL && popper.ent->model == NULL)
        {
            return;
        }

        S32 max_emit = zParPTankBubblesAvailable();
        if (max_emit <= 0)
        {
            return;
        }

        if (emit > max_emit)
        {
            emit = max_emit;
        }

        // Not in the DWARF order (buffer, loc, end_loc, vel): declaring end_loc after loc changes
        // the generated code, as in zFX_SpawnBubbleBlast.
        xVec3* end_loc;
        xVec3* buffer = (xVec3*)xMemPushTemp(emit * sizeof(xVec3) * 2);
        if (buffer == NULL)
        {
            return;
        }

        end_loc = buffer + emit;
        xVec3* loc = buffer;
        xVec3* vel = end_loc;
        xModelInstance* model = popper.ent->model;
        xMat3x3 oldmat = {};

        if (model->Scale.x != 0.0f)
        {
            xMat3x3& mat = *(xMat3x3*)model->Mat;
            oldmat = mat;
            mat.right *= model->Scale.x;
            mat.up *= model->Scale.y;
            mat.at *= model->Scale.z;
        }

        F32 svel = popper.vel * scale + vel_add;
        F32 rloc = popper.rloc * scale;
        F32 rvel = popper.rvel * scale;

        for (; loc != end_loc; loc++, vel++)
        {
            random_surface_point(*loc, *vel, popper);
            *vel *= svel;

            if (popper.rloc != 0.0f)
            {
                loc->x += rloc * (xurand() - 0.5f);
                loc->y += rloc * (xurand() - 0.5f);
                loc->z += rloc * (xurand() - 0.5f);
            }

            if (popper.rvel != 0.0f)
            {
                vel->x += rvel * (xurand() - 0.5f);
                vel->y += rvel * (xurand() - 0.5f);
                vel->z += rvel * (xurand() - 0.5f);
            }
        }

        if (model->Scale.x != 0.0f)
        {
            *(xMat3x3*)model->Mat = oldmat;
        }

        zParPTankSpawnBubbles(buffer, end_loc, emit, scale);
        xMemPopTemp(buffer);
    }

    void update_popper(popper_data& popper, F32 dt)
    {
        F32 rate = popper.rate;
        F32 scale;
        F32 vel_add = 0.0f;

        if (popper.state == STATE_CONSTANT)
        {
            scale = 1.0f;
        }
        else
        {
            popper.time += dt;
            if (popper.time > popper.end_time)
            {
                dt = popper.time - popper.end_time;
                popper.time = popper.end_time;
            }

            F32 area = popper.area;
            F32 s = popper.time / popper.end_time;

            switch (popper.state)
            {
            case STATE_ON:
            {
                F32 ss;
                xSCurve(s, vel_add, ss, s);
                area *= s;
                vel_add *= popper.radius;

                xVec3 model_scale;
                if (popper.model_scale.x == 0.0f)
                {
                    model_scale = s;
                }
                else
                {
                    model_scale = popper.model_scale * s;
                }

                xModelSetScale(popper.ent->model, model_scale);
                break;
            }
            case STATE_OFF:
                set_popper_alpha(popper, xSCurve(1.0f - s));
                break;
            }

            if (area <= 4.0f)
            {
                rate *= area;
                scale = 1.0f;
            }
            else
            {
                rate *= 4.0f;
                scale = 0.5f * xsqrt(area);
            }
        }

        popper.emitted += rate * dt;
        if (popper.emitted >= 1.0f)
        {
            S32 emit = popper.emitted;
            popper.emitted -= emit;
            emit_popper_bubbles(popper, emit, scale, vel_add);
        }
    }
} // namespace

void zFXPopOn(xEnt& ent, F32 rate, F32 time)
{
    if (!validate_popper(ent))
    {
        return;
    }

    popper_data* popper = grab_popper(ent);
    if (popper == NULL)
    {
        return;
    }

    if (rate == 0.0f)
    {
        rate = 200.0f;
    }
    if (time == 0.0f)
    {
        time = 0.25f;
    }
    else if (time < 0.0f)
    {
        time = 0.0f;
    }

    if (time <= 0.0f)
    {
        popper->state = STATE_ON;
        popper->rate = rate;
        emit_popper_bubbles_immediate(*popper);
        popper->state = STATE_NONE;
        xEntShow(&ent);
        return;
    }

    xSphere o;
    xBoundGetSphere(o, ent.bound);
    popper->radius = o.r;

    popper->state = STATE_ON;
    popper->time = 0.0f;
    popper->end_time = time;
    popper->rate = rate;
    popper->vel = 0.0f;
    popper->rloc = 0.5f;
    popper->rvel = 1.0f;
    popper->emitted = 0.0f;
    popper->model_scale = ent.model->Scale;

    xVec3 tiny = { 0.001f, 0.001f, 0.001f };
    xModelSetScale(popper->ent->model, tiny);
}

void zFXPopOff(xEnt& ent, F32 rate, F32 time)
{
    if (!validate_popper(ent))
    {
        return;
    }

    popper_data* popper = grab_popper(ent);
    if (popper == NULL)
    {
        return;
    }

    if (rate == 0.0f)
    {
        rate = 200.0f;
    }
    if (time == 0.0f)
    {
        time = 0.25f;
    }
    else if (time < 0.0f)
    {
        time = 0.0f;
    }

    if (time <= 0.0f)
    {
        // Note: unlike zFXPopOn, the passed rate is not stored before the immediate burst.
        popper->state = STATE_OFF;
        emit_popper_bubbles_immediate(*popper);
        popper->state = STATE_NONE;
        xEntHide(&ent);
        return;
    }

    xSphere o;
    xBoundGetSphere(o, ent.bound);
    popper->radius = o.r;

    popper->state = STATE_OFF;
    popper->time = 0.0f;
    popper->end_time = time;
    popper->rate = rate;
    popper->vel = 0.0f;
    popper->rloc = 0.0f;
    popper->rvel = 0.25f;
    popper->emitted = 0.0f;
    popper->pipe_flags = ent.model->Pipe.Flags & 0x30000;

    set_popper_alpha(*popper, 0.0f);
}

void zFXBubbleSpawnStart(xEnt& ent, F32 rate)
{
    if (!validate_popper(ent))
    {
        return;
    }

    popper_data* popper = grab_popper(ent);
    if (popper == NULL)
    {
        return;
    }

    if (rate == 0.0f)
    {
        rate = 200.0f;
    }

    xSphere o;
    xBoundGetSphere(o, ent.bound);
    popper->radius = o.r;

    popper->state = STATE_CONSTANT;
    popper->rate = rate;
    popper->time = 0.0f;
    popper->end_time = 0.0f;
    popper->vel = 0.0f;
    popper->rloc = 0.5f;
    popper->rvel = 1.0f;
    popper->emitted = 0.0f;
    popper->model_scale = 1.0f;
}

void zFXBubbleSpawnStop(xEnt& ent)
{
    popper_data* pPopper = find_popper(&ent);
    if (pPopper != NULL)
    {
        destroy_popper(*pPopper);
    }
}

void zFXPopOnOffAtomic(xModelInstance* model, F32 rate)
{
    xModelInstance* nextModel;
    xEnt dummyEnt;

    if (model == NULL)
    {
        return;
    }

    nextModel = model->Next;
    model->Next = NULL;
    dummyEnt.model = model;
    instantPopper.ent = &dummyEnt;
    setup_popper_emitter(instantPopper);
    instantPopper.state = STATE_ON;
    instantPopper.rate = rate;

    emit_popper_bubbles_immediate(instantPopper);

    model->Next = nextModel;
}

namespace
{
    // inline: retail emits it right after its only caller update_entrails, and its inline cost
    // keeps update_entrails out of line in zFXUpdate.
    inline void entrail_data::update(F32 dt)
    {
        if (ent == NULL)
        {
            return;
        }

        if (!xEntIsVisible(ent))
        {
            flags &= ~1;
            return;
        }

        entrail_type& t = entrail_types[type];

        const xVec3& campos = globals.cam->mat.pos;
        xMat4x3* frame = xEntGetFrame(ent);
        const xVec3 dist = { campos.x - frame->pos.x, campos.y - frame->pos.y,
                             campos.z - frame->pos.z };

        if (dist.length2() > t.cull_dist * t.cull_dist)
        {
            flags &= ~1;
            return;
        }

        if (t.bone > ent->model->BoneCount)
        {
            return;
        }

        if (!(flags & 1))
        {
            emitted = 0.0f;
        }

        emitted += t.rate * dt;

        S32 emit = emitted;
        if (emit <= 0)
        {
            return;
        }
        emitted -= emit;

        xMat4x3 mat;
        xModelGetBoneMat(mat, *ent->model, t.bone);

        xVec3 new_loc;
        xVec3 new_vel;
        xMat4x3Toworld(&new_loc, &mat, &t.offset);
        xMat3x3RMulVec(&new_vel, &mat, &t.vel);

        if (flags & 1)
        {
            zFX_SpawnBubbleTrail(&loc, &new_loc, &vel, &new_vel, emit, &t.offset_rand, &t.vel_rand,
                                 1.0f);
        }
        else
        {
            zFX_SpawnBubbleTrail(&new_loc, &new_loc, &new_vel, &new_vel, emit, &t.offset_rand,
                                 &t.vel_rand, 1.0f);
        }

        loc = new_loc;
        vel = new_vel;
        flags |= 1;
    }
} // namespace

static void update_entrails(F32 dt)
{
    entrail_data* it;
    entrail_data* end = entrails + entrails_size;
    for (it = entrails; it != end; it++)
    {
        it->update(dt);
    }
}

static void setup_entrails(zScene& s)
{
    U32 hash[7];
    U32 hash_dff[7];
    U32 hash_minf[7];

    for (U32 i = 0; i < 7; i++)
    {
        hash[i] = xStrHash(entrail_types[i].model_name);
        hash_dff[i] = xStrHashCat(hash[i], ".dff");
        hash_minf[i] = xStrHashCat(hash[i], ".minf");
    }

    entrails_size = 0;

    {
        xBase** it = s.base;
        xBase** end = it + s.num_base;
        for (; it != end; it++)
        {
            xEnt* ent = (xEnt*)*it;
            if (ent == NULL || !(ent->baseFlags & 0x20))
            {
                continue;
            }

            U32 model = ent->asset->modelInfoID;
            for (U32 i = 0; i < 7; i++)
            {
                if (model == hash[i] || model == hash_dff[i] || model == hash_minf[i])
                {
                    entrails_size++;
                }
            }
        }
    }

    if (entrails_size == 0)
    {
        return;
    }

    entrails = (entrail_data*)xMemAllocSize(entrails_size * sizeof(entrail_data));

    U32 index = 0;
    xBase** it = s.base;
    xBase** end = it + s.num_base;
    for (; it != end; it++)
    {
        xEnt* ent = (xEnt*)*it;
        if (!(ent->baseFlags & 0x20))
        {
            continue;
        }

        U32 model = ent->asset->modelInfoID;
        for (U32 i = 0; i < 7; i++)
        {
            if (model == hash[i] || model == hash_dff[i] || model == hash_minf[i])
            {
                entrails[index].reset();
                entrails[index].type = i;
                entrails[index].ent = ent;
                index++;
            }
        }
    }
}

void zFX_SpawnBubblesOnSphere(S32 numBubbles, xSphere* genSphere, F32 velocity)
{
    xVec3* posbuf = (xVec3*)xMemPushTemp(numBubbles * 2 * sizeof(xVec3));
    if (posbuf == NULL)
    {
        return;
    }

    xVec3* velbuf = posbuf + numBubbles;
    xVec3* pp = posbuf;
    xVec3* vp = velbuf;
    F32 velocityScale = velocity / genSphere->r;
    for (S32 j = 0; j < numBubbles; j++, pp++, vp++)
    {
        xSphereRandomPointOnSphere(genSphere, *vp);
        *pp = *vp;
        xVec3SubFrom(vp, &genSphere->center);
        xVec3SMulBy(vp, velocityScale);
    }

    zParPTankSpawnBubbles(posbuf, velbuf, numBubbles, 1.0f);
    xMemPopTemp(posbuf);
}
