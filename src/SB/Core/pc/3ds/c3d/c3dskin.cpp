// rw::c3d: skinning on the GPU.
//
// The vertex shader sees PALETTE_BONES bones at once (u_bones, 3 rows each).
// A skinned geometry that uses no more is instanced as it is, one palette for
// all of it. A bigger one is split as it is instanced: each mesh's triangles
// are packed first-fit, in their order, into pieces of at most PALETTE_BONES
// bones, and each piece gets its own copy of its vertices, whose bone bytes
// are palette slots x 3 (the first of the bone's rows, for mova). A bone that
// two consecutive pieces share keeps its slot, so it is not sent again.

#include "c3dimpl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <vector>

namespace rw
{
    namespace c3d
    {
        namespace
        {
            struct BoneSet
            {
                uint64 w[4];

                void clear(void)
                {
                    w[0] = w[1] = w[2] = w[3] = 0;
                }
                void add(uint32 b)
                {
                    w[b >> 6] |= (uint64)1 << (b & 63);
                }
                bool has(uint32 b) const
                {
                    return (w[b >> 6] >> (b & 63)) & 1;
                }
                int countWith(const BoneSet& o) const
                {
                    int n = 0;
                    for (int i = 0; i < 4; i++)
                    {
                        n += __builtin_popcountll(w[i] | o.w[i]);
                    }
                    return n;
                }
                void merge(const BoneSet& o)
                {
                    for (int i = 0; i < 4; i++)
                    {
                        w[i] |= o.w[i];
                    }
                }
            };

            struct Tri
            {
                uint16 v[3];
            };

            // The weights as bytes summing to 255, and the bones they name.
            // An unused weight is 0 on the first bone; a vertex with no weight
            // at all is all on its first listed bone.
            void vertexWeights(const Skin* skin, int32 v, uint8 bones[4], uint8 weights[4],
                               int32* noWeights)
            {
                const float* w = &skin->weights[v * 4];
                const uint8* ix = &skin->indices[v * 4];
                float sum = w[0] + w[1] + w[2] + w[3];
                if (!(sum > 0.0f))
                {
                    (*noWeights)++;
                    bones[0] = ix[0];
                    weights[0] = 255;
                    for (int k = 1; k < 4; k++)
                    {
                        bones[k] = ix[0];
                        weights[k] = 0;
                    }
                    return;
                }
                int total = 0;
                int big = 0;
                for (int k = 0; k < 4; k++)
                {
                    float f = w[k] > 0.0f ? w[k] / sum * 255.0f + 0.5f : 0.0f;
                    weights[k] = (uint8)(f > 255.0f ? 255.0f : f);
                    bones[k] = weights[k] != 0 ? ix[k] : ix[0];
                    total += weights[k];
                    if (weights[k] > weights[big])
                    {
                        big = k;
                    }
                }
                weights[big] = (uint8)(weights[big] + 255 - total);
            }

            void triBones(const uint8 (*vb)[4], const uint8 (*vw)[4], const Tri& t, BoneSet* set)
            {
                set->clear();
                for (int i = 0; i < 3; i++)
                {
                    for (int k = 0; k < 4; k++)
                    {
                        if (vw[t.v[i]][k] != 0)
                        {
                            set->add(vb[t.v[i]][k]);
                        }
                    }
                }
            }

            // A mesh's triangles: strips unrolled with their winding kept,
            // degenerate ones dropped.
            void meshTris(const Mesh* mesh, bool strip, std::vector<Tri>* out)
            {
                out->clear();
                const uint16* ix = mesh->indices;
                if (ix == nil)
                {
                    return;
                }
                if (!strip)
                {
                    for (uint32 i = 0; i + 2 < mesh->numIndices; i += 3)
                    {
                        Tri t = { { ix[i], ix[i + 1], ix[i + 2] } };
                        out->push_back(t);
                    }
                    return;
                }
                for (uint32 i = 0; i + 2 < mesh->numIndices; i++)
                {
                    uint16 a = ix[i], b = ix[i + 1], c = ix[i + 2];
                    if (a == b || b == c || a == c)
                    {
                        continue;
                    }
                    Tri t;
                    if (i & 1)
                    {
                        t.v[0] = b;
                        t.v[1] = a;
                    }
                    else
                    {
                        t.v[0] = a;
                        t.v[1] = b;
                    }
                    t.v[2] = c;
                    out->push_back(t);
                }
            }

