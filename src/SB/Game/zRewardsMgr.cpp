#include "zRewardsMgr.h"
#include "zRewards.h"
#include "zGlobals.h"
#include "zPlayerSB.h"
#include "zEntPickup.h"

#include "xHudMeter.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EnemyRewardsTable zRewardsMgr::manlinessPointRewards[38] = {
    { NME_TYPE_TIKI_WOOD },    { NME_TYPE_TIKI_LOVEY },   { NME_TYPE_TIKI_QUIET },
    { NME_TYPE_TIKI_THUNDER }, { NME_TYPE_TIKI_STONE },   { NME_TYPE_CRITJELLY },
    { NME_TYPE_TURBARREL_V1 }, { NME_TYPE_TURBARREL_V2 }, { NME_TYPE_TURBARREL_V3 },
    { NME_TYPE_FOGGER_V1 },    { NME_TYPE_FOGGER_V2 },    { NME_TYPE_FOGGER_V3 },
    { NME_TYPE_SLAMMER_V1 },   { NME_TYPE_SLAMMER_V2 },   { NME_TYPE_SLAMMER_V3 },
    { NME_TYPE_FLINGER_V1 },   { NME_TYPE_FLINGER_V2 },   { NME_TYPE_FLINGER_V3 },
    { NME_TYPE_SPINNER_V1 },   { NME_TYPE_SPINNER_V2 },   { NME_TYPE_SPINNER_V3 },
    { NME_TYPE_POPPER_V1 },    { NME_TYPE_POPPER_V2 },    { NME_TYPE_POPPER_V3 },
    { NME_TYPE_MERVYN_V1 },    { NME_TYPE_MERVYN_V2 },    { NME_TYPE_MERVYN_V3 },
    { NME_TYPE_BUCKOTRON_V1 }, { NME_TYPE_BUCKOTRON_V2 }, { NME_TYPE_BUCKOTRON_V3 },
    { NME_TYPE_BUCKOTRON_V4 }, { NME_TYPE_BUCKOTRON_V5 }, { NME_TYPE_BUCKOTRON_V6 },
    { NME_TYPE_BUCKOTRON_V7 }, { NME_TYPE_BUCKETJELLY },  { NME_TYPE_ZAPPER_V1 },
    { NME_TYPE_ZAPPER_V2 },    { NME_TYPE_ZAPPER_V3 },
};

EnemyRewardsTable zRewardsMgr::comboManlinessPoints[16];

ThresholdPointsRange zRewardsMgr::thresholdPoints[40] = {
    { 0, 0, 1000 },      { 0, 1001, 2000 },   { 0, 2001, 3000 },   { 0, 3001, 4000 },
    { 0, 4001, 5000 },   { 0, 5001, 6000 },   { 0, 6001, 7000 },   { 0, 7001, 8000 },
    { 0, 8001, 9000 },   { 0, 9001, 10000 },  { 0, 10001, 11000 }, { 0, 11001, 12000 },
    { 0, 12001, 13000 }, { 0, 13001, 14000 }, { 0, 14001, 15000 }, { 0, 15001, 16000 },
};

U32 zRewardsMgr::upgradeSlotsPoints[14];

void zRewardsMgrStartup()
{
    static zRewardsMgr rewardsMgr;

    globals.player.rewardsMgr = &rewardsMgr;
    globals.player.rewardsMgr->comboTime = globals.player.g.ComboTimer;
}

static inline U32 GetLowerThreshold()
{
    for (S32 i = 0; i < 40; i++)
    {
        if (globals.sceneCur->sceneID == zRewardsMgr::thresholdPoints[i].sceneID)
        {
            return zRewardsMgr::thresholdPoints[i].lower;
        }
    }

    return 0;
}

static inline U32 GetUpperThreshold()
{
    for (S32 i = 0; i < 40; i++)
    {
        if (globals.sceneCur->sceneID == zRewardsMgr::thresholdPoints[i].sceneID)
        {
            return zRewardsMgr::thresholdPoints[i].upper;
        }
    }

    return 99999;
}

