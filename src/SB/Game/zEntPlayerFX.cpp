#include "zEntPlayerFX.h"

#include <types.h>
#include <stdio.h>
#include <string.h>

#include "iMath.h"
#include "xEnt.h"
#include "xEntBoulder.h"
#include "xEvent.h"
#include "xFX.h"
#include "xMathInlines.h"
#include "xModel.h"
#include "xModelBlur.h"
#include "xRand.h"
#include "xRumbleEmitter.h"
#include "xScreenWarp.h"
#include "xSnd.h"
#include "xString.h"
#include "xSurface.h"
#include "xstransvc.h"
#include "zDust.h"
#include "zEntPlayer.h"
#include "zExplosion.h"
#include "zGlobals.h"
#include "zLightning.h"
#include "zMusic.h"
#include "zNMECommon.h"
#include "zNMESupport.h"
#include "zScene.h"

static inline bool player_is_boulder_rolling()
{
    return boulderRollShouldStart && boulderVehicle;
}

namespace zEntPlayerFX
{
    namespace SuperMachoBowlLightning
    {
        _tagLightningAdd LightningInfo;
        InnerLightningData Lightning[16];
        F32 TimeToChangeLightning;
        F32 TimeToFlash;
        S32 TargetCount;
        F32 TimeLeftForExplosion;
        bool Exploding;
        F32 Step;
        F32 Scale;

        // No GC symbol: auto-inlined into zEntPlayerCommonInit (its only caller) and dead-stripped.
        void Init()
        {
            LightningInfo.type = 1;
            LightningInfo.flags = 0x10;
            LightningInfo.total_points = 12;
            LightningInfo.color = g_WHITE;
            LightningInfo.rand_radius = 0.1f;
            LightningInfo.arc_height = 0.1f;
            LightningInfo.rot_radius = 0.1f;
            LightningInfo.thickness = 0.5f;
            LightningInfo.setup_degrees = 65.0f;
            LightningInfo.move_degrees = 5000.0f;
            LightningInfo.mainTexture = xStrHash("lightning");
            LightningInfo.branchTexture = LightningInfo.mainTexture;
            LightningInfo.segmentsPerMeter = 24.0f;
            TimeToChangeLightning = -1.0f;
            TargetCount = 0;
            TimeLeftForExplosion = 0.0f;
            Exploding = false;
            Step = 0.125f;
        }

