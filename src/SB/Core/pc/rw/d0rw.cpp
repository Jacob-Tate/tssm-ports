// The librw half of iD0Stats (the 3DS study's measurements; iD0Stats.h): what
// each frame asks the renderer to draw, what the skinned models would need from
// a 10-bone split, and what a 3DS build would do to each texture at load.
//
// Apart from iD0Stats.cpp because it needs librw's own headers, which do not
// mix with the game's.

#include <rwcore.h>
#include <rpworld.h>

#include "rw.h"

#include "iD0Stats.h"
#include "iD0StatsRw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <vector>

namespace
{
    // ---------------------------------------------------------------------
    // Draw counts: every object pipeline's render is wrapped once.

    typedef void (*RenderFn)(rw::ObjPipeline* pipe, rw::Atomic* atomic);

    struct Wrap
    {
        rw::ObjPipeline* pipe;
        RenderFn orig;
    };

    const int kMaxWraps = 16;
    Wrap sWraps[kMaxWraps];
    int sWrapCount;

    iD0RwFrame sFrame;

    void CountAtomic(rw::Atomic* atomic)
    {
        rw::Geometry* geo = atomic->geometry;
        if (geo == NULL)
        {
            return;
        }

        sFrame.atomics++;
        if (geo->meshHeader != NULL)
        {
            sFrame.meshes += geo->meshHeader->numMeshes;
            sFrame.tris += (S32)geo->meshHeader->guessNumTriangles();
        }

        rw::Skin* skin = rw::Skin::get(geo);
        if (skin != NULL)
        {
            sFrame.skinAtomics++;
            sFrame.skinVerts += geo->numVertices;
            if (skin->numBones > sFrame.maxBones)
            {
                sFrame.maxBones = skin->numBones;
            }
        }

        if (geo->flags & rw::Geometry::LIGHT)
        {
            sFrame.litAtomics++;

            rw::World* world = rw::engine != NULL ? rw::engine->currentWorld : NULL;
            if (world != NULL)
            {
                rw::Light* dirs[32];
                rw::Light* locals[32];
                rw::WorldLights lights;
                lights.numDirectionals = 32;
                lights.directionals = dirs;
                lights.numLocals = 32;
                lights.locals = locals;
                world->enumerateLights(atomic, &lights);

                if (lights.numDirectionals > sFrame.maxDirLights)
                {
                    sFrame.maxDirLights = lights.numDirectionals;
                }
                if (lights.numLocals > sFrame.maxLocalLights)
                {
                    sFrame.maxLocalLights = lights.numLocals;
                }
                if (lights.numAmbients > sFrame.maxAmbients)
                {
                    sFrame.maxAmbients = lights.numAmbients;
                }
                sFrame.dirLightSum += lights.numDirectionals;
                sFrame.localLightSum += lights.numLocals;
            }
        }
    }

    // TSSM_LIGHTLOG: the frames whose lit atomics are logged with the lights
    // each one gets, as both backends' lighting reads them
    // (World::enumerateLights, at most 8 of each kind), so the 3DS and the PC
    // can be compared line for line.
    const int kMaxLightFrames = 16;
    S32 sLightFrames[kMaxLightFrames];
    int sLightFrameCount;
    bool sLightParsed;
    bool sLightOn;
    S32 sLightFrame;
    int sLightIndex;

