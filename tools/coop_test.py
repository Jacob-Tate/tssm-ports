#!/usr/bin/env python3
"""Online co-op, two copies of the game on one PC (docs/MULTIPLAYER.md, 4.7 and 5.9).

    python tools/coop_test.py --assets DIR --exe build-x/tssm.exe --out DIR
                              [--scene BB02|DE01|TT01|PT01]
                              [--variant normal|netsim|wrong-password|wrong-version|
                                         wrong-files|no-answer|kick|blackout|follow|stay|
                                         stall-host|stall-guest|drag-host|drag-guest|
                                         combat|respawn|host-death|bowl|
                                         host-paused|levels|look|persist|save|
                                         own-show|replay-show|look-flood|title|tab|lan|
                                         far-area|both-hit|drive|arena|boxes|boxes-resync|
                                         breakables|zap|tokens|maintoken|given|upgrade|
                                         own-level|maintoken-death|keep-reset|arena-waves|
                                         maintoken-stop]
                              [--fight combat|respawn|host-death|bowl|host-paused|levels|
                                       far-area|both-hit|drive|arena|boxes|boxes-resync|
                                       breakables|zap|tokens|maintoken|given|upgrade|
                                       own-level|maintoken-death|keep-reset|arena-waves|
                                       maintoken-stop]
                              [--stall SECONDS] [--stall-frame N] [--hitch [FRAME:]SEC]
                              [--guest-exe build-x86/tssm.exe] [--all] [--stall-all]
                              [--drag-all]

What it does:

  1. starts two copies of our own tssm.exe at once, each with its own folder
     (config.ini, save folder, log, captures) under --out. Both boot straight
     into the scene through the same boot mod regress uses (sb04.ini with
     BOOT=<scene> and ShowMenuOnBoot=0, TSSM_MOD). The host hosts on 127.0.0.1
     only (TSSM_COOP=host, TSSM_COOP_BIND=loopback); the guest joins it. Both
     get a password made for this run and log their own and the partner's
     state every 10 frames (TSSM_COOP_LOG=10).
  2. closes the level's opening talk (Mindy) and task screen with B, then
     puts both players where each sees the other and the level's enemies
     (TSSM_TEST_PLACE, with the camera put behind them). In DE01 the guest
     stands by a robot and the host far off, so the robot goes for the guest;
     later the host comes over, so both are in each other's view. In TT01 the
     guest stands by the spinning robots and the tikis, the host a few steps
     off. In PT01 each stands on one of the two pillars that slide back and
     forth over the lava (TSSM_TEST_PLACE_ON): the guest rides one the host
     owns.
  3. captures frames on both at the same moments of the host's clock
     (TSSM_COOP_SYNC_CAPTURE: frames/sync_<host ms>.png on both copies), the
     window with the HUD layer and the Co-op tab (the host stays 200 frames
     longer, so the guest's tab shows the session up), and lets each exit by
     itself (TSSM_EXIT_AFTER).
  4. checks the logs:
     - both connected; the guest switched to Patrick; each draws the other;
       every partner state a copy logged is the other's own logged state for
       that frame number (M1);
     - both built the same table of shared things, the guest showed the
       host's world only once it had every thing, and snapshots arrived and
       were applied;
     - at every synchronised capture, each enemy and platform near the players
       is within 0.25 units on both copies;
     - every position the guest drew (`world shown`, 100 ms behind the host)
       is within 0.25 units of where the host had it at that moment
       (interpolated between its 30-a-second `world sent` lines);
     - nothing on the guest moved a host-owned thing after it was posed
       (`pose check` lines);
     - DE01 and TT01: an enemy targets the guest and closes in on it (any one of
       them; one that hits the guest counts);
     - PT01: the guest stays on the host's moving pillar and goes with it;
     - the host's traffic stays under the 36 KB/s ceiling;
     - no snapshot refused for a level mismatch, no unknown animation state, no
       protocol error, no timeout; both exited at their exit frame.

Variants:
  --variant netsim          TSSM_COOP_NETSIM=80,20,5 (80 ms delay, 20 ms
                            jitter, 5% loss each way): the same checks, and the
                            guest draws the host's world at most 300 ms back.
  --variant wrong-password  the guest's password is wrong: both logs show the
                            refusal, and no partner, puppet or snapshot appears.
  --variant stay            as follow, with following off on the guest
                            (TSSM_COOP_FOLLOW=0): the guest stays in BB02 and
                            its Co-op tab offers "Go to the host's level"
                            (captured, then closed: TSSM_GUI_CLOSE); a press of
                            it (TSSM_TEST_COOP_GO) takes the guest to the host,
                            beside it.
  --variant follow          both boot into BB02; after the opening the host
                            goes to the scene (DE01 when the scene is BB02)
                            through TSSM_TEST_SCENE, and the guest must follow
                            by itself ([coop] follow), be put beside the host
                            after its own opening of the level, and stay apart
                            from it. Both games then ask for an autosave
                            (TSSM_TEST_AUTOSAVE): the host's saves, the
                            guest's must not; the host leaves, and the guest's
                            game asks again in the level it followed the host
                            to, and must not save there either.
  --variant stall-host      the host's game thread stops for --stall seconds
  --variant stall-guest     (5 by default; TSSM_TEST_STALL) once both players
                            are in view, as when its window is held or dragged,
                            while its co-op I/O thread goes on. The session must
                            stay up (no disconnect, no second join), the other
                            game must hear the stall (STALL) and say it waits
                            (from 2 s on), the guest keeps posing the host's
                            things through it, and within about a second after
                            it both games show the same things in the same
                            places again (same-moment captures every 250 ms,
                            each thing within 0.25 units), with every death
                            effect, character switch and arrival once only.
                            The other game's HUD is captured during a stall of
                            3 s or more ("waiting for ...'s game"). A stall of
                            more than 60 s is past the session's limit: then the
                            stalled game must close the session (DC_STALLED),
                            both must say why, and neither may time out.
  --variant drag-host       as stall-host / stall-guest, but the game is
  --variant drag-guest      stopped by Windows' own window move loop, as when a
                            player holds the window's title bar: the script
                            posts the window a "Move" (WM_SYSCOMMAND SC_MOVE,
                            the window menu's Move) and, --stall seconds later,
                            an Escape, which ends the move and puts the window
                            back. Windows puts the mouse pointer on the window
                            for that long (the script puts it back after), so
                            do not use the mouse meanwhile.
  --all                     every scene and variant in turn, with a summary.
  --stall-all               the stall variants of --scene (DE01 by default):
                            host and guest, 1, 5 and 20 s each, then each past
                            the one-minute limit (65 s).
  --drag-all                the drag variants of --scene (DE01 by default):
                            host and guest, 1, 5 and 20 s each (the mouse
                            pointer is taken meanwhile).

M3's fights (docs/MULTIPLAYER.md 6.7), each with its own placing and scripts:
  --variant combat          DE01: the robot hits the guest; the guest, kept a
                            step from it (TSSM_TEST_FACE), spins at it until it
                            is defeated; the shiny objects there are taken.
                            Checks: each hit on the guest sent once and taken
                            once, the guest's claims applied on the host, the
                            robot's death once on both, the pickups counted on
                            the host once and played on the guest once.
  --variant respawn         DE01: the guest, at one health (TSSM_TEST_HEALTH),
                            is defeated by the robot and comes back beside the
                            host with full health; nothing is reset.
  --variant host-death      DE01: the host, at one health, is defeated; its
                            game keeps its own rules, and the guest is put
                            beside it once it plays again.
  --variant bowl            TT01 (the guest is SpongeBob there): the guest bowls
                            at a spinning robot from 7 units; the host applies
                            the SpongeBowl's claims.
  --variant host-paused     DE01: the combat fight with the host's game paused
                            (its pause menu) while the guest's first spins
                            land: the host refuses those claims (no enemy is
                            hurt while its game is paused), then applies the
                            later ones once it plays again.
  --fight NAME              runs the stall, drag or netsim variant over that
                            fight (--stall-frame N puts the stall in it).

Protocol 7's fixes (docs/MULTIPLAYER.md 13), each a fight of its own:
  --variant far-area        DE01: the host stays at the start, the guest spins
                            at a crate in another part of the level (culled on
                            the host's game): its hits are applied.
  --variant both-hit        DE01, 40 ms of lag: both players hit the robot (2
                            health) within its half-second recovery: the
                            guest's hit counts too, and the robot falls.
  --variant drive           BB01: the guest drives its Patty Wagon into a pile
                            of crates: the car's hits (attack 9) break them.
  --variant arena           AM01, the first combat arena: the guest spins at an
                            enemy the arena's spawner brought back (its hits
                            are applied), an enemy's fog breath hurts the
                            guest, and LB says at once that the arena has one
                            character only.
  --variant boxes           BB02: the guest (Patrick) breaks a stack of crates;
                            the one that falls is hit and breaks.
  --variant boxes-resync    BB02: the guest breaks crates while its connection
                            is cut (its own world); once the host's world is
                            back, the same crates (whole on the host) are hit
                            and break.
  --variant breakables      TT01: the guest breaks barrels (breakables): the
                            host breaks them too.
  --variant zap             BB02: the guest's upgraded SpongeBowl: its
                            lightning (attack 10) is applied.
  --variant tokens          DE01, both with 3 tokens: a token only the guest's
                            game showed is counted for both; the count and the
                            moves are the host's on both; at Mindy (5 tokens)
                            both get the Bash; after the session the guest's
                            own progress is back and its save folder empty.
  --variant maintoken       DE01: the guest takes the level's main token; the
                            host's level ends and both go to DE02.
  --variant given           BB01: the guest's own script gives it a token: the
                            host counts it.
  --variant upgrade         DE01, both with an upgrade point: the guest buys an
                            upgrade in its pause menu (TSSM_TEST_UPGRADE): the
                            host's save gets it, the guest plays its film. A
                            second purchase with the menu closed is refused.
  --variant own-level       BB02, following off: the host goes to DE01; the
                            guest stays, its own progress back once the host
                            has gone, breaks crates there and takes their
                            shiny objects; after the session its autosave has
                            them.
  --variant maintoken-death DE01: as maintoken, but the host is defeated while
                            the guest's reward plays: once the host plays
                            again its level still ends, and the guest's screen
                            is not left dark meanwhile.
  --variant maintoken-stop  DE01: as maintoken, but the host stops hosting
                            (the Co-op tab's Stop) while the guest's reward
                            plays: the token's level signal still fires on
                            the host, with co-op off.
  --variant keep-reset      DE01: the guest's pickups go to the host's save,
                            then the guest's level is reset (a lost chance,
                            TSSM_TEST_RESET, as an out-of-bounds fall), which
                            writes its save block; the host leaves for DE02
                            and the guest follows: its block for DE01 is put
                            back as its own game had it.
  --variant arena-waves     AM01: the guest's kills count down "N left"; the
                            host's level logic is made to end each wave
                            (TSSM_TEST_SEND: its count to 1, then a
                            decrement), and the arena ends after wave 3. Both
                            copies' counters and challenge texts ("Wave 1
                            Complete!", the countdown, "You Win!") change at
                            the same moments; the guest's own decrement of a
                            wave's counter is held (its level runs no wave of
                            its own).

M4's playing through together (docs/MULTIPLAYER.md 7.8):
  --variant levels          both boot into BB01, a Patty Wagon level (each
                            draws the other's wagon); the host takes the
                            level's own exit (TSSM_TEST_EVENT, the portal's own
                            event) and the guest goes along at once; both start
                            DE01 together (within 300 ms) and its fly-through
                            plays on both; the guest's skip does nothing, the
                            host's ends it on both. Costumes (every one offered
                            with TSSM_TEST_COSTUMES=all, chosen with
                            TSSM_TEST_COOP_LOOK): each loads the other's with
                            DE01 and draws it; the guest's LB makes two
                            SpongeBobs in different costumes, the host's makes
                            it Patrick. A cutscene the host starts plays on
                            both; the guest's skip does nothing, the host's
                            ends it on both. The host then leaves for DE02 (a
                            slide: each draws the other's sled) while the
                            level's cutscene plays again: the guest's copy ends
                            and it goes along. The guest's save folder stays
                            empty. With --variant stall-guest --fight levels
                            --stall-frame 1290 the guest stalls across the level
                            change (the host waits for it, then both start
                            together); with stall-host --stall-frame 3150 the
                            host stalls during the cutscene, with stall-guest
                            --stall-frame 3060 the guest does (the host ends
                            the cutscene meanwhile; the guest's ends when it
                            runs again).
  --variant look            BB02, facing each other: the guest takes Patrick by
                            itself, then each taps LB in turn: two SpongeBobs,
                            then each the other's character, then two
                            Patricks; each choice is kept in config.ini. Then
                            the guest holds LB with no costume unlocked:
                            nothing changes or is written.
  --variant persist         BB02 from config.ini files an earlier session left
                            (the host: character patrick, costumes Disco and
                            Mermaid Man; the guest: SpongeBob as Caveman,
                            character automatic): the host takes Patrick, the
                            guest stays SpongeBob by the host's look; each holds
                            LB and the next costume is kept in config.ini.
  --variant save            BB02: both save by hand (TSSM_TEST_SAVE): the
                            host's is written, the guest's save screen closes at
                            once and its save folder stays empty.
  --variant own-show        DE01: the guest's game starts a cutscene by itself;
                            the host sends nothing and the guest's skip is
                            taken.
  --variant replay-show     DE01: the host's cutscene plays on both and the
                            host skips it; later the guest's own game starts
                            it again: that replay is the guest's to skip.
  --variant look-flood      as persist, with the guest stopped 15 s while the
                            host holds LB 12 times: the guest gets the looks
                            all at once and must end on the host's newest.
  --variant title           BB02: the guest leaves for the title menu
                            (TSSM_TEST_QUIT) right after drawing the host; the
                            partner's model goes with the level and the title
                            draws none of it; the guest's title runs on to its
                            exit.

M5's join flow and dropped connections (docs/MULTIPLAYER.md 8.8):
  --variant wrong-version   the guest's build ID differs (TSSM_TEST_COOP_BUILD_SALT):
                            refused as "a different version".
  --variant wrong-files     the guest's game-files hash differs
                            (TSSM_TEST_COOP_ASSET_SALT): refused as "game files
                            differ".
  --variant no-answer       the guest joins a port nobody hosts on: "no answer
                            from the host", with what to check.
                            (These and wrong-password: the guest's Co-op tab is
                            captured with the reason; no partner, no world.)
  --variant tab             BB02: both start with co-op off and use the Co-op
                            tab's own code (TSSM_TEST_COOP_TAB): the host types
                            a name, port and password and presses Host; the
                            guest types a name, the address and a wrong
                            password and presses Join, is refused, types the
                            right one and presses Try again, and joins. Then
                            each switches its options off. Checks: the refusal
                            and the join, the same co-op version on both, and
                            config.ini on each: the name, port (host) and
                            address (guest), and the switches, never a
                            password.
  --variant lan             BB02, the home network's list and the Co-op tab
                            (docs/MULTIPLAYER.md 14.2, 14.3) through the loopback
                            test hooks only (TSSM_COOP_LAN_PORT,
                            TSSM_COOP_LAN_TARGET=127.0.0.1), so nothing
                            broadcasts and the firewall is not asked: both start
                            with co-op off; the host presses "Host on this
                            network" (its code fixed by TSSM_TEST_COOP_CODE, so
                            the test can type it; its tab and the HUD's corner
                            code captured); the guest's tab searches by itself
                            when it opens, lists the host, and a virtual
                            controller moves to the row, picks it with A and
                            opens the code entry with Start (its pad opens by
                            itself); it types a wrong code (the d-pad, A and B
                            for the first keys, the pad's test action for the
                            rest) and is refused, goes back, closes the
                            overlay (the search ends) and opens it (the tab
                            searches again), opens the code entry by the test
                            actions, the search is stopped (as when its minute
                            ends), and the right code joins with Start. Then a prober from other 127.x
                            addresses checks the host's answers: one good
                            answer, smaller than the question, saying "has a
                            player"; silence for every malformed question and
                            for a source port below 1024; a flood from one
                            address held to 4 at once and 2 a second. Also: the
                            address reaches the guest's config.ini only after
                            its session is up, never after the refused join; no
                            code in a config.ini or a log. The normal run's
                            checks follow.
  --variant lan-version     the guest's build ID differs: its list says
                            "Different version" with both codes (the row
                            picked by a tap), and joining anyway is refused as
                            a different version.
  --variant lan-files       the guest's game files differ: "Different game
                            files" with both game-files codes; refused.
  --variant lan-full        a host and a guest in a session, and a third copy
                            whose list says "has a player" (and "Full"; the row
                            picked by a mouse click); its join is refused as
                            full; the session goes on.
  --variant lan-none        nothing to find: the guest's list stays empty and
                            shows the checklist after 5 s (captured); the host's
                            discovery port is held by another program (this
                            test), so its tab says others can't see it; the
                            guest then joins by address.
  --variant lan-hidden      "Host over the internet": not visible, so the
                            guest's list is empty; ticking "Visible on this
                            network" lists it, unticking takes it off the list
                            again, and the guest joins by address; the host's
                            screen shows the join code only while visible.
  --variant lan-retry       the row picked by a tap, the code entry opened with
                            its field holding the keyboard and a wrong code
                            typed frames later: that code is the one refused;
                            Try again opens the entry with the code pad, which
                            stays all on screen while the list fills in again;
                            the right code joins. With --phone, at a phone's
                            layout.
  --variant kick            BB02: the host kicks the guest (TSSM_TEST_COOP_KICK)
                            once both are in view: the guest is told "the host
                            removed you", does not join again by itself, and
                            its tab says so.
  --variant blackout        DE01 by default: the guest's connection is cut for
                            --stall seconds (TSSM_TEST_BLACKOUT: its I/O thread
                            drops everything in and out, as a pulled cable).
                            Under 10 s the session stays up: each side logs the
                            interruption and the host's HUD says "no word from
                            Guest" (captured). From 10 s on both time out; the
                            guest's game joins again by itself once the cable is
                            back (its HUD, captured, says it is joining again),
                            stays in the same level and is not put anywhere,
                            and both show the same world again; autosaves asked
                            while it joins again write nothing. Same-moment
                            captures four times a second, compared outside the
                            cut and the time after it (1 s; 3 s after a join
                            again). --hitch [FRAME:]SEC also stops the guest's
                            own game thread for SEC seconds at FRAME (250
                            frames into the cut by default) while the cable is
                            out: the host's world, gone quiet, must not come
                            back on the guest after it. --blackout-all: 5 and
                            15 s, and 15 s with a 1.3 s hitch.

Local co-op (docs/MULTIPLAYER.md 12): the script starts only Player 1's copy
(--local-coop), which hosts on 127.0.0.1 at a port the system picks and starts
Player 2's copy itself, in <run>/host/local2 (its saves, its log, player2.ini).
Player 2 is scripted through TSSM_CHILD_* (passed on renamed). Each run also
watches for a Windows Firewall prompt (any new window titled like one; none is
expected, every socket is bound to 127.0.0.1) and never touches one.
  --variant local           BB02 or DE01: Player 2 started once and joined
                            within 10 s of its start with nothing typed,
                            on a port that is not 2171, put beside Player 1;
                            two logs; the run's config.ini, put back missing
                            settings before Player 2 starts, unchanged (bytes
                            and time); player2.ini only Player 2's keys; no
                            save in Player 2's folder; Player 2's mod log in
                            its own folder; the M1/M2 checks.
  --variant local-close-guest / local-close-host
                            the script closes one copy's window (WM_CLOSE):
                            Player 1 says "Player 2 left" within 2 s and plays
                            on; or Player 2's game closes by itself within 2 s
                            (the pipe's end), before Player 1's ends.
  --variant local-kill-guest / local-kill-host
                            the script ends one copy (our own processes only):
                            Player 1 reports the crash within 2 s; or Player
                            2's game ends with Player 1's within 2 s.
  --variant local-close-host-hung
                            Player 2's game stops 60 s (TSSM_TEST_STALL), then
                            the script closes Player 1's window: Player 1 waits
                            2 s for it, and Player 2's game ends with Player
                            1's (the job), so nothing is left running.
  --variant local-hang-guest
                            Player 2's game stops 65 s (TSSM_TEST_STALL): Player
                            1 waits, the session closes at a minute, Player 1
                            says End would end it, and Player 2's game, running
                            again, closes by itself.
  --variant local-end       End local co-op (the tab's button): Player 2's game
                            closes by itself; then Bring Player 2 back: a new
                            port, a new Player 2 that joins.
  --variant local-end-hung  End local co-op while Player 2's game is stopped
                            (100 s): ended after 5 s.
  --variant local-bringback five wrong passwords from a test client on
                            127.0.0.1 (tssm_nettest --wrong-proofs, beside
                            --exe) block that address before Player 2 joins, so
                            Player 2 is turned away; Bring Player 2 back hosts
                            again (a new port and password) and the new Player
                            2 joins from 127.0.0.1.
  --local-all               local in BB02 and DE01, then every lifetime variant.

Each copy is our own build and its own process; the script stops only those
two, and only after --timeout. It never touches anyone's saves: each copy gets
an empty save folder of its own. Look at the frames: sync_<ms>.png of the same
name on both copies show the same moment, so the same enemies must stand in
the same places (as the logs check), and each copy shows the other player.

Only the Python standard library is used.
"""

import argparse
import bisect
import math
import os
import re
import secrets
import subprocess
import sys
import threading
import time

LOG_EVERY = 10
GUI_AFTER = 30

# Frame numbers are frames presented (TSSM_INPUT counts them). Per scene:
# when the opening is over, where the two players go, and when to stop.
# Places are "frame:x:y:z:yaw" (TSSM_TEST_PLACE; yaw 0 faces +z, 90 faces +x).
# "b/1" is one frame of B: it closes Mindy's talk, then the level's task
# screen (the talk's QUIT, the screen's RETURN TO GAME).
SCENES = {
    # Bikini Bottom: the story movie and the opening cutscene end near frame
    # 900. Both players on the road by the tikis south-east of the start, the
    # host nearer them, both facing them (the guest sees the host and the
    # tikis); at 1600 the host turns round to face the guest.
    "BB02": {
        "exit": 2100,
        # Local co-op: Player 1 is in the level only after the story movie
        # (frame ~650), so Player 2 runs about 1000 frames behind.
        "local_late": 1200,
        "host_script": "620:a,900:a",
        "guest_script": "620:a,900:a",
        "host_place": "1000:24.3:1.5:-30.5:180,1600:24.3:1.5:-30.5:0",
        "guest_place": "1000:24.0:1.5:-22.0:180",
    },
    # Jellyfish Fields: the level's camera fly-through ends near frame 760,
    # Mindy's talk and the task screen close at 900 and 1000. The guest by the
    # robot (a slammer, thing 126 at 11.4 6.0 65.2) and the host at the start,
    # 60 units away: the robot goes for the guest. At 1600 the host comes
    # over, facing the guest, and both are in view; at 2050 the host defeats
    # the nearest enemy (TSSM_TEST_KILL, the game's own damage), and its death
    # effect must play on the guest too.
    "DE01": {
        "exit": 2500,
        "host_script": "10:a,900:b/1,1000:b/1",
        "guest_script": "10:a,900:b/1,1000:b/1",
        "host_place": "1600:9.5:7.0:61.0:15",
        "host_extra": {"TSSM_TEST_KILL": "2050:30"},
        "guest_place": "1060:11.0:7.0:71.0:180",
        "chase": True,
    },
    # Thugtown (a lot of shared things: 538). Mindy's talk and the task screen
    # close at 950 and 1030; the guest on the floor by the spinning robots
    # south of the start (things 388 and 389), facing the host; the host by
    # the tikis at the start, 9 units off, facing the guest and the robots.
    "TT01": {
        "exit": 2100,
        "host_script": "10:a,950:b/1,1030:b/1",
        "guest_script": "10:a,950:b/1,1030:b/1",
        "host_place": "1100:12.0:12.8:61.0:180",
        "guest_place": "1100:12.0:11.0:52.0:0",
        "chase": True,
    },
    # Planktopolis: two pillars slide back and forth over the lava by
    # themselves (platforms 9fa0e488 and 9fa0e489). After the talk and the
    # task screen each player is put on one, facing the other: the guest on
    # 9fa0e488 rides a platform the host owns.
    # Bikini Bottom's Patty Wagon level (one character: both drive one). B
    # every 50 frames closes the opening's talk and task screen.
    "BB01": {
        "exit": 1600,
        "host_script": "10:a," + ",".join("%d:b/1" % f for f in range(300, 1101, 50)),
        "guest_script": "10:a," + ",".join("%d:b/1" % f for f in range(300, 1101, 50)),
    },
    # The first combat arena (from BB03's challenge): SpongeBob only; its
    # opening talk closes by 550.
    "AM01": {
        "exit": 2600,
        "host_script": "10:a," + ",".join("%d:b/1" % f for f in range(200, 551, 50)),
        "guest_script": "10:a," + ",".join("%d:b/1" % f for f in range(200, 551, 50)),
    },
    "PT01": {
        "exit": 2300,
        "host_script": "10:a,1050:b/1,1120:b/1",
        "guest_script": "10:a,1050:b/1,1120:b/1",
        "host_extra": {"TSSM_TEST_PLACE_ON": "1200:9fa0e489:0.5:270"},
        "guest_extra": {"TSSM_TEST_PLACE_ON": "1200:9fa0e488:0.5:90"},
        "ride": "9fa0e488",
    },
}

SYNC_EVERY_MS = 1000
SYNC_COUNT = 40
# Stall variants: the stall starts at this frame (both players in view in
# every scene by then) and the captures come four times a second.
STALL_FRAME = 1700
STALL_SYNC_MS = 250
FPS = 53  # about what two copies at once run at; frame counts are planned with it
# What one copy runs at while the other is stalled (it has the PC to itself):
# a stalled guest's host gets this many frames a second during the stall.
FPS_ALONE = 62

STATE = re.compile(r"tssm: coop: (self|partner) frame (\d+) scene (\S+) char (\d+) pos (\S+) (\S+) (\S+) "
                   r"yaw (\d+) anim ([0-9a-f]+) t (\S+) flags (\d+)")
SYNC = re.compile(r"tssm: coop: sync (\d+) thing (\d+) (enemy|mover) pos (\S+) (\S+) (\S+)")
SYNC_SELF = re.compile(r"tssm: coop: sync (\d+) at host ms (\d+) self (\S+) (\S+) (\S+)")
SENT = re.compile(r"tssm: coop: world sent ms (\d+) thing (\d+) pos (\S+) (\S+) (\S+)(?: anim \S+ flags (\d+))?")
# The flags (iNetProto.h's WF_*) whose change between two of the host's samples
# makes the guest put a thing at the newer place rather than slide it there: a
# death (WF_DEATHS), a break, shown or hidden (iCoopWorld.cpp's InterpPose).
WF_STATE = 0x01 | 0x04 | 0x30
SHOWN = re.compile(r"tssm: coop: world shown ms (\d+) thing (\d+) pos (\S+) (\S+) (\S+)")
TABLE = re.compile(r"tssm: coop: level (\w+) shares (\d+) things .* table ([0-9a-f]+)")
TRAFFIC = re.compile(r"tssm: coop: traffic out (\S+) KB/s in (\S+) KB/s")
TARGET = re.compile(r"tssm: coop: enemy thing (\d+) targets the guest")
LAG = re.compile(r"tssm: coop: world lag (-?\d+) ms")
STALL_LIMIT_S = 60  # iCoop.cpp's kStallLimit: a longer stall ends the session
STALL_FROM = re.compile(r"tssm: test: frame \d+: the game thread stops for (\S+) s \(TSSM_TEST_STALL\) at clock ms (\d+)")
STALL_TO = re.compile(r"tssm: test: frame \d+: the game thread runs again at clock ms (\d+)")
# M5: a cut connection (TSSM_TEST_BLACKOUT) and the guest's join again.
BLACKOUT_AT = re.compile(r"co-op's connection is cut for (\S+) s \(TSSM_TEST_BLACKOUT\) at clock ms (\d+)")
REJOINED = re.compile(r"coop: reconnected (\d+) ms after the session was lost \(clock ms (\d+)\)")
QUALITY = re.compile(r"tssm: coop: traffic out .*; ping (-?\d+) ms, lost in (-?\d+)% out (-?\d+)%, quality (\d+)")
SESSION_TIMEOUT_S = 10  # iNetSession.h's kSessionTimeout
TAB_HOST, TAB_JOIN, TAB_AGAIN, TAB_OPTIONS = 300, 450, 750, 1500  # the tab variant's frames
LAN_PORT_OFFSET = 100  # every run's discovery port (loopback only): the game port plus this, never 2172
# The home network's short variants: no shared play to check, only the tab.
LAN_SHORT = ("lan-version", "lan-files", "lan-full", "lan-none", "lan-hidden", "lan-retry")
LAN_VARIANTS = ("lan",) + LAN_SHORT
LAN_NAV = "down.down"  # the virtual controller's way from the tab's first stop to the list's row
CROCKFORD = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
# --phone WxH:SCALE: every copy's window and [gui] scale, with a touch screen's
# sizes and words (TSSM_GUI_TOUCH=1, on builds that have it); None: 640x480.
PHONE = None


def join_code():
    """A random join code of the game's own form (12 of the 32 symbols)."""
    return "".join(CROCKFORD[b & 31] for b in os.urandom(12))
SNAP_SPEED = 35.0  # iCoopWorld.cpp kSnapSpeed: a faster move between two samples is not slid
# The refusal variants: the guest's own environment, and what its log must say.
REFUSALS = {
    "wrong-password": ({}, "coop: the join was refused: wrong join code or password"),
    "wrong-version": ({"TSSM_TEST_COOP_BUILD_SALT": "1"},
                      "coop: the join was refused: the host's game is a different version"),
    "wrong-files": ({"TSSM_TEST_COOP_ASSET_SALT": "1"}, "coop: the join was refused: your game files differ"),
    "no-answer": ({}, "coop: no answer from the host"),
}


def b_every(first, last):
    """A press of B every 50 frames from `first` to `last` (TSSM_INPUT)."""
    return ",".join("%d:b/1" % f for f in range(first, last + 1, 50))


