#ifndef ICOLLIDE_H
#define ICOLLIDE_H

#include "xMath3.h"
#include "xModel.h"
#include "xEnv.h"
#include "xCollide.h"

void iBoxForModelLocal(xBox* o, const xModelInstance* m);
void iBoxForModel(xBox* o, const xModelInstance* m);

// TSSM moved these into xCollide (target symbols are xSphereHitsEnv3, xSphereHitsEnv4,
// xSphereHitsModel3, xRayHitsModel and xRayHitsEnv).
S32 xSphereHitsEnv3(const xSphere* b, const xEnv* env, xCollis* colls, U8 ncolls, F32 sth);
S32 xSphereHitsEnv4(const xSphere* b, const xEnv* env, const xMat3x3* mat, xCollis* colls);
S32 xSphereHitsModel3(const xSphere* b, const xModelInstance* m, xCollis* colls, U8 ncolls,
                      F32 sth);
U32 xRayHitsModel(const xRay3* r, const xModelInstance* m, xCollis* coll);
U32 xRayHitsEnv(const xRay3* r, const xEnv* env, xCollis* coll);
void iCollideInit(xScene* sc);

#endif
