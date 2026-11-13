#ifndef XPARSYS_H
#define XPARSYS_H

#include "xBase.h"
#include "xParCmd.h"
#include "xParGroup.h"

#include <rwcore.h>

struct xScene;

#define XPARSYSINFO_TYPE_SPRITE 0
#define XPARSYSINFO_TYPE_STREAK 1
#define XPARSYSINFO_TYPE_FLAT 2
#define XPARSYSINFO_TYPE_STATIC 3
#define XPARSYSINFO_TYPE_GROUND 4
#define XPARSYSINFO_TYPE_QUADSTREAK 5
#define XPARSYSINFO_TYPE_INVSTREAK 6

struct xParSysAsset : xBaseAsset
{
    U32 type;
    U32 parentParSysID;
    U32 textureID;
    U8 pad;
    U8 priority;
    U16 maxPar;
    U8 renderFunc;
    U8 renderSrcBlendMode;
    U8 renderDstBlendMode;
    U8 cmdCount;
    U32 cmdSize;
    U32 parFlags;
};

struct xParSys : xBase
{
    xParSysAsset* tasset;
    U32 cmdCount;
    xParCmd* cmd;
    xParSys* parent;
    xParGroup* group;
    U8 visible;
    RwTexture* txtr_particle;
};

struct xParSysInfo
{
    S32 type;
    void (*func)(void*, xParGroup*);
};

// No symbol on GC (always inlined); a global function in xParSys.cpp per the Ratatouille GC DWARF.
inline void xParGroupUpdate(xParSys* s, xParGroup* g, F32 dt)
{
    for (U32 i = 0; i < s->cmdCount; i++)
    {
        xParCmd* cmd = &s->cmd[i];
        xASSERT(cmd);
        if (cmd != NULL && cmd->tasset != NULL)
        {
            xParCmdUpdateFunc func = xParCmdGetUpdateFunc(cmd->tasset->type);
            if (func != NULL)
            {
                func(cmd, g, dt);
            }
        }
    }
}

void xParSysInit(void* b, void* tasset);
void xParSysInit(xBase* b, xParSysAsset* tasset);
void xParSysSetup(xParSys* t);
void xParSysExit(xParSys* t);
void xParSysRender(xBase* b);
void xParSysUpdate(xBase* to, xScene*, F32 dt);
void xParSysEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                    U32 toParamWidgetID);

#endif
