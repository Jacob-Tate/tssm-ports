#include "zNMEConfig.h"

static zMODConfig* g_mod_config;

// No GC symbol: inlined into zNMECentral_Startup (its unused copy is dead-stripped).
void zNMEConfig_Startup()
{
    static zMODConfig mod_config;

    g_mod_config = &mod_config;
    zNMECentral_RegisterModule(&mod_config, 'NCFG');
}

// No GC symbol (always inlined into zNMECentral_Shutdown).
inline void zNMEConfig_Shutdown()
{
    g_mod_config->Shutdown();
}

// No GC or PS2 symbol (inlined into ModulePrepUse); the name is ours, after
// zNMEGoals_RegisterTypes.
static inline void zNMEConfig_RegisterTypes(xFactory* fac)
{
    XGOFTypeInfo typelist[] = {
        { 0, NULL, NULL },
    };
    fac->RegItemType(typelist);
}

void zMODConfig::ModulePrepUse()
{
    cfgFactory = new ('NCFG', NULL) xFactory(2);

    zNMEConfig_RegisterTypes(cfgFactory);
}

void zMODConfig::ScenePrepare()
{
}

void zMODConfig::SceneFinish()
{
}
