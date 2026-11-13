// PC-only: a package with some of its assets replaced. See iModPackage.h.
#include "iModPackage.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>

struct iModPkg
{
    S32 refs;
    U32 size;

    // bufs[0] is the package header (everything before the DPAK payload) with
    // the DICT rewritten; the rest are the replacements.
    std::vector<std::vector<U8> > bufs;

    struct Extent
    {
        U32 voff; // where it starts in the virtual file
        U32 len;
        S32 kind;
        U32 src; // EXT_ORIG: offset in the original file; EXT_MEM: offset in bufs[buf]
        S32 buf;
    };
    std::vector<Extent> extents; // sorted by voff, covering [0, size) without gaps

    S32 replaced;
    S32 moved;
};

namespace
{
    enum
    {
        EXT_ORIG,
        EXT_MEM,
        EXT_ZERO
    };

    // Layers start on these boundaries in every retail package, and the
    // packer rounds a layer's size up to one (xpkrsvc.cpp LOD_r_LHDR).
    const U32 kLayerAlign = 0x800;

    // The smallest alignment a moved asset gets. Retail pads every asset to 16.
    const U32 kMinAlign = 16;

    const U32 kSNDI = 0x534E4449; // 'SNDI'
    const U32 kSNDIHeader = 12;
    const U32 kSNDIEntry = 44;

    U32 RdBE(const U8* p)
    {
        return ((U32)p[0] << 24) | ((U32)p[1] << 16) | ((U32)p[2] << 8) | (U32)p[3];
    }

    void WrBE(U8* p, U32 v)
    {
        p[0] = (U8)(v >> 24);
        p[1] = (U8)(v >> 16);
        p[2] = (U8)(v >> 8);
        p[3] = (U8)v;
    }

    U32 RdLE(const U8* p)
    {
        return ((U32)p[3] << 24) | ((U32)p[2] << 16) | ((U32)p[1] << 8) | (U32)p[0];
    }

    void WrLE(U8* p, U32 v)
    {
        p[0] = (U8)v;
        p[1] = (U8)(v >> 8);
        p[2] = (U8)(v >> 16);
        p[3] = (U8)(v >> 24);
    }

    void WrLE16(U8* p, U16 v)
    {
        p[0] = (U8)v;
        p[1] = (U8)(v >> 8);
    }

    U32 RoundUp(U32 v, U32 a)
    {
        return (v + a - 1) / a * a;
    }

    std::string FourCC(U32 t)
    {
        char s[5] = { (char)(t >> 24), (char)(t >> 16), (char)(t >> 8), (char)t, 0 };
        return s;
    }

