#include "zCineFX.h"
#include "zCineFXSBCommon.h"
#include "zCineFXShared.h"

#include "xMath.h"
#include "xMath3.h"
#include "xString.h"
#include "xVec3Inlines.h"
#include "xstransvc.h"

#include "zCutsceneMgr.h"
#include "zFX.h"
#include "zFXRibbonPool.h"
#include "zGlobals.h"
#include "zNMEParFX.h"
#include "zNMEShrapnel.h"
#include "zNMESupport.h"
#include "zShrapnel.h"

void zCinFX_BubSlamBone_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                           U32 animIndex, U32)
{
    xMat4x3* mat_root;
    S32 idx_bone;
    xVec3 pos_emit;

    if (animIndex != fxrec->index.idx_anim)
    {
        return;
    }

    if (!(fxrec->flg_stat & NCIN_STAT_8))
    {
        return;
    }

    idx_bone = fxrec->index.idx_bone;
    if (idx_bone < 0)
    {
        pos_emit = fxrec->basic.pos_A[0];
    }
    else
    {
        mat_root = (xMat4x3*)&animMat[0];
        pos_emit = ((xMat4x3*)&animMat[idx_bone])->pos;
        if (idx_bone != 0)
        {
            xVec3AddTo(&pos_emit, &mat_root->pos);
        }
    }

    zFX_SpawnBubbleSlam(&pos_emit, fxrec->basic.pos_B[0].x, PI, fxrec->basic.pos_B[0].y,
                        fxrec->basic.pos_B[0].z);
}

void zCinFX_BubTrailBoneSimp_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*,
                                RwMatrixTag* animMat, U32 animIndex, U32)
{
    xMat4x3* mat_root;
    S32 idx_bone;
    xVec3 pos_emit;
    xMat4x3* mat_bone;
    xVec3 vec_offset;
    U32 num_emit;
    xVec3 pos_rand;

    if (animIndex != fxrec->index.idx_anim)
    {
        return;
    }

    mat_root = (xMat4x3*)&animMat[0];
    idx_bone = fxrec->index.idx_bone;
    pos_emit = mat_root->pos;

    if (idx_bone >= 0)
    {
        mat_bone = (xMat4x3*)&animMat[idx_bone];
        vec_offset = fxrec->basic.pos_A[1];

        xMat3x3RMulVec(&pos_emit, mat_bone, &vec_offset);
        xVec3AddTo(&pos_emit, &mat_bone->pos);
        if (idx_bone != 0)
        {
            xVec3AddTo(&pos_emit, &mat_root->pos);
        }
    }

    num_emit = MAX(1, (S32)fxrec->basic.pos_B[0].x);

    pos_rand = fxrec->basic.pos_B[1];
    if (xVec3Length2(&pos_rand) < 0.1f)
    {
        zFX_SpawnBubbleTrail(&pos_emit, num_emit, NULL, NULL);
    }
    else
    {
        zFX_SpawnBubbleTrail(&pos_emit, num_emit, &pos_rand, NULL);
    }
}

void zCinFX_BubbleTrail_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    NCINCustom& data = fxrec->fxdata.customdata;

    if (killit)
    {
        fxrec->flg_stat |= NCIN_STAT_DEAD;
        return;
    }

    if (fxrec->flg_stat & NCIN_STAT_FIRST)
    {
        data.v[0].x = FLOAT_MAX;
        data.f[0] = 0.0f;
    }
}

void zCinFX_BubbleTrail_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                           U32 animIndex, U32)
{
    if (animIndex != fxrec->index.idx_anim)
    {
        return;
    }

    F32 dt = globals.update_dt;
    F32 rate = fxrec->basic.pos_A[1].z;
    xVec3& offset = fxrec->basic.pos_A[0];
    xVec3& root_vel = fxrec->basic.pos_B[0];
    F32 rand_diameter = fxrec->basic.pos_B[1].x;
    F32 rand_vel = fxrec->basic.pos_B[1].y;
    F32 scale = fxrec->basic.pos_B[1].z;
    NCINCustom& data = fxrec->fxdata.customdata;
    xVec3& oldloc = data.v[0];
    xVec3& oldvel = data.v[1];
    F32& emitted = data.f[0];

    emitted += rate * dt;
    S32 total = (S32)emitted;
    if (total <= 0)
    {
        return;
    }

    emitted -= total;

    xMat4x3 mat;
    mat = *(xMat4x3*)&animMat[fxrec->index.idx_bone];
    if (fxrec->index.idx_bone != 0)
    {
        mat.pos += ((xMat4x3*)&animMat[0])->pos;
    }

    xVec3 vel;
    xMat3x3RMulVec(&vel, &mat, &root_vel);

    xVec3 loc;
    xMat4x3Toworld(&loc, &mat, &offset);

    if (oldloc.x >= 1e38f)
    {
        oldloc = loc;
        oldvel = vel;
    }

    xVec3 rloc = { rand_diameter, rand_diameter, rand_diameter };
    xVec3 rvel = { rand_vel, rand_vel, rand_vel };
    zFX_SpawnBubbleTrail(&oldloc, &loc, &oldvel, &vel, total, &rloc, &rvel, scale);

    oldloc = loc;
    oldvel = vel;
}

