// Debug events for unattended runs; see iTestEvents.h. Port-only, compiled
// with the game units.

#include "iTestEvents.h"

#include "iCoop.h"
#include "gui/iGui.h"

#include "xBound.h"
#include "xEnt.h"
#include "xMath3.h"
#include "xScreenWarp.h"
#include "zCam.h"
#include "zCamera.h"
#include "zGlobals.h"
#include "zGame.h"
#include "zNMECommon.h"
#include "zPlayer.h"
#include "zPortal.h"
#include "zSaveLoad.h"
#include "zGameState.h"
#include "zScene.h"
#include "xSaveGame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <thread>

namespace
{
    struct WarpEvent
    {
        S32 frame;
        xScreenWarp::type_enum type;
        F32 radius;
        F32 seconds;
        F32 intensity;
    };

    const S32 kMaxWarps = 32;

    WarpEvent sWarps[kMaxWarps];
    S32 sWarpCount;
    bool sParsed;

    // One entry of TSSM_TEST_PLACE: put the player at pos, facing yaw degrees.
    struct PlaceEvent
    {
        S32 frame;
        xVec3 pos;
        F32 yaw;
    };

    // A benchmark (tools/android_bench.py) visits a level's spots in turn.
    const S32 kMaxPlaces = 64;

    PlaceEvent sPlaces[kMaxPlaces];
    S32 sPlaceCount;

    // TSSM_TEST_PLACE_CAMERA=reset: reset the camera behind the placed player.
    bool sPlaceCamReset;

    // TSSM_TEST_SCENE=frame:SCEN: go to another level at that frame, as the
    // game's soak test does (a portal of our own, whatever the save has
    // unlocked). For tools/coop_test.py's following test.
    S32 sSceneFrame;
    U32 sSceneId;

    // TSSM_TEST_KILL=frame:radius: at that frame, defeat the nearest living
    // enemy within radius of the player, through the game's own damage
    // (an instant kill), so its own death plays out.
    S32 sKillFrame;
    F32 sKillRadius;

    // TSSM_TEST_PLACE_ON=frame:id[:dy[:yaw]]: at that frame, the player is put
    // on top of the entity with that asset ID (hex), wherever it is then:
    // above the middle of its bound, dy units over its top (1 by default).
    // For tools/coop_test.py's riding test (a moving platform).
    S32 sPlaceOnFrame;
    U32 sPlaceOnId;
    F32 sPlaceOnDy = 1.0f;
    F32 sPlaceOnYaw;

    // TSSM_TEST_COOP_GO=frame: at that frame, what the Co-op tab's "Go to the
    // host's level" button does (tools/coop_test.py, with following off).
    S32 sCoopGoFrame;

    // M5: TSSM_TEST_BLACKOUT=frame:seconds: at that frame, co-op's I/O thread
    // drops everything for that long, as a pulled cable would (the reconnect
    // tests). TSSM_TEST_COOP_KICK=frame: the Co-op tab's Kick.
    S32 sBlackoutFrame;
    F32 sBlackoutSeconds;
    S32 sKickFrame;

    // TSSM_TEST_AUTOSAVE=frame[,frame...]: at each, the game's own autosave
    // as a token pickup asks for it (tools/coop_test.py: a co-op guest's
    // game must not save).
    const S32 kMaxAutoSaves = 4;
    S32 sAutoSaveFrames[kMaxAutoSaves];
    S32 sAutoSaveCount;

    // TSSM_TEST_SAVE=frame: at that frame, the save screen as the pause menu
    // opens it, saving straight into the first slot (tools/coop_test.py: a
    // co-op guest's save by hand is refused); then, once the game is back in
    // play, a log line says so.
    S32 sSaveFrame;
    bool sSaveWaiting;

    // TSSM_TEST_QUIT=frame: at that frame, the game is left for the title
    // menu, as the pause menu's quit leaves it (the game's own exit event).
    // For tools/coop_test.py's title variant: the title draws its scene
    // without co-op's frame, so nothing of the last level may be drawn there.
    S32 sQuitFrame;

    // TSSM_TEST_EVENT=frame:id:what[,...]: at that frame, the game's own event
    // to the level's object with that asset ID (hex): "teleport" (a portal's
    // eEventTeleportPlayer, as walking into it), "cutscene" (a cutscene
    // manager's start, as a trigger sends it) or "run" (a fly-through's).
    // For tools/coop_test.py's level change and cutscene tests (M4).
    const S32 kMaxGameEvents = 8;
    S32 sEventFrames[kMaxGameEvents];
    U32 sEventIds[kMaxGameEvents];
    U32 sEventWhat[kMaxGameEvents];
    S32 sEventCount;
    S32 sEventWaitLogged;

    // The game is in plain play: no pause, task or save screen, no level
    // change under way. Only then does the game's own scene update start a
    // level change (zSceneUpdate), so a test's level change waits for it.
    bool InPlainPlay()
    {
        return gGameMode == eGameMode_Game && gGameState == eGameState_Play && !zGameIsPaused() &&
               globals.sceneCur != NULL && globals.sceneCur->pendingPortal == NULL;
    }