    std::string Format(const char* fmt, ...)
    {
        char line[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(line, sizeof(line), fmt, ap);
        va_end(ap);
        return line;
    }

    // Where the bytes of a package come from: a plain file, or a virtual
    // package over one.
    struct Source
    {
        FILE* fp;
        const iModPkg* pkg;
        U32 size;

        bool Read(U32 off, void* buf, U32 n) const
        {
            if (off > size || n > size - off)
            {
                return false;
            }
            if (pkg != NULL)
            {
                iModPkgRead(pkg, fp, off, buf, n);
                return true;
            }
            if (fseek(fp, (long)off, SEEK_SET) != 0)
            {
                return false;
            }
            return fread(buf, 1, n, fp) == n;
        }
    };

    struct Parsed
    {
        std::vector<U8> head; // [0, the DPAK payload)
        U32 fileSize;
        U32 strmHdr; // the STRM chunk header's offset
        U32 dpakHdr; // the DPAK chunk header's offset
        std::vector<iModPkgTocAsset> assets;
        std::vector<U32> ahdr; // each asset's AHDR payload offset in head
        std::vector<iModPkgTocLayer> layers;
    };

    // The chunks one level down from [begin, end) of `head`, calling `fn` with
    // each one's id, payload offset and payload size. Stops at anything that
    // does not fit.
    template <class Fn>
    void Walk(const std::vector<U8>& head, U32 begin, U32 end, Fn fn)
    {
        U32 pos = begin;
        while (pos + 8 <= end && pos + 8 <= head.size())
        {
            U32 id = RdBE(&head[pos]);
            U32 size = RdBE(&head[pos + 4]);
            U32 body = pos + 8;
            if (size > end - body)
            {
                break;
            }
            fn(id, body, size);
            pos = body + size;
        }
    }

    // A NUL-terminated string at head[off], bounded by `end`.
    std::string ReadString(const std::vector<U8>& head, U32 off, U32 end)
    {
        std::string s;
        while (off < end && off < head.size() && head[off] != 0)
        {
            s += (char)head[off++];
        }
        return s;
    }

    bool Parse(const Source& src, Parsed* out)
    {
        out->fileSize = src.size;
        out->strmHdr = 0;
        out->dpakHdr = 0;

        // HIPA holds no payload of its own: the other chunks follow it as
        // siblings (xpkrsvc.cpp PKR_parse_TOC, and pc/iFile.cpp iHipPlatform).
        U8 h[8];
        if (!src.Read(0, h, 8) || RdBE(h) != 0x48495041) // 'HIPA'
        {
            return false;
        }

        // The top level, header by header, until the DPAK payload: everything
        // before that is the header the DICT is in.
        bool sawDict = false;
        U32 pos = 8;
        while (pos + 8 <= src.size && out->dpakHdr == 0)
        {
            if (!src.Read(pos, h, 8))
            {
                return false;
            }
            U32 id = RdBE(h);
            U32 size = RdBE(h + 4);
            if (size > src.size - pos - 8)
            {
                return false;
            }
            if (id == 0x44494354) // 'DICT'
            {
                sawDict = true;
            }
            else if (id == 0x5354524D) // 'STRM'
            {
                out->strmHdr = pos;
                U32 sub = pos + 8;
                U32 end = pos + 8 + size;
                while (sub + 8 <= end)
                {
                    if (!src.Read(sub, h, 8))
                    {
                        return false;
                    }
                    if (RdBE(h) == 0x4450414B) // 'DPAK'
                    {
                        out->dpakHdr = sub;
                        break;
                    }
                    sub += 8 + RdBE(h + 4);
                }
                if (out->dpakHdr == 0)
                {
                    return false;
                }
            }
            pos += 8 + size;
        }
        if (!sawDict || out->dpakHdr == 0)
        {
            return false;
        }

        U32 headLen = out->dpakHdr + 8;
        out->head.resize(headLen);
        if (!src.Read(0, &out->head[0], headLen))
        {
            return false;
        }

        std::map<U32, S32> byId;
        std::vector<std::pair<U32, std::vector<U32> > > lhdr;

        Walk(out->head, 8, headLen, [&](U32 id, U32 body, U32 size) {
            if (id != 0x44494354) // 'DICT'
            {
                return;
            }
            Walk(out->head, body, body + size, [&](U32 id2, U32 body2, U32 size2) {
                if (id2 == 0x41544F43) // 'ATOC'
                {
                    Walk(out->head, body2, body2 + size2, [&](U32 id3, U32 b3, U32 s3) {
                        if (id3 != 0x41484452 || s3 < 24) // 'AHDR'
                        {
                            return;
                        }
                        const U8* p = &out->head[b3];
                        iModPkgTocAsset a;
                        a.id = RdBE(p);
                        a.type = RdBE(p + 4);
                        a.off = RdBE(p + 8);
                        a.size = RdBE(p + 12);
                        a.pad = RdBE(p + 16);
                        a.flags = RdBE(p + 20);
                        a.align = 0;
                        a.layer = -1;
                        if ((S32)a.size < 0)
                        {
                            a.size = 0;
                        }
                        if ((S32)a.pad < 0)
                        {
                            a.pad = 0;
                        }
                        Walk(out->head, b3 + 24, b3 + s3, [&](U32 id4, U32 b4, U32 s4) {
                            if (id4 == 0x41444247 && s4 >= 4) // 'ADBG'
                            {
                                a.align = RdBE(&out->head[b4]);
                                a.name = ReadString(out->head, b4 + 4, b4 + s4);
                            }
                        });
                        byId[a.id] = (S32)out->assets.size();
                        out->assets.push_back(a);
                        out->ahdr.push_back(b3);
                    });
                }
                else if (id2 == 0x4C544F43) // 'LTOC'
                {
                    Walk(out->head, body2, body2 + size2, [&](U32 id3, U32 b3, U32 s3) {
                        if (id3 != 0x4C484452 || s3 < 8) // 'LHDR'
                        {
                            return;
                        }
                        const U8* p = &out->head[b3];
                        U32 type = RdBE(p);
                        U32 count = RdBE(p + 4);
                        if (count > (s3 - 8) / 4)
                        {
                            count = (s3 - 8) / 4;
                        }
                        std::vector<U32> ids;
                        for (U32 i = 0; i < count; i++)
                        {
                            ids.push_back(RdBE(p + 8 + 4 * i));
                        }
                        lhdr.push_back(std::make_pair(type, ids));
                    });
                }
            });
        });

        // Layers after assets: the DICT holds ATOC before LTOC, but nothing
        // here depends on it.
        for (size_t i = 0; i < lhdr.size(); i++)
        {
            iModPkgTocLayer layer;
            layer.type = lhdr[i].first;
            for (size_t j = 0; j < lhdr[i].second.size(); j++)
            {
                std::map<U32, S32>::const_iterator it = byId.find(lhdr[i].second[j]);
                if (it == byId.end())
                {
                    continue;
                }
                layer.assets.push_back(it->second);
                out->assets[it->second].layer = (S32)out->layers.size();
            }
            out->layers.push_back(layer);
        }

        return !out->assets.empty();
    }

    // The alignment a moved asset keeps: whatever its old offset had (so a
    // streamed texture read as `sector * 32` and a sound on a 2 KB boundary
    // stay where their readers expect them), and at least what ADBG asked for,
    // within [16, 0x800].
    U32 AlignOf(const iModPkgTocAsset& a)
    {
        U32 align = kLayerAlign;
        if (a.off != 0)
        {
            align = a.off & (~a.off + 1);
        }
        if (a.align > 0 && a.align <= kLayerAlign && a.align > align)
        {
            align = a.align;
        }
        if (align < kMinAlign)
        {
            align = kMinAlign;
        }
        if (align > kLayerAlign)
        {
            align = kLayerAlign;
        }
        return align;
    }

    // Rewrites the SNDI entries that describe replaced assets. Returns true if
    // `table` changed.
    bool PatchSNDI(std::vector<U8>& table, const std::map<U32, S32>& byId,
                   const std::vector<S32>& replOf, const std::vector<iModPkgReplacement>& repls,
                   const std::vector<iModPkgTocAsset>& assets, std::vector<std::string>* log)
    {
        if (table.size() < kSNDIHeader)
        {
            return false;
        }

        bool changed = false;
        U32 count = (U32)((table.size() - kSNDIHeader) / kSNDIEntry);
        for (U32 i = 0; i < count; i++)
        {
            U8* e = &table[kSNDIHeader + kSNDIEntry * i];
            U32 aid = RdLE(e + 0x18);
            std::map<U32, S32>::const_iterator it = byId.find(aid);
            if (it == byId.end() || replOf[it->second] < 0)
            {
                continue;
            }
            const iModPkgReplacement& r = repls[replOf[it->second]];

            WrLE(e + 0x14, (U32)r.bytes.size()); // data_size
            if (r.pcm)
            {
                U16 block = (U16)(r.channels * 2);
                WrLE16(e + 0x00, 1); // WAVE_FORMAT_PCM
                WrLE16(e + 0x02, r.channels);
                WrLE(e + 0x04, r.rate);
                WrLE(e + 0x08, r.rate * block);
                WrLE16(e + 0x0C, block);
                WrLE16(e + 0x0E, 16);
                WrLE16(e + 0x10, 0);
            }
            if (log != NULL)
            {
                log->push_back(Format("  SNDI entry for %08X '%s' now %s, %u bytes", aid,
                                      assets[it->second].name.c_str(),
                                      r.pcm ? Format("PCM %u Hz x%u", r.rate, r.channels).c_str()
                                            : "the original format",
                                      (U32)r.bytes.size()));
            }
            changed = true;
        }
        return changed;
    }
} // namespace

iModPkg* iModPkgBuild(const char* path, iModPkgLookupFn lookup, void* ctx,
                      std::vector<std::string>* log)
{
    FILE* fp = fopen(path, "rb");
    if (fp == NULL)
    {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    if (len <= 0)
    {
        fclose(fp);
        return NULL;
    }

    Source src = { fp, NULL, (U32)len };
    Parsed P;
    if (!Parse(src, &P))
    {
        fclose(fp);
        return NULL;
    }

    const S32 n = (S32)P.assets.size();
    std::map<U32, S32> byId;
    for (S32 i = 0; i < n; i++)
    {
        byId[P.assets[i].id] = i;
    }

    // --- what is replaced ---------------------------------------------------------
    std::vector<S32> replOf(n, -1);
    std::vector<iModPkgReplacement> repls;
    for (S32 i = 0; i < n; i++)
    {
        const iModPkgTocAsset& a = P.assets[i];
        iModPkgAsset q = { a.id, a.type, a.name.c_str(), a.size };
        iModPkgReplacement r;
        if (!lookup(ctx, q, &r))
        {
            continue;
        }
        if (r.bytes.empty())
        {
            if (log != NULL)
            {
                log->push_back(Format("  %08X '%s': %s is empty; an asset cannot be removed, kept "
                                      "the original",
                                      a.id, a.name.c_str(), r.source.c_str()));
            }
            continue;
        }
        replOf[i] = (S32)repls.size();
        repls.push_back(r);
    }

    if (repls.empty())
    {
        fclose(fp);
        return NULL;
    }

    // SNDI tables describing replaced sounds. Taken after the lookups, so a
    // mod that replaces the table itself is patched too.
    for (S32 i = 0; i < n; i++)
    {
        if (P.assets[i].type != kSNDI)
        {
            continue;
        }
        std::vector<U8> table;
        if (replOf[i] >= 0)
        {
            table = repls[replOf[i]].bytes;
        }
        else
        {
            table.resize(P.assets[i].size);
            if (table.empty() || !src.Read(P.assets[i].off, &table[0], (U32)table.size()))
            {
                continue;
            }
        }
        if (!PatchSNDI(table, byId, replOf, repls, P.assets, log))
        {
            continue;
        }
        if (replOf[i] >= 0)
        {
            repls[replOf[i]].bytes.swap(table);
        }
        else
        {
            iModPkgReplacement r;
            r.bytes.swap(table);
            r.source = "its sound table, for the sounds above";
            replOf[i] = (S32)repls.size();
            repls.push_back(r);
        }
    }

    fclose(fp);

    // --- layout ---------------------------------------------------------------------
    iModPkg* pkg = new iModPkg;
    pkg->refs = 1;
    pkg->replaced = 0;
    pkg->moved = 0;
    pkg->bufs.push_back(std::vector<U8>());
    pkg->bufs[0].swap(P.head);
    for (size_t i = 0; i < repls.size(); i++)
    {
        pkg->bufs.push_back(std::vector<U8>());
        pkg->bufs.back().swap(repls[i].bytes);
    }
    std::vector<U8>& head = pkg->bufs[0];
    const U32 headLen = (U32)head.size();
    const U32 fileSize = P.fileSize;

    std::vector<U32> noff(n), nsize(n), npad(n);
    for (S32 i = 0; i < n; i++)
    {
        noff[i] = P.assets[i].off;
        nsize[i] = P.assets[i].size;
        npad[i] = P.assets[i].pad;
        if (replOf[i] >= 0)
        {
            nsize[i] = (U32)pkg->bufs[1 + replOf[i]].size();
        }
    }

    // Where the next asset after each offset starts, for the room the last
    // asset of a layer has.
    std::vector<U32> starts;
    for (S32 i = 0; i < n; i++)
    {
        if (P.assets[i].size > 0)
        {
            starts.push_back(P.assets[i].off);
        }
    }
    std::sort(starts.begin(), starts.end());
    auto nextStart = [&](U32 off) -> U32 {
        std::vector<U32>::const_iterator it = std::upper_bound(starts.begin(), starts.end(), off);
        return it == starts.end() ? fileSize : *it;
    };

    std::vector<iModPkg::Extent> pieces;
    auto addPiece = [&](U32 voff, U32 len, S32 kind, U32 srcOff, S32 buf) {
        if (len == 0)
        {
            return;
        }
        iModPkg::Extent e = { voff, len, kind, srcOff, buf };
        pieces.push_back(e);
    };

    U32 end = fileSize;

    // Unlayered assets are read through the TOC alone; each is a group of one.
    std::vector<std::vector<S32> > groups;
    for (size_t l = 0; l < P.layers.size(); l++)
    {
        groups.push_back(P.layers[l].assets);
    }
    for (S32 i = 0; i < n; i++)
    {
        if (P.assets[i].layer < 0 && replOf[i] >= 0)
        {
            groups.push_back(std::vector<S32>(1, i));
        }
    }

    for (size_t g = 0; g < groups.size(); g++)
    {
        const std::vector<S32>& as = groups[g];
        bool any = false;
        for (size_t k = 0; k < as.size(); k++)
        {
            any = any || replOf[as[k]] >= 0;
        }
        if (!any)
        {
            continue;
        }

        // In place if every replacement fits its slot and the layer is laid
        // out the way the packer reads it.
        bool inPlace = true;
        std::vector<U32> slot(as.size());
        for (size_t k = 0; k < as.size(); k++)
        {
            const iModPkgTocAsset& a = P.assets[as[k]];
            if (replOf[as[k]] >= 0 && (a.off < headLen || a.off >= fileSize))
            {
                inPlace = false; // not in the stream where a slot can be rewritten
            }
            if (k + 1 < as.size())
            {
                const iModPkgTocAsset& b = P.assets[as[k + 1]];
                if (a.off + a.size + a.pad != b.off)
                {
                    inPlace = false;
                }
                slot[k] = b.off > a.off ? b.off - a.off : 0;
            }
            else
            {
                U32 bound = RoundUp(a.off + a.size, kLayerAlign);
                U32 next = nextStart(a.off);
                if (next < bound)
                {
                    bound = next;
                }
                if (bound > fileSize)
                {
                    bound = fileSize;
                }
                slot[k] = bound > a.off ? bound - a.off : 0;
            }
            if (replOf[as[k]] >= 0 && nsize[as[k]] > slot[k])
            {
                inPlace = false;
            }
        }

        if (inPlace)
        {
            for (size_t k = 0; k < as.size(); k++)
            {
                S32 i = as[k];
                if (replOf[i] < 0)
                {
                    continue;
                }
                addPiece(noff[i], nsize[i], EXT_MEM, 0, 1 + replOf[i]);
                addPiece(noff[i] + nsize[i], slot[k] - nsize[i], EXT_ZERO, 0, 0);
                if (k + 1 < as.size())
                {
                    npad[i] = slot[k] - nsize[i];
                }
                else if (nsize[i] + npad[i] > slot[k])
                {
                    npad[i] = slot[k] - nsize[i];
                }
                pkg->replaced++;
                if (log != NULL)
                {
                    log->push_back(Format("  %08X %s '%s' <- %s (%u -> %u bytes, in place)",
                                          P.assets[i].id, FourCC(P.assets[i].type).c_str(),
                                          P.assets[i].name.c_str(),
                                          repls[replOf[i]].source.c_str(), P.assets[i].size,
                                          nsize[i]));
                }
            }
            continue;
        }

        // Moved: the whole group, to the end of the stream.
        U32 pos = RoundUp(end, kLayerAlign);
        U32 first = pos;
        for (size_t k = 0; k < as.size(); k++)
        {
            S32 i = as[k];
            pos = RoundUp(pos, AlignOf(P.assets[i]));
            if (k > 0)
            {
                S32 prev = as[k - 1];
                npad[prev] = pos - (noff[prev] + nsize[prev]);
            }
            noff[i] = pos;
            if (replOf[i] >= 0)
            {
                addPiece(pos, nsize[i], EXT_MEM, 0, 1 + replOf[i]);
                pkg->replaced++;
                if (log != NULL)
                {
                    log->push_back(Format("  %08X %s '%s' <- %s (%u -> %u bytes, layer moved)",
                                          P.assets[i].id, FourCC(P.assets[i].type).c_str(),
                                          P.assets[i].name.c_str(),
                                          repls[replOf[i]].source.c_str(), P.assets[i].size,
                                          nsize[i]));
                }
            }
            else
            {
                addPiece(pos, nsize[i], EXT_ORIG, P.assets[i].off, 0);
            }
            pos += nsize[i];
        }
        end = pos;
        pkg->moved++;
        if (log != NULL)
        {
            log->push_back(Format("  layer of %u asset(s) moved to 0x%X (0x%X bytes)",
                                  (U32)as.size(), first, pos - first));
        }
    }

    const U32 size = end > fileSize ? RoundUp(end, kLayerAlign) : fileSize;

    // --- the DICT -------------------------------------------------------------------
    for (S32 i = 0; i < n; i++)
    {
        U8* p = &head[P.ahdr[i]];
        WrBE(p + 8, noff[i]);
        WrBE(p + 12, nsize[i]);
        WrBE(p + 16, npad[i]);
    }

    // STRM and DPAK grow by what was appended, when they ran to the end of the
    // file (they do in every retail package). The packer only skips over them.
    if (size > fileSize)
    {
        U32 delta = size - fileSize;
        U32 strmSize = RdBE(&head[P.strmHdr + 4]);
        if (P.strmHdr + 8 + strmSize == fileSize)
        {
            WrBE(&head[P.strmHdr + 4], strmSize + delta);
        }
        U32 dpakSize = RdBE(&head[P.dpakHdr + 4]);
        if (P.dpakHdr + 8 + dpakSize == fileSize)
        {
            WrBE(&head[P.dpakHdr + 4], dpakSize + delta);
        }
    }

    // --- the extent list --------------------------------------------------------------
    addPiece(0, headLen, EXT_MEM, 0, 0);
    std::sort(pieces.begin(), pieces.end(),
              [](const iModPkg::Extent& a, const iModPkg::Extent& b) { return a.voff < b.voff; });

    U32 at = 0;
    for (size_t i = 0; i < pieces.size(); i++)
    {
        const iModPkg::Extent& e = pieces[i];
        if (e.voff > at)
        {
            // A gap: the original file's bytes where there are any, zero past it.
            if (at < fileSize)
            {
                U32 stop = e.voff < fileSize ? e.voff : fileSize;
                iModPkg::Extent o = { at, stop - at, EXT_ORIG, at, 0 };
                pkg->extents.push_back(o);
                at = stop;
            }
            if (e.voff > at)
            {
                iModPkg::Extent z = { at, e.voff - at, EXT_ZERO, 0, 0 };
                pkg->extents.push_back(z);
                at = e.voff;
            }
        }
        pkg->extents.push_back(e);
        at = e.voff + e.len;
    }
    if (at < size)
    {
        if (at < fileSize)
        {
            iModPkg::Extent o = { at, fileSize - at, EXT_ORIG, at, 0 };
            pkg->extents.push_back(o);
            at = fileSize;
        }
        if (at < size)
        {
            iModPkg::Extent z = { at, size - at, EXT_ZERO, 0, 0 };
            pkg->extents.push_back(z);
        }
    }
    pkg->size = size;

    return pkg;
}

void iModPkgRetain(iModPkg* pkg)
{
    if (pkg != NULL)
    {
        pkg->refs++;
    }
}

void iModPkgRelease(iModPkg* pkg)
{
    if (pkg != NULL && --pkg->refs == 0)
    {
        delete pkg;
    }
}

U32 iModPkgSize(const iModPkg* pkg)
{
    return pkg->size;
}

S32 iModPkgReplacedCount(const iModPkg* pkg)
{
    return pkg->replaced;
}

S32 iModPkgMovedLayers(const iModPkg* pkg)
{
    return pkg->moved;
}

U32 iModPkgRead(const iModPkg* pkg, FILE* original, U32 offset, void* buf, U32 size)
{
    U8* out = (U8*)buf;
    U32 done = 0;

    // The extent holding `offset`: the last one starting at or before it.
    size_t lo = 0;
    size_t hi = pkg->extents.size();
    while (hi - lo > 1)
    {
        size_t mid = (lo + hi) / 2;
        if (pkg->extents[mid].voff <= offset)
        {
            lo = mid;
        }
        else
        {
            hi = mid;
        }
    }

    for (size_t x = lo; done < size && x < pkg->extents.size(); x++)
    {
        const iModPkg::Extent& e = pkg->extents[x];
        U32 pos = offset + done;
        if (pos < e.voff || pos >= e.voff + e.len)
        {
            continue;
        }
        U32 in = pos - e.voff;
        U32 n = e.len - in;
        if (n > size - done)
        {
            n = size - done;
        }

        switch (e.kind)
        {
        case EXT_ORIG:
        {
            size_t got = 0;
            if (original != NULL && fseek(original, (long)(e.src + in), SEEK_SET) == 0)
            {
                got = fread(out + done, 1, n, original);
            }
            if (got < n)
            {
                memset(out + done + got, 0, n - got);
            }
            break;
        }
        case EXT_MEM:
            memcpy(out + done, &pkg->bufs[e.buf][e.src + in], n);
            break;
        default:
            memset(out + done, 0, n);
            break;
        }
        done += n;
    }

    if (done < size)
    {
        memset(out + done, 0, size - done);
    }
    return size;
}

bool iModPkgReadToc(FILE* fp, const iModPkg* pkg, iModPkgToc* toc)
{
    U32 size;
    if (pkg != NULL)
    {
        size = pkg->size;
    }
    else
    {
        fseek(fp, 0, SEEK_END);
        long len = ftell(fp);
        if (len <= 0)
        {
            return false;
        }
        size = (U32)len;
    }

    Source src = { fp, pkg, size };
    Parsed P;
    if (!Parse(src, &P))
    {
        return false;
    }
    toc->fileSize = size;
    toc->assets.swap(P.assets);
    toc->layers.swap(P.layers);
    return true;
}

U32 iModPkgLayerSize(const iModPkgToc& toc, S32 layer)
{
    const std::vector<S32>& as = toc.layers[layer].assets;
    U32 size = 0;
    for (size_t k = 0; k < as.size(); k++)
    {
        size += toc.assets[as[k]].size;
        if (k + 1 < as.size())
        {
            size += toc.assets[as[k]].pad;
        }
    }
    return size > 0 ? RoundUp(size, kLayerAlign) : 0;
}
