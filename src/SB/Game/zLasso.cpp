#include "zLasso.h"

#include "iModel.h"
#include "xLightKit.h"
#include "xMath.h"
#include "xMemMgr.h"
#include "xVec3Inlines.h"
#include "xstransvc.h"
#include "xString.h"
#include "zParPTank.h"

#include <string.h>

extern tagiRenderArrays gRenderArr;

static S32 sPatTongueLastCount;
static RwRaster* sLassoRaster;
static zLassoGuideList sGuideList[64];
static U32 sNumGuideLists;
static zLassoGuide* sCurrentGuide;
static RxObjSpace3DVertex* lnverts;
static S32 negativeHondaX = 1;
static PatTongueVert sPatMouthVert[12] = {
    { { 0.0f, 0.924f, -0.132f } },    { { -0.027f, 0.928f, -0.128f } },
    { { -0.063f, 0.927f, -0.116f } }, { { -0.069f, 0.91f, -0.098f } },
    { { -0.064f, 0.899f, -0.081f } }, { { -0.031f, 0.885f, -0.079f } },
    { { 0.0f, 0.88f, -0.08f } },      { { 0.031f, 0.882f, -0.075f } },
    { { 0.061f, 0.894f, -0.081f } },  { { 0.07f, 0.905f, -0.098f } },
    { { 0.063f, 0.927f, -0.116f } },  { { 0.027f, 0.928f, -0.128f } },
};
static PatTongueVert sPatTipVert[12] = {
    { { 0.0f, 0.018f, -0.122f } },     { { -0.027f, 0.022f, -0.118f } },
    { { -0.063f, 0.021f, -0.106f } },  { { -0.069f, 0.005f, -0.088f } },
    { { -0.064f, -0.007f, -0.072f } }, { { -0.031f, -0.021f, -0.07f } },
    { { 0.0f, -0.025f, -0.071f } },    { { 0.031f, -0.024f, -0.065f } },
    { { 0.061f, -0.012f, -0.072f } },  { { 0.07f, -0.001f, -0.088f } },
    { { 0.063f, 0.021f, -0.106f } },   { { 0.027f, 0.022f, -0.118f } },
};
static PatTongueVert sPatTeethVert[2] = {
    { { 0.0f, 0.92f, 0.092f } },
    { { 0.0f, 0.92f, 0.102f } },
};
static xVec3 sPatHitchPos = { 0.0f, 0.0f, 0.0f };
static RwMatrixTag sPatTongueTipMatrix;
static xVec3 sPatTongueLastVec[16];
static xVec3 sPatTongueLastRight;

// The fizzical* helpers are plain statics (as in BfBB): GC/2.6 auto-inlines them into zLasso_Update
// and strips the copies, but their literals are pooled at this source position, as in retail.
static void fizzicalRadius(zLasso* lasso, F32 dt, xVec3* newPoint);
static void fizzicalCenter(zLasso* lasso, F32 dt, xVec3* newPoint);
static void fizzicalNormal(zLasso* lasso, F32 dt, xVec3* newPoint);
static void fizzicalHonda(zLasso* lasso, F32 dt, xVec3* newPoint);
static void nonfizzicalHonda(zLasso* lasso, F32 dt, xVec3* newPoint);
static void fizzicalSlack(zLasso* lasso, F32 dt, xVec3* newPoint);
static void vec2vecMat(xMat4x3* m, xVec3* v1, xVec3* v2);
static void zPatTongue_Render(zLasso* lasso);

struct IMLightKit
{
    xVec4 lightdir[3];
    xVec4 lightcol[5];
    xVec4 extraLightdir[3];
    xVec4 extraLightcol[4];
    S32 extraLights;
};

void zLasso_Init(zLasso* lasso, xModelInstance* model, F32 x, F32 y, F32 z)
{
    lasso->patTongue = 0;
    if (sLassoRaster == NULL)
    {
        RwTexture* tempTexture = (RwTexture*)xSTFindAsset(xStrHash("rope"), NULL);
        if (tempTexture != NULL)
        {
            sLassoRaster = tempTexture->raster;
        }
        else
        {
            sLassoRaster = NULL;
        }
    }

    iModelTagSetup(&lasso->tag, model->Data, x, y, z);

    lasso->model = model;
    lnverts = gRenderArr.m_vertex;
}

