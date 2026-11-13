#ifndef XSPACEPARTITIONTREE_H
#define XSPACEPARTITIONTREE_H

#include <types.h>

#include "xMath2.h"

struct xSpacePartitionTree2D
{
    // Free-list links overlay the whole record (GC: cell stride 0xC, entry_node 4 bytes).
    union entry_data
    {
        entry_data* next_free_entry;
        struct
        {
            void* value;
            S32 type;
            U16 searchid;
            U16 owner_size;
            U16 owner[4];
        };
    };

    union entry_node
    {
        entry_node* next_free_node;
        struct
        {
            U16 next;
            U16 entry;
        };
    };

    union cell
    {
        cell* next_free_cell;
        struct
        {
            U16 parent;
            U16 children;
            U16 entries;
            U8 cid;
            U8 level;
            U16 xcode;
            U16 ycode;
        };
    };

    xVec2 origin;
    xVec2 size;
    F32 cell_size[9];
    F32 cell_isize[9];
    F32 cell_frac[9];
    S32 max_depth;
    F32 ilogdepth;
    S32 grid_size_x;
    S32 grid_shift_x;
    S32 grid_mask_x;
    S32 grid_size_y;
    S32 grid_shift_y;
    S32 grid_mask_y;
    U16 current_searchid;
    entry_data* free_entries; // 0xA0
    entry_node* free_nodes;
    cell* free_cells;
    S32 free_cells_size;
    S32 safety_free_cells_size;
    cell* grid;
    entry_data* entry_buffer;
    S32 entry_buffer_size;
    entry_node* node_buffer;
    S32 node_buffer_size;
    cell* cell_buffer;
    S32 cell_buffer_size;

    void create(F32 x0, F32 y0, F32 x1, F32 y1, F32 min_cell_size, F32 max_cell_size,
                S32 max_entries);
    void clear();
    void reset_searchid();
    // No TSSM symbols (dead-stripped in retail); declared as in the Ratatouille GC DWARF.
    void intersect_rect(F32 x0, F32 y0, F32 x1, F32 y1, bool (*cb)(entry_data*, void*),
                        void* context);
    S32 intersect_rect(entry_data** results, S32 max_results, F32 x0, F32 y0, F32 x1, F32 y1);
    void erase_internal(entry_data* e);
    void insert_internal(entry_data* e, F32 x0, F32 y0, F32 x1, F32 y1, F32 width);
    void insert_internal(entry_data* e, cell* c, S32 end_depth, F32 x0, F32 y0, F32 x1, F32 y1);
    // remove_empty, create_cell and create_children have no GC symbols (always inlined).
    void remove_empty(cell* c)
    {
        cell* const cell_list = cell_buffer;
        U16 parent = c->parent;

        while (parent != 0xffff)
        {
            cell* pc = cell_list + parent;
            cell* bro = cell_list + pc->children;
            parent = pc->parent;

            for (cell* end_bro = bro + 4; bro != end_bro; ++bro)
            {
                if (bro->entries != 0xffff || bro->children != 0xffff)
                {
                    return;
                }
            }

            bro = cell_list + pc->children;
            bro->next_free_cell = free_cells;
            free_cells = bro;
            free_cells_size += 4;
            pc->children = 0xffff;
        }
    }

    void create_cell(cell& c, U16 parent, S32 cid)
    {
        c.parent = parent;
        c.children = 0xffff;
        c.entries = 0xffff;
        c.cid = cid;

        if (parent == 0xffff)
        {
            c.level = max_depth;
            c.xcode = c.ycode = 0;
        }
        else
        {
            cell* pc = cell_buffer + parent;
            c.level = pc->level - 1;
            // retail truncates the codes to 8 bits before each store
            c.xcode = (U8)(pc->xcode | ((cid & 1) << c.level));
            c.ycode = (U8)(pc->ycode | (((cid >> 1) & 1) << c.level));
        }
    }

    void create_children(cell& c)
    {
        cell* const cell_list = cell_buffer;
        cell* cc = alloc_children();
        c.children = cc - cell_list;

        cell* end_cc = cc + 4;
        U16 parent = &c - cell_list;
        for (; cc != end_cc; ++cc)
        {
            create_cell(*cc, parent, (cc - cell_list) - c.children);
        }
    }

    static S32 get_grid_shift(F32 size, F32 max_width)
    {
        S32 gsize = size / max_width;
        if (gsize >= 16)
        {
            return 4;
        }

        S32 shift = 0;
        while (gsize > 1)
        {
            gsize >>= 1;
            shift++;
        }
        return shift;
    }

    cell& grid_cell(S32 gx, S32 gy)
    {
        return grid[(gy << grid_shift_x) + gx];
    }

    // No TSSM symbol (always inlined); name from the Ratatouille GC DWARF.
    cell* alloc_children()
    {
        cell* c = free_cells;
        free_cells = c->next_free_cell;
        free_cells_size -= 4;
        return c;
    }

    void free_node(entry_node* e)
    {
        e->next_free_node = free_nodes;
        free_nodes = e;
    }

    bool full() const
    {
        return free_entries == NULL || free_cells_size < safety_free_cells_size;
    }

    entry_data* insert(void* value, S32 type, F32 x, F32 y, F32 r)
    {
        entry_data* e = free_entries;
        free_entries = e->next_free_entry;
        e->value = value;
        e->type = type;
        insert_internal(e, x - r, y - r, x + r, y + r, 2.0f * r);
        return e;
    }

