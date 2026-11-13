#include "zNMEParFX.h"
#include "zGameExtras.h"
#include "xordarray.h"

// Static in retail, but declared extern in zNMEParFX.h for its inlines. CodeWarrior accepts
// redeclaring an extern as static (GCC/Clang need -fpermissive).
WAD_STATIC NPARMgmt g_npar_mgmt[NPAR_TYP_NOMORE];
WAD_STATIC NPARInfo* g_npar_info[NPAR_TYP_NOMORE];

void zNMEParFX_Startup()
{
    static zMODParFX mod_nmeparfx;

    zNMECentral_RegisterModule(&mod_nmeparfx, 'NPAR');
    mod_nmeparfx.gameExtrasFlags = 0;
    mod_nmeparfx.month = 0;
    mod_nmeparfx.day = 0;
    zNMEParFX_RegisterTypes();
}

void zMODParFX::ScenePrepare()
{
    S32 i;

    for (i = 0; i < NPAR_TYP_NOMORE; i++)
    {
        g_npar_mgmt[i].Clear();
    }
}

void zMODParFX::SceneFinish()
{
    S32 i;

    for (i = 0; i < NPAR_TYP_NOMORE; i++)
    {
        g_npar_mgmt[i].Done();
    }

    gameExtrasFlags = 0;
    month = 0;
    day = 0;
}

void zMODParFX::SceneReset()
{
    for (S32 i = 0; i < NPAR_TYP_NOMORE; i++)
    {
        g_npar_mgmt[i].Reset();
    }
}

void zMODParFX::Timestep(F32 dt)
{
    gameExtrasFlags = zGameExtras_ExtrasFlags();
    zGameExtras_MoDay(&month, &day);
    isSpecialDay = gameExtrasFlags & 0x1F7;
}

NPARMgmt* zNMEParFX_PartyInvite(en_npartyp npartype, void** userData, S32 skipWarn)
{
    NPARMgmt* mgmt = &g_npar_mgmt[npartype];

    if (mgmt->inf_npar != NULL)
    {
        return mgmt;
    }

    mgmt->Init(npartype, userData);
    return mgmt;
}