    void LogLights(rw::Atomic* atomic)
    {
        rw::Geometry* geo = atomic->geometry;
        rw::World* world = rw::engine != NULL ? rw::engine->currentWorld : NULL;
        if (geo == NULL || world == NULL || !(geo->flags & rw::Geometry::LIGHT))
        {
            return;
        }

        rw::Light* dirs[8];
        rw::Light* locals[8];
        rw::WorldLights lights;
        lights.numDirectionals = 8;
        lights.directionals = dirs;
        lights.numLocals = 8;
        lights.locals = locals;
        world->enumerateLights(atomic, &lights);

        const rw::V3d& pos = atomic->getFrame()->getLTM()->pos;
        char line[512];
        int o = snprintf(line, sizeof(line),
                         "tssm: lights: frame %d #%d: %d vertices at (%.1f %.1f %.1f), ambient "
                         "%.3f %.3f %.3f from %d, %d directional",
                         (int)sLightFrame, sLightIndex, (int)geo->numVertices, (double)pos.x,
                         (double)pos.y, (double)pos.z, (double)lights.ambient.red,
                         (double)lights.ambient.green, (double)lights.ambient.blue,
                         (int)lights.numAmbients, (int)lights.numDirectionals);
        for (int i = 0; i < lights.numDirectionals && o < (int)sizeof(line) - 40; i++)
        {
            const rw::RGBAf& c = lights.directionals[i]->color;
            o += snprintf(line + o, sizeof(line) - (size_t)o, " (%.3f %.3f %.3f)", (double)c.red,
                          (double)c.green, (double)c.blue);
        }
        snprintf(line + o, sizeof(line) - (size_t)o, ", %d local\n", (int)lights.numLocals);
        fputs(line, stdout);
        sLightIndex++;
    }

    void CountingRender(rw::ObjPipeline* pipe, rw::Atomic* atomic)
    {
        if (iD0Enabled())
        {
            CountAtomic(atomic);
        }
        if (sLightOn)
        {
            LogLights(atomic);
        }

        for (int i = 0; i < sWrapCount; i++)
        {
            if (sWraps[i].pipe == pipe)
            {
                sWraps[i].orig(pipe, atomic);
                return;
            }
        }
    }

    void WrapPipe(rw::ObjPipeline* pipe)
    {
        if (pipe == NULL || sWrapCount >= kMaxWraps)
        {
            return;
        }

        // Once per pipeline, whatever its render is now: iFX.cpp wraps the
        // UV-transform pipeline's render with its own and calls whatever it
        // found there, so wrapping that pipeline again would count it twice.
        for (int i = 0; i < sWrapCount; i++)
        {
            if (sWraps[i].pipe == pipe)
            {
                return;
            }
        }

        sWraps[sWrapCount].pipe = pipe;
        sWraps[sWrapCount].orig = pipe->impl.render;
        sWrapCount++;
        pipe->impl.render = CountingRender;
    }

    // ---------------------------------------------------------------------
    // The 3DS texture path, simulated (TSSM_D0_TEX3DS / TSSM_D0_PICA).

    int sTexMode = -1; // 0 off, 1 time it, 2 time it and keep the result

    int TexMode()
    {
        if (sTexMode < 0)
        {
            const char* pica = getenv("TSSM_D0_PICA");
            const char* tex = getenv("TSSM_D0_TEX3DS");
            sTexMode = (pica != NULL && atoi(pica) > 0) ? 2 :
                       (tex != NULL && atoi(tex) > 0)   ? 1 :
                                                          0;
        }
        return sTexMode;
    }

    enum PicaFormat
    {
        kRGB565,
        kRGBA5551,
        kRGBA4
    };

    inline U16 Pack(const U8* p, PicaFormat fmt)
    {
        switch (fmt)
        {
        case kRGB565:
            return (U16)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
        case kRGBA5551:
            return (U16)(((p[0] >> 3) << 11) | ((p[1] >> 3) << 6) | ((p[2] >> 3) << 1) |
                         (p[3] >= 128 ? 1 : 0));
        default:
            return (U16)(((p[0] >> 4) << 12) | ((p[1] >> 4) << 8) | ((p[2] >> 4) << 4) |
                         (p[3] >> 4));
        }
    }

