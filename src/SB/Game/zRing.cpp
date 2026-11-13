#include "zRing.h"

#include "zBase.h"
#include "zGame.h"
#include "zParPTank.h"
#include "zScene.h"

#include "xCollide.h"
#include "xCollideFast.h"
#include "xEnt.h"
#include "xMath.h"
#include "xModelBucket.h"
#include "xShadow.h"
#include "xString.h"
#include "xstransvc.h"
#include "xVec3.h"

#include "iModel.h"
#include "xRand.h"

xVec3 zRing::bubblePos[300];
xVec3 zRing::bubbleVel[300];
RwRGBAReal zRing::warningColor;
RwRGBAReal zRing::normalColor;
RwRaster* zRing::ringShadowRaster;
U8 zRing::applyNormalColor;

// Reconstructed helper (no symbol, no DWARF entry) for the colour block of ChangeRingColor and
// Show; Render repeats it inline (see there). The original may have repeated it inline too.
static inline void zRingSetModelColor(zRing* ring, const RwRGBAReal& color)
{
    // declared in ARGB order (RGBA order swaps two registers against retail)
    U8 alpha, red, green, blue;

    red = color.red * 255.0f;
    blue = color.blue * 255.0f;
    green = color.green * 255.0f;
    alpha = color.alpha * 255.0f;
    RpGeometry* geom = RpAtomicGetGeometry(ring->model);

    geom->flags |= rpGEOMETRYMODULATEMATERIALCOLOR;

    RpMaterial* material = RpGeometryGetMaterial(geom, 0);
    material->color.red = red;
    material->color.green = green;
    material->color.blue = blue;
    material->color.alpha = alpha;

    RpAtomicGetGeometry(ring->model)->flags |= rpGEOMETRYMODULATEMATERIALCOLOR;
}

void zRing::Load(xBase& data, xDynAsset& asset, size_t)
{
    xBaseInit(&data, &asset);
    data.baseType = eBaseTypeRing;
    ((zRing&)data).ringAsset = (zRingType*)&asset;
    data.eventFunc = NULL;
    if (data.linkCount != 0)
    {
        data.link = (xLinkAsset*)((zRingType*)&asset + 1);
    }

    ((zRing&)data).Reset();
}

void zRing::Reset()
{
    RwTexture* tex;

    xMat3x3Euler(&matrix, &ringAsset->direction);
    matrix.pos = ringAsset->location;
    // not DWARF locals: x and y give retail's register order for the scale components (z is
    // loaded first), as in the PS2 demo build
    F32 x = ringAsset->scale.x;
    F32 y = ringAsset->scale.y;
    xMat3x3MulScaleC(&matrix, &matrix, x, y, ringAsset->scale.z);

    offset = ringAsset->offset;
    triggerBoundsType = (eRingBoundType)(ringAsset->triggerBoundsType != 0);
    warningTime = ringAsset->warningTime;
    timeOut = ringAsset->timeOut;

    if (triggerBoundsType == eRingBoundTypePlane)
    {
        boundingBox.lower.x = offset.x - ringAsset->width / 2.0f;
        boundingBox.lower.y = offset.y - ringAsset->height / 2.0f;
        boundingBox.lower.z = offset.z - 0.05f;

        boundingBox.upper.x = offset.x + ringAsset->width / 2.0f;
        boundingBox.upper.y = offset.y + ringAsset->height / 2.0f;
        boundingBox.upper.z = offset.z + 0.05f;
    }
    else
    {
        boundingSphere.center = offset;
        boundingSphere.r = ringAsset->radius * ringAsset->scale.x;
    }

    hasPlayerCrossed = 0;
    elapsedTime = 0.0f;
    state = eRingIdle;
    lastSpawnTime = 0.0f;
    currentPos = 0;
    xBaseDisable(this);

    warningColor.red = 0.9f;
    warningColor.blue = 0.25f;
    warningColor.green = 0.1f;
    warningColor.alpha = 1.0f;

    normalColor.red = normalColor.green = normalColor.blue = normalColor.alpha = 1.0f;

    if (ringAsset->drivenByID != 0)
    {
        drivenBy = (xEnt*)zSceneFindObject(ringAsset->drivenByID);
        if (drivenBy != NULL)
        {
            if (drivenBy->frame != NULL)
            {
                matrix = drivenBy->frame->mat;
            }
            else if (drivenBy->model->Mat != NULL)
            {
                matrix = *(xMat4x3*)drivenBy->model->Mat;
            }
        }
    }

    tex = (RwTexture*)xSTFindAsset(xStrHash("shadow_round"), NULL);
    if (tex != NULL)
    {
        ringShadowRaster = tex->raster;
    }
}

