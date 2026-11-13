#ifndef ZNMECONFIG_H
#define ZNMECONFIG_H

#include "zNMECentral.h"
#include "zNPCTypes.h"
#include "xFactory.h"
#include "xListItem.h"

// The config items cfgFactory creates (PS2 DWARF, zWadNME.cpp).
struct NMEConfig : xListItem<NMEConfig>, xFactoryInst
{
    en_npctyp npcType;
    S32 flg_didInit : 1;
    S32 flg_unused : 31;
};

struct CFGSearch
{
    en_npctyp typ_search;
    NMEConfig* match;
};

struct zMODConfig : zModule
{
    xFactory* cfgFactory;

    void ModulePrepUse();

    // No GC symbol (always inlined into zNMECentral_Shutdown).
    void Shutdown()
    {
        delete cfgFactory;
        cfgFactory = NULL;
    }

    void ScenePrepare();
    void SceneFinish();
};

void zNMEConfig_Startup();
void zNMEConfig_Shutdown();

#endif
