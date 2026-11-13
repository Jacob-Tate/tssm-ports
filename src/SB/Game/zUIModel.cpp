#include "zUIModel.h"

#include "iModel.h"
#include "xEnt.h"
#include "xEvent.h"
#include "xScene.h"
#include "xstransvc.h"
#include "zAnimList.h"
#include "zEnt.h"
#include "zGlobals.h"

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>

void zUIModel::DoInit()
{
    zUI::DoInit();
    if (linkCount)
    {
        link = (xLinkAsset*)(GetAsset() + 1);
    }

    InitModel();
}

void zUIModel::InitModel()
{
    // Shadows the member, as in Ratatouille (missing from the PS2 DWARF).
    const zUIModelAsset* asset = (const zUIModelAsset*)this->asset;
    RpAtomic* imodel;
    xAnimState* state;
    xSurface* surf;
    xModelInstance* minst;

    if (model)
    {
        return;
    }

    atbl = NULL;

    if (!asset->model)
    {
        model = NULL;
        return;
    }

    imodel = (RpAtomic*)xSTFindAsset(asset->model, NULL);
    if (imodel)
    {
        model = xModelInstanceAlloc(imodel, this, 0, 0, NULL);

        while (imodel = iModelFile_RWMultiAtomic(imodel))
        {
            xModelInstanceAttach(xModelInstanceAlloc(imodel, this, 0x8, 0, NULL), model);
        }

        if (asset->animationList && zAnimListGetNumUsed(asset->animationList) > 0)
        {
            atbl = zAnimListGetTable(asset->animationList);
#ifdef PLATFORM_PC
            // PLATFORM_PC: the animation callbacks read their object as an xBase/xEnt
            // (soundEffectCB reads baseType). A host compiler puts zUI's vptr at offset 0,
            // ahead of the xBase, so a plain 'this' would be read through the vptr.
            xAnimPoolAlloc(&globals.sceneCur->mempool, (xBase*)this, atbl, model);
#else
            xAnimPoolAlloc(&globals.sceneCur->mempool, this, atbl, model);
#endif

            state = xAnimTableGetState(atbl, "idle");
            if (state)
            {
                xAnimPlaySetState(model->Anim->Single, state, 0.0f);
            }
        }

        if (asset->surface)
        {
            surf = (xSurface*)xSceneResolvID(g_xSceneCur, asset->surface);
            if (surf)
            {
                surf->type = XSURFACE_TYPE_3;

                minst = model;
                while (minst)
                {
                    minst->Surf = surf;
                    minst = minst->Next;
                }
            }
        }
    }
    else
    {
        model = NULL;
    }
}

void zUIModel::DoReset()
{
    zUI::DoReset();
}

void zUIModel::DoRender() const
{
    static xVec3 from = { 0.0f, 0.0f, 1.0f };
    static xVec3 to = { 0.0f, 0.0f, 0.0f };

    if (!model)
    {
        return;
    }

    basic_rect<F32> r = { (1.0f / 640) * current.x, (1.0f / 480) * current.y,
                          (1.0f / 640) * current.width, (1.0f / 480) * current.height };

    if (r.w <= 0.0f || r.h <= 0.0f)
    {
        return;
    }

    iModelSetMaterialAlpha(model->Data, current.color.a);

    U32 srcblend = (model->Pipe.Flags >> 8) & 0xf;
    U32 destblend = (model->Pipe.Flags >> 12) & 0xf;

    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)(srcblend ? srcblend : rwBLENDSRCALPHA));
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)(destblend ? destblend : rwBLENDINVSRCALPHA));

    if ((model->Pipe.Flags & 0x30000) == 0x10000)
    {
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    }
    else
    {
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    }

    xEntSetupPipeline(model);

    model->Scale.assign(1.0f, 1.0f, 1.0f);
    xModelRender2D(*model, r, from, to);
    xEntRestorePipeline(model);

    if ((model->Pipe.Flags & 0x30000) == 0x10000)
    {
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    }

    iModelSetMaterialAlpha(model->Data, 0xff);
}

void zUIModel::DoUpdate(F32 dt, U32 buttonsPressed)
{
    zUI::DoUpdate(dt, buttonsPressed);

    if (model)
    {
        xModelUpdate(model, dt);
        xModelEval(model);
    }
}

void zUIModel::DoExit()
{
    if (model)
    {
        xModelInstanceFree(model);
        model = NULL;
        surf = NULL;
    }
}

void zUIModel::DoHandleEvent(xBase* from, U32 toEvent, const F32* toParam, xBase* toParamWidget,
                             U32 toParamWidgetID)
{
    switch (toEvent)
    {
    case eEventAnimPlay:
    case eEventAnimPlayLoop:
    case eEventAnimStop:
    case eEventAnimPause:
    case eEventAnimResume:
    case eEventAnimTogglePause:
    case eEventAnimPlayRandom:
    case eEventAnimPlayMaybe:
        zEntAnimEventCore(model, atbl, toEvent, toParam, NULL);
        break;
    case eEventUIResetMotion:
        zUI::DoHandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);

        if (model->Anim && model->Anim->Single->State)
        {
            xAnimPlaySetState(model->Anim->Single, model->Anim->Single->State, 0.0f);
        }
        break;
    default:
        zUI::DoHandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
        break;
    }
}

void zUIModel_Init(xBase& data, xDynAsset& asset, size_t)
{
    zUIModel_Init((zUIModel*)&data, (zUIModelAsset*)&asset);
}

void zUIModel_Init(zUIModel* ui, zUIModelAsset* asset)
{
    new (ui) zUIModel;
    zUI_Init(ui, asset);
}
