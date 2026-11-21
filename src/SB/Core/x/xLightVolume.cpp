#include "xLightVolume.h"
#include "xMemMgr.h"

#include <types.h>

#ifdef PLATFORM_PC
#include "iDraw.h"
#include "iExtras.h"
#include "iLightVolume.h"
#include "xLightKit.h"
#include "zGlobals.h"
#include "zRenderState.h"
#include "zScene.h"
#endif

// Dead-stripped placeholder, not recovered code: retail's .rodata keeps two unreferenced
// all-zero 16-byte brace-init templates here, most likely from an uncalled header inline with
// two 16-byte locals. That body is unknown.
static void __deadstripped_xLightVolume()
{
    F32 a[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    F32 b[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
}

activity_type* xLightVolume::activities;
S32 xLightVolume::activities_used;

void xLightVolume::scene_enter()
{
    activities = (activity_type*)xMemAllocSize(64 * sizeof(activity_type));
    activities_used = 0;
    reset_all();
}

void xLightVolume::scene_exit()
{
}

void xLightVolume::reset_all()
{
    activity_type* act = activities;
    activity_type* end_act = act + activities_used;

    for (; act != end_act; act++)
    {
        act->owner->activity = NULL;
    }

    activities_used = 0;
}

void xLightVolume::create()
{
    activity = NULL;
}

bool xLightVolume::activate()
{
    if (activities_used >= 64)
    {
        return false;
    }

    activity = activities + activities_used;
    activities_used++;
    activity->owner = this;
    return true;
}

void xLightVolume::deactivate()
{
    activities_used--;
    activity_type* tail_activity = activities + activities_used;

    if (activity != tail_activity)
    {
        *activity = *tail_activity;
        activity->owner->activity = activity;
    }

    activity = NULL;
}

void xLightVolume::render_all()
{
#ifdef PLATFORM_PC
    // PLATFORM_PC: empty on the GameCube, which never draws a light volume. With [extras]
    // spotlights = xbox, the Xbox's (DEFAULT.XBE 0x185640): each active volume drawn through
    // the stencil buffer, cleared for each one (iLightVolume.h).
    if (activities_used <= 0 || iExtrasInForce(iEXTRAS_SPOTLIGHTS) != iEXTRAS_XBOX ||
        !iLightVolumeReady())
    {
        return;
    }

    U32 mask = lastSetFBMSK;
    iLightVolumeBegin();
    iDrawSetFBMSK(0, 0xFFFFFFFF, true);
    xLightKit_Enable(NULL, globals.currWorld);
    zRenderState(SDRS_LightVolume);

    activity_type* act = activities;
    activity_type* end_act = act + activities_used;

    for (; act != end_act; act++)
    {
        iLightVolumeClearStencil();
        act->owner->render();
    }

    if (mask != lastSetFBMSK)
    {
        iDrawSetFBMSK(mask, 0xFFFFFFFF, true);
    }
    iLightVolumeEnd();
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
    zRenderState(SDRS_OpaqueModels);
#endif
}

void xLightVolume::render_atomic(RpAtomic* atomic, xColor_tag color, bool inside, U32)
{
#ifdef PLATFORM_PC
    // PLATFORM_PC: the Xbox's (0x17ece0), reached only through render_all above. It never reads
    // `inside`; B401's plain adding is its own check of the scene ID (0x17ed01).
    RwRGBA c = { color.r, color.g, color.b, color.a };
    iLightVolumeRender(atomic, c, globals.sceneCur != NULL && globals.sceneCur->sceneID == 'B401');
#endif
}
