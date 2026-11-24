// PC-only: the co-op service (iCoop.h, docs/MULTIPLAYER.md). Settings, the I/O
// thread that owns the socket and the session, the mailboxes between that
// thread and the game thread, and the status the Co-op tab shows.
//
// **Threads.** Level loads block the game thread for seconds, and a session
// that goes quiet for ten is closed, so the socket lives on a thread of its
// own that keeps answering keepalives through a load. That thread never
// touches game state, and the game thread never touches the socket: they meet
// in a few fixed-size fields under one mutex (the newest local state going
// out, a short queue of partner states coming in, the status, the notices).
// Nothing here allocates on the network path.
//
// **Stalls.** Either game's thread can stop for a while: a held or dragged
// window (Windows runs its own move loop inside the game's event pump), a
// minimised window, a hitch, a slow disk, a level load. The I/O thread keeps
// the session up through it (keepalives, acknowledgements) and tells the other
// side (STALL, 4 times a second once this game has run no frame for 250 ms),
// so the other game holds its picture of this one instead of giving it up.
// Only a stall longer than a minute ends the session (DC_STALLED). What a
// stall means for the puppet and the shared world is iCoopPuppet.cpp's and
// iCoopWorld.cpp's (docs/MULTIPLAYER.md 5.11).
//
// **Off means off.** Until a player hosts or joins (the Co-op tab, [coop] mode,
// TSSM_COOP or --host / --join), there is no thread and no socket, and the
// game's hooks return on their first line.

#include "iCoop.h"

#include "iCoopInternal.h"
#include "iCoopLocal.h"
#include "iConfig.h"
#include "iFile.h"
#include "iHost.h"
#include "iMod.h"
#include "iNetCrypto.h"
#include "iNetDisc.h"
#include "iNetRelay.h"
#include "iNetSession.h"
#include "iNetSock.h"
#include "iSaveStore.h"

#include "coop_id.h" // generated at build time by tools/coop_id.cmake

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace iNet;

namespace
{
    // --- settings (game thread) ------------------------------------------------

    char sName[kMaxName + 1] = "Player";
    U16 sPort = kDefaultPort;
    char sAddress[64];
    char sServer[128];
    bool sLoopback;
    bool sNameTag = true;
    bool sFollow = true;
    std::atomic<bool> sReconnect{ true }; // M5: [coop] reconnect (read by the I/O thread)
    bool sQualityHud = true;              // M5: [coop] quality
    bool sShowCode = true; // [coop] show_code
    // Tests only: a build ID or a game-files hash that differs from the
    // other copy's (TSSM_TEST_COOP_BUILD_SALT, TSSM_TEST_COOP_ASSET_SALT), so
    // tools/coop_test.py can show the refusals.
    bool sTestBuildSalt, sTestAssetSalt;
    // The game-files code (iCoopGameFilesCode) once hashed, under sLock.
    char sFilesCode[9];
    U32 sFilesCount;
    S32 sLogEvery;
    bool sNeedsPassword;
    char sStartupCode[16];

    // TSSM_COOP_NETSIM: a bad connection, simulated on what this side sends.
    // Loopback only.
    bool sNetsim;
    S32 sSimDelayMs, sSimJitterMs, sSimLossPct;

    // The home network (iNetDisc.h). [coop] lan_visible, read by the I/O
    // thread. Tests: TSSM_COOP_LAN_PORT puts discovery on 127.0.0.1 at that
    // port, and TSSM_COOP_LAN_TARGET names the 127.x addresses the finder
    // asks instead of broadcasting, so no test opens a socket the network
    // (or the firewall) sees. Set before the I/O thread starts.
    std::atomic<bool> sLanVisible{ true };
    bool sLanTest;
    U16 sLanPort = kDiscPort;
    const S32 kMaxLanTargets = 4;
    NetAddr sLanTargets[kMaxLanTargets];
    S32 sLanTargetCount;
    // The level this game is in, for a host's answers (iCoopNoteScene).
    std::atomic<U32> sLocalScene{ 0 };

    // --- the shared state (sLock) ----------------------------------------------

    enum CommandKind
    {
        CMD_NONE,
        CMD_HOST,
        CMD_JOIN,
        CMD_STOP,
        CMD_KICK,
    };

    struct Command
    {
        CommandKind kind;
        U16 port;
        bool loopback;
        bool local; // local co-op: port 0, the bound port read back
        NetAddr address;
        char password[kMaxPassword + 1];
        char name[kMaxName + 1];
        iCoopAssetJob assets;
        bool remember; // joining: keep the address once the session is up
        // Through the relay server (docs/MULTIPLAYER.md 15): its address, and
        // joining, the room's code. The session's password stays the join code.
        bool relay;
        char server[128];
        char room[RelayLink::kMaxCodeText];
    };

    const S32 kSampleQueue = 32;
    const S32 kNotices = 4;

    std::mutex sLock;
    std::condition_variable sWake;
    // A pointer, never destroyed while running: a std::thread still joinable
    // when the process exits (TSSM_EXIT_AFTER, a quit from the menu) would
    // call std::terminate from its destructor. iCoopShutdown runs at exit.
    std::thread* sThread;
    bool sThreadStarted;
    bool sQuit;

    Command sCmd;
    bool sCmdPending;

    // Finding games (iCoopFindGames): wanted, a new search each time the
    // generation changes, the game-files job for this game's own code and
    // [coop] address for the last-host question. The I/O thread's list,
    // copied out for the tab.
    bool sFindWanted;
    U32 sFindGen;
    iCoopAssetJob sFindAssets;
    char sFindProbe[64];
    Found sFound[kMaxFound];
    S32 sFoundCount;
    // The game whose code entry is open (iCoopKeepFound), so its join still
    // works after the search's minute ends and the list empties.
    NetAddr sKeptAddr;
    bool sKept;
    // A joined host's address to keep in [coop] address, once its session
    // is up; the game thread writes config.ini (iCoopGameBeat).
    char sSaveAddress[64];
    std::atomic<bool> sSaveAddressPending{ false };

    PlayerState sOut;
    bool sOutPending;
    // M2: the host's snapshot going out with sOut, and the guest's
    // acknowledgement; what came in the other way.
    Snapshot sOutSnap;
    bool sOutSnapPending;
    U32 sOutAck;
    U32 sAckIn;
    bool sAckInPending;
    const S32 kSnapQueue = 8;
    struct QueuedSnap
    {
        Snapshot s;
        U64 at;
    };
    QueuedSnap sSnapQ[kSnapQueue];
    S32 sSnapQCount;
    bool sGoToHost;

    // M3: the reliable events (docs/MULTIPLAYER.md 6.3). Out: pushed by the
    // game thread, sent and sent again by the I/O thread with every datagram
    // until the partner's REL_ACK covers them. In: delivered once each, in
    // order, by the I/O thread, and taken by the game thread; a full inbox
    // (this game stalled) takes no more, so they stay unacknowledged and come
    // again later.
    ReliableOut sRelOut;
    ReliableIn sRelIn;
    bool sRelAckDue; // the partner has sent events in this session: say how far they came
    const S32 kEventInbox = 2 * (S32)kRelWindow + (S32)kMaxEventsPerMsg;
    GameEvent sEvIn[kEventInbox];
    S32 sEvInCount;
    U32 sRelBadLogged;
    // M3: the host's PICKUPS going out with sOut, and the newest that came in.
    Pickups sOutPick;
    bool sOutPickPending;
    Pickups sInPick;
    bool sInPickPending;
    // Protocol 7: the host's PROGRESS, the same way.
    Progress sOutProg;
    bool sOutProgPending;
    Progress sInProg;
    bool sInProgPending;
    // Protocol 7: the host's COUNTERS, the same way; those that come before
    // the game thread takes one are merged into it (each carries only the
    // counters that changed lately).
    Counters sOutCnt;
    bool sOutCntPending;
    Counters sInCnt;
    bool sInCntPending;
    // M4: the partner's newest EV_READY (the guest has loaded a level and
    // waits) and EV_GO (the host starts it), for the start of a level both go
    // to (docs/MULTIPLAYER.md 7.2). Kept here, not in the inbox: the game
    // thread waits for them while it is between levels.
    LevelSignals sPeerLevel;

    iCoopSample sQueue[kSampleQueue];
    S32 sQueueCount;
    U32 sQueueSeq;
    U32 sLastFrame;
    bool sHaveFrame;
    U32 sEpoch;

    iCoopStatus sStatus;

    // Stalls (docs/MULTIPLAYER.md 5.11). The game thread's heartbeat: when it
    // last ran (iCoopGameBeat, every frame from the window's event pump). The
    // partner's STALL reports: the newest one's arrival and length, 0 while
    // its game runs (a PLAYER_STATE clears it).
    std::atomic<U64> sGameBeat{ 0 };
    U64 sPeerStallAt;
    U32 sPeerStallMs;
    const U64 kStallReport = 250 * kNsPerMs;   // no frame this long: tell the partner
    const U64 kStallEvery = 250 * kNsPerMs;    // and again this often
    const U64 kStallLimit = 60 * kNsPerSec;    // no frame this long: the session ends
    const U64 kPeerStallFor = 1500 * kNsPerMs; // a STALL report holds this long
    const U64 kEventsEvery = 30 * kNsPerMs;    // M4: events wait at most this long for a datagram
    // M5: a guest whose session is lost (nothing heard for kSessionTimeout)
    // joins again by itself for this long.
    const U64 kRejoinFor = 60 * kNsPerSec;
    // A first join refused as FULL: asked again for this long (past
    // iNetSession.h's kReplaceSilence, 3 s, with room for the handshake).
    const U64 kFirstFullFor = 6 * kNsPerSec;
    // Tests (TSSM_TEST_BLACKOUT): until then, nothing goes out or comes in.
    std::atomic<U64> sBlackoutUntil{ 0 };

    struct Notice
    {
        const char* text;
        U64 at;
    };
    Notice sNoticeList[kNotices];
    S32 sNoticeCount;

    // --- game thread only ---------------------------------------------------------

    bool sRunning;
    bool sTagValid;
    F32 sTagX, sTagY;
    bool sTagInFront;

    // --- helpers -------------------------------------------------------------------

    U64 NowNs()
    {
        using namespace std::chrono;
        return (U64)duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }

    void Log(const char* msg)
    {
        // Constant text only (iNetSession.h's on_log and the strings here).
        printf("tssm: %s\n", msg);
        fflush(stdout);
    }

    void AddNoticeLocked(const char* text)
    {
        if (sNoticeCount == kNotices)
        {
            memmove(sNoticeList, sNoticeList + 1, sizeof(Notice) * (kNotices - 1));
            sNoticeCount--;
        }
        sNoticeList[sNoticeCount].text = text;
        sNoticeList[sNoticeCount].at = NowNs();
        sNoticeCount++;
    }

    void Why(char* why, size_t whySize, const char* text)
    {
        if (why != NULL && whySize > 0)
        {
            snprintf(why, whySize, "%s", text);
        }
    }

    // The build ID (iNetProto.h): different builds are refused before anything
    // else is compared.
    void BuildId(U8 out[kBuildIdSize])
    {
        build_id(TSSM_COOP_ID, sTestBuildSalt, out);
    }

    iCoopAssetJob AssetJob()
    {
        iCoopAssetJob job;
        job.assetRoot = iFileAssetRoot();
        job.modRoot = iFileModRoot();
        if (iModsEnabled())
        {
            for (S32 i = 0; i < iModCount(); i++)
            {
                const iModInfo* m = iModGet(i);
                if (m != NULL && m->enabled)
                {
                    job.mods.push_back(m->path);
                }
            }
        }
        const char* saves = iSaveStoreRoot();
        if (saves != NULL && saves[0] != '\0')
        {
            job.cachePath = std::string(saves) + "/coop_files.cache";
        }
        return job;
    }

    // --- the I/O thread --------------------------------------------------------------

    struct Delayed
    {
        U64 due;
        NetAddr to;
        U16 len;
        U8 data[kMaxDatagram];
    };

    struct Io
    {
        UdpSocket sock;
        bool open;
        // Through the relay server (docs/MULTIPLAYER.md 15): the link stands
        // in for `sock`. Joining waits for the host in the room before the
        // session's HELLO goes out (relayJoinPending).
        bool relay;
        RelayLink link;
        bool relayJoinPending;
        U64 relayStatusAt;
        iCoopRole role;
        Host host;
        Client client;
        U64 rngState;
        Delayed delayed[64];
        S32 delayedCount;
        bool wasConnected;
        // The traffic, measured over a few seconds (the Co-op tab, the log).
        U64 rateAt, rateOut, rateIn;
        // TSSM_COOP_NETSIM applies to this session: set per host / join, and
        // only when its socket is bound to loopback or its peer is 127.x, so a
        // later join from the Co-op tab to a real address is never slowed.
        bool netsim;
        // Stalls: the last STALL sent, whether the current stall was logged,
        // and a session this side ended because its game stalled too long.
        U64 lastStallSent;
        bool stallLogged;
        bool stallEnded;
        // M4: when this side last sent anything in the session, so events
        // (and the acknowledgement of the partner's) go out within kEventsEvery
        // even while this game sends no state (between levels).
        U64 lastSent;
        // M5: joining again after a lost connection. The join's own settings,
        // the password included, are kept until the join ends (Leave, a
        // refusal, a goodbye, a kick), and only for this.
        Command rejoinCmd;
        bool haveRejoin;
        U8 build[kBuildIdSize];
        U8 assets[kHashSize];
        bool rejoining;
        bool rejoinWait;
        U64 rejoinUntil, rejoinAt, lostAt;
        // M5 fix round: a first join the host refuses as FULL is asked again
        // each second until this time (kFirstFullFor): the session the host
        // still has may be this player's own from a game that just ended (a
        // crash, a restart), which the host lets go after kReplaceSilence.
        U64 firstFullUntil;
        bool firstFullWait;
        // The session this side has: a host's that changes while connected
        // was replaced by the guest's new join (kReplaceSilence).
        U32 sessionId;
        bool blackoutLogged;
        // Joining with `remember`: the address, kept in config.ini once the
        // session is up.
        bool rememberJoin;
        NetAddr joinAddr;

        // The home network (iNetDisc.h). The interfaces, read again every
        // 10 s while either side runs.
        IfV4 ifs[kMaxIfs];
        S32 ifsCount;
        U64 ifsAt;
        // Hosting: the responder, its socket on the discovery port, and why
        // it is not open (a port in use is said once per hosting).
        bool hostLoopback, hostLocal;
        bool hostAddrsDirty; // the tab's addresses are to be written again
        UdpSocket resp;
        bool respOpen, respFailed;
        Responder responder;
        U64 respLogAt;
        // Joining from the list: the finder, one socket per interface (the
        // broadcasts leave by that interface only), the questions' timing.
        bool finding;
        U32 findGen;
        UdpSocket findSock[kMaxIfs];
        S32 findIf[kMaxIfs]; // the interface each socket is bound to
        S32 findSockCount;
        Finder finder;
        U64 findUntil, nextAsk;
        NetAddr probe;
        bool haveProbe;
        S32 foundLogged;
    };

