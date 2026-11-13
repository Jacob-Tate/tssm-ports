#include "xTransient.h"

void xTransient::scene_enter(const xBox& world_box)
{
    space_tree.create(world_box.lower.x - 5.0f, world_box.lower.z - 5.0f, world_box.upper.x + 5.0f,
                      world_box.upper.z + 5.0f, 0.5f, 5.0f, 500);
}

void xTransient::scene_exit()
{
}

void xTransient::reset()
{
    space_tree.clear();
}
