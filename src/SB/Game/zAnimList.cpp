#include "zAnimList.h"

#include "xMemMgr.h"
#include "xstransvc.h"

static S32 nals;
static U32* aids; // anim list asset IDs
static xAnimTable** atbls; // anim tables
static S32* anused;

static const char* astnames[2][10] = {
    "stop0", "stop1", "stop2", "stop3", "stop4", "stop5", "stop6", "stop7", "stop8", "stop9",
    "loop0", "loop1", "loop2", "loop3", "loop4", "loop5", "loop6", "loop7", "loop8", "loop9",
};

static U32 AlwaysConditional(xAnimTransition*, xAnimSingle*, void*)
{
    return 1;
}

void zAnimListInit()
{
    nals = xSTAssetCountByType('ALST');
    if (nals == 0)
    {
        return;
    }

    aids = (U32*)xMemAllocSize(nals * sizeof(U32));
    atbls = (xAnimTable**)xMemAllocSize(nals * sizeof(xAnimTable*));
    anused = (S32*)xMemAllocSize(nals * sizeof(S32));

    for (S32 i = 0; i < nals; i++)
    {
        U32 size;
        zAnimListAsset* zala = (zAnimListAsset*)xSTFindAssetByType('ALST', i, &size);
        PKRAssetTOCInfo ainfo;
        xSTGetAssetInfoByType('ALST', i, &ainfo);
        xAnimTable* atbl = xAnimTableNew("", 0);

        aids[i] = ainfo.aid;
        atbls[i] = atbl;
        anused[i] = 0;

        void* buf;
        if (zala->ids[0] == 0)
        {
            buf = NULL;
        }
        else
        {
            buf = xSTFindAsset(zala->ids[0], &size);
        }
        if (buf)
        {
            xAnimFile* afile = xAnimFileNew(buf, "", 0, NULL);
            xAnimTableNewState(atbl, "idle", 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                               xAnimDefaultBeforeEnter, NULL, NULL);
            xAnimTableAddFile(atbl, afile, "idle");
            anused[i]++;
        }

        SPtr idle_exists = (SPtr)buf;
        for (S32 j = 1; j < 10; j++)
        {
            if (zala->ids[j] == 0)
            {
                continue;
            }

            buf = xSTFindAsset(zala->ids[j], &size);
            if (!buf)
            {
                continue;
            }

            xAnimFile* afile = xAnimFileNew(buf, "", 0, NULL);
            if (idle_exists)
            {
                xAnimTableNewState(atbl, astnames[0][j], 0x20, 0, 1.0f, NULL, NULL, 0.0f, NULL,
                                   NULL, xAnimDefaultBeforeEnter, NULL, NULL);
                xAnimTableNewTransition(atbl, astnames[0][j], "idle", AlwaysConditional, NULL, 0x10,
                                        0, 0.0f, 0.0f, 0, 0, 0.0f, NULL);
            }
            else
            {
                xAnimTableNewState(atbl, astnames[0][j], 0, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                                   xAnimDefaultBeforeEnter, NULL, NULL);
            }

            xAnimTableAddFile(atbl, afile, astnames[0][j]);
            xAnimTableNewState(atbl, astnames[1][j], 0x10, 0, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                               xAnimDefaultBeforeEnter, NULL, NULL);
            xAnimTableAddFile(atbl, afile, astnames[1][j]);
            anused[i]++;
        }
    }
}

void zAnimListExit()
{
    nals = 0;
    aids = NULL;
    atbls = NULL;
    anused = NULL;
}

xAnimTable* zAnimListGetTable(U32 id)
{
    for (S32 i = 0; i < nals; i++)
    {
        if (aids[i] == id)
        {
            return atbls[i];
        }
    }

    return NULL;
}

S32 zAnimListGetNumUsed(U32 id)
{
    for (S32 i = 0; i < nals; i++)
    {
        if (aids[i] == id)
        {
            return anused[i];
        }
    }

    return 0;
}
