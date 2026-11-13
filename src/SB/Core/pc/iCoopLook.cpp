// PC-only: co-op's looks (docs/MULTIPLAYER.md 7.4 and 7.5). Compiled with the
// game units.
//
//   - Which character each player plays. Either player switches between
//     SpongeBob and Patrick at any time, in any level that has both (the
//     Co-op tab, or a tap of LB / Z), with the event the game's own bus stops
//     send. [coop] character keeps the choice: auto (the default: a guest
//     takes the other character than the host's when it joins and when a
//     level starts), spongebob or patrick (that one whenever a level starts).
//     Both may be the same character.
//   - Which costume each player wears: the game's own (the level's outfit, or
//     the one the extras menu put on), or one of that character's extras
//     costumes this player's own save has unlocked. A costume is a model pack
//     the game loads with the level, so a new choice is worn from the next
//     level on, as in the game's own extras menu. Neither save's unlocks are
//     ever changed.
//   - The partner's model. Each game draws the partner with its own copy of
//     the model the partner's game says its player is drawn with (the
//     PLAYER_STATE's `model`): a second instance built from that model's MINF,
//     apart from this player's own, so two SpongeBobs, a SpongeBob and a
//     costumed SpongeBob, two Patty Wagons or two slide sleds all work. The
//     partner's costume pack is loaded with each level when this game knows
//     the partner wears it (EV_LOOK, which both sides send when it changes).
//
// Network values here are the partner's EV_LOOK (a character and two costume
// numbers, each range-checked by the parser and used only as indexes into this
// file's own tables of five) and the PLAYER_STATE's model (below 27, an index
// into this file's own table of the game's 27 model names).

#include "iCoop.h"
#include "iCoopInternal.h"
#include "iCoopWorldInternal.h"
#include "iConfig.h"

#include "xAnim.h"
#include "xEnt.h"
#include "xEvent.h"
#include "xModel.h"
#include "xPad.h"
#include "xString.h"
#include "xstransvc.h"
#include "zCamera.h"
#include "zConfig.h"
#include "zEnt.h"
#include "zEntCruiseBubble.h"
#include "zEntPlayer.h"
#include "zEntPlayerBungeeState.h"
#include "zEntPlayerOOBState.h"
#include "zGame.h"
#include "zGameExtras.h"
#include "zGameState.h"
#include "zGlobals.h"
#include "zScene.h"
#include "zUICustom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// zMenuSB.cpp (PLATFORM_PC): whether the extras screen's costume `id` (1-4) is
// unlocked in this save (its treasure chests found).
U32 zMenuSB_PcCostumeUnlocked(U32 id);
// zEntPlayer.cpp (PLATFORM_PC): which part each place in a SpongeBob costume's
// model chain is.
const U8* zEntPlayer_PcSpongeBobAtomicOrder(S32 costumeIndex);

using namespace iNet;

namespace
{
    const U64 kNsPerMs = 1000000ull;

    // The game's 27 player models (zConfig.cpp's own list, ePlayerEnum order).
    const char* const kModelInfo[kModelEnd] = {
        "costume_sb_1_bind.MINF",  "costume_sb_2_bind.MINF",  "costume_sb_3_bind.MINF",
        "costume_sb_4_bind.MINF",  "costume_sb_5_bind.MINF",  "costume_sb_6_bind.MINF",
        "costume_sb_7_bind.MINF",  "costume_sb_a_bind.MINF",  "costume_sb_b_bind.MINF",
        "costume_sb_c_bind.MINF",  "costume_sb_d_bind.MINF",  "costume_pat_1_bind.MINF",
        "costume_pat_2_bind.MINF", "costume_pat_3_bind.MINF", "costume_pat_4_bind.MINF",
        "costume_pat_5_bind.MINF", "costume_pat_6_bind.MINF", "costume_pat_7_bind.MINF",
        "costume_pat_a_bind.MINF", "costume_pat_b_bind.MINF", "costume_pat_c_bind.MINF",
        "costume_pat_d_bind.MINF", "spongepat_0_bind.MINF",   "spongepat_1_bind.MINF",
        "spongepat_2_bind.MINF",   "patty_wagon_bind.MINF",   "sb_ball_bind.MINF",
    };
    // The packs that hold them (zConfig.cpp's PlayerTags).
    const U32 kModelTag[kModelEnd] = {
        'PLS1', 'PLS2', 'PLS3', 'PLS4', 'PLS5', 'PLS6', 'PLS7', 'PLSA', 'PLSB', 'PLSC', 'PLSD', 'PLP1', 'PLP2', 'PLP3',
        'PLP4', 'PLP5', 'PLP6', 'PLP7', 'PLPA', 'PLPB', 'PLPC', 'PLPD', 'PLD0', 'PLD1', 'PLD2', 'PLY2', 'PLY4',
    };
    // How the models are named in the Co-op tab.
    const char* const kModelName[kModelEnd] = {
        "SpongeBob", "SpongeBob", "SpongeBob", "SpongeBob", "SpongeBob", "SpongeBob", "SpongeBob",
        "SpongeBob, Caveman", "SpongeBob, Mermaid Man", "SpongeBob, Ripped Shorts", "SpongeBob, Plain Sponge",
        "Patrick, Plain Patrick", "Patrick", "Patrick", "Patrick", "Patrick", "Patrick", "Patrick",
        "Patrick, Caveman", "Patrick, Disco", "Patrick, Goofy Goober", "Patrick",
        "SpongeBob and Patrick (slide)", "SpongeBob and Patrick (slide)", "SpongeBob and Patrick (slide)",
        "Patty Wagon", "Spongeball",
    };

