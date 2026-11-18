#include "iSystem.h"
#include "iAsync.h"
#include "iBoot.h"
#include "iConfig.h"
#include "iFile.h"
#include "iFMV.h"
#include "iHost.h"
#include "iLog.h"
#include "iPad.h"
#include "iPadHost.h"
#include "iPadStick.h"
#include "iSaveCLI.h"
#include "iSaveStore.h"
#include "iSaveText.h"
#include "iScreen.h"
#include "iSnd.h"
#include "iTime.h"
#include "iTRC.h"
#include "iWindow.h"
#include "isavegame.h"

#include <types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iCamera.h"
#include "xDebug.h"
#include "xFX.h"
#include "xMath.h"
#include "xMath3.h"
#include "xMemMgr.h"
#include "xPad.h"
#include "xShadow.h"
#include "xSnd.h"
#include "xString.h"
#include "xstransvc.h"

#include <rwcore.h>
#include <rpworld.h>
#include <rpcollis.h>
#include <rphanim.h>
#include <rpmatfx.h>
#include <rpptank.h>
#include <rpskin.h>
#include <rpusrdat.h>

#include "rw/backend.h"

// After the RenderWare headers: both name RwCamera in their signatures.
#include "iDistort.h"
#include "iGlow.h"
#include "iHDRGlow.h"
#include "iScreenWarp.h"
#include "iSnapshot.h"
#include "iCapture.h"
#include "iLowHeap.h"

// The overlay and the launcher (docs/GUI.md).
#include "gui/iGui.h"

// PC iSystem: the boot sequence of gc/iSystem.cpp on a host.
//
// The shape is TSSM's, which splits RenderWare startup in two where BfBB did it
// in one go: iSystemInit attaches the plugins (RwEngineInit), and
// iSystemInitGraphics, which zMain calls later, opens the device. The steps
// that are the console booting itself -- OSInit, DVDInit, VIInit, the FPU and
// memory-protection error handlers, the GX draw-sync callback -- are gone. What
// is added is treedome's: config.ini first, the asset folder checked before
// anything reads from it, and a window, opened where VIInit/VISetBlack were.
//
// Written for TSSM from treedome's pc/iSystem.cpp. Its BfBB features (toon,
// day/night, hipoly, fonts, cheats, menus, soundtrack, tour, load-screen
// tuning) are not here: none of those modules was imported.

// Retail waits on the vertical retrace, which is what paces the game. Until a
// present can wait for the display, sleeping to the next 60 Hz boundary is
// what that means on a 60 Hz screen (treedome).
#define IVSYNC_PERIOD_NS (1000000000 / 60)

static bool sVSyncStarted;
static U64 sNextVSync;

void iVSync()
{
    U64 now = iHostMonotonicNs();

    if (!sVSyncStarted)
    {
        sNextVSync = now;
        sVSyncStarted = true;
    }

    sNextVSync += IVSYNC_PERIOD_NS;

    // One slow frame must not become a burst of frames that do not sleep.
    if (sNextVSync < now)
    {
        sNextVSync = now;
        return;
    }

    iHostSleepUntilNs(sNextVSync);
}

void null_func()
{
}

// Retail's GX draw-sync callback, installed by iSystemInitGraphics. Empty
// there too.
void my_dsc(u16 dsc)
{
}

// TSSM's video mode is 512 wide on the console: the framebuffer is 512x480
// (NTSC) or 512x528 (PAL) and the video interface stretches it to the TV.
// There is no VI here, and the picture is drawn at the render size config.ini
// asks for (iScreen) with square pixels, so that is what FB_XRES/FB_YRES get.
// PAL is not a host concept; the PAL arm answers the same.
void iSystemGetWidthHeight(U32& uWidth, U32& uHeight, bool bPALMode)
{
    uWidth = (U32)iScreenWidth();
    uHeight = (U32)iScreenHeight();
}