void zRing::CalculateBubbleOffsets()
{
    RpGeometry* geom = RpAtomicGetGeometry(model);
    S32 size = geom->numTriangles;
    RpMorphTarget* mt = geom->morphTarget;
    xVec3* vert = (xVec3*)mt->verts;
    xVec3* norm;
    RpTriangle* tri = geom->triangles;
    S32 i;
    S32 j;
    S32 bubblesPerFace;
    RpTriangle* myTri;
    F32 u;
    F32 v;
    F32 w;
    xVec3 pos;

    if (zParPTankBubblesAvailable() < size * 10)
    {
        bubblesPerFace = zParPTankBubblesAvailable() / size;
    }
    else
    {
        bubblesPerFace = 10;
    }

    if (bubblesPerFace * size > 300)
    {
        bubblesPerFace = 300 / size;
    }

    bubbleCount = 0;

    for (i = 0; i < size; i++)
    {
        myTri = &tri[i];

        for (j = 0; j < bubblesPerFace; j++)
        {
            u = xurand();
            v = xurand();

            if (u + v >= 1.0f)
            {
                u = 1.0f - u;
                v = 1.0f - v;
            }

            w = 1.0f - u - v;

            pos.x = u * vert[myTri->vertIndex[0]].x + v * vert[myTri->vertIndex[1]].x +
                    w * vert[myTri->vertIndex[2]].x;
            pos.y = u * vert[myTri->vertIndex[0]].y + v * vert[myTri->vertIndex[1]].y +
                    w * vert[myTri->vertIndex[2]].y;
            pos.z = u * vert[myTri->vertIndex[0]].z + v * vert[myTri->vertIndex[1]].z +
                    w * vert[myTri->vertIndex[2]].z;

            xMat4x3Toworld(&bubblePos[bubbleCount], &matrix, &pos);

            bubbleVel[bubbleCount].x = 3.0f * (xurand() - 0.5f);
            bubbleVel[bubbleCount].y = 3.0f * (xurand() - 0.5f);
            bubbleVel[bubbleCount].z = 3.0f * (xurand() - 0.5f);
            bubbleCount++;
        }
    }
}

void zRing::ChangeRingColor(F32 dt, xModelInstance* modelInstance)
{
    F32 timeRemaining = timeOut - elapsedTime;
    xModelInstance* minst;

    if (timeRemaining > 0.0f && state == eRingWarning)
    {
        // not a DWARF local: a named quotient keeps retail's operand order
        F32 fadeRate = warningTime / timeRemaining;
        warningColor.red -= fadeRate * dt;
        if (warningColor.red <= 0.39215687f)
        {
            warningColor.red = 1.0f;
        }

        if (modelInstance == NULL)
        {
            zRingSetModelColor(this, warningColor);
        }
        else
        {
            for (minst = modelInstance; minst != NULL; minst = minst->Next)
            {
                minst->RedMultiplier = warningColor.red;
                minst->BlueMultiplier = warningColor.blue;
                minst->GreenMultiplier = warningColor.green;
                minst->Alpha = warningColor.alpha;
            }
        }
    }
}

