// The Dolphin SDK's OS calls that shared code still makes, on PC.
//
// OSCheckActiveThreads (zGame.cpp, once per frame): a STUB. On the console it
// walks the OS thread queue and halts on a corrupt one, a debugging check whose
// count the game ignores. A host has no OS thread queue to check; it answers 1,
// the main thread, and reports itself once.
//
// OSResetSystem (zMain.cpp, after the boot memory-card check formats a card):
// implemented as the PC's reset, which is what ResetButton::ResetSystem does in
// pc/iTRC.cpp -- the process exits. The arguments (hot reset, reset code, go to
// the IPL menu) have no host meaning.

#include <dolphin/os.h>

#include "iStub.h"

#include <stdio.h>
#include <stdlib.h>

extern "C" {

long OSCheckActiveThreads(void)
{
    IPORT_STUB();
    return 1;
}

void OSResetSystem(BOOL reset, u32 resetCode, BOOL forceMenu)
{
    printf("[pcport] OSResetSystem: the console would reset here; exiting\n");
    fflush(stdout);
    exit(0);
}

} // extern "C"
