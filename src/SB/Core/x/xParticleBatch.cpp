#include "xParticleBatch.h"
#include "xPtankPool.h"
#include "xGlobals.h"
#include "xMemMgr.h"
#include "xutil.h"
#include "xDebug.h"

#include <PowerPC_EABI_Support/MSL_C/MSL_Common/stdlib.h>
#include <string.h>

// Functions are in the original source order (PS2 DWARF line order); retail emits them in
// reverse.

namespace
{
    // Every system's particles live in fixed-size blocks carved out of block_buffer.
    const S32 BLOCK_SIZE = 0x500;
    const S32 MAX_BLOCKS = 32;
    const S32 MAX_SYSTEMS = 128;
    const S32 MAX_BUCKETS = 128;

    struct block_info
    {
        U8* mem;
        S32 used;
        block_info* next;
    };

    struct internal_system : xParticleBatchSystem
    {
        block_info* block_list;
        S32 blocks_used;
        S32 units_used;
        S32 units_per_block;
        block_info* best_block;
        internal_system* next;
    };

    // Systems sharing a render state (and order, for sorted states) are rendered in one batch.
    struct system_bucket
    {
        RwTexture* texture;
        union
        {
            S32 rskey;
            struct
            {
                U8 src_blend;
                U8 dst_blend;
                U16 flags;
            };
        };
        U32 order_group;
        S32 order_index;
        internal_system* systems;
    };

    internal_system systems[MAX_SYSTEMS];
    system_bucket buckets[MAX_PGT][MAX_BUCKETS];
    block_info* head_free_block;
    U8* block_buffer;
    block_info block_info_buffer[MAX_BLOCKS];
    S32 systems_size;
    S32 buckets_size[MAX_PGT];

    struct compare_buckets
    {
        S32 operator()(const system_bucket& b1, const system_bucket& b2) const
        {
            if (b1.texture != b2.texture)
            {
                return b1.texture - b2.texture;
            }

            // Retail compares b1's sort flag with itself, so this never decides the order.
            S32 ordered1 = b1.flags & 0x4;
            S32 ordered2 = b1.flags & 0x4;

            if (ordered1 != ordered2)
            {
                return ordered1 - ordered2;
            }

            if (b1.order_group != b2.order_group)
            {
                return b1.order_group - b2.order_group;
            }

            if (b1.order_index != b2.order_index)
            {
                return b1.order_index - b2.order_index;
            }

            return b1.rskey - b2.rskey;
        }

        // The casts are passed directly: reference locals would hoist the pivot's texture load in
        // quicksort, which retail does not.
        S32 operator()(void* e1, void* e2) const
        {
            return (*this)(*(const system_bucket*)e1, *(const system_bucket*)e2);
        }
    };

    inline block_info* alloc_block()
    {
        block_info* b = head_free_block;
        head_free_block = b->next;
        return b;
    }

    inline void free_block(block_info* b)
    {
        b->next = head_free_block;
        head_free_block = b;
    }

    inline void add_to_bucket(internal_system& isys)
    {
        S32& bsize = buckets_size[isys.pool_type];
        system_bucket* begin_bucket = buckets[isys.pool_type];
        system_bucket* end_bucket = begin_bucket + bsize;

        system_bucket key;
        key.texture = isys.rs.texture;
        key.src_blend = isys.rs.src_blend;
        key.dst_blend = isys.rs.dst_blend;
        key.flags = isys.rs.flags;
        key.order_group = isys.order_group;
        key.order_index = isys.order_index;

        system_bucket* bucket = binary_find(begin_bucket, end_bucket, key, compare_buckets());

        if (bucket == end_bucket)
        {
            *bucket = key;
            bucket->systems = NULL;
            ++bsize;
        }

        isys.next = bucket->systems;
        bucket->systems = &isys;

        if (bucket == end_bucket)
        {
            quicksort(begin_bucket, bsize, sizeof(system_bucket), compare_buckets());
        }
    }

    // Combines the two least used blocks of the system when they fit in one. The assert and the
    // total/tail locals are Ratatouille's; with them the function is too big to auto-inline, as
    // retail calls it from every pre_render_buckets<T>.
    void merge_blocks(internal_system& system)
    {
        block_info* first;
        block_info* second;
        block_info* s0;
        block_info* s1;
        block_info** ps0;
        block_info* b;
        block_info** pb;
        S32 total;
        U8* tail;

        xASSERT(system.blocks_used >= 2);

        first = system.block_list;
        second = first->next;

        if (first->used < second->used)
        {
            s0 = first;
            s1 = second;
            ps0 = &system.block_list;
        }
        else
        {
            s0 = second;
            s1 = first;
            ps0 = &first->next;
        }

        for (b = second->next, pb = &second->next; b != NULL; pb = &b->next, b = b->next)
        {
            if (b->used < s0->used)
            {
                s1 = s0;
                s0 = b;
                ps0 = pb;
            }
            else if (b->used < s1->used)
            {
                s1 = b;
            }
        }

        total = s0->used + s1->used;

        if (total <= system.units_per_block)
        {
            tail = s1->mem + s1->used * system.stride;
            memcpy(tail, s0->mem, s0->used * system.stride);
            s1->used += s0->used;
            *ps0 = s0->next;
            free_block(s0);
            system.blocks_used--;
        }
    }

