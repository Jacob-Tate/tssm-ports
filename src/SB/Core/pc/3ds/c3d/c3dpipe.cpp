// rw::c3d: the object pipelines.
//
// Each geometry is instanced once, into linear memory: one vertex buffer and
// one u16 index buffer, each mesh a range of the indices. Positions are
// rebased on the geometry's centre, so the PICA's 24-bit floats stay precise
// far from the origin, and the render folds the centre back into the matrix.
//
// Lit models get the ambient and up to four directional lights in the vertex
// shader (the GL3 device's formula), MatFX materials their UV transform and
// environment map, and skinned models are skinned in the vertex shader
// (c3dskin.cpp has the palette and the split) or, as TSSM_C3D_SKIN=cpu, from
// the same vertices on the CPU into the frame ring.

#include "c3dimpl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace rw
{
    namespace c3d
    {
        namespace
        {
            enum UVMode
            {
                UV_NONE, // the texture coordinates as they are
                UV_GLOBAL, // rw::uvTransform (GetUVTransformPipeline)
                UV_MATFX // each material's MatFX UV transform
            };

            struct Pipe : rw::ObjPipeline
            {
                int32 uvMode;
                bool32 skinned;
                bool32 envMaps; // MatFX's environment map (the MatFX pipelines)
            };

            void freeInstanceData(Geometry* geo)
            {
                if (geo->instData == nil || geo->instData->platform != PLATFORM_C3D)
                {
                    return;
                }
                InstanceDataHeader* h = (InstanceDataHeader*)geo->instData;
                geo->instData = nil;
                vbVramForget(h);
                // The GPU may still be reading them (this frame, or the one
                // in flight).
                deferFree(h->vertexBuffer);
                deferFree(h->indexBuffer);
                rwFree(h->srcMap);
                rwFree(h->pieces);
                rwFree(h->inst);
                rwFree(h);
            }

            void packNormal(int8* q, const V3d& n)
            {
                float c[3] = { n.x, n.y, n.z };
                for (int i = 0; i < 3; i++)
                {
                    float f = c[i] * 127.0f;
                    f = f > 127.0f ? 127.0f : f < -127.0f ? -127.0f : f;
                    q[i] = (int8)(f < 0.0f ? f - 0.5f : f + 0.5f);
                }
                q[3] = 0;
            }

            // The model format a geometry instances as (MODEL_SKIN aside).
            uint32 formatOf(Geometry* geo)
            {
                uint32 format = 0;
                if ((geo->flags & Geometry::NORMALS) && geo->morphTargets[0].normals != nil)
                    format |= MODEL_NORMAL;
                if ((geo->flags & Geometry::PRELIT) && geo->colors != nil)
                    format |= MODEL_COLOR;
                if (geo->numTexCoordSets > 0 && geo->texCoords[0] != nil)
                    format |= MODEL_UV;
                return format;
            }

            // Changed vertices only (positions, normals, prelight, the first
            // texture coordinates: the cutscenes' morphs, PTank's billboards,
            // UV animations): the same layout, in a new vertex buffer that
            // starts as a copy of the old one, which goes once the GPU is done
            // with it. The meshes, the indices, a skin's split and its bone
            // bytes stay. Each vertex gets the bytes a full instancing would
            // give it, so the pictures are the same. FALSE: instance it from
            // the start.
            bool refreshInstance(Geometry* geo, InstanceDataHeader* h, bool skinned, bool rebase)
            {
                const int32 locked = geo->lockedSinceInst;
                const int32 kCan = Geometry::LOCKVERTICES | Geometry::LOCKNORMALS |
                                   Geometry::LOCKPRELIGHT | Geometry::LOCKTEXCOORDSALL;
                MeshHeader* mh = geo->meshHeader;
                if ((locked & ~kCan) != 0 || mh == nil || h->serialNumber != mh->serialNum ||
                    h->numMeshes != mh->numMeshes || geo->numVertices <= 0 ||
                    geo->morphTargets == nil || geo->morphTargets[0].vertices == nil ||
                    h->vertexBuffer == nil || (h->format & ~MODEL_SKIN) != formatOf(geo) ||
                    (skinned ? h->pieces == nil || h->srcVertices != (uint32)geo->numVertices
                             : h->pieces != nil || h->totalNumVertex != (uint32)geo->numVertices))
                {
                    return false;
                }
                // TSSM_C3D_VBVRAM moved it: not read back from VRAM.
                if ((uintptr_t)h->vertexBuffer >= 0x1F000000u &&
                    (uintptr_t)h->vertexBuffer < 0x1F600000u)
                {
                    return false;
                }
                const uint32 format = h->format;
                const uint32 stride = h->stride;
                const uint32 n = h->totalNumVertex;
                const V3d* pos = geo->morphTargets[0].vertices;
                const V3d* nrm = geo->morphTargets[0].normals;

                // A plain model made again is rebased on its first vertex
                // (instanceMesh's `changed`).
                V3d center = h->center;
                if (!skinned && rebase)
                {
                    center = pos[0];
                    if (!(fabsf(center.x) < 1e30f && fabsf(center.y) < 1e30f &&
                          fabsf(center.z) < 1e30f))
                    {
                        center.x = center.y = center.z = 0.0f;
                    }
                }
                const bool wPos = (locked & Geometry::LOCKVERTICES) || center.x != h->center.x ||
                                  center.y != h->center.y || center.z != h->center.z;
                const bool wNrm = (format & MODEL_NORMAL) && (locked & Geometry::LOCKNORMALS);
                const bool wCol = (format & MODEL_COLOR) && (locked & Geometry::LOCKPRELIGHT);
                const bool wUV = (format & MODEL_UV) && (locked & Geometry::LOCKTEXCOORDS);
                if (!wPos && !wNrm && !wCol && !wUV)
                {
                    return true; // nothing the model holds
                }
                // Everything a plain model holds is written again: no copy.
                const bool all = !skinned && wPos && (wNrm || !(format & MODEL_NORMAL)) &&
                                 (wCol || !(format & MODEL_COLOR)) &&
                                 (wUV || !(format & MODEL_UV));

                uint8* buf = (uint8*)linearAlloc((size_t)n * stride);
                if (buf == nil)
                {
                    return false;
                }
                if (!all)
                {
                    memcpy(buf, h->vertexBuffer, (size_t)n * stride);
                }
                uint32 off = 12;
                const uint32 offNormal = off;
                if (format & MODEL_NORMAL)
                    off += 4;
                const uint32 offColor = off;
                if (format & MODEL_COLOR)
                    off += 4;
                const uint32 offUV = off;
                const uint16* map = skinned ? h->srcMap : nil;
                const TexCoords* uv = geo->texCoords[0];
                for (uint32 i = 0; i < n; i++)
                {
                    const uint32 v = map != nil ? map[i] : i;
                    uint8* d = buf + (size_t)i * stride;
                    if (wPos)
                    {
                        float* p = (float*)d;
                        p[0] = pos[v].x - center.x;
                        p[1] = pos[v].y - center.y;
                        p[2] = pos[v].z - center.z;
                    }
                    if (wNrm)
                    {
                        packNormal((int8*)(d + offNormal), nrm[v]);
                    }
                    if (wUV)
                    {
                        float* t = (float*)(d + offUV);
                        t[0] = uv[v].u;
                        t[1] = uv[v].v;
                    }
                    if (wCol && skinned)
                    {
                        const RGBA& c = geo->colors[v];
                        d[offColor] = c.red;
                        d[offColor + 1] = c.green;
                        d[offColor + 2] = c.blue;
                        d[offColor + 3] = c.alpha;
                    }
                }
                if (wCol)
                {
                    // instanceMesh's and instanceSkin's vertex alpha (and a
                    // plain model's colours, as instanceMesh writes them).
                    Mesh* mesh = mh->getMeshes();
                    for (uint32 m = 0; m < h->numMeshes; m++, mesh++)
                    {
                        InstanceData* inst = &h->inst[m];
                        if (skinned)
                        {
                            uint8 a = 0xFF;
                            for (uint32 k = 0; mesh->indices != nil && k < mesh->numIndices; k++)
                            {
                                a &= geo->colors[mesh->indices[k]].alpha;
                            }
                            inst->vertexAlpha = a != 0xFF;
                        }
                        else if (inst->numVertices > 0)
                        {
                            inst->vertexAlpha = instColor(
                                VERT_RGBA, buf + offColor + (size_t)stride * inst->minVert,
                                geo->colors + inst->minVert, inst->numVertices, stride);
                        }
                    }
                }
                GSPGPU_FlushDataCache(buf, (u32)n * stride);
                vbVramForget(h);
                deferFree(h->vertexBuffer);
                h->vertexBuffer = buf;
                h->center = center;
                return true;
            }

            void fillVertices(Geometry* geo, InstanceDataHeader* h)
            {
                const uint32 stride = h->stride;
                const int32 n = geo->numVertices;
                uint8* base = h->vertexBuffer;
                const V3d* pos = geo->morphTargets[0].vertices;
                const V3d* nrm = geo->morphTargets[0].normals;
                uint32 off = 12;
                const uint32 offNormal = off;
                if (h->format & MODEL_NORMAL)
                    off += 4;
                const uint32 offColor = off;
                if (h->format & MODEL_COLOR)
                    off += 4;
                const uint32 offUV = off;

                for (int32 v = 0; v < n; v++)
                {
                    uint8* d = base + (size_t)v * stride;
                    float* p = (float*)d;
                    p[0] = pos[v].x - h->center.x;
                    p[1] = pos[v].y - h->center.y;
                    p[2] = pos[v].z - h->center.z;
                    if (h->format & MODEL_NORMAL)
                    {
                        int8* q = (int8*)(d + offNormal);
                        float c[3] = { nrm[v].x, nrm[v].y, nrm[v].z };
                        for (int i = 0; i < 3; i++)
                        {
                            float f = c[i] * 127.0f;
                            f = f > 127.0f ? 127.0f : f < -127.0f ? -127.0f : f;
                            q[i] = (int8)(f < 0.0f ? f - 0.5f : f + 0.5f);
                        }
                        q[3] = 0;
                    }
                    if (h->format & MODEL_UV)
                    {
                        float* t = (float*)(d + offUV);
                        t[0] = geo->texCoords[0][v].u;
                        t[1] = geo->texCoords[0][v].v;
                    }
                }

                if (h->format & MODEL_COLOR)
                {
                    // instColor also says whether the mesh has vertex alpha.
                    for (uint32 i = 0; i < h->numMeshes; i++)
                    {
                        InstanceData* inst = &h->inst[i];
                        if (inst->numVertices <= 0)
                        {
                            continue;
                        }
                        inst->vertexAlpha =
                            instColor(VERT_RGBA, base + offColor + (size_t)stride * inst->minVert,
                                      geo->colors + inst->minVert, inst->numVertices, stride);
                    }
                }
            }

            // `rebase`: positions relative to a centre. The bounding sphere's
            // is the best for a geometry read from a file. One instanced again
            // because it changed (`changed`), or without a sphere, has only
            // its vertices to go by: PTank rewrites its billboards every frame
            // and its sphere stays at the origin. The first vertex is the
            // centre then, not the middle of the box, because PTank's unused
            // particles are zeros, which would pull the box to the origin.
            InstanceDataHeader* instanceMesh(Geometry* geo, bool rebase, bool changed)
            {
                MeshHeader* mh = geo->meshHeader;
                if (mh == nil || geo->numVertices <= 0 || geo->morphTargets == nil ||
                    geo->morphTargets[0].vertices == nil)
                {
                    return nil;
                }
                if (geo->numVertices > 65536)
                {
                    logOnce("bigmesh", "a geometry of %d vertices was not drawn (u16 indices)",
                            (int)geo->numVertices);
                    return nil;
                }

                uint32 format = 0;
                if ((geo->flags & Geometry::NORMALS) && geo->morphTargets[0].normals != nil)
                    format |= MODEL_NORMAL;
                if ((geo->flags & Geometry::PRELIT) && geo->colors != nil)
                    format |= MODEL_COLOR;
                if (geo->numTexCoordSets > 0 && geo->texCoords[0] != nil)
                    format |= MODEL_UV;

                InstanceDataHeader* h = rwNewT(InstanceDataHeader, 1, MEMDUR_EVENT | ID_GEOMETRY);
                memset(h, 0, sizeof(*h));
                h->platform = PLATFORM_C3D;
                h->serialNumber = mh->serialNum;
                h->numMeshes = mh->numMeshes;
                h->primType =
                    mh->flags == MeshHeader::TRISTRIP ? GPU_TRIANGLE_STRIP : GPU_TRIANGLES;
                h->totalNumIndex = mh->totalIndices;
                h->totalNumVertex = geo->numVertices;
                h->format = format;
                h->stride = modelStride(format);
                h->inst = rwNewT(InstanceData, h->numMeshes > 0 ? h->numMeshes : 1,
                                 MEMDUR_EVENT | ID_GEOMETRY);
                h->indexBuffer = (uint16*)linearAlloc(
                    (h->totalNumIndex > 0 ? h->totalNumIndex : 1) * sizeof(uint16));
                h->vertexBuffer = (uint8*)linearAlloc((size_t)h->totalNumVertex * h->stride);
                if (h->indexBuffer == nil || h->vertexBuffer == nil)
                {
                    logOnce("geomfull", "linear memory is full (%u KB free); a model was not drawn",
                            (unsigned)(linearSpaceFree() >> 10));
                    if (h->indexBuffer != nil)
                        linearFree(h->indexBuffer);
                    if (h->vertexBuffer != nil)
                        linearFree(h->vertexBuffer);
                    rwFree(h->inst);
                    rwFree(h);
                    return nil;
                }

                Mesh* mesh = mh->getMeshes();
                uint32 offset = 0;
                for (uint32 i = 0; i < h->numMeshes; i++, mesh++)
                {
                    InstanceData* inst = &h->inst[i];
                    inst->numIndex = mesh->numIndices;
                    inst->material = mesh->material;
                    inst->vertexAlpha = 0;
                    inst->offset = offset;
                    findMinVertAndNumVertices(mesh->indices, mesh->numIndices, &inst->minVert,
                                              &inst->numVertices);
                    if (mesh->numIndices > 0 && mesh->indices != nil &&
                        offset + mesh->numIndices <= h->totalNumIndex)
                    {
                        memcpy(h->indexBuffer + offset, mesh->indices,
                               mesh->numIndices * sizeof(uint16));
                    }
                    else
                    {
                        inst->numIndex = 0;
                    }
                    offset += mesh->numIndices;
                }

                if (rebase)
                {
                    const Sphere& sphere = geo->morphTargets[0].boundingSphere;
                    h->center = changed || !(sphere.radius > 0.0f) ?
                                    geo->morphTargets[0].vertices[0] :
                                    sphere.center;
                    // A first vertex that is not a number would take every
                    // other one with it.
                    if (!(fabsf(h->center.x) < 1e30f && fabsf(h->center.y) < 1e30f &&
                          fabsf(h->center.z) < 1e30f))
                    {
                        h->center.x = h->center.y = h->center.z = 0.0f;
                    }
                }
                fillVertices(geo, h);
                GSPGPU_FlushDataCache(h->vertexBuffer, (u32)h->totalNumVertex * h->stride);
                GSPGPU_FlushDataCache(h->indexBuffer, (u32)h->totalNumIndex * sizeof(uint16));
                return h;
            }

            // TSSM_C3D_REFRESHCHECK=1 (a check): each refresh compared with a
            // full instancing of the same geometry made beside it: the same
            // centre, indices and vertex alpha, and the same bytes in every
            // vertex an index reaches. A line every 2 s; the first mismatch
            // in detail.
            struct RefreshCheck
            {
                int on = -1;
                uint32 checked, bad, verts;
                u64 last;
            } sRefreshCheck;

            void refreshCheck(Geometry* geo, InstanceDataHeader* h, bool skinned)
            {
                RefreshCheck& rc = sRefreshCheck;
                if (rc.on < 0)
                {
                    const char* e = getenv("TSSM_C3D_REFRESHCHECK");
                    rc.on = e != nil && e[0] == '1';
                    rc.last = svcGetSystemTick();
                }
                if (!rc.on)
                {
                    return;
                }
                InstanceDataHeader* r =
                    skinned ? instanceSkin(geo, true) : instanceMesh(geo, true, true);
                if (r == nil)
                {
                    return;
                }
                const char* why = nil;
                if (r->totalNumVertex != h->totalNumVertex || r->stride != h->stride ||
                    r->totalNumIndex != h->totalNumIndex || r->numMeshes != h->numMeshes ||
                    r->format != h->format)
                {
                    why = "layout";
                }
                else if (memcmp(&r->center, &h->center, sizeof(V3d)) != 0)
                {
                    why = "centre";
                }
                else if (memcmp(r->indexBuffer, h->indexBuffer,
                                (size_t)h->totalNumIndex * sizeof(uint16)) != 0)
                {
                    why = "indices";
                }
                else
                {
                    for (uint32 m = 0; m < h->numMeshes && why == nil; m++)
                    {
                        if ((r->inst[m].vertexAlpha != 0) != (h->inst[m].vertexAlpha != 0))
                        {
                            why = "vertex alpha";
                        }
                    }
                    for (uint32 i = 0; i < h->totalNumIndex && why == nil; i++)
                    {
                        const uint32 v = h->indexBuffer[i];
                        if (v < h->totalNumVertex &&
                            memcmp(r->vertexBuffer + (size_t)v * h->stride,
                                   h->vertexBuffer + (size_t)v * h->stride, h->stride) != 0)
                        {
                            why = "vertex bytes";
                            if (rc.bad == 0)
                            {
                                printf("tssm: c3d: refresh check: vertex %u of %p differs (format "
                                       "%#x, stride %u, locked %#x)\n",
                                       (unsigned)v, (void*)geo, (unsigned)h->format,
                                       (unsigned)h->stride, (unsigned)geo->lockedSinceInst);
                            }
                        }
                    }
                }
                rc.checked++;
                rc.verts += h->totalNumVertex;
                if (why != nil)
                {
                    if (rc.bad == 0)
                    {
                        printf("tssm: c3d: refresh check: the first mismatch: %s (%p, %u vertices, "
                               "%s)\n",
                               why, (void*)geo, (unsigned)h->totalNumVertex,
                               skinned ? "skinned" : "plain");
                    }
                    rc.bad++;
                }
                linearFree(r->vertexBuffer);
                linearFree(r->indexBuffer);
                rwFree(r->srcMap);
                rwFree(r->pieces);
                rwFree(r->inst);
                rwFree(r);
                const u64 now = svcGetSystemTick();
                if (now - rc.last >= 2ull * SYSCLOCK_ARM11)
                {
                    printf("tssm: c3d: refresh check: %u refreshes checked (%u vertices), %u differ "
                           "from a full instancing\n",
                           (unsigned)rc.checked, (unsigned)rc.verts, (unsigned)rc.bad);
                    fflush(stdout);
                    rc.last = now;
                }
            }

            void instance(rw::ObjPipeline* rwpipe, Atomic* atomic)
            {
                Pipe* pipe = (Pipe*)rwpipe;
                Geometry* geo = atomic->geometry;
                if (geo == nil)
                {
                    return;
                }
                // librw's Atomic::instance (the game's RpAtomicInstance) marks
                // a geometry NATIVE once it is instanced, and keeps its
                // portable data: one with this platform's instance data is
                // ours and may be instanced again. Without any, or with
                // another platform's, it was never converted off the Xbox.
                InstanceDataHeader* h = (InstanceDataHeader*)geo->instData;
                if ((h != nil && h->platform != PLATFORM_C3D) ||
                    (h == nil && (geo->flags & Geometry::NATIVE)))
                {
                    static Geometry* told[4];
                    static int ntold;
                    bool seen = false;
                    for (int i = 0; i < ntold; i++)
                    {
                        seen = seen || told[i] == geo;
                    }
                    if (!seen && ntold < 4)
                    {
                        told[ntold++] = geo;
                        printf("tssm: c3d: a geometry in another platform's format was not drawn "
                               "(platform %d, %d vertices, %d triangles, flags %#x)\n",
                               h != nil ? (int)h->platform : -1, (int)geo->numVertices,
                               (int)geo->numTriangles, (unsigned)geo->flags);
                        fflush(stdout);
                    }
                    return;
                }
                // Changed since it was instanced (PTank's billboards, morphs):
                // instanced again into new buffers; the old ones are freed
                // after the GPU is done with them.
                InstProf& ip = gInstProf;
                const u64 t0 = ip.on ? svcGetSystemTick() : 0;
                bool changed = false;
                if (h != nil && geo->lockedSinceInst && modes.refresh &&
                    refreshInstance(geo, h, pipe->skinned != 0, !pipe->skinned))
                {
                    refreshCheck(geo, h, pipe->skinned != 0);
                    if (ip.on)
                    {
                        ip.refreshed++;
                        ip.refreshVerts += h->totalNumVertex;
                        ip.refresh += svcGetSystemTick() - t0;
                        ip.total += svcGetSystemTick() - t0;
                    }
                    geo->lockedSinceInst = 0;
                    return;
                }
                if (h != nil &&
                    (geo->meshHeader == nil || h->serialNumber != geo->meshHeader->serialNum ||
                     geo->lockedSinceInst))
                {
                    if (ip.on)
                    {
                        // Each geometry made again, once: what changed.
                        static Geometry* seen[24];
                        static int nseen;
                        bool known = false;
                        for (int i = 0; i < nseen; i++)
                        {
                            known = known || seen[i] == geo;
                        }
                        if (!known && nseen < 24)
                        {
                            seen[nseen++] = geo;
                            printf("tssm: c3d: instprof: made again: %p, %d vertices, %d triangles, "
                                   "%d meshes, %d morph targets, flags %#x, locked %#x, serial "
                                   "%s, %s, skin %s\n",
                                   (void*)geo, (int)geo->numVertices, (int)geo->numTriangles,
                                   geo->meshHeader != nil ? (int)geo->meshHeader->numMeshes : -1,
                                   (int)geo->numMorphTargets, (unsigned)geo->flags,
                                   (unsigned)geo->lockedSinceInst,
                                   geo->meshHeader == nil ? "no meshes"
                                   : h->serialNumber != geo->meshHeader->serialNum ? "changed"
                                                                                   : "same",
                                   pipe->skinned ? "skin pipeline" : "plain pipeline",
                                   Skin::get(geo) != nil ? "yes" : "no");
                            fflush(stdout);
                        }
                    }
                    freeInstanceData(geo);
                    h = nil;
                    changed = true;
                }
                const u64 t1 = ip.on ? svcGetSystemTick() : 0;
                if (h == nil)
                {
                    // A skinned geometry without its weights draws as it is.
                    h = pipe->skinned ? instanceSkin(geo, changed) : nil;
                    const u64 t2 = ip.on ? svcGetSystemTick() : 0;
                    geo->instData = h != nil ? h : instanceMesh(geo, !pipe->skinned, changed);
                    if (ip.on)
                    {
                        const u64 t3 = svcGetSystemTick();
                        ip.freeing += t1 - t0;
                        if (h == nil)
                        {
                            ip.mesh += t3 - t2;
                            ip.meshes++;
                            ip.meshVerts += (uint32)geo->numVertices;
                        }
                        ip.total += t3 - t0;
                        if (!changed)
                        {
                            ip.first++;
                        }
                    }
                    if (!changed && modes.skin != SKIN_CPU)
                    {
                        // TSSM_C3D_VBVRAM: not what is made again (PTank's),
                        // nor what the CPU reads (skin=cpu).
                        vbVramQueue((InstanceDataHeader*)geo->instData);
                    }
                }
                geo->lockedSinceInst = 0;
            }

            void uninstance(rw::ObjPipeline*, Atomic*)
            {
                logOnce("uninstance", "uninstancing is not supported");
            }

            // MatFX::setupEnv's matrix takes a world normal; the shader has
            // the object's, so the world's normal matrix (its axes made unit
            // length, GL3's u_normal) is folded in: the two rows of
            // uv = T^T (Mn n) + T.pos.
            void envRows(Atomic* atomic, const RawMatrix& t, float rows[8])
            {
                Matrix* ltm = atomic->getFrame()->getLTM();
                V3d axes[3] = { ltm->right, ltm->up, ltm->at };
                for (int a = 0; a < 3; a++)
                {
                    float len = sqrtf(axes[a].x * axes[a].x + axes[a].y * axes[a].y +
                                      axes[a].z * axes[a].z);
                    if (len > 1e-8f)
                    {
                        axes[a].x /= len;
                        axes[a].y /= len;
                        axes[a].z /= len;
                    }
                }
                for (int a = 0; a < 3; a++)
                {
                    rows[a] = axes[a].x * t.right.x + axes[a].y * t.up.x + axes[a].z * t.at.x;
                    rows[4 + a] = axes[a].x * t.right.y + axes[a].y * t.up.y + axes[a].z * t.at.y;
                }
                rows[3] = t.pos.x;
                rows[7] = t.pos.y;
            }

            // TSSM_C3D_STATS=1: each environment-mapped material once.
            void noteEnvMaterial(Material* m, const MatFX::Env* env, const MatFXEnvState& es)
            {
                static int on = -1;
                static Material* seen[64];
                static int nseen;
                if (on < 0)
                {
                    const char* e = getenv("TSSM_C3D_STATS");
                    on = e != nil && e[0] != '\0' && e[0] != '0';
                }
                if (!on || nseen >= 64)
                {
                    return;
                }
                for (int i = 0; i < nseen; i++)
                {
                    if (seen[i] == m)
                    {
                        return;
                    }
                }
                seen[nseen++] = m;
                printf("tssm: c3d: environment map: texture %s over %s, coefficient %.3f, colour "
                       "%.2f %.2f %.2f, %s frame, fb alpha %d\n",
                       env->tex != nil ? env->tex->name : "-",
                       m->texture != nil ? m->texture->name : "-", env->coefficient, es.color.red,
                       es.color.green, es.color.blue,
                       env->frame != nil ? "its own" : "the camera's", (int)env->fbAlpha);
                fflush(stdout);
            }

            uint8 unit8(float f)
            {
                f = f * 255.0f + 0.5f;
                return (uint8)(f > 255.0f ? 255.0f : f < 0.0f ? 0.0f : f);
            }

            // One draw's material, texture, MatFX and state, as GL3's
            // matfxDefaultRender and matfxEnvRender set them. FALSE: left out.
            // *env: drawn with the environment map (SRCBLEND is ONE until
            // endMesh).
            bool beginMesh(Atomic* atomic, InstanceData* inst, int32 uvMode, bool32 envMaps,
                           bool* env)
            {
                static const RGBA white = { 255, 255, 255, 255 };
                ProfScope prof(PROF_MESH);
                Geometry* geo = atomic->geometry;
                Material* m = inst->material;
                *env = false;
                if (uvMode == UV_MATFX)
                {
                    float32 xf[NUMUVTRANSFORMELEMENTS];
                    const bool uv = MatFX::getUVTransform(m, xf);
                    setUVMatrix(uv ? xf : nil);
                    if (uv && Skin::get(geo) != nil && m->texture != nil)
                    {
                        logOnce("skinuv", "a skinned mesh with a UV transform: %s",
                                m->texture->name);
                    }
                }
                setMaterial((geo->flags & Geometry::MODULATE) ? m->color : white, m->surfaceProps);
                setTexture(m->texture);

                MatFX* fx = envMaps && modes.env ? MatFX::get(m) : nil;
                MatFXEnvState es;
                if (fx != nil && fx->type == MatFX::ENVMAP &&
                    MatFX::setupEnv(&es, m, &fx->fx[0].env))
                {
                    if (es.colorClamp.red < 1.0f)
                    {
                        logOnce("envlight", "an environment map tinted by the lighting is "
                                            "drawn untinted");
                    }
                    noteEnvMaterial(m, &fx->fx[0].env, es);
                    float rows[8];
                    envRows(atomic, es.texMatrix, rows);
                    const uint32 rgba = (uint32)unit8(es.color.red * es.shininess) |
                                        (uint32)unit8(es.color.green * es.shininess) << 8 |
                                        (uint32)unit8(es.color.blue * es.shininess) << 16 |
                                        0xFF000000u;
                    setEnvMap(fx->fx[0].env.tex, rows, rgba, es.disableFBA == 0.0f);
                    setPipelineVertexAlpha(1);
                    rw::SetRenderState(SRCBLEND, BLENDONE);
                    *env = true;
                }
                else
                {
                    setEnvMap(nil, nil, 0, 0);
                    setPipelineVertexAlpha(inst->vertexAlpha || m->color.alpha != 0xFF);
                }
                if (!flushState())
                {
                    if (*env)
                    {
                        rw::SetRenderState(SRCBLEND, BLENDSRCALPHA);
                    }
                    return false;
                }
                return true;
            }

            void endMesh(bool env)
            {
                if (env)
                {
                    rw::SetRenderState(SRCBLEND, BLENDSRCALPHA);
                }
            }

            void atomicSetup(Atomic* atomic, const V3d* rebase, int32 uvMode)
            {
                Geometry* geo = atomic->geometry;
                const bool lit = (geo->flags & Geometry::LIGHT) != 0;
                {
                    ProfScope prof(PROF_MATRIX);
                    atomicDepth(atomic);
                    setWorldMatrix(atomic->getFrame()->getLTM(), rebase);
                }
                {
                    ProfScope prof(PROF_LIGHTS);
                    setLights(lit ? atomic : nil);
                }
                hudLogAtomic(atomic, modes.skin == SKIN_CPU && Skin::get(geo) != nil
                                         ? "skinned on the CPU"
                                         : "drawn whole or in skinned pieces");
                if (uvMode == UV_GLOBAL)
                {
                    setUVMatrix(uvTransform);
                }
                else if (uvMode == UV_NONE)
                {
                    setUVMatrix(nil);
                }
            }

            void renderMeshes(Atomic* atomic, InstanceDataHeader* h, const Pipe* pipe)
            {
                atomicSetup(atomic, &h->center, pipe->uvMode);
                const bool lit = (atomic->geometry->flags & Geometry::LIGHT) != 0;
                for (uint32 i = 0; i < h->numMeshes; i++)
                {
                    InstanceData* inst = &h->inst[i];
                    bool env;
                    if (inst->numIndex == 0 ||
                        !beginMesh(atomic, inst, pipe->uvMode, pipe->envMaps, &env))
                    {
                        continue;
                    }
                    {
                        ProfScope prof(PROF_C3D);
                        useProgram(env ? PROG_ENV : lit && modes.light ? PROG_LIT : PROG_WORLD);
                        useFormat((int32)h->format, h->vertexBuffer, h->stride);
                        drawElements(h->primType, (int32)inst->numIndex,
                                     h->indexBuffer + inst->offset);
                    }
                    endMesh(env);
                }
            }

            // SKIN_CPU: the whole geometry skinned into the ring, then its
            // pieces drawn as an unskinned model's meshes.
            void renderPiecesCpu(Atomic* atomic, InstanceDataHeader* h, const Pipe* pipe)
            {
                const uint32 format = h->format & ~MODEL_SKIN;
                const uint32 stride = modelStride(format);
                if ((uintptr_t)h->vertexBuffer >= 0x1F000000u &&
                    (uintptr_t)h->vertexBuffer < 0x1F600000u)
                {
                    logOnce("vbcpu", "vbvram: a skinned model in VRAM is not skinned on the CPU "
                                     "(left out)");
                    return;
                }
                uint8* v = (uint8*)ringAlloc(h->totalNumVertex * stride);
                if (v == nil)
                {
                    return;
                }
                const u64 t0 = svcGetSystemTick();
                skinBegin(atomic, 0);
                const u64 t1 = svcGetSystemTick();
                skinCpu(h, v);
                noteTicks(TIME_CPUSKIN, svcGetSystemTick() - t1);
                noteSkinTicks(t1 - t0);
                atomicSetup(atomic, nil, pipe->uvMode);
                const bool lit = (atomic->geometry->flags & Geometry::LIGHT) != 0;
                uint32 drawn = 0;
                for (uint32 i = 0; i < h->numPieces; i++)
                {
                    const SkinPiece* p = &h->pieces[i];
                    bool env;
                    if (p->numIndex == 0 ||
                        !beginMesh(atomic, &h->inst[p->mesh], pipe->uvMode, pipe->envMaps, &env))
                    {
                        continue;
                    }
                    {
                        ProfScope prof(PROF_C3D);
                        useProgram(env ? PROG_ENV : lit && modes.light ? PROG_LIT : PROG_WORLD);
                        useFormat((int32)format, v, stride);
                        drawElements(h->primType, (int32)p->numIndex, h->indexBuffer + p->offset);
                    }
                    endMesh(env);
                    drawn++;
                }
                noteCount(COUNT_SKIN_PIECES, drawn);
                noteCount(COUNT_SKIN_VERTS, h->totalNumVertex);
            }

            void renderPieces(Atomic* atomic, InstanceDataHeader* h, const Pipe* pipe)
            {
                const u64 t0 = svcGetSystemTick();
                skinBegin(atomic, modes.skin == SKIN_BIND);
                u64 ticks = svcGetSystemTick() - t0;
                atomicSetup(atomic, nil, pipe->uvMode);
                uint32 regs = 0;
                uint32 drawn = 0;
                for (uint32 i = 0; i < h->numPieces; i++)
                {
                    const SkinPiece* p = &h->pieces[i];
                    if (p->numIndex == 0)
                    {
                        continue;
                    }
                    const u64 t1 = svcGetSystemTick();
                    regs += (uint32)skinPalette(p);
                    const u64 palette = svcGetSystemTick() - t1;
                    ticks += palette;
                    noteProf(PROF_PALETTE, palette);
                    bool env;
                    if (!beginMesh(atomic, &h->inst[p->mesh], pipe->uvMode, pipe->envMaps, &env))
                    {
                        continue;
                    }
                    {
                        ProfScope prof(PROF_C3D);
                        useProgram(env ? PROG_SKINENV : PROG_SKIN);
                        useFormat((int32)h->format, h->vertexBuffer, h->stride);
                        drawElements(h->primType, (int32)p->numIndex, h->indexBuffer + p->offset);
                    }
                    endMesh(env);
                    drawn++;
                }
                noteSkinTicks(ticks);
                noteCount(COUNT_BONE_REGS, regs);
                noteCount(COUNT_SKIN_PIECES, drawn);
                noteCount(COUNT_SKIN_VERTS, h->totalNumVertex);
            }

            // TSSM_C3D_INSTPROF=1: the line, every 2 s (ms a second).
            void instProfTick(void)
            {
                static int on = -1;
                static u64 last;
                InstProf& ip = gInstProf;
                if (on < 0)
                {
                    const char* e = getenv("TSSM_C3D_INSTPROF");
                    on = e != nil && e[0] == '1';
                    ip.on = on != 0;
                    last = svcGetSystemTick();
                }
                if (!on)
                {
                    return;
                }
                const u64 now = svcGetSystemTick();
                const u64 win = now - last;
                if (win < 2ull * SYSCLOCK_ARM11)
                {
                    return;
                }
                const double k = 1000.0 / (double)win; // ticks -> ms a second
                printf("tssm: c3d: instprof: %.2f s: instancing %.1f ms a second (freeing %.1f; "
                       "skin: %.1f a second, %u vertices a second, prep %.1f, stats pack %.1f, "
                       "alloc %.1f, write %.1f, flush %.1f, print %.1f; mesh: %.1f a second, %u "
                       "vertices a second, %.1f ms; refreshed: %.1f a second, %u vertices a "
                       "second, %.1f ms); first-time %u\n",
                       (double)win / SYSCLOCK_ARM11, ip.total * k, ip.freeing * k,
                       ip.skins * (double)SYSCLOCK_ARM11 / (double)win,
                       (unsigned)(ip.skinVerts * (double)SYSCLOCK_ARM11 / win), ip.prep * k,
                       ip.stats * k, ip.alloc * k, ip.write * k, ip.flush * k, ip.print * k,
                       ip.meshes * (double)SYSCLOCK_ARM11 / (double)win,
                       (unsigned)(ip.meshVerts * (double)SYSCLOCK_ARM11 / win), ip.mesh * k,
                       ip.refreshed * (double)SYSCLOCK_ARM11 / (double)win,
                       (unsigned)(ip.refreshVerts * (double)SYSCLOCK_ARM11 / win),
                       ip.refresh * k, (unsigned)ip.first);
                printf("tssm: c3d: instprof: L3: light cache %u of %u atomics, state fast path %u "
                       "of %u draws\n",
                       (unsigned)gL3.lightHits, (unsigned)gL3.lightCalls, (unsigned)gL3.stateHits,
                       (unsigned)gL3.stateCalls);
                memset(&gL3, 0, sizeof(gL3));
                fflush(stdout);
                const bool keep = ip.on;
                memset(&ip, 0, sizeof(ip));
                ip.on = keep;
                last = svcGetSystemTick();
            }

            void render(rw::ObjPipeline* rwpipe, Atomic* atomic)
            {
                if (drawsDiscarded())
                {
                    return;
                }
                instProfTick();
                RecordScope timed;
                Pipe* pipe = (Pipe*)rwpipe;
                pipe->instance(atomic);
                Geometry* geo = atomic->geometry;
                if (geo == nil || geo->instData == nil || geo->instData->platform != PLATFORM_C3D)
                {
                    return;
                }
                InstanceDataHeader* h = (InstanceDataHeader*)geo->instData;
                if (h->pieces == nil)
                {
                    renderMeshes(atomic, h, pipe);
                    return;
                }
                if (modes.skin == SKIN_CPU)
                {
                    renderPiecesCpu(atomic, h, pipe);
                }
                else if (modes.skin != SKIN_OFF)
                {
                    renderPieces(atomic, h, pipe);
                }
            }

            Pipe* makePipe(int32 uvMode, bool32 skinned, bool32 envMaps)
            {
                Pipe* pipe = rwNewT(Pipe, 1, MEMDUR_GLOBAL);
                pipe->init(PLATFORM_C3D);
                pipe->impl.instance = c3d::instance;
                pipe->impl.uninstance = c3d::uninstance;
                pipe->impl.render = c3d::render;
                pipe->uvMode = uvMode;
                pipe->skinned = skinned;
                pipe->envMaps = envMaps;
                return pipe;
            }
        } // namespace

        void* destroyNativeData(void* object, int32, int32)
        {
            freeInstanceData((Geometry*)object);
            return object;
        }

        ObjPipeline* makeDefaultPipeline(void)
        {
            return makePipe(UV_NONE, 0, 0);
        }

        ObjPipeline* makeUVTransformPipeline(void)
        {
            return makePipe(UV_GLOBAL, 0, 0);
        }

        ObjPipeline* makeMatFXPipeline(void)
        {
            Pipe* pipe = makePipe(UV_MATFX, 0, 1);
            pipe->pluginID = ID_MATFX;
            pipe->pluginData = 0;
            return pipe;
        }

        // GL3's: the plain one draws no material effect, the MatFX one both.
        ObjPipeline* makeSkinPipeline(bool32 matfx)
        {
            Pipe* pipe = makePipe(matfx ? UV_MATFX : UV_NONE, 1, matfx);
            pipe->pluginID = ID_SKIN;
            pipe->pluginData = 1;
            return pipe;
        }
    } // namespace c3d
} // namespace rw
