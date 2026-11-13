#include "zConfig.h"

#include <types.h>

#include "xutil.h"
#include "zEntPlayer.h"
#include "zUICustom.h"
#ifdef PLATFORM_PC
#include "iCoop.h"
#endif

struct ScenePlayerMapEntry
{
    U32 sceneTag;
    U32 sceneMask;
    ePlayerEnum playerEnum[10];
    U32 pointerToPlayer[10];
    S32 playerCount;
    S32 pointerCount;
};

struct AlternateCostumes
{
    U32 sceneTag;
    U32 normalPlayerTag[10];
    U32 alternatePlayerTag[10];
    S32 alternatePlayerCount;
};

namespace
{
    char* PlayerModelInfo[PLAYER_COUNT] = {
        "costume_sb_1_bind.MINF",  "costume_sb_2_bind.MINF",  "costume_sb_3_bind.MINF",
        "costume_sb_4_bind.MINF",  "costume_sb_5_bind.MINF",  "costume_sb_6_bind.MINF",
        "costume_sb_7_bind.MINF",  "costume_sb_a_bind.MINF",  "costume_sb_b_bind.MINF",
        "costume_sb_c_bind.MINF",  "costume_sb_d_bind.MINF",  "costume_pat_1_bind.MINF",
        "costume_pat_2_bind.MINF", "costume_pat_3_bind.MINF", "costume_pat_4_bind.MINF",
        "costume_pat_5_bind.MINF", "costume_pat_6_bind.MINF", "costume_pat_7_bind.MINF",
        "costume_pat_a_bind.MINF", "costume_pat_b_bind.MINF", "costume_pat_c_bind.MINF",
        "costume_pat_d_bind.MINF", "spongepat_0_bind.MINF",   "spongepat_1_bind.MINF",
        "spongepat_2_bind.MINF",   "patty_wagon_bind.MINF",   "sb_ball_bind.MINF",
    };

    const U32 PlayerTags[PLAYER_COUNT] = {
        'PLS1', 'PLS2', 'PLS3', 'PLS4', 'PLS5', 'PLS6', 'PLS7', 'PLSA', 'PLSB',
        'PLSC', 'PLSD', 'PLP1', 'PLP2', 'PLP3', 'PLP4', 'PLP5', 'PLP6', 'PLP7',
        'PLPA', 'PLPB', 'PLPC', 'PLPD', 'PLD0', 'PLD1', 'PLD2', 'PLY2', 'PLY4',
    };

    ScenePlayerMapEntry ScenePlayerMap[128] = { 0 };
    S32 ScenePlayerMapCount;
    AlternateCostumes AltCostumes[128];
} // namespace

U32 zConfig_getPlayerEnumIdx(U32 playerTag)
{
    U32 i;

    for (i = 0; i < PLAYER_COUNT; i++)
    {
        if (playerTag == PlayerTags[i])
        {
            return i;
        }
    }

    return 0;
}

void zConfig_setScenePlayer_MapEntry(U32 sceneID, U32 playerTag)
{
    for (S32 i = 0; i < ScenePlayerMapCount; i++)
    {
        if (sceneID == ScenePlayerMap[i].sceneTag)
        {
            ScenePlayerMap[i].playerEnum[ScenePlayerMap[i].playerCount] =
                (ePlayerEnum)zConfig_getPlayerEnumIdx(playerTag);
            ScenePlayerMap[i].playerCount++;
            return;
        }
    }

    // A '?' in any byte of the scene ID acts as a wildcard for that byte.
    U32 sceneMask = 0xFFFFFFFF;
    for (S32 i = 0; i < 4; i++)
    {
        if (((sceneID >> (i * 8)) & 0xFF) == '?')
        {
            sceneMask &= ~(0xFF << (i * 8));
        }
    }

    ScenePlayerMap[ScenePlayerMapCount].sceneTag = sceneID;
    ScenePlayerMap[ScenePlayerMapCount].sceneMask = sceneMask;
    ScenePlayerMap[ScenePlayerMapCount].playerEnum[ScenePlayerMap[ScenePlayerMapCount].playerCount] =
        (ePlayerEnum)zConfig_getPlayerEnumIdx(playerTag);
    ScenePlayerMap[ScenePlayerMapCount].playerCount++;
    ScenePlayerMapCount++;
}

U32 zConfig_getPlayerTag(U32 sceneID, S32 playerIdx)
{
    S32 i;
    U32 mask;

    for (i = 0; i < ScenePlayerMapCount; i++)
    {
        mask = ScenePlayerMap[i].sceneMask;
        if ((sceneID & mask) == (mask & ScenePlayerMap[i].sceneTag))
        {
            // Retail bug: the range check uses && instead of ||, so it never rejects an index.
            if (playerIdx < 0 && playerIdx >= ScenePlayerMap[i].playerCount)
            {
                break;
            }
            return PlayerTags[ScenePlayerMap[i].playerEnum[playerIdx]];
        }
    }

    return PlayerTags[0];
}

