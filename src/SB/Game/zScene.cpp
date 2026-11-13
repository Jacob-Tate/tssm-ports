#include "zScene.h"

#include "zRewards.h"
#include "zReactiveAnimation.h"
#include "zRenderState.h"
#include "zEntDestructObj.h"
#include "zParCmd.h"
#include "zAnimList.h"
#include "zDispatcher.h"
#include "zCameraTweak.h"
#include "zCamMarker.h"
#include "zMusic.h"
#include "zGrid.h"
#include "xDecal.h"
#include "xFog.h"
#include "zTalkBox.h"
#include "zTaskBox.h"
#include "zUIFont.h"
#include "zEntButton.h"
#include "xSkyDome.h"
#include "zLOD.h"
#include "zGame.h"
#include "zGoo.h"
#include "iModel.h"
#include "xShadow.h"
#include "zLightning.h"
#include "zLight.h"
#include "zActionLine.h"
#include "xPtankPool.h"
#include "zParPTank.h"
#include "zNPCMgr.h"
#include "xDebug.h"
#include "xScrFx.h"
#include "xScrPulseGlare.h"
#include "xTextAsset.h"
#include "zMenuSB.h"
#include "zGameExtras.h"
#include "zRaceTimer.h"
#include "zCutsceneMgr.h"
#include "zConditional.h"
#include "zEGenerator.h"
#include "zCameraFly.h"
#include "zEntTeleportBox.h"
#include "zEntTrigger.h"
#include "zEntPickup.h"
#include "zEntSimpleObj.h"
#include "xEntBoulder.h"
#include "zScript.h"
#include "zDiscoFloor.h"
#include "zPortal.h"
#include "zPlatform.h"
#include "zPendulum.h"
#include "zCutsceneMgr.h"
#include "zVolume.h"
#include "xHudModel.h"
#include "xHudMeter.h"
#include "xHudText.h"
#include "xHudImage.h"
#include "xRumbleEmitter.h"
#include "zUIText.h"
#include "zUIImage.h"
#include "zUIModel.h"
#include "zUIBox.h"
#include "zUIUserString.h"
#include "zUIController.h"
#include "xScreenFade.h"
#include "zFlameEmitter.h"
#include "zSmoke.h"
#include "zJSPExtraData.h"
#include "zSpotlight.h"
#include "zBusStop.h"
#include "zEntPlayerBungeeState.h"
#include "zVentType.h"
#include "zVent.h"
#include "zRing.h"
#include "zRingControl.h"
#include "xFunctionGenerator.h"
#include "zReference.h"
#include "zEntHangable.h"
#include "zMovePoint.h"
#include "zEnv.h"
#include "xTimer.h"
#include "xGroup.h"
#include "xCounter.h"
#include "xSurface.h"
#include "xVolume.h"
#include "xScreenWarp.h"
#include "xFXHighDynamicRange.h"
#include "xTRC.h"
#include "xCM.h"
#include "xDebug.h"
#include "xMath.h"
#include "xutil.h"
#include "iCamera.h"
#include "iDraw.h"
#include "iScrFX.h"
#include "zEntCruiseBubble.h"
#include "zTextBox.h"
#include "zHud.h"
#include "zUIMgr.h"
#include "zNMECentral.h"
#include "zNMEShrapnel.h"
#include "zNMERumble.h"
#include "xAntenna.h"
#include "zCineFX.h"
#include "zTaxi.h"
#include "zMenu.h"
#include "zFX.h"
#include "zFXRibbonPool.h"
#include "zSurface.h"
#include "zThrown.h"
#include "zPlayerBase.h"
#include "zEntPlayerFX.h"
#include "zEntPlayerOOBState.h"
#include "zLightWeight.h"
#include "zSplash.h"
#include "zShrapnel.h"
#include "zDust.h"
#include "zEntPlayerCar.h"
#include "zFire.h"
#include "zExplosion.h"
#include "zParticleGenerator.h"
#include "zParSys.h"
#include "zCam.h"
#include "xRumbleManager.h"
#include "xSnd.h"
#include "xTransient.h"
#include "zParticleCustom.h"
#include "zStreamedSound.h"
#include "zTaxi.h"
#include "xParticleBatch.h"
#include "xModelBucket.h"
#include "xAntenna.h"
#include "xLightKit.h"
#include "xLightVolume.h"
#include "xModelBlur.h"
#include "xMarkerAsset.h"
#include "xRenderState.h"
#include "xString.h"
#include "iTime.h"
#include "zNMEAsset.h"
#include "zNMETypeTiki.h"
#include "zNMECommon.h"
#include "zCollGeom.h"
#include "zDestructible.h"
#include "zMain.h"
#include "xQuickCull.h"
#include "xPartition.h"
#include "xFont.h"
#include "xShadowSimple.h"
#include "zSymbol.h"

#include <stdlib.h>
#include <string.h>

#ifdef PLATFORM_PC
#include "iCoop.h"
#endif

static S32 bytesNeeded;
static S32 availOnDisk;
static S32 neededFiles;
static F32 offsetx;
static F32 offsety;
static U32 enableScreenAdj;
static F32 oldOffsetx;
static F32 oldOffsety;
static S32 sMemDepthSceneStart = -1;
static S32 sMemDepthJustHIPStart = -1;

struct pointer_asset : xDynAsset
{
    xVec3 loc;
    F32 yaw;
    F32 pitch;
    F32 roll;

    static const char* type_name()
    {
        return "pointer";
    }
};

// No symbol (always inlined); the name is a guess, after zGame.cpp's pointer check.
static inline bool zSceneIsPointerAsset(const xDynAsset* a)
{
    return a->baseType == 0 && a->version == 1 && a->type == xStrHash(pointer_asset::type_name());
}

static U32 zSceneInitFunc_DefaultEnt(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx);
static U32 zSceneInitFunc_Default(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx);
static U32 zSceneInitFunc_MovePoint(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx);
static U32 zSceneInitFunc_Player(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx);
static U32 zSceneInitFunc_Camera(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx);
static U32 zSceneInitFunc_Surface(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx);
static U32 zSceneInitFunc_Volume(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx);
static U32 zSceneInitFunc_LobMaster(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx);
static U32 zSceneInitFunc_Dispatcher(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx);

// clang-format off
static zSceneObjectInstanceDesc sInitTable[] =
{
    { "Trig", eBaseTypeTrigger, 'TRIG', sizeof(zEntTrigger), zSceneInitFunc_DefaultEnt, zEntTriggerInit, NULL },
    { "Move Point", eBaseTypeMovePoint, 'MVPT', sizeof(zMovePoint), zSceneInitFunc_MovePoint, NULL, NULL },
    { "Pickup", eBaseTypePickup, 'PKUP', sizeof(zEntPickup), zSceneInitFunc_DefaultEnt, zEntPickupInit, NULL },
    { "ParticleSystem", eBaseTypeParticleSystem, 'PARS', sizeof(xParSys), zSceneInitFunc_Default, xParSysInit, NULL },
    { "ParticleEmitter", eBaseTypeParticleEmitter, 'PARE', sizeof(xParEmitter), zSceneInitFunc_Default, xParEmitterInit, NULL },
    { "Simple", eBaseTypeStatic, 'SIMP', sizeof(zEntSimpleObj), zSceneInitFunc_DefaultEnt, zEntSimpleObj_Init, NULL },
    { "Track", eBaseTypeTrackPhysics, 'TRCK', sizeof(zEntSimpleObj), zSceneInitFunc_DefaultEnt, zEntTrackPhysics_Init, NULL },
    { "Platform", eBaseTypePlatform, 'PLAT', sizeof(zPlatform), zSceneInitFunc_DefaultEnt, zPlatform_Init, NULL },
    { "Pendulum", eBaseTypePendulum, 'PEND', sizeof(_zPendulum), zSceneInitFunc_DefaultEnt, zPendulum_Init, NULL },
    { "Hanger", eBaseTypeHangable, 'HANG', sizeof(zEntHangable), zSceneInitFunc_DefaultEnt, zEntHangable_Init, NULL },
    { "Boulder", eBaseTypeBoulder, 'BOUL', sizeof(xEntBoulder), zSceneInitFunc_DefaultEnt, xEntBoulder_Init, NULL },
    { "Button", eBaseTypeButton, 'BUTN', sizeof(_zEntButton), zSceneInitFunc_DefaultEnt, zEntButton_Init, NULL },
    { "EGenerator", eBaseTypeEGenerator, 'EGEN', sizeof(zEGenerator), zSceneInitFunc_DefaultEnt, zEGenerator_Init, NULL },
    { "Player", eBaseTypePlayer, 'PLYR', sizeof(zEnt), zSceneInitFunc_Player, NULL, NULL },
    { "Timer", eBaseTypeTimer, 'TIMR', sizeof(xTimer), zSceneInitFunc_Default, xTimerInit, NULL },
    { "Counter", eBaseTypeCounter, 'CNTR', sizeof(_xCounter), zSceneInitFunc_Default, xCounterInit, NULL },
    { "SoundFX", eBaseTypeSoundFX, 'SDFX', sizeof(xSoundFX), zSceneInitFunc_Default, xSoundFXInit, NULL },
    { "Group", eBaseTypeGroup, 'GRUP', sizeof(xGroup), zSceneInitFunc_Default, xGroupInit, NULL },
    { "Portal", eBaseTypePortal, 'PORT', sizeof(_zPortal), zSceneInitFunc_Default, zPortalInit, NULL },
    { "Camera", eBaseTypeCamera, 'CAM ', sizeof(zCamMarker), zSceneInitFunc_Camera, NULL, NULL },
    { "Surface", eBaseTypeSurface, 'SURF', sizeof(xSurface), zSceneInitFunc_Surface, NULL, NULL },
    { "Volume", eBaseTypeVolume, 'VOLU', sizeof(zVolume), zSceneInitFunc_Volume, NULL, NULL },
    { "Conditional", eBaseTypeCond, 'COND', sizeof(_zConditional), zSceneInitFunc_Default, zConditionalInit, NULL },
    { "Lob Master", eBaseTypeLobMaster, 'LOBM', 0, zSceneInitFunc_LobMaster, NULL, NULL },
    { "Env", eBaseTypeEnv, 'ENV ', sizeof(_zEnv), zSceneInitFunc_Default, zEnvInit, NULL },
    { "Dispatcher", eBaseTypeDispatcher, 'DPAT', 0, zSceneInitFunc_Dispatcher, NULL, NULL },
    { "Fog", eBaseTypeFog, 'FOG ', sizeof(_xFog), zSceneInitFunc_Default, xFogInit, NULL },
    { "Light", eBaseTypeLight, 'LITE', sizeof(_zLight), zSceneInitFunc_Default, zLightInit, NULL },
    { "CutSceneMgr", eBaseTypeCutsceneMgr, 'CSNM', sizeof(zCutsceneMgr), zSceneInitFunc_Default, zCutsceneMgrInit, NULL },
    { "Script", eBaseTypeScript, 'SCRP', sizeof(_zScript), zSceneInitFunc_Default, zScriptInit, NULL },
    { "Disco Floor", eBaseTypeDiscoFloor, 'DSCO', sizeof(z_disco_floor), zSceneInitFunc_Default, z_disco_floor::init, NULL },
    { NULL }
};
// clang-format on

namespace
{
    struct dynamic_type_data
    {
        const char* name;
        S32 type;
        size_t size;
        bool is_ent;
        void (*load)(xBase& data, xDynAsset& asset, size_t size);
    };

    // clang-format off
    const dynamic_type_data dynamic_types[] =
    {
        { "hud:model", eBaseTypeHUD_model, sizeof(xBase) + sizeof(xhud::model_widget), false, xhud::model_widget::load },
        { "hud:meter:font", eBaseTypeHUD_font_meter, sizeof(xBase) + sizeof(xhud::font_meter_widget), false, xhud::font_meter_widget::load },
        { "hud:meter:unit", eBaseTypeHUD_unit_meter, sizeof(xBase) + sizeof(xhud::unit_meter_widget), false, xhud::unit_meter_widget::load },
        { "hud:text", eBaseTypeHUD_text, sizeof(xBase) + sizeof(xhud::text_widget), false, xhud::text_widget::load },
        { "hud:image", eBaseTypeHUD_image, sizeof(xBase) + sizeof(xhud::image_widget), false, xhud::image_widget::load },
        { "game_object:text_box", eBaseTypeTextBox, sizeof(ztextbox), false, ztextbox::load },
        { "game_object:talk_box", eBaseTypeTalkBox, sizeof(ztalkbox), false, ztalkbox::load },
        { "game_object:task_box", eBaseTypeTaskBox, sizeof(ztaskbox), false, ztaskbox::load },
        { "game_object:BoulderGenerator", eBaseTypeBoulderGenerator, sizeof(xBoulderGenerator), false, xBoulderGenerator_Init },
        { "game_object:Flythrough", eBaseTypeCameraFly, sizeof(zCameraFly), false, zCameraFly_Init },
        { "game_object:Camera_Tweak", eBaseTypeCameraTweak, sizeof(zCameraTweak), false, zCameraTweak_Init },
        { "effect:Rumble", eBaseTypeRumbleEffect, sizeof(xRumble::effect), false, xRumble::effect::Init },
        { "effect:Rumble Spherical Emitter", eBaseTypeRumbleSphericalEmitter, sizeof(xRumble::sphericalEmitter), false, xRumble::sphericalEmitter::Init },
        { "effect:Rumble Box Emitter", eBaseTypeRumbleBoxEmitter, sizeof(xRumble::boxEmitter), false, xRumble::boxEmitter::Init },
        { "ui:text", eBaseTypeUIText, sizeof(zUIText), false, zUIText_Init },
        { "ui:image", eBaseTypeUIImage, sizeof(zUIImage), false, zUIImage_Init },
        { "ui:model", eBaseTypeUIModel, sizeof(zUIModel), false, zUIModel_Init },
        { "ui:box", eBaseTypeUIBox, sizeof(zUIBox), false, zUIBox_Init },
        { "ui:text:user string", eBaseTypeUIUserString, sizeof(zUIUserString), false, zUIUserString_Init },
        { "ui:controller", eBaseTypeUIController, sizeof(zUIController), false, zUIController_Init },
        { "effect:ScreenFade", eBaseTypeScreenFade, sizeof(xScreenFade), false, xScreenFade::load },
        { "game_object:flame_emitter", eBaseTypeFlameEmitter, sizeof(zFlameEmitter), false, zFlameEmitter::load },
        { "effect:smoke_emitter", eBaseTypeSmokeEmitter, sizeof(zSmokeEmitter), false, zSmokeEmitter::load },
        { "JSP Extra Data", eBaseTypeJSPExtraData, sizeof(zJSPExtraData), false, zJSPExtraData::Load },
        { "effect:Lightning", eBaseTypeLightning, sizeof(zLightningWidget), false, zLightningWidget::Init },
        { "effect:spotlight", eBaseTypeSpotlight, sizeof(zSpotlight), false, zSpotlight::load },
        { "game_object:Teleport", eBaseTypeTeleportBox, sizeof(_zEntTeleportBox), true, zEntTeleportBox_Init },
        { "game_object:BusStop", eBaseTypeBusStop, sizeof(zBusStop), false, zBusStop_Init },
        { "game_object:bungee_hook", eBaseTypeBungeeHook, sizeof(bungee_state::hook_type), false, bungee_state::load },
        { "game_object:VentType", eBaseTypeVentType, sizeof(zVentType), false, zVentType::load },
        { "game_object:Vent", eBaseTypeVent, sizeof(zVent), false, zVent::load },
        { "game_object:RaceTimer", eBaseTypeRaceTimer, sizeof(zRaceTimer), false, zRaceTimer::Load },
        { "game_object:Ring", eBaseTypeRing, sizeof(zRing), false, zRing::Load },
        { "game_object:RingControl", eBaseTypeRingControl, sizeof(zRingControl), false, zRingControl::Load },
        { "logic:Function Generator", eBaseTypeFunctionGenerator, sizeof(xFunctionGenerator), false, xFunctionGenerator_Init },
        { "logic:reference", eBaseTypeReference, sizeof(zReference), false, zReferenceInit }
    };
    // clang-format on
} // namespace