            // First-fit: each triangle into the first piece it fits. Returns
            // the piece count; triPiece[i] is triangle i's piece.
            int pack(const std::vector<Tri>& tris, const std::vector<BoneSet>& sets, int limit,
                     std::vector<int>* triPiece, std::vector<BoneSet>* pieces)
            {
                pieces->clear();
                triPiece->resize(tris.size());
                for (size_t i = 0; i < tris.size(); i++)
                {
                    size_t p = 0;
                    for (; p < pieces->size(); p++)
                    {
                        if ((*pieces)[p].countWith(sets[i]) <= limit)
                        {
                            break;
                        }
                    }
                    if (p == pieces->size())
                    {
                        BoneSet e;
                        e.clear();
                        pieces->push_back(e);
                    }
                    (*pieces)[p].merge(sets[i]);
                    (*triPiece)[i] = (int)p;
                }
                return (int)pieces->size();
            }

            // Vertices a split gives: each piece's distinct ones.
            int32 splitVertices(const std::vector<Tri>& tris, const std::vector<int>& triPiece,
                                int numPieces, int32 numVerts, std::vector<int32>* mark)
            {
                int32 n = 0;
                for (int p = 0; p < numPieces; p++)
                {
                    mark->assign(numVerts, -1);
                    for (size_t i = 0; i < tris.size(); i++)
                    {
                        if (triPiece[i] != p)
                        {
                            continue;
                        }
                        for (int k = 0; k < 3; k++)
                        {
                            if ((*mark)[tris[i].v[k]] < 0)
                            {
                                (*mark)[tris[i].v[k]] = 1;
                                n++;
                            }
                        }
                    }
                }
                return n;
            }

            void writeVertex(Geometry* geo, uint32 format, uint32 stride, uint8* d, int32 v,
                             const uint8 slots[4], const uint8 weights[4])
            {
                const V3d& p = geo->morphTargets[0].vertices[v];
                float* fp = (float*)d;
                fp[0] = p.x;
                fp[1] = p.y;
                fp[2] = p.z;
                uint32 off = 12;
                if (format & MODEL_NORMAL)
                {
                    const V3d& n = geo->morphTargets[0].normals[v];
                    int8* q = (int8*)(d + off);
                    float c[3] = { n.x, n.y, n.z };
                    for (int i = 0; i < 3; i++)
                    {
                        float f = c[i] * 127.0f;
                        f = f > 127.0f ? 127.0f : f < -127.0f ? -127.0f : f;
                        q[i] = (int8)(f < 0.0f ? f - 0.5f : f + 0.5f);
                    }
                    q[3] = 0;
                    off += 4;
                }
                if (format & MODEL_COLOR)
                {
                    const RGBA& c = geo->colors[v];
                    d[off] = c.red;
                    d[off + 1] = c.green;
                    d[off + 2] = c.blue;
                    d[off + 3] = c.alpha;
                    off += 4;
                }
                if (format & MODEL_UV)
                {
                    float* t = (float*)(d + off);
                    t[0] = geo->texCoords[0][v].u;
                    t[1] = geo->texCoords[0][v].v;
                    off += 8;
                }
                for (int k = 0; k < 4; k++)
                {
                    d[off + k] = (uint8)(slots[k] * 3);
                    d[off + 4 + k] = weights[k];
                }
                (void)stride;
            }

            bool statsOn(void)
            {
                static int on = -1;
                if (on < 0)
                {
                    const char* e = getenv("TSSM_C3D_STATS");
                    on = e != nil && e[0] != '\0' && e[0] != '0';
                }
                return on != 0;
            }

            // The run's totals, for the log.
            struct SplitTotals
            {
                int32 geometries;
                int32 split; // needed more than one palette
                int32 pieces10, pieces20; // what a palette of 10 or 20 bones gives
                int32 verts, verts10, verts20;
                int32 maxBones;
                int32 noWeights;
            } sTotals;

            Matrix* sBones; // the current atomic's, in object space
            int32 sNumBones;
            int32 sBonesCap;
            bool sBonesIdentity;
        } // namespace

