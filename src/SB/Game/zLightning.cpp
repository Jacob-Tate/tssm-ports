#include "zLightning.h"

#include "xFXHighDynamicRange.h"
#include "xMath.h"
#include "xMath3.h"
#include "xSnd.h"
#include "xstransvc.h"
#include "zCam.h"
#include "zEntPlayer.h"
#include "zGlobals.h"
#include "zScene.h"

#define NUM_LIGHTNING 80

extern tagiRenderArrays gRenderArr;

zLightning* sLightning;
static F32 sLightningComplexityScale = 1.0f;
RwRaster* sLightningRaster;
RwRaster* sLightningRasterBranch;
RwTexture* sLightningParticleRaster;
F32 LIGHTNING_FADE_DIST_SQR;
F32 LIGHTNING_CULL_DIST_SQR;
static xFuncPiece sLFuncX[10];
static xFuncPiece sLFuncY[10];
static xFuncPiece sLFuncZ[10];
static xVec3 sLFuncVal[10];
static xVec3 sLFuncSlope[10][2];
static F32 sLFuncEnd[10];
static F32 sLFuncJerkFreq = 20.0f;
static F32 sLFuncJerkTime;
static F32 sLFuncShift = 6.0f;
static F32 sLFuncMinScale = 0.1f;
static F32 sLFuncMaxScale = 0.4f;
static F32 sLFuncScalePerLength = 0.1f;
static F32 sLFuncMinSpan = 1.3f;
static F32 sLFuncSpanPerLength = 9.5f;
static F32 sLFuncSlopeRange = 2.0f;
static F32 sLFuncUVSpeed = 1.0f;
static F32 sLFuncUVOffset;
zLightningFuncPiece sLFuncNew[11];
_tagLightningAdd gLightningTweakAddInfo;
static xVec3 sTweakStart;
static xVec3 sTweakEnd;
static tweak_callback sLightningStartCB;
static tweak_callback sLightningChangeCB;
CollisionInfo collisionInfo;
xVec3* posArray;
static xVec3 sPoint[5];

static inline zLightning* lightningStart(zLightningAsset* asset);
static F32 sSize[5];

void zLightningWidget::Init(xBase& data, xDynAsset& asset, size_t)
{
    xBaseInit(&data, &asset);
    ((zLightningWidget&)data).asset = (zLightningAsset*)&asset;

    if (data.linkCount != 0)
    {
        data.link = (xLinkAsset*)((zLightningAsset*)&asset + 1);
    }
    else
    {
        data.link = NULL;
    }

    data.eventFunc = EventCB;
}

void zLightningWidget::EventCB(xBase*, xBase* to, U32 toEvent, const F32*, xBase*, U32)
{
    zLightningWidget& lightningWidget = *(zLightningWidget*)to;

    switch (toEvent)
    {
    case eEventOn:
    case eEventLightningStart:
        if (lightningWidget.l != NULL)
        {
            zLightningKill(lightningWidget.l);
            lightningWidget.l = NULL;
        }

        lightningWidget.l = lightningStart(lightningWidget.asset);
        lightningWidget.l->widget = &lightningWidget;
        break;
    case eEventOff:
    case eEventLightningStop:
        if (lightningWidget.l != NULL)
        {
            zLightningKill(lightningWidget.l);
            lightningWidget.l = NULL;
        }
        break;
    }
}

static inline zLightning* lightningStart(zLightningAsset* asset)
{
    gLightningTweakAddInfo.color = asset->color;

    gLightningTweakAddInfo.start = &asset->start;
    gLightningTweakAddInfo.followStart = NULL;
    if (asset->followStart != 0)
    {
        gLightningTweakAddInfo.followStart = (xEnt*)zSceneFindObject(asset->followStart);
    }

    gLightningTweakAddInfo.end = &asset->end;
    gLightningTweakAddInfo.followEnd = NULL;
    if (asset->followEnd != 0)
    {
        gLightningTweakAddInfo.followEnd = (xEnt*)zSceneFindObject(asset->followEnd);
    }

    gLightningTweakAddInfo.thickness = asset->thickness;
    gLightningTweakAddInfo.segmentsPerMeter = 3.0f;
    gLightningTweakAddInfo.branchSpeed = asset->branchSpeed;
    gLightningTweakAddInfo.mainTexture = asset->mainTexture;
    gLightningTweakAddInfo.branchTexture = asset->branchTexture;
    gLightningTweakAddInfo.damage = asset->damage;
    gLightningTweakAddInfo.knockBackSpeed = asset->knockBackSpeed;

    if (asset->collisionEnabled == 1)
    {
        gLightningTweakAddInfo.collisionEnabled = TRUE;
    }
    else
    {
        gLightningTweakAddInfo.collisionEnabled = FALSE;
    }

    iSndGroupHandle handle = xSndMgrGetSoundGroup(asset->sound);
    xVec3 pos = (asset->end - asset->start) * 0.5f + asset->start;
    gLightningTweakAddInfo.sndHandle = xSndMgrPlay(handle, 0, &pos, NULL, NULL, NULL, NULL);

    collisionInfo.soundHit1 = xSndMgrGetSoundGroup(asset->soundHit1);
    collisionInfo.soundHit2 = xSndMgrGetSoundGroup(asset->soundHit2);

    return zLightningAdd(&gLightningTweakAddInfo);
}

