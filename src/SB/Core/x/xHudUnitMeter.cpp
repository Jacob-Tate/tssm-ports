#include "xHudMeter.h" // unit_meter_widget is declared here (xHudUnitMeter.h is a stale duplicate)

#include "xMath.h"
#include "xMathInlines.h"
#include "xString.h"

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>
#include <stdio.h>

namespace xhud
{
    namespace
    {
        F32 tweak_anim_time_delta = 0.1f;
    }
} // namespace xhud

void xhud::unit_meter_widget::load(xBase& data, xDynAsset& asset, size_t)
{
    init_base(data, asset, sizeof(xBase) + sizeof(unit_meter_widget));
    unit_meter_widget* widget = (unit_meter_widget*)(&data + 1);
    new (widget) unit_meter_widget((unit_meter_asset&)asset);
}

// TSSM assets give positions and sizes in 640x480 screen units.
// anim_time is set in the initializer list: the PS2 demo line table puts its store in the
// constructor's first line, with no line of its own before the loops.
xhud::unit_meter_widget::unit_meter_widget(const unit_meter_asset& a)
    : meter_widget(a), res(a), anim_time(0.0f)
{
    S32 i;
    S32 j;

    for (i = 0; i < 2; i++)
    {
        for (j = 0; j < 6; j++)
        {
            res.model[i].loc.x = (1.0f / 640) * a.model[i].loc.x;
            res.model[i].loc.y = (1.0f / 480) * a.model[i].loc.y;
            res.model[i].size.x = (1.0f / 640) * a.model[i].size.x;
            res.model[i].size.y = (1.0f / 480) * a.model[i].size.y;
            model[j][i] = load_model(res.model[i].id);
        }
    }

    static bool registered = false;
    if (!registered)
    {
        registered = true;
    }

    res.offset.x = (1.0f / 640) * a.offset.x;
    res.offset.y = (1.0f / 480) * a.offset.y;
}

void xhud::unit_meter_widget::destroy()
{
    destruct();
}

U32 xhud::unit_meter_widget::type() const
{
    static U32 myid = xStrHash(unit_meter_asset::type_name());
    return myid;
}

bool xhud::unit_meter_widget::is(U32 id) const
{
    return id == unit_meter_widget::type() || meter_widget::is(id);
}

void xhud::unit_meter_widget::setup()
{
    presetup();

    char tempString[128];
    S32 i;
    char* modelName[2] = { "ModelEmpty", "ModelFull" };

    for (i = 0; i < 2; i++)
    {
        sprintf(tempString, "%s|Location \001X", modelName[i]);
        sprintf(tempString, "%s|Location \001Y", modelName[i]);
        sprintf(tempString, "%s|Location \001Z", modelName[i]);
        sprintf(tempString, "%s|Size \001Width", modelName[i]);
        sprintf(tempString, "%s|Size \001Height", modelName[i]);
    }
}

void xhud::unit_meter_widget::update(F32 dt)
{
    S32 units;
    S32 i;
    S32 which;
    xModelInstance* m;
    F32 duration;
    F32 time;

    // A direct call (as in BfBB): retail's bl goes to updater, not to the weak
    // meter_widget::update.
    meter_widget::updater(dt);

    if (!visible() || rc.a <= 0.5f / 255.0f)
    {
        return;
    }

    anim_time += dt;

    units = 0.5f + max_value;
    if (units > 6)
    {
        units = 6;
    }

    for (i = 0; i < units; i++)
    {
        which = 0;
        if ((res.fill_forward && value >= i + 1) || (!res.fill_forward && value >= units - i))
        {
            which = 1;
        }

        m = model[i][which];
        if (m != NULL && m->Anim != NULL)
        {
            duration = m->Anim->Single->State->Data->Duration;
            if (duration <= 0.0f)
            {
                continue;
            }

            time = i * tweak_anim_time_delta + anim_time;
            if (time > duration)
            {
                time = xfmod(time, duration);
            }

            m->Anim->Single->Time = time;
            xModelEval(m);
        }
    }
}

void xhud::unit_meter_widget::render()
{
    render_context unitrc = rc;
    S32 units;
    S32 i;
    S32 which;

    if (xfeq0(rc.a))
    {
        return;
    }

    units = 0.5f + max_value;
    if (units > 6)
    {
        units = 6;
    }

    for (i = 0; i < units; i++)
    {
        which = 0;
        if ((res.fill_forward && value >= i + 1) || (!res.fill_forward && value >= units - i))
        {
            which = 1;
        }

        if (model[i][which] == NULL)
        {
            continue;
        }

        unitrc.loc.x = rc.loc.x + res.model[which].loc.x + res.offset.x * i;
        unitrc.loc.y = rc.loc.y + res.model[which].loc.y + res.offset.y * i;
        unitrc.loc.z = rc.loc.z + res.model[which].loc.z + res.offset.z * i;
        unitrc.size.x = rc.size.x * res.model[which].size.x;
        unitrc.size.y = rc.size.y * res.model[which].size.y;
        unitrc.size.z = rc.size.z * res.model[which].size.z;

        render_model(*model[i][which], unitrc);
    }
}
