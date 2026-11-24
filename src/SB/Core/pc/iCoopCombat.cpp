// PC-only: fights, damage, the guest's respawn and pickups in online co-op
// (iCoop.h, docs/MULTIPLAYER.md 6). Compiled with the game units.
//
// While both players are in the same level (the shared world of
// iCoopWorld.cpp is up):
//   - the guest's attacks on the host's enemies are claims (EV_HIT): the
//     guest's own hit does nothing to its copy, the host checks the claim
//     (the level, the enemy, the attack the guest's character has, where the
//     guest and the enemy are, how often) and applies it with its player
//     standing in at the guest's place, so the enemy is knocked away from the
//     guest; its death reaches the guest with the snapshots (5.5);
//   - an enemy's attack on the guest (the stand-in of 5.4) and a projectile
//     that flies at the guest (a stand-in around the hazard's update) are
//     sent to the guest (EV_DAMAGE), whose game applies them to its own player
//     with the game's own damage functions: health, HUD, knockback, death;
//   - the guest's defeat plays out as in single-player, then the guest comes
//     back beside the host in a burst of bubbles, with full health, instead of
//     losing a life: nothing is reset on either side. The host's defeat keeps
//     the game's own rules (its lives, its level reset), and the guest is put
//     beside the host once the host plays again;
//   - pickups are the host's: the host says which are still there (PICKUPS),
//     the guest's copies follow, and a guest that touches one asks the host
//     (EV_PICKUP); the host counts it for its own progress (shiny objects,
//     Goofy Goober tokens, treasure chests) and answers (EV_GRANT), and the
//     guest's game plays the pickup (health goes to the guest);
//   - progress is one (docs/MULTIPLAYER.md 13): the host's save's tokens,
//     shiny objects, upgrade points and moves go to the guest (PROGRESS),
//     whose game shows and plays by them while it is in the host's level and
//     gets its own back on leaving it; a move the guest's game would give its player
//     (Mindy's talk, the pause menu) is asked of the host (EV_UPGRADE).
//
// Everything from the network is used as validated numbers only: net IDs and
// pickup IDs index this game's own tables after a range check, the level and
// the table's hash must be this game's own, positions are finite and bounded
// (iNetProto.cpp) and checked against what this game sees.

#include "iCoop.h"
#include "iCoopInternal.h"
#include "iCoopWorldInternal.h"

#include "xEnt.h"
#include "xEvent.h"
#include "xGroup.h"
#include "xMath3.h"
#include "xserializer.h"
#include "zEnt.h"
#include "zEntPickup.h"
#include "zEntPlayer.h"
#include "zFX.h"
#include "zGame.h"
#include "zGameState.h"
#include "zGlobals.h"
#include "zConditional.h"
#include "zDestructible.h"
#include "zMenuSB.h"
#include "zNMECommon.h"
#include "zNMENPCWrapper.h"
#include "zPickupTable.h"
#include "zRewards.h"
#include "zSaveLoad.h"
#include "zScene.h"
#include "zScript.h"
#include "zUICustom.h"
#include "zVar.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <vector>

using namespace iNet;

namespace
{
    const U64 kNsPerMs = 1000000ull;
    const U64 kNsPerSec = 1000000000ull;

    // The guest: one claim per enemy this often at most (a spin touches an
    // enemy on several frames; the enemy's own invulnerability is about as
    // long).
    const U64 kClaimEvery = 300 * kNsPerMs;
    // The host: claims believed at most this many a second (burst as many).
    const F32 kHitsPerSec = 12.0f;
    // The host: one hit on the same enemy this often at most.
    const U64 kHitEvery = 150 * kNsPerMs;
    // How far the target the guest hit may be from where the host has it (the
    // guest draws the host's world 100 ms back, and its claim takes a trip),
    // and how far from the guest each attack reaches.
    const F32 kTargetSlack = 8.0f;
    const F32 kStillSlack = 2.0f; // a breakable that never moves
    // zVar's entry for the Goofy Goober token count (a condition's variable).
    const U32 kVarTokens = 18;
    const F32 kReachMelee = 7.0f;
    const F32 kReachToss = 45.0f;
    const F32 kReachBowl = 60.0f;
    const F32 kReachCruise = 150.0f;
    // The car's bound plus its top speed over the guest state's age.
    const F32 kReachCar = 15.0f;
    // The lightning reaches 4 past the bowl's target.
    const F32 kReachZap = kReachBowl + 4.0f;
    // Crates and breakables: a car through a pile hits up to 15 at once.
    const F32 kBoxesPerSec = 20.0f;
    // The enemies' own invulnerability after a hit (cfg_common's tym_invuln),
    // kept for each player on the host.
    const U64 kOwnInvuln = 500 * kNsPerMs;
    // The guest: damage events applied at most this many a second.
    const F32 kDamagePerSec = 10.0f;
    // The guest's respawn: safe from hits this long after.
    const F32 kRespawnSafe = 2.0f;
    // Pickups: claims the host believes per second, how far from the guest a
    // pickup may be, how often the guest asks for the same one, how often the
    // host sends PICKUPS when nothing changed.
    const F32 kPickupsPerSec = 15.0f;
    const F32 kPickupReach = 6.0f;
    const U64 kPickupAskEvery = 1000 * kNsPerMs;
    const U64 kPickupsEvery = 1000 * kNsPerMs;
    // A token or chest the guest's own game showed or gave: one claim this
    // often at most.
    const U64 kOwnPickupEvery = 5000 * kNsPerMs;
    // The guest: a given token the host has not granted is asked again once
    // a second while this player plays, this many times at most (its script
    // gives it once only).
    const U8 kGivenTries = 30;
    // A token the guest took: the host's level goes on (its level end waits
    // for the reward animation's end) as long after as that animation lasts.
    const U64 kTokenDoneAfter = 5800 * kNsPerMs;
    // PROGRESS: sent when it changes, and this often anyway.
    const U64 kProgressEvery = 1000 * kNsPerMs;
    // EV_UPGRADE: believed at most this many a second, one per move this often.
    const F32 kUpgradesPerSec = 2.0f;
    const U64 kUpgradeEvery = 500 * kNsPerMs;
    // A purchase is believed this many of the guest's states (about 2 s: it
    // sends about 33 a second) after one said paused: a resent event may
    // come with a state from after the menu closed.
    const U32 kPausedStates = 66;
    // The guest: an answer counts only this long after its ask.
    const U64 kUpgradeAnswerWithin = 5000 * kNsPerMs;
    const S32 kLogMax = 64; // log lines of one kind per level, at most

    struct Bucket
    {
        F32 tokens;
        U64 last;
        bool take(U64 now, F32 rate)
        {
            if (last == 0)
            {
                tokens = rate;
            }
            else if (now > last)
            {
                tokens += (F32)((F64)(now - last) / 1e9) * rate;
                if (tokens > rate)
                {
                    tokens = rate;
                }
            }
            last = now;
            if (tokens < 1.0f)
            {
                return false;
            }
            tokens -= 1.0f;
            return true;
        }
    };

    // --- both sides -----------------------------------------------------------------------------

    U32 sLogged[5];
    // The level the per-level state below is for (0: none).
    U32 sLevel;
    U32 sLevelHash;

    // --- the guest's hits and the host's checks --------------------------------------------------

    std::vector<U64> sClaimAt; // guest: per thing, the last claim
    std::vector<U64> sHitAt;   // host: per thing, the last hit applied
    std::vector<U64>
        sOwnHitAt[2]; // host: per thing, each player's last hit that landed (0 host, 1 guest)
    std::vector<U8> sOwnHitBy; // and whose it was, the newest
    bool sInvulnOn; // host: the guest is in this level (per-player invulnerability)
    bool sLanded; // host: the hit being applied got past the enemy's own check
    // Host: a hit of one player while the enemy still reels from the other's
    // (its wound goal takes no damage then): it lands as soon as that ends.
    struct PendingHit
    {
        S32 id;
        U8 who; // 0 the host's player, 1 the guest
        en_npcdmg type;
        S32 amount;
        en_plyrpup pup;
        bool haveDir;
        xVec3 dir;
        U8 attack; // the guest's ATK_*, for the log
        U64 at;
    };
    std::vector<PendingHit> sPending;
    bool sDeferred;  // host: the hook held the hit being applied back (PendingHit)
    bool sReplaying; // host: a held hit is being applied now
    const U64 kHoldFor = 2500 * kNsPerMs; // a held hit is dropped after this long
    Bucket sHitBucket;
    Bucket sBoxBucket;
    U32 sHitsApplied, sHitsRefused, sHitsNoEffect;
    bool sApplying;   // host: a guest's hit is being applied
    xBase* sApplyTarget; // and the enemy it is on (another one hurt meanwhile is not the guest's hit)
    // Host: game time (the frames' own time, as the enemies' timers count
    // it), for each player's invulnerability and the held hits; a stall of
    // this game does not age them.
    U64 sGameNs = kNsPerSec;

    // --- damage to the guest ----------------------------------------------------------------------

    U64 sGuestSafeUntil; // host: the guest's own DamageTimer, as the host last set it by a hit
    Bucket sDamageBucket;
    U32 sDamageSent, sDamageApplied;

    // --- the guest's defeat and respawn, the host's ------------------------------------------------

    bool sRespawnPending; // guest
    U32 sRespawns;
    bool sHostWasDead;   // guest: the host's newest state said defeated
    bool sGuestWasDead;  // host: the guest's newest state said defeated

    // --- pickups ------------------------------------------------------------------------------------

    enum
    {
        CAN_REVEAL = 1, // this level's own data can show it (a link or a script)
        CAN_GIVE = 2, // or give it
    };

    struct Pick
    {
        zEntPickup* ent;
        U32 id;
        U64 askedAt;     // guest: the last claim
        bool hiddenByHost; // guest: hidden because the host's is gone
        bool shownByHost; // guest: shown because the host's game shows it
        // Guest: its state, shown flag and collision as this player's own
        // game had them when the host's world first reached this level here.
        U8 ownState;
        U8 ownFlags;
        U8 ownPickFlags;
        U8 can; // CAN_*
        bool givenAsked; // guest: its own game gave it; asked of the host
        U8 givenTries; // guest: asks of it while playing
    };
    std::vector<Pick> sPicks; // sorted by asset ID: the same on both games
    U32 sPickHash;
    zScene* sPickScene;
    U32 sPickSceneId;
    bool sPickValid;
    zScene* sPickBuiltScene; // the level the table was last made for (it may have none)
    U32 sPickBuiltId;
    Pickups sPickOut;     // host: the newest bits
    bool sPickOutDirty;   // changed since last sent
    U64 sPickSentAt;
    Bucket sPickBucket;
    Pickups sPickIn;      // guest: the newest from the host
    bool sPickInHave;
    bool sPickHostChanged; // guest: the host's PICKUPS changed one of this level's here
    U64 sOwnPickupAt; // host: the last claim of one the guest's own game showed or gave
    U64 sTokenDoneAt; // host: when the tokens the guest took have had their reward animation
    U32 sTokenDoneScene;
    std::vector<zEntPickup*> sTokensDone; // host: those tokens, whose level signal is still due
    bool sTokensReset; // host: the level was reset since (zSceneReset)
    U32 sTeaches; // bit i: this level's data gives move i (eEventGivePowerUp)

    // --- one progress (docs/MULTIPLAYER.md 13) -------------------------------------------------

    Progress sProgOut; // host: the newest
    bool sProgReady; // host: made from this save at least once (nothing goes out before)
    bool sProgOutDirty;
    U64 sProgSentAt;
    Progress sProgIn; // guest: the newest from the host
    bool sProgInHave;
    Progress sProgLogged; // guest: the last one logged
    // Guest: this player's own progress, as it was when the host's level was
    // entered (put back on leaving it, when the host has gone elsewhere, and
    // when the session ends), so the save only ever gets this player's own.
    struct OwnProgress
    {
        U32 tokens, shiny, range, chestTotal, chestOpened, chestLevel;
        U32 chest[20];
        S16 unused, spent;
        U8 powerup[kPowerUps];
        U32 sbMax, patMax;
    };
    OwnProgress sOwn;
    bool sOwnSaved; // guest: the host's progress is shown (this level is the host's)
    U32 sShareScene; // and the level
    // Guest: the level whose own state was kept as the host's world first
    // reached it (KeepOwnLevel; the Pick's own* fields), 0 for none.
    U32 sOwnLevelScene;
    // And its save block, written from this game's own state just then; put
    // back when the level is left with its block kept (iCoopKeepSceneSave),
    // so a level reset meanwhile (which saves the block) leaves nothing of
    // the host's in it.
    std::vector<S32> sOwnBlock;
    U32 sOwnBlockScene;
    U64 sHostAwaySince; // guest: the host in another level since (0: it is not)
    // The host in another level this long: this level is this player's own
    // again (longer than following the host takes to start).
    const U64 kUnshareAfter = 3000 * kNsPerMs;
    Bucket sUpgBucket;
    U64 sUpgAt[kPowerUps]; // host: per move, the last claim; guest: the last asked
    U8 sUpgAskKind[kPowerUps]; // guest: the kind of that ask (UPG_*), 0 once answered
    // Host: the guest's newest state seen, and how many since one said paused.
    bool sPauseSeenHave;
    U32 sPauseSeenFrame;
    U32 sStatesSincePaused = kPausedStates + 1;

