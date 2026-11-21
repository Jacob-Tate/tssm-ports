// The HUD at the screen's real edges. What moves, what stays and why is in
// iHudAnchor.h.

#include "iHudAnchor.h"

#include "iScreen.h"
#include "iTime.h"
#include "xDebug.h"
#include "xHud.h"
#include "xString.h"
#include "xstransvc.h"
#include "zGame.h"

#include <rwcore.h>
#include <stdio.h>

namespace
{
    struct Group
    {
        const char* name;
        S8 sideX;
        S8 sideY;
        U32 id;
    };

    // mnui.HIP's HUD widgets ("MANILESS" is the files' spelling).
    Group sGroups[] = {
        { "HUD_UNIT_METER_HEALTH", -1, -1, 0 },
        { "HUD_MODEL_MANILESS_POINTS", 1, -1, 0 },
        { "HUD_MODEL_MANILESS_POINTS_NOTCH", 1, -1, 0 },
        { "HUD_FONT_MANLINESS_POINTS", 1, -1, 0 },
        { "HUD_MODEL_UPGRADE_ICON", 1, -1, 0 },
        { "HUD_TEXT_MANLINESS_UPGRADE", 1, -1, 0 },
        { "HUD_TEXT_COMBO_POINTS", 1, -1, 0 },
        { "HUD_TEXT_COMBO_TEXT", 1, -1, 0 },
        { "HUD_MODEL_TREASURE_CHEST", -1, 1, 0 },
        { "HUD_FONT_TREASURE_CHEST", -1, 1, 0 },
        { "HUD_MODEL_GOOFY_GOOBER", 1, 1, 0 },
        { "HUD_FONT_GOOFY_GOOBER", 1, 1, 0 },
        { "HUD_MODEL_NITRO", 1, 1, 0 },
        { "HUD_FONT_NITRO", 1, 1, 0 },
    };
    bool sGroupsHashed;

    // A widget no group names: by where its middle was drawn, as a share of
    // the 4:3 screen.
    S32 SideOf(F32 share)
    {
        if (share < 0.4f)
        {
            return -1;
        }
        return share > 0.6f ? 1 : 0;
    }

    void SidesOf(const xhud::widget& w, S32* sideX, S32* sideY)
    {
        if (!sGroupsHashed)
        {
            for (Group& g : sGroups)
            {
                g.id = xStrHash(g.name);
            }
            sGroupsHashed = true;
        }

        for (const Group& g : sGroups)
        {
            if (g.id == w.a->id)
            {
                *sideX = g.sideX;
                *sideY = g.sideY;
                return;
            }
        }

        *sideX = SideOf((w.a->loc.x + 0.5f * w.a->size.x) / 640.0f);
        *sideY = SideOf((w.a->loc.y + 0.5f * w.a->size.y) / 480.0f);
    }

    // 1 in play, 0 while the game is paused, and a fifth of a second between.
    // Moved once a frame, so every piece of a frame's HUD gets the same share.
    F32 sPlayShare = 1.0f;
    iTime sShareTime;
    U32 sShareFrame;
    bool sShareTimed;

    F32 PlayShare()
    {
        if (!sShareTimed || sShareFrame != gFrameCount)
        {
            const iTime now = iTimeGet();
            F32 dt = sShareTimed ? iTimeDiffSec(sShareTime, now) : 0.0f;
            sShareTime = now;
            sShareFrame = gFrameCount;
            sShareTimed = true;
            if (dt > 0.1f)
            {
                dt = 0.1f;
            }

            const F32 step = 5.0f * dt;
            if (zGameIsPaused())
            {
                sPlayShare = sPlayShare > step ? sPlayShare - step : 0.0f;
            }
            else
            {
                sPlayShare = sPlayShare < 1.0f - step ? sPlayShare + step : 1.0f;
            }
        }

        // Smoothstep, so the glide starts and ends gently.
        return sPlayShare * sPlayShare * (3.0f - 2.0f * sPlayShare);
    }

