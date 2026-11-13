#include "iPadTouch.h"

#include "iConfig.h"
#include "iPadBind.h"
#include "iPadHost.h"
#include "iPadSettings.h"
#include "iTouchOverlay.h"

// TSSM-ADAPTED: each button presses one row of the input layer's table
// (kPadBindButtons, iPadBindTable.cpp), so it sets the bits a controller's
// button of the same name does: the small buttons are TSSM's Back (Select + Z:
// the upgrade window) and black (R2 + Z), and TSSM shows its HUD on LT, which
// is L. The camera is a second stick, the controller's right stick, rather than
// a drag (the drag is kept behind input.touch_drag_look), and each finger is
// owned by the control it landed on until it lifts.

#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

// See iPadTouch.h.

namespace
{
    // ---- the layout ---------------------------------------------------------------

    // One on-screen button. Placed in units of the window's height (times
    // input.touch_scale), from the corner named by `right` and `bottom`, inside
    // the safe area, so the layout keeps its size on any aspect.
    struct Button
    {
        // The row of kPadBindButtons this button presses.
        const char* row;
        char label;
        F32 x;
        F32 y;
        bool right;
        bool bottom;
        F32 halfWidth;
        F32 halfHeight;
        U8 r, g, b;
    };

    // The face four by position, as an Xbox pad has them: A south, B east, X
    // west, Y north. Raised off the bottom edge so the right stick fits below
    // and to the left of them. '=' is Back and 'Z' is black.
    const F32 kFaceX = 0.22f;
    const F32 kFaceY = 0.36f;
    const F32 kFaceGap = 0.12f;
    const F32 kFace = 0.07f;

    const Button kButtons[] = {
        { "a", 'A', kFaceX, kFaceY - kFaceGap, true, true, kFace, kFace, 96, 176, 72 },
        { "b", 'B', kFaceX - kFaceGap, kFaceY, true, true, kFace, kFace, 208, 72, 64 },
        { "x", 'X', kFaceX + kFaceGap, kFaceY, true, true, kFace, kFace, 64, 120, 208 },
        { "y", 'Y', kFaceX, kFaceY + kFaceGap, true, true, kFace, kFace, 224, 184, 48 },

        { "lt", 'L', 0.16f, 0.09f, false, false, 0.11f, 0.05f, 160, 160, 160 },
        { "back", '=', 0.13f, 0.21f, false, false, 0.08f, 0.045f, 160, 160, 160 },
        { "rt", 'R', 0.16f, 0.09f, true, false, 0.11f, 0.05f, 160, 160, 160 },
        { "black", 'Z', 0.13f, 0.21f, true, false, 0.08f, 0.045f, 160, 160, 160 },

        // Top right, left of R: out of the way of the face buttons.
        { "start", '>', 0.40f, 0.08f, true, false, 0.04f, 0.04f, 160, 160, 160 },
    };

    const S32 kButtonCount = (S32)(sizeof(kButtons) / sizeof(kButtons[0]));

    // Each button's XPAD_BUTTON_* bits: its row's mask, looked up once.
    U32 sBits[kButtonCount];

    // A thumb covers more than it hits: a landing this far past a button's
    // edge (as a share of its size) still lands on it, and a finger that
    // landed on one keeps it pressed until it strays further than the hold --
    // which is short of the next face button's centre, so a thumb rolled onto
    // B lets go of A (and does not press B: it is A's finger).
    const F32 kHitSlop = 1.3f;
    const F32 kHoldSlop = 1.5f;

    // The sticks. Both centre where the thumb lands; at rest the ring is drawn
    // at home. The left one takes a finger anywhere on the left half, the right
    // one a finger within kRightZone of its home on the right half.
    enum
    {
        STICK_LEFT,
        STICK_RIGHT,
        STICK_COUNT
    };

    struct StickHome
    {
        const char* name;
        F32 x;
        F32 y;
        bool right;
    };

    const StickHome kStickHomes[STICK_COUNT] = {
        { "left_stick", 0.24f, 0.26f, false },
        { "right_stick", 0.54f, 0.22f, true },
    };

    const F32 kStickRadius = 0.13f;
    const F32 kStickKnob = 0.055f;
    const F32 kRightZone = 0.30f;

    // Of the stick's reach. A resting thumb still wanders a few pixels.
    const F32 kStickDeadzone = 0.12f;

