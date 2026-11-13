// Debug events for unattended runs; see iTestEvents.h. Port-only, compiled
// with the game units.

#include "iTestEvents.h"

#include "xEnt.h"
#include "xMath3.h"
#include "xScreenWarp.h"
#include "zCamera.h"
#include "zGlobals.h"
#include "zPlayer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        const char* crash = getenv("TSSM_TEST_CRASH");
        if (crash != NULL && atoi(crash) > 1)
        {
            sCrashFrame = atoi(crash);
        }
        const char* power = getenv("TSSM_TEST_POWERUP");
        if (power != NULL && sscanf(power, "%d:%d", &sPowerFrame, &sPowerLevel) != 2)
        {
            sPowerFrame = 0;
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
