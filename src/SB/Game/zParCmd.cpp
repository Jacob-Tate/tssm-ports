#include "zParCmd.h"

#include <types.h>
#include <string.h>
#include <stdio.h>

#include "xParCmd.h"
#include "xMath.h"
#include "xMathInlines.h"
#include "xRand.h"
#include "xString.h"
#include "xVec3.h"
#include "xVec3Inlines.h"

#include "zGlobals.h"
#include "xCam.h"
#include "zScene.h"
#include "zVolume.h"

static zVolume* sClipVolume[32];
static S32 sClipVolumeTotal;

void zParCmdInit()
{
    xParCmdInit();
    xParCmdRegister(XPARCMD_TYPE_JET, sizeof(xParCmdJet), xParCmdJet_Update);
    xParCmdRegister(XPARCMD_TYPE_PLAYERCOLLISION, sizeof(xParCmdPlayerCollision),
                    xParCmdPlayerCollision_Update);
    xParCmdRegister(XPARCMD_TYPE_CUSTOM, sizeof(xParCmdCustom), xParCmdCustom_Update);
    xParCmdRegister(XPARCMD_TYPE_CLIPVOLUMES, sizeof(xParCmdClipVolumes),
                    xParCmdClipVolumes_Update);
    xParCmdRegister(XPARCMD_TYPE_ANIMALMAGENTISM, sizeof(xParCmdAnimalMagentism),
                    xParCmdAnimalMagentism_Update);
    xParCmdRegister(XPARCMD_TYPE_DAMAGEPLAYER, sizeof(xParCmdDamagePlayer),
                    xParCmdDamagePlayer_Update);
    xParCmdRegister(XPARCMD_TYPE_APPLYCAMMAT, sizeof(xParCmdApplyCamMat),
                    xParCmdApplyCamMat_Update);
    xParCmdRegister(XPARCMD_TYPE_KILLDISTANCE, sizeof(xParCmdKillDistance),
                    xParCmdKillDistance_Update);
}

inline void zParCmdFindClipVolumes()
{
    char findname[64];

    memset(sClipVolume, 0, sizeof(zVolume*) * 32);
    for (sClipVolumeTotal = 0; sClipVolumeTotal < 32; sClipVolumeTotal++)
    {
        sprintf(findname, "PARTICLE_CLIP_%d", sClipVolumeTotal + 1);
        zVolume* vol = (zVolume*)zSceneFindObject(xStrHash(findname));
        if (vol == NULL)
        {
            return;
        }
        sClipVolume[sClipVolumeTotal] = vol;
    }
}

void xParCmdKillDistance_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdKillDistance* cmd = (xParCmdKillDistance*)c->tasset;
    xVec3* camera_pos = &globals.cam->mat.pos;

    if (cmd->kill_greater_than)
    {
        for (; p != NULL; p = p->m_next)
        {
            if (xVec3Dist2(&p->m_pos, camera_pos) > cmd->dSqr)
            {
                p->m_lifetime = -1.0f;
            }
        }
    }
    else
    {
        for (; p != NULL; p = p->m_next)
        {
            if (xVec3Dist2(&p->m_pos, camera_pos) < cmd->dSqr)
            {
                p->m_lifetime = -1.0f;
            }
        }
    }
}

void xParCmdClipVolumes_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    for (xPar* p = ps->m_root; p != NULL; p = p->m_next)
    {
        for (S32 i = 0; i < sClipVolumeTotal; i++)
        {
            xCollis collis;
            xVecHitsBound(&p->m_pos, &sClipVolume[i]->asset->bound, &collis);
            if (collis.flags & 1)
            {
                p->m_lifetime = -1.0f;
            }
        }
    }
}

void xParCmdPlayerCollision_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
}