// The guide setup follows the PS2 line order. Each strip opens with spoke i = 2, the pair that
// closes the tube, which gives retail's (x + 2) * 12 indexing without an extra local.
void zLasso_Render(zLasso* lasso)
{
    S32 i;
    xVec3 loop[16];
    S32 numVerts;
    F32 arc_length;
    U8 shouldUseGuide;

    if (lasso->patTongue)
    {
        zPatTongue_Render(lasso);
        return;
    }

    shouldUseGuide = ((lasso->flags & 0x800) && (lasso->flags & 0x4000)) ||
                     (!(lasso->flags & 0x800) && (lasso->flags & 0x2000));

    if (shouldUseGuide)
    {
        F32 time;
        RpGeometry* geom;
        S32 numMorphs;
        S32 mIndx1;
        F32 interp;
        S32 mIndx2;
        RpMorphTarget* m1;
        RpMorphTarget* m2;
        RwV3d* m1V;
        RwV3d* m2V;

        time = sCurrentGuide->poly->Anim->Single->Time;
        time = time * 30.0f;
        geom = sCurrentGuide->poly->Data->geometry;
        numMorphs = geom->numMorphTargets;
        mIndx1 = (S32)time;
        interp = time - mIndx1;
        mIndx1 %= numMorphs;
        mIndx2 = (mIndx1 + 1) % numMorphs;
        m1 = &geom->morphTarget[mIndx1];
        m2 = &geom->morphTarget[mIndx2];
        m1V = m1->verts;
        m2V = m2->verts;
        numVerts = geom->numTriangles;
        arc_length = 0.0f;

        for (i = 0; i < numVerts; i++)
        {
            xVec3SMul(&loop[i], (xVec3*)&m1V[sCurrentGuide->vertMap[i]], 1.0f - interp);
            xVec3AddScaled(&loop[i], (xVec3*)&m2V[sCurrentGuide->vertMap[i]], interp);
            xMat4x3Toworld(&loop[i], (xMat4x3*)sCurrentGuide->poly->Mat, &loop[i]);
        }

        {
            xVec3 temp;
            xVec3 dif;
            xVec3 currClose;
            xVec3 closestPoint;
            xVec3 hand;
            F32 closestDistSqr;
            F32 len;
            F32 dot; // not in the PS2 DWARF; retail keeps this projection in a register

            xVec3Add(&hand, &lasso->lastRefs[lasso->reindex[0]], &lasso->anchor);
            xVec3Sub(&temp, &hand, &loop[0]);
            xVec3Copy(&closestPoint, &loop[0]);
            closestDistSqr = xVec3Dot(&temp, &temp);

            for (i = 0; i < numVerts; i++)
            {
                xVec3Sub(&temp, &hand, &loop[i]);
                xVec3Sub(&dif, &loop[(i + 1) % numVerts], &loop[i]);
                len = xVec3Normalize(&dif, &dif);
                dot = xVec3Dot(&temp, &dif);

                if (dot < 0.0f)
                {
                    xVec3Copy(&currClose, &loop[i]);
                }
                else if (dot > len)
                {
                    xVec3Copy(&currClose, &loop[(i + 1) % numVerts]);
                }
                else
                {
                    xVec3SMul(&currClose, &dif, dot);
                    xVec3AddTo(&currClose, &loop[i]);
                }

                xVec3Sub(&temp, &hand, &currClose);
                len = xVec3Dot(&temp, &temp);
                if (len < closestDistSqr)
                {
                    xVec3Copy(&closestPoint, &currClose);
                    closestDistSqr = len;
                }

                arc_length += xVec3Dist(&loop[i], &loop[(i + 1) % numVerts]);
            }

            xVec3Sub(&lasso->honda, &closestPoint, &lasso->anchor);
        }
    }
    else
    {
        arc_length = 2.0f * PI * lasso->crRadius;
    }

    {
        RxObjSpace3DVertex* tmp_pt;
        xVec3 b1;
        xVec3 b2;
        xVec3 b3;
        F32 s[3];
        F32 c[3];
        F32 a;
        F32 astep;
        xVec3 worldHonda;
        xVec3 incr;
        xVec3 disp;
        xVec3 crossSection[3];
        xVec3 sections[6];
        S32 currSection;
        S32 lastSection;
        S32 numSections;
        F32 distSoFar;
        F32 distTotal;
        F32 spokeDistIncr;
        U32 numVertsSoFar;
        F32 u;
        F32 v;
        F32 ustep;
        F32 vstep;

        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, sLassoRaster);

        currSection = 3;
        lastSection = 0;
        numSections = 0;
        distSoFar = 0.0f;

        xVec3Sub(&b2, &lasso->lastRefs[lasso->reindex[0]], &lasso->honda);
        distTotal = xVec3Normalize(&b2, &b2);
        lasso->lastDist = lasso->currDist;
        lasso->currDist = distTotal;

        b1.x = b2.y - b2.z;
        b1.y = b2.z - b2.x;
        b1.z = b2.x - b2.y;
        xVec3Normalize(&b1, &b1);
        xVec3Cross(&b3, &b1, &b2);

        // one pass over the three spokes: the PS2 line table has a loop header here, and a = 0 is
        // folded from i
        for (i = 0; i < 3; i += 3)
        {
            a = 2.0f * (PI * i) / 3.0f;
            astep = 2.0f * PI / 3.0f;
            c[0] = icos(a);
            c[1] = icos(a + astep);
            c[2] = icos(a + 2.0f * astep);
            s[0] = isin(a);
            s[1] = isin(a + astep);
            s[2] = isin(a + 2.0f * astep);

            xVec3SMul(&crossSection[i], &b1, 0.025f * s[0]);
            xVec3AddScaled(&crossSection[i], &b3, 0.025f * c[0]);
            xVec3SMul(&crossSection[i + 1], &b1, 0.025f * s[1]);
            xVec3AddScaled(&crossSection[i + 1], &b3, 0.025f * c[1]);
            xVec3SMul(&crossSection[i + 2], &b1, 0.025f * s[2]);
            xVec3AddScaled(&crossSection[i + 2], &b3, 0.025f * c[2]);
        }

        xVec3Add(&worldHonda, &lasso->honda, &lasso->anchor);
        xVec3Copy(&disp, &worldHonda);

        spokeDistIncr = distTotal / 43.0f;

        for (i = 0; i < 3; i++)
        {
            xVec3Add(&sections[i], &crossSection[i], &worldHonda);
        }

        if (spokeDistIncr < 0.2f)
        {
            spokeDistIncr = 0.2f;
        }

        xVec3SMul(&incr, &b2, spokeDistIncr);

        u = arc_length;
        tmp_pt = lnverts;
        ustep = spokeDistIncr;
        v = 0.0f;
        vstep = 1.0f / 3.0f;
        numVertsSoFar = 0;

        while (distSoFar < distTotal && numVertsSoFar + 10 <= 480)
        {
            xVec3 sectionCenter;
            F32 xDisp;
            F32 yDisp;

            if (u > 1.0f)
            {
                u -= 1.0f;
            }

            distSoFar += spokeDistIncr;

            if (distSoFar < distTotal)
            {
                xVec3AddTo(&disp, &incr);
                xVec3Copy(&sectionCenter, &disp);
                xDisp = (distTotal - distSoFar) *
                        (distSoFar * (lasso->crSlack * isin(PI * (0.75f * distSoFar)))) /
                        (distTotal * distTotal);
                yDisp = (distTotal - distSoFar) *
                        (distSoFar * (lasso->crSlack * icos(PI * (0.75f * distSoFar)))) /
                        (distTotal * distTotal);
                xVec3AddScaled(&sectionCenter, &b1, xDisp);
                xVec3AddScaled(&sectionCenter, &b3, yDisp);
            }
            else
            {
                xVec3Add(&sectionCenter, &lasso->anchor, &lasso->lastRefs[lasso->reindex[0]]);
                ustep = distTotal + (spokeDistIncr - distSoFar);
            }

            for (i = 0; i < 3; i++)
            {
                xVec3Add(&sections[currSection + i], &crossSection[i], &sectionCenter);
            }

            i = 2;

            RwIm3DVertexSetPos(tmp_pt, sections[lastSection + i].x, sections[lastSection + i].y,
                               sections[lastSection + i].z);
            RwIm3DVertexSetRGBA(tmp_pt, 255, 255, 255, 255);
            RwIm3DVertexSetUV(tmp_pt, u, 2.0f / 3.0f);
            tmp_pt++;
            RwIm3DVertexSetPos(tmp_pt, sections[currSection + i].x, sections[currSection + i].y,
                               sections[currSection + i].z);
            RwIm3DVertexSetRGBA(tmp_pt, 255, 255, 255, 255);
            RwIm3DVertexSetUV(tmp_pt, u + ustep, 2.0f / 3.0f);
            tmp_pt++;
            numVertsSoFar += 2;

            for (i = 0; i < 3; i++)
            {
                v = i * vstep;
                RwIm3DVertexSetPos(tmp_pt, sections[lastSection + i].x, sections[lastSection + i].y,
                                   sections[lastSection + i].z);
                RwIm3DVertexSetRGBA(tmp_pt, 255, 255, 255, 255);
                RwIm3DVertexSetUV(tmp_pt, u, v);
                tmp_pt++;
                RwIm3DVertexSetPos(tmp_pt, sections[currSection + i].x, sections[currSection + i].y,
                                   sections[currSection + i].z);
                RwIm3DVertexSetRGBA(tmp_pt, 255, 255, 255, 255);
                RwIm3DVertexSetUV(tmp_pt, u + ustep, v);
                tmp_pt++;
                numVertsSoFar += 2;
            }

            lastSection = currSection;
            currSection = currSection ? 0 : 3;
            u += ustep;
            numSections++;
        }

        RwIm3DVertexSetPos(tmp_pt, sections[lastSection + 2].x, sections[lastSection + 2].y,
                           sections[lastSection + 2].z);
        RwIm3DVertexSetRGBA(tmp_pt, 255, 255, 255, 255);
        RwIm3DVertexSetUV(tmp_pt, u, v);
        tmp_pt++;
        numVertsSoFar++;
        S32 shouldRepeat = 1;

        if (!shouldUseGuide)
        {
            xMat4x3 rotMatrix;
            xVec3 addTo;
            xVec3 temp;
            S32 k;

            xVec3Sub(&b1, &lasso->honda, &lasso->crCenter);
            xVec3Cross(&b3, &b1, &lasso->crNormal);
            xVec3Normalize(&temp, &b3);
            vec2vecMat(&rotMatrix, &b2, &temp);

            for (i = 0; i < 3; i++)
            {
                xMat4x3Toworld(&crossSection[i], &rotMatrix, &crossSection[i]);
            }

            xMat4x3Rot(&rotMatrix, &lasso->crNormal, 2.0f * PI / 15.0f);

            lastSection = 0;
            currSection = 3;

            for (i = 0; i < 3; i++)
            {
                xVec3Add(&sections[i], &crossSection[i], &worldHonda);
            }

            xVec3Sub(&disp, &lasso->honda, &lasso->crCenter);

            xVec3Add(&addTo, &lasso->crCenter, &lasso->anchor);
            ustep = 2.0f * (PI * lasso->crRadius) / 15.0f;
            u = 0.0f;

            for (k = 0; k < 15 && numVertsSoFar + 8 + shouldRepeat <= 480; k++)
            {
                if (u > 1.0f)
                {
                    u -= 1.0f;
                }

                xMat4x3Toworld(&disp, &rotMatrix, &disp);

                for (i = 0; i < 3; i++)
                {
                    xMat4x3Toworld(&crossSection[i], &rotMatrix, &crossSection[i]);
                    xVec3Add(&sections[i + currSection], &crossSection[i], &disp);
                    xVec3AddTo(&sections[i + currSection], &addTo);
                }

                i = 2;

                if (shouldRepeat)
                {
                    RwIm3DVertexSetPos(tmp_pt, sections[lastSection + i].x,
                                       sections[lastSection + i].y, sections[lastSection + i].z);
                    RwIm3DVertexSetRGBA(tmp_pt, 255, 255, 255, 255);
                    RwIm3DVertexSetUV(tmp_pt, u, 2.0f / 3.0f);
                    tmp_pt++;
                    shouldRepeat = 0;
                    numVertsSoFar++;
                }

                RwIm3DVertexSetPos(tmp_pt, sections[lastSection + i].x, sections[lastSection + i].y,
                                   sections[lastSection + i].z);
                RwIm3DVertexSetRGBA(tmp_pt, 255, 255, 255, 255);
                RwIm3DVertexSetUV(tmp_pt, u, 2.0f / 3.0f);
                tmp_pt++;
                RwIm3DVertexSetPos(tmp_pt, sections[currSection + i].x, sections[currSection + i].y,
                                   sections[currSection + i].z);
                RwIm3DVertexSetRGBA(tmp_pt, 255, 255, 255, 255);
                RwIm3DVertexSetUV(tmp_pt, u + ustep, 2.0f / 3.0f);
                tmp_pt++;
                numVertsSoFar += 2;

                for (i = 0; i < 3; i++)
                {
                    v = i * vstep;
                    RwIm3DVertexSetPos(tmp_pt, sections[lastSection + i].x,
                                       sections[lastSection + i].y, sections[lastSection + i].z);
                    RwIm3DVertexSetRGBA(tmp_pt, 255, 255, 255, 255);
                    RwIm3DVertexSetUV(tmp_pt, u, v);
                    tmp_pt++;
                    RwIm3DVertexSetPos(tmp_pt, sections[currSection + i].x,
                                       sections[currSection + i].y, sections[currSection + i].z);
                    RwIm3DVertexSetRGBA(tmp_pt, 255, 255, 255, 255);
                    RwIm3DVertexSetUV(tmp_pt, u + ustep, v);
                    tmp_pt++;
                    numVertsSoFar += 2;
                }

                lastSection = currSection;
                currSection = currSection ? 0 : 3;
                u += ustep;
                numSections++;
            }
        }
        else
        {
            xMat4x3 rotMatrix;
            xVec3 norm;
            xVec3 oldNorm;
            S32 k;
            S32 k1;
            S32 k2;
            S32 k3;

            xVec3Copy(&oldNorm, &b2);
            xVec3Sub(&norm, &loop[1], &loop[numVerts - 1]);
            xVec3Normalize(&norm, &norm);
            vec2vecMat(&rotMatrix, &oldNorm, &norm);
            xVec3Copy(&norm, &oldNorm);

            for (i = 0; i < 3; i++)
            {
                xMat4x3Toworld(&crossSection[i], &rotMatrix, &crossSection[i]);
            }

            lastSection = 0;
            currSection = 3;

            for (i = 0; i < 3; i++)
            {
                xVec3Add(&sections[i], &crossSection[i], &loop[0]);
            }

            u = 0.0f;

            for (k = 1; k <= numVerts && numVertsSoFar + 8 + shouldRepeat <= 480; k++)
            {
                k1 = k % numVerts;
                k2 = (k + 1) % numVerts;
                k3 = (k - 1) % numVerts;

                if (u > 1.0f)
                {
                    u -= 1.0f;
                }

                xVec3Sub(&norm, &loop[k2], &loop[k3]);
                xVec3Normalize(&norm, &norm);
                vec2vecMat(&rotMatrix, &oldNorm, &norm);
                xVec3Copy(&oldNorm, &norm);

                for (i = 0; i < 3; i++)
                {
                    xMat4x3Toworld(&crossSection[i], &rotMatrix, &crossSection[i]);
                    xVec3Add(&sections[i + currSection], &crossSection[i], &loop[k1]);
                }

                ustep = xVec3Dist(&loop[k1], &loop[k2]);

                i = 2;

                if (shouldRepeat)
                {
                    RwIm3DVertexSetPos(tmp_pt, sections[lastSection + i].x,
                                       sections[lastSection + i].y, sections[lastSection + i].z);
                    RwIm3DVertexSetRGBA(tmp_pt, 255, 255, 255, 255);
                    RwIm3DVertexSetUV(tmp_pt, u, 2.0f / 3.0f);
                    tmp_pt++;
                    shouldRepeat = 0;
                    numVertsSoFar++;
                }

                RwIm3DVertexSetPos(tmp_pt, sections[lastSection + i].x, sections[lastSection + i].y,
                                   sections[lastSection + i].z);
                RwIm3DVertexSetRGBA(tmp_pt, 255, 255, 255, 255);
                RwIm3DVertexSetUV(tmp_pt, u, 2.0f / 3.0f);
                tmp_pt++;
                RwIm3DVertexSetPos(tmp_pt, sections[currSection + i].x, sections[currSection + i].y,
                                   sections[currSection + i].z);
                RwIm3DVertexSetRGBA(tmp_pt, 255, 255, 255, 255);
                RwIm3DVertexSetUV(tmp_pt, u + ustep, 2.0f / 3.0f);
                tmp_pt++;
                numVertsSoFar += 2;

                for (i = 0; i < 3; i++)
                {
                    v = i * vstep;
                    RwIm3DVertexSetPos(tmp_pt, sections[lastSection + i].x,
                                       sections[lastSection + i].y, sections[lastSection + i].z);
                    RwIm3DVertexSetRGBA(tmp_pt, 255, 255, 255, 255);
                    RwIm3DVertexSetUV(tmp_pt, u, v);
                    tmp_pt++;
                    RwIm3DVertexSetPos(tmp_pt, sections[currSection + i].x,
                                       sections[currSection + i].y, sections[currSection + i].z);
                    RwIm3DVertexSetRGBA(tmp_pt, 255, 255, 255, 255);
                    RwIm3DVertexSetUV(tmp_pt, u + ustep, v);
                    tmp_pt++;
                    numVertsSoFar += 2;
                }

                lastSection = currSection;
                currSection = currSection ? 0 : 3;
                u += ustep;
                numSections++;
            }
        }

        if (numSections > 0)
        {
            RwIm3DTransform(lnverts, numVertsSoFar, (RwMatrix*)&g_I3, 0x1b);
            RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
            RwIm3DEnd();
        }
    }
}

