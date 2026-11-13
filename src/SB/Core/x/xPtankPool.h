#ifndef XPTANKPOOL_H
#define XPTANKPOOL_H

#include <types.h>
#include <rwplcore.h>
#include <rwcore.h>
#include <rpworld.h>
#include <rpptank.h>

#include "iColor.h"
struct xVec2;
struct xVec3;
struct xMat4x3;

// TSSM values from the PS2 DWARF (zSmoke passes PGT_POS_COLOR_SIZE_UV2 == 3)
enum ptank_group_type
{
    PGT_COLOR_MAT,
    PGT_COLOR_MAT_UV2,
    PGT_POS_COLOR_SIZE,
    PGT_POS_COLOR_SIZE_UV2,
    PGT_POS_COLOR_SIZE_ROT,
    PGT_POS_COLOR_SIZE_ROT_UV2,
    MAX_PGT
};

struct render_state
{
    RwTexture* texture;
    U32 src_blend;
    U32 dst_blend;
    S32 flags;
};

struct ptank_pool
{
    render_state rs;
    U32 order_group;
    S32 order_index;
    U32 used;
    RpAtomic* ptank;

    bool valid() const
    {
        return ptank != NULL;
    }

    // ptanks are created with room for 80 particles (see grab_block)
    bool at_block_end() const
    {
        return used >= 80;
    }

    void unlock_block()
    {
        RpPTankAtomicUnlock(ptank);
        RPATOMICPTANKPLUGINDATA(ptank)->instFlags |= rpPTANKIFLAGACTNUMCHG;
        RPATOMICPTANKPLUGINDATA(ptank)->actPCount = used;
    }

    // No GC symbols for reset/flush: they are always inlined (zExplosion's ptank_render_*).
    // used = 0xFFFF makes the next next() call grab a fresh block.
    void reset()
    {
        ptank = NULL;
        used = 0xFFFF;
    }

    void flush()
    {
        if (valid())
        {
            unlock_block();
        }
    }

    void grab_block(ptank_group_type type);
};

// The GC build stores the lock stride of every cluster after the data pointers
// (absent from the PS2 DWARF).
struct ptank_pool__color_mat : ptank_pool
{
    xColor_tag* color;
    xMat4x3* mat;
    S32 color_stride;
    S32 mat_stride;

    void next()
    {
        if (at_block_end())
        {
            if (valid())
            {
                unlock_block();
            }

            grab_block(PGT_COLOR_MAT);

            if (!valid())
            {
                return;
            }

            used = 0;
            lock_block();
        }
        else
        {
            color = (xColor_tag*)((U8*)color + color_stride);
            mat = (xMat4x3*)((U8*)mat + mat_stride);
        }

        used++;
    }

    void lock_block()
    {
        RpPTankLockStruct ls_color;
        RpPTankLockStruct ls_mat;

        RpPTankAtomicLock(ptank, &ls_color, rpPTANKLFLAGCOLOR, rpPTANKLOCKWRITE);
        RpPTankAtomicLock(ptank, &ls_mat, rpPTANKLFLAGMATRIX, rpPTANKLOCKWRITE);

        color = (xColor_tag*)ls_color.data;
        mat = (xMat4x3*)ls_mat.data;
        color_stride = ls_color.stride;
        mat_stride = ls_mat.stride;
    }
};

struct ptank_pool__pos_color_size : ptank_pool
{
    xVec3* pos;
    xColor_tag* color;
    xVec2* size;

    // GC only (absent from the PS2 DWARF): the lock strides, stored at
    // 0x2C/0x30/0x34 by zExplosion's ptank_render_* functions.
    S32 pos_stride;
    S32 color_stride;
    S32 size_stride;

    void next()
    {
        if (at_block_end())
        {
            if (valid())
            {
                unlock_block();
            }

            grab_block(PGT_POS_COLOR_SIZE);

            if (!valid())
            {
                return;
            }

            used = 0;
            lock_block();
        }
        else
        {
            pos = (xVec3*)((U8*)pos + pos_stride);
            color = (xColor_tag*)((U8*)color + color_stride);
            size = (xVec2*)((U8*)size + size_stride);
        }

        used++;
    }

