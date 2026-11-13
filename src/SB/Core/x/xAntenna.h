#ifndef XANTENNA_H
#define XANTENNA_H

#include <types.h>

struct xEnt;

// Signatures from the PS2 DWARF and the GC symbols (noLight is a bool).
void xAntennaInit();
void xAntennaSceneInit();
void xAntennaScenePostInit();
void xAntennaRender();
void xAntennaAddAntenna(xEnt* pParent, F32 length, F32 rigidity, S32 startBone, S32 endBone,
                        bool noLight);
void xAntennaUpdate(F32 dt);
void xAntennaRemove(xEnt* pParent, S32 startBone, S32 endBone);
void xAntennaRemoveAll();
void xAntennaSceneReset();

#endif
