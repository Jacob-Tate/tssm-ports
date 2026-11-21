// The Sonic Wave Guitar's screen distortion, the game's side; see iXboxDistort.h.
// The pass itself is rw/xboxdistort.cpp.

#include "iXboxDistort.h"

#include "iExtras.h"
#include "xstransvc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // xStrHash("BXCruiseBubbleDistort"): the Xbox looks it up by this number
    // (0x1afcb0), in plat.HIP.
    const U32 kSwirlMap = 0xCEE037E0;

    F32 sAmount;

    // TSSM_TEST_DISTORT.
    bool sTestRead;
    bool sTestOn;
    F32 sTestAmount;
    F32 sTestMs = -1.0f;
    bool sTestLog;

    bool sToldMissing;

    void ReadTest()
    {
        if (sTestRead)
        {
            return;
        }
        sTestRead = true;
        const char* env = getenv("TSSM_TEST_DISTORT");
        if (env == NULL || env[0] == '\0')
        {
            return;
        }
        if (strcmp(env, "log") == 0)
        {
            sTestLog = true;
            return;
        }
        float amount = 0.0f;
        float ms = -1.0f;
        const int n = sscanf(env, "%f,%f", &amount, &ms);
        if (n >= 1 && amount >= 0.0f && amount <= 1.0f)
        {
            sTestOn = true;
            sTestAmount = amount;
            sTestMs = n >= 2 && ms >= 0.0f ? ms : -1.0f;
        }
    }

    // The swirl map, or NULL when no loaded package has it (GameCube files).
    // Its sampler is the Xbox pass's: linear.
    RwTexture* SwirlMap()
    {
        RwTexture* map = (RwTexture*)xSTFindAsset(kSwirlMap, NULL);
        if (map != NULL && RwTextureGetRaster(map) != NULL)
        {
            RwTextureSetFilterMode(map, rwFILTERLINEAR);
            return map;
        }
        return NULL;
    }
} // namespace

void iXboxDistortSetAmount(F32 amount)
{
    sAmount = amount;
}

F32 iXboxDistortAmount()
{
    ReadTest();
    return sTestOn ? sTestAmount : sAmount;
}

const char* iXboxDistortStatus()
{
    const char* renderer = iXboxDistortRendererStatus();
    if (renderer != NULL)
    {
        return renderer;
    }
    if (SwirlMap() == NULL)
    {
        return "The game's files have no swirl map (BXCruiseBubbleDistort in plat.HIP): none is "
               "drawn.";
    }
    return NULL;
}

bool iXboxDistortActive()
{
    return iExtrasInForce(iEXTRAS_GUITAR_DISTORTION) == iEXTRAS_XBOX &&
           iXboxDistortStatus() == NULL;
}

void iXboxDistortRender(RwCamera* cam)
{
    // The Xbox's pass returns at once at an amount of 0 (0x1afcb0).
    const F32 amount = iXboxDistortAmount();
    if (amount <= 0.0f || cam == NULL ||
        iExtrasInForce(iEXTRAS_GUITAR_DISTORTION) != iEXTRAS_XBOX)
    {
        return;
    }

    RwTexture* map = SwirlMap();
    if (map == NULL)
    {
        if (!sToldMissing)
        {
            sToldMissing = true;
            printf("tssm: the guitar's distortion is off -- no swirl map (BXCruiseBubbleDistort) "
                   "in the loaded files\n");
            fflush(stdout);
        }
        return;
    }

    const bool drawn = iXboxDistortDraw(cam, map, amount > 1.0f ? 1.0f : amount, sTestMs);
    if (sTestLog)
    {
        printf("tssm: test: guitar distortion amount %.3f%s\n", amount,
               drawn ? "" : " (not drawn)");
        fflush(stdout);
    }
}
