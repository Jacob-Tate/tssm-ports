// tssm_gcrwtest: the GameCube RenderWare data -- native textures, native
// geometry and skins -- read with the port's librw (tools/librw/gc_*.patch)
// over every package of a GameCube disc, without the game.
//
//     tssm_gcrwtest <GameCube files> [<Xbox files>] [options]
//
//       --fuzz N      after each texture and geometry, N damaged copies of it
//       --seed S      the damage's random seed
//       --only tex|geo  one half
//       --pkg TEXT    only packages whose path contains TEXT
//       --show N      list up to N examples of each kind of difference (8)
//
// Textures (RWTX, TEXS): every stored level decoded (readGCTextureLevels), the
// level sizes checked against the size the struct gives, and the name the
// port writes back (iGCPackageRestoreTextureNames). With an Xbox folder each
// is compared with the Xbox texture of the same id: size, levels, whether
// level 0 is opaque, cut out or blended, the restored name against the Xbox
// name, and the PSNR of level 0 at the smaller of the two sizes.
//
// Geometry (MODL, and the world clumps in JSP): each clump read with
// Clump::streamRead, which de-instances native geometry at read. Every
// geometry must come out ordinary (not native) and not as the stub a failed
// read leaves. With an Xbox folder each atomic is compared with the Xbox
// atomic of the same place -- for JSP, the atomics of all the package's world
// clumps in order, because the GameCube cuts them into more clumps -- by its
// triangles as position triples with their winding, its BinMesh index counts,
// and per vertex its normal, prelight, texcoords and skin weights.
//
// Every skinned clump is read once more with each native geometry's GameCube
// skin replaced by a generic skin placed before its native data: those
// geometries must come out as the stub, with their skin laid out for it.
//
// With --fuzz every texture struct is cut short and damaged N times and
// decoded in a copy that ends at an inaccessible page; every clump with
// native data has the bytes inside its native and skin structs damaged N
// times and is read again. Run it under AddressSanitizer as well
// (tools/gcswap/README.md).
//
// Exit code 0 when every GameCube texture decodes, every geometry de-instances
// and, with an Xbox folder, no texture or geometry differs in a way the two
// releases' content does not explain (the report lists those).

// librw first: the decomp's types.h defines `null`, which librw uses as a
// namespace.
#include <rw.h>