        InstanceDataHeader* instanceSkin(Geometry* geo)
        {
            Skin* skin = Skin::get(geo);
            MeshHeader* mh = geo->meshHeader;
            if (skin == nil || skin->indices == nil || skin->weights == nil || mh == nil ||
                geo->numVertices <= 0 || geo->morphTargets == nil ||
                geo->morphTargets[0].vertices == nil)
            {
                return nil;
            }
            const int32 nv = geo->numVertices;

            uint32 format = MODEL_SKIN;
            if ((geo->flags & Geometry::NORMALS) && geo->morphTargets[0].normals != nil)
                format |= MODEL_NORMAL;
            if ((geo->flags & Geometry::PRELIT) && geo->colors != nil)
                format |= MODEL_COLOR;
            if (geo->numTexCoordSets > 0 && geo->texCoords[0] != nil)
                format |= MODEL_UV;
            const uint32 stride = modelStride(format);

            std::vector<uint8> vbones((size_t)nv * 4), vweights((size_t)nv * 4);
            uint8(*vb)[4] = (uint8(*)[4])vbones.data();
            uint8(*vw)[4] = (uint8(*)[4])vweights.data();
            int32 noWeights = 0;
            BoneSet used;
            used.clear();
            for (int32 v = 0; v < nv; v++)
            {
                vertexWeights(skin, v, vb[v], vw[v], &noWeights);
                for (int k = 0; k < 4; k++)
                {
                    if (vw[v][k] != 0)
                    {
                        used.add(vb[v][k]);
                    }
                }
            }
            BoneSet none;
            none.clear();
            const int numUsed = used.countWith(none);
            const bool whole = numUsed <= PALETTE_BONES;
            const bool strip = mh->flags == MeshHeader::TRISTRIP;

            // Per mesh: triangles, their bone sets, the packing.
            const uint32 nm = mh->numMeshes;
            std::vector<std::vector<Tri> > tris(nm);
            std::vector<std::vector<int> > triPiece(nm);
            std::vector<std::vector<BoneSet> > pieceSets(nm);
            std::vector<BoneSet> sets;
            std::vector<int> tp10;
            std::vector<BoneSet> ps10;
            std::vector<int32> mark;
            int32 pieces10 = 0, pieces20 = 0, verts10 = 0, verts20 = 0;
            Mesh* meshes = mh->getMeshes();
            for (uint32 m = 0; m < nm; m++)
            {
                meshTris(&meshes[m], strip, &tris[m]);
                sets.resize(tris[m].size());
                for (size_t i = 0; i < tris[m].size(); i++)
                {
                    triBones(vb, vw, tris[m][i], &sets[i]);
                }
                int n = pack(tris[m], sets, PALETTE_BONES, &triPiece[m], &pieceSets[m]);
                pieces20 += n;
                verts20 += splitVertices(tris[m], triPiece[m], n, nv, &mark);
                if (statsOn())
                {
                    int n10 = pack(tris[m], sets, 10, &tp10, &ps10);
                    pieces10 += n10;
                    verts10 += splitVertices(tris[m], tp10, n10, nv, &mark);
                }
            }

            // The vertices and indices.
            uint32 numPieces = 0;
            for (uint32 m = 0; m < nm; m++)
            {
                numPieces += whole ? 1 : (uint32)pieceSets[m].size();
            }
            int32 outVerts = whole ? nv : verts20;
            uint32 outIndex = 0;
            for (uint32 m = 0; m < nm; m++)
            {
                outIndex += whole ? meshes[m].numIndices : (uint32)tris[m].size() * 3;
            }
            if (outVerts > 65536)
            {
                logOnce("bigskin",
                        "a skinned geometry of %d vertices after its split was not "
                        "drawn (u16 indices)",
                        (int)outVerts);
                return nil;
            }

            InstanceDataHeader* h = rwNewT(InstanceDataHeader, 1, MEMDUR_EVENT | ID_GEOMETRY);
            memset(h, 0, sizeof(*h));
            h->platform = PLATFORM_C3D;
            h->serialNumber = mh->serialNum;
            h->numMeshes = nm;
            h->primType = whole && strip ? GPU_TRIANGLE_STRIP : GPU_TRIANGLES;
            h->totalNumIndex = outIndex;
            h->totalNumVertex = (uint32)outVerts;
            h->format = format;
            h->stride = stride;
            h->inst = rwNewT(InstanceData, nm > 0 ? nm : 1, MEMDUR_EVENT | ID_GEOMETRY);
            h->pieces =
                rwNewT(SkinPiece, numPieces > 0 ? numPieces : 1, MEMDUR_EVENT | ID_GEOMETRY);
            h->numPieces = numPieces;
            memset(h->pieces, 0, sizeof(SkinPiece) * (numPieces > 0 ? numPieces : 1));
            h->indexBuffer = (uint16*)linearAlloc((outIndex > 0 ? outIndex : 1) * sizeof(uint16));
            h->vertexBuffer = (uint8*)linearAlloc((size_t)outVerts * stride);
            if (h->indexBuffer == nil || h->vertexBuffer == nil)
            {
                logOnce("geomfull", "linear memory is full (%u KB free); a model was not drawn",
                        (unsigned)(linearSpaceFree() >> 10));
                if (h->indexBuffer != nil)
                    linearFree(h->indexBuffer);
                if (h->vertexBuffer != nil)
                    linearFree(h->vertexBuffer);
                rwFree(h->pieces);
                rwFree(h->inst);
                rwFree(h);
                return nil;
            }

            uint8 prev[PALETTE_BONES]; // the previous piece's bone per slot
            int prevSlots = 0;
            uint32 pi = 0;
            uint32 offset = 0;
            int32 vbase = 0;
            if (whole)
            {
                // One palette, the used bones in order; the vertices as they are.
                uint8 slotOf[256];
                SkinPiece pal;
                memset(&pal, 0, sizeof(pal));
                for (uint32 b = 0; b < 256; b++)
                {
                    if (used.has(b))
                    {
                        slotOf[b] = pal.numSlots;
                        pal.bones[pal.numSlots++] = (uint8)b;
                    }
                }
                for (int32 v = 0; v < nv; v++)
                {
                    uint8 slots[4];
                    for (int k = 0; k < 4; k++)
                    {
                        slots[k] = vw[v][k] != 0 ? slotOf[vb[v][k]] : 0;
                    }
                    writeVertex(geo, format, stride, h->vertexBuffer + (size_t)v * stride, v, slots,
                                vw[v]);
                }
                for (uint32 m = 0; m < nm; m++)
                {
                    SkinPiece* p = &h->pieces[pi++];
                    *p = pal;
                    p->mesh = m;
                    p->firstVert = 0;
                    p->numVerts = m == 0 ? (uint32)nv : 0;
                    p->offset = offset;
                    p->numIndex = meshes[m].numIndices;
                    if (meshes[m].indices != nil && p->numIndex > 0)
                    {
                        memcpy(h->indexBuffer + offset, meshes[m].indices,
                               p->numIndex * sizeof(uint16));
                    }
                    else
                    {
                        p->numIndex = 0;
                    }
                    offset += meshes[m].numIndices;
                }
            }
            else
            {
                std::vector<int32> local(nv);
                for (uint32 m = 0; m < nm; m++)
                {
                    for (size_t q = 0; q < pieceSets[m].size(); q++)
                    {
                        SkinPiece* p = &h->pieces[pi++];
                        p->mesh = m;
                        p->offset = offset;
                        // Slots: a bone the previous piece had keeps its slot.
                        const BoneSet& want = pieceSets[m][q];
                        bool taken[PALETTE_BONES] = {};
                        int slotOf[256];
                        for (int b = 0; b < 256; b++)
                        {
                            slotOf[b] = -1;
                        }
                        for (int sl = 0; sl < prevSlots; sl++)
                        {
                            if (want.has(prev[sl]))
                            {
                                slotOf[prev[sl]] = sl;
                                taken[sl] = true;
                            }
                        }
                        int next = 0;
                        int top = 0;
                        for (int b = 0; b < 256; b++)
                        {
                            if (!want.has((uint32)b))
                            {
                                continue;
                            }
                            if (slotOf[b] < 0)
                            {
                                while (taken[next])
                                {
                                    next++;
                                }
                                slotOf[b] = next;
                                taken[next] = true;
                            }
                            if (slotOf[b] + 1 > top)
                            {
                                top = slotOf[b] + 1;
                            }
                        }
                        p->numSlots = (uint8)top;
                        for (int sl = 0; sl < top; sl++)
                        {
                            // A slot nothing uses keeps what it held: not sent.
                            p->bones[sl] = sl < prevSlots ? prev[sl] : 0;
                        }
                        for (int b = 0; b < 256; b++)
                        {
                            if (slotOf[b] >= 0)
                            {
                                p->bones[slotOf[b]] = (uint8)b;
                            }
                        }
                        memcpy(prev, p->bones, top);
                        prevSlots = top > prevSlots ? top : prevSlots;

                        p->firstVert = (uint32)vbase;
                        local.assign(nv, -1);
                        for (size_t i = 0; i < tris[m].size(); i++)
                        {
                            if (triPiece[m][i] != (int)q)
                            {
                                continue;
                            }
                            for (int k = 0; k < 3; k++)
                            {
                                const int32 v = tris[m][i].v[k];
                                if (local[v] < 0)
                                {
                                    local[v] = vbase++;
                                    uint8 slots[4];
                                    for (int j = 0; j < 4; j++)
                                    {
                                        slots[j] = vw[v][j] != 0 ? (uint8)slotOf[vb[v][j]] : 0;
                                    }
                                    writeVertex(geo, format, stride,
                                                h->vertexBuffer + (size_t)local[v] * stride, v,
                                                slots, vw[v]);
                                }
                                h->indexBuffer[offset++] = (uint16)local[v];
                            }
                        }
                        p->numIndex = offset - p->offset;
                        p->numVerts = (uint32)vbase - p->firstVert;
                    }
                }
            }

            // The meshes, as the other pipelines keep them (vertex alpha, the
            // material).
            for (uint32 m = 0; m < nm; m++)
            {
                InstanceData* inst = &h->inst[m];
                memset(inst, 0, sizeof(*inst));
                inst->material = meshes[m].material;
                inst->numIndex = meshes[m].numIndices;
                if ((format & MODEL_COLOR) && meshes[m].indices != nil)
                {
                    uint8 a = 0xFF;
                    for (uint32 i = 0; i < meshes[m].numIndices; i++)
                    {
                        a &= geo->colors[meshes[m].indices[i]].alpha;
                    }
                    inst->vertexAlpha = a != 0xFF;
                }
            }

            GSPGPU_FlushDataCache(h->vertexBuffer, (u32)outVerts * stride);
            GSPGPU_FlushDataCache(h->indexBuffer, (u32)outIndex * sizeof(uint16));

            sTotals.geometries++;
            sTotals.split += whole ? 0 : 1;
            sTotals.pieces20 += pieces20;
            sTotals.pieces10 += pieces10;
            sTotals.verts += nv;
            sTotals.verts20 += verts20;
            sTotals.verts10 += verts10;
            sTotals.noWeights += noWeights;
            if (numUsed > sTotals.maxBones)
            {
                sTotals.maxBones = numUsed;
            }
            if (statsOn() && (!whole || noWeights > 0))
            {
                printf("tssm: c3d: skin: %d vertices, %u meshes, %d bones used: %u pieces "
                       "(%d vertices; at 10 bones %d pieces, %d vertices)%s; totals: %d "
                       "geometries, %d split, %d pieces at 20 bones and %d at 10, %d vertices "
                       "become %d at 20 and %d at 10, %d without weights\n",
                       (int)nv, (unsigned)nm, numUsed, (unsigned)numPieces, (int)outVerts, pieces10,
                       verts10, whole ? " (not split)" : "", sTotals.geometries, sTotals.split,
                       sTotals.pieces20, sTotals.pieces10, sTotals.verts, sTotals.verts20,
                       sTotals.verts10, sTotals.noWeights);
                fflush(stdout);
            }
            return h;
        }

