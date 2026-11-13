// PC shadow of the Dolphin SDK's <dolphin/card.h>: the memory-card calls shared
// code still makes directly -- zMain.cpp's boot check (format a card, delete
// corrupt files) and zSaveLoad.cpp's autosave probe. Saving itself goes through
// isavegame, which on PC keeps the console's two card slots as two folders
// (pc/isavegame.cpp); src/SB/Core/pc/stubs/dolphin_card.cpp answers these calls
// in those terms. See ../dolphin.h.
#pragma once

#include <dolphin/types.h>

#define CARD_WORKAREA_SIZE (5 * 8 * 1024)

#define CARD_RESULT_UNLOCKED 1
#define CARD_RESULT_READY 0
#define CARD_RESULT_BUSY -1
#define CARD_RESULT_WRONGDEVICE -2
#define CARD_RESULT_NOCARD -3
#define CARD_RESULT_NOFILE -4
#define CARD_RESULT_IOERROR -5
#define CARD_RESULT_BROKEN -6
#define CARD_RESULT_EXIST -7
#define CARD_RESULT_NOENT -8
#define CARD_RESULT_INSSPACE -9
#define CARD_RESULT_NOPERM -10
#define CARD_RESULT_LIMIT -11
#define CARD_RESULT_NAMETOOLONG -12
#define CARD_RESULT_ENCODING -13
#define CARD_RESULT_CANCELED -14
#define CARD_RESULT_FATAL_ERROR -128

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*CARDCallback)(s32 chan, s32 result);

s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize);
s32 CARDMount(s32 chan, void* workArea, CARDCallback detachCallback);
s32 CARDUnmount(s32 chan);
s32 CARDCheck(s32 chan);
s32 CARDFormatAsync(s32 chan, CARDCallback callback);
s32 CARDDeleteAsync(s32 chan, const char* fileName, CARDCallback callback);
s32 CARDGetResultCode(s32 chan);
s32 CARDGetXferredBytes(s32 chan);

#ifdef __cplusplus
}
#endif
