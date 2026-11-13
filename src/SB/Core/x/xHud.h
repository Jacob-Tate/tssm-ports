#ifndef XHUD_H
#define XHUD_H

#include "xBase.h"
#include "xVec3.h"
#include "xDynAsset.h"
#include "xModel.h"
#include "iColor.h"
#include "xMemMgr.h"

typedef struct asset;
typedef struct widget;

namespace xhud
{
    struct block_allocator
    {
        struct holder;

        struct header
        {
            holder* next;
        };

        struct holder
        {
            header h;
            U8 data[1];
        };

        S32 block_size;
        S32 max_blocks;
        holder* head;
        void* buffer;

        void init(S32 size, S32 count)
        {
            block_size = size;
            max_blocks = count;
            buffer = xMemAlloc(gActiveHeap, block_size * max_blocks, 0);

            U8* it = (U8*)buffer;
            U8* end = it + (max_blocks - 1) * block_size;
            while (it != end)
            {
                ((holder*)it)->h.next = (holder*)(it + block_size);
                it += block_size;
            }
            ((holder*)it)->h.next = NULL;

            head = (holder*)buffer;
        }

        void* alloc()
        {
            holder* h = head;
            head = head->h.next;
            return h->data;
        }

        void free(void* ptr)
        {
            holder* h = (holder*)((U8*)ptr - sizeof(header));
            h->h.next = head;
            head = h;
        }
    };

    struct render_context
    {
        xVec3 loc;
        xVec3 size;
        xVec3 rot;
        F32 r;
        F32 g;
        F32 b;
        F32 a;
    };

    struct asset : xDynAsset
    {
        xVec3 loc;
        xVec3 size;

        static const char* type_name()
        {
            return "hud";
        }
    };

    struct motive;
    struct motive_node;

    struct widget
    {
        enum move_direction
        {
            LEFT,
            RIGHT,
            TOP,
            BOTTOM
        };

        struct
        {
            U8 visible;
            U8 enabled;
            U8 active;
            F32 timer;
        } flag;
        render_context rc;
        render_context start_rc;
        const asset* a;
        enum _enum
        {
            ACT_NONE,
            ACT_SHOW,
            ACT_HIDE,
            MAX_ACT
        } activity;

        virtual void destroy()
        {
        }
        virtual U32 type() const;
        virtual bool is(U32 id) const;
        virtual void init()
        {
        }
        virtual void setup()
        {
            presetup();
        }
        virtual void update(F32 dt)
        {
            updater(dt);
        }
        virtual void render()
        {
        }
        virtual void dispatch(xBase* b1, U32 event, const F32* toParam, xBase* b2,
                              U32 toParamWidgetID)
        {
            dispatcher(b1, event, toParam, b2, toParamWidgetID);
        }

        motive_node* _motive_top;
        motive_node* _motive_temp;
        motive_node** _motive_temp_tail;

        static void disable_all(bool);
        static void setup_all();
        static void update_all(F32 dt);
        static void render_all();
        static void scene_enter();

        static void cb_dispatch(xBase*, xBase*, U32, const F32*, xBase*, U32);

        // No symbol in the target: inlined into every widget load().
        static void init_base(xBase& data, const xBaseAsset& asset, unsigned long chunk_size)
        {
            xBaseInit(&data, &asset);
            data.eventFunc = cb_dispatch;
            if (data.linkCount != 0)
            {
                // The links follow the whole widget chunk in the asset.
                data.link = (xLinkAsset*)((U8*)&asset + chunk_size);
            }
        }

        // No symbol in the target: inlined into the derived widget constructors.
        // TSSM assets give positions/sizes in 640x480 screen units.
        widget(const asset& a)
        {
            this->a = &a;
            _motive_top = NULL;
            _motive_temp = NULL;
            _motive_temp_tail = NULL;
            flag.visible = 1;
            flag.enabled = 0;
            rc.loc.x = (1.0f / 640) * a.loc.x;
            rc.loc.y = (1.0f / 480) * a.loc.y;
            rc.loc.z = a.loc.z;
            rc.size.x = (1.0f / 640) * a.size.x;
            rc.size.y = (1.0f / 480) * a.size.y;
            // rc.size.z is left unset, as in the target.
            rc.r = rc.g = rc.b = rc.a = 1.0f;
            start_rc = rc;
        }
        // Not virtual and never called (widgets are placement-new'd and never deleted), so retail
        // has no symbol for it. Its presence still shapes the derived constructors' code
        // (unit_meter_widget, model/image_widget::load).
        ~widget()
        {
        }
        // No destruct symbol exists in the target: the destroy() overrides inline it
        // down to disable().
        void destruct()
        {
            disable();
        }
        void presetup();
        void updater(F32 dt);
        void dispatcher(xBase*, U32, const F32*, xBase*, U32);

        void disable()
        {
            flag.enabled = 0;
        }

