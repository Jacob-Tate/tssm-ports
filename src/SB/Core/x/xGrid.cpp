#include "xGrid.h"

#include "zGrid.h"

#include <string.h>

void xGridInit(xGrid* grid, const xBox* bounds, U16 nx, U16 nz, U8 ingrid_id)
{
    grid->ingrid_id = ingrid_id;
    grid->nx = nx;
    grid->nz = nz;
    grid->minx = bounds->lower.x;
    grid->minz = bounds->lower.z;
    grid->maxx = bounds->upper.x;
    grid->maxz = bounds->upper.z;

    F32 gsizex = grid->maxx - grid->minx;
    F32 gsizez = grid->maxz - grid->minz;

    grid->csizex = gsizex / nx;
    grid->csizez = gsizez / nz;

    if (xabs(gsizex) <= 0.001f)
    {
        grid->inv_csizex = 1.0f;
    }
    else
    {
        grid->inv_csizex = nx / gsizex;
    }

    if (xabs(gsizez) <= 0.001f)
    {
        grid->inv_csizez = 1.0f;
    }
    else
    {
        grid->inv_csizez = nz / gsizez;
    }

    grid->iter_active = 0;
    grid->maxr = 0.25f * MIN(grid->csizex, grid->csizez);
    grid->cells = (xGridBound**)xMemAllocSize(nx * nz * sizeof(xGridBound*));
    // retail (and the PS2 demo) recompute nx * nz here rather than reusing the allocation size
    memset(grid->cells, 0, (U32)nx * nz * sizeof(xGridBound*));
}

void xGridKill(xGrid* grid)
{
    xGridEmpty(grid);
    grid->cells = NULL;
    grid->iter_active = 0;
}

void xGridEmpty(xGrid* grid)
{
    xGridBound** head;
    xGridBound* curr;
    S32 x;
    S32 z;

    for (x = 0; x < grid->nx; ++x)
    {
        for (z = 0; z < grid->nz; ++z)
        {
            head = &grid->cells[z * grid->nx] + x;
            curr = *head;
            while (curr)
            {
                xGridBound* currnext = curr->next;
                xGridBoundInit(curr, curr->data);
                curr = currnext;
            }
            *head = NULL;
        }
    }

    curr = grid->other;
    while (curr)
    {
        xGridBound* currnext = curr->next;
        xGridBoundInit(curr, curr->data);
        curr = currnext;
    }
    grid->other = NULL;
}

S32 xGridAddToCell(xGridBound** cell, xGridBound* gridb)
{
    if (gridb->head)
    {
        if (gridb->grid->iter_active == 0)
        {
            if (!xGridRemove(gridb))
            {
                return 0;
            }
        }
        else
        {
            return 0;
        }
    }

    gridb->head = cell;
    gridb->next = *cell;
    *cell = gridb;
    return 1;
}

