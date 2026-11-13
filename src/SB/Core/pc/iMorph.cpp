#include "iMorph.h"

#include "iModel.h"
#include "xMemMgr.h"

#include <rpusrdat.h>
#include <string.h>

static RpGeometry* s_geom;
static RpMorphTarget* s_tgt;
static F32* s_alloc;
static F32* s_vTemp;
static F32* s_nTemp;
static S32 s_numV;

static void MorphCommon(RpAtomic* model, RwMatrixTag* mat, S16** v_array, S16* weight, U32 normals,
                        F32 scale, S32 dorender)
{
    U32 i;
    U32 a;
    S16 wa[4];
    S16* va[4];
    S32 wsum;
    RwV3d* vold;
    RwV3d* nold;
    S32 lockMode;
    void* temp_scratch;
    RpUserDataArray* usr;
    DirtyMorph* dm;

    s_geom = model->geometry;
    nold = NULL;
    temp_scratch = NULL;
    s_tgt = s_geom->morphTarget;
    s_numV = s_geom->numVertices;
    s_alloc = NULL;
    s_nTemp = NULL;

    normals = normals && (s_geom->object.flags & rpGEOMETRYNORMALS);

    vold = s_tgt->verts;
    lockMode = (normals ? rpGEOMETRYLOCKNORMALS : 0) | rpGEOMETRYLOCKVERTICES;
    if (normals)
    {
        nold = s_tgt->normals;
    }

    a = 0;
    wsum = 0;
    for (i = 0; i < 4; i++)
    {
        if (v_array[i] != NULL && weight[i] != 0)
        {
            va[a] = v_array[i];
            wa[a] = weight[i];
            wsum += weight[i];
            a++;
        }
    }

    usr = RpGeometryGetUserDataArray(s_geom, 0);
    if (usr != NULL)
    {
        dm = (DirtyMorph*)usr->data;

        s_vTemp = (F32*)(dm + 1);
        while ((UPtr)s_vTemp & 0xF)
        {
            s_vTemp++;
        }
        s_tgt->verts = (RwV3d*)s_vTemp;

        if (normals)
        {
            s_nTemp = s_vTemp + s_numV * 3;
            while ((UPtr)s_nTemp & 0xF)
            {
                s_nTemp++;
            }
            s_tgt->normals = (RwV3d*)s_nTemp;
        }

        if (dm->count == a && dm->weight[0] == wa[0] && dm->v_array[0] == va[0] &&
            dm->scale == scale)
        {
            for (i = 1; i < a; i++)
            {
                if (dm->weight[i] != wa[i] || va[i] != dm->v_array[i])
                {
                    break;
                }
            }

            if (a == i)
            {
                if (dorender)
                {
                    iModelRender(model, mat);
                }

                s_tgt->verts = vold;
                if (nold)
                {
                    s_tgt->normals = nold;
                }
                return;
            }
        }

        dm->count = a;
        dm->scale = scale;
        for (i = 0; i < a; i++)
        {
            dm->weight[i] = wa[i];
            dm->v_array[i] = v_array[i];
        }

        RpGeometryLock(s_geom, lockMode);
    }

    if (a == 3)
    {
        va[3] = va[2];
        wa[3] = 0;
    }

    if (usr == NULL)
    {
        if (s_numV <= 0x4000 / sizeof(RwV3d))
        {
            temp_scratch = xMemScratchPush(0x4000, 0);
        }

        if (temp_scratch != NULL)
        {
            s_vTemp = (F32*)temp_scratch;
        }
        else
        {
            s_vTemp = (F32*)xMemPushTemp(s_numV * 3 * sizeof(F32) + 16);
            s_alloc = s_vTemp;
            while ((UPtr)s_vTemp & 0xF)
            {
                s_vTemp++;
            }
        }

        if (normals && (s_geom->object.flags & rpGEOMETRYNORMALS))
        {
            if (temp_scratch != NULL)
            {
                s_nTemp = (F32*)((U8*)temp_scratch + 0x4000 / 2);
            }
            else
            {
                s_nTemp = (F32*)xMemPushTemp(s_numV * 3 * sizeof(F32) + 16);
                if (s_alloc == NULL)
                {
                    s_alloc = s_nTemp;
                }
                while ((UPtr)s_nTemp & 0xF)
                {
                    s_nTemp++;
                }
            }
        }
    }

    if (a == 1)
    {
        FastS16unpack(s_vTemp, va[0], s_numV * 3, scale * wsum);
    }
    else if (a == 2)
    {
        FastS16weight2(s_vTemp, va, wa, s_numV * 3, scale);
    }
    else
    {
        FastS16weight4(s_vTemp, va, wa, s_numV * 3, scale);
    }

    if (s_nTemp != NULL)
    {
        scale = 1.0f / (wsum * 16384.0f);

        for (i = 0; i < a; i++)
        {
            va[i] += (s_numV * 3 + 7) & ~7;
        }

        if (a == 1)
        {
            FastS16unpack(s_nTemp, va[0], s_numV * 3, scale * wsum);
        }
        else if (a == 2)
        {
            FastS16weight2(s_nTemp, va, wa, s_numV * 3, scale);
        }
        else
        {
            FastS16weight4(s_nTemp, va, wa, s_numV * 3, scale);
        }
    }

    if (usr != NULL)
    {
        RpGeometryUnlock(s_geom);
        if (dorender)
        {
            iModelRender(model, mat);
        }
        s_tgt->verts = vold;
        if (normals)
        {
            s_tgt->normals = nold;
        }
    }
    else if (dorender)
    {
        RpGeometryLock(s_geom, lockMode);
        vold = s_tgt->verts;
        s_tgt->verts = (RwV3d*)s_vTemp;
        if (normals)
        {
            nold = s_tgt->normals;
            s_tgt->normals = (RwV3d*)s_nTemp;
        }
        RpGeometryUnlock(s_geom);
        iModelRender(model, mat);
        s_tgt->verts = vold;
        if (normals)
        {
            s_tgt->normals = nold;
        }
    }

    if (temp_scratch != NULL)
    {
        xMemScratchPop(temp_scratch);
    }
}

