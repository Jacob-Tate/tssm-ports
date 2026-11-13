// rw::c3d: rasters.
//
// The PICA samples no DXT and no palettes, so every texture is decoded on the
// CPU when it loads and packed by what its top level holds: RGB565 when it is
// opaque, RGBA5551 when its alpha is only on or off, RGBA4 otherwise. It is
// kept at half size -- the 400 x 240 screen shows most textures at their
// second level anyway -- except the palettised font atlases, which keep their
// size (half-size text is unreadable at 400 x 240) and are LA8 when grey.
// Mips go down to 8 x 8. The texels are stored as the PICA reads them: 8 x 8
// tiles in Morton order, the picture's bottom row first (the PICA samples
// v = 0 at the end of its memory, and RenderWare's v = 0 is the picture's top
// row), so the game's texture coordinates go through unchanged.
//
// A Raster::CAMERA is the top screen and has no storage of its own; neither
// does its ZBUFFER. A camera texture gets its storage and render target at its
// first use (c3drtt.cpp).

#include "c3dimpl.h"

#include "d3d/rwxbox.h"

#include <stdio.h>
#include <stdlib.h>

namespace rw
{
    namespace c3d
    {
        int32 nativeRasterOffset;

        namespace
        {
            // ----------------------------------------------------- packing

            inline uint16 pack565(const uint8* p)
            {
                const uint32 r = (p[0] * 31u + 127u) / 255u;
                const uint32 g = (p[1] * 63u + 127u) / 255u;
                const uint32 b = (p[2] * 31u + 127u) / 255u;
                return (uint16)((r << 11) | (g << 5) | b);
            }

            inline uint16 pack5551(const uint8* p)
            {
                const uint32 r = (p[0] * 31u + 127u) / 255u;
                const uint32 g = (p[1] * 31u + 127u) / 255u;
                const uint32 b = (p[2] * 31u + 127u) / 255u;
                return (uint16)((r << 11) | (g << 6) | (b << 1) | (p[3] >= 128 ? 1u : 0u));
            }

            inline uint16 pack4444(const uint8* p)
            {
                const uint32 r = (p[0] * 15u + 127u) / 255u;
                const uint32 g = (p[1] * 15u + 127u) / 255u;
                const uint32 b = (p[2] * 15u + 127u) / 255u;
                const uint32 a = (p[3] * 15u + 127u) / 255u;
                return (uint16)((r << 12) | (g << 8) | (b << 4) | a);
            }

            inline uint16 packLA8(const uint8* p)
            {
                return (uint16)(((uint32)p[0] << 8) | p[3]);
            }

            // The PICA's texel order: 8 x 8 tiles, rows of tiles from the
            // bottom of the picture up, texels in a tile in Morton (Z) order.
            inline uint32 tiledIndex(uint32 x, uint32 y, uint32 w, uint32 h)
            {
                const uint32 fy = h - 1 - y;
                const uint32 tile = (fy >> 3) * (w >> 3) + (x >> 3);
                const uint32 tx = x & 7;
                const uint32 ty = fy & 7;
                const uint32 morton = (tx & 1) | ((ty & 1) << 1) | ((tx & 2) << 1) |
                                      ((ty & 2) << 2) | ((tx & 4) << 2) | ((ty & 4) << 3);
                return tile * 64 + morton;
            }

            int32 pow2Ceil(int32 v)
            {
                int32 p = 8;
                while (p < v && p < 1024)
                {
                    p <<= 1;
                }
                return p;
            }

            bool isPow2(int32 v)
            {
                return v >= 8 && v <= 1024 && (v & (v - 1)) == 0;
            }

            // A box filter to half size (or the same size along a side that
            // is already 8 or less). RGBA8 in and out.
            uint8* halve(const uint8* src, int32 w, int32 h, int32* ow, int32* oh)
            {
                const int32 nw = w > 8 ? w / 2 : w;
                const int32 nh = h > 8 ? h / 2 : h;
                const int32 sx = w / nw;
                const int32 sy = h / nh;
                uint8* dst = (uint8*)malloc((size_t)nw * nh * 4);
                if (dst == nil)
                {
                    return nil;
                }
                for (int32 y = 0; y < nh; y++)
                {
                    for (int32 x = 0; x < nw; x++)
                    {
                        uint32 acc[4] = { 0, 0, 0, 0 };
                        for (int32 j = 0; j < sy; j++)
                        {
                            const uint8* row = src + ((size_t)(y * sy + j) * w + x * sx) * 4;
                            for (int32 i = 0; i < sx; i++)
                            {
                                acc[0] += row[i * 4 + 0];
                                acc[1] += row[i * 4 + 1];
                                acc[2] += row[i * 4 + 2];
                                acc[3] += row[i * 4 + 3];
                            }
                        }
                        const uint32 n = (uint32)(sx * sy);
                        uint8* d = dst + ((size_t)y * nw + x) * 4;
                        d[0] = (uint8)((acc[0] + n / 2) / n);
                        d[1] = (uint8)((acc[1] + n / 2) / n);
                        d[2] = (uint8)((acc[2] + n / 2) / n);
                        d[3] = (uint8)((acc[3] + n / 2) / n);
                    }
                }
                *ow = nw;
                *oh = nh;
                return dst;
            }