    bool Blackout(U64 now) { return now < sBlackoutUntil.load(std::memory_order_relaxed); }

    void ForgetRejoin(Io* io)
    {
        secure_zero(io->rejoinCmd.password, sizeof(io->rejoinCmd.password));
        io->haveRejoin = false;
        io->rejoining = false;
        io->rejoinWait = false;
    }

    Io* sIo; // allocated when the thread starts, never on the network path

    void IoSockSend(Io* io, const NetAddr& to, const U8* d, size_t n)
    {
        if (io->relay)
        {
            io->link.send(to, d, n);
        }
        else
        {
            udp_send(io->sock, to, d, n);
        }
    }

    // Closes the co-op socket, or leaves the relay's room.
    void IoSockClose(Io* io)
    {
        if (io->relay)
        {
            io->link.stop();
            io->relay = false;
            io->relayJoinPending = false;
            std::lock_guard<std::mutex> hold(sLock);
            sStatus.relay = false;
            sStatus.relayRoom[0] = ' ';
            sStatus.relayPartner = false;
            sStatus.relayPingMs = -1;
        }
        else
        {
            IoSockClose(io);
        }
    }

    void IoSend(void* ctx, const NetAddr& to, const U8* d, size_t n)
    {
        Io* io = (Io*)ctx;
        if (!io->open || n > kMaxDatagram)
        {
            return;
        }
        io->lastSent = NowNs();
        if (Blackout(io->lastSent))
        {
            return; // tests: the cable is out
        }
        if (io->netsim)
        {
            io->rngState = io->rngState * 6364136223846793005ull + 1442695040888963407ull;
            U32 r = (U32)(io->rngState >> 33);
            if ((S32)(r % 100) < sSimLossPct)
            {
                return;
            }
            if (io->delayedCount < 64)
            {
                Delayed& q = io->delayed[io->delayedCount++];
                S32 jitter = sSimJitterMs > 0 ? (S32)((r >> 8) % (U32)(sSimJitterMs + 1)) : 0;
                q.due = NowNs() + (U64)(sSimDelayMs + jitter) * kNsPerMs;
                q.to = to;
                q.len = (U16)n;
                memcpy(q.data, d, n);
            }
            return;
        }
        IoSockSend(io, to, d, n);
    }

    void IoFlushDelayed(Io* io, U64 now, bool all)
    {
        S32 keep = 0;
        for (S32 i = 0; i < io->delayedCount; i++)
        {
            Delayed& q = io->delayed[i];
            if (all || q.due <= now)
            {
                if (io->open && !Blackout(now))
                {
                    IoSockSend(io, q.to, q.data, q.len);
                }
            }
            else
            {
                if (keep != i)
                {
                    io->delayed[keep] = q;
                }
                keep++;
            }
        }
        io->delayedCount = keep;
    }

    void DeliverLocked(void*, const GameEvent& e)
    {
        // M4: the level signals are kept apart (the newest of each; an
        // EV_LEVEL also forgets a stale EV_GO for its level, iNetProto).
        if (level_signals_take(&sPeerLevel, e))
        {
            return;
        }
        // IoOnPayload made room for kRelMaxDelivered before offering any.
        if (sEvInCount < kEventInbox)
        {
            sEvIn[sEvInCount++] = e;
        }
    }

    void IoOnPayload(void*, const Payload& p)
    {
        // Already authenticated, parsed and validated, its slot is the
        // partner's, and SNAPSHOT came from the host and SNAP_ACK from the
        // guest (iNetSession.cpp, iNetProto.cpp).
        std::lock_guard<std::mutex> hold(sLock);
        // M3's events: how far ours came, and the partner's in order, once
        // each. An acknowledgement of events never sent, or an event past the
        // window, changes nothing (logged).
        if (p.has & HAS_REL_ACK)
        {
            if (!sRelOut.ack(p.rel_ack) && sRelBadLogged < 4)
            {
                sRelBadLogged++;
                Log("coop: an acknowledgement for events never sent was ignored");
            }
        }
        if (p.has & HAS_EVENT)
        {
            sRelAckDue = true;
            // Room for the most one message can complete (kRelMaxDelivered),
            // so DeliverLocked never drops one that the REL_ACK then covers.
            if (sEvInCount + (S32)kRelMaxDelivered <= kEventInbox)
            {
                for (S32 i = 0; i < (S32)p.ev_count; i++)
                {
                    if (!sRelIn.offer(p.ev[i], DeliverLocked, NULL))
                    {
                        if (sRelBadLogged < 4)
                        {
                            sRelBadLogged++;
                            Log("coop: an event past the window was ignored");
                        }
                        break;
                    }
                }
            }
        }
        // The partner's game is stalled (its I/O thread says so), or running
        // again (a state of its own came).
        if (p.has & HAS_STALL)
        {
            sPeerStallAt = NowNs();
            sPeerStallMs = p.stall_ms;
        }
        else if (p.has & HAS_PSTATE)
        {
            sPeerStallAt = 0;
            sPeerStallMs = 0;
        }
        if (p.has & HAS_SNAPSHOT)
        {
            // Applied in order by the game thread (iCoopWorld.cpp), which
            // drops any that is not newer than the last it applied.
            if (sSnapQCount == kSnapQueue)
            {
                memmove(sSnapQ, sSnapQ + 1, sizeof(QueuedSnap) * (kSnapQueue - 1));
                sSnapQCount--;
            }
            QueuedSnap& q = sSnapQ[sSnapQCount++];
            memcpy(&q.s, &p.snap, offsetof(Snapshot, e) + sizeof(SnapEntry) * p.snap.count);
            q.at = NowNs();
        }
        if (p.has & HAS_SNAP_ACK)
        {
            // Newest wins; the game thread checks it against what it sent.
            if (!sAckInPending || frame_newer(p.snap_ack, sAckIn))
            {
                sAckIn = p.snap_ack;
                sAckInPending = true;
            }
        }
        if (!(p.has & HAS_PSTATE))
        {
            return;
        }
        // Newest wins by the sender's frame.
        if (sHaveFrame && !frame_newer(p.pstate.frame, sLastFrame))
        {
            return;
        }
        sHaveFrame = true;
        sLastFrame = p.pstate.frame;
        if (p.has & HAS_PICKUPS)
        {
            // With the host's newest state only, so an older one that comes
            // late never undoes a newer one.
            sInPick = p.pick;
            sInPickPending = true;
        }
        if (p.has & HAS_PROGRESS)
        {
            sInProg = p.prog;
            sInProgPending = true;
        }
        if (p.has & HAS_COUNTERS)
        {
            if (sInCntPending)
            {
                counters_merge(&sInCnt, p.cnt);
            }
            else
            {
                sInCnt = p.cnt;
            }
            sInCntPending = true;
        }
        if (sQueueCount == kSampleQueue)
        {
            memmove(sQueue, sQueue + 1, sizeof(iCoopSample) * (kSampleQueue - 1));
            sQueueCount--;
        }
        iCoopSample& s = sQueue[sQueueCount++];
        s.s = p.pstate;
        s.at = NowNs();
        s.seq = ++sQueueSeq;
    }

    void IoOnLog(void*, const char* msg) { Log(msg); }

    // --- the home network (iNetDisc.h, docs/MULTIPLAYER.md 14.2) ----------------------

    const U64 kIfsEvery = 10 * kNsPerSec; // the interfaces are read again this often
    const U64 kFindFor = 60 * kNsPerSec; // one search
    const S32 kFoundLogs = 16; // games logged per search
    const size_t kDiscRecv =
        256; // a question is 128 bytes; anything this size or more is dropped unread

    // TRUE when the interface list changed. In the loopback test mode
    // 127.0.0.0/8 stands in for the network and nothing is read.
    bool IoRefreshIfs(Io* io, U64 now)
    {
        if (sLanTest)
        {
            if (io->ifsCount == 1)
            {
                return false;
            }
            const U8 lo[4] = { 127, 0, 0, 1 };
            memcpy(io->ifs[0].ip, lo, 4);
            io->ifs[0].prefix = 8;
            io->ifsCount = 1;
            return true;
        }
        if (io->ifsAt != 0 && now - io->ifsAt < kIfsEvery)
        {
            return false;
        }
        io->ifsAt = now;
        IfV4 got[kMaxIfs];
        S32 n = (S32)if_list(got, kMaxIfs);
        if (n == io->ifsCount && memcmp(got, io->ifs, sizeof(IfV4) * (size_t)n) == 0)
        {
            return false;
        }
        memcpy(io->ifs, got, sizeof(IfV4) * (size_t)n);
        io->ifsCount = n;
        return true;
    }

    void IoRespClose(Io* io)
    {
        if (!io->respOpen)
        {
            return;
        }
        udp_close(&io->resp);
        io->respOpen = false;
        std::lock_guard<std::mutex> hold(sLock);
        sStatus.lanAnswering = false;
    }

    // A host's answers, from its start (IoStart): what they say besides the
    // state and the level, which each tick keeps current.
    void IoRespSetUp(Io* io, const Command& cmd, U16 port, const U8* build, const U8* assets)
    {
        io->hostLoopback = cmd.loopback;
        io->hostLocal = cmd.local;
        io->respFailed = false;
        io->hostAddrsDirty = true;
        Offer& o = io->responder.offer;
        memset(&o, 0, sizeof(o));
        o.disc_version = kDiscVersion;
        o.proto = kProtoVersion;
        memcpy(o.build, build, kIdPrefix);
        memcpy(o.files, assets, kIdPrefix);
        o.game_port = port;
        // Tells this hosting from the next one's; never 0.
        if (!os_random(&o.instance, sizeof(o.instance)) || o.instance == 0)
        {
            o.instance = 1;
        }
        o.platform = disc_this_platform();
        snprintf(o.name, sizeof(o.name), "%s", cmd.name);
        io->responder.reset();
    }

