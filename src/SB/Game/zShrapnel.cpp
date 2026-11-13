#include "zShrapnel.h"

#include "iModel.h"
#include "xColor.h"
#include "xMemMgr.h"
#include "xModelBucket.h"
#include "xScreenWarp.h"
#include "xSnd.h"
#include "xString.h"
#include "xVec3Inlines.h"
#include "zFire.h"
#include "zGlobals.h"
#include "zLightning.h"

static void CinFragCB(zFrag* frag, zFragAsset* asset);

static xMat4x3 tmpMat;
static zFrag sFragPool[150];
static zFrag sFirstFreeFrag;
static zFrag sFirstActiveFrag;
static zFrag sProjectileList;
static zFrag sLightningList;
static zFrag sParticleList;
static zFrag sSoundList;
static zFrag sExplosionList;
static zFrag sDistortionList;
static zFrag sFireList;
static _tagLightningAdd sLightningAddInfo;
static zFragProjectileAsset sCinProj;

static S32 sNumActiveFrags;
static void (*sCinCB)(zFrag*, zFragAsset*);
static zFrag* sCinFrag;
static RpAtomic* sCinModel;

static zShrapnelInitTable sShrapnelTable[] = { { NULL, NULL, 0 } };

inline void zFragLoc_Setup(zFragLocation* loc, xModelInstance* parent)
{
    if ((loc->type & ~1) == eFragLocTag)
    {
        iModelTagSetup(&loc->info.tag, parent->Data, loc->info.tag.v.x, loc->info.tag.v.y,
                       loc->info.tag.v.z);
    }
}
inline void zFrag_ProjectileCollData(zFrag* frag)
{
    xCollis colls;

    frag->info.projectile.tColl = 1e38f;
    frag->info.projectile.path.initPos.y -=
        frag->info.projectile.model->Data->boundingSphere.radius;
    xParabolaHitsEnv(&frag->info.projectile.path, globals.sceneCur->env, &colls);

    if (colls.flags & 1)
    {
        frag->info.projectile.tColl = colls.dist;
        xVec3Copy(&frag->info.projectile.N, &colls.norm);
    }
    else
    {
        frag->info.projectile.tColl = 1e38f;
    }

    frag->info.projectile.path.initPos.y +=
        frag->info.projectile.model->Data->boundingSphere.radius;
}

void zFrag_ProjectileSetupPath(zFrag* frag, zFragProjectileAsset* passet);

static void zFrag_DistortionManager(F32 dt);
static void zFrag_DefaultFireUpdate(zFrag* frag, F32 dt);
static void zFrag_DefaultDistortionUpdate(zFrag* frag, F32 dt);
static void zFrag_DefaultExplosionUpdate(zFrag* frag, F32 dt);
static void zFrag_ExplosionManager(F32 dt);
static inline void zFrag_SoundManager(F32 dt);

inline zFrag* zFrag_Alloc(zFragType type)
{
    zFrag* result;

    if (sNumActiveFrags == 150)
    {
        return NULL;
    }

    result = sFirstFreeFrag.next;

    if (result->prev != NULL)
    {
        result->prev->next = result->next;
    }

    if (result->next != NULL)
    {
        result->next->prev = result->prev;
    }

    result->next = sFirstActiveFrag.next;
    result->prev = &sFirstActiveFrag;

    if (result->next != NULL)
    {
        result->next->prev = result;
    }

    if (result->prev != NULL)
    {
        result->prev->next = result;
    }

    sNumActiveFrags++;
    result->type = type;

    return result;
}

inline void zFrag_Free(zFrag* frag)
{
    if (frag->destroy != NULL)
    {
        frag->destroy(frag);
    }

    frag->type = eFragInactive;

    if (frag->next != NULL)
    {
        frag->next->prev = frag->prev;
    }

    if (frag->prev != NULL)
    {
        frag->prev->next = frag->next;
    }

    sNumActiveFrags--;
    frag->next = sFirstFreeFrag.next;
    frag->prev = &sFirstFreeFrag;

    if (frag->next != NULL)
    {
        frag->next->prev = frag;
    }

    if (frag->prev != NULL)
    {
        frag->prev->next = frag;
    }
}

void zShrapnel_GameInit()
{
    zShrapnelInitTable* curr = sShrapnelTable;
    // 'i' is not a PS2 DWARF local; retail keeps a separate induction variable.
    S32 i = 0;

    while (curr->name != NULL)
    {
        curr->ID = xStrHash(curr->name);
        i++;
        curr = &sShrapnelTable[i];
    }

    sLightningAddInfo.type = 1;
    sLightningAddInfo.flags = 0x10;
    sLightningAddInfo.total_points = 4;
    sLightningAddInfo.thickness = 0.1f;
    sLightningAddInfo.color = g_YELLOW;
    sLightningAddInfo.rand_radius = 0.25f;
    sLightningAddInfo.arc_height = 0.1f;
    sLightningAddInfo.rot_radius = 0.3f;
    sLightningAddInfo.setup_degrees = 65.0f;
    sLightningAddInfo.move_degrees = 7000.0f;

    sCinProj.type = eFragProjectile;
    sCinProj.id = 0;
    sCinProj.parentID[0] = 0;
    sCinProj.parentID[1] = 0;
    sCinProj.lifetime = 0.0f;
    sCinProj.delay = 0.0f;
    sCinProj.modelInfoID = 0;
    sCinProj.modelFile = NULL;
    sCinProj.launch.type = eFragLocBone;
    xVec3Init(&sCinProj.launch.info.bone.offset, 0.0f, 0.0f, 0.0f);
    sCinProj.launch.info.bone.index = 0;
    sCinProj.vel.type = eFragLocBone;
    xVec3Init(&sCinProj.vel.info.bone.offset, 0.0f, 0.0f, 0.0f);
    sCinProj.vel.info.bone.index = 0;
    sCinProj.bounce = 0.0f;
    sCinProj.maxBounces = -1;
    sCinProj.flags = 0x808;
    sCinProj.childID = 0;
    sCinProj.child = NULL;
    sCinProj.minScale = 1.0f;
    sCinProj.maxScale = 1.0f;
    sCinProj.scaleCurveID = 0;
    sCinProj.scaleCurve = NULL;
    sCinProj.gravity = 0.0f;
}