    // --- helpers -----------------------------------------------------------------------------------

    zPlayer* Player()
    {
        return xglobals->___player_ent_dont_use_directly;
    }

    S32 SendDamage(xBase* src, U32 damage, S32 kind, const F32* vec, S32 sub, const char* what);

    bool Guest()
    {
        return iCoopLocalSlot() == 1;
    }

    F32 Dist(const F32* a, const F32* b)
    {
        F32 dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        return sqrtf(dx * dx + dy * dy + dz * dz);
    }

    enum LogKind
    {
        LOG_HIT,
        LOG_DAMAGE,
        LOG_PICK,
        LOG_PICKSTATE,
        LOG_PROGRESS,
        LOG_KINDS
    };

    // Each kind of line at most kLogMax times a level (a hostile partner
    // cannot fill the log).
    bool LogOk(S32 kind)
    {
        return sLogged[kind]++ < (U32)kLogMax;
    }

    // Host: this game is paused, in a cutscene or its player is defeated. The
    // guest's claims are refused meanwhile: the single-player game never has
    // its player hit an enemy or take a pickup then (an enemy hurt in the
    // middle of a cutscene, a token's autosave during the host's defeat).
    const char* HostBusy()
    {
        if (zGameIsPaused())
        {
            return "the host's game is paused";
        }
        if (globals.cmgr != NULL)
        {
            return "the host is in a cutscene";
        }
        if (globals.player.s == NULL || globals.player.s->Health == 0)
        {
            return "the host is defeated";
        }
        return NULL;
    }

    zNMECommon* Enemy(xEnt* e)
    {
        return static_cast<zNMECommon*>(static_cast<zNMENPCWrapper*>(e));
    }

    // The game's damage types the guest's attacks give, as the wire's kinds.
    U8 AttackOf(S32 dmgtype)
    {
        switch ((en_npcdmg)dmgtype)
        {
        case DMGTYP_SIDE: return ATK_SIDE;
        case DMGTYP_BELOW: return ATK_BELOW;
        case DMGTYP_ABOVE: return ATK_ABOVE;
        case DMGTYP_HITBYTOSS: return ATK_TOSS;
        case DMGTYP_BUBBOWL: return ATK_BOWL;
        case DMGTYP_CRUISEBUBBLE: return ATK_CRUISE;
        case DMGTYP_CARTWHEEL: return ATK_CARTWHEEL;
        case DMGTYP_SLIDE: return ATK_SLIDE;
        case DMGTYP_CARSMASH:
            return ATK_CAR;
        case DMGTYP_BBOWLZAP:
            return ATK_ZAP;
        default: return 0;
        }
    }

    bool Tiki(xEnt* e)
    {
        U32 t = (U32) static_cast<zNMENPCWrapper*>(e)->SelfType();
        return t >= NME_TYPE_TIKI && t <= NME_TYPE_TIKI_STONE;
    }

    // There, as zNMECommon::IsPresent has it, but culled or not: the host's
    // game culls the parts of the level its own player is not in (flag 0x4),
    // and the guest may be in one of those.
    bool PresentAnywhere(zNMECommon* nme)
    {
        return nme->runtimeData.flg_activeOn && (nme->flags & 0x81) == 0x1 && nme->IsAlive();
    }

    // And back, on the host, for the guest's character: the damage type, and
    // the attack whose damage it does (as NMEC_DmgAmtForAttack would on the
    // guest's own game). FALSE: that character has no such attack.
    bool DamageOf(U8 atk, U8 character, en_npcdmg* type, en_plyratak* attack)
    {
        bool sb = character == CHAR_SPONGEBOB;
        switch (atk)
        {
        case ATK_SIDE:
            *type = DMGTYP_SIDE;
            *attack = sb ? PLYR_ATAK_SB_KARATE : PLYR_ATAK_PAT_BELLY;
            return true;
        case ATK_BELOW: // the game's Bash damage, whoever lands it from below
            *type = DMGTYP_BELOW;
            *attack = PLYR_ATAK_SB_BASH;
            return true;
        case ATK_ABOVE: // and its Smash damage from above
            *type = DMGTYP_ABOVE;
            *attack = PLYR_ATAK_PAT_FLOP;
            return true;
        case ATK_TOSS:
            *type = DMGTYP_HITBYTOSS;
            *attack = PLYR_ATAK_PAT_THROW;
            return !sb;
        case ATK_BOWL:
            *type = DMGTYP_BUBBOWL;
            *attack = PLYR_ATAK_SB_BOWL;
            return sb;
        case ATK_CRUISE:
            *type = DMGTYP_CRUISEBUBBLE;
            *attack = PLYR_ATAK_SB_GUITAR;
            return sb;
        case ATK_CARTWHEEL:
            *type = DMGTYP_CARTWHEEL;
            *attack = PLYR_ATAK_PAT_CARTWHEEL;
            return !sb;
        case ATK_SLIDE:
            *type = DMGTYP_SLIDE;
            *attack = PLYR_ATAK_SLIDE;
            return true;
        case ATK_CAR: // only while this game's own player is the car: a drive level
            *type = DMGTYP_CARSMASH;
            *attack = PLYR_ATAK_NONE;
            return gCurrentPlayer == eCurrentPlayerCar;
        case ATK_ZAP: // the upgraded SpongeBowl's: the shared save must have it
            *type = DMGTYP_BBOWLZAP;
            *attack = PLYR_ATAK_NONE;
            return sb && globals.player.g.PowerUp[PLYR_ATAK_SB_BOWL - 1] >= 2;
        default:
            return false;
        }
    }

    F32 ReachOf(U8 atk)
    {
        switch (atk)
        {
        case ATK_TOSS: return kReachToss;
        case ATK_BOWL: return kReachBowl;
        case ATK_CRUISE: return kReachCruise;
        case ATK_CAR:
            return kReachCar;
        case ATK_ZAP:
            return kReachZap;
        default: return kReachMelee;
        }
    }

    // A breakable's hit as the guest's attack would send it (zEntPlayer's
    // { 1, 9 }, the car's { 1, 1 }), for the host's own destructible code.
    U32 BreakEventOf(U8 atk, F32 param[4])
    {
        param[0] = 1.0f;
        param[1] = 9.0f;
        param[2] = param[3] = 0.0f;
        switch (atk)
        {
        case ATK_CAR:
            param[1] = 1.0f;
            return eEventHit;
        case ATK_BELOW:
            return eEventHit_BubbleBash;
        case ATK_CRUISE:
            param[1] = 0.0f;
            return eEventHit_Cruise;
        default:
            return eEventHit;
        }
    }

    // The per-level state, for this level's table (iCoopWorld.cpp's).
    bool LevelTable(U32* scene, U32* hash, S32* count)
    {
        if (!iCoopWorldTable(scene, hash, count))
        {
            return false;
        }
        if (*scene != sLevel || *hash != sLevelHash || (S32)sClaimAt.size() != *count)
        {
            sLevel = *scene;
            sLevelHash = *hash;
            sClaimAt.assign(*count, 0);
            sHitAt.assign(*count, 0);
            sOwnHitAt[0].assign(*count, 0);
            sOwnHitAt[1].assign(*count, 0);
            sOwnHitBy.assign(*count, 0);
            memset(sLogged, 0, sizeof(sLogged));
        }
        return true;
    }

    // --- pickups: the table -------------------------------------------------------------------

    // Shiny objects (all five colours), and health, Goofy Goober tokens and
    // treasure chests; not the car's nitro.
    bool SharedPickup(const zEntPickup* p)
    {
        if (p->p == NULL)
        {
            return false;
        }
        if (p->p->pickupType == 1)
        {
            return p->p->pickupIndex <= 4;
        }
        return p->p->pickupType == 0 &&
               (p->p->pickupIndex == 2 || p->p->pickupIndex == 3 || p->p->pickupIndex == 4);
    }

    bool PickTableCurrent()
    {
        zScene* s = globals.sceneCur;
        return sPickValid && s != NULL && s == sPickScene && s->sceneID == sPickSceneId;
    }

    void ClearPicks()
    {
        sPicks.clear();
        sPickValid = false;
        sPickScene = NULL;
        sPickSceneId = 0;
        sPickHash = 0;
        sPickOutDirty = true;
        sPickSentAt = 0;
        sPickInHave = false;
    }

    void ScanPicks(zScene* s);

    void BuildPicks()
    {
        ClearPicks();
        sPickHostChanged = false;
        zScene* s = globals.sceneCur;
        if (s == NULL || s->base == NULL)
        {
            return;
        }
        sPickBuiltScene = s;
        sPickBuiltId = s->sceneID;
        for (U32 i = 0; i < s->num_base; i++)
        {
            xBase* b = s->base[i];
            if (b == NULL || b->baseType != eBaseTypePickup)
            {
                continue;
            }
            zEntPickup* p = (zEntPickup*)b;
            if (!SharedPickup(p) || p->model == NULL || p->model->Mat == NULL)
            {
                continue;
            }
            Pick k;
            memset(&k, 0, sizeof(k));
            k.ent = p;
            k.id = b->id;
            sPicks.push_back(k);
            if (sPicks.size() >= kMaxPickups)
            {
                printf("tssm: coop: this level has more than %d pickups; the rest stay each game's own\n",
                       (int)kMaxPickups);
                break;
            }
        }
        std::stable_sort(sPicks.begin(), sPicks.end(), [](const Pick& a, const Pick& b) { return a.id < b.id; });
        ScanPicks(s);
        U32 h = 2166136261u;
        for (size_t i = 0; i < sPicks.size(); i++)
        {
            U8 bytes[6] = { (U8)sPicks[i].id, (U8)(sPicks[i].id >> 8), (U8)(sPicks[i].id >> 16),
                            (U8)(sPicks[i].id >> 24), sPicks[i].ent->p->pickupType, sPicks[i].ent->p->pickupIndex };
            for (int k = 0; k < 6; k++)
            {
                h = (h ^ bytes[k]) * 16777619u;
            }
        }
        sPickHash = h;
        sPickScene = s;
        sPickSceneId = s->sceneID;
        sPickValid = !sPicks.empty();
        S32 counts[3] = { 0, 0, 0 };
        for (size_t i = 0; i < sPicks.size(); i++)
        {
            const zAssetPickup* a = sPicks[i].ent->p;
            counts[a->pickupType == 1 ? 0 : (a->pickupIndex == 3 ? 1 : 2)]++;
        }
        printf("tssm: coop: level shares %d pickups (%d shiny objects, %d health, %d tokens and chests), table %08x\n",
               (int)sPicks.size(), (int)counts[0], (int)counts[1], (int)counts[2], (unsigned)sPickHash);
        fflush(stdout);
    }