    inline void Unpack(U16 v, PicaFormat fmt, U8* out)
    {
        switch (fmt)
        {
        case kRGB565:
            out[0] = (U8)(((v >> 11) & 31) * 255 / 31);
            out[1] = (U8)(((v >> 5) & 63) * 255 / 63);
            out[2] = (U8)((v & 31) * 255 / 31);
            out[3] = 255;
            break;
        case kRGBA5551:
            out[0] = (U8)(((v >> 11) & 31) * 255 / 31);
            out[1] = (U8)(((v >> 6) & 31) * 255 / 31);
            out[2] = (U8)(((v >> 1) & 31) * 255 / 31);
            out[3] = (v & 1) ? 255 : 0;
            break;
        default:
            out[0] = (U8)(((v >> 12) & 15) * 17);
            out[1] = (U8)(((v >> 8) & 15) * 17);
            out[2] = (U8)(((v >> 4) & 15) * 17);
            out[3] = (U8)((v & 15) * 17);
            break;
        }
    }

    // The PICA200's texel order: 8x8 tiles, rows of tiles from the bottom of
    // the picture up, texels inside a tile in Morton (Z) order.
    inline U32 TiledIndex(U32 x, U32 y, U32 w, U32 h)
    {
        const U32 fy = h - 1 - y;
        const U32 tile = (fy >> 3) * (w >> 3) + (x >> 3);
        const U32 tx = x & 7;
        const U32 ty = fy & 7;
        const U32 morton = (tx & 1) | ((ty & 1) << 1) | ((tx & 2) << 1) | ((ty & 2) << 2) |
                           ((tx & 4) << 2) | ((ty & 4) << 3);
        return tile * 64 + morton;
    }

    // A box-filtered copy at half size, or the same size where a side is
    // already at the 8-texel minimum.
    U8* Shrink(const U8* src, int w, int h, int stride, int* ow, int* oh)
    {
        const int nw = w > 8 ? w / 2 : w;
        const int nh = h > 8 ? h / 2 : h;
        const int sx = w / nw;
        const int sy = h / nh;
        U8* dst = (U8*)malloc((size_t)nw * nh * 4);

        for (int y = 0; y < nh; y++)
        {
            for (int x = 0; x < nw; x++)
            {
                U32 acc[4] = { 0, 0, 0, 0 };
                for (int j = 0; j < sy; j++)
                {
                    const U8* row = src + (size_t)(y * sy + j) * stride;
                    for (int i = 0; i < sx; i++)
                    {
                        const U8* p = row + (x * sx + i) * 4;
                        acc[0] += p[0];
                        acc[1] += p[1];
                        acc[2] += p[2];
                        acc[3] += p[3];
                    }
                }
                const U32 n = (U32)(sx * sy);
                U8* d = dst + ((size_t)y * nw + x) * 4;
                d[0] = (U8)(acc[0] / n);
                d[1] = (U8)(acc[1] / n);
                d[2] = (U8)(acc[2] / n);
                d[3] = (U8)(acc[3] / n);
            }
        }

        *ow = nw;
        *oh = nh;
        return dst;
    }

    U32 sTexFailed;
    U32 sTexCount;

    // ---------------------------------------------------------------------
    // Skinned geometry: the 10-bone split's inputs.

    struct SkinTotals
    {
        U32 geometries;
        U32 noWeights;
        U32 tris;
        U32 trisOver10;
        U32 trisOver8;
        U32 trisOver4;
        S32 maxBonesPerTri;
        S32 maxBonesPerVertex;
        S32 maxBones;
        U32 piecesTotal;
        U32 meshesTotal;
        U32 vertsTotal;
        U32 dupVertsTotal;
        U32 hist[13];
    } sSkin;

    // Bones with a non-zero weight on vertex v, as a count, into `out`.
    int VertexBones(rw::Skin* skin, int v, U8* out)
    {
        int n = 0;
        for (int k = 0; k < 4; k++)
        {
            if (skin->weights[v * 4 + k] > 0.0f)
            {
                out[n++] = skin->indices[v * 4 + k];
            }
        }
        return n;
    }