void zLasso_Update(zLasso* lasso, xEnt* ent, F32 dt)
{
    S32 i;
    xVec3 newPoint;
    F32 interp;

    iModelTagEval(ent->model->Data, &lasso->tag, lasso->model->Mat, &newPoint);
    xVec3Copy(&lasso->anchor, (xVec3*)&ent->model->Mat->pos);
    xVec3SubFrom(&newPoint, &lasso->anchor);

    if (!(lasso->flags & 0x800))
    {
        lasso->secsLeft -= dt;
        if (lasso->secsLeft < 0.0f)
        {
            lasso->secsLeft = 0.0f;
            lasso->flags |= 0x800;
        }

        interp = 1.0f - lasso->secsLeft / lasso->secsTotal;

        if (!(lasso->flags & 0x2000))
        {
            if (lasso->flags & 0x8)
            {
                fizzicalRadius(lasso, dt, &newPoint);
            }
            else
            {
                lasso->crRadius = lasso->stRadius * (1.0f - interp) + lasso->tgRadius * interp;
            }

            if (lasso->flags & 0x2)
            {
                fizzicalCenter(lasso, dt, &newPoint);
            }
            else
            {
                xVec3SMul(&lasso->crCenter, &lasso->stCenter, 1.0f - interp);
                xVec3AddScaled(&lasso->crCenter, &lasso->tgCenter, interp);
                xVec3SubFrom(&lasso->crCenter, &lasso->anchor);
            }

            if (lasso->flags & 0x4)
            {
                fizzicalNormal(lasso, dt, &newPoint);
            }
            else
            {
                xVec3SMul(&lasso->crNormal, &lasso->stNormal, 1.0f - interp);
                xVec3AddScaled(&lasso->crNormal, &lasso->tgNormal, interp);
            }

            xVec3SMulBy(&lasso->crNormal, 1.0f / xVec3Length(&lasso->crNormal));

            if (lasso->flags & 0x10)
            {
                fizzicalHonda(lasso, dt, &newPoint);
            }
            else
            {
                nonfizzicalHonda(lasso, dt, &newPoint);
            }
        }

        if (lasso->flags & 0x20)
        {
            fizzicalSlack(lasso, dt, &newPoint);
        }
        else
        {
            lasso->crSlack = lasso->stSlack * (1.0f - interp) + lasso->tgSlack * interp;
        }
    }
    else
    {
        if (!(lasso->flags & 0x4000))
        {
            if (lasso->flags & 0x100)
            {
                fizzicalRadius(lasso, dt, &newPoint);
            }

            if (lasso->flags & 0x40)
            {
                fizzicalCenter(lasso, dt, &newPoint);
            }

            if (lasso->flags & 0x80)
            {
                fizzicalNormal(lasso, dt, &newPoint);
                xVec3SMulBy(&lasso->crNormal, 1.0f / xVec3Length(&lasso->crNormal));
            }

            if (lasso->flags & 0x200)
            {
                fizzicalHonda(lasso, dt, &newPoint);
            }
            else
            {
                nonfizzicalHonda(lasso, dt, &newPoint);
            }
        }

        if (lasso->flags & 0x400)
        {
            fizzicalSlack(lasso, dt, &newPoint);
        }
    }

    xVec3Copy(&lasso->lastRefs[lasso->reindex[4]], &newPoint);

    lasso->reindex[0] = lasso->reindex[4];
    for (i = 1; i < 5; i++)
    {
        lasso->reindex[i] = lasso->reindex[0] + i;
        if (lasso->reindex[i] > 4)
        {
            lasso->reindex[i] -= 5;
        }
    }

    if (lasso->patTongue)
    {
        if (lasso->tipDelay < 0.25f)
        {
            lasso->tipDelay += dt;
        }
        else
        {
            lasso->tipLerp += 6.0f * dt;
            if (lasso->tipLerp > 1.0f)
            {
                lasso->tipLerp = 1.0f;
            }
        }
    }
}

