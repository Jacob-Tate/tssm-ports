// The Dolphin SDK's VIWaitForRetrace on PC: implemented, as the host's 60 Hz
// frame wait.
//
// Caller: zAsync.cpp's loading thread, which spins on it while a disc error
// holds the load. The console waits for the next vertical retrace; iVSync
// (pc/iSystem.cpp) is the port's equivalent, a sleep to the next 1/60 s tick.

#include <dolphin/vi.h>

#include "iSystem.h"

extern "C" void VIWaitForRetrace(void)
{
    iVSync();
}
