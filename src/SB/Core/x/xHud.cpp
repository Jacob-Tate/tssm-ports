#include "xHud.h"
#include "xHudImage.h"
#include "xHudMeter.h"
#include "xHudModel.h"
#include "xHudText.h"

#include "xEvent.h"
#include "xString.h"

#include "zHud.h"
#include "zEnt.h"
#include "zScene.h"
#include "xstransvc.h"

xhud::block_allocator xhud::widget::motive_allocator;

namespace xhud
{
    static bool inited;

    namespace
    {
        const struct
        {
            U8 type;
            U32 size;
        } known_types[] = {
            { eBaseTypeHUD_image, sizeof(xBase) + sizeof(image_widget) },
            { eBaseTypeHUD_model, sizeof(xBase) + sizeof(model_widget) },
            { eBaseTypeHUD_unit_meter, sizeof(xBase) + sizeof(unit_meter_widget) },
            { eBaseTypeHUD_font_meter, sizeof(xBase) + sizeof(font_meter_widget) },
            { eBaseTypeHUD_text, sizeof(xBase) + sizeof(text_widget) },
        };

        struct functor_update
        {
            F32 dt;

            functor_update(F32 dt)
            {
                this->dt = dt;
            }

            void operator()(widget& w)
            {
                if (w.enabled())
                {
                    w.update(dt);
                }
            }
        };

        struct functor_disable
        {
            U8 destroy_widgets;

            functor_disable(bool destroy_widgets)
            {
                this->destroy_widgets = destroy_widgets;
            }

            void operator()(widget& w)
            {
                w.disable();
                if (destroy_widgets)
                {
                    w.destroy();
                }
            }
        };

        void fp_render(widget& w);
    } // namespace
} // namespace xhud

void xhud::init()
{
    if (!inited)
    {
        inited = true;
    }
    else
    {
        widget::disable_all(true);
    }
}

void xhud::destroy()
{
    widget::disable_all(true);
    inited = false;
}

void xhud::update(F32 dt)
{
    widget::update_all(dt);
}

void xhud::render()
{
    if (inited)
    {
        widget::render_all();
    }
}

void xhud::widget::presetup()
{
    activity = ACT_NONE;
}

void xhud::widget::dispatcher(xBase*, U32 event, const F32* toParam, xBase*, U32)
{
    switch (event)
    {
    case eEventEnable:
        enable();
        break;
    case eEventDisable:
        disable();
        break;
    case eEventVisible:
    case eEventFastVisible:
        flag.visible = 1;
        break;
    case eEventInvisible:
    case eEventFastInvisible:
        flag.visible = 0;
        break;
    case eEventDispatcher_ShowHud:
        show(LEFT, true, false);
        break;
    case eEventDispatcher_HideHud:
        hide(LEFT, true, false);
        break;
    }
}

void xhud::widget::scene_enter()
{
    motive_allocator.init(sizeof(block_allocator::header) + sizeof(motive_node), 100);
}

U32 xhud::widget::type() const
{
    static U32 myid = xStrHash(asset::type_name());
    return myid;
}

bool xhud::widget::is(U32 id) const
{
    return id == widget::type();
}

void xhud::widget::show(move_direction dir, bool clearmotives, bool bSkipShowMotive)
{
    F32 dx;
    F32 dy;
    F32 vx;
    F32 vy;
    F32 a;
    F32 ax;
    F32 ay;
    F32 da;

    if (clearmotives)
    {
        clear_motives();
    }

    activity = ACT_SHOW;

    dx = start_rc.loc.x - rc.loc.x;
    dy = start_rc.loc.y - rc.loc.y;
    a = dx * dx + dy * dy;
    // Retail's constant is 0x322BCC76 = 0.0001f squared in float, one ULP below 1e-8f.
    if (a <= SQR(0.0001f) || bSkipShowMotive)
    {
        rc.loc = start_rc.loc;
        rc.a = start_rc.a;
    }
    else
    {
        a = xsqrt(a);
        vx = 10.0f * dx;
        vy = 10.0f * dy;
        da = -(vx * vx + vy * vy) / (2.0f * a);
        ax = da * dx / a;
        ay = da * dy / a;

        switch (dir)
        {
        case LEFT:
        case RIGHT:
            add_motive(motive(&rc.loc.x, vx, dx, ax, accelerate_motive_update, NULL, 1.0f));
            break;
        case TOP:
        case BOTTOM:
            add_motive(motive(&rc.loc.y, vy, dy, ay, accelerate_motive_update, NULL, 1.0f, 1));
            break;
        }

        add_motive(motive(&rc.a, 3.0f * (start_rc.a - rc.a), start_rc.a - rc.a, 0.0f,
                          linear_motive_update, NULL, 1.0f));
    }
}

