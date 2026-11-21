// See iCameraLook.h.

#include "iCameraLook.h"

namespace
{
    S32 sActive[iCAMERALOOK_SOURCE_COUNT];
    F32 sYaw[iCAMERALOOK_SOURCE_COUNT];
    F32 sPitch[iCAMERALOOK_SOURCE_COUNT];
    S32 sTaken;
    S32 sTakenLast;
} // namespace

void iCameraLookSet(iCameraLookSource source, F32 yaw, F32 pitch)
{
    sYaw[source] = sActive[source] ? yaw : 0.0f;
    sPitch[source] = sActive[source] ? pitch : 0.0f;
}

void iCameraLookSetActive(iCameraLookSource source, S32 on)
{
    sActive[source] = on ? 1 : 0;
    if (!sActive[source])
    {
        sYaw[source] = 0.0f;
        sPitch[source] = 0.0f;
    }
}

S32 iCameraLookActive()
{
    for (S32 i = 0; i < iCAMERALOOK_SOURCE_COUNT; i++)
    {
        if (sActive[i])
        {
            return 1;
        }
    }
    return 0;
}

S32 iCameraLookTake(F32* yaw, F32* pitch)
{
    F32 y = 0.0f;
    F32 p = 0.0f;
    for (S32 i = 0; i < iCAMERALOOK_SOURCE_COUNT; i++)
    {
        y += sYaw[i];
        p += sPitch[i];
        sYaw[i] = 0.0f;
        sPitch[i] = 0.0f;
    }
    *yaw = y;
    *pitch = p;
    sTaken = 1;
    return iCameraLookActive();
}

void iCameraLookPoll()
{
    sTakenLast = sTaken;
    sTaken = 0;
}

S32 iCameraLookTaken()
{
    return sTakenLast;
}