            // Nearest-neighbour onto a size the PICA takes (powers of two
            // from 8 to 1024); NULL when nothing needs doing.
            uint8* fitPow2(const uint8* src, int32 w, int32 h, int32* ow, int32* oh)
            {
                int32 nw = isPow2(w) ? w : pow2Ceil(w);
                int32 nh = isPow2(h) ? h : pow2Ceil(h);
                if (nw == w && nh == h)
                {
                    return nil;
                }
                uint8* dst = (uint8*)malloc((size_t)nw * nh * 4);
                // Each column's source, worked out once: a division for every
                // texel (a library call on the ARM11) was most of the cost.
                uint32* cols = (uint32*)malloc((size_t)nw * sizeof(uint32));
                if (dst == nil || cols == nil)
                {
                    free(dst);
                    free(cols);
                    return nil;
                }
                for (int32 x = 0; x < nw; x++)
                {
                    cols[x] = (uint32)x * (uint32)w / (uint32)nw;
                }
                const uint32* s = (const uint32*)src;
                uint32* d = (uint32*)dst;
                for (int32 y = 0; y < nh; y++)
                {
                    const uint32* row = s + (size_t)((uint32)y * (uint32)h / (uint32)nh) * w;
                    for (int32 x = 0; x < nw; x++)
                    {
                        *d++ = row[cols[x]];
                    }
                }
                free(cols);
                *ow = nw;
                *oh = nh;
                return dst;
            }

            int32 kindOf(const uint8* rgba, int32 w, int32 h)
            {
                int32 total = w * h;
                int32 between = 0;
                bool any = false;
                for (int32 i = 0; i < total; i++)
                {
                    const uint8 a = rgba[i * 4 + 3];
                    if (a != 255)
                    {
                        any = true;
                    }
                    if (a > ALPHAEDGELOW && a < ALPHAEDGEHIGH)
                    {
                        between++;
                    }
                }
                if (!any)
                {
                    return ALPHAOPAQUE;
                }
                return between * 100 < total * ALPHAKEYEDPERCENT ? ALPHAKEYED : ALPHAGRADED;
            }

            bool allGrey(const uint8* rgba, int32 w, int32 h)
            {
                for (int32 i = 0; i < w * h; i++)
                {
                    const uint8* p = rgba + i * 4;
                    if (p[0] != p[1] || p[1] != p[2])
                    {
                        return false;
                    }
                }
                return true;
            }

            // An Image as tightly packed RGBA8, top row first: the caller frees
            // it. Paletted and 16/24-bit images are expanded.
            uint8* imageRGBA(Image* img)
            {
                if (img->depth != 32)
                {
                    img->convertTo32();
                }
                if (img->depth != 32 || img->width <= 0 || img->height <= 0)
                {
                    return nil;
                }
                uint8* out = (uint8*)malloc((size_t)img->width * img->height * 4);
                if (out == nil)
                {
                    return nil;
                }
                for (int32 y = 0; y < img->height; y++)
                {
                    memcpy(out + (size_t)y * img->width * 4, img->pixels + (size_t)y * img->stride,
                           (size_t)img->width * 4);
                }
                return out;
            }

            uint32 sTexLogged;

            // A decal's border (iDecalFilter.h): the top level's edges where
            // every texel is clear, kept clear on every level made here. A
            // decal is laid over whole triangles with CLAMP, so its edge texels
            // cover the rest of each one, and the half-size top level and the
            // box-filtered levels below have the art pulled into their edges.
            enum
            {
                EDGE_TOP,
                EDGE_BOTTOM,
                EDGE_LEFT,
                EDGE_RIGHT
            };

            struct ClearEdges
            {
                uint8* line[4]; // RGBA8 along the edge; nil where it is not all clear
                int32 len[4];
            };

            void edgesFree(ClearEdges* e)
            {
                for (int32 i = 0; i < 4; i++)
                {
                    free(e->line[i]);
                    e->line[i] = nil;
                }
            }

            // Where a side of a w x h picture starts, and the step along it.
            uint8* edgeStart(uint8* pic, int32 side, int32 w, int32 h, size_t* step)
            {
                const bool row = side == EDGE_TOP || side == EDGE_BOTTOM;
                *step = row ? 4 : (size_t)w * 4;
                return side == EDGE_BOTTOM ? pic + (size_t)(h - 1) * w * 4 :
                       side == EDGE_RIGHT  ? pic + (size_t)(w - 1) * 4 :
                                             pic;
            }

