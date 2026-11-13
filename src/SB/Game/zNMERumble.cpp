#include "zNMERumble.h"
#include "zNMECommon.h"
#include "zScene.h"
#include "zGlobals.h"
#include "xString.h"
#include "xEvent.h"
#include "xRumbleEmitter.h"

namespace
{
    zNMERumbleTableEntry sNMERumble[6] = {
        { NME_TYPE_COMMON, "RUMBLE_NME_DEATH", NULL },
        { NME_TYPE_TIKI_WOOD, "RUMBLE_TIKI_DEATH", NULL },
        { NME_TYPE_TIKI_LOVEY, "RUMBLE_TIKI_DEATH", NULL },
        { NME_TYPE_TIKI_QUIET, "RUMBLE_TIKI_DEATH", NULL },
        { NME_TYPE_TIKI_STONE, "RUMBLE_TIKI_DEATH", NULL },
        { NME_TYPE_TIKI_THUNDER, "RUMBLE_THUNDER_TIKI_DEATH", NULL },
    };
    S32 sRumbleTableSize = sizeof(sNMERumble) / sizeof(sNMERumble[0]);
} // namespace

// No symbol (inlined into DeathFXKick and zNMEGoalTikiDying::Exit; the name is ours). Starts the
// rumble emitter for an enemy type (falling back to the common one) at the enemy's position.
void zNMERumblePlay(const zNMECommon* npc)
{
    en_npctyp type = (en_npctyp)npc->itemType;
    xRumble::sphericalEmitter* rumble = NULL;

    for (S32 i = 0; i < sRumbleTableSize; i++)
    {
        if (type == sNMERumble[i].nmeType)
        {
            rumble = sNMERumble[i].data;
            break;
        }
    }

    if (rumble == NULL)
    {
        rumble = sNMERumble[0].data;
    }

    if (rumble != NULL)
    {
        rumble->UpdatePosition(*(xVec3*)&npc->model->Mat->pos);
        zEntEvent(rumble, eEventStartRumbleEffect, globals.currentActivePad, 0.0f, 0.0f, 0.0f);
    }
}

void zNMERumbleScenePostInit()
{
    S32 i;

    for (i = 0; i < sRumbleTableSize; i++)
    {
        sNMERumble[i].data = NULL;
        if (sNMERumble[i].name != NULL && sNMERumble[i].name[0] != '\0')
        {
            sNMERumble[i].data =
                (xRumble::sphericalEmitter*)zSceneFindObject(xStrHash(sNMERumble[i].name));
        }
    }
}