#include "iGCPackage.h"
#include "iModPackage.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace
{
    const U32 kRWTX = 0x52575458;
    const U32 kTEXS = 0x54455853;
    const U32 kMODL = 0x4D4F444C;
    const U32 kJSP = 0x4A535020;

    U32 RdLE(const U8* p)
    {
        return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
    }

    U32 RdBE(const U8* p)
    {
        return ((U32)p[0] << 24) | ((U32)p[1] << 16) | ((U32)p[2] << 8) | (U32)p[3];
    }

    U64 sRandom = 0x9E3779B97F4A7C15ull;
    U32 Rand()
    {
        sRandom ^= sRandom << 13;
        sRandom ^= sRandom >> 7;
        sRandom ^= sRandom << 17;
        return (U32)(sRandom >> 16);
    }

    // --- packages ---------------------------------------------------------------------

    struct Package
    {
        std::vector<U8> bytes;
        iModPkgToc toc;
        std::map<U32, S32> byId;
    };

    bool LoadPackage(const fs::path& path, Package* pkg)
    {
        FILE* fp = fopen(path.string().c_str(), "rb");
        if (fp == NULL)
        {
            return false;
        }
        bool hasToc = iModPkgReadToc(fp, NULL, &pkg->toc);
        fseek(fp, 0, SEEK_END);
        long len = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        bool ok = len > 0 && hasToc;
        if (ok)
        {
            pkg->bytes.resize((size_t)len);
            ok = fread(pkg->bytes.data(), 1, (size_t)len, fp) == (size_t)len;
        }
        fclose(fp);
        for (size_t i = 0; ok && i < pkg->toc.assets.size(); i++)
        {
            pkg->byId[pkg->toc.assets[i].id] = (S32)i;
        }
        return ok;
    }

    bool AssetBytes(const Package& pkg, const iModPkgTocAsset& a, const U8** p)
    {
        if (a.off > pkg.bytes.size() || a.size > pkg.bytes.size() - a.off)
        {
            return false;
        }
        *p = pkg.bytes.data() + a.off;
        return true;
    }

    std::map<std::string, fs::path> FindPackages(const fs::path& root)
    {
        std::map<std::string, fs::path> out;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec))
        {
            if (ec || !it->is_regular_file())
            {
                continue;
            }
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext != ".hip" && ext != ".hop")
            {
                continue;
            }
            std::string rel = fs::relative(it->path(), root).generic_string();
            std::transform(rel.begin(), rel.end(), rel.begin(), ::tolower);
            out[rel] = it->path();
        }
        return out;
    }

    // A copy of `n` bytes that ends where an inaccessible page begins, so
    // that reading past its end stops the test at the read.
    struct Guarded
    {
        U8* base = NULL;
        size_t total = 0;
        U8* data = NULL;

        void Make(const U8* src, size_t n)
        {
            Free();
#ifdef _WIN32
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            size_t page = si.dwPageSize;
#else
            size_t page = (size_t)sysconf(_SC_PAGESIZE);
#endif
            size_t body = (n + page - 1) / page * page;
            total = body + page;
#ifdef _WIN32
            base = (U8*)VirtualAlloc(NULL, total, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            DWORD old;
            VirtualProtect(base + body, page, PAGE_NOACCESS, &old);
#else
            base = (U8*)mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            mprotect(base + body, page, PROT_NONE);
#endif
            data = base + body - n;
            memcpy(data, src, n);
        }

        void Free()
        {
            if (base != NULL)
            {
#ifdef _WIN32
                VirtualFree(base, 0, MEM_RELEASE);
#else
                munmap(base, total);
#endif
            }
            base = NULL;
            data = NULL;
        }

        ~Guarded()
        {
            Free();
        }
    };

    // --- RenderWare chunks ------------------------------------------------------------

    struct Chunk
    {
        U32 type, size, body;
    };

    bool ChunkAt(const U8* d, U32 n, U32 o, Chunk* c)
    {
        if (o > n || n - o < 12)
        {
            return false;
        }
        c->type = RdLE(d + o);
        c->size = RdLE(d + o + 4);
        c->body = o + 12;
        return c->size <= n - c->body;
    }

    // The native texture struct of a one-texture dictionary.
    bool TextureStruct(const U8* d, U32 n, U32* off, U32* size)
    {
        Chunk dict, s, tex, ts;
        if (!ChunkAt(d, n, 0, &dict) || dict.type != 0x16 || !ChunkAt(d, n, dict.body, &s) ||
            !ChunkAt(d, n, s.body + s.size, &tex) || tex.type != 0x15 ||
            !ChunkAt(d, n, tex.body, &ts) || ts.type != 1)
        {
            return false;
        }
        *off = ts.body;
        *size = ts.size;
        return true;
    }

    // Every STRUCT body directly inside a NATIVEDATA (0x510) or SKIN (0x116)
    // chunk, anywhere in the stream: the bytes the GameCube readers parse.
    void NativeStructs(const U8* d, U32 n, U32 o, U32 end, int depth,
                       std::vector<std::pair<U32, U32> >* out)
    {
        Chunk c;
        while (depth < 12 && ChunkAt(d, end, o, &c))
        {
            if (c.type == 0x510 || c.type == 0x116)
            {
                Chunk s;
                if (ChunkAt(d, c.body + c.size, c.body, &s) && s.type == 1)
                {
                    out->push_back(std::make_pair(s.body, s.size));
                }
            }
            else if (c.type != 1 && c.size >= 12)
            {
                // Containers: clump, frame list, geometry list, geometry,
                // atomic, extension, material list, material.
                switch (c.type)
                {
                case 0x10: case 0x0E: case 0x1A: case 0x0F: case 0x14: case 0x03: case 0x08:
                case 0x07:
                    NativeStructs(d, n, c.body, c.body + c.size, depth + 1, out);
                    break;
                }
            }
            o = c.body + c.size;
        }
    }

    // --- librw ------------------------------------------------------------------------

    void StartLibrw()
    {
        rw::platform = rw::PLATFORM_NULL;
        rw::Engine::init(NULL);
        rw::registerMeshPlugin();
        rw::registerNativeDataPlugin();
        rw::registerAtomicRightsPlugin();
        rw::registerMaterialRightsPlugin();
        rw::registerSkinPlugin();
        rw::registerHAnimPlugin();
        rw::registerMatFXPlugin();
        rw::registerUVAnimPlugin();
        rw::registerUserDataPlugin();
        rw::EngineOpenParams params;
        rw::Engine::open(&params);
        rw::Engine::start();
        rw::Texture::setLoadTextures(0);
    }

    rw::Clump* ReadClump(const U8* d, U32 n)
    {
        rw::StreamMemory s;
        s.open((rw::uint8*)d, n);
        rw::Clump* c = NULL;
        if (rw::findChunk(&s, rw::ID_CLUMP, NULL, NULL))
        {
            c = rw::Clump::streamRead(&s);
        }
        s.close();
        return c;
    }

    rw::Texture* ReadXboxTexture(const U8* d, U32 n)
    {
        rw::StreamMemory s;
        s.open((rw::uint8*)d, n);
        rw::Texture* t = NULL;
        if (rw::findChunk(&s, rw::ID_TEXDICTIONARY, NULL, NULL))
        {
            rw::TexDictionary* dict = rw::TexDictionary::streamRead(&s);
            if (dict != NULL)
            {
                rw::LLLink* l = dict->textures.link.next;
                if (l != dict->textures.end())
                {
                    // Out of the dictionary first, as RWTX_Read does, so that
                    // destroying the dictionary leaves it alone.
                    t = rw::Texture::fromDict(l);
                    dict->remove(t);
                }
                dict->destroy();
            }
        }
        s.close();
        return t;
    }

    // --- report -----------------------------------------------------------------------

    U32 sShowMax = 8;
    std::map<std::string, U32> sCount;
    std::map<std::string, std::vector<std::string> > sExamples;

    void Note(const std::string& what, const std::string& example)
    {
        sCount[what]++;
        std::vector<std::string>& ex = sExamples[what];
        if (ex.size() < sShowMax && !example.empty())
        {
            ex.push_back(example);
        }
    }

    std::string Fmt(const char* fmt, ...)
    {
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        return buf;
    }

    // --- textures ---------------------------------------------------------------------

    int AlphaKind(rw::Image* img)
    {
        if (img == NULL)
        {
            return -1;
        }
        return !img->hasAlpha() ? 0 : img->alphaIsBinary() ? 1 : 2;
    }

    const char* const kAlphaNames[] = { "opaque", "cut out", "blended" };

    // PSNR of two RGBA images at the smaller size, the larger box-filtered
    // down; -1 when the sizes are not a power of two apart.
    double Psnr(rw::Image* a, rw::Image* b)
    {
        if (a->width > b->width)
        {
            std::swap(a, b);
        }
        if (a->width == 0 || a->height == 0 || b->width % a->width || b->height % a->height)
        {
            return -1;
        }
        int fx = b->width / a->width, fy = b->height / a->height;
        double se = 0;
        for (int y = 0; y < a->height; y++)
        {
            for (int x = 0; x < a->width; x++)
            {
                for (int c = 0; c < 4; c++)
                {
                    double sum = 0;
                    for (int j = 0; j < fy; j++)
                    {
                        for (int i = 0; i < fx; i++)
                        {
                            sum += b->pixels[(y * fy + j) * b->stride + (x * fx + i) * 4 + c];
                        }
                    }
                    double d = a->pixels[y * a->stride + x * 4 + c] - sum / (fx * fy);
                    se += d * d;
                }
            }
        }
        double mse = se / (a->width * a->height * 4.0);
        return mse <= 1e-9 ? 99.0 : 10.0 * log10(255.0 * 255.0 / mse);
    }

    rw::Image* To32(rw::Image* img)
    {
        if (img != NULL && img->depth != 32)
        {
            img->unpalettize();
            img->convertTo32();
        }
        return img;
    }

    struct TexStats
    {
        U32 gc = 0, decoded = 0, pairs = 0, sameSize = 0, smaller = 0, sameLevels = 0;
        U32 alphaSame = 0, named = 0, nameSame = 0, nameXboxEmpty = 0;
        U32 psnrN = 0, psnrLow = 0;
        double psnrSum = 0, psnrMin = 99;
        U32 fuzzRuns = 0, fuzzDecoded = 0;
    } T;

    void CheckTexture(const std::string& where, const iModPkgTocAsset& a, const U8* d, U32 n,
                      const U8* xd, U32 xn, S32 fuzz)
    {
        T.gc++;
        U32 so, ss;
        if (!TextureStruct(d, n, &so, &ss) || ss < 0x70 || RdBE(d + so) != 6)
        {
            Note("texture: not a GameCube texture dictionary", where);
            return;
        }
        const U8* st = d + so;

        rw::Image* lv[16];
        const char* why = NULL;
        int nl = rw::readGCTextureLevels(st, ss, lv, 16, &why);
        if (nl == 0)
        {
            Note(std::string("texture: does not decode: ") + (why ? why : "?"), where);
            return;
        }
        T.decoded++;

        // The struct's level size against the sum of the levels.
        int fmt = st[0x62];
        U32 lsOff = 0x68 + (fmt == 9 ? 512 : fmt == 8 ? 32 : 0);
        U32 stored = RdBE(st + lsOff);
        U32 need = 0;
        {
            int tw = fmt == 14 || fmt == 0 || fmt == 8 ? 8 : (fmt == 1 || fmt == 2 || fmt == 9) ? 8 : 4;
            int th = fmt == 14 || fmt == 0 || fmt == 8 ? 8 : (fmt == 1 || fmt == 2 || fmt == 9) ? 4 : 4;
            int tb = fmt == 6 ? 64 : 32;
            for (int i = 0; i < nl; i++)
            {
                int w = std::max(1, lv[0]->width >> i), h = std::max(1, lv[0]->height >> i);
                need += (U32)((w + tw - 1) / tw) * (U32)((h + th - 1) / th) * tb;
            }
        }
        if (need != stored)
        {
            Note("texture: stored level size is not the levels' size",
                 Fmt("%s stored %u levels %u", where.c_str(), stored, need));
        }
        if ((RdBE(st + 0x58) & 0x8000) != 0 &&
            nl != rw::Raster::calculateNumLevels(lv[0]->width, lv[0]->height))
        {
            Note("texture: mipmapped but not the whole chain",
                 Fmt("%s %dx%d %d levels", where.c_str(), lv[0]->width, lv[0]->height, nl));
        }
        if ((RdBE(st + 0x58) & 0x8000) == 0 && nl > 1)
        {
            Note("texture: several levels without the mipmap flag", where);
        }

        // The name the view writes.
        std::vector<U8> named(d, d + n);
        U32 wrote = iGCPackageRestoreTextureNames(named.data(), n, a.name.c_str());
        char gcName[33];
        memcpy(gcName, named.data() + so + 0x18, 32);
        gcName[32] = '\0';
        if (wrote)
        {
            T.named++;
        }
        else
        {
            Note("texture: no name restored", Fmt("%s '%s' native '%s'", where.c_str(),
                                                  a.name.c_str(), gcName));
        }

        if (xd != NULL)
        {
            rw::Texture* xt = ReadXboxTexture(xd, xn);
            if (xt == NULL || xt->raster == NULL)
            {
                Note("texture: the Xbox twin does not read", where);
            }
            else
            {
                T.pairs++;
                rw::Raster* xr = xt->raster;
                if (xr->width == lv[0]->width && xr->height == lv[0]->height)
                {
                    T.sameSize++;
                }
                else if (xr->width >= lv[0]->width && xr->height >= lv[0]->height)
                {
                    T.smaller++;
                }
                else
                {
                    Note("texture: larger on the GameCube",
                         Fmt("%s %dx%d vs %dx%d", where.c_str(), lv[0]->width, lv[0]->height,
                             xr->width, xr->height));
                }
                if (xr->getNumLevels() == nl)
                {
                    T.sameLevels++;
                }
                if (strcmp(xt->name, gcName) == 0)
                {
                    T.nameSame++;
                }
                else if (xt->name[0] == '\0')
                {
                    T.nameXboxEmpty++;
                }
                else
                {
                    Note("texture: restored name differs from the Xbox name",
                         Fmt("%s '%s' vs '%s'", where.c_str(), gcName, xt->name));
                }

                rw::Image* xi = To32(xr->toImage());
                if (xi != NULL)
                {
                    // Xbox DXT1 without the alpha flag is opaque, as the port draws it.
                    int ka = AlphaKind(lv[0]), kb = AlphaKind(xi);
                    if (ka == kb)
                    {
                        T.alphaSame++;
                    }
                    else
                    {
                        Note(Fmt("texture: level 0 is %s on the GameCube, %s on the Xbox",
                                 kAlphaNames[ka], kAlphaNames[kb]),
                             Fmt("%s '%s'", where.c_str(), gcName));
                    }
                    double p = Psnr(lv[0], xi);
                    if (p >= 0)
                    {
                        T.psnrN++;
                        T.psnrSum += p;
                        T.psnrMin = std::min(T.psnrMin, p);
                        if (p < 20)
                        {
                            T.psnrLow++;
                            Note("texture: PSNR under 20 dB against the Xbox art",
                                 Fmt("%s '%s' %.1f dB", where.c_str(), gcName, p));
                        }
                    }
                    xi->destroy();
                }
            }
            if (xt != NULL)
            {
                xt->destroy();
            }
        }
        for (int i = 0; i < nl; i++)
        {
            lv[i]->destroy();
        }

        // Damaged copies: cut short, and bytes changed, header first.
        for (S32 f = 0; f < fuzz; f++)
        {
            std::vector<U8> m(st, st + ss);
            U32 len = ss;
            switch (f % 4)
            {
            case 0:
                len = Rand() % ss;
                break;
            case 1:
                for (int k = 0; k < 4; k++)
                {
                    m[0x58 + Rand() % 16] = (U8)Rand();
                }
                break;
            case 2:
                for (int k = 0; k < 8; k++)
                {
                    m[Rand() % ss] = (U8)Rand();
                }
                break;
            case 3:
                m[0x5C + Rand() % 4] = 0xFF;
                m[0x61] = (U8)(Rand() % 16);
                len = 0x68 + Rand() % (ss - 0x68 + 1);
                break;
            }
            Guarded g;
            g.Make(m.data(), len);
            rw::Image* fl[16];
            int fn = rw::readGCTextureLevels(g.data, len, fl, 16, NULL);
            T.fuzzRuns++;
            if (fn > 0)
            {
                T.fuzzDecoded++;
            }
            for (int i = 0; i < fn; i++)
            {
                fl[i]->destroy();
            }
        }
    }

    // --- geometry ---------------------------------------------------------------------

    struct GeoStats
    {
        U32 clumps = 0, clumpFail = 0, geometries = 0, native = 0, stub = 0, stillNative = 0;
        U32 skinned = 0;
        U32 pairs = 0, atomCountSame = 0, triSame = 0, triAll = 0, triFound = 0, triWinding = 0;
        U32 meshIdxSame = 0, meshIdxAll = 0, vertsCompared = 0, nrmSame = 0, clrSame = 0;
        U32 uvSame = 0, skinCompared = 0, skinSame = 0, skinNear = 0, jspPackages = 0;
        U32 jspAtomsSame = 0;
        U32 fuzzRuns = 0;
        U32 maxIdxRatio = 0;
        U32 models = 0, modelsUnfit = 0, xbModels = 0, xbModelsUnfit = 0; // iGCPackageModelFits
        U32 skinFirst = 0, skinFirstStub = 0; // SkinFirst: geometries reordered, read as stubs
    } G;

    struct Key3
    {
        S32 x, y, z;
        bool operator<(const Key3& o) const
        {
            return x != o.x ? x < o.x : y != o.y ? y < o.y : z < o.z;
        }
        bool operator==(const Key3& o) const
        {
            return x == o.x && y == o.y && z == o.z;
        }
    };

    Key3 Q(const rw::V3d& v)
    {
        Key3 k = { (S32)lround(v.x * 256.0), (S32)lround(v.y * 256.0), (S32)lround(v.z * 256.0) };
        return k;
    }

    typedef std::vector<Key3> Tri; // three, rotated so the smallest comes first

    Tri OrientedTri(const Key3& a, const Key3& b, const Key3& c)
    {
        Tri t;
        if (!(b < a) && !(c < a))
        {
            t = { a, b, c };
        }
        else if (!(a < b) && !(c < b))
        {
            t = { b, c, a };
        }
        else
        {
            t = { c, a, b };
        }
        return t;
    }

    Tri UnorientedTri(const Key3& a, const Key3& b, const Key3& c)
    {
        Tri t = { a, b, c };
        std::sort(t.begin(), t.end());
        return t;
    }

    // Triangles with area, from the meshes (strips), as the renderer and the
    // collision would see them.
    void Triangles(rw::Geometry* g, std::multiset<Tri>* oriented, std::multiset<Tri>* any)
    {
        rw::MeshHeader* mh = g->meshHeader;
        if (mh == NULL || g->morphTargets == NULL || g->morphTargets[0].vertices == NULL)
        {
            return;
        }
        rw::V3d* v = g->morphTargets[0].vertices;
        rw::Mesh* m = mh->getMeshes();
        for (U32 i = 0; i < mh->numMeshes; i++)
        {
            if (m[i].indices == NULL || m[i].numIndices < 3)
            {
                continue;
            }
            bool strip = mh->flags == rw::MeshHeader::TRISTRIP;
            for (U32 j = 0; j + 2 < m[i].numIndices; j += strip ? 1 : 3)
            {
                rw::uint16 i0 = m[i].indices[j], i1 = m[i].indices[j + 1], i2 = m[i].indices[j + 2];
                if (strip && (j & 1))
                {
                    std::swap(i1, i2);
                }
                if (i0 >= g->numVertices || i1 >= g->numVertices || i2 >= g->numVertices)
                {
                    continue;
                }
                Key3 a = Q(v[i0]), b = Q(v[i1]), c = Q(v[i2]);
                if (a == b || b == c || a == c)
                {
                    continue;
                }
                oriented->insert(OrientedTri(a, b, c));
                any->insert(UnorientedTri(a, b, c));
            }
        }
    }

    // Per vertex: the attributes, keyed by position and texcoords.
    struct VertexInfo
    {
        rw::V3d n;
        rw::RGBA c;
        U8 bones[4];
        float w[4];
        bool hasN, hasC, hasSkin;
    };

    typedef std::multimap<std::pair<Key3, std::pair<S32, S32> >, VertexInfo> VertexMap;

    void Vertices(rw::Geometry* g, VertexMap* out)
    {
        rw::Skin* skin = rw::Skin::get(g);
        for (S32 i = 0; i < g->numVertices; i++)
        {
            VertexInfo vi;
            memset(&vi, 0, sizeof(vi));
            S32 u = 0, vv = 0;
            if (g->numTexCoordSets > 0 && g->texCoords[0] != NULL)
            {
                u = (S32)lround(g->texCoords[0][i].u * 4096.0);
                vv = (S32)lround(g->texCoords[0][i].v * 4096.0);
            }
            if ((g->flags & rw::Geometry::NORMALS) && g->morphTargets[0].normals != NULL)
            {
                vi.n = g->morphTargets[0].normals[i];
                vi.hasN = true;
            }
            if ((g->flags & rw::Geometry::PRELIT) && g->colors != NULL)
            {
                vi.c = g->colors[i];
                vi.hasC = true;
            }
            if (skin != NULL && skin->indices != NULL && skin->weights != NULL)
            {
                memcpy(vi.bones, skin->indices + i * 4, 4);
                memcpy(vi.w, skin->weights + i * 4, 16);
                vi.hasSkin = true;
            }
            out->insert(std::make_pair(
                std::make_pair(Q(g->morphTargets[0].vertices[i]), std::make_pair(u, vv)), vi));
        }
    }

    // A skin's weights at a vertex as (bone, weight in 128ths) with weight > 0, sorted.
    std::vector<std::pair<int, int> > SkinSet(const VertexInfo& v)
    {
        std::vector<std::pair<int, int> > s;
        for (int j = 0; j < 4; j++)
        {
            int w = (int)lround(v.w[j] * 128.0);
            if (w > 0)
            {
                s.push_back(std::make_pair((int)v.bones[j], w));
            }
        }
        std::sort(s.begin(), s.end());
        return s;
    }

    bool IsStub(rw::Geometry* g)
    {
        return g->numVertices == 1 && g->numTriangles == 0;
    }

    void CompareGeometry(const std::string& where, rw::Geometry* a, rw::Geometry* b)
    {
        G.pairs++;
        std::multiset<Tri> ao, aa, bo, ba;
        Triangles(a, &ao, &aa);
        Triangles(b, &bo, &ba);
        G.triAll += (U32)aa.size();
        U32 found = 0, wound = 0;
        for (const Tri& t : aa)
        {
            if (ba.count(t))
            {
                found++;
            }
        }
        for (const Tri& t : ao)
        {
            if (bo.count(t))
            {
                wound++;
            }
        }
        G.triFound += found;
        G.triWinding += wound;
        if (aa.size() == ba.size() && found == aa.size() && wound == ao.size())
        {
            G.triSame++;
        }
        else
        {
            Note("geometry: triangles differ from the Xbox twin",
                 Fmt("%s: %u vs %u triangles, %u found, %u with the same winding", where.c_str(),
                     (U32)aa.size(), (U32)ba.size(), found, wound));
        }

        if (a->meshHeader && b->meshHeader && a->meshHeader->numMeshes == b->meshHeader->numMeshes)
        {
            for (U32 m = 0; m < a->meshHeader->numMeshes; m++)
            {
                G.meshIdxAll++;
                U32 na = a->meshHeader->getMeshes()[m].numIndices;
                U32 nb = b->meshHeader->getMeshes()[m].numIndices;
                if (na == nb)
                {
                    G.meshIdxSame++;
                }
                else
                {
                    Note("geometry: a mesh's index count differs", Fmt("%s mesh %u: %u vs %u",
                                                                       where.c_str(), m, na, nb));
                }
            }
        }
        else
        {
            Note("geometry: mesh count differs", where);
        }

        // Vertices at one place with one texcoord can still differ (a hard
        // edge), so each GameCube vertex is matched with any Xbox vertex of
        // its key that agrees.
        VertexMap va, vb;
        Vertices(a, &va);
        Vertices(b, &vb);
        U32 missing = 0, badN = 0, badC = 0, badS = 0;
        for (VertexMap::const_iterator it = va.begin(); it != va.end(); ++it)
        {
            std::pair<VertexMap::const_iterator, VertexMap::const_iterator> r =
                vb.equal_range(it->first);
            if (r.first == r.second)
            {
                missing++;
                continue;
            }
            G.vertsCompared++;
            const VertexInfo& x = it->second;
            bool nOk = false, cOk = false, skinned = x.hasSkin;
            int bestMoved = 1 << 30;
            bool skinExact = false;
            for (VertexMap::const_iterator jt = r.first; jt != r.second; ++jt)
            {
                const VertexInfo& y = jt->second;
                nOk = nOk || (x.hasN == y.hasN &&
                              (!x.hasN || x.n.x * y.n.x + x.n.y * y.n.y + x.n.z * y.n.z > 0.99f));
                cOk = cOk ||
                      (x.hasC == y.hasC &&
                       (!x.hasC || (abs(x.c.red - y.c.red) <= 1 && abs(x.c.green - y.c.green) <= 1 &&
                                    abs(x.c.blue - y.c.blue) <= 1 && abs(x.c.alpha - y.c.alpha) <= 1)));
                if (x.hasSkin || y.hasSkin)
                {
                    skinned = true;
                    std::vector<std::pair<int, int> > sx = SkinSet(x), sy = SkinSet(y);
                    bool same = sx.size() == sy.size();
                    for (size_t k = 0; same && k < sx.size(); k++)
                    {
                        same = sx[k].first == sy[k].first && abs(sx[k].second - sy[k].second) <= 1;
                    }
                    skinExact = skinExact || same;
                    // The GameCube keeps weights in 128ths and drops what
                    // rounds to nothing, so the same skin can move a weight
                    // of 1/128 or so from one bone to another.
                    std::map<int, int> d;
                    for (auto& e : sx)
                    {
                        d[e.first] += e.second;
                    }
                    for (auto& e : sy)
                    {
                        d[e.first] -= e.second;
                    }
                    int moved = 0;
                    for (auto& e : d)
                    {
                        moved += abs(e.second);
                    }
                    bestMoved = std::min(bestMoved, moved);
                }
            }
            if (nOk)
            {
                G.nrmSame++;
            }
            else
            {
                if (badN < 2 && getenv("GCRW_NRM_DUMP") != NULL && x.hasN)
                {
                    const VertexInfo& y = r.first->second;
                    printf("nrm %s: (%.3f %.3f %.3f) vs (%.3f %.3f %.3f)\n", where.c_str(), x.n.x,
                           x.n.y, x.n.z, y.n.x, y.n.y, y.n.z);
                }
                badN++;
            }
            if (cOk)
            {
                G.clrSame++;
            }
            else
            {
                badC++;
            }
            if (skinned)
            {
                G.skinCompared++;
                if (skinExact)
                {
                    G.skinSame++;
                }
                else if (bestMoved <= 4)
                {
                    G.skinNear++;
                }
                else
                {
                    badS++;
                }
            }
        }
        if (missing)
        {
            Note("geometry: GameCube vertices with no Xbox vertex at the same place and texcoords",
                 Fmt("%s: %u of %u", where.c_str(), missing, (U32)va.size()));
        }
        if (badN)
        {
            Note("geometry: normals differ", Fmt("%s: %u", where.c_str(), badN));
        }
        if (badC)
        {
            Note("geometry: prelight differs", Fmt("%s: %u", where.c_str(), badC));
        }
        if (badS)
        {
            Note("geometry: skin weights differ by more than 4/128", Fmt("%s: %u", where.c_str(), badS));
        }
    }

    // Every atomic's geometry of a clump, in the clump's list order.
    std::vector<rw::Geometry*> Geometries(rw::Clump* c)
    {
        std::vector<rw::Geometry*> out;
        FORLIST(lnk, c->atomics)
        {
            out.push_back(rw::Atomic::fromClump(lnk)->geometry);
        }
        return out;
    }

    void CountGeometries(const std::string& where, const U8* d, U32 n, rw::Clump* c)
    {
        std::vector<std::pair<U32, U32> > natives;
        NativeStructs(d, n, 0, n, 0, &natives);
        for (rw::Geometry* g : Geometries(c))
        {
            G.geometries++;
            if (g->flags & rw::Geometry::NATIVE)
            {
                G.stillNative++;
                Note("geometry: still native after reading", where);
            }
            else if (IsStub(g) && !natives.empty())
            {
                G.stub++;
                Note("geometry: could not be de-instanced (stub)", where);
            }
            if (rw::Skin::get(g) != NULL)
            {
                G.skinned++;
            }
        }
        for (const std::pair<U32, U32>& s : natives)
        {
            if (s.second >= 4 && RdLE(d + s.first) == 6)
            {
                G.native++;
            }
        }
    }

    // A copy of the chunks in [o, end) of `d` in which every native geometry's
    // GameCube skin is replaced by a generic skin (librw's readSkin layout: one
    // bone, one weight, sized for the geometry struct's numVertices) placed
    // before its native data chunk, the containers' sizes rewritten to match.
    // The GameCube never wrote such a stream; a damaged or hand-made one can.
    // `moved` counts the geometries changed.
    std::vector<U8> SkinFirst(const U8* d, U32 o, U32 end, int depth, S32 numVertices, U32* moved)
    {
        std::vector<U8> out;
        Chunk c;
        while (depth < 12 && ChunkAt(d, end, o, &c))
        {
            const U8* hdr = d + o;
            std::vector<U8> body(d + c.body, d + c.body + c.size);
            if (c.type == 0x03 && numVertices > 0)
            {
                // The geometry's extension: the native data and a skin after it.
                std::vector<Chunk> kids;
                Chunk k;
                for (U32 q = c.body; ChunkAt(d, c.body + c.size, q, &k); q = k.body + k.size)
                {
                    kids.push_back(k);
                }
                size_t nat = kids.size(), skin = kids.size();
                for (size_t i = 0; i < kids.size(); i++)
                {
                    if (kids[i].type == 0x510 && nat == kids.size())
                    {
                        nat = i;
                    }
                    else if (kids[i].type == 0x116 && nat < i)
                    {
                        skin = i;
                    }
                }
                if (nat < skin && skin < kids.size())
                {
                    std::vector<U8> gen;
                    const U8 head[4] = { 1, 1, 1, 0 }; // numBones, numUsedBones, numWeights
                    gen.insert(gen.end(), head, head + 4);
                    gen.push_back(0); // usedBones[0]
                    gen.insert(gen.end(), (size_t)numVertices * 4, 0); // indices
                    for (S32 v = 0; v < numVertices; v++)
                    {
                        float w[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
                        gen.insert(gen.end(), (const U8*)w, (const U8*)w + 16);
                    }
                    float mat[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
                    gen.insert(gen.end(), (const U8*)mat, (const U8*)mat + 64);
                    gen.insert(gen.end(), 12, 0); // split data: boneLimit, numMeshes, rleSize
                    body.clear();
                    for (size_t i = 0; i < kids.size(); i++)
                    {
                        if (i == skin)
                        {
                            continue;
                        }
                        if (i == nat)
                        {
                            U8 sh[12];
                            memcpy(sh, d + kids[skin].body - 12, 12);
                            U32 n = (U32)gen.size();
                            memcpy(sh + 4, &n, 4);
                            body.insert(body.end(), sh, sh + 12);
                            body.insert(body.end(), gen.begin(), gen.end());
                        }
                        const U8* b = d + kids[i].body - 12;
                        body.insert(body.end(), b, b + 12 + kids[i].size);
                    }
                    (*moved)++;
                }
            }
            else if (c.type == 0x10 || c.type == 0x1A || c.type == 0x0F)
            {
                // Containers down to the geometry; a geometry's numVertices is
                // the third word of its struct, and only a native one is changed.
                S32 nv = 0;
                Chunk s;
                if (c.type == 0x0F && ChunkAt(d, c.body + c.size, c.body, &s) && s.type == 1 &&
                    s.size >= 16 && (RdLE(d + s.body) & 0x01000000) != 0)
                {
                    nv = (S32)RdLE(d + s.body + 8);
                }
                body = SkinFirst(d, c.body, c.body + c.size, depth + 1, nv, moved);
            }
            U8 h[12];
            memcpy(h, hdr, 12);
            U32 n = (U32)body.size();
            memcpy(h + 4, &n, 4);
            out.insert(out.end(), h, h + 12);
            out.insert(out.end(), body.begin(), body.end());
            o = c.body + c.size;
        }
        return out;
    }

    // A skin read before the native data is the generic one, sized for the
    // welded vertices: the geometry must come out as the stub, its skin laid
    // out for the stub's one vertex (read here, so AddressSanitizer sees an
    // array that is too short).
    void CheckSkinFirst(const std::string& where, const U8* d, U32 n)
    {
        U32 moved = 0;
        std::vector<U8> m = SkinFirst(d, 0, n, 0, 0, &moved);
        if (moved == 0)
        {
            return;
        }
        G.skinFirst += moved;
        rw::Clump* c = ReadClump(m.data(), (U32)m.size());
        if (c == NULL)
        {
            Note("skin first: clump does not read", where);
            return;
        }
        for (rw::Geometry* g : Geometries(c))
        {
            rw::Skin* skin = rw::Skin::get(g);
            if (skin == NULL)
            {
                continue;
            }
            if (g->flags & rw::Geometry::NATIVE)
            {
                Note("skin first: a geometry stayed native", where);
                continue;
            }
            if (IsStub(g))
            {
                G.skinFirstStub++;
            }
            else
            {
                Note("skin first: a geometry was not made the stub", where);
            }
            volatile U32 sum = 0;
            for (S32 v = 0; v < g->numVertices; v++)
            {
                for (int j = 0; j < 4; j++)
                {
                    sum += skin->indices[v * 4 + j] + (U32)skin->weights[v * 4 + j];
                }
            }
            (void)sum;
        }
        c->destroy();
    }

    void FuzzClump(const U8* d, U32 n, S32 fuzz)
    {
        std::vector<std::pair<U32, U32> > natives;
        NativeStructs(d, n, 0, n, 0, &natives);
        if (natives.empty())
        {
            return;
        }
        for (S32 f = 0; f < fuzz; f++)
        {
            std::vector<U8> m(d, d + n);
            // Past the platform word: a struct that names another platform
            // goes to that platform's reader (librw's Xbox, PS2 or D3D
            // ones), which is the same for any mod and not what this tests.
            std::pair<U32, U32> s = natives[Rand() % natives.size()];
            if (s.second <= 4)
            {
                continue;
            }
            s.first += 4;
            s.second -= 4;
            switch (f % 3)
            {
            case 0: // the header words: sizes, counts, offsets
                for (int k = 0; k < 3; k++)
                {
                    U32 o = s.first + Rand() % std::min<U32>(s.second, 96);
                    m[o] = (U8)Rand();
                }
                break;
            case 1: // anywhere in the struct
                for (int k = 0; k < 6; k++)
                {
                    m[s.first + Rand() % s.second] = (U8)Rand();
                }
                break;
            case 2: // a huge value in one word
            {
                U32 o = s.first + (Rand() % std::max<U32>(1, std::min<U32>(s.second, 128) / 4)) * 4;
                if (o + 4 <= s.first + s.second)
                {
                    U32 v = Rand() | 0x80000000u;
                    memcpy(&m[o], &v, 4);
                }
                break;
            }
            }
            G.fuzzRuns++;
            if (getenv("GCRW_FUZZ_TRACE") != NULL)
            {
                printf("fuzz run %u: struct at %u, kind %d\n", G.fuzzRuns, s.first, (int)(f % 3));
                fflush(stdout);
            }
            rw::Clump* c = ReadClump(m.data(), n);
            if (c != NULL)
            {
                // What the game does with a model: walk its triangles.
                for (rw::Geometry* g : Geometries(c))
                {
                    if (g->flags & rw::Geometry::NATIVE)
                    {
                        Note("fuzz: a geometry stayed native", "");
                        continue;
                    }
                    volatile U32 sum = 0;
                    for (S32 t = 0; t < g->numTriangles; t++)
                    {
                        sum += g->triangles[t].v[0] + g->triangles[t].v[1] + g->triangles[t].v[2];
                        if (g->triangles[t].v[0] >= g->numVertices ||
                            g->triangles[t].v[1] >= g->numVertices ||
                            g->triangles[t].v[2] >= g->numVertices)
                        {
                            Note("fuzz: a triangle indexes past the vertices", "");
                            break;
                        }
                    }
                    (void)sum;
                }
                c->destroy();
            }
        }
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: tssm_gcrwtest <GameCube files> [<Xbox files>] [--fuzz N] "
                        "[--seed S] [--only tex|geo] [--pkg TEXT] [--show N]\n");
        return 2;
    }
    fs::path gcRoot, xbRoot;
    S32 fuzz = 0;
    std::string only, pkgFilter;
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        if (a == "--fuzz" && i + 1 < argc)
        {
            fuzz = atoi(argv[++i]);
        }
        else if (a == "--seed" && i + 1 < argc)
        {
            sRandom = strtoull(argv[++i], NULL, 0) | 1;
        }
        else if (a == "--only" && i + 1 < argc)
        {
            only = argv[++i];
        }
        else if (a == "--pkg" && i + 1 < argc)
        {
            pkgFilter = argv[++i];
        }
        else if (a == "--show" && i + 1 < argc)
        {
            sShowMax = (U32)atoi(argv[++i]);
        }
        else if (gcRoot.empty())
        {
            gcRoot = a;
        }
        else
        {
            xbRoot = a;
        }
    }

    StartLibrw();

    std::map<std::string, fs::path> gcFiles = FindPackages(gcRoot);
    std::map<std::string, fs::path> xbFiles;
    if (!xbRoot.empty())
    {
        xbFiles = FindPackages(xbRoot);
    }
    printf("tssm_gcrwtest: %u GameCube packages%s\n", (U32)gcFiles.size(),
           xbRoot.empty() ? "" : Fmt(", %u Xbox", (U32)xbFiles.size()).c_str());

    U32 packages = 0;
    for (const auto& kv : gcFiles)
    {
        if (!pkgFilter.empty() && kv.first.find(pkgFilter) == std::string::npos)
        {
            continue;
        }
        Package gc;
        if (!LoadPackage(kv.second, &gc))
        {
            continue;
        }
        char id[8], name[32];
        if (iGCPackagePlatform(gc.bytes.data(), (U32)std::min<size_t>(gc.bytes.size(), 512), id,
                               sizeof(id), name, sizeof(name)) != IGCPLAT_GAMECUBE)
        {
            continue;
        }
        packages++;
        Package xb;
        bool haveXb = false;
        auto x = xbFiles.find(kv.first);
        if (x != xbFiles.end())
        {
            haveXb = LoadPackage(x->second, &xb);
        }

        std::vector<rw::Geometry*> gcJsp, xbJsp;
        std::vector<rw::Clump*> gcJspClumps, xbJspClumps;
        for (const iModPkgTocAsset& a : gc.toc.assets)
        {
            const U8* d;
            if (!AssetBytes(gc, a, &d) || a.size == 0)
            {
                continue;
            }
            std::string where = Fmt("%s %08X %s", kv.first.c_str(), a.id, a.name.c_str());
            const U8* xd = NULL;
            U32 xn = 0;
            const iModPkgTocAsset* xa = NULL;
            if (haveXb)
            {
                auto j = xb.byId.find(a.id);
                if (j != xb.byId.end() && AssetBytes(xb, xb.toc.assets[j->second], &xd))
                {
                    xa = &xb.toc.assets[j->second];
                    xn = xa->size;
                }
                else
                {
                    xd = NULL;
                }
            }

            if ((a.type == kRWTX || a.type == kTEXS) && only != "geo")
            {
                CheckTexture(where, a, d, a.size, xd, xn, fuzz);
            }
            else if ((a.type == kMODL || a.type == kJSP) && only != "tex")
            {
                if (a.type == kMODL)
                {
                    // The check a GameCube package's models pass as it is
                    // converted (iMod.cpp): every retail model must.
                    G.models++;
                    if (!iGCPackageModelFits(d, a.size))
                    {
                        G.modelsUnfit++;
                        Note("geometry: a model the conversion would refuse", where);
                    }
                    if (xd != NULL && xa->type == kMODL)
                    {
                        G.xbModels++;
                        if (!iGCPackageModelFits(xd, xn))
                        {
                            G.xbModelsUnfit++;
                            Note("geometry: an Xbox model the check would refuse", where);
                        }
                    }
                }
                Chunk c;
                if (!ChunkAt(d, a.size, 0, &c) || c.type != 0x10)
                {
                    continue; // JSPINFO, or not a clump
                }
                G.clumps++;
                rw::Clump* gcl = ReadClump(d, a.size);
                if (gcl == NULL)
                {
                    G.clumpFail++;
                    Note("geometry: clump does not read", where);
                    continue;
                }
                CountGeometries(where, d, a.size, gcl);
                if (a.type == kJSP)
                {
                    gcJspClumps.push_back(gcl);
                    for (rw::Geometry* g : Geometries(gcl))
                    {
                        gcJsp.push_back(g);
                    }
                }
                else
                {
                    if (xd != NULL)
                    {
                        rw::Clump* xcl = ReadClump(xd, xn);
                        if (xcl != NULL)
                        {
                            std::vector<rw::Geometry*> ga = Geometries(gcl), gb = Geometries(xcl);
                            if (ga.size() == gb.size())
                            {
                                G.atomCountSame++;
                                for (size_t k = 0; k < ga.size(); k++)
                                {
                                    CompareGeometry(Fmt("%s atomic %u", where.c_str(), (U32)k),
                                                    ga[k], gb[k]);
                                }
                            }
                            else
                            {
                                Note("geometry: atomic count differs", where);
                            }
                            xcl->destroy();
                        }
                    }
                    gcl->destroy();
                }
                CheckSkinFirst(where, d, a.size);
                FuzzClump(d, a.size, fuzz);
            }
        }

        // The world: every JSP clump's atomics in order, on both.
        if (haveXb && !gcJsp.empty() && only != "tex")
        {
            for (const iModPkgTocAsset& a : xb.toc.assets)
            {
                const U8* d;
                Chunk c;
                if (a.type != kJSP || !AssetBytes(xb, a, &d) || !ChunkAt(d, a.size, 0, &c) ||
                    c.type != 0x10)
                {
                    continue;
                }
                rw::Clump* xcl = ReadClump(d, a.size);
                if (xcl != NULL)
                {
                    xbJspClumps.push_back(xcl);
                    for (rw::Geometry* g : Geometries(xcl))
                    {
                        xbJsp.push_back(g);
                    }
                }
            }
            G.jspPackages++;
            if (gcJsp.size() == xbJsp.size())
            {
                G.jspAtomsSame++;
                for (size_t k = 0; k < gcJsp.size(); k++)
                {
                    CompareGeometry(Fmt("%s world atomic %u", kv.first.c_str(), (U32)k), gcJsp[k],
                                    xbJsp[k]);
                }
            }
            else
            {
                Note("geometry: world atomic count differs",
                     Fmt("%s: %u vs %u", kv.first.c_str(), (U32)gcJsp.size(), (U32)xbJsp.size()));
            }
        }
        for (rw::Clump* c : gcJspClumps)
        {
            c->destroy();
        }
        for (rw::Clump* c : xbJspClumps)
        {
            c->destroy();
        }
    }

    printf("\n%u GameCube packages read\n", packages);
    printf("\ntextures: %u GameCube, %u decode", T.gc, T.decoded);
    printf(", %u names restored\n", T.named);
    if (T.pairs)
    {
        printf("  against the Xbox: %u pairs, %u the same size, %u smaller on the GameCube, %u "
               "the same level count\n",
               T.pairs, T.sameSize, T.smaller, T.sameLevels);
        printf("  level 0 alpha kind the same: %u; restored name = Xbox name: %u (Xbox name "
               "empty: %u)\n",
               T.alphaSame, T.nameSame, T.nameXboxEmpty);
        if (T.psnrN)
        {
            printf("  PSNR of level 0 at the smaller size: mean %.1f dB, lowest %.1f dB, %u "
                   "under 20 dB (of %u)\n",
                   T.psnrSum / T.psnrN, T.psnrMin, T.psnrLow, T.psnrN);
        }
    }
    if (T.fuzzRuns)
    {
        printf("  damaged copies: %u decoded, %u of them still decoded\n", T.fuzzRuns,
               T.fuzzDecoded);
    }
    printf("\ngeometry: %u clumps (%u do not read), %u geometries, %u native structs, %u skinned\n",
           G.clumps, G.clumpFail, G.geometries, G.native, G.skinned);
    printf("  still native after reading: %u; left as a stub: %u\n", G.stillNative, G.stub);
    if (G.pairs)
    {
        printf("  against the Xbox: %u atomic pairs; triangles all the same: %u\n", G.pairs,
               G.triSame);
        printf("  triangles: %u, %u found on the Xbox (%.3f%%), %u with the same winding\n",
               G.triAll, G.triFound, G.triAll ? 100.0 * G.triFound / G.triAll : 0.0,
               G.triWinding);
        printf("  BinMesh index counts the same: %u of %u\n", G.meshIdxSame, G.meshIdxAll);
        printf("  vertices matched by place and texcoords: %u; normals %u, prelight %u the "
               "same\n",
               G.vertsCompared, G.nrmSame, G.clrSame);
        printf("  skinned vertices compared: %u, weights the same: %u, within 4/128 in all: %u\n",
               G.skinCompared, G.skinSame, G.skinNear);
        printf("  world (JSP) packages: %u, atomic count the same: %u\n", G.jspPackages,
               G.jspAtomsSame);
    }
    printf("  generic skin before the native data: %u geometries, %u read as the stub\n",
           G.skinFirst, G.skinFirstStub);
    if (G.fuzzRuns)
    {
        printf("  damaged clumps read: %u\n", G.fuzzRuns);
    }
    printf("  models that hold together (iGCPackageModelFits): %u of %u; Xbox %u of %u\n",
           G.models - G.modelsUnfit, G.models, G.xbModels - G.xbModelsUnfit, G.xbModels);

    printf("\n-- notes\n");
    for (const auto& kv : sCount)
    {
        printf("%7u  %s\n", kv.second, kv.first.c_str());
        for (const std::string& e : sExamples[kv.first])
        {
            printf("           %s\n", e.c_str());
        }
    }

    bool ok = T.decoded == T.gc && G.clumpFail == 0 && G.stillNative == 0 && G.stub == 0 &&
              G.modelsUnfit == 0 && G.xbModelsUnfit == 0 &&
              G.skinFirst > 0 && G.skinFirstStub == G.skinFirst &&
              sCount.count("fuzz: a triangle indexes past the vertices") == 0 &&
              sCount.count("fuzz: a geometry stayed native") == 0;
    printf("\n%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
