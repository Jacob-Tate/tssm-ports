#ifndef ZREWARDS_H
#define ZREWARDS_H

#include "types.h"
#include <string.h>
#include "zEnt.h"
#include "zEntPickup.h"
#include "zNPCTypes.h"
#include "xIni.h"

struct zRewardsQ
{
    U8 active;
    S32 numRewards;
    xVec3 spawnPos;
};

struct EnemyRewardsTable
{
    S32 enemy;
    U32 normalValue;
    U32 lowerValue;
    U32 upperValue;
};

struct ThresholdPointsRange
{
    U32 sceneID;
    U32 lower;
    U32 upper;
};

struct zRewardsMgr
{
    zRewardsQ rewardsQ[20];
    F32 comboTime;
    F32 comboTimer;
    U32 numCratesDestroyed;
    U32 numEnemiesDefeated;
    U32 lastComboManlinessPoints; // 0x1a0
    U32 lastComboIndex; // 0x1a4
    F32 spawnTimer;
    bool playerDead; // 0x1ac
    zEntPickup* enemyManlinessPoints[5][20];

    static EnemyRewardsTable manlinessPointRewards[38];
    static EnemyRewardsTable comboManlinessPoints[16];
    static ThresholdPointsRange thresholdPoints[40];
    static U32 upgradeSlotsPoints[14];

    static void ParseValuesFromIni(xIniFile* ini);

    zRewardsMgr()
    {
        lastComboManlinessPoints = 0;
        lastComboIndex = 0;
        numCratesDestroyed = 0;
        numEnemiesDefeated = 0;
        comboTime = 0.0f;
        comboTimer = -1.0f;
        spawnTimer = 0.0f;
        playerDead = FALSE;
        memset(rewardsQ, 0, sizeof(rewardsQ));
    }

    void SetPlayerDead();
    void GiveAllRewardsNow();
    void IncManlinessPoints(U32 points, bool set);
    void ResetLastComboIndex();
    U32 GetLastComboIndex() const;
    void ResetLastComboManlinessPoints();
    U32 GetLastComboManlinessPoints() const;
    void SpawnRewards(S32 numRewards, const xVec3& fromPos);
    void Update(F32 dt);
    U32 GetComboManlinessPoints();
    void GiveReward(en_npctyp enemy, const xVec3& fromPos);
    U32 GetUpgradeRangeUpper() const;
    U32 GetUpgradeRangeLower() const;
};

#endif
