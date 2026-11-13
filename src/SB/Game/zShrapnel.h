#ifndef ZSHRAPNEL_H
#define ZSHRAPNEL_H

#include "xModel.h"
#include "xCurveAsset.h"
#include "xCollide.h"
#include "xMath.h"

#include "zParEmitter.h"
#include "zExplosion.h"

#include <rwcore.h>
#include <rpworld.h>

typedef struct zFrag;
typedef struct zShrapnelAsset;
struct zLightning;

enum zFragLocType
{
    eFragLocBone,
    eFragLocBoneUpdated,
    eFragLocBoneLocal,
    eFragLocBoneLocalUpdated,
    eFragLocTag,
    eFragLocTagUpdated,
    eFragLocCount,
    eFragLocForceSize = 0x7fffffff
};

struct zFragBone
{
    S32 index;
    xVec3 offset;
};

struct zFragLocInfo
{
    union
    {
        zFragBone bone;
        xModelTag tag;
    };
};

struct zFragLocation
{
    zFragLocType type;
    zFragLocInfo info;
    F32 rand_radius;
};

enum zFragType
{
    eFragInactive,
    eFragGroup,
    eFragShrapnel,
    eFragParticle,
    eFragProjectile,
    eFragLightning,
    eFragSound,
    eFragShockwave,
    eFragExplosion,
    eFragDistortion,
    eFragFire,
    eFragCount,
    eFragForceSize = 0x7fffffff
};

struct zFragAsset
{
    zFragType type;
    U32 id;
    U32 parentID[2];
    F32 lifetime;
    F32 delay;
};

struct zFragGroup
{
    zFrag* list[21];
};

struct zFragParticleAsset : zFragAsset
{
    zFragLocation source;
    zFragLocation vel;
    xParEmitterCustomSettings emit;
    U32 flags;
    U32 parEmitterID;
    zParEmitter* parEmitter;
};

struct zFragParticle
{
    zFragParticleAsset* fasset;
};

struct zFragProjectileAsset : zFragAsset
{
    U32 modelInfoID;
    RpAtomic* modelFile;
    zFragLocation launch;
    zFragLocation vel;
    zFragLocation velPlusMinus;
    zFragLocation rot;
    zFragLocation rotPlusMinus;
    F32 bounce;
    S32 maxBounces;
    U32 flags;
    U32 childID;
    zShrapnelAsset* child;
    F32 minScale;
    F32 maxScale;
    U32 scaleCurveID;
    xCurveAsset* scaleCurve;
    F32 gravity;
};

struct zFragProjectile
{
    zFragProjectileAsset* fasset;
    xModelInstance* model;
    xParabola path;
    F32 angVel;
    F32 t;
    F32 tColl;
    S32 numBounces;
    F32 scale;
    F32 alpha;
    F32 bounceC1;
    F32 bounceC2;
    xVec3 N;
    xVec3 axis;
};

struct zFragLightningAsset : zFragAsset
{
    zFragLocation start;
    zFragLocation end;
    U32 startParentID;
    U32 endParentID;
};

struct zFragLightning
{
    zFragLightningAsset* fasset;
    xModelInstance* startParent;
    xModelInstance* endParent;
    zLightning* lightning;
};

struct zFragSoundAsset : zFragAsset
{
    U32 assetID;
    zFragLocation source;
};

struct zFragSound
{
    zFragSoundAsset* fasset;
    iSndHandle soundID;
};

struct zFragShockwaveAsset : zFragAsset
{
    U32 modelInfoID;
    F32 birthRadius;
    F32 deathRadius;
    F32 birthVelocity;
    F32 deathVelocity;
    F32 birthSpin;
    F32 deathSpin;
    F32 birthColor[4];
    F32 deathColor[4];
};

struct zFragShockwave
{
    zFragShockwaveAsset* fasset;
    F32 currSize;
    F32 currVelocity;
    F32 deltVelocity;
    F32 currSpin;
    F32 deltSpin;
    F32 currColor[4];
    F32 deltColor[4];
};

struct zFragShrapnelAsset : zFragAsset
{
    U32 shrapnelID;
    zShrapnelAsset* shrapAsset;
};