// No symbol in TSSM; named in Ratatouille's DWARF.
static inline void zShrapnel_SetShrapnelAssetInitCB(zShrapnelAsset* sasset)
{
    sasset->initCB = zShrapnel_DefaultInit;

    S32 i = 0;
    zShrapnelInitTable* curr = sShrapnelTable;

    while (curr->name != NULL)
    {
        if (curr->ID == sasset->shrapnelID)
        {
            sasset->initCB = curr->initCB;
            return;
        }

        i++;
        curr = &sShrapnelTable[i];
    }
}

// No GC symbol (always inlined); name and local from Ratatouille's DWARF.
static inline void zShrapnel_SceneInit_Cinematic()
{
    const char* dummyModel = "fx_generic_ball.dff";
    sCinModel = (RpAtomic*)xSTFindAsset(xStrHash(dummyModel), NULL);
}

void zShrapnel_SceneInit(zScene*)
{
    S32 i;
    S32 numShrapnel;
    S32 j;
    zShrapnelAsset* sa;
    zFragAsset* fa;
    zFragProjectileAsset* projAss;
    zFragParticleAsset* partAss;
    zFragShrapnelAsset* shrapAss;

    sFirstActiveFrag.next = NULL;
    sFragPool[0].type = eFragInactive;
    sFirstFreeFrag.next = &sFragPool[0];

    for (i = 1; i < 150; i++)
    {
        sFragPool[i - 1].next = &sFragPool[i];
        sFragPool[i].prev = &sFragPool[i - 1];
        sFragPool[i].type = eFragInactive;
    }

    sFragPool[0].prev = &sFirstFreeFrag;
    sFragPool[149].next = NULL;
    sNumActiveFrags = 0;

    sProjectileList.next = NULL;
    sLightningList.next = NULL;
    sParticleList.next = NULL;
    sSoundList.next = NULL;
    sExplosionList.next = NULL;
    sDistortionList.next = NULL;
    sFireList.next = NULL;

    zShrapnel_SceneInit_Cinematic();

    numShrapnel = xSTAssetCountByType('SHRP');

    for (i = 0; i < numShrapnel; i++)
    {
        sa = (zShrapnelAsset*)xSTFindAssetByType('SHRP', i, NULL);
        zShrapnel_SetShrapnelAssetInitCB(sa);

        fa = (zFragAsset*)(sa + 1);

        for (j = 0; j < sa->fassetCount; j++)
        {
            switch (fa->type)
            {
            case eFragProjectile:
                projAss = (zFragProjectileAsset*)fa;

                if (projAss->modelInfoID != 0)
                {
                    projAss->modelFile = (RpAtomic*)xSTFindAsset(projAss->modelInfoID, NULL);
                }

                if (projAss->scaleCurveID != 0)
                {
                    projAss->scaleCurve = (xCurveAsset*)xSTFindAsset(projAss->scaleCurveID, NULL);
                }

                if (projAss->childID != 0)
                {
                    projAss->child = (zShrapnelAsset*)xSTFindAsset(projAss->childID, NULL);
                }

                fa = projAss + 1;
                break;
            case eFragParticle:
                partAss = (zFragParticleAsset*)fa;

                if (partAss->parEmitterID != 0)
                {
                    partAss->parEmitter = zParEmitterFind(partAss->parEmitterID);
                }

                fa = partAss + 1;
                break;
            case eFragSound:
                fa = (zFragSoundAsset*)fa + 1;
                break;
            case eFragLightning:
                fa = (zFragLightningAsset*)fa + 1;
                break;
            case eFragExplosion:
                fa = (zFragExplosionAsset*)fa + 1;
                break;
            case eFragDistortion:
                fa = (zFragDistortionAsset*)fa + 1;
                break;
            case eFragFire:
                fa = (zFragFireAsset*)fa + 1;
                break;
            case eFragShrapnel:
                shrapAss = (zFragShrapnelAsset*)fa;

                if (shrapAss->shrapnelID != 0)
                {
                    shrapAss->shrapAsset =
                        (zShrapnelAsset*)xSTFindAsset(shrapAss->shrapnelID, NULL);
                }

                fa = shrapAss + 1;
                break;
            }
        }
    }
}

// No GC symbol: inlined into zSceneReset. Name and body from BfBB.
void zShrapnel_Reset()
{
    if (sNumActiveFrags == 0)
        return;

    zFrag* frag = sProjectileList.next;
    while (frag != NULL)
    {
        zFrag* next = frag->next;

        if (frag->info.projectile.model != NULL)
        {
            xModelInstanceFree(frag->info.projectile.model);
            frag->info.projectile.model = NULL;
        }
        zFrag_Free(frag);
        frag = next;
    }
}

void zShrapnel_Update(F32 dt)
{
    zFrag* curr;
    zFrag* next;

    if (sNumActiveFrags == 0)
    {
        return;
    }

    curr = sFirstActiveFrag.next;

    while (curr != NULL)
    {
        next = curr->next;

        if (curr->delay > 0.0f)
        {
            curr->delay -= dt;
        }
        else if (curr->update != NULL)
        {
            curr->update(curr, dt);
        }

        curr = next;
    }

    if (sProjectileList.next != NULL)
    {
        zFrag_ProjectileManager(dt);
    }

    if (sLightningList.next != NULL)
    {
        zFrag_LightningManager(dt);
    }

    if (sParticleList.next != NULL)
    {
        zFrag_ParticleManager(dt);
    }

    if (sSoundList.next != NULL)
    {
        zFrag_SoundManager(dt);
    }

    if (sExplosionList.next != NULL)
    {
        zFrag_ExplosionManager(dt);
    }

    if (sDistortionList.next != NULL)
    {
        zFrag_DistortionManager(dt);
    }
}

// No GC symbol: inlined into zSceneRenderPreFX. Body from BfBB.
void zShrapnel_Render()
{
    if (sNumActiveFrags != 0 && sProjectileList.next != NULL)
    {
        zFrag_ProjectileRenderer();
    }
}