void zCinFX_BubBurst_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                        U32 animIndex, U32)
{
    if (animIndex != fxrec->index.idx_anim)
    {
        return;
    }

    F32 dt = globals.update_dt;
    xVec3& offset = fxrec->basic.pos_A[0];
    F32 rate = fxrec->basic.pos_A[1].z;
    xVec3& root_vel = fxrec->basic.pos_B[0];
    F32 rand_diameter = fxrec->basic.pos_B[1].x;
    F32 rand_vel = fxrec->basic.pos_B[1].y;
    F32 scale = fxrec->basic.pos_B[1].z;
    NCINCustom& data = fxrec->fxdata.customdata;
    xVec3& oldloc = data.v[0];
    xVec3& oldvel = data.v[1];
    F32& emitted = data.f[0];

    emitted += rate * dt;
    S32 total = (S32)emitted;
    if (total <= 0)
    {
        return;
    }

    emitted -= total;

    xMat4x3 mat;
    mat = *(xMat4x3*)&animMat[fxrec->index.idx_bone];
    if (fxrec->index.idx_bone != 0)
    {
        mat.pos += ((xMat4x3*)&animMat[0])->pos;
    }

    xVec3 vel;
    xVec3 loc;
    xMat3x3RMulVec(&vel, &mat, &root_vel);
    xMat4x3Toworld(&loc, &mat, &offset);

    if (oldloc.x >= 1e38f)
    {
        oldloc = loc;
        oldvel = vel;
    }

    xVec3 rloc = { rand_diameter, rand_diameter, rand_diameter };
    xVec3 rvel = { rand_vel, rand_vel, rand_vel };
    zFX_SpawnBubbleHit(&loc, total, &rloc, &rvel, scale);

    oldloc = loc;
    oldvel = vel;
}

void NCIN_NMERibHelp_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit)
{
    NMEHelpRib::RibData* ribhelp = (NMEHelpRib::RibData*)fxrec->input.fxparm;

    if (ribhelp == NULL)
    {
        fxrec->flg_stat |= NCIN_STAT_DEAD;
        return;
    }

    if (killit)
    {
        NCINRibbon* ribrun = &fxrec->fxdata.ribbondata;

        fxrec->flg_stat |= NCIN_STAT_DEAD;
        if (ribrun->ribbon != NULL)
        {
            zFXRibbonPoolDelete(ribrun->ribbon);
        }
        ribrun->ribbon = NULL;
        return;
    }

    if (fxrec->flg_stat & NCIN_STAT_FIRST)
    {
        NCINRibbon* ribrun = &fxrec->fxdata.ribbondata;

        ribhelp->FindTexture();
        ribrun->ribbon = zFXRibbonPoolNew();
        if (ribrun->ribbon != NULL)
        {
            ribhelp->ConfigRibbon(ribrun->ribbon);
        }
    }

    if (fxrec->fxdata.ribbondata.ribbon == NULL)
    {
        fxrec->flg_stat |= NCIN_STAT_DEAD;
    }
}

// 'len' is not in the PS2 DWARF, but retail computes the length once and divides by it.
void NCIN_NMERibHelp_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                        U32 animIndex, U32)
{
    xMat4x3 mat_bone;
    xVec3 vec_offset;
    xVec3 vec_orient;
    xVec3 pos_emit;
    xVec3 dir_emit;
    NMEHelpRib::RibData* ribhelp;

    if (animIndex != fxrec->index.idx_anim)
    {
        return;
    }

    mat_bone = *(xMat4x3*)&animMat[fxrec->index.idx_bone];
    if (fxrec->index.idx_bone != 0)
    {
        mat_bone.pos += ((xMat4x3*)&animMat[0])->pos;
    }

    vec_offset = fxrec->basic.pos_A[0];
    vec_orient = fxrec->basic.pos_A[1];

    pos_emit = mat_bone.pos;
    pos_emit += mat_bone.right * vec_offset.x;
    pos_emit += mat_bone.up * vec_offset.y;
    pos_emit += mat_bone.at * vec_offset.z;

    F32 len = vec_orient.length();
    if (len < 0.1f)
    {
        dir_emit = mat_bone.at;
    }
    else
    {
        vec_orient /= len;
        dir_emit = mat_bone.right * vec_orient.x;
        dir_emit += mat_bone.up * vec_orient.y;
        dir_emit += mat_bone.at * vec_orient.z;
        dir_emit.normalize();
    }

    ribhelp = (NMEHelpRib::RibData*)fxrec->input.fxparm;
    fxrec->fxdata.ribbondata.ribbon->insert(pos_emit, dir_emit, ribhelp->updinfo.scl_master,
                                            ribhelp->updinfo.alf_master, 0);
}