void xhud::widget::hide(move_direction d, bool clearmotives, bool bSkipHideMotive)
{
    F32 sx;
    F32 sy;
    F32 cx;
    F32 cy;
    F32 tcx;
    F32 tcy;
    F32 vx;
    F32 vy;
    F32 ex;
    F32 ey;

    if (clearmotives)
    {
        clear_motives();
    }

    activity = ACT_HIDE;

    sx = start_rc.size.x;
    sy = start_rc.size.y;
    cx = (start_rc.loc.x - 0.5f) + 0.5f * sx;
    cy = (start_rc.loc.y - 0.5f) + 0.5f * sy;
    F32 acx = iabs(cx);
    F32 acy = iabs(cy);
    if (iabs(acx + acy) <= 0.0001f)
    {
        rc.a = 0.0f;
        return;
    }

    if (acx > acy)
    {
        tcx = cx >= 0.0f ? 0.5f + sx : -0.5f - sx;
        tcy = tcx * cy / cx;
    }
    else
    {
        tcy = cy >= 0.0f ? 0.5f + sy : -0.5f - sy;
        tcx = tcy * cx / cy;
    }

    ex = 0.5f + (tcx - 0.5f * sx) - rc.loc.x;
    ey = 0.5f + (tcy - 0.5f * sy) - rc.loc.y;
    F32 dist = xsqrt(ex * ex + ey * ey);
    vx = dist * (5.0f * ex);
    vy = dist * (5.0f * ey);

    switch (d)
    {
    case LEFT:
    case RIGHT:
        if (bSkipHideMotive)
        {
            // Retail stores the offset, not the end position.
            rc.loc.x = ex;
        }
        else
        {
            add_motive(
                motive(&rc.loc.x, 0.0f, ex, 5.0f * vx, accelerate_motive_update, NULL, 2.0f));
        }
        break;
    case TOP:
    case BOTTOM:
        if (bSkipHideMotive)
        {
            // Retail stores the offset, not the end position.
            rc.loc.y = ey;
        }
        else
        {
            add_motive(
                motive(&rc.loc.y, 0.0f, ey, 5.0f * vy, accelerate_motive_update, NULL, 2.0f));
        }
        break;
    }

    if (bSkipHideMotive)
    {
        rc.a = 0.0f;
        update(0.0f);
    }
    else
    {
        add_motive(motive(&rc.a, -rc.a, -rc.a, 0.0f, linear_motive_update, NULL, 2.0f));
    }
}

// Dead-stripped placeholder, not recovered code: retail's .sdata2 has 3.0f between
// delay_hide_widget's and hide's literals, from a function between them that the linker
// stripped. Name and body are unknown.
static void __deadstripped_xHud(F32* out)
{
    out[0] = 3.0f;
}

void xhud::widget::delay_hide_widget(move_direction d, F32 time)
{
    switch (d)
    {
    case TOP:
        add_motive(motive(NULL, 0.0f, time, 0.0f, delay_motive_update, (void*)top_hide_widget,
                          time + 0.1f));
        break;
    case BOTTOM:
        add_motive(motive(NULL, 0.0f, time, 0.0f, delay_motive_update, (void*)bottom_hide_widget,
                          time + 0.1f));
        break;
    case LEFT:
        add_motive(motive(NULL, 0.0f, time, 0.0f, delay_motive_update, (void*)left_hide_widget,
                          time + 0.1f));
        break;
    case RIGHT:
        add_motive(motive(NULL, 0.0f, time, 0.0f, delay_motive_update, (void*)right_hide_widget,
                          time + 0.1f));
        break;
    }
}

namespace xhud
{
    namespace
    {

        template <class F> void for_each(U8 type, U32 size, F f)
        {
            zScene& s = *xglobals->sceneCur;
            U8* it = (U8*)s.baseList[type];
            U8* end = it + s.baseCount[type] * size;
            while (it != end)
            {
                widget_chunk* wc = (widget_chunk*)it;
                f(wc->w);
                it += size;
            }
        }
    } // namespace
} // namespace xhud

inline void xhud::widget::disable_all(bool destroy_widgets)
{
    functor_disable f(destroy_widgets);
    for (U32 i = 0; i < sizeof(known_types) / sizeof(known_types[0]); i++)
    {
        for_each(known_types[i].type, known_types[i].size, f);
    }
}

