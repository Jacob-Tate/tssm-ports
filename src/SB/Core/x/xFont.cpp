#include "xFont.h"

#include "xCounter.h"
#include "xString.h"
#include "xTextAsset.h"
#include "xTimer.h"
#include "zMain.h"
#include "zScene.h"

#define FLAG_UNK40 0x40
#define FLAG_UNK80 0x80

#define WRAP_WORD 0x0
#define WRAP_CHAR 0x10
#define WRAP_NONE 0x20
#define WRAP_MASK (WRAP_WORD | WRAP_CHAR | WRAP_NONE)

#define XJUSTIFY_LEFT 0x0
#define XJUSTIFY_RIGHT 0x1
#define XJUSTIFY_CENTER 0x2
#define XJUSTIFY_MASK (XJUSTIFY_LEFT | XJUSTIFY_RIGHT | XJUSTIFY_CENTER)

#define YJUSTIFY_TOP 0x0
#define YJUSTIFY_BOTTOM 0x4
#define YJUSTIFY_CENTER 0x8
#define YJUSTIFY_MASK (YJUSTIFY_TOP | YJUSTIFY_BOTTOM | YJUSTIFY_CENTER)

extern F32 gRealTimeElapsed;

namespace
{
    struct font_asset
    {
        U32 tex_id;
        U16 u;
        U16 v;
        U8 du;
        U8 dv;
        U8 line_size;
        U8 baseline;
        struct
        {
            S16 x;
            S16 y;
        } space;
        U32 flags;
        F32 pixel_shear;
        U8 char_set[161];
        struct
        {
            U8 offset;
            U8 size;
        } char_pos[160];
    };

    struct font_data
    {
        font_asset* asset;
        U32 index_max;
        U8 char_index[256];
        U8 fixed_width_character;
        F32 iwidth;
        F32 iheight;
        basic_rect<F32> tex_bounds[160];
        basic_rect<F32> bounds[160];
        xVec2 dstfrac[160];
        RwTexture* texture;
        RwRaster* raster;
    };

    struct model_cache_entry
    {
        U32 id;
        U32 order;
        xModelInstance* model;
    };

    // clang-format off
    const char* default_font_texture[] =
    {
        "font_sb",
        "font1_sb",
        "font_numbers"
    };

    font_asset default_font_assets[4] =
    {
        // font1_sb (SpongeBob font)
        1, 0, 0, 18, 22, 14, 0, 0, 0, 0x1, 0.0f,
        {
            '~', '{', '}', '#', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
            'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
            'Y', 'Z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '?', '!',
            '.', ',', '-', ':', '\xAE', '"', '\'', '&', '(', ')', '<', '>', '/', '%',
            '\xFC', '\xFB', '\xF9', '\xE2', '\xE4', '\xE0', '\xEA', '\xE8', '\xE9',
            '\xEE', '\xF6', '\xF4', '\xE7', '\xDF', '+', '\xDC', '\xDB', '\xD9',
            '\xC2', '\xC4', '\xC0', '\xCA', '\xC8', '\xC9', '\xCE', '\xD6', '\xD4',
            '\xC7'
        },
        {},

        // font_sb (Arial font)
        0, 0, 0, 18, 22, 14, 0, 1, 0, 0, 0.0f,
        {
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
            'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b',
            'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
            'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '0', '1', '2', '3',
            '4', '5', '6', '7', '8', '9', '?', '!', '.', ',', ';', ':', '+', '-',
            '=', '/', '&', '(', ')', '%', '"', '\'', '_', '<', '>', '*', '[', ']',
            '\xDC', '\xDB', '\xD9', '\xC2', '\xC4', '\xC0', '\xCA', '\xC8', '\xC9',
            '\xCE', '\xD6', '\xD4', '\xC7', '\xDF', '\xFC', '\xFB', '\xF9', '\xE2',
            '\xE4', '\xE0', '\xEA', '\xE8', '\xE9', '\xEE', '\xF6', '\xF4', '\xE7',
            '~', '\xA9', '\xAE', '\x99', '@', '|'
        },
        {},

        // font1_sb (System font)
        1, 148, 232, 6, 8, 18, 0, 0, 0, 0x9, 0.0f,
        {
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D',
            'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R',
            'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', ',', '.', '/', '*', '+', '-',
            '=', ':', ';', '%', '<', '>', '[', ']', '|', '(', ')', '_'
        },
        {},

        // font_numbers (SpongeBob numbers font)
        2, 0, 0, 32, 32, 4, 0, 0, 0, 0, 0.0f,
        {
            '1', '2', '3', '4', '5', '6', '7', '8', '9', '0'
        },
        {}
    };
    // clang-format on

    font_data active_fonts[4];
    U32 active_fonts_size;

#define CODEPAGE_TEXTURES_SIZE 16

    RwTexture* codepage_textures[CODEPAGE_TEXTURES_SIZE];
    U32 codepage_textures_size;

#define VERT_BUFFER_SIZE 240

    RwIm2DVertex vert_buffer[VERT_BUFFER_SIZE];
    U32 vert_buffer_used = 0;
    F32 rcz = 0;
    F32 nsz = 0;

#define MODEL_CACHE_SIZE 8

    model_cache_entry model_cache[MODEL_CACHE_SIZE];
    bool model_cache_inited = false;

    struct
    {
        S32 fogenable;
        S32 vertexalphaenable;
        S32 zwriteenable;
        S32 ztestenable;
        U32 srcblend;
        U32 destblend;
        U32 shademode;
        RwTextureFilterMode filter;
    } oldrs;

    const substr text_delims = { " \t\n{}=*+:;,", 11 };

    struct tex_args
    {
        RwRaster* raster;
        F32 rot;
        basic_rect<F32> src;
        basic_rect<F32> dst;
        xVec2 off;
        enum
        {
            SCALE_FONT,
            SCALE_SCREEN,
            SCALE_SIZE,
            SCALE_FONT_WIDTH,
            SCALE_FONT_HEIGHT,
            SCALE_SCREEN_WIDTH,
            SCALE_SCREEN_HEIGHT
        } scale;
    };

    struct model_args
    {
        xModelInstance* model;
        xVec3 rot;
        basic_rect<F32> dst;
        xVec2 off;
        enum
        {
            SCALE_FONT,
            SCALE_SCREEN,
            SCALE_SIZE
        } scale;
    };

    tex_args def_tex_args;
    model_args def_model_args;

    inline void tex_flush()
    {
        if (vert_buffer_used)
        {
            RwIm2DRenderPrimitive(rwPRIMTYPETRILIST, vert_buffer, vert_buffer_used);
            vert_buffer_used = 0;
        }
    }

    void tex_render(const basic_rect<F32>& src, const basic_rect<F32>& dst,
                    const basic_rect<F32>& clip, iColor_tag color);

    void tex_render_shadowed(const basic_rect<F32>& src, const basic_rect<F32>& dst,
                             const basic_rect<F32>& clip, xColor_tag color, xColor_tag shadowcolor,
                             F32 shadowOffsetX, F32 shadowOffsetY, F32 fshear);

