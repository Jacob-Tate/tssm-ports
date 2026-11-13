// RenderWare C API entry points that TSSM calls and BfBB does not.
//
// The rest of this directory is treedome's shim as it stands
// (tools/pcimport.py --check compares it with treedome); the functions TSSM
// needs beyond it are kept here, so that comparison stays clean. They follow
// the same rules: a RenderWare object IS the librw object it is mirrored onto
// (rwsdk/*.h, asserted in layout*.cpp), so most of these are a cast and a call.
//
// Callers, all measured by the first PC link:
//   xDecal.cpp        RpAtomicCreate, RpMaterialCreate/Destroy, RpSkinCreate,
//                     RpSkinGeometrySetSkin, RpSkinAtomicGetType,
//                     RpSkinAtomicGetHAnimHierarchy,
//                     RpMatFXMaterialSetUVTransformMatrices
//   zSpotlight.cpp    RpAtomicCreate, RpMaterialCreate/Destroy,
//                     RpMeshGet/SetTriStripMethod, _rpMeshHeaderCreate
//   xFX.cpp           RpMatFXMaterialSetupDualTexture, ...SetUVTransformMatrices
//   xScrFx.cpp        RwMatrixMultiply, RwRasterSetFromImage, RwTextureSetRaster
//   pc/iScrFX.cpp     RwMatrixMultiply
//   xFXCameraTexture.cpp, zNMETypeBossNeptune.cpp   RwCameraSetViewOffset
//   zAssetTypes.cpp   RwFrameSetIdentity
//   xModelBucket.cpp  RpClumpStreamWrite
//   pc/iModel.cpp     RwV3dTransformVectors
//
// Two are only partly RenderWare's here and say so at the site: the tri-strip
// method (librw builds triangle lists and never calls a custom stripper) and
// _rpMeshHeaderCreate (reached only from such a stripper).

#include <rwcore.h>
#include <rpmatfx.h>
#include <rpskin.h>
#include <rpworld.h>

#include "iStub.h"
#include "stream.h" // brings in librw's rw.h, which must not be included twice

#include <stdlib.h>
#include <string.h>

// --- RpAtomic, RpMaterial -------------------------------------------------

RpAtomic* RpAtomicCreate(void)
{
    // A bare atomic: no frame, no geometry, the default render callback and
    // pipeline, as RenderWare's. Both callers give it a geometry and a frame
    // on the next lines.
    return reinterpret_cast<RpAtomic*>(rw::Atomic::create());
}

RpMaterial* RpMaterialCreate(void)
{
    // White, opaque, untextured, surface properties (1, 1, 1): librw's
    // defaults are RenderWare's.
    return reinterpret_cast<RpMaterial*>(rw::Material::create());
}

RwBool RpMaterialDestroy(RpMaterial* material)
{
    if (material == NULL)
    {
        return FALSE;
    }

    // Reference counted, as RenderWare's: xDecal.cpp and zSpotlight.cpp drop
    // their creation reference right after handing the material to a geometry,
    // which holds its own.
    reinterpret_cast<rw::Material*>(material)->destroy();
    return TRUE;
}

// --- RpClump ----------------------------------------------------------------

RpClump* RpClumpStreamWrite(RpClump* clump, RwStream* stream)
{
    if (clump == NULL || stream == NULL)
    {
        return NULL;
    }

    // FullAtomicDupe (xModelBucket.cpp) writes a one-atomic clump to a memory
    // stream and reads it back. librw writes the whole rwID_CLUMP chunk, header
    // included, as RenderWare does.
    if (!reinterpret_cast<rw::Clump*>(clump)->streamWrite(stream))
    {
        return NULL;
    }
    return clump;
}

// --- RpSkin -----------------------------------------------------------------

