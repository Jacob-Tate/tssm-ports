#include "zNMETypeTongueSpin.h"
#include "xDebug.h"

U32 gHashTongueSpinAnim[12];
char* gStrTongueSpinAnim[12] = {
    "Unknown", "Idle",     "Hit",      "Attack1Prep", "Attack1Loop", "Attack1Done",
    "Attack2", "StunPrep", "StunLoop", "StunDone",    "Up",          "Down",
};
iSndGroupHandle zNMETongueSpin::sSoundAsset[6];
xDecalEmitter gopherDecal;

PuffEmitter FFpuffTongue = {
    NPAR_TYP_DUSTPUFF,
    0.5f,
    { 0x96, 0x96, 0x96, 0x32 },
    { 1.0f, 2.0f },
    { 0.0f, -2.0f, 0.0f },
    7.2f,
    93,
    0,
    1,
    0,
    { 0.2f, 5.0f },
};

static F32 sTongueNextEmit;

static const xDecalEmitter::curve_node gopherCurve[3] = {
    { 0.0f, { 0x96, 0x96, 0x96, 0x32 }, 1.0f },
    { 0.9f, { 0x96, 0x96, 0x96, 0x32 }, 1.0f },
    { 1.0f, { 0x96, 0x96, 0x96, 0x00 }, 1.0f },
};

static const xFXRibbon::curve_node coneCurveData[5] = {
    { 0.0f, { 0xFF, 0x80, 0xC0, 0xFF }, 1.0f }, { 0.17f, { 0xFF, 0x80, 0xC0, 0xC8 }, 0.9f },
    { 0.6f, { 0xB9, 0xB9, 0x8F, 0x96 }, 0.3f }, { 0.75f, { 0xB9, 0xB9, 0x8F, 0x32 }, 0.0f },
    { 1.0f, { 0xB9, 0xB9, 0x8F, 0x32 }, 0.0f },
};

NMEHelpRib::RibData ribCone = {
    { 1, 1, 0, 0 },
    { "fx_spinner_swirl", (xFXRibbon::curve_node*)coneCurveData, 5, 0.35f, 0.6f },
    { 1, 1.0f, 1.0f },
    { NULL, 0 },
};

xAnimTable* zNMETongueSpin::CreateAnimTable(xAnimTable* table)
{
    if (!table)
    {
        table = xAnimTableNew("zNMETongueSpin", 0);
    }

    xAnimTableNewState(table, gStrTongueSpinAnim[1], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrTongueSpinAnim[2], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrTongueSpinAnim[3], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrTongueSpinAnim[4], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrTongueSpinAnim[5], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrTongueSpinAnim[6], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrTongueSpinAnim[7], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrTongueSpinAnim[8], 0x10, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrTongueSpinAnim[9], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrTongueSpinAnim[10], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, gStrTongueSpinAnim[11], 0, 1, 1.0f, NULL, NULL, 0.0f, NULL, NULL,
                       xAnimDefaultBeforeEnter, NULL, NULL);

    return table;
}

void zNMETongueSpin::SelfSetup()
{
}

void zNMETongueSpin::Init(xEntAsset* asset)
{
    zNMECommon::Init(asset);
    update = NULL;
    ribbon = NULL;
}

void zNMETongueSpin::Setup()
{
    zNPCMgr* npcmgr;
    char* nam_bossman;
    zNPC::base* nb;
    S32 i;

    zNMECommon::Setup();

    npcmgr = zNPCMgrSelf();
    nam_bossman = "FROG_FISH_NME";
    nb = npcmgr->FindNPC(xStrHash(nam_bossman));
    npc_bossman = (zNMEFrogfish*)nb;

    for (i = 0; i < 12; i++)
    {
        gHashTongueSpinAnim[i] = xStrHash(gStrTongueSpinAnim[i]);
    }

    SoundInitAssetTable();

    GopherDecalInit();
    ribCone.FindTexture();
}

void zNMETongueSpin::BUpdate(xVec3* pos)
{
    if (npc_bossman != NULL)
    {
        if (npc_bossman->attackCurr == 2)
        {
            bound.type = XBOUND_TYPE_SPHERE;
            bound.sph.center = *pos;
            bound.sph.r = 0.7f;
            bound.sph.center.y += bound.sph.r;
            xBoundUpdate(&bound);
            zGridUpdateEnt(this);
        }
        else
        {
            bound.type = XBOUND_TYPE_SPHERE;
            bound.sph.center = *pos;
            bound.sph.r = 1.0f;
            bound.sph.center.y += bound.sph.r;
            xBoundUpdate(&bound);
            zGridUpdateEnt(this);
        }
    }
}

