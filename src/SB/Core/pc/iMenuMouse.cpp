// The mouse in the game's own menus. The argument for the shape of it is in
// iMenuMouse.h.

#include "iMenuMouse.h"

#include "iMouseLook.h"
#include "iPadPrompts.h"
#include "iScreen.h"
#include "iWindow.h"
#include "xEvent.h"
#include "xFont.h"
#include "xPad.h"
#include "xTRC.h"
#include "xTextAsset.h"
#include "xstransvc.h"
#include "zBase.h"
#include "zGlobals.h"
#include "zScene.h"
#include "zUI.h"
#include "zUIBox.h"
#include "zUIController.h"
#include "zUIImage.h"
#include "zUIModel.h"
#include "zUIText.h"
#include "zUIUserString.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // Every widget of the scene, as zUIMgrInit gathers them. This list and the
    // other two of kMaxWidgets (sSteps, Press's) are made the first time they
    // are used: a console without a mouse never uses them.
    const S32 kMaxWidgets = 1024;
    zUI** sAll;
    S32 sCount;

    // The pointer in the menus' own units, 640x480 across the UI box.
    F32 sX;
    F32 sY;

    // What the pointer was on when it last moved or clicked, for the hand and
    // the log.
    const zUI* sOver;
    const zUI* sOverHint;

    // A click that the menu took stays taken from the button the mouse button
    // is bound to until the button is let go.
    bool sLeftTaken;
    bool sRightTaken;

    template <class T> void Gather(zScene* s, S32 type)
    {
        T* list = (T*)s->baseList[type];
        if (list == NULL)
        {
            return;
        }
        for (S32 i = 0; i < s->baseCount[type] && sCount < kMaxWidgets; i++)
        {
            sAll[sCount++] = &list[i];
        }
    }

    void GatherAll(zScene* s)
    {
        sCount = 0;
        if (sAll == NULL && (sAll = (zUI**)malloc(kMaxWidgets * sizeof(zUI*))) == NULL)
        {
            return;
        }
        Gather<zUIText>(s, eBaseTypeUIText);
        Gather<zUIImage>(s, eBaseTypeUIImage);
        Gather<zUIModel>(s, eBaseTypeUIModel);
        Gather<zUIBox>(s, eBaseTypeUIBox);
        Gather<zUIUserString>(s, eBaseTypeUIUserString);
        Gather<zUIController>(s, eBaseTypeUIController);
    }

    // The widget an asset ID names, or -1. Looked up in the gathered list
    // rather than cast from zSceneFindObject's xBase: a widget is not at its
    // xBase's address on every compiler.
    S32 Find(U32 id)
    {
        for (S32 i = 0; i < sCount; i++)
        {
            if (sAll[i]->id == id)
            {
                return i;
            }
        }
        return -1;
    }

    // Window pixels to the menus' units: through the letterbox the backends
    // put the render size in, then through the centred 4:3 UI box.
    bool Scale(F32* s, F32* ox, F32* oy)
    {
        S32 ww = 0;
        S32 wh = 0;
        iWindowGetSize(&ww, &wh);
        F32 rw = (F32)iScreenWidth();
        F32 rh = (F32)iScreenHeight();
        if (ww <= 0 || wh <= 0 || rw <= 0.0f || rh <= 0.0f)
        {
            return false;
        }

        *s = (F32)ww / rw;
        if (rh * *s > (F32)wh)
        {
            *s = (F32)wh / rh;
        }
        *ox = 0.5f * ((F32)ww - rw * *s);
        *oy = 0.5f * ((F32)wh - rh * *s);
        return true;
    }

    bool ToMenu(F32 wx, F32 wy, F32* mx, F32* my)
    {
        F32 s;
        F32 ox;
        F32 oy;
        if (!Scale(&s, &ox, &oy))
        {
            return false;
        }

        F32 rx = (wx - ox) / s;
        F32 ry = (wy - oy) / s;
        *mx = (rx - iScreenUIOriginXF()) / iScreenUIWidthF() * 640.0f;
        *my = (ry - iScreenUIOriginYF()) / iScreenUIHeightF() * 480.0f;
        return true;
    }

    bool IsMove(U32 e)
    {
        return e == eEventPadPressUp || e == eEventPadPressDown || e == eEventPadPressLeft ||
               e == eEventPadPressRight;
    }

    U32 MoveButton(U32 e)
    {
        switch (e)
        {
        case eEventPadPressUp:
            return XPAD_BUTTON_UP;
        case eEventPadPressDown:
            return XPAD_BUTTON_DOWN;
        case eEventPadPressLeft:
            return XPAD_BUTTON_LEFT;
        default:
            return XPAD_BUTTON_RIGHT;
        }
    }

    // The events that give a widget the highlight.
    bool IsSelect(U32 e)
    {
        return e == eEventUISelect || e == eEventUIFocusOn_Select ||
               e == eEventUIVisible_FocusOn_Select;
    }

    bool HasLink(const zUI* ui, U32 srcEvent)
    {
        for (S32 i = 0; i < ui->linkCount; i++)
        {
            if (ui->link[i].srcEvent == srcEvent)
            {
                return true;
            }
        }
        return false;
    }

    // Whether a press of `srcEvent` moves the highlight off this widget to
    // another, by its own links.
    bool MovesBy(const zUI* ui, U32 srcEvent)
    {
        for (S32 i = 0; i < ui->linkCount; i++)
        {
            const xLinkAsset& link = ui->link[i];
            if (link.srcEvent == srcEvent && IsSelect(link.dstEvent) && link.dstAssetID != ui->id &&
                Find(link.dstAssetID) >= 0)
            {
                return true;
            }
        }
        return false;
    }

    bool HasMoveLink(const zUI* ui)
    {
        return MovesBy(ui, eEventPadPressUp) || MovesBy(ui, eEventPadPressDown) ||
               MovesBy(ui, eEventPadPressLeft) || MovesBy(ui, eEventPadPressRight);
    }

    bool Shown(const zUI* ui)
    {
        return ui->visible && ui->current.color.a != 0;
    }

    // Whether a pad press reaches the widget (zUIMgrUpdate, zUI::DoUpdate).
    bool Hears(const zUI* ui)
    {
        return xBaseIsEnabled(ui) && (ui->focus || ((ui->asset->flags & 0x20) && Shown(ui)));
    }

    const char* TextOf(const zUI* ui)
    {
        if (ui->baseType != eBaseTypeUIText)
        {
            return NULL;
        }
        const char* text = xTextFindString(((const zUITextAsset*)ui->asset)->textID, NULL);
        return (text != NULL && text[0] != '\0') ? text : NULL;
    }

    // The widget's rectangle, in the menus' units. FALSE for one too small to
    // point at (the managers that sit under every menu).
    bool BoxRect(const zUI* ui, F32* x0, F32* y0, F32* x1, F32* y1)
    {
        *x0 = ui->current.x;
        *y0 = ui->current.y;
        *x1 = *x0 + ui->current.width;
        *y1 = *y0 + ui->current.height;
        if (*x1 < *x0)
        {
            F32 t = *x0;
            *x0 = *x1;
            *x1 = t;
        }
        if (*y1 < *y0)
        {
            F32 t = *y0;
            *y0 = *y1;
            *y1 = t;
        }
        return *x1 - *x0 >= 8.0f && *y1 - *y0 >= 8.0f;
    }

    // Where a text widget's words are, across: its text laid out as
    // zUIText::DoRender lays it out (setup_textbox). A hint's box is the row it
    // may use, and the boxes of the hints of one row overlap; the words do not.
    bool TextSpan(const zUI* ui, const char* text, F32* x0, F32* x1)
    {
        const zUITextAsset* a = (const zUITextAsset*)ui->asset;

        xtextbox tb = xtextbox::create();
        tb.font.id = a->font;
        tb.font.width = (1.0f / 640) * a->fontSizeW;
        tb.font.height = (1.0f / 480) * a->fontSizeH;
        tb.font.space = (1.0f / 640) * a->fontSpacingX - tb.font.width;
        tb.font.clip.assign((1.0f / 640) * ui->current.x, (1.0f / 480) * ui->current.y,
                            (1.0f / 640) * ui->current.width, (1.0f / 480) * ui->current.height);
        tb.bounds = tb.font.clip;
        tb.bounds.contract((1.0f / 640) * a->textBoxInsetLeft, (1.0f / 480) * a->textBoxInsetTop,
                           (1.0f / 640) * a->textBoxInsetRight,
                           (1.0f / 480) * a->textBoxInsetBottom);
        // xtextbox flags: 0x1 = right, 0x2 = center.
        tb.flags = (a->justifyX == 0) ? 0 : ((a->justifyX == 2) ? 0x1 : 0x2);
        tb.set_text(text);

        const xtextbox::layout& l = tb.temp_layout(false);

        // xtextbox::layout::render's placement of each line.
        F32 lo = 1e9f;
        F32 hi = -1e9f;
        for (size_t i = 0; i < l._lines_size; i++)
        {
            const xtextbox::jot_line& line = l._lines[i];
            if (line.bounds.w <= 0.0f)
            {
                continue;
            }

            F32 x = tb.bounds.x + line.bounds.x;
            if (tb.flags & 0x2)
            {
                x += 0.5f * (tb.bounds.w - line.bounds.w);
            }
            else if (tb.flags & 0x1)
            {
                x += tb.bounds.w - line.bounds.w;
            }
            lo = x < lo ? x : lo;
            hi = x + line.bounds.w > hi ? x + line.bounds.w : hi;
        }
        if (hi < lo)
        {
            return false;
        }

        // A little slack round the words, so a short word is not a small
        // target.
        const F32 kSlack = 6.0f;
        *x0 = lo * 640.0f - kSlack;
        *x1 = hi * 640.0f + kSlack;
        return true;
    }

    // Under the pointer, and how far from the middle of the widget, for
    // widgets that overlap. -1 when not under it. `words`: a text widget is
    // as wide as its words rather than its box.
    F32 Distance(const zUI* ui, bool words)
    {
        F32 x0;
        F32 y0;
        F32 x1;
        F32 y1;
        if (!BoxRect(ui, &x0, &y0, &x1, &y1))
        {
            return -1.0f;
        }
        if (sY < y0 || sY >= y1 || sX < x0 || sX >= x1)
        {
            return -1.0f;
        }

        if (words)
        {
            const char* text = TextOf(ui);
            F32 t0;
            F32 t1;
            if (text == NULL || !TextSpan(ui, text, &t0, &t1))
            {
                return -1.0f;
            }
            x0 = t0 > x0 ? t0 : x0;
            x1 = t1 < x1 ? t1 : x1;
            if (sX < x0 || sX >= x1)
            {
                return -1.0f;
            }
        }

        F32 dx = sX - 0.5f * (x0 + x1);
        F32 dy = sY - 0.5f * (y0 + y1);
        return dx * dx + dy * dy;
    }

    // Every widget the highlight can be walked to with pad presses, outward
    // from the widgets that hear the pad now.
    struct Step
    {
        S32 node; // index into sAll
        S32 from; // index of the step it was reached from, -1 at a start
        U32 event; // the press, heard by `from`'s widget, that selects this one
    };

    Step* sSteps; // kMaxWidgets of them
    S32 sStepCount;

    S32 FindStep(S32 node)
    {
        for (S32 i = 0; i < sStepCount; i++)
        {
            if (sSteps[i].node == node)
            {
                return i;
            }
        }
        return -1;
    }

    void AddStep(S32 node, S32 from, U32 event)
    {
        if (node < 0 || sStepCount == kMaxWidgets || FindStep(node) >= 0)
        {
            return;
        }
        sSteps[sStepCount].node = node;
        sSteps[sStepCount].from = from;
        sSteps[sStepCount].event = event;
        sStepCount++;
    }

    void Walk()
    {
        sStepCount = 0;
        if (sSteps == NULL && (sSteps = (Step*)malloc(kMaxWidgets * sizeof(Step))) == NULL)
        {
            return;
        }
        for (S32 i = 0; i < sCount; i++)
        {
            if (Hears(sAll[i]))
            {
                AddStep(i, -1, 0);
            }
        }

        for (S32 at = 0; at < sStepCount; at++)
        {
            const zUI* ui = sAll[sSteps[at].node];
            for (S32 l = 0; l < ui->linkCount; l++)
            {
                const xLinkAsset& link = ui->link[l];
                if (!IsMove(link.srcEvent))
                {
                    continue;
                }

                S32 to = Find(link.dstAssetID);
                if (to >= 0)
                {
                    if (IsSelect(link.dstEvent) && to != sSteps[at].node)
                    {
                        AddStep(to, at, link.srcEvent);
                    }
                    continue;
                }

                // Through one object that is not a widget -- a conditional, a
                // counter, a group's script: whatever it selects in turn is
                // where the press may lead. Follow checks that it did.
                const xBase* via = zSceneFindObject(link.dstAssetID);
                if (via == NULL)
                {
                    continue;
                }
                for (S32 v = 0; v < via->linkCount; v++)
                {
                    if (IsSelect(via->link[v].dstEvent))
                    {
                        S32 end = Find(via->link[v].dstAssetID);
                        if (end != sSteps[at].node)
                        {
                            AddStep(end, at, link.srcEvent);
                        }
                    }
                }
            }
        }
    }

    // One pad press as zUIMgrUpdate delivers it: to every widget that hears
    // the pad, with focus as it stood before the press.
    void Press(U32 button)
    {
        static zUI** hear = NULL;
        if (hear == NULL && (hear = (zUI**)malloc(kMaxWidgets * sizeof(zUI*))) == NULL)
        {
            return;
        }
        S32 n = 0;
        for (S32 i = 0; i < sCount; i++)
        {
            sAll[i]->lastFocus = sAll[i]->focus;
            if (Hears(sAll[i]))
            {
                hear[n++] = sAll[i];
            }
        }
        for (S32 i = 0; i < n; i++)
        {
            hear[i]->DoUpdate(0.0f, button);
        }
    }

    // Plays the presses that walk the highlight to step `at`, first one first.
    void Follow(S32 at)
    {
        S32 path[64];
        S32 n = 0;
        while (at >= 0 && sSteps[at].from >= 0 && n < 64)
        {
            path[n++] = at;
            at = sSteps[at].from;
        }
        if (at >= 0 && sSteps[at].from >= 0)
        {
            return; // further than any menu is long
        }

        for (S32 i = n - 1; i >= 0; i--)
        {
            Press(MoveButton(sSteps[path[i]].event));

            // A press that did not move the highlight where the links said
            // (a locked item, a condition): stop, and look again next frame.
            if (!sAll[sSteps[path[i]].node]->focus)
            {
                break;
            }
        }
    }

    // Whether the widget is an item of a list: the highlight moves to or from
    // it by pad presses.
    bool IsItem(S32 step)
    {
        return sSteps[step].from >= 0 || HasMoveLink(sAll[sSteps[step].node]);
    }

    // The item under the pointer that a pad could select, and its step.
    //
    // A widget that hears the pad but is no item of a list -- a prompt's
    // question, with the accept link on it -- counts only on a screen with no
    // list at all ("Press Start"), where it is the one thing to click.
    S32 Pick(S32* step)
    {
        bool list = false;
        for (S32 at = 0; at < sStepCount && !list; at++)
        {
            list = IsItem(at) && Shown(sAll[sSteps[at].node]);
        }

        S32 best = -1;
        F32 bestDistance = 0.0f;
        *step = -1;

        for (S32 at = 0; at < sStepCount; at++)
        {
            const zUI* ui = sAll[sSteps[at].node];
            if (!Shown(ui) || !xBaseIsEnabled(ui))
            {
                continue;
            }
            if (list ? !IsItem(at) :
                       !(HasLink(ui, eEventPadPressX) || HasLink(ui, eEventPadPressStart)))
            {
                continue;
            }

            F32 d = Distance(ui, false);
            if (d < 0.0f || (best >= 0 && d >= bestDistance))
            {
                continue;
            }

            best = sSteps[at].node;
            bestDistance = d;
            *step = at;
        }
        return best;
    }

    // The events that only change a widget's state. A pad press that sends a
    // widget anything else is showing the press on it (PickPress).
    bool IsState(U32 e)
    {
        switch (e)
        {
        case eEventEnable:
        case eEventDisable:
        case eEventVisible:
        case eEventInvisible:
        case eEventUISelect:
        case eEventUIUnselect:
        case eEventUIFocusOn:
        case eEventUIFocusOff:
        case eEventUIFocusOn_Select:
        case eEventUIFocusOff_Unselect:
        case eEventUIVisible_FocusOn_Select:
        case eEventUIFocusOff_Unselect_Invisible:
        case eEventUIFlipVisibility:
        case eEventUIReset:
            return true;
        default:
            return false;
        }
    }

    // An arrow: a small widget that a direction press lights up (the pause
    // screen's level arrows glow on up and down). Clicking it is that press.
    // The button, or 0 when the pointer is on no such widget.
    U32 PickPress()
    {
        for (S32 i = 0; i < sCount; i++)
        {
            const zUI* ui = sAll[i];
            if (!Hears(ui))
            {
                continue;
            }

            for (S32 l = 0; l < ui->linkCount; l++)
            {
                const xLinkAsset& link = ui->link[l];
                if (!IsMove(link.srcEvent) || IsState(link.dstEvent) || link.dstAssetID == ui->id)
                {
                    continue;
                }

                S32 to = Find(link.dstAssetID);
                F32 x0;
                F32 y0;
                F32 x1;
                F32 y1;
                if (to < 0 || !BoxRect(sAll[to], &x0, &y0, &x1, &y1) || x1 - x0 > 120.0f ||
                    y1 - y0 > 120.0f)
                {
                    continue;
                }

                const F32 kSlack = 10.0f;
                if (sX >= x0 - kSlack && sX < x1 + kSlack && sY >= y0 - kSlack && sY < y1 + kSlack)
                {
                    return MoveButton(link.srcEvent);
                }
            }
        }
        return 0;
    }

    // The button a hint names, for hints like "{i:button_picture_03} Return"
    // that lead with a button's picture: the bit the picture is of, or 0 for
    // text that is anything else. The picture is another text, whose own
    // {tex:pad_button3} names the texture, which is the same on both discs
    // where the pictures' numbers are not.
    U32 HintButton(const char* s)
    {
        if (s == NULL)
        {
            return 0;
        }

        while (*s == ' ')
        {
            s++;
        }

        for (S32 depth = 0; depth < 4 && *s == '{'; depth++)
        {
            const char* close = strchr(s, '}');
            if (close == NULL)
            {
                return 0;
            }

            const char* name = NULL;
            if (strncmp(s, "{i:", 3) == 0)
            {
                name = s + 3;
            }
            else if (strncmp(s, "{insert:", 8) == 0)
            {
                name = s + 8;
            }
            else if (strncmp(s, "{tex:", 5) == 0)
            {
                char texture[48];
                size_t n = strcspn(s + 5, ";}");
                if (n >= sizeof(texture))
                {
                    return 0;
                }
                memcpy(texture, s + 5, n);
                texture[n] = '\0';
                return iPadPromptsTextureBits(texture);
            }

            if (name == NULL)
            {
                // A formatting tag before the picture.
                s = close + 1;
                continue;
            }

            char insert[64];
            size_t n = (size_t)(close - name);
            if (n >= sizeof(insert))
            {
                return 0;
            }
            memcpy(insert, name, n);
            insert[n] = '\0';

            s = xTextFindString(insert, NULL);
            if (s == NULL)
            {
                return 0;
            }
        }

        return 0;
    }

    // The hint under the pointer and the button it names, or -1.
    S32 PickHint(U32* mask)
    {
        for (S32 i = 0; i < sCount; i++)
        {
            const zUI* ui = sAll[i];
            if (!Shown(ui) || !xBaseIsEnabled(ui))
            {
                continue;
            }

            U32 bit = HintButton(TextOf(ui));
            if (bit != 0 && Distance(ui, true) >= 0.0f)
            {
                *mask = bit;
                return i;
            }
        }
        return -1;
    }

    // A row whose left and right change a value rather than move the
    // highlight (an on/off, a volume): the value is drawn to the right of the
    // row's label, and a click there is the press toward the side clicked --
    // the left half lowers, the right half raises. 0 when the pointer is not
    // there or no such row has the highlight.
    U32 PickAdjust()
    {
        for (S32 at = 0; at < sStepCount; at++)
        {
            if (sSteps[at].from >= 0)
            {
                continue;
            }

            const zUI* ui = sAll[sSteps[at].node];
            F32 x0;
            F32 y0;
            F32 x1;
            F32 y1;
            if (!ui->focus || !ui->selected || !Shown(ui) || !BoxRect(ui, &x0, &y0, &x1, &y1) ||
                !HasLink(ui, eEventPadPressLeft) || !HasLink(ui, eEventPadPressRight) ||
                MovesBy(ui, eEventPadPressLeft) || MovesBy(ui, eEventPadPressRight))
            {
                continue;
            }

            // From the label's right edge to as far from the screen's right
            // edge as the label is from its left.
            F32 right = 640.0f - x0;
            if (sX < x1 || sX >= right || right - x1 < 16.0f)
            {
                continue;
            }
            return sX < 0.5f * (x1 + right) ? XPAD_BUTTON_LEFT : XPAD_BUTTON_RIGHT;
        }
        return 0;
    }

    bool SceneReady(zScene* s)
    {
        static const S32 kTypes[] = {
            eBaseTypeUIText, eBaseTypeUIImage,      eBaseTypeUIModel,
            eBaseTypeUIBox,  eBaseTypeUIUserString, eBaseTypeUIController
        };
        if (s == NULL)
        {
            return false;
        }
        // A scene still loading has counts before it has lists.
        for (size_t i = 0; i < sizeof(kTypes) / sizeof(kTypes[0]); i++)
        {
            if (s->baseCount[kTypes[i]] != 0 && s->baseList[kTypes[i]] == NULL)
            {
                return false;
            }
        }
        return true;
    }

    // The pad and highlight events' names, for the log.
    const char* EventName(U32 e)
    {
        static const char* const kPad[] = { "X",  "Square", "O",     "Triangle", "L1",
                                            "L2", "R1",     "R2",    "Start",    "Select",
                                            "Up", "Down",   "Right", "Left" };
        static char number[16];
        if (e >= eEventPadPressX && e <= eEventPadPressLeft)
        {
            return kPad[e - eEventPadPressX];
        }
        switch (e)
        {
        case eEventUISelect:
            return "Select";
        case eEventUIUnselect:
            return "Unselect";
        case eEventUIFocusOn:
            return "FocusOn";
        case eEventUIFocusOff:
            return "FocusOff";
        case eEventUIFocusOn_Select:
            return "FocusOn_Select";
        case eEventUIFocusOff_Unselect:
            return "FocusOff_Unselect";
        case eEventUIVisible_FocusOn_Select:
            return "Visible_FocusOn_Select";
        case eEventUIFocusOff_Unselect_Invisible:
            return "FocusOff_Unselect_Invisible";
        default:
            snprintf(number, sizeof(number), "event %u", (unsigned)e);
            return number;
        }
    }

    // The menu is gone, or its pointer is: a click it took and that is still
    // held stays out of play until it is let go (it closed the menu; it is
    // not also an attack).
    U32 Spend(U32 on)
    {
        iMouseLookSpend((sLeftTaken ? 1u : 0u) | (sRightTaken ? 2u : 0u));
        sLeftTaken = false;
        sRightTaken = false;
        return iMouseLookSpentOut(on);
    }
} // namespace