static inline void TRCInit()
{
    iTRCDisk::Init();
    iTRCDisk::SetPadStopRumblingFunction(iPadStopRumble);
    iTRCDisk::SetSndSuspendFunction(iSndSuspend);
    iTRCDisk::SetSndResumeFunction(iSndResume);
    iTRCDisk::SetMovieSuspendFunction(iFMVSuspend);
    iTRCDisk::SetMovieResumeFunction(iFMVResume);
    iTRCDisk::SetAsyncLoadSuspendFunction(iAsyncSuspend);
    iTRCDisk::SetAsyncLoadResumeFunction(iAsyncResume);
    ResetButton::SetSndKillFunction(iSndKill);
    ResetButton::SetMovieKillFunction(iFMVKill);
}

// --- configuration ---------------------------------------------------------

static iWindowMode sWindowMode = iWINDOW_BORDERLESS;

static iWindowMode WindowModeFromConfig()
{
    const char* name = iConfigGetString("video.mode", "borderless");

    if (iHostStrCaseCmp(name, "fullscreen") == 0)
    {
        return iWINDOW_FULLSCREEN;
    }
    if (iHostStrCaseCmp(name, "borderless") == 0)
    {
        return iWINDOW_BORDERLESS;
    }
    if (iHostStrCaseCmp(name, "windowed") == 0)
    {
        return iWINDOW_WINDOWED;
    }

    printf("tssm: config: video.mode is not fullscreen, borderless or windowed, using the "
           "default: %s\n",
           name);
    return iWINDOW_BORDERLESS;
}

// How fast the game runs and whether the present waits for the display. After
// the window is open: `display` means the monitor the game landed on.
static void ApplyDisplayRateConfig()
{
    iWindowSetVSync(iConfigGetBool("video.vsync", TRUE));

    const char* rate = iConfigGetString("video.framerate", "60");
    S32 fps;

    if (iHostStrCaseCmp(rate, "display") == 0)
    {
        fps = iWindowGetDisplayRefreshRate();
        if (fps <= 0)
        {
            printf("tssm: config: video.framerate = display, but the monitor's refresh rate "
                   "could not be read; not capping\n");
            fps = 0;
        }
    }
    else if (iHostStrCaseCmp(rate, "off") == 0 || iHostStrCaseCmp(rate, "unlimited") == 0 ||
             iHostStrCaseCmp(rate, "none") == 0)
    {
        fps = 0;
    }
    else
    {
        fps = iConfigGetInt("video.framerate", 60);
        if (fps < 0)
        {
            printf("tssm: config: video.framerate cannot be negative, using the default: %s\n",
                   rate);
            fps = 60;
        }
    }

    iWindowSetFrameRate(fps);
    if (fps > 0)
    {
        printf("tssm: %d fps cap, vsync %s\n", (int)fps, iWindowGetVSync() ? "on" : "off");
    }
    else
    {
        printf("tssm: no fps cap, vsync %s\n", iWindowGetVSync() ? "on" : "off");
    }
}

