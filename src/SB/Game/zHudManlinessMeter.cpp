#include "xHudMeter.h"
#include "xMath.h"
#include "xString.h"
#include "iMath.h"
#include "zGlobals.h"
#include "zRewards.h"

#include <string.h>

zHud::Combo zHud::ManlinessMeter::combos[16];
U32 zHud::ManlinessMeter::comboIndex;
U32 zHud::ManlinessMeter::lastComboIndex;

F32 zHud::ComboGlobals::Time[16];
U32 zHud::ComboGlobals::Combo15Count;

zHud::ManlinessMeter::ManlinessMeter()
{
    lastManlinessPoints = globals.player.InvManlinessPoints;
    memset(nextUpgrade, 0, sizeof(nextUpgrade));
    manlinessPointsComboText = NULL;
    manlinessPointsComboPointsText = NULL;
    firstTimeUpdate = 1;
    memset(widgets, 0, sizeof(widgets));
    memset(combos, 0, sizeof(combos));
    comboIndex = 0;
    lastComboIndex = 0;
    lastMaxManlinessPoints = 0;
    lastUnusedSkillPoints = 0;
}

void zHud::ManlinessMeter::reset()
{
    S32 i;

    for (i = 0; i < 8; i++)
    {
        if (widgets[i] != NULL)
        {
            widgets[i]->clear_motives();
        }
    }

    hide();
}