void zShrapnel_DefaultInit(zShrapnelAsset* shrap, xModelInstance* parent, xVec3* initVel,
                           void (*cb)(zFrag*, zFragAsset*))
{
    zShrapnelParentList* plist;
    zFrag* frag;
    zFragAsset* fasset;
    S32 i;
    S32 j;
    S32 k;

    plist = (zShrapnelParentList*)xMemPushTemp(shrap->fassetCount * sizeof(zShrapnelParentList));
    fasset = (zFragAsset*)(shrap + 1);

    for (i = 0; i < shrap->fassetCount; i++)
    {
        frag = zFrag_Alloc(fasset->type);

        if (frag == NULL)
        {
            break;
        }

        for (j = 0; j < 2; j++)
        {
            if (fasset->parentID[j] == 0)
            {
                frag->parent[j] = parent;
            }
            else
            {
                for (k = 0; k < i; k++)
                {
                    if (fasset->parentID[j] == plist[k].parentID)
                    {
                        break;
                    }
                }

                frag->parent[j] = plist[k].parentModel;
            }
        }

        if (!zFrag_DefaultInit(frag, fasset))
        {
            xMemPopTemp(plist);
            return;
        }

        plist[i].parentID = fasset->id;

        if (frag->type == eFragInactive)
        {
            plist[i].parentModel = parent;
        }
        else if (fasset->type == eFragProjectile)
        {
            plist[i].parentModel = frag->info.projectile.model;

            if (initVel != NULL && !(((zFragProjectileAsset*)fasset)->flags & 0x40))
            {
                xVec3AddTo(&frag->info.projectile.path.initVel, initVel);
            }
        }
        else
        {
            plist[i].parentModel = frag->parent[0];
        }

        if (cb != NULL)
        {
            cb(frag, fasset);
        }

        switch (fasset->type)
        {
        case eFragProjectile:
            fasset = (zFragProjectileAsset*)fasset + 1;
            break;
        case eFragParticle:
            fasset = (zFragParticleAsset*)fasset + 1;
            break;
        case eFragSound:
            fasset = (zFragSoundAsset*)fasset + 1;
            break;
        case eFragLightning:
            fasset = (zFragLightningAsset*)fasset + 1;
            break;
        case eFragExplosion:
            fasset = (zFragExplosionAsset*)fasset + 1;
            break;
        case eFragDistortion:
            fasset = (zFragDistortionAsset*)fasset + 1;
            break;
        case eFragFire:
            fasset = (zFragFireAsset*)fasset + 1;
            break;
        case eFragShrapnel:
            fasset = (zFragShrapnelAsset*)fasset + 1;
            break;
        }
    }

    xMemPopTemp(plist);
}

static void CinFragCB(zFrag* frag, zFragAsset* asset)
{
    F32 time = 1.0f + (frag->delay + frag->lifetime);

    if (time > sCinFrag->lifetime)
    {
        sCinFrag->lifetime = time;
    }

    if (sCinCB != NULL)
    {
        sCinCB(frag, asset);
    }
}

void zShrapnel_CinematicInit(zShrapnelAsset* shrap, RpAtomic* cinModel, RwMatrixTag* animMat,
                             xVec3* initVel, void (*cb)(zFrag*, zFragAsset*))
{
    S32 i;
    zFrag* frag;
    zFragProjectile* proj;
    F32 spd;
    xModelInstance* model;

    if (cinModel == NULL || shrap == NULL || animMat == NULL)
    {
        return;
    }

    frag = zFrag_Alloc(eFragProjectile);
    frag->parent[0] = NULL;
    frag->parent[1] = NULL;

    proj = &frag->info.projectile;

    if (initVel != NULL)
    {
        spd = initVel->length();

        if (spd > 0.1f)
        {
            proj->axis = *initVel * (1.0f / spd);
        }
        else
        {
            proj->axis = g_Z3;
        }
    }
    else
    {
        proj->axis = g_Z3;
    }

    sCinProj.modelFile = cinModel;

    zFrag_DefaultInit(frag, &sCinProj);

    model = proj->model;

    if (model == NULL)
    {
        zFrag_Free(frag);
        return;
    }

    model->Data = sCinModel;

    xMat3x3Copy((xMat3x3*)model->Mat, &g_I3);
    xVec3Copy((xVec3*)&model->Mat->pos, (xVec3*)&animMat->pos);

    for (i = 1; i < model->BoneCount; i++)
    {
        xMat4x3Copy((xMat4x3*)(model->Mat + i), (xMat4x3*)(animMat + i));
    }

    sCinCB = cb;
    sCinFrag = frag;

    shrap->initCB(shrap, model, initVel, CinFragCB);

    sCinCB = NULL;
    sCinFrag = NULL;
    sCinProj.modelFile = NULL;
}

// No GC symbol (always inlined); name and locals from Ratatouille's DWARF.
static inline void random_sphere_offset(xVec3& offset, F32 radius)
{
    F32 ang = 2 * PI * xurand();
    F32 uz = 2.0f * xurand() - 1.0f;
    F32 r = xsqrt(1.0f - uz * uz);
    F32 rr = xurand();

    rr = 1.0f - rr * rr * rr;
    rr *= radius;
    offset.assign(rr * r * icos(ang), rr * r * isin(ang), rr * uz);
}

void zFragLoc_InitMat(zFragLocation* loc, xMat4x3* mat, xModelInstance* parent)
{
    S32 index;
    xVec3 offset;

    switch (loc->type & ~1)
    {
    case eFragLocBone:
    case eFragLocBoneLocal:
        index = loc->info.bone.index;

        if (index > parent->BoneCount)
        {
            index = 0;
        }

        if (index == 0)
        {
            xMat4x3Copy(mat, (xMat4x3*)parent->Mat);
        }
        else
        {
            xMat4x3Mul(mat, (xMat4x3*)(parent->Mat + index), (xMat4x3*)parent->Mat);
        }
        break;
    case eFragLocTag:
        xMat4x3Identity(mat);
        iModelTagEval(parent->Data, &loc->info.tag, parent->Mat, &mat->pos);
        break;
    }

    switch (loc->type & ~1)
    {
    case eFragLocBone:
        xMat3x3RMulVec(&offset, (xMat3x3*)parent->Mat, &loc->info.bone.offset);
        xVec3AddTo(&mat->pos, &offset);
        break;
    case eFragLocBoneLocal:
        xMat3x3RMulVec(&offset, mat, &loc->info.bone.offset);
        xVec3AddTo(&mat->pos, &offset);
        break;
    }

    if (loc->rand_radius > 0.0f)
    {
        random_sphere_offset(offset, loc->rand_radius);
        xVec3AddTo(&mat->pos, &offset);
    }
}