static void ApplyConfig()
{
    sWindowMode = WindowModeFromConfig();

    // The render size, before the window opens at it: the height as set, and a
    // width of the aspect's shape (iScreen.h). Wide takes the display's shape,
    // which is also the shape of a window opened at the render size.
    {
        const char* aspectName = iConfigGetString("video.aspect", "wide");
        const iScreenAspect aspect = iScreenAspectFromName(aspectName);
        S32 shapeW = 0;
        S32 shapeH = 0;
        if (aspect == iSCREENASPECT_WIDE && !iWindowGetDisplaySize(&shapeW, &shapeH))
        {
            shapeW = 0;
            shapeH = 0;
        }
        S32 width = 0;
        S32 height = 0;
        iScreenSizeForAspect(aspect, iConfigGetInt("video.width", 640),
                             iConfigGetInt("video.height", 480), shapeW, shapeH, &width, &height);
        iScreenSetSize(width, height);
        printf("tssm: render size %dx%d (aspect %s)\n", (int)width, (int)height, aspectName);
    }
    iScreenSetMultiSample(iConfigGetInt("video.msaa", 1));
    iScreenSetFOV(iConfigGetFloat("video.fov", 75.0f));

    const char* backend = iConfigGetString("video.backend", "auto");
    if (iHostStrCaseCmp(backend, "d3d9") == 0)
    {
        iScreenSetBackend(iSCREENBACKEND_D3D9);
    }
    else if (iHostStrCaseCmp(backend, "d3d11") == 0)
    {
        iScreenSetBackend(iSCREENBACKEND_D3D11);
    }
    else if (iHostStrCaseCmp(backend, "gl3") == 0)
    {
        iScreenSetBackend(iSCREENBACKEND_GL3);
    }
    else if (iHostStrCaseCmp(backend, "vulkan") == 0)
    {
        iScreenSetBackend(iSCREENBACKEND_VULKAN);
    }
    else
    {
        if (iHostStrCaseCmp(backend, "auto") != 0)
        {
            printf("tssm: config: video.backend is not auto, d3d9, d3d11, gl3 or vulkan, using "
                   "the default: %s\n",
                   backend);
        }
        iScreenSetBackend(iSCREENBACKEND_AUTO);
    }

    const char* pipeline = iConfigGetString("video.pipeline", "auto");
    if (iHostStrCaseCmp(pipeline, "fixed") == 0)
    {
        iScreenSetPipeline(iSCREENPIPE_FIXED);
    }
    else if (iHostStrCaseCmp(pipeline, "shader") == 0)
    {
        iScreenSetPipeline(iSCREENPIPE_SHADER);
    }
    else
    {
        iScreenSetPipeline(iSCREENPIPE_AUTO);
    }

    iBootSetScene(iConfigGetString("game.boot", ""));
    iBootSetIntroMovies(iConfigGetBool("game.intro_movies", TRUE));

    // Where saves go, before xSaveGameInit resolves the root; empty is the
    // per-user data folder (isavegame.cpp).
    iSGSetSaveRoot(iConfigGetString("game.save_folder", ""));
    iBootSetCameraSensitivity(iConfigGetFloat("input.camera_sensitivity", 1.0f));

    const char* deadzone = iConfigGetString("input.deadzone", "auto");
    if (iHostStrCaseCmp(deadzone, "auto") == 0)
    {
        iPadStickSetDeadzone(-1.0f);
    }
    else
    {
        F32 percent = iConfigGetFloat("input.deadzone", -1.0f);
        if (percent < 0.0f || percent > 90.0f)
        {
            printf("tssm: config: input.deadzone is not auto or a percentage 0 to 90: %s\n",
                   deadzone);
            percent = -1.0f;
        }
        iPadStickSetDeadzone(percent);
    }

    // The BfBB Xbox screen passes, imported from treedome. TSSM's Xbox release
    // has a glow of its own (iXboxGlow.h, [extras] glow), so these start off
    // and nothing turns them on.
    iGlowSetEnabled(FALSE);
    iDistortSetEnabled(FALSE);
    iSnapshotSetEnabled(FALSE);

    // TSSM's own screen passes, the console's effects drawn with render targets.
    iHDRGlowSetEnabled(iConfigGetBool("video.hdr_glow", TRUE));
    iScreenWarpSetEnabled(iConfigGetBool("video.screen_warp", TRUE));

    const char* path = iConfigPath();
    printf("tssm: %s -- %dx%d %s, backend %s\n", path != NULL ? path : "no config.ini, defaults",
           (int)iScreenWidth(), (int)iScreenHeight(),
           sWindowMode == iWINDOW_FULLSCREEN ? "fullscreen" :
           sWindowMode == iWINDOW_BORDERLESS ? "borderless" :
                                               "windowed",
           backend);
}