    struct BoneSet
    {
        U32 bits[8];
        int count;

        void clear()
        {
            memset(bits, 0, sizeof(bits));
            count = 0;
        }
        bool has(U8 b) const
        {
            return (bits[b >> 5] >> (b & 31)) & 1;
        }
        void add(U8 b)
        {
            if (!has(b))
            {
                bits[b >> 5] |= 1u << (b & 31);
                count++;
            }
        }
    };
} // namespace

void iD0RwWrapPipelines()
{
    if (rw::engine == NULL)
    {
        return;
    }

    rw::Driver* driver = rw::engine->driver[rw::platform];
    if (driver != NULL)
    {
        WrapPipe(driver->defaultPipeline);
    }
    WrapPipe(rw::skinGlobals.pipelines[rw::platform]);
    WrapPipe(rw::skinGlobals.matfxPipelines[rw::platform]);
    WrapPipe(rw::matFXGlobals.pipelines[rw::platform]);
    WrapPipe(rw::GetUVTransformPipeline());
}

void iD0RwLightLogFrame(S32 frame)
{
    if (!sLightParsed)
    {
        sLightParsed = true;
        const char* e = getenv("TSSM_LIGHTLOG");
        while (e != NULL && *e != '\0' && sLightFrameCount < kMaxLightFrames)
        {
            const S32 f = (S32)atoi(e);
            if (f > 0)
            {
                sLightFrames[sLightFrameCount++] = f;
            }
            e = strchr(e, ',');
            e = e != NULL ? e + 1 : NULL;
        }
        if (sLightFrameCount > 0)
        {
            printf("tssm: lights: %d frame(s) to log (TSSM_LIGHTLOG)\n", sLightFrameCount);
        }
    }
    if (sLightFrameCount == 0)
    {
        return;
    }

    if (sLightOn)
    {
        printf("tssm: lights: frame %d: %d lit atomics\n", (int)sLightFrame, sLightIndex);
        fflush(stdout);
        sLightOn = false;
    }

    for (int i = 0; i < sLightFrameCount; i++)
    {
        if (sLightFrames[i] == frame + 1)
        {
            iD0RwWrapPipelines();
            sLightOn = true;
            sLightFrame = frame + 1;
            sLightIndex = 0;
        }
    }
}

void iD0RwTakeFrame(iD0RwFrame* out)
{
    *out = sFrame;
    memset(&sFrame, 0, sizeof(sFrame));
}

