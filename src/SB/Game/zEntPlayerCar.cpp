#include "zEntPlayerCar.h"

#include "zCamCar.h"
#include "zCar.h"

#include "xCamera.h"
#include "xScrFx.h"
#include "xMath3.h"
#include "zCam.h"
#include "zEntPlayer.h"
#include "zGameState.h"
#include "zGlobals.h"
#include "zMain.h"

#include <math.h>

WAD_STATIC zCar playerCar;
zCamCar* carCam;

// No symbol: auto-inlined into zEntPlayer_CarInit and dead-stripped. Defined at the top of this
// file, where retail numbers its carCamReal static. The name is a guess, the counterpart of
// zEntPlayerCarExit.
void zEntPlayerCarInit(xEnt* ent)
{
    static zCamCar carCamReal;
    carCam = &carCamReal;

    playerCar.Initialize(ent);
    carCam->create();
    carCam->set_frame(xglobals->___player_ent_dont_use_directly->frame);

    ent->eventFunc = zEntPlayerCarHandleEvents;
}

// Analog stick deflection (-128..127) to -1..1, with a dead zone of 40 either side of centre
static inline F32 AnalogToUnit(S32 v)
{
    if (v <= 0)
    {
        if (v >= -40)
        {
            return 0.0f;
        }
        if (v <= -128)
        {
            return -1.0f;
        }
        return (v + 40) / 88.0f;
    }

    if (v <= 40)
    {
        return 0.0f;
    }
    if (v >= 127)
    {
        return 1.0f;
    }
    return (v - 40) / 87.0f;
}

zCar* zEntPlayerCarGetCar()
{
    return &playerCar;
}

void zEntPlayerCarExit()
{
    playerCar.Exit();
}

