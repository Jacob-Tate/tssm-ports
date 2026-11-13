#include "xColor.h"

// Only these colors survive in retail; the PS2 DWARF lists the full HI color set, the others
// are unreferenced and dead-stripped.
const iColor_tag g_YELLOW = { 255, 255, 0, 255 };
const iColor_tag g_BLACK = { 0, 0, 0, 255 };
const iColor_tag g_WHITE = { 255, 255, 255, 255 };
const iColor_tag g_CLEAR = { 0, 0, 0, 0 };
const iColor_tag g_PIMP_GOLD = { 215, 220, 19, 255 };

// Dead-stripped in retail; its "" initializer is still in the string pool here.
char* g_xColor_FMT = "";