    S32 sLogFrame;

    const char* const kSideX[] = { "left", "centre", "right" };
    const char* const kSideY[] = { "top", "middle", "bottom" };

    // Where a point of the 4:3 screen lands, in pixels, with the anchor in force.
    F32 PixelX(F32 x)
    {
        return iScreenUIOriginXF() + iScreenAnchorX(x) * iScreenUIWidthF();
    }

    F32 PixelY(F32 y)
    {
        return iScreenUIOriginYF() + iScreenAnchorY(y) * iScreenUIHeightF();
    }

    RwMatrix sBossMeter;
    F32 sBossMeterLogged = -1.0f;
} // namespace

void iHudAnchorLog(S32 frame)
{
    sLogFrame = frame;
}

void iHudAnchorBegin(const xhud::widget& w)
{
    const bool native = iScreenUIModeInForce() == iSCREENUI_NATIVE;
    if (!native && sLogFrame == 0)
    {
        return;
    }

    S32 sideX = 0;
    S32 sideY = 0;
    SidesOf(w, &sideX, &sideY);

    if (native)
    {
        const F32 share = PlayShare();
        iScreenSetAnchor(share * (F32)sideX, share * (F32)sideY);
    }

    if (sLogFrame != 0)
    {
        const char* name = xSTAssetName(w.a->id);
        printf("tssm: test: frame %d: hud %08X '%s' authored %.0f %.0f %.0f %.0f now %.0f %.0f "
               "%.0f %.0f %s %s at pixel %.1f %.1f\n",
               (int)sLogFrame, (unsigned)w.a->id, name != NULL ? name : "", (double)w.a->loc.x,
               (double)w.a->loc.y, (double)w.a->size.x, (double)w.a->size.y,
               (double)(640.0f * w.rc.loc.x), (double)(480.0f * w.rc.loc.y),
               (double)(640.0f * w.rc.size.x), (double)(480.0f * w.rc.size.y), kSideX[sideX + 1],
               kSideY[sideY + 1], (double)PixelX(w.rc.loc.x), (double)PixelY(w.rc.loc.y));
    }
}

void iHudAnchorBegin(F32 x, F32 y)
{
    const bool native = iScreenUIModeInForce() == iSCREENUI_NATIVE;
    if (!native && sLogFrame == 0)
    {
        return;
    }

    const S32 sideX = SideOf(x);
    const S32 sideY = SideOf(y);

    if (native)
    {
        const F32 share = PlayShare();
        iScreenSetAnchor(share * (F32)sideX, share * (F32)sideY);
    }

    if (sLogFrame != 0)
    {
        printf("tssm: test: frame %d: hud point %.3f %.3f %s %s at pixel %.1f %.1f\n",
               (int)sLogFrame, (double)x, (double)y, kSideX[sideX + 1], kSideY[sideY + 1],
               (double)PixelX(x), (double)PixelY(y));
    }
}

void iHudAnchorEnd()
{
    iScreenClearAnchor();
}

RwMatrixTag* iHudAnchorBossMeter()
{
    // The meter is bottom centre: it goes nowhere across, and down only as far
    // as a tall screen reaches below the box.
    F32 down = iScreenAnchorMarginYF();
    if (down > 0.0f)
    {
        down *= PlayShare();
    }

    if (sLogFrame != 0 && down != sBossMeterLogged)
    {
        printf("tssm: test: frame %d: hud boss meter down %.4f of the 4:3 height\n", (int)sLogFrame,
               (double)down);
        sBossMeterLogged = down;
    }

    if (down <= 0.0f)
    {
        return NULL;
    }

    // In the HUD camera the 4:3 box is 2 units high (zHud.cpp), and up is +y.
    RwMatrixSetIdentity(&sBossMeter);
    rwMatrixSetFlags(&sBossMeter, rwMATRIXTYPEORTHONORMAL);
    sBossMeter.pos.y = -2.0f * down;
    return &sBossMeter;
}