RpSkin* RpSkinCreate(RwUInt32 numVertices, RwUInt32 numBones, RwMatrixWeights* weights,
                     RwUInt32* indices, const RwMatrix* skinToBoneMatrices)
{
    // librw's skin reader, old-format branch (skin.cpp readSkin), fed from
    // memory: one block for the used-bone list, the inverse bone matrices, and
    // four indices and four weights per vertex. The strides are the ones
    // skin.cpp's accessors assert in layout_geometry.cpp: an RwMatrix is the 16
    // floats librw keeps per bone, an RwMatrixWeights the 4 floats per vertex,
    // and a packed RwUInt32 of indices is librw's 4 bytes per vertex on a
    // little-endian host.
    rw::Skin* skin = rwNewT(rw::Skin, 1, rw::MEMDUR_EVENT | rw::ID_SKIN);
    skin->init((rw::int32)numBones, (rw::int32)numBones, (rw::int32)numVertices);
    skin->legacyType = 0;

    if (skin->inverseMatrices != NULL && skinToBoneMatrices != NULL)
    {
        memcpy(skin->inverseMatrices, skinToBoneMatrices, 64 * numBones);
    }
    if (skin->indices != NULL && indices != NULL)
    {
        memcpy(skin->indices, indices, 4 * numVertices);
    }
    if (skin->weights != NULL && weights != NULL)
    {
        memcpy(skin->weights, weights, 16 * numVertices);
    }

    skin->findNumWeights((rw::int32)numVertices);
    skin->findUsedBones((rw::int32)numVertices);
    return reinterpret_cast<RpSkin*>(skin);
}

RpGeometry* RpSkinGeometrySetSkin(RpGeometry* geometry, RpSkin* skin)
{
    if (geometry == NULL)
    {
        return NULL;
    }

    // The geometry takes the skin over: librw's skin plugin frees it with the
    // geometry, as RenderWare's does.
    rw::Skin::set(reinterpret_cast<rw::Geometry*>(geometry), reinterpret_cast<rw::Skin*>(skin));
    return geometry;
}

RpHAnimHierarchy* RpSkinAtomicGetHAnimHierarchy(const RpAtomic* atomic)
{
    if (atomic == NULL)
    {
        return NULL;
    }

    return reinterpret_cast<RpHAnimHierarchy*>(
        rw::Skin::getHierarchy(reinterpret_cast<const rw::Atomic*>(atomic)));
}

RpSkinType RpSkinAtomicGetType(const RpAtomic* atomic)
{
    // librw keeps no skin type: RpSkinAtomicSetType (skin.cpp) turns it into a
    // pipeline. So the type is read back from the pipeline -- the combined
    // skin-and-matfx one means rpSKINTYPEMATFX, anything else generic. Toon has
    // no pipeline here and nothing in TSSM sets it.
    if (atomic == NULL)
    {
        return rpSKINTYPEGENERIC;
    }

    const rw::Atomic* a = reinterpret_cast<const rw::Atomic*>(atomic);
    rw::ObjPipeline* matfx = rw::skinGlobals.matfxPipelines[rw::platform];
    if (matfx != NULL && a->pipeline == matfx)
    {
        return rpSKINTYPEMATFX;
    }
    return rpSKINTYPEGENERIC;
}

// --- RpMatFX ------------------------------------------------------------------
//
// Like matfx.cpp's Setup calls, these require RpMatFXMaterialSetEffects to
// have been called first with an effect that includes this one; librw looks the
// effect up in the material's two slots and does nothing when it is absent.

RpMaterial* RpMatFXMaterialSetupDualTexture(RpMaterial* material, RwTexture* texture,
                                            RwBlendFunction srcBlendMode,
                                            RwBlendFunction dstBlendMode)
{
    if (material == NULL)
    {
        return NULL;
    }

    rw::MatFX* fx = rw::MatFX::get(reinterpret_cast<rw::Material*>(material));
    if (fx == NULL)
    {
        return NULL;
    }

    // RwBlendFunction and rw::BlendFunction share their values (rwBLENDZERO and
    // BLENDZERO are both 1); layout_camera.cpp asserts the blend enumerations.
    fx->setDualTexture(reinterpret_cast<rw::Texture*>(texture));
    fx->setDualSrcBlend((rw::int32)srcBlendMode);
    fx->setDualDestBlend((rw::int32)dstBlendMode);
    return material;
}

RpMaterial* RpMatFXMaterialSetUVTransformMatrices(RpMaterial* material, RwMatrix* baseTransform,
                                                  RwMatrix* dualTransform)
{
    if (material == NULL)
    {
        return NULL;
    }

    rw::MatFX* fx = rw::MatFX::get(reinterpret_cast<rw::Material*>(material));
    if (fx == NULL)
    {
        return NULL;
    }

    // librw keeps the pointers, not copies. xFX.cpp's matrices are globals
    // (gFXUVs), and xDecal.cpp points at a local again right before each draw
    // (xDecal.cpp:1707), so both are valid while the material draws.
    fx->setUVTransformMatrices(reinterpret_cast<rw::Matrix*>(baseTransform),
                               reinterpret_cast<rw::Matrix*>(dualTransform));
    return material;
}