def boot_mod(assets, run_dir, scene, name="mod_boot"):
    """A mod folder (TSSM_MOD) whose sb04.ini is the game's with BOOT=<scene>
    and ShowMenuOnBoot=0 (as tools/regress.py makes it)."""
    lines = open(os.path.join(assets, "sb04.ini"), "rb").read().decode("latin-1").splitlines()
    wanted = {"BOOT": "BOOT = %s" % scene, "ShowMenuOnBoot": "ShowMenuOnBoot = 0"}
    out, done = [], set()
    for line in lines:
        key = line.split("=", 1)[0].strip()
        if "=" in line and key in wanted:
            if key in done:
                continue
            done.add(key)
            line = wanted[key]
        out.append(line)
    out[1:1] = [v for k, v in wanted.items() if k not in done]
    mod = os.path.join(run_dir, name)
    os.makedirs(mod, exist_ok=True)
    with open(os.path.join(mod, "sb04.ini"), "wb") as f:
        f.write(("\r\n".join(out) + "\r\n").encode("latin-1"))
    return mod


def env_for(role, run_dir, assets, mod, port, password, script, extra, timing, place):
    """The environment of one copy; `timing` has its exit frame, the frames to
    capture, and when the HUD layer and the Co-op tab are written. `extra` is
    added to it, except its "CONFIG" (text added to the copy's config.ini, as
    a player's earlier session would have left it). The parent's TSSM_TEST_*
    and TSSM_COOP* variables are not passed on: a run's own come from
    `extra`."""
    extra = dict(extra)
    seed = extra.pop("CONFIG", "")
    os.makedirs(os.path.join(run_dir, "frames"), exist_ok=True)
    os.makedirs(os.path.join(run_dir, "save"), exist_ok=True)
    if PHONE:
        video = "[video]\nmode = windowed\naspect = custom\nwidth = %d\nheight = %d\nbackend = d3d9\n[gui]\nscale = %s\n" % PHONE
    else:
        video = "[video]\nmode = windowed\naspect = 4:3\nwidth = 640\nheight = 480\nbackend = d3d9\n"
    with open(os.path.join(run_dir, "config.ini"), "w", newline="\r\n") as f:
        f.write(video + "[game]\nintro_movies = off\n" + seed)
    env = dict(os.environ)
    for k in [k for k in env if k.upper().startswith(("TSSM_TEST_", "TSSM_COOP"))] + [
            "TSSM_WIDEN", "TSSM_GUI_SELFTEST", "TSSM_GUI_OPEN_AT", "TSSM_INPUT_TEST", "TSSM_MOD",
            "TSSM_MODS"]:
        env.pop(k, None)
    env.update({
        "TSSM_ASSETS": assets,
        "TSSM_CONFIG": os.path.join(run_dir, "config.ini"),
        "TSSM_SAVE_DIR": os.path.join(run_dir, "save"),
        "TSSM_MODS_DIR": os.path.join(run_dir, "mods-none"),
        "TSSM_MOD": mod,
        "TSSM_CAPTURE_DIR": os.path.join(run_dir, "frames"),
        "TSSM_CAPTURE_FRAMES": ",".join(str(n) for n in timing["frames"]),
        "TSSM_EXIT_AFTER": str(timing["exit"]),
        "TSSM_INPUT": script,
        "TSSM_LOG": os.path.join(run_dir, "tssm.log"),
        "TSSM_AUDIO": "0",
        "TSSM_COOP": role,
        "TSSM_COOP_PORT": str(port),
        "TSSM_COOP_BIND": "loopback",
        "TSSM_COOP_PASSWORD": password,
        "TSSM_COOP_NAME": "Host" if role == "host" else "Guest",
        "TSSM_COOP_LOG": str(LOG_EVERY),
        "TSSM_COOP_SYNC_CAPTURE": "%d:%d" % (SYNC_EVERY_MS, SYNC_COUNT),  # stall variants: 250 ms
        "TSSM_GUI_OPEN_AT": str(timing["gui"]),
        "TSSM_GUI_TAB": "coop",
        "TSSM_GUI_CAPTURE": os.path.join(run_dir, "frames", "coop_tab.png"),
        "TSSM_GUI_CAPTURE_FRAMES": str(GUI_AFTER),
        "TSSM_GUI_HUD_CAPTURE": "%d:%s" % (timing["hud"], os.path.join(run_dir, "frames", "hud.png")),
        # Placed players get the camera put behind them.
        "TSSM_TEST_PLACE_CAMERA": "reset",
    })
    if PHONE:
        env["TSSM_GUI_TOUCH"] = "1"
    if port >= 1024:
        # Any search or visible host of any run stays on 127.0.0.1 (no socket
        # the network or the firewall sees).
        env["TSSM_COOP_LAN_PORT"] = str(port + LAN_PORT_OFFSET)
    if place:
        env["TSSM_TEST_PLACE"] = place
    if role == "join":
        env["TSSM_COOP_ADDR"] = "127.0.0.1:%d" % port
    env.update(extra)
    return env


def states(log, kind):
    """frame -> (pos, anim) of the log's `kind` ("self" or "partner") lines."""
    out = {}
    for m in STATE.finditer(log):
        if m.group(1) != kind:
            continue
        out[int(m.group(2))] = ((float(m.group(5)), float(m.group(6)), float(m.group(7))), m.group(9))
    return out


def compare(viewer, sender):
    """(matched, problems): the viewer's partner lines against the sender's own."""
    seen = states(viewer, "partner")
    sent = states(sender, "self")
    problems = []
    matched = 0
    for frame, (pos, anim) in sorted(seen.items()):
        if frame not in sent:
            problems.append("frame %d: shown but never logged as sent" % frame)
            continue
        spos, sanim = sent[frame]
        d = max(abs(a - b) for a, b in zip(pos, spos))
        if d > 0.05 or anim != sanim:
            problems.append("frame %d: shown %s %s, sent %s %s" % (frame, pos, anim, spos, sanim))
        else:
            matched += 1
    return matched, problems


def syncs(log):
    """sync id -> {thing: pos}, sync id -> the copy's own position, and sync id
    -> the host time the frame was really for (a frame's length either side)."""
    things, selves, at = {}, {}, {}
    for m in SYNC.finditer(log):
        things.setdefault(int(m.group(1)), {})[int(m.group(2))] = tuple(float(m.group(i)) for i in (4, 5, 6))
    for m in SYNC_SELF.finditer(log):
        selves[int(m.group(1))] = tuple(float(m.group(i)) for i in (3, 4, 5))
        at[int(m.group(1))] = int(m.group(2))
    return things, selves, at


def compare_syncs(host, guest, skip=None, per_moment=None):
    """(moments, pairs, worst, worst_where) of the positions both logged at the same sync moments.
    Each copy's frame is for a moment within a frame of the sync time; a thing
    moving fast may be that much further on one, so its speed (from the host's
    own neighbouring moments) times the two frames' difference is allowed on top.
    `skip(sid)`: moments left out (a stall and the second after it); `per_moment`:
    a dict filled with each moment's worst difference. A thing that jumped (a
    defeated enemy put back at its spawn point) within a frame or two of the
    moment, or in the host's next sample after it (the guest does not slide
    across a jump, so until that sample it shows the one before), is left out
    of it."""
    jumps = teleports(host)
    h, _, hat = syncs(host)
    g, _, gat = syncs(guest)
    # The host's own speed of each thing at each moment (its sync line ends
    # with it): an average over the second around the moment misses a short
    # burst (an enemy knocked back by a hit).
    burst_at = {(int(m.group(1)), int(m.group(2))): float(m.group(3))
                for m in re.finditer(r"tssm: coop: sync (\d+) thing (\d+) \w+ pos \S+ \S+ \S+(?: hidden)? speed (\S+)",
                                     host)}

    def burst(t, sid):
        return burst_at.get((sid, t), 0.0)

    ids = sorted(h)
    moments = pairs = 0
    worst, where, raw = 0.0, "", 0.0
    for k, sid in enumerate(ids):
        if sid not in g:
            continue
        if per_moment is not None:
            per_moment.setdefault(sid, 0.0)
        common = set(h[sid]) & set(g[sid])
        if not common:
            continue
        if skip is None or not skip(sid):
            moments += 1
        dt = abs(hat.get(sid, sid) - gat.get(sid, sid)) / 1000.0
        for t in common:
            if any(-50 <= j - sid <= 120 for j in jumps.get(t, ())):
                continue
            if burst(t, sid) > SNAP_SPEED:
                # Faster than the game slides a thing (an enemy flung out of a
                # spawner): drawn at a sample's end, as across a jump.
                continue
            if skip is None or not skip(sid):
                pairs += 1
            # Its speed: over the nearest moments before and after at which the
            # host logged it too (within 3 s; it is not logged while far from
            # both players).
            speed = 0.0
            for step in (-1, 1):
                j = k + step
                while 0 <= j < len(ids) and abs(ids[j] - sid) <= 3000 and t not in h[ids[j]]:
                    j += step
                if 0 <= j < len(ids) and abs(ids[j] - sid) <= 3000:
                    gap = abs(ids[j] - sid) / 1000.0
                    speed = max(speed, math.dist(h[ids[j]][t], h[sid][t]) / gap)
            speed = max(speed, burst(t, sid))
            d = max(0.0, math.dist(h[sid][t], g[sid][t]) - speed * (dt + 0.017))
            if per_moment is not None:
                per_moment[sid] = max(per_moment[sid], d)
            if skip is not None and skip(sid):
                continue
            raw = max(raw, math.dist(h[sid][t], g[sid][t]))
            if d > worst:
                worst, where = d, "sync %d thing %d: host %s guest %s (frames %.0f ms apart)" % (
                    sid, t, h[sid][t], g[sid][t], dt * 1000)
    where = ("%s; " % where if where else "") + "%.3f before the speed allowance" % raw
    return moments, pairs, worst, where


def sent_flags(m):
    return int(m.group(6)) if m.group(6) is not None else None


def jumped(t0, p0, f0, t1, p1, f1):
    """The game's rule for not sliding a thing between two of the host's
    samples (iCoopWorld.cpp kSnapDist, kSnapSpeed): more than 10 units, or
    more than one at over 35 a second, or a death, break, or show or hide
    between them."""
    jump = math.dist(p0, p1)
    state = f0 is not None and f1 is not None and ((f0 ^ f1) & WF_STATE) != 0
    return state or jump > 10.0 or (jump > 1.0 and jump > SNAP_SPEED * max(t1 - t0, 1) / 1000.0)


def teleports(host):
    """thing -> host times at which it jumped (jumped()), or may have: the
    host logs a thing only while it is near a player, so its first sample
    after a gap in the log (over 120 ms) may follow a jump the log missed
    (an enemy's patrol starting over far off)."""
    last, out = {}, {}
    for m in SENT.finditer(host):
        t, thing = int(m.group(1)), int(m.group(2))
        pos = tuple(float(m.group(i)) for i in (3, 4, 5))
        flags = sent_flags(m)
        if thing in last:
            t0, p0, f0 = last[thing]
            if t - t0 > 120 or jumped(t0, p0, f0, t, pos, flags):
                out.setdefault(thing, []).append(t)
        last[thing] = (t, pos, flags)
    return out


def compare_shown(host, guest, skip=None):
    """(checked, worst, where, skipped): every `world shown` position of the guest
    against the host's `world sent` ones at that moment, interpolated (outside
    `skip(host ms)`: a stall and the second after it)."""
    sent = {}
    for m in SENT.finditer(host):
        sent.setdefault(int(m.group(2)), []).append((int(m.group(1)), tuple(float(m.group(i)) for i in (3, 4, 5)),
                                                     sent_flags(m)))
    for v in sent.values():
        v.sort()
    checked = skipped = 0
    worst, where = 0.0, ""
    for m in SHOWN.finditer(guest):
        t, thing = int(m.group(1)), int(m.group(2))
        pos = tuple(float(m.group(i)) for i in (3, 4, 5))
        seq = sent.get(thing)
        if not seq or (skip is not None and skip(t)):
            skipped += 1
            continue
        times = [s[0] for s in seq]
        k = bisect.bisect_right(times, t)
        if k == 0 or k == len(seq) or seq[k][0] - seq[k - 1][0] > 120:
            skipped += 1  # outside what the host logged, or a gap in its log
            continue
        (t0, p0, f0), (t1, p1, f1) = seq[k - 1], seq[k]
        if jumped(t0, p0, f0, t1, p1, f1):
            # A teleport (a level reset, a defeated enemy put back at its
            # spawn point): the game shows one end or the other, never a slide
            # across it.
            d = min(math.dist(pos, p0), math.dist(pos, p1))
            checked += 1
            if d > worst:
                worst, where = d, "thing %d at host ms %d, across a jump: host %s then %s, guest drew %s" % (
                    thing, t, p0, p1, pos)
            continue
        f = (t - t0) / (t1 - t0) if t1 > t0 else 0.0
        want = tuple(a + (b - a) * f for a, b in zip(p0, p1))
        d = math.dist(want, pos)
        checked += 1
        if d > worst:
            worst, where = d, "thing %d at host ms %d: host %s, guest drew %s" % (thing, t, want, pos)
    return checked, worst, where, skipped


def chase(host, guest):
    """(ok, detail): an enemy that targets the guest closes in on it, on both copies
    (any of the targets; one that hits the guest counts, however far it came)."""
    targets = sorted(set(int(m.group(1)) for m in TARGET.finditer(host)))
    hs, _, _ = syncs(host)
    # Since M3 the enemy's attacks land: a guest that stands by the robot long
    # enough is defeated and comes back beside the host, far off. The chase
    # is measured up to that defeat.
    down = guest.find("coop: this player was defeated")
    gs, gself, _ = syncs(guest if down < 0 else guest[:down])
    hitters = set(m.group(1) for m in DAMAGE_SENT.finditer(host))
    seen = []
    for t in targets:
        series = []
        for sid in sorted(gself):
            if sid in hs and t in hs[sid]:
                series.append((sid, math.dist(hs[sid][t], gself[sid]), gs.get(sid, {}).get(t)))
        if len(series) < 3:
            continue
        start = series[0][1]
        low = min(s[1] for s in series)
        moved = max(math.dist(hs[series[0][0]][t], hs[s[0]][t]) for s in series)
        # It went for the guest: it closed in to striking distance (within 4
        # units; a slammer strikes from about 4 off), moving itself (not only
        # the guest moving), or it hit the guest (M3: the hit, and a defeat
        # after it, end the chase however far it had to come).
        hit = str(t) in hitters
        ok = hit or (start - low >= 2.0 and low <= 4.0 and moved >= 2.0)
        seen.append((ok, hit, start - low, t, start, low, moved, len(series)))
    if not seen:
        return False, "no enemy targeted the guest while both logged it (targets: %s)" % targets[:12]
    # The one that met it (one that hit first), else the one that came closest.
    ok, hit, _, t, start, low, moved, n = max(seen, key=lambda s: (s[0], s[1], s[2]))
    return ok, "enemy thing %d: %.1f units from the guest, then %.1f (it moved %.1f; %d moments)%s; %d of %d targets " \
               "went for it" % (t, start, low, moved, n, "; it hit the guest" if hit else "",
                                sum(1 for s in seen if s[0]), len(seen))


def ride(host, guest, platform_id):
    """(ok, detail): the guest stood on the host's moving platform and went with it.
    At every same-moment capture after the guest was put on it, the guest's
    player is on the platform as the guest's game draws it (on its top, near
    its middle), the platform moved on the host by several units over those
    moments, and the guest moved with it."""
    m = re.search(r"coop: thing (\d+) mover .* id %s " % platform_id, guest)
    if not m:
        return False, "the platform %s is not in the guest's table" % platform_id
    thing = int(m.group(1))
    placed = re.search(r"tssm: test: frame (\d+): on entity %s" % platform_id, guest)
    if not placed:
        return False, "the guest was never put on the platform"
    hs, _, _ = syncs(host)
    gs, gself, _ = syncs(guest)
    # Sync moments after the guest was put on it: those the guest logged after
    # the placing line.
    cut = guest.find(placed.group(0))
    later = set(int(x) for x in re.findall(r"tssm: coop: sync (\d+) at host ms", guest[cut:]))
    on, off, plat_host, selves = 0, [], [], []
    for sid in sorted(later):
        if sid not in gself or thing not in gs.get(sid, {}):
            continue
        p = gs[sid][thing]
        me = gself[sid]
        flat = math.hypot(me[0] - p[0], me[2] - p[2])
        if flat <= 2.6 and -0.6 <= me[1] - p[1] <= 1.0:
            on += 1
        else:
            off.append("sync %d: guest %s, platform %s" % (sid, me, p))
        selves.append(me)
        if thing in hs.get(sid, {}):
            plat_host.append(hs[sid][thing])
    if on < 5:
        return False, "%d moments on the platform; %s" % (on, "; ".join(off[:2]))
    moved_plat = max(math.dist(a, b) for a in plat_host for b in plat_host) if plat_host else 0.0
    moved_me = max(math.dist(a, b) for a in selves for b in selves)
    ok = not off and moved_plat >= 3.0 and moved_me >= 3.0
    return ok, "thing %d: the guest on it at %d of %d moments; the platform went %.1f units on the host, " \
               "the guest %.1f with it%s" % (thing, on, on + len(off), moved_plat, moved_me,
                                              ("; off: " + "; ".join(off[:2])) if off else "")


def apart(host, guest):
    """(ok, detail): at the last same-moment capture both copies logged, the two
    players do not stand inside each other."""
    _, hself, _ = syncs(host)
    _, gself, _ = syncs(guest)
    both = sorted(set(hself) & set(gself))
    if not both:
        return False, "no same-moment capture on both"
    sid = both[-1]
    d = math.dist(hself[sid], gself[sid])
    return d >= 1.0, "%.2f units apart at sync %d (host %s, guest %s)" % (d, sid, hself[sid], gself[sid])


def saves(run_dir):
    """Save files a copy wrote (its save folder starts empty)."""
    d = os.path.join(run_dir, "save")
    out = []
    for root, _, files in os.walk(d):
        for f in files:
            if f != "coop_files.cache":
                out.append(os.path.relpath(os.path.join(root, f), d))
    return out


