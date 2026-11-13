#include "xPtankPool.h"
#include "xMemMgr.h"
#include "xFXHighDynamicRange.h"
#include "iDraw.h"
#include "iMemMgr.h"
#include "zRenderState.h"
#include "xDebug.h"

#include <rwcore.h>
#include <rpworld.h>
#include <rpptank.h>
#include <PowerPC_EABI_Support/MSL_C/MSL_Common/stdlib.h>
#include <string.h>

namespace
{
    // ptank_context::flags
    // 0x1     no z-test (render_state flag)
    // 0x2     z-write (render_state flag)
    // 0x4     sort by order_group/order_index (render_state flag)
    // 0x8     brighten for HDR (render_state flag)
    // 0x10000 grabbed this frame
    // 0x20000 texture released

    struct ptank_context
    {
        ptank_context* next;
        RpAtomic* ptank;
        S32 flags;
        U32 src_blend;
        U32 dst_blend;
        U32 order_group;
        S32 order_index;
        U32 sequence_index;
    };

    struct group_data
    {
        U32 max_size;
        U32 create_flags;
        ptank_context* ptanks;
        ptank_context** buckets;
        U32 size;
        U32 used;
        U32 buckets_used;
    };

    U8 sxPTankPoolFree = TRUE;
    U8 inited;
    U32 current_sequence_index;

    group_data groups[MAX_PGT] = {
        { 4, rpPTANKDFLAGCNSVTX2TEXCOORDS | rpPTANKDFLAGMATRIX | rpPTANKDFLAGCOLOR },
        { 8, rpPTANKDFLAGVTX2TEXCOORDS | rpPTANKDFLAGMATRIX | rpPTANKDFLAGCOLOR },
        { 12, rpPTANKDFLAGCNSVTX2TEXCOORDS | rpPTANKDFLAGSIZE | rpPTANKDFLAGCOLOR |
                  rpPTANKDFLAGPOSITION },
        { 16,
          rpPTANKDFLAGVTX2TEXCOORDS | rpPTANKDFLAGSIZE | rpPTANKDFLAGCOLOR | rpPTANKDFLAGPOSITION },
        { 12, rpPTANKDFLAGCNSVTX2TEXCOORDS | rpPTANKDFLAG2DROTATE | rpPTANKDFLAGSIZE |
                  rpPTANKDFLAGCOLOR | rpPTANKDFLAGPOSITION },
        { 8, rpPTANKDFLAGVTX2TEXCOORDS | rpPTANKDFLAG2DROTATE | rpPTANKDFLAGSIZE |
                 rpPTANKDFLAGCOLOR | rpPTANKDFLAGPOSITION },
    };

    // The assert and btexture are Ratatouille's; with them the function is too big to auto-inline,
    // as retail calls it from xPTankPoolSceneEnter and xPTankPoolRender.
    void sort_buckets(group_data& group)
    {
        memset(group.buckets, 0, group.buckets_used * sizeof(ptank_context*));
        group.buckets_used = 2;

        ptank_context* it = group.ptanks;
        ptank_context* endp = it + group.size;
        while (it != endp)
        {
            RwTexture* texture = it->ptank->geometry->matList.materials[0]->texture;
            ptank_context** bucket = group.buckets;

            if (texture == NULL)
            {
                if (it->flags & 0x1)
                {
                    bucket++;
                }
            }
            else
            {
                bucket += 2;
                ptank_context** end = bucket + group.buckets_used - 2;

                while (bucket < end)
                {
                    ptank_context* front = *bucket;

                    xASSERT(front != 0);

                    RwTexture* btexture = front->ptank->geometry->matList.materials[0]->texture;
                    if (texture == btexture && it->dst_blend == front->dst_blend &&
                        it->src_blend == front->src_blend && !((it->flags ^ front->flags) & 0x1))
                    {
                        break;
                    }

                    bucket++;
                }

                if (bucket == end)
                {
                    group.buckets_used++;
                }
            }

            it->next = *bucket;
            *bucket = it;
            it++;
        }
    }

    inline void init_groups()
    {
        U32 total = 0;

        {
            group_data* it = groups;
            group_data* end = groups + MAX_PGT;
            while (it != end)
            {
                total += it->max_size;
                it++;
            }
        }

        total = total * sizeof(ptank_context) + (total + MAX_PGT * 2) * sizeof(ptank_context*);
        U8* mem = (U8*)xMemAlloc(gActiveHeap, total, 0);

        {
            group_data* it = groups;
            group_data* end = groups + MAX_PGT;
            while (it != end)
            {
                it->buckets_used = 0;
                it->used = 0;
                it->size = 0;
                it->ptanks = (ptank_context*)mem;
                mem += it->max_size * sizeof(ptank_context);
                it->buckets = (ptank_context**)mem;
                it->buckets_used = it->max_size + 2;
                mem += it->buckets_used * sizeof(ptank_context*);
                sort_buckets(*it);
                it++;
            }
        }
    }

