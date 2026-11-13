// Checks the mod system by running it: tssm_modtest (CMake target).
//
//     tssm_modtest <asset folder> <scratch folder>
//
// The asset folder is your own extracted Xbox disc (TSSM_ASSETS); the scratch
// folder is where the test writes its mods. Without an asset folder only the
// parts that need no game file run (INI overlays, sound decoding, scanning).
//
// What it checks:
//   * INI overlays: replace, repeated keys, add, remove, sections, CRLF.
//   * Sound decoding: WAV at 8/16/24/32 bits and float, mono, stereo and
//     6-channel; with FFmpeg, a movie's soundtrack.
//   * Package views (iModPackage), against real packages: an asset replaced in
//     place, one too big for its slot (its layer moves), several layers at
//     once, and a sound with its SNDI entry. Each view is read back the way
//     the packer reads it -- layer by layer from the first asset, laysize
//     bytes, every asset at `layer + (d_off - first d_off)` -- and every
//     asset's bytes are compared with what they should be; the DICT, the
//     chunk sizes, the alignments and random reads across extents too.
//   * The manager (iMod): two mods, by name and by ID, scoped and not; load
//     order from folder names, priorities and mods.ini; conflicts; enable,
//     disable, move, save; loose files, textures, movies, INI overlays; the
//     global switch; and nothing at all when there are no mods.
//
// Exit code 0 when every check passed.

#include "iHost.h"
#include "iMod.h"
#include "iModAudio.h"
#include "iModIni.h"
#include "iModPackage.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

// --- the two settings iMod reads, and the texture hook it installs ----------------

static std::map<std::string, std::string> sConfig;

const char* iConfigGetString(const char* key, const char* def)
{
    std::map<std::string, std::string>::const_iterator it = sConfig.find(key);
    return it != sConfig.end() ? it->second.c_str() : def;
}

S32 iConfigGetBool(const char* key, S32 def)
{
    std::map<std::string, std::string>::const_iterator it = sConfig.find(key);
    if (it == sConfig.end())
    {
        return def;
    }
    return it->second == "on" || it->second == "1" ? 1 : 0;
}

void iModTextureInstall()
{
}

// --- checking ----------------------------------------------------------------------------

static int sChecks;
static int sFailures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        sChecks++;                                                                                 \
        if (!(cond))                                                                               \
        {                                                                                          \
            sFailures++;                                                                           \
            printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);                               \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static std::string sAssets;
static std::string sScratch;

static bool WriteFile(const std::string& path, const void* data, size_t len)
{
    FILE* fp = fopen(path.c_str(), "wb");
    if (fp == NULL)
    {
        return false;
    }
    bool ok = len == 0 || fwrite(data, 1, len, fp) == len;
    return fclose(fp) == 0 && ok;
}

static bool WriteText(const std::string& path, const std::string& text)
{
    return WriteFile(path, text.data(), text.size());
}

static void MakeDirs(const std::string& path)
{
    for (size_t i = 1; i <= path.size(); i++)
    {
        if (i == path.size() || path[i] == '/')
        {
            iHostMakeDir(path.substr(0, i).c_str());
        }
    }
}

static std::vector<U8> ReadRange(FILE* fp, U32 off, U32 size)
{
    std::vector<U8> out(size);
    if (size > 0)
    {
        fseek(fp, (long)off, SEEK_SET);
        size_t got = fread(&out[0], 1, size, fp);
        (void)got;
    }
    return out;
}

static std::vector<U8> ReadView(const iModPkg* pkg, FILE* fp, U32 off, U32 size)
{
    std::vector<U8> out(size);
    if (size > 0)
    {
        iModPkgRead(pkg, fp, off, &out[0], size);
    }
    return out;
}

// --- INI overlays ---------------------------------------------------------------------