    // Seconds for the drag camera to cover most of a drag's movement. Touch
    // samples and frames arrive at different rates, so the distance moved in
    // one frame jitters; the camera takes this share of what is still owed
    // each frame, which evens it out and still turns it exactly as far as the
    // finger went.
    const F32 kCameraSmoothing = 0.08f;

    // Where everything is this poll, in window pixels.
    struct Layout
    {
        F32 w;
        F32 h;
        // One unit: the window's height times input.touch_scale.
        F32 unit;
        F32 left;
        F32 right;
        F32 top;
        F32 bottom;
        F32 buttonX[kButtonCount];
        F32 buttonY[kButtonCount];
        F32 homeX[STICK_COUNT];
        F32 homeY[STICK_COUNT];
    };

    F32 sTestInsets[4] = { -1.0f, -1.0f, -1.0f, -1.0f };

    // The safe area's insets in window pixels: the parts of the screen a notch,
    // a cut-out or a rounded corner takes, which SDL reports on Android.
    void SafeInsets(F32 w, F32 h, F32* l, F32* r, F32* t, F32* b)
    {
        *l = *r = *t = *b = 0.0f;

        if (sTestInsets[0] >= 0.0f)
        {
            *l = sTestInsets[0];
            *r = sTestInsets[1];
            *t = sTestInsets[2];
            *b = sTestInsets[3];
            return;
        }

        SDL_Window* window = SDL_GetKeyboardFocus();
        if (window == NULL)
        {
            int count = 0;
            SDL_Window** windows = SDL_GetWindows(&count);
            window = (windows != NULL && count > 0) ? windows[0] : NULL;
            SDL_free(windows);
        }
        if (window == NULL)
        {
            return;
        }

        int ww = 0;
        int wh = 0;
        SDL_Rect safe;
        if (!SDL_GetWindowSize(window, &ww, &wh) || ww <= 0 || wh <= 0 ||
            !SDL_GetWindowSafeArea(window, &safe))
        {
            return;
        }

        // The safe area is in window coordinates; the overlay draws in pixels.
        const F32 sx = w / (F32)ww;
        const F32 sy = h / (F32)wh;
        *l = (F32)safe.x * sx;
        *t = (F32)safe.y * sy;
        *r = (F32)(ww - safe.x - safe.w) * sx;
        *b = (F32)(wh - safe.y - safe.h) * sy;
    }

    void MakeLayout(F32 w, F32 h, Layout* L)
    {
        L->w = w;
        L->h = h;
        L->unit = h * iPadSettingsGet().touch_scale;
        SafeInsets(w, h, &L->left, &L->right, &L->top, &L->bottom);

        const F32 u = L->unit;
        for (S32 i = 0; i < kButtonCount; i++)
        {
            const Button& b = kButtons[i];
            L->buttonX[i] = b.right ? w - L->right - b.x * u : L->left + b.x * u;
            L->buttonY[i] = b.bottom ? h - L->bottom - b.y * u : L->top + b.y * u;
        }
        for (S32 i = 0; i < STICK_COUNT; i++)
        {
            const StickHome& s = kStickHomes[i];
            L->homeX[i] = s.right ? w - L->right - s.x * u : L->left + s.x * u;
            L->homeY[i] = h - L->bottom - s.y * u;
        }
    }

    // How far a point is from button `i`, in units of its half extents grown
    // by `slop`: 1 or less is on it.
    F32 ButtonDistance(const Layout& L, S32 i, F32 px, F32 py, F32 slop)
    {
        const Button& b = kButtons[i];
        F32 dx = fabsf(px - L.buttonX[i]) / (b.halfWidth * L.unit * slop);
        F32 dy = fabsf(py - L.buttonY[i]) / (b.halfHeight * L.unit * slop);
        return dx > dy ? dx : dy;
    }

    // The button a finger lands on: the nearest whose grown box holds it.
    S32 ButtonAt(const Layout& L, F32 px, F32 py)
    {
        S32 best = -1;
        F32 bestDist = 0.0f;

        for (S32 i = 0; i < kButtonCount; i++)
        {
            F32 d = ButtonDistance(L, i, px, py, kHitSlop);
            if (d <= 1.0f && (best < 0 || d < bestDist))
            {
                best = i;
                bestDist = d;
            }
        }

        return best;
    }

    // ---- the fingers --------------------------------------------------------------

