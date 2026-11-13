#include "xPartition.h"
#include "xMemMgr.h"
#include <string.h>

static inline _tagPartLink* PartitionGetFreeLink()
{
    return (_tagPartLink*)xMemAllocSize(sizeof(_tagPartLink));
}

static inline void PartitionSpaceReset(_tagPartSpace* space)
{
    memset(space, 0, sizeof(_tagPartSpace));
}

static inline void PartitionSpaceInsert(_tagPartSpace* space, void* data)
{
    space->total++;
    _tagPartLink* head = &space->head;
    _tagPartLink* tmp = head;
    while (tmp->next != NULL)
    {
        tmp = tmp->next;
    }
    head = PartitionGetFreeLink();
    tmp->next = head;
    tmp->next->data = data;
    tmp->next->next = NULL;
}

static inline S32 xPartitionGetTrueIdx(_tagPartition* part, S32 x, S32 y, S32 z)
{
    S32 idx = (y * (part->total_x * part->total_z));
    idx += part->total_x * z;
    idx += x;
    return idx;
}

void xPartitionReset()
{
}

void xPartitionVolume(_tagPartition* part, xVolume* volume, S32 x_spaces, S32 y_spaces,
                      S32 z_spaces)
{
    memset(part, 0, sizeof(_tagPartition));
    xBound* xb = volume->GetBound();
    xBox* bb = &xb->box.box;
    part->min = bb->lower;
    part->max = bb->upper;

    F32 dx = part->max.x - part->min.x;
    F32 dy = part->max.y - part->min.y;
    F32 dz = part->max.z - part->min.z;

    part->total_x = x_spaces;
    part->total_y = y_spaces;
    part->total_z = z_spaces;

    part->space_dim.x = dx / part->total_x;
    part->space_dim.y = dy / part->total_y;
    part->space_dim.z = dz / part->total_z;

    part->space = (_tagPartSpace*)xMemAllocSize(sizeof(_tagPartSpace) * part->total_x *
                                                part->total_y * part->total_z);

    for (S32 z = 0; z < z_spaces; z++)
    {
        for (S32 y = 0; y < y_spaces; y++)
        {
            for (S32 x = 0; x < x_spaces; x++)
            {
                PartitionSpaceReset(&part->space[xPartitionGetTrueIdx(part, x, y, z)]);
            }
        }
    }

    PartitionSpaceReset(&part->global);
}

void xPartitionWorld(_tagPartition* part, xEnv* env, S32 x_spaces, S32 y_spaces, S32 z_spaces)
{
    memset(part, 0, sizeof(_tagPartition));
    xBox* bb = (xBox*)xEnvGetBBox(env);
    part->min = bb->lower;
    part->max = bb->upper;

    F32 dx = part->max.x - part->min.x;
    F32 dy = part->max.y - part->min.y;
    F32 dz = part->max.z - part->min.z;

    part->total_x = x_spaces;
    part->total_y = y_spaces;
    part->total_z = z_spaces;

    part->space_dim.x = dx / part->total_x;
    part->space_dim.y = dy / part->total_y;
    part->space_dim.z = dz / part->total_z;

    part->space = (_tagPartSpace*)xMemAllocSize(sizeof(_tagPartSpace) * part->total_x *
                                                part->total_y * part->total_z);

    for (S32 z = 0; z < z_spaces; z++)
    {
        for (S32 y = 0; y < y_spaces; y++)
        {
            for (S32 x = 0; x < x_spaces; x++)
            {
                PartitionSpaceReset(&part->space[xPartitionGetTrueIdx(part, x, y, z)]);
            }
        }
    }

    PartitionSpaceReset(&part->global);
}

S32 xPartitionInsert(_tagPartition* part, void* insert_data, xVec3* insert_pos)
{
    if (insert_pos->x < part->min.x || insert_pos->x > part->max.x || insert_pos->y < part->min.y ||
        insert_pos->y > part->max.y || insert_pos->z < part->min.z || insert_pos->z > part->max.z)
    {
        PartitionSpaceInsert(&part->global, insert_data);
        return -1;
    }

    // x/y/z are not in the PS2 DWARF; passing the three quotients straight to xPartitionGetTrueIdx
    // swaps the FPRs of part->min.x/min.z.
    S32 x = (insert_pos->x - part->min.x) / part->space_dim.x;
    S32 y = (insert_pos->y - part->min.y) / part->space_dim.y;
    S32 z = (insert_pos->z - part->min.z) / part->space_dim.z;
    S32 idx = xPartitionGetTrueIdx(part, x, y, z);
    PartitionSpaceInsert(&part->space[idx], insert_data);

    return idx;
}

// dest is not in the PS2 DWARF. The if/else for src/dest and the x/y/z locals give retail's
// registers.
S32 xPartitionUpdate(_tagPartition* part, void* data, S32 old_idx, xVec3* current_pos)
{
    S32 cur_idx;
    _tagPartSpace* src;
    _tagPartSpace* dest;

    if (current_pos->x < part->min.x || current_pos->x > part->max.x ||
        current_pos->y < part->min.y || current_pos->y > part->max.y ||
        current_pos->z < part->min.z || current_pos->z > part->max.z)
    {
        cur_idx = -1;
    }
    else
    {
        S32 x = (current_pos->x - part->min.x) / part->space_dim.x;
        S32 y = (current_pos->y - part->min.y) / part->space_dim.y;
        S32 z = (current_pos->z - part->min.z) / part->space_dim.z;
        cur_idx = xPartitionGetTrueIdx(part, x, y, z);
    }

    if (old_idx == cur_idx)
    {
        return cur_idx;
    }

    if (old_idx == -1)
    {
        src = &part->global;
    }
    else
    {
        src = &part->space[old_idx];
    }

    if (cur_idx == -1)
    {
        dest = &part->global;
    }
    else
    {
        dest = &part->space[cur_idx];
    }

    xPartitionSpaceMove(src, dest, (UPtr)data);
    return cur_idx;
}

void xPartitionSpaceMove(_tagPartSpace* src, _tagPartSpace* dest, UPtr data)
{
    _tagPartLink* dest_lnk = &dest->head;
    _tagPartLink* src_lnk;
    _tagPartLink* src_pre;

    while (dest_lnk->next != NULL)
    {
        dest_lnk = dest_lnk->next;
    }

    src_lnk = &src->head;
    src_pre = &src->head;

    while (src_lnk->data != (void*)data)
    {
        src_pre = src_lnk;
        src_lnk = src_lnk->next;
    }

    src_pre->next = src_lnk->next;

    dest_lnk->next = src_lnk;
    dest_lnk->next->next = NULL;

    src->total--;
    dest->total++;
}