static void TestIni()
{
    printf("-- INI overlays\n");
    std::string original = "PATH=.\r\n"
                           "\r\n"
                           "# comment\r\n"
                           "BOOT = BB02\r\n"
                           "LanguageSetting = US\r\n"
                           "LanguageSetting = JP\r\n"
                           "G.TakeDamage = 1 # yes\r\n"
                           "[Sec]\r\n"
                           "a = 1\r\n"
                           "b = 2\r\n";
    std::string overlay = "; a mod's overlay\n"
                          "boot = MNU3\n"
                          "LanguageSetting = UK\n"
                          "NewKey = 5\n"
                          "-G.TakeDamage\n"
                          "[sec]\n"
                          "B = 3\n"
                          "c = 4\n"
                          "[New]\n"
                          "x = y\n";
    std::string expect = "PATH=.\r\n"
                         "\r\n"
                         "# comment\r\n"
                         "boot = MNU3\r\n"
                         "LanguageSetting = UK\r\n"
                         "NewKey = 5\r\n"
                         "[Sec]\r\n"
                         "a = 1\r\n"
                         "B = 3\r\n"
                         "c = 4\r\n"
                         "[New]\r\n"
                         "x = y\r\n";
    std::vector<std::string> log;
    std::string got = iModIniMerge(original, overlay, &log);
    CHECK(got == expect, "merged:\n%s", got.c_str());
    CHECK(log.size() == 7, "%d log lines", (int)log.size());

    // Repeated overlay keys, and a file with no newline at the end.
    got = iModIniMerge("A = 1\nB = 2", "A = x\nA = y\nC = 3\n", NULL);
    CHECK(got == "A = x\nA = y\nB = 2\nC = 3\n", "merged:\n%s", got.c_str());

    // An empty overlay changes nothing.
    CHECK(iModIniMerge(original, "; nothing\n", NULL) == original, "empty overlay");

    std::string modIni = "\xEF\xBB\xBF[mod]\nname = \"Solid Colours\"\nauthor=me\n; c\nversion = 2\n"
                         "[other]\nname = no\n";
    std::vector<iModIniEntry> ini = iModIniRead(modIni.data(), modIni.size());
    CHECK(iModIniGet(ini, "mod", "name", "") == "Solid Colours", "name");
    CHECK(iModIniGet(ini, "MOD", "Author", "") == "me", "author");
    CHECK(iModIniGet(ini, "mod", "version", "") == "2", "version");
    CHECK(iModIniGet(ini, "mod", "missing", "dflt") == "dflt", "default");
    CHECK(iModIniGet(ini, "other", "name", "") == "no", "section");
}

// --- sound decoding -------------------------------------------------------------------

static std::vector<U8> MakeWav(U16 format, U16 channels, U32 rate, U16 bits, U32 frames)
{
    U16 block = (U16)(channels * bits / 8);
    U32 dataLen = frames * block;
    std::vector<U8> w(44 + dataLen);
    U8* p = &w[0];
    memcpy(p, "RIFF", 4);
    U32 v = 36 + dataLen;
    memcpy(p + 4, &v, 4);
    memcpy(p + 8, "WAVEfmt ", 8);
    v = 16;
    memcpy(p + 16, &v, 4);
    memcpy(p + 20, &format, 2);
    memcpy(p + 22, &channels, 2);
    memcpy(p + 24, &rate, 4);
    v = rate * block;
    memcpy(p + 28, &v, 4);
    memcpy(p + 32, &block, 2);
    memcpy(p + 34, &bits, 2);
    memcpy(p + 36, "data", 4);
    memcpy(p + 40, &dataLen, 4);
    for (U32 f = 0; f < frames; f++)
    {
        for (U16 c = 0; c < channels; c++)
        {
            // A known 16-bit value per frame and channel.
            S32 s16 = (S32)((f * 97 + c * 1000) % 60000) - 30000;
            U8* q = p + 44 + f * block + c * (bits / 8);
            switch (bits)
            {
            case 8:
                q[0] = (U8)((s16 >> 8) + 128);
                break;
            case 16:
                q[0] = (U8)s16;
                q[1] = (U8)(s16 >> 8);
                break;
            case 24:
                q[0] = 0;
                q[1] = (U8)s16;
                q[2] = (U8)(s16 >> 8);
                break;
            case 32:
                if (format == 3)
                {
                    float fl = s16 / 32767.0f;
                    memcpy(q, &fl, 4);
                }
                else
                {
                    S32 s32 = s16 << 16;
                    memcpy(q, &s32, 4);
                }
                break;
            }
        }
    }
    return w;
}

static S32 Sample(const iModPcm& pcm, U32 frame, U32 ch)
{
    const U8* p = &pcm.bytes[(frame * pcm.channels + ch) * 2];
    return (S16)(p[0] | (p[1] << 8));
}