// The asset folder, checked before anything reads from it. zMainLoadFontHIP
// spins on xSTLoadStep('FONT') with no exit, so a wrong path is a hang on a
// blank window unless it is caught here (treedome).
static void CheckAssets()
{
    const char* missing = iFileMissingAssetPath();
    if (missing != NULL)
    {
        const char* root = iFileAssetRoot();
        const char* env = getenv("TSSM_ASSETS");
        const char* config = iConfigPath();

        char where[768];
        if (env != NULL && env[0] != '\0')
        {
            snprintf(where, sizeof(where),
                     "TSSM_ASSETS is set to \"%s\", and overrides the settings file.", root);
        }
        else if (root[0] != '\0')
        {
            snprintf(where, sizeof(where), "[assets] path in %s is \"%s\".",
                     config != NULL ? config : "config.ini", root);
        }
        else
        {
            snprintf(where, sizeof(where), "[assets] path in %s is empty; set it there.",
                     config != NULL ? config : "config.ini");
        }

        char message[1536];
        snprintf(message, sizeof(message),
                 "The game's files were not found.\n\nLooked for:\n%s\n\n%s\n\n"
                 "The Xbox and the GameCube release both work. It has to name one of these "
                 "folders, not a disc image:\n\n"
                 "Xbox: the folder that directly contains boot.HIP, font.HIP and FMV\\, "
                 "extracted from your Xbox disc image (XISO) with an XISO extractor.\n\n"
                 "GameCube: after Dolphin's Extract Entire Disc (right-click the game), its "
                 "files\\ folder, or the folder holding files\\ and sys\\.\n\n"
                 "No assets ship with the port; they come from your own copy of the game.",
                 missing, where);

        printf("tssm: FATAL -- the game's files were not found.\n");
        printf("tssm:   looked for: %s\n", missing);
        printf("tssm:   %s\n", where);
        fflush(stdout);

        iHostErrorBox(TSSM_GAME_TITLE, message);
        exit(1);
    }

    // TSSM's packs name their platform in the PLAT chunk: "BX" on the Xbox,
    // "GC" and "PS2" elsewhere (heavyironmodding.org). The port reads the Xbox
    // release, and the GameCube one through the conversion each GameCube
    // package gets when it is opened (iMod.h). A PS2 set opens and then fails
    // far from the cause, so it is stopped here with the reason.
    //
    // TSSM's PLAT (HIP revision 3) has no long platform name after the id --
    // the second string is the region -- so the name is spelled out here.
    const char* region = NULL;
    const char* plat = iFileAssetPlatform(&region);
    const bool xbox = plat != NULL && (strcmp(plat, "BX") == 0 || strcmp(plat, "XB") == 0);
    const bool gamecube = plat != NULL && strcmp(plat, "GC") == 0;
    if (plat != NULL && !xbox && !gamecube)
    {
        const char* who = strcmp(plat, "PS2") == 0 ? "PlayStation 2" : plat;
        char message[1024];
        snprintf(message, sizeof(message),
                 "These are %s assets, and the port reads the Xbox and GameCube releases'.\n\n"
                 "Extract the Xbox or GameCube disc instead, and point [assets] path at the "
                 "folder holding boot.HIP.",
                 who);
        printf("tssm: FATAL -- these are %s assets; the port reads the Xbox and GameCube "
               "releases'.\n",
               who);
        fflush(stdout);
        iHostErrorBox(TSSM_GAME_TITLE, message);
        exit(1);
    }

    // Which release the game files are, for the log. A GameCube set's disc
    // header, when it is a Dolphin extraction (sys/ next to files/), names the
    // disc: GGVE78 is the USA release the port was measured on.
    if (plat != NULL)
    {
        char disc[16] = "";
        if (gamecube)
        {
            char path[600];
            snprintf(path, sizeof(path), "%s/../sys/boot.bin", iFileAssetRoot());
            FILE* fp = fopen(path, "rb");
            if (fp != NULL)
            {
                char id[6];
                if (fread(id, 1, sizeof(id), fp) == sizeof(id))
                {
                    snprintf(disc, sizeof(disc), ", disc %.6s", id);
                }
                fclose(fp);
            }
        }
        printf("tssm: game files: the %s release%s%s%s%s\n", gamecube ? "GameCube" : "Xbox",
               region != NULL ? " (" : "", region != NULL ? region : "", region != NULL ? ")" : "",
               disc);
        if (disc[0] != '\0' && strstr(disc, "GGVE78") == NULL)
        {
            printf("tssm:   (only the USA disc, GGVE78, has been tested)\n");
        }
        fflush(stdout);
    }
}