void zLasso_InitTimer(zLasso* lasso, F32 interpTime)
{
    lasso->secsTotal = interpTime;
    lasso->secsLeft = interpTime;

    lasso->stRadius = lasso->tgRadius = lasso->crRadius = 0.0f;
    lasso->stSlack = lasso->tgSlack = lasso->crSlack = 0.0f;

    iModelTagEval(lasso->model->Data, &lasso->tag, lasso->model->Mat, &lasso->crCenter);

    xVec3Copy(&lasso->anchor, (xVec3*)&lasso->model->Mat->pos);
    xVec3SubFrom(&lasso->crCenter, &lasso->anchor);
    xVec3Copy(&lasso->honda, &lasso->crCenter);

    lasso->currDist = lasso->lastDist = 0.0f;

    xVec3Init(&lasso->stNormal, 0.0f, 1.0f, 0.0f);
    xVec3Init(&lasso->tgNormal, 0.0f, 1.0f, 0.0f);
    xVec3Init(&lasso->crNormal, 0.0f, 1.0f, 0.0f);

    xVec3Copy(&lasso->lastRefs[0], &lasso->crCenter);
    xVec3Copy(&lasso->lastRefs[1], &lasso->crCenter);
    xVec3Copy(&lasso->lastRefs[2], &lasso->crCenter);
    xVec3Copy(&lasso->lastRefs[3], &lasso->crCenter);
    xVec3Copy(&lasso->lastRefs[4], &lasso->crCenter);

    lasso->reindex[0] = 0;
    lasso->reindex[1] = 1;
    lasso->reindex[2] = 2;
    lasso->reindex[3] = 3;
    lasso->reindex[4] = 4;
}

void zLasso_ResetTimer(zLasso* lasso, F32 interpTime)
{
    lasso->secsTotal = interpTime;
    lasso->secsLeft = interpTime;
    lasso->tgRadius = lasso->stRadius = lasso->crRadius;
    lasso->tgSlack = lasso->stSlack = lasso->crSlack;
    xVec3Copy(&lasso->stNormal, &lasso->crNormal);
    xVec3Copy(&lasso->tgNormal, &lasso->crNormal);
    xVec3Add(&lasso->stCenter, &lasso->crCenter, &lasso->anchor);
}

static void fizzicalRadius(zLasso* lasso, F32 dt, xVec3* newPoint)
{
    lasso->crRadius -= 2.0f * (0.75f - lasso->crSlack) * dt;
    if (lasso->crRadius < 0.0f)
    {
        lasso->crRadius = 0.0f;
    }
}

static void fizzicalCenter(zLasso* lasso, F32 dt, xVec3* newPoint)
{
    xVec3AddScaled(&lasso->crCenter, &lasso->lastRefs[lasso->reindex[4]], -0.2f);
    xVec3AddScaled(&lasso->crCenter, newPoint, 0.2f);
}

static void fizzicalNormal(zLasso* lasso, F32 dt, xVec3* newPoint)
{
    xVec3 delta;

    xVec3Sub(&delta, &lasso->lastRefs[lasso->reindex[0]], &lasso->lastRefs[lasso->reindex[1]]);
    F32 dot = xVec3Dot(&lasso->crNormal, &delta);
    // The length is written out, not xVec3Length(&delta) as in BfBB: retail has this xsqrt at the
    // same inline depth as the xVec3Length(&crNormal) below.
    xVec3AddScaled(&lasso->crNormal, &delta,
                   1.1f *
                       (-dot / xsqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z)));
    xVec3Normalize(&lasso->crNormal, &lasso->crNormal);
}

static void fizzicalHonda(zLasso* lasso, F32 dt, xVec3* newPoint)
{
    xVec3SMul(&lasso->honda, newPoint, -1.0f);
    xVec3AddScaled(&lasso->honda, &lasso->lastRefs[lasso->reindex[0]], 0.571429f);
    xVec3AddScaled(&lasso->honda, &lasso->lastRefs[lasso->reindex[1]], 0.285714f);
    xVec3AddScaled(&lasso->honda, &lasso->lastRefs[lasso->reindex[2]], 0.142857f);
    xVec3AddScaled(&lasso->honda, &lasso->crNormal, -xVec3Dot(&lasso->crNormal, &lasso->honda));
    xVec3Normalize(&lasso->honda, &lasso->honda);
    xVec3SMulBy(&lasso->honda, lasso->crRadius);

    if (negativeHondaX && lasso->honda.x > 0.0f)
    {
        negativeHondaX = 0;
    }

    if (lasso->honda.x < 0.0f)
    {
        negativeHondaX = 1;
    }

    xVec3AddTo(&lasso->honda, &lasso->crCenter);
}

