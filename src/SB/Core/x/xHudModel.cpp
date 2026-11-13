#include "xHudModel.h"

#include "xString.h"

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>

xAnimTable* xHUDCreateAnimTable()
{
    xAnimTable* table;

    table = xAnimTableNew("xHUD", 0);

    xAnimTableNewState(table, "Idle01", 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    return table;
}

void xhud::model_widget::load(xBase& data, xDynAsset& asset, size_t)
{
    init_base(data, asset, sizeof(xBase) + sizeof(model_widget));
    model_widget* widget = (model_widget*)(&data + 1);
    new (widget) model_widget((model_asset&)asset);
}

void xhud::model_widget::destroy()
{
    destruct();
}

U32 xhud::model_widget::type() const
{
    static U32 myid = xStrHash(model_asset::type_name());
    return myid;
}

bool xhud::model_widget::is(U32 id) const
{
    return id == model_widget::type() || widget::is(id);
}

void xhud::model_widget::update(F32 dt)
{
    widget::updater(dt);

    if (!visible() || rc.a <= (0.5f / 255.0f))
    {
        return;
    }

    xModelUpdate(model, dt);
    xModelEval(model);
}

void xhud::model_widget::set_params(xModelInstance*& model, render_context& rc, F32 xs, F32 ys,
                                    F32 lx, F32 ly)
{
    x_scale = xs;
    y_scale = ys;
    model->Scale.x = xs;
    model->Scale.y = ys;
    rc.loc.x = lx;
    rc.loc.y = ly;
}

void xhud::model_widget::render()
{
    if (rc.a >= -0.00001f && rc.a <= 0.00001f)
    {
        return;
    }

    model->Scale.x = x_scale;
    model->Scale.y = y_scale;
    model->Scale.z = 1.0f;

    render_model(*model, rc);
}
