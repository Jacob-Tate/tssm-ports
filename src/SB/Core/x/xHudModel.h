#ifndef XHUDMODEL_H
#define XHUDMODEL_H

#include "xAnim.h"
#include "xHud.h"

namespace xhud
{
    struct model_asset : asset
    {
        U32 model;

        static const char* type_name()
        {
            return "hud:model";
        }
    };

    struct model_widget : widget
    {
        U32 mid;
        xModelInstance* model;
        F32 x_scale;
        F32 y_scale;

        // No symbol in the target: inlined into load().
        model_widget(const model_asset& masset) : widget(masset), mid(masset.model), model(NULL)
        {
            model = load_model(mid);
            x_scale = 1.0f;
            y_scale = 1.0f;
        }

        static void load(xBase& data, xDynAsset& asset, size_t);

        void destruct()
        {
            widget::destruct();
        }

        virtual void destroy();
        virtual U32 type() const;
        virtual bool is(U32 id) const;
        virtual void update(F32 dt);
        virtual void render();
        virtual void set_params(xModelInstance*& model, render_context& rc, F32 xs, F32 ys, F32 lx,
                                F32 ly);
    };

} // namespace xhud

xAnimTable* XHUD_AnimTable_Idle();
xAnimTable* xHUDCreateAnimTable();

#endif
