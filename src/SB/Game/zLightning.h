#ifndef ZLIGHTNING_H
#define ZLIGHTNING_H

#include "xMath3.h"
#include "iColor.h"
#include "iSnd.h"

#include "xBase.h"
#include "xDynAsset.h"
#include "zFX.h"

struct _tagLightningAdd
{
    U32 type;
    F32 setup_degrees;
    F32 move_degrees;
    F32 rot_radius;
    xVec3* start;
    xVec3* end;
    S16 total_points;
    S16 end_points;
    F32 time;
    F32 arc_height;
    F32 thickness;
    F32 segmentsPerMeter;
    iColor_tag color;
    F32 rand_radius;
    U32 flags;
    F32 zeus_normal_offset;
    F32 zeus_back_offset;
    F32 zeus_side_offset;
    F32 branchSpeed;
    U32 mainTexture;
    U32 branchTexture;
    S32 damage;
    F32 knockBackSpeed;
    xEnt* followStart;
    xEnt* followEnd;
    iSndHandle sndHandle;
    U8 collisionEnabled;
    U8 isMain;
};

struct zLightningWidget;

struct zLightningAsset : xDynAsset
{
    xVec3 start;
    xVec3 end;
    iColor_tag color;
    F32 thickness;
    F32 branchSpeed;
    U32 mainTexture;
    U32 branchTexture;
    S32 damage;
    F32 knockBackSpeed;
    U32 sound;
    U32 soundHit1;
    U32 soundHit2;
    U32 followStart;
    U32 followEnd;
    U32 collisionEnabled;
};

struct zLightning
{
    U32 type;
    U32 flags;
    struct
    {
        xVec3 endPoint[2];
        xVec3 endPointB;
        xVec3 direction;
        F32 length;
        F32 scale;
        F32 width;
        F32 endParam[2];
        F32 endVel[2];
        F32 paramSpan[2];
    } func;
    // GC only (not in the PS2 DWARF): the widget that owns this lightning, cleared on reset
    zLightningWidget* widget;
    U32 numStrips;
    F32 alphaRamp;
    iColor_tag color;
    F32 time_left;
    F32 time_total;
    zLightning* nextBranch;
    zLightning* prevBranch;
    F32 parentSegment;
    F32 lastParentSegment;
    F32 segmentsPerMeter;
    F32 branchSpeed;
    S32 damage;
    F32 knockBackSpeed;
    RwRaster* mainTexture;
    RwRaster* branchTexture;
    F32 sparkTimer;
    xVec3 collisionPoint;
    F32 genTime;
    U8 randomizeBranchEndPoint;
    U8 collisionEnabled;
    iSndHandle sndHandle;
    xEnt* followStart;
    xVec3 followStartOffset;
    xEnt* followEnd;
    xVec3 followEndOffset;
    void (*renderCB)(zLightning*, RxObjSpace3DVertex*, RxObjSpace3DVertex*, U32);
    void* context;
    F32 weightParam[12];
};

struct zLightningFuncPiece
{
    xVec3 coef0;
    F32 param;
    xVec3 coef1;
    U32 pad;
};

struct zLightningWidget : xBase
{
    zLightning* l;
    zLightningAsset* asset;

    static void EventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                        xBase* toParamWidget, U32 toParamWidgetID);
    static void Init(xBase& data, xDynAsset& asset, size_t);
};

void lightningTweakChangeType(tweak_info& t);
void zLightningInit();
zLightning* zLightningAdd(_tagLightningAdd* add);
void UpdateLightning(zLightning* l, F32 seconds);
void zLightningUpdate(F32 seconds);
bool zLightningCull(zLightning* l, F32& distanceFade);
void zLightningShow(zLightning* l, S32 show);
void zLightningKill(zLightning* l);
void RenderLightning(zLightning* l);
void zLightningRender();
void zLightningCollision(zLightning* l, xVec3& start, xVec3& end);
void zLightningModifyEndpoints(zLightning* l, const xVec3* start, const xVec3* end,
                               F32 scalePerLength);
void zLightningModifyEndpoints(zLightning* l, const xVec3* start, const xVec3* end);
void _zLightningKill(zLightning* l);
zLightning* _zLightningAdd(_tagLightningAdd* add);
void zLightningReset();
void xDebugAddTweak(char* name, U8* v, U8 vmin, U8 vmax, tweak_callback* cb, void* context,
                    U32 flags);
void xDebugAddFlagTweak(char* name, U32* v, U32 mask, tweak_callback* cb, void* context, U32 flags);
void xDebugAddSelectTweak(char* name, U32* v, char** labels, U32* values, U32 labels_size,
                          tweak_callback* cb, void* context, U32 flags);
void xDebugAddTweak(char* name, S16* v, S16 vmin, S16 vmax, tweak_callback* cb, void* context,
                    U32 flags);

void zLightningInit();
void zLightningUpdate(F32 seconds);
void zLightningRender();

#endif