// not in the DWARF: barPart. Both builds load barPartsWidgets[0] once and keep it across the
// texture scroller setup; retail's register order needs it to come from the BarMeter::GetBarPart
// accessor.
void zHud::ManlinessMeter::setup()
{
    xhud::widget* barPart;
    U32 meterID;
    xBase* baseWidget;
    U32 hudLightKitID;
    xVec3 location;
    S32 i;
    char comboString[32];
    char* textString;

    lastManlinessPoints = 0;

    hudLightKitID = xStrHash("hud_lightkit");

    meterID = xStrHash("HUD_MODEL_MANILESS_POINTS");
    baseWidget = zSceneFindObject(meterID);
    if (baseWidget != NULL)
    {
        bar = (xhud::model_widget*)&((xhud::widget_chunk*)baseWidget)->w;
        bar->setup();
        bar->enable();
        sizeOfActualBar = bar->a->size.x;
        barPartsWidgets[0] = bar;
    }

    if (baseWidget != NULL)
    {
        barPart = GetBarPart(0);

        sizeOfActualBar = barPart->a->size.x - 28.0f;

        RwTexCoords low = { 0.0f, 0.5f };
        RwTexCoords high = { 0.5f, 1.0f };

        manlinessTextureScroller.setup((xhud::model_widget*)barPart, low, high, 0.0f, -0.5f,
                                       eDIRECTION_Y);
        textureScroller = &manlinessTextureScroller;
        widgets[0] = barPart;
    }
    else
    {
        widgets[0] = NULL;
    }

    meterID = xStrHash("HUD_MODEL_MANILESS_POINTS_NOTCH");
    baseWidget = zSceneFindObject(meterID);
    if (baseWidget != NULL)
    {
        barNotch = (xhud::model_widget*)&((xhud::widget_chunk*)baseWidget)->w;
        barNotch->setup();
        barNotch->enable();
        notchLocation = barNotch->rc.loc;
        barPartsWidgets[1] = barNotch;
    }

    if (baseWidget != NULL)
    {
        barPart = GetBarPart(1);

        location.x = barPart->rc.loc.x - 0.00625f;
        location.y = barPart->rc.loc.y;
        location.z = barPart->rc.loc.z;
        notchLocation = location;
        widgets[1] = barPart;
    }
    else
    {
        widgets[1] = NULL;
    }

    meterID = xStrHash("HUD_FONT_MANLINESS_POINTS");
    baseWidget = zSceneFindObject(meterID);
    if (baseWidget != NULL)
    {
        barMeter = (xhud::font_meter_widget*)&((xhud::widget_chunk*)baseWidget)->w;
        barMeter->setup();
        barMeter->enable();
        barPartsWidgets[2] = barMeter;
        fontLocation.x = barMeter->rc.loc.x - barMeter->rc.size.x / 2.0f;
        fontLocation.y = barMeter->rc.loc.y - barMeter->rc.size.y / 2.0f;
        fontLocation.z = barMeter->rc.loc.z;
    }

    if (baseWidget != NULL)
    {
        valueToWatch = &globals.player.InvManlinessPoints;
        barMeter->set_value_immediate(globals.player.InvManlinessPoints);
        minValueToWatch = &minManlinessPoints;
        maxValueToWatch = &maxManlinessPoints;
        widgets[2] = barPartsWidgets[2];
    }
    else
    {
        widgets[2] = NULL;
    }

    hudLightKit = (xLightKit*)xSTFindAsset(hudLightKitID, NULL);

    if (bar != NULL)
    {
        bar->model->LightKit = hudLightKit;
    }

    if (barNotch != NULL)
    {
        barNotch->model->LightKit = hudLightKit;
    }

    meterID = xStrHash("HUD_MODEL_UPGRADE_ICON");
    baseWidget = zSceneFindObject(meterID);
    if (baseWidget != NULL)
    {
        upgradeIcon = (xhud::model_widget*)&((xhud::widget_chunk*)baseWidget)->w;
        upgradeIcon->setup();
        upgradeIcon->disable();
        widgets[5] = upgradeIcon;
        upgradeIcon->model->LightKit = hudLightKit;
    }
    else
    {
        widgets[5] = NULL;
    }

    meterID = xStrHash("HUD_TEXT_MANLINESS_UPGRADE");
    baseWidget = zSceneFindObject(meterID);
    if (baseWidget != NULL)
    {
        maxValueText = (xhud::text_widget*)&((xhud::widget_chunk*)baseWidget)->w;
        maxValueText->setup();
        maxValueText->enable();
        barPartsWidgets[4] = maxValueText;
    }

    if (baseWidget != NULL)
    {
        maxManlinessPoints = globals.player.rewardsMgr->GetUpgradeRangeUpper();
        widgets[4] = barPartsWidgets[4];
    }
    else
    {
        widgets[4] = NULL;
    }

    meterID = xStrHash("HUD_TEXT_COMBO_TEXT");
    baseWidget = zSceneFindObject(meterID);
    if (baseWidget != NULL)
    {
        manlinessPointsComboText = (xhud::text_widget*)&((xhud::widget_chunk*)baseWidget)->w;
        manlinessPointsComboText->setup();
        manlinessPointsComboText->disable();
        widgets[6] = manlinessPointsComboText;
    }
    else
    {
        widgets[6] = NULL;
    }

    meterID = xStrHash("HUD_TEXT_COMBO_POINTS");
    baseWidget = zSceneFindObject(meterID);
    if (baseWidget != NULL)
    {
        manlinessPointsComboPointsText = (xhud::text_widget*)&((xhud::widget_chunk*)baseWidget)->w;
        manlinessPointsComboPointsText->setup();
        manlinessPointsComboPointsText->disable();
        widgets[7] = manlinessPointsComboPointsText;
    }
    else
    {
        widgets[7] = NULL;
    }

    for (i = 3; i < 16; i++)
    {
        sprintf(comboString, "combo_text%d", i);
        textString = xTextFindString(comboString, NULL);
        if (textString != NULL)
        {
            strcpy(combos[i].comboText, textString);
        }
    }

    combos[3].callback = Combo3Effect;
    combos[4].callback = Combo4Effect;
    combos[5].callback = Combo5Effect;
    combos[6].callback = Combo6Effect;
    combos[7].callback = Combo7Effect;
    combos[8].callback = Combo8Effect;
    combos[9].callback = Combo9Effect;
    combos[10].callback = Combo10Effect;
    combos[11].callback = Combo11Effect;
    combos[12].callback = Combo12Effect;
    combos[13].callback = Combo13Effect;
    combos[14].callback = Combo14Effect;
    combos[15].callback = Combo15Effect;

    upgradeSoundGroupHandle = xSndMgrGetSoundGroup(xStrHash("CHEAT_SUCCESS"));

    reset();
}

void zHud::ManlinessMeter::SetImmediateValue(U32 value)
{
    if (widgets[2] != NULL)
    {
        ((xhud::meter_widget*)widgets[2])->set_value_immediate(value);
    }
}

