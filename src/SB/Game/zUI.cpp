#include "zUI.h"

#include "iMath.h"
#include "xEvent.h"
#include "xMemory.h"
#include "xPad.h"
#include "xTRC.h"
#include "xstransvc.h"
#include "zGlobals.h"
#include "zMenuSB.h"
#include "zScene.h"

U8 zUI::bAllowInput = 1;

namespace
{
    void* (*staticAllocator)(U32, void*);
    void* staticUser;
    void (*signalHandler)(zUI*, U32, U32, const F32*, xBase*, U32, void*);
    void* signalUser;

    // Defined in zUIMgr.cpp.
    extern U8 uiAnyActive;

    void UIEventHandler(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                        xBase* toParamWidget, U32 toParamWidgetID)
    {
        if (((zUI*)to)->custom && !((zUI*)to)->custom->HandleEvent(from, toEvent, toParam,
                                                                   toParamWidget, toParamWidgetID))
        {
            return;
        }

        ((zUI*)to)->DoHandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
    }
} // namespace

void zUI_Init(zUI* text, zUIAsset* asset)
{
    xBaseInit(text, asset);
    text->asset = asset;
    text->eventFunc = UIEventHandler;

    if (asset->custom)
    {
        zUICustom* custom;

        // The custom type IDs are xStrHash()es of the type names.
        switch (asset->custom)
        {
        case 0x4570177A: // "ExtrasManager"
            custom = new zUICustomExtrasManager(text);
            break;
        case 0x5CC577E4: // "ExtrasIcon"
            custom = new zUICustomExtrasIcon(text);
            break;
        case 0x889B7391: // "LoadingLevelText"
            custom = globals.InitializingLoadingScreen ? new zUICustomLoadingText(text) : NULL;
            break;
        case 0x268F0AB7: // "UpgradeTutorialControl"
            custom = new zUICustomUpgradeTutorialContoller(text);
            break;
        case 0x88AC5E79: // "DialogPlayer"
            custom = new zUICustomDialogPlayer(text);
            break;
        case 0x3A716D8E: // "PalRedirector"
            custom = new zUICustomPalRedirector(text);
            break;
        case 0x69D9260D: // "SlideshowImage"
            custom = new zUICustomImageSlideshow(text);
            break;
        case 0xF41C41E3: // "ConditionallyDisabled"
            custom = new zUICustomConditionallyDisabled(text);
            break;
        default:
            custom = NULL;
            break;
        }

        text->custom = custom;
    }

    text->Init();
    text->Reset();
}

U8 zUI::Blends() const
{
    return 1;
}

void zUI::DoInit()
{
    selectedMotion = asset->selectedMotion == 0 ?
                         NULL :
                         (zUIMotionAsset*)xSTFindAsset(asset->selectedMotion, NULL);
    unselectedMotion = asset->unselectedMotion == 0 ?
                           NULL :
                           (zUIMotionAsset*)xSTFindAsset(asset->unselectedMotion, NULL);
    hdrPass = 0;
    locked = 0;
}

void zUI::DoSetup()
{
}

void zUI::DoReset()
{
    DoResetMotion();
    if (custom)
    {
        custom->ResetMotion();
    }

    z = asset->z;
    visible = (asset->flags & 0x1) != 0;
    focus = (asset->flags & 0x2) != 0;
    selected = (asset->flags & 0x4) != 0;
    brighten = (asset->flags & 0x200) != 0;
}

void zUI::DoResetMotion()
{
    current.x = asset->x;
    current.y = asset->y;
    current.width = asset->width;
    current.height = asset->height;
    current.color = *(xColor_tag*)&asset->color;
    current.brightness = asset->brightness;
    motion = NULL;
    motionTime = 0.0f;
    motionFiredEvent = 0;
}

