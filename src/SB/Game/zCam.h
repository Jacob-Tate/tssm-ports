#ifndef ZCAM_H
#define ZCAM_H

#include "xCamera.h"
#include "xCam.h"
#include "zCamSB.h"

struct zScene;

zCamSB* zCamGetDefault();
xMat4x3& zCamGetMatrix();
void zCamRemove(xCam& cam, bool force_cut);
void zCamAdd(xCam& cam, bool force_cut);
void zCamEndRender();
void zCamBeginRender();
void zCamPrepareRender();
void zCamUpdate(F32 dt);
void zCamReset(const xMat4x3& mat);
void zCamSceneExit();
void zCamSceneEnter(zScene& scene, const xMat4x3& mat);
void zCamSetStartOrientation(F32 yaw, F32 pitch, F32 roll);
void zCamSetStartOrientation(U32 pointer_id);

#endif