void zHud::ManlinessMeter::update(F32 dt)
{
    U32 comboPoints;
    F32 delta;

    if (lastManlinessPoints != globals.player.InvManlinessPoints || firstTimeUpdate)
    {
        firstTimeUpdate = 0;
        comboIndex = globals.player.rewardsMgr->GetLastComboIndex();
        comboPoints = globals.player.rewardsMgr->GetLastComboManlinessPoints();
        lastManlinessPoints = globals.player.InvManlinessPoints;
        minManlinessPoints = globals.player.rewardsMgr->GetUpgradeRangeLower();
        maxManlinessPoints = globals.player.rewardsMgr->GetUpgradeRangeUpper();

        BarMeter<U32>::update(dt);

        if (comboPoints != 0)
        {
            manlinessPointsComboPointsText->enable();
            sprintf(manlinessPointsComboPointsText->text,
                    "{c=ffffe600}{font=0}{font=0}{w*0.9}{h*0.9}+%d", comboPoints);
        }

        if (comboIndex >= 3)
        {
            manlinessPointsComboText->enable();
        }

        if (globals.player.CurrentUnusedSkillPoints != 0)
        {
            upgradeIcon->enable();

            if (lastUnusedSkillPoints != globals.player.CurrentUnusedSkillPoints)
            {
                delta = 0.1f / dt;

                xVec3Init(&upgradeIcon->model->Scale, 0.001f, 0.001f, 0.001f);
                upgradeIcon->x_scale = 0.001f;
                upgradeIcon->y_scale = 0.001f;

                upgradeIcon->show(xhud::widget::TOP, true, false);
                upgradeIcon->add_motive(xhud::motive(&upgradeIcon->x_scale, delta, 1.0f, 0.0f,
                                                     xhud::linear_motive_update, NULL, 1.0f));
                upgradeIcon->add_motive(xhud::motive(&upgradeIcon->y_scale, delta, 1.0f, 0.0f,
                                                     xhud::linear_motive_update, NULL, 1.0f));

                if (upgradeSound != -1 && !xSndMgrIsPlaying(upgradeSoundGroupHandle))
                {
                    upgradeSound =
                        xSndMgrPlay(upgradeSoundGroupHandle, 0, NULL, NULL, NULL, NULL, NULL);
                }
            }
        }
        else
        {
            xVec3Init(&upgradeIcon->model->Scale, 0.001f, 0.001f, 0.001f);
            upgradeIcon->x_scale = 0.001f;
            upgradeIcon->y_scale = 0.001f;
            upgradeIcon->disable();
        }

        show(true);
    }

    if (globals.player.CurrentUnusedSkillPoints == 0)
    {
        upgradeIcon->disable();
    }
}

