#include "zNMEHazardUVAnim.h"

// No GC symbol: inlined into its only caller, HAZBasic::Stuff_PostInit.
S32 UVAModelInfo::Init(RpAtomic* model, U32 flg_uvamCaller)
{
    RpGeometry* geom;

    this->model = model;
    flg_uvam = flg_uvamCaller;
    if (model == NULL)
    {
        return 0;
    }

    if (flg_uvam & 1)
    {
        geom = model->geometry;
        if (geom == NULL)
        {
            return 0;
        }
        geom->flags |= rpGEOMETRYMODULATEMATERIALCOLOR;
    }

    if (!CloneUV(uv, uvsize, model))
    {
        return 0;
    }

    offset.assign(0.0f, 0.0f);
    offset_vel.assign(0.0f, 0.0f);
    return 1;
}