void zUI::DoHandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                        U32 toParamWidgetID)
{
    switch (toEvent)
    {
    case eEventEnable:
        xBaseEnable(this);
        break;
    case eEventDisable:
        xBaseDisable(this);
        break;
    case eEventUIFocusOn:
        FocusOn();
        break;
    case eEventUIFocusOn_Select:
        FocusOn();
        Select();
        Signal(eEventUIFocusOn);
        Signal(eEventUISelect);
        break;
    case eEventUIFocusOff:
        FocusOff();
        break;
    case eEventUIFocusOff_Unselect:
        FocusOff();
        Unselect();
        Signal(eEventUIFocusOff);
        Signal(eEventUIUnselect);
        break;
    case eEventUIVisible_FocusOn_Select:
        Visible();
        FocusOn();
        Select();
        Signal(eEventVisible);
        Signal(eEventUIFocusOn);
        Signal(eEventUISelect);
        break;
    case eEventUIFocusOff_Unselect_Invisible:
        FocusOff();
        Unselect();
        Invisible();
        Signal(eEventUIFocusOff);
        Signal(eEventUIUnselect);
        Signal(eEventInvisible);
        break;
    case eEventVisible:
        Visible();
        break;
    case eEventInvisible:
        Invisible();
        break;
    case eEventUIFlipVisibility:
        if (visible)
        {
            Invisible();
            Signal(eEventInvisible);
        }
        else
        {
            Visible();
            Signal(eEventVisible);
        }
        break;
    case eEventUIReset:
        Reset();
        break;
    case eEventUIResetMotion:
        ResetMotion();
        break;
    case eEventUISelect:
        Select();
        break;
    case eEventUIUnselect:
        Unselect();
        break;
    case eEventUISetMotion:
        if (toParamWidget)
        {
            RunMotion((zUIMotionAsset*)toParamWidget, toParam && toParam[0] != 0.0f,
                      toParam && toParam[1] != 0.0f);
        }
        else
        {
            if (motion)
            {
                motionTime = motion->totalTime;
                ApplyMotion();
            }

            motionTime = 0.0f;
            motion = NULL;
        }
        break;
    case eEventUIBrighten:
        brighten = 1;
        break;
    case eEventUIUnbrighten:
        brighten = 0;
        break;
    }
}