void zRing::DrawShadow(xMat4x3* Mat, RwRaster* rwras, F32 factor, F32 distance)
{
    xVec3 center;
    xMat4x3 matrix;
    xShadowCache cache;
    S32 i;
    xEnt* ep;

    xVec3Copy(&center, &Mat->pos);
    xVec3AddScaled(&center, &Mat->up, 2.0f);
    xVec3AddScaled(&center, &Mat->at, distance);

    xVec3Copy(&matrix.pos, &center);
    xVec3Copy(&matrix.right, &Mat->right);
    xVec3Copy(&matrix.up, &Mat->at);
    xVec3Inv(&matrix.at, &Mat->up);

    if (triggerBoundsType == eRingBoundTypeCircle)
    {
        xVec3SMulBy(&matrix.right, boundingSphere.r);
    }
    else
    {
        xVec3SMulBy(&matrix.right, ringAsset->width / 2.0f);
    }

    xVec3SMulBy(&matrix.right, 1.0f);
    xVec3SMulBy(&matrix.up, 1.0f);
    xVec3SMulBy(&matrix.at, 1.0f);
    matrix.flags = 0;

    gShadowObjectRadius = 0.5f * factor + 0.9f;
    xShadowVertical_FillCache(&cache, &center, gShadowObjectRadius, 6.0f, 0.65f);
    xShadowVertical_DrawCache(&cache, factor, 0.0f, 1, (RwMatrixTag*)&matrix, rwras);

    for (i = 0; i < cache.entCount; i++)
    {
        ep = cache.ent[i];
        if (xShadowReceiveShadowSetup(ep))
        {
            xShadowReceiveShadow(ep, factor, 1, (RwMatrixTag*)&matrix, rwras);
        }
    }
}

void zRing::Update(F32 dt, xModelInstance* modelInstance)
{
    U32 count;

    switch (state)
    {
    case eRingIdle:
        break;
    case eRingWarning:
        if (elapsedTime < timeOut)
        {
            count = bubbleCount * (elapsedTime - lastSpawnTime);
            ChangeRingColor(dt, modelInstance);

            if (currentPos + count > bubbleCount)
            {
                zParPTankSpawnBubbles(bubblePos + bubbleCount - currentPos, bubbleVel,
                                      bubbleCount - currentPos, 1.0f);
                currentPos = currentPos + count - bubbleCount;
                zParPTankSpawnBubbles(bubblePos, bubbleVel, currentPos, 1.0f);
            }
            else
            {
                zParPTankSpawnBubbles(bubblePos + currentPos, bubbleVel, count, 1.0f);
            }

            currentPos += count;
            lastSpawnTime = elapsedTime;
            elapsedTime += dt;
        }
        else
        {
            state = eRingIdle;
            hasPlayerCrossed = 0;
        }
        break;
    case eRingRun:
        if (elapsedTime < timeOut)
        {
            elapsedTime += dt;
            if (elapsedTime >= timeOut - warningTime)
            {
                state = eRingWarning;
                lastSpawnTime = elapsedTime;
            }
        }
        else
        {
            state = eRingIdle;
            hasPlayerCrossed = 0;
        }
        break;
    }
}

// The colour block is written out here rather than calling zRingSetModelColor (the PS2 line table
// gives it its own lines in Render); its locals are not in the PS2 DWARF.
void zRing::Render(xModelInstance* modelInstance)
{
    S32 cull;
    xSphere* worldSphere;
    xModelInstance* minst;

    if (drivenBy != NULL)
    {
        if (drivenBy->frame != NULL)
        {
            matrix = drivenBy->frame->mat;
        }
        else if (drivenBy->model->Mat != NULL)
        {
            matrix = *(xMat4x3*)drivenBy->model->Mat;
        }
    }

    if (modelInstance == NULL)
    {
        worldSphere = iModelWorldBoundSphere(model, (RwMatrixTag*)&matrix, NULL);
        cull = xModelSphereCull(worldSphere);
        if (cull)
        {
            return;
        }

        if (surface != NULL)
        {
            xEntSetupPipeline(surface, model);
        }

        if (applyNormalColor)
        {
            RpGeometry* geom;
            RpMaterial* material;
            U8 red = normalColor.red * 255.0f;
            U8 blue = normalColor.blue * 255.0f;
            U8 green = normalColor.green * 255.0f;
            U8 alpha = normalColor.alpha * 255.0f;

            geom = RpAtomicGetGeometry(model);
            geom->flags |= rpGEOMETRYMODULATEMATERIALCOLOR;

            material = RpGeometryGetMaterial(geom, 0);
            material->color.red = red;
            material->color.green = green;
            material->color.blue = blue;
            material->color.alpha = alpha;

            RpAtomicGetGeometry(model)->flags |= rpGEOMETRYMODULATEMATERIALCOLOR;
            applyNormalColor = 0;
        }

        if (model != NULL)
        {
            iModelRender(model, (RwMatrixTag*)&matrix);
        }
    }
    else
    {
        for (minst = modelInstance; minst != NULL; minst = minst->Next)
        {
            minst->Surf = surface;
            *(xMat4x3*)minst->Mat = matrix;
            xModelBucket_Add(minst);
        }
    }

    DrawShadow(&matrix, ringShadowRaster, 0.4f, 0.0f);
}