void zHud::ManlinessMeter::show(bool autohide)
{
    S32 i;

    for (i = 0; i < 8; i++)
    {
        if (widgets[i] != NULL)
        {
            switch (i)
            {
            case 2:
                widgets[i]->show(xhud::widget::TOP, true, false);
                if (autohide)
                {
                    widgets[i]->delay_hide_widget(xhud::widget::TOP, 4.0f);
                }
                break;
            case 7:
                if (widgets[i]->enabled())
                {
                    widgets[i]->show(xhud::widget::TOP, true, false);
                    widgets[i]->add_motive(xhud::motive(NULL, 0.0f, 0.9f, 0.0f,
                                                        xhud::delay_motive_update,
                                                        (void*)HideAndClearText, 1.0f));
                }
                break;
            case 6:
                if (widgets[i]->enabled() && comboIndex < 16 &&
                    (ComboGlobals::Time[comboIndex] == 0.0f || lastComboIndex != comboIndex))
                {
                    lastComboIndex = comboIndex;
                    ComboGlobals::Time[comboIndex] = 0.0f;
                    widgets[i]->show(xhud::widget::TOP, true, false);

                    if (comboIndex >= 3 && combos[comboIndex].callback != NULL)
                    {
                        switch (comboIndex)
                        {
                        case 14:
                            widgets[i]->add_motive(xhud::motive(&widgets[i]->rc.loc.x, 0.01f, 0.0f,
                                                                0.7f, xhud::shake_motive_update,
                                                                NULL, 0.0f));
                            break;
                        case 15:
                            ComboGlobals::Combo15Count = 0;
                            break;
                        }

                        widgets[i]->add_motive(xhud::motive(NULL, 0.0f, 0.9f, 0.0f,
                                                            combos[comboIndex].callback,
                                                            (void*)HideAndClearText, 1.0f));
                    }
                    else
                    {
                        widgets[i]->add_motive(xhud::motive(NULL, 0.0f, 0.9f, 0.0f,
                                                            xhud::delay_motive_update,
                                                            (void*)HideAndClearText, 1.0f));
                    }
                }
                break;
            case 4:
                widgets[i]->show(xhud::widget::TOP, true, false);
                if (autohide)
                {
                    if (lastMaxManlinessPoints != maxManlinessPoints)
                    {
                        lastMaxManlinessPoints = maxManlinessPoints;
                        widgets[i]->add_motive(xhud::motive(&widgets[i]->rc.loc.x, 0.01f, 0.0f,
                                                            0.7f, xhud::shake_motive_update, NULL,
                                                            0.0f));
                    }
                    widgets[i]->delay_hide_widget(xhud::widget::TOP, 4.2f);
                }
                break;
            case 5:
                if (globals.player.CurrentUnusedSkillPoints != 0)
                {
                    if (lastUnusedSkillPoints != globals.player.CurrentUnusedSkillPoints)
                    {
                        lastUnusedSkillPoints = globals.player.CurrentUnusedSkillPoints;
                        widgets[i]->delay_hide_widget(xhud::widget::TOP, 4.2f);
                    }
                    else
                    {
                        xVec3Init(&upgradeIcon->model->Scale, 1.0f, 1.0f, 1.0f);
                        upgradeIcon->x_scale = 1.0f;
                        upgradeIcon->y_scale = 1.0f;
                        widgets[i]->show(xhud::widget::TOP, true, false);
                        widgets[i]->delay_hide_widget(xhud::widget::TOP, 4.2f);
                    }
                }
                else
                {
                    widgets[i]->disable();
                }
                break;
            default:
                widgets[i]->show(xhud::widget::TOP, true, false);
                if (autohide)
                {
                    widgets[i]->delay_hide_widget(xhud::widget::TOP, 4.2f);
                }
                break;
            }
        }
    }
}

void zHud::ManlinessMeter::hide()
{
    S32 i;

    for (i = 0; i < 8; i++)
    {
        if (widgets[i] != NULL)
        {
            widgets[i]->hide(xhud::widget::TOP, true, false);
        }
    }
}

void zHud::ManlinessMeter::ClearText(xhud::widget& w, xhud::motive&)
{
    xhud::text_widget& textWidget = (xhud::text_widget&)w;

    strcpy(textWidget.text, "");
    textWidget.disable();

    if (textWidget.a->id == xStrHash("HUD_TEXT_COMBO_TEXT"))
    {
        ComboGlobals::Time[globals.player.rewardsMgr->GetLastComboIndex()] = 0.0f;
        ComboGlobals::Combo15Count = 0;
        globals.player.rewardsMgr->ResetLastComboIndex();
    }
    else if (textWidget.a->id == xStrHash("HUD_TEXT_COMBO_POINTS"))
    {
        globals.player.rewardsMgr->ResetLastComboManlinessPoints();
    }

    textWidget.rc.loc.x = (1.0f / 640) * textWidget.a->loc.x;
    textWidget.rc.loc.y = (1.0f / 480) * textWidget.a->loc.y;
    textWidget.rc.loc.z = textWidget.a->loc.z;
}

