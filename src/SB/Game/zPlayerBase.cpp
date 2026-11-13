#include "zPlayerBase.h"
#include "xGlobals.h"
#include "xutil.h"
#include "xMemMgr.h"
#include "xstransvc.h"
#include "iSnd.h"
#include "zConfig.h"
#include "zGlobals.h"
#include "zScene.h"
#include "zPlayerSB.h"

#include <string.h>

static S32 sMemDepthJustHIPStartPlayer = -1;

void zPlayerUpdate(xEnt* ent, xScene* sc, F32 dt)
{
    zPlayer* player = (zPlayer*)ent;

    player->BeginUpdate(dt);
    player->Update(dt);
    player->EndUpdate(dt);
}

void zPlayerRender(zEnt* ent)
{
    ((zPlayer*)ent)->Render();
}

void zPlayerReset(zEnt* ent)
{
    ((zPlayer*)ent)->Reset();
}

// No GC symbol: inlined into zSceneInit.
inline void zPlayerLoadHIP(U32 sceneID)
{
    U32 tagPlayer = zConfig_getPlayerTag(sceneID, 0);

    if (tagPlayer == globals.playerTag)
    {
        if (globals.playerLoaded)
        {
            return;
        }
    }
    else
    {
        if (globals.playerLoaded)
        {
            zPlayerUnloadHIP();
        }
    }

    switch ((ePlayerEnum)zConfig_getPlayerEnumIdx(tagPlayer))
    {
    case COSTUME_SB_1:
    case COSTUME_SB_2:
    case COSTUME_SB_3:
    case COSTUME_SB_4:
    case COSTUME_SB_5:
    case COSTUME_SB_6:
    case COSTUME_SB_7:
    case COSTUME_SB_A:
    case COSTUME_SB_B:
    case COSTUME_SB_C:
    case COSTUME_SB_X:
    case COSTUME_PAT_1:
    case COSTUME_PAT_2:
    case COSTUME_PAT_3:
    case COSTUME_PAT_4:
    case COSTUME_PAT_5:
    case COSTUME_PAT_6:
    case COSTUME_PAT_7:
    case COSTUME_PAT_A:
    case COSTUME_PAT_B:
    case COSTUME_PAT_C:
    case COSTUME_PAT_X:
    case SPONGEPAT_0:
    case SPONGEPAT_1:
    case SPONGEPAT_2:
    case CAR_NORMAL:
    case SPONGEBALL_NORMAL:
        xglobals->___player_ent_dont_use_directly =
            new (xMemAllocSize(sizeof(zPlayerSB))) zPlayerSB;
        break;
    }

    globals.playerTag = tagPlayer;
    globals.player.checkpoint.jsp_active = NULL;
    sMemDepthJustHIPStartPlayer = xMemPushBase();
    zPlayerLoadMultipleHIPs(sceneID);
}

void zPlayerLoadMultipleHIPs(U32 sceneID)
{
    U32 tagPlayer;
    S32 i;
    S32 playerOrderCount;
    U32 playersToLoad;
    U8 needSBCommon;
    U8 needPatCommon;
    ePlayerEnum player;

    zConfig_getPlayers(sceneID, &playersToLoad);

    playerOrderCount = 0;
    needSBCommon = FALSE;
    needPatCommon = FALSE;

    memset(globals.playerLoadOrder, 0, sizeof(globals.playerLoadOrder));

    for (i = 0; i < zConfig_getPlayerCount(sceneID); i++)
    {
        player = zConfig_getPlayerEnum(sceneID, i);

        if (!needSBCommon && zConfig_isPlayerSpongebob(player))
        {
            needSBCommon = TRUE;
            globals.playerLoadOrder[playerOrderCount++] = 'PLSX';
            xUtil_idtag2string('PLSX', 0);
            zSceneLoadPackFile('PLSX', 1, false, NULL);
        }

        if (!needPatCommon && zConfig_isPlayerPatrick(player))
        {
            needPatCommon = TRUE;
            globals.playerLoadOrder[playerOrderCount++] = 'PLPX';
            xUtil_idtag2string('PLPX', 0);
            zSceneLoadPackFile('PLPX', 1, false, NULL);
        }
    }

    for (i = 0; i < zConfig_getPlayerCount(sceneID); i++)
    {
        tagPlayer = zConfig_getAlternateCostume(zConfig_getPlayerTag(sceneID, i));
        globals.playerLoadOrder[playerOrderCount] = tagPlayer;
        playerOrderCount++;
        zSceneLoadPackFile(tagPlayer, 1, false, NULL);
    }

    globals.playerLoaded = playersToLoad;
}

// No GC symbol: auto-inlined into zPlayerUnloadHIP and dead-stripped. The TSSM Xbox build has this
// loop and the memset as one out-of-line function; the name is ours.
void zPlayerUnloadMultipleHIPs()
{
    S32 i;

    for (i = 7; i >= 0; i--)
    {
        if (globals.playerLoadOrder[i])
        {
            xSTUnLoadSceneMany(globals.playerLoadOrder[i], 5);
            iSndSceneExit();
            xUtil_idtag2string(globals.playerLoadOrder[i], 0);
        }
    }

    // Retail bug: i is -1 here, so this clears playerLoaded (cleared again by zPlayerUnloadHIP)
    // instead of the player load order.
    memset(&globals.playerLoadOrder[i], 0, sizeof(globals.playerLoadOrder[i]));
}

// No GC symbol: inlined into zSceneExit and zSceneInit (via zPlayerLoadHIP); name from the
// Incredibles/Ratatouille DWARF.
inline void zPlayerUnloadHIP()
{
    xglobals->___player_ent_dont_use_directly->Exit();
    zPlayerUnloadMultipleHIPs();
    xMemPopBase(sMemDepthJustHIPStartPlayer);
    sMemDepthJustHIPStartPlayer = -1;
    globals.playerLoaded = 0;
}