    void erase(entry_data* e)
    {
        erase_internal(e);
        e->next_free_entry = free_entries;
        free_entries = e;
    }

    // No TSSM symbol (always inlined); name and params from the Ratatouille GC DWARF.
    void move(entry_data* e, F32 x, F32 y, F32 r)
    {
        erase_internal(e);
        insert_internal(e, x - r, y - r, x + r, y + r, 2.0f * r);
    }

    template <class Callback> void intersect_rect(F32 x0, F32 y0, F32 x1, F32 y1, Callback& cb);
    template <class Callback>
    bool intersect_rect(cell* c, F32 x0, F32 y0, F32 x1, F32 y1, Callback& cb);

    template <class Callback> void intersect_circle(F32 x, F32 y, F32 r, Callback& cb);
};

// Dead-stripped; Ratatouille lists this static as unused in every includer. Its out-of-line copy
// opens this header's .text section, which puts __sinit_xWad5_cpp where retail has it. The body
// is reconstructed (the comparison strictness is unknown).
static bool line_vs_square(F32 tx0, F32 ty0, F32 tx1, F32 ty1)
{
    F32 tmin = MAX(tx0, ty0);
    F32 tmax = MIN(tx1, ty1);
    if (tmin < tmax && tmax >= 0.0f && tmin <= 1.0f)
    {
        return true;
    }
    return false;
}

template <class Callback>
void xSpacePartitionTree2D::intersect_circle(F32 x, F32 y, F32 r, Callback& cb)
{
    intersect_rect(x - r, y - r, x + r, y + r, cb);
}

template <class Callback>
void xSpacePartitionTree2D::intersect_rect(F32 x0, F32 y0, F32 x1, F32 y1, Callback& cb)
{
    current_searchid++;
    if (current_searchid == 0)
    {
        reset_searchid();
    }

    F32 scale = cell_isize[0];
    x0 = scale * (x0 - origin.x);
    y0 = scale * (y0 - origin.y);
    x1 = scale * (x1 - origin.x);
    y1 = scale * (y1 - origin.y);

    S32 gx0 = CLAMP((S32)x0, 0, grid_size_x - 1);
    S32 gy0 = CLAMP((S32)y0, 0, grid_size_y - 1);
    S32 gx1 = CLAMP((S32)x1, 0, grid_size_x - 1);
    S32 gy1 = CLAMP((S32)y1, 0, grid_size_y - 1);

    for (S32 gy = gy0; gy <= gy1; gy++)
    {
        for (S32 gx = gx0; gx <= gx1; gx++)
        {
            if (!intersect_rect(&grid_cell(gx, gy), x0 - gx, y0 - gy, x1 - gx, y1 - gy, cb))
            {
                break;
            }
        }
    }
}

template <class Callback>
bool xSpacePartitionTree2D::intersect_rect(cell* c, F32 x0, F32 y0, F32 x1, F32 y1, Callback& cb)
{
    F32 code_scale = 1 << max_depth;
    S32 xcode0 = code_scale * x0;
    S32 ycode0 = code_scale * y0;
    S32 xcode1 = code_scale * x1;
    S32 ycode1 = code_scale * y1;
    cell* const cell_list = cell_buffer;
    entry_node* const node_list = node_buffer;
    entry_data* const entry_list = entry_buffer;
    S32 diff = (xcode0 ^ xcode1) | (ycode0 ^ ycode1);
    S32 level = max_depth;

    // Descend while the whole rect stays inside one child; everything stored on the way
    // overlaps the rect.
    for (;;)
    {
        level--;
        S32 bit = 1 << level;
        if (diff & bit)
        {
            break;
        }

        U16 _i = c->entries;
        while (_i != 0xffff)
        {
            entry_node* const _n = node_list + _i;
            entry_data* const _e = entry_list + _n->entry;
            if (!cb(_e))
            {
                return false;
            }
            _i = _n->next;
        }

        if (c->children == 0xffff)
        {
            return true;
        }

        S32 index = ((xcode0 & bit) >> level) | (((ycode0 & bit) >> level) << 1);
        c = cell_list + c->children + index;
    }

    // Walk the subtree under topc, visiting every overlapping cell once.
    U16 searchid = current_searchid;
    cell* topc = c;
    S32 index = 0;

    for (;;)
    {
    next_cell:
        U16 _i = c->entries;
        while (_i != 0xffff)
        {
            entry_node* _n = node_list + _i;
            entry_data* const _e = entry_list + _n->entry;
            if (_e->searchid != searchid)
            {
                _e->searchid = searchid;
                if (!cb(_e))
                {
                    return false;
                }
            }
            _i = _n->next;
        }

        for (;;)
        {
            if (c->children != 0xffff)
            {
                for (; index < 4; index++)
                {
                    cell* cc = cell_list + c->children + index;
                    S32 code_width = 1 << cc->level;
                    S32 left = cc->xcode;
                    S32 right = left + code_width;
                    S32 top = cc->ycode;
                    S32 bottom = top + code_width;

                    if (left > xcode1 || right <= xcode0 || top > ycode1 || bottom <= ycode0)
                    {
                        continue;
                    }

                    c = cc;
                    index = 0;
                    // restart the walk with the child (retail jumps back to the entry scan)
                    goto next_cell;
                }
            }

            if (c == topc)
            {
                return true;
            }

            index = c->cid + 1;
            c = cell_list + c->parent;
        }
    }
}

#endif
