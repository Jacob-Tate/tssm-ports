#include "iFX.h"
#include "xFX.h"

#include <dolphin/gx.h>
#include <rpworld.h>

// RenderWare GameCube driver internals (types and parameter names from the Ratatouille GC DWARF).
struct RpGameCubeVtxFmt;

extern "C" {
extern RwUInt16 _RwDlTokenCurrent;
extern RwInt32 _rpDlGeomVtxFmtOffset;
extern RwInt32 _rpDlWorldVtxFmtOffset;

void _rwDlVtxFmtSetup(RpGameCubeVtxFmt* fmt, RxGameCubePipeData* pipeData);
void _rwDlTransformSetup(RwMatrix* ltm, RwBool normals);
void (*_rwDlObjectRenderSetup(RwUInt32 flags, RwUInt32 lightMask, RwInt32 ambient,
                              RwInt32 prelightAlpha))(RwRGBAReal*, RwRGBA*, RwReal);
void _rwDlTextureSet(RwTexture* texture, RwInt32 index);
}

static void* _iGCUVRenderCallback(void* object, RxGameCubePipeData* pipeData)
{
    RwUInt32 numMeshes;
    RxGameCubeVertexBuffer* vbHeader;
    RxGameCubeDisplayList* dList;
    RpMesh* mesh;
    RwMatrix* ltm;
    void (*matFunc)(RwRGBAReal*, RwRGBA*, RwReal);
    RpAtomic* atomic;
    RwTexture* texture;
    RwGameCubeRasterExtension* rasExt;

    vbHeader = (RxGameCubeVertexBuffer*)(pipeData->resEntry + 1);
    dList = (RxGameCubeDisplayList*)(vbHeader + 1) + (vbHeader->numAttrArrays - 1);
    vbHeader->token = _RwDlTokenCurrent;

    if (RwObjectGetType(object) == rpATOMIC)
    {
        atomic = (RpAtomic*)object;
        ltm = RwFrameGetLTM(RpAtomicGetFrame(atomic));
        _rwDlVtxFmtSetup(
            *RWPLUGINOFFSET(RpGameCubeVtxFmt*, atomic->geometry, _rpDlGeomVtxFmtOffset), pipeData);
    }
    else
    {
        ltm = NULL;
        _rwDlVtxFmtSetup(*RWPLUGINOFFSET(RpGameCubeVtxFmt*, RWSRCGLOBAL(curWorld),
                                         _rpDlWorldVtxFmtOffset),
                         pipeData);
    }

    if (pipeData->flags & rpGEOMETRYNORMALS)
    {
        _rwDlTransformSetup(ltm, TRUE);
    }
    else
    {
        _rwDlTransformSetup(ltm, FALSE);
    }

    matFunc = _rwDlObjectRenderSetup(pipeData->flags, pipeData->lightMask, pipeData->ambientLight,
                                     vbHeader->flags & 1);

    F32 tmtx[3][4] = {
        { xFXanimUVRotMat0[0], xFXanimUVRotMat0[1], xFXanimUVTrans[0], xFXanimUVScale[0] },
        { xFXanimUVRotMat1[0], xFXanimUVRotMat1[1], xFXanimUVTrans[1], xFXanimUVScale[1] },
    };

    GXLoadTexMtxImm(tmtx, GX_TEXMTX0, GX_MTX2x4);
    GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0);

    numMeshes = pipeData->meshHeader->numMeshes;
    mesh = (RpMesh*)(pipeData->meshHeader + 1);

    if (pipeData->flags & (rpGEOMETRYTEXTURED | rpGEOMETRYTEXTURED2))
    {
        while (numMeshes--)
        {
            texture = mesh->material->texture;
            _rwDlTextureSet(texture, 0);

            if (texture != NULL && texture->raster != NULL)
            {
                rasExt =
                    RWPLUGINOFFSET(RwGameCubeRasterExtension, RwRasterGetParent(texture->raster),
                                   _RwGameCubeRasterExtOffset);
                _rwDlRenderStateSetZCompLoc((rasExt->flags & 1) ^ 1);
            }

            if (matFunc != NULL)
            {
                matFunc(&pipeData->ambientLightColor, &mesh->material->color,
                        mesh->material->surfaceProps.ambient);
            }

            GXCallDisplayList(dList->displayList, dList->size);
            dList++;
            mesh++;
        }
    }
    else
    {
        while (numMeshes--)
        {
            if (matFunc != NULL)
            {
                matFunc(&pipeData->ambientLightColor, &mesh->material->color,
                        mesh->material->surfaceProps.ambient);
            }

            GXCallDisplayList(dList->displayList, dList->size);
            dList++;
            mesh++;
        }
    }

    return object;
}

RxPipeline* iFXanimUVCreatePipe()
{
    RxPipeline* newPipe;
    RxPipeline* lpipe;
    RxNodeDefinition* instanceNode;
    RxPipelineNode* node;

    newPipe = RxPipelineCreate();
    if (newPipe != NULL)
    {
        lpipe = RxPipelineLock(newPipe);
        if (lpipe != NULL)
        {
            instanceNode = RxNodeDefinitionGetGameCubeAtomicAllInOne();
            lpipe = RxLockedPipeAddFragment(lpipe, NULL, instanceNode, NULL);
            RxLockedPipeUnlock(lpipe);

            node = RxPipelineFindNodeByName(newPipe, instanceNode->name, NULL, NULL);
            RxGameCubeAllInOneSetRenderCallBack(node, _iGCUVRenderCallback);
            return newPipe;
        }

        _rxPipelineDestroy(newPipe);
        newPipe = NULL;
    }

    return newPipe;
}
