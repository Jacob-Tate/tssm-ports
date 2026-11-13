#ifndef ZNMEHAZARDUVANIM_H
#define ZNMEHAZARDUVANIM_H

#include <types.h>
#include <string.h>
#include <rwcore.h>
#include <rpworld.h>

#include "xMath2.h"
#include "xMathInlines.h"
#include "xMemMgr.h"

struct UVAModelInfo
{
    xVec2 offset_vel;
    U32 flg_uvam;
    RpAtomic* model;
    RwTexCoords* uv;
    S32 uvsize;
    xVec2 offset;

    // No GC symbols (always inlined); names from BfBB's UVAModelInfo.
    void Clear()
    {
        memset(this, 0, sizeof(UVAModelInfo));
    }
    void Hemorrage()
    {
        model = NULL;
        uv = NULL;
    }
    // Inlined (no symbols); names from BfBB's UVAModelInfo. Returns bool because retail tests the
    // result as a byte.
    bool Valid() const
    {
        return model != NULL && uv != NULL;
    }
    void UVVelSet(F32 u, F32 v)
    {
        offset_vel.x = u;
        offset_vel.y = v;
    }
    // Inlined into zMODHazard::Timestep (no symbols); bodies from BfBB's
    // UVAModelInfo::Update/Refresh.
    void Update(F32 dt, const xVec2* use_offset)
    {
        if (use_offset != NULL)
        {
            offset = *use_offset;
        }
        else if (xVec2Dot(&offset_vel, &offset_vel) > 0.0f)
        {
            offset += offset_vel * dt;
        }
        else
        {
            return;
        }

        offset.x = xfmod(offset.x, 1.0f);
        offset.y = xfmod(offset.y, 1.0f);

        Refresh();
    }
    void Refresh()
    {
        RpGeometry* geo = model->geometry;
        RwTexCoords* src = uv;
        RwTexCoords* dst = geo->texCoords[0];
        RwTexCoords* end = &uv[uvsize];

        RpGeometryLock(geo, rpGEOMETRYLOCKTEXCOORDS1);

        while (src < end)
        {
            dst->u = src->u + offset.x;
            dst->v = src->v + offset.y;
            src++;
            dst++;
        }

        RpGeometryUnlock(geo);
    }
    // Inlined into Init (no symbols); bodies and names from BfBB's UVAModelInfo::GetUV/CloneUV.
    S32 GetUV(RwTexCoords*& coords, S32& numVertices, RpAtomic* model) const
    {
        coords = NULL;
        numVertices = 0;
        RpGeometry* geom = model->geometry;
        if (geom == NULL)
        {
            return 0;
        }
        numVertices = geom->numVertices;
        if (numVertices <= 0)
        {
            return 0;
        }

        coords = geom->texCoords[0];

        return coords != NULL;
    }
    S32 CloneUV(RwTexCoords*& coords, S32& numVertices, RpAtomic* model) const
    {
        RwTexCoords* src;

        if (!GetUV(src, numVertices, model))
        {
            return 0;
        }

        coords = (RwTexCoords*)xMemAlloc(gActiveHeap, numVertices * sizeof(RwTexCoords), 0);
        if (coords == NULL)
        {
            return 0;
        }

        memcpy(coords, src, numVertices * sizeof(RwTexCoords));
        return 1;
    }
    // Out of line in zNMEHazardUVAnim.cpp (PS2 DWARF); on GC only inlined into
    // HAZBasic::Stuff_PostInit.
    S32 Init(RpAtomic* model, U32 flg_uvamCaller);
};

#endif