    bool IsSpongeBobModel(S32 m) { return m >= COSTUME_SB_1 && m <= COSTUME_SB_D; }
    bool IsPatrickModel(S32 m) { return m >= COSTUME_PAT_1 && m <= COSTUME_PAT_D; }
    bool IsSlideModel(S32 m) { return m >= SPONGEPAT_0 && m <= SPONGEPAT_2; }

    // The costume choices: 0 the game's own, 1-4 the extras costumes of that
    // character in the order of the game's own list (zMenuSB.cpp's
    // sExtraCostumeList: two from the extras screen, then two from cheats).
    struct Costume
    {
        const char* key;  // [coop] costume_spongebob / costume_patrick
        const char* name; // the Co-op tab
        U32 tag;          // the pack (0: the game's own)
        U32 extraId;      // the extras screen's costume (1-4), or 0
        U32 cheatFlag;    // or the cheat that turns it on
    };
    const Costume kCostumes[2][kCostumeChoices] = {
        {
            { "game", "The game's own", 0, 0, 0 },
            { "mermaidman", "Mermaid Man", 'PLSB', 1, 0 },
            { "plain", "Plain Sponge", 'PLSD', 2, 0 },
            { "caveman", "Caveman", 'PLSA', 0, 0x2000000 },
            { "rippedshorts", "Ripped Shorts", 'PLSC', 0, 0x4000000 },
        },
        {
            { "game", "The game's own", 0, 0, 0 },
            { "disco", "Disco", 'PLPB', 3, 0 },
            { "plain", "Plain Patrick", 'PLP1', 4, 0 },
            { "caveman", "Caveman", 'PLPA', 0, 0x10000000 },
            { "goofygoober", "Goofy Goober", 'PLPC', 0, 0x8000000 },
        },
    };

    // --- settings ([coop] character, costume_spongebob, costume_patrick) -----------

    bool sSettingsRead;
    S32 sCharSetting; // ICOOP_CHAR_*
    S32 sCostume[2];
    bool sTestAllCostumes; // TSSM_TEST_COSTUMES=all

    void ReadSettings()
    {
        if (sSettingsRead)
        {
            return;
        }
        sSettingsRead = true;
        const char* c = iConfigGetString("coop.character", "auto");
        sCharSetting = strcmp(c, "spongebob") == 0 ? ICOOP_CHAR_SPONGEBOB
                                                  : (strcmp(c, "patrick") == 0 ? ICOOP_CHAR_PATRICK : ICOOP_CHAR_AUTO);
        const char* keys[2] = { "coop.costume_spongebob", "coop.costume_patrick" };
        for (S32 k = 0; k < 2; k++)
        {
            const char* v = iConfigGetString(keys[k], "game");
            sCostume[k] = 0;
            for (S32 i = 1; i < kCostumeChoices; i++)
            {
                if (strcmp(v, kCostumes[k][i].key) == 0)
                {
                    sCostume[k] = i;
                }
            }
        }
        const char* t = getenv("TSSM_TEST_COSTUMES");
        sTestAllCostumes = t != NULL && strcmp(t, "all") == 0;
        // TSSM_TEST_COOP_LOOK=character,sb,pat: the settings a test starts with
        // (as if config.ini had them), e.g. "patrick,0,1".
        const char* look = getenv("TSSM_TEST_COOP_LOOK");
        if (look != NULL)
        {
            char ch[16] = { 0 };
            int a = 0, b = 0;
            if (sscanf(look, "%15[a-z],%d,%d", ch, &a, &b) == 3 && a >= 0 && a < kCostumeChoices && b >= 0 &&
                b < kCostumeChoices)
            {
                sCharSetting = strcmp(ch, "spongebob") == 0
                                   ? ICOOP_CHAR_SPONGEBOB
                                   : (strcmp(ch, "patrick") == 0 ? ICOOP_CHAR_PATRICK : ICOOP_CHAR_AUTO);
                sCostume[0] = a;
                sCostume[1] = b;
                printf("tssm: coop: look from TSSM_TEST_COOP_LOOK: %s, costumes %d %d\n", ch, a, b);
            }
        }
    }

    void SaveSetting(const char* key, const char* value)
    {
        iConfigSet(key, value);
        iConfigSave();
    }

    bool Offered(S32 ch, S32 choice)
    {
        if (ch < 0 || ch > 1 || choice < 0 || choice >= kCostumeChoices)
        {
            return false;
        }
        if (choice == 0 || sTestAllCostumes)
        {
            return true;
        }
        const Costume& c = kCostumes[ch][choice];
        if (c.extraId != 0)
        {
            return zMenuSB_PcCostumeUnlocked(c.extraId) != 0;
        }
        return (zGameExtras_CheatFlags() & c.cheatFlag) != 0;
    }

    // The costume this player's game puts on character `ch` when a level loads:
    // the co-op choice when it is offered, otherwise what the extras menu put
    // on (as a choice), otherwise 0.
    S32 Effective(S32 ch)
    {
        if (Offered(ch, sCostume[ch]) && sCostume[ch] != 0)
        {
            return sCostume[ch];
        }
        U32 tag = zUICustomExtrasManager::IsCostumeEnabled(ch == 0 ? eCurrentPlayerSpongeBob : eCurrentPlayerPatrick);
        for (S32 i = 1; i < kCostumeChoices; i++)
        {
            if (tag != 0 && kCostumes[ch][i].tag == tag)
            {
                return i;
            }
        }
        return 0;
    }

    // --- the level's models ----------------------------------------------------------

    xEnt* Player()
    {
        return xglobals->___player_ent_dont_use_directly;
    }

