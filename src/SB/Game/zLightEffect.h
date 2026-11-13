#ifndef ZLIGHTEFFECT_H
#define ZLIGHTEFFECT_H

#include "iLight.h"
#include "xBase.h"
#include "zLight.h"

#include <types.h>

typedef void (*lightInitFunc)(_zLight*);

void zLightOn(_zLight* zl, S32 on);
static F32 leGetRandom();
void zLightEffectStrobeSlow(_zLight* zlight, F32 seconds);
void zLightEffectStrobe(_zLight* zlight, F32 seconds);
void zLightEffectStrobeFast(_zLight* zlight, F32 seconds);
void zLightEffectDimSlow(_zLight* zlight, F32 seconds);
void zLightEffectDim(_zLight* zlight, F32 seconds);
void zLightEffectDimFast(_zLight* zlight, F32 seconds);
void zLightEffectHalfDimSlow(_zLight* zlight, F32 seconds);
void zLightEffectHalfDim(_zLight* zlight, F32 seconds);
void zLightEffectHalfDimFast(_zLight* zlight, F32 seconds);
void zLightEffectRandomColSlow(_zLight* zlight, F32 seconds);
void zLightEffectRandomCol(_zLight* zlight, F32 seconds);
void zLightEffectRandomColFast(_zLight* zlight, F32 seconds);
void zLightEffectInitStrobe(_zLight* zlight);
void zLightEffectInitDim(_zLight* zlight);
void zLightEffectInitHalfDim(_zLight* zlight);
void zLightEffectInitRandomCol(_zLight* zlight);
void zLightEffectInitFlicker(_zLight* zlight);
static void EffectFlicker(_zLight* zlight, F32 seconds, F32 min, F32 rnd);
void zLightEffectFlicker(_zLight* zlight, F32 seconds);
void zLightEffectFlickerSlow(_zLight* zlight, F32 seconds);
void zLightEffectFlickerErratic(_zLight* zlight, F32 seconds);

void zLightEffectInitCauldron(_zLight* zlight);
static F32 leBlendToCol(F32 f1, F32 f2, F32 f3);
void zLightEffectCauldron(_zLight* zlight, F32 seconds);

#endif
