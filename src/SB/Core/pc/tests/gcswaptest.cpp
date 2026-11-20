// tssm_gcswaptest: the GameCube byte swaps (iGCSwap.h) run over every package
// of a GameCube disc, without the game.
//
//     tssm_gcswaptest <GameCube files> [<Xbox files>] [options]
//
//       --fuzz N      after each asset, N damaged copies of it (default 0)
//       --type XXXX   only that type (DYNA, SIMP, "CSN "); --type DYNA:name too
//       --show N      list up to N mismatching offsets per type (default 8)
//       --dump N      every differing word of the first N assets that differ
//       --seed S      the damage's random seed
//
// For every package it checks the platform its PLAT chunk gives (iGCPackage.h).
// For every asset of a GameCube package it runs iGCSwapAsset on a copy that
// ends where an inaccessible page begins, with a pattern in front of it: a
// read or write past the end stops the test at the faulty access, and a write
// before the start is caught when the pattern is checked. Each asset is also
// swapped cut short at a run of lengths, and with --fuzz with bytes and words
// damaged; none of those may touch anything outside the copy either.
//
// With an Xbox folder, each converted asset is compared with the Xbox asset of
// the same id in the package of the same path. A word that differs is
// "layout" when some byte order of the GameCube word gives the Xbox one -- a
// rule is wrong there -- "near" when the two values differ by a rounding (a
// float a few units in the last place apart, a 16-bit key off by one or two),
// and "content" otherwise: the two builds shipped different values. Cutscenes are cut differently on the two discs, so a CSN
// is checked against its own disc's CTOC instead, and its animations for the
// Xbox magic.
//
// Exit code 0 when every GameCube asset converted (or is read as it is), no
// copy was touched outside its bytes, and no layout difference was found.