// --- RenderWare --------------------------------------------------------------

static RwVideoMode sVideoMode;

static RwTexture* TextureRead(const RwChar* name, const RwChar* maskName);

static U32 RWAttachPlugins()
{
    // TRUE on FAILURE: retail's convention.
    if (!RpWorldPluginAttach())
    {
        return TRUE;
    }
    if (!RpCollisionPluginAttach())
    {
        return TRUE;
    }
    if (!RpMatFXPluginAttach())
    {
        return TRUE;
    }
    if (!RpSkinPluginAttach())
    {
        return TRUE;
    }
    if (!RpHAnimPluginAttach())
    {
        return TRUE;
    }
    if (!RpUserDataPluginAttach())
    {
        return TRUE;
    }
    if (!RpPTankPluginAttach())
    {
        return TRUE;
    }
    return FALSE;
}

// The first half of retail's startup: the engine and its plugins, before
// RwEngineOpen freezes every object's plugin offsets.
//
// NULL memory functions rather than iMemMgr's: librw allocates with malloc,
// outside the game arena, so RenderWare's allocations do not count against
// the game heap here (treedome does the same; worth revisiting before memory
// is measured). The resource-arena size is retail's.
// RenderWare's allocator. On the console it is the game's memory manager
// (iMemMgrMalloc), so every RenderWare pointer is a 32-bit arena address and
// game code keeps them in U32s. The port uses the C library's heap, and on a
// 64-bit host that heap is above 4 GB; there it is iLowHeap's block below 4 GB
// instead. See iLowHeap.h.
static void* LowHeapMalloc(size_t size, RwUInt32)
{
    return iLowHeapMalloc(size);
}

static void* LowHeapRealloc(void* mem, size_t size, RwUInt32)
{
    return iLowHeapRealloc(mem, size);
}

static void* LowHeapCalloc(size_t count, size_t size, RwUInt32)
{
    return iLowHeapCalloc(count, size);
}

static const RwMemoryFunctions sLowHeapFunctions = { LowHeapMalloc, iLowHeapFree, LowHeapRealloc,
                                                     LowHeapCalloc };

static S32 RenderWareInit()
{
    // NULL is the C library's heap (rw/engine_start.cpp).
    const RwMemoryFunctions* memFuncs = NULL;
    if (iLowHeapWanted())
    {
        if (!iLowHeapInit())
        {
            printf("tssm:   no RenderWare heap below 4 GB\n");
            return TRUE;
        }
        memFuncs = &sLowHeapFunctions;
    }

    if (!RwEngineInit(memFuncs, 0, 0xAE000))
    {
        printf("tssm:   RwEngineInit failed\n");
        return TRUE;
    }
    RwResourcesSetArenaSize(0xAE000);
    if (RWAttachPlugins())
    {
        printf("tssm:   a RenderWare plugin failed to attach\n");
        return TRUE;
    }
    return FALSE;
}

static S32 RenderWareExit()
{
    iLowHeapReport("at exit");
    RwEngineStop();
    RwEngineClose();
    return RwEngineTerm();
}