inline void xhud::widget::render_all()
{
    RwCamera* camera = RwCameraGetCurrentCamera();
    if (camera == NULL)
    {
        return;
    }

    zHud::HudCamera->frameBuffer = camera->frameBuffer;
    zHud::HudCamera->zBuffer = camera->zBuffer;
    RwCameraEndUpdate(camera);

    RpWorldAddCamera(xglobals->currWorld, zHud::HudCamera);
    RwCameraBeginUpdateWrapper(zHud::HudCamera);
    xLightKit_Enable(zHud::HudLightKit, xglobals->currWorld);

    for (U32 i = 0; i < sizeof(known_types) / sizeof(known_types[0]); i++)
    {
        for_each(known_types[i].type, known_types[i].size, fp_render);
    }

    xLightKit_Enable(NULL, xglobals->currWorld);
    RwCameraEndUpdate(zHud::HudCamera);
    zHud::HudCamera->frameBuffer = NULL;
    zHud::HudCamera->zBuffer = NULL;
    RpWorldRemoveCamera(xglobals->currWorld, zHud::HudCamera);
    RwCameraBeginUpdateWrapper(camera);
}

inline void xhud::widget::update_all(F32 dt)
{
    functor_update f(dt);
    for (U32 i = 0; i < sizeof(known_types) / sizeof(known_types[0]); i++)
    {
        for_each(known_types[i].type, known_types[i].size, f);
    }
}

namespace xhud
{
    namespace
    {

        void fp_render(widget& w)
        {
            if (w.visible())
            {
                w.render();
            }
        }
    } // namespace
} // namespace xhud

void xhud::widget::add_motive(const motive& m)
{
    motive_node* n = (motive_node*)motive_allocator.alloc();
    new (n) motive(m);

    if (_motive_temp_tail == NULL)
    {
        n->next = _motive_top;
        _motive_top = n;
    }
    else
    {
        if (_motive_temp == NULL)
        {
            _motive_temp_tail = &n->next;
        }
        n->next = _motive_temp;
        _motive_temp = n;
    }
}

void xhud::widget::clear_motives()
{
    activity = ACT_NONE;
    motive_node* it = _motive_top;
    while (it != NULL)
    {
        it->m.finish(*this);
        _motive_top = it->next;
        motive_allocator.free(it);
        it = _motive_top;
    }
}

void xhud::widget::cb_dispatch(xBase* from, xBase* to, U32 event, const F32* argf, xBase* argw,
                               U32 argwi)
{
    ((widget_chunk*)to)->w.dispatch(from, event, argf, argw, argwi);
}

namespace xhud
{
    namespace
    {
        void render_one_model(xModelInstance& m, F32 alpha, const basic_rect<F32>& r,
                              const xVec3& from, const xVec3& to, const xMat4x3& frame)
        {
            U32 oldZWrite;

            xModelSetMaterialAlpha(&m, 255.0f * alpha + 0.5f);
            xModelSetFrame(&m, &frame);

            RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, (void*)&oldZWrite);
            if ((m.Pipe.Flags & 0x30000) == 0x10000)
            {
                RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
                if ((U8)(255.0f * alpha + 0.5f) == 0xff)
                {
                    xModelSetMaterialAlpha(&m, 0xfe);
                }
            }
            else
            {
                RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
            }

            if (m.Pipe.Flags & 0xf00)
            {
                RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)((m.Pipe.Flags >> 8) & 0xf));
            }
            if (m.Pipe.Flags & 0xf000)
            {
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)((m.Pipe.Flags >> 12) & 0xf));
            }

            xModelRender2D(m, r, from, to);

            RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)oldZWrite);
            if (m.Pipe.Flags & 0xf00)
            {
                RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
            }
            if (m.Pipe.Flags & 0xf000)
            {
                RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
            }

            xModelSetMaterialAlpha(&m, 0xff);
        }
    } // namespace
} // namespace xhud

