#ifndef ZCUTSCENEMGR_H
#define ZCUTSCENEMGR_H

#include "xCutsceneMgr.h"
#include "xModel.h"
#include "rpworld.h"

extern F32 gSkipTimeCutscene;

struct xScene;
struct zCutsceneMgr : xCutsceneMgr
{
    U8 pausedMusic;
    U8 sendDoneForDisabledCutscene;
};

struct zCutsceneHack
{
    char* cinname;
    char* modelname;
    U32 noshadow;
    U32 numPipes;
    RpAtomic* (*renderCB)(RpAtomic*);
    xModelPipe pipe[8];
};

struct zCutSceneNames
{
    char name[64];
    S32 played;
    S32 skipOK;
};

void zCutsceneMgrInit(void* b, void* tasset);
void zCutsceneMgrInit(xBase* b, xCutsceneMgrAsset* tasset);
void zCutsceneMgrReset(zCutsceneMgr* mgr);
void zCutsceneMgrSave(zCutsceneMgr*, xSerial* s);
void zCutsceneMgrLoad(zCutsceneMgr*, xSerial* s);
RpAtomic* HackBoundCB(RpAtomic* atomic, void* data);
RpMaterial* HackAlphaSetMaterialAlphaCB(RpMaterial* material, void* data);
RpAtomic* HackAlphaCB(RpAtomic* atomic, void* data);
void zCutSceneNamesTable_clearAll();
void zCutsceneMgrEventCB(xBase*, xBase* to, U32 toEvent, const F32*, xBase*, U32);
void zCutsceneMgrFinishLoad(xBase* to);
void zCutsceneMgrFinishExit(xBase* to);

void zCutsceneMgrKillFX(zCutsceneMgr* t);
void zCutsceneMgrUpdateFX(zCutsceneMgr* t, F32);
void zCutsceneMgrUpdate(xBase* to, xScene* sc, F32 dt);
void check_hide_entities();

#endif
