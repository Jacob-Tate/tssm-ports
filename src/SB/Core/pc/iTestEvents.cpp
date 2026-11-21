// Debug events for unattended runs; see iTestEvents.h. Port-only, compiled
// with the game units.

#include "iTestEvents.h"

#include "iCheats.h"
#include "iExtras.h"
#include "iHost.h"
#include "iLightVolume.h"
#include "iHudAnchor.h"
#include "iMenuMouse.h"
#include "iMouseLook.h"
#include "iPadTouch.h"
#include "iWindow.h"
#include "iPadKeyboard.h"
#include "iPadTokens.h"
#include "xAnim.h"
#include "xEnt.h"
#include "xMath3.h"
#include "xScreenWarp.h"
#include "zCamera.h"
#include "zCam.h"
#include "zCar.h"
#include "zEntSimpleObj.h"
#include "zGlobals.h"
#include "zPlayer.h"
#include "zScene.h"
#include "zSpotlight.h"

extern F32 gRealTimeElapsed;

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

    // TSSM_TEST_CARLOG=first-last: from first to last, log the Patty Wagon every frame (the
    // frame's time, the car's step share, position and velocity), for the frame-rate checks of
    // the car (docs/EXTRAS.md).
    S32 sCarLogFirst;
    S32 sCarLogLast;

    // TSSM_TEST_SPOTLOG=n: every n frames, log the scene's spotlights (which are on, which are
    // drawn, whether the camera is inside a beam), for the spotlight checks (docs/EXTRAS.md).
    S32 sSpotLogEvery;

    // TSSM_TEST_CROWDLOG=n: every n frames, log the scene's crowd objects (the Xbox's skinned
    // crowd models, iExtrasCrowdModel) and how many of them animated since the last line: the
    // crowd check of [extras] crowds where the camera is not still enough to compare frames
    // (BB01, from the Patty Wagon).
    S32 sCrowdLogEvery;
    S32 sCrowdLogLast;
    const S32 kMaxCrowd = 64;
    F32 sCrowdTime[kMaxCrowd];

    // TSSM_TEST_MOUSE: a mouse of the test's own (iMouseLook.h).
    enum MouseKind
    {
        MOUSE_MOVE,
        MOUSE_AT,
        MOUSE_CLICK,
        MOUSE_RCLICK,
        MOUSE_WHEEL,
        MOUSE_ESC
    };

    struct MouseEvent
    {
        S32 first;
        S32 last;
        MouseKind kind;
        F32 x;
        F32 y;
    };

    const S32 kMaxMouse = 128;

    MouseEvent sMouse[kMaxMouse];
    S32 sMouseCount;

    // TSSM_TEST_TAP: taps on the touch screen (iPadTouch.h), in menu units.
    struct TapEvent
    {
        S32 frame;
        F32 x;
        F32 y;
    };

    const S32 kMaxTaps = 64;

    TapEvent sTaps[kMaxTaps];
    S32 sTapCount;

    void ParseTaps(const char* list)
    {
        const char* c = list;
        while (*c != '\0')
        {
            int frame = 0;
            float x = 0.0f;
            float y = 0.0f;
            if (sscanf(c, "%d:%f:%f", &frame, &x, &y) == 3 && sTapCount < kMaxTaps)
            {
                sTaps[sTapCount].frame = frame;
                sTaps[sTapCount].x = x;
                sTaps[sTapCount].y = y;
                sTapCount++;
            }
            else
            {
                printf("tssm: test: TSSM_TEST_TAP: an entry is not frame:x:y (or there are more "
                       "than %d); ignored\n",
                       (int)kMaxTaps);
            }

            c = strchr(c, ',');
            if (c == NULL)
            {
                break;
            }
            c++;
        }
        fflush(stdout);
    }

    void TapFrame(S32 frame)
    {
        for (S32 i = 0; i < sTapCount; i++)
        {
            if (sTaps[i].frame == frame)
            {
                F32 wx;
                F32 wy;
                iMenuMouseToWindow(sTaps[i].x, sTaps[i].y, &wx, &wy);
                S32 ww = 0;
                S32 wh = 0;
                iWindowGetSize(&ww, &wh);
                if (ww > 0 && wh > 0)
                {
                    iPadTouchTestTap(wx / (F32)ww, wy / (F32)wh);
                }
                printf("tssm: test: frame %d: tap at %.0f %.0f\n", (int)frame,
                       (double)sTaps[i].x, (double)sTaps[i].y);
            }
        }
    }

    // TSSM_TEST_UILOG=n: every n frames, log the menu widgets (iMenuMouseLog).
    S32 sUILogEvery;

    // TSSM_TEST_HUDLOG=n: every n frames, log the HUD widgets drawn (iHudAnchorLog).
    S32 sHudLogEvery;

    // TSSM_TEST_LOOKLOG=1: with each TSSM_TEST_CAMLOG line, the on-foot camera's angles and
    // whether the mouse is steering it.
    bool sLookLog;

    void ParseMouse(const char* list)
    {
        static char buf[4096];
        strncpy(buf, list, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        for (char* entry = strtok(buf, ","); entry != NULL; entry = strtok(NULL, ","))
        {
            MouseEvent e;
            e.x = 0.0f;
            e.y = 0.0f;
            int frames = 1;
            char word[16] = "";
            bool ok = sMouseCount < kMaxMouse;

            if (sscanf(entry, "%d:%15[a-z]", &e.first, word) != 2 || e.first < 1)
            {
                ok = false;
            }
            else if (strcmp(word, "move") == 0)
            {
                e.kind = MOUSE_MOVE;
                ok = ok && sscanf(entry, "%*d:move:%f:%f/%d", &e.x, &e.y, &frames) >= 2;
            }
            else if (strcmp(word, "at") == 0)
            {
                e.kind = MOUSE_AT;
                ok = ok && sscanf(entry, "%*d:at:%f:%f", &e.x, &e.y) == 2;
            }
            else if (strcmp(word, "click") == 0)
            {
                e.kind = MOUSE_CLICK;
            }
            else if (strcmp(word, "rclick") == 0)
            {
                e.kind = MOUSE_RCLICK;
            }
            else if (strcmp(word, "wheel") == 0)
            {
                e.kind = MOUSE_WHEEL;
                ok = ok && sscanf(entry, "%*d:wheel:%f", &e.x) == 1;
            }
            else if (strcmp(word, "esc") == 0)
            {
                // The Escape key itself going down, for the pointer: what it
                // is bound to (Start) is TSSM_INPUT's to press.
                e.kind = MOUSE_ESC;
            }
            else
            {
                ok = false;
            }

            if (!ok)
            {
                printf("tssm: test: TSSM_TEST_MOUSE: '%s' is not frame:move:dx:dy[/frames], "
                       "frame:at:x:y, frame:click, frame:rclick, frame:wheel:n or frame:esc (or there are "
                       "more than %d); ignored\n",
                       entry, (int)kMaxMouse);
                continue;
            }

            e.last = e.first + (frames > 1 ? frames - 1 : 0);
            sMouse[sMouseCount++] = e;
        }

        // The test's mouse from the first frame on, whatever is plugged in.
        iMouseLookTestMove(0.0f, 0.0f);
        fflush(stdout);
    }

    void MouseFrame(S32 frame)
    {
        for (S32 i = 0; i < sMouseCount; i++)
        {
            const MouseEvent& e = sMouse[i];
            if (frame < e.first || frame > e.last)
            {
                continue;
            }

            switch (e.kind)
            {
            case MOUSE_MOVE:
                iMouseLookTestMove(e.x, e.y);
                break;
            case MOUSE_AT:
            {
                F32 wx;
                F32 wy;
                iMenuMouseToWindow(e.x, e.y, &wx, &wy);
                iMouseLookTestPointer(wx, wy);
                printf("tssm: test: frame %d: pointer to %.0f %.0f\n", (int)frame, (double)e.x,
                       (double)e.y);
                break;
            }
            case MOUSE_CLICK:
                iMouseLookTestPress(1);
                printf("tssm: test: frame %d: click\n", (int)frame);
                break;
            case MOUSE_RCLICK:
                iMouseLookTestPress(2);
                printf("tssm: test: frame %d: right click\n", (int)frame);
                break;
            case MOUSE_WHEEL:
                iMouseLookTestWheel(e.x > 0.0f ? 1 : -1);
                break;
            case MOUSE_ESC:
                iMouseLookTestEscape();
                printf("tssm: test: frame %d: Escape\n", (int)frame);
                break;
            }
        }
    }

    // TSSM_TEST_KEYS: a keyboard of the test's own, iPadKeyboard.h's stand-in.
    struct KeyPress
    {
        S32 first;
        S32 last;
        S16 key[4];
        S32 count;
        char text[48];
    };

    const S32 kMaxKeys = 128;

    KeyPress sKeys[kMaxKeys];
    S32 sKeyCount;
    // The frame whose keys the polls read: set after a frame is drawn, so the
    // next frame reads it, as TSSM_INPUT's buttons are (iCapture.cpp).
    S32 sKeyFrame;
    iPadKeyboardTestSource sTestKeyboard;

    bool TestKeyDown(S32 id)
    {
        for (S32 i = 0; i < sKeyCount; i++)
        {
            const KeyPress& k = sKeys[i];
            if (sKeyFrame < k.first || sKeyFrame > k.last)
            {
                continue;
            }
            for (S32 j = 0; j < k.count; j++)
            {
                if (k.key[j] == id)
                {
                    return true;
                }
            }
        }
        return false;
    }

    // A [keyboard] key name ("tab", "space", "mouse1"), or -1.
    S32 KeyId(const char* name)
    {
        S32 count;
        const iPadBindToken* tokens = iPadKeyTokens(&count);
        for (S32 i = 0; i < count; i++)
        {
            if (iHostStrCaseCmp(tokens[i].name, name) == 0)
            {
                return tokens[i].id;
            }
        }
        return -1;
    }

    // "700:tab", "900:space+w/10": held from that frame for that many frames (6
    // when not given), as TSSM_INPUT's entries are.
    bool ParseKey(const char* text, KeyPress* out)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", text);
        snprintf(out->text, sizeof(out->text), "%s", text);

        char* end;
        long at = strtol(buf, &end, 10);
        if (end == buf || at < 1 || *end != ':')
        {
            return false;
        }

        char* names = end + 1;
        long frames = 6;
        char* slash = strchr(names, '/');
        if (slash != NULL)
        {
            *slash = '\0';
            frames = strtol(slash + 1, &end, 10);
            if (end == slash + 1 || *end != '\0' || frames < 1)
            {
                return false;
            }
        }

        out->count = 0;
        for (char* name = names; name != NULL && *name != '\0';)
        {
            char* plus = strchr(name, '+');
            if (plus != NULL)
            {
                *plus = '\0';
            }
            const S32 id = KeyId(name);
            if (id < 0 || out->count == 4)
            {
                return false;
            }
            out->key[out->count++] = (S16)id;
            name = plus != NULL ? plus + 1 : NULL;
        }

        out->first = (S32)at;
        out->last = (S32)(at + frames - 1);
        return out->count > 0;
    }

    void ParseKeys(const char* list)
    {
        static char buf[4096];
        snprintf(buf, sizeof(buf), "%s", list);

        for (char* entry = strtok(buf, ","); entry != NULL; entry = strtok(NULL, ","))
        {
            if (sKeyCount == kMaxKeys || !ParseKey(entry, &sKeys[sKeyCount]))
            {
                printf("tssm: test: TSSM_TEST_KEYS: '%s' is not frame:key[+key][/frames] with "
                       "[keyboard] key names (or there are more than %d); ignored\n",
                       entry, (int)kMaxKeys);
                continue;
            }
            sKeyCount++;
        }

        // The test's keyboard from here on, whatever is plugged in and
        // whichever window has the focus.
        sTestKeyboard.down = TestKeyDown;
        sTestKeyboard.mouse_dx = 0.0f;
        sTestKeyboard.mouse_dy = 0.0f;
        iPadKeyboardSetTestSource(&sTestKeyboard);
        printf("tssm: test: a keyboard of the test's own, %d key presses\n", (int)sKeyCount);
        fflush(stdout);
    }

    void KeyFrame(S32 frame)
    {
        sKeyFrame = frame;
        for (S32 i = 0; i < sKeyCount; i++)
        {
            if (sKeys[i].first == frame)
            {
                printf("tssm: test: frame %d: keys %s\n", (int)frame, sKeys[i].text);
                fflush(stdout);
            }
        }
    }

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

    // One line: how many of the scene's spotlights are on (flag 1), how many of those are in
    // view and drawn (flag 0x100 clear), how many of those have a flare that shows with
    // spotlights = xbox (add_flare's alpha above 0: the lamp faces the camera, zSpotlight.cpp),
    // how many light volumes have the camera inside, how far
    // the camera is from the nearest beam it is beside (outside the cone's side, in units; below
    // 0 inside it), and the IDs of the lights that are on ("+" after one the camera is inside).
    void LogSpotlights(S32 frame)
    {
        zScene& s = *globals.sceneCur;
        const S32 count = s.baseCount[eBaseTypeSpotlight];
        zSpotlight* lights = (zSpotlight*)s.baseList[eBaseTypeSpotlight];
        const xMat4x3& camMat = zCamGetMatrix();
        const xVec3 cam = camMat.pos;
        S32 on = 0;
        S32 drawn = 0;
        S32 flares = 0;
        S32 inside = 0;
        F32 nearest = 1e9f;
        char ids[512];
        size_t len = 0;
        ids[0] = '\0';
        for (S32 i = 0; i < count; i++)
        {
            const zSpotlight& l = lights[i];
            if (!(l.flags & 0x1))
            {
                continue;
            }
            on++;
            const bool in = l.light_volume != NULL && l.light_volume->active() &&
                            l.light_volume->inside;
            if (!(l.flags & 0x100))
            {
                drawn++;
                // add_flare's alpha, before it is stored into a byte.
                F32 mag = l.mat.at.dot(camMat.at);
                F32 alpha = l.asset->flare.glow_min;
                if (mag < 0.0f)
                {
                    mag *= mag;
                    mag *= mag;
                    alpha = 0.5f + ((l.asset->flare.glow_max - l.asset->flare.glow_min) * mag +
                                    l.asset->flare.glow_min);
                }
                if (alpha >= 1.0f && l.asset->flare.size_max > 0.0f)
                {
                    flares++;
                }
            }
            if (in)
            {
                inside++;
            }
            // The camera against the beam: its distance along the axis, and from the axis less
            // the cone's radius there.
            const xVec3 d = cam - l.mat.pos;
            const F32 along = d.dot(l.mat.at);
            if (along >= 0.0f && along <= l.asset->max_dist)
            {
                const xVec3 off = d - l.mat.at * along;
                const F32 out = off.length() - (l.asset->radius + along * l.slope);
                if (out < nearest)
                {
                    nearest = out;
                }
            }
            if (len + 12 < sizeof(ids))
            {
                len += (size_t)snprintf(ids + len, sizeof(ids) - len, " %08X%s", (unsigned)l.id,
                                        in ? "+" : "");
            }
        }
        char near[48] = "";
        if (nearest < 1e9f)
        {
            snprintf(near, sizeof(near), ", nearest beam %.2f", nearest);
        }
        printf("tssm: test: frame %d: spotlights %d on of %d, %d drawn, %d flares, camera inside "
               "%d%s;%s\n",
               (int)frame, (int)on, (int)count, (int)drawn, (int)flares, (int)inside, near,
               on > 0 ? ids : " none");

        // The Xbox light volumes' remembered cone colours (iLightVolumeForgetCones), when the
        // count changes: no more than the level's cone shapes.
        static S32 sCones = -1;
        const S32 cones = iLightVolumeConeCount();
        if (cones != sCones)
        {
            sCones = cones;
            printf("tssm: test: frame %d: light volumes: %d cone colours kept\n", (int)frame,
                   (int)cones);
        }
        fflush(stdout);
    }

    // One line: the crowd objects (simple objects with one of the Xbox's skinned crowd models and
    // an animation), the speed of their idle cycle, and how many of them have moved on in it
    // since the line before.
    void LogCrowd(S32 frame)
    {
        zScene& s = *globals.sceneCur;
        const S32 count = s.baseCount[eBaseTypeStatic];
        zEntSimpleObj* simps = (zEntSimpleObj*)s.baseList[eBaseTypeStatic];
        S32 crowd = 0;
        S32 moved = 0;
        F32 speed = 0.0f;
        for (S32 i = 0; i < count; i++)
        {
            const zEntSimpleObj& e = simps[i];
            if (e.asset == NULL || !iExtrasCrowdModel(e.asset->modelInfoID) || e.model == NULL ||
                e.model->Anim == NULL || e.model->Anim->Single == NULL)
            {
                continue;
            }
            const xAnimSingle& single = *e.model->Anim->Single;
            if (crowd < kMaxCrowd)
            {
                if (sCrowdLogLast > 0 && single.Time != sCrowdTime[crowd])
                {
                    moved++;
                }
                sCrowdTime[crowd] = single.Time;
            }
            speed = single.CurrentSpeed;
            crowd++;
        }
        if (sCrowdLogLast > 0)
        {
            printf("tssm: test: frame %d: crowd %d objects, speed %.2f, %d moved since frame %d\n",
                   (int)frame, (int)crowd, speed, (int)moved, (int)sCrowdLogLast);
        }
        else
        {
            printf("tssm: test: frame %d: crowd %d objects, speed %.2f\n", (int)frame, (int)crowd,
                   speed);
        }
        fflush(stdout);
        sCrowdLogLast = frame;
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
        const char* carLog = getenv("TSSM_TEST_CARLOG");
        if (carLog != NULL && sscanf(carLog, "%d-%d", &sCarLogFirst, &sCarLogLast) != 2)
        {
            sCarLogFirst = sCarLogLast = 0;
        }
        const char* spotLog = getenv("TSSM_TEST_SPOTLOG");
        if (spotLog != NULL && atoi(spotLog) > 0)
        {
            sSpotLogEvery = atoi(spotLog);
        }
        const char* crowdLog = getenv("TSSM_TEST_CROWDLOG");
        if (crowdLog != NULL && atoi(crowdLog) > 0)
        {
            sCrowdLogEvery = atoi(crowdLog);
        }
        const char* mouse = getenv("TSSM_TEST_MOUSE");
        if (mouse != NULL && mouse[0] != '\0')
        {
            ParseMouse(mouse);
        }
        const char* taps = getenv("TSSM_TEST_TAP");
        if (taps != NULL && taps[0] != '\0')
        {
            ParseTaps(taps);
        }
        const char* keys = getenv("TSSM_TEST_KEYS");
        if (keys != NULL && keys[0] != '\0')
        {
            ParseKeys(keys);
        }
        const char* lookLog = getenv("TSSM_TEST_LOOKLOG");
        sLookLog = lookLog != NULL && atoi(lookLog) != 0;
        const char* uiLog = getenv("TSSM_TEST_UILOG");
        if (uiLog != NULL && atoi(uiLog) > 0)
        {
            sUILogEvery = atoi(uiLog);
        }
        const char* hudLog = getenv("TSSM_TEST_HUDLOG");
        if (hudLog != NULL && atoi(hudLog) > 0)
        {
            sHudLogEvery = atoi(hudLog);
        }
    }

    MouseFrame(frame);
    TapFrame(frame);
    if (sKeyCount > 0)
    {
        KeyFrame(frame);
    }

    // TSSM_TEST_CHEAT, TSSM_TEST_CHEATLOG (iCheats.h).
    iCheatsTestFrame(frame);

    if (sUILogEvery > 0 && frame % sUILogEvery == 0)
    {
        iMenuMouseLog(frame);
    }

    // The log is of the frame drawn next; 0 when that is not a frame to log.
    iHudAnchorLog(sHudLogEvery > 0 && (frame + 1) % sHudLogEvery == 0 ? frame + 1 : 0);

    if (sCarLogLast > 0 && frame >= sCarLogFirst && frame <= sCarLogLast)
    {
        // The car as zEntPlayerCarUpdate left it this frame; gRealTimeElapsed is the frame's
        // measured time (zGameLoop), before any slowdown.
        const zCar* car = zEntPlayerCarGetCar();
        const xVec3 pos = car->mat.pos;
        printf("tssm: test: frame %d: car dt %.6f share %.4f at (%.4f, %.4f, %.4f) vel (%.4f, "
               "%.4f, %.4f)\n",
               (int)frame, gRealTimeElapsed, zCarGetStepShare(), pos.x, pos.y, pos.z,
               car->velocity.x, car->velocity.y, car->velocity.z);
    }

    if (sSpotLogEvery > 0 && frame % sSpotLogEvery == 0 && globals.sceneCur != NULL)
    {
        LogSpotlights(frame);
    }

    if (sCrowdLogEvery > 0 && frame % sCrowdLogEvery == 0 && globals.sceneCur != NULL)
    {
        LogCrowd(frame);
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
        if (sLookLog)
        {
            // For the mouse look checks: where the on-foot camera sits around the player
            // (its orbit angle), how it is aimed, and who is steering it.
            const xCamera& cam = globals.oldSkoolCamera;
            printf("tssm: test: frame %d: look orbit %.4f yaw %.4f pitch %.4f at (%.3f, %.3f, "
                   "%.3f) right (%.3f, %.3f, %.3f) mouse %s\n",
                   (int)frame, (double)cam.pcur, (double)cam.yaw_cur, (double)cam.pitch_cur,
                   (double)cam.mat.at.x, (double)cam.mat.at.y, (double)cam.mat.at.z,
                   (double)cam.mat.right.x, (double)cam.mat.right.y, (double)cam.mat.right.z,
                   iMouseLookSteering() ? "steering" : "off");
            fflush(stdout);
        }
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
