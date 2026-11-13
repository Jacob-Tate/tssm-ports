#ifndef ZNMESUPPLEMENT_H
#define ZNMESUPPLEMENT_H

#include "zNMECentral.h"
#include "xMath3.h"
#include "iColor.h"

struct zLightning;
struct pointer_asset;
struct _tagLightningAdd;
struct xShadowSimpleCache;
struct xParabola;
struct xShadowCache;
struct xParabola;
struct xEnt;
struct RwRaster;
struct xParabola;

enum en_npclyt
{
    NME_LYT_PLACEHOLDER,
    NME_LYT_JELLYPINK,
    NME_LYT_LARVAARC,
    NME_LYT_ZAPBOOM,
    NME_LYT_NEPTUNETRIDENT,
    NME_LYT_NOMORE,
    NME_LYT_FORCE = 0x7fffffff
};

enum en_npcburst
{
    NME_BURST_BALL,
    NME_BURST_OILBUB,
    NME_BURST_TUBEBONK,
    NME_BURST_NOMORE,
    NME_BURST_FORCE = 0x7fffffff
};

enum en_typeSplash
{
    NMEC_SPLASH_WATER,
    NMEC_SPLASH_CHOCOLATE,
    NMEC_SPLASH_LAVA,
    NMEC_SPLASH_BILE,
    NMEC_SPLASH_OIL,
    NMEC_SPLASH_HOTSAUCE,
    NMEC_SPLASH_NUKEGOO,
    NMEC_SPLASH_NOMORE
};

struct NMECSplashData
{
    pointer_asset* ass_ptr;
    xVec3 pos_splash;
    F32 rad_splash;
    F32 tym_duration;
    en_typeSplash typ_splash;
};

struct StreakInfo
{
    F32 freq;
    F32 alf_fade;
    F32 alf_start;
    U32 idx_useTxtr;
    xColor_tag rgba_left;
    xColor_tag rgba_right;
    S32 taper;
};

struct JellySting
{
    S32 idx_curr;
    zLightning* zap_lytnin[2];
};

struct zMODSupple : zModule
{
    struct
    {
        S32 flg_doSlipFX : 1;
        S32 flg_doJellySting : 1;
        S32 flg_unused : 30;
    };
    F32 tmr_slipGasEmit;
    JellySting sting;

    void ModulePrepUse();
    void ScenePrepare();
    void ScenePostInit();
    void SceneReset();
    void Timestep(F32 dt);
    void StingerPrepare();
    void StingerUpdate(F32 dt);
};

// Defined inline in zNMESupplement.cpp (no GC symbol); declared here for earlier files of the wad.
inline xParabola* NMEC_ParabReserve();
xShadowSimpleCache* NMEC_SimpShadowCacheReserve();
inline xParabola* NMEC_ParabReserve();
void NMEC_SimpShadowCacheRelease(xShadowSimpleCache* entShad);
// Inline helpers defined in zNMESupplement.cpp (they use its static pools); declared here for
// HAZBasic::Stuff_Cleanup, which inlines them.
inline void NMEC_ShadowCacheRelease(xShadowCache* shadCache);
inline void NMEC_ParabRelease(xParabola* parab);
// Pool reset helpers (inline), defined in zNMESupplement.cpp with the pools; declared here for
// zMODSupple's scene functions, as BfBB's zNPCSupport.h does.
inline void NMEC_ShadowCacheReset();
inline void NMEC_SimpShadowCacheReset();
inline void NMEC_EntShadowCacheReset();
inline void NMEC_ParabReset();
inline void NMEC_DustRingCreate();
inline void NMEC_DustRingReset();
void NMEC_RenderProjTexture(RwRaster* rast, F32 factor, xMat4x3* mat, F32 radius, F32 height,
                            xShadowCache* cache, S32 fillCache, xEnt* ent);
void NMEC_RenderProjTextureFaceCamera(RwRaster* rast, F32 factor, xVec3* pos, F32 radius,
                                      F32 height, xShadowCache* cache, S32 fillCache, xEnt* ent);
void NMEC_MakeASplash(const xMat4x3* mat_orient, en_typeSplash typ_splash, F32 radius,
                      F32 tym_duration);
void NMEC_MakeASplash(const xVec3* pos_splash, const xVec3* dir_up, en_typeSplash typ_splash,
                      F32 radius, F32 tym_duration);
void NMEC_MakeASplash(const pointer_asset* ass_ptr, en_typeSplash typ_splash, F32 radius,
                      F32 tym_duration);
void NMEC_MakeASplash(const NMECSplashData* splash);
void NMEC_BurstBubble(en_npcburst, xVec3* pos_base, F32 rad_pop, S32 flags);
void NMEC_MakeLightningInfo(en_npclyt style, _tagLightningAdd* info);

// Inline, defined in zNMESupplement.cpp; declared here for the slimy hazards of
// zNMEHazardTypes.cpp.
inline void NMEC_PlayerSlip();

void zNMESupplement_Startup();

#endif