    enum Control
    {
        // On empty screen, or on a stick another finger already holds:
        // nothing, until it lifts.
        CONTROL_NONE,
        CONTROL_BUTTON,
        CONTROL_STICK,
        // input.touch_drag_look.
        CONTROL_DRAG,
    };

    struct Finger
    {
        // A finger is its device's: a pen and a finger may share an id.
        SDL_TouchID touch;
        SDL_FingerID id;
        Control control;
        // The button or the stick it owns.
        S32 which;
        F32 x;
        F32 y;
        // Where the last poll left it, for the drag.
        F32 lastX;
        F32 lastY;
        // Went down during this poll's events, and lifted during them.
        bool landed;
        bool lifted;
    };

    const S32 kMaxFingers = 10;

    Finger sFingers[kMaxFingers];
    S32 sFingerCount;

    // The touch events since the last poll, in the order SDL posted them. SDL's
    // Java half posts them on Android's UI thread, so a watch copies them here
    // and the poll takes them on the game's thread.
    enum
    {
        EVENT_DOWN,
        EVENT_MOTION,
        EVENT_UP,
        // Everything let go: the app went to the background.
        EVENT_RESET,
    };

    struct TouchEvent
    {
        S32 kind;
        SDL_TouchID touch;
        SDL_FingerID id;
        F32 x;
        F32 y;
    };

    const S32 kQueueMax = 256;
    TouchEvent sQueue[kQueueMax];
    S32 sQueueCount;
    bool sQueueOverflowed;
    SDL_SpinLock sQueueLock;
    bool sWatching;

    bool sEnabled;

    // Hidden while a controller is in use, until the screen is touched.
    bool sShowing = true;

    // Where each stick is centred while held, and what it reads.
    F32 sStickCenterX[STICK_COUNT];
    F32 sStickCenterY[STICK_COUNT];
    F32 sStickX[STICK_COUNT];
    F32 sStickY[STICK_COUNT];

    // The drag camera: movement this poll, and pixels not yet handed over.
    F32 sCameraDX;
    F32 sCameraDY;
    F32 sCameraOwedX;
    F32 sCameraOwedY;
    Uint64 sLastPollNS;

    U32 sHeld;
    bool sInUse;

    void Queue(S32 kind, SDL_TouchID touch, SDL_FingerID id, F32 x, F32 y)
    {
        SDL_LockSpinlock(&sQueueLock);

        // A finger that is only moving keeps one entry: its latest position.
        bool merged = false;
        if (kind == EVENT_MOTION)
        {
            for (S32 i = sQueueCount - 1; i >= 0; i--)
            {
                if (sQueue[i].kind == EVENT_RESET)
                {
                    break;
                }
                if (sQueue[i].touch == touch && sQueue[i].id == id)
                {
                    if (sQueue[i].kind == EVENT_MOTION)
                    {
                        sQueue[i].x = x;
                        sQueue[i].y = y;
                        merged = true;
                    }
                    break;
                }
            }
        }

        if (!merged)
        {
            if (sQueueCount < kQueueMax)
            {
                TouchEvent& e = sQueue[sQueueCount++];
                e.kind = kind;
                e.touch = touch;
                e.id = id;
                e.x = x;
                e.y = y;
            }
            else
            {
                sQueueOverflowed = true;
            }
        }

        SDL_UnlockSpinlock(&sQueueLock);
    }

    bool SDLCALL WatchTouch(void*, SDL_Event* e)
    {
        if (!sEnabled)
        {
            return true;
        }

        switch (e->type)
        {
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            // On Android SDL also makes a finger of a mouse's left button
            // (SDL_HINT_MOUSE_TOUCH_EVENTS). The mouse is the keyboard
            // layer's; it does not press the on-screen buttons as well.
            if (e->tfinger.touchID == SDL_MOUSE_TOUCHID)
            {
                break;
            }
            Queue(e->type == SDL_EVENT_FINGER_DOWN   ? EVENT_DOWN :
                  e->type == SDL_EVENT_FINGER_MOTION ? EVENT_MOTION :
                                                       EVENT_UP,
                  e->tfinger.touchID, e->tfinger.fingerID, e->tfinger.x, e->tfinger.y);
            break;

        case SDL_EVENT_WILL_ENTER_BACKGROUND:
            Queue(EVENT_RESET, 0, 0, 0.0f, 0.0f);
            break;

        default:
            break;
        }
        return true;
    }

