#include "zMain.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <types.h>

#include "iCamera.h"
#include "iFile.h"
#include "iMath.h"
#include "iSnd.h"
#include "iSystem.h"
#include "iTime.h"
#include "iTRC.h"
#include "xBehaveMgr.h"
#include "xColor.h"
#include "xDebug.h"
#include "xFont.h"
#include "xIni.h"
#include "xMemMgr.h"
#include "xOutputMgr.h"
#include "xPad.h"
#include "xRegionSupport.h"
#include "xScrFx.h"
#include "xParMgr.h"
#include "xShadowSimple.h"
#include "xString.h"
#include "xTRC.h"
#include "xsavegame.h"
#include "xserializer.h"
#include "xstransvc.h"
#include "xutil.h"
#include "zAssetTypes.h"
#include "zConfig.h"
#include "zDispatcher.h"
#include "zExplosion.h"
#include "zGame.h"
#include "zGameState.h"
#include "zGlobals.h"
#include "zHud.h"
#include "zMenu.h"
#include "zMenuSB.h"
#include "zNPCMgr.h"
#include "zRewards.h"
#include "zScene.h"
#include "zTalkBox.h"
#include "zUICustom.h"
#include "zVar.h"

// Defined in other translation units, but not yet declared in any header.
char* xTextFindString(const char* name, U32* size);
void RenderText(const char* text, bool enabled);
void xrand_RandomInit();
void iSystemInit(U32 options);
void iSystemInitGraphics();
void iSystemExit();
void iFMVSystemInit();
void var_init();
void zFX_GameInit();
void SB04FMVPauseSoundCB(bool pause);
void (*xFMVSetPauseSoundCB(void (*cb)(bool)))(bool);
void xScrFxInit();
void xFXHighDynamicRangeInit();
void xRenderBufferInit();
void zParCmdInit();
void zCameraTweakGlobal_Init();
void zShrapnel_GameInit();
void zCineFXStartup();
void zCineFXShutdown();
void zNMECentral_Startup();
void zNMECentral_Shutdown();
void xAntennaInit();
void zRewardsMgrStartup();
void xModelInitOnce();
void zPickupTableInit();
void xTRCInitMessages();
void zMenuInit(U32 theSceneID);
void zMenuSetup();
U32 zMenuLoop();
void zMenuExit();
void zGameInit(U32 theSceneID);
void zGameSetup();
void zGameLoop();
void zGameExit();

// Camera tuning globals (defined in xCamera.cpp / zCamera.cpp).
extern F32 xcam_collis_radius;
extern F32 xcam_collis_stiffness;
extern F32 zcam_pad_pyaw_scale;
extern F32 zcam_pad_pitch_scale;
extern F32 zcam_near_d;
extern F32 zcam_near_h;
extern F32 zcam_near_pitch;
extern F32 zcam_far_d;
extern F32 zcam_far_h;
extern F32 zcam_far_pitch;
extern F32 zcam_above_d;
extern F32 zcam_above_h;
extern F32 zcam_above_pitch;
extern F32 zcam_below_d;
extern F32 zcam_below_h;
extern F32 zcam_below_pitch;
extern F32 zcam_highbounce_d;
extern F32 zcam_highbounce_h;
extern F32 zcam_highbounce_pitch;
extern F32 zcam_wall_d;
extern F32 zcam_wall_h;
extern F32 zcam_wall_pitch;
extern F32 zcam_overrot_min;
extern F32 zcam_overrot_mid;
extern F32 zcam_overrot_max;
extern F32 zcam_overrot_rate;
extern F32 zcam_overrot_tstart;
extern F32 zcam_overrot_tend;
extern F32 zcam_overrot_velmin;
extern F32 zcam_overrot_velmax;
extern F32 zcam_overrot_tmanual;

static void zMainReadINI(const char* iniName);
static void zMainParseINIGlobals(xIniFile* ini);
static void zMainOutputMgrSetup();
static void zMainMemLvlChkCB();
static void zMainLoop();

// Unused here: the PS2 DWARF lists this memory-card static (also defined in isavegame.cpp) in
// zMain's unit ahead of zMain's own globals, so it reached zMain through a GC save-game header;
// its "GGVE" initializer is the first string of the target @stringBase0.
static const char* ISG_GC_GAMENAME = "GGVE";

F32 VBLANKS_PER_SEC = 60.0f;
U32 FB_XRES = 640;
U32 FB_YRES = 480;
U32 FB_DEPTH = 24;
zGlobals globals;
xGlobals* xglobals = &globals;
static S32 sShowMenuOnBoot = 1;
F32 gSkipTimeCutscene = 1.0f;
F32 gSkipTimeFlythrough = 1.0f;
F32 SECS_PER_VBLANK = 1.0f / VBLANKS_PER_SEC;
U32 SCENE_ID_MNU_INGAME;
U32 SCENE_ID_MNU_START;
_tagGameVidMode gVidMode;
F32 ShortbreadCookieJar;

static st_SERIAL_PERCID_SIZE g_xser_sizeinfo[] = { { 'PLYR', 0x148 }, { 0, 0 } };

// No symbol in TSSM: inlined into main (the PS2 line table attributes the whole
// sequence to a single line of main). BfBB has it as a separate function.
static inline void zMainInitGlobals()
{
    memset(&globals, 0, sizeof(globals));
    globals.sceneFirst = 1;
    iTimeDiffSec(iTimeGet());
    iTimeGet();
}

#ifdef __MWERKS__
void main(S32 argc, char** argv)
#else
int main(S32 argc, char** argv)
#endif
{
    // Not in the DWARF order (i, ini): declaring i first changes the generated code.
    char* ini;

    zMainInitGlobals();
    globals.firstStartPressed = TRUE;
    ini = "SB04.INI";

    for (S32 i = 1; i < argc; i++)
    {
        if (xMemicmp(argv[i], "-launcher=", 10) == 0)
        {
            globals.fromLauncher = TRUE;
            strcpy(globals.fromLauncherUser, argv[i] + 10);
            printf("Launcher detected; user=%s\n", argv[i] + 10);
        }
        else if (xMemicmp(argv[i], "-ini=", 5) == 0)
        {
            ini = argv[i] + 5;
        }
    }

    xrand_RandomInit();
    iSystemInit(4);
    zMainOutputMgrSetup();
    iFMVSystemInit();
    xMemRegisterBaseNotifyFunc(zMainMemLvlChkCB);
    var_init();
    xTRCInit();
    zMainReadINI(ini);
    zFX_GameInit();
    xFMVSetPauseSoundCB(SB04FMVPauseSoundCB);
    iSystemInitGraphics();
    xUtilStartup();
    xSerialStartup(0x80, g_xser_sizeinfo);
    zDispatcher_Startup();
    zAssetStartup();
    xMemPushBase();
    zSceneLoadPackFile('FONT', 1, false, NULL);
    xfont::init();
    zMainFirstScreen(1);

    globals.pad0 = xPadEnable(globals.currentActivePad);
    globals.pad1 = NULL;
    xPadRumbleEnable(globals.currentActivePad, globals.option_vibration);

    xScrFxInit();
    xFXHighDynamicRangeInit();
    zExplosion::startup();
    xRenderBufferInit();
    xParMgrInit();
    zParCmdInit();
    zCameraTweakGlobal_Init();
    xSGStartup();
    xDebugTimestampScreen();
    zHud::hud_system::instance().startup();

    if (sShowMenuOnBoot)
    {
        zMainMemCardSpaceQuery();
    }

    zShrapnel_GameInit();
    zCineFXStartup();
    xBehaveMgr_Startup();
    zNPCMgr_Startup();
    zNMECentral_Startup();
    xAntennaInit();
    zRewardsMgrStartup();
    xModelInitOnce();

    zMainLoop();

    zHud::hud_system::instance().shutdown();
    zNMECentral_Shutdown();
    zNPCMgr_Shutdown();
    xBehaveMgr_Shutdown();
    zCineFXShutdown();
    zCineFXShutdown();
    zAssetShutdown();
    zDispatcher_Shutdown();
    xSGShutdown();
    xSerialShutdown();
    xUtilShutdown();
    iSystemExit();
}

