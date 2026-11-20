#include "iMouseLook.h"
#include "iPadHost.h"
#include "iPadPrompts.h"
#include "iPadTest.h"

#include <rwcore.h>

#include <string.h>

// The backend that is always available: no controllers, ever.
//
// This is not a placeholder for a missing implementation -- it is what the
// port does when it is built without any input library, and it is the correct
// answer in that configuration. iPadUpdate reports the pad as missing, the
// game shows its "please reconnect" screen, and nothing reads uninitialised
// state. A real backend replaces this file in the link, not around it.
//
// TSSM-ADAPTED: it also stands in for the SDL-only input pieces iPad.cpp calls
// (the prompts and the input test), which a build without SDL does not have.

static iPadHostState sState[IPAD_MAX_CONTROLLERS];

void iPadHostInit()
{
    memset(sState, 0, sizeof(sState));
}

void iPadHostExit()
{
}

void iPadHostPoll()
{
}

void iPadHostPollDevices()
{
}

const iPadHostState* iPadHostGet(S32 port)
{
    if (port < 0 || port >= IPAD_MAX_CONTROLLERS)
    {
        return NULL;
    }

    return &sState[port];
}

void iPadHostRumble(S32 port, S32 on)
{
}

const char* iPadHostName()
{
    return "null (no input backend)";
}

const char* iPadHostDeviceName(S32)
{
    return NULL;
}

const char* iPadHostBoundInput(U32)
{
    return NULL;
}

const char* iPadHostPadKind()
{
    return NULL;
}

const char* iPadHostInputForLabel(char)
{
    return NULL;
}

void iPadHostReloadBindings()
{
}

const iPadBind* iPadHostPadBinds()
{
    return NULL;
}

U32 iPadHostBindGeneration()
{
    return 0;
}

void iPadHostScript(S32, U32)
{
}

S32 iPadHostCaptureKey(S32, char*, S32)
{
    return FALSE;
}

S32 iPadHostCaptureButton(S32, char*, S32)
{
    return FALSE;
}

void iPadHostSetHotkey(iPadHostHotkeyFn)
{
}

void iPadPromptsUpdate(const iPadHostState*)
{
}

// No mouse either (iMouseLook.h): no pointer in the menus, nothing steering.
bool iMouseLookSetPointer(bool)
{
    return false;
}

bool iMouseLookPointer(F32*, F32*, bool*, U32*, U32*, S32*)
{
    return false;
}

void iMouseLookSetHand(bool)
{
}

U32 iMouseLookBound(S32)
{
    return 0;
}

bool iMouseLookSteering()
{
    return false;
}

void iMouseLookTestMove(F32, F32)
{
}

void iMouseLookTestPointer(F32, F32)
{
}

void iMouseLookTestPress(U32)
{
}

void iMouseLookTestWheel(S32)
{
}

void iMouseLookTestEscape()
{
}

void iMouseLookSpend(U32)
{
}

U32 iMouseLookSpentOut(U32 on)
{
    return on;
}

void iMouseLookPump()
{
}

bool iMouseLookHeld()
{
    return false;
}

void iPadPromptsShutdown()
{
}

RwRaster* iPadPromptsDiscRaster(RwTexture* texture)
{
    return texture != NULL ? texture->raster : NULL;
}

void iPadTestStart()
{
}

void iPadTestFrame(const _tagxPad*, U32, const iPadHostState*)
{
}
