#include "xHudMeter.h"
#include "xMath.h"
#include "xMemMgr.h"

// No symbol on GC (always inlined); the PS2 line table puts the whole test on one line.
static inline bool InRange(const RwTexCoords& tex, const RwTexCoords& low, const RwTexCoords& high)
{
    return tex.u >= low.u && tex.u <= high.u && tex.v >= low.v && tex.v <= high.v;
}

zHud::TextureScroller::TextureScroller()
{
    m_prevOffset = 0.0f;
    m_aMoveIndices = NULL;
}

void zHud::TextureScroller::setup(xhud::model_widget* pModelWidget, RwTexCoords low,
                                  RwTexCoords high, F32 initialOffset, F32 finalOffset,
                                  eDirection dir)
{
    S32 numVerts;
    RwTexCoords* pTex;
    S32 i;

    m_initialOffset = 0.0f;
    m_finalOffset = finalOffset;
    m_pModelWidget = pModelWidget;
    m_dir = dir;

    pTex = iModelLockAndGetTextureCoords(m_pModelWidget->model->Data, &numVerts);

    m_moveCount = 0;
    for (i = 0; i < numVerts; i++)
    {
        if (InRange(pTex[i], low, high))
        {
            m_moveCount++;
        }
    }

    m_aMoveIndices = (U16*)xMemAlloc(gActiveHeap, m_moveCount * sizeof(U16), 0);

    m_moveCount = 0;
    for (i = 0; i < numVerts; i++)
    {
        if (InRange(pTex[i], low, high))
        {
            m_aMoveIndices[m_moveCount] = i;
            m_moveCount++;
        }
    }

    iModelUnlock(m_pModelWidget->model->Data);

    shiftUVs(initialOffset - m_initialOffset);
    m_initialOffset = initialOffset;
}

void zHud::TextureScroller::set(F32 moveOffset)
{
    F32 actualOffset;

    moveOffset = CLAMP(moveOffset, 0.0f, 1.0f);
    actualOffset = moveOffset - m_prevOffset;
    m_prevOffset = moveOffset;
    actualOffset *= m_finalOffset;

    shiftUVs(actualOffset);
}
