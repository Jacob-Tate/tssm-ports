#ifndef ICOOPLOCAL_H
#define ICOOPLOCAL_H

#include <types.h>

#include <stddef.h>

// PC-only: local co-op (docs/MULTIPLAYER.md 12). Two copies of the game on one
// PC: Player 1's, started with --local-coop (Local co-op.bat), hosts on
// 127.0.0.1 and starts Player 2's, which joins it by itself. Everything after
// the join is online co-op unchanged. Windows only, and off unless started
// that way: then every call here returns at once.
//
// Player 1's copy writes the session (the port and a one-time password) into
// a pipe only Player 2's copy inherits; Player 2's environment names its own
// profile folder (<save folder's parent>/local2: saves, log, player2.ini) and
// reads config.ini without ever writing it (iConfig.h). The pipe and the
// process handle tell each copy when the other is gone, and Player 2's game
// never outlives Player 1's (iHost.h).

enum iCoopLocalRole
{
    ICOOP_LOCAL_OFF,
    ICOOP_LOCAL_PLAYER1,
    ICOOP_LOCAL_PLAYER2,
};

iCoopLocalRole iCoopLocalGetRole();
bool iCoopLocalPlayer2();

// Whether this platform can run local co-op at all (Windows).
bool iCoopLocalAvailable();

// iSystemInit, once the log runs and before ApplyConfig: the role, from
// --local-coop[=side|stack] or TSSM_LOCAL_COOP=1 (Player 1) or the
// environment Player 1 gave (Player 2). Player 2 takes its pipe and waits at
// most 10 s for the session's line; without it, it closes.
void iCoopLocalInit();

// ApplyConfig, after [game] boot is read: Player 2 starts in Player 1's scene,
// without the logo movies.
void iCoopLocalApplyBoot();

// iCoopInit, after the settings: Player 1 hosts on 127.0.0.1, port 0; Player 2
// joins it. [coop] mode, bind, address and port are not used.
void iCoopLocalStartSession();

// Every frame (iCoopGameBeat): Player 1 starts Player 2's copy once it is in
// a level and has drawn a frame, then watches it; Player 2 reads the pipe.
void iCoopLocalPoll();

// Game side (iCoopWorldFrame), while Player 1 waits to start Player 2:
// this game is in `scene`, which a game can boot into when `known`.
bool iCoopLocalWantsLevel();
void iCoopLocalLevel(U32 scene, bool known);

// iCoop.cpp: hosting for local co-op, on 127.0.0.1 and a port the system
// picks (the status and the log say which).
bool iCoopHostLocal(const char* password, const char* name, char* why, size_t whySize);

// For the Co-op tab. `state` is constant text; `detail` may name a file.
struct iCoopLocalView
{
    iCoopLocalRole role;
    const char* state;
    char detail[400];
    bool canEnd; // Player 1: End local co-op (ends Player 2's game)
    bool canBringBack; // Player 1: Bring Player 2 back (hosts again, fresh)
    bool canPlayAlone; // Player 1: local co-op off; the online form again
    bool canLeave; // Player 2: Leave local co-op (closes this game)
};
void iCoopLocalGetView(iCoopLocalView* out);
void iCoopLocalEnd();
void iCoopLocalBringBack();
void iCoopLocalPlayAlone();
void iCoopLocalLeave();

// iCoopShutdown (this game's exit, atexit included): Player 1 closes the pipe
// and gives Player 2's game up to 2 s to close by itself; whatever is left
// ends with this process (iHost.h).
void iCoopLocalShutdown();

#endif