#include "iGCPackage.h"
#include "iGCSwap.h"
#include "iModPackage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <filesystem>
#include <map>
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
    U32 RdBE(const U8* p)
    {
        return ((U32)p[0] << 24) | ((U32)p[1] << 16) | ((U32)p[2] << 8) | (U32)p[3];
    }

    U32 RdLE(const U8* p)
    {
        return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
    }

    U32 FourCC(const char* s)
    {
        char b[4] = { ' ', ' ', ' ', ' ' };
        for (int i = 0; i < 4 && s[i]; i++)
        {
            b[i] = s[i];
        }
        return ((U32)(U8)b[0] << 24) | ((U32)(U8)b[1] << 16) | ((U32)(U8)b[2] << 8) | (U8)b[3];
    }

    std::string TypeName(U32 t)
    {
        char b[5];
        return iGCSwapTypeName(t, b);
    }

    // xStrHash, for the DYNA subtype names.
    U32 StrHash(const char* s)
    {
        U32 h = 0;
        for (; *s; s++)
        {
            U8 ch = (U8)*s;
            h = (U32)((ch - (ch & (ch >> 1) & 0x20)) & 0xFF) + h * 0x83;
        }
        return h;
    }

    const char* const kDynaNames[] = {
        "Enemy:SB:SupplyCrate", "Enemy:SB:Standard", "Enemy:SB:BucketOTron", "Enemy:SB:CastNCrew",
        "Enemy:SB:Critter", "Enemy:SB:Dennis", "Enemy:SB:FrogFish", "Enemy:SB:Mindy",
        "Enemy:SB:Neptune", "Enemy:SB:Turret", "Enemy:SB", "game_object:Ring",
        "game_object:RingControl", "game_object:RaceTimer", "game_object:Vent",
        "game_object:VentType", "game_object:talk_box", "game_object:task_box",
        "game_object:text_box", "game_object:flame_emitter", "game_object:BoulderGenerator",
        "game_object:Camera_Tweak", "game_object:Flythrough", "game_object:Teleport",
        "game_object:BusStop", "game_object:bungee_drop", "game_object:bungee_hook", "pointer",
        "ui:image", "ui:text", "ui:model", "ui:box", "ui:controller", "ui:text:user string",
        "hud:model", "hud:text", "hud:image", "hud:meter:font", "hud:meter:unit",
        "effect:ScreenFade", "effect:Rumble", "effect:Rumble Spherical Emitter",
        "effect:Rumble Box Emitter", "effect:Lightning", "effect:spotlight", "effect:smoke_emitter",
        "Effect:particle_generator", "JSP Extra Data", "logic:reference", "logic:Function Generator",
        "effect:Lens Flare Element", "effect:Lens Flare Params", "effect:Lens Flare Source",
    };

    std::string DynaName(U32 hash)
    {
        for (const char* n : kDynaNames)
        {
            if (StrHash(n) == hash)
            {
                return n;
            }
        }
        if (hash == 0xFABDB3B3)
        {
            return "zSceneParameters";
        }
        char b[16];
        snprintf(b, sizeof(b), "%08X", hash);
        return b;
    }

    // The key results are grouped by: the type, or DYNA and its subtype.
    std::string KeyOf(U32 type, const U8* raw, U32 size)
    {
        if (type == FourCC("DYNA") && size >= 12)
        {
            return "DYNA:" + DynaName(RdBE(raw + 8));
        }
        return TypeName(type);
    }

    // A buffer whose end is an inaccessible page.
    struct Guarded
    {
        U8* base = NULL;
        size_t cap = 0;
        size_t page = 4096;

        bool Reserve(size_t want)
        {
            if (want <= cap)
            {
                return true;
            }
            Free();
#ifdef _WIN32
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            page = si.dwPageSize;
#else
            page = (size_t)sysconf(_SC_PAGESIZE);
#endif
            size_t c = (want + page - 1) / page * page;
#ifdef _WIN32
            U8* p = (U8*)VirtualAlloc(NULL, c + page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (p == NULL)
            {
                return false;
            }
            DWORD old;
            VirtualProtect(p + c, page, PAGE_NOACCESS, &old);
#else
            U8* p = (U8*)mmap(NULL, c + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == (U8*)MAP_FAILED)
            {
                return false;
            }
            mprotect(p + c, page, PROT_NONE);
#endif
            base = p;
            cap = c;
            return true;
        }

        void Free()
        {
            if (base != NULL)
            {
#ifdef _WIN32
                VirtualFree(base, 0, MEM_RELEASE);
#else
                munmap(base, cap + page);
#endif
            }
            base = NULL;
            cap = 0;
        }
    };

    const U32 kFront = 64; // pattern bytes in front of each copy
    Guarded sBuf;
    S32 sCanaryFailures;

    // Runs the swap on a copy of `n` bytes of `src` placed against the guard
    // page, and checks the pattern in front of it. The copy is left in *out.
    iGCSwapResult RunGuarded(U32 type, const U8* src, U32 n, const U8** out)
    {
        if (!sBuf.Reserve((size_t)n + kFront))
        {
            fprintf(stderr, "gcswaptest: out of memory for %u bytes\n", n);
            exit(2);
        }
        U8* dst = sBuf.base + sBuf.cap - n;
        memset(dst - kFront, 0xA5, kFront);
        if (n != 0)
        {
            memcpy(dst, src, n);
        }
        iGCSwapResult r = iGCSwapAsset(type, dst, n);
        for (U32 i = 0; i < kFront; i++)
        {
            if (dst[-(S32)kFront + (S32)i] != 0xA5)
            {
                sCanaryFailures++;
                fprintf(stderr, "gcswaptest: %s wrote before its copy (%u bytes)\n",
                        TypeName(type).c_str(), n);
                break;
            }
        }
        if (out != NULL)
        {
            *out = dst;
        }
        return r;
    }

    S32 sEmptyPackages;

    struct Package
    {
        std::vector<U8> bytes;
        iModPkgToc toc;
        iGCPlatform platform = IGCPLAT_NONE;
        std::string id;
        std::map<U32, S32> byId;
    };

    bool LoadPackage(const fs::path& path, Package* pkg)
    {
        FILE* fp = fopen(path.string().c_str(), "rb");
        if (fp == NULL)
        {
            return false;
        }
        // iModPkgReadToc refuses a package with no assets (the GameCube's
        // plat.HIP is one: its water and reflection art is Xbox-only). That
        // is a package with nothing to convert, not an unreadable one.
        bool hasToc = iModPkgReadToc(fp, NULL, &pkg->toc);
        fseek(fp, 0, SEEK_END);
        long len = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        bool ok = len > 0;
        if (ok)
        {
            pkg->bytes.resize((size_t)len);
            ok = fread(pkg->bytes.data(), 1, (size_t)len, fp) == (size_t)len;
        }
        fclose(fp);
        if (!ok)
        {
            return false;
        }
        char id[8], name[32];
        pkg->platform = iGCPackagePlatform(pkg->bytes.data(), (U32)std::min<size_t>(pkg->bytes.size(), 512),
                                           id, sizeof(id), name, sizeof(name));
        if (!hasToc)
        {
            pkg->toc = iModPkgToc();
            if (pkg->platform == IGCPLAT_NONE || pkg->platform == IGCPLAT_NOTPACKAGE)
            {
                return false;
            }
            sEmptyPackages++;
        }
        pkg->id = id;
        for (size_t i = 0; i < pkg->toc.assets.size(); i++)
        {
            pkg->byId[pkg->toc.assets[i].id] = (S32)i;
        }
        return true;
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

    struct Stats
    {
        S32 assets = 0, done = 0, asis = 0, unknown = 0, malformed = 0;
        S32 pairs = 0, sameSize = 0, exact = 0, nearAssets = 0, layoutAssets = 0, contentAssets = 0;
        S64 bytes = 0;
        std::map<U32, S32> layoutAt; // word offset -> assets with a layout difference there
        std::map<U32, std::string> layoutHow;
        std::vector<std::string> examples;
    };

    std::map<std::string, Stats> sStats;

    // Some byte order of the GameCube word gives the Xbox one.
    const char* LayoutClass(const U8* raw, const U8* xb)
    {
        if (memcmp(raw, xb, 4) == 0)
        {
            return "as on the GameCube disc";
        }
        if (raw[0] == xb[3] && raw[1] == xb[2] && raw[2] == xb[1] && raw[3] == xb[0])
        {
            return "a u32";
        }
        if (raw[0] == xb[1] && raw[1] == xb[0] && raw[2] == xb[3] && raw[3] == xb[2])
        {
            return "two u16";
        }
        if (raw[0] == xb[1] && raw[1] == xb[0] && raw[2] == xb[2] && raw[3] == xb[3])
        {
            return "u16 then bytes";
        }
        if (raw[0] == xb[0] && raw[1] == xb[1] && raw[2] == xb[3] && raw[3] == xb[2])
        {
            return "bytes then u16";
        }
        return NULL;
    }

    // --dump: every differing word of the first assets that differ.
    S32 sDumpLeft;

    void Dump(const std::string& where, const U8* raw, const U8* conv, U32 n, const U8* xb)
    {
        printf("== %s (%u bytes)\n", where.c_str(), n);
        for (U32 o = 0; o + 4 <= n; o += 4)
        {
            if (memcmp(conv + o, xb + o, 4) == 0)
            {
                continue;
            }
            const char* how = LayoutClass(raw + o, xb + o);
            printf("  +%04X gc %02x%02x%02x%02x conv %02x%02x%02x%02x xbox %02x%02x%02x%02x %s\n", o, raw[o],
                   raw[o + 1], raw[o + 2], raw[o + 3], conv[o], conv[o + 1], conv[o + 2], conv[o + 3], xb[o],
                   xb[o + 1], xb[o + 2], xb[o + 3], how != NULL ? how : "(content)");
        }
    }

    // The two builds rounded the same value differently: a float a few units
    // in the last place apart, or a 16-bit quantity off by a few (animation
    // keys, 0x7FFF against 0x7FFE).
    bool Near(const U8* a, const U8* b)
    {
        S32 wa = (S32)RdLE(a), wb = (S32)RdLE(b);
        if ((wa ^ wb) >= 0 && (wa > wb ? wa - wb : wb - wa) <= 4)
        {
            return true;
        }
        for (S32 h = 0; h < 4; h += 2)
        {
            S32 x = (S16)(a[h] | (a[h + 1] << 8)), y = (S16)(b[h] | (b[h + 1] << 8));
            if ((x > y ? x - y : y - x) > 2)
            {
                return false;
            }
        }
        return true;
    }

    void Compare(Stats& st, const std::string& where, const U8* raw, const U8* conv, U32 n,
                 const U8* xb, U32 xn, U32 showMax)
    {
        st.pairs++;
        if (n != xn)
        {
            st.contentAssets++;
            return;
        }
        st.sameSize++;
        if (memcmp(conv, xb, n) == 0)
        {
            st.exact++;
            return;
        }
        if (sDumpLeft > 0)
        {
            sDumpLeft--;
            Dump(where, raw, conv, n, xb);
        }
        bool layout = false;
        bool roundedOnly = (n & 3) == 0 || memcmp(conv + (n & ~3u), xb + (n & ~3u), n & 3) == 0;
        for (U32 o = 0; o + 4 <= n; o += 4)
        {
            if (memcmp(conv + o, xb + o, 4) == 0)
            {
                continue;
            }
            roundedOnly = roundedOnly && Near(conv + o, xb + o);
            const char* how = LayoutClass(raw + o, xb + o);
            if (how != NULL)
            {
                layout = true;
                st.layoutAt[o]++;
                if (st.layoutHow.find(o) == st.layoutHow.end())
                {
                    st.layoutHow[o] = how;
                }
            }
        }
        if (layout)
        {
            st.layoutAssets++;
            if (st.examples.size() < showMax)
            {
                st.examples.push_back(where);
            }
        }
        else if (roundedOnly)
        {
            st.nearAssets++;
        }
        else
        {
            st.contentAssets++;
        }
    }

    // The damaged copies: cut short at a run of lengths, then --fuzz random
    // damage. Only whether they stay inside their bytes matters.
    U64 sRandom = 0x9E3779B97F4A7C15ull;

    U32 Rand()
    {
        sRandom ^= sRandom << 13;
        sRandom ^= sRandom >> 7;
        sRandom ^= sRandom << 17;
        return (U32)(sRandom >> 16);
    }

    S64 sDamaged;

    void Damage(U32 type, const U8* raw, U32 n, S32 fuzz)
    {
        U32 cuts[] = { 0, 1, 2, 3, 4, 5, 7, 8, 11, 12, 15, 16, 20, 24, 31, 32, 0x2C, 0x50, 0x54, 0x100,
                       n / 2, n / 3, n - 1, n - 2, n - 3, n - 4, n - 8, n - 16, n - 0x20 };
        for (U32 c : cuts)
        {
            if (c < n)
            {
                RunGuarded(type, raw, c, NULL);
                sDamaged++;
            }
        }
        if (fuzz <= 0 || n == 0)
        {
            return;
        }
        std::vector<U8> copy;
        S32 rounds = n > (1u << 20) ? std::min(fuzz, 2) : fuzz;
        for (S32 k = 0; k < rounds; k++)
        {
            copy.assign(raw, raw + n);
            S32 hits = 1 + (S32)(Rand() % 8);
            for (S32 h = 0; h < hits; h++)
            {
                U32 at = Rand() % n;
                switch (Rand() % 4)
                {
                case 0:
                    copy[at] ^= (U8)(1 + Rand() % 255);
                    break;
                case 1:
                    copy[at] = 0xFF;
                    break;
                default:
                {
                    // A big count or offset, in either byte order.
                    U32 w = at & ~3u;
                    if (w + 4 <= n)
                    {
                        static const U32 kBig[] = { 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x00010000,
                                                    0x10000000, 0x0000FFFF, 0x00000100 };
                        U32 v = kBig[Rand() % 7];
                        if (Rand() & 1)
                        {
                            v = ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | (v >> 24);
                        }
                        memcpy(&copy[w], &v, 4);
                    }
                    break;
                }
                }
            }
            if (Rand() % 4 == 0)
            {
                copy.resize(Rand() % n);
            }
            RunGuarded(type, copy.data(), (U32)copy.size(), NULL);
            sDamaged++;
        }
    }

    // CSN: the converted header against the converted CTOC entry of the same
    // cutscene, and the time chunks' animations for the Xbox's SKB magic.
    struct CutsceneHeader
    {
        std::vector<U8> bytes;
    };
    std::map<U32, CutsceneHeader> sTocHeaders; // by cutscene asset id, converted
    U32 sSkbMagic; // the first word of an Xbox ANIM
    S32 sCsnChecked, sCsnHeaderMatch, sCsnAnims, sCsnAnimMagicBad, sCsnTimeChunks;

    void NoteToc(const U8* conv, U32 n)
    {
        if (n < 4)
        {
            return;
        }
        U32 count = RdLE(conv);
        U32 c = 4;
        for (U32 i = 0; i < count && c + 0x830 <= n; i++)
        {
            U32 header = RdLE(conv + c + 0x1C);
            if (header < 0x830 || header > n - c)
            {
                return;
            }
            sTocHeaders[RdLE(conv + c + 4)].bytes.assign(conv + c, conv + c + header);
            c += header;
        }
    }

    void CheckCutscene(const U8* conv, U32 n)
    {
        sCsnChecked++;
        U32 id = RdLE(conv + 4);
        U32 header = RdLE(conv + 0x1C);
        std::map<U32, CutsceneHeader>::const_iterator toc = sTocHeaders.find(id);
        if (toc != sTocHeaders.end() && toc->second.bytes.size() == header &&
            memcmp(toc->second.bytes.data(), conv, header) == 0)
        {
            sCsnHeaderMatch++;
        }
        U32 numData = RdLE(conv + 8);
        U32 numTime = RdLE(conv + 0xC);
        U32 pos = (header + 2047) / 2048 * 2048;
        for (U32 i = 0; i < numData; i++)
        {
            U32 len = RdLE(conv + 0x830 + i * 16 + 8);
            pos += (len + 2047) / 2048 * 2048;
        }
        U32 offs = 0x830 + numData * 16;
        for (U32 t = 0; t < numTime && pos < n; t++)
        {
            U32 len = RdLE(conv + offs + t * 4 + 4) - RdLE(conv + offs + t * 4);
            if (len < 16 || len > n - pos)
            {
                break;
            }
            const U8* chunk = conv + pos;
            U32 recs = RdLE(chunk + 8);
            U32 c = 16;
            sCsnTimeChunks++;
            for (U32 r = 0; r < recs && c + 16 <= len; r++)
            {
                U32 type = RdLE(chunk + c);
                U32 size = RdLE(chunk + c + 8);
                if (size > len - c - 16)
                {
                    break;
                }
                if (type == 2 && size >= 0x20)
                {
                    sCsnAnims++;
                    if (sSkbMagic != 0 && RdLE(chunk + c + 16 + 16) != sSkbMagic)
                    {
                        sCsnAnimMagicBad++;
                    }
                }
                c += 16 + ((size + 15) & ~15u);
            }
            pos += len;
        }
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: tssm_gcswaptest <GameCube files> [<Xbox files>] [--fuzz N] [--type XXXX] "
                        "[--show N] [--seed S]\n");
        return 2;
    }

    fs::path gcRoot, xbRoot;
    S32 fuzz = 0;
    U32 showMax = 8;
    std::string only;
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        if (a == "--fuzz" && i + 1 < argc)
        {
            fuzz = atoi(argv[++i]);
        }
        else if (a == "--type" && i + 1 < argc)
        {
            only = argv[++i];
        }
        else if (a == "--show" && i + 1 < argc)
        {
            showMax = (U32)atoi(argv[++i]);
        }
        else if (a == "--dump" && i + 1 < argc)
        {
            sDumpLeft = atoi(argv[++i]);
        }
        else if (a == "--seed" && i + 1 < argc)
        {
            sRandom = strtoull(argv[++i], NULL, 0) | 1;
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

    std::map<std::string, fs::path> gcFiles = FindPackages(gcRoot);
    std::map<std::string, fs::path> xbFiles;
    if (!xbRoot.empty())
    {
        xbFiles = FindPackages(xbRoot);
    }
    if (gcFiles.empty())
    {
        fprintf(stderr, "gcswaptest: no packages under %s\n", gcRoot.string().c_str());
        return 2;
    }

    // The Xbox SKB magic, for the cutscene animations.
    std::map<std::string, S32> platformCounts;
    S32 failures = 0;
    S32 unreadable = 0;
    S32 badTocAssets = 0;
    S64 converted = 0;

    // Xbox packages: the platform each says.
    for (const auto& kv : xbFiles)
    {
        Package xb;
        if (!LoadPackage(kv.second, &xb))
        {
            continue;
        }
        platformCounts[std::string("xbox folder: ") +
                       (xb.platform == IGCPLAT_XBOX       ? "Xbox"
                        : xb.platform == IGCPLAT_GAMECUBE ? "GameCube"
                        : xb.platform == IGCPLAT_OTHER    ? "other"
                                                          : "no PLAT")]++;
        if (sSkbMagic == 0)
        {
            for (const iModPkgTocAsset& a : xb.toc.assets)
            {
                const U8* p;
                if (a.type == FourCC("ANIM") && a.size >= 4 && AssetBytes(xb, a, &p))
                {
                    sSkbMagic = RdLE(p);
                    break;
                }
            }
        }
    }

    // CTOCs first, so that each CSN can be checked against its own.
    for (int pass = 0; pass < 2; pass++)
    {
        for (const auto& kv : gcFiles)
        {
            Package gc;
            if (!LoadPackage(kv.second, &gc))
            {
                if (pass == 0)
                {
                    unreadable++;
                    fprintf(stderr, "gcswaptest: %s: not a package this reads\n", kv.first.c_str());
                }
                continue;
            }
            if (pass == 0)
            {
                platformCounts[std::string("GameCube folder: ") +
                               (gc.platform == IGCPLAT_XBOX       ? "Xbox"
                                : gc.platform == IGCPLAT_GAMECUBE ? "GameCube"
                                : gc.platform == IGCPLAT_OTHER    ? "other"
                                                                  : "no PLAT")]++;
            }
            if (gc.platform != IGCPLAT_GAMECUBE)
            {
                continue;
            }

            Package xb;
            bool haveXb = false;
            if (pass == 1)
            {
                std::map<std::string, fs::path>::const_iterator x = xbFiles.find(kv.first);
                haveXb = x != xbFiles.end() && LoadPackage(x->second, &xb);
            }

            for (const iModPkgTocAsset& a : gc.toc.assets)
            {
                bool isToc = a.type == FourCC("CTOC");
                if ((pass == 0) != isToc)
                {
                    continue;
                }
                const U8* raw;
                if (!AssetBytes(gc, a, &raw))
                {
                    badTocAssets++;
                    continue;
                }
                std::string key = KeyOf(a.type, raw, a.size);
                if (!only.empty() && only != key && only != TypeName(a.type) &&
                    !(only.size() <= 4 && FourCC(only.c_str()) == a.type))
                {
                    continue;
                }
                Stats& st = sStats[key];
                st.assets++;
                st.bytes += a.size;

                const U8* conv;
                iGCSwapResult r = RunGuarded(a.type, raw, a.size, &conv);
                switch (r)
                {
                case IGCSWAP_DONE:
                    st.done++;
                    converted++;
                    break;
                case IGCSWAP_ASIS:
                    st.asis++;
                    break;
                case IGCSWAP_UNKNOWN:
                    st.unknown++;
                    failures++;
                    if (st.examples.size() < showMax)
                    {
                        char b[160];
                        snprintf(b, sizeof(b), "%s %08X %s: no rule", kv.first.c_str(), a.id, a.name.c_str());
                        st.examples.push_back(b);
                    }
                    break;
                case IGCSWAP_MALFORMED:
                    st.malformed++;
                    failures++;
                    if (st.examples.size() < showMax)
                    {
                        char b[160];
                        snprintf(b, sizeof(b), "%s %08X %s: malformed", kv.first.c_str(), a.id, a.name.c_str());
                        st.examples.push_back(b);
                    }
                    break;
                }
                if (r != IGCSWAP_DONE)
                {
                    continue;
                }

                // Copy out of the guarded buffer before the next run reuses it.
                std::vector<U8> result(conv, conv + a.size);

                if (isToc)
                {
                    NoteToc(result.data(), a.size);
                }
                else if (a.type == FourCC("CSN "))
                {
                    CheckCutscene(result.data(), a.size);
                }

                if (haveXb && a.type != FourCC("CSN "))
                {
                    std::map<U32, S32>::const_iterator t = xb.byId.find(a.id);
                    const U8* xp;
                    if (t != xb.byId.end() && xb.toc.assets[t->second].type == a.type &&
                        AssetBytes(xb, xb.toc.assets[t->second], &xp))
                    {
                        char where[160];
                        snprintf(where, sizeof(where), "%s %08X %s", kv.first.c_str(), a.id, a.name.c_str());
                        Compare(st, where, raw, result.data(), a.size, xp, xb.toc.assets[t->second].size,
                                showMax);
                    }
                }

                Damage(a.type, raw, a.size, fuzz);
            }
        }
    }

    // The report.
    printf("packages:\n");
    for (const auto& kv : platformCounts)
    {
        printf("  %-28s %d\n", kv.first.c_str(), kv.second);
    }
    if (sEmptyPackages != 0)
    {
        printf("  package reads with no assets %d\n", sEmptyPackages);
    }
    if (unreadable || badTocAssets)
    {
        printf("  unreadable packages %d, assets outside their file %d\n", unreadable, badTocAssets);
    }
    printf("\n%-34s %6s %6s %5s %4s %4s | %6s %6s %6s %6s %6s %6s\n", "type", "assets", "done", "as-is",
           "unk", "bad", "pairs", "same", "exact", "near", "layout", "content");
    S32 layoutTotal = 0;
    for (const auto& kv : sStats)
    {
        const Stats& s = kv.second;
        printf("%-34s %6d %6d %5d %4d %4d | %6d %6d %6d %6d %6d %6d\n", kv.first.c_str(), s.assets, s.done,
               s.asis, s.unknown, s.malformed, s.pairs, s.sameSize, s.exact, s.nearAssets, s.layoutAssets,
               s.contentAssets);
        layoutTotal += s.layoutAssets;
    }
    for (const auto& kv : sStats)
    {
        const Stats& s = kv.second;
        if (s.layoutAt.empty() && s.examples.empty())
        {
            continue;
        }
        printf("\n%s:\n", kv.first.c_str());
        U32 shown = 0;
        for (const auto& at : s.layoutAt)
        {
            if (shown++ >= showMax * 4)
            {
                printf("  ... %d more offsets\n", (S32)s.layoutAt.size() - (S32)shown + 1);
                break;
            }
            printf("  +%04X in %d assets: the Xbox word is the GameCube one read as %s\n", at.first,
                   at.second, s.layoutHow.find(at.first)->second.c_str());
        }
        for (const std::string& e : s.examples)
        {
            printf("  e.g. %s\n", e.c_str());
        }
    }
    printf("\ncutscenes: %d CSN, %d with the CTOC's header, %d time chunks, %d animations, %d without the "
           "Xbox SKB magic\n",
           sCsnChecked, sCsnHeaderMatch, sCsnTimeChunks, sCsnAnims, sCsnAnimMagicBad);
    printf("converted %lld assets; %lld cut-short or damaged copies; %d copies touched before their start\n",
           (long long)converted, (long long)sDamaged, sCanaryFailures);
    if (!xbRoot.empty())
    {
        printf("layout differences from the Xbox twins: %d assets\n", layoutTotal);
    }

    bool ok = failures == 0 && sCanaryFailures == 0 && layoutTotal == 0 && unreadable == 0 &&
              badTocAssets == 0 && sCsnAnimMagicBad == 0 && sCsnHeaderMatch == sCsnChecked;
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