// Slides the widget off the screen edge nearest to its centre (like xhud::widget::hide, but only
// vertically), fades it out and clears its text afterwards (ClearText).
void zHud::ManlinessMeter::HideAndClearText(xhud::widget& w, xhud::motive&)
{
    F32 sx;
    F32 sy;
    F32 cx;
    F32 cy;
    F32 tcx;
    F32 tcy;
    F32 ex;
    F32 ey;

    w.activity = xhud::widget::ACT_HIDE;

    sx = w.start_rc.size.x;
    sy = w.start_rc.size.y;
    cx = (w.start_rc.loc.x - 0.5f) + 0.5f * sx;
    cy = (w.start_rc.loc.y - 0.5f) + 0.5f * sy;
    // acx, acy and dist are not in the PS2 DWARF (copy-propagated away there); this is the code of
    // xhud::widget::hide, whose BfBB DWARF lists them.
    F32 acx = iabs(cx);
    F32 acy = iabs(cy);

    if (iabs(acx + acy) <= 0.0001f)
    {
        w.rc.a = 0.0f;
        return;
    }

    if (acx > acy)
    {
        if (cx >= 0.0f)
        {
            ex = 0.5f + sx;
        }
        else
        {
            ex = -0.5f - sx;
        }
        tcx = ex;
        tcy = (ex * cy) / cx;
    }
    else
    {
        if (cy >= 0.0f)
        {
            ey = 0.5f + sy;
        }
        else
        {
            ey = -0.5f - sy;
        }
        tcy = ey;
        tcx = (ey * cx) / cy;
    }

    ex = 0.5f + (tcx - 0.5f * sx) - w.rc.loc.x;
    ey = 0.5f + (tcy - 0.5f * sy) - w.rc.loc.y;

    F32 dist = xsqrt(ex * ex + ey * ey);
    w.add_motive(xhud::motive(&w.rc.loc.y, 0.0f, ey, 5.0f * (dist * (5.0f * ey)),
                              xhud::accelerate_motive_update, NULL, 2.0f));
    w.add_motive(
        xhud::motive(&w.rc.a, -w.rc.a, -w.rc.a, 0.0f, xhud::linear_motive_update, NULL, 2.0f));
    w.add_motive(
        xhud::motive(NULL, 0.0f, 0.1f, 0.0f, xhud::delay_motive_update, (void*)ClearText, 0.2f));

    if (w.a->id == xStrHash("HUD_TEXT_COMBO_TEXT"))
    {
        sprintf(((xhud::text_widget&)w).text, "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s",
                combos[comboIndex].comboText);
    }
}

// The ComboNEffect text animations share these inline helpers (original names unknown: no symbols,
// and the PS2 line table attributes the whole strlen..memcpy sequence to a single caller line).

// Advances a combo text animation timer and returns how many characters of the text are revealed.
static inline U32 GetComboTextPos(char* text, F32& time, F32 dt)
{
    size_t len = strlen(text);
    U32 pos;

    time += dt;
    pos = 0.5f + time / (0.9f / len);
    if (pos >= len)
    {
        pos = len;
    }

    return pos;
}

// GetComboTextPos that also copies the revealed part of the text into pos1.
static inline U32 GetComboText(char* text, F32& time, F32 dt, char* pos1)
{
    size_t len = strlen(text);
    U32 pos;

    time += dt;
    pos = 0.5f + time / (0.9f / len);
    if (pos >= len)
    {
        pos = len;
    }

    memset(pos1, 0, len + 1);
    memcpy(pos1, text, pos);

    return pos;
}

// GetComboTextPos that also splits the text into the revealed part (pos1) and the rest (pos2).
static inline U32 GetComboText(char* text, F32& time, F32 dt, char* pos1, char* pos2)
{
    size_t len = strlen(text);
    U32 pos;

    time += dt;
    pos = 0.5f + time / (0.9f / len);
    if (pos >= len)
    {
        pos = len;
    }

    memset(pos1, 0, len + 1);
    memcpy(pos1, text, pos);
    memset(pos2, 0, len + 1);
    memcpy(pos2, text + pos, len - pos);

    return pos;
}

bool zHud::ManlinessMeter::Combo3Effect(xhud::widget& w, xhud::motive& m, F32 dt)
{
    sprintf(((xhud::text_widget&)w).text, "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s",
            combos[comboIndex].comboText);
    return xhud::delay_motive_update(w, m, dt);
}

bool zHud::ManlinessMeter::Combo4Effect(xhud::widget& w, xhud::motive& m, F32 dt)
{
    xhud::text_widget& textWidget = (xhud::text_widget&)w;
    char pos1[64];

    if (ComboGlobals::Time[4] >= 0.9f)
    {
        ComboGlobals::Time[4] = 0.0f;
    }

    if (combos[4].comboText != NULL)
    {
        U32 pos = GetComboText(combos[4].comboText, ComboGlobals::Time[4], dt, pos1);

        if (pos != 0)
        {
            sprintf(textWidget.text, "{c=ffffe600}%s", pos1);
        }
        else
        {
            strcpy(textWidget.text, "");
        }
    }

    return xhud::delay_motive_update(w, m, dt);
}