// Retail stores -1 to burrowSndHandle twice after each xSndMgrStop, as in EnterState. oldMat is
// unused (as in the PS2 build).
void zNMETongueSpin::Process(F32 dt)
{
    xMat4x3 oldMat;
    F32 tym_animCurr;
    F32 tym_animDur;
    F32 tym_animPlusTime;
    xVec3 targetPos;
    xVec3 newPos;

    if (tmr_stunned >= 0.0f)
    {
        tmr_stunned = MAX(-1.0f, tmr_stunned - dt);
    }

    if (IsAlive())
    {
        ShadStufUpdate();
    }
    else if (simpShadow != NULL || entShadow != NULL)
    {
        ShadStufIAmDead();
    }

    oldMat = *(xMat4x3*)model->Mat;

    if (IsAlive())
    {
        zNMECommon::Process(dt);
    }
    else
    {
        zNMECommon::Process(dt);
        return;
    }

    tym_animCurr = AnimTimeCurrent();
    tym_animDur = AnimDuration(NULL);
    tym_animPlusTime = tym_animCurr + dt;

    if (anid_played == gHashTongueSpinAnim[11] && tym_animPlusTime > tym_animDur)
    {
        ShadStufIAmDead();
    }

    FXUpdate(dt, tym_animPlusTime, tym_animDur, tym_animCurr);

    switch (state)
    {
    case ezTONGUESPIN_STATE_HIDDEN:
    case ezTONGUESPIN_STATE_BURROW:
    case ezTONGUESPIN_STATE_POPDOWN:
    case ezTONGUESPIN_STATE_TAUNT:
    case ezTONGUESPIN_STATE_DEAD:
        break;
    case ezTONGUESPIN_STATE_POPUP:
        if (!locationChosen)
        {
            ChooseLocation();
        }

        if (!locationChosen)
        {
            popupTimer -= dt;
            if (popupTimer < 0.0f)
            {
                if (npc_bossman->camState == ezFFBOSS_CAMERA_STATE_PREPGENERATION &&
                    npc_bossman->camTimer < 0.0f)
                {
                    npc_bossman->SetCamState(ezFFBOSS_CAMERA_STATE_AFTERGENERATION);
                }
                else if (npc_bossman->camState == ezFFBOSS_CAMERA_STATE_AFTERGENERATION &&
                         npc_bossman->camTimer < 0.0f)
                {
                    npc_bossman->SetCamState(ezFFBOSS_CAMERA_STATE_TRANSTOREGULAR);
                }
                else if (npc_bossman->camState == ezFFBOSS_CAMERA_STATE_TRANSTOREGULAR)
                {
                    npc_bossman->SetCamState(ezFFBOSS_CAMERA_STATE_REGULAR);
                }
            }
        }
        else if (anid_played == gHashTongueSpinAnim[10])
        {
            if (tym_animPlusTime > tym_animDur)
            {
                if (npc_bossman->camState == ezFFBOSS_CAMERA_STATE_REGULAR)
                {
                    EnterState(ezTONGUESPIN_STATE_ATTACK);
                }
                else if (npc_bossman->camTimer < 0.0f)
                {
                    EnterState(ezTONGUESPIN_STATE_ATTACK);
                    npc_bossman->SetCamState(ezFFBOSS_CAMERA_STATE_REGULAR);
                }
            }
        }
        else
        {
            popupTimer -= dt;
            if (popupTimer < 0.0f)
            {
                if (npc_bossman->camState == ezFFBOSS_CAMERA_STATE_REGULAR)
                {
                    if (locationChosen)
                    {
                        PlayExplicitAnim(gHashTongueSpinAnim[10], 0);
                        xSndMgrStop(burrowSndHandle);
                        burrowSndHandle = (iSndHandle)-1;
                        burrowSndHandle = (iSndHandle)-1;
                        xSndMgrPlay(sSoundAsset[2], 2, NULL, NULL, this, NULL, NULL);
                    }
                }
                else if (npc_bossman->camState == ezFFBOSS_CAMERA_STATE_PREPGENERATION &&
                         npc_bossman->camTimer < 0.0f)
                {
                    if (locationChosen)
                    {
                        PlayExplicitAnim(gHashTongueSpinAnim[10], 0);
                        xSndMgrStop(burrowSndHandle);
                        burrowSndHandle = (iSndHandle)-1;
                        burrowSndHandle = (iSndHandle)-1;
                        xSndMgrPlay(sSoundAsset[2], 2, NULL, NULL, this, NULL, NULL);
                    }
                    npc_bossman->SetCamState(ezFFBOSS_CAMERA_STATE_AFTERGENERATION);
                }
                else if (npc_bossman->camState == ezFFBOSS_CAMERA_STATE_AFTERGENERATION &&
                         npc_bossman->camTimer < 0.0f)
                {
                    npc_bossman->SetCamState(ezFFBOSS_CAMERA_STATE_TRANSTOREGULAR);
                }
            }
        }
        break;
    case ezTONGUESPIN_STATE_ATTACK:
        if (anid_played == gHashTongueSpinAnim[3])
        {
            if (tym_animPlusTime > tym_animDur)
            {
                PlayExplicitAnim(gHashTongueSpinAnim[4], 0);
            }
        }
        else if (anid_played == gHashTongueSpinAnim[4])
        {
            if (ColChk_Player())
            {
                PlayExplicitAnim(gHashTongueSpinAnim[11], 0);
            }
            else
            {
                ThrottleAdjust(dt, 6.0f, 1.5f);
                MatFacePlayer(dt, PI / 2, 1);

                targetPos = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
                if (targetPos.y > 0.5f)
                {
                    targetPos.x += xrand_RandomFloatRange(-1.0f, 1.0f);
                    targetPos.z += xrand_RandomFloatRange(-1.0f, 1.0f);
                }
                targetPos.y = 0.0f;

                MatMoveToPos(&targetPos, dt, spd_throttle, 1, -1.0f);
            }
        }
        else if (anid_played == gHashTongueSpinAnim[5])
        {
            if (tym_animPlusTime > tym_animDur)
            {
                PlayExplicitAnim(gHashTongueSpinAnim[11], 0);
            }
        }
        else if (anid_played == gHashTongueSpinAnim[11])
        {
            if (tym_animPlusTime > tym_animDur)
            {
                if (npc_bossman->attackCurr == 2 && !flgHitPlayer)
                {
                    EnterState(ezTONGUESPIN_STATE_ATTACK_WAIT);
                }
                else
                {
                    EnterState(ezTONGUESPIN_STATE_TAUNT);
                    npc_bossman->EnterState(ezFFBOSS_STATE_TAUNT_HIDE);
                }
            }
        }
        else if (anid_played == gHashTongueSpinAnim[6])
        {
            // not a DWARF local: the current single is fetched once, before the time test
            xAnimSingle* single = AnimCurSingle();
            if (tym_animCurr > 0.73333335f && tym_animCurr < 1.1333333f)
            {
                single->CurrentSpeed = 2.0f;
            }
            else
            {
                single->CurrentSpeed = 1.0f;
            }

            DoCapsuleTests();

            if (tym_animPlusTime > tym_animDur)
            {
                PlayExplicitAnim(gHashTongueSpinAnim[11], 0);
            }
        }
        break;
    case ezTONGUESPIN_STATE_ATTACK_WAIT:
        attackTimer -= dt;
        if (attackTimer < 0.0f)
        {
            xSndMgrStop(burrowSndHandle);
            burrowSndHandle = (iSndHandle)-1;
            burrowSndHandle = (iSndHandle)-1;
            EnterState(ezTONGUESPIN_STATE_POPUP);
        }
        break;
    case ezTONGUESPIN_STATE_DAMAGE:
        if (anid_played == gHashTongueSpinAnim[2])
        {
            if (knockbackTimer >= 0.0f)
            {
                *(xVec3*)&model->Mat->pos += vel * dt;
                knockbackTimer -= dt;
            }

            if (tym_animPlusTime > tym_animDur)
            {
                newPos = *(xVec3*)&model->Mat->pos;
                newPos.y = -20.0f;
                MatPosSet(&newPos);
                EnterState(ezTONGUESPIN_STATE_HIDDEN);
            }
        }
        break;
    case ezTONGUESPIN_STATE_STUN:
        if (anid_played == gHashTongueSpinAnim[7])
        {
            if (tym_animPlusTime > tym_animDur)
            {
                PlayExplicitAnim(gHashTongueSpinAnim[8], 0);
            }
        }
        else if (anid_played == gHashTongueSpinAnim[8])
        {
            if (tmr_stunned <= 0.0f)
            {
                PlayExplicitAnim(gHashTongueSpinAnim[9], 0);
                npc_bossman->EnterState(ezFFBOSS_STATE_TONGUESTUNDONE);
            }
        }
        else if (anid_played == gHashTongueSpinAnim[9] && tym_animPlusTime > tym_animDur)
        {
            EnterState(ezTONGUESPIN_STATE_ATTACK);
        }
        break;
    }

    zNMECommon::Process(dt);
}