S32 xGridAdd(xGrid* grid, xEnt* ent)
{
    xBound* bound;
    xVec3* center;
    F32 maxr;

    bound = &ent->bound;
    maxr = grid->maxr;

    if (bound->type == XBOUND_TYPE_SPHERE)
    {
        xSphere* sph = &bound->sph;
        center = &sph->center;
        if (sph->r >= maxr)
        {
            S32 r = xGridAddToCell(&grid->other, &ent->gridb);
            if (r)
            {
                ent->gridb.grid = grid;
            }
            return r;
        }
    }
    else if (bound->type == XBOUND_TYPE_OBB)
    {
        xBBox* bbox = &bound->box;
        center = &bbox->center;
        F32 rx = bbox->box.upper.x - bbox->box.lower.x;
        F32 ry = bbox->box.upper.y - bbox->box.lower.y;
        F32 rz = bbox->box.upper.z - bbox->box.lower.z;

        if (SQR(rx) * (SQR(bound->mat->right.x) + SQR(bound->mat->right.y) +
                       SQR(bound->mat->right.z)) +
                SQR(ry) * (SQR(bound->mat->up.x) + SQR(bound->mat->up.y) + SQR(bound->mat->up.z)) +
                SQR(rz) * (SQR(bound->mat->at.x) + SQR(bound->mat->at.y) + SQR(bound->mat->at.z)) >=
            4.0f * maxr * maxr)
        {
            S32 r = xGridAddToCell(&grid->other, &ent->gridb);
            if (r)
            {
                ent->gridb.grid = grid;
            }
            return r;
        }
    }
    else if (bound->type == XBOUND_TYPE_BOX)
    {
        xBBox* bbox = &bound->box;
        center = &bbox->center;
        F32 rx = bbox->box.upper.x - bbox->box.lower.x;
        F32 rz = bbox->box.upper.z - bbox->box.lower.z;

        if (SQR(rx) + SQR(rz) >= 4.0f * maxr * maxr)
        {
            S32 r = xGridAddToCell(&grid->other, &ent->gridb);
            if (r)
            {
                ent->gridb.grid = grid;
            }
            return r;
        }
    }
    else
    {
        return 0;
    }

    F32 cgridx = center->x - grid->minx;
    F32 cgridz = center->z - grid->minz;
    cgridx *= grid->inv_csizex;
    cgridz *= grid->inv_csizez;

    S32 x = MIN(grid->nx - 1, MAX(0.0f, cgridx));
    S32 z = MIN(grid->nz - 1, MAX(0.0f, cgridz));

    if (xGridAddToCell(&grid->cells[z * grid->nx] + x, &ent->gridb))
    {
        ent->gridb.gx = x;
        ent->gridb.gz = z;
        ent->gridb.grid = grid;
        return 1;
    }

    return 0;
}

S32 xGridRemove(xGridBound* gridb)
{
    if (gridb->head)
    {
        if (gridb->grid->iter_active)
        {
            gridb->deleted = 1;
            return 0;
        }
        else
        {
            xGridBound* cur = gridb->head[0];
            xGridBound** prev = gridb->head;
            while (cur && cur != gridb)
            {
                prev = &cur->next;
                cur = cur->next;
            }

            *prev = cur->next;
            cur->next = NULL;
            cur->head = NULL;
            cur->grid = NULL;
            cur->deleted = 0;
            cur->gx = -1;
            cur->gz = -1;
        }
    }
    return 1;
}

void xGridUpdate(xGrid* grid, xEnt* ent)
{
    S32 dx;
    S32 dz;
    xGridGetCell(grid, xEntGetCenter(ent)->x, xEntGetCenter(ent)->y, xEntGetCenter(ent)->z, dx, dz);

    xGridBound* gridb = &ent->gridb;

    if (gridb->oversize == 2)
    {
        return;
    }

    if (dx == gridb->gx && dz == gridb->gz)
    {
        return;
    }

    xGridBound* cur = *gridb->head;
    xGridBound** prev = gridb->head;

    while (cur && cur != gridb)
    {
        prev = &cur->next;
        cur = cur->next;
    }

    *prev = cur->next;
    cur->head = NULL;
    cur->next = NULL;

    if (cur != gridb)
    {
        cur->grid = NULL;
        cur->deleted = 0;
        cur->gx = 0xFFFF;
        cur->gz = 0xFFFF;
    }

    xGridBound** cell = &grid->cells[dz * grid->nx] + dx;
    gridb->head = cell;
    gridb->next = *cell;
    gridb->gx = dx;
    gridb->gz = dz;
    *cell = gridb;
}

void xGridGetCell(xGrid* grid, F32 posx, F32, F32 posz, S32& grx, S32& grz)
{
    F32 pgridx = posx - grid->minx;
    F32 pgridz = posz - grid->minz;
    pgridx *= grid->inv_csizex;
    pgridz *= grid->inv_csizez;

    grx = MIN(grid->nx - 1, MAX(0.0f, pgridx));
    grz = MIN(grid->nz - 1, MAX(0.0f, pgridz));
}

