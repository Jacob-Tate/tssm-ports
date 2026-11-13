#ifndef XPARGROUP_H
#define XPARGROUP_H

#include "xPar.h"
#include "xDebug.h"

struct xParCmdTex;

struct xParGroup
{
    xPar* m_root;
    xPar* m_dead;
    S32 m_num_of_particles;
    U8 m_alive;
    U8 m_killWhenDead;
    U8 m_active;
    U8 m_visible;
    U8 m_culled;
    U8 m_priority;
    U8 m_flags;
    U8 m_regidx;
    xParGroup* m_next;
    xParGroup* m_prev;
    void (*draw)(void*, xParGroup*);
    xParCmdTex* m_cmdTex;
};

#define XPARGROUP_UNK1 0x1
#define XPARGROUP_ALLOCPARS 0x2
#define XPARGROUP_NOAGING 0x4
#define XPARGROUP_NOBACK2LIFE 0x8
#define XPARGROUP_UNK10 0x10

extern S32 sParGroupRegTableInit;
extern S32 sParGroupRegTableCount;
extern xParGroup* sParGroupRegTable[255];

void xParGroupAnimate(xParGroup* ps, F32 dt);
xPar* xParGroupAddPar(xParGroup* ps);
void xParGroupKillPar(xParGroup* ps, xPar* p);

// The helpers below have no symbol in the retail DOL: always inlined.

// The asserts here and below are Ratatouille's.
inline void xParGroupInit(xParGroup* ps)
{
    xASSERT(ps);

    ps->m_root = NULL;
    ps->m_dead = NULL;
    ps->m_alive = 0;
    ps->m_num_of_particles = 0;
    ps->m_killWhenDead = 0;
    ps->m_flags = 0;
    ps->m_active = 1;
    ps->m_culled = 0;
    ps->m_visible = 1;
    ps->m_cmdTex = NULL;
    ps->m_priority = 0;
    ps->m_next = NULL;
    ps->m_prev = NULL;

    ps->m_flags |= XPARGROUP_ALLOCPARS;
    ps->m_flags |= XPARGROUP_UNK1;
}

inline void xParGroupAddParP(xParGroup* ps, xPar* p)
{
    xASSERT(p);
    xASSERT(ps);

    ps->m_alive = 1;
    ps->m_num_of_particles++;

    p->m_prev = NULL;
    p->m_next = NULL;

    if (ps->m_root)
    {
        p->m_next = ps->m_root;
        ps->m_root->m_prev = p;
        ps->m_root = p;
    }
    else
    {
        ps->m_root = p;
    }
}

inline void xParGroupRegisterInit()
{
    for (S32 i = 0; i < 255; i++)
    {
        sParGroupRegTable[i] = NULL;
    }

    sParGroupRegTableInit = TRUE;
    sParGroupRegTableCount = 0;
}

inline void xParGroupSetPriority(xParGroup* ps, U8 val)
{
    ps->m_priority = val;
}

inline void xParGroupRegister(xParGroup* ps)
{
    if (!sParGroupRegTableInit)
    {
        xParGroupRegisterInit();
    }

    for (U8 i = 0; i < 255; i++)
    {
        if (!sParGroupRegTable[i])
        {
            sParGroupRegTable[i] = ps;
            ps->m_regidx = i;
            break;
        }
    }
}

inline void xParGroupSetAging(xParGroup* ps, S32 age)
{
    if (age)
    {
        ps->m_flags &= (U8)~XPARGROUP_NOAGING;
    }
    else
    {
        ps->m_flags |= XPARGROUP_NOAGING;
    }
}

inline void xParGroupSetBack2Life(xParGroup* ps, S32 b2l)
{
    if (b2l)
    {
        ps->m_flags &= (U8)~XPARGROUP_NOBACK2LIFE;
    }
    else
    {
        ps->m_flags |= XPARGROUP_NOBACK2LIFE;
    }
}

inline void xParGroupSetVisibility(xParGroup* ps, S32 vis)
{
    ps->m_visible = vis;
}

inline void xParGroupUnregister(xParGroup* ps)
{
    for (U8 i = 0; i < 255; i++)
    {
        if (sParGroupRegTable[i] == ps)
        {
            sParGroupRegTable[i] = NULL;
            ps->m_regidx = 0;
            break;
        }
    }
}

inline void xParGroupSetActive(xParGroup* ps, U32 isActive)
{
    ps->m_active = isActive;
}

inline void xParGroupKillAllParticles(xParGroup* ps)
{
    xPar* i = ps->m_root;

    while (i)
    {
        xPar* tmp = i;

        i = i->m_next;

        xParGroupKillPar(ps, tmp);
    }
}

inline void xParGroupAddParToDeadList(xParGroup* ps, xPar* p)
{
    xASSERT(p);

    if (ps->m_dead)
    {
        ps->m_dead->m_prev = p;
    }

    p->m_next = ps->m_dead;
    p->m_prev = NULL;

    ps->m_dead = p;
}

#endif