    // Per level: which of the 27 MINFs it has loaded, and the model ID of each
    // one's first model (what this player's chain carries as its modelID).
    zScene* sModelsScene;
    U32 sModelsSceneId;
    void* sInfo[kModelEnd];
    U32 sFirstModel[kModelEnd];

    void ScanModels()
    {
        if (globals.sceneCur == sModelsScene && globals.sceneCur != NULL && globals.sceneCur->sceneID == sModelsSceneId)
        {
            return;
        }
        sModelsScene = globals.sceneCur;
        sModelsSceneId = globals.sceneCur != NULL ? globals.sceneCur->sceneID : 0;
        for (S32 i = 0; i < kModelEnd; i++)
        {
            U32 size = 0;
            sInfo[i] = sModelsScene != NULL ? xSTFindAsset(xStrHash(kModelInfo[i]), &size) : NULL;
            sFirstModel[i] = 0;
            if (sInfo[i] != NULL && size >= sizeof(xModelAssetInfo) + sizeof(xModelAssetInst))
            {
                const xModelAssetInfo* mi = (const xModelAssetInfo*)sInfo[i];
                if (mi->NumModelInst >= 1)
                {
                    sFirstModel[i] = ((const xModelAssetInst*)(mi + 1))->ModelID;
                }
            }
            else
            {
                sInfo[i] = NULL;
            }
        }
    }

    // The first loaded model of a kind (SpongeBob's, Patrick's, the slide's),
    // the level's own outfit first.
    S32 FirstLoaded(S32 from, S32 to, S32 prefer)
    {
        if (prefer >= from && prefer <= to && sInfo[prefer] != NULL)
        {
            return prefer;
        }
        for (S32 i = from; i <= to; i++)
        {
            if (sInfo[i] != NULL)
            {
                return i;
            }
        }
        return -1;
    }

    // The level's own outfit of character `ch` (0 SpongeBob, 1 Patrick), as a
    // model, or -1 when the level has none.
    S32 LevelOutfit(S32 ch)
    {
        if (globals.sceneCur == NULL)
        {
            return -1;
        }
        U32 scene = globals.sceneCur->sceneID;
        S32 n = zConfig_getPlayerCount(scene);
        for (S32 i = 0; i < n && i < 8; i++)
        {
            S32 e = (S32)zConfig_getPlayerEnum(zConfig_getPlayerTag(scene, i));
            if ((ch == 0 && IsSpongeBobModel(e)) || (ch == 1 && IsPatrickModel(e)))
            {
                return e;
            }
        }
        return -1;
    }

    // The model this player is drawn with now, or kModelUnknown.
    U8 LocalModel()
    {
        xEnt* p = Player();
        if (p == NULL || p->model == NULL || globals.sceneCur == NULL)
        {
            return kModelUnknown;
        }
        ScanModels();
        // By the character the game has the player as (its model chain is the
        // game's own for that character; the entity's model can lag behind
        // it, as at a level's start).
        if (gCurrentPlayer == eCurrentPlayerCar)
        {
            return globals.player.model_car != NULL && sInfo[CAR_NORMAL] != NULL ? (U8)CAR_NORMAL : kModelUnknown;
        }
        if (gCurrentPlayer == eCurrentPlayerSpongePat)
        {
            S32 k = globals.player.model_spongepat != NULL ? FirstLoaded(SPONGEPAT_0, SPONGEPAT_2, -1) : -1;
            return k < 0 ? kModelUnknown : (U8)k;
        }
        if ((gCurrentPlayer != eCurrentPlayerSpongeBob && gCurrentPlayer != eCurrentPlayerPatrick) ||
            player_is_sponge_ball())
        {
            return kModelUnknown; // the Spongeball, or something else
        }
        bool pat = gCurrentPlayer == eCurrentPlayerPatrick;
        xModelInstance* m = pat ? globals.player.model_patrick : globals.player.model_spongebob;
        if (m == NULL)
        {
            return kModelUnknown;
        }
        S32 from = pat ? COSTUME_PAT_1 : COSTUME_SB_1;
        S32 to = pat ? COSTUME_PAT_D : COSTUME_SB_D;
        for (S32 i = from; i <= to; i++)
        {
            if (sInfo[i] != NULL && sFirstModel[i] != 0 && sFirstModel[i] == m->modelID)
            {
                return (U8)i;
            }
        }
        S32 k = FirstLoaded(from, to, LevelOutfit(from == COSTUME_SB_1 ? 0 : 1));
        return k < 0 ? kModelUnknown : (U8)k;
    }

    // --- the partner's models (one instance each, per level) -----------------------------

    struct Built
    {
        S32 model;          // ePlayerEnum
        xModelInstance* m;  // NULL: could not be built (not loaded here)
    };
    const S32 kBuiltMax = 6;
    Built sBuilt[kBuiltMax];
    S32 sBuiltCount;
    zScene* sBuiltScene;
    U32 sBuiltSceneId;
    S32 sFallbackLogged;

    void ForgetBuilt()
    {
        sBuiltCount = 0;
        sBuiltScene = NULL;
        sBuiltSceneId = 0;
        sFallbackLogged = 0;
    }

