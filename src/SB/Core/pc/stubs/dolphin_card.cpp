// The Dolphin SDK's memory-card calls that shared code still makes, on PC.
//
// The port saves through isavegame (pc/isavegame.cpp), which keeps the
// console's two card slots as two folders that are always present and always
// formatted. These calls are answered in those terms.
//
// CARDProbeEx (zSaveLoad.cpp's autosave probe): implemented. Slots 0 and 1
// answer READY, as a 59-block card (4 Mbit, 8 KB sectors) would; any other
// channel has no card.
//
// The rest (zMain.cpp's boot check: mount, check, format, delete corrupt
// files, poll the result) run only when isavegame reports an unformatted,
// damaged or corrupt card, which it never does on PC. They are STUBS that
// report themselves once and answer CARD_RESULT_FATAL_ERROR, so reaching one
// shows in the log as the bug it would be.

#include <dolphin/card.h>

#include "iStub.h"

#include <stddef.h>

// pc/isavegame.cpp's ISG_HOST_TARGETS: the slots that are folders.
#define PC_CARD_SLOTS 2

extern "C" {

s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize)
{
    if (chan < 0 || chan >= PC_CARD_SLOTS)
    {
        return CARD_RESULT_NOCARD;
    }

    if (memSize != NULL)
    {
        *memSize = 4;
    }
    if (sectorSize != NULL)
    {
        *sectorSize = 8 * 1024;
    }

    return CARD_RESULT_READY;
}

s32 CARDMount(s32 chan, void* workArea, CARDCallback detachCallback)
{
    IPORT_STUB();
    return CARD_RESULT_FATAL_ERROR;
}

s32 CARDUnmount(s32 chan)
{
    IPORT_STUB();
    return CARD_RESULT_FATAL_ERROR;
}

s32 CARDCheck(s32 chan)
{
    IPORT_STUB();
    return CARD_RESULT_FATAL_ERROR;
}

s32 CARDFormatAsync(s32 chan, CARDCallback callback)
{
    IPORT_STUB();
    return CARD_RESULT_FATAL_ERROR;
}

s32 CARDDeleteAsync(s32 chan, const char* fileName, CARDCallback callback)
{
    IPORT_STUB();
    return CARD_RESULT_FATAL_ERROR;
}

s32 CARDGetResultCode(s32 chan)
{
    IPORT_STUB();
    return CARD_RESULT_FATAL_ERROR;
}

s32 CARDGetXferredBytes(s32 chan)
{
    IPORT_STUB();
    return 0;
}

} // extern "C"