void zNMETongueSpin::Reset()
{
    zNMECommon::Reset();

    state = ezTONGUESPIN_STATE_HIDDEN;
    flg_vuln = DfltVulnFlags();
    tmr_stunned = 0.0f;

    if (ribbon != NULL)
    {
        zFXRibbonPoolDelete(ribbon);
        ribbon = NULL;
    }

    burrowSndHandle = (iSndHandle)-1;
    spinSndHandle = (iSndHandle)-1;
}

S32 zNMETongueSpin::DfltVulnFlags()
{
    return 0xD0000001;
}

// GC only: the PS2 demo has this code written out in EnterState and Process, so the local names
// are ours.
void zNMETongueSpin::ChooseLocation()
{
    xVec3 pos;
    bool nearPlayer = false;

    if (npc_bossman->attackCurr == 2)
    {
        nearPlayer = true;
    }

    if (PickRandomPointInArena(&pos, nearPlayer))
    {
        model->Mat->pos.x = pos.x;
        model->Mat->pos.y = pos.y;
        model->Mat->pos.z = pos.z;

        if (npc_bossman->attackCurr == 2)
        {
            xVec3 dir;
            F32 dy;
            F32 len;

            XZVecToPos(&dir, xEntGetPos(xglobals->___player_ent_dont_use_directly), &dy);
            len = xVec3Length(&dir);
            if (len > 0.00001f)
            {
                xMat3x3 mat;
                xVec3 at = -dir * (1.0f / len);

                xMat3x3LookVec(&mat, &at);
                *(xMat3x3*)model->Mat = mat;
            }
        }

        popupTimer = 1.0f;
        locationChosen = 1;
    }
}