    // Releases the blocks left empty by an update.
    inline void refresh_blocks(internal_system& system)
    {
        block_info** pb = &system.block_list;
        block_info* b = system.block_list;

        while (b != NULL)
        {
            if (b->used <= 0)
            {
                *pb = b->next;
                free_block(b);
                system.blocks_used--;
            }
            else
            {
                pb = &b->next;
            }

            b = *pb;
        }
    }

    template <class T> void pre_render_buckets(system_bucket* first, system_bucket* last, F32 dt)
    {
        T pool;

        for (system_bucket* bucket = first; bucket != last; bucket++)
        {
            internal_system* system = bucket->systems;
            U8 started = FALSE;

            while (system != NULL)
            {
                if (system->units_used > 0)
                {
                    if (!started)
                    {
                        started = TRUE;
                        pool.rs = system->rs;
                        pool.order_group = system->order_group;
                        pool.order_index = system->order_index;
                        pool.reset();
                    }

                    U8 lost_any = FALSE;

                    for (block_info* block = system->block_list; block != NULL; block = block->next)
                    {
                        S32 newused =
                            system->update(block->mem, block->used, pool, dt, system->context);

                        if (newused < block->used)
                        {
                            lost_any = TRUE;
                        }

                        system->units_used += newused - block->used;
                        block->used = newused;
                    }

                    if (lost_any)
                    {
                        system->best_block = NULL;
                        refresh_blocks(*system);

                        if (system->blocks_used > 1)
                        {
                            merge_blocks(*system);
                        }
                    }
                }

                system = system->next;
            }

            if (started)
            {
                pool.flush();
            }
        }
    }
} // namespace

void xParticleBatchSceneEnter()
{
    block_buffer = (U8*)xMemAlloc(gActiveHeap, BLOCK_SIZE * MAX_BLOCKS, 0);
    systems_size = 0;
    memset(buckets_size, 0, sizeof(buckets_size));
    xParticleBatchReset();
}

void xParticleBatchSceneExit()
{
}

void xParticleBatchReset()
{
    internal_system* sys = systems;
    internal_system* end_sys = sys + systems_size;

    for (; sys != end_sys; sys++)
    {
        sys->block_list = NULL;
        sys->blocks_used = 0;
        sys->units_used = 0;
        sys->best_block = NULL;
    }

    head_free_block = block_info_buffer;

    block_info* b = block_info_buffer;
    block_info* endb = b + (MAX_BLOCKS - 1);
    U8* mem = block_buffer;

    for (; b != endb; b++)
    {
        b->mem = mem;
        b->next = b + 1;
        mem += BLOCK_SIZE;
    }

    endb->mem = mem;
    endb->next = NULL;
}

void xParticleBatchPreRender()
{
    F32 dt = xglobals->update_dt;

    pre_render_buckets<ptank_pool__color_mat>(
        buckets[PGT_COLOR_MAT], buckets[PGT_COLOR_MAT] + buckets_size[PGT_COLOR_MAT], dt);
    pre_render_buckets<ptank_pool__color_mat_uv2>(
        buckets[PGT_COLOR_MAT_UV2], buckets[PGT_COLOR_MAT_UV2] + buckets_size[PGT_COLOR_MAT_UV2],
        dt);
    pre_render_buckets<ptank_pool__pos_color_size>(
        buckets[PGT_POS_COLOR_SIZE], buckets[PGT_POS_COLOR_SIZE] + buckets_size[PGT_POS_COLOR_SIZE],
        dt);
    pre_render_buckets<ptank_pool__pos_color_size_uv2>(
        buckets[PGT_POS_COLOR_SIZE_UV2],
        buckets[PGT_POS_COLOR_SIZE_UV2] + buckets_size[PGT_POS_COLOR_SIZE_UV2], dt);
    pre_render_buckets<ptank_pool__pos_color_size_rot>(
        buckets[PGT_POS_COLOR_SIZE_ROT],
        buckets[PGT_POS_COLOR_SIZE_ROT] + buckets_size[PGT_POS_COLOR_SIZE_ROT], dt);
    pre_render_buckets<ptank_pool__pos_color_size_rot_uv2>(
        buckets[PGT_POS_COLOR_SIZE_ROT_UV2],
        buckets[PGT_POS_COLOR_SIZE_ROT_UV2] + buckets_size[PGT_POS_COLOR_SIZE_ROT_UV2], dt);
}