void zUI::DoUpdate(F32 dt, U32 buttonsPressed)
{
    if (bAllowInput)
    {
        if (lastFocus || (asset->flags & 0x20))
        {
            _tagxPad* pad = globals.pad0;

            if (gTrcPad[0].state == TRC_PadInserted)
            {
                xSTGetLocalizationEnum();

                if (buttonsPressed & XPAD_BUTTON_START)
                {
                    Signal(eEventPadPressStart);
                    xTRCClearPadMsg();
                }
                else if (buttonsPressed & XPAD_BUTTON_SELECT)
                {
                    if (!globals.player.g.CheatFlyToggle)
                    {
                        Signal(eEventPadPressSelect);
                    }
                    else if (!globals.player.g.FriendlySelectMode &&
                             !(buttonsPressed & XPAD_BUTTON_L1) && !(pad->on & XPAD_BUTTON_L1))
                    {
                        Signal(eEventPadPressSelect);
                    }
                    else if (globals.player.g.FriendlySelectMode &&
                             ((buttonsPressed & XPAD_BUTTON_L1) || (pad->on & XPAD_BUTTON_L1)))
                    {
                        Signal(eEventPadPressSelect);
                    }
                }
                else if (buttonsPressed & XPAD_BUTTON_Z)
                {
                    if (!globals.player.g.CheatFlyToggle)
                    {
                        Signal(eEventPadPressE);
                    }
                    else if (!globals.player.g.FriendlySelectMode &&
                             !(buttonsPressed & XPAD_BUTTON_L2) && !(pad->on & XPAD_BUTTON_L2))
                    {
                        Signal(eEventPadPressE);
                    }
                    else if (globals.player.g.FriendlySelectMode &&
                             ((buttonsPressed & XPAD_BUTTON_L2) || (pad->on & XPAD_BUTTON_L2)))
                    {
                        Signal(eEventPadPressE);
                    }
                }
                else if (buttonsPressed & XPAD_BUTTON_X)
                {
                    Signal(eEventPadPressX);
                }
                else if (buttonsPressed & XPAD_BUTTON_O)
                {
                    Signal(eEventPadPressO);
                }
                else if (buttonsPressed & XPAD_BUTTON_TRIANGLE)
                {
                    Signal(eEventPadPressTriangle);
                }
                else if (buttonsPressed & XPAD_BUTTON_SQUARE)
                {
                    Signal(eEventPadPressSquare);
                }
                else if (buttonsPressed & XPAD_BUTTON_L1)
                {
                    Signal(eEventPadPressL1);
                }
                else if (buttonsPressed & XPAD_BUTTON_L2)
                {
                    Signal(eEventPadPressL2);
                }
                else if (buttonsPressed & XPAD_BUTTON_R1)
                {
                    Signal(eEventPadPressR1);
                }
                else if (buttonsPressed & XPAD_BUTTON_R2)
                {
                    Signal(eEventPadPressR2);
                }
                else if (buttonsPressed & XPAD_BUTTON_UP)
                {
                    Signal(eEventPadPressUp);
                }
                else if (buttonsPressed & XPAD_BUTTON_DOWN)
                {
                    Signal(eEventPadPressDown);
                }
                else if (buttonsPressed & XPAD_BUTTON_LEFT)
                {
                    Signal(eEventPadPressLeft);
                }
                else if (buttonsPressed & XPAD_BUTTON_RIGHT)
                {
                    Signal(eEventPadPressRight);
                }
                else if (asset->flags & 0x400)
                {
                    // Unreachable in retail: every direction button was already handled above.
                    if (buttonsPressed & XPAD_BUTTON_UP)
                    {
                        AutoMenu(&zUIAsset::autoMenuUp, eEventUIAutoMenuRunUp);
                    }
                    else if (buttonsPressed & XPAD_BUTTON_DOWN)
                    {
                        AutoMenu(&zUIAsset::autoMenuDown, eEventUIAutoMenuRunDown);
                    }

                    if (buttonsPressed & XPAD_BUTTON_LEFT)
                    {
                        AutoMenu(&zUIAsset::autoMenuLeft, eEventUIAutoMenuRunLeft);
                    }
                    else if (buttonsPressed & XPAD_BUTTON_RIGHT)
                    {
                        AutoMenu(&zUIAsset::autoMenuRight, eEventUIAutoMenuRunRight);
                    }
                }
            }
        }

        if (visible && motion)
        {
            motionTime += dt;
            ApplyMotion();
        }
    }
}

void zUI::DoExit()
{
}

void zUI::RunMotion(zUIMotionAsset* asset, bool loop, bool reset)
{
    if (reset)
    {
        DoResetMotion();
        if (custom)
        {
            custom->ResetMotion();
        }
    }
    else if (motion)
    {
        motionLoop = 0;
        motionTime = motion->totalTime;
        ApplyMotion();
    }

    if (custom)
    {
        custom->PreInitMotion();
    }

    DoInitMotion();

    if (custom)
    {
        custom->PostInitMotion();
    }

    motionTime = 0.0f;
    motion = asset;
    motionFiredEvent = 0;
    motionLoop = loop;
    ApplyMotion();
}

// No GC symbol: auto-inlined into ApplyMotion (its only caller) and dead-stripped.
// ApplyMotion only calls it while motionTime < endTime, so curDistance is always set.
F32 zUI::CalcAcceleratedDistance(F32 totalDistance, F32 startTime, F32 endTime, F32 accelTime,
                                 F32 decelTime)
{
    F32 timePassed = motionTime - startTime;
    F32 timeLength = endTime - startTime;
    F32 topVelocity = totalDistance / (timeLength - 0.5f * (accelTime + decelTime));
    F32 curDistance;

    if (motionTime < startTime + accelTime)
    {
        curDistance = timePassed * (0.5f * topVelocity * timePassed) / accelTime;
    }
    else if (motionTime <= endTime - decelTime)
    {
        curDistance = topVelocity * (timePassed - 0.5f * accelTime);
    }
    else if (motionTime <= endTime)
    {
        F32 timeToEnd = timeLength - timePassed;
        curDistance = totalDistance - timeToEnd * (0.5f * topVelocity * timeToEnd) / decelTime;
    }

    return curDistance;
}

