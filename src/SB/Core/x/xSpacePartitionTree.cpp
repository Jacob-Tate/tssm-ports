#include "xSpacePartitionTree.h"

#include "xMath.h"
#include "xMemMgr.h"

#include <math.h>

void xSpacePartitionTree2D::create(F32 minx, F32 miny, F32 maxx, F32 maxy, F32 min_width,
                                   F32 max_width, S32 max_entries)
{
    origin.x = minx;
    origin.y = miny;
    size.x = maxx - minx;
    size.y = maxy - miny;

    grid_shift_x = get_grid_shift(size.x, max_width);
    grid_size_x = 1 << grid_shift_x;
    grid_mask_x = grid_size_x - 1;
    grid_shift_y = get_grid_shift(size.y, max_width);
    grid_size_y = 1 << grid_shift_y;
    grid_mask_y = grid_size_y - 1;

    xVec2 gsize = { size.x / grid_size_x, size.y / grid_size_y };
    cell_size[0] = MAX(gsize.x, gsize.y);
    cell_isize[0] = 1.0f / cell_size[0];
    cell_frac[0] = 1.0f;
    size.assign(cell_size[0] * grid_size_x, cell_size[0] * grid_size_y);

    ilogdepth = -1.0f / (F32)log(2.0);

    if (min_width > cell_size[0])
    {
        max_depth = 0;
    }
    else
    {
        max_depth = ilogdepth * (F32)log(min_width / cell_size[0]);
    }

    if (max_depth > 8)
    {
        max_depth = 8;
    }

    F32* cs = cell_size + 1;
    F32* end_cs = cs + max_depth;
    F32* ics = cell_isize + 1;
    F32* cf = cell_frac + 1;
    for (; cs != end_cs; ++cs, ++ics, ++cf)
    {
        *cs = 0.5f * cs[-1];
        *ics = 2.0f * ics[-1];
        *cf = 0.5f * cf[-1];
    }

    node_buffer_size = max_entries * 4;
    node_buffer = (entry_node*)xMemAlloc(gActiveHeap, node_buffer_size * sizeof(entry_node), 0);
    entry_buffer_size = max_entries;
    entry_buffer = (entry_data*)xMemAlloc(gActiveHeap, entry_buffer_size * sizeof(entry_data), 0);

    cell_buffer_size = 4.0f * max_entries;
    cell_buffer_size = (cell_buffer_size + 3) / 4 * 4 + grid_size_x * grid_size_y;
    cell_buffer = (cell*)xMemAlloc(gActiveHeap, cell_buffer_size * sizeof(cell), 0);
    grid = cell_buffer;

    clear();
}

void xSpacePartitionTree2D::clear()
{
    cell* gc = grid;
    cell* end_gc = gc + grid_size_x * grid_size_y;
    for (; gc != end_gc; ++gc)
    {
        create_cell(*gc, 0xffff, 0);
    }

    free_entries = entry_buffer;
    entry_data* fe = entry_buffer;
    entry_data* end_fe = fe + entry_buffer_size - 1;
    for (; fe != end_fe; ++fe)
    {
        fe->next_free_entry = fe + 1;
    }
    end_fe->next_free_entry = NULL;

    free_nodes = node_buffer;
    entry_node* fn = node_buffer;
    entry_node* end_fn = fn + node_buffer_size - 1;
    for (; fn != end_fn; ++fn)
    {
        fn->next_free_node = fn + 1;
    }
    end_fn->next_free_node = NULL;

    S32 grid_size = grid_size_x * grid_size_y;
    free_cells = cell_buffer + grid_size;
    cell* fc = free_cells;
    cell* end_fc = fc + cell_buffer_size - grid_size - 4;
    while (fc != end_fc)
    {
        fc->next_free_cell = fc + 4;
        fc = fc->next_free_cell;
    }
    end_fc->next_free_cell = NULL;

    free_cells_size = cell_buffer_size - grid_size;
    safety_free_cells_size = (max_depth + 1) * 4;
    current_searchid = 0;
    reset_searchid();
}

void xSpacePartitionTree2D::reset_searchid()
{
    current_searchid = 1;

    entry_data* e = entry_buffer;
    entry_data* end = e + entry_buffer_size;
    for (; e != end; ++e)
    {
        e->searchid = 0;
    }
}

// Dead-stripped in retail: the two non-template intersect_rect front ends and their callback
// wrappers (as in Ratatouille; the PS2 line table has a gap here). Nothing in TSSM calls them
// (zFire uses the templates directly).
namespace
{
    struct cb_fp_wrapper
    {
        bool (*cb)(xSpacePartitionTree2D::entry_data*, void*);
        void* context;

        cb_fp_wrapper(bool (*cb)(xSpacePartitionTree2D::entry_data*, void*), void* context)
            : cb(cb), context(context)
        {
        }

        bool operator()(xSpacePartitionTree2D::entry_data* e) const
        {
            return cb(e, context);
        }
    };

    struct cb_accum_wrapper
    {
        xSpacePartitionTree2D::entry_data** results;
        xSpacePartitionTree2D::entry_data** end_results;
        S32 maxResults;

        cb_accum_wrapper(xSpacePartitionTree2D::entry_data** results, S32 max_results)
            : results(results), end_results(results + max_results), maxResults(max_results)
        {
        }

        // Ratatouille's debug build reports an overflow here (compiled out in retail).
        bool operator()(xSpacePartitionTree2D::entry_data* e)
        {
            if (results == end_results)
            {
                return false;
            }

            *results = e;
            results++;
            return true;
        }
    };
} // namespace

