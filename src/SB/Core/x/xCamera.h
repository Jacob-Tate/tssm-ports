#ifndef XCAMERA_H
#define XCAMERA_H

#include "iCamera.h"
// No xDebugTweak.h here: retail xCamera.cpp, iWad, zDust, zSmoke, zSplash and other units that
// include this header (directly or through xGlobals.h) have none of the tweak_callback templates.
#include "xDebug.h"

#include "xBase.h"
#include "xMath3.h"
#include "xBound.h"
#include "xMath2.h"
#include "xMathInlines.h"

#include <rwcore.h>

#include "xPad.h"
#include "xMemMgr.h"

#include <PowerPC_EABI_Support/MSL_C++/MSL_Common/Include/new>

struct xScene;
struct xQCData;
struct xEnt;

enum _tagTransType
{
    eTransType_None,
    eTransType_Interp1,
    eTransType_Interp2,
    eTransType_Interp3,
    eTransType_Interp4,
    eTransType_Linear,
    eTransType_Interp1Rev,
    eTransType_Interp2Rev,
    eTransType_Interp3Rev,
    eTransType_Interp4Rev,
    eTransType_Total
};

struct xCamFollowAsset
{
    F32 rotation;
    F32 distance;
    F32 height;
    F32 rubber_band;
    F32 start_speed;
    F32 end_speed;
};

struct _tagxCamShoulderAsset
{
    F32 distance;
    F32 height;
    F32 realign_speed;
    F32 realign_delay;
};

struct _tagp2CamStaticAsset
{
    U32 unused;
};

struct _tagxCamPathAsset
{
    U32 assetID;
    F32 time_end;
    F32 time_delay;
};

struct _tagp2CamStaticFollowAsset
{
    F32 rubber_band;
};

struct xCamAsset : xBaseAsset
{
    xVec3 pos;
    xVec3 at;
    xVec3 up;
    xVec3 right;
    xVec3 view_offset;
    S16 offset_start_frames;
    S16 offset_end_frames;
    F32 fov;
    F32 trans_time;
    _tagTransType trans_type;
    U32 flags;
    F32 fade_up;
    F32 fade_down;
    union
    {
        xCamFollowAsset cam_follow;
        _tagxCamShoulderAsset cam_shoulder;
        _tagp2CamStaticAsset cam_static;
        _tagxCamPathAsset cam_path;
        _tagp2CamStaticFollowAsset cam_staticFollow;
    };
    U32 valid_flags;
    U32 markerid[2];
    U8 cam_type;
    U8 pad[3];
};

struct xCamera : xBase
{
    RwCamera* lo_cam;
    // Offset: 0x14
    xMat4x3 mat;
    // Offset: 0x54
    xMat4x3 omat;
    xMat3x3 mbasis;
    xBound bound;
    xMat4x3* tgt_mat;
    xMat4x3* tgt_omat;
    xBound* tgt_bound;
    xVec3 focus;
    xScene* sc;
    xVec3 tran_accum;
    F32 fov;
    U32 flags;
    F32 tmr;
    F32 tm_acc;
    F32 tm_dec;
    F32 ltmr;
    F32 ltm_acc;
    F32 ltm_dec;
    F32 dmin;
    F32 dmax;
    F32 dcur;
    F32 dgoal;
    F32 hmin;
    F32 hmax;
    F32 hcur;
    F32 hgoal;
    F32 pmin;
    F32 pmax;
    F32 pcur;
    F32 pgoal;
    F32 depv;
    F32 hepv;
    F32 pepv;
    F32 orn_epv;
    F32 yaw_epv;
    F32 pitch_epv;
    F32 roll_epv;
    xQuat orn_cur;
    xQuat orn_goal;
    xQuat orn_diff;
    F32 yaw_cur;
    F32 yaw_goal;
    F32 pitch_cur;
    F32 pitch_goal;
    F32 roll_cur;
    F32 roll_goal;
    F32 dct;
    F32 dcd;
    F32 dccv;
    F32 dcsv;
    F32 hct;
    F32 hcd;
    F32 hccv;
    F32 hcsv;
    F32 pct;
    F32 pcd;
    F32 pccv;
    F32 pcsv;
    F32 orn_ct;
    F32 orn_cd;
    F32 orn_ccv;
    F32 orn_csv;
    F32 yaw_ct;
    F32 yaw_cd;
    F32 yaw_ccv;
    F32 yaw_csv;
    F32 pitch_ct;
    F32 pitch_cd;
    F32 pitch_ccv;
    F32 pitch_csv;
    F32 roll_ct;
    F32 roll_cd;
    F32 roll_ccv;
    F32 roll_csv;
    xVec4 frustplane[12];
    F32 smoothOutwardSlidePos;
};

struct xBinaryCamera
{
    struct zone_data
    {
        F32 distance;
        F32 height;
        F32 height_focus;
    };

    struct config
    {
        zone_data zone_rest;
        zone_data zone_above;
        zone_data zone_below;
        F32 move_speed;
        F32 turn_speed;
        F32 stick_speed;
        F32 stick_yaw_vel;
        F32 max_yaw_vel;
        F32 margin_angle;
    };

    config cfg;
    xCamera* camera;
    xQuat cam_dir;
    xVec3* s1;
    xVec3* s2;
    F32 s2_radius;
    xVec2 stick_offset;