void xParCmdAnimalMagentism_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;

    xVec3 pos = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
    pos.y += 1.0f;

    F32 mul = dt * -((xParCmdAnimalMagentism*)c->tasset)->magnetism;
    for (p = ps->m_root; p != NULL; p = p->m_next)
    {
        xVec3 vec;
        vec.x = p->m_pos.x - pos.x;
        vec.y = p->m_pos.y - pos.y;
        vec.z = p->m_pos.z - pos.z;
        F32 dist = xVec3Normalize(&vec, &vec);

        p->m_vel.x += vec.x * mul;
        p->m_vel.y += vec.y * mul;
        p->m_vel.z += vec.z * mul;
        if (dist < 0.25f)
        {
            p->m_lifetime = -1.0f;
        }
        else if (dist < 2.0f)
        {
            dist *= 0.49f;
            p->m_vel.x *= dist;
            p->m_vel.y *= dist;
            p->m_vel.z *= dist;
        }
    }
}

void xParCmdDamagePlayer_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdDamagePlayer* cmd = (xParCmdDamagePlayer*)c->tasset;
    xBound* pbound = &xglobals->___player_ent_dont_use_directly->bound;
    S32 last_idx = 10 - (xrand_RandomInt32() & 1);

    p = ps->m_root;
    while (p != NULL && last_idx-- > 0)
    {
        p = p->m_next;
    }

    static xCollis collis;
    // The initializer lands in qcd.xmin, not in type (the memset below clears it anyway).
    static xBound bnd_fake = { XBOUND_TYPE_SPHERE };

    memset(&collis, 0, sizeof(xCollis));
    memset(&bnd_fake, 0, sizeof(xBound));
    bnd_fake.type = XBOUND_TYPE_SPHERE;

    while (p != NULL)
    {
        if (p->m_lifetime > 0.1f)
        {
            collis.flags = 0;
            if (p->m_size > 1.0f)
            {
                bnd_fake.sph.r = 0.5f * p->m_size;
                xVec3Copy(&bnd_fake.sph.center, &p->m_pos);
                xQuickCullForBound(&bnd_fake.qcd, &bnd_fake);
                xBoundHitsBound(&bnd_fake, pbound, &collis);
            }
            else
            {
                xVecHitsBound(&p->m_pos, pbound, &collis);
            }

            if (collis.flags & 1)
            {
                // zHitTarget value names are unknown (no DWARF definition)
                xglobals->___player_ent_dont_use_directly->Damage(NULL, 1, zHS_PROJECTILE,
                                                                  (zHitTarget)0, NULL);
            }
        }

        for (S32 i = 0; i < cmd->granular; i++)
        {
            if (p == NULL)
            {
                break;
            }
            p = p->m_next;
        }
    }
}

void xParCmdJet_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdJet* cmd = (xParCmdJet*)c->tasset;
    F32 mdt = cmd->gravity * dt;

    xVec3 center;
    if (cmd->mode == 0)
    {
        center.x = xglobals->___player_ent_dont_use_directly->model->Mat->pos.x;
        center.y = xglobals->___player_ent_dont_use_directly->model->Mat->pos.y;
        center.z = xglobals->___player_ent_dont_use_directly->model->Mat->pos.z;
    }
    else
    {
        center = cmd->center;
    }

    for (p = ps->m_root; p != NULL; p = p->m_next)
    {
        xVec3 r;
        xVec3Sub(&r, &center, &p->m_pos);

        F32 rSqr = r.x * r.x + r.y * r.y + r.z * r.z;
        if (!(rSqr < cmd->radiusSqr))
        {
            continue;
        }

        F32 oorSqr = mdt / xsqrt(rSqr + cmd->epsilon);
        r.x = r.x < 0.0f ? 1.0f : -1.0f;
        r.z = r.z < 0.0f ? 1.0f : -1.0f;

        p->m_vel.x = cmd->acc.x * oorSqr * r.x + p->m_vel.x;
        p->m_vel.y = cmd->acc.y * oorSqr + p->m_vel.y;
        p->m_vel.z = cmd->acc.z * oorSqr * r.z + p->m_vel.z;
    }
}