    // Hosting: this device's home-network addresses for the tab, read again
    // with the interfaces. Never for local co-op's host, nor for a loopback
    // host outside the loopback test mode (only this PC can join it).
    void IoHostAddrs(Io* io, U64 now)
    {
        bool lan = io->open && !io->relay && io->role == ICOOP_ROLE_HOST && !io->hostLocal &&
                   (!io->hostLoopback || sLanTest);
        if (!lan)
        {
            return;
        }
        if (!IoRefreshIfs(io, now) && !io->hostAddrsDirty)
        {
            return;
        }
        io->hostAddrsDirty = false;
        std::lock_guard<std::mutex> hold(sLock);
        sStatus.lanHost = true;
        sStatus.lanAddressCount = 0;
        for (S32 i = 0; i < io->ifsCount && sStatus.lanAddressCount < 4; i++)
        {
            const U8* ip = io->ifs[i].ip;
            snprintf(sStatus.lanAddresses[sStatus.lanAddressCount++],
                     sizeof(sStatus.lanAddresses[0]), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        }
    }

    void IoRespTick(Io* io, U64 now)
    {
        IoHostAddrs(io, now);
        bool hosting = io->open && !io->relay && io->role == ICOOP_ROLE_HOST;
        bool want = disc_host_visible(hosting, sLanVisible.load(std::memory_order_relaxed),
                                      io->hostLoopback, io->hostLocal, sLanTest);
        if (!want)
        {
            if (io->respOpen)
            {
                IoRespClose(io);
                Log("coop: lan: no longer visible on this network");
            }
            return;
        }
        if (!io->respOpen)
        {
            if (io->respFailed)
            {
                return;
            }
            const U8 any[4] = { 0, 0, 0, 0 };
            const U8 lo[4] = { 127, 0, 0, 1 };
            if (!udp_open_v4(&io->resp, sLanTest ? lo : any, sLanPort, false, true))
            {
                io->respFailed = true;
                std::lock_guard<std::mutex> hold(sLock);
                sStatus.lanAnswering = false;
                snprintf(
                    sStatus.lanNote, sizeof(sStatus.lanNote), "%s",
                    "Others on this network can't see this game automatically (port 2172 is in use by another "
                    "program or another copy of the game). They can still join with the address below.");
                printf(
                    "tssm: coop: lan: UDP port %u could not be opened; other devices cannot list this game "
                    "(joining by address still works)\n",
                    (unsigned)sLanPort);
                fflush(stdout);
                return;
            }
            io->respOpen = true;
            io->responder.reset();
            io->responder.loopback_test = sLanTest;
            io->respLogAt = now;
            io->ifsAt = 0;
            {
                std::lock_guard<std::mutex> hold(sLock);
                sStatus.lanAnswering = true;
                sStatus.lanNote[0] = '\0';
            }
            printf("tssm: coop: lan: visible on this network: answering on UDP port %u%s\n",
                   (unsigned)sLanPort, sLanTest ? " (test: 127.0.0.1 only)" : "");
            fflush(stdout);
        }
        IoRefreshIfs(io, now);
        Responder& r = io->responder;
        memcpy(r.ifs, io->ifs, sizeof(IfV4) * (size_t)io->ifsCount);
        r.ifs_count = (U8)io->ifsCount;
        r.offer.state = io->host.session().active ? OFFER_HAS_PLAYER : OFFER_WAITING;
        U32 scene = sLocalScene.load(std::memory_order_relaxed);
        r.offer.scene = scene_valid(scene) ? scene : 0;

        U8 buf[kDiscRecv];
        for (S32 i = 0; i < 64; i++)
        {
            NetAddr from;
            int n = udp_recv(io->resp, buf, sizeof(buf), &from);
            if (n == 0)
            {
                break;
            }
            if (n < 0)
            {
                r.st.drop_size++; // too large, truncated, or an ICMP error
                continue;
            }
            U8 out[kOfferMax];
            size_t len = r.on_question(from, buf, (size_t)n, now, out, sizeof(out));
            if (len != 0)
            {
                udp_send(io->resp, from, out, len);
            }
        }
        if (sLogEvery > 0 && now - io->respLogAt >= 5 * kNsPerSec)
        {
            io->respLogAt = now;
            printf(
                "tssm: coop: lan: questions answered %llu; dropped: size %llu, malformed %llu, source %llu, "
                "rate %llu\n",
                (unsigned long long)r.st.answered, (unsigned long long)r.st.drop_size,
                (unsigned long long)r.st.drop_bad, (unsigned long long)r.st.drop_source,
                (unsigned long long)r.st.drop_rate);
            fflush(stdout);
        }
    }

    void IoFindClose(Io* io)
    {
        for (S32 i = 0; i < io->findSockCount; i++)
        {
            udp_close(&io->findSock[i]);
        }
        io->findSockCount = 0;
    }

    // One socket per interface, bound to its address, so a broadcast leaves
    // by that interface and never by a VPN that holds the default route.
    void IoFindOpen(Io* io)
    {
        IoFindClose(io);
        Finder& f = io->finder;
        memcpy(f.ifs, io->ifs, sizeof(IfV4) * (size_t)io->ifsCount);
        f.ifs_count = (U8)io->ifsCount;
        if (sLanTest)
        {
            const U8 lo[4] = { 127, 0, 0, 1 };
            if (udp_open_v4(&io->findSock[0], lo, 0, false, false))
            {
                io->findIf[0] = 0;
                io->findSockCount = 1;
            }
            return;
        }
        for (S32 i = 0; i < io->ifsCount; i++)
        {
            S32 k = io->findSockCount;
            if (udp_open_v4(&io->findSock[k], io->ifs[i].ip, 0, true, false))
            {
                io->findIf[k] = i;
                io->findSockCount++;
            }
        }
    }

    // Ends a search (the minute is over, a host or join starts, the list
    // closed, a new search): the sockets go, the list empties. `over`: the
    // minute ran out, so the next search waits for "Search again".
    void IoFindStop(Io* io, const char* how, bool over = false)
    {
        if (!io->finding)
        {
            return;
        }
        IoFindClose(io);
        io->finding = false;
        const FinderStats& st = io->finder.st;
        printf(
            "tssm: coop: lan: search %s: %llu answers, %llu games listed; dropped: rate %llu, source %llu, "
            "malformed %llu, nonce %llu, list full %llu\n",
            how, (unsigned long long)st.answers, (unsigned long long)st.added,
            (unsigned long long)st.drop_rate, (unsigned long long)st.drop_source,
            (unsigned long long)st.drop_bad, (unsigned long long)st.drop_nonce,
            (unsigned long long)st.drop_full);
        fflush(stdout);
        std::lock_guard<std::mutex> hold(sLock);
        sStatus.finding = false;
        sStatus.findLeftSec = 0;
        sFoundCount = 0;
        if (over)
        {
            sFindWanted = false;
        }
    }

    void IoFindStart(Io* io, U64 now, U32 gen, const iCoopAssetJob& job, const char* probe)
    {
        IoFindStop(io, "stopped (searching again)");
        // This game's own version and game files, so each game found says
        // Ready or why not.
        U8 build[kBuildIdSize];
        BuildId(build);
        U8 assets[kHashSize];
        memset(assets, 0, sizeof(assets));
        char why[160];
        size_t files = 0;
        bool filesKnown = iCoopHashAssets(job, assets, why, sizeof(why), &files);
        if (filesKnown)
        {
            if (sTestAssetSalt)
            {
                assets[0] ^= 0x5A;
            }
            std::lock_guard<std::mutex> hold(sLock);
            version_code(assets, sFilesCode);
            sFilesCount = (U32)files;
            printf("tssm: coop: game files code %s, %u files\n", sFilesCode, (unsigned)files);
            fflush(stdout);
        }
        else
        {
            printf(
                "tssm: coop: lan: this game's files could not be checked (%s); the list cannot say whether "
                "they match\n",
                why);
        }
        if (!net_init())
        {
            Log("coop: lan: the network could not be started");
        }
        Finder& f = io->finder;
        memcpy(f.build, build, kIdPrefix);
        memcpy(f.files, assets, kIdPrefix);
        f.files_known = filesKnown;
        f.disc_port = sLanPort;
        f.loopback_test = sLanTest;
        f.reset();
        io->ifsAt = 0;
        IoRefreshIfs(io, now);
        IoFindOpen(io);
        io->finding = true;
        io->findGen = gen;
        io->findUntil = now + kFindFor;
        io->nextAsk = now;
        io->foundLogged = 0;
        io->haveProbe = disc_probe_target(probe, sLanPort, sLanTest, &io->probe);
        if (sLanTest)
        {
            printf(
                "tssm: coop: lan: searching for games (test: %d loopback addresses on port %u%s)\n",
                (int)sLanTargetCount, (unsigned)sLanPort,
                io->haveProbe ? ", and the last host" : "");
        }
        else
        {
            printf("tssm: coop: lan: searching for games on this network (%d networks%s)\n",
                   (int)io->findSockCount, io->haveProbe ? ", and the last host" : "");
        }
        if (io->findSockCount == 0)
        {
            Log("coop: lan: no network to search on (no socket could be opened)");
        }
        fflush(stdout);
        std::lock_guard<std::mutex> hold(sLock);
        sStatus.finding = true;
        sStatus.findLeftSec = (U32)(kFindFor / kNsPerSec);
        sFoundCount = 0;
        if (io->findSockCount == 0)
        {
            snprintf(
                sStatus.lanNote, sizeof(sStatus.lanNote), "%s",
                "No home network found: turn on Wi-Fi (or connect a cable). Over the internet you can still "
                "join by address.");
        }
        else
        {
            sStatus.lanNote[0] = '\0';
        }
    }

    void LogFound(const Found& g)
    {
        // The name passed iNet::name_valid; the rest is constant text and numbers.
        if (g.newer)
        {
            Log("coop: lan: found a game of a newer version (only its address is known)");
            return;
        }
        printf("tssm: coop: lan: found a game: %s (%s, %s), game port %u: %s\n", g.name,
               disc_platform_name(g.platform),
               g.state == OFFER_HAS_PLAYER ? "has a player" : "waiting", (unsigned)g.addr.port,
               g.kind == FOUND_READY ? "ready" :
               g.kind == FOUND_FILES ? "different game files" :
                                       "different version");
        fflush(stdout);
    }

    void IoFindTick(Io* io, U64 now)
    {
        if (!io->finding)
        {
            return;
        }
        if (now >= io->findUntil)
        {
            IoFindStop(io, "over (a minute)", true);
            return;
        }
        if (!sLanTest && IoRefreshIfs(io, now))
        {
            IoFindOpen(io); // another network: its own sockets
        }
        if (now >= io->nextAsk)
        {
            // One question a second, the same to every target.
            io->nextAsk = now + kNsPerSec;
            U8 nonce[kDiscNonce];
            U8 q[kDiscoverSize];
            size_t qn = 0;
            if (io->findSockCount > 0 && os_random(nonce, sizeof(nonce)) &&
                (qn = encode_discover(q, sizeof(q), nonce)) != 0)
            {
                io->finder.asked(nonce);
                if (sLanTest)
                {
                    for (S32 t = 0; t < sLanTargetCount; t++)
                    {
                        udp_send(io->findSock[0], sLanTargets[t], q, qn);
                    }
                }
                else
                {
                    for (S32 k = 0; k < io->findSockCount; k++)
                    {
                        NetAddr to[kAskTargets];
                        size_t m = disc_ask_targets(io->ifs[io->findIf[k]], sLanPort, to);
                        for (size_t t = 0; t < m; t++)
                        {
                            udp_send(io->findSock[k], to[t], q, qn);
                        }
                    }
                }
                if (io->haveProbe)
                {
                    // From the interface whose network holds it, else the first.
                    S32 k = 0;
                    U8 ip[4];
                    v4_of(io->probe, ip);
                    for (S32 j = 0; j < io->findSockCount && !sLanTest; j++)
                    {
                        if (v4_in_subnet(ip, io->ifs[io->findIf[j]]))
                        {
                            k = j;
                            break;
                        }
                    }
                    udp_send(io->findSock[k], io->probe, q, qn);
                }
            }
            std::lock_guard<std::mutex> hold(sLock);
            sStatus.findLeftSec = (U32)((io->findUntil - now + kNsPerSec - 1) / kNsPerSec);
        }
        bool changed = false;
        U8 buf[kDiscRecv];
        for (S32 k = 0; k < io->findSockCount; k++)
        {
            for (S32 i = 0; i < 64; i++)
            {
                NetAddr from;
                int n = udp_recv(io->findSock[k], buf, sizeof(buf), &from);
                if (n == 0)
                {
                    break;
                }
                if (n < 0)
                {
                    continue;
                }
                U64 added = io->finder.st.added;
                changed = io->finder.on_answer(from, buf, (size_t)n, now) || changed;
                if (io->finder.st.added > added && io->foundLogged < kFoundLogs)
                {
                    io->foundLogged++;
                    LogFound(io->finder.list[io->finder.count - 1]);
                }
            }
        }
        changed = io->finder.expire(now) || changed;
        if (changed)
        {
            std::lock_guard<std::mutex> hold(sLock);
            memcpy(sFound, io->finder.list, sizeof(Found) * io->finder.count);
            sFoundCount = io->finder.count;
        }
    }

    void IoClose(Io* io, U64 now)
    {
        if (!io->open)
        {
            return;
        }
        if (io->role == ICOOP_ROLE_HOST)
        {
            io->host.close(now);
        }
        else if (io->role == ICOOP_ROLE_JOIN)
        {
            io->client.leave(now);
        }
        IoFlushDelayed(io, now, true);
        IoSockClose(io);
        io->open = false;
        io->role = ICOOP_ROLE_OFF;
        io->rememberJoin = false;
        ForgetRejoin(io);
        IoRespClose(io);
        std::lock_guard<std::mutex> hold(sLock);
        sStatus.lanHost = false;
        sStatus.lanAddressCount = 0;
    }

    void SetPhaseLocked(iCoopPhase phase, const char* reason)
    {
        sStatus.phase = phase;
        snprintf(sStatus.reason, sizeof(sStatus.reason), "%s", reason != NULL ? reason : "");
    }

    void ResetPartnerLocked()
    {
        sQueueCount = 0;
        sSnapQCount = 0;
        sOutSnapPending = false;
        sOutAck = 0;
        sAckInPending = false;
        sAckIn = 0;
        sRelOut = ReliableOut();
        sRelIn = ReliableIn();
        sRelAckDue = false;
        sEvInCount = 0;
        sOutPickPending = false;
        sInPickPending = false;
        sOutProgPending = false;
        sInProgPending = false;
        sOutCntPending = false;
        sInCntPending = false;
        level_signals_reset(&sPeerLevel);
        sStatus.worldShared = false;
        sStatus.worldThings = 0;
        sStatus.worldMismatch = 0;
        sStatus.following = false;
        sStatus.kbOut = sStatus.kbIn = -1.0f;
        sHaveFrame = false;
        sLastFrame = 0;
        sPeerStallAt = 0;
        sPeerStallMs = 0;
        sEpoch++;
        sStatus.connected = false;
        sStatus.partnerName[0] = '\0';
        sStatus.pingMs = -1;
        sStatus.partnerFresh = false;
        sStatus.partnerScene = 0;
        sStatus.partnerModel = kModelUnknown;
        sStatus.partnerInMyLevel = false;
        sStatus.partnerLevelName[0] = '\0';
        sStatus.lossInPct = sStatus.lossOutPct = -1;
        sStatus.partnerSilentMs = 0;
        sStatus.quality = ICOOP_QUALITY_NONE;
    }

    // Constant text: what to check when the host does not answer (M5, the
    // join flow; docs/HOSTING.txt and docs/JOINING.txt say the same).
    const char* const kNoAnswerText =
        "no answer from the host. Check that the address is the host's public address with the port it hosts "
        "on, that the host is hosting right now, that the host's router forwards that UDP port to the host's "
        "PC, and that the host allowed the game through Windows Firewall";

    const char* const kNoAnswerRelayText =
        "no answer from the host's game through the co-op server. Check that the host is still hosting in that "
        "room, and try joining again";

    const char* EndText(EndReason e)
    {
        switch (e)
        {
        case END_PEER_LEFT: return "the other player left";
        case END_KICKED: return "the host removed you from the game";
        case END_TIMEOUT: return "the connection was lost (nothing heard for 10 seconds)";
        case END_PROTOCOL: return "the connection was closed after too many bad messages";
        case END_STALLED: return "the other player's game stopped for over a minute (a window held down, or a hang)";
        default: return "the session ended";
        }
    }

    // Through the relay server (docs/MULTIPLAYER.md 15). The server's build
    // check is the co-op version in hex: the handshake checks it again, with
    // the game files, end to end.
    void IoStartRelay(Io* io, Command& cmd, const NetIO& nio, const U8* build, const U8* assets)
    {
        char buildHex[2 * kBuildIdSize + 1];
        for (size_t i = 0; i < kBuildIdSize; i++)
        {
            snprintf(buildHex + 2 * i, 3, "%02x", build[i]);
        }
        bool create = cmd.kind == CMD_HOST;
        if (!io->link.start(cmd.server, cmd.name, buildHex, create, cmd.room))
        {
            const char* why = io->link.error();
            io->link.stop();
            secure_zero(cmd.password, sizeof(cmd.password));
            std::lock_guard<std::mutex> hold(sLock);
            SetPhaseLocked(ICOOP_PHASE_ERROR, why);
            return;
        }
        io->relay = true;
        io->open = true;
        io->relayJoinPending = !create;
        io->relayStatusAt = 0;
        {
            std::lock_guard<std::mutex> hold(sLock);
            sStatus.relay = true;
            sStatus.relayRoom[0] = '\0';
            sStatus.relayPartner = false;
            sStatus.relayPingMs = -1;
        }
        if (create)
        {
            HostConfig cfg;
            memset(&cfg, 0, sizeof(cfg));
            memcpy(cfg.build_id, build, kBuildIdSize);
            memcpy(cfg.asset_hash, assets, kHashSize);
            snprintf(cfg.name, sizeof(cfg.name), "%s", cmd.name);
            cfg.password = cmd.password;
            cfg.iters = kDefaultIters;
            bool ok = io->host.init(cfg, nio, NowNs());
            secure_zero(cmd.password, sizeof(cmd.password));
            if (!ok)
            {
                IoSockClose(io);
                io->open = false;
                std::lock_guard<std::mutex> hold(sLock);
                SetPhaseLocked(ICOOP_PHASE_ERROR, "hosting could not start");
                return;
            }
            io->role = ICOOP_ROLE_HOST;
            std::lock_guard<std::mutex> hold(sLock);
            sStatus.port = 0;
            SetPhaseLocked(ICOOP_PHASE_CONNECTING, ""); // WAITING once the server gives the room
            Log("coop: hosting through the co-op server: asking it for a room");
        }
        else
        {
            // The session starts once the host is seen in the room
            // (IoRelayTick); the command is kept for it, as for joining again.
            io->rejoinCmd = cmd;
            io->haveRejoin = true;
            io->rejoining = false;
            io->rejoinWait = false;
            io->firstFullUntil = 0;
            io->firstFullWait = false;
            io->role = ICOOP_ROLE_JOIN;
            io->rememberJoin = false;
            secure_zero(cmd.password, sizeof(cmd.password));
            std::lock_guard<std::mutex> hold(sLock);
            SetPhaseLocked(ICOOP_PHASE_CONNECTING, "");
            Log("coop: joining through the co-op server");
        }
    }

    // The relay link's own changes, each pass of the I/O thread: the room
    // given, the partner in it, the link lost. False: the link is gone (the
    // co-op session ended with it).
    bool IoRelayTick(Io* io, U64 now)
    {
        RelayLink& l = io->link;
        if (l.state() == RelayState::Failed)
        {
            const char* why = l.error();
            printf("tssm: coop: the co-op server: %s\n", why);
            fflush(stdout);
            if (io->role == ICOOP_ROLE_HOST)
            {
                io->host.close(now);
            }
            else if (io->role == ICOOP_ROLE_JOIN && !io->relayJoinPending)
            {
                io->client.leave(now);
            }
            IoSockClose(io);
            io->open = false;
            io->role = ICOOP_ROLE_OFF;
            ForgetRejoin(io);
            std::lock_guard<std::mutex> hold(sLock);
            ResetPartnerLocked();
            SetPhaseLocked(ICOOP_PHASE_ENDED, why);
            return false;
        }
        bool changed = l.take_partner_changed();
        if (l.state() == RelayState::InRoom)
        {
            bool first = false;
            {
                std::lock_guard<std::mutex> hold(sLock);
                if (sStatus.relayRoom[0] == '\0')
                {
                    snprintf(sStatus.relayRoom, sizeof(sStatus.relayRoom), "%s", l.code());
                    first = true;
                    if (io->role == ICOOP_ROLE_HOST)
                    {
                        SetPhaseLocked(ICOOP_PHASE_WAITING, "");
                    }
                }
                NetAddr p;
                sStatus.relayPartner = l.partner(&p);
                if (now >= io->relayStatusAt)
                {
                    sStatus.relayPingMs = l.ping_ms();
                    io->relayStatusAt = now + kNsPerSec;
                }
            }
            if (first)
            {
                printf("tssm: coop: in room %s on the co-op server\n", l.code());
                fflush(stdout);
            }
        }
        if (io->relayJoinPending)
        {
            NetAddr hostAddr;
            if (l.partner(&hostAddr))
            {
                io->relayJoinPending = false;
                io->rejoinCmd.address = hostAddr;
                io->joinAddr = hostAddr;
                io->firstFullUntil = now + kFirstFullFor;
                NetIO nio = { io, IoSend, IoOnPayload, IoOnLog };
                if (!io->client.connect(hostAddr, io->rejoinCmd.password, io->rejoinCmd.name, io->build, io->assets,
                                        nio, now))
                {
                    IoSockClose(io);
                    io->open = false;
                    io->role = ICOOP_ROLE_OFF;
                    ForgetRejoin(io);
                    std::lock_guard<std::mutex> hold(sLock);
                    SetPhaseLocked(ICOOP_PHASE_ERROR, "joining could not start");
                    return false;
                }
                Log("coop: the host is in the room; joining its game");
            }
        }
        (void)changed;
        return true;
    }

    void IoStart(Io* io, Command& cmd)
    {
        U64 now = NowNs();
        IoClose(io, now);
        IoFindStop(io, cmd.kind == CMD_HOST ? "stopped (hosting)" : "stopped (joining)");
        io->netsim = false;
        io->rememberJoin = false;
        {
            std::lock_guard<std::mutex> hold(sLock);
            sFindWanted = false;
            sStatus.lanAnswering = false;
            sStatus.lanNote[0] = '\0';
            sStatus.lanHost = false;
            sStatus.lanAddressCount = 0;
            ResetPartnerLocked();
            sStatus.role = cmd.kind == CMD_HOST ? ICOOP_ROLE_HOST : ICOOP_ROLE_JOIN;
            sStatus.wrongPasswords = 0;
            sStatus.reconnects = 0;
            sStatus.rejoins = 0;
            sStatus.reconnectLeftSec = 0;
            sStatus.port = cmd.kind == CMD_HOST ? cmd.port : 0;
            sStatus.localAddress[0] = '\0';
            SetPhaseLocked(ICOOP_PHASE_HASHING, "");
        }

        U8 build[kBuildIdSize];
        BuildId(build);
        U8 assets[kHashSize];
        char why[160];
        size_t files = 0;
        if (!iCoopHashAssets(cmd.assets, assets, why, sizeof(why), &files))
        {
            std::lock_guard<std::mutex> hold(sLock);
            SetPhaseLocked(ICOOP_PHASE_ERROR, why);
            secure_zero(cmd.password, sizeof(cmd.password));
            return;
        }
        if (sTestAssetSalt)
        {
            assets[0] ^= 0x5A; // tests: game files that differ
        }
        {
            // Not a secret: every joining game sends the whole hash.
            std::lock_guard<std::mutex> hold(sLock);
            version_code(assets, sFilesCode);
            sFilesCount = (U32)files;
            printf("tssm: coop: game files code %s, %u files\n", sFilesCode, (unsigned)files);
            fflush(stdout);
        }
        memcpy(io->build, build, sizeof(build));
        memcpy(io->assets, assets, sizeof(assets));

        if (!net_init())
        {
            std::lock_guard<std::mutex> hold(sLock);
            SetPhaseLocked(ICOOP_PHASE_ERROR, "the network could not be started");
            secure_zero(cmd.password, sizeof(cmd.password));
            return;
        }

        NetIO nio = { io, IoSend, IoOnPayload, IoOnLog };
        io->delayedCount = 0;
        os_random(&io->rngState, sizeof(io->rngState));

        if (cmd.relay)
        {
            IoStartRelay(io, cmd, nio, build, assets);
            io->wasConnected = false;
            io->sessionId = 0;
            return;
        }

        if (cmd.kind == CMD_HOST)
        {
            if (!udp_open(&io->sock, cmd.loopback ? BindScope::Loopback : BindScope::Any, cmd.port))
            {
                std::lock_guard<std::mutex> hold(sLock);
                SetPhaseLocked(ICOOP_PHASE_ERROR,
                               "the port could not be opened (is another program, or another copy of the game, "
                               "using it?)");
                secure_zero(cmd.password, sizeof(cmd.password));
                return;
            }
            io->open = true;
            io->netsim = sNetsim && cmd.loopback;
            U16 port = cmd.port;
            if (cmd.local && (!udp_local_port(io->sock, &port) || port == 0))
            {
                IoSockClose(io);
                io->open = false;
                std::lock_guard<std::mutex> hold(sLock);
                SetPhaseLocked(ICOOP_PHASE_ERROR, "the port the system gave could not be read");
                secure_zero(cmd.password, sizeof(cmd.password));
                return;
            }
            HostConfig cfg;
            memset(&cfg, 0, sizeof(cfg));
            memcpy(cfg.build_id, build, kBuildIdSize);
            memcpy(cfg.asset_hash, assets, kHashSize);
            snprintf(cfg.name, sizeof(cfg.name), "%s", cmd.name);
            cfg.password = cmd.password;
            cfg.iters = kDefaultIters;
            bool ok = io->host.init(cfg, nio, NowNs());
            secure_zero(cmd.password, sizeof(cmd.password));
            if (!ok)
            {
                IoSockClose(io);
                io->open = false;
                std::lock_guard<std::mutex> hold(sLock);
                SetPhaseLocked(ICOOP_PHASE_ERROR, "hosting could not start");
                return;
            }
            io->role = ICOOP_ROLE_HOST;
            IoRespSetUp(io, cmd, port, build, assets);
            NetAddr local;
            std::lock_guard<std::mutex> hold(sLock);
            if (!cmd.loopback && local_address(&local))
            {
                char text[64];
                addr_format(local, text, sizeof(text));
                char* colon = strrchr(text, ':'); // the port is the tab's to show
                if (colon != NULL)
                {
                    *colon = '\0';
                }
                snprintf(sStatus.localAddress, sizeof(sStatus.localAddress), "%s", text);
            }
            sStatus.port = port;
            SetPhaseLocked(ICOOP_PHASE_WAITING, "");
            printf("tssm: coop: hosting on UDP port %u%s%s\n", (unsigned)port,
                   cmd.loopback ? " (this PC only)" : "", cmd.local ? " for local co-op" : "");
            if (cmd.local && sLogEvery > 0)
            {
                // Tests (a client with a wrong password): neither is a secret,
                // every joining game sends both.
                char b[2 * kBuildIdSize + 1], h[2 * kHashSize + 1];
                for (size_t i = 0; i < kBuildIdSize; i++)
                {
                    snprintf(b + 2 * i, 3, "%02x", build[i]);
                }
                for (size_t i = 0; i < kHashSize; i++)
                {
                    snprintf(h + 2 * i, 3, "%02x", assets[i]);
                }
                printf("tssm: coop: test: build id %s game files %s\n", b, h);
            }
            fflush(stdout);
        }
        else
        {
            bool loop = cmd.loopback || (addr_is_v4(cmd.address) && cmd.address.ip[12] == 127);
            if (!udp_open(&io->sock, loop ? BindScope::Loopback : BindScope::Any, 0))
            {
                std::lock_guard<std::mutex> hold(sLock);
                SetPhaseLocked(ICOOP_PHASE_ERROR, "the network could not be opened");
                secure_zero(cmd.password, sizeof(cmd.password));
                return;
            }
            io->open = true;
            io->netsim = sNetsim && loop;
            // M5: kept to join again after a lost connection ([coop] reconnect).
            io->rejoinCmd = cmd;
            io->haveRejoin = true;
            io->rejoining = false;
            io->rejoinWait = false;
            io->firstFullUntil = NowNs() + kFirstFullFor;
            io->firstFullWait = false;
            bool ok = io->client.connect(cmd.address, cmd.password, cmd.name, build, assets, nio, NowNs());
            secure_zero(cmd.password, sizeof(cmd.password));
            if (!ok)
            {
                ForgetRejoin(io);
                IoSockClose(io);
                io->open = false;
                std::lock_guard<std::mutex> hold(sLock);
                SetPhaseLocked(ICOOP_PHASE_ERROR, "joining could not start");
                return;
            }
            io->role = ICOOP_ROLE_JOIN;
            io->rememberJoin = cmd.remember;
            io->joinAddr = cmd.address;
            std::lock_guard<std::mutex> hold(sLock);
            SetPhaseLocked(ICOOP_PHASE_CONNECTING, "");
            Log("coop: joining");
        }
        io->wasConnected = false;
        io->sessionId = 0;
    }

    // M5: how the connection is doing (the HUD's quality line, the tab).
    void QualityLocked(const Session* s, U64 now)
    {
        sStatus.lossInPct = session_loss_in(*s);
        sStatus.lossOutPct = session_loss_out(*s);
        U64 silent = now > s->last_recv ? now - s->last_recv : 0;
        sStatus.partnerSilentMs = (U32)(silent / kNsPerMs > 0xFFFFFFFFull ? 0xFFFFFFFFull : silent / kNsPerMs);
        S32 loss = sStatus.lossInPct > sStatus.lossOutPct ? sStatus.lossInPct : sStatus.lossOutPct;
        S32 ping = sStatus.pingMs;
        if (silent >= 1500 * kNsPerMs)
        {
            if (sStatus.quality != ICOOP_QUALITY_INTERRUPTED)
            {
                printf("tssm: coop: nothing heard from the other player for %d ms: the connection is interrupted "
                       "(clock ms %lld)\n",
                       (int)(silent / kNsPerMs), (long long)(now / kNsPerMs));
                fflush(stdout);
            }
            sStatus.quality = ICOOP_QUALITY_INTERRUPTED;
            return;
        }
        if (sStatus.quality == ICOOP_QUALITY_INTERRUPTED)
        {
            printf("tssm: coop: the other player is heard again (clock ms %lld)\n", (long long)(now / kNsPerMs));
            fflush(stdout);
        }
        if (ping < 0)
        {
            sStatus.quality = ICOOP_QUALITY_NONE;
        }
        else if (ping < 120 && loss < 3)
        {
            sStatus.quality = ICOOP_QUALITY_GOOD;
        }
        else if (ping < 250 && loss < 10)
        {
            sStatus.quality = ICOOP_QUALITY_FAIR;
        }
        else
        {
            sStatus.quality = ICOOP_QUALITY_POOR;
        }
    }

    // M5 fix round: a first join (never connected in this join) refused as
    // FULL, within kFirstFullFor of its start: asked again (IoRejoin).
    bool FirstFullRetry(const Io* io, U64 now)
    {
        return !io->rejoining && io->haveRejoin && !io->wasConnected && io->firstFullUntil != 0 &&
               now < io->firstFullUntil && io->client.fail() == FAIL_REJECTED &&
               io->client.fail_reason() == REJ_FULL;
    }

    // A session that ended this way is joined again (IoRejoin).
    bool WillRejoin(const Io* io)
    {
        return io->role == ICOOP_ROLE_JOIN && io->client.fail() == FAIL_ENDED &&
               io->client.end_reason() == END_TIMEOUT && !io->stallEnded && sReconnect.load() && io->haveRejoin;
    }

    // After the session has had its datagrams and its timers: the status, the
    // partner's arrival or departure, and the end of a join.
    void IoUpdateStatus(Io* io)
    {
        bool connected = false;
        const Session* s = NULL;
        if (io->role == ICOOP_ROLE_HOST)
        {
            s = &io->host.session();
            connected = s->active;
        }
        else if (io->role == ICOOP_ROLE_JOIN)
        {
            s = &io->client.session();
            connected = io->client.state() == ClientState::Connected;
        }

        std::lock_guard<std::mutex> hold(sLock);
        U64 now = NowNs();
        if (connected && io->wasConnected && s->id != io->sessionId)
        {
            // M5: the host replaced its guest's silent session with that
            // guest's new join (iNetSession.cpp, kReplaceSilence): the old
            // partner is gone, a new session starts.
            io->stallEnded = false;
            ResetPartnerLocked();
            snprintf(sStatus.partnerName, sizeof(sStatus.partnerName), "%s", s->peer_name);
            sStatus.connected = true;
            sStatus.rejoins++;
            SetPhaseLocked(ICOOP_PHASE_CONNECTED, "");
            AddNoticeLocked("the other player joined again");
            Log("coop: the other player joined again (its old session had gone silent)");
            // The name passed iNet::name_valid; it need not be the same player's.
            printf("tssm: coop: the other player's name: %s\n", sStatus.partnerName);
            Log("coop: connected (hosting)");
        }
        else if (connected && !io->wasConnected)
        {
            io->stallEnded = false;
            io->firstFullUntil = 0; // from now on a lost session joins again (IoRejoin)
            if (io->role == ICOOP_ROLE_JOIN && io->rememberJoin)
            {
                // The host proved the password: only now does its address
                // reach config.ini, written again from the parsed numbers.
                io->rememberJoin = false;
                addr_format(io->joinAddr, sSaveAddress, sizeof(sSaveAddress));
                sSaveAddressPending.store(true, std::memory_order_release);
            }
            ResetPartnerLocked();
            snprintf(sStatus.partnerName, sizeof(sStatus.partnerName), "%s", s->peer_name);
            sStatus.connected = true;
            SetPhaseLocked(ICOOP_PHASE_CONNECTED, "");
            // The name passed iNet::name_valid (letters, digits, space, _ . -).
            printf("tssm: coop: the other player's name: %s\n", sStatus.partnerName);
            if (io->role == ICOOP_ROLE_JOIN && io->rejoining)
            {
                // M5: back after a lost connection.
                io->rejoining = false;
                io->rejoinWait = false;
                sStatus.reconnects++;
                sStatus.reconnectLeftSec = 0;
                AddNoticeLocked("joined the host's game again");
                printf("tssm: coop: reconnected %d ms after the session was lost (clock ms %lld)\n",
                       (int)((now - io->lostAt) / kNsPerMs), (long long)(now / kNsPerMs));
                fflush(stdout);
                Log("coop: connected (joined)");
            }
            else
            {
                AddNoticeLocked(io->role == ICOOP_ROLE_HOST ? "a player joined your game"
                                                            : "you joined the host's game");
                Log(io->role == ICOOP_ROLE_HOST ? "coop: connected (hosting)" : "coop: connected (joined)");
            }
        }
        else if (!connected && io->wasConnected)
        {
            EndReason e = io->role == ICOOP_ROLE_HOST ? io->host.last_end() : io->client.end_reason();
            const char* text = EndText(e);
            if (io->stallEnded)
            {
                text = "this game stopped for over a minute (a window held down, or a hang), so the session was "
                       "closed";
                AddNoticeLocked("this game stopped for too long; the session was closed");
            }
            else if (WillRejoin(io))
            {
                AddNoticeLocked("the connection was lost; joining again...");
            }
            else if (e == END_TIMEOUT && io->role == ICOOP_ROLE_HOST)
            {
                AddNoticeLocked("the connection was lost; the other player can join again");
            }
            else
            {
                AddNoticeLocked(e == END_TIMEOUT   ? "the connection was lost"
                                : e == END_STALLED ? "the other player's game stopped for too long"
                                : e == END_KICKED  ? "the host removed you from the game"
                                                   : "the other player left");
            }
            ResetPartnerLocked();
            if (io->role == ICOOP_ROLE_HOST)
            {
                SetPhaseLocked(ICOOP_PHASE_WAITING, text);
            }
            else
            {
                // A guest about to join again is in that phase from here, so
                // no frame sees it as out of the session (the save, the tab's
                // Try again).
                SetPhaseLocked(WillRejoin(io) ? ICOOP_PHASE_RECONNECTING : ICOOP_PHASE_ENDED, text);
            }
            // `text` is one of this file's constants (EndText or the above).
            printf("tssm: coop: disconnected: %s\n", text);
            fflush(stdout);
        }
        io->wasConnected = connected;
        io->sessionId = connected ? s->id : 0;

        if (connected)
        {
            // The traffic: the datagrams' own bytes (the UDP and IP headers
            // add 28 bytes to each).
            if (io->rateAt == 0 || s->bytes_out < io->rateOut || s->bytes_in < io->rateIn)
            {
                io->rateAt = now;
                io->rateOut = s->bytes_out;
                io->rateIn = s->bytes_in;
            }
            else if (now - io->rateAt >= 5 * kNsPerSec)
            {
                F64 sec = (F64)(now - io->rateAt) / 1e9;
                sStatus.kbOut = (F32)((F64)(s->bytes_out - io->rateOut) / 1024.0 / sec);
                sStatus.kbIn = (F32)((F64)(s->bytes_in - io->rateIn) / 1024.0 / sec);
                if (sLogEvery > 0)
                {
                    printf("tssm: coop: traffic out %.2f KB/s in %.2f KB/s (%llu packets out, %llu in, %llu dropped); "
                           "ping %d ms, lost in %d%% out %d%%, quality %d\n",
                           sStatus.kbOut, sStatus.kbIn, (unsigned long long)s->pkts_out,
                           (unsigned long long)s->pkts_in,
                           (unsigned long long)(s->drop_parse + s->drop_rate + s->drop_tag), (int)sStatus.pingMs,
                           (int)sStatus.lossInPct, (int)sStatus.lossOutPct, (int)sStatus.quality);
                    fflush(stdout);
                }
                io->rateAt = now;
                io->rateOut = s->bytes_out;
                io->rateIn = s->bytes_in;
            }
        }
        else
        {
            io->rateAt = 0;
        }

        if (io->role == ICOOP_ROLE_HOST)
        {
            sStatus.wrongPasswords = (U32)io->host.stats().bad_password;
        }
        if (connected)
        {
            if (s->ping_ms >= 0)
            {
                sStatus.pingMs = (S32)(s->ping_ms + 0.5);
            }
            QualityLocked(s, now);
        }
        if (io->role == ICOOP_ROLE_JOIN && !connected && io->rejoining)
        {
            // M5: joining again (IoRejoin); the phase says so until it works
            // or gives up.
            sStatus.phase = ICOOP_PHASE_RECONNECTING;
            sStatus.reconnectLeftSec =
                io->rejoinUntil > now ? (U32)((io->rejoinUntil - now + kNsPerSec - 1) / kNsPerSec) : 0;
        }
        else if (io->role == ICOOP_ROLE_JOIN && !connected)
        {
            switch (io->client.state())
            {
            case ClientState::HelloSent:
                sStatus.phase = ICOOP_PHASE_CONNECTING;
                break;
            case ClientState::AuthSent:
                sStatus.phase = ICOOP_PHASE_CHECKING;
                break;
            case ClientState::Failed:
                if (FirstFullRetry(io, now))
                {
                    sStatus.phase = ICOOP_PHASE_CONNECTING; // asked again in a moment (IoRejoin)
                }
                else if (io->client.fail() == FAIL_REJECTED)
                {
                    SetPhaseLocked(ICOOP_PHASE_REFUSED, reject_text(io->client.fail_reason()));
                    printf("tssm: coop: the join was refused: %s\n", reject_text(io->client.fail_reason()));
                    fflush(stdout);
                }
                else if (io->client.fail() == FAIL_NO_ANSWER)
                {
                    SetPhaseLocked(ICOOP_PHASE_NO_ANSWER, io->relay ? kNoAnswerRelayText : kNoAnswerText);
                    Log("coop: no answer from the host");
                }
                else if (io->client.fail() == FAIL_ENDED && sStatus.phase != ICOOP_PHASE_ENDED &&
                         sStatus.phase != ICOOP_PHASE_RECONNECTING)
                {
                    // A session that ended before this side saw it up (a
                    // welcome and a goodbye in one batch): the tab must not
                    // stay on "Checking the password...".
                    const char* text = EndText(io->client.end_reason());
                    SetPhaseLocked(ICOOP_PHASE_ENDED, text);
                    printf("tssm: coop: disconnected: %s\n", text);
                    fflush(stdout);
                }
                break;
            default:
                break;
            }
        }
    }

    // Adds REL_ACK and as many of the oldest unacknowledged events as fit in
    // the datagram beside what `p` already carries (and the PONG the session
    // adds); the rest go with a later one. The host's snapshot builder leaves
    // room for them (iCoopEventBytes).
    void AddEvents(Payload& p, iCoopRole role, bool pongDue, bool relAckDue, U16 relAck, const GameEvent* evs, U8 evCount,
                   U16 evFirst)
    {
        static U8 scratch[kMaxPayload]; // the I/O thread's own
        Role me = role == ICOOP_ROLE_HOST ? Role::Host : Role::Client;
        if (relAckDue)
        {
            p.has |= HAS_REL_ACK;
            p.rel_ack = relAck;
        }
        for (U8 n = evCount; n > 0; n--)
        {
            p.has |= HAS_EVENT;
            p.ev_first = evFirst;
            p.ev_count = n;
            memcpy(p.ev, evs, sizeof(GameEvent) * n);
            U32 had = p.has;
            if (pongDue)
            {
                p.has |= HAS_PONG;
                p.pong = 1;
            }
            size_t len = encode_payload(p, me, scratch, sizeof(scratch));
            p.has = had;
            if (len != 0)
            {
                return;
            }
        }
        p.has &= ~HAS_EVENT;
        p.ev_count = 0;
    }

    // A join that failed or a session that ended (the client says Failed).
    // M5: a session lost to silence (END_TIMEOUT) is joined again, with the
    // same address, password and name, for up to kRejoinFor: each attempt is
    // the ordinary handshake (10 s at most), and a host that has not noticed
    // the loss yet (FULL) or refuses joins for a moment (BUSY) is asked again
    // a second later. Any other refusal, the minute running out, a kick, a
    // goodbye or Leave ends it. Otherwise the socket goes.
    void IoRejoin(Io* io, U64 now)
    {
        ClientFail f = io->client.fail();
        if (FirstFullRetry(io, now))
        {
            // M5 fix round: a first join refused as FULL, asked again each
            // second for kFirstFullFor (Io::firstFullUntil).
            if (!io->firstFullWait)
            {
                io->firstFullWait = true;
                io->rejoinAt = now + kNsPerSec;
                printf("tssm: coop: the host's game has a player; asking again in a second (for up to %d s more: "
                       "a session of this player's that just ended there is let go after 3 s of silence)\n",
                       (int)((io->firstFullUntil - now) / kNsPerSec));
                fflush(stdout);
            }
            if (now < io->rejoinAt)
            {
                return;
            }
            io->firstFullWait = false;
            NetIO nio = { io, IoSend, IoOnPayload, IoOnLog };
            if (io->client.connect(io->rejoinCmd.address, io->rejoinCmd.password, io->rejoinCmd.name, io->build,
                                   io->assets, nio, now))
            {
                Log("coop: joining: asking again");
                return;
            }
            // Could not start: refused as it stands.
            io->firstFullUntil = 0;
            std::lock_guard<std::mutex> hold(sLock);
            SetPhaseLocked(ICOOP_PHASE_REFUSED, reject_text(REJ_FULL));
        }
        if (!io->rejoining && WillRejoin(io))
        {
            io->rejoining = true;
            io->rejoinWait = true;
            io->lostAt = now;
            io->rejoinUntil = now + kRejoinFor;
            io->rejoinAt = now;
            printf("tssm: coop: the connection was lost; joining again for up to %d s\n",
                   (int)(kRejoinFor / kNsPerSec));
            fflush(stdout);
        }
        if (io->rejoining)
        {
            U8 why = io->client.fail_reason();
            bool retry = f != FAIL_REJECTED || why == REJ_FULL || why == REJ_BUSY;
            if (now < io->rejoinUntil && retry)
            {
                if (!io->rejoinWait)
                {
                    // This attempt failed: the next in a moment.
                    io->rejoinWait = true;
                    io->rejoinAt = now + (f == FAIL_REJECTED ? kNsPerSec : 250 * kNsPerMs);
                    printf("tssm: coop: joining again: an attempt failed (%s); trying again\n",
                           f == FAIL_REJECTED ? reject_text(why) : "no answer");
                    fflush(stdout);
                }
                if (now < io->rejoinAt)
                {
                    return;
                }
                io->rejoinWait = false;
                NetIO nio = { io, IoSend, IoOnPayload, IoOnLog };
                if (io->client.connect(io->rejoinCmd.address, io->rejoinCmd.password, io->rejoinCmd.name, io->build,
                                       io->assets, nio, now))
                {
                    Log("coop: joining again");
                    return;
                }
            }
            std::lock_guard<std::mutex> hold(sLock);
            if (!retry)
            {
                SetPhaseLocked(ICOOP_PHASE_REFUSED, reject_text(why));
                printf("tssm: coop: joining again was refused: %s\n", reject_text(why));
            }
            else
            {
                SetPhaseLocked(ICOOP_PHASE_ENDED,
                               "the connection was lost, and joining again did not work within a minute (is the "
                               "host still hosting, and is its connection up?)");
                printf("tssm: coop: joining again gave up after %d s\n", (int)((now - io->lostAt) / kNsPerSec));
            }
            fflush(stdout);
            sStatus.reconnectLeftSec = 0;
            AddNoticeLocked("could not join the host's game again");
        }
        else
        {
            // The switch was turned off in the moment after the status said
            // this guest joins again: it does not, and the tab says so.
            std::lock_guard<std::mutex> hold(sLock);
            if (sStatus.phase == ICOOP_PHASE_RECONNECTING)
            {
                SetPhaseLocked(ICOOP_PHASE_ENDED, EndText(io->client.end_reason()));
            }
        }
        // The socket goes, and the join's settings with it.
        IoSockClose(io);
        io->open = false;
        io->role = ICOOP_ROLE_OFF;
        ForgetRejoin(io);
    }

    void IoThread()
    {
        Io* io = sIo;
        U8 buf[kRecvBuffer];
        for (;;)
        {
            Command cmd{};
            bool haveCmd = false;
            PlayerState out;
            bool haveOut = false;
            static Snapshot outSnap; // this thread's own copy (3.5 KB)
            bool haveSnap = false;
            U32 outAck = 0;
            static Pickups outPick; // this thread's own
            bool havePick = false;
            static Progress outProg;
            bool haveProg = false;
            static Counters outCnt;
            bool haveCnt = false;
            GameEvent evs[kMaxEventsPerMsg];
            U8 evCount = 0;
            U16 evFirst = 0;
            bool relAckDue = false;
            U16 relAck = 0;
            bool findStart = false, findStop = false;
            U32 findGen = 0;
            static iCoopAssetJob findJob; // this thread's own
            char findProbe[64] = "";
            {
                std::unique_lock<std::mutex> hold(sLock);
                if (!io->open && !io->finding)
                {
                    sWake.wait(hold, [] { return sQuit || sCmdPending || sFindWanted; });
                }
                if (sQuit)
                {
                    break;
                }
                // A command first: hosting or joining ends a search anyway.
                if (!sCmdPending && sFindWanted && (!io->finding || sFindGen != io->findGen))
                {
                    findStart = true;
                    findGen = sFindGen;
                    findJob = sFindAssets;
                    snprintf(findProbe, sizeof(findProbe), "%s", sFindProbe);
                }
                else if (!sFindWanted && io->finding)
                {
                    findStop = true;
                }
                if (sCmdPending)
                {
                    cmd = sCmd;
                    secure_zero(sCmd.password, sizeof(sCmd.password));
                    sCmd.kind = CMD_NONE;
                    sCmdPending = false;
                    haveCmd = true;
                }
                if (sOutPending)
                {
                    out = sOut;
                    sOutPending = false;
                    haveOut = true;
                    if (sOutSnapPending)
                    {
                        memcpy(&outSnap, &sOutSnap, offsetof(Snapshot, e) + sizeof(SnapEntry) * sOutSnap.count);
                        sOutSnapPending = false;
                        haveSnap = true;
                    }
                    outAck = sOutAck;
                    sOutAck = 0;
                    if (sOutPickPending)
                    {
                        outPick = sOutPick;
                        sOutPickPending = false;
                        havePick = true;
                    }
                    if (sOutProgPending)
                    {
                        outProg = sOutProg;
                        sOutProgPending = false;
                        haveProg = true;
                    }
                    if (sOutCntPending)
                    {
                        outCnt = sOutCnt;
                        sOutCntPending = false;
                        haveCnt = true;
                    }
                }
                // The oldest events not acknowledged yet go out again with
                // every datagram, and how far the partner's came.
                evFirst = sRelOut.base;
                evCount = (U8)(sRelOut.count < kMaxEventsPerMsg ? sRelOut.count : kMaxEventsPerMsg);
                for (U8 i = 0; i < evCount; i++)
                {
                    evs[i] = sRelOut.q[(U16)(sRelOut.base + i) & (kRelWindow - 1)];
                }
                relAckDue = sRelAckDue;
                relAck = sRelIn.expect;
            }

            U64 now = NowNs();
            if (findStart)
            {
                IoFindStart(io, now, findGen, findJob, findProbe);
                now = NowNs();
            }
            else if (findStop)
            {
                IoFindStop(io, "stopped");
            }
            if (haveCmd)
            {
                switch (cmd.kind)
                {
                case CMD_HOST:
                case CMD_JOIN:
                    IoStart(io, cmd);
                    break;
                case CMD_STOP:
                {
                    IoClose(io, now);
                    std::lock_guard<std::mutex> hold(sLock);
                    ResetPartnerLocked();
                    sStatus.role = ICOOP_ROLE_OFF;
                    SetPhaseLocked(ICOOP_PHASE_OFF, "");
                    Log("coop: stopped");
                    break;
                }
                case CMD_KICK:
                    if (io->role == ICOOP_ROLE_HOST)
                    {
                        io->host.kick(now);
                    }
                    break;
                default:
                    break;
                }
                continue;
            }
            if (!io->open && !io->finding)
            {
                continue;
            }

            {
                const UdpSocket* waits[2 + kMaxIfs];
                size_t nw = 0;
                if (io->open && !io->relay)
                {
                    waits[nw++] = &io->sock;
                }
                if (io->respOpen)
                {
                    waits[nw++] = &io->resp;
                }
                for (S32 i = 0; i < io->findSockCount; i++)
                {
                    waits[nw++] = &io->findSock[i];
                }
                if (io->open && io->relay)
                {
                    // The relay's ENet socket waits; the home network's
                    // sockets are not open while it runs (no LAN answers
                    // for a relay host), so polling them is enough.
                    if (nw > 0)
                    {
                        udp_wait_any(waits, nw, 0);
                    }
                    io->link.service(4);
                }
                else
                {
                    udp_wait_any(waits, nw, 4);
                }
            }
            now = NowNs();
            // The home network's questions and answers, on their own sockets.
            IoRespTick(io, now);
            IoFindTick(io, now);
            if (!io->open)
            {
                continue;
            }
            if (Blackout(now) != io->blackoutLogged)
            {
                io->blackoutLogged = !io->blackoutLogged;
                printf(io->blackoutLogged ? "tssm: coop: test: blackout: nothing goes out or comes in\n"
                                          : "tssm: coop: test: blackout over\n");
                fflush(stdout);
            }
            if (io->relay && !IoRelayTick(io, now))
            {
                IoUpdateStatus(io);
                continue;
            }
            for (S32 i = 0; i < 256; i++)
            {
                NetAddr from;
                int n = io->relay ? io->link.recv(buf, sizeof(buf), &from) : udp_recv(io->sock, buf, sizeof(buf), &from);
                if (n == 0)
                {
                    break;
                }
                if (n < 0)
                {
                    continue; // dropped unread: too large, truncated, or an ICMP error
                }
                if (Blackout(now))
                {
                    continue; // tests: the cable is out
                }
                if (io->role == ICOOP_ROLE_HOST)
                {
                    io->host.on_datagram(from, buf, (size_t)n, now);
                }
                else
                {
                    io->client.on_datagram(from, buf, (size_t)n, now);
                }
            }

            if (io->role == ICOOP_ROLE_HOST)
            {
                io->host.update(now);
            }
            else
            {
                io->client.update(now);
            }

            Session* s = io->role == ICOOP_ROLE_HOST ? &io->host.session() : &io->client.session();
            bool up = io->role == ICOOP_ROLE_HOST ? s->active : io->client.state() == ClientState::Connected;

            // This game's stall: how long its thread has run no frame. The
            // session stays up; the partner is told; past the limit it ends.
            U64 beat = sGameBeat.load(std::memory_order_relaxed);
            U64 stalled = beat != 0 && now > beat ? now - beat : 0;
            if (up && stalled >= kStallLimit)
            {
                printf("tssm: coop: this game ran no frame for %d s; the session is closed (DC_STALLED)\n",
                       (int)(stalled / kNsPerSec));
                fflush(stdout);
                io->stallEnded = true;
                if (io->role == ICOOP_ROLE_HOST)
                {
                    io->host.close(now, DC_STALLED); // hosting goes on
                    IoFlushDelayed(io, now, true);
                }
                else
                {
                    io->client.leave(now, DC_STALLED);
                    IoFlushDelayed(io, now, true);
                    IoSockClose(io);
                    io->open = false;
                    io->role = ICOOP_ROLE_OFF;
                    ForgetRejoin(io);
                }
                IoUpdateStatus(io);
                continue;
            }
            bool reportStall = up && stalled >= kStallReport;
            U16 stallMs = (U16)(stalled / kNsPerMs > 65535 ? 65535 : stalled / kNsPerMs);
            if (reportStall && !io->stallLogged)
            {
                io->stallLogged = true;
                printf("tssm: coop: this game ran no frame for %d ms; telling the other player (STALL)\n",
                       (int)stallMs);
                fflush(stdout);
            }
            else if (!reportStall && io->stallLogged)
            {
                io->stallLogged = false;
                printf("tssm: coop: this game runs again\n");
                fflush(stdout);
            }
            if (reportStall && !haveOut && !s->pong_due && now - io->lastStallSent >= kStallEvery)
            {
                static Payload sp; // this thread's own
                payload_clear(&sp);
                sp.has = HAS_STALL;
                sp.stall_ms = stallMs;
                // The events keep going through this game's stall.
                AddEvents(sp, io->role, false, relAckDue, relAck, evs, evCount, evFirst);
                if (io->role == ICOOP_ROLE_HOST ? io->host.send(sp, now) : io->client.send(sp, now))
                {
                    io->lastStallSent = now;
                }
            }
            if (up && (haveOut || s->pong_due))
            {
                static Payload p; // this thread's own (3.5 KB)
                payload_clear(&p);
                if (reportStall)
                {
                    p.has |= HAS_STALL;
                    p.stall_ms = stallMs;
                    io->lastStallSent = now;
                }
                if (haveOut)
                {
                    p.has |= HAS_PSTATE;
                    p.pstate = out;
                    p.pstate.slot = io->role == ICOOP_ROLE_HOST ? 0 : 1;
                    if (haveSnap && io->role == ICOOP_ROLE_HOST)
                    {
                        p.has |= HAS_SNAPSHOT;
                        memcpy(&p.snap, &outSnap, offsetof(Snapshot, e) + sizeof(SnapEntry) * outSnap.count);
                    }
                    if (outAck != 0 && io->role == ICOOP_ROLE_JOIN)
                    {
                        p.has |= HAS_SNAP_ACK;
                        p.snap_ack = outAck;
                    }
                }
                if (havePick && haveOut && io->role == ICOOP_ROLE_HOST)
                {
                    p.has |= HAS_PICKUPS;
                    p.pick = outPick;
                }
                if (haveProg && haveOut && io->role == ICOOP_ROLE_HOST)
                {
                    p.has |= HAS_PROGRESS;
                    p.prog = outProg;
                }
                if (haveCnt && haveOut && io->role == ICOOP_ROLE_HOST)
                {
                    p.has |= HAS_COUNTERS;
                    p.cnt = outCnt;
                }
                AddEvents(p, io->role, s->pong_due, relAckDue, relAck, evs, evCount, evFirst);
                bool sent = io->role == ICOOP_ROLE_HOST ? io->host.send(p, now) : io->client.send(p, now);
                if (!sent && (p.has & HAS_SNAPSHOT))
                {
                    // The builder keeps a snapshot within its budget, so this
                    // does not happen; if it did, the state still goes out.
                    p.has &= ~HAS_SNAPSHOT;
                    io->host.send(p, now);
                }
            }

            // M4: events waiting and nothing sent for a while, as between
            // levels when this game sends no state: they go on their own (with
            // how far the partner's came).
            if (up && evCount > 0 && now - io->lastSent >= kEventsEvery)
            {
                static Payload ep; // this thread's own
                payload_clear(&ep);
                AddEvents(ep, io->role, false, relAckDue, relAck, evs, evCount, evFirst);
                if (ep.has != 0)
                {
                    if (io->role == ICOOP_ROLE_HOST)
                    {
                        io->host.send(ep, now);
                    }
                    else
                    {
                        io->client.send(ep, now);
                    }
                }
            }

            IoFlushDelayed(io, now, false);
            IoUpdateStatus(io);

            if (io->role == ICOOP_ROLE_JOIN && io->client.state() == ClientState::Failed)
            {
                IoRejoin(io, now);
            }
        }

        IoClose(io, NowNs());
        IoFindStop(io, "stopped (the game closes)");
    }

    void EnsureThread()
    {
        if (!sThreadStarted)
        {
            sIo = new Io();
            sIo->open = false;
            sIo->role = ICOOP_ROLE_OFF;
            sQuit = false;
            sThread = new std::thread(IoThread);
            sThreadStarted = true;
            atexit(iCoopShutdown);
        }
    }

    void Post(const Command& cmd)
    {
        EnsureThread();
        sGameBeat.store(NowNs(), std::memory_order_relaxed);
        std::lock_guard<std::mutex> hold(sLock);
        sCmd = cmd;
        sCmdPending = true;
        sWake.notify_all();
    }

    // --- startup settings ------------------------------------------------------------

    const char* Env(const char* name)
    {
        const char* v = getenv(name);
        return (v != NULL && v[0] != '\0') ? v : NULL;
    }

    bool ParsePort(const char* text, U16* out)
    {
        if (text == NULL)
        {
            return false;
        }
        char* end = NULL;
        long v = strtol(text, &end, 10);
        if (end == text || *end != '\0' || v < 1024 || v > 65535)
        {
            return false;
        }
        *out = (U16)v;
        return true;
    }

    void SetName(const char* text)
    {
        if (text != NULL && name_valid(text, strnlen(text, kMaxName + 1)))
        {
            snprintf(sName, sizeof(sName), "%s", text);
        }
        else if (text != NULL && text[0] != '\0')
        {
            printf("tssm: coop: the name is 1-16 letters, digits, spaces, _ . or -; using \"%s\"\n", sName);
        }
    }
} // namespace

// --- the shared seam (iCoopInternal.h) ------------------------------------------------

bool iCoopRunning()
{
    return sRunning;
}

bool iCoopConnected()
{
    if (!sRunning)
    {
        return false;
    }
    std::lock_guard<std::mutex> hold(sLock);
    return sStatus.connected;
}

bool iCoopJoiningAgain()
{
    if (!sRunning)
    {
        return false;
    }
    std::lock_guard<std::mutex> hold(sLock);
    return sStatus.phase == ICOOP_PHASE_RECONNECTING;
}

U8 iCoopLocalSlot()
{
    std::lock_guard<std::mutex> hold(sLock);
    return sStatus.role == ICOOP_ROLE_JOIN ? 1 : 0;
}

void iCoopNoteScene(U32 scene)
{
    sLocalScene.store(scene, std::memory_order_relaxed);
}

void iCoopPostLocal(const PlayerState& s, const Snapshot* snap, U32 snapAck, const Pickups* pick,
                    const Progress* prog, const Counters* cnt)
{
    if (!sRunning)
    {
        return;
    }
    std::lock_guard<std::mutex> hold(sLock);
    sOut = s;
    sOutPending = true;
    if (snap != NULL)
    {
        memcpy(&sOutSnap, snap, offsetof(Snapshot, e) + sizeof(SnapEntry) * snap->count);
        sOutSnapPending = true;
    }
    if (snapAck != 0)
    {
        sOutAck = snapAck;
    }
    if (pick != NULL)
    {
        sOutPick = *pick;
        sOutPickPending = true;
    }
    if (prog != NULL)
    {
        sOutProg = *prog;
        sOutProgPending = true;
    }
    if (cnt != NULL)
    {
        sOutCnt = *cnt;
        sOutCntPending = true;
    }
}

bool iCoopPushEvent(const GameEvent& e)
{
    if (!sRunning)
    {
        return false;
    }
    std::lock_guard<std::mutex> hold(sLock);
    if (!sStatus.connected)
    {
        return false;
    }
    return sRelOut.push(e);
}

bool iCoopPeerLevelSignal(U8 type, U32 scene)
{
    std::lock_guard<std::mutex> hold(sLock);
    if (!sStatus.connected || scene == 0)
    {
        return false;
    }
    return type == EV_READY ? sPeerLevel.ready_scene == scene : (type == EV_GO ? sPeerLevel.go_scene == scene : false);
}

U32 iCoopPeerLevelScene(U8 type)
{
    std::lock_guard<std::mutex> hold(sLock);
    if (!sStatus.connected)
    {
        return 0;
    }
    return type == EV_READY ? sPeerLevel.ready_scene : (type == EV_GO ? sPeerLevel.go_scene : 0);
}

void iCoopClearLevelSignal(U8 type)
{
    std::lock_guard<std::mutex> hold(sLock);
    if (type == EV_READY)
    {
        sPeerLevel.ready_scene = 0;
    }
    else if (type == EV_GO)
    {
        sPeerLevel.go_scene = 0;
    }
}

bool iCoopTakeEvent(GameEvent* out)
{
    std::lock_guard<std::mutex> hold(sLock);
    if (sEvInCount == 0)
    {
        return false;
    }
    *out = sEvIn[0];
    memmove(sEvIn, sEvIn + 1, sizeof(GameEvent) * (sEvInCount - 1));
    sEvInCount--;
    return true;
}

size_t iCoopEventBytes()
{
    std::lock_guard<std::mutex> hold(sLock);
    if (sRelOut.count == 0)
    {
        return 0;
    }
    GameEvent evs[kMaxEventsPerMsg];
    U8 n = (U8)(sRelOut.count < kMaxEventsPerMsg ? sRelOut.count : kMaxEventsPerMsg);
    for (U8 i = 0; i < n; i++)
    {
        evs[i] = sRelOut.q[(U16)(sRelOut.base + i) & (kRelWindow - 1)];
    }
    return event_msg_size(evs, n);
}

bool iCoopTakeProgress(Progress* out)
{
    std::lock_guard<std::mutex> hold(sLock);
    if (!sInProgPending)
    {
        return false;
    }
    *out = sInProg;
    sInProgPending = false;
    return true;
}

bool iCoopTakeCounters(Counters* out)
{
    std::lock_guard<std::mutex> hold(sLock);
    if (!sInCntPending)
    {
        return false;
    }
    *out = sInCnt;
    sInCntPending = false;
    return true;
}

bool iCoopTakePickups(Pickups* out)
{
    std::lock_guard<std::mutex> hold(sLock);
    if (!sInPickPending)
    {
        return false;
    }
    *out = sInPick;
    sInPickPending = false;
    return true;
}

bool iCoopTakeSnapAck(U32* seq)
{
    std::lock_guard<std::mutex> hold(sLock);
    if (!sAckInPending)
    {
        return false;
    }
    *seq = sAckIn;
    sAckInPending = false;
    return true;
}

bool iCoopTakeSnapshot(Snapshot* out, U64* at)
{
    std::lock_guard<std::mutex> hold(sLock);
    if (sSnapQCount == 0)
    {
        return false;
    }
    memcpy(out, &sSnapQ[0].s, offsetof(Snapshot, e) + sizeof(SnapEntry) * sSnapQ[0].s.count);
    *at = sSnapQ[0].at;
    memmove(sSnapQ, sSnapQ + 1, sizeof(QueuedSnap) * (kSnapQueue - 1));
    sSnapQCount--;
    return true;
}

void iCoopSetWorldView(bool shared, S32 things, S32 mismatch, bool following)
{
    std::lock_guard<std::mutex> hold(sLock);
    sStatus.worldShared = shared;
    sStatus.worldThings = things;
    sStatus.worldMismatch = mismatch;
    sStatus.following = following;
}

bool iCoopTakeGoToHost()
{
    bool v = sGoToHost;
    sGoToHost = false;
    return v;
}

S32 iCoopTakeSamples(iCoopSample* out, S32 max, U32* sinceSeq)
{
    std::lock_guard<std::mutex> hold(sLock);
    S32 first = 0;
    while (first < sQueueCount && sQueue[first].seq <= *sinceSeq)
    {
        first++;
    }
    S32 n = sQueueCount - first;
    if (n > max)
    {
        first += n - max;
        n = max;
    }
    for (S32 i = 0; i < n; i++)
    {
        out[i] = sQueue[first + i];
    }
    if (n > 0)
    {
        *sinceSeq = out[n - 1].seq;
    }
    return n;
}

bool iCoopPartnerStalled(U32* ms)
{
    std::lock_guard<std::mutex> hold(sLock);
    U64 now = NowNs();
    if (!sStatus.connected || sPeerStallAt == 0 || now - sPeerStallAt >= kPeerStallFor)
    {
        if (ms != NULL)
        {
            *ms = 0;
        }
        return false;
    }
    if (ms != NULL)
    {
        *ms = sPeerStallMs + (U32)((now - sPeerStallAt) / kNsPerMs);
    }
    return true;
}

U32 iCoopSessionEpoch()
{
    std::lock_guard<std::mutex> hold(sLock);
    return sEpoch;
}

U64 iCoopNowNs()
{
    return NowNs();
}

S32 iCoopLogEvery()
{
    return sLogEvery;
}

void iCoopSetPartnerView(bool fresh, U32 scene, U8 character, bool inMyLevel, const char* levelName, U8 model)
{
    std::lock_guard<std::mutex> hold(sLock);
    sStatus.partnerFresh = fresh;
    sStatus.partnerScene = scene;
    sStatus.partnerCharacter = character;
    sStatus.partnerModel = model;
    sStatus.partnerInMyLevel = inMyLevel;
    snprintf(sStatus.partnerLevelName, sizeof(sStatus.partnerLevelName), "%s", levelName != NULL ? levelName : "");
}

void iCoopSetNameTag(bool valid, F32 x, F32 y, bool inFront)
{
    sTagValid = valid;
    sTagX = x;
    sTagY = y;
    sTagInFront = inFront;
}

void iCoopAddNotice(const char* text)
{
    std::lock_guard<std::mutex> hold(sLock);
    AddNoticeLocked(text);
}

// --- iCoop.h --------------------------------------------------------------------------

namespace
{
    // M5 fix round: the Co-op tab's "Co-op version" line, in the log too
    // (both players' must be the same; tests compare them).
    void LogVersion()
    {
        char version[9];
        iCoopVersionCode(version);
        printf("tssm: coop: co-op version %s\n", version);
        fflush(stdout);
    }
} // namespace

void iCoopInit()
{
    sStatus.role = ICOOP_ROLE_OFF;
    sStatus.phase = ICOOP_PHASE_OFF;
    sStatus.pingMs = -1;
    sStatus.kbOut = sStatus.kbIn = -1.0f;

    // [coop], then the environment, then the command line.
    const char* mode = iConfigGetString("coop.mode", "off");
    S32 port = iConfigGetInt("coop.port", kDefaultPort);
    if (port >= 1024 && port <= 65535)
    {
        sPort = (U16)port;
    }
    snprintf(sAddress, sizeof(sAddress), "%s", iConfigGetString("coop.address", ""));
    snprintf(sServer, sizeof(sServer), "%s", iConfigGetString("coop.server", "10.0.0.5:7865"));
    SetName(iConfigGetString("coop.name", ""));
    sLoopback = strcmp(iConfigGetString("coop.bind", "any"), "loopback") == 0;
    sNameTag = iConfigGetBool("coop.nametag", TRUE) != 0;
    sFollow = iConfigGetBool("coop.follow", TRUE) != 0;
    sReconnect = iConfigGetBool("coop.reconnect", TRUE) != 0;
    sQualityHud = iConfigGetBool("coop.quality", TRUE) != 0;
    sShowCode = iConfigGetBool("coop.show_code", TRUE) != 0;
    sLanVisible = iConfigGetBool("coop.lan_visible", TRUE) != 0;
    sTestBuildSalt = Env("TSSM_TEST_COOP_BUILD_SALT") != NULL;
    sTestAssetSalt = Env("TSSM_TEST_COOP_ASSET_SALT") != NULL;
    sStatus.lossInPct = sStatus.lossOutPct = -1;

    char modeBuf[16];
    snprintf(modeBuf, sizeof(modeBuf), "%s", mode);
    if (Env("TSSM_COOP"))
    {
        snprintf(modeBuf, sizeof(modeBuf), "%s", Env("TSSM_COOP"));
    }
    if (Env("TSSM_COOP_PORT") && !ParsePort(Env("TSSM_COOP_PORT"), &sPort))
    {
        printf("tssm: coop: TSSM_COOP_PORT is not a port from 1024 to 65535; ignored\n");
    }
    if (Env("TSSM_COOP_ADDR"))
    {
        snprintf(sAddress, sizeof(sAddress), "%s", Env("TSSM_COOP_ADDR"));
    }
    if (Env("TSSM_COOP_NAME"))
    {
        SetName(Env("TSSM_COOP_NAME"));
    }
    if (Env("TSSM_COOP_BIND"))
    {
        sLoopback = strcmp(Env("TSSM_COOP_BIND"), "loopback") == 0;
    }
    if (Env("TSSM_COOP_FOLLOW"))
    {
        sFollow = strcmp(Env("TSSM_COOP_FOLLOW"), "0") != 0 && strcmp(Env("TSSM_COOP_FOLLOW"), "off") != 0;
    }
    if (Env("TSSM_COOP_RECONNECT"))
    {
        sReconnect = strcmp(Env("TSSM_COOP_RECONNECT"), "0") != 0 && strcmp(Env("TSSM_COOP_RECONNECT"), "off") != 0;
    }
    if (Env("TSSM_COOP_LOG"))
    {
        sLogEvery = atoi(Env("TSSM_COOP_LOG"));
        if (sLogEvery < 0)
        {
            sLogEvery = 0;
        }
    }

    S32 argc = 0;
    char** argv = NULL;
    if (iHostCommandLine(&argc, &argv) && argv != NULL)
    {
        for (S32 i = 1; i < argc; i++)
        {
            const char* a = argv[i];
            if (strcmp(a, "--host") == 0)
            {
                snprintf(modeBuf, sizeof(modeBuf), "host");
            }
            else if (strncmp(a, "--host=", 7) == 0)
            {
                snprintf(modeBuf, sizeof(modeBuf), "host");
                if (!ParsePort(a + 7, &sPort))
                {
                    printf("tssm: coop: --host= takes a port from 1024 to 65535; using %u\n", (unsigned)sPort);
                }
            }
            else if (strncmp(a, "--join=", 7) == 0)
            {
                snprintf(modeBuf, sizeof(modeBuf), "join");
                snprintf(sAddress, sizeof(sAddress), "%s", a + 7);
            }
            else if (strncmp(a, "--coop-name=", 12) == 0)
            {
                SetName(a + 12);
            }
        }
    }

    // Tests: the home network's discovery on 127.0.0.1 only, at a port of
    // the test's own, and the finder asking 127.x addresses instead of
    // broadcasting (iNetDisc.h). Anything else is ignored.
    if (Env("TSSM_COOP_LAN_PORT"))
    {
        if (ParsePort(Env("TSSM_COOP_LAN_PORT"), &sLanPort))
        {
            sLanTest = true;
            printf(
                "tssm: coop: test: the home network's discovery on 127.0.0.1 port %u only (TSSM_COOP_LAN_PORT)\n",
                (unsigned)sLanPort);
        }
        else
        {
            sLanPort = kDiscPort;
            printf("tssm: coop: TSSM_COOP_LAN_PORT is not a port from 1024 to 65535; ignored\n");
        }
    }
    if (Env("TSSM_COOP_LAN_TARGET"))
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s", Env("TSSM_COOP_LAN_TARGET"));
        for (char* t = strtok(buf, ","); t != NULL; t = strtok(NULL, ","))
        {
            NetAddr a;
            if (sLanTest && sLanTargetCount < kMaxLanTargets && addr_parse(t, sLanPort, &a) &&
                addr_is_v4(a) && a.ip[12] == 127)
            {
                a.port = sLanPort;
                sLanTargets[sLanTargetCount++] = a;
            }
            else
            {
                printf("tssm: coop: a TSSM_COOP_LAN_TARGET entry is not a 127.x.x.x address (with "
                       "TSSM_COOP_LAN_PORT); ignored\n");
            }
        }
    }