    inline RwRaster* set_tex_raster(RwRaster* raster)
    {
        RwRaster* r;
        RwRenderStateGet(rwRENDERSTATETEXTURERASTER, (void*)&r);

        if (raster == r)
        {
            return raster;
        }

        tex_flush();
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)raster);

        return r;
    }

    inline void start_tex_render(U32 font_id)
    {
        rcz = 1.0f / RwCameraGetNearClipPlane(RwCameraGetCurrentCamera());
        nsz = RwIm2DGetNearScreenZ();

        font_data& fd = active_fonts[font_id];
        xfont::set_render_state(fd.raster);
    }

    inline void stop_tex_render()
    {
        tex_flush();
        xfont::restore_render_state();
    }

    basic_rect<S32> find_bounds(const xColor_tag* bits, const basic_rect<S32>& r, S32 pitch)
    {
        S32 diff = pitch - r.w;
        const xColor_tag* endp = bits + pitch * r.h;
        const xColor_tag* p = bits;
        S32 pmode = (p->r == p->g && p->g == p->b && p->r >= 240);

        S32 minx = r.x + r.w;
        S32 maxx = r.x - 1;
        S32 miny = r.y + r.h;
        S32 maxy = r.y - 1;

        S32 y = r.y;

        while (p != endp)
        {
            const xColor_tag* endline = p + r.w;
            S32 x = r.x;

            while (p != endline)
            {
                if ((pmode && p->a) || (!pmode && p->r))
                {
                    minx = MIN(x, minx);
                    maxx = MAX(x, maxx);
                    miny = MIN(y, miny);
                    maxy = MAX(y, maxy);
                }

                p++;
                x++;
            }

            p += diff;
            y++;
        }

        basic_rect<S32> b;
        b.x = minx;
        b.y = miny;
        b.w = maxx + 1 - minx;
        b.h = maxy + 1 - miny;

        return b;
    }

    bool reset_font_spacing(font_asset& a)
    {
        RwTexture* tex = (RwTexture*)xSTFindAsset(a.tex_id, NULL);

        if (!tex)
        {
            return false;
        }

        basic_rect<S32> char_bounds;
        char_bounds.w = a.du;
        char_bounds.h = a.dv;

        U8 baseline_count[256];
        memset(baseline_count, 0, 256);

        a.baseline = 0;

        S32 width = RwRasterGetWidth(RwTextureGetRaster(tex));
        RwImage* image = RwImageCreate(width, RwRasterGetHeight(RwTextureGetRaster(tex)), 32);

        if (!image)
        {
            return false;
        }

        RwImageAllocatePixels(image);
        RwImageSetFromRaster(image, RwTextureGetRaster(tex));

        xColor_tag* bits = (xColor_tag*)RwImageGetPixels(image);

        for (S32 i = 0; a.char_set[i] != '\0'; i++)
        {
            if (a.flags & 0x4)
            {
                char_bounds.x = a.u + char_bounds.w * (i / a.line_size);
                char_bounds.y = a.v + char_bounds.h * (i % a.line_size);
            }
            else
            {
                char_bounds.x = a.u + char_bounds.w * (i % a.line_size);
                char_bounds.y = a.v + char_bounds.h * (i / a.line_size);
            }

            basic_rect<S32> r =
                find_bounds(bits + width * char_bounds.y + char_bounds.x, char_bounds, width);

            if (a.flags & 0x8)
            {
                a.char_pos[i].offset = 0;
                a.char_pos[i].size = char_bounds.w;
            }
            else
            {
                a.char_pos[i].offset = r.x - char_bounds.x;
                a.char_pos[i].size = r.w;
            }

            S32 baseline = r.y - char_bounds.y + r.h + 1;

            if (++baseline_count[baseline] > baseline_count[a.baseline])
            {
                a.baseline = baseline;
            }
        }

        RwImageDestroy(image);
        return true;
    }

    // No symbol in the target (always inlined); name from the Ratatouille GC DWARF.
    // 2-byte characters: a lead byte >= 0x80 holds the high 7 bits of the character code.
    inline U32 nextCharacter(const char*& stream)
    {
        U8 ch1 = *stream++;

        if (ch1 < 0x80)
        {
            return ch1;
        }

        U8 ch2 = *stream++;
        return ((ch1 & 0x7F) << 7) + (ch2 & 0x7F);
    }

    // No symbol in the target (always inlined); name from the Ratatouille GC DWARF.
    inline U32 getCharacter(const char* stream)
    {
        return nextCharacter(stream);
    }

    // No symbol in the target (always inlined into init_font_data).
    inline basic_rect<F32> get_tex_bounds(const font_data& fd, U8 i)
    {
        const font_asset& a = *fd.asset;
        basic_rect<F32> r;

        if (a.flags & 0x4)
        {
            r.x = i / a.line_size;
            r.y = i % a.line_size;
        }
        else
        {
            r.x = i % a.line_size;
            r.y = i / a.line_size;
        }

        r.x = a.char_pos[i].offset + (a.du * r.x + a.u);
        r.w = a.char_pos[i].size - 0.5f;
        r.y = a.dv * r.y + a.v;
        r.h = a.dv - 0.5f;

        r.scale(fd.iwidth, fd.iheight);

        return r;
    }

    // No symbol in the target (always inlined into init_font_data).
    inline basic_rect<F32> get_bounds(const font_data& fd, U8 i)
    {
        const font_asset& a = *fd.asset;
        basic_rect<F32> r;

        r.x = 0.0f;
        r.y = (F32)-a.baseline / a.dv;
        r.w = (F32)(a.char_pos[i].size + a.space.x) / (a.du + a.space.x);
        r.h = 1.0f;

        return r;
    }

    bool init_font_data(font_data& fd)
    {
        font_asset& a = *fd.asset;
        S32 height;
        U8 i;
        U8 c;
        U32 tail_index;

        fd.texture = (RwTexture*)xSTFindAsset(a.tex_id, NULL);

        if (!fd.texture)
        {
            return false;
        }

        RwTextureSetFilterMode(fd.texture, rwFILTERLINEAR);
        fd.raster = fd.texture->raster;

        height = fd.raster->height;

        fd.iwidth = 1.0f / fd.raster->width;
        fd.iheight = 1.0f / height;

        memset(fd.char_index, 0xFF, sizeof(fd.char_index));

        fd.index_max = 0;

        while (a.char_set[fd.index_max] != '\0')
        {
            i = fd.index_max;
            c = a.char_set[i];

            fd.char_index[c] = i;

            if (a.flags & 0x1)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    fd.char_index[c - 'A' + 'a'] = i;
                }
                else if (c >= 0xC0 && c <= 0xDF)
                {
                    fd.char_index[c + 0x20] = i;
                }
            }
            else if (a.flags & 0x2)
            {
                if (c >= 'a' && c <= 'z')
                {
                    fd.char_index[c - 'a' + 'A'] = i;
                }
                else if (c >= 0xE0 && c <= 0xFE)
                {
                    fd.char_index[c - 0x20] = i;
                }
            }

            fd.tex_bounds[i] = get_tex_bounds(fd, i);
            fd.bounds[i] = get_bounds(fd, i);
            fd.dstfrac[i].x = (F32)a.char_pos[i].size / (a.char_pos[i].size + a.space.x);
            fd.dstfrac[i].y = (F32)a.dv / (a.dv + a.space.y);

            fd.index_max++;
        }

        tail_index = fd.index_max;

        if (fd.char_index[' '] == 0xFF)
        {
            fd.char_index[' '] = tail_index;
            fd.tex_bounds[tail_index].assign(0.0f, 0.0f, 0.0f, 0.0f);
            fd.bounds[tail_index].assign(0.0f, (F32)-a.baseline / a.dv,
                                         (a.flags & 0x8) ? 1.0f : 0.5f, 1.0f);
            tail_index++;
        }

        if (fd.char_index['\n'] == 0xFF)
        {
            fd.char_index['\n'] = tail_index;
            fd.tex_bounds[tail_index].assign(0.0f, 0.0f, 0.0f, 0.0f);
            fd.bounds[tail_index].assign(0.0f, (F32)-a.baseline / a.dv, 0.0f, 1.0f);
            tail_index++;
        }

        fd.fixed_width_character = tail_index;
        fd.tex_bounds[fd.fixed_width_character].assign(0.0f, 0.0f, 0.0f, 0.0f);
        fd.bounds[fd.fixed_width_character].assign(0.0f, (F32)-a.baseline / a.dv, 1.0f, 1.0f);

        return true;
    }

    // No symbol in the target (always inlined into xfont::init).
    inline void init_model_cache()
    {
        struct model_pool
        {
            RwMatrix mat[MODEL_CACHE_SIZE];
            xModelInstance model[MODEL_CACHE_SIZE];
        };

        model_cache_inited = true;

        void* data = xMemAlloc(gActiveHeap, sizeof(model_pool), 16);
        memset(data, 0, sizeof(model_pool));

        model_pool& pool = *(model_pool*)data;

        for (U32 i = 0; i < MODEL_CACHE_SIZE; i++)
        {
            xModelInstance& model = pool.model[i];
            model_cache_entry& e = model_cache[i];

            e.order = 0;
            e.id = 0;
            e.model = &model;

            model.Mat = &pool.mat[i];
            model.Flags = 0x1;
            model.BoneCount = 1;
            model.shadowID = 0xDEADBEEF;
        }
    }

    // No symbol in the target: auto-inlined into its only caller, load_model_args.
    xModelInstance* load_model(U32 id)
    {
        static U32 next_order = 0;
        U32 oldest = 0;

        next_order++;

        for (U32 i = 0; i < MODEL_CACHE_SIZE; i++)
        {
            model_cache_entry& e = model_cache[i];

            if (e.id == id)
            {
                e.order = next_order;
                return e.model;
            }

            if (e.order < model_cache[oldest].order)
            {
                oldest = i;
            }
        }

        RpAtomic* mf = (RpAtomic*)xSTFindAsset(id, NULL);

        if (!mf)
        {
            return NULL;
        }

        model_cache_entry& e = model_cache[oldest];

        e.id = id;
        e.order = next_order;

        xModelInstance& model = *e.model;

        xMat4x3Identity((xMat4x3*)model.Mat);

        model.Data = mf;
        model.Bucket = xModelBucket_GetBuckets(model.Data);
        model.Pipe = model.Bucket ? model.Bucket[0]->Pipe : xModelGetPipe(model.Data);

        return &model;
    }

    // No symbol in the target (always inlined).
    inline void set_vert(RwIm2DVertex& vert, F32 x, F32 y, F32 u, F32 v, xColor_tag c)
    {
        RwIm2DVertexSetScreenX(&vert, x);
        RwIm2DVertexSetScreenY(&vert, y);
        RwIm2DVertexSetScreenZ(&vert, nsz);
        RwIm2DVertexSetU(&vert, u, rcz);
        RwIm2DVertexSetV(&vert, v, rcz);
        RwIm2DVertexSetIntRGBA(&vert, c.r, c.g, c.b, c.a);
    }

    void tex_render(const basic_rect<F32>& src, const basic_rect<F32>& dst,
                    const basic_rect<F32>& clip, xColor_tag color)
    {
        basic_rect<F32> r = dst;
        basic_rect<F32> rt = src;

        if (r.empty())
        {
            return;
        }

        clip.clip(r, rt);

        if (r.empty())
        {
            return;
        }

        if (vert_buffer_used == VERT_BUFFER_SIZE)
        {
            RwIm2DRenderPrimitive(rwPRIMTYPETRILIST, vert_buffer, VERT_BUFFER_SIZE);
            vert_buffer_used = 0;
        }

        RwIm2DVertex* vert = &vert_buffer[vert_buffer_used];

        r.scale(FB_XRES, FB_YRES);

        set_vert(vert[0], r.x, r.y, rt.x, rt.y, color);
        set_vert(vert[1], r.x, r.y + r.h, rt.x, rt.y + rt.h, color);
        set_vert(vert[2], r.x + r.w, r.y, rt.x + rt.w, rt.y, color);

        vert[3] = vert[2];
        vert[4] = vert[1];

        set_vert(vert[5], r.x + r.w, r.y + r.h, rt.x + rt.w, rt.y + rt.h, color);

        vert_buffer_used += 6;
    }

    void tex_render_shadowed(const basic_rect<F32>& src, const basic_rect<F32>& dst,
                             const basic_rect<F32>& clip, xColor_tag color, xColor_tag shadowcolor,
                             F32 shadowOffsetX, F32 shadowOffsetY, F32 fshear)
    {
        basic_rect<F32> r = dst;
        basic_rect<F32> rt = src;

        clip.clip(r, rt);

        if (r.empty())
        {
            return;
        }

        if (vert_buffer_used == VERT_BUFFER_SIZE)
        {
            RwIm2DRenderPrimitive(rwPRIMTYPETRILIST, vert_buffer, VERT_BUFFER_SIZE);
            vert_buffer_used = 0;
        }

        RwIm2DVertex* vert = &vert_buffer[vert_buffer_used];

        r.scale(FB_XRES, FB_YRES);

        set_vert(vert[6], r.x + fshear, r.y, rt.x, rt.y, color);
        set_vert(vert[7], r.x - fshear, r.y + r.h, rt.x, rt.y + rt.h, color);
        set_vert(vert[8], fshear + (r.x + r.w), r.y, rt.x + rt.w, rt.y, color);

        vert[9] = vert[8];
        vert[10] = vert[7];

        set_vert(vert[11], (r.x + r.w) - fshear, r.y + r.h, rt.x + rt.w, rt.y + rt.h, color);

        vert[0] = vert[6];
        vert[1] = vert[7];
        vert[2] = vert[8];
        vert[3] = vert[9];
        vert[4] = vert[10];
        vert[5] = vert[11];

        vert[0].x += shadowOffsetX;
        vert[0].y += shadowOffsetY;

        vert[1].x += shadowOffsetX;
        vert[1].y += shadowOffsetY;

        vert[2].x += shadowOffsetX;
        vert[2].y += shadowOffsetY;

        vert[3].x += shadowOffsetX;
        vert[3].y += shadowOffsetY;

        vert[4].x += shadowOffsetX;
        vert[4].y += shadowOffsetY;

        vert[5].x += shadowOffsetX;
        vert[5].y += shadowOffsetY;

        RwIm2DVertexSetIntRGBA(&vert[0], shadowcolor.r, shadowcolor.g, shadowcolor.b,
                               shadowcolor.a);

        RwIm2DVertexCopyRGBA(&vert[1], &vert[0]);
        RwIm2DVertexCopyRGBA(&vert[2], &vert[0]);
        RwIm2DVertexCopyRGBA(&vert[3], &vert[0]);
        RwIm2DVertexCopyRGBA(&vert[4], &vert[0]);
        RwIm2DVertexCopyRGBA(&vert[5], &vert[0]);

        vert_buffer_used += 12;
    }

    inline void start_layout(const xtextbox&);

    inline void stop_layout(const xtextbox&);

    inline void start_render(const xtextbox& tb);

    inline void stop_render(const xtextbox& tb);

} // namespace

#define min(a, b) ((a) >= (b) ? (b) : (a))

void xfont::init()
{
    active_fonts_size = 0;

    for (U32 i = 0; i < sizeof(default_font_assets) / sizeof(font_asset); i++)
    {
        if (default_font_assets[i].tex_id < sizeof(default_font_texture) / sizeof(const char*))
        {
            default_font_assets[i].tex_id =
                xStrHash(default_font_texture[default_font_assets[i].tex_id]);
        }

        if (reset_font_spacing(default_font_assets[i]))
        {
            font_data& fd = active_fonts[active_fonts_size];
            fd.asset = &default_font_assets[i];

            if (init_font_data(fd))
            {
                active_fonts_size++;
            }
        }
    }

    char filename[20];
    sprintf(filename, "lfont_%i", codepage_textures_size);
    codepage_textures[codepage_textures_size] = (RwTexture*)xSTFindAsset(xStrHash(filename), NULL);

    // Retail bug: once all CODEPAGE_TEXTURES_SIZE slots are filled, the lookup
    // for the next codepage still stores one entry past the end of the array.
    while (codepage_textures_size < CODEPAGE_TEXTURES_SIZE &&
           codepage_textures[codepage_textures_size])
    {
        RwTextureSetFilterMode(codepage_textures[codepage_textures_size], rwFILTERLINEAR);
        codepage_textures_size++;
        sprintf(filename, "lfont_%i", codepage_textures_size);
        codepage_textures[codepage_textures_size] =
            (RwTexture*)xSTFindAsset(xStrHash(filename), NULL);
    }

    init_model_cache();
}

// No symbol in the target (always inlined).
inline basic_rect<F32> xfont::bounds(U32 c) const
{
    if (c < 0x100)
    {
        return bounds((char)c);
    }

    if (c == 0x3FFF)
    {
        basic_rect<F32> r = { 0.0f, 0.0f, 0.0f, 0.0f };
        return r;
    }

    const font_data& fd = active_fonts[id];
    basic_rect<F32> r = fd.bounds[fd.fixed_width_character];
    r.scale(width, height);
    r.w += space;
    return r;
}

inline basic_rect<F32> xfont::bounds(char c) const
{
    const font_data& fd = active_fonts[id];
    U32 i = fd.char_index[(U8)c];

    if (i == 0xFF)
    {
        return basic_rect<F32>::m_Null;
    }

    basic_rect<F32> r = fd.bounds[i];
    r.scale(width, height);
    r.w += space;
    return r;
}

void xfont::set_render_state(RwRaster* raster)
{
    RwRenderStateGet(rwRENDERSTATEFOGENABLE, (void*)&oldrs.fogenable);
    RwRenderStateGet(rwRENDERSTATESRCBLEND, (void*)&oldrs.srcblend);
    RwRenderStateGet(rwRENDERSTATEDESTBLEND, (void*)&oldrs.destblend);
    RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)&oldrs.vertexalphaenable);
    RwRenderStateGet(rwRENDERSTATESHADEMODE, (void*)&oldrs.shademode);
    RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, (void*)&oldrs.zwriteenable);
    RwRenderStateGet(rwRENDERSTATEZTESTENABLE, (void*)&oldrs.ztestenable);
    RwRenderStateGet(rwRENDERSTATETEXTUREFILTER, (void*)&oldrs.filter);

    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)rwSHADEMODEFLAT);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)raster);
}

void xfont::restore_render_state()
{
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)oldrs.fogenable);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)oldrs.srcblend);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)oldrs.destblend);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)oldrs.vertexalphaenable);
    RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)oldrs.shademode);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)oldrs.zwriteenable);
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)oldrs.ztestenable);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)oldrs.filter);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
}

basic_rect<F32> xfont::bounds(const char* text) const
{
    size_t size;
    return bounds(text, 0x40000000, 1e38f, size);
}