void zNMETongueSpin::EnterState(eTongueSpinState newState)
{
    if (state == newState)
    {
        return;
    }

    if (state == ezTONGUESPIN_STATE_ATTACK && ribbon != NULL)
    {
        zFXRibbonPoolDelete(ribbon);
        ribbon = NULL;
    }

    state = newState;

    switch (state)
    {
    case ezTONGUESPIN_STATE_POPUP:
        gopherDecal.set_default_config();
        gopherDecal.cfg.blend_src = 5;
        gopherDecal.cfg.blend_dst = 6;
        gopherDecal.cfg.life_time = 5.0f;
        gopherDecal.set_curve(gopherCurve, 3);
        gopherDecal.set_texture("fx_gen_dust");
        gopherDecal.cfg.texture.uv[0].assign(0.0f, 0.0f);
        gopherDecal.cfg.texture.uv[1].assign(1.0f, 1.0f);
        gopherDecal.cfg.texture.rows = 2;
        gopherDecal.cfg.texture.cols = 2;
        gopherDecal.cfg.texture.mode = xDecalEmitter::TM_RANDOM;
        gopherDecal.refresh_config();

        locationChosen = 0;
        ChooseLocation();

        if (npc_bossman->camState != ezFFBOSS_CAMERA_STATE_REGULAR)
        {
            npc_bossman->SetCamState(ezFFBOSS_CAMERA_STATE_PREPGENERATION);
        }

        if (burrowSndHandle == (iSndHandle)-1)
        {
            burrowSndHandle = xSndMgrPlay(sSoundAsset[1], 2, NULL, NULL, this, NULL, NULL);
        }
        break;
    case ezTONGUESPIN_STATE_ATTACK:
        if (npc_bossman->attackCurr == 1)
        {
            PlayExplicitAnim(gHashTongueSpinAnim[3], 0);

            RibbonStart();

            spinSndHandle = xSndMgrPlay(sSoundAsset[3], 2, NULL, NULL, this, NULL, NULL);
        }
        else
        {
            PlayExplicitAnim(gHashTongueSpinAnim[6], 0);

            RibbonStart();

            flgHitPlayer = 0;
            attackSoundPlayed = 0;
        }
        break;
    case ezTONGUESPIN_STATE_ATTACK_WAIT:
        burrowSndHandle = xSndMgrPlay(sSoundAsset[1], 2, NULL, NULL, this, NULL, NULL);
        attackTimer = 0.3f;
        break;
    case ezTONGUESPIN_STATE_BURROW:
        burrowSndHandle = xSndMgrPlay(sSoundAsset[1], 2, NULL, NULL, this, NULL, NULL);
        break;
    case ezTONGUESPIN_STATE_POPDOWN:
        PlayExplicitAnim(gHashTongueSpinAnim[11], 0);
        break;
    case ezTONGUESPIN_STATE_DAMAGE:
        xSndMgrPlay(sSoundAsset[5], 2, NULL, NULL, this, NULL, NULL);
        PlayExplicitAnim(gHashTongueSpinAnim[2], 0);
        npc_bossman->EnterState(ezFFBOSS_STATE_TONGUEDAMAGE);
        break;
    case ezTONGUESPIN_STATE_STUN:
        PlayExplicitAnim(gHashTongueSpinAnim[7], 0);
        break;
    case ezTONGUESPIN_STATE_DEAD:
        // retail's jump table covers DEAD (10 entries), so the switch names it
        break;
    }

    if (spinSndHandle != (iSndHandle)-1 && state != ezTONGUESPIN_STATE_ATTACK)
    {
        xSndMgrStop(spinSndHandle);
        // retail stores -1 twice here and after every other xSndMgrStop in this file
        spinSndHandle = (iSndHandle)-1;
        spinSndHandle = (iSndHandle)-1;
    }

    if (burrowSndHandle != (iSndHandle)-1 && state != ezTONGUESPIN_STATE_ATTACK_WAIT &&
        state != ezTONGUESPIN_STATE_BURROW && state != ezTONGUESPIN_STATE_POPUP)
    {
        xSndMgrStop(burrowSndHandle);
        burrowSndHandle = (iSndHandle)-1;
        burrowSndHandle = (iSndHandle)-1;
    }
}