def frame_luma(path):
    """A captured frame's mean brightness (0-255), from its PNG (8-bit RGB or RGBA,
    not interlaced: the game's own captures); None if it cannot be read."""
    import struct
    import zlib
    try:
        data = open(path, "rb").read()
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            return None
        w, h, depth, ctype, _, _, lace = struct.unpack(">IIBBBBB", data[16:29])
        if depth != 8 or ctype not in (2, 6) or lace:
            return None
        bpp = 3 if ctype == 2 else 4
        raw, i = b"", 8
        while i < len(data):
            n, kind = struct.unpack(">I4s", data[i:i + 8])
            if kind == b"IDAT":
                raw += data[i + 8:i + 8 + n]
            i += 12 + n
        px = zlib.decompress(raw)
        stride = w * bpp
        prev = bytearray(stride)
        total, step = 0, 0
        for y in range(h):
            f = px[y * (stride + 1)]
            line = bytearray(px[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                if f == 1:
                    line[x] = (line[x] + a) & 255
                elif f == 2:
                    line[x] = (line[x] + b) & 255
                elif f == 3:
                    line[x] = (line[x] + (a + b) // 2) & 255
                elif f == 4:
                    pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                    line[x] = (line[x] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
            prev = line
            if y % 8 == 0:
                for x in range(0, stride, bpp * 8):
                    total += line[x] + line[x + 1] + line[x + 2]
                    step += 3
        return total / step if step else None
    except (OSError, ValueError, zlib.error, struct.error):
        return None


def clock_ms():
    """The games' log clock (steady_clock, the performance counter) in ms."""
    return time.perf_counter_ns() // 1000000


def drag_window(proc, log_path, marker, seconds, got):
    """The drag variants: once `marker` is in the copy's log, hold its window
    in Windows' own move loop for `seconds` (as a player holding the title
    bar), then let go. Fills `got` with the clock ms it started and ended."""
    import ctypes
    import ctypes.wintypes as wt
    u32 = ctypes.windll.user32
    deadline = time.time() + 300
    while time.time() < deadline and proc.poll() is None:
        try:
            if marker in open(log_path, "rb").read().decode("utf-8", "replace"):
                break
        except OSError:
            pass
        time.sleep(0.02)
    else:
        got["error"] = "the copy never reached the drag's frame"
        return
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(h, _):
        pid = wt.DWORD()
        u32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value == proc.pid and u32.IsWindowVisible(h):
            found.append(h)
        return True

    u32.EnumWindows(each, 0)
    if not found:
        got["error"] = "no window"
        return
    h = found[0]
    cursor = wt.POINT()
    u32.GetCursorPos(ctypes.byref(cursor))
    WM_SYSCOMMAND, SC_MOVE, WM_KEYDOWN, WM_KEYUP, VK_ESCAPE = 0x0112, 0xF010, 0x0100, 0x0101, 0x1B
    got["from"] = clock_ms()
    u32.PostMessageW(h, WM_SYSCOMMAND, SC_MOVE, 0)
    time.sleep(seconds)
    # Escape ends the move loop and puts the window back where it was.
    u32.PostMessageW(h, WM_KEYDOWN, VK_ESCAPE, 0x00010001)
    u32.PostMessageW(h, WM_KEYUP, VK_ESCAPE, 0xC0010001)
    got["to"] = clock_ms()
    u32.SetCursorPos(cursor.x, cursor.y)


def run(a, scene, variant):
    """One two-copy run; returns (passed, out folder)."""
    exe = os.path.abspath(a.exe)
    guest_exe = os.path.abspath(a.guest_exe) if a.guest_exe else exe
    assets = os.path.abspath(a.assets)
    out_root = os.path.abspath(a.out)
    sc = dict(SCENES[scene])
    out = os.path.join(out_root, "coop_%s_%s_%s" % (scene, variant, time.strftime("%Y%m%d_%H%M%S")))
    host_dir, guest_dir = os.path.join(out, "host"), os.path.join(out, "guest")
    os.makedirs(host_dir)
    os.makedirs(guest_dir)
    host_place, guest_place = sc.get("host_place", ""), sc.get("guest_place", "")
    host_extra = dict(sc.get("host_extra", {}))
    guest_extra = dict(sc.get("guest_extra", {}))
    dest = scene
    exit_frame = sc["exit"]
    # Per copy: when it exits, when its frames, HUD and Co-op tab are written.
    # The host stays 200 frames longer, so the guest's tab shows the session
    # still up.
    timing = {
        "host": {"exit": exit_frame + 200, "frames": [exit_frame - 400, exit_frame - 200],
                 "hud": exit_frame - 100, "gui": exit_frame - 60},
        "join": {"exit": exit_frame, "frames": [exit_frame - 400, exit_frame - 200],
                 "hud": exit_frame - 100, "gui": exit_frame - 60},
    }
    if variant == "follow":
        # Both start in BB02 (the same game files: a boot mod is part of the
        # files both must share); after the opening the host goes to the
        # scene (TSSM_TEST_SCENE) and the guest must follow by itself. Each
        # game's own opening of the new level (its fly-through, then Mindy's
        # talk and the level's task screen) plays out; the presses of B close
        # the talk and the task screen whenever they come (B does nothing in
        # plain play). Then: an autosave on both while the session is up (the
        # host's saves, the guest's must not), the host leaves, and the
        # guest's game asks for an autosave again, in the level it followed
        # the host to (it must not save there either).
        if dest == "BB02":
            dest = "DE01"
        sc = dict(SCENES[dest])
        mod = boot_mod(assets, out, "BB02")
        host_extra = {"TSSM_TEST_SCENE": "1000:%s" % dest}
        guest_extra = {}
        host_place = guest_place = ""  # the follow puts the guest beside the host
        b = ",".join("%d:b/1" % f for f in range(1900, 2451, 50))
        sc["host_script"] = sc["guest_script"] = SCENES["BB02"]["host_script"] + "," + b
        exit_frame = 3200
        host_extra["TSSM_TEST_AUTOSAVE"] = str(exit_frame - 700)
        guest_extra["TSSM_TEST_AUTOSAVE"] = "%d,%d" % (exit_frame - 700, exit_frame - 100)
        timing = {
            "host": {"exit": exit_frame - 300, "frames": [exit_frame - 800, exit_frame - 750],
                     "hud": exit_frame - 400, "gui": exit_frame - 360},
            "join": {"exit": exit_frame, "frames": [exit_frame - 800, exit_frame - 750],
                     "hud": exit_frame - 650, "gui": exit_frame - 600},
        }
    elif variant == "stay":
        # As follow, but the guest does not follow by itself: it stays in
        # BB02 while the host is in the other level, its Co-op tab shows the
        # "Go to the host's level" button (captured, then closed), and a press
        # of it takes the guest there.
        if dest == "BB02":
            dest = "DE01"
        sc = dict(SCENES[dest])
        mod = boot_mod(assets, out, "BB02")
        host_extra = {"TSSM_TEST_SCENE": "1000:%s" % dest}
        guest_extra = {"TSSM_COOP_FOLLOW": "0", "TSSM_TEST_COOP_GO": "1800", "TSSM_GUI_CLOSE": "1"}
        host_place = guest_place = ""
        sc["host_script"] = SCENES["BB02"]["host_script"] + "," + ",".join(
            "%d:b/1" % f for f in range(1900, 2451, 50))
        sc["guest_script"] = SCENES["BB02"]["host_script"] + "," + ",".join(
            "%d:b/1" % f for f in range(2700, 3251, 50))
        exit_frame = 3600
        timing = {
            "host": {"exit": exit_frame + 200, "frames": [exit_frame - 400, exit_frame - 200],
                     "hud": exit_frame - 100, "gui": exit_frame - 60},
            "join": {"exit": exit_frame, "frames": [1450, exit_frame - 400, exit_frame - 200],
                     "hud": exit_frame - 100, "gui": 1500},
        }
    elif variant == "look":
        # M4 (docs/MULTIPLAYER.md 7.8): the characters, in BB02, facing each
        # other. The guest takes Patrick by itself (automatic), then each
        # taps LB (the "white" button) in turn: the guest becomes SpongeBob
        # (two SpongeBobs), the host Patrick (each the other's character
        # again), the guest Patrick (two Patricks).
        mod = boot_mod(assets, out, "BB02")
        sc = dict(SCENES["BB02"])
        sc["host_script"] = sc["host_script"] + ",1450:white/1"
        # Then the guest holds LB (a second): its save has no costume
        # unlocked, so nothing changes and nothing is written.
        sc["guest_script"] = sc["guest_script"] + ",1150:white/1,1650:white/1,1900:white/60"
        host_place = "1000:24.3:1.5:-30.5:0"
        timing = {
            "host": {"exit": exit_frame + 200, "frames": [1100, 1350, 1600, 1850], "hud": exit_frame - 100,
                     "gui": exit_frame - 60},
            "join": {"exit": exit_frame, "frames": [1050, 1300, 1550, 1800], "hud": exit_frame - 100,
                     "gui": exit_frame - 60},
        }
    elif variant == "title":
        # M4's review: the guest leaves the game for the title menu
        # (TSSM_TEST_QUIT, as the pause menu's quit) in BB02 right after
        # drawing the host. The partner's model was built from the level's
        # memory, which goes with the level; the title draws its own scene
        # (and co-op's partner hook) without co-op's frame, so it must drop
        # that model and draw nothing of it. The guest's title runs on for
        # ten seconds to its own exit.
        mod = boot_mod(assets, out, "BB02")
        sc = dict(SCENES["BB02"])
        host_place = "1000:24.3:1.5:-30.5:0"
        guest_extra = {"TSSM_TEST_QUIT": str(TITLE_QUIT)}
        timing = {
            "host": {"exit": exit_frame + 200, "frames": [1300, 1450], "hud": 1350, "gui": 1400},
            "join": {"exit": exit_frame, "frames": [1300, 1450, 1800, exit_frame - 50], "hud": 1350, "gui": 1400},
        }
    elif variant == "persist":
        # M4 (docs/MULTIPLAYER.md 7.8): a session started from config.ini
        # files an earlier session left: the host's [coop] character =
        # patrick, costume_patrick = disco, costume_spongebob = mermaidman;
        # the guest's costume_spongebob = caveman (character automatic). Every
        # costume is offered (TSSM_TEST_COSTUMES=all). In BB02, facing each
        # other: the host takes Patrick (Disco) by its setting, the guest on
        # automatic takes the other character, SpongeBob (Caveman), going by
        # the host's look; each draws the other in an outfit this level has
        # until the next level. Then each holds LB a second: the next costume
        # of its character, kept in config.ini.
        mod = boot_mod(assets, out, "BB02")
        sc = dict(SCENES["BB02"])
        sc["host_script"] = sc["host_script"] + ",1500:white/60"
        sc["guest_script"] = sc["guest_script"] + ",1450:white/60"
        host_place = "1000:24.3:1.5:-30.5:0"
        host_extra = {"TSSM_TEST_COSTUMES": "all",
                      "CONFIG": "[coop]\ncharacter = patrick\ncostume_spongebob = mermaidman\n"
                                "costume_patrick = disco\n"}
        guest_extra = {"TSSM_TEST_COSTUMES": "all", "CONFIG": "[coop]\ncostume_spongebob = caveman\n"}
        timing = {
            "host": {"exit": exit_frame + 200, "frames": [1300, 1850], "hud": exit_frame - 100,
                     "gui": exit_frame - 60},
            "join": {"exit": exit_frame, "frames": [1250, 1800], "hud": exit_frame - 100, "gui": exit_frame - 60},
        }
    elif variant == "look-flood":
        # M4's fix round (docs/MULTIPLAYER.md 7.4): the newest look is never
        # dropped. As persist, but the guest's game stops for 15 s at its frame
        # 1280 while the host holds LB 12 times (one a second, each the next
        # costume): the host sends 12 looks the guest gets only when it runs
        # again, all in one frame. The guest must end up with the host's last
        # one (more than the 8 a second the log takes).
        mod = boot_mod(assets, out, "BB02")
        sc = dict(SCENES["BB02"])
        sc["host_script"] = sc["host_script"] + "," + ",".join("%d:white/40" % f for f in range(1360, 1360 + 55 * 12, 55))
        host_place = "1000:24.3:1.5:-30.5:0"
        host_extra = {"TSSM_TEST_COSTUMES": "all",
                      "CONFIG": "[coop]\ncharacter = patrick\ncostume_spongebob = mermaidman\n"
                                "costume_patrick = disco\n"}
        guest_extra = {"TSSM_TEST_COSTUMES": "all", "CONFIG": "[coop]\ncostume_spongebob = caveman\n",
                       "TSSM_TEST_STALL": "1280:15"}
        late = int(15 * FPS_ALONE)
        timing = {
            "host": {"exit": exit_frame + 200 + late, "frames": [1300, 1850, 2300 + late // 2],
                     "hud": exit_frame + late - 100, "gui": exit_frame + 200 + late - 60},
            # The guest's Co-op tab soon after it runs again, with the host still there:
            # it names the host's newest look.
            "join": {"exit": 1800, "frames": [1250, 1400, 1600], "hud": 1450,
                     "gui": 1500},
        }
    elif variant == "save":
        # M4 (docs/MULTIPLAYER.md 7.6): a save by hand on both, in BB02, as
        # the pause menu's save screen does it (TSSM_TEST_SAVE: straight into
        # the first slot). The host's is written (the control); the guest's
        # save screen closes at once, back to play, with a co-op notice.
        mod = boot_mod(assets, out, "BB02")
        sc = dict(SCENES["BB02"])
        sc["guest_script"] = sc["guest_script"] + SAVE_GUEST_KEYS
        host_extra = {"TSSM_TEST_SAVE": "1300"}
        guest_extra = {"TSSM_TEST_SAVE": "1300"}
        timing = {
            "host": {"exit": exit_frame + 200, "frames": [1340, 1450, 1600], "hud": exit_frame - 100,
                     "gui": exit_frame - 60},
            # The guest's HUD layer just after: the co-op notice says why.
            "join": {"exit": exit_frame, "frames": SAVE_GUEST_FRAMES, "hud": 1360,
                     "gui": exit_frame - 60},
        }
    elif variant == "own-show":
        # M4 (docs/MULTIPLAYER.md 7.3): a show only the guest's game started.
        # In DE01, after the opening, the guest's game starts the level's
        # flinger cutscene by itself (TSSM_TEST_EVENT, as its trigger would)
        # while the host plays on: the host sends nothing, and the guest's
        # skip (A) is taken.
        mod = boot_mod(assets, out, "DE01")
        sc = dict(SCENES["DE01"])
        sc["chase"] = False
        sc["guest_script"] = sc["guest_script"] + ",1450:a/1"
        host_place = guest_place = ""
        host_extra = {}
        guest_extra = {"TSSM_TEST_EVENT": "1300:de0789d3:cutscene"}
        dest = "DE01"
    elif variant == "replay-show":
        # M4's fix round (docs/MULTIPLAYER.md 7.3): a replay of a show the
        # host already played. In DE01 the host's game starts the flinger
        # cutscene (its frame 1100): it plays on both, and the host skips it
        # (1250), ending it on both. Then the guest's own game starts the
        # same cutscene by itself (its 1300, about 100 frames after the host's
        # end, as its trigger would): that replay is the guest's own, so its
        # skip (1370) is taken. (About 200 frames after the host's skip, the
        # level's own script starts it again on the host, which then plays on
        # both as the host's.)
        mod = boot_mod(assets, out, "DE01")
        sc = dict(SCENES["DE01"])
        sc["chase"] = False
        sc["host_script"] = sc["host_script"] + ",1250:a/1"
        sc["guest_script"] = sc["guest_script"] + ",1370:a/1"
        host_place = guest_place = ""
        host_extra = {"TSSM_TEST_EVENT": "1100:de0789d3:cutscene"}
        guest_extra = {"TSSM_TEST_EVENT": "1300:de0789d3:cutscene"}
        dest = "DE01"
        # Long enough for the level's own replay on the host (it starts 200
        # to 600 frames after the host's skip) to end there and on the guest.
        exit_frame = 2900
        timing = {
            "host": {"exit": exit_frame + 200, "frames": [1200, 1350, 2400, 2700], "hud": exit_frame - 100,
                     "gui": exit_frame - 60},
            "join": {"exit": exit_frame, "frames": [1150, 1330, 2350, 2650], "hud": exit_frame - 100,
                     "gui": exit_frame - 60},
        }
    else:
        mod = boot_mod(assets, out, scene)
    fight = FIGHTS.get(variant) or a.fight
    journey = fight == "levels"
    if journey:
        # M4 (docs/MULTIPLAYER.md 7.8): playing through together. Both boot
        # into BB01, a Patty Wagon level (one character: both drive one),
        # and connect; the host takes the level's own exit to DE01
        # (PORTAL_TO_DE01, the game's own portal event), the guest goes along
        # at once, both start DE01 together and its fly-through plays on both;
        # the guest's skip does nothing, the host's ends it on both. In DE01
        # each wears the costumes it chose (TSSM_TEST_COOP_LOOK, every costume
        # offered with TSSM_TEST_COSTUMES=all): the host SpongeBob as Mermaid
        # Man, the guest Patrick as Goofy Goober (its automatic character) and
        # SpongeBob as Caveman; the guest taps LB (SpongeBob: two SpongeBobs in
        # different costumes), then the host does (Patrick). The host starts a
        # cutscene of the level's (FLINGER_CUTSCENE_MGR_01, as its trigger
        # does): it plays on both, the guest's skip does nothing, the host's
        # ends it on both. Then the host takes DE01's exit to DE02, a slide
        # level (one character: both ride a SpongeBob-and-Patrick sled).
        mod = boot_mod(assets, out, "BB01")
        dest = "DE02"
        f = FIGHT_SETUPS["levels"]
        sc = {"exit": f["exit"], "host_script": f["host_script"], "guest_script": f["guest_script"]}
        host_place, guest_place = "", f["guest_place"]
        host_extra = dict(f["host_extra"])
        guest_extra = dict(f["guest_extra"])
        exit_frame = f["exit"]
        timing = {
            "host": {"exit": exit_frame + 200, "frames": f["host_frames"], "hud": exit_frame - 100,
                     "gui": exit_frame - 60},
            "join": {"exit": exit_frame, "frames": f["guest_frames"], "hud": exit_frame - 100, "gui": exit_frame - 60},
        }
    elif fight:
        # M3 (docs/MULTIPLAYER.md 6.7): a fight in DE01 by the robot, with
        # its own placing and scripts; also under the stall, drag and netsim
        # variants (--fight).
        sc = dict(sc)
        f = FIGHT_SETUPS[fight]
        if f.get("ini"):
            # Settings of the game's own sb04.ini (both copies share the mod).
            ini = os.path.join(mod, "sb04.ini")
            txt = open(ini, "rb").read().decode("latin-1")
            for k, v in f["ini"].items():
                txt, n = re.subn(r"(?m)^%s[ \t]*=[^\r\n]*" % re.escape(k), "%s = %s" % (k, v), txt)
                if not n:
                    txt = txt.replace("\r\n", "\r\n%s = %s\r\n" % (k, v), 1)
            open(ini, "wb").write(txt.encode("latin-1"))
        sc["host_script"] = sc["host_script"] + f.get("host_script_more", "")
        sc["guest_script"] = sc["guest_script"] + f.get("guest_script_more", "")
        host_place = f.get("host_place", "")
        guest_place = f.get("guest_place", "")
        host_extra = dict(f.get("host_extra", {}))
        guest_extra = dict(f.get("guest_extra", {}))
        exit_frame = f.get("exit", exit_frame)
        # A host that leaves first has its HUD and Co-op tab written before.
        hx = min(exit_frame, f.get("host_exit", exit_frame + 200))
        timing = {
            "host": {"exit": f.get("host_exit", exit_frame + 200), "frames": [hx - 400, hx - 200],
                     "hud": hx - 100, "gui": hx - 60},
            "join": {"exit": f.get("guest_exit", exit_frame), "frames": [exit_frame - 400, exit_frame - 200],
                     "hud": exit_frame - 100, "gui": exit_frame - 60},
        }
    stall_side = {"stall-host": "host", "stall-guest": "join", "drag-host": "host", "drag-guest": "join"}.get(variant)
    drag = variant.startswith("drag")
    stall_frame = a.stall_frame or STALL_FRAME
    if stall_side:
        # One side's game thread stops for a.stall seconds at STALL_FRAME.
        # Its frames come that much later, so the exits move: the guest
        # still exits first (its Co-op tab is captured with the session up),
        # and both play on for a good while after the stall.
        late = int(a.stall * FPS)
        exit_frame = max(exit_frame, stall_frame + 600)
        if not drag:
            (host_extra if stall_side == "host" else guest_extra)["TSSM_TEST_STALL"] = "%d:%g" % (stall_frame,
                                                                                                    a.stall)
        g_exit = exit_frame + (late if stall_side == "host" else 0)
        h_exit = exit_frame + 200 + (late if stall_side == "join" else 0)
        # While the guest is stalled the host runs alone, faster than FPS: its
        # exit moves later by the most that can add (else a long stall eats the
        # 200 frames by which it outlasts the guest), its captures stay put.
        h_slack = int(a.stall * (FPS_ALONE - FPS)) if stall_side == "join" else 0
        timing = {
            # A fight whose host runs faster than FPS a second (a small level)
            # stays longer, so it outlasts the stalled guest.
            "host": {"exit": h_exit + h_slack + (FIGHT_SETUPS[fight].get("host_slack", 0) if fight in FIGHT_SETUPS
                                                 else 0),
                     "frames": [h_exit - 600, h_exit - 400], "hud": h_exit - 300,
                     "gui": h_exit - 260},
            "join": {"exit": g_exit, "frames": [g_exit - 400, g_exit - 200], "hud": g_exit - 100, "gui": g_exit - 60},
        }
        if drag:
            # The frame the drag starts at: its capture line is the marker.
            timing[stall_side]["frames"].insert(0, stall_frame)
        if a.stall >= 3:
            # The other game's HUD while it waits ("waiting for ...'s game").
            other = "join" if stall_side == "host" else "host"
            timing[other]["hud"] = stall_frame + int(min(a.stall - 0.5, 0.6 * a.stall + 1.5) * FPS)
    blackout = variant == "blackout"
    if blackout:
        # M5: the guest's connection is cut for a.stall seconds at the stall
        # frame. Both games run on meanwhile; past the session timeout both
        # give up on the session and the guest joins again once the cut ends.
        rejoin = a.stall >= SESSION_TIMEOUT_S
        late = int((a.stall + (8 if rejoin else 3)) * FPS)
        exit_frame = max(exit_frame, stall_frame + late + 500)
        guest_extra["TSSM_TEST_BLACKOUT"] = "%d:%g" % (stall_frame, a.stall)
        if rejoin:
            # While it joins again the guest is still in the session as far as
            # its save goes (docs/MULTIPLAYER.md 7.6): autosaves asked across
            # the time between the loss and the end of the cut must not write.
            span = a.stall - SESSION_TIMEOUT_S
            guest_extra["TSSM_TEST_AUTOSAVE"] = ",".join(
                str(stall_frame + int((SESSION_TIMEOUT_S + span * k / 4.0) * FPS)) for k in (1, 2, 3))
        if a.hitch:
            # A stall of the guest's own game while nothing comes in.
            hf, _, hs = a.hitch.rpartition(":")
            guest_extra["TSSM_TEST_STALL"] = "%d:%g" % (int(hf) if hf else stall_frame + 250, float(hs))
        timing = {
            "host": {"exit": exit_frame + 200, "frames": [exit_frame - 400, exit_frame - 200],
                     "hud": stall_frame + int(min(3.5, a.stall - 0.5) * FPS), "gui": exit_frame - 60},
            "join": {"exit": exit_frame, "frames": [exit_frame - 400, exit_frame - 200],
                     "hud": stall_frame + int(((SESSION_TIMEOUT_S + a.stall) / 2 + 0.5 if rejoin else
                                               min(3.5, a.stall - 0.5)) * FPS),
                     "gui": exit_frame - 60},
        }
    if variant == "kick":
        # M5: the host kicks the guest once both are in view.
        host_extra["TSSM_TEST_COOP_KICK"] = "1500"
    password = secrets.token_hex(8)
    guest_password = password if variant != "wrong-password" else secrets.token_hex(8)
    if variant == "tab":
        # M5 fix round: everything through the Co-op tab's code, co-op off
        # at the start (the run's password is typed into the form).
        wrong = secrets.token_hex(8)
        host_extra["TSSM_COOP"] = guest_extra["TSSM_COOP"] = "off"
        host_extra["TSSM_TEST_COOP_TAB"] = ("%d:name=TabHost,port=%d,password=%s,host;%d:nametag=off,quality=off" %
                                            (TAB_HOST, a.port, password, TAB_OPTIONS))
        guest_extra["TSSM_TEST_COOP_TAB"] = (
            "%d:name=TabGuest,address=127.0.0.1:%d,password=%s,join;%d:password=%s,tryagain;"
            "%d:nametag=off,follow=off,reconnect=off,quality=off" %
            (TAB_JOIN, a.port, wrong, TAB_AGAIN, password, TAB_OPTIONS))
        tab_secrets = (password, wrong)
    lan_port = a.port + LAN_PORT_OFFSET
    third = None
    holder = None
    lan_code = join_code()
    if variant in LAN_VARIANTS:
        # The home network through the loopback test hooks only: the host's
        # answers on 127.0.0.1 at a test port, the guest asking 127.0.0.1.
        host_extra["TSSM_COOP_LAN_PORT"] = guest_extra["TSSM_COOP_LAN_PORT"] = str(lan_port)
        guest_extra["TSSM_COOP_LAN_TARGET"] = "127.0.0.1"
        host_extra["TSSM_TEST_COOP_CODE"] = lan_code
        wrong = "01" + join_code()[2:]
        tab_secrets = (password, lan_code, wrong, "%s-%s-%s" % (lan_code[:4], lan_code[4:8], lan_code[8:]))
        if variant != "lan-full":
            host_extra["TSSM_COOP"] = guest_extra["TSSM_COOP"] = "off"
        if variant in LAN_SHORT:
            exit_frame = {"lan-hidden": 1700, "lan-full": 1300}.get(variant, 1100)
            timing = {
                "host": {"exit": exit_frame + 200, "frames": [exit_frame - 400, exit_frame - 200],
                         "hud": exit_frame - 100, "gui": exit_frame - 60},
                "join": {"exit": exit_frame, "frames": [exit_frame - 400, exit_frame - 200],
                         "hud": exit_frame - 100, "gui": exit_frame - 60},
            }
        # Each copy's first search or hosting checks its game files first
        # (about 3 s); the overlay is closed again before the scripts' presses
        # at 620 (the guest's own game must skip its story movie).
        lan_host = "150:name=Host,open=on,hostlan;400:shot=lan_host;450:open=off"
        if variant == "lan":
            # The first join through the real widgets, by a controller: down
            # to the row, A picks it, Start opens the code entry and its pad
            # (by itself), then two keys (A on 0, right, A on 1, down, A on 9,
            # B deletes it), ten more on the pad. The second through the test
            # actions, its search stopped (as when the minute ends) while the
            # code entry is open.
            host_extra["TSSM_TEST_COOP_TAB"] = lan_host + ";500:hudshot=lan_hud_code"
            guest_extra["TSSM_TEST_COOP_TAB"] = (
                "150:name=LanGuest,open=on;420:listed,shot=lan_list;"
                "430:vpad=%s.a.start.a.right.a.down.a.b;535:pad=%s,shot=lan_pad;560:joincode;"
                "590:shot=lan_refused;605:stop;615:open=off;650:open=on;"
                "740:listed,pick=Host,joinpicked=pad;750:find=off;765:shot=lan_search_over;"
                "770:pad=%s;780:vpad=start;880:open=off" % (LAN_NAV, wrong[2:], lan_code))
        elif variant in ("lan-version", "lan-files"):
            guest_extra.update(REFUSALS["wrong-version" if variant == "lan-version" else "wrong-files"][0])
            host_extra["TSSM_TEST_COOP_TAB"] = lan_host
            guest_extra["TSSM_TEST_COOP_TAB"] = (
                "150:name=LanGuest,open=on;420:listed,tap=Host;435:pick=Host,shot=%s;"
                "460:joinpicked=pad,pad=%s,joincode;"
                "550:shot=%s_refused;600:open=off" % (variant.replace("-", "_"), lan_code, variant.replace("-", "_")))
        elif variant == "lan-full":
            # Host and guest from the environment, in a session at once; a
            # third copy searches.
            third = {
                "TSSM_COOP": "off", "TSSM_COOP_NAME": "Third", "TSSM_COOP_LAN_PORT": str(lan_port),
                "TSSM_COOP_LAN_TARGET": "127.0.0.1",
                # A join refused as full is asked again for up to 6 s (a
                # player's own session that just ended may be let go).
                "TSSM_TEST_COOP_TAB": "300:name=Third,open=on;600:listed,click=Host;615:pick=Host,shot=lan_full;"
                                      "640:joinpicked,code=%s,joincode;1080:shot=lan_full_refused;1120:open=off"
                                      % password,
            }
        elif variant == "lan-none":
            # Another program holds the discovery port before the host starts.
            import socket
            holder = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            holder.bind(("127.0.0.1", lan_port))
            host_extra["TSSM_TEST_COOP_TAB"] = lan_host
            guest_extra["TSSM_TEST_COOP_TAB"] = (
                "150:name=LanGuest,open=on;160:shot=lan_none_searching;660:listed,shot=lan_none_checklist;"
                "700:open=off;750:address=127.0.0.1:%d,password=%s,join" % (a.port, lan_code))
        elif variant == "lan-retry":
            # A tap picks the row; the code entry opens with the keyboard on
            # its field, and the wrong code is typed frames later. Try again
            # opens it with the pad (a finger was last used), the list's row
            # comes back while it is open, and the right code joins.
            host_extra["TSSM_TEST_COOP_TAB"] = lan_host
            guest_extra["TSSM_TEST_COOP_TAB"] = (
                "150:name=LanGuest,open=on;420:listed,tap=Host;435:pick=Host;440:joinpicked;470:code=%s;"
                "490:joincode;600:shot=lan_retry_refused,tryagain;606:padview;760:listed,padview,shot=lan_retry_pad;"
                "780:code=%s;800:joincode;1000:open=off" % (wrong, lan_code))
        elif variant == "lan-hidden":
            host_extra["TSSM_TEST_COOP_TAB"] = (
                "150:name=Host,open=on,host;400:shot=lan_hidden_host;450:open=off;700:visible=on;1000:visible=off")
            guest_extra["TSSM_TEST_COOP_TAB"] = (
                "150:name=LanGuest,open=on;600:listed,shot=lan_hidden_list;850:listed,shot=lan_hidden_visible;"
                "1300:listed;1350:address=127.0.0.1:%d,password=%s,join;1450:open=off" % (a.port, lan_code))
    extra = {"TSSM_COOP_NETSIM": "80,20,5"} if variant == "netsim" else {}
    if fight and fight in FIGHT_SETUPS and FIGHT_SETUPS[fight].get("netsim") and variant != "netsim":
        extra["TSSM_COOP_NETSIM"] = FIGHT_SETUPS[fight]["netsim"]
    if variant in REFUSALS:
        guest_extra.update(REFUSALS[variant][0])
    if variant == "no-answer":
        guest_extra["TSSM_COOP_ADDR"] = "127.0.0.1:%d" % (a.port + 1)
    if stall_side or blackout:
        # Same-moment captures four times a second (the second after the stall).
        extra["TSSM_COOP_SYNC_CAPTURE"] = "%d:%d" % (STALL_SYNC_MS, 400)
    if journey:
        # Three levels: same-moment captures all the way, and the camera's
        # fly-through state on both.
        extra["TSSM_COOP_SYNC_CAPTURE"] = "%d:%d" % (1000, 150)
        extra["TSSM_TEST_CAMLOG"] = "10"

    procs = []
    for role, d, pw, script, place, more in (
            ("host", host_dir, password, sc["host_script"], host_place, host_extra),
            ("join", guest_dir, guest_password, sc["guest_script"], guest_place, guest_extra)):
        env = env_for(role, d, assets, mod, a.port, pw, script, dict(extra, **more), timing[role], place)
        o = open(os.path.join(d, "log.txt"), "wb")
        e = open(os.path.join(d, "stderr.txt"), "wb")
        procs.append((role, d, subprocess.Popen([exe if role == "host" else guest_exe], cwd=d, env=env, stdout=o,
                                                stderr=e), o, e))
        time.sleep(1.0)  # the host's socket first
    third_dir = None
    if third is not None:
        third_dir = os.path.join(out, "third")
        os.makedirs(third_dir)
        t_timing = {"exit": 1200, "frames": [500], "hud": 1100, "gui": 1140}
        env = env_for("join", third_dir, assets, mod, a.port, "", sc["guest_script"], third, t_timing, "")
        env.pop("TSSM_COOP_ADDR", None)
        env.pop("TSSM_COOP_PASSWORD", None)
        o = open(os.path.join(third_dir, "log.txt"), "wb")
        e = open(os.path.join(third_dir, "stderr.txt"), "wb")
        procs.append(("third", third_dir, subprocess.Popen([exe], cwd=third_dir, env=env, stdout=o, stderr=e),
                      o, e))

    probed = {}
    if variant == "lan":
        prober = threading.Thread(target=lan_probe, args=(lan_port, os.path.join(guest_dir, "log.txt"), probed,
                                                          time.time() + a.timeout - 30), daemon=True)
        prober.start()

    dragged = {}
    if drag:
        role, d, p = [(r, d, p) for r, d, p, *_ in procs if r == stall_side][0]
        mover = threading.Thread(target=drag_window, args=(p, os.path.join(d, "log.txt"),
                                                           "capture: frame %d " % stall_frame, a.stall, dragged),
                                 daemon=True)
        mover.start()

    t0 = time.time()
    codes = {}
    timeout = max(a.timeout, 240 + 2 * a.stall) if stall_side else a.timeout
    for role, d, p, o, e in procs:
        left = max(1, timeout - (time.time() - t0))
        try:
            codes[role] = p.wait(left)
        except subprocess.TimeoutExpired:
            p.kill()  # our own copy, and only it
            p.wait()
            codes[role] = None
        o.close()
        e.close()

    if holder is not None:
        holder.close()
    logs = {role: open(os.path.join(d, "log.txt"), "rb").read().decode("utf-8", "replace")
            for role, d, *_ in procs}
    host, guest = logs["host"], logs["join"]
    procs = [p for p in procs if p[0] != "third"]
    rows = []

    def row(what, ok, detail=""):
        rows.append((what, ok, detail))

    for role in ("host", "join") + (("third",) if third is not None else ()):
        # The PC's own trouble, not co-op's, named so a rerun is the answer:
        # Direct3D 9 that would not open (seen while other runs held the GPU).
        env = "; Direct3D 9 did not open on this PC (rerun)" if "no adapter reports hardware support" in logs[role] \
            else ""
        row("%s exited by itself at frame %d" % (role, timing[role]["exit"] if role in timing else 1200),
            codes[role] == 0 and "TSSM_EXIT_AFTER reached" in logs[role], "exit %s%s" % (codes[role], env))
    if variant == "lan":
        lan_checks(row, host, guest, host_dir, guest_dir, a.port, lan_port, tab_secrets, probed)
    if variant in LAN_SHORT:
        lan_short_checks(row, variant, host, guest, logs.get("third", ""), host_dir, guest_dir, third_dir, a.port,
                         lan_port, tab_secrets)
    if variant == "title":
        row("host connected", "coop: connected (hosting)" in host)
        row("guest connected", "coop: connected (joined)" in guest)
        quit_at = guest.find("leaving the game for the title (TSSM_TEST_QUIT)")
        drawn = guest.find("coop: the partner is drawn as")
        row("the guest drew the host, then left for the title", 0 <= drawn < quit_at,
            "drawn at %d, left at %d" % (drawn, quit_at))
        dropped = guest.find("coop: the partner's model goes with the level", max(quit_at, 0))
        row("leaving the level dropped the partner's model (the title draws none of it)", quit_at >= 0 and dropped > 0,
            "at %d" % dropped)
        for role in ("host", "join"):
            bad = [l for l in logs[role].splitlines() if "unknown animation state" in l or "protocol errors" in l]
            row("%s: no unknown animation or protocol error" % role, not bad, bad[0].strip() if bad else "")
    elif variant in REFUSALS:
        # M5: each refusal, in words, on the guest (its log and its Co-op tab,
        # captured), and nothing of a session on either side.
        said = REFUSALS[variant][1]
        line = re.search(r"tssm: (coop: (?:the join was refused|no answer from the host).*)", guest)
        row("the guest was told: %s" % said[6:], said in guest, line.group(1)[:140] if line else "")
        if variant == "wrong-password":
            row("host saw a wrong password", "wrong password" in host)
        if variant == "no-answer":
            row("the guest's answer came after the handshake's 10 s", "coop: joining" in guest, "")
        row("no partner on the host", "coop: partner frame" not in host and "coop: connected" not in host)
        row("no shared world on the guest", "coop: world got" not in guest and "coop: connected" not in guest)
        row("the guest did not try again by itself", "joining again" not in guest)
    elif variant == "tab":
        tab_checks(row, host, guest, host_dir, guest_dir, a.port, tab_secrets)
    elif variant in LAN_SHORT:
        pass
    elif variant == "kick":
        row("host connected", "coop: connected (hosting)" in host)
        row("guest connected", "coop: connected (joined)" in guest)
        drawn = guest.find("coop: the partner is drawn as")
        kicked = host.find("coop: the player was kicked")
        row("the guest drew the host, then the host kicked it", 0 <= drawn and kicked >= 0, "")
        told = guest.find("coop: disconnected: the host removed you from the game")
        row("the guest was told the host removed it", told >= 0, "")
        row("the guest did not join again by itself, and the host has no second join",
            "joining again" not in guest and guest.count("coop: connected (joined)") == 1 and
            host.count("coop: connected (hosting)") == 1, "")
        row("the guest's world went back to its own after the kick (the host's no longer shown)",
            told >= 0 and "coop: world shown" not in guest[told:] and "coop: world shown" in guest[:told], "")
    else:
        row("host connected", "coop: connected (hosting)" in host)
        row("guest connected", "coop: connected (joined)" in guest)
        # The guest takes the character the host is not (Patrick, or
        # SpongeBob where a level starts as Patrick, like TT01).
        sw = re.search(r"coop: switched to (\w+)", guest)
        other = sw.group(1) if sw else "?"
        hostchar = "SpongeBob" if other == "Patrick" else "Patrick"
        if variant in ("persist", "look-flood") or (fight in FIGHT_SETUPS and FIGHT_SETUPS[fight].get("host_patrick")):
            # The host plays Patrick by its [coop] character: the guest keeps
            # SpongeBob, BB02's own (no switch).
            other, hostchar = "SpongeBob", "Patrick"
            row("guest kept SpongeBob, the other character than the host's Patrick",
                sw is None or sw.group(1) == "SpongeBob", sw.group(0) if sw else "no switch")
        elif fight in FIGHT_SETUPS and FIGHT_SETUPS[fight].get("one_char"):
            # A one-character level (the Patty Wagon, an arena): no switch.
            other = hostchar = FIGHT_SETUPS[fight]["one_char"]
            row("one-character level: the guest plays its character too (no switch)", sw is None,
                sw.group(0) if sw else "no switch")
        else:
            row("guest switched to the other character", sw is not None, other)
        row("host draws the guest as %s" % other, "coop: the partner is drawn as %s" % other in host)
        row("guest draws the host as %s" % hostchar, "coop: the partner is drawn as %s" % hostchar in guest)
        n1, p1 = compare(host, guest)
        n2, p2 = compare(guest, host)
        row("host shows what the guest sent", n1 > 20 and not p1, "%d samples; %s" % (n1, "; ".join(p1[:3])))
        row("guest shows what the host sent", n2 > 20 and not p2, "%d samples; %s" % (n2, "; ".join(p2[:3])))

        # M2: the shared world.
        ht = [m.groups() for m in TABLE.finditer(host)]
        gt = [m.groups() for m in TABLE.finditer(guest)]
        hs = {s: (n, t) for s, n, t in ht}
        gs = {s: (n, t) for s, n, t in gt}
        same = dest in hs and hs.get(dest) == gs.get(dest)
        row("both made the same table of shared things in %s" % dest, same,
            "host %s, guest %s" % (hs.get(dest), gs.get(dest)))
        got = guest.count("coop: world got")
        shown = guest.count("coop: world shown")
        # BB01's wagon roads have no moving thing near the players to log as
        # received; what the guest drew is logged all the same.
        scripted = fight in FIGHT_SETUPS and FIGHT_SETUPS[fight].get("scripted_movers")
        # A fight whose guest plays a movie of its own game's (its script's)
        # draws nothing of the host's world meanwhile.
        movie = fight in FIGHT_SETUPS and FIGHT_SETUPS[fight].get("guest_movie")
        row("the guest applied the host's snapshots and drew its world", movie or ((got > 50 or scripted) and shown > 20),
            "%d entries logged as received, %d as drawn%s" % (got, shown, " (the guest's own movie)" if movie else ""))
        full = re.findall(r"coop: the host's world is shown: (\d+) of (\d+) things known, (\d+) ms", guest)
        row("the guest showed the host's world only with every thing known",
            bool(full) and all(k == n for k, n, _ in full),
            "; ".join("%s of %s after %s ms" % f for f in full))
        stalled = logs.get(stall_side, "") if stall_side else (guest if variant == "look-flood" else "")
        sfrom, sto = STALL_FROM.search(stalled), STALL_TO.search(stalled)
        s0 = int(sfrom.group(2)) if sfrom else None
        s1 = int(sto.group(1)) if sto else None
        if drag:
            s0, s1 = dragged.get("from"), dragged.get("to")
            print("window held from clock ms %s to %s %s" % (s0, s1, dragged.get("error", "")))
        after_ms = 1000
        if blackout:
            # The cut, to its end (or to the guest's join again past the
            # session timeout), and the time after it: not compared.
            cut, back = BLACKOUT_AT.search(guest), REJOINED.search(guest)
            s0 = int(cut.group(2)) if cut else None
            s1 = (int(back.group(2)) if back else None) if a.stall >= SESSION_TIMEOUT_S else (
                s0 + int(a.stall * 1000) if s0 is not None else None)
            after_ms = 3000 if a.stall >= SESSION_TIMEOUT_S else 1000
            print("connection cut from clock ms %s; back in step expected from %s" % (s0, s1))
        cut_s = FIGHT_SETUPS[fight].get("cut") if fight in FIGHT_SETUPS else None
        if cut_s:
            # A fight with the guest's connection cut a while (its own world
            # meanwhile): the cut and the 3 s after it are not compared.
            cut = BLACKOUT_AT.search(guest)
            s0 = int(cut.group(2)) if cut else None
            s1 = s0 + int(cut_s * 1000) if s0 is not None else None
            after_ms = 3000
        skip = (lambda sid: s0 - 200 <= sid <= s1 + after_ms) if (s0 and s1) else None
        if journey or (fight in FIGHT_SETUPS and FIGHT_SETUPS[fight].get("hitch_skip")):
            # Each level load, and each cutscene that loads at once, stops a
            # game for a second or more: as after a test's stall, the moments
            # up to a second after one are not compared (and the test's own
            # stall, if any).
            hitches = [(int(m.group(2)) - int(m.group(1)), int(m.group(2))) for log in (host, guest)
                       for m in re.finditer(r"coop: this game ran no frame for (\d+) ms \(a stall\); going on from "
                                            r"the partner's newest \(clock ms (\d+)\)", log)]
            test_skip = skip
            skip = lambda sid: (any(a - 200 <= sid <= b + 1000 for a, b in hitches) or
                                (test_skip is not None and test_skip(sid)))
        if variant in ("own-show", "replay-show"):
            # While the guest's game plays its own cutscene, its game is in the
            # cutscene and poses nothing (the host's things hold still there):
            # the moments from its start to a second after its skip are not
            # compared.
            ev = guest.find("(TSSM_TEST_EVENT)")
            sk = guest.find("a skip by this player was taken")
            before = [int(m.group(1)) for m in re.finditer(r"coop: sync (\d+) at host ms", guest[:ev])] if ev >= 0 else []
            after = [int(m.group(1)) for m in re.finditer(r"coop: sync (\d+) at host ms", guest[sk:])] if sk >= 0 else []
            windows = []
            if before and after:
                windows.append((before[-1], after[0] + 1000))
                print("the guest's own cutscene: host ms %d to %d not compared" % windows[-1])
            # replay-show: the level's own script then starts the cutscene
            # again on the host, which flings the host's player off and
            # defeats it there; the host's game leaves the cutscene with its
            # level reset, and the guest's copy ends a second later (the host
            # is in no show). Meanwhile the guest's game is in the cutscene and
            # poses nothing: from the host's start to a second after the
            # guest's end, not compared.
            if variant == "replay-show":
                ends = [m.start() for m in re.finditer(r"coop: the host's show \d+ ended there without its end", guest)]
                plays = [m.start() for m in re.finditer(r"coop: the host's cutscene \d+ (plays here too|: it already "
                                                        r"plays here)", guest)]
                for e in ends:
                    p0 = max([p for p in plays if p < e] or [-1])
                    b = [int(m.group(1)) for m in re.finditer(r"coop: sync (\d+) at host ms", guest[:p0])] if p0 >= 0 else []
                    a2 = [int(m.group(1)) for m in re.finditer(r"coop: sync (\d+) at host ms", guest[e:])]
                    if b and a2:
                        windows.append((b[-1], a2[0] + 1000))
                        print("the host's replay, until the guest's copy ended: host ms %d to %d not compared" %
                              windows[-1])
            if windows:
                skip = lambda sid: any(w0 <= sid <= w1 for w0, w1 in windows)
        per = {}
        moments, pairs, worst, where = compare_syncs(host, guest, skip, per)
        row("same places at the same moments (sync captures, <= 0.25)%s" % (
            " outside the stall and the second after it" if skip else ""),
            (moments >= 5 and pairs >= 10 or movie or scripted) and worst <= 0.25,
            "%d moments, %d positions, worst %.3f %s" % (moments, pairs, worst, where))
        if stall_side and not journey:
            stall_checks(row, a.stall, stall_side, host, guest, s0, s1, per, drag,
                         one_char=bool(fight in FIGHT_SETUPS and FIGHT_SETUPS[fight].get("one_char")))
        if blackout:
            blackout_checks(row, a.stall, host, guest, s0, s1, per, guest_dir)
            if a.hitch:
                ran = [int(m) for m in re.findall(r"coop: this game ran no frame for (\d+) ms \(a stall\)", guest)]
                want = float(a.hitch.rpartition(":")[2]) * 1000
                row("the guest's own game stopped for %s s during the cut (--hitch)" % a.hitch.rpartition(":")[2],
                    "(TSSM_TEST_STALL)" in guest and any(r >= want - 100 for r in ran),
                    "its stops: %s ms" % ran[:6])
        checked, worst2, where2, skipped = compare_shown(host, guest, skip)
        row("everything the guest drew is where the host had it (<= 0.25)", (checked >= 50 or movie) and worst2 <= 0.25,
            "%d positions, worst %.3f %s (%d outside the host's log)" % (checked, worst2, where2, skipped))
        # A stop in posing the host's things that ends a few frames later is a
        # flicker: for those frames the guest's copies ran their own AI.
        flips = [(m.group(1), int(m.group(2))) for m in
                 re.finditer(r"coop: posing the host's things (on|off) at frame (\d+)", guest)]
        flicker = ["off at %d, on at %d" % (f, g) for (x, f), (y, g) in zip(flips, flips[1:])
                   if x == "off" and y == "on" and g - f <= 30]
        if stall_side:
            # Through a stall too: the guest never gives the host's things
            # back to its own AI.
            flicker += ["off at %d" % f for x, f in flips if x == "off"]
        if blackout:
            # A cut connection: the guest's things go back to its own AI once
            # nothing is heard for 3 s (and with the session), and back to the
            # host's when it is heard again: one stop and one start, no more.
            flicker = [] if len(flips) <= 3 else ["%d starts and stops" % len(flips)]
        row("the guest posed the host's things without a gap", not flicker,
            "%d starts and stops%s" % (len(flips), ("; gaps: " + ", ".join(flicker[:4])) if flicker else ""))
        moved = re.findall(r"coop: pose (?:check|outside its samples): .*", guest)
        known = []
        if journey or (fight in FIGHT_SETUPS and FIGHT_SETUPS[fight].get("scripted_movers")):
            # BB01's platforms run by the level's own scripts (each game's own,
            # docs/MULTIPLAYER.md 7.9) can be put back by a script of the
            # guest's for a frame; listed, not counted.
            level, moved, known = None, [], []
            for line in guest.splitlines():
                m = re.search(r"coop: level (\w+) shares", line)
                if m:
                    level = m.group(1)
                elif re.search(r"coop: pose (?:check|outside its samples): ", line):
                    (known if level == "BB01" else moved).append(line.strip())
        row("the guest posed the host's things between their samples, and nothing moved them after", not moved,
            "; ".join(moved[:2]) + ("; in BB01 (its scripted platforms, not counted): %d, e.g. %s" %
                                    (len(known), known[0]) if known else ""))
        if journey:
            journey_checks(row, host, guest, host_dir, guest_dir, stall_side, a.stall if stall_side else 0,
                           stall_frame if stall_side else 0)
        if variant == "look":
            look_checks(row, host, guest, host_dir, guest_dir)
        elif variant == "persist":
            persist_checks(row, host, guest, host_dir, guest_dir)
        elif variant == "look-flood":
            look_flood_checks(row, host, guest)
        elif variant == "replay-show":
            replay_show_checks(row, host, guest)
        elif variant == "save":
            save_checks(row, host, guest, host_dir, guest_dir)
        elif variant == "own-show":
            started = guest.find("event 190 to de0789d3 (TSSM_TEST_EVENT)")
            taken = guest.find("coop: a skip by this player was taken: the show is this game's own")
            row("the guest's own cutscene: the host sent no show, and the guest's skip was taken",
                0 <= started < taken and "(cutscene start) sent to the guest" not in host and
                "a skip by this player was not taken" not in guest,
                "started at %d, skip taken at %d" % (started, taken))
        elif fight in FIX_FIGHTS:
            fix_checks(row, fight, host, guest, host_dir, guest_dir)
        elif fight:
            fight_checks(row, fight, host, guest, timing)
        if sc.get("chase") and variant not in ("follow", "stay") and not fight:
            ok, detail = chase(host, guest)
            row("an enemy goes for the guest when the guest is nearer", ok, detail)
        if sc.get("ride") and variant not in ("follow", "stay"):
            ok, detail = ride(host, guest, sc["ride"])
            row("the guest rides the host's moving platform", ok, detail)
        died = re.findall(r"coop: enemy thing (\d+) died \(death", host)
        shown_dead = re.findall(r"coop: enemy thing (\d+) died on the host", guest)
        if died and not (stall_side and a.stall > STALL_LIMIT_S) and not (blackout and a.stall >= SESSION_TIMEOUT_S):
            row("enemies the host defeated blew up on the guest's screen too", set(died) <= set(shown_dead),
                "host: %s; guest: %s" % (sorted(set(died)), sorted(set(shown_dead))))
        rates = [float(m.group(1)) for m in TRAFFIC.finditer(host)]
        row("host traffic under the 36 KB/s ceiling", bool(rates) and max(rates) < 36.0,
            "KB/s out: max %.1f, mean %.1f over %d samples" % (max(rates or [0]), sum(rates or [0]) / max(1, len(rates)),
                                                             len(rates)))
        if variant == "netsim":
            lags = [int(m.group(1)) for m in LAG.finditer(guest)]
            row("the guest draws the host's world at most 300 ms back", bool(lags) and max(lags) <= 300,
                "lags %s ms" % lags[:12])
        if variant == "stay":
            go = guest.find("TSSM_TEST_COOP_GO")
            went = guest.find("coop: following the host to %s" % dest)
            row("with following off, the guest stayed until it pressed \"Go to the host's level\"",
                go >= 0 and went > go and "the overlay" in guest[:go],
                "pressed at %s, went at %s" % (go, went))
            m = re.search(r"arrived in the host's level; placed (.*?) at (\S+) (\S+) (\S+) \(the host at (\S+) "
                          r"(\S+) (\S+)\)", guest)
            d = math.dist([float(m.group(i)) for i in (2, 3, 4)], [float(m.group(i)) for i in (5, 6, 7)]) if m else 0
            row("the guest arrived and was put beside the host", m is not None and 1.0 <= d <= 3.0,
                "%s, %.2f units from the host" % (m.group(1), d) if m else "")
            ok, detail = apart(host, guest)
            row("the two players stay apart after the level's opening", ok, detail)
        if variant == "follow":
            row("the guest followed the host to %s" % dest, "coop: following the host to %s" % dest in guest)
            m = re.search(r"arrived in the host's level; placed (.*?) at (\S+) (\S+) (\S+) \(the host at (\S+) (\S+) "
                          r"(\S+)\)", guest)
            d = math.dist([float(m.group(i)) for i in (2, 3, 4)], [float(m.group(i)) for i in (5, 6, 7)]) if m else 0
            row("the guest arrived and was put beside the host", m is not None and 1.0 <= d <= 3.0,
                "%s, %.2f units from the host" % (m.group(1), d) if m else "")
            ok, detail = apart(host, guest)
            row("the two players stay apart after the level's opening", ok, detail)
            hsaves, gsaves = saves(host_dir), saves(guest_dir)
            asked = len(re.findall(r"autosave asked", guest))
            skipped = len(re.findall(r"coop: autosave skipped", guest))
            left_at = guest.find("coop: the host ended the session")
            late = left_at >= 0 and "coop: autosave skipped" in guest[left_at:]
            row("the host's game autosaved (control)", "the slot is kept (saved)" in host and bool(hsaves),
                "host saves: %s" % hsaves)
            row("the guest's game did not autosave, in the session or after it in the host's level",
                asked == 2 and skipped == 2 and late and not gsaves,
                "asked %d, skipped %d, after the host left: %s, guest saves: %s" % (asked, skipped, late, gsaves))
        if variant in ("normal", "netsim"):
            # M5: the connection quality the HUD shows: good on one PC, fair
            # through the simulated 80 ms (+20) and 5% loss each way.
            q = [m.groups() for m in QUALITY.finditer(guest)]
            want = "2" if variant == "netsim" else "1"
            late = q[len(q) // 2:]
            row("the guest's connection quality says %s" % ("fair" if want == "2" else "good"),
                bool(late) and sum(1 for g in late if g[3] == want) >= max(1, len(late) * 2 // 3),
                "ping, lost in, lost out, quality: %s" % [g for g in late[:6]])
        expected_drop = blackout and a.stall >= SESSION_TIMEOUT_S
        for role in ("host", "join"):
            bad = [l for l in logs[role].splitlines()
                   if "unknown animation state" in l or "protocol errors" in l or
                   (("timed out" in l or "connection was lost" in l) and not expected_drop)
                   or "is not this level" in l or "faster than a player" in l]
            row("%s: no unknown animation, level mismatch, protocol error or timeout" % role, not bad,
                bad[0].strip() if bad else "")
    for role, d, *_ in procs:
        frames = os.listdir(os.path.join(d, "frames"))
        # maintoken-stop: the host's co-op HUD is gone with co-op.
        shots = ("coop_tab.png",) if fight == "maintoken-stop" and role == "host" else ("coop_tab.png", "hud.png")
        missing = [p for p in shots if p not in frames]
        sync = [f for f in frames if f.startswith("sync_")]
        if variant not in REFUSALS and variant not in LAN_SHORT + ("title", "kick") and len(sync) < 5:
            missing.append("sync captures (%d)" % len(sync))
        row("%s frames captured" % role, not missing, "missing %s" % missing if missing else "%d sync" % len(sync))
    if variant not in REFUSALS and variant not in LAN_SHORT:
        both = sorted(set(f for f in os.listdir(os.path.join(host_dir, "frames")) if f.startswith("sync_")) &
                      set(f for f in os.listdir(os.path.join(guest_dir, "frames")) if f.startswith("sync_")))
        print("same-moment frame pairs (host/frames and guest/frames): %d, e.g. %s" % (len(both), both[-3:]))

    width = max(len(r[0]) for r in rows)
    for what, ok, detail in rows:
        print("%-*s  %s  %s" % (width, what, "PASS" if ok else "FAIL", detail))
    passed = all(r[1] for r in rows)
    print("\n%s: %d of %d (%s)" % ("PASS" if passed else "FAIL", sum(1 for r in rows if r[1]), len(rows), out))
    sys.stdout.flush()
    return passed, out


DAMAGE_SENT = re.compile(r"coop: an enemy hit the guest: thing (-?\d+), (\d+) damage, kind (\d+)")
DAMAGE_GOT = re.compile(r"coop: the host says an enemy hit this player: thing (-?\d+), (\d+) damage, kind (\d+); "
                        r"(it lands|no effect now) \(health (\d+) to (\d+)\)")
HIT_APPLIED = re.compile(r"coop: the guest's hit on thing (\d+) \(attack (\d+), level (\d+)\) applied: (\d+) damage"
                         r"(; the enemy is defeated)?")
PLACED = re.compile(r"coop: (respawned in a bubble beside the host|the host plays again after its defeat); placed "
                    r"(.*?) at (\S+) (\S+) (\S+) \(the host at (\S+) (\S+) (\S+)\)")


def own_flags(log):
    """The flags of every state a copy logged as its own (PS_DEAD is 16)."""
    return [int(m.group(11)) for m in STATE.finditer(log) if m.group(1) == "self"]


def fight_checks(row, fight, host, guest, timing):
    """M3's checks (docs/MULTIPLAYER.md 6.7)."""
    sent = [m.groups() for m in DAMAGE_SENT.finditer(host)]
    got = [m.groups() for m in DAMAGE_GOT.finditer(guest)]
    landed = [g for g in got if g[3] == "it lands"]
    wrong = [g for g in landed if int(g[4]) - int(g[5]) != min(int(g[1]), int(g[4]))]
    if fight in ("combat", "respawn"):
        # Every hit the host sent arrived once, in order, and each that landed
        # took exactly its damage off the guest's health.
        same = [(s[0], s[1], s[2]) for s in sent] == [(g[0], g[1], g[2]) for g in got]
        row("an enemy hit the guest: the host sent it, the guest took it once (health down by the damage)",
            bool(sent) and same and bool(landed) and not wrong,
            "host sent %d, guest applied %d (%d landed: %s)%s" % (
                len(sent), len(got), len(landed), ", ".join("%s->%s" % (g[4], g[5]) for g in landed[:6]),
                ("; wrong: %s" % wrong[:2]) if wrong else ""))
    if fight == "host-paused":
        # The host's own states said paused (PS_PAUSED, 4) for a while; the
        # claims that came meanwhile were refused for it, and none was applied
        # before the last of those refusals.
        paused = sum(1 for f in own_flags(host) if f & 4)
        lines = host.splitlines()
        refused_at = [i for i, l in enumerate(lines) if "refused: the host's game is paused" in l]
        applied_at = [i for i, l in enumerate(lines) if HIT_APPLIED.search(l)]
        row("while the host's game was paused, the guest's claims were refused (no enemy hurt meanwhile)",
            paused >= 3 and bool(refused_at) and not any(refused_at[0] < i < refused_at[-1] for i in applied_at),
            "the host's own states paused: %d; %d claims refused for the pause; %d applied, %d of them during it" % (
                paused, len(refused_at), len(applied_at),
                sum(1 for i in applied_at if refused_at and refused_at[0] < i < refused_at[-1])))
    if fight in ("combat", "bowl", "host-paused"):
        claims = len(re.findall(r"coop: hit claim sent", guest))
        applied = [m.groups() for m in HIT_APPLIED.finditer(host)]
        refused = re.findall(r"coop: the guest's hit on thing \d+.*refused: (.*)", host)
        defeated = sorted(set(a[0] for a in applied if a[4]))
        shown = set(re.findall(r"coop: enemy thing (\d+) died on the host", guest))
        deaths = re.findall(r"coop: enemy thing (\d+) died \(death", host)
        row("the guest's hits went to the host, which applied them", claims >= 1 and bool(applied),
            "%d claims sent, %d applied, %d refused (%s)" % (claims, len(applied), len(refused),
                                                           "; ".join(sorted(set(refused))[:3])))
        row("the guest defeated an enemy: it died once on the host and blew up on the guest's screen",
            bool(defeated) and set(defeated) <= shown and all(deaths.count(t) == 1 for t in defeated),
            "defeated %s; death effects on the host %s; shown on the guest %s" % (defeated, deaths, sorted(shown)))
        if fight == "bowl":
            # From a distance: the SpongeBowl (attack 5), checked against its
            # own reach on the host.
            bowls = [a for a in applied if a[1] == "5"]
            row("the guest's SpongeBowl hit from afar and the host applied it (attack 5)", bool(bowls),
                "%d SpongeBowl hits applied of %d" % (len(bowls), len(applied)))
    # Pickups the guest touched (the shiny objects by the robot): each asked of
    # the host, counted there once, and played on the guest once.
    asked = re.findall(r"coop: pickup (\d+) asked of the host", guest)
    took = re.findall(r"coop: the guest took pickup (\d+) ", host)
    gave = re.findall(r"coop: the host gave this player pickup (\d+) ", guest)
    if fight == "combat" or asked:
        row("the guest's pickups were the host's: asked, counted on the host once, played on the guest once",
            bool(asked) and sorted(took) == sorted(gave) and len(set(took)) == len(took) and set(took) <= set(asked),
            "asked %s; the host counted %s; the guest got %s" % (asked[:8], took[:8], gave[:8]))
    if fight == "respawn":
        down = guest.count("this player was defeated; it comes back beside the host")
        back = re.findall(r"coop: this player is back \(respawn (\d+)\), health (\d+)", guest)
        m = PLACED.search(guest)
        d = math.dist([float(m.group(i)) for i in (3, 4, 5)], [float(m.group(i)) for i in (6, 7, 8)]) if m else 0
        row("the guest was defeated and came back beside the host in a bubble, health full",
            down >= 1 and len(back) == down and m is not None and 1.0 <= d <= 3.0 and all(int(h) > 1 for _, h in back),
            "defeated %d, back %s, %s" % (down, back, ("placed %s, %.2f units from the host" % (m.group(2), d))
                                          if m else "never placed"))
        row("nothing was reset: no lost life, the guest's level not loaded again, the host never defeated",
            "loses a life as in single-player" not in guest and len(re.findall(r"coop: level DE01 shares", guest)) == 1
            and not any(f & 16 for f in own_flags(host)) and "the guest was defeated; its own game brings it back"
            in host,
            "the guest's level built %d times; the host's own states defeated: %d" % (
                len(re.findall(r"coop: level DE01 shares", guest)), sum(1 for f in own_flags(host) if f & 16)))
    if fight == "host-death":
        dead = any(f & 16 for f in own_flags(host))
        m = PLACED.search(guest)
        d = math.dist([float(m.group(i)) for i in (3, 4, 5)], [float(m.group(i)) for i in (6, 7, 8)]) if m else 0
        row("the host was defeated and its game kept its own rules (its level reset)",
            dead and "the host was defeated (its game keeps its own rules" in guest,
            "host's own states defeated: %d" % sum(1 for f in own_flags(host) if f & 16))
        row("once the host played again, the guest was put beside it",
            m is not None and m.group(1).startswith("the host plays again") and 1.0 <= d <= 3.0,
            ("placed %s, %.2f units from the host" % (m.group(2), d)) if m else "never placed")
        row("the guest's own game lost nothing (no defeat, its level not loaded again)",
            "this player was defeated" not in guest and len(re.findall(r"coop: level DE01 shares", guest)) == 1, "")


CLAIM = re.compile(r"coop: hit claim sent: (?:breakable )?thing (\d+) attack (\d+)")
REFUSED = re.compile(r"coop: the guest's hit on (?:breakable )?thing (\d+)(?: \(attack \d+\))? refused: (.*)")
SENT_ANIM = re.compile(r"tssm: coop: world sent ms (\d+) thing (\d+) pos \S+ \S+ \S+ anim (\S+) flags (\d+)")
PROG = re.compile(r"coop: progress from the host: tokens (\d+), shiny objects (\d+), upgrade points (\d+) \(spent "
                  r"(-?\d+)\), moves ((?:\d+ ?){8}), health (\d+) (\d+)")


def fix_checks(row, fight, host, guest, host_dir, guest_dir):
    """Protocol 7's fixes (docs/MULTIPLAYER.md 13): each fight's own rows."""
    claims = [m.groups() for m in CLAIM.finditer(guest)]
    applied = [m.groups() for m in HIT_APPLIED.finditer(host)]
    refused = [m.groups() for m in REFUSED.finditer(host)]
    breaks = re.findall(r"coop: the guest's hit on breakable thing (\d+) \(attack (\d+)\) applied(; it broke)?", host)
    why = "; ".join(sorted(set(r[1] for r in refused))[:3])
    if fight == "far-area":
        culled = host.count("(in a part of the level culled here)")
        gone = [r for r in refused if r[1].startswith("the enemy is not there")]
        row("the guest's hits in a part of the level the host's game culls are applied",
            bool(claims) and bool(applied) and not gone,
            "%d claims sent, %d applied (%d in a culled part), %d refused as gone; refusals: %s" % (
                len(claims), len(applied), culled, len(gone), why))
        row("the crate it hit broke on the host", any(a[4] for a in applied),
            "defeated: %s" % sorted(set(a[0] for a in applied if a[4])))
    elif fight == "both-hit":
        # The host's spin hit the robot (its hit reaction, 4e81d69f), then the
        # guest's first applied claim within the robot's half-second: it must
        # count (the robot has 2 health and falls).
        lines = host.splitlines()
        hit_ms, ms = None, None
        first = None
        for l in lines:
            m = SENT_ANIM.search(l)
            if m:
                ms = int(m.group(1))
                if m.group(2) == "126" and m.group(3) == "4e81d69f" and hit_ms is None:
                    hit_ms = ms
                continue
            a = HIT_APPLIED.search(l)
            if a and a.group(1) == "126" and first is None:
                first = (ms, a.group(5) is not None)
        gap = first[0] - hit_ms if (first and hit_ms is not None and first[0] is not None) else None
        on126 = [a for a in applied if a[0] == "126"]
        upto = []
        for a in on126:
            upto.append(a)
            if a[4]:
                break
        row("both players' hits on the robot within its recovery count: it fell to the host's spin and one claim",
            gap is not None and 0 <= gap < 500 and bool(on126) and on126[0][4] is not None and len(upto) == 1 and
            bool(upto[-1][4]),
            "the host's hit at host ms %s, the guest's first applied claim %s ms later (%s); applied before the "
            "robot fell: %d" % (hit_ms, gap, "it fell" if first and first[1] else "it did not fall", len(upto)))
    elif fight == "drive":
        cars = [c for c in claims if c[1] == "9"]
        notsent = re.findall(r"coop: a hit on thing (\d+) \(damage type 21\) is not this player's attack", guest)
        car_applied = [a for a in applied if a[1] == "9"] + [b for b in breaks if b[1] == "9"]
        row("the guest's car sent its hits (attack 9), none dropped as not its own", bool(cars) and not notsent,
            "%d car claims (things %s); %d 'damage type 21 not sent'" % (len(cars), sorted(set(c[0] for c in cars)),
                                                                        len(notsent)))
        last = {}
        for m in SENT_ANIM.finditer(host):
            last[int(m.group(2))] = int(m.group(4))
        hidden = [t for t in range(162, 167) if t in last and not (last[t] & 1)]
        row("the host applied them: the crates the car hit broke", bool(car_applied) and len(hidden) >= 2,
            "%d applied (%d defeated); crates 162-166 hidden on the host at the end: %s; refusals: %s" % (
                len(car_applied), sum(1 for a in car_applied if a[-1]), hidden, why))
    elif fight == "arena":
        pooled = [c for c in claims if int(c[0]) <= 33]
        row("the guest's hits on the arena's spawned enemies were sent and applied", bool(pooled) and bool(applied),
            "%d claims on things 0-33, %d applied; refusals: %s" % (len(pooled), len(applied), why))
        killed = set(a[0] for a in applied if a[4])
        shown = set(re.findall(r"coop: enemy thing (\d+) died on the host", guest))
        row("an enemy the guest defeated died on the host and on the guest's screen", bool(killed & shown),
            "the guest's defeats %s; deaths shown on the guest %s" % (sorted(killed), sorted(shown)[:12]))
        revived = len(re.findall(r"shown by the host while this game's copy is in its dead goal", guest))
        row("the guest's copies the spawner brought back got their collision back", revived >= 1,
            "%d copies" % revived)
        fog = re.findall(r"coop: an enemy hit the guest: .*a breath or a cloud", host)
        took = re.findall(r"coop: the host says an enemy hit this player", guest)
        row("an enemy's breath hurt the guest (the host sent it, the guest took it)", bool(fog) and bool(took),
            "the host sent %d particle hits; the guest took %d hits" % (len(fog), len(took)))
        drawn = sorted(set(re.findall(r"coop: enemy thing (\d+) breathes on the host; its breath is drawn here",
                                      guest)))
        row("the host's foggers' breath was drawn on the guest's screen", bool(drawn), "foggers %s" % drawn)
        row("LB in a one-character arena: said at once, no switch tried",
            "this level has one character only;" in guest and "could not switch to" not in guest and
            "coop: switched to" not in guest, "")
    elif fight == "arena-waves":
        hud_checks(row, host, guest)
    elif fight == "boxes":
        on22 = [a for a in applied if a[0] == "22"]
        row("the crate that fell when the one under it broke (thing 22) was hit and broke",
            bool(on22) and any(a[4] for a in on22),
            "claims on 22: %d; applied on the host: %d; all the guest's claims %s" % (
                sum(1 for c in claims if c[0] == "22"), len(on22), sorted(set(c[0] for c in claims))))
    elif fight == "boxes-resync":
        on = [m.start() for m in re.finditer(r"coop: posing the host's things on at frame", guest)]
        after = guest[on[-1]:] if len(on) >= 2 else ""
        late = [m.groups() for m in CLAIM.finditer(after)]
        whole = len(re.findall(r"a crate this game broke is whole on the host; whole here again", guest))
        row("after the cut, crates the guest broke on its own were whole again, hit, and broke on the host",
            whole >= 1 and bool(late) and any(a[4] for a in applied),
            "%d crates made whole; %d claims after the host's world came back; %d applied (%d broke)" % (
                whole, len(late), len(applied), sum(1 for a in applied if a[4])))
    elif fight == "breakables":
        broke = set(b[0] for b in breaks if b[2])
        here = set(re.findall(r"coop: the host broke thing (\d+); broken here too", guest))
        row("the guest's hits on breakables (barrels) broke them on the host, and the guest saw the host's break",
            bool(broke) and bool(broke & here),
            "%d claims; broke on the host %s; the host's breaks shown on the guest %s; refusals: %s" % (
                len(claims), sorted(broke), sorted(here), why))
    elif fight == "zap":
        notsent = re.findall(r"coop: a hit on thing (\d+) \(damage type 24\) is not this player's attack", guest)
        zaps = [a for a in applied if a[1] == "10"]
        row("the upgraded SpongeBowl's lightning (attack 10) was sent and applied", not notsent and bool(zaps),
            "%d lightning claims; %d applied; %d 'damage type 24 not sent'; refusals: %s" % (
                sum(1 for c in claims if c[1] == "10"), len(zaps), len(notsent), why))
    elif fight == "tokens":
        took = re.findall(r"coop: the guest took pickup (\d+) .*?; counted here \(shiny objects \d+, tokens (\d+)\)",
                          host)
        gone = re.findall(r"coop: the guest's pickup 209 refused: (.*)", host)
        row("token 05, shown by the guest's own game only, was counted on the host at the first ask",
            any(t[0] == "209" for t in took) and not gone, "taken %s; refused %d (%s)" % (took, len(gone),
                                                                                    "; ".join(gone[:2])))
        progs = [m.groups() for m in PROG.finditer(guest)]
        row("the guest shows the host's count: 5 tokens once the host took token 03",
            any(p[0] == "5" for p in progs), "the guest's counts from the host: %s" % sorted(set(p[0] for p in progs)))
        bash = [p for p in progs if p[4].split()[1] == "1"]
        paused = [int(m.group(2)) for m in STATE.finditer(guest) if m.group(1) == "self" and int(m.group(2)) >= 2450
                  and int(m.group(2)) <= 2950 and int(m.group(11)) & 4]
        row("Mindy taught both the Bash: the host's game, and the guest's by the host's save",
            "ps_bash1.bik" in host and bool(bash) and len(paused) <= 3,
            "the host's Bash film: %s; the guest's moves from the host with the Bash: %d; the guest's states paused "
            "(Mindy's 'you still need' pause menu) %d" % ("ps_bash1.bik" in host, len(bash), len(paused)))
        back = re.search(r"coop: this player's own progress is back \(([^)]*)\): tokens (\d+), shiny objects \d+, "
                         r"moves ((?:\d+ ?){8})", guest)
        row("after the session the guest's own progress is back (3 tokens, no Bash) and it saved nothing",
            back is not None and back.group(2) == "3" and back.group(3).split()[1] == "0" and not saves(guest_dir),
            (back.group(0)[len("coop: "):] if back else "never put back") + "; saves: %s" % saves(guest_dir))
    elif fight == "maintoken":
        took = host.find("coop: the guest took pickup 206 ")
        lvl = host.find("going to DE02; the guest is told (EV_LEVEL)")
        row("the guest took the main token; the host's level ended and both went to DE02",
            0 <= took < lvl and "level DE02 shares" in guest and "level DE02 shares" in host,
            "taken at %d, the host's level end at %d; DE02 on the guest: %s" % (took, lvl, "level DE02 shares" in guest))
    elif fight == "given":
        given = re.findall(r"coop: the guest took pickup (\d+) .*; its own game gave it", host)
        row("a token the guest's own script gave was asked of the host and counted there", bool(given),
            "taken: %s; the guest asked: %d" % (given, guest.count("this game's own script gives it")))
        progs = [m.groups() for m in PROG.finditer(guest)]
        counted = re.findall(r"coop: the guest took pickup \d+ .*; counted here \(shiny objects \d+, tokens (\d+)\)", host)
        row("the guest's count is the host's, with that token", bool(counted) and any(p[0] == counted[-1] for p in progs),
            "the host's count %s; the guest's from the host: %s" % (counted[-1:] or "none",
                                                                    sorted(set(p[0] for p in progs))))
    elif fight == "upgrade":
        done = re.search(r"coop: the guest's upgrade of move 0 applied: now level 2", host)
        film = "the host's game gave this player move 0 (upgrade)" in guest
        progs = [m.groups() for m in PROG.finditer(guest)]
        row("the guest's purchase upgraded the host's save; the guest played its film and has it too",
            done is not None and film and any(p[4].split()[0] == "2" for p in progs),
            "host: %s; the guest's film: %s; the guest's moves from the host: %s" % (
                done.group(0)[len("coop: "):] if done else "no upgrade", film, sorted(set(p[4] for p in progs))))
        closed = re.search(r"coop: the guest's upgrade of move 1 refused: (.*)", host)
        row("a purchase with the guest's pause menu closed was refused, and the guest acted on no answer for it",
            closed is not None and closed.group(1).startswith("the guest's pause menu is not open") and
            "gave this player move 1" not in guest,
            closed.group(0)[len("coop: "):] if closed else "not refused")
    elif fight == "own-level":
        back = re.search(r"coop: this player's own progress is back \(the host is in another level\): tokens (\d+), "
                         r"shiny objects (\d+)", guest)
        after = guest[back.end():] if back else ""
        row("with the host in another level, the guest's own progress came back and the host's was not shown again",
            back is not None and "coop: progress from the host" not in after,
            back.group(0)[len("coop: "):] if back else "never put back")
        saved = re.search(r"coop: this game may autosave: its own progress, tokens (\d+), shiny objects (\d+)", after)
        row("the guest took shiny objects in its own level; after the session its autosave had them",
            back is not None and saved is not None and int(saved.group(2)) > int(back.group(2)) and
            bool(saves(guest_dir)),
            "shiny objects %s when the host had gone, %s at the autosave; saves: %s" % (
                back.group(2) if back else "?", saved.group(2) if saved else "?", saves(guest_dir)))
    elif fight == "maintoken-death":
        took = host.find("coop: the guest took pickup 206 ")
        died = host.find("the player is defeated (TSSM_TEST_HEALTH)")
        done = host.find("coop: the guest's token: its reward is over")
        lvl = host.find("going to DE02; the guest is told (EV_LEVEL)")
        row("the host was defeated during the guest's token reward; once it played again its level still ended",
            0 <= took < died < done < lvl and "level DE02 shares" in guest and "level DE02 shares" in host,
            "taken at %d, defeated at %d, the token's signal at %d, the level end at %d" % (took, died, done, lvl))
        gave = guest.find("coop: the host gave this player pickup 206 ")
        went = guest.find("coop: the host is going to DE02 (EV_LEVEL)")
        shots = re.findall(r"capture: frame \d+ -> (.*?sync_\d+\.png)", guest[gave:went]) if 0 <= gave < went else []
        lum = [frame_luma(s.strip()) for s in shots]
        # A fade of its own level end may be caught once (a capture a second);
        # never two in a row.
        run_dark, longest = 0, 0
        for v in lum:
            run_dark = run_dark + 1 if v is not None and v < 12.0 else 0
            longest = max(longest, run_dark)
        refused = "a portal to DE02 was not taken" in guest
        up = re.search(r"coop: the screen this exit faded down comes up again \(its own wait was ([0-9.]+) s\)", guest)
        row("the guest's screen was not left dark while the host's level end waited",
            len(lum) >= 4 and longest <= 1 and None not in lum and (up is not None or not refused),
            "its own exit %s; " % ("refused, the screen up again after its fade (%s s wait cut short)" % up.group(1)
                                   if up else ("refused, the screen left as it was" if refused else "not tried")) +
            "%d frames between the token and the host's level end; brightness %s" % (
                len(lum), [round(v, 1) if v is not None else None for v in lum][:16]))
    elif fight == "maintoken-stop":
        took = host.find("coop: the guest took pickup 206 ")
        stop = host.find("coop tab (test): Stop pressed")
        done = host.find("coop: the guest's token: its reward is over")
        # The host's clock: its last sent world before the take and the stop.
        def sent_ms(upto):
            ms = re.findall(r"coop: world sent ms (\d+)", host[:upto]) if upto >= 0 else []
            return int(ms[-1]) if ms else None
        took_ms, stop_ms = sent_ms(took), sent_ms(stop)
        m = re.search(r"coop: the guest's token: its reward is over.*\(clock ms (\d+)\)", host)
        done_ms = int(m.group(1)) if m else None
        row("the host stopped hosting during the guest's token reward; the token's level signal still fired, "
            "with co-op off, once the reward was over",
            0 <= took < stop < done and None not in (took_ms, stop_ms, done_ms) and stop_ms < took_ms + 5000 and
            done_ms >= took_ms + 5000 and "the guest's token's level signal was still due" not in host,
            "taken at host ms %s, stopped at %s, the token's signal at %s" % (took_ms, stop_ms, done_ms))
    elif fight == "keep-reset":
        kept = [m.group(1) for m in re.finditer(r"coop: this level as this game has it kept aside \(\d+ pickups, "
                                                 r"save block \d+ bytes, hash ([0-9a-f]+)\)", guest)]
        reset = guest.find("(TSSM_TEST_RESET)")
        put = re.search(r"coop: this level's save block is as this game had it before the host's world came \(hash "
                        r"([0-9a-f]+); written since: ([0-9a-f]+)\)", guest)
        took = re.findall(r"coop: the guest took pickup (\d+) ", host)
        row("the guest took pickups (the host's save counted them), then its level was reset in the shared level",
            bool(took) and reset >= 0 and bool(kept) and guest.find("kept aside") < reset,
            "the host counted %d; reset at %d" % (len(took), reset))
        row("leaving the level, the guest's save block was put back as its own game had it (the reset had "
            "written it)", put is not None and put.group(1) == kept[0] and put.group(2) != put.group(1) and
            put.start() > reset,
            put.group(0)[len("coop: "):] if put else "not put back")
        row("the guest followed the host to DE02 with its own progress back",
            "coop: following the host to DE02" in guest and
            "coop: this player's own progress is back (leaving the host's level)" in guest, "")


HUD_TABLE = re.compile(r"coop: hud: the level's texts show (\d+) counters; their logic writes (\d+) text boxes "
                       r"\(table ([0-9a-f]+)\)")
HUD_COUNTER_ID = re.compile(r"coop: hud: counter (\d+) is ([0-9a-f]{8}), from (-?\d+)")
HUD_COUNTER = re.compile(r"coop: hud: counter (\d+) is (-?\d+)(?: \((the host's|this game's own)\))? "
                         r"\(clock ms (\d+)\)")
HUD_BOX = re.compile(r"coop: hud: text box (\d+) (?:shows text ([0-9a-f]{8})|is (hidden))"
                     r"(?: \((the host's|this game's own)\))? \(clock ms (\d+)\)")
HUD_HELD = re.compile(r"coop: hud: this game's own event (\d+) to counter (\d+) held")
HUD_MIRROR = re.compile(r"coop: hud: the host's counters and challenge texts are shown here; .* \(clock ms (\d+)\)")
# AM01's texts (am01_US.hip): the end of each wave, the countdown, the end.
ARENA_TEXTS = {"b9ae780a": "Wave 1 Complete!", "cb3c2e5b": "Wave 2 Complete!",
               "dcc9e4ac": "Wave 3 Complete!", "78a4e15c": "You Win!", "fff55269": "Wave 2 of 3",
               "fff5526a": "Wave 3 of 3", "1c6f0b9c": "Are Ya Ready Kids?", "1c6f0b9d": "Set", "1c6f0b9e": "Go!"}


def lasting(changes, key, until, least):
    """The changes (key first, clock ms last) that held for `least` ms before the next of their key."""
    out = []
    for k, c in enumerate(changes):
        nxt = next((d[-1] for d in changes[k + 1:] if key(d) == key(c)), until)
        if nxt - c[-1] >= least:
            out.append(c)
    return out


def hud_checks(row, host, guest, within=500):
    """The arena's counters and challenge texts on both copies (docs/MULTIPLAYER.md 13).

    Compared from the moment the guest shows the host's (its first state
    after both are in the level); a count the host had for less than 100 ms
    (several enemies down at once) may go by between two of its states."""
    ht, gt = HUD_TABLE.search(host), HUD_TABLE.search(guest)
    row("both made the same table of the level's counters and challenge text boxes",
        ht is not None and gt is not None and ht.groups() == gt.groups() and int(ht.group(1)) >= 1,
        "host %s, guest %s" % (ht.groups() if ht else None, gt.groups() if gt else None))
    names = {m.group(1): m.group(2) for m in HUD_COUNTER_ID.finditer(host)}
    # From the guest's start of showing the host's; without one, from its
    # first line (its own state as it connected).
    mirror = HUD_MIRROR.search(guest)
    first = re.search(r"coop: hud: (?:counter|text box) \d+ .*\(clock ms (\d+)\)", guest)
    since = int(mirror.group(1)) if mirror else (int(first.group(1)) if first else None)
    hc_all = [(m.group(1), int(m.group(2)), int(m.group(4))) for m in HUD_COUNTER.finditer(host)]
    gc = [(m.group(1), int(m.group(2)), m.group(3), int(m.group(4))) for m in HUD_COUNTER.finditer(guest)]
    end = max([c[-1] for c in hc_all] or [0]) + 10 ** 6
    hc = [c for c in lasting(hc_all, lambda c: c[0], end, 100) if since is not None and c[2] >= since]
    # Each of the host's counts on the guest, and how much later (one PC: one clock).
    late, missing = [], []
    for i, v, t in hc:
        g = next((x for x in gc if x[0] == i and x[1] == v and x[3] >= t - 50), None)
        if g is None:
            missing.append((names.get(i, i), v))
        else:
            late.append(g[3] - t)
    wave1 = [v for i, v, t in hc if names.get(i) == "ceaf65a1"]
    row("\"N left\": every count of the host's counters showed on the guest, within %d ms" % within,
        bool(hc) and not missing and max(late or [within + 1]) <= within and len(wave1) >= 3,
        "shown from clock ms %s; %d of the host's %d counts held 100 ms (wave 1's: %s), %d missing (%s), at "
        "most %s ms later" % (
            since, len(hc), len(hc_all), wave1 if len(wave1) <= 8 else wave1[:6] + ["..."] + wave1[-2:],
            len(missing), missing[:4], max(late) if late else None))
    last_h, last_g = {}, {}
    for i, v, t in hc_all:
        last_h[i] = v
    for i, v, why, t in gc:
        last_g[i] = v
    row("the counters ended the same on both", bool(last_h) and all(last_g.get(i) == v for i, v in last_h.items()),
        "host %s, guest %s" % (sorted(last_h.items()), sorted(last_g.items())))
    hb = [(m.group(1), m.group(2) or "hidden", int(m.group(5))) for m in HUD_BOX.finditer(host)]
    hb = [b for b in hb if since is not None and b[2] >= since]
    gb = [(m.group(1), m.group(2) or "hidden", m.group(4), int(m.group(5))) for m in HUD_BOX.finditer(guest)]
    seen, lost, delays = [], [], []
    for b, what, t in hb:
        g = next((x for x in gb if x[0] == b and x[1] == what and x[3] >= t - 50), None)
        if what in ARENA_TEXTS:
            seen.append(ARENA_TEXTS[what])
        if g is None:
            lost.append((b, ARENA_TEXTS.get(what, what)))
        else:
            delays.append(g[3] - t)
    want = ("Wave 1 Complete!", "Wave 2 Complete!", "Wave 3 Complete!", "You Win!", "Go!")
    row("the wave texts showed and went on the guest when the host's did, within %d ms" % within,
        all(w in seen for w in want) and not lost and max(delays or [within + 1]) <= within,
        "the host's texts %s; %d changes, %d not on the guest (%s), at most %s ms later" % (
            sorted(set(seen)), len(hb), len(lost), lost[:4], max(delays) if delays else None))
    held = [m.groups() for m in HUD_HELD.finditer(guest)]
    own = [x for x in gb if x[2] == "this game's own" and x[1] in ARENA_TEXTS]
    first_h = next((t for b, what, t in hb if what == "b9ae780a"), None)
    first_g = next((x[3] for x in gb if x[1] == "b9ae780a"), None)
    row("the guest's own level ran no wave: its decrement held, no wave text of its own",
        any(e == "12" and names.get(c) == "ceaf65a1" for e, c in held) and not own and first_h is not None and
        first_g is not None and first_g >= first_h - 50,
        "held %s; own wave texts %s; \"Wave 1 Complete!\" on the host at %s, on the guest at %s" % (
            held[:3], own[:2], first_h, first_g))


def blackout_checks(row, seconds, host, guest, s0, s1, per, guest_dir):
    """M5: a cut connection (docs/MULTIPLAYER.md 8.8): under the session
    timeout the session rides it out; past it both time out and the guest
    joins again by itself, into the same level."""
    rejoin = seconds >= SESSION_TIMEOUT_S
    row("the guest's connection was cut for %g s" % seconds,
        s0 is not None and "test: blackout: nothing goes out or comes in" in guest and
        "test: blackout over" in guest, "from clock ms %s" % s0)
    row("both noticed the silence (\"the connection is interrupted\")",
        "the connection is interrupted" in host and "the connection is interrupted" in guest, "")
    hj, gj = host.count("coop: connected (hosting)"), guest.count("coop: connected (joined)")
    if not rejoin:
        row("the session rode it out (one join each, no disconnect, no timeout)",
            hj == 1 and gj == 1 and "coop: disconnected" not in guest.split("TSSM_EXIT_AFTER reached")[0] and
            "timed out" not in host + guest, "joins %d/%d" % (hj, gj))
        row("both heard the other again", "the other player is heard again" in host and
            "the other player is heard again" in guest, "")
    else:
        back = REJOINED.search(guest)
        row("both timed out (the guest's game: \"the connection was lost; joining again\")",
            "coop: the player timed out" in host and "coop: the host timed out" in guest and
            "the connection was lost; joining again" in guest, "")
        took = int(back.group(1)) if back else None
        row("the guest joined again by itself once the cut was over",
            back is not None and hj == 2 and gj == 2 and took <= (seconds - SESSION_TIMEOUT_S) * 1000 + 2500,
            "joins %d/%d; %s ms after the session was lost" % (hj, gj, took))
        rejoined_at = guest.find("coop: reconnected")
        rest = guest[rejoined_at:] if rejoined_at >= 0 else ""
        row("back in the same level: no following, no placing, the host's world shown again",
            rejoined_at >= 0 and "coop: following the host" not in guest and
            "arrived in the host's level; placed" not in rest and "coop: the host's world is shown" in rest, "")
        # The save (7.6): asked for while joining again, at least once, and
        # never written.
        lost_at = guest.find("the connection was lost; joining again")
        between = guest[lost_at:rejoined_at] if 0 <= lost_at < rejoined_at else ""
        gsaves = saves(guest_dir)
        row("the guest's game did not autosave while joining again",
            "autosave asked" in between and not gsaves and
            guest.count("coop: autosave skipped") == guest.count("autosave asked"),
            "asked %d times in all, %d of them while joining again; skipped %d; guest saves: %s" %
            (guest.count("autosave asked"), between.count("autosave asked"),
             guest.count("coop: autosave skipped"), gsaves))
    after = sorted(sid for sid in per if s1 is not None and sid >= s1)
    bad = [sid for sid in after if per[sid] > 0.25]
    ok_from = (bad[-1] + 1) if bad else (after[0] if after else None)
    later = [sid for sid in after if ok_from is not None and sid >= ok_from]
    took = (ok_from - s1) if (ok_from is not None and s1 is not None) else None
    limit = (3000 if rejoin else 1000) + STALL_SYNC_MS
    row("both show the same places again within %d s after it" % (limit // 1000),
        took is not None and took <= limit and len(later) >= 8,
        "in step from %s ms after it (%d later moments all within 0.25)" % (took, len(later)))
    deaths = re.findall(r"coop: enemy thing (\d+) died on the host", guest)
    dup = sorted(set(d for d in deaths if deaths.count(d) > 1))
    row("nothing happened twice (death effects)", not dup, "death effects %s" % deaths)


def stall_checks(row, seconds, side, host, guest, s0, s1, per, drag=False, one_char=False):
    """The stall variants' own checks (docs/MULTIPLAYER.md 5.11)."""
    other_log, stalled_log = (guest, host) if side == "host" else (host, guest)
    other_name = "guest" if side == "host" else "host"
    who = "host" if side == "host" else "guest"
    if drag:
        # Held by Windows' move loop: the game's own word for how long it ran
        # no frame (iCoopPuppet.cpp, at its first frame after).
        ran = [int(m) for m in re.findall(r"coop: this game ran no frame for (\d+) ms \(a stall\)", stalled_log)]
        longest = max(ran or [0])
        row("the %s's window was held for %g s, and its game thread stopped all that time" % (who, seconds),
            s0 is not None and s1 is not None and longest >= seconds * 1000 - 100,
            "held from clock ms %s to %s; the game's longest stop %d ms" % (s0, s1, longest))
    else:
        row("the %s's game thread stopped for %g s" % (who, seconds),
            s0 is not None and s1 is not None and s1 - s0 >= seconds * 1000 - 20,
            "clock ms %s to %s" % (s0, s1))
    limit = seconds > STALL_LIMIT_S
    gcut = guest.find("TSSM_EXIT_AFTER reached")
    hcut = host.find("coop: disconnected")
    timeouts = [l for l in (host + guest).splitlines() if "timed out" in l or "connection was lost" in l]
    if limit:
        # Past the limit: the stalled game's I/O thread closes the session
        # itself (DC_STALLED) and both say why; nobody times out.
        row("past the one-minute limit the stalled game closed the session (DC_STALLED)",
            "the session is closed (DC_STALLED)" in stalled_log and
            "coop: disconnected: this game stopped for over a minute" in stalled_log, "")
        row("the %s was told why (its game stopped for over a minute), not a timeout" % other_name,
            "coop: disconnected: the other player's game stopped for over a minute" in other_log and not timeouts,
            timeouts[0].strip() if timeouts else "")
        first = [re.search(r"coop: disconnected(.*)", l) for l in (host, guest)]
        row("the session stayed up until the limit (one join each, and that was the first disconnect)",
            guest.count("coop: connected (joined)") == 1 and host.count("coop: connected (hosting)") == 1 and
            all(m is not None and "over a minute" in m.group(1) for m in first),
            "joins %d/%d; first disconnects: %s" % (host.count("coop: connected (hosting)"),
                                                    guest.count("coop: connected (joined)"),
                                                    [m.group(1) if m else None for m in first]))
    else:
        # The session stayed up: one connection each, no disconnect before the
        # guest's own exit, no timeout.
        row("the session stayed up through the stall (one join, no disconnect, no timeout)",
            guest.count("coop: connected (joined)") == 1 and host.count("coop: connected (hosting)") == 1 and
            "coop: disconnected" not in guest[:gcut if gcut >= 0 else None] and
            (hcut < 0 or "TSSM_EXIT_AFTER" not in host[:hcut]) and "DC_STALLED" not in host + guest and
            not timeouts,
            "joins %d/%d" % (host.count("coop: connected (hosting)"), guest.count("coop: connected (joined)")))
    if seconds >= 0.5:
        row("the stalled game told the other (STALL)%s" % ("" if limit else ", and said when it ran again"),
            "telling the other player (STALL)" in stalled_log and "coop: this game ran no frame for" in stalled_log
            and (limit or "coop: this game runs again" in stalled_log), "")
    if seconds >= 2.5:
        row("the %s waited for it%s" % (other_name, "" if limit else ", and saw it run again"),
            "waiting for it" in other_log and (limit or "the partner's game runs again" in other_log), "")
    if not limit:
        # Back in step: the first same-moment capture after the stall from
        # which every later one has each thing within 0.25 units.
        after = sorted(sid for sid in per if s1 is not None and sid >= s1)
        bad = [sid for sid in after if per[sid] > 0.25]
        ok_from = (bad[-1] + 1) if bad else (after[0] if after else None)
        later = [sid for sid in after if ok_from is not None and sid >= ok_from]
        took = (ok_from - s1) if (ok_from is not None and s1 is not None) else None
        row("both show the same places again within about a second after the stall",
            took is not None and took <= 1000 + STALL_SYNC_MS and len(later) >= 8,
            "in step from %s ms after it (%d later moments all within 0.25; worst in the first second: %s)" % (
                took, len(later), "%.3f" % max([per[s] for s in after if s < s1 + 1000] or [0.0])))
    # Nothing twice.
    switches = len(re.findall(r"coop: switched to", guest))
    shown = len(re.findall(r"coop: the host's world is shown", guest))
    deaths = re.findall(r"coop: enemy thing (\d+) died on the host", guest)
    dup = sorted(set(d for d in deaths if deaths.count(d) > 1))
    row("nothing happened twice (character switch, world shown, death effects)",
        switches == (0 if one_char else 1) and shown == 1 and not dup,
        "switches %d, world shown %d, death effects %s" % (switches, shown, deaths))
    gaps = re.findall(r"coop: the host's snapshots go on after a gap of (\d+) ms \(([^)]*)\)", guest)
    print("snapshot gaps on the guest: %s" % gaps[:6])


# M3's fights (docs/MULTIPLAYER.md 6.7), in DE01 by the robot (a slammer, thing
# 126). combat: the host comes to watch from 10 units off while the robot
# goes for the guest and hits it; from frame 1450 the guest, put a step from it
# and facing it every 30 frames (TSSM_TEST_FACE), spins (X) until it is
# defeated. respawn: the guest has one health left, the robot
# defeats it, and it must come back beside the host (at the level's start, far
# off) without anything being reset. host-death: the host stands by the robot
# with one health left; its game must keep its own rules (a lost chance, its
# level reset) and the guest is put beside it when it plays again.
# bowl: in TT01, where the level starts as Patrick, the guest is SpongeBob;
# from frame 1300 it is put 7 units from a spinning robot (thing 388), facing
# it, every 400 frames, and bowls (B) at it in between: a hit from afar.
def look_checks(row, host, guest, host_dir, guest_dir):
    """M4 (docs/MULTIPLAYER.md 7.8): the characters in BB02, each switched with LB."""
    def order(log, *texts):
        at = 0
        for t in texts:
            at = log.find(t, at)
            if at < 0:
                return False
        return True

    row("the guest took Patrick by itself, and each draws the other so",
        "coop: switched to Patrick (a guest takes the other character than the host's)" in guest and
        "the partner is drawn as Patrick (model 11)" in host and "the partner is drawn as SpongeBob (model 0)" in guest)
    row("the guest's LB: SpongeBob, and the host draws a second SpongeBob",
        order(guest, "coop: LB tapped: to SpongeBob", "coop: switched to SpongeBob (asked for)") and
        order(host, "the partner is drawn as Patrick (model 11)", "the partner is drawn as SpongeBob (model 0)"))
    row("the host's LB: Patrick, and the guest draws it so",
        order(host, "coop: LB tapped: to Patrick", "coop: switched to Patrick (asked for)") and
        order(guest, "the partner is drawn as SpongeBob (model 0)", "the partner is drawn as Patrick (model 11)"))
    row("the guest's LB again: Patrick, so both are Patrick",
        order(guest, "coop: switched to SpongeBob (asked for)", "coop: LB tapped: to Patrick",
              "coop: switched to Patrick (asked for)") and
        order(host, "the partner is drawn as SpongeBob (model 0)", "the partner is drawn as Patrick (model 11)"))
    for role, d, want in (("host", host_dir, "patrick"), ("guest", guest_dir, "patrick")):
        cfg = open(os.path.join(d, "config.ini"), "rb").read().decode("latin-1")
        m = re.search(r"^\s*character\s*=\s*(\w+)", cfg, re.M)
        row("%s's choice kept in config.ini ([coop] character = %s)" % (role, want), m is not None and m.group(1) == want,
            m.group(0).strip() if m else "no character line")
    row("the guest's LB hold with no costume unlocked changed nothing and wrote nothing",
        "coop: LB held: no costume of Patrick is unlocked in this save; nothing changed" in guest and
        "costume set to" not in guest and config_value(guest_dir, "costume_patrick") in (None, "game"),
        "costume_patrick = %s" % config_value(guest_dir, "costume_patrick"))


def coop_config(d):
    """[coop] of a copy's config.ini, as key -> value."""
    out, section = {}, None
    for line in open(os.path.join(d, "config.ini"), "rb").read().decode("latin-1").splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip().lower()
        elif section == "coop" and "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def tab_checks(row, host, guest, host_dir, guest_dir, port, tab_secrets):
    """M5 fix round: the Co-op tab's own code (TSSM_TEST_COOP_TAB)."""
    row("the host pressed Host in the tab and hosted",
        re.search(r"coop tab \(test\): Host pressed\s*$", host, re.M) is not None and
        "coop: hosting on UDP port %d" % port in host)
    refused = guest.find("coop: the join was refused: wrong join code or password")
    again = guest.find("coop tab (test): Try again pressed")
    joined = guest.find("coop: connected (joined)")
    row("the guest's Join with a wrong password was refused", "coop tab (test): Join pressed" in guest and refused >= 0)
    row("the tab showed Try again after it, and pressing it joined", 0 <= refused < again < joined,
        "refused at %d, Try again at %d, joined at %d" % (refused, again, joined))
    hv = re.findall(r"coop: co-op version (\w+)", host)
    gv = re.findall(r"coop: co-op version (\w+)", guest)
    row("both logged the same co-op version (the tab's last line)", bool(hv) and bool(gv) and set(hv) == set(gv),
        "host %s, guest %s" % (hv[:1], gv[:2]))
    row("the partners know each other by the typed names",
        "coop: the other player's name: TabGuest" in host and "coop: the other player's name: TabHost" in guest)
    hc, gc = coop_config(host_dir), coop_config(guest_dir)
    row("the host's config.ini keeps its name, port and switches",
        hc.get("name") == "TabHost" and hc.get("port") == str(port) and hc.get("nametag") == "off" and
        hc.get("quality") == "off", str(hc))
    row("the guest's config.ini keeps its name, address and switches",
        gc.get("name") == "TabGuest" and gc.get("address") == "127.0.0.1:%d" % port and
        all(gc.get(k) == "off" for k in ("nametag", "follow", "reconnect", "quality")), str(gc))
    texts = [open(os.path.join(d, "config.ini"), "rb").read().decode("latin-1") for d in (host_dir, guest_dir)]
    row("no password in either config.ini or log", not any(sec in t for sec in tab_secrets for t in texts + [host, guest]))
    row("the switches were pressed while connected (after the join, before the exit)",
        0 <= host.find("coop: connected (hosting)") < host.find("coop tab (test): nametag switched off") and
        0 <= joined < guest.find("coop tab (test): reconnect switched off"))


def lan_probe(port, guest_log, out, deadline):
    """The lan variant's hostile cases against the host's answers on
    127.0.0.1:port, once the guest has joined from its list (so the guest's
    own questions are done): from 127.0.0.2 to 127.0.0.5 only."""
    import socket
    while time.time() < deadline:
        try:
            if "coop: connected (joined)" in open(guest_log, "rb").read().decode("utf-8", "replace"):
                break
        except OSError:
            pass
        time.sleep(0.2)
    else:
        out["error"] = "the guest never joined"
        return
    time.sleep(1.0)
    target = ("127.0.0.1", port)

    def sock(ip, p=0):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.bind((ip, p))
        return s

    def question(nonce, version=1, flags=0):
        q = b"TSDQ" + bytes([version, flags]) + nonce
        return q + bytes(128 - len(q))

    def replies(s, wait):
        got, end = [], time.time() + wait
        while time.time() < end:
            s.settimeout(max(0.01, end - time.time()))
            try:
                got.append(s.recvfrom(2048)[0])
            except socket.timeout:
                break
            except OSError:
                break
        return got

    try:
        s = sock("127.0.0.2")
        nonce = os.urandom(8)
        out["nonce"] = nonce
        s.sendto(question(nonce), target)
        out["good"] = replies(s, 0.5)
        newer = bytearray(question(nonce, 2, 0x81))
        newer[100] = 5
        s.sendto(bytes(newer), target)
        out["newer"] = replies(s, 0.5)
        s.close()
        good = question(os.urandom(8))
        bad = {
            "127 bytes": good[:127],
            "129 bytes": good + b"\0",
            "1 byte": b"T",
            "1200 bytes (a HELLO's size)": b"TSCP" + bytes(1196),
            "wrong magic": b"TSDX" + good[4:],
            "version 0": good[:4] + b"\0" + good[5:],
            "version 1 with flags": good[:5] + b"\x01" + good[6:],
            "version 1 with padding": good[:100] + b"\x01" + good[101:],
            "an answer sent to the host": b"TSDR\x01\x08\x00" + bytes(121),
        }
        out["hostile"] = {}
        s = sock("127.0.0.3")
        for what, d in bad.items():
            s.sendto(d, target)
            out["hostile"][what] = len(replies(s, 0.3))
        s.close()
        try:
            low = sock("127.0.0.5", 1000)
        except OSError as e:
            out["low"] = "port 1000 could not be bound here (%s)" % e
        else:
            low.sendto(question(os.urandom(8)), target)
            out["low"] = len(replies(low, 0.5))
            low.close()
        s = sock("127.0.0.4")
        t0 = time.time()
        for _ in range(100):
            s.sendto(question(os.urandom(8)), target)
            time.sleep(0.01)
        spent = time.time() - t0
        got = replies(s, 0.6)
        out["flood"] = (len(got), spent, max([len(x) for x in got] or [0]))
        s.close()
    except OSError as e:
        out["error"] = "the prober's socket failed: %s" % e


def lan_silence(port, host_log, out, deadline):
    """Local co-op: questions to Player 1's discovery test port while it
    waits for Player 2 and once Player 2 has joined; any answer is counted."""
    import socket
    out["asked"] = out["answers"] = 0

    def ask(n):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.bind(("127.0.0.2", 0))
        for _ in range(n):
            q = b"TSDQ\x01\x00" + os.urandom(8)
            s.sendto(q + bytes(128 - len(q)), ("127.0.0.1", port))
            out["asked"] += 1
            end = time.time() + 0.4
            while time.time() < end:
                s.settimeout(max(0.01, end - time.time()))
                try:
                    s.recvfrom(2048)
                    out["answers"] += 1
                except socket.timeout:
                    break
                except OSError:
                    # Nothing listening (port unreachable): no answer.
                    break
        s.close()

    try:
        for marker, n in (("coop: hosting on UDP port", 3), ("coop: connected (hosting)", 3)):
            while time.time() < deadline:
                try:
                    if marker in open(host_log, "rb").read().decode("utf-8", "replace"):
                        break
                except OSError:
                    pass
                time.sleep(0.2)
            else:
                return
            time.sleep(1.0)
            ask(n)
    except OSError as e:
        out["error"] = "the prober's socket failed: %s" % e


def lan_offer(d):
    """A version 1 OFFER as a dict, or None (iNetDisc.h's layout, read here
    on its own as a check of the C++ side)."""
    if len(d) < 45 or len(d) > 60 or d[:4] != b"TSDR" or d[4] != 1 or 44 + d[43] != len(d):
        return None
    return {"proto": d[5] | d[6] << 8, "nonce": d[7:15], "build": d[15:23], "files": d[23:31],
            "port": d[31] | d[32] << 8, "instance": int.from_bytes(d[33:37], "little"), "state": d[37],
            "platform": d[38], "scene": d[39:43], "name": d[44:].decode("ascii", "replace")}


LAN_COUNTERS = re.compile(r"coop: lan: questions answered (\d+); dropped: size (\d+), malformed (\d+), source (\d+), "
                          r"rate (\d+)")


LISTED = re.compile(r"coop tab \(test\): the list shows (\d+) games?")


def shots(d, names):
    """The test shots (shot=, hudshot=) missing from a copy's frames folder."""
    have = os.listdir(os.path.join(d, "frames"))
    return [n for n in names if n + ".png" not in have]


def lan_tab_rows(row, who, log, lan_port, again=False):
    """The tab's own search when it opened, on loopback only."""
    row("%s: the tab searched by itself when it opened%s" % (who, ", and again when co-op stopped" if again else ""),
        "coop tab: searching for games on this network (the tab opened)" in log and
        (not again or "coop tab: searching for games on this network (co-op stopped)" in log) and
        "coop: lan: searching for games (test: 1 loopback addresses on port %d" % lan_port in log and
        "coop: lan: searching for games on this network" not in log)


def lan_checks(row, host, guest, host_dir, guest_dir, port, lan_port, tab_secrets, probed):
    """The home network's list and tab, through the loopback test hooks (docs/MULTIPLAYER.md 14.2, 14.3)."""
    row("the host pressed Host on this network and hosts",
        re.search(r"coop tab \(test\): Host on this network pressed\s*$", host, re.M) is not None and
        "coop: hosting on UDP port %d" % port in host)
    shown = host.find("coop hud: the join code is shown")
    joined_h = host.find("coop: connected (hosting)")
    row("the host's HUD showed the join code while waiting, and hid it once the guest joined",
        0 <= shown < joined_h and host.find("coop hud: the join code is hidden", shown) >= 0, "")
    lan_tab_rows(row, "guest", guest, lan_port, again=True)
    row("the host answers on the test port only (127.0.0.1)",
        "coop: lan: visible on this network: answering on UDP port %d (test: 127.0.0.1 only)" % lan_port in host)
    row("the guest asked 127.0.0.1 only, never a broadcast",
        "coop: lan: searching for games (test: 1 loopback addresses on port %d" % lan_port in guest and
        "coop: lan: searching for games on this network" not in guest)
    found = re.findall(r"coop: lan: found a game: (.*)", guest)
    row("the guest's finder listed the host: its name, Windows, waiting, the game port, ready",
        "Host (Windows, waiting), game port %d: ready" % port in found, "; ".join(found[:3]))
    vp = guest.find("coop tab (test): a virtual controller plugged in, 10 presses")
    real = guest.find("coop tab: picked Host's game in the list", max(vp, 0))
    entry = guest.find("coop tab: the code entry for Host's game, with the code pad", max(real, 0))
    row("a controller picked the row with A, and Start opened the code entry with its pad (no test shortcut)",
        0 <= vp < real < entry and entry < guest.find("coop tab (test): Join (list) pressed", max(entry, 0)),
        "plugged in at %d, picked at %d, entry at %d" % (vp, real, entry))
    picks = re.findall(r"coop tab \(test\): picked (.*)", guest)
    row("the second time, the test action picked it, with the host's level", len(picks) == 1 and
        picks[0].startswith("Host (Windows, ") and ", not in a level," not in picks[0], "; ".join(picks))
    row("a controller typed on the code pad: A on 0, right, A on 1, down, A on 9, B deleted it (2 left)",
        vp >= 0 and "presses are done; the code reads 2 characters" in guest[vp:], "")
    closed = guest.find("coop tab: no longer shown; no more searching")
    row("closing the overlay ended the search; opening it again searched again",
        0 <= closed < guest.find("searching for games on this network (the tab opened)", closed), "at %d" % closed)
    e2 = guest.find("coop tab (test): Join (list) pressed")
    off = guest.find("coop tab (test): search stopped", max(e2, 0))
    j2 = guest.find("coop tab: joining Host's game from the list", max(off, 0))
    row("with the search stopped and the list empty, the open code entry still joined",
        0 <= e2 < off < j2 < guest.find("coop: connected (joined)", max(j2, 0)) and
        "that game is no longer in the list" not in guest, "entry %d, stopped %d, join %d" % (e2, off, j2))
    row("the code pad's keys typed the rest (10, then 12)",
        "coop tab (test): 10 code pad keys pressed" in guest and "coop tab (test): 12 code pad keys pressed" in guest)
    st = guest.find("coop tab (test): a virtual controller plugged in, 1 presses")
    jn = guest.find("coop tab: joining Host's game from the list", max(st, 0))
    row("the controller's Start on the pad joined with the right code",
        0 <= st < jn < guest.find("coop: connected (joined)", max(jn, 0)), "start at %d, join at %d" % (st, jn))
    miss = shots(guest_dir, ("lan_list", "lan_pad", "lan_refused", "lan_search_over")) + \
        shots(host_dir, ("lan_host", "lan_hud_code"))
    row("the tab's pictures captured (list, code pad, refusal; host's tab and HUD code)", not miss,
        "missing %s" % miss if miss else "")
    refused = guest.find("coop: the join was refused: wrong join code or password")
    joined = guest.find("coop: connected (joined)")
    kept = [m.start() for m in re.finditer(r"coop: the host's address is kept for the next time", guest)]
    again = guest.find("coop: lan: searching for games", max(refused, 0))
    row("the first join from the list, with a wrong code, was refused; a new search, then the right code joined",
        0 <= refused < again < joined, "refused at %d, searched at %d, joined at %d" % (refused, again, joined))
    row("the search stopped when the first join started (the second had none running)",
        guest.count("coop: lan: search stopped (joining)") == 1, "%d" % guest.count("search stopped (joining)"))
    row("the address was kept once, only after the session was up (not after the refused join)",
        len(kept) == 1 and kept[0] > joined >= 0, "kept at %s, joined at %d" % (kept, joined))
    cfg = coop_config(guest_dir)
    row("the guest's config.ini has the joined address, as numbers", cfg.get("address") == "127.0.0.1:%d" % port,
        str(cfg))
    texts = [open(os.path.join(d, "config.ini"), "rb").read().decode("latin-1") for d in (host_dir, guest_dir)]
    row("no code in either config.ini or log",
        not any(sec in t for sec in tab_secrets for t in texts + [host, guest]))
    row("the host knows the guest by its typed name", "coop: the other player's name: LanGuest" in host)
    if "error" in probed or "flood" not in probed:
        row("the prober ran", False, probed.get("error", "it did not finish before the copies exited"))
        return
    hv = re.findall(r"coop: co-op version (\w+)", host)
    good = probed.get("good", [])
    o = lan_offer(good[0]) if len(good) == 1 else None
    row("a good question: one answer, smaller than the question (%s bytes)" % (len(good[0]) if good else "-"),
        o is not None and len(good[0]) < 128, "%d answers" % len(good))
    if o:
        row("the answer: protocol 8, the question's nonce, the game port, the host's co-op version and name",
            o["proto"] == 8 and o["nonce"] == probed["nonce"] and o["port"] == port and o["instance"] != 0 and
            bool(hv) and o["build"][:4].hex() == hv[0] and o["name"] == "Host" and o["platform"] == 1,
            "proto %d port %d build %s name %s" % (o["proto"], o["port"], o["build"].hex(), o["name"]))
        row("the answer says the game has a player now, and its level", o["state"] == 1 and
            o["scene"][::-1] == b"BB02", "state %d scene %r" % (o["state"], o["scene"]))
    newer = probed.get("newer", [])
    row("a newer version's question gets this version's answer", len(newer) == 1 and lan_offer(newer[0]) is not None,
        "%d answers" % len(newer))
    hostile = probed.get("hostile", {})
    row("silence for every malformed question (%d kinds)" % len(hostile), bool(hostile) and
        not any(hostile.values()), str({k: v for k, v in hostile.items() if v}))
    low = probed.get("low")
    row("silence for a source port below 1024", low == 0 or isinstance(low, str), str(low))
    flood = probed.get("flood", (0, 0, 0))
    row("100 questions from one address in %.1f s: at most 4 at once and 2 a second" % flood[1],
        4 <= flood[0] <= 4 + 2 * flood[1] + 1 and flood[2] < 128, "%d answers, largest %d bytes" % (flood[0], flood[2]))
    c = LAN_COUNTERS.findall(host)
    last = [int(x) for x in c[-1]] if c else None
    row("the host counted the drops (answered, size, malformed, source, rate)",
        last is not None and last[1] >= 4 and last[2] >= 5 and last[4] >= 90 and (last[3] >= 1 or isinstance(low, str)),
        str(last))


def lan_short_checks(row, variant, host, guest, third, host_dir, guest_dir, third_dir, port, lan_port, tab_secrets):
    """The home network's short variants: what the list and the tab say."""
    hv = re.findall(r"coop: co-op version (\w+)", host)
    gv = re.findall(r"coop: co-op version (\w+)", guest)
    texts = [open(os.path.join(d, "config.ini"), "rb").read().decode("latin-1")
             for d in (host_dir, guest_dir, third_dir) if d]
    row("no code in any config.ini or log",
        not any(sec in t for sec in tab_secrets for t in texts + [host, guest, third]))
    if variant in ("lan-version", "lan-files"):
        lan_tab_rows(row, "guest", guest, lan_port)
        tap = guest.find("coop tab (test): a tap on the row of Host")
        row("a tap on the row picked it", 0 <= tap < guest.find("coop tab: picked Host's game in the list", max(tap, 0)),
            "tap at %d" % tap)
        pick = re.search(r"coop tab \(test\): picked (.*)", guest)
        if variant == "lan-version":
            want = "Different version: both need the same co-op version (yours %s, theirs %s)." % (
                gv[0] if gv else "?", hv[0] if hv else "?")
            row("the list says: different version, with both codes", pick is not None and
                "different version" in pick.group(1) and want in pick.group(1) and gv != hv,
                pick.group(1) if pick else "not picked")
            refused = "coop: the join was refused: the host's game is a different version"
        else:
            gf = re.findall(r"coop: game files code (\w+)", guest)
            hf = re.findall(r"coop: game files code (\w+)", host)
            want = "Game files codes: yours %s, theirs %s." % (gf[0] if gf else "?", hf[0] if hf else "?")
            row("the list says: different game files, with both game-files codes", pick is not None and
                "different game files" in pick.group(1) and want in pick.group(1) and gf[:1] != hf[:1],
                pick.group(1) if pick else "not picked")
            refused = "coop: the join was refused: your game files differ"
        row("joining anyway: the handshake refused it", refused in guest and "coop: connected" not in guest)
        miss = shots(guest_dir, (variant.replace("-", "_"), variant.replace("-", "_") + "_refused"))
        row("pictures of the row and the refusal", not miss, "missing %s" % miss if miss else "")
    elif variant == "lan-full":
        row("host and guest in a session", "coop: connected (hosting)" in host and "coop: connected (joined)" in guest)
        lan_tab_rows(row, "third", third, lan_port)
        rows_ = re.findall(r"coop tab \(test\):   (.*)", third)
        row("the third copy's list: Host, has a player, full", "Host, Windows, has a player: full" in rows_,
            "; ".join(rows_[:3]))
        click = third.find("coop tab (test): a click on the row of Host")
        row("a mouse click on the row picked it",
            0 <= click < third.find("coop tab: picked Host's game in the list", max(click, 0)), "click at %d" % click)
        pick = re.search(r"coop tab \(test\): picked (.*)", third)
        row("its tab says the game already has two players", pick is not None and
            "This game already has two players." in pick.group(1), pick.group(1) if pick else "")
        row("joining anyway: refused as full", "coop: the join was refused: the host's game already has a player"
            in third, "")
        row("the session went on (one join each, no drop)", host.count("coop: connected (hosting)") == 1 and
            guest.count("coop: connected (joined)") == 1 and "coop: disconnected" not in guest, "")
        miss = shots(third_dir, ("lan_full", "lan_full_refused"))
        row("pictures of the full row and the refusal", not miss, "missing %s" % miss if miss else "")
    elif variant == "lan-none":
        row("the host's discovery port was taken: its tab says others can't see it, hosting goes on",
            "coop: lan: UDP port %d could not be opened; other devices cannot list this game" % lan_port in host and
            "coop: hosting on UDP port %d" % port in host)
        lan_tab_rows(row, "guest", guest, lan_port)
        n = LISTED.findall(guest)
        row("the guest's list stayed empty", n == ["0"], str(n))
        row("the guest joined by address", "coop tab (test): Join pressed" in guest and
            "coop: connected (joined)" in guest)
        miss = shots(guest_dir, ("lan_none_searching", "lan_none_checklist")) + shots(host_dir, ("lan_host",))
        row("pictures: searching, the checklist, the host's note", not miss, "missing %s" % miss if miss else "")
    elif variant == "lan-retry":
        lan_tab_rows(row, "guest", guest, lan_port)
        typed = guest.find("coop tab (test): code typed")
        refused = guest.find("coop: the join was refused: wrong join code or password")
        row("a code typed frames after the code entry opened (its field holding the keyboard) was the one sent",
            0 <= typed < refused and "wrong password" in host, "typed at %d, refused at %d" % (typed, refused))
        again = guest.find("Try again pressed (the tab said: ", max(refused, 0))
        entry = guest.find("coop tab: the code entry for Host's game, with the code pad", max(refused, 0))
        row("Try again opened the code entry with the pad", 0 <= refused < entry < again,
            "entry at %d, Try again at %d" % (entry, again))
        views = re.findall(r"padview: the code pad (is all in view|is NOT all in view|no code pad shown)(.*)",
                           guest[max(again, 0):])
        row("the code pad was all on screen when it opened and after the list filled in",
            len(views) == 2 and all(v[0] == "is all in view" for v in views), "; ".join(a + b for a, b in views))
        row("the right code joined after Try again", "coop: connected (joined)" in guest and
            guest.count("coop: connected (joined)") == 1, "")
        miss = shots(guest_dir, ("lan_retry_refused", "lan_retry_pad"))
        row("pictures of the refusal and the pad after Try again", not miss, "missing %s" % miss if miss else "")
    elif variant == "lan-hidden":
        row("Host over the internet: hosting, not visible on this network",
            re.search(r"coop tab \(test\): Host pressed\s*$", host, re.M) is not None and
            "coop: hosting on UDP port %d" % port in host and
            host.find("coop: lan: visible on this network") > host.find("visible on this network switched on"))
        n = LISTED.findall(guest)
        row("the guest's list: empty, then the host once ticked visible, then empty once unticked",
            n == ["0", "1", "0"], str(n))
        row("unticking stopped the answers", "coop: lan: no longer visible on this network" in host)
        on = host.find("visible on this network switched on")
        off = host.find("visible on this network switched off")
        shown = [m.start() for m in re.finditer(r"coop hud: the join code is shown", host)]
        hid = host.find("coop hud: the join code is hidden", max(off, 0))
        row("the join code was on the host's screen only while visible", 0 <= on < off and
            all(on < p < off for p in shown) and (not shown or off < hid),
            "shown at %s, visible %d to %d, hidden at %d" % (shown, on, off, hid))
        row("the guest joined by address", "coop tab (test): Join pressed" in guest and
            "coop: connected (joined)" in guest)
        miss = shots(guest_dir, ("lan_hidden_list", "lan_hidden_visible")) + shots(host_dir, ("lan_hidden_host",))
        row("pictures: the hidden host's tab, the empty list, the listed host", not miss,
            "missing %s" % miss if miss else "")


def config_value(d, key):
    cfg = open(os.path.join(d, "config.ini"), "rb").read().decode("latin-1")
    m = re.search(r"^\s*%s\s*=\s*(\w+)" % key, cfg, re.M)
    return m.group(1) if m else None


def persist_checks(row, host, guest, host_dir, guest_dir):
    """M4 (docs/MULTIPLAYER.md 7.8): a session started from earlier choices in config.ini."""
    own = lambda log, m: re.search(r"coop: self frame \d+ scene BB02 .* model %d$" % m, log, re.M) is not None
    row("the host read [coop] character = patrick and took Patrick, in Disco (model 19)",
        "coop: switched to Patrick ([coop] character)" in host and own(host, 19))
    look = guest.find("coop: the partner's look: Patrick")
    row("the guest on automatic heard the host's Patrick and stayed SpongeBob, in Caveman (model 7)",
        look >= 0 and "coop: switched to Patrick" not in guest and own(guest, 7),
        "the host's look at %d" % look)
    row("each draws the other in an outfit of BB02 until the next level (its own is not loaded here)",
        re.search(r"the partner's model 7 is not loaded here; drawn as model \d+", host) is not None and
        re.search(r"the partner's model 19 is not loaded here; drawn as model \d+", guest) is not None)
    row("the guest's Caveman parts are mapped onto the outfit the host draws (its chain is ordered otherwise)",
        re.search(r"the partner's parts [0-9a-f]+ on model 7 are [0-9a-f]+ on model \d+", host) is not None)
    row("the host's LB hold: Patrick's next costume (Plain Patrick), kept in config.ini",
        "coop: LB held: the next costume" in host and "coop: Patrick's costume set to plain" in host and
        config_value(host_dir, "costume_patrick") == "plain", "costume_patrick = %s" % config_value(host_dir, "costume_patrick"))
    row("the guest's LB hold: SpongeBob's next costume (Ripped Shorts), kept in config.ini",
        "coop: LB held: the next costume" in guest and "coop: SpongeBob's costume set to rippedshorts" in guest and
        config_value(guest_dir, "costume_spongebob") == "rippedshorts",
        "costume_spongebob = %s" % config_value(guest_dir, "costume_spongebob"))
    row("the host's other choices kept (character patrick, SpongeBob's costume mermaidman)",
        config_value(host_dir, "character") == "patrick" and config_value(host_dir, "costume_spongebob") == "mermaidman")


def look_flood_checks(row, host, guest):
    """M4's fix round (docs/MULTIPLAYER.md 7.4): the newest of a backlog of looks is kept."""
    st = guest.find("(TSSM_TEST_STALL)")
    row("the guest's game stopped for 15 s", st >= 0 and "the game thread runs again" in guest[st:])
    waits = [m.start() for m in re.finditer(r"coop: the partner's game has run no frame for (\d+) ms; waiting", host)
             if int(m.group(1)) >= 2000]
    w = waits[-1] if waits else -1
    again = host.find("coop: the partner's game runs again", w) if w >= 0 else -1
    during = len(re.findall(r"coop: look sent", host[w:again])) if 0 <= w < again else 0
    row("the host changed its look more than 8 times while the guest was stopped (LB held 12 times)",
        during > 8 and len(re.findall(r"coop: LB held: the next costume", host)) >= 10,
        "%d looks sent during the stall" % during)
    row("the guest got them all at once, past the rate (logged)",
        "many looks from the partner at once" in guest, "")
    sent = re.findall(r"coop: look sent: (\w+), costumes (\d+) (\d+)", host)
    got = re.findall(r"coop: the partner's look: (\w+), costumes (\d+) (\d+)", guest)
    row("the guest ends with the host's newest look", bool(sent) and bool(got) and sent[-1] == got[-1],
        "host's last %s, guest's last %s" % (sent[-1] if sent else None, got[-1] if got else None))
    timeouts = [l for l in (host + guest).splitlines() if "timed out" in l or "connection was lost" in l]
    row("the session stayed up (one join each, no timeout)",
        guest.count("coop: connected (joined)") == 1 and host.count("coop: connected (hosting)") == 1 and
        not timeouts, timeouts[0].strip() if timeouts else "")


def replay_show_checks(row, host, guest):
    """M4's fix round (docs/MULTIPLAYER.md 7.3): the host's show, then the guest's own replay of it."""
    hc = re.search(r"coop: show (\d+) \(cutscene start\) sent to the guest", host)
    c = hc.group(1) if hc else "?"
    gc = re.search(r"coop: the host's cutscene %s( plays here too|: it already plays here)" % c, guest)
    row("the host's cutscene played on both", hc is not None and gc is not None)
    hend = host.find("coop: show %s (cutscene end) sent to the guest" % c)
    gend = guest.find("coop: the host's cutscene %s ended; it ends here too" % c)
    row("the host's skip ended it on both", hend >= 0 and gend >= 0)
    own = guest.find("event 190 to de0789d3 (TSSM_TEST_EVENT)")
    taken = guest.find("coop: a skip by this player was taken: the show is this game's own", own)
    refused = guest.find("a skip by this player was not taken", own) if own >= 0 else -1
    refused = refused if 0 <= refused < (taken if taken >= 0 else len(guest)) else -1
    row("the guest's own replay of it afterwards: its skip was taken (not refused for the host)",
        0 <= gend < own < taken and refused < 0,
        "replay at %d, skip taken at %d, refused at %d" % (own, taken, refused))
    # The host's replay (the level's own script) that ends on the host
    # without its end (its player defeated, its level reset): the guest's copy
    # ends too, once the host's state has said for a second that it is in no
    # show, rather than playing on with no skip for the guest.
    starts = len(re.findall(r"coop: show %s \(cutscene start\) sent to the guest" % c, host))
    ends = len(re.findall(r"coop: show %s \(cutscene end\) sent to the guest" % c, host))
    if starts > ends:
        row("the host's replay ended there without its end, and the guest's copy ended too",
            "ended there without its end (the host is in no show); it ends here too" in guest[taken:],
            "host starts %d, ends %d" % (starts, ends))


def save_checks(row, host, guest, host_dir, guest_dir):
    """M4 (docs/MULTIPLAYER.md 7.6): a save by hand on both."""
    row("the host's save by hand was written (the control)",
        "a save by hand into the first slot (TSSM_TEST_SAVE)" in host and bool(saves(host_dir)) and
        "a save by hand was refused" not in host, "host saves: %s" % saves(host_dir))
    asked = guest.find("a save by hand into the first slot (TSSM_TEST_SAVE)")
    refused = guest.find("coop: a save by hand was refused: in co-op only the host's game saves")
    back = re.search(r"tssm: test: frame (\d+): back in play after the save screen", guest)
    row("the guest's save by hand was refused", 0 <= asked < refused)
    row("the guest's game wrote no save", not saves(guest_dir), "guest saves: %s" % saves(guest_dir))
    row("the guest's save screen closed by itself, back to play within 10 frames",
        back is not None and back.start() > refused >= 0 and int(back.group(1)) <= 1310,
        "back in play at frame %s" % (back.group(1) if back else "?"))


def journey_checks(row, host, guest, host_dir, guest_dir, stall_side, stall, stall_frame):
    """M4 (docs/MULTIPLAYER.md 7.8): the checks of the levels journey."""
    model = lambda log, k: "coop: partner model %s (%d) built for this level" % (k[0], k[1]) in log
    car = ("patty_wagon_bind.MINF", 25)
    row("BB01 (one character): each draws the other as a Patty Wagon of its own", model(host, car) and model(guest, car))
    row("the host took BB01's exit and told the guest (EV_LEVEL), to wait for it",
        "coop: going to DE01; the guest is told (EV_LEVEL) and its arrival will be waited for" in host)
    walked = guest.find("coop: a portal to DE01 was not taken: the host leads the way to other levels")
    row("the guest's own walk into BB01's exit (while the host was there) was not taken",
        0 <= walked < guest.find("coop: the host is going to DE01 (EV_LEVEL)"))
    row("the guest went along at once", "coop: the host is going to DE01 (EV_LEVEL)" in guest and
        "coop: following the host to DE01 (it went there just now)" in guest)
    hg = re.search(r"coop: the guest has loaded DE01 \(waited (\d+) ms\); both start it now \(EV_GO\) at clock ms (\d+)",
                   host)
    gg = re.search(r"coop: the host starts DE01 \(EV_GO, waited (\d+) ms\) at clock ms (\d+)", guest)
    apart_ms = abs(int(hg.group(2)) - int(gg.group(2))) if hg and gg else None
    row("both started DE01 together (within 300 ms)", apart_ms is not None and apart_ms <= 300,
        "host waited %s ms, guest %s ms, started %s ms apart" % (hg and hg.group(1), gg and gg.group(1), apart_ms))
    if stall_side == "join" and hg and stall_frame < 1350:  # the guest stalled across the level change
        row("the host waited through the guest's stall before starting", int(hg.group(1)) >= 1000 * stall - 1500,
            "waited %s ms for a %g s stall" % (hg.group(1), stall))
    # The fly-through: on both, from the start; the guest's skip refused, the host's ends both.
    hfly = re.search(r"coop: show (\d+) \(fly-through start\) sent to the guest", host)
    k = hfly.group(1) if hfly else "?"
    gfly = re.search(r"coop: the host's fly-through %s(: one already plays here| plays here too)" % k, guest)
    row("DE01's fly-through played on both (the host's start sent; the guest's own was on)", bool(hfly and gfly),
        gfly.group(1) if gfly else "")
    go = guest.find("coop: the host starts DE01")
    flyon = re.search(r"tssm: test: frame (\d+): camera at .*fly-through on", guest[go:]) if go >= 0 else None
    row("the guest's camera was in the fly-through after the start", flyon is not None)
    refused = [m.start() for m in re.finditer(r"coop: a skip by this player was not taken", guest)]
    hend = host.find("coop: show %s (fly-through end) sent to the guest" % k)
    gend = guest.find("coop: the host's fly-through %s ended; it ends here too" % k)
    row("the guest's skip of the fly-through was not taken", any(go < r < (gend if gend >= 0 else len(guest))
                                                                for r in refused))
    row("the host's skip ended the fly-through on both", hend >= 0 and gend >= 0)
    # Costumes and characters.
    row("each loaded the other's chosen outfits with DE01",
        "loading the partner's outfit too (PLSA)" in host and "loading the partner's outfit too (PLPC)" in host and
        "loading the partner's outfit too (PLSB)" in guest)
    own = lambda log, m: re.search(r"coop: self frame \d+ scene DE01 .* model %d$" % m, log, re.M) is not None
    row("the host plays SpongeBob in its Mermaid Man costume (model 8)", own(host, 8))
    row("the guest took Patrick, in its Goofy Goober costume (model 20)",
        "coop: switched to Patrick (a guest takes the other character than the host's)" in guest and own(guest, 20))
    row("the host draws the guest as Patrick, Goofy Goober", "the partner is drawn as Patrick (model 20)" in host)
    row("the guest draws the host as SpongeBob, Mermaid Man", "the partner is drawn as SpongeBob (model 8)" in guest)
    row("the guest's LB: SpongeBob (Caveman), so both are SpongeBob in different costumes",
        "coop: switched to SpongeBob (asked for)" in guest and own(guest, 7) and
        "the partner is drawn as SpongeBob (model 7)" in host)
    row("the host's LB: Patrick, and the guest draws it so",
        "coop: switched to Patrick (asked for)" in host and "the partner is drawn as Patrick (model 13)" in guest)
    # The cutscene.
    hc = re.search(r"coop: show (\d+) \(cutscene start\) sent to the guest", host)
    c = hc.group(1) if hc else "?"
    gc = guest.find("coop: the host's cutscene %s plays here too" % c)
    row("the host's cutscene played on the guest too", hc is not None and gc >= 0)
    gce = guest.find("coop: the host's cutscene %s ended; it ends here too" % c)
    if not (stall_side == "join" and 2900 <= stall_frame <= 3300):
        # (A guest stalled during the cutscene presses skip only after the
        # host has ended it.)
        row("the guest's skip of the cutscene was not taken", any(gc < r < (gce if gce >= 0 else len(guest))
                                                                  for r in refused) if gc >= 0 else False)
    row("the host's skip ended the cutscene on both",
        "coop: show %s (cutscene end) sent to the guest" % c in host and gce >= 0)
    if stall_side:
        journey_stall_checks(row, host, guest, stall_side, stall, stall_frame, c)
    # DE02, the slide.
    sled = ("spongepat_0_bind.MINF", 22)
    row("the host took DE01's exit to DE02 and the guest went along",
        "coop: going to DE02; the guest is told (EV_LEVEL)" in host and
        "coop: following the host to DE02 (it went there just now)" in guest)
    row("DE02 (one character): each draws the other on a sled of its own", model(host, sled) and model(guest, sled))
    row("in DE02 the guest's LB switches nothing (one character)",
        "coop: LB tapped: only SpongeBob or Patrick switch" in guest[guest.find("following the host to DE02"):])
    row("the guest's game wrote no save", not saves(guest_dir), "guest saves: %s" % saves(guest_dir))


def journey_stall_checks(row, host, guest, stall_side, stall, stall_frame, c):
    """The levels journey's stall variants (docs/MULTIPLAYER.md 7.8): the stall
    happened where it was meant to, the other game waited it out, the session
    stayed up, and the shows went on as they should."""
    stalled_log, other_log = (host, guest) if stall_side == "host" else (guest, host)
    who, other = ("host", "guest") if stall_side == "host" else ("guest", "host")
    st = stalled_log.find("(TSSM_TEST_STALL)")
    back = stalled_log.find("the game thread runs again", st) if st >= 0 else -1
    told = stalled_log.find("telling the other player (STALL)", st) if st >= 0 else -1
    row("the %s's game thread stopped for %g s and told the other (STALL)" % (who, stall),
        0 <= st < back and told >= 0, "")
    # The other game's word on it: its first wait of 2 s or more after what
    # the stall comes after (the host leaving BB01, or the cutscene starting),
    # and the next "runs again" after that.
    if stall_frame >= 2900:
        anchor = ("coop: show %s (cutscene start) sent to the guest" % c if other == "host" else
                  "coop: the host's cutscene %s plays here too" % c)
    else:
        anchor = "coop: going to DE01" if other == "host" else "coop: the host is going to DE01 (EV_LEVEL)"
    at = other_log.find(anchor)
    waits = [m for m in re.finditer(r"coop: the partner's game has run no frame for (\d+) ms; waiting for it",
                                    other_log)
             if int(m.group(1)) >= 2000 and 0 <= at < m.start()]
    w = waits[0].start() if waits else -1
    again = other_log.find("coop: the partner's game runs again", w) if w >= 0 else -1
    if stall_frame >= 2900:
        # (Across the level change the host's wait is the level's start,
        # checked above: "the host waited through the guest's stall".)
        row("the %s waited for it and saw it run again" % other, 0 <= w < again,
            "waited from %s, again at %s" % (w, again))
    timeouts = [l for l in (host + guest).splitlines() if "timed out" in l or "connection was lost" in l]
    gcut = guest.find("TSSM_EXIT_AFTER reached")
    row("the session stayed up through it (one join each, no disconnect before the end, no timeout)",
        guest.count("coop: connected (joined)") == 1 and host.count("coop: connected (hosting)") == 1 and
        "coop: disconnected" not in guest[:gcut if gcut >= 0 else None] and "DC_STALLED" not in host + guest and
        not timeouts, timeouts[0].strip() if timeouts else "")
    if 2900 <= stall_frame <= 3300:
        # During the cutscene (DE01's flinger, the host's).
        hcs = host.find("coop: show %s (cutscene start) sent to the guest" % c)
        hce = host.find("coop: show %s (cutscene end) sent to the guest" % c, hcs) if hcs >= 0 else -1
        gcs = guest.find("coop: the host's cutscene %s plays here too" % c)
        gce = guest.find("coop: the host's cutscene %s ended; it ends here too" % c, gcs) if gcs >= 0 else -1
        if stall_side == "host":
            row("the host's stall fell inside its cutscene", 0 <= hcs < st < back < hce, "")
            row("the guest's copy of the cutscene ended only after the host ran again and ended it",
                0 <= gcs < w < again < gce, "")
        else:
            row("the guest's stall fell inside the host's cutscene, playing on both", 0 <= gcs < st and hcs >= 0, "")
            row("the host ended the cutscene during the guest's stall", 0 <= w < hce < again,
                "the host waited at %d, ended it at %d, saw the guest again at %d" % (w, hce, again))
            row("the guest's copy ended when it ran again", 0 <= back < gce, "")


FIGHTS = {"combat": "combat", "respawn": "respawn", "host-death": "host-death", "bowl": "bowl",
          "host-paused": "host-paused", "levels": "levels"}
# Protocol 7's fixes (docs/MULTIPLAYER.md 13): each a fight of its own.
FIX_FIGHTS = ("far-area", "both-hit", "drive", "arena", "boxes", "boxes-resync", "breakables", "zap", "tokens",
              "maintoken", "given", "upgrade", "own-level", "maintoken-death", "keep-reset", "arena-waves",
              "maintoken-stop")
FIGHTS.update({k: k for k in FIX_FIGHTS})


def spins(first, last, every, key="x/4"):
    """A press of `key` every `every` frames from `first` to `last` (TSSM_INPUT)."""
    return ",".join("%d:%s" % (f, key) for f in range(first, last + 1, every))


# Mindy's talk at DE01's fly-through end (A through its pages, B to close).
MINDY_KEYS = ",".join("%d:a" % f for f in range(2480, 2761, 40)) + ",2850:b/1,2900:b/1"
# A third B at 1100 closes the task screen when the talk came late (B in
# play is a SpongeBowl or a Cartwheel, before the players are placed).
FIGHT_SETUPS = {
    # M4's journey (run(): BB01, DE01, DE02). Frames are each copy's own; the
    # guest boots a second after the host, so its frames run about 50 behind.
    # B every 50 frames closes the levels' talks and task screens when they
    # come; A in a fly-through or a cutscene is its skip.
    "levels": {
        "exit": 4700,
        "host_script": ("10:a," + b_every(300, 1200) + ",1650:a," + b_every(1700, 2200) + ",2900:white/1,3250:a,3800:a,"
                        + b_every(3850, 4300)),
        "guest_script": ("10:a," + b_every(300, 1200) + ",1500:a," + b_every(1700, 2200) + ",2600:white/1,3120:a,3150:a,3190:a,3700:a,"
                         + b_every(3850, 4300) + ",4380:white/1"),
        # In BB01 the guest's Patty Wagon a few steps beside and ahead of
        # the host's, so each sees the other's.
        "guest_place": "1120:-27.3:2.5:418.8:235",
        "host_extra": {"TSSM_TEST_EVENT": "1350:d93d5947:teleport,3100:de0789d3:cutscene,3500:d5997670:teleport",
                       "TSSM_TEST_COOP_LOOK": "auto,1,0", "TSSM_TEST_COSTUMES": "all"},
        # The guest walks into BB01's exit itself (its portal's own event) at
        # 1200, while the host is still in BB01 (it leaves at its 1350; on a
        # loaded PC the guest's frame count runs up to 120 behind): not
        # taken; the host's exit then takes it along.
        "guest_extra": {"TSSM_TEST_COOP_LOOK": "auto,3,4", "TSSM_TEST_COSTUMES": "all",
                        "TSSM_TEST_EVENT": "1200:d93d5947:teleport"},
        "host_frames": [1200, 1500, 2500, 2800, 3050, 3200, 4450],
        "guest_frames": [1150, 1450, 2450, 2750, 3000, 3150, 4400],
    },
    "combat": {
        "exit": 2650,
        "host_script_more": ",1100:b/1",
        # A step from the robot, facing it (TSSM_TEST_FACE), then a spin, a
        # second apart, wherever the robot has gone.
        "guest_script_more": ",1100:b/1," + ",".join("%d:x/4" % f for f in range(1450, 2400, 30)),
        "guest_place": "1150:11.0:7.0:71.0:180",
        "guest_extra": {"TSSM_TEST_FACE": "1446-2400/30:6e72b6c4:1.6"},
        "host_place": "1300:9.5:7.0:61.0:15",
    },
    # The combat fight, with the host's pause menu open (START) from 1400 to
    # 1700: the guest's spins from 1450 on are claims the paused host refuses;
    # once it plays again it applies the next ones.
    "host-paused": {
        "exit": 2650,
        "host_script_more": ",1100:b/1,1400:start/1,1700:start/1",
        "guest_script_more": ",1100:b/1," + ",".join("%d:x/4" % f for f in range(1450, 2400, 30)),
        "guest_place": "1150:11.0:7.0:71.0:180",
        "guest_extra": {"TSSM_TEST_FACE": "1446-2400/30:6e72b6c4:1.6"},
        "host_place": "1300:9.5:7.0:61.0:15",
    },
    "respawn": {
        "exit": 2550,
        "host_script_more": ",1100:b/1",
        "guest_script_more": ",1100:b/1",
        "guest_place": "1150:11.0:7.0:71.0:180",
        "guest_extra": {"TSSM_TEST_HEALTH": "1160:1"},
    },
    "host-death": {
        "exit": 2750,
        "host_script_more": ",1100:b/1",
        "guest_script_more": ",1100:b/1",
        "host_place": "1150:11.0:7.0:71.0:180",
        "host_extra": {"TSSM_TEST_HEALTH": "1200:1"},
    },
    "bowl": {
        "scene": "TT01",
        "exit": 2650,
        "host_script_more": ",1080:b/1",
        # A fresh save has no SpongeBowl yet (TSSM_TEST_POWERUP gives it, on
        # the host too: the moves are the host's save's, 13). Each placing is
        # a jump the host's speed check ignores the guest's position for (2
        # s), so the guest is put facing the robot only every 400 frames and
        # bowls in between, from 150 frames after each.
        "guest_script_more": ",1080:b/1," + ",".join(
            "%d:b/12" % f for k in (1300, 1700, 2100) for f in range(k + 150, k + 400, 40)),
        "guest_extra": {"TSSM_TEST_FACE": "1300-2100/400:a1462dbc:7.0", "TSSM_TEST_POWERUP": "1200:1"},
        "host_extra": {"TSSM_TEST_POWERUP": "1150:1"},
        "host_place": "1100:12.0:12.8:61.0:180",
        "guest_place": "1100:12.0:11.0:52.0:0",
    },
    # --- protocol 7's fixes ---------------------------------------------------------------
    # The host stays at DE01's start; the guest spins at a crate (thing 73) in
    # a part of the level the host's game culls.
    "far-area": {
        "exit": 2650,
        "host_script_more": ",1100:b/1",
        "guest_script_more": ",1100:b/1," + spins(1450, 2400, 30),
        "guest_place": "1150:160.6:8.2:30.3:180",
        "guest_extra": {"TSSM_TEST_FACE": "1446-2400/30:0f18e3b3:1.4"},
    },
    # Both spin at the robot (2 health), the host once at its 1490, the
    # guest every 30 frames from its 1450 (its frames run about 40-50 behind,
    # so its first spin lands a few hundred ms after the host's), with 40 ms
    # of lag each way.
    "both-hit": {
        "exit": 2650,
        "host_script_more": ",1100:b/1,1490:x/4",
        "guest_script_more": ",1100:b/1," + spins(1450, 2400, 30),
        "guest_place": "1150:11.0:7.0:71.0:180",
        "guest_extra": {"TSSM_TEST_FACE": "1446-2400/30:6e72b6c4:1.6"},
        "host_place": "1300:9.5:7.0:61.0:15",
        "host_extra": {"TSSM_TEST_FACE": "1486-1494/4:6e72b6c4:1.6"},
        "netsim": "40,10,1",
    },
    # BB01: the guest's wagon faces the pile of crates (things 162-166) and
    # drives into it.
    "drive": {
        "scene": "BB01",
        "exit": 1600,
        "one_char": "SpongeBob",  # the Patty Wagon, SpongeBob's character
        "scripted_movers": True,
        "guest_script_more": ",1200:a/120",
        "guest_place": "1150:-0.5:13.5:308.4:180",
        "host_place": "1150:2.0:13.5:318.0:180",
    },
    # AM01: the guest spins at the wave's first fogger (thing 10, brought
    # back by a spawner), taps LB at 1200; the host stays at the start, kept
    # from defeat.
    "arena": {
        "scene": "AM01",
        "exit": 2600,
        "one_char": "SpongeBob",
        # The guest spins until 1400, then stands further in, nearer the
        # foggers than the host (they breathe at it).
        "guest_script_more": "," + spins(604, 1400, 30) + ",1200:white/1",
        "guest_place": "1420:2.0:-0.1:-42.0:180",
        "guest_extra": {"TSSM_TEST_FACE": "600-2400/30:0baa0a98:1.6"},
        # The host only stands by: its defeat would reset the arena.
        "host_extra": {"TSSM_TEST_HEALTH_KEEP": "3"},
        "host_slack": 400,
    },
    # AM01: the guest spins at the wave's first fogger until 1300 (its kills
    # count the wave down); the host's own level then ends each wave: the
    # wave's counter to 1 (eEventCount1, 402) and a decrement (12), waves
    # 2 and 3 once the last one's countdown is over (10 s). The guest's own
    # decrement of wave 1's counter at its 1650 (the host's count is 1 then)
    # must not end the wave there. The host stands by, kept from defeat.
    "arena-waves": {
        "scene": "AM01",
        "exit": 4300,
        "one_char": "SpongeBob",
        "guest_script_more": "," + spins(604, 1300, 30),
        "guest_extra": {"TSSM_TEST_FACE": "600-1300/30:0baa0a98:1.6",
                        "TSSM_TEST_SEND": "1650:WAVE_1_DONE_COUNTER_01:12"},
        "host_extra": {"TSSM_TEST_HEALTH_KEEP": "3",
                       "TSSM_TEST_SEND": ",".join("%d:WAVE_%d_DONE_COUNTER_01:%d" % (f, w, e) for f, w, e in (
                           (1500, 1, 402), (1800, 1, 12), (2500, 2, 402), (2600, 2, 12), (3300, 3, 402),
                           (3400, 3, 12)))},
        "host_slack": 400,
    },
    # BB02: the guest (Patrick) spins at the crate under a stack (18); the
    # upper one (22) falls and must break when hit.
    "boxes": {
        "scene": "BB02",
        "exit": 2100,
        "guest_script_more": "," + spins(1105, 1865, 40),
        "guest_place": "1000:24.0:1.5:-36.0:180",
        "guest_extra": {"TSSM_TEST_FACE": "1100-1900/40:0f18e01c:1.5"},
        "host_place": "1000:24.3:1.5:-30.5:180",
    },
    # BB02: the guest's connection is cut for 7 s at its 1150; it breaks
    # crates meanwhile (its own world), then spins at them again.
    "boxes-resync": {
        "scene": "BB02",
        "exit": 2050,
        "cut": 7.0,
        "guest_script_more": "," + spins(1380, 1470, 30) + "," + spins(1700, 1940, 30),
        "guest_extra": {"TSSM_TEST_FACE": "1000-1001/100:0f18e01c:1.5", "TSSM_TEST_BLACKOUT": "1150:7"},
        "host_place": "1000:24.3:1.5:-30.5:180",
    },
    # TT01: the guest's karate at the barrels by the start (breakables).
    "breakables": {
        "scene": "TT01",
        "exit": 1900,
        "host_script_more": ",1080:b/1",
        "guest_script_more": ",1080:b/1," + spins(1250, 1700, 40),
        "host_place": "1100:11.0:12.9:63.4:270",
        "guest_place": "1100:4.0:12.9:63.6:270",
    },
    # BB02: the guest (SpongeBob: the host plays Patrick) bowls at the crates
    # with the upgraded SpongeBowl (the shared save's, on the host).
    "zap": {
        "scene": "BB02",
        "exit": 1800,
        "guest_script_more": ",1250:b/12,1450:b/12,1650:b/12",
        "guest_place": "1000:25.5:0.5:-33.0:180",
        "host_place": "1000:30.3:1.5:-30.5:180",
        "host_patrick": True,
        "host_extra": {"TSSM_TEST_COOP_LOOK": "patrick,0,0", "TSSM_TEST_POWERUP": "1050:2"},
        "guest_extra": {"TSSM_TEST_POWERUP": "1050:2"},
    },
    # DE01, both saves with 3 tokens: the guest's own game shows token 05
    # (the bungee's reward, its script) and the guest takes it; the host
    # takes token 03 itself; at the fly-through's end (its script, on both)
    # Mindy's gate needs 5. The host leaves first: the guest's session ends.
    "tokens": {
        "exit": 3000,
        "host_exit": 3000,
        "guest_exit": 3300,
        "ini": {"G.InitialSpatulaCount": "3"},
        "host_script_more": ",1100:b/1," + MINDY_KEYS,
        "guest_script_more": "," + MINDY_KEYS,
        "guest_place": "1100:98.0:15.5:-70.0:0",
        "guest_extra": {"TSSM_TEST_EVENT": "1150:e37531f6:run,2400:e82ad3c8:run",
                        "TSSM_TEST_FACE": "1200-1900/25:7ecc0fe2:0.3", "CONFIG": "[coop]\nreconnect = off\n"},
        "host_place": "1650:94.9:15.5:-60.1:180,1995:118.0:10.0:-98.0:0,2012:94.9:15.5:-60.1:180",
        "host_extra": {"TSSM_TEST_EVENT": "2400:e82ad3c8:run", "TSSM_TEST_FACE": "2000-2010/5:7ecc0fe0:0.3"},
    },
    # DE01: both games show the main task's token (TOWER3_SCRIPT_01); the
    # guest takes it. The host's level ends (DE02).
    "maintoken": {
        "exit": 2900,
        "hitch_skip": True,
        "host_script_more": ",1100:b/1",
        "host_extra": {"TSSM_TEST_EVENT": "1200:fbe20710:run"},
        "guest_extra": {"TSSM_TEST_EVENT": "1200:fbe20710:run", "TSSM_TEST_FACE": "1300-1600/20:7ecc0fde:0.3"},
    },
    # BB01: the guest's game alone runs MINDY_1_END_SCRIPT, which gives the
    # main challenge's token.
    "given": {
        "scene": "BB01",
        "exit": 2300,
        "one_char": "SpongeBob",  # the Patty Wagon, SpongeBob's character
        "scripted_movers": True,
        "guest_movie": True,  # the script plays the level's story movie on the guest
        "guest_place": "1120:-27.3:2.5:418.8:235",
        "guest_extra": {"TSSM_TEST_EVENT": "1300:5e3ee221:run"},
    },
    # DE01, both saves with an upgrade point: the guest's pause menu (START
    # at 1350, closed at 1500) buys the Karate Spin's upgrade (move 0); a
    # second purchase (move 1) at 1700, with the menu closed, is refused.
    "upgrade": {
        "exit": 2100,
        "ini": {"G.UnusedSkillPoints": "1"},
        "host_script_more": ",1100:b/1",
        "guest_script_more": ",1100:b/1,1350:start/1,1500:start/1",
        "guest_extra": {"TSSM_TEST_UPGRADE": "1400:0,1700:1"},
    },
    # BB02, following off: the host goes to DE01 at 1000; the guest stays.
    # Once the host has been gone 3 s the guest's own progress is back; it
    # spins at the crates by the start (its own world: their shiny objects
    # are its own); the host leaves the game at 2000 and the guest's
    # autosave at 2350 has those shiny objects.
    "own-level": {
        "scene": "BB02",
        "exit": 2500,
        "host_exit": 2000,
        "guest_exit": 2500,
        "guest_script_more": "," + spins(1505, 1945, 40),
        "guest_place": "1460:24.0:1.5:-36.0:180",
        "guest_extra": {"TSSM_COOP_FOLLOW": "0", "TSSM_TEST_FACE": "1500-1940/40:0f18e01c:1.5",
                        "TSSM_TEST_AUTOSAVE": "2350", "CONFIG": "[coop]\nreconnect = off\n"},
        "host_extra": {"TSSM_TEST_SCENE": "1000:DE01"},
    },
    # DE01: as maintoken, the host defeated (TSSM_TEST_HEALTH 0) 5 s
    # after it counted the guest's token, inside the token's 5.8 s.
    "maintoken-death": {
        "exit": 3500,
        "hitch_skip": True,
        "host_script_more": ",1100:b/1",
        "host_extra": {"TSSM_TEST_EVENT": "1200:fbe20710:run", "TSSM_TEST_HEALTH": "1640:0",
                       "TSSM_COOP_SYNC_CAPTURE": "1000:70"},
        # Captures all the way to the level end (the guest's screen there).
        "guest_extra": {"TSSM_TEST_EVENT": "1200:fbe20710:run", "TSSM_TEST_FACE": "1300-1600/20:7ecc0fde:0.3",
                        "TSSM_COOP_SYNC_CAPTURE": "1000:70"},
    },
    # DE01: as maintoken, the host's Co-op tab's Stop 1.75 s after it counted
    # the guest's token, inside the token's 5.8 s.
    "maintoken-stop": {
        "exit": 2900,
        "hitch_skip": True,
        "host_script_more": ",1100:b/1",
        "host_extra": {"TSSM_TEST_EVENT": "1200:fbe20710:run", "TSSM_TEST_COOP_TAB": "1450:stop"},
        "guest_extra": {"TSSM_TEST_EVENT": "1200:fbe20710:run", "TSSM_TEST_FACE": "1300-1600/20:7ecc0fde:0.3",
                        "CONFIG": "[coop]\nreconnect = off\n"},
    },
    # DE01, as combat: the guest's spins defeat the robot and it takes the
    # shiny objects (the host's save counts them); its level is reset at
    # 2100 (TSSM_TEST_RESET: a lost chance, as an out-of-bounds fall); the
    # host goes to DE02 at 2400 and the guest follows.
    "keep-reset": {
        "exit": 3300,
        "hitch_skip": True,
        "host_script_more": ",1100:b/1",
        "guest_script_more": ",1100:b/1," + ",".join("%d:x/4" % f for f in range(1450, 2050, 30)),
        "guest_place": "1150:11.0:7.0:71.0:180",
        "guest_extra": {"TSSM_TEST_FACE": "1446-2050/30:6e72b6c4:1.6", "TSSM_TEST_RESET": "2100"},
        "host_place": "1300:9.5:7.0:61.0:15",
        "host_extra": {"TSSM_TEST_SCENE": "2400:DE02", "TSSM_TEST_HEALTH_KEEP": "3"},
    },
}

# The save variant: no presses after the guest's refused save (frame 1300):
# its save screen closes by itself.
SAVE_GUEST_KEYS = ""
SAVE_GUEST_FRAMES = [1310, 1340, 1450]
# The title variant: the guest's frame to leave for the title (both in view by then).
TITLE_QUIT = 1500

# --all: every scene and variant, one after the other.
MATRIX = [("BB02", "normal"), ("DE01", "normal"), ("TT01", "normal"), ("PT01", "normal"), ("BB02", "netsim"),
          ("DE01", "netsim"), ("TT01", "netsim"), ("DE01", "follow"), ("TT01", "follow"), ("DE01", "stay"),
          ("BB02", "wrong-password")]


# ---------------------------------------------------------------------------
# Local co-op (docs/MULTIPLAYER.md 12): only Player 1's copy is started, with
# --local-coop; it hosts on 127.0.0.1 at a port the system picks and starts
# Player 2's copy itself, in <run>/host/local2. Player 2 is scripted through
# TSSM_CHILD_* (Player 1's game passes those on, renamed).

LOCAL_VARIANTS = ("local", "local-close-guest", "local-close-host", "local-kill-guest", "local-kill-host",
                  "local-close-host-hung", "local-hang-guest", "local-end", "local-end-hung", "local-bringback")
LOCAL_STALLED = re.compile(r"tssm: test: frame \d+: the game thread stops for [\d.]+ s \(TSSM_TEST_STALL\)")
LOCAL_ALL = [("BB02", "local"), ("DE01", "local")] + [("DE01", v) for v in LOCAL_VARIANTS[1:]]
LOCAL_HOSTED = re.compile(r"tssm: coop: hosting on UDP port (\d+) \(this PC only\) for local co-op")
LOCAL_STARTED = re.compile(r"coop: local: Player 2's game started \(process (\d+)\) in (\w+) at clock ms (\d+)")
LOCAL_JOINED = re.compile(r"coop: local: joined Player 1's game at clock ms (\d+) \((-?\d+) ms after this game's first "
                          r"frame\)")
LOCAL_GONE = re.compile(r"coop: local: (.*?) \(process (\d+), exit code 0x([0-9a-f]+)\) at clock ms (\d+)")
LOCAL_PLACED = re.compile(r"coop: joined beside Player 1; placed (.*?) at (\S+) (\S+) (\S+) \(the host at (\S+) "
                          r"(\S+) (\S+)\)")
LOCAL_IDS = re.compile(r"coop: test: build id ([0-9a-f]+) game files ([0-9a-f]+)")
PLAYER2_KEYS = {"coop.name", "coop.character", "coop.costume_spongebob", "coop.costume_patrick", "coop.nametag",
                "input.camera_invert_x", "input.camera_invert_y", "input.camera_sensitivity", "input.stick_curve",
                "input.deadzone", "input.rumble"}
FIREWALL_TITLE = re.compile(r"Windows Security Alert|Windows Defender Firewall|Firewall|allow .* to communicate|"
                            r"Windows Security", re.I)


def top_windows():
    """(pid, class, title) of every visible top-level window."""
    import ctypes
    import ctypes.wintypes as wt
    u32 = ctypes.windll.user32
    out = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(h, _):
        if u32.IsWindowVisible(h):
            pid = wt.DWORD()
            u32.GetWindowThreadProcessId(h, ctypes.byref(pid))
            cls = ctypes.create_unicode_buffer(256)
            title = ctypes.create_unicode_buffer(256)
            u32.GetClassNameW(h, cls, 256)
            u32.GetWindowTextW(h, title, 256)
            out.append((pid.value, cls.value, title.value, h))
        return True

    u32.EnumWindows(each, 0)
    return out


FIREWALL_SEEN = []  # once set, no further run starts


class FirewallWatch:
    """Watches for a Windows Firewall prompt while the copies run: any new
    top-level window whose title reads like one. Never touches it; on one,
    `on_seen` ends our own copy and no further run starts."""

    def __init__(self, on_seen=None):
        self.before = {(p, c, t) for p, c, t, _ in top_windows()}
        self.seen = []
        self.on_seen = on_seen
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.loop, daemon=True)
        self.thread.start()

    def loop(self):
        while not self.stop.wait(1.0):
            for p, c, t, _ in top_windows():
                if (p, c, t) not in self.before and FIREWALL_TITLE.search(t) and (p, c, t) not in self.seen:
                    self.seen.append((p, c, t))
                    FIREWALL_SEEN.append((p, c, t))
                    print("FIREWALL PROMPT? process %d, class %s, title %r: stopping; it is left for the user"
                          % (p, c, t))
                    if self.on_seen is not None:
                        self.on_seen()

    def done(self):
        self.stop.set()
        self.thread.join(5)
        return self.seen


def post_close(pid):
    """Asks process `pid`'s game window to close, as its close button does."""
    import ctypes
    hs = [h for p, c, t, h in top_windows() if p == pid and c != "ConsoleWindowClass"]
    for h in hs:
        ctypes.windll.user32.PostMessageW(h, 0x0010, 0, 0)  # WM_CLOSE
    return bool(hs)


class Child:
    """Player 2's process, found from Player 1's log: waited on, and ended only
    when it is our own build (its image is --exe) and the test needs it."""

    def __init__(self, pid, exe):
        import ctypes
        import ctypes.wintypes as wt
        self.k32 = k32 = ctypes.windll.kernel32
        k32.OpenProcess.restype = wt.HANDLE
        k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
        k32.WaitForSingleObject.argtypes = [wt.HANDLE, wt.DWORD]
        k32.WaitForSingleObject.restype = wt.DWORD
        k32.GetExitCodeProcess.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD)]
        k32.TerminateProcess.argtypes = [wt.HANDLE, wt.UINT]
        k32.QueryFullProcessImageNameW.argtypes = [wt.HANDLE, wt.DWORD, wt.LPWSTR, ctypes.POINTER(wt.DWORD)]
        self.pid = pid
        self.h = self.k32.OpenProcess(0x00100000 | 0x1000 | 0x0001, False, pid)  # SYNCHRONIZE, QUERY_LIMITED, TERMINATE
        self.ours = False
        if self.h:
            buf = ctypes.create_unicode_buffer(1024)
            n = wt.DWORD(1024)
            if self.k32.QueryFullProcessImageNameW(self.h, 0, buf, ctypes.byref(n)):
                self.ours = os.path.normcase(os.path.abspath(buf.value)) == os.path.normcase(os.path.abspath(exe))
        self.ended_at = None

    def wait(self, seconds):
        """The exit code once it has ended within `seconds`, else None."""
        import ctypes
        import ctypes.wintypes
        if not self.h:
            return None
        if self.k32.WaitForSingleObject(self.h, int(seconds * 1000)) != 0:
            return None
        if self.ended_at is None:
            self.ended_at = clock_ms()
        code = ctypes.wintypes.DWORD()
        self.k32.GetExitCodeProcess(self.h, ctypes.byref(code))
        return code.value

    def kill(self):
        if self.h and self.ours:
            self.k32.TerminateProcess(self.h, 1)
            self.killed_at = clock_ms()


def wait_for(path, pattern, deadline, proc=None):
    """The first match of `pattern` in the file at `path`, waiting until
    `deadline` (time.time()); None if it never came or `proc` ended first."""
    while time.time() < deadline:
        try:
            m = re.search(pattern, open(path, "rb").read().decode("utf-8", "replace"))
            if m:
                return m
        except OSError:
            pass
        if proc is not None and proc.poll() is not None:
            try:
                return re.search(pattern, open(path, "rb").read().decode("utf-8", "replace"))
            except OSError:
                return None
        time.sleep(0.05)
    return None


def read_text(path):
    try:
        return open(path, "rb").read().decode("utf-8", "replace")
    except OSError:
        return ""


def file_state(folder):
    """name -> (size, mtime) of each file in `folder` (not below it)."""
    out = {}
    try:
        for name in os.listdir(folder):
            st = os.stat(os.path.join(folder, name))
            out[name] = (st.st_size, st.st_mtime_ns)
    except OSError:
        pass
    return out


def ini_keys(path):
    """section.key of every setting in an ini file."""
    out, section = [], ""
    for line in read_text(path).splitlines():
        line = line.split(";", 1)[0].split("#", 1)[0].strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip().lower()
        elif "=" in line:
            out.append("%s.%s" % (section, line.split("=", 1)[0].strip().lower()))
    return out


def run_local(a, scene, variant):
    """One local co-op run: Player 1's copy, which starts Player 2's."""
    exe = os.path.abspath(a.exe)
    assets = os.path.abspath(a.assets)
    out = os.path.join(os.path.abspath(a.out), "coop_%s_%s_%s" % (scene, variant, time.strftime("%Y%m%d_%H%M%S")))
    host_dir, guest_dir = os.path.join(out, "host"), os.path.join(out, "guest")
    os.makedirs(host_dir)
    os.makedirs(os.path.join(guest_dir, "frames"))
    profile = os.path.join(host_dir, "local2")
    p2_log_path = os.path.join(profile, "last_run.log")
    sc = dict(SCENES[scene])
    mod = boot_mod(assets, out, scene)
    lifetime = variant != "local"
    # Player 2 starts once Player 1 is in the level, then loads for about
    # 6 s: its frames run behind Player 1's by about that much ("local_late").
    late = sc.get("local_late", 400)
    g_exit = sc["exit"] if not lifetime else 1900
    h_exit = g_exit + late + 400
    host_extra, guest_extra = {}, {}
    host_place = sc.get("host_place", "")
    stall_at = 1300  # Player 2's frame: after it was put beside Player 1
    if variant == "local-hang-guest":
        guest_extra["TSSM_TEST_STALL"] = "%d:65" % stall_at
        h_exit = stall_at + late + 65 * 62 + 1500
        g_exit = h_exit + 1000  # it closes by itself once out of the session
    elif variant == "local-end":
        # End local co-op once Player 2 plays, then Bring Player 2 back; the
        # new Player 2 closes when Player 1 does.
        host_extra["TSSM_TEST_COOP_TAB"] = "1900:end;2500:bringback"
        g_exit = 2600
        h_exit = 2500 + late + 1200
    elif variant == "local-end-hung":
        guest_extra["TSSM_TEST_STALL"] = "%d:100" % stall_at
        host_extra["TSSM_TEST_COOP_TAB"] = "%d:end" % (stall_at + late + 30 * 60)
        h_exit = stall_at + late + 30 * 60 + 1200
        g_exit = h_exit + 6000
    elif variant == "local-bringback":
        host_extra["TSSM_TEST_COOP_TAB"] = "1500:bringback"
        g_exit = 1700
        h_exit = 1500 + late + g_exit + 400
    elif variant in ("local-close-host", "local-kill-host"):
        g_exit = h_exit + 3000  # it closes because Player 1's did
    elif variant == "local-close-host-hung":
        # Player 1's window is closed while Player 2's game is stopped.
        guest_extra["TSSM_TEST_STALL"] = "%d:60" % stall_at
        h_exit = stall_at + late + 60 * 60 + 1200
        g_exit = h_exit + 6000
    if lifetime:
        host_place = "1000:24.3:1.5:-30.5:0" if scene == "BB02" else ""
    # Player 1's HUD layer exists only while Player 2 is in the session.
    timing = {"exit": h_exit, "frames": [min(h_exit - 200, g_exit - 200 + late)], "hud": g_exit,
              "gui": h_exit - 60}
    env = env_for("host", host_dir, assets, mod, 0, "", sc["host_script"], host_extra, timing, host_place)
    for k in [k for k in env if k.upper().startswith(("TSSM_CHILD_", "TSSM_LOCAL_"))] + [
            "TSSM_COOP", "TSSM_COOP_PORT", "TSSM_COOP_BIND", "TSSM_COOP_PASSWORD"]:
        env.pop(k, None)
    # Discovery in the loopback test mode, where a loopback host could be
    # visible: local co-op's host must still never answer (lan_silence).
    lan_port = a.port + LAN_PORT_OFFSET
    env["TSSM_COOP_LAN_PORT"] = str(lan_port)
    guest = {
        "TSSM_INPUT": sc["guest_script"],
        "TSSM_EXIT_AFTER": str(g_exit),
        "TSSM_CAPTURE_DIR": os.path.join(guest_dir, "frames"),
        "TSSM_CAPTURE_FRAMES": ",".join(str(f) for f in (g_exit - 400, g_exit - 200)),
        "TSSM_COOP_NAME": "Guest",
        "TSSM_COOP_LOG": str(LOG_EVERY),
        "TSSM_COOP_SYNC_CAPTURE": "%d:%d" % (SYNC_EVERY_MS, SYNC_COUNT),
        "TSSM_GUI_OPEN_AT": str(g_exit - 60),
        "TSSM_GUI_TAB": "coop",
        "TSSM_GUI_CAPTURE": os.path.join(guest_dir, "frames", "coop_tab.png"),
        "TSSM_GUI_CAPTURE_FRAMES": str(GUI_AFTER),
        "TSSM_GUI_HUD_CAPTURE": "%d:%s" % (g_exit - 100, os.path.join(guest_dir, "frames", "hud.png")),
        "TSSM_TEST_PLACE_CAMERA": "reset",
    }
    guest.update(guest_extra)
    for k, v in guest.items():
        env["TSSM_CHILD_" + k] = v
    config = os.path.join(host_dir, "config.ini")
    config_text = open(config, "rb").read()
    # An (empty) mods folder, so both copies have a mod log to write.
    mods_dir = env.get("TSSM_MODS_DIR", "")
    if mods_dir:
        os.makedirs(mods_dir, exist_ok=True)

    rows = []

    def row(what, ok, detail=""):
        rows.append((what, ok, detail))

    procs = []
    watch = FirewallWatch(lambda: [p.kill() for p in procs])  # Player 2 closes on the pipe's end
    o = open(os.path.join(host_dir, "log.txt"), "wb")
    e = open(os.path.join(host_dir, "stderr.txt"), "wb")
    proc = subprocess.Popen([exe, "--local-coop"], cwd=host_dir, env=env, stdout=o, stderr=e)
    procs.append(proc)
    log1 = os.path.join(host_dir, "log.txt")
    deadline = time.time() + a.timeout
    events = {}
    silence = {}
    threading.Thread(target=lan_silence, args=(lan_port, log1, silence, deadline - 30), daemon=True).start()

    # Player 1 appended this build's missing settings at its start; put the
    # file back as it was, missing them, before Player 2's copy reads it:
    # read only, Player 2's must not add them.
    hosted = wait_for(log1, LOCAL_HOSTED.pattern, deadline, proc)
    if hosted:
        events["config_back"] = clock_ms()
        open(config, "wb").write(config_text)
        if mods_dir:
            events["mods_state"] = file_state(mods_dir)
    config_state = (open(config, "rb").read(), os.stat(config).st_mtime_ns)
    if variant == "local-bringback" and hosted:
        ids = wait_for(log1, LOCAL_IDS.pattern, deadline, proc)
        nettest = os.path.join(os.path.dirname(exe), "tssm_nettest.exe")
        if ids:
            r = subprocess.run([nettest, "--wrong-proofs", "127.0.0.1:%s" % hosted.group(1), ids.group(1),
                                ids.group(2), "5"], capture_output=True, text=True, timeout=60)
            events["proofs"] = r.stdout.strip()
            events["proofs_at"] = clock_ms()
            print(r.stdout.strip())

    started = wait_for(log1, LOCAL_STARTED.pattern, deadline, proc)
    child = Child(int(started.group(1)), exe) if started else None
    if child is not None and not child.ours:
        print("process %d is not our build; it is left alone" % child.pid)
    if child is not None and variant in ("local-close-guest", "local-close-host", "local-kill-guest",
                                         "local-kill-host"):
        placed = wait_for(p2_log_path, LOCAL_PLACED.pattern, deadline)
        time.sleep(2.0)
        events["act_at"] = clock_ms()
        if variant == "local-close-guest":
            events["acted"] = post_close(child.pid)
            child.wait(20)
        elif variant == "local-close-host":
            events["acted"] = post_close(proc.pid)
        elif variant == "local-kill-guest":
            child.kill()
            events["acted"] = child.ours
        else:
            proc.kill()  # our own copy
            events["acted"] = True
        events["placed_first"] = placed is not None
    elif child is not None and variant == "local-close-host-hung":
        stalled = wait_for(p2_log_path, LOCAL_STALLED.pattern, deadline)
        time.sleep(2.0)
        events["act_at"] = clock_ms()
        events["acted"] = stalled is not None and post_close(proc.pid)

    code1 = None
    try:
        code1 = proc.wait(max(1, deadline - time.time()))
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    events["p1_end"] = clock_ms()
    o.close()
    e.close()
    code2 = child.wait(max(1, deadline - time.time() + 15)) if child is not None else None
    if child is not None and code2 is None:
        print("Player 2's copy still runs after the test; ending it (our own)")
        child.kill()
        child.wait(10)
    # A second child (Bring back) is waited for through Player 1's log.
    host = read_text(log1)
    starts = [m for m in LOCAL_STARTED.finditer(host)]
    later = []
    for m in starts[1:]:
        c2 = Child(int(m.group(1)), exe)
        if c2.wait(30) is None:
            c2.kill()
            c2.wait(10)
            later.append("process %s ended by the test" % m.group(1))
    fw = watch.done()
    guest_log = read_text(p2_log_path)
    guest_prev = read_text(os.path.join(profile, "last_run.prev.log"))

    row("no Windows Firewall prompt while the copies ran", not fw, "; ".join("%s %r" % (c, t) for _, c, t in fw))
    row("local co-op's host is never listed: no answer to the questions (waiting and joined), not visible",
        silence.get("asked", 0) >= 4 and silence.get("answers", 1) == 0 and
        "coop: lan: visible on this network" not in host,
        "%s questions, %s answers%s" % (silence.get("asked"), silence.get("answers"),
                                        "; " + silence["error"] if "error" in silence else ""))
    row("no join code on screen in local co-op (either game's HUD)",
        "coop hud: the join code is shown" not in host + guest_log + guest_prev)
    hosted_all = [int(p) for p in LOCAL_HOSTED.findall(host)]
    row("Player 1 hosted on 127.0.0.1 at a port the system picked (not 2171)",
        bool(hosted_all) and all(p >= 1024 and p != 2171 for p in hosted_all), "ports %s" % hosted_all)
    row("Player 1 started Player 2's game in %s" % scene, started is not None and started.group(2) == scene,
        "process %s" % (started.group(1) if started else None))
    joined = LOCAL_JOINED.search(guest_log if variant != "local-bringback" else guest_log)
    row("two logs: Player 1's and Player 2's own (%s)" % os.path.relpath(p2_log_path, out),
        "coop: local: this is Player 2's game" in guest_log and os.path.abspath(p2_log_path) != os.path.abspath(log1),
        "")
    no_tab = "joining needs the host's password" not in guest_log + guest_prev
    gone = [m.groups() for m in LOCAL_GONE.finditer(host)]
    p2_dir_saves = []
    for root, _, files in os.walk(os.path.join(profile, "saves")):
        p2_dir_saves += [f for f in files if f != "coop_files.cache"]
    row("Player 2's saves folder has no save (its files cache aside)", not p2_dir_saves, str(p2_dir_saves))
    now_cfg = (open(config, "rb").read(), os.stat(config).st_mtime_ns)
    row("the run's config.ini, missing settings, unchanged by Player 2's game (bytes and time)",
        hosted is not None and now_cfg == config_state and started is not None and
        events.get("config_back", 1 << 62) < int(started.group(3)),
        "put back at clock ms %s, Player 2 started at %s" % (events.get("config_back"),
                                                            started.group(3) if started else None))
    p2ini = os.path.join(profile, "player2.ini")
    keys = ini_keys(p2ini)
    row("player2.ini holds only Player 2's own settings", os.path.exists(p2ini) and set(keys) <= PLAYER2_KEYS,
        str(keys))
    closed_by_script = variant in ("local-close-host", "local-kill-host", "local-close-host-hung")
    row("Player 1 exited by itself at frame %d" % h_exit if not closed_by_script else "Player 1's copy closed",
        (code1 == 0 and "TSSM_EXIT_AFTER reached" in host) if not closed_by_script else events.get("acted", False),
        "exit %s" % code1)

    if variant == "local":
        # Both clocks are this PC's monotonic one.
        start_to_join = int(joined.group(1)) - int(started.group(3)) if joined and started else None
        row("Player 2 joined within 10 s of its start, with no password asked",
            start_to_join is not None and start_to_join <= 10000 and no_tab,
            "%s ms after its start, %s ms after its first frame" % (
                start_to_join, joined.group(2) if joined else None))
        own_mods_log = os.path.join(profile, "mods.log")
        row("Player 2's mod log is in its own folder; Player 1's mods folder unchanged after Player 2 started",
            os.path.exists(own_mods_log) and events.get("mods_state") is not None and
            events["mods_state"] == file_state(mods_dir), "%s; %s" % (
                os.path.relpath(own_mods_log, out), sorted(file_state(mods_dir)) if mods_dir else None))
        m = LOCAL_PLACED.search(guest_log)
        d = math.dist([float(m.group(i)) for i in (2, 3, 4)], [float(m.group(i)) for i in (5, 6, 7)]) if m else 99
        row("Player 2 was put beside Player 1", m is not None and d <= 3.0,
            "%s, %.2f units from Player 1" % (m.group(1), d) if m else "")
        row("Player 2's game exited by itself at its frame %d, and Player 1 saw it leave" % g_exit,
            "TSSM_EXIT_AFTER reached" in guest_log and any(t == "Player 2 left" and c == "0" for t, _, c, _ in gone),
            str(gone))
        # The M1 and M2 checks of the online runs.
        row("Player 1 connected", "coop: connected (hosting)" in host)
        row("Player 2 connected", "coop: connected (joined)" in guest_log)
        sw = re.search(r"coop: switched to (\w+)", guest_log)
        other = sw.group(1) if sw else "?"
        hostchar = "SpongeBob" if other == "Patrick" else "Patrick"
        row("Player 2 took the other character", sw is not None, other)
        row("Player 1 draws Player 2 as %s" % other, "coop: the partner is drawn as %s" % other in host)
        row("Player 2 draws Player 1 as %s" % hostchar, "coop: the partner is drawn as %s" % hostchar in guest_log)
        n1, p1 = compare(host, guest_log)
        n2, p2 = compare(guest_log, host)
        row("Player 1 shows what Player 2 sent", n1 > 20 and not p1, "%d samples; %s" % (n1, "; ".join(p1[:3])))
        row("Player 2 shows what Player 1 sent", n2 > 20 and not p2, "%d samples; %s" % (n2, "; ".join(p2[:3])))
        hs = {s: (n, t) for s, n, t in (m.groups() for m in TABLE.finditer(host))}
        gs = {s: (n, t) for s, n, t in (m.groups() for m in TABLE.finditer(guest_log))}
        row("both made the same table of shared things in %s" % scene, scene in hs and hs.get(scene) == gs.get(scene),
            "host %s, guest %s" % (hs.get(scene), gs.get(scene)))
        row("Player 2 applied Player 1's snapshots and drew its world",
            guest_log.count("coop: world got") > 50 and guest_log.count("coop: world shown") > 20,
            "%d got, %d shown" % (guest_log.count("coop: world got"), guest_log.count("coop: world shown")))
        moments, pairs, worst, where = compare_syncs(host, guest_log, None, {})
        row("same places at the same moments (sync captures, <= 0.25)",
            moments >= 5 and pairs >= 10 and worst <= 0.25,
            "%d moments, %d positions, worst %.3f %s" % (moments, pairs, worst, where))
        checked, worst2, where2, skipped = compare_shown(host, guest_log, None)
        row("everything Player 2 drew is where Player 1 had it (<= 0.25)", checked >= 50 and worst2 <= 0.25,
            "%d positions, worst %.3f %s" % (checked, worst2, where2))
        moved = re.findall(r"coop: pose (?:check|outside its samples): .*", guest_log)
        row("Player 2 posed Player 1's things, and nothing moved them after", not moved, "; ".join(moved[:2]))
        for who, log in (("Player 1", host), ("Player 2", guest_log)):
            bad = [l for l in log.splitlines() if "unknown animation state" in l or "protocol errors" in l or
                   "timed out" in l or "connection was lost" in l or "is not this level" in l]
            row("%s: no unknown animation, protocol error or timeout" % who, not bad, bad[0].strip() if bad else "")
        frames = os.listdir(os.path.join(guest_dir, "frames"))
        sync = [f for f in frames if f.startswith("sync_")]
        row("Player 2's frames, Co-op tab and HUD captured",
            "coop_tab.png" in frames and "hud.png" in frames and len(sync) >= 5, "%d sync" % len(sync))
        hframes = os.listdir(os.path.join(host_dir, "frames"))
        row("Player 1's frames captured", "coop_tab.png" in hframes and "hud.png" in hframes)
    else:
        row("Player 2 joined", joined is not None or "coop: connected (joined)" in guest_prev,
            "%s ms after its first frame" % (joined.group(2) if joined else "?"))
    p2_end = child.ended_at if child is not None else None

    if variant == "local-close-guest":
        g = [x for x in gone if x[0] == "Player 2 left"]
        at = int(g[0][3]) if g else None
        row("Player 2's window closed: its game exited cleanly", events.get("acted") and code2 == 0 and
            "window closed -- exiting" in guest_log, "exit %s" % code2)
        row("Player 1 noticed within 2 s: \"Player 2 left\"", at is not None and p2_end is not None and
            abs(at - p2_end) <= 2000, "noticed at %s, ended at %s" % (at, p2_end))
        row("Player 1 offers Bring back / Play alone, and plays on alone",
            "TSSM_EXIT_AFTER reached" in host and "coop: stopped" in host)
    elif variant == "local-kill-guest":
        g = [x for x in gone if x[0].startswith("Player 2's game stopped unexpectedly")]
        at = int(g[0][3]) if g else None
        row("Player 1 noticed the crash within 2 s", at is not None and getattr(child, "killed_at", None) is not None
            and at - child.killed_at <= 2000, "noticed at %s, ended at %s" % (at, getattr(child, "killed_at", None)))
        row("and said where Player 2's crash report goes", "Player 2's crash report, if any:" in host)
    elif variant == "local-close-host":
        said = re.search(r"coop: local: (.*); closing this game", guest_log)
        row("Player 2's game closed by itself on the pipe's end, before Player 1's ended",
            code2 == 0 and p2_end is not None and p2_end - events["p1_end"] <= 2000 and said is not None and
            "Player 1's game closed" in said.group(0) and "Player 2's game closed with this one" in host,
            "%s; %s ms after; exit %s" % (said.group(1) if said else None,
                                         (p2_end - events["p1_end"]) if p2_end else None, code2))
    elif variant == "local-kill-host":
        # Ended at once: the pipe's end, or the job when that comes first.
        said = re.search(r"coop: local: (.*); closing this game", guest_log)
        row("Player 2's game ended with Player 1's, within 2 s", code2 is not None and p2_end is not None and
            p2_end - events["p1_end"] <= 2000, "%s; %s ms after; exit %s" % (
                said.group(1) if said else "ended by the job", (p2_end - events["p1_end"]) if p2_end else None, code2))
    elif variant == "local-close-host-hung":
        row("Player 2's game stopped before Player 1's window was closed", events.get("acted", False) and
            "TSSM_TEST_STALL" in guest_log)
        row("Player 1 waited 2 s for it, then left it to end with Player 1's game",
            "Player 2's game did not close within 2 s; it ends with this one" in host)
        row("Player 2's game ended with Player 1's, within 2 s (nothing left running)",
            code2 is not None and p2_end is not None and p2_end - events["p1_end"] <= 2000 and not later,
            "%s ms after; exit %s" % ((p2_end - events["p1_end"]) if p2_end else None, code2))
    elif variant == "local-hang-guest":
        row("Player 1 waited for Player 2's stalled game", "coop: the partner's game has run no frame for" in host)
        row("after a minute the session closed (Player 2's own game ended it)",
            "the session is closed (DC_STALLED)" in guest_log and
            "disconnected: the other player's game stopped for over a minute" in host)
        row("Player 1 said Player 2's game is out of the session and End would end it",
            "Player 2's game still runs but is out of the session" in host)
        row("Player 2's game, running again, closed by itself, and Player 1 saw it leave",
            "out of Player 1's game" in guest_log and any(t == "Player 2 left" for t, _, _, _ in gone), str(gone))
    elif variant == "local-end":
        row("End local co-op asked Player 2's game to close; it did, by itself", "end pressed" in host and
            "Player 1 ended local co-op; closing this game" in guest_prev + guest_log and
            any(t == "Player 2's game closed" and c == "0" for t, _, c, _ in gone), str(gone))
        row("Bring Player 2 back hosted again (a new port) and started a new Player 2, which joined",
            "bringback pressed" in host and len(hosted_all) == 2 and hosted_all[0] != hosted_all[1] and
            len(starts) == 2 and "coop: local: joined Player 1's game" in guest_log, "ports %s; %s" % (hosted_all, later))
    elif variant == "local-end-hung":
        row("End local co-op: Player 2's hung game did not answer, and was ended after 5 s",
            "end pressed" in host and "did not close within 5 s; ending it" in host and
            any(t.startswith("Player 2's game did not close by itself") for t, _, _, _ in gone), str(gone))
    elif variant == "local-bringback":
        proofs = events.get("proofs", "")
        row("five wrong passwords from 127.0.0.1 before Player 2 joined: four refused, then blocked",
            proofs.count("wrong join code or password") == 4 and proofs.count("blocked") == 1 and
            events.get("proofs_at", 1 << 62) < (int(started.group(3)) if started else 0), proofs.replace("\n", "; "))
        row("so the first Player 2 was turned away (blocked) and closed",
            "could not join Player 1's game" in guest_prev and
            any(t.startswith("Player 2's game closed before it joined") for t, _, _, _ in gone), str(gone))
        row("Bring Player 2 back hosted again on a new port, and the new Player 2 joined from 127.0.0.1",
            "bringback pressed" in host and len(hosted_all) == 2 and hosted_all[0] != hosted_all[1] and
            "coop: local: joined Player 1's game" in guest_log, "ports %s; %s" % (hosted_all, later))
        m = LOCAL_PLACED.search(guest_log)
        row("and was put beside Player 1", m is not None, m.group(1) if m else "")

    width = max(len(r[0]) for r in rows)
    for what, ok, detail in rows:
        print("%-*s  %s  %s" % (width, what, "PASS" if ok else "FAIL", detail))
    passed = all(r[1] for r in rows)
    print("\n%s: %d of %d (%s)" % ("PASS" if passed else "FAIL", sum(1 for r in rows if r[1]), len(rows), out))
    sys.stdout.flush()
    return passed, out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--assets", default=os.environ.get("TSSM_ASSETS"))
    ap.add_argument("--exe", required=True, help="our own tssm.exe (never a player's build)")
    ap.add_argument("--guest-exe", help="another build for the guest (e.g. the 32-bit one of the same "
                    "commit, to check that 32-bit and 64-bit games play together)")
    ap.add_argument("--out", required=True, help="a folder for this run's two copies")
    ap.add_argument("--scene", choices=sorted(SCENES), default=None,
                    help="BB02 by default; DE01 for --stall-all and --drag-all")
    ap.add_argument("--port", type=int, default=24650)
    ap.add_argument("--variant", choices=("normal", "wrong-password", "wrong-version", "wrong-files", "no-answer",
                                          "kick", "blackout", "netsim", "follow", "stay", "stall-host",
                                          "stall-guest", "drag-host", "drag-guest", "combat", "respawn",
                                          "host-death", "bowl", "host-paused", "levels", "look", "persist", "save", "own-show",
                                          "look-flood", "replay-show", "title", "tab") + LAN_VARIANTS + FIX_FIGHTS +
                    LOCAL_VARIANTS,
                    default="normal")
    ap.add_argument("--fight", choices=sorted(FIGHTS), default=None,
                    help="M3: run the stall, drag or netsim variant over that fight (DE01; bowl: TT01)")
    ap.add_argument("--stall-frame", type=int, default=0,
                    help="stall and drag variants: the frame the stall starts at (%d by default)" % STALL_FRAME)
    ap.add_argument("--stall", type=float, default=5.0, help="stall and drag variants: seconds the game thread "
                    "stops (more than 60: past the session's limit)")
    ap.add_argument("--stall-all", action="store_true", help="the stall variants of --scene: host and guest, "
                    "1, 5 and 20 s, then 65 s (past the limit)")
    ap.add_argument("--drag-all", action="store_true", help="the drag variants of --scene: host and guest, "
                    "1, 5 and 20 s (takes the mouse pointer meanwhile)")
    ap.add_argument("--hitch", default=None, help="blackout variant: [FRAME:]SEC, the guest's own game thread "
                    "stops for SEC seconds at FRAME (250 frames into the cut by default) while the cable is out")
    ap.add_argument("--blackout-all", action="store_true", help="M5: the blackout variant of --scene (DE01 by "
                    "default), 5 and 15 s, and 15 s with a 1.3 s --hitch")
    ap.add_argument("--join-all", action="store_true", help="M5: the join-flow variants: wrong-password, "
                    "wrong-version, wrong-files, no-answer, kick")
    ap.add_argument("--local-all", action="store_true", help="local co-op (docs/MULTIPLAYER.md 12): local in BB02 "
                    "and DE01, then every lifetime variant in DE01")
    ap.add_argument("--all", action="store_true", help="every scene and variant in turn (%s)" %
                    ", ".join("%s %s" % m for m in MATRIX))
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--phone", default=None, help="WxH:SCALE: every copy's window and [gui] scale, with a touch "
                    "screen's sizes and words (TSSM_GUI_TOUCH=1), to look at the tab as a phone lays it out")
    a = ap.parse_args()
    if a.phone:
        global PHONE
        size, scale = a.phone.split(":")
        w, h = size.lower().split("x")
        PHONE = (int(w), int(h), scale)
    if a.scene is None and a.variant in LOCAL_VARIANTS:
        a.scene = "BB02" if a.variant == "local" else "DE01"
    if a.scene is None:
        fight = FIGHTS.get(a.variant) or a.fight
        a.scene = ("DE01" if (a.stall_all or a.drag_all or a.blackout_all or a.variant == "blackout") else
                   FIGHT_SETUPS[fight].get("scene", "DE01") if fight else "BB02")
    if not a.assets or not os.path.exists(os.path.join(os.path.abspath(a.assets), "sb04.ini")):
        sys.exit("--assets: no sb04.ini in %s" % a.assets)
    for path in (a.exe, a.guest_exe or a.exe):
        if not os.path.exists(os.path.abspath(path)):
            sys.exit("--exe / --guest-exe: %s not found" % path)
    if a.stall_all:
        todo = [(a.scene, "stall-%s" % side, sec) for sec in (1.0, 5.0, 20.0, 65.0) for side in ("host", "guest")]
    elif a.drag_all:
        todo = [(a.scene, "drag-%s" % side, sec) for sec in (1.0, 5.0, 20.0) for side in ("host", "guest")]
    elif a.blackout_all:
        todo = [(a.scene, "blackout", sec) for sec in (5.0, 15.0)] + [(a.scene, "blackout+hitch", 15.0)]
    elif a.local_all:
        todo = [(s, v, a.stall) for s, v in LOCAL_ALL]
    elif a.join_all:
        todo = [("BB02", v, a.stall) for v in ("wrong-password", "wrong-version", "wrong-files", "no-answer", "kick")]
    elif a.all:
        todo = [(s, v, a.stall) for s, v in MATRIX]
    else:
        todo = [(a.scene, a.variant, a.stall)]
    results = []
    hitch = a.hitch
    for scene, variant, sec in todo:
        a.stall = sec
        label = variant + (" %gs" % sec if variant.startswith(("stall", "drag", "blackout")) else "")
        a.hitch = hitch
        if variant == "blackout+hitch":
            variant, a.hitch = "blackout", hitch or "1.3"
        many = a.all or a.stall_all or a.drag_all or a.blackout_all or a.join_all or a.local_all
        if FIREWALL_SEEN:
            print("\nA Windows Firewall prompt was seen; the remaining runs are not started")
            results.append((scene, label, False, "not run"))
            break
        if many:
            print("\n=== %s %s" % (scene, label))
        results.append((scene, label) + (run_local if variant in LOCAL_VARIANTS else run)(a, scene, variant))
    if a.all or a.stall_all or a.drag_all or a.blackout_all or a.join_all or a.local_all:
        print("\nAll runs:")
        for scene, variant, ok, out in results:
            print("  %-5s %-15s %s  %s" % (scene, variant, "PASS" if ok else "FAIL", out))
    return 0 if all(r[2] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