    void lock_block()
    {
        RpPTankLockStruct ls_color;
        RpPTankLockStruct ls_pos;
        RpPTankLockStruct ls_size;

        RpPTankAtomicLock(ptank, &ls_pos, rpPTANKLFLAGPOSITION, rpPTANKLOCKWRITE);
        RpPTankAtomicLock(ptank, &ls_color, rpPTANKLFLAGCOLOR, rpPTANKLOCKWRITE);
        RpPTankAtomicLock(ptank, &ls_size, rpPTANKLFLAGSIZE, rpPTANKLOCKWRITE);

        pos = (xVec3*)ls_pos.data;
        color = (xColor_tag*)ls_color.data;
        size = (xVec2*)ls_size.data;
        pos_stride = ls_pos.stride;
        color_stride = ls_color.stride;
        size_stride = ls_size.stride;
    }
};

// GC only (absent from the PS2 DWARF): one lock stride per element buffer
// (target offsets 0x30..0x3C, filled from the RpPTankLockStruct strides).
struct ptank_pool__pos_color_size_rot : ptank_pool
{
    xVec3* pos;
    xColor_tag* color;
    xVec2* size;
    F32* rot;
    S32 pos_stride;
    S32 color_stride;
    S32 size_stride;
    S32 rot_stride;

    void lock_block()
    {
        RpPTankLockStruct ls_color;
        RpPTankLockStruct ls_pos;
        RpPTankLockStruct ls_size;
        RpPTankLockStruct ls_rot;

        RpPTankAtomicLock(ptank, &ls_pos, rpPTANKLFLAGPOSITION, rpPTANKLOCKWRITE);
        RpPTankAtomicLock(ptank, &ls_color, rpPTANKLFLAGCOLOR, rpPTANKLOCKWRITE);
        RpPTankAtomicLock(ptank, &ls_size, rpPTANKLFLAGSIZE, rpPTANKLOCKWRITE);
        RpPTankAtomicLock(ptank, &ls_rot, rpPTANKLFLAG2DROTATE, rpPTANKLOCKWRITE);

        pos = (xVec3*)ls_pos.data;
        color = (xColor_tag*)ls_color.data;
        size = (xVec2*)ls_size.data;
        rot = (F32*)ls_rot.data;
        pos_stride = ls_pos.stride;
        color_stride = ls_color.stride;
        size_stride = ls_size.stride;
        rot_stride = ls_rot.stride;
    }

    void next()
    {
        if (at_block_end())
        {
            if (valid())
            {
                unlock_block();
            }

            grab_block(PGT_POS_COLOR_SIZE_ROT);

            if (!valid())
            {
                return;
            }

            used = 0;
            lock_block();
        }
        else
        {
            pos = (xVec3*)((U8*)pos + pos_stride);
            color = (xColor_tag*)((U8*)color + color_stride);
            size = (xVec2*)((U8*)size + size_stride);
            rot = (F32*)((U8*)rot + rot_stride);
        }

        used++;
    }
};

// GC only (absent from the PS2 DWARF): one lock stride per element buffer, as in the other
// pools (layouts from the inlined next() in zFire, zSmoke and zDust).
struct ptank_pool__color_mat_uv2 : ptank_pool
{
    xColor_tag* color;
    xMat4x3* mat;
    xVec2* uv;
    S32 color_stride;
    S32 mat_stride;
    S32 uv_stride;

    void next()
    {
        if (at_block_end())
        {
            if (valid())
            {
                unlock_block();
            }

            grab_block(PGT_COLOR_MAT_UV2);

            if (!valid())
            {
                return;
            }

            used = 0;
            lock_block();
        }
        else
        {
            color = (xColor_tag*)((U8*)color + color_stride);
            mat = (xMat4x3*)((U8*)mat + mat_stride);
            uv = (xVec2*)((U8*)uv + uv_stride);
        }

        used++;
    }

    // Lock order from the Ratatouille GC DWARF.
    void lock_block()
    {
        RpPTankLockStruct ls_color;
        RpPTankLockStruct ls_mat;
        RpPTankLockStruct ls_uv;

        RpPTankAtomicLock(ptank, &ls_color, rpPTANKLFLAGCOLOR, rpPTANKLOCKWRITE);
        RpPTankAtomicLock(ptank, &ls_mat, rpPTANKLFLAGMATRIX, rpPTANKLOCKWRITE);
        RpPTankAtomicLock(ptank, &ls_uv, rpPTANKLFLAGVTX2TEXCOORDS, rpPTANKLOCKWRITE);

        color = (xColor_tag*)ls_color.data;
        mat = (xMat4x3*)ls_mat.data;
        uv = (xVec2*)ls_uv.data;
        color_stride = ls_color.stride;
        mat_stride = ls_mat.stride;
        uv_stride = ls_uv.stride;
    }
};