        // No symbol in either build (auto-inlined into BbowlTossEndCB and dead-stripped); the name
        // is a guess. The bowl position is computed before the powerup check.
        void Start(xVec3& pos)
        {
            if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_BOWL) > ePowerupLevelNormal)
            {
                Exploding = false;
                RemoveAll();

                LightningInfo.start = &pos;
                LightningInfo.end = &pos;

                for (S32 i = 0; i < 16; i++)
                {
                    Lightning[i].lightning = zLightningAdd(&LightningInfo);
                }

                TimeToChangeLightning = -1.0f;
            }
        }

        void RemoveAll()
        {
            S32 i;

            for (i = 0; i < 16; i++)
            {
                if (Lightning[i].lightning)
                {
                    _zLightningKill(Lightning[i].lightning);
                    memset(&Lightning[i], 0, sizeof(InnerLightningData));
                }
            }
        }

        // The three helpers below have no symbol in either build; their names are guesses. Each
        // loop of Update is one PS2 line and retail lays out their locals as inlined code, the idle
        // loop one level deeper (through MoveToBowlSurface).

        // Picks a new random spherical coordinate for every bolt: u = cos(polar angle) in steps of
        // Step, theta = azimuth on the half matching the sign of u.
        inline void Randomize()
        {
            S32 i = 0;

            for (F32 u = -1.0f; u < 1.0f; u += Step)
            {
                if (u > 0.0f)
                {
                    Lightning[i].theta = xrand_RandomFloatRange(0.0f, PI);
                }
                else
                {
                    Lightning[i].theta = xrand_RandomFloatRange(PI, 2.0f * PI);
                }

                Lightning[i].u = u;
                i++;
            }
        }

        // Puts the end of every live bolt on a sphere of the given radius around the bowl.
        inline void MoveEndpoints(F32 radius)
        {
            xVec3 center =
                globals.player.bubblebowl[globals.player.g.PowerUp[2] >= 2]->frame->mat.pos;

            for (S32 i = 0; i < 16; i++)
            {
                if (Lightning[i].lightning)
                {
                    // u is read once for the sqrt and z reloads Lightning[i].u, as in the PS2 build
                    // (retail fuses 1 - u*u into fnmsubs, which CW does only for a variable operand).
                    F32 u = Lightning[i].u;
                    F32 s = xsqrt(1.0f - u * u);

                    Lightning[i].randomPoint.x = radius * s * icos(Lightning[i].theta);
                    Lightning[i].randomPoint.y = radius * s * isin(Lightning[i].theta);
                    Lightning[i].randomPoint.z = radius * Lightning[i].u;
                    Lightning[i].randomPoint += center;

                    zLightningModifyEndpoints(
                        Lightning[i].lightning,
                        &globals.player.bubblebowl[globals.player.g.PowerUp[2] >= 2]->frame->mat.pos,
                        &Lightning[i].randomPoint, 0.3f);
                }
            }
        }

        // Bolts crawl just above the bowl's surface.
        inline void MoveToBowlSurface()
        {
            MoveEndpoints(globals.player.bubblebowl[globals.player.g.PowerUp[2] >= 2]->bound.sph.r +
                          0.2f);
        }

        // Defined below Update, as in the PS2 build.
        inline void UpdateFlashing(F32 dt);

        void Update(F32 dt)
        {
            if (TimeToChangeLightning < 0.0f)
            {
                Randomize();

                UpdateFlashing(dt);
                TimeToChangeLightning = 0.5f;
            }
            else
            {
                TimeToChangeLightning -= dt;

                if (!Exploding)
                {
                    TimeLeftForExplosion = 0.0f;

                    MoveToBowlSurface();

                    UpdateFlashing(dt);
                }
                else
                {
                    TimeLeftForExplosion -= dt;
                    if (TimeLeftForExplosion < 0.0f)
                    {
                        Exploding = false;
                        GauntletExplosion::Sparking = false;
                        RemoveAll();
                    }

                    MoveEndpoints(4.0f);

                    SuperMachoBowlExplosion::Exploding = Exploding;
                }
            }
        }

        // No symbol on GC (always inlined into Update)
        inline void UpdateFlashing(F32 dt)
        {
            xModelInstance* model =
                globals.player.bubblebowl[globals.player.g.PowerUp[2] >= 2]->model;

            if (TargetCount > 0)
            {
                model->RedMultiplier = 1.0f;
                model->BlueMultiplier = 0.0f;
                model->GreenMultiplier = 0.0f;

                TimeToFlash -= dt;
                if (TimeToFlash > 0.0f)
                {
                    model->RedMultiplier -= 2.5f * dt;
                    if (model->RedMultiplier <= 0.4f)
                    {
                        model->RedMultiplier = 1.0f;
                    }

                    model->BlueMultiplier = 0.0f;
                    model->GreenMultiplier = 0.0f;
                }
                else
                {
                    model->RedMultiplier = 1.0f;
                    model->GreenMultiplier = 1.0f;
                    model->BlueMultiplier = 1.0f;
                    TimeToFlash = 2.0f;
                }
            }
            else
            {
                model->RedMultiplier = 1.0f;
                model->BlueMultiplier = 1.0f;
                model->GreenMultiplier = 1.0f;
                TargetCount = 0;
                TimeToFlash = 0.0f;
            }
        }
    } // namespace SuperMachoBowlLightning

    namespace SuperMachoBowlExplosion
    {
        bool Exploding;
        xModelInstance* ExplosionSphere;
        xSurface* Surface;
        iSndHandle ExplodeSndHandle = (iSndHandle)-1;
    } // namespace SuperMachoBowlExplosion

    namespace GauntletExplosion
    {
        bool Exploding;
        bool Sparking;

        // clang-format off
        static const xFXRing FXExplodeRing[2] =
        {
            { 0x4B30F76E, 0.7f, { 0.0f, 0.0f, 0.0f }, 0.0f, 0.49540308f, 3.0f, 1.5707964f, -1.5707964f,
              0.5f, -0.25f, { 255, 255, 0, 255 }, 32, 3, 1, NULL, NULL },
            { 0x4B30F76E, 0.7f, { 0.0f, 0.0f, 0.0f }, 0.0f, 3.0f, -3.0f, 1.5707964f, -1.5707964f,
              0.5f, -0.25f, { 255, 255, 0, 255 }, 32, 3, 1, NULL, NULL }
        };
        // clang-format on

        // No symbol in either build (always inlined); the name is a guess. Both explosions spawn
        // the rings from this table. With the loop open-coded, the counter and ring pointer
        // registers swap in Explode, zEntPlayerExplodeBowl and zEntPlayer_MinimalUpdate.
        static inline void Rings(xVec3& pos)
        {
            for (U32 i = 0; i < sizeof(FXExplodeRing) / sizeof(FXExplodeRing[0]); i++)
            {
                xFXRingCreate(&pos, &FXExplodeRing[i]);
            }
        }
    } // namespace GauntletExplosion

    namespace SuperMachoBowlExplosion
    {
        // No GC symbol (auto-inlined into zEntPlayer_SpongeBobInit and dead-stripped); the name is
        // a guess. Its "incrediball_ball" string is pooled among this file's strings, so the code
        // lives here.
        void Init()
        {
            void* sbInfo;

            ExplodeSndHandle = (iSndHandle)-1;
            sbInfo = xSTFindAsset(xStrHash("incrediball_ball"), NULL);
            if (sbInfo)
            {
                ExplosionSphere = xModelInstanceAlloc((RpAtomic*)sbInfo, NULL, 0, 0, NULL);
                ExplosionSphere->Alpha = 0.3f;
                ExplosionSphere->RedMultiplier = 1.0f;
                ExplosionSphere->GreenMultiplier = 1.0f;
                ExplosionSphere->BlueMultiplier = 1.0f;
                SuperMachoBowlLightning::Scale = 4.1f;
            }
            else
            {
                ExplosionSphere = NULL;
            }
        }

        // No GC symbol: auto-inlined into zEntPlayerExplodeBowl (its only caller) and dead-stripped.
        void Explode(xVec3& pos)
        {
            GauntletExplosion::Rings(pos);

            if (ExplosionSphere)
            {
                xVec3Copy((xVec3*)&ExplosionSphere->Mat->pos, &pos);
            }

            GauntletExplosion::Sparks(pos);
            xScreenWarp::emit(pos, 4.0f, 1.0f, xScreenWarp::TYPE_SHOCKWAVE, 1.0f, 1.0f, 0);

            if (!xSndMgrIsPlaying(ExplodeSndHandle))
            {
                ExplodeSndHandle =
                    xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("SB_BOWL_EXPLODE_GRP")), 0, &pos,
                                NULL, NULL, NULL, NULL);
            }
        }
    } // namespace SuperMachoBowlExplosion

    namespace GauntletExplosion
    {
        void Explode(xVec3& pos)
        {
            xRumble::sphericalEmitter* rumble;

            if (!Exploding)
            {
                zExplosion::emit(3, pos);

                Rings(pos);

                rumble = (xRumble::sphericalEmitter*)zSceneFindObject(
                    xStrHash("RUMBLE_BUBBLE_BASH_SUPER"));
                if (rumble)
                {
                    rumble->UpdatePosition(pos);
                    zEntEvent(rumble, eEventStartRumbleEffect, globals.currentActivePad, 0.0f, 0.0f,
                              0.0f);
                }

                xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("SB_BASH2_SPLODE")), 0, &pos, NULL, NULL,
                            NULL, NULL);
                Exploding = true;
            }
        }

        void Sparks(xVec3& pos)
        {
            if (!Sparking)
            {
                zExplosion::emit(4, pos);
                Sparking = true;
            }
        }

        void SetSparks(bool value)
        {
            Sparking = value;
        }
    } // namespace GauntletExplosion

    namespace SlamWarp
    {
        bool WarpEmitted;

        void SetWarp(bool value)
        {
            WarpEmitted = value;
        }

        void Update(F32 dt)
        {
            if (!WarpEmitted)
            {
                xScreenWarp::emit(
                    *xEntGetPos(xglobals->___player_ent_dont_use_directly),
                    zEntPlayer_PowerupLevel(PLYR_ATAK_PAT_FLOP) > ePowerupLevelNormal ? 3.0f : 1.0f,
                    0.5f, xScreenWarp::TYPE_SHOCKWAVE, 1.0f, 1.0f, 0);
                WarpEmitted = true;
            }
        }
    } // namespace SlamWarp

    namespace MotionBlur
    {
        xModelBlur NormalBlur;
        xModelBlur UpgradeBlur;
        bool Blurring;
        xEnt* EntToTrack;
        xModelBlur::config_data NormalBlurConfig = { 0,    1.0f, 1.0f,   1.0f, 1.0f,
                                                     0.1f, 0.1f, 100.0f, 8 };
        xModelBlur::config_data UpgradeBlurConfig = { 0,    2.0f, 1.0f,   1.0f, 1.0f,
                                                      0.4f, 0.3f, 100.0f, 16 };

        void Update(F32 dt)
        {
            en_plyratak attackToCheck;

            if (Blurring && EntToTrack)
            {
                if (player_is_sponge_ball() || player_is_boulder_rolling())
                {
                    if (NormalBlur.activity)
                    {
                        NormalBlur.move(*xEntGetPos(EntToTrack));
                    }
                }
                else
                {
                    attackToCheck = gCurrentPlayer == eCurrentPlayerPatrick ? PLYR_ATAK_PAT_FLOP :
                                                                              PLYR_ATAK_SB_BASH;

                    if (zEntPlayer_PowerupLevel(attackToCheck) > ePowerupLevelNormal)
                    {
                        if (UpgradeBlur.activity)
                        {
                            UpgradeBlur.move(*xEntGetCenter(EntToTrack));
                        }
                    }
                    else if (NormalBlur.activity)
                    {
                        NormalBlur.move(*xEntGetCenter(EntToTrack));
                    }
                }
            }
        }

        // No symbol in either build (auto-inlined into zEntPlayer_StreakFX and dead-stripped); the
        // name is a guess. A plain function of this file, so its 0.8f is pooled here as in retail.
        void Start()
        {
            if (!Blurring)
            {
                EntToTrack = xglobals->___player_ent_dont_use_directly;
                Blurring = true;

                if (zEntPlayer_PowerupLevel(gCurrentPlayer == eCurrentPlayerPatrick ?
                                                PLYR_ATAK_PAT_FLOP :
                                                PLYR_ATAK_SB_BASH) > ePowerupLevelNormal)
                {
                    UpgradeBlur.activate(0.3f, 1.0f, UpgradeBlurConfig, EntToTrack->model);
                }
                else
                {
                    NormalBlur.activate(0.3f, 0.8f, NormalBlurConfig, EntToTrack->model);
                }
            }
        }
    } // namespace MotionBlur

    namespace CartShield
    {
        xModelInstance* CartShieldModelInstances[2] = { NULL, NULL };
        S32 on;
        S32 lastOn;
        S32 render;
        F32 modelRot[2];
        F32 maxAlpha[2];
        F32 minAlpha[2];
        F32 maxScale[2];
        F32 minScale[2];
        F32 rotSpeed[2];
        S32 isBright[2];
        xSurface* surface;
        xColor_tag modelColor[2];

        void SetVisible(S32 visible)
        {
            on = visible;
        }

        void Init()
        {
            S32 i;
            char modelName[256];
            RpAtomic* imodel;
            xModelInstance* model;

            on = 0;
            lastOn = 0;
            render = 0;

            maxAlpha[0] = 0.6f;
            minAlpha[0] = 0.0f;
            maxScale[0] = 1.0f;
            minScale[0] = 0.3f;
            rotSpeed[0] = 8.0f;
            isBright[0] = 1;

            maxAlpha[1] = 0.6f;
            minAlpha[1] = 0.0f;
            maxScale[1] = 1.05f;
            minScale[1] = 0.3f;
            rotSpeed[1] = -8.0f;
            isBright[1] = 1;

            surface = NULL;

            for (i = 0; i < 2; i++)
            {
                modelRot[i] = 0.0f;

                sprintf(modelName, "cartwheel_shield%1d", i + 1);

                imodel = (RpAtomic*)xSTFindAsset(xStrHash(modelName), NULL);
                if (imodel)
                {
                    CartShieldModelInstances[i] = xModelInstanceAlloc(imodel, NULL, 0, 0, NULL);

                    model = CartShieldModelInstances[i];
                    if (model)
                    {
                        model->LightKit =
                            xglobals->___player_ent_dont_use_directly->model->LightKit;
                        model->Pipe.Flags |= 0x400000;
                        model->Alpha = 1.0f;
                        model->RedMultiplier = 1.0f;
                        model->BlueMultiplier = 1.0f;
                        model->GreenMultiplier = 1.0f;
                        memset(&modelColor[i], 0xFF, sizeof(xColor_tag));
                        model->Scale.x = 0.0f;
                        model->Scale.y = 0.0f;
                        model->Scale.z = 0.0f;
                    }
                }
            }

            modelColor[0].a = 0xFE;

            surface = xSurfaceGetByName("UV ANIM SURF");
            if (surface)
            {
                CartShieldModelInstances[0]->Surf = surface;
            }
        }

        void Reset()
        {
            render = 0;
            memset(CartShieldModelInstances, 0, sizeof(CartShieldModelInstances));
        }

        void Update(F32 dt)
        {
            S32 i;
            xModelInstance* model;

            for (i = 0; i < 2; i++)
            {
                if (CartShieldModelInstances[i])
                {
                    modelRot[i] += dt * rotSpeed[i];
                    if (modelRot[i] > 2.0f * PI)
                    {
                        modelRot[i] -= 2.0f * PI;
                    }
                    if (modelRot[i] < 0.0f)
                    {
                        modelRot[i] += 2.0f * PI;
                    }

                    model = CartShieldModelInstances[i];

                    memcpy(model->Mat, xglobals->___player_ent_dont_use_directly->model->Mat,
                           sizeof(RwMatrix));
                    xVec3Copy((xVec3*)&model->Mat->pos,
                              xEntGetCenter(xglobals->___player_ent_dont_use_directly));

                    if (isBright[i])
                    {
                        model->Pipe.Flags |= 0x400000;
                    }
                    else
                    {
                        model->Pipe.Flags &= ~0x400000;
                    }

                    if (on != lastOn)
                    {
                        switch (on)
                        {
                        case 0:
                            break;
                        case 1:
                            render = 1;
                            break;
                        }
                    }

                    if (on)
                    {
                        model->Alpha += dt * ((maxAlpha[i] - minAlpha[i]) / 0.4f);
                        if (model->Alpha > maxAlpha[i])
                        {
                            model->Alpha = maxAlpha[i];
                        }

                        model->Scale.x += dt * ((maxScale[i] - minScale[i]) / 0.4f);
                        if (model->Scale.x > maxScale[i])
                        {
                            model->Scale.x = maxScale[i];
                        }

                        model->Scale.y = model->Scale.z = model->Scale.x;
                    }
                    else
                    {
                        model->Alpha -= dt * ((maxAlpha[i] - minAlpha[i]) / 0.2f);
                        if (model->Alpha < minAlpha[i])
                        {
                            model->Alpha = minAlpha[i];
                            render = 0;
                        }

                        model->Scale.x -= dt * ((maxScale[i] - minScale[i]) / 0.2f);
                        if (model->Scale.x < minScale[i])
                        {
                            model->Scale.x = minScale[i];
                        }

                        model->Scale.y = model->Scale.z = model->Scale.x;
                    }
                }
            }

            lastOn = on;
        }
    } // namespace CartShield

    namespace HammerThrow
    {
        zDustRingEmitter DustEmitter;
        zDustRingEmitter::config_type DustConfig;
        U8 Dust;
        U8 Blur;
        F32 DustForce;
        xModelBlur ThrowBlur;
        xEnt* ThrownObject;
        xModelBlur::config_data ThrowBlurConfig = { 0,    2.0f, 1.0f,   1.0f, 1.0f,
                                                    0.1f, 0.3f, 100.0f, 16 };

        void Update(F32 dt)
        {
            if (Dust)
            {
                DustEmitter.kickup(*xEntGetPos(xglobals->___player_ent_dont_use_directly),
                                   DustForce, dt);
            }

            if (Blur && ThrownObject)
            {
                ThrowBlur.move(*xEntGetCenter(ThrownObject));
            }
        }

        void SetDust(bool on)
        {
            if (Dust != on)
            {
                Dust = on;

                if (on)
                {
                    DustEmitter.activate();
                }
                else
                {
                    DustEmitter.deactivate();
                }
            }
        }

        void SetBlur(bool on)
        {
            if (Blur != on)
            {
                Blur = on;

                if (on)
                {
                    if (globals.player.carry.grabbed)
                    {
                        ThrownObject = globals.player.carry.grabbed;
                        ThrowBlur.activate(1.0f, 1.0f, ThrowBlurConfig, ThrownObject->model);
                    }
                }
                else
                {
                    ThrownObject = NULL;
                }
            }
        }

        void Reset()
        {
            if (Dust)
            {
                DustEmitter.deactivate();
            }

            if (Blur)
            {
                ThrowBlur.deactivate();
            }

            ThrownObject = NULL;
            Dust = false;
            Blur = false;
        }

        // No GC symbol (auto-inlined into zEntPlayerCommonInit and dead-stripped); the name is a
        // guess. A plain function, not a header inline, so its 0.75f is pooled here as in retail.
        void Init()
        {
            Dust = false;
            DustForce = 0.75f;
            DustConfig.radius_inner = 0.3f;
            DustConfig.radius_outer = 0.5f;
            DustConfig.intensity = 0.6f;
            DustConfig.alpha = 1.0f;
            DustConfig.rate_scale = 1.0f;
            DustEmitter.set_config(&DustConfig);
            ThrowBlur.create();
        }
    } // namespace HammerThrow

    namespace Ribbon
    {
        xFXRibbon PlayerRibbons[2][4];
        xFXRibbon CartRibbon[2];
        xFXRibbon::config_type RibbonConfigs[2][4];
        xFXRibbon::config_type CartConfig;

        // clang-format off
        xFXRibbon::curve_node PlayerRibbonCurve[2] =
        {
            { 0.0f, { 200, 166, 161, 160 }, 0.2f },
            { 1.0f, { 200, 166, 161, 100 }, 0.0f }
        };

        xFXRibbon::curve_node SBKarate2Curve[2] =
        {
            { 0.0f, { 255, 50, 50, 180 }, 0.5f },
            { 1.0f, { 128, 128, 128, 100 }, 0.0f }
        };

        xFXRibbon::curve_node PatMelee2Curve[2] =
        {
            { 0.0f, { 255, 50, 50, 180 }, 0.5f },
            { 1.0f, { 128, 128, 128, 100 }, 0.0f }
        };

        static const xFXRibbon::curve_node CartWheelRibbonCurve[2] =
        {
            { 0.0f, { 245, 166, 161, 255 }, 1.0f },
            { 1.0f, { 245, 166, 161, 100 }, 0.0f }
        };

        static const xFXRibbon::curve_node CartWheel2RibbonCurve[2] =
        {
            { 0.0f, { 255, 0, 0, 255 }, 2.0f },
            { 1.0f, { 0, 0, 0, 200 }, 0.0f }
        };
        // clang-format on

        F32 yaw;
        F32 roll;
        F32 pitch;
        xVec3 axis;

        // The inner switch cases are in the PS2 line table's order 1, 0, 3, 2; CW emits case blocks
        // in source order.
        void Init()
        {
            S32 i;
            S32 j;

            yaw = pitch = roll = 0.0f;
            axis = xVec3::m_UnitAxisY;

            for (i = 0; i < sizeof(PlayerRibbons) / sizeof(PlayerRibbons[0]); i++)
            {
                for (j = 0; j < 4; j++)
                {
                    PlayerRibbons[i][j].create(0);
                    xFXRibbon::load_default_config(RibbonConfigs[i][j]);
                    RibbonConfigs[i][j].blend_src = 5;
                    RibbonConfigs[i][j].blend_dst = 6;
                    RibbonConfigs[i][j].raster = NMEC_FindRWRaster("fx_streak2");
                    RibbonConfigs[i][j].pivot = 0.0f;

                    switch (i)
                    {
                    case 0:
                        switch (j)
                        {
                        case 1:
                            RibbonConfigs[i][j].life_time = 0.2f;
                            break;
                        case 0:
                            RibbonConfigs[i][j].life_time = 0.2f;
                            break;
                        case 3:
                            RibbonConfigs[i][j].life_time = 0.2f;
                            break;
                        case 2:
                            RibbonConfigs[i][j].life_time = 0.2f;
                            break;
                        }
                        break;
                    case 1:
                        switch (j)
                        {
                        case 1:
                            RibbonConfigs[i][j].life_time = 0.2f;
                            break;
                        case 0:
                            RibbonConfigs[i][j].life_time = 0.2f;
                            break;
                        case 3:
                            RibbonConfigs[i][j].life_time = 0.2f;
                            break;
                        case 2:
                            RibbonConfigs[i][j].life_time = 0.2f;
                            break;
                        }
                        break;
                    }

                    RibbonConfigs[i][j].flags |= 0x5;
                    RibbonConfigs[i][j].curve = PlayerRibbonCurve;
                    RibbonConfigs[i][j].curve_size = 2;
                    SetConfig(i, j);
                }
            }

            for (i = 0; i < sizeof(CartRibbon) / sizeof(CartRibbon[0]); i++)
            {
                CartRibbon[i].create(0);
                xFXRibbon::load_default_config(CartConfig);
                CartConfig.blend_src = 5;
                CartConfig.blend_dst = 2;
                CartConfig.raster = NMEC_FindRWRaster("fx_streak2");
                CartConfig.life_time = 0.501f;
                CartConfig.pivot = 0.5f;
                CartConfig.flags |= 0x5;
                CartConfig.curve = (xFXRibbon::curve_node*)CartWheelRibbonCurve;
                CartConfig.curve_size = 2;
                CartRibbon[i].set_config(&CartConfig);
            }
        }

        void EnableRibbonKarateSpin(xVec3& pos)
        {
            xVec3 normal;

            if (zEntPlayer_PowerupLevel(PLYR_ATAK_SB_KARATE) > ePowerupLevelNormal)
            {
                RibbonConfigs[0][1].curve = SBKarate2Curve;
                RibbonConfigs[0][1].curve_size = 2;
            }
            else
            {
                RibbonConfigs[0][1].curve = PlayerRibbonCurve;
                RibbonConfigs[0][1].curve_size = 2;
            }

            normal = *xEntGetPos(xglobals->___player_ent_dont_use_directly) - pos;
            SetConfig(0, 1);
            PlayerRibbons[0][1].insert(pos, normal, 1.0f, 1.0f, 0);
        }

        void EnableRibbonMelee(xVec3& leftHand, xVec3& rightHand)
        {
            xVec3 normal;

            if (zEntPlayer_PowerupLevel(PLYR_ATAK_PAT_BELLY) > ePowerupLevelNormal)
            {
                RibbonConfigs[1][0].curve = PatMelee2Curve;
                RibbonConfigs[1][0].curve_size = 2;
                RibbonConfigs[1][1].curve = PatMelee2Curve;
                RibbonConfigs[1][1].curve_size = 2;
            }
            else
            {
                RibbonConfigs[1][0].curve = PlayerRibbonCurve;
                RibbonConfigs[1][0].curve_size = 2;
                RibbonConfigs[1][1].curve = PlayerRibbonCurve;
                RibbonConfigs[1][1].curve_size = 2;
            }

            normal = *xEntGetPos(xglobals->___player_ent_dont_use_directly) - leftHand;
            PlayerRibbons[1][0].set_config(&RibbonConfigs[1][0]);
            PlayerRibbons[1][0].insert(leftHand, normal, 1.0f, 1.0f, 0);

            normal = *xEntGetPos(xglobals->___player_ent_dont_use_directly) - rightHand;
            PlayerRibbons[1][1].set_config(&RibbonConfigs[1][1]);
            PlayerRibbons[1][1].insert(rightHand, normal, 1.0f, 1.0f, 0);
        }

        void EnableRibbonCartWheel()
        {
            xVec3 ribbonPos1;
            xVec3 normal1;
            xVec3 ribbonPos2;
            xVec3 normal2;

            xVec3Copy(&ribbonPos1, xEntGetCenter(xglobals->___player_ent_dont_use_directly));
            xVec3Copy(&ribbonPos2, xEntGetCenter(xglobals->___player_ent_dont_use_directly));

            normal1 = ribbonPos1;
            normal2 = ribbonPos2;

            normal1.y += 0.25f + xglobals->___player_ent_dont_use_directly->bound.sph.r;
            normal2.x += 0.25f + xglobals->___player_ent_dont_use_directly->bound.sph.r;

            normal1 -= ribbonPos1;
            normal2 -= ribbonPos2;

            if (zEntPlayer_PowerupLevel(PLYR_ATAK_PAT_CARTWHEEL) > ePowerupLevelNormal)
            {
                CartConfig.curve = (xFXRibbon::curve_node*)CartWheel2RibbonCurve;
                CartConfig.curve_size = 2;
            }
            else
            {
                CartConfig.curve = (xFXRibbon::curve_node*)CartWheelRibbonCurve;
                CartConfig.curve_size = 2;
                CartRibbon[1].set_config(&CartConfig);
                CartRibbon[1].insert(ribbonPos2, normal2, 1.0f, 1.0f, 0);
            }

            CartRibbon[0].set_config(&CartConfig);
            CartRibbon[0].insert(ribbonPos1, normal1, 1.0f, 1.0f, 0);
        }
    } // namespace Ribbon

    namespace SFX
    {
        iSndHandle Snd[5];
        iSndGroupHandle SndGroup[5];

        // No GC symbol: auto-inlined into zEntPlayer_SNDInit (its only caller) and dead-stripped;
        // the name is a guess. Retail's string pool has these names among this file's strings.
        void Init()
        {
            SndGroup[0] = xSndMgrGetSoundGroup(xStrHash("FAILURE_RIFF_GRP"));
            SndGroup[1] = xSndMgrGetSoundGroup(xStrHash("GG_COLLECT_GRP"));
            SndGroup[2] = xSndMgrGetSoundGroup(xStrHash("TCH_COLLECT_GRP"));
            SndGroup[3] = xSndMgrGetSoundGroup(xStrHash("SPRINGBOARD_BOUNCE_GRP"));
            SndGroup[4] = xSndMgrGetSoundGroup(xStrHash("HEALTH COLLECT SGRP"));
        }

        void PlayFailure()
        {
            if (Snd[0] != -1 && !xSndMgrIsPlaying(SndGroup[0]))
            {
                Snd[0] = xSndMgrPlay(SndGroup[0], 0, NULL, NULL, NULL, NULL, NULL);
            }

            zMusicSetVolume(0.1f, 0.0f);
        }
    } // namespace SFX
} // namespace zEntPlayerFX