static inline U32 GetEnemyManlinessPoints(en_npctyp enemy)
{
    U32 lower = GetLowerThreshold();
    U32 upper = GetUpperThreshold();

    for (S32 i = 0; i < 38; i++)
    {
        if (enemy == zRewardsMgr::manlinessPointRewards[i].enemy)
        {
            if (globals.player.InvManlinessPoints >= lower &&
                globals.player.InvManlinessPoints <= upper)
            {
                return zRewardsMgr::manlinessPointRewards[i].normalValue;
            }

            if (globals.player.InvManlinessPoints < lower)
            {
                return zRewardsMgr::manlinessPointRewards[i].lowerValue;
            }

            if (globals.player.InvManlinessPoints > upper)
            {
                return zRewardsMgr::manlinessPointRewards[i].upperValue;
            }
        }
    }

    return 0;
}

static inline bool IsTiki(en_npctyp enemy)
{
    return enemy == NME_TYPE_TIKI_WOOD || enemy == NME_TYPE_TIKI_LOVEY ||
           enemy == NME_TYPE_TIKI_QUIET || enemy == NME_TYPE_TIKI_THUNDER ||
           enemy == NME_TYPE_TIKI_STONE;
}

void zRewardsMgr::GiveReward(en_npctyp enemy, const xVec3& fromPos)
{
    S32 i;

    if (playerDead)
    {
        IncManlinessPoints(GetEnemyManlinessPoints(enemy), false);
        return;
    }

    for (i = 0; i < 20; i++)
    {
        if (!rewardsQ[i].active)
        {
            rewardsQ[i].active = TRUE;
            rewardsQ[i].numRewards = GetEnemyManlinessPoints(enemy);
            rewardsQ[i].spawnPos = fromPos;

            if (comboTimer < 0.0f)
            {
                comboTimer = comboTime;
            }

            if (comboTimer > 0.0f)
            {
                if (IsTiki(enemy))
                {
                    numCratesDestroyed++;
                }
                else
                {
                    numEnemiesDefeated++;
                }
            }
            else
            {
                numCratesDestroyed = 0;
                numEnemiesDefeated = 0;
            }

            return;
        }
    }
}

U32 zRewardsMgr::GetComboManlinessPoints()
{
    S32 comboIndex = numCratesDestroyed + numEnemiesDefeated * 3 - 1;

    lastComboIndex = comboIndex < 0 ? 0 : comboIndex;

    if (comboIndex <= 2)
    {
        return 0;
    }

    if (comboIndex >= 16)
    {
        comboIndex = 15;
        lastComboIndex = comboIndex;
    }

    U32 lower = GetLowerThreshold();
    U32 upper = GetUpperThreshold();

    if (globals.player.InvManlinessPoints >= lower && globals.player.InvManlinessPoints <= upper)
    {
        return comboManlinessPoints[comboIndex].normalValue;
    }

    if (globals.player.InvManlinessPoints < lower)
    {
        return comboManlinessPoints[comboIndex].lowerValue;
    }

    if (globals.player.InvManlinessPoints > upper)
    {
        return comboManlinessPoints[comboIndex].upperValue;
    }

    return 0;
}

void zRewardsMgr::Update(F32 dt)
{
    S32 i;
    S32 j;

    if (playerDead)
    {
        return;
    }

    if (spawnTimer >= 0.0f)
    {
        spawnTimer -= dt;
    }

    if (spawnTimer < 0.0f)
    {
        for (i = 0; i < 5; i++)
        {
            for (j = 0; j < 20; j++)
            {
                if (enemyManlinessPoints[i][j]->state & 0x40)
                {
                    zEntPickup_SetState(enemyManlinessPoints[i][j], 0x4);
                    xEntShow(enemyManlinessPoints[i][j]);
                    spawnTimer = 0.1f;
                    break;
                }
            }
        }
    }

    if (comboTimer >= 0.0f)
    {
        comboTimer -= dt;

        if (comboTimer < 0.0f)
        {
            lastComboManlinessPoints = GetComboManlinessPoints();
            IncManlinessPoints(lastComboManlinessPoints, false);
            comboTimer = -1.0f;
            numCratesDestroyed = 0;
            numEnemiesDefeated = 0;
        }

        for (i = 0; i < 20; i++)
        {
            if (rewardsQ[i].active)
            {
                SpawnRewards(rewardsQ[i].numRewards, rewardsQ[i].spawnPos);
                rewardsQ[i].active = FALSE;
                return;
            }
        }
    }
}