bool zHud::ManlinessMeter::Combo5Effect(xhud::widget& w, xhud::motive& m, F32 dt)
{
    xhud::text_widget& textWidget = (xhud::text_widget&)w;
    char pos1[64];
    char pos2[64];
    // comboText[192] in the PS2 DWARF; retail's GC frame (0x140) only has room for 128.
    char comboText[128];

    if (ComboGlobals::Time[5] >= 0.9f)
    {
        ComboGlobals::Time[5] = 0.0f;
    }

    if (combos[5].comboText != NULL)
    {
        GetComboText(combos[5].comboText, ComboGlobals::Time[5], dt, pos1, pos2);

        sprintf(comboText, "{font=0}{w*0.9}{h*0.9}{c=ffdd3333}%s{c=ffffe600}%s{~:c}", pos1, pos2);
        textWidget.set_text(comboText);
    }

    return xhud::delay_motive_update(w, m, dt);
}

bool zHud::ManlinessMeter::Combo6Effect(xhud::widget& w, xhud::motive& m, F32 dt)
{
    xhud::text_widget& textWidget = (xhud::text_widget&)w;
    char pos1[64];
    char pos2[64];
    char comboText[128];

    if (ComboGlobals::Time[6] >= 0.9f)
    {
        ComboGlobals::Time[6] = 0.0f;
    }

    if (combos[6].comboText != NULL)
    {
        U32 pos = GetComboText(combos[6].comboText, ComboGlobals::Time[6], dt, pos1, pos2);

        if (pos != 0)
        {
            pos1[pos - 1] = '\0';
            sprintf(comboText,
                    "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s{c=ff3333dd}%c{c=ffffe600}%s{~:c}", pos1,
                    combos[6].comboText[pos - 1], pos2);
        }
        else
        {
            sprintf(comboText, "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s{~:c}", combos[6].comboText);
        }
        textWidget.set_text(comboText);
    }

    return xhud::delay_motive_update(w, m, dt);
}

bool zHud::ManlinessMeter::Combo7Effect(xhud::widget& w, xhud::motive& m, F32 dt)
{
    xhud::text_widget& textWidget = (xhud::text_widget&)w;
    char pos1[64];
    char pos2[64];
    char comboText[128];
    U32 len;

    if (ComboGlobals::Time[7] >= 0.9f)
    {
        ComboGlobals::Time[7] = 0.0f;
    }

    if (combos[7].comboText != NULL)
    {
        U32 pos = GetComboText(combos[7].comboText, ComboGlobals::Time[7], dt, pos1);

        if (pos != 0)
        {
            len = strlen(combos[7].comboText);
            if (pos <= len && pos != 0)
            {
                memset(pos2, 0, sizeof(pos2));
                if (pos > 1)
                {
                    pos1[pos - 2] = '\0';
                    memcpy(pos2, combos[7].comboText + pos, len - pos);
                    sprintf(comboText,
                            "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s{c=ff3333dd}%c{c=ffdd3333}%c{c="
                            "ffffe600}%s{~:c}",
                            pos1, combos[7].comboText[pos - 2], combos[7].comboText[pos - 1], pos2);
                }
                else
                {
                    pos1[pos - 1] = '\0';
                    memcpy(pos2, combos[7].comboText + pos, len - pos);
                    sprintf(comboText,
                            "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s{c=ff3333dd}%c{c=ffffe600}%s{~:c}",
                            pos1, combos[7].comboText[pos - 1], pos2);
                }
            }
            else
            {
                ComboGlobals::Time[7] = 0.0f;
                sprintf(comboText, "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s{~:c}",
                        combos[7].comboText);
            }
        }
        else
        {
            sprintf(comboText, "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s{~:c}", combos[7].comboText);
        }

        textWidget.set_text(comboText);
    }

    return xhud::delay_motive_update(w, m, dt);
}