inline xGridBound* xGridIterFirstCell(xGrid* grid, F32 posx, F32 posy, F32 posz, S32& grx, S32& grz,
                                      xGridIterator& iter)
{
    xGridGetCell(grid, posx, posy, posz, grx, grz);
    return xGridIterFirstCell(grid, grx, grz, iter);
}

S32 xGridEntIsTooBig(xGrid* grid, const xEnt* ent)
{
    const xBound* bound = &ent->bound;
    F32 maxr = grid->maxr;

    if (bound->type == XBOUND_TYPE_SPHERE)
    {
        if (bound->sph.r >= maxr)
        {
            return 1;
        }
    }
    else if (bound->type == XBOUND_TYPE_OBB)
    {
        const xBBox* bbox = &bound->box;
        F32 rx = bbox->box.upper.x - bbox->box.lower.x;
        F32 ry = bbox->box.upper.y - bbox->box.lower.y;
        F32 rz = bbox->box.upper.z - bbox->box.lower.z;

        if (SQR(rx) * (SQR(bound->mat->right.x) + SQR(bound->mat->right.y) +
                       SQR(bound->mat->right.z)) +
                SQR(ry) * (SQR(bound->mat->up.x) + SQR(bound->mat->up.y) + SQR(bound->mat->up.z)) +
                SQR(rz) * (SQR(bound->mat->at.x) + SQR(bound->mat->at.y) + SQR(bound->mat->at.z)) >=
            4.0f * maxr * maxr)
        {
            return 1;
        }
    }
    else if (bound->type == XBOUND_TYPE_BOX)
    {
        const xBBox* bbox = &bound->box;
        F32 rx = bbox->box.upper.x - bbox->box.lower.x;
        F32 rz = bbox->box.upper.z - bbox->box.lower.z;

        if (SQR(rx) + SQR(rz) >= 4.0f * maxr * maxr)
        {
            return 1;
        }
    }

    return 0;
}

// Dead-stripped placeholder, not recovered code: retail's .sdata2 has 1e38f, 0.001f and -1e38f
// together between xGridInit's and xGridCheckBoundSetup's literals, from a function the linker
// stripped (no live xGrid function uses +-1e38f). Name and body are unknown.
static void __deadstripped_xGrid(F32* out)
{
    out[0] = 1e38f;
    out[1] = 0.001f;
    out[2] = -1e38f;
}

void xGridCheckPosition(xGrid* grid, xVec3* pos, xQCData* qcd, GridEntCallback hitCB, void* cbdata)
{
    xGridIterator it;
    S32 px;
    S32 pz;
    xGridBound* cell;
    F32 clcenterx;
    F32 clcenterz;
    S32 i;

    cell = xGridIterFirstCell(grid, pos->x, pos->y, pos->z, px, pz, it);
    while (cell)
    {
        xBound* cellbound = (xBound*)(cell + 1);
        if (xQuickCullIsects(qcd, &cellbound->qcd) && !hitCB((xEnt*)cell->data, cbdata))
        {
            xGridIterClose(it);
            return;
        }
        cell = xGridIterNextCell(it);
    }

    xBox clbox;
    clbox.lower.x = grid->csizex * px;
    clbox.lower.z = grid->csizez * pz;
    clbox.lower.x += grid->minx;
    clbox.lower.z += grid->minz;

    clcenterx = 0.5f * grid->csizex;
    clcenterx += clbox.lower.x;

    clcenterz = 0.5f * grid->csizez;
    clcenterz += clbox.lower.z;

    static S32 offs[4][3][2] = { -1, 0, -1, -1, 0, -1, 0, -1, 1,  -1, 1,  0,
                                 1,  0, 1,  1,  0, 1,  0, 1,  -1, 1,  -1, 0 };
    static S32 k;

    if (pos->x < clcenterx)
    {
        if (pos->z < clcenterz)
        {
            k = 0;
        }
        else
        {
            k = 1;
        }
    }
    else
    {
        if (pos->z < clcenterz)
        {
            k = 3;
        }
        else
        {
            k = 2;
        }
    }

    for (i = 0; i < 3; i++)
    {
        S32 _x = px + offs[k][i][1];
        if (_x >= 0 && _x < grid->nx)
        {
            S32 _z = pz + offs[k][i][0];
            if (_z >= 0 && _z < grid->nz)
            {
                cell = xGridIterFirstCell(grid, _x, _z, it);
                while (cell)
                {
                    xBound* cellbound = (xBound*)(cell + 1);
                    if (xQuickCullIsects(qcd, &cellbound->qcd) && !hitCB((xEnt*)cell->data, cbdata))
                    {
                        xGridIterClose(it);
                        return;
                    }
                    cell = xGridIterNextCell(it);
                }
            }
        }
    }

    cell = xGridIterFirstCell(&grid->other, it);
    while (cell)
    {
        xBound* cellbound = (xBound*)(cell + 1);
        if (xQuickCullIsects(qcd, &cellbound->qcd) && !hitCB((xEnt*)cell->data, cbdata))
        {
            xGridIterClose(it);
            return;
        }
        cell = xGridIterNextCell(it);
    }
}

