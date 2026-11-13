#include "zNMEMessenger.h"
#include "zNMECommon.h"
#include "xMemMgr.h"
#include "xMath.h"
#include "zNPCMgr.h"
#include "zGlobals.h"
#include "zGrid.h"

#include <string.h>

// Lasso tables (BfBB's zNPCTypeCommon.cpp has them). Nothing on GC uses them; they are defined
// here because retail's .data has them right before g_postoffice.
U32 g_hash_lassanim[3] = {};
char* g_strz_lassanim[3] = { "Unknown", "LassoGuide_Grab01", "LassoGuide_Hold01" };
char* g_strz_lassevents[6] = { "LASS_EVNT_BEGIN",   "LASS_EVNT_ENDED", "LASS_EVNT_GRABSTART",
                               "LASS_EVNT_GRABEND", "LASS_EVNT_YANK",  "LASS_EVNT_ABORT" };
char* g_strz_lassstages[4] = { "LASS_STAT_DONE", "LASS_STAT_PENDING", "LASS_STAT_GRABBING",
                               "LASS_STAT_TOSSING" };

static NMEPSData g_postoffice = {};

// The NMEPS_* helpers have no GC symbols (always inlined); BfBB's zNPCMessenger has the same
// helpers as static functions (NPCPS_*).
inline NMEPSData* NMEPS_postOffice()
{
    return &g_postoffice;
}

inline void NMEPS_CltPoolReset(NMEPSData* npcps)
{
    S32 max_clts = npcps->cltlist.max;

    memset(npcps->cltblob, 0, max_clts * sizeof(NMEPSClt));
    npcps->cltfree = NULL;
    for (S32 i = 0; i < max_clts; i++)
    {
        NMEPSClt* clt = &npcps->cltblob[i];

        clt->next = npcps->cltfree;
        npcps->cltfree = clt;
    }
    npcps->cltlist.cnt = 0;
}

inline void NMEPS_CltPoolInit(NMEPSData* npcps, S32 num)
{
    XOrdInit(&npcps->cltlist, num, 0);
    npcps->cltblob = (NMEPSClt*)xMemAlloc(gActiveHeap, num * sizeof(NMEPSClt), 0);
    memset(npcps->cltblob, 0, num * sizeof(NMEPSClt));
    NMEPS_CltPoolReset(npcps);
}

inline void NMEPS_MsgPoolReset(NMEPSData* npcps)
{
    npcps->quelist.cnt = 0;

    S32 max_msgs = npcps->quelist.max;

    memset(npcps->msgblob, 0, max_msgs * sizeof(NMEMsg));
    npcps->msgfree = NULL;
    for (S32 i = 0; i < max_msgs; i++)
    {
        NMEMsg* msg = &npcps->msgblob[i];

        msg->next = npcps->msgfree;
        npcps->msgfree = msg;
    }
}

inline void NMEPS_MsgPoolInit(NMEPSData* npcps, S32 num)
{
    XOrdInit(&npcps->quelist, num, 0);
    npcps->msgblob = (NMEMsg*)xMemAlloc(gActiveHeap, num * sizeof(NMEMsg), 0);
    memset(npcps->msgblob, 0, num * sizeof(NMEMsg));
    NMEPS_MsgPoolReset(npcps);
}

inline NMEMsg* NMEPS_grabMsg()
{
    NMEPSData* npcps = NMEPS_postOffice();
    NMEMsg* msg = npcps->msgfree;

    if (msg == NULL)
    {
        return NULL;
    }

    npcps->msgfree = msg->next;
    msg->next = NULL;
    return msg;
}

inline void NMEPS_copyMsgInfo(NMEMsg* dst, NMEMsg* src, F32 delay)
{
    memcpy(dst, src, sizeof(NMEMsg));
    dst->next = NULL;
    dst->tmr_delay = delay;
}

inline void NMEPS_queMessage(NMEMsg* msg)
{
    NMEPSData* npcps = NMEPS_postOffice();

    XOrdAppend(&npcps->quelist, msg);
}

// Every client currently hears every message.
inline S32 NMEPS_flagForMID(en_npcmsg msgid)
{
    return 0x7;
}

inline void NMEPS_freeMsg(NMEMsg* msg)
{
    NMEPSData* npcps = NMEPS_postOffice();

    msg->next = npcps->msgfree;
    npcps->msgfree = msg;
}

// No GC symbol: inlined into zNMECentral_Startup (its unused copy is dead-stripped).
void zNMEMsg_Startup()
{
    static zMODMessage mod_message;
    NMEPSData* npcps;

    zNMECentral_RegisterModule(&mod_message, 'NMSG');

    npcps = NMEPS_postOffice();
    NMEPS_CltPoolInit(npcps, 32);
    NMEPS_MsgPoolInit(npcps, 64);
}

