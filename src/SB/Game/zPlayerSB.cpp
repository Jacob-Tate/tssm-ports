#include "zPlayerSB.h"
#include "zEntPlayer.h"
#include "zEntPlayerCar.h"
#include "zGameState.h"
#include "zGlobals.h"
#include "zCam.h"
#include "zCamCar.h"
#include "zBossCam.h"
#include "zConfig.h"
#include "xSnd.h"
#include "xJaw.h"

void zPlayerSB::Init(xEntAsset* asset, S32 playerIdx)
{
    ePlayerEnum playerEnum = zConfig_getPlayerEnum(globals.sceneCur->sceneID, playerIdx);
    Init(asset);

    switch (playerEnum)
    {
    case CAR_NORMAL:
        zEntPlayer_CarInit(this, asset);
        break;
    case SPONGEPAT_0:
    case SPONGEPAT_1:
    case SPONGEPAT_2:
        zEntPlayer_SpongePatInit(this, asset);
        break;
    case SPONGEBALL_NORMAL:
        zEntPlayer_SpongeBallInit(this, asset);
        break;
    case COSTUME_SB_1:
    case COSTUME_SB_2:
    case COSTUME_SB_3:
    case COSTUME_SB_4:
    case COSTUME_SB_5:
    case COSTUME_SB_6:
    case COSTUME_SB_7:
    case COSTUME_SB_A:
    case COSTUME_SB_B:
    case COSTUME_SB_C:
    case COSTUME_SB_D:
        zEntPlayer_SpongeBobInit(this, asset,
                                 zConfig_getPlayerEnum(zConfig_getAlternateCostume(playerEnum)));
        break;
    case COSTUME_PAT_1:
    case COSTUME_PAT_2:
    case COSTUME_PAT_3:
    case COSTUME_PAT_4:
    case COSTUME_PAT_5:
    case COSTUME_PAT_6:
    case COSTUME_PAT_7:
    case COSTUME_PAT_A:
    case COSTUME_PAT_B:
    case COSTUME_PAT_C:
    case COSTUME_PAT_D:
        zEntPlayer_PatrickInit(this, asset,
                               zConfig_getPlayerEnum(zConfig_getAlternateCostume(playerEnum)) -
                                   COSTUME_PAT_1);
        break;
    }
}

void zPlayerSB::InitPlayerSpecifics(ePlayerEnum playerEnum)
{
    switch (playerEnum)
    {
    case COSTUME_SB_1:
    case COSTUME_SB_2:
    case COSTUME_SB_3:
    case COSTUME_SB_4:
    case COSTUME_SB_5:
    case COSTUME_SB_6:
    case COSTUME_SB_7:
    case COSTUME_SB_A:
    case COSTUME_SB_B:
    case COSTUME_SB_C:
    case COSTUME_SB_D:
        gCurrentPlayer = eCurrentPlayerSpongeBob;
        zEntPlayer_setBoulderMode(0);
        if (carCam && carCam->active())
        {
            zCamRemove(*carCam, true);
        }
        break;
    case COSTUME_PAT_1:
    case COSTUME_PAT_2:
    case COSTUME_PAT_3:
    case COSTUME_PAT_4:
    case COSTUME_PAT_5:
    case COSTUME_PAT_6:
    case COSTUME_PAT_7:
    case COSTUME_PAT_A:
    case COSTUME_PAT_B:
    case COSTUME_PAT_C:
    case COSTUME_PAT_D:
        gCurrentPlayer = eCurrentPlayerPatrick;
        zEntPlayer_setBoulderMode(0);
        if (carCam && carCam->active())
        {
            zCamRemove(*carCam, true);
            carCam = NULL;
        }
        break;
    case CAR_NORMAL:
        gCurrentPlayer = eCurrentPlayerCar;
        zEntPlayer_setBoulderMode(0);
        if (carCam)
        {
            zCamAdd(*carCam, true);
        }
        break;
    case SPONGEPAT_0:
    case SPONGEPAT_1:
    case SPONGEPAT_2:
        gCurrentPlayer = eCurrentPlayerSpongePat;
        zEntPlayer_setBoulderMode(0);
        if (carCam && carCam->active())
        {
            zCamRemove(*carCam, true);
        }
        break;
    case SPONGEBALL_NORMAL:
        gCurrentPlayer = eCurrentPlayerSpongeBob;
        zEntPlayer_setBoulderMode(1);
        if (carCam && carCam->active())
        {
            zCamRemove(*carCam, true);
        }
        break;
    }
}