    // TSSM_TEST_STALL=frame:seconds[,...]: the game thread sleeps at that
    // frame, as a held window stops it (co-op's stall tests).
    const S32 kMaxStalls = 4;
    S32 sStallFrames[kMaxStalls];
    F32 sStallSeconds[kMaxStalls];
    S32 sStallCount;

    // M5 fix round: TSSM_TEST_COOP_TAB=frame:actions[;frame:actions...]: at
    // each frame, what the Co-op tab's fields, buttons and switches do
    // (iGuiCoopTest; tools/coop_test.py's tab variant).
    const S32 kMaxTabSteps = 24;
    S32 sTabFrames[kMaxTabSteps];
    char sTabActions[kMaxTabSteps][160];
    S32 sTabCount;

    long long ClockMs()
    {
        // The clock co-op's logs use (iCoop.cpp's NowNs).
        using namespace std::chrono;
        return (long long)(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count() / 1000000);
    }

    void Stall(S32 frame, F32 seconds)
    {
        long long from = ClockMs();
        printf("tssm: test: frame %d: the game thread stops for %.2f s (TSSM_TEST_STALL) at clock ms %lld\n",
               (int)frame, seconds, from);
        fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds((long long)(seconds * 1000.0f)));
        long long to = ClockMs();
        printf("tssm: test: frame %d: the game thread runs again at clock ms %lld (stopped %lld ms)\n", (int)frame,
               to, to - from);
        fflush(stdout);
    }

    void AutoSave(S32 frame)
    {
        XSGAutoData* a = xSGAutoSave_GetCache();
        if (a == NULL)
        {
            return;
        }
        // No save slot was picked (the run booted straight into a level):
        // the first one, as if the game had been saved there.
        if (!a->IsValid() && !a->SetCache(0, 0, 0))
        {
            printf("tssm: test: frame %d: no save slot for the autosave\n", (int)frame);
            return;
        }
        globals.autoSaveFeature = 1;
        printf("tssm: test: frame %d: autosave asked (TSSM_TEST_AUTOSAVE)\n", (int)frame);
        fflush(stdout);
        zSaveLoadPreAutoSave(true);
        zSaveLoad_TriggerAutoSave();
        printf("tssm: test: frame %d: autosave done; the slot is %s\n", (int)frame,
               a->IsValid() ? "kept (saved)" : "not kept (not saved)");
        fflush(stdout);
    }

    void Kill(S32 frame)
    {
        xEnt* player = (xEnt*)xglobals->___player_ent_dont_use_directly;
        zScene* sc = globals.sceneCur;
        if (player == NULL || player->model == NULL || sc == NULL || sc->base == NULL)
        {
            return;
        }
        const xVec3& me = *(const xVec3*)&player->model->Mat->pos;
        zNMECommon* best = NULL;
        F32 bestD = sKillRadius * sKillRadius;
        for (U32 i = 0; i < sc->num_base; i++)
        {
            xBase* b = sc->base[i];
            if (b == NULL || b->baseType != eBaseTypeNPC || !(b->baseFlags & 0x1))
            {
                continue;
            }
            zNMECommon* npc = static_cast<zNMECommon*>((xEnt*)b);
            if (npc->model == NULL || !npc->IsAlive() || !xEntIsVisible(npc))
            {
                continue;
            }
            const xVec3& p = *(const xVec3*)&npc->model->Mat->pos;
            F32 d = (p.x - me.x) * (p.x - me.x) + (p.y - me.y) * (p.y - me.y) + (p.z - me.z) * (p.z - me.z);
            if (d < bestD)
            {
                bestD = d;
                best = npc;
            }
        }
        if (best == NULL)
        {
            printf("tssm: test: frame %d: TSSM_TEST_KILL found no enemy within %.1f\n", (int)frame,
                   (double)sKillRadius);
            return;
        }
        printf("tssm: test: frame %d: TSSM_TEST_KILL defeats enemy %08x (type %d)\n", (int)frame,
               (unsigned)best->id, (int)best->SelfType());
        best->Damage(DMGTYP_INSTAKILL, player, NULL, -1, ePowerupLevelNormal);
    }
    xPortalAsset sScenePortalAsset;
    _zPortal sScenePortal;

    // TSSM_TEST_CRASH as a frame number (above 1).
    S32 sCrashFrame;

    // TSSM_TEST_HOLD=first-last: from first to last, put the player back at the latest
    // TSSM_TEST_PLACE spot every frame (camera included), so the view stays still while the
    // world goes on: a conveyor cannot carry the player off, and two frames a few apart differ
    // only by what animates.
    S32 sHoldFirst;
    S32 sHoldLast;

    // TSSM_TEST_POWERUP=frame:level: at that frame, set every move's power-up (PowerUp[0..7])
    // to level (1 normal, 2 upgraded), e.g. for Patrick's upgraded cartwheel and its shield.
    S32 sPowerFrame;
    S32 sPowerLevel;

    // TSSM_TEST_UPGRADE=frame:index[,frame:index]: at that frame, the pause menu's purchase of an
    // upgrade of move `index` (0-9): eEventUpgradePowerUp to the player, as the menu's dispatcher
    // sends it.
    S32 sUpgradeFrame[2];
    S32 sUpgradeIndex[2];

    // TSSM_TEST_HEALTH=frame:n: at that frame, the player's health becomes n (1 to its
    // maximum), e.g. one hit from defeat, for tools/coop_test.py's respawn test; 0 defeats it
    // (the game's own eEventKill).
    S32 sHealthFrame;
    S32 sHealthValue;
    // TSSM_TEST_HEALTH_KEEP=n: the player's health is put back to its maximum whenever it falls
    // below n (a test's bystander that must not be defeated, so its level is not reset).
    S32 sHealthKeep;
    // TSSM_TEST_RESET=frame: at that frame (or the first plain-play frame after), the level reset
    // a lost chance does, as an out-of-bounds fall ends in (tools/coop_test.py's keep-reset).
    S32 sResetFrame;

    // TSSM_TEST_FACE=first-last/every:id:dist: every `every` frames from first to last, the
    // player is put dist units from the entity with that asset ID (hex; an enemy too),
    // on its side of it, facing it, for tools/coop_test.py's fight (so a scripted attack
    // reaches an enemy that moves).
    S32 sFaceFirst, sFaceLast, sFaceEvery;
    U32 sFaceId;
    F32 sFaceDist;
    S32 sFaceCount;

    // TSSM_TEST_CAMLOG=n: every n frames, log where the game's camera is and whether a level
    // fly-through (zCameraFly) is playing, for tools/regress.py's fly-through check.
    S32 sCamLogEvery;

    struct TypeName
    {
        const char* name;
        xScreenWarp::type_enum type;
    };

    const TypeName kTypes[] = {
        { "warble", xScreenWarp::TYPE_WARBLE },
        { "lens", xScreenWarp::TYPE_LENS },
        { "ripple", xScreenWarp::TYPE_RIPPLE },
        { "shockwave", xScreenWarp::TYPE_SHOCKWAVE },
        { "heat", xScreenWarp::TYPE_HEAT_SHIMMER },
        { "static", xScreenWarp::TYPE_STATIC_LENS },
    };

    void ParseWarps(const char* list)
    {
        char buf[1024];
        strncpy(buf, list, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        for (char* entry = strtok(buf, ","); entry != NULL; entry = strtok(NULL, ","))
        {
            if (sWarpCount == kMaxWarps)
            {
                printf("tssm: test: TSSM_TEST_WARP has more than %d entries; the rest are "
                       "ignored\n",
                       (int)kMaxWarps);
                break;
            }

            char name[32] = "";
            WarpEvent e;
            e.frame = 0;
            e.radius = 4.0f;
            e.seconds = 1.0f;
            e.intensity = 1.0f;

            const int got = sscanf(entry, "%d:%31[a-z]:%f:%f:%f", &e.frame, name, &e.radius,
                                   &e.seconds, &e.intensity);
            e.type = xScreenWarp::TYPE_INVALID;
            for (size_t i = 0; i < sizeof(kTypes) / sizeof(kTypes[0]); i++)
            {
                if (strcmp(name, kTypes[i].name) == 0)
                {
                    e.type = kTypes[i].type;
                }
            }

            if (got < 2 || e.frame < 1 || e.type == xScreenWarp::TYPE_INVALID || e.radius <= 0.0f ||
                e.seconds <= 0.0f)
            {
                printf("tssm: test: TSSM_TEST_WARP: '%s' is not "
                       "frame:type[:radius[:seconds[:intensity]]]; ignored\n",
                       entry);
                continue;
            }

            sWarps[sWarpCount++] = e;
        }
        fflush(stdout);
    }

    void ParsePlaces(const char* list)
    {
        static char buf[4096];
        strncpy(buf, list, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        for (char* entry = strtok(buf, ","); entry != NULL; entry = strtok(NULL, ","))
        {
            if (sPlaceCount == kMaxPlaces)
            {
                printf("tssm: test: TSSM_TEST_PLACE has more than %d entries; the rest are "
                       "ignored\n",
                       (int)kMaxPlaces);
                break;
            }

            PlaceEvent e;
            e.yaw = 0.0f;
            const int got =
                sscanf(entry, "%d:%f:%f:%f:%f", &e.frame, &e.pos.x, &e.pos.y, &e.pos.z, &e.yaw);
            if (got < 4 || e.frame < 1)
            {
                printf("tssm: test: TSSM_TEST_PLACE: '%s' is not frame:x:y:z[:yaw]; ignored\n",
                       entry);
                continue;
            }

            sPlaces[sPlaceCount++] = e;
        }
        fflush(stdout);
    }

    // As the game's own checkpoint restore (zScene.cpp's zEntPlayer_LoadCheckPoint) puts the
    // player down: the frame, its rotation, the model matrix and the camera.
    void Place(S32 frame, const PlaceEvent& e, bool quiet = false)
    {
        zPlayer* player = xglobals->___player_ent_dont_use_directly;
        if (player == NULL || player->frame == NULL || player->model == NULL)
        {
            printf("tssm: test: frame %d: no player to place\n", (int)frame);
            return;
        }

        xEntFrame* f = player->frame;
        const xVec3 was = f->mat.pos;
        const F32 yaw = e.yaw * (3.14159265f / 180.0f);

        f->mat.pos = e.pos;
        f->rot.angle = yaw;
        f->rot.axis = xVec3::m_UnitAxisY;
        xMat3x3Euler(&f->mat, yaw, 0.0f, 0.0f);
        *(xMat4x3*)player->model->Mat = f->mat;
        f->oldmat = f->mat;
        // TSSM_TEST_PLACE_CAMERA=reset: as the game's own same-level teleport
        // (zSceneSwitch), the camera is put behind the player; without it a
        // long move can leave the camera where it was. Off by default, so
        // regress's placed runs keep their reference frames.
        if (sPlaceCamReset)
        {
            zCamReset(f->mat);
        }
        player->SetCamera();

        if (quiet)
        {
            return;
        }
        printf("tssm: test: frame %d: player moved from (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f), "
               "facing %.0f degrees\n",
               (int)frame, was.x, was.y, was.z, e.pos.x, e.pos.y, e.pos.z, e.yaw);
        fflush(stdout);
    }
} // namespace

void iTestEventsFrame(S32 frame)
{
    if (!sParsed)
    {
        sParsed = true;
        const char* warps = getenv("TSSM_TEST_WARP");
        if (warps != NULL && warps[0] != '\0')
        {
            ParseWarps(warps);
        }
        const char* places = getenv("TSSM_TEST_PLACE");
        if (places != NULL && places[0] != '\0')
        {
            ParsePlaces(places);
        }
        const char* scene = getenv("TSSM_TEST_SCENE");
        char sceneName[8] = "";
        if (scene != NULL && sscanf(scene, "%d:%4[A-Z0-9]", &sSceneFrame, sceneName) == 2 &&
            strlen(sceneName) == 4 && sSceneFrame > 1)
        {
            sSceneId = ((U32)sceneName[0] << 24) | ((U32)sceneName[1] << 16) | ((U32)sceneName[2] << 8) |
                       (U32)sceneName[3];
        }
        else
        {
            sSceneFrame = 0;
        }
        const char* kill = getenv("TSSM_TEST_KILL");
        if (kill == NULL || sscanf(kill, "%d:%f", &sKillFrame, &sKillRadius) != 2 || sKillFrame < 2 ||
            !(sKillRadius > 0.0f))
        {
            sKillFrame = 0;
        }
        const char* placeOn = getenv("TSSM_TEST_PLACE_ON");
        unsigned placeId = 0;
        if (placeOn != NULL &&
            sscanf(placeOn, "%d:%x:%f:%f", &sPlaceOnFrame, &placeId, &sPlaceOnDy, &sPlaceOnYaw) >= 2 &&
            sPlaceOnFrame > 1)
        {
            sPlaceOnId = placeId;
        }
        else
        {
            sPlaceOnFrame = 0;
        }
        const char* face = getenv("TSSM_TEST_FACE");
        unsigned faceId = 0;
        if (face != NULL && sscanf(face, "%d-%d/%d:%x:%f", &sFaceFirst, &sFaceLast, &sFaceEvery, &faceId,
                                   &sFaceDist) == 5 &&
            sFaceFirst > 1 && sFaceLast >= sFaceFirst && sFaceEvery > 0 && sFaceDist > 0.0f)
        {
            sFaceId = faceId;
        }
        else
        {
            sFaceFirst = 0;
        }
        const char* coopGo = getenv("TSSM_TEST_COOP_GO");
        if (coopGo != NULL && atoi(coopGo) > 1)
        {
            sCoopGoFrame = atoi(coopGo);
        }
        const char* blackout = getenv("TSSM_TEST_BLACKOUT");
        if (blackout != NULL && (sscanf(blackout, "%d:%f", &sBlackoutFrame, &sBlackoutSeconds) != 2 ||
                                 sBlackoutFrame < 2 || !(sBlackoutSeconds > 0.0f) || sBlackoutSeconds > 300.0f))
        {
            printf("tssm: test: TSSM_TEST_BLACKOUT is not frame:seconds (up to 300); ignored\n");
            sBlackoutFrame = 0;
        }
        const char* kick = getenv("TSSM_TEST_COOP_KICK");
        if (kick != NULL && atoi(kick) > 1)
        {
            sKickFrame = atoi(kick);
        }
        const char* autoSave = getenv("TSSM_TEST_AUTOSAVE");
        if (autoSave != NULL)
        {
            char buf[128];
            strncpy(buf, autoSave, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            for (char* entry = strtok(buf, ","); entry != NULL && sAutoSaveCount < kMaxAutoSaves;
                 entry = strtok(NULL, ","))
            {
                S32 f = atoi(entry);
                if (f > 1)
                {
                    sAutoSaveFrames[sAutoSaveCount++] = f;
                }
            }
        }
        const char* save = getenv("TSSM_TEST_SAVE");
        if (save != NULL && atoi(save) > 1)
        {
            sSaveFrame = atoi(save);
        }
        const char* quit = getenv("TSSM_TEST_QUIT");
        if (quit != NULL && atoi(quit) > 1)
        {
            sQuitFrame = atoi(quit);
        }
        const char* gameEvents = getenv("TSSM_TEST_EVENT");
        if (gameEvents != NULL)
        {
            char buf[256];
            strncpy(buf, gameEvents, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            for (char* entry = strtok(buf, ","); entry != NULL && sEventCount < kMaxGameEvents;
                 entry = strtok(NULL, ","))
            {
                int f = 0;
                unsigned id = 0;
                char what[16] = "";
                U32 ev = 0;
                if (sscanf(entry, "%d:%x:%15[a-z]", &f, &id, what) == 3 && f > 1)
                {
                    ev = strcmp(what, "teleport") == 0 ? (U32)eEventTeleportPlayer
                                                       : (strcmp(what, "cutscene") == 0
                                                              ? 0xbeu
                                                              : (strcmp(what, "run") == 0 ? (U32)eEventRun : 0u));
                }
                if (ev != 0)
                {
                    sEventFrames[sEventCount] = f;
                    sEventIds[sEventCount] = id;
                    sEventWhat[sEventCount] = ev;
                    sEventCount++;
                }
                else
                {
                    printf("tssm: test: TSSM_TEST_EVENT entry '%s' is not frame:id:teleport|cutscene|run; ignored\n",
                           entry);
                }
            }
        }
        const char* stall = getenv("TSSM_TEST_STALL");
        if (stall != NULL)
        {
            char buf[128];
            strncpy(buf, stall, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            for (char* entry = strtok(buf, ","); entry != NULL && sStallCount < kMaxStalls; entry = strtok(NULL, ","))
            {
                int f = 0;
                float sec = 0.0f;
                if (sscanf(entry, "%d:%f", &f, &sec) == 2 && f > 1 && sec >= 0.05f && sec <= 120.0f)
                {
                    sStallFrames[sStallCount] = f;
                    sStallSeconds[sStallCount] = sec;
                    sStallCount++;
                }
                else
                {
                    printf("tssm: test: TSSM_TEST_STALL entry '%s' is not frame:seconds (0.05-120); ignored\n", entry);
                }
            }
        }
        const char* tab = getenv("TSSM_TEST_COOP_TAB");
        if (tab != NULL)
        {
            char buf[4096];
            strncpy(buf, tab, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            for (char* entry = strtok(buf, ";"); entry != NULL && sTabCount < kMaxTabSteps; entry = strtok(NULL, ";"))
            {
                char* colon = strchr(entry, ':');
                int f = atoi(entry);
                if (colon != NULL && f > 1 && strlen(colon + 1) < sizeof(sTabActions[0]))
                {
                    sTabFrames[sTabCount] = f;
                    snprintf(sTabActions[sTabCount], sizeof(sTabActions[0]), "%s", colon + 1);
                    sTabCount++;
                }
                else
                {
                    printf("tssm: test: a TSSM_TEST_COOP_TAB entry is not frame:actions; ignored\n");
                }
            }
        }
        const char* placeCam = getenv("TSSM_TEST_PLACE_CAMERA");
        sPlaceCamReset = placeCam != NULL && strcmp(placeCam, "reset") == 0;
        const char* crash = getenv("TSSM_TEST_CRASH");
        if (crash != NULL && atoi(crash) > 1)
        {
            sCrashFrame = atoi(crash);
        }
        const char* health = getenv("TSSM_TEST_HEALTH");
        if (health != NULL && (sscanf(health, "%d:%d", &sHealthFrame, &sHealthValue) != 2 ||
                               sHealthFrame < 2 || sHealthValue < 0))
        {
            sHealthFrame = 0;
        }
        const char* power = getenv("TSSM_TEST_POWERUP");
        if (power != NULL && sscanf(power, "%d:%d", &sPowerFrame, &sPowerLevel) != 2)
        {
            sPowerFrame = 0;
        }
        const char* reset = getenv("TSSM_TEST_RESET");
        sResetFrame = reset != NULL && atoi(reset) > 1 ? atoi(reset) : 0;
        const char* keep = getenv("TSSM_TEST_HEALTH_KEEP");
        sHealthKeep = keep != NULL ? atoi(keep) : 0;
        const char* upgrade = getenv("TSSM_TEST_UPGRADE");
        S32 got = upgrade != NULL ?
                      sscanf(upgrade, "%d:%d,%d:%d", &sUpgradeFrame[0], &sUpgradeIndex[0],
                             &sUpgradeFrame[1], &sUpgradeIndex[1]) :
                      0;
        for (S32 i = 0; i < 2; i++)
        {
            if (got < 2 * (i + 1) || sUpgradeIndex[i] < 0 || sUpgradeIndex[i] > 9)
            {
                sUpgradeFrame[i] = 0;
            }
        }
        const char* camLog = getenv("TSSM_TEST_CAMLOG");
        if (camLog != NULL && atoi(camLog) > 0)
        {
            sCamLogEvery = atoi(camLog);
        }
        const char* hold = getenv("TSSM_TEST_HOLD");
        if (hold != NULL && sscanf(hold, "%d-%d", &sHoldFirst, &sHoldLast) == 2)
        {
            printf("tssm: test: holding the player still from frame %d to %d\n", (int)sHoldFirst,
                   (int)sHoldLast);
        }
    }

    if (sCrashFrame != 0 && frame == sCrashFrame)
    {
        printf("tssm: test: frame %d: TSSM_TEST_CRASH, a write through a null pointer\n",
               (int)frame);
        *(volatile int*)0 = 1;
    }

    // In plain play only, as TSSM_TEST_EVENT's teleport (below): a level
    // change asked for while the game is paused never starts.
    if (sSceneFrame != 0 && frame >= sSceneFrame && InPlainPlay())
    {
        sSceneFrame = 0;
        memset(&sScenePortalAsset, 0, sizeof(sScenePortalAsset));
        sScenePortalAsset.sceneID = sSceneId;
        memset(&sScenePortal, 0, sizeof(sScenePortal));
        sScenePortal.passet = &sScenePortalAsset;
        gLevelChanged = (U8)(globals.sceneCur->sceneID >> 24) != (U8)(sSceneId >> 24);
        globals.sceneCur->pendingPortal = &sScenePortal;
        printf("tssm: test: frame %d: going to another level (TSSM_TEST_SCENE)\n", (int)frame);
        fflush(stdout);
    }

    if (sPlaceOnFrame != 0 && frame == sPlaceOnFrame)
    {
        xBase* b = zSceneFindObject(sPlaceOnId);
        xEnt* ent = b != NULL && (b->baseType == eBaseTypePlatform || b->baseType == eBaseTypePendulum ||
                                  b->baseType == eBaseTypeStatic)
                        ? (xEnt*)b
                        : NULL;
        if (ent == NULL || ent->model == NULL)
        {
            printf("tssm: test: frame %d: TSSM_TEST_PLACE_ON: no entity %08x\n", (int)frame,
                   (unsigned)sPlaceOnId);
        }
        else
        {
            xBox box;
            xBoundGetBox(box, ent->bound);
            PlaceEvent e;
            e.frame = frame;
            e.pos.x = 0.5f * (box.lower.x + box.upper.x);
            e.pos.y = box.upper.y + sPlaceOnDy;
            e.pos.z = 0.5f * (box.lower.z + box.upper.z);
            e.yaw = sPlaceOnYaw;
            printf("tssm: test: frame %d: on entity %08x (its bound %.2f %.2f %.2f to %.2f %.2f %.2f)\n",
                   (int)frame, (unsigned)sPlaceOnId, box.lower.x, box.lower.y, box.lower.z, box.upper.x,
                   box.upper.y, box.upper.z);
            Place(frame, e);
        }
        fflush(stdout);
    }

    if (sFaceFirst != 0 && frame >= sFaceFirst && frame <= sFaceLast && (frame - sFaceFirst) % sFaceEvery == 0)
    {
        xBase* b = zSceneFindObject(sFaceId);
        xEnt* ent = b != NULL ? (xEnt*)b : NULL;
        zPlayer* player = xglobals->___player_ent_dont_use_directly;
        if (ent != NULL && ent->model != NULL && ent->model->Mat != NULL && player != NULL && player->model != NULL &&
            xEntIsVisible(ent))
        {
            const xVec3& at = *(const xVec3*)&ent->model->Mat->pos;
            const xVec3& me = *(const xVec3*)&player->model->Mat->pos;
            F32 dx = me.x - at.x, dz = me.z - at.z;
            F32 len = xsqrt(dx * dx + dz * dz);
            if (len < 0.01f)
            {
                dx = 0.0f;
                dz = 1.0f;
                len = 1.0f;
            }
            PlaceEvent e;
            e.frame = frame;
            e.pos.x = at.x + dx / len * sFaceDist;
            e.pos.y = (me.y > at.y ? me.y : at.y) + 0.25f;
            e.pos.z = at.z + dz / len * sFaceDist;
            e.yaw = xatan2(-dx, -dz) * (180.0f / 3.14159265f);
            if (sFaceCount++ < 3)
            {
                printf("tssm: test: frame %d: facing entity %08x from %.1f units (TSSM_TEST_FACE)\n", (int)frame,
                       (unsigned)sFaceId, sFaceDist);
            }
            Place(frame, e, true);
        }
    }

    if (sCoopGoFrame != 0 && frame == sCoopGoFrame)
    {
        printf("tssm: test: frame %d: the Co-op tab's \"Go to the host's level\" (TSSM_TEST_COOP_GO)\n",
               (int)frame);
        fflush(stdout);
        iCoopGoToHost();
    }

    if (sBlackoutFrame != 0 && frame == sBlackoutFrame)
    {
        printf("tssm: test: frame %d: co-op's connection is cut for %.1f s (TSSM_TEST_BLACKOUT) at clock ms %lld\n",
               (int)frame, sBlackoutSeconds, ClockMs());
        fflush(stdout);
        iCoopTestBlackout(sBlackoutSeconds);
    }

    for (S32 i = 0; i < sTabCount; i++)
    {
        if (sTabFrames[i] == frame)
        {
            printf("tssm: test: frame %d: the Co-op tab (TSSM_TEST_COOP_TAB)\n", (int)frame);
            iGuiCoopTest(sTabActions[i]);
        }
    }

    if (sKickFrame != 0 && frame == sKickFrame)
    {
        printf("tssm: test: frame %d: the Co-op tab's Kick (TSSM_TEST_COOP_KICK)\n", (int)frame);
        fflush(stdout);
        iCoopKick();
    }

    for (S32 i = 0; i < sStallCount; i++)
    {
        if (sStallFrames[i] == frame)
        {
            Stall(frame, sStallSeconds[i]);
        }
    }

    for (S32 i = 0; i < sEventCount; i++)
    {
        if (sEventFrames[i] == 0 || frame < sEventFrames[i] || globals.sceneCur == NULL)
        {
            continue;
        }
        // A teleport is sent only in plain play, as walking into a portal
        // sends it: sent while the game is paused (a pause or task screen),
        // the level change waits for an unpaused update while no frame is
        // shown, so the frame-counted script that would close the screen
        // never runs on, and the game spins without a picture. It waits.
        if (sEventWhat[i] == (U32)eEventTeleportPlayer && !InPlainPlay())
        {
            if (sEventWaitLogged == 0)
            {
                sEventWaitLogged = frame;
                printf("tssm: test: frame %d: event to %08x waits: the game is not in plain play "
                       "(mode %d, state %d)\n",
                       (int)frame, (unsigned)sEventIds[i], (int)gGameMode, (int)gGameState);
                fflush(stdout);
            }
            continue;
        }
        sEventFrames[i] = 0;
        sEventWaitLogged = 0;
        xBase* b = zSceneFindObject(sEventIds[i]);
        printf("tssm: test: frame %d: event %u to %08x (TSSM_TEST_EVENT)%s\n", (int)frame,
               (unsigned)sEventWhat[i], (unsigned)sEventIds[i],
               b != NULL ? "" : ": no such object in this level");
        fflush(stdout);
        if (b != NULL)
        {
            zEntEvent(b, sEventWhat[i]);
        }
    }

    for (S32 i = 0; i < sAutoSaveCount; i++)
    {
        if (sAutoSaveFrames[i] == frame)
        {
            AutoSave(frame);
        }
    }

    if (sSaveFrame != 0 && frame == sSaveFrame)
    {
        if (gGameMode == eGameMode_Game && gGameState == eGameState_Play && globals.sceneCur != NULL)
        {
            printf("tssm: test: frame %d: a save by hand into the first slot (TSSM_TEST_SAVE)\n", (int)frame);
            zSaveLoad_PcTestSaveByHand(0);
            zGameStateSwitchEvent(eEventDispatcher_SetSaveState_Saving);
            sSaveWaiting = true;
        }
        else
        {
            printf("tssm: test: frame %d: no save by hand: the game is not in plain play\n", (int)frame);
        }
        fflush(stdout);
    }
    else if (sSaveWaiting && gGameMode == eGameMode_Game && gGameState == eGameState_Play)
    {
        sSaveWaiting = false;
        printf("tssm: test: frame %d: back in play after the save screen\n", (int)frame);
        fflush(stdout);
    }

    if (sQuitFrame != 0 && frame == sQuitFrame)
    {
        if (gGameMode == eGameMode_Game && gGameState == eGameState_Play && globals.sceneCur != NULL)
        {
            printf("tssm: test: frame %d: leaving the game for the title (TSSM_TEST_QUIT)\n", (int)frame);
            zGameStateSwitchEvent(eEventDispatcher_SetGameState_Exit);
        }
        else
        {
            printf("tssm: test: frame %d: no quit: the game is not in plain play\n", (int)frame);
        }
        fflush(stdout);
    }

    if (sKillFrame != 0 && frame == sKillFrame)
    {
        Kill(frame);
        fflush(stdout);
    }

    if (sHealthFrame != 0 && frame == sHealthFrame && globals.player.s != NULL &&
        sHealthValue == 0 && xglobals->___player_ent_dont_use_directly != NULL)
    {
        printf("tssm: test: frame %d: the player is defeated (TSSM_TEST_HEALTH)\n", (int)frame);
        fflush(stdout);
        zEntEvent(xglobals->___player_ent_dont_use_directly, eEventKill);
    }
    else if (sHealthFrame != 0 && frame == sHealthFrame && globals.player.s != NULL)
    {
        U32 v = (U32)sHealthValue;
        if (v > globals.player.s->MaxHealth)
        {
            v = globals.player.s->MaxHealth;
        }
        printf("tssm: test: frame %d: health %u set to %u\n", (int)frame, (unsigned)globals.player.s->Health,
               (unsigned)v);
        globals.player.s->Health = v;
        fflush(stdout);
    }

    if (sHealthKeep > 0 && globals.player.s != NULL && globals.player.s->Health > 0 &&
        globals.player.s->Health < (U32)sHealthKeep)
    {
        globals.player.s->Health = globals.player.s->MaxHealth;
        printf("tssm: test: frame %d: health back to %u (TSSM_TEST_HEALTH_KEEP)\n", (int)frame,
               (unsigned)globals.player.s->Health);
        fflush(stdout);
    }

    if (sResetFrame != 0 && frame >= sResetFrame && InPlainPlay())
    {
        sResetFrame = 0;
        printf(
            "tssm: test: frame %d: the level is reset as after a lost chance (TSSM_TEST_RESET)\n",
            (int)frame);
        fflush(stdout);
        zGameStateSwitch(eGameState_LoseChance);
    }

    if (sPowerFrame != 0 && frame == sPowerFrame)
    {
        for (S32 i = 0; i < 8; i++)
        {
            globals.player.g.PowerUp[i] = (U8)sPowerLevel;
        }
        printf("tssm: test: frame %d: every move's power-up set to %d\n", (int)frame,
               (int)sPowerLevel);
        fflush(stdout);
    }

    for (S32 i = 0; i < 2; i++)
    {
        if (sUpgradeFrame[i] != 0 && frame == sUpgradeFrame[i] &&
            xglobals->___player_ent_dont_use_directly != NULL)
        {
            printf(
                "tssm: test: frame %d: the pause menu's upgrade of move %d (TSSM_TEST_UPGRADE); upgrade points "
                "%d%s\n",
                (int)frame, (int)sUpgradeIndex[i], (int)globals.player.CurrentUnusedSkillPoints,
                zGameIsPaused() ? "" : " (the pause menu is not open)");
            fflush(stdout);
            F32 param[4] = { (F32)sUpgradeIndex[i], 0.0f, 0.0f, 0.0f };
            zEntEvent(xglobals->___player_ent_dont_use_directly, eEventUpgradePowerUp, param);
        }
    }

    S32 latest = -1;
    for (S32 i = 0; i < sPlaceCount; i++)
    {
        if (sPlaces[i].frame == frame)
        {
            Place(frame, sPlaces[i]);
        }
        if (sPlaces[i].frame <= frame && (latest < 0 || sPlaces[i].frame >= sPlaces[latest].frame))
        {
            latest = i;
        }
    }
    if (latest >= 0 && frame >= sHoldFirst && frame <= sHoldLast && sPlaces[latest].frame != frame)
    {
        Place(frame, sPlaces[latest], true);
    }

    if (sCamLogEvery > 0 && frame % sCamLogEvery == 0)
    {
        // The camera the game draws with (zCamSB.cpp updates it through zCameraUpdate). %g, so
        // that a camera thrown far out of the world shows as such, and a NaN as nan.
        const xVec3& pos = globals.oldSkoolCamera.mat.pos;
        printf("tssm: test: frame %d: camera at (%g, %g, %g), fly-through %s\n", (int)frame,
               (double)pos.x, (double)pos.y, (double)pos.z, zcam_fly != 0 ? "on" : "off");
    }

    for (S32 i = 0; i < sWarpCount; i++)
    {
        const WarpEvent& e = sWarps[i];
        if (e.frame != frame)
        {
            continue;
        }

        xEnt* player = (xEnt*)xglobals->___player_ent_dont_use_directly;
        if (player == NULL || player->frame == NULL)
        {
            printf("tssm: test: frame %d: no player for the screen warp\n", (int)frame);
            continue;
        }

        // The player's position, as the game's own player warp (zEntPlayerFX's
        // slam) uses.
        xVec3 at = *xEntGetPos(player);
        xScreenWarp::warper* w =
            xScreenWarp::emit(at, e.radius, e.seconds, e.type, e.intensity, 1.0f, 0);
        (void)w;

        printf("tssm: test: frame %d: screen warp %s at the player (%.1f, %.1f, %.1f), radius "
               "%.1f, %.1f s, intensity %.1f\n",
               (int)frame, xScreenWarp::common_warp_names[e.type], at.x, at.y, at.z, e.radius,
               e.seconds, e.intensity);
        fflush(stdout);
    }
}