// No symbol in TSSM: always inlined into zRingControl::ShowNextRings.
inline void zRing::Show(xModelInstance* modelInstance)
{
    xModelInstance* minst;

    if (!xBaseIsEnabled(this))
    {
        xBaseEnable(this);
        CalculateBubbleOffsets();
        zParPTankSpawnBubbles(bubblePos, bubbleVel, bubbleCount, 1.0f);

        if (model != NULL && modelInstance == NULL)
        {
            zRingSetModelColor(this, normalColor);
        }
        else
        {
            for (minst = modelInstance; minst != NULL; minst = minst->Next)
            {
                minst->RedMultiplier = normalColor.red;
                minst->BlueMultiplier = normalColor.blue;
                minst->GreenMultiplier = normalColor.green;
                minst->Alpha = normalColor.alpha;
            }
        }
    }
}

// lastDist, curDist and hit are not PS2 DWARF locals; retail evaluates both plane distances
// before the first compare.
U8 zRing::CheckIfPlayerCrossed(const xMat4x3& mat, xVec3& lastCheckedPos)
{
    xVec3 end;
    xRay3 ray;
    xPlane plane;
    F32 t;
    xVec3 ringCenter;

    if (state == eRingIdle)
    {
        return 0;
    }

    end = mat.pos;
    ray.origin = lastCheckedPos;
    ray.dir = lastCheckedPos - mat.pos;
    ray.dir.safe_normalize(mat.at);
    ray.min_t = 0.0f;
    ray.max_t = ray.dir.length();
    ray.flags = XRAY3_USE_MIN | XRAY3_USE_MAX;

    hasPlayerCrossed = 0;

    switch (triggerBoundsType)
    {
    case eRingBoundTypePlane:
        end -= matrix.pos;
        xMat3x3Tolocal(&end, &matrix, &end);
        ray.origin -= matrix.pos;
        xMat3x3Tolocal(&ray.origin, &matrix, &ray.origin);
        xMat3x3Tolocal(&ray.dir, &matrix, &ray.dir);

        if (xRayHitsBoxFast(&ray, &boundingBox))
        {
            hasPlayerCrossed = 1;
        }
        break;
    case eRingBoundTypeCircle:
    {
        ringCenter = matrix.pos;
        plane.norm = matrix.at;
        plane.d = plane.norm.dot(ringCenter);

        F32 lastDist = plane.norm.dot(lastCheckedPos) - plane.d;
        F32 curDist = plane.norm.dot(mat.pos) - plane.d;

        if ((lastDist < 0.0f && curDist >= 0.0f) || (lastDist > 0.0f && curDist <= 0.0f))
        {
            t = -(lastDist / (curDist - lastDist));

            xVec3 hit;
            xVec3AddScaled(hit, lastCheckedPos, mat.pos - lastCheckedPos, t);

            if (xVec3Dist(&ringCenter, &hit) <= boundingSphere.r)
            {
                hasPlayerCrossed = 1;
            }
        }
        break;
    }
    }

    return hasPlayerCrossed;
}