void zFragLoc_InitVec(zFragLocation* loc, xVec3* vec, xModelInstance* parent)
{
    xMat4x3 tmpMat;
    S32 index;
    xVec3 offset;

    switch (loc->type & ~1)
    {
    case eFragLocBone:
    case eFragLocBoneLocal:
        index = loc->info.bone.index;

        if (index > parent->BoneCount)
        {
            index = 0;
        }

        if (index == 0)
        {
            xMat4x3Copy(&tmpMat, (xMat4x3*)parent->Mat);
        }
        else
        {
            xMat4x3Mul(&tmpMat, (xMat4x3*)(parent->Mat + index), (xMat4x3*)parent->Mat);
        }
        break;
    case eFragLocTag:
        iModelTagEval(parent->Data, &loc->info.tag, parent->Mat, vec);
        return;
    }

    switch (loc->type & ~1)
    {
    case eFragLocBone:
        xMat3x3RMulVec(&offset, (xMat3x3*)parent->Mat, &loc->info.bone.offset);
        xVec3AddTo(&tmpMat.pos, &offset);
        break;
    case eFragLocBoneLocal:
        xMat3x3RMulVec(&offset, &tmpMat, &loc->info.bone.offset);
        xVec3AddTo(&tmpMat.pos, &offset);
        break;
    }

    xVec3Copy(vec, &tmpMat.pos);

    if (loc->rand_radius > 0.0f)
    {
        random_sphere_offset(offset, loc->rand_radius);
        xVec3AddTo(vec, &offset);
    }
}

void zFragLoc_InitDir(zFragLocation* loc, xVec3* vec, xModelInstance* parent)
{
    S32 index;
    xMat4x3 tmpMat;

    switch (loc->type & ~1)
    {
    case eFragLocBone:
        xMat3x3RMulVec(vec, (xMat3x3*)parent->Mat, &loc->info.bone.offset);
        break;
    case eFragLocBoneLocal:
        index = loc->info.bone.index;

        if (index > parent->BoneCount)
        {
            index = 0;
        }

        if (index == 0)
        {
            xMat4x3Copy(&::tmpMat, (xMat4x3*)parent->Mat);
            xMat3x3RMulVec(vec, (xMat3x3*)parent->Mat, &loc->info.bone.offset);
        }
        else
        {
            xMat4x3Mul(&tmpMat, (xMat4x3*)(parent->Mat + index), (xMat4x3*)parent->Mat);
            xMat3x3RMulVec(vec, &tmpMat, &loc->info.bone.offset);
        }
        break;
    case eFragLocTag:
        iModelTagEval(parent->Data, &loc->info.tag, parent->Mat, vec);
        break;
    }
}

// Placeholder for dead-stripped code: retail pools -0.00001f here, but no surviving zShrapnel
// function uses it. Ratatouille has an unused parabolaEnvFunc at this spot, the likely owner; its
// body is unknown.
static void __deadstripped_zShrapnel(F32* out)
{
    *out = -0.00001f;
}

U8 zFrag_DefaultInit(zFrag* frag, zFragAsset* fasset)
{
    zFragProjectileAsset* passet;
    RpAtomic* m;
    zFragProjectile* proj;
    zFragLightningAsset* lasset;
    zFragParticleAsset* prasset;
    zFragSoundAsset* sasset;
    zFragExplosionAsset* easset;

    frag->alivetime = 0.0f;
    frag->lifetime = fasset->lifetime;
    frag->delay = fasset->delay;
    frag->destroy = NULL;

    switch (fasset->type)
    {
    case eFragProjectile:
        passet = (zFragProjectileAsset*)fasset;
        frag->info.projectile.fasset = passet;
        frag->update = zFrag_DefaultProjectileUpdate;

        m = passet->modelFile;

        if (m != NULL)
        {
            frag->info.projectile.model = xModelInstanceAlloc(m, NULL, 0, 0, NULL);
        }
        else
        {
            frag->info.projectile.model = NULL;
        }

        if (frag->info.projectile.model == NULL)
        {
            zFrag_Free(frag);
            return FALSE;
        }

        if (frag->info.projectile.model != NULL && frag->parent[0] != NULL)
        {
            proj = &frag->info.projectile;

            xVec3Init(&proj->path.initVel, 0.0f, 0.0f, 0.0f);

            if (!(passet->flags & 0x10))
            {
                zFrag_ProjectileSetupPath(frag, passet);
            }

            proj->model->LightKit = xglobals->___player_ent_dont_use_directly->model->LightKit;
            proj->alpha = 1.0f;
            proj->numBounces = 0;
            zShrapnel_CalcBounceCoefficients(proj->bounceC1, proj->bounceC2, proj->fasset->bounce,
                                             0.0f);
            proj->scale = passet->minScale;

            if (passet->scaleCurve != NULL)
            {
                proj->scale *= xCurveAssetEvaluate(passet->scaleCurve, 0.0f);
            }
        }
        break;
    case eFragLightning:
        lasset = (zFragLightningAsset*)fasset;
        frag->info.lightning.fasset = lasset;
        frag->update = zFrag_DefaultLightningUpdate;

        if (frag->parent[0] != NULL && frag->parent[1] != NULL)
        {
            zFragLoc_Setup(&lasset->start, frag->parent[0]);
            zFragLoc_Setup(&lasset->end, frag->parent[1]);
        }
        break;
    case eFragParticle:
        prasset = (zFragParticleAsset*)fasset;
        frag->info.particle.fasset = prasset;
        frag->update = zFrag_DefaultParticleUpdate;

        if (frag->parent[0] != NULL)
        {
            zFragLoc_Setup(&prasset->source, frag->parent[0]);
            zFragLoc_Setup(&prasset->vel, frag->parent[0]);

            if (!(prasset->flags & 1))
            {
                zFragLoc_InitVec(&prasset->source, &prasset->emit.pos, frag->parent[0]);
                zFragLoc_InitDir(&prasset->vel, &prasset->emit.vel, frag->parent[0]);
            }
        }
        break;
    case eFragSound:
        sasset = (zFragSoundAsset*)fasset;
        frag->info.sound.fasset = sasset;
        frag->update = zFrag_DefaultSoundUpdate;

        if (frag->parent[0] != NULL)
        {
            zFragLoc_Setup(&sasset->source, frag->parent[0]);
        }
        break;
    case eFragExplosion:
        easset = (zFragExplosionAsset*)fasset;
        frag->info.explosion.fasset = easset;

        if (!(easset->flags & 1))
        {
            zFragLoc_Setup(&easset->loc, frag->parent[0]);
            zFragLoc_InitVec(&easset->loc, &frag->info.explosion.initPos, frag->parent[0]);
        }

        frag->update = zFrag_DefaultExplosionUpdate;
        break;
    case eFragDistortion:
        frag->info.distortion.fasset = (zFragDistortionAsset*)fasset;
        frag->update = zFrag_DefaultDistortionUpdate;
        break;
    case eFragFire:
        frag->info.fire.fasset = (zFragFireAsset*)fasset;
        frag->update = zFrag_DefaultFireUpdate;
        break;
    case eFragShrapnel:
        frag->info.shrapnel.fasset = (zFragShrapnelAsset*)fasset;
        frag->update = zFrag_DefaultShrapnelUpdate;
        break;
    }

    return TRUE;
}