void iMenuMouseToWindow(F32 mx, F32 my, F32* wx, F32* wy)
{
    F32 s = 1.0f;
    F32 ox = 0.0f;
    F32 oy = 0.0f;
    Scale(&s, &ox, &oy);
    *wx = (mx / 640.0f * iScreenUIWidthF() + iScreenUIOriginXF()) * s + ox;
    *wy = (my / 480.0f * iScreenUIHeightF() + iScreenUIOriginYF()) * s + oy;
}

U32 iMenuMouseUpdate(S32 inMenu, U32 on)
{
    zScene* s = globals.sceneCur;
    const bool menu =
        inMenu && SceneReady(s) && zUI::bAllowInput && gTrcPad[0].state == TRC_PadInserted;
    if (!iMouseLookSetPointer(menu))
    {
        sOver = NULL;
        sOverHint = NULL;
        return Spend(on);
    }

    F32 wx = 0.0f;
    F32 wy = 0.0f;
    bool moved = false;
    U32 pressed = 0;
    U32 held = 0;
    S32 wheel = 0;
    if (!iMouseLookPointer(&wx, &wy, &moved, &pressed, &held, &wheel) || !ToMenu(wx, wy, &sX, &sY))
    {
        return Spend(on);
    }

    const bool clicked = (pressed & 1u) != 0;
    U32 add = 0;

    // A right click is the back button (B), whatever the mouse button is
    // bound to, wherever the pointer is.
    if (pressed & 2u)
    {
        sRightTaken = true;
        add |= XPAD_BUTTON_TRIANGLE;
    }

    // The wheel steps through a list the way the d-pad does.
    if (wheel > 0)
    {
        add |= XPAD_BUTTON_UP;
    }
    else if (wheel < 0)
    {
        add |= XPAD_BUTTON_DOWN;
    }

    if (moved || clicked)
    {
        GatherAll(s);
        Walk();

        S32 step = -1;
        S32 over = Pick(&step);
        sOver = over >= 0 ? sAll[over] : NULL;
        sOverHint = NULL;

        // Pointing at an item gives it the highlight.
        if (over >= 0 && sSteps[step].from >= 0)
        {
            Follow(step);
        }

        U32 hint = 0;
        U32 adjust = 0;
        if (over < 0)
        {
            S32 h = PickHint(&hint);
            sOverHint = h >= 0 ? sAll[h] : NULL;
            if (h < 0)
            {
                adjust = PickPress();
            }
            if (h < 0 && adjust == 0)
            {
                adjust = PickAdjust();
            }
        }

        iMouseLookSetHand(over >= 0 || hint != 0 || adjust != 0);

        if (clicked)
        {
            if (over >= 0 && sAll[over]->focus)
            {
                // Accept, for the item that has the highlight now. Start
                // where that is the only press anything answers.
                bool accept = false;
                bool start = false;
                for (S32 i = 0; i < sCount; i++)
                {
                    if (Hears(sAll[i]))
                    {
                        accept = accept || HasLink(sAll[i], eEventPadPressX);
                        start = start || HasLink(sAll[i], eEventPadPressStart);
                    }
                }
                add |= (!accept && start) ? XPAD_BUTTON_START : XPAD_BUTTON_X;
                sLeftTaken = true;
            }
            else if (hint != 0)
            {
                // A hint is the button whose picture it shows.
                add |= hint;
                sLeftTaken = true;
            }
            else if (adjust != 0)
            {
                add |= adjust;
                sLeftTaken = true;
            }
            // Anywhere else the click is the button the mouse button is bound
            // to, as the hints on screen say.
        }
    }

    if (sLeftTaken)
    {
        on &= ~iMouseLookBound(1);
        if (!(held & 1u) && !clicked)
        {
            sLeftTaken = false;
        }
    }
    if (sRightTaken)
    {
        on &= ~iMouseLookBound(2);
        if (!(held & 2u) && !(pressed & 2u))
        {
            sRightTaken = false;
        }
    }

    return on | add;
}

