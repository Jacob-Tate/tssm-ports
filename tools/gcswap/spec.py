"""What each TSSM asset type is made of, for gen.py.

A type is a list of steps, applied from the start of the asset:

    "xEntAsset"                 a struct, swapped by its layout (layouts.py)
    ("array", elem, count)      `count` copies of `elem` -- a struct name,
                                "u16" or "u32". `count` is a number, a
                                "Struct.member" read from a struct placed
                                earlier at a fixed offset, or "rest" for as
                                many as fit before the links.
    ("words",)                  every aligned u32 to the end of the body
    ("bytes",)                  the rest of the body is bytes
    ("tagged", "Struct.count", CASES)
                                a list whose entries start with a u32 type
                                that picks the entry's struct
    ("select", "Struct.member", {value: struct})
                                one struct, picked by a member of a struct
                                placed earlier; none for another value
    ("custom", "fn")            a hand-written function in iGCSwap.cpp,
                                handed the asset, the end of its body (before
                                the links, which are already swapped) and
                                where the steps before it stopped

An asset derived from xBaseAsset ends in `linkCount` xLinkAssets, and the
generated code swaps those for every type not in NOBASE or LINKS_FIRST.

EITHER names fields the GameCube packer swapped for some assets and not for
others. Each is decided per asset by which byte order gives a plausible value.

AS_IS names fields left as they are on the disc.

UNIONS says how to swap a union whose members disagree: keyed by the struct
the union is declared in and its offset there, or by the struct being swapped
and the union's offset in it. Either a selector member (of that struct, before
the union) and a struct (or "u32", "bytes", or a list of (offset, width)) per
value, or "fixed", a list of (offset, width) to swap whatever the value.

DYNA is a dictionary instead: xDynAsset's `type` is xStrHash of one of these
names, and each maps to steps that start at the beginning of the asset (the
xDynAsset fields are swapped once, before the dispatch).

The answer key for all of it is the Xbox asset of the same id: tssm_gcswaptest
(src/SB/Core/pc/tests/gcswaptest.cpp) runs the generated table over a GameCube
disc and compares each result with its Xbox twin.

After the method of the BfBB PC port's tools/gcswap (CC0); TSSM's types,
structs and DYNA subtypes are its own.
"""

HEADERS = [
    "xEnt.h", "xDynAsset.h", "xLinkAsset.h", "zEntSimpleObj.h", "zPlatform.h", "xEntMotion.h",
    "zEntButton.h", "zEntPickup.h", "zEntTrigger.h", "xEntBoulder.h", "xTimer.h", "zScript.h",
    "xCamera.h", "xCounter.h", "zConditional.h", "xParEmitter.h", "zPortal.h", "zEGenerator.h",
    "zSurface.h", "xCutsceneMgr.h", "xFog.h", "iEnv.h", "xEnv.h", "xGroup.h", "xMovePoint.h",
    "xParSys.h", "zPickupTable.h", "zDiscoFloor.h", "xModel.h", "xTextAsset.h", "zAssetTypes.h",
    "xSnd.h", "zUI.h", "zDestructible.h", "xLightKit.h", "zLOD.h", "zCollGeom.h", "xCM.h",
    "xCutscene.h", "xJSP.h", "xMarkerAsset.h", "xParCmd.h", "zAnimList.h", "zShrapnel.h",
    "zEntDestructObj.h", "xSplineAsset.h", "zScene.h", "iAnimSKB.h", "xClumpColl.h",
    "zRing.h", "zRaceTimer.h", "zRingControl.h", "zSmoke.h", "zVent.h", "zVentType.h",
    "zParticleGenerator.h", "zJSPExtraData.h", "zCameraFly.h", "zCameraTweak.h",
    "zEntTeleportBox.h", "zBusStop.h", "zReference.h", "zSpotlight.h", "zLightning.h",
    "zFlameEmitter.h", "zTalkBox.h", "zTaskBox.h", "zTextBox.h", "zUIImage.h", "zUIText.h",
    "zUIBox.h", "zUIModel.h", "zUIController.h", "zNMEAsset.h", "zNMECommon.h", "xScreenFade.h",
    "xRumbleEmitter.h", "xHudModel.h", "xHudText.h", "xHudMeter.h", "zEntPlayerBungeeState.h",
    "zNMETypeStandard.h", "zNMETypeTurret.h", "zNMETypeBuckOTron.h", "zNMETypeBossDennis.h",
    "zNMETypeFrdMindy.h", "zNMETypeBossFrogfish.h", "zNMETypeBossNeptune.h",
    "zNMETypeCritter.h", "xJaw.h", "xMorph.h", "zCamera.h", "zParticleSystemWaterfall.h",
]

EXTRA_SOURCE = ""