    xModelInstance* Build(S32 k)
    {
        if (globals.sceneCur != sBuiltScene || (globals.sceneCur != NULL && globals.sceneCur->sceneID != sBuiltSceneId))
        {
            ForgetBuilt();
            sBuiltScene = globals.sceneCur;
            sBuiltSceneId = globals.sceneCur != NULL ? globals.sceneCur->sceneID : 0;
        }
        for (S32 i = 0; i < sBuiltCount; i++)
        {
            if (sBuilt[i].model == k)
            {
                return sBuilt[i].m;
            }
        }
        if (sBuiltCount == kBuiltMax)
        {
            return NULL;
        }
        ScanModels();
        xModelInstance* m = NULL;
        if (sInfo[k] != NULL && Player() != NULL)
        {
            // As the player's own init builds its characters: statically
            // allocated, from this level's memory, gone with the level.
            S32 old = xModelInstStaticAlloc;
            xModelInstStaticAlloc = 1;
            m = zEntRecurseModelInfo(sInfo[k], Player());
            xModelInstStaticAlloc = old;
            if (m != NULL && (m->Anim == NULL || m->Anim->Table == NULL || m->Anim->Single == NULL || m->Mat == NULL))
            {
                m = NULL; // drawn only with an animation table, like the player
            }
            if (m != NULL && IsSpongeBobModel(k))
            {
                // zEntPlayer_SpongeBobInit's own fix-up of SpongeBob's eleventh
                // part: it shares the root's matrix.
                xModelInstance* p = m;
                for (S32 i = 0; i < 10 && p != NULL; i++)
                {
                    p = p->Next;
                }
                if (p != NULL)
                {
                    p->Parent = m;
                    p->Flags = 0x2000;
                    p->Mat = m->Mat;
                }
            }
            if (m != NULL)
            {
                // Only the base layer plays.
                xAnimPlay* play = m->Anim;
                for (U16 i = 1; i < play->NumSingle; i++)
                {
                    if (play->Single[i].State != NULL)
                    {
                        xAnimPlaySetState(&play->Single[i], NULL, 0.0f);
                    }
                }
            }
        }
        sBuilt[sBuiltCount].model = k;
        sBuilt[sBuiltCount].m = m;
        sBuiltCount++;
        printf("tssm: coop: partner model %s (%d) %s\n", kModelInfo[k], (int)k,
               m != NULL ? "built for this level" : "is not loaded in this level");
        fflush(stdout);
        return m;
    }

    // --- this player's character -------------------------------------------------------

    bool sLevelPending;   // a level started (or a session): apply the setting once
    U64 sLevelPendingAt;
    zScene* sLevelScene;
    U32 sLevelSceneId;
    S32 sManualTo = -1;   // a switch asked for in the tab or with LB (CHAR_*)
    U64 sManualAt;
    U64 sLastSwitchAt;
    U32 sEpoch = ~0u;
    S32 sLastChar = -1;   // the character last told to the partner
    S32 sLastEff[2] = { -1, -1 };
    S32 sLastSetting = -1;
    U64 sLookSentAt;
    bool sLookDirty;
    U64 sLbDownAt;        // LB held since (0: not held)
    bool sLbHeldDone;     // this hold already changed the costume

    // The partner's newest EV_LOOK, and the level it was sent from (0: none).
    bool sPartnerLook;
    U8 sPartnerChar;
    U8 sPartnerCostume[2];
    U32 sPartnerLookScene;
    // EV_LOOK arrivals (iNet::LookInbox): every look is taken, the newest
    // replacing the one before, however many a frame drains after a stall
    // (the sender sends one only when its look changes, so a dropped newest
    // look would never come again). Looks past kLooksPerSecond in a second
    // (the sender sends at most 4) are only counted, and the log lines about
    // the partner's look are rationed (a burst, then one a second), so a
    // peer that floods looks cannot fill the log.
    iNet::LookInbox sLookIn;
    U64 sLookFloodLoggedAt;
    // A change of the partner's look the ration left out of the log: the
    // newest is logged once the ration allows (iCoopLookFrame), so the log
    // always ends on the look this game really has.
    bool sLookLogPending;
    F32 sLookLogTokens = 10.0f;
    U64 sLookLogAt;
    // The partner is drawn in an outfit of the same character this level
    // has, because its own is not loaded here (iCoopLookPuppetModel).
    bool sPartnerFallback;

    const U64 kManualFor = 3000 * kNsPerMs;   // a switch asked for waits this long to be possible
    const U64 kLevelFor = 30000 * kNsPerMs;   // a level's own switch waits this long
    const U64 kSwitchGap = 700 * kNsPerMs;    // switches at most this often
    const U64 kLookEvery = 250 * kNsPerMs;    // EV_LOOK at most this often
    const U64 kLbHold = 600 * kNsPerMs;       // held this long, LB changes the costume

    S32 CurrentChar()
    {
        if (gCurrentPlayer == eCurrentPlayerPatrick)
        {
            return CHAR_PATRICK;
        }
        if (gCurrentPlayer == eCurrentPlayerSpongeBob)
        {
            return CHAR_SPONGEBOB;
        }
        return -1;
    }

    // Why this player cannot switch now (NULL: it can).
    const char* SwitchWhyNot()
    {
        xEnt* p = Player();
        if (globals.sceneCur == NULL || p == NULL || p->model == NULL)
        {
            return "not in a level";
        }
        if (globals.player.model_spongebob == NULL || globals.player.model_patrick == NULL)
        {
            return "this level has one character only";
        }
        if (CurrentChar() < 0 || gCurrentPlayer != lastgCurrentPlayer)
        {
            return "not now";
        }
        if (bungee_state::active() || cruise_bubble::active() || oob_state::enabled() || zEntPlayerIsInSlidingMode() ||
            player_is_sponge_ball())
        {
            return "not in this move";
        }
        if (globals.cmgr != NULL || zCameraIsFlyCamRunning() || gGameState != eGameState_Play || zGameIsPaused())
        {
            return "not during a cutscene or a pause";
        }
        if (globals.player.ControlOff || globals.player.carry.grabbed != NULL || globals.player.s == NULL ||
            globals.player.s->Health == 0 || iCoopCombatRespawning())
        {
            return "not right now";
        }
        return NULL;
    }

