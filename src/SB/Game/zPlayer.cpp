#include "zPlayer.h"
#include "zPlayerBase.h"
#include "xGlobals.h"
#include "xJaw.h"
#include "xMath3.h"
#include "xRand.h"
#include "xSnd.h"
#include "xVec3Inlines.h"
#include "xRay3.h"
#include "zEntTrigger.h"
#include "zEntPlayer.h"
#include "zGlobals.h"

TalkData zPlayer::playerTalkData;

namespace
{
    void bound_update_wrapper(xEnt* ent, xVec3* pos)
    {
        ((zPlayer*)ent)->BoundUpdate(pos);
    }
} // namespace

void zPlayer::Init(xEntAsset* asset)
{
    xModelInstance* m;

    zPlayerFlags &= ~0x400;
    zPlayerFlags &= ~0x800;
    zEntInit(this, asset, 'PLYR');
    chkby |= XENT_COLLTYPE_DYN;
    zPlayerFlags &= ~0x100;
    zPlayerFlags &= ~0x200;

    for (m = model; m != NULL; m = m->Next)
    {
        m->Flags |= 0x3;
    }

    xBaseEnable(this);
    xEntShow(this);

    zPlayerFlags |= 0x80;
    pflags &= (U8)~0x4;
    collis->chk &= (U8)~XENT_COLLTYPE_TRIG;
    collis->chk &= XENT_COLLTYPE_DYN;
    render = (xEntRenderCallback)zPlayerRender;
    update = zPlayerUpdate;
    eventFunc = zPlayerEventCB;
    bupdate = bound_update_wrapper;

    if (linkCount != 0)
    {
        link = (xLinkAsset*)(asset + 1);
    }
    else
    {
        link = NULL;
    }
}

void zPlayer::SpeakStart(iSndGroupHandle hSoundGroup, S32 anim)
{
    SpeakStop();
    playerTalkData.data = xJaw_FindData(xSndMgrGetSoundId(hSoundGroup, 0));
    if (playerTalkData.data != NULL)
    {
        playerTalkData.anim = (anim == -1) ? (xrand_RandomInt32() >> 13) & 3 : anim;
        playerTalkData.sndid = hSoundGroup;
        playerTalkData.time = 0.0f;
    }
}

void zPlayer::SetEnemyIsNear(bool isit)
{
    if (isit)
    {
        zPlayerFlags |= 0x1000;
    }
    else
    {
        zPlayerFlags &= ~0x1000;
    }
}

void zPlayer::SpeakStop()
{
    playerTalkData.anim = -1;
    model->Anim->Single->BilinearLerp[0] = 0.0f;
}

U32 zPlayer::TalkCheck(xAnimTransition* tran, xAnimSingle*, void*)
{
    return tran->UserFlags == playerTalkData.anim;
}

U32 zPlayer::TalkDoneCheck(xAnimTransition* tran, xAnimSingle*, void*)
{
    return tran->UserFlags != playerTalkData.anim;
}

void zPlayer::zPlayerEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                             xBase* toParamWidget, U32 toParamWidgetID)
{
    ((zPlayer*)to)->HandleEvent(from, toEvent, toParam, toParamWidget, toParamWidgetID);
}

U8 zPlayer::Damage(xBase* src, S32 damage, zHitSource source, zHitTarget target,
                   const xVec3* hitPos)
{
    if (IsDead())
    {
        if (driver != NULL)
        {
            if (driver != NULL)
            {
                driver->driving_count--;
            }
            driver = NULL;
            xVec3Copy(&frame->vel, &g_O3);
            xVec3Copy(&frame->oldvel, &g_O3);
        }
    }

    return 1;
}

static inline bool xRayHitsPlaneX(const xVec3& start, const xVec3& end, F32 plane, F32& y, F32& z)
{
    F32 x_total = end.x - start.x;
    if (x_total < 0.00001f && x_total > -0.00001f)
    {
        return false;
    }

    F32 percent = (plane - start.x) / x_total;
    y = percent * (end.y - start.y) + start.y;
    z = percent * (end.z - start.z) + start.z;
    return true;
}

static inline bool xRayHitsPlaneY(const xVec3& start, const xVec3& end, F32 plane, F32& x, F32& z)
{
    F32 y_total = end.y - start.y;
    if (y_total < 0.00001f && y_total > -0.00001f)
    {
        return false;
    }

    F32 percent = (plane - start.y) / y_total;
    x = percent * (end.x - start.x) + start.x;
    z = percent * (end.z - start.z) + start.z;
    return true;
}

