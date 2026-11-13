#include "xHudMeter.h"
#include "zGlobals.h"
#include "zEntPlayer.h"
#include "zScene.h"
#include "zUIText.h"

zHud::GoofyGooberMeter::GoofyGooberMeter()
{
}

void zHud::GoofyGooberMeter::setup()
{
    ModelAndFontHideOnTextBox::setup("HUD_MODEL_GOOFY_GOOBER", "HUD_FONT_GOOFY_GOOBER");

    valueToWatch = &globals.player.InvGoofyGoober;
    lastValue = *valueToWatch;
    fontMeter->set_value(*valueToWatch);

    if (gCurrentPlayer == eCurrentPlayerCar)
    {
        ModelAndFont::disable();
    }

    autoSave = (zUIText*)zSceneFindObject(xStrHash("AUTO_SAVE"));
}

void zHud::GoofyGooberMeter::update(F32 dt)
{
    if (gCurrentPlayer != eCurrentPlayerCar)
    {
        if (autoSave != NULL && autoSave->visible)
        {
            hide();
            return;
        }

        ModelAndFontHideOnTextBox::update(dt);
    }
}

void zHud::GoofyGooberMeter::show(bool autohide)
{
    if (gCurrentPlayer != eCurrentPlayerCar)
    {
        ModelAndFont::enable();

        if (autoSave != NULL && autoSave->visible)
        {
            delayedShow = TRUE;
            return;
        }

        ModelAndFontHideOnTextBox::show(autohide);
    }
}