void zFrag_DefaultParticleUpdate(zFrag* frag, F32 dt)
{
    zFragParticleAsset* passet = frag->info.particle.fasset;

    if ((passet->flags & 1) || (passet->source.type & 1))
    {
        zFragLoc_InitVec(&passet->source, &passet->emit.pos, frag->parent[0]);
    }

    if ((passet->flags & 1) || (passet->vel.type & 1))
    {
        zFragLoc_InitDir(&passet->vel, &passet->emit.vel, frag->parent[0]);
    }

    passet->emit.last_emit_position = passet->emit.pos;
    passet->emit.last_emit_velocity = passet->emit.vel;

    if (frag->prev != NULL)
        frag->prev->next = frag->next;
    if (frag->next != NULL)
        frag->next->prev = frag->prev;

    frag->next = sParticleList.next;
    frag->prev = &sParticleList;

    if (frag->prev != NULL)
        frag->prev->next = frag;
    if (frag->next != NULL)
        frag->next->prev = frag;
}

void zFrag_ParticleManager(F32 dt)
{
    zFrag* frag = sParticleList.next;

    while (frag != NULL)
    {
        zFrag* next = frag->next;
        zFragParticleAsset* passet = frag->info.particle.fasset;

        frag->lifetime -= dt;
        frag->alivetime += dt;

        if (frag->lifetime < 0.0f)
        {
            zFrag_Free(frag);
        }
        else
        {
            if (passet->source.type & 1)
            {
                zFragLoc_InitVec(&passet->source, &passet->emit.pos, frag->parent[0]);
            }

            if (passet->vel.type & 1)
            {
                zFragLoc_InitDir(&passet->vel, &passet->emit.vel, frag->parent[0]);
            }

            if (passet->parEmitter != NULL)
            {
                xParEmitterEmitCustom(passet->parEmitter, dt, &passet->emit);
            }
        }

        frag = next;
    }
}

inline void zFrag_ProjectileSetupPath(zFrag* frag, zFragProjectileAsset* passet)
{
    zFragProjectile* proj = &frag->info.projectile;
    xVec3 baseVel;

    zFragLoc_Setup(&passet->launch, frag->parent[0]);
    zFragLoc_InitMat(&passet->launch, (xMat4x3*)proj->model->Mat, frag->parent[0]);
    xVec3Copy(&proj->path.initPos, (xVec3*)&proj->model->Mat->pos);
    proj->t = 0.0f;

    if (passet->flags & 8)
    {
        zFragLoc_Setup(&passet->vel, frag->parent[0]);
        zFragLoc_InitDir(&passet->vel, &baseVel, frag->parent[0]);

        if (proj->fasset->flags & 0x400)
        {
            zFragLocation randLoc = passet->velPlusMinus;
            xVec3 randVel;

            randLoc.info.bone.offset.x *= 2.0f * xurand() - 1.0f;
            randLoc.info.bone.offset.y *= 2.0f * xurand() - 1.0f;
            randLoc.info.bone.offset.z *= 2.0f * xurand() - 1.0f;

            zFragLoc_Setup(&randLoc, frag->parent[0]);
            zFragLoc_InitDir(&randLoc, &randVel, frag->parent[0]);
            xVec3AddTo(&baseVel, &randVel);
        }
    }
    else
    {
        xVec3Sub(&baseVel, (xVec3*)&proj->model->Mat->pos, (xVec3*)&frag->parent[0]->Mat->pos);
        xVec3SMulBy(&baseVel, 0.25f);

        if (baseVel.y < 0.0f)
        {
            baseVel.y = 0.0f;
        }

        baseVel.y += 3.0f * xurand() + 4.0f;
        baseVel.x += 6.0f * xurand() - 3.0f;
        baseVel.z += 6.0f * xurand() - 3.0f;
    }

    xVec3AddTo(&proj->path.initVel, &baseVel);

    if (proj->fasset->flags & 0x20)
    {
        xVec3Copy(&proj->axis, &baseVel);

        F32 len2 = xVec3Length2(&proj->axis);

        if (len2 > 1e-5f)
        {
            xVec3SMulBy(&proj->axis, 1.0f / xsqrt(len2));
        }
        else if (proj->path.gravity < 0.0f)
        {
            xVec3Init(&proj->axis, 0.0f, 1.0f, 0.0f);
        }
        else
        {
            xVec3Init(&proj->axis, 0.0f, -1.0f, 0.0f);
        }

        xVec3 back;
        xVec3Inv(&back, &proj->axis);
        xMat3x3LookVec((xMat3x3*)proj->model->Mat, &back);
    }
    else if (proj->fasset->flags & 0x100)
    {
        // The rotation axis really is set up twice.
        zFragLoc_Setup(&passet->rot, frag->parent[0]);
        zFragLoc_InitDir(&passet->rot, &proj->axis, frag->parent[0]);
        zFragLoc_Setup(&passet->rot, frag->parent[0]);
        zFragLoc_InitDir(&passet->rot, &proj->axis, frag->parent[0]);

        if (proj->fasset->flags & 0x200)
        {
            zFragLocation randLoc = passet->rotPlusMinus;
            xVec3 randAxis;

            randLoc.info.bone.offset.x *= 2.0f * xurand() - 1.0f;
            randLoc.info.bone.offset.y *= 2.0f * xurand() - 1.0f;
            randLoc.info.bone.offset.z *= 2.0f * xurand() - 1.0f;

            zFragLoc_Setup(&randLoc, frag->parent[0]);
            zFragLoc_InitDir(&randLoc, &randAxis, frag->parent[0]);
            xVec3AddTo(&proj->axis, &randAxis);
        }

        proj->angVel = xVec3Length(&proj->axis);

        if (proj->angVel > 1e-5f)
        {
            xVec3SMulBy(&proj->axis, 1.0f / proj->angVel);
        }
        else
        {
            xVec3Init(&proj->axis, 0.0f, 1.0f, 0.0f);
        }

        proj->angVel = (PI / 180.0f) * proj->angVel;
    }
    else
    {
        if (baseVel.x < 0.01f && baseVel.x > -0.01f && baseVel.z < 0.01f && baseVel.z > -0.01f)
        {
            xVec3Init(&proj->axis, 1.0f, 0.0f, 0.0f);
            proj->angVel = 5.0f;
        }
        else
        {
            xVec3Init(&proj->axis, baseVel.z, 0.0f, -baseVel.x);
            proj->angVel = 3.0f * xVec3Normalize(&proj->axis, &proj->axis) + 4.0f;
        }

        proj->angVel *= 0.5f + xurand();
    }

    proj->path.gravity = passet->gravity;
}