basic_rect<F32> xfont::bounds(const char* text, size_t text_size, F32 max_width, size_t& size) const
{
    const font_data& fd = active_fonts[id];
    basic_rect<F32> r;

    r.x = 0.0f;
    r.y = fd.bounds[0].y * height;
    r.w = 0.0f;
    r.h = fd.bounds[0].h * height;

    if (!text || !text_size)
    {
        size = 0;
        return r;
    }

    const char* s = text;

    while (s - text < text_size && *s != '\0')
    {
        F32 dx;
        U32 charIndex = nextCharacter(s);

        if (charIndex == 0x3FFF)
        {
            continue;
        }

        if (charIndex < 0x100)
        {
            charIndex = fd.char_index[charIndex];

            if (charIndex == 0xFF)
            {
                continue;
            }

            dx = fd.bounds[charIndex].w * width;
        }
        else
        {
            const font_data& fd = active_fonts[id];
            dx = fd.bounds[fd.fixed_width_character].w * width;
        }

        if (r.w + dx > max_width)
        {
            break;
        }

        r.w += dx + space;
    }

    if (r.w > 0.0f)
    {
        r.w -= space;
    }

    size = s - text;
    return r;
}

void xfont::start_render() const
{
    start_tex_render(id);
}

void xfont::stop_render() const
{
    stop_tex_render();
}

// Dead-stripped placeholder, not recovered code: retail's .rodata has an unreferenced 16-byte
// zero template here, left by a stripped single-character irender(U32 c, F32 x, F32 y) (unused
// in Ratatouille's link map) whose basic_rect bounds initializer is all that survives.
static void __deadstripped_xFont()
{
    basic_rect<F32> bounds = { 0.0f, 0.0f, 0.0f, 0.0f };
}

void xfont::irender(const char* text, F32 x, F32 y) const
{
    irender(text, 0x40000000, x, y);
}

namespace
{
    // No symbol in the target (always inlined); signature from the Ratatouille GC DWARF.
    inline void char_render(U8 c, U32 font_index, const basic_rect<F32>& bounds,
                            const basic_rect<F32>& clip, xColor_tag color, xColor_tag shadowColor,
                            F32 shadowOffsetX, F32 shadowOffsetY)
    {
        const font_data& fd = active_fonts[font_index];
        U32 i = fd.char_index[c];

        if (i >= fd.index_max)
        {
            return;
        }

        basic_rect<F32> dest = fd.bounds[i];
        dest.scale(bounds.w, bounds.h);
        dest.x += bounds.x;
        dest.y += bounds.y;
        dest.w *= fd.dstfrac[i].x;
        dest.h *= fd.dstfrac[i].y;

        F32 fShear = fd.asset->pixel_shear * (dest.h * FB_YRES / fd.asset->dv);

        tex_render_shadowed(fd.tex_bounds[i], dest, clip, color, shadowColor, shadowOffsetX,
                            shadowOffsetY, fShear);
    }

    // No symbol in the target (always inlined); signature from the Ratatouille GC DWARF.
    // 2-byte characters live on codepage textures of 12x12 cells, 21 texels each (21/256).
    inline void mchar_render(U32 c, U32 font_index, basic_rect<F32> bounds,
                             const basic_rect<F32>& clip, xColor_tag color, xColor_tag shadowColor,
                             F32 shadowOffsetX, F32 shadowOffsetY)
    {
        U32 offset = (c - 0x100) % 144;

        basic_rect<F32> src;
        src.x = 0.08203125f * (offset % 12);
        src.y = 0.08203125f * (offset / 12);
        src.w = 0.08203125f;
        src.h = 0.08203125f;

        const font_data& fd = active_fonts[font_index];
        basic_rect<F32> dest = fd.bounds[fd.fixed_width_character];
        dest.scale(bounds.w, bounds.h);
        dest.x += bounds.x;
        dest.y += bounds.y;

        tex_render_shadowed(src, dest, clip, color, shadowColor, shadowOffsetX, shadowOffsetY,
                            0.0f);
    }
} // namespace

void xfont::irender(const char* text, size_t text_size, F32 x, F32 y) const
{
    if (!text)
    {
        return;
    }

    font_data& fd = active_fonts[id];
    basic_rect<F32> bounds = { x, y, width, height };

    U8 pageCompleted[CODEPAGE_TEXTURES_SIZE];
    memset(pageCompleted, 0, sizeof(pageCompleted));

    // Render the 2-byte characters one codepage texture at a time.
    for (U32 cpItr = 0; cpItr < codepage_textures_size; cpItr++)
    {
        bounds.x = x;
        S32 whichCodePage = -1;
        const char* s = text;

        while (s - text < text_size && *s != '\0')
        {
            U32 c = nextCharacter(s);

            if (c == 0x3FFF)
            {
                continue;
            }

            if (c >= 0x100)
            {
                U32 codePage = (c - 0x100) / 144;

                if (whichCodePage == -1 && !pageCompleted[codePage])
                {
                    whichCodePage = codePage;
                    set_tex_raster(RwTextureGetRaster(codepage_textures[codePage]));
                }

                if (whichCodePage == codePage)
                {
                    mchar_render(c, id, bounds, clip, color, shadowColor, shadowOffsetX,
                                 shadowOffsetY);
                }

                bounds.x += fd.bounds[fd.fixed_width_character].w * width + space;
            }
            else
            {
                U32 charIndex = fd.char_index[c];

                if (charIndex != 0xFF)
                {
                    bounds.x += fd.bounds[charIndex].w * width + space;
                }
            }
        }

        if (whichCodePage == -1)
        {
            break;
        }

        pageCompleted[whichCodePage] = true;
    }

    // Then the 1-byte characters from the font texture.
    bounds.x = x;
    set_tex_raster(fd.raster);

    const char* s = text;

    while (s - text < text_size && *s != '\0')
    {
        U32 c = nextCharacter(s);

        if (c == 0x3FFF)
        {
            continue;
        }

        if (c >= 0x100)
        {
            bounds.x += fd.bounds[fd.fixed_width_character].w * width + space;
        }
        else
        {
            char_render(c, id, bounds, clip, color, shadowColor, shadowOffsetX, shadowOffsetY);

            U32 charIndex = fd.char_index[c];

            if (charIndex != 0xFF)
            {
                bounds.x += fd.bounds[charIndex].w * width + space;
            }
        }
    }
}

namespace
{
    // Each inlined is_ws re-extends the character (clrlwi) before its second compare, as in retail;
    // that comes from the cpp_extensions pragma around skip_ws/rskip_ws in xString.h.
    size_t parse_split_tag(xtextbox::split_tag& ti)
    {
        ti.value.size = 0;
        ti.action.size = 0;
        ti.name.size = 0;

        substr s = ti.tag;

        s.text++;
        s.size--;

        ti.name.text = skip_ws(s);
        s.text = find_char(s, text_delims);

        if (!s.text)
        {
            return 0;
        }

        ti.name.size = s.text - ti.name.text;
        s.size -= ti.name.size;
        ti.action.text = skip_ws(s);

        if (!s.size)
        {
            return 0;
        }

        char c = s.text[0];

        if (c == '\0' || c == '{')
        {
            return 0;
        }

        s.text++;
        s.size--;

        if (c == '}')
        {
            return ti.tag.size - s.size;
        }

        ti.action.size = 1;
        ti.value.text = skip_ws(s);
        s.text = find_char(s, '}');
        s.size -= s.text - ti.value.text;

        if (!s.text)
        {
            return 0;
        }

        ti.value.size = s.text - ti.value.text;

        rskip_ws(ti.value);

        s.text++;
        s.size--;

        return ti.tag.size - s.size;
    }

    inline const char* parse_next_tag_jot(xtextbox::jot& a, const xtextbox& tb, const xtextbox& ctb,
                                          const char* text, size_t text_size)
    {
        xtextbox::split_tag ti = {};
        ti.tag.text = text;
        ti.tag.size = text_size;

        size_t size = parse_split_tag(ti);

        if (!size)
        {
            return NULL;
        }

        a.s.text = text;
        a.s.size = size;
        a.flag.invisible = a.flag.ethereal = true;

        if (icompare(ti.name, substr::create("~", 1)) == 0 ||
            icompare(ti.name, substr::create("reset", 5)) == 0)
        {
            a.tag = xtextbox::find_format_tag(ti.value);

            if (a.tag && a.tag->reset_tag)
            {
                a.tag->reset_tag(a, tb, ctb, ti);
            }
        }
        else
        {
            a.tag = xtextbox::find_format_tag(ti.name);

            if (a.tag && a.tag->parse_tag)
            {
                a.tag->parse_tag(a, tb, ctb, ti);
            }
        }

        return text + size;
    }

    const char* parse_next_text_jot(xtextbox::jot& a, const xtextbox& tb, const xtextbox& ctb,
                                    const char* text, size_t text_size)
    {
        U32 c = getCharacter(text);
        bool hasSpaceChar = false;

        if (xtextbox::japanese_word_breaking && c >= 0x100)
        {
            if (text_size >= 6 && getCharacter(text + 2) == 0x3FFF)
            {
                hasSpaceChar = true;
            }
            else
            {
                a.flag.japanese_break = true;
            }
        }

        a.s.text = text;
        a.flag.merge = true;
        a.cb = &xtextbox::text_cb;
        a.context = NULL;
        a.context_size = 0;

        if (hasSpaceChar)
        {
            // The character and the following space character form one jot.
            a.s.size = 6;

            size_t size;
            a.bounds = tb.font.bounds(a.s.text, 6, 1e38f, size);
        }
        else
        {
            a.s.size = (c < 0x80) ? 1 : 2;
            a.bounds = tb.font.bounds(c);
        }

        switch (c)
        {
        case '\n':
            a.flag.line_break = true;
            a.flag.invisible = a.flag.word_break = true;
            break;
        case '\t':
            a.flag.tab = true;
            a.flag.invisible = a.flag.word_break = true;
            break;
        case '-':
            a.flag.word_end = true;
            break;
        case ' ':
            a.flag.invisible = a.flag.word_break = true;
            break;
        }

        return a.s.text + a.s.size;
    }

    inline const char* parse_next_jot(xtextbox::jot& a, const xtextbox& tb, const xtextbox& ctb,
                                      const char* text, size_t text_size)
    {
        const char* ret;

        if (text[0] != '{' || !(ret = parse_next_tag_jot(a, tb, ctb, text, text_size)))
        {
            ret = parse_next_text_jot(a, tb, ctb, text, text_size);
        }

        a.flag.merge = a.flag.merge && !(tb.flags & FLAG_UNK80);
        return ret;
    }

    inline void reset_tex_args(tex_args& ta)
    {
        ta.raster = NULL;
        ta.rot = 0.0f;
        ta.src = ta.dst = basic_rect<F32>::m_Unit;
        ta.off.x = ta.off.y = 1.0f;
        ta.scale = tex_args::SCALE_FONT;
    }

    void load_tex_args(tex_args& ta, const substr& s)
    {
        xtextbox::tag_entry_list el = xtextbox::read_tag(s);
        xtextbox::tag_entry* e;

        if (el.size == 0)
        {
            return;
        }

        e = &el.entries[0];

        if (e->op == ':' || (e->args_size == 1 && e->args[0].size))
        {
            substr& name = e->args[0];
            U32 id;

            if (name.size == 10 && imemcmp(name.text, "0x", 2) == 0)
            {
                id = atox(substr::create(name.text + 2, 8));
            }
            else
            {
                id = xStrHash(name.text, name.size);
            }

            RwTexture* texture = (RwTexture*)xSTFindAsset(id, NULL);

            if (texture && texture->raster && texture->raster->width > 0 &&
                texture->raster->height > 0 && texture->raster->width <= 4096 &&
                texture->raster->height <= 4096)
            {
                ta.raster = texture->raster;
            }
        }

        el.entries++;
        el.size--;

        e = xtextbox::find_entry(el, substr::create("rot", 3));

        if (e && e->op == '=' && e->args_size == 1)
        {
            xtextbox::read_list(*e, &ta.rot, 1);
        }

        e = xtextbox::find_entry(el, substr::create("src", 3));

        if (e && e->op == '=' && e->args_size == 4)
        {
            xtextbox::read_list(*e, (F32*)&ta.src, 4);
        }

        e = xtextbox::find_entry(el, substr::create("dst", 3));

        if (e && e->op == '=' && e->args_size == 4)
        {
            xtextbox::read_list(*e, (F32*)&ta.dst, 4);
        }

        e = xtextbox::find_entry(el, substr::create("off", 3));

        if (e && e->op == '=' && e->args_size == 2)
        {
            xtextbox::read_list(*e, (F32*)&ta.off, 2);
        }

        e = xtextbox::find_entry(el, substr::create("scale", 5));

        if (e && e->op == '=' && e->args_size == 1)
        {
            if (icompare(e->args[0], substr::create("font", 4)) == 0)
            {
                ta.scale = tex_args::SCALE_FONT;
            }
            else if (icompare(e->args[0], substr::create("screen", 6)) == 0)
            {
                ta.scale = tex_args::SCALE_SCREEN;
            }
            else if (icompare(e->args[0], substr::create("size", 4)) == 0)
            {
                ta.scale = tex_args::SCALE_SIZE;
            }
            else if (icompare(e->args[0], substr::create("font_width", 10)) == 0)
            {
                ta.scale = tex_args::SCALE_FONT_WIDTH;
            }
            else if (icompare(e->args[0], substr::create("font_height", 11)) == 0)
            {
                ta.scale = tex_args::SCALE_FONT_HEIGHT;
            }
            else if (icompare(e->args[0], substr::create("screen_width", 12)) == 0)
            {
                ta.scale = tex_args::SCALE_SCREEN_WIDTH;
            }
            else if (icompare(e->args[0], substr::create("screen_height", 13)) == 0)
            {
                ta.scale = tex_args::SCALE_SCREEN_HEIGHT;
            }
            else
            {
                ta.scale = tex_args::SCALE_FONT;
            }
        }
    }