static void nonfizzicalHonda(zLasso* lasso, F32 dt, xVec3* newPoint)
{
    xVec3Sub(&lasso->honda, newPoint, &lasso->crCenter);
    xVec3AddScaled(&lasso->honda, &lasso->crNormal, -xVec3Dot(&lasso->honda, &lasso->crNormal));

    F32 hondaLength = xVec3Length2(&lasso->honda);
    if (hondaLength < 0.00001f)
    {
        lasso->honda.x = lasso->crNormal.y;
        lasso->honda.y = -lasso->crNormal.x;
        lasso->honda.z = 0.0f;
        hondaLength = xVec3Length2(&lasso->honda);
        xVec3SMulBy(&lasso->honda, lasso->crRadius / xsqrt(hondaLength));
        xVec3AddTo(&lasso->honda, &lasso->crCenter);
        return;
    }

    xVec3SMulBy(&lasso->honda, lasso->crRadius / xsqrt(hondaLength));
    xVec3AddTo(&lasso->honda, &lasso->crCenter);
}

static void fizzicalSlack(zLasso* lasso, F32 dt, xVec3* newPoint)
{
    lasso->crSlack += 2.0f * (lasso->lastDist - lasso->currDist) - 0.6f * dt;
    if (lasso->crSlack < 0.0f)
    {
        lasso->crSlack = 0.0f;
    }
    else if (lasso->crSlack > 1.0f)
    {
        lasso->crSlack = 1.0f;
    }
}

static void vec2vecMat(xMat4x3* m, xVec3* v1, xVec3* v2)
{
    xVec3 v3;
    xVec3Cross(&v3, v1, v2);
    F32 f1 = xasin(xVec3Normalize(&v3, &v3));
    xMat4x3Rot(m, &v3, f1);
}

void zPatTongue_Init(zLasso* lasso, xModelInstance* patmodel)
{
    S32 i;
    RwV3d* verts;
    U32 index;

    lasso->patTongue = 0;
    lasso->tipModel = (RpAtomic*)xSTFindAsset(xStrHash("pattongue_tip"), NULL);
    if (lasso->tipModel == NULL)
    {
        lasso->tipModel = (RpAtomic*)xSTFindAsset(xStrHash("pattongue_tip.dff"), NULL);
    }

    lasso->tipMatrix = (xMat4x3*)&sPatTongueTipMatrix;
    *lasso->tipMatrix = g_I3;

    if (lasso->tipModel != NULL)
    {
        lasso->patTongue = 1;

        for (i = 0; i < 12; i++)
        {
            iModelTagSetup(&sPatMouthVert[i].tag, patmodel->Data, sPatMouthVert[i].pos.x,
                           sPatMouthVert[i].pos.y, sPatMouthVert[i].pos.z);
        }

        verts = lasso->tipModel->geometry->morphTarget->verts;
        for (i = 0; i < 12; i++)
        {
            index = iModelTagSetup(&sPatTipVert[i].tag, lasso->tipModel, sPatTipVert[i].pos.x,
                                   sPatTipVert[i].pos.y, sPatTipVert[i].pos.z);
            sPatTipVert[i].tag.v.x = verts[index].x;
            sPatTipVert[i].tag.v.y = verts[index].y;
            sPatTipVert[i].tag.v.z = verts[index].z;
        }

        for (i = 0; i < 2; i++)
        {
            iModelTagSetup(&sPatTeethVert[i].tag, patmodel->Data, sPatTeethVert[i].pos.x,
                           sPatTeethVert[i].pos.y, sPatTeethVert[i].pos.z);
        }

        sLassoRaster = patmodel->Data->geometry->matList.materials[0]->texture->raster;
        iModelTagSetup(&lasso->tag, patmodel->Data, sPatMouthVert[0].pos.x, sPatMouthVert[0].pos.y,
                       sPatMouthVert[0].pos.z);
        lasso->model = patmodel;

        sPatTongueLastCount = 0;
    }
}

static void CalcSegLerp(xVec3* segOut, xVec3* segA, xVec3* segB, F32 lerp, xVec3* translate,
                        F32 scale)
{
    S32 i;

    if (lerp <= 0.0f)
    {
        memcpy(segOut, segA, 24 * sizeof(xVec3));
    }
    else if (lerp >= 1.0f)
    {
        memcpy(segOut, segB, 24 * sizeof(xVec3));
    }
    else
    {
        for (i = 0; i < 12; i++)
        {
            xVec3Lerp(&segOut[i * 2], &segA[i * 2], &segB[i * 2], lerp);
            if (translate != NULL)
            {
                segOut[i * 2] = segOut[i * 2] + *translate;
            }
        }

        if (scale != 0.0f)
        {
            // center is not in the PS2 DWARF, but the scale needs the midpoint fixed before the
            // loop overwrites segOut[0]/segOut[12].
            xVec3 center;
            xVec3Lerp(&center, &segOut[0], &segOut[12], 0.5f);

            for (i = 0; i < 12; i++)
            {
                segOut[i * 2].x = scale * (segOut[i * 2].x - center.x) + center.x;
                segOut[i * 2].y = scale * (segOut[i * 2].y - center.y) + center.y;
                segOut[i * 2].z = scale * (segOut[i * 2].z - center.z) + center.z;
            }
        }
    }
}

static inline void AddTriNormal(xVec3* normsum, xVec3* center, xVec3* a, xVec3* b)
{
    if (a != NULL && b != NULL)
    {
        xVec3 da;
        xVec3 db;
        xVec3 norm;

        xVec3Sub(&da, a, center);
        xVec3Sub(&db, b, center);
        xVec3Cross(&norm, &da, &db);
        xVec3Normalize(&norm, &norm);
        xVec3AddTo(normsum, &norm);
    }
}

static void CalcSegNormals(xVec3* segA, xVec3* segB, xVec3* segC)
{
    S32 i;
    xVec3 normsum;
    xVec3* vright;
    xVec3* vleft;

    for (i = 0; i < 12; i++)
    {
        xVec3* vcur = &segB[i * 2];
        vleft = &segB[((i + 11) % 12) * 2];
        vright = &segB[((i + 1) % 12) * 2];
        xVec3* vup = segA ? &segA[i * 2] : NULL;
        xVec3* vdown = segC ? &segC[i * 2] : NULL;

        normsum.x = 0.0f;
        normsum.y = 0.0f;
        normsum.z = 0.0f;
        AddTriNormal(&normsum, vcur, vright, vdown);
        AddTriNormal(&normsum, vcur, vdown, vleft);
        AddTriNormal(&normsum, vcur, vleft, vup);
        AddTriNormal(&normsum, vcur, vup, vright);

        xVec3Normalize(&segB[i * 2 + 1], &normsum);
    }
}

// Dead-stripped placeholder: retail pools 2*PI here, from a stripped function after
// IMLightKitSetup. Body unknown.
static void __deadstripped_zLasso(F32* out)
{
    out[0] = 2.0f * PI;
}

