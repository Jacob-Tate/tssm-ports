#ifndef XWAD4_H
#define XWAD4_H

#include "xutil.h"
#include "xCollide.h"
#include "iCollide.h"
#include "xEnt.h"
#include "xBase.h"
#include "xScene.h"
#include "zBase.h"
#include "xRMemData.h"
#include "xMemMgr.h"
#include "xhipio.h"
#include "xordarray.h"
#include "xpkrsvc.h"
#include "xPar.h"
#include "xParMgr.h"
#include "xParGroup.h"
#include "xParCmd.h"
#include "xPad.h"
#include "xVec3Inlines.h"
#include "xMath.h"
#include "xMathInlines.h"

struct tagiRenderArrays
{
    U16 m_index[960];
    RxObjSpace3DVertex m_vertex[480];
    F32 m_vertexTZ[480];
};

struct tagiRenderInput
{
    U16* m_index;
    RxObjSpace3DVertex* m_vertex;
    F32* m_vertexTZ;
    U32 m_mode;
    S32 m_vertexType;
    S32 m_vertexTypeSize;
    S32 m_indexCount;
    S32 m_vertexCount;
    xMat4x3 m_camViewMatrix;
    xVec4 m_camViewR;
    xVec4 m_camViewU;
};

tagiRenderArrays gRenderArr;
tagiRenderInput gRenderBuffer;

void iParMgrRender();
void iParMgrUpdate(F32);
void iParMgrInit();
void iParMgrRenderParSys_Sprite(void* data, xParGroup* ps);
void iParMgrRenderParSys_Streak(void* data, xParGroup* ps);
void iParMgrRenderParSys_Flat(void* data, xParGroup* ps);
void iParMgrRenderParSys_Static(void* data, xParGroup* ps);
void iParMgrRenderParSys_Ground(void* data, xParGroup* ps);
void iParMgrRenderParSys_QuadStreak(void* data, xParGroup* ps);
void iParMgrRenderParSys_InvStreak(void* data, xParGroup* ps);

#endif
