#ifndef ZNMEHAZARDTYPEINFO_H
#define ZNMEHAZARDTYPEINFO_H

#include <types.h>
#include <rwcore.h>
#include <rpworld.h>

#include "zNMECommon.h"
#include "iSnd.h"
#include "zNMEHazardUVAnim.h"

struct zNMEHazard;
struct zShrapnelAsset;
struct xSweptSphere;

enum en_hazrc
{
    HAZ_RC_FAILED,
    HAZ_RC_SUCCESS,
    HAZ_RC_NOMORE,
    HAZ_RC_FORCE = 0x7fffffff
};

struct HAZShadowProps
{
    F32 alf_max;
    F32 rad_doCast;
    F32 rad_shadow;
    F32 hyt_shadow;
    S32 cacheFillFreq;
    F32 fac_maxEarly;
};

struct HAZSndGrpEntry
{
    char* nam_sndGrp;
    S32 flg_hazSnd;
    U32 flg_sndPlay;
    iSndGroupHandle sndGrpID;
};

struct HAZSndTable
{
    HAZSndGrpEntry grpEntry[1];
};

// Per-type hazard handler. Virtual order from the target vtables (__vt__14zNMEHazardType);
// the vptr follows the data members (offset 0x20).
struct zNMEHazardType
{
    en_haztyp typ_hazard;
    RpAtomic* rawModel;
    zShrapnelAsset* shrapass;
    UVAModelInfo* uvAnimInfo;
    HAZShadowProps* shadProps;
    RwRaster* rast_shadow;
    HAZSndTable* sndTable;
    struct
    {
        S32 flg_warnedOfBadConfigure : 1;
        S32 flg_warnedOfNoOwner : 1;
        S32 flg_unusedFlags : 30;
    };

    virtual void ScenePrepare()
    {
    }
    virtual void SceneFinish()
    {
    }
    virtual void ScenePostInit()
    {
    }
    virtual en_hazrc Configure(zNMEHazard* haz)
    {
        return HAZ_RC_FAILED;
    }
    virtual void Cleanup(zNMEHazard* haz)
    {
    }
    virtual void Update(zNMEHazard* haz, F32 dt)
    {
    }
    virtual void CollideResponse(zNMEHazard* haz, S32 flg_hazcolf, const xSweptSphere* swdata,
                                 F32 tym_inFuture)
    {
    }
    virtual void Render(zNMEHazard* haz)
    {
    }
    virtual void CloneRenderPrep()
    {
    }
    virtual void CloneRender(zNMEHazard** hazlist, S32 cnt)
    {
    }
    virtual void CloneRenderDone()
    {
    }
};

#endif
