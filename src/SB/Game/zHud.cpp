#include "zHud.h"

#include "iCamera.h"
#include "xString.h"
#include "xTRC.h"
#include "xstransvc.h"
#include "zCamera.h"
#include "zCutsceneMgr.h"
#include "zGame.h"
#include "zGameState.h"
#include "zGlobals.h"

RwCamera* zHud::HudCamera;
xLightKit* zHud::HudLightKit;

namespace
{
    void EventHandlerFunc(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                          xBase* toParamWidget, U32 toParamWidgetID)
    {
        ((zHud::hud_element*)to)
            ->handle_event(from, toEvent, toParam, toParamWidget, toParamWidgetID);
    }
} // namespace

void zHud::hud_element::init()
{
    eventFunc = EventHandlerFunc;
    xBaseEnable(this);
}

void zHud::hud_system::startup()
{
    HudCamera = iCameraRwCreate();
    RwCameraSetFrame(HudCamera, RwFrameCreate());
    RwV2d vw = { 1.0f, 1.0f };
    RwCameraSetViewWindow(HudCamera, &vw);
    RwCameraSetProjection(HudCamera, rwPARALLEL);
    RwCameraSetNearClipPlane(HudCamera, 0.3f);
    RwCameraSetFarClipPlane(HudCamera, 10000.0f);
}

void zHud::hud_system::shutdown()
{
    RwFrame* tmpframe;

    if (HudCamera != NULL)
    {
        tmpframe = RwCameraGetFrame(HudCamera);
        if (tmpframe != NULL)
        {
            RwCameraSetFrame(HudCamera, NULL);
            _rwFrameSyncDirty();
            RwFrameDestroy(tmpframe);
        }

        RwCameraDestroy(HudCamera);
        HudCamera = NULL;
    }
}

void zHud::hud_system::init()
{
    hud_element* element;

    last_paused = false;
    xhud::init();
    HudLightKit = (xLightKit*)xSTFindAsset(xStrHash("hud_lightkit"), NULL);

    for (element = elements; element != NULL; element = element->_next)
    {
        element->init();
    }
}

void zHud::hud_system::update(F32 dt)
{
    hud_element* element;

    if (globals.sceneCur != NULL && globals.sceneCur->sceneID == 'B402')
    {
        hide();
        return;
    }

    if (globals.cmgr != NULL && globals.cmgr->csn->Ready)
    {
        hide();
    }

    if (globals.pad0->pressed & XPAD_BUTTON_L1 && !globals.player.ControlOff &&
        gTrcPad[0].state != TRC_PadMissing)
    {
        show(true);
    }

    if (gGameMode == eGameMode_Stall)
    {
        hide();
    }
    else if (!last_paused && zGameIsPaused())
    {
        show(true);
        last_paused = true;
    }
    else if (last_paused && !zGameIsPaused())
    {
        hide();
        last_paused = false;
    }

    for (element = elements; element != NULL; element = element->_next)
    {
        element->update(dt);
    }

    xhud::update(dt);
}

void zHud::hud_system::render()
{
    if (globals.cmgr != NULL || zCameraIsFlyCamRunning())
    {
        return;
    }

    xhud::render();
}

void zHud::hud_system::show(bool autohide)
{
    hud_element* element;

    for (element = elements; element != NULL; element = element->_next)
    {
        element->show(autohide);
    }
}

void zHud::hud_system::hide()
{
    hud_element* element;

    for (element = elements; element != NULL; element = element->_next)
    {
        element->hide();
    }
}

void zHud::hud_system::add(hud_element* element)
{
    element->setup();
    element->_next = elements;
    elements = element;
}

void zHud::hud_system::destroy()
{
    hud_element* element;

    for (element = elements; element != NULL; element = element->_next)
    {
        element->destroy();
    }

    elements = NULL;
    xhud::destroy();
}

zHud::hud_system& zHud::hud_system::instance()
{
    static hud_system _instance;
    return _instance;
}