// Debug output setup, with Ratatouille's xOutEnable list. Compiled out, these statements keep
// GC/2.6 from auto-inlining this (>= 30 statements), as retail calls it from main; only the
// xOutInfo time arguments remain.
static void zMainOutputMgrSetup()
{
    xOutSetSev(2);

    xOutEnable("zMain.cpp", xOutSevIgnore);
    xOutEnable("zGame.cpp", xOutSevIgnore);
    xOutEnable("zMenu.cpp", xOutSevIgnore);
    xOutEnable("zScene.cpp", xOutSevIgnore);
    xOutEnable("PAD", xOutSevIgnore);
    xOutEnable("PACKER", xOutSevIgnore);
    xOutEnable("FONT", xOutSevIgnore);
    xOutEnable("FILE", xOutSevIgnore);
    xOutEnable("PAR", xOutSevIgnore);
    xOutEnable("ANIM", xOutSevIgnore);
    xOutEnable("COLLIS", xOutSevIgnore);
    xOutEnable("SOUND", xOutSevIgnore);
    xOutEnable("CAMERA", xOutSevIgnore);
    xOutEnable("ENV", xOutSevIgnore);
    xOutEnable("BSP", xOutSevIgnore);
    xOutEnable("HUD", xOutSevIgnore);
    xOutEnable("PLAYER", xOutSevIgnore);
    xOutEnable("ENT", xOutSevIgnore);
    xOutEnable("PLAT", xOutSevIgnore);
    xOutEnable("MOVP", xOutSevIgnore);
    xOutEnable("PICKUP", xOutSevIgnore);
    xOutEnable("EVENT", xOutSevIgnore);
    xOutEnable("MATH", xOutSevIgnore);
    xOutEnable("UTIL", xOutSevIgnore);
    xOutEnable("GRID", xOutSevIgnore);
    xOutEnable("GROUP", xOutSevIgnore);
    xOutEnable("SAVE", xOutSevIgnore);
    xOutEnable("GAME", xOutSevForce);
    xOutEnable("RyzMemData", xOutSevIgnore);
    xOutEnable("NPCSpawner", xOutSevIgnore);
    xOutEnable("NPCTypesModule", xOutSevIgnore);
    xOutEnable("NPCGoals", xOutSevIgnore);
    xOutEnable("NPCModes", xOutSevIgnore);
    xOutEnable("NPCManager", xOutSevIgnore);
    xOutEnable("PACKER", xOutSevWarn);

    // the time arguments of the compiled-out xOutInfo
    iTimeDiffSec(iTimeGet());
    iTimeGet();
}

