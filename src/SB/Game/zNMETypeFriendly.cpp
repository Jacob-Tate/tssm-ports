#include "zNMETypeFriendly.h"

U32 g_hash_chumanim[9];
char* g_strz_chumanim[9] = {
    "Unknown", "Idle01", "Idle02", "Idle03", "Idle04", "Talk01", "Talk02", "Talk03", "Talk04",
};

U32 zNMEFriendly::AnimPick(S32 gid, en_npcgspot gspot, xGoal* goal)
{
    U32 da_anim = g_hash_chumanim[1];
    return da_anim;
}

xAnimTable* zNMEFriendly::CreateAnimTable(xAnimTable* table)
{
    if (table == NULL)
    {
        table = xAnimTableNew("zNMEFriendly", 0);
    }

    xAnimTableNewState(table, g_strz_chumanim[1], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    return table;
}
