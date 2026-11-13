#include "zGrid.h"

#include "zReactiveAnimation.h"
#include "zSurface.h"

#include "xMath.h"

#include <PowerPC_EABI_Support/MSL_C/MSL_Common/cmath>

xGrid colls_grid;
xGrid colls_oso_grid;
xGrid npcs_grid;
xGrid grabbable_grid;
static S32 zGridInitted;

void zGridReset(zScene* s)
{
    for (S32 i = 0; i < s->num_base; i++)
    {
        xBase* base = s->base[i];
        if (base && (base->baseFlags & 0x20) && base->baseType != eBaseTypeTrigger &&
            base->baseType != eBaseTypePlayer && ((xEnt*)base)->model)
        {
            xEnt* ent = (xEnt*)base;
            if (ent->bupdate)
            {
                ent->bupdate(ent, (xVec3*)&ent->model->Mat->pos);
            }
            else
            {
                xEntDefaultBoundUpdate(ent, (xVec3*)&ent->model->Mat->pos);
            }
            zGridUpdateEnt(ent);
        }
    }
}

void zGridInit(zScene* s)
{
    xBox* ebox = xEntGetAllEntsBox();

    F32 grsizex = MAX(0.001f, ebox->upper.x - ebox->lower.x);
    F32 grsizez = MAX(0.001f, ebox->upper.z - ebox->lower.z);

    F32 min_csize = 10.0f;

    xGridInit(&colls_grid, ebox, (U16)MAX(1.0f, MIN(32.0f, std::floorf(grsizex / min_csize))),
              (U16)MAX(1.0f, MIN(32.0f, std::floorf(grsizez / min_csize))), 1);

    xBox osobox = *ebox;
    osobox.lower.x -= 1.0f;
    osobox.lower.z -= 1.0f;
    osobox.upper.x += 3.4567f;
    osobox.upper.z += 3.4567f;

    grsizex = MAX(0.001f, osobox.upper.x - osobox.lower.x);
    grsizez = MAX(0.001f, osobox.upper.z - osobox.lower.z);

    min_csize *= 6.0f;

    xGridInit(&colls_oso_grid, &osobox, (U16)MAX(1.0f, MIN(8.0f, std::floorf(grsizex / min_csize))),
              (U16)MAX(1.0f, MIN(8.0f, std::floorf(grsizez / min_csize))), 2);

    xGridInit(&grabbable_grid, &osobox, (U16)MAX(1.0f, MIN(8.0f, std::floorf(grsizex / min_csize))),
              (U16)MAX(1.0f, MIN(8.0f, std::floorf(grsizez / min_csize))), 4);

    xGridInit(&npcs_grid, ebox, (U16)MAX(1.0f, MIN(16.0f, std::floorf(grsizex / 20.0f))),
              (U16)MAX(1.0f, MIN(16.0f, std::floorf(grsizez / 20.0f))), 3);

    zGridInitted = TRUE;
    zGridReset(s);
}

void zGridExit(zScene* s)
{
    xGridKill(&colls_grid);
    xGridKill(&colls_oso_grid);
    xGridKill(&npcs_grid);
    xGridKill(&grabbable_grid);
    zGridInitted = 0;
}

static inline U8 IsEntityGrabbable(xEnt* ent)
{
    if (!ent || !ent->model || !ent->model->Surf || !ent->model->Surf->moprops)
    {
        return FALSE;
    }

    zSurfaceProps* props = (zSurfaceProps*)ent->model->Surf->moprops;
    return (props->asset->phys_flags & 0x40) != 0;
}

void zGridUpdateEnt(xEnt* ent)
{
    if (!zGridInitted)
    {
        return;
    }

    xGrid* grid = ent->gridb.grid;

    if ((ent->chkby & 0x98) || ent->baseType == eBaseTypePickup || zReactiveIsReactive(*ent))
    {
        if (grid)
        {
            xGridUpdate(grid, ent);
        }
        else if (ent->collType == XENT_COLLTYPE_NPC)
        {
            if (xGridEntIsTooBig(&npcs_grid, ent))
            {
                ent->gridb.oversize = 1;
            }
            else
            {
                ent->gridb.oversize = 0;
            }
            xGridAdd(&npcs_grid, ent);
        }
        else if (xGridEntIsTooBig(&colls_grid, ent))
        {
            if (xGridEntIsTooBig(&colls_oso_grid, ent))
            {
                ent->gridb.oversize = 2;
            }
            else
            {
                ent->gridb.oversize = 1;
            }
            xGridAdd(&colls_oso_grid, ent);
        }
        else
        {
            xGridAdd(&colls_grid, ent);
            ent->gridb.oversize = 0;
        }
    }
    else if (IsEntityGrabbable(ent) == TRUE)
    {
        if (grid && grid != &grabbable_grid)
        {
            xGridRemove(&ent->gridb);
        }
        else if (!grid)
        {
            xGridAdd(&grabbable_grid, ent);
            ent->gridb.oversize = 0;
        }
    }
    else if (grid)
    {
        xGridRemove(&ent->gridb);
    }
}