    // Local co-op (iCoopLocal.h): its own session; [coop] mode, bind, address
    // and port, TSSM_COOP and --host / --join are not used.
    if (iCoopLocalGetRole() != ICOOP_LOCAL_OFF)
    {
        if (iCoopLocalPlayer2())
        {
            // Player 2's own name (player2.ini), else this; and it goes where
            // Player 1 goes.
            if (iConfigGetString("coop.name", "")[0] == '\0' && Env("TSSM_COOP_NAME") == NULL)
            {
                SetName("Player 2");
            }
            sFollow = true;
            sReconnect = true;
        }
        iCoopLocalStartSession();
        fflush(stdout);
        return;
    }

    // TSSM_COOP_NETSIM: loopback only, so it can never slow a real game.
    const char* sim = Env("TSSM_COOP_NETSIM");
    if (sim != NULL)
    {
        int d = 0, j = 0, l = 0;
        if (sscanf(sim, "%d,%d,%d", &d, &j, &l) == 3 && d >= 0 && d <= 2000 && j >= 0 && j <= 2000 && l >= 0 &&
            l <= 90)
        {
            sSimDelayMs = d;
            sSimJitterMs = j;
            sSimLossPct = l;
            sNetsim = true; // checked again against the address below
        }
        else
        {
            printf("tssm: coop: TSSM_COOP_NETSIM is not delay_ms,jitter_ms,loss_pct; ignored\n");
        }
    }