    inline void reset_model_args(model_args& ma)
    {
        ma.model = NULL;
        ma.rot = xVec3::m_Null;
        ma.dst = basic_rect<F32>::m_Unit;
        ma.off.x = ma.off.y = 1.0f;
        ma.scale = model_args::SCALE_FONT;
    }

    void load_model_args(model_args& ma, const substr& s)
    {
        xtextbox::tag_entry_list el = xtextbox::read_tag(s);
        xtextbox::tag_entry* e;

        if (!el.size)
        {
            return;
        }

        e = &el.entries[0];

        if (e->op == ':' || (e->args_size == 1 && e->args[0].size))
        {
            substr& name = e->args[0];
            U32 id = xStrHash(name.text, name.size);

            ma.model = load_model(id);
        }

        el.entries++;
        el.size--;

        e = xtextbox::find_entry(el, substr::create("rot", 3));

        if (e && e->op == '=' && e->args_size == 3)
        {
            xtextbox::read_list(*e, (F32*)&ma.rot, 3);
        }

        e = xtextbox::find_entry(el, substr::create("dst", 3));

        if (e && e->op == '=' && e->args_size == 4)
        {
            xtextbox::read_list(*e, (F32*)&ma.dst, 4);
        }

        e = xtextbox::find_entry(el, substr::create("off", 3));

        if (e && e->op == '=' && e->args_size == 2)
        {
            xtextbox::read_list(*e, (F32*)&ma.off, 2);
        }

        e = xtextbox::find_entry(el, substr::create("scale", 5));

        if (e && e->op == '=' && e->args_size == 1)
        {
            if (icompare(e->args[0], substr::create("screen", 6)) == 0)
            {
                ma.scale = model_args::SCALE_SCREEN;
            }
            else
            {
                ma.scale = model_args::SCALE_FONT;
            }
        }
    }
} // namespace

void xtextbox::text_render(const jot& j, const xtextbox& tb, F32 x, F32 y)
{
    tb.font.irender(j.s.text, j.s.size, x, y);
}

bool xtextbox::japanese_word_breaking;

const xtextbox::callback xtextbox::text_cb = { xtextbox::text_render, NULL, NULL };

void xtextbox::set_text(const char* text)
{
    set_text(text, 0x40000000);
}

void xtextbox::set_text(const char** texts, size_t size)
{
    set_text(texts, NULL, size);
}

namespace
{
    struct tl_cache_entry
    {
        U32 used;
        iTime last_used;
        xtextbox::layout tl;
    };

#define TL_CACHE_COUNT 1

    tl_cache_entry tl_cache[TL_CACHE_COUNT];
} // namespace

xtextbox::layout& xtextbox::temp_layout(bool cache) const
{
    // not in the DWARF: one second of timer ticks; a compile-time constant on PS2 (folded away),
    // but on GC OS_TIMER_CLOCK is read at run time and retail computes it first, before iTimeGet.
    iTime max_time = (iTime)(1.0f * OS_TIMER_CLOCK);
    iTime cur_time = iTimeGet();
    bool refresh = false;
    U32 index = 0;

    if (cache)
    {
        for (; index < TL_CACHE_COUNT; index++)
        {
            if (!tl_cache[index].tl.changed(*this))
            {
                break;
            }
        }
    }
    else
    {
        index = TL_CACHE_COUNT;
    }

    if (index >= TL_CACHE_COUNT)
    {
        S32 min_used = 1000000000;

        refresh = true;
        index = 0;

        for (U32 i = 0; i < TL_CACHE_COUNT; i++)
        {
            if (cur_time - tl_cache[i].last_used > max_time)
            {
                index = i;
                break;
            }

            S32 used = tl_cache[i].used;

            if (tl_cache[i].tl.dynamics_size)
            {
                used -= 10;
            }

            if (tl_cache[i].tl.jots_size() > 50)
            {
                used += 10;
            }

            if (used < min_used)
            {
                min_used = used;
                index = i;
            }
        }
    }

    tl_cache_entry& e = tl_cache[index];

    if (refresh)
    {
        e.used = 0;
        e.tl.refresh(*this, true);
    }
    else
    {
        e.tl.tb = *this;
    }

    if (cache)
    {
        e.used++;
        e.last_used = cur_time;
    }

    return e.tl;
}

void xtextbox::render(layout& l, S32 begin_jot, S32 end_jot) const
{
    l.render(*this, begin_jot, end_jot);
}

F32 xtextbox::yextent(F32 max, S32& size, const layout& l, S32 begin_jot, S32 end_jot) const
{
    return l.yextent(max, size, begin_jot, end_jot);
}

// The four inlined is_ws (via trim_ws) re-extend the char like parse_split_tag's (see the
// cpp_extensions pragma in xString.h).
xtextbox::tag_entry_list xtextbox::read_tag(const substr& s)
{
    static substr arg_buffer[32];
    static tag_entry entry_buffer[16];

    U32 entries_used = 0;
    U32 args_used = 0;
    substr it = s;

    const char* d = find_char(it, '{');

    if (d)
    {
        it.size -= d + 1 - it.text;
        it.text = d + 1;
    }

    substr delims = { "=:+*;}", 6 };
    substr sub_delims = { ",;}", 3 };

    while (it.size)
    {
        tag_entry& entry = entry_buffer[entries_used];

        entry.args_size = 0;
        entry.op = 0;
        entry.name.text = it.text;

        const char* d = find_char(it, delims);

        entry.name.size = (!d) ? it.size : d - it.text;

        trim_ws(entry.name);

        if (entry.name.size)
        {
            entries_used++;
        }

        if (!d || *d == '}')
        {
            break;
        }

        it.size -= d + 1 - it.text;
        it.text = d + 1;

        if (*d != ';')
        {
            entry.op = *d;
            entry.args = &arg_buffer[args_used];

            while (it.size)
            {
                substr& arg = arg_buffer[args_used];

                arg.text = it.text;

                const char* d = find_char(it, sub_delims);

                if (!d)
                {
                    arg.size = it.size;
                    it.size = 0;
                }
                else
                {
                    arg.size = d - it.text;
                    it.size -= arg.size + 1;
                    it.text += arg.size + 1;
                }

                trim_ws(arg);

                if (arg.size)
                {
                    args_used++;
                    entry.args_size++;
                }

                // Retail reads *d here before the NULL check below.
                if (*d == ';')
                {
                    break;
                }

                if (!d || *d == '}')
                {
                    it.size = 0;
                    break;
                }
            }
        }
    }

    tag_entry_list ret = { entry_buffer, 0 };
    ret.size = entries_used;

    return ret;
}

xtextbox::tag_entry* xtextbox::find_entry(const tag_entry_list& el, const substr& name)
{
    for (size_t i = 0; i < el.size; i++)
    {
        tag_entry& e = el.entries[i];

        if (icompare(name, e.name) == 0)
        {
            return &e;
        }
    }

    return NULL;
}

size_t xtextbox::read_list(const tag_entry& e, F32* v, size_t vsize)
{
    size_t total = vsize < e.args_size ? vsize : e.args_size;

    for (size_t i = 0; i < total; i++)
    {
        v[i] = xatof(e.args[i].text);
    }

    return total;
}

size_t xtextbox::read_list(const tag_entry& e, S32* v, size_t vsize)
{
    size_t total = vsize < e.args_size ? vsize : e.args_size;

    for (size_t i = 0; i < total; i++)
    {
        v[i] = atoi(e.args[i].text);
    }

    return total;
}

void xtextbox::clear_layout_cache()
{
    for (U32 index = 0; index < TL_CACHE_COUNT; index++)
    {
        tl_cache[index].tl.clear();
    }
}

void xtextbox::layout::refresh(const xtextbox& tb, bool force)
{
    if (force || changed(tb))
    {
        this->tb = tb;
        calc(tb, 0);
    }
}

void xtextbox::layout::clear()
{
    dynamics_size = 0;
    context_buffer_size = 0;
    _lines_size = 0;
    _jots_size = 0;
    tb = xtextbox::create();
}

void xtextbox::layout::refresh_end(const xtextbox& tb)
{
    size_t start_text = this->tb.texts_size;

    if (start_text > tb.texts_size)
    {
        this->tb = tb;
        calc(tb, start_text);
    }
}

void xtextbox::layout::trim_line(jot_line& line)
{
    for (S32 i = line.last - 1; i >= (S32)line.first; i--)
    {
        jot& a = _jots[i];

        if (!a.flag.ethereal)
        {
            if (a.flag.invisible)
            {
                erase_jots(i, (size_t)i + 1);
                line.last--;
            }

            break;
        }
    }

    for (size_t i = line.first; i < line.last; i++)
    {
        jot& a = _jots[i];

        if (!a.flag.ethereal)
        {
            if (a.flag.invisible)
            {
                erase_jots(i, i + 1);
                line.last--;
            }

            break;
        }
    }
}

void xtextbox::layout::erase_jots(size_t first, size_t last)
{
    if (last >= _jots_size)
    {
        _jots_size = first;
        return;
    }

    size_t offset = last - first;

    _jots_size -= offset;

    for (size_t i = first; i < _jots_size; i++)
    {
        jot& a = _jots[i];
        a = _jots[i + offset];
    }
}

void xtextbox::layout::merge_line(jot_line& line)
{
    size_t d = line.first;
    size_t i = line.first + 1;

    while (i != line.last)
    {
        jot& a1 = _jots[d];
        jot& a2 = _jots[i];

        if (!a1.flag.ethereal && !a2.flag.ethereal && a1.flag.merge && a2.flag.merge &&
            a1.cb == a2.cb)
        {
            a1.s.size = a2.s.text - a1.s.text + a2.s.size;
            a1.bounds |= a2.bounds;
            a1.intersect_flags(a2);
        }
        else
        {
            d++;

            if (d != i)
            {
                _jots[d] = a2;
            }
        }

        i++;
    }

    erase_jots(d + 1, line.last);
    line.last = d + 1;
}

bool xtextbox::layout::fit_line(bool isLastLine)
{
    jot_line& line = _lines[_lines_size];

    if (line.bounds.w > tb.bounds.w)
    {
        switch (tb.flags & WRAP_MASK)
        {
        case WRAP_CHAR:
        {
            if (line.last > line.first + 1)
            {
                line.last--;
            }

            break;
        }
        case WRAP_NONE:
        {
            merge_line(line);
            bound_line(line);
            return false;
        }
        default:
        {
            for (S32 i = line.last - 1; i > (S32)line.first; i--)
            {
                if (_jots[i].flag.word_break)
                {
                    line.last = i + 1;
                    break;
                }

                if (_jots[i - 1].flag.word_end)
                {
                    line.last = i;
                    break;
                }

                if (_jots[i].flag.japanese_break)
                {
                    line.last = i;
                    break;
                }
            }

            if (line.last <= line.first)
            {
                line.last = line.first + 1;
            }

            trim_line(line);
            break;
        }
        }
    }

    if (isLastLine)
    {
        trim_line(line);
    }

    merge_line(line);
    bound_line(line);

    return true;
}

