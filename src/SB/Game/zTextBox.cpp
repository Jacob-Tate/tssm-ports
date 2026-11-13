#include "zTextBox.h"

#include "zBase.h"
#include "zMain.h"

#include "xEvent.h"
#include "xstransvc.h"
#include "xTextAsset.h"

#include <string.h>

inline iColor_tag convert(const ztextbox::asset_type::color_type& color)
{
    iColor_tag c;
    c.r = color.r;
    c.g = color.g;
    c.b = color.b;
    c.a = color.a;
    return c;
}

namespace
{
    ztextbox* head_active;

    typedef void (*render_bk_callback)(const ztextbox& e);

    void render_bk_fill(const ztextbox& e);
    void render_bk_tex_scale(const ztextbox& e);
    void render_bk_tex_wrap(const ztextbox& e);
    void render_bk_by_pieces(const ztextbox& e);

    render_bk_callback render_bk_table[] = { render_bk_fill, render_bk_tex_scale,
                                             render_bk_tex_wrap, render_bk_by_pieces };

    void parse_tag_blahblah(xtextbox::jot&, const xtextbox&, const xtextbox&,
                            const xtextbox::split_tag&);

    xtextbox::tag_type new_tags[] = { SUBSTR("blahblah"), parse_tag_blahblah, NULL, NULL };
    U32 new_tags_size = sizeof(new_tags) / sizeof(new_tags[0]);

    void render_bk_fill(const ztextbox& e)
    {
        const ztextbox::asset_type& a = *e.asset;

        render_fill_rect(e.tb.font.clip, convert(a.backdrop.color));
    }

    inline void set_vert(RwIm2DVertex& vert, F32 x, F32 y, F32 u, F32 v, iColor_tag c, F32 nsz,
                         F32 rcz)
    {
        RwIm2DVertexSetScreenX(&vert, x);
        RwIm2DVertexSetScreenY(&vert, y);
        RwIm2DVertexSetScreenZ(&vert, nsz);
        RwIm2DVertexSetU(&vert, u, rcz);
        RwIm2DVertexSetV(&vert, v, rcz);
        RwIm2DVertexSetIntRGBA(&vert, c.r, c.g, c.b, c.a);
    }

    void render_bk_tex_scale(const ztextbox& e)
    {
        iColor_tag color = convert(e.asset->backdrop.color);
        F32 rcz = 1.0f / RwCameraGetNearClipPlane(RwCameraGetCurrentCamera());
        F32 nsz = RwIm2DGetNearScreenZ();
        RwIm2DVertex vert[6];

        xfont::set_render_state(e.bgtex);

        basic_rect<F32> r = e.tb.font.clip;
        r.scale(FB_XRES, FB_YRES);

        set_vert(vert[0], r.x, r.y, 0.0f, 0.0f, color, nsz, rcz);
        set_vert(vert[1], r.x, r.y + r.h, 0.0f, 1.0f, color, nsz, rcz);
        set_vert(vert[2], r.x + r.w, r.y, 1.0f, 0.0f, color, nsz, rcz);

        vert[3] = vert[2];
        vert[4] = vert[1];

        set_vert(vert[5], r.x + r.w, r.y + r.h, 1.0f, 1.0f, color, nsz, rcz);

        RwIm2DRenderPrimitive(rwPRIMTYPETRILIST, vert, 6);

        xfont::restore_render_state();
    }

    void render_bk_tex_wrap(const ztextbox& e)
    {
    }