static void TestAudio()
{
    printf("-- sound decoding (%s)\n", iModAudioDecoders());
    MakeDirs(sScratch + "/audio");

    struct Case
    {
        U16 format, channels, bits;
        S32 tolerance;
    } cases[] = { { 1, 1, 16, 0 }, { 1, 2, 16, 0 }, { 1, 2, 8, 256 }, { 1, 1, 24, 0 },
                  { 1, 2, 32, 0 }, { 3, 2, 32, 2 } };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        const Case& c = cases[i];
        std::vector<U8> wav = MakeWav(c.format, c.channels, 22050, c.bits, 500);
        char path[512];
        snprintf(path, sizeof(path), "%s/audio/t%d.wav", sScratch.c_str(), (int)i);
        WriteFile(path, &wav[0], wav.size());

        iModPcm pcm;
        std::string error;
        bool ok = iModDecodeAudio(path, &pcm, &error);
        CHECK(ok, "%s: %s", path, error.c_str());
        if (!ok)
        {
            continue;
        }
        CHECK(pcm.rate == 22050 && pcm.channels == c.channels, "%s: %u Hz x%u", path, pcm.rate,
              pcm.channels);
        CHECK(pcm.bytes.size() == 500u * c.channels * 2, "%s: %u bytes", path,
              (U32)pcm.bytes.size());
        S32 worst = 0;
        for (U32 f = 0; f < 500 && pcm.bytes.size() >= 500u * c.channels * 2; f++)
        {
            for (U32 ch = 0; ch < c.channels; ch++)
            {
                S32 want = (S32)((f * 97 + ch * 1000) % 60000) - 30000;
                S32 d = abs(Sample(pcm, f, ch) - want);
                worst = d > worst ? d : worst;
            }
        }
        CHECK(worst <= c.tolerance, "%s: off by %d", path, worst);
    }

    // Six channels mix down to two.
    {
        std::vector<U8> wav = MakeWav(1, 6, 48000, 16, 100);
        std::string path = sScratch + "/audio/six.wav";
        WriteFile(path, &wav[0], wav.size());
        iModPcm pcm;
        std::string error;
        CHECK(iModDecodeAudio(path.c_str(), &pcm, &error) && pcm.channels == 2 &&
                  pcm.bytes.size() == 100u * 2 * 2,
              "six channels: %s", error.c_str());
    }

    // Not audio at all.
    {
        std::string path = sScratch + "/audio/junk.ogg";
        WriteText(path, "this is not a sound");
        iModPcm pcm;
        std::string error;
        CHECK(!iModDecodeAudio(path.c_str(), &pcm, &error), "junk decoded");
    }

#ifdef TSSM_HAVE_FFMPEG
    // A movie's soundtrack: Bink audio, through FFmpeg.
    if (!sAssets.empty())
    {
        std::string path = sAssets + "/FMV/LogoHI.bik";
        iModPcm pcm;
        std::string error;
        bool ok = iModDecodeAudio(path.c_str(), &pcm, &error);
        CHECK(ok && pcm.channels >= 1 && pcm.rate >= 8000 && pcm.bytes.size() > 48000,
              "%s: %s", path.c_str(), error.c_str());
        printf("   FFmpeg: %s -> %u Hz x%u, %.1f s\n", path.c_str(), pcm.rate, pcm.channels,
               pcm.rate ? pcm.bytes.size() / 2.0 / pcm.channels / pcm.rate : 0.0);
    }
#endif
}

// --- package views --------------------------------------------------------------------

typedef std::map<U32, iModPkgReplacement> Replacements;

static bool MapLookup(void* ctx, const iModPkgAsset& a, iModPkgReplacement* out)
{
    Replacements* r = (Replacements*)ctx;
    Replacements::const_iterator it = r->find(a.id);
    if (it == r->end())
    {
        return false;
    }
    *out = it->second;
    return true;
}

static U32 LowBit(U32 v)
{
    return v == 0 ? 0x800 : (v & (~v + 1));
}

