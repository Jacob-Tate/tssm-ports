#include "zUIMgr.h"

#include "zGlobals.h"
#include "zRenderState.h"
#include "zUIBox.h"
#include "zUIController.h"
#include "zUIImage.h"
#include "zUIModel.h"
#include "zUIUserString.h"

#include "iDraw.h"
#ifdef PLATFORM_PC
#include "iScreen.h"
#endif
#include "xFXHighDynamicRange.h"
#include "xRenderState.h"
#include "xTRC.h"
#include "xstransvc.h"
#include "xutil.h"

namespace
{
    zUI* uiElements[1024];
    U32 uiElementCount;
    U8 uiHDREnabled;
#ifdef PLATFORM_PC
    // PC: what the menus asked zUIMgrEnableHDR for. uiHDREnabled follows it while the Xbox
    // glow is drawn (zUIMgrRender).
    U8 uiHDRRequested;
#endif
    U8 uiAnyActive = 1;
    F32 buttonTimer;
    U32 buttonsPressedQueue;
    U32 buttonsPressed;

    // Sort order for UI elements: by z, then by descending sort key.
    struct ElementCompare
    {
        S32 operator()(const void* A, const void* B) const
        {
            const zUI* a = *(const zUI* const*)A;
            const zUI* b = *(const zUI* const*)B;

            if (a->z != b->z)
            {
                return a->z > b->z ? 1 : -1;
            }

            U32 akey = a->GetSortKey();
            U32 bkey = b->GetSortKey();
            return bkey - akey;
        }
    };
} // namespace

// HDR render helpers used by zUIMgrRender and zLoadingScreen::Render. No GC symbols (always
// inlined); names from Ratatouille. Callers test ui->visible before zUIMgrRenderElementHDR.
struct zUIMgrHDRContext
{
    U8 renderedHDR;
    xFXHighDynamicRangeConfiguration oldConfiguration;
};

inline void zUIMgrSetHDR(zUIMgrHDRContext& context)
{
    context.renderedHDR = 0;
    xFXHighDynamicRangeGetConfiguration(context.oldConfiguration);

    xFXHighDynamicRangeConfiguration newConfiguration = context.oldConfiguration;
    newConfiguration.glow = 0;
    newConfiguration.darken = 0;
    newConfiguration.downsamples = 3;
    newConfiguration.overbrighten = 1.0f;
    newConfiguration.blur_faster = false;
    xFXHighDynamicRangeSetConfiguration(newConfiguration);

    xFXHighDynamicRangeStartRender();
    xRenderFixIMBegin();
}

inline void zUIMgrFinishHDR(zUIMgrHDRContext& context)
{
    if (context.renderedHDR)
    {
#ifdef PLATFORM_PC
        // PC: the Xbox's menu glow with [extras] glow = xbox, as it is otherwise.
        xFXHighDynamicRangeRenderMenu(RwCameraGetCurrentCamera());
#else
        xFXHighDynamicRangeRender(RwCameraGetCurrentCamera());
#endif
    }

    xRenderFixIMEnd();
    HDR_brightening = 0;
    iDrawSetFBMSK(0xFF000000, 0xFFFFFFFF, true);
    xFXHighDynamicRangeSetConfiguration(context.oldConfiguration);
}

inline void zUIMgrRenderElementHDR(zUIMgrHDRContext& context, zUI* ui)
{
    U8 blends;
    xColor_tag save;
    void* oldAlphaTest;
    void* oldAlphaTestValue;

    if (ui->brighten)
    {
        blends = ui->Blends();
        iDrawSetFBMSK(blends ? 0x00FFFFFF : 0, 0xFFFFFFFF, true);

        save = ui->current.color;
        ui->current.color.a = ui->current.color.a * ui->current.brightness / 255;

        RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTION, &oldAlphaTest);
        RwRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF, &oldAlphaTestValue);
        RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONALWAYS);

        ui->hdrPass = 1;
        ui->Render();
        ui->hdrPass = 0;

        ui->current.color = save;
        iDrawSetFBMSK(0xFF000000, 0xFFFFFFFF, true);
        context.renderedHDR = 1;

        if ((S32)(SPtr)oldAlphaTest >= rwALPHATESTFUNCTIONNEVER)
        {
            RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, oldAlphaTest);
        }
        else
        {
            RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION,
                             (void*)rwALPHATESTFUNCTIONGREATEREQUAL);
        }
        RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, oldAlphaTestValue);

        if (!blends)
        {
            return;
        }
    }

    ui->Render();
}