# Types that do not start with an xBaseAsset, so carry no links.
NOBASE = {"MRKR", "TEXT", "ALST", "PIPT", "LODT", "COLL", "LKIT", "ATBL", "JAW ", "SHRP", "MAPR",
          "CRDT", "PICK", "CTOC", "MINF", "ANIM", "DEST", "CSN ", "JSP "}

# Base types whose links are not at the end; their custom step swaps them.
LINKS_FIRST = {"DSCO", "PLYR"}

# Read as they are: RenderWare streams (librw reads the GameCube's native
# textures and geometry), the sound data and its table (the sound layer reads
# the GameCube's layout), and the flythrough keys, which the GameCube disc
# ships little-endian already (zCamera.cpp swaps them only on the console).
AS_IS_TYPES = {"MODL", "RWTX", "TEXS", "SND ", "SNDS", "SNDI", "CSSS", "RAW ", "FLY "}

TYPES = {
    "SIMP": ["xEntAsset", "xSimpleObjAsset"],
    "PLAT": ["xEntAsset", "xPlatformAsset", "xEntMotionAsset"],
    "BUTN": ["xEntAsset", "zEntButtonAsset", "xEntMotionAsset"],
    "PKUP": ["xEntAsset", "xEntPickupAsset"],
    "TRIG": ["xEntAsset", "xTriggerAsset"],
    "BOUL": ["xEntAsset", "xEntBoulderAsset"],
    "EGEN": ["zEGenAsset"],
    "PLYR": [("custom", "iGCSwapPlayer")],
    "TIMR": ["xTimerAsset"],
    "SCRP": ["xScriptAsset", ("array", "xScriptEventAsset", "xScriptAsset.eventCount")],
    "CAM ": ["xCamAsset"],
    "CNTR": ["xCounterAsset"],
    "COND": ["zCondAsset"],
    "DPAT": ["xBaseAsset"],
    "PARE": ["xParEmitterAsset"],
    "PARP": ["xParEmitterPropsAsset"],
    "PORT": ["xPortalAsset"],
    "SURF": ["zSurfAssetIN"],
    "CSNM": ["xCutsceneMgrAsset"],
    "FOG ": ["xFogAsset"],
    "ENV ": ["xEnvAsset"],
    "GRUP": ["xGroupAsset", ("array", "u32", "xGroupAsset.itemCount")],
    "MVPT": ["xMovePointAsset", ("array", "u32", "xMovePointAsset.numPoints")],
    "PARS": ["xParSysAsset", ("tagged", "xParSysAsset.cmdCount", "PARCMDS")],
    "PICK": ["zAssetPickupTable", ("array", "zAssetPickup", "zAssetPickupTable.Count")],
    "DSCO": [("custom", "iGCSwapDisco")],
    "MINF": [("custom", "iGCSwapModelInfo")],
    "TEXT": ["xTextAsset"],
    "SDFX": ["xSoundFXAsset"],
    "SGRP": ["xSndGroupHeader", ("array", "xSndGroupInfo", "xSndGroupHeader.uInfoCount")],
    "UIM ": ["zUIMotionAsset", ("tagged", "zUIMotionAsset.cmdCount", "UIMCMDS")],
    # zReactiveAnimation.cpp's asset_type: xTableAsset (version, numRows), then
    # rows of ids and floats.
    "RANM": ["xBaseAsset", ("words",)],
    # x::NURBS, then its control points and knots.
    "SPLN": ["x::NURBS", ("words",)],
    "SHRP": ["zShrapnelAsset", ("tagged", "zShrapnelAsset.fassetCount", "FRAGS")],

    # Every field a u32 or a float.
    "MRKR": [("words",)],
    "ALST": [("words",)],
    "COLL": [("words",)],
    "LKIT": [("words",)],
    "MAPR": [("words",)],
    "LODT": [("words",)],
    # zScene.cpp's xModelPipeAssetHeader: a count, then xModelPipeTables.
    "PIPT": [("array", "u32", 1), ("array", "xModelPipeTable", "rest")],
    # The raw animation ids, the files -- whose NumAnims pair is 16-bit -- and
    # the states, then the files' index arrays (words) and the states' effects.
    "ATBL": ["xAnimAssetTable", ("array", "u32", "xAnimAssetTable.NumRaw"),
             ("array", "xAnimAssetFile", "xAnimAssetTable.NumFiles"),
             ("array", "xAnimAssetState", "xAnimAssetTable.NumStates"),
             ("custom", "iGCSwapAnimTableRest")],

    "DEST": [("custom", "iGCSwapDestructible")],
    "CRDT": [("custom", "iGCSwapCredits")],
    "CTOC": [("custom", "iGCSwapCutsceneToc")],
    "CSN ": [("custom", "iGCSwapCutscene")],
    "ANIM": [("custom", "iGCSwapAnim")],
    "JAW ": [("custom", "iGCSwapJaw")],
    "JSP ": [("custom", "iGCSwapJspInfo")],
}