S32 xParticleBatchAddSystem(const xParticleBatchSystem& system)
{
    S32 system_id = systems_size;
    systems_size++;
    internal_system& isys = systems[system_id];

    (xParticleBatchSystem&)isys = system;
    isys.block_list = NULL;
    isys.blocks_used = 0;
    isys.units_used = 0;
    isys.units_per_block = BLOCK_SIZE / isys.stride;
    isys.best_block = NULL;

    if (!(isys.rs.flags & 0x4))
    {
        isys.order_group = 0;
        isys.order_index = 0;
    }

    add_to_bucket(isys);
    return system_id;
}

namespace
{
    // Returns the fullest block that still has room, grabbing a free block if there is none.
    inline block_info* grab_best_block(internal_system& isys)
    {
        if (isys.best_block != NULL)
        {
            return isys.best_block;
        }

        S32 most_used = -1;

        for (block_info* b = isys.block_list; b != NULL; b = b->next)
        {
            if (b->used < isys.units_per_block && most_used < b->used)
            {
                isys.best_block = b;
                most_used = b->used;
            }
        }

        if (isys.best_block != NULL)
        {
            return isys.best_block;
        }

        if (head_free_block == NULL)
        {
            return NULL;
        }

        isys.best_block = alloc_block();
        isys.best_block->used = 0;
        isys.best_block->next = isys.block_list;
        isys.block_list = isys.best_block;
        isys.blocks_used++;
        return isys.best_block;
    }
} // namespace

S32 xParticleBatchEmit(S32 system_id, S32 amount, U8** mem)
{
    internal_system& isys = systems[system_id];
    block_info* block = grab_best_block(isys);

    if (block == NULL)
    {
        return 0;
    }

    S32 remain = isys.units_per_block - block->used;
    *mem = block->mem + block->used * isys.stride;

    if (amount >= remain)
    {
        amount = remain;
        isys.best_block = NULL;
    }

    block->used += amount;
    isys.units_used += amount;
    return amount;
}

void xParticleBatchGroup::create()
{
    elements = (U8*)xMemAlloc(gActiveHeap, 16, 0);
    elements_size = 0;
}

namespace
{
    inline S32 compare_systems(const xParticleBatchSystem& sys1, const xParticleBatchSystem& sys2)
    {
        if (sys1.pool_type != sys2.pool_type)
        {
            return sys1.pool_type - sys2.pool_type;
        }

        // Pointers compare as their low 32 bits, as retail does; the sort only groups equal ones.
        if (sys1.rs.texture != sys2.rs.texture)
        {
            return (S32)(SPtr)sys1.rs.texture - (S32)(SPtr)sys2.rs.texture;
        }

        if (sys1.rs.src_blend != sys2.rs.src_blend)
        {
            return sys1.rs.src_blend - sys2.rs.src_blend;
        }

        if (sys1.rs.dst_blend != sys2.rs.dst_blend)
        {
            return sys1.rs.dst_blend - sys2.rs.dst_blend;
        }

        if (sys1.rs.flags != sys2.rs.flags)
        {
            return sys1.rs.flags - sys2.rs.flags;
        }

        if (sys1.rs.flags & 0x4)
        {
            if (sys1.order_group != sys2.order_group)
            {
                return sys1.order_group - sys2.order_group;
            }

            if (sys1.order_index != sys2.order_index)
            {
                return sys1.order_index - sys2.order_index;
            }
        }

        if (sys1.stride != sys2.stride)
        {
            return sys1.stride - sys2.stride;
        }

        if (sys1.update != sys2.update)
        {
            return (S32)(SPtr)sys1.update - (S32)(SPtr)sys2.update;
        }

        return (S32)(SPtr)sys1.context - (S32)(SPtr)sys2.context;
    }

    struct compare_group_systems_functor
    {
        S32 operator()(const xParticleBatchSystem& system, const U8& system_id) const
        {
            return compare_systems(system, systems[system_id]);
        }
    };

    S32 compare_group_systems_function(const void* e1, const void* e2)
    {
        S32 sysid1 = *(const U8*)e1;
        S32 sysid2 = *(const U8*)e2;
        return compare_systems(systems[sysid1], systems[sysid2]);
    }
} // namespace

S32 xParticleBatchGroup::create_system(const xParticleBatchSystem& system)
{
    const U8* end_elements = elements + elements_size;
    const U8* found =
        binary_find((const U8*)elements, end_elements, system, compare_group_systems_functor());

    if (found != end_elements)
    {
        return *found;
    }

    S32 system_id = xParticleBatchAddSystem(system);
    elements[elements_size] = system_id;
    elements_size++;
    qsort(elements, elements_size, sizeof(U8), compare_group_systems_function);
    return system_id;
}