void zUI::ApplyMotion()
{
    zUIMotionFrame frame;
    xMemorySetV32A32(&frame, 0, sizeof(zUIMotionFrame));
    frame.scaleX = 1.0f;
    frame.scaleY = 1.0f;
    frame.centerScaleX = 1.0f;
    frame.centerScaleY = 1.0f;
    frame.textScaleX = 1.0f;
    frame.textScaleY = 1.0f;
    frame.color = current.color;
    frame.brightness = current.brightness;

    F32 timeCompensation = 0.0f;
    bool fireEndEvent = false;

    if (motionTime > motion->totalTime)
    {
        if (motionLoop)
        {
            timeCompensation = motionTime - motion->totalTime;
            if (timeCompensation > motion->totalTime)
            {
                timeCompensation -= motion->totalTime * (S32)(timeCompensation / motion->totalTime);
            }
        }

        motionTime = motion->totalTime;
        fireEndEvent = true;
    }

    const char* entity = (const char*)(motion + 1);
    for (S32 i = 0; i < motion->cmdCount; i++)
    {
        S32 size = 0;

        switch (((const zUIMotionCmdAsset*)entity)->type)
        {
        case 0:
        {
            const zUIMotionCmdMove* cmd = (const zUIMotionCmdMove*)entity;
            size = sizeof(zUIMotionCmdMove);

            if (cmd->enabled && motionTime >= cmd->startTime)
            {
                if (motionTime >= cmd->endTime)
                {
                    frame.offsetX += cmd->distX;
                    frame.offsetY += cmd->distY;
                }
                else
                {
                    frame.offsetX += CalcAcceleratedDistance(
                        cmd->distX, cmd->startTime, cmd->endTime, cmd->accelTime, cmd->decelTime);
                    frame.offsetY += CalcAcceleratedDistance(
                        cmd->distY, cmd->startTime, cmd->endTime, cmd->accelTime, cmd->decelTime);
                }
            }
            break;
        }
        case 1:
        {
            const zUIMotionCmdScale* cmd = (const zUIMotionCmdScale*)entity;
            size = sizeof(zUIMotionCmdScale);

            if (cmd->enabled && motionTime >= cmd->startTime)
            {
                F32 scaleX = 1.0f;
                F32 scaleY = 1.0f;

                if (motionTime >= cmd->endTime)
                {
                    scaleX *= cmd->amountX;
                    scaleY *= cmd->amountY;
                }
                else
                {
                    scaleX *= 1.0f + CalcAcceleratedDistance(cmd->amountX - 1.0f, cmd->startTime,
                                                             cmd->endTime, cmd->accelTime,
                                                             cmd->decelTime);
                    scaleY *= 1.0f + CalcAcceleratedDistance(cmd->amountY - 1.0f, cmd->startTime,
                                                             cmd->endTime, cmd->accelTime,
                                                             cmd->decelTime);
                }

                if (cmd->centerPivot)
                {
                    if (cmd->centerOffsetX || cmd->centerOffsetY)
                    {
                        frame.offsetX += (scaleX - 1.0f) * cmd->centerOffsetX;
                        frame.offsetY += (scaleY - 1.0f) * cmd->centerOffsetY;
                    }

                    frame.centerScaleX = scaleX;
                    frame.centerScaleY = scaleY;
                }
                else
                {
                    frame.scaleX = scaleX;
                    frame.scaleY = scaleY;
                }
            }
            break;
        }
        case 2:
        {
            const zUIMotionCmdRotate* cmd = (const zUIMotionCmdRotate*)entity;
            size = sizeof(zUIMotionCmdRotate);

            if (cmd->enabled && motionTime >= cmd->startTime)
            {
                if (motionTime >= cmd->endTime)
                {
                    frame.rotation += cmd->rotation;
                }
                else
                {
                    frame.rotation +=
                        CalcAcceleratedDistance(cmd->rotation, cmd->startTime, cmd->endTime,
                                                cmd->accelTime, cmd->decelTime);
                }

                if (frame.rotation && (cmd->centerOffsetX || cmd->centerOffsetY))
                {
                    F32 rotation = frame.rotation;
                    if (rotation > 0.0f)
                    {
                        rotation = rotation - (S32)rotation / 360 * 360;
                    }
                    else
                    {
                        rotation = rotation + (360.0f - (S32)rotation / 360 * 360);
                    }

                    rotation = (PI / 180.0f) * (360.0f - rotation);

                    F32 sinA = isin(rotation);
                    F32 cosA = icos(rotation);

                    frame.offsetX +=
                        cmd->centerOffsetX * cosA + cmd->centerOffsetY * sinA - cmd->centerOffsetX;
                    frame.offsetY +=
                        -cmd->centerOffsetX * sinA + cmd->centerOffsetY * cosA - cmd->centerOffsetY;
                }
            }
            break;
        }
        case 3:
        {
            const zUIMotionCmdOpacity* cmd = (const zUIMotionCmdOpacity*)entity;
            size = sizeof(zUIMotionCmdOpacity);

            if (cmd->enabled && motionTime >= cmd->startTime)
            {
                if (motionTime >= cmd->endTime)
                {
                    frame.color.a = cmd->endOpacity;
                }
                else
                {
                    F32 opacityChange =
                        CalcAcceleratedDistance(cmd->endOpacity - cmd->startOpacity, cmd->startTime,
                                                cmd->endTime, cmd->accelTime, cmd->decelTime);
                    frame.color.a = cmd->startOpacity + opacityChange;
                }
            }
            break;
        }
        case 4:
        {
            const zUIMotionCmdAbsoluteScale* cmd = (const zUIMotionCmdAbsoluteScale*)entity;
            size = sizeof(zUIMotionCmdAbsoluteScale);

            if (cmd->enabled && motionTime >= cmd->startTime)
            {
                F32 scaleX;
                F32 scaleY;

                if (motionTime >= cmd->endTime)
                {
                    scaleX = cmd->endX;
                    scaleY = cmd->endY;
                }
                else
                {
                    F32 percent = (motionTime - cmd->startTime) / (cmd->endTime - cmd->startTime);
                    scaleX = cmd->startX + percent * (cmd->endX - cmd->startX);
                    scaleY = cmd->startY + percent * (cmd->endY - cmd->startY);
                }

                if (cmd->textScale)
                {
                    frame.textScaleX = scaleX;
                    frame.textScaleY = scaleY;
                }
                else if (cmd->centerPivot)
                {
                    frame.centerScaleX = scaleX;
                    frame.centerScaleY = scaleY;
                }
                else
                {
                    frame.scaleX = scaleX;
                    frame.scaleY = scaleY;
                }
            }
            break;
        }
        case 5:
        {
            const zUIMotionCmdBrightness* cmd = (const zUIMotionCmdBrightness*)entity;
            size = sizeof(zUIMotionCmdBrightness);

            if (cmd->enabled && motionTime >= cmd->startTime)
            {
                if (motionTime >= cmd->endTime)
                {
                    frame.brightness = cmd->endBrightness;
                }
                else
                {
                    F32 brightnessChange =
                        CalcAcceleratedDistance(cmd->endBrightness - cmd->startBrightness,
                                                cmd->startTime, cmd->endTime, cmd->accelTime,
                                                cmd->decelTime);
                    frame.brightness = cmd->startBrightness + brightnessChange;
                }
            }
            break;
        }
        case 6:
        {
            const zUIMotionCmdColor* cmd = (const zUIMotionCmdColor*)entity;
            size = sizeof(zUIMotionCmdColor);

            if (cmd->enabled && motionTime >= cmd->startTime)
            {
                if (motionTime >= cmd->endTime)
                {
                    frame.color.r = cmd->endRed;
                    frame.color.g = cmd->endGreen;
                    frame.color.b = cmd->endBlue;
                }
                else
                {
                    F32 redChange =
                        CalcAcceleratedDistance(cmd->endRed - cmd->startRed, cmd->startTime,
                                                cmd->endTime, cmd->accelTime, cmd->decelTime);
                    F32 greenChange =
                        CalcAcceleratedDistance(cmd->endGreen - cmd->startGreen, cmd->startTime,
                                                cmd->endTime, cmd->accelTime, cmd->decelTime);
                    F32 blueChange =
                        CalcAcceleratedDistance(cmd->endBlue - cmd->startBlue, cmd->startTime,
                                                cmd->endTime, cmd->accelTime, cmd->decelTime);
                    frame.color.r = cmd->startRed + redChange;
                    frame.color.g = cmd->startGreen + greenChange;
                    frame.color.b = cmd->startBlue + blueChange;
                }
            }
            break;
        }
        case 7:
        {
            const zUIMotionCmdUVScroll* cmd = (const zUIMotionCmdUVScroll*)entity;
            size = sizeof(zUIMotionCmdUVScroll);

            if (cmd->enabled && motionTime >= cmd->startTime)
            {
                if (motionTime >= cmd->endTime)
                {
                    frame.offsetU = cmd->amountU;
                    frame.offsetV = cmd->amountV;
                }
                else
                {
                    frame.offsetU = CalcAcceleratedDistance(
                        cmd->amountU, cmd->startTime, cmd->endTime, cmd->accelTime, cmd->decelTime);
                    frame.offsetV = CalcAcceleratedDistance(
                        cmd->amountV, cmd->startTime, cmd->endTime, cmd->accelTime, cmd->decelTime);
                }
            }
            break;
        }
        }

        entity += size;
    }

    if (custom)
    {
        // The target works on a copy of the frame here (unnamed in both DWARFs).
        zUIMotionFrame customFrame = frame;
        custom->PreApplyMotionFrame(&customFrame);
        DoApplyMotionFrame(&customFrame);
        custom->PostApplyMotionFrame(&customFrame);
    }
    else
    {
        DoApplyMotionFrame(&frame);
    }

    if (fireEndEvent)
    {
        if (!motionFiredEvent && !motionLoop)
        {
            motionFiredEvent = 1;

            bool in = motion->in;
            xBase uiMotionBase;
            uiMotionBase.id = motion->id;
            uiMotionBase.baseType = eBaseTypeUIMotion;
            uiMotionBase.linkCount = 0;
            uiMotionBase.baseFlags = 0;
            uiMotionBase.link = NULL;

            if (motion != selectedMotion && motion != unselectedMotion && current.color.a &&
                visible)
            {
                if (selected && selectedMotion)
                {
                    Select();
                }
                else if (unselectedMotion)
                {
                    Unselect();
                }
                else
                {
                    motion = NULL;
                }
            }
            else
            {
                motion = NULL;
            }

            Signal(&uiMotionBase, eEventUIMotionFinished);
            Signal(&uiMotionBase, in ? eEventUIMotionFinishedIn : eEventUIMotionFinishedOut);
            return;
        }

        if (motionLoop)
        {
            zUIMotionAsset* oldMotion = motion;
            Signal(eEventUIMotionLoop);
            if (oldMotion != motion)
            {
                timeCompensation = 0.0f;
            }
        }
    }

    if (motion && timeCompensation > 0.0f)
    {
        motionTime = motion->loopTime + timeCompensation;
        if (motionTime > motion->totalTime)
        {
            motionTime = 0.0f;
        }

        ApplyMotion();
    }
}