static void IMLightKitSetup(IMLightKit* imlkit, xLightKit* lkit)
{
    U32 i;
    U32 numdir;
    F32* lightdir;
    F32* extraLightdir;
    xLightKitLight* light;
    RwMatrixTag* mat;

    numdir = 0;
    memset(imlkit, 0, sizeof(IMLightKit));
    lightdir = &imlkit->lightdir[0].x;
    extraLightdir = &imlkit->extraLightdir[0].x;

    for (i = 0; i < lkit->lightCount; i++)
    {
        light = &lkit->lightList[i];
        switch (light->type)
        {
        case 1:
            imlkit->lightcol[4].x += 255.0f * light->color.red;
            imlkit->lightcol[4].y += 255.0f * light->color.green;
            imlkit->lightcol[4].z += 255.0f * light->color.blue;
            break;
        case 2:
            mat = RwFrameGetMatrix(RpLightGetFrame(light->platLight));
            if (numdir < 4)
            {
                lightdir[0] = -mat->at.x;
                lightdir[4] = -mat->at.y;
                lightdir[8] = -mat->at.z;
                lightdir++;
                imlkit->lightcol[numdir].x = 255.0f * light->color.red;
                imlkit->lightcol[numdir].y = 255.0f * light->color.green;
                imlkit->lightcol[numdir].z = 255.0f * light->color.blue;
                numdir++;
            }
            else if (numdir < 8)
            {
                imlkit->extraLights++;
                extraLightdir[0] = -mat->at.x;
                extraLightdir[4] = -mat->at.y;
                extraLightdir[8] = -mat->at.z;
                extraLightdir++;
                imlkit->extraLightcol[numdir - 4].x = 255.0f * light->color.red;
                imlkit->extraLightcol[numdir - 4].y = 255.0f * light->color.green;
                imlkit->extraLightcol[numdir - 4].z = 255.0f * light->color.blue;
                numdir++;
            }
            break;
        }
    }
}

// lightdir/extraLightdir hold up to four directional lights transposed (one light per xVec4
// component), so the four per-light dot products are kept in an xVec4.
static void IMLightKitLight(IMLightKit* imlkit, xVec3* norm, U8* r, U8* g, U8* b)
{
    F32 red;
    F32 grn;
    F32 blu;
    xVec4 dot;

    dot.x = imlkit->lightdir[0].x * norm->x + imlkit->lightdir[1].x * norm->y +
            imlkit->lightdir[2].x * norm->z;
    dot.y = imlkit->lightdir[0].y * norm->x + imlkit->lightdir[1].y * norm->y +
            imlkit->lightdir[2].y * norm->z;
    dot.z = imlkit->lightdir[0].z * norm->x + imlkit->lightdir[1].z * norm->y +
            imlkit->lightdir[2].z * norm->z;
    dot.w = imlkit->lightdir[0].w * norm->x + imlkit->lightdir[1].w * norm->y +
            imlkit->lightdir[2].w * norm->z;

    dot.x = (dot.x > 0.0f) ? dot.x : 0.0f;
    dot.y = (dot.y > 0.0f) ? dot.y : 0.0f;
    dot.z = (dot.z > 0.0f) ? dot.z : 0.0f;
    dot.w = (dot.w > 0.0f) ? dot.w : 0.0f;

    red = imlkit->lightcol[4].x + (imlkit->lightcol[0].x * dot.x + imlkit->lightcol[1].x * dot.y +
                                   imlkit->lightcol[2].x * dot.z + imlkit->lightcol[3].x * dot.w);
    grn = imlkit->lightcol[4].y + (imlkit->lightcol[0].y * dot.x + imlkit->lightcol[1].y * dot.y +
                                   imlkit->lightcol[2].y * dot.z + imlkit->lightcol[3].y * dot.w);
    blu = imlkit->lightcol[4].z + (imlkit->lightcol[0].z * dot.x + imlkit->lightcol[1].z * dot.y +
                                   imlkit->lightcol[2].z * dot.z + imlkit->lightcol[3].z * dot.w);

    if (imlkit->extraLights != 0)
    {
        dot.x = imlkit->extraLightdir[0].x * norm->x + imlkit->extraLightdir[1].x * norm->y +
                imlkit->extraLightdir[2].x * norm->z;
        dot.y = imlkit->extraLightdir[0].y * norm->x + imlkit->extraLightdir[1].y * norm->y +
                imlkit->extraLightdir[2].y * norm->z;
        dot.z = imlkit->extraLightdir[0].z * norm->x + imlkit->extraLightdir[1].z * norm->y +
                imlkit->extraLightdir[2].z * norm->z;
        dot.w = imlkit->extraLightdir[0].w * norm->x + imlkit->extraLightdir[1].w * norm->y +
                imlkit->extraLightdir[2].w * norm->z;

        dot.x = (dot.x > 0.0f) ? dot.x : 0.0f;
        dot.y = (dot.y > 0.0f) ? dot.y : 0.0f;
        dot.z = (dot.z > 0.0f) ? dot.z : 0.0f;
        dot.w = (dot.w > 0.0f) ? dot.w : 0.0f;

        red += imlkit->extraLightcol[0].x * dot.x + imlkit->extraLightcol[1].x * dot.y +
               imlkit->extraLightcol[2].x * dot.z + imlkit->extraLightcol[3].x * dot.w;
        grn += imlkit->extraLightcol[0].y * dot.x + imlkit->extraLightcol[1].y * dot.y +
               imlkit->extraLightcol[2].y * dot.z + imlkit->extraLightcol[3].y * dot.w;
        blu += imlkit->extraLightcol[0].z * dot.x + imlkit->extraLightcol[1].z * dot.y +
               imlkit->extraLightcol[2].z * dot.z + imlkit->extraLightcol[3].z * dot.w;
    }

    *r = (red < 255.0f) ? red : 255.0f;
    *g = (grn < 255.0f) ? grn : 255.0f;
    *b = (blu < 255.0f) ? blu : 255.0f;
}

// No symbol (inlined; the name is ours). The PS2 line table has the whole normadjust loop body on
// one line, and retail's registers treat the clamp and the LERP operands as an inline's values.
// 0.7853982f/5.497787f fold to retail's 4.712389f range (0.25f/1.75f * PI is one ulp off).
static inline F32 TongueSegScale(F32 frac, F32 tongueScale)
{
    F32 normadjust = (icos(LERP(frac, 0.7853982f, 5.497787f)) + 1.0f) / 1.7071068f;
    normadjust = CLAMP(normadjust, 0.0f, 1.0f);
    return tongueScale * (1.0f - normadjust) + normadjust;
}