    void render_bk_by_pieces(const ztextbox& e)
    {
        iColor_tag color = convert(e.asset->backdrop.color);
        F32 rcz = 1.0f / RwCameraGetNearClipPlane(RwCameraGetCurrentCamera());
        F32 nsz = RwIm2DGetNearScreenZ();
        // Initialised earlier than the DWARF places it: its dead store must not sit in the pieces
        // block, or GC/2.6 reschedules that block.
        U32 i = 0;

        xfont::set_render_state(e.bgtex);

        basic_rect<F32> r = e.tb.font.clip;
        r.scale(FB_XRES, FB_YRES);

        F32 borderX = FB_XRES * e.asset->backdrop.borderWidth;
        F32 borderY = FB_YRES * e.asset->backdrop.borderHeight;
        F32 borderU = e.asset->backdrop.borderU;
        F32 borderV = e.asset->backdrop.borderV;

        struct
        {
            F32 x;
            F32 y;
            F32 w;
            F32 h;
            F32 lu;
            F32 lv;
            F32 hu;
            F32 hv;
        } pieces[9] = {
            { r.x, r.y, borderX, borderY, 0.0f, 0.0f, borderU, borderV },
            { r.x + borderX, r.y, r.w - 2.0f * borderX, borderY, borderU, 0.0f,
              1.0f - 2.0f * borderU, borderV },
            { r.x + r.w - borderX, r.y, borderX, borderY, 1.0f - borderU, 0.0f, 1.0f, borderV },
            { r.x, r.y + borderY, borderX, r.h - 2.0f * borderY, 0.0f, borderV, borderU,
              1.0f - 2.0f * borderV },
            { r.x + borderX, r.y + borderY, r.w - 2.0f * borderX, r.h - 2.0f * borderY, borderU,
              borderV, 1.0f - 2.0f * borderU, 1.0f - 2.0f * borderV },
            { r.x + r.w - borderX, r.y + borderY, borderX, r.h - 2.0f * borderY, 1.0f - borderU,
              borderV, 1.0f, 1.0f - 2.0f * borderV },
            { r.x, r.y + r.h - borderY, borderX, borderY, 0.0f, 1.0f - borderV, borderU, 1.0f },
            { r.x + borderX, r.y + r.h - borderY, r.w - 2.0f * borderX, borderY, borderU,
              1.0f - borderV, 1.0f - 2.0f * borderU, 1.0f },
            { r.x + r.w - borderX, r.y + r.h - borderY, borderX, borderY, 1.0f - borderU,
              1.0f - borderV, 1.0f, 1.0f },
        };

        RwIm2DVertex vert[54];

        for (; i < 9; i++)
        {
            set_vert(vert[i * 6 + 0], pieces[i].x, pieces[i].y, pieces[i].lu, pieces[i].lv, color,
                     nsz, rcz);
            set_vert(vert[i * 6 + 1], pieces[i].x, pieces[i].y + pieces[i].h, pieces[i].lu,
                     pieces[i].hv, color, nsz, rcz);
            set_vert(vert[i * 6 + 2], pieces[i].x + pieces[i].w, pieces[i].y, pieces[i].hu,
                     pieces[i].lv, color, nsz, rcz);
            vert[i * 6 + 3] = vert[i * 6 + 2];
            vert[i * 6 + 4] = vert[i * 6 + 1];
            set_vert(vert[i * 6 + 5], pieces[i].x + pieces[i].w, pieces[i].y + pieces[i].h,
                     pieces[i].hu, pieces[i].hv, color, nsz, rcz);
        }

        RwIm2DRenderPrimitive(rwPRIMTYPETRILIST, vert, 54);

        xfont::restore_render_state();
    }

    void init_textbox(ztextbox& e)
    {
        static const U32 xjlookup[] = { 0x0, 0x2, 0x1 };
        static const U32 yjlookup[] = { 0x0, 0x8, 0x4 };

        ztextbox::asset_type& a = *e.asset;

        e.tb.flags = 0;
        e.tb.font.id = a.font;
        e.tb.font.width = a.size.width;
        e.tb.font.height = a.size.height;
        e.tb.font.space = a.space.x;
        e.tb.line_space = a.space.y;
        e.tb.font.color = convert(a.color);
        e.tb.bounds.x = a.bounds.x + a.inset.left;
        e.tb.bounds.y = a.bounds.y + a.inset.top;
        e.tb.bounds.w = a.bounds.w - a.inset.left - a.inset.right;
        e.tb.bounds.h = a.bounds.h - a.inset.top - a.inset.bottom;
        e.tb.flags |= (a.xjustify >= 3) ? xjlookup[0] : xjlookup[a.xjustify];
        e.tb.flags |= (a.yjustify >= 3) ? yjlookup[0] : yjlookup[a.yjustify];
        e.tb.set_text(e.segments, e.segments_size);

        if (a.expand < ztextbox::asset_type::MAX_EX && a.max_height > a.bounds.h)
        {
            S32 lines;
            F32 minh = e.tb.bounds.h;
            F32 maxh = a.max_height - a.inset.top - a.inset.bottom;

            e.tb.bounds.h = maxh;
            e.tb.bounds.h = e.tb.yextent(maxh, lines, true);

            if (e.tb.bounds.h > minh)
            {
                F32 hmore = e.tb.bounds.h - minh;

                if (a.expand == ztextbox::asset_type::EX_CENTER)
                {
                    e.tb.bounds.y -= 0.5f * hmore;
                }
                else if (a.expand == ztextbox::asset_type::EX_UP)
                {
                    e.tb.bounds.y -= hmore;
                }
            }
            else
            {
                e.tb.bounds.h = minh;
            }
        }

        e.tb.font.clip.x = e.tb.bounds.x - a.inset.left;
        e.tb.font.clip.y = e.tb.bounds.y - a.inset.top;
        e.tb.font.clip.w = e.tb.bounds.w + a.inset.left + a.inset.right;
        e.tb.font.clip.h = e.tb.bounds.h + a.inset.top + a.inset.bottom;
    }

    void parse_tag_blahblah(xtextbox::jot&, const xtextbox&, const xtextbox&,
                            const xtextbox::split_tag&)
    {
    }
} // namespace