            // One side of a w x h picture, kept when its alpha is all zero.
            void takeEdge(ClearEdges* e, int32 side, uint8* pic, int32 w, int32 h)
            {
                size_t step;
                const uint8* p = edgeStart(pic, side, w, h, &step);
                const int32 n = side == EDGE_TOP || side == EDGE_BOTTOM ? w : h;
                for (int32 i = 0; i < n; i++)
                {
                    if (p[i * step + 3] != 0)
                    {
                        return;
                    }
                }
                uint8* line = (uint8*)malloc((size_t)n * 4);
                if (line == nil)
                {
                    return;
                }
                for (int32 i = 0; i < n; i++)
                {
                    memcpy(line + i * 4, p + i * step, 4);
                }
                free(e->line[side]);
                e->line[side] = line;
                e->len[side] = n;
            }

            // The clear edges onto a w x h level, each texel the average of
            // the top level's edge texels it stands for.
            void applyEdges(uint8* pic, int32 w, int32 h, const ClearEdges* e)
            {
                if (e == nil)
                {
                    return;
                }
                for (int32 side = 0; side < 4; side++)
                {
                    const uint8* line = e->line[side];
                    if (line == nil)
                    {
                        continue;
                    }
                    size_t step;
                    uint8* p = edgeStart(pic, side, w, h, &step);
                    const int32 n = side == EDGE_TOP || side == EDGE_BOTTOM ? w : h;
                    const int32 len = e->len[side];
                    for (int32 i = 0; i < n; i++)
                    {
                        const int32 a = i * len / n;
                        int32 b = (i + 1) * len / n;
                        if (b <= a)
                        {
                            b = a + 1;
                        }
                        uint32 acc[3] = { 0, 0, 0 };
                        for (int32 j = a; j < b; j++)
                        {
                            acc[0] += line[j * 4 + 0];
                            acc[1] += line[j * 4 + 1];
                            acc[2] += line[j * 4 + 2];
                        }
                        const uint32 k = (uint32)(b - a);
                        uint8* d = p + i * step;
                        d[0] = (uint8)((acc[0] + k / 2) / k);
                        d[1] = (uint8)((acc[1] + k / 2) / k);
                        d[2] = (uint8)((acc[2] + k / 2) / k);
                        d[3] = 0;
                    }
                }
            }

            // Pack, tile and upload: `rgba` (w x h, top row first) becomes
            // `natras`'s texture, with mips down to 8 x 8 when `mips`, and
            // `edges` kept clear on each level.
            bool buildTexture(C3dRaster* natras, uint8* rgba, int32 w, int32 h, int32 kind,
                              bool font, bool mips, const ClearEdges* edges = nil)
            {
                int32 fw = w;
                int32 fh = h;
                uint8* fitted = fitPow2(rgba, w, h, &fw, &fh);
                uint8* level = fitted != nil ? fitted : rgba;

                uint32 fmt;
                if (font)
                {
                    fmt = allGrey(level, fw, fh) ? GPU_LA8 : GPU_RGBA4;
                }
                else
                {
                    fmt = kind == ALPHAOPAQUE ? GPU_RGB565 :
                          kind == ALPHAKEYED  ? GPU_RGBA5551 :
                                                GPU_RGBA4;
                }

                int32 maxLevel = 0;
                if (mips && !font)
                {
                    maxLevel = C3D_TexCalcMaxLevel((u32)fw, (u32)fh);
                    if (maxLevel < 0)
                    {
                        maxLevel = 0;
                    }
                }

                uint32 total = 0;
                for (int32 l = 0; l <= maxLevel; l++)
                {
                    total += (uint32)((fw >> l) * (fh >> l) * 2);
                }
                uint16* texels = (uint16*)malloc(total);
                if (texels == nil)
                {
                    free(fitted);
                    return false;
                }

                uint16* out = texels;
                uint8* owned = nil; // a level made here, to free
                int32 lw = fw;
                int32 lh = fh;
                for (int32 l = 0; l <= maxLevel; l++)
                {
                    applyEdges(level, lw, lh, edges);
                    for (int32 y = 0; y < lh; y++)
                    {
                        const uint8* row = level + (size_t)y * lw * 4;
                        for (int32 x = 0; x < lw; x++)
                        {
                            const uint8* p = row + x * 4;
                            uint16 v;
                            switch (fmt)
                            {
                            case GPU_RGB565:
                                v = pack565(p);
                                break;
                            case GPU_RGBA5551:
                                v = pack5551(p);
                                break;
                            case GPU_LA8:
                                v = packLA8(p);
                                break;
                            default:
                                v = pack4444(p);
                                break;
                            }
                            out[tiledIndex((uint32)x, (uint32)y, (uint32)lw, (uint32)lh)] = v;
                        }
                    }
                    out += lw * lh;
                    if (l < maxLevel)
                    {
                        int32 nw;
                        int32 nh;
                        uint8* next = halve(level, lw, lh, &nw, &nh);
                        free(owned);
                        owned = next;
                        if (next == nil)
                        {
                            maxLevel = l;
                            break;
                        }
                        level = next;
                        lw = nw;
                        lh = nh;
                    }
                }
                free(owned);
                free(fitted);

                // The chain's size for the levels actually made.
                total = 0;
                for (int32 l = 0; l <= maxLevel; l++)
                {
                    total += (uint32)((fw >> l) * (fh >> l) * 2);
                }

                const bool ok = texCreate(natras, fw, fh, maxLevel, fmt, texels, total);
                free(texels);
                if (!ok)
                {
                    logOnce("texfail", "a %dx%d texture could not be made (memory)", (int)fw,
                            (int)fh);
                    return false;
                }
                // What the top level holds decides blending and the cut-out
                // rule, whatever the levels below were filtered into.
                natras->alphaKind = (uint8)kind;
                // citro3d's defaults are nearest; the state's first bind sets
                // what the texture asks for.
                natras->filter = 0;
                natras->addrU = 0;
                natras->addrV = 0;
                // A picture of another size was stretched onto the texture
                // (fitPow2), not padded, so its coordinates are the texture's.
                natras->uScale = 1.0f;
                natras->vScale = 1.0f;
                return true;
            }