void zMODMessage::SceneFinish()
{
    SceneReset();
}

void zMODMessage::SceneReset()
{
    NMEPSData* npcps = NMEPS_postOffice();

    NMEPS_CltPoolReset(npcps);
    NMEPS_MsgPoolReset(npcps);
}

void zMODMessage::Timestep(F32 dt)
{
    NMEPSData* npcps = NMEPS_postOffice();
    S32 i;
    NMEMsg* msg;

    for (i = npcps->quelist.cnt - 1; i >= 0; i--)
    {
        msg = (NMEMsg*)npcps->quelist.list[i];
        msg->tmr_delay = MAX(-1.0f, msg->tmr_delay - dt);

        if (msg->tmr_delay < 0.0f)
        {
            zNMEMsg_SendMsg(msg, NULL, -1.0f);
            XOrdRemove(&npcps->quelist, NULL, i);
            NMEPS_freeMsg(msg);
        }
    }
}

// No GC symbol (unused, dead-stripped), but its static msg survives in retail's .bss. BfBB has the
// same pair of overloads; the delay parameter is assumed to mirror the zNMECommon* one.
void zNMEMsg_SendMsg(en_npcmsg msgevent, U32 npc_id, F32 delay)
{
    static NMEMsg msg;

    memset(&msg, 0, sizeof(NMEMsg));
    msg.msgid = msgevent;
    msg.sendto = npc_id;
    zNMEMsg_SendMsg(&msg, NULL, -1.0f);
}

// The GC build added the delay parameter; the message is always sent immediately.
void zNMEMsg_SendMsg(en_npcmsg msgevent, zNMECommon* npc_sendto, F32 delay)
{
    static NMEMsg msg;

    memset(&msg, 0, sizeof(NMEMsg));
    msg.msgid = msgevent;
    msg.sendto = npc_sendto->id;
    zNMEMsg_SendMsg(&msg, npc_sendto, -1.0f);
}

void zNMEMsg_SendMsg(NMEMsg* inmsg, zNMECommon* npc_sendto, F32 delay)
{
    if (delay > 0.0f)
    {
        NMEMsg* msg = NMEPS_grabMsg();

        if (msg != NULL)
        {
            NMEPS_copyMsgInfo(msg, inmsg, delay);
            NMEPS_queMessage(msg);
        }
        else
        {
            zNMEMsg_SendMsg(inmsg, npc_sendto, -1.0f);
        }
    }
    else
    {
        if (npc_sendto == NULL)
        {
            st_XORDEREDARRAY* npclist = zNPCMgr_GetNPCList();
            S32 idx = XOrdLookup(npclist, (void*)inmsg->sendto, zNPCMgr_OrdTest_npcid);

            if (idx >= 0)
            {
                npc_sendto = (zNMECommon*)npclist->list[idx];
            }
        }

        if (npc_sendto != NULL)
        {
            npc_sendto->NMEMessage(inmsg);
        }

        NMEPSData* npcps = NMEPS_postOffice();

        for (S32 i = 0; i < npcps->cltlist.cnt; i++)
        {
            NMEPSClt* clt = (NMEPSClt*)npcps->cltlist.list[i];

            if (clt->flg_filter & NMEPS_flagForMID(inmsg->msgid))
            {
                clt->notify(clt->notedata, inmsg);
            }
        }
    }
}

// Declared here, not with g_postoffice, to follow retail's .sbss/.bss order.
static S32 g_lockarea;
static NMEMsg g_msgdata;

void zNMEMsg_AreaNotify(zNMECommon* sender, en_npcmsg msgid, F32 rad, S32 filter,
                        en_npctyp toNMEType)
{
    if (g_lockarea == 0)
    {
        g_lockarea = 1;

        en_npctyp toTypes[2] = { toNMEType, NME_TYPE_UNKNOWN };

        memset(&g_msgdata, 0, sizeof(NMEMsg));
        g_msgdata.from = (sender != NULL) ? sender->id : 'FAKE';
        g_msgdata.msgid = msgid;
        g_msgdata.sendto = 0;
        g_msgdata.infotype = NME_MDAT_BLANK;
        zNMEMsg_AreaNotify(sender, &g_msgdata, rad, filter, toTypes);

        g_lockarea = 0;
    }
}

