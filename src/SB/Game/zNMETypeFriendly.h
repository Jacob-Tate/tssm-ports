#ifndef ZNMETYPEFRIENDLY_H
#define ZNMETYPEFRIENDLY_H

#include "zNMECommon.h"

extern U32 g_hash_chumanim[9];
extern char* g_strz_chumanim[9];

struct zNMEFriendly : zNMECommon
{
    U32 AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal);
    xAnimTable* CreateAnimTable(xAnimTable* table);
};

void ZNME_Friendly_Startup();

#endif