// tipcross/teethToMouth are not in the PS2 DWARF, but retail has two xVec3 stack slots for them
// below mouthToTeeth. cross keeps the first cross product while inside_normvec is built from it.
static void zPatTongue_Render(zLasso* lasso)
{
    RxObjSpace3DVertex* currvert;
    IMLightKit imlkit;
    S32 i;
    xVec3 tippos;
    xVec3 tiptag;
    xVec3 teethtag;
    xVec3 mouthtag;
    xVec3 ignoreNorm;
    xVec3 tonguevec;
    xVec3 mouthvec;
    F32 totaldist;
    F32 tipsin;
    F32 tipangle;
    xMat4x3* tipmat;
    xVec3 hitchpos;
    xVec3 mouthToTeeth;
    xVec3 tipcross;
    xVec3 teethToMouth;
    xMat3x3 rotmat;
    F32 distLerp;
    S32 numsegments;
    xVec3 seglinear[13];
    F32 seglerp[13];
    F32 segscale[13];
    F32 invdistsqr;
    F32 tongueScale;
    F32 slope;
    F32 normadjust;
    xVec3 inside_normvec;
    xVec3 tangent_vec;
    S32 currsegidx;
    xVec3 start_xsect[24];
    xVec3 end_xsect[24];
    S32 seg;
    xVec3 xsectbuf[4][24];
    xVec3* curr_xsect[4];
    RxObjSpace3DVertex* xsectStartVert;
    xVec3* xsectA;
    xVec3* xsectB;
    U8 color[4];
    xVec3* tmpxsect;

    currvert = lnverts;
    IMLightKitSetup(&imlkit, lasso->model->LightKit);
    tipmat = lasso->tipMatrix;

    iModelTagEval(lasso->model->Data, &sPatMouthVert[0].tag, lasso->model->Mat, &mouthtag,
                  &ignoreNorm);
    iModelTagEval(lasso->model->Data, &sPatTeethVert[0].tag, lasso->model->Mat, &teethtag,
                  &ignoreNorm);
    iModelTagEval(lasso->tipModel, &sPatTipVert[0].tag, (RwMatrixTag*)&g_I3, &tiptag, &ignoreNorm);

    if (globals.player.lassoInfo.swingTarget == NULL)
    {
        return;
    }

    xMat4x3Toworld(&hitchpos, (xMat4x3*)globals.player.lassoInfo.swingTarget->model->Mat,
                   &sPatHitchPos);

    if (lasso->tipLerp == 0.0f)
    {
        return;
    }

    mouthToTeeth = teethtag - mouthtag;
    xVec3Normalize(&mouthToTeeth, &mouthToTeeth);
    tippos.x = mouthtag.x + 0.5f * mouthToTeeth.x;
    tippos.y = mouthtag.y + 0.5f * mouthToTeeth.y;
    tippos.z = mouthtag.z + 0.5f * mouthToTeeth.z;
    xVec3Lerp(&tippos, &tippos, &hitchpos, lasso->tipLerp);

    tonguevec = tippos - teethtag;
    totaldist = xVec3Length(&tonguevec);
    if (totaldist < 0.00001f)
    {
        return;
    }

    tipsin = tiptag.y / totaldist;
    if (tipsin > 1.0f || tipsin < -1.0f)
    {
        return;
    }

    tipangle = xasin(tipsin);
    xMat3x3RotC(&rotmat, lasso->model->Mat->right.x, lasso->model->Mat->right.y,
                lasso->model->Mat->right.z, -tipangle);
    xMat3x3RMulVec(&tonguevec, &rotmat, &tonguevec);

    mouthvec = tippos - mouthtag;
    totaldist = xVec3Length(&mouthvec);
    if (totaldist < 0.00001f)
    {
        return;
    }

    tipsin = tiptag.y / totaldist;
    if (tipsin > 1.0f || tipsin < -1.0f)
    {
        return;
    }

    tipangle = xasin(tipsin);
    xMat3x3RotC(&rotmat, lasso->model->Mat->right.x, lasso->model->Mat->right.y,
                lasso->model->Mat->right.z, -tipangle);
    xMat3x3RMulVec(&mouthvec, &rotmat, &mouthvec);

    xVec3Cross(&tipcross, &tonguevec, &mouthvec);
    if (xVec3Dot(&tipcross, (xVec3*)&lasso->model->Mat->right) < 0.0f)
    {
        // The tip would bend back past the mouth: aim it along the mouth vector and pull the
        // teeth point along the mouth-tip line.
        distLerp = xVec3Dist(&mouthtag, &teethtag);
        totaldist = xVec3Length(&tonguevec);
        distLerp = distLerp / (totaldist + distLerp);

        xVec3Normalize(&tipmat->at, &mouthvec);
        tipmat->right = *(xVec3*)&lasso->model->Mat->right;
        xVec3Cross(&tipmat->up, &tipmat->at, &tipmat->right);
        xVec3Normalize(&tipmat->up, &tipmat->up);
        xVec3Cross(&tipmat->right, &tipmat->up, &tipmat->at);
        lasso->tipMatrix->pos = tippos;
        iModelTagEval(lasso->tipModel, &sPatTipVert[0].tag, (RwMatrixTag*)lasso->tipMatrix, &tiptag,
                      &ignoreNorm);

        xVec3Lerp(&teethtag, &mouthtag, &tiptag, distLerp);
    }
    else
    {
        xVec3Normalize(&tipmat->at, &tonguevec);
        tipmat->right = *(xVec3*)&lasso->model->Mat->right;
        xVec3Cross(&tipmat->up, &tipmat->at, &tipmat->right);
        xVec3Normalize(&tipmat->up, &tipmat->up);
        xVec3Cross(&tipmat->right, &tipmat->up, &tipmat->at);
        lasso->tipMatrix->pos = tippos;
        iModelTagEval(lasso->tipModel, &sPatTipVert[0].tag, (RwMatrixTag*)lasso->tipMatrix, &tiptag,
                      &ignoreNorm);
    }

    xLightKit_Enable(lasso->model->LightKit, xglobals->currWorld);
    iModelWorldBoundSphere(lasso->tipModel, (RwMatrixTag*)lasso->tipMatrix, NULL);
    iModelRender(lasso->tipModel, (RwMatrix*)lasso->tipMatrix);
    xLightKit_Enable(NULL, xglobals->currWorld);

    // Lay the tongue out along the mouth -> teeth -> tip line.
    tonguevec = tiptag - mouthtag;
    invdistsqr = 1.0f / tonguevec.length2();

    seglinear[0] = seglinear[1] = mouthtag;
    xVec3Lerp(&seglinear[2], &mouthtag, &teethtag, 0.5f);
    seglinear[3] = teethtag;
    segscale[0] = segscale[1] = 1.0f;

    totaldist = xVec3Dist(&teethtag, &tiptag);
    if (totaldist < 0.3f * 2)
    {
        numsegments = 4;
    }
    else if (totaldist < 0.3f * 3)
    {
        numsegments = 5;
    }
    else if (totaldist < 0.3f * 4)
    {
        numsegments = 6;
    }
    else if (totaldist < 0.3f * 5)
    {
        numsegments = 7;
    }
    else if (totaldist < 0.3f * 6)
    {
        numsegments = 8;
    }
    else if (totaldist < 0.3f * 7)
    {
        numsegments = 9;
    }
    else
    {
        numsegments = 10;
    }

    for (i = 3; i < numsegments; i++)
    {
        xVec3Lerp(&seglinear[i + 1], &teethtag, &tiptag, (F32)(i - 2) / (F32)(numsegments - 2));
    }

    seglinear[numsegments + 1] = seglinear[numsegments + 2] = tiptag;
    segscale[numsegments + 1] = segscale[numsegments + 2] = 1.0f;

    tongueScale = (totaldist - 1.75f) / 1.75f;

    tongueScale = xlerp(0.8f, 0.45f, CLAMP(tongueScale, 0.0f, 1.0f));
    for (i = 1; i < numsegments; i++)
    {
        segscale[i + 1] = TongueSegScale((F32)i / (F32)numsegments, tongueScale);
    }

    // Store each point as its offset from the straight mouth -> tip line.
    for (i = 0; i < numsegments + 3; i++)
    {
        seglerp[i] = invdistsqr * (tonguevec.x * (seglinear[i].x - mouthtag.x) +
                                   tonguevec.y * (seglinear[i].y - mouthtag.y) +
                                   tonguevec.z * (seglinear[i].z - mouthtag.z));
        if (seglerp[i] > 0.999f)
        {
            seglerp[i] = 1.0f;
        }
        if (seglerp[i] < 0.001f)
        {
            seglerp[i] = 0.0f;
        }
        // not in the DWARF (a struct local promoted to registers)
        xVec3 linpos;
        xVec3Lerp(&linpos, &mouthtag, &tiptag, seglerp[i]);
        seglinear[i] -= linpos;
    }

    // Push the point between mouth and teeth inwards so the tongue curls over the teeth.
    teethToMouth = mouthtag - teethtag;
    tangent_vec = teethtag - tiptag;
    xVec3Normalize(&tangent_vec, &tangent_vec);
    totaldist = teethToMouth.length();
    if (totaldist > 0.00001f)
    {
        xVec3 cross;

        // '*= 1.0f / totaldist' (a temporary reciprocal), not operator/= (its local i): retail
        // colours 1/totaldist after the x/y/z loads
        teethToMouth *= 1.0f / totaldist;
        xVec3Cross(&cross, &teethToMouth, &tangent_vec);
        if (xVec3Length(&cross) > 0.00001f)
        {
            xVec3Cross(&inside_normvec, &cross, &teethToMouth);
            xVec3Normalize(&inside_normvec, &inside_normvec);

            distLerp = xVec3Dot(&teethToMouth, &tangent_vec);
            slope = xVec3Dot(&inside_normvec, &tangent_vec);
            if (slope > distLerp)
            {
                slope = 1.0f;
            }
            else if (slope < 0.00001f)
            {
                slope = 0.0f;
            }
            else
            {
                slope = slope / distLerp;
            }

            normadjust = 0.25f * (slope * totaldist);
            seglinear[2].x += normadjust * inside_normvec.x;
            seglinear[2].y += normadjust * inside_normvec.y;
            seglinear[2].z += normadjust * inside_normvec.z;
        }
    }

    for (i = 0; i < 12; i++)
    {
        iModelTagEval(lasso->model->Data, &sPatMouthVert[i].tag, lasso->model->Mat,
                      &start_xsect[i * 2], &start_xsect[i * 2 + 1]);
    }

    for (i = 0; i < 12; i++)
    {
        iModelTagEval(lasso->tipModel, &sPatTipVert[i].tag, (RwMatrixTag*)lasso->tipMatrix,
                      &end_xsect[i * 2], &end_xsect[i * 2 + 1]);
    }

    for (seg = 0; seg < 3; seg++)
    {
        curr_xsect[seg] = xsectbuf[seg];
        CalcSegLerp(curr_xsect[seg], start_xsect, end_xsect, seglerp[seg], &seglinear[seg],
                    segscale[seg]);
    }
    curr_xsect[3] = xsectbuf[3];

    CalcSegNormals(NULL, curr_xsect[1], curr_xsect[2]);

    xVec3Lerp(&sPatTongueLastVec[0], &curr_xsect[1][0], &curr_xsect[1][6], 0.5f);
    sPatTongueLastCount = 1;
    sPatTongueLastRight = *(xVec3*)&lasso->model->Mat->right;

    currsegidx = 3;
    for (seg = 0; seg < numsegments; seg++)
    {
        CalcSegLerp(curr_xsect[3], start_xsect, end_xsect, seglerp[currsegidx],
                    &seglinear[currsegidx], segscale[currsegidx]);
        currsegidx++;
        if (seg != numsegments - 1)
        {
            CalcSegNormals(curr_xsect[1], curr_xsect[2], curr_xsect[3]);
        }

        xsectStartVert = currvert;
        xsectA = curr_xsect[1];
        xsectB = curr_xsect[2];
        for (i = 0; i < 12; i++)
        {
            IMLightKitLight(&imlkit, &xsectA[1], &color[0], &color[1], &color[2]);
            RwIm3DVertexSetPos(currvert, xsectA->x, xsectA->y, xsectA->z);
            RwIm3DVertexSetRGBA(currvert, color[0], color[1], color[2], 255);
            RwIm3DVertexSetUV(currvert, 0.95f, 0.05f);
            currvert++;

            IMLightKitLight(&imlkit, &xsectB[1], &color[0], &color[1], &color[2]);
            RwIm3DVertexSetPos(currvert, xsectB->x, xsectB->y, xsectB->z);
            RwIm3DVertexSetRGBA(currvert, color[0], color[1], color[2], 255);
            RwIm3DVertexSetUV(currvert, 0.95f, 0.05f);
            currvert++;

            xsectA += 2;
            xsectB += 2;
        }

        // Close the strip around the tongue.
        *currvert++ = xsectStartVert[0];
        *currvert++ = xsectStartVert[1];

        // The trail point is recorded before the rotation (as in the PS2 line table), so from
        // curr_xsect[2].
        xVec3Lerp(&sPatTongueLastVec[sPatTongueLastCount], &curr_xsect[2][0], &curr_xsect[2][6],
                  0.5f);
        sPatTongueLastCount++;

        tmpxsect = curr_xsect[0];
        curr_xsect[0] = curr_xsect[1];
        curr_xsect[1] = curr_xsect[2];
        curr_xsect[2] = curr_xsect[3];
        curr_xsect[3] = tmpxsect;
    }

    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, sLassoRaster);
    RwIm3DTransform(lnverts, currvert - lnverts, (RwMatrix*)&g_I3, 0x1b);
    RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
    RwIm3DEnd();
}