void zNMEMsg_AreaNotify(zNMECommon* sender, NMEMsg* msg, F32 radius, S32 filter,
                        en_npctyp* npcTypeList)
{
    S32 i;
    F32 ds2_discard = SQ(radius);
    st_XORDEREDARRAY* npclist = zNPCMgr_GetNPCList();
    xVec3 pos_origin;

    if (filter & 0x100)
    {
        xVec3Copy(&pos_origin, xEntGetPos(xglobals->___player_ent_dont_use_directly));
    }
    else if (sender != NULL)
    {
        xVec3Copy(&pos_origin, xEntGetPos(sender));
    }
    else
    {
        xVec3Copy(&pos_origin, &g_O3);
    }

    if (msg->infotype == NME_MDAT_BLANK)
    {
        xVec3Copy(&msg->areadata.pos_origin, &pos_origin);
        msg->areadata.npc_origin = sender;
        msg->infotype = NME_MDAT_AREANOTIFY;
    }

    for (i = 0; i < npclist->cnt; i++)
    {
        zNMECommon* npc = (zNMECommon*)npclist->list[i];
        en_npctyp* typ;
        en_npctyp ntyp;

        if (!(filter & 0x800) && (npc->baseFlags & 0x40))
        {
            continue;
        }

        if (sender != NULL && !(filter & 0x8) && npc == sender)
        {
            continue;
        }

        ntyp = (en_npctyp)npc->itemType;

        if (sender != NULL && (filter & 0x200) && ntyp != sender->itemType)
        {
            continue;
        }

        if (npcTypeList != NULL && *npcTypeList != NME_TYPE_UNKNOWN)
        {
            typ = npcTypeList;
            S32 skipit = TRUE;

            while (*typ != NME_TYPE_UNKNOWN)
            {
                if (ntyp == *typ)
                {
                    skipit = FALSE;
                    break;
                }
                typ++;
            }

            if (skipit)
            {
                continue;
            }
        }

        if (!(filter & 0x10))
        {
            if (!(ntyp >= NME_TYPE_TIKI && ntyp <= NME_TYPE_TIKI_STONE) &&
                !(ntyp >= NME_TYPE_STANDARD && ntyp <= NME_TYPE_MERVYN_V3) &&
                !(ntyp >= NME_TYPE_MINDY && ntyp <= NME_TYPE_PATRICK))
            {
                continue;
            }

            if (ntyp >= NME_TYPE_TIKI && ntyp <= NME_TYPE_TIKI_STONE && !(filter & 0x1))
            {
                continue;
            }

            if (ntyp >= NME_TYPE_STANDARD && ntyp <= NME_TYPE_MERVYN_V3 && !(filter & 0x2))
            {
                continue;
            }

            if (ntyp >= NME_TYPE_MINDY && ntyp <= NME_TYPE_PATRICK && !(filter & 0x4))
            {
                continue;
            }
        }

        if (!(filter & 0x400) && !npc->IsAlive())
        {
            continue;
        }

        xVec3 diff;
        xVec3Sub(&diff, xEntGetPos(npc), &pos_origin);

        if (xabs(diff.y) > 2.0f)
        {
            continue;
        }

        diff.y = 0.0f;
        if (xVec3Length2(&diff) > ds2_discard)
        {
            continue;
        }

        msg->sendto = npc->id;
        npc->NMEMessage(msg);
    }
}

void zNMEMsg_AreaPlayerStun(F32 stuntime, F32 radius, xVec3* pos)
{
    st_XORDEREDARRAY* npclist = zNPCMgr_GetNPCList();
    xVec3 from;
    if (pos != NULL)
    {
        from = *pos;
    }
    else
    {
        from = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
    }
    F32 ds2_discard = SQ(radius);
    NMEMsg msg;

    memset(&msg, 0, sizeof(NMEMsg));
    msg.from = 0;
    msg.sendto = 0;
    msg.msgid = NME_MID_STUN;
    msg.infotype = NME_MDAT_STUN;
    msg.stundata.tym_stuntime = stuntime;

    for (S32 i = 0; i < npclist->cnt; i++)
    {
        zNMECommon* npc = (zNMECommon*)npclist->list[i];

        if (npc->baseFlags & 0x40)
        {
            continue;
        }

        if (!npc->runtimeData.flg_activeOn)
        {
            continue;
        }

        if (npc->model != NULL && (npc->model->Flags & 0x400))
        {
            continue;
        }

        if (!(npc->flg_vuln & 0x10000000))
        {
            continue;
        }

        en_npctyp ntyp = (en_npctyp)npc->itemType;

        if (!(ntyp >= NME_TYPE_STANDARD && ntyp <= NME_TYPE_MERVYN_V3) &&
            !(ntyp >= NME_TYPE_FROGFISH && ntyp <= NME_TYPE_NEPTUNE) && ntyp != NME_TYPE_TONGUESPIN)
        {
            continue;
        }

        xVec3 diff;
        npc->XYZVecToPos(&diff, &from);

        if (xabs(diff.y) > radius)
        {
            continue;
        }

        if (SQ(diff.x) + SQ(diff.z) > ds2_discard)
        {
            continue;
        }

        msg.sendto = npc->id;
        npc->NMEMessage(&msg);
    }
}

