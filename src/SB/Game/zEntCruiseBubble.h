#ifndef ZENTCRUISEBUBBLE_H

#define ZENTCRUISEBUBBLE_H

#include <types.h>

#include "xAnim.h"
#include "xCamera.h"
#include "xCam.h"
#include "xMath3.h"
#include "xScene.h"
#include "zEnt.h"

struct xModelAssetParam;

// auto_tweak::load_param lives in zAutoTweak.h (included by zEntCruiseBubble.cpp).

struct xBase;

// Public interface of the cruise bubble. In TSSM the internals (states, tweaks, hud, ...)
// live in the cruise_bubble::CruiseBubble namespace inside zEntCruiseBubble.cpp.
namespace cruise_bubble
{
    extern void (*Cam_Launch)();
    extern void (*Cam_Return)();
    extern xCam* xCamLaunchCam;

    U8 CheckAbilityToLaunch();
    xMat4x3* GetMissileMat();
    void reset();
    void add_life(F32 life, F32 max);
    void set_life(F32 life);
    void reset_life();
    bool event_handler(xBase* from, U32 event, const F32* fparam, xBase* to);
    xVec3& location();
    F32 flying();
    U8 active();
    xAnimTable* anim_table();
    void insert_player_animations(xAnimTable& table);
    bool render();
    void render_screen();
    U8 update(xScene* sc, F32 dt);
    void launch();
    void init();
    void deinit();
    void xCamReturn();
    void xCamLaunch();

    namespace CruiseBubble
    {
        void kill(bool reset_camera, bool abortive);
    } // namespace CruiseBubble
} // namespace cruise_bubble

void zEntCruiseBubbleRenderFX();

#endif