void iD0SkinGeometry(RpAtomic* rpAtomic)
{
    if (!iD0Enabled() || rpAtomic == NULL)
    {
        return;
    }

    rw::Atomic* atomic = reinterpret_cast<rw::Atomic*>(rpAtomic);
    rw::Geometry* geo = atomic->geometry;
    if (geo == NULL)
    {
        return;
    }
    rw::Skin* skin = rw::Skin::get(geo);
    if (skin == NULL)
    {
        return;
    }

    struct Timed
    {
        S64 start = iD0Now();
        ~Timed()
        {
            iD0Add(iD0_OVERHEAD, start, 0);
        }
    } timed;

    FILE* f = iD0File();
    sSkin.geometries++;
    if (skin->numBones > sSkin.maxBones)
    {
        sSkin.maxBones = skin->numBones;
    }

    if (skin->indices == NULL || skin->weights == NULL || geo->triangles == NULL)
    {
        sSkin.noWeights++;
        fprintf(f, "#skin\t%s\tnoweights\tbones=%d\tverts=%d\ttris=%d\n", iD0SceneName(),
                (int)skin->numBones, (int)geo->numVertices, (int)geo->numTriangles);
        return;
    }

    U8 vb[3][4];
    int vn[3];
    int maxTri = 0;
    int maxVert = 0;
    int hist[13] = {};

    for (int v = 0; v < geo->numVertices; v++)
    {
        U8 tmp[4];
        int n = VertexBones(skin, v, tmp);
        if (n > maxVert)
        {
            maxVert = n;
        }
    }

    // The split, estimated: per material, the distinct bone sets of its
    // triangles packed first-fit-decreasing into pieces of at most 10 bones,
    // each set into the piece it grows least. RtSkinSplit is not known to do
    // better or worse; the piece count is what a 10-matrix palette costs in
    // draw calls, and a vertex used by two pieces is stored twice.
    const int nmat = geo->matList.numMaterials > 0 ? geo->matList.numMaterials : 1;
    U32 pieces = 0;
    std::vector<U64> vertPiece;
    std::vector<int> triSet((size_t)geo->numTriangles, -1);

    for (int m = 0; m < nmat; m++)
    {
        std::vector<BoneSet> uniq;
        for (int t = 0; t < geo->numTriangles; t++)
        {
            const rw::Triangle& tri = geo->triangles[t];
            if ((int)tri.matId != m && nmat > 1)
            {
                continue;
            }

            BoneSet set;
            set.clear();
            for (int c = 0; c < 3; c++)
            {
                vn[c] = VertexBones(skin, tri.v[c], vb[c]);
                for (int k = 0; k < vn[c]; k++)
                {
                    set.add(vb[c][k]);
                }
            }
            if (set.count > maxTri)
            {
                maxTri = set.count;
            }
            hist[set.count > 12 ? 12 : set.count]++;

            int found = -1;
            for (size_t u = 0; u < uniq.size(); u++)
            {
                if (memcmp(uniq[u].bits, set.bits, sizeof(set.bits)) == 0)
                {
                    found = (int)u;
                    break;
                }
            }
            if (found < 0)
            {
                found = (int)uniq.size();
                uniq.push_back(set);
            }
            triSet[t] = found;
        }

        std::vector<int> order(uniq.size());
        for (size_t u = 0; u < uniq.size(); u++)
        {
            order[u] = (int)u;
        }
        std::sort(order.begin(), order.end(),
                  [&](int x, int y) { return uniq[x].count > uniq[y].count; });

        std::vector<BoneSet> bins;
        std::vector<int> setBin(uniq.size(), -1);
        for (int u : order)
        {
            int best = -1;
            int bestGrow = 99;
            for (size_t bi = 0; bi < bins.size(); bi++)
            {
                int unionCount = 0;
                for (int w = 0; w < 8; w++)
                {
                    unionCount += __builtin_popcount(bins[bi].bits[w] | uniq[u].bits[w]);
                }
                const int grow = unionCount - bins[bi].count;
                if (unionCount <= 10 && grow < bestGrow)
                {
                    best = (int)bi;
                    bestGrow = grow;
                }
            }
            if (best < 0)
            {
                best = (int)bins.size();
                BoneSet empty;
                empty.clear();
                bins.push_back(empty);
            }
            for (int w = 0; w < 8; w++)
            {
                bins[best].bits[w] |= uniq[u].bits[w];
            }
            bins[best].count = 0;
            for (int w = 0; w < 8; w++)
            {
                bins[best].count += __builtin_popcount(bins[best].bits[w]);
            }
            setBin[u] = best;
        }

        for (int t = 0; t < geo->numTriangles; t++)
        {
            const rw::Triangle& tri = geo->triangles[t];
            if (triSet[t] < 0 || ((int)tri.matId != m && nmat > 1))
            {
                continue;
            }
            const U64 piece = pieces + (U64)setBin[triSet[t]];
            for (int c = 0; c < 3; c++)
            {
                vertPiece.push_back((piece << 32) | tri.v[c]);
            }
        }
        pieces += (U32)bins.size();
    }

    std::sort(vertPiece.begin(), vertPiece.end());
    vertPiece.erase(std::unique(vertPiece.begin(), vertPiece.end()), vertPiece.end());
    const U32 pieceVerts = (U32)vertPiece.size();
    std::vector<U32> verts;
    verts.reserve(vertPiece.size());
    for (U64 vp : vertPiece)
    {
        verts.push_back((U32)vp);
    }
    std::sort(verts.begin(), verts.end());
    const U32 usedVerts = (U32)(std::unique(verts.begin(), verts.end()) - verts.begin());

    const U32 over10 = (U32)(hist[11] + hist[12]);
    sSkin.tris += (U32)geo->numTriangles;
    sSkin.trisOver10 += over10;
    sSkin.trisOver8 += (U32)(hist[9] + hist[10] + hist[11] + hist[12]);
    for (int i = 5; i <= 12; i++)
    {
        sSkin.trisOver4 += (U32)hist[i];
    }
    for (int i = 0; i <= 12; i++)
    {
        sSkin.hist[i] += (U32)hist[i];
    }
    if (maxTri > sSkin.maxBonesPerTri)
    {
        sSkin.maxBonesPerTri = maxTri;
    }
    if (maxVert > sSkin.maxBonesPerVertex)
    {
        sSkin.maxBonesPerVertex = maxVert;
    }
    sSkin.piecesTotal += pieces;
    sSkin.meshesTotal += (U32)(geo->meshHeader != NULL ? geo->meshHeader->numMeshes : 0);
    sSkin.vertsTotal += usedVerts;
    sSkin.dupVertsTotal += pieceVerts - usedVerts;

    fprintf(f,
            "#skin\t%s\tbones=%d\tused=%d\tverts=%d\ttris=%d\tmeshes=%d\tmaxPerVertex=%d\t"
            "maxPerTri=%d\ttrisOver10=%u\tsplitPieces=%u\tdupVerts=%u\n",
            iD0SceneName(), (int)skin->numBones, (int)skin->numUsedBones, (int)geo->numVertices,
            (int)geo->numTriangles, (int)(geo->meshHeader != NULL ? geo->meshHeader->numMeshes : 0),
            maxVert, maxTri, over10, pieces, pieceVerts - usedVerts);
}

