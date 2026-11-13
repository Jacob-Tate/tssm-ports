#ifndef XPARTICLEBATCH_H
#define XPARTICLEBATCH_H

#include <types.h>

#include "xPtankPool.h"

// Layouts from the PS2 DWARF (xWad4.cpp); create_system takes a const reference on GC
// (create_system__19xParticleBatchGroupFRC20xParticleBatchSystem).
// total size: 0x28
struct xParticleBatchSystem
{
    ptank_group_type pool_type; // 0x0
    render_state rs; // 0x4
    U32 order_group; // 0x14
    S32 order_index; // 0x18
    S32 stride; // 0x1C
    S32 (*update)(U8*, S32, ptank_pool&, F32, void*); // 0x20
    void* context; // 0x24
};

struct xParticleBatchGroup
{
    U8* elements;
    S32 elements_size;

    S32 create_system(const xParticleBatchSystem& system);
    void create();
};

S32 xParticleBatchEmit(S32 system_id, S32 amount, U8** mem);
S32 xParticleBatchAddSystem(const xParticleBatchSystem& system);
void xParticleBatchPreRender();
void xParticleBatchReset();
void xParticleBatchSceneExit();
void xParticleBatchSceneEnter();

#endif
