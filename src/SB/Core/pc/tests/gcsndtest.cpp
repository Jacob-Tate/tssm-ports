// tssm_gcsndtest: the GameCube sound data (iSndGC.h, iSndDsp.h) over every
// package of a GameCube disc, without the game.
//
//     tssm_gcsndtest <GameCube files> [<Xbox files>] [options]
//
//       --fuzz N      N damaged copies of each sound table (default 0)
//       --seed S      the damage's random seed
//       --pkg TEXT    only packages whose path contains TEXT
//       --show N      list up to N examples of each kind of finding (8)
//       --dump DIR    write each cutscene track, decoded, as DIR/<id>.wav
//
// Every SNDI asset is read with iSndGCReadTable and every sound decoded with
// iSndGCDecode from a copy of the asset that ends where an inaccessible page
// begins, so a read past the asset stops the test where it happens. Every
// cutscene track is put together from its cutscene's SOUND records, as
// iCutscene.cpp feeds it, and decoded with the history carried from chunk to
// chunk; the jump at each chunk's seam is measured against the track's own.
//
// With an Xbox folder each sound is compared with the Xbox sound of the same
// id in the package of the same path, decoded with iSndXbox: its length, and
// how well the loudness over time (10 ms windows, either 100 ms ahead or
// behind) agrees. A cutscene track is compared with the Xbox cutscene of the
// same id's left track. The two releases were encoded separately (the
// GameCube's at 32 kHz or 22,050 Hz, the Xbox's mostly at 44.1 or 48 kHz), so
// the samples cannot match; the envelope can.
//
// With --fuzz each table is damaged in the bytes the reader parses -- its
// header, its tables at the end, each bank's headers -- and cut short at
// every boundary, and read again; every sound it then describes must lie
// inside the asset and decode without touching anything outside it.
//
// Exit code 0 when every table reads, every sound and track decodes, and no
// damaged copy was read outside its bytes.

#include "iGCPackage.h"
#include "iModPackage.h"
#include "iSndDsp.h"
#include "iSndGC.h"
#include "iSndXbox.h"