        // The bone matrices in the atomic's space, as GL3's
        // uploadSkinMatrices builds them; the bind pose without a hierarchy.
        void skinBegin(Atomic* atomic, bool32 bindPose)
        {
            Geometry* geo = atomic->geometry;
            Skin* skin = Skin::get(geo);
            HAnimHierarchy* hier = Skin::getHierarchy(atomic);
            const int32 numBones = skin != nil ? skin->numBones : 0;
            if (numBones > sBonesCap)
            {
                rwFree(sBones);
                sBonesCap = numBones < 64 ? 64 : numBones;
                sBones = rwNewT(Matrix, sBonesCap, MEMDUR_GLOBAL);
            }
            sNumBones = numBones;
            sBonesIdentity = bindPose || hier == nil || skin->inverseMatrices == nil ||
                             hier->numNodes < numBones;
            if (sBonesIdentity)
            {
                return;
            }
            Matrix* invMats = (Matrix*)skin->inverseMatrices;
            if (hier->flags & HAnimHierarchy::LOCALSPACEMATRICES)
            {
                for (int32 i = 0; i < numBones; i++)
                {
                    invMats[i].flags = 0;
                    Matrix::mult(&sBones[i], &invMats[i], &hier->matrices[i]);
                }
            }
            else
            {
                Matrix invAtm;
                Matrix tmp;
                Matrix::invert(&invAtm, atomic->getFrame()->getLTM());
                for (int32 i = 0; i < numBones; i++)
                {
                    invMats[i].flags = 0;
                    Matrix::mult(&tmp, &hier->matrices[i], &invAtm);
                    Matrix::mult(&sBones[i], &invMats[i], &tmp);
                }
            }
        }

