// PC shadow of the Dolphin SDK's <dolphin/vi.h>: VIWaitForRetrace, which
// zAsync.cpp calls while a disc error holds the loading thread. On PC it waits
// for the next 60 Hz frame (src/SB/Core/pc/stubs/dolphin_vi.cpp). See
// ../dolphin.h.
#pragma once

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void VIWaitForRetrace(void);

#ifdef __cplusplus
}
#endif