void xhud::render_model(xModelInstance& m, const render_context& rc)
{
    basic_rect<F32> r = { 0.0f, 0.0f, 0.0f, 0.0f };
    r.x = rc.loc.x;
    r.y = rc.loc.y;
    r.w = rc.size.x;
    r.h = rc.size.y;

    xVec3 from = { 0.0f, 0.0f, 1.0f };
    xVec3 to = { 0.0f, 0.0f, -rc.loc.z };

    xMat4x3 frame;
    xMat3x3Euler(&frame, rc.rot.x, rc.rot.y, rc.rot.z);
    frame.right *= 1.0f + rc.loc.z;
    frame.up *= 1.0f + rc.loc.z;
    frame.at *= 0.01f;
    frame.pos.z = 0.0f;
    frame.pos.y = 0.0f;
    frame.pos.x = 0.0f;
    frame.flags = 0;

    for (xModelInstance* model = &m; model != NULL; model = model->Next)
    {
        render_one_model(*model, rc.a, r, from, to, frame);
    }
}

bool xhud::linear_motive_update(widget& w, motive& m, F32 dt)
{
    if (!m.update_time(dt))
    {
        return false;
    }

    F32 diff = dt * m.delta;
    F32 remaining;
    if (!m.inverse)
    {
        remaining = m.max_offset - m.offset;
        if ((diff >= 0.0f && diff >= remaining) || (diff < 0.0f && diff <= remaining))
        {
            *m.value += remaining;
            m.offset = m.max_offset;
            if (m.fp_finish != NULL)
            {
                m.fp_finish(w, m);
            }
            return false;
        }

        *m.value += diff;
        m.offset += diff;
    }
    else
    {
        remaining = m.max_offset - m.offset;
        if (remaining <= 0.0f)
        {
            *m.value -= remaining;
            m.offset = m.max_offset;
            if (m.fp_finish != NULL)
            {
                m.fp_finish(w, m);
            }
            return false;
        }

        *m.value -= diff;
        m.offset += diff;
    }

    return true;
}

bool xhud::accelerate_motive_update(widget& w, motive& m, F32 dt)
{
    if (!m.update_time(dt))
    {
        return false;
    }

    F32 diff = 0.5f * m.accel * dt * dt + m.delta * dt;
    m.delta += dt * m.accel;
    F32 remaining = m.max_offset - m.offset;
    if ((diff >= 0.0f && diff >= remaining) || (diff < 0.0f && diff <= remaining))
    {
        *m.value += remaining;
        m.offset = m.max_offset;
        return false;
    }

    *m.value += diff;
    m.offset += diff;
    return true;
}

bool xhud::shake_motive_update(widget& w, motive& m, F32 dt)
{
    static const F32 mult[4] = { -1.0f, -1.0f, 1.0f, 1.0f };

    if (!m.update_time(dt))
    {
        return false;
    }

    // The context holds the shake step counter.
    U32& iter = *(U32*)&m.context;
    ++iter;
    if (iter > 100)
    {
        iter = 0;
        *m.value -= m.offset;
        m.offset = 0.0f;
        return false;
    }

    U32 i = iter & 0x3;
    F32 diff = m.delta * mult[i];
    if (i == 0)
    {
        m.delta *= m.accel;
    }

    *m.value += diff;
    m.offset += diff;
    return true;
}

void xhud::top_hide_widget(widget& w, motive&)
{
    w.hide(widget::TOP, false, false);
}

void xhud::bottom_hide_widget(widget& w, motive&)
{
    w.hide(widget::BOTTOM, false, false);
}

void xhud::left_hide_widget(widget& w, motive&)
{
    w.hide(widget::LEFT, false, false);
}

void xhud::right_hide_widget(widget& w, motive&)
{
    w.hide(widget::RIGHT, false, false);
}

bool xhud::delay_motive_update(widget& w, motive& m, F32 dt)
{
    if (!m.update_time(dt))
    {
        return false;
    }

    m.offset += dt;
    if (m.max_offset - m.offset < 0.0f)
    {
        ((void (*)(widget&, motive&))m.context)(w, m);
        return false;
    }

    return true;
}

// No symbol in the target: inlined into its callers.
xModelInstance* xhud::load_model(U32 id)
{
    U32 bufsize;
    xModelAssetInfo* minf = (xModelAssetInfo*)xSTFindAsset(xStrHashCat(id, ".minf"), &bufsize);
    if (minf != NULL)
    {
        return zEntRecurseModelInfo(minf, NULL);
    }

    RpAtomic* imodel = (RpAtomic*)xSTFindAsset(id, &bufsize);
    if (imodel == NULL)
    {
        imodel = (RpAtomic*)xSTFindAsset(xStrHashCat(id, ".dff"), &bufsize);
    }
    if (imodel == NULL)
    {
        return NULL;
    }

    return xModelInstanceAlloc(imodel, NULL, 0, 0, NULL);
}