void zNMEMsg_AreaNMEExplode(zNMECommon* who, F32 radius, const xVec3* pos_fromHere, F32 spd_grow)
{
    st_XORDEREDARRAY* npclist = zNPCMgr_GetNPCList();
    xVec3 from = (pos_fromHere != NULL) ? *pos_fromHere : *xEntGetCenter(who);
    F32 ds2_discard = SQ(radius);
    NMEMsg msgtmp;

    if (spd_grow < 1e-5f)
    {
        spd_grow = -1.0f;
    }

    memset(&msgtmp, 0, sizeof(NMEMsg));
    msgtmp.from = (who != NULL) ? who->id : 0;
    msgtmp.sendto = 0;
    msgtmp.msgid = NME_MID_DAMAGE;
    msgtmp.infotype = NME_MDAT_DAMAGE;
    msgtmp.dmgdata.dmg_type = DMGTYP_EXPLOSION;
    msgtmp.dmgdata.dmg_from = who;
    msgtmp.dmgdata.amt_damage = -1;
    msgtmp.dmgdata.fac_powerup = 0;

    for (S32 i = 0; i < npclist->cnt; i++)
    {
        zNMECommon* npc = (zNMECommon*)npclist->list[i];

        if (npc == who)
        {
            continue;
        }

        if (!xEntIsVisible(npc))
        {
            continue;
        }

        if (npc->baseFlags & 0x40)
        {
            continue;
        }

        if (npc->model != NULL && (npc->model->Flags & 0x400))
        {
            continue;
        }

        if (!(npc->flg_vuln & 0x1))
        {
            continue;
        }

        if (!(npc->flg_vuln & 0x2))
        {
            continue;
        }

        xVec3 diff;
        F32 dst = npc->XYZDstSqToPos(&from, &diff);

        if (dst > ds2_discard)
        {
            continue;
        }

        if (!npc->IsAlive())
        {
            continue;
        }

        dst = xsqrt(dst);
        if (dst > 1e-5f)
        {
            diff *= 1.0f / dst;
        }
        else if (xabs(diff.x) > xabs(diff.z))
        {
            diff = (diff.x > 0.0f) ? g_X3 : g_NX3;
        }
        else
        {
            diff = (diff.z > 0.0f) ? g_Z3 : g_NZ3;
        }

        en_npctyp ntyp = (en_npctyp)npc->itemType;

        if (ntyp >= NME_TYPE_TIKI && ntyp <= NME_TYPE_TIKI_STONE)
        {
            npc->Damage(DMGTYP_EXPLOSION, NULL, &diff, -1, ePowerupLevelNormal);
            continue;
        }

        msgtmp.sendto = npc->id;
        msgtmp.dmgdata.vec_dmghit = diff;

        F32 tym_delay = -1.0f;
        if (dst > 0.0f && spd_grow > 0.0f)
        {
            tym_delay = dst / spd_grow;
        }

        zNMEMsg_SendMsg(&msgtmp, npc, tym_delay);
    }
}

void zNMEMsg_AreaNMEYellBoo(zNMECommon* who, F32 radius, const xVec3* pos_fromHere)
{
    static S32 amSpooking = 0;

    if (amSpooking)
    {
        return;
    }

    amSpooking = 1;

    st_XORDEREDARRAY* npclist = zNPCMgr_GetNPCList();
    xVec3 from = (pos_fromHere != NULL) ? *pos_fromHere : *xEntGetCenter(who);
    F32 ds2_discard = SQ(radius);

    for (S32 i = 0; i < npclist->cnt; i++)
    {
        zNMECommon* npc = (zNMECommon*)npclist->list[i];

        if (npc == who)
        {
            continue;
        }

        if (npc->baseFlags & 0x40)
        {
            continue;
        }

        if (!npc->runtimeData.flg_activeOn)
        {
            continue;
        }

        if (npc->model != NULL && (npc->model->Flags & 0x400))
        {
            continue;
        }

        en_npctyp ntyp = (en_npctyp)npc->itemType;

        if (!(ntyp >= NME_TYPE_STANDARD && ntyp <= NME_TYPE_MERVYN_V3))
        {
            continue;
        }

        xVec3 diff;
        if (npc->XYZDstSqToPos(&from, &diff) > ds2_discard)
        {
            continue;
        }

        npc->Boo(who);
    }

    amSpooking = 0;
}

