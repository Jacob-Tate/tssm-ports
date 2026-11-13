#include "xHudMeter.h"
#include "zGlobals.h"

zHud::KrabbyPattyMeter::KrabbyPattyMeter()
{
    krabbyPattyMeter = NULL;
    lastHealth = globals.player.s->Health;
    lastMaxHealth = globals.player.s->MaxHealth;
    firstTime = 1;
}

void zHud::KrabbyPattyMeter::reset()
{
    if (krabbyPattyMeter != NULL)
    {
        krabbyPattyMeter->clear_motives();
    }

    lastHealth = globals.player.s->Health;
    lastMaxHealth = globals.player.s->MaxHealth;
    firstTime = 1;
}

void zHud::KrabbyPattyMeter::setup()
{
    U32 meterID;
    xBase* baseWidget;
    U32 hudLightKitID;
    xLightKit* hudLightKit;
    S32 i;
    S32 j;

    meterID = xStrHash("HUD_UNIT_METER_HEALTH");
    baseWidget = zSceneFindObject(meterID);

    if (baseWidget != NULL)
    {
        krabbyPattyMeter = (xhud::unit_meter_widget*)&((xhud::widget_chunk*)baseWidget)->w;
        krabbyPattyMeter->setup();
        krabbyPattyMeter->enable();

        lastHealth = globals.player.s->Health;
        lastMaxHealth = globals.player.s->MaxHealth;
        krabbyPattyMeter->max_value = globals.player.s->MaxHealth;

        hudLightKitID = xStrHash("hud_lightkit");
        hudLightKit = (xLightKit*)xSTFindAsset(hudLightKitID, NULL);

        if (hudLightKit != NULL)
        {
            for (i = 0; i < 2; i++)
            {
                for (j = 0; j < 6; j++)
                {
                    if (krabbyPattyMeter->model[j][i] != NULL)
                    {
                        krabbyPattyMeter->model[j][i]->LightKit = hudLightKit;
                    }
                }
            }
        }

        reset();
    }
}

void zHud::KrabbyPattyMeter::update(F32 dt)
{
    S32 healthIdx;
    F32 delta;

    if (lastHealth != globals.player.s->Health || lastMaxHealth != globals.player.s->MaxHealth ||
        firstTime)
    {
        krabbyPattyMeter->set_value_immediate(globals.player.s->Health);

        delta = 0.1f / dt;
        healthIdx = globals.player.s->Health - 1;

        if (!firstTime && healthIdx >= 0)
        {
            krabbyPattyMeter->show(xhud::widget::TOP, true, false);

            if (lastHealth > globals.player.s->Health)
            {
                xVec3Init(&krabbyPattyMeter->model[healthIdx + 1][0]->Scale, 0.001f, 0.001f,
                          0.001f);
                krabbyPattyMeter->add_motive(
                    xhud::motive(&krabbyPattyMeter->model[healthIdx + 1][0]->Scale.x, delta, 1.0f,
                                 0.0f, xhud::linear_motive_update, NULL, 1.0f));
                krabbyPattyMeter->add_motive(
                    xhud::motive(&krabbyPattyMeter->model[healthIdx + 1][0]->Scale.y, delta, 1.0f,
                                 0.0f, xhud::linear_motive_update, NULL, 1.0f));
                krabbyPattyMeter->add_motive(
                    xhud::motive(&krabbyPattyMeter->model[healthIdx + 1][0]->Scale.z, delta, 1.0f,
                                 0.0f, xhud::linear_motive_update, NULL, 1.0f));
            }
            else
            {
                xVec3Init(&krabbyPattyMeter->model[healthIdx][1]->Scale, 0.001f, 0.001f, 0.001f);
                krabbyPattyMeter->add_motive(
                    xhud::motive(&krabbyPattyMeter->model[healthIdx][1]->Scale.x, delta, 1.0f, 0.0f,
                                 xhud::linear_motive_update, NULL, 1.0f));
                krabbyPattyMeter->add_motive(
                    xhud::motive(&krabbyPattyMeter->model[healthIdx][1]->Scale.y, delta, 1.0f, 0.0f,
                                 xhud::linear_motive_update, NULL, 1.0f));
                krabbyPattyMeter->add_motive(
                    xhud::motive(&krabbyPattyMeter->model[healthIdx][1]->Scale.z, delta, 1.0f, 0.0f,
                                 xhud::linear_motive_update, NULL, 1.0f));
            }
        }

        lastMaxHealth = globals.player.s->MaxHealth;
        krabbyPattyMeter->max_value = globals.player.s->MaxHealth;
        show(true);
    }
}

void zHud::KrabbyPattyMeter::show(bool autohide)
{
    if (krabbyPattyMeter != NULL)
    {
        if (lastHealth != globals.player.s->Health && !firstTime)
        {
            lastHealth = globals.player.s->Health;
        }
        else
        {
            firstTime = 0;
            krabbyPattyMeter->show(xhud::widget::TOP, true, false);
        }

        if (autohide)
        {
            krabbyPattyMeter->delay_hide_widget(xhud::widget::TOP, 4.0f);
        }
    }
}

void zHud::KrabbyPattyMeter::hide()
{
    if (krabbyPattyMeter != NULL)
    {
        krabbyPattyMeter->hide(xhud::widget::TOP, true, false);
    }
}
