#include "zEntPlayerSlideFX.h"

#include <string.h>

#include "zCar.h"
#include "zEntPlayer.h"
#include "zEntPlayerOOBState.h"
#include "zGlobals.h"
#include "zParEmitter.h"
#include "zSurface.h"
#include "xMath.h"
#include "xMathInlines.h"
#include "xSnd.h"
#include "xString.h"
#include "iTime.h"

static iSndGroupHandle surfaceSoundGroupID[ezDRIVINGSURF_MaxTypes];
static iSndGroupHandle surfaceSoundGroupSpongebob;
static iSndGroupHandle surfaceSoundGroupPatrick;
static zDrivingSurf sCurrentSurface;
iSndHandle slideSoundHandle = (iSndHandle)-1;
static zParEmitter* sSlideDustEmit;

static void zEntPlayerSlideFXUpdateDust(F32 dt);

void zEntPlayerSlideFXUpdate(F32 dt)
{
    if (gCurrentPlayer == eCurrentPlayerSpongePat || gCurrentPlayer == eCurrentPlayerSpongeBob ||
        gCurrentPlayer == eCurrentPlayerPatrick)
    {
        xSurface* pSurface = globals.player.floor_surf;
        zSurfaceProps* pSurfaceProps = NULL;
        if (pSurface != NULL)
        {
            pSurfaceProps = (zSurfaceProps*)pSurface->moprops;
        }

        bool sliding = (globals.player.SlideTrackSliding & 1) && globals.player.JumpState == 0 &&
                       pSurfaceProps != NULL && pSurfaceProps->asset != NULL;
        zDrivingSurf surfaceType = (zDrivingSurf)-1;
        if (sliding)
        {
            surfaceType = (zDrivingSurf)pSurfaceProps->asset->driving_surface_type;
        }

        if (globals.player.PlayerIsFrozen || oob_state::OOBState::shared.control)
        {
            surfaceType = (zDrivingSurf)-1;
        }

        if (gCurrentPlayer == eCurrentPlayerSpongePat)
        {
            switch (surfaceType)
            {
            case ezDRIVINGSURF_Default:
            case ezDRIVINGSURF_SlideCrownMetalLamp:
            case ezDRIVINGSURF_SlideCrownRubberCable:
            case ezDRIVINGSURF_SlideCrownRubberHose:
            case ezDRIVINGSURF_SlideShellTongue:
            case ezDRIVINGSURF_SlideTubWood:
                break;
            case ezDRIVINGSURF_SlideShellHardRock:
            case ezDRIVINGSURF_SlideTubHardRock:
            case ezDRIVINGSURF_SlideTubSand:
                zEntPlayerSlideFXUpdateDust(dt);
                break;
            }
        }

        if (sCurrentSurface != surfaceType)
        {
            xSndMgrStop(slideSoundHandle);
            slideSoundHandle = (iSndHandle)-1;
        }

        if (surfaceType == ezDRIVINGSURF_SlideTubWood)
        {
            static F32 nextNoiseTime;
            F32 timeNow = iTimeDiffSec(iTimeGet());

            if (sCurrentSurface != surfaceType)
            {
                nextNoiseTime = timeNow - 1.0f;
            }

            if (timeNow > nextNoiseTime)
            {
                if (globals.player.s->Health != 0 &&
                    surfaceSoundGroupID[surfaceType] != (iSndGroupHandle)-1)
                {
                    slideSoundHandle =
                        xSndMgrPlay(surfaceSoundGroupID[surfaceType], 2, NULL, NULL,
                                    xglobals->___player_ent_dont_use_directly, NULL, NULL);
                }

                F32 slowFactor = 1.0f - (globals.player.PredictCurrVel - 5.0f) / 9.0f;
                slowFactor = CLAMP(slowFactor, 0.0f, 1.0f);
                nextNoiseTime = timeNow + (0.175f * slowFactor + 0.075f);
            }
        }
        else if (surfaceType != (zDrivingSurf)-1 && sCurrentSurface != surfaceType)
        {
            iSndGroupHandle soundGroup;

            switch (gCurrentPlayer)
            {
            case eCurrentPlayerSpongePat:
                soundGroup = surfaceSoundGroupID[surfaceType];
                break;
            case eCurrentPlayerSpongeBob:
                soundGroup = surfaceSoundGroupSpongebob;
                break;
            case eCurrentPlayerPatrick:
                soundGroup = surfaceSoundGroupPatrick;
                break;
            }

            if (globals.player.s->Health != 0 && soundGroup != (iSndGroupHandle)-1)
            {
                slideSoundHandle =
                    xSndMgrPlay(soundGroup, 2, NULL, NULL,
                                xglobals->___player_ent_dont_use_directly, NULL, NULL);
            }
        }

        sCurrentSurface = surfaceType;
    }
}

