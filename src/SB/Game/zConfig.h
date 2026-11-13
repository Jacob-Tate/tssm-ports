#ifndef ZCONFIG_H
#define ZCONFIG_H

#include <types.h>

enum ePlayerEnum
{
    PLAYER_BEGIN,
    COSTUME_SB_1 = 0,
    COSTUME_SB_2,
    COSTUME_SB_3,
    COSTUME_SB_4,
    COSTUME_SB_5,
    COSTUME_SB_6,
    COSTUME_SB_7,
    COSTUME_SB_A,
    COSTUME_SB_B,
    COSTUME_SB_C,
    COSTUME_SB_D,
    COSTUME_SB_X = 0xa,
    COSTUME_PAT_1,
    COSTUME_PAT_2,
    COSTUME_PAT_3,
    COSTUME_PAT_4,
    COSTUME_PAT_5,
    COSTUME_PAT_6,
    COSTUME_PAT_7,
    COSTUME_PAT_A,
    COSTUME_PAT_B,
    COSTUME_PAT_C,
    COSTUME_PAT_D,
    COSTUME_PAT_X = 0x15,
    SPONGEPAT_0,
    SPONGEPAT_1,
    SPONGEPAT_2,
    CAR_NORMAL,
    SPONGEBALL_NORMAL,
    PLAYER_COUNT
};

U8 zConfig_isPlayerPatrick(ePlayerEnum playerEnum);
U8 zConfig_isPlayerSpongebob(ePlayerEnum playerEnum);
U32 zConfig_getAlternateCostume(ePlayerEnum player);
U32 zConfig_getAlternateCostume(U32 normalPlayerTag);
void zConfig_setPointerPlayer_MapEntry(U32 sceneID, U32 playerTag);
S32 zConfig_getPlayers(U32 sceneID, U32* players);
ePlayerEnum zConfig_getPlayerEnum(U32 playerTag);
ePlayerEnum zConfig_getPlayerEnum(U32 sceneID, S32 playerIdx);
S32 zConfig_getPlayerCount(U32 sceneID);
char* zConfig_getPlayerModelInfoString(U32 _tag);
U32 zConfig_getPlayerTag(U32 sceneID, S32 playerIdx);
void zConfig_setScenePlayer_MapEntry(U32 sceneID, U32 playerTag);
U32 zConfig_getPlayerEnumIdx(U32 playerTag);

#endif