            // A DXT top level's clear edges, from its edge blocks alone: a row
            // of blocks decodes as a w x 4 picture, a column as 4 x h.
            void dxtEdges(ClearEdges* edges, int32 dxt, const uint8* blocks, int32 w, int32 h)
            {
                if (w < 4 || h < 4 || (w & 3) != 0 || (h & 3) != 0)
                {
                    return;
                }
                const int32 bsize = dxt == 1 ? 8 : 16;
                const int32 bw = w / 4;
                const int32 bh = h / 4;
                uint8* strip = (uint8*)malloc((size_t)(bw > bh ? bw : bh) * bsize);
                if (strip == nil)
                {
                    return;
                }
                for (int32 side = 0; side < 4; side++)
                {
                    const bool row = side == EDGE_TOP || side == EDGE_BOTTOM;
                    const int32 n = row ? bw : bh;
                    for (int32 i = 0; i < n; i++)
                    {
                        const int32 b = side == EDGE_TOP    ? i :
                                        side == EDGE_BOTTOM ? (bh - 1) * bw + i :
                                        side == EDGE_LEFT   ? i * bw :
                                                              i * bw + bw - 1;
                        memcpy(strip + (size_t)i * bsize, blocks + (size_t)b * bsize, bsize);
                    }
                    const int32 pw = row ? w : 4;
                    const int32 ph = row ? 4 : h;
                    Image* img = Image::create(pw, ph, 32);
                    img->allocate();
                    img->setPixelsDXT(dxt, strip);
                    takeEdge(edges, side, img->pixels, pw, ph);
                    img->destroy();
                }
                free(strip);
            }

            // The alpha of an Xbox raster's top level (rw/texture.cpp's
            // GlLevel0AlphaKind, for the same reason: a mip chain filters a
            // keyed edge into a graded one), and its clear edges unless
            // `edges` is nil.
            int32 xboxLevel0Kind(Raster* ras, bool paddedRGB, ClearEdges* edges)
            {
                if (paddedRGB)
                {
                    return ALPHAOPAQUE;
                }
                xbox::XboxRaster* xr = GETXBOXRASTEREXT(ras);
                if (xr->customFormat)
                {
                    int32 dxt = xr->format == xbox::D3DFMT_DXT1 ? 1 :
                                xr->format == xbox::D3DFMT_DXT3 ? 3 :
                                xr->format == xbox::D3DFMT_DXT5 ? 5 :
                                                                  0;
                    if (dxt == 0)
                    {
                        return -1;
                    }
                    if (dxt == 1 && !xr->hasAlpha)
                    {
                        return ALPHAOPAQUE;
                    }
                    uint8* blocks = ras->lock(0, Raster::LOCKREAD);
                    int32 kind =
                        blocks != nil ? classifyDXTAlpha(dxt, blocks, ras->width, ras->height) : -1;
                    if (kind > ALPHAOPAQUE && edges != nil)
                    {
                        dxtEdges(edges, dxt, blocks, ras->width, ras->height);
                    }
                    ras->unlock(0);
                    return kind;
                }
                Image* img = ras->toImage();
                if (img == nil)
                {
                    return -1;
                }
                int32 kind = !img->hasAlpha()     ? ALPHAOPAQUE :
                             img->alphaIsBinary() ? ALPHAKEYED :
                                                    ALPHAGRADED;
                if (kind > ALPHAOPAQUE && img->depth == 32 && edges != nil)
                {
                    for (int32 side = 0; side < 4; side++)
                    {
                        takeEdge(edges, side, img->pixels, img->width, img->height);
                    }
                }
                img->destroy();
                return kind;
            }

