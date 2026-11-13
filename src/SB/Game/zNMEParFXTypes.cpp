#include "zNMEParFX.h"

xVec2 uv_cell_2x2[4][2];
xVec2 uv_cell_3x3[9][2];
xVec2 uv_cell_4x4[16][2];

// No symbol (always inlined; the name is ours). Retail's evaluation order needs the corner math to
// go through an inline's parameters, one call per cell size as in the PS2 line table.
static inline void SetCellCorners(S32 c, S32 r, F32 size, xVec2* uv0, xVec2* uv1)
{
    uv0->x = c * size;
    uv0->y = r * size;
    uv1->x = uv0->x + size;
    uv1->y = uv0->y + size;
}

static inline void BuildUVOffsetTables()
{
    S32 dim;
    S32 r;
    S32 c;
    S32 idx;

    for (dim = 2; dim <= 4; dim++)
    {
        for (r = 0; r < dim; r++)
        {
            for (c = 0; c < dim; c++)
            {
                idx = r * dim + c;
                if (dim == 2)
                {
                    SetCellCorners(c, r, 1.0f / dim, &uv_cell_2x2[idx][0], &uv_cell_2x2[idx][1]);
                }
                else if (dim == 3)
                {
                    SetCellCorners(c, r, 1.0f / dim, &uv_cell_3x3[idx][0], &uv_cell_3x3[idx][1]);
                }
                else if (dim == 4)
                {
                    SetCellCorners(c, r, 1.0f / dim, &uv_cell_4x4[idx][0], &uv_cell_4x4[idx][1]);
                }
            }
        }
    }
}

void zNMEParFX_RegisterTypes()
{
    BuildUVOffsetTables();
    DUMMY_NAMESPACE::DUMMY_PREFIXSysRegister();
    Exhaust::ExhaustSysRegister();
    FogBreath::FogSysRegister();
    Orient::OrientSysRegister();
    Plasma::PlasmaSysRegister();
    PopGas::PopGasSysRegister();
    PuffRing::PuffSysRegister();
    Sparky::SparkySysRegister();
}