void zFrag_DefaultProjectileUpdate(zFrag* frag, F32 dt)
{
    zFragProjectileAsset* passet = frag->info.projectile.fasset;

    if (passet->flags & 0x10)
    {
        zFrag_ProjectileSetupPath(frag, passet);
    }

    if (passet->flags & 1)
    {
        frag->info.projectile.path.minTime = 0.0f;
        frag->info.projectile.path.maxTime = 1.0f;
        zFrag_ProjectileCollData(frag);
        frag->info.projectile.t = 0.0f;
    }

    if (frag->prev != NULL)
        frag->prev->next = frag->next;
    if (frag->next != NULL)
        frag->next->prev = frag->prev;

    frag->next = sProjectileList.next;
    frag->prev = &sProjectileList;

    if (frag->prev != NULL)
        frag->prev->next = frag;
    if (frag->next != NULL)
        frag->next->prev = frag;
}

inline void zFrag_DeleteProjectile(zFrag* frag)
{
    xVec3 finalVel;
    zShrapnelAsset* child = frag->info.projectile.fasset->child;

    if (child != NULL && child->initCB != NULL)
    {
        xParabolaEvalVel(&frag->info.projectile.path, &finalVel, frag->info.projectile.t);
        child->initCB(child, frag->info.projectile.model, &finalVel, NULL);
    }

    if (frag->info.projectile.model != NULL)
    {
        xModelInstanceFree(frag->info.projectile.model);
        frag->info.projectile.model = NULL;
    }

    zFrag_Free(frag);
}

void zFrag_ProjectileManager(F32 dt)
{
    zFrag* frag;
    zFragProjectile* proj;
    zFrag* next;
    S32 killed;
    F32 percent;
    F32 minScale;
    F32 newScale;
    xVec3 back;
    xMat3x3 spin;

    frag = sProjectileList.next;

    while (frag != NULL)
    {
        proj = &frag->info.projectile;
        next = frag->next;

        frag->lifetime -= dt;
        frag->alivetime += dt;

        if (frag->lifetime < 0.0f)
        {
            zFrag_DeleteProjectile(frag);
        }
        else if (!(proj->fasset->flags & 0x80))
        {
            proj->t += dt;
            killed = FALSE;

            if (proj->fasset->flags & 1)
            {
                if (proj->t > proj->tColl)
                {
                    proj->numBounces++;

                    if (proj->fasset->maxBounces >= 0 &&
                        proj->numBounces > proj->fasset->maxBounces)
                    {
                        xVec3Init(&proj->path.initVel, 0.0f, 0.0f, 0.0f);
                        zFrag_DeleteProjectile(frag);
                        killed = TRUE;
                    }
                    else
                    {
                        xParabolaRecenter(&proj->path, proj->tColl);

                        percent = xVec3Dot(&proj->N, &proj->path.initVel);
                        xVec3AddScaled(&proj->path.initVel, &proj->N, proj->bounceC1 * percent);
                        xVec3SMulBy(&proj->path.initVel, proj->bounceC2);
                        xVec3AddScaled(&proj->path.initPos, &proj->path.initVel,
                                       proj->t - proj->tColl);
                        proj->path.minTime = 0.0f;
                        proj->path.maxTime = frag->lifetime;

                        if (!(proj->fasset->flags & 0x20))
                        {
                            xVec3 tanVel;

                            xVec3Cross(&proj->axis, &proj->N, &proj->path.initVel);
                            xVec3Normalize(&proj->axis, &proj->axis);
                            xVec3Copy(&tanVel, &proj->path.initVel);
                            xVec3AddScaled(&tanVel, &proj->N, percent * proj->fasset->bounce);
                            proj->angVel =
                                xVec3Length(&tanVel) / proj->model->Data->boundingSphere.radius;
                        }

                        zFrag_ProjectileCollData(frag);
                        proj->t = 0.0f;
                    }
                }
                else if (proj->t > proj->path.maxTime)
                {
                    proj->path.minTime = proj->path.maxTime;
                    proj->path.maxTime += 1.0f;
                    zFrag_ProjectileCollData(frag);
                }
            }

            if (!killed)
            {
                xVec3 oldpos;

                minScale = proj->fasset->minScale;

                if (minScale != proj->fasset->maxScale || proj->fasset->scaleCurve != NULL)
                {
                    percent = frag->alivetime / (frag->lifetime + frag->alivetime);
                    newScale = percent * (proj->fasset->maxScale - minScale) + minScale;

                    if (proj->fasset->scaleCurve != NULL)
                    {
                        if (proj->fasset->flags & 4)
                        {
                            newScale *= xCurveAssetEvaluate(proj->fasset->scaleCurve, percent);
                        }
                        else
                        {
                            newScale *=
                                xCurveAssetEvaluate(proj->fasset->scaleCurve, frag->alivetime);
                        }
                    }

                    proj->scale = newScale;
                }

                xVec3Copy(&oldpos, (xVec3*)&proj->model->Mat->pos);
                xVec3Copy((xVec3*)&proj->model->Mat->pos, &proj->path.initPos);
                xVec3AddScaled((xVec3*)&proj->model->Mat->pos, &proj->path.initVel, proj->t);
                proj->model->Mat->pos.y -= proj->t * (0.5f * proj->path.gravity * proj->t);

                if (proj->fasset->flags & 0x20)
                {
                    xVec3 newAxis;
                    F32 len2;

                    xVec3Sub(&back, (xVec3*)&proj->model->Mat->pos, &oldpos);
                    len2 = xVec3Length2(&back);

                    if (len2 > 1e-5f)
                    {
                        xVec3SMul(&proj->axis, &back, 1.0f / xsqrt(len2));
                    }

                    xVec3Inv(&newAxis, &proj->axis);
                    xMat3x3LookVec((xMat3x3*)proj->model->Mat, &newAxis);
                }
                else
                {
                    xMat3x3Rot(&spin, &proj->axis, dt * proj->angVel);
                    xMat3x3Mul((xMat3x3*)proj->model->Mat, (xMat3x3*)proj->model->Mat, &spin);
                }
            }
        }

        frag = next;
    }
}