        void add_tweaks()
        {
        }

        void enable()
        {
            flag.enabled = 1;
        }

        void clear_motives();
        void clear_motives(bool (*fp_update)(widget&, motive&, F32), void* context);
        void add_motive(const motive& m);
        void delay_hide_widget(move_direction d, F32 time);
        void hide(move_direction d, bool clearmotives, bool bSkipHideMotive);
        void show(move_direction dir, bool clearmotives, bool bSkipShowMotive);

        bool visible() const
        {
            return flag.visible && enabled();
        }

        U8 enabled() const
        {
            return flag.enabled;
        }

        static block_allocator motive_allocator;
    };

    struct motive
    {
        union
        {
            F32* value;
            xColor_tag* value_color;
        };
        union
        {
            F32 delta;
            F32 on_time;
        };
        union
        {
            F32 max_offset;
            F32 total_time;
            F32 begin_scale;
        };
        union
        {
            F32 accel;
            F32 fade_time;
            F32 final_scale;
        };
        union
        {
            void* context;
            xColor_tag on_color;
            F32 begin_alpha;
        };
        union
        {
            F32 period;
            xColor_tag off_color;
            F32 end_alpha;
        };
        union
        {
            F32 elapsed;
            F32 orig_width;
        };
        union
        {
            F32 offset;
            F32 current_scale;
        };
        bool (*fp_update)(widget&, motive&, F32);
        bool (*fp_finish)(widget&, motive&);
        union
        {
            F32 start_delta;
            F32 orig_height;
        };
        U8 hold;
        U8 inverse;

        // The trailing parameters follow Ratatouille's GameCube DWARF of this constructor.
        motive(F32* value, F32 delta, F32 max_offset, F32 accel,
               bool (*fp_update)(xhud::widget&, motive&, F32), void* context, F32 period,
               U8 _hold = 0, U8 _inverse = 0, bool (*finish)(xhud::widget&, motive&) = NULL)
        {
            this->value = value;
            this->delta = delta;
            this->max_offset = max_offset;
            this->accel = accel;
            this->context = context;
            this->period = period;
            this->elapsed = 0.0f;
            this->offset = 0.0f;
            this->fp_update = fp_update;
            this->fp_finish = finish;
            this->start_delta = delta;
            this->hold = _hold;
            this->inverse = _inverse;
        }

        // Motives with a non-zero period expire once they have run for longer than that many
        // seconds. Inlined at the start of every *_motive_update.
        bool update_time(F32 dt)
        {
            if (!period)
            {
                return true;
            }

            elapsed += dt;
            if (elapsed > period)
            {
                elapsed = 0.0f;
                return false;
            }
            return true;
        }

        bool update(widget& w, F32 dt)
        {
            return fp_update(w, *this, dt);
        }

        void finish(widget& w)
        {
            bool finished = true;
            if (fp_finish != NULL)
            {
                finished = fp_finish(w, *this);
            }

            if (finished)
            {
                if (value != NULL)
                {
                    *value += (max_offset - offset);
                }
                offset = max_offset;
            }
        }
    };

    struct motive_node
    {
        motive m;
        motive_node* next;
    };

    inline void widget::updater(F32 dt)
    {
        if (flag.active)
        {
            flag.timer = dt + flag.timer;
        }

        _motive_temp_tail = &_motive_temp;

        motive_node** prev = &_motive_top;
        motive_node* it = _motive_top;
        while (it != NULL)
        {
            if (!it->m.update(*this, dt))
            {
                *prev = it->next;
                if (it == _motive_top)
                {
                    _motive_top = it->next;
                }
                motive_allocator.free(it);
            }
            else
            {
                prev = &it->next;
                if (it->m.hold)
                {
                    break;
                }
            }
            it = *prev;
        }

        if (_motive_temp != NULL)
        {
            *_motive_temp_tail = _motive_top;
            _motive_top = _motive_temp;
            _motive_temp = NULL;
        }
        _motive_temp_tail = NULL;

        if (_motive_top == NULL)
        {
            activity = ACT_NONE;
        }
    }

    // A widget as stored in the scene's base list: the xBase header directly followed by the
    // widget itself.
    struct widget_chunk : xBase
    {
        widget w;
    };

    void init();
    void setup();
    void destroy();
    void update(F32 dt);
    void render();
    void render_model(xModelInstance& m, const render_context& rc);

    bool linear_motive_update(widget& w, motive& m, F32);
    bool accelerate_motive_update(widget& w, motive& m, F32);
    bool shake_motive_update(widget& w, motive& m, F32);
    bool delay_motive_update(widget& w, motive& m, F32);

    void top_hide_widget(widget& w, motive&);
    void bottom_hide_widget(widget& w, motive&);
    void left_hide_widget(widget& w, motive&);
    void right_hide_widget(widget& w, motive&);

    xModelInstance* load_model(U32);
}; // namespace xhud

#endif