void iMenuMouseLog(S32 frame)
{
    zScene* s = globals.sceneCur;
    if (!SceneReady(s))
    {
        return;
    }

    GatherAll(s);
    Walk();

    S32 shown = 0;
    for (S32 i = 0; i < sCount; i++)
    {
        const zUI* ui = sAll[i];
        const char* text = TextOf(ui);
        U32 hint = Shown(ui) ? HintButton(text) : 0;
        S32 at = FindStep(i);
        if (!ui->focus && !(Shown(ui) && (ui->selected || at >= 0 || hint != 0 || text != NULL)))
        {
            continue;
        }
        shown++;

        const char* kind = "?";
        switch (ui->baseType)
        {
        case eBaseTypeUIText:
            kind = "text";
            break;
        case eBaseTypeUIImage:
            kind = "image";
            break;
        case eBaseTypeUIModel:
            kind = "model";
            break;
        case eBaseTypeUIBox:
            kind = "box";
            break;
        case eBaseTypeUIUserString:
            kind = "string";
            break;
        case eBaseTypeUIController:
            kind = "ctrl";
            break;
        }

        char brief[48];
        snprintf(brief, sizeof(brief), "%s", text != NULL ? text : "");
        for (char* c = brief; *c != '\0'; c++)
        {
            if (*c == '\n' || *c == '\r')
            {
                *c = ' ';
            }
        }

        F32 t0 = 0.0f;
        F32 t1 = 0.0f;
        if (hint != 0)
        {
            TextSpan(ui, text, &t0, &t1);
        }

        const char* name = xSTAssetName(ui->id);
        printf("tssm: test: frame %d: ui %s %08X '%s' rect %.0f %.0f %.0f %.0f%s%s%s%s flags %X "
               "a %d",
               (int)frame, kind, (unsigned)ui->id, name != NULL ? name : "", (double)ui->current.x,
               (double)ui->current.y, (double)ui->current.width, (double)ui->current.height,
               ui->visible ? " vis" : "", ui->focus ? " focus" : "", ui->selected ? " sel" : "",
               xBaseIsEnabled(ui) ? "" : " disabled", (unsigned)ui->asset->flags,
               (int)ui->current.color.a);
        if (at >= 0)
        {
            printf(" step %d%s", (int)sSteps[at].from, IsItem(at) ? " item" : "");
        }
        if (hint != 0)
        {
            printf(" hint %X words %.0f-%.0f", (unsigned)hint, (double)t0, (double)t1);
        }
        printf(" \"%s\"%s\n", brief,
               ui == sOver ? " <- pointer" : (ui == sOverHint ? " <- pointer (hint)" : ""));

        if (at < 0)
        {
            continue;
        }
        for (S32 l = 0; l < ui->linkCount; l++)
        {
            const xLinkAsset& link = ui->link[l];
            if (link.srcEvent < eEventPadPressX || link.srcEvent > eEventPadPressLeft)
            {
                continue;
            }
            const char* to = xSTAssetName(link.dstAssetID);
            printf("tssm: test: frame %d:     %s -> %08X '%s' ", (int)frame,
                   EventName(link.srcEvent), (unsigned)link.dstAssetID, to != NULL ? to : "");
            printf("%s\n", EventName(link.dstEvent));
        }
    }
    printf("tssm: test: frame %d: ui %d widgets, %d listed, %d hear or are reachable, pointer "
           "%.0f %.0f\n",
           (int)frame, (int)sCount, (int)shown, (int)sStepCount, (double)sX, (double)sY);
    fflush(stdout);
}