// No GC symbol (always inlined); the name and signature are ours. Returns the distance from 'from'
// along 'dir' to the far edge of the circle of radius 'radius' around 'to'.
static inline F32 ArenaEdgeDist(const xVec3& from, const xVec3& to, const xVec3& dir, F32 radius)
{
    xVec3 vec = to - from;
    xVec3 axis = dir * radius;

    return radius + vec.dot(axis) / axis.length();
}

U8 zNMETongueSpin::PickRandomPointInArena(xVec3* pos, bool fNearPlayer)
{
    xVec3 center;
    center.x = npc_bossman->cfg->arena.centerX;
    center.y = 0.0f;
    center.z = npc_bossman->cfg->arena.centerZ;

    xVec3 player = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
    F32 radius = npc_bossman->cfg->arena.innerRadius;
    xVec3 plyrToFF = *xEntGetPos(npc_bossman) - player;

    if (!fNearPlayer)
    {
        F32 deltax = player.x - center.x;
        F32 deltaz = player.z - center.z;

        if (deltax * deltax + deltaz * deltaz >= radius * radius)
        {
            fNearPlayer = true;
        }
    }

    if (fNearPlayer)
    {
        F32 angle = xAngleClampFast(atan2(plyrToFF.x, plyrToFF.z));

        angle += xrand_RandomFloatRange(-PI / 4, PI / 4);
        pos->x = 3.5f * isin(angle) + player.x;
        pos->y = 0.0f;
        pos->z = 3.5f * icos(angle) + player.z;

        xVec3 tongueToCenter = *pos - center;
        if (!(tongueToCenter.x * tongueToCenter.x + tongueToCenter.z * tongueToCenter.z <
              radius * radius))
        {
            return false;
        }
    }
    else
    {
        F32 angle = xAngleClampFast(atan2(plyrToFF.x, plyrToFF.z));
        xVec3 dir;
        F32 dist;

        angle += xrand_RandomFloatRange(-PI / 6, PI / 6);
        dir.x = isin(angle);
        dir.y = 0.0f;
        dir.z = icos(angle);

        dist = ArenaEdgeDist(player, center, dir, radius);
        if (10.0f < dist)
        {
            dist = xrand_RandomFloatRange(10.0f, dist);
        }

        pos->x = dir.x * dist + player.x;
        pos->y = 0.0f;
        pos->z = dir.z * dist + player.z;
    }

    return true;
}

// No GC symbol: auto-inlined into its only caller (TongueSpinMailDamage) and dead-stripped. 'dir'
// is our name.
void zNMETongueSpin::KnockMeBack()
{
    xVec3 dir;

    dir.x = model->Mat->pos.x - xglobals->___player_ent_dont_use_directly->model->Mat->pos.x;
    dir.y = 0.0f;
    dir.z = model->Mat->pos.z - xglobals->___player_ent_dont_use_directly->model->Mat->pos.z;
    if (!xfeq0(dir.length2()))
    {
        dir.normalize();
    }
    else
    {
        dir.x = 0.5f;
        dir.z = 0.5f;
        dir.normalize();
    }

    dir *= 5.0f;
    vel.x = dir.x;
    vel.y = 0.0f;
    vel.z = dir.z;
    knockbackTimer = 1.0f;
}

