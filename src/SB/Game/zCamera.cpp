#include "xpkrsvc.h"
#include "xScrFx.h"
#include "xstransvc.h"
#include "zCamera.h"
#include "zCameraFly.h"
#include "zCameraTweak.h"
#include "zCutsceneMgr.h"
#include "zGlobals.h"
#include "zMain.h"

#ifdef PLATFORM_PC
#include "iCameraOrbit.h"
#endif

F32 zcam_pad_pyaw_scale = 0.18124573f; // unknown math
F32 zcam_pad_pitch_scale = 1.0f / 52;
F32 zcam_near_d = 3.0f;
F32 zcam_near_h = 1.8f;
F32 zcam_near_pitch = DEG2RAD(10);
F32 zcam_far_d = 5.0f;
F32 zcam_far_h = 3.0f;
F32 zcam_far_pitch = DEG2RAD(15);
F32 zcam_wall_d = 7.5f;
F32 zcam_wall_h = 2.0f;
F32 zcam_wall_pitch = DEG2RAD(18);
F32 zcam_above_d = 0.2f;
F32 zcam_above_h = 2.2f;
F32 zcam_above_pitch = DEG2RAD(70);
F32 zcam_below_d = 0.6f;
F32 zcam_below_h = 0.2f;
F32 zcam_below_pitch = DEG2RAD(-70);
F32 zcam_highbounce_d = 0.2f;
F32 zcam_highbounce_h = 5.0f;
F32 zcam_highbounce_pitch = DEG2RAD(89);
F32 zcam_overrot_min = DEG2RAD(25);
F32 zcam_overrot_mid = DEG2RAD(90);
F32 zcam_overrot_max = DEG2RAD(170);
F32 zcam_overrot_rate = 0.1f;
F32 zcam_overrot_tstart = 1.5f;
F32 zcam_overrot_tend = 2.5f;
F32 zcam_overrot_velmin = 3.0f;
F32 zcam_overrot_velmax = 5.0f;
F32 zcam_overrot_tmanual = 1.5f;
F32 zcam_overrot_tmr;
S32 zcam_near;
U8 zcam_centering;
U8 zcam_lastcentering;
S32 zcam_mode;
S32 zcam_bbounce;
S32 zcam_lbbounce;
S32 zcam_convers;
S32 zcam_lconvers;
S32 zcam_longbounce;
S32 zcam_highbounce;
S32 zcam_cutscene;
S32 zcam_reward;
xVec3* zcam_playervel;
F32 zcam_mintgtheight = FLOAT_MIN;
S32 zcam_fly;
S32 zcam_flypaused;
void* zcam_flydata;
U32 zcam_flysize;
F32 zcam_flytime;
F32 zcam_flyrate;
S32 zcam_flyJustStopped;
xCamAsset* zcam_dest;
F32 zcam_tmr;
F32 zcam_ttm;
F32 zcam_fovcurr;
F32 zcam_fovdest;
static bool sNearToggleEnabled;
static F32 sCenteringFactorDP;

namespace
{
    U32 stop_track;
    bool input_enabled = true;
    bool lassocam_enabled;
    F32 lassocam_factor;
    bool changing_folcam_orientation;
    F32 change_folcam_timer;
    WallJumpViewState wall_jump_enabled;
    // follow_cam is defined with the other camera globals in zCam.cpp
    xVec3 wall_jump_view;
    F32 dMultiplier;
    F32 dOffset;
    F32 hMultiplier;
    F32 hOffset;

    inline F32 _GetCurrentH()
    {
        if (zcam_highbounce != 0)
        {
            return zcam_highbounce_h;
        }

        if (wall_jump_enabled == WJVS_ENABLED)
        {
            return zcam_wall_h;
        }

        return zCameraTweakGlobal_GetH();
    }

    inline F32 GetCurrentH()
    {
        return dMultiplier * _GetCurrentH() + dOffset;
    }

    inline F32 GetCurrentPitch()
    {
        if (zcam_highbounce != 0)
        {
            return zcam_highbounce_pitch;
        }

        return zCameraTweakGlobal_GetPitch();
    }

    inline F32 _GetCurrentD()
    {
        if (zcam_highbounce != 0)
        {
            return zcam_highbounce_d;
        }

        if (wall_jump_enabled == WJVS_ENABLED)
        {
            return zcam_wall_d;
        }

        return zCameraTweakGlobal_GetD();
    }

    inline F32 GetCurrentD()
    {
        return dMultiplier * _GetCurrentD() + dOffset;
    }
} // namespace

static F32 rewardMove = 1.0f;
static F32 rewardMoveSpeed = 1.1f;
static F32 rewardZoomSpeed = 6.0f;
static F32 rewardZoomAmount = 100.0f;
static F32 rewardTiltTime = 1.5f;
static F32 rewardTiltAmount = -0.22f;

// No symbol (always inlined into ZDSP_elcb_event); the name is a guess.
inline void zCameraSetFollowOrientation(F32 yaw, F32 time)
{
    F32 angle = xAngleClamp((PI / 180.0f) * (180.0f + yaw));
    if (angle == 0.0f)
    {
        angle = 2.0f * PI;
    }

    changing_folcam_orientation = true;
    change_folcam_timer = time;
    zCameraReset(&globals.oldSkoolCamera);
    input_enabled = false;
    xCameraMove(&globals.oldSkoolCamera, globals.oldSkoolCamera.flags, GetCurrentD(), GetCurrentH(),
                angle, time, 0.0f, 0.0f);
}