static void zMainParseINIGlobals(xIniFile* ini)
{
    U32 use_degrees;

    globals.player.g.AnalogMin = xIniGetInt(ini, "g.AnalogMin", 32);
    globals.player.g.AnalogMax = xIniGetInt(ini, "g.AnalogMax", 110);
    xScrFxLetterBoxSetSize(xIniGetFloat(ini, "ScrFxLetterBoxSize", 0.0f));
    xScrFxLetterBoxSetAlpha(xIniGetInt(ini, "ScrFxLetterBoxAlpha", 255));
    globals.player.g.InitialShinyCount = xIniGetInt(ini, "g.InitialShinyCount", 0);
    globals.player.g.InitialSpatulaCount = xIniGetInt(ini, "g.InitialSpatulaCount", 0);
    globals.player.g.ShinyValuePurple = xIniGetInt(ini, "g.ShinyValuePurple", 100);
    globals.player.g.ShinyValueBlue = xIniGetInt(ini, "g.ShinyValueBlue", 25);
    globals.player.g.ShinyValueGreen = xIniGetInt(ini, "g.ShinyValueGreen", 10);
    globals.player.g.ShinyValueYellow = xIniGetInt(ini, "g.ShinyValueYellow", 5);
    globals.player.g.ShinyValueRed = xIniGetInt(ini, "g.ShinyValueRed", 1);
    zRewardsMgr::ParseValuesFromIni(ini);
    globals.player.g.ComboTimer = xIniGetFloat(ini, "g.ComboTimer", 1.0f);
    globals.player.g.SundaeTime = xIniGetFloat(ini, "g.SundaeTime", 6.0f);
    globals.player.g.SundaeMult = xIniGetFloat(ini, "g.SundaeMult", 1.5f);
    globals.player.g.BBashTime = xIniGetFloat(ini, "g.BBashTime", 0.3f);
    globals.player.g.BBashDelay = xIniGetFloat(ini, "g.BBashDelay", 0.15f);
    globals.player.g.BBashCVTime = xIniGetFloat(ini, "g.BBashCVTime", 0.15f);
    globals.player.g.BBashHeight = xIniGetFloat(ini, "g.BBashHeight", 3.0f);
    globals.player.g.BBounceSpeed = xIniGetFloat(ini, "g.BBounceSpeed", 10.0f);
    globals.player.g.BBashGauntletDelay = xIniGetFloat(ini, "g.BBashGauntletDelay", 3.0f);
    globals.player.g.BBashGauntletBlastRadius =
        xIniGetFloat(ini, "g.BBashGauntletBlastRadius", 4.0f);
    globals.player.g.BSpinMinFrame = xIniGetFloat(ini, "g.BSpinMinFrame", 2.0f);
    globals.player.g.BSpinMaxFrame = xIniGetFloat(ini, "g.BSpinMaxFrame", 20.0f);
    globals.player.g.BSpinRadius = xIniGetFloat(ini, "g.BSpinRadius", 0.3f);
    globals.player.g.BSpinReach = xIniGetFloat(ini, "g.BSpinReach", 1.22f);
    globals.player.g.BSpinReachUpgrade = xIniGetFloat(ini, "g.BSpinReachUpgrade", 1.6f);
    globals.player.g.BSpinMinFrame *= 1.0f / 30.0f;
    globals.player.g.BSpinMaxFrame *= 1.0f / 30.0f;
    globals.player.g.DamageTimeHit = xIniGetFloat(ini, "g.DamageTimeHit", 0.5f);
    globals.player.g.DamageTimeSurface = xIniGetFloat(ini, "g.DamageTimeSurface", 1.0f);
    globals.player.g.DamageTimeEGen = xIniGetFloat(ini, "g.DamageTimeEGen", 1.0f);
    globals.player.g.DamageSurfKnock = xIniGetFloat(ini, "g.DamageSurfKnock", 1.75f);
    globals.player.g.DamageGiveHealthKnock = xIniGetFloat(ini, "g.DamageGiveHealthKnock", 1.75f);
    globals.player.g.BubbleBowlTimeDelay = xIniGetFloat(ini, "g.BubbleBowlTimeDelay", 1.0f);
    globals.player.g.BubbleBowlLaunchPosLeft = xIniGetFloat(ini, "g.BubbleBowlLaunchPosLeft", 0.0f);
    globals.player.g.BubbleBowlLaunchPosUp = xIniGetFloat(ini, "g.BubbleBowlLaunchPosUp", 1.0f);
    globals.player.g.BubbleBowlLaunchPosAt = xIniGetFloat(ini, "g.BubbleBowlLaunchPosAt", 1.5f);
    globals.player.g.BubbleBowlLaunchVelLeft = xIniGetFloat(ini, "g.BubbleBowlLaunchVelLeft", 0.0f);
    globals.player.g.BubbleBowlLaunchVelUp = xIniGetFloat(ini, "g.BubbleBowlLaunchVelUp", 0.0f);
    globals.player.g.BubbleBowlLaunchVelAt = xIniGetFloat(ini, "g.BubbleBowlLaunchVelAt", 10.0f);
    globals.player.g.BubbleBowlPercentIncrease =
        xIniGetFloat(ini, "g.BubbleBowlPercentIncrease", 0.85f);
    globals.player.g.BubbleBowlMinSpeed = xIniGetFloat(ini, "g.BubbleBowlMinSpeed", 0.1f);
    globals.player.g.BubbleBowlMinRecoverTime =
        xIniGetFloat(ini, "g.BubbleBowlMinRecoverTime", 0.15f);
    globals.player.g.CartwheelDamageSize = xIniGetFloat(ini, "g.CartwheelDamageSize", 1.3f);
    globals.player.g.SlideAccelVelMin = xIniGetFloat(ini, "g.SlideAccelVelMin", 6.0f);
    globals.player.g.SlideAccelVelMax = xIniGetFloat(ini, "g.SlideAccelVelMax", 9.0f);
    globals.player.g.SlideAccelStart = xIniGetFloat(ini, "g.SlideAccelStart", 9.0f);
    globals.player.g.SlideAccelEnd = xIniGetFloat(ini, "g.SlideAccelEnd", 4.5f);
    globals.player.g.SlideAccelPlayerFwd = xIniGetFloat(ini, "g.SlideAccelPlayerFwd", 5.0f);
    globals.player.g.SlideAccelPlayerBack = xIniGetFloat(ini, "g.SlideAccelPlayerBack", 5.0f);
    globals.player.g.SlideAccelPlayerSide = xIniGetFloat(ini, "g.SlideAccelPlayerSide", 15.0f);
    globals.player.g.SlideVelMaxStart = xIniGetFloat(ini, "g.SlideVelMaxStart", 12.0f);
    globals.player.g.SlideVelMaxEnd = xIniGetFloat(ini, "g.SlideVelMaxEnd", 17.0f);
    globals.player.g.SlideVelMaxIncTime = xIniGetFloat(ini, "g.SlideVelMaxIncTime", 6.0f);
    globals.player.g.SlideVelMaxIncAccel = xIniGetFloat(ini, "g.SlideVelMaxIncAccel", 1.0f);
    globals.player.g.SlideAirHoldTime = xIniGetFloat(ini, "g.SlideAirHoldTime", 1.0f);
    globals.player.g.SlideAirSlowTime = xIniGetFloat(ini, "g.SlideAirSlowTime", 1.5f);
    globals.player.g.SlideAirDblHoldTime = xIniGetFloat(ini, "g.SlideAirDblHoldTime", 1.0f);
    globals.player.g.SlideAirDblSlowTime = xIniGetFloat(ini, "g.SlideAirDblSlowTime", 1.5f);
    globals.player.g.SlideVelDblBoost = xIniGetFloat(ini, "g.SlideVelDblBoost", 6.0f);
    globals.player.g.TakeDamage = 1;
    globals.player.g.CheatSpongeball = 0;
    globals.player.g.CheatPlayerSwitch = 0;
    globals.player.g.CheatAlwaysPortal = 0;
    globals.player.g.CheatFlyToggle = 0;
    globals.player.g.FriendlySelectMode = 0;
    globals.player.g.RotMatchMaxAngle = xIniGetFloat(ini, "g.RotMatchMaxAngle", 30.0f);
    globals.player.g.RotMatchMatchTime = xIniGetFloat(ini, "g.RotMatchMatchTime", 0.1f);
    globals.player.g.RotMatchRelaxTime = xIniGetFloat(ini, "g.RotMatchRelaxTime", 0.3f);
    globals.player.g.RotMatchMaxAngle = (PI / 180.0f) * globals.player.g.RotMatchMaxAngle;

    use_degrees = xIniGetFloat(ini, "zcam_highbounce_pitch", 180.0f) != 180.0f;
    xcam_collis_radius = xIniGetFloat(ini, "xcam_collis_radius", xcam_collis_radius);
    xcam_collis_stiffness = xIniGetFloat(ini, "xcam_collis_stiffness", xcam_collis_stiffness);
    zcam_pad_pyaw_scale = xIniGetFloat(ini, "zcam_pad_pyaw_scale", zcam_pad_pyaw_scale);
    zcam_pad_pitch_scale = xIniGetFloat(ini, "zcam_pad_pitch_scale", zcam_pad_pitch_scale);
    zcam_near_d = xIniGetFloat(ini, "zcam_near_d", zcam_near_d);
    zcam_near_h = xIniGetFloat(ini, "zcam_near_h", zcam_near_h);
    zcam_near_pitch = xIniGetFloat(ini, "zcam_near_pitch",
                                   use_degrees ? (180.0f / PI) * zcam_near_pitch : zcam_near_pitch);
    zcam_far_d = xIniGetFloat(ini, "zcam_far_d", zcam_far_d);
    zcam_far_h = xIniGetFloat(ini, "zcam_far_h", zcam_far_h);
    zcam_far_pitch = xIniGetFloat(ini, "zcam_far_pitch",
                                  use_degrees ? (180.0f / PI) * zcam_far_pitch : zcam_far_pitch);
    zcam_above_d = xIniGetFloat(ini, "zcam_above_d", zcam_above_d);
    zcam_above_h = xIniGetFloat(ini, "zcam_above_h", zcam_above_h);
    zcam_above_pitch = xIniGetFloat(
        ini, "zcam_above_pitch", use_degrees ? (180.0f / PI) * zcam_above_pitch : zcam_above_pitch);
    zcam_below_d = xIniGetFloat(ini, "zcam_below_d", zcam_below_d);
    zcam_below_h = xIniGetFloat(ini, "zcam_below_h", zcam_below_h);
    zcam_below_pitch = xIniGetFloat(
        ini, "zcam_below_pitch", use_degrees ? (180.0f / PI) * zcam_below_pitch : zcam_below_pitch);
    zcam_highbounce_d = xIniGetFloat(ini, "zcam_highbounce_d", zcam_highbounce_d);
    zcam_highbounce_h = xIniGetFloat(ini, "zcam_highbounce_h", zcam_highbounce_h);
    zcam_highbounce_pitch =
        xIniGetFloat(ini, "zcam_highbounce_pitch",
                     use_degrees ? (180.0f / PI) * zcam_highbounce_pitch : zcam_highbounce_pitch);
    zcam_wall_d = xIniGetFloat(ini, "zcam_wall_d", zcam_wall_d);
    zcam_wall_h = xIniGetFloat(ini, "zcam_wall_h", zcam_wall_h);
    zcam_wall_pitch = xIniGetFloat(ini, "zcam_wall_pitch",
                                   use_degrees ? (180.0f / PI) * zcam_wall_pitch : zcam_wall_pitch);
    zcam_overrot_min = xIniGetFloat(ini, "zcam_overrot_min", (180.0f / PI) * zcam_overrot_min);
    zcam_overrot_mid = xIniGetFloat(ini, "zcam_overrot_mid", (180.0f / PI) * zcam_overrot_mid);
    zcam_overrot_max = xIniGetFloat(ini, "zcam_overrot_max", (180.0f / PI) * zcam_overrot_max);
    zcam_overrot_rate = xIniGetFloat(ini, "zcam_overrot_rate", zcam_overrot_rate);
    zcam_overrot_tstart = xIniGetFloat(ini, "zcam_overrot_tstart", zcam_overrot_tstart);
    zcam_overrot_tend = xIniGetFloat(ini, "zcam_overrot_tend", zcam_overrot_tend);
    zcam_overrot_velmin = xIniGetFloat(ini, "zcam_overrot_velmin", zcam_overrot_velmin);
    zcam_overrot_velmax = xIniGetFloat(ini, "zcam_overrot_velmax", zcam_overrot_velmax);
    zcam_overrot_tmanual = xIniGetFloat(ini, "zcam_overrot_tmanual", zcam_overrot_tmanual);
    globals.player.g.Gravity = xIniGetFloat(ini, "g.Gravity", 30.0f);
    globals.noMovies = xIniGetInt(ini, "NoMovies", 0);

    if (use_degrees)
    {
        zcam_near_pitch = (PI / 180.0f) * zcam_near_pitch;
        zcam_far_pitch = (PI / 180.0f) * zcam_far_pitch;
        zcam_above_pitch = (PI / 180.0f) * zcam_above_pitch;
        zcam_below_pitch = (PI / 180.0f) * zcam_below_pitch;
        zcam_highbounce_pitch = (PI / 180.0f) * zcam_highbounce_pitch;
        zcam_wall_pitch = (PI / 180.0f) * zcam_wall_pitch;
    }

    zcam_overrot_min = (PI / 180.0f) * zcam_overrot_min;
    zcam_overrot_mid = (PI / 180.0f) * zcam_overrot_mid;
    zcam_overrot_max = (PI / 180.0f) * zcam_overrot_max;

    gSkipTimeCutscene = xIniGetFloat(ini, "gSkipTimeCutscene", gSkipTimeCutscene);
    gSkipTimeFlythrough = xIniGetFloat(ini, "gSkipTimeFlythrough", gSkipTimeFlythrough);
    gSkipTimeCutscene = gSkipTimeCutscene > 1.0f ? gSkipTimeCutscene : 1.0f;
    gSkipTimeFlythrough = gSkipTimeFlythrough > 0.0f ? gSkipTimeFlythrough : 0.0f;

    globals.option_vibration = xIniGetInt(ini, "option_vibration", 1);
    globals.option_subtitles = xIniGetInt(ini, "option_subtitles", 0);

    globals.player.carry.minDist = xIniGetFloat(ini, "carry.minDist", 0.675f);
    globals.player.carry.maxDist = xIniGetFloat(ini, "carry.maxDist", 1.9f);
    globals.player.carry.minHeight = xIniGetFloat(ini, "carry.minHeight", -0.2f);
    globals.player.carry.maxHeight = xIniGetFloat(ini, "carry.maxHeight", 0.4f);
    globals.player.carry.maxCosAngle = xIniGetFloat(ini, "carry.maxCosAngle", 45.0f);
    globals.player.carry.throwMinDist = xIniGetFloat(ini, "carry.throwMinDist", 1.5f);
    globals.player.carry.throwMaxDist = xIniGetFloat(ini, "carry.throwMaxDist", 12.0f);
    globals.player.carry.throwHammerMinDist = xIniGetFloat(ini, "carry.throwHammerMinDist", 1.5f);
    globals.player.carry.throwHammerMaxDist = xIniGetFloat(ini, "carry.throwHammerMaxDist", 12.0f);
    globals.player.carry.throwMinHeight = xIniGetFloat(ini, "carry.throwMinHeight", -3.0f);
    globals.player.carry.throwMaxHeight = xIniGetFloat(ini, "carry.throwMaxHeight", 5.0f);
    globals.player.carry.throwMaxStack = xIniGetFloat(ini, "carry.throwMaxStack", 2.75f);
    globals.player.carry.throwMaxCosAngle = xIniGetFloat(ini, "carry.throwMaxCosAngle", 25.0f);
    globals.player.carry.grabLerpMin = xIniGetFloat(ini, "carry.grabLerpMin", 0.0f);
    globals.player.carry.grabLerpMax = xIniGetFloat(ini, "carry.grabLerpMax", 0.2f);
    globals.player.carry.throwGravity = xIniGetFloat(ini, "carry.throwGravity", 30.0f);
    globals.player.carry.throwHeight = xIniGetFloat(ini, "carry.throwHeight", 3.75f);
    globals.player.carry.throwDistance = xIniGetFloat(ini, "carry.throwDistance", 10.0f);
    globals.player.carry.throwPowerAddDistance =
        xIniGetFloat(ini, "carry.throwPowerAddDistance", 10.0f);
    globals.player.carry.fruitFloorDecayMin = xIniGetFloat(ini, "carry.fruitFloorDecayMin", 0.3f);
    globals.player.carry.fruitFloorDecayMax = xIniGetFloat(ini, "carry.fruitFloorDecayMax", 6.0f);
    globals.player.carry.fruitFloorBounce = xIniGetFloat(ini, "carry.fruitFloorBounce", 0.15f);
    globals.player.carry.fruitFloorFriction = xIniGetFloat(ini, "carry.fruitFloorFriction", 0.4f);
    globals.player.carry.fruitCeilingBounce = xIniGetFloat(ini, "carry.fruitCeilingBounce", 0.1f);
    globals.player.carry.fruitWallBounce = xIniGetFloat(ini, "carry.fruitWallBounce", 0.5f);
    globals.player.carry.fruitLifetime = xIniGetFloat(ini, "carry.fruitLifetime", 15.0f);
    globals.player.carry.maxCosAngle = icos((PI / 180.0f) * globals.player.carry.maxCosAngle);
    globals.player.carry.throwMaxCosAngle =
        icos((PI / 180.0f) * globals.player.carry.throwMaxCosAngle);

    globals.player.g.PowerUp[0] = xIniGetInt(ini, "g.KarateSpin", 0);
    globals.player.g.PowerUp[2] = xIniGetInt(ini, "g.MachoBowl", 0);
    globals.player.g.PowerUp[3] = xIniGetInt(ini, "g.WaveGuitar", 0);
    globals.player.g.PowerUp[5] = xIniGetInt(ini, "g.Cartwheel", 0);
    globals.player.g.PowerUp[1] = xIniGetInt(ini, "g.Bash", 0);
    globals.player.g.PowerUp[6] = xIniGetInt(ini, "g.BellyFlop", 0);
    globals.player.g.PowerUp[4] = xIniGetInt(ini, "g.BellyBump", 0);
    globals.player.g.PowerUp[7] = xIniGetInt(ini, "g.Throw", 0);
    globals.player.g.PowerUp[8] = xIniGetInt(ini, "g.HealthSB", 0);
    globals.player.g.PowerUp[9] = xIniGetInt(ini, "g.HealthPat", 0);
    memcpy(globals.player.g.InitialPowerUp, globals.player.g.PowerUp,
           sizeof(globals.player.g.InitialPowerUp));
    globals.player.InitialUnusedSkillPoints = xIniGetInt(ini, "g.UnusedSkillPoints", 0);
    globals.player.InvManlinessPointsMultiplier = 1;

    SCENE_ID_MNU_INGAME = (xIniGetString(ini, "MNUI", "MNUI")[0] << 24) |
                          (xIniGetString(ini, "MNUI", "MNUI")[1] << 16) |
                          (xIniGetString(ini, "MNUI", "MNUI")[2] << 8) |
                          xIniGetString(ini, "MNUI", "MNUI")[3];

    char* SCENE_ID_MNU_START_TEXT = (char*)xIniGetString(ini, "MNUS", "MNUS");
    SCENE_ID_MNU_START = (SCENE_ID_MNU_START_TEXT[0] << 24) | (SCENE_ID_MNU_START_TEXT[1] << 16) |
                         (SCENE_ID_MNU_START_TEXT[2] << 8) | SCENE_ID_MNU_START_TEXT[3];

    char* videosys = (char*)xIniGetString(ini, "VideoSystem", "");
    if (strcmp(videosys, "PAL") == 0)
    {
        gVidMode = eGameVidModePAL;
        iSystemGetWidthHeight(FB_XRES, FB_YRES, true);
        FB_DEPTH = 24;
        VBLANKS_PER_SEC = 50.0f;
    }
    else
    {
        if (strcmp(videosys, "NTSC") != 0)
        {
            // unknown video system: default to NTSC
        }

        gVidMode = eGameVidModeNTSC;
        iSystemGetWidthHeight(FB_XRES, FB_YRES, false);
        FB_DEPTH = 24;
        VBLANKS_PER_SEC = 60.0f;
    }

    char DEFAULT_REGION_NTSC[3] = "US";
    char DEFAULT_REGION_PAL[3] = "UK";

    char* defaultregion = (char*)xIniGetString(ini, "DefaultRegion", "");
    if (!xRegionCodeIsValid(defaultregion))
    {
        if (strcmp(videosys, "PAL") == 0)
        {
            defaultregion = DEFAULT_REGION_PAL;
        }
        else
        {
            defaultregion = DEFAULT_REGION_NTSC;
        }
    }

    xSTSetLocalizationCode(defaultregion);
    xtextbox::japanese_word_breaking = xSTGetLocalizationEnum() == eRegionJP;
    SECS_PER_VBLANK = 1.0f / VBLANKS_PER_SEC;

    char* iniString;
    S32 idx;
    S32 playerCount;

    idx = 0;
    iniString = xIniGetString(ini, "ScenePlayerMapping", "", idx);
    while (idx > 0)
    {
        U32 sceneTag;

        while (_isspace(*iniString))
        {
            iniString++;
        }

        sceneTag = (_toupper(iniString[0]) << 24) | (_toupper(iniString[1]) << 16) |
                   (_toupper(iniString[2]) << 8) | _toupper(iniString[3]);
        iniString += 4;

        playerCount = 0;
        while (playerCount < 10 && *iniString != '\0')
        {
            U32 playerTag;

            while (_isspace(*iniString))
            {
                iniString++;
            }

            playerTag = (_toupper(iniString[0]) << 24) | (_toupper(iniString[1]) << 16) |
                        (_toupper(iniString[2]) << 8) | _toupper(iniString[3]);
            zConfig_setScenePlayer_MapEntry(sceneTag, playerTag);
            playerCount++;
            iniString += 4;
        }

        iniString = xIniGetString(ini, "ScenePlayerMapping", "", idx);
    }

    idx = 0;
    iniString = xIniGetString(ini, "PointerPlayerMapping", "", idx);
    while (idx > 0)
    {
        U32 sceneTag;

        while (_isspace(*iniString))
        {
            iniString++;
        }

        sceneTag = (_toupper(iniString[0]) << 24) | (_toupper(iniString[1]) << 16) |
                   (_toupper(iniString[2]) << 8) | _toupper(iniString[3]);
        iniString += 4;

        // playerCount is neither reset nor advanced here (same in the PS2 build); it is
        // left over from the scene loop above, or uninitialized if that loop never ran
        while (playerCount < 10 && *iniString != '\0')
        {
            U32 playerTag;

            while (_isspace(*iniString))
            {
                iniString++;
            }

            playerTag = (_toupper(iniString[0]) << 24) | (_toupper(iniString[1]) << 16) |
                        (_toupper(iniString[2]) << 8) | _toupper(iniString[3]);
            zConfig_setPointerPlayer_MapEntry(sceneTag, playerTag);
            iniString += 4;
        }

        iniString = xIniGetString(ini, "PointerPlayerMapping", "", idx);
    }

    ztalkbox::load_settings(*ini);
    zEntPlayer_InitPlayerDefaultValues(ini);
    zMenuSB_InitTaskDefaults(ini);
    zUICustomExtrasManager::InitExtrasList(ini);

    sShowMenuOnBoot = xIniGetInt(ini, "ShowMenuOnBoot", 1);
    globals.asyncLoadingScreen = xIniGetInt(ini, "AsyncLoadingScreen", 1);
    globals.asyncLoadingFlags = xIniGetInt(ini, "AsyncLoadingFlags", 0);
    globals.demoType = (zGlobalDemoType)xIniGetInt(ini, "DemoType", 0);
    globals.FlashWIP = xIniGetInt(ini, "FlashWIP", 0) != 0;
    ShortbreadCookieJar = xIniGetFloat(ini, "ShortbreadCookieJar", 0.0f);
    globals.player.g.AttractModeDuringGameplay = xIniGetInt(ini, "g.AttractModeDuringGameplay", 0);
}

