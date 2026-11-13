#ifndef IENV_H
#define IENV_H

#include "xJSP.h"
#include "xMath3.h"

#include <rwcore.h>
#include <rpworld.h>

struct iEnv
{
    RpWorld* world;
    RpWorld* collision;
    RpWorld* fx;
    RpWorld* camera;
    S32 jsp_count;
    U32* jsp_aid;
    xJSPHeader** jsp_list;
    xBox* jsp_bound;
    S32* jsp_visibilityCount;
    RpLight* light[2];
    RwFrame* light_frame[2];
    S32 memlvl;
};

struct xEnvAsset;

void iEnvLoad(iEnv* env, const void* data, U32 datasize, S32 dataType);
void iEnvLoadBegin(iEnv* env, S32 dataType, S32 count);
bool iEnvLoadJSP(iEnv* env, U32 aid, const void* data, U32 datasize, S32 dataType, S32 index);
void iEnvLoadEnd(iEnv* env, S32 dataType);
void iEnvSetup(iEnv* env);
void iEnvFree(iEnv* env);
void iEnvDefaultLighting(iEnv*);
void iEnvLightingBasics(iEnv*, xEnvAsset*);
void iEnvRender(iEnv* env, bool);
void iEnvEndRenderFX(iEnv*);
void iEnvJSPOff(iEnv* env, xJSPHeader* jsp);
void iEnvJSPOn(iEnv* env, xJSPHeader* jsp);
void iEnvJSPVisibilityDec(iEnv* env, xJSPHeader* jsp);
void iEnvJSPVisibilityInc(iEnv* env, xJSPHeader* jsp);

inline RwBBox* iEnvGetBBox(iEnv* env)
{
    return &env->world->boundingBox;
}

#endif
