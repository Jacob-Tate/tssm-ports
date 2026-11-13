#include "xHudText.h"

#include "xString.h"
#include "xTextAsset.h"
#include "xstransvc.h"

#include "zScene.h"
#include "zTextBox.h"

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>

void xhud::text_widget::load(xBase& data, xDynAsset& asset, size_t)
{
    init_base(data, asset, sizeof(xBase) + sizeof(text_widget));
    text_widget* widget = (text_widget*)(&data + 1);
    new (widget) text_widget((text_asset&)asset);
}

void xhud::text_widget::setup()
{
    presetup();
    const text_asset& ta = *(const text_asset*)a;
    ztextbox* ztb = (ztextbox*)zSceneFindObject(ta.text_box);
    U32 len;
    char* s;

    if (!ztb)
    {
        tb = xtextbox::create();
        return;
    }

    ztb->refresh();
    tb = ztb->tb;
    ztb->get_text(text, sizeof(text));

    if (ta.text)
    {
        s = xTextFindString(ta.text, &len);
        if (s)
        {
            memcpy(text, s, len);
            text[len] = '\0';
        }
    }

    // The retail code converts back through U8, so each component ends up 0 or 1 (only 255
    // maps to 1). Kept as is: update() no longer copies rc's color into the text box.
    rc.r = (U8)(tb.font.color.r / 255.0f);
    rc.g = (U8)(tb.font.color.g / 255.0f);
    rc.b = (U8)(tb.font.color.b / 255.0f);
    rc.a = (U8)(tb.font.color.a / 255.0f);
    start_rc.r = rc.r;
    start_rc.g = rc.g;
    start_rc.b = rc.b;
    start_rc.a = rc.a;
}

void xhud::text_widget::destroy()
{
    destruct();
}

U32 xhud::text_widget::type() const
{
    static U32 myid = xStrHash(text_asset::type_name());
    return myid;
}

bool xhud::text_widget::is(U32 id) const
{
    return id == text_widget::type() || widget::is(id);
}

void xhud::text_widget::update(F32 dt)
{
    updater(dt);

    tb.set_text(text);
    tb.bounds.x = rc.loc.x;
    tb.bounds.y = rc.loc.y;
    tb.bounds.w = rc.size.x;
    tb.bounds.h = rc.size.y;

    tb.font.clip = tb.bounds;
}

void xhud::text_widget::render()
{
    tb.render(true);
}

void xhud::text_widget::set_text(const char* intext)
{
    U32 textchars = strlen(intext);
    textchars = MIN(textchars, sizeof(text) - 1);
    memcpy(text, intext, textchars);
    text[textchars] = '\0';
}