void zUIMgrInit(zScene* s)
{
    uiElementCount = 0;
    zUITextInitSystem();

    zUIText* ui1 = (zUIText*)s->baseList[eBaseTypeUIText];
    for (S32 i = 0; i < s->baseCount[eBaseTypeUIText]; i++)
    {
        uiElements[uiElementCount++] = &ui1[i];
    }

    zUIImage* ui2 = (zUIImage*)s->baseList[eBaseTypeUIImage];
    for (S32 i = 0; i < s->baseCount[eBaseTypeUIImage]; i++)
    {
        uiElements[uiElementCount++] = &ui2[i];
    }

    zUIModel* ui3 = (zUIModel*)s->baseList[eBaseTypeUIModel];
    for (S32 i = 0; i < s->baseCount[eBaseTypeUIModel]; i++)
    {
        uiElements[uiElementCount++] = &ui3[i];
    }

    zUIBox* ui4 = (zUIBox*)s->baseList[eBaseTypeUIBox];
    for (S32 i = 0; i < s->baseCount[eBaseTypeUIBox]; i++)
    {
        uiElements[uiElementCount++] = &ui4[i];
    }

    zUIUserString* ui5 = (zUIUserString*)s->baseList[eBaseTypeUIUserString];
    for (S32 i = 0; i < s->baseCount[eBaseTypeUIUserString]; i++)
    {
        uiElements[uiElementCount++] = &ui5[i];
    }

    zUIController* ui7 = (zUIController*)s->baseList[eBaseTypeUIController];
    for (S32 i = 0; i < s->baseCount[eBaseTypeUIController]; i++)
    {
        uiElements[uiElementCount++] = &ui7[i];
    }

    quicksort(uiElements, uiElementCount, sizeof(zUI*), ElementCompare());

    for (U32 i = 0; i < uiElementCount; i++)
    {
        uiElements[i]->Setup();
    }

    zUIMgrReset();
}

// No GC symbol: auto-inlined into zUIMgrInit (the linker strips the out-of-line copy). Name and
// body from Ratatouille.
void zUIMgrReset()
{
    for (U32 i = 0; i < uiElementCount; i++)
    {
        uiElements[i]->Reset();
    }

    uiAnyActive = 1;
}

void zUIMgrUpdate(F32 dt)
{
    if (globals.pad0->pressed & (XPAD_BUTTON_START | XPAD_BUTTON_Z))
    {
        uiAnyActive = 1;
    }

    buttonTimer -= dt;

    if (gTrcPad[0].state == TRC_PadInserted)
    {
        buttonsPressedQueue |= globals.pad0->pressed;
    }
    else
    {
        buttonsPressedQueue = 0;
    }

    if (!uiAnyActive)
    {
        return;
    }

    for (U32 i = 0; i < uiElementCount; i++)
    {
        uiElements[i]->lastFocus = uiElements[i]->focus;
    }

    if (buttonTimer <= 0.0f && buttonsPressedQueue)
    {
        buttonsPressed = buttonsPressedQueue;
        buttonsPressedQueue = 0;
        buttonTimer = 0.1f;
    }
    else
    {
        buttonsPressed = 0;
    }

    uiAnyActive = 0;

    for (U32 i = 0; i < uiElementCount; i++)
    {
        if (xBaseIsEnabled(uiElements[i]) &&
            (uiElements[i]->focus ||
             (uiElements[i]->visible && (uiElements[i]->current.color.a || uiElements[i]->motion))))
        {
            uiElements[i]->Update(dt, buttonsPressed);

            // Meant to keep two elements (asset names unknown) from holding the UI awake, but the
            // original tests with || so the condition is always true.
            if (uiElements[i]->id != 0x97CBE19F || uiElements[i]->id != 0x1DAA8BAE)
            {
                uiAnyActive = 1;
            }
        }
    }
}

// The visibility test reads uiElements[i] while the render calls use ui, as retail copies the
// element from a scratch register into ui's saved one.
void zUIMgrRender()
{
    zUIMgrHDRContext context;
    S32 i;
    zUI* ui;

    if (!uiAnyActive)
    {
        return;
    }

#ifdef PLATFORM_PC
    // PC: the Xbox draws the glow of the elements that brighten while the title or pause menu
    // has asked for it (zSceneSetup clears the flag at every level start); the GameCube never
    // does.
    uiHDREnabled = uiHDRRequested && xFXHighDynamicRangeMenuGlow();

    // PC: on a screen taller than 4:3 the menus are cut at the 4:3 area's top and bottom, as a
    // 4:3 television cuts them (iScreen.h).
    iScreenBoxClip boxClip;
#endif

    if (uiHDREnabled)
    {
        zUIMgrSetHDR(context);
    }

    zRenderState(SDRS_UI);

    for (i = uiElementCount - 1; i >= 0; i--)
    {
        ui = uiElements[i];
        if (uiElements[i]->visible && uiElements[i]->current.color.a)
        {
            if (uiHDREnabled)
            {
                zUIMgrRenderElementHDR(context, ui);
            }
            else
            {
                ui->Render();
            }
        }
    }

    if (uiHDREnabled)
    {
        zUIMgrFinishHDR(context);
    }
}

void zUIMgrExit(zScene* s)
{
    for (U32 i = 0; i < uiElementCount; i++)
    {
        uiElements[i]->Exit();
    }
}

void zUIMgrEnableHDR(bool enabled)
{
    // Empty in the GC build: nothing turns on uiHDREnabled.
#ifdef PLATFORM_PC
    // PC: the Xbox's (DEFAULT.XBE 0x12f180) stores the flag; the title's "MNU3 PRESS START DISP"
    // and the pause menu's "PAUSE STATE DISP" send it on both discs.
    uiHDRRequested = enabled;
#endif
}

// No GC symbol: auto-inlined into zLoadingScreen::Init (the linker strips the out-of-line
// copy). Retail's ElementCompare temporaries place it in this file.
void zUIMgrSortList(zUI** list, S32 count)
{
    quicksort(list, count, sizeof(zUI*), ElementCompare());
}
