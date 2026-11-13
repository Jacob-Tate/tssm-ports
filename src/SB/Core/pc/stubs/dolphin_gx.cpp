// The Dolphin SDK's GX calls that shared code still makes, on PC.
//
// Caller: zAsync.cpp, which waits for the GPU with GXDrawDone before it lets
// the load-screen thread draw. librw draws synchronously from the game's point
// of view, so there is nothing to wait for and this is correct as a no-op.
//
// The two units that drove GX by hand, xFXHighDynamicRange.cpp and
// xScreenWarp.cpp, call the port's render passes on PLATFORM_PC (iHDRGlow.h,
// iScreenWarp.h), so the rest of the GX stubs went with them.
//
// Listed in stubs/STUBS.md (tools/pcstubs.py).

#include <dolphin/gx.h>

extern "C" {

void GXDrawDone(void)
{
}

} // extern "C"