void zRewardsMgr::SpawnRewards(S32 numRewards, const xVec3& fromPos)
{
    S32 i;
    S32 j;
    S32 numPickupsToSpawn;
    S32 manlinessOfEachType[5];
    zEntPickup* p;

    if (numRewards < 1)
    {
        return;
    }

    memset(manlinessOfEachType, 0, sizeof(manlinessOfEachType));
    numPickupsToSpawn = numRewards;

    while (numPickupsToSpawn > 0)
    {
        if (numPickupsToSpawn >= globals.player.g.ShinyValuePurple)
        {
            numPickupsToSpawn -= globals.player.g.ShinyValuePurple;
            manlinessOfEachType[4]++;
        }
        else if (numPickupsToSpawn >= globals.player.g.ShinyValueBlue)
        {
            numPickupsToSpawn -= globals.player.g.ShinyValueBlue;
            manlinessOfEachType[3]++;
        }
        else if (numPickupsToSpawn >= globals.player.g.ShinyValueGreen)
        {
            numPickupsToSpawn -= globals.player.g.ShinyValueGreen;
            manlinessOfEachType[2]++;
        }
        else if (numPickupsToSpawn >= globals.player.g.ShinyValueYellow)
        {
            numPickupsToSpawn -= globals.player.g.ShinyValueYellow;
            manlinessOfEachType[1]++;
        }
        else if (numPickupsToSpawn >= globals.player.g.ShinyValueRed)
        {
            numPickupsToSpawn -= globals.player.g.ShinyValueRed;
            manlinessOfEachType[0]++;
        }
    }

    for (i = 0; i < 5; i++)
    {
        for (j = 0; j < 20 && manlinessOfEachType[i] > 0; j++)
        {
            p = enemyManlinessPoints[i][j];

            if (p != NULL && (p->state & 0x8))
            {
                zEntPickup_SetState(p, 0x40);
                p->baseFlags &= (U8)~0x40;
                p->baseFlags |= 0x80;
                xVec3Copy((xVec3*)&p->model->Mat->pos, &fromPos);
                p->grab_pos = fromPos;
                manlinessOfEachType[i]--;
            }
        }
    }
}

U32 zRewardsMgr::GetLastComboManlinessPoints() const
{
    return lastComboManlinessPoints;
}

void zRewardsMgr::ResetLastComboManlinessPoints()
{
    lastComboManlinessPoints = 0;
}

U32 zRewardsMgr::GetLastComboIndex() const
{
    return lastComboIndex;
}

void zRewardsMgr::ResetLastComboIndex()
{
    lastComboIndex = 0;
}

U32 zRewardsMgr::GetUpgradeRangeLower() const
{
    if (globals.player.ManlinessPointsCurrentRange != 0)
    {
        return upgradeSlotsPoints[globals.player.ManlinessPointsCurrentRange - 1];
    }

    return 0;
}

U32 zRewardsMgr::GetUpgradeRangeUpper() const
{
    if (globals.player.ManlinessPointsCurrentRange < 14)
    {
        return upgradeSlotsPoints[globals.player.ManlinessPointsCurrentRange];
    }

    return 99999;
}

void zRewardsMgr::IncManlinessPoints(U32 points, bool set)
{
    U32 currentRange;

    if (set)
    {
        globals.player.InvManlinessPoints = points * globals.player.InvManlinessPointsMultiplier;
        globals.player.ManlinessPointsCurrentRange = 0;
        globals.player.CurrentUnusedSkillPoints = 0;
        ((zPlayerSB*)xglobals->___player_ent_dont_use_directly)
            ->manlinessMeter.SetImmediateValue(globals.player.InvManlinessPoints);
    }
    else
    {
        globals.player.InvManlinessPoints += points * globals.player.InvManlinessPointsMultiplier;
    }

    currentRange = globals.player.ManlinessPointsCurrentRange;

    while (globals.player.InvManlinessPoints >= upgradeSlotsPoints[currentRange])
    {
        if (globals.player.ManlinessPointsCurrentRange >= 14)
        {
            break;
        }

        globals.player.ManlinessPointsCurrentRange++;
        globals.player.CurrentUnusedSkillPoints++;
        currentRange = globals.player.ManlinessPointsCurrentRange;

        if (!set && currentRange == 1)
        {
            zEntEventAllOfType(eEventFirstPowerupPoint, eBaseTypeDispatcher);
        }
    }

    if (set)
    {
        globals.player.CurrentUnusedSkillPoints -= globals.player.CurrentSpentSkillPoints;
    }

    if (globals.player.CurrentUnusedSkillPoints < 0)
    {
        globals.player.CurrentUnusedSkillPoints = 0;
    }

    if (globals.player.InvManlinessPoints > 99999)
    {
        globals.player.InvManlinessPoints = 99999;
    }
}

