#ifndef ZPLAYERBASE_H
#define ZPLAYERBASE_H

#include "zEnt.h"

void zPlayerReset(zEnt* ent);
void zPlayerRender(zEnt* ent);
void zPlayerUpdate(xEnt* ent, xScene* sc, F32 dt);
void zPlayerLoadHIP(U32 sceneID);
void zPlayerLoadMultipleHIPs(U32 sceneID);
void zPlayerUnloadHIP();

#endif