void zUI::DoInitMotion()
{
    startMovement = current;
}

void zUI::DoApplyMotionFrame(const zUIMotionFrame* frame)
{
    current.x = startMovement.x + frame->offsetX;
    current.y = startMovement.y + frame->offsetY;
    current.width = startMovement.width * frame->scaleX;
    current.height = startMovement.height * frame->scaleY;

    current.x -= (frame->centerScaleX - 1.0f) * current.width / 2.0f;
    current.width *= frame->centerScaleX;
    current.y -= (frame->centerScaleY - 1.0f) * current.height / 2.0f;
    current.height *= frame->centerScaleY;

    current.color = frame->color;
    current.brightness = frame->brightness;
}

void zUI::AutoMenu(U32 zUIAsset::* item, U32 event)
{
    zUI* ui;
    U32 id = asset->*item;
    U8 bAllowAutoMenu;

    while (true)
    {
        if (!id)
        {
            return;
        }

        ui = (zUI*)zSceneFindObject(id);
        if (!ui)
        {
            return;
        }

        bAllowAutoMenu = ui->visible;
        if (ui->custom)
        {
            bAllowAutoMenu &= ui->custom->PreAutoMenu();
        }

        if (bAllowAutoMenu)
        {
            break;
        }

        id = ui->asset->*item;
    }

    if (ui && ui->locked)
    {
        switch (event)
        {
        case eEventUIAutoMenuRunUp:
            ui->AutoMenu(&zUIAsset::autoMenuUp, eEventUIAutoMenuRunUp);
            break;
        case eEventUIAutoMenuRunDown:
            ui->AutoMenu(&zUIAsset::autoMenuDown, eEventUIAutoMenuRunDown);
            break;
        }

        Signal(eEventUIAutoMenuRun);
        Signal(event);

        HandleEvent(this, eEventUIFocusOff_Unselect, NULL, NULL, 0);
        Signal(eEventUIFocusOff_Unselect);
    }
    else
    {
        Signal(eEventUIAutoMenuRun);
        Signal(event);

        HandleEvent(this, eEventUIFocusOff_Unselect, NULL, NULL, 0);
        Signal(eEventUIFocusOff_Unselect);

        ui->HandleEvent(this, eEventUIFocusOn_Select, NULL, NULL, 0);
        ui->Signal(this, eEventUIFocusOn_Select);
    }
}

