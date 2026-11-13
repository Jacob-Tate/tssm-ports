#include "xParGroup.h"
#include "xPar.h"

#define REG_TABLE_SIZE 255

S32 sParGroupRegTableInit = 0;
S32 sParGroupRegTableCount = 0;
xParGroup* sParGroupRegTable[REG_TABLE_SIZE];

#define clamp(x, a, b) (((x) < (a)) ? (a) : (((x) > (b)) ? (b) : (x)))

void xParGroupAnimate(xParGroup* ps, F32 dt)
{
    xPar* i = ps->m_root;
    xPar* tmp;
    F32 age = dt;

    if (ps->m_flags & XPARGROUP_NOAGING)
    {
        age = 0.0f;
    }

    if (!ps->m_root)
    {
        ps->m_alive = 0;
    }

    while (i)
    {
        i->m_lifetime -= age;

        if (i->m_lifetime < 0.0f)
        {
            tmp = i;

            i = i->m_next;

            xParGroupKillPar(ps, tmp);
        }
        else
        {
            i->m_c[0] = i->m_cfl[0] = clamp(i->m_cvel[0] * dt + i->m_cfl[0], 0.0f, 255.0f);
            i->m_c[1] = i->m_cfl[1] = clamp(i->m_cvel[1] * dt + i->m_cfl[1], 0.0f, 255.0f);
            i->m_c[2] = i->m_cfl[2] = clamp(i->m_cvel[2] * dt + i->m_cfl[2], 0.0f, 255.0f);
            i->m_c[3] = i->m_cfl[3] = clamp(i->m_cvel[3] * dt + i->m_cfl[3], 0.0f, 255.0f);
            i->m_size += i->m_sizeVel * dt;

            i = i->m_next;
        }
    }
}

xPar* xParGroupAddPar(xParGroup* ps)
{
    xPar* p;

    if (ps->m_flags & XPARGROUP_ALLOCPARS)
    {
        p = xParAlloc();

        if (!p)
        {
            S32 myPriority = ps->m_priority;

            for (U8 i = 0; i < REG_TABLE_SIZE; i++)
            {
                if (sParGroupRegTable[i] && sParGroupRegTable[i] != ps &&
                    (sParGroupRegTable[i]->m_flags & XPARGROUP_ALLOCPARS) &&
                    !(sParGroupRegTable[i]->m_flags & XPARGROUP_UNK10) &&
                    sParGroupRegTable[i]->m_priority < myPriority && sParGroupRegTable[i]->m_root)
                {
                    xParGroupKillPar(sParGroupRegTable[i], sParGroupRegTable[i]->m_root);

                    p = xParAlloc();

                    break;
                }
            }
        }
    }
    else if (ps->m_dead)
    {
        p = ps->m_dead;
        ps->m_dead = p->m_next;

        if (ps->m_dead)
        {
            ps->m_dead->m_prev = NULL;
        }
    }
    else
    {
        p = NULL;
    }

    if (p)
    {
        xParInit(p);
        xParGroupAddParP(ps, p);
    }

    return p;
}

// Ratatouille's xParGroupKillPar asserts p. Compiled out, it still raises the inline cost so
// that xParEmitterEmit calls this out of line while its other callers inline it, as in retail.
void xParGroupKillPar(xParGroup* ps, xPar* p)
{
    xASSERT(p);

    if (p == ps->m_root)
    {
        ps->m_root = p->m_next;

        if (ps->m_root)
        {
            ps->m_root->m_prev = NULL;
        }

        p->m_next = NULL;
        p->m_prev = NULL;
    }
    else
    {
        if (p->m_next)
        {
            p->m_next->m_prev = p->m_prev;
        }

        if (p->m_prev)
        {
            p->m_prev->m_next = p->m_next;
        }

        p->m_prev = NULL;
        p->m_next = NULL;
    }

    ps->m_num_of_particles--;

    if (ps->m_flags & XPARGROUP_ALLOCPARS)
    {
        xParFree(p);
    }
    else
    {
        xParGroupAddParToDeadList(ps, p);
    }
}
