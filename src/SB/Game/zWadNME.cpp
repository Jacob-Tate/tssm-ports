#include "zWadNME.h"

// Original source order: the reverse of retail's .text sections, which run in reverse alphabetical
// order except for zNMETypeTongueSpin.cpp, zNMEParFXTypes.cpp and zNMERumble.cpp.
// zNMECommon.cpp is the DWARF's zNMETypeCommon.cpp.
#include "zNMEArena.cpp"
#include "zNMEAsset.cpp"
#include "zNMECentral.cpp"
#include "zNMEConfig.cpp"
#include "zNMEGoals.cpp"
#include "zNMEGoalStd.cpp"
#include "zNMEHazard.cpp"
#include "zNMEHazardTypes.cpp"
#include "zNMEHazardUVAnim.cpp"
#include "zNMEHealthMeter.cpp"
#include "zNMEMessenger.cpp"
#include "zNMEMotion.cpp"
#include "zNMENPCWrapper.cpp"
#include "zNMEParFX.cpp"
#include "zNMEParFXTypes.cpp"
#include "zNMEParFXTypeDummy.cpp"
#include "zNMEParFXTypeExhaust.cpp"
#include "zNMEParFXTypeFogBreath.cpp"
#include "zNMEParFXTypeOrient.cpp"
#include "zNMEParFXTypePlasma.cpp"
#include "zNMEParFXTypePopGas.cpp"
#include "zNMEParFXTypePuffRing.cpp"
#include "zNMEParFXTypeSparky.cpp"