bool zHud::ManlinessMeter::Combo8Effect(xhud::widget& w, xhud::motive& m, F32 dt)
{
    xhud::text_widget& textWidget = (xhud::text_widget&)w;
    char pos1[64];
    char pos2[64];
    char comboText[128];

    if (ComboGlobals::Time[8] >= 0.9f)
    {
        ComboGlobals::Time[8] = 0.0f;
    }

    if (combos[8].comboText != NULL)
    {
        U32 pos = GetComboText(combos[8].comboText, ComboGlobals::Time[8], dt, pos1, pos2);

        if (pos != 0)
        {
            pos1[pos - 1] = '\0';
            sprintf(
                comboText,
                "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s{font=0}{w*0.8}{h*0.8}{c=ffffe600}%c{font=0}"
                "{w*0.9}{h*0.9}{c=ffffe600}%s{~:c}",
                pos1, combos[8].comboText[pos - 1], pos2);
        }
        else
        {
            sprintf(comboText, "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s{~:c}", combos[8].comboText);
        }
        textWidget.set_text(comboText);
    }

    return xhud::delay_motive_update(w, m, dt);
}

bool zHud::ManlinessMeter::Combo9Effect(xhud::widget& w, xhud::motive& m, F32 dt)
{
    xhud::text_widget& textWidget = (xhud::text_widget&)w;
    char pos1[64];
    char pos2[64];
    // comboText[192] in the PS2 DWARF; retail's GC frame (0x140) only has room for 128.
    char comboText[128];

    if (ComboGlobals::Time[9] >= 0.9f)
    {
        ComboGlobals::Time[9] = 0.0f;
    }

    if (combos[9].comboText != NULL)
    {
        // not in the DWARF: the split position returned by GetComboText is used twice.
        S32 pos = GetComboText(combos[9].comboText, ComboGlobals::Time[9], dt, pos1, pos2);
        pos1[pos] = '\0';

        sprintf(comboText,
                "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s{font=0}{w*0.8}{h*0.8}{c=ff3333dd}%c{font=0}"
                "{w*0.9}{h*0.9}{c=ffffe600}%s{~:c}",
                pos1, combos[9].comboText[pos], pos2);
        textWidget.set_text(comboText);
    }

    return xhud::delay_motive_update(w, m, dt);
}

bool zHud::ManlinessMeter::Combo10Effect(xhud::widget& w, xhud::motive& m, F32 dt)
{
    xhud::text_widget& textWidget = (xhud::text_widget&)w;
    char comboText[128];
    F32 scale;

    if (ComboGlobals::Time[10] >= 0.9f)
    {
        ComboGlobals::Time[10] = 0.0f;
    }

    if (combos[10].comboText != NULL)
    {
        scale = 0.6f * isin(2.0f * ComboGlobals::Time[10]) + 0.4f;
        if (scale <= 0.0f)
        {
            scale = 0.01f;
        }

        ComboGlobals::Time[10] += dt;

        sprintf(comboText, "{font=0}{w*%4.2f}{h*%4.2f}{c=ffffe600}%s", scale + 0.01, scale + 0.01,
                combos[10].comboText);
        textWidget.set_text(comboText);
    }

    return xhud::delay_motive_update(w, m, dt);
}

bool zHud::ManlinessMeter::Combo11Effect(xhud::widget& w, xhud::motive& m, F32 dt)
{
    xhud::text_widget& textWidget = (xhud::text_widget&)w;
    char pos1[64];
    char pos2[64];
    char comboText[128];
    F32 scale;

    if (ComboGlobals::Time[11] >= 0.9f)
    {
        ComboGlobals::Time[11] = 0.0f;
    }

    if (combos[11].comboText != NULL)
    {
        // not in the DWARF: the split position returned by GetComboText is used twice.
        S32 pos = GetComboText(combos[11].comboText, ComboGlobals::Time[11], dt, pos1, pos2);
        pos1[pos] = '\0';

        scale = 0.5f * isin(2.0f * ComboGlobals::Time[11]) + 0.4f;
        if (scale <= 0.0f)
        {
            scale = 0.01f;
        }

        sprintf(
            comboText,
            "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s{font=0}{w*%4.2f}{h*%4.2f}{c=ff3333dd}%c{font=0}"
            "{w*0.9}{h*0.9}{c=ffffe600}%s{~:c}",
            pos1, scale + 0.01, scale + 0.01, combos[11].comboText[pos], pos2);
        textWidget.set_text(comboText);
    }

    return xhud::delay_motive_update(w, m, dt);
}