void zPlayerSB::Init(xEntAsset* asset)
{
    if (!inited)
    {
        zPlayer::Init(asset);
        inited = 1;
        zPlayerFlags |= 0x100;

        if (gGameMode == eGameMode_Game)
        {
            zHud::hud_system::instance().add(&krabbyPattyMeter);
            zHud::hud_system::instance().add(&manlinessMeter);
            zHud::hud_system::instance().add(&treasureChest);
            zHud::hud_system::instance().add(&goofyGooberMeter);
            zHud::hud_system::instance().add(&nitroMeter);
        }
    }
}

void zPlayerSB::SceneSetup()
{
}

void zPlayerSB::Reset()
{
    trigLastFramePos.x = 1e38f;
    trigLastFramePos.y = 0.0f;
    trigLastFramePos.z = 0.0f;

    if (gGameState != eGameState_LoseChance && gGameState != eGameState_Play)
    {
        InitPlayerSpecifics((ePlayerEnum)zConfig_getPlayerEnumIdx(globals.playerTag));
    }

    zEntPlayerReset(this);
    inited = 0;
    playerTalkData.anim = -1;
    manlinessMeter.reset();
    krabbyPattyMeter.reset();
    treasureChest.reset();
    goofyGooberMeter.reset();
    nitroMeter.reset();
}

void zPlayerSB::BeginUpdate(F32 dt)
{
}

void zPlayerSB::Update(F32 dt)
{
    if (playerTalkData.anim != -1)
    {
        if (playerTalkData.time < 0.2f || xSndMgrIsPlaying(playerTalkData.sndid))
        {
            playerTalkData.time += dt;
            model->Anim->Single->BilinearLerp[0] =
                xJaw_EvalData(playerTalkData.data, playerTalkData.time);
        }
        else
        {
            SpeakStop();
        }
    }

    zEntPlayer_Update(this, globals.sceneCur, dt);
}

void zPlayerSB::EndUpdate(F32 dt)
{
}

void zPlayerSB::Move(xScene* sc, F32 dt, xEntFrame* frame)
{
}

void zPlayerSB::Exit()
{
    if (carCam)
    {
        zCamRemove(*carCam, true);
        carCam = NULL;
    }

    if (gCurrentPlayer == eCurrentPlayerCar)
    {
        zEntPlayerCarExit();
    }
}

void zPlayerSB::Render()
{
    zEntPlayer_Render(this);
}

void zPlayerSB::RenderEffects()
{
    zEntPlayer_RenderEffects(this);
}

void zPlayerSB::HandleEvent(xBase* from, U32 thisEvent, const F32* toParam, xBase* toParamWidget,
                            U32 toParamWidgetID)
{
    zEntPlayerEventCB(from, this, thisEvent, toParam, toParamWidget, toParamWidgetID);
}

void zPlayerSB::StoreCheckPoint()
{
}

void zPlayerSB::LoadCheckPoint()
{
}

void zPlayerSB::SetCamera()
{
    zCamReset(frame->mat);

    if (gCurrentPlayer == eCurrentPlayerCar)
    {
        // Retail bug: dereferences carCam exactly when it is NULL.
        if (!carCam)
        {
            carCam->create();
        }
        zCamAdd(*carCam, true);
    }

    if (gzBinaryCam)
    {
        zCamAdd(*gzBinaryCam, true);
    }
}

zHud::TreasureChest* zPlayerSB::GetTreasureChestHud()
{
    return &treasureChest;
}