// No GC symbol: auto-inlined into its only caller (ColChk_Player) and dead-stripped.
S32 zNMETongueSpin::ColChk_PlayerJump()
{
    xVec3 pos_nme = bound.sph.center;
    xVec3 pos_plyr = xglobals->___player_ent_dont_use_directly->bound.sph.center;
    xVec3 vec_NtoB = pos_plyr - pos_nme;

    if (vec_NtoB.length2() < 0.00001f)
    {
        return 0;
    }

    if (vec_NtoB.length2() > SQR(1.0f + xglobals->___player_ent_dont_use_directly->bound.sph.r))
    {
        return 0;
    }

    static U32 hashes_pa[6] = {
        xStrHash("StunJump"),   xStrHash("StunJump02"), xStrHash("StunFall"),
        xStrHash("StunFall02"), xStrHash("StunLand"),   xStrHash("StunLand02"),
    };

    U32 anid_player = xglobals->___player_ent_dont_use_directly->model->Anim->Single->State->ID;
    S32 found = 0;
    for (S32 k = 0; k < 6; k++)
    {
        if (anid_player == hashes_pa[k])
        {
            found = 1;
            break;
        }
    }

    if (found)
    {
        return 0;
    }

    zEntPlayer_DamageNPCKnockBack(this, 1, (xVec3*)&model->Mat->pos, ePlayerNMEDamageDefault);
    return 1;
}

S32 zNMETongueSpin::ColChk_Player()
{
    if (ColChk_PlayerJump())
    {
        return 1;
    }

    cnt_skipPlyrChecks--;
    if (cnt_skipPlyrChecks > 0)
    {
        return 0;
    }

    cnt_skipPlyrChecks = 3;

    xVec3 pos_nme = *(xVec3*)&model->Mat->pos;
    F32 upper = 0.55f + pos_nme.y;
    xVec3 pos_plyr = *xEntGetPos(xglobals->___player_ent_dont_use_directly);
    if (pos_plyr.y < pos_nme.y)
    {
        return 0;
    }

    if (pos_plyr.y > upper)
    {
        return 0;
    }

    xVec3 vec_NtoB = pos_plyr - pos_nme;
    if (vec_NtoB.length2() < 0.00001f)
    {
        return 0;
    }

    if (vec_NtoB.length2() > 4.84f)
    {
        return 0;
    }

    zEntPlayer_DamageNPCKnockBack(this, 1, (xVec3*)&model->Mat->pos, ePlayerNMEDamageDefault);
    return 1;
}

