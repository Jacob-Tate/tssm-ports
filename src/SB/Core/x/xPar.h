#ifndef XPAR_H
#define XPAR_H

#include "xMath3.h"
#include "xDebug.h"

struct xParEmitterAsset;

struct xPar
{
    xPar* m_next;
    xPar* m_prev;
    F32 m_lifetime;
    U8 m_c[4];
    xVec3 m_pos;
    F32 m_size;
    xVec3 m_vel;
    F32 m_sizeVel;
    U8 m_flag;
    U8 m_mode;
    U8 m_texIdx[2];
    U8 m_rotdeg[3];
    U8 pad8;
    F32 totalLifespan;
    xParEmitterAsset* m_asset;
    F32 m_cvel[4];
    F32 m_cfl[4];
};

extern xPar* gParDead;

void xParMemInit();
xPar* xParAlloc();
void xParInit(xPar* p);

// No symbol in the retail DOL: always inlined. The assert is Ratatouille's.
inline void xParFree(xPar* par)
{
    xASSERT(par);

    if (par->m_next != NULL)
    {
        par->m_next->m_prev = par->m_prev;
    }
    if (par->m_prev != NULL)
    {
        par->m_prev->m_next = par->m_next;
    }
    if (gParDead != NULL)
    {
        gParDead->m_prev = par;
    }
    par->m_next = gParDead;
    par->m_prev = NULL;
    gParDead = par;
}

#endif