void iD0RwReport(FILE* f)
{
    fprintf(f,
            "#skintotal\tgeometries=%u\tnoweights=%u\ttris=%u\ttrisOver10=%u\ttrisOver8=%u\t"
            "trisOver4=%u\tmaxBonesPerTri=%d\tmaxBonesPerVertex=%d\tmaxBones=%d\t"
            "splitPieces=%u\tmeshes=%u\tverts=%u\tdupVerts=%u\thist=",
            sSkin.geometries, sSkin.noWeights, sSkin.tris, sSkin.trisOver10, sSkin.trisOver8,
            sSkin.trisOver4, sSkin.maxBonesPerTri, sSkin.maxBonesPerVertex, sSkin.maxBones,
            sSkin.piecesTotal, sSkin.meshesTotal, sSkin.vertsTotal, sSkin.dupVertsTotal);
    for (int i = 0; i <= 12; i++)
    {
        fprintf(f, "%s%u", i ? "," : "", sSkin.hist[i]);
    }
    fprintf(f, "\n#textotal\tconverted=%u\tfailed=%u\n", sTexCount, sTexFailed);
}

void iD0AfterConvert(rw::Atomic* atomic)
{
    if (!iD0Enabled() || atomic == NULL)
    {
        return;
    }

    static int sInstance = -1;
    if (sInstance < 0)
    {
        const char* env = getenv("TSSM_D0_INSTANCE");
        sInstance = (env != NULL && atoi(env) > 0) ? 1 : 0;
    }
    if (!sInstance)
    {
        return;
    }

    const S64 start = iD0Now();
    atomic->instance();
    iD0Add(iD0_GEOM_INSTANCE, start, 0);
}