        int32 skinPalette(const SkinPiece* p)
        {
            int32 sent = 0;
            for (int32 sl = 0; sl < p->numSlots; sl++)
            {
                const int32 b = p->bones[sl];
                float rows[12];
                if (sBonesIdentity || b >= sNumBones)
                {
                    static const float kId[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
                    memcpy(rows, kId, sizeof(rows));
                }
                else
                {
                    const Matrix& m = sBones[b];
                    rows[0] = m.right.x;
                    rows[1] = m.up.x;
                    rows[2] = m.at.x;
                    rows[3] = m.pos.x;
                    rows[4] = m.right.y;
                    rows[5] = m.up.y;
                    rows[6] = m.at.y;
                    rows[7] = m.pos.y;
                    rows[8] = m.right.z;
                    rows[9] = m.up.z;
                    rows[10] = m.at.z;
                    rows[11] = m.pos.z;
                }
                sent += setUniformRows(REG_BONES + 3 * sl, rows, 3);
            }
            return sent;
        }

        void skinCpu(const InstanceDataHeader* h, uint8* dst)
        {
            // vs_skin's arithmetic: the weight bytes times its INV255, the
            // four rows blended first, the normal bytes over 127.
            const float kInv255 = 0.003921628f;
            const uint32 inStride = h->stride;
            const uint32 outStride = modelStride(h->format & ~MODEL_SKIN);
            const bool normals = (h->format & MODEL_NORMAL) != 0;
            const uint32 skinOff = inStride - 8;
            float rows[PALETTE_BONES][12];
            for (uint32 i = 0; i < h->numPieces; i++)
            {
                const SkinPiece* p = &h->pieces[i];
                if (p->numVerts == 0)
                {
                    continue;
                }
                for (int32 sl = 0; sl < p->numSlots; sl++)
                {
                    const int32 b = p->bones[sl];
                    float* r = rows[sl];
                    if (sBonesIdentity || b >= sNumBones)
                    {
                        static const float kId[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
                        memcpy(r, kId, sizeof(kId));
                        continue;
                    }
                    const Matrix& m = sBones[b];
                    r[0] = m.right.x;
                    r[1] = m.up.x;
                    r[2] = m.at.x;
                    r[3] = m.pos.x;
                    r[4] = m.right.y;
                    r[5] = m.up.y;
                    r[6] = m.at.y;
                    r[7] = m.pos.y;
                    r[8] = m.right.z;
                    r[9] = m.up.z;
                    r[10] = m.at.z;
                    r[11] = m.pos.z;
                }
                const uint8* src = h->vertexBuffer + (size_t)p->firstVert * inStride;
                uint8* out = dst + (size_t)p->firstVert * outStride;
                for (uint32 v = 0; v < p->numVerts; v++, src += inStride, out += outStride)
                {
                    const uint8* sk = src + skinOff;
                    float mx[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
                    for (int k = 0; k < 4; k++)
                    {
                        const uint8 wb = sk[4 + k];
                        const uint32 sl = sk[k] / 3u;
                        if (wb == 0 || sl >= p->numSlots)
                        {
                            continue;
                        }
                        const float w = wb * kInv255;
                        const float* r = rows[sl];
                        for (int j = 0; j < 12; j++)
                        {
                            mx[j] += w * r[j];
                        }
                    }
                    memcpy(out, src, outStride);
                    const float* pp = (const float*)src;
                    float* op = (float*)out;
                    op[0] = mx[0] * pp[0] + mx[1] * pp[1] + mx[2] * pp[2] + mx[3];
                    op[1] = mx[4] * pp[0] + mx[5] * pp[1] + mx[6] * pp[2] + mx[7];
                    op[2] = mx[8] * pp[0] + mx[9] * pp[1] + mx[10] * pp[2] + mx[11];
                    if (normals)
                    {
                        // Bytes again, for vs_lit's 1/127.
                        const int8* n = (const int8*)(src + 12);
                        int8* q = (int8*)(out + 12);
                        for (int a = 0; a < 3; a++)
                        {
                            const float* r = &mx[4 * a];
                            float f = r[0] * n[0] + r[1] * n[1] + r[2] * n[2];
                            f = f > 127.0f ? 127.0f : f < -127.0f ? -127.0f : f;
                            q[a] = (int8)(f < 0.0f ? f - 0.5f : f + 0.5f);
                        }
                    }
                }
            }
        }
    } // namespace c3d
} // namespace rw