    const char* password = Env("TSSM_COOP_PASSWORD");
    char why[160];
    if (strcmp(modeBuf, "host") == 0)
    {
        if (sNetsim && !sLoopback)
        {
            printf("tssm: coop: TSSM_COOP_NETSIM needs TSSM_COOP_BIND=loopback; ignored\n");
            sNetsim = false;
        }
        if (password == NULL)
        {
            iCoopNewJoinCode(sStartupCode);
            password = sStartupCode;
        }
        if (!iCoopHost(sPort, password, sName, sLoopback, why, sizeof(why)))
        {
            printf("tssm: coop: hosting did not start: %s\n", why);
        }
    }
    else if (strcmp(modeBuf, "join") == 0)
    {
        NetAddr a;
        if (sNetsim && !(addr_parse(sAddress, sPort, &a) && addr_is_v4(a) && a.ip[12] == 127))
        {
            printf("tssm: coop: TSSM_COOP_NETSIM needs a 127.x.x.x address; ignored\n");
            sNetsim = false;
        }
        if (password == NULL)
        {
            sNeedsPassword = true; // the overlay opens on the Co-op tab to ask
            printf("tssm: coop: joining needs the host's password; the Co-op tab asks for it\n");
        }
        else if (!iCoopJoin(sAddress, password, sName, why, sizeof(why)))
        {
            printf("tssm: coop: joining did not start: %s\n", why);
        }
    }
    else
    {
        sNetsim = false;
        if (strcmp(modeBuf, "off") != 0)
        {
            printf("tssm: coop: mode '%s' is not off, host or join; co-op is off\n", modeBuf);
        }
    }
    fflush(stdout);
}