struct zFragShrapnel
{
    zFragShrapnelAsset* fasset;
};

struct zFragExplosionAsset : zFragAsset
{
    U32 type;
    zFragLocation loc;
    U32 flags;
};

struct zFragExplosion
{
    zFragExplosionAsset* fasset;
    zExplosion::trail_emitter trail;
    xVec3 initPos;
};

struct zFragDistortionAsset : zFragAsset
{
    U32 type;
    zFragLocation loc;
    U32 flags;
    F32 radius;
    F32 duration;
    F32 intensity;
    F32 freq;
    F32 repeat_delay;
};

struct zFragDistortion
{
    zFragDistortionAsset* fasset;
    xVec3 loc;
    S32 type;
    F32 repeat_time;
};

struct zFragFireAsset : zFragAsset
{
    zFragLocation loc;
    U32 flags;
    F32 radius;
    F32 scale;
    F32 fuel;
    F32 heat;
    F32 damage;
    F32 knockback;
};

struct zFragFire
{
    zFragFireAsset* fasset;
};

struct zFragInfo
{
    union
    {
        zFragGroup group;
        zFragShrapnel shrapnel;
        zFragParticle particle;
        zFragProjectile projectile;
        zFragLightning lightning;
        zFragSound sound;
        zFragShockwave shockwave;
        zFragExplosion explosion;
        zFragDistortion distortion;
        zFragFire fire;
    };
};

struct zFrag
{
    zFragType type;
    zFragInfo info;
    F32 delay;
    F32 alivetime;
    F32 lifetime;
    void (*update)(zFrag*, F32);
    void (*destroy)(zFrag*);
    xModelInstance* parent[2];
    zFrag* prev;
    zFrag* next;
};

struct zShrapnelAsset
{
    S32 fassetCount;
    U32 shrapnelID;
    void (*initCB)(zShrapnelAsset*, xModelInstance*, xVec3*, void (*)(zFrag*, zFragAsset*));
};

struct zShrapnelParentList
{
    U32 parentID;
    xModelInstance* parentModel;
};

struct zShrapnelInitTable
{
    char* name;
    void (*initCB)(zShrapnelAsset*, xModelInstance*, xVec3*, void (*)(zFrag*, zFragAsset*));
    U32 ID;
};

struct zScene;

// No symbol in TSSM (always inlined).
inline void zShrapnel_CalcBounceCoefficients(F32& c1, F32& c2, F32 bounce, F32 friction)
{
    c1 = -1.0f * bounce - 1.0f;
    c2 = xlerp(1.0f, bounce, friction);
}

void zFrag_Free(zFrag* frag);
void zFragLoc_InitDir(zFragLocation* loc, xVec3* vec, xModelInstance* parent);
void zFragLoc_InitVec(zFragLocation* loc, xVec3* vec, xModelInstance* parent);
void zFrag_DefaultShrapnelUpdate(zFrag* frag, F32 dt);
void zFrag_DefaultSoundUpdate(zFrag* frag, F32 dt);
void zFrag_DefaultLightningUpdate(zFrag* frag, F32 dt);
void zFrag_DefaultProjectileUpdate(zFrag* frag, F32 dt);
void zFrag_ParticleManager(F32 dt);
void zFrag_DefaultParticleUpdate(zFrag* frag, F32 dt);
void zFrag_LightningManager(F32 dt);
void zFrag_ProjectileManager(F32 dt);
void zFrag_ProjectileRenderer();
U8 zFrag_DefaultInit(zFrag* frag, zFragAsset* fasset);
void zShrapnel_DefaultInit(zShrapnelAsset* shrap, xModelInstance* parent, xVec3* initVel,
                           void (*cb)(zFrag*, zFragAsset*));
void zShrapnel_GameInit();
void zShrapnel_SceneInit(zScene*);
void zShrapnel_Update(F32 dt);
void zShrapnel_Reset();
void zShrapnel_Render();
void zShrapnel_CinematicInit(zShrapnelAsset* shrap, RpAtomic* cinModel, RwMatrixTag* animMat,
                             xVec3* initVel, void (*cb)(zFrag*, zFragAsset*));

#endif