    inline RpAtomic* create_ptank(U32 flags)
    {
        flags |= rpPTANKDFLAGARRAY;

        RpAtomic* ptank = RpPTankAtomicCreate(80, flags, rpPTANKDFLAGNONE);
        if (ptank == NULL)
        {
            return NULL;
        }

        RwFrame* frame = RwFrameCreate();
        if (frame == NULL)
        {
            RpPTankAtomicDestroy(ptank);
            return NULL;
        }

        frame->modelling.right.x = frame->modelling.up.y = frame->modelling.at.z = 1.0f;
        frame->modelling.right.y = frame->modelling.right.z = frame->modelling.up.x = 0.0f;
        frame->modelling.up.z = frame->modelling.at.x = frame->modelling.at.y = 0.0f;
        frame->modelling.pos.x = frame->modelling.pos.y = frame->modelling.pos.z = 0.0f;
        frame->modelling.flags |= rwMATRIXTYPEORTHONORMAL | rwMATRIXINTERNALIDENTITY;

        RpAtomicSetFrame(ptank, frame);
        RPATOMICPTANKPLUGINDATA(ptank)->publicData.vertexAlphaBlend = TRUE;

        if (flags & rpPTANKDFLAGCNSVTX2TEXCOORDS)
        {
            RwTexCoords uv[2] = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
            memcpy(RPATOMICPTANKPLUGINDATA(ptank)->publicData.cUV, uv, sizeof(uv));
            RPATOMICPTANKPLUGINDATA(ptank)->instFlags |= rpPTANKIFLAGCNSVTX2TEXCOORDS;
        }

        return ptank;
    }

    inline void destroy_ptank(RpAtomic* ptank)
    {
        if (ptank != NULL)
        {
            RwFrame* frame = RpAtomicGetFrame(ptank);
            RpAtomicSetFrame(ptank, NULL);

            if (frame != NULL)
            {
                _rwFrameSyncDirty();
                RwFrameDestroy(frame);
            }

            RpPTankAtomicDestroy(ptank);
        }
    }

    inline U32 create_ptanks(group_data& group, U32 want)
    {
        U32 old_group_size = group.size;
        if (old_group_size + want > group.max_size)
        {
            want = group.max_size - old_group_size;
        }

        ptank_context* head = group.ptanks + old_group_size;
        ptank_context* end = head + want;
        ptank_context* it = head;
        while (it < end)
        {
            it->ptank = create_ptank(group.create_flags);
            if (it->ptank == NULL)
            {
                break;
            }

            it->flags = 0x1;
            it->next = *group.buckets;
            *group.buckets = it;
            it++;
            group.size++;
        }

        return group.size - old_group_size;
    }

    inline void render_ptank(ptank_context& context, S32& render_flags)
    {
        S32 xor_flags = render_flags ^ context.flags;

        if (xor_flags & 0x1)
        {
            S32 noztest = context.flags & 0x1;
            render_flags = (render_flags & ~0x1) | noztest;
            RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)!noztest);
        }

        if (xor_flags & 0x2)
        {
            S32 zwrite = context.flags & 0x2;
            render_flags = (render_flags & ~0x2) | zwrite;
            RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)zwrite);
        }

        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)!(context.flags & 0x1));

        U32 oldtest = xFXHighDynamicRangeBrightenPtank(context.ptank, context.flags & 0x8);
        context.ptank->renderCallBack(context.ptank);
        xFXHighDynamicRangeRestorePtank(context.ptank, oldtest);
    }

    S32 compare_ptanks(const void* e1, const void* e2)
    {
        ptank_context* p1 = (ptank_context*)e1;
        ptank_context* p2 = (ptank_context*)e2;

        if (p1->order_group != p2->order_group)
        {
            return p1->order_group - p2->order_group;
        }

        if (p1->order_index != p2->order_index)
        {
            return p1->order_index - p2->order_index;
        }

        if (p1->dst_blend != p2->dst_blend)
        {
            return p2->dst_blend - p1->dst_blend;
        }

        if (p1->src_blend != p2->src_blend)
        {
            return p1->src_blend - p2->src_blend;
        }

        RwTexture* tex1 = p1->ptank->geometry->matList.materials[0]->texture;
        RwTexture* tex2 = p2->ptank->geometry->matList.materials[0]->texture;

        if (tex1 != tex2)
        {
            return (U32)(UPtr)tex1 - (U32)(UPtr)tex2;
        }

        return p1->sequence_index - p2->sequence_index;
    }
} // namespace

void xPTankPoolSceneEnter()
{
    inited = TRUE;
    current_sequence_index = 0;
    init_groups();

    group_data* it = groups;
    group_data* end = groups + MAX_PGT;
    while (it != end)
    {
        create_ptanks(*it, (U32)(it->max_size + 0.5f));
        it++;
    }
}

void xPTankPoolSceneExit()
{
    inited = FALSE;

    group_data* g = groups;
    group_data* endg = groups + MAX_PGT;
    while (g != endg)
    {
        ptank_context* p = g->ptanks;
        ptank_context* endp = p + g->size;
        while (p != endp)
        {
            destroy_ptank(p->ptank);
            p++;
        }

        g++;
    }
}