EITHER = {
    "xEnvAsset.loldHeight": "float",
    "xParSysAsset.parFlags": "int",
    "zFragParticleAsset.flags": "int",
    "zSurfAssetBase.damage_timer": "float",
    "zSurfAssetBase.damage_bounce": "float",
}

# A colour the decomp declares as a U32 that the packers wrote as its four
# bytes: the Xbox disc has the GameCube's bytes in every ui widget.
AS_IS = {"zUIAsset.color",
         # The one SPLN (gg02's curve) has its xBaseAsset header little-endian
         # on the GameCube disc; the rest of it is big-endian.
         "x::NURBS.id", "x::NURBS.baseFlags"}

# The structs the hand-written rules call.
EXTRA_STRUCTS = [
    "xEntAsset", "xModelAssetInfo", "xModelAssetInst", "z_disco_floor_asset",
    "xCutsceneInfo", "xCutsceneData", "xCutsceneTime", "xCutsceneBreak", "xCutsceneAnimHdr",
    "xCutsceneMphFrame", "xCutsceneMphRun", "xMorphTargetFile", "zFlyKey",
    "xClumpCollBSPBranchNode", "xClumpCollBSPTriangle", "xJSPHeader", "xJSPNodeInfo",
    "xJSPNodeTree", "xJSPNodeTreeBranch", "xJSPNodeTreeLeaf", "xJSPExtraInfo",
    "xDestructibleAssetState", "xAnimAssetEffect",
]

# Structs the hand-written rules call in their bounded form too.
EXTRA_BOUNDED = ["xDestructibleAsset"]

# The command types xParCmd.cpp registers, by XPARCMD_TYPE_*, and the ones it
# has a struct for but does not register.
PARCMDS = {
    0: "xParCmdMove", 1: "xParCmdMove", 2: "xParCmdAccelerate", 3: "xParCmdAsset",
    4: "xParCmdJet", 5: "xParCmdKillOld", 6: "xParCmdKillSlow", 7: "xParCmdFollow",
    8: "xParCmdOrbitPoint", 9: "xParCmdOrbitLine", 10: "xParCmdMoveRandomPar",
    11: "xParCmdScale3rdPolyReg", 12: "xParCmdTex", 13: "xParCmdTexAnim",
    14: "xParCmdPlayerCollision", 15: "xParCmdRandomVelocityPar", 16: "xParCmdCustom",
    17: "xParCmdKillDistance", 18: "xParCmdAge", 19: "xParCmdAlpha3rdPolyReg",
    20: "xParCmdApplyWind", 21: "xParCmdRotPar", 22: "xParCmdApplyCamMat",
    23: "xParCmdRotateAround", 24: "xParCmdSmokeAlpha", 25: "xParCmdScale",
    26: "xParCmdClipVolumes", 27: "xParCmdAnimalMagentism", 28: "xParCmdDamagePlayer",
    29: "xParCmdCollideFall", 30: "xParCmdShaperData", 31: "xParCmdAlphaInOutData",
    32: "xParCmdSizeInOutData", 33: "xParCmdDampenData", 34: "xParCmdCollideFallSticky",
}

# zUI.cpp's motion commands, by type.
UIMCMDS = {
    0: "zUIMotionCmdMove", 1: "zUIMotionCmdScale", 2: "zUIMotionCmdRotate",
    3: "zUIMotionCmdOpacity", 4: "zUIMotionCmdAbsoluteScale", 5: "zUIMotionCmdBrightness",
    6: "zUIMotionCmdColor", 7: "zUIMotionCmdUVScroll",
}

# zShrapnel.h's zFragType, and the asset each one reads.
FRAGS = {
    2: "zFragShrapnelAsset", 3: "zFragParticleAsset", 4: "zFragProjectileAsset",
    5: "zFragLightningAsset", 6: "zFragSoundAsset", 7: "zFragShockwaveAsset",
    8: "zFragExplosionAsset", 9: "zFragDistortionAsset", 10: "zFragFireAsset",
}