void xtextbox::layout::calc(const xtextbox& ctb, size_t start_text)
{
    if (!start_text)
    {
        dynamics_size = 0;
        context_buffer_size = 0;
        _lines_size = 0;
        _jots_size = 0;
    }

    if (!tb.texts_size)
    {
        return;
    }

    start_layout(ctb);

    jot_line& first_line = _lines[_lines_size];
    first_line.first = 0;
    first_line.bounds.w = 0.0f;
    first_line.bounds.x = 0.0f;
    first_line.bounds.y = 0.0f;
    first_line.baseline = 0.0f;

    struct
    {
        const char* s;
        const char* end;
    } text_stack[16];

    size_t text_stack_size = 0;
    size_t text_size = ((!tb.text_sizes) ? 0x40000000 : tb.text_sizes[start_text]);
    size_t text_index = start_text + 1;
    const char* s = tb.texts[start_text];
    const char* end = s + text_size;

    for (;;)
    {
        jot& a = _jots[_jots_size];
        jot_line& line = _lines[_lines_size];

        a.context = &context_buffer[context_buffer_size];
        a.context_size = 0;
        a.reset_flags();
        a.cb = NULL;
        a.tag = NULL;

        if (s == end || *s == '\0')
        {
            if (text_stack_size)
            {
                text_stack_size--;
                s = text_stack[text_stack_size].s;
                end = text_stack[text_stack_size].end;
            }
            else if (text_index < tb.texts_size)
            {
                text_size = ((!tb.text_sizes) ? 0x40000000 : tb.text_sizes[text_index]);

                s = tb.texts[text_index];
                end = s + text_size;
                text_index++;
            }
            else
            {
                break;
            }

            a.flag.invisible = a.flag.ethereal = true;
            a.s = substr::create(NULL, 0);

            _jots_size++;
        }
        else
        {
            const char* next = parse_next_jot(a, tb, ctb, s, end - s);

            if (a.context == &context_buffer[context_buffer_size])
            {
                context_buffer_size += ALIGN(a.context_size, 4);
            }

            _jots_size++;

            if (a.cb && a.cb->layout_update)
            {
                a.cb->layout_update(a, tb, ctb);
            }

            if (a.flag.stop)
            {
                break;
            }

            if (!a.flag.ethereal)
            {
                a.bounds.x += line.bounds.w;
                line.bounds.w += a.bounds.w;

                if (line.bounds.w >= tb.bounds.w)
                {
                    line.last = _jots_size;

                    if (!fit_line(false))
                    {
                        break;
                    }

                    next_line();
                }
            }

            if (a.flag.line_break || a.flag.page_break)
            {
                line.last = _jots_size;

                if (!fit_line(false))
                {
                    break;
                }

                next_line();
            }

            s = next;

            if (a.flag.insert)
            {
                s = (const char*)a.context;
                text_stack[text_stack_size].s = next;
                text_stack[text_stack_size].end = end;
                text_stack_size++;
                end = s + a.context_size;
            }
        }
    }

    jot_line& last_line = _lines[_lines_size];

    if (last_line.first < _jots_size)
    {
        last_line.last = _jots_size;

        fit_line(true);
        _lines_size++;
    }

    for (size_t i = 0; i < _jots_size; i++)
    {
        if (_jots[i].flag.dynamic)
        {
            dynamics[dynamics_size++] = i;
        }
    }

    stop_layout(ctb);
}

void xtextbox::layout::render(const xtextbox& ctb, S32 begin_jot, S32 end_jot)
{
    if (begin_jot < 0)
    {
        begin_jot = 0;
    }

    if (end_jot < begin_jot)
    {
        end_jot = _jots_size;
    }

    if (begin_jot >= end_jot)
    {
        return;
    }

    tb = ctb;
    start_render(ctb);

    S32 begin_line = 0;

    while (true)
    {
        if (begin_line >= (S32)_lines_size)
        {
            stop_render(ctb);
            return;
        }

        if ((S32)_lines[begin_line].last > begin_jot)
        {
            break;
        }

        begin_line++;
    }

    for (S32 i = 0; i < begin_jot; i++)
    {
        jot& j = _jots[i];

        if (j.cb && j.cb->render_update)
        {
            j.cb->render_update(j, tb, ctb);
        }
    }

    F32 top = _lines[begin_line].bounds.y;
    U32 li = begin_line - 1;
    S32 line_last = -1;
    F32 x = 0.0f;
    F32 y = 0.0f;

    for (S32 i = begin_jot; i < end_jot; i++)
    {
        if (i >= line_last)
        {
            li++;
            jot_line& line = _lines[li];

            line_last = line.last;

            x = tb.bounds.x + line.bounds.x;
            y = tb.bounds.y + line.bounds.y + line.baseline - top;
            y += li * tb.line_space;

            U32 xj = tb.flags & XJUSTIFY_MASK;

            if (xj == XJUSTIFY_CENTER)
            {
                x += 0.5f * (tb.bounds.w - line.bounds.w);
            }
            else if (xj == XJUSTIFY_RIGHT)
            {
                x += tb.bounds.w - line.bounds.w;
            }

            U32 yj = tb.flags & YJUSTIFY_MASK;

            if (yj == YJUSTIFY_CENTER)
            {
                y += 0.5f * (tb.bounds.h - _lines_size * line.bounds.h -
                             (_lines_size - 1) * tb.line_space);
            }
            else if (yj == YJUSTIFY_BOTTOM)
            {
                y += tb.bounds.h - _lines_size * line.bounds.h - (_lines_size - 1) * tb.line_space;
            }

            if (line.page_break && end_jot > line_last)
            {
                end_jot = line_last;
            }
        }

        jot& j = _jots[i];

        if (j.cb)
        {
            if (j.cb->render_update)
            {
                j.cb->render_update(j, tb, ctb);
            }

            if (!j.flag.ethereal && !j.flag.invisible && j.cb->render)
            {
                j.cb->render(j, tb, x + j.bounds.x, y);
            }
        }
    }

    stop_render(ctb);
}

F32 xtextbox::layout::yextent(F32 max, S32& size, S32 begin_jot, S32 end_jot) const
{
    size = 0;

    if (begin_jot < 0)
    {
        begin_jot = 0;
    }

    if (end_jot < begin_jot)
    {
        end_jot = _jots_size;
    }

    if (begin_jot >= end_jot)
    {
        return 0.0f;
    }

    S32 begin_line = 0;

    while (true)
    {
        if (begin_line >= (S32)_lines_size)
        {
            return 0.0f;
        }

        if ((S32)_lines[begin_line].last > begin_jot)
        {
            break;
        }

        begin_line++;
    }

    F32 top = _lines[begin_line].bounds.y;
    S32 i = begin_line;

    max += top;

    while (true)
    {
        if (i == (S32)_lines_size)
        {
            break;
        }

        const jot_line& line = _lines[i];

        if (line.bounds.y + line.bounds.h > max)
        {
            i--;
            break;
        }

        if ((S32)line.last >= end_jot)
        {
            break;
        }

        if (line.page_break)
        {
            break;
        }

        i++;
    }

    if (i < begin_line)
    {
        return 0.0f;
    }

    const jot_line& line = _lines[i];

    size = min((S32)line.last, end_jot) - begin_jot;

    return line.bounds.y + line.bounds.h - top;
}

bool xtextbox::layout::changed(const xtextbox& ctb)
{
    U32 flags1 = tb.flags & (WRAP_MASK | FLAG_UNK40);
    U32 flags2 = ctb.flags & (WRAP_MASK | FLAG_UNK40);

    if (tb.text_hash != ctb.text_hash || tb.font.id != ctb.font.id ||
        tb.font.width != ctb.font.width || tb.font.height != ctb.font.height ||
        tb.font.space != ctb.font.space || tb.bounds.w != ctb.bounds.w || flags1 != flags2 ||
        tb.line_space != ctb.line_space)
    {
        return true;
    }

    S32 i = dynamics_size;

    while (i > 0)
    {
        i--;

        jot& j = _jots[dynamics[i]];
        U32 oldval = xStrHash((char*)j.context, j.context_size);

        parse_next_jot(j, tb, ctb, j.s.text, j.s.size);

        U32 val = xStrHash((char*)j.context, j.context_size);

        if (val != oldval)
        {
            return true;
        }
    }

    return false;
}

namespace
{
    inline void start_layout(const xtextbox&)
    {
        reset_tex_args(def_tex_args);
        reset_model_args(def_model_args);
    }

    inline void stop_layout(const xtextbox&)
    {
    }

    inline void start_render(const xtextbox& tb)
    {
        tb.font.start_render();
    }

    inline void stop_render(const xtextbox& tb)
    {
        tb.font.stop_render();
    }
} // namespace

namespace
{
    void update_tag_alpha(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.font.color.a = 255.0f * (F32&)j.context + 0.5f;
    }

    void update_tag_reset_alpha(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.font.color.a = ctb.font.color.a;
    }

    void parse_tag_alpha(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                         const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_alpha, update_tag_alpha };
        F32& v = (F32&)a.context;

        if (ti.value.size == 0 || ti.action.size == 0)
        {
            return;
        }

        v = xatof(ti.value.text);

        switch (ti.action.text[0])
        {
        case '=':
        {
            break;
        }
        case '+':
        {
            v += (1.0f / 255.0f) * tb.font.color.a;
            break;
        }
        case '*':
        {
            v *= (1.0f / 255.0f) * tb.font.color.a;
            break;
        }
        default:
        {
            return;
        }
        }

        if (v < 0.0f)
        {
            v = 0.0f;
        }
        else if (v > 1.0f)
        {
            v = 1.0f;
        }