void xPTankPoolFreeEnable(bool freeme)
{
    sxPTankPoolFree = freeme;
}

void xPTankPoolRender()
{
    zRenderState(SDRS_XPTankPool);

    if (HDR_brightening != TRUE)
    {
        HDR_brightening = TRUE;
        iDrawSetFBMSK(0x00000000, 0xFFFFFFFF, true);
    }

    ptank_context* ptanks = (ptank_context*)iMemPushTemp(60 * sizeof(ptank_context));
    ptank_context* ptank = ptanks;

    group_data* g = groups;
    group_data* endg = groups + MAX_PGT;
    while (g != endg)
    {
        if (g->used)
        {
            memcpy(ptank, g->ptanks, g->size * sizeof(ptank_context));
            ptank += g->size;

            if (sxPTankPoolFree)
            {
                ptank_context* p = g->ptanks;
                ptank_context* endp = p + g->size;
                while (p != endp)
                {
                    if (p->flags & 0x10000)
                    {
                        p->flags &= ~0x10000;
                    }
                    else if (!(p->flags & 0x20000))
                    {
                        RpMaterialSetTexture(p->ptank->geometry->matList.materials[0], NULL);
                        p->flags |= 0x20000;
                    }

                    p++;
                }

                g->used = 0;
                sort_buckets(*g);
            }
        }

        g++;
    }

    S32 render_flags = 0;
    qsort(ptanks, ptank - ptanks, sizeof(ptank_context), compare_ptanks);

    ptank_context* end_ptank = ptank;
    ptank = ptanks;
    while (ptank != end_ptank)
    {
        if (ptank->flags & 0x10000)
        {
            render_ptank(*ptank, render_flags);
        }

        ptank++;
    }

    if (render_flags & 0x1)
    {
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    }

    if (render_flags & 0x2)
    {
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    }

    iMemPopTemp(ptanks);

    if (sxPTankPoolFree)
    {
        current_sequence_index = 0;
    }

    if (HDR_brightening)
    {
        HDR_brightening = FALSE;
        iDrawSetFBMSK(0xFF000000, 0xFFFFFFFF, true);
    }
}

void ptank_pool::grab_block(ptank_group_type type)
{
    ptank = NULL;

    group_data& group = groups[type];
    if (group.used < group.size || create_ptanks(group, 1) >= 1)
    {
        ptank_context** end = group.buckets + group.buckets_used;

        {
            ptank_context** it = group.buckets + 2;
            while (it < end)
            {
                if (*it != NULL &&
                    (*it)->ptank->geometry->matList.materials[0]->texture == rs.texture &&
                    (*it)->dst_blend == rs.dst_blend && (*it)->src_blend == rs.src_blend &&
                    !(((*it)->flags ^ rs.flags) & 0x1))
                {
                    if (rs.flags & 0x4)
                    {
                        (*it)->order_group = order_group;
                        (*it)->order_index = order_index;
                    }
                    else
                    {
                        (*it)->order_group = 0;
                        (*it)->order_index = 0;
                    }

                    (*it)->sequence_index = current_sequence_index;
                    current_sequence_index++;
                    ptank = (*it)->ptank;
                    (*it)->flags = ((*it)->flags & 0xFFFF0000) | (rs.flags & 0xFFFF) | 0x10000;
                    *it = (*it)->next;
                    group.used++;
                    return;
                }

                it++;
            }
        }

        {
            ptank_context** it = group.buckets;
            while (it < end)
            {
                if (*it != NULL)
                {
                    if ((rs.flags & 0x1) && !((*it)->flags & 0x1))
                    {
                        destroy_ptank((*it)->ptank);
                        (*it)->ptank = create_ptank(group.create_flags);
                    }

                    ptank = (*it)->ptank;
                    RpMaterialSetTexture(ptank->geometry->matList.materials[0], rs.texture);
                    RPATOMICPTANKPLUGINDATA(ptank)->publicData.srcBlend = rs.src_blend;
                    RPATOMICPTANKPLUGINDATA(ptank)->publicData.dstBlend = rs.dst_blend;
                    RPATOMICPTANKPLUGINDATA(ptank)->instFlags |= rpPTANKIFLAGALPHABLENDING;

                    (*it)->src_blend = rs.src_blend;
                    (*it)->dst_blend = rs.dst_blend;

                    if (rs.flags & 0x4)
                    {
                        (*it)->order_group = order_group;
                        (*it)->order_index = order_index;
                    }
                    else
                    {
                        (*it)->order_group = 0;
                        (*it)->order_index = 0;
                    }

                    (*it)->sequence_index = current_sequence_index;
                    current_sequence_index++;
                    (*it)->flags = rs.flags | 0x10000;
                    *it = (*it)->next;
                    group.used++;
                    return;
                }

                it++;
            }
        }
    }
}
