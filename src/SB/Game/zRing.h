#ifndef ZRING_H
#define ZRING_H

#include "xBase.h"
#include "xDynAsset.h"
#include "xMath3.h"
#include "xModel.h"
#include "xSurface.h"

#include "zParPTank.h"

#include <rwcore.h>
#include <rpworld.h>

struct xEnt;
struct xSurface;

enum eRingBoundType
{
    eRingBoundTypePlane,
    eRingBoundTypeCircle
};

enum eRingState
{
    eRingIdle,
    eRingRun,
    eRingWarning
};

// Size: 0x5C
struct zRingType : xDynAsset
{
    xVec3 location;
    xVec3 direction;
    xVec3 offset;
    xVec3 scale;
    U32 triggerBoundsType;
    F32 radius;
    F32 width;
    F32 height;
    F32 timeOut;
    F32 warningTime;
    U32 drivenByID;
};

// Size: 0xB0
class zRing : xBase
{
public:
    zRingType* ringAsset;
    xMat4x3 matrix;
    RpAtomic* model;
    union
    {
        xBox boundingBox;
        xSphere boundingSphere;
    };
    xSurface* surface;
    xVec3 offset;
    F32 timeOut;
    F32 warningTime;
    F32 elapsedTime;
    U32 bubbleCount;
    U32 currentPos;
    F32 lastSpawnTime;
    xEnt* drivenBy;
    eRingBoundType triggerBoundsType : 8;
    eRingState state : 8;
    U8 hasPlayerCrossed;

    static xVec3 bubblePos[300];
    static xVec3 bubbleVel[300];
    static RwRGBAReal warningColor;
    static RwRGBAReal normalColor;
    static RwRaster* ringShadowRaster;
    static U8 applyNormalColor;

    U8 CheckIfPlayerCrossed(const xMat4x3& mat, xVec3& lastCheckedPos);
    void Show(xModelInstance* modelInstance);

    // Hide, Activate and SetModel have no symbol and no PS2 entry (inlined into zRingControl).
    // zRing's xBase is private, so the flag updates must be zRing members. The names are guesses.
    void Hide()
    {
        if (xBaseIsEnabled(this))
        {
            xBaseDisable(this);
            zParPTankSpawnBubbles(bubblePos, bubbleVel, bubbleCount, 1.0f);
        }
    }

    void Activate()
    {
        if (state == eRingIdle)
        {
            state = eRingRun;
            elapsedTime = 0.0f;
        }
    }

    void SetModel(RpAtomic* ringModel)
    {
        model = ringModel;
        if (ringModel != NULL)
        {
            surface = xSurfaceGetByName("RING UV ANIM SURF");
        }
    }

    void Render(xModelInstance* modelInstance);
    void Update(F32 dt, xModelInstance* modelInstance);
    void DrawShadow(xMat4x3* Mat, RwRaster* rwras, F32 factor, F32 distance);
    void ChangeRingColor(F32 dt, xModelInstance* modelInstance);
    void CalculateBubbleOffsets();
    void Reset();
    static void Load(xBase& data, xDynAsset& asset, size_t);
};

#endif