UNIONS = {
    # XENTMOTION_TYPE_*.
    ("xEntMotionAsset", 0x4): {"selector": "type", "cases": {
        0: "xEntMotionERData", 1: "xEntMotionOrbitData", 2: "xEntMotionSplineData",
        3: "xEntMotionMPData", 4: "xEntMotionMechData", 5: "xEntMotionPenData"}},
    # en_xParEmitterEmitTypes. Every shape but the two entity ones is floats
    # and ids, so "u32" swaps the whole union a word at a time.
    ("xParEmitterAsset", 0x10): {"selector": "emit_type", "cases": dict(
        [(k, "u32") for k in range(15)] + [(15, "xPEEntBone"), (16, "xPEEntBound")])},
    # TSSM's JSPINFO is version 5: xJSP_MultiStreamRead reads v.rawIdx, a u32
    # index into the strip vectors, and puts a pointer there.
    ("xClumpCollBSPTriangle", 0x0): {"fixed": [(0, 4)]},
    # zParticleGenerator.cpp get_attach_entity: 0 fixed, 1 entity, 2 entity tag.
    ("zParticleAsset", 0x38): {"selector": "attach_type", "cases": {
        0: "bytes", 1: "attach_entity_data", 2: "attach_entity_tag_data"}},
    # point, sphere, circle, line, model.
    ("zParticleAsset", 0x5C): {"selector": "volume_type", "cases": {
        0: "bytes", 1: "u32", 2: "u32", 3: "volume_line_data", 4: "volume_model_data"}},
}

DYNA_NAMES = {
    0xFABDB3B3: "zSceneParameters",
}

DYNA = {
    "Enemy:SB:SupplyCrate": ["zNMEAsset", ("words",)],  # zNMEAsset.cpp's zNMEAssetCrate
    "Enemy:SB:Standard": ["zNMEAssetStandard"],
    "Enemy:SB:BucketOTron": ["zNMEAssetBucketOTron"],
    "Enemy:SB:CastNCrew": ["zNMEAsset"],
    "Enemy:SB:Critter": ["zNMEAssetCritter"],
    "Enemy:SB:Dennis": ["zNMEAssetDennis"],
    "Enemy:SB:FrogFish": ["zNMEAssetFrogFish"],
    "Enemy:SB:Mindy": ["zNMEAssetMindy"],
    "Enemy:SB:Neptune": ["zNMEAssetNeptune"],
    "Enemy:SB:Turret": ["zNMEAssetTurret"],
    "game_object:Ring": ["zRingType"],
    "game_object:RingControl": ["zRingControlType", ("words",)],
    "game_object:RaceTimer": ["zRaceTimerType"],
    "game_object:Vent": ["zVent::asset_type"],
    "game_object:VentType": ["zVentType::asset_type"],
    "game_object:talk_box": ["ztalkbox::asset_type"],
    "game_object:task_box": ["ztaskbox::asset_type"],
    "game_object:text_box": ["ztextbox::asset_type"],
    "game_object:flame_emitter": ["zFlameEmitter::asset_type"],
    "game_object:BoulderGenerator": ["xBoulderGeneratorAsset"],
    "game_object:Camera_Tweak": ["CameraTweak_asset"],
    "game_object:Flythrough": ["CameraFly_asset"],
    "game_object:Teleport": ["teleport_asset"],
    "game_object:BusStop": ["busstop_asset"],
    "game_object:bungee_drop": ["bungee_state::drop_asset"],
    "game_object:bungee_hook": ["bungee_state::hook_asset"],
    # zCam.cpp's pointer_asset: a position and three angles.
    "pointer": [("words",)],
    "ui:image": ["zUIImageAsset"],
    "ui:text": ["zUITextAsset"],
    "ui:model": ["zUIModelAsset"],
    "ui:box": ["zUIBoxAsset"],
    "ui:controller": ["zUIControllerAsset"],
    "hud:model": ["xhud::model_asset"],
    "hud:text": ["xhud::text_asset"],
    "hud:meter:font": ["xhud::font_meter_asset"],
    "hud:meter:unit": ["xhud::unit_meter_asset"],
    "effect:ScreenFade": ["xScreenFade::asset_type"],
    "effect:Rumble": ["xRumble::effectAsset"],
    "effect:Rumble Spherical Emitter": ["xRumble::sphericalEmitterAsset"],
    "effect:Lightning": ["zLightningAsset"],
    "effect:spotlight": ["zSpotlightAsset"],
    "effect:smoke_emitter": ["zSmokeEmitter::asset_type"],
    # zParticleSystems.cpp's systems: each reads its asset_data after the
    # zParticleAsset.
    "Effect:particle_generator": ["zParticleAsset", ("select", "zParticleAsset.system_type", {
        1: "zParticleSystemWaterfall::asset_data", 2: "zParticleSystemWaterfallMist::asset_data",
        3: "zParticleSystemWaterfallSplash::asset_data"}), ("words",)],
    "JSP Extra Data": ["zJSPExtraDataAsset"],
    "logic:reference": ["zReferenceAsset"],
    # The scene parameters, then the idle extras' animation ids.
    0xFABDB3B3: ["zSceneParameters", ("words",)],
}