char* zConfig_getPlayerModelInfoString(U32 _tag)
{
    S32 i;

    for (i = 0; i < PLAYER_COUNT; i++)
    {
        if (_tag == PlayerTags[i])
        {
            return PlayerModelInfo[i];
        }
    }

    return PlayerModelInfo[0];
}

S32 zConfig_getPlayerCount(U32 sceneID)
{
    S32 i;

    for (i = 0; i < ScenePlayerMapCount; i++)
    {
        if (sceneID == ScenePlayerMap[i].sceneTag)
        {
            return ScenePlayerMap[i].playerCount;
        }
    }

    return 1;
}

ePlayerEnum zConfig_getPlayerEnum(U32 sceneID, S32 playerIdx)
{
    S32 i;

    for (i = 0; i < ScenePlayerMapCount; i++)
    {
        if (sceneID == ScenePlayerMap[i].sceneTag)
        {
            // Retail bug: the range check uses && instead of ||, so it never rejects an index.
            if (playerIdx < 0 && playerIdx >= ScenePlayerMap[i].playerCount)
            {
                break;
            }
            return ScenePlayerMap[i].playerEnum[playerIdx];
        }
    }

    return PLAYER_BEGIN;
}

ePlayerEnum zConfig_getPlayerEnum(U32 playerTag)
{
    S32 i;

    for (i = 0; i < PLAYER_COUNT; i++)
    {
        if (playerTag == PlayerTags[i])
        {
            return (ePlayerEnum)i;
        }
    }

    return PLAYER_BEGIN;
}

S32 zConfig_getPlayers(U32 sceneID, U32* players)
{
    S32 i;
    S32 j;

    *players = 0;

    for (i = 0; i < ScenePlayerMapCount; i++)
    {
        if (sceneID == ScenePlayerMap[i].sceneTag)
        {
            for (j = 0; j < ScenePlayerMap[i].playerCount; j++)
            {
                *players |= 1 << ScenePlayerMap[i].playerEnum[j];
            }
            return ScenePlayerMap[i].playerCount;
        }
    }

    *players = 1;
    return 1;
}

void zConfig_setPointerPlayer_MapEntry(U32 sceneID, U32 playerTag)
{
    S32 i;

    for (i = 0; i < ScenePlayerMapCount; i++)
    {
        if (sceneID == ScenePlayerMap[i].sceneTag)
        {
            ScenePlayerMap[i].pointerToPlayer[ScenePlayerMap[i].pointerCount] = playerTag;
            ScenePlayerMap[i].pointerCount++;
            return;
        }
    }

    ScenePlayerMap[ScenePlayerMapCount].sceneTag = sceneID;
    ScenePlayerMap[ScenePlayerMapCount]
        .pointerToPlayer[ScenePlayerMap[ScenePlayerMapCount].pointerCount] = playerTag;
    ScenePlayerMap[ScenePlayerMapCount].pointerCount++;
    ScenePlayerMapCount++;
}

U32 zConfig_getAlternateCostume(U32 normalPlayerTag)
{
    ePlayerEnum player = zConfig_getPlayerEnum(normalPlayerTag);
    return zConfig_getAlternateCostume(player);
}

U32 zConfig_getAlternateCostume(ePlayerEnum player)
{
    U32 altCostume;

#ifdef PLATFORM_PC
    // Online co-op (docs/MULTIPLAYER.md 7.4): the costume chosen for co-op,
    // when it is unlocked in this save. FALSE at once when co-op is off.
    if (iCoopCostumeOverride(zConfig_isPlayerSpongebob(player) ? 0 : (zConfig_isPlayerPatrick(player) ? 1 : -1),
                             &altCostume))
    {
        xUtil_idtag2string(altCostume, 0);
        return altCostume;
    }
#endif

    if (zConfig_isPlayerSpongebob(player))
    {
        altCostume = zUICustomExtrasManager::IsCostumeEnabled(eCurrentPlayerSpongeBob);
        if (altCostume)
        {
            xUtil_idtag2string(altCostume, 0);
            return altCostume;
        }
    }
    else if (zConfig_isPlayerPatrick(player))
    {
        altCostume = zUICustomExtrasManager::IsCostumeEnabled(eCurrentPlayerPatrick);
        if (altCostume)
        {
            xUtil_idtag2string(altCostume, 0);
            return altCostume;
        }
    }

    if (player >= PLAYER_BEGIN && player < PLAYER_COUNT)
    {
        altCostume = PlayerTags[player];
    }
    else
    {
        altCostume = PlayerTags[0];
    }

    xUtil_idtag2string(altCostume, 0);
    return altCostume;
}

U8 zConfig_isPlayerSpongebob(ePlayerEnum playerEnum)
{
    U8 isSpongebob = 0;
    if ((playerEnum >= 0) && (playerEnum <= 10))
    {
        isSpongebob = 1;
    }
    return isSpongebob;
}

U8 zConfig_isPlayerPatrick(ePlayerEnum playerEnum)
{
    U8 isPatrick = 0;
    if ((playerEnum >= 10) && (playerEnum <= 0x15))
    {
        isPatrick = 1;
    }
    return isPatrick;
}
