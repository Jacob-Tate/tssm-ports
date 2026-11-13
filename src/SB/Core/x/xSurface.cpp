#include "xSurface.h"
#include "xString.h"

xSurface* surfs;
U16 nsurfs;

void xSurfaceInit(U16 num_surfs)
{
    nsurfs = num_surfs;
    if (num_surfs != 0)
    {
        surfs = (xSurface*)xMemAllocSize(num_surfs * sizeof(xSurface));
        for (U16 i = 0; i < nsurfs; i++)
        {
            surfs[i].idx = i;
        }
    }
    else
    {
        surfs = NULL;
    }
}

void xSurfaceExit()
{
}

void xSurfaceSave(xSurface* ent, xSerial* s)
{
    xBaseSave((xBase*)ent, s);
}

void xSurfaceLoad(xSurface* ent, xSerial* s)
{
    xBaseLoad((xBase*)ent, s);
}

void xSurfaceReset(xSurface* ent)
{
}

U16 xSurfaceGetNumSurfaces()
{
    return nsurfs;
}

xSurface* xSurfaceGetByIdx(U16 n)
{
    return surfs != NULL ? &surfs[n] : NULL;
}

xSurface* xSurfaceGetByName(const char* name)
{
    S32 i;

    for (i = 0; i < nsurfs; i++)
    {
        if (surfs[i].id == xStrHash(name))
        {
            return &surfs[i];
        }
    }

    return NULL;
}
