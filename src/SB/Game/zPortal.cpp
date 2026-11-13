#include "zPortal.h"

#include "zGlobals.h"
#include "zScene.h"
#include "xEvent.h"
#include "zAutoTweak.h"
#ifdef PLATFORM_PC
#include "iCoop.h"
#endif

void zPortalInit(void* portal, void* passet)
{
    zPortalInit((_zPortal*)portal, (xPortalAsset*)passet);
}

void zPortalInit(_zPortal* portal, xPortalAsset* passet)
{
    xBaseInit(portal, passet);

    portal->passet = passet;
    portal->eventFunc = zPortalEventCB;

    if (portal->linkCount != 0)
    {
        portal->link = (xLinkAsset*)(portal->passet + 1);
    }
}

void zPortalEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam, xBase* b3, U32)
{
    switch (toEvent)
    {
    case eEventReset:
        xBaseReset(to, ((_zPortal*)to)->passet);
        break;
    case eEventTeleportPlayer:
#ifdef PLATFORM_PC
        // Online co-op (docs/MULTIPLAYER.md 7.2): a guest in the host's level
        // does not leave it by a portal; the host leads. FALSE at once when
        // co-op is off.
        if (iCoopRefusePortal(((_zPortal*)to)->passet->sceneID))
        {
            break;
        }
#endif
        if (globals.player.s->Health != 0)
        {
            if (globals.player.ControlOff & CONTROL_OWNER_BUS_STOP)
            {
                globals.player.ControlOff &= ~CONTROL_OWNER_BUS_STOP;
            }
            zSceneSwitch((_zPortal*)to, 0);
        }
        break;
    }
}