void NCIN_NMEEjectShrapnel_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic* model,
                              RwMatrixTag* animMat, U32 animIndex, U32)
{
    zShrapnelAsset* shrap;
    xMat4x3 mat_bone;
    xVec3 vel_rel;
    xVec3 vel_emit;

    if (animIndex != fxrec->index.idx_anim)
    {
        return;
    }

    if (!(fxrec->flg_stat & NCIN_STAT_8))
    {
        return;
    }

    shrap = zNMEShrapnelRetrieveShrapnel((en_npctyp)fxrec->input.int_val);
    if (shrap == NULL)
    {
        return;
    }

    mat_bone = *(xMat4x3*)&animMat[fxrec->index.idx_bone];
    if (fxrec->index.idx_bone != 0)
    {
        mat_bone.pos += ((xMat4x3*)&animMat[0])->pos;
    }

    vel_rel = fxrec->basic.pos_B[0];
    vel_emit = fxrec->basic.pos_B[1];
    vel_emit += mat_bone.right * vel_rel.x;
    vel_emit += mat_bone.up * vel_rel.y;
    vel_emit += mat_bone.at * vel_rel.z;

    zShrapnel_CinematicInit(shrap, model, animMat, &vel_emit, NULL);
}

void NCIN_EjectShrapnel_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic* model,
                           RwMatrixTag* animMat, U32 animIndex, U32)
{
    U32 aid_shrap;
    zShrapnelAsset* shrap;
    xMat4x3 mat_bone;
    xVec3 vel_rel;
    xVec3 vel_emit;

    if (animIndex != fxrec->index.idx_anim)
    {
        return;
    }

    if (!(fxrec->flg_stat & NCIN_STAT_8))
    {
        return;
    }

    aid_shrap = xStrHash(fxrec->input.str_fx);
    shrap = (zShrapnelAsset*)xSTFindAsset(aid_shrap, NULL);
    if (shrap == NULL)
    {
        return;
    }

    mat_bone = *(xMat4x3*)&animMat[fxrec->index.idx_bone];
    if (fxrec->index.idx_bone != 0)
    {
        mat_bone.pos += ((xMat4x3*)&animMat[0])->pos;
    }

    vel_rel = fxrec->basic.pos_B[0];
    vel_emit = fxrec->basic.pos_B[1];
    vel_emit += mat_bone.right * vel_rel.x;
    vel_emit += mat_bone.up * vel_rel.y;
    vel_emit += mat_bone.at * vel_rel.z;

    zShrapnel_CinematicInit(shrap, model, animMat, &vel_emit, NULL);
}

void NCIN_NMEPuffPropWash_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                             U32 animIndex, U32)
{
    S32 idx_bone;
    xMat4x3* mat_root;
    xMat4x3* mat_bone;
    xVec3 vec_offset;
    xVec3 pos_emit;
    xVec3 vel_base;
    xVec3 vel_drift;

    if (animIndex != fxrec->index.idx_anim)
    {
        return;
    }

    idx_bone = fxrec->index.idx_bone;
    mat_root = (xMat4x3*)&animMat[0];
    mat_bone = (xMat4x3*)&animMat[idx_bone];

    vec_offset = fxrec->basic.pos_A[0];
    pos_emit = mat_bone->pos;
    if (idx_bone != 0)
    {
        pos_emit += mat_root->pos;
    }

    pos_emit += mat_bone->right * vec_offset.x;
    pos_emit += mat_bone->up * vec_offset.y;
    pos_emit += mat_bone->at * vec_offset.z;

    vel_base = fxrec->basic.pos_A[1];
    vel_drift = mat_bone->right * vel_base.x;
    vel_drift += mat_bone->up * vel_base.y;
    vel_drift += mat_bone->at * vel_base.z;

    ((PuffEmitter*)fxrec->input.fxparm)->PropWash(&pos_emit, &vel_drift);
}

void NCIN_NMEBreathSpew_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                           U32 animIndex, U32)
{
    S32 idx_bone;
    xMat4x3* mat_root;
    xMat4x3* mat_bone;
    xVec3 pos_emit;

    if (animIndex != fxrec->index.idx_anim)
    {
        return;
    }

    idx_bone = fxrec->index.idx_bone;
    mat_root = (xMat4x3*)&animMat[0];
    mat_bone = (xMat4x3*)&animMat[idx_bone];

    pos_emit = mat_bone->pos;
    if (idx_bone != 0)
    {
        xVec3AddTo(&pos_emit, &mat_root->pos);
    }

    ((FogBreathEmitter*)fxrec->input.fxparm)->SpewVolume(&pos_emit, mat_bone);
}
