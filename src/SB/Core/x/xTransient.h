#ifndef XTRANSIENT_H
#define XTRANSIENT_H

#include <types.h>

#include "xMath3.h"
#include "xSpacePartitionTree.h"

namespace xTransient
{
    extern xSpacePartitionTree2D space_tree;

    void reset();
    void scene_exit();
    void scene_enter(const xBox& world_box);
} // namespace xTransient

#endif