// Reads the view the way the packer would and compares every asset.
static void VerifyView(const std::string& path, const iModPkg* pkg,
                       const std::map<U32, std::vector<U8> >& expect, const char* label)
{
    FILE* fp = fopen(path.c_str(), "rb");
    CHECK(fp != NULL, "%s", path.c_str());
    if (fp == NULL)
    {
        return;
    }

    iModPkgToc orig, mod;
    CHECK(iModPkgReadToc(fp, NULL, &orig), "%s: original TOC", label);
    CHECK(iModPkgReadToc(fp, pkg, &mod), "%s: modded TOC", label);
    CHECK(orig.assets.size() == mod.assets.size() && orig.layers.size() == mod.layers.size(),
          "%s: %d/%d assets, %d/%d layers", label, (int)orig.assets.size(),
          (int)mod.assets.size(), (int)orig.layers.size(), (int)mod.layers.size());
    if (orig.assets.size() != mod.assets.size() || orig.layers.size() != mod.layers.size())
    {
        fclose(fp);
        return;
    }

    // The chunks: HIPA, then PACK DICT STRM, STRM and DPAK running to the end.
    {
        std::vector<U8> h = ReadView(pkg, fp, 0, 8);
        CHECK(memcmp(&h[0], "HIPA", 4) == 0, "%s: HIPA", label);
        U32 pos = 8;
        U32 strmEnd = 0;
        while (pos + 8 <= mod.fileSize)
        {
            h = ReadView(pkg, fp, pos, 8);
            U32 size = (h[4] << 24) | (h[5] << 16) | (h[6] << 8) | h[7];
            if (memcmp(&h[0], "STRM", 4) == 0)
            {
                strmEnd = pos + 8 + size;
            }
            pos += 8 + size;
        }
        CHECK(strmEnd == mod.fileSize && pos == mod.fileSize, "%s: STRM ends at %X, file at %X",
              label, strmEnd, mod.fileSize);
    }

    S32 moved = 0;
    for (size_t i = 0; i < mod.assets.size(); i++)
    {
        const iModPkgTocAsset& o = orig.assets[i];
        const iModPkgTocAsset& m = mod.assets[i];
        CHECK(o.id == m.id && o.type == m.type && o.name == m.name && o.layer == m.layer &&
                  o.flags == m.flags,
              "%s: asset %d header", label, (int)i);

        std::map<U32, std::vector<U8> >::const_iterator e = expect.find(o.id);
        std::vector<U8> want = e != expect.end() ? e->second : ReadRange(fp, o.off, o.size);
        std::vector<U8> got = ReadView(pkg, fp, m.off, m.size);
        CHECK(m.size == want.size() && got == want, "%s: %08X '%s' bytes (%u vs %u)", label,
              o.id, o.name.c_str(), m.size, (U32)want.size());

        if (m.off != o.off)
        {
            moved++;
            U32 align = LowBit(o.off) > 0x800 ? 0x800 : LowBit(o.off);
            CHECK(m.off % align == 0, "%s: %08X moved to %X, was aligned to %X", label, o.id,
                  m.off, align);
            CHECK(m.off >= orig.fileSize, "%s: %08X moved inside the original file", label,
                  o.id);
        }
    }

    // Layer by layer, as PKR_LoadStep_Async and PKR_updateLayerAssets read them.
    for (size_t l = 0; l < mod.layers.size(); l++)
    {
        const std::vector<S32>& as = mod.layers[l].assets;
        if (as.empty())
        {
            continue;
        }
        U32 laysize = iModPkgLayerSize(mod, (S32)l);
        const iModPkgTocAsset& first = mod.assets[as[0]];
        CHECK(first.off % 32 == 0, "%s: layer %d at %X", label, (int)l, first.off);
        std::vector<U8> layer = ReadView(pkg, fp, first.off, laysize);
        for (size_t k = 0; k < as.size(); k++)
        {
            const iModPkgTocAsset& a = mod.assets[as[k]];
            U32 rel = a.off - first.off;
            CHECK(a.off >= first.off && rel + a.size <= laysize, "%s: %08X outside its layer",
                  label, a.id);
            if (k + 1 < as.size())
            {
                CHECK(a.off + a.size + a.pad == mod.assets[as[k + 1]].off,
                      "%s: %08X pad %u does not reach the next asset", label, a.id, a.pad);
            }
            if (rel + a.size <= laysize)
            {
                std::map<U32, std::vector<U8> >::const_iterator e = expect.find(a.id);
                const iModPkgTocAsset& o = orig.assets[as[k]];
                std::vector<U8> want = e != expect.end() ? e->second : ReadRange(fp, o.off, o.size);
                CHECK(a.size == want.size() &&
                          (a.size == 0 || memcmp(&layer[rel], &want[0], a.size) == 0),
                      "%s: %08X wrong in its layer", label, a.id);
            }
        }
    }

    // Random reads across extents agree with one read of the whole view.
    {
        std::vector<U8> whole = ReadView(pkg, fp, 0, mod.fileSize);
        U32 seed = 12345;
        bool same = true;
        for (int i = 0; i < 2000 && same; i++)
        {
            seed = seed * 1103515245 + 12345;
            U32 off = (seed >> 8) % mod.fileSize;
            seed = seed * 1103515245 + 12345;
            U32 len = 1 + (seed >> 8) % 0x3000;
            std::vector<U8> part = ReadView(pkg, fp, off, len);
            for (U32 j = 0; j < len && same; j++)
            {
                U8 want = off + j < mod.fileSize ? whole[off + j] : 0;
                same = part[j] == want;
            }
        }
        CHECK(same, "%s: random reads", label);
    }

    printf("   %s: %d assets, %d layers, %d replaced, %d moved, %u -> %u bytes\n", label,
           (int)mod.assets.size(), (int)mod.layers.size(), (int)iModPkgReplacedCount(pkg),
           moved, orig.fileSize, mod.fileSize);
    fclose(fp);
}

static const iModPkgTocAsset* FindAsset(const iModPkgToc& toc, const char* name)
{
    for (size_t i = 0; i < toc.assets.size(); i++)
    {
        if (toc.assets[i].name == name)
        {
            return &toc.assets[i];
        }
    }
    return NULL;
}

static std::vector<U8> Pattern(size_t n, U8 seed)
{
    std::vector<U8> v(n);
    for (size_t i = 0; i < n; i++)
    {
        v[i] = (U8)(seed + i * 7);
    }
    return v;
}

