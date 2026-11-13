#include "zNMETypeBossNeptune.h"
#include "zNMESupport.h"
#include "zCamera.h"
#include "zEntCruiseBubble.h"
#include "zEntPlayerOOBState.h"
#include "zEntSimpleObj.h"
#include "xDecal.h"
#include "zGrid.h"
#include "iCollide.h"
#include "iDraw.h"
#include "iParMgr.h"
#include "xModelBlur.h"
#include "xPtankPool.h"
#include "zRenderState.h"
#include "zScene.h"
#include "iAnimSKB.h"
#include "iModel.h"
#include "xLightKit.h"
#include "xModelBucket.h"
#include "zFXRibbonPool.h"
#include "zParPTank.h"
#include "zScript.h"
#include "xRenderState.h"
#include "xFXHighDynamicRange.h"
#include "xAntenna.h"

#include <rpskin.h>
#include <rpusrdat.h>
#include <PowerPC_EABI_Support/MSL_C/MSL_Common/stdlib.h>

// Placeholder for dead-stripped code, not recovered code: retail's .rodata keeps two unreferenced
// all-zero 0x10 templates here. The same pair recurs at the end of other files' includes, so it
// likely came from an uncalled header inline with two 16-byte locals.
static void __deadstripped_zNMETypeBossNeptune()
{
    F32 a[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    F32 b[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
}

extern void (*zSB04_BossPreRender)();

namespace Neptune
{
    static AnimDefStruct AnimDef[14] = {
        { "Idle", 0x10, 0x1 },
        { "Taunt", 0x20, 0x80001 },
        { "Outrage01", 0x10, 0x1 },
        { "Outrage02", 0x10, 0x1 },
        { "Outrage03", 0x10, 0x1 },
        { "AttackStart", 0x20, 0x0 },
        { "AttackLoop", 0x10, 0x100000 },
        { "AttackEnd", 0x20, 0x0 },
        { "TailLeft", 0x20, 0x200000 },
        { "TailRight", 0x20, 0x200000 },
        { "Damage", 0x20, 0x80000 },
        { "Stun", 0x10, 0x1 },
        { "Recover", 0x20, 0x0 },
        { "Defend", 0x10, 0x400001 },
    };

    static U32 AnimIDs[14];
    namespace Config
    {
        static const NMECfgNeptune cfg_neptune_default = {
            { 6, 6, 6 }, // stageHealth
            6.0f, // tailattack_rad
            1.5f, // tailattack_height
            0.9f, // tailattack_time
            25.0f, // tailattack_kbvel
            0.6f, // tailattack_kbtime
            0.08f, // tailattack_ribstarttime
            0.22f, // tailattack_ribstoptime
            0.1f, // tailattack_bubstarttime
            0.21f, // tailattack_bubstoptime
            20.0f, // tailattack_bubspeed
            40.0f, // tailattack_bubdensity
            1.0f, // tailattack_bubscale
            19.0f, // player_safe_dist
            6.0f, // player_safe_vel
            20.0f, // stun_duration
            6.5f, // stun_neardist
            1.0f, // stun_bucketscale
            0.3f, // defend_cruiseTime
            12.0f, // defend_cruiseDist
            0.25f, // defend_min_duration
            0.4f, // defend_spinCenter
            3.4f, // defend_scale
            0.15f, // taunt_blurstart
            2.0f, // taunt_blurend
            180.0f, // turn_rate
            1.5f, // safety_time
            0, // minion_state
            0.35f, // poke_dist
            1.0f, // outrageCam_dist
            1.0f, // outrageCam_height
            0.65f, // outrageCam_lerptime
            0.99619f, // fire_angle_dot
            { 0.6f, 0.6f, 0.6f }, // fire_target_height
            0.035f, // fire_track_factor
            5.5f, // fire_track_mindist
            0.55f, // fire_damageRadBeam
            0.35f, // fire_damageRadLightning
            { -0.59f, 4.8f, -0.15f }, // aim_center
            { -3.0f, 2.0f, 0.0f }, // aim_rest
            0.6f, // aim_anglerate
            0.12f, // aim_slerprate
            0, // aim_slerpfirst
            4.5f, // aim_tooCloseMin
            8.5f, // aim_tooCloseMax
            { { 0.005f, -0.01f, 5.976f },
              { 0.656f, -0.01f, 5.786f },
              { -0.645f, -0.01f, 5.794f } }, // projPOVTag
            { { { 2.319f, 3.733f, -0.133f }, 0 },
              { { 2.319f, 0.005f, -0.146f }, 0 },
              { { 1.913f, 3.619f, -0.133f }, 0 },
              { { 2.726f, 3.614f, -0.133f }, 0 },
              { { 0.704f, 0.347f, -2.065f }, 4 },
              { { -0.704f, 0.347f, -2.065f }, 4 },
              { { 0.0f, 0.305f, -1.846f }, 4 },
              { { 0.0f, 0.316f, -1.388f }, 4 },
              { { 0.0f, 0.159f, -1.443f }, 4 },
              { { 0.541f, 3.665f, -0.057f }, 1 },
              { { 0.358f, 4.546f, -0.177f }, 1 },
              { { -0.533f, 3.645f, -0.081f }, 1 },
              { { -0.355f, 4.532f, -0.193f }, 1 } }, // bossTagInit
            { { 0.293f, 0.71f, 0.125f },
              { 0.27f, 0.623f, -0.112f },
              { -0.27f, 0.623f, -0.112f },
              { -0.291f, 0.697f, 0.126f } }, // playerMidSectInit
            { 0.0f, 0.0f, 10.0f }, // bossStage2_safeLoc
            { 0.0f, 0.0f, 0.0f }, // scope_camtag
            { 0.0f, 0.2621f, 1.4315f }, // scope_center
            { 0.0f, 0.0f, 0.0f }, // scope_posadjust
            1.0f, // scope_scaleadjust
            0, // scope_drawtrident
            0.4f, // scope_zoomViewMin
            1.5f, // scope_zoomViewMax
            4.5f, // scope_zoomDistMin
            0.1f, // scope_zoomOutTime
            0.8f, // tridentblur_alphaStart
            0.2f, // tridentblur_alphaEnd
            0.8f, // tridentblur_colorStart
            0.3f, // tridentblur_colorEnd
            0.55f, // tridentblur_timeIncrement
            20, // fire_tipAvgNum
            0.3f, // fire_colHeight
            200.0f, // fire_beamEmitRate
            25.0f, // fire_beamSpeedMin
            25.0f, // fire_beamSpeedMax
            0.2f, // fire_beamSpreadDist
            0.25f, // fire_beamSpreadTime
            2.0f, // fire_beamFrontDist
            6.0f, // fire_beamFrontStartDist
            9.0f, // fire_beamFrontEndDist
            0.35f, // fire_beamFrontDelay
            0.8f, // fire_beamFrontTime
            150.0f, // fire_spiralEmitRate
            25.0f, // fire_spiralSpeedMin
            25.0f, // fire_spiralSpeedMax
            1.0f, // fire_spiralRadiusRate
            1.0f, // fire_spiralRadiusTarget
            5.0f, // fire_spiralRotMin
            10.0f, // fire_spiralRotMax
            0, // fire_spiralSign
            0.5f, // fire_ltTimeBeamEnd
            0.4f, // fire_ltTimeFeelerStart
            0.8f, // fire_ltTimeFeelerEnd
            0.6f, // fire_ltTimeRingStart
            3.0f, // fire_ltRingLerpRate
            0.6f, // fire_ltDamageDelay
            0.4f, // fire_ltDamageAfter
            0.3f, // pov_fadeTime
            0.07f, // pov_damageDelay
            0.3f, // pov_damageRad
            0.1f, // pov_krabsTargetTime
            0.6f, // pov_krabsTargetRad
            { -0.04f, 0.6f, 0.13f }, // pov_krabsTargetPoint
            { 0.0f, 0.922f, 0.05f }, // pov_reflectorTargetPoint
            20.0f, // pov_kbmaxvel
            5.0f, // pov_kbminvel
            0.5f, // pov_kbmaxtime
            4.0f, // pov_kbmindist
            8.0f, // pov_kbmaxdist
            1, // pov_kbalways
            0.4f, // pov_pokeDist
            0.8f, // pov_stunModelSwapTime
            0.77f, // pov_stunAimLerp
            93.0f, // pov_stunTridentRot
            4.8f, // pov_stunAnimTranTime
            0.5f, // pov_missFlyStopDelay
            { 0.75f, 1.9f, 2.72f, 3.2f, 3.85f }, // pov_missFlyStopTime
            { 0.5f, 1.3f, 2.1f, 2.9f, 3.7f }, // pov_missFlySoundTime
            1.5f, // pov_glowStart
            0.15f, // pov_glowMax
            0.04f, // pov_glowMin
            0.075f, // pov_glowFadeTime
            0.3f, // snd_lightningVol
            0.4f, // snd_lightningFade
            0.75f, // snd_hoseVol
            0.5f, // snd_hoseFadeDown
            0.5f, // snd_hoseFadeUp
            0.7f, // snd_hoseCenterBias
            1.0f, // snd_povAttackVol
            0.8f, // snd_povReflectVol
            1.5f, // snd_povChargeTime
            0.75f, // snd_povChargeVol
            0.8f, // snd_povBeamStunVol
            1.0f, // snd_povBucketStunVol
            3.7f, // snd_povBucketStunFadeStart
            4.4f, // snd_povBucketStunFadeEnd
            0.65f, // snd_ringVol
            3.0f, // snd_ringFade
            5.0f, // snd_ringEarDist
            2.0f, // snd_ringDopplerPitchMax
            -2.0f, // snd_ringDopplerPitchMin
            5.0f, // snd_ringDopplerDist
            { { 0.7f, 1.05f, 0.2f, 10, 1.25f, 5.5f, 3, 0, 0.0f, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f },
              { 0.7f, 1.05f, 0.2f, 10, 1.25f, 12.0f, 2, 3, 20.0f, 60.0f, 1.0f, 6, 1.75f, 0.5f,
                20.0f, 6.0f, 1.0f, 6.0f },
              { 0.7f, 1.05f, 0.2f, 10, 1.25f, 14.0f, 2, 5, 2.0f, 180.0f, 1.0f, 10, 1.2f, 0.5f,
                20.0f, 6.0f, 1.0f, 6.0f } }, // stageAtk
            { 0.0f, 20.0f, 80.0f, 135.0f, 180.0f, 220.0f, 240.0f, 240.0f }, // tailleft_angles
            { 0.0f, 45.0f, 90.0f, 135.0f, 180.0f, 220.0f, 240.0f, 240.0f }, // tailright_angles
        };

        static const zBossCam_Binary::config cfg_bosscam = {
            { 6.0f, 3.0f, 2.0f },
            { 0.2f, 2.2f, -1.0f },
            { 1.0f, 0.2f, 1.5f },
            10.0f,
            10.0f,
            10.0f,
            10.0f,
            50.0f,
            -0.5235988f,
        };

        static const zBossCam_Binary::config cfg_thirdstage_bosscam = {
            { 7.0f, 3.3f, 1.5f },
            { 0.2f, 2.2f, -1.0f },
            { 1.0f, 0.2f, 1.5f },
            10.0f,
            10.0f,
            10.0f,
            10.0f,
            50.0f,
            -0.5235988f,
        };

        static const zCamNeptune::config cfg_povcam = {
            { 0.0f, 5.7f, 0.0f },
            1.0f,
            50.0f,
        };
    } // namespace Config

    namespace Ribbon
    {
        static const xFXRibbon::curve_node curvedata[4] = {
            { 0.0f, { 0x80, 0x80, 0x80, 0xFF }, 1.0f },
            { 0.2f, { 0x80, 0x80, 0x00, 0xC8 }, 1.5f },
            { 0.6f, { 0x00, 0x80, 0x80, 0x96 }, 0.4f },
            { 1.0f, { 0x80, 0x00, 0x80, 0x64 }, 0.0f },
        };

        NMEHelpRib::RibData rib_tailswipe = {
            { 1, 1, 0, 0 },
            { "particle_cloud", (xFXRibbon::curve_node*)curvedata, 4, 1.24f, 0.5f },
            { 1, 1.0f, 1.0f },
            { NULL, 0 },
        };
    } // namespace Ribbon

    namespace Par
    {
        zNeptuneParTex Tex[3] = {
            { 1.0f, 0.0f, 0.0f, 64.0f },
            { 1.0f, 64.0f, 0.0f, 64.0f },
            { 1.0f, 128.0f, 0.0f, 64.0f },
        };

        zNeptuneParCol Col[7] = {
            { 0.0f, 0xff, 0xff, 0xff, 0xff, 0.0f, 0.0f },
            { 1.0f, 0x80, 0x80, 0x80, 0x00, 0.0f, 0.0f },
            { 0.0f, 0xff, 0xff, 0xff, 0xff, 0.0f, 0.0f },
            { 1.0f, 0x00, 0x00, 0x00, 0x00, 0.0f, 0.0f },
            { 0.0f, 0x00, 0x00, 0x00, 0xff, 0.0f, 0.0f },
            { 0.8f, 0x20, 0x20, 0x20, 0xc4, 0.0f, 0.0f },
            { 1.0f, 0x20, 0x20, 0x20, 0x00, 0.0f, 0.0f },
        };

        zNeptuneParSize Size[8] = {
            { 0.0f, 0.05f, 0.0f, 0.0f }, { 0.03f, 0.8f, 0.0f, 0.0f }, { 1.0f, 0.9f, 0.0f, 0.0f },
            { 0.0f, 0.05f, 0.0f, 0.0f }, { 0.05f, 0.2f, 0.0f, 0.0f }, { 1.0f, 0.2f, 0.0f, 0.0f },
            { 0.0f, 0.15f, 0.0f, 0.0f }, { 1.0f, 0.3f, 0.0f, 0.0f },
        };

        zNeptuneParType Type[3] = {
            { 2.5f, 0, 0, 0, 0 },
            { 2.5f, 1, 1, 1, 1 },
            { 0.75f, 2, 2, 2, 2 },
        };
    } // namespace Par

    struct CrowdSortStruct
    {
        xEnt* ent;
        F32 angle;
    };

    static S32 inPreRender;
    static zNMENeptune* me;

    namespace Decal
    {
        struct animix
        {
            F32 r;
            F32 g;
            F32 b;
            F32 a;
            F32 scale_x;
            F32 scale_y;
        };

        struct curve_node
        {
            F32 t;
            animix ani;
        };

        xResponseCurve curve_test;
        curve_node anim_test[3] = {
            { 0.0f, { 0.3f, 0.2f, 0.2f, 1.0f, 1.0f, 1.0f } },
            { 0.8f, { 0.1f, 0.1f, 0.1f, 1.0f, 1.0f, 1.0f } },
            { 1.0f, { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f } },
        };
    } // namespace Decal

    static void DecalDestCB(RpAtomic* atomic);
    static void DecalEmitCB(RpAtomic* atomic, const xMat4x3* driver);
} // namespace Neptune

namespace Neptune
{
    static void ReflectBeforeAnimMatrices(xAnimPlay* play, xQuat* quat, xVec3* tran, S32);
}

// Context the fire attack hands to EmitBeamCB / EmitSpiralCB (PS2 DWARF, size 0x54).
struct EmitContext
{
    xVec3 tip;
    xVec3 dir;
    xVec3 oldtip;
    xVec3 tine;
    xVec3 fbasisX;
    xVec3 fbasisY;
    S16 basis[6];
};

namespace Neptune
{
    namespace Par
    {
        // No GC symbol: retail inlines it into FireFXUpdate through ParEmitBunch.
        static U32 EmitBeamCB(zNeptuneParticle* par, void* context, F32 timeoffs)
        {
            EmitContext* emitctx = (EmitContext*)context;
            zNeptuneParticleBeamUser* bmuser = (zNeptuneParticleBeamUser*)par->userdata;
            F32 vo;
            F32 spreadx;
            F32 spready;
            F32 randang;

            xVec3Lerp(&par->pos, &emitctx->oldtip, &emitctx->tip, timeoffs);

            vo = (me->cfg_neptune->fire_beamSpeedMax - me->cfg_neptune->fire_beamSpeedMin) *
                     xurand() +
                 me->cfg_neptune->fire_beamSpeedMin;
            bmuser->velEnd = emitctx->dir * vo;
            bmuser->velStart = bmuser->velEnd;
            bmuser->velRate = Type[0].lifetime / me->cfg_neptune->fire_beamSpreadTime;

            randang = 6.2831855f * xurand();
            spreadx = icos(randang);
            spready = isin(randang);
            spreadx *=
                2.0f * me->cfg_neptune->fire_beamSpreadDist / me->cfg_neptune->fire_beamSpreadTime;
            spready *=
                2.0f * me->cfg_neptune->fire_beamSpreadDist / me->cfg_neptune->fire_beamSpreadTime;

            bmuser->velStart.x += spreadx * emitctx->fbasisX.x + spready * emitctx->fbasisY.x;
            bmuser->velStart.y += spreadx * emitctx->fbasisX.y + spready * emitctx->fbasisY.y;
            bmuser->velStart.z += spreadx * emitctx->fbasisX.z + spready * emitctx->fbasisY.z;

            par->vel = bmuser->velStart;

            return 0;
        }

        // No GC symbol: retail inlines it into FireFXUpdate through ParEmitBunch. The PS2 DWARF
        // names no third parameter.
        static U32 EmitSpiralCB(zNeptuneParticle* par, void* context, F32)
        {
            EmitContext* emitctx = (EmitContext*)context;
            F32 xinit;
            F32 yinit;
            F32 invmag;
            zNeptuneParticleSpiralUser* spuser;
            S32 sign;

            par->pos = emitctx->tip;
            par->vel =
                emitctx->dir *
                ((me->cfg_neptune->fire_spiralSpeedMax - me->cfg_neptune->fire_spiralSpeedMin) *
                     xurand() +
                 me->cfg_neptune->fire_spiralSpeedMin);

            F32 rotrand = xurand();

            spuser = (zNeptuneParticleSpiralUser*)par->userdata;
            spuser->basis[0] = emitctx->basis[0];
            spuser->basis[1] = emitctx->basis[1];
            spuser->basis[2] = emitctx->basis[2];
            spuser->basis[3] = emitctx->basis[3];
            spuser->basis[4] = emitctx->basis[4];
            spuser->basis[5] = emitctx->basis[5];

            xinit = xVec3Dot(&emitctx->fbasisX, &emitctx->tine) -
                    xVec3Dot(&emitctx->fbasisX, &emitctx->tip);
            yinit = xVec3Dot(&emitctx->fbasisY, &emitctx->tine) -
                    xVec3Dot(&emitctx->fbasisY, &emitctx->tip);

            F32 mag = xsqrt(xinit * xinit + yinit * yinit);
            invmag = 1.0f / mag;
            spuser->rad = mag * 0.00024414062f;
            spuser->anglecs[0] = xinit * invmag;
            spuser->anglecs[1] = yinit * invmag;
            spuser->delta = rotrand * (me->cfg_neptune->fire_spiralRotMax -
                                       me->cfg_neptune->fire_spiralRotMin) +
                            me->cfg_neptune->fire_spiralRotMin;

            if (me->cfg_neptune->fire_spiralSign == 0)
            {
                sign = (xrand_RandomInt32() & 2) - 1;
            }
            else
            {
                sign = me->cfg_neptune->fire_spiralSign;
            }
            spuser->delta *= sign;

            return 1;
        }

        static U32 EmitScorchCB(zNeptuneParticle* par, void*, F32)
        {
            S32 foundScorch;
            U32 foundTriIdx;
            U32 foundVertIdx;
            F32 foundUV;
            S32 i;
            S32 whichScorch;
            F32 areaSample;
            RpGeometry* geom;
            U32 triVertIdx;
            U32 triIdx;
            U32 vertIdx;
            F32 testUV;
            xVec3 evalVerts[3];
            RpGeometry* scorchGeom;
            zNeptuneParticleGravBounceUser* gbuser;

            foundUV = 1e38f;
            for (i = 0; i < 6; i++)
            {
                whichScorch = 0;
                areaSample = me->scorchArea * xurand();
                while (whichScorch < me->scorchCount - 1 &&
                       areaSample > me->scorchList[whichScorch].area)
                {
                    areaSample -= me->scorchList[whichScorch].area;
                    whichScorch++;
                }

                geom = me->scorchList[whichScorch].atomic->geometry;
                triVertIdx = xrand_RandomInt32() % (geom->numTriangles * 3);
                triIdx = triVertIdx / 3;
                vertIdx = triVertIdx % 3;
                testUV =
                    SQR(geom->texCoords[0][geom->triangles[triIdx].vertIndex[vertIdx]].u - 0.5f) +
                    SQR(geom->texCoords[0][geom->triangles[triIdx].vertIndex[vertIdx]].v - 0.5f);
                if (testUV < foundUV)
                {
                    foundUV = testUV;
                    foundScorch = whichScorch;
                    foundTriIdx = triIdx;
                    foundVertIdx = vertIdx;
                }

                // testUV is a squared UV radius.
                if (testUV < SQR(0.4f))
                {
                    break;
                }
            }

            scorchGeom = me->scorchList[foundScorch].atomic->geometry;
            for (i = 0; i < 3; i++)
            {
                iModelVertEval(me->scorchList[foundScorch].atomic,
                               scorchGeom->triangles[foundTriIdx].vertIndex[i], 1,
                               (RwMatrixTag*)me->scorchList[foundScorch].driver, NULL,
                               &evalVerts[i]);
            }

            par->pos = evalVerts[foundVertIdx];
            evalVerts[1] -= evalVerts[0];
            evalVerts[2] -= evalVerts[0];
            xVec3Cross(&par->vel, &evalVerts[1], &evalVerts[2]);
            xVec3Normalize(&par->vel, &par->vel);
            par->vel = par->vel * 1.5f;

            gbuser = (zNeptuneParticleGravBounceUser*)par->userdata;
            gbuser->bounceHeight = -1000.0f;
            gbuser->gravity = -8.0f;

            return 2;
        }
    } // namespace Par

    static U8 SndHose(zNMENeptune::zLoopingSound* loopsnd)
    {
        const NMECfgNeptune* cfg = me->cfg_neptune;

        if (me->fireBeamGroundHit >= 0.0f)
        {
            loopsnd->pos = me->fireBeamGroundLoc;
        }
        else
        {
            loopsnd->pos = me->fireBeamParPos;
            me->fireBeamParPos = me->fireBeamParPos + me->fireBeamParVel * me->last_dt;
        }

        loopsnd->pos.x *= cfg->snd_hoseCenterBias;
        loopsnd->pos.z *= cfg->snd_hoseCenterBias;

        if (loopsnd->time < cfg->snd_hoseFadeUp)
        {
            loopsnd->volscale = cfg->snd_hoseVol * (loopsnd->time / cfg->snd_hoseFadeUp);
        }
        else if (me->fireBeamGroundHit >= 0.0f)
        {
            loopsnd->volscale =
                cfg->snd_hoseVol * (1.0f - me->fireBeamGroundHit / cfg->snd_hoseFadeDown);
            me->fireBeamGroundHit += me->last_dt;
            if (loopsnd->volscale < 0.0f)
            {
                loopsnd->volscale = 0.0f;
                if (!me->beamParThisFrame)
                {
                    return 0;
                }
            }
        }
        else
        {
            loopsnd->volscale = cfg->snd_hoseVol;
        }

        return 1;
    }

    // inFireLoop is named as in Process and FireFXUpdate, which make the same test.
    static U8 SndLightning(zNMENeptune::zLoopingSound* loopsnd)
    {
        U8 inFireLoop = (me->model->Anim->Single->State->UserFlags & 0x100000) &&
                        me->model->Anim->Single->Blend->State == NULL;

        if (inFireLoop)
        {
            loopsnd->volscale = me->cfg_neptune->snd_lightningVol;
            loopsnd->pos = me->ltBeamTarget;
        }
        else
        {
            loopsnd->volscale -= me->last_dt * (me->cfg_neptune->snd_lightningVol /
                                                me->cfg_neptune->snd_lightningFade);
            if (loopsnd->volscale < 0.0f)
            {
                loopsnd->volscale = 0.0f;
                return 0;
            }
        }

        return 1;
    }

    // playerX is its own statement while z stays in one expression, for retail's register order.
    // inFireLoop (named as in SndLightning) is a bool: retail narrows it only where it is used.
    static U8 SndRing(zNMENeptune::zLoopingSound* loopsnd)
    {
        const NMECfgNeptune* cfg = me->cfg_neptune;
        const NMECfgNeptuneAttack* currAtk = &cfg->stageAtk[me->bossStage];
        bool inFireLoop = (me->model->Anim->Single->State->UserFlags & 0x100000) &&
                          me->model->Anim->Single->Blend->State == NULL;
        zNMENeptune::zLightningRing* ltring = (zNMENeptune::zLightningRing*)loopsnd->context;
        F32 playerX;
        F32 playerZ;
        F32 invPlayerMag;
        F32 dopplerp;
        F32 distScale;

        loopsnd->pos = ltring->mat.pos;

        playerX = xEntGetPos(xglobals->___player_ent_dont_use_directly)->x - ltring->mat.pos.x;
        playerZ = xEntGetPos(xglobals->___player_ent_dont_use_directly)->z - ltring->mat.pos.z;
        invPlayerMag = xsqrt(playerX * playerX + playerZ * playerZ);
        if (invPlayerMag < 0.00001f)
        {
            playerX = 1.0f;
            playerZ = 0.0f;
        }
        else
        {
            playerX *= 1.0f / invPlayerMag;
            playerZ *= 1.0f / invPlayerMag;
        }

        if (invPlayerMag > ltring->rad)
        {
            loopsnd->pitch = cfg->snd_ringDopplerPitchMax;
        }
        else
        {
            dopplerp = (ltring->rad - invPlayerMag) / cfg->snd_ringDopplerDist;
            dopplerp = CLAMP(dopplerp, 0.0f, 1.0f);
            loopsnd->pitch =
                cfg->snd_ringDopplerPitchMax +
                dopplerp * (cfg->snd_ringDopplerPitchMin - cfg->snd_ringDopplerPitchMax);
        }

        playerX = playerX * ltring->rad + ltring->mat.pos.x;
        playerX = xEntGetPos(xglobals->___player_ent_dont_use_directly)->x - playerX;
        playerZ = xEntGetPos(xglobals->___player_ent_dont_use_directly)->z -
                  (playerZ * ltring->rad + ltring->mat.pos.z);
        distScale = xsqrt(SQ(playerX) + SQ(playerZ));
        distScale = 1.0f - distScale / cfg->snd_ringEarDist;
        distScale = MAX(distScale, 0.0f);

        if (ltring->rad > 0.9f * currAtk->ring_maxrad || !inFireLoop)
        {
            loopsnd->volscale -= me->last_dt * cfg->snd_ringFade;
            if (loopsnd->volscale < 0.0f)
            {
                loopsnd->volscale = 0.0f;
                return 0;
            }
        }
        else
        {
            loopsnd->volscale = distScale * cfg->snd_ringVol;
        }

        return 1;
    }

    static U8 SndBeamStun(zNMENeptune::zLoopingSound* loopsnd)
    {
        loopsnd->volscale = me->povBeamFade * me->cfg_neptune->snd_povBeamStunVol;
        if (me->povBeamFade <= 0.0f)
        {
            loopsnd->volscale = 0.0f;
            return 0;
        }
        return 1;
    }

    static U8 SndBucketStun(zNMENeptune::zLoopingSound* loopsnd)
    {
        const NMECfgNeptune* cfg = me->cfg_neptune;
        F32 vollerp;

        if (zCameraIsFlyCamRunning() && zcam_flytime < cfg->snd_povBucketStunFadeEnd)
        {
            if (zcam_flytime <= cfg->snd_povBucketStunFadeStart)
            {
                loopsnd->volscale = 0.0f;
            }
            else
            {
                vollerp = (zcam_flytime - cfg->snd_povBucketStunFadeStart) /
                          (cfg->snd_povBucketStunFadeEnd - cfg->snd_povBucketStunFadeStart);
                vollerp = MIN(1.0f, vollerp);
                loopsnd->volscale = vollerp * cfg->snd_povBucketStunVol;
            }
        }
        else
        {
            loopsnd->volscale = me->povBucketFade * cfg->snd_povBucketStunVol;
            if (me->povBucketFade <= 0.0f)
            {
                loopsnd->volscale = 0.0f;
                return 0;
            }
        }

        return 1;
    }

    NMEHealthMeter::MeterData health_meter = {
        { 1, 0, { 0, 0 } },
        { { -1, 12, 12, 12 }, "particle_cloud", 0.14f, 0.25f },
        { { -1, 10, 10, 10 } },
    };

    static NMEShadParms cfg_shadparm_static = { { 2.0f, "shadow_round", NULL },
                                                { 0 },
                                                { 100.0f, 100.0f } };

    xLightKit scopeLightKit;
    xLightKitLight scopeLightKitLight;

    static SoundTable SoundList[eSOUND_MAX] = {
        { "NeptuneOutrageGroup", NULL, 0 },        { "NeptuneDamageGroup", NULL, 0 },
        { "NeptuneTailLeftGroup", NULL, 0 },       { "NeptuneTailRightGroup", NULL, 0 },
        { "NeptuneDefendGroup", NULL, 0 },         { "NeptuneStunGroup", NULL, 0 },
        { "NeptuneFireClickGroup", NULL, 0 },      { "NeptuneRecoverGroup", NULL, 0 },
        { "NeptuneTauntGroup", NULL, 0 },          { "NeptuneFireChargeGroup", NULL, 0 },
        { "NeptuneFireHoseGroup", SndHose, 0 },    { "NeptuneFireLightningGroup", SndLightning, 0 },
        { "NeptuneFireRingGroup", SndRing, 0 },    { "NeptunePovChargeGroup", NULL, 0 },
        { "NeptunePovAttackGroup", NULL, 0 },      { "NeptunePovReflectGroup", NULL, 0 },
        { "NeptunePovStunGroup", SndBeamStun, 0 }, { "NeptunePovBucketGroup", SndBucketStun, 0 },
        { "PSBOSSNEP_004_SFX_GROUP", NULL, 0 },    { "PSBOSSNEP_003_SFX_GROUP", NULL, 0 },
        { "PSBOSSNEP_005_SFX_GROUP", NULL, 0 },    { "PSBOSSNEP_007_SFX_GROUP", NULL, 0 },
        { "PSBOSSNEP_006_SFX_GROUP", NULL, 0 },    { "PSBOSSNEP_008_SFX_GROUP", NULL, 0 },
        { "PSBOSSNEP_009_SFX_GROUP", NULL, 0 },    { "PLBOSSNEP_008B_SFX_GROUP", NULL, 0 },
        { "PLBOSSNEP_011_SFX_GROUP", NULL, 0 },    { "PLBOSSNEP_007_SFX_GROUP", NULL, 0 },
        { "PLBOSSNEP_010_SFX_GROUP", NULL, 0 },    { "PLBOSSNEP_009_SFX_GROUP", NULL, 0 },
        { "NEPTUNE_007C_SFX_GROUP", NULL, 0 },     { "NEPTUNE_001_SFX_GROUP", NULL, 0 },
        { "NEPTUNE_002_SFX_GROUP", NULL, 0 },      { "NEPTUNE_008C_SFX_GROUP", NULL, 0 },
    };

    // Retail bug kept: the user data name is compared with 'BAIH' (the PS2 byte order of "HIAB"),
    // so on GameCube the search never matches and usrdat is left at the last array checked.
    static void HackScaleModel(RpAtomic* model, F32 scale)
    {
        S32 i;

        while (model != NULL)
        {
            S32 vertCount = model->geometry->numVertices;
            RwV3d* verts = model->geometry->morphTarget->verts;

            for (i = 0; i < vertCount; i++)
            {
                verts[i].x = scale * verts[i].x;
                verts[i].y = scale * verts[i].y;
                verts[i].z = scale * verts[i].z;
            }

            S32 usrdatcount = RpGeometryGetUserDataArrayCount(model->geometry);
            RpUserDataArray* usrdat = NULL;

            for (i = 0; i < usrdatcount; i++)
            {
                usrdat = RpGeometryGetUserDataArray(model->geometry, i);
                if (*(S32*)usrdat->name == 'BAIH')
                {
                    break;
                }
            }

            if (usrdat != NULL)
            {
                S32* data = (S32*)usrdat->data;
                S32 numbones = data[0];
                xSphere* bonesph = (xSphere*)(data + data[1]);

                for (i = 0; i < numbones; i++, bonesph++)
                {
                    bonesph->center.x = scale * bonesph->center.x;
                    bonesph->center.y = scale * bonesph->center.y;
                    bonesph->center.z = scale * bonesph->center.z;
                    bonesph->r = scale * bonesph->r;
                }
            }

            RpSkin* pSkin = RpSkinGeometryGetSkin(model->geometry);
            if (pSkin != NULL)
            {
                S32 numbones = RpSkinGetNumBones(pSkin);
                RwMatrix* skinBoneMat = (RwMatrix*)RpSkinGetSkinToBoneMatrices(pSkin);

                for (i = 0; i < numbones; i++)
                {
                    skinBoneMat[i].pos.x = scale * skinBoneMat[i].pos.x;
                    skinBoneMat[i].pos.y = scale * skinBoneMat[i].pos.y;
                    skinBoneMat[i].pos.z = scale * skinBoneMat[i].pos.z;
                }
            }

            model->boundingSphere.center.x = scale * model->boundingSphere.center.x;
            model->boundingSphere.center.y = scale * model->boundingSphere.center.y;
            model->boundingSphere.center.z = scale * model->boundingSphere.center.z;
            model->boundingSphere.radius = scale * model->boundingSphere.radius;
            model->worldBoundingSphere.center.x = scale * model->boundingSphere.center.x;
            model->worldBoundingSphere.center.y = scale * model->boundingSphere.center.y;
            model->worldBoundingSphere.center.z = scale * model->boundingSphere.center.z;
            model->worldBoundingSphere.radius = scale * model->boundingSphere.radius;

            model = iModelFile_RWMultiAtomic(model);
        }
    }

    // No GC symbol (inlined into BossPreRender, its only caller); the name is ours.
    static inline void CopyCameraView(xFXCameraTexture* tex, RwCamera* src_cam)
    {
        RwCameraSetViewOffset(tex->cam, RwCameraGetViewOffset(src_cam));
        RwCameraSetNearClipPlane(tex->cam, RwCameraGetNearClipPlane(src_cam));
        RwCameraSetFarClipPlane(tex->cam, RwCameraGetFarClipPlane(src_cam));
        RwCameraSetFogDistance(tex->cam, RwCameraGetFogDistance(src_cam));
        RwCameraSetProjection(tex->cam, RwCameraGetProjection(src_cam));
        RwCameraSetViewWindow(tex->cam, RwCameraGetViewWindow(src_cam));
        RwFrameTransform(RwCameraGetFrame(tex->cam), RwFrameGetMatrix(RwCameraGetFrame(src_cam)),
                         rwCOMBINEREPLACE);
    }

    static void BossPreRender()
    {
        RwV2d vw;

        me->PostProcess(me->last_dt);
        me->FireFXUpdate(me->last_dt);
        me->ScorchUpdate(me->last_dt);
        me->ParUpdate(me->last_dt, false);

        if (me->ScopeViewActive())
        {
            inPreRender = 1;
            xModelBlur::render_enable(false);
            xPTankPoolFreeEnable(false);

            RpWorldRemoveCamera(me->scopeTex.world, me->scopeTex.cam);
            me->scopeTex.world = globals.sceneCur->env->geom->world;
            RpWorldAddCamera(me->scopeTex.world, me->scopeTex.cam);

            CopyCameraView(&me->scopeTex, xglobals->screen->icam);

            RwFrameTransform(RwCameraGetFrame(me->scopeTex.cam), (RwMatrix*)&me->scopeCamMat,
                             rwCOMBINEREPLACE);
            vw.x = vw.y = me->scopeCamFOV;
            RwCameraSetViewWindow(me->scopeTex.cam, &vw);

            me->scopeTex.begin_render(3, lastSetFBMSK, lastSetFBMSK, 0.0f, 0.0f);
            zRenderStateBegin();
            zSceneRenderPreFX();
            zRenderStateBegin();
            me->scopeTex.end_render();

            xPTankPoolFreeEnable(true);
            xModelBlur::render_enable(true);
            inPreRender = 0;
            xFXAuraClearCount();
        }
    }

    static void LightningBeamRenderCB(zLightning*, RxObjSpace3DVertex* vertA,
                                      RxObjSpace3DVertex* vertB, U32 numVerts)
    {
        S32 vnum;
        RxObjSpace3DVertex* vlist[2] = { vertA, vertB };

        for (vnum = 0; vnum < 2; vnum++)
        {
            if (vlist[vnum] != NULL &&
                RwIm3DTransform(vlist[vnum], numVerts, NULL,
                                rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
            {
                RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
                RwIm3DEnd();
            }
        }

        if (vertA != NULL && me->ltTimer > me->cfg_neptune->fire_ltDamageDelay)
        {
            me->CollideLightning(vertA, numVerts);
        }
    }

    // The GameCube RxObjSpace3DVertex is a flat struct (no objVertex), so the vertex y is scaled in
    // place.
    static void LightningFeelerRenderCB(zLightning*, RxObjSpace3DVertex* vertA,
                                        RxObjSpace3DVertex* vertB, U32 numVerts)
    {
        S32 vnum;
        RxObjSpace3DVertex* vlist[2] = { vertA, vertB };
        RxObjSpace3DVertex* vert;
        U32 i;
        const NMECfgNeptuneAttack* currAtk;
        F32 centerY;

        for (vnum = 0; vnum < 2; vnum++)
        {
            if (vlist[vnum] != NULL)
            {
                vert = vlist[vnum];
                currAtk = &me->cfg_neptune->stageAtk[me->bossStage];
                centerY = me->fireTarget.y;
                for (i = 0; i < numVerts; i++)
                {
                    vert[i].y = currAtk->feeler_thickness * (vert[i].y - centerY) + centerY;
                }

                if (RwIm3DTransform(vlist[vnum], numVerts, NULL,
                                    rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
                {
                    RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
                    RwIm3DEnd();
                }
            }
        }

        if (vertA != NULL && me->ltTimer > me->cfg_neptune->fire_ltTimeFeelerStart +
                                               me->cfg_neptune->fire_ltDamageDelay)
        {
            me->CollideLightning(vertA, numVerts);
        }
    }

    // The GameCube RxObjSpace3DVertex is a flat struct (no objVertex), so the vertex y is scaled in
    // place. The ring is closed by copying the last two vertices onto the first two with a two-pass
    // j loop, as in the PS2 build.
    static void LightningRingRenderCB(zLightning* ltng, RxObjSpace3DVertex* vertA,
                                      RxObjSpace3DVertex* vertB, U32 numVerts)
    {
        S32 vnum;
        S32 j;
        U32 i;
        RxObjSpace3DVertex* verts;
        RxObjSpace3DVertex* vlist[2] = { vertA, vertB };
        zNMENeptune::zLightningRing* currLtRing = (zNMENeptune::zLightningRing*)ltng->context;
        const NMECfgNeptuneAttack* currAtk = &me->cfg_neptune->stageAtk[me->bossStage];
        F32 rad = currLtRing->rad;
        F32 lineDelta = ltng->func.length / (0.5f * (numVerts - 2));
        F32 rotDelta = 2.0f * PI / (0.5f * (numVerts - 2));
        F32 deltaCos = icos(rotDelta);
        F32 deltaSin = isin(rotDelta);
        F32 lineOffset;
        F32 currCos;
        F32 currSin;
        xVec3* pos;
        F32 px;
        F32 pz;
        F32 tempCos;
        xVec3* posStart;
        xVec3* posEnd;
        F32 centerY;

        for (vnum = 0; vnum < 2; vnum++)
        {
            lineOffset = 0.0f;
            currCos = 1.0f;
            verts = vlist[vnum];
            currSin = 0.0f;
            if (verts != NULL)
            {
                for (i = 0; i < numVerts; i += 2)
                {
                    for (j = 0; j < 2; j++)
                    {
                        pos = (xVec3*)&verts[i + j].x;

                        px = pos->x - lineOffset;
                        pz = pos->z + rad;

                        pos->x = px * currCos + pz * currSin;
                        pos->z = pz * currCos - px * currSin;

                        xMat4x3Toworld(pos, &currLtRing->mat, pos);
                    }

                    tempCos = deltaCos * currCos - deltaSin * currSin;
                    currSin = deltaCos * currSin + deltaSin * currCos;
                    currCos = tempCos;
                    lineOffset += lineDelta;
                }

                for (j = 0; j < 2; j++)
                {
                    posStart = (xVec3*)&verts[j].x;
                    posEnd = (xVec3*)&verts[numVerts - 2 + j].x;
                    posStart->x = posEnd->x;
                    posStart->y = posEnd->y;
                    posStart->z = posEnd->z;
                }

                centerY = me->fireTarget.y;
                for (i = 0; i < numVerts; i++)
                {
                    verts[i].y = currAtk->feeler_thickness * (verts[i].y - centerY) + centerY;
                }
            }
        }

        for (vnum = 0; vnum < 2; vnum++)
        {
            if (vlist[vnum] != NULL &&
                RwIm3DTransform(vlist[vnum], numVerts, NULL,
                                rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
            {
                RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
                RwIm3DEnd();
            }
        }

        if (vertA != NULL &&
            me->ltTimer > currLtRing->startTime + me->cfg_neptune->fire_ltDamageDelay)
        {
            me->CollideLightning(vertA, numVerts);
        }
    }

    static S32 ExtractBoneInfluence(RpAtomic* model, S32* influence, RwMatrixTag* skinmat,
                                    S32 maxcount)
    {
        RpSkin* skin;
        U32* boneidx;
        RwMatrixWeights* bonewt;
        RwMatrixTag* bonemat;
        S32 i;
        S32 j;
        S32 numVert;
        S32 influenceIdx;
        S32 foundInfl;
        F32* currwt;
        U32 curridx;
        U32 matidx;

        skin = RpSkinGeometryGetSkin(model->geometry);
        foundInfl = 0;
        boneidx = (U32*)RpSkinGetVertexBoneIndices(skin);
        bonewt = (RwMatrixWeights*)RpSkinGetVertexBoneWeights(skin);
        bonemat = (RwMatrixTag*)RpSkinGetSkinToBoneMatrices(skin);
        numVert = model->geometry->numVertices;

        memset(influence, 0xff, maxcount * sizeof(S32));

        for (i = 0; i < numVert; i++)
        {
            curridx = *boneidx;
            for (j = 0, currwt = &bonewt->w0; j < 4 && *currwt != 0.0f; j++, currwt++)
            {
                matidx = curridx & 0xff;

                for (influenceIdx = 0; influenceIdx < maxcount; influenceIdx++)
                {
                    // signed compare in retail
                    if (influence[influenceIdx] == (S32)(matidx + 1))
                    {
                        break;
                    }
                }

                if (influenceIdx == maxcount)
                {
                    for (influenceIdx = 0; influenceIdx < maxcount; influenceIdx++)
                    {
                        if (influence[influenceIdx] == -1)
                        {
                            influence[influenceIdx] = matidx + 1;
                            if (skinmat != NULL)
                            {
                                skinmat[influenceIdx] = bonemat[matidx];
                            }
                            break;
                        }
                    }

                    if (influenceIdx == maxcount)
                    {
                        return maxcount + 1;
                    }

                    foundInfl++;
                }

                curridx >>= 8;
            }

            boneidx++;
            bonewt++;
        }

        return foundInfl;
    }

    // No PS2 DWARF entry (GameCube-only function); local names are ours.
    static void PlayerKnockBack(F32 vel, F32 time)
    {
        if (!globals.player.cheat_mode)
        {
            globals.player.KnockBackTimer = time;

            F32 dx = xglobals->___player_ent_dont_use_directly->model->Mat->pos.x -
                     me->model->Mat->pos.x;
            F32 dz = xglobals->___player_ent_dont_use_directly->model->Mat->pos.z -
                     me->model->Mat->pos.z;
            F32 dist = xsqrt(dx * dx + dz * dz);
            if (dist < 0.001f)
            {
                dx = me->model->Mat->at.x;
                dz = me->model->Mat->at.z;
                dist = 1.0f;
            }

            xglobals->___player_ent_dont_use_directly->frame->vel.x = dx * vel / dist;
            xglobals->___player_ent_dont_use_directly->frame->vel.z = dz * vel / dist;
        }
    }

    // No PS2 DWARF entry (GameCube-only function); names are ours. Solves
    // |target + targetVel * t - shooter| = speed * t for the larger root t.
    static F32 ComputeTargetFixedVel(xVec3* shooter, F32 speed, xVec3* target, xVec3* targetVel)
    {
        F32 a = speed * speed - targetVel->length2();
        xVec3 diff = *shooter - *target;
        F32 c = diff.length2();
        F32 b = 2.0f * xVec3Dot(&diff, targetVel);
        F32 disc = b * b - 4.0f * a * -c;

        if (disc < 0.0f)
        {
            return 0.0f;
        }

        F32 root = xsqrt(disc);
        F32 t1 = (0.5f / a) * (-b + root);
        F32 t2 = (0.5f / a) * (-b - root);
        return t1 > t2 ? t1 : t2;
    }

    static void BodyCollideBoundUpdateCB(xEnt* ent, xVec3* pos)
    {
        ent->bound.sph.center.x = pos->x;
        ent->bound.sph.center.y = pos->y + 2.0f;
        ent->bound.sph.center.z = pos->z;
        ent->bound.sph.r = 6.0f;
        xBoundUpdate(&ent->bound);
        zGridUpdateEnt(ent);
    }

    static void PovCollideBoundUpdateCB(xEnt* ent, xVec3* pos)
    {
        ent->bound.sph.center.x = pos->x;
        ent->bound.sph.center.y = pos->y;
        ent->bound.sph.center.z = pos->z;
        ent->bound.sph.r = 10.0f;
        xBoundUpdate(&ent->bound);
        zGridUpdateEnt(ent);
    }

    static void BucketCollideEventFunc(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                                       xBase* toParamWidget, U32 toParamWidgetID)
    {
        S32 i;

        if (toEvent == eEventHit_Cruise && me->state == 4)
        {
            if ((me->bossStage == 0 && me->pts_health[0] == me->cfg_neptune->stageHealth[0]) ||
                (me->bossStage == 1 && me->pts_health[1] == me->cfg_neptune->stageHealth[1]))
            {
                me->voSound = me->SndPlay(eSOUND_VO_PatDamage1, 1.0f);
            }

            if (me->bossStage == 2 && me->pts_health[2] == me->cfg_neptune->stageHealth[2])
            {
                me->voSound = me->SndPlay(eSOUND_VO_PlankDamage1, 1.0f);
            }

            me->voTableHintTimer = 0.0f;
            me->voTableHintCount = 1;
            me->voCruiseBubbleToggle = 0;

            if (globals.player.g.PowerUp[3] == 2)
            {
                me->pts_health[me->bossStage] -= 3;
            }
            else
            {
                me->pts_health[me->bossStage] -= 2;
            }

            if (me->pts_health[me->bossStage] < 0)
            {
                me->pts_health[me->bossStage] = 0;
            }

            if (me->bossStage == 0 && me->pts_health[0] == 0)
            {
                me->voSound = me->SndPlay(eSOUND_VO_PlankDamage1, 1.0f);
            }

            if (me->pts_health[me->bossStage] <= 0)
            {
                me->pts_health[me->bossStage] = 0;

                if (me->bossStage < 2)
                {
                    me->voDamageToggle = 0;
                    me->bossStage++;
                    if (me->bossStage == 2)
                    {
                        me->stage2wait = 1;
                    }
                }
                else
                {
                    zEntEvent(me, me, eEventDeath);
                }
            }

            if (me->bossStage == 2)
            {
                me->minionsActive = 1;
            }

            me->gotNeptune = 1;
            me->MeterUpdate();

            for (i = 0; i < 5; i++)
            {
                zEntEvent(me->entReflectors[i], eEventEnable);
                zEntEvent(me->entReflectors[i], eEventReverse);
            }
        }
    }

    static bool MinionAlive(xEnt* ent)
    {
        if (ent == NULL)
        {
            return false;
        }

        xGoal* goal = ((zNMECommon*)ent)->psy_self->GetCurGoal();
        return goal != NULL && goal->GetID() != NME_GOAL_SCAREWAIT &&
               goal->GetID() != NME_GOAL_AFTERLIFE;
    }

    static void MinionSpawn(xEnt* ent)
    {
        if (ent != NULL && !MinionAlive(ent))
        {
            xGoal* goal = ((zNMECommon*)ent)->psy_self->GetCurGoal();
            if (goal != NULL && goal->GetID() == NME_GOAL_SCAREWAIT)
            {
                zEntEvent(ent, eEventNMEScareSkip);
            }
            else
            {
                zEntEvent(ent, eEventNPCRespawn);
            }
        }
    }

    static void MinionKill(xEnt* ent)
    {
        if (ent != NULL && MinionAlive(ent))
        {
            zEntEvent(ent, eEventNPCKillQuietly);
        }
    }

    static void ReflectBeforeAnimMatrices(xAnimPlay* play, xQuat* quat, xVec3* tran, S32)
    {
        F32 fadeLerp;
        xQuat quatRot;
        static xVec3 idleTipTranslate = { 0.016312f, 4.396056f, 0.0f };

        fadeLerp = 1.0f;
        if (!(play->Single->State->UserFlags & 0x100000))
        {
            if (play->Single->Blend->State != NULL && play->Single->Tran != NULL)
            {
                fadeLerp = 1.0f - play->Single->BlendFactor * play->Single->Tran->BlendRecip;
            }
            else
            {
                play->BeforeAnimMatrices = NULL;
                return;
            }
        }

        xQuatFromAxisAngle(&quatRot, &g_Y3,
                           fadeLerp * ((PI / 180.0f) * me->cfg_neptune->pov_stunTridentRot));
        xQuatMul(&quat[me->tridentInfl[1] - 1], &quat[me->tridentInfl[1] - 1], &quatRot);
        xVec3Lerp(&tran[me->tridentInfl[2] - 1], &tran[me->tridentInfl[2] - 1], &idleTipTranslate,
                  fadeLerp);
    }

    static void DecalEmitCB(RpAtomic* atomic, const xMat4x3* driver)
    {
        F32 area;
        S32 i;
        S32 numTri;
        RpGeometry* geom;
        RwV3d* vert;
        RpTriangle* tri;
        xVec3* va;
        xVec3* vb;
        xVec3* vc;
        F32 dx0, dy0, dz0;
        F32 dx1, dy1, dz1;
        F32 crossx, crossy, crossz;

        if (me->scorchCount < 16)
        {
            me->scorchList[me->scorchCount].atomic = atomic;
            me->scorchList[me->scorchCount].driver = (xMat4x3*)driver;
            me->scorchList[me->scorchCount].timer = 0.0f;

            area = 0.0f;
            geom = atomic->geometry;
            numTri = geom->numTriangles;
            vert = geom->morphTarget->verts;
            tri = geom->triangles;

            for (i = 0; i < numTri; i++)
            {
                va = (xVec3*)&vert[tri->vertIndex[0]];
                vb = (xVec3*)&vert[tri->vertIndex[1]];
                vc = (xVec3*)&vert[tri->vertIndex[2]];
                dx0 = vb->x - va->x;
                dy0 = vb->y - va->y;
                dz0 = vb->z - va->z;
                dx1 = vc->x - va->x;
                dy1 = vc->y - va->y;
                dz1 = vc->z - va->z;
                crossx = dy0 * dz1 - dz0 * dy1;
                crossy = dz0 * dx1 - dx0 * dz1;
                crossz = dx0 * dy1 - dy0 * dx1;
                area += xsqrt(crossx * crossx + crossy * crossy + crossz * crossz);
                tri++;
            }

            me->scorchList[me->scorchCount].area = area;
            me->scorchCount++;
        }
    }

    static void DecalDestCB(RpAtomic* atomic)
    {
        S32 i;

        for (i = 0; i < me->scorchCount; i++)
        {
            if (me->scorchList[i].atomic == atomic)
            {
                me->scorchList[i] = me->scorchList[me->scorchCount - 1];
                me->scorchCount--;
                return;
            }
        }
    }

    static S32 CmpCrowd(const void* a, const void* b)
    {
        if (((CrowdSortStruct*)a)->angle > ((CrowdSortStruct*)b)->angle)
        {
            return 1;
        }
        if (((CrowdSortStruct*)a)->angle < ((CrowdSortStruct*)b)->angle)
        {
            return -1;
        }
        return 0;
    }

    static void Crowd_UpdateSlave(xEnt* ent, xScene* sc, F32 dt)
    {
        if (me->crowdSlaveUpdate)
        {
            zEntSimpleObj_Update((zEntSimpleObj*)ent, sc, dt);
            ent->model->animBound.lower.x -= 0.25f;
            ent->model->animBound.lower.y -= 0.25f;
            ent->model->animBound.lower.z -= 0.25f;
            ent->model->animBound.upper.x += 0.25f;
            ent->model->animBound.upper.y += 0.25f;
            ent->model->animBound.upper.z += 0.25f;
        }
    }

    // No symbol (always inlined; the name is ours): returns the crowd group whose master is ent.
    static inline xEnt** CrowdFindGroup(xEnt* ent)
    {
        for (S32 i = 0; i < 1; i++)
        {
            if (ent == me->entCrowd[i][0])
            {
                return me->entCrowd[i];
            }
        }
        return NULL;
    }

    // The GameCube build renders each crowd member with the master's atomic instead of the PS2
    // clone batching, so cloneList/cloneCount/frustRes from the PS2 DWARF are gone.
    static void Crowd_RenderMaster(xEnt* ent)
    {
        xEnt** crowdGroup;
        RpAtomic* atomic;
        RwFrame* frame;
        RpHAnimHierarchy* pHierarchy;
        RwMatrixTag* pAnimOldMatrix;

        crowdGroup = CrowdFindGroup(ent);

        atomic = ent->model->Data;
        frame = RpAtomicGetFrame(atomic);
        pHierarchy = iModelGetHierarchy(atomic);
        pAnimOldMatrix = pHierarchy->pMatrixArray;
        pHierarchy->pMatrixArray = ent->model->Mat + 1;

        xLightKit_Enable(ent->model->LightKit, globals.currWorld);

        while (*crowdGroup != NULL)
        {
            if (!xModelBoxCull(&(*crowdGroup)->model->animBound))
            {
                iModelSetWorldBoundFromBox(atomic, &(*crowdGroup)->model->animBound);
                frame->ltm = *(*crowdGroup)->model->Mat;
                RwMatrixUpdate(&frame->ltm);
                RpAtomicRender(atomic);
            }
            crowdGroup++;
        }

        pHierarchy->pMatrixArray = pAnimOldMatrix;
    }

    static void Crowd_RenderSlave(xEnt*)
    {
    }

    // No symbol (always inlined; the name is ours). Retail adds 0.0f to the crowd master's anim
    // speed; a literal '+= 0.0f' would be folded away, so the value comes in as a parameter.
    static inline void Crowd_AddSpeed(xEnt* ent, F32 speed)
    {
        ent->model->Anim->Single->CurrentSpeed += speed;
    }

    static U32 DefendCheck(xAnimTransition*, xAnimSingle*, void*)
    {
        return me->state == 5;
    }

    static U32 ExitDefendCheck(xAnimTransition*, xAnimSingle*, void*)
    {
        return me->state != 5;
    }

    static U32 TailAttackLeftCheck(xAnimTransition*, xAnimSingle* anim, void*)
    {
        return me->tailAttackValid && me->tailAttackRange && !me->tailAttackRight &&
               !anim->Blend->State && me->tailRepeatTimer == 0.0f;
    }

    static U32 TailAttackRightCheck(xAnimTransition*, xAnimSingle* anim, void*)
    {
        return me->tailAttackValid && me->tailAttackRange && me->tailAttackRight &&
               !anim->Blend->State && me->tailRepeatTimer == 0.0f;
    }

    static U32 TailAttackCB(xAnimTransition*, xAnimSingle*, void*)
    {
        me->tailRepeatTimer = me->cfg_neptune->tailattack_time;
        me->tailAttackToggle = !me->tailAttackToggle;
        me->tailAttackKnocked = 0;
        me->tail_bubcounter = 0.0f;
        return 0;
    }

    static U32 FireCheck(xAnimTransition*, xAnimSingle*, void*)
    {
        return me->state == 2;
    }

    static U32 FireLoopCheck(xAnimTransition*, xAnimSingle* anim, void*)
    {
        return anim->Time + 0.1f * anim->CurrentSpeed >= anim->State->Data->Duration;
    }

    static U32 FireCB(xAnimTransition*, xAnimSingle*, void*)
    {
        me->timer = 0.0f;
        return 0;
    }

    static U32 FireLoopCB(xAnimTransition*, xAnimSingle*, void*)
    {
        me->FirePitchUpdate();
        return 0;
    }

    static U32 ReflectCheck(xAnimTransition*, xAnimSingle*, void*)
    {
        return me->state == 0x1004;
    }

    static U32 ReflectCB(xAnimTransition*, xAnimSingle* anim, void*)
    {
        anim->BilinearLerp[0] = me->cfg_neptune->pov_stunAimLerp;
        anim->Play->BeforeAnimMatrices = ReflectBeforeAnimMatrices;
        me->povStunTagCalced = 0;
        return 0;
    }

    static U32 ExitFireCheck(xAnimTransition*, xAnimSingle*, void*)
    {
        return me->state != 2 && me->state != 0x1004;
    }

    static U32 TauntCheck(xAnimTransition*, xAnimSingle*, void*)
    {
        return me->tauntEm;
    }

    static U32 TauntCB(xAnimTransition*, xAnimSingle*, void*)
    {
        me->tauntEm = 0;
        me->timer = 0.0f;
        return 0;
    }

    static U32 OutrageCheck(xAnimTransition* tran, xAnimSingle*, void*)
    {
        return me->state == 3 && me->bossStage == tran->UserFlags;
    }

    static U32 OutrageEndCheck(xAnimTransition*, xAnimSingle*, void*)
    {
        return me->state != 3;
    }

    static U32 StunCheck(xAnimTransition*, xAnimSingle*, void*)
    {
        return me->state == 4 || (me->state == 0x1005 && me->povStunnedMe);
    }

    static U32 ReflectStunCheck(xAnimTransition*, xAnimSingle*, void*)
    {
        return me->state == 0x1004 && me->povStunnedMe &&
               me->timer >= me->cfg_neptune->pov_stunAnimTranTime;
    }

    static U32 NoStunCheck(xAnimTransition* tran, xAnimSingle* anim, void* data)
    {
        return !StunCheck(tran, anim, data) && !me->gotNeptune && me->state != 0x1004;
    }

    static U32 DamageCheck(xAnimTransition* tran, xAnimSingle* anim, void* data)
    {
        return !StunCheck(tran, anim, data) && me->gotNeptune;
    }

    static U32 DamageExitCB(xAnimTransition*, xAnimSingle*, void*)
    {
        zEntPlayerControlOn(CONTROL_OWNER_BOSS);
        me->gotNeptune = 0;
        return 0;
    }
} // namespace Neptune

zCamNeptune::zCamNeptune()
{
}

void zCamNeptune::create()
{
    xCam::create();
    coord_type = XCAM_COORD_CART;
    orient_type = XCAM_ORIENT_QUAT;
    cfg_common.blend_time = 1.0f;
    cfg = Neptune::Config::cfg_povcam;
}

void zCamNeptune::start()
{
    fov = (PI / 180.0f) * cfg.povfov;
    xCam::start();
}

void zCamNeptune::update(xScene& s, F32 dt)
{
    xVec3 atvec;
    xMat4x3 atmat;

    fov = (PI / 180.0f) * cfg.povfov;

    if (zCameraIsFlyCamRunning())
    {
        if (lastFlyThru)
        {
            coord.cart = globals.oldSkoolCamera.mat.pos;
            xQuatFromMat(&orient.quat, &globals.oldSkoolCamera.mat);
        }
        lastFlyThru = 1;
    }
    else
    {
        if (!lastFlyThru)
        {
            atvec.x = cfg.viewPoint.x - xEntGetPos(xglobals->___player_ent_dont_use_directly)->x;
            atvec.y = cfg.viewPoint.y - cfg.targetHeight;
            atvec.z = cfg.viewPoint.z - xEntGetPos(xglobals->___player_ent_dont_use_directly)->z;
            xMat3x3LookVec(&atmat, &atvec);
            coord.cart = cfg.viewPoint;
            xQuatFromMat(&orient.quat, &atmat);
        }
        lastFlyThru = 0;
    }

    refresh_mat();
}

void zCamNeptune::destroy()
{
}

void zNMENeptune::CreateConfig(const char* name)
{
    zNMECommon::CreateConfig(name);
    cfg_neptune = &Neptune::Config::cfg_neptune_default;
    if (Neptune::cfg_shadparm_static.simpshad.nam_shadowTexture == NULL)
    {
        Neptune::cfg_shadparm_static.simpshad.rast_shadow = NULL;
    }
    else
    {
        Neptune::cfg_shadparm_static.simpshad.rast_shadow =
            NMEC_FindRWRaster(Neptune::cfg_shadparm_static.simpshad.nam_shadowTexture);
    }
    cfg_shadparm = &Neptune::cfg_shadparm_static;
}

xAnimTable* zNMENeptune::CreateAnimTable(xAnimTable* table)
{
    S32 i;

    for (i = 0; i < 14; i++)
    {
        Neptune::AnimIDs[i] = xStrHash(Neptune::AnimDef[i].name);
    }

    if (table == NULL)
    {
        table = xAnimTableNew("zNMEBossNeptune", 0);
    }

    for (i = 0; i < 14; i++)
    {
        xAnimTableNewState(table, Neptune::AnimDef[i].name, Neptune::AnimDef[i].flags,
                           Neptune::AnimDef[i].userFlags, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                           xAnimDefaultBeforeEnter, NULL, NULL);
    }

    xAnimTableNewTransition(table, "Taunt AttackEnd TailLeft TailRight Recover", "Idle", NULL, NULL,
                            0x10, 0, 0.0f, 0.0f, 5, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "AttackStart", "AttackLoop", NULL, NULL, 0x10, 0, 0.0f, 0.0f, 5,
                            0, 0.1f, NULL);
    xAnimTableNewTransition(table, "Damage", "Idle", NULL, Neptune::DamageExitCB, 0x10, 0, 0.0f,
                            0.0f, 5, 0, 0.15f, NULL);

    xAnimTableNewTransition(
        table, "Idle Taunt Outrage01 Outrage02 Outrage03 AttackStart AttackLoop AttackEnd",
        "Defend", Neptune::DefendCheck, NULL, 0, 0, 0.0f, 0.0f, 10, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Defend", "Idle", Neptune::ExitDefendCheck, NULL, 0, 0, 0.0f,
                            0.0f, 5, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Defend", "AttackStart", Neptune::FireCheck, Neptune::FireCB, 0,
                            0, 0.0f, 0.0f, 6, 0, 0.15f, NULL);

    xAnimTableNewTransition(table, "Idle", "AttackStart", Neptune::FireCheck, Neptune::FireCB, 0, 0,
                            0.0f, 0.0f, 4, 0, 0.1f, NULL);
    xAnimTableNewTransition(table, "AttackLoop", "AttackEnd", Neptune::ExitFireCheck,
                            Neptune::FireLoopCB, 0, 0, 0.0f, 0.0f, 4, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "AttackStart", "AttackLoop", Neptune::FireLoopCheck,
                            Neptune::FireLoopCB, 0, 0, 0.0f, 0.0f, 6, 0, 0.1f, NULL);

    xAnimTableNewTransition(table,
                            "Idle Taunt Outrage01 Outrage02 Outrage03 AttackStart AttackEnd "
                            "TailLeft TailRight Damage Recover Defend",
                            "AttackLoop", Neptune::ReflectCheck, Neptune::ReflectCB, 0, 0, 0.0f,
                            0.0f, 15, 0, 0.0f, NULL);

    xAnimTableNewTransition(
        table, "Idle Taunt Outrage01 Outrage02 Outrage03 AttackStart AttackLoop AttackEnd",
        "TailLeft", Neptune::TailAttackLeftCheck, Neptune::TailAttackCB, 8, 0, 0.0f, 0.0f, 11, 0,
        0.08f, NULL);

    xAnimTableNewTransition(
        table, "Idle Taunt Outrage01 Outrage02 Outrage03 AttackStart AttackLoop AttackEnd",
        "TailRight", Neptune::TailAttackRightCheck, Neptune::TailAttackCB, 8, 0, 0.0f, 0.0f, 11, 0,
        0.08f, NULL);

    xAnimTableNewTransition(table, "Idle AttackStart AttackLoop AttackEnd", "Taunt",
                            Neptune::TauntCheck, Neptune::TauntCB, 0, 0, 0.0f, 0.0f, 9, 0, 0.15f,
                            NULL);

    xAnimTableNewTransition(table, "Idle Taunt AttackStart AttackLoop AttackEnd", "Outrage01",
                            Neptune::OutrageCheck, NULL, 0, 0, 0.0f, 0.0f, 10, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Idle Taunt AttackStart AttackLoop AttackEnd", "Outrage02",
                            Neptune::OutrageCheck, NULL, 0, 1, 0.0f, 0.0f, 10, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Idle Taunt AttackStart AttackLoop AttackEnd", "Outrage03",
                            Neptune::OutrageCheck, NULL, 0, 2, 0.0f, 0.0f, 10, 0, 0.15f, NULL);
    xAnimTableNewTransition(table, "Outrage01 Outrage02 Outrage03", "Idle",
                            Neptune::OutrageEndCheck, NULL, 0, 0, 0.0f, 0.0f, 12, 0, 0.0f, NULL);

    xAnimTableNewTransition(table, "Idle", "Stun", Neptune::StunCheck, NULL, 0, 0, 0.0f, 0.0f, 13,
                            0, 0.0f, NULL);
    xAnimTableNewTransition(table, "Stun", "Recover", Neptune::NoStunCheck, NULL, 0, 0, 0.0f, 0.0f,
                            1, 0, 0.1f, NULL);
    xAnimTableNewTransition(table, "Stun", "Damage", Neptune::DamageCheck, NULL, 0, 0, 0.0f, 0.0f,
                            2, 0, 0.1f, NULL);
    xAnimTableNewTransition(table, "AttackLoop", "Stun", Neptune::ReflectStunCheck, NULL, 0, 0,
                            0.0f, 0.0f, 16, 0, 0.3f, NULL);

    return table;
}

void zNMENeptune::Init(xEntAsset* entasset)
{
    S32 i;
    xAnimTable* table;
    xAnimState* currstate;
    iAnimSKBHeader* skb;

    Neptune::HackScaleModel((RpAtomic*)xSTFindAsset(0x3b71fafb, NULL), 2.0f);
    Neptune::HackScaleModel((RpAtomic*)xSTFindAsset(0x27376f67, NULL), 1.0f);

    table = (xAnimTable*)xSTFindAsset(0x9b79ab38, NULL);
    for (currstate = table->StateList; currstate != NULL; currstate = currstate->Next)
    {
        for (i = 0; i < currstate->Data->NumAnims[0] * currstate->Data->NumAnims[1]; i++)
        {
            skb = (iAnimSKBHeader*)currstate->Data->RawData[i];
            // The top bit of ExtractedData marks an animation that was already scaled.
            if (!((UPtr)skb->ExtractedData & 0x80000000))
            {
                skb->Scale[0] = 2.0f * skb->Scale[0];
                skb->Scale[1] = 2.0f * skb->Scale[1];
                skb->Scale[2] = 2.0f * skb->Scale[2];
                skb->ExtractedData = (void*)((UPtr)skb->ExtractedData | 0x80000000);
            }
        }
    }

    for (currstate = table->StateList; currstate != NULL; currstate = currstate->Next)
    {
        for (i = 0; i < currstate->Data->NumAnims[0] * currstate->Data->NumAnims[1]; i++)
        {
            skb = (iAnimSKBHeader*)currstate->Data->RawData[i];
            skb->ExtractedData = (void*)((UPtr)skb->ExtractedData & 0x7fffffff);
        }
    }

    Neptune::me = this;
    bossStage = 0;
    zNMECommon::Init(entasset);

    povcam.create();
    bosscam.cfg = Neptune::Config::cfg_bosscam;
    bosscam.create();
    bosscam.cfg_common.blend_time = 1.0f;

    scopeTex.create(0x80, 0x80, true, 0x20, 0x20);
    scopeTex.set_background(g_BLACK);
    scopeWorld = scopeTex.world;

    tridentModel = (RpAtomic*)xSTFindAsset(0x27376f67, NULL);
    scopeModel[1] = iModelFile_RWMultiAtomic(tridentModel);
    scopeModel[0] = iModelFile_RWMultiAtomic(scopeModel[1]);
    if (scopeModel[0] == NULL)
    {
        scopeModel[0] = scopeModel[1];
    }

    scopeLight = RpLightCreate(rpLIGHTAMBIENT);
    RwRGBAReal tempColor = { 0.7f, 0.8f, 0.7f, 1.0f };
    RpLightSetColor(scopeLight, &tempColor);
    Neptune::scopeLightKit.lightCount = 1;
    Neptune::scopeLightKit.lightList = &Neptune::scopeLightKitLight;
    Neptune::scopeLightKitLight.type = 1;
    Neptune::scopeLightKitLight.color = tempColor;
    Neptune::scopeLightKitLight.platLight = scopeLight;

    scopeOldRast = scopeModel[0]->geometry->matList.materials[0]->texture->raster;
    scopeCamModelBucket[0] = xModelBucket_GetBuckets(scopeModel[0]);
    scopeCamModelBucket[1] = xModelBucket_GetBuckets(scopeModel[1]);
    scopeCamModelBucket[2] = xModelBucket_GetBuckets(tridentModel);

    tail_ribbon[0] = NULL;
    tail_ribbon[1] = NULL;
    tail_bubcounter = 0.0f;

    Neptune::Decal::curve_test.reset(6, Neptune::Decal::anim_test, 3);

    // The trident is the first atomic of its multi-atomic model, so this skip loop never runs.
    i = 0;
    RpAtomic* trident_model = (RpAtomic*)xSTFindAsset(0x3b71fafb, NULL);
    RpAtomic* bucket_model = trident_model;
    while (i != 0)
    {
        trident_model = iModelFile_RWMultiAtomic(trident_model);
        i++;
    }

    for (i = 0; i < 8; i++)
    {
        blurTrident[i] = xModelInstanceAllocReuse(trident_model, this, 0, 0, NULL, NULL);
        blurTrident[i]->Pipe.Flags = 0x6520;
    }

    tridentInfl[0] = 0;
    Neptune::ExtractBoneInfluence(trident_model, &tridentInfl[1], NULL, 3);
    bucket_model = iModelFile_RWMultiAtomic(bucket_model);
    Neptune::ExtractBoneInfluence(bucket_model, &bucketInfl, &bucketSkinMat, 1);

    rwpartex = (RwTexture*)xSTFindAsset(0xaf7cd347, NULL);
    rwpovbeamtex = (RwTexture*)xSTFindAsset(0x27029fe2, NULL);
    rwdecaltex = (RwTexture*)xSTFindAsset(0x813e5b0e, NULL);

    for (i = 0; i < Neptune::eSOUND_MAX; i++)
    {
        Neptune::SoundList[i].assetID = xStrHash(Neptune::SoundList[i].assetName);
    }

    if (scopeModel[0] != NULL)
    {
        S32 matidx;
        RpGeometry* geom = scopeModel[0]->geometry;
        RpMaterialList* matlist = &geom->matList;
        RpMaterial* scopeMat = NULL;

        for (matidx = 0; matidx < matlist->numMaterials; matidx++)
        {
            if (matlist->materials[matidx]->texture != NULL &&
                xStricmp(matlist->materials[matidx]->texture->name, "porthole_big_2") == 0)
            {
                scopeMat = matlist->materials[matidx];
                break;
            }
        }

        if (scopeMat != NULL)
        {
            for (S32 i = 0; i < geom->numVertices; i++)
            {
                for (S32 j = 0; j < geom->numTriangles; j++)
                {
                    if (matidx == geom->triangles[j].matIndex &&
                        (i == geom->triangles[j].vertIndex[0] ||
                         i == geom->triangles[j].vertIndex[1] ||
                         i == geom->triangles[j].vertIndex[2]))
                    {
                        geom->texCoords[0][i].u = 1.0f - geom->texCoords[0][i].u;
                        break;
                    }
                }
            }
        }
    }
}

void zNMENeptune::CommonReset()
{
    S32 i;
    zEntSimpleObj* simpList;
    S32 simpCount;
    S32 crowdCount;
    Neptune::CrowdSortStruct crowdList[20];

    crowdCount = 0;
    simpList = (zEntSimpleObj*)globals.sceneCur->baseList[eBaseTypeStatic];
    simpCount = globals.sceneCur->baseCount[eBaseTypeStatic];
    for (i = 0; i < simpCount; i++, simpList++)
    {
        if (simpList->asset->modelInfoID == 0xe9ffa48 && crowdCount < 20)
        {
            crowdList[crowdCount].ent = simpList;
            crowdList[crowdCount].angle =
                xAngleClampFast(atan2(simpList->asset->pos.x, simpList->asset->pos.z));
            crowdCount++;
        }
    }

    qsort(crowdList, crowdCount, sizeof(Neptune::CrowdSortStruct), Neptune::CmpCrowd);
    memset(entCrowd, 0, sizeof(entCrowd));

    for (i = 0; i < crowdCount; i++)
    {
        if (i == 0)
        {
            crowdList[i].ent->render = Neptune::Crowd_RenderMaster;
            Neptune::Crowd_AddSpeed(crowdList[i].ent, 0.0f);
        }
        else
        {
            crowdList[i].ent->update = Neptune::Crowd_UpdateSlave;
            crowdList[i].ent->render = Neptune::Crowd_RenderSlave;
        }
        entCrowd[0][i] = crowdList[i].ent;
    }

    povcam.cfg_common.priority = 10;
    bosscam.cfg_common.priority = 5;
    bosscam.attach(xglobals->___player_ent_dont_use_directly);
    camState = 0;
    povcam.lastFlyThru = 0;
    BossCamSetTargets();
    bosscam.move_to_rest();

    zSB04_BossPreRender = Neptune::BossPreRender;
    SetState(1);
    timer = -cfg_neptune->safety_time;
    stateFrozen = 0;
    locked_aim_lerp = 0.0f;
    cruise_bubble::Cam_Launch = NULL;
    cruise_bubble::Cam_Return = NULL;
    tailAttackValid = 1;
    tailRepeatTimer = 0.0f;
    firePlayerLagDir = 5.0f;

    for (i = 0; i < 2; i++)
    {
        if (tail_ribbon[i] != NULL)
        {
            zFXRibbonPoolDelete(tail_ribbon[i]);
        }
    }

    blurHistLast = 0;
    blurHistTotal = 0;

    if (collideBucket != NULL)
    {
        collideBucket->model->Mat->pos.x = 0.0f;
        collideBucket->model->Mat->pos.y = 100.0f;
        collideBucket->model->Mat->pos.z = 0.0f;
        bucketCollideActive = 0;
    }

    if (collideDefend != NULL)
    {
        collideDefend->model->Mat->pos.x = 0.0f;
        collideDefend->model->Mat->pos.y = 100.0f;
        collideDefend->model->Mat->pos.z = 0.0f;
    }

    defendLast = 0;

    if (entMrKrabs->model->Anim != NULL)
    {
        entMrKrabs->model->Anim->Table->StateList->Speed = 0.0f;
        entMrKrabs->model->Anim->Single->CurrentSpeed = 0.0f;
        entMrKrabs->model->Anim->Single->Time = 0.0f;
    }

    parCount = 0;
    fireBeamEmit = 0.0f;
    fireSpiralTineEmit[0] = 0.0f;
    fireSpiralTineEmit[1] = 0.0f;
    scorchEmit = 0.0f;
    memset(ltBeam, 0, sizeof(ltBeam));
    memset(ltFeeler, 0, sizeof(ltFeeler));
    memset(ltRing, 0, sizeof(ltRing));
    firePlayerLagIdx = 0;
    last_dt = 0.0f;
    gotPlayer = 0;
    gotPlayerThisAttack = 0;
    tauntEm = 0;
    outrageCount = 0;
    povBeamFade = 0.0f;
    povBucketFade = 0.0f;
    povBeamU = 0.0f;
    gotNeptune = 0;
    lightningDamageAfter = 0.0f;
    aim_tooCloseLerp = 0.0f;
    minionsActive = 0;
    scorchCount = 0;
    povFirstReflect = 1;
    povTridentGlow = 0.0f;
    crowdSlaveUpdate = 3;
    cruiseTimer = 0.0f;
    stage2wait = 0;
    firstTag = 1;
    bossModel[2]->Flags &= (U16)~0x3;
    bossModel[3]->Flags &= (U16)~0x3;
    zEntPlayerControlOn(CONTROL_OWNER_BOSS);

    for (i = 0; i < 16; i++)
    {
        loopSnd[i].handle = (iSndHandle)-1;
    }

    voSound = (iSndHandle)-1;
    voTimer = 0.0f;
    voRand = xurand();
    voTableHintTimer = 0.0f;
    voTableHintCount = 0;
    voDamageToggle = 0;
    voPOVDamageToggle = 0;
    voOutrageTauntToggle = 0;
    voDodgeHoseToggle = 0;
    voCruiseBubbleToggle = 0;
    bucketCollideActive = 1;
    memcpy(pts_health, cfg_neptune->stageHealth, sizeof(pts_health));

    if (bossStage == 2)
    {
        minionsActive = 1;
        zEntEvent(entDestructFloor, entDestructFloor, eEventEnable);
        zEntEvent(entDestructFloor, entDestructFloor, eEventCollision_Visible_Off);
        if (underFloorJSP != NULL)
        {
            iEnvJSPOn(globals.sceneCur->env->geom, underFloorJSP);
        }
    }
    else
    {
        zEntEvent(entDestructFloor, entDestructFloor, eEventDisable);
        if (underFloorJSP != NULL)
        {
            iEnvJSPOff(globals.sceneCur->env->geom, underFloorJSP);
        }
    }
}

// No GC symbol (always inlined; the name is ours, after SndPlay and SndLoop3D). Defined here
// because it needs the file-static SoundList.
inline iSndHandle zNMENeptune::SndPlay3D(Neptune::eSOUND soundEnum, xVec3* playpos, F32 volume)
{
    if (globals.cmgr != NULL)
    {
        return (iSndHandle)-1;
    }
    F32 vol = volume;
    return xSndMgrPlay(xSndMgrGetSoundGroup(Neptune::SoundList[soundEnum].assetID), 0, playpos,
                       NULL, NULL, &vol, NULL);
}

iSndHandle zNMENeptune::SndPlay(Neptune::eSOUND soundEnum, F32 volume)
{
    if (globals.cmgr != NULL)
    {
        return (iSndHandle)-1;
    }
    F32 vol = volume;
    return xSndMgrPlay(xSndMgrGetSoundGroup(Neptune::SoundList[soundEnum].assetID), 0, NULL, NULL,
                       NULL, &vol, NULL);
}

zNMENeptune::zLoopingSound* zNMENeptune::SndLoop3D(Neptune::eSOUND soundEnum, xVec3* playpos,
                                                   void* context)
{
    S32 i;
    U32 sndID;
    U32 sndFlags;
    zLoopingSound* looper;

    if (globals.cmgr != NULL)
    {
        return NULL;
    }

    sndFlags = 0;
    looper = NULL;
    sndID = Neptune::SoundList[soundEnum].assetID;

    for (i = 0; i < 16; i++)
    {
        if (loopSnd[i].handle == (iSndHandle)-1)
        {
            looper = &loopSnd[i];
            break;
        }
    }

    if (looper == NULL)
    {
        return NULL;
    }

    looper->sndUpdateCB = Neptune::SoundList[soundEnum].updateCB;
    looper->context = context;
    looper->time = 0.0f;
    if (playpos != NULL)
    {
        sndFlags |= 2;
        looper->pos = *playpos;
    }
    looper->volscale = 1.0f;
    looper->pitch = 0.0f;

    if (looper->sndUpdateCB != NULL)
    {
        looper->sndUpdateCB(looper);
    }

    looper->handle =
        xSndMgrPlay(xSndMgrGetSoundGroup(sndID), sndFlags, playpos != NULL ? &looper->pos : NULL,
                    NULL, NULL, &looper->volscale, NULL);
    if (looper->handle == (iSndHandle)-1)
    {
        return NULL;
    }

    return looper;
}

void zNMENeptune::SndVOUpdate(F32 dt)
{
    if (voSound != (iSndHandle)-1)
    {
        if (xSndMgrIsPlaying(voSound))
        {
            return;
        }

        voSound = (iSndHandle)-1;
        voTimer = 0.0f;
        voRand = xurand();
    }

    voTimer += dt;
}

bool zNMENeptune::SndVOTableHintCheck()
{
    return SndVOCheck(1.0f) && state != 3 && state != 0x1004 && state != 0x1005 && state != 4 &&
           state != 5 && state != 6;
}

void zNMENeptune::CamStateChange(U32 newCamState, bool force_cut)
{
    if (bossStage == 2)
    {
        bosscam.cfg = Neptune::Config::cfg_thirdstage_bosscam;
    }
    else
    {
        bosscam.cfg = Neptune::Config::cfg_bosscam;
    }

    if (newCamState != camState)
    {
        if (newCamState == 0)
        {
            zCameraEnableTracking(CO_BOSSCRUISELAUNCH);
            if (camState == 1)
            {
                zCamRemove(bosscam, force_cut);
            }
            else
            {
                zCamRemove(povcam, force_cut);
            }
        }
        else if (newCamState == 1)
        {
            bosscam.cfg_common.priority = 10;
            povcam.cfg_common.priority = 5;
            if (camState == 2)
            {
                zCamAdd(bosscam, force_cut);
                zCamRemove(povcam, force_cut);
            }
            else
            {
                zCamAdd(bosscam, force_cut);
            }
        }
        else
        {
            povcam.cfg_common.priority = 10;
            bosscam.cfg_common.priority = 5;
            if (camState == 1)
            {
                zCamAdd(povcam, force_cut);
                zCamRemove(bosscam, force_cut);
            }
            else
            {
                zCamAdd(povcam, force_cut);
            }
        }

        if (newCamState == 1)
        {
            cruise_bubble::Cam_Launch = cruise_bubble::xCamLaunch;
            cruise_bubble::Cam_Return = cruise_bubble::xCamReturn;
            cruise_bubble::xCamLaunchCam = &bosscam;
        }
        else
        {
            cruise_bubble::Cam_Launch = NULL;
            cruise_bubble::Cam_Return = NULL;
        }

        if (newCamState == 1 && force_cut)
        {
            BossCamSetTargets();
            bosscam.move_to_rest();
        }
    }

    camState = newCamState;
}

// GameCube-only (the PS2 demo inlines it into Process); the names are ours. Cosine of the angle
// between Neptune's facing and the direction to the player on the XZ plane (-1 when either is
// degenerate).
F32 zNMENeptune::DotToPlayer()
{
    const xVec3* ppos = xEntGetPos(xglobals->___player_ent_dont_use_directly);
    F32 dx = ppos->x - xEntGetPos(this)->x;
    F32 dz = ppos->z - xEntGetPos(this)->z;
    F32 dist = xsqrt(dx * dx + dz * dz);
    if (dist < 0.00001f)
    {
        return -1.0f;
    }

    const xVec3* at = &xEntGetFrame(this)->at;
    F32 atlen = xsqrt(at->x * at->x + at->z * at->z);
    if (atlen < 0.00001f)
    {
        return -1.0f;
    }

    F32 atx = xEntGetFrame(this)->at.x;
    F32 atz = xEntGetFrame(this)->at.z;
    return (atx * dx + atz * dz) / (dist * atlen);
}

// No symbol (always inlined; name and signature are ours). Intersects the idx-th sampled fire ray
// (model space) with the horizontal plane at the fire target's height; returns 0 when it points up.
static inline S32 FirePitchHit(zNMENeptune* nep, S32 idx, F32& hitX, F32& hitZ)
{
    xVec3 worldStart;
    xVec3 worldDir;

    xMat4x3Toworld(&worldStart, xModelGetFrame(nep->model), &nep->firePitchStart[idx]);
    xMat3x3RMulVec(&worldDir, xModelGetFrame(nep->model), &nep->firePitchDir[idx]);

    if (worldDir.y > -0.00001f)
    {
        hitZ = hitX = 0.0f;
        return 0;
    }

    F32 t = (nep->fireTarget.y - worldStart.y) / worldDir.y;
    hitX = worldStart.x + t * worldDir.x;
    hitZ = worldStart.z + t * worldDir.z;
    return 1;
}

// Picks the fire animation's pitch blend so the beam hits the ground at fireTarget, then mirrors
// that hit point across Neptune's facing into fireFace.
void zNMENeptune::FirePitchUpdate()
{
    S32 i;
    xAnimSingle* fireSingle;
    S32 lastResult;
    F32 lastHitX;
    F32 lastHitZ;
    F32 lastDist;
    F32 hitX;
    F32 hitZ;
    F32 testDist;
    F32 distToTarget;
    F32 fireLerp;
    F32 closeDist;
    S32 closeIdx;
    xMat4x3 targetMat;
    xVec3 mirrorVec;

    fireSingle = NULL;
    if (model->Anim->Single->State->UserFlags & 0x100000)
    {
        fireSingle = model->Anim->Single;
    }
    else if (model->Anim->Single->Blend->State != NULL &&
             (model->Anim->Single->Blend->State->UserFlags & 0x100000))
    {
        fireSingle = model->Anim->Single->Blend;
    }

    distToTarget = SQR(fireTarget.x - model->Mat->pos.x) + SQR(fireTarget.z - model->Mat->pos.z);

    lastResult = FirePitchHit(this, 0, lastHitX, lastHitZ);
    lastDist = SQR(lastHitX - model->Mat->pos.x) + SQR(lastHitZ - model->Mat->pos.z);

    for (i = 1; i < 17; i++)
    {
        S32 result = FirePitchHit(this, i, hitX, hitZ);
        testDist = SQR(hitX - model->Mat->pos.x) + SQR(hitZ - model->Mat->pos.z);

        if (lastResult && result &&
            ((lastDist <= distToTarget && testDist >= distToTarget) ||
             (lastDist >= distToTarget && testDist <= distToTarget)))
        {
            lastDist = xsqrt(lastDist);
            testDist = xsqrt(testDist);
            distToTarget = xsqrt(distToTarget);

            if (xabs(lastDist - testDist) < 0.00001f)
            {
                fireLerp = 1.0f;
            }
            else
            {
                fireLerp = (distToTarget - lastDist) / (testDist - lastDist);
            }

            if (fireSingle != NULL)
            {
                fireSingle->BilinearLerp[0] = 0.0625f * ((i - 1.0f) + fireLerp);
            }

            lastHitX = xlerp(lastHitX, hitX, fireLerp);
            lastHitZ = xlerp(lastHitZ, hitZ, fireLerp);
            // goto: both builds jump straight past the closest-sample search.
            goto face;
        }

        lastHitX = hitX;
        lastHitZ = hitZ;
        lastResult = result;
        lastDist = testDist;
    }

    // No sample pair brackets the target distance: take the closest single sample.
    closeDist = 1e38f;
    closeIdx = -1;
    for (i = 0; i < 17; i++)
    {
        if (FirePitchHit(this, i, hitX, hitZ))
        {
            testDist =
                xabs(SQR(hitX - model->Mat->pos.x) + SQR(hitZ - model->Mat->pos.z) - distToTarget);
            if (testDist < closeDist)
            {
                closeDist = testDist;
                closeIdx = i;
                lastHitX = hitX;
                lastHitZ = hitZ;
            }
        }
    }

    if (fireSingle != NULL)
    {
        fireSingle->BilinearLerp[0] = 0.0625f * closeIdx;
    }

face:
    targetMat.at.x = fireTarget.x - model->Mat->pos.x;
    targetMat.at.y = 0.0f;
    targetMat.at.z = fireTarget.z - model->Mat->pos.z;
    targetMat.at.safe_normalize(xModelGetFrame(model)->at);
    targetMat.up = g_Y3;
    targetMat.pos = xModelGetFrame(model)->pos;
    xVec3Cross(&targetMat.right, &targetMat.up, &targetMat.at);

    mirrorVec.x = lastHitX;
    mirrorVec.y = fireTarget.y;
    mirrorVec.z = lastHitZ;
    xMat4x3Tolocal(&mirrorVec, xModelGetFrame(model), &mirrorVec);
    mirrorVec.x = -mirrorVec.x;
    xMat4x3Toworld(&fireFace, &targetMat, &mirrorVec);
}

void zNMENeptune::ParArrayInit()
{
    S32 i;
    S32 startNew;

    parCountTex = 0;
    parCountCol = 0;
    parCountSize = 0;

    startNew = 1;
    for (i = 0; i < 3; i++)
    {
        if (rwpartex != NULL)
        {
            parTex[i].tx = (Neptune::Par::Tex[i].tx + 2.0f) / rwpartex->raster->width;
            parTex[i].ty = (Neptune::Par::Tex[i].ty + 2.0f) / rwpartex->raster->width;
            parTex[i].tsize = (Neptune::Par::Tex[i].tsize - 4.0f) / rwpartex->raster->width;
        }
        else
        {
            parTex[i].tx = 0.0f;
            parTex[i].ty = 0.0f;
            parTex[i].tsize = 0.0f;
        }
        parTex[i].tnext = Neptune::Par::Tex[i].tnext;

        if (startNew)
        {
            parStartTex[parCountTex++] = i;
            startNew = 0;
        }

        if (Neptune::Par::Tex[i].tnext == 1.0f)
        {
            startNew = 1;
        }
    }

    startNew = 1;
    for (i = 0; i < 7; i++)
    {
        parCol[i] = Neptune::Par::Col[i];

        if (startNew)
        {
            parStartCol[parCountCol++] = i;
            startNew = 0;
        }

        if (Neptune::Par::Col[i].tnext == 1.0f)
        {
            parCol[i].tsub = 0.0f;
            parCol[i].tmul = 0.0f;
            startNew = 1;
        }
        else
        {
            parCol[i].tsub = Neptune::Par::Col[i].tnext;
            parCol[i].tmul = 1.0f / (Neptune::Par::Col[i + 1].tnext - Neptune::Par::Col[i].tnext);
            parCol[i].tnext = Neptune::Par::Col[i + 1].tnext;
        }
    }

    startNew = 1;
    for (i = 0; i < 8; i++)
    {
        parSize[i] = Neptune::Par::Size[i];

        if (startNew)
        {
            parStartSize[parCountSize++] = i;
            startNew = 0;
        }

        if (Neptune::Par::Size[i].tnext == 1.0f)
        {
            parSize[i].tsub = 0.0f;
            parSize[i].dsize = 0.0f;
            startNew = 1;
        }
        else
        {
            parSize[i].tsub = Neptune::Par::Size[i].tnext;
            parSize[i].dsize = (Neptune::Par::Size[i + 1].size - Neptune::Par::Size[i].size) /
                               (Neptune::Par::Size[i + 1].tnext - Neptune::Par::Size[i].tnext);
            parSize[i].tnext = Neptune::Par::Size[i + 1].tnext;
        }
    }
}

void zNMENeptune::ParEmitBunch(F32* emitVar, F32 emitRate, F32 dt,
                               U32 (*emitCB)(zNeptuneParticle*, void*, F32), void* context)
{
    if (dt < 0.00001f)
    {
        return;
    }

    F32 invEmitRate = 1.0f / emitRate;
    F32 invDt = 1.0f / dt;

    *emitVar += dt;
    *emitVar = MIN(*emitVar, 0.9999f * dt + invEmitRate);

    while (*emitVar >= invEmitRate)
    {
        *emitVar -= invEmitRate;

        if (parCount >= 1024)
        {
            return;
        }

        zNeptuneParticle* allocpar = &par[parCount];
        parCount++;
        allocpar->lifetime = 0.0f;

        U32 emitType = emitCB(allocpar, context, *emitVar * invDt);

        zNeptuneParType* ptype = &Neptune::Par::Type[emitType];
        allocpar->updtype = ptype->updtype;
        allocpar->liferate = 1.0f / ptype->lifetime;
        allocpar->texidx = parStartTex[ptype->texidx];
        allocpar->colidx = parStartCol[ptype->colidx];
        allocpar->sizeidx = parStartSize[ptype->sizeidx];

        ParUpdate(*emitVar, true);
    }
}

void zNMENeptune::ScorchUpdate(F32 dt)
{
    S32 i;

    for (i = 0; i < scorchCount; i++)
    {
        scorchList[i].timer += dt;
        if (scorchList[i].timer > 0.65f)
        {
            scorchList[i] = scorchList[scorchCount - 1];
            i--;
            scorchCount--;
        }
    }

    if (scorchCount != 0)
    {
        scorchArea = 0.0f;
        for (i = 0; i < scorchCount; i++)
        {
            scorchArea += scorchList[i].area;
        }

        ParEmitBunch(&scorchEmit, 200.0f, dt, Neptune::Par::EmitScorchCB, NULL);
    }
}

void zNMENeptune::ParUpdate(F32 dt, bool updatelast)
{
    xMat4x3* cammat;
    F32 camAtX;
    F32 camAtY;
    F32 camAtZ;
    F32 camD;
    F32 playerX;
    F32 playerY;
    F32 playerZ;
    S32 i;
    S32 sortcount;
    zNeptuneSortParticle* sortpar;
    F32 spiral_radrate;
    F32 spiral_radtarget;

    // The GC build tests each particle against the camera frustum unless the scope view is active
    // (the PS2 demo culls with VU0 code instead).
    bool scopeView = Neptune::me->ScopeViewActive();

    if (!updatelast)
    {
        beamParThisFrame = 0;
    }

    sortcount = 0;

    cammat = &xglobals->cam->mat;
    camAtX = 255.99219f * -cammat->at.x;
    camAtY = 255.99219f * -cammat->at.y;
    camAtZ = 255.99219f * -cammat->at.z;

    camD = 32767.0f - (camAtX * cammat->pos.x + camAtY * cammat->pos.y + camAtZ * cammat->pos.z);

    playerX = xglobals->___player_ent_dont_use_directly->model->Mat->pos.x;
    playerY = xglobals->___player_ent_dont_use_directly->model->Mat->pos.y +
              xglobals->___player_ent_dont_use_directly->bound.sph.r;
    playerZ = xglobals->___player_ent_dont_use_directly->model->Mat->pos.z;

    if (!updatelast)
    {
        sortpar = (zNeptuneSortParticle*)xMemScratchPush(1024 * sizeof(zNeptuneSortParticle), 0);
    }

    spiral_radrate = 0.00024414063f * cfg_neptune->fire_spiralRadiusRate;
    spiral_radtarget = 0.00024414063f * cfg_neptune->fire_spiralRadiusTarget;

    if (updatelast)
    {
        i = parCount - 1;
    }
    else
    {
        i = 0;
    }

    for (; i < parCount; i++)
    {
        F32 posX;
        F32 posY;
        F32 posZ;
        zNeptuneParticle* currpar;
        F32 currLife;

        currpar = &par[i];
        currLife = par[i].lifetime + par[i].liferate * dt;

        if (currLife >= 1.0f)
        {
            par[i] = par[parCount - 1];
            i--;
            parCount--;

            if (updatelast)
            {
                return;
            }

            continue;
        }

        par[i].lifetime = currLife;

        while (currLife > parTex[par[i].texidx].tnext)
        {
            par[i].texidx++;
        }

        while (currLife > parCol[par[i].colidx].tnext)
        {
            par[i].colidx++;
        }

        while (currLife > parSize[par[i].sizeidx].tnext)
        {
            par[i].sizeidx++;
        }

        posX = par[i].pos.x;
        posY = par[i].pos.y;
        posZ = par[i].pos.z;
        par[i].pos.x = posX + par[i].vel.x * dt;
        par[i].pos.y = posY + par[i].vel.y * dt;
        par[i].pos.z = posZ + par[i].vel.z * dt;

        switch (par[i].updtype)
        {
        case 0:
        {
            zNeptuneParticleBeamUser* beamuser;
            F32 lerp;

            if (!updatelast)
            {
                beamParThisFrame = 1;
            }

            beamuser = (zNeptuneParticleBeamUser*)currpar->userdata;
            lerp = currLife * beamuser->velRate;
            lerp = MIN(lerp, 1.0f);

            par[i].vel.x = beamuser->velStart.x * (1.0f - lerp) + beamuser->velEnd.x * lerp;
            par[i].vel.y = beamuser->velStart.y * (1.0f - lerp) + beamuser->velEnd.y * lerp;
            par[i].vel.z = beamuser->velStart.z * (1.0f - lerp) + beamuser->velEnd.z * lerp;

            if (SQR(playerX - posX) + SQR(playerY - posY) + SQR(playerZ - posZ) <
                SQR(cfg_neptune->fire_damageRadBeam))
            {
                gotPlayer++;
            }

            if (posY < cfg_neptune->fire_colHeight)
            {
                par[i].liferate *= 5.0f;
                par[i].updtype = 3;
                par[i].vel.x = 3.0f * xurand() - 1.5f;
                par[i].vel.y = 5.0f;
                par[i].vel.z = 3.0f * xurand() - 1.5f;
                par[i].pos.x = posX;
                par[i].pos.y = posY;
                par[i].pos.z = posZ;
                par[i].texidx = parStartTex[2];
                fireBeamGroundHit = 0.0f;
                fireBeamGroundLoc = par[i].pos;
            }
            break;
        }
        case 1:
        {
            zNeptuneParticleSpiralUser* spuser;
            F32 spiral_rad;
            F32 cnew;
            F32 snew;
            F32 cold;
            F32 sold;
            F32 deltadt;
            F32 bcx;
            F32 bcy;
            F32 bcz;
            F32 bsx;
            F32 bsy;
            F32 bsz;

            spuser = (zNeptuneParticleSpiralUser*)currpar->userdata;

            spiral_rad = spuser->rad;
            spiral_rad += spiral_radrate * dt;
            if (spiral_rad > spiral_radtarget)
            {
                spiral_rad = spiral_radtarget;
            }
            spuser->rad = spiral_rad;

            deltadt = spuser->delta * dt;

            cold = spuser->anglecs[0];
            sold = spuser->anglecs[1];
            cnew = cold - cold * deltadt - sold * deltadt;
            snew = sold - sold * deltadt + cold * deltadt;

            F32 rlen = xinvsqrt(cnew * cnew + snew * snew);

            bcx = spuser->basis[0];
            bcy = spuser->basis[1];
            bcz = spuser->basis[2];
            bsx = spuser->basis[3];
            bsy = spuser->basis[4];
            bsz = spuser->basis[5];

            cnew *= rlen;
            snew *= rlen;
            spuser->anglecs[0] = cnew;
            spuser->anglecs[1] = snew;

            cnew *= spiral_rad;
            snew *= spiral_rad;
            posX += cnew * bcx + snew * bsx;
            posY += cnew * bcy + snew * bsy;
            posZ += cnew * bcz + snew * bsz;

            if (posY < cfg_neptune->fire_colHeight)
            {
                zNeptuneParticleGravBounceUser* gbuser;

                par[i].liferate *= 2.0f;
                par[i].updtype = 2;
                par[i].vel.x = 0.2f * par[i].vel.x;
                par[i].vel.y = 5.0f;
                par[i].vel.z = 0.2f * par[i].vel.z;
                par[i].pos.x = posX;
                par[i].pos.y = posY;
                par[i].pos.z = posZ;

                gbuser = (zNeptuneParticleGravBounceUser*)currpar->userdata;
                gbuser->gravity = 20.0f;
                gbuser->bounceHeight = cfg_neptune->fire_colHeight;
            }
            break;
        }
        case 2:
        {
            zNeptuneParticleGravBounceUser* gbuser;

            gbuser = (zNeptuneParticleGravBounceUser*)currpar->userdata;
            par[i].vel.y -= dt * gbuser->gravity;

            if (posY < gbuser->bounceHeight && par[i].vel.y < 0.0f)
            {
                par[i].vel.y = -par[i].vel.y;
            }
            break;
        }
        }

        if (updatelast)
        {
            return;
        }

        zNeptuneParSize* npsize;
        F32 size;

        npsize = &parSize[par[i].sizeidx];
        size = npsize->dsize * (currLife - npsize->tsub) + npsize->size;

        if (!scopeView)
        {
            RwSphere worldsph;
            worldsph.center.x = posX;
            worldsph.center.y = posY;
            worldsph.center.z = posZ;
            worldsph.radius = size;

            if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph) == rwSPHEREOUTSIDE)
            {
                continue;
            }
        }

        F32 fDepth;
        U32 uLife;

        uLife = (U32)(currLife * 65535.48f);
        fDepth = camD + (camAtX * posX + camAtY * posY + camAtZ * posZ);
        fDepth = CLAMP(fDepth, 0.0f, 32767.0f);

        sortpar[sortcount].x.f = posX;
        sortpar[sortcount].y.f = posY;
        sortpar[sortcount].z.f = posZ;
        sortpar[sortcount].depthlife = ((U32)fDepth << 16) | uLife;
        sortpar[sortcount].x.u = par[i].texidx | (sortpar[sortcount].x.u & 0xFFFFFF00);
        sortpar[sortcount].y.u = par[i].colidx | (sortpar[sortcount].y.u & 0xFFFFFF00);
        sortpar[sortcount].z.u = par[i].sizeidx | (sortpar[sortcount].z.u & 0xFFFFFF00);
        sortcount++;
    }

    if (sortcount == 0)
    {
        xMemScratchPop(sortpar);
        return;
    }

    // The key offset is computed at runtime from sortpar, as retail does.
    quicksort_int(sortpar, sortcount, sizeof(zNeptuneSortParticle),
                  (UPtr)&sortpar->depthlife - (UPtr)sortpar);

    ptank_pool__pos_color_size_uv2 pool;
    pool.rs.texture = rwpartex;
    pool.rs.src_blend = rwBLENDSRCALPHA;
    pool.rs.dst_blend = rwBLENDINVSRCALPHA;
    pool.rs.flags = 0;
    pool.reset();

    for (i = 0; i < sortcount; i++)
    {
        pool.next();
        if (!pool.valid())
        {
            break;
        }

        F32 lifetime;
        zNeptuneParTex* ptex;
        zNeptuneParCol* pcol;
        F32 collerp;
        F32 invcollerp;
        zNeptuneParSize* psize;
        F32 interpsize;

        lifetime = (1.0f / 65535.0f) * (sortpar[i].depthlife & 0xFFFF);

        pool.pos->x = sortpar[i].x.f;
        pool.pos->y = sortpar[i].y.f;
        pool.pos->z = sortpar[i].z.f;

        ptex = &parTex[(U8)sortpar[i].x.u];
        pool.uv[0].x = ptex->tx;
        pool.uv[0].y = ptex->ty;
        pool.uv[1].x = ptex->tx + ptex->tsize;
        pool.uv[1].y = ptex->ty + ptex->tsize;

        pcol = &parCol[(U8)sortpar[i].y.u];
        collerp = pcol->tmul * (lifetime - pcol->tsub);
        invcollerp = 1.0f - collerp;
        pool.color->r = collerp * pcol[1].r + invcollerp * pcol->r;
        pool.color->g = collerp * pcol[1].g + invcollerp * pcol->g;
        pool.color->b = collerp * pcol[1].b + invcollerp * pcol->b;
        pool.color->a = collerp * pcol[1].a + invcollerp * pcol->a;

        psize = &parSize[(U8)sortpar[i].z.u];
        interpsize = psize->dsize * (lifetime - psize->tsub) + psize->size;
        pool.size->x = interpsize;
        pool.size->y = interpsize;
    }

    pool.flush();
    xMemScratchPop(sortpar);
}

// No GC symbol (always inlined); the name is ours. Slides a cross-section point along the beam
// direction onto the plane (norm, planeD).
static inline void BeamClipToPlane(xVec3* pt, const xVec3* dir, const xVec3* norm, F32 planeD)
{
    F32 d = xVec3Dot(dir, norm);
    if (xabs(d) < 0.00001f)
    {
        return;
    }

    F32 t = (planeD - xVec3Dot(pt, norm)) / d;
    pt->x += t * dir->x;
    pt->y += t * dir->y;
    pt->z += t * dir->z;
}

// The beam length along dir goes straight into texuscale, which is then turned into the u scale
// in place.
void zNMENeptune::RenderLightBeam(xVec3* start, xVec3* end, xVec3* dir, xVec3* startnorm,
                                  xVec3* endnorm, F32 radstart, F32 radend, xColor_tag color,
                                  F32 texu_start, F32 texu_end, F32 texv_start, F32 texv_end,
                                  S32 numseg, S32 numradial)
{
    S32 seg;
    S32 i;
    xMat4x3 beamBasis;
    xVec3 xsect[2][16];
    xVec3 xsectlerp[2][16];
    F32 anglescale;
    F32 startPlaneD;
    F32 endPlaneD;
    RxObjSpace3DVertex* currvert;
    RxObjSpace3DVertex* xsectStartVert;
    F32 invradial;
    F32 invseg;
    F32 texuscale;
    xVec3* xsectA;
    xVec3* xsectB;
    F32 seglerpA;
    F32 seglerpB;

    beamBasis.pos = *start;
    beamBasis.at = *dir;
    beamBasis.up = g_Y3;
    xVec3Cross(&beamBasis.right, &beamBasis.up, &beamBasis.at);
    xVec3Normalize(&beamBasis.right, &beamBasis.right);
    xVec3Cross(&beamBasis.up, &beamBasis.at, &beamBasis.right);

    anglescale = 6.2831855f / numradial;
    for (i = 0; i < numradial; i++)
    {
        F32 ang = i * anglescale;
        F32 c = icos(ang);
        F32 s = isin(ang);
        xsect[0][i].x =
            start->x + beamBasis.right.x * (radstart * c) + beamBasis.up.x * (radstart * s);
        xsect[0][i].y =
            start->y + beamBasis.right.y * (radstart * c) + beamBasis.up.y * (radstart * s);
        xsect[0][i].z =
            start->z + beamBasis.right.z * (radstart * c) + beamBasis.up.z * (radstart * s);
        xsect[1][i].x = end->x + beamBasis.right.x * (radend * c) + beamBasis.up.x * (radend * s);
        xsect[1][i].y = end->y + beamBasis.right.y * (radend * c) + beamBasis.up.y * (radend * s);
        xsect[1][i].z = end->z + beamBasis.right.z * (radend * c) + beamBasis.up.z * (radend * s);
    }

    if (startnorm == NULL)
    {
        startnorm = &beamBasis.at;
    }
    if (endnorm == NULL)
    {
        endnorm = &beamBasis.at;
    }

    startPlaneD = xVec3Dot(startnorm, start);
    endPlaneD = xVec3Dot(endnorm, end);
    for (i = 0; i < numradial; i++)
    {
        BeamClipToPlane(&xsect[0][i], dir, startnorm, startPlaneD);
        BeamClipToPlane(&xsect[1][i], dir, endnorm, endPlaneD);
    }

    currvert = gRenderArr.m_vertex;
    invradial = 1.0f / numradial;
    invseg = 1.0f / numseg;

    startPlaneD = xVec3Dot(start, dir);
    texuscale = xVec3Dot(end, dir) - xVec3Dot(start, dir);
    if (xabs(texuscale) < 0.00001f)
    {
        texuscale = 0.0f;
    }
    else
    {
        texuscale = (texu_end - texu_start) / texuscale;
    }

    for (seg = 0; seg < numseg; seg++)
    {
        seglerpA = seg * invseg;
        seglerpB = (seg + 1) * invseg;

        if (seg == 0)
        {
            xsectA = xsect[0];
        }
        else
        {
            for (i = 0; i < numradial; i++)
            {
                xVec3Lerp(&xsectlerp[0][i], &xsect[0][i], &xsect[1][i], seglerpA);
            }
            xsectA = xsectlerp[0];
        }

        if (seg == numseg - 1)
        {
            xsectB = xsect[1];
        }
        else
        {
            for (i = 0; i < numradial; i++)
            {
                xVec3Lerp(&xsectlerp[1][i], &xsect[0][i], &xsect[1][i], seglerpB);
            }
            xsectB = xsectlerp[1];
        }

        xsectStartVert = currvert;
        for (i = 0; i < numradial; i++)
        {
            RwIm3DVertexSetPos(currvert, xsectA->x, xsectA->y, xsectA->z);
            RwIm3DVertexSetRGBA(currvert, color.r, color.g, color.b, color.a);
            RwIm3DVertexSetUV(currvert,
                              texuscale * (xVec3Dot(xsectA, dir) - startPlaneD) + texu_start,
                              i * invradial * (texv_end - texv_start) + texv_start);
            currvert++;

            RwIm3DVertexSetPos(currvert, xsectB->x, xsectB->y, xsectB->z);
            RwIm3DVertexSetRGBA(currvert, color.r, color.g, color.b, color.a);
            RwIm3DVertexSetUV(currvert,
                              texuscale * (xVec3Dot(xsectB, dir) - startPlaneD) + texu_start,
                              i * invradial * (texv_end - texv_start) + texv_start);
            currvert++;

            xsectA++;
            xsectB++;
        }

        currvert[0] = xsectStartVert[0];
        currvert[1] = xsectStartVert[1];
        currvert[0].v = texv_end;
        currvert[1].v = texv_end;
        currvert += 2;
    }

    if (RwIm3DTransform(gRenderArr.m_vertex, currvert - gRenderArr.m_vertex, NULL,
                        rwIM3D_VERTEXUV | rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
    {
        RwIm3DRenderPrimitive(rwPRIMTYPETRISTRIP);
        RwIm3DEnd();
    }
}

void zNMENeptune::BucketCollideTransform(F32 scale)
{
    xMat4x3 bucketMat;
    xMat4x3* mat = (xMat4x3*)collideBucket->model->Mat;

    xMat4x3Mul(&bucketMat, (xMat4x3*)&bucketSkinMat, (xMat4x3*)&model->Mat[bucketInfl]);
    xMat4x3Mul(mat, &bucketMat, (xMat4x3*)model->Mat);

    mat->right = mat->right * 2.0f;
    mat->up = mat->up * 2.0f;
    mat->at = mat->at * 2.0f;

    if (scale != 1.0f)
    {
        xVec3 offset = (collideBucket->bound.box.box.upper + collideBucket->bound.box.box.lower) *
                       ((1.0f - scale) / 2.0f);
        xMat3x3RMulVec(&offset, mat, &offset);

        mat->right = mat->right * scale;
        mat->up = mat->up * scale;
        mat->at = mat->at * scale;
        mat->pos = mat->pos + offset;
    }
}

void zNMENeptune::BucketCollideUpdate()
{
    if (collideBucket != NULL)
    {
        if (!bucketCollideActive || (state & 0x1000))
        {
            xEntGetPos(collideBucket)->x = 0.0f;
            xEntGetPos(collideBucket)->y = 100.0f;
            xEntGetPos(collideBucket)->z = 0.0f;
        }
        else
        {
            BucketCollideTransform(state == 4 ? cfg_neptune->stun_bucketscale : 1.0f);
        }

        if (collideBucket->bupdate != NULL)
        {
            collideBucket->bupdate(collideBucket, xEntGetPos(collideBucket));
        }
        else
        {
            xEntDefaultBoundUpdate(collideBucket, xEntGetPos(collideBucket));
        }
    }
}

// defendRight is scaled by xinvsqrt(length2()) rather than normalize()d, for retail's register
// order.
void zNMENeptune::DefendCollideUpdate()
{
    xVec3 butt;
    xVec3 tip;
    xVec3 defendCurrVec;
    xVec3 defendForward;
    xVec3 defendRight;
    xVec3 defendUp;
    xMat4x3* defendMat;

    if (collideDefend == NULL)
    {
        return;
    }

    if (state != 5 || model->Anim->Single->Blend->State != NULL ||
        !(model->Anim->Single->State->UserFlags & 0x400000))
    {
        xEntGetPos(collideDefend)->x = 0.0f;
        xEntGetPos(collideDefend)->y = 100.0f;
        xEntGetPos(collideDefend)->z = 0.0f;
        defendLast = 0;
        return;
    }

    iModelTagEval(model->Data, &bossTag[1], model->Mat, &butt);
    iModelTagEval(model->Data, &bossTag[0], model->Mat, &tip);

    if (!defendLast)
    {
        xEntGetPos(collideDefend)->x = 0.0f;
        xEntGetPos(collideDefend)->y = 100.0f;
        xEntGetPos(collideDefend)->z = 0.0f;
        defendLast = 1;
        defendLastVec = tip - butt;
        return;
    }

    defendCurrVec = tip - butt;
    xVec3Cross(&defendForward, &defendCurrVec, &defendLastVec);
    if (xVec3Dot(&defendForward, (xVec3*)&model->Mat->at) < 0.0f)
    {
        defendForward = -defendForward;
    }
    defendForward.safe_normalize(*(xVec3*)&model->Mat->at);

    xVec3Cross(&defendRight, &defendForward, (xVec3*)&model->Mat->up);
    if (defendRight.length2() < 9.9999994e-11f)
    {
        xVec3Cross(&defendRight, &defendForward, (xVec3*)&model->Mat->right);
    }
    defendRight *= xinvsqrt(defendRight.length2());

    xVec3Cross(&defendUp, &defendRight, &defendForward);

    defendMat = (xMat4x3*)collideDefend->model->Mat;
    xVec3Lerp(&defendMat->pos, &butt, &tip, cfg_neptune->defend_spinCenter);
    defendMat->up = defendForward * cfg_neptune->defend_scale;
    defendMat->right = defendRight * cfg_neptune->defend_scale;
    defendMat->at = defendUp * cfg_neptune->defend_scale;

    defendLastVec = defendCurrVec;
}

// Retail bug kept: the else path also runs (and dereferences collideBody) when it is NULL.
void zNMENeptune::BodyCollideUpdate()
{
    if (cruise_bubble::active() && cruise_bubble::flying() && collideBody != NULL)
    {
        memcpy(collideBody->model->Mat, model->Mat,
               collideBody->model->BoneCount * sizeof(RwMatrix));
        collideBody->bupdate(collideBody, xEntGetPos(collideBody));
    }
    else
    {
        xEntGetPos(collideBody)->x = 0.0f;
        xEntGetPos(collideBody)->y = 100.0f;
        xEntGetPos(collideBody)->z = 0.0f;
        collideBody->bupdate(collideBody, xEntGetPos(collideBody));
    }
}

void zNMENeptune::PovCollideUpdate()
{
    if (state & 0x1000)
    {
        xEntGetPos(collidePOV)->x = xEntGetPos(this)->x;
        xEntGetPos(collidePOV)->y = xEntGetPos(xglobals->___player_ent_dont_use_directly)->y;
        xEntGetPos(collidePOV)->z = xEntGetPos(this)->z;
    }
    else
    {
        xEntGetPos(collidePOV)->x = 0.0f;
        xEntGetPos(collidePOV)->y = 100.0f;
        xEntGetPos(collidePOV)->z = 0.0f;
    }
}

void zNMENeptune::CollideLightning(RxObjSpace3DVertex* verts, U32 numVerts)
{
    U32 i;
    F32 playerX, playerY, playerZ;
    F32 currX, currY, currZ;
    F32 nextX, nextY, nextZ;
    F32 checkDist2;
    F32 ax, ay, az;
    F32 bx, by, bz;
    F32 dotAB;
    F32 dotBB;

    if (numVerts < 2)
    {
        return;
    }

    playerX = xglobals->___player_ent_dont_use_directly->model->Mat->pos.x;
    playerY = xglobals->___player_ent_dont_use_directly->model->Mat->pos.y +
              xglobals->___player_ent_dont_use_directly->bound.sph.r;
    playerZ = xglobals->___player_ent_dont_use_directly->model->Mat->pos.z;
    checkDist2 = SQR(cfg_neptune->fire_damageRadLightning);

    currX = 0.5f * (verts[0].x + verts[1].x);
    currY = 0.5f * (verts[0].y + verts[1].y);
    currZ = 0.5f * (verts[0].z + verts[1].z);

    for (i = 0; i < numVerts - 2; i += 2)
    {
        nextX = 0.5f * (verts[2].x + verts[3].x);
        nextY = 0.5f * (verts[2].y + verts[3].y);
        nextZ = 0.5f * (verts[2].z + verts[3].z);

        ax = playerX - currX;
        ay = playerY - currY;
        az = playerZ - currZ;
        bx = nextX - currX;
        by = nextY - currY;
        bz = nextZ - currZ;
        dotAB = ax * bx + ay * by + az * bz;

        if (dotAB <= 0.0f)
        {
            if (ax * ax + ay * ay + az * az < checkDist2)
            {
                gotPlayer++;
                return;
            }
        }
        else
        {
            dotBB = bx * bx + by * by + bz * bz;
            if (dotAB >= dotBB)
            {
                if (SQR(playerX - nextX) + SQR(playerY - nextY) + SQR(playerZ - nextZ) < checkDist2)
                {
                    gotPlayer++;
                    return;
                }
            }
            else if (dotBB > 0.00001f)
            {
                dotAB /= dotBB;
                ax -= dotAB * bx;
                ay -= dotAB * by;
                az -= dotAB * bz;
                if (ax * ax + ay * ay + az * az < checkDist2)
                {
                    gotPlayer++;
                    return;
                }
            }
        }

        currX = nextX;
        currY = nextY;
        currZ = nextZ;
        verts += 2;
    }
}

void zNMENeptune::TailSwipeUpdate()
{
    S32 i;
    F32 tailTimer;

    tailTimer = cfg_neptune->tailattack_time - tailRepeatTimer;

    if (!tailRepeatTimer || tailTimer > cfg_neptune->tailattack_ribstoptime)
    {
        for (i = 0; i < 2; i++)
        {
            if (tail_ribbon[i] != NULL)
            {
                zFXRibbonPoolDelete(tail_ribbon[i]);
            }
        }
    }
    else if (tailTimer > cfg_neptune->tailattack_ribstarttime)
    {
        for (i = 0; i < 2; i++)
        {
            if (tail_ribbon[i] == NULL)
            {
                tail_ribbon[i] = zFXRibbonPoolNew();
                if (tail_ribbon[i] != NULL)
                {
                    Neptune::Ribbon::rib_tailswipe.ConfigRibbon(tail_ribbon[i]);
                }
            }
        }
    }

    if (tailRepeatTimer)
    {
        if (tailTimer >= cfg_neptune->tailattack_bubstarttime &&
            tailTimer <= cfg_neptune->tailattack_bubstoptime)
        {
            S32 emitted;
            xVec3 posEmit[20];
            xVec3 velEmit[20];
            F32 normrand;
            F32 dist;

            dist = xVec3Dist(&currTag[4], &lastTag[4]) + xVec3Dist(&currTag[5], &lastTag[5]) +
                   xVec3Dist(&currTag[6], &lastTag[6]);
            tail_bubcounter += 0.33333334f * dist * cfg_neptune->tailattack_bubdensity;

            while (tail_bubcounter > 1.0f)
            {
                for (emitted = 0; emitted < 20 && tail_bubcounter > 1.0f;
                     emitted++, tail_bubcounter -= 1.0f)
                {
                    xVec3 currPos;
                    xVec3 lastPos;
                    F32 tagLerp;

                    normrand = xurand();
                    tagLerp = 2.0f * xurand();
                    if (tagLerp >= 1.0f)
                    {
                        xVec3Lerp(&currPos, &currTag[5], &currTag[6], tagLerp - 1.0f);
                        xVec3Lerp(&lastPos, &lastTag[5], &lastTag[6], tagLerp - 1.0f);
                    }
                    else
                    {
                        xVec3Lerp(&currPos, &currTag[4], &currTag[6], tagLerp);
                        xVec3Lerp(&lastPos, &lastTag[4], &lastTag[6], tagLerp);
                    }
                    xVec3Lerp(&posEmit[emitted], &currPos, &lastPos, normrand);

                    xVec3Normalize(&velEmit[emitted], &posEmit[emitted]);
                    if (velEmit[emitted].y < 0.0f)
                    {
                        velEmit[emitted].y = 0.0f;
                    }
                    velEmit[emitted].y *= 0.1f;
                    velEmit[emitted] = velEmit[emitted] * cfg_neptune->tailattack_bubspeed;
                }

                zParPTankSpawnBubbles(posEmit, velEmit, emitted, cfg_neptune->tailattack_bubscale);
            }
        }

        if (tail_ribbon[0] != NULL || tail_ribbon[1] != NULL)
        {
            xVec3 tipCenter;
            xVec3 neckTop;
            xVec3 neckBottom;
            xVec3 perpVec;

            tipCenter = currTag[6];
            neckTop = currTag[7];
            neckBottom = currTag[8];
            perpVec.x = tipCenter.x - 0.5f * (neckTop.x + neckBottom.x);
            perpVec.y = tipCenter.y - 0.5f * (neckTop.y + neckBottom.y);
            perpVec.z = tipCenter.z - 0.5f * (neckTop.z + neckBottom.z);
            xVec3Normalize(&perpVec, &perpVec);

            for (i = 0; i < 2; i++)
            {
                if (tail_ribbon[i] != NULL)
                {
                    xVec3 ribpos;
                    xFXRibbon* rib;
                    NMEHelpRib::RibData* ribhelp;

                    ribpos = currTag[i + 4];
                    rib = tail_ribbon[i];
                    ribhelp = &Neptune::Ribbon::rib_tailswipe;
                    if (ribhelp->flags.dev_enable)
                    {
                        ribhelp->ConfigRibbon(rib);
                    }
                    rib->insert(ribpos, perpVec, ribhelp->updinfo.scl_master,
                                ribhelp->updinfo.alf_master, 0);
                }
            }
        }

        if (!tailAttackKnocked && model->Anim->Single->Blend->State == NULL &&
            (model->Anim->Single->State->UserFlags & 0x200000))
        {
            const F32* tailtable;
            F32 tailframe;
            F32 taillerp;
            F32 tailangle;
            F32 vecleftX;
            F32 vecleftZ;
            F32 dotAt;
            F32 dotLeft;
            F32 playerX;
            F32 playerZ;
            S32 tailidx;
            F32 dist;

            if (strcmp(model->Anim->Single->State->Name, "TailLeft") == 0)
            {
                tailtable = cfg_neptune->tailleft_angles;
                vecleftX = xEntGetFrame(this)->right.x;
                vecleftZ = xEntGetFrame(this)->right.z;
            }
            else
            {
                tailtable = cfg_neptune->tailright_angles;
                vecleftX = -xEntGetFrame(this)->right.x;
                vecleftZ = -xEntGetFrame(this)->right.z;
            }

            tailframe = 30.0f * tailTimer;
            tailframe = MIN(tailframe, 6.999f);
            tailidx = (S32)tailframe;
            taillerp = tailframe - tailidx;
            tailangle = (1.0f - taillerp) * tailtable[tailidx] + taillerp * tailtable[tailidx + 1];

            playerX =
                xglobals->___player_ent_dont_use_directly->model->Mat->pos.x - model->Mat->pos.x;
            playerZ =
                xglobals->___player_ent_dont_use_directly->model->Mat->pos.z - model->Mat->pos.z;
            dotAt = model->Mat->at.x * playerX + model->Mat->at.z * playerZ;
            dotLeft = vecleftX * playerX + vecleftZ * playerZ;
            dist = xsqrt(playerX * playerX + playerZ * playerZ);
            if (dist < 0.00001f)
            {
                tailAttackKnocked = 1;
            }
            else
            {
                dist = 1.0f / dist;
                dotAt *= dist;
                dotLeft *= dist;
                if (tailangle < 180.0f)
                {
                    if (dotLeft > 0.0f && dotAt < icos((PI / 180.0f) * (180.0f - tailangle)))
                    {
                        tailAttackKnocked = 1;
                    }
                }
                else if (dotLeft > 0.0f || dotAt > icos((PI / 180.0f) * (tailangle - 180.0f)))
                {
                    tailAttackKnocked = 1;
                }
            }

            {
                // The tail direction of a debug draw compiled out of the final build; only the
                // trig calls remain.
                xVec3 tailDir;
                xVec3 tailDirDraw;

                tailDir.x = isin((PI / 180.0f) * tailangle);
                tailDir.y = 0.0f;
                tailDir.z = icos((PI / 180.0f) * tailangle);
                tailDirDraw.x = isin((PI / 180.0f) * tailangle);
                tailDirDraw.y = 0.0f;
                tailDirDraw.z = icos((PI / 180.0f) * tailangle);
            }

            if (tailAttackKnocked)
            {
                Neptune::PlayerKnockBack(cfg_neptune->tailattack_kbvel,
                                         cfg_neptune->tailattack_kbtime);
            }
        }
    }
}

void zNMENeptune::FireFXUpdate(F32 dt)
{
    S32 i;

    if (state == 2)
    {
        bool inFireLoop = (model->Anim->Single->State->UserFlags & 0x100000) &&
                          model->Anim->Single->Blend->State == NULL;
        xVec3 worldTipAvg;

        if (inFireLoop)
        {
            RwMatrixTag tmproot = *model->Mat;
            xMat4x3Identity((xMat4x3*)model->Mat);
            iModelTagEval(model->Data, &bossTag[0], model->Mat, &fireTipAvg[fireTipAvgIdx]);
            *model->Mat = tmproot;

            fireTipAvgIdx++;
            fireTipAvgCount++;
            if (fireTipAvgIdx >= cfg_neptune->fire_tipAvgNum)
            {
                fireTipAvgIdx = 0;
            }
            if (fireTipAvgCount > cfg_neptune->fire_tipAvgNum)
            {
                fireTipAvgCount = cfg_neptune->fire_tipAvgNum;
            }

            worldTipAvg = fireTipAvg[0];
            for (i = 1; i < (S32)fireTipAvgCount; i++)
            {
                worldTipAvg += fireTipAvg[i];
            }
            worldTipAvg *= 1.0f / fireTipAvgCount;
            xMat4x3Toworld(&worldTipAvg, (xMat4x3*)model->Mat, &worldTipAvg);
        }

        xVec3 targetdir;
        xVec3 tridentdir;
        targetdir = fireTarget - currTag[1];
        xVec3Normalize(&targetdir, &targetdir);
        tridentdir = currTag[0] - currTag[1];
        xVec3Normalize(&tridentdir, &tridentdir);

        EmitContext emitctx;
        emitctx.oldtip = lastTag[0];
        emitctx.tip = currTag[0];
        emitctx.dir = targetdir;

        if (fireTipAvgCount != 0)
        {
            F32 avgDot = xVec3Dot(&targetdir, &worldTipAvg);
            emitctx.oldtip += targetdir * (avgDot - xVec3Dot(&targetdir, &emitctx.oldtip));
            emitctx.tip += targetdir * (avgDot - xVec3Dot(&targetdir, &emitctx.tip));
        }

        xVec3Cross(&emitctx.fbasisX, &targetdir, &g_Y3);
        xVec3Cross(&emitctx.fbasisY, &emitctx.fbasisX, &targetdir);
        xVec3Normalize(&emitctx.fbasisX, &emitctx.fbasisX);
        xVec3Normalize(&emitctx.fbasisY, &emitctx.fbasisY);
        emitctx.basis[0] = emitctx.fbasisX.x * 4096.0f;
        emitctx.basis[1] = emitctx.fbasisX.y * 4096.0f;
        emitctx.basis[2] = emitctx.fbasisX.z * 4096.0f;
        emitctx.basis[3] = emitctx.fbasisY.x * 4096.0f;
        emitctx.basis[4] = emitctx.fbasisY.y * 4096.0f;
        emitctx.basis[5] = emitctx.fbasisY.z * 4096.0f;

        if (inFireLoop && bossStage == 0 && fireTipAvgCount > 1)
        {
            if (fireTimer == -1.0f)
            {
                fireTimer = 0.0f;
            }
            else
            {
                fireTimer += dt;
            }

            S32 oldParCount = parCount;
            ParEmitBunch(&fireBeamEmit, cfg_neptune->fire_beamEmitRate, dt,
                         Neptune::Par::EmitBeamCB, &emitctx);

            if (!fireBeamParLaunched && parCount != oldParCount)
            {
                fireBeamParLaunched = 1;
                fireBeamParPos = par[parCount - 1].pos;
                fireBeamParVel = par[parCount - 1].vel;
                SndLoop3D(Neptune::eSOUND_FireHoseLoop, &fireBeamParPos, NULL);
                SndPlay3D(Neptune::eSOUND_FireCharge, (xVec3*)&model->Mat->pos, 1.0f);
            }

            emitctx.tine = currTag[2];
            ParEmitBunch(&fireSpiralTineEmit[0], cfg_neptune->fire_spiralEmitRate, dt,
                         Neptune::Par::EmitSpiralCB, &emitctx);
            emitctx.tine = currTag[3];
            ParEmitBunch(&fireSpiralTineEmit[1], cfg_neptune->fire_spiralEmitRate, dt,
                         Neptune::Par::EmitSpiralCB, &emitctx);
        }

        if (inFireLoop && bossStage >= 1)
        {
            const NMECfgNeptuneAttack* currAtk = &cfg_neptune->stageAtk[bossStage];

            if (ltTimer == -1.0f)
            {
                ltTimer = 0.0f;
            }
            else
            {
                ltTimer += dt;
            }

            _tagLightningAdd ltadd;
            memset(&ltadd, 0, sizeof(_tagLightningAdd));
            ltadd.type = 3;
            ltadd.setup_degrees = 0.0f;
            ltadd.move_degrees = 0.0f;
            ltadd.rot_radius = 0.15f;
            ltadd.total_points = 16;
            ltadd.end_points = 0;
            ltadd.time = 20.0f;
            ltadd.arc_height = 0.5f;
            ltadd.thickness = 0.25f;
            ltadd.segmentsPerMeter = 2.5f;
            ltadd.color = g_PIMP_GOLD;
            ltadd.rand_radius = 1.0f;
            ltadd.zeus_normal_offset = 0.75f;
            ltadd.zeus_back_offset = 0.2f;
            ltadd.zeus_side_offset = 0.0f;
            ltadd.branchSpeed = 0.0f;
            ltadd.mainTexture = 0x24e99e06;
            ltadd.branchTexture = 0x24e99e06;
            ltadd.damage = 0;
            ltadd.knockBackSpeed = 5.0f;
            ltadd.collisionEnabled = 0;
            ltadd.isMain = 1;

            if (ltBeam[0] == NULL)
            {
                ltadd.flags = 0xa10;
                ltadd.start = &fireTarget;
                ltadd.end = &currTag[0];
                ltBeam[0] = zLightningAdd(&ltadd);
                ltBeam[0]->renderCB = Neptune::LightningBeamRenderCB;
                ltBeam[0]->context = NULL;
            }

            F32 beamlerp = ltTimer / cfg_neptune->fire_ltTimeBeamEnd;
            beamlerp = CLAMP(beamlerp, 0.05f, 1.0f);
            xVec3Lerp(&ltBeamTarget, &currTag[0], &fireTarget, beamlerp);
            zLightningModifyEndpoints(ltBeam[0], &ltBeamTarget, &currTag[0]);

            if (ltTimer == 0.0f)
            {
                SndLoop3D(Neptune::eSOUND_FireLightningLoop, &ltBeamTarget, NULL);
                SndPlay3D(Neptune::eSOUND_FireCharge, (xVec3*)&model->Mat->pos, 1.0f);
            }

            if (ltTimer > cfg_neptune->fire_ltTimeFeelerStart)
            {
                if (ltFeeler[0] == NULL)
                {
                    for (i = 0; i < currAtk->feeler_count; i++)
                    {
                        F32 feelAngle = PI * i / currAtk->feeler_count;

                        ltFeelerTargets[i * 2].x =
                            currAtk->feeler_length * icos(feelAngle) + fireTarget.x;
                        ltFeelerTargets[i * 2].y = fireTarget.y;
                        ltFeelerTargets[i * 2].z =
                            currAtk->feeler_length * isin(feelAngle) + fireTarget.z;
                        ltFeelerTargets[i * 2 + 1].x =
                            fireTarget.x - currAtk->feeler_length * icos(feelAngle);
                        ltFeelerTargets[i * 2 + 1].y = fireTarget.y;
                        ltFeelerTargets[i * 2 + 1].z =
                            fireTarget.z - currAtk->feeler_length * isin(feelAngle);

                        ltadd.flags = 0x10;
                        ltadd.start = &ltFeelerTargets[i * 2];
                        ltadd.end = &ltFeelerTargets[i * 2 + 1];
                        ltFeeler[i] = zLightningAdd(&ltadd);
                        ltFeeler[i]->renderCB = Neptune::LightningFeelerRenderCB;
                        ltFeeler[i]->context = (void*)i;
                    }
                }

                F32 feelerlerp =
                    (ltTimer - cfg_neptune->fire_ltTimeFeelerStart) /
                    (cfg_neptune->fire_ltTimeFeelerEnd - cfg_neptune->fire_ltTimeFeelerStart);
                feelerlerp = CLAMP(feelerlerp, 0.05f, 1.0f);

                xMat4x3 rotmat;
                xMat3x3RotC(&rotmat, 0.0f, 1.0f, 0.0f,
                            (PI / 180.0f) * currAtk->feeler_rotation * dt);
                xMat3x3RMulVec(&rotmat.pos, &rotmat, &fireTarget);
                rotmat.pos.x = fireTarget.x - rotmat.pos.x;
                rotmat.pos.y = fireTarget.y - rotmat.pos.y;
                rotmat.pos.z = fireTarget.z - rotmat.pos.z;

                for (i = 0; i < currAtk->feeler_count; i++)
                {
                    S32 j;
                    for (j = 0; j < 2; j++)
                    {
                        xMat4x3Toworld(&ltFeelerTargets[i * 2 + j], &rotmat,
                                       &ltFeelerTargets[i * 2 + j]);
                    }

                    xVec3 start;
                    xVec3 end;
                    start.x = xlerp(fireTarget.x, ltFeelerTargets[i * 2].x, feelerlerp);
                    start.y = xlerp(fireTarget.y, ltFeelerTargets[i * 2].y, feelerlerp);
                    start.z = xlerp(fireTarget.z, ltFeelerTargets[i * 2].z, feelerlerp);
                    end.x = xlerp(fireTarget.x, ltFeelerTargets[i * 2 + 1].x, feelerlerp);
                    end.y = xlerp(fireTarget.y, ltFeelerTargets[i * 2 + 1].y, feelerlerp);
                    end.z = xlerp(fireTarget.z, ltFeelerTargets[i * 2 + 1].z, feelerlerp);
                    zLightningModifyEndpoints(ltFeeler[i], &start, &end);
                }
            }

            static xVec3 ringEnds[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };

            if (ltTimer >= cfg_neptune->fire_ltTimeRingStart)
            {
                if (ltRingStarted < currAtk->ring_count &&
                    ltTimer >=
                        ltRingStarted * currAtk->ring_delay + cfg_neptune->fire_ltTimeRingStart)
                {
                    zLightningRing* currLtRing = &ltRing[ltRingStarted];
                    ltRingStarted++;
                    if (currLtRing->lt == NULL)
                    {
                        ltadd.flags = 0x200;
                        ltadd.start = &ringEnds[0];
                        ltadd.end = &ringEnds[1];
                        ltadd.time = currAtk->ring_lifetime;
                        ltadd.segmentsPerMeter = 0.8f;
                        currLtRing->lt = zLightningAdd(&ltadd);
                        currLtRing->lt->renderCB = Neptune::LightningRingRenderCB;
                        currLtRing->lt->context = currLtRing;
                        currLtRing->rad = currAtk->ring_minrad;
                        currLtRing->radvel = 0.0f;
                        currLtRing->startTime = ltTimer;
                        xMat4x3Identity(&currLtRing->mat);
                        currLtRing->mat.pos = emitctx.tip;
                        SndLoop3D(Neptune::eSOUND_FireRingLoop, &currLtRing->mat.pos, currLtRing);
                    }
                }

                for (i = 0; i < ltRingStarted; i++)
                {
                    zLightningRing* currLtRing = &ltRing[i];
                    F32 poslerp =
                        cfg_neptune->fire_ltRingLerpRate * (ltTimer - currLtRing->startTime);
                    poslerp = MIN(poslerp, 1.0f);
                    xVec3Lerp(&currLtRing->mat.pos, &emitctx.tip, &fireTarget, poslerp);
                    if (poslerp == 1.0f)
                    {
                        currLtRing->radvel = currAtk->ring_speed;
                    }
                    currLtRing->rad += currLtRing->radvel * dt;
                    if (currLtRing->rad > currAtk->ring_maxrad)
                    {
                        currLtRing->rad = currAtk->ring_maxrad;
                    }

                    xVec3 tempEnd;
                    tempEnd.x = 6.2831855f * currLtRing->rad;
                    tempEnd.y = 0.0f;
                    tempEnd.z = 0.0f;
                    zLightningModifyEndpoints(currLtRing->lt, &ringEnds[0], &tempEnd);
                }
            }
        }
    }
    else
    {
        if (ltBeam[0] != NULL)
        {
            zLightningKill(ltBeam[0]);
            ltBeam[0] = NULL;
        }

        for (i = 0; i < 12; i++)
        {
            if (ltFeeler[i] != NULL)
            {
                zLightningKill(ltFeeler[i]);
                ltFeeler[i] = NULL;
            }
        }

        for (i = 0; i < 12; i++)
        {
            if (ltRing[i].lt != NULL)
            {
                zLightningKill(ltRing[i].lt);
                ltRing[i].lt = NULL;
            }
        }
    }
}

void zNMENeptune::PaintDecalOnPlayer()
{
    xDecal::emit_context ec;

    memset(&ec, 0, sizeof(ec));
    ec.owner = 'NEPT';
    ec.flags = 8;
    ec.priority = 0;
    ec.life = 2.0f;
    ec.max_dist = 50.0f;
    ec.size.x = ec.size.y = 0.35f;
    ec.blend_mode = 0;
    ec.texture = rwdecaltex;
    ec.uv[0].x = ec.uv[0].y = 0.0f;
    ec.uv[1].x = ec.uv[1].y = 1.0f;
    ec.curve = &Neptune::Decal::curve_test;
    ec.emitAtomicCB = Neptune::DecalEmitCB;
    ec.destAtomicCB = Neptune::DecalDestCB;
    xDecal::emit(ec, povBeamMat, 0x10);
}

// GameCube-only (the PS2 demo does this inline in Process); local names follow Process's.
F32 zNMENeptune::KrabAimDistance2(xMat4x3* mat)
{
    xVec3 krabspos;
    xVec3 beamstart;
    xVec3 beamend;
    xIsect isx;

    if (entMrKrabs != NULL)
    {
        xMat4x3Toworld(&beamstart, mat, &cfg_neptune->projPOVTag[0]);
        beamend.x = 20.0f * mat->at.x + beamstart.x;
        beamend.y = 20.0f * mat->at.y + beamstart.y;
        beamend.z = 20.0f * mat->at.z + beamstart.z;
        xMat4x3Toworld(&krabspos, (xMat4x3*)entMrKrabs->model->Mat,
                       &cfg_neptune->pov_krabsTargetPoint);
        xLine3VecDist2(&beamstart, &beamend, &krabspos, &isx);
        return isx.dist;
    }

    return 1e10f;
}

bool zNMENeptune::PlayerGotPoked(xVec3* tri, F32 dist)
{
    F32 s, t;
    xVec3 closest;

    xTri3VecMinDist(&tri[0], &tri[1], &tri[2],
                    xEntGetCenter(xglobals->___player_ent_dont_use_directly), s, t);

    closest.x = tri[0].x + s * (tri[1].x - tri[0].x) + t * (tri[2].x - tri[0].x);
    closest.y = tri[0].y + s * (tri[1].y - tri[0].y) + t * (tri[2].y - tri[0].y);
    closest.z = tri[0].z + s * (tri[1].z - tri[0].z) + t * (tri[2].z - tri[0].z);

    return xVec3Dist2(xEntGetCenter(xglobals->___player_ent_dont_use_directly), &closest) <
           dist * dist;
}

void zNMENeptune::BossCamSetTargets()
{
    if (state == 3)
    {
        F32 dx = xglobals->___player_ent_dont_use_directly->model->Mat->pos.x - model->Mat->pos.x;
        F32 dz = xglobals->___player_ent_dont_use_directly->model->Mat->pos.z - model->Mat->pos.z;
        F32 dist = xsqrt(dx * dx + dz * dz);
        F32 outlerp;

        if (dist < 0.00001f)
        {
            dx = 0.0f;
            dz = 1.0f;
        }
        else
        {
            dx *= 1.0f / dist;
            dz *= 1.0f / dist;
        }

        xVec3 dest;
        outlerp = timer / cfg_neptune->outrageCam_lerptime;
        dest.x = dx * cfg_neptune->outrageCam_dist + model->Mat->pos.x;
        dest.y = model->Mat->pos.y + cfg_neptune->outrageCam_height;
        dest.z = dz * cfg_neptune->outrageCam_dist + model->Mat->pos.z;

        outlerp = MIN(outlerp, 1.0f);
        xVec3Lerp(&bossPlayerTarget, xEntGetPos(xglobals->___player_ent_dont_use_directly), &dest,
                  outlerp);
        bossBossTarget = *xEntGetPos(this);
    }
    else
    {
        bossPlayerTarget = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
        bossBossTarget = *xEntGetPos(this);
    }

    bosscam.set_targets(bossPlayerTarget, bossBossTarget, 5.0f);
}

U8 zNMENeptune::BeamCollide(xRay3* beamray, S32* aimedAtKrabs, xVec3* reflectnorm)
{
    xIsect isx;
    xCollis beamcollis;
    xCollis boundcollis;
    xSphere krabSph;
    S32 i;
    xEnt* reflent;

    beamcollis.flags = 0x200;
    if (xRayHitsEnv(beamray, xglobals->sceneCur->env, &beamcollis))
    {
        beamray->max_t = beamcollis.dist;
    }

    boundcollis.flags = 0;
    beamcollis.flags = 0x200;
    xRayHitsBound(beamray, &entMrKrabs->bound, &boundcollis);
    if ((boundcollis.flags & 1) && xRayHitsModel(beamray, entMrKrabs->model, &beamcollis))
    {
        beamray->max_t = beamcollis.dist;
        *aimedAtKrabs = 1;
        return 0;
    }

    if (*aimedAtKrabs)
    {
        xMat4x3Toworld(&krabSph.center, (xMat4x3*)entMrKrabs->model->Mat,
                       &cfg_neptune->pov_krabsTargetPoint);
        krabSph.r = 1.001f * cfg_neptune->pov_krabsTargetRad;
        iSphereIsectRay(&krabSph, beamray, &isx);
        if (isx.penned < 0.0f)
        {
            beamray->max_t = isx.dist;
        }
        return 0;
    }

    for (i = 0; i < 5; i++)
    {
        reflent = entReflectors[i];
        boundcollis.flags = 0;
        beamcollis.flags = 0x200;
        xRayHitsBound(beamray, &reflent->bound, &boundcollis);
        if ((boundcollis.flags & 1) && xRayHitsModel(beamray, reflent->model, &beamcollis))
        {
            beamray->max_t = beamcollis.dist;
            xMat3x3RMulVec(reflectnorm, (xMat4x3*)reflent->model->Mat, &beamcollis.norm);
            xVec3Normalize(reflectnorm, reflectnorm);
            return 1;
        }
    }

    return 0;
}

// The PS2 demo also calls rwPDS_HICloneEnable on a PS2-only asset here (its 'atomic' local).
void zNMENeptune::Setup()
{
    S32 i;
    S32 currIndex;
    xModelInstance* minst;
    S32 tagidx;
    S32 whichCollideBody;
    S32 pitchIdx;
    xAnimState* astate;
    RwMatrixTag tempmat[64];
    xQuat tempquat[64];
    xVec3 temptran[64];
    F32 bilinear[2];
    xVec3 butt;
    xVec3 tip;
    S32 samp;

    currIndex = 0;
    minst = model;
    while (minst != NULL)
    {
        bossModel[currIndex] = minst;
        currIndex++;
        minst = minst->Next;
    }

    for (tagidx = 0; tagidx < 13; tagidx++)
    {
        iModelTagSetup(&bossTag[tagidx],
                       bossModel[cfg_neptune->bossTagInit[tagidx].modelIndex]->Data,
                       2.0f * cfg_neptune->bossTagInit[tagidx].tagpos.x,
                       2.0f * cfg_neptune->bossTagInit[tagidx].tagpos.y,
                       2.0f * cfg_neptune->bossTagInit[tagidx].tagpos.z);
    }

    for (tagidx = 0; tagidx < 4; tagidx++)
    {
        iModelTagSetup(&playerMidSect[tagidx], globals.player.sb_models[0]->Data,
                       cfg_neptune->playerMidSectInit[tagidx].x,
                       cfg_neptune->playerMidSectInit[tagidx].y,
                       cfg_neptune->playerMidSectInit[tagidx].z);
    }

    collideBucket = (xEnt*)zSceneFindObject(0xD599A2ED);
    collideDefend = (xEnt*)zSceneFindObject(0xB42C2FE9);
    collideBody = (xEnt*)zSceneFindObject(0x418DD3D5);
    collidePOV = (xEnt*)zSceneFindObject(0x1025F864);
    entDestructFloor = (xEnt*)zSceneFindObject(0xE2BA79D0);
    entMrKrabs = (xEnt*)zSceneFindObject(0xE3EB0801);
    entReflectors[0] = (xEnt*)zSceneFindObject(0xA9BD3542);
    entReflectors[1] = (xEnt*)zSceneFindObject(0xA9BD3543);
    entReflectors[2] = (xEnt*)zSceneFindObject(0xA9BD3544);
    entReflectors[3] = (xEnt*)zSceneFindObject(0xA9BD3545);
    entReflectors[4] = (xEnt*)zSceneFindObject(0xA9BD3546);
    entMinions[0] = (xEnt*)zSceneFindObject(0x47ADFEB9);
    entMinions[1] = (xEnt*)zSceneFindObject(0x47ADFEBA);
    entMinions[2] = (xEnt*)zSceneFindObject(0x47ADFEBB);
    entMinions[3] = (xEnt*)zSceneFindObject(0x288F31A4);
    entMinions[4] = (xEnt*)zSceneFindObject(0x288F31A5);
    entMinions[5] = (xEnt*)zSceneFindObject(0x288F31A6);
    baseBeamFlythru = zSceneFindObject(0x832BF13A);
    underFloorJSP = (xJSPHeader*)xSTFindAsset(0x56320732, NULL);
    deflectSFX = (_zScript*)zSceneFindObject(0xD1E3A7E1);

    for (i = 0; i < 6; i++)
    {
        if (entMinions[i] != NULL)
        {
            ((zNMECommon*)entMinions[i])->DuploOwner(this);
        }
    }

    collideBucket->moreFlags |= 0x10;
    collideBucket->eventFunc = Neptune::BucketCollideEventFunc;

    whichCollideBody = 4;
    while (whichCollideBody != 0 && collideBody->model->Next != NULL)
    {
        collideBody->model = collideBody->model->Next;
        whichCollideBody--;
    }
    collideBody->collModel = collideBody->model;
    xModelAnimCollStart(*collideBody->model);

    collideBody->bound.type = XBOUND_TYPE_SPHERE;
    collideBody->bound.sph.center.x = 0.0f;
    collideBody->bound.sph.center.y = 2.0f;
    collideBody->bound.sph.center.z = 0.0f;
    collideBody->bound.sph.r = 6.0f;
    collideBody->bupdate = Neptune::BodyCollideBoundUpdateCB;

    collidePOV->bound.type = XBOUND_TYPE_SPHERE;
    collidePOV->bound.sph.center.x = 0.0f;
    collidePOV->bound.sph.center.y = 100.0f;
    collidePOV->bound.sph.center.z = 0.0f;
    collidePOV->bound.sph.r = 10.0f;
    collidePOV->bupdate = Neptune::PovCollideBoundUpdateCB;

    Neptune::Ribbon::rib_tailswipe.FindTexture();

    zNMECommon::Setup();

    astate = xAnimTableGetState(model->Anim->Table, "AttackLoop");

    xMat4x3Identity((xMat4x3*)&tempmat[0]);
    for (pitchIdx = 0; pitchIdx <= 16; pitchIdx++)
    {
        bilinear[0] = 0.0625f * pitchIdx;
        bilinear[1] = 0.0f;
        xAnimFileEval(astate->Data, 0.0f, bilinear, 0, temptran, tempquat, NULL);
        iModelAnimMatrices(model->Data, tempquat, temptran, &tempmat[1]);
        iModelTagEval(model->Data, &bossTag[1], tempmat, &butt);
        iModelTagEval(model->Data, &bossTag[0], tempmat, &tip);

        firePitchStart[pitchIdx] = tip;
        firePitchDir[pitchIdx] = tip - butt;
        F32 len = firePitchDir[pitchIdx].normalize();

        for (samp = 1; samp < 20; samp++)
        {
            xAnimFileEval(astate->Data, 0.05f * (samp * astate->Data->Duration), bilinear, 0,
                          temptran, tempquat, NULL);
            iModelAnimMatrices(model->Data, tempquat, temptran, &tempmat[1]);
            iModelTagEval(model->Data, &bossTag[0], tempmat, &tip);
            firePitchStart[pitchIdx] += tip;
        }

        firePitchStart[pitchIdx] = firePitchStart[pitchIdx] * 0.05f - firePitchDir[pitchIdx] * len;
    }

    ParArrayInit();
    xAntennaAddAntenna(this, 0.0f, 0.0f, 8, 9, false);

    MeterSetup();

    CommonReset();
}

void zNMENeptune::Destroy()
{
    zSB04_BossPreRender = NULL;

    scopeTex.world = scopeWorld;
    scopeTex.destroy();
    RpLightDestroy(scopeLight);
    scopeModel[0]->geometry->matList.materials[0]->texture->raster = scopeOldRast;

    povcam.destroy();
    bosscam.destroy();
    cruise_bubble::Cam_Launch = NULL;
    cruise_bubble::Cam_Return = NULL;

    zNMEBoss::Destroy();
}

void zNMENeptune::Reset()
{
    zNMECommon::Reset();
    CommonReset();

    Neptune::health_meter.status.healthCurr[1] =
        (bossStage < 1) ? cfg_neptune->stageHealth[0] * 2 : 0;
    Neptune::health_meter.status.healthCurr[2] =
        (bossStage < 2) ? cfg_neptune->stageHealth[1] * 2 : 0;
    Neptune::health_meter.status.healthCurr[3] = cfg_neptune->stageHealth[2] * 2;
}

// The GameCube build moved several PS2-demo inline blocks into helpers (PlayerGotPoked,
// KrabAimDistance2, PlayerKnockBack, DotToPlayer, the *CollideUpdate functions, MeterUpdate,
// SndVOUpdate, SndPlay). firstbeam and inFireLoop are bool (the PS2 dump prints bool as uint8).
void zNMENeptune::Process(F32 dt)
{
    if (IsAlive())
    {
        ShadStufUpdate();
    }
    else if (simpShadow != NULL || entShadow != NULL)
    {
        ShadStufIAmDead();
    }

    if (crowdSlaveUpdate != 0)
    {
        crowdSlaveUpdate--;
    }

    F32 safeDist = xsqrt(SQR(xglobals->___player_ent_dont_use_directly->model->Mat->pos.x) +
                         SQR(xglobals->___player_ent_dont_use_directly->model->Mat->pos.z));
    F32 safeVel = xsqrt(SQR(xglobals->___player_ent_dont_use_directly->frame->vel.x) +
                        SQR(xglobals->___player_ent_dont_use_directly->frame->vel.z));
    if (safeDist > cfg_neptune->player_safe_dist && safeVel > cfg_neptune->player_safe_vel)
    {
        F32 velscale = cfg_neptune->player_safe_vel / safeVel;
        xglobals->___player_ent_dont_use_directly->frame->vel.x *= velscale;
        xglobals->___player_ent_dont_use_directly->frame->vel.z *= velscale;
    }

    shadow_cache_fudge_factor = 1.0f;
    if (entShadow != NULL)
    {
        entShadow->flg_disableEnvCast = 1;
    }

    if (stage2wait != 0)
    {
        if (stage2wait == 1 && strcmp(model->Anim->Single->State->Name, "Damage") == 0)
        {
            stage2wait = 2;
        }

        if (stage2wait == 2 && strcmp(model->Anim->Single->State->Name, "Damage") != 0)
        {
            xBase* base = zSceneFindObject(0x1c8d66b5);
            if (base != NULL)
            {
                F32 tmpparam[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                zEntEvent(base, eEventPreload, tmpparam);
            }
            stage2wait = 3;
        }

        if (stage2wait == 3 && globals.cmgr == NULL)
        {
            *(xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos =
                cfg_neptune->bossStage2_safeLoc;
            xglobals->___player_ent_dont_use_directly->frame->mat.pos =
                cfg_neptune->bossStage2_safeLoc;
            xglobals->___player_ent_dont_use_directly->frame->oldmat.pos =
                cfg_neptune->bossStage2_safeLoc;
            BossCamSetTargets();
            bosscam.move_to_rest();
            if (entDestructFloor != NULL)
            {
                zEntEvent(entDestructFloor, entDestructFloor, eEventCollisionOff);
            }
            timer = -0.001f;
            stage2wait = 0;
        }

        xprintf("Stage2wait = %d\n", stage2wait);
    }

    if (entDestructFloor != NULL)
    {
        if (bossStage == 2 && globals.cmgr == NULL && stage2wait == 0)
        {
            if (xEntIsVisible(entDestructFloor))
            {
                F32 zeroParam[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                zEntEvent(entDestructFloor, entDestructFloor, eEventEnable);
                zEntEvent(this, entDestructFloor, eEventLaunchShrapnel, zeroParam,
                          (xBase*)xSTFindAsset(0xf07a4606, NULL), 0xf07a4606);
                zEntEvent(entDestructFloor, entDestructFloor, eEventCollision_Visible_Off);
                *(xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos =
                    cfg_neptune->bossStage2_safeLoc;
                xglobals->___player_ent_dont_use_directly->frame->mat.pos =
                    cfg_neptune->bossStage2_safeLoc;
                xglobals->___player_ent_dont_use_directly->frame->oldmat.pos =
                    cfg_neptune->bossStage2_safeLoc;
                BossCamSetTargets();
                bosscam.move_to_rest();
            }
        }
        else if (!xEntIsVisible(entDestructFloor))
        {
            zEntEvent(entDestructFloor, entDestructFloor, eEventEnable);
            zEntEvent(entDestructFloor, entDestructFloor, eEventCollision_Visible_On);
            zEntEvent(entDestructFloor, entDestructFloor, eEventReset);
            zEntEvent(entDestructFloor, entDestructFloor, eEventDisable);
        }
    }

    if (underFloorJSP != NULL)
    {
        if (entDestructFloor != NULL && xEntIsVisible(entDestructFloor))
        {
            iEnvJSPOff(globals.sceneCur->env->geom, underFloorJSP);
        }
        else
        {
            iEnvJSPOn(globals.sceneCur->env->geom, underFloorJSP);
        }
    }

    flg_newtime = 1;
    flg_xtrarend = 1;

    if (last_dt > 0.00001f)
    {
        xVec3* playerpos = (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos;
        xVec3* playeroldpos = &xglobals->___player_ent_dont_use_directly->frame->oldmat.pos;
        firePlayerLagVel[firePlayerLagIdx] = (*playerpos - *playeroldpos) * (1.0f / last_dt);

        bool inFireLoop = (model->Anim->Single->State->UserFlags & 0x100000) &&
                          model->Anim->Single->Blend->State == NULL;

        if (bossStage == 0 && !inFireLoop)
        {
            firePlayerLagVel[firePlayerLagIdx] += ((xMat4x3*)model->Mat)->right * firePlayerLagDir;
        }

        firePlayerLagIdx++;
        if (firePlayerLagIdx >= 45)
        {
            firePlayerLagIdx = 0;
        }
    }

    last_dt = dt;

    if (!stateFrozen && !(model->Anim->Single->State->UserFlags & 0x80000) &&
        oob_state::IsPlayerInControl())
    {
        timer += dt;
    }

    if (globals.cmgr != NULL)
    {
        timer = -0.001f;
    }

    if (model->Anim->Single->Blend->State == NULL ||
        !(model->Anim->Single->State->UserFlags & 0x200000))
    {
        tailRepeatTimer -= dt;
        if (tailRepeatTimer < 0.0f)
        {
            tailRepeatTimer = 0.0f;
        }
    }

    if (locked_aim_lerp > 0.0f)
    {
        locked_aim_lerp -= 4.0f * dt;
        if (locked_aim_lerp < 0.0f)
        {
            locked_aim_lerp = 0.0f;
        }
    }

    if (povBeamFade > 0.0f && state != 0x1003)
    {
        povBeamFade -= dt / cfg_neptune->pov_fadeTime;
        if (povBeamFade < 0.0f)
        {
            povBeamFade = 0.0f;
        }
    }

    if (povBeamFade == 0.0f && povBucketFade > 0.0f)
    {
        povBucketFade -= dt / cfg_neptune->pov_fadeTime;
        if (povBucketFade < 0.0f)
        {
            povBucketFade = 0.0f;
        }
    }

    if (povBeamFade != 0.0f && povTridentGlow > 0.0f)
    {
        povTridentGlow -= cfg_neptune->pov_glowMax * dt / cfg_neptune->pov_glowFadeTime;
        if (povTridentGlow < 0.0f)
        {
            povTridentGlow = 0.0f;
        }
    }

    povBeamU += 10.0f * dt;

    if (state & 0x1000)
    {
        S32 i;
        xVec3 triTipTags[3];
        S32 aimedAtKrabs;
        F32 aimKrabsDist;

        for (i = 0; i < 3; i++)
        {
            xMat4x3Toworld(&triTipTags[i], &aim_mat, &cfg_neptune->projPOVTag[i]);
        }

        if (PlayerGotPoked(triTipTags, cfg_neptune->pov_pokeDist))
        {
            zEntPlayer_Damage(this, 1);
            Neptune::PlayerKnockBack(20.0f, 0.6f);
        }

        aimedAtKrabs = 0;
        aimKrabsDist = KrabAimDistance2(&aim_mat);
        if (aimKrabsDist < SQR(cfg_neptune->pov_krabsTargetRad))
        {
            aimedAtKrabs = 1;
        }

        switch (state)
        {
        case 0x1001:
            if (timer > 0.5f)
            {
                SetState(0x1002);
                xQuatIdent(&aim_momentum);
                povKrabsLockon = 0;
                povKrabsLockCloser = 0;
                povChargeSoundPlayed = 0;
            }
            break;
        case 0x1002:
        {
            const NMECfgNeptuneAttack* currAtk = &cfg_neptune->stageAtk[bossStage];

            if (cruise_bubble::active() && !povChargeSoundPlayed)
            {
                timer += dt;
            }

            F32 time_remaining =
                currAtk->time_randomtrack + currAtk->time_finaltrack + currAtk->time_lockon - timer;

            if (!povChargeSoundPlayed && time_remaining < cfg_neptune->snd_povChargeTime)
            {
                SndPlay(Neptune::eSOUND_PovCharge, cfg_neptune->snd_povChargeVol);
                povChargeSoundPlayed = 1;
            }

            if (time_remaining < cfg_neptune->pov_glowStart)
            {
                povTridentGlow = 1.0f - time_remaining / cfg_neptune->pov_glowStart;
                povTridentGlow = CLAMP(povTridentGlow, 0.0f, 1.0f);
                povTridentGlow *= cfg_neptune->pov_glowMax;
            }

            if (time_remaining <= 0.0f)
            {
                SetState(0x1003);
                locked_aim_pos = aim_pos;
                locked_aim_lerp = 1.0f;
                povBeamMat = aim_mat;
                povBeamU = 0.0f;
                povfire_shotcount++;
                gotPlayerThisAttack = 0;
                povHitReflectors = 0;
                if (!povChargeSoundPlayed)
                {
                    SndPlay(Neptune::eSOUND_PovCharge, cfg_neptune->snd_povChargeVol);
                    povChargeSoundPlayed = 1;
                }
            }

            if (time_remaining < cfg_neptune->pov_krabsTargetTime && aimedAtKrabs &&
                !povKrabsLockon)
            {
                povKrabsLockon = 1;
                povKrabsLockCloser = 1;
                povKrabsLockDist = aimKrabsDist;
            }
            break;
        }
        case 0x1003:
        {
            if (timer > cfg_neptune->pov_damageDelay * (1.0f - aim_tooCloseLerp))
            {
                bool firstbeam = (povBeamFade == 0.0f);
                xIsect isx;
                xVec3 beamstart;
                xVec3 beamdir;
                U8 reflected;
                xRay3 beamray;
                xVec3 reflectnorm;

                povBeamFade = 1.0f;
                xMat4x3Toworld(&beamstart, &povBeamMat, &cfg_neptune->projPOVTag[0]);
                beamdir = povBeamMat.at;
                beamray.origin = beamstart;
                beamray.dir = beamdir;
                beamray.min_t = 0.0f;
                beamray.max_t = 40.0f;
                beamray.flags = 0xc00;

                reflected = BeamCollide(&beamray, &aimedAtKrabs, &reflectnorm);
                povBeamHit.x = povBeamMat.at.x * beamray.max_t + beamstart.x;
                povBeamHit.y = povBeamMat.at.y * beamray.max_t + beamstart.y;
                povBeamHit.z = povBeamMat.at.z * beamray.max_t + beamstart.z;
                povBeamReflected = reflected;

                if (reflected)
                {
                    F32 reflDot = 2.0f * xVec3Dot(&reflectnorm, &beamray.dir);
                    povBeamReflectDir.x = beamray.dir.x - reflDot * reflectnorm.x;
                    povBeamReflectDir.y = beamray.dir.y - reflDot * reflectnorm.y;
                    povBeamReflectDir.z = beamray.dir.z - reflDot * reflectnorm.z;
                }

                xVec3 tempPlayerMidsect;
                xMat4x3Toworld(&tempPlayerMidsect,
                               (xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat,
                               &playerMidHist[0]);
                xLine3VecDist2(&beamstart, &povBeamHit, &tempPlayerMidsect, &isx);

                if (isx.dist < SQR(cfg_neptune->pov_damageRad) && !gotPlayerThisAttack)
                {
                    S32 oldScorchCount = scorchCount;
                    PaintDecalOnPlayer();
                    gotPlayerThisAttack = 1;

                    if (oldScorchCount != scorchCount)
                    {
                        zEntPlayer_Damage(this, 1);

                        if (SndVOCheck(0.25f))
                        {
                            if (voPOVDamageToggle == 0)
                            {
                                voSound = SndPlay(Neptune::eSOUND_VO_PlankTauntDmg3, 1.0f);
                            }
                            else if (voPOVDamageToggle == 1)
                            {
                                voSound = SndPlay(Neptune::eSOUND_VO_PatHintTable2, 1.0f);
                            }

                            voPOVDamageToggle++;
                            if (voPOVDamageToggle > 3)
                            {
                                voPOVDamageToggle = 0;
                            }
                        }

                        F32 distPlayer2D =
                            xsqrt(SQR(xglobals->___player_ent_dont_use_directly->model->Mat->pos.x -
                                      model->Mat->pos.x) +
                                  SQR(xglobals->___player_ent_dont_use_directly->model->Mat->pos.z -
                                      model->Mat->pos.z));
                        F32 distLerp = (cfg_neptune->pov_kbmaxdist - distPlayer2D) /
                                       (cfg_neptune->pov_kbmaxdist - cfg_neptune->pov_kbmindist);
                        distLerp = CLAMP(distLerp, 0.0f, 1.0f);

                        if (globals.player.g.TakeDamage &&
                            (cfg_neptune->pov_kbalways || distLerp > 0.05f))
                        {
                            static S32 kbpower = 0;

                            if (kbpower == 1)
                            {
                                distLerp = distLerp * distLerp;
                            }

                            if (kbpower == -1)
                            {
                                distLerp = xsqrt(distLerp);
                            }

                            F32 kbvel = cfg_neptune->pov_kbminvel * (1.0f - distLerp) +
                                        cfg_neptune->pov_kbmaxvel * distLerp;
                            F32 kbtime = cfg_neptune->pov_kbmaxtime;
                            Neptune::PlayerKnockBack(kbvel, kbtime);
                        }
                    }
                }

                if (aimedAtKrabs)
                {
                    gotPlayerThisAttack = 1;

                    for (povHitReflectors = 1; povHitReflectors < 6; povHitReflectors++)
                    {
                        xEnt* refl = entReflectors[povHitReflectors - 1];
                        xMat3x3 assetMat;
                        xMat3x3Euler(&assetMat, refl->asset->ang.x, refl->asset->ang.y,
                                     refl->asset->ang.z);
                        if (xVec3Dot(&assetMat.at, &((xMat4x3*)refl->model->Mat)->at) > 0.999f)
                        {
                            break;
                        }
                    }

                    if (povHitReflectors == 6 || (povHitReflectors != 0 && povFirstReflect))
                    {
                        povFirstReflect = 0;
                        povStunnedMe = (povHitReflectors == 6);
                        xQuatIdent(&aim_momentum);
                        zEntPlayerControlOff(CONTROL_OWNER_BOSS);
                        zEntEvent(baseBeamFlythru, eEventRun);
                        SetState(0x1004);
                    }

                    cruise_bubble::CruiseBubble::kill(true, false);
                }

                if (firstbeam)
                {
                    if (povHitReflectors != 0)
                    {
                        SndPlay(Neptune::eSOUND_PovAttack, cfg_neptune->snd_povAttackVol);
                        SndLoop3D(Neptune::eSOUND_PovStunLoop, NULL, NULL);

                        if (povHitReflectors == 6)
                        {
                            xVec3 bucketSoundPos = *(xVec3*)&model->Mat->pos;
                            bucketSoundPos.y += 4.0f;
                            SndLoop3D(Neptune::eSOUND_PovBucketLoop, &bucketSoundPos, NULL);
                        }
                    }
                    else if (reflected)
                    {
                        SndPlay(Neptune::eSOUND_PovAttack, cfg_neptune->snd_povReflectVol);
                        SndPlay(Neptune::eSOUND_PovReflect, cfg_neptune->snd_povReflectVol);
                    }
                    else
                    {
                        SndPlay(Neptune::eSOUND_PovAttack, cfg_neptune->snd_povAttackVol);
                    }
                }
            }

            const NMECfgNeptuneAttack* currAtk = &cfg_neptune->stageAtk[bossStage];
            if (timer > 0.35f)
            {
                if (cruise_bubble::active())
                {
                    povfire_shotcount = MIN(povfire_shotcount, currAtk->proj_count - 1);
                }

                if (povfire_shotcount >= currAtk->proj_count)
                {
                    if (globals.player.JumpState == 0)
                    {
                        SetState(0x1005);
                        xQuatIdent(&aim_momentum);
                        zEntPlayerControlOff(CONTROL_OWNER_BOSS);
                    }
                }
                else
                {
                    SetState(0x1002);
                    xQuatIdent(&aim_momentum);
                    povKrabsLockon = 0;
                    povKrabsLockCloser = 0;
                    povChargeSoundPlayed = 0;
                }
            }
            break;
        }
        case 0x1004:
            if (!povStunnedMe)
            {
                if (zcam_flytime > cfg_neptune->pov_missFlySoundTime[povHitReflectors - 1] &&
                    deflectSFX != NULL && deflectSFX->state == ZSCRIPT_STATE_RUNNING)
                {
                    zScriptReset(deflectSFX);
                    printf("Resetting deflection!!!\n");
                }

                if (zcam_flyrate != 0.0f &&
                    zcam_flytime > cfg_neptune->pov_missFlyStopTime[povHitReflectors - 1])
                {
                    zcam_flyrate = 0.0f;
                    timer = 0.0f;
                }

                if (zcam_flyrate == 0.0f && timer >= cfg_neptune->pov_missFlyStopDelay)
                {
                    zCameraFlyStop();
                    SetState(0x1001);
                    CamStateChange(2, true);
                    povcam.lastFlyThru = 0;
                    zEntPlayerControlOn(CONTROL_OWNER_BOSS);
                    break;
                }
            }

            if (!zCameraIsFlyCamRunning())
            {
                if (povStunnedMe)
                {
                    SetState(4);
                }
                else
                {
                    SetState(1);
                }
                CamStateChange(1, true);
                povcam.lastFlyThru = 0;
                zEntPlayerControlOn(CONTROL_OWNER_BOSS);
            }
            else
            {
                if (timer < cfg_neptune->pov_stunAnimTranTime - cfg_neptune->pov_fadeTime)
                {
                    povBeamFade = 1.0f;
                }

                if (timer < cfg_neptune->pov_stunAnimTranTime)
                {
                    povBucketFade = 1.0f;
                }
            }
            break;
        case 0x1005:
            if (timer > 0.4f)
            {
                if (povStunnedMe)
                {
                    SetState(4);
                    zEntPlayerControlOn(CONTROL_OWNER_BOSS);
                }
                else
                {
                    SetState(1);
                    zEntPlayerControlOn(CONTROL_OWNER_BOSS);
                }
            }
            break;
        }
    }
    else
    {
        xVec3 triTipTags[4];
        xMat4x3* cruiseMat;
        F32 cruiseDist;

        iModelTagEval(model->Data, &bossTag[0], model->Mat, &triTipTags[0]);
        iModelTagEval(model->Data, &bossTag[2], model->Mat, &triTipTags[1]);
        iModelTagEval(model->Data, &bossTag[3], model->Mat, &triTipTags[2]);
        iModelTagEval(model->Data, &bossTag[1], model->Mat, &triTipTags[3]);

        if (PlayerGotPoked(&triTipTags[0], cfg_neptune->poke_dist) ||
            PlayerGotPoked(&triTipTags[1], cfg_neptune->poke_dist))
        {
            zEntPlayer_Damage(this, 1);
            Neptune::PlayerKnockBack(20.0f, 0.6f);
        }

        if (IsDead())
        {
            SetState(4);
        }

        if (gotNeptune)
        {
            SetState(1);
            zEntPlayerControlOff(CONTROL_OWNER_BOSS);
        }

        cruiseMat = cruise_bubble::GetMissileMat();
        cruiseDist = xsqrt(SQR(cruiseMat->pos.x - model->Mat->pos.x) +
                           SQR(cruiseMat->pos.z - model->Mat->pos.z));

        if (cruise_bubble::active() && cruise_bubble::flying())
        {
            cruiseTimer += dt;
        }
        else
        {
            cruiseTimer = 0.0f;
        }

        if (state != 4 && cruise_bubble::active() && cruise_bubble::flying() &&
            cruiseTimer >= cfg_neptune->defend_cruiseTime &&
            cruiseDist <= cfg_neptune->defend_cruiseDist)
        {
            SetState(5);
        }

        if (((bossStage == 0 && gotPlayer > 4) || (bossStage >= 1 && gotPlayer > 0)) &&
            !gotPlayerThisAttack)
        {
            gotPlayerThisAttack = 1;
            zEntPlayer_Damage(this, 1);

            if (SndVOCheck(0.5f) && xurand() < 0.5f)
            {
                if (bossStage == 0)
                {
                    if (voDamageToggle == 0)
                    {
                        voSound = SndPlay(Neptune::eSOUND_VO_PlankTauntDmg1, 1.0f);
                    }
                    else
                    {
                        voSound = SndPlay(Neptune::eSOUND_VO_NepTauntDmg1, 1.0f);
                    }
                    voDamageToggle = !voDamageToggle;
                }
                else
                {
                    if (voDamageToggle == 0)
                    {
                        voSound = SndPlay(Neptune::eSOUND_VO_PlankTauntDmg2, 1.0f);
                    }
                    else if (voDamageToggle == 1)
                    {
                        voSound = SndPlay(Neptune::eSOUND_VO_PatHintDodge2, 1.0f);
                    }
                    else if (voDamageToggle == 2)
                    {
                        voSound = SndPlay(Neptune::eSOUND_VO_NepTauntDmg1, 1.0f);
                    }

                    voDamageToggle++;
                    if (voDamageToggle > 2)
                    {
                        voDamageToggle = 0;
                    }
                }
            }

            outrageCount = 0;
            tauntEm = 1;

            if (bossStage == 0)
            {
                SetState(1);
                firePlayerLagDir = -firePlayerLagDir;
            }
            else
            {
                lightningDamageAfter = cfg_neptune->fire_ltDamageAfter;
            }
        }

        if (lightningDamageAfter != 0.0f)
        {
            lightningDamageAfter -= dt;
            if (lightningDamageAfter <= 0.0f)
            {
                lightningDamageAfter = 0.0f;
                SetState(1);
            }
        }

        gotPlayer--;
        if (gotPlayer < 0)
        {
            gotPlayer = 0;
        }

        if (state == 1 || state == 2)
        {
            const NMECfgNeptuneAttack* currAtk = &cfg_neptune->stageAtk[bossStage];
            if (outrageCount >= currAtk->outrage_count && !beamParThisFrame &&
                globals.player.JumpState == 0)
            {
                SetState(3);
                outrageCount = 0;
                zEntPlayerControlOff(CONTROL_OWNER_BOSS);
                if (voDodgeHoseToggle == 2)
                {
                    voDodgeHoseToggle = 1;
                }
            }
        }

        switch (state)
        {
        case 1:
            if (minionsActive && timer > 0.0f)
            {
                S32 i;
                for (i = 0; i < 2; i++)
                {
                    Neptune::MinionSpawn(entMinions[i]);
                }

                if (cfg_neptune->minion_state)
                {
                    SetState(6);
                }
                else
                {
                    minionsActive = 0;
                }
            }
            else
            {
                const NMECfgNeptuneAttack* currAtk = &cfg_neptune->stageAtk[bossStage];
                if (timer > currAtk->time_wait && DotToPlayer() >= cfg_neptune->fire_angle_dot)
                {
                    fireTipAvgCount = 0;
                    fireTipAvgIdx = 0;
                    gotPlayerThisAttack = 0;
                    gotPlayer = 0;
                    ltTimer = -1.0f;
                    fireTimer = -1.0f;
                    ltRingStarted = 0;
                    lightningDamageAfter = 0.0f;
                    fireBeamGroundHit = -1.0f;
                    fireBeamParLaunched = 0;
                    SetState(2);
                }
            }
            break;
        case 2:
        {
            const NMECfgNeptuneAttack* currAtk = &cfg_neptune->stageAtk[bossStage];
            if (timer > currAtk->time_fire && lightningDamageAfter == 0.0f)
            {
                if (!gotPlayerThisAttack)
                {
                    outrageCount++;
                }
                SetState(1);
                firePlayerLagDir = -firePlayerLagDir;
            }
            else if (model->Anim->Single->State->UserFlags & 0x200000)
            {
                SetState(1);
            }
            break;
        }
        case 4:
        {
            if (!stateFrozen && !(model->Anim->Single->State->UserFlags & 0x80000) &&
                cruise_bubble::active() && cruise_bubble::flying())
            {
                timer -= dt;
                if (timer < 0.0f)
                {
                    timer = 0.0f;
                }
            }

            F32 distplayer2 = SQR(xglobals->___player_ent_dont_use_directly->model->Mat->pos.x -
                                  model->Mat->pos.x) +
                              SQR(xglobals->___player_ent_dont_use_directly->model->Mat->pos.z -
                                  model->Mat->pos.z);
            if (timer > cfg_neptune->stun_duration || distplayer2 < SQR(cfg_neptune->stun_neardist))
            {
                SetState(1);
                voCruiseBubbleToggle = 1;
            }
            break;
        }
        case 5:
            if (timer > cfg_neptune->defend_min_duration)
            {
                if (minionsActive && cfg_neptune->minion_state)
                {
                    SetState(6);
                }
                else
                {
                    fireTipAvgCount = 0;
                    fireTipAvgIdx = 0;
                    gotPlayerThisAttack = 0;
                    gotPlayer = 0;
                    ltTimer = -1.0f;
                    fireTimer = -1.0f;
                    ltRingStarted = 0;
                    lightningDamageAfter = 0.0f;
                    fireBeamGroundHit = -1.0f;
                    fireBeamParLaunched = 0;
                    SetState(2);
                }
            }
            break;
        case 3:
            if (timer > 2.5f)
            {
                if (SndVOCheck(0.0f))
                {
                    if (voOutrageTauntToggle == 0)
                    {
                        voSound = SndPlay(Neptune::eSOUND_VO_PlankTaunt1, 1.0f);
                    }
                    else if (voOutrageTauntToggle == 1)
                    {
                        voSound = SndPlay(Neptune::eSOUND_VO_NepTaunt2, 1.0f);
                    }
                    else if (voOutrageTauntToggle == 2)
                    {
                        voSound = SndPlay(Neptune::eSOUND_VO_NepTaunt3, 1.0f);
                    }

                    voOutrageTauntToggle++;
                    if (voOutrageTauntToggle > 2)
                    {
                        voOutrageTauntToggle = 0;
                    }
                }

                SetState(0x1001);
                zEntPlayerControlOn(CONTROL_OWNER_BOSS);
                povfire_shotcount = 0;
                povStunnedMe = 0;
            }
            break;
        case 6:
        {
            S32 i;

            if (!minionsActive || !cfg_neptune->minion_state)
            {
                SetState(1);
                for (i = 0; i < 2; i++)
                {
                    Neptune::MinionKill(entMinions[i]);
                }
            }
            else
            {
                for (i = 0; i < 2; i++)
                {
                    if (Neptune::MinionAlive(entMinions[i]))
                    {
                        break;
                    }
                }

                if (i == 2)
                {
                    SetState(1);
                    minionsActive = 0;
                }
            }
            break;
        }
        }
    }

    if ((state & 0x1000) && state != 0x1005)
    {
        CamStateChange(2, true);
    }
    else if (state == 6 || (state == 5 && minionsActive && cfg_neptune->minion_state))
    {
        CamStateChange(0, false);
    }
    else
    {
        CamStateChange(1, false);
    }

    if (bossStage == 0 && state == 2)
    {
        S32 i;
        S32 lagidx = firePlayerLagIdx;
        xVec3 laggedVel = { 0.0f, 0.0f, 0.0f };
        F32 tgtTime;
        F32 launchSpeed = 25.0f;

        for (i = 0; i < 45; i++)
        {
            if (--lagidx < 0)
            {
                lagidx = 44;
            }
            laggedVel += firePlayerLagVel[lagidx];
        }
        laggedVel *= 1.0f / 45.0f;

        static xVec3 shooterVec = { 0.0f, 4.3f, 0.0f };
        tgtTime = Neptune::ComputeTargetFixedVel(
            &shooterVec, launchSpeed,
            (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos, &laggedVel);
        tgtTime -= cfg_neptune->fire_track_mindist / launchSpeed;
        tgtTime = MAX(tgtTime, 0.0f);

        fireTarget.x =
            tgtTime * laggedVel.x + xglobals->___player_ent_dont_use_directly->model->Mat->pos.x;
        fireTarget.y = cfg_neptune->fire_target_height[bossStage];
        fireTarget.z =
            tgtTime * laggedVel.z + xglobals->___player_ent_dont_use_directly->model->Mat->pos.z;

        bool inFireLoop = (model->Anim->Single->State->UserFlags & 0x100000) &&
                          model->Anim->Single->Blend->State == NULL;

        F32 playerDist =
            xVec3Dist((xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos,
                      (xVec3*)&model->Mat->pos);
        F32 distLerp = (playerDist - cfg_neptune->fire_beamFrontStartDist) /
                       (cfg_neptune->fire_beamFrontEndDist - cfg_neptune->fire_beamFrontStartDist);
        F32 timeLerp = inFireLoop ? (fireTimer - cfg_neptune->fire_beamFrontDelay) /
                                        cfg_neptune->fire_beamFrontTime :
                                    0.0f;
        distLerp = CLAMP(distLerp, 0.0f, 1.0f);
        timeLerp = CLAMP(timeLerp, 0.0f, 1.0f);
        F32 frontLerp = distLerp * (1.0f - timeLerp);
        F32 frontDist = frontLerp * cfg_neptune->fire_beamFrontDist;

        fireTarget.x -= model->Mat->at.x * frontDist;
        fireTarget.y -= frontLerp * cfg_neptune->fire_target_height[bossStage];
        fireTarget.z -= model->Mat->at.z * frontDist;
    }
    else if (state != 2 || !(model->Anim->Single->State->UserFlags & 0x100000) ||
             model->Anim->Single->Blend->State != NULL)
    {
        fireTarget.x = xglobals->___player_ent_dont_use_directly->model->Mat->pos.x;
        fireTarget.y = cfg_neptune->fire_target_height[bossStage];
        fireTarget.z = xglobals->___player_ent_dont_use_directly->model->Mat->pos.z;
        fireTarget.x *= 0.8f;
        fireTarget.z *= 0.8f;
    }

    if (state == 2)
    {
        FirePitchUpdate();
    }

    if (!(state & 0x1000))
    {
        xVec3 facepos;
        xVec3 diffVec;
        xVec3 atvec;
        F32 currAngle;
        F32 targetAngle;
        F32 diffAngle;
        F32 absAngle;
        F32 rightdot;
        F32 xzdist2;

        if (state == 5 && cruise_bubble::flying())
        {
            facepos = cruise_bubble::location();
        }
        else if (state == 2)
        {
            facepos = fireFace;
        }
        else
        {
            facepos = *(xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos;
        }

        diffVec.x = facepos.x - model->Mat->pos.x;
        diffVec.y = facepos.y - model->Mat->pos.y;
        diffVec.z = facepos.z - model->Mat->pos.z;

        targetAngle = xatan2(diffVec.x, diffVec.z);
        currAngle = xatan2(model->Mat->at.x, model->Mat->at.z);
        diffAngle = targetAngle - currAngle;
        if (diffAngle < -PI)
        {
            diffAngle += 2.0f * PI;
        }
        else if (diffAngle > PI)
        {
            diffAngle -= 2.0f * PI;
        }

        absAngle = xabs(diffAngle);
        absAngle = MIN((PI / 180.0f) * cfg_neptune->turn_rate * dt, absAngle);
        if (diffAngle < 0.0f)
        {
            currAngle -= absAngle;
        }
        else
        {
            currAngle += absAngle;
        }

        atvec.x = -isin(currAngle);
        atvec.y = 0.0f;
        atvec.z = -icos(currAngle);
        xMat3x3LookVec((xMat3x3*)model->Mat, &atvec);

        diffVec.x =
            xglobals->___player_ent_dont_use_directly->model->Mat->pos.x - model->Mat->pos.x;
        diffVec.y =
            xglobals->___player_ent_dont_use_directly->model->Mat->pos.y - model->Mat->pos.y;
        diffVec.z =
            xglobals->___player_ent_dont_use_directly->model->Mat->pos.z - model->Mat->pos.z;
        rightdot = xVec3Dot(&diffVec, &((xMat4x3*)model->Mat)->right);
        xzdist2 = SQR(diffVec.x) + SQR(diffVec.z);

        tailAttackRange = xzdist2 < SQR(cfg_neptune->tailattack_rad) &&
                          diffVec.y < cfg_neptune->tailattack_height;

        tailAttackRight = (xabs(rightdot) < 0.5f) ? tailAttackToggle : (rightdot < 0.0f);
    }

    zNMECommon::Process(dt);
    BossCamSetTargets();
    BucketCollideUpdate();
    DefendCollideUpdate();
    BodyCollideUpdate();
    PovCollideUpdate();
    MeterUpdate();
    SndVOUpdate(dt);

    if (SndVOCheck(1.0f) && bossStage == 0 && state == 2 && fireBeamGroundHit >= 0.0f)
    {
        if (voDodgeHoseToggle == 0)
        {
            voSound = SndPlay(Neptune::eSOUND_VO_PatHintDodge1, 1.0f);
        }
        else if (voDodgeHoseToggle == 1 && xurand() < 0.33f)
        {
            voSound = SndPlay(Neptune::eSOUND_VO_PatHintDodge1, 1.0f);
        }
        voDodgeHoseToggle = 2;
    }

    if (SndVOCheck(0.0f) && state == 1 && timer < -0.5f && timer > -1.0f)
    {
        voSound = SndPlay(Neptune::eSOUND_VO_NepTaunt1, 1.0f);
    }

    if (bossStage == 0)
    {
        if (voTableHintCount == 0)
        {
            if (voTableHintTimer < 40.0f)
            {
                voTableHintTimer += dt;
            }
            else if (SndVOTableHintCheck())
            {
                voSound = SndPlay(Neptune::eSOUND_VO_PatHintTable1, 1.0f);
                voTableHintCount = 1;
            }
        }
        else if (voTableHintTimer < voTableHintCount * 30.0f + 40.0f)
        {
            voTableHintTimer += dt;
        }
        else
        {
            S32 turncount = 0;
            S32 i;

            for (i = 0; i < 5; i++)
            {
                xEnt* refl = entReflectors[i];
                xMat3x3 assetMat;
                xMat3x3Euler(&assetMat, refl->asset->ang.x, refl->asset->ang.y, refl->asset->ang.z);
                if (xVec3Dot(&assetMat.at, &((xMat4x3*)refl->model->Mat)->at) < 0.999f)
                {
                    turncount++;
                }
            }

            if (voTableHintCount & 1)
            {
                if (SndVOTableHintCheck() && (state & 0x1000))
                {
                    if (pts_health[0] == cfg_neptune->stageHealth[0])
                    {
                        voSound = SndPlay(Neptune::eSOUND_VO_PatHintTable3, 1.0f);
                    }
                    voTableHintCount++;
                }
            }
            else if (SndVOTableHintCheck())
            {
                if (turncount < 3)
                {
                    voSound = SndPlay(Neptune::eSOUND_VO_PatHintTable1, 1.0f);
                }
                voTableHintCount++;
            }
        }
    }

    if (SndVOCheck(1.0f) && bossStage == 0 && state == 4 && voCruiseBubbleToggle)
    {
        voSound = SndPlay(Neptune::eSOUND_VO_PatHintWave1, 1.0f);
        voCruiseBubbleToggle = 0;
    }
}

// No symbol (always inlined); the name is ours. Records the trident bones' current rotations and
// positions in the next slot of the motion blur history.
static inline void BlurHistoryUpdate(zNMENeptune* nep)
{
    nep->blurHistLast++;
    nep->blurHistTotal++;
    if (nep->blurHistTotal > 6)
    {
        nep->blurHistTotal = 6;
    }
    if (nep->blurHistLast >= 6)
    {
        nep->blurHistLast = 0;
    }

    for (S32 i = 0; i < 4; i++)
    {
        if (nep->tridentInfl[i] != -1)
        {
            xQuatFromMat(&nep->blurHistoryQuat[nep->blurHistLast][i],
                         (xMat3x3*)&nep->model->Mat[nep->tridentInfl[i]]);
            nep->blurHistoryTran[nep->blurHistLast][i] =
                *(xVec3*)&nep->model->Mat[nep->tridentInfl[i]].pos;
        }
    }
}

// No symbol (always inlined); the name is ours. Builds in q the rotation about a x b by scale
// times the angle between the unit vectors a and b; false (q untouched) when they are
// (anti)parallel.
static inline bool AimQuatFromVecs(xQuat* q, const xVec3* a, const xVec3* b, F32 scale)
{
    F32 dot = xVec3Dot(a, b);
    xVec3 axis;

    if (xabs(dot) >= 0.99999f)
    {
        return false;
    }

    xVec3Cross(&axis, a, b);
    if (xVec3Length(&axis) < 1e-6f)
    {
        return false;
    }

    axis.normalize();
    F32 angle = xacos(dot);
    xQuatFromAxisAngle(q, &axis, angle * scale);
    return true;
}

// No symbol (always inlined); the name is ours. Runs each playing looped sound's update callback:
// stops the sound when it returns false, else applies its volume/pitch.
static inline void SndLoopUpdate(zNMENeptune* nep, F32 dt)
{
    for (S32 i = 0; i < 16; i++)
    {
        if (nep->loopSnd[i].handle != (iSndHandle)-1)
        {
            if (nep->loopSnd[i].sndUpdateCB != NULL)
            {
                if (!nep->loopSnd[i].sndUpdateCB(&nep->loopSnd[i]))
                {
                    xSndMgrStop(nep->loopSnd[i].handle);
                    nep->loopSnd[i].handle = (iSndHandle)-1;
                }
                else
                {
                    xSndMgrSetVolume(nep->loopSnd[i].handle, nep->loopSnd[i].volscale);
                    xSndMgrSetPitch(nep->loopSnd[i].handle, nep->loopSnd[i].pitch);
                }
            }
            nep->loopSnd[i].time += dt;
        }
    }
}

void zNMENeptune::PostProcess(F32 dt)
{
    S32 i;
    xVec3 tempMidSect;
    RwMatrixTag oldPlyrMat;

    if (!firstTag)
    {
        memcpy(lastTag, currTag, sizeof(lastTag));
    }

    for (i = 0; i < 13; i++)
    {
        iModelTagEval(model->Data, &bossTag[i], model->Mat, &currTag[i]);
    }

    if (firstTag)
    {
        memcpy(lastTag, currTag, sizeof(lastTag));
        firstTag = 0;
    }

    oldPlyrMat = *xglobals->___player_ent_dont_use_directly->model->Mat;
    xMat4x3Identity((xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat);

    playerMidHist[2] = playerMidHist[1];
    playerMidHist[1] = playerMidHist[0];
    playerMidHist[0] = 0.0f;

    for (i = 0; i < 4; i++)
    {
        iModelTagEval(globals.player.sb_models[0]->Data, &playerMidSect[i],
                      xglobals->___player_ent_dont_use_directly->model->Mat, &tempMidSect);
        playerMidHist[0] += tempMidSect;
    }

    playerMidHist[0] = playerMidHist[0] * 0.25f;
    *xglobals->___player_ent_dont_use_directly->model->Mat = oldPlyrMat;

    if (oob_state::IsPlayerInControl())
    {
        TailSwipeUpdate();
    }

    BlurHistoryUpdate(this);

    BossCamSetTargets();

    if (state & 0x1000)
    {
        F32 oldPos;
        xVec3 atvec;

        oldPos = model->Mat->pos.y;
        model->Mat->pos.y = 0.0f;

        atvec.x = model->Mat->pos.x - xglobals->___player_ent_dont_use_directly->model->Mat->pos.x;
        atvec.y = 0.0f;
        atvec.z = model->Mat->pos.z - xglobals->___player_ent_dont_use_directly->model->Mat->pos.z;
        xMat3x3LookVec((xMat3x3*)model->Mat, &atvec);

        xVec3 old_aim_pos = aim_pos;
        xVec3 old_aim_dir = aim_dir;
        xQuat old_aim_momentum = aim_momentum;
        xMat4x3 old_aim_mat = aim_mat;

        switch (state)
        {
        case 0x1001:
        {
            xVec3 target_aim_pos;
            xMat3x3 restMat;

            xMat4x3Toworld(&target_aim_pos, xEntGetFrame(this), &cfg_neptune->aim_center);
            xVec3Lerp(&aim_pos, &target_aim_pos, &locked_aim_pos, locked_aim_lerp);

            xMat3x3Euler(&restMat, (PI / 180.0f) * cfg_neptune->aim_rest.x,
                         (PI / 180.0f) * cfg_neptune->aim_rest.y, 0.0f);
            xMat3x3RMulVec(&aim_dir, &povcam.mat, &restMat.at);
            xVec3Normalize(&aim_dir, &aim_dir);

            if (scopeCamFOVLerp > 0.0f)
            {
                scopeCamFOVLerp -= last_dt / cfg_neptune->scope_zoomOutTime;
                if (scopeCamFOVLerp < 0.0f)
                {
                    scopeCamFOVLerp = 0.0f;
                }
            }
            break;
        }
        case 0x1002:
        {
            F32 distPlayer2D;
            const NMECfgNeptuneAttack* currAtk;
            F32 finalTimer;
            F32 lockonTimer;
            F32 aim_directlerp;
            F32 tooCloseTrackLerp;

            distPlayer2D = xsqrt(SQR(xglobals->___player_ent_dont_use_directly->model->Mat->pos.x -
                                     model->Mat->pos.x) +
                                 SQR(xglobals->___player_ent_dont_use_directly->model->Mat->pos.z -
                                     model->Mat->pos.z));
            aim_tooCloseLerp = (cfg_neptune->aim_tooCloseMax - distPlayer2D) /
                               (cfg_neptune->aim_tooCloseMax - cfg_neptune->aim_tooCloseMin);
            aim_tooCloseLerp = CLAMP(aim_tooCloseLerp, 0.0f, 1.0f);

            if (!stateFrozen && !povChargeSoundPlayed)
            {
                timer += aim_tooCloseLerp * dt;
            }

            currAtk = &cfg_neptune->stageAtk[bossStage];
            finalTimer = timer - currAtk->time_randomtrack;
            if (timer < currAtk->time_randomtrack)
            {
                lockonTimer = 0.0f;
            }
            else if (finalTimer < currAtk->time_finaltrack)
            {
                lockonTimer = finalTimer / currAtk->time_finaltrack;
                lockonTimer *= lockonTimer;
            }
            else
            {
                lockonTimer = 1.0f;
            }

            scopeCamFOVLerp = 2.0f * lockonTimer;
            scopeCamFOVLerp = MIN(scopeCamFOVLerp, 1.0f);

            tooCloseTrackLerp = aim_tooCloseLerp * aim_tooCloseLerp;
            aim_directlerp = (1.0f - tooCloseTrackLerp) * lockonTimer + tooCloseTrackLerp;

            if (!povKrabsLockon || povKrabsLockCloser)
            {
                xVec3 target_aim_pos;
                xVec3 player_midsect;
                xVec3 target_dir;
                xQuat aimQuat;
                xMat3x3 aimMomentMat;
                xMat3x3 directMat;

                xMat4x3Toworld(&target_aim_pos, xEntGetFrame(this), &cfg_neptune->aim_center);
                xVec3Lerp(&aim_pos, &target_aim_pos, &locked_aim_pos, locked_aim_lerp);

                // Summed in two steps: retail keeps the first sum in a stack slot of its own.
                xVec3 hist_sum = playerMidHist[0] + playerMidHist[1];
                player_midsect = (hist_sum + playerMidHist[2]) * (1.0f / 3.0f);
                xMat4x3Toworld(&player_midsect,
                               (xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat,
                               &player_midsect);

                target_dir = player_midsect - aim_pos;
                xVec3Normalize(&target_dir, &target_dir);

                if (AimQuatFromVecs(&aimQuat, &target_dir, &aim_dir,
                                    cfg_neptune->aim_anglerate * dt))
                {
                    if (cfg_neptune->aim_slerpfirst)
                    {
                        xQuatSlerp(&aim_momentum, &aim_momentum, &g_IQ, cfg_neptune->aim_slerprate);
                        xQuatMul(&aim_momentum, &aim_momentum, &aimQuat);
                    }
                    else
                    {
                        xQuatMul(&aim_momentum, &aim_momentum, &aimQuat);
                        xQuatSlerp(&aim_momentum, &aim_momentum, &g_IQ, cfg_neptune->aim_slerprate);
                    }
                }
                else
                {
                    xQuatSlerp(&aim_momentum, &aim_momentum, &g_IQ, cfg_neptune->aim_slerprate);
                }

                xQuatToMat(&aim_momentum, &aimMomentMat);
                xMat3x3RMulVec(&aim_dir, &aimMomentMat, &aim_dir);

                if (AimQuatFromVecs(&aimQuat, &target_dir, &aim_dir, aim_directlerp))
                {
                    xQuatToMat(&aimQuat, &directMat);
                    xMat3x3RMulVec(&aim_dir, &directMat, &aim_dir);
                }
                else
                {
                    xVec3Lerp(&aim_dir, &aim_dir, &target_dir, aim_directlerp);
                }

                xVec3Normalize(&aim_dir, &aim_dir);
            }
            break;
        }
        case 0x1003:
            if (scopeCamFOVLerp > 0.0f &&
                timer > cfg_neptune->pov_damageDelay * (1.0f - aim_tooCloseLerp))
            {
                scopeCamFOVLerp -= last_dt / cfg_neptune->scope_zoomOutTime;
                if (scopeCamFOVLerp < 0.0f)
                {
                    scopeCamFOVLerp = 0.0f;
                }
            }
            locked_aim_lerp = 1.0f;
            break;
        case 0x1005:
            if (scopeCamFOVLerp > 0.0f)
            {
                scopeCamFOVLerp -= last_dt / cfg_neptune->scope_zoomOutTime;
                if (scopeCamFOVLerp < 0.0f)
                {
                    scopeCamFOVLerp = 0.0f;
                }
            }
            locked_aim_lerp = 1.0f;
            break;
        }

        atvec = -aim_dir;
        xMat3x3LookVec(&aim_mat, &atvec);
        aim_mat.pos = aim_pos;

        xVec3 scopeCamPos;
        F32 checkDist;
        if (state == 0x1002 && povKrabsLockon && povKrabsLockCloser)
        {
            checkDist = KrabAimDistance2(&aim_mat);
            if (checkDist < povKrabsLockDist)
            {
                povKrabsLockDist = checkDist;
            }
            else
            {
                povKrabsLockCloser = 0;
                aim_pos = old_aim_pos;
                aim_dir = old_aim_dir;
                aim_momentum = old_aim_momentum;
                aim_mat = old_aim_mat;
            }
        }

        scopeCamMat = aim_mat;

        xMat4x3Toworld(&scopeCamPos, &aim_mat, &cfg_neptune->scope_camtag);
        scopeCamMat.pos = scopeCamPos;

        checkDist =
            xVec3Dist(&scopeCamMat.pos, xEntGetPos(xglobals->___player_ent_dont_use_directly));
        scopeCamFOV = (cfg_neptune->scope_zoomViewMin * scopeCamFOVLerp +
                       cfg_neptune->scope_zoomViewMax * (1.0f - scopeCamFOVLerp)) /
                      MAX(cfg_neptune->scope_zoomDistMin, checkDist);

        model->Mat->pos.y = oldPos;
    }

    SndLoopUpdate(this, dt);
}

void zNMENeptune::Render()
{
    if (Neptune::inPreRender)
    {
        if (cfg_neptune->scope_drawtrident)
        {
            xLightKit_Enable(xglobals->___player_ent_dont_use_directly->model->LightKit,
                             xglobals->currWorld);
            iModelWorldBoundSphere(tridentModel, (RwMatrixTag*)&aim_mat, NULL);
            iModelRender(tridentModel, (RwMatrix*)&aim_mat);
        }
    }
    else if (ScopeViewActive())
    {
        flags |= 0x40;
        xShadowManager_Remove(this);

        RpMaterialList* matlist = &scopeModel[0]->geometry->matList;
        static xMat4x3 adjustedScopeMat;
        adjustedScopeMat = aim_mat;
        xMat4x3Toworld(&adjustedScopeMat.pos, &aim_mat, &cfg_neptune->scope_posadjust);

        RpMaterial* scopeMaterial = matlist->materials[0];
        iModelWorldBoundSphere(scopeModel[0], (RwMatrixTag*)&adjustedScopeMat, NULL);
        iModelWorldBoundSphere(scopeModel[1], (RwMatrixTag*)&adjustedScopeMat, NULL);

        xLightKit_Enable(xglobals->___player_ent_dont_use_directly->model->LightKit,
                         xglobals->currWorld);
        iModelWorldBoundSphere(tridentModel, (RwMatrixTag*)&aim_mat, NULL);
        iModelRender(tridentModel, (RwMatrix*)&aim_mat);
        iModelRender(scopeModel[1], (RwMatrix*)&adjustedScopeMat);
        xLightKit_Enable(NULL, xglobals->currWorld);

        if (scopeMaterial)
        {
            RwGameCubeSetAlphaCompare(6, 1, 0, 7, 0);
            _rwDlRenderStateSetZCompLoc(TRUE);

            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
            scopeMaterial->color.alpha = 0xff;
            scopeMaterial->texture->raster = scopeTex.raster;
            RpWorldAddLight(xglobals->currWorld, scopeLight);
            iModelRender(scopeModel[0], (RwMatrix*)&adjustedScopeMat);
            RpWorldRemoveLight(xglobals->currWorld, scopeLight);
            RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
            RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);

            memset(scopeCamModelInst, 0, sizeof(scopeCamModelInst));
            for (S32 i = 0; i < 2; i++)
            {
                scopeCamModelInst[i].Pipe.Layer = 16;
                scopeCamModelInst[i].Pipe.AlphaDiscard = 1;
                scopeCamModelInst[i].TrueClip = 1;
                scopeCamModelInst[i].RedMultiplier = 1.0f;
                scopeCamModelInst[i].GreenMultiplier = 1.0f;
                scopeCamModelInst[i].BlueMultiplier = 1.0f;
                scopeCamModelInst[i].Alpha = 1.0f;
                scopeCamModelInst[i].FadeStart = 1e38f;
                scopeCamModelInst[i].FadeEnd = 1e38f;
                scopeCamModelInst[i].LightKit = &Neptune::scopeLightKit;
                scopeCamModelInst[i].Flags = 1;
            }

            scopeCamModelInst[0].Pipe.Flags = 0x406500;
            scopeCamModelInst[0].Data = scopeModel[0];
            scopeCamModelInst[0].Bucket = scopeCamModelBucket[0];
            scopeCamModelInst[0].Mat = (RwMatrixTag*)&adjustedScopeMat;
            xModelBucket_Add(&scopeCamModelInst[0]);

            if (povTridentGlow >= cfg_neptune->pov_glowMin)
            {
                scopeCamModelInst[1].Pipe.Flags = 0x402500;
                scopeCamModelInst[1].Data = tridentModel;
                scopeCamModelInst[1].Bucket = scopeCamModelBucket[2];
                scopeCamModelInst[1].Mat = (RwMatrixTag*)&aim_mat;
                scopeCamModelInst[1].Alpha = povTridentGlow;
                xModelBucket_Add(&scopeCamModelInst[1]);
            }
        }
    }
    else
    {
        flags |= 0x40;
        xShadowManager_Remove(this);
        xEntRender(this);

        if (TridentBlurActive())
        {
            xQuat blurQuat[6][4];
            xVec3 blurTran[6][4];
            S32 i;
            S32 j;
            S32 currIdx = blurHistLast;

            for (i = 0; i < blurHistTotal; i++)
            {
                for (j = 0; j < 4 && tridentInfl[j] != -1; j++)
                {
                    blurQuat[i][j] = blurHistoryQuat[currIdx][j];
                    blurTran[i][j] = blurHistoryTran[currIdx][j];
                }

                currIdx--;
                if (currIdx < 0)
                {
                    currIdx = 5;
                }
            }

            F32 blurTimeCurr = 0.0f;
            for (i = 0; i < 8; i++)
            {
                blurTimeCurr += cfg_neptune->tridentblur_timeIncrement;
                S32 blurInt = std::floorf(blurTimeCurr);
                F32 blurFrac = blurTimeCurr - blurInt;

                if (blurInt + 1 >= blurHistTotal)
                {
                    return;
                }

                F32 blurLerp = i * (1.0f / 7.0f);
                F32 lerpAlpha = (1.0f - blurLerp) * cfg_neptune->tridentblur_alphaStart +
                                blurLerp * cfg_neptune->tridentblur_alphaEnd;
                F32 lerpColor = (1.0f - blurLerp) * cfg_neptune->tridentblur_colorStart +
                                blurLerp * cfg_neptune->tridentblur_colorEnd;

                memcpy(blurTrident[i]->Mat, model->Mat, (model->BoneCount + 1) * sizeof(RwMatrix));

                for (j = 0; j < 4 && tridentInfl[j] != -1; j++)
                {
                    xQuat lerpQuat;
                    xVec3 lerpTran;

                    xQuatSlerp(&lerpQuat, &blurQuat[blurInt][j], &blurQuat[blurInt + 1][j],
                               blurFrac);
                    xVec3Lerp(&lerpTran, &blurTran[blurInt][j], &blurTran[blurInt + 1][j],
                              blurFrac);
                    xQuatToMat(&lerpQuat, (xMat3x3*)&blurTrident[i]->Mat[tridentInfl[j]]);
                    blurTrident[i]->Mat[tridentInfl[j]].pos = *(RwV3d*)&lerpTran;
                }

                blurTrident[i]->LightKit = model->LightKit;
                blurTrident[i]->Alpha = lerpAlpha;
                blurTrident[i]->RedMultiplier = lerpColor;
                blurTrident[i]->GreenMultiplier = lerpColor;
                blurTrident[i]->BlueMultiplier = lerpColor;
                iModelAnimBoundWorld(&blurTrident[i]->animBound, NULL, blurTrident[i]->Data,
                                     blurTrident[i]->Mat, blurTrident[i]->Mat + 1);
                xModelRender(blurTrident[i]);
            }
        }
    }
}

void zNMENeptune::RenderExtra()
{
    flg_xtrarend = 1;

    if (povBeamFade || povBucketFade)
    {
        F32 colorScale = 255.0f;
        F32 alphaScale = 160.0f;

        xFXHighDynamicRangeBrighten(true);

        F32 faderad = povBeamFade * 0.8f + 0.2f;

        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);

        if (povStunnedMe && !ScopeViewActive())
        {
            RwMatrixTag matCopy = *collideBucket->model->Mat;

            BucketCollideTransform(1.1f);

            if (!xModelSphereCull(iModelWorldBoundSphere(collideBucket->model->Data,
                                                         collideBucket->model->Mat, NULL)))
            {
                RpMaterial* mat = collideBucket->model->Data->geometry->matList.materials[0];
                RwRGBA oldcolor = mat->color;
                RwTexture* oldtexture = mat->texture;

                collideBucket->model->Data->geometry->flags |= rpGEOMETRYMODULATEMATERIALCOLOR;
                mat->texture = rwpovbeamtex;
                mat->color.red = colorScale * povBucketFade;
                mat->color.green = colorScale * povBucketFade;
                mat->color.blue = colorScale * povBucketFade;
                mat->color.alpha = alphaScale * povBucketFade * 0.5f;

                xRenderFixUntexturedBegin(collideBucket->model->Data);
                iModelRender(collideBucket->model->Data, collideBucket->model->Mat);
                xRenderFixUntexturedEnd(collideBucket->model->Data);

                mat->color = oldcolor;
                mat->texture = oldtexture;
            }

            *collideBucket->model->Mat = matCopy;
        }

        if (povBeamFade)
        {
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)rwpovbeamtex->raster);

            F32 uoffset = -(povBeamU - std::floorf(povBeamU));
            xVec3 beamtips[3];
            xVec3 beamstart;
            xVec3 beamend;
            xVec3 beamdir;
            U32 i;
            xColor_tag fadecolor;
            xVec3 prevReflect;
            xVec3 nextReflect;

            if (ScopeViewActive())
            {
                for (i = 0; i < 3; i++)
                {
                    xMat4x3Toworld(&beamtips[i], &povBeamMat, &cfg_neptune->projPOVTag[i]);
                }
            }
            else
            {
                beamtips[0] = currTag[0];
                beamtips[1] = currTag[2];
                beamtips[2] = currTag[3];
            }

            for (i = 1; i < 3; i++)
            {
                xVec3Lerp(&beamtips[i], &beamtips[0], &beamtips[i], faderad);
            }

            beamstart.x = beamtips[0].x + povBeamMat.at.x;
            beamstart.y = beamtips[0].y + povBeamMat.at.y;
            beamstart.z = beamtips[0].z + povBeamMat.at.z;

            beamend = povBeamHit;
            beamdir = beamend - beamstart;
            xVec3Normalize(&beamdir, &beamdir);

            if (Neptune::inPreRender)
            {
                static F32 scopeBeamAdjust[3][2] = {
                    { -0.5f, -1.0f },
                    { -0.2f, -0.5f },
                    { -0.2f, -0.5f },
                };

                for (i = 0; i < 3; i++)
                {
                    beamtips[i].x += povBeamMat.up.x * scopeBeamAdjust[i][0] +
                                     povBeamMat.at.x * scopeBeamAdjust[i][1];
                    beamtips[i].y += povBeamMat.up.y * scopeBeamAdjust[i][0] +
                                     povBeamMat.at.y * scopeBeamAdjust[i][1];
                    beamtips[i].z += povBeamMat.up.z * scopeBeamAdjust[i][0] +
                                     povBeamMat.at.z * scopeBeamAdjust[i][1];
                }

                alphaScale = colorScale;
            }

            fadecolor.r = colorScale * povBeamFade;
            fadecolor.g = colorScale * povBeamFade;
            fadecolor.b = colorScale * povBeamFade;
            fadecolor.a = alphaScale * povBeamFade;
            RenderLightBeam(&beamstart, &beamend, &beamdir, NULL, NULL, faderad * 0.1125f,
                            faderad * 0.15f, fadecolor, uoffset - 4.0f, uoffset + 4.0f, 0.0f, 1.0f,
                            8, 12);

            fadecolor.r = colorScale * 0.4f * povBeamFade;
            fadecolor.g = colorScale * 0.4f * povBeamFade;
            fadecolor.b = colorScale * 0.4f * povBeamFade;
            fadecolor.a = alphaScale * 0.4f * povBeamFade;
            RenderLightBeam(&beamtips[0], &beamstart, &beamdir, NULL, NULL, faderad * 0.005f,
                            faderad * 0.1125f, fadecolor, uoffset - 4.5f, uoffset - 4.0f, 0.0f,
                            1.0f, 1, 12);

            fadecolor.r = colorScale * 0.3f * povBeamFade;
            fadecolor.g = colorScale * 0.3f * povBeamFade;
            fadecolor.b = colorScale * 0.3f * povBeamFade;
            fadecolor.a = alphaScale * 0.3f * povBeamFade;
            RenderLightBeam(&beamtips[1], &beamstart, &beamdir, NULL, NULL, faderad * 0.005f,
                            faderad * 0.1125f, fadecolor, uoffset - 4.5f, uoffset - 4.0f, 0.0f,
                            1.0f, 1, 12);

            fadecolor.r = colorScale * 0.3f * povBeamFade;
            fadecolor.g = colorScale * 0.3f * povBeamFade;
            fadecolor.b = colorScale * 0.3f * povBeamFade;
            fadecolor.a = alphaScale * 0.3f * povBeamFade;
            RenderLightBeam(&beamtips[2], &beamstart, &beamdir, NULL, NULL, faderad * 0.005f,
                            faderad * 0.1125f, fadecolor, uoffset - 4.5f, uoffset - 4.0f, 0.0f,
                            1.0f, 1, 12);

            prevReflect = beamend;
            fadecolor.r = colorScale * povBeamFade;
            fadecolor.g = colorScale * povBeamFade;
            fadecolor.b = colorScale * povBeamFade;
            fadecolor.a = alphaScale * povBeamFade;

            if (povBeamReflected)
            {
                nextReflect = povBeamReflectDir * 40.0f;
                beamdir = nextReflect - prevReflect;
                xVec3Normalize(&beamdir, &beamdir);
                RenderLightBeam(&prevReflect, &nextReflect, &beamdir, NULL, NULL, faderad * 0.15f,
                                faderad * 0.15f, fadecolor, uoffset - 4.0f, uoffset + 4.0f, 0.0f,
                                1.0f, 1, 12);
            }
            else
            {
                for (i = 0; i < povHitReflectors; i++)
                {
                    if (i == 5)
                    {
                        if (ScopeViewActive())
                        {
                            nextReflect.assign(0.0f, 3.0f, 0.0f);
                        }
                        else
                        {
                            if (!povStunTagCalced)
                            {
                                povStunTag =
                                    (currTag[9] + currTag[10] + currTag[11] + currTag[12]) * 0.25f;
                                povStunTagCalced = 1;
                            }
                            nextReflect = povStunTag;
                        }
                    }
                    else
                    {
                        if (entReflectors[i] == NULL)
                        {
                            break;
                        }

                        xMat4x3Toworld(&nextReflect, (xMat4x3*)entReflectors[i]->model->Mat,
                                       &cfg_neptune->pov_reflectorTargetPoint);
                    }

                    beamdir = nextReflect - prevReflect;
                    xVec3Normalize(&beamdir, &beamdir);
                    RenderLightBeam(&prevReflect, &nextReflect, &beamdir, NULL, NULL,
                                    faderad * 0.15f, faderad * 0.15f, fadecolor, uoffset - 4.0f,
                                    uoffset + 4.0f, 0.0f, 1.0f, 1, 12);
                    prevReflect = nextReflect;
                }
            }
        }

        xFXHighDynamicRangeBrighten(false);
    }
}

// No GC symbol (inlined into Setup; the name is ours, after MeterUpdate/MeterRender). Defined
// after Process so that it is compiled first, for retail's string pool order.
void zNMENeptune::MeterSetup()
{
    Neptune::health_meter.runtime.rast_inner = NMEC_FindRWRaster("fx_boss_bar");
    Neptune::health_meter.runtime.rast_outer = NMEC_FindRWRaster("fx_boss_graph");
    Neptune::health_meter.runtime.rast_solid_bar = NMEC_FindRWRaster("fx_boss_solid_bar");
    Neptune::health_meter.runtime.rast_broken_bar = NMEC_FindRWRaster("fx_boss_broken_bar");

    Neptune::health_meter.runtime.rast_logo = NMEC_FindRWRaster("uii_boss_meter_b101");
    if (Neptune::health_meter.runtime.rast_logo == NULL)
    {
        Neptune::health_meter.runtime.rast_logo = NMEC_FindRWRaster("uii_boss_meter_b201");
    }
    if (Neptune::health_meter.runtime.rast_logo == NULL)
    {
        Neptune::health_meter.runtime.rast_logo = NMEC_FindRWRaster("uii_boss_meter_b301");
    }
    if (Neptune::health_meter.runtime.rast_logo == NULL)
    {
        Neptune::health_meter.runtime.rast_logo = NMEC_FindRWRaster("uii_boss_meter_b401");
    }

    Neptune::health_meter.cfginfo.tym_fadeIn = 0.14f;
    Neptune::health_meter.cfginfo.tym_fadeOut = 0.25f;

    Neptune::health_meter.status.healthCurr[1] = cfg_neptune->stageHealth[0] * 2;
    Neptune::health_meter.status.healthCurr[2] = cfg_neptune->stageHealth[1] * 2;
    Neptune::health_meter.status.healthCurr[3] = cfg_neptune->stageHealth[2] * 2;
    Neptune::health_meter.cfginfo.stageHealth[1] = cfg_neptune->stageHealth[0] * 2;
    Neptune::health_meter.cfginfo.stageHealth[2] = cfg_neptune->stageHealth[1] * 2;
    Neptune::health_meter.cfginfo.stageHealth[3] = cfg_neptune->stageHealth[2] * 2;
}

void zNMENeptune::MeterUpdate()
{
    flg_hudrend = 1;
    Neptune::health_meter.status.healthCurr[1] = (bossStage < 1) ? pts_health[0] * 2 : 0;
    Neptune::health_meter.status.healthCurr[2] = (bossStage < 2) ? pts_health[1] * 2 : 0;
    Neptune::health_meter.status.healthCurr[3] = pts_health[2] * 2;
}

void zNMENeptune::MeterRender()
{
    Neptune::health_meter.Render();
}

void zNMENeptune::RenderHud()
{
    MeterRender();
}

S32 zNMENeptune::TypeHandleMail(NMEMsg* mail)
{
    S32 handled;

    switch (mail->msgid)
    {
    case NME_MID_SYSEVENT:
        handled = DfltSysEventMail(&mail->sysevent);
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}
