#ifndef ZCINEFXSBCOMMON_H
#define ZCINEFXSBCOMMON_H

#include "zCineFXShared.h"

void zCinFX_BubSlamBone_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                           U32 animIndex, U32);

void zCinFX_BubTrailBoneSimp_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*,
                                RwMatrixTag* animMat, U32 animIndex, U32);

inline void zCinFX_BubTrailBoneSimp_Twk(const char*, NCINEntry*)
{
}

void zCinFX_BubbleTrail_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit);
void zCinFX_BubbleTrail_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                           U32 animIndex, U32);

inline void zCinFX_BubbleTrail_Twk(const char*, NCINEntry*)
{
}

void zCinFX_BubBurst_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                        U32 animIndex, U32);

inline void zCinFX_BubBurst_Twk(const char*, NCINEntry*)
{
}

void NCIN_NMERibHelp_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit);
void NCIN_NMERibHelp_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                        U32 animIndex, U32);

inline void NCIN_NMERibHelp_Twk(const char*, NCINEntry*)
{
}

void NCIN_NMEEjectShrapnel_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic* model,
                              RwMatrixTag* animMat, U32 animIndex, U32);
void NCIN_EjectShrapnel_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic* model,
                           RwMatrixTag* animMat, U32 animIndex, U32);

void NCIN_NMEPuffPropWash_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                             U32 animIndex, U32);

inline void NCIN_NMEPuffPropWash_Twk(const char*, NCINEntry*)
{
}

void NCIN_NMEBreathSpew_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                           U32 animIndex, U32);

inline void NCIN_NMEBreathSpew_Twk(const char*, NCINEntry*)
{
}

#endif