void zUI::FocusOn()
{
    focus = 1;

    if (asset->flags & 0x100)
    {
        Visible();
        Signal(eEventVisible);
    }
}

void zUI::FocusOff()
{
    focus = 0;

    if (asset->flags & 0x100)
    {
        Invisible();
        Signal(eEventInvisible);
    }
}

void zUI::Visible()
{
    uiAnyActive = 1;
    visible = 1;

    if (asset->flags & 0x8)
    {
        focus = 1;
    }
}

void zUI::Invisible()
{
    visible = 0;

    if (asset->flags & 0x10)
    {
        focus = 0;
    }
}

void zUI::Select()
{
    selected = 1;

    if (asset->flags & 0x40)
    {
        if (selectedMotion)
        {
            RunMotion(selectedMotion, asset->flags & 0x2000, asset->flags & 0x800);
        }
    }
    else if (asset->flags & 0x80)
    {
        if (motion)
        {
            motionTime = motion->totalTime;
            ApplyMotion();
        }

        motionTime = 0.0f;
        motion = NULL;
    }
}

void zUI::Unselect()
{
    selected = 0;

    if (asset->flags & 0x80)
    {
        if (unselectedMotion)
        {
            RunMotion(unselectedMotion, asset->flags & 0x4000, asset->flags & 0x1000);
        }
    }
    else if (asset->flags & 0x40)
    {
        if (motion)
        {
            motionTime = motion->totalTime;
            ApplyMotion();
        }

        motionTime = 0.0f;
        motion = NULL;
    }
}