S32 iD0Texture(rw::Texture* texture, S32 opaque)
{
    if (!iD0Enabled() || TexMode() == 0 || texture == NULL || texture->raster == NULL)
    {
        return FALSE;
    }

    const S64 start = iD0Now();
    rw::Raster* raster = texture->raster;
    rw::Image* img = raster->toImage();
    if (img == NULL)
    {
        sTexFailed++;
        return FALSE;
    }
    if (img->depth != 32)
    {
        img->convertTo32();
    }
    if (img->depth != 32 || img->width < 1 || img->height < 1)
    {
        sTexFailed++;
        img->destroy();
        return FALSE;
    }

    const int w = img->width;
    const int h = img->height;

    // The format a converter would pick from the pixels: no alpha 565, on/off
    // alpha 5551, anything else 4444 (what DXT3 and DXT5 turn into).
    bool hasAlpha = false;
    bool binary = true;
    if (!opaque)
    {
        for (int y = 0; y < h; y++)
        {
            const U8* row = img->pixels + (size_t)y * img->stride;
            for (int x = 0; x < w; x++)
            {
                const U8 a = row[x * 4 + 3];
                if (a != 255)
                {
                    hasAlpha = true;
                    if (a != 0)
                    {
                        binary = false;
                    }
                }
            }
        }
    }
    else
    {
        for (int y = 0; y < h; y++)
        {
            U8* row = img->pixels + (size_t)y * img->stride;
            for (int x = 0; x < w; x++)
            {
                row[x * 4 + 3] = 255;
            }
        }
    }
    const PicaFormat fmt = !hasAlpha ? kRGB565 : binary ? kRGBA5551 : kRGBA4;

    // Half size, then the mip chain down to 8x8, each level packed and tiled.
    int lw;
    int lh;
    U8* level = Shrink(img->pixels, w, h, img->stride, &lw, &lh);
    U32 bytes = 0;
    int levels = 0;
    const int topW = lw;
    const int topH = lh;
    U8* top = NULL;

    for (;;)
    {
        const int tw = lw < 8 ? 8 : lw;
        const int th = lh < 8 ? 8 : lh;
        U16* tiled = (U16*)calloc((size_t)tw * th, sizeof(U16));
        for (int y = 0; y < lh; y++)
        {
            for (int x = 0; x < lw; x++)
            {
                tiled[TiledIndex((U32)x, (U32)y, (U32)tw, (U32)th)] =
                    Pack(level + ((size_t)y * lw + x) * 4, fmt);
            }
        }
        bytes += (U32)(tw * th * 2);
        levels++;

        if (levels == 1 && TexMode() == 2)
        {
            // Keep the top level as the 3DS would show it.
            top = (U8*)malloc((size_t)lw * lh * 4);
            for (int y = 0; y < lh; y++)
            {
                for (int x = 0; x < lw; x++)
                {
                    Unpack(tiled[TiledIndex((U32)x, (U32)y, (U32)tw, (U32)th)], fmt,
                           top + ((size_t)y * lw + x) * 4);
                }
            }
        }
        free(tiled);

        if (lw <= 8 && lh <= 8)
        {
            break;
        }
        int nw;
        int nh;
        U8* next = Shrink(level, lw, lh, lw * 4, &nw, &nh);
        free(level);
        level = next;
        lw = nw;
        lh = nh;
    }
    free(level);
    img->destroy();
    sTexCount++;

    iD0AddTex3DS(start, bytes, (S32)fmt);

    if (top == NULL)
    {
        return FALSE;
    }

    rw::Image* shown = rw::Image::create(topW, topH, 32);
    shown->allocate();
    for (int y = 0; y < topH; y++)
    {
        memcpy(shown->pixels + (size_t)y * shown->stride, top + (size_t)y * topW * 4,
               (size_t)topW * 4);
    }
    free(top);

    rw::Raster* replaced = rw::Raster::createFromImage(shown, rw::platform);
    shown->destroy();
    if (replaced == NULL)
    {
        sTexFailed++;
        return FALSE;
    }

    raster->destroy();
    texture->raster = replaced;
    return TRUE;
}
