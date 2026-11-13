#ifndef ZENTPLAYERFX_H
#define ZENTPLAYERFX_H

#include "xMath3.h"
#include "xFX.h"
#include "xModelBlur.h"
#include "iSnd.h"
#include "zDust.h"
#include "zLightning.h"

struct xEnt;
struct xSurface;
struct xModelInstance;

namespace zEntPlayerFX
{
    namespace SuperMachoBowlLightning
    {
        struct InnerLightningData
        {
            F32 theta;
            F32 u;
            zLightning* lightning;
            xVec3 randomPoint;
        };

        extern _tagLightningAdd LightningInfo;
        extern InnerLightningData Lightning[16];
        extern F32 TimeToChangeLightning;
        extern F32 TimeToFlash;
        extern S32 TargetCount;
        extern F32 TimeLeftForExplosion;
        extern bool Exploding;
        extern F32 Step;
        extern F32 Scale;

        void Init();
        void Update(F32 dt);
        void Start(xVec3& pos);
        void RemoveAll();
    } // namespace SuperMachoBowlLightning

    namespace SuperMachoBowlExplosion
    {
        extern bool Exploding;
        extern xModelInstance* ExplosionSphere;
        extern xSurface* Surface;
        extern iSndHandle ExplodeSndHandle;

        void Init();
        void Explode(xVec3& pos);

        // No symbol in either build (inlined into zEntPlayer_SceneExit); the name is a guess.
        inline void SceneExit()
        {
            Exploding = false;
            if (ExplosionSphere)
            {
                xModelInstanceFree(ExplosionSphere);
            }
            ExplosionSphere = NULL;
            Surface = NULL;
            ExplodeSndHandle = (iSndHandle)-1;
        }
    } // namespace SuperMachoBowlExplosion

    namespace GauntletExplosion
    {
        extern bool Exploding;
        extern bool Sparking;

        void Explode(xVec3& pos);
        void Sparks(xVec3& pos);
        void SetSparks(bool value);
    } // namespace GauntletExplosion

    namespace SlamWarp
    {
        extern bool WarpEmitted;

        void SetWarp(bool value);
        void Update(F32 dt);
    } // namespace SlamWarp

    namespace MotionBlur
    {
        extern xModelBlur NormalBlur;
        extern xModelBlur UpgradeBlur;
        extern bool Blurring;
        extern xEnt* EntToTrack;
        extern xModelBlur::config_data NormalBlurConfig;
        extern xModelBlur::config_data UpgradeBlurConfig;

        void Update(F32 dt);
        void Start();

        // No symbol in either build (inlined into zEntPlayerCommonInit); the name is a guess, after
        // its siblings.
        inline void Init()
        {
            Blurring = false;
            NormalBlur.create();
            UpgradeBlur.create();
        }
    } // namespace MotionBlur

    namespace CartShield
    {
        extern xModelInstance* CartShieldModelInstances[2];
        extern S32 on;
        extern S32 lastOn;
        extern S32 render;
        extern F32 modelRot[2];
        extern F32 maxAlpha[2];
        extern F32 minAlpha[2];
        extern F32 maxScale[2];
        extern F32 minScale[2];
        extern F32 rotSpeed[2];
        extern S32 isBright[2];
        extern xSurface* surface;
        extern xColor_tag modelColor[2];

        void SetVisible(S32 visible);
        void Init();
        void Reset();
        void Update(F32 dt);

        // No symbol in either build (inlined into zEntPlayer_SceneExit); the name is a guess.
        inline void SceneExit()
        {
            S32 i;

            for (i = 0; i < 2; i++)
            {
                if (CartShieldModelInstances[i])
                {
                    xModelInstanceFree(CartShieldModelInstances[i]);
                    CartShieldModelInstances[i] = NULL;
                }
            }
            on = 0;
            lastOn = 0;
            render = 0;
            surface = NULL;
        }
    } // namespace CartShield

    namespace HammerThrow
    {
        extern zDustRingEmitter DustEmitter;
        extern zDustRingEmitter::config_type DustConfig;
        extern U8 Dust;
        extern U8 Blur;
        extern F32 DustForce;
        extern xModelBlur ThrowBlur;
        extern xEnt* ThrownObject;
        extern xModelBlur::config_data ThrowBlurConfig;

        void Update(F32 dt);
        void SetDust(bool on);
        void SetBlur(bool on);
        void Reset();

        void Init();
    } // namespace HammerThrow

    namespace Ribbon
    {
        extern xFXRibbon PlayerRibbons[2][4];
        extern xFXRibbon CartRibbon[2];
        extern xFXRibbon::config_type RibbonConfigs[2][4];
        extern xFXRibbon::config_type CartConfig;
        extern xFXRibbon::curve_node PlayerRibbonCurve[2];
        extern xFXRibbon::curve_node SBKarate2Curve[2];
        extern xFXRibbon::curve_node PatMelee2Curve[2];
        extern F32 yaw;
        extern F32 roll;
        extern F32 pitch;
        extern xVec3 axis;

        // No GC symbol (always inlined); the name is a guess, after the sibling Set* functions.
        // Retail recomputes &PlayerRibbons[0][1] after set_config(), which an inline's index
        // parameters give.
        inline void SetConfig(S32 player, S32 limb)
        {
            PlayerRibbons[player][limb].set_config(&RibbonConfigs[player][limb]);
        }

        void Init();
        void EnableRibbonKarateSpin(xVec3& pos);
        void EnableRibbonMelee(xVec3& leftHand, xVec3& rightHand);
        void EnableRibbonCartWheel();
    } // namespace Ribbon

    namespace SFX
    {
        extern iSndHandle Snd[5];
        extern iSndGroupHandle SndGroup[5];

        void Init();
        void PlayFailure();
    } // namespace SFX
} // namespace zEntPlayerFX

#endif