void iCoopShutdown()
{
    iCoopLocalShutdown();
    if (!sThreadStarted)
    {
        return;
    }
    {
        std::lock_guard<std::mutex> hold(sLock);
        sQuit = true;
        sWake.notify_all();
    }
    sThread->join();
    delete sThread;
    sThread = NULL;
    sThreadStarted = false;
    sRunning = false;
    delete sIo;
    sIo = NULL;
    net_shutdown();
}

void iCoopGetStatus(iCoopStatus* out)
{
    {
        std::lock_guard<std::mutex> hold(sLock);
        *out = sStatus;
    }
    U32 ms = 0;
    out->partnerStallMs = iCoopPartnerStalled(&ms) ? ms : 0;
}

void iCoopGameBeat()
{
    iCoopLocalPoll();
    if (sRunning)
    {
        sGameBeat.store(NowNs(), std::memory_order_relaxed);
    }
    if (sSaveAddressPending.load(std::memory_order_acquire))
    {
        // A joined host's address, once its session is up (IoUpdateStatus).
        char text[64];
        {
            std::lock_guard<std::mutex> hold(sLock);
            snprintf(text, sizeof(text), "%s", sSaveAddress);
            sSaveAddressPending.store(false, std::memory_order_relaxed);
        }
        snprintf(sAddress, sizeof(sAddress), "%s", text);
        iConfigSet("coop.address", text);
        Log(iConfigSave() ? "coop: the host's address is kept for the next time ([coop] address)" :
                            "coop: the host's address could not be written to config.ini");
    }
}