static void zMainMemLvlChkCB()
{
    zSceneMemLvlChkCB();
}

static void zMainLoop()
{
    zAssetPickupTable* ptr;
    U32 newGameSceneID;

    zMainShowProgressBar(-1);
    zVarInit(zVarEntryTable);
    zMainShowProgressBar(-1);
    xMemPushBase();
    zSceneLoadPackFile('BOOT', 1, false, NULL);
    zMainShowProgressBar(-1);
    zSceneLoadPackFile('PLAT', 1, true, NULL);
    zMainShowProgressBar(-1);
    xShadowSimple_Init();
    zMainShowProgressBar(-1);
    ptr = (zAssetPickupTable*)xSTFindAssetByType('PICK', 0, NULL);
    globals.pickupTable = ptr;
    zPickupTableInit();
    zMainShowProgressBar(-1);
    zMainShowProgressBar(-1);
    xMemPushBase();
    zSceneLoadPackFile('MNUC', 1, false, NULL);
    zMainShowProgressBar(-1);
    xMemPushBase();
    xTRCInitMessages();

    static U32 menuModeID = SCENE_ID_MNU_START;
    static U32 gameSceneID = (globals.sceneStart[0] << 24) | (globals.sceneStart[1] << 16) |
                             (globals.sceneStart[2] << 8) | globals.sceneStart[3];

    newGameSceneID = gameSceneID;
    xMemPushBase();
    zMainShowProgressBar(-1);

    while (true)
    {
        xSerialWipeMainBuffer();
        zUICustomExtrasManager::ResetExtrasStatus();

        if (sShowMenuOnBoot)
        {
            zMenuInit(menuModeID);
            zMainShowProgressBar(100);
            zMenuSetup();
            newGameSceneID = zMenuLoop();
            zMenuExit();
        }
        else
        {
            sShowMenuOnBoot = 1;
            globals.firstStartPressed = TRUE;
            zGameModeSwitch(eGameMode_Game);
            zGameStateSwitch(0);
        }

        if (newGameSceneID == 0)
        {
            zVarNewGame();
            iTimeSetGame(0.0f);
            gameSceneID = (globals.sceneStart[0] << 24) | (globals.sceneStart[1] << 16) |
                          (globals.sceneStart[2] << 8) | globals.sceneStart[3];
        }
        else
        {
            gameSceneID = newGameSceneID;
            iTimeSetGame(0.0f);
        }

        zGameInit(gameSceneID);
        zGameSetup();
        iProfileClear(gameSceneID);
        zGameLoop();
        zGameExit();
    }
}