// No symbol (always inlined; the name is ours): scans an eBaseTypeUnknown-terminated list of base
// types.
inline S32 NMEMsg_TypeInList(S32 typ, const en_ZBASETYPE* list)
{
    S32 found = 0;
    for (S32 i = 0; list[i] != eBaseTypeUnknown; i++)
    {
        if (typ == list[i])
        {
            found = 1;
            break;
        }
    }
    return found;
}

U8 ExplodeCollideData::operator()(xEnt& ent)
{
    xCollis colrec;

    if (&ent == npc_from)
    {
        return 1;
    }

    if (!(ent.chkby & 0x8))
    {
        return 1;
    }

    memset(&colrec, 0, sizeof(xCollis));
    if (ent.collLev == 5)
    {
        xBoundHitsModel(&bound, ent.model, &colrec, 1, PI / 4);
    }
    else
    {
        xBoundHitsBound(&bound, &ent.bound, &colrec);
    }

    if (!(colrec.flags & 0x1))
    {
        return 1;
    }

    static const en_ZBASETYPE typesWeCanHurt[] = {
        eBaseTypePlatform, eBaseTypeStatic,      eBaseTypeDynamic, eBaseTypePendulum,
        eBaseTypeHangable, eBaseTypeHangableObj, eBaseTypeButton,  eBaseTypeDestructObj,
        eBaseTypeNPC,      eBaseTypeBoulder,     eBaseTypeUnknown,
    };
    if (!NMEMsg_TypeInList(ent.baseType, typesWeCanHurt))
    {
        return 1;
    }

    switch (ent.baseType)
    {
    case eBaseTypeNPC:
    {
        zNMECommon* npc_hit = (zNMECommon*)&ent;
        NMEMsg* msg = &msgdata;
        F32 tym_toImpact = -1.0f;

        if (spd_explode > 1e-5f)
        {
            xVec3 vec_diff = *xEntGetPos(npc_hit) - bound.sph.center;
            F32 dst = vec_diff.length();

            if (dst > 0.1f)
            {
                tym_toImpact = dst / spd_explode;
                msg->dmgdata.vec_dmghit = vec_diff * (1.0f / dst);
            }
        }

        msg->sendto = npc_hit->id;
        zNMEMsg_SendMsg(msg, npc_hit, tym_toImpact);
        break;
    }
    default:
    {
        F32 fparam[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
        zEntEvent(npc_from, 0, &ent, eEventHit, fparam, NULL, 0, FE_NO);
        break;
    }
    }

    return 1;
}

// No GC symbol: inlined into the hazard types' Update functions (the PS2 build keeps it out of
// line).
void zNMEMsg_AreaExplode(zNMECommon* who, F32 radius, const xVec3* pos_altOrigin, F32 spd_grow,
                         S32 skipNME)
{
    ExplodeCollideData cbdata;

    memset(&cbdata, 0, sizeof(ExplodeCollideData));
    cbdata.npc_from = who;
    cbdata.spd_explode = spd_grow;
    cbdata.bound.type = XBOUND_TYPE_SPHERE;
    cbdata.bound.sph.r = radius;

    if (pos_altOrigin != NULL)
    {
        cbdata.bound.sph.center = *pos_altOrigin;
    }
    else
    {
        cbdata.bound.sph.center = *xEntGetPos(who);
    }

    xQuickCullForSphere(&xqc_def_ctrl, &cbdata.bound.qcd, &cbdata.bound.sph);

    if (!skipNME)
    {
        NMEMsg* msg = &cbdata.msgdata;

        memset(msg, 0, sizeof(NMEMsg));
        msg->from = (who != NULL) ? who->id : 0;
        msg->sendto = 0;
        msg->msgid = NME_MID_DAMAGE;
        msg->infotype = NME_MDAT_DAMAGE;
        msg->dmgdata.dmg_type = DMGTYP_EXPLOSION;
        msg->dmgdata.dmg_from = who;
        msg->dmgdata.amt_damage = -1;
        msg->dmgdata.fac_powerup = 0;

        xGridCheckBound(npcs_grid, cbdata.bound, cbdata.bound.qcd, cbdata);
    }

    xGridCheckBound(colls_grid, cbdata.bound, cbdata.bound.qcd, cbdata);
}