            // One level of an Xbox raster, decoded to RGBA8.
            uint8* xboxLevelRGBA(Raster* ras, int32 level, bool paddedRGB, int32* w, int32* h)
            {
                if (ras->lock(level, Raster::LOCKREAD) == nil)
                {
                    return nil;
                }
                Image* img = ras->toImage();
                ras->unlock(level);
                if (img == nil)
                {
                    return nil;
                }
                *w = img->width;
                *h = img->height;
                uint8* rgba = imageRGBA(img);
                img->destroy();
                if (rgba != nil && paddedRGB)
                {
                    for (int32 i = 0; i < *w * *h; i++)
                    {
                        rgba[i * 4 + 3] = 255;
                    }
                }
                return rgba;
            }

            // ------------------------------------------------------ plugin

            void* createNativeRaster(void* object, int32 offset, int32)
            {
                C3dRaster* natras = PLUGINOFFSET(C3dRaster, object, offset);
                memset(natras, 0, sizeof(*natras));
                natras->uScale = 1.0f;
                natras->vScale = 1.0f;
                return object;
            }

            void* destroyNativeRaster(void* object, int32 offset, int32)
            {
                Raster* raster = (Raster*)object;
                if (raster->platform != PLATFORM_C3D)
                {
                    return object;
                }
                C3dRaster* natras = PLUGINOFFSET(C3dRaster, object, offset);
                texDestroy(natras);
                free(natras->lockPixels);
                natras->lockPixels = nil;
                free(natras->keep);
                natras->keep = nil;
                return object;
            }

            // A copy of a font atlas's texels for rasterToImage (C3dRaster's
            // `keep`): w x h RGBA8, the raster's own size.
            void keepCopy(C3dRaster* natras, const uint8* rgba, int32 w, int32 h)
            {
                free(natras->keep);
                natras->keep = (uint8*)malloc((size_t)w * h * 4);
                if (natras->keep != nil)
                {
                    memcpy(natras->keep, rgba, (size_t)w * h * 4);
                }
            }

            void* copyNativeRaster(void* dst, void*, int32 offset, int32)
            {
                C3dRaster* natras = PLUGINOFFSET(C3dRaster, dst, offset);
                memset(natras, 0, sizeof(*natras));
                natras->uScale = 1.0f;
                natras->vScale = 1.0f;
                return dst;
            }