    void SwitchTo(S32 ch, U64 now, const char* why)
    {
        F32 param[4] = { ch == CHAR_PATRICK ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
        zEntEvent(Player(), eEventPlrSwitchCharacter, param);
        sLastSwitchAt = now;
        printf("tssm: coop: switched to %s (%s)\n", ch == CHAR_PATRICK ? "Patrick" : "SpongeBob", why);
        fflush(stdout);
    }

    void SendLook(U64 now)
    {
        S32 ch = CurrentChar();
        S32 eff0 = Effective(0), eff1 = Effective(1);
        if (sLastChar == ch && sLastEff[0] == eff0 && sLastEff[1] == eff1 && sLastSetting == sCharSetting && !sLookDirty)
        {
            return;
        }
        if (now - sLookSentAt < kLookEvery || !iCoopConnected())
        {
            return;
        }
        GameEvent e;
        memset(&e, 0, sizeof(e));
        e.type = EV_LOOK;
        e.scene = globals.sceneCur != NULL && scene_valid(globals.sceneCur->sceneID) ? globals.sceneCur->sceneID : 0;
        e.kind = (U8)(ch >= 0 ? ch : (sCharSetting == ICOOP_CHAR_PATRICK ? CHAR_PATRICK : CHAR_SPONGEBOB));
        e.level = (U8)eff0;
        e.sub = (U8)eff1;
        if (iCoopPushEvent(e))
        {
            sLookSentAt = now;
            sLastChar = ch;
            sLastEff[0] = eff0;
            sLastEff[1] = eff1;
            sLastSetting = sCharSetting;
            sLookDirty = false;
            printf("tssm: coop: look sent: %s, costumes %d %d\n", e.kind == CHAR_PATRICK ? "Patrick" : "SpongeBob",
                   (int)eff0, (int)eff1);
            fflush(stdout);
        }
    }

    // The character a level (or the session) starts this player as, or -1
    // to keep the level's own.
    S32 LevelChoice(const PlayerState* partner)
    {
        if (sCharSetting == ICOOP_CHAR_SPONGEBOB)
        {
            return CHAR_SPONGEBOB;
        }
        if (sCharSetting == ICOOP_CHAR_PATRICK)
        {
            return CHAR_PATRICK;
        }
        // auto: a guest takes the other character than the host's.
        if (iCoopLocalSlot() != 1)
        {
            return -1;
        }
        if (partner == NULL || partner->scene != globals.sceneCur->sceneID ||
            (partner->flags & (PS_SPECIAL | PS_CUTSCENE)))
        {
            return -2; // not yet: the host is not here (or not as a character)
        }
        // The host's character is the one its EV_LOOK from this level names:
        // the host sends it only once its own start-of-level choice is made
        // (iCoopLookFrame), while its PLAYER_STATE can still carry the
        // character it is about to leave. Until that look has come, and while
        // the two disagree (one of them is a moment behind the other), wait.
        if (!sPartnerLook || sPartnerLookScene != globals.sceneCur->sceneID || partner->character != sPartnerChar)
        {
            return -2;
        }
        return sPartnerChar == CHAR_PATRICK ? CHAR_SPONGEBOB : CHAR_PATRICK;
    }

    // How many costumes of character `ch` this player may wear besides the
    // game's own.
    S32 OfferedCount(S32 ch)
    {
        S32 n = 0;
        for (S32 i = 1; i < kCostumeChoices; i++)
        {
            n += Offered(ch, i) ? 1 : 0;
        }
        return n;
    }

    // A log line about the partner's look, if the ration allows (a burst of
    // ten, then one a second).
    bool LookLogOk(U64 now)
    {
        sLookLogTokens += (F32)((now - sLookLogAt) / kNsPerMs) / 1000.0f;
        sLookLogAt = now;
        sLookLogTokens = sLookLogTokens > 10.0f ? 10.0f : sLookLogTokens;
        if (sLookLogTokens < 1.0f)
        {
            return false;
        }
        sLookLogTokens -= 1.0f;
        return true;
    }

    // The partner's look, as known now, if a change of it is not logged yet
    // and the ration allows.
    void LogPartnerLook(U64 now)
    {
        if (!sLookLogPending || !sPartnerLook || !LookLogOk(now))
        {
            return;
        }
        sLookLogPending = false;
        printf("tssm: coop: the partner's look: %s, costumes %d %d\n",
               sPartnerChar == CHAR_PATRICK ? "Patrick" : "SpongeBob", (int)sPartnerCostume[0],
               (int)sPartnerCostume[1]);
        fflush(stdout);
    }

    void CycleCostume(U64 now)
    {
        S32 ch = CurrentChar();
        if (ch < 0)
        {
            iCoopAddNotice("costumes: only as SpongeBob or Patrick");
            return;
        }
        if (OfferedCount(ch) == 0)
        {
            // Nothing to step to: no setting is written.
            printf("tssm: coop: LB held: no costume of %s is unlocked in this save; nothing changed\n",
                   ch == CHAR_PATRICK ? "Patrick" : "SpongeBob");
            fflush(stdout);
            iCoopAddNotice(ch == CHAR_PATRICK ? "no costumes for Patrick are unlocked in your save"
                                              : "no costumes for SpongeBob are unlocked in your save");
            return;
        }
        S32 c = sCostume[ch];
        for (S32 i = 1; i <= kCostumeChoices; i++)
        {
            S32 n = (c + i) % kCostumeChoices;
            if (Offered(ch, n))
            {
                iCoopSetCostumeSetting(ch, n);
                break;
            }
        }
        (void)now;
    }
} // namespace

// --- for iCoopPuppet.cpp and iCoopWorld.cpp ---------------------------------------------------

U8 iCoopLookLocalModel()
{
    return LocalModel();
}

xModelInstance* iCoopLookPuppetModel(U8 model, U8 character, S32* drawnAs)
{
    *drawnAs = -1;
    if (globals.sceneCur == NULL || Player() == NULL)
    {
        return NULL;
    }
    ScanModels();
    S32 k = model < kModelEnd ? (S32)model : -1;
    if (k >= 0 && sInfo[k] != NULL)
    {
        xModelInstance* m = Build(k);
        if (m != NULL)
        {
            *drawnAs = k;
            sPartnerFallback = false;
            return m;
        }
    }
    sPartnerFallback = k >= 0;
    // Not loaded here: the same character in an outfit this level has.
    S32 fb = -1;
    if (k < 0 ? character == CHAR_SPONGEBOB : IsSpongeBobModel(k))
    {
        fb = FirstLoaded(COSTUME_SB_1, COSTUME_SB_D, LevelOutfit(0));
    }
    else if (k < 0 ? character == CHAR_PATRICK : IsPatrickModel(k))
    {
        fb = FirstLoaded(COSTUME_PAT_1, COSTUME_PAT_D, LevelOutfit(1));
    }
    else if (IsSlideModel(k))
    {
        fb = FirstLoaded(SPONGEPAT_0, SPONGEPAT_2, -1);
    }
    if (fb < 0)
    {
        return NULL;
    }
    if (sFallbackLogged < 4 && k >= 0)
    {
        sFallbackLogged++;
        printf("tssm: coop: the partner's model %d is not loaded here; drawn as model %d\n", (int)k, (int)fb);
        fflush(stdout);
    }
    *drawnAs = fb;
    return Build(fb);
}

U16 iCoopLookPartsFor(U16 parts, U8 sentModel, S32 drawnModel)
{
    // The state's parts are places in the chain of the model the partner
    // wears. SpongeBob's costumes do not all order their chains alike (Caveman,
    // Ripped Shorts and Plain Sponge differ), so on another outfit of the
    // level's the same places are other parts: a pair of shoes without a body.
    if (sentModel == drawnModel || !IsSpongeBobModel(sentModel) || !IsSpongeBobModel(drawnModel))
    {
        return parts;
    }
    const U8* from = zEntPlayer_PcSpongeBobAtomicOrder(sentModel);
    const U8* to = zEntPlayer_PcSpongeBobAtomicOrder(drawnModel);
    if (from == NULL || to == NULL)
    {
        return parts;
    }
    const S32 kChain = 11;
    U16 out = (U16)(parts & ~((1u << kChain) - 1u));
    for (S32 i = 0; i < kChain; i++)
    {
        if ((parts & (1u << i)) == 0)
        {
            continue;
        }
        for (S32 j = 0; j < kChain; j++)
        {
            if (to[j] == from[i])
            {
                out |= (U16)(1u << j);
                break;
            }
        }
    }
    return out;
}

void iCoopLookSceneExit()
{
    ForgetBuilt();
    sModelsScene = NULL;
    sModelsSceneId = 0;
}

void iCoopLookFrame(U64 now, const PlayerState* partner)
{
    ReadSettings();
    LogPartnerLook(now);
    if (globals.sceneCur == NULL || Player() == NULL)
    {
        return;
    }
    U32 epoch = iCoopSessionEpoch();
    if (epoch != sEpoch)
    {
        // A new session (iCoopLookReset already forgot the last partner's
        // look, before this frame's events came in): the setting applies.
        sEpoch = epoch;
        sLastChar = -1;
        sLookDirty = true;
        sLevelPending = true;
        sLevelPendingAt = now;
    }
    if (globals.sceneCur != sLevelScene || globals.sceneCur->sceneID != sLevelSceneId)
    {
        sLevelScene = globals.sceneCur;
        sLevelSceneId = globals.sceneCur->sceneID;
        sLevelPending = true;
        sLevelPendingAt = now;
        sManualTo = -1;
        sLookDirty = true;
    }

    // LB (the "white" button; Z on the keyboard): a tap switches the
    // character, a hold of 0.6 s changes the costume.
    bool lb = globals.pad0 != NULL && (globals.pad0->on & XPAD_BUTTON_L2) != 0;
    if (lb && sLbDownAt == 0)
    {
        sLbDownAt = now;
        sLbHeldDone = false;
    }
    else if (lb && !sLbHeldDone && now - sLbDownAt >= kLbHold)
    {
        sLbHeldDone = true;
        if (gGameState == eGameState_Play && !zGameIsPaused())
        {
            printf("tssm: coop: LB held: the next costume\n");
            CycleCostume(now);
        }
    }
    else if (!lb && sLbDownAt != 0)
    {
        if (!sLbHeldDone && now - sLbDownAt < kLbHold && gGameState == eGameState_Play && !zGameIsPaused())
        {
            S32 ch = CurrentChar();
            printf("tssm: coop: LB tapped: %s\n",
                   ch < 0 ? "only SpongeBob or Patrick switch" : (ch == CHAR_PATRICK ? "to SpongeBob" : "to Patrick"));
            fflush(stdout);
            if (ch >= 0)
            {
                iCoopSetCharacterSetting(ch == CHAR_PATRICK ? ICOOP_CHAR_SPONGEBOB : ICOOP_CHAR_PATRICK);
                if (globals.player.model_spongebob == NULL || globals.player.model_patrick == NULL)
                {
                    // As in single player, a one-character level (a combat
                    // arena, a drive level) has no other character: said at
                    // once rather than after the 3 s a switch is tried for.
                    printf(
                        "tssm: coop: this level has one character only; %s from the next level\n",
                        ch == CHAR_PATRICK ? "SpongeBob" : "Patrick");
                    fflush(stdout);
                    iCoopAddNotice(
                        "this level has one character only; the choice applies from the next level");
                    sManualTo = -1;
                }
            }
            else
            {
                iCoopAddNotice("this level has one character; SpongeBob and Patrick switch in the others");
            }
        }
        sLbDownAt = 0;
    }

    // A switch asked for: as soon as it can be done, for a few seconds.
    const char* whyNot = SwitchWhyNot();
    if (sManualTo >= 0)
    {
        if (now > sManualAt && now - sManualAt > kManualFor) // sManualAt can be later than this frame's now
        {
            if (CurrentChar() != sManualTo)
            {
                printf("tssm: coop: could not switch to %s within 3 s (%s)\n",
                       sManualTo == CHAR_PATRICK ? "Patrick" : "SpongeBob", whyNot != NULL ? whyNot : "?");
                fflush(stdout);
                iCoopAddNotice(globals.player.model_spongebob == NULL || globals.player.model_patrick == NULL
                                   ? "this level has one character only; the choice applies in the next level"
                                   : "could not switch now; the choice applies when the next level starts");
            }
            sManualTo = -1;
        }
        else if (CurrentChar() == sManualTo)
        {
            sManualTo = -1;
        }
        else if (whyNot == NULL && now - sLastSwitchAt >= kSwitchGap)
        {
            SwitchTo(sManualTo, now, "asked for");
            iCoopAddNotice(sManualTo == CHAR_PATRICK ? "you play Patrick" : "you play SpongeBob");
            sManualTo = -1;
            sLevelPending = false;
        }
    }
    // The level's (or the session's) start: the setting, once.
    if (sLevelPending && sManualTo < 0)
    {
        if (now - sLevelPendingAt > kLevelFor || globals.player.model_spongebob == NULL ||
            globals.player.model_patrick == NULL)
        {
            sLevelPending = false;
        }
        else if (whyNot == NULL && now - sLastSwitchAt >= kSwitchGap)
        {
            S32 want = LevelChoice(partner);
            if (want != -2)
            {
                sLevelPending = false;
                if (want >= 0 && want != CurrentChar())
                {
                    SwitchTo(want, now,
                             sCharSetting == ICOOP_CHAR_AUTO ? "a guest takes the other character than the host's"
                                                            : "[coop] character");
                }
            }
        }
    }

    // This player's own start-of-level choice still to be made: its look
    // waits for it, so the partner never hears a character this player is
    // about to leave (a guest on auto decides by the host's look).
    bool settling = false;
    if (sLevelPending && sManualTo < 0)
    {
        S32 want = LevelChoice(partner);
        settling = want == -2 || (want >= 0 && want != CurrentChar());
    }
    if (!settling)
    {
        SendLook(now);
    }
}

void iCoopLookEvent(const GameEvent& e)
{
    if (e.type != EV_LOOK)
    {
        return;
    }
    U64 now = iCoopNowNs();
    bool over = false;
    bool changed = iNet::look_inbox_take(&sLookIn, e, now, &over);
    if (over && now - sLookFloodLoggedAt >= 10000 * kNsPerMs)
    {
        sLookFloodLoggedAt = now;
        printf("tssm: coop: many looks from the partner at once (more than %d a second, %u so far); the newest is "
               "kept\n",
               (int)iNet::kLooksPerSecond, (unsigned)sLookIn.over);
        fflush(stdout);
    }
    if (!sLookIn.known)
    {
        return;
    }
    sPartnerLook = true;
    sPartnerChar = sLookIn.character;
    sPartnerCostume[0] = sLookIn.costume[0];
    sPartnerCostume[1] = sLookIn.costume[1];
    sPartnerLookScene = sLookIn.scene;
    if (changed)
    {
        sLookLogPending = true;
        LogPartnerLook(now);
    }
}

void iCoopLookReset()
{
    iNet::look_inbox_reset(&sLookIn);
    sLookLogPending = false;
    sPartnerLook = false;
    sPartnerLookScene = 0;
    sPartnerFallback = false;
    sLastChar = -1;
    sLookDirty = true;
}

// --- the game's hooks ----------------------------------------------------------------------------

bool iCoopCostumeOverride(S32 character, U32* tag)
{
    if (!iCoopRunning() || character < 0 || character > 1)
    {
        return false;
    }
    ReadSettings();
    S32 c = sCostume[character];
    if (c == 0 || !Offered(character, c))
    {
        return false;
    }
    *tag = kCostumes[character][c].tag;
    return true;
}

namespace
{
    bool sExtraLoaded; // this level's player packs have the partner's too
}

S32 iCoopExtraPlayerPacks(U32 sceneID, const U32* loaded, S32 loadedCount, U32* out, S32 max)
{
    sExtraLoaded = false;
    if (!iCoopRunning() || !iCoopConnected() || !sPartnerLook || max <= 0)
    {
        return 0;
    }
    S32 n = 0;
    S32 count = zConfig_getPlayerCount(sceneID);
    for (S32 i = 0; i < count && i < 8; i++)
    {
        U32 levelTag = zConfig_getPlayerTag(sceneID, i);
        S32 e = (S32)zConfig_getPlayerEnum(levelTag);
        S32 ch = IsSpongeBobModel(e) ? 0 : (IsPatrickModel(e) ? 1 : -1);
        if (ch < 0)
        {
            continue;
        }
        U8 c = sPartnerCostume[ch];
        U32 want = c > 0 && c < kCostumeChoices ? kCostumes[ch][c].tag : levelTag;
        bool have = false;
        for (S32 k = 0; k < loadedCount; k++)
        {
            have = have || loaded[k] == want;
        }
        for (S32 k = 0; k < n; k++)
        {
            have = have || out[k] == want;
        }
        if (!have && n < max)
        {
            out[n++] = want;
            char t[5] = { (char)(want >> 24), (char)(want >> 16), (char)(want >> 8), (char)want, 0 };
            printf("tssm: coop: loading the partner's outfit too (%s)\n", t);
            fflush(stdout);
        }
    }
    sExtraLoaded = n > 0;
    return n;
}

void* iCoopLocalModelInfo(S32 character, U32* size)
{
    if (!sExtraLoaded || globals.sceneCur == NULL || character < 0 || character > 1)
    {
        return NULL;
    }
    // This player's own outfit for that character in this level: the level's,
    // through the game's own costume rule (and co-op's choice).
    U32 scene = globals.sceneCur->sceneID;
    S32 count = zConfig_getPlayerCount(scene);
    for (S32 i = 0; i < count && i < 8; i++)
    {
        U32 levelTag = zConfig_getPlayerTag(scene, i);
        S32 e = (S32)zConfig_getPlayerEnum(levelTag);
        if ((character == 0 && IsSpongeBobModel(e)) || (character == 1 && IsPatrickModel(e)))
        {
            U32 tag = zConfig_getAlternateCostume(levelTag);
            void* info = xSTFindAsset(xStrHash(zConfig_getPlayerModelInfoString(tag)), size);
            return info;
        }
    }
    return NULL;
}

// --- the Co-op tab ---------------------------------------------------------------------------------

S32 iCoopCharacterSetting()
{
    ReadSettings();
    return sCharSetting;
}

void iCoopSetCharacterSetting(S32 setting)
{
    ReadSettings();
    if (setting < ICOOP_CHAR_AUTO || setting > ICOOP_CHAR_PATRICK)
    {
        return;
    }
    sCharSetting = setting;
    SaveSetting("coop.character",
                setting == ICOOP_CHAR_SPONGEBOB ? "spongebob" : (setting == ICOOP_CHAR_PATRICK ? "patrick" : "auto"));
    if (setting != ICOOP_CHAR_AUTO)
    {
        sManualTo = setting == ICOOP_CHAR_PATRICK ? CHAR_PATRICK : CHAR_SPONGEBOB;
        sManualAt = iCoopNowNs();
    }
    sLookDirty = true;
}

S32 iCoopCostumeSetting(S32 character)
{
    ReadSettings();
    return character == 0 || character == 1 ? sCostume[character] : 0;
}

void iCoopSetCostumeSetting(S32 character, S32 choice)
{
    ReadSettings();
    if (character < 0 || character > 1 || choice < 0 || choice >= kCostumeChoices || !Offered(character, choice))
    {
        return;
    }
    sCostume[character] = choice;
    SaveSetting(character == 0 ? "coop.costume_spongebob" : "coop.costume_patrick", kCostumes[character][choice].key);
    sLookDirty = true;
    static const char* const kNotice[2][kCostumeChoices] = {
        { "SpongeBob's costume: the game's own (from the next level)", "SpongeBob's costume: Mermaid Man (from the next level)",
          "SpongeBob's costume: Plain Sponge (from the next level)", "SpongeBob's costume: Caveman (from the next level)",
          "SpongeBob's costume: Ripped Shorts (from the next level)" },
        { "Patrick's costume: the game's own (from the next level)", "Patrick's costume: Disco (from the next level)",
          "Patrick's costume: Plain Patrick (from the next level)", "Patrick's costume: Caveman (from the next level)",
          "Patrick's costume: Goofy Goober (from the next level)" },
    };
    iCoopAddNotice(kNotice[character][choice]);
    printf("tssm: coop: %s's costume set to %s\n", character == 0 ? "SpongeBob" : "Patrick",
           kCostumes[character][choice].key);
    fflush(stdout);
}

const char* iCoopCostumeName(S32 character, S32 choice)
{
    if (character < 0 || character > 1 || choice < 0 || choice >= kCostumeChoices)
    {
        return "?";
    }
    return kCostumes[character][choice].name;
}

bool iCoopCostumeOffered(S32 character, S32 choice)
{
    ReadSettings();
    return Offered(character, choice);
}

void iCoopGetLookView(iCoopLookView* out)
{
    ReadSettings();
    memset(out, 0, sizeof(*out));
    out->character = -1;
    out->partnerCharacter = -1;
    xEnt* p = Player();
    out->inLevel = globals.sceneCur != NULL && p != NULL && p->model != NULL;
    if (out->inLevel)
    {
        out->character = CurrentChar();
        U8 m = LocalModel();
        out->modelName = m < kModelEnd ? kModelName[m] : "";
        out->switchWhyNot = SwitchWhyNot();
        out->effective[0] = Effective(0);
        out->effective[1] = Effective(1);
    }
    out->partnerKnown = sPartnerLook;
    if (sPartnerLook)
    {
        out->partnerCharacter = sPartnerChar;
        out->partnerCostume[0] = sPartnerCostume[0];
        out->partnerCostume[1] = sPartnerCostume[1];
    }
    out->partnerFallback = sPartnerFallback;
}

const char* iCoopModelName(U8 model)
{
    return model < kModelEnd ? kModelName[model] : "";
}
