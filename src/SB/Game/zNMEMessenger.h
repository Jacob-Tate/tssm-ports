#ifndef ZNMEMESSENGER_H
#define ZNMEMESSENGER_H

#include "zNMECentral.h"
#include "zNMECommon.h"
#include "xordarray.h"

struct NMEMsg;

struct NMEPSClt
{
    void (*notify)(void*, NMEMsg*);
    void* notedata;
    S32 flg_filter;
    NMEPSClt* next;
};

struct NMEPSData
{
    NMEPSClt* cltblob;
    NMEPSClt* cltfree;
    st_XORDEREDARRAY cltlist;
    NMEMsg* msgblob;
    NMEMsg* msgfree;
    st_XORDEREDARRAY quelist;
};

// Collision callback data for an explosion's grid query (layout from the PS2 DWARF).
struct ExplodeCollideData
{
    zNMECommon* npc_from;
    F32 spd_explode;
    xBound bound;
    NMEMsg msgdata;

    U8 operator()(xEnt& ent);
};

struct zMODMessage : zModule
{
    void SceneFinish();
    void SceneReset();
    void Timestep(F32 dt);
};

void zNMEMsg_AreaNotify(zNMECommon* sender, NMEMsg* msg, F32 radius, S32 filter,
                        en_npctyp* npcTypeList);
void zNMEMsg_AreaNotify(zNMECommon* sender, en_npcmsg msgid, F32 rad, S32 filter,
                        en_npctyp toNMEType);
void zNMEMsg_SendMsg(NMEMsg* inmsg, zNMECommon* npc_sendto, F32 delay);
void zNMEMsg_SendMsg(en_npcmsg msgevent, zNMECommon* npc_sendto, F32 delay);
void zNMEMsg_AreaPlayerStun(F32 stuntime, F32 radius, xVec3* pos);
void zNMEMsg_AreaNMEExplode(zNMECommon* who, F32 radius, const xVec3* pos_fromHere, F32 spd_grow);
void zNMEMsg_AreaNMEYellBoo(zNMECommon* who, F32 radius, const xVec3* pos_fromHere);
void zNMEMsg_AreaExplode(zNMECommon* who, F32 radius, const xVec3* pos_altOrigin, F32 spd_grow,
                         S32 skipNME);

void zNMEMsg_Startup();

#endif
