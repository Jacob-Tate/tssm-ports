#include "zSpotlight.h"

#include <rwcore.h>
#include <rpworld.h>

#include "iMath.h"
#include "xCollide.h"
#include "xEvent.h"
#include "xMath.h"
#include "xModel.h"
#include "zBase.h"
#include "zGlobals.h"
#include "zScene.h"
#include "zCam.h"
#include "zRenderState.h"
#include "xFXHighDynamicRange.h"
#include "xLightKit.h"
#include "xMathInlines.h"
#include "xRenderState.h"
#include "xstransvc.h"
#include "xutil.h"
#include "xMemMgr.h"
#include "xMemory.h"
#include "xDebug.h"

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>
#include <stdlib.h>

// Dead-stripped placeholder, not recovered code: retail's .rodata keeps two unreferenced all-zero
// 0x10 templates here, before zSpotlight.cpp's own. The same pair recurs in other wads, so it is
// likely from an uncalled header inline whose body is unknown.
static void __deadstripped_zSpotlight()
{
    F32 a[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    F32 b[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
}

namespace
{
    struct cone_attrib
    {
        F32 radius;
        F32 max_dist;
        F32 slope;
        zSpotlight* spot;
    };

    struct flare_element
    {
        union
        {
            RwRaster* raster;
            S32 sort_key;
        };
        xMat4x3* mat;
        xColor_tag color;
        F32 size;
    };

    struct cone_element
    {
        zSpotlight* spotlight;
    };

    flare_element* flares;
    S32 flares_used;
    cone_element* cones;
    S32 cones_used;
    RpAtomic** light_cone_atomics;
    RpAtomic** aura_cone_atomics;
    S32 light_cone_atomics_size;
    S32 aura_cone_atomics_size;
} // namespace

namespace
{
    S32 compare_cones(const void* e1, const void* e2)
    {
        const cone_attrib& a1 = *(const cone_attrib*)e1;
        const cone_attrib& a2 = *(const cone_attrib*)e2;

        if (!xeq(a1.radius, a2.radius, 0.1f))
        {
            return a1.radius < a2.radius ? -1 : 1;
        }

        if (!xeq(a1.max_dist, a2.max_dist, 0.5f))
        {
            return a1.max_dist < a2.max_dist ? -1 : 1;
        }

        if (!xeq(a1.slope, a2.slope, 0.5f))
        {
            return a1.slope < a2.slope ? -1 : 1;
        }

        return 0;
    }

    void set_cone_tris(RpAtomic* atomic)
    {
        RpTriangle* tri = atomic->geometry->triangles;
        RpTriangle* end_tri = tri + 448;
        S32 index0 = 248;
        S32 index1 = 0;
        S32 end_index1 = 7;

        while (tri != end_tri)
        {
            tri[0].vertIndex[0] = index0;
            tri[0].vertIndex[1] = index1;
            tri[0].vertIndex[2] = index0 + 1;
            tri[1].vertIndex[0] = index0 + 1;
            tri[1].vertIndex[1] = index1;
            tri[1].vertIndex[2] = index1 + 1;
            tri += 2;
            index0++;
            index1++;

            if (index1 == end_index1)
            {
                end_index1 = index1 + 8;
                index0 = index1 - 7;
                index1++;
            }
        }
    }

    void set_cone_verts(RpAtomic* atomic, F32 radius, F32 slope, F32 max_dist)
    {
        RpMorphTarget* mt;
        xVec3* vert;
        xVec3* end_vert;
        F32 dz;
        F32 dradius;
        F32 dcos;
        F32 dsin;
        F32 xdir;
        F32 ydir;
        F32 x;
        F32 y;
        F32 z;
        F32 dx;
        F32 dy;
        xVec3* end_column_vert;
        F32 end_radius;
        F32 hdist;
        F32 hdist2;
        xSphere o;

        mt = atomic->geometry->morphTarget;
        vert = (xVec3*)mt->verts;
        end_vert = vert + 256;

        dz = max_dist / 7.0f;
        dradius = slope * dz;
        dcos = icos(2.0f * PI / 32.0f);
        dsin = isin(2.0f * PI / 32.0f);
        xdir = 1.0f;
        ydir = 0.0f;

        while (vert != end_vert)
        {
            x = dcos * xdir - dsin * ydir;
            y = dsin * xdir + dcos * ydir;
            xdir = x;
            ydir = y;
            x = xdir * radius;
            y = ydir * radius;
            z = 0.0f;
            dx = xdir * dradius;
            dy = ydir * dradius;

            end_column_vert = vert + 8;

            while (vert != end_column_vert)
            {
                vert->x = x;
                x += dx;
                vert->y = y;
                y += dy;
                vert->z = z;
                z += dz;
                vert++;
            }
        }

        end_radius = max_dist * slope + radius;
        hdist = 0.5f * max_dist;
        hdist2 = hdist * hdist;
        o.center.x = 0.0f;
        o.center.y = 0.0f;
        o.center.z = hdist;

        o.r = radius >= end_radius ? xsqrt(radius * radius + hdist2) :
                                     xsqrt(end_radius * end_radius + hdist2);

        mt->boundingSphere = *(RwSphere*)&o;
    }

    // Compiled-out assert placeholder (condition unknown): with it the body has 30 statements or
    // more, so GC/2.6 does not auto-inline it and retail's call is kept.
    RpAtomic* create_cone_atomic(F32, F32, F32)
    {
        xASSERT_PLACEHOLDER();

        RpMaterial* material = RpMaterialCreate();
        RpGeometry* geom = RpGeometryCreate(256, 448, 0x4b);
        RpAtomic* atomic = RpAtomicCreate();
        RwFrame* frame = RwFrameCreate();

        if (geom == NULL || atomic == NULL || frame == NULL)
        {
            if (geom != NULL)
            {
                RpGeometryDestroy(geom);
            }

            if (atomic != NULL)
            {
                RpAtomicDestroy(atomic);
            }

            if (frame != NULL)
            {
                _rwFrameSyncDirty();
                RwFrameDestroy(frame);
            }

            return NULL;
        }

        RpAtomicSetFrame(atomic, frame);
        RpAtomicSetGeometry(atomic, geom, 0);
        RpMaterialSetColor(material, &g_WHITE.rgba);

        RpTriangle* tri = geom->triangles;
        RpTriangle* end_tri = tri + 448;

        while (tri != end_tri)
        {
            RpGeometryTriangleSetMaterial(geom, tri, material);
            tri++;
        }

        *(xMat4x3*)&frame->ltm = g_I3;
        RwMatrixUpdate(&frame->ltm);
        RpGeometryDestroy(geom);
        RpMaterialDestroy(material);
        set_cone_tris(atomic);
        return atomic;
    }

    void set_cone_atomic_color_solid(RpAtomic* atomic, xColor_tag color)
    {
        RpGeometry* geom = atomic->geometry;
        xMemorySetV32A32(geom->preLitLum, *(U32*)&color, 256 * sizeof(xColor_tag));
    }

    void set_cone_atomic_color_faded(RpAtomic* atomic, xColor_tag color)
    {
        xColor_tag* lum = (xColor_tag*)atomic->geometry->preLitLum;
        xColor_tag* end_lum = lum + 256;
        F32 rstart = color.r;
        F32 gstart = color.g;
        F32 bstart = color.b;
        F32 astart = color.a;
        F32 dr = (1.0f / 7.0f) * -rstart;
        F32 dg = (1.0f / 7.0f) * -gstart;
        F32 db = (1.0f / 7.0f) * -bstart;
        F32 da = (1.0f / 7.0f) * -astart;

        while (lum != end_lum)
        {
            F32 r = rstart;
            F32 g = gstart;
            F32 b = bstart;
            F32 a = astart;
            xColor_tag* end_column_lum = lum + 8;

            while (lum != end_column_lum)
            {
                lum->r = r + 0.5f;
                lum->g = g + 0.5f;
                lum->b = b + 0.5f;
                lum->a = a + 0.5f;
                r += dr;
                g += dg;
                b += db;
                a += da;
                lum++;
            }
        }
    }

    RpMeshHeader* cone_stripper(RpBuildMesh* build_mesh, void*)
    {
        RpMeshHeader* mesh_header = _rpMeshHeaderCreate(sizeof(RpMeshHeader) + sizeof(RpMesh) +
                                                        574 * sizeof(RxVertexIndex));
        mesh_header->flags = rpMESHHEADERTRISTRIP;
        mesh_header->numMeshes = 1;
        mesh_header->serialNum = 0;
        mesh_header->totalIndicesInMesh = 574;
        mesh_header->firstMeshOffset = 0;

        RpMesh* mesh = (RpMesh*)(mesh_header + 1);
        RxVertexIndex* indices = (RxVertexIndex*)(mesh + 1);
        mesh->indices = indices;
        mesh->numIndices = 574;
        mesh->material = build_mesh->meshTriangles->material;

        S32 left_index_value = 248;
        S32 right_index_value = 0;

        while (right_index_value < 256)
        {
            RxVertexIndex* end_column = indices + 16;
            S32 il = left_index_value;
            S32 ir = right_index_value;

            while (indices != end_column)
            {
                indices[0] = il++;
                indices[1] = ir++;
                indices += 2;
            }

            left_index_value = right_index_value;
            right_index_value += 8;

            if (right_index_value == 256)
            {
                break;
            }

            indices[0] = ir - 1;
            indices[1] = left_index_value;
            indices += 2;
        }

        return mesh_header;
    }

    void finalize_cone_atomic(RpAtomic* atomic)
    {
        RpTriStripMeshCallBack old_callback;
        void* old_data;

        RpMeshGetTriStripMethod(&old_callback, &old_data);
        RpMeshSetTriStripMethod(cone_stripper, NULL);
        RpGeometry* geom = atomic->geometry;
        RpGeometryUnlock(geom);
        RpMeshSetTriStripMethod(old_callback, old_data);
    }

    void set_atomic_color(RpAtomic* atomic, xColor_tag& color)
    {
        RpGeometry* geom = atomic->geometry;
        S32 materials = geom->matList.numMaterials;

        for (S32 i = 0; i < materials; i++)
        {
            RpMaterial* material = geom->matList.materials[i];
            RpMaterialSetColor(material, (RwRGBA*)&color);
        }
    }

    // No GC symbol: inlined into zSpotlight::scene_exit.
    inline void destroy_atomic(RpAtomic* atomic)
    {
        RwFrame* frame = RpAtomicGetFrame(atomic);
        RpAtomicSetFrame(atomic, NULL);

        if (frame != NULL)
        {
            _rwFrameSyncDirty();
            RwFrameDestroy(frame);
        }

        RpAtomicDestroy(atomic);
    }

    void move_atomic(RpAtomic* atomic, const xMat4x3& mat, F32 scale)
    {
        RwFrame* frame = RpAtomicGetFrame(atomic);
        RwFrameTransform(frame, (RwMatrix*)&mat, rwCOMBINEREPLACE);
        RwV3dTransformPoints(&atomic->worldBoundingSphere.center, &atomic->boundingSphere.center, 1,
                             (RwMatrix*)&mat);
        atomic->worldBoundingSphere.radius = atomic->boundingSphere.radius * scale;
    }
} // namespace

// Inline member defined here rather than in zSpotlight.h: the target emits this weak function in
// zSpotlight's own .text section, and it inlines the file-local move_atomic.
inline void zSpotlight::light_volume_type::render()
{
    move_atomic(atomic, *mat, 1.0f);
    render_atomic(atomic, color, inside, 0);
}

// No GC symbol: inlined into zSceneExit.
inline void zSpotlight::scene_exit()
{
    RpAtomic** light = light_cone_atomics;
    RpAtomic** end_light = light + light_cone_atomics_size;

    for (; light != end_light; ++light)
    {
        destroy_atomic(*light);
    }

    RpAtomic** aura = aura_cone_atomics;
    RpAtomic** end_aura = aura + aura_cone_atomics_size;

    for (; aura != end_aura; ++aura)
    {
        destroy_atomic(*aura);
    }
}

void zSpotlight::setup_all()
{
    light_cone_atomics_size = 0;
    aura_cone_atomics_size = 0;
    flares = NULL;
    cones = NULL;

    zScene& s = *globals.sceneCur;
    zSpotlight* spots = (zSpotlight*)s.baseList[eBaseTypeSpotlight];
    S32 spots_size = s.baseCount[eBaseTypeSpotlight];
    zSpotlight* spot;
    zSpotlight* end_spot = spots + spots_size;

    if (spots_size <= 0)
    {
        return;
    }

    flares = new (gActiveHeap) flare_element[spots_size];
    cones = new (gActiveHeap) cone_element[spots_size];

    for (spot = spots; spot != end_spot; ++spot)
    {
        spot->setup();
    }

    cone_attrib* cone_buffer = (cone_attrib*)RwMalloc(spots_size * sizeof(cone_attrib), 0);
    cone_attrib* cone = cone_buffer;
    cone_attrib* end_cone = cone_buffer + spots_size;

    for (spot = spots; spot != end_spot; ++spot, ++cone)
    {
        cone->radius = spot->asset->radius;
        cone->max_dist = spot->asset->max_dist;
        cone->slope = spot->slope;
        cone->spot = spot;
    }

    qsort(cone_buffer, spots_size, sizeof(cone_attrib), compare_cones);

    RpAtomic** lights = new (gActiveHeap) RpAtomic*[spots_size];
    RpAtomic** auras = new (gActiveHeap) RpAtomic*[spots_size];
    S32 lights_size = 0;
    S32 auras_size = 0;
    cone_attrib* last_light_cone = NULL;
    cone_attrib* last_aura_cone = NULL; // in the PS2 DWARF; never used

    for (cone = cone_buffer; cone != end_cone; ++cone)
    {
        if (last_light_cone == NULL || !xeq(cone->radius, last_light_cone->radius, 0.1f) ||
            !xeq(cone->max_dist, last_light_cone->max_dist, 0.5f) ||
            !xeq(cone->slope, last_light_cone->slope, 0.1f))
        {
            last_light_cone = cone;

            lights[lights_size] = create_cone_atomic(cone->radius, cone->slope, cone->max_dist);
            set_cone_verts(lights[lights_size], cone->radius, cone->slope, cone->max_dist);
            set_cone_atomic_color_solid(lights[lights_size], g_WHITE);
            finalize_cone_atomic(lights[lights_size]);
            lights_size++;

            auras[auras_size] = create_cone_atomic(cone->radius, cone->slope, cone->max_dist);
            set_cone_verts(auras[auras_size], cone->radius, cone->slope, cone->max_dist);
            set_cone_atomic_color_faded(auras[auras_size], g_WHITE);
            finalize_cone_atomic(auras[auras_size]);
            auras_size++;
        }

        cone->spot->aura_atomic = auras[auras_size - 1];
        cone->spot->light_volume = new (xMemAllocSize(sizeof(light_volume_type)))
            light_volume_type(lights[lights_size - 1], cone->spot->volumeMat);
    }

    light_cone_atomics = lights;
    aura_cone_atomics = auras;
    light_cone_atomics_size = lights_size;
    aura_cone_atomics_size = auras_size;
    RwFree(cone_buffer);
}

void zSpotlight::update_all(F32 dt)
{
    zScene& s = *globals.sceneCur;
    zSpotlight* it = (zSpotlight*)s.baseList[eBaseTypeSpotlight];
    zSpotlight* end = it + s.baseCount[eBaseTypeSpotlight];

    for (; it != end; ++it)
    {
        if (it->flags & 0x1)
        {
            it->update(dt);
        }
    }
}

void zSpotlight::reset_all()
{
    zScene& s = *globals.sceneCur;
    zSpotlight* it = (zSpotlight*)s.baseList[eBaseTypeSpotlight];
    zSpotlight* end = it + s.baseCount[eBaseTypeSpotlight];

    for (; it != end; ++it)
    {
        it->reset();
    }
}

// No GC symbol: inlined into zSceneRenderPreFX. Name from Ratatouille.
void zSpotlight::early_render()
{
    zScene& s = *globals.sceneCur;
    zSpotlight* it = (zSpotlight*)s.baseList[eBaseTypeSpotlight];
    zSpotlight* end = it + s.baseCount[eBaseTypeSpotlight];

    for (; it != end; ++it)
    {
        it->refresh_light_volume();
    }
}

void zSpotlight::refresh_light_volume()
{
    if (flags & 0x1)
    {
        volumeMat = mat;
        volumeScale = 1.0f;
        move_atomic(light_volume->atomic, volumeMat, volumeScale);

        if (!xModelSphereCull((xSphere*)&light_volume->atomic->worldBoundingSphere))
        {
            flags &= ~0x100;

            if (!light_volume->active() && !light_volume->activate())
            {
                return;
            }

            light_volume->color = asset->light.color;

            F32 radius = asset->radius;
            F32 view_angle = asset->view_angle;
            xVec3& cam_loc = zCamGetMatrix().pos;

            if (xSphereHitsCone(cam_loc, 0.055f, mat.pos, radius, end_loc, end_radius))
            {
                xMat4x3 scale_mat;
                xMat4x3 expand_mat;
                F32 scale;

                if (xabs(view_angle) <= 0.01f)
                {
                    scale = (0.11f + radius) / radius;
                    xMat4x3ScaleC(&scale_mat, scale, scale, scale);
                    xMat4x3Mul(&expand_mat, &scale_mat, &mat);
                }
                else
                {
                    F32 cam_dist = (cam_loc - mat.pos).dot(mat.at);
                    F32 radius_at_camera = cam_dist * slope + radius;

                    if (radius_at_camera > 0.01f)
                    {
                        scale = 1.0f + 0.11f / radius_at_camera / icos(view_angle);

                        xMat4x3 trans_mat;
                        xMat3x3Identity(&trans_mat);
                        trans_mat.pos.x = 0.0f;
                        trans_mat.pos.y = 0.0f;
                        trans_mat.pos.z = -cam_dist;

                        xMat4x3 itrans_mat;
                        xMat3x3Identity(&itrans_mat);
                        itrans_mat.pos.x = 0.0f;
                        itrans_mat.pos.y = 0.0f;
                        itrans_mat.pos.z = cam_dist;

                        xMat4x3ScaleC(&scale_mat, scale, scale, scale);

                        xMat4x3 temp_mat1;
                        xMat4x3 temp_mat2;
                        xMat4x3Mul(&temp_mat1, &trans_mat, &scale_mat);
                        xMat4x3Mul(&temp_mat2, &temp_mat1, &itrans_mat);
                        xMat4x3Mul(&expand_mat, &temp_mat2, &mat);
                    }
                    else
                    {
                        expand_mat = mat;
                        scale = 1.0f;
                    }
                }

                volumeMat = expand_mat;
                volumeScale = scale;
                move_atomic(light_volume->atomic, volumeMat, volumeScale);
                light_volume->inside = true;
                return;
            }

            volumeMat = mat;
            volumeScale = 1.0f;
            move_atomic(light_volume->atomic, volumeMat, volumeScale);
            light_volume->inside = false;
            return;
        }
    }

    flags |= 0x100;

    if (light_volume->active())
    {
        light_volume->deactivate();
    }
}

void zSpotlight::render_all()
{
    zScene& s = *globals.sceneCur;

    if (s.baseCount[eBaseTypeSpotlight] > 0)
    {
        zRenderState(SDRS_Spotlight);
        cones_used = 0;
        flares_used = 0;

        zSpotlight* it = (zSpotlight*)s.baseList[eBaseTypeSpotlight];
        zSpotlight* end = it + s.baseCount[eBaseTypeSpotlight];

        for (; it != end; ++it)
        {
            if ((it->flags & 0x1) && !(it->flags & 0x100))
            {
                it->add_flare();
                it->add_aura();
            }
        }

        if (flares_used > 0 || cones_used > 0)
        {
            quicksort_int(flares, flares_used, sizeof(flare_element), 0);
            xFXHighDynamicRangeBrighten(true);
            xFXHighDynamicRangeSetGlowClamp(xFXHighDynamicRangeGetBackgroundGlow(), false);
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
            render_auras();
            // The PS2 demo also rendered the flares here; the GC retail build only collects and
            // sorts them.
            xFXHighDynamicRangeResetGlowClamp();
            xFXHighDynamicRangeBrighten(false);
        }
    }
}

void zSpotlight::load(xBase& data, xDynAsset& asset, size_t)
{
    ((zSpotlight&)data).load((zSpotlightAsset&)asset);
}

void zSpotlight::setup()
{
    slope = itan(asset->view_angle);
    reload_asset();
    reset();
}

void zSpotlight::reset()
{
    load_target(asset->target);

    if (asset->flags & 0x1)
    {
        flags |= 0x1;
    }

    flags &= ~0x2;

    if ((asset->flags & 0x4) && attach_ent != NULL && attach_ent->model != NULL &&
        target_ent != NULL)
    {
        flags |= 0x4;
    }

    if (flags & 0x1)
    {
        update_position();
    }
}

void zSpotlight::update(F32 dt)
{
    update_position();

    if (asset->flags & 0x2)
    {
        bool hit = xSphereHitsCone(*xEntGetCenter(xglobals->___player_ent_dont_use_directly), 0.2f,
                                   mat.pos, asset->radius, end_loc, end_radius);

        if (hit && !(flags & 0x2))
        {
            flags |= 0x2;
            zEntEvent(this, this, eEventEnterPlayer);
        }
        else if (!hit && (flags & 0x2))
        {
            flags &= ~0x2;
            zEntEvent(this, this, eEventExitPlayer);
        }
    }

    if (flags & 0x4)
    {
        xMat4x3& model_mat = *xModelGetFrame(attach_ent->model);
        (xMat3x3&)model_mat = mat;
    }
}

void zSpotlight::activate()
{
    if (flags & 0x1)
    {
        return;
    }

    reset();

    if (!(flags & 0x1))
    {
        update_position();
    }

    flags |= 0x1;
}

void zSpotlight::reload_asset()
{
    load_attach_to(asset->attach_to);
    load_target(asset->target);
    load_textures();

    if (asset->flags & 0x1)
    {
        flags |= 0x1;
    }
    else
    {
        flags &= ~0x1;
    }

    slope = itan(asset->view_angle);
    end_radius = asset->radius + asset->max_dist * slope;
    end_loc.x = mat.at.x * asset->max_dist + mat.pos.x;
    end_loc.y = mat.at.y * asset->max_dist + mat.pos.y;
    end_loc.z = mat.at.z * asset->max_dist + mat.pos.z;
}

void zSpotlight::load_attach_to(U32 id)
{
    xBase* ent = zSceneFindObject(id);

    if (ent != NULL && (ent->baseFlags & 0x20) && ((xEnt*)ent)->model != NULL)
    {
        attach_ent = (xEnt*)ent;
    }
    else
    {
        attach_ent = NULL;
        mat = g_I3;
    }
}

void zSpotlight::load_textures()
{
    RwTexture* texture_flare =
        asset->flare.texture == 0 ? NULL : (RwTexture*)xSTFindAsset(asset->flare.texture, NULL);
    raster_flare = texture_flare == NULL ? NULL : texture_flare->raster;

    if (raster_flare != NULL)
    {
        xFXHighDynamicRangeTunePalette(raster_flare, 1.0f, true);
    }
}

void zSpotlight::add_flare()
{
    flare_element* flare = &flares[flares_used];
    flares_used++;
    flare->raster = raster_flare;
    flare->mat = &mat;
    flare->color = asset->flare.color;

    F32 mag = mat.at.dot(zCamGetMatrix().at);

    if (mag < 0.0f)
    {
        mag *= mag;
        mag *= mag;
        F32 size0 = asset->flare.size_min;
        F32 dsize = asset->flare.size_max - size0;
        flare->size = dsize * mag + size0;
        F32 glow0 = asset->flare.glow_min;
        F32 dglow = asset->flare.glow_max - glow0;
        flare->color.a = 0.5f + (dglow * mag + glow0);
    }
    else
    {
        flare->color.a = asset->flare.glow_min;
        flare->size = asset->flare.size_min;
    }
}

void zSpotlight::add_aura()
{
    cone_element* cone = &cones[cones_used];
    cones_used++;
    cone->spotlight = this;
}

void zSpotlight::render_aura()
{
    xLightKit_Enable(NULL, globals.currWorld);
    move_atomic(aura_atomic, volumeMat, volumeScale);

    xColor_tag color = asset->aura.color;
    set_atomic_color(aura_atomic, color);
    xRenderFixUntexturedBegin(aura_atomic);
    RwRenderStateSet(rwRENDERSTATECULLMODE,
                     (void*)(light_volume->inside ? rwCULLMODECULLFRONT : rwCULLMODECULLBACK));
    RpAtomicRender(aura_atomic);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
    xRenderFixUntexturedEnd(aura_atomic);
}

void zSpotlight::render_auras()
{
    cone_element* cone = cones;
    cone_element* end_cone = cones + cones_used;

    for (; cone != end_cone; ++cone)
    {
        cone->spotlight->render_aura();
    }
}

void zSpotlight::dispatch(xBase* from, U32 event, const F32* argf, xBase* toParamWidget,
                          U32 toParamWidgetID)
{
    switch (event)
    {
    case eEventReset:
        reset();
        break;
    case eEventOn:
        activate();
        break;
    case eEventOff:
        deactivate();
        break;
    }
}

void zSpotlight::cb_dispatch(xBase* from, xBase* to, U32 event, const F32* argf,
                             xBase* toParamWidget, U32 toParamWidgetID)
{
    zSpotlight& e = *(zSpotlight*)to;
    e.dispatch(from, event, argf, toParamWidget, toParamWidgetID);
}
