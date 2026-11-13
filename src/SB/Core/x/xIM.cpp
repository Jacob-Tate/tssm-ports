#include "xIM.h"

#include "xMemMgr.h"

#include <rwcore.h>

void xIMLock(xIMLockContext& context, xIMFormat format, S32 vertCount)
{
    S32 pos_qwords = (vertCount * sizeof(xVec3) + (16 - 1)) / 16;
    S32 uv_qwords = (vertCount * sizeof(xVec2) + (16 - 1)) / 16;
    S32 color_qwords = (vertCount * sizeof(xColor_tag) + (16 - 1)) / 16;
    S32 alloc_qwords = pos_qwords + uv_qwords + color_qwords;

    context.data = xMemPushTemp(alloc_qwords * 16);

    U8* data_buffer = (U8*)context.data;

    context.pos = (xVec3*)data_buffer;
    data_buffer += pos_qwords * 16;

    context.uv = (xVec2*)data_buffer;
    data_buffer += uv_qwords * 16;

    context.color = (xColor_tag*)data_buffer;
    data_buffer += color_qwords * 16;

    context.vertCount = vertCount;
    context.format = format;
}

void xIMUnlock(xIMLockContext& context)
{
    if (context.data)
    {
        xMemPopTemp(context.data);
    }
}

void xIMRender(xIMLockContext& context, const xMat4x3* mat)
{
    S32 verts_size = context.vertCount;
    const xVec3* pos = context.pos;
    const xVec2* uv = context.uv;
    const xColor_tag* color = context.color;

    RwIm3DVertex* vert_buffer = (RwIm3DVertex*)xMemPushTemp(verts_size * sizeof(RwIm3DVertex));

    RwIm3DVertex* vert = vert_buffer;
    RwIm3DVertex* end_vert = vert + verts_size;
    while (vert != end_vert)
    {
        RwIm3DVertexSetPos(vert, pos->x, pos->y, pos->z);
        RwIm3DVertexSetUV(vert, uv->x, uv->y);
        RwIm3DVertexSetRGBA(vert, color->r, color->g, color->b, color->a);
        pos++;
        uv++;
        color++;
        vert++;
    }

    RwIm3DTransform(vert_buffer, verts_size, (RwMatrix*)mat,
                    rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA);
    RwIm3DRenderPrimitive((RwPrimitiveType)context.format);
    RwIm3DEnd();

    xMemPopTemp(vert_buffer);
}
