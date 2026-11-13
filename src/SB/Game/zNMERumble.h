#ifndef ZNMERUMBLE_H
#define ZNMERUMBLE_H

#include "zNPCTypes.h"

struct zNMECommon;

namespace xRumble
{
    struct sphericalEmitter;
}

// Size: 0xC (PS2 DWARF)
struct zNMERumbleTableEntry
{
    en_npctyp nmeType;
    char* name;
    xRumble::sphericalEmitter* data;
};

void zNMERumbleScenePostInit();
void zNMERumblePlay(const zNMECommon* npc);

#endif
