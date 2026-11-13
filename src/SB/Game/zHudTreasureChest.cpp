#include "xHudMeter.h"
#include "zGlobals.h"

zHud::TreasureChest::TreasureChest()
{
}

void zHud::TreasureChest::setup()
{
    ModelAndFontHideOnTextBox::setup("HUD_MODEL_TREASURE_CHEST", "HUD_FONT_TREASURE_CHEST");

    valueToWatch = &globals.player.InvTreasureChestTotal;
    lastValue = *valueToWatch;
    fontMeter->set_value(*valueToWatch);
}
