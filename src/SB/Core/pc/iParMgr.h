#ifndef IPARMGR_H
#define IPARMGR_H

#include "xMath3.h"

#include <rwcore.h>

struct tagiRenderArrays
{
    U16 m_index[960];
    RxObjSpace3DVertex m_vertex[480];
    F32 m_vertexTZ[480];
};

struct tagiRenderInput
{
    U16* m_index;
    RwIm3DVertex* m_vertex;
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

extern tagiRenderArrays gRenderArr;
extern tagiRenderInput gRenderBuffer;

void iParMgrInit();
void iParMgrUpdate(F32 elapsedTime);
void iParMgrRender();

#endif
