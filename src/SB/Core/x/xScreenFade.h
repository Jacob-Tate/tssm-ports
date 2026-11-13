#ifndef XSCREENFADE_H
#define XSCREENFADE_H

#include "xBase.h"
#include "xDynAsset.h"
#include "xColor.h"
#include "zGlobals.h"
#include "zScene.h"
#include "iScrFX.h"

#include <rwcore.h>

enum eScreenFadeState
{
    eScreenFadeStateOff,
    eScreenFadeStateFadeDown,
    eScreenFadeStateWait,
    eScreenFadeStateFadeUp,
    eScreenFadeStateMax
};

struct xScreenFade : xBase
{
    struct asset_type : xDynAsset
    {
        xColor_tag dest;
        F32 fadeDownTime;
        F32 waitTime;
        F32 fadeUpTime;
    };

    asset_type* asset;
    eScreenFadeState state;
    xColor_tag src;
    xColor_tag current_color;
    F32 time_passed;
    xBase* owner;

    static void cb_dispatch(xBase* from, xBase* to, U32 event, const F32*, xBase*, U32);
    static void load(xBase& data, xDynAsset& asset, size_t asset_size);
    void update(F32 seconds);

    void load(asset_type& a);
    static U8 InterpCol(F32 t, U8 s, U8 d);
    void StartFade();
    void reset();

    void render()
    {
        if (state == eScreenFadeStateOff)
        {
            return;
        }

        RwVideoMode video_mode;
        RwEngineGetVideoModeInfo(&video_mode, RwEngineGetCurrentVideoMode());
        iScrFxDrawBox(0.0f, 0.0f, video_mode.width, video_mode.height, current_color.r,
                      current_color.g, current_color.b, current_color.a, true);
    }

    static void update_all(F32 dt)
    {
        zScene& scene = *xglobals->sceneCur;
        xScreenFade* it = (xScreenFade*)scene.baseList[eBaseTypeScreenFade];
        xScreenFade* end = it + scene.baseCount[eBaseTypeScreenFade];

        while (it != end)
        {
            it->update(dt);
            it++;
        }
    }

    static void render_all()
    {
        zScene& scene = *xglobals->sceneCur;
        xScreenFade* it = (xScreenFade*)scene.baseList[eBaseTypeScreenFade];
        xScreenFade* end = it + scene.baseCount[eBaseTypeScreenFade];

        while (it != end)
        {
            it->render();
            it++;
        }
    }
};

S32 xScreenFade_FadeInProgress();

#endif