static void TestPackages()
{
    printf("-- package views\n");
    std::string fontUS = sAssets + "/font_US.hip";
    std::string font = sAssets + "/font.HIP";
    std::string boot = sAssets + "/boot.HIP";

    iModPkgToc tocUS, tocFont, tocBoot;
    FILE* fp = fopen(fontUS.c_str(), "rb");
    CHECK(fp != NULL && iModPkgReadToc(fp, NULL, &tocUS), "%s", fontUS.c_str());
    if (fp)
        fclose(fp);
    fp = fopen(font.c_str(), "rb");
    CHECK(fp != NULL && iModPkgReadToc(fp, NULL, &tocFont), "%s", font.c_str());
    if (fp)
        fclose(fp);
    fp = fopen(boot.c_str(), "rb");
    CHECK(fp != NULL && iModPkgReadToc(fp, NULL, &tocBoot), "%s", boot.c_str());
    if (fp)
        fclose(fp);

    const iModPkgTocAsset* loading = FindAsset(tocUS, "startup_loading_text");
    const iModPkgTocAsset* cont = FindAsset(tocUS, "text_menu_continue");
    const iModPkgTocAsset* bkg = FindAsset(tocFont, "ui_loading_bkg");
    const iModPkgTocAsset* btn = FindAsset(tocFont, "pad_button1");
    const iModPkgTocAsset* snd = FindAsset(tocBoot, "genenemy_throw.WAV");
    CHECK(loading && cont && bkg && btn && snd, "the assets these tests use");
    if (!(loading && cont && bkg && btn && snd))
    {
        return;
    }

    // Nothing replaced: no view.
    {
        Replacements none;
        CHECK(iModPkgBuild(fontUS.c_str(), MapLookup, &none, NULL) == NULL, "empty view");
    }

    // In place: a TEXT no bigger than its slot.
    {
        Replacements r;
        r[loading->id].bytes = Pattern(loading->size > 4 ? loading->size - 4 : 1, 1);
        std::map<U32, std::vector<U8> > expect;
        expect[loading->id] = r[loading->id].bytes;
        std::vector<std::string> log;
        iModPkg* pkg = iModPkgBuild(fontUS.c_str(), MapLookup, &r, &log);
        CHECK(pkg != NULL && iModPkgMovedLayers(pkg) == 0 && iModPkgSize(pkg) == tocUS.fileSize,
              "in place");
        if (pkg)
        {
            VerifyView(fontUS, pkg, expect, "font_US in place");
            iModPkgRelease(pkg);
        }
    }

    // Too big for its slot: the layer moves to the end.
    {
        Replacements r;
        r[cont->id].bytes = Pattern(cont->size + cont->pad + 1000, 2);
        r[loading->id].bytes = Pattern(3, 3);
        std::map<U32, std::vector<U8> > expect;
        expect[cont->id] = r[cont->id].bytes;
        expect[loading->id] = r[loading->id].bytes;
        iModPkg* pkg = iModPkgBuild(fontUS.c_str(), MapLookup, &r, NULL);
        CHECK(pkg != NULL && iModPkgMovedLayers(pkg) == 1 && iModPkgSize(pkg) > tocUS.fileSize &&
                  iModPkgSize(pkg) % 0x800 == 0,
              "moved");
        if (pkg)
        {
            VerifyView(fontUS, pkg, expect, "font_US moved");
            iModPkgRelease(pkg);
        }
    }

    // Textures: one bigger (moves its layer), one smaller (in place, another layer).
    {
        Replacements r;
        r[bkg->id].bytes = Pattern(bkg->size * 2, 4);
        r[btn->id].bytes = Pattern(100, 5);
        std::map<U32, std::vector<U8> > expect;
        expect[bkg->id] = r[bkg->id].bytes;
        expect[btn->id] = r[btn->id].bytes;
        iModPkg* pkg = iModPkgBuild(font.c_str(), MapLookup, &r, NULL);
        CHECK(pkg != NULL && iModPkgReplacedCount(pkg) == 2, "font textures");
        if (pkg)
        {
            VerifyView(font, pkg, expect, "font textures");
            iModPkgRelease(pkg);
        }
    }

    // A sound, decoded PCM, twice its old size: the SRAM layer moves (2 KB
    // alignment kept) and the SNDI entry is rewritten.
    {
        Replacements r;
        iModPkgReplacement& s = r[snd->id];
        s.bytes = Pattern(snd->size * 3 + 6, 6);
        s.pcm = true;
        s.channels = 2;
        s.rate = 32000;
        iModPkg* pkg = iModPkgBuild(boot.c_str(), MapLookup, &r, NULL);
        CHECK(pkg != NULL, "sound view");
        if (pkg)
        {
            FILE* f = fopen(boot.c_str(), "rb");
            iModPkgToc mod;
            iModPkgReadToc(f, pkg, &mod);
            std::map<U32, std::vector<U8> > expect;
            expect[snd->id] = s.bytes;
            bool patched = false;
            for (size_t i = 0; i < mod.assets.size(); i++)
            {
                if (mod.assets[i].type != 0x534E4449) // 'SNDI'
                {
                    continue;
                }
                std::vector<U8> table = ReadView(pkg, f, mod.assets[i].off, mod.assets[i].size);
                std::vector<U8> before = ReadRange(f, tocBoot.assets[i].off, tocBoot.assets[i].size);
                for (size_t e = 12; e + 44 <= table.size(); e += 44)
                {
                    U32 aid;
                    memcpy(&aid, &table[e + 0x18], 4);
                    if (aid != snd->id)
                    {
                        CHECK(memcmp(&table[e], &before[e], 44) == 0, "other SNDI entry changed");
                        continue;
                    }
                    U16 tag, ch, block, bits;
                    U32 rate, avg, dsize;
                    memcpy(&tag, &table[e], 2);
                    memcpy(&ch, &table[e + 2], 2);
                    memcpy(&rate, &table[e + 4], 4);
                    memcpy(&avg, &table[e + 8], 4);
                    memcpy(&block, &table[e + 12], 2);
                    memcpy(&bits, &table[e + 14], 2);
                    memcpy(&dsize, &table[e + 0x14], 4);
                    CHECK(tag == 1 && ch == 2 && rate == 32000 && avg == 128000 && block == 4 &&
                              bits == 16 && dsize == s.bytes.size(),
                          "SNDI entry: tag %u ch %u rate %u avg %u block %u bits %u size %u", tag,
                          ch, rate, avg, block, bits, dsize);
                    patched = true;
                }
                expect[mod.assets[i].id] = table;
            }
            CHECK(patched, "no SNDI entry for the sound");
            const iModPkgTocAsset* moved = FindAsset(mod, "genenemy_throw.WAV");
            CHECK(moved != NULL && moved->off % 0x800 == 0, "sound alignment");
            fclose(f);
            VerifyView(boot, pkg, expect, "boot sound");
            iModPkgRelease(pkg);
        }
    }

    // Not a package.
    {
        Replacements r;
        r[1].bytes = Pattern(4, 7);
        CHECK(iModPkgBuild((sAssets + "/sb04.ini").c_str(), MapLookup, &r, NULL) == NULL,
              "an INI as a package");
    }
}