        a.cb = &cb;
    }

    void reset_tag_alpha(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                         const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_alpha,
                                               update_tag_reset_alpha };
        a.cb = &cb;
    }

    void update_tag_red(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.font.color.r = 255.0f * (F32&)j.context + 0.5f;
    }

    void update_tag_reset_red(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.font.color.r = ctb.font.color.r;
    }

    void parse_tag_red(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                       const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_red, update_tag_red };
        F32& v = (F32&)a.context;

        if (ti.value.size == 0 || ti.action.size == 0)
        {
            return;
        }

        v = xatof(ti.value.text);

        switch (ti.action.text[0])
        {
        case '=':
        {
            break;
        }
        case '+':
        {
            v += (1.0f / 255.0f) * tb.font.color.r;
            break;
        }
        case '*':
        {
            v *= (1.0f / 255.0f) * tb.font.color.r;
            break;
        }
        default:
        {
            return;
        }
        }

        if (v < 0.0f)
        {
            v = 0.0f;
        }
        else if (v > 1.0f)
        {
            v = 1.0f;
        }

        a.cb = &cb;
    }

    void reset_tag_red(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                       const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_red, update_tag_reset_red };
        a.cb = &cb;
    }

    void update_tag_green(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.font.color.g = 255.0f * (F32&)j.context + 0.5f;
    }

    void update_tag_reset_green(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.font.color.g = ctb.font.color.g;
    }

    void parse_tag_green(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                         const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_green, update_tag_green };
        F32& v = (F32&)a.context;

        if (ti.value.size == 0 || ti.action.size == 0)
        {
            return;
        }

        v = xatof(ti.value.text);

        switch (ti.action.text[0])
        {
        case '=':
        {
            break;
        }
        case '+':
        {
            v += (1.0f / 255.0f) * tb.font.color.g;
            break;
        }
        case '*':
        {
            v *= (1.0f / 255.0f) * tb.font.color.g;
            break;
        }
        default:
        {
            return;
        }
        }

        if (v < 0.0f)
        {
            v = 0.0f;
        }
        else if (v > 1.0f)
        {
            v = 1.0f;
        }

        a.cb = &cb;
    }

    void reset_tag_green(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                         const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_green,
                                               update_tag_reset_green };
        a.cb = &cb;
    }

    void update_tag_blue(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.font.color.b = 255.0f * (F32&)j.context + 0.5f;
    }

    void update_tag_reset_blue(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.font.color.b = ctb.font.color.b;
    }

    void parse_tag_blue(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                        const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_blue, update_tag_blue };
        F32& v = (F32&)a.context;

        if (ti.value.size == 0 || ti.action.size == 0)
        {
            return;
        }

        v = xatof(ti.value.text);

        switch (ti.action.text[0])
        {
        case '=':
        {
            break;
        }
        case '+':
        {
            v += (1.0f / 255.0f) * tb.font.color.b;
            break;
        }
        case '*':
        {
            v *= (1.0f / 255.0f) * tb.font.color.b;
            break;
        }
        default:
        {
            return;
        }
        }

        if (v < 0.0f)
        {
            v = 0.0f;
        }
        else if (v > 1.0f)
        {
            v = 1.0f;
        }

        a.cb = &cb;
    }

    void reset_tag_blue(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                        const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_blue, update_tag_reset_blue };
        a.cb = &cb;
    }

    void update_tag_salpha(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.font.shadowColor.a = 255.0f * (F32&)j.context + 0.5f;
    }

    void update_tag_reset_salpha(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.font.shadowColor.a = ctb.font.shadowColor.a;
    }

    void parse_tag_salpha(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                          const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_salpha, update_tag_salpha };
        F32& v = (F32&)a.context;

        if (ti.value.size == 0 || ti.action.size == 0)
        {
            return;
        }

        v = xatof(ti.value.text);

        switch (ti.action.text[0])
        {
        case '=':
        {
            break;
        }
        case '+':
        {
            v += (1.0f / 255.0f) * tb.font.shadowColor.a;
            break;
        }
        case '*':
        {
            v *= (1.0f / 255.0f) * tb.font.shadowColor.a;
            break;
        }
        default:
        {
            return;
        }
        }

        if (v < 0.0f)
        {
            v = 0.0f;
        }
        else if (v > 1.0f)
        {
            v = 1.0f;
        }

        a.cb = &cb;
    }

    void reset_tag_salpha(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                          const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_salpha,
                                               update_tag_reset_salpha };
        a.cb = &cb;
    }

    void update_tag_sred(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.font.shadowColor.r = 255.0f * (F32&)j.context + 0.5f;
    }

    void update_tag_reset_sred(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.font.shadowColor.r = ctb.font.shadowColor.r;
    }

    void parse_tag_sred(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                        const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_sred, update_tag_sred };
        F32& v = (F32&)a.context;

        if (ti.value.size == 0 || ti.action.size == 0)
        {
            return;
        }

        v = xatof(ti.value.text);

        switch (ti.action.text[0])
        {
        case '=':
        {
            break;
        }
        case '+':
        {
            v += (1.0f / 255.0f) * tb.font.shadowColor.r;
            break;
        }
        case '*':
        {
            v *= (1.0f / 255.0f) * tb.font.shadowColor.r;
            break;
        }
        default:
        {
            return;
        }
        }

        if (v < 0.0f)
        {
            v = 0.0f;
        }
        else if (v > 1.0f)
        {
            v = 1.0f;
        }

        a.cb = &cb;
    }

    void reset_tag_sred(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                        const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_sred, update_tag_reset_sred };
        a.cb = &cb;
    }

    void update_tag_sgreen(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.font.shadowColor.g = 255.0f * (F32&)j.context + 0.5f;
    }

    void update_tag_reset_sgreen(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.font.shadowColor.g = ctb.font.shadowColor.g;
    }

    void parse_tag_sgreen(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                          const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_sgreen, update_tag_sgreen };
        F32& v = (F32&)a.context;

        if (ti.value.size == 0 || ti.action.size == 0)
        {
            return;
        }

        v = xatof(ti.value.text);

        switch (ti.action.text[0])
        {
        case '=':
        {
            break;
        }
        case '+':
        {
            v += (1.0f / 255.0f) * tb.font.shadowColor.g;
            break;
        }
        case '*':
        {
            v *= (1.0f / 255.0f) * tb.font.shadowColor.g;
            break;
        }
        default:
        {
            return;
        }
        }

        if (v < 0.0f)
        {
            v = 0.0f;
        }
        else if (v > 1.0f)
        {
            v = 1.0f;
        }

        a.cb = &cb;
    }

    void reset_tag_sgreen(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                          const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_sgreen,
                                               update_tag_reset_sgreen };
        a.cb = &cb;
    }

    void update_tag_sblue(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.font.shadowColor.b = 255.0f * (F32&)j.context + 0.5f;
    }

    void update_tag_reset_sblue(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.font.shadowColor.b = ctb.font.shadowColor.b;
    }

    void parse_tag_sblue(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                         const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_sblue, update_tag_sblue };
        F32& v = (F32&)a.context;

        if (ti.value.size == 0 || ti.action.size == 0)
        {
            return;
        }

        v = xatof(ti.value.text);

        switch (ti.action.text[0])
        {
        case '=':
        {
            break;
        }
        case '+':
        {
            v += (1.0f / 255.0f) * tb.font.shadowColor.b;
            break;
        }
        case '*':
        {
            v *= (1.0f / 255.0f) * tb.font.shadowColor.b;
            break;
        }
        default:
        {
            return;
        }
        }

        if (v < 0.0f)
        {
            v = 0.0f;
        }
        else if (v > 1.0f)
        {
            v = 1.0f;
        }

        a.cb = &cb;
    }

    void reset_tag_sblue(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                         const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_sblue,
                                               update_tag_reset_sblue };
        a.cb = &cb;
    }

    void update_tag_width(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.font.width = (F32&)j.context;
    }

    void update_tag_reset_width(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.font.width = ctb.font.width;
    }

    void parse_tag_width(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                         const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_width, update_tag_width };
        F32& v = (F32&)a.context;

        if (ti.value.size == 0 || ti.action.size == 0)
        {
            return;
        }

        v = xatof(ti.value.text);

        switch (ti.action.text[0])
        {
        case '=':
        {
            break;
        }
        case '+':
        {
            v += tb.font.width;
            break;
        }
        case '*':
        {
            v *= tb.font.width;
            break;
        }
        default:
        {
            return;
        }
        }

        if (v < 0.0f)
        {
            v = 0.0f;
        }
        else if (v > 1.0f)
        {
            v = 1.0f;
        }

        a.cb = &cb;
    }

    void reset_tag_width(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                         const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_width,
                                               update_tag_reset_width };
        a.cb = &cb;
    }

    void update_tag_height(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.font.height = (F32&)j.context;
    }

    void update_tag_reset_height(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.font.height = ctb.font.height;
    }

    void parse_tag_height(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                          const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_height, update_tag_height };
        F32& v = (F32&)a.context;

        if (ti.value.size == 0 || ti.action.size == 0)
        {
            return;
        }

        v = xatof(ti.value.text);

        switch (ti.action.text[0])
        {
        case '=':
        {
            break;
        }
        case '+':
        {
            v += tb.font.height;
            break;
        }
        case '*':
        {
            v *= tb.font.height;
            break;
        }
        default:
        {
            return;
        }
        }

        if (v < 0.0f)
        {
            v = 0.0f;
        }
        else if (v > 1.0f)
        {
            v = 1.0f;
        }

        a.cb = &cb;
    }

    void reset_tag_height(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                          const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_height,
                                               update_tag_reset_height };
        a.cb = &cb;
    }

    void update_tag_left_indent(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.left_indent = (F32&)j.context;
    }

    void update_tag_reset_left_indent(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.left_indent = ctb.left_indent;
    }

    void parse_tag_left_indent(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                               const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_left_indent, NULL };
        F32& v = (F32&)a.context;

        if (ti.value.size == 0 || ti.action.size == 0)
        {
            return;
        }

        v = xatof(ti.value.text);

        switch (ti.action.text[0])
        {
        case '=':
        {
            break;
        }
        case '+':
        {
            v += tb.left_indent;
            break;
        }
        case '*':
        {
            v *= tb.left_indent;
            break;
        }
        default:
        {
            return;
        }
        }

        if (v < -1.0f)
        {
            v = -1.0f;
        }
        else if (v > 1.0f)
        {
            v = 1.0f;
        }

        a.cb = &cb;
    }

    void reset_tag_left_indent(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                               const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_left_indent, NULL };
        a.cb = &cb;
    }

    void update_tag_right_indent(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.right_indent = (F32&)j.context;
    }

    void update_tag_reset_right_indent(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.right_indent = ctb.right_indent;
    }

    void parse_tag_right_indent(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                                const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_right_indent, NULL };
        F32& v = (F32&)a.context;

        if (ti.value.size == 0 || ti.action.size == 0)
        {
            return;
        }

        v = xatof(ti.value.text);

        switch (ti.action.text[0])
        {
        case '=':
        {
            break;
        }
        case '+':
        {
            v += tb.right_indent;
            break;
        }
        case '*':
        {
            v *= tb.right_indent;
            break;
        }
        default:
        {
            return;
        }
        }

        if (v < -1.0f)
        {
            v = -1.0f;
        }
        else if (v > 1.0f)
        {
            v = 1.0f;
        }

        a.cb = &cb;
    }

    void reset_tag_right_indent(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                                const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_right_indent, NULL };
        a.cb = &cb;
    }

    void update_tag_tab_stop(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.tab_stop = (F32&)j.context;
    }

    void update_tag_reset_tab_stop(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.tab_stop = ctb.tab_stop;
    }

    void parse_tag_tab_stop(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                            const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_tab_stop, NULL };
        F32& v = (F32&)a.context;

        if (ti.value.size == 0 || ti.action.size == 0)
        {
            return;
        }

        v = xatof(ti.value.text);

        switch (ti.action.text[0])
        {
        case '=':
        {
            break;
        }
        case '+':
        {
            v += tb.tab_stop;
            break;
        }
        case '*':
        {
            v *= tb.tab_stop;
            break;
        }
        default:
        {
            return;
        }
        }

        if (v < 0.0f)
        {
            v = 0.0f;
        }
        else if (v > 1.0f)
        {
            v = 1.0f;
        }

        a.cb = &cb;
    }

    void reset_tag_tab_stop(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                            const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_tab_stop, NULL };
        a.cb = &cb;
    }

    void update_tag_xspace(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.font.space = (F32&)j.context;
    }

    void update_tag_reset_xspace(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.font.space = ctb.font.space;
    }

    void parse_tag_xspace(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                          const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_xspace, update_tag_xspace };
        F32& v = (F32&)a.context;

        if (ti.value.size == 0 || ti.action.size == 0)
        {
            return;
        }

        v = xatof(ti.value.text);

        switch (ti.action.text[0])
        {
        case '=':
        {
            break;
        }
        case '+':
        {
            v += tb.font.space;
            break;
        }
        case '*':
        {
            v *= tb.font.space;
            break;
        }
        default:
        {
            return;
        }
        }

        if (v < -1.0f)
        {
            v = -1.0f;
        }
        else if (v > 1.0f)
        {
            v = 1.0f;
        }

        a.cb = &cb;
    }

    void reset_tag_xspace(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                          const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_xspace,
                                               update_tag_reset_xspace };
        a.cb = &cb;
    }

    void update_tag_yspace(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.line_space = (F32&)j.context;
    }

    void update_tag_reset_yspace(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.line_space = ctb.line_space;
    }

    void parse_tag_yspace(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                          const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_yspace, NULL };
        F32& v = (F32&)a.context;

        if (ti.value.size == 0 || ti.action.size == 0)
        {
            return;
        }

        v = xatof(ti.value.text);

        switch (ti.action.text[0])
        {
        case '=':
        {
            break;
        }
        case '+':
        {
            v += tb.line_space;
            break;
        }
        case '*':
        {
            v *= tb.line_space;
            break;
        }
        default:
        {
            return;
        }
        }

        if (v < -1.0f)
        {
            v = -1.0f;
        }
        else if (v > 1.0f)
        {
            v = 1.0f;
        }

        a.cb = &cb;
    }

    void reset_tag_yspace(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                          const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_yspace, NULL };
        a.cb = &cb;
    }

    void update_tag_reset_all(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb = ctb;
    }

    void reset_tag_all(xtextbox::jot& j, const xtextbox&, const xtextbox&,
                       const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_all, update_tag_reset_all };
        j.cb = &cb;
    }

    void update_tag_color(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.font.color.r = ((xColor_tag&)j.context).r;
        tb.font.color.g = ((xColor_tag&)j.context).g;
        tb.font.color.b = ((xColor_tag&)j.context).b;
    }

    void update_tag_reset_color(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.font.color = ctb.font.color;
    }

    void parse_tag_color(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                         const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_color, update_tag_color };

        if (ti.value.size < 6 || ti.action.size == 0)
        {
            return;
        }

        xColor_tag& color = (xColor_tag&)a.context;
        U32 v;
        size_t temp;
        v = atox(ti.value, temp);

        if (ti.value.size < 8)
        {
            // Retail bug: masks with 0xFF000000 instead of 0x00FFFFFF, so a
            // 6-digit color loses its RGB part and keeps only the current alpha.
            v = (v & 0xFF000000) | (tb.font.color.a << 24);
        }

        switch (ti.action.text[0])
        {
        case '=':
            color.a = v >> 24;
            color.r = v >> 16;
            color.g = v >> 8;
            color.b = v;
            break;
        case '+':
            color.a = min(255, (v >> 24) + tb.font.color.a);
            color.r = min(255, ((v >> 16) & 0xFF) + tb.font.color.r);
            color.g = min(255, ((v >> 8) & 0xFF) + tb.font.color.g);
            color.b = min(255, (v & 0xFF) + tb.font.color.b);
            break;
        case '*':
            color.a = (v >> 24) * tb.font.color.a / 255;
            color.r = ((v >> 16) & 0xFF) * tb.font.color.r / 255;
            color.g = ((v >> 8) & 0xFF) * tb.font.color.g / 255;
            color.b = (v & 0xFF) * tb.font.color.b / 255;
            break;
        default:
            return;
        }

        a.cb = &cb;
    }

    void reset_tag_color(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                         const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_color,
                                               update_tag_reset_color };
        a.cb = &cb;
    }

    void update_tag_font(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.font.id = (U32&)j.context;
    }

    void update_tag_reset_font(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.font.id = ctb.font.id;
    }

    void parse_tag_font(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                        const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_font, update_tag_font };
        U32& id = (U32&)a.context;

        if (ti.action.size < 1 || ti.action.text[0] != '=' || ti.value.size == 0)
        {
            return;
        }

        id = atoi(ti.value.text);

        if (id < active_fonts_size)
        {
            a.cb = &cb;
        }
    }

    void reset_tag_font(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                        const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_font, update_tag_reset_font };
        a.cb = &cb;
    }

    void update_tag_wrap(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.flags = (tb.flags & ~WRAP_MASK) | ((U32&)j.context & WRAP_MASK);
    }

    void update_tag_reset_wrap(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.flags = (tb.flags & ~WRAP_MASK) | (ctb.flags & WRAP_MASK);
    }

    void parse_tag_wrap(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                        const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, update_tag_wrap, NULL };
        U32& flags = (U32&)a.context;

        if (ti.action.size < 1 || ti.action.text[0] != '=' || ti.value.size != 4)
        {
            return;
        }

        if (imemcmp(ti.value.text, "word", 4) == 0)
        {
            flags = WRAP_WORD;
        }
        else if (imemcmp(ti.value.text, "char", 4) == 0)
        {
            flags = WRAP_CHAR;
        }
        else if (imemcmp(ti.value.text, "none", 4) == 0)
        {
            flags = WRAP_NONE;
        }
        else
        {
            return;
        }

        a.cb = &cb;
    }

    void reset_tag_wrap(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                        const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, update_tag_reset_wrap, NULL };
        a.cb = &cb;
    }

    void update_tag_xjustify(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.flags = (tb.flags & ~XJUSTIFY_MASK) | ((U32&)j.context & XJUSTIFY_MASK);
    }

    void update_tag_reset_xjustify(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.flags = (tb.flags & ~XJUSTIFY_MASK) | (ctb.flags & XJUSTIFY_MASK);
    }

    void parse_tag_xjustify(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                            const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, NULL, update_tag_xjustify };
        U32& flags = (U32&)a.context;

        if (ti.action.size < 1 || ti.action.text[0] != '=')
        {
            return;
        }

        if (icompare(ti.value, substr::create("left", 4)) == 0)
        {
            flags = XJUSTIFY_LEFT;
        }
        else if (icompare(ti.value, substr::create("center", 6)) == 0)
        {
            flags = XJUSTIFY_CENTER;
        }
        else if (icompare(ti.value, substr::create("right", 5)) == 0)
        {
            flags = XJUSTIFY_RIGHT;
        }
        else
        {
            return;
        }

        a.cb = &cb;
    }

    void reset_tag_xjustify(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                            const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, NULL, update_tag_reset_xjustify };
        a.cb = &cb;
    }

    void update_tag_yjustify(const xtextbox::jot& j, xtextbox& tb, const xtextbox&)
    {
        tb.flags = (tb.flags & ~YJUSTIFY_MASK) | ((U32&)j.context & YJUSTIFY_MASK);
    }

    void update_tag_reset_yjustify(const xtextbox::jot&, xtextbox& tb, const xtextbox& ctb)
    {
        tb.flags = (tb.flags & ~YJUSTIFY_MASK) | (ctb.flags & YJUSTIFY_MASK);
    }

    void parse_tag_yjustify(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                            const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { NULL, NULL, update_tag_yjustify };
        U32& flags = (U32&)a.context;

        if (ti.action.size < 1 || ti.action.text[0] != '=')
        {
            return;
        }

        if (icompare(ti.value, substr::create("top", 4)) == 0)
        {
            flags = YJUSTIFY_TOP;
        }
        else if (icompare(ti.value, substr::create("center", 4)) == 0)
        {
            flags = YJUSTIFY_CENTER;
        }
        else if (icompare(ti.value, substr::create("bottom", 4)) == 0)
        {
            flags = YJUSTIFY_BOTTOM;
        }
        else
        {
            return;
        }

        a.cb = &cb;
    }

    void reset_tag_yjustify(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                            const xtextbox::split_tag&)
    {
        static const xtextbox::callback cb = { NULL, NULL, update_tag_reset_yjustify };
        a.cb = &cb;
    }

    void parse_tag_open_curly(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                              const xtextbox::split_tag& ti)
    {
        a.s.text = ti.tag.text;
        a.s.size = 1;

        char c = a.s.text[0];

        a.reset_flags();
        a.bounds = tb.font.bounds(c);
        a.cb = tb.cb;
        a.context = NULL;
    }

    void parse_tag_newline(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                           const xtextbox::split_tag&)
    {
        a.flag.line_break = true;
        a.flag.ethereal = false;
        a.bounds = tb.font.bounds('\n');
    }

    void parse_tag_nbsp(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                        const xtextbox::split_tag&)
    {
        a.reset_flags();
        a.flag.insert = true;
        a.flag.merge = true;
        a.cb = &xtextbox::text_cb;
        a.s = substr::create(" ", 1);
        a.bounds = tb.font.bounds(' ');
    }

    void parse_tag_tab(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                       const xtextbox::split_tag&)
    {
        a.flag.tab = true;
    }

    void parse_tag_word_break(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                              const xtextbox::split_tag&)
    {
        a.flag.word_break = true;
    }

    void parse_tag_page_break(xtextbox::jot& a, const xtextbox&, const xtextbox&,
                              const xtextbox::split_tag&)
    {
        a.flag.page_break = true;
    }

    struct model_tag_context
    {
        xModelInstance* model;
        xVec3 rot;
        basic_rect<F32> dst;
        xSphere o;
    };

    void render_tag_model(const xtextbox::jot& j, const xtextbox& tb, F32 x, F32 y)
    {
        model_tag_context& mtc = *(model_tag_context*)j.context;

        basic_rect<F32> dst = mtc.dst;
        dst.move(x, y);

        xVec3 from = { 0, 0, 1.0f };
        xVec3 to = { 0, 0, -0.001f };

        xMat4x3 frame;

        xMat3x3Euler(&frame, &mtc.rot);

        F32 ir = (mtc.o.r <= 0.0f) ? 1.0f : 0.5f / mtc.o.r;
        F32 scale = 1.001f * ir;

        frame.right *= scale;
        frame.up *= scale;
        frame.at *= scale;
        frame.pos = mtc.o.center;
        frame.flags = 0;

        xModelSetFrame(mtc.model, &frame);
        xModelSetMaterialAlpha(mtc.model, tb.font.color.a);

        tex_flush();

        RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)rwSHADEMODEGOURAUD);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);

        xModelRender2D(*mtc.model, dst, from, to);

        RwRenderStateSet(rwRENDERSTATESHADEMODE, (void*)rwSHADEMODEFLAT);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    }

    void parse_tag_model(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                         const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { render_tag_model, NULL, NULL };

        load_model_args(def_model_args, ti.tag);

        if (def_model_args.model)
        {
            model_tag_context& mtc = *(model_tag_context*)a.context;

            mtc.model = def_model_args.model;
            mtc.rot = def_model_args.rot;
            mtc.rot *= 0.017453292f;
            mtc.dst = def_model_args.dst;

            mtc.o = *xModelGetLocalSBound(mtc.model);

            mtc.dst.y -= def_model_args.off.y;

            a.bounds.assign(0.0f, -def_model_args.off.y, def_model_args.off.x,
                            def_model_args.off.y);

            switch (def_model_args.scale)
            {
            case model_args::SCALE_SIZE:
                mtc.dst.scale(mtc.o.r);
                a.bounds.scale(mtc.o.r);
                break;
            case model_args::SCALE_SCREEN:
                break;
            default:
                mtc.dst.scale(tb.font.width, tb.font.height);
                a.bounds.scale(tb.font.width, tb.font.height);
                break;
            }

            a.reset_flags();
            a.context_size = sizeof(model_tag_context);
            a.cb = &cb;
        }
    }

    void reset_tag_model(xtextbox::jot&, const xtextbox&, const xtextbox&,
                         const xtextbox::split_tag&)
    {
        reset_model_args(def_model_args);
    }

    struct tex_tag_context
    {
        RwRaster* raster;
        F32 rot;
        basic_rect<F32> src;
        basic_rect<F32> dst;
    };

    void render_tag_tex(const xtextbox::jot& j, const xtextbox& tb, F32 x, F32 y)
    {
        tex_tag_context& ttc = *(tex_tag_context*)j.context;

        set_tex_raster(ttc.raster);

        basic_rect<F32> dst = ttc.dst;
        dst.move(x, y);

        tex_render(ttc.src, dst, tb.font.clip, tb.font.color);
    }

    // No symbol in the target (always inlined).
    inline xVec2 get_texture_size(RwRaster& raster)
    {
        xVec2 ret = { raster.width / 640.0f, raster.height / 480.0f };
        return ret;
    }

    void parse_tag_tex(xtextbox::jot& a, const xtextbox& tb, const xtextbox&,
                       const xtextbox::split_tag& ti)
    {
        static const xtextbox::callback cb = { render_tag_tex, NULL, NULL };

        load_tex_args(def_tex_args, ti.tag);

        if (def_tex_args.raster)
        {
            tex_tag_context& ttc = *(tex_tag_context*)a.context;

            ttc.raster = def_tex_args.raster;
            ttc.rot = def_tex_args.rot;
            ttc.src = def_tex_args.src;
            ttc.dst = def_tex_args.dst;
            ttc.dst.y -= def_tex_args.off.y;

            a.bounds.assign(0.0f, -def_tex_args.off.y, def_tex_args.off.x, def_tex_args.off.y);

            xVec2 scale;

            switch (def_tex_args.scale)
            {
            case tex_args::SCALE_SCREEN:
                scale.assign(1.0f, 1.0f);
                break;
            case tex_args::SCALE_SIZE:
                scale = get_texture_size(*ttc.raster);
                break;
            case tex_args::SCALE_FONT_WIDTH:
                scale = get_texture_size(*ttc.raster);
                scale.y *= tb.font.width / scale.x;
                scale.x = tb.font.width;
                break;
            case tex_args::SCALE_FONT_HEIGHT:
                scale = get_texture_size(*ttc.raster);
                scale.x *= tb.font.height / scale.y;
                scale.y = tb.font.height;
                break;
            case tex_args::SCALE_SCREEN_WIDTH:
                scale = get_texture_size(*ttc.raster);
                scale.y *= 1.0f / scale.x;
                scale.x = 1.0f;
                break;
            case tex_args::SCALE_SCREEN_HEIGHT:
                scale = get_texture_size(*ttc.raster);
                scale.x *= 1.0f / scale.y;
                scale.y = 1.0f;
                break;
            default:
                scale.assign(tb.font.width, tb.font.height);
                break;
            }

            ttc.dst.scale(scale.x, scale.y);
            a.bounds.scale(scale.x, scale.y);
            a.reset_flags();
            a.context_size = sizeof(tex_tag_context);
            a.cb = &cb;
        }
    }

    void reset_tag_tex(xtextbox::jot&, const xtextbox&, const xtextbox&, const xtextbox::split_tag&)
    {
        reset_tex_args(def_tex_args);
    }

    void parse_tag_insert(xtextbox::jot& j, const xtextbox&, const xtextbox&,
                          const xtextbox::split_tag& ti)
    {
        j.reset_flags();
        j.flag.invisible = j.flag.ethereal = true;

        if (ti.action.size != 1 || ti.action.text[0] != ':' || ti.value.size == 0)
        {
            return;
        }

        U32 id = xStrHash(ti.value.text, ti.value.size);

        if (id)
        {
            U32 len;
            char* text = xTextFindString(id, &len);

            if (text)
            {
                j.context = text;
                j.context_size = len;
                j.flag.insert = true;
            }
        }
    }

    void parse_tag_insert_hash(xtextbox::jot& j, const xtextbox&, const xtextbox&,
                               const xtextbox::split_tag& ti)
    {
        j.reset_flags();
        j.flag.invisible = j.flag.ethereal = true;

        if (ti.action.size != 1 || ti.action.text[0] != ':' || ti.value.size == 0)
        {
            return;
        }

        U32 id = atox(ti.value);

        if (id)
        {
            U32 len;
            char* text = xTextFindString(id, &len);

            if (text)
            {
                j.context = text;
                j.context_size = len;
                j.flag.insert = true;
            }
        }
    }

    void parse_tag_pop(xtextbox::jot&, const xtextbox&, const xtextbox&,
                       const xtextbox::split_tag& ti)
    {
        if (ti.action.size != 1 || ti.action.text[0] != ':' || ti.value.size == 0)
        {
            return;
        }
    }

    void parse_tag_timer(xtextbox::jot& j, const xtextbox&, const xtextbox&,
                         const xtextbox::split_tag& ti)
    {
        j.reset_flags();
        j.flag.invisible = j.flag.ethereal = true;

        if (ti.action.size != 1 || ti.action.text[0] != ':' || ti.value.size == 0)
        {
            return;
        }

        U32 id = xStrHash(ti.value.text, ti.value.size);

        if (id)
        {
            xTimer* ta = (xTimer*)zSceneFindObject(id);

            if (ta)
            {
                j.flag.insert = j.flag.dynamic = true;

                char buffer[64];
                U32 sec = (U32)ta->secondsLeft + 1;
                U32 mn = sec / 60;

                if (mn)
                {
                    sprintf(buffer, "%d:%02d", mn, sec - mn * 60);
                }
                else
                {
                    sprintf(buffer, "%02d", sec);
                }

                sprintf((char*)j.context, "%.*s", 15, buffer);
                j.context_size = 16;
            }
        }
    }

    void parse_tag_counter(xtextbox::jot& j, const xtextbox&, const xtextbox&,
                           const xtextbox::split_tag& ti)
    {
        j.reset_flags();
        j.flag.invisible = j.flag.ethereal = true;

        if (ti.action.size != 1 || ti.action.text[0] != ':' || ti.value.size == 0)
        {
            return;
        }

        U32 id = xStrHash(ti.value.text, ti.value.size);
        _xCounter* counter = (_xCounter*)zSceneFindObject(id);

        if (!counter || counter->baseType != eBaseTypeCounter)
        {
            return;
        }

        char* buffer = (char*)j.context;
        sprintf(buffer, "%d", counter->count);
        j.context_size = strlen(buffer);
    }

    // clang-format off
    xtextbox::tag_type format_tags_buffer[2][128] =
    {
        SUBSTR(""), parse_tag_open_curly, NULL, NULL,
        SUBSTR("a"), parse_tag_alpha, reset_tag_alpha, NULL,
        SUBSTR("all"), NULL, reset_tag_all, NULL,
        SUBSTR("alpha"), parse_tag_alpha, reset_tag_alpha, NULL,
        SUBSTR("b"), parse_tag_blue, reset_tag_blue, NULL,
        SUBSTR("blue"), parse_tag_blue, reset_tag_blue, NULL,
        SUBSTR("c"), parse_tag_color, reset_tag_color, NULL,
        SUBSTR("color"), parse_tag_color, reset_tag_color, NULL,
        SUBSTR("counter"), parse_tag_counter, NULL, NULL,
        SUBSTR("f"), parse_tag_font, reset_tag_font, NULL,
        SUBSTR("font"), parse_tag_font, reset_tag_font, NULL,
        SUBSTR("g"), parse_tag_green, reset_tag_green, NULL,
        SUBSTR("green"), parse_tag_green, reset_tag_green, NULL,
        SUBSTR("h"), parse_tag_height, reset_tag_height, NULL,
        SUBSTR("height"), parse_tag_height, reset_tag_height, NULL,
        SUBSTR("i"), parse_tag_insert, NULL, NULL,
        SUBSTR("ih"), parse_tag_insert_hash, NULL, NULL,
        SUBSTR("insert"), parse_tag_insert, NULL, NULL,
        SUBSTR("left_indent"), parse_tag_left_indent, reset_tag_left_indent, NULL,
        SUBSTR("li"), parse_tag_left_indent, reset_tag_left_indent, NULL,
        SUBSTR("model"), parse_tag_model, reset_tag_model, NULL,
        SUBSTR("n"), parse_tag_newline, NULL, NULL,
        SUBSTR("nbsp"), parse_tag_nbsp, NULL, NULL,
        SUBSTR("newline"), parse_tag_newline, NULL, NULL,
        SUBSTR("page_break"), parse_tag_page_break, NULL, NULL,
        SUBSTR("pb"), parse_tag_page_break, NULL, NULL,
        SUBSTR("pop"), parse_tag_pop, NULL, NULL,
        SUBSTR("r"), parse_tag_red, reset_tag_red, NULL,
        SUBSTR("red"), parse_tag_red, reset_tag_red, NULL,
        SUBSTR("ri"), parse_tag_right_indent, reset_tag_right_indent, NULL,
        SUBSTR("right_indent"), parse_tag_right_indent, reset_tag_right_indent, NULL,
        SUBSTR("sa"), parse_tag_salpha, reset_tag_salpha, NULL,
        SUBSTR("salpha"), parse_tag_salpha, reset_tag_salpha, NULL,
        SUBSTR("sb"), parse_tag_sblue, reset_tag_sblue, NULL,
        SUBSTR("sblue"), parse_tag_sblue, reset_tag_sblue, NULL,
        SUBSTR("sg"), parse_tag_sgreen, reset_tag_sgreen, NULL,
        SUBSTR("sgreen"), parse_tag_sgreen, reset_tag_sgreen, NULL,
        SUBSTR("sr"), parse_tag_sred, reset_tag_sred, NULL,
        SUBSTR("sred"), parse_tag_sred, reset_tag_sred, NULL,
        SUBSTR("t"), parse_tag_tab, NULL, NULL,
        SUBSTR("tab"), parse_tag_tab, NULL, NULL,
        SUBSTR("tab_stop"), parse_tag_tab_stop, reset_tag_tab_stop, NULL,
        SUBSTR("tex"), parse_tag_tex, reset_tag_tex, NULL,
        SUBSTR("timer"), parse_tag_timer, NULL, NULL,
        SUBSTR("ts"), parse_tag_tab_stop, reset_tag_tab_stop, NULL,
        SUBSTR("w"), parse_tag_width, reset_tag_width, NULL,
        SUBSTR("wb"), parse_tag_word_break, NULL, NULL,
        SUBSTR("width"), parse_tag_width, reset_tag_width, NULL,
        SUBSTR("word_break"), parse_tag_word_break, NULL, NULL,
        SUBSTR("wrap"), parse_tag_wrap, reset_tag_wrap, NULL,
        SUBSTR("xj"), parse_tag_xjustify, reset_tag_xjustify, NULL,
        SUBSTR("xjustify"), parse_tag_xjustify, reset_tag_xjustify, NULL,
        SUBSTR("xs"), parse_tag_xspace, reset_tag_xspace, NULL,
        SUBSTR("xspace"), parse_tag_xspace, reset_tag_xspace, NULL,
        SUBSTR("yj"), parse_tag_yjustify, reset_tag_yjustify, NULL,
        SUBSTR("yjustify"), parse_tag_yjustify, reset_tag_yjustify, NULL,
        SUBSTR("ys"), parse_tag_yspace, reset_tag_yspace, NULL,
        SUBSTR("yspace"), parse_tag_yspace, reset_tag_yspace, NULL
    };
    // clang-format on

    xtextbox::tag_type* format_tags = format_tags_buffer[0];
    U32 format_tags_size = 58;
} // namespace