namespace
{
    void cb_dispatch(xBase*, xBase* to, U32 event, const F32* argf, xBase*, U32)
    {
        ztextbox& e = *(ztextbox*)to;

        switch (event)
        {
        case eEventReset:
        case eEventSceneEnd:
        {
            e.reset();
            break;
        }
        case eEventFontBackdropOn:
        {
            e.flag.show_backdrop = true;
            break;
        }
        case eEventFontBackdropOff:
        {
            e.flag.show_backdrop = false;
            break;
        }
        case eEventVisible:
        {
            if (argf && *(U32*)&argf[0])
            {
                e.set_text(*(U32*)&argf[0]);
            }

            e.activate(false);
            break;
        }
        case eEventInvisible:
        {
            e.deactivate();
            break;
        }
        case eEventSetText:
        {
            if (argf)
            {
                e.set_text(*(U32*)&argf[0]);
            }

            break;
        }
        case eEventAddText:
        {
            if (argf)
            {
                e.add_text(*(U32*)&argf[0]);
            }

            break;
        }
        case eEventClearText:
        {
            e.clear_text();
            break;
        }
        }
    }
} // namespace

// No symbol: inlined into load(xBase&, xDynAsset&, size_t). Defined here (not in-class)
// because it uses this file's cb_dispatch().
inline void ztextbox::load(const asset_type& a)
{
    xBaseInit(this, (xBaseAsset*)&a);

    baseType = eBaseTypeTextBox;
    asset = (asset_type*)&a;
    eventFunc = cb_dispatch;

    if (linkCount)
    {
        link = (xLinkAsset*)(&a + 1);
    }

    tb.context = this;
    prev = NULL;
    next = NULL;
    flag.active = false;
    bgtex = NULL;

    if (a.backdrop.texture)
    {
        RwTexture* tex = (RwTexture*)xSTFindAsset(a.backdrop.texture, NULL);

        if (tex)
        {
            bgtex = RwTextureGetRaster(tex);
        }
    }

    reset();
}

// No symbol: inlined into load and cb_dispatch. Defined here (not in-class) because it uses
// this file's convert().
inline void ztextbox::reset()
{
    deactivate();

    segments_size = 0;
    flag.dirty = true;
    flag.show_backdrop = true;

    set_text(asset->text);

    tb.font.shadowColor = convert(asset->shadowColor);
    tb.font.shadowOffsetX = asset->shadowOffsetX;
    tb.font.shadowOffsetY = asset->shadowOffsetY;
}

// No symbol: inlined into render_all. Defined here (not in-class) because it uses this
// file's init_textbox().
inline void ztextbox::render()
{
    if (flag.dirty)
    {
        init_textbox(*this);
        flag.dirty = false;
    }

    if (flag.skipFrame)
    {
        flag.skipFrame = false;
        return;
    }

    if (flag.show_backdrop)
    {
        render_backdrop();
    }

    tb.render(true);
}

void ztextbox::render_backdrop()
{
    if (asset->backdrop.type < sizeof(render_bk_table) / sizeof(render_bk_table[0]))
    {
        render_bk_table[asset->backdrop.type](*this);
    }
}

void ztextbox::activate(bool skipFirstFrame)
{
    if (flag.active)
    {
        return;
    }

    flag.active = true;
    flag.skipFrame = skipFirstFrame;

    prev = NULL;
    next = head_active;

    if (head_active)
    {
        head_active->prev = this;
    }

    head_active = this;
}

void ztextbox::deactivate()
{
    if (!flag.active)
    {
        return;
    }

    flag.active = false;

    if (prev)
    {
        prev->next = next;
    }

    if (next)
    {
        next->prev = prev;
    }

    if (head_active == this)
    {
        head_active = next;
    }
}

void ztextbox::set_text(const char* s)
{
    clear_text();
    add_text(s);
}

void ztextbox::set_text(U32 id)
{
    if (!id)
    {
        return;
    }

    const char* text = xTextFindString(id, NULL);

    if (!text)
    {
        clear_text();
    }
    else
    {
        set_text(text);
    }
}

void ztextbox::add_text(const char* s)
{
    if (!s)
    {
        return;
    }

    segments[segments_size] = s;
    segments_size++;

    flag.dirty = true;
}

void ztextbox::clear_text()
{
    segments_size = 0;
    flag.dirty = true;
}

void ztextbox::refresh()
{
    if (flag.dirty)
    {
        init_textbox(*this);
        flag.dirty = false;
    }
}

void ztextbox::get_text(char* buffer, size_t buffer_size) const
{
    const char* const* it = segments;
    const char* const* end = it + segments_size;

    while (it != end)
    {
        const char* s = *it;
        U32 len = strlen(s);

        if (len >= buffer_size)
        {
            memcpy(buffer, s, buffer_size - 1);
            buffer += buffer_size - 1;
            break;
        }

        memcpy(buffer, s, len);
        buffer += len;
        it++;
    }

    *buffer = '\0';
}

void ztextbox::init()
{
    xtextbox::register_tags(new_tags, new_tags_size);

    head_active = NULL;
}

void ztextbox::load(xBase& data, xDynAsset& asset, size_t)
{
    ((ztextbox&)data).load((asset_type&)asset);
}

void ztextbox::update_all(xScene& s, F32 dt)
{
    ztextbox* it = head_active;

    while (it)
    {
        it->update(s, dt);
        it = it->next;
    }
}

void ztextbox::render_all()
{
    ztextbox* it = head_active;

    while (it)
    {
        it->render();
        it = it->next;
    }
}
