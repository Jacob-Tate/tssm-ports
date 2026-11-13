#ifndef XGRID_H
#define XGRID_H

#include <types.h>

#include "xMath3.h"
#include "xBound.h"

struct xBound;
struct xEnt;
struct xQCData;
struct xGrid;

struct xGridBound
{
    void* data;
    U16 gx;
    U16 gz;
    U8 oversize;
    U8 deleted;
    U8 gpad;
    U8 pad;
    xGrid* grid;
    xGridBound** head;
    xGridBound* next;
};

struct xGrid
{
    U8 ingrid_id;
    U8 pad[3];
    U16 nx;
    U16 nz;

    // Offset: 0x8
    F32 minx;
    F32 minz;
    F32 maxx;
    F32 maxz;

    // Offset: 0x18
    F32 csizex;
    F32 csizez;
    F32 inv_csizex;
    F32 inv_csizez;

    // Offset: 0x28
    F32 maxr;
    xGridBound** cells;
    xGridBound* other;
    S32 iter_active;
};

struct xGridIterator
{
    xGridBound** listhead;
    xGridBound* curcell;
    U32 delfound;
};

typedef S32 (*GridEntCallback)(xEnt*, void*);

void xGridInit(xGrid* grid, const xBox* bounds, U16 nx, U16 nz, U8 ingrid_id);
void xGridKill(xGrid* grid);
void xGridEmpty(xGrid* grid);
void xGridUpdate(xGrid* grid, xEnt* ent);
S32 xGridRemove(xGridBound* gridb);
xGridBound** xGridGetCell(xGrid* grid, const xEnt* ent, S32& grx, S32& grz);
void xGridGetCell(xGrid* grid, F32 posx, F32 posy, F32 posz, S32& grx, S32& grz);
xGridBound* xGridIterFirstCell(xGrid* grid, S32 grx, S32 grz, xGridIterator& it);
xGridBound* xGridIterFirstCell(xGrid* grid, F32 posx, F32, F32 posz, S32& grx, S32& grz,
                               xGridIterator& it);
xGridBound* xGridIterFirstCell(xGridBound** head, xGridIterator& it);
xGridBound* xGridIterNextCell(xGridIterator& it);
void xGridIterClose(xGridIterator& it);
void xGridCheckPosition(xGrid* grid, xVec3* pos, xQCData* qcd, GridEntCallback hitCB, void* cbdata);
S32 xGridEntIsTooBig(xGrid* grid, const xEnt* ent);
S32 xGridAdd(xGrid* grid, xEnt* ent);
void xGridCheckBoundSetup(xBox& box, S32& minx, S32& minz, S32& maxx, S32& maxz, xGrid& grid,
                          const xBound& bound);

inline void xGridBoundInit(xGridBound* gridb, void* data)
{
    gridb->data = data;
    gridb->gx = -1;
    gridb->gz = -1;
    gridb->grid = NULL;
    gridb->oversize = 0;
    gridb->head = NULL;
    gridb->next = NULL;
    gridb->gpad = 0xea;
}

inline xGridBound* xGridIterNextCell(xGridIterator& it)
{
    if (it.curcell)
    {
        it.curcell = it.curcell->next;
    }

    while (it.curcell)
    {
        if (!it.curcell->deleted)
        {
            return it.curcell;
        }

        it.delfound = 1;
        it.curcell = it.curcell->next;
    }

    xGridIterClose(it);
    return NULL;
}

// Calls cb(ent) for every grid entry whose quick-cull data intersects qcd, first in the
// oversize list and then in the cells overlapped by bound; stops as soon as cb returns false.
template <class T> void xGridCheckBound(xGrid& grid, const xBound& bound, const xQCData& qcd, T& cb)
{
    xGridIterator it;
    xBox box;
    S32 lower_x;
    S32 lower_z;
    S32 upper_x;
    S32 upper_z;

    xGridCheckBoundSetup(box, lower_x, lower_z, upper_x, upper_z, grid, bound);

    xGridBound* cell = xGridIterFirstCell(&grid.other, it);
    S32 gx = lower_x;
    S32 gz = lower_z;

    while (true)
    {
        while (cell)
        {
            if (xQuickCullIsects(&qcd, &((xBound*)(cell + 1))->qcd) && !cb(*(xEnt*)cell->data))
            {
                xGridIterClose(it);
                return;
            }

            cell = xGridIterNextCell(it);
        }

        if (gx > upper_x)
        {
            break;
        }

        cell = xGridIterFirstCell(&grid, gx, gz, it);

        if (++gz > upper_z)
        {
            gz = lower_z;
            gx++;
        }
    }
}

#endif
