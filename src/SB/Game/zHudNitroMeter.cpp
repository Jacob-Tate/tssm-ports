#include "xHudMeter.h"
#include "zCar.h"
#include "zEntPlayer.h"

zHud::NitroMeter::NitroMeter()
{
}

void zHud::NitroMeter::setup()
{
    zCar* car;

    ModelAndFontHideOnTextBox::setup("HUD_MODEL_NITRO", "HUD_FONT_NITRO");

    car = zEntPlayerCarGetCar();
    valueToWatch = car->GetVarNitros();
    lastValue = *valueToWatch;
    fontMeter->set_value(*valueToWatch);

    if (gCurrentPlayer != eCurrentPlayerCar)
    {
        ModelAndFont::hide();
    }
}

void zHud::NitroMeter::update(F32 dt)
{
    if (gCurrentPlayer == eCurrentPlayerCar)
    {
        ModelAndFontHideOnTextBox::update(dt);
    }
}

void zHud::NitroMeter::show(bool autohide)
{
    if (gCurrentPlayer == eCurrentPlayerCar)
    {
        ModelAndFontHideOnTextBox::show(autohide);
    }
}