xGridBound* xGridIterFirstCell(xGridBound** head, xGridIterator& it)
{
    xGridBound* curcell = *head;

    if (!curcell)
    {
        return NULL;
    }

    it.delfound = 0;
    it.listhead = head;
    it.curcell = curcell;

    curcell->grid->iter_active++;

    return curcell;
}

xGridBound* xGridIterFirstCell(xGrid* grid, S32 grx, S32 grz, xGridIterator& it)
{
    if (grx < 0 || grx >= grid->nx)
    {
        return NULL;
    }

    if (grz < 0 || grz >= grid->nz)
    {
        return NULL;
    }

    return xGridIterFirstCell(grid->cells + grz * grid->nx + grx, it);
}

void xGridIterClose(xGridIterator& it)
{
    if (it.listhead)
    {
        S32& iter_active = (*it.listhead)->grid->iter_active;
        iter_active--;

        if (it.delfound && !iter_active)
        {
            xGridBound* cur = *it.listhead;
            xGridBound** prev = it.listhead;

            while (cur)
            {
                if (cur->deleted)
                {
                    *prev = cur->next;

                    cur->next = NULL;
                    cur->head = NULL;
                    cur->grid = NULL;
                    cur->deleted = 0;
                    cur->gx = 0xFFFF;
                    cur->gz = 0xFFFF;

                    cur = *prev;
                }
                else
                {
                    prev = &cur->next;
                    cur = cur->next;
                }
            }
        }

        it.listhead = NULL;
        it.curcell = NULL;
        it.delfound = 0;
    }
}

static inline void get_grid_index(S32& gx, S32& gz, const xGrid& grid, F32 x, F32 z)
{
    x = grid.inv_csizex * (x - grid.minx);
    z = grid.inv_csizez * (z - grid.minz);

    gx = range_limit<F32>(x, 0.0f, grid.nx - 1);
    gz = range_limit<F32>(z, 0.0f, grid.nz - 1);
}

void xGridCheckBoundSetup(xBox& box, S32& lower_x, S32& lower_z, S32& upper_x, S32& upper_z,
                          xGrid& grid, const xBound& bound)
{
    xBoundGetBox(box, bound);

    F32 ex = 0.25f * grid.csizex;
    F32 ez = 0.25f * grid.csizez;

    box.upper.x += ex;
    box.upper.z += ez;
    box.lower.x -= ex;
    box.lower.z -= ez;

    get_grid_index(lower_x, lower_z, grid, box.lower.x, box.lower.z);
    get_grid_index(upper_x, upper_z, grid, box.upper.x, box.upper.z);

    // An empty z range also empties the x range, so xGridCheckBound visits no cells
    if (lower_z > upper_z)
    {
        lower_x = upper_x + 1;
    }
}