void zRewardsMgr::ParseValuesFromIni(xIniFile* ini)
{
    S32 i;
    char lower[64];
    char upper[64];
    char normal[64];
    char upgradeTok[64];
    char* iniString;
    S32 idx;
    U32 count;

    for (i = 0; i < 16; i++)
    {
        sprintf(normal, "Combo%d.Normal", i);
        sprintf(lower, "Combo%d.Below", i);
        sprintf(upper, "Combo%d.Above", i);

        comboManlinessPoints[i].normalValue = xIniGetInt(ini, normal, 2);
        comboManlinessPoints[i].lowerValue = xIniGetInt(ini, lower, 4);
        comboManlinessPoints[i].upperValue = xIniGetInt(ini, upper, 1);
    }

    manlinessPointRewards[0].normalValue = xIniGetInt(ini, "TIKI_WOOD.Normal", 2);
    manlinessPointRewards[0].lowerValue = xIniGetInt(ini, "TIKI_WOOD.Below", 2);
    manlinessPointRewards[0].upperValue = xIniGetInt(ini, "TIKI_WOOD.Above", 2);

    manlinessPointRewards[1].normalValue = xIniGetInt(ini, "TIKI_LOVEY.Normal", 5);
    manlinessPointRewards[1].lowerValue = xIniGetInt(ini, "TIKI_LOVEY.Below", 5);
    manlinessPointRewards[1].upperValue = xIniGetInt(ini, "TIKI_LOVEY.Above", 5);

    manlinessPointRewards[2].normalValue = xIniGetInt(ini, "TIKI_QUIET.Normal", 15);
    manlinessPointRewards[2].lowerValue = xIniGetInt(ini, "TIKI_QUIET.Below", 15);
    manlinessPointRewards[2].upperValue = xIniGetInt(ini, "TIKI_QUIET.Above", 15);

    manlinessPointRewards[3].normalValue = xIniGetInt(ini, "TIKI_THUNDER.Normal", 10);
    manlinessPointRewards[3].lowerValue = xIniGetInt(ini, "TIKI_THUNDER.Below", 10);
    manlinessPointRewards[3].upperValue = xIniGetInt(ini, "TIKI_THUNDER.Above", 10);

    manlinessPointRewards[4].normalValue = xIniGetInt(ini, "TIKI_STONE.Normal", 25);
    manlinessPointRewards[4].lowerValue = xIniGetInt(ini, "TIKI_STONE.Below", 25);
    manlinessPointRewards[4].upperValue = xIniGetInt(ini, "TIKI_STONE.Above", 25);

    manlinessPointRewards[5].normalValue = xIniGetInt(ini, "CRITJELLY.Normal", 5);
    manlinessPointRewards[5].lowerValue = xIniGetInt(ini, "CRITJELLY.Below", 5);
    manlinessPointRewards[5].upperValue = xIniGetInt(ini, "CRITJELLY.Above", 5);

    manlinessPointRewards[6].normalValue = xIniGetInt(ini, "TURBARREL_V1.Normal", 20);
    manlinessPointRewards[6].lowerValue = xIniGetInt(ini, "TURBARREL_V1.Below", 20);
    manlinessPointRewards[6].upperValue = xIniGetInt(ini, "TURBARREL_V1.Above", 20);

    manlinessPointRewards[7].normalValue = xIniGetInt(ini, "TURBARREL_V2.Normal", 25);
    manlinessPointRewards[7].lowerValue = xIniGetInt(ini, "TURBARREL_V2.Below", 25);
    manlinessPointRewards[7].upperValue = xIniGetInt(ini, "TURBARREL_V2.Above", 25);

    manlinessPointRewards[8].normalValue = xIniGetInt(ini, "TURBARREL_V3.Normal", 60);
    manlinessPointRewards[8].lowerValue = xIniGetInt(ini, "TURBARREL_V3.Below", 60);
    manlinessPointRewards[8].upperValue = xIniGetInt(ini, "TURBARREL_V3.Above", 60);

    manlinessPointRewards[9].normalValue = xIniGetInt(ini, "FOGGER_V1.Normal", 10);
    manlinessPointRewards[9].lowerValue = xIniGetInt(ini, "FOGGER_V1.Below", 10);
    manlinessPointRewards[9].upperValue = xIniGetInt(ini, "FOGGER_V1.Above", 10);

    manlinessPointRewards[10].normalValue = xIniGetInt(ini, "FOGGER_V2.Normal", 10);
    manlinessPointRewards[10].lowerValue = xIniGetInt(ini, "FOGGER_V2.Below", 10);
    manlinessPointRewards[10].upperValue = xIniGetInt(ini, "FOGGER_V2.Above", 10);

    manlinessPointRewards[11].normalValue = xIniGetInt(ini, "FOGGER_V3.Normal", 20);
    manlinessPointRewards[11].lowerValue = xIniGetInt(ini, "FOGGER_V3.Below", 20);
    manlinessPointRewards[11].upperValue = xIniGetInt(ini, "FOGGER_V3.Above", 20);

    manlinessPointRewards[12].normalValue = xIniGetInt(ini, "SLAMMER_V1.Normal", 50);
    manlinessPointRewards[12].lowerValue = xIniGetInt(ini, "SLAMMER_V1.Below", 50);
    manlinessPointRewards[12].upperValue = xIniGetInt(ini, "SLAMMER_V1.Above", 50);

    manlinessPointRewards[13].normalValue = xIniGetInt(ini, "SLAMMER_V2.Normal", 60);
    manlinessPointRewards[13].lowerValue = xIniGetInt(ini, "SLAMMER_V2.Below", 60);
    manlinessPointRewards[13].upperValue = xIniGetInt(ini, "SLAMMER_V2.Above", 60);

    manlinessPointRewards[14].normalValue = xIniGetInt(ini, "SLAMMER_V3.Normal", 75);
    manlinessPointRewards[14].lowerValue = xIniGetInt(ini, "SLAMMER_V3.Below", 75);
    manlinessPointRewards[14].upperValue = xIniGetInt(ini, "SLAMMER_V3.Above", 75);

    manlinessPointRewards[15].normalValue = xIniGetInt(ini, "FLINGER_V1.Normal", 40);
    manlinessPointRewards[15].lowerValue = xIniGetInt(ini, "FLINGER_V1.Below", 40);
    manlinessPointRewards[15].upperValue = xIniGetInt(ini, "FLINGER_V1.Above", 40);

    manlinessPointRewards[16].normalValue = xIniGetInt(ini, "FLINGER_V2.Normal", 45);
    manlinessPointRewards[16].lowerValue = xIniGetInt(ini, "FLINGER_V2.Below", 45);
    manlinessPointRewards[16].upperValue = xIniGetInt(ini, "FLINGER_V2.Above", 45);

    manlinessPointRewards[17].normalValue = xIniGetInt(ini, "FLINGER_V3.Normal", 50);
    manlinessPointRewards[17].lowerValue = xIniGetInt(ini, "FLINGER_V3.Below", 50);
    manlinessPointRewards[17].upperValue = xIniGetInt(ini, "FLINGER_V3.Above", 50);

    manlinessPointRewards[18].normalValue = xIniGetInt(ini, "SPINNER_V1.Normal", 50);
    manlinessPointRewards[18].lowerValue = xIniGetInt(ini, "SPINNER_V1.Below", 50);
    manlinessPointRewards[18].upperValue = xIniGetInt(ini, "SPINNER_V1.Above", 50);

    manlinessPointRewards[19].normalValue = xIniGetInt(ini, "SPINNER_V2.Normal", 55);
    manlinessPointRewards[19].lowerValue = xIniGetInt(ini, "SPINNER_V2.Below", 55);
    manlinessPointRewards[19].upperValue = xIniGetInt(ini, "SPINNER_V2.Above", 55);

    manlinessPointRewards[20].normalValue = xIniGetInt(ini, "SPINNER_V3.Normal", 85);
    manlinessPointRewards[20].lowerValue = xIniGetInt(ini, "SPINNER_V3.Below", 85);
    manlinessPointRewards[20].upperValue = xIniGetInt(ini, "SPINNER_V3.Above", 85);

    manlinessPointRewards[21].normalValue = xIniGetInt(ini, "POPPER_V1.Normal", 60);
    manlinessPointRewards[21].lowerValue = xIniGetInt(ini, "POPPER_V1.Below", 60);
    manlinessPointRewards[21].upperValue = xIniGetInt(ini, "POPPER_V1.Above", 60);

    manlinessPointRewards[22].normalValue = xIniGetInt(ini, "POPPER_V2.Normal", 65);
    manlinessPointRewards[22].lowerValue = xIniGetInt(ini, "POPPER_V2.Below", 65);
    manlinessPointRewards[22].upperValue = xIniGetInt(ini, "POPPER_V2.Above", 65);

    manlinessPointRewards[23].normalValue = xIniGetInt(ini, "POPPER_V3.Normal", 70);
    manlinessPointRewards[23].lowerValue = xIniGetInt(ini, "POPPER_V3.Below", 70);
    manlinessPointRewards[23].upperValue = xIniGetInt(ini, "POPPER_V3.Above", 70);

    manlinessPointRewards[24].normalValue = xIniGetInt(ini, "MERVYN_V1.Normal", 70);
    manlinessPointRewards[24].lowerValue = xIniGetInt(ini, "MERVYN_V1.Below", 70);
    manlinessPointRewards[24].upperValue = xIniGetInt(ini, "MERVYN_V1.Above", 70);

    manlinessPointRewards[25].normalValue = xIniGetInt(ini, "MERVYN_V2.Normal", 80);
    manlinessPointRewards[25].lowerValue = xIniGetInt(ini, "MERVYN_V2.Below", 80);
    manlinessPointRewards[25].upperValue = xIniGetInt(ini, "MERVYN_V2.Above", 80);

    manlinessPointRewards[26].normalValue = xIniGetInt(ini, "MERVYN_V3.Normal", 100);
    manlinessPointRewards[26].lowerValue = xIniGetInt(ini, "MERVYN_V3.Below", 100);
    manlinessPointRewards[26].upperValue = xIniGetInt(ini, "MERVYN_V3.Above", 100);

    manlinessPointRewards[27].normalValue = xIniGetInt(ini, "BUCKOTRON_V1.Normal", 300);
    manlinessPointRewards[27].lowerValue = xIniGetInt(ini, "BUCKOTRON_V1.Below", 300);
    manlinessPointRewards[27].upperValue = xIniGetInt(ini, "BUCKOTRON_V1.Above", 300);

    manlinessPointRewards[28].normalValue = xIniGetInt(ini, "BUCKOTRON_V2.Normal", 300);
    manlinessPointRewards[28].lowerValue = xIniGetInt(ini, "BUCKOTRON_V2.Below", 300);
    manlinessPointRewards[28].upperValue = xIniGetInt(ini, "BUCKOTRON_V2.Above", 300);

    manlinessPointRewards[29].normalValue = xIniGetInt(ini, "BUCKOTRON_V3.Normal", 300);
    manlinessPointRewards[29].lowerValue = xIniGetInt(ini, "BUCKOTRON_V3.Below", 300);
    manlinessPointRewards[29].upperValue = xIniGetInt(ini, "BUCKOTRON_V3.Above", 300);

    manlinessPointRewards[30].normalValue = xIniGetInt(ini, "BUCKOTRON_V4.Normal", 300);
    manlinessPointRewards[30].lowerValue = xIniGetInt(ini, "BUCKOTRON_V4.Below", 300);
    manlinessPointRewards[30].upperValue = xIniGetInt(ini, "BUCKOTRON_V4.Above", 300);

    manlinessPointRewards[31].normalValue = xIniGetInt(ini, "BUCKOTRON_V5.Normal", 300);
    manlinessPointRewards[31].lowerValue = xIniGetInt(ini, "BUCKOTRON_V5.Below", 300);
    manlinessPointRewards[31].upperValue = xIniGetInt(ini, "BUCKOTRON_V5.Above", 300);

    manlinessPointRewards[32].normalValue = xIniGetInt(ini, "BUCKOTRON_V6.Normal", 300);
    manlinessPointRewards[32].lowerValue = xIniGetInt(ini, "BUCKOTRON_V6.Below", 300);
    manlinessPointRewards[32].upperValue = xIniGetInt(ini, "BUCKOTRON_V6.Above", 300);

    manlinessPointRewards[33].normalValue = xIniGetInt(ini, "BUCKOTRON_V7.Normal", 300);
    manlinessPointRewards[33].lowerValue = xIniGetInt(ini, "BUCKOTRON_V7.Below", 300);
    manlinessPointRewards[33].upperValue = xIniGetInt(ini, "BUCKOTRON_V7.Above", 300);

    manlinessPointRewards[34].normalValue = xIniGetInt(ini, "BUCKETJELLY.Normal", 5);
    manlinessPointRewards[34].lowerValue = xIniGetInt(ini, "BUCKETJELLY.Below", 5);
    manlinessPointRewards[34].upperValue = xIniGetInt(ini, "BUCKETJELLY.Above", 5);

    manlinessPointRewards[35].normalValue = xIniGetInt(ini, "ZAPPER_V1.Normal", 70);
    manlinessPointRewards[35].lowerValue = xIniGetInt(ini, "ZAPPER_V1.Below", 70);
    manlinessPointRewards[35].upperValue = xIniGetInt(ini, "ZAPPER_V1.Above", 70);

    manlinessPointRewards[36].normalValue = xIniGetInt(ini, "ZAPPER_V2.Normal", 80);
    manlinessPointRewards[36].lowerValue = xIniGetInt(ini, "ZAPPER_V2.Below", 80);
    manlinessPointRewards[36].upperValue = xIniGetInt(ini, "ZAPPER_V2.Above", 80);

    manlinessPointRewards[37].normalValue = xIniGetInt(ini, "ZAPPER_V3.Normal", 100);
    manlinessPointRewards[37].lowerValue = xIniGetInt(ini, "ZAPPER_V3.Below", 100);
    manlinessPointRewards[37].upperValue = xIniGetInt(ini, "ZAPPER_V3.Above", 100);

    memset(upgradeSlotsPoints, 0, sizeof(upgradeSlotsPoints));

    for (i = 0; i < 14; i++)
    {
        sprintf(upgradeTok, "Upgrade%d", i + 1);
        upgradeSlotsPoints[i] = xIniGetInt(ini, upgradeTok, i * 1000);

        if (i > 0)
        {
            upgradeSlotsPoints[i] += upgradeSlotsPoints[i - 1];
        }
    }

    idx = 0;
    count = 0;
    iniString = xIniGetString(ini, "ThresholdPointsRange", "", idx);

    while (idx > 0 && count < 40)
    {
        U32 values[2];
        U32 i;
        U32 sceneID;
        char* start;

        while (_isspace(*iniString))
        {
            iniString++;
        }

        sceneID = (_toupper(iniString[0]) << 24) | (_toupper(iniString[1]) << 16) |
                  (_toupper(iniString[2]) << 8) | _toupper(iniString[3]);
        iniString += 4;

        memset(values, 0, sizeof(values));

        for (i = 0; i < 2; i++)
        {
            U8 foundSpace = FALSE;

            while (_isspace(*iniString))
            {
                iniString++;

                if (!foundSpace && !_isspace(*iniString))
                {
                    start = iniString;
                    foundSpace = TRUE;
                }
            }

            if (foundSpace)
            {
                char strValue[64];

                while (!_isspace(*iniString))
                {
                    iniString++;
                }

                memset(strValue, 0, sizeof(strValue));
                memcpy(strValue, start, iniString - start);
                values[i] = atoi(strValue);
            }
        }

        if (values[0] < values[1])
        {
            thresholdPoints[count].sceneID = sceneID;
            thresholdPoints[count].lower = values[0];
            thresholdPoints[count].upper = values[1];
            count++;
        }

        iniString = xIniGetString(ini, "ThresholdPointsRange", "", idx);
    }
}

void zRewardsMgr::GiveAllRewardsNow()
{
    S32 i;

    for (i = 0; i < 20; i++)
    {
        if (rewardsQ[i].active)
        {
            IncManlinessPoints(rewardsQ[i].numRewards, false);
            rewardsQ[i].active = FALSE;
            return;
        }
    }
}

void zRewardsMgr::SetPlayerDead()
{
    playerDead = TRUE;
}