// Enemy sound tables. Owning file unknown; retail's data puts them between the zNMEParFXType*.cpp
// and zNMEShrapnel.cpp data, so they are defined here. gzSoundTableList is indexed by en_npctyp.
zNMESoundTable gzSoundTable_Default = {
    {},
    { "DefaultSoundGroup", "", "", "", "", "", "", "", "", "" },
    {},
    { -1.0f },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

zNMESoundTable gzSoundTable_Tiki_Wood = {
    {},
    { "", "", "", "", "TikiDeathGroup", "", "", "TikiStackGroup", "", "" },
    {},
    { -1.0f },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

zNMESoundTable gzSoundTable_Tiki_Hover = {
    {},
    { "", "", "", "", "TikiHoverDeathGroup", "", "", "TikiHoverStackGroup", "", "" },
    {},
    { -1.0f },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

zNMESoundTable gzSoundTable_Tiki_Shrink = {
    {},
    { "", "", "", "", "TikiShrinkDeathGroup", "", "", "TikiShrinkStackGroup",
      "TikiShrinkShrinkGroup", "TikiShrinkGrowGroup" },
    {},
    { -1.0f },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

zNMESoundTable gzSoundTable_Tiki_Explode = {
    {},
    { "", "", "", "", "TikiExplodeDeathGroup", "", "", "TikiExplodeStackGroup",
      "TikiExplodeFuseGroup", "" },
    {},
    { -1.0f },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0x8000, 0 },
};

zNMESoundTable gzSoundTable_Tiki_Steel = {
    {},
    { "", "", "", "", "TikiSteelDeathGroup", "", "", "TikiSteelStackGroup", "", "" },
    {},
    { -1.0f },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

zNMESoundTable gzSoundTable_Critter = {
    {},
    { "", "", "JellyAttackGroup", "", "", "", "", "", "", "" },
    {},
    { -1.0f },
    { 0, 0, 0, 0, 0x2000, 0, 0, 0x8000, 0, 0 },
};

zNMESoundTable gzSoundTable_Turret = {
    {},
    { "", "", "", "", "", "TurretHitGroup", "", "", "", "" },
    {},
    { -1.0f },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

zNMESoundTable gzSoundTable_Buckotron = {
    {},
    { "BotIdleGroup", "", "", "", "BotDefeatShrapnelGroup", "BotDamageGroup", "", "",
      "BotDefeatStartGroup", "" },
    {},
    { -1.0f },
    { 0x8000, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

static zNMESoundTable gzSoundTable_Spinner = {
    {},
    { "", "", "", "", "", "SpinnerHitGroup", "SpinnerStunRecoverGroup", "", "", "" },
    {},
    { -1.0f },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

static zNMESoundTable gzSoundTable_Slammer = {
    {},
    { "", "", "", "", "", "SlammerHitGroup", "SlammerStunRecoverGroup", "", "", "" },
    {},
    { -1.0f },
    { 0, 0, 0x8000, 0, 0, 0, 0, 0, 0, 0 },
};

static zNMESoundTable gzSoundTable_Mervyn = {
    {},
    { "MervynIdleGroup", "", "MervynAttackBombGroup", "MervynAlertGroup", "", "MervynHitGroup", "",
      "MervynGunReadyGroup", "MervynAttackBeamGroup", "MervynAttackZapGroup" },
    {},
    { -1.0f },
    { 0x8000, 0, 0, 0x8000, 0, 0, 0, 0, 0x8000, 0 },
};

static zNMESoundTable gzSoundTable_Zapper = {
    {},
    { "ZapperIdleGroup", "", "", "ZapperAlertGroup", "", "ZapperHitGroup", "",
      "ZapperGunReadyGroup", "ZapperAttackBeamGroup", "ZapperAttackZapGroup" },
    {},
    { -1.0f },
    { 0, 0, 0x8000, 0, 0, 0, 0, 0, 0, 0 },
};

static zNMESoundTable gzSoundTable_Fogger = {
    {},
    { "", "", "FoggerAttackGroup", "", "", "FoggerHitGroup", "FoggerStunRecoverGroup", "", "", "" },
    {},
    { -1.0f },
    { 0, 0, 0, 0, 0x2000, 0, 0, 0, 0, 0 },
};

static zNMESoundTable gzSoundTable_Flinger = {
    {},
    { "", "", "FlingerAttackGroup", "", "", "FlingerHitGroup", "FlingerStunRecoverGroup", "", "",
      "" },
    {},
    { -1.0f },
    { 0, 0, 0, 0, 0x2000, 0, 0, 0, 0, 0 },
};

static zNMESoundTable gzSoundTable_Popper = {
    {},
    { "", "", "PopperAttackGroup", "", "", "PopperHitGroup", "", "", "", "" },
    {},
    { -1.0f },
    { 0, 0, 0, 0, 0x2000, 0, 0, 0, 0, 0 },
};

static zNMESoundTable gzSoundTable_DennisV1 = {
    {},
    { "", "", "", "", "", "", "", "", "", "DennisGasGroup" },
    {},
    { -1.0f },
    { 0, 0, 0, 0, 0x2000, 0, 0, 0, 0, 0 },
};

static zNMESoundTable gzSoundTable_DennisV2 = {
    {},
    { "", "", "", "", "", "", "", "", "", "DennisHitPlayerGroup" },
    {},
    { -1.0f },
    { 0, 0, 0, 0, 0x2000, 0, 0, 0, 0, 0 },
};

zNMESoundTable* gzSoundTableList[58] = {
    &gzSoundTable_Default,    &gzSoundTable_Tiki_Wood,   &gzSoundTable_Tiki_Wood,
    &gzSoundTable_Tiki_Hover, &gzSoundTable_Tiki_Shrink, &gzSoundTable_Tiki_Explode,
    &gzSoundTable_Tiki_Steel, &gzSoundTable_Critter,     &gzSoundTable_Critter,
    &gzSoundTable_Critter,    &gzSoundTable_Critter,     &gzSoundTable_Turret,
    &gzSoundTable_Turret,     &gzSoundTable_Turret,      &gzSoundTable_Turret,
    &gzSoundTable_Turret,     &gzSoundTable_Turret,      &gzSoundTable_Turret,
    &gzSoundTable_Turret,     &gzSoundTable_Turret,      &gzSoundTable_Default,
    &gzSoundTable_Fogger,     &gzSoundTable_Fogger,      &gzSoundTable_Fogger,
    &gzSoundTable_Slammer,    &gzSoundTable_Slammer,     &gzSoundTable_Slammer,
    &gzSoundTable_Flinger,    &gzSoundTable_Flinger,     &gzSoundTable_Flinger,
    &gzSoundTable_Spinner,    &gzSoundTable_Spinner,     &gzSoundTable_Spinner,
    &gzSoundTable_Popper,     &gzSoundTable_Popper,      &gzSoundTable_Popper,
    &gzSoundTable_Zapper,     &gzSoundTable_Zapper,      &gzSoundTable_Zapper,
    &gzSoundTable_Mervyn,     &gzSoundTable_Mervyn,      &gzSoundTable_Mervyn,
    &gzSoundTable_Buckotron,  &gzSoundTable_Buckotron,   &gzSoundTable_Buckotron,
    &gzSoundTable_Buckotron,  &gzSoundTable_Buckotron,   &gzSoundTable_Buckotron,
    &gzSoundTable_Buckotron,  &gzSoundTable_Default,     &gzSoundTable_DennisV1,
    &gzSoundTable_DennisV2,   &gzSoundTable_Default,     &gzSoundTable_Default,
    &gzSoundTable_Default,    &gzSoundTable_Default,     &gzSoundTable_Default,
    &gzSoundTable_Default
};

#include "zNMEShrapnel.cpp"
#include "zNMESpawner.cpp"
#include "zNMESupplement.cpp"
#include "zNMESupport.cpp"
#include "zNMETypeBossDennis.cpp"
#include "zNMETypeBossDenToo.cpp"
#include "zNMETypeBossFrogfish.cpp"
#include "zNMETypeBossNeptune.cpp"
#include "zNMETypeBuckOTron.cpp"
#include "zNMECommon.cpp"
#include "zNMETypeCritBasic.cpp"
#include "zNMETypeCritJelly.cpp"
#include "zNMETypeCritter.cpp"
#include "zNMETypeFrdMindy.cpp"
#include "zNMETypeFriendly.cpp"
#include "zNMETypeSBBat.cpp"
#include "zNMETypeStandard.cpp"
#include "zNMETypeStdFlinger.cpp"
#include "zNMETypeStdFogger.cpp"
#include "zNMETypeStdMervyn.cpp"
#include "zNMETypeStdPopper.cpp"
#include "zNMETypeStdSlammer.cpp"
#include "zNMETypeStdSpinner.cpp"
#include "zNMETypeStdZapper.cpp"
#include "zNMETypeTiki.cpp"
#include "zNMETypeTurBarrel.cpp"
#include "zNMETypeTurPopup.cpp"
#include "zNMETypeTurret.cpp"
#include "zNMETypeTurSpiral.cpp"
#include "zNMETypeTurTurner.cpp"
#include "zNMETypeTongueSpin.cpp"
#include "zNMERumble.cpp"
#include "zNMEZapGun.cpp"