#include <math.h>
#include <stdarg.h>
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

    const U32 kSNDI = 0x534E4449;
    const U32 kSND = 0x534E4420;
    const U32 kSNDS = 0x534E4453;
    const U32 kCSN = 0x43534E20;

    S32 sShow = 8;
    S32 sFailures;

    void Fail(const char* fmt, ...)
    {
        sFailures++;
        va_list ap;
        va_start(ap, fmt);
        fprintf(stdout, "FAIL: ");
        vfprintf(stdout, fmt, ap);
        fprintf(stdout, "\n");
        va_end(ap);
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

        // `n` bytes placed against the guard page.
        U8* Place(const U8* src, U32 n)
        {
            if (!Reserve(n != 0 ? n : 1))
            {
                fprintf(stderr, "gcsndtest: out of memory for %u bytes\n", n);
                exit(2);
            }
            U8* dst = base + cap - n;
            if (n != 0)
            {
                memcpy(dst, src, n);
            }
            return dst;
        }
    };

    Guarded sBuf;

    // The reader iSndGCReadTable is given: the asset in memory. A request
    // outside it is a fault of the reader's caller.
    struct MemReader
    {
        const U8* p;
        U32 size;
        S32 outside;
    };

    U32 ReadMem(void* ctx, U32 offset, void* buf, U32 n)
    {
        MemReader* m = (MemReader*)ctx;
        if (offset > m->size || n > m->size - offset)
        {
            m->outside++;
            return 0;
        }
        memcpy(buf, m->p + offset, n);
        return n;
    }

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
        bool ok = iModPkgReadToc(fp, NULL, &pkg->toc);
        fseek(fp, 0, SEEK_END);
        long len = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        ok = ok && len > 0;
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

    const iModPkgTocAsset* Find(const Package& pkg, U32 id, U32 type)
    {
        auto it = pkg.byId.find(id);
        if (it == pkg.byId.end())
        {
            return NULL;
        }
        const iModPkgTocAsset& a = pkg.toc.assets[(size_t)it->second];
        return (type == 0 || a.type == type) ? &a : NULL;
    }

    const U8* Bytes(const Package& pkg, const iModPkgTocAsset& a)
    {
        if (a.off > pkg.bytes.size() || a.size > pkg.bytes.size() - a.off)
        {
            return NULL;
        }
        return pkg.bytes.data() + a.off;
    }

    std::string NameOf(const Package& pkg, U32 id)
    {
        const iModPkgTocAsset* a = Find(pkg, id, 0);
        return a != NULL ? a->name : std::string();
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

    // --- signals ------------------------------------------------------------------------

    // Loudness in 10 ms windows, of the channels mixed.
    std::vector<F32> Envelope(const S16* pcm, U32 frames, U32 channels, U32 rate)
    {
        std::vector<F32> env;
        const U32 win = rate / 100 != 0 ? rate / 100 : 1;
        for (U32 start = 0; start + win <= frames; start += win)
        {
            double sum = 0.0;
            for (U32 i = start; i < start + win; i++)
            {
                double m = 0.0;
                for (U32 c = 0; c < channels; c++)
                {
                    m += pcm[(size_t)i * channels + c];
                }
                m /= channels;
                sum += m * m;
            }
            env.push_back((F32)sqrt(sum / win));
        }
        return env;
    }

    double Pearson(const F32* a, const F32* b, size_t n)
    {
        if (n < 4)
        {
            return 0.0;
        }
        double ma = 0.0, mb = 0.0;
        for (size_t i = 0; i < n; i++)
        {
            ma += a[i];
            mb += b[i];
        }
        ma /= n;
        mb /= n;
        double sab = 0.0, sa = 0.0, sb = 0.0;
        for (size_t i = 0; i < n; i++)
        {
            sab += (a[i] - ma) * (b[i] - mb);
            sa += (a[i] - ma) * (a[i] - ma);
            sb += (b[i] - mb) * (b[i] - mb);
        }
        if (sa <= 0.0 || sb <= 0.0)
        {
            // Both flat (silence, a constant): alike if both are.
            return (sa <= 0.0 && sb <= 0.0) ? 1.0 : 0.0;
        }
        return sab / sqrt(sa * sb);
    }

    // The best agreement of two envelopes with one of them up to 100 ms ahead
    // or behind; *lag gets the shift, in windows.
    double EnvelopeMatch(const std::vector<F32>& a, const std::vector<F32>& b, S32* lag)
    {
        double best = -2.0;
        *lag = 0;
        for (S32 l = -10; l <= 10; l++)
        {
            size_t ia = l > 0 ? (size_t)l : 0;
            size_t ib = l < 0 ? (size_t)-l : 0;
            if (ia >= a.size() || ib >= b.size())
            {
                continue;
            }
            size_t n = std::min(a.size() - ia, b.size() - ib);
            double r = Pearson(a.data() + ia, b.data() + ib, n);
            if (r > best)
            {
                best = r;
                *lag = l;
            }
        }
        return best;
    }

    double Correlation(const S16* pcm, U32 frames, U32 channels, U32 c0, U32 c1)
    {
        std::vector<F32> a(frames), b(frames);
        for (U32 i = 0; i < frames; i++)
        {
            a[i] = pcm[(size_t)i * channels + c0];
            b[i] = pcm[(size_t)i * channels + c1];
        }
        return Pearson(a.data(), b.data(), frames);
    }

    // --- the Xbox side ------------------------------------------------------------------

    struct XboxSound
    {
        std::vector<S16> pcm;
        U32 channels = 0;
        U32 rate = 0;
    };

    bool XboxDecode(const Package& x, const iSndXboxEntry& e, XboxSound* out)
    {
        const iModPkgTocAsset* a = Find(x, e.assetID, 0);
        if (a == NULL || (a->type != kSND && a->type != kSNDS))
        {
            return false;
        }
        const U8* p = Bytes(x, *a);
        if (p == NULL)
        {
            return false;
        }
        U32 n = 0;
        void* pcm = iSndXboxDecode(p, a->size, e.format_tag, e.channels, e.block_align,
                                   e.bits_per_sample, &n);
        if (pcm == NULL)
        {
            return false;
        }
        out->channels = e.channels != 0 ? e.channels : 1;
        out->rate = e.samples_per_sec;
        out->pcm.assign((const S16*)pcm, (const S16*)pcm + n / 2);
        free(pcm);
        return true;
    }

    // --- cutscenes ----------------------------------------------------------------------

    struct Chunk
    {
        U32 off; // of the chunk, in the CSN asset
        U32 size;
    };

    // The time chunks of a cutscene asset, as iCutscene.cpp reads them: the
    // header, the data chunks rounded to 2048 from the next 2048, then the time
    // chunks back to back. `be` for the GameCube's byte order.
    bool CsnChunks(const U8* p, U32 size, bool be, std::vector<Chunk>* out, U32* left, U32* numData)
    {
        auto rd = [&](U32 o) { return be ? RdBE(p + o) : RdLE(p + o); };
        const U32 kInfo = 0x830;
        if (size < kInfo)
        {
            return false;
        }
        const U32 nd = rd(0x8);
        const U32 nt = rd(0xC);
        const U32 header = rd(0x1C);
        if (nd > 4096 || nt > 4096 || (U64)kInfo + nd * 16ull + (nt + 1) * 4ull > size)
        {
            return false;
        }
        *left = rd(0x30); // AudioTracks[0] (US).uLeftSoundId
        *numData = nd;
        U64 pos = (header + 2047ull) / 2048 * 2048;
        for (U32 i = 0; i < nd; i++)
        {
            U32 len = rd(kInfo + i * 16 + 8);
            if (len != 0)
            {
                pos += (len + 2047ull) / 2048 * 2048;
            }
        }
        const U32 offs = kInfo + nd * 16;
        for (U32 i = 0; i < nt; i++)
        {
            U32 from = rd(offs + i * 4);
            U32 to = rd(offs + i * 4 + 4);
            if (to < from || pos + (to - from) > size)
            {
                return false;
            }
            out->push_back({ (U32)pos, to - from });
            pos += to - from;
        }
        return true;
    }

    // A track's bytes in one chunk: the SOUND record's payload from its first
    // 32-byte boundary (iSndXboxCsnAudio's rule, on the GameCube's order).
    const U8* ChunkAudio(const U8* chunk, U32 bytes, U32 id, bool be, U32* size)
    {
        auto rd = [&](U32 o) { return be ? RdBE(chunk + o) : RdLE(chunk + o); };
        *size = 0;
        if (bytes < 16)
        {
            return NULL;
        }
        U32 n = rd(8);
        U32 p = 16;
        for (U32 i = 0; i < n; i++)
        {
            if (p + 16 > bytes)
            {
                return NULL;
            }
            U32 type = rd(p) & 0x0FFFFFFF;
            U32 aid = rd(p + 4);
            U32 len = rd(p + 8);
            U32 payload = p + 16;
            if (len > bytes - payload)
            {
                return NULL;
            }
            if (type == 5 && aid == id)
            {
                U32 start = payload;
                while ((start & 0x1F) != 0 && len >= 16)
                {
                    start += 16;
                    len -= 16;
                }
                *size = len;
                return chunk + start;
            }
            p = payload + ((len + 15) & ~15U);
        }
        return NULL;
    }

    // --- the run --------------------------------------------------------------------------

    struct Totals
    {
        S32 tables = 0, tablesFailed = 0;
        S32 sounds = 0, decoded = 0, streams = 0, stereo = 0;
        U64 dspBytes = 0;
        S32 paired = 0, noTwin = 0;
        S32 env95 = 0, env90 = 0, env80 = 0, envLow = 0;
        S32 lenClose = 0, lenFar = 0;
        S32 tracks = 0, tracksDecoded = 0, tracksPaired = 0, trackEnv90 = 0;
        S32 seams = 0, seamJumps = 0;
        S32 fuzzCopies = 0, fuzzRefused = 0, fuzzOutside = 0;
        std::map<std::string, S32> rates;
    };

    Totals T;
    std::vector<std::string> sLowEnv, sFarLen, sStereoNotes, sTrackNotes;

    void Note(std::vector<std::string>& v, const char* fmt, ...)
    {
        char b[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(b, sizeof(b), fmt, ap);
        va_end(ap);
        v.push_back(b);
    }

    U32 sSeed = 12345;
    U32 Rand()
    {
        sSeed = sSeed * 1103515245u + 12345u;
        return sSeed >> 8;
    }

    // Every sound a table describes must lie inside the asset; decoding one
    // reads only that. A damaged table's sounds are decoded at their ends only
    // (the first and last blocks), which is where a wrong size would show.
    bool CheckTable(const iSndGCTable& t, const U8* asset, U32 size, bool whole)
    {
        for (U32 i = 0; i < t.numSounds; i++)
        {
            const iSndGCSound& s = t.sounds[i];
            if (s.offset > size || s.size > size - s.offset ||
                iSndDspBytesFor(s.samples, s.channels) > s.size || s.channels == 0 ||
                s.channels > ISNDDSP_MAX_CHANNELS)
            {
                return false;
            }
            if (s.samples == 0)
            {
                continue;
            }
            if (whole)
            {
                U32 n = 0;
                void* pcm = iSndGCDecode(&s, asset + s.offset, s.size, &n);
                if (pcm == NULL)
                {
                    return false;
                }
                free(pcm);
            }
            else
            {
                const U32 block = ISNDDSP_FRAME_BYTES * s.channels;
                const U32 blocks = s.size / block;
                const U32 n = blocks < 16 ? blocks : 16;
                static S16 out[16 * ISNDDSP_FRAME_SAMPLES * ISNDDSP_MAX_CHANNELS];
                iSndDspState st;
                iSndDspReset(&st, s.ch, s.channels, false);
                iSndDspDecode(asset + s.offset, n * block, s.channels, s.ch, &st, out, n);
                iSndDspDecode(asset + s.offset + (blocks - n) * block, n * block, s.channels, s.ch,
                              &st, out, n);
            }
        }
        return true;
    }

    void Fuzz(const U8* asset, U32 size, const iSndGCTable& good, S32 copies, const std::string& label)
    {
        // The bytes the reader parses: the header, the tables at the end, and
        // each bank's header (before its first sample).
        std::vector<std::pair<U32, U32>> regions;
        regions.push_back({ 0, 0x20 });
        const U32 sum = RdBE(asset + 4);
        const U32 tail = 0x20 + sum;
        regions.push_back({ tail, size - tail });
        U32 lowest = size;
        for (U32 i = 0; i < good.numSounds; i++)
        {
            lowest = std::min(lowest, good.sounds[i].offset);
        }
        const U32 nb = asset[0x1E];
        for (U32 b = 0; b < nb && tail + 4 * b + 4 <= size; b++)
        {
            U32 at = 0x20 + RdBE(asset + tail + 4 * b);
            if (at + 24 <= size)
            {
                U32 hdr = 24 + RdLE(asset + at + 8);
                regions.push_back({ at, std::min(hdr, size - at) });
            }
        }

        // The copy is placed against the guard page once; damage is undone
        // after each try.
        U8* copy = sBuf.Place(asset, size);

        // Cut short at each boundary, and a little either side.
        std::vector<U32> cuts = { 0, 1, 0x1F, 0x20, 0x21, tail, tail + 1, size - 1 };
        for (auto& r : regions)
        {
            cuts.push_back(r.first);
            cuts.push_back(r.first + r.second / 2);
            cuts.push_back(r.first + r.second);
        }
        for (U32 cut : cuts)
        {
            if (cut > size)
            {
                continue;
            }
            // The reader refuses anything past the cut.
            MemReader m = { asset, cut, 0 };
            iSndGCTable t;
            char why[256];
            T.fuzzCopies++;
            if (iSndGCReadTable(ReadMem, &m, cut, &t, why, sizeof(why)))
            {
                if (!CheckTable(t, asset, cut, false))
                {
                    T.fuzzOutside++;
                    Fail("%s cut to %u bytes: a sound outside the asset", label.c_str(), cut);
                }
                iSndGCFreeTable(&t);
            }
            else
            {
                T.fuzzRefused++;
            }
            if (m.outside != 0)
            {
                T.fuzzOutside++;
                Fail("%s cut to %u bytes: read outside the asset", label.c_str(), cut);
            }
        }

        for (S32 k = 0; k < copies; k++)
        {
            struct Saved
            {
                U32 at;
                U8 b;
            };
            std::vector<Saved> saved;
            S32 hits = 1 + (S32)(Rand() % 4);
            for (S32 h = 0; h < hits; h++)
            {
                const auto& r = regions[Rand() % regions.size()];
                if (r.second == 0)
                {
                    continue;
                }
                U32 at = r.first + Rand() % r.second;
                U32 kind = Rand() % 4;
                U32 width = kind == 0 ? 1 : 4;
                for (U32 w = 0; w < width && at + w < size; w++)
                {
                    saved.push_back({ at + w, copy[at + w] });
                }
                if (kind == 0)
                {
                    copy[at] ^= (U8)(1 + Rand() % 255);
                }
                else
                {
                    static const U32 kWords[] = { 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x00010000, 0 };
                    U32 v = kind == 1 ? kWords[Rand() % 5] : Rand();
                    for (U32 w = 0; w < 4 && at + w < size; w++)
                    {
                        copy[at + w] = (U8)(v >> (8 * (3 - w)));
                    }
                }
            }

            MemReader m = { copy, size, 0 };
            iSndGCTable t;
            char why[256];
            T.fuzzCopies++;
            if (iSndGCReadTable(ReadMem, &m, size, &t, why, sizeof(why)))
            {
                if (!CheckTable(t, copy, size, false))
                {
                    T.fuzzOutside++;
                    Fail("%s damaged copy %d: a sound outside the asset", label.c_str(), k);
                }
                iSndGCFreeTable(&t);
            }
            else
            {
                T.fuzzRefused++;
            }
            if (m.outside != 0)
            {
                T.fuzzOutside++;
                Fail("%s damaged copy %d: read outside the asset", label.c_str(), k);
            }
            for (size_t i = saved.size(); i-- > 0;)
            {
                copy[saved[i].at] = saved[i].b;
            }
        }
    }

    void WriteWav(const std::string& dir, U32 id, const std::vector<S16>& pcm, U32 rate)
    {
        if (dir.empty())
        {
            return;
        }
        char path[1024];
        snprintf(path, sizeof(path), "%s/%08x.wav", dir.c_str(), id);
        iSndXboxWriteWav(path, pcm.data(), (U32)pcm.size(), 1, rate);
    }

    void Cutscenes(const std::string& rel, const Package& g, const Package* x,
                   const std::vector<iSndGCTable>& tables, const std::string& dumpDir)
    {
        for (const iModPkgTocAsset& a : g.toc.assets)
        {
            if (a.type != kCSN)
            {
                continue;
            }
            const U8* p = Bytes(g, a);
            std::vector<Chunk> chunks;
            U32 left = 0, nd = 0;
            if (p == NULL || !CsnChunks(p, a.size, true, &chunks, &left, &nd))
            {
                Fail("%s: cutscene %08x '%s' cannot be walked", rel.c_str(), a.id, a.name.c_str());
                continue;
            }

            const iSndGCCutscene* track = NULL;
            for (const iSndGCTable& t : tables)
            {
                for (U32 j = 0; j < t.numCutscene; j++)
                {
                    if (t.cutscenes[j].assetID == left)
                    {
                        track = &t.cutscenes[j];
                    }
                }
            }
            if (track == NULL)
            {
                continue;
            }
            T.tracks++;

            // The track, chunk by chunk, with the history carried across.
            std::vector<S16> pcm;
            std::vector<size_t> seams;
            iSndDspState st;
            iSndDspReset(&st, &track->ch, 1, false);
            for (const Chunk& c : chunks)
            {
                U32 n = 0;
                const U8* au = ChunkAudio(p + c.off, c.size, left, true, &n);
                if (au == NULL || n == 0)
                {
                    continue;
                }
                const U32 blocks = n / ISNDDSP_FRAME_BYTES;
                size_t at = pcm.size();
                if (at != 0)
                {
                    seams.push_back(at);
                }
                pcm.resize(at + (size_t)blocks * ISNDDSP_FRAME_SAMPLES);
                iSndDspDecode(au, n, 1, &track->ch, &st, pcm.data() + at, blocks);
            }
            if (pcm.size() > track->samples)
            {
                pcm.resize(track->samples);
            }
            if (pcm.empty())
            {
                Fail("%s: cutscene %08x '%s' has no audio for track %08x", rel.c_str(), a.id,
                     a.name.c_str(), left);
                continue;
            }
            T.tracksDecoded++;
            WriteWav(dumpDir, left, pcm, track->rate);

            // A seam where the history was wrong would jump: count the seams
            // whose step is past the track's own 99.9th percentile step.
            std::vector<S32> steps;
            for (size_t i = 1; i < pcm.size(); i++)
            {
                steps.push_back(abs(pcm[i] - pcm[i - 1]));
            }
            std::vector<S32> sorted = steps;
            std::sort(sorted.begin(), sorted.end());
            const S32 p999 = sorted.empty() ? 0 : sorted[sorted.size() * 999 / 1000];
            S32 jumps = 0;
            for (size_t s : seams)
            {
                if (s < pcm.size() && abs(pcm[s] - pcm[s - 1]) > p999)
                {
                    jumps++;
                }
            }
            T.seams += (S32)seams.size();
            T.seamJumps += jumps;

            std::string vs = "no Xbox twin";
            if (x != NULL)
            {
                const iModPkgTocAsset* xa = Find(*x, a.id, kCSN);
                const U8* xp = xa != NULL ? Bytes(*x, *xa) : NULL;
                std::vector<Chunk> xc;
                U32 xl = 0, xnd = 0;
                if (xp != NULL && CsnChunks(xp, xa->size, false, &xc, &xl, &xnd))
                {
                    std::vector<S16> xpcm;
                    for (const Chunk& c : xc)
                    {
                        U32 n = 0;
                        const U8* au = ChunkAudio(xp + c.off, c.size, xl, false, &n);
                        if (au != NULL)
                        {
                            xpcm.insert(xpcm.end(), (const S16*)au, (const S16*)au + n / 2);
                        }
                    }
                    // The Xbox track's rate and channels are its SNDI entry's.
                    U32 xrate = 0;
                    U32 xch = 1;
                    for (const iModPkgTocAsset& s : x->toc.assets)
                    {
                        if (s.type != kSNDI)
                        {
                            continue;
                        }
                        U32 cnt = 0;
                        const iSndXboxEntry* e = iSndXboxTableEntries(Bytes(*x, s), s.size, &cnt);
                        for (U32 k = 0; e != NULL && k < cnt; k++)
                        {
                            if (e[k].assetID == xl)
                            {
                                xrate = e[k].samples_per_sec;
                                xch = e[k].channels != 0 ? e[k].channels : 1;
                            }
                        }
                    }
                    if (!xpcm.empty() && xrate != 0)
                    {
                        S32 lag = 0;
                        double r = EnvelopeMatch(Envelope(pcm.data(), (U32)pcm.size(), 1, track->rate),
                                                 Envelope(xpcm.data(), (U32)(xpcm.size() / xch), xch, xrate),
                                                 &lag);
                        T.tracksPaired++;
                        T.trackEnv90 += r >= 0.9;
                        char b[128];
                        snprintf(b, sizeof(b), "Xbox %u Hz %u ch %.2f s, envelope %.3f (lag %d ms)", xrate,
                                 xch, (double)(xpcm.size() / xch) / xrate, r, lag * 10);
                        vs = b;
                    }
                }
            }
            Note(sTrackNotes, "%-12s %-28s %08x %u Hz %6.2f s (header %6.2f s), %zu seams, %d past the 99.9%% step; %s",
                 rel.c_str(), a.name.c_str(), left, track->rate, (double)pcm.size() / track->rate,
                 (double)track->samples / track->rate, seams.size(), jumps, vs.c_str());
        }
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: tssm_gcsndtest <GameCube files> [<Xbox files>] [--fuzz N] "
                        "[--seed S] [--pkg TEXT] [--show N] [--dump DIR]\n");
        return 2;
    }

    fs::path gcRoot = argv[1];
    fs::path xbRoot;
    S32 fuzz = 0;
    std::string only, dumpDir;
    for (int i = 2; i < argc; i++)
    {
        std::string a = argv[i];
        if (a == "--fuzz" && i + 1 < argc)
        {
            fuzz = atoi(argv[++i]);
        }
        else if (a == "--seed" && i + 1 < argc)
        {
            sSeed = (U32)strtoul(argv[++i], NULL, 0);
        }
        else if (a == "--pkg" && i + 1 < argc)
        {
            only = argv[++i];
        }
        else if (a == "--show" && i + 1 < argc)
        {
            sShow = atoi(argv[++i]);
        }
        else if (a == "--dump" && i + 1 < argc)
        {
            dumpDir = argv[++i];
        }
        else if (a.size() > 0 && a[0] != '-' && xbRoot.empty())
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
        fprintf(stderr, "gcsndtest: no packages under %s\n", gcRoot.string().c_str());
        return 2;
    }

    for (const auto& kv : gcFiles)
    {
        const std::string& rel = kv.first;
        if (!only.empty() && rel.find(only) == std::string::npos)
        {
            continue;
        }

        Package g;
        if (!LoadPackage(kv.second, &g))
        {
            continue;
        }
        char id[8], nm[32];
        if (iGCPackagePlatform(g.bytes.data(), (U32)std::min<size_t>(g.bytes.size(), 512), id,
                               sizeof(id), nm, sizeof(nm)) != IGCPLAT_GAMECUBE)
        {
            continue;
        }

        Package x;
        bool haveX = false;
        auto xi = xbFiles.find(rel);
        if (xi != xbFiles.end())
        {
            haveX = LoadPackage(xi->second, &x);
        }

        std::vector<iSndGCTable> tables;
        for (const iModPkgTocAsset& a : g.toc.assets)
        {
            if (a.type != kSNDI)
            {
                continue;
            }
            const U8* raw = Bytes(g, a);
            if (raw == NULL)
            {
                Fail("%s: SNDI %08x runs past the file", rel.c_str(), a.id);
                continue;
            }
            T.tables++;
            const U8* asset = sBuf.Place(raw, a.size);
            MemReader m = { asset, a.size, 0 };
            iSndGCTable t;
            char why[256];
            if (!iSndGCReadTable(ReadMem, &m, a.size, &t, why, sizeof(why)))
            {
                T.tablesFailed++;
                Fail("%s: SNDI %08x: %s", rel.c_str(), a.id, why);
                continue;
            }
            if (m.outside != 0)
            {
                Fail("%s: SNDI %08x: read outside the asset", rel.c_str(), a.id);
            }

            // The Xbox table of the same id.
            const iSndXboxEntry* xe = NULL;
            U32 xn = 0;
            if (haveX)
            {
                const iModPkgTocAsset* xa = Find(x, a.id, kSNDI);
                if (xa != NULL)
                {
                    xe = iSndXboxTableEntries(Bytes(x, *xa), xa->size, &xn);
                }
            }

            for (U32 i = 0; i < t.numSounds; i++)
            {
                const iSndGCSound& s = t.sounds[i];
                T.sounds++;
                T.streams += (s.flags & 0x2) != 0;
                T.stereo += s.channels == 2;
                T.dspBytes += s.size;
                char rk[48];
                snprintf(rk, sizeof(rk), "%u Hz, %u ch", s.rate, s.channels);
                T.rates[rk]++;

                if (s.offset > a.size || s.size > a.size - s.offset)
                {
                    Fail("%s: sound %08x lies outside its table", rel.c_str(), s.assetID);
                    continue;
                }
                U32 n = 0;
                S16* pcm = (S16*)iSndGCDecode(&s, asset + s.offset, s.size, &n);
                if (pcm == NULL)
                {
                    Fail("%s: sound %08x does not decode", rel.c_str(), s.assetID);
                    continue;
                }
                T.decoded++;
                const U32 frames = n / (2 * s.channels);

                if (s.channels == 2)
                {
                    Note(sStereoNotes, "%-12s %08x %-28s %7.2f s, left/right correlation %.3f",
                         rel.c_str(), s.assetID, NameOf(g, s.assetID).c_str(),
                         (double)frames / s.rate, Correlation(pcm, frames, 2, 0, 1));
                }

                const iSndXboxEntry* twin = NULL;
                for (U32 k = 0; xe != NULL && k < xn; k++)
                {
                    if (xe[k].assetID == s.assetID)
                    {
                        twin = &xe[k];
                    }
                }
                XboxSound xs;
                if (twin == NULL || !XboxDecode(x, *twin, &xs))
                {
                    T.noTwin++;
                    free(pcm);
                    continue;
                }
                T.paired++;

                const double gsec = (double)frames / s.rate;
                const double xsec = (double)(xs.pcm.size() / xs.channels) / xs.rate;
                if (fabs(gsec - xsec) <= 0.05 + 0.02 * std::max(gsec, xsec))
                {
                    T.lenClose++;
                }
                else
                {
                    T.lenFar++;
                    Note(sFarLen, "%-12s %08x %-28s GameCube %.2f s, Xbox %.2f s", rel.c_str(),
                         s.assetID, NameOf(g, s.assetID).c_str(), gsec, xsec);
                }

                S32 lag = 0;
                const double r = EnvelopeMatch(Envelope(pcm, frames, s.channels, s.rate),
                                               Envelope(xs.pcm.data(), (U32)(xs.pcm.size() / xs.channels),
                                                        xs.channels, xs.rate),
                                               &lag);
                if (r >= 0.95)
                {
                    T.env95++;
                }
                else if (r >= 0.9)
                {
                    T.env90++;
                }
                else if (r >= 0.8)
                {
                    T.env80++;
                }
                else
                {
                    T.envLow++;
                }
                if (r < 0.9)
                {
                    Note(sLowEnv, "%-12s %08x %-28s envelope %.3f (lag %d ms), GameCube %.2f s %u Hz, Xbox %.2f s %u Hz",
                         rel.c_str(), s.assetID, NameOf(g, s.assetID).c_str(), r, lag * 10, gsec,
                         s.rate, xsec, xs.rate);
                }
                free(pcm);
            }

            if (fuzz > 0)
            {
                Fuzz(raw, a.size, t, fuzz, rel + " SNDI");
            }
            tables.push_back(t);
        }

        Cutscenes(rel, g, haveX ? &x : NULL, tables, dumpDir);
        for (iSndGCTable& t : tables)
        {
            iSndGCFreeTable(&t);
        }
    }

    printf("-- tables\n");
    printf("  %d GameCube sound tables, %d could not be read\n", T.tables, T.tablesFailed);
    printf("-- sounds\n");
    printf("  %d sounds (%d streams, %d stereo), %d decoded, %.1f MB of DSP-ADPCM\n", T.sounds,
           T.streams, T.stereo, T.decoded, (double)T.dspBytes / (1024.0 * 1024.0));
    for (const auto& kv : T.rates)
    {
        printf("    %-16s %d\n", kv.first.c_str(), kv.second);
    }
    if (!xbRoot.empty())
    {
        printf("  against the Xbox sound of the same id: %d paired, %d with no Xbox twin\n", T.paired,
               T.noTwin);
        printf("    length within 0.05 s + 2%%: %d; further apart: %d\n", T.lenClose, T.lenFar);
        printf("    envelope agreement >= 0.95: %d, 0.90-0.95: %d, 0.80-0.90: %d, below 0.80: %d\n",
               T.env95, T.env90, T.env80, T.envLow);
        for (size_t i = 0; i < sFarLen.size() && (S32)i < sShow; i++)
        {
            printf("    length: %s\n", sFarLen[i].c_str());
        }
        for (size_t i = 0; i < sLowEnv.size() && (S32)i < sShow; i++)
        {
            printf("    envelope: %s\n", sLowEnv[i].c_str());
        }
    }
    printf("-- stereo streams (two-byte interleave)\n");
    for (size_t i = 0; i < sStereoNotes.size() && (S32)i < sShow; i++)
    {
        printf("  %s\n", sStereoNotes[i].c_str());
    }
    if ((S32)sStereoNotes.size() > sShow)
    {
        printf("  (%zu more)\n", sStereoNotes.size() - (size_t)sShow);
    }
    printf("-- cutscene tracks\n");
    printf("  %d tracks, %d decoded, %d with an Xbox twin, %d of those with envelope >= 0.9\n",
           T.tracks, T.tracksDecoded, T.tracksPaired, T.trackEnv90);
    printf("  %d chunk seams, %d with a step past their track's 99.9th percentile\n", T.seams,
           T.seamJumps);
    for (const std::string& s : sTrackNotes)
    {
        printf("  %s\n", s.c_str());
    }
    if (fuzz > 0)
    {
        printf("-- damaged tables\n");
        printf("  %d copies read: %d refused, %d read or described bytes outside the asset\n",
               T.fuzzCopies, T.fuzzRefused, T.fuzzOutside);
    }

    printf("\n%s: %d failure(s)\n", sFailures == 0 ? "PASS" : "FAIL", sFailures);
    return sFailures == 0 ? 0 : 1;
}