static void zMainReadINI(const char* iniName)
{
    char* str;
    void* buf;
    U32 size;
    xIniFile* ini;
    U32 num_levels;
    char* tok;
    S32 level_count;
    char* watermark;

    buf = iFileLoad(iniName, NULL, &size);
    if (buf)
    {
        ini = xIniParse((char*)buf, size);

        str = (char*)xIniGetString(ini, "PATH", NULL);
        if (str)
        {
            iFileSetPath(str);
        }

        str = (char*)xIniGetString(ini, "BOOT", NULL);
        if (str)
        {
            if (xStricmp(str, "Soak") == 0)
            {
                gSoak = 1;
                strcpy(globals.sceneStart, allsoaklevels[0]);
                soaklevels = allsoaklevels;
            }
            else if (xStricmp(str, "SuperSoak") == 0)
            {
                gSoak = 1;
                enableSuperSoaker = 1;
                strcpy(globals.sceneStart, allsoaklevels[0]);
                soaklevels = allsoaklevels;
            }
            else if (xStricmp(str, "ButtonMash") == 0)
            {
                gSoak = 1;
                enableMuttonMasher = 1;
                strcpy(globals.sceneStart, allsoaklevels[0]);
                soaklevels = allsoaklevels;
            }
            else if (xStricmp(str, "SuperMash") == 0)
            {
                gSoak = 1;
                enableSuperSoaker = 1;
                enableMuttonMasher = 1;
                strcpy(globals.sceneStart, allsoaklevels[0]);
                soaklevels = allsoaklevels;
            }
            else
            {
                num_levels = xStrTokCount(str, " ,", NULL);
                soaklevels = (char**)RwMalloc((num_levels + 1) * sizeof(char*), 0);

                tok = strtok(str, " ,");
                level_count = 0;
                while (tok)
                {
                    if (strlen(tok) == 4)
                    {
                        soaklevels[level_count] = (char*)RwMalloc(strlen(tok) + 1, 0);
                        strcpy(soaklevels[level_count], tok);
                        level_count++;
                    }
                    tok = strtok(NULL, " ,");
                }

                soaklevels[num_levels] = NULL;
                strupr(soaklevels[0]);
                strcpy(globals.sceneStart, soaklevels[0]);

                if (level_count != 1)
                {
                    gSoak = 1;
                }
            }
        }

        globals.ForceAllCinematics = xIniGetInt(ini, "ForceAllCinematics", 0);
        if (gSoak)
        {
            globals.ForceAllCinematics = 1;
        }

        strncpy(globals.profFunc[0], xIniGetString(ini, "ProfFuncTriangle", ""), 128);
        globals.profFunc[0][127] = '\0';
        strncpy(globals.profFunc[1], xIniGetString(ini, "ProfFuncSquare", ""), 128);
        globals.profFunc[1][127] = '\0';
        strncpy(globals.profFunc[2], xIniGetString(ini, "ProfFuncLeft", ""), 128);
        globals.profFunc[2][127] = '\0';
        strncpy(globals.profFunc[3], xIniGetString(ini, "ProfFuncRight", ""), 128);
        globals.profFunc[3][127] = '\0';
        strncpy(globals.profFunc[4], xIniGetString(ini, "ProfFuncUp", ""), 128);
        globals.profFunc[4][127] = '\0';
        strncpy(globals.profFunc[5], xIniGetString(ini, "ProfFuncDown", ""), 128);
        globals.profFunc[5][127] = '\0';

        globals.NoMusic = xIniGetInt(ini, "NoMusic", 0) != 0;
        globals.ForceMono = xIniGetInt(ini, "ForceMono", 0) != 0;
        if (globals.ForceMono)
        {
            gSnd.sndMode = xSndMode_Mono;
            iSndSetMode();
        }

        globals.NoCutscenes = xIniGetInt(ini, "NoCutscenes", 0) != 0;
        globals.NoPadCheck = xIniGetInt(ini, "NoPadCheck", 0) != 0;

        watermark = (char*)xIniGetString(ini, "Watermark", NULL);
        if (watermark)
        {
            strcpy(globals.watermark, watermark);
        }
        globals.watermarkAlpha = xIniGetInt(ini, "WatermarkAlpha", 0x30);
        globals.watermarkSize = xIniGetFloat(ini, "WatermarkSize", 40.0f);

        zMainParseINIGlobals(ini);
        zDispatcherParseINI(ini);
        xIniDestroy(ini);
    }

    iTimeDiffSec(iTimeGet());
    iTimeGet();
    RwFree(buf);
}

