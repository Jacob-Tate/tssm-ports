#ifndef ZCINEFXSHARED_H
#define ZCINEFXSHARED_H

#include <types.h>
#include "xVec3.h"
#include "rwcore.h"
#include "rpworld.h"

#include "zNMESupplement.h"

struct zCutsceneMgr;
struct zLightning;
struct zParEmitter;
struct zNMEHazard;
struct xEnt;
struct xFXRibbon;
struct NCINRibbonInput;
struct NCINStreakInput;
struct NCINEntry;

struct NCINLyt
{
    zLightning* lyt_zap;
};

struct NCINStreak
{
    U32 streakID;
};

struct NCINArc
{
    zLightning* lightning;
    xVec3 endPos;
};

struct NCINPar
{
    zParEmitter* emitter;
};

struct NCINEnts
{
    xEnt* ent[4];
};

struct NCINCustom
{
    F32 f[1];
    xVec3 v[2];
    S32 n[3];
};

struct NCINMat
{
    RwMatrixTag* mat;
};

struct NCINRibbon
{
    xFXRibbon* ribbon;
};

struct NCINHaz
{
    zNMEHazard* npchaz;
};

struct NCINData
{
    union
    {
        NCINLyt lytdata;
        NCINStreak streakdata;
        NCINArc arcdata;
        NCINPar pardata;
        NCINEnts entdata;
        NCINCustom customdata;
        NCINMat matdata;
        NCINRibbon ribbondata;
        NCINHaz hazdata;
    };
};

struct TypeInfo
{
    union
    {
        S32 fxTypeID;
        S32 usrTypeID;
    };
    F32 tym_beg;
    F32 tym_end;
    char* twk_name;
};

// The NCIN_* table callbacks take a const zCutsceneMgr* (and the tweak ones a const char*), so
// the effect tables cast them to the Callbacks member types.
typedef void (*NCINUpdFunc)(zCutsceneMgr*, NCINEntry*, S32);
typedef void (*NCINAnimFunc)(zCutsceneMgr*, NCINEntry*, RpAtomic*, RwMatrixTag*, U32, U32);
typedef void (*NCINRendFunc)(zCutsceneMgr*, NCINEntry*);
typedef void (*NCINTweakFunc)(char*, NCINEntry*);

struct Callbacks
{
    void (*fxupd)(zCutsceneMgr*, NCINEntry*, S32);
    void (*fxanim)(zCutsceneMgr*, NCINEntry*, RpAtomic*, RwMatrixTag*, U32, U32);
    void (*fxrend)(zCutsceneMgr*, NCINEntry*);
    void (*tweaks)(char*, NCINEntry*);
};

struct IndexInput
{
    S32 idx_anim;
    S32 idx_bone;
    S32 idx_boneA;
    S32 idx_boneB;
};

struct PosInput
{
    xVec3 pos_A[2];
    xVec3 pos_B[2];
};

struct NCINInput
{
    union
    {
        void* whateverMakesTheCompilerHappyIsFineWithMe;
        void* fxparm;
        char* str_fx;
        S32 int_val;
        F32 flt_val;
        NCINRibbonInput* ribbon;
        NCINStreakInput* streak;
    };
};

struct NCINStreakInput
{
    F32 length;
    StreakInfo streakInfo;
};

// Size: 0x2C
struct NCINLytNMEInfo
{
    union
    {
        en_npclyt npclyt;
        S32 npclyt_tweakFriendly;
    };
    struct BoneA
    {
        S32 idx_anim;
        S32 idx_bone;
        xVec3 vec_offset;
    } src;
    struct
    {
        S32 idx_anim;
        S32 idx_bone;
        xVec3 vec_offset;
    } tgt;
};

// flg_stat bits
#define NCIN_STAT_ACTIVE 0x1
#define NCIN_STAT_DEAD 0x2
#define NCIN_STAT_FIRST 0x4
#define NCIN_STAT_8 0x8

struct NCINEntry
{
    TypeInfo info; // 0x0
    Callbacks cb; // 0x10
    IndexInput index; // 0x20
    PosInput basic; // 0x30
    NCINInput input; // 0x60
    void* userFXData; // 0x64
    NCINData fxdata; // 0x68
    S32 flg_stat; // 0x90
};

void zCinFX_StreakBone_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                          U32 animIndex, U32);
void zCinFX_StreakBone_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit);

inline void zCinFX_StreakBone_Twk(const char*, NCINEntry*)
{
}

void zCinFX_EntityBonePar_AR(const zCutsceneMgr*, NCINEntry* fxrec, RpAtomic*, RwMatrixTag* animMat,
                             U32 animIndex, U32);
void zCinFX_CycleOnce_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit);
void zCinFX_GenNCINInput_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit);
void zCinFX_Generic_Upd(const zCutsceneMgr*, NCINEntry* fxrec, S32 killit);

#endif