// No GC symbol: inlined into zEntPlayerSlideFXUpdate
static void zEntPlayerSlideFXUpdateDust(F32 dt)
{
    zPlayerGlobals* pg = &globals.player;

    if (sSlideDustEmit != NULL)
    {
        F32 intensityFactor =
            xsqrt(SQR(pg->SlideTrackVel.x) + SQR(pg->SlideTrackVel.z)) / pg->g.SlideVelMaxEnd;
        xParEmitterCustomSettings parEmitInfo;
        memset(&parEmitInfo, 0, sizeof(xParEmitterCustomSettings));

        parEmitInfo.custom_flags = 0x308;

        F32 deathSize = 2.0f * intensityFactor;
        parEmitInfo.size_death.set(deathSize, deathSize, 1.0f, 0);

        parEmitInfo.pos = xglobals->___player_ent_dont_use_directly->frame->mat.pos;

        static F32 lastPosY = 0.0f;
        F32 velY = (parEmitInfo.pos.y - lastPosY) / dt;
        if (velY < 0.0f)
        {
            velY = 0.0f;
        }

        parEmitInfo.vel.x = 0.5f * pg->SlideTrackVel.x;
        parEmitInfo.vel.z = 0.5f * pg->SlideTrackVel.z;
        parEmitInfo.vel.y = velY + 0.5f;

        lastPosY = parEmitInfo.pos.y;

        xParEmitterEmitCustom(sSlideDustEmit, dt, &parEmitInfo);
    }
}

void zEntPlayerSlideFXInit()
{
    U32 i;

    sCurrentSurface = (zDrivingSurf)-1;
    sSlideDustEmit = zParEmitterFind("TUB_DUST_EMIT");

    for (i = 0; i < ezDRIVINGSURF_MaxTypes; i++)
    {
        surfaceSoundGroupID[i] = (iSndGroupHandle)-1;
    }

    surfaceSoundGroupID[ezDRIVINGSURF_SlideTubWood] =
        xSndMgrGetSoundGroup(xStrHash("SLIDE_TUB_WOOD_SG"));
    surfaceSoundGroupID[ezDRIVINGSURF_SlideTubSand] =
        xSndMgrGetSoundGroup(xStrHash("SLIDE_TUB_SAND_SG"));
    surfaceSoundGroupID[ezDRIVINGSURF_SlideTubHardRock] =
        xSndMgrGetSoundGroup(xStrHash("SLIDE_TUB_ROCK_SG"));
    surfaceSoundGroupID[ezDRIVINGSURF_SlideShellHardRock] =
        xSndMgrGetSoundGroup(xStrHash("SLIDE_SHELL_ROCK_SG"));
    surfaceSoundGroupID[ezDRIVINGSURF_SlideShellTongue] =
        xSndMgrGetSoundGroup(xStrHash("SLIDE_SHELL_TONGUE_SG"));
    surfaceSoundGroupID[ezDRIVINGSURF_SlideCrownRubberHose] =
        xSndMgrGetSoundGroup(xStrHash("SLIDE_CROWN_HOSE_SG"));
    surfaceSoundGroupID[ezDRIVINGSURF_SlideCrownRubberCable] =
        xSndMgrGetSoundGroup(xStrHash("SLIDE_CROWN_CABLE_SG"));
    surfaceSoundGroupID[ezDRIVINGSURF_SlideCrownMetalLamp] =
        xSndMgrGetSoundGroup(xStrHash("SLIDE_CROWN_METAL_SG"));

    surfaceSoundGroupSpongebob = xSndMgrGetSoundGroup(xStrHash("SB_tb_loop_grp"));
    surfaceSoundGroupPatrick = xSndMgrGetSoundGroup(xStrHash("Pat_slide_loop_grp"));
}
