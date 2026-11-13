#ifndef XLIGHTVOLUME_H
#define XLIGHTVOLUME_H

#include <types.h>
#include <rwcore.h>
#include <rpworld.h>

#include "iColor.h"

class xLightVolume;

struct activity_type
{
    xLightVolume* owner;
};

// Layout from the Ratatouille GC DWARF (activity at 0x4, as the TSSM code reads it). Vtable:
// render (pure), render_immediate_pass (weak, empty).
class xLightVolume
{
public:
    static void scene_enter();
    static void scene_exit();
    static void reset_all();
    static void render_all();
    void render_atomic(RpAtomic* atomic, xColor_tag color, bool inside, U32);

    void create();
    bool activate();
    void deactivate();

    // No symbol (inlined); name from the Ratatouille GC DWARF. activity is private; callers test
    // it for NULL before calling deactivate.
    U8 active() const
    {
        return activity != NULL;
    }

    virtual void render() = 0;
    virtual void render_immediate_pass(xColor_tag color, bool inside, bool)
    {
    }

private:
    static S32 activities_used;
    static activity_type* activities;

    activity_type* activity;
};

#endif