// --- RpMesh: the tri-strip method -------------------------------------------
//
// RenderWare lets an application replace the function RpGeometryUnlock uses to
// turn triangles into meshes; zSpotlight.cpp installs its own (cone_stripper)
// for the one unlock that builds its cone, then puts the old one back. librw's
// unlock always builds triangle lists from the geometry's triangles and has no
// such hook, so the cone is drawn from its triangle list instead of the strip
// zSpotlight would have built. The method is still stored and handed back, so
// the save-and-restore around the unlock behaves as on the console.

static RpTriStripMeshCallBack sTriStripCallBack = NULL;
static void* sTriStripData = NULL;

RwBool RpMeshSetTriStripMethod(RpTriStripMeshCallBack callback, void* data)
{
    sTriStripCallBack = callback;
    sTriStripData = data;
    return TRUE;
}

RwBool RpMeshGetTriStripMethod(RpTriStripMeshCallBack* callback, void** data)
{
    if (callback != NULL)
    {
        *callback = sTriStripCallBack;
    }
    if (data != NULL)
    {
        *data = sTriStripData;
    }
    return TRUE;
}

RpMeshHeader* _rpMeshHeaderCreate(RwUInt32 size)
{
    // RenderWare's allocator for a stripper's result. Only a custom tri-strip
    // method calls it, and librw never calls one (above), so this is not
    // reached; if it is, it allocates zeroed memory of the size asked for, as
    // RenderWare's does, and reports itself once.
    IPORT_STUB();
    return reinterpret_cast<RpMeshHeader*>(calloc(1, size));
}

// --- RwCamera, RwFrame, RwMatrix, RwV3d -------------------------------------

RwCamera* RwCameraSetViewOffset(RwCamera* camera, const RwV2d* offset)
{
    if (camera == NULL || offset == NULL)
    {
        return NULL;
    }

    reinterpret_cast<rw::Camera*>(camera)->setViewOffset(reinterpret_cast<const rw::V2d*>(offset));
    return camera;
}

RwFrame* RwFrameSetIdentity(RwFrame* frame)
{
    if (frame == NULL)
    {
        return NULL;
    }

    // RenderWare's: identity modelling matrix, then mark the frame dirty.
    rw::Frame* f = reinterpret_cast<rw::Frame*>(frame);
    f->matrix.setIdentity();
    f->updateObjects();
    return frame;
}

RwMatrix* RwMatrixMultiply(RwMatrix* matrixOut, const RwMatrix* matrixIn1,
                           const RwMatrix* matrixIn2)
{
    // matrixIn1 then matrixIn2, RenderWare's order, which librw's mult keeps.
    // Neither library allows matrixOut to alias an input; no caller does.
    rw::Matrix::mult(reinterpret_cast<rw::Matrix*>(matrixOut),
                     reinterpret_cast<const rw::Matrix*>(matrixIn1),
                     reinterpret_cast<const rw::Matrix*>(matrixIn2));
    return matrixOut;
}

RwV3d* RwV3dTransformVectors(RwV3d* vectorsOut, const RwV3d* vectorsIn, RwInt32 numPoints,
                             const RwMatrix* matrix)
{
    // Vectors, not points: the rotation part only, no translation.
    rw::V3d::transformVectors(reinterpret_cast<rw::V3d*>(vectorsOut),
                              reinterpret_cast<const rw::V3d*>(vectorsIn), numPoints,
                              reinterpret_cast<const rw::Matrix*>(matrix));
    return vectorsOut;
}

// --- RwRaster, RwTexture ------------------------------------------------------

RwRaster* RwRasterSetFromImage(RwRaster* raster, RwImage* image)
{
    if (raster == NULL || image == NULL)
    {
        return NULL;
    }

    return reinterpret_cast<RwRaster*>(
        reinterpret_cast<rw::Raster*>(raster)->setFromImage(reinterpret_cast<rw::Image*>(image)));
}

RwTexture* RwTextureSetRaster(RwTexture* texture, RwRaster* raster)
{
    if (texture == NULL)
    {
        return NULL;
    }

    // The raster it replaces is not freed, here or in RenderWare: xScrFx.cpp
    // destroys the old one itself, right after this call.
    reinterpret_cast<rw::Texture*>(texture)->raster = reinterpret_cast<rw::Raster*>(raster);
    return texture;
}