bool zHud::ManlinessMeter::Combo12Effect(xhud::widget& w, xhud::motive& m, F32 dt)
{
    xhud::text_widget& textWidget = (xhud::text_widget&)w;
    char pos1[64];
    char pos2[64];
    char pos3[64];
    char comboText[128];
    U32 len;

    if (ComboGlobals::Time[12] >= 0.9f)
    {
        ComboGlobals::Time[12] = 0.0f;
    }

    if (combos[12].comboText != NULL)
    {
        U32 pos = GetComboText(combos[12].comboText, ComboGlobals::Time[12], dt, pos1);

        if (pos != 0)
        {
            len = strlen(combos[12].comboText);
            if (pos <= len / 2)
            {
                pos1[pos - 1] = '\0';
                memset(pos2, 0, sizeof(pos2));
                memset(pos3, 0, sizeof(pos3));
                memcpy(pos2, combos[12].comboText + pos, len - pos * 2);
                memcpy(pos3, combos[12].comboText + len + 1 - pos, len - pos);
                sprintf(
                    comboText,
                    "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s{c=ff3333dd}%c{c=ffffe600}%s{c=ffdd3333}"
                    "%c{c=ffffe600}%s{~:c}",
                    pos1, combos[12].comboText[pos - 1], pos2, combos[12].comboText[len - pos],
                    pos3);
            }
            else
            {
                ComboGlobals::Time[12] = 0.0f;
                sprintf(comboText, "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s{~:c}",
                        combos[12].comboText);
            }
        }
        else
        {
            sprintf(comboText, "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s{~:c}", combos[12].comboText);
        }

        textWidget.set_text(comboText);
    }

    return xhud::delay_motive_update(w, m, dt);
}

bool zHud::ManlinessMeter::Combo13Effect(xhud::widget& w, xhud::motive& m, F32 dt)
{
    xhud::text_widget& textWidget = (xhud::text_widget&)w;
    char pos1[64];
    char comboText[128];
    S32 pos;
    S32 len;

    if (ComboGlobals::Time[13] >= 0.9f)
    {
        ComboGlobals::Time[13] = 0.0f;
    }

    if (combos[13].comboText != NULL)
    {
        pos = GetComboTextPos(combos[13].comboText, ComboGlobals::Time[13], dt);

        if (pos >= 0)
        {
            len = strlen(combos[13].comboText);
            strcpy(pos1, combos[13].comboText + len - 1 - pos);
            sprintf(comboText, "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s{~:c}", pos1);
        }

        textWidget.set_text(comboText);
    }

    return xhud::delay_motive_update(w, m, dt);
}

bool zHud::ManlinessMeter::Combo14Effect(xhud::widget& w, xhud::motive& m, F32 dt)
{
    sprintf(((xhud::text_widget&)w).text, "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s",
            combos[comboIndex].comboText);
    return xhud::delay_motive_update(w, m, dt);
}

bool zHud::ManlinessMeter::Combo15Effect(xhud::widget& w, xhud::motive& m, F32 dt)
{
    xhud::text_widget& textWidget = (xhud::text_widget&)w;
    char pos1[64];

    if (ComboGlobals::Time[15] >= 0.9f)
    {
        ComboGlobals::Time[15] = 0.0f;
    }

    if (combos[15].comboText != NULL)
    {
        U32 pos = GetComboText(combos[15].comboText, ComboGlobals::Time[15], dt, pos1);
        U32 len = strlen(combos[15].comboText);
        pos1[pos] = '\0';

        if (pos >= 1)
        {
            if (pos1[pos - 1] >= 'A' && pos1[pos - 1] <= 'z')
            {
                ComboGlobals::Combo15Count++;
                pos1[pos - 1] += (char)ComboGlobals::Combo15Count;
                if (pos1[pos - 1] > 'z')
                {
                    ComboGlobals::Combo15Count = 0;
                    pos1[pos - 1] = 'A';
                }

                if (combos[15].comboText[pos] == pos1[pos - 1])
                {
                    pos1[pos - 1] = combos[15].comboText[pos];
                    ComboGlobals::Combo15Count = 0;
                }
            }
            else
            {
                pos1[pos - 1] = combos[15].comboText[pos];
                ComboGlobals::Combo15Count = 0;
            }
        }

        sprintf(textWidget.text, "{font=0}{w*0.9}{h*0.9}{c=ffffe600}%s", pos1);
    }

    return xhud::delay_motive_update(w, m, dt);
}