static void lightningTweakStart(const tweak_info&)
{
    xVec3 s;
    xVec3 e;

    xVec3Add(&s, (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos, &sTweakStart);
    xVec3Add(&e, (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos, &sTweakEnd);
    gLightningTweakAddInfo.start = &s;
    gLightningTweakAddInfo.end = &e;
    zLightningAdd(&gLightningTweakAddInfo);
}

// No symbol on GC: empty in TSSM (the debug tweak registration is compiled out), so
// _zLightningAdd's call compiles to nothing and the copy is stripped.
void lightningAddTweaks(zLightning* l)
{
}

void zLightningInit()
{
    S32 i;
    S32 j;
    F32 prevEnd;

    LIGHTNING_FADE_DIST_SQR = 400.0f;
    LIGHTNING_CULL_DIST_SQR = 40000.0f;

    sLightning = (zLightning*)xMemAlloc(gActiveHeap, NUM_LIGHTNING * sizeof(zLightning), 0);

    for (i = 0; i < NUM_LIGHTNING; i++)
    {
        sLightning[i].sndHandle = (iSndHandle)-1;
    }

    sLightningParticleRaster = (RwTexture*)xSTFindAsset(0x24E99E06, NULL);

    posArray = (xVec3*)xMemAlloc(gActiveHeap, 100 * sizeof(xVec3), 0);

    for (i = 0; i < 9; i++)
    {
        sLFuncX[i].next = &sLFuncX[i + 1];
        sLFuncY[i].next = &sLFuncY[i + 1];
        sLFuncZ[i].next = &sLFuncZ[i + 1];
    }

    sLFuncX[9].next = NULL;
    sLFuncY[9].next = NULL;
    sLFuncZ[9].next = NULL;

    for (i = 0; i < 10; i++)
    {
        xVec3Init(&sLFuncVal[i], 2.0f * (xurand() - 0.5f), 2.0f * (xurand() - 0.5f),
                  2.0f * (xurand() - 0.5f));
        xVec3Init(&sLFuncSlope[i][0], sLFuncSlopeRange * (2.0f * (xurand() - 0.5f)),
                  sLFuncSlopeRange * (2.0f * (xurand() - 0.5f)),
                  sLFuncSlopeRange * (2.0f * (xurand() - 0.5f)));
        xVec3Init(&sLFuncSlope[i][1], sLFuncSlopeRange * (2.0f * (xurand() - 0.5f)),
                  sLFuncSlopeRange * (2.0f * (xurand() - 0.5f)),
                  sLFuncSlopeRange * (2.0f * (xurand() - 0.5f)));
        sLFuncEnd[i] = 0.25f * (xurand() - 0.5f) + (i + 1);
    }

    sLFuncEnd[9] = 10.0f;

    for (i = 0; i < 10; i++)
    {
        if (i == 0)
        {
            prevEnd = 0.0f;
            j = 9;
        }
        else
        {
            j = i - 1;
            prevEnd = sLFuncEnd[j];
        }

        xFuncPiece_EndPoints(&sLFuncX[i], prevEnd, sLFuncEnd[i], sLFuncVal[j].x, sLFuncVal[i].x);
        xFuncPiece_EndPoints(&sLFuncY[i], prevEnd, sLFuncEnd[i], sLFuncVal[j].y, sLFuncVal[i].y);
        xFuncPiece_EndPoints(&sLFuncZ[i], prevEnd, sLFuncEnd[i], sLFuncVal[j].z, sLFuncVal[i].z);
    }

    sLFuncJerkTime = 0.0f;

    gLightningTweakAddInfo.type = 3;
    gLightningTweakAddInfo.flags = 0x10;
    gLightningTweakAddInfo.time = 1000000.0f;
    xVec3Init(&sTweakStart, 0.0f, 0.0f, 0.0f);
    xVec3Init(&sTweakEnd, 3.0f, 3.0f, 3.0f);
    gLightningTweakAddInfo.color.r = 200;
    gLightningTweakAddInfo.color.g = 200;
    gLightningTweakAddInfo.color.b = 255;
    gLightningTweakAddInfo.color.a = 200;
    gLightningTweakAddInfo.arc_height = 0.5f;
    gLightningTweakAddInfo.rot_radius = 0.15f;
    gLightningTweakAddInfo.thickness = 0.25f;
    gLightningTweakAddInfo.rand_radius = 8.0f;
    gLightningTweakAddInfo.move_degrees = -2500.0f;
    gLightningTweakAddInfo.setup_degrees = 66.0f;
    gLightningTweakAddInfo.total_points = 16;
    gLightningTweakAddInfo.zeus_normal_offset = 0.75f;
    gLightningTweakAddInfo.zeus_back_offset = 0.2f;
    gLightningTweakAddInfo.zeus_side_offset = 0.0f;

    sLightningStartCB.on_change = (void (*)(tweak_info&))lightningTweakStart;
}

void zLightningReset()
{
    S32 i;

    for (i = 0; i < NUM_LIGHTNING; i++)
    {
        sLightning[i].flags &= ~0x1;
        xSndMgrStop(sLightning[i].sndHandle);
        sLightning[i].sndHandle = (iSndHandle)-1;

        if (sLightning[i].widget != NULL)
        {
            sLightning[i].widget->l = NULL;
        }
    }
}

zLightning* zLightningAdd(_tagLightningAdd* add)
{
    add->isMain = TRUE;
    return _zLightningAdd(add);
}

// No symbol on GC (always inlined). Branch bolts (flag 0x2000) search from NUM_LIGHTNING, i.e.
// retail never finds a free slot for them.
static inline zLightning* FindFreeLightning(U32 flags)
{
    S32 start;
    S32 end;

    if (flags & 0x2000)
    {
        start = NUM_LIGHTNING;
        end = NUM_LIGHTNING;
    }
    else
    {
        start = 0;
        end = NUM_LIGHTNING;
    }

    for (S32 i = start; i < end; i++)
    {
        if (!(sLightning[i].flags & 0x1))
        {
            return &sLightning[i];
        }
    }

    return NULL;
}

zLightning* _zLightningAdd(_tagLightningAdd* add)
{
    RwTexture* tex;
    static F32 defaultWeightParam[4][12] = {
        { 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f },
        { 1.0f, -2.5f, 2.0f, 0.0f, -1.0f, 1.5f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 0.0f },
        { 1.0f, -1.5f, 0.0f, 0.5f, -1.0f, 0.5f, 0.0f, 0.5f, 0.0f, -1.0f, 0.0f, 1.0f },
        { 4.0f, -8.0f, 4.0f, 0.0f, -4.0f, 4.0f, 0.0f, 0.0f, 0.0f, -4.0f, 4.0f, 0.0f },
    };
    zLightning* l;

    l = FindFreeLightning(add->flags);
    if (l == NULL)
    {
        return NULL;
    }

    l->widget = NULL;

    if (add->isMain)
    {
        tex = (RwTexture*)xSTFindAsset(add->mainTexture, NULL);
        if (tex != NULL)
        {
            l->mainTexture = tex->raster;
        }

        tex = (RwTexture*)xSTFindAsset(add->branchTexture, NULL);
        if (tex != NULL)
        {
            l->branchTexture = tex->raster;
        }
    }

    l->type = add->type;
    l->flags = add->flags | 0x41;
    l->color = add->color;
    l->sparkTimer = 0.0f;
    l->collisionPoint = *add->end;
    l->prevBranch = NULL;
    l->nextBranch = NULL;

    zLightningModifyEndpoints(l, add->start, add->end);

    l->func.endParam[0] = 10.0f * xurand();
    l->func.endParam[1] = 10.0f * xurand();
    l->func.endVel[0] = -1.0f;
    l->func.endVel[1] = 1.0f;
    l->func.width = add->thickness;
    l->segmentsPerMeter = add->segmentsPerMeter;
    l->genTime = 0.0f;
    l->branchSpeed = add->branchSpeed;
    l->damage = add->damage;
    l->knockBackSpeed = add->knockBackSpeed;
    l->collisionEnabled = add->collisionEnabled;
    l->sndHandle = add->sndHandle;
    l->alphaRamp = 0.0f;

    l->followStart = add->followStart;
    if (l->followStart != NULL && l->followStart->model != NULL)
    {
        l->followStartOffset = *add->start;
    }
    else
    {
        l->followStart = NULL;
    }

    l->followEnd = add->followEnd;
    if (l->followEnd != NULL && l->followEnd->model != NULL)
    {
        l->followEndOffset = *add->end;
    }
    else
    {
        l->followEnd = NULL;
    }

    l->renderCB = NULL;
    l->context = NULL;

    if (!(l->flags & 0x200))
    {
        l->flags |= 0xe00;
    }

    memcpy(l->weightParam, defaultWeightParam[(l->flags >> 10) & 0x3], sizeof(l->weightParam));

    if (l->flags & 0x10)
    {
        l->time_left = l->time_total = 0.1f;
    }
    else
    {
        l->time_left = l->time_total = add->time;
    }

    if (!(l->flags & 0x2000))
    {
        lightningAddTweaks(l);
    }

    return l;
}

void UpdateLightning(zLightning* l, F32 dt)
{
    xVec3 start;
    xVec3 end;
    S32 i;

    if (l->followStart != NULL || l->followEnd != NULL)
    {
        start = l->func.endPoint[0];
        if (l->followStart != NULL && l->followStart->model != NULL)
        {
            xVec3Add(&start, (xVec3*)&l->followStart->model->Mat->pos, &l->followStartOffset);
        }

        end = l->func.endPoint[1];
        if (l->followEnd != NULL && l->followEnd->model != NULL)
        {
            xVec3Add(&end, (xVec3*)&l->followEnd->model->Mat->pos, &l->followEndOffset);
        }

        zLightningModifyEndpoints(l, &start, &end);
    }

    if (l->flags & 0x2000)
    {
        if (l->branchSpeed != 0.0f)
        {
            l->parentSegment += l->branchSpeed;
            if (l->parentSegment >= l->lastParentSegment)
            {
                l->time_left = 0.0f;
            }
            else if (l->parentSegment < 0.0f)
            {
                l->time_left = 0.0f;
            }
        }
    }
    else if (!(l->flags & 0x10))
    {
        l->time_left -= dt;
    }

    if (l->time_left <= 0.0f)
    {
        if (l->flags & 0x100)
        {
            l->time_left = 0.0f;
            l->flags &= ~0x40;
        }
        else
        {
            l->flags &= ~0x1;
            if (l->prevBranch != NULL)
            {
                l->prevBranch->nextBranch = l->nextBranch;
            }
            if (l->nextBranch != NULL)
            {
                l->nextBranch->prevBranch = l->prevBranch;
            }
        }
    }

    for (i = 0; i < 2; i++)
    {
        l->func.endParam[i] += dt * (l->func.endVel[i] * sLFuncShift);
        while (l->func.endParam[i] > sLFuncEnd[9])
        {
            l->func.endParam[i] -= 10.0f;
        }
        while (l->func.endParam[i] < 0.0f)
        {
            l->func.endParam[i] += 10.0f;
        }
    }

    if (!(l->flags & 0x2000))
    {
        zLightningCollision(l, l->func.endPoint[0], l->func.endPoint[1]);
    }
}

void zLightningUpdate(F32 dt)
{
    S32 i;
    S32 picker;
    S32 j;
    F32 prevEnd;

    for (i = 0; i < NUM_LIGHTNING; i++)
    {
        if (sLightning[i].flags & 0x1)
        {
            UpdateLightning(&sLightning[i], dt);
        }
    }

    sLFuncUVOffset += sLFuncUVSpeed * dt;
    if (sLFuncUVOffset > 1.0f)
    {
        sLFuncUVOffset -= 1.0f;
    }

    sLFuncJerkTime += sLFuncJerkFreq * dt;
    if (sLFuncJerkTime > 1.0f)
    {
        picker = 9.0f * xurand();
        if (picker >= 9)
        {
            picker = 8;
        }
        if (picker < 0)
        {
            picker = 0;
        }

        xVec3Init(&sLFuncVal[picker], 2.0f * (xurand() - 0.5f), 2.0f * (xurand() - 0.5f),
                  2.0f * (xurand() - 0.5f));
        sLFuncEnd[picker] = 0.25f * (xurand() - 0.5f) + (picker + 1);

        for (i = picker; i <= picker + 1; i++)
        {
            if (i == 0)
            {
                prevEnd = 0.0f;
                j = 9;
            }
            else
            {
                j = i - 1;
                prevEnd = sLFuncEnd[j];
            }

            xFuncPiece_EndPoints(&sLFuncX[i], prevEnd, sLFuncEnd[i], sLFuncVal[j].x,
                                 sLFuncVal[i].x);
            xFuncPiece_EndPoints(&sLFuncY[i], prevEnd, sLFuncEnd[i], sLFuncVal[j].y,
                                 sLFuncVal[i].y);
            xFuncPiece_EndPoints(&sLFuncZ[i], prevEnd, sLFuncEnd[i], sLFuncVal[j].z,
                                 sLFuncVal[i].z);
        }

        sLFuncJerkTime = 0.0f;
    }

    for (i = 0; i < 10; i++)
    {
        sLFuncNew[i + 1].coef0.x = sLFuncX[i].coef[0];
        sLFuncNew[i + 1].coef0.y = sLFuncY[i].coef[0];
        sLFuncNew[i + 1].coef0.z = sLFuncZ[i].coef[0];
        sLFuncNew[i + 1].coef1.x = sLFuncX[i].coef[1];
        sLFuncNew[i + 1].coef1.y = sLFuncY[i].coef[1];
        sLFuncNew[i + 1].coef1.z = sLFuncZ[i].coef[1];
        sLFuncNew[i + 1].param = sLFuncX[i].end;
    }

    sLFuncNew[0] = sLFuncNew[1];
}

// Dead-stripped placeholder for zLightningDebugCB (Ratatouille's name for the stripped debug
// function here): restores the float literals and debug strings that retail still pools at this
// position. Body unknown.
static const char* __deadstripped_zLightning(F32* out)
{
    out[0] = 66.0f;
    out[1] = 15.0f;
    out[2] = 0.75f;
    out[3] = 5.0f;
    return "X to test lightning\n\0"
           "               \0"
           "1\0"
           "0\0"
           "\n";
}

// Forward differencing of the cubic weight curve w(t) = (a t^3 + b t^2 + c t + d) * scale, stepped
// by pstep: func = w(0), funcB/funcC/funcD = first/second/third differences, funcEnd = w(1). As in
// Ratatouille's debug build, _scl1.._scl3 re-read 'scale' while func and funcEnd use _scl.
#define LIGHTNING_FUNC_SETUP(func, funcB, funcC, funcD, funcEnd, wp, scale)                        \
    {                                                                                              \
        F32 _scl = (scale);                                                                        \
        F32 _scl1 = pstep * (scale);                                                               \
        F32 _scl2 = pstep2 * (scale);                                                              \
        F32 _scl3 = pstep3 * (scale);                                                              \
        func = (wp)[3] * _scl;                                                                     \
        funcB = (wp)[0] * _scl3 + (wp)[1] * _scl2 + (wp)[2] * _scl1;                               \
        funcC = 6.0f * (wp)[0] * _scl3 + 2.0f * (wp)[1] * _scl2;                                   \
        funcD = 6.0f * (wp)[0] * _scl3;                                                            \
        funcEnd = _scl * ((wp)[0] + (wp)[1] + (wp)[2] + (wp)[3]);                                  \
    }

static void zLightningFunc_Render(zLightning* l)
{
    F32 percent;
    F32 pstep;
    xVec3 val[2];
    xVec3 lastPos;
    xVec3 currPos;
    S32 i;
    F32 distanceFade;
    xVec3 drawPos;
    xVec3 drawAxis[2];
    RxObjSpace3DVertex* verts0;
    U32 numVerts;
    S32 u;
    U8 aVal;
    F32 pstep2;
    F32 pstep3;
    F32 w1func;
    F32 w1funcB;
    F32 w1funcC;
    F32 w1funcD;
    F32 w1funcEnd;
    F32 w2func;
    F32 w2funcB;
    F32 w2funcC;
    F32 w2funcD;
    F32 w2funcEnd;
    F32 wsumfunc;
    F32 wsumfuncB;
    F32 wsumfuncC;
    F32 wsumfuncD;
    F32 wsumfuncEnd;

    if (zLightningCull(l, distanceFade))
    {
        return;
    }

    percent = 0.0f;
    pstep = 1.0f;
    if (l->func.length > 0.00001f)
    {
        pstep = 1.0f / (l->func.length * (l->segmentsPerMeter * sLightningComplexityScale));
    }

    if ((U32)(l->func.length * l->segmentsPerMeter) * 2 + 2 > 480)
    {
        pstep = 0.004201681f;
    }

    verts0 = gRenderArr.m_vertex;
    numVerts = 0;

    if (l->func.length > 0.00001f)
    {
        drawAxis[0].x = l->func.direction.y - l->func.direction.z;
        drawAxis[0].y = l->func.direction.z - l->func.direction.x;
        drawAxis[0].z = l->func.direction.x - l->func.direction.y;
        xVec3Normalize(&drawAxis[0], &drawAxis[0]);
        xVec3Cross(&drawAxis[1], &drawAxis[0], &l->func.direction);
    }
    else
    {
        xVec3Init(&drawAxis[0], 1.0f, 0.0f, 0.0f);
        xVec3Init(&drawAxis[1], 0.0f, 0.0f, 1.0f);
    }

    // retail copies the start point and never reads it again
    xVec3Copy(&lastPos, &l->func.endPoint[0]);

    u = 1;

    if (l->flags & 0x80)
    {
        l->alphaRamp -= 0.1f;
        if (l->alphaRamp <= 0.0f)
        {
            _zLightningKill(l);
        }
    }
    else
    {
        l->alphaRamp += 0.1f;
    }

    pstep2 = pstep * pstep;
    pstep3 = pstep * pstep2;

    LIGHTNING_FUNC_SETUP(w1func, w1funcB, w1funcC, w1funcD, w1funcEnd, &l->weightParam[0],
                         l->func.scale);
    LIGHTNING_FUNC_SETUP(w2func, w2funcB, w2funcC, w2funcD, w2funcEnd, &l->weightParam[4],
                         l->func.scale);
    LIGHTNING_FUNC_SETUP(wsumfunc, wsumfuncB, wsumfuncC, wsumfuncD, wsumfuncEnd, &l->weightParam[8],
                         l->func.scale * l->func.width);

    U8 isFinishLoop = FALSE;

    do
    {
        if (numVerts >= 480)
        {
            return;
        }

        if (percent >= 1.0f)
        {
            percent = 1.0f;
            isFinishLoop = TRUE;
            w1func = w1funcEnd;
            w2func = w2funcEnd;
            wsumfunc = wsumfuncEnd;
        }

        // currPos = lerp(endPoint[0], endPoint[1], percent)
        RwV3dScaleMacro((RwV3d*)&currPos, (RwV3d*)&l->func.endPoint[0], 1.0f - percent);
        RwV3dIncrementScaledMacro((RwV3d*)&currPos, (RwV3d*)&l->func.endPoint[1], percent);

        for (i = 0; i < 2; i++)
        {
            F32 fParam;
            S32 iParam;
            zLightningFuncPiece* piece;

            fParam = percent * l->func.paramSpan[i] + l->func.endParam[i];
            fParam -= (S32)(0.1f * fParam) * 10;
            iParam = 0.49f + fParam;
            if (fParam > sLFuncNew[iParam].param)
            {
                iParam++;
            }
            piece = &sLFuncNew[iParam];

            val[i].x = piece->coef1.x * fParam + piece->coef0.x;
            val[i].y = piece->coef1.y * fParam + piece->coef0.y;
            val[i].z = piece->coef1.z * fParam + piece->coef0.z;
        }

        currPos.x += w1func * val[0].x + w2func * val[1].x;
        currPos.y += w1func * val[0].y + w2func * val[1].y;
        currPos.z += w1func * val[0].z + w2func * val[1].z;

        if (l->alphaRamp < 2.0f)
        {
            F32 alpha;

            if (percent < 0.5f)
            {
                alpha = -(2.0f * percent) + l->alphaRamp;
            }
            else
            {
                alpha = 2.0f * (percent - 1.0f) + l->alphaRamp;
            }

            if (alpha < 0.0f)
            {
                alpha = 0.0f;
            }
            else if (alpha > 1.0f)
            {
                alpha = 1.0f;
            }

            aVal = alpha * (255.99f * xurand());
        }
        else
        {
            aVal = xrand_RandomInt32() & 0xFF;
        }

        if (distanceFade != 1.0f)
        {
            aVal = aVal * distanceFade;
        }

        {
            xVec3 dir;

            xVec3SMul(&dir, &drawAxis[0], wsumfunc);

            xVec3Add(&drawPos, &currPos, &dir);
            RwIm3DVertexSetPos(&verts0[numVerts], drawPos.x, drawPos.y, drawPos.z);
            RwIm3DVertexSetRGBA(&verts0[numVerts], l->color.r, l->color.g, l->color.b, aVal);
            RwIm3DVertexSetUV(&verts0[numVerts], u + sLFuncUVOffset, 0.0f);

            xVec3Sub(&drawPos, &currPos, &dir);
            RwIm3DVertexSetPos(&verts0[numVerts + 1], drawPos.x, drawPos.y, drawPos.z);
            RwIm3DVertexSetRGBA(&verts0[numVerts + 1], l->color.r, l->color.g, l->color.b, aVal);
            RwIm3DVertexSetUV(&verts0[numVerts + 1], u + sLFuncUVOffset, 1.0f);
        }

        {
            xVec3 dir;

            xVec3SMul(&dir, &drawAxis[1], wsumfunc);

            xVec3Add(&drawPos, &currPos, &dir);
            RwIm3DVertexSetNormal(&verts0[numVerts], drawPos.x, drawPos.y, drawPos.z);

            xVec3Sub(&drawPos, &currPos, &dir);
            RwIm3DVertexSetNormal(&verts0[numVerts + 1], drawPos.x, drawPos.y, drawPos.z);
        }

        u = 1 - u;
        numVerts += 2;

        w1func += w1funcB;
        w1funcB += w1funcC;
        w1funcC += w1funcD;
        w2func += w2funcB;
        w2funcB += w2funcC;
        w2funcC += w2funcD;
        wsumfunc += wsumfuncB;
        wsumfuncB += wsumfuncC;
        wsumfuncC += wsumfuncD;

        percent += pstep;
    } while (!isFinishLoop);

    if (l->renderCB != NULL)
    {
        l->renderCB(l, verts0, NULL, numVerts);
    }
    else if (RwIm3DTransform(verts0, numVerts, NULL,
                             rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
    {
        RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
        RwIm3DEnd();
    }

    // second ribbon: the positions along drawAxis[1] were stashed in the normal slots
    for (U32 vidx = 0; vidx < numVerts; vidx++)
    {
        // RwV3d copy as in Ratatouille's debug build (from the vertex's nx field to its x field)
        *(RwV3d*)&verts0[vidx].x = *(RwV3d*)&verts0[vidx].nx;
    }

    if (l->renderCB != NULL)
    {
        l->renderCB(l, NULL, verts0, numVerts);
    }
    else if (RwIm3DTransform(verts0, numVerts, NULL,
                             rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
    {
        RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
        RwIm3DEnd();
    }
}

// Returns true when the bolt is out of range or no point along it is inside the view frustum.
bool zLightningCull(zLightning* l, F32& distanceFade)
{
    xMat4x3 mat = zCamGetMatrix();

    xVec3 pos = l->func.endPoint[0] + (l->func.endPoint[1] - l->func.endPoint[0]) * 0.5f;
    F32 dist2 = xVec3Dist2(&pos, &mat.pos);
    if (dist2 > LIGHTNING_CULL_DIST_SQR)
    {
        return true;
    }

    if (dist2 > LIGHTNING_FADE_DIST_SQR)
    {
        distanceFade = 1.0f - (dist2 - LIGHTNING_FADE_DIST_SQR) /
                                  (LIGHTNING_CULL_DIST_SQR - LIGHTNING_FADE_DIST_SQR);
    }
    else
    {
        distanceFade = 1.0f;
    }

    xVec3 step = (l->func.endPoint[1] - l->func.endPoint[0]) * 0.1f;
    pos = l->func.endPoint[0];

    S32 i = 11;
    bool doDraw = false;
    RwSphere worldsph;
    while (i != 0)
    {
        i--;
        worldsph.center.x = pos.x;
        worldsph.center.y = pos.y;
        worldsph.center.z = pos.z;
        worldsph.radius = 0.5f;

        if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
        {
            doDraw = true;
            break;
        }

        pos += step;
    }

    return !doDraw;
}

void zLightningRender()
{
    S32 i;
    zLightning* l;
    S32 start;
    F32 frac;
    xVec3 oldEndPoint;

    xFXHighDynamicRangeBrighten(true);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);

    for (i = 0; i < NUM_LIGHTNING; i++)
    {
        if ((sLightning[i].flags & 0x41) == 0x41)
        {
            sLightningRaster = sLightning[i].mainTexture;
            if (sLightningRaster != NULL)
            {
                RwRenderStateSet(rwRENDERSTATETEXTURERASTER, sLightningRaster);
            }
            zLightningFunc_Render(&sLightning[i]);

            sLightningRasterBranch = sLightning[i].branchTexture;
            if (sLightningRasterBranch != NULL)
            {
                RwRenderStateSet(rwRENDERSTATETEXTURERASTER, sLightningRasterBranch);
            }

            for (l = sLightning[i].nextBranch; l != NULL; l = l->nextBranch)
            {
                if (l->branchSpeed != 0.0f)
                {
                    start = l->parentSegment;
                    frac = l->parentSegment - start;
                    oldEndPoint = l->func.endPoint[0];
                    l->func.endPoint[0] =
                        posArray[start + 1] * frac + posArray[start] * (1.0f - frac);
                    l->func.endPointB += l->func.endPoint[0] - oldEndPoint;
                }
                else
                {
                    l->func.endPoint[0] = posArray[(S32)l->parentSegment];
                }

                if (l->randomizeBranchEndPoint)
                {
                    l->func.endPoint[1].x = 0.3f * (xurand() - 0.5f) + l->func.endPointB.x;
                    l->func.endPoint[1].y = 0.3f * (xurand() - 0.5f) + l->func.endPointB.y;
                    l->func.endPoint[1].z = 0.3f * (xurand() - 0.5f) + l->func.endPointB.z;
                }

                zLightningFunc_Render(l);
            }
        }
    }

    xFXHighDynamicRangeBrighten(false);
}

void zLightningKill(zLightning* l)
{
    _zLightningKill(l);
}

void _zLightningKill(zLightning* l)
{
    if (l == NULL)
    {
        return;
    }

    l->flags &= ~0x101;
    if (l->sndHandle != -1)
    {
        xSndMgrStop(l->sndHandle);
        l->sndHandle = (iSndHandle)-1;
    }
}

// Dead-stripped placeholder: retail pools 4.0f and the int->float double here, from a stripped
// function (likely Ratatouille's zLightningCreateBranch). Body unknown.
static void __deadstripped2_zLightning(F32* out, S32 n)
{
    out[0] = 4.0f;
    out[1] = n;
}

void zLightningModifyEndpoints(zLightning* l, const xVec3* start, const xVec3* end)
{
    zLightningModifyEndpoints(l, start, end, sLFuncScalePerLength);
}

void zLightningModifyEndpoints(zLightning* l, const xVec3* start, const xVec3* end,
                               F32 scalePerLength)
{
    if (l == NULL)
    {
        return;
    }

    xVec3Copy(&l->func.endPoint[0], start);
    xVec3Copy(&l->func.endPoint[1], end);
    xVec3Copy(&l->func.endPointB, end);
    xVec3Sub(&l->func.direction, &l->func.endPoint[1], &l->func.endPoint[0]);
    l->func.length = xVec3Length(&l->func.direction);

    if (l->func.length > 0.00001f)
    {
        l->func.direction *= 1.0f / l->func.length;
    }
    else
    {
        xVec3Init(&l->func.direction, 0.0f, 0.0f, 0.0f);
    }

    l->func.scale = l->func.length * scalePerLength;
    if (l->func.scale < sLFuncMinScale)
    {
        l->func.scale = sLFuncMinScale;
    }
    if (l->func.scale > sLFuncMaxScale)
    {
        l->func.scale = sLFuncMaxScale;
    }

    l->func.paramSpan[0] = l->func.length * sLFuncSpanPerLength;
    if (l->func.paramSpan[0] < sLFuncMinSpan)
    {
        l->func.paramSpan[0] = sLFuncMinSpan;
    }
    l->func.paramSpan[1] = l->func.paramSpan[0];
}

// Weak in retail and emitted in zLightning's own .text right after zLightningCollision, its caller,
// so TSSM defined it in this file (Ratatouille has it as an xMath3.h inline).
inline F32 xMat3x3LookVec3(xMat3x3& mat, const xVec3& at)
{
    F32 mag = at.length();
    if (mag >= -0.00001f && mag <= 0.00001f)
    {
        mat = g_I3;
        return 0.0f;
    }

    mat.at = at;
    mat.at *= 1.0f / mag;

    F32 ax = xabs(mat.at.x);
    F32 ay = xabs(mat.at.y);
    F32 az = xabs(mat.at.z);
    if (ax < ay && ax < az)
    {
        mat.right.assign(0.0f, mat.at.z, -mat.at.y);
    }
    else if (ay < az)
    {
        mat.right.assign(-mat.at.z, 0.0f, mat.at.x);
    }
    else
    {
        mat.right.assign(mat.at.y, -mat.at.x, 0.0f);
    }

    mat.right.normalize();
    mat.up = mat.right.cross(mat.at);

    return mag;
}

// zLightningCollisionCB and zLightningCollide are global functions in Ratatouille, with these names
// and parameters. TSSM has no symbols for them: they are auto-inlined into zLightningCollision.
// Retail copies hitDir into dir before overwriting it with the player's facing.
static void zLightningCollisionCB(xEnt& ent)
{
    xVec3 dir = collisionInfo.hitDir;
    dir = xEntGetFrame(&ent)->at;
    dir.x = -dir.x;
    dir.y = 0.0f;
    dir.z = -dir.z;
    dir.normalize();
    dir *= collisionInfo.knockBackSpeed;
    zEntPlayer_Damage(&ent, 1, &dir);
}

static void zLightningCollide(const xBound& bound)
{
    if (xSphereHitsBound(xglobals->___player_ent_dont_use_directly->bound.sph, bound))
    {
        zLightningCollisionCB(*xglobals->___player_ent_dont_use_directly);
    }
}

void zLightningCollision(zLightning* l, xVec3& start, xVec3& end)
{
    xBound bound;
    xMat4x3 mat;

    if (l->collisionEnabled)
    {
        bound.type = XBOUND_TYPE_OBB;
        collisionInfo.hitDir = end - start;
        collisionInfo.l = l;
        xMat3x3LookVec3(mat, collisionInfo.hitDir);
        bound.mat = &mat;
        mat.pos = start;
        bound.box.box.lower.x = -0.25f;
        bound.box.box.lower.y = -0.75f;
        bound.box.box.lower.z = 0.0f;
        bound.box.box.upper.x = 0.25f;
        bound.box.box.upper.y = 0.25f;
        bound.box.box.upper.z = collisionInfo.hitDir.length();

        collisionInfo.damage = l->damage;
        collisionInfo.knockBackSpeed = l->knockBackSpeed;

        zLightningCollide(bound);
    }
}