    void init();
    void start(xCamera& camera);
    void stop();
    void update(F32 dt);
    void add_tweaks(char const*);
    void set_targets(xVec3 const& par_1, xVec3 const& par_2, F32 par_3);
    // No symbol (BfBB: weak, empty).
    void render_debug()
    {
    }
};

F32 xVec3Length(const xVec3* vec);
void xCameraInit(xCamera* cam, U32 width, U32 height);
void xCameraSetScene(xCamera* cam, xScene* sc);
void xCameraReset(xCamera* cam, F32 d, F32 h, F32 pitch);
void xCameraExit(xCamera* cam);
void xCameraUpdate(xCamera* cam, F32 dt);
void xCameraBegin(xCamera* cam, S32);
void xCameraEnd(xCamera* cam, F32 seconds, S32 update_scrn_fx);
void xCameraShowRaster(xCamera* cam);
// No GC symbol (BfBB: weak, an inline accessor).
inline F32 xCameraGetFOV(const xCamera* cam)
{
    return cam->fov;
}
// No GC symbol (always inlined): unlike BfBB it only stores the field.
inline void xCameraSetFOV(xCamera* cam, F32 fov)
{
    cam->fov = fov;
}
void xCameraMove(xCamera* cam, U32 flags, F32 dgoal, F32 hgoal, F32 pgoal, F32 tm, F32 tm_acc,
                 F32 tm_dec);
void xCameraMove(xCamera* cam, const xVec3& loc);
void xCameraMove(xCamera* cam, const xVec3& loc, F32 maxSpeed);
void xCameraFOV(xCamera* cam, F32 fov, F32 maxSpeed, F32 dt);
void xCameraRotate(xCamera* cam, const xMat3x3& m, F32 time, F32 accel, F32 decl);
void xCameraRotate(xCamera* cam, const xVec3& v, F32 roll, F32 time, F32 accel, F32 decl);
void xCameraLook(xCamera* cam, U32 flags, const xQuat* orn_goal, F32 tm, F32 tm_acc, F32 tm_dec);
void xCameraLookYPR(xCamera* cam, U32 flags, F32 yaw, F32 pitch, F32 roll, F32 tm, F32 tm_acc,
                    F32 tm_dec);
void xCameraDoCollisions(S32 do_collis, S32 owner);
void SweptSphereHitsCameraEnt(xScene*, xRay3* ray, xQCData* qcd, xEnt* ent, void* data);
void xCameraSetTargetMatrix(xCamera* cam, xMat4x3* mat);
void xCameraSetTargetOMatrix(xCamera* cam, xMat4x3* mat);
struct cameraFXShake
{
    F32 magnitude;
    xVec3 dir;
    F32 cycleTime;
    F32 cycleMax;
    F32 dampen;
    F32 dampenRate;
    F32 rotate_magnitude;
    F32 radius;
    const xVec3* epicenterP;
    xVec3 epicenter;
    const xVec3* player;
};

struct cameraFXZoom
{
    F32 holdTime;
    F32 vel;
    F32 accel;
    F32 distance;
    U32 mode;
    F32 velCur;
    F32 distanceCur;
    F32 holdTimeCur;
};

#define CAMERAFX_ZOOM_MODE_0 0
#define CAMERAFX_ZOOM_MODE_1 1
#define CAMERAFX_ZOOM_MODE_2 2
#define CAMERAFX_ZOOM_MODE_3 3

struct cameraFX
{
    S32 type;
    S32 flags;
    F32 elapsedTime;
    F32 maxTime;
    union
    {
        cameraFXShake shake;
        cameraFXZoom zoom;
    };
};

#define CAMERAFX_TYPE_NONE 0
#define CAMERAFX_TYPE_ZOOM 1
#define CAMERAFX_TYPE_SHAKE 2

struct cameraFXTableEntry
{
    S32 type;
    void (*func)(cameraFX*, F32, const xMat4x3*, xMat4x3*);
    void (*funcKill)(cameraFX*);
};

cameraFX* xCameraFXShake(F32 maxTime, F32 magnitude, F32 cycleMax, F32 rotate_magnitude, F32 radius,
                         const xVec3* epicenter, const xVec3* player, bool shakeY);
void xCameraFXShakeEnd(cameraFX* fx, F32 fadeOutTime);
cameraFX* xCameraFXShakeForever(F32 magnitude, F32 cycleMax, F32 rotate_magnitude, F32 radius,
                                const xVec3* epicenter, const xVec3* player, bool shakeY);
void xCameraFXBegin(const xMat4x3& mat);
void xCameraFXUpdate(xMat4x3& mat, RwCamera* icam, F32 dt);
void xCameraFXEnd(xMat4x3& mat, RwCamera* icam);
void xCameraFXKill();

// The screen camera (methods in xCam.cpp). Declared here, not in xCam.h: units that only reach it
// through xglobals->screen have none of xCam.h's .rodata templates in retail.
struct xCamScreen
{
    RwCamera* icam;
    F32 fov;

    void create(S32 w, S32 h);
    void destroy();
    void set_scene(xScene& scene);
    void prepare_render();
    void begin_render(bool clear);
    void end_render();
    void set_world_matrix(const xMat4x3& mat);
    void set_fov(F32 fov);
};

#endif