void xtextbox::register_tags(const tag_type* t, size_t size)
{
    const tag_type *s1, *s2, *end1, *end2;

    end1 = (s1 = format_tags) + format_tags_size;
    s2 = t;
    end2 = t + size;

    tag_type* d =
        (format_tags == format_tags_buffer[0]) ? format_tags_buffer[1] : format_tags_buffer[0];

    format_tags = d;

    while (s1 < end1 && s2 < end2)
    {
        S32 c = icompare(s1->name, s2->name);

        if (c < 0)
        {
            *d = *s1;
            s1++;
        }
        else if (c > 0)
        {
            *d = *s2;
            s2++;
        }
        else
        {
            *d = *s2;
            s1++;
            s2++;
        }

        d++;
    }

    while (s1 < end1)
    {
        *d = *s1;
        d++;
        s1++;
    }

    while (s2 < end2)
    {
        *d = *s2;
        d++;
        s2++;
    }

    format_tags_size = d - format_tags;
}

xtextbox::tag_type* xtextbox::find_format_tag(const substr& s, S32& index)
{
    S32 start = 0;
    S32 end = format_tags_size;

    while (start != end)
    {
        index = (start + end) / 2;

        tag_type& t = format_tags[index];
        S32 c = icompare(s, t.name);

        if (c < 0)
        {
            end = index;
        }
        else if (c > 0)
        {
            start = index + 1;
        }
        else
        {
            return &t;
        }
    }

    index = -1;
    return NULL;
}