// --- the manager ---------------------------------------------------------------------

static void Setenv(const char* name, const char* value)
{
    iHostSetEnv(name, value);
}

static void TestManager()
{
    printf("-- the manager\n");
    std::string mods = sScratch + "/mods";
    MakeDirs(mods + "/ModA/assets/font_US");
    MakeDirs(mods + "/ModA/textures/ui");
    MakeDirs(mods + "/ModA/fmv");
    MakeDirs(mods + "/ModA/ini");
    MakeDirs(mods + "/ModA/files/FMV");
    MakeDirs(mods + "/ModB/assets");
    MakeDirs(mods + "/ModB/sounds");
    MakeDirs(mods + "/ModB/textures");
    MakeDirs(mods + "/ModB/ini");
    MakeDirs(mods + "/NotAMod/saves");
    iHostRemoveFile((mods + "/mods.ini").c_str());

    // What the end of a previous run added.
    const char* const kLater[] = { "ModC", "AModD" };
    for (int i = 0; i < 2; i++)
    {
        std::string d = mods + "/" + kLater[i];
        iHostRemoveFile((d + "/mod.ini").c_str());
        iHostRemoveFile((d + "/ini/water.ini").c_str());
        iHostRemoveDir((d + "/ini").c_str());
        iHostRemoveDir(d.c_str());
    }

    WriteText(mods + "/ModA/mod.ini", "[mod]\nname = Mod A\nauthor = test\nversion = 1.2\n"
                                      "description = the first\npriority = 0\n");
    WriteText(mods + "/ModA/assets/font_US/startup_loading_text.txt", "Hello from A\n");
    WriteText(mods + "/ModA/textures/ui/ui_loading_bkg.png", "not decoded here");
    WriteText(mods + "/ModA/textures/readme.txt", "ignored, not an image");
    WriteText(mods + "/ModA/fmv/HILogo.webm", "a movie");
    WriteText(mods + "/ModA/files/FMV/LogoTHQ.bik", "a movie");
    WriteText(mods + "/ModA/files/water.ini", "loose");
    WriteText(mods + "/ModA/ini/sb04.ini", "G.TakeDamage = 0\nModdedBy = A\n");

    WriteText(mods + "/ModB/mod.ini", "name = Mod B\n");
    U8 textB[] = { 1, 0, 0, 0, 'B', 0, 0, 0 };
    WriteFile(mods + "/ModB/assets/20D7D45E.bin", textB, sizeof(textB)); // startup_loading_text
    std::vector<U8> wav = MakeWav(1, 1, 16000, 16, 1600);
    WriteFile(mods + "/ModB/sounds/genenemy_throw.wav", &wav[0], wav.size());
    WriteText(mods + "/ModB/textures/UI_LOADING_BKG.dds", "not decoded here");
    WriteText(mods + "/ModB/ini/SB04.INI", "ModdedBy = B\n");

    Setenv("TSSM_MODS_DIR", mods.c_str());
    Setenv("TSSM_MODS", NULL);
    iModInit();

    CHECK(iModCount() == 2, "%d mods (NotAMod is not one)", (int)iModCount());
    S32 a = iModFind("moda");
    S32 b = iModFind("ModB");
    CHECK(a == 0 && b == 1, "order A, B: %d %d", (int)a, (int)b);
    if (a != 0 || b != 1)
    {
        return;
    }
    const iModInfo* ia = iModGet(a);
    CHECK(strcmp(ia->name, "Mod A") == 0 && strcmp(ia->author, "test") == 0 &&
              strcmp(ia->version, "1.2") == 0 && ia->enabled,
          "mod.ini");
    CHECK(strcmp(iModGet(b)->name, "Mod B") == 0, "mod.ini without a section");
    CHECK(ia->counts[IMOD_KIND_ASSET] == 1 && ia->counts[IMOD_KIND_TEXTURE] == 1 &&
              ia->counts[IMOD_KIND_MOVIE] == 1 && ia->counts[IMOD_KIND_FILE] == 2 &&
              ia->counts[IMOD_KIND_INI] == 1,
          "A's counts %d %d %d %d %d", ia->counts[IMOD_KIND_ASSET],
          ia->counts[IMOD_KIND_TEXTURE], ia->counts[IMOD_KIND_MOVIE], ia->counts[IMOD_KIND_FILE],
          ia->counts[IMOD_KIND_INI]);
    CHECK(ia->problem[0] != '\0', "readme.txt in textures/ is reported");

    // The texture both replace: B, later, wins, and it is a conflict.
    const char* tex = iModFindTexture("UI_Loading_Bkg");
    CHECK(tex != NULL && strstr(tex, "ModB") != NULL, "texture winner %s", tex ? tex : "none");
    bool texConflict = false;
    for (S32 i = 0; i < iModConflictCount(); i++)
    {
        const iModConflict* c = iModGetConflict(i);
        texConflict = texConflict ||
                      (c->kind == IMOD_KIND_TEXTURE && c->winner == b && c->loser == a);
    }
    CHECK(texConflict, "texture conflict listed");

    char path[512];
    CHECK(iModResolveFile("WATER.ini", path, sizeof(path)) && strstr(path, "ModA") != NULL,
          "loose file");
    CHECK(iModResolveFile("./water.ini", path, sizeof(path)), "./ prefix");
    CHECK(!iModResolveFile("explode.ini", path, sizeof(path)), "not replaced");
    CHECK(iModFindMovie("FMV\\HILogo.bik", path, sizeof(path)) && strstr(path, ".webm") != NULL,
          "movie from fmv/");
    CHECK(iModFindMovie("FMV\\logothq.bik", path, sizeof(path)) && strstr(path, "files") != NULL,
          "movie from files/FMV/");
    CHECK(!iModFindMovie("FMV\\Intro.bik", path, sizeof(path)), "movie not replaced");

    // INI overlays stack in load order.
    {
        std::string ini = "PATH=.\nG.TakeDamage = 1\nModdedBy = nobody\n";
        U32 size = 0;
        char* merged = (char*)iModApplyIniOverlays("SB04.INI", ini.data(), (U32)ini.size(), &size);
        CHECK(merged != NULL && size % 32 == 0, "overlay applied");
        if (merged)
        {
            std::string m(merged);
            CHECK(m == "PATH=.\nG.TakeDamage = 0\nModdedBy = B\n", "overlays:\n%s", m.c_str());
            free(merged);
        }
        CHECK(iModApplyIniOverlays("explode.ini", ini.data(), (U32)ini.size(), &size) == NULL,
              "no overlay for explode.ini");
    }

    // The package: B's startup_loading_text (by ID, any package) beats A's (by
    // name, font_US only) because B is later; the sound is decoded and its
    // SNDI entry rewritten.
    if (!sAssets.empty())
    {
        std::string fontUS = sAssets + "/font_US.hip";
        iModPkg* pkg = iModOpenPackage(fontUS.c_str());
        CHECK(pkg != NULL, "font_US view");
        std::map<U32, std::vector<U8> > expect;
        expect[0x20D7D45E] = std::vector<U8>(textB, textB + sizeof(textB));
        if (pkg)
        {
            VerifyView(fontUS, pkg, expect, "manager: B wins");
            iModPkgRelease(pkg);
        }
        bool assetConflict = false;
        for (S32 i = 0; i < iModConflictCount(); i++)
        {
            const iModConflict* c = iModGetConflict(i);
            assetConflict = assetConflict || (c->kind == IMOD_KIND_ASSET && c->winner == b);
        }
        CHECK(assetConflict, "ID/name conflict found when the package opened");

        // The same package again comes from the cache: the same view.
        iModPkg* again = iModOpenPackage(fontUS.c_str());
        iModPkg* third = iModOpenPackage(fontUS.c_str());
        CHECK(again != NULL && again == third, "cached view");
        iModPkgRelease(again);
        iModPkgRelease(third);

        // font.HIP has nothing replaced (A's text is scoped to font_US).
        CHECK(iModOpenPackage((sAssets + "/font.HIP").c_str()) == NULL, "scoped override");

        iModPkg* bootPkg = iModOpenPackage((sAssets + "/boot.HIP").c_str());
        CHECK(bootPkg != NULL, "boot view (sound)");
        iModPkgRelease(bootPkg);

        // Swap the order: A wins now, with its text encoded as a TEXT asset.
        CHECK(iModMove(1, 0), "move");
        CHECK(iModFind("ModB") == 0 && iModRestartAdvised(), "moved, restart advised");
        pkg = iModOpenPackage(fontUS.c_str());
        std::string hello = "Hello from A";
        std::vector<U8> textA(4 + ((hello.size() + 1 + 3) & ~3u), 0);
        textA[0] = (U8)hello.size();
        memcpy(&textA[4], hello.data(), hello.size());
        expect[0x20D7D45E] = textA;
        CHECK(pkg != NULL, "font_US view after the move");
        if (pkg)
        {
            VerifyView(fontUS, pkg, expect, "manager: A wins after the move");
            iModPkgRelease(pkg);
        }

        // A off: B's again.
        CHECK(iModSetEnabled(iModFind("ModA"), 0), "disable");
        pkg = iModOpenPackage(fontUS.c_str());
        expect[0x20D7D45E] = std::vector<U8>(textB, textB + sizeof(textB));
        if (pkg)
        {
            VerifyView(fontUS, pkg, expect, "manager: A off");
            iModPkgRelease(pkg);
        }
        CHECK(pkg != NULL, "font_US view with A off");
    }
    else
    {
        CHECK(iModMove(1, 0), "move");
        CHECK(iModSetEnabled(iModFind("ModA"), 0), "disable");
    }

    // Saved and read back: B first, A off.
    CHECK(iModSaveList(), "save");
    iModRescan();
    CHECK(iModCount() == 2 && iModFind("ModB") == 0 && iModFind("ModA") == 1 &&
              !iModGet(1)->enabled && iModGet(0)->enabled,
          "mods.ini read back");

    // A new mod not in mods.ini goes last; a higher priority goes later still.
    MakeDirs(mods + "/ModC/ini");
    WriteText(mods + "/ModC/mod.ini", "[mod]\npriority = 5\n");
    WriteText(mods + "/ModC/ini/water.ini", "x = 1\n");
    MakeDirs(mods + "/AModD/ini");
    WriteText(mods + "/AModD/ini/water.ini", "x = 2\n");
    iModRescan();
    CHECK(iModCount() == 4 && iModFind("AModD") == 2 && iModFind("ModC") == 3,
          "new mods: D at %d, C at %d", (int)iModFind("AModD"), (int)iModFind("ModC"));

    // The global switch.
    Setenv("TSSM_MODS", "0");
    iModInit();
    CHECK(!iModsEnabled() && iModFindTexture("ui_loading_bkg") == NULL &&
              !iModResolveFile("water.ini", path, sizeof(path)),
          "mods off");
    if (!sAssets.empty())
    {
        CHECK(iModOpenPackage((sAssets + "/font_US.hip").c_str()) == NULL, "mods off: package");
    }
    Setenv("TSSM_MODS", NULL);

    // No mods at all: an empty folder.
    std::string empty = sScratch + "/nomods";
    MakeDirs(empty);
    Setenv("TSSM_MODS_DIR", empty.c_str());
    iModInit();
    U32 size;
    CHECK(iModCount() == 0 && iModConflictCount() == 0 &&
              !iModResolveFile("boot.HIP", path, sizeof(path)) &&
              iModApplyIniOverlays("sb04.ini", "A=1\n", 4, &size) == NULL &&
              iModFindTexture("ui_loading_bkg") == NULL,
          "no mods");
    if (!sAssets.empty())
    {
        CHECK(iModOpenPackage((sAssets + "/boot.HIP").c_str()) == NULL, "no mods: package");
    }

    // A folder that does not exist.
    Setenv("TSSM_MODS_DIR", (sScratch + "/does-not-exist").c_str());
    iModInit();
    CHECK(iModCount() == 0 && iModFolder()[0] == '\0', "missing folder");
    Setenv("TSSM_MODS_DIR", NULL);
}

int main(int argc, char** argv)
{
    if (argc > 1)
    {
        sAssets = argv[1];
    }
    else if (getenv("TSSM_ASSETS") != NULL)
    {
        sAssets = getenv("TSSM_ASSETS");
    }
    sScratch = argc > 2 ? argv[2] : "modtest-scratch";
    for (size_t i = 0; i < sAssets.size(); i++)
    {
        sAssets[i] = sAssets[i] == '\\' ? '/' : sAssets[i];
    }
    for (size_t i = 0; i < sScratch.size(); i++)
    {
        sScratch[i] = sScratch[i] == '\\' ? '/' : sScratch[i];
    }
    if (!sAssets.empty() && !iHostPathExists((sAssets + "/boot.HIP").c_str()))
    {
        printf("no boot.HIP in %s: the package tests are skipped\n", sAssets.c_str());
        sAssets.clear();
    }
    MakeDirs(sScratch);

    TestIni();
    TestAudio();
    if (!sAssets.empty())
    {
        TestPackages();
    }
    TestManager();

    printf("%d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
