#ifndef ZNMESHRAPNEL_H
#define ZNMESHRAPNEL_H

#include "zNPCTypes.h"

struct zShrapnelAsset;

// Size: 0xC (PS2 DWARF)
struct zNMEShrapnelTableEntry
{
    en_npctyp nmeType;
    char* name;
    zShrapnelAsset* data;
};

void zNMEShrapnelScenePostInit();

zShrapnelAsset* zNMEShrapnelRetrieveShrapnel(en_npctyp wantedType);

#endif