    S32 PickIndex(const xBase* b)
    {
        // Sorted by asset ID; the pointer must be the one found there.
        U32 id = b->id;
        size_t lo = 0, hi = sPicks.size();
        while (lo < hi)
        {
            size_t mid = (lo + hi) / 2;
            if (sPicks[mid].id < id)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
        for (size_t i = lo; i < sPicks.size() && sPicks[i].id == id; i++)
        {
            if ((const xBase*)sPicks[i].ent == b)
            {
                return (S32)i;
            }
        }
        return -1;
    }

    U32 PickState(const zEntPickup* p)
    {
        return p->state & 0x7F;
    }

    // Taken: on its way to a player (0x4) or collected (0x8).
    bool PickTaken(const zEntPickup* p)
    {
        U32 st = PickState(p);
        return st == 0x4 || st == 0x8;
    }

    // There to be taken: waiting (0x1) or dropping (0x2), shown, its
    // collision on. Culled or not (flag 0x4): the host's game culls the parts
    // of the level its own player is not in, and the guest may be in one.
    bool PickAvailable(const zEntPickup* p)
    {
        U32 st = PickState(p);
        return (st == 0x1 || st == 0x2) && (p->flags & 0x81) == 0x1 && (p->pickupFlags & 0x2);
    }

    bool TokenOrChest(const zEntPickup* p)
    {
        return p->p->pickupType == 0 && (p->p->pickupIndex == 2 || p->p->pickupIndex == 4);
    }

    // What this level's own data does to pickup `id` by event `ev` (a link of
    // any of its things, or a script's event), and the moves it gives.
    void MarkPick(U32 id, U32 ev, const F32* param, S32 depth)
    {
        if (ev == eEventGivePowerUp && param != NULL && param[0] >= 0.0f && param[0] < (F32)kMoves)
        {
            sTeaches |= 1u << (U32)param[0];
        }
        U8 bit = 0;
        if (ev == eEventVisible || ev == eEventFastVisible || ev == eEventCollision_Visible_On)
        {
            bit = CAN_REVEAL;
        }
        else if (ev == eEventPickup)
        {
            bit = CAN_GIVE;
        }
        if (bit == 0)
        {
            return;
        }
        xBase* to = zSceneFindObject(id);
        if (to != NULL && to->baseType == eBaseTypeGroup && depth < 4)
        {
            xGroup* g = (xGroup*)to;
            U32 n = xGroupGetCount(g);
            for (U32 k = 0; k < n && k < 4096; k++)
            {
                xBase* m = xGroupGetItemPtr(g, k);
                if (m != NULL)
                {
                    MarkPick(m->id, ev, NULL, depth + 1);
                }
            }
            return;
        }
        for (size_t i = 0; i < sPicks.size(); i++)
        {
            if (sPicks[i].id == id)
            {
                sPicks[i].can |= bit;
            }
        }
    }

    void ScanPicks(zScene* s)
    {
        sTeaches = 0;
        for (U32 i = 0; i < s->num_base; i++)
        {
            xBase* b = s->base[i];
            if (b == NULL)
            {
                continue;
            }
            for (U32 k = 0; k < b->linkCount && b->link != NULL; k++)
            {
                MarkPick(b->link[k].dstAssetID, b->link[k].dstEvent, b->link[k].param, 0);
            }
            if (b->baseType == eBaseTypeScript)
            {
                _zScript* sc = (_zScript*)b;
                if (sc->tasset == NULL)
                {
                    continue;
                }
                const xScriptEventAsset* ev = (const xScriptEventAsset*)(sc->tasset + 1);
                for (U32 k = 0; k < sc->tasset->eventCount && k < 4096; k++)
                {
                    MarkPick(ev[k].widget, ev[k].paramEvent, ev[k].param, 0);
                }
            }
        }
    }

    // Host: one of this level's conditions on the Goofy Goober tokens (the
    // gate of a talk that teaches a move) holds with this save's count.
    bool TokenGateHolds()
    {
        zScene* s = globals.sceneCur;
        if (s == NULL || s->base == NULL)
        {
            return false;
        }
        for (U32 i = 0; i < s->num_base; i++)
        {
            xBase* b = s->base[i];
            if (b == NULL || b->baseType != eBaseTypeCond)
            {
                continue;
            }
            _zConditional* c = (_zConditional*)b;
            if (c->varEntry != NULL && c->varEntry->entry == kVarTokens && zConditional_Evaluate(c))
            {
                return true;
            }
        }
        return false;
    }

    // --- the host: the guest's hits ------------------------------------------------------------

    void Refuse(const GameEvent& e, const char* why)
    {
        sHitsRefused++;
        if (LogOk(LOG_HIT))
        {
            printf("tssm: coop: the guest's hit on thing %d (attack %d) refused: %s\n", (int)e.id, (int)e.kind, why);
            fflush(stdout);
        }
    }

    // A guest's hit on a breakable (a static or a platform with a
    // destructible): the host's own copy takes the hit its attack would give,
    // so its links and effects run here once; its break reaches the guest
    // with the snapshots (WF_BROKEN).
    void HostBreak(const GameEvent& e, xEnt* ent, zDestructible* dest, const F32* guestPos,
                   U8 character, U64 now)
    {
        en_npcdmg type;
        en_plyratak attack;
        if (e.kind == ATK_ZAP || !DamageOf(e.kind, character, &type, &attack))
        {
            Refuse(e, "not an attack of the guest's character on a breakable");
            return;
        }
        const F32* at = (const F32*)&ent->model->Mat->pos;
        F32 slack = Dist(e.pos, at);
        F32 reach = Dist(guestPos, e.pos);
        // One that never moves is drawn where it is; one on a platform moves.
        bool still = ent->baseType == eBaseTypeStatic && ent->driver == NULL;
        if (slack > (still ? kStillSlack : kTargetSlack) || reach > ReachOf(e.kind))
        {
            if (LogOk(LOG_HIT))
            {
                printf(
                    "tssm: coop: the guest's hit on thing %d refused: out of reach (the guest drew it %.1f from "
                    "here, %.1f from itself)\n",
                    (int)e.id, slack, reach);
                fflush(stdout);
            }
            sHitsRefused++;
            return;
        }
        if (!dest->active)
        {
            Refuse(e, "the breakable is broken already");
            return;
        }
        if (sHitAt[e.id] != 0 && now - sHitAt[e.id] < kHitEvery)
        {
            Refuse(e, "too soon after the last hit on it");
            return;
        }
        if (!sBoxBucket.take(now, kBoxesPerSec))
        {
            Refuse(e, "too many hits on crates and breakables a second");
            return;
        }
        sHitAt[e.id] = now;
        F32 param[4];
        U32 ev = BreakEventOf(e.kind, param);
        zEntEvent(ent, ev, param);
        sHitsApplied++;
        if (LogOk(LOG_HIT))
        {
            printf("tssm: coop: the guest's hit on breakable thing %d (attack %d) applied%s\n",
                   (int)e.id, (int)e.kind, dest->active ? "" : "; it broke");
            fflush(stdout);
        }
    }

    // A guest's hit on enemy `nme`, with this player standing in at the
    // guest's place (the enemy is knocked away from the guest) and the
    // guest's part of the level not culled for the moment (the game's own
    // damage checks IsPresent). sLanded says whether it landed; sDeferred
    // whether it was held back (PendingHit).
    void ApplyGuestHit(xEnt* ent, zNMECommon* nme, bool tiki, en_npcdmg type, S32 amount, en_plyrpup pup,
                       const xVec3* dir)
    {
        bool stood = iCoopStandInGuest();
        bool culled = (ent->flags & 0x4) != 0;
        if (culled)
        {
            ent->flags &= (U8)~0x4;
        }
        bool vulnerable = tiki && nme->IsHealthy(); // a crate's own check: it breaks once
        sApplying = true;
        sApplyTarget = (xBase*)nme;
        sLanded = false;
        sDeferred = false;
        nme->Damage(type, stood ? (xBase*)Player() : NULL, dir, amount, pup);
        sApplying = false;
        sApplyTarget = NULL;
        if (tiki)
        {
            sLanded = vulnerable && !nme->IsHealthy();
        }
        if (culled)
        {
            ent->flags |= 0x4;
        }
        if (stood)
        {
            iCoopStandInEnd();
        }
    }

    // The enemy's current goal is its wound (it reels, and takes no damage)
    // from the other player's hit than `who`'s.
    bool ReelsFromOther(S32 id, U8 who, U64 now)
    {
        xEnt* ent = iCoopWorldEnemy(id);
        if (ent == NULL || Tiki(ent))
        {
            return false;
        }
        zNMECommon* nme = Enemy(ent);
        if (nme->psy_self == NULL || nme->psy_self->GIDOfActive() != NME_GOAL_WOUND)
        {
            return false;
        }
        U8 by = sOwnHitBy[id];
        return by != who && sOwnHitAt[by][id] != 0 && now - sOwnHitAt[by][id] < kHoldFor;
    }

    // Host, each frame: the held hits whose enemy no longer reels land now.
    void HostPendingHits(U64 now)
    {
        for (size_t k = 0; k < sPending.size();)
        {
            PendingHit p = sPending[k];
            xEnt* ent = iCoopWorldEnemy(p.id);
            zNMECommon* nme = ent != NULL ? Enemy(ent) : NULL;
            bool gone = nme == NULL || !PresentAnywhere(nme) || now - p.at > kHoldFor;
            bool reels = !gone && nme->psy_self != NULL && nme->psy_self->GIDOfActive() == NME_GOAL_WOUND;
            if (!gone && reels)
            {
                k++;
                continue;
            }
            sPending.erase(sPending.begin() + k);
            if (gone || HostBusy() != NULL)
            {
                continue;
            }
            sReplaying = true;
            if (p.who == 1)
            {
                ApplyGuestHit(ent, nme, false, p.type, p.amount, p.pup, p.haveDir ? &p.dir : NULL);
                if (sLanded)
                {
                    sHitsApplied++;
                }
                if (LogOk(LOG_HIT))
                {
                    printf("tssm: coop: the guest's hit on thing %d (attack %d, level %d) applied: %d damage%s "
                           "(held %d ms while it reeled)\n",
                           (int)p.id, (int)p.attack, (int)p.pup, (int)(p.amount < 0 ? 1 : p.amount),
                           sLanded ? (PresentAnywhere(nme) ? "" : "; the enemy is defeated") : "; it did nothing",
                           (int)((now - p.at) / kNsPerMs));
                    fflush(stdout);
                }
            }
            else
            {
                nme->Damage(p.type, Player(), p.haveDir ? &p.dir : NULL, p.amount, p.pup);
                if (iCoopLogEvery() > 0)
                {
                    printf("tssm: coop: this player's hit on thing %d, held %d ms while it reeled, lands now\n",
                           (int)p.id, (int)((now - p.at) / kNsPerMs));
                    fflush(stdout);
                }
            }
            sReplaying = false;
        }
    }

    void HostHit(const GameEvent& e, const PlayerState* partner, U64 now)
    {
        U32 scene, hash;
        S32 count;
        if (!LevelTable(&scene, &hash, &count) || e.scene != scene || e.table != hash)
        {
            Refuse(e, "another level, or another table");
            return;
        }
        if (e.id >= (U16)count)
        {
            Refuse(e, "no such thing");
            return;
        }
        xEnt* ent = iCoopWorldEnemy(e.id);
        zDestructible* dest = NULL;
        if (ent == NULL)
        {
            ent = iCoopWorldBreakable(e.id, &dest);
        }
        if (ent == NULL || ent->model == NULL || ent->model->Mat == NULL)
        {
            Refuse(e, "not an enemy or a breakable");
            return;
        }
        F32 guestPos[3];
        if (partner == NULL || partner->scene != scene || !iCoopWorldPartnerHere(guestPos))
        {
            Refuse(e, "the guest is not in this level, or its position is not believable");
            return;
        }
        if (partner->flags & (PS_DEAD | PS_PAUSED | PS_CUTSCENE))
        {
            Refuse(e, "the guest is defeated, paused or in a cutscene");
            return;
        }
        if (const char* busy = HostBusy())
        {
            Refuse(e, busy);
            return;
        }
        if (dest != NULL)
        {
            HostBreak(e, ent, dest, guestPos, partner->character, now);
            return;
        }
        en_npcdmg type;
        en_plyratak attack;
        if (!DamageOf(e.kind, partner->character, &type, &attack))
        {
            Refuse(e, e.kind == ATK_CAR ?
                          "the car's hit outside a drive level" :
                          (e.kind == ATK_ZAP ?
                               "the SpongeBowl's lightning, which the save has not got" :
                               "not an attack of the guest's character"));
            return;
        }
        const F32* at = (const F32*)&ent->model->Mat->pos;
        F32 slack = Dist(e.pos, at);
        F32 reach = Dist(guestPos, e.pos);
        if (slack > kTargetSlack || reach > ReachOf(e.kind) + iCoopBossReach(ent))
        {
            if (LogOk(LOG_HIT))
            {
                printf("tssm: coop: the guest's hit on thing %d refused: out of reach (the guest drew it %.1f from "
                       "here, %.1f from itself)\n",
                       (int)e.id, slack, reach);
                fflush(stdout);
            }
            sHitsRefused++;
            return;
        }
        zNMECommon* nme = Enemy(ent);
        if (!PresentAnywhere(nme))
        {
            Refuse(e, "the enemy is not there (defeated or hidden)");
            return;
        }
        if (sHitAt[e.id] != 0 && now - sHitAt[e.id] < kHitEvery)
        {
            Refuse(e, "too soon after the last hit on it");
            return;
        }
        bool tiki = Tiki(ent);
        if (!(tiki ? sBoxBucket.take(now, kBoxesPerSec) : sHitBucket.take(now, kHitsPerSec)))
        {
            Refuse(e, tiki ? "too many hits on crates and breakables a second" :
                             "too many hits a second");
            return;
        }
        sHitAt[e.id] = now;
        // The shared save's power-up level (one progress: the host's is the
        // guest's), as the game works the damage out for its own player.
        en_plyrpup pup = zEntPlayer_PowerupLevel(attack);
        S32 amount = attack != PLYR_ATAK_NONE ? DmgAmtForPlayerAttack(attack, pup) : -1;
        // A unit vector whatever the claim's length (the game's own are
        // unit length; a longer one must not knock the enemy further).
        F32 unit[3];
        bool haveDir = hit_dir_unit(e.dir, unit);
        xVec3 dir = { unit[0], unit[1], unit[2] };
        bool culled = (ent->flags & 0x4) != 0;
        // Reeling from the host's hit, the enemy takes no damage (its wound
        // goal, which may also turn its damage off): held as the hook would.
        U64 game = sGameNs;
        sDeferred = !tiki && ReelsFromOther(e.id, 1, game);
        if (!sDeferred)
        {
            ApplyGuestHit(ent, nme, tiki, type, amount, pup, haveDir ? &dir : NULL);
        }
        if (sDeferred)
        {
            sDeferred = false;
            bool have = false;
            for (size_t k = 0; k < sPending.size(); k++)
            {
                have = have || (sPending[k].id == (S32)e.id && sPending[k].who == 1);
            }
            if (!have && sPending.size() < 16)
            {
                PendingHit p = { (S32)e.id, 1, type, amount, pup, haveDir, dir, e.kind, game };
                sPending.push_back(p);
            }
            if (LogOk(LOG_HIT))
            {
                printf("tssm: coop: the guest's hit on thing %d (attack %d) held: the enemy reels from this "
                       "player's hit; it lands when that ends\n",
                       (int)e.id, (int)e.kind);
                fflush(stdout);
            }
            return;
        }
        if (!sLanded)
        {
            sHitsNoEffect++;
            if (LogOk(LOG_HIT))
            {
                printf(
                    "tssm: coop: the guest's hit on thing %d (attack %d) did nothing: the enemy cannot be "
                    "hurt just now (still recovering from the guest's last hit, or this attack does not hurt it)\n",
                    (int)e.id, (int)e.kind);
                fflush(stdout);
            }
            return;
        }
        sHitsApplied++;
        if (LogOk(LOG_HIT))
        {
            printf(
                "tssm: coop: the guest's hit on thing %d (attack %d, level %d) applied: %d damage%s%s\n",
                (int)e.id, (int)e.kind, (int)e.level, (int)(amount < 0 ? 1 : amount),
                PresentAnywhere(nme) ? "" : "; the enemy is defeated",
                culled ? " (in a part of the level culled here)" : "");
            if (e.level != (U8)(pup < kPowerupMin ? kPowerupMin :
                                                    (pup > kPowerupMax ? kPowerupMax : pup)) &&
                attack != PLYR_ATAK_NONE && attack != PLYR_ATAK_SLIDE)
            {
                printf("tssm: coop: (the guest claimed level %d; the shared save's is %d)\n",
                       (int)e.level, (int)pup);
            }
            fflush(stdout);
        }
    }

    // --- the host: the guest's pickups ----------------------------------------------------------

    void Grant(const GameEvent& e, U8 result)
    {
        GameEvent g;
        memset(&g, 0, sizeof(g));
        g.type = EV_GRANT;
        g.scene = e.scene;
        g.table = e.table;
        g.id = e.id;
        g.kind = result;
        if (!iCoopPushEvent(g) && LogOk(LOG_PICK))
        {
            printf("tssm: coop: the answer for pickup %d could not be queued\n", (int)e.id);
        }
    }

    // The guest took `p`: this game counts it for its own progress (its links
    // fire as when its own player takes one), as zEntPickup_GivePickup does,
    // without what that does to this game's player (sounds, the reward
    // animation, health).
    void Credit(zEntPickup* p)
    {
        xEntHide(p);
        zEntPickup_SetState(p, 0x8);
        if (p->pickupFlags & 0x1)
        {
            p->timer = 10.0f; // one that comes back, as when taken here
        }
        zEntEvent(p, eEventPickup); // no parameters: the pickup's own handler leaves it be
        const zAssetPickup* a = p->p;
        zPlayerGlobals& g = globals.player;
        if (a->pickupType == 1)
        {
            S32 value = 0;
            switch (a->pickupIndex)
            {
            case 4: value = g.g.ShinyValuePurple; break;
            case 3: value = g.g.ShinyValueBlue; break;
            case 2: value = g.g.ShinyValueGreen; break;
            case 1: value = g.g.ShinyValueYellow; break;
            default: value = g.g.ShinyValueRed; break;
            }
            if (g.rewardsMgr != NULL && value > 0)
            {
                g.rewardsMgr->IncManlinessPoints((U32)value, false);
            }
            if (g.InvManlinessPoints > 99999)
            {
                g.InvManlinessPoints = 99999;
            }
        }
        else if (a->pickupIndex == 2)
        {
            g.InvGoofyGoober++;
            if (globals.autoSaveFeature)
            {
                zSaveLoadPreAutoSave(true);
            }
            // The rest once the guest's reward animation is over, as this
            // game's own token does it (SpatulaGrabStopCB): the autosave, and
            // the level's dispatchers told (a main task's token ends the level).
            if (sTokensDone.size() < 16)
            {
                sTokensDone.push_back(p);
            }
            sTokenDoneAt = iCoopNowNs() + kTokenDoneAfter;
            sTokenDoneScene = globals.sceneCur != NULL ? globals.sceneCur->sceneID : 0;
        }
        else if (a->pickupIndex == 4)
        {
            g.InvTreasureChest[zSceneGetLevelIndex()]++;
            g.InvTreasureChestCurrentLevel = g.InvTreasureChest[zSceneGetLevelIndex()];
            zUICustomExtrasManager::GivePlayerTreasureChest(1);
        }
        // Health (index 3) is the guest's own: its game adds it.
    }

    void HostPickup(const GameEvent& e, const PlayerState* partner, U64 now)
    {
        const char* why = NULL;
        F32 guestPos[3];
        bool here = false;
        if (!PickTableCurrent() || e.scene != sPickSceneId || e.table != sPickHash || e.id >= sPicks.size())
        {
            why = "another level, or another table";
        }
        else if (partner == NULL || partner->scene != sPickSceneId ||
                 (partner->flags & (PS_DEAD | PS_PAUSED | PS_CUTSCENE)))
        {
            why = "the guest is not playing in this level";
        }
        else if (!(here = iCoopWorldPartnerHere(guestPos)) && e.kind != PW_GIVEN)
        {
            why = "the guest is not playing in this level";
        }
        else if (HostBusy() != NULL)
        {
            why = HostBusy();
        }
        else if (e.kind == PW_TOUCHED)
        {
            if (!PickAvailable(sPicks[e.id].ent))
            {
                why = "it is gone";
            }
            else if (Dist(guestPos, (const F32*)&sPicks[e.id].ent->model->Mat->pos) > kPickupReach)
            {
                why = "too far from the guest";
            }
        }
        else
        {
            // One the guest's own game showed (its task or challenge) or gave
            // (its script): the host's copy is not out, so the host cannot
            // check the guest's win; it takes only a token or a chest that
            // this level's own data can show or give, once each, seldom.
            const Pick& k = sPicks[e.id];
            U8 need = e.kind == PW_REVEALED ? CAN_REVEAL : CAN_GIVE;
            if (!TokenOrChest(k.ent))
            {
                why = "not a token or a chest";
            }
            else if (PickTaken(k.ent))
            {
                why = "it is gone";
            }
            else if (!(k.can & need))
            {
                why = e.kind == PW_REVEALED ? "nothing in this level shows it" :
                                              "nothing in this level gives it";
            }
            else if (e.kind == PW_REVEALED &&
                     Dist(guestPos, (const F32*)&k.ent->model->Mat->pos) > kPickupReach)
            {
                why = "too far from the guest";
            }
            else if (sOwnPickupAt != 0 && now - sOwnPickupAt < kOwnPickupEvery)
            {
                why = "too soon after the last one the guest's game showed or gave";
            }
        }
        if (why == NULL && !sPickBucket.take(now, kPickupsPerSec))
        {
            why = "too many a second";
        }
        if (why != NULL)
        {
            if (LogOk(LOG_PICK))
            {
                printf("tssm: coop: the guest's pickup %d refused: %s\n", (int)e.id, why);
                fflush(stdout);
            }
            if (e.id < kMaxPickups)
            {
                Grant(e, GR_NO);
            }
            return;
        }
        zEntPickup* p = sPicks[e.id].ent;
        if (e.kind != PW_TOUCHED)
        {
            sOwnPickupAt = now;
            p->pickupFlags |= 0x2; // out here too, for its own links, as the guest's game had it
            xEntShow(p);
        }
        Credit(p);
        sPickOutDirty = true;
        Grant(e, GR_YOURS);
        if (LogOk(LOG_PICK))
        {
            static const char* const kWay[PW_END] = { "", "; its own game showed it",
                                                      "; its own game gave it" };
            printf(
                "tssm: coop: the guest took pickup %d (type %d index %d)%s; counted here (shiny objects %u, "
                "tokens %u)\n",
                (int)e.id, (int)p->p->pickupType, (int)p->p->pickupIndex,
                kWay[e.kind < PW_END ? e.kind : 0], (unsigned)globals.player.InvManlinessPoints,
                (unsigned)globals.player.InvGoofyGoober);
            fflush(stdout);
        }
    }

    // --- the guest: damage and grants from the host -----------------------------------------------

    void GuestDamage(const GameEvent& e, U64 now)
    {
        zPlayer* pl = Player();
        if (pl == NULL || globals.player.s == NULL || globals.sceneCur == NULL || e.scene != globals.sceneCur->sceneID)
        {
            return; // for another level: it no longer applies
        }
        if (globals.player.s->Health == 0 || sRespawnPending || zGameIsPaused() || globals.cmgr != NULL)
        {
            // Defeated (its defeat plays out once), paused or in a cutscene:
            // the single-player game never hurts its player then.
            if (LogOk(LOG_DAMAGE))
            {
                printf("tssm: coop: a hit from the host dropped (this player is defeated, paused or in a "
                       "cutscene)\n");
                fflush(stdout);
            }
            return;
        }
        if (!sDamageBucket.take(now, kDamagePerSec))
        {
            if (LogOk(LOG_DAMAGE))
            {
                printf("tssm: coop: a hit from the host dropped (too many a second)\n");
            }
            return;
        }
        xBase* src = NULL;
        if (e.id != kNoThing)
        {
            src = (xBase*)iCoopWorldEnemy(e.id); // range-checked; NULL when not this level's enemy
        }
        U32 before = globals.player.s->Health;
        xVec3 v = { e.pos[0], e.pos[1], e.pos[2] };
        S32 landed;
        switch (e.kind)
        {
        case DK_FROM:
            landed = zEntPlayer_DamageNPCKnockBack(src, e.level, &v, (ePlayerNMEDamage)e.sub);
            break;
        case DK_PUSH:
            landed = zEntPlayer_Damage(src, e.level, &v);
            break;
        default:
            landed = zEntPlayer_Damage(src, e.level);
            break;
        }
        sDamageApplied++;
        if (LogOk(LOG_DAMAGE))
        {
            printf("tssm: coop: the host says an enemy hit this player: thing %d, %d damage, kind %d; %s (health %u "
                   "to %u)\n",
                   e.id == kNoThing ? -1 : (int)e.id, (int)e.level, (int)e.kind, landed ? "it lands" : "no effect now",
                   (unsigned)before, (unsigned)globals.player.s->Health);
            fflush(stdout);
        }
    }

    void GuestGrant(const GameEvent& e)
    {
        if (!PickTableCurrent() || e.scene != sPickSceneId || e.table != sPickHash || e.id >= sPicks.size())
        {
            return;
        }
        Pick& k = sPicks[e.id];
        if (e.kind != GR_YOURS)
        {
            // It may be asked again, once a second at most (it comes back, or
            // the claim came a moment too early).
            if (LogOk(LOG_PICK))
            {
                printf("tssm: coop: the host said no to pickup %d\n", (int)e.id);
            }
            return;
        }
        zEntPickup* p = k.ent;
        if (PickState(p) == 0x4)
        {
            return; // already on its way
        }
        if (!PickAvailable(p))
        {
            // Hidden here because it is gone on the host (it is: this player
            // took it): back for the moment it flies to this player.
            zEntPickup_SetState(p, 0x1);
            p->pickupFlags |= 0x2;
            xEntShow(p);
        }
        k.hiddenByHost = false;
        if (TokenOrChest(p) && p->p->pickupIndex == 2 && !k.givenAsked)
        {
            // A token this player touched: the touch's own way, whose links
            // (the token's task, the level's end) run in the reward.
            zEntPickup_PcTakeTouched(p);
        }
        else
        {
            zEntPickup_DoPickup(
                p, false); // its flight to this player, then the game's own reward (health too)
        }
        k.givenAsked = false;
        if (LogOk(LOG_PICK))
        {
            printf("tssm: coop: the host gave this player pickup %d (type %d index %d)\n", (int)e.id,
                   (int)p->p->pickupType, (int)p->p->pickupIndex);
            fflush(stdout);
        }
    }

    // The guest: ask the host for pickup `i`, which this game's own script gave.
    void AskGiven(S32 i, U64 now)
    {
        Pick& k = sPicks[i];
        k.askedAt = now;
        k.givenAsked = true;
        k.givenTries++;
        GameEvent e;
        memset(&e, 0, sizeof(e));
        e.type = EV_PICKUP;
        e.scene = sPickSceneId;
        e.table = sPickHash;
        e.id = (U16)i;
        e.kind = PW_GIVEN;
        if (iCoopPushEvent(e) && LogOk(LOG_PICK))
        {
            printf("tssm: coop: pickup %d asked of the host (this game's own script gives it%s)\n",
                   (int)i, k.givenTries > 1 ? "; again" : "");
            fflush(stdout);
        }
    }

    // The guest: a given one the host has not granted yet is asked again (a
    // refusal while the host was busy, or too soon after another), until
    // granted, taken on the host, or the level changes.
    void GuestAskGivenAgain(U64 now)
    {
        bool playing = globals.player.s != NULL && globals.player.s->Health != 0 &&
                       !sRespawnPending && !zGameIsPaused() && globals.cmgr == NULL;
        bool have = sPickInHave && sPickIn.scene == sPickSceneId && sPickIn.table == sPickHash &&
                    sPickIn.count == sPicks.size();
        for (size_t i = 0; i < sPicks.size() && playing; i++)
        {
            Pick& k = sPicks[i];
            if (!k.givenAsked || k.givenTries >= kGivenTries || now - k.askedAt < kPickupAskEvery ||
                (have && ((sPickIn.bits[i >> 3] >> (i & 7)) & 1)))
            {
                continue;
            }
            AskGiven((S32)i, now);
        }
    }

    // The guest's copies follow the host's PICKUPS.
    void GuestApplyPicks()
    {
        if (!sPickInHave || !PickTableCurrent() || sPickIn.scene != sPickSceneId || sPickIn.table != sPickHash ||
            sPickIn.count != sPicks.size())
        {
            return;
        }
        for (size_t i = 0; i < sPicks.size(); i++)
        {
            Pick& k = sPicks[i];
            zEntPickup* p = k.ent;
            bool taken = (sPickIn.bits[i >> 3] >> (i & 7)) & 1;
            bool there = (sPickIn.avail[i >> 3] >> (i & 7)) & 1;
            U32 st = PickState(p);
            if (taken && !PickTaken(p))
            {
                zEntPickup_SetState(p, 0x8);
                xEntHide(p);
                k.hiddenByHost = true;
                sPickHostChanged = true;
                if (LogOk(LOG_PICKSTATE))
                {
                    printf("tssm: coop: pickup %d is gone on the host; hidden here\n", (int)i);
                }
            }
            else if (!taken && st == 0x8)
            {
                // Taken here before (this player's own save, or the host's
                // level reset brought it back): the host's is there.
                zEntPickup_SetState(p, 0x1);
                p->pickupFlags |= 0x2;
                xEntShow(p);
                k.hiddenByHost = false;
                sPickHostChanged = true;
                if (LogOk(LOG_PICKSTATE))
                {
                    printf("tssm: coop: pickup %d is not taken on the host; shown here\n", (int)i);
                }
            }
            else if (there && (st == 0x1 || st == 0x2) &&
                     !((p->flags & 0x1) && (p->pickupFlags & 0x2)))
            {
                // The host's game showed it (its task, its challenge): so is
                // it here. One only the guest's game shows stays (it may be
                // asked of the host, PW_REVEALED).
                p->pickupFlags |= 0x2;
                xEntShow(p);
                k.shownByHost = true;
                sPickHostChanged = true;
                if (LogOk(LOG_PICKSTATE))
                {
                    printf("tssm: coop: pickup %d is shown on the host; shown here\n", (int)i);
                }
            }
            else if (k.shownByHost && !there && !taken && (st == 0x1 || st == 0x2))
            {
                // The host's game hid it again (a challenge failed): so here.
                p->pickupFlags &= (U8)~0x2;
                xEntHide(p);
                k.shownByHost = false;
                if (LogOk(LOG_PICKSTATE))
                {
                    printf("tssm: coop: pickup %d is hidden again on the host; hidden here\n",
                           (int)i);
                }
            }
        }
    }

    void HostPicks(U64 now)
    {
        if (!PickTableCurrent())
        {
            return;
        }
        Pickups cur;
        memset(&cur, 0, sizeof(cur));
        cur.scene = sPickSceneId;
        cur.table = sPickHash;
        cur.count = (U16)sPicks.size();
        for (size_t i = 0; i < sPicks.size(); i++)
        {
            if (PickTaken(sPicks[i].ent))
            {
                cur.bits[i >> 3] |= (U8)(1u << (i & 7));
            }
            else if (PickAvailable(sPicks[i].ent))
            {
                cur.avail[i >> 3] |= (U8)(1u << (i & 7));
            }
        }
        if (memcmp(&cur, &sPickOut, sizeof(cur)) != 0)
        {
            sPickOut = cur;
            sPickOutDirty = true;
        }
        (void)now;
    }

    // --- one progress (docs/MULTIPLAYER.md 13) -------------------------------------------------

    U8 Clamp8(S32 v, S32 hi)
    {
        return (U8)(v < 0 ? 0 : (v > hi ? hi : v));
    }

    // Host: this save's progress as PROGRESS carries it.
    void HostProgress(U64 now)
    {
        const zPlayerGlobals& g = globals.player;
        Progress cur;
        memset(&cur, 0, sizeof(cur));
        cur.tokens = (U16)(g.InvGoofyGoober > kMaxTokens ? kMaxTokens : g.InvGoofyGoober);
        cur.shiny = g.InvManlinessPoints > kMaxShiny ? kMaxShiny : g.InvManlinessPoints;
        cur.range = Clamp8((S32)g.ManlinessPointsCurrentRange, kMaxRangeIdx);
        cur.unused = Clamp8(g.CurrentUnusedSkillPoints, kMaxRange);
        S32 spent = g.CurrentSpentSkillPoints;
        cur.spent =
            (S8)(spent < -(S32)kMaxRange ? -(S32)kMaxRange :
                                           (spent > (S32)kMaxRange ? (S32)kMaxRange : spent));
        for (S32 i = 0; i < kPowerUps; i++)
        {
            cur.powerup[i] = Clamp8(g.g.PowerUp[i], i < kMoves ? 2 : 3);
        }
        U32 level = zSceneGetLevelIndex();
        cur.chests = Clamp8(level < 20 ? (S32)g.InvTreasureChest[level] : 0, kMaxChests);
        if (!sProgReady || memcmp(&cur, &sProgOut, sizeof(cur)) != 0)
        {
            sProgOut = cur;
            sProgOutDirty = true;
        }
        sProgReady = true;
        (void)now;
    }

    void SaveOwnProgress()
    {
        const zPlayerGlobals& g = globals.player;
        OwnProgress& o = sOwn;
        o.tokens = g.InvGoofyGoober;
        o.shiny = g.InvManlinessPoints;
        o.range = g.ManlinessPointsCurrentRange;
        o.chestTotal = g.InvTreasureChestTotal;
        o.chestOpened = g.InvTreasureChestOpened;
        o.chestLevel = g.InvTreasureChestCurrentLevel;
        memcpy(o.chest, g.InvTreasureChest, sizeof(o.chest));
        o.unused = g.CurrentUnusedSkillPoints;
        o.spent = g.CurrentSpentSkillPoints;
        memcpy(o.powerup, g.g.PowerUp, sizeof(o.powerup));
        o.sbMax = g.sb.MaxHealth;
        o.patMax = g.patrick.MaxHealth;
        sOwnSaved = true;
        sShareScene = globals.sceneCur != NULL ? globals.sceneCur->sceneID : 0;
        sHostAwaySince = 0;
        memset(&sProgLogged, 0xFF, sizeof(sProgLogged)); // the first from the host is logged
        printf(
            "tssm: coop: this player's own progress kept aside in the host's level (tokens %u, shiny objects "
            "%u, moves %u %u %u %u %u %u %u %u)\n",
            (unsigned)o.tokens, (unsigned)o.shiny, o.powerup[0], o.powerup[1], o.powerup[2],
            o.powerup[3], o.powerup[4], o.powerup[5], o.powerup[6], o.powerup[7]);
        fflush(stdout);
    }

    // For the log: a save block's words, hashed.
    U32 BlockHash(const S32* w, size_t n)
    {
        U32 h = 2166136261u;
        for (size_t i = 0; i < n; i++)
        {
            h = (h ^ (U32)w[i]) * 16777619u;
        }
        return h;
    }

    // Guest, the first frame the host is in this level (before its PICKUPS or
    // PROGRESS change anything here): this level as this game has it, its
    // pickups (PutPicksBack) and its save block (iCoopCombatPutOwnBlock).
    void KeepOwnLevel(U32 scene)
    {
        if (sOwnSaved || globals.sceneCur == NULL || globals.sceneCur->sceneID != scene)
        {
            return;
        }
        sOwnLevelScene = scene;
        for (size_t i = 0; i < sPicks.size() && PickTableCurrent(); i++)
        {
            Pick& k = sPicks[i];
            k.ownState = (U8)PickState(k.ent);
            k.ownFlags = k.ent->flags & 0x1;
            k.ownPickFlags = k.ent->pickupFlags & 0x2;
            k.shownByHost = k.hiddenByHost = false;
        }
        // The block from this game's own state now, as an autosave writes it
        // (this game's own progress too: the host's is not shown yet).
        zSceneSave(globals.sceneCur);
        xSerial s;
        s.setClient(scene);
        const st_SERIAL_CLIENTINFO* c = s.ctxtdata;
        sOwnBlockScene = 0;
        sOwnBlock.clear();
        if (c != NULL && c->membuf != NULL && c->actsize > 0)
        {
            sOwnBlock.assign(c->membuf, c->membuf + c->actsize / 4);
            sOwnBlockScene = scene;
        }
        if (iCoopLogEvery() > 0)
        {
            printf(
                "tssm: coop: this level as this game has it kept aside (%d pickups, save block %d bytes, hash "
                "%08x)\n",
                (int)(PickTableCurrent() ? sPicks.size() : 0), (int)(sOwnBlock.size() * 4),
                (unsigned)BlockHash(sOwnBlock.data(), sOwnBlock.size()));
            fflush(stdout);
        }
    }

    void ClampHealth()
    {
        if (globals.player.s != NULL && globals.player.s->Health > globals.player.s->MaxHealth)
        {
            globals.player.s->Health = globals.player.s->MaxHealth;
        }
    }

    // Guest: the host's progress is this game's while it is in the host's level.
    void MirrorProgress(const Progress& in)
    {
        if (!sOwnSaved)
        {
            SaveOwnProgress();
        }
        zPlayerGlobals& g = globals.player;
        bool moves = memcmp(g.g.PowerUp, in.powerup, kPowerUps) != 0;
        g.InvGoofyGoober = in.tokens;
        g.InvManlinessPoints = in.shiny;
        g.ManlinessPointsCurrentRange = in.range;
        g.CurrentUnusedSkillPoints = in.unused;
        g.CurrentSpentSkillPoints = in.spent;
        U32 level = zSceneGetLevelIndex();
        if (level < 20)
        {
            // The host's count is for its own level, this one.
            g.InvTreasureChest[level] = in.chests;
            g.InvTreasureChestCurrentLevel = in.chests;
        }
        if (moves)
        {
            memcpy(g.g.PowerUp, in.powerup, kPowerUps);
            g.sb.MaxHealth = g.g.PowerUp[8] + 3;
            g.patrick.MaxHealth = g.g.PowerUp[9] + 3;
            ClampHealth();
            zMenuSB_UpdatePowerUpMenu();
        }
        if (memcmp(&in, &sProgLogged, sizeof(in)) != 0 && LogOk(LOG_PROGRESS))
        {
            printf(
                "tssm: coop: progress from the host: tokens %u, shiny objects %u, upgrade points %u (spent %d), "
                "moves %u %u %u %u %u %u %u %u, health %u %u\n",
                (unsigned)in.tokens, (unsigned)in.shiny, (unsigned)in.unused, (int)in.spent,
                in.powerup[0], in.powerup[1], in.powerup[2], in.powerup[3], in.powerup[4],
                in.powerup[5], in.powerup[6], in.powerup[7], in.powerup[8], in.powerup[9]);
            fflush(stdout);
            sProgLogged = in;
        }
    }

    // Guest: this level's pickups as this game had them when the host's world
    // first reached it here (KeepOwnLevel). Nothing taken since counts for
    // this player's own progress (the host's save counted what it took), so
    // the level is this player's own again from here on, and its save gets it
    // so.
    void PutPicksBack()
    {
        if (!PickTableCurrent() || sOwnLevelScene == 0 || sPickSceneId != sOwnLevelScene)
        {
            return;
        }
        S32 back = 0;
        for (size_t i = 0; i < sPicks.size(); i++)
        {
            Pick& k = sPicks[i];
            zEntPickup* p = k.ent;
            bool ownTaken = k.ownState == 0x4 || k.ownState == 0x8;
            if (PickState(p) == 0x4)
            {
                continue; // on its way to this player: its flight ends it
            }
            if (ownTaken != PickTaken(p))
            {
                zEntPickup_SetState(p, ownTaken ? 0x8 : 0x1);
                back++;
            }
            else if ((p->flags & 0x1) == k.ownFlags && (p->pickupFlags & 0x2) == k.ownPickFlags)
            {
                k.hiddenByHost = k.shownByHost = false;
                continue;
            }
            else
            {
                back++;
            }
            p->pickupFlags = (U8)((p->pickupFlags & ~0x2) | k.ownPickFlags);
            if (k.ownFlags)
            {
                xEntShow(p);
            }
            else
            {
                xEntHide(p);
            }
            k.hiddenByHost = k.shownByHost = false;
        }
        sOwnLevelScene = 0;
        sPickHostChanged = false;
        if (back > 0)
        {
            printf("tssm: coop: %d of this level's pickups are as this game had them again\n",
                   (int)back);
            fflush(stdout);
        }
    }

    // Guest: this player's own progress back, as it was on entering the
    // host's level, and the level's pickups (PutPicksBack).
    void RestoreOwnProgress(const char* why)
    {
        PutPicksBack();
        if (!sOwnSaved)
        {
            return;
        }
        sOwnSaved = false;
        sShareScene = 0;
        sHostAwaySince = 0;
        zPlayerGlobals& g = globals.player;
        const OwnProgress& o = sOwn;
        g.InvGoofyGoober = o.tokens;
        g.InvManlinessPoints = o.shiny;
        g.ManlinessPointsCurrentRange = o.range;
        g.InvTreasureChestTotal = o.chestTotal;
        g.InvTreasureChestOpened = o.chestOpened;
        g.InvTreasureChestCurrentLevel = o.chestLevel;
        memcpy(g.InvTreasureChest, o.chest, sizeof(o.chest));
        g.CurrentUnusedSkillPoints = o.unused;
        g.CurrentSpentSkillPoints = o.spent;
        memcpy(g.g.PowerUp, o.powerup, sizeof(o.powerup));
        g.sb.MaxHealth = o.sbMax;
        g.patrick.MaxHealth = o.patMax;
        ClampHealth();
        zMenuSB_UpdatePowerUpMenu();
        printf(
            "tssm: coop: this player's own progress is back (%s): tokens %u, shiny objects %u, moves %u %u %u %u %u "
            "%u %u %u\n",
            why, (unsigned)o.tokens, (unsigned)o.shiny, o.powerup[0], o.powerup[1], o.powerup[2],
            o.powerup[3], o.powerup[4], o.powerup[5], o.powerup[6], o.powerup[7]);
        fflush(stdout);
    }

    void UpgradeAnswer(const GameEvent& e, U8 result)
    {
        GameEvent a;
        memset(&a, 0, sizeof(a));
        a.type = EV_UPGRADE_ACK;
        a.scene = e.scene;
        a.kind = e.kind;
        a.sub = e.sub;
        a.level = result;
        iCoopPushEvent(a);
    }

    // Host: a move the guest's game would have given its player. The host's
    // save is the shared one: the host's game rules decide, on its own
    // numbers, and the change reaches the guest with PROGRESS.
    void HostUpgrade(const GameEvent& e, const PlayerState* partner, U64 now)
    {
        const char* why = NULL;
        zPlayerGlobals& g = globals.player;
        U32 scene = globals.sceneCur != NULL ? globals.sceneCur->sceneID : 0;
        U8 i = e.sub;
        bool already = false;
        if (partner == NULL || partner->scene != scene || e.scene != scene)
        {
            why = "the guest is not in this level";
        }
        else if (gGameState != eGameState_Play)
        {
            why = "this game is between levels";
        }
        else if (e.kind == UPG_BOUGHT && sStatesSincePaused > kPausedStates)
        {
            why = "the guest's pause menu is not open (none of its last 66 states said paused)";
        }
        else if ((sUpgAt[i] != 0 && now - sUpgAt[i] < kUpgradeEvery) ||
                 !sUpgBucket.take(now, kUpgradesPerSec))
        {
            why = "too many a second";
        }
        else if (e.kind == UPG_TAUGHT)
        {
            if (g.g.PowerUp[i] != 0)
            {
                already = true; // this game's own talk taught it too
            }
            else if (!(sTeaches & (1u << i)))
            {
                why = "nothing in this level teaches that move";
            }
            else if (!TokenGateHolds())
            {
                why = "the shared save has not got the tokens for it";
            }
        }
        else if (g.CurrentUnusedSkillPoints <= 0)
        {
            why = "no upgrade points left";
        }
        else if (i < kMoves ? !(g.g.PowerUp[i] != 0 && g.g.PowerUp[i] < 2) : g.g.PowerUp[i] >= 3)
        {
            why = "that move cannot be upgraded";
        }
        if (why == NULL)
        {
            sUpgAt[i] = now;
        }
        if (why == NULL && !already)
        {
            if (e.kind == UPG_TAUGHT)
            {
                g.g.PowerUp[i] = 1;
            }
            else
            {
                // zEntPlayerEventCB's eEventUpgradePowerUp, without this
                // game's own film of it (the guest's game plays that).
                if (i == 8)
                {
                    g.sb.MaxHealth++;
                }
                else if (i == 9)
                {
                    g.patrick.MaxHealth++;
                }
                g.g.PowerUp[i]++;
                g.CurrentUnusedSkillPoints--;
                g.CurrentSpentSkillPoints++;
            }
            zMenuSB_UpdatePowerUpMenu();
        }
        UpgradeAnswer(e, why == NULL ? GR_YOURS : GR_NO);
        if (LogOk(LOG_PROGRESS))
        {
            if (why != NULL)
            {
                printf("tssm: coop: the guest's %s of move %d refused: %s\n",
                       e.kind == UPG_TAUGHT ? "lesson" : "upgrade", (int)i, why);
            }
            else
            {
                printf(
                    "tssm: coop: the guest's %s of move %d %s: now level %d (upgrade points %d, spent %d)\n",
                    e.kind == UPG_TAUGHT ? "lesson" : "upgrade", (int)i,
                    already ? "taken (had it already)" : "applied", (int)g.g.PowerUp[i],
                    (int)g.CurrentUnusedSkillPoints, (int)g.CurrentSpentSkillPoints);
            }
            fflush(stdout);
        }
    }

    void GuestUpgradeAnswer(const GameEvent& e, U64 now)
    {
        // Only the answer to an ask of this game's own, once.
        U8 i = e.sub;
        if (sUpgAskKind[i] != e.kind || sUpgAt[i] == 0 || now - sUpgAt[i] > kUpgradeAnswerWithin)
        {
            return;
        }
        sUpgAskKind[i] = 0;
        if (e.level == GR_YOURS)
        {
            if (e.kind == UPG_BOUGHT)
            {
                // The film the game plays for an upgrade; the new level comes
                // with PROGRESS.
                UpgradeSparkleEffect::PlayUpgradeFMV(e.sub);
            }
            if (LogOk(LOG_PROGRESS))
            {
                printf("tssm: coop: the host's game gave this player move %d (%s)\n", (int)e.sub,
                       e.kind == UPG_BOUGHT ? "upgrade" : "lesson");
                fflush(stdout);
            }
            return;
        }
        iCoopAddNotice(e.kind == UPG_BOUGHT ? "the host's game said no to that upgrade" :
                                              "the host's game said no to that move");
        if (LogOk(LOG_PROGRESS))
        {
            printf("tssm: coop: the host's game said no to move %d\n", (int)e.sub);
            fflush(stdout);
        }
    }

    // --- the guest's respawn -------------------------------------------------------------------

    void BubbleBurst(xEnt* e)
    {
        if (e == NULL || e->model == NULL || e->model->Mat == NULL)
        {
            return;
        }
        const xVec3& c = *(const xVec3*)&e->model->Mat->pos;
        for (S32 i = 0; i < 24; i++)
        {
            F32 a = (F32)i * (6.2831853f / 24.0f);
            F32 y = 0.2f + 0.08f * (F32)(i % 6);
            xVec3 p = { c.x + 0.9f * cosf(a), c.y + y * 6.0f, c.z + 0.9f * sinf(a) };
            zFX_SpawnBubbleTrail(&p, 3);
        }
    }

    void GuestRespawnNow(const PlayerState* host)
    {
        zPlayer* pl = Player();
        if (pl == NULL || globals.player.s == NULL)
        {
            return;
        }
        if (host == NULL)
        {
            // No host to come back to after all: the game's own rules.
            printf("tssm: coop: no host to come back beside; this player loses a life as in single-player\n");
            fflush(stdout);
            zGameStateSwitch(eGameState_LoseChance);
            return;
        }
        // What the game's own level reset does for the player, without the
        // level: control back, the player's own reset (animation, camera,
        // the fade back in), shown again, not dead for the rewards.
        globals.player.ControlOff = 0;
        pl->Reset();
        if (globals.player.rewardsMgr != NULL)
        {
            globals.player.rewardsMgr->playerDead = FALSE;
        }
        xEntShow(pl);
        globals.player.Visible = 1;
        if (globals.player.s->Health == 0)
        {
            globals.player.s->Health = globals.player.s->MaxHealth;
        }
        iCoopWorldPlaceBeside(*host, "respawned in a bubble beside the host");
        globals.player.DamageTimer = kRespawnSafe;
        BubbleBurst(pl);
        sRespawns++;
        printf("tssm: coop: this player is back (respawn %u), health %u; nothing was reset\n", (unsigned)sRespawns,
               (unsigned)globals.player.s->Health);
        fflush(stdout);
    }
    // Host: the level signal of the tokens the guest took, once their reward
    // is over and this player plays (a defeat, a pause or a cutscene in
    // between: when it is over), with or without the guest. With co-op off
    // it still runs every frame (iCoopFrame's iCoopWorldReset, through
    // iCoopCombatReset); leaving the level drops it (HostTokensDrop).
    void HostTokensDone(U64 now)
    {
        if (sTokensDone.empty() || now < sTokenDoneAt || HostBusy() != NULL ||
            gGameState != eGameState_Play || globals.sceneCur == NULL ||
            globals.sceneCur->sceneID != sTokenDoneScene)
        {
            return;
        }
        std::vector<zEntPickup*> done;
        done.swap(sTokensDone);
        bool reset = sTokensReset;
        sTokensReset = false;
        sTokenDoneAt = 0;
        S32 again = 0;
        if (reset)
        {
            // This player's defeat (or a fall) reset the level meanwhile,
            // which a token's reward never lets happen: what the token's own
            // links did to things the level's save does not keep (the
            // dispatcher of a main task's end, its counters) is undone. They
            // are sent again as the token sent them (not its sounds).
            for (size_t i = 0; i < done.size(); i++)
            {
                zEntPickup* p = done[i];
                for (U32 k = 0; k < p->linkCount && p->link != NULL; k++)
                {
                    const xLinkAsset& l = p->link[k];
                    xBase* to = l.srcEvent == eEventPickup && l.chkAssetID == 0 ?
                                    zSceneFindObject(l.dstAssetID) :
                                    NULL;
                    if (to == NULL || (to->baseFlags & 0x2) || to->baseType == eBaseTypeSoundFX ||
                        to->baseType == eBaseTypeSoundGroup)
                    {
                        continue;
                    }
                    xBase* w =
                        l.paramWidgetAssetID != 0 ? zSceneFindObject(l.paramWidgetAssetID) : NULL;
                    zEntEvent(p, eEventPickup, to, l.dstEvent, l.param, w, l.paramWidgetAssetID,
                              FE_NO);
                    again++;
                }
            }
        }
        if (globals.autoSaveFeature)
        {
            zSaveLoad_TriggerAutoSave();
        }
        for (size_t k = 0; k < done.size(); k++)
        {
            zEntEventAllOfType(eEventTokenPickupComplete, eBaseTypeDispatcher);
        }
        printf(
            "tssm: coop: the guest's token: its reward is over, the level goes on (as for this player's own)%s",
            done.size() > 1 ? " (two or more)" : "");
        if (reset)
        {
            printf("; the level was reset meanwhile: %d of the token's links sent again",
                   (int)again);
        }
        printf(" (clock ms %llu)\n", (unsigned long long)(now / kNsPerMs));
        fflush(stdout);
    }

    // Host, leaving the level: a token's signal still due goes with it, and
    // the autosave it held ready is off again.
    void HostTokensDrop()
    {
        sTokensReset = false;
        if (sTokensDone.empty())
        {
            return;
        }
        sTokensDone.clear();
        sTokenDoneAt = 0;
        zSaveLoadPreAutoSave(false);
        printf(
            "tssm: coop: the guest's token's level signal was still due as the level ended; dropped\n");
        fflush(stdout);
    }
} // namespace

// --- iCoopWorldInternal.h ---------------------------------------------------------------------

void iCoopCombatOffline()
{
    // The defeat hook ran (the game will not switch to its lost chance by
    // itself again) but the session ended before this frame's respawn: the
    // game's own rules, or the player would stay down for good.
    if (sRespawnPending)
    {
        sRespawnPending = false;
        GuestRespawnNow(NULL);
    }
    sInvulnOn = false;
    HostTokensDone(iCoopNowNs());
    iCoopCombatProgressDone("the session is over");
}

bool iCoopCombatHostChangedLevel()
{
    zScene* sc = globals.sceneCur;
    return sPickHostChanged || (sc != NULL && ((sOwnSaved && sc->sceneID == sShareScene) ||
                                               sc->sceneID == sOwnLevelScene));
}

void iCoopCombatHostHere(U32 scene)
{
    if (Guest() && scene != 0 && sOwnLevelScene != scene)
    {
        if (globals.sceneCur != sPickBuiltScene ||
            (globals.sceneCur != NULL && globals.sceneCur->sceneID != sPickBuiltId))
        {
            BuildPicks();
        }
        KeepOwnLevel(scene);
    }
}

bool iCoopCombatPutOwnBlock(U32 scene)
{
    if (scene == 0 || scene != sOwnBlockScene)
    {
        return false;
    }
    xSerial s;
    s.setClient(scene);
    st_SERIAL_CLIENTINFO* c = s.ctxtdata;
    sOwnBlockScene = 0;
    if (c == NULL || c->membuf == NULL || (size_t)(c->actsize / 4) != sOwnBlock.size())
    {
        return false;
    }
    U32 was = BlockHash(c->membuf, sOwnBlock.size());
    memcpy(c->membuf, sOwnBlock.data(), sOwnBlock.size() * sizeof(S32));
    printf(
        "tssm: coop: this level's save block is as this game had it before the host's world came (hash %08x; "
        "written since: %08x)\n",
        (unsigned)BlockHash(sOwnBlock.data(), sOwnBlock.size()), (unsigned)was);
    fflush(stdout);
    return true;
}

void iCoopCombatProgressDone(const char* why)
{
    if (!(iCoopRunning() && (iCoopConnected() || iCoopJoiningAgain())))
    {
        RestoreOwnProgress(why);
        sProgInHave = false;
    }
}

void iCoopCombatReset()
{
    iCoopCombatOffline();
    iCoopCombatProgressDone("co-op is off");
    sClaimAt.clear();
    sHitAt.clear();
    sOwnHitAt[0].clear();
    sOwnHitAt[1].clear();
    sOwnHitBy.clear();
    sPending.clear();
    sLevel = sLevelHash = 0;
    sHitBucket = Bucket();
    sBoxBucket = Bucket();
    sDamageBucket = Bucket();
    sPickBucket = Bucket();
    sUpgBucket = Bucket();
    memset(sUpgAt, 0, sizeof(sUpgAt));
    memset(sUpgAskKind, 0, sizeof(sUpgAskKind));
    sPauseSeenHave = false;
    sStatesSincePaused = kPausedStates + 1;
    sGuestSafeUntil = 0;
    sApplying = false;
    sInvulnOn = false;
    sOwnPickupAt = 0;
    sProgOutDirty = true;
    sProgReady = false;
    sProgSentAt = 0;
    sRespawnPending = false;
    sHostWasDead = sGuestWasDead = false;
    for (size_t i = 0; i < sPicks.size(); i++)
    {
        sPicks[i].askedAt = 0;
    }
    sPickOutDirty = true;
    sPickSentAt = 0;
    sPickInHave = false;
}

void iCoopCombatSceneReset()
{
    if (!sTokensDone.empty())
    {
        sTokensReset = true;
    }
}

void iCoopCombatSceneExit()
{
    // Before the level's own save into memory (zSceneSave): this player's
    // own progress and pickups, so the save never holds the host's.
    RestoreOwnProgress("leaving the host's level");
    iCoopCombatProgressDone("leaving the level after the session");
    sOwnLevelScene = 0;
    ClearPicks();
    sPickBuiltScene = NULL;
    sPickBuiltId = 0;
    sClaimAt.clear();
    sHitAt.clear();
    sOwnHitAt[0].clear();
    sOwnHitAt[1].clear();
    sOwnHitBy.clear();
    sPending.clear();
    sLevel = sLevelHash = 0;
    sRespawnPending = false;
    HostTokensDrop();
    sInvulnOn = false;
}

F32 iCoopCombatGuestDamageTimer()
{
    U64 now = iCoopNowNs();
    return now < sGuestSafeUntil ? (F32)((F64)(sGuestSafeUntil - now) / 1e9) : 0.0f;
}

void iCoopCombatFrame(U64 now, const PlayerState* partner, U64 partnerAt)
{
    (void)partnerAt;
    F32 dt = globals.update_dt;
    if (!zGameIsPaused() && dt > 0.0f && dt < 1.0f)
    {
        sGameNs += (U64)((F64)dt * 1e9);
    }
    if (globals.sceneCur != sPickBuiltScene ||
        (globals.sceneCur != NULL && globals.sceneCur->sceneID != sPickBuiltId))
    {
        BuildPicks(); // once per level, also when it has none
    }
    bool guest = Guest();
    U32 scene = globals.sceneCur != NULL ? globals.sceneCur->sceneID : 0;
    bool sameLevel = partner != NULL && partner->scene == scene;
    if (!guest && partner != NULL &&
        (!sPauseSeenHave || frame_newer(partner->frame, sPauseSeenFrame)))
    {
        sPauseSeenHave = true;
        sPauseSeenFrame = partner->frame;
        if (partner->flags & PS_PAUSED)
        {
            sStatesSincePaused = 0;
        }
        else if (sStatesSincePaused <= kPausedStates)
        {
            sStatesSincePaused++;
        }
    }

    // The partner's events, in order, once each.
    GameEvent e;
    while (iCoopTakeEvent(&e))
    {
        switch (e.type)
        {
        case EV_HIT:
            if (!guest)
            {
                HostHit(e, partner, now);
            }
            break;
        case EV_PICKUP:
            if (!guest)
            {
                HostPickup(e, partner, now);
            }
            break;
        case EV_DAMAGE:
            if (guest)
            {
                GuestDamage(e, now);
            }
            break;
        case EV_GRANT:
            if (guest)
            {
                GuestGrant(e);
            }
            break;
        case EV_UPGRADE:
            if (!guest)
            {
                HostUpgrade(e, partner, now);
            }
            break;
        case EV_UPGRADE_ACK:
            if (guest)
            {
                GuestUpgradeAnswer(e, now);
            }
            break;
        // M4: the host's level changes and shows, and either side's look.
        case EV_LEVEL:
        case EV_SHOW:
            iCoopFlowEvent(e, now);
            break;
        case EV_LOOK:
            iCoopLookEvent(e);
            break;
        // Protocol 9: the host's bosses (iCoopBoss.cpp).
        case EV_BOSS:
            if (guest)
            {
                iCoopBossEvent(e);
            }
            break;
        // Protocol 9: one health for both players in a boss fight.
        case EV_POOL:
            if (guest)
            {
                iCoopBossPoolEvent(e, now);
            }
            break;
        case EV_POOL_DELTA:
            if (!guest)
            {
                iCoopBossPoolEvent(e, now);
            }
            break;
        default:
            break;
        }
    }
    iCoopBossFrame(now, guest);

    if (!guest)
    {
        HostPicks(now);
        HostProgress(now);
        sInvulnOn = sameLevel;
        if (!sameLevel)
        {
            sPending.clear();
        }
        HostPendingHits(sGameNs);
        HostTokensDone(now);
        bool dead = sameLevel && (partner->flags & PS_DEAD);
        if (dead && !sGuestWasDead)
        {
            printf("tssm: coop: the guest was defeated; its own game brings it back (nothing is reset here)\n");
            fflush(stdout);
        }
        sGuestWasDead = dead;
        return;
    }

    // The guest.
    Pickups in;
    while (iCoopTakePickups(&in))
    {
        sPickIn = in;
        sPickInHave = true;
    }
    Progress pg;
    bool fresh = false;
    while (iCoopTakeProgress(&pg))
    {
        sProgIn = pg;
        sProgInHave = true;
        fresh = true;
    }
    // The host's progress only in the host's level: elsewhere this player
    // plays its own game, and its pickups count for its own save. Not in a
    // level it followed the host to: its save keeps nothing of that one
    // (5.7), so nothing taken there may count either.
    bool hostAway = partner != NULL && partner->scene != scene && !iCoopWorldFollowedHere();
    if ((sOwnSaved || sOwnLevelScene != 0) && hostAway)
    {
        if (sHostAwaySince == 0)
        {
            sHostAwaySince = now;
        }
        else if (now - sHostAwaySince >= kUnshareAfter)
        {
            RestoreOwnProgress("the host is in another level");
        }
    }
    else
    {
        sHostAwaySince = 0;
    }
    if (sameLevel && scene != 0 && sOwnLevelScene != scene)
    {
        KeepOwnLevel(scene); // iCoopCombatHostHere has, unless this level has no shared things
    }
    if (sameLevel && scene != 0 && sProgInHave && (fresh || !sOwnSaved))
    {
        MirrorProgress(sProgIn);
    }
    if (iCoopWorldGuestSynced())
    {
        GuestApplyPicks();
        if (PickTableCurrent())
        {
            GuestAskGivenAgain(now);
        }
    }
    if (sRespawnPending)
    {
        sRespawnPending = false;
        GuestRespawnNow(sameLevel ? partner : NULL);
    }
    // The host's defeat: its own game resets its level; once it plays again
    // this player is put beside it.
    bool hostDead = sameLevel && (partner->flags & PS_DEAD);
    if (hostDead && !sHostWasDead)
    {
        printf("tssm: coop: the host was defeated (its game keeps its own rules: its lives, its level reset)\n");
        fflush(stdout);
    }
    else if (!hostDead && sHostWasDead && sameLevel)
    {
        iCoopWorldPlaceSoon("the host plays again after its defeat");
    }
    sHostWasDead = hostDead;
}

const Pickups* iCoopCombatOutgoing(U64 now, bool partnerHere, size_t* reserve,
                                   const Progress** prog)
{
    *reserve = iCoopEventBytes();
    *prog = NULL;
    if (!Guest() && sProgReady &&
        (sProgOutDirty || sProgSentAt == 0 || now - sProgSentAt >= kProgressEvery))
    {
        // Wherever the guest is: one progress, in every level.
        sProgOutDirty = false;
        sProgSentAt = now;
        *prog = &sProgOut;
        *reserve += progress_msg_size();
    }
    if (!partnerHere || !PickTableCurrent() || sPickOut.count == 0 || sPickOut.scene != sPickSceneId)
    {
        return NULL;
    }
    if (!sPickOutDirty && sPickSentAt != 0 && now - sPickSentAt < kPickupsEvery)
    {
        return NULL;
    }
    sPickOutDirty = false;
    sPickSentAt = now;
    *reserve += pickups_msg_size(sPickOut.count);
    return &sPickOut;
}

// --- iCoop.h: M3's hooks ------------------------------------------------------------------------

bool iCoopGuestHit(xBase* b, S32 dmgtype, const F32* vecHit)
{
    if (!Guest() || !iCoopWorldGuestSynced())
    {
        return false;
    }
    U8 atk = AttackOf(dmgtype);
    U32 scene, hash;
    S32 count;
    S32 id = iCoopWorldLookup(b);
    if (atk == 0 || id < 0 || !LevelTable(&scene, &hash, &count) || id >= count)
    {
        // Not one of this player's attacks (a trigger, a hazard): the host's
        // own world decides.
        if (id >= 0 && LogOk(LOG_HIT))
        {
            printf("tssm: coop: a hit on thing %d (damage type %d) is not this player's attack; not sent\n", (int)id,
                   (int)dmgtype);
        }
        return false;
    }
    xEnt* ent = iCoopWorldEnemy(id);
    if (ent == NULL || ent->model == NULL || ent->model->Mat == NULL)
    {
        return false;
    }
    U64 now = iCoopNowNs();
    if (sClaimAt[id] != 0 && now - sClaimAt[id] < kClaimEvery)
    {
        return false;
    }
    sClaimAt[id] = now;
    en_plyrpup pup = ePowerupLevelNormal;
    NMEC_DmgAmtForAttack((en_npcdmg)dmgtype, &pup);
    S32 level = (S32)pup;
    level = level < kPowerupMin ? kPowerupMin : (level > kPowerupMax ? kPowerupMax : level);
    GameEvent e;
    memset(&e, 0, sizeof(e));
    e.type = EV_HIT;
    e.scene = scene;
    e.table = hash;
    e.id = (U16)id;
    e.kind = atk;
    e.level = (U8)level;
    if (vecHit != NULL)
    {
        F32 len = sqrtf(vecHit[0] * vecHit[0] + vecHit[1] * vecHit[1] + vecHit[2] * vecHit[2]);
        if (len > 1e-4f && len == len)
        {
            for (S32 k = 0; k < 3; k++)
            {
                F32 c = vecHit[k] / len * 32767.0f;
                c = c > 32767.0f ? 32767.0f : (c < -32767.0f ? -32767.0f : c);
                e.dir[k] = (S16)c;
            }
        }
    }
    const xVec3& at = *(const xVec3*)&ent->model->Mat->pos;
    e.pos[0] = at.x;
    e.pos[1] = at.y;
    e.pos[2] = at.z;
    if (!event_valid(e, Role::Client) || !iCoopPushEvent(e))
    {
        return false;
    }
    if (LogOk(LOG_HIT))
    {
        printf("tssm: coop: hit claim sent: thing %d attack %d level %d\n", (int)id, (int)atk, (int)level);
        fflush(stdout);
    }
    return true;
}

bool iCoopClaimPowerup(S32* level)
{
    // One progress (13): the host's save is the guest's too, so the host's
    // own levels are the ones a guest's hit is applied with.
    (void)level;
    return false;
}

bool iCoopEnemyHittable(xBase* b, F32 invuln, S32 present, S32 dmgtype, const F32* vecHit, S32 amount, S32 pup)
{
    if (!sInvulnOn)
    {
        return invuln < 0.0f && present;
    }
    U32 scene, hash;
    S32 count;
    S32 id = iCoopWorldLookup(b);
    // The per-level tables, also before the guest's first claim (the host's
    // own hits are recorded from the start).
    if (id < 0 || !LevelTable(&scene, &hash, &count) || id >= (S32)sOwnHitBy.size())
    {
        return invuln < 0.0f && present;
    }
    // Each player has its own: the enemy's timer started by the other
    // player's hit holds back only that player. Damage that is no player's
    // attack (another enemy, a hazard) is the game's own.
    bool guests = sApplying && b == sApplyTarget;
    if (!guests && AttackOf(dmgtype) == 0)
    {
        return invuln < 0.0f && present;
    }
    U8 who = guests ? 1 : 0;
    U64 now = sGameNs;
    bool open = invuln < 0.0f;
    if (!open)
    {
        U8 by = sOwnHitBy[id];
        U64 theirs = sOwnHitAt[by][id], mine = sOwnHitAt[who][id];
        bool fromOther = by != who && theirs != 0 && now - theirs < kOwnInvuln + 100 * kNsPerMs;
        open = fromOther && (mine == 0 || now - mine >= kOwnInvuln);
    }
    if (!open || !present)
    {
        return false;
    }
    if (!sReplaying && ReelsFromOther(id, who, now))
    {
        // Its wound goal would swallow this hit: held, it lands when that
        // ends (HostPendingHits).
        if (guests)
        {
            sDeferred = true;
        }
        else
        {
            bool have = false;
            for (size_t k = 0; k < sPending.size(); k++)
            {
                have = have || (sPending[k].id == id && sPending[k].who == 0);
            }
            if (!have && sPending.size() < 16)
            {
                PendingHit p;
                memset(&p, 0, sizeof(p));
                p.id = id;
                p.who = 0;
                p.type = (en_npcdmg)dmgtype;
                p.amount = amount;
                p.pup = (en_plyrpup)pup;
                p.haveDir = vecHit != NULL;
                if (vecHit != NULL)
                {
                    p.dir.assign(vecHit[0], vecHit[1], vecHit[2]);
                }
                p.at = now;
                sPending.push_back(p);
            }
        }
        return false;
    }
    if (iCoopLogEvery() > 0 && !guests)
    {
        // For tests: this player's own hit, and how long after the guest's.
        U64 theirs = sOwnHitAt[1][id];
        printf(
            "tssm: coop: this player's hit on thing %d landed (the guest's last hit %d ms before)\n",
            (int)id, theirs != 0 ? (int)((now - theirs) / kNsPerMs) : -1);
        fflush(stdout);
    }
    sOwnHitAt[who][id] = now;
    sOwnHitBy[id] = who;
    if (guests)
    {
        sLanded = true;
    }
    return true;
}

bool iCoopGuestBreakableHit(xBase* b, U32 event, const F32* param)
{
    if (!Guest() || !iCoopWorldGuestSynced())
    {
        return false;
    }
    U32 scene, hash;
    S32 count;
    S32 id = iCoopWorldLookup(b);
    zDestructible* dest = NULL;
    xEnt* ent = id >= 0 ? iCoopWorldBreakable(id, &dest) : NULL;
    if (ent == NULL || !LevelTable(&scene, &hash, &count) || id >= count || ent->model == NULL ||
        ent->model->Mat == NULL)
    {
        return false;
    }
    U8 atk;
    switch (event)
    {
    case eEventHit_BubbleBash:
        atk = ATK_BELOW;
        break;
    case eEventHit_BubbleBowl:
        atk = ATK_BOWL;
        break;
    case eEventHit_PatrickSlam:
        atk = ATK_ABOVE;
        break;
    case eEventHit_Throw:
        atk = ATK_TOSS;
        break;
    case eEventHit_Cruise:
        atk = ATK_CRUISE;
        break;
    case eEventHit_Cartwheel:
        atk = ATK_CARTWHEEL;
        break;
    default:
        atk = (param != NULL && param[1] == 1.0f && gCurrentPlayer == eCurrentPlayerCar) ? ATK_CAR :
                                                                                           ATK_SIDE;
        break;
    }
    if (!dest->active)
    {
        return true; // broken here already: the host's break
    }
    U64 now = iCoopNowNs();
    if (sClaimAt[id] != 0 && now - sClaimAt[id] < kClaimEvery)
    {
        return true;
    }
    sClaimAt[id] = now;
    GameEvent e;
    memset(&e, 0, sizeof(e));
    e.type = EV_HIT;
    e.scene = scene;
    e.table = hash;
    e.id = (U16)id;
    e.kind = atk;
    e.level = kPowerupMin;
    const xVec3& at = *(const xVec3*)&ent->model->Mat->pos;
    e.pos[0] = at.x;
    e.pos[1] = at.y;
    e.pos[2] = at.z;
    if (event_valid(e, Role::Client) && iCoopPushEvent(e) && LogOk(LOG_HIT))
    {
        printf("tssm: coop: hit claim sent: breakable thing %d attack %d\n", (int)id, (int)atk);
        fflush(stdout);
    }
    return true; // the host's breaks it, or not; here only by the host's word
}

bool iCoopParticleWanted()
{
    return sInvulnOn;
}

void iCoopParticleHit(const F32* pos, F32 radiusSq)
{
    F32 at[3];
    if (!sInvulnOn || pos == NULL || !iCoopWorldPartnerTarget(at))
    {
        return;
    }
    if (iCoopNowNs() < sGuestSafeUntil)
    {
        return;
    }
    F32 dx = pos[0] - at[0], dy = pos[1] - at[1], dz = pos[2] - at[2];
    if (dx * dx + dy * dy + dz * dz < radiusSq)
    {
        SendDamage(NULL, 1, DK_FROM, pos, ePlayerNMEDamageEnemyParticle, "a breath or a cloud");
    }
}

S32 iCoopStandInHit(xBase* src, U32 damage, S32 kind, const F32* vec, S32 sub)
{
    if (!iCoopStandInActive())
    {
        return 0;
    }
    return SendDamage(src, damage, kind, vec, sub, NULL);
}

namespace
{
    S32 SendDamage(xBase* src, U32 damage, S32 kind, const F32* vec, S32 sub, const char* what)
    {
        U32 scene, hash;
        S32 count;
        if (!iCoopWorldTable(&scene, &hash, &count))
        {
            return 0;
        }
        U64 now = iCoopNowNs();
        if (now < sGuestSafeUntil)
        {
            return 0; // the guest's own game would not take it either (its DamageTimer)
        }
        GameEvent e;
        memset(&e, 0, sizeof(e));
        e.type = EV_DAMAGE;
        e.scene = scene;
        S32 id = src != NULL ? iCoopWorldLookup(src) : -1;
        e.id = (id >= 0 && iCoopWorldEnemy(id) != NULL) ? (U16)id : (U16)kNoThing;
        // The game's "666" is one damage that ignores the safe time; it goes as a
        // plain one, so the guest's safe time (and the check above) can stop it
        // (docs/MULTIPLAYER.md 6.8).
        U32 amount = damage == 666 ? 1 : damage;
        amount = amount < 1 ? 1 : (amount > kPlayerHealthMax ? kPlayerHealthMax : amount);
        e.level = (U8)amount;
        e.kind = DK_PLAIN;
        if (kind == DK_FROM && vec != NULL && sub >= 0 && sub < kDamageSubEnd)
        {
            e.kind = DK_FROM;
            e.sub = (U8)sub;
            memcpy(e.pos, vec, sizeof(e.pos));
        }
        else if (kind == DK_PUSH && vec != NULL)
        {
            e.kind = DK_PUSH;
            for (S32 k = 0; k < 3; k++)
            {
                F32 c = vec[k];
                e.pos[k] = c > kPushLimit ? kPushLimit : (c < -kPushLimit ? -kPushLimit : c);
            }
        }
        if (!event_valid(e, Role::Host) || !iCoopPushEvent(e))
        {
            return 0;
        }
        F32 safe = globals.player.g.DamageTimeHit;
        sGuestSafeUntil = now + (U64)((safe > 0.0f && safe < 10.0f ? safe : 1.0f) * 1e9);
        sDamageSent++;
        if (LogOk(LOG_DAMAGE))
        {
            printf("tssm: coop: an enemy hit the guest: thing %d, %u damage, kind %d%s%s\n",
                   e.id == kNoThing ? -1 : (int)e.id, (unsigned)amount, (int)e.kind,
                   what != NULL ? "; " : "", what != NULL ? what : "");
            fflush(stdout);
        }
        return 1;
    }
} // namespace

void iCoopStandInEvent(xBase* from, U32 event, const F32* param)
{
    switch (event)
    {
    case eEventHit:
        iCoopStandInHit(from, 1, DK_PLAIN, NULL, 0);
        break;
    case eEventGiveHealth:
    {
        S32 v = param != NULL ? (S32)param[0] : 0;
        if (v < 0)
        {
            iCoopStandInHit(from, (U32)(-v), DK_PLAIN, NULL, 0);
        }
        break;
    }
    case eEventKill:
        iCoopStandInHit(from, kPlayerHealthMax, DK_PLAIN, NULL, 0);
        break;
    default:
        break; // anything else an enemy tells "the player" is the host player's business, not the guest's
    }
}

bool iCoopCombatRespawning()
{
    return sRespawnPending;
}

bool iCoopGuestRespawn()
{
    if (!iCoopRunning() || !iCoopConnected() || !Guest() || !iCoopWorldGuestSynced())
    {
        return false;
    }
    sRespawnPending = true;
    printf("tssm: coop: this player was defeated; it comes back beside the host (no level reset, no life lost)\n");
    fflush(stdout);
    return true;
}

bool iCoopPickupTouch(xBase* b)
{
    if (!Guest() || !iCoopWorldGuestSynced() || !PickTableCurrent() || b == NULL)
    {
        return false;
    }
    S32 i = PickIndex(b);
    if (i < 0)
    {
        return false;
    }
    Pick& k = sPicks[i];
    zEntPickup* p = k.ent;
    if (globals.player.s == NULL || globals.player.s->Health == 0 || sRespawnPending)
    {
        return true; // a defeated player lying on it takes nothing (the host would say no)
    }
    if (p->p->pickupType == 0 && p->p->pickupIndex == 3 && globals.player.s != NULL &&
        globals.player.s->Health >= globals.player.s->MaxHealth)
    {
        return true; // health only when this player is hurt, as the game's own check
    }
    U64 now = iCoopNowNs();
    if (k.askedAt != 0 && now - k.askedAt < kPickupAskEvery)
    {
        return true;
    }
    k.askedAt = now;
    GameEvent e;
    memset(&e, 0, sizeof(e));
    e.type = EV_PICKUP;
    e.scene = sPickSceneId;
    e.table = sPickHash;
    e.id = (U16)i;
    // Out on the host too, or only here: a token or chest this game's own
    // task or challenge showed.
    e.kind = PW_TOUCHED;
    if (sPickInHave && sPickIn.scene == sPickSceneId && sPickIn.table == sPickHash &&
        sPickIn.count == sPicks.size() && !((sPickIn.avail[i >> 3] >> (i & 7)) & 1) &&
        !((sPickIn.bits[i >> 3] >> (i & 7)) & 1) && TokenOrChest(p))
    {
        e.kind = PW_REVEALED;
    }
    if (iCoopPushEvent(e) && LogOk(LOG_PICK))
    {
        printf("tssm: coop: pickup %d asked of the host%s\n", (int)i,
               e.kind == PW_REVEALED ? " (only this game shows it)" : "");
        fflush(stdout);
    }
    return true;
}

bool iCoopProgressClaim(S32 kind, S32 index)
{
    if (!Guest() || !sOwnSaved || !iCoopConnected())
    {
        return false; // this game's own progress: the game's own rules
    }
    if (index < 0 || index >= (kind == UPG_TAUGHT ? (S32)kMoves : (S32)kPowerUps))
    {
        return false;
    }
    U64 now = iCoopNowNs();
    if (sUpgAt[index] != 0 && now - sUpgAt[index] < kUpgradeEvery)
    {
        return true;
    }
    sUpgAt[index] = now;
    sUpgAskKind[index] = (U8)kind;
    GameEvent e;
    memset(&e, 0, sizeof(e));
    e.type = EV_UPGRADE;
    e.scene = globals.sceneCur != NULL ? globals.sceneCur->sceneID : 0;
    e.kind = (U8)kind;
    e.sub = (U8)index;
    if (event_valid(e, Role::Client) && iCoopPushEvent(e) && LogOk(LOG_PROGRESS))
    {
        printf("tssm: coop: move %d asked of the host (%s)\n", (int)index,
               kind == UPG_TAUGHT ? "this game's talk teaches it" :
                                    "bought in this game's pause menu");
        fflush(stdout);
    }
    // The shared save changes on the host; this game's comes with PROGRESS.
    return true;
}

bool iCoopPickupGiven(xBase* b)
{
    if (!Guest() || !iCoopWorldGuestSynced() || !PickTableCurrent() || b == NULL)
    {
        return false;
    }
    S32 i = PickIndex(b);
    if (i < 0 || !TokenOrChest(sPicks[i].ent))
    {
        return false; // the rest are each game's own when a script gives them
    }
    Pick& k = sPicks[i];
    U64 now = iCoopNowNs();
    if (k.givenAsked || (k.askedAt != 0 && now - k.askedAt < kPickupAskEvery))
    {
        return true; // asked already (and again until granted)
    }
    k.givenTries = 0;
    AskGiven(i, now);
    return true;
}