void zFrag_DefaultLightningUpdate(zFrag* frag, F32 dt)
{
    zFragLightningAsset* lasset = frag->info.lightning.fasset;
    xVec3 start;
    xVec3 end;

    sLightningAddInfo.start = &start;
    sLightningAddInfo.end = &end;
    sLightningAddInfo.segmentsPerMeter = 3.0f;

    zFragLoc_InitVec(&lasset->start, sLightningAddInfo.start, frag->parent[0]);
    zFragLoc_InitVec(&lasset->end, sLightningAddInfo.end, frag->parent[1]);

    // xStrHash("LIGHTNING")
    sLightningAddInfo.mainTexture = 0x24e99e06;
    sLightningAddInfo.branchTexture = 0x24e99e06;

    frag->info.lightning.lightning = zLightningAdd(&sLightningAddInfo);

    if (frag->prev != NULL)
        frag->prev->next = frag->next;
    if (frag->next != NULL)
        frag->next->prev = frag->prev;

    frag->next = sLightningList.next;
    frag->prev = &sLightningList;

    if (frag->prev != NULL)
        frag->prev->next = frag;
    if (frag->next != NULL)
        frag->next->prev = frag;
}

void zFrag_LightningManager(F32 dt)
{
    zFrag* frag;
    zFragLightning* ligh;
    zFragLightningAsset* lasset;
    zLightning* inst;
    zFrag* next;
    xVec3 start;
    xVec3 end;

    frag = sLightningList.next;

    while (frag != NULL)
    {
        next = frag->next;
        ligh = &frag->info.lightning;
        inst = ligh->lightning;
        lasset = ligh->fasset;

        frag->lifetime -= dt;
        frag->alivetime += dt;

        if (frag->lifetime < 0.0f)
        {
            zLightningKill(inst);
            zFrag_Free(frag);
        }
        else if ((lasset->start.type & 1) || (lasset->end.type & 1))
        {
            if (lasset->start.type & 1)
            {
                zFragLoc_InitVec(&lasset->start, &start, frag->parent[0]);
            }
            else
            {
                xVec3Copy(&start, &inst->func.endPoint[0]);
            }

            if (lasset->end.type & 1)
            {
                zFragLoc_InitVec(&lasset->end, &end, frag->parent[1]);
            }
            else
            {
                xVec3Copy(&end, &inst->func.endPoint[1]);
            }

            zLightningModifyEndpoints(inst, &start, &end);
        }

        frag = next;
    }
}

void zFrag_DefaultSoundUpdate(zFrag* frag, F32 dt)
{
    zFragSound* sound = &frag->info.sound;
    zFragSoundAsset* sasset = sound->fasset;
    xVec3 source;

    zFragLoc_InitVec(&sasset->source, &source, frag->parent[0]);

    sound->soundID =
        xSndMgrPlay(xSndMgrGetSoundGroup(sasset->assetID), 0, &source, NULL, NULL, NULL, NULL);

    if (frag->prev != NULL)
        frag->prev->next = frag->next;
    if (frag->next != NULL)
        frag->next->prev = frag->prev;

    frag->next = sSoundList.next;
    frag->prev = &sSoundList;

    if (frag->prev != NULL)
        frag->prev->next = frag;
    if (frag->next != NULL)
        frag->next->prev = frag;
}

static inline void zFrag_SoundManager(F32 dt)
{
    zFrag* frag;
    zFragSound* sound;
    zFrag* next;

    frag = sSoundList.next;

    while (frag != NULL)
    {
        next = frag->next;
        sound = &frag->info.sound;

        frag->lifetime -= dt;
        frag->alivetime += dt;

        if (frag->lifetime < 0.0f)
        {
            if (sound->soundID != (iSndHandle)-1)
            {
                xSndMgrStop(sound->soundID);
            }

            zFrag_Free(frag);
        }

        frag = next;
    }
}

static void zFrag_DefaultExplosionUpdate(zFrag* frag, F32 dt)
{
    zFragExplosion* f = &frag->info.explosion;
    zFragExplosionAsset* a = f->fasset;
    xVec3 loc;

    if ((a->flags & 1) || (a->loc.type & 1))
    {
        zFragLoc_Setup(&a->loc, frag->parent[0]);
        zFragLoc_InitVec(&a->loc, &loc, frag->parent[0]);
    }
    else
    {
        xVec3Copy(&loc, &f->initPos);
    }

    S32 type = zExplosion::find_type(a->type);

    if (a->loc.type & 1)
    {
        f->trail.start(type, loc);

        if (frag->prev != NULL)
            frag->prev->next = frag->next;
        if (frag->next != NULL)
            frag->next->prev = frag->prev;

        frag->next = sExplosionList.next;
        frag->prev = &sExplosionList;

        if (frag->prev != NULL)
            frag->prev->next = frag;
        if (frag->next != NULL)
            frag->next->prev = frag;
    }
    else
    {
        zExplosion::emit(type, loc);
        zFrag_Free(frag);
    }
}