void xSpacePartitionTree2D::intersect_rect(F32 x0, F32 y0, F32 x1, F32 y1,
                                           bool (*cb)(entry_data*, void*), void* context)
{
    cb_fp_wrapper fp_wrapper(cb, context);
    intersect_rect(x0, y0, x1, y1, fp_wrapper);
}

S32 xSpacePartitionTree2D::intersect_rect(entry_data** results, S32 max_results, F32 x0, F32 y0,
                                          F32 x1, F32 y1)
{
    cb_accum_wrapper accum_wrapper(results, max_results);
    intersect_rect(x0, y0, x1, y1, accum_wrapper);
    return accum_wrapper.results - results;
}

void xSpacePartitionTree2D::insert_internal(entry_data* e, F32 x0, F32 y0, F32 x1, F32 y1,
                                            F32 width)
{
    F32 scale = cell_isize[0];
    x0 = scale * (x0 - origin.x);
    y0 = scale * (y0 - origin.y);
    x1 = scale * (x1 - origin.x);
    y1 = scale * (y1 - origin.y);
    width = scale * width;

    S32 gx0 = CLAMP((S32)x0, 0, grid_size_x - 1);
    S32 gy0 = CLAMP((S32)y0, 0, grid_size_y - 1);
    S32 gx1 = CLAMP((S32)x1, 0, grid_size_x - 1);
    S32 gy1 = CLAMP((S32)y1, 0, grid_size_y - 1);

    S32 end_depth;
    if (width < 0.0001f)
    {
        end_depth = max_depth;
    }
    else
    {
        end_depth = ilogdepth * (F32)log(width);
        if (end_depth > max_depth)
        {
            end_depth = max_depth;
        }
    }

    e->owner_size = 0;

    for (S32 gy = gy0; gy <= gy1; gy++)
    {
        for (S32 gx = gx0; gx <= gx1; gx++)
        {
            insert_internal(e, &grid_cell(gx, gy), end_depth, x0 - gx, y0 - gy, x1 - gx, y1 - gy);
        }
    }

    e->searchid = current_searchid;
}

void xSpacePartitionTree2D::insert_internal(entry_data* e, cell* c, S32 end_depth, F32 x0, F32 y0,
                                            F32 x1, F32 y1)
{
    S32 index = 0;
    F32 dxy = cell_frac[1];
    F32 cx0 = 0.0f;
    F32 cy0 = 0.0f;
    F32 cx1 = 1.0f;
    F32 cy1 = 1.0f;
    S32 depth = 0;
    struct
    {
        F32 cx0;
        F32 cy0;
        F32 cx1;
        F32 cy1;
    } bound_stack[8];
    cell* const cell_list = cell_buffer;
    entry_node* const node_list = node_buffer;
    entry_data* const entry_list = entry_buffer;

    for (;;)
    {
    descend:
        if (depth == end_depth)
        {
            e->owner[e->owner_size] = c - cell_list;

            entry_node* n = free_nodes;
            free_nodes = n->next_free_node;
            n->entry = e - entry_list;
            n->next = c->entries;
            c->entries = n - node_list;

            e->owner_size++;
        }
        else
        {
            for (; index < 4; ++index)
            {
                F32 ccx0 = cx0 + dxy * (index & 1);
                F32 ccy0 = cy0 + dxy * (index / 2);
                F32 ccx1 = ccx0 + dxy;
                F32 ccy1 = ccy0 + dxy;

                if (ccx0 > x1 || ccy0 > y1 || ccx1 < x0 || ccy1 < y0)
                {
                    continue;
                }

                if (c->children == 0xffff)
                {
                    create_children(*c);
                }

                bound_stack[depth].cx0 = cx0;
                bound_stack[depth].cy0 = cy0;
                bound_stack[depth].cx1 = cx1;
                bound_stack[depth].cy1 = cy1;
                cx0 = ccx0;
                cy0 = ccy0;
                cx1 = ccx1;
                cy1 = ccy1;
                depth++;
                dxy = cell_frac[depth + 1];
                c = cell_list + c->children + index;
                index = 0;
                // continue the outer loop with the child (retail's descend edge skips the loop head)
                goto descend;
            }
        }

        if (depth <= 0)
        {
            return;
        }

        depth--;
        index = c->cid + 1;
        dxy = cell_frac[depth + 1];
        cx0 = bound_stack[depth].cx0;
        cy0 = bound_stack[depth].cy0;
        cx1 = bound_stack[depth].cx1;
        cy1 = bound_stack[depth].cy1;
        c = cell_list + c->parent;
    }
}

void xSpacePartitionTree2D::erase_internal(entry_data* e)
{
    cell* const cell_list = cell_buffer;
    entry_node* const node_list = node_buffer;
    entry_data* const entry_list = entry_buffer;
    U16* owner = e->owner;
    U16* end_owner = owner + e->owner_size;

    for (; owner != end_owner; ++owner)
    {
        cell* c = cell_list + *owner;
        entry_node* prev = NULL;
        entry_node* n = node_list + c->entries;
        U16 key = e - entry_list;

        while (true)
        {
            if (n->entry == key)
            {
                break;
            }

            prev = n;
            n = node_list + n->next;
        }

        if (prev == NULL)
        {
            c->entries = n->next;
        }
        else
        {
            prev->next = n->next;
        }

        free_node(n);
        remove_empty(c);
    }
}

// Dead-stripped placeholder, not recovered code: retail's .sdata2 interns -1e-5f after
// erase_internal's code, from stripped functions after it that no TSSM build keeps
// (Ratatouille has its debug and line helpers there). Their bodies are unknown.
static void __deadstripped_xSpacePartitionTree(F32* out)
{
    out[0] = -1e-5f;
}