S32 zNMETongueSpin::TypeHandleMail(NMEMsg* mail)
{
    S32 handled;

    switch (mail->msgid)
    {
    case NME_MID_STUN:
        TongueStun(&mail->stundata);
        handled = 1;
        break;
    case NME_MID_DAMAGE:
        handled = TongueSpinMailDamage(&mail->dmgdata);
        break;
    case NME_MID_SYSEVENT:
        handled = DfltSysEventMail(&mail->sysevent);
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

S32 zNMETongueSpin::TongueSpinMailDamage(NMEDamageInfo* dmgmail)
{
    S32 handled = 1;
    en_plyrpup pup;
    S32 amt_dmg;

    if ((dmgmail->dmg_type == DMGTYP_CARTWHEEL || dmgmail->dmg_type == DMGTYP_SIDE) &&
        state == ezTONGUESPIN_STATE_ATTACK && anid_played == gHashTongueSpinAnim[4])
    {
        return 1;
    }

    switch (dmgmail->dmg_type)
    {
    case DMGTYP_ABOVE:
    case DMGTYP_BELOW:
    case DMGTYP_SIDE:
    case DMGTYP_INSTAKILL:
    case DMGTYP_WOUNDEVENT:
    case DMGTYP_KILLEVENT:
    case DMGTYP_NMEATTACK:
    case DMGTYP_HITBYTOSS:
    case DMGTYP_NMEKNOCKED:
    case DMGTYP_ROPE:
    case DMGTYP_CRUISEBUBBLE:
    case DMGTYP_REFLECTED:
    case DMGTYP_BOULDER:
    case DMGTYP_BUBBOWL:
    case DMGTYP_THUNDER_TIKI_EXPLOSION:
    case DMGTYP_DAMAGE_SURFACE:
    case DMGTYP_BUNGEED:
    case DMGTYP_SURFACE:
    case DMGTYP_CARTWHEEL:
    case DMGTYP_SLIDE:
    case DMGTYP_EXPLOSION:
    case DMGTYP_BBOWLZAP:
        amt_dmg = NMEC_DmgAmtForAttack(dmgmail->dmg_type, &pup);
        if (state != ezTONGUESPIN_STATE_DAMAGE)
        {
            npc_bossman->TakeDamage(amt_dmg);

            if (amt_dmg > 0)
            {
                KnockMeBack();
            }
        }
        EnterState(ezTONGUESPIN_STATE_DAMAGE);
        break;
    case DMGTYP_FRIENDLYFIRE:
        break;
    default:
        handled = 0;
        break;
    }

    return handled;
}

// Compiled-out asserts (conditions unknown). With them the function is too big to be
// auto-inlined into Process; retail calls it.
void zNMETongueSpin::DoCapsuleTests()
{
    xVec3 pos_head;
    xVec3 pos_hand;
    xSweptSphere sws;

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    pos_head = *(xVec3*)&model->Mat[20].pos;
    pos_hand = *(xVec3*)&model->Mat[17].pos;
    xMat3x3RMulVec(&pos_head, (xMat3x3*)model->Mat, &pos_head);
    xMat3x3RMulVec(&pos_hand, (xMat3x3*)model->Mat, &pos_hand);
    xVec3AddTo(&pos_head, (xVec3*)&model->Mat->pos);
    xVec3AddTo(&pos_hand, (xVec3*)&model->Mat->pos);

    memset(&sws, 0, sizeof(xSweptSphere));
    xSweptSpherePrepare(&sws, &pos_hand, &pos_head, 0.75f);

    xASSERT_PLACEHOLDER();

    if (!flgHitPlayer &&
        xSweptSphereToSphere(&sws, &xglobals->___player_ent_dont_use_directly->bound.sph))
    {
        zEntPlayer_DamageNPCKnockBack(this, 1, &bound.sph.center, ePlayerNMEDamageDefault);
        flgHitPlayer = 1;
    }
}

// No GC symbol (auto-inlined into Setup and dead-stripped); the name is ours. Defined after
// ColChk_Player, as retail's string pool has "Gopher" before ColChk_Player's names.
void zNMETongueSpin::GopherDecalInit()
{
    gopherDecal.init(20, "Gopher");
    gopherDecal.set_default_config();
    gopherDecal.cfg.blend_src = 5;
    gopherDecal.cfg.blend_dst = 6;
    gopherDecal.cfg.life_time = 5.0f;
    gopherDecal.set_curve(gopherCurve, 3);
    gopherDecal.set_texture("fx_gen_dust");
    gopherDecal.cfg.texture.uv[0].assign(0.0f, 0.0f);
    gopherDecal.cfg.texture.uv[1].assign(1.0f, 1.0f);
    gopherDecal.cfg.texture.rows = 2;
    gopherDecal.cfg.texture.cols = 2;
    gopherDecal.cfg.texture.mode = xDecalEmitter::TM_RANDOM;
    gopherDecal.refresh_config();
}

// No GC symbol (auto-inlined into Setup and dead-stripped). Defined after ColChk_Player for the
// string pool order, like GopherDecalInit.
void zNMETongueSpin::SoundInitAssetTable()
{
    // Retail clears only the first handle (the size of one entry); all six are assigned below.
    memset(sSoundAsset, 0, sizeof(iSndGroupHandle));
    sSoundAsset[0] = xSndMgrGetSoundGroup(xStrHash("TongueConeReleaseGroup"));
    sSoundAsset[1] = xSndMgrGetSoundGroup(xStrHash("TongueBurrowGroup"));
    sSoundAsset[2] = xSndMgrGetSoundGroup(xStrHash("TongueUpGroup"));
    sSoundAsset[3] = xSndMgrGetSoundGroup(xStrHash("TongueSpinLoopGroup"));
    sSoundAsset[4] = xSndMgrGetSoundGroup(xStrHash("TongueSlamGroup"));
    sSoundAsset[5] = xSndMgrGetSoundGroup(xStrHash("TongueHurtGroup"));
}

void zNMETongueSpin::RibbonLoop()
{
    NMEHelpRib::RibData* ribhelp;
    xVec3 pos1;
    xVec3 pos2;

    if (ribbon == NULL)
    {
        return;
    }

    cntSkipRibbonJoint--;
    if (cntSkipRibbonJoint > 0)
    {
        return;
    }

    ribhelp = &ribCone;
    cntSkipRibbonJoint = ribhelp->updinfo.frq_ribJoint;

    xMat4x3* mat_root = (xMat4x3*)model->Mat;
    pos1 = mat_root[17].pos;
    pos2 = mat_root[20].pos;
    xMat4x3Toworld(&pos1, mat_root, &pos1);
    xMat4x3Toworld(&pos2, mat_root, &pos2);

    xVec3 dir_emit = pos1 - pos2;
    F32 dst = xVec3Normalize(&dir_emit, &dir_emit);
    ribbon->insert(pos2, dir_emit, dst * ribhelp->updinfo.scl_master, ribhelp->updinfo.alf_master,
                   0);
}

void zNMETongueSpin::FXUpdate(F32 dt, F32 tym_animPlusTime, F32 tym_animDur, F32 tym_animCurr)
{
    xSphere bubSphere;
    xMat4x3* mat_root;
    xVec3 pos_head;

    if (npc_bossman->attackCurr == 0)
    {
        return;
    }

    if (npc_bossman->attackCurr == 1)
    {
        if (state == ezTONGUESPIN_STATE_ATTACK && anid_played == gHashTongueSpinAnim[4])
        {
            cntBubemit--;
            if (cntBubemit <= 0)
            {
                cntBubemit = 1;
                mat_root = (xMat4x3*)model->Mat;
                // An unnamed temporary summed in place (operator+ would copy into a second one).
                zFX_SpawnBubbleTrail(&(xVec3(mat_root[20].pos) += mat_root->pos), 1);
            }
            RibbonLoop();
        }
    }
    else if (npc_bossman->attackCurr == 2)
    {
        if (state == ezTONGUESPIN_STATE_ATTACK && anid_played == gHashTongueSpinAnim[6])
        {
            if (tym_animPlusTime > 0.73333335f && tym_animCurr <= 1.1333333f)
            {
                RibbonLoop();
            }

            if (tym_animPlusTime > 0.8f && !attackSoundPlayed)
            {
                attackSoundPlayed = 1;
                xSndMgrPlay(sSoundAsset[4], 2, NULL, NULL, this, NULL, NULL);
            }
            else if (tym_animPlusTime > 1.8f && tym_animCurr <= 1.8f)
            {
                mat_root = (xMat4x3*)model->Mat;
                bubSphere.center = mat_root[20].pos;
                bubSphere.r = 1.0f;
                xMat4x3Toworld(&bubSphere.center, mat_root, &bubSphere.center);
                zFX_SpawnBubblesOnSphere(30, &bubSphere, 3.0f);
                xSndMgrPlay(sSoundAsset[0], 2, NULL, NULL, this, NULL, NULL);

                mat_root = (xMat4x3*)model->Mat;
                pos_head = mat_root[20].pos;
                xMat4x3Toworld(&pos_head, mat_root, &pos_head);
                pos_head.y = 0.0f;
                MakeIceCreamSplat(&pos_head);
            }
        }
    }

    if (state == ezTONGUESPIN_STATE_POPUP ||
        (state == ezTONGUESPIN_STATE_ATTACK && anid_played == gHashTongueSpinAnim[4]) ||
        state == ezTONGUESPIN_STATE_DAMAGE || state == ezTONGUESPIN_STATE_POPDOWN ||
        (anid_played == gHashTongueSpinAnim[2] && tym_animPlusTime < tym_animDur))
    {
        xVec3 posEmit = *(xVec3*)&model->Mat->pos;
        if (posEmit.y < 0.0f)
        {
            posEmit.y = 0.0f;
        }

        xVec3 dirEmit = { 0.0f, 0.5f, 0.0f };
        dirEmit.x = 4.0f * xurand() + -2.0f;
        dirEmit.z = 4.0f * xurand() + -2.0f;
        FFpuffTongue.Footstroke(&posEmit, &dirEmit);

        sTongueNextEmit -= dt;
        if (sTongueNextEmit > 0.0f)
        {
            return;
        }

        xVec3 decalScale = { 2.0f, 2.0f, 2.0f };
        xMat4x3 gopherMat;
        xRay3 ray;
        xCollis coll;

        xMat3x3RotX(&gopherMat, 1.5707964f);
        xVec3Copy(&gopherMat.pos, &g_O3);
        gopherMat.pos.x = model->Mat->pos.x;
        gopherMat.pos.y = 0.0f;
        gopherMat.pos.z = model->Mat->pos.z;

        ray.dir.x = 0.0f;
        ray.dir.y = -1.0f;
        ray.dir.z = 0.0f;
        ray.origin = gopherMat.pos;
        ray.origin.y += 2.0f;
        ray.min_t = 0.0f;
        ray.max_t = 4.0f;
        xRayHitsTikiLandableScene(globals.sceneCur, &ray, &coll);

        gopherMat.pos.y = 2.0f - coll.dist + 0.1f;
        gopherDecal.emit(gopherMat, decalScale, -1);
        sTongueNextEmit = 0.15f;
    }
}

// No GC symbol: auto-inlined into its only caller FXUpdate (the out-of-line copy is dead-stripped).
S32 zNMETongueSpin::MakeIceCreamSplat(xVec3* splatPos)
{
    zNMEHazard* haz;
    HAZTypical* typical;

    haz = HAZ_Acquire();
    if (haz == NULL)
    {
        return 0;
    }

    if (haz->ConfigHelper(HAZ_TYP_ICECREAMSPLAT))
    {
        haz->npc_owner = this;
    }
    else
    {
        haz->Discard();
        return 0;
    }

    typical = &haz->custdata.typical;
    typical->rad_min = 0.1f;
    typical->rad_max = 0.4f;
    typical->rad_cur = typical->rad_min;
    haz->flg_hazard &= ~0xC002000;
    typical->vel.x = typical->vel.y = typical->vel.z = 0.0f;
    splatPos->y = 0.3f;
    haz->Start(splatPos, 4.0f);

    return 1;
}