void zCameraReset(xCamera* cam)
{
    zcam_mode = 0;
    zcam_bbounce = 0;
    zcam_lbbounce = 0;
    zcam_lconvers = 0;
    zcam_longbounce = 0;
    zcam_highbounce = 0;
    zcam_convers = 0;
    zcam_fly = 0;
    zcam_flypaused = 0;
    zcam_cutscene = 0;
    zcam_reward = 0;

    zcam_fovcurr = 75.0f;
    zcam_overrot_tmr = 0.0f;
    wall_jump_enabled = WJVS_DISABLED;
    lassocam_enabled = '\0';
    stop_track = 0;
    zcam_mintgtheight = FLOAT_MIN;
    zcam_centering = '\0';
    zcam_lastcentering = '\0';
    sNearToggleEnabled = '\0';

    cam->fov = 75.0f;
    zCameraTweakGlobal_Update(0.0f);
    xCameraReset(cam, GetCurrentD(), GetCurrentH(), GetCurrentPitch());

    input_enabled = true;
    dMultiplier = 1.0f;
    dOffset = 0.0f;
    hMultiplier = 1.0f;
    hOffset = 0.0f;

    if (follow_cam != NULL)
    {
        follow_cam->cfg_common.priority = '\0';
    }
}

static F32 EaseInOut(F32 param)
{
    return param * (param * (2.0f - 1.5f * param) + 0.5f);
}

static void zCameraConversUpdate(xCamera* cam, F32 dt)
{
    F32 s;
    xQuat a;
    xQuat c;

    if (zcam_dest == NULL)
    {
        return;
    }

    if (zcam_tmr <= 0.0f)
    {
        zcam_tmr = 0.0f;
        return;
    }

    if (dt / zcam_tmr > 1.0f)
    {
        cam->mat.right = zcam_dest->right;
        cam->mat.up = zcam_dest->up;
        cam->mat.at = zcam_dest->at;
        cam->mat.pos = zcam_dest->pos;

        zcam_fovcurr = zcam_fovdest;
    }
    else
    {
        s = EaseInOut(1.0f - zcam_tmr / zcam_ttm);
        s = (EaseInOut(1.0f - (zcam_tmr - dt) / zcam_ttm) - s) / (1.0f - s);

        xQuatFromMat(&a, &cam->mat);
        xQuatSlerp(&c, &a, &zcam_quat, s);
        xQuatToMat(&c, &cam->mat);
        xVec3Lerp(&cam->mat.pos, &cam->mat.pos, &zcam_dest->pos, s);
        zcam_fovcurr = zcam_fovcurr * (1.0f - s) + zcam_fovdest * s;
    }

    zcam_tmr -= dt;
}

static inline F32 TranSpeed(zFlyKey keys[])
{
    return 30.0f * xVec3Dist((xVec3*)&keys[0].matrix[9], (xVec3*)&keys[1].matrix[9]);
}

static inline F32 MatrixSpeed(zFlyKey keys[])
{
    F32 dot = iabs(xVec3Dot((xVec3*)&keys[0].matrix[0], (xVec3*)&keys[1].matrix[0]));
    dot = MAX(iabs(xVec3Dot((xVec3*)&keys[0].matrix[3], (xVec3*)&keys[1].matrix[3])), dot);
    dot = MAX(iabs(xVec3Dot((xVec3*)&keys[0].matrix[6], (xVec3*)&keys[1].matrix[6])), dot);

    return xacos(MIN(1.0f, dot)) * 114.59155f * 30.0f;
}

static S32 zCameraFlyUpdate(xCamera* cam, F32 dt)
{
    S32 i;
    S32 flyIdx;
    S32 numKeys;
    F32 flyLerp;
    F32 flyFrame;
    zFlyKey keys[4];
    // not in the DWARF: locals of the GameCube-only byte swap of the fly keys
    U8* data;
    U8 tmpA;
    U8 tmpB;
    F32 matdiff1;
    F32 matdiff2;
    F32 matdiff3;
    xMat3x3 tmpMat;
    xQuat quats[2];
    xQuat qresult;

    if ((globals.pad0->pressed & 0x50000) && zcam_flytime > gSkipTimeFlythrough)
    {
        zcam_flytime = 0.033333335f * zcam_flysize;
    }

    flyFrame = 30.0f * zcam_flytime;
    numKeys = std::floorf(flyFrame);
    flyLerp = flyFrame - std::floorf(flyFrame);

    if (numKeys >= (S32)(zcam_flysize >> 6) - 1)
    {
        return 0;
    }

    flyIdx = numKeys - 1 >= 0 ? numKeys - 1 : numKeys;
    keys[0] = *((zFlyKey*)zcam_flydata + flyIdx);
    keys[1] = *((zFlyKey*)zcam_flydata + numKeys);
    keys[2] = *((zFlyKey*)zcam_flydata + (numKeys + 1));

    flyIdx = numKeys + 2 < (S32)(zcam_flysize >> 6) ? numKeys + 2 : numKeys + 1;
    keys[3] = *((zFlyKey*)zcam_flydata + flyIdx);

#ifndef PLATFORM_PC
    // Reverses the byte order (endianness) of 64 4-byte blocks
    data = (U8*)&keys[0].frame;
    for (i = 64; i > 0; i--)
    {
        tmpA = data[0];
        tmpB = data[1];
        data[0] = data[3];
        data[1] = data[2];
        data[2] = tmpB;
        data[3] = tmpA;

        data += 4;
    }
#else
    // PLATFORM_PC: the fly keys ('FLY ' assets, the level fly-throughs) are little-endian in the
    // files, which is why the console swaps them here. The port reads the Xbox files on a
    // little-endian host, so they are already in the host's order; swapping them made every key
    // garbage and threw the fly-through camera to random places (into the floor, out of the world).
    (void)data;
    (void)tmpA;
    (void)tmpB;
#endif

    if (0 < numKeys)
    {
        matdiff1 = TranSpeed(&keys[0]);
        matdiff2 = TranSpeed(&keys[1]);
        matdiff3 = TranSpeed(&keys[2]);

        if (matdiff2 > 10.0f && matdiff2 > 5.0f * matdiff1 && matdiff2 > 5.0f * matdiff3)
        {
            flyLerp = 0.0f;
        }
        else
        {
            matdiff1 = MatrixSpeed(&keys[0]);
            matdiff2 = MatrixSpeed(&keys[1]);
            matdiff3 = MatrixSpeed(&keys[2]);

            if (matdiff2 > 45.0f && matdiff2 > 5.0f * matdiff1 && matdiff2 > 5.0f * matdiff3)
            {
                flyLerp = 0.0f;
            }
        }
    }

    for (i = 0; i < 2; i++)
    {
        tmpMat.right.x = -keys[i + 1].matrix[0];
        tmpMat.right.y = -keys[i + 1].matrix[1];
        tmpMat.right.z = -keys[i + 1].matrix[2];

        tmpMat.up.x = keys[i + 1].matrix[3];
        tmpMat.up.y = keys[i + 1].matrix[4];
        tmpMat.up.z = keys[i + 1].matrix[5];

        tmpMat.at.x = -keys[i + 1].matrix[6];
        tmpMat.at.y = -keys[i + 1].matrix[7];
        tmpMat.at.z = -keys[i + 1].matrix[8];

        xQuatFromMat(&quats[i], &tmpMat);
    }

    xQuatSlerp(&qresult, &quats[0], &quats[1], flyLerp);
    xQuatToMat(&qresult, &cam->mat);
    xVec3Lerp(&cam->mat.pos, (xVec3*)&keys[1].matrix[9], (xVec3*)&keys[2].matrix[9], flyLerp);

    zcam_flytime += dt * zcam_flyrate;

    return 1;
}

