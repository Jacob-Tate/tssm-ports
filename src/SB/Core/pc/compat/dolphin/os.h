// PC shadow of the Dolphin SDK's <dolphin/os.h>: the two OS calls shared code
// still makes, zGame.cpp's per-frame OSCheckActiveThreads and zMain.cpp's
// OSResetSystem after a memory-card format. Definitions in
// src/SB/Core/pc/stubs/dolphin_os.cpp. OS_TIMER_CLOCK, which the SDK defines
// here, is in pc/iTime.h. See ../dolphin.h.
#pragma once

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

long OSCheckActiveThreads(void);
void OSResetSystem(BOOL reset, u32 resetCode, BOOL forceMenu);

#ifdef __cplusplus
}
#endif