    Finger* FindFinger(SDL_TouchID touch, SDL_FingerID id)
    {
        for (S32 i = 0; i < sFingerCount; i++)
        {
            if (sFingers[i].touch == touch && sFingers[i].id == id && !sFingers[i].lifted)
            {
                return &sFingers[i];
            }
        }
        return NULL;
    }

    bool StickTaken(S32 stick)
    {
        for (S32 i = 0; i < sFingerCount; i++)
        {
            if (sFingers[i].control == CONTROL_STICK && sFingers[i].which == stick &&
                !sFingers[i].lifted)
            {
                return true;
            }
        }
        return false;
    }

    bool DragTaken()
    {
        for (S32 i = 0; i < sFingerCount; i++)
        {
            if (sFingers[i].control == CONTROL_DRAG && !sFingers[i].lifted)
            {
                return true;
            }
        }
        return false;
    }

    // A stick centred where the thumb landed, kept whole inside the safe area.
    void CentreStick(const Layout& L, S32 stick, F32 px, F32 py)
    {
        const F32 r = kStickRadius * L.unit;
        F32 lo = L.left + r;
        F32 hi = L.w - L.right - r;
        sStickCenterX[stick] = px < lo ? lo : (px > hi ? hi : px);
        lo = L.top + r;
        hi = L.h - L.bottom - r;
        sStickCenterY[stick] = py < lo ? lo : (py > hi ? hi : py);
    }

    // What a finger that has just landed at (px, py) is for. Decided once:
    // the finger keeps it until it lifts, wherever it goes.
    void Assign(const Layout& L, Finger* f, F32 px, F32 py)
    {
        f->control = CONTROL_NONE;
        f->which = -1;

        S32 button = ButtonAt(L, px, py);
        if (button >= 0)
        {
            f->control = CONTROL_BUTTON;
            f->which = button;
            return;
        }

        const F32 half = L.w * 0.5f;

        if (px < half)
        {
            if (!StickTaken(STICK_LEFT))
            {
                f->control = CONTROL_STICK;
                f->which = STICK_LEFT;
                CentreStick(L, STICK_LEFT, px, py);
            }
            return;
        }

        const F32 dx = px - L.homeX[STICK_RIGHT];
        const F32 dy = py - L.homeY[STICK_RIGHT];
        const F32 zone = kRightZone * L.unit;
        if (dx * dx + dy * dy <= zone * zone)
        {
            if (!StickTaken(STICK_RIGHT))
            {
                f->control = CONTROL_STICK;
                f->which = STICK_RIGHT;
                CentreStick(L, STICK_RIGHT, px, py);
            }
            return;
        }

        if (iPadSettingsGet().touch_drag_look && !DragTaken())
        {
            f->control = CONTROL_DRAG;
        }
    }

    void Lift(Finger* f)
    {
        f->lifted = true;
    }

    // Takes the queued events, in order. Returns whether a finger went down.
    bool TakeEvents(const Layout* L)
    {
        static TouchEvent batch[kQueueMax];

        SDL_LockSpinlock(&sQueueLock);
        const S32 n = sQueueCount;
        memcpy(batch, sQueue, sizeof(TouchEvent) * (size_t)n);
        sQueueCount = 0;
        const bool overflowed = sQueueOverflowed;
        sQueueOverflowed = false;
        SDL_UnlockSpinlock(&sQueueLock);

        bool down = false;

        for (S32 i = 0; i < n; i++)
        {
            const TouchEvent& e = batch[i];

            if (e.kind == EVENT_RESET || L == NULL)
            {
                // Nothing to lay out in yet, or everything let go.
                for (S32 k = 0; k < sFingerCount; k++)
                {
                    Lift(&sFingers[k]);
                }
                continue;
            }

            const F32 px = e.x * L->w;
            const F32 py = e.y * L->h;
            Finger* f = FindFinger(e.touch, e.id);

            if (e.kind == EVENT_DOWN)
            {
                down = true;

                // A down for a finger still held means its up was lost: that
                // one is gone, and this is a new finger.
                if (f != NULL)
                {
                    Lift(f);
                }
                if (sFingerCount == kMaxFingers)
                {
                    continue;
                }

                f = &sFingers[sFingerCount++];
                memset(f, 0, sizeof(*f));
                f->touch = e.touch;
                f->id = e.id;
                f->x = f->lastX = px;
                f->y = f->lastY = py;
                f->landed = true;
                Assign(*L, f, px, py);
            }
            else if (f != NULL)
            {
                f->x = px;
                f->y = py;
                if (e.kind == EVENT_UP)
                {
                    Lift(f);
                }
            }
        }

        // Events were dropped: let go of whatever SDL no longer holds.
        if (overflowed)
        {
            for (S32 k = 0; k < sFingerCount; k++)
            {
                bool held = false;
                int count = 0;
                SDL_Finger** fingers = SDL_GetTouchFingers(sFingers[k].touch, &count);
                for (int c = 0; fingers != NULL && c < count; c++)
                {
                    if (fingers[c]->id == sFingers[k].id)
                    {
                        held = true;
                    }
                }
                SDL_free(fingers);

                if (!held)
                {
                    Lift(&sFingers[k]);
                }
            }
        }

        return down;
    }