// 'PIPT' asset: the count, then that many pipe tables. The name is ours (the DWARFs type the data
// as void*).
struct xModelPipeAssetHeader
{
    S32 count;
    xModelPipeTable info[1];
};

_zEnv* gCurEnv;
static U32 sPipeSceneAssetID;
U32 gTransitionSceneID;

struct zSceneLevel
{
    const char* desc;
    const char* prefix;
};

struct IDBasePair
{
    U32 id;
    xBase* base;
};

static void zSceneObjHashtableInit(S32 count);
static void zSceneObjHashtableExit();
static S32 zSceneObjHashtableUsage();
void zSceneObjHashtableAdd(U32 id, xBase* base);
static xBase* zSceneObjHashtableGet(U32 id);
static xBase* zSceneExitSoundIteratorCB(xBase* b, zScene*, void*);
static void zSceneAutoSave();

void zSceneRenderPreFX();
static void zSceneRenderPostFX();
S32 zSceneSetup_serialTraverseCB(U32 clientID, xSerial* xser);
void zSceneResetLightKits();

namespace
{
    // The two asserts (as in Ratatouille) are compiled out, but they keep this function from being
    // auto-inlined; retail calls it.
    U32 init_dynamic_type(zScene& s, U32 index, const dynamic_type_data& d)
    {
        S32 count;
        U32 type;
        S32 dyn_size;
        S32 i;
        S32 cnt;

        xASSERT((d.type >= 0) && (d.type < eBaseTypeCount));
        s.baseList[d.type] = NULL;

        if (s.baseCount[d.type] == 0)
        {
            return index;
        }

        if (d.size)
        {
            dyn_size = s.baseCount[d.type] * d.size;
#ifdef PLATFORM_PC
            // A host compiler puts a polymorphic type's vptr at offset 0, ahead of its xBase, and
            // the load functions build each object where its xBase is the slot (xRumble::*::Init,
            // zUI*_Init), so the first object starts before the array. Room for it.
            s.baseList[d.type] = (xBase*)((U8*)xMemAllocSize(dyn_size + 16) + 16);
#else
            s.baseList[d.type] = (xBase*)xMemAllocSize(dyn_size);
#endif
            xASSERT(s.baseList[d.type]);
        }

        type = xStrHash(d.name);
        count = xSTAssetCountByType('DYNA');

        for (i = 0, cnt = 0; i < count; i++)
        {
            U32 asset_size;
            xDynAsset* a;
            xBase* b;

            a = (xDynAsset*)xSTFindAssetByType('DYNA', i, &asset_size);

            if (a && a->type == type)
            {
                b = NULL;
                a->baseType = d.type;

                if (d.size)
                {
                    b = (xBase*)((U8*)s.baseList[d.type] + cnt * d.size);
                    zSceneSet(b, index);
                }

                if (d.load)
                {
                    d.load(*b, *a, asset_size);
                }

                if (d.size)
                {
                    if (d.is_ent)
                    {
                        xSceneAddEnt(&s, (xEnt*)s.base[index]);
                    }

                    zSceneObjHashtableAdd(b->id, b);

                    index++;
                    cnt++;
                }
            }
        }

        return index;
    }

    S32 count_dynamic_types(const char* name)
    {
        U32 type = xStrHash(name);
        S32 dynaCount = xSTAssetCountByType('DYNA');
        S32 count = 0;

        for (S32 i = 0; i < dynaCount; i++)
        {
            U32 size;
            xDynAsset* asset = (xDynAsset*)xSTFindAssetByType('DYNA', i, &size);

            if (asset && asset->type == type)
            {
                count++;
            }
        }

        return count;
    }

    void add_dynamic_types(zScene& s)
    {
        for (S32 i = 0; i < sizeof(dynamic_types) / sizeof(dynamic_types[0]); i++)
        {
            S32 count = count_dynamic_types(dynamic_types[i].name);

            s.baseCount[dynamic_types[i].type] = count;

            if (dynamic_types[i].size)
            {
                s.num_base += count;
            }
        }
    }

    U32 init_dynamic_types(zScene& s, U32 index)
    {
        for (S32 i = 0; i < sizeof(dynamic_types) / sizeof(dynamic_types[0]); i++)
        {
            if (dynamic_types[i].load)
            {
                index = init_dynamic_type(s, index, dynamic_types[i]);
            }
        }

        return index;
    }
} // namespace

namespace zTransient
{
    // Defined in zTransient.cpp (later in this wad; auto-inlined into zSceneInit).
    void scene_enter();
} // namespace zTransient

static U32 zSceneInitFunc_DefaultEnt(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx)
{
    U8* block;
    S32 count;
    U32 assetSize, offset;
    xBase* b;

    block = NULL;
    assetSize = 0;
    offset = desc->sizeRuntime;
    count = s->baseCount[desc->baseType];

    if (count)
    {
        block = (U8*)xMemAllocSize(count * offset);

        s->baseList[desc->baseType] = (xBase*)block;
    }

    for (S32 i = 0; i < count; i++)
    {
        void* asset = xSTFindAssetByType(desc->assetType, i, &assetSize);
        b = (xBase*)(block + i * offset);

        zSceneSet(b, base_idx);

        if (desc->objectInitFunc)
        {
            desc->objectInitFunc(b, asset);
        }

        xSceneAddEnt(s, (xEnt*)s->base[base_idx]);
        zSceneObjHashtableAdd(b->id, b);

        base_idx++;
    }

    return base_idx;
}

static U32 zSceneInitFunc_Default(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx)
{
    U8* block;
    S32 count;
    U32 assetSize, offset;
    xBase* b;

    block = NULL;
    assetSize = 0;
    offset = desc->sizeRuntime;
    count = s->baseCount[desc->baseType];

    if (count)
    {
        block = (U8*)xMemAllocSize(count * offset);

        s->baseList[desc->baseType] = (xBase*)block;
    }

    for (S32 i = 0; i < count; i++)
    {
        void* asset = xSTFindAssetByType(desc->assetType, i, &assetSize);
        b = (xBase*)(block + i * offset);

        zSceneSet(b, base_idx);

        if (desc->objectInitFunc)
        {
            desc->objectInitFunc(b, asset);
        }

        zSceneObjHashtableAdd(b->id, b);

        base_idx++;
    }

    return base_idx;
}

static U32 zSceneInitFunc_MovePoint(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx)
{
    S32 count;
    U32 assetSize;
    zMovePoint* movpBlock;

    assetSize = 0;
    count = s->baseCount[desc->baseType];
    movpBlock = zMovePoint_GetMemPool(count);

    if (movpBlock)
    {
        s->baseList[desc->baseType] = movpBlock;

        for (S32 idx = 0; idx < count; idx++)
        {
            xBase* b = zMovePoint_GetInst(idx);
            xBaseAsset* basset = (xBaseAsset*)xSTFindAssetByType('MVPT', idx, &assetSize);

            zSceneSet(b, base_idx);
            zMovePointInit(zMovePoint_GetInst(idx), (xMovePointAsset*)basset);
            zSceneObjHashtableAdd(b->id, b);

            base_idx++;
        }
    }

    return base_idx;
}

static U32 zSceneInitFunc_Player(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx)
{
    S32 count;
    zEnt* entBlock;

    count = s->baseCount[desc->baseType];

    if (count)
    {
        entBlock = (zEnt*)xMemAllocSize(count * sizeof(zEnt));

        s->baseList[desc->baseType] = entBlock;

        for (S32 idx = 0; idx < count; idx++)
        {
            xBase* b = xglobals->___player_ent_dont_use_directly;
            xEntAsset* asset;

            zSceneSet(b, base_idx);

            b->baseType = eBaseTypePlayer;
            b->baseFlags |= 0x20;

            if (idx == count - 1)
            {
                xSceneAddEnt(s, (xEnt*)s->base[base_idx]);
            }

            asset = (xEntAsset*)xSTFindAssetByType('PLYR', idx, NULL);

            b->id = asset->id;

            zSceneObjHashtableAdd(b->id, b);

            base_idx++;
        }
    }

    return base_idx;
}

static U32 zSceneInitFunc_Camera(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx)
{
    S32 count;
    zCamMarker* camBlock;

    count = s->baseCount[desc->baseType];

    if (count)
    {
        camBlock = (zCamMarker*)xMemAllocSize(count * sizeof(zCamMarker));
        s->baseList[desc->baseType] = camBlock;

        for (S32 idx = 0; idx < count; idx++)
        {
            xBase* b = &camBlock[idx];
            xCamAsset* assetCam = (xCamAsset*)xSTFindAssetByType('CAM ', idx, NULL);

            zSceneSet(b, base_idx);
            zCamMarkerInit(b, assetCam);
            zSceneObjHashtableAdd(b->id, b);

            base_idx++;
        }
    }

    return base_idx;
}

static U32 zSceneInitFunc_Surface(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx)
{
    S32 count;

    count = s->baseCount[desc->baseType];

    if (count)
    {
        s->baseList[desc->baseType] = xSurfaceGetByIdx(0);

        for (S32 idx = 0; idx < count; idx++)
        {
            xBase* b = xSurfaceGetByIdx(idx);

            zSceneSet(b, base_idx);
            zSceneObjHashtableAdd(b->id, b);

            base_idx++;
        }
    }

    return base_idx;
}

static U32 zSceneInitFunc_Volume(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx)
{
    S32 count;

    count = s->baseCount[desc->baseType];

    zVolumeInit();

    if (count)
    {
        s->baseList[desc->baseType] = zVolumeGetVolume(0);

        for (S32 idx = 0; idx < count; idx++)
        {
            xBase* b = zVolumeGetVolume(idx);

            zLightSetVolume((zVolume*)b);

            zSceneSet(b, base_idx);
            zSceneObjHashtableAdd(b->id, b);

            base_idx++;
        }
    }

    return base_idx;
}

static U32 zSceneInitFunc_LobMaster(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx)
{
    return base_idx;
}

static U32 zSceneInitFunc_Dispatcher(zScene* s, zSceneObjectInstanceDesc* desc, U32 base_idx)
{
    S32 count;

    count = s->baseCount[desc->baseType];

    if (count)
    {
        st_ZDISPATCH_DATA* dpat_pool = zDispatcher_memPool(count);
        s->baseList[desc->baseType] = dpat_pool;

        for (S32 idx = 0; idx < count; idx++)
        {
            xBase* b = zDispatcher_getInst(dpat_pool, idx);
            xEntAsset* asset = (xEntAsset*)xSTFindAssetByType('DPAT', idx, NULL);

            zSceneSet(b, base_idx);
            zDispatcher_Init((st_ZDISPATCH_DATA*)b, asset);
            zSceneObjHashtableAdd(b->id, b);

            base_idx++;
        }
    }

    return base_idx;
}

void zSceneSet(xBase* b, S32 index)
{
    globals.sceneCur->base[index] = b;
}

static void PipeCountStuffCB(RpAtomic*, xModelPipe&, U32)
{
    xModelLookupCount++;
}

static void PipeAddStuffCB(RpAtomic* data, xModelPipe& pipe, U32)
{
    xModelLookupList[xModelLookupCount].model = data;
    xModelLookupList[xModelLookupCount].Pipe = pipe;
    xModelLookupCount++;
}

static void PipeForAllSceneModels(void (*pipeCB)(RpAtomic* data, xModelPipe& pipe, U32 subObjects))
{
    static xModelPipe zeroPipe = {};

    S32 i, j, k;
    S32 numModels = xSTAssetCountByType('MODL');

    for (i = 0; i < numModels; i++)
    {
        RpAtomic* model = (RpAtomic*)xSTFindAssetByType('MODL', i, NULL);

        if (model)
        {
            PKRAssetTOCInfo ainfo;
            U32 numSubObjects, remainSubObjBits, currSubObjBits;
            RpAtomic* tempmodel;

            xSTGetAssetInfoByType('MODL', i, &ainfo);
            sPipeSceneAssetID = ainfo.aid;

            numSubObjects = 0;
            tempmodel = model;

            do
            {
                numSubObjects++;
                tempmodel = iModelFile_RWMultiAtomic(tempmodel);
            } while (tempmodel);

            remainSubObjBits = (1 << numSubObjects) - 1;

            for (j = 0; j < xModelPipeNumTables; j++)
            {
                for (k = 0; k < xModelPipeCount[j]; k++)
                {
                    if (ainfo.aid == xModelPipeData[j][k].ModelHashID ||
                        ainfo.aid == xStrHashCat(xModelPipeData[j][k].ModelHashID, ".dff"))
                    {
                        currSubObjBits = remainSubObjBits & xModelPipeData[j][k].SubObjectBits;

                        if (currSubObjBits)
                        {
                            pipeCB(model, xModelPipeData[j][k].Pipe, currSubObjBits);

                            if ((xModelPipeData[j][k].Pipe.Flags & 0x7) == 4 &&
                                iModelNumBones(model))
                            {
                                xModelPipeData[j][k].Pipe.Flags &=
                                    ~(xModelPipeData[j][k].Pipe.Flags & 0x4);
                                xModelPipeData[j][k].Pipe.Flags |= 0x2;
                            }
                        }

                        remainSubObjBits &= ~currSubObjBits;

                        // BfBB has the same goto; a break or loop condition adds a remainSubObjBits
                        // test.
                        if (!remainSubObjBits)
                        {
                            goto next_model;
                        }
                    }
                }
            }

            if (remainSubObjBits)
            {
                pipeCB(model, zeroPipe, remainSubObjBits);
            }
        }

    next_model:
        continue;
    }
}

static U32 BaseTypeNeedsUpdate(U8 baseType)
{
    if (baseType == eBaseTypeUnknown || baseType == eBaseTypeCamera ||
        baseType == eBaseTypeStatic || baseType == eBaseTypeMovePoint ||
        baseType == eBaseTypeBubble || baseType == eBaseTypePortal || baseType == eBaseTypeGroup ||
        baseType == eBaseTypeSFX || baseType == eBaseTypeFFX || baseType == eBaseTypeVFX ||
        baseType == eBaseTypeCounter || baseType == eBaseTypeProj ||
        baseType == eBaseTypeHUD_image || baseType == eBaseTypeVolume ||
        baseType == eBaseTypeDispatcher || baseType == eBaseTypeCond || baseType == eBaseTypeUI ||
        baseType == eBaseTypeProjectileType || baseType == eBaseTypeLobMaster ||
        baseType == eBaseTypeCutsceneMgr || baseType == eBaseTypePlayer ||
        baseType == eBaseTypePickup || baseType == eBaseTypeEnv || baseType == eBaseTypeNPC ||
        baseType == eBaseTypeHud || baseType == eBaseTypeNPCProps ||
        baseType == eBaseTypeParticleEmitterProps || baseType == eBaseTypeCruiseBubble ||
        baseType == eBaseTypeTextBox || baseType == eBaseTypeTalkBox ||
        baseType == eBaseTypeTaskBox || baseType == eBaseTypeBoulderGenerator ||
        baseType == eBaseTypeNPCSettings || baseType == eBaseTypeTurret)
    {
        return 0;
    }

    return 1;
}