void zPatTongue_PopOff()
{
    F32 totalLen;
    F32 actualDensity;
    S32 i;
    S32 max_emit;
    xVec3* buffer;
    xVec3* loc;
    xVec3* vel;
    S32 emitted;
    F32 segEmit;
    F32 rightRand;

    totalLen = 0.0f;
    actualDensity = 20.0f;
    max_emit = zParPTankBubblesAvailable();

    for (i = 0; i < sPatTongueLastCount - 1; i++)
    {
        totalLen += xVec3Dist(&sPatTongueLastVec[i], &sPatTongueLastVec[i + 1]);
    }

    if (totalLen < 0.1f)
    {
        return;
    }

    segEmit = std::floorf(actualDensity * totalLen);
    if (segEmit > max_emit)
    {
        actualDensity = max_emit / totalLen;
    }
    else
    {
        max_emit = segEmit;
    }

    buffer = (xVec3*)xMemPushTemp(max_emit * 2 * sizeof(xVec3));
    if (buffer == NULL)
    {
        return;
    }

    loc = buffer;
    vel = buffer + max_emit;
    emitted = 0;
    segEmit = 0.0f;

    for (i = 0; i < sPatTongueLastCount - 1; i++)
    {
        // The DWARF has no segment-length local: totalLen (dead after the density calculation) is
        // reused.
        totalLen = xVec3Dist(&sPatTongueLastVec[i], &sPatTongueLastVec[i + 1]);
        segEmit += totalLen * actualDensity;

        while (segEmit > 1.0f)
        {
            rightRand = 0.12f * (xurand() - 0.5f);
            xVec3Lerp(loc, &sPatTongueLastVec[i], &sPatTongueLastVec[i + 1], xurand());
            loc->x += rightRand * sPatTongueLastRight.x + 0.03f * (xurand() - 0.5f);
            loc->y += rightRand * sPatTongueLastRight.y + 0.03f * (xurand() - 0.5f);
            loc->z += rightRand * sPatTongueLastRight.z + 0.03f * (xurand() - 0.5f);

            vel->x = 0.5f * (xurand() - 0.5f);
            vel->y = 0.5f * (xurand() - 0.5f);
            vel->z = 0.5f * (xurand() - 0.5f);

            emitted++;
            loc++;
            vel++;

            if (emitted >= max_emit)
            {
                // goto: retail leaves both loops here without re-testing
                goto spawn;
            }

            segEmit -= 1.0f;
        }
    }

spawn:
    if (emitted == 0)
    {
        return;
    }

    zParPTankSpawnBubbles(buffer, buffer + max_emit, emitted, 0.5f);
    xMemPopTemp(buffer);
    sPatTongueLastCount = 0;
}
