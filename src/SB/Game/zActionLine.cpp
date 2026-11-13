#include <types.h>
#include <rwcore.h>

#include "xstransvc.h"
#include "xMath3.h"
#include "xString.h"

#include "zActionLine.h"

static _tagActionLine* sActionLine[8];
static RwRaster* sActionLineRaster;

void zActionLineInit()
{
    for (S32 i = 0; i < 8; i++)
    {
        sActionLine[i] = NULL;
    }
    sActionLineRaster = NULL;

    RwTexture* tex = (RwTexture*)xSTFindAsset(xStrHash("ACTIONLINES"), NULL);
    if (tex != NULL)
    {
        sActionLineRaster = tex->raster;
    }
}

void zActionLineUpdate(F32 seconds)
{
    for (S32 i = 0; i < 8; i++)
    {
        _tagActionLine* l = sActionLine[i];

        if (l && l->flags & 1)
        {
            l->time_left -= seconds;

            if (l->time_left <= 0.0f)
            {
                l->flags &= ~1;
            }
        }
    }
}

static inline void RenderActionLine(_tagActionLine* l)
{
    static RxObjSpace3DVertex sStripVert[4];

    RxObjSpace3DVertex* vert = sStripVert;
    for (S32 i = 0; i < 4; i++)
    {
        RwIm3DVertexSetPos(vert, l->pos[i].x, l->pos[i].y, l->pos[i].z);
        RwIm3DVertexSetUV(vert, 0.0f, 0.0f);
        RwIm3DVertexSetRGBA(vert, 0xFF, 0xFF, 0xFF, 0x80);
        vert++;
    }

    sStripVert[2].u = 1.0f;
    sStripVert[3].u = 1.0f;
    sStripVert[3].v = 1.0f;

    if (RwIm3DTransform(sStripVert, 4, NULL,
                        rwIM3D_VERTEXRGBA | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXUV))
    {
        RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
        RwIm3DEnd();
    }
}

void zActionLineRender()
{
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, sActionLineRaster);

    for (S32 i = 0; i < 8; i++)
    {
        if (sActionLine[i] && sActionLine[i]->flags & 1)
        {
            RenderActionLine(sActionLine[i]);
        }
    }
}