    // What the fingers hold this poll.
    void Evaluate(const Layout& L)
    {
        sHeld = 0;
        sInUse = false;
        sCameraDX = 0.0f;
        sCameraDY = 0.0f;

        bool stickHeld[STICK_COUNT] = { false, false };
        for (S32 i = 0; i < STICK_COUNT; i++)
        {
            sStickX[i] = 0.0f;
            sStickY[i] = 0.0f;
        }

        for (S32 i = 0; i < sFingerCount; i++)
        {
            const Finger& f = sFingers[i];

            switch (f.control)
            {
            case CONTROL_BUTTON:
                // Held while the finger stays near its button; a tap that
                // went down and up since the last poll is held for this one.
                if (f.lifted ? f.landed : ButtonDistance(L, f.which, f.x, f.y, kHoldSlop) <= 1.0f)
                {
                    sHeld |= sBits[f.which];
                }
                sInUse = true;
                break;

            case CONTROL_STICK:
            {
                sInUse = true;
                if (f.lifted)
                {
                    break;
                }

                const F32 r = kStickRadius * L.unit;
                F32 dx = (f.x - sStickCenterX[f.which]) / r;
                F32 dy = (f.y - sStickCenterY[f.which]) / r;
                F32 len = sqrtf(dx * dx + dy * dy);
                if (len > 1.0f)
                {
                    dx /= len;
                    dy /= len;
                }
                else if (len < kStickDeadzone)
                {
                    dx = 0.0f;
                    dy = 0.0f;
                }

                // Up-positive, as iPadHost.h asks.
                sStickX[f.which] = dx;
                sStickY[f.which] = -dy;
                stickHeld[f.which] = true;
                break;
            }

            case CONTROL_DRAG:
                sInUse = true;
                sCameraDX += f.x - f.lastX;
                sCameraDY += f.y - f.lastY;
                break;

            default:
                break;
            }
        }

        for (S32 i = 0; i < STICK_COUNT; i++)
        {
            if (!stickHeld[i])
            {
                sStickCenterX[i] = L.homeX[i];
                sStickCenterY[i] = L.homeY[i];
            }
        }
    }

    // Lifted fingers go; the rest remember where this poll left them.
    void Settle()
    {
        for (S32 i = 0; i < sFingerCount;)
        {
            Finger& f = sFingers[i];
            if (f.lifted)
            {
                f = sFingers[--sFingerCount];
                continue;
            }
            f.lastX = f.x;
            f.lastY = f.y;
            f.landed = false;
            i++;
        }
    }

