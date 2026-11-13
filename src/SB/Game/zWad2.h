#ifndef ZWAD2_H
#define ZWAD2_H

// zAutoTweak.h first: retail puts its load_param<xColor_tag,S32> template after
// zFlameEmitter.cpp's templates and before xFont.h's statics.
#include "zAutoTweak.h"
#include <types.h>

#include "xCounter.h"
#include "xDebug.h"
#include "xPartition.h"
#include "xWad5.h"

#include "iParMgr.h"

#include "zFMV.h"
#include "zGame.h"
#include "zGameExtras.h"
#include "zGrapple.h"
#include "zHangable.h"
#include "zNPCMgr.h"
#include "zLight.h"
#include "zLightEffect.h"
#include "zLightning.h"
#include "zLightWeight.h"
#include "zLOD.h"
#include "zMusic.h"
#include "zNMECommon.h"
#include "zUI.h"

// Dwarf data says that most of these are only used here in zWad2.cpp

enum ePOWERUPMENU
{
    ePOWERUPMENU_KarateSpin,
    ePOWERUPMENU_Bash,
    ePOWERUPMENU_BubbleBowl,
    ePOWERUPMENU_SonicWave,
    ePOWERUPMENU_BellyBump,
    ePOWERUPMENU_Cartwheel,
    ePOWERUPMENU_BellyFlop,
    ePOWERUPMENU_Throw,
    ePOWERUPMENU_SBHealth,
    ePOWERUPMENU_PatHealth,
    ePOWERUPMENU_PointsAvail,
    ePOWERUPMENU_Max
};

namespace
{
    struct entrail_type
    {
        char* model_name;
        S32 bone;
        F32 rate;
        F32 cull_dist;
        xVec3 offset;
        xVec3 offset_rand;
        xVec3 vel;
        xVec3 vel_rand;
    };

    struct entrail_data
    {
        U16 flags;
        U16 type;
        xEnt* ent;
        xVec3 loc;
        xVec3 vel;
        F32 emitted;

        void reset();
        void update(F32 dt);
    };
} // namespace

struct zGooParams
{
    F32 depth;
    F32 orig_depth;
    xEnt* goo_ent;
};

struct PatTongueVert
{
    xVec3 pos;
    xModelTagWithNormal tag;
};

struct CollisionInfo
{
    xVec3 hitDir;
    S32 damage;
    F32 knockBackSpeed;
    zLightning* l;
    iSndGroupHandle soundHit1;
    iSndGroupHandle soundHit2;
};

struct PowerUpMenuTableEntry
{
    ePOWERUPMENU entry;
    char* name;
    S32 powerup;
    _xCounter* counter;
};

struct PowerUpMovieTableEntry
{
    S32 powerup;
    eFMVFile level[3];
};

struct taskEntry
{
    char name[4];
    U8 taskStatus[8];
    U32 numTasks;
};

struct extraEntry
{
    U8 type;
    U8 id;
    U8 chestsNeeded;
    U8 status;
};

struct extraCostumeEntry
{
    U32 costumeID;
    eSNDCurrentPlayer player;
    U32 gameExtrasFlag;
};

struct zMusicState
{
    U32 uMusicDefault;
    U32 uMusicRestart;
    U8 bPaused;
};

#endif
