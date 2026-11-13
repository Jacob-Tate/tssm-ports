#include "xMath3.h"
#include "xEnt.h"

#include "zGlobals.h"
#include "zScene.h"
#include "zFlameEmitter.h"
#include "zFire.h"
#include "zExplosion.h"
#include "xTransient.h"

namespace
{
    // No GC symbol: inlined into zSceneInit (the PS2 build keeps it out of line).
    void get_world_box(xBox& world_box)
    {
        world_box = *xEntGetAllEntsBox();

        zScene& scene = *globals.sceneCur;
        xBox box;
        zFlameEmitter* flame = (zFlameEmitter*)scene.baseList[eBaseTypeFlameEmitter];
        zFlameEmitter* end_flame = flame + scene.baseCount[eBaseTypeFlameEmitter];

        for (; flame != end_flame; ++flame)
        {
            flame->get_bounding_box(box);
            xBoxUnion(world_box, world_box, box);
        }

        xBox* jspbox = globals.sceneCur->env->geom->jsp_bound;
        xBox* end_jspbox = jspbox + globals.sceneCur->env->geom->jsp_count;

        for (; jspbox != end_jspbox; ++jspbox)
        {
            xBoxUnion(world_box, world_box, *jspbox);
        }
    }
} // namespace

namespace zTransient
{
    // No TSSM symbol: auto-inlined into zSceneInit (the stand-alone copy is dead-stripped). Name
    // and body as in Ratatouille; the extra inline level gives zSceneInit retail's registers.
    void scene_enter()
    {
        xBox world_box;
        get_world_box(world_box);
        xTransient::scene_enter(world_box);
        zFire::scene_enter();
        zExplosion::scene_enter();
    }
} // namespace zTransient