void zMainFirstScreen(S32 mode)
{
    RwCamera* cam = iCameraCreate(FB_XRES, FB_YRES, 0);
    RwRGBA bg = { 0, 0, 0, 0 };

    for (S32 i = 0; i < 3; i++)
    {
        RwCameraClear(cam, &bg, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
        RwCameraBeginUpdateWrapper(cam);

        if (mode)
        {
            char* str = NULL;
            if (globals.demoType)
            {
                str = xTextFindString("LEGAL TEXT DEMO", NULL);
            }
            if (str == NULL)
            {
                str = xTextFindString("LEGAL TEXT", NULL);
            }

            // not in the DWARF: retail loads the whole color from an anonymous .sdata2 literal,
            // which xColorFromRGBA cannot produce; the local has no stack slot
            iColor_tag color = { 255, 230, 0, 200 };
            xtextbox tb = xtextbox::create(xfont::create(1, NSCREENX(19.0f), NSCREENY(22.0f), 0.0f,
                                                         color, screen_bounds),
                                           screen_bounds, 2, 0.0f, 0.0f, 0.0f, 0.0f);

            tb.set_text(str);
            tb.bounds = screen_bounds;
            tb.bounds.contract(0.1f);
            tb.bounds.h = tb.yextent(true);
            tb.bounds.y = 0.5f - 0.5f * tb.bounds.h;
            tb.render(true);
        }

        RwCameraEndUpdate(cam);
        RwCameraShowRaster(cam, NULL, 1);
    }

    RwCameraShowRaster(cam, NULL, 1);

    iTime startTime = iTimeGet();
    F32 diffTime;
    do
    {
        iTRCDisk::TRCCheck();
        diffTime = iTimeDiffSec(startTime, iTimeGet());
    } while (diffTime < 5.0f || diffTime > 200.0f);

    iCameraDestroy(cam);
}

enum eStartupErrors
{
    eNoError,
    eNoFormat,
    eDamagedCard,
    eWrongDevice,
    eNoCards,
    eCorruptFile,
    eMaxSaveFiles,
    eNoController
};

// No symbol in TSSM (BfBB has an out-of-line copy): inlined at every call site.
static inline void zMainMemCardQueryPost(S32 needed, S32 available, S32 neededFiles, S32 showMsg)
{
    RwRGBA bg = { 0, 0, 0, 0 };
    RwCamera* cam = iCameraCreate(FB_XRES, FB_YRES, 0);
    RwCameraClear(cam, &bg, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
    RwCameraBeginUpdateWrapper(cam);
    render_mem_card_no_space(needed, available, neededFiles, showMsg != 0);
    RwCameraEndUpdate(cam);
    RwCameraShowRaster(cam, NULL, 1);
    iCameraDestroy(cam);
}

// No symbol in TSSM (BfBB has an out-of-line copy): inlined at every call site.
static inline void zMainMemCardRenderText(const char* text, bool enabled)
{
    RwRGBA bg = { 0, 0, 0, 0 };
    RwCamera* cam = iCameraCreate(FB_XRES, FB_YRES, 0);
    RwCameraClear(cam, &bg, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
    RwCameraBeginUpdateWrapper(cam);
    RenderText(text, enabled);
    RwCameraEndUpdate(cam);
    RwCameraShowRaster(cam, NULL, 1);
    iCameraDestroy(cam);
}

void zMainMemCardSpaceQuery()
{
    S32 bytesNeeded;
    S32 availOnDisk;
    S32 neededFiles;
    S32 do_chk;
    S32 fullCard;
    U8 formatInProgress;
    U8 formatFailed;
    eStartupErrors startupError;
    S32 status;
    void* workArea;
    // not in the PS2 DWARF (GameCube-only memory-card code): Ratatouille's GC DWARF lists a
    // function-scope 'long resultCode' (an SDK s32) shared by the format and delete code.
    s32 resultCode;
    // not in the DWARF: GameCube memory-card formatting progress code, which the PS2 build does not
    // have.
    S32 startBytes;

    if (globals.demoType != zDT_NONE)
    {
        return;
    }

    bytesNeeded = 0;
    availOnDisk = 0;
    neededFiles = 0;
    do_chk = 1;
    status = 1;
    formatInProgress = 0;
    formatFailed = 0;
    startupError = eNoError;
    fullCard = -1;
    startBytes = 0;
    workArea = RwMalloc(CARD_WORKAREA_SIZE, 0);

    while (true)
    {
        iTRCDisk::TRCCheck();

        if (do_chk)
        {
            status = zMenuCardCheckStartup(&bytesNeeded, &availOnDisk, &neededFiles);
        }

        if (bytesNeeded == -1 && availOnDisk == -1 && neededFiles == -1)
        {
            startupError = eNoFormat;
            fullCard = zMenuGetBadCard() - 1;
        }

        if (bytesNeeded == -5 && availOnDisk == -5 && neededFiles == -5)
        {
            startupError = eDamagedCard;
            fullCard = zMenuGetBadCard() - 1;
        }

        if (bytesNeeded == -2 && availOnDisk == -2 && neededFiles == -2)
        {
            startupError = eWrongDevice;
            fullCard = zMenuGetBadCard() - 1;
        }

        if (bytesNeeded == -3 && availOnDisk == -3 && neededFiles == -3)
        {
            startupError = eNoCards;
            fullCard = -1;
        }

        if (bytesNeeded == -4 && availOnDisk == -4 && neededFiles == -4)
        {
            startupError = eCorruptFile;
            fullCard = zMenuGetBadCard() - 1;
        }

        if (bytesNeeded == -6 && availOnDisk == -6 && neededFiles == -6)
        {
            startupError = eMaxSaveFiles;
            fullCard = zMenuGetBadCard() - 1;
        }

        if (fullCard >= 0 && formatInProgress)
        {
            char bar[16];
            char msg[256];
            F32 pct;
            S32 pos;

            resultCode = CARDGetResultCode(fullCard);
            memset(bar, 0, sizeof(bar));
            strcpy(bar, "oooooooo");
            pct = (F32)(CARDGetXferredBytes(fullCard) - startBytes) / CARD_WORKAREA_SIZE;
            pos = pct * strlen(bar);
            bar[pos] = 'O';
            sprintf(msg, "{i:text_mem_card_formatting}{n}[%s]", bar);
            zMainMemCardRenderText(msg, true);

            switch (resultCode)
            {
            case CARD_RESULT_BUSY:
                break;
            case CARD_RESULT_READY:
                CARDUnmount(fullCard);
                do_chk = 1;
                formatInProgress = 0;
                formatFailed = 0;
                startupError = eNoError;
                fullCard = -1;
                continue;
            case CARD_RESULT_NOCARD:
                CARDUnmount(fullCard);
                formatInProgress = 0;
                formatFailed = 1;
                fullCard = -1;
                zMainMemCardRenderText("{i:text_mem_card_format_failed_yanked}", true);
                break;
            default:
                CARDUnmount(fullCard);
                formatInProgress = 0;
                formatFailed = 1;
                fullCard = -1;
                zMainMemCardRenderText("{i:text_mem_card_format_failed}", true);
                break;
            }
        }

        if (status == 0 && startupError == eNoError)
        {
            break;
        }

        do_chk = 0;

        if (!formatInProgress && !formatFailed && startupError == eNoError)
        {
            zSceneCardCheckStartup_set(bytesNeeded, availOnDisk, neededFiles);
            zMainMemCardQueryPost(bytesNeeded, availOnDisk, neededFiles, 1);
        }
        else if (!formatInProgress)
        {
            switch (startupError)
            {
            case eNoFormat:
                if (!formatFailed)
                {
                    zMainMemCardRenderText("{i:text_mem_card_no_format}", true);
                }
                break;
            case eDamagedCard:
                zMainMemCardRenderText("{i:text_mem_card_damaged_card}", true);
                break;
            case eWrongDevice:
                zMainMemCardRenderText("{i:text_mem_card_wrong_device}", true);
                break;
            case eNoCards:
                zMainMemCardRenderText("{i:text_mem_card_no_card}", true);
                break;
            case eCorruptFile:
                zMainMemCardRenderText("{i:text_mem_card_corrupt_file}", true);
                break;
            case eMaxSaveFiles:
                zMainMemCardRenderText("{i:text_mem_card_no_space_overwrite}", true);
                break;
            case eNoController:
                zMainMemCardRenderText("{i:text_no_controller}", true);
                break;
            }
        }

        xPadUpdate(globals.currentActivePad, SECS_PER_VBLANK);

        if (globals.pad0->pressed & XPAD_BUTTON_TRIANGLE)
        {
            do_chk = 1;
            startupError = eNoError;
            formatInProgress = 0;
            formatFailed = 0;
            fullCard = -1;
            zMainMemCardRenderText("{i:text_retry}", true);
        }
        else if (globals.pad0->pressed & XPAD_BUTTON_X)
        {
            if (startupError != eNoController)
            {
                break;
            }

            do_chk = 1;
            startupError = eNoError;
        }
        else if (globals.pad0->pressed & XPAD_BUTTON_O)
        {
            switch (startupError)
            {
            case eNoFormat:
                if (!formatFailed && !formatInProgress && fullCard >= 0)
                {
                    while (CARDProbeEx(fullCard, NULL, NULL) == CARD_RESULT_BUSY)
                    {
                    }

                    CARDMount(fullCard, workArea, NULL);
                    CARDCheck(fullCard);
                    CARDFormatAsync(fullCard, NULL);
                    startBytes = CARDGetXferredBytes(fullCard);
                    formatInProgress = 1;
                }
                break;
            case eDamagedCard:
            case eWrongDevice:
            case eNoCards:
                break;
            case eCorruptFile:
            {
                char files[3][64];

                while (CARDProbeEx(fullCard, NULL, NULL) == CARD_RESULT_BUSY)
                {
                }

                CARDMount(fullCard, workArea, NULL);

                if (CARDCheck(fullCard) == CARD_RESULT_READY)
                {
                    S32 fileCount = zMenuGetCorruptFile(files);

                    for (S32 i = 0; i < fileCount; i++)
                    {
                        zMainMemCardRenderText("{i:text_mem_card_deleting_file}", true);
                        resultCode = CARDDeleteAsync(fullCard, files[i], NULL);

                        if (resultCode != CARD_RESULT_READY)
                        {
                            zMainMemCardRenderText("{i:text_mem_card_delete_failed}", true);
                        }

                        while (resultCode == CARD_RESULT_BUSY)
                        {
                            resultCode = CARDGetResultCode(fullCard);
                            zMainMemCardRenderText("{i:text_mem_card_deleting_file}", true);
                        }

                        if (resultCode != CARD_RESULT_READY)
                        {
                            zMainMemCardRenderText("{i:text_mem_card_delete_failed}", true);
                        }
                    }
                }

                CARDUnmount(fullCard);
                do_chk = 1;
                startupError = eNoError;
                formatInProgress = 0;
                formatFailed = 0;
                fullCard = -1;
                break;
            }
            case eMaxSaveFiles:
            default:
                if (startupError != eWrongDevice && status == 0 && workArea)
                {
                    RwFree(workArea);
                    workArea = NULL;
                }

                OSResetSystem(1, 0, TRUE);
                break;
            }
        }
    }

    zMainMemCardQueryPost(0, 0, 0, 0);

    if (workArea)
    {
        RwFree(workArea);
    }
}

// Dead-stripped placeholder: retail pools these strings here, unreferenced. They belong to
// zStealthDebugGetAssertionInfo (Ratatouille's name), whose body cannot be recovered.
const char* __deadstripped_zMain_AssertionInfo()
{
    return "????\0MASTER\0%d %d\t%s\t%c%c%c%c\t%.4f %.4f %.4f\t%s";
}

// Dead-stripped in retail; name and body from Ratatouille's version, which returns this pooled
// header string.
char* zStealthDebugGetAssertionHeader()
{
    return "BUILD\tCONFIG\tSCENE\tPOS\tPLAYER";
}

// Dead-stripped placeholder: retail pools "demo_exit_screen", 1.0f, 5.0f and the unsigned-int
// conversion constant here, the literals of the PS2 demo's zMainExitScreen and DisplayExitCard. The
// GC bodies are unknown, so only the literals are kept.
F32 __deadstripped_zMain_ExitScreen(const char** name, F32* times)
{
    *name = "demo_exit_screen";
    times[0] = 1.0f;
    times[1] = 5.0f;
    return 0.5f / FB_XRES;
}

static S32 percentageDone;

// The GC frame has room for a 12-byte loadingText (the PS2 demo has [64]) and a constant xColor_tag
// c1 next to bg, as in Ratatouille's DWARF.
void zMainShowProgressBar(S32 percentage)
{
    char loadingText[12];
    char textToShow[128];
    char beforeText[64];
    char afterText[64];
    S32 spotToInsertColor;
    char* str;

    if (!zMenuIsFirstBoot() && (percentage == -1 || percentage == -2))
    {
        return;
    }

    if ((gGameMode == eGameMode_Game || gGameMode == eGameMode_Title) && percentage >= 0)
    {
        percentageDone = percentage;
    }

    if (percentageDone > 100)
    {
        percentageDone = 100;
    }

    spotToInsertColor = percentageDone / 10;

    str = xTextFindString("startup_loading_text", NULL);
    if (str == NULL)
    {
        strcpy(loadingText, "          ");
    }
    else
    {
        strcpy(loadingText, str);
    }

    memset(textToShow, 0, sizeof(textToShow));
    memset(afterText, 0, sizeof(afterText));
    strcpy(beforeText, loadingText);
    beforeText[spotToInsertColor] = '\0';
    memcpy(afterText, &loadingText[spotToInsertColor], strlen(loadingText) - spotToInsertColor);
    sprintf(textToShow, "{font=0}{h*2}{w*2}%s{color=FFFFFFFF}%s{~:c}", beforeText, afterText);

    RwCamera* cam;
    RwRGBA bg = { 0, 0, 0, 0 };
    cam = iCameraCreate(FB_XRES, FB_YRES, 0);
    RwCameraClear(cam, &bg, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
    RwCameraBeginUpdateWrapper(cam);

    xColor_tag c1 = { 0xFF, 0xE6, 0x00, 0xFF };
    static xtextbox backGroundTexture = xtextbox::create(
        xfont::create(0, NSCREENX(19.0f), NSCREENY(22.0f), 0.0f, c1, screen_bounds), screen_bounds,
        2, 0.0f, 0.0f, 0.0f, 0.0f);

    str = xTextFindString("startup_loading_background", NULL);
    if (str == NULL)
    {
        backGroundTexture.set_text("");
    }
    else
    {
        backGroundTexture.set_text(str);
    }

    backGroundTexture.bounds = screen_bounds;
    backGroundTexture.bounds.contract(0.1f);
    backGroundTexture.bounds.h = backGroundTexture.yextent(true);
    backGroundTexture.bounds.y = 0.5f - 0.5f * backGroundTexture.bounds.h;
    backGroundTexture.render(true);

    RenderText(textToShow, true);

    RwCameraEndUpdate(cam);
    iCameraShowRaster(cam);
    iCameraDestroy(cam);

    if (percentage != -2)
    {
        percentageDone += 10;
    }
}
