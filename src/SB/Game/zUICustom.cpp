#include "zUI.h"
#include "zUICustom.h"
#include "zUIImage.h"

#include "zScene.h"

#include "xEvent.h"
#include "xGroup.h"
#include "xMemMgr.h"

// No GC symbols for these constructors: always inlined into zUI_Init.
inline zUICustomConditionallyDisabled::zUICustomConditionallyDisabled(zUI* ui)
    : zUICustom(ui), m_pCondition(NULL)
{
    xBase* pCond = zSceneFindObject(ui->asset->customWidget);

    // The redundant second NULL test is also in the PS2 build (probably left over from a
    // debug-only check of pCond).
    if (pCond)
    {
        if (pCond && pCond->baseType == eBaseTypeCond)
        {
            m_pCondition = (_zConditional*)pCond;
        }
    }
}

inline zUICustomImageSlideshow::zUICustomImageSlideshow(zUI* ui)
    : zUICustom(ui), m_CurrentImage(0), m_NumImages(0), m_TextureGroup(NULL)
{
    U32 widgetID = ui->asset->customWidget;

    if (widgetID)
    {
        SetTextureList((xGroup*)zSceneFindObject(widgetID));
    }
}

void zUICustom::Init()
{
}

void zUICustom::Setup()
{
}

void zUICustom::Reset()
{
}

void zUICustom::ResetMotion()
{
}

U8 zUICustom::HandleEvent(xBase*, unsigned int, const float*, xBase*, unsigned int)
{
    return 1;
}

void zUICustom::PreUpdate(float)
{
}

void zUICustom::PostUpdate(float)
{
}

U8 zUICustom::PreRender()
{
    return 1;
}

U8 zUICustom::PreAutoMenu()
{
    return 1;
}

void zUICustom::PostRender()
{
}

void zUICustom::Exit()
{
}

void zUICustom::PreInitMotion()
{
}

void zUICustom::PostInitMotion()
{
}

void zUICustom::PreApplyMotionFrame(zUIMotionFrame*)
{
}

void zUICustom::PostApplyMotionFrame(zUIMotionFrame*)
{
}

void* zUICustom::operator new(size_t size)
{
    return staticAllocator ? staticAllocator(size, staticUser) : xMemAlloc(gActiveHeap, size, 0);
}

void zUICustomImageSlideshow::Reset()
{
    m_CurrentImage = 0;
    if (m_TextureGroup)
    {
        UpdateTexture();
    }
}

U8 zUICustomImageSlideshow::HandleEvent(xBase* from, U32 toEvent, const F32* toParam,
                                        xBase* toParamWidget, U32 toParamWidgetID)
{
    switch (toEvent)
    {
    case eEventUISignalUp:
        m_CurrentImage = (m_CurrentImage < m_NumImages - 1) ? m_CurrentImage + 1 : 0;
        return 1;

    case eEventUISignalDown:
        m_CurrentImage = m_CurrentImage ? m_CurrentImage - 1 : m_NumImages - 1;
        return 1;

    case eEventUIChangeTexture:
        UpdateTexture();
        return 0;
    }

    return 1;
}

void zUICustomImageSlideshow::SetTextureList(xGroup* textureList)
{
    m_TextureGroup = textureList;
    m_CurrentImage = 0;
    m_NumImages = xGroupGetCount(m_TextureGroup);
}

void zUICustomImageSlideshow::UpdateTexture()
{
    U32 textureID = xGroupGetItem(m_TextureGroup, m_CurrentImage);
    zUIImage* image = (zUIImage*)ui;

    image->SetTexture(textureID, false);
}

void zUICustomConditionallyDisabled::PreUpdate(F32 dt)
{
    if (m_pCondition)
    {
        m_bCondition = zConditional_Evaluate(m_pCondition) != 0;
    }
    else
    {
        m_bCondition = 1;
    }
}

unsigned char zUICustomConditionallyDisabled::PreAutoMenu()
{
    return m_bCondition; // struct sees to be missing some data
}

unsigned char zUICustomConditionallyDisabled::PreRender()
{
    return m_bCondition; // struct sees to be missing some data
}
