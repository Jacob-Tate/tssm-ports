#ifndef ZUIMGR_H
#define ZUIMGR_H

#include "zScene.h"

struct zUI;

void zUIMgrInit(zScene* s);
void zUIMgrReset();
void zUIMgrUpdate(F32 dt);
void zUIMgrRender();
void zUIMgrExit(zScene* s);
void zUIMgrEnableHDR(bool enabled);
void zUIMgrSortList(zUI** list, S32 count);

#endif