struct ptank_pool__pos_color_size_uv2 : ptank_pool
{
    xVec3* pos;
    xColor_tag* color;
    xVec2* size;
    xVec2* uv;
    S32 pos_stride;
    S32 color_stride;
    S32 size_stride;
    S32 uv_stride;

    void lock_block()
    {
        RpPTankLockStruct ls_color;
        RpPTankLockStruct ls_pos;
        RpPTankLockStruct ls_size;
        RpPTankLockStruct ls_uv;

        RpPTankAtomicLock(ptank, &ls_pos, rpPTANKLFLAGPOSITION, rpPTANKLOCKWRITE);
        RpPTankAtomicLock(ptank, &ls_color, rpPTANKLFLAGCOLOR, rpPTANKLOCKWRITE);
        RpPTankAtomicLock(ptank, &ls_size, rpPTANKLFLAGSIZE, rpPTANKLOCKWRITE);
        RpPTankAtomicLock(ptank, &ls_uv, rpPTANKLFLAGVTX2TEXCOORDS, rpPTANKLOCKWRITE);

        pos = (xVec3*)ls_pos.data;
        color = (xColor_tag*)ls_color.data;
        size = (xVec2*)ls_size.data;
        uv = (xVec2*)ls_uv.data;
        pos_stride = ls_pos.stride;
        color_stride = ls_color.stride;
        size_stride = ls_size.stride;
        uv_stride = ls_uv.stride;
    }

    void next()
    {
        if (at_block_end())
        {
            if (valid())
            {
                unlock_block();
            }

            grab_block(PGT_POS_COLOR_SIZE_UV2);

            if (!valid())
            {
                return;
            }

            used = 0;
            lock_block();
        }
        else
        {
            pos = (xVec3*)((U8*)pos + pos_stride);
            color = (xColor_tag*)((U8*)color + color_stride);
            size = (xVec2*)((U8*)size + size_stride);
            uv = (xVec2*)((U8*)uv + uv_stride);
        }

        used++;
    }
};

struct ptank_pool__pos_color_size_rot_uv2 : ptank_pool
{
    xVec3* pos;
    xColor_tag* color;
    xVec2* size;
    xVec2* uv;
    F32* rot;
    S32 pos_stride;
    S32 color_stride;
    S32 size_stride;
    S32 rot_stride;
    S32 uv_stride;

    void lock_block()
    {
        RpPTankLockStruct ls_color;
        RpPTankLockStruct ls_pos;
        RpPTankLockStruct ls_size;
        RpPTankLockStruct ls_rot;
        RpPTankLockStruct ls_uv;

        RpPTankAtomicLock(ptank, &ls_pos, rpPTANKLFLAGPOSITION, rpPTANKLOCKWRITE);
        RpPTankAtomicLock(ptank, &ls_color, rpPTANKLFLAGCOLOR, rpPTANKLOCKWRITE);
        RpPTankAtomicLock(ptank, &ls_size, rpPTANKLFLAGSIZE, rpPTANKLOCKWRITE);
        RpPTankAtomicLock(ptank, &ls_rot, rpPTANKLFLAG2DROTATE, rpPTANKLOCKWRITE);
        RpPTankAtomicLock(ptank, &ls_uv, rpPTANKLFLAGVTX2TEXCOORDS, rpPTANKLOCKWRITE);

        pos = (xVec3*)ls_pos.data;
        color = (xColor_tag*)ls_color.data;
        size = (xVec2*)ls_size.data;
        rot = (F32*)ls_rot.data;
        uv = (xVec2*)ls_uv.data;
        pos_stride = ls_pos.stride;
        color_stride = ls_color.stride;
        size_stride = ls_size.stride;
        rot_stride = ls_rot.stride;
        uv_stride = ls_uv.stride;
    }

    void next()
    {
        if (at_block_end())
        {
            if (valid())
            {
                unlock_block();
            }

            grab_block(PGT_POS_COLOR_SIZE_ROT_UV2);

            if (!valid())
            {
                return;
            }

            used = 0;
            lock_block();
        }
        else
        {
            pos = (xVec3*)((U8*)pos + pos_stride);
            color = (xColor_tag*)((U8*)color + color_stride);
            size = (xVec2*)((U8*)size + size_stride);
            rot = (F32*)((U8*)rot + rot_stride);
            uv = (xVec2*)((U8*)uv + uv_stride);
        }

        used++;
    }
};

void xPTankPoolSceneEnter();
void xPTankPoolFreeEnable(bool freeme);
void xPTankPoolSceneExit();
void xPTankPoolRender();

#endif