bool iCoopHost(U16 port, const char* password, const char* name, bool loopback, char* why, size_t whySize)
{
    Command cmd{};
    cmd.kind = CMD_HOST;
    if (port < 1024)
    {
        Why(why, whySize, "the port must be from 1024 to 65535");
        return false;
    }
    if (password == NULL || !normalize_code(password, cmd.password, sizeof(cmd.password)))
    {
        Why(why, whySize, "the password must be 1 to 63 characters");
        return false;
    }
    if (name == NULL || !name_valid(name, strnlen(name, kMaxName + 1)))
    {
        Why(why, whySize, "the name must be 1 to 16 letters, digits, spaces, _ . or -");
        secure_zero(cmd.password, sizeof(cmd.password));
        return false;
    }
    cmd.port = port;
    cmd.loopback = loopback;
    snprintf(cmd.name, sizeof(cmd.name), "%s", name);
    cmd.assets = AssetJob();
    sRunning = true;
    sNeedsPassword = false;
    LogVersion();
    Post(cmd);
    secure_zero(cmd.password, sizeof(cmd.password));
    return true;
}

bool iCoopHostLocal(const char* password, const char* name, char* why, size_t whySize)
{
    Command cmd{};
    cmd.kind = CMD_HOST;
    if (password == NULL || !normalize_code(password, cmd.password, sizeof(cmd.password)))
    {
        Why(why, whySize, "the password must be 1 to 63 characters");
        return false;
    }
    if (name == NULL || !name_valid(name, strnlen(name, kMaxName + 1)))
    {
        Why(why, whySize, "the name must be 1 to 16 letters, digits, spaces, _ . or -");
        secure_zero(cmd.password, sizeof(cmd.password));
        return false;
    }
    cmd.port = 0;
    cmd.loopback = true;
    cmd.local = true;
    snprintf(cmd.name, sizeof(cmd.name), "%s", name);
    cmd.assets = AssetJob();
    {
        // Never a stale port or phase from an earlier session (Bring back).
        std::lock_guard<std::mutex> hold(sLock);
        sStatus.role = ICOOP_ROLE_HOST;
        sStatus.port = 0;
        SetPhaseLocked(ICOOP_PHASE_HASHING, "");
    }
    sRunning = true;
    sNeedsPassword = false;
    LogVersion();
    Post(cmd);
    secure_zero(cmd.password, sizeof(cmd.password));
    return true;
}