    U8 Alpha(F32 a)
    {
        F32 v = a * iPadSettingsGet().touch_opacity;
        return (U8)(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
    }

    void Submit(const Layout& L)
    {
        iTouchOverlayShape shapes[ITOUCHOVERLAY_MAX_SHAPES];
        S32 n = 0;

        if (sShowing)
        {
            const F32 u = L.unit;

            // Each stick: a ring for its reach and a knob where it points.
            for (S32 i = 0; i < STICK_COUNT; i++)
            {
                const bool pushed = sStickX[i] != 0.0f || sStickY[i] != 0.0f;

                iTouchOverlayShape& ring = shapes[n++];
                memset(&ring, 0, sizeof(ring));
                ring.x = sStickCenterX[i];
                ring.y = sStickCenterY[i];
                ring.halfWidth = ring.halfHeight = ring.corner = kStickRadius * u;
                ring.stroke = 0.008f * u;
                ring.r = ring.g = ring.b = 255;
                ring.a = Alpha(110.0f);

                iTouchOverlayShape& knob = shapes[n++];
                memset(&knob, 0, sizeof(knob));
                knob.x = sStickCenterX[i] + sStickX[i] * kStickRadius * u;
                knob.y = sStickCenterY[i] - sStickY[i] * kStickRadius * u;
                knob.halfWidth = knob.halfHeight = knob.corner = kStickKnob * u;
                knob.r = knob.g = knob.b = 255;
                knob.a = Alpha(pushed ? 150.0f : 90.0f);
            }

            for (S32 i = 0; i < kButtonCount; i++)
            {
                const Button& b = kButtons[i];
                bool held = sBits[i] != 0 && (sHeld & sBits[i]) == sBits[i];

                iTouchOverlayShape& s = shapes[n++];
                memset(&s, 0, sizeof(s));
                s.x = L.buttonX[i];
                s.y = L.buttonY[i];
                s.halfWidth = b.halfWidth * u;
                s.halfHeight = b.halfHeight * u;
                s.corner = b.halfWidth == b.halfHeight ? s.halfWidth : s.halfHeight * 0.5f;
                s.r = b.r;
                s.g = b.g;
                s.b = b.b;
                // The label is drawn brighter on a solid button; the held
                // highlight is kept above that line whatever the opacity.
                s.a = held ? (U8)(Alpha(200.0f) < 128 ? 128 : Alpha(200.0f)) : Alpha(90.0f);
                s.label = b.label;
            }
        }

        iTouchOverlaySubmit(shapes, n);
    }

    // The drag camera's turn this poll, in the C-stick units iPadHostState's
    // camera takes, smoothed over kCameraSmoothing.
    void DragCamera(iPadHostState* s, F32 h)
    {
        Uint64 now = SDL_GetTicksNS();
        F32 dt = sLastPollNS != 0 ? (F32)(now - sLastPollNS) / 1e9f : 0.0f;
        sLastPollNS = now;
        if (dt > 0.1f)
        {
            dt = 0.1f;
        }

        sCameraOwedX += sCameraDX;
        sCameraOwedY += sCameraDY;

        F32 share = 1.0f - expf(-dt / kCameraSmoothing);
        F32 turnX = sCameraOwedX * share;
        F32 turnY = sCameraOwedY * share;
        sCameraOwedX -= turnX;
        sCameraOwedY -= turnY;

        // The last half pixel goes at once. Owed forever, it would hold the
        // C-stick just past the game's deadzone and the camera would creep on
        // after the finger stopped.
        if (fabsf(sCameraOwedX) < 0.5f)
        {
            turnX += sCameraOwedX;
            sCameraOwedX = 0.0f;
        }
        if (fabsf(sCameraOwedY) < 0.5f)
        {
            turnY += sCameraOwedY;
            sCameraOwedY = 0.0f;
        }

        // As a mouse moves it: a drag right turns the view right, a drag down
        // looks down. TSSM's camera reads only the C-stick, so the drag becomes
        // C-stick deflection the way the mouse does: past the game's 32 in
        // proportion to the distance, a screen height being a full deflection.
        F32 turnStick[2] = { -turnX, -turnY };
        F32* outStick[2] = { &s->camera_x, &s->camera_y };
        for (S32 i = 0; i < 2; i++)
        {
            F32 v = turnStick[i] * 400.0f / h;
            if (v != 0.0f)
            {
                F32 mag = 33.0f + fabsf(v);
                mag = mag > 110.0f ? 110.0f : mag;
                *outStick[i] += v < 0.0f ? -mag : mag;
            }
        }
    }
} // namespace

bool iPadTouchEnabled()
{
    return sEnabled;
}

bool iPadTouchShowing()
{
    return sEnabled && sShowing;
}

void iPadTouchInit()
{
    for (S32 i = 0; i < kButtonCount; i++)
    {
        const iPadBindButton* row = iPadBindFind(kButtons[i].row);
        sBits[i] = row != NULL ? row->mask : 0;
        if (sBits[i] == 0)
        {
            printf("tssm: touch: the input layer has no button row '%s'; the %c button does "
                   "nothing\n",
                   kButtons[i].row, kButtons[i].label);
        }
    }

    const char* mode = iConfigGetString("input.touch_controls", "auto");

    if (strcmp(mode, "on") == 0)
    {
        sEnabled = true;
    }
    else if (strcmp(mode, "off") == 0)
    {
        sEnabled = false;
    }
    else
    {
#ifdef __ANDROID__
        sEnabled = true;
#else
        sEnabled = false;
#endif
    }

    if (sEnabled && !sWatching)
    {
        SDL_AddEventWatch(WatchTouch, NULL);
        sWatching = true;
    }
}

void iPadTouchPoll(iPadHostState* s, bool padOnPort0)
{
    if (!sEnabled)
    {
        return;
    }

    S32 winW = 0;
    S32 winH = 0;
    if (!iTouchOverlayWindowSize(&winW, &winH))
    {
        // Nothing has been presented yet, so there is no size to lay out in:
        // whatever touched the screen before then is let go.
        TakeEvents(NULL);
        Settle();
        return;
    }

    Layout L;
    MakeLayout((F32)winW, (F32)winH, &L);

    bool touched = TakeEvents(&L);
    Evaluate(L);

    if (touched || !padOnPort0)
    {
        sShowing = true;
    }
    else
    {
        // A controller in use takes the screen back.
        if (s->buttons != 0 || s->stick_x != 0.0f || s->stick_y != 0.0f ||
            s->substick_x != 0.0f || s->substick_y != 0.0f)
        {
            sShowing = false;
        }
    }

    Submit(L);
    Settle();

    if (!sShowing)
    {
        sCameraOwedX = 0.0f;
        sCameraOwedY = 0.0f;
        sLastPollNS = 0;
        return;
    }

    DragCamera(s, L.h);

    s->connected = true;
    s->buttons |= sHeld;
    if (sInUse)
    {
        s->sources |= IPAD_HOST_SOURCE_TOUCH;
    }

    // Each stick is taken over when the finger pushes it further than the
    // controller or the keys do, as the keyboard's are.
    if (sStickX[STICK_LEFT] * sStickX[STICK_LEFT] + sStickY[STICK_LEFT] * sStickY[STICK_LEFT] >
        s->stick_x * s->stick_x + s->stick_y * s->stick_y)
    {
        s->stick_x = sStickX[STICK_LEFT];
        s->stick_y = sStickY[STICK_LEFT];
    }
    if (sStickX[STICK_RIGHT] * sStickX[STICK_RIGHT] + sStickY[STICK_RIGHT] * sStickY[STICK_RIGHT] >
        s->substick_x * s->substick_x + s->substick_y * s->substick_y)
    {
        s->substick_x = sStickX[STICK_RIGHT];
        s->substick_y = sStickY[STICK_RIGHT];
    }
}

char iPadTouchButtonLabel(const char* row)
{
    if (row == NULL)
    {
        return 0;
    }
    for (S32 i = 0; i < kButtonCount; i++)
    {
        if (strcmp(kButtons[i].row, row) == 0)
        {
            return kButtons[i].label;
        }
    }
    return 0;
}

bool iPadTouchControlAt(const char* name, F32* x, F32* y)
{
    S32 winW = 0;
    S32 winH = 0;
    if (name == NULL || !iTouchOverlayWindowSize(&winW, &winH))
    {
        return false;
    }

    Layout L;
    MakeLayout((F32)winW, (F32)winH, &L);

    for (S32 i = 0; i < STICK_COUNT; i++)
    {
        if (strcmp(kStickHomes[i].name, name) == 0)
        {
            *x = L.homeX[i] / L.w;
            *y = L.homeY[i] / L.h;
            return true;
        }
    }
    for (S32 i = 0; i < kButtonCount; i++)
    {
        if (strcmp(kButtons[i].row, name) == 0)
        {
            *x = L.buttonX[i] / L.w;
            *y = L.buttonY[i] / L.h;
            return true;
        }
    }
    return false;
}

void iPadTouchTestInsets(F32 left, F32 right, F32 top, F32 bottom)
{
    if (left < 0.0f || right < 0.0f || top < 0.0f || bottom < 0.0f)
    {
        sTestInsets[0] = sTestInsets[1] = sTestInsets[2] = sTestInsets[3] = -1.0f;
        return;
    }
    sTestInsets[0] = left;
    sTestInsets[1] = right;
    sTestInsets[2] = top;
    sTestInsets[3] = bottom;
}