            // A picture the port fills by hand, opaque: a movie frame
            // (iFMV.cpp locks and unlocks its raster for every frame it
            // decodes; the movies are 640 x 480, the logo 720 x 486). Kept as
            // the textures are, at half size, which is what the 400 x 240
            // screen shows of it: a 2 x 2 box into RGB565, padded (not
            // stretched) onto the power-of-two texture, 320 x 240 onto 512 x
            // 256, with the raster's uv scale saying how much of it the
            // picture covers. The padding repeats the last column and row, so
            // filtering at the edge finds the picture. The texture is made
            // once and its texels written again in place at every unlock
            // after. `bgra` is the locked level (w x h, top row first).
            // FALSE: not such a picture (it has alpha, it is small, a frame
            // is open, no memory); the caller takes the general way.
            bool streamPicture(C3dRaster* natras, const uint8* bgra, int32 w, int32 h)
            {
                if (w < 16 || h < 16 || inFrame())
                {
                    return false;
                }
                const int32 ow = w / 2;
                const int32 oh = h / 2;
                const int32 tw = pow2Ceil(ow);
                const int32 th = pow2Ceil(oh);
                if (ow > tw || oh > th)
                {
                    return false; // over 1024 even at half size
                }
                const uint32 bytes = (uint32)tw * (uint32)th * 2;

                uint16* out = nil;
                if (natras->hasTex && natras->tex.width == tw && natras->tex.height == th &&
                    natras->tex.fmt == GPU_RGB565 && natras->tex.maxLevel == 0)
                {
                    out = (uint16*)texWriteBegin(natras, bytes);
                }
                uint16* made = nil; // the first frame's texels, for texCreate
                if (out == nil)
                {
                    made = (uint16*)malloc(bytes);
                    if (made == nil)
                    {
                        return false;
                    }
                    out = made;
                }

                // The PICA's texel order (tiledIndex), a row at a time: x's
                // and y's bits within a tile, interleaved.
                static const uint8 kMortonX[8] = { 0, 1, 4, 5, 16, 17, 20, 21 };
                static const uint8 kMortonY[8] = { 0, 2, 8, 10, 32, 34, 40, 42 };
                uint16 row[1024];
                uint32 notOpaque = 0;
                const uint32* src = (const uint32*)bgra;
                for (int32 y = 0; y < th; y++)
                {
                    if (y < oh)
                    {
                        const uint32* r0 = src + (size_t)(2 * y) * w;
                        const uint32* r1 = r0 + w;
                        for (int32 x = 0; x < ow; x++)
                        {
                            // Bytes B, G, R, A: B and R in the halves of one
                            // sum, G and A in the halves of another, four
                            // pixels at a time without overflow.
                            const uint32 a = r0[2 * x];
                            const uint32 b = r0[2 * x + 1];
                            const uint32 c = r1[2 * x];
                            const uint32 d = r1[2 * x + 1];
                            const uint32 br = (a & 0x00FF00FFu) + (b & 0x00FF00FFu) +
                                              (c & 0x00FF00FFu) + (d & 0x00FF00FFu);
                            const uint32 ga = ((a >> 8) & 0x00FF00FFu) + ((b >> 8) & 0x00FF00FFu) +
                                              ((c >> 8) & 0x00FF00FFu) + ((d >> 8) & 0x00FF00FFu);
                            notOpaque |= (ga >> 16) ^ (4u * 255u);
                            row[x] = (uint16)(((br >> 21) << 11) | (((ga & 0xFFFFu) >> 4) << 5) |
                                              ((br & 0xFFFFu) >> 5));
                        }
                        if (notOpaque != 0)
                        {
                            free(made);
                            return false;
                        }
                        for (int32 x = ow; x < tw; x++)
                        {
                            row[x] = row[ow - 1];
                        }
                    }
                    // Below the picture, its last row again (still in row[]).
                    const uint32 fy = (uint32)(th - 1 - y);
                    uint16* line = out + (fy >> 3) * (uint32)(tw >> 3) * 64 + kMortonY[fy & 7];
                    for (int32 x = 0; x < tw; x += 8)
                    {
                        uint16* tile = line + (x >> 3) * 64;
                        for (int32 i = 0; i < 8; i++)
                        {
                            tile[kMortonX[i]] = row[x + i];
                        }
                    }
                }

                if (made != nil)
                {
                    texDestroy(natras);
                    const bool ok = texCreate(natras, tw, th, 0, GPU_RGB565, made, bytes);
                    free(made);
                    if (!ok)
                    {
                        return false;
                    }
                    // citro3d's defaults; the first draw sets what it asks for.
                    natras->filter = 0;
                    natras->addrU = 0;
                    natras->addrV = 0;
                }
                else
                {
                    texWriteEnd(natras, out, bytes);
                }
                natras->alphaKind = ALPHAOPAQUE;
                natras->uScale = (float)ow / (float)tw;
                natras->vScale = (float)oh / (float)th;
                return true;
            }
        } // namespace

        void registerNativeRaster(void)
        {
            nativeRasterOffset =
                Raster::registerPlugin(sizeof(C3dRaster), ID_RASTERC3D, createNativeRaster,
                                       destroyNativeRaster, copyNativeRaster);
        }

        Raster* rasterCreate(Raster* raster)
        {
            C3dRaster* natras = GETC3DRASTEREXT(raster);
            switch (raster->type)
            {
            case Raster::NORMAL:
            case Raster::TEXTURE:
                // The storage comes with the texels: at the first unlock, or
                // from the conversion that made this raster.
                raster->stride = raster->width * 4;
                break;
            case Raster::CAMERATEXTURE:
                natras->isCamera = 1;
                raster->stride = raster->width * 4;
                break;
            case Raster::CAMERA:
            case Raster::ZBUFFER:
                // The top screen and its depth (c3dsubmit.cpp).
                raster->stride = 0;
                break;
            default:
                return nil;
            }
            raster->originalWidth = raster->width;
            raster->originalHeight = raster->height;
            raster->originalStride = raster->stride;
            raster->originalPixels = raster->pixels;
            return raster;
        }

        // A lock hands out the level as D3D's devices do (rows top-down,
        // BGRA), which is what iRasterFill.h tells the port's fillers for any
        // backend but GL3; the unlock makes the texture from it.
        uint8* rasterLock(Raster* raster, int32 level, int32 lockMode)
        {
            C3dRaster* natras = GETC3DRASTEREXT(raster);
            if (raster->type == Raster::CAMERA || raster->type == Raster::ZBUFFER)
            {
                logOnce("lockscreen", "a lock of the screen or its depth was refused");
                return nil;
            }
            if (natras->locked)
            {
                return nil;
            }
            int32 w = raster->width;
            int32 h = raster->height;
            for (int32 i = 0; i < level; i++)
            {
                w = w > 1 ? w / 2 : 1;
                h = h > 1 ? h / 2 : 1;
            }
            // The buffer of the lock before is kept (a movie locks its raster
            // every frame): the texels cannot be read back, so a lock that is
            // not only for writing sees zeros the first time and what was
            // written last after.
            const size_t bytes = (size_t)w * h * 4;
            if (natras->lockPixels == nil || natras->lockBytes < bytes)
            {
                free(natras->lockPixels);
                natras->lockPixels = (uint8*)calloc(bytes, 1);
                natras->lockBytes = natras->lockPixels != nil ? (uint32)bytes : 0;
                if (natras->lockPixels == nil)
                {
                    return nil;
                }
            }
            natras->locked = 1;
            natras->lockLevel = level;
            raster->width = w;
            raster->height = h;
            raster->stride = w * 4;
            raster->pixels = natras->lockPixels;
            raster->privateFlags = lockMode;
            return raster->pixels;
        }

