#include "xVec3.h"

void zParticleCustomSceneEnter();
void zParticleCustomUpdate(F32 dt);
void zParticleCustomEmitFireSmoke(const xVec3* loc, const xVec3* vel, S32 amount, F32 life_min,
                                  F32 life_range, F32 size_min, F32 size_range, F32 dt);