static inline bool xRayHitsPlaneZ(const xVec3& start, const xVec3& end, F32 plane, F32& x, F32& y)
{
    F32 z_total = end.z - start.z;
    if (z_total < 0.00001f && z_total > -0.00001f)
    {
        return false;
    }

    F32 percent = (plane - start.z) / z_total;
    x = percent * (end.x - start.x) + start.x;
    y = percent * (end.y - start.y) + start.y;
    return true;
}

static bool xRayHitsTrigger(const xVec3& start, const xVec3& end, const xBox& b, bool& end_inside)
{
    F32 hit_x;
    F32 hit_y;
    F32 hit_z;

    end_inside = end.x <= b.upper.x && end.x >= b.lower.x && end.y <= b.upper.y &&
                 end.y >= b.lower.y && end.z <= b.upper.z && end.z >= b.lower.z;
    if (end_inside == true)
    {
        return true;
    }

    if (start.x <= b.upper.x && start.x >= b.lower.x && start.y <= b.upper.y &&
        start.y >= b.lower.y && start.z <= b.upper.z && start.z >= b.lower.z)
    {
        return true;
    }

    if ((start.x < b.lower.x && end.x < b.lower.x) || (start.x > b.upper.x && end.x > b.upper.x) ||
        (start.y < b.lower.y && end.y < b.lower.y) || (start.y > b.upper.y && end.y > b.upper.y) ||
        (start.z < b.lower.z && end.z < b.lower.z) || (start.z > b.upper.z && end.z > b.upper.z))
    {
        return false;
    }

    if (start.x < b.lower.x && xRayHitsPlaneX(start, end, b.lower.x, hit_y, hit_z) &&
        hit_y >= b.lower.y && hit_y <= b.upper.y && hit_z >= b.lower.z && hit_z <= b.upper.z)
    {
        return true;
    }

    if (start.x > b.upper.x && xRayHitsPlaneX(start, end, b.upper.x, hit_y, hit_z) &&
        hit_y >= b.lower.y && hit_y <= b.upper.y && hit_z >= b.lower.z && hit_z <= b.upper.z)
    {
        return true;
    }

    if (start.y < b.lower.y && xRayHitsPlaneY(start, end, b.lower.y, hit_x, hit_z) &&
        hit_x >= b.lower.x && hit_x <= b.upper.x && hit_z >= b.lower.z && hit_z <= b.upper.z)
    {
        return true;
    }

    if (start.y > b.upper.y && xRayHitsPlaneY(start, end, b.upper.y, hit_x, hit_z) &&
        hit_x >= b.lower.x && hit_x <= b.upper.x && hit_z >= b.lower.z && hit_z <= b.upper.z)
    {
        return true;
    }

    if (start.z < b.lower.z && xRayHitsPlaneZ(start, end, b.lower.z, hit_x, hit_y) &&
        hit_x >= b.lower.x && hit_x <= b.upper.x && hit_y >= b.lower.y && hit_y <= b.upper.y)
    {
        return true;
    }

    if (start.z > b.upper.z && xRayHitsPlaneZ(start, end, b.upper.z, hit_x, hit_y) &&
        hit_x >= b.lower.x && hit_x <= b.upper.x && hit_y >= b.lower.y && hit_y <= b.upper.y)
    {
        return true;
    }

    return false;
}

// No GC or PS2 symbol (inlined into CollideTrigger); Ratatouille's DWARF has it as this
// xRayHitsTrigger overload.
static inline bool xRayHitsTrigger(const xVec3& start, const xVec3& end, const xVec3& dir,
                                   const xSphere& s, bool& end_inside)
{
    F32 radius_squ = SQR(s.r);
    xVec3 end_to_sphere;
    xVec3Sub(&end_to_sphere, &s.center, &end);

    end_inside = end_to_sphere.length2() <= radius_squ;
    if (end_inside == true)
    {
        return true;
    }

    xVec3 start_to_sphere;
    xVec3Sub(&start_to_sphere, &s.center, &start);
    if (start_to_sphere.length2() <= radius_squ)
    {
        return true;
    }

    if (start_to_sphere.dot(dir) < 0.0f || end_to_sphere.dot(dir) > 0.0f)
    {
        return false;
    }

    xVec3 out_vec;
    xVec3AddScaled(out_vec, start_to_sphere, dir, -start_to_sphere.dot(dir));
    return out_vec.length2() <= radius_squ;
}

