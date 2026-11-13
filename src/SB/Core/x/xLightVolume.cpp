#include "xLightVolume.h"
#include "xMemMgr.h"

#include <types.h>

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
}

void xLightVolume::render_atomic(RpAtomic* atomic, xColor_tag color, bool inside, U32)
{
}