void zSceneInit(U32 theSceneID, S32 reloadInProgress)
{
    F32 pdone;
    zScene* s;
    S32 i;
    char* pSceneId;

    xFontInitModelCache();
    zPlayerLoadHIP(theSceneID);

    if (theSceneID != SCENE_ID_MNU_START)
    {
        zSceneLoadPackFile(SCENE_ID_MNU_INGAME, 1, false, NULL);
    }

    gTransitionSceneID = theSceneID;
    gOccludeCount = 0;

    pSceneId = (char*)&theSceneID;
    pSceneId[0] = pSceneId[0] >= 'a' && pSceneId[0] <= 'z' ? pSceneId[0] - 32 : pSceneId[0];
    pSceneId[1] = pSceneId[1] >= 'a' && pSceneId[1] <= 'z' ? pSceneId[1] - 32 : pSceneId[1];
    pSceneId[2] = pSceneId[2] >= 'a' && pSceneId[2] <= 'z' ? pSceneId[2] - 32 : pSceneId[2];
    pSceneId[3] = pSceneId[3] >= 'a' && pSceneId[3] <= 'z' ? pSceneId[3] - 32 : pSceneId[3];

    xUtil_idtag2string(theSceneID, 0);
    xFogClearFog();

    if (!reloadInProgress)
    {
        sMemDepthSceneStart = xMemPushBase();
    }

    zGameScreenTransitionBegin();
    zParPTankInit();
    zRingControl::ringControlForScene = NULL;
    zNPCMgr_sceneInit();
    xModelBlur::scene_enter();

    pdone = 2.0f;

    if (!reloadInProgress)
    {
        zGameScreenTransitionUpdate(pdone, "... scene preload ...\n");
        zSceneLoadPackFile(theSceneID, 2, false, NULL);
    }

    sMemDepthJustHIPStart = xMemPushBase();
    s = globals.sceneCur = (zScene*)xMemAllocSize(sizeof(zScene));

    xSceneInit(s, 200, 2048, 2068, 384);

    s->env = (xEnv*)xMemAllocSize(sizeof(xEnv));
    s->sceneID = theSceneID;

    iTime time = iTimeGet();
    xUtil_idtag2string(theSceneID, 0);
    iTimeDiffSec(time);

    zSceneLoadPackFile(theSceneID, 1, false, NULL);

    pdone += 2.0f;
    zGameScreenTransitionUpdate(pdone, "...initializing scene - sound\n");

    s->parameters = NULL;

    S32 numDynAsset = xSTAssetCountByType('DYNA');

    for (i = 0; i < numDynAsset; i++)
    {
        xDynAsset* asset = (xDynAsset*)xSTFindAssetByType('DYNA', i, NULL);

        if (asset->type == 0xFABDB3B3)
        {
            s->parameters = (zSceneParameters*)asset;
            break;
        }
    }

    if (!s->parameters)
    {
        s->parameters = (zSceneParameters*)xMemAllocSize(sizeof(zSceneParameters));
        memset(s->parameters, 0, sizeof(zSceneParameters));
        s->parameters->bombCount = 20;
        s->parameters->extraIdleDelay = 5;
        s->parameters->hdrGlow = 10;
        s->parameters->hdrDarken = 80;
        s->parameters->uDefaultMusicHash = 0;
        s->parameters->flags = 0;
        s->parameters->lodFadeDistance = 4.0f;
    }

    // copy-at-load: zSceneParameters (a DYNA asset) has pointer fields, and its idle extras are
    // offsets to arrays of 4-byte asset IDs that are replaced by xAnimFile pointers in place. A host
    // with 8-byte pointers must widen the asset when it is loaded.
    U32 idleWrite = 0;

    s->parameters->idle03Extras =
        (xAnimFile**)((UPtr)s->parameters->idle03Extras + (UPtr)&s->parameters->idle03ExtraCount);

    for (U32 j = 0; j < s->parameters->idle03ExtraCount; j++)
    {
        iAnimFile* raw = (iAnimFile*)xSTFindAsset((U32)(UPtr)s->parameters->idle03Extras[j], NULL);

        if (raw)
        {
            s->parameters->idle03Extras[idleWrite++] = xAnimFileNew(raw, "", 0, NULL);
        }
    }

    s->parameters->idle03ExtraCount = idleWrite;

    idleWrite = 0;

    s->parameters->idle04Extras =
        (xAnimFile**)((UPtr)s->parameters->idle04Extras + (UPtr)&s->parameters->idle03ExtraCount);

    for (U32 j = 0; j < s->parameters->idle04ExtraCount; j++)
    {
        iAnimFile* raw = (iAnimFile*)xSTFindAsset((U32)(UPtr)s->parameters->idle04Extras[j], NULL);

        if (raw)
        {
            s->parameters->idle04Extras[idleWrite++] = xAnimFileNew(raw, "", 0, NULL);
        }
    }

    s->parameters->idle04ExtraCount = idleWrite;

    iSndEnableEffects(s->parameters->flags & 0x1);
    iSndInitSceneLoaded();
    xSndMgrSceneInit();
    xPadRumbleEnable(globals.currentActivePad, globals.option_vibration);
    zHud::hud_system::instance().init();
    xtextbox::clear_layout_cache();

    xSTSwitchScene(theSceneID, NULL, NULL);

    globals.sceneCur->resolvID = zSceneFindObject;
    globals.sceneCur->id2Name = zSceneGetName;
    globals.sceneCur->base2Name = zSceneGetName;

    g_xSceneCur = globals.sceneCur;

    xModelPipeNumTables = xSTAssetCountByType('PIPT');

    for (S32 i = 0; i < xModelPipeNumTables; i++)
    {
        void* data = xSTFindAssetByType('PIPT', i, NULL);

        xModelPipeCount[i] = ((xModelPipeAssetHeader*)data)->count;
        xModelPipeData[i] = ((xModelPipeAssetHeader*)data)->info;
    }

    xModelLookupCount = 0;

    PipeForAllSceneModels(PipeCountStuffCB);

    if (xModelLookupCount)
    {
        xModelLookupList =
            (xModelPipeLookup*)xMemAllocSize(xModelLookupCount * sizeof(xModelPipeLookup));

        xModelLookupCount = 0;

        PipeForAllSceneModels(PipeAddStuffCB);
    }

    xModelBucket_PreCountReset();
    PipeForAllSceneModels(xModelBucket_PreCountBucket);

    xModelBucket_PreCountAlloc(512);
    PipeForAllSceneModels(xModelBucket_InsertBucket);

    xModelBucket_Init();
    xRenderSceneEnter();
    xScrFX_PulseGlaresReset();
    xFXHighDynamicRangeSceneEnter();
    xPTankPoolSceneEnter();
    xParticleBatchSceneEnter();
    zParticleCustomSceneEnter();
    zParPTankSceneEnter();
    xhud::widget::scene_enter();
    xDecal::scene_enter();
    xDecalInit();
    xScreenWarp::scene_enter();
    xShadowSimple_Reset();
    zDispatcher_scenePrepare();
    zAnimListInit();
    zGooInit(24);

    zGameScreenTransitionUpdate(100.0f, "...initializing scene - base types\n");

    for (i = 0; i < eBaseTypeCount; i++)
    {
        s->baseCount[i] = 0;
        s->baseList[i] = NULL;
    }

    zCollGeom_Init();
    ztextbox::init();
    ztalkbox::init();
    ztaskbox::init();
    z::symbol::init();
    zDustSceneEnter();
    xCounterInit();
    zSurfaceInit();
    xtextbox::register_tags(&RaceTimerTag, 1);
    xCounterStartup();
    z_disco_floor::init();
    zJSPExtraData::ClearGroupAssociations();
    zEntPickupSceneInit(theSceneID);

    zStreamedSound::sDelayedSound = NULL;
    zStreamedSound::sLastPlayedSoundHdl = (iSndHandle)-1;

    xDrawTextInit(0, 0);

    xModelInstStaticAlloc = 1;
    s->num_base = 0;

    for (i = 0; sInitTable[i].name; i++)
    {
        S32 typeCount = xSTAssetCountByType(sInitTable[i].assetType);

        s->baseCount[sInitTable[i].baseType] = typeCount;
        s->num_base += typeCount;

        if (sInitTable[i].querySubObjects)
        {
            for (S32 j = 0; j < typeCount; j++)
            {
                s->num_base += sInitTable[i].querySubObjects(
                    xSTFindAssetByType(sInitTable[i].assetType, j, NULL));
            }
        }
    }

    add_dynamic_types(*s);

    z::lightweight_manager::setup();

    S32 total_npcs = zNMEAsset_CountEnemies(NULL, NULL);

    s->baseCount[eBaseTypeNPC] = total_npcs;
    s->num_base += total_npcs;

    zNPCMgr_scenePrepare(total_npcs);
    zNMECentral_ScenePrepare();
    xAnimTempTransitionInit(total_npcs * 2 + 16);
    xAnimTempStateInit(total_npcs * 2 + 16);

    if (s->num_base)
    {
        s->base = (xBase**)xMemAllocSize(s->num_base * sizeof(xBase*));
    }
    else
    {
        s->base = NULL;
    }

    zSceneObjHashtableInit(8192);

    GG02::specialsExist = 0;
    GG02::rast_goofyShadow = NULL;

    zParticleSceneEnter();
    xFFXPoolInit(12);
    xFFXShakePoolInit(3);
    xFFXRotMatchPoolInit(1);
    xEntSceneInit();
    zLightSetVolume(NULL);
    xPartitionReset();
    xFXRibbon::scene_enter();
    zCamMarkerSceneEnter();

    S32 base_idx = 0;

    for (i = 0; sInitTable[i].name; i++)
    {
        if (sInitTable[i].func)
        {
            base_idx = sInitTable[i].func(s, &sInitTable[i], base_idx);
        }
    }

    base_idx = init_dynamic_types(*s, base_idx);

    s->baseList[eBaseTypeNPC] = NULL;

    zNMEAsset_CreateEnemies();
    zNMEAsset_RegisterEnemies(s, &base_idx);

    s->num_update_base = 0;

    for (i = 0; i < s->num_base; i++)
    {
        if (s->base[i] && BaseTypeNeedsUpdate(s->base[i]->baseType))
        {
            s->num_update_base++;
        }
    }

    s->update_base = (xBase**)xMemAllocSize(s->num_update_base * sizeof(xBase*));

    S32 curidx = 0;

    for (i = 0; i < s->num_base; i++)
    {
        if (s->base[i] && BaseTypeNeedsUpdate(s->base[i]->baseType))
        {
            s->update_base[curidx++] = s->base[i];
        }
    }

    xModelInstStaticAlloc = 0;

    zGameScreenTransitionEnd();
    zLightResolveLinks();
    zRenderStateInit();
    xFXStreakInit();
    xFXShineInit();
    xFXFireworksInit("GG_STARS_EMIT", "GG_STARS_EMIT", "GG_STARS_EMIT", "sound_firework_explode",
                     "sound_firework_trail");
    zLightningInit();
    zParCmdFindClipVolumes();
    zShrapnel_SceneInit(globals.sceneCur);
    zFXRibbonPoolInit(64);
    xScrFxLensFlare_SceneEnter();
    zNMECentral_SceneInit();
    zThrown_Reset();
    zCameraTweakGlobal_Reset();
    zActionLineInit();
    xScrFxLetterboxReset();

    S32 shadForEnemies = zNMECommon_MaxShadowNeededLater();
    xShadowManager_Init(shadForEnemies + 2, 256);

    xQuickCullInit(&xqc_def_ctrl, xEntGetAllEntsBox());
    zGridInit(s);
    ZNME_Tiki_Startup();

    zTransient::scene_enter();
    xLightVolume::scene_enter();
    zSmokeSceneEnter();
    zSplashSceneEnter();
    zFlameEmitter::scene_enter();

    S32 lkitCount = xSTAssetCountByType('LKIT');
    void* lkitData;

    for (S32 i = 0; i < lkitCount; i++)
    {
        lkitData = xSTFindAssetByType('LKIT', i, NULL);

        xLightKit_Prepare(lkitData);
    }

    xFXHighDynamicRangeSetBackgroundDefault(s->parameters->hdrGlow, s->parameters->hdrDarken, true);
    xClimateInit(&gClimate);

    for (i = 0; i < s->baseCount[eBaseTypePlatform]; i++)
    {
        // Retail always resets the first platform's destructible, never indexing by i.
        zPlatform* pobj = (zPlatform*)s->baseList[eBaseTypePlatform];

        if (pobj->destructible)
        {
            zDestructibleAsset_ReReset(pobj->destructible, pobj);
        }
    }

    switch (globals.sceneCur->sceneID)
    {
    case 'BB01':
    case 'GG02':
    case 'PT02':
    case 'SC02':
    case 'TT01':
    case 'TT02':
        globals.minVSyncCnt = 2;
        break;
    default:
        globals.minVSyncCnt = 1;
        break;
    }

    globals.sceneCur->enableDrawing = TRUE;

    iSndSuspendCD(0);
}

void zSceneExit(S32 beginReload)
{
    zScene* s;
    RpWorld* world;
    // Not in DWARF order (it lists i after ps); declaring it there changes the generated code.
    S32 i;

#ifdef PLATFORM_PC
    // The scene's HIP (the credits' CRDT among them) is released below, on a reload as well;
    // credits still running must not outlive it.
    xCMSceneExit();
    // Online co-op (docs/MULTIPLAYER.md 5.2): the level's table of shared
    // things goes with it. Nothing when co-op is off.
    iCoopWorldSceneExit();
#endif

    if (globals.cmgr)
    {
        zCutsceneMgrFinishExit(globals.cmgr);
    }

    xLightKit_Enable(NULL, xglobals->currWorld);

    s = globals.sceneCur;

    zGridExit(s);
    zSceneForAllBase(zSceneExitSoundIteratorCB, NULL);
    xRumble::Manager::Get().Reset();
    z::lightweight_manager::exit();
    xSndMgrSceneExit();
    iSndSuspendCD(1);
    iFuncProfileDump();

    world = s->env->geom->world;

    xModel_SceneExit(world);
    zFX_SceneExit(world);
    zFXRibbonPool_SceneReset();
    xFX_SceneShutdown();
    xFXHighDynamicRangeSceneExit();
    zEntEventAll(NULL, 0, eEventRoomEnd, NULL);
    zEntEventAll(NULL, 0, eEventSceneEnd, NULL);

    if (globals.sceneCur->pendingPortal)
    {
        char nextScene[8];
        char curScene[8];

        strcpy(nextScene, xUtil_idtag2string(globals.sceneCur->pendingPortal->passet->sceneID, 0));
        strcpy(curScene, xUtil_idtag2string(globals.sceneCur->sceneID, 0));
    }

    gOccludeCount = 0;

    if (globals.sceneCur->sceneID != SCENE_ID_MNU_START)
    {
        zSceneSave(s);
    }

    zSurfaceExit();
    zLightDestroyAll();
    xEntSceneExit();
    zUIMgrExit(s);
    zSceneObjHashtableExit();

    if (s->baseCount[eBaseTypeParticleSystem])
    {
        zParSys* ps = (zParSys*)s->baseList[eBaseTypeParticleSystem];

        for (i = 0; i < s->baseCount[eBaseTypeParticleSystem]; i++)
        {
            if (xBaseIsValid(ps))
            {
                xParSysExit(ps);
            }

            ps++;
        }
    }

    xParEmitterDestroy();
    xModelBucket_Deinit();
    zHud::hud_system::instance().destroy();
    zParPTankExit();
    zAnimListExit();
    zGooExit();
    xAntennaRemoveAll();
    zThrown_Reset();
    zNMECentral_SceneFinish();
    cruise_bubble::deinit();
    bungee_state::deinit();
    oob_state::reset();
    zEntPlayer_SceneExit();
    zNPCMgr_sceneFinish();
    zDispatcher_sceneFinish();
    zFlameEmitter::scene_exit();
    zSpotlight::scene_exit();
    xModelBlur::scene_exit();
    zSplashSceneExit();
    zSmokeSceneExit();
    xLightVolume::scene_exit();
    zFire::scene_exit();
    zExplosion::scene_exit();
    xTransient::scene_exit();
    xDecal::scene_exit();
    xScreenWarp::scene_exit();
    xScrFxLensFlare_SceneExit();
    zParticleSceneExit();
    xParticleBatchSceneExit();
    xPTankPoolSceneExit();
    xRenderSceneExit();

    if (beginReload)
    {
        xSTUnLoadSceneMany(globals.sceneCur->sceneID, 5);
        xMemPopBase(sMemDepthJustHIPStart);
        xSTUnLoadSceneMany(SCENE_ID_MNU_INGAME, 5);

        sMemDepthJustHIPStart = -1;
    }
    else
    {
        xSTUnLoadSceneMany(globals.sceneCur->sceneID, 7);
        globals.screen = NULL;
        xMemPopBase(sMemDepthSceneStart);
        xSTUnLoadSceneMany(SCENE_ID_MNU_INGAME, 5);

        sMemDepthSceneStart = -1;
        sMemDepthJustHIPStart = -1;
    }

    zPlayerUnloadHIP();

    xSndMgrSceneExit();
    xUtil_idtag2string(globals.sceneCur->sceneID, 0);

    static S32 scenesCompleted = 1;

    globals.sceneCur = NULL;
    scenesCompleted++;

    xSceneExit(s);

    zEntPlayerFX::CartShield::Reset();
    zEntPlayerFX::HammerThrow::Reset();
    zEntPlayerFX::SuperMachoBowlLightning::RemoveAll();
}

