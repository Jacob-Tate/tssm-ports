#ifndef ZCINEFX_H
#define ZCINEFX_H

#include <types.h>
#include "xCutscene.h"

struct xScene;
struct zCutsceneMgr;
struct NCINEntry;

struct NCINMap
{
    char* cinname;
    NCINEntry* cinfxtab;
    U32 cinhash;
};

struct NCINBeNosey : XCSNNosey
{
    zCutsceneMgr* use_csnmgr;
    NCINEntry* use_fxtab;

    void CanRenderNow();
    void UpdatedAnimated(RpAtomic* model, RwMatrixTag* animMat, U32 animIndex, U32 dataIndex, U32&);

    void Init(const zCutsceneMgr* csnmgr, NCINEntry* fxtab, S32 flags)
    {
        use_csnmgr = (zCutsceneMgr*)csnmgr;
        use_fxtab = fxtab;
        flg_nosey = flags;
    }

    void Done()
    {
        use_csnmgr = NULL;
        use_fxtab = NULL;
        flg_nosey = 0;
    }
};

void zCineFXStartup();
void zCineFXShutdown();
void zCineFXSceneSetup();
S32 zCineFXCutscenePrep(const xScene*, F32, const zCutsceneMgr* csnmgr);
void zCineFXCutscene(const xScene*, F32, const zCutsceneMgr* csnmgr);
void zCineFXCutsceneDone(const xScene*, F32, const zCutsceneMgr* csnmgr);

void zCineFXRegisterTable(NCINMap* maplist);

void zCineFX_Register_BossNeptune();
void zCineFX_Register_BossFrogFish();
void zCineFX_Register_BossDenOne();
void zCineFX_Register_BossDenTwo();
void zCineFX_Register_General();
void zCineFX_Register_IntroBuckOTron();
void zCineFX_Register_IntroFogger();
void zCineFX_Register_IntroFlinger();
void zCineFX_Register_IntroJelly();
void zCineFX_Register_IntroPopper();
void zCineFX_Register_IntroSlammer();
void zCineFX_Register_IntroSpinner();
void zCineFX_Register_IntroTurret();
void zCineFX_Register_IntroZapper();
void zCineFX_Register_SceneBB02();
void zCineFX_Register_SceneDE01();
void zCineFX_Register_SceneTR02();
void zCineFX_Register_SceneJK01();
void zCineFX_Register_ScenePT01();
void zCineFX_Register_ScenePT02();
void zCineFX_Register_TestTable();

void zCineFX_ZapperIntro_PostInit();

#endif