static void zFrag_ExplosionManager(F32 dt)
{
    zFrag* frag;
    zFrag* next;
    zFragExplosion* f;
    zFragExplosionAsset* a;
    xVec3 loc;

    frag = sExplosionList.next;

    while (frag != NULL)
    {
        next = frag->next;
        f = &frag->info.explosion;
        a = f->fasset;

        frag->lifetime -= dt;
        frag->alivetime += dt;

        if (frag->lifetime < 0.0f)
        {
            f->trail.stop();
            zFrag_Free(frag);
            frag = next;
            continue;
        }

        zFragLoc_InitVec(&a->loc, &loc, frag->parent[0]);
        f->trail.update(loc, dt, 1.0f);

        frag = next;
    }
}

static void zFrag_DefaultDistortionUpdate(zFrag* frag, F32 dt)
{
    zFragDistortion* f = &frag->info.distortion;
    zFragDistortionAsset* a = f->fasset;
    S32 flags;

    f->type = xScreenWarp::find_type(a->type);

    if (f->type == xScreenWarp::TYPE_INVALID)
    {
        zFrag_Free(frag);
        return;
    }

    f->repeat_time = 0.0f;

    zFragLoc_Setup(&a->loc, frag->parent[0]);
    zFragLoc_InitVec(&a->loc, &f->loc, frag->parent[0]);

    flags = 0;

    if ((a->loc.type & 1) && (a->flags & 1))
    {
        a->repeat_delay = 0.0f;
        flags |= 1;
    }

    xScreenWarp::emit(f->loc, a->radius, a->duration, (xScreenWarp::type_enum)f->type, a->intensity,
                      a->freq, flags);

    if (frag->prev != NULL)
        frag->prev->next = frag->next;
    if (frag->next != NULL)
        frag->next->prev = frag->prev;

    frag->next = sDistortionList.next;
    frag->prev = &sDistortionList;

    if (frag->prev != NULL)
        frag->prev->next = frag;
    if (frag->next != NULL)
        frag->next->prev = frag;
}

static void zFrag_DefaultFireUpdate(zFrag* frag, F32 dt)
{
    zFragFireAsset* a = frag->info.fire.fasset;
    zFire::spawn_data spawn;

    spawn.flags = 0;
    spawn.radius = a->scale;
    spawn.fuel = a->fuel;
    spawn.heat = a->heat;
    spawn.heat_absorb = 0.0f;
    spawn.driver = NULL;
    // The asset's damage/knockback fields are used directly as the fire's property_set.
    spawn.props = (zFire::property_set*)&a->damage;

    if (a->flags & 1)
    {
        spawn.flags |= 1;
    }

    xVec3Init(&spawn.up, 0.0f, 1.0f, 0.0f);

    zFragLoc_Setup(&a->loc, frag->parent[0]);
    zFragLoc_InitVec(&a->loc, &spawn.loc, frag->parent[0]);
    zFire::emit_sphere(spawn, spawn.loc, a->radius);

    zFrag_Free(frag);
}

static void zFrag_DistortionManager(F32 dt)
{
    zFrag* frag;
    zFrag* next;
    zFragDistortion* f;
    zFragDistortionAsset* a;

    frag = sDistortionList.next;

    while (frag != NULL)
    {
        next = frag->next;

        frag->lifetime -= dt;
        frag->alivetime += dt;

        if (frag->lifetime < 0.0f)
        {
            // Retail bug: the freed frag is still processed below.
            zFrag_Free(frag);
        }

        f = &frag->info.distortion;
        a = f->fasset;

        if (a->loc.type & 1)
        {
            zFragLoc_InitVec(&a->loc, &f->loc, frag->parent[0]);
        }

        if (a->repeat_delay > 0.0f)
        {
            f->repeat_time += dt;

            if (f->repeat_time >= a->repeat_delay)
            {
                xScreenWarp::emit(f->loc, a->radius, a->duration, (xScreenWarp::type_enum)f->type,
                                  a->intensity, a->freq, 0);
                f->repeat_time -= a->repeat_delay;
            }
        }

        frag = next;
    }
}

void zFrag_DefaultShrapnelUpdate(zFrag* frag, F32 dt)
{
    zShrapnelAsset* sasset = frag->info.shrapnel.fasset->shrapAsset;

    if (sasset != NULL && sasset->initCB != NULL)
    {
        sasset->initCB(sasset, frag->parent[0], NULL, NULL);
    }

    zFrag_Free(frag);
}

// No GC symbol: inlined into zSceneRenderPreFX (through zShrapnel_Render).
void zFrag_ProjectileRenderer()
{
    for (zFrag* frag = sProjectileList.next; frag != NULL; frag = frag->next)
    {
        zFragProjectile* proj = &frag->info.projectile;

        if (proj->model != NULL && !(proj->fasset->flags & 0x800))
        {
            if (!(proj->fasset->flags & 0x1000))
            {
                proj->model->Scale.x = proj->model->Scale.y = proj->model->Scale.z = proj->scale;
            }

            proj->model->Alpha = proj->alpha;

            if (frag->lifetime < 1.0f && proj->fasset->child == NULL)
            {
                if (proj->fasset->flags & 0x2000)
                {
                    // Shrink the model as it dies instead of fading it out.
                    xMat4x3& model_mat = *(xMat4x3*)proj->model->Mat;
                    F32 mag2 = model_mat.right.length2();

                    // 0x322BCC76 in retail: 0.0001f squared, one ULP below 1e-8f.
                    if (mag2 > SQR(0.0001f))
                    {
                        F32 scale = proj->scale * frag->lifetime * xinvsqrt(mag2);

                        model_mat.right *= scale;
                        model_mat.up *= scale;
                        model_mat.at *= scale;
                    }

                    xModelBucket_Add(proj->model);
                }
                else
                {
                    xModelBucket_AddAlphaFade(proj->model, frag->lifetime);
                }
            }
            else
            {
                xModelBucket_Add(proj->model);
            }
        }
    }
}
