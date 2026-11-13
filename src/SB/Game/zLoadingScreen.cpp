#include "zLoadingScreen.h"

#include "zUIBox.h"
#include "zUIController.h"
#include "zUIImage.h"
#include "zUIText.h"
#include "zUIMgr.h"
#include "zScene.h"

#include "iDraw.h"
#include "xFXHighDynamicRange.h"
#include "xRenderState.h"
#include "xstransvc.h"
#include "xutil.h"

#include <string.h>

namespace
{
    void* CustomStaticAllocator(U32 size, void* user)
    {
        ((zLoadingScreen*)user)->allocations[((zLoadingScreen*)user)->allocationCount] =
            RwMalloc(size, 0);
        return ((zLoadingScreen*)user)->allocations[((zLoadingScreen*)user)->allocationCount++];
    }

    void CustomSignalHandler(zUI* from, U32 dest, U32 event, const F32* param, xBase* paramWidget,
                             U32 paramWidgetID, void* user)
    {
        ((zLoadingScreen*)user)->SignalControl(from, dest, event, param, paramWidget, paramWidgetID);
    }
} // namespace

void zLoadingScreen::Init()
{
    allocationCount = 0;
    staticAllocator = CustomStaticAllocator;
    staticUser = this;
    controlCount = 0;

    xGroupAsset* group = (xGroupAsset*)xSTFindAsset(0x7F67BBC4, NULL); // xStrHash("LOADING SCREEN")

    for (S16 i = 0; i < group->itemCount; i++)
    {
        U32 size;
        xDynAsset* asset = (xDynAsset*)xSTFindAsset(((U32*)(group + 1))[i], &size);
        if (!asset)
        {
            continue;
        }

        void* test;
        zUI* element = NULL;

        switch (asset->type)
        {
        case 0x337BCB31: // xStrHash("ui:image")
            test = RwMalloc(sizeof(zUIImage), 0);
            element = (zUI*)test;
            memset(test, 0, sizeof(zUIImage));
            zUIImage_Init(*element, *asset, size);
            break;
        case 0x8C2D107D: // xStrHash("ui:box")
            element = (zUI*)RwMalloc(sizeof(zUIBox), 0);
            memset(element, 0, sizeof(zUIBox));
            zUIBox_Init(*element, *asset, size);
            break;
        case 0xBD7646D7: // xStrHash("ui:text")
            element = (zUI*)RwMalloc(sizeof(zUIText), 0);
            memset(element, 0, sizeof(zUIText));
            zUIText_Init(*element, *asset, size);
            break;
        case 0xE8753BAE: // xStrHash("ui:controller")
            element = (zUI*)RwMalloc(sizeof(zUIController), 0);
            memset(element, 0, sizeof(zUIController));
            zUIController_Init(*element, *asset, size);
            break;
        }

        if (element)
        {
            controls[controlCount++] = element;
            allocations[allocationCount++] = element;
        }
    }

    staticAllocator = NULL;
    staticUser = NULL;
    signalHandler = CustomSignalHandler;
    signalUser = this;

    zUIMgrSortList(controls, controlCount);

    for (S32 i = 0; i < controlCount; i++)
    {
        controls[i]->HandleEvent(controls[i], eEventVisible, NULL, NULL, 0);
        controls[i]->Signal(eEventVisible);
    }
}

void zLoadingScreen::Update(F32 dt)
{
    S32 i;

    for (i = 0; i < controlCount; i++)
    {
        if (controls[i]->visible)
        {
            controls[i]->Update(dt, 0);
        }
    }
}

void zLoadingScreen::Render()
{
    zUIMgrHDRContext context;
    S32 i;

    zUIMgrSetHDR(context);

    for (i = controlCount - 1; i >= 0; i--)
    {
        if (controls[i]->visible)
        {
            zUIMgrRenderElementHDR(context, controls[i]);
        }
    }

    zUIMgrFinishHDR(context);
}

// The PS2 DWARF lists j before i; declaring them in that order changes the generated code.
void zLoadingScreen::Exit()
{
    for (S32 i = 0; i < controlCount; i++)
    {
        controls[i]->Exit();
    }

    signalHandler = NULL;
    signalUser = NULL;

    for (S32 j = 0; j < allocationCount; j++)
    {
        RwFree(allocations[j]);
    }

    allocationCount = 0;
}