void zCameraFlyStart(U32 assetID)
{
    PKRAssetTOCInfo info;
    if (xSTGetAssetInfo(assetID, &info) == 0)
    {
        return;
    }

    follow_cam->cfg_common.priority = '\x7f';
    zcam_fly = 1;
    zcam_flypaused = 0;
    zcam_flydata = info.mempos;
    zcam_flysize = info.size;
    zcam_flytime = 0.033333335f;
    zcam_flyasset_current = assetID;
    zcam_flyrate = 1.0f;

    zEntPlayerControlOff(CONTROL_OWNER_FLY_CAM);
    xScrFxLetterbox(1);

    zcam_backupcam = globals.oldSkoolCamera;
}

// WIP
static void zCameraFlyRestoreBackup(xCamera* backup)
{
    globals.oldSkoolCamera.mat = backup->mat;
    globals.oldSkoolCamera.omat = backup->omat;
    globals.oldSkoolCamera.mbasis = backup->mbasis;
    globals.oldSkoolCamera.bound = backup->bound;
    globals.oldSkoolCamera.focus = backup->focus;

    globals.oldSkoolCamera.flags = backup->flags;
    globals.oldSkoolCamera.tmr = backup->tmr;
    globals.oldSkoolCamera.tm_acc = backup->tm_acc;
    globals.oldSkoolCamera.tm_dec = backup->tm_dec;
    globals.oldSkoolCamera.ltmr = backup->ltmr;
    globals.oldSkoolCamera.ltm_acc = backup->ltm_acc;
    globals.oldSkoolCamera.ltm_dec = backup->ltm_dec;
    globals.oldSkoolCamera.dmin = backup->dmin;
    globals.oldSkoolCamera.dmax = backup->dmax;
    globals.oldSkoolCamera.dcur = backup->dcur;
    globals.oldSkoolCamera.dgoal = backup->dgoal;
    globals.oldSkoolCamera.hmin = backup->hmin;
    globals.oldSkoolCamera.hmax = backup->hmax;
    globals.oldSkoolCamera.hcur = backup->hcur;
    globals.oldSkoolCamera.hgoal = backup->hgoal;
    globals.oldSkoolCamera.pmin = backup->pmin;
    globals.oldSkoolCamera.pmax = backup->pmax;
    globals.oldSkoolCamera.pcur = backup->pcur;
    globals.oldSkoolCamera.pgoal = backup->pgoal;
    globals.oldSkoolCamera.depv = backup->depv;
    globals.oldSkoolCamera.hepv = backup->hepv;
    globals.oldSkoolCamera.pepv = backup->pepv;
    globals.oldSkoolCamera.orn_epv = backup->orn_epv;
    globals.oldSkoolCamera.yaw_epv = backup->yaw_epv;
    globals.oldSkoolCamera.pitch_epv = backup->pitch_epv;
    globals.oldSkoolCamera.roll_epv = backup->roll_epv;
    globals.oldSkoolCamera.orn_cur = backup->orn_cur;
    globals.oldSkoolCamera.orn_goal = backup->orn_goal;
    globals.oldSkoolCamera.orn_diff = backup->orn_diff;
    globals.oldSkoolCamera.yaw_cur = backup->yaw_cur;
    globals.oldSkoolCamera.yaw_goal = backup->yaw_goal;
    globals.oldSkoolCamera.pitch_cur = backup->pitch_cur;
    globals.oldSkoolCamera.pitch_goal = backup->pitch_goal;
    globals.oldSkoolCamera.roll_cur = backup->roll_cur;
    globals.oldSkoolCamera.roll_goal = backup->roll_goal;
    globals.oldSkoolCamera.dct = backup->dct;
    globals.oldSkoolCamera.dcd = backup->dcd;
    globals.oldSkoolCamera.dccv = backup->dccv;
    globals.oldSkoolCamera.dcsv = backup->dcsv;
    globals.oldSkoolCamera.hct = backup->hct;
    globals.oldSkoolCamera.hcd = backup->hcd;
    globals.oldSkoolCamera.hccv = backup->hccv;
    globals.oldSkoolCamera.hcsv = backup->hcsv;
    globals.oldSkoolCamera.pct = backup->pct;
    globals.oldSkoolCamera.pcd = backup->pcd;
    globals.oldSkoolCamera.pccv = backup->pccv;
    globals.oldSkoolCamera.pcsv = backup->pcsv;
    globals.oldSkoolCamera.orn_ct = backup->orn_ct;
    globals.oldSkoolCamera.orn_cd = backup->orn_cd;
    globals.oldSkoolCamera.orn_ccv = backup->orn_ccv;
    globals.oldSkoolCamera.orn_csv = backup->orn_csv;
    globals.oldSkoolCamera.yaw_ct = backup->yaw_ct;
    globals.oldSkoolCamera.yaw_cd = backup->yaw_cd;
    globals.oldSkoolCamera.yaw_ccv = backup->yaw_ccv;
    globals.oldSkoolCamera.yaw_csv = backup->yaw_csv;
    globals.oldSkoolCamera.pitch_ct = backup->pitch_ct;
    globals.oldSkoolCamera.pitch_cd = backup->pitch_cd;
    globals.oldSkoolCamera.pitch_ccv = backup->pitch_ccv;
    globals.oldSkoolCamera.pitch_csv = backup->pitch_csv;
    globals.oldSkoolCamera.roll_ct = backup->roll_ct;
    globals.oldSkoolCamera.roll_cd = backup->roll_cd;
    globals.oldSkoolCamera.roll_ccv = backup->roll_ccv;
    globals.oldSkoolCamera.roll_csv = backup->roll_csv;
}