        void rasterUnlock(Raster* raster, int32 level)
        {
            C3dRaster* natras = GETC3DRASTEREXT(raster);
            if (!natras->locked)
            {
                return;
            }
            const int32 w = raster->width;
            const int32 h = raster->height;
            const bool write0 = (raster->privateFlags & Raster::LOCKWRITE) && level == 0 &&
                                natras->lockPixels != nil && !natras->isCamera;
            // A raster the port fills by hand is a movie frame (the fast way
            // above) or a picture made at run time.
            if (write0 && !streamPicture(natras, natras->lockPixels, w, h))
            {
                logOnce("slowpicture",
                        "a picture filled by hand (%dx%d) took the slow way: it has alpha, or a "
                        "frame was open",
                        (int)w, (int)h);
                // BGRA to RGBA, then a texture of its own size (no mips).
                const int32 n = w * h;
                for (int32 i = 0; i < n; i++)
                {
                    uint8* p = natras->lockPixels + i * 4;
                    const uint8 t = p[0];
                    p[0] = p[2];
                    p[2] = t;
                }
                texDestroy(natras);
                buildTexture(natras, natras->lockPixels, w, h, kindOf(natras->lockPixels, w, h),
                             false, false);
            }
            else if ((raster->privateFlags & Raster::LOCKWRITE) && level > 0)
            {
                logOnce("locklevel",
                        "a write to a raster's level %d was dropped (the levels are "
                        "made from level 0)",
                        (int)level);
            }
            if (!write0)
            {
                free(natras->lockPixels);
                natras->lockPixels = nil;
                natras->lockBytes = 0;
            }
            natras->locked = 0;
            raster->width = raster->originalWidth;
            raster->height = raster->originalHeight;
            raster->stride = raster->originalStride;
            raster->pixels = raster->originalPixels;
            raster->privateFlags = 0;
        }

        int32 rasterNumLevels(Raster* raster)
        {
            C3dRaster* natras = GETC3DRASTEREXT(raster);
            return natras->hasTex ? natras->tex.maxLevel + 1 : 1;
        }

        bool32 imageFindRasterFormat(Image* img, int32 type, int32* width, int32* height,
                                     int32* depth, int32* format)
        {
            *width = img->width;
            *height = img->height;
            *depth = 32;
            bool alpha = img->hasAlpha() != 0;
            *format = (type & 0xF) | (alpha ? Raster::C8888 : Raster::C888);
            return 1;
        }

        // An Image into a raster, by the policy above: half size and mips
        // unless the image is paletted (a font atlas, kept whole).
        bool32 rasterFromImage(Raster* raster, Image* image)
        {
            if ((raster->type & 0xF) != Raster::TEXTURE)
            {
                return 0;
            }
            C3dRaster* natras = GETC3DRASTEREXT(raster);
            const bool font = image->depth <= 8;

            Image* copy = Image::create(image->width, image->height, image->depth);
            copy->allocate();
            memcpy(copy->pixels, image->pixels, (size_t)image->stride * image->height);
            if (image->palette != nil && copy->palette != nil)
            {
                memcpy(copy->palette, image->palette, (size_t)(image->depth == 4 ? 16 : 256) * 4);
            }
            uint8* rgba = imageRGBA(copy);
            copy->destroy();
            if (rgba == nil)
            {
                return 0;
            }

            int32 w = image->width;
            int32 h = image->height;
            const int32 kind = kindOf(rgba, w, h);
            if (font && w == raster->width && h == raster->height)
            {
                keepCopy(natras, rgba, w, h);
            }
            ClearEdges edges = {};
            if (!font && kind > ALPHAOPAQUE)
            {
                for (int32 side = 0; side < 4; side++)
                {
                    takeEdge(&edges, side, rgba, w, h);
                }
            }
            uint8* use = rgba;
            uint8* half = nil;
            if (!font && (w > 8 || h > 8))
            {
                half = halve(rgba, w, h, &w, &h);
                if (half != nil)
                {
                    use = half;
                }
                else
                {
                    w = image->width;
                    h = image->height;
                }
            }
            texDestroy(natras);
            const bool ok = buildTexture(natras, use, w, h, kind, font,
                                         (raster->format & Raster::MIPMAP) != 0, &edges);
            edgesFree(&edges);
            free(half);
            free(rgba);
            return ok ? 1 : 0;
        }