void zPlayer::CollideTrigger(xScene* sc)
{
    xVec3 end_pos;
    xVec3Add(&end_pos, &frame->mat.pos, &frame->dpos);

    if (trigLastFramePos.x == 1e38f)
    {
        trigLastFramePos = end_pos;
    }

    xVec3 start_pos = trigLastFramePos;
    xVec3 frame_disp;
    xVec3Sub(&frame_disp, &end_pos, &start_pos);

    if (xVec3Length2(&frame_disp) > 400.0f)
    {
        start_pos = frame->mat.pos;
        xVec3Sub(&frame_disp, &end_pos, &start_pos);
    }

    xVec3 frame_dir;
    F32 frame_dist = xVec3NormalizeSafe(frame_dir, frame_disp);

    if (frame_dist == 0.0f)
    {
        frame_dir = frame->mat.at;
    }

    trigLastFramePos = end_pos;

    for (S32 i = 0; i < sc->num_trigs; i++)
    {
        zEntTrigger* trig = (zEntTrigger*)sc->trigs[i];

        if (!xBaseIsEnabled(trig))
        {
            continue;
        }

        xTriggerAsset* tasset = zEntTriggerAsset(*trig);

        xRay3 ray;
        ray.origin = start_pos;
        ray.dir = frame_dir;
        ray.min_t = 0.0f;
        ray.max_t = frame_dist;
        ray.flags = XRAY3_USE_MIN | XRAY3_USE_MAX;

        bool start_inside = trig->entered & 0x1;
        bool end_inside = false;
        bool collide = false;
        xVec3 start;
        xVec3 end;
        xSphere sphere;

        switch (trig->subType)
        {
        case ZENTTRIGGER_TYPE_BOX:
            xVec3Sub(&end, &end_pos, &trig->frame->mat.pos);
            xMat3x3Tolocal(&end, &trig->frame->mat, &end);

            xVec3Sub(&start, &start_pos, &trig->frame->mat.pos);
            xMat3x3Tolocal(&start, &trig->frame->mat, &start);

            collide = xRayHitsTrigger(start, end, trig->triggerBox, end_inside);
            break;
        case ZENTTRIGGER_TYPE_SPHERE:
            sphere.center = tasset->p[0];
            sphere.r = tasset->p[1].x;
            collide = xRayHitsTrigger(start_pos, end_pos, frame_dir, sphere, end_inside);
            break;
        }

        if ((collide && !start_inside && !end_inside) || (!start_inside && end_inside))
        {
            if (tasset->flags & 0x1)
            {
                if (xVec3Dot(&tasset->direction, &ray.dir) <= 0.0f)
                {
                    zEntEvent(trig, eEventEnterPlayer);
                    switch (gCurrentPlayer)
                    {
                    case eCurrentPlayerSpongeBob:
                        zEntEvent(trig, eEventEnterSpongeBob);
                        break;
                    case eCurrentPlayerPatrick:
                        zEntEvent(trig, eEventEnterPatrick);
                        break;
                    }
                }
            }
            else
            {
                zEntEvent(trig, eEventEnterPlayer);
                switch (gCurrentPlayer)
                {
                case eCurrentPlayerSpongeBob:
                    zEntEvent(trig, eEventEnterSpongeBob);
                    break;
                case eCurrentPlayerPatrick:
                    zEntEvent(trig, eEventEnterPatrick);
                    break;
                }
            }
            trig->entered |= 0x1;
        }

        if ((collide && !start_inside && !end_inside) || (!end_inside && start_inside))
        {
            if (tasset->flags & 0x1)
            {
                if (xVec3Dot(&tasset->direction, &xModelGetFrame(model)->at) <= 0.0f)
                {
                    zEntEvent(trig, eEventExitPlayer);
                    switch (gCurrentPlayer)
                    {
                    case eCurrentPlayerSpongeBob:
                        zEntEvent(trig, eEventExitSpongeBob);
                        break;
                    case eCurrentPlayerPatrick:
                        zEntEvent(trig, eEventExitPatrick);
                        break;
                    }
                }
            }
            else
            {
                zEntEvent(trig, eEventExitPlayer);
                switch (gCurrentPlayer)
                {
                case eCurrentPlayerSpongeBob:
                    zEntEvent(trig, eEventExitSpongeBob);
                    break;
                case eCurrentPlayerPatrick:
                    zEntEvent(trig, eEventExitPatrick);
                    break;
                }
            }
            trig->entered &= ~0x1;
        }

        if ((start_inside || end_inside || collide) && !globals.player.ControlOff &&
            (globals.pad0->pressed & XPAD_MOVE(0x20000)))
        {
            zEntEvent(trig, eEventButtonPressAction);
        }
    }
}
