#include "xHudMeter.h"

#include <stdio.h>

#include "xString.h"
#include "xstransvc.h"
#include "zGame.h"
#include "zScene.h"
#include "zTalkBox.h"
#include "zTextBox.h"

// These names are the first strings of the wad's string pool and the first object of its .data
// in the target.
const char* zHud::ModelAndFontHideOnTextBox::includedTextBoxNames[7] = {
    "MESSAGE_01_TEXTBOX", "MESSAGE_02_TEXTBOX", "MESSAGE_03_TEXTBOX",  "MNUC TELEPORT BOX",
    "TALK_TO_NPC_PROMPT", "PROMPT_TEXTBOX",     "TEXTBOX_BUNGEE_HELP",
};
ztextbox* zHud::ModelAndFontHideOnTextBox::includedTextBoxes[7];

void zHud::ModelAndFont::setup()
{
}

void zHud::ModelAndFont::reset()
{
    if (modelWidget != NULL)
    {
        modelWidget->clear_motives();
    }

    if (fontMeter != NULL)
    {
        fontMeter->clear_motives();
    }

    hide();
}

void zHud::ModelAndFont::setup(const char* modelName, const char* fontMeterName)
{
    U32 meterID;
    xBase* baseWidget;
    char tempName[64];
    U32 hudLightKitID;
    xLightKit* hudLightKit;
    xModelInstance* minst;

    meterID = xStrHash(modelName);
    baseWidget = zSceneFindObject(meterID);

    if (baseWidget != NULL)
    {
        modelWidget = (xhud::model_widget*)&((xhud::widget_chunk*)baseWidget)->w;
        modelWidget->setup();
        modelWidget->enable();

        hudLightKitID = xStrHash("hud_lightkit");
        hudLightKit = (xLightKit*)xSTFindAsset(hudLightKitID, NULL);

        for (minst = modelWidget->model; minst != NULL; minst = minst->Next)
        {
            minst->LightKit = hudLightKit;
        }
    }
    else
    {
        sprintf(tempName, "Missing %s! Get latest MNUI and repack!", modelName);
    }

    meterID = xStrHash(fontMeterName);
    baseWidget = zSceneFindObject(meterID);

    if (baseWidget != NULL)
    {
        fontMeter = (xhud::font_meter_widget*)&((xhud::widget_chunk*)baseWidget)->w;
        fontMeter->setup();
        fontMeter->enable();
    }
    else
    {
        sprintf(tempName, "Missing %s! Get latest MNUI and repack!", fontMeterName);
    }

    reset();
}

void zHud::ModelAndFont::update(F32 dt)
{
    if (valueToWatch != NULL)
    {
        if (*valueToWatch != lastValue)
        {
            fontMeter->set_value(*valueToWatch);
            lastValue = *valueToWatch;
            show(true);
        }
    }
}

void zHud::ModelAndFont::show(bool autohide)
{
    if (!autohide)
    {
        okToAutoHide = FALSE;
    }

    if (modelWidget != NULL)
    {
        modelWidget->show(xhud::widget::TOP, true, false);
        if (okToAutoHide)
        {
            modelWidget->delay_hide_widget(xhud::widget::TOP, 4.0f);
        }
    }

    if (fontMeter != NULL)
    {
        fontMeter->show(xhud::widget::TOP, true, false);
        if (okToAutoHide)
        {
            fontMeter->delay_hide_widget(xhud::widget::TOP, 4.0f);
        }
    }
}

void zHud::ModelAndFont::hide()
{
    okToAutoHide = TRUE;

    if (modelWidget != NULL)
    {
        modelWidget->hide(xhud::widget::TOP, true, false);
    }

    if (fontMeter != NULL)
    {
        fontMeter->hide(xhud::widget::TOP, true, false);
    }
}

void zHud::ModelAndFont::enable()
{
    if (modelWidget != NULL)
    {
        modelWidget->enable();
    }

    if (fontMeter != NULL)
    {
        fontMeter->enable();
    }
}

void zHud::ModelAndFont::disable()
{
    if (modelWidget != NULL)
    {
        modelWidget->disable();
    }

    if (fontMeter != NULL)
    {
        fontMeter->disable();
    }
}

void zHud::ModelAndFontHideOnTextBox::setup(const char* modelName, const char* fontMeterName)
{
    S32 i;

    ModelAndFont::setup(modelName, fontMeterName);
    delayedShow = FALSE;
    hidden = FALSE;

    for (i = 0; i < 7; i++)
    {
        includedTextBoxes[i] = (ztextbox*)zSceneFindObject(xStrHash(includedTextBoxNames[i]));
    }
}

void zHud::ModelAndFontHideOnTextBox::update(F32 dt)
{
    ModelAndFont::update(dt);

    if (!ztalkbox::get_active() && !IsIncludedTextboxActive() && delayedShow)
    {
        delayedShow = FALSE;
        show(true);
    }

    if ((ztalkbox::get_active() || IsIncludedTextboxActive()) && !zGameIsPaused() && !hidden)
    {
        hidden = TRUE;
        hide();
    }
}

void zHud::ModelAndFontHideOnTextBox::show(bool autohide)
{
    if ((ztalkbox::get_active() || IsIncludedTextboxActive()) && !zGameIsPaused())
    {
        delayedShow = TRUE;
    }
    else
    {
        ModelAndFont::show(autohide);
        hidden = FALSE;
    }
}

U8 zHud::ModelAndFontHideOnTextBox::IsIncludedTextboxActive()
{
    S32 i;

    for (i = 0; i < 7; i++)
    {
        if (includedTextBoxes[i] != NULL && includedTextBoxes[i]->flag.active)
        {
            return TRUE;
        }
    }

    return FALSE;
}
