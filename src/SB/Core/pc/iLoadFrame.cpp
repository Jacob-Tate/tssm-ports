// The loading screen's seam (iLoadFrame.h). The frames themselves are drawn by
// iAsync.cpp. Port-only.

#include "iLoadFrame.h"

iLoadPumpFn iLoadFramePumpFn;

namespace
{
    S32 sDrawing;
    iLoadFrameShowFn sShowHook;
} // namespace

void iLoadFrameSetDrawing(S32 on)
{
    sDrawing = on;
}

S32 iLoadFrameDrawing()
{
    return sDrawing;
}

void iLoadFrameSetShowHook(iLoadFrameShowFn fn)
{
    sShowHook = fn;
}

void iLoadFrameShow()
{
    if (sShowHook != NULL)
    {
        sShowHook();
    }
}