namespace
{
    bool JoinAt(const NetAddr& address, const char* password, const char* name, bool remember,
                char* why, size_t whySize)
    {
        Command cmd{};
        cmd.kind = CMD_JOIN;
        cmd.address = address;
        cmd.remember = remember;
        if (password == NULL || !normalize_code(password, cmd.password, sizeof(cmd.password)))
        {
            Why(why, whySize, "the password must be 1 to 63 characters");
            return false;
        }
        if (name == NULL || !name_valid(name, strnlen(name, kMaxName + 1)))
        {
            Why(why, whySize, "the name must be 1 to 16 letters, digits, spaces, _ . or -");
            secure_zero(cmd.password, sizeof(cmd.password));
            return false;
        }
        cmd.loopback = sLoopback;
        snprintf(cmd.name, sizeof(cmd.name), "%s", name);
        cmd.assets = AssetJob();
        sRunning = true;
        sNeedsPassword = false;
        LogVersion();
        Post(cmd);
        secure_zero(cmd.password, sizeof(cmd.password));
        return true;
    }
} // namespace

bool iCoopJoin(const char* address, const char* password, const char* name, char* why,
               size_t whySize, bool remember)
{
    NetAddr a;
    if (address == NULL || !addr_parse(address, sPort, &a))
    {
        Why(why, whySize,
            "the address must be numbers: an IPv4 address like 203.0.113.7 or 203.0.113.7:2171, or an IPv6 "
            "address in brackets");
        return false;
    }
    return JoinAt(a, password, name, remember, why, whySize);
}

bool iCoopJoinFound(const char* address, const char* password, const char* name, char* why,
                    size_t whySize)
{
    NetAddr a;
    bool listed = false;
    {
        std::lock_guard<std::mutex> hold(sLock);
        for (S32 i = 0; i < sFoundCount && !listed; i++)
        {
            char text[64];
            addr_format(sFound[i].addr, text, sizeof(text));
            if (!sFound[i].newer && address != NULL && strcmp(text, address) == 0)
            {
                a = sFound[i].addr;
                listed = true;
            }
        }
        if (!listed && sKept && address != NULL)
        {
            char text[64];
            addr_format(sKeptAddr, text, sizeof(text));
            if (strcmp(text, address) == 0)
            {
                a = sKeptAddr;
                listed = true;
            }
        }
    }
    if (!listed)
    {
        Why(why, whySize, "that game is no longer in the list of games on this network");
        return false;
    }
    return JoinAt(a, password, name, true, why, whySize);
}

bool iCoopKeepFound(const char* address)
{
    std::lock_guard<std::mutex> hold(sLock);
    sKept = false;
    for (S32 i = 0; i < sFoundCount && address != NULL; i++)
    {
        char text[64];
        addr_format(sFound[i].addr, text, sizeof(text));
        if (!sFound[i].newer && strcmp(text, address) == 0)
        {
            sKeptAddr = sFound[i].addr;
            sKept = true;
            break;
        }
    }
    return sKept;
}

void iCoopStop()
{
    if (!sThreadStarted)
    {
        return;
    }
    Command cmd{};
    cmd.kind = CMD_STOP;
    Post(cmd);
    sRunning = false;
    sTagValid = false;
}

void iCoopKick()
{
    if (!sThreadStarted)
    {
        return;
    }
    Command cmd{};
    cmd.kind = CMD_KICK;
    Post(cmd);
}

void iCoopNewJoinCode(char out[16])
{
    // 32 symbols, 5 bits each: 12 of them are 60 bits. No I, L, O or U, so
    // nothing reads as another symbol over the phone.
    static const char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    // Tests only (TSSM_TEST_COOP_CODE, and only in the loopback test mode): a
    // fixed code of the same form, so a test can type the code a "Host on
    // this network" shows. The code itself is never logged.
    const char* fixed = sLanTest ? Env("TSSM_TEST_COOP_CODE") : NULL;
    if (fixed != NULL && strlen(fixed) == 12 && strspn(fixed, kAlphabet) == 12)
    {
        static bool said;
        if (!said)
        {
            said = true;
            printf(
                "tssm: coop: test: new join codes are a fixed test code (TSSM_TEST_COOP_CODE)\n");
            fflush(stdout);
        }
        snprintf(out, 16, "%.4s-%.4s-%.4s", fixed, fixed + 4, fixed + 8);
        return;
    }
    U8 r[12];
    if (!os_random(r, sizeof(r)))
    {
        // No guessable stand-in (a clock is one): no code, so hosting with
        // it is refused and the tab says why.
        out[0] = '\0';
        return;
    }
    S32 o = 0;
    for (int i = 0; i < 12; i++)
    {
        if (i == 4 || i == 8)
        {
            out[o++] = '-';
        }
        out[o++] = kAlphabet[r[i] & 31];
    }
    out[o] = '\0';
    secure_zero(r, sizeof(r));
}

const char* iCoopDefaultName()
{
    return sName;
}

U16 iCoopDefaultPort()
{
    return sPort;
}

const char* iCoopDefaultAddress()
{
    return sAddress;
}

const char* iCoopDefaultServer()
{
    return sServer;
}

namespace
{
    // Letters, digits, '.', '-', ':', '[' and ']' only: a name or an address,
    // never a path or anything a shell or the log could take for more.
    bool ServerTextValid(const char* s)
    {
        size_t n = s ? strnlen(s, 128) : 0;
        if (n == 0 || n >= 128)
        {
            return false;
        }
        for (size_t i = 0; i < n; i++)
        {
            char c = s[i];
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                      c == '-' || c == ':' || c == '[' || c == ']';
            if (!ok)
            {
                return false;
            }
        }
        return true;
    }

    bool PostRelay(CommandKind kind, const char* server, const char* room, const char* password, const char* name,
                   char* why, size_t whySize)
    {
        Command cmd{};
        cmd.kind = kind;
        cmd.relay = true;
        if (!ServerTextValid(server))
        {
            Why(why, whySize, "the co-op server must be a name or an address, like tssm.example.net or 203.0.113.7:7865");
            return false;
        }
        snprintf(cmd.server, sizeof(cmd.server), "%s", server);
        if (kind == CMD_JOIN)
        {
            // Spaces and dashes ignored, letters not case-sensitive.
            size_t k = 0;
            for (const char* p = room ? room : ""; *p && k < 5; p++)
            {
                if (*p == ' ' || *p == '-')
                {
                    continue;
                }
                cmd.room[k++] = (char)(*p >= 'a' && *p <= 'z' ? *p - 'a' + 'A' : *p);
            }
            if (k != 4)
            {
                Why(why, whySize, "the room code is 4 letters or digits");
                return false;
            }
            cmd.room[4] = ' ';
        }
        if (password == NULL || !normalize_code(password, cmd.password, sizeof(cmd.password)))
        {
            Why(why, whySize, "the password must be 1 to 63 characters");
            return false;
        }
        if (name == NULL || !name_valid(name, strnlen(name, kMaxName + 1)))
        {
            Why(why, whySize, "the name must be 1 to 16 letters, digits, spaces, _ . or -");
            secure_zero(cmd.password, sizeof(cmd.password));
            return false;
        }
        snprintf(cmd.name, sizeof(cmd.name), "%s", name);
        cmd.assets = AssetJob();
        if (strcmp(sServer, server) != 0)
        {
            snprintf(sServer, sizeof(sServer), "%s", server);
            iConfigSet("coop.server", sServer);
            iConfigSave();
        }
        sRunning = true;
        sNeedsPassword = false;
        LogVersion();
        Post(cmd);
        secure_zero(cmd.password, sizeof(cmd.password));
        return true;
    }
} // namespace

bool iCoopHostServer(const char* server, const char* password, const char* name, char* why, size_t whySize)
{
    return PostRelay(CMD_HOST, server, NULL, password, name, why, whySize);
}

bool iCoopJoinServer(const char* server, const char* room, const char* password, const char* name, char* why,
                     size_t whySize)
{
    return PostRelay(CMD_JOIN, server, room, password, name, why, whySize);
}

bool iCoopDefaultLoopback()
{
    return sLoopback;
}

bool iCoopNeedsPassword()
{
    return sNeedsPassword;
}

const char* iCoopStartupJoinCode()
{
    return sStartupCode;
}

bool iCoopNameTagEnabled()
{
    return sNameTag;
}

void iCoopSetNameTagEnabled(bool on)
{
    sNameTag = on;
}

bool iCoopFollowEnabled()
{
    return sFollow;
}

void iCoopSetFollowEnabled(bool on)
{
    sFollow = on;
}

bool iCoopReconnectEnabled()
{
    return sReconnect.load();
}

void iCoopSetReconnectEnabled(bool on)
{
    sReconnect = on;
}

bool iCoopQualityHudEnabled()
{
    return sQualityHud;
}

void iCoopSetQualityHudEnabled(bool on)
{
    sQualityHud = on;
}

bool iCoopShowCodeEnabled()
{
    return sShowCode;
}

void iCoopSetShowCodeEnabled(bool on)
{
    sShowCode = on;
}

void iCoopVersionCode(char out[9])
{
    U8 id[kBuildIdSize];
    BuildId(id);
    version_code(id, out);
}

bool iCoopGameFilesCode(char out[9], U32* files)
{
    std::lock_guard<std::mutex> hold(sLock);
    if (sFilesCode[0] == '\0')
    {
        out[0] = '\0';
        *files = 0;
        return false;
    }
    memcpy(out, sFilesCode, 9);
    *files = sFilesCount;
    return true;
}

bool iCoopFindGames(bool on)
{
    if (!on)
    {
        if (sThreadStarted)
        {
            std::lock_guard<std::mutex> hold(sLock);
            sFindWanted = false;
            sWake.notify_all();
        }
        return true;
    }
    // Not while hosting or joining, nor in local co-op (two players already).
    if (sRunning || iCoopLocalGetRole() != ICOOP_LOCAL_OFF)
    {
        return false;
    }
    iCoopAssetJob job = AssetJob();
    EnsureThread();
    std::lock_guard<std::mutex> hold(sLock);
    sFindWanted = true;
    sFindGen++;
    sFindAssets = job;
    snprintf(sFindProbe, sizeof(sFindProbe), "%s", iConfigGetString("coop.address", ""));
    sWake.notify_all();
    return true;
}

S32 iCoopGetFound(iCoopFoundGame* out, S32 max)
{
    std::lock_guard<std::mutex> hold(sLock);
    S32 n = sFoundCount < max ? sFoundCount : max;
    for (S32 i = 0; i < n; i++)
    {
        const Found& f = sFound[i];
        iCoopFoundGame& g = out[i];
        memset(&g, 0, sizeof(g));
        char text[64];
        addr_format(f.addr, text, sizeof(text));
        if (f.newer)
        {
            char* colon = strrchr(text, ':'); // no game port is known
            if (colon != NULL)
            {
                *colon = '\0';
            }
        }
        snprintf(g.address, sizeof(g.address), "%s", text);
        snprintf(g.name, sizeof(g.name), "%s", f.name);
        g.platform = f.platform;
        g.hasPlayer = f.state == OFFER_HAS_PLAYER;
        g.scene = f.scene;
        g.status = f.kind == FOUND_READY ? ICOOP_FOUND_READY :
                   f.kind == FOUND_FILES ? ICOOP_FOUND_FILES :
                                           ICOOP_FOUND_VERSION;
        g.newer = f.newer;
        if (!f.newer)
        {
            U8 id[kBuildIdSize];
            memset(id, 0, sizeof(id));
            memcpy(id, f.build, kIdPrefix);
            version_code(id, g.version);
            memcpy(id, f.files, kIdPrefix);
            version_code(id, g.files);
        }
    }
    return n;
}

const char* iCoopPlatformName(U8 platform)
{
    return disc_platform_name(platform);
}

U8 iCoopThisPlatform()
{
    return disc_this_platform();
}

bool iCoopLanVisible()
{
    return sLanVisible.load(std::memory_order_relaxed);
}

void iCoopSetLanVisible(bool on)
{
    sLanVisible.store(on, std::memory_order_relaxed);
}

void iCoopTestBlackout(F32 seconds)
{
    if (!(seconds > 0.0f) || seconds > 300.0f)
    {
        return;
    }
    sBlackoutUntil.store(NowNs() + (U64)(seconds * 1000.0f) * kNsPerMs, std::memory_order_relaxed);
}

void iCoopGoToHost()
{
    sGoToHost = true;
}

bool iCoopHudActive()
{
    if (!sRunning)
    {
        return false;
    }
    return true;
}

bool iCoopNameTag(F32* x, F32* y, bool* inFront)
{
    if (!sRunning || !sNameTag || !sTagValid)
    {
        return false;
    }
    *x = sTagX;
    *y = sTagY;
    *inFront = sTagInFront;
    return true;
}

bool iCoopNotice(S32 index, const char** text, F32* ageSec)
{
    std::lock_guard<std::mutex> hold(sLock);
    U64 now = NowNs();
    // Notices fade after a few seconds; the old ones are dropped here.
    while (sNoticeCount > 0 && now - sNoticeList[0].at > 6 * kNsPerSec)
    {
        memmove(sNoticeList, sNoticeList + 1, sizeof(Notice) * (kNotices - 1));
        sNoticeCount--;
    }
    if (index < 0 || index >= sNoticeCount)
    {
        return false;
    }
    *text = sNoticeList[index].text;
    *ageSec = (F32)((now - sNoticeList[index].at) / 1e9);
    return true;
}