void iMorphOptimize(RpAtomic* model, S32 normals)
{
    normals = (normals != 0);
    RpGeometry* geom = model->geometry;

    if (RpGeometryGetUserDataArrayCount(geom) == 0)
    {
        S32 usridx =
            RpGeometryAddUserDataArray(geom, "MORPHSTATE", rpINTUSERDATA,
                                       (normals + 1) * geom->numVertices * sizeof(RwV3d) + 56);
        RpUserDataArray* usr = RpGeometryGetUserDataArray(geom, usridx);

        memset(usr->data, 0, 0x20);
    }
}

void iMorphRender(RpAtomic* model, RwMatrixTag* mat, S16** v_array, S16* weight, U32 normals,
                  F32 scale)
{
    MorphCommon(model, mat, v_array, weight, normals, scale, 1);
    if (s_alloc != NULL)
    {
        xMemPopTemp(s_alloc);
        s_alloc = NULL;
    }
}

void FastS16unpack(F32* dest, S16* v, S32 count, F32 scale)
{
    S32 i;

    for (i = 0; i < count; i++)
    {
        dest[i] = v[i] * scale;
    }
}

void FastS16weight2(F32* dest, S16** v_array, S16* weight, S32 count, F32 scale)
{
    S32 i;
    S16* v0 = v_array[0];
    S16* v1 = v_array[1];
    F32 w0 = scale * weight[0];
    F32 w1 = scale * weight[1];

    for (i = 0; i < count; i++)
    {
        dest[i] = v0[i] * w0 + v1[i] * w1;
    }
}

void FastS16weight4(F32* dest, S16** v_array, S16* weight, S32 count, F32 scale)
{
    S32 i;
    S16* v0 = v_array[0];
    S16* v1 = v_array[1];
    S16* v2 = v_array[2];
    S16* v3 = v_array[3];
    F32 w0 = scale * weight[0];
    F32 w1 = scale * weight[1];
    F32 w2 = scale * weight[2];
    F32 w3 = scale * weight[3];

    for (i = 0; i < count; i++)
    {
        dest[i] = v0[i] * w0 + v1[i] * w1 + v2[i] * w2 + v3[i] * w3;
    }
}