void zSceneUpdateSFXWidgets()
{
    zScene* s = globals.sceneCur;
    xSoundFXUpdateEnvironmentalStreamSounds((xSoundFX*)s->baseList[eBaseTypeSoundFX],
                                            s->baseCount[eBaseTypeSoundFX]);
}

void zSceneSwitch(_zPortal* p, S32 forceSameScene)
{
    globals.sceneCur->pendingPortal = p;

    xPortalAsset* passet = globals.sceneCur->pendingPortal->passet;
    U32 nextUnlockedSceneID = zDispatcher_FindNextValidWorld(passet->sceneID);

    if (nextUnlockedSceneID != passet->sceneID)
    {
        passet->assetMarkerID = zDispatcher_FindTaskPointer(nextUnlockedSceneID, 1);
        passet->sceneID = nextUnlockedSceneID;
    }

    gLevelChanged = zSceneLeavingLevel();

    U32 nextSceneID = passet->sceneID;

    if (!forceSameScene && nextSceneID == globals.sceneCur->sceneID)
    {
        U32 PlayerMarkerStartID = passet->assetMarkerID;
        U32 PlayerMarkerStartCamID = passet->assetCameraID;
        F32 PlayerStartAngle = passet->ang;

        globals.player.checkpoint.initCamID = PlayerMarkerStartCamID;

        if (PlayerStartAngle >= 1000.0f)
        {
            pointer_asset* pointer = (pointer_asset*)xSTFindAsset(PlayerMarkerStartID, NULL);

            if (pointer != NULL && zSceneIsPointerAsset(pointer))
            {
                xglobals->___player_ent_dont_use_directly->frame->mat.pos = pointer->loc;
            }
            else
            {
                xMarkerAsset* marker = (xMarkerAsset*)xSTFindAsset(PlayerMarkerStartID, NULL);

                if (marker != NULL)
                {
                    xEnt& player = *xglobals->___player_ent_dont_use_directly;
                    xVec3& loc = (xVec3&)player.model->Mat->pos;

                    loc = player.frame->mat.pos = marker->pos;
                }
            }
        }
        else
        {
            pointer_asset* pointer = (pointer_asset*)xSTFindAsset(PlayerMarkerStartID, NULL);

            if (pointer != NULL && zSceneIsPointerAsset(pointer))
            {
                xEntFrame* frame = xglobals->___player_ent_dont_use_directly->frame;

                frame->mat.pos = pointer->loc;
                frame->rot.angle = (PI / 180.0f) * pointer->yaw;
                frame->rot.axis = xVec3::m_UnitAxisY;
                xMat3x3Euler(&xglobals->___player_ent_dont_use_directly->frame->mat,
                             frame->rot.angle, 0.0f, 0.0f);
                *(xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat = frame->mat;
                zCamReset(frame->mat);
                xglobals->___player_ent_dont_use_directly->SetCamera();
            }
            else
            {
                U32 size;
                xMarkerAsset* m = (xMarkerAsset*)xSTFindAsset(PlayerMarkerStartID, &size);

                if (m != NULL)
                {
                    xVec3Copy(&xglobals->___player_ent_dont_use_directly->frame->mat.pos, &m->pos);
                    xVec3Copy((xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos,
                              &m->pos);
                    zCamReset(*(xMat4x3*)xglobals->___player_ent_dont_use_directly->model->Mat);
                }

                xEntFrame* frame = xglobals->___player_ent_dont_use_directly->frame;

                frame->rot.angle = (PI / 180.0f) * PlayerStartAngle;
                frame->rot.axis = xVec3::m_UnitAxisY;
                xMat3x3Euler(&frame->mat, frame->rot.angle, 0.0f, 0.0f);
                *(xMat3x3*)xglobals->___player_ent_dont_use_directly->model->Mat = frame->mat;
                zCamReset(frame->mat);
                xglobals->___player_ent_dont_use_directly->SetCamera();
            }
        }

        globals.sceneCur->pendingPortal = NULL;
    }
}

void zSceneSave(zScene* ent)
{
    xSerial xser;
    xSerial* s = &xser;
    S32 i;

    s->setClient('PLYR');
    s->Write_b1(1);
    s->Write(globals.player.InvManlinessPoints);
    s->Write(globals.player.InvGoofyGoober);
    s->Write(globals.player.InvTreasureChestTotal);
    s->Write(globals.player.InvTreasureChestOpened);
    s->Write(globals.player.CurrentSpentSkillPoints);

    for (i = 0; i < 20; i++)
    {
        s->Write(globals.player.InvTreasureChest[i]);
        s->Write(globals.player.InvLevelPickups[i]);
    }

    for (i = 0; i < 10; i++)
    {
        s->Write(globals.player.g.PowerUp[i]);
    }

    zCutsceneMgrSave(NULL, s);

    s->setClient('CNTR');
    s->Write_b1(1);

    zMenuSB_SaveMenuCounters(s);
    zGameExtras_Save(s);

#ifdef PLATFORM_PC
    // Online co-op (docs/MULTIPLAYER.md 13): a guest's level that the host's
    // world changed keeps the block this game's save had for it. FALSE at
    // once when co-op is off.
    if (iCoopKeepSceneSave(ent->sceneID))
    {
        return;
    }
#endif

    s->setClient(ent->sceneID);
    s->Write_b1(1);

    xSceneSave(ent, s);

    s->Write(offsetx);
    s->Write(offsety);

    for (S32 i = 0; i < globals.sceneCur->num_base; i++)
    {
        xBase* b = globals.sceneCur->base[i];

        if (b && (b->baseFlags & 0x2))
        {
            switch (b->baseType)
            {
            case eBaseTypeTrigger:
                zEntTriggerSave((zEntTrigger*)b, s);
                break;
            case eBaseTypePickup:
                zEntPickup_Save((zEntPickup*)b, s);
                break;
            case eBaseTypeEnv:
                zEnvSave((_zEnv*)b, s);
                break;
            case eBaseTypeFog:
                xFogSave((_xFog*)b, s);
                break;
            case eBaseTypeLight:
                zLightSave((_zLight*)b, s);
                break;
            case eBaseTypePlatform:
                zEntSave((zEnt*)b, s);
                break;
            case eBaseTypeCamera:
                zCamMarkerSave((zCamMarker*)b, s);
                break;
            case eBaseTypeStatic:
                zEntSimpleObj_Save((zEntSimpleObj*)b, s);
                break;
            case eBaseTypeMovePoint:
                zMovePointSave((zMovePoint*)b, s);
                break;
            case eBaseTypeTimer:
                xTimerSave((xTimer*)b, s);
                break;
            case eBaseTypePortal:
                xBaseSave(b, s);
                break;
            case eBaseTypeGroup:
                xGroupSave((xGroup*)b, s);
                break;
            case eBaseTypePendulum:
                zEntSave((zEnt*)b, s);
                break;
            case eBaseTypeCounter:
                xCounterSave((_xCounter*)b, s);
                break;
            case eBaseTypeHangable:
                zEntHangable_Save((zEntHangable*)b, s);
                break;
            case eBaseTypeButton:
                zEntButton_Save((_zEntButton*)b, s);
                break;
            case eBaseTypeSurface:
                xSurfaceSave((xSurface*)b, s);
                break;
            case eBaseTypeScript:
                xBaseSave(b, s);
                break;
            case eBaseTypeVolume:
                ((xVolume*)b)->Save(s);
                break;
            case eBaseTypeDispatcher:
                zDispatcher_Save((st_ZDISPATCH_DATA*)b, s);
                break;
            case eBaseTypeCond:
                zConditionalSave((_zConditional*)b, s);
                break;
            case eBaseTypeEGenerator:
                zEGenerator_Save((zEGenerator*)b, s);
                break;
            case eBaseTypeTaskBox:
                ((ztaskbox*)b)->write(*s);
                break;
            case eBaseTypeTeleportBox:
                zEntTeleportBox_Save((_zEntTeleportBox*)b, s);
                break;
            case eBaseTypeTaxi:
                xBaseSave(b, s);
                break;
            case eBaseTypeRaceTimer:
                s->Write(((zRaceTimer*)b)->bestTime);
                break;
            case eBaseTypeCameraFly:
                zCameraFly_Save((zCameraFly*)b, s);
                break;
            case eBaseTypeCameraTweak:
                zCameraTweak_Save((zCameraTweak*)b, s);
                break;
            }
        }
    }
}

// No GC or PS2 symbol; most likely an inline, as the PS2 build has the save, the call and the
// restore on one line. The name is ours.
static inline void zSceneLoadManlinessPoints(U32 points)
{
    U32 multiplier = globals.player.InvManlinessPointsMultiplier;
    globals.player.InvManlinessPointsMultiplier = 1;
    globals.player.rewardsMgr->IncManlinessPoints(points, true);
    globals.player.InvManlinessPointsMultiplier = multiplier;
}

// No GC or PS2 symbol; most likely an inline, as the PS2 build has the whole case on one line.
// The name is ours.
static inline void zSceneLoadRaceTimer(zRaceTimer* timer, xSerial* s)
{
    s->Read(&timer->bestTime);
    timer->startTime =
        (timer->raceTimerAsset->startTime < 0) ? timer->bestTime : timer->raceTimerAsset->startTime;
    timer->victoryTime = (timer->raceTimerAsset->victoryTime < 0) ?
                             timer->bestTime :
                             timer->raceTimerAsset->victoryTime;
    timer->totalTime =
        ((timer->raceTimerAsset->countDown & 1) ? 1 : -1) * (timer->startTime - timer->victoryTime);
}

void zSceneLoad(zScene* ent)
{
    xSerial xser;
    xSerial* s = &xser;
    S32 sceneExist;
    S32 i;

    s->setClient('PLYR');

    sceneExist = 0;
    s->Read_b1(&sceneExist);

    if (sceneExist)
    {
        s->Read(&globals.player.InvManlinessPoints);
        s->Read(&globals.player.InvGoofyGoober);
        s->Read(&globals.player.InvTreasureChestTotal);
        s->Read(&globals.player.InvTreasureChestOpened);
        s->Read(&globals.player.CurrentSpentSkillPoints);

        for (i = 0; i < 20; i++)
        {
            s->Read(&globals.player.InvTreasureChest[i]);
            s->Read(&globals.player.InvLevelPickups[i]);
        }

        for (i = 0; i < 10; i++)
        {
            s->Read(&globals.player.g.PowerUp[i]);
        }

        globals.player.sb.MaxHealth = globals.player.g.PowerUp[8] + 3;
        globals.player.patrick.MaxHealth = globals.player.g.PowerUp[9] + 3;

        zEntPlayerHealthResetAll();

        zSceneLoadManlinessPoints(globals.player.InvManlinessPoints);

        zCutsceneMgrLoad(NULL, s);
    }

    s->setClient('CNTR');

    sceneExist = 0;
    s->Read_b1(&sceneExist);

    if (sceneExist)
    {
        zMenuSB_LoadMenuCounters(s);
        zGameExtras_Load(s);
    }
    else
    {
        zMenuSB_LoadTaskDefaults();
    }

    s->setClient(ent->sceneID);

    sceneExist = 0;
    s->Read_b1(&sceneExist);

    if (sceneExist)
    {
        xSceneLoad(ent, s);

        s->Read(&offsetx);
        s->Read(&offsety);

        for (U16 i = 0; i < globals.sceneCur->num_base; i++)
        {
            xBase* b = globals.sceneCur->base[i];

            if (b->baseFlags & 0x2)
            {
                switch (b->baseType)
                {
                case eBaseTypeTrigger:
                    zEntTriggerLoad((zEntTrigger*)b, s);
                    break;
                case eBaseTypePickup:
                    zEntPickup_Load((zEntPickup*)b, s);
                    break;
                case eBaseTypeEnv:
                    zEnvLoad((_zEnv*)b, s);
                    break;
                case eBaseTypeFog:
                    xFogLoad((_xFog*)b, s);
                    break;
                case eBaseTypeLight:
                    zLightLoad((_zLight*)b, s);
                    break;
                case eBaseTypePlatform:
                    zEntLoad((zEnt*)b, s);
                    break;
                case eBaseTypeCamera:
                    zCamMarkerLoad((zCamMarker*)b, s);
                    break;
                case eBaseTypeStatic:
                    zEntSimpleObj_Load((zEntSimpleObj*)b, s);
                    break;
                case eBaseTypeMovePoint:
                    zMovePointLoad((zMovePoint*)b, s);
                    break;
                case eBaseTypeTimer:
                    xTimerLoad((xTimer*)b, s);
                    break;
                case eBaseTypePortal:
                    xBaseLoad(b, s);
                    break;
                case eBaseTypeGroup:
                    xGroupLoad((xGroup*)b, s);
                    break;
                case eBaseTypePendulum:
                    zEntLoad((zEnt*)b, s);
                    break;
                case eBaseTypeCounter:
                    xCounterLoad((_xCounter*)b, s);
                    break;
                case eBaseTypeHangable:
                    zEntHangable_Load((zEntHangable*)b, s);
                    break;
                case eBaseTypeButton:
                    zEntButton_Load((_zEntButton*)b, s);
                    break;
                case eBaseTypeSurface:
                    xSurfaceLoad((xSurface*)b, s);
                    break;
                case eBaseTypeScript:
                    xBaseLoad(b, s);
                    break;
                case eBaseTypeVolume:
                    ((xVolume*)b)->Load(s);
                    break;
                case eBaseTypeDispatcher:
                    zDispatcher_Load((st_ZDISPATCH_DATA*)b, s);
                    break;
                case eBaseTypeCond:
                    zConditionalLoad((_zConditional*)b, s);
                    break;
                case eBaseTypeEGenerator:
                    zEGenerator_Load((zEGenerator*)b, s);
                    break;
                case eBaseTypeTaskBox:
                    ((ztaskbox*)b)->read(*s);
                    break;
                case eBaseTypeTeleportBox:
                    zEntTeleportBox_Load((_zEntTeleportBox*)b, s);
                    break;
                case eBaseTypeTaxi:
                    xBaseLoad(b, s);
                    break;
                case eBaseTypeRaceTimer:
                    zSceneLoadRaceTimer((zRaceTimer*)b, s);
                    break;
                case eBaseTypeCameraFly:
                    zCameraFly_Load((zCameraFly*)b, s);
                    break;
                case eBaseTypeCameraTweak:
                    zCameraTweak_Load((zCameraTweak*)b, s);
                    break;
                }
            }
        }
    }
}

// No GC or PS2 symbol (inline; zSceneSetup runs the same loop as zSceneReset without clearing
// playerDead). The name is ours.
static inline void zSceneFindEnemyManlinessPoints()
{
    S32 i, j;
    char name[64];
    zRewardsMgr* mgr = globals.player.rewardsMgr;

    for (i = 0; i < 5; i++)
    {
        for (j = 0; j < 20; j++)
        {
            sprintf(name, "ENEMY_MANLINESS_POINT%02d", j + 1 + i * 20);

            mgr->enemyManlinessPoints[i][j] = (zEntPickup*)zSceneFindObject(xStrHash(name));

            if (mgr->enemyManlinessPoints[i][j])
            {
                mgr->enemyManlinessPoints[i][j]->state = 0x88;
                xEntHide(mgr->enemyManlinessPoints[i][j]);
            }
        }
    }
}

// No GC or PS2 symbol (inline; one line of zSceneReset in the PS2 build). The name is ours.
static inline void zSceneInitEnemyManlinessPoints()
{
    S32 i, j;
    char name[64];
    zRewardsMgr* mgr = globals.player.rewardsMgr;

    for (i = 0; i < 5; i++)
    {
        for (j = 0; j < 20; j++)
        {
            sprintf(name, "ENEMY_MANLINESS_POINT%02d", j + 1 + i * 20);

            mgr->enemyManlinessPoints[i][j] = (zEntPickup*)zSceneFindObject(xStrHash(name));

            if (mgr->enemyManlinessPoints[i][j])
            {
                mgr->enemyManlinessPoints[i][j]->state = 0x88;
                xEntHide(mgr->enemyManlinessPoints[i][j]);
            }
        }
    }

    mgr->playerDead = FALSE;
}

// No GC or PS2 symbol: inlined into zSceneReset and dead-stripped. Name from BfBB, whose
// zEntPlayer_LoadCheckPoint also restores the frame and then fixes up the camera and sounds.
void zEntPlayer_LoadCheckPoint()
{
    xEnt* p = xglobals->___player_ent_dont_use_directly;
    xEntFrame* f = p->frame;
    xModelInstance* m = p->model;

    f->mat.pos = globals.player.checkpoint.pos;
    f->rot.angle = globals.player.checkpoint.rot;
    f->rot.axis = xVec3::m_UnitAxisY;

    xMat3x3Euler(&f->mat, f->rot.angle, 0.0f, 0.0f);
    *(xMat4x3*)m->Mat = f->mat;
    f->oldmat = f->mat;

    if (xSndMgrGetEffect() != globals.player.checkpoint.currentEffect)
    {
        xSndMgrSetEffect(globals.player.checkpoint.currentEffect);
    }

    xglobals->___player_ent_dont_use_directly->LoadCheckPoint();
    xglobals->___player_ent_dont_use_directly->SetCamera();
}

void zSceneReset()
{
    zScene* s = globals.sceneCur;

    gOccludeCount = 0;

    globals.player.ControlOff = 0;

    xFXHighDynamicRangeReset();
    xScrFX_PulseGlaresReset();
    xScreenWarp::reset();
    xDecal::reset();
    xScrFxLetterboxReset();
    zParticleReset();
    xParticleBatchReset();
    zFXRibbonPool_SceneReset();
    zFX_SceneReset();
    zMusicKill();
    xModelBlur::reset_all();
    zThrown_ResetAll();
    zShrapnel_Reset();
    ztalkbox::reset_all();
    z::lightweight_manager::reset();
    zCameraTweakGlobal_Reset();
    zCamMarkerReset();
    xTransient::reset();
    zFire::reset();
    zFlameEmitter::fire_reset();

    globals.cmgr = NULL;

    zEntEventAll(NULL, 0, eEventRoomEnd, NULL);
    zEntEventAll(NULL, 0, eEventSceneEnd, NULL);

    zSceneSave(s);

    xSndMgrStopSounds(-3, false);
    xSndMgrUpdate();

    xRumble::Manager::Get().Reset();

    for (S32 i = 0; i < s->num_base; i++)
    {
        if (s->base[i])
        {
            switch (s->base[i]->baseType)
            {
            case eBaseTypePickup:
            case eBaseTypeCamera:
            case eBaseTypeDoor:
            case eBaseTypeSavePoint:
            case eBaseTypeItem:
            case eBaseTypeStatic:
            case eBaseTypeDynamic:
            case eBaseTypeMovePoint:
            case eBaseTypeTimer:
            case eBaseTypeBubble:
            case eBaseTypePortal:
            case eBaseTypeSFX:
            case eBaseTypeFFX:
            case eBaseTypeVFX:
            case eBaseTypeButton:
            case eBaseTypeProj:
            case eBaseTypeSurface:
            case eBaseTypeDestructObj:
            case eBaseTypeHUD_image:
            case eBaseTypeVolume:
            case eBaseTypeDispatcher:
            case eBaseTypeCond:
            case eBaseTypeUI:
            case eBaseTypeUIFont:
            case eBaseTypeProjectileType:
            case eBaseTypeFog:
            case eBaseTypeLight:
            case eBaseTypeParticleEmitter:
            case eBaseTypeParticleSystem:
            case eBaseTypeCutsceneMgr:
            default:
            {
                zEntEvent(NULL, 0, s->base[i], eEventReset, NULL, NULL, 0, FE_YES);
                break;
            }
            case eBaseTypeGroup:
            {
                break;
            }
            case eBaseTypeCounter:
            {
                xCounterReset(s->base[i]);
                break;
            }
            case eBaseTypePlayer:
            {
                ((zPlayer*)s->base[i])->Reset();
                break;
            }
            case eBaseTypePlatform:
            {
                zPlatform_Reset((zPlatform*)s->base[i], globals.sceneCur);
                break;
            }
            case eBaseTypePendulum:
            {
                zPendulum_Reset((_zPendulum*)s->base[i], globals.sceneCur);
                break;
            }
            case eBaseTypeHangable:
            {
                zEntHangable_Reset((zEntHangable*)s->base[i]);
                break;
            }
            case eBaseTypeLobMaster:
            {
                zEntEvent(s->base[i], eEventReset);
                break;
            }
            case eBaseTypeEGenerator:
            {
                zEGenerator_Reset((zEGenerator*)s->base[i], globals.sceneCur);
                break;
            }
            case eBaseTypeEnv:
            {
                _zEnv* env = (_zEnv*)s->base[i];

                if (env)
                {
                    xClimateInitAsset(&gClimate, env->easset);
                }
            }
            }
        }
    }

    zSceneResetLightKits();
    zReactiveReset();
    zSpotlight::reset_all();
    zDustReset();
    xLightVolume::reset_all();
    zLightningReset();
    zNMECentral_SceneReset();
    xAntennaSceneReset();

    zStreamedSound::sDelayedSound = NULL;
    zStreamedSound::sLastPlayedSoundHdl = (iSndHandle)-1;

    xEnvSceneReset(globals.sceneCur->env);
    zMenuSB_UpdatePowerUpMenu();
    zNPCMgr_sceneReset();
    zSceneLoad(globals.sceneCur);
    zSceneEnableVisited(s);
    xSerialTraverse(zSceneSetup_serialTraverseCB);
    xUpdateCull_Reset(globals.updateMgr);
    xUpdateCull_Update(globals.updateMgr, 100);
    zGridReset(s);

    zEntEventAll(NULL, 0, eEventSceneBegin, NULL);
    zEntEventAll(NULL, 0, eEventRoomBegin, NULL);
    zEntEventAll(NULL, 0, eEventSceneReset, NULL);

    xUpdateCull_Update(globals.updateMgr, 100);
    zLOD_Update(100);

    zGameExtras_SceneReset();

    zSceneInitEnemyManlinessPoints();

    if (!oob_state::enabled())
    {
        zEntPlayer_LoadCheckPoint();
    }

    zEntPickup_UpdateReset();

#ifdef PLATFORM_PC
    // Online co-op (docs/MULTIPLAYER.md 13): what the reset undid of a level
    // end still due. Nothing when co-op is off.
    iCoopWorldSceneReset();
#endif
}

void zSceneStoreCheckPoint(U32 initialCameraID)
{
    zSceneStoreCheckPoint(xglobals->___player_ent_dont_use_directly->frame->mat.pos,
                          xatan2(xglobals->___player_ent_dont_use_directly->frame->mat.at.x,
                                 xglobals->___player_ent_dont_use_directly->frame->mat.at.z),
                          initialCameraID);
}

void zSceneStoreCheckPoint(const xVec3& pos, F32 rot, U32 initialCameraID)
{
    globals.player.checkpoint.pos = pos;
    globals.player.checkpoint.rot = rot;
    globals.player.checkpoint.initCamID = initialCameraID;

    if (!globals.player.checkpoint.jsp_active)
    {
        globals.player.checkpoint.jsp_active =
            (U8*)xMemAllocSize(globals.sceneCur->env->geom->jsp_count);
    }

    globals.player.checkpoint.currentEffect = xSndMgrGetEffect();

    xglobals->___player_ent_dont_use_directly->StoreCheckPoint();
    xEntStoreCheckPoint();
}

void zSceneDiskSpin(F32)
{
}

static void ActivateCB(xBase* base)
{
    base->baseFlags &= (U8)~0x40;
}

static void DeactivateCB(xBase* base)
{
    base->baseFlags |= 0x40;
}

void zSceneSetup()
{
    zScene* s = globals.sceneCur;

    globals.cmgr = NULL;

    xSceneSetup(s);

    xModelInstStaticAlloc = 1;

    zUIMgrInit(s);

    s->gravity = -50.0f;
    s->drag = 0.08f;
    s->flags = 0x5;

    zNMECentral_ScenePostInit();
    zNMEShrapnelScenePostInit();
    zNMERumbleScenePostInit();
    xAntennaScenePostInit();
    zNPCMgr_scenePostInit();

    if (s->baseCount[eBaseTypeVolume])
    {
        zVolumeSetup();
    }

    {
        U32 dontcaresize;
        xCutscene_Init(xSTFindAssetByType('CTOC', 0, &dontcaresize));
    }

    gCurEnv = NULL;

    {
        S32 i;
        xBase* b;

        for (i = 0; i < s->num_base; i++)
        {
            b = s->base[i];

            if (b)
            {
                switch (b->baseType)
                {
                case eBaseTypeEnv:
                {
                    gCurEnv = (_zEnv*)b;

                    zEnvSetup(gCurEnv);
                    xClimateInitAsset(&gClimate, gCurEnv->easset);

                    break;
                }
                case eBaseTypePlatform:
                {
                    zPlatform_Reset((zPlatform*)b, s);
                    zPlatform_Setup((zPlatform*)b, s);
                    break;
                }
                case eBaseTypeMovePoint:
                {
                    zMovePointSetup((zMovePoint*)b, s);
                    break;
                }
                case eBaseTypePendulum:
                {
                    zPendulum_Reset((_zPendulum*)b, s);
                    zPendulum_Setup((_zPendulum*)b, s);
                    break;
                }
                case eBaseTypeButton:
                {
                    zEntButton_Reset((_zEntButton*)b, s);
                    zEntButton_Setup((_zEntButton*)b, s);
                    break;
                }
                case eBaseTypeStatic:
                {
                    zEntSimpleObj_Setup((zEntSimpleObj*)b);
                    break;
                }
                case eBaseTypeTrackPhysics:
                {
                    zEntSimpleObj_Setup((zEntSimpleObj*)b);
                    break;
                }
                case eBaseTypeDispatcher:
                {
                    zDispatcher_InitDep((st_ZDISPATCH_DATA*)b, s);
                    break;
                }
                case eBaseTypeEGenerator:
                {
                    zEGenerator_Setup((zEGenerator*)b, s);
                    zEGenerator_Reset((zEGenerator*)b, s);
                    break;
                }
                case eBaseTypePickup:
                {
                    zEntPickup_Setup((zEntPickup*)b);
                    zEntPickup_Reset((zEntPickup*)b);
                    break;
                }
                case eBaseTypeParticleSystem:
                {
                    xParSysSetup((xParSys*)b);
                    break;
                }
                case eBaseTypeSurface:
                {
                    zSurfaceSetup((xSurface*)b);
                    break;
                }
                case eBaseTypeParticleEmitter:
                {
                    xParEmitterSetup((xParEmitter*)b);
                    break;
                }
                case eBaseTypeGroup:
                {
                    xGroupSetup((xGroup*)b);
                    break;
                }
                case eBaseTypeTeleportBox:
                {
                    zEntTeleportBox_Setup((_zEntTeleportBox*)b);
                    break;
                }
                case eBaseTypeDiscoFloor:
                {
                    ((z_disco_floor*)b)->setup();
                    break;
                }
                case eBaseTypeBusStop:
                {
                    zBusStop_Setup((zBusStop*)b);
                    break;
                }
                case eBaseTypeTaxi:
                {
                    zTaxi_Setup((zTaxi*)b);
                    break;
                }
                case eBaseTypeCameraFly:
                {
                    zCameraFly_Setup((zCameraFly*)b);
                    break;
                }
                case eBaseTypeDestructObj:
                case eBaseTypeProj:
                {
                    break;
                }
                case eBaseTypeBoulder:
                {
                    xEntBoulder_Setup((xEntBoulder*)b);
                    break;
                }
                case eBaseTypeTrigger:
                case eBaseTypePlayer:
                case eBaseTypeCamera:
                case eBaseTypeDoor:
                case eBaseTypeSavePoint:
                case eBaseTypeItem:
                case eBaseTypeDynamic:
                case eBaseTypeTimer:
                case eBaseTypeBubble:
                case eBaseTypePortal:
                case eBaseTypeSFX:
                case eBaseTypeFFX:
                case eBaseTypeVFX:
                case eBaseTypeCounter:
                case eBaseTypeHangable:
                case eBaseTypeCond:
                case eBaseTypeUI:
                case eBaseTypeUIFont:
                case eBaseTypeProjectileType:
                case eBaseTypeFog:
                case eBaseTypeLight:
                case eBaseTypeCutsceneMgr:
                case eBaseTypeScript:
                case eBaseTypeHud:
                case eBaseTypeNPCProps:
                case eBaseTypeParticleEmitterProps:
                case eBaseTypeCruiseBubble:
                case eBaseTypeTextBox:
                case eBaseTypeTalkBox:
                case eBaseTypeTaskBox:
                case eBaseTypeBoulderGenerator:
                case eBaseTypeNPCSettings:
                case eBaseTypeHUD_model:
                case eBaseTypeHUD_font_meter:
                case eBaseTypeHUD_unit_meter:
                case eBaseTypeTurret:
                case eBaseTypeSlideProps:
                case eBaseTypeReference:
                case eBaseTypeSoundFX:
                case eBaseTypeKaboomMinigame:
                case eBaseTypeDestructibleAsset:
                case eBaseTypeFlameEmitter:
                case eBaseTypeUIText:
                case eBaseTypeUIImage:
                case eBaseTypeUIModel:
                case eBaseTypeSceneSettingsAsset:
                case eBaseTypeLightning:
                case eBaseTypeWaterhose:
                case eBaseTypeBulletMarkAsset:
                case eBaseTypeBulletTimeAsset:
                case eBaseTypeProgressScript:
                {
                    break;
                }
                }
            }
        }
    }

    zFire::setup();
    zFlameEmitter::setup_all();
    zSpotlight::setup_all();
    zVent::setupAll();
    zEntPickup_Setup();
    zThrown_Setup(globals.sceneCur);
    zNMECentral_SceneSetup();
    xFX_SceneSetup();
    zSceneFindEnemyManlinessPoints();
    zEntHangable_SetupFX();

    xglobals->___player_ent_dont_use_directly->SceneSetup();

    if (gGameMode == eGameMode_Game)
    {
        zMenuSB_InitPowerUpMenu();
    }

    for (S32 i = 0; i < s->num_base; i++)
    {
        if (s->base[i])
        {
            switch (s->base[i]->baseType)
            {
            case eBaseTypeNPC:
            {
                break;
            }
            case eBaseTypeMovePoint:
            {
                xMovePointSplineSetup((xMovePoint*)s->base[i]);
                break;
            }
            }
        }
    }

    z::lightweight_manager::scene_setup();
    zNMECentral_ScenePostSetup();
    zCineFXSceneSetup();
    zParticleSetup();
    zSceneEnableVisited(s);
    xSerialTraverse(zSceneSetup_serialTraverseCB);

    enableScreenAdj = 0;

    zLOD_Setup(4.0f);

    for (S32 i = 0; i < s->num_base; i++)
    {
        if (s->base[i] && s->base[i]->baseType == eBaseTypeNPC)
        {
            xEnt* ent = (xEnt*)s->base[i];
            zLODTable* lod = zLOD_Get(ent);

            if (lod)
            {
                if (lod->lodBucket[2])
                {
                    ent->entShadow->shadowModel = lod->lodBucket[2][0]->Data;
                }
                else if (lod->lodBucket[1])
                {
                    ent->entShadow->shadowModel = lod->lodBucket[1][0]->Data;
                }
            }
        }
    }

    xAntennaSceneInit();

    {
        S32 max_drivensort_iters = 256;
        U32 driven_swapped;
        U32 i, j;

        do
        {
            driven_swapped = 0;

            for (i = 0; i < s->num_update_base; i++)
            {
                xEnt* bdriven = (xEnt*)s->update_base[i];

                if (bdriven->baseFlags & 0x20)
                {
                    if (bdriven->driver)
                    {
                        for (j = i + 1; j < s->num_update_base; j++)
                        {
                            if (bdriven->driver == s->update_base[j])
                            {
                                driven_swapped = 1;

                                xBase* btmp = s->update_base[i];

                                s->update_base[i] = s->update_base[j];
                                s->update_base[j] = btmp;
                            }
                        }
                    }
                }
            }
        } while (--max_drivensort_iters && driven_swapped);
    }

    zSceneResetLightKits();

    {
        zEntSimpleObj** entList =
            (zEntSimpleObj**)&s
                ->act_ents[s->baseCount[eBaseTypeTrigger] + s->baseCount[eBaseTypePickup]];
        U32 entCount = s->baseCount[eBaseTypeStatic];

        zReactiveSceneEnter();
        zEntSimpleObj_MgrInit(entList, entCount);
    }

    xEnt** entList = &s->act_ents[s->baseCount[eBaseTypeTrigger] + s->baseCount[eBaseTypePickup]];
    S32 entCount = s->num_act_ents - s->baseCount[eBaseTypeTrigger] - s->baseCount[eBaseTypePickup];
    S32 i, j;
    U32 k;
    U32 numPrimeMovers = 0;
    U32 numDriven = 0;

    for (i = 0; i < s->num_base; i++)
    {
        if (s->base[i] && (s->base[i]->baseFlags & 0x20))
        {
            ((xEnt*)s->base[i])->_isCulled = 0;
        }
    }

    for (i = 0; i < s->num_base; i++)
    {
        if (s->base[i] && (s->base[i]->baseFlags & 0x20) && ((xEnt*)s->base[i])->driver)
        {
            xEnt* ent = (xEnt*)s->base[i];

            if (!ent->_isCulled)
            {
                numDriven++;
                ent->_isCulled = 2;
            }

            while (ent->driver)
            {
                ent = ent->driver;
            }

            if (!ent->_isCulled)
            {
                numPrimeMovers++;
                ent->_isCulled = 1;
            }
        }
    }

    U32 numGroups = 0;

    for (i = 0; i < s->num_base; i++)
    {
        if (s->base[i] && s->base[i]->baseType == eBaseTypeGroup)
        {
            xGroup* grp = (xGroup*)s->base[i];

            for (j = 0; j < grp->linkCount; j++)
            {
                // Like BfBB, this checks grp->link (the first link) on every pass instead of
                // grp->link[j].
                if (grp->link->srcEvent == eEventSceneBegin &&
                    grp->link->dstEvent == eEventGroupUpdateTogether)
                {
                    numGroups++;

                    U32 gcnt = xGroupGetCount(grp);

                    for (k = 0; k < gcnt; k++)
                    {
                        xBase* gbase = xGroupGetItemPtr(grp, k);

                        if (gbase && (gbase->baseFlags & 0x20))
                        {
                            xEnt* gent = (xEnt*)gbase;

                            if (gent->_isCulled)
                            {
                                if (gent->_isCulled == 1)
                                {
                                    numPrimeMovers--;
                                }

                                if (gent->_isCulled != 1)
                                {
                                    numDriven--;
                                }

                                gent->_isCulled = 0;
                            }
                        }
                    }
                }
            }
        }
    }

    xGroup* driveGroupList = NULL;

    if (numPrimeMovers)
    {
        U32 allocsize = numPrimeMovers * sizeof(xGroup) + numPrimeMovers * sizeof(xGroupAsset) +
                        (numPrimeMovers + numDriven) * sizeof(xBase*);

        driveGroupList = (xGroup*)RwMalloc(allocsize, 0);
        memset(driveGroupList, 0, allocsize);

        xGroupAsset* grpAssetList = (xGroupAsset*)(driveGroupList + numPrimeMovers);
        xBase** grpBaseList = (xBase**)(grpAssetList + numPrimeMovers);

        for (U32 i = 0; i < numPrimeMovers; i++)
        {
            driveGroupList[i].baseType = eBaseTypeGroup;
            driveGroupList[i].asset = &grpAssetList[i];
            driveGroupList[i].flg_group |= 0x1;
        }

        U32 currMover = 0;

        for (S32 i = 0; i < s->num_base; i++)
        {
            if (s->base[i] && (s->base[i]->baseFlags & 0x20))
            {
                xEnt* ent = (xEnt*)s->base[i];

                if (ent->_isCulled == 1)
                {
                    xGroupAsset* gasset = driveGroupList[currMover].asset;

                    driveGroupList[currMover].item = grpBaseList;
                    *grpBaseList++ = ent;
                    gasset->itemCount++;

                    for (j = 0; j < s->num_base; j++)
                    {
                        if (s->base[j] && (s->base[j]->baseFlags & 0x20))
                        {
                            xEnt* other = (xEnt*)s->base[j];

                            if (other->_isCulled == 2)
                            {
                                while (other->driver)
                                {
                                    other = other->driver;
                                }

                                if (ent == other)
                                {
                                    *grpBaseList++ = s->base[j];
                                    gasset->itemCount++;
                                }
                            }
                        }
                    }

                    if (gasset->itemCount > 1)
                    {
                        numGroups++;
                    }

                    currMover++;
                }
            }
        }
    }

    xGroup** tempGrpList = NULL;

    if (numGroups)
    {
        tempGrpList = (xGroup**)RwMalloc(numGroups * sizeof(xGroup*), 0);

        xGroup** tempGrpCurr = tempGrpList;

        for (i = 0; i < s->num_base; i++)
        {
            if (s->base[i] && s->base[i]->baseType == eBaseTypeGroup)
            {
                xGroup* grp = (xGroup*)s->base[i];

                for (j = 0; j < grp->linkCount; j++)
                {
                    // Same first-link check as above.
                    if (grp->link->srcEvent == eEventSceneBegin &&
                        grp->link->dstEvent == eEventGroupUpdateTogether)
                    {
                        *tempGrpCurr++ = grp;
                    }
                }
            }
        }

        for (U32 i = 0; i < numPrimeMovers; i++)
        {
            if (driveGroupList[i].asset->itemCount > 1)
            {
                *tempGrpCurr++ = &driveGroupList[i];
            }
        }
    }

    globals.updateMgr = xUpdateCull_Init((void**)entList, entCount, tempGrpList, numGroups);
    globals.updateMgr->activateCB = (xUpdateCullActivateCallback)ActivateCB;
    globals.updateMgr->deactivateCB = (xUpdateCullDeactivateCallback)DeactivateCB;

    FloatAndVoid defaultDist;
    defaultDist.f = 4900.0f;

    FloatAndVoid lodDist;
    lodDist.f = 0.0f;

    for (i = 0; i < entCount; i++)
    {
        zLODTable* lod = zLOD_Get(entList[i]);

        if (lod)
        {
            if (lod->noRenderDist == 0.0f)
            {
                lod = NULL;
            }
            else
            {
                lodDist.f = SQR(10.0f + xsqrt(lod->noRenderDist));
            }
        }

        xUpdateCull_SetCB(globals.updateMgr, entList[i], xUpdateCull_DistanceSquaredCB,
                          (lod) ? lodDist.v : defaultDist.v);
    }

    if (tempGrpList)
    {
        RwFree(tempGrpList);
    }

    if (driveGroupList)
    {
        RwFree(driveGroupList);
    }

    for (i = 0; i < s->num_base; i++)
    {
        if (s->base[i] && (s->base[i]->baseFlags & 0x20))
        {
            ((xEnt*)s->base[i])->_isCulled = 0;
        }
    }

    zEntPickup_UpdateInit((zEntPickup*)s->baseList[eBaseTypePickup], s->baseCount[eBaseTypePickup]);

    xScrFxFade(&g_BLACK, &g_CLEAR, 1.0f, 0);

    xModelInstStaticAlloc = 0;

    zUIMgrEnableHDR(false);
}

// No GC or PS2 symbol (inlined into zSceneResetLightKits). Name, parameter and locals from
// Ratatouille's DWARF, where it is a real function.
inline void zSceneSetObjectLightKit(xLightKit* objLightKit)
{
    S32 f;
    zScene* zsc = globals.sceneCur;

    for (f = 0; f < zsc->num_base; f++)
    {
        if (zsc->base[f] && (zsc->base[f]->baseFlags & 0x20) &&
            zsc->base[f]->baseType != eBaseTypeNPC)
        {
            xEnt* tgtent = (xEnt*)zsc->base[f];

            if (tgtent->model)
            {
                xModelSetLightKit(tgtent->model, objLightKit);
            }
        }
    }
}

// No GC symbol (inline; a real function in the PS2 build). Name and locals from the PS2 DWARF.
inline void zSceneResetLightKits()
{
    S32 i;
    xEnvAsset* easset = globals.sceneCur->zen->easset;

    if (easset->bspLightKit)
    {
        globals.sceneCur->env->lightKit = (xLightKit*)xSTFindAsset(easset->bspLightKit, NULL);
    }

    if (easset->objectLightKit)
    {
        xLightKit* objLightKit = (xLightKit*)xSTFindAsset(easset->objectLightKit, NULL);

        if (objLightKit)
        {
            zSceneSetObjectLightKit(objLightKit);
        }
    }

    S32 lkitCount = xSTAssetCountByType('LKIT');

    for (i = 0; i < lkitCount; i++)
    {
        xLightKit* lkit = (xLightKit*)xSTFindAssetByType('LKIT', i, NULL);

        if (lkit->groupID)
        {
            xGroup* group = (xGroup*)zSceneFindObject(lkit->groupID);

            if (group)
            {
                U32 j, nitam;

                nitam = xGroupGetCount(group);

                for (j = 0; j < nitam; j++)
                {
                    xBase* itamz = xGroupGetItemPtr(group, j);

                    if (itamz && (itamz->baseFlags & 0x20) && ((xEnt*)itamz)->model)
                    {
                        xModelSetLightKit(((xEnt*)itamz)->model, lkit);
                    }
                }
            }
        }
    }
}

S32 zSceneSetup_serialTraverseCB(U32 clientID, xSerial* xser)
{
    char uiName[16];
    S32 val = 0;
    xBase* b;

    xser->Read_b1(&val);

    if (val == 0)
    {
        return 1;
    }

    sprintf(uiName, "%s", xUtil_idtag2string(clientID, 0));

    uiName[1] = ' ';

    strcat(uiName, " VISITED");

    b = zSceneFindObject(xStrHash(uiName));

    if (b)
    {
        b->baseFlags |= 0x1;
    }

    return 1;
}

// Defined after zSceneExit, as retail's data has these after the statics of the functions above.
F32 gSceneUpdateTime;
static xVec3 sOldPosPlayer;
static xVec3 sOldPosCamera;
static U32 sSuddenMove;
static IDBasePair* scobj_idbps;
static S32 scobj_size = -1;
static S32 nidbps = -1;
xVec3 scale = { 0.2f, -0.7f, 1.5f };

void zSceneUpdate(F32 elapsedSec)
{
    U32 i;
    S32 isPaused;
    F32 apadx;
    F32 apady;
    zScene* pScene;
    xBase** ppBaseObjs;
    xBase* bob;
    xEnt* ent;
    S32 mode;

    if (0.0f == elapsedSec)
    {
        return;
    }

    isPaused = zGameIsPaused();
    gSceneUpdateTime = elapsedSec;

    if (!isPaused)
    {
        zEntButton_SceneUpdate(elapsedSec);
    }

    zEntPickup_SceneUpdate(elapsedSec);
    zStreamedSound::UpdateSounds();

    if (globals.cmgr)
    {
        zCutsceneMgrUpdate(globals.cmgr, globals.sceneCur, elapsedSec);
    }
    else
    {
        zHud::hud_system::instance().update(elapsedSec);
    }

    if (globals.DisabledCutsceneDoneMgr)
    {
        zEntEvent(globals.DisabledCutsceneDoneMgr, globals.DisabledCutsceneDoneMgr, eEventDone);
        globals.DisabledCutsceneDoneMgr = NULL;
    }

    apadx = 0.0f;
    apady = 0.0f;

    if (enableScreenAdj)
    {
        if (globals.pad0->pressed & 0x40000)
        {
            offsetx = oldOffsetx;
            offsety = oldOffsety;
        }

        if (globals.pad0->pressed & 0x20000)
        {
            offsetx = 0.0f;
            offsety = 0.0f;
        }

        if (globals.pad0->analog1.x > 45.0f)
        {
            apadx = globals.pad0->analog1.x - 45.0f;
            apadx = (apadx > 90.0f) ? 90.0f : apadx;
        }

        if (globals.pad0->analog1.x < -45.0f)
        {
            apadx = globals.pad0->analog1.x + 45.0f;
            apadx = (apadx < -90.0f) ? -90.0f : apadx;
        }

        if (globals.pad0->analog1.y > 45.0f)
        {
            apady = globals.pad0->analog1.y - 45.0f;
            apady = (apady > 90.0f) ? 90.0f : apady;
        }

        if (globals.pad0->analog1.y < -45.0f)
        {
            apady = globals.pad0->analog1.y + 45.0f;
            apady = (apady < -90.0f) ? -90.0f : apady;
        }

        if (globals.pad0->on & 0x10)
        {
            offsety -= 0.001f;
        }
        else if (globals.pad0->on & 0x40)
        {
            offsety += 0.001f;
        }
        else if (globals.pad0->on & 0x80)
        {
            offsetx -= 0.001f;
        }
        else if (globals.pad0->on & 0x20)
        {
            offsetx += 0.001f;
        }

        if (globals.pad0->pressed & 0xf0)
        {
            xSndMgrPlay(xSndMgrGetSoundGroup(xStrHash("mnu4_dial_click")), 0, NULL, NULL, NULL,
                        NULL, NULL);
        }

        offsetx += 0.00006f * apadx;
        offsety += 0.00006f * apady;

        if (offsetx > 0.2f)
        {
            offsetx = 0.2f;
        }

        if (offsetx < -0.2f)
        {
            offsetx = -0.2f;
        }

        if (offsety > 0.2f)
        {
            offsety = 0.2f;
        }

        if (offsety < -0.2f)
        {
            offsety = -0.2f;
        }
    }

    pScene = globals.sceneCur;
    ppBaseObjs = pScene->update_base;

    ztextbox::update_all(*pScene, elapsedSec);
    ztalkbox::update_all(*pScene, elapsedSec);
    zUIMgrUpdate(elapsedSec);

    if (xglobals->___player_ent_dont_use_directly->model)
    {
        if (xVec3Dist(&sOldPosPlayer,
                      (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos) > 5.0f ||
            xVec3Dist(&sOldPosCamera, &globals.cam->mat.pos) > 5.0f)
        {
            sSuddenMove = 1;
        }
        else
        {
            sSuddenMove = 0;
        }
    }

    sOldPosPlayer = *(xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos;
    sOldPosCamera = globals.cam->mat.pos;

    xUpdateCull_Update(globals.updateMgr, sSuddenMove ? 100 : 5);

    for (i = 0; i < pScene->num_update_base; i++)
    {
        bob = ppBaseObjs[i];

#ifdef PLATFORM_PC
        // Online co-op (docs/MULTIPLAYER.md 5.5): on the guest, a platform the
        // host owns is posed from the host's snapshots instead of moving by
        // itself. FALSE at once when co-op is off.
        if (iCoopWorldGuestPose(bob, elapsedSec))
        {
            continue;
        }
#endif

        if ((bob->baseFlags & 0x1) && !(bob->baseFlags & 0x40))
        {
            switch (bob->baseType)
            {
            case eBaseTypeUIFont:
                ent = (xEnt*)bob;
                if (ent->update)
                {
                    ent->update(ent, pScene, elapsedSec);
                }
                break;
            case eBaseTypeTrigger:
            case eBaseTypePickup:
            case eBaseTypePlatform:
            case eBaseTypeDoor:
            case eBaseTypeSavePoint:
            case eBaseTypeItem:
            case eBaseTypeStatic:
            case eBaseTypeDynamic:
            case eBaseTypePendulum:
            case eBaseTypeHangable:
            case eBaseTypeButton:
            case eBaseTypeDestructObj:
            case eBaseTypeEGenerator:
            case eBaseTypeNPC:
            case eBaseTypeBoulder:
            case eBaseTypeTeleportBox:
            case eBaseTypeDuplicator:
                ent = (xEnt*)bob;
                if (ent->update && !isPaused)
                {
                    ent->update(ent, pScene, elapsedSec);
                }
                break;
            case eBaseTypeTrainCar:
                ent = (xEnt*)bob;
                if (ent->update && !isPaused)
                {
                    ent->update(ent, pScene, elapsedSec);
                }
                break;
            case eBaseTypeTimer:
                if (!isPaused || ((xTimer*)bob)->runsInPause)
                {
                    xTimerUpdate(bob, pScene, elapsedSec);
                }
                break;
            case eBaseTypeScript:
                if (!isPaused)
                {
                    zScriptUpdate(bob, pScene, elapsedSec);
                }
                DoTheResetinator();
                break;
            case eBaseTypeFog:
                if (!isPaused)
                {
                    xFogUpdate(bob, pScene, elapsedSec);
                }
                break;
            case eBaseTypeParticleEmitter:
                if (!isPaused)
                {
                    xParEmitterUpdate(bob, pScene, elapsedSec);
                }
                break;
            case eBaseTypeParticleSystem:
                if (!isPaused && !zGameIsPaused())
                {
                    xParSysUpdate(bob, pScene, elapsedSec);
                }
                break;
            case eBaseTypeLight:
                if (!isPaused)
                {
                    zLightUpdate(bob, pScene, elapsedSec);
                }
                break;
            case eBaseTypeSurface:
                if (!isPaused || ((xSurface*)bob)->type == XSURFACE_TYPE_3)
                {
                    zSurfaceUpdate(bob, pScene, elapsedSec);
                }
                break;
            case eBaseTypeDiscoFloor:
                if (!isPaused)
                {
                    ((z_disco_floor*)bob)->update(*pScene, elapsedSec);
                }
                break;
            case eBaseTypeBusStop:
                if (!isPaused)
                {
                    zBusStop_Update(bob, pScene, elapsedSec);
                }
                break;
            case eBaseTypeTaxi:
                if (!isPaused)
                {
                    zTaxi_Update(bob, pScene, elapsedSec);
                }
                break;
            case eBaseTypeRaceTimer:
                if (!isPaused)
                {
                    ((zRaceTimer*)bob)->Update(elapsedSec);
                }
                break;
            case eBaseTypeRingControl:
                if (!isPaused)
                {
                    ((zRingControl*)bob)->Update(elapsedSec);
                }
                break;
            case eBaseTypeCameraFly:
                if (!isPaused)
                {
                    zCameraFly_Update(bob, pScene, elapsedSec);
                }
                break;
            case eBaseTypeFunctionGenerator:
                if (!isPaused)
                {
                    ((xFunctionGenerator*)bob)->Update(elapsedSec);
                }
                break;
            }
        }
    }

    zNMECentral_Timestep(elapsedSec);
    zEntPickup_UpdateAll(elapsedSec);

    if (!isPaused)
    {
        zEntSimpleObj_MgrCustomUpdate(pScene, elapsedSec);
    }

    if (!isPaused)
    {
        xFXHighDynamicRangeUpdate(elapsedSec);
        zReactiveUpdate(elapsedSec);
        zSpotlight::update_all(elapsedSec);
        zFlameEmitter::update_all(elapsedSec);
        zSmokeUpdate(elapsedSec);
        zSplashUpdate(elapsedSec);
        zDustUpdate(elapsedSec);
        zVent::updateAll(elapsedSec);
        xFXStreakUpdate(elapsedSec);
        xFXShineUpdate(elapsedSec);
        xFXFireworksUpdate(elapsedSec);
        zActionLineUpdate(elapsedSec);
        zLightningUpdate(elapsedSec);
        xClimateUpdate(&gClimate, elapsedSec);
        zShrapnel_Update(elapsedSec);
        zFire::update(elapsedSec);
        zExplosion::update(elapsedSec);
        zFXUpdate(elapsedSec);
        zLOD_Update(sSuddenMove ? 100 : 5);
        zParticleUpdate(elapsedSec);
        zParticleCustomUpdate(elapsedSec);
        xDecal::update(elapsedSec);
        xDecalUpdate(elapsedSec);
        xScreenWarp::update(elapsedSec);
        xCMupdate(elapsedSec);
        zNMECentral_PostTimestep(elapsedSec);
        globals.player.rewardsMgr->Update(elapsedSec);
        z::lightweight_manager::update(elapsedSec);

        if (pScene->pendingPortal)
        {
            zGameStateSwitch(eGameState_SceneSwitch);
        }
    }

    mode = zGameModeGet();

    if (mode == eGameMode_Intro || mode == eGameMode_Title || !isPaused || mode == eGameMode_Load)
    {
        zParPTankUpdate(elapsedSec);
    }
}

void zSceneRenderPreFX()
{
    zScene* s = globals.sceneCur;
    xScene* sc;

    globals.currWorld = s->env->geom->world;

    iDrawSetDisplayOffset(offsetx, offsety);

    if (s->enableDrawing)
    {
        xLightKit_Enable(NULL, globals.currWorld);

        zRenderState(SDRS_SkyBack);
        xSkyDome_Render();

        zRenderState(SDRS_Environment);
        zLightAddLocalEnv();
        zEnvRender(s->env);
        zLightRemoveLocalEnv();

        zRenderState(SDRS_OpaqueModels);

        U32 shadowHackCase = 0;

        xEnt** entptr = &s->act_ents[s->num_act_ents - 1];
        xEnt** entlast =
            &s->act_ents[s->baseCount[eBaseTypeTrigger] + s->baseCount[eBaseTypePickup] +
                         s->baseCount[eBaseTypeStatic]];

        xModelBucket_Begin();
        zEntSimpleObj_MgrUpdateRender(globals.currWorld, sTimeElapsed);
        zEntSimpleObj_MgrCustomRender();
        zReactiveRender();

        while (entptr >= entlast)
        {
            xEnt* ent = *entptr;

            if (ent->collType != XENT_COLLTYPE_TRIG)
            {
                if (ent->collType == XENT_COLLTYPE_PLYR)
                {
                    shadowHackCase = 1;
                }
                else if (ent->baseType != eBaseTypeNPC && ent->render)
                {
                    ent->render(ent);
                }
            }

            entptr--;
        }

        z::lightweight_manager::render();
        zNMECentral_RenderToBuckets();
        zVent::renderAll();
        zShrapnel_Render();

        if (shadowHackCase == 1)
        {
            if (!oob_state::render())
            {
                xglobals->___player_ent_dont_use_directly->render(
                    xglobals->___player_ent_dont_use_directly);
            }

#ifdef PLATFORM_PC
            // Online co-op (docs/MULTIPLAYER.md): the partner, drawn like the
            // player (buckets off, the player's light kit). Returns at once
            // when co-op is off.
            iCoopRenderPartner();
#endif

            xLightKit_Enable(NULL, globals.currWorld);
        }

        xModelBucket_RenderOpaqueBegin();
        xModelBucket_RenderOpaque(-1);
        zRingControl::RenderAll();
        zDustRender();
        zNMECentral_Render();
        xAntennaRender();

        xModelBucket_RenderAlphaBegin();

        if (xModelBucket_RenderAny(1))
        {
            zRenderState(SDRS_AlphaModels);
            xModelBucket_RenderAlphaLayer(1);
        }

        if (gCurrentPlayer == eCurrentPlayerCar)
        {
            zEntPlayerCarRenderFX(xglobals->___player_ent_dont_use_directly);
        }

        xLightKit_Enable(NULL, globals.currWorld);
        zEntPickup_RenderList((zEntPickup*)s->baseList[eBaseTypePickup],
                              s->baseCount[eBaseTypePickup]);
        zEntCruiseBubbleRenderFX();

        if (xModelBucket_RenderAny(5))
        {
            zRenderState(SDRS_AlphaModels);
            xModelBucket_RenderAlphaLayer(5);
        }

        if (globals.cmgr && globals.cmgr->csn->Ready)
        {
            sc = globals.sceneCur;

            if (!sc->env->lightKit)
            {
                xLightKit* objLightKit = NULL;

                if (globals.sceneCur->zen->easset->objectLightKit)
                {
                    objLightKit = (xLightKit*)xSTFindAsset(
                        globals.sceneCur->zen->easset->objectLightKit, NULL);
                }

                if (objLightKit)
                {
                    xLightKit_Enable(objLightKit, globals.currWorld);
                }
            }
            else
            {
                xLightKit_Enable(sc->env->lightKit, globals.currWorld);
            }

            xCutscene_Render(globals.cmgr->csn, NULL, NULL, NULL);
            xLightKit_Enable(NULL, globals.currWorld);
        }

        if (xModelBucket_RenderAny(9))
        {
            zRenderState(SDRS_AlphaModels);
            xModelBucket_RenderAlphaLayer(9);
        }

        zSpotlight::early_render();
        zEntPlayer_ShadowModelEnable();
        xShadowManager_Render();
        zEntPlayer_ShadowModelDisable();
        xShadowSimple_Render();
        xFXHighDynamicRangeStartRender();

        if (xModelBucket_RenderAny(15))
        {
            zRenderState(SDRS_AlphaModels);
            xModelBucket_RenderAlphaLayer(15);
        }

        xLightVolume::render_all();
        xDecal::render_all();

        zRenderState(SDRS_AlphaModels);
        xFXAuraRender();

        if (xModelBucket_RenderAny(16))
        {
            zRenderState(SDRS_AlphaModels);
            xModelBucket_RenderAlphaLayer(16);
        }

        zRenderState(SDRS_Lightning);
        zLightningRender();
        zActionLineRender();

        zRenderState(SDRS_Streak);
        xFXStreakRender();
        xFXShineRender();
        xFXRibbon::render_all();
        zSpotlight::render_all();
        zSplashRender();

        if (xModelBucket_RenderAny(18))
        {
            zRenderState(SDRS_AlphaModels);
            xModelBucket_RenderAlphaLayer(18);
        }

        zRenderState(SDRS_Particles);

        zParSys* psys = (zParSys*)s->baseList[eBaseTypeParticleSystem];

        for (S32 i = s->baseCount[eBaseTypeParticleSystem] - 1; i >= 0; i--, psys++)
        {
            xParSysRender(psys);
        }

        zRenderState(SDRS_AlphaModels);
        xEnvRender(s->env, true);

        zRenderState(SDRS_AlphaModels);
        xModelBucket_RenderAlphaLayer(29);

        xLightKit_Enable(NULL, globals.currWorld);

        if (xModelBucket_RenderAny(7))
        {
            zRenderState(SDRS_AlphaModels);
            xModelBucket_RenderAlphaLayer(7);
        }

        zNMECentral_PostRender();
        zFlameEmitter::render_all();
        zFire::render();
        zExplosion::render();

        if (xModelBucket_RenderAny(29))
        {
            zRenderState(SDRS_AlphaModels);
            xModelBucket_RenderAlphaLayer(29);
        }

        zParticleRender();
        xParMgrRender();
        zParPTankRender();
        xPTankPoolRender();

        if (xModelBucket_RenderAny(31))
        {
            zRenderState(SDRS_AlphaModels);
            xModelBucket_RenderAlphaLayer(31);
        }

        if (xModelBucket_RenderAny(33))
        {
            zRenderState(SDRS_AlphaModels);
            xModelBucket_RenderAlphaLayer(33);
        }

        xModelBlur::render_all();
        z::lightweight_manager::post_render_3d();
        z::lightweight_manager::post_render_2d();
        xglobals->___player_ent_dont_use_directly->RenderEffects();

        if (xModelBucket_RenderAny(36))
        {
            zRenderState(SDRS_AlphaModels);
            xModelBucket_RenderAlphaLayer(36);
        }

        xModelBucket_RenderAlphaEnd();
    }
}

static void zSceneRenderPostFX()
{
    if (zGameModeGet() == eGameMode_Intro &&
        (zGameStateGet() == eIntroState_Sony || zGameStateGet() == eIntroState_Publisher))
    {
        render_mem_card_no_space(bytesNeeded, availOnDisk, neededFiles,
                                 zGameStateGet() == eIntroState_Sony);
    }

    if (globals.sceneCur->enableDrawing)
    {
        cruise_bubble::render_screen();
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        xScreenWarp::render(globals.screen->icam);
        xFXHighDynamicRangeRender(globals.screen->icam);
        xScrFxLensFlare_Render(globals.screen->icam);
    }
    else
    {
        iDrawSetDisplayOffset(offsetx, offsety);
    }

    iScrFxBegin();
    xScrFxRenderLetterBox(globals.screen->icam);
    iScrFxEnd();

    RwCameraEndUpdate(globals.screen->icam);

    if (gGameMode != eGameMode_Game)
    {
        RwCameraClear(globals.screen->icam, NULL, rwCAMERACLEARZ);
    }

    zNMECentral_HudRender();
    RwCameraBeginUpdateWrapper(globals.screen->icam);

    zRenderState(SDRS_Font);
    xDrawTextRender();

    if (globals.sceneCur->enableDrawing)
    {
        ztextbox::render_all();
        ztalkbox::render_all();
    }

    xglobals->___player_ent_dont_use_directly->RenderCustomUI();

    zUIMgrRender();

    eGameMode mode = zGameModeGet();

    if (mode != eGameMode_Game && mode != eGameMode_Stall)
    {
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
        zSceneSpawnRandomBubbles();
        UpgradeSparkleEffect::Update(sTimeElapsed);

        if (mode == eGameMode_Pause || mode == eGameMode_Save)
        {
            zParPTankUpdate(sTimeElapsed);
        }

        zParPTankRender();
    }

    switch (mode)
    {
    case eGameMode_Load:
    case eGameMode_WorldMap:
    case eGameMode_ConceptArtGallery:
        break;
    case eGameMode_Options:
    case eGameMode_Save:
    case eGameMode_Pause:
    case eGameMode_MonsterGallery:
    case eGameMode_Game:
        zRenderState(SDRS_HUD);
        zHud::hud_system::instance().render();
        break;
    }

    if (zGameModeGet() == eGameMode_Intro &&
        (zGameStateGet() == eIntroState_Sony || zGameStateGet() == eIntroState_Publisher))
    {
        render_mem_card_no_space(bytesNeeded, availOnDisk, neededFiles,
                                 zGameStateGet() == eIntroState_Sony);
    }

    xCMrender();
    xTRCRender();

    if (zGameModeGet() == eGameMode_Intro && zGameStateGet() == eIntroState_License)
    {
        xScrFxDrawScreenSizeRectangle();
    }
}

#ifdef PLATFORM_PC
// PLATFORM_PC: the shadows drawn before the scene's camera begins, so the scene
// is drawn without leaving its frame buffer for each one (xShadow.cpp,
// xShadowManager_PreRender). Called from the game loop beside the other
// pre-render work, before zCamBeginRender. The player's shadow model is put in
// place as zSceneRenderPreFX does around xShadowManager_Render, and what
// putting it there changes (the player's bounds) is put back, so the scene
// sees the player as it would have.
void zScenePreRenderShadows()
{
    if (globals.sceneCur == NULL || !globals.sceneCur->enableDrawing || globals.screen == NULL)
    {
        return;
    }

    enum
    {
        MAX_PARTS = 32
    };
    xModelInstance* parts[MAX_PARTS];
    xBox bounds[MAX_PARTS];
    U16 flags[MAX_PARTS];
    S32 count = 0;
    xModelInstance* head = xglobals->___player_ent_dont_use_directly != NULL ?
                               xglobals->___player_ent_dont_use_directly->model :
                               NULL;
    xBox combined;

    if (head != NULL)
    {
        combined = head->combinedAnimBound;
    }
    for (xModelInstance* m = head; m != NULL; m = m->Next)
    {
        if (count == MAX_PARTS)
        {
            return;
        }
        parts[count] = m;
        bounds[count] = m->animBound;
        flags[count] = m->Flags;
        count++;
    }
    // The shadow model and the body (zEntPlayer_ShadowModelEnable) need not be
    // in the chain the player draws.
    xModelInstance* extra[2] = { globals.player.sb_models[0], globals.player.sb_models[10] };
    xBox extraBounds[2];
    U16 extraFlags[2];
    for (S32 i = 0; i < 2; i++)
    {
        if (extra[i] != NULL)
        {
            extraBounds[i] = extra[i]->animBound;
            extraFlags[i] = extra[i]->Flags;
        }
    }

    zEntPlayer_ShadowModelEnable();
    xShadowManager_PreRender(globals.screen->icam);
    zEntPlayer_ShadowModelDisable();

    for (S32 i = 0; i < 2; i++)
    {
        if (extra[i] != NULL)
        {
            extra[i]->animBound = extraBounds[i];
            extra[i]->Flags = extraFlags[i];
        }
    }
    for (S32 i = 0; i < count; i++)
    {
        parts[i]->animBound = bounds[i];
        parts[i]->Flags = flags[i];
    }
    if (head != NULL)
    {
        head->combinedAnimBound = combined;
    }
}
#endif

void zSceneRender()
{
    zRenderStateBegin();

    if (globals.sceneCur->enableDrawing)
    {
        zSceneRenderPreFX();
        zRenderState(SDRS_Glare);
        xScrFXGlareRender(globals.cam->mat);
        xScrFX_PulseGlaresRender(globals.cam->mat);
        xScrFXFullScreenGlareRender();
        xScrFxRender(globals.screen->icam);
    }

    zSceneRenderPostFX();

    xglobals->___player_ent_dont_use_directly->PostRenderCleanup();
}

// No GC symbol: inlined into zSceneInit.
static inline void zSceneObjHashtableInit(S32 count)
{
    scobj_idbps = (IDBasePair*)xMemAllocSize(count * sizeof(IDBasePair));

    memset(scobj_idbps, 0, count * sizeof(IDBasePair));

    scobj_size = count;
    nidbps = 0;
}

// No GC symbol: inlined into zSceneExit.
static inline void zSceneObjHashtableExit()
{
    scobj_idbps = NULL;
    scobj_size = -1;
    nidbps = -1;
}

void zSceneObjHashtableAdd(U32 id, xBase* base)
{
    S32 k, chkd;

    chkd = id & (scobj_size - 1);

    for (k = 0; k < scobj_size; k++)
    {
        IDBasePair* idbp = &scobj_idbps[chkd];

        if (idbp->id == 0)
        {
            idbp->id = id;
            idbp->base = base;
            nidbps++;
            break;
        }

        chkd++;

        if (chkd == scobj_size)
        {
            chkd = 0;
        }
    }
}

xBase* zSceneFindObject(U32 gameID)
{
    return zSceneObjHashtableGet(gameID);
}

const char* zSceneGetName(U32 gameID)
{
    xBase* b = zSceneFindObject(gameID);

    if (b)
    {
        return zSceneGetName(b);
    }

    const char* n = xSTAssetName(gameID);

    if (n)
    {
        return n;
    }

    return "";
}

static inline xBase* zSceneObjHashtableGet(U32 id)
{
    S32 k, chkd;

    chkd = id & (scobj_size - 1);

    for (k = 0; k < scobj_size; k++)
    {
        IDBasePair* idbp = &scobj_idbps[chkd];

        if (idbp->id == id)
        {
            return idbp->base;
        }

        if (idbp->id == 0)
        {
            return NULL;
        }

        chkd++;

        if (chkd == scobj_size)
        {
            chkd = 0;
        }
    }

    return NULL;
}

const char* zSceneGetName(xBase* b)
{
    if (b)
    {
        const char* n = xSTAssetName(b->id);

        if (n)
        {
            return n;
        }
    }

    return "";
}

// No GC symbol: inlined (callback included) into zSceneExit. Body from BfBB.
inline void zSceneForAllBase(xBase* (*func)(xBase*, zScene*, void*), void* data)
{
    zScene* s = globals.sceneCur;

    if (s)
    {
        for (U16 i = 0; i < s->num_base; i++)
        {
            if (!func(s->base[i], s, data))
            {
                break;
            }
        }
    }
}

// No GC symbol: inlined (callback included) into zThrown_Update. Body from BfBB.
inline void zSceneForAllBase(xBase* (*func)(xBase*, zScene*, void*), S32 baseType, void* data)
{
    zScene* s = globals.sceneCur;

    if (s)
    {
        for (U16 i = 0; i < s->num_base; i++)
        {
            if (baseType == s->base[i]->baseType)
            {
                if (!func(s->base[i], s, data))
                {
                    break;
                }
            }
        }
    }
}

// No GC symbol: inlined into zSceneExit through zSceneForAllBase.
static inline xBase* zSceneExitSoundIteratorCB(xBase* b, zScene*, void*)
{
    xSndMgrStopChildren((xEnt*)b);
    return b;
}

void zSceneMemLvlChkCB()
{
}

// No GC symbol: inlined into zSceneSwitch.
inline U32 zSceneLeavingLevel()
{
    char curScene[4] = "";
    char nextScene[4] = "";

    strncpy(curScene, xStrupr(xUtil_idtag2string(globals.sceneCur->sceneID, 0)), 4);
    strncpy(nextScene,
            xStrupr(xUtil_idtag2string(globals.sceneCur->pendingPortal->passet->sceneID, 0)), 4);

    return curScene[0] != nextScene[0];
}

U32 zSceneLeavingWorld()
{
    char curScene[4] = "";
    char nextScene[4] = "";

    strncpy(curScene, xStrupr(xUtil_idtag2string(globals.sceneCur->sceneID, 0)), 4);
    strncpy(nextScene,
            xStrupr(xUtil_idtag2string(globals.sceneCur->pendingPortal->passet->sceneID, 0)), 4);

    char cur = curScene[0];
    char next = nextScene[3];

    if (next == 'H')
    {
        return 1;
    }

    switch (next)
    {
    case 'F':
    case 'L':
    case 'W':
        if (cur == 'F' || cur == 'L' || cur == 'W')
        {
            return 0;
        }
        break;
    case 'C':
    case 'E':
    case 'G':
        if (cur == 'E' || cur == 'C' || cur == 'G')
        {
            return 0;
        }
        break;
    case 'B':
    case 'P':
    case 'S':
        if (cur == 'P' || cur == 'B' || cur == 'S')
        {
            return 0;
        }
        break;
    case 'I':
    case 'O':
    case 'R':
        if (cur == 'I' || cur == 'R' || cur == 'O')
        {
            return 0;
        }
        break;
    }

    return 1;
}

const char* zSceneGetAreaname(U32 sceneID)
{
    S32 world;
    S32 task;
    char name[32];
    const char* text;

    zDispatcher_FindWorldTask(sceneID, world, task);

    if (world > 0 && task > 0)
    {
        sprintf(name, "TASK TXT %d-%d", world, task);
        text = xTextFindString(name, NULL);

        if (text)
        {
            return text;
        }
    }

    return "????";
}

const char* zSceneGetLevelName(U32 sceneID)
{
    S32 world;
    S32 task;
    char name[32];
    const char* text;

    zDispatcher_FindWorldTask(sceneID, world, task);

    if (world > 0)
    {
        sprintf(name, "LEVEL TXT %d", world);
        text = xTextFindString(name, NULL);

        if (text)
        {
            return text;
        }
    }

    return "????";
}

U32 zSceneGetLevelIndex()
{
    S32 world;
    S32 task;

    zDispatcher_FindWorldTask(globals.sceneCur->sceneID, world, task);

    return world > 0 ? world - 1 : 0;
}

// No GC symbol: inlined into zSaveLoad_DoAutoSave and zSaveLoad_SaveGame.
inline U32 zSceneCalcProgress()
{
    U32 progress = globals.player.InvGoofyGoober * 100 / 68;
    return progress > 100 ? 100 : progress;
}

void zScene_UpdateFlyToInterface(F32 dt)
{
}

void zSceneCardCheckStartup_set(S32 needed, S32 available, S32 files)
{
    bytesNeeded = needed;
    availOnDisk = available;
    neededFiles = files;
}

// No GC symbol: inlined into zSceneReset and dead-stripped (a real function in the PS2
// build). Body from BfBB.
void zSceneEnableVisited(zScene* s)
{
    U32 uiNameID;
    char uiName[64];
    char* sceneName;

    sceneName = xUtil_idtag2string(s->sceneID, 0);

    strcpy(uiName, sceneName);

    uiName[1] = ' ';

    strcat(uiName, " VISITED");

    uiNameID = xStrHash(uiName);

    for (S32 i = 0; i < s->num_base; i++)
    {
        if (s->base[i] && s->base[i]->baseType == eBaseTypeUI && uiNameID == s->base[i]->id)
        {
            s->base[i]->baseFlags |= 0x1;
        }
    }
}

void zSceneEnableScreenAdj(U32 enable)
{
    enableScreenAdj = enable;
}

void zSceneSetOldScreenAdj()
{
    oldOffsetx = offsetx;
    oldOffsety = offsety;
}

U32 zScene_ScreenAdjustMode()
{
    return enableScreenAdj;
}

// No GC symbol: inlined into zSceneRenderPostFX and dead-stripped.
inline void zSceneSpawnRandomBubbles()
{
    RwCamera* currentCamera = RwCameraGetCurrentCamera();

    if (!currentCamera || zMenuIsFirstBoot())
    {
        return;
    }

    RwMatrix* mat = RwFrameGetMatrix(RwCameraGetFrame(currentCamera));
    F32 floatRand = xurand();

    if (floatRand > 0.985f)
    {
        xVec3 pos, offset;
        F32 r;

        pos.x = mat->pos.x;
        pos.y = mat->pos.y;
        pos.z = mat->pos.z;

        r = xurand();
        r *= (((S32)(10000 * floatRand) % 2) ? 1 : -1);

        xVec3ScaleC(&offset, (xVec3*)&mat->right, r, r, r);
        xVec3Add(&pos, &pos, &offset);
        xVec3ScaleC(&offset, (xVec3*)&mat->up, scale.y, scale.y, scale.y);
        xVec3Add(&pos, &pos, &offset);
        xVec3ScaleC(&offset, (xVec3*)&mat->at, scale.z, scale.z, scale.z);
        xVec3Add(&pos, &pos, &offset);

        zFX_SpawnBubbleMenuTrail(&pos, 1, NULL, NULL);
    }
}

bool zSceneLoadPackFile(U32 id, S32 options, bool noLocalized, const char* filename)
{
    S32 versionNumber;

    if (filename)
    {
        versionNumber = xSTPreLoadScene(id, NULL, options, 4, filename);
    }
    else
    {
        versionNumber = xSTPreLoadScene(id, NULL, options, 0, NULL);
    }

    if (!versionNumber && (options & 3))
    {
#ifdef PLATFORM_PC
        // PLATFORM_PC: a HIP or HOP the game cannot do without (only the localized pack, 3, is
        // optional). Retail carries on and crashes in zTransient::scene_enter; a disc always
        // has the file, a copied file set may not.
        if ((options & 3) != 3)
        {
            iFileMissingPackage(id, options, filename);
        }
#endif
        return false;
    }

    xSTQueueSceneAssets(id, options);

    while (xSTLoadStep(id) < 1.0f)
    {
    }

    xSTDisconnect(id, options);

    if (!noLocalized && (options & 3) == 1)
    {
        S32 versionNumber;

        if (filename)
        {
            char buffer[256];
            char* token = strtok((char*)filename, ".");

            if (token)
            {
                sprintf(buffer, "%s%s.HIP", token, xSTGetLocalizationCode());
            }
            else
            {
                sprintf(buffer, "%s%s.HIP", filename, xSTGetLocalizationCode());
            }

            versionNumber = xSTPreLoadScene(id, NULL, 3, 4, buffer);
        }
        else
        {
            versionNumber = xSTPreLoadScene(id, NULL, 3, 0, NULL);
        }

        if (versionNumber >= 0xA000F)
        {
            xSTQueueSceneAssets(id, 3);

            while (xSTLoadStep(id) < 1.0f)
            {
            }

            xSTDisconnect(id, 3);
        }
    }

    return true;
}