void zCameraFlyStop()
{
    if (zcam_fly)
    {
        zcam_fly = 0;
        zcam_flypaused = 0;
        follow_cam->cfg_common.priority = 0;
        zEntPlayerControlOn(CONTROL_OWNER_FLY_CAM);
        xScrFxLetterbox(0);
        zCameraFlyRestoreBackup(&zcam_backupcam);
        zcam_flyasset_current = 0;
        globals.oldSkoolCamera.fov = 75.0f;
        zcam_flyJustStopped = 1;
    }
}

static S32 zCameraRewardUpdate(xCamera* cam, F32 dt)
{
    xCameraUpdate(cam, dt);

    xVec3 v = { 0.0f, 0.0f, 0.0f };

    xVec3Copy(&v, (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos);
    v.y -= 0.7f;

    if (zcam_near)
    {
        if (globals.player.s->pcType == ePlayer_SB)
        {
            rewardMove = 1.3f;
            rewardMoveSpeed = 0.68f;
            rewardZoomSpeed = 7.1f;
            rewardZoomAmount = 108.0f;
            rewardTiltTime = 1.5f;
            rewardTiltAmount = -0.22f;
        }
        else if (globals.player.s->pcType == ePlayer_Patrick)
        {
            rewardMove = 1.6f;
            rewardMoveSpeed = 0.68f;
            rewardZoomSpeed = 7.1f;
            rewardZoomAmount = 108.0f;
            rewardTiltTime = 1.0f;
            rewardTiltAmount = -0.25f;
        }
    }
    else
    {
        if (globals.player.s->pcType == ePlayer_SB)
        {
            rewardMove = 1.5f;
            rewardMoveSpeed = 1.1f;
            rewardZoomSpeed = 5.9f;
            rewardZoomAmount = 100.0f;
            rewardTiltTime = 1.5f;
            rewardTiltAmount = -0.2f;
        }
        else if (globals.player.s->pcType == ePlayer_Patrick)
        {
            rewardMove = 1.5f;
            rewardMoveSpeed = 1.1f;
            rewardZoomSpeed = 5.9f;
            rewardZoomAmount = 100.0f;
            rewardTiltTime = 1.5f;
            rewardTiltAmount = -0.2f;
        }
    }

    if (xVec3Dist2((xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos,
                   &globals.oldSkoolCamera.mat.pos) > rewardMove * rewardMove)
    {
        xCameraMove(cam, v, rewardMoveSpeed * dt);
        xCameraFOV(cam, rewardZoomAmount, rewardZoomSpeed, dt);
    }

    xCameraLookYPR(cam, 0, globals.oldSkoolCamera.yaw_cur, rewardTiltAmount,
                   globals.oldSkoolCamera.roll_cur, rewardTiltTime, 0.1f, 0.1f);

    return 1;
}

static void zCameraFreeLookSetGoals(xCamera* cam, F32 pitch_s, F32& dgoal, F32& hgoal,
                                    F32& pitch_goal, F32& lktm, F32 dt)
{
    F32 s;

    if (zcam_bbounce)
    {
        if (zcam_highbounce)
        {
            dgoal = GetCurrentD();
            hgoal = GetCurrentH();
            pitch_goal = GetCurrentPitch();
            return;
        }

        dgoal = zcam_near ? 3.5f : GetCurrentD();
        hgoal = zcam_near ? 2.4f : GetCurrentH();

        if (zcam_longbounce)
        {
            F32 len = xsqrt(zcam_playervel->x * zcam_playervel->x +
                            zcam_playervel->y * zcam_playervel->y +
                            zcam_playervel->z * zcam_playervel->z);

            // Retail materialises the validity test as a bool before branching on it.
            bool lenValid = false;
            if (zcam_playervel != NULL)
            {
                if (len)
                {
                    lenValid = true;
                }
            }

            if (lenValid)
            {
                s = -MIN(0.0f,
                         (cam->mat.at.x * zcam_playervel->x + cam->mat.at.y * zcam_playervel->y +
                          cam->mat.at.z * zcam_playervel->z) /
                             len);
            }
            else
            {
                s = 0.0f;
            }

            pitch_goal = zcam_near ? (PI / 180.0f) * (20.0f * s + 20.0f) : 0.5235988f;
        }
        else
        {
            pitch_goal = zcam_near ? 0.6981317f : 0.5235988f;
        }
        return;
    }

    F32 d = GetCurrentD();
    F32 h = GetCurrentH();
    F32 p = GetCurrentPitch();

    if (lassocam_enabled && !stop_track)
    {
        dgoal = lassocam_factor * (d - zcam_near_d) + zcam_near_d;
        hgoal = lassocam_factor * (h - zcam_near_h) + zcam_near_h;
        pitch_goal = lassocam_factor * (p - zcam_near_pitch) + zcam_near_pitch;
        return;
    }

    if (pitch_s > 0.0f)
    {
        dgoal = pitch_s * (zcam_below_d - d) + d;
        hgoal = pitch_s * (zcam_below_h - h) + h;
        pitch_goal = pitch_s * (pitch_s * (pitch_s * (zcam_below_pitch - p))) + p;
    }
    else
    {
        dgoal = -pitch_s * (zcam_above_d - d) + d;
        hgoal = -pitch_s * (zcam_above_h - h) + h;
        pitch_goal = -pitch_s * (zcam_above_pitch - p) + p;
    }

    if (lktm > 0.1f)
    {
        lktm -= dt;
        if (lktm < 0.1f)
        {
            lktm = 0.1f;
        }
    }
    else
    {
        lktm = 0.1f;
    }
}

S32 zCameraIsFlyCamRunning()
{
    return zcam_fly;
}

void zCameraUpdate(xCamera* cam, F32 dt)
{
    xVec3 tran_accum;
    F32 minDist;
    F32 dist;
    F32 tgtHeight;
    F32 oldTgtHeight;
    F32 plerp;
    F32 dlerp;
    F32 vertical_lerp;
    xVec3 delta;
    F32 dgoal;
    F32 hgoal;
    F32 pgoal;
    F32 yaw_goal;
    F32 pitch_goal;

    zcam_near &= 0x1;
    zcam_flyJustStopped = 0;

    zCameraTweakGlobal_Update(dt);

    tran_accum = cam->tran_accum;
    cam->tran_accum.x = 0.0f;
    cam->tran_accum.y = 0.0f;
    cam->tran_accum.z = 0.0f;

    if (globals.cmgr != NULL && globals.cmgr->csn->Ready != 0)
    {
        if (zcam_fly != 0 && zcam_flypaused != 0)
        {
            zCameraFlyStop();
        }

        if (zcam_cutscene == 0)
        {
            zcam_backupcam = globals.oldSkoolCamera;
            zcam_cutscene = 1;
        }

        iCameraUpdatePos(cam->lo_cam, &cam->mat);
        return;
    }

    if (zcam_cutscene != 0)
    {
        zCameraFlyRestoreBackup(&zcam_backupcam);
        cam->fov = 75.0f;
        zcam_cutscene = 0;
    }

    if (zcam_convers != 0)
    {
        zCameraConversUpdate(cam, dt);
        iCameraUpdatePos(cam->lo_cam, &cam->mat);
        cam->fov = zcam_fovcurr;
        zcam_lconvers = zcam_convers;
        return;
    }

    if (zcam_reward != 0)
    {
        minDist = zcam_near != 0 ? 12.139999f : 33.9f;
        dist = SQR(cam->dcur) + SQR(cam->hcur);

        if (stop_track & CO_REWARDANIM)
        {
            zCameraRewardUpdate(cam, dt);
            return;
        }

        if (dist > minDist)
        {
            zCameraDisableTracking(CO_REWARDANIM);
        }
    }

    if (zcam_fly != 0)
    {
        if (zCameraFlyUpdate(cam, dt) != 0)
        {
            iCameraUpdatePos(cam->lo_cam, &cam->mat);
            return;
        }

        if (zcam_flypaused == 0)
        {
            zCameraFlyProcessStopEvent();
        }

        if (globals.cmgr != NULL)
        {
            zcam_flypaused = 1;
            return;
        }

        zCameraFlyStop();
    }

    if (stop_track != 0)
    {
        if (stop_track & CO_OOB)
        {
            xCameraUpdate(cam, dt);
        }
        else
        {
            iCameraUpdatePos(cam->lo_cam, &cam->mat);
        }
        return;
    }

    tgtHeight = cam->tgt_mat->pos.y;
    oldTgtHeight = cam->tgt_omat->pos.y;

    if (tgtHeight < zcam_mintgtheight)
    {
        cam->tgt_omat->pos.y += zcam_mintgtheight - tgtHeight;
        cam->tgt_mat->pos.y += zcam_mintgtheight - cam->tgt_mat->pos.y;
    }

    plerp = 5.729579f * (xabs(cam->pitch_cur) - 1.2217305f);
    plerp = CLAMP(plerp, 0.0f, 1.0f);

    dist = xsqrt(SQR(cam->mat.pos.x - cam->tgt_mat->pos.x) +
                 SQR(cam->mat.pos.z - cam->tgt_mat->pos.z));

    dlerp = 1.6666666f * (1.2f - dist);
    dlerp = CLAMP(dlerp, 0.0f, 1.0f);

    vertical_lerp = MAX(dlerp, plerp);

    if (vertical_lerp)
    {
        delta.x = vertical_lerp * (cam->tgt_mat->pos.x - cam->tgt_omat->pos.x - tran_accum.x);
        delta.y = vertical_lerp * (cam->tgt_mat->pos.y - cam->tgt_omat->pos.y - tran_accum.y);
        delta.z = vertical_lerp * (cam->tgt_mat->pos.z - cam->tgt_omat->pos.z - tran_accum.z);

        cam->mat.pos.x += delta.x;
        cam->mat.pos.y += delta.y;
        cam->mat.pos.z += delta.z;

        cam->omat.pos = cam->mat.pos;
    }

    static F32 mvtm = 0.1f;
    static F32 lktm = 0.1f;
    static F32 pitch_s = 0.0f;

    if (wall_jump_enabled == WJVS_ENABLING || wall_jump_enabled == WJVS_DISABLING)
    {
        mvtm = 0.3f;
        lktm = 0.3f;

        if (wall_jump_enabled == WJVS_ENABLING)
        {
            wall_jump_enabled = WJVS_ENABLED;
        }
    }

    // Holding L for half a second toggles the near camera; a shorter press recenters it (below).
    if (globals.pad0->pressed & XPAD_BUTTON_L1)
    {
        sNearToggleEnabled = true;
    }
    else if (globals.pad0->released & XPAD_BUTTON_L1)
    {
        sNearToggleEnabled = false;
    }

    if (sNearToggleEnabled && (globals.pad0->on & XPAD_BUTTON_L1) &&
        globals.pad0->down_tmr[8] > 0.5f)
    {
        sNearToggleEnabled = false;

        if (input_enabled && !(zcam_near & 0x2))
        {
            zcam_near ^= 0x1;
            mvtm = 0.3f;
            lktm = 0.3f;
            pitch_s = 0.0f;
        }
    }
    else if (zcam_bbounce != zcam_lbbounce)
    {
        mvtm = 0.3f;
        lktm = 0.3f;
        pitch_s = 0.0f;
    }
    else if (mvtm > 0.1f)
    {
        mvtm -= dt;
        if (mvtm < 0.1f)
        {
            mvtm = 0.1f;
        }
    }

    dgoal = GetCurrentD();
    hgoal = GetCurrentH();
    pgoal = cam->pcur;
    pitch_goal = zcam_near != 0 ? zcam_near_pitch : zcam_far_pitch;

    if (lassocam_enabled && stop_track == 0)
    {
        dgoal = lassocam_factor * (dgoal - zcam_near_d) + zcam_near_d;
        hgoal = lassocam_factor * (hgoal - zcam_near_h) + zcam_near_h;
    }

    if ((globals.pad0->released & XPAD_BUTTON_L1) && globals.pad0->down_tmr[8] < 0.5f)
    {
        zcam_centering = 1;
    }

    if (zcam_centering == 1 && input_enabled)
    {
        F32 pCentered = xatan2(cam->tgt_mat->at.x, cam->tgt_mat->at.z);
        pCentered = xAngleClampFast(PI + pCentered);

        if (zcam_centering != zcam_lastcentering)
        {
            sCenteringFactorDP = xabs(cam->pcur - pCentered);
            sCenteringFactorDP /= PI / 2;
        }

        F32 centeringFactor = 20.0f * sCenteringFactorDP + 12.0f;
        xprintf("cam:%f\n", sCenteringFactorDP);

        if (cam->pcur != pCentered)
        {
            if (xabs(cam->pcur - pCentered) <= PI)
            {
                if (cam->pcur > pCentered)
                {
                    cam->pcur -= centeringFactor * dt;
                    if (cam->pcur < pCentered)
                    {
                        cam->pcur = pCentered;
                        zcam_centering = 0;
                    }
                }
                else
                {
                    cam->pcur += centeringFactor * dt;
                    if (cam->pcur > pCentered)
                    {
                        cam->pcur = pCentered;
                        zcam_centering = 0;
                    }
                }
            }
            else if (cam->pcur > pCentered)
            {
                cam->pcur += centeringFactor * dt;
                if (cam->pcur < pCentered)
                {
                    cam->pcur = pCentered;
                    zcam_centering = 0;
                }
                cam->pcur = xAngleClampFast(cam->pcur);
            }
            else
            {
                cam->pcur -= centeringFactor * dt;
                if (cam->pcur > pCentered)
                {
                    cam->pcur = pCentered;
                    zcam_centering = 0;
                }
                cam->pcur = xAngleClampFast(cam->pcur);
            }
        }

        pgoal = cam->pcur;
        cam->pgoal = pgoal;
    }

    zcam_lastcentering = zcam_centering;

    if (input_enabled && wall_jump_enabled == WJVS_DISABLED)
    {
        if (globals.pad0->analog2.x > 32)
        {
            F32 dp = (F32)(CLAMP(globals.pad0->analog2.x, 32, 110) - 32);
            dp = 0.016666668f * (dp * zcam_pad_pyaw_scale);

            if (lassocam_enabled && stop_track == 0)
            {
                dp *= 0.2f;
            }

            pgoal += dp;
            cam->pcur += dp;
            cam->pgoal += dp;

            if (lktm > 0.025f)
            {
                lktm -= dt;
                if (lktm < 0.025f)
                {
                    lktm = 0.025f;
                }
            }

            zcam_overrot_tmr = -zcam_overrot_tmanual;
        }
        else if (globals.pad0->analog2.x < -32)
        {
            F32 dp = (F32)(CLAMP(globals.pad0->analog2.x, -110, -32) + 32);
            dp = 0.016666668f * (dp * zcam_pad_pyaw_scale);

            if (lassocam_enabled && stop_track == 0)
            {
                dp *= 0.2f;
            }

            pgoal += dp;
            cam->pcur += dp;
            cam->pgoal += dp;

            if (lktm > 0.025f)
            {
                lktm -= dt;
                if (lktm < 0.025f)
                {
                    lktm = 0.025f;
                }
            }

            zcam_overrot_tmr = -zcam_overrot_tmanual;
        }
    }

    pitch_s = 0.0f;

    if (input_enabled && wall_jump_enabled == WJVS_DISABLED && zcam_highbounce == 0)
    {
        if (globals.pad0->analog2.y > 32)
        {
            F32 dp = (F32)(CLAMP(globals.pad0->analog2.y, 32, 110) - 32);
            pitch_s = 0.016666668f * (dp * zcam_pad_pitch_scale);

            if (pitch_s > 1.0f)
            {
                pitch_s = 1.0f;
            }

            zcam_overrot_tmr = -zcam_overrot_tmanual;
        }
        else if (globals.pad0->analog2.y < -32)
        {
            F32 dp = (F32)(CLAMP(globals.pad0->analog2.y, -110, -32) + 32);
            pitch_s = 0.016666668f * (dp * zcam_pad_pitch_scale);

            if (pitch_s < -1.0f)
            {
                pitch_s = -1.0f;
            }

            zcam_overrot_tmr = -zcam_overrot_tmanual;
        }
    }

    if (dt > 0.00001f && cam->tgt_mat != NULL && cam->tgt_omat != NULL && vertical_lerp < 0.9999f)
    {
        F32 velx = (cam->tgt_mat->pos.x - cam->tgt_omat->pos.x - tran_accum.x) / dt;
        F32 velz = (cam->tgt_mat->pos.z - cam->tgt_omat->pos.z - tran_accum.z) / dt;
        F32 camx = cam->tgt_mat->pos.x - cam->mat.pos.x;
        F32 camz = cam->tgt_mat->pos.z - cam->mat.pos.z;
        F32 cammag = xsqrt(SQR(camx) + SQR(camz));
        F32 velmag = xsqrt(SQR(velx) + SQR(velz));

        if (velmag > zcam_overrot_velmin || zcam_overrot_tmr < 0.0f)
        {
            zcam_overrot_tmr += dt;
        }
        else if (zcam_overrot_tmr > 0.0f)
        {
            if (zcam_overrot_tmr > zcam_overrot_tend)
            {
                zcam_overrot_tmr = zcam_overrot_tend;
            }

            zcam_overrot_tmr -= dt;

            if (zcam_overrot_tmr < 0.0f)
            {
                zcam_overrot_tmr = 0.0f;
            }
        }

#ifdef PLATFORM_PC
        // PC: no swing behind the player while the mouse steers, held off the way the stick
        // holds it off.
        if (iCameraOrbitSteering())
        {
            zcam_overrot_tmr = -zcam_overrot_tmanual;
        }
#endif

        if (zcam_overrot_tmr > zcam_overrot_tstart && cammag > 1.2f && velmag > zcam_overrot_velmin)
        {
            camz /= cammag;
            velz /= velmag;
            camx /= cammag;
            velx /= velmag;

            F32 velcos = CLAMP(camx * velx + camz * velz, -1.0f, 1.0f);
            F32 velsin = camz * velx - camx * velz;
            F32 velang = xacos(velcos);

            if (velang > zcam_overrot_min && velang < zcam_overrot_max)
            {
                F32 angle_factor;

                if (velang <= zcam_overrot_mid)
                {
                    angle_factor =
                        (velang - zcam_overrot_min) / (zcam_overrot_mid - zcam_overrot_min);
                }
                else
                {
                    angle_factor =
                        (zcam_overrot_max - velang) / (zcam_overrot_max - zcam_overrot_mid);
                }

                F32 vel_factor =
                    (velmag - zcam_overrot_velmin) / (zcam_overrot_velmax - zcam_overrot_velmin);
                vel_factor = CLAMP(vel_factor, 0.0f, 1.0f);

                F32 time_factor = (zcam_overrot_tmr - zcam_overrot_tstart) /
                                  (zcam_overrot_tend - zcam_overrot_tstart);
                time_factor = CLAMP(time_factor, 0.0f, 1.0f);

                F32 dp = time_factor * (zcam_overrot_rate * (vel_factor * angle_factor));

                if (lassocam_enabled && stop_track == 0)
                {
                    dp *= 0.2f;
                }

                if (velsin > 0.0f)
                {
                    pgoal += dp;
                    cam->pcur += dp;
                    cam->pgoal += dp;
                }
                else
                {
                    pgoal -= dp;
                    cam->pcur -= dp;
                    cam->pgoal -= dp;
                }
            }
        }
    }

    zCameraFreeLookSetGoals(cam, pitch_s, dgoal, hgoal, pitch_goal, lktm, dt);

    F32 dirx;
    F32 dirz;
    {
        F32 dx__ = cam->tgt_mat->pos.x - cam->mat.pos.x;
        F32 dy__ = cam->tgt_mat->pos.y - cam->mat.pos.y;
        F32 dz__ = cam->tgt_mat->pos.z - cam->mat.pos.z;
        F32 dist2 = SQR(dx__) + SQR(dy__) + SQR(dz__);

        if (xeq(dist2, 1.0f, 0.00001f))
        {
            dirx = dx__;
            dirz = dz__;
        }
        else if (xeq(dist2, 0.0f, 0.00001f))
        {
            dirx = 0.0f;
            dirz = 0.0f;
        }
        else
        {
            F32 dist_inv = 1.0f / xsqrt(dist2);
            dirx = dx__ * dist_inv;
            dirz = dz__ * dist_inv;
        }
    }

    yaw_goal = xatan2(dirx, dirz);

#ifdef PLATFORM_PC
    // PC: the mouse owns the camera's two angles (iCameraOrbit.h). Its goals replace the ones
    // made above and are reached at once: with the 0.1 s ease a mouse turns late and coasts.
    F32 mvtm_was = mvtm;
    F32 lktm_was = lktm;
    if (iCameraOrbit(cam, dt,
                     input_enabled && !changing_folcam_orientation &&
                         wall_jump_enabled == WJVS_DISABLED && zcam_centering == 0,
                     dgoal, hgoal, pgoal, yaw_goal, pitch_goal))
    {
        mvtm = 0.0f;
        lktm = 0.0f;
    }
#endif

    if (changing_folcam_orientation)
    {
        change_folcam_timer -= dt;
        if (change_folcam_timer <= 0.0f)
        {
            input_enabled = true;
            changing_folcam_orientation = false;
            change_folcam_timer = 0.0f;
        }
    }
    else if (lassocam_enabled && stop_track == 0)
    {
        xCameraMove(cam, 0x20, dgoal, hgoal, pgoal, mvtm, 0.0f, 0.0f);
    }
    else
    {
        xCameraMove(cam, 0x28, dgoal, hgoal, pgoal, mvtm, 0.0f, 0.0f);
    }

    xCameraLookYPR(cam, 0, yaw_goal, pitch_goal, 0.0f, lktm, 0.0f, 0.0f);

#ifdef PLATFORM_PC
    mvtm = mvtm_was;
    lktm = lktm_was;
#endif

    if (wall_jump_enabled == WJVS_ENABLED)
    {
        xVec3 destPosition;

        xCameraRotate(cam, wall_jump_view, 0.0f, 0.5f, 0.0f, 0.0f);

        cam->dcur = cam->dgoal;

        xVec3SMul(&destPosition, &wall_jump_view, -cam->dcur);
        xVec3Add(&destPosition, &destPosition,
                 (xVec3*)&xglobals->___player_ent_dont_use_directly->model->Mat->pos);

        destPosition.y += hgoal / 2.0f;

        xCameraMove(cam, destPosition, 6.0f * dt);

        cam->flags |= 0x20;
    }
    else if (wall_jump_enabled == WJVS_DISABLING)
    {
        cam->dcur = cam->dgoal;
        cam->pcur = cam->pgoal;
        cam->hcur = cam->hgoal;

        wall_jump_enabled = WJVS_DISABLED;
    }

    xCameraUpdate(cam, dt);

#ifdef PLATFORM_PC
    iCameraOrbitCollided(cam);
#endif

    if (lassocam_enabled && stop_track == 0)
    {
        cam->pcur = pgoal;
    }

    zcam_lbbounce = zcam_bbounce;

    cam->tgt_mat->pos.y = tgtHeight;
    cam->tgt_omat->pos.y = oldTgtHeight;
}

void zCameraSetBbounce(S32 bbouncing)
{
    zcam_bbounce = bbouncing;
}

void zCameraSetLongbounce(S32 lbounce)
{
    if (zcam_highbounce != 0 || zcam_longbounce != lbounce)
    {
        zcam_lbbounce = 0;
    }

    zcam_longbounce = lbounce;
    zcam_highbounce = 0;
}

void zCameraSetHighbounce(S32 hbounce)
{
    if (zcam_longbounce != 0 || zcam_highbounce != hbounce)
    {
        zcam_lbbounce = 0;
    }

    zcam_highbounce = hbounce;
    zcam_longbounce = 0;
}

void zCameraSetPlayerVel(xVec3* vel)
{
    zcam_playervel = vel;
}

void zCameraDisableTracking(camera_owner_enum owner)
{
    stop_track |= owner;
}

void zCameraEnableTracking(camera_owner_enum owner)
{
    stop_track &= ~owner;
}

U32 zCameraIsTrackingDisabled()
{
    return stop_track;
}

void zCameraDisableInput()
{
    input_enabled = 0;
}

void zCameraEnableInput()
{
    input_enabled = 1;
}

void zCameraDisableLassoCam()
{
    lassocam_enabled = 0;
}

void zCameraEnableLassoCam()
{
    lassocam_enabled = 1;
}

void zCameraSetLassoCamFactor(F32 factor)
{
    lassocam_factor = factor;
}

F32 zCameraGetLassoCamFactor()
{
    return lassocam_factor;
}

S32 zCameraGetConvers()
{
    return zcam_convers;
}

// zCameraSetConvers and zCameraDoTrans (BfBB bodies) are stripped at link but were compiled:
// zCameraSetConvers is the first user of xCamera's implicit operator=, which retail emits after
// zCameraTranslate.
void zCameraSetConvers(S32 on)
{
    xCamera& cam = globals.oldSkoolCamera;
    zcam_convers = on;
    static U8 saved = 0;

    if (on)
    {
        cam = zcam_backupconvers;
        saved = 1;
        zcam_dest = NULL;
        zcam_tmr = 0.0f;
    }
    else
    {
        xCameraSetFOV(&cam, 75.0f);
        zcam_fovcurr = 75.0f;

        if (saved)
        {
            zCameraFlyRestoreBackup(&zcam_backupconvers);
            xCameraMove(&cam, 0x2E, cam.dcur, cam.hcur, cam.pcur, 0.0f, 0.0f, 0.0f);
            saved = 0;
        }
    }
}

void zCameraDoTrans(xCamAsset* asset, F32 ttime)
{
    xMat3x3 m;

    zcam_dest = asset;

    ttime = ttime > 0.0f ? ttime : asset->trans_time;

    zcam_tmr = ttime;
    zcam_ttm = ttime;

    if (ttime <= 0.0f)
    {
        globals.oldSkoolCamera.mat.right = asset->right;
        globals.oldSkoolCamera.mat.up = asset->up;
        globals.oldSkoolCamera.mat.at = asset->at;
        globals.oldSkoolCamera.mat.pos = asset->pos;

        zcam_fovcurr = asset->fov;
        zcam_fovdest = asset->fov;
    }
    else
    {
        m.right = asset->right;
        m.up = asset->up;
        m.at = asset->at;

        xQuatFromMat(&zcam_quat, &m);
        zcam_fovdest = asset->fov;
    }
}

void zCameraTranslate(xCamera* cam, F32 dposx, F32 dposy, F32 dposz)
{
    cam->mat.pos.x += dposx;
    cam->mat.pos.y += dposy;
    cam->mat.pos.z += dposz;
    cam->tran_accum.x += dposx;
    cam->tran_accum.y += dposy;
    cam->tran_accum.z += dposz;
}

void zCameraEnableWallJump(xCamera* cam, const xVec3& collNormal)
{
    if (wall_jump_enabled != WJVS_ENABLED)
    {
        wall_jump_enabled = WJVS_ENABLING;
    }

    xVec3 up = { 0.0f, 1.0f, 0.0f };

    // Missing inline?
    xVec3Cross(&wall_jump_view, &collNormal, &up);
    xVec3Normalize(&wall_jump_view, &wall_jump_view);

    if (xVec3Dot(&wall_jump_view, &globals.oldSkoolCamera.mat.at) < 0.0f)
    {
        // Missing inline?
        xVec3Sub(&wall_jump_view, &g_O3, &wall_jump_view);
    }
}

void zCameraDisableWallJump(xCamera* cam)
{
    if (wall_jump_enabled != WJVS_DISABLED)
    {
        wall_jump_enabled = WJVS_DISABLING;
    }
}

void zCameraSetReward(S32 on)
{
    if (stop_track != 0)
    {
        zcam_reward = 0;
        return;
    }
    zcam_reward = on;
}

void zCameraMinTargetHeightSet(F32 height)
{
    zcam_mintgtheight = height;
}

void zCameraMinTargetHeightClear()
{
    zcam_mintgtheight = FLOAT_MIN;
}