// The second half, iRenderWareInit2 on the console: the device. The window is
// opened first, where retail blanked the video interface, because D3D needs a
// window to open on and GL3 creates its own inside librw (rw/engine_start.cpp).
static S32 iRenderWareInit2()
{
    iBackendResolve();

    iWindowParams windowParams;
    windowParams.title = TSSM_GAME_TITLE;
    windowParams.width = iScreenWidth();
    windowParams.height = iScreenHeight();
    windowParams.mode = sWindowMode;
    if (!iWindowOpen(&windowParams))
    {
        printf("tssm:   the window could not be opened\n");
        return TRUE;
    }

    // The console's RwGameCubeDeviceConfig has no host counterpart; the shim
    // builds librw's open parameters from the window and ignores this.
    RwEngineOpenParams params;
    params.displayID = NULL;
    if (!RwEngineOpen(&params))
    {
        printf("tssm:   RwEngineOpen failed -- see the line above for why\n");
        RwEngineTerm();
        return TRUE;
    }

    RwEngineGetVideoModeInfo(&sVideoMode, RwEngineGetCurrentVideoMode());
    if (!RwEngineStart())
    {
        printf("tssm:   RwEngineStart failed\n");
        RwEngineClose();
        RwEngineTerm();
        return TRUE;
    }

    RwTextureSetReadCallBack(TextureRead);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
    xShadowInit();
    xFXInit();
    RwTextureSetMipmapping(TRUE);
    RwTextureSetAutoMipmapping(TRUE);

    ApplyDisplayRateConfig();

    // The overlay hooks the present, so it goes in before the first frame.
    iGuiInit();

    // Retail clears both frame buffers to black before the video is enabled.
    RwCamera* camera = iCameraCreate(iScreenWidth(), iScreenHeight(), 0);
    if (camera != NULL)
    {
        RwRGBA black = { 0, 0, 0, 0 };
        RwCameraClear(camera, &black, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
        RwCameraShowRaster(camera, NULL, 1);
        iCameraDestroy(camera);
    }

    printf("tssm: RenderWare up, %dx%d\n", (int)sVideoMode.width, (int)sVideoMode.height);
    return FALSE;
}

// Resolves a texture a model's material names out of the game's asset store.
// Retail rejects a sub-8-bit raster unless the GameCube raster extension says
// it is CMPR; the port's rasters are what librw converted, so there is no
// extension to ask and the check is dropped (treedome).
static RwTexture* TextureRead(const RwChar* name, const RwChar* maskName)
{
    char tmpname[256];
    U32 tmpsize;

    snprintf(tmpname, sizeof(tmpname), "%s.rw3", name);
    RwTexture* result = (RwTexture*)xSTFindAsset(xStrHash(tmpname), &tmpsize);

    if (result != NULL)
    {
        strcpy(result->name, name);
        strcpy(result->mask, maskName);
    }
    else if (getenv("TSSM_TEX") != NULL)
    {
        printf("tssm: texture '%s' not in the asset store\n", name);
        fflush(stdout);
    }

    return result;
}

// --- one copy of the game per save folder -----------------------------------

// A second copy of the game on the same save folder is turned away, and the
// copy already running is brought to the front.
//
// Two copies never work: SDL hands a controller's input to a window that is not
// in front (iPadHostSDL.cpp asks it to, so a pad keeps working across alt-tab),
// so the copy behind played along with every press on whatever screen it was
// on -- a New Game, a memory card slot, an overwrite prompt -- and both saved
// to the same files. Starting the game while it runs, from the .bat or the exe,
// is easy to do; this makes it bring the game forward instead. (Copies on
// different folders can still run side by side; the one not in front then
// ignores the controller while another copy is in front, iPadHostSDL.cpp.)
//
// The claim is the save folder's (iSaveStoreClaimFolder), so a run with its own
// TSSM_SAVE_DIR, as the regression harness makes, is not stopped by the game
// being played, and the copy that holds it is the only one that repairs
// (iSGStartup) or writes those saves.
static void ClaimSaveFolder()
{
    U32 holder = 0;
    if (iSaveStoreClaimFolder(&holder) != 0)
    {
        return;
    }

    const S32 raised = iHostRaiseProcessWindow(holder);
    printf("tssm: the game is already running (process %u) with the saves in %s; this copy is "
           "closing%s\n",
           (unsigned)holder, iSaveStoreRoot(),
           raised == 1 ? " and bringing that one to the front" : "");
    fflush(stdout);

    if (raised != 1)
    {
        char message[1024];
        snprintf(message, sizeof(message),
                 TSSM_GAME_TITLE " is already running, with the saves in\n%s\n\n"
                 "Only one copy can run at a time: both would take every controller press, "
                 "and their saves would overwrite each other.\n\n%s",
                 iSaveStoreRoot(),
                 raised == 2 ? "That copy is not responding. End tssm.exe in Task Manager, then "
                               "start the game again." :
                               "If you cannot see it, end tssm.exe in Task Manager, then start the "
                               "game again.");
        iHostErrorBox(TSSM_GAME_TITLE, message);
    }
    exit(0);
}

// --- the entry points ------------------------------------------------------

void iSystemInit(U32 options)
{
    // Before anything that may want a setting, and before iFileInit reads
    // where the assets are.
    iConfigLoad();

    // tssm --saves, --import-save, --export-save and the rest: a save tool run
    // from the command line does its job and ends the process here, before a
    // window, the assets or the game (iSaveCLI.h).
    S32 saveTool = iSaveCLIRun();
    if (saveTool >= 0)
    {
        fflush(stdout);
        exit(saveTool);
    }

    // The game, then: its output, from the banner on, goes to last_run.log
    // too (iLog.h), the last run's kept as last_run.prev.log.
    iLogStart();

    // The launcher, when [gui] launcher asks for it or the game's files are not
    // where the settings say: before anything takes a setting it can change.
    iGuiLauncherRun();

    ApplyConfig();

    // TSSM_CAPTURE_FRAMES / TSSM_EXIT_AFTER: off unless asked for.
    iCaptureInit();

    xDebugInit();
    xMemInit();
    iFileInit();
    CheckAssets();

    // After iFileInit, which settles whether a mod's save folder is in use,
    // and before the window opens or the saves are looked at (iSGStartup).
    ClaimSaveFolder();
    iSGSetNoGamesHook(iSaveTextNoGames);
    iTimeInit();
    xPadInit();
    printf("tssm: platform layer up, input backend: %s\n", iPadHostName());

    if (RenderWareInit())
    {
        // Retail OSPanics. Continuing would fault in the first thing that
        // touches the engine, far from here.
        printf("tssm: FATAL -- RenderWare failed to start\n");
        exit(1);
    }

    xSndMgrInit();
    TRCInit();
    xMathInit();
    xMath3Init();
}

void iSystemInitGraphics()
{
    if (iRenderWareInit2())
    {
        printf("tssm: FATAL -- the render device failed to open\n");
        exit(1);
    }

    // Retail calls these a second time here, after iRenderWareInit2 already
    // did; kept as retail has it.
    xShadowInit();
    xFXInit();
}

void iSystemExit()
{
    iGuiShutdown();
    xDebugExit();
    xMathExit();
    RenderWareExit();
    xSndMgrExit();
    xPadKill();
    iFileExit();
    iTimeExit();
    xMemExit();
    iWindowClose();

    // Retail ends on OSPanic ("This the end, my only friend, The End."); a host
    // process returns to whoever called it.
}

// The fail-safe pair brackets the disc-error screen's own use of RenderWare on
// the console. There is no such screen here.
void iSystemInitRWFailSafe()
{
}

void iSystemExitRWFailSafe()
{
}

// iSystemSleep suspends the thread on an alarm on the console; iSnd uses it
// while a sound bank loads.
void iSystemSleep(S32 milliSecondsToSleep)
{
    if (milliSecondsToSleep > 0)
    {
        iHostSleepUntilNs(iHostMonotonicNs() + (U64)milliSecondsToSleep * 1000000ULL);
    }
}

// --- ARAM ------------------------------------------------------------------
// On the console, 16 MB of audio RAM the CPU cannot address, split between
// FMOD (0xD00000 bytes) and 32 blocks for the movie player. A host has one
// address space, so a block is just host memory. The FMOD region has no
// counterpart: the port's sound does not run on FMOD.

void iSystemARAMInit()
{
}

U32 iSystemARAMBasePtrForFMOD()
{
    return 0;
}

U32 iSystemARAMSizeForFMOD()
{
    return 0;
}

void* iSystemARAMAlloc(U32 numbytes_to_use)
{
    return malloc((numbytes_to_use + 31) & ~31);
}

void iSystemARAMFree(void* ptr)
{
    free(ptr);
}
