#include "zUIText.h"

#include "xEvent.h"
#include "xTextAsset.h"
#include "xstransvc.h"

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>

// CJK text scaling, set up by zUITextInitSystem.
namespace
{
    U8 isCJK;
    F32 HSCALE;
    F32 VSCALE;
} // namespace

void zUIText::DoInit()
{
    zUI::DoInit();
    if (linkCount)
    {
        link = (xLinkAsset*)(GetAsset() + 1);
    }

    shadowColor = ((zUITextAsset*)asset)->shadowColor;
}

void zUIText::DoReset()
{
    zUI::DoReset();
    text = NULL;
    shadowColor = ((zUITextAsset*)asset)->shadowColor;
}

// No GC symbol: inlined into DoRender.
inline void zUIText::setup_textbox(xtextbox& tb, const zUITextAsset* asset) const
{
    tb.line_space = 0.0f;
    tb.tab_stop = 0.0f;
    tb.right_indent = 0.0f;
    tb.left_indent = 0.0f;

    tb.font.id = asset->font;
    tb.font.width = (1.0f / 640) * asset->fontSizeW;
    tb.font.height = (1.0f / 480) * asset->fontSizeH;
    tb.font.space = (1.0f / 640) * asset->fontSpacingX - tb.font.width;
    tb.font.color = current.color;
    tb.font.shadowColor = shadowColor;
    tb.font.shadowOffsetX = asset->shadowOffsetX;
    tb.font.shadowOffsetY = asset->shadowOffsetY;
    tb.font.clip.assign((1.0f / 640) * current.x, (1.0f / 480) * current.y,
                        (1.0f / 640) * current.width, (1.0f / 480) * current.height);

    tb.bounds = tb.font.clip;
    tb.bounds.contract((1.0f / 640) * asset->textBoxInsetLeft,
                       (1.0f / 480) * asset->textBoxInsetTop,
                       (1.0f / 640) * asset->textBoxInsetRight,
                       (1.0f / 480) * asset->textBoxInsetBottom);

    tb.flags = 0;

    // xtextbox flags: 0x1 = right, 0x2 = center (x); 0x4 = bottom, 0x8 = center (y).
    switch (asset->justifyX)
    {
    case 0:
        break;
    case 2:
        tb.flags |= 0x1;
        break;
    case 1:
    default:
        tb.flags |= 0x2;
        break;
    }

    switch (asset->justifyY)
    {
    case 0:
        break;
    case 2:
        tb.flags |= 0x4;
        break;
    case 1:
    default:
        tb.flags |= 0x8;
        break;
    }
}

void zUIText::DoRender() const
{
    if (current.width < 1.0f || current.height < 1.0f)
    {
        return;
    }

    if (text == NULL)
    {
        text = xTextFindString(((zUITextAsset*)asset)->textID, NULL);
        if (text == NULL)
        {
            text = "";
        }
    }

    if (*text == '\0')
    {
        return;
    }

    const zUITextAsset* asset = (const zUITextAsset*)this->asset;

    xtextbox tb;
    setup_textbox(tb, asset);
    tb.set_text(text);

    F32 hscale = 1.0f;
    F32 vscale = 1.0f;

    if (isCJK)
    {
        if (asset->textFlags & 0x1)
        {
            hscale = HSCALE;
            vscale = VSCALE;
        }
    }
    else if (asset->textFlags & 0x2)
    {
        F32 oldWidth = tb.bounds.w;
        F32 oldHeight = tb.bounds.h;

        tb.bounds.w = 1e8f;
        tb.bounds.h = 1e8f;

        const xtextbox::layout& layout = tb.temp_layout(true);

        F32 actualWidth = 0.0f;
        F32 actualHeight = 0.0f;

        // Not in the PS2 DWARF; retail walks the lines from a hoisted base pointer.
        const xtextbox::jot_line* lines = layout._lines;

        for (S32 i = 0; i < (S32)layout._lines_size; i++)
        {
            if (lines[i].bounds.w > actualWidth)
            {
                actualWidth = lines[i].bounds.w;
            }

            if (lines[i].bounds.h > actualHeight)
            {
                actualHeight = lines[i].bounds.h;
            }
        }

        tb.bounds.w = oldWidth;
        tb.bounds.h = oldHeight;

        if (actualWidth != 0.0f && actualWidth > 0.95f * tb.bounds.w)
        {
            hscale = 0.95f * tb.bounds.w / actualWidth;
        }

        if (actualHeight != 0.0f && actualHeight > 0.95f * tb.bounds.h)
        {
            vscale = 0.95f * tb.bounds.h / actualHeight;
        }
    }

    tb.font.width *= hscale;
    if (hscale < 1.0f)
    {
        tb.font.space = (1.0f / 640) * (asset->fontSpacingX * hscale) - tb.font.width;
    }

    tb.font.height *= vscale;
    if (vscale != 1.0f)
    {
        tb.bounds.y -= tb.font.height * (vscale - 1.0f) / 2.0f;
        tb.font.clip.y -= tb.font.height * (vscale - 1.0f) / 2.0f;
    }

    if (tb.font.width <= 0.0f || tb.font.height <= 0.0f)
    {
        return;
    }

    tb.render(false);
}

void zUIText::DoHandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                            U32 toParamWidgetID)
{
    switch (toEvent)
    {
    case eEventSetText:
        text = xTextFindString(toParamWidgetID, NULL);
        if (text == NULL)
        {
            text = "";
        }
        break;
    default:
        zUI::DoHandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
        break;
    }
}

void zUIText::DoApplyMotionFrame(const zUIMotionFrame* frame)
{
    zUI::DoApplyMotionFrame(frame);
    shadowColor.a = frame->color.a * ((zUITextAsset*)asset)->shadowColor.a / 255;
}

void zUIText_Init(xBase& data, xDynAsset& asset, size_t)
{
    zUIText_Init((zUIText*)&data, (zUITextAsset*)&asset);
}

void zUIText_Init(zUIText* ui, zUITextAsset* asset)
{
    new (ui) zUIText;
    zUI_Init(ui, asset);
}

// No GC symbol: auto-inlined into its only caller zUIMgrInit (the linker strips the
// out-of-line copy). Last function of zUIText.cpp in the PS2 build.
void zUITextInitSystem()
{
    xRegion region = xSTGetLocalizationEnum();

    switch (region)
    {
    case eRegionCH:
    case eRegionTW:
        isCJK = 1;
        HSCALE = 1.2f;
        VSCALE = 1.2f;
        break;
    case eRegionJP:
        isCJK = 1;
        HSCALE = 1.15f;
        VSCALE = 1.15f;
        break;
    case eRegionKR:
        isCJK = 1;
        HSCALE = 1.2f;
        VSCALE = 1.2f;
        break;
    default:
        isCJK = 0;
        break;
    }
}