namespace
{
    inline void set_rect_vert(RwIm2DVertex& vert, F32 x, F32 y, F32 z, xColor_tag c, F32 rcz)
    {
        RwIm2DVertexSetScreenX(&vert, x);
        RwIm2DVertexSetScreenY(&vert, y);
        RwIm2DVertexSetScreenZ(&vert, z);
        RwIm2DVertexSetIntRGBA(&vert, c.r, c.g, c.b, c.a);
        RwIm2DVertexSetRecipCameraZ(&vert, rcz);
    }

    inline void set_rect_verts(RwIm2DVertex* verts, F32 x, F32 y, F32 w, F32 h, xColor_tag c,
                               F32 rcz, F32 nsz)
    {
        set_rect_vert(verts[0], x, y, nsz, c, rcz);
        set_rect_vert(verts[1], x, y + h, nsz, c, rcz);
        set_rect_vert(verts[2], x + w, y, nsz, c, rcz);
        set_rect_vert(verts[3], x + w, y + h, nsz, c, rcz);
    }
} // namespace

void render_fill_rect(const basic_rect<F32>& bounds, xColor_tag color)
{
    if (!bounds.empty())
    {
        F32 rcz = 1.0f / RwCameraGetNearClipPlane(RwCameraGetCurrentCamera());
        F32 nsz = RwIm2DGetNearScreenZ();

        xfont::set_render_state(NULL);

        RwIm2DVertex vert[4];
        basic_rect<F32> r = bounds;
        r.scale(FB_XRES, FB_YRES);
        set_rect_verts(vert, r.x, r.y, r.w, r.h, color, rcz, nsz);
        RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, vert, 4);
        xfont::restore_render_state();
    }
}

// The wip timer cycles through 0.5s fade in, 0.5s on, 0.5s fade out and 0.05s off.
static void PrintWIPText()
{
    static const char* TXT_WIP = NULL;

    if (!TXT_WIP)
    {
        TXT_WIP = xTextFindString("TXT_WIP", NULL);
    }

    static F32 wipTimer = 0.0f;

    wipTimer += gRealTimeElapsed;

    if (wipTimer > 1.55f)
    {
        wipTimer -= (S32)(wipTimer / 1.55f) * 1.55f;
    }

    if (wipTimer < 1.5f)
    {
        xColor_tag color = { 0xFF, 0xFF, 0xFF, 0x80 };
        xColor_tag dropcolor = { 0x00, 0x00, 0x00, 0x80 };
        F32 fadePer;

        if (wipTimer < 0.5f)
        {
            fadePer = wipTimer / 0.5f;
        }
        else if (wipTimer > 1.0f)
        {
            // Retail quirk: not divided by the 0.5s fade time, so the fade out starts at half alpha.
            fadePer = 0.5f - (wipTimer - 0.5f - 0.5f);
        }
        else
        {
            fadePer = 1.0f;
        }

        // Both alphas are faded from dropcolor's (the target converts the product twice).
        color.a = dropcolor.a * fadePer;
        dropcolor.a = dropcolor.a * fadePer;

        // Bottom-anchored: retail's y is exactly 0.875f (0x3F600000), which NSCREENY(420.0f) misses by an ulp.
        basic_rect<F32> bounds = { 0.0f, 1.0f - NSCREENY(60.0f), NSCREENX(610.0f),
                                   NSCREENY(30.0f) };

        static xtextbox tb = xtextbox::create(xfont::create(0, NSCREENX(20.0f), NSCREENY(20.0f),
                                                            0.0f, g_WHITE, screen_bounds),
                                              bounds, 0x1, 0.0f, 0.0f, 0.0f, 0.0f);

        tb.set_text(TXT_WIP, 0x40000000);

        tb.font.shadowColor = dropcolor;
        tb.font.shadowOffsetX = 2.0f;
        tb.font.shadowOffsetY = 2.0f;
        tb.font.color = color;
        tb.bounds = bounds;

        tb.render(true);
    }
}

static void PrintWatermark()
{
    basic_rect<F32> bounds = { 0.0f, 0.05f, 0.95f, 0.0f };
    bounds.h = 1.5f * NSCREENY(xglobals->watermarkSize);

    xtextbox tb = xtextbox::create(xfont::create(0, NSCREENX(xglobals->watermarkSize),
                                                 NSCREENY(xglobals->watermarkSize), 0.0f, g_WHITE,
                                                 screen_bounds),
                                   bounds, 0x1, 0.0f, 0.0f, 0.0f, 0.0f);

    tb.set_text(xglobals->watermark, 0x40000000);

    xColor_tag color = { 0xFF, 0xFF, 0xFF, xglobals->watermarkAlpha };
    xColor_tag dropcolor = { 0x00, 0x00, 0x00, color.a };

    tb.font.color = color;
    tb.font.shadowColor = dropcolor;
    tb.font.shadowOffsetX = 2.0f;
    tb.font.shadowOffsetY = 2.0f;
    tb.font.color = color; // retail stores the color twice
    tb.bounds = bounds;

    tb.render(true);
}

void xFontPrintTopText()
{
    if (xglobals->FlashWIP)
    {
        PrintWIPText();
    }

    if (xglobals->watermark[0])
    {
        PrintWatermark();
    }
}

void xFontInitModelCache()
{
    for (U32 i = 0; i < MODEL_CACHE_SIZE; i++)
    {
        model_cache_entry& e = model_cache[i];

        e.order = 0;
        e.id = 0;
    }
}