        // The screen, for the frame grab (rw/framegrab.cpp through
        // iSnapshotCopyFrame); textures cannot be read back.
        Image* rasterToImage(Raster* raster)
        {
            if (raster->type == Raster::CAMERATEXTURE)
            {
                return rttToImage(raster); // TSSM_DUMPSHADOW
            }
            if (raster->type != Raster::CAMERA)
            {
                // A font atlas: the copy kept when it was read.
                C3dRaster* natras = GETC3DRASTEREXT(raster);
                if (natras->keep != nil && raster->width > 0 && raster->height > 0)
                {
                    Image* img = Image::create(raster->width, raster->height, 32);
                    img->allocate();
                    for (int32 y = 0; y < raster->height; y++)
                    {
                        memcpy(img->pixels + (size_t)y * img->stride,
                               natras->keep + (size_t)y * raster->width * 4,
                               (size_t)raster->width * 4);
                    }
                    return img;
                }
                logOnce("toimage", "a texture cannot be read back on the 3DS (only font atlases)");
                return nil;
            }
            Image* img = Image::create(SCREEN_W, SCREEN_H, 32);
            img->allocate();
            if (!readScreen(img->pixels, SCREEN_W, SCREEN_H))
            {
                img->destroy();
                return nil;
            }
            return img;
        }

        Raster* convertXboxRaster(Raster* xboxRas, int paddedRGB)
        {
            if (xboxRas == nil || xboxRas->platform != PLATFORM_XBOX || !submitIsOpen())
            {
                return nil;
            }
            const bool padded = paddedRGB != 0;
            const bool font = (xboxRas->format & (Raster::PAL8 | Raster::PAL4)) != 0;
            const int32 numLevels = xboxRas->getNumLevels();
            const int32 w0 = xboxRas->width;
            const int32 h0 = xboxRas->height;

            ClearEdges edges = {};
            int32 kind = xboxLevel0Kind(xboxRas, padded, font ? nil : &edges);
            if (kind < 0)
            {
                edgesFree(&edges);
                return nil;
            }

            // The level to keep: the source's own level 1 when it has one
            // (decoding it is a quarter of the work and needs no filter),
            // level 0 halved when it has not, level 0 for a font.
            int32 w = 0;
            int32 h = 0;
            uint8* rgba = nil;
            if (font || (w0 <= 8 && h0 <= 8))
            {
                rgba = xboxLevelRGBA(xboxRas, 0, padded, &w, &h);
            }
            else if (numLevels >= 2 && w0 >= 16 && h0 >= 16)
            {
                rgba = xboxLevelRGBA(xboxRas, 1, padded, &w, &h);
            }
            else
            {
                int32 fw;
                int32 fh;
                uint8* full = xboxLevelRGBA(xboxRas, 0, padded, &fw, &fh);
                if (full != nil)
                {
                    rgba = halve(full, fw, fh, &w, &h);
                    free(full);
                }
            }
            if (rgba == nil)
            {
                edgesFree(&edges);
                return nil;
            }

            int32 format = (kind == ALPHAOPAQUE ? Raster::C888 : Raster::C8888) |
                           (xboxRas->format & Raster::MIPMAP);
            Raster* ras = Raster::create(
                w0, h0, 32, format | Raster::TEXTURE | Raster::DONTALLOCATE, PLATFORM_C3D);
            if (ras == nil)
            {
                free(rgba);
                edgesFree(&edges);
                return nil;
            }
            C3dRaster* natras = GETC3DRASTEREXT(ras);
            if (font && w == w0 && h == h0)
            {
                keepCopy(natras, rgba, w, h);
            }
            // Mips where the source has them: art without a chain is drawn
            // at its own size (menus, the HUD).
            const bool ok =
                buildTexture(natras, rgba, w, h, kind, font, !font && numLevels > 1, &edges);
            free(rgba);
            edgesFree(&edges);
            if (!ok)
            {
                ras->destroy();
                return nil;
            }
            if (sTexLogged < 3 && getenv("TSSM_C3D_TEXLOG") != nil)
            {
                sTexLogged++;
                printf("tssm: c3d: texture %dx%d -> %dx%d fmt %d kind %d levels %d\n", (int)w0,
                       (int)h0, (int)natras->tex.width, (int)natras->tex.height,
                       (int)natras->tex.fmt, (int)kind, (int)natras->tex.maxLevel + 1);
            }
            return ras;
        }
    } // namespace c3d
} // namespace rw