inline void xParCmdCustom_Grass_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;

    xVec3 pos;
    pos.x = xglobals->___player_ent_dont_use_directly->model->Mat->pos.x;
    pos.y = xglobals->___player_ent_dont_use_directly->model->Mat->pos.y;
    pos.z = xglobals->___player_ent_dont_use_directly->model->Mat->pos.z;

    for (p = ps->m_root; p != NULL; p = p->m_next)
    {
        xVec3 r;
        xVec3Sub(&r, &pos, &p->m_pos);

        F32 rSqr = r.x * r.x + r.y * r.y + r.z * r.z;
        if (rSqr < 2.0f)
        {
            p->m_size = p->m_size - 4.0f * dt;
            if (p->m_size < 0.1f)
            {
                p->m_size = 0.1f;
            }
        }
        else
        {
            p->m_size = p->m_size + 0.01f * dt;
            if (p->m_size > 1.0f)
            {
                p->m_size = 1.0f;
            }
        }
    }
}

void xParCmdApplyCamMat_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p;
    xParCmdApplyCamMat* cmd = (xParCmdApplyCamMat*)c->tasset;
    xMat4x3* mat = &globals.cam->mat;
    F32 mul;

    if (cmd->apply.x != 0.0f)
    {
        mul = dt * cmd->apply.x;
        for (p = ps->m_root; p != NULL; p = p->m_next)
        {
            p->m_vel.x += mat->right.x * mul;
            p->m_vel.y += mat->right.y * mul;
            p->m_vel.z += mat->right.z * mul;
        }
    }

    if (cmd->apply.y != 0.0f)
    {
        mul = dt * cmd->apply.y;
        for (p = ps->m_root; p != NULL; p = p->m_next)
        {
            p->m_vel.x += mat->up.x * mul;
            p->m_vel.y += mat->up.y * mul;
            p->m_vel.z += mat->up.z * mul;
        }
    }

    if (cmd->apply.z != 0.0f)
    {
        mul = dt * cmd->apply.z;
        for (p = ps->m_root; p != NULL; p = p->m_next)
        {
            p->m_vel.x += mat->at.x * mul;
            p->m_vel.y += mat->at.y * mul;
            p->m_vel.z += mat->at.z * mul;
        }
    }
}

void xParCmdCustom_Update(xParCmd* c, xParGroup* ps, F32 dt)
{
    xPar* p = ps->m_root;
    xParCmdCustom* cmd = (xParCmdCustom*)c->tasset;

    switch (cmd->user_id)
    {
    case 0:
    {
        F32 mdt = 4.0f * dt;
        // radSqr and epsilon are locals in Ratatouille's debug build; as a variable, epsilon keeps
        // 'rSqr + epsilon' in that operand order.
        F32 radSqr = 16.0f;
        F32 epsilon = 0.001f;

        xVec3 center;
        center.x = xglobals->___player_ent_dont_use_directly->model->Mat->pos.x;
        center.y = xglobals->___player_ent_dont_use_directly->model->Mat->pos.y;
        center.z = xglobals->___player_ent_dont_use_directly->model->Mat->pos.z;

        for (; p != NULL; p = p->m_next)
        {
            xVec3 dir;
            xVec3Sub(&dir, &center, &p->m_pos);

            F32 rSqr = xVec3Length2(&dir);
            if (!(rSqr < radSqr))
            {
                continue;
            }

            F32 oorSqr = mdt / xsqrt(rSqr + epsilon);
            dir.x = dir.x < 0.0f ? 1.0f : -1.0f;
            dir.z = dir.z < 0.0f ? 1.0f : -1.0f;

            p->m_size = 1.0f / (10.0f * oorSqr * (dir.x + dir.z));
        }
        break;
    }
    case 2:
        for (; p != NULL; p = p->m_next)
        {
            p->m_vel.x = 0.0f;
            p->m_vel.y = 0.0f;
            p->m_vel.z = 0.0f;
        }
        break;
    case 10:
        xParCmdCustom_Grass_Update(c, ps, dt);
        break;
    case 11:
        for (; p != NULL; p = p->m_next)
        {
            xVec3SMulBy(&p->m_vel, 0.98f);
        }
        break;
    }
}