U8 zEntPlayerCarUpdate(xEnt* ent, xScene* sc, F32 dt)
{
    F32 px;
    F32 pz;
    // PS2 DWARF types these uint8, but retail passes them to CarUpdate's bool params unconverted
    bool accel = false;
    bool brake = false;
    bool boost = false;
    S32 numSteps;
    F32 perfectStep;
    S32 i;

    if (!globals.player.ControlOff)
    {
        px = -globals.pad0->analog1.x;
        pz = -globals.pad0->analog1.y;

        if (globals.player.g.CheatFlyToggle &&
            ((globals.player.g.FriendlySelectMode && (globals.pad0->pressed & XPAD_BUTTON_Z) &&
              !(globals.pad0->pressed & XPAD_BUTTON_L2) && !(globals.pad0->on & XPAD_BUTTON_L2)) ||
             (!globals.player.g.FriendlySelectMode &&
              ((globals.pad0->pressed & XPAD_BUTTON_L2) || (globals.pad0->on & XPAD_BUTTON_L2)) &&
              (globals.pad0->pressed & XPAD_BUTTON_Z))))
        {
            if (globals.player.cheat_mode)
            {
                ent->collis->chk = 0x2e;
                globals.player.cheat_mode = 0;
                xCameraDoCollisions(1, 3);
                if (carCam)
                {
                    zCamAdd(*carCam, true);
                }
            }
            else
            {
                xVec3Copy(&ent->frame->vel, &g_O3);
                ent->collis->chk = 0;
                globals.player.cheat_mode = 1;
                xCameraDoCollisions(0, 3);
                if (carCam && carCam->active())
                {
                    zCamRemove(*carCam, true);
                }
            }
        }

        if (!globals.player.cheat_mode)
        {
            if (globals.pad0->on & XPAD_BUTTON_X)
            {
                accel = true;
            }
            if (globals.pad0->on & XPAD_MOVE(XPAD_BUTTON_TRIANGLE))
            {
                brake = true;
            }
            if (globals.pad0->on & XPAD_BUTTON_R1)
            {
                boost = true;
            }
            if (globals.pad0->pressed & XPAD_MOVE(XPAD_BUTTON_O))
            {
                playerCar.SoundPlayHorn();
            }
        }
    }
    else
    {
        px = 0.0f;
        pz = 0.0f;
    }

    xEntBeginUpdate(ent, sc, dt);

    if (globals.player.cheat_mode)
    {
        if (!globals.player.ControlOff && (globals.pad0->on & XPAD_BUTTON_SQUARE))
        {
            ent->frame->mat.pos.y += 10.92f * dt;
            ent->frame->mode |= 0x2;
        }
        else if (!globals.player.ControlOff && (globals.pad0->on & XPAD_MOVE(XPAD_BUTTON_TRIANGLE)))
        {
            ent->frame->mat.pos.y -= 10.92f * dt;
            ent->frame->mode |= 0x2;
        }

        xMat3x3RMulRotY(&ent->frame->mat, &ent->frame->mat, dt * (2.3561945f * AnalogToUnit(px)));
        ent->frame->mat.pos += ent->frame->mat.at * (dt * (30.0f * AnalogToUnit(pz)));
    }

    numSteps = 1;
    perfectStep = 1.0f / VBLANKS_PER_SEC;
    if (dt > 1.5f * perfectStep)
    {
        numSteps = std::ceilf(dt / perfectStep);
    }

    for (i = 0; i < numSteps; i++)
    {
        playerCar.SetMatrix(&ent->frame->mat);
        playerCar.CarUpdate(dt / numSteps, px, accel, brake, boost);
        ent->frame->mat = *playerCar.GetMatrix();

        if (globals.player.ControlOff && globals.player.ControlOnEvent)
        {
            zEntPlayerControlOn(CONTROL_OWNER_EVENT);
            globals.player.ControlOnEvent = 0;
        }
    }

    ((zPlayer*)ent)->CollideTrigger(sc);
    zEntPlayerCarUpdateHealth(ent, dt);
    xEntEndUpdate(ent, sc, dt);

    playerCar.UpdateEffects(dt);
    playerCar.UpdateAnimState(ent);
    playerCar.SoundUpdate();

    if (globals.player.DamageTimer > 0.0f)
    {
        globals.player.DamageTimer -= dt;
        if (globals.player.DamageTimer < 0.0f)
        {
            globals.player.DamageTimer = 0.0f;
        }
    }

    if (globals.player.ControlOffTimer > 0.0f)
    {
        globals.player.ControlOffTimer -= dt;
        if (globals.player.ControlOffTimer < 0.0f)
        {
            globals.player.ControlOffTimer = 0.0f;
        }
    }

    return FALSE;
}

void zEntPlayerCarRenderFX(xEnt* ent)
{
    playerCar.RenderBrakeLights(ent->model);
}

void zEntPlayerCarUpdateHealth(xEnt* ent, F32 dt)
{
    globals.player.s->Health = playerCar.GetHealth();

    if (!globals.player.ControlOff && !globals.player.s->Health)
    {
        globals.player.DamageTimer = 10.0f;
        zEntPlayerControlOff(CONTROL_OWNER_GLOBAL);
    }
    else if (!globals.player.s->Health)
    {
        if (globals.player.DamageTimer > 0.0f)
        {
            globals.player.DamageTimer -= dt;
        }

        if (globals.player.DamageTimer >= 0.3f && ent->model->Anim->Single->CurrentSpeed == 0.0f &&
            !xScrFxIsFading())
        {
            xColor_tag black = { 0x00, 0x00, 0x00, 0xFF };
            xColor_tag clear = { 0x00, 0x00, 0x00, 0x00 };
            xScrFxFade(&clear, &black, 0.15f, 1);
            globals.player.DamageTimer = 0.3f;
        }

        if (globals.player.DamageTimer <= 0.0f)
        {
            zGameStateSwitch(eGameState_LoseChance);
            globals.player.s->Health = globals.player.s->MaxHealth;
            playerCar.SetFullHealth();
        }
    }
}

void zEntPlayerCarHandleEvents(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                               xBase* toParamWidget, U32 toParamWidgetID)
{
    playerCar.HandleEvent(from, to, toEvent, toParam, toParamWidget, toParamWidgetID);
}
