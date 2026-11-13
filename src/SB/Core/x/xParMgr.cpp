#include "xParMgr.h"
#include "xPar.h"

static U32 sFrameCount;
static S32 sParMgrInit;

static inline void xParMgrKillAllParticles()
{
}

void xParMgrInit()
{
    if (sParMgrInit == 0)
    {
        xParMemInit();
        iParMgrInit();
        sParMgrInit = 1;
    }
    else
    {
        xParMgrKillAllParticles();
        iParMgrInit();
    }
}

void xParMgrUpdate(F32 elapsedTime)
{
    iParMgrUpdate(elapsedTime);

    sFrameCount++;

    if (sFrameCount > 10)
    {
        sFrameCount = 1;
    }

    while (sFrameCount >= 1)
    {
        sFrameCount--;
    }
}

void xParMgrRender()
{
    iParMgrRender();
}
