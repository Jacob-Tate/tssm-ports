#ifndef ZNMECENTRAL_H
#define ZNMECENTRAL_H

#include <types.h>

// Base of the NME runtime modules (zMODEnemy, zMODConfig, ...).
// Virtual order must match __vt__7zModule; every hook defaults to doing nothing.
struct zModule
{
    struct
    {
        S32 flg_skipRuntime : 1;
        S32 flg_updateWhenPaused : 1;
        S32 flg_updateInCinematic : 1;
        S32 flg_needHUDRender : 1;
        S32 flg_notused : 28;
    };
    S32 modID;

    virtual void ScenePrepare()
    {
    }
    virtual void SceneFinish()
    {
    }
    virtual void SceneInit()
    {
    }
    virtual void ScenePostInit()
    {
    }
    virtual void SceneSetup()
    {
    }
    virtual void ScenePostSetup()
    {
    }
    virtual void SceneReset()
    {
    }
    virtual void Timestep(F32 dt)
    {
    }
    virtual void PostTimestep(F32 dt)
    {
    }
    virtual void RenderToBuckets()
    {
    }
    virtual void Render()
    {
    }
    virtual void PostRender()
    {
    }
    virtual void HudRender()
    {
    }
    virtual void ModulePrepUse()
    {
    }

    // Not virtual (no slot in __vt__7zModule), but not trivial: the derived modules' weak
    // destructors restore __vt__7zModule, and their function-local static instances register
    // them with __register_global_object.
    ~zModule()
    {
    }
};

void zNMECentral_SceneFinish();
void zNMECentral_ScenePrepare();
void zNMECentral_SceneInit();
void zNMECentral_SceneReset();
void zNMECentral_ScenePostSetup();
void zNMECentral_SceneSetup();
void zNMECentral_ScenePostInit();
void zNMECentral_HudRender();
void zNMECentral_PostRender();
void zNMECentral_Render();
void zNMECentral_RenderToBuckets();
void zNMECentral_Timestep(F32 dt);
void zNMECentral_PostTimestep(F32 dt);

// No GC symbol: a plain zNMECentral.cpp function (like Ratatouille's zModuleMgr_RegisterModule)
// that every module startup inlines, so the linker dead-strips its own copy.
void zNMECentral_RegisterModule(zModule* mod, S32 modID);

#endif
