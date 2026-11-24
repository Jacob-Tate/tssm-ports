# Online co-op (two players)

This document is the design for two-player online co-op in the PC port. One
player hosts, the other joins by IP address and port, and both play in the
same level at the same time. It covers the architecture, the milestones, the
exact scope of the first milestone, the security rules every milestone keeps,
and what the two players need.

Status: all of it is built on this branch. M1 (ghost buddy), M2 (shared
enemies and objects, with stall tolerance: 5.11), M3 (combat, damage and death
for both, and the host's pickups: section 6), M4 (playing through together:
level changes, cutscenes, saving, characters, costumes; section 7) and M5
(polish: the join flow, joining again after a drop, the connection on screen,
the guides `docs/HOSTING.txt` and `docs/JOINING.txt`; section 8). Protocol
version 6 moved co-op onto the port's 0.9 (8.12). Local co-op runs two copies
of the game on one PC that join each other by themselves (section 12).
Protocol version 7 makes hits, boxes and progress one game (section 13), and
protocol version 8 brings one co-op version to every device and finding games
on the home network (section 14). The networking core (sockets, handshake,
crypto, message parsing, rate limits) was written and fuzzed as a standalone
prototype before this document, and M1 imports it.

Contents

1. What co-op is, for the players
2. Architecture
3. Milestones
4. M1 in detail
5. M2 in detail
6. M3 in detail
7. M4 in detail
8. M5 in detail
9. Security requirements (every milestone)
10. What the two players need
11. Open decisions
12. Local co-op: two copies on one PC
13. Playing as one game: hits that land, and one progress (protocol version 7)
14. Co-op on the home network, across devices (protocol version 8)
15. Co-op through a relay server
16. Boss fights: the Frogfish (protocol version 9)

---

## 1. What co-op is, for the players

- One player is the **host**. They start the game, open the overlay (F1),
  go to the **Co-op** tab and press **Host**. The game shows a join code.
- The other player is the **guest**. They open the Co-op tab, type the host's
  address (for example `203.0.113.7:2171`) and the join code, and press
  **Join**.
- Each player runs their own copy of the game with their own game files and
  their own controller. Only game state and input travel between the two
  games. No game files, textures, sounds, saves or anything else from the game
  discs are ever sent.
- The host's game is in charge of the world: enemies, objects, progress,
  level changes and cutscenes. The guest's game shows that world and plays
  its own character in it. When the host leaves a level, the guest goes
  along, and both start the next one together.
- Each player plays the character they like, and can switch at any time
  (a tap of LB, or the Co-op tab): by default the guest takes the other
  character than the host's, but both may be SpongeBob, or both Patrick.
  Each can also wear a costume their own save has unlocked (section 7).

Co-op is off unless a player turns it on. With co-op off the game is exactly
the single-player game: no socket is opened and no co-op code runs.

---

## 2. Architecture

### 2.1 The choice: a host-authoritative listen server with one guest

The host's game is both a player and the server. It accepts exactly one
guest. There is no separate server program, no relay and no third-party
service; the guest connects straight to the host through a forwarded UDP port
(like a Minecraft server).

The host is the authority for everything that is shared:

- enemies (AI, position, animation, health, death),
- world objects (moving platforms, buttons, doors, destructibles),
- pickups, triggers, tasks and level progress,
- level changes and cutscenes,
- the save.

Each game is the authority for **its own player's movement**. The guest's
character runs on the guest's machine with the game's own, unchanged player
code, so jumping and running feel exactly like single-player with zero input
delay. The guest tells the host where it is; the host checks that this is
plausible (speed and teleport limits) but does not re-simulate it.

### 2.2 Why each game simulates its own player

The game was written for one player, and that assumption is everywhere:

- there is one player entity (`xglobals->___player_ent_dont_use_directly`),
  and one `globals.player` (about 7.7 KB of state: health, the three
  character settings, carry, lasso, bubble, checkpoint, inventory);
- about 200 file-scope variables in `zEntPlayer.cpp`, plus singletons for
  bungee, cruise bubble and out-of-bounds, plus statics in the lasso, thrown
  object and camera code;
- about 2,000 reads of `globals.player.` and about 680 uses of the player
  pointer across the game code, and about 180 reads of `globals.pad0`.

Two ways to get a second player were considered:

- **(a) Puppets (chosen).** Each machine runs its own player exactly as
  single-player does, and draws the other player as a *puppet*: a model whose
  position and animation come from the network. Shared things are owned by the
  host and replicated.
- **(b) Two real players in one simulation (rejected).** The host would run
  the guest's player too, which means saving and restoring all of the state
  above around a second player update every frame, and redirecting every pad
  read. It touches a large share of the game code (much of it byte-matched to
  the GameCube game), and the guest would feel the full network round trip on
  every jump. It would only make sense for split-screen on one PC, which is
  out of scope.

Lockstep or rollback (both machines simulating everything from shared inputs)
is also rejected: the game runs with a variable frame time and was never
written to be deterministic across machines, and an input delay would ruin
platforming.

The same split is what the successful decomp-based co-op projects use (the
local player stays the game's one player; remote players are driven from the
network), and each of them reached "see the other player" first, then shared
progress, then shared enemies.

### 2.3 The puppet

(M4 replaced the model choice below with a model of the partner's own: 7.5.
The driving of the puppet is unchanged.)

On each machine the partner is drawn with a **model that is already loaded
and not in use**. In the levels that load both characters (BB02, BB03, DE01,
TT01, TR02, TR03, JK01, JK02, PT01 in `sb04.ini`'s `ScenePlayerMapping`), the
game builds both SpongeBob's and Patrick's model chains with their full
animation tables, and only draws the active one. So if the host plays
SpongeBob, the host draws the guest with its own idle Patrick model, and the
guest draws the host with its own idle SpongeBob model. Each machine renders
from its own files.

How the puppet is driven, without running any of the player's game logic:

- The model's animation is set with `xAnimTableGetStateID` +
  `xAnimPlaySetState` (state ID from the network) and its time is written
  directly. The puppet's animation is **never** updated with
  `xAnimPlayUpdate` / `xModelUpdate`, so no transitions, state callbacks or
  animation effects (sounds, particles) run for it. `xAnimPlaySetState`
  leaves `LastTime` at -1, so stopping effects does nothing either.
- The pose is computed with `xModelEval`. During that call the puppet's
  `BeforeAnimMatrices` callbacks (the animation's own and its state's) are
  set to NULL and restored afterwards, because the player's callbacks read
  `globals.player`.
- Its matrix is built from the network position and yaw.
- It is drawn with `xModelRender` (model buckets) next to the local player.

From M2 on, the host also lets its enemies see the partner. There is no second
player entity: while one enemy updates, the host's own player entity is moved
to where the guest is (the *stand-in*, 5.4) if the guest is the nearer player,
and put back right after. On the guest, host-owned enemies and objects become
puppets in the same way as the partner (5.5).

### 2.4 What the guest simulates locally, by milestone

| Thing | M1 | M2 | M3 | M4 |
|---|---|---|---|---|
| Guest's own player (movement, animation, camera, HUD) | guest | guest | guest | guest |
| Enemies | each its own copy | host; guest shows puppets | host | host |
| Moving platforms, doors, buttons, destructibles | each its own copy | host | host | host |
| Destructibles (crates, barrels) | each its own copy | host's breaks shown; guest's own stay local | host | host (the guest's hits claimed since protocol 7, 13.2) |
| Guest's attacks on enemies | own copy | no effect yet | sent to host, host decides | host |
| Damage to the guest | own copy | none yet (enemies' attacks run only on the host, and miss the stand-in) | host decides, guest applies (touching an enemy: the guest's own collision) | host |
| Guest's death | own game | own game | respawn next to host, no reset | same |
| Pickups | each its own | each its own | host decides, counts them | host decides |
| Triggers, tasks | each its own | each its own | each its own | host decides |
| Level changes, cutscenes | each its own | guest follows host's level | same | host-driven, synced |
| Saving | each its own save | guest does not autosave in co-op | same | host's save only |

M1 is deliberately a "ghost buddy": both players are in the same level and
see each other move and animate, but each fights their own copy of the
world. It proves the whole pipeline (connection, security, port forwarding,
puppet animation) and is already fun.

### 2.5 Identity: what both games must agree on

Both games must give the same identifier to the same thing.

- **Build.** The handshake carries a build ID: the first 16 bytes of
  SHA-256 over a fingerprint of the code both games share and the protocol
  version. Up to protocol 7 the fingerprint was the git commit; from 8 it is
  the git object IDs of the game code and the co-op and protocol files, so
  the PC, Android and Switch branches of one co-op release give the same ID
  (14.1). Different builds are refused with a "different version" message.
  x86 and x64 builds of the same code can play together, because nothing is
  sent as a raw struct. The protocol version
  goes up when the messages change, and also when the game under them does
  in a way the two must not mix: version 6 is the move to the port's 0.9
  (8.12), so a 0.8 co-op copy and a 0.9 one refuse each other by version
  before anything else is compared, in both directions.
- **Game files.** The handshake carries an asset hash: SHA-256 over the
  sorted list of (lower-case relative path, size, SHA-256 of the content) of
  every file the game reads under the asset folder, plus the hashes of the
  enabled mods. It is cached in the save folder by (path, size, modified
  time), so it costs a few seconds once. Different files (another region,
  another release, different mods) are refused with a "game files differ"
  message. Only the hash is sent, never a file.
- **Things in a level.** From M2 on, shared things are named by a *net ID*:
  an index into a table the host and the guest both build from the level's
  own entity order. The tables match because the files match; each level
  change also re-checks that level's file hash. Net IDs are never pointers
  and never used as addresses; an unknown or out-of-range ID is rejected.
- **Animation states** are named by the game's own `xAnimState::ID`, looked
  up in the receiver's animation table. An ID that is not in the table is
  ignored (the puppet keeps its last pose).
- **Levels** are named by the game's 4-character scene ID (`BB02`).

### 2.6 Code layout and rules

- Network and co-op code lives in `src/SB/Core/pc` (files `iNet*` and
  `iCoop*`) and `src/SB/Core/pc/gui/iGuiCoop.cpp`.
- The game code is touched only at a few choke points, each inside
  `#ifdef PLATFORM_PC` and each a single call into `iCoop*` that returns at
  once when co-op is off:
  - M1: after the player update in the game loop (`zGame.cpp`), and after
    the player render in `zSceneRender` (`zScene.cpp`).
  - M2: the enemy update loops (`zMODEnemy::Timestep` / `PostTimestep` in
    `zNMECommon.cpp`), enemy damage (`zNMECommon::Damage`,
    `zNMETiki::Damage`), the scene's update loop (`zSceneUpdate`, for
    platforms), the update cull (`xUpdateCull_DistanceSquaredCB`), the
    player's damage and event entry points (`zEntPlayer.cpp`) and the
    autosave (`zSaveLoadAutoSaveUpdate`). Section 5.8 lists them.
  - M3: the enemy damage functions (`zNMECommon::Damage`,
    `zNMETiki::Damage`, which every player attack ends in),
    `zEntPlayer_PowerupLevel`, the player's damage functions and event
    handler, the hazard update loop, the end of the player's defeat, the
    pickup check. Section 6.6 lists them.
  - M4: the level's start (`zGameUpdateMode`), the portal callback, the
    cutscene manager and the camera fly-through, the save by hand, the
    costume rule, the player's packs and model info. Section 7.7 lists them.
- The GameCube build must stay byte-identical: `main.dol` sha1
  `f772db7f38b9b55e27169822ed17184fa2dc2574`. Every hook is a `PLATFORM_PC`
  block that adds nothing to the GameCube code path. Rebuild the GameCube
  target and check the sha1 after every change to a game file.
- With co-op off, single-player behaviour is unchanged and
  `tools/regress.py` passes.
- Sockets are the operating system's own (Winsock2 on Windows, POSIX on
  Linux/Android, libnx's BSD sockets on the Switch, IPv4 only there) behind a
  small `iNetSock` interface. The random source is the system's too:
  `BCryptGenRandom`, `arc4random_buf` on Android, csrng (then the kernel's
  generator) on the Switch, `getrandom` or `/dev/urandom` elsewhere.
  No networking library is used. On Windows the only additions to the link
  are `ws2_32`, `bcrypt` and `iphlpapi` (the interface list for finding
  games on the home network, 14.2), all part of Windows.

### 2.7 Threading

Level loads block the game thread for seconds. So the socket and the session
live on their own I/O thread that keeps answering keepalives during loads. The
game thread and the I/O thread exchange messages through two small fixed-size
mailboxes (latest partner state, plus a bounded queue for events), protected by
a mutex. The game thread never touches the socket and the I/O thread never
touches game state.

---

## 3. Milestones

Each milestone is playable on its own and has a test that two copies on one
PC can run.

### M1: Ghost buddy

Both players in the same level see each other run, jump and animate, with
the guest playing the other character. Each has their own enemies and items.
Includes the whole network core with full security, the Co-op tab, config and
command-line options, and the two-copy test. Section 4 has the details.

Done when: the two-copy test passes, wrong password / wrong version / wrong
files are refused with a clear message, the network unit test and fuzz loop
pass, regress passes with co-op off, and the GameCube sha1 is unchanged.

### M2: Shared enemies and objects

Section 5 has the details as built; the plan was:

- **Follow the host's level.** When the host is in another level, the guest
  gets a "Go to host's level" button (and an auto-follow option): the guest
  loads the host's scene at the host's position, through a PC-owned portal
  whose scene and marker come from the host (not from the guest's own unlock
  state, which can differ).
- **Host snapshots.** The host sends a snapshot 30 times a second: for each
  enemy and each shared object, its net ID, position, yaw, animation state and
  time, and flags (active, visible, dead). Snapshots are deltas against the
  newest one the guest acknowledged, limited to 1,200 bytes, with the nearest
  things first and the rest rotated into later packets. They are unreliable
  (the newest wins); the guest draws them about 100 ms in the past and
  interpolates.
- **Guest side.** In the NPC update loop, enemies owned by the host skip
  their AI on the guest and take their pose from the snapshot (same puppet
  rules as 2.3). Moving platforms move by host time, so the guest can ride
  them.
- **Host side.** The partner gets a proxy entity with a bound. In the NPC
  loop, each enemy's "player" is set to the nearer of the host's player and
  the proxy, then restored, so enemies chase whoever is closer. The update
  cull also counts the proxy's position, so enemies near the guest do not
  freeze.
- Not yet: the guest's attacks do nothing to shared enemies.

Done when: in BB02 and DE01, the same enemy is in the same place with the same
animation on both screens (logged positions within 0.25 units at the same host
frame), enemies chase the nearer player, and the guest can ride a moving
platform (shown in PT01, whose lava pillars slide by themselves: 5.9). Added
during M2: either game can stop for up to a minute (its window held, a hitch,
a load) without the session dropping, and both are back in step within about
a second after (5.11).

### M3: Combat, damage and death for both

Section 6 has the details as built (with the host's pickups, planned for M4,
brought forward); the plan was:

- **Guest attacks.** The guest's attack paths send a hit claim (net ID,
  attack kind, the guest's position) instead of applying the hit. The guest
  plays the hit effect at once. The host checks the claim (target exists and
  is alive, within reach of that attack plus a margin, attack allowed for that
  character, rate limit) and applies it with the player pointer temporarily
  set to the proxy, so knockback and credit are right. Enemy deaths come only
  from the host, so there are never duplicate or ghost enemies.
- **Damage to the guest.** Enemy hits on the proxy are sent to the guest as a
  damage event, and the guest's game applies it to its own player with the
  game's own damage function. The guest's local contact damage from
  host-owned enemies is turned off, so a hit is never counted twice.
- **Death.** The guest's death never resets anything. The guest respawns in
  a bubble next to the host after a few seconds. The host's death behaves as
  in single-player (lives, level reset), and the guest is moved to the host's
  respawn point afterwards. Lives belong to the host.
- Later (M5): the host keeps a short history of enemy positions and checks
  the guest's hits as of the guest's ping.

Done when: the guest can defeat an enemy and the host sees it die once; an
enemy hit on the guest lowers the guest's health once; guest death and
respawn work without a reset on either side.
Done (6.7): all three, also through stalls of either game, a held window,
a lossy link and a 32-bit guest.

### M4: Level changes, cutscenes and progress

Section 7 has the details as built (with characters, costumes and the
partner's own model added, and triggers left for later); the plan was:

- **Level changes are host-driven.** The host's portal sends the scene,
  marker and the level's file hash. The guest loads it and says when it is
  ready; the host waits up to 10 seconds before resuming, showing a loading
  message.
- **Cutscenes.** Cutscenes started on the host play on both games; the host
  skips for both. The guest's player is frozen and the puppet hidden while one
  plays.
- **Pickups and triggers.** The guest sends "pickup ID" and "trigger ID
  enter / exit / action"; the host checks them (exists, not already taken,
  guest is within reach, rate limit) and fires them with the proxy as the
  sender. SpongeBob-only and Patrick-only triggers use the character of the
  player who entered.
- **Progress.** The host's save is the only one written. At level entry the
  host sends the level's state (collected items, opened doors, task states)
  as a list of IDs, and changes as reliable events; resending is harmless, so
  a reconnect is just a resync. The guest's game does not autosave in co-op
  and its own save is not changed.
- **Special levels.** A compatibility table marks each level as shared or
  spectate. Bosses, slide levels, driving levels and Spongeball start as
  spectate: the guest's player is hidden and frozen and the guest's camera
  follows the host.

Done when: the two players can go from BB02 into another level together and
back, watch a cutscene together, and the host's save has the items either
player collected.
Done (7.8): from BB01 into DE01 and on to DE02 together, both starting each
level at once; DE01's fly-through and a cutscene on both, skipped by the host
only; the pickups count on the host (6.5) and only the host's game saves.
Triggers and tasks stay each game's own (7.9).

### M5: Polish

(Built as section 8 describes: the join flow and its guides, joining again
after a drop, the connection on screen, settings kept, 32/64-bit together,
the cost per frame. The rest of this list stays a plan.)

- Reconnect within 60 seconds without losing the session; full resync.
- HUD: partner health, an arrow towards an off-screen partner, a
  "teleport to partner" button, ping graph. Quick emotes only; no free-text
  chat (it would be a new string surface).
- Hit rewind for the guest's attacks; interpolation tuning.
- An optional "Ask my router to open the port" button on Windows (UPnP via
  the NATUPnP COM API that ships with Windows; explicit button, off by
  default, mapping removed when hosting stops).
- Android: touch-friendly Co-op tab, close the socket when the app is
  suspended, check the INTERNET permission in our manifest.
- Switch: libnx BSD sockets and its random number source behind the same
  `iNetSock` / random interfaces.
- One-character levels: done in M4 instead (both play that level's
  character, 7.5).
- A costume put on in the middle of a level (7.4); triggers and tasks shared
  (7.9).

---

## 4. M1 in detail

### 4.1 Scope

In:

- The network core: `iNetSock` (Winsock2 and POSIX), bounded wire reader and
  writer, SHA-256 / HMAC-SHA256 / PBKDF2 / HKDF / constant-time compare /
  OS random, the handshake, sealed data packets with replay protection, rate
  limits, lockouts, timeouts.
- The co-op service: I/O thread, mailboxes, build ID and asset hash, status
  for the UI.
- The puppet: sending the local player's state 30 times a second, drawing the
  partner with the idle character model, interpolation, name tag.
- The guest playing the other character.
- The Co-op tab, the config section, the command-line options, the test
  environment variables.
- `tssm_nettest` (unit, loopback, hostile and fuzz-loop tests) and
  `tools/coop_test.py` (two copies on one PC).

Out (later milestones): any shared enemy, object, pickup, damage, death,
level change or cutscene; player-to-player collision; one-character levels
(the partner shows as a name tag only); UPnP; reconnect; Android and Switch
testing (the code must compile for Android, as the prototype does).

### 4.2 Game behaviour in M1

- **Co-op off:** the hooks return on their first line. No socket, no thread,
  no allocation.
- **Hosting / joining** can start at any time. The puppet appears when both
  games are in gameplay in the same scene.
- **Characters.** (M4 changed this: 7.4.) The host never switches automatically. The guest, when
  connected, in the same scene as the host, in a level that has both
  characters loaded, and not in a special mode, switches its own player to the
  other character by sending its player the game's own
  `eEventPlrSwitchCharacter`. This also covers the host using a bus stop: the
  guest follows. While both players are the same character, the puppet is
  hidden (the idle model would be the wrong body).
- **One-character levels** (bosses, slides, driving): no idle model exists,
  so the partner is shown only as a name tag at their position.
- **Hidden partner.** The puppet is not drawn when the partner reports
  "not visible" or a special mode (bungee, cruise bubble, out of bounds,
  slide, car, Spongeball, boulder), when either game is in a cutscene, or when
  no state has arrived for 3 seconds.
- **Pause.** Each game pauses on its own. A paused partner stands still.
- **Saves.** M1 does not touch saving; each player keeps playing their own
  save.
- **Interpolation.** The partner is drawn 100 ms in the past, interpolating
  between the two surrounding states (position, yaw, and animation time when
  the state is the same). After the newest state it extrapolates with the
  velocity for at most 100 ms, then holds. A jump of more than 10 units
  between two states snaps instead of sliding.
- **Disconnect.** After 10 seconds without packets, or a DISCONNECT, the
  puppet is removed and a short notice says the partner left.

### 4.3 Files

New (all PC-only):

| File | What it holds |
|---|---|
| `src/SB/Core/pc/iNetSock.h`, `iNetSockCommon.cpp`, `iNetSockWin32.cpp`, `iNetSockPosix.cpp` | UDP socket interface: init, open (dual-stack any-address or loopback), wait, receive (drops anything over 1,200 bytes or truncated), send, close, numeric-only address parse/format. Windows: `SIO_UDP_CONNRESET` off. |
| `src/SB/Core/pc/iNetWire.h` | Bounded little-endian Reader/Writer with sticky failure. |
| `src/SB/Core/pc/iNetCrypto.h`, `iNetCrypto.cpp` | SHA-256, HMAC-SHA256, PBKDF2, HKDF, constant-time compare, OS random (BCryptGenRandom / getrandom). |
| `src/SB/Core/pc/iNetProto.h`, `iNetProto.cpp` | Handshake packets and M1 payload messages: encode, strict parse, validate. |
| `src/SB/Core/pc/iNetSession.h`, `iNetSession.cpp` | Host and Client state machines: cookies, proofs, session keys, seal/open, replay window, reliable layer (kept for M2+, not accepted on the wire in M1), rate limits, lockouts, timeouts. |
| `src/SB/Core/pc/iCoop.h`, `iCoop.cpp`, `iCoopInternal.h` | The co-op service: settings (config, env, command line), I/O thread, mailboxes, build ID, status for the UI, `iCoopInit` / `iCoopShutdown` / `iCoopFrame` / `iCoopRenderPartner`. `iCoopInternal.h` is the seam between the platform half and the game-side half. |
| `src/SB/Core/pc/iCoopAssets.cpp` | The asset hash and its cache. |
| `src/SB/Core/pc/iCoopPuppet.cpp` | Reading the local player into PLAYER_STATE; driving, evaluating and drawing the partner model; the guest's character rule; the name-tag position. |
| `src/SB/Core/pc/gui/iGuiCoop.cpp` | The Co-op tab and the always-on co-op HUD layer (name tag, connect/disconnect notices). |
| `src/SB/Core/pc/tests/nettest.cpp` | `tssm_nettest`: crypto known-answer tests, wire tests, loopback sessions with simulated loss, hostile scenarios, a fixed-seed mutation fuzz loop. |
| `src/SB/Core/pc/tests/netfuzzcore.h`, `netfuzzcore.cpp` | The fuzz targets and their invariants, shared by `tssm_nettest`'s loop and the libFuzzer target. |
| `src/SB/Core/pc/tests/netfuzz.cpp` | Optional libFuzzer target (`TSSM_NETFUZZ=ON`; not part of regress). |
| `tools/coop_test.py` | Two copies on one PC through 127.0.0.1 (4.7). |
| `docs/MULTIPLAYER.md` | This document. |

Changed:

| File | Change |
|---|---|
| `src/SB/Game/zGame.cpp` | One `PLATFORM_PC` block after the player update in the game loop: `iCoopFrame(sTimeElapsed, isPaused);` (runs paused or not). |
| `src/SB/Game/zScene.cpp` | One `PLATFORM_PC` block right after the player render in `zSceneRender`, inside the model-bucket pass and with the player's light kit still on: `iCoopRenderPartner();` |
| `src/SB/Core/pc/gui/iGui.cpp`, `iGuiInternal.h` | A "Co-op" tab (`TAB_COOP`, between Mods and About). A HUD layer drawn every frame even when the overlay is closed, with ImGui's foreground draw list; it never takes input. |
| `src/SB/Core/pc/iConfigTable.cpp` | A `[coop]` section (4.6). |
| `src/SB/Core/pc/iSystem.cpp` | Call `iCoopInit` once the config is read and the asset and save folders are settled (it parses `--host`, `--join`, `--coop-name` through `iHostCommandLine`), and `iCoopShutdown` in `iSystemExit` (also registered with `atexit`, so `TSSM_EXIT_AFTER` and a quit say goodbye to the partner). |
| `CMakeLists.txt` | Add the sources; link `ws2_32` and `bcrypt` on Windows; add `tssm_nettest`. |
| `tools/regress.py` | Run `tssm_nettest` with the other unit tests. |
| `src/SB/Core/pc/README.md`, `PORTING.md`, `docs/GUI.md` | List the new files, the environment variables and the tab. |

### 4.4 Network messages in M1

All multi-byte fields are little-endian. Nothing is sent as a struct.

**Before the session (handshake).** Every packet starts with magic `TSCP`
(4 bytes), protocol version (u16) and type (u8). The version and HELLO's
nonce are at fixed offsets in every version, so any version can answer
"update your game".

| Packet | Direction | Body |
|---|---|---|
| HELLO (1) | guest to host | client_nonce[16], build_id[16], asset_hash[32], zero padding to exactly 1,200 bytes. Any other size gets no answer. |
| CHALLENGE (2) | host to guest | client_nonce echo, cookie[32], cookie_time u32, salt[16], iterations u32, host_nonce[16]. The host stores nothing: the cookie is HMAC(host secret, address, port, 10-second bucket, client_nonce). |
| AUTH (3) | guest to host | both nonces, cookie, cookie_time, build_id, asset_hash, name (u8 length + 1-16 chars of `A-Z a-z 0-9 space _ . -`), proof[32]. |
| WELCOME (4) | host to guest | session_id u32 (random), slot u8 (1), host name, host proof[32]. |
| REJECT (5) | host to guest | client_nonce echo, reason u8: VERSION, BUILD, ASSETS, FULL, PASSWORD, BLOCKED, BUSY. |

Password proof:

- K = PBKDF2-HMAC-SHA256(password, salt, iterations); 100,000 iterations by
  default. The guest refuses iteration counts outside 10,000-1,000,000, and
  derives K at most once per join attempt: a later CHALLENGE with another
  salt or count is ignored, so a fake host cannot keep it busy in PBKDF2.
- th = SHA-256("TSSM-coop/1" || build_id || asset_hash || client_nonce ||
  host_nonce || salt || iterations || name).
- Guest proof = HMAC(K, "client" || th). Host proof = HMAC(K, "host" || th ||
  session_id || slot). The guest checks the host's proof, so a fake host
  cannot pretend to be the real one. The password never crosses the wire.
- Session keys: HKDF(salt = client_nonce || host_nonce, key = K) with labels
  "tssm-coop c2h" and "tssm-coop h2c".

**In the session.** Every packet is
`[0x80][session u32][seq u32][ack u32][ack_bits u32][payload][tag 16]`, where
the tag is HMAC-SHA256 of everything before it with the sender's direction
key, cut to 16 bytes. The receiver checks, in order: the header, the session
ID, the sender's address, a quick replay pre-check, the tag (constant time),
the 64-packet replay window, the rate limit, and only then parses the payload.

The payload is a list of messages `[type u8][len u16][body]`, types in
ascending order, each at most once, no trailing bytes. M1 accepts only these
types; any other type (including those reserved for later milestones) is a
protocol violation:

| Type | Direction | Body |
|---|---|---|
| PLAYER_STATE (2) | both, 30 per second | 42 bytes, below |
| PING (7) | both, 1 per second | id u32 |
| PONG (8) | both | id u32 (echo) |
| DISCONNECT (9) | both | reason u8: 1 quit, 2 kicked, 3 timeout, 4 protocol error |

PLAYER_STATE (42 bytes in M1-M3; 43 from M4):

| Offset | Field | Type | Rule (otherwise the message is rejected) |
|---|---|---|---|
| 0 | slot | u8 | 0 = host, 1 = guest; must equal the sender's slot |
| 1 | frame | u32 | sender's frame counter; not newer than the last accepted one (serial-number compare) means "drop, newest wins" |
| 5 | scene | u32 | scene ID; each of the 4 bytes in `A-Z 0-9` |
| 9 | character | u8 | 0 SpongeBob, 1 Patrick |
| 10 | flags | u8 | bit 0 visible, bit 1 special mode, bit 2 paused, bit 3 in cutscene, bit 4 dead or respawning; bits 5-7 zero |
| 11 | position | f32 x 3 | finite, each within +-100,000 |
| 23 | yaw | u16 | 65,536 = full turn |
| 25 | velocity | i16 x 3 | hundredths of a unit per second |
| 31 | anim state | u32 | an `xAnimState::ID`; looked up in the receiver's table for that character; not found: keep the last pose (logged once) |
| 35 | anim time | f32 | finite, 0 to 600 |
| 39 | health | u8 | at most 32 |
| 40 | parts | u16 | bit i: the i-th model in the character's model chain is visible (so held items and accessories match) |
| 42 | model | u8 | M4: which of the game's 27 player models the player is drawn with (the game's own order), or 255 unknown; anything else rejected (7.5) |

Bandwidth: about 76 bytes per packet plus 28 bytes of UDP/IP headers, 30
times a second: about 3 KB/s each way.

Session timers: keepalive (PING) every second; partner hidden after 3 seconds
of silence; session closed after 10 seconds; the guest resends HELLO/AUTH
every 250 ms and gives up after 10 seconds. The keepalives come from the I/O
thread, so a game whose own thread stops (a held window, a load) keeps its
session; M2 adds `STALL` and a one-minute limit for that (5.11).

### 4.5 The Co-op tab and the HUD layer

The tab (overlay, F1 or the pad combo):

- **Mode:** Off / Host / Join.
- **Your name:** 1-16 characters (letters, digits, space, `_ . -`).
- **Host:**
  - Port (default 2171).
  - Password: by default a generated join code of 12 characters shown as
    `K7QX-M2PD-9TRW`, with "New code" and "Copy" buttons; or a password the
    host types, with a warning when it is short.
  - **Start hosting** / **Stop**.
  - While hosting: "Listening on UDP port 2171", this PC's local
    addresses ("forward UDP 2171 on your router to 192.168.1.23"), and a
    reminder that the guest needs the public address, which the router's
    status page shows. The game does not contact any service to find it.
  - "Wrong password attempts: N", **Kick** (ends the session and blocks that
    address until hosting stops).
- **Join:** address (`IP` or `IP:port`; numbers only, IPv4 or IPv6, no host
  names), password, **Join** / **Leave**.
- **Status:** Off / Waiting for a player / Connecting / Checking password /
  Connected / Refused: (reason in plain words) / No answer from the host.
- **Partner:** name, ping in ms (green under 100, yellow under 250, red
  above), character, level name (`zSceneGetLevelName`), "in your level" or
  "in another level".
- "Show partner name tag" checkbox.
- A note: "Co-op sends game state only. Each player uses their own game
  files. Traffic is protected against tampering but not encrypted."

Passwords and join codes ignore spaces and dashes and are not case-sensitive
(both sides normalise them the same way before the proof), so a code read out
over the phone works however it is typed. The ping shown is the round trip of
the once-a-second PING; only the echo of the newest PING counts.

The HUD layer, drawn when the overlay is closed and co-op is on:

- the partner's name above their head (projected with the game camera), or at
  the screen edge when off-screen;
- a short notice when the partner joins, leaves or the connection is bad.

The name is the only network string. It is drawn with
`ImGui::TextUnformatted` / `AddText` only, never used as a format string, a
path or a log format.

### 4.6 Config, environment and command line

`config.ini`, section `[coop]` (in `iConfigTable.cpp`):

| Key | Kind | Default | Meaning |
|---|---|---|---|
| `mode` | off / host / join | off | Start hosting or joining when the game starts. |
| `port` | int 1024-65535 | 2171 | UDP port to host on, and the default port when joining. |
| `address` | string | (empty) | Address to join (numeric). |
| `name` | string | (empty: "Player") | Your name. |
| `bind` | any / loopback | any | Host on every network, or only on this PC (tests). |
| `nametag` | bool | on | Show the partner's name tag. |
| `follow` | bool | on | When joining, go to the host's level by yourself (M2, 5.6). |
| `reconnect` | bool | on | When joining, join again by yourself for a minute after the connection drops (M5, 8.3). |
| `quality` | bool | on | The connection-quality dot and ping on the HUD (M5, 8.4). |
| `lan_visible` | bool | on | When hosting from the start (`mode = host`, `--host`), answer the questions of devices on the same home network (14.2). The tab's two host buttons set it for their hosting (14.3). |
| `show_code` | bool | on | While a host visible on the home network waits for a player, its join code at the bottom right of the picture (14.3). |

The password is **not** stored in `config.ini`. It is typed in the Co-op tab
for that session. When `mode` is `host`, the game generates a join code and
shows it in the tab; when `mode` is `join` and no password is set, the overlay
opens on the Co-op tab to ask.

Environment variables (they override the config; mainly for tests):

| Variable | Meaning |
|---|---|
| `TSSM_COOP` | off / host / join |
| `TSSM_COOP_PORT` | port |
| `TSSM_COOP_ADDR` | address to join |
| `TSSM_COOP_NAME` | name |
| `TSSM_COOP_PASSWORD` | password (tests and scripts) |
| `TSSM_COOP_BIND` | any / loopback |
| `TSSM_COOP_LOG` | log the local and partner state every N frames; with M2 also the shared world (5.9) and the traffic every 5 s |
| `TSSM_COOP_FOLLOW` | 0 / 1: `[coop] follow` |
| `TSSM_COOP_RECONNECT` | 0 / 1: `[coop] reconnect` (M5) |
| `TSSM_COOP_SYNC_CAPTURE` | `every_ms:count`: tests; capture a frame at the same moments of the host's clock on both copies (5.9) |
| `TSSM_COOP_NETSIM` | `delay_ms,jitter_ms,loss_pct`: simulate a bad connection (loopback only; ignored otherwise) |
| `TSSM_COOP_LAN_PORT`, `TSSM_COOP_LAN_TARGET` | tests: the home network's discovery on 127.0.0.1 only (14.2) |
| `TSSM_TEST_COOP_CODE` | tests: the 12 symbols every new join code is (`iCoopNewJoinCode`), so a test can type what "Host on this network" shows (14.3); only in the loopback test mode (`TSSM_COOP_LAN_PORT`); the log says once that a test code is in use, never the code |

Command line:

- `tssm --host` or `tssm --host=2171`
- `tssm --join=203.0.113.7:2171`
- `--coop-name=NAME`

There is no password option on the command line, because other programs on
the PC can read command lines.

### 4.7 Test plan

Each copy in a test gets its own copy
of a save folder (`TSSM_SAVE_DIR`), its own `TSSM_LOG` and `TSSM_CONFIG`.
Tests never use the players' own saves or play builds.

1. **Unit test, no GPU** (`tssm_nettest`, also run by `tools/regress.py`):
   - Crypto known-answer tests: SHA-256 (FIPS 180-4), HMAC-SHA256 (RFC 4231),
     PBKDF2-HMAC-SHA256, HKDF (RFC 5869).
   - Wire: numeric-only address parser, name rules, exact HELLO size, non-zero
     padding refused, every PLAYER_STATE rule above (NaN, out-of-range,
     reserved bits, wrong slot, bad scene byte, unknown character), unknown
     and later-milestone message types refused.
   - Real UDP on 127.0.0.1 with 10% loss each way: handshake completes,
     PLAYER_STATE flows both ways, newest wins.
   - Hostile cases: wrong password refused and then blocked while the right
     password still works; third player gets FULL; wrong build, files or
     version get the right refusal; forged cookie, oversized HELLO, replayed,
     tampered and wrong-address packets get silence or are dropped; HELLO
     floods from one and from many addresses stay within the caps; no reply
     before the session is larger than its request; a flood from one address
     does not starve other joiners; a fake host asking for too many
     iterations, or for a second key derivation in one join, or without a
     valid proof is ignored; a silent guest times out and the slot is reused.
   - A fixed-seed mutation fuzz loop over every parser and over live host and
     client sessions, checking that accepted input re-encodes byte for byte.
   - Once per milestone, by hand: the same test built with ASan + UBSan, and
     the libFuzzer target for a few minutes.

2. **Single-player unchanged:** `tools/regress.py` passes on both widths with
   co-op off, the run's logs contain no `coop:` lines, and the GameCube
   `main.dol` sha1 is `f772db7f38b9b55e27169822ed17184fa2dc2574`.

3. **Two copies on one PC** (`tools/coop_test.py`):
   - Both copies boot straight into BB02 with the same boot mod regress uses
     (`TSSM_MOD`: `BOOT=BB02`, `ShowMenuOnBoot=0`), with `TSSM_COOP_BIND=loopback`
     and a fixed test port.
   - Host: `TSSM_COOP=host`. Guest: `TSSM_COOP=join`,
     `TSSM_COOP_ADDR=127.0.0.1:<port>`. Both: `TSSM_COOP_PASSWORD` set to a
     value the script generates for that run, `TSSM_COOP_LOG=10`.
   - Scripted input (`TSSM_INPUT`, which replaces the controllers, so the
     copy behind the other still moves): the host stands still, then walks
     and jumps; the guest walks a circle and jumps.
   - `TSSM_CAPTURE_FRAMES` at fixed frames on both, and `TSSM_EXIT_AFTER`.
   - Automatic checks from the logs:
     - both report "connected" within 5 seconds of gameplay;
     - the guest switched to Patrick;
     - for every partner sample a copy logged, the other copy's own logged
       state for that frame number is within 0.05 units, with the same
       animation state ID (what each shows is what the other did);
     - no "unknown animation state" line, no protocol violation, no timeout;
     - both exited by themselves at the exit frame.
   - Frames: on the host's frames Patrick stands where the guest walked, and on
     the guest's frames SpongeBob stands where the host walked, each with a name
     tag. The first good set is reviewed by eye and stored as reference frames;
     later runs compare with the same perceptual check regress uses.
   - Variants of the same run:
     - `TSSM_COOP_NETSIM=80,20,5`: the puppet never jumps more than the
       snap distance except after a gap, and partner samples are never older
       than 300 ms;
     - wrong password on the guest: both logs show the refusal, and no puppet
       appears.

4. **By the players (no automation):** two PCs on the same home network
   (join by the LAN address), then over the internet through the forwarded
   port.

### 4.8 What M1 delivered

- `tssm_nettest`: 126 checks, about 4 s, run by `tools/regress.py`.
- `tools/coop_test.py`: the normal run, `--variant netsim` (80 ms, 20 ms
  jitter, 5% loss: about 190 ms ping in the Co-op tab) and
  `--variant wrong-password` pass. In the normal run each copy logs about 85
  partner states and every one is exactly the state the other copy logged as
  sent; the host draws the guest as Patrick and the guest draws the host as
  SpongeBob, each with the other's own idle model.
- The frame captures (`TSSM_CAPTURE_FRAMES`) are the game's render target,
  taken before the overlay is drawn, so the HUD layer's name tag is not in
  them. `tools/coop_test.py` also writes the window with the HUD layer
  (`TSSM_GUI_HUD_CAPTURE`, `hud.png`: the partner's name above their head, or
  at the picture's edge when they are behind the camera) and the Co-op tab
  (`TSSM_GUI_CAPTURE`, `coop_tab.png`); `TSSM_COOP_LOG` also logs the tag's
  place ("tag frame N at x y").
- Not in M1, as planned: shared enemies, objects, pickups, damage, death,
  level changes and cutscenes; player-to-player collision; reconnect; UPnP;
  testing on Android and Switch (the network code compiles for Android with
  the NDK; the port's Android manifest still needs the INTERNET permission,
  an M5 item).

---

## 5. M2 in detail

### 5.1 Scope

In:

- Enemies, moving platforms (with pendulums) and destructible objects are the
  host's whenever both players are in the same level. The host sends their
  state; the guest's copies stop simulating and show it, death effects
  included.
- Enemies chase the nearer player: on the host, an enemy whose nearer player is
  the guest updates with the host's player entity standing in at the guest's
  place.
- The host keeps enemies near the guest awake (the update cull).
- The guest follows the host's level.
- The guest does not autosave while joined.
- `SNAPSHOT` and `SNAP_ACK` on the wire (protocol version 2), with parsers,
  tests and fuzz targets.
- Stall tolerance (5.11; protocol version 3: `STALL` and the disconnect
  reason 5): either game's thread may stop for a while (its window held or
  dragged, minimised, a hitch, a load) and both come back in step within a
  second, with the session up.

Out (M3 and later): the guest's attacks and grabs reaching host-owned enemies,
enemy attacks reaching the guest, projectiles, hazards (fire, electric and
other damaging volumes, which are triggers), pickups, buttons and triggers,
platforms that the local player's weight or hits set off (falling, breakaway,
paddles: 5.2), cutscenes started by the host, bosses and other one-character
levels (5.10 lists what this means in play). The first M2 goal also named
hazards, pickups and interactive objects as the host's; they are left to M3
and M4 because each one changes what the guest's game does (a hit, an item, a
door), which needs the guest's actions to reach the host first.

### 5.2 Shared things and their net IDs

When a level is set up, each game makes the same table of shared things from
the level's own entities:

| Kind | Entities | What is shared |
|---|---|---|
| Enemy | the scene's NPCs of the enemy types (Tikis, critters, turrets, the robots: `NME_TYPE_TIKI` to `NME_TYPE_TIKI_STONE` and `NME_TYPE_CRITTER` to `NME_TYPE_BUCKOTRON_V7`) | position, rotation, animation state and time, shown / enabled, death effects |
| Mover | platforms, except the ones whose motion comes from the local player standing on or hitting them (teeter-totters, conveyor belts, springboards, falling and breakaway platforms, paddles), and pendulums | position, rotation, animation, shown / enabled, broken |
| Breakable | static objects with a destructible attached (crates, barrels) | broken |

Bosses, the hint-giving NPCs (Mindy and the SpongeBob / Patrick NPCs) and
everything else stay each game's own. A platform left to each game moves on
each screen by that player's own weight or hits (a falling platform falls under
the guest on the guest's screen only). In the test levels: BB02 shares 130
things (79 enemies, 49 movers, 2 breakables), DE01 245, TT01 538, PT01 288.

The table is sorted by the entity's asset ID (then kind), and the net ID is
the index in it. Its hash (FNV-1a over the kinds and IDs) and its length are
in every snapshot; a guest whose table differs ignores the snapshot and logs
it once. A net ID is only ever an index checked against the table's length,
never a pointer.

### 5.3 Snapshots

The host sends one `SNAPSHOT` with each of its `PLAYER_STATE`s (30 a second)
while the guest is in the host's level, in the same datagram. The guest
answers with `SNAP_ACK` (the newest snapshot it applied) in its own next
`PLAYER_STATE` datagram.

`SNAPSHOT` (type 3, host to guest only):

| Field | Type | Rule |
|---|---|---|
| scene | u32 | as in `PLAYER_STATE` |
| table | u32 | the table's hash |
| count of things | u16 | 1-1,024: the table's length |
| seq | u32 | not 0; it runs on through a session, level changes included |
| base | u32 | 0 (no base), or a seq before this one by at most 64 |
| host time | u32 | the host's clock in ms |
| flags | u8 | bit 0: complete (nothing that changed was left out); bits 1-7 zero |
| entries | u8 | 0-96 |
| entry | ... | ascending net IDs, each below the count of things |

Each entry is `id u16`, `mask u8` (not 0; bits 5-7 zero), then the fields the
mask names, in this order:

| Bit | Field | Rule |
|---|---|---|
| 0 | position f32 x 3 | finite, within +-100,000 |
| 1 | rotation i16 x 4 | a quaternion scaled to 32,767; no component -32,768; not all zero |
| 2 | animation u32 + time f32 | an `xAnimState::ID` (looked up, unknown ignored); time 0-600 |
| 3 | flags u8 | bit 0 shown, bit 1 enabled, bit 2 broken, bit 3 still (the animation is not running), bits 4-5 the death effects played (modulo 4); bits 6-7 zero |
| 4 | fade u8 | the model's alpha, 0-255 (on the wire, not sent yet: a model's alpha is mostly the camera's distance fade, each game's own) |

`SNAP_ACK` (type 6, guest to host only): `seq u32`, not 0.

`STALL` (type 10, either side; 5.11): `ms u16`, 1-65,535 (saturated): how long
the sender's game thread has run no frame. Sent by the sender's I/O thread
only. DISCONNECT gains reason 5, `STALLED`: the sender's game ran no frame for
a minute.

The rotation is the model matrix's rows with their lengths (the scale) taken
out; the receiver puts its own copy's scale back, and flips the first row for
a mirrored model as its own copy is.

**Deltas.** Both sides keep the last 32 snapshots as "what the guest
believes" about every thing. The host makes each snapshot against the newest
one the guest acknowledged (`base`), and puts in only what differs from it,
field by field. A thing the guest has never had in full (after joining, or
after a level change) is sent with all its fields. A thing sent since the base
and not yet acknowledged is sent again when it is anywhere else than that
sent value, even when it is back where the base had it, so the guest never
keeps showing a newer value that is no longer true. A guest that does not
have the base drops the snapshot and keeps acknowledging its newest; the host
then falls back to an older base or to none.

**Animation times.** An animation's time runs on by its state's own speed,
looping or stopping as the state does (`xAnimState` flags), on both sides
alike, so the time goes out only with a new state or when the guest's running
time would be off by more than 0.05 s (3 frames). A thing whose animation is
not running on the host (not updated because it is far, or stopped) carries the
"still" flag, so the guest holds it too.

**Room and rate.** A snapshot gets the room left in the datagram after the
`PLAYER_STATE` and a `PING` / `PONG` (1,100 bytes, 96 entries at most). Things
go in nearest to either player first, the ones that have waited longer moving
up; a thing more than 70 units from both players (the game's own update
distance) goes out every second snapshot, more than 140 units every fourth
(the guest draws it between what arrives), and what the guest has never had
and breaks go at once. The guest
moves only the things a snapshot carried.

**Bandwidth** (measured, `tools/coop_test.py`, the host's datagrams without
UDP/IP headers): the first snapshots carry the whole table in a few dozen
packets; after that the host sends 8-12 KB/s on average in BB02, DE01 and TT01
(538 things), 17 KB/s at most, a little more with packet loss (deltas against
older bases); PT01, whose many platforms all move at once, 18 KB/s on average
and 27 KB/s at most. The guest sends 2.5 KB/s. The ceiling is 1,200-byte
datagrams 30 times a second (36 KB/s).

### 5.4 The host

- **The stand-in.** Each frame, for each enemy about to update, the host picks
  its target: the nearer of its own player and the guest (by the guest's
  newest state). An enemy keeps the player it has unless the other one is at
  least 20% and 2 units nearer, so a fight is not dropped for a step.
  When the target is the guest, the host moves its own player entity (model
  matrix with its bones, frame, bound) to the guest's place for that one
  update, and puts it back exactly afterwards; the player's health, damage
  timer and movement prediction are the guest's for that update too. Damage
  and events that reach the host's player during a stand-in are dropped (M3
  turns them into hits on the guest). The guest is a target only while it is
  in the host's level, fresh (a state in the last 0.5 s), alive, visible and
  not in a special mode, a cutscene or the pause menu.
- **Culling.** An entity within its update distance of the guest stays awake,
  as it would near the host's camera.
- **Checking the guest.** The guest's positions are used only for targeting,
  culling and choosing what to send first, and only after a speed check: a move
  faster than 60 units a second (a running player does about 10) is a
  teleport, and at most one teleport in 2 seconds is believed; otherwise the
  host ignores the guest's position (no targeting, no culling near it) for 2
  seconds.
- **Death effects.** `zNMECommon::DeathFXKick` counts each enemy's death
  effects; the count goes out in the enemy's flags.

### 5.5 The guest

- While the host is in the same level and its snapshots match this level's
  table, every enemy and mover in the table skips its own update (its AI, its
  motion, its post-update) and becomes a puppet: position and rotation come
  from the snapshots, drawn 100 ms in the past between the two snapshots
  around that moment by the host's clock (the clock's offset is the smallest
  delay seen over the last 64 snapshots; positions in a line, rotations along
  the short arc, no slide across a jump of more than 10 units, or of more
  than a unit at over 35 units a second, such as a defeated enemy put back at
  its spawn point or every enemy put back by the host's level reset after its
  defeat (2 units in 49 ms); the fastest shared things measured over every
  M5 run move at 28 (BB02's platforms); nor across a
  death, a break, or a thing shown or hidden between the two snapshots, since
  a defeated enemy can be put back less than 2 units away, slowly enough to
  pass for a step). A snapshot
  that leaves a thing out says it had not changed: within 60 units of a
  player (where a thing goes out in every snapshot it changes in) the thing is
  held at its last place until the moment of the snapshot before the one it
  moves in, so it does not slide from its previous sample (which can be a
  second old, such as an enemy's wind-up animation before a lunge) and get
  ahead of the host; farther off, where things go out only every second or
  fourth snapshot, after 400 ms without one. Its animation
  state is set and its time written as for the partner (2.3), and the bound,
  the collision models and the collision grid follow (`xEntEndUpdate`). A
  platform's frame keeps its last place too, so the guest's player rides it
  with the game's own code. While the guest's game is paused, its copies keep
  still.
- The guest shows the host's world only once it has a state from the host for
  every thing in the table (the first snapshots carry all of it in a few
  packets: 0.1-0.7 s, 538 things included), or 2 seconds after the first
  snapshot whatever is missing; until then every copy is the guest's own, so
  none is drawn at its own place among the host's and then jumps. A thing still
  without a state then keeps still (logged).
- Shown and enabled are copied. When the host's enemy plays its death effect
  (shrapnel, sound, bubbles), the guest's copy plays its own at the moment it
  is drawn. When the host breaks a destructible, the guest's copy gets the
  game's own destroy event (its break effect plays once), also again after the
  guest's own level reset; when the host's one comes back (a respawn or a
  level reset), the guest's copy is reset too. Since protocol 7 the guest's
  hits on a destructible are claims too (13.2): it breaks by the host's word,
  on both screens.
  A death effect, break or respawn plays at most once in 2 seconds per thing,
  whatever a host's snapshots flip.
- Damage the guest's own player deals to a host-owned enemy does nothing
  (`zNMECommon::Damage`, `zNMETiki::Damage` return), and the guest's Patrick
  does not pick one up (`SetCarryState` refuses the attempt), so the guest's
  copy never dies, drops anything or leaves its place on its own.
- When the host is in another level, or the session ends, the guest's world
  runs as single-player again from where it stands.

### 5.6 Following the host's level

- The guest's game loads the host's level when the host has been in another
  story level for 1.5 seconds (the scene must be a level of the game's own
  world table, `zDispatcher_FindWorldTask`), once the guest is in plain play
  (not in a cutscene, paused, dead or already changing level). It goes through
  a PC-owned portal with the host's scene, as the game's own soak test does, so
  the guest's save (its unlocks) cannot send it somewhere else. "When joining,
  follow the host to their level" in the Co-op tab (`[coop] follow`, default
  on; `TSSM_COOP_FOLLOW`) turns this off and on; "Go to the host's level" does
  it once.
- Once there, after the guest's own opening of the level (its fly-through,
  which ends by putting the player at the level's start) and a second of plain
  play, the guest's player is put beside the host, facing the same way, with
  the camera behind it: a step (1.75 units) to the host's side, its other side,
  behind it or in front of it, the first with nothing in between and floor
  near the host's height; on the host's own spot when none has (the players do
  not collide).
- The scene ID comes from the network: it is checked (`A-Z 0-9`) by the parser
  and against the game's world table here; it is never a path.

### 5.7 Saving

The guest's game does not autosave while the session is up, nor afterwards in
a level it followed the host to, until it leaves that level (by a portal to
another level, to the title, or a game over; a lost chance keeps it in the
level). `zSaveLoadAutoSaveUpdate` returns and drops the pending autosave, and
`zSaveLoad_TriggerAutoSave` skips one asked for in the same frame as its
pickup. So the guest's checkpoints and token pickups in the host's levels do
not write the guest's save. (M4: saving by hand is refused too, 7.6.) Once the
guest leaves the followed level its own saves come back. The host saves as usual. `tools/coop_test.py --variant
follow` checks this with a save slot (`TSSM_TEST_AUTOSAVE`): the host's autosave
writes its save, the guest's is skipped during the session and again after the
host has left, and the guest's save folder stays empty.

### 5.8 Game hooks (all `PLATFORM_PC`, each a call that returns at once when co-op is off)

| File | Where | Call |
|---|---|---|
| `zNMECommon.cpp` | `zMODEnemy::Timestep`, per enemy | guest: `iCoopWorldGuestPose` (pose and skip); host: `iCoopStandInBegin` / `iCoopStandInEnd` around the update |
| `zNMECommon.cpp` | `zMODEnemy::PostTimestep`, per enemy | guest: `iCoopWorldGuestOwns` (skip) |
| `zNMECommon.cpp`, `zNMETypeTiki.cpp` | `Damage` | guest: `iCoopWorldNoDamage` (return) |
| `zNMECommon.cpp` | `DeathFXKick` | host: `iCoopWorldDeathFx` (count it) |
| `zNMETypeStandard.cpp`, `zNMETypeTiki.cpp` | `SetCarryState` | guest: refuse a pick-up attempt of a host-owned enemy |
| `zScene.cpp` | `zSceneUpdate`'s loop over entities | guest: `iCoopWorldGuestPose` for host-owned movers |
| `zScene.cpp` | `zSceneExit` | `iCoopWorldSceneExit` (the table goes) |
| `xUpdateCull.cpp` | `xUpdateCull_DistanceSquaredCB` | host: `iCoopCullNearPartner` |
| `zEntPlayer.cpp` | `zEntPlayer_Damage` (which the knockback variants call first), `zEntPlayerEventCB` | host: nothing during a stand-in |
| `zSaveLoad.cpp` | `zSaveLoadAutoSaveUpdate`, `zSaveLoad_TriggerAutoSave` | guest: `iCoopNoAutoSave` |

And one platform call (not game code): `iPadHostPoll` (`iPadHostSDL.cpp`,
`iPadHostNull.cpp`), which runs every frame in play, menus and movies, calls
`iCoopGameBeat`, the game thread's heartbeat for 5.11.

The game-side code is `iCoopWorld.cpp` (table, snapshots, stand-in, puppets,
level following); `iCoop.cpp` carries the snapshots and acknowledgements
between the threads and measures the traffic.

### 5.9 Tests

- `tssm_nettest` (189 checks): every `SNAPSHOT`, `SNAP_ACK` and `STALL` rule above (each
  header field out of range, reserved bits, unsorted or repeated IDs, an ID past
  the count, too many entries, a count past the end, trailing bytes, the wrong
  direction in the parser, the encoder and a live session), the round trip of
  every mask combination, the largest snapshot the builder makes beside a
  state, a ping and a pong, snapshots and acknowledgements over the lossy
  loopback session, `STALL` both ways over a live session, the `STALLED`
  goodbye both ways, a peer that reports a stall past 75 s given up on (both
  ways, and a state of its own in between restarting the count), and all three messages in the fuzz seeds and the sealed
  hostile-peer targets. Once for M2: the ASan + UBSan build passed, and the
  libFuzzer target ran 17 million inputs in 3 minutes with no finding.
- `tools/coop_test.py --scene BB02|DE01|TT01|PT01 [--variant netsim|wrong-password|follow|stay|stall-host|stall-guest|drag-host|drag-guest] [--stall S] [--guest-exe ...] [--all] [--stall-all] [--drag-all]`
  (`--all` runs the first five variants over the scenes, 11 runs, with a
  summary; `--stall-all` and `--drag-all` the stall and drag variants, 5.11):
  - after the level's opening each copy closes Mindy's talk and the level's
    task screen (B in `TSSM_INPUT`), then both are put in each other's view
    (`TSSM_TEST_PLACE`, `TSSM_TEST_PLACE_CAMERA=reset`): in DE01 the guest
    stands by a robot while the host is 60 units off, then the host comes
    over and defeats the robot (`TSSM_TEST_KILL`); in TT01 the guest stands by
    the spinning robots and the host a few steps off; in PT01 each is put on
    one of the two pillars that slide over the lava by themselves
    (`TSSM_TEST_PLACE_ON`), so the guest rides a platform the host owns;
  - both write a frame at the same moments of the host's clock
    (`TSSM_COOP_SYNC_CAPTURE`: `frames/sync_<host ms>.png` on both) and log
    where every enemy and mover near the players is in it; the guest only for
    frames posed from the host's world, and neither for a moment that went by
    while it was not shown (a level load);
  - the host stays 200 frames longer than the guest, so the guest's Co-op tab
    (`frames/coop_tab.png`) shows the session up, with the shared-world and
    traffic lines;
  - checks: the same table on both; the guest showed the host's world only
    once it had every thing; at every same-moment capture each thing within
    0.25 units on both (allowing for its speed over the frames' small
    difference; the raw worst is printed too); everything the guest drew
    within 0.25 units of where the host had it at that moment (interpolated
    between the host's 30-a-second `world sent` lines); the guest posing the
    host's things without a gap of a few frames (`posing ... on/off` lines),
    nothing posed outside its samples and nothing moving a posed thing
    afterwards (`pose` lines); in
    DE01 and TT01 an enemy targets the guest and closes in on it, and in DE01
    the enemy the host defeats blows up on the guest's screen too; in PT01 the
    guest stays on the host's moving pillar at every capture and moves with
    it; traffic under the ceiling; with netsim the guest draws the world at
    most 300 ms back; plus every M1 check;
  - follow: both boot into BB02, the host goes to the scene (`TSSM_TEST_SCENE`;
    DE01 for BB02), and the guest must follow by itself, be put beside the host
    (1-3 units from it) and stay apart from it; then both games ask for an
    autosave (`TSSM_TEST_AUTOSAVE`): the host's is written, the guest's is
    skipped; the host leaves, the guest asks again in the level it followed
    the host to, and that one is skipped too (the guest's save folder stays
    empty);
  - stay: as follow with following off (`TSSM_COOP_FOLLOW=0`): the guest stays
    in BB02, its Co-op tab offers "Go to the host's level" (captured, then
    closed: `TSSM_GUI_CLOSE`), and a press of it (`TSSM_TEST_COOP_GO`) takes it
    to the host, beside it;
  - stall-host, stall-guest, drag-host, drag-guest: 5.11.

### 5.10 What M2 delivered, and its limits

Delivered as above. What the players will notice that is not shared yet:

- **The guest cannot hurt or grab enemies** (by design until M3): the
  guest's hits do nothing to them and Patrick cannot pick them up. (M3: the
  hits count, 6.2; the grab still does nothing.)
- **Enemies cannot hurt the guest**: their attacks run on the host, against
  the stand-in, and are dropped there. (M3 sends them to the guest, 6.4.)
- **Projectiles and enemy sounds**: what enemies throw or shoot, and the
  sounds and particles of their own update (footsteps, attack yells), are not
  shown or played on the guest; the death effect is.
- **Enemies the host has not woken**: an enemy far from both players keeps
  still on the guest as on the host.
- **Decorative animated scenery** (for example the balloon crates by the
  windmill in DE01) is not in the table: it runs on each game's own clock, so
  it can be at another point of its loop on the two screens.
- **Hazards, pickups, buttons, triggers, hint NPCs and cutscenes** stay each
  game's own (M3/M4): fire, electricity and other damaging volumes hurt each
  player on that player's own screen; each player picks up their own copies; a
  button the guest presses moves nothing (platforms are the host's), and talk
  boxes come up on each screen on its own. This is less than the first M2
  goal, which named hazards, pickups and interactive objects too.
- **Falling and breakaway platforms, paddles, teeter-totters, conveyor belts
  and springboards** are each game's own: each moves by its own player's
  weight or hits on that player's screen, so a platform that falls under the
  guest falls on the guest's screen only, and the host's copy stays up
  (M3/M4 can send the guest's weight to the host).
- **One-character levels and bosses**: the guest follows the host there too,
  but bosses are each game's own and the partner shows as a name tag (M4/M5).
- **Riding a moving platform** works through the game's own driver code with
  the posed platform: in PT01 the guest stands on a lava pillar the host owns,
  stays on it and goes back and forth with it (7 units), on both screens.
  Platforms in other levels have not each been tried.
- **The guest's player is put beside the host only on arrival**; afterwards
  the two players can walk through each other (they do not collide).
- **Stalls** (5.11): a game whose window is held stops, as single-player does;
  the other player sees its world (the host's) or its player hold still with
  "waiting for NAME's game", and both are back in step within a second after.
  A stall of more than a minute ends the session. A level load counts as a
  stall too (the other game waits for it).
- Not tested yet: two PCs, a LAN, the internet through a forwarded port.

### 5.11 Stalls: a game that stops for a while

On Windows, holding or dragging a game's window stops its game thread for as
long as it is held (Windows runs its own move loop inside the game's event
pump); minimising, a hitch, a slow disk or a level load can do the same. Either
player's game may stop for anything from a fraction of a second to half a
minute, and when it runs again both games must be back in step at once.

**The session stays up.** The socket lives on the I/O thread (2.7), which
keeps answering keepalives and acknowledgements while the game thread is
stopped, so a stopped game is never taken for a gone one. The game thread's
heartbeat is `iCoopGameBeat` (every frame from the pad poll, in play, menus and
movies). Once it has run no frame for 250 ms, the I/O thread says so to the
other side with `STALL` (its length so far) 4 times a second. After a minute
without a frame it ends the session itself, with the disconnect reason
`STALLED`, and both Co-op tabs say why ("this game stopped for over a minute"
/ "the other player's game stopped for over a minute"). A minute is long
enough for any window drag or load, and short enough that a hung game does not
keep its partner waiting forever. A partner whose `STALL` reports go on for
75 seconds with no state of its own in between (one that never ends the
session itself) is given up on by the session layer (`kPeerStallLimit`): the
other side says goodbye and its Co-op tab says the other player's game stopped
for too long. A partner's stall is over at its next state, or once its `STALL`
reports have stopped for 2 seconds (`kPeerStallQuiet`), state or not: a game
that runs again inside a movie sends no state until the movie ends. (Before
8.12, only a state ended it, so the short stall when a story movie opens,
followed by a story movie longer than 75 s, ended the session: 8.12.)

**The other game waits.** While the partner's game reports a stall:

- its puppet stands at its newest state (no extrapolation), and stays drawn
  past the usual 3 seconds;
- from 2 seconds on, the HUD shows "Co-op: waiting for NAME's game (N s)..."
  and the Co-op tab "their game is not running; waiting"; when it runs again,
  a notice says so;
- on the guest, a stalled host's enemies and platforms hold where they are,
  their animations too (they are not running on the host either), instead of
  going back to the guest's own AI after 3 seconds;
- on the host, a stalled guest's snapshots pause after 0.5 s without a state
  from it, without forgetting what it acknowledged, so the first one after the
  stall is a small delta against the guest's last acknowledged snapshot.

Without `STALL` reports (the partner's game is at the title screen, or the link
is gone) the old rules apply: hidden after 3 seconds, the guest's world its own
again, the session closed after 10 seconds of silence.

**Back in step.** When a stalled game runs again:

- its first frame uses the game's own frame-time clamp (0.1 s), as after any
  hitch in single-player: no catch-up;
- the guest shows the host's newest snapshot at once. Snapshots more than
  400 ms apart in the host's time had a stall between them: whatever the newer
  one carries is shown right away rather than slid to from where it was before
  the gap, and after the host's own stall the held animations go on from where
  they stopped (the host then sends the animation times anew). The guest's
  clock offset is unaffected (it is measured from the host's wall clock as the
  I/O thread stamps each arrival), and the snapshots that queued up during the
  guest's own stall are all applied in one frame, not one per frame;
- what the I/O thread took in while this game's thread was stopped counts as
  arriving at this game's first frame after the stall, so a guest that comes
  back after 20 seconds does not throw the host's world away as stale. Only
  what arrived during the stall: what came before it ages through it as
  usual, so a stall while nothing comes in (the guest's own hitch while its
  connection is out) never makes the host's last snapshot fresh again and
  never puts the guest back on the host's frozen world (M5 fix round 1; the
  game remembers its last 8 stalls, each as when it began and when it ran
  again);
- the puppet shows the partner's newest state at once: two states more than
  400 ms apart in arrival are not slid between;
- nothing happens twice: death effects, breaks and respawns come from counters
  and states in the snapshots (each at most once in 2 seconds per thing), the
  level the guest follows the host to and the character switch from the newest
  state only, and the guest's arrival placement is once per level. M2 has no
  queued events on the wire; M3's reliable events (6.3) wait in their queue
  through a stall.

**Tests.** Two ways to stop one game once both players are in view (DE01 by
default), with the same checks:

- `TSSM_TEST_STALL=<frame>:<seconds>` sleeps the game thread at the end of
  that frame (after it is shown) for that long, while the I/O thread runs on:
  `tools/coop_test.py --variant stall-host|stall-guest --stall S`, and
  `--stall-all` for host and guest at 1, 5, 20 and 65 seconds;
- a real held window: `--variant drag-host|drag-guest --stall S` (and
  `--drag-all`: host and guest at 1, 5 and 20 seconds) posts the game's window
  the window menu's Move (`WM_SYSCOMMAND` / `SC_MOVE`), which puts it in
  Windows' own move loop exactly as holding the title bar does (the game
  thread stops inside the pad poll's event pump), and an Escape `S` seconds
  later, which ends the loop and puts the window back. Windows moves the mouse
  pointer onto the window meanwhile (the script puts it back after), so these
  are not part of `--all`.

The checks: the game stopped for the whole time (for a drag, by its own
"ran no frame" line); the session stayed up (one join, no disconnect, no
timeout); the stalled game said `STALL` and the other said it was waiting (from
2.5 s) and saw it run again; the guest never stopped posing the host's things;
same-moment captures four times a second, each thing within 0.25 units on both,
from at most a second after the stall on (and everywhere outside the stall and
that second); everything else of the normal run (the chase, the defeated
robot's death effect on both); nothing twice (character switch, world shown,
death effects). The other game's HUD is captured during a stall of 3 seconds
or more. Past the limit (65 s), instead: the stalled game closed the session
with `STALLED`, the first disconnect on each side says that ("this game
stopped..." / "the other player's game stopped..."), and neither timed out.

**Not done: ticking through the window's move loop.** SDL can deliver events
while Windows' move loop runs, and the game could draw frames from there, but
the game's loop is not re-entrant (a frame would run inside the pad read of
the frame before), so a held window still stops the game as in single-player;
the network side copes with that as with any other stall.

---

## 6. M3 in detail

### 6.1 Scope

In, while both players are in the same level (the shared world of section 5
is up):

- **The guest's attacks reach the host's enemies.** Every attack that ends in
  the game's own enemy damage (`zNMECommon::Damage`, `zNMETiki::Damage`) is a
  claim the guest sends to the host: Karate Spin, Bash, SpongeBowl, the cruise
  bubble, Star Spin and belly, Smash, Cartwheel, a thrown object, a slide. The
  host checks the claim and applies it with the game's own damage; the
  enemy's reaction and death reach the guest with the snapshots (5.5).
- **Enemy attacks reach the guest.** An enemy's attack on the guest (its
  swing, slam, tongue or zap during the stand-in of 5.4) and a projectile that
  flies at the guest are worked out on the host and sent to the guest, whose
  game applies them to its own player with the game's own damage functions:
  health, the HUD's Krabby Patties, knockback, the hit animation, defeat.
- **The guest's defeat** plays out as in single-player, then the guest comes
  back beside the host in a burst of bubbles with full health. Nothing is
  reset on either side and no life is lost.
- **The host's defeat** keeps the game's own rules (6.4).
- **Pickups are the host's**: shiny objects, health, Goofy Goober tokens and
  treasure chests (6.5).
- On the wire (protocol version 4): `EVENT` and `REL_ACK` (the reliable layer
  kept since M1, now on), and `PICKUPS`.

Out (later): triggers, buttons, hazard volumes (fire, electricity), tasks and
level progress beyond the pickups' own counters, cutscenes, level changes
the host starts (M4); bosses and one-character levels; hit rewind (M5).

### 6.2 The guest's hits (claims)

On the guest, the enemy damage functions of a host-owned enemy no longer
just return (5.5): when the damage is one of the player's own attacks, the
hook sends `EV_HIT` and plays the enemy's "ouch" at once; the guest's copy of
the enemy is not changed (its reaction comes from the host, about a round trip
later). Anything else that damages a host-owned copy on the guest (a trigger,
an explosion of the guest's own) does nothing, as in M2. One claim per enemy
per 0.3 s (an attack touches an enemy on several frames).

`EV_HIT` carries the level and the table's hash, the enemy's net ID, the
attack (`ATK_*`, the wire's own codes, mapped from the game's damage types on
each side), the attack's power-up level on the guest (its own save's upgrade),
the hit's direction, and where the guest drew the enemy.

The host checks, in order, and logs a refusal with its reason:

- the claim is for this level and this table, the net ID is in the table and
  is an enemy;
- the guest is in this level, its position is believable (the speed check of
  5.4) and it is not defeated, paused or in a cutscene;
- the host's own game is not paused, in a cutscene or defeated: the
  single-player game never has its player hit an enemy then (an enemy hurt in
  the middle of a cutscene would be a state the game never meets);
- the attack is one the guest's character has (SpongeBowl and the cruise bubble
  are SpongeBob's, a throw and the Cartwheel Patrick's; the others either's);
- reach: where the guest drew the enemy is within 8 units of where the host
  has it (the guest draws the host's world 100 ms back and the claim takes a
  trip), and the guest is within the attack's reach of that point (7 units for
  the melee attacks, 45 for a throw, 60 for the SpongeBowl, 150 for the cruise
  bubble);
- the enemy is there (`IsPresent`: not defeated, not hidden);
- timing: one hit on the same enemy every 0.15 s at most, and 12 claims a
  second at most (a token bucket).

Then the host applies it as the guest's own game would have: the damage
amount from the game's own table for that attack, the guest's character and
the claimed power-up level (`DmgAmtForPlayerAttack`), the hit's direction made
unit length whatever the claim's length (`hit_dir_unit`), with the host's player
standing in at the guest's place (5.4) so the enemy is knocked away from the
guest, and with `zEntPlayer_PowerupLevel` answering the guest's level while it
runs (a stone tiki needs the upgraded SpongeBowl: the guest's upgrade counts).
Enemy deaths come only from the host, once; the guest sees the death effect
when its snapshot says so (5.5), so there are never duplicate or ghost
enemies.

### 6.3 On the wire (protocol version 4)

Three message types join M2's (their parsers, rules and fuzz targets are in
`iNetProto.cpp`; `tssm_nettest` 2c):

`EVENT` (type 4, either side): `first u16`, `count u8` (1-8), then `count`
events, the n-th with the reliable id `first + n`. Each event is `type u8`,
`scene u32` (`A-Z 0-9` bytes), then the type's own fields:

| Event | From | Fields after the scene | Rules |
|---|---|---|---|
| `EV_HIT` (1), 31 bytes | guest | table u32, net ID u16, attack u8, level u8, direction i16 x 3, position f32 x 3 | ID below 1,024; attack 1-8; level 1-3; no -32,768 in the direction; position finite, within +-100,000 |
| `EV_DAMAGE` (2), 22 bytes | host | net ID u16, kind u8, damage u8, sub-kind u8, point f32 x 3 | ID below 1,024 or 65,535 (no enemy); kind 0 plain (sub-kind and point zero), 1 from a point (sub-kind 0-3: the game's `ePlayerNMEDamage`; point within the world), 2 a knockback velocity (each axis within +-200, sub-kind zero); damage 1-32 |
| `EV_PICKUP` (3), 11 bytes | guest | table u32, pickup ID u16 | ID below 1,024 |
| `EV_GRANT` (4), 12 bytes | host | table u32, pickup ID u16, result u8 | ID below 1,024; result 1 (yours) or 2 (no) |

An event of the wrong side, an unknown type, a count of 0 or more than 8, a
field out of its range or a byte too many or too few is a protocol violation.

`REL_ACK` (type 5, either side): `next u16`, the next event id the sender
expects (every one before it arrived).

`PICKUPS` (type 11, host only): `scene u32`, `table u32` (the pickup table's
hash), `count u16` (1-1,024), then `(count + 7) / 8` bytes of bits, bit *i* set
while the host's pickup *i* is taken; the bits past the count must be zero.

The reliable layer is the one kept since M1 (`ReliableOut` / `ReliableIn`,
`iNetSession.h`), run by the I/O thread (`iCoop.cpp`): the game thread queues
an event (at most 64 waiting), the I/O thread puts the oldest unacknowledged
ones (up to 8, as many as fit) in every datagram it sends, the state's and the
`STALL` reports' alike, until the other side's `REL_ACK` covers them. The
receiving I/O thread delivers each event once and in order into an inbox the
game thread empties every frame; it takes a message's events only while the
inbox (136 slots) has room for the most one message can complete (a whole
window waiting behind a gap plus the message's other 7: 71,
`kRelMaxDelivered`), so neither a stalled game nor a hostile peer ever makes
it drop one it acknowledges: what it cannot take is not acknowledged and comes
again. An
acknowledgement of events never sent, or an event past the window, changes
nothing (logged). So nothing is lost and nothing happens twice through loss,
reordering or a stall of either game (5.11): the events wait in the queue.

`PICKUPS` is state, not an event: the host sends it when a pickup is taken or
comes back, and once a second anyway, and the guest takes only the one that
came with the host's newest state. The host's snapshot builder leaves room in
the datagram for the events waiting and for `PICKUPS` (`kSnapBodyMax` is now
1,095 bytes, which leaves room for a `REL_ACK` and a `STALL` too).

### 6.4 Damage, defeat and respawn

**Hits on the guest.** On the host, the player's damage functions
(`zEntPlayer_Damage`, its knockback overload, `zEntPlayer_DamageNPCKnockBack`)
and its event handler (a hit or a negative "give health" event) are, during a
stand-in, the guest's: instead of touching the host's player they queue
`EV_DAMAGE` with the enemy's net ID, the damage, and the knockback (the
enemy's position, or the velocity), and answer as if the hit landed. A hazard
(a projectile, a lobbed ball) updates with the stand-in too when the guest is
nearer it than the host, so what an enemy fires at the guest can hit the
guest. The host keeps the guest's safe time after each hit it sends (the
game's own `DamageTimeHit`), so it never sends a second hit the guest's game
would ignore, and its enemies see that safe time while they target the guest.
On the guest, `EV_DAMAGE` calls the same game function the host's enemy
called, on the guest's own player, with the guest's copy of the enemy as the
source; the game's own rules apply (its safe time, a bus stop, a teleport box).
At most 10 a second are applied, and none while the guest is defeated (its
defeat plays out once), paused or in a cutscene, when the single-player game
never hurts its player either. Two kinds of enemy damage stay apart this
way: an enemy's own strike (a slam, a spinner's blades, a tongue, a zap) is
the enemy's update, so the host's, with the stand-in, and arrives as
`EV_DAMAGE`; touching an enemy that hurts on contact is the player's own
update (`zEntPlayer_Update`'s collision), so on the guest it is the guest's
own game against what its screen shows. The host's player update never runs
for the guest, so nothing is counted twice.

**The guest's defeat.** The guest's game plays the defeat as in single-player
(the animation, the fade to black). Where the game would then lose a chance
and reset the level (`zEntPlayer_Update`: `zGameStateSwitch(LoseChance)`), a
guest in the host's level instead comes back: its player is reset as the
game's own level reset resets it (`zPlayer::Reset`: animation, camera, the
fade back in), shown again, with full health, put beside the host as on
arrival (5.6), safe from hits for 2 s, in a burst of bubbles. No life is lost
and nothing of the level is reset, on either side. The host's game sees the
guest's "defeated" flag (it stops targeting the guest meanwhile) and logs it;
nothing of the host's changes. A guest whose host has gone (the session
ended, the host in another level) loses a chance as in single-player.

**The host's defeat** keeps the game's own rules: the host loses a life, its
level resets to its checkpoint (its enemies and pickups as at the checkpoint),
or the game ends. The guest is not touched while the host is defeated (its
enemies are still the host's and hold where they are); once the host plays
again, the guest is put beside it (after a second of the guest's own plain
play), so the two start again together. Lives are the host's; the guest has
none to lose in co-op.

### 6.5 Pickups

Both games make the same table of the level's pickups (shiny objects of all
five colours, health, Goofy Goober tokens, treasure chests; not the car's
nitro), sorted by asset ID, with an FNV-1a hash over the IDs and kinds.
While the guest shows the host's world:

- the host's `PICKUPS` says which are taken; a guest copy that is taken on the
  host is hidden (without any reward), and one that is not taken on the host
  but was taken on the guest (in its own save, or before the host's level
  reset brought it back) is shown again;
- the guest's game never takes a shared pickup itself: when its player
  touches one (`CheckPickupAgainstPlayer`), it asks the host (`EV_PICKUP`, at
  most once a second per pickup; health only when the guest is hurt, as the
  game's own check);
- the host checks the claim: this level and table, the guest playing in this
  level with a believable position within 6 units of the pickup, the host's
  own game not paused, in a cutscene or defeated (no token's autosave in the
  middle of the host's defeat), the pickup still there, 15 a second at most. It then counts the pickup for its own
  progress (the shiny objects' value, a Goofy Goober token with the game's
  autosave, a treasure chest) and fires the pickup's own links, as when the
  host's player takes one, but without anything of the host's player (no
  sound, no reward animation, no health). It answers `EV_GRANT`: yours, or no;
- on "yours" the guest's game plays the pickup as its own (it flies to the
  guest's player, the sound, the reward animation; health goes to the guest).

So everything the guest collects counts on the host, whose save is the only
one written in co-op (5.7). The guest's own counters go up too as its game
plays the pickup, but the guest's game does not save in the host's levels.

### 6.6 Game hooks (all `PLATFORM_PC`, each a call that returns at once when co-op is off)

| File | Where | Call |
|---|---|---|
| `zNMECommon.cpp`, `zNMETypeTiki.cpp` | `Damage` of a host-owned enemy (the tiki's after its stone check) | guest: `iCoopGuestHit` (the claim; the "ouch") |
| `zEntPlayer.cpp` | `zEntPlayer_PowerupLevel` | host: `iCoopClaimPowerup` (the guest's level while its hit is applied) |
| `zEntPlayer.cpp` | `zEntPlayer_Damage` (both), `zEntPlayer_DamageNPCKnockBack` | host, during a stand-in: `iCoopStandInHit` (`EV_DAMAGE`) |
| `zEntPlayer.cpp` | `zEntPlayerEventCB` | host, during a stand-in: `iCoopStandInEvent` (hit events to the guest, the rest dropped) |
| `zEntPlayer.cpp` | `zEntPlayer_Update`, the end of a defeat | guest: `iCoopGuestRespawn` (instead of the lost chance) |
| `zNMEHazard.cpp` | `zMODHazard::Timestep`, around each hazard's update | host: `iCoopStandInNear` / `iCoopStandInEnd` |
| `zEntPickup.cpp` | `CheckPickupAgainstPlayer` | guest: `iCoopPickupTouch` (asked of the host) |

The code is `iCoopCombat.cpp` (claims and their checks, damage to the guest,
the respawn, pickups), with `iCoopWorldInternal.h` as its seam to
`iCoopWorld.cpp`.

### 6.7 Tests

- `tssm_nettest` (255 checks since the review: 244 from M3, then 9 for the
  hit direction made unit length and 2 for the most one `EVENT` message can
  complete): every rule of 6.3 (each event type round-trips
  from its own side and is refused from the other, by the parser and the
  encoder; every field out of range; counts of 0 and 9; a count past what is
  there; a trailing byte; an event cut short; `REL_ACK` from either side and
  in order; `PICKUPS` from the host only, counts of 0 and 1,025, bits past the
  count, cut short, 1,024 pickups), the fullest datagram the host builds (a
  state, the snapshot budget less the room for 8 events and 1,024 pickups, a
  ping, a pong and a `STALL`) under the ceiling, the reliable layer in memory
  with 30% loss, and events both ways over the live sessions with 10% loss
  (300 each way, each delivered once and in order, both queues emptied).
  The fuzz loop has all three messages in its seeds and runs a hostile
  peer's events and acknowledgements through the reliable queues as the I/O
  thread does (nothing past the window, nothing delivered twice).
- `tools/coop_test.py --scene DE01 --variant combat|respawn|host-death|host-paused`, and
  `--fight NAME` to run a stall, drag or netsim variant over a
  fight (`--stall-frame N` puts the stall where the fight is):
  - combat: the robot by the guest goes for it and hits it; from frame 1450 the
    guest is put a step from the robot and facing it every 30 frames
    (`TSSM_TEST_FACE`) and spins (X) until the robot is defeated. Checks: every
    hit the host sent arrived once and took exactly its damage off the guest's
    health; the guest's claims were applied on the host; the robot the guest
    defeated died once on the host and blew up on the guest's screen; the
    shiny objects the guest touched were asked of the host, counted there once
    and played on the guest once;
  - respawn: the guest has one health left (`TSSM_TEST_HEALTH`); the robot
    defeats it. Checks: it came back beside the host (1-3 units) with full
    health, no life lost, its level not loaded again, the host never defeated
    and told of it;
  - host-death: the host stands by the robot with one health left. Checks: the
    host was defeated and its game kept its rules, the guest was put beside the
    host once it played again, and lost nothing itself;
  - bowl (TT01, where the level starts as Patrick, so the guest is SpongeBob):
    the guest is put 7 units from a spinning robot, facing it, and bowls at
    it. Checks: the SpongeBowl's claims (from afar, against its own reach)
    were applied on the host;
  - host-paused (added by the review): the combat fight with the host's pause
    menu open (START) from frame 1400 to 1700. Checks: the host's own states
    said paused, the guest's claims meanwhile were refused for it and none was
    applied before the last of those refusals; then the later ones were
    applied, the robot died once on both screens and the pickups were the
    host's as in combat;
  - plus every M2 check of the run (the same places at the same moments, the
    traffic, no protocol error).

### 6.8 What M3 delivered, and its limits

Delivered as above. What the players will notice that is not shared yet:

- **Patrick cannot pick up enemies** (a stunned enemy to throw): the guest's
  grab does nothing to the host's enemies, as in M2. Thrown objects that are
  not enemies (and what Patrick throws) do count as hits (`ATK_TOSS`).
- **What enemies fire is not shown on the guest** (5.10), but since M3 it can
  hit the guest: a projectile the host's enemy throws at the guest hurts the
  guest without the guest having seen it fly. The hit itself (the health, the
  knockback away from where it came from) is right.
- **Enemy particle attacks are not shown on the guest**: gas clouds, fog
  breath, sparks, plasma and exhaust (`zNMEParFX*`) are made only on the host.
  Since protocol 7 they hurt the guest (13.1); before, they checked only the
  host's player. The exhaust, spark and orient kinds hurt the guest only while
  the particle is in the host's view (their own alpha is worked out there).
- **Hits are checked against where the host has the enemy now**, not where it
  was when the guest swung (no rewind yet, M5): the 8-unit slack covers the
  100 ms the guest draws behind and an ordinary ping; a fast enemy over a slow
  link can have a hit refused (logged on the host).
- **Moves and power-up levels**: since protocol 7 they are the host's save's on
  both games (13.3); the host applies a hit with its own level for that attack
  (the claim's level is only compared).
- **Hazards, triggers, buttons, tasks and level progress** other than the
  pickups' own counters stay each game's own (M4): fire and electricity hurt
  each player on that player's own screen.
- **Enemies' "kill" events and the game's "666" damage** (one hit that ignores
  the safe time) arrive on the guest as ordinary damage, so the guest's safe
  time after a hit can stop them. No enemy of the game sends either to the
  player; level hazards do, and those are each game's own.
- **Goofy Goober tokens**: the host counts the guest's token but plays no
  reward animation of its own; the guest's game plays it. Since protocol 7 the
  host's autosave and the level's own "token taken" signal (a main task's
  token ends the level) come when the guest's reward animation is over (13.3).
- **Upgraded moves of the guest not yet claimed** (13.5): the upgraded Bubble
  Bash's delayed blast and the upgraded Belly Flop's stun ring do nothing to
  the host's enemies. The upgraded SpongeBowl's lightning does (attack 10).
- **Patrick's slam down a stack of crates** (not reproduced): the guest's own
  copy of a crate never breaks by itself, so the guest may stop on top of a
  stack instead of falling through it; each crate takes its own slam.
- **No partner health on the HUD** yet (M5); each player sees their own.
- **Bosses and one-character levels** are each game's own (M4/M5).
- Not tested yet: two PCs, a LAN, the internet through a forwarded port.

---

## 7. M4 in detail

### 7.1 Scope

In (as built):

- **Level changes led by the host** (7.2): the host's portal takes the guest
  along at once, and both games start the new level together.
- **Cutscenes and fly-throughs on both** (7.3), started and ended by the
  host; only the host skips the ones it started. Talk boxes stay each
  player's own.
- **Characters** (7.4): either player switches between SpongeBob and Patrick
  at any time, in any level that has both, from the Co-op tab or with a tap of
  LB (Z on the keyboard). Both may be the same character. The guest takes the
  other character than the host's only as a default.
- **Costumes** (7.4): each player picks a costume for each character from the
  ones their own save has unlocked; the choice is kept in config.ini.
- **The partner's own model** (7.5): each game draws the partner with a
  second copy of the very model the partner's game uses (its outfit, its
  costume, the Patty Wagon, the slide sled), so two of the same character,
  costumes and the one-character levels all work.
- **Saving is the host's** (7.6): the guest's game writes no save while a
  guest, by hand or by itself.

Out (still each game's own, or later): triggers, buttons, tasks and level
progress other than the pickups (each game runs its own copy of the level's
scripts; 7.9); spectating; bosses' fights shared (7.5); a costume changed in
the middle of a level is worn from the next level (7.4).

### 7.2 Level changes

- **The host leads.** When the host's game leaves a level through a portal
  (`zSceneExit` with a pending portal to a level of the game's world table),
  it sends **EV_LEVEL** with the destination. The guest's game goes there at
  once through the same PC-owned portal as M2's following (5.6), without M2's
  1.5-second wait, as soon as it is in plain play. A cutscene or fly-through
  playing on the guest when the host leaves ends there (as a skip would), so
  the guest can go along. With following turned off in the Co-op tab
  (`[coop] follow`), the guest stays.
- **Both start together.** Between a level's load and its start (between
  `zGameInit` and `zGameSetup` in `zGameUpdateMode`), the host waits until the
  guest has loaded the same level (**EV_READY**) and then both start it
  (**EV_GO**). Each waits at most 10 seconds, keeping its window answering
  (the window's event pump runs every 16 ms); a guest that never comes is not
  waited for longer, and the host's HUD says so. The wait only happens when
  the guest was in the host's level when the host left; a guest that will
  not go along (following off) says so at once (an EV_READY naming the level
  it stays in), so the host does not wait for it. A host that did not wait
  (the guest was elsewhere: still on its way, or stalled) still sends EV_GO
  at once, since the guest's game follows the EV_LEVEL all the same and
  would otherwise wait its 10 s for it; and an EV_LEVEL to a level forgets an
  EV_GO for that level from an earlier visit, so a trip there waits for its
  own (`iNet::LevelSignals`; both added by M4's review). These two signals are
  kept apart from the event inbox on the receiving I/O thread (the game
  thread is between levels while it waits), and events go out on their own
  within 30 ms when no state is being sent (between levels).
- **The guest's own portals.** A portal the guest walks into while the host
  is in its level does nothing (`zPortalEventCB`; a same-level teleport still
  works), and a notice says the host leads the way. The guest is not left
  behind: the host's exit takes it along. (A "the guest asks the host" message
  was considered and left out: the host would need a prompt to answer it, and
  walking to the exit together is simpler.)
- **Placing.** Once there, after a second of plain play, the guest is put
  beside the host as in M2 (5.6).

### 7.3 Cutscenes, fly-throughs and talk boxes

- Each level has a **show table**: its cutscene managers and camera
  fly-throughs in the level's own entity order, with a hash both games check.
- When a cutscene starts on the host (`zCutsceneMgrEventCB`, its start event)
  or ends (`zCutsceneMgrUpdate`, played out or skipped), and when a
  fly-through starts (`zCameraFlyEventCB`) or stops (`zCameraFlyStop`), the
  host sends **EV_SHOW** (the show's index, start or end, and for a cutscene
  how it was started) while the guest is in its level, or arriving with it.
- The guest's game starts its own copy with the game's own start event (the
  same music pause and loading), and ends it as the game's own skip does. Its
  own copy's ending runs the level's own links (doors, tasks), as on the host.
  A level's opening fly-through already plays on both, since both start the
  level together; the host's start then finds it playing.
- **Skipping is the host's.** While the guest watches the host's show (the
  host said it started it, and that same one plays here; a level's opening,
  which both games start together, counts once the host's word on it has
  come), the guest's skip buttons do nothing (a log line says so); the host's
  skip ends it on both. A show the guest's game started a moment ago while the
  host watches one too counts as the host's for 1.5 s, until the host's word
  on it comes. A cutscene or fly-through that only the guest's own trigger
  started is the guest's to skip, whatever the host is watching (the host's
  skip would not end it: the host's end only ends the same show).
- The guest forgets that a show is the host's as soon as it is over: when the
  host's end for it comes, when it ends here, and at the first frame it no
  longer plays here. So a later replay of the same cutscene that the guest's
  own trigger starts (a level's cutscenes can play again) is the guest's to
  skip.
- A host's show can end on the host without its end: a level reset (the
  host's player defeated, as DE01's flinger cutscene can do when the level
  plays it again) ends a cutscene past the hook that sends the end. The guest
  cannot skip the host's show, so it would watch it to its end alone. Once
  the host's own state, fresh (a stalled host's goes stale, and this waits),
  has said for a second that it is in no cutscene or fly-through, the guest's
  copy ends as the host's end would end it, with a log line.
- **Talk boxes** (Mindy, the task screen, hints) stay each player's own: each
  reads at their own pace, while the other plays on; the host's world goes on
  for both. This was chosen over pausing both: in this game a talk box opens
  from the player's own position and buttons, and pausing the other player
  for someone else's reading would be worse than each reading their own.

### 7.4 Characters and costumes

- **Switching.** Either player switches at any time where the level has both
  characters loaded: the Co-op tab's **Customize** section, or a tap of **LB**
  (the "white" button; **Z** on the keyboard; it does nothing else in play).
  The switch is the game's own (`eEventPlrSwitchCharacter`, as a bus stop
  sends), done as soon as the player is in plain control (within 3 seconds;
  otherwise a notice says it will apply when the next level starts). A
  one-character level (7.5) says so.
- **The setting.** `[coop] character = auto | spongebob | patrick`. **auto**
  (the default): a guest takes the other character than the host's when it
  joins and when a level both are in starts; the host keeps the level's own.
  The guest goes by the host's EV_LOOK from that level, and only while the
  host's PLAYER_STATE agrees with it. Each game sends its look only once its
  own start-of-level choice is made, so a host whose `[coop] character`
  switches it when the level starts is never taken for the character it is
  about to leave (a guest on auto once became the same character as such a
  host that way).
  A chosen character is taken whenever a level with both starts. A switch
  with LB or the tab saves the choice. Both players may be the same
  character: the partner is drawn with its own model (7.5).
- **Costumes.** The game has eight costumes besides the levels' own outfits:
  for SpongeBob, Mermaid Man and Plain Sponge (from the extras screen) and
  Caveman and Ripped Shorts (from cheats); for Patrick, Disco and Plain
  Patrick (extras) and Caveman and Goofy Goober (cheats). Each player picks
  one per character, or **the game's own** (the level's outfit, or what the
  extras menu put on): `[coop] costume_spongebob = game | mermaidman | plain |
  caveman | rippedshorts`, `costume_patrick = game | disco | plain | caveman |
  goofygoober`. A hold of LB (0.6 s) steps to the next costume of the
  character being played. With none of that character's costumes unlocked in
  the player's save, a hold says so ("no costumes for Patrick are unlocked in
  your save") and changes and writes nothing.
- **Which are offered: the player's own unlocks.** An extras costume is
  offered when the player's own save has found its treasure chests (as the
  extras screen decides), a cheat costume while its cheat is on in the
  player's game. The host's unlocks do not apply to the guest: each game loads
  the costume from its own files anyway, so this is only about fairness, and
  "what you earned in your game" is the simplest rule to explain. Neither
  save's unlocks are ever changed: co-op only asks the game's costume rule
  (`zConfig_getAlternateCostume`) for the chosen one while co-op is on.
- **When a costume is worn.** A costume is a model pack the game loads with a
  level (`PLS?`, `PLP?`), so a new choice is worn **from the next level**, as
  with the game's own extras menu. The tab and a notice say so. (Changing it
  in place would mean rebuilding the player's model chain under the running
  player code, which keeps model, tag and lasso pointers into it.)
- **On the wire.** **EV_LOOK** (reliable, either side, sent when it changes
  and when a level starts): the character and the costume each character gets
  when the next level loads. PLAYER_STATE carries the model being drawn now.
  The receiver keeps every look it gets, the newest replacing the one before:
  a look is sent only when it changes, so after a stall (a held window, a long
  load, a burst after loss) the newest of a backlog is the one that counts,
  however many came at once (7.7).

### 7.5 The partner's model, and one-character levels

- **A model of its own.** M1 drew the partner with the character model this
  game had loaded and was not using, which allowed only different characters.
  Now the partner is drawn with a second instance built per level, apart from
  this player's own, with `zEntRecurseModelInfo` from the MINF of the model
  the partner's game uses (PLAYER_STATE's `model`: one of the game's 27 player
  models, from SpongeBob's eleven outfits to the Patty Wagon), allocated as the
  player's own (static, from the level's memory) and gone with the level: it is
  dropped at `zSceneExit` (M4's review), since the title menu draws its scene,
  and with it co-op's partner hook, without co-op's frame that picks the model
  for the next level. It is
  driven as the M1 puppet was (2.3): animation state and time from the network,
  no game logic. SpongeBob's eleventh part gets the player init's own fix-up.
- **The partner's outfit loads with the level.** When a level loads while a
  partner is connected and its EV_LOOK is known, the partner's outfit pack of
  each character the level has is loaded too (at most two more packs, in the
  player's own pack list, unloaded with it). With two outfits of one character
  loaded, this player's own model is taken by name (`iCoopLocalModelInfo`),
  since the game takes the first outfit it finds.
- **The first level of every session shows the partner's costume wrong.** A
  costume pack can only load with a level, and only when this game already
  knows the partner's look when it loads the level. In the usual start (both
  players are in a level, then they connect) neither knows the other's look
  then, so until the next level loads each draws the partner in an outfit of
  the same character this level has (the level's own, or one of this
  player's own costumes): the two screens can disagree about costumes there,
  for example the host draws the guest's plain Patrick as Disco when the host
  wears Disco itself. A log line says so ("the partner's model 11 is not
  loaded here; drawn as model 19"), and the Co-op tab's Customize section
  says "NAME's costume shows here from the next level". The same holds after
  a costume change in the middle of a level. From the next level both
  screens agree. (Loading a pack in the middle of a level would mean reading
  a level's worth of files during play.) The outfit drawn shows the partner's
  parts by what they are, not by their place in the chain
  (`iCoopLookPartsFor`): SpongeBob's Caveman, Ripped Shorts and Plain Sponge
  order their chains otherwise than the others, and a partner in one of them
  drawn in another outfit used to show a pair of shoes or a red shape instead
  of a body.
- **One-character levels: both play that level's character.** The game loads
  only that level's player (the Patty Wagon in the driving levels, the
  SpongeBob-and-Patrick sled in the slides, SpongeBob or Patrick alone in some
  levels and bosses), and both games load the same, so each player plays it
  and draws the other as a second one: two Patty Wagons in BB01, two sleds in
  DE02 (7.8). LB says the level has one character. The shared world (M2/M3)
  applies there as anywhere: the host's things, pickups and hits. Not
  covered: the bosses' own fight logic is each game's own (they are not
  shared enemies), and Spongeball (a ball, not a plain model) hides the
  partner, as before. No level needed the spectating fallback.

### 7.6 Saving

The host's save is the only one written. While a guest is joined, and in a
level it followed the host to until it leaves it, the guest's game neither
autosaves (5.7) nor saves by hand: its save screen (the pause menu's Save
Game) closes at once, back to play, with a co-op notice ("in co-op only the
host's game saves; your save was not written"). (The game's own failure prompt
was the first way, but its text blames the memory card: "Unable to save game!
Your Xbox doesn't have enough free blocks". It is still what a guest sees if a
session is joined while its save screen is open.) Progress the guest makes counts where it is shared: the host's
pickups (6.5) and the host's level. The guest's own save stays as it was
before the session.

### 7.7 On the wire (protocol version 5) and game hooks

| Event | From | Fields | Checks on arrival |
|---|---|---|---|
| EV_LEVEL | host | scene | a level of the game's own world table; at most 4 a second |
| EV_READY | guest | scene | names the level the host waits for |
| EV_GO | host | scene | names the level the guest waits for; an EV_LEVEL to that level forgets one from an earlier visit (`iNet::LevelSignals`) |
| EV_SHOW | host | scene, show table hash, index (< 256), start/end of a cutscene or fly-through, a cutscene's start flags | the guest's level and table hash; the index in its own table; the kind matches the thing; at most 20 a second |
| EV_LOOK | either | character, a costume number (< 5) per character; scene or 0 | only indexes this file's own tables of five; every look is taken, the newest replacing the one before (`iNet::LookInbox`), since a look is state and the sender sends one only when it changes: a receiver back from a stall drains a whole backlog in one frame and must end on the newest. Looks past 8 a second (the sender sends at most 4) are only counted, with one log line every 10 s; the log lines about the partner's look and model are rationed (ten, then one a second), and the newest look is logged once the ration allows |

PLAYER_STATE (43 bytes) adds `model`: below 27 or 0xFF; the receiver only
looks it up in its own table of the game's 27 model names. Each event's
fields are parsed strictly and range-checked by `iNetProto` and are in the
fuzz loop; delivered looks also go through the receiver's inbox there
(`tssm_nettest`: 305 checks, with a backlog of 20 looks drained at once, and
the level signals, which the fuzz loop's delivered events also go through).

Game hooks, all `PLATFORM_PC` blocks that return at once with co-op off:

| File | Where | Call |
|---|---|---|
| `zGame.cpp` | `zGameUpdateMode`, between the level's load and start | `iCoopLevelStartBarrier` |
| `zPortal.cpp` | `zPortalEventCB`, eEventTeleportPlayer | `iCoopRefusePortal` |
| `zCutsceneMgr.cpp` | start event, end of play, the skip buttons | `iCoopShowCutsceneStart`, `iCoopShowCutsceneEnd`, `iCoopMaySkipShow` |
| `zCameraFly.cpp`, `zCamera.cpp` | eEventRun, `zCameraFlyStop`, the skip buttons; `zCameraFlyFinishPC` (the skip, for the guest) | `iCoopShowFlyStart`, `iCoopShowFlyStop`, `iCoopMaySkipShow` |
| `zSaveLoad.cpp` | `zSaveLoad_SaveLoop` (a co-op guest's save screen goes straight to state 11, back to play, with a notice), `zSaveLoad_SaveGame` (a save that gets there anyway); `zSaveLoad_PcTestSaveByHand` (new: `TSSM_TEST_SAVE`'s entry point, the next save screen saves into that slot) | `iCoopNoSave` |
| `zConfig.cpp` | `zConfig_getAlternateCostume` | `iCoopCostumeOverride` |
| `zPlayerBase.cpp` | `zPlayerLoadMultipleHIPs` | `iCoopExtraPlayerPacks` |
| `zEntPlayer.cpp` | `zEntPlayer_SpongeBobInit`, `zEntPlayer_PatrickInit` | `iCoopLocalModelInfo` |
| `zMenuSB.cpp` | (new) `zMenuSB_PcCostumeUnlocked` | read by the costume list |

The code is `iCoopFlow.cpp` (levels, shows, saving) and `iCoopLook.cpp`
(characters, costumes, the partner's models); `iCoopPuppet.cpp` draws the
model `iCoopLook.cpp` gives it.

### 7.8 Tests

`tools/coop_test.py`:

- `--variant levels`: both boot into BB01 (a Patty Wagon level), the guest's
  wagon a few steps beside the host's. The host takes the level's own exit
  (`TSSM_TEST_EVENT` sends `PORTAL_TO_DE01` its own event); the guest goes
  along; both start DE01 together and its fly-through plays on both; the
  guest presses skip (nothing happens), the host does (it ends on both).
  Costumes (`TSSM_TEST_COSTUMES=all`, `TSSM_TEST_COOP_LOOK`): the host's
  SpongeBob as Mermaid Man, the guest's SpongeBob as Caveman and Patrick as
  Goofy Goober; each loads the other's with DE01. The guest takes Patrick by
  itself, then taps LB (two SpongeBobs, different costumes), then the host
  does (Patrick). The host starts the level's flinger cutscene: it plays on
  both, the guest's skip does nothing, the host's ends it. The cutscene starts
  again by the level's own script, and the host leaves for DE02 (a slide)
  during it: the guest's copy ends, it goes along, and each draws the other's
  sled; there LB switches nothing. The guest's save folder stays empty.
- `--variant look`: BB02, facing each other: the guest takes Patrick by
  itself, then each taps LB in turn (two SpongeBobs, then each the other's
  character, then two Patricks); each choice is kept in config.ini. Then the
  guest holds LB with no costume unlocked in its save: nothing changes and no
  costume is written.
- `--variant persist`: BB02, both started from config.ini files an earlier
  session left (the run's own `CONFIG` text added to each copy's config.ini):
  the host's `[coop] character = patrick`, `costume_patrick = disco`,
  `costume_spongebob = mermaidman`, the guest's `costume_spongebob = caveman`
  with its character automatic. The host takes Patrick in Disco; the guest
  hears the host's look and stays SpongeBob, in Caveman (the case where a
  guest on auto once became Patrick too); each draws the other in an outfit
  BB02 has (the first level of a session); each holds LB and the next costume
  is kept in config.ini.
- `--variant save`: BB02: both save by hand (`TSSM_TEST_SAVE`, the save
  screen straight into the first slot). The host's is written; the guest's is
  refused with the game's failure prompt, its save folder stays empty, and
  after A (the prompt), B (the save screen) and START (the pause menu) it
  plays on.
- `--variant own-show`: DE01: after the opening the guest's game starts the
  level's flinger cutscene by itself (as its trigger would) while the host
  plays on: the host sends nothing, and the guest's skip is taken.
- `--variant replay-show`: DE01: the host's game starts the flinger cutscene
  (it plays on both) and the host skips it (it ends on both); later the
  guest's own game starts the same cutscene again by itself, about 100 frames
  after the host's end. That replay is the guest's own: its skip is taken, not
  refused for the host. Later the level's own script plays the cutscene again
  on the host, which flings the host's player off and defeats it; the host's
  game leaves the cutscene with its level reset and sends no end, and the
  guest's copy must end too (the host is in no show).
- `--variant look-flood`: as persist, but the guest's game stops for 15 s
  (`TSSM_TEST_STALL=1280:15`) while the host holds LB 12 times (each the next
  costume), so the guest gets more than 8 looks in one frame when it runs
  again. It must end with the host's newest look (the host's last "look
  sent" is the guest's last "the partner's look"), and the session stays up.
- `--variant title` (M4's review): BB02, facing each other; right after
  drawing the host, the guest's game leaves for the title menu
  (`TSSM_TEST_QUIT`, the game's own exit event, as the pause menu's quit).
  The partner's model goes with the level ("the partner's model goes with the
  level"), and the title, which draws its scene without co-op's frame, runs
  on for ten seconds to the guest's own exit.
- The journey (`--variant levels`) also has the guest walk into BB01's exit
  itself (`TSSM_TEST_EVENT`, the portal's own event) while the host is still
  there: not taken, and the host's exit then takes it along.
- With `--fight levels`, the stall variants: `stall-guest --stall-frame 1290`
  stops the guest's game for 5 s across the level change (the host waits for
  it at the level's start), `stall-host --stall-frame 3150` stops the host's
  for 5 s during the cutscene, `stall-guest --stall-frame 3060` the guest's
  (the host ends the cutscene meanwhile; the guest's copy ends once it runs
  again). Same-moment captures up to a second after a game's own hitch (a
  level load, a cutscene loading at once) are not compared, as after a test's
  stall. Their own rows: the stalled game stopped and told the other; the
  session stayed up (one join each, no disconnect, no timeout); for the
  stalls during the cutscene, that the stall fell inside it, that the other
  game waited and saw it run again, and the cutscene's end: a stalled host
  ends it only after it runs again (and only then does the guest's copy end),
  while for a stalled guest the host ends it during the stall and the guest's
  copy ends when the guest runs again.

### 7.9 What M4 delivered, and its limits

Delivered as above. What the players will notice that is not shared yet:

- **A new costume is worn from the next level**, not at once (7.4), and **the
  first level of every session shows the partner's costume wrong**: until the
  next level loads, each game draws the partner in an outfit of the same
  character that level has, so the two screens can disagree about costumes
  there (7.5; the Co-op tab says so).
- **A guest's save screen just closes** with a co-op notice (7.6). The guest
  cannot keep a save of its own during a session; its own save stays as it
  was before.
- **Triggers, buttons, tasks and the level's scripts are each game's own.**
  Each game runs its own copy of the level's scripts, so a door the host's
  trigger opens opens on the guest only when the guest's own trigger or a
  shared cutscene's ending opens it; the host's progress (and its save) is
  what counts. A script of the guest's own can also move a platform the host
  owns for a frame before the host's next snapshot puts it back (seen with
  BB01's scripted platforms; the journey test lists it for BB01).
- **The guest cannot ask to leave a level**; the host's exit takes both.
- **Talk boxes are each player's own** (7.3).
- **A show only the guest's game plays** (its own trigger's cutscene) stops
  the guest's game as in single-player: the host's things hold still on the
  guest's screen until it ends (or is skipped).
- **Bosses**: both play the boss level's character, but each game runs its
  own boss fight for its own player (a boss is not one of the shared
  enemies), and when the host's fight ends the level the guest goes along by
  the host's exit. No boss level was run in a test.
- **Spongeball** hides the partner (a ball is not a plain model).
- The barrier's wait shows the loading screen a little longer, without a
  "waiting" line (the HUD is not drawn there; the log says it).
- Not tested yet: two PCs, a LAN, the internet through a forwarded port.

---

## 8. M5 in detail

### 8.1 Scope

M5 makes co-op pleasant and robust for two players over the internet, and
checks the whole of M2-M5 once more before the players get it.

In (as built):

- **Joining** (8.2): every refusal and failure says what went wrong and what
  to check, in the Co-op tab and in plain-language guides
  (`docs/HOSTING.txt`, `docs/JOINING.txt`; every build copies both beside
  `tssm.exe`, so `bin\` from `tools\pcbuild.bat` carries them), and both tabs
  show a co-op version code the two players can compare (each game also logs
  it, "coop: co-op version CODE", when it hosts or joins).
- **A dropped connection** (8.3): a guest whose session is lost joins again by
  itself for up to a minute, back into the host's level; the host lets a
  guest that comes back from a new address (or a restarted game) in again
  without waiting for the old session to time out.
- **The connection on screen** (8.4): a quality dot and the ping at the top
  right, the loss and quality in the tab, "no word from NAME" when nothing
  comes for 2 seconds; the name tag and Kick as before.
- **Settings kept** in `config.ini` (8.5): the name, port and address from the
  last host or join, and the two new switches.
- **32-bit and 64-bit together** (8.6), **performance with both players**
  (8.7), the **tests** (8.8), **Android** (8.9, a note only) and the **final
  security review** of M2-M5 (8.10).

Out: Android and Switch co-op; UPnP (the "ask my router" button stays a
plan: the guide explains forwarding by hand instead); partner health on the
HUD, an arrow to an off-screen partner (the name tag already sits at the
picture's edge on the partner's side), emotes; hit rewind; reconnecting the
host (a host that goes away ends the session: its game owns the world and the
join code). M5 adds no message to the protocol (it stays version 5): joining
again is the ordinary handshake. (Version 6 is the 0.9 base, with the same
messages: 8.12.)

### 8.2 Joining: what each player sees

The guest's Co-op tab shows the status (Connecting, Checking the password,
Connected, or a reason in red, with **Try again** and **Back**). The reasons,
constant text (`iNetProto.cpp`'s `reject_text`, `iCoop.cpp`):

| Case | The guest's tab says |
|---|---|
| Wrong join code or password | wrong join code or password. Check it with the host: their Co-op tab shows the join code (it changes when they make a new one) |
| Another version (protocol or build ID) | the host's game is a different version of the port. Both players need the same release: the "co-op version" at the bottom of both Co-op tabs must be the same |
| Other game files or mods | your game files differ from the host's. Both players need the same release and region of the game, and the same mods turned on (or none) |
| A full game | the host's game already has a player (co-op is for two) |
| Blocked (wrong passwords, or kicked) | this address was blocked by the host (too many wrong passwords, or the host removed you). Wait a minute and try again, or ask the host |
| The host refusing joins for a minute | the host is refusing joins for a minute (too many wrong passwords). Try again in a minute |
| Nothing comes back within 10 s | no answer from the host. Check that the address is the host's public address with the port it hosts on, that the host is hosting right now, that the host's router forwards that UDP port to the host's PC, and that the host allowed the game through Windows Firewall |
| Kicked while playing | the host removed you from the game |

The **co-op version** at the bottom of the Co-op tab is the first 8 hex digits
of the build ID the handshake compares (2.5), so two players can see at a
glance whether their builds will play together. The guides walk through the
same cases from each side.

### 8.3 A dropped connection, and joining again

What a drop looks like, from the first second:

- **Up to 1.5 s** without a packet: nothing shows (the puppet is drawn 100 ms
  back and the snapshots ride out short gaps).
- **From 1.5 s** without anything at all from the partner (not a stall it
  reported, 5.11: its I/O thread would still be talking): the connection is
  *interrupted*. Each game logs it, the quality dot turns red and says "no
  signal", and from 2 s the HUD says "Co-op: no word from NAME for N s
  (connection interrupted)...". The partner is hidden after 3 s and the
  guest's world goes back to its own after 3 s, as before (4.2, 5.5).
- **When packets come again** within the session timeout (10 s), the session
  simply goes on: "the other player is heard again" (logged), and the guest
  shows the host's newest snapshot at once, as after a stall (5.11).
- **At 10 s** both sides end the session (`END_TIMEOUT`). The host keeps
  hosting with the same join code ("the connection was lost; the other player
  can join again"). The guest, unless `[coop] reconnect` is off, **joins again
  by itself** for up to 60 seconds (`kRejoinFor`): the HUD and the tab say
  "the connection was lost; joining the host's game again (N s left)".

Joining again (`iCoop.cpp`, `IoRejoin`):

- It is the ordinary handshake, with the same address, password and name and
  the same socket (so a home router keeps the same mapping). Each attempt
  lasts at most 10 s; a failed one is followed by the next a quarter of a
  second later. The derived key is cached for the host's salt, so an attempt
  costs no PBKDF2.
- A host that has not noticed the loss yet answers FULL, and one refusing
  joins for a moment BUSY: the guest asks again a second later. Any other
  refusal (a new join code: the host's game was restarted; another version)
  ends it, with that reason. So do a kick, a goodbye, Leave, the guest's own
  stall past the limit, and the minute running out ("the connection was lost,
  and joining again did not work within a minute").
- To be able to join again, the guest's I/O thread keeps the join's password
  in memory until the join ends (Leave, a refusal, a goodbye, a kick, the
  minute); it is zeroed then. It is never written anywhere.
- Once in, it is a new session: the epoch changes, so both games start their
  picture of the partner, the shared world and the looks afresh (as at a first
  join). The guest is in the host's level already, so it neither follows nor
  is placed; if the host changed level meanwhile, following (5.6) takes it
  there and places it beside the host. The host's save is never touched by a
  guest's join, and the guest's game does not save while joined (7.6). The
  time it spends joining again counts as joined for its save: that game has not
  left the session, and its save stays as it was until the join ends.

**The host takes back a silent guest's place** (`iNetSession.cpp`,
`kReplaceSilence`). When the guest's game is restarted, or its address
changes (another network, a router's new mapping), its old session would hold
the one slot until it times out, and the new join would get FULL for up to
10 s. So a join whose AUTH proves the password **replaces** a session that has
had nothing at all for 3 seconds. A guest that is only stalled keeps its slot
(its I/O thread sends STALL four times a second), a wrong password replaces
nothing (and counts as one), and while the session is heard from a second
join is still FULL. The old address's packets are then dropped as any
non-partner's. On the host this shows as "the other player joined again" (a
new session, a new epoch). A copy of an AUTH the host has already let in (a late
duplicate, or a replay by someone on the path: traffic is not encrypted, and the
copy passes the cookie and the proof for up to 20 s) is dropped: it replaces
nothing and starts nothing. A real new join always has a new nonce.

**A first join that meets the old session** (M5 fix round). A guest whose
game was restarted can start so fast that its first join reaches the host
less than 3 s after its old session went silent: the host answers FULL, as
it must while that session might still be heard. A first join refused as
FULL is therefore asked again each second for 6 seconds from the Join
(`kFirstFullFor`, `Io::firstFullUntil`), with "the host's game has a player;
asking again in a second" in the log and "Connecting..." in the tab; then the
host has let the silent session go and the join gets in. A game that really
is full gives the FULL refusal after those 6 seconds. Only a first join does
this (never connected in this join); joining again after a lost session
already asks again after FULL for its minute.

Not done: a host that drops does not come back by itself (its game is the
world; the guest's game gives up after its minute, and the host hosts again
and sends a new code).

### 8.4 The connection on screen, the name tag and Kick

- **The quality dot** (top right of the picture while connected, `[coop]
  quality`, hidden while the overlay is open): green **good** (ping under
  120 ms and under 3% lost), yellow **fair** (under 250 ms and under 10%), red
  **poor**, or red "no signal" (interrupted, 8.3), with the ping in ms.
- **The loss**: in, the share of the partner's last 56 packets this side never
  got (the replay window, the newest 8 left out, as they may still be on their
  way); out, the share of this side's 32 packets before the newest the
  partner acknowledged that it says it did not get (its `ack_bits`). Both
  come from authenticated headers only (`session_loss_in`,
  `session_loss_out`). The traffic log line (`TSSM_COOP_LOG`) adds the ping,
  both losses and the quality.
- **The Co-op tab's partner table**: name, ping, a **Connection** row (the
  quality in words, the loss both ways, and "nothing heard for N s" while
  interrupted), what they play, their level.
- **The name tag** (M1): the partner's name above their head, at the edge of
  the picture on their side when off screen.
- **Kick** (M1): ends the session with DC_KICKED and blocks the address until
  hosting stops; the guest's tab says "the host removed you from the game",
  and the guest does not join again by itself.

### 8.5 Settings kept in config.ini

| Key | Written when | Default |
|---|---|---|
| `[coop] name` | Start hosting or Join (with a valid name) | (empty: Player) |
| `[coop] port` | Start hosting | 2171 |
| `[coop] address` | a join from the tab, once its session is up (14.2) | (empty) |
| `[coop] nametag`, `follow` | the tab's switches (M1, M2) | on |
| `[coop] reconnect` | the tab's "join again by itself" switch (M5) | on |
| `[coop] quality` | the tab's "connection quality on screen" switch (M5) | on |
| `[coop] character`, `costume_spongebob`, `costume_patrick` | Customize, LB (M4) | auto, game, game |

The password and join code are never written. `[coop] mode` is not written
by the tab (a game that starts hosting by itself is a choice for config.ini
only). `TSSM_COOP_RECONNECT=0|1` overrides `[coop] reconnect` for tests.
`tools/coop_test.py --variant tab` (8.8) checks every row above but the M4
ones (the `persist` and `look` variants check those) through the tab's own
code.

### 8.6 32-bit and 64-bit together

Nothing is sent as a raw struct (9, rule 2), and the build ID is the commit
both builds were configured from, so a 32-bit and a 64-bit build of the same
release play together. Both widths are tested against each other (8.8) with
both builds configured from the same commit (a build configured from an older
commit has another build ID and is refused as another version, which is the
intended behaviour).

### 8.7 Performance with both players

`TSSM_COOP_LOG` adds a line every 5 seconds with co-op's own cost per frame
(`iCoopCostScope`, `iCoopWorldInternal.h`): the frame's work (`iCoopFrame`:
the state out, the samples in, the shared world, the flow, the looks),
drawing the partner, the guest's posing of the host's things (in the
entities' own update loop) and the host's stand-ins, as mean and worst per
frame, with the frame time. The scopes are opened only after each hook's
"co-op is off" return, so single-player pays nothing. Results are in 8.11.

### 8.8 Tests

`tssm_nettest` adds `[5b]` (M5): a silent session replaced by a new join that
proves the password, not by a wrong password and not while the guest is heard
from; the old address's packets dropped afterwards; a join again from the
same address after both sides timed out; a kicked guest blocked; the loss
figures; each refusal in words (constant text, no format characters).

`tools/coop_test.py`:

- `--variant wrong-password|wrong-version|wrong-files|no-answer` (and
  `--join-all`, which adds `kick`): the guest is refused (a build ID or
  game-files hash made to differ by `TSSM_TEST_COOP_BUILD_SALT` /
  `TSSM_TEST_COOP_ASSET_SALT`; for no-answer, a port nobody hosts on), its log
  and its Co-op tab (captured) give the reason in words, no session or world
  appears on either side, and the guest does not try again by itself.
- `--variant kick`: the host kicks the guest once both are in view
  (`TSSM_TEST_COOP_KICK`); the guest is told why, its world goes back to its
  own, and it does not join again by itself.
- `--variant blackout --stall S` (and `--blackout-all`: 5 and 15 s), DE01: the
  guest's I/O thread drops everything in and out for S seconds
  (`TSSM_TEST_BLACKOUT`, a pulled cable). Under 10 s: one join each, both log
  the interruption and hearing the other again, the host's HUD (captured)
  says "no word from Guest". From 10 s: both time out, the guest joins again
  by itself within 2.5 s of the cable coming back, stays in DE01 (no
  following, no placing), the host's world is shown again; the guest's HUD
  (captured mid-way) says it is joining again. Both: same-moment captures
  four times a second agree (each thing within 0.25 units) outside the cut
  and the time after it, and are back in step within 1 s after a short cut
  and 3 s after a join again; nothing happens twice.
- `--variant normal` and `netsim` check the quality the guest reports: good
  on one PC, fair through 80 ms (+20 ms) and 5% loss each way.
- `--variant blackout --stall 15 --hitch [FRAME:]SEC` (M5 fix round; in
  `--blackout-all` as 15 s with a 1.3 s hitch): the guest's own game thread
  also stops for SEC seconds (`TSSM_TEST_STALL`, 250 frames into the cut by
  default) while the cable is out. The guest must not go back to posing the
  host's frozen world after it: one stop and one start of posing in all
  (5.11, what arrived before a stall ages through it).
- `--variant tab` (M5 fix round), BB02: both games start with co-op off and
  do everything through the Co-op tab's own code (`TSSM_TEST_COOP_TAB`,
  `iGuiCoopTest`: the fields typed, the buttons and switches pressed, with
  the overlay open or not). The host types a name, a port and a password and
  presses Host; the guest types a name, the address and a wrong password and
  presses Join, is refused, types the right one and presses Try again (shown
  only where the tab shows it) and joins. Then each switches its options
  off. Checks: the refusal and the join in that order, the same co-op version
  logged by both, each name known to the other, `[coop]` in each config.ini
  (name, port or address, the switches), and no password in either
  config.ini or log.
- `tools/coop_restart_test.py [--gap S]`: the guest's game is killed (no
  goodbye) and started again S seconds later (4 by default); the host must
  let the new join take the silent session's place before its timeout, and
  the shared world must come back (same-moment captures). With `--gap 0` the
  new join meets the old session within 3 s of its silence and must ask again
  after FULL (8.3). With `--third` nobody is killed: a third copy joins with
  the right password while the guest plays. It asks for its 6 seconds, is told
  the game is full (its tab is captured), and the two players' session never
  notices.

### 8.9 Android

Out of scope in M5: co-op was built and tested on Windows only. An earlier
version of this section said the network code compiled for Android with the
NDK. It did not for the APK: its random source used `getrandom`, which
Android has only from API 28, and the APK's minimum is API 24. Protocol 8
(14.1) uses `arc4random_buf` there, and the co-op files then compile for
both of the APK's ABIs at API 24. The normal Android app (the `android` branch)
still declares no network permission: co-op on the phone needs
`<uses-permission android:name="android.permission.INTERNET" />` in its
manifest, a Co-op tab for touch, and its sockets handled while the app is in
the background. That is a separate co-op app, beside the normal one (14).

### 8.10 Security review of M2-M5

Checked once more before the players get it, against section 9's rules, over
everything M2-M5 added:

- **Parsing** (rules 2-4, 13): every message M2-M4 added (SNAPSHOT,
  SNAP_ACK, STALL, EVENT with its kinds, REL_ACK, PICKUPS, the M4 events) is
  read field by field with the bounded reader; every count is checked against
  its constant before its loop (snapshot entries, events 1-8, names 1-16),
  and each kind is accepted only from its own side. M5 adds no message: a
  join again is the ordinary handshake.
- **Network values that index memory** (rule 14), each site read again: net
  IDs index the thing table, the hit timers and the snapshot scratch only
  after the level, the table hash and the count match; pickup IDs are checked
  against the pickup table, show indexes against the show table (and the kind
  against the thing); a model number is below 27, a costume number below 5;
  animation IDs are looked up, never used as indexes; a scene must be a level
  of the game's own world table before the loader sees it.
- **The host decides** (rule 15): hits, pickups and level changes from the
  guest are claims with their distance, state and rate checks (6.2, 6.5,
  7.2); the guest applies the host's damage with its own rules, at most 10 a
  second.
- **M5's own additions**: replacing a silent guest needs a cookie from the
  address and the password proof, and 3 s of total silence from the old
  session; a wrong password replaces nothing and counts as one. The loss
  figures read only authenticated header fields, and an acknowledgement only
  of packets this side sent. Joining again keeps the password in the I/O
  thread's memory only for the join (zeroed when it ends), tries for one
  minute at most, at the handshake's own pace (the host's per-address limit
  of 4 a second still applies), and never after a kick, a goodbye or a
  refusal other than FULL or BUSY. The test hooks (`TSSM_TEST_BLACKOUT`,
  `TSSM_TEST_COOP_KICK`, the two salts) are read from the environment only.
  The new texts are constants; the partner's name is still the only network
  string, drawn as an argument, never as a format. `config.ini` gets only
  what the player typed (the name and address fields filter their
  characters), never a password.
- **Sanitizers and fuzzing** (rule 18): `tssm_nettest` built with ASan and
  UBSan found one out-of-bounds write, in the test itself (a helper copied a
  ninth event into an array of eight while testing that the encoder refuses
  nine; fixed); the game's network code was clean, 343 of 343. The libFuzzer
  target ran 16.4 million inputs in 181 s from the nettest seeds and M2's
  corpus with no finding.

**The last review**, over everything since M4's review, the guest's claims and the hooks they
reach read again. M5 still adds no message. What it found and fixed:

- A copy of an accepted AUTH could replace a silent guest, or start a session
  nobody was behind, while its cookie was good (someone on the path, or a late
  duplicate). The host now keeps the nonces of the last 8 joins it let in and
  drops a copy (`Host::auth_used`, nettest [5b]; a resend before the session is
  confirmed still gets its WELCOME).
- A guest's game could write its save in the minute it was joining again:
  `iCoopNoAutoSave` looked at "connected" only. Joining again now counts as
  joined, from the moment the session is lost (`coop_test.py --variant blackout`
  asks for autosaves in that minute; the unfixed build wrote one).
- A session that ended before the guest's status saw it up (a welcome and a
  goodbye in one batch, which a hostile host can send) left the tab on "Checking
  the password..." with the socket closed. It now says the session ended and
  offers Try again. The same for a retry after FULL that could not start.
- On a replacement the new player's name is logged too (it need not be the same
  player).

Read and found sound: replacing a session (needs the cookie, the proof and 3 s
of total silence); the join-again loop (one minute, at the handshake's pace,
FULL and BUSY only); the password's lifetime; the loss figures; the partner's
parts (`iCoopLookPartsFor`: model checked, the accessor checks its index, bits
past the chain kept and only ever tested below the part count); the guest's
hits, pickups and level signals (6.2, 6.5, 7.2); snapshots go only to the
address that proved the password; the partner's models are built at most once
each per level; the HUD's lines stay within their array; the texts are constants
and the name is an argument. The test hooks are read from the environment only.
Sanitizers and fuzzing on the fixed code: `tssm_nettest` 362 of 362 with ASan and
UBSan, no report; libFuzzer 9.2 million inputs in 91 s, no finding.

Accepted, as documented: traffic is authenticated, not encrypted (11); a
second person who has the join code can take a guest's place after 3 s of
that guest's silence (the code is the key; a host who suspects it stops and
hosts again for a new one); a hostile host can keep a guest trying to join
again for its one minute.

### 8.11 What M5 delivered, and its limits

Delivered as above: the join flow and its guides, joining again after a drop,
the connection on screen, the settings kept, 32-bit and 64-bit together, and
the cost per frame, each checked by the runs in 8.8.

Limits, as the players will meet them:

- **Tested mostly as two copies on one PC** through 127.0.0.1 (with simulated
  delay, loss and cuts). CGNAT and NAT rebinding were never tried.
- **A host that drops does not come back by itself**: the guest's game tries
  for a minute; once the host hosts again (with a new code unless it typed its
  own password), the guest joins again by hand.
- **No UPnP**: the host forwards the port by hand (HOSTING.txt).
- **Not added**: partner health on the HUD, an arrow to an off-screen partner
  (the name tag sits at the picture's edge on their side), emotes, hit
  rewind, a touch-friendly tab; Android co-op (8.9).
- The quality dot can say "good" on a link that is fine now but drops a
  moment later; "no signal" comes after 1.5 s of silence.
- The limits of M2-M4 (5.10, 6.8, 7.9) stand, less those protocol 7 removed
  (13): triggers, buttons, tasks and the levels' scripts are each game's
  own (a token or chest the guest's own task shows or gives is now asked of
  the host); a new costume is worn from the next level, and the first level
  of a session can show the partner's costume wrong; bosses' fights are each
  game's own; Spongeball hides the partner.
- Before protocol 7, a combat arena's counters and texts
  ("N LEFT", "Wave n Complete!") were each game's own, so the guest's stayed
  at the start while the host's counted down. Protocol 7 shows the host's on
  both (13.4).

### 8.12 On the 0.9 base (protocol version 6)

Protocol version 6 is co-op on the port's 0.9 (co-op began on 0.8), with the
fix for King Neptune's dizzy sound after the ending:

- **What 0.9 brings to co-op games**: the speed savings on the game thread and
  the GPU (the shadows drawn before the scene, frame metrics, the librw
  patches), the sound device freeze, the black line over the aiming pin,
  Neptune's scope depth buffer, the crash while a level loads, the four
  Settings volume sliders. The merge applied by itself; the files both sides
  changed were read side by side. In `zEntPlayer.cpp`, `zGame.cpp` and
  `zScene.cpp` co-op's hooks and 0.9's decal filter and shadow pre-render sit
  in separate places and share no state (co-op draws the partner in
  `zSceneRenderPreFX` and gives it no shadow of its own, as before). The
  GameCube build is unchanged: `ninja` with the hash check, `main.dol` sha1
  `f772db7f38b9b55e27169822ed17184fa2dc2574`.
- **Neptune's dizzy sound**: a cutscene that loads frame by frame waits the
  console's 0.63 s disc seek before its first read. In co-op the guest starts
  its copy of the host's cutscene with the same start event (7.3), so both wait
  the same time, and the host's state says "in a cutscene" from the start of
  the load (`globals.cmgr`), so the guest's check for a host that left its show
  is not set off by the wait.
- **Protocol version 6**, with the same messages as 5 (2.5): a 0.8 co-op copy
  and this one refuse each other before anything else is compared, each way
  with "the host's game is a different version of the port". The co-op version
  code at the bottom of the tab changes with it.
- **A fix for a bug of the 0.8 co-op build**: the session ended during any
  story movie longer than 75 s. Opening a movie stops the game thread for
  about 250 ms, so each game told the other STALL; inside the movie the game
  runs (its heartbeat goes on) but plays no frame and sends no state, and only
  a state ended a partner's stall, so 75 s later each side gave the other up
  ("the other player's game stopped for over a minute"). Story 2, 3, 6, 8, 11
  and the ending's 12 run 73-81 s (3, 6, 11 and 12 over the limit). A
  partner's stall now also ends once its reports have stopped for 2 s
  (`kPeerStallQuiet`, 5.11).
- **The SpongeBall on a slow PC**: below about 48 fps the ball's update runs as the fewest equal steps
  of at most 1/60 s, so the ball keeps its 60 fps speed while BL01-BL04's
  breakaway blocks keep real time. It is a deliberate port-only change,
  described in `src/SB/Core/pc/PORTING.md`; at a steady 60 fps and faster
  nothing changes. Co-op has no hook in `zEntPlayer_BoulderVehicleUpdate`, and
  each game moves its own ball (the partner is hidden in a SpongeBall level, 8.11).
- **Test hooks wait for plain play**: a
  `TSSM_TEST_EVENT` teleport or `TSSM_TEST_SCENE` waits until the game is not
  paused and has no level change pending, so a scripted run with a task screen
  open no longer spins forever. Test hooks only.

---

## 9. Security requirements (every milestone)

The host's port is open to the whole internet. Every milestone keeps all of
these, and each milestone's review checks them.

1. **Every byte from the network is hostile**, in both directions: the host
   does not trust the guest and the guest does not trust the host, even after
   the password check.
2. **No raw structs on the wire.** Every field is read and written one at a
   time with the bounded reader/writer, in a fixed byte order.
3. **Every field is validated:** counts bounded by a constant and by the bytes
   left before any loop; enums, slots, IDs and indices range-checked; floats
   finite and within limits; reserved bits and padding zero; no trailing
   bytes. Parsing is strict and canonical (accepted input re-encodes byte for
   byte).
4. **Unknown or not-yet-enabled message types are rejected.** A message type
   is accepted only from the milestone that uses it, and only from the side
   that sends it (M2: `SNAPSHOT` from the host, `SNAP_ACK` from the guest,
   `STALL` from either; M3: `EVENT` and `REL_ACK` from either, each kind of
   event only from its own side, `PICKUPS` from the host; M4: `EV_LEVEL`,
   `EV_GO` and `EV_SHOW` from the host, `EV_READY` from the guest, `EV_LOOK`
   from either, each rate-limited on arrival where it starts anything). Events are
   delivered once and in order through a bounded window; an acknowledgement
   of events never sent or an event past the window changes nothing, and an
   inbox with no room for a whole window takes none (they come again). A
   `STALL` only makes the receiver hold what it shows
   and wait; it indexes nothing, and the session's rate limit applies. A peer
   that keeps sending it with no state of its own is given up on after 75
   seconds (`kPeerStallLimit`), so it cannot hold the other side's slot or
   its "waiting" line forever; its stall counts as over once its reports have
   stopped for 2 seconds (`kPeerStallQuiet`).
5. **Version and identity handshake:** protocol version, build ID and asset
   hash are checked by the host and refused with an explicit reason.
6. **Password required.** It never crosses the wire; the proof is
   HMAC-based on a PBKDF2 key; both sides prove they know it. The default is a
   generated 60-bit join code.
7. **Stateless until authenticated:** the host keeps no state for a source
   until it shows a valid cookie, and allocates nothing for it before the
   proof checks out.
8. **No amplification:** HELLO is padded to 1,200 bytes and no reply before the
   session is larger than its request; malformed or forged packets get
   silence.
9. **Rate limits, inbound:** before the session, 4 packets/s per address (an
   IPv6 /64 counts as one) and 100/s for everyone together, checked in that
   order so packets an address's own limit refuses do not spend the shared
   allowance; in the session,
   240 packets/s, counted after the tag check so spoofed packets cannot use up
   the partner's allowance. Packets from any address other than the partner's
   are dropped before any crypto.
10. **Password guessing:** 5 wrong attempts from an address block it for 1
    minute, doubling up to 64 minutes; 20 wrong attempts a minute in total
    pause logins for a minute. The host sees the count and can kick.
11. **Every session packet is authenticated** (16-byte HMAC tag) and
    replay-protected (64-packet window). Traffic is not encrypted; this is
    stated in the UI.
12. **One guest only.** A peer that sends more than 16 malformed messages is
    disconnected.
13. **No allocation on the network path:** fixed-size tables and buffers;
    datagrams over 1,200 bytes or truncated are dropped unread.
14. **Network data never reaches** file paths, mods, saves, config, format
    strings or log formats. The player name is the only string, with a strict
    character set and length, drawn only as plain text. Logs from network
    code print constant text with numbers. The one network value that picks a
    file is the host's scene when the guest follows it (M2): four characters of
    `A-Z 0-9` (the parser) that must be a level of the game's own world table
    (5.6) before the game's own loader sees it; M4's EV_LEVEL is the same
    (7.2), and a model or costume number (PLAYER_STATE's model, EV_LOOK)
    only picks from this game's own fixed tables of the game's own pack and
    model names, whose files the player's own game loads. Net IDs, pickup
    IDs and show indexes index only this game's own tables, and only for the
    level and table hash the message names; animation IDs are looked up in
    this game's own tables. The one network value written to `config.ini`
    is a joined host's address, after its session is up (the host proved the
    password), from the parsed numbers (14.2).
15. **The host decides shared outcomes** (M3 and later): hits, pickups and
    triggers from the guest are claims, checked for existence, distance,
    state and rate, so a hostile guest cannot finish a level from afar or
    break the host's save. M3 does this for hits (6.2) and pickups (6.5).
    The guest, in turn, applies the host's damage with its own game's rules
    and at most 10 hits a second. If the host's level state is ever sent as a blob
    for the game's own loader, it is size-bounded and fully validated first.
16. **No game content over the wire,** only state, IDs and hashes.
17. **No third-party libraries or services:** OS sockets only; no relay, no
    lookup service, no DNS for joining (numeric addresses only). UPnP, if
    added, is an explicit button that the player presses.
18. **Tests:** every new message gets parser tests and joins the fuzz loop
    before it is enabled; `tssm_nettest` passes; the ASan/UBSan build and the
    libFuzzer target run clean once per milestone.

---

## 10. What the two players need

The players' own step-by-step guides are `docs/HOSTING.txt` (the host:
forwarding the port, the firewall prompt, the public address, sharing the
code) and `docs/JOINING.txt` (the guest: the same version, their own game
files, joining), plain text for the release. In short:

- **The same build.** Both run the same version of this port (the same
  release). Different versions are refused with "different version". A
  32-bit and a 64-bit build of the same release can play together.
- **Their own game files.** Each player needs their own copy of the game's
  files, set up as for single-player. Nothing from the game is sent between
  them. The files must be the same release and region, and both should turn
  off mods or enable the same ones; otherwise the join is refused with "game
  files differ".
- **A forwarded port on the host's side.** On the host's router, forward
  **UDP** port 2171 (or the port chosen in the Co-op tab) to the host PC's
  local address, which the Co-op tab shows while hosting. The guest needs no
  forwarding. The guest types the host's **public** address, which the host's
  router status page shows (the game never asks an outside service for it).
  Two PCs on the same home network use the host's local address instead and
  need no forwarding at all.
- **The firewall prompt.** The first time the host presses **Host**, Windows
  asks whether the game may communicate on networks. Allow it on private
  networks (and on public networks only if the PC is on one). The game never
  changes firewall or router settings itself.
- **The join code.** The host tells the guest the join code over a channel of
  their own (in person, a message). It changes every time hosting starts
  unless the host types their own password.

---

## 11. Open decisions

- **The default port.** UDP 2171. Players can
  still pick another one in the Co-op tab or config.ini ([coop] port).
- **Encryption.** Packets are authenticated, not encrypted. Nothing secret
  travels after the handshake (positions and animation states), so this was
  judged enough; adding an AEAD later changes only the seal/open functions.
- **UPnP.** Not built in M5: the guides walk the host through forwarding the
  port by hand. If it is added, only behind an explicit button.
- **Ticking through a held window** (5.11): not done; the game's loop is not
  re-entrant. A held window stops that game as in single-player, and co-op
  waits for it (up to a minute).
- **Which character each player plays.** Decided in M4 (7.4): each player
  picks and switches at any time; the guest takes the other character only by
  default.
- **One-character levels.** Decided in M4 (7.5): both play that level's
  character, the partner drawn as a second one. Spectating was not needed.
- **Costumes.** Decided in M4 (7.4): each player's own unlocks; worn from the
  next level.

---

## 12. Local co-op: two copies on one PC

Two players on one Windows PC. Player 1's copy hosts on `127.0.0.1` and starts
Player 2's copy, which joins it by itself: nothing is typed, forwarded or
allowed through the firewall. After the join it is the online co-op of this
document, unchanged (one host, one guest, rule 12). Code: `iCoopLocal.cpp/.h`
(the game side), `iCoopLocalCore.cpp/.h` (the pipe's lines, the password, the
child's environment; no OS, no game, so `tssm_nettest` tests and fuzzes it),
and the child-process calls in `iHost.h` (`iHostWin32.cpp`; every other host
gets `iHostChildNull.cpp`, "not available").

This is the first part (S1). Both copies are ordinary windows; split windows
and pictures, one controller each, Player 2's sound rule and Player 2 starting
from Player 1's save come next. Until then Player 2 has a new game's progress.

### 12.1 Starting

- `Local co-op.bat`, beside `tssm.exe`, runs `tssm.exe --local-coop`.
  `--local-coop=side|stack` names the layout for later parts;
  `TSSM_LOCAL_COOP=1` does the same as `--local-coop`.
- Player 1's copy hosts at once on `127.0.0.1`, port 0: the system picks a
  free port, which the I/O thread reads back (`udp_local_port`) into the
  status and the log ("hosting on UDP port N (this PC only) for local co-op").
  The password is 24 symbols of the join-code alphabet (120 bits), made fresh
  for each session. `[coop] mode, bind, address, port`, `TSSM_COOP*`'s mode
  and address, `--host` and `--join` are not used, and the Co-op tab shows the
  local section in place of the online form.
- Once Player 1's game is in a level and has drawn a frame there, it starts
  Player 2's copy in that scene. The tab says "Player 2's game starts once you
  are in a level" until then.

### 12.2 The second copy

`iHostChildCreate` makes a pipe; Player 1 writes the session line into it,
then `iHostChildStart` starts the same `tssm.exe` with:

- **Only the pipe's read end inherited** (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`),
  no standard handles, no console. Neither end is inheritable except the read
  end for the moment of the start. Player 1 closes its copy of the read end,
  so Player 1's exit, however it happens, is the end of the pipe for Player 2.
- **In a job that ends it with Player 1** (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`):
  the child starts suspended, joins the job, then runs. The job is held until
  Player 1's process ends, so a Player 2 that is hung, stuck in a message box
  or not reading the pipe cannot outlive it (nor keep its folder's claims). What
  Player 2 starts itself, a folder or a web page, is not tied to it.
- **An environment block built for it.** Player 1's own environment is never
  changed. Left out (prefixes, any case): `TSSM_COOP`, `TSSM_TEST_`,
  `TSSM_INPUT`, `TSSM_CAPTURE`, `TSSM_EXIT_AFTER`, `TSSM_GUI_`, `TSSM_LOCAL_`,
  `TSSM_SAVE_DIR`, `TSSM_LOG`, `TSSM_CONFIG`, `TSSM_PERF`, `TSSM_SND_DUMP`,
  `TSSM_TEXDUMP`, `TSSM_DUMPSHADOW`. `TSSM_CHILD_<NAME>=v` becomes `<NAME>=v` (how a test scripts
  Player 2). Then set: `TSSM_LOCAL_COOP=2`, `TSSM_LOCAL_PIPE` (the handle),
  `TSSM_LOCAL_BOOT` (Player 1's scene), `TSSM_CONFIG` (Player 1's
  `config.ini`), `TSSM_CONFIG_READONLY` (`player2.ini`), `TSSM_SAVE_DIR`,
  `TSSM_LOG`, `TSSM_ASSETS` (Player 1's resolved folder, so both hash the same
  files) and `TSSM_GUI_LAUNCHER=never`. Sorted by name ignoring case, as
  Windows wants.

**The pipe** runs one way, Player 1 to Player 2, in ASCII lines of at most 128
bytes:

| Line | Meaning |
|---|---|
| `hello 1 port=<1024-65535> code=<24 symbols>` | the session; written before the child exists |
| `quit` | End local co-op: close now (the pipe's end means the same) |

The parser is strict (printable ASCII, single spaces, fixed keys in a fixed
order, no leading zero, the code's alphabet and length) and canonical; an
over-long line is dropped up to its newline. Anything else is logged (at most
4 times) and ignored. Player 2 checks that `TSSM_LOCAL_PIPE` names a pipe,
stops it being inherited further, and reads the `hello` line right after its
log starts, before the launcher and `iCoopInit`, waiting at most 10 s; without
it, it closes; a `quit` that comes with it closes it too. Later reads never
wait (`PeekNamedPipe` first); a close asked for before Player 2's window opens
is asked again every frame, since opening the window clears it. Player 1's
writes can wait on a full pipe (4 KB), which a few short lines never fill. The
password is in no environment variable, command line, file or log. Both copies
zero their own copies once `iCoopJoin` and the host command have taken it: the
line, the read buffers and the line reader's buffer.

**Player 2's profile** is `<the parent of Player 1's save folder>/local2`
(for a player: `%APPDATA%/tssm/local2`; in a test: `<run>/local2`): `saves/`,
`last_run.log`, `last_run.prev.log`, `crash.log` and `player2.ini`. Its own
save folder and log give it its own single-copy claims, so a third copy on
Player 1's folders is still turned away. Player 1 copies its
`coop_files.cache` there first, so Player 2's files check takes no time.

**Player 2 reads `config.ini` and never writes it** (`TSSM_CONFIG_READONLY`,
`iConfig.h`): no missing settings appended, no file created, `iConfigSave`
writes nothing there. Player 2's own keys (`[coop] name, character,
costume_spongebob, costume_patrick, nametag`; `[input] camera_invert_x,
camera_invert_y, camera_sensitivity, stick_curve, deadzone, rumble`) come from
`player2.ini` alone and are saved only there; any other key in `player2.ini`
is ignored, and any other change lasts until the game closes; its Settings tab
says so. Player 2's name is "Player 2" until it picks one. Player 2 reads Player
1's mods folder and writes nothing there: its `mods.log` goes in its own
folder, and a new load order is not written to `mods.ini`.

**Player 2 never saves**: `iCoopNoSave` and `iCoopNoAutoSave` refuse for a
local Player 2 from its start, not only while connected. It boots straight into
Player 1's scene (as `[game] boot` would, without the logo movies), joins, is
put beside Player 1 once on its first join (`iCoopWorldPlaceSoon`), and
follows Player 1 from then on.

### 12.3 Lifetimes

| What happens | Player 1's copy | Player 2's copy |
|---|---|---|
| Player 2 closes its window, or *Leave local co-op* | its exit (process handle) seen at once: "Player 2 left"; the session stops; the tab offers *Bring Player 2 back* and *Play alone* | exits normally |
| Player 2's copy crashes or is ended | the same within a frame: "Player 2's game stopped unexpectedly", and where its `crash.log` is | (gone) |
| Player 2's copy hangs | the stall rules (5.11): "waiting", the session ends after a minute; the tab says Player 2's game is not responding and *End local co-op* ends it | (hung) |
| Player 1 closes | its exit closes the pipe and waits up to 2 s for Player 2's game: "Player 2's game closed with this one" | the pipe's end: "Player 1's game closed", closes; one hung past the 2 s ends with Player 1's process (the job) |
| Player 1 crashes or is ended | (gone) | ends with it (the job) |
| *End local co-op* | `quit`, then the pipe closed; after 5 s the child is ended (only ever this process's own child) | closes |
| *Bring Player 2 back* | hosts again with a fresh session: a new port and a new password, and `Host::init` clears the bans and lockouts, so five wrong passwords from a program on this PC cannot lock Player 2 out; then starts a new Player 2 | boots, joins, is put beside Player 1 |
| *Play alone* | local co-op off until the next start; the online form again | - |

### 12.4 Security and tests

- Nothing new faces the network: both sockets are bound to `127.0.0.1` and the
  session is section 9's, unchanged. A program on the PC can reach the port but
  still needs the 120-bit password; one that can read Player 2's environment
  can read its memory too, so the pipe is for lifetimes, not secrecy.
- `tssm_nettest` `[7]` tests the pipe's lines (every rule, oversize, NUL, port
  range, alphabet), the environment block and the profile folder, and a port-0
  host on `127.0.0.1` with wrong passwords refused, then blocked, and hosting
  again letting Player 2 in; the fuzz loop has "local pipe lines" and "local
  environment". `tssm_nettest --wrong-proofs` is the test client of
  `local-bringback`.
- `tssm_configtest` (in `tools/regress.py`): a `config.ini` missing settings
  keeps its bytes and time through a load and a save; `player2.ini` takes only
  Player 2's keys; a missing `config.ini` is not created.
- `tools/coop_test.py --variant local` (BB02, DE01) and `local-close-guest`,
  `-close-host`, `-kill-guest`, `-kill-host`, `-close-host-hung`, `-hang-guest`,
  `local-end`, `local-end-hung`, `local-bringback` (`--local-all` runs them
  all). The script starts only Player 1's copy, puts the run's `config.ini` back
  missing settings before Player 2 starts and checks it afterwards, checks
  Player 2's folder has no save and holds its mod log while Player 1's mods
  folder is untouched, and watches for a Windows Firewall prompt (never
  touching one; a prompt ends the run and the runs after it).

---

## 13. Playing as one game: hits that land, and one progress (protocol version 7)

Protocol version 7 fixes what the first long play sessions found. The guest
"sometimes" could not hit enemies or break boxes, as if out of step; in the
combat arenas it could neither hit nor be hit; and in "Three Thousand Miles
to Shell City" the host had the 5 tokens for Mindy's Bash and the guest, with
3, did not get it. The guest's world was never out of step: under lag, loss
and different frame rates every claim measured had the guest's drawing within
1.64 units of the host's. What made hits "come and go" were several separate
holes, each found with two copies and closed below.

Three changes of behaviour that the players chose:

- Enemies go down sooner when both players hit them (each player's hits have
  their own half second, 13.1): wanted, kept.
- The host's save holds all the progress of the session, and the guest's own
  save keeps nothing of it (13.3): wanted, kept.
- A challenge's counters ("N left", "Wave n Complete!") count down on both
  screens, the counts taken from the host and not from the guest's own copy
  of the level (13.4).

### 13.1 Hits that land

- **The host's own culling** (the cause most often met): the host's game hides
  the parts of a level its own player is not in (the JSP areas, entity flag
  0x4), and `IsPresent` counts a culled enemy as not there. A guest in another
  part of the level had every hit refused ("the enemy is not there"), and its
  pickups too ("it is gone"). The host now judges an enemy and a pickup by its
  own state, culled or not (`PresentAnywhere`, `PickAvailable`), and lifts the
  cull flag for the moment a guest's hit is applied, so the game's own damage
  code takes it.
- **One invulnerability for two players**: an enemy ignores hits for 0.5 s after
  one lands (`tmr_invuln`), whoever landed it, and its wound goal takes no
  damage while it reels; so the guest's hit soon after the host's did
  nothing, while the host's log said "applied". In co-op each player now has
  its own half second per enemy, and a hit while the enemy reels from the
  other player's hit is held and lands as soon as it stops reeling (at most
  2.5 s later; `iCoopEnemyHittable`, a hook in `zNMECommon::Damage`). A
  player's repeat hits on an enemy reeling from that player's own hit are
  ignored as in single player, which is unchanged. Enemies fall sooner when
  both players hit them, as two players' hits should. The host's log says
  "applied" only for a hit that landed, "held" for one waiting, and "did
  nothing" for one that did not land. The half seconds and the hold are
  measured in the host's game time (its frames' own time, as the enemy's
  timer counts it), so a stall of the host's game does not run them out
  early. Damage that is no player's attack (a hazard, another enemy) keeps the
  enemy's one invulnerability, as in single player.
- **Enemies a spawner brings back**: a spawner's pool (the arenas' Buck-O-Trons,
  three in DE01) keeps its enemies in the game's dead goal, which turns their
  collision off; only the owner's update brings them back. The guest's copies
  never ran it, so the guest's attacks and its player went through them.
  While the host shows such a copy, the guest puts its collision back as the
  goal's own exit does (`Revive`), and takes it away again when the host hides
  it. The same puts back a copy the guest's own game killed while the host's
  world was not shown (a cut connection), crates included.
- **A crate's box**: a crate's hit box is made from its place only by its own
  update, which a host-owned copy does not run, so a crate that fell (the one
  under it broke) or floated along its path was hit where it had been. The box
  now moves with the copy.
- **The car**: the Patty Wagon's crashes (`DMGTYP_CARSMASH`) were not an attack
  the guest sent. They are now `ATK_CAR` (9), believed only while the host's
  own player is the car (a drive level), within 15 units (the car's bound and
  its speed over the state's age).
- **The upgraded SpongeBowl's lightning** (`DMGTYP_BBOWLZAP`) is `ATK_ZAP` (10):
  SpongeBob only, the shared save's bowl at level 2, within 64 units.
- **Two jumps in 2 s**: the host ignores the guest's position for 2 s after two
  jumps faster than a player can move, and co-op makes such jumps itself (the
  respawn beside the host, the placing after the host's defeat). A jump that
  lands within 8 units of the host's player is co-op's own placing and is not
  counted (a cheat gains nothing by standing beside the host), nor is the
  defeat's own short jump as it starts (about 5.4 units seen; up to 10 units,
  once every 2 s). The jump out of a defeat lands beside the host.
- **Breath and clouds**: an enemy's fog breath, gas, sparks, plasma and exhaust
  (`zNMEParFXType*`) checked only the host's player; the arenas' foggers and
  poppers could not hurt the guest at all. The host now checks each particle
  that can hurt against the guest's centre too (`iCoopParticleHit`), wherever
  the host's own camera looks, and sends the hit as an ordinary `EV_DAMAGE`
  (from a point, the game's particle kind), held back by the guest's own safe
  time as before. A fogger copy that the host's fogger poses in its breath
  attack's loop breathes on the guest's screen too, from copies of the
  game's own breath that hurt nobody (`zNMEFogger_PcBreathShown`): the
  arenas' damage is seen where it comes from.

### 13.2 Boxes and breakables

The boxes are mostly wooden crates (Tiki enemies: 13.1 covers them) and the
breakables: barrels, tire stacks, tables, walls and the like (a static or a
platform with a destructible; TT01 has 126, BB01 142, BB03 212). A guest's hit
on a breakable broke only its own copy. It is now a claim (`EV_HIT` naming the
breakable's net ID, a hook in `zDestructibleAsset_EventCB`), checked as an
enemy hit is (this level, the guest believable and playing, the host not busy,
the attack's reach from the guest, 0.15 s per thing, and the 8-unit slack, 2
units for a static that never moves) and
applied with the event the guest's attack sends (`{1, 9}`, the car's `{1, 1}`),
so the host's own destructible code breaks it and runs its links once; the
break reaches the guest with the snapshots and plays there. Crates and
breakables share a bucket of their own, 20 a second (a car through a pile of
up to 15), apart from the enemies' 12. A crate the host breaks now counts as a
death for the guest (a hook in `zNMEGoalTikiDying::Exit`), whose copy plays the
crate's own splinters instead of vanishing.

### 13.3 One progress

The guest's game counted only what it took itself and its moves were its own
save's, so the two players had two token counts and two sets of moves. Now
the host's save is the one progress of the session:

- `PROGRESS` (host to guest, state, sent when it changes and once a second):
  tokens, shiny objects, upgrade points (earned, left, spent), the ten
  power-up levels (the eight moves, both health upgrades) and this level's
  treasure chests. The guest's game shows and plays by these while it is in
  the host's level: its HUD, its pause menu, Mindy's token gates, the moves
  its player has, the stone crates' check of the SpongeBowl. Elsewhere (its
  own level, with following off or before following starts) it plays by its
  own save, and what it takes there counts for its own save.
- What the guest's own save gets: the first frame the host is in a level, the
  guest's game keeps aside that level as it has it: each shared pickup's
  state, and the level's save block, written then from its own state as an
  autosave writes it (`KeepOwnLevel`). Its own numbers are kept aside at the
  first `PROGRESS`. They are put back when the guest leaves the level (before
  `zSceneSave` writes the level's progress block, so that block only ever
  holds the guest's own numbers), when the host has been in another level for
  3 s (not in a level the guest followed the host to, whose save keeps
  nothing of it, 5.7), and when the session is over (no session and not
  joining again). Leaving the level, its save block is the one kept aside
  (`iCoopKeepSceneSave`): a level reset meanwhile (an out-of-bounds fall, a
  forced reset) writes the block with the host's world in it, and that is
  undone. Nothing taken in the host's level counts twice or goes missing: the
  host's save counted it, the guest's has it still to take.
- A move the guest's game would give its player in the host's level is asked
  of the host (`EV_UPGRADE`, hooks in `zEntPlayerEventCB`): a move a talk
  teaches (Mindy: the host checks that this level's data gives that move and
  that one of its token conditions holds on the shared count) or an upgrade
  bought in the pause menu (the host checks that one of the guest's last 66
  states, about 2 s, said paused, as the game sells upgrades only there: a
  resent event may come with a state from after the menu closed; then its
  own upgrade points and the game's own rule). The host applies it as the game does, without the
  film; it answers `EV_UPGRADE_ACK`, and on yes the guest's game plays the
  upgrade's film. The guest acts only on an answer to an ask of its own (that
  kind and move, within 5 s), once; a refusal's notice names no reason (the
  host's log does).
- Pickups: a token or chest the guest's own game showed (its own task or
  challenge) or gave (its script, a race's end) is asked of the host with its
  way (`EV_PICKUP`'s new byte: touched, shown here only, given here). The host
  cannot check the guest's win, so it takes only a token or chest that this
  level's own data can show or give (its links and scripts, scanned when the
  level loads), once per 5 s, the guest believable within 6 units of it when
  it was shown (a given one may be far: a race ends elsewhere). A given one
  the host refused (busy, too soon after another) is asked again once a second
  while the guest plays, 30 times at most, until it is granted, gone on the
  host, or the level changes: its script gives it once only. `PICKUPS` has a
  second bit array, "there to be taken on the host", so a token the host's
  game shows is shown on the guest too, and hidden again when the host's game
  hides it again (a challenge failed).
- A token the guest took: the host's autosave and the level's "token taken"
  signal now come when the guest's reward animation is over (5.8 s), so a
  main task's token ends the level as the host's own does. The guest's game
  takes a granted token the way a touched one is taken, so its own links run.
  The signal waits while the host is defeated, paused or in a cutscene, and
  goes only with a level change (the autosave it held ready is then off
  again). The host's defeat in those seconds resets its level, which undoes
  what the token's own links did to things the level's save does not keep
  (DE01: the dispatcher that ends the level on the signal, and its counters);
  those links are sent again as the token sent them, not its sounds, before
  the signal (a hook at the end of `zSceneReset`). Several tokens in those
  seconds each give their signal.
- The guest's own level end for that token fades its screen down and sends
  its player to the next level, a portal the guest does not take (the host
  leads): its screen comes up again at once instead of after its fade's own
  wait (2 s in DE01), and the host's level end takes it along.

### 13.4 The challenges' counters and texts, from the host

The combat arenas (AM01-AM04) count each wave with counters of the level.
"N left" is a text box showing `{counter:WAVE_n_DONE_COUNTER_01} left`; the
spawners' links count each enemy born and killed; a wave's counter that
reaches 0 runs the next wave's script ("Wave 1 Complete!", "Wave 2 of 3",
"Are Ya Ready Kids?", "Set", "Go!", then the spawners go on), and after wave
3 the end (`END_SCRIPT_01`: "Wave 3 Complete!", the cages break, the player
is put by the token, "You Win!"). The host's enemies are born and die only on
the host, so the guest's copy never counted: the host's screen said 7 left
while the guest's said 30, the guest never saw "Wave 1 Complete!", and its
own copy of the challenge never ended. Now the guest's counters take their
values from the host:

- **What is shared.** Both games make the same table when a level loads
  (`iCoopHud.cpp`): every counter a loaded text shows (`{counter:NAME}`),
  sorted by asset ID, with the range the level's own data can make of it
  (from its start down to 0, up to the highest `CountN` or `SetCount` sent
  to it, 9999 if something counts it up), and every text box that those
  counters' links, or the scripts they run, write to, with the texts the
  level's data puts in each box. Both check the table's hash. In the game's
  data that is each arena's six wave counters (AM04: one) and two boxes
  (MNUC's `MESSAGE_01_TEXTBOX` for the wave texts and `AMBUSH_TEXTBOX` for "N
  left", with 9 and 3 texts), and the bungee's targets counter in DE01, PT01
  and TR02 (no box: that counter runs nothing).
- **Why only those.** Every level has counters (TR01 66, DE01 21); most count
  each game's own triggers, buttons, voice lines and task screens (DE01's
  `SIDE1_COUNTER_01` sets the pause menu's task state,
  `SECONDARY_TASK_COUNTER` marks a task done). Holding those would stop the
  guest's own level logic, so all counters are not shared. A counter a text
  shows is one a player reads on screen. The arenas' run only what the host
  owns (the spawners, the next wave, the end); the bungee's is counted only
  by the target counters, whose targets are shared breakables (their breaks
  come from the host), and runs nothing.
- **COUNTERS** (host to guest, with its states): the counts that changed in
  the last 0.3 s, so a lost datagram is made up by the next one; everything
  once a second and as soon as the guest is in the level; and each box's text
  (0 not shown, k the box's text k).
- **The guest** puts the host's counts into its copies every frame while it
  shows the host's world (its own reset of the level puts its own back; the
  next frame undoes that), and its own level does not count them:
  `iCoopCounterHeld`, a hook in `xCounterEventCB`, drops this game's own
  events to them. That holds the count and the counter's own Expired and
  Count1..20 links (sent from inside that function); `zEntEvent` still
  handles Enable and Disable and sends the links that start on the incoming
  event itself. In the game's data the only such link is the arenas'
  Decrement to the tick sound, and the guest's game does not decrement those
  counters; the table logs any such link ("... is not held on a guest").
  Its copy of the challenge never expires, so its own scripts never start a
  wave, end, restart, teleport or give a reward. The
  host's end takes the guest along, as before (the host takes the token and
  goes back to BB03; the guest follows).
- **The texts.** Of the ways to show them (running the host's scripts on the
  guest with all but their texts left out; sending each text event; sending
  what each box shows), the boxes' state has the fewest side effects: only
  what two text boxes show changes, nothing of the level's logic runs on the
  guest, and the guest shows only texts of its own copy of the level. A box
  takes the host's text, or is hidden, when the host's box changes; it is
  hidden here only while it shows one of its own list, so the guest's own
  talks in the message box (the arena's opening) are left alone.
- **Its own back.** The guest keeps its own counts and boxes aside when it
  starts showing the host's; they go back when it leaves the level (before
  the level's save, so its save keeps its own), when the host has been in
  another level for 3 s, and when the session is over.
- The arenas have no "try again" prompt: their text files have one
  (`FailureText`), but nothing in the levels shows it. A host's defeat resets
  its level, which puts its counters back, and the guest's go back with them.
- **Checks on the guest**: only from the host; the exact size (11 bytes, 3
  per count, 1 per box); at most 32 counters and 8 boxes, not neither; counts
  ascending, each below the table's counters and within +-9999 (the parser),
  then within that counter's own range in this game's data; each box's text
  at most its own list's length (16 at most); the table's hash and sizes this
  game's own; at most 60 a second (the host sends one with each state, about
  33). Anything else is refused and logged. The host sends only its own
  copy's counts of these counters and indices into the guest's own lists.

### 13.5 What stays as before, and why

- **No other character in a combat arena**: every arena loads one character
  (AM01-AM03 SpongeBob, AM04 Patrick), in single player too. LB there now says
  at once that the choice applies from the next level.
- Not claimed yet: the upgraded Bubble Bash's delayed blast and the upgraded
  Belly Flop's stun ring (6.8); Patrick's slam through a stack of crates.

### 13.6 On the wire (protocol version 7)

| Change | Rules |
|---|---|
| `EV_HIT` attacks 9 (car) and 10 (lightning) | attack 1-10; the ID may name a breakable |
| `EV_PICKUP`, 12 bytes: way u8 | 0 touched, 1 shown on the guest only, 2 given on the guest |
| `EV_UPGRADE` (10), 7 bytes, guest: kind u8, move u8 | kind 1 taught (move 0-7) or 2 bought (move 0-9) |
| `EV_UPGRADE_ACK` (11), 8 bytes, host: kind u8, move u8, result u8 | as `EV_UPGRADE`; result 1 yes, 2 no |
| `PICKUPS`: a second `(count + 7) / 8` bytes | "there to be taken"; past the count zero; never taken and there at once |
| `PROGRESS` (type 12), host only, 20 bytes | tokens u16 <= 999, shiny objects u32 <= 99,999, points earned u8 <= 14 (the game's table has 14 entries, `zRewardsMgr::upgradeSlotsPoints`), left u8 <= 15, spent i8 -15..15, moves u8 x 10 (0-7 <= 2, 8-9 <= 3), chests u8 <= 99 |
| `COUNTERS` (type 13), host only, 11 + 3 n + boxes bytes | scene u32 (a level ID), table u32, counters u8 <= 32, boxes u8 <= 8 (not both 0), n u8 <= counters; n x (index u8 < counters, ascending; value i16 -9999..9999); boxes x text u8 <= 16. The guest then checks the table against its own, each value against its counter's range and each text against its box's list (13.4) |

A field out of its range, a byte too many or too few, or a message from the
wrong side is a protocol violation. A 0.9 co-op copy (protocol 6) and this one
refuse each other at the handshake, each saying the other's game is a
different version; the co-op version code changes with it. `tssm_nettest`
checks every rule above, round trips from each side, a live session's
`PROGRESS` and `COUNTERS` both ways, the merging of `COUNTERS` that come
before the game takes one, and the fuzz loop has seeds of every new message.
The host clamps its own points earned to 14 as well.

### 13.7 Tests

`tools/coop_test.py` has a variant per fix:

| Variant | What it shows |
|---|---|
| `far-area` | DE01: the host at the start, the guest at crates in a culled part: hits applied |
| `both-hit` | DE01, 40 ms lag: the host's spin and the guest's within half a second fell the 2-health robot |
| `drive` | BB01: the guest's car breaks the pile of crates |
| `arena` | AM01: hits on spawned foggers applied, a fog breath hurts the guest and is drawn there, LB said at once |
| `boxes`, `boxes-resync` | BB02: the crate that fell breaks; crates broken in the guest's own world are whole and break again |
| `breakables` | TT01: the guest's karate breaks barrels on the host |
| `zap` | BB02: the lightning's hits applied |
| `tokens` | DE01: one count, Mindy teaches both the Bash, the guest's own progress back after |
| `maintoken`, `given` | DE01 main token ends the host's level; BB01 a script-given token counted |
| `upgrade` | DE01: the guest's pause-menu upgrade goes to the host's save; one with the menu closed is refused |
| `own-level` | BB02, following off: the host leaves, the guest's own progress is back, its crates' shiny objects are in its autosave after the session |
| `maintoken-death` | DE01: the host defeated 5 s into the guest's main-token reward: its level still ends; the guest's screen comes up after its own fade |
| `maintoken-stop` | DE01: the host stops hosting 1.75 s into the guest's main-token reward: the token's level signal still fires on the host with co-op off |
| `keep-reset` | DE01: the guest's level reset (a lost chance) in the shared level writes its block; leaving, the block it had before the host's world is put back |
| `arena-waves` | AM01: the guest's kills count "N left" down; the host's own counters end each wave (`TSSM_TEST_SEND`: the count to 1, then a decrement) and, after wave 3, the arena. Every count the host had for 100 ms and every change of its two boxes shows on the guest within 500 ms; the guest's own decrement of wave 1's counter is held, and it shows no wave text of its own |

### 13.8 Limits

- In a challenge, the sounds of the host's scripts and counters (the tick as
  "N left" goes down, the jingle with "Wave n Complete!") and its camera at
  the end (the cages breaking) are the host's only; the guest sees the texts
  and counts, and is taken along by the host's end.
- Only counters a text shows are shared (13.4); other counters stay each
  game's own, as the levels' own logic needs.
- A token or chest the guest's own task shows or gives is believed within the
  bounds above; the host cannot check the task itself.
- The guest's save keeps nothing of the session: tokens and moves earned
  together are the host's save's (7.6). What it takes in its own level, the
  host elsewhere, is its own.
- In a level the guest followed the host to, the guest keeps the host's
  progress after the host has gone (following off) until it leaves that
  level; nothing it takes there counts, as its save keeps nothing of that
  level (5.7).
- Only the foggers' breath is drawn on the guest; other harmful particles of
  host-owned enemies (sparks, exhaust) hurt the guest but are drawn only on
  the host.
- A guest's hit on a breakable is applied with its attack's own event and the
  player's `{1, 9}` (the car's `{1, 1}`, the cruise bubble's `{1, 0}`), not
  the guest's exact parameter; every destructible checked takes it so.
- Mindy's lesson is believed when any token condition of the level holds on
  the shared count, not only the one before that lesson (TR02 has 10 and
  20); the levels' data teaches each move only past its own gate.
- A hit of the host's while an enemy reels from the guest's and its damage is
  off (part of some wound reactions) is still the game's own: ignored. The
  guest's is held in that case too.
- In an arena, an enemy flung out of a spawner (30-40 units a second) can be
  drawn a sample behind, up to 1.3 units, for a frame, under 80 ms of
  simulated lag and at times with a 32-bit guest; `--variant netsim --fight
  arena` fails a position row for that (1.16 units in the last run) while
  every fix row passes, and the 32-bit guest's arena failed it in some runs
  (it passed 28/28 in the last). Without lag the arena passes.
- Tested mostly as two copies on one PC.

---

## 14. Co-op on the home network, across devices (protocol version 8)

The aim: a PC, a phone and a Switch on the same Wi-Fi see each other's games
in a list and join with the host's short code; over the internet, joining by
address stays as it is. Co-op stays two players, and a separate co-op build on
every platform (a PC build, a co-op app beside the normal one on the phone, a
co-op `.nro` on the Switch), using the normal game's save and game-files
folders.

### 14.1 One co-op version on every device

Up to protocol 7 the build ID was the git commit a tree was configured from
(2.5). The PC, Android and Switch branches are different commits, so they
would have refused each other as "a different version" with the same co-op
code, and a build directory that was not configured again kept an old ID.

**The fingerprint.** From protocol 8, `tools/coop_id.cmake` hashes the git
object IDs of what the two games must agree on:

- the trees `src/SB/Game`, `src/SB/Core/x`, `src/SB/Core/gc`, `include`,
  `src/rwsdk` and `src/dolphin` (the game code, with co-op's hooks in it);
- the files `iCoop*` (the service, the asset hash, the puppet, the shared
  world, combat, flow, looks and the challenges' counters, `iCoopHud.cpp`),
  `iNetProto.*`, `iNetSession.*`, `iNetWire.h`, `iNetDisc.*` (from the next
  step) and `iCutscene.cpp` (the cutscene wait both games keep in step,
  8.12), all in `src/SB/Core/pc`.

Left out, because they do not change what the two games agree on:

- local co-op's files (`iCoopLocal*`): the pipe between two copies of one
  program on one PC, never between devices;
- librw: renderer code, and every PC configure applies the committed
  `tools/librw/*.patch` to it, so its state would give every PC build its own
  ID;
- the socket backends, `iNetCrypto.cpp` (checked against published test
  vectors on every platform), the GUI, the platform folders, the tests,
  tools, documents and build files.

A working-copy change to a covered file is mixed in by content (`git status`
first, so a file only touched does not count): a changed copy gets its own
ID, and two copies with the same change still match. Without git the
fingerprint is `unknown`, with a warning, and such a build plays only with
other `unknown` builds. No package is made from such a build: the release audit refuses
co-op version `5d04eb33` (the `unknown` ID), and each platform's build (the
Gradle CMake run too) must show that it found git.

The ID is computed at **build** time: the `tssm_coop_id` target runs the
script at every build and rewrites `coop_id.h` only when the text changes, so
nothing recompiles otherwise. The build ID is then the first 16 bytes of
SHA-256(`"tssm-coop build/2 <fingerprint> proto 8"`), and the co-op version
code its first 8 hex digits, as before. The script prints that code, so any
checkout can be checked by hand:

    cmake -DSRC=<repository> -P tools/coop_id.cmake

**The rule that keeps it equal:** an Android- or Switch-only change never
touches a covered file. Platform code in those files goes in on the co-op
branch under its `#ifdef`, as the random sources below do.

**Join codes.** The code alphabet (Crockford's base 32) has no O, I or L, so
protocol 8 reads them as 0, 1 and 1 on both sides, as it already dropped
spaces and dashes and ignored case (`iNet::normalize_code`). A typed password
goes through the same rule on both sides.

**Older versions.** A protocol 7 copy and a protocol 6 one refuse this one at the handshake, and this one them, each
saying the other's game is a different version.

**The network core on the other platforms**, each under its `#ifdef`:

- Android: `arc4random_buf`. The APK's minimum is API 24 and `getrandom` is
  API 28, so `iNetCrypto.cpp` did not compile for it before (8.9).
- Switch: csrng, and the kernel-seeded `randomGet` if csrng cannot start
  (Horizon has no `/dev/urandom`; the log says so once); sockets IPv4 from the start; and
  `udp_wait` polls without waiting and sleeps 2 ms between polls, because a
  blocking BSD call holds one of libnx's few service sessions while it waits.

**The game-files code.** Next to the co-op version, the Co-op tab shows
"Game files: `1a2b3c4d`, 139 files": the first 8 hex digits of the game-files
hash this game last sent or checked, and how many files it covers, from the
first host or join on. The log says `coop: game files code CODE, N files`.
Two players refused for different game files can see whose differ. It is no
secret: every joining game sends the whole hash.

### 14.2 Finding games on the home network

A host's game answers questions from its own home network, so a joiner can
list it without typing an address; the join is then the ordinary handshake,
with the host's code. This step is the Windows side and the network part; the
Co-op tab's list, the host buttons and the code pad are 14.3.
`iNetDisc.h/.cpp` holds the two messages, the
checks and the tables as pure code (fuzzed in-process, like `iNetProto`); the
I/O thread in `iCoop.cpp` owns the sockets.

**The two messages,** on UDP port **2172** (`kDiscPort`), never the game's
port, so the session parser never sees them and nobody forwards 2172:

| Message | Bytes | Layout (little-endian) |
|---|---|---|
| `DISCOVER` (joiner to port 2172) | exactly 128 | magic `TSDQ`, `disc_version` u8 = 1, flags u8 = 0, nonce[8], zero padding |
| `OFFER` (host to the question's source, unicast) | 45 to 60 | magic `TSDR`, `disc_version` u8 = 1, `proto_version` u16, nonce[8] (the question's), build ID's first 8 bytes, game-files hash's first 8 bytes, game port u16 (1024-65535), instance u32 (random per hosting, not 0), state u8 (0 waiting, 1 has a player), platform u8, scene u32 (0, or four of `A-Z 0-9`), name length u8 (1-16), the name (`name_valid`) |

- **The same prefix in every version:** magic, `disc_version`,
  `proto_version` and the nonce at offsets 0, 4, 5 and 7 of an `OFFER`. A
  version 1 `OFFER` is parsed strictly and canonically (it re-encodes byte
  for byte). An answer with a valid prefix, a known nonce and another
  `disc_version` is listed as "different version", with its address only.
  A question of a newer version (128 bytes, the same magic and nonce place)
  is answered with this version's `OFFER`, so a newer joiner still lists
  this host. A version 1 question must have flags 0 and all-zero padding.
- **Platform:** 1 Windows, 2 Android, 3 Switch, 4 Linux, 5 macOS. Any other
  value is listed as "other device", not dropped.
- **Not in the answer:** the code or password, or anything derived from it;
  any address (the datagram's source is the address); the partner's name or
  address; save data, positions, paths; the device's own name, account,
  serial or MAC; the commit. The name is the host's co-op name.
- **No amplification:** an answer (60 bytes at most) is always smaller than
  the question (128), and a newer version's must stay under 128 too.

**The host (responder).** It answers only while hosting with **Visible on
this network** on (`[coop] lan_visible`, on by default), never as a loopback
host (`[coop] bind = loopback`) and never as local co-op's host. Its socket
is IPv4, bound to `0.0.0.0:2172` with `SO_EXCLUSIVEADDRUSE` on Windows (no
other program can take the port), and no `SO_REUSEADDR` anywhere. If 2172
cannot be opened (another copy of the game, another program), hosting goes
on and the tab's status says that others can still join by address. Checks,
in order, each failure silent:

1. the size (128), the message (magic, version, flags, padding);
2. the source: IPv4, port 1024 or above, in 10/8, 172.16/12, 192.168/16 or
   169.254/16, **and** inside one of this device's own interface subnets
   (re-read every 10 s), but neither a subnet's network or broadcast address
   nor this device's own address, so a forged source cannot turn the answer
   into a broadcast or aim it at this device's sockets. Anything from the
   internet, for example through a port forwarded by mistake, gets nothing;
3. that address's rate: 2 answers a second, a burst of 4, from a table of 32
   addresses (the one heard from least lately is replaced);
4. everyone's rate: 20 answers a second.

A bucket keeps the time it has not yet turned into tokens, so packets that
come closer together than one token's time still add up to the full rate.

No allocation and no log line per packet; with `TSSM_COOP_LOG` a line every
5 s counts the answers and the drops (size, malformed, source, rate).

**The joiner (finder).** A search runs for a minute ("Search again" starts
another); hosting or joining ends it. Each second it sends one question (one
fresh nonce, from the system's random source) to: 255.255.255.255 and the
subnet broadcast of each interface, both every time (`disc_ask_targets`:
some systems take 255.255.255.255 into a VPN app's tunnel, others drop a
subnet broadcast), from a socket **bound to that interface's address** (so a
broadcast leaves by that interface and never by a VPN that holds the default
route); and to the last host joined
(`[coop] address`), but **only when that is a private IPv4 address**, so no
question ever goes to an internet host. An answer counts only if: it comes
from a private IPv4 address and from port 2172 (checked first, so strangers
cannot use up the rate); at most 64 are handled a second; it parses; and it
echoes one of the last 4 questions' nonces. The list holds 8 games, one row
per (address, game port, instance), so the same answer arriving over two
interfaces is one row, and at most 2 rows per address, so one device cannot
fill the list by changing its instance; a ninth is dropped; a row
goes 4 s after its last answer. Each row says **Ready** (same co-op version
and game files), **different version** or **different game files**, from
the two prefixes; the joiner's own game-files hash is taken when the search
starts.

**Interfaces** (`if_list` in the socket backends, filtered by
`iNetDisc.cpp`'s pure `if_filter`): up, hardware, broadcast-capable, neither
loopback nor point-to-point; their private IPv4 addresses with a prefix of 8
to 30 (not the network's own address or its broadcast, and only addresses in
use); a 169.254 address only on an adapter with no other (two devices on a
cable); at most 8.

- Windows: `GetAdaptersAddresses` and `GetIfEntry2`'s `HardwareInterface`
  (`iphlpapi`); tunnels, PPP and type 53 (the virtual adapters VPNs use)
  count as point-to-point. On the development PC that keeps the Wi-Fi
  adapter and leaves out the VPN adapter, which keeps `10.5.0.2/16` while
  disconnected. An adapter Windows does not mark as hardware is left out,
  so a PC whose address sits on a Hyper-V external switch finds no home
  network (LAN.txt says to join by address then).
- Android, Linux, macOS: `getifaddrs`, with `IFF_UP`, `IFF_RUNNING` and
  `IFF_BROADCAST`, without `IFF_POINTOPOINT` (tun VPNs) or `IFF_LOOPBACK`.
- Switch: nifm's one current network (`nifmGetCurrentIpConfigInfo`).

**Joining from the list** goes to the answer's source address and the game
port it gave. **The address is kept only once the session is up:** a join
from the tab (by address or from the list) writes `[coop] address` after
the host has proved the password, from the parsed numbers
(`192.168.0.23:2171`), never the typed text; a join that never connects
writes nothing. That is the one network value that reaches `config.ini`
(9, rule 14).

**Tests only** (loopback, so no test opens a socket the network or the
firewall sees):

| Variable | Meaning |
|---|---|
| `TSSM_COOP_LAN_PORT` | port: discovery on 127.0.0.1 at this port instead of 2172; the host answers 127.x sources only, and a loopback host is visible |
| `TSSM_COOP_LAN_TARGET` | `127.x.x.x[,...]` (with `TSSM_COOP_LAN_PORT`, up to 4): the finder asks these instead of broadcasting |

**The code** (for the Co-op tab, 14.3): `iCoopFindGames(on)`,
`iCoopGetFound`, `iCoopJoinFound(address, code, name)`,
`iCoopLanVisible` / `iCoopSetLanVisible`, `iCoopPlatformName`; the status's
`lanAnswering`, `lanNote` (why not visible, or why the list stays empty),
`finding` and `findLeftSec`. The tab's test actions (`TSSM_TEST_COOP_TAB`):
`find=on|off`, `pick=NAME`, `joinpicked`, `visible=on|off`.

**Tests:**

- `tssm_nettest` group [8]: both messages (every size, the magic, the
  versions, flags and padding, each field's range, the name rule, a NUL
  inside the name, trailing bytes, re-encoding); a newer version's offer
  listed as "different version" with its address only; an unknown platform
  listed as "other device"; the responder's silence for the internet, a
  private address on no network of this device, another subnet, a source
  port below 1024, loopback outside the test mode, IPv6 and every malformed
  question (with no rate-table work for it); one address's flood held to
  the burst and 2 a second, 40 addresses held to 20 a second, every answer
  smaller than its question; the finder's wrong nonce, the fifth question
  back, the wrong port and the internet dropped, the same answer twice one
  row, a ninth game dropped, expiry at 4 s, 64 answers a second; the
  interface filter over the development PC's recorded adapter list (Wi-Fi
  in, the VPN out, connected or not) and Android-style lists; the last-host
  probe skipped for public, IPv6, broadcast and loopback addresses; who is
  visible; and a responder and a finder over 127.0.0.1.
- Three fuzz targets (the messages; a question into a responder from a good
  source and four bad ones; an answer into a finder from the home network
  and the internet), in the built-in loop and libFuzzer, with seeds.
- `tools/coop_test.py --variant lan`: a guest finds the host through the
  finder (loopback test hooks), joins from the list with a wrong code
  (refused, nothing written), searches again and joins; a prober checks the
  host's answers and its silence for the hostile cases (the tool's
  help).

### 14.3 The home-network tab

The Co-op tab (`gui/iGuiCoop.cpp`) on top of 14.2. With co-op off it shows,
under the player's name:

- **Join a game**: the list **Games on this network** and **Join by
  address** (folded; the internet's form, opened by itself when the game
  started in join mode without a password).
- **Host a game**: **Host on this network** and **Host over the internet**.

The players' guide is `docs/LAN.txt`; `HOSTING.txt` and `JOINING.txt` start
with a "same home network" part that points to it. All three are staged
beside the executable.

**The list.** The tab starts a search by itself when it opens with co-op off
(it was not drawn in the last frame, and more than half a second ago) and again when a
host or join ends; a search runs a minute, **Search again** starts another.
It stops once the tab is no longer drawn for three frames and half a
second (the overlay closed, another tab: `iGui::CoopFrame`, every frame; one
long frame, a load, while it is shown does not count), so the socket is open
only while the list is on screen. When a search that listed games ends, the
list says "The search ended after a minute: press Search again to look
again." instead of the checklist. A debug capture (`TSSM_GUI_CAPTURE`) starts none unless the loopback test
mode is on, so no capture run opens a network socket. Until the first answer
can be judged the header says "checking your game files...". Each row: the
host's name, its device (`iCoopPlatformName`), its level
(`iCoopSceneName`: the game's own name for the scene ID, "title screen" for
the menus, the four characters for an ID this game does not have, "not in a
level" for none), "waiting" or "has a player", and **Ready**, **Different
version**, **Different game files** or **Full**. Every network text is
drawn unformatted (`name_valid` names; scene IDs of `A-Z 0-9`). Rows are 1.7
font heights tall, 2.5 on a touch screen. The headers are plain text in a
header row, not cursor stops. A click, a tap or A on a controller picks a
row (the pick is checked against the list after the rows are drawn, so it
holds in the frame it is made). Picking a row that is not Ready says why in
a sentence:

| Row | The tab says |
|---|---|
| another version | "Different version: both need the same co-op version (yours `xxxxxxxx`, theirs `yyyyyyyy`)." The codes are the build prefixes' first 8 hex digits (`Found::build`), the same code the tab shows at its foot |
| other game files | "Different game files: both need the same release and region, and the same mods (or none). Game files codes: yours `xxxxxxxx`, theirs `yyyyyyyy`." |
| full | "This game already has two players. Co-op is for two." |
| a newer discovery version | "A newer co-op version: both need the same co-op version. Update this game to join it." (no Join: only its address is known) |

Join is still offered for a row that is not Ready; the handshake's refusal
then says why, as for a join by address (8.2).

**Joining.** **Join** under the list, Start on a controller while a row is
picked, or a double click on a row opens the code entry for that game: "Join NAME's game (Windows)", the code as typed in
large letters with a dash after every four, a **Join code** field and the
**code pad**. The pad opens by itself when the row was picked with a
controller (ImGui's last navigation came from a gamepad) or a finger (the
mouse source is a touch screen); **Code pad** opens it for a mouse. A mouse
and keyboard player just types: the field takes the keyboard focus.

- **The code pad:** the 32 symbols of the join-code alphabet in 4 rows of
  8, then **Delete**, **Clear**, **Keyboard** and **Join**; at most 12
  symbols (a longer password is typed). A controller's cursor starts on the
  first key. The d-pad moves (ImGui's navigation), A types, **B deletes**
  and on an empty code closes the pad, **Y** puts the keyboard in the field,
  **Start** joins (not while Back is held: Back+Start closes the overlay;
  not the Start that opened the entry, in the same frame).
  B, Y and Start are routed to the pad (`ImGui::Shortcut`, global route), so
  ImGui's own B does not drop the cursor. The pad scrolls into view when it
  opens.
- **The keyboard:** when the field becomes active the tab starts SDL's text
  input with `SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_VISIBLE` (an Android keyboard
  neither learns nor suggests the code), capitals and no autocorrect; when it
  ends, the next field starts with the defaults again.
- The join goes through `iCoopJoinFound`: the listed address and game
  port, or the game kept when the entry opened (`iCoopKeepFound`), so the
  entry still joins after the search's minute ends and the list empties.
  The address form then holds that address and the code, and **Back** works
  as for a join by address. **Try again** after a list join the host
  refused opens the code entry again, empty, so a wrong code is not sent
  twice (each counts toward the host's lockout, 8.2); after no answer or a
  lost session it joins again with the same code. A refusal scrolls into view, as the view may
  still be where the pad was.
- The "Join by address" form opens by itself the first time the checklist
  shows.

**Hosting.** **Host on this network** is one press: a new join code,
"Visible on this network" on, the port `[coop] port`. **Host over the
internet** opens today's form (port, the code or a password of one's own,
**Start hosting**) and hosts with "Visible" off. While hosting the tab shows
the join code in large letters (a typed password is never shown), who others
see ("Others on this network see NAME (Windows) in their list"), the
**Visible on this network** checkbox (for this hosting; `[coop] lan_visible`
is for hosting from the start), this device's addresses on its home networks
with the game port ("On this network: 192.168.0.23:2171", from the interface
list of 14.2, read again every 10 s), and for internet hosting the port to
forward. On Android and the Switch it also says that leaving the game for
more than about 10 seconds ends the session.

**The code on screen** (`[coop] show_code`, on; Options: "When hosting on
this network, show the join code on screen"): while the host waits for a
player, the HUD layer draws "Join code: K7QX-M2PD-9TRW" at the bottom right
of the picture, large, so a couch player reads it off the TV. Only for a
host that may be found on the network (the status's `lanHost`: never local
co-op's host, never a loopback host outside the loopback test mode) while
"Visible on this network" is on (so never for "Host over the internet"
unless it is ticked: a stream or a screen share shows no code), only for a
join code, hidden while the overlay is open and once a player is in.
The log says when it is shown or hidden, never the code.

**Texts** (constant):

| Situation | The tab says |
|---|---|
| no usable network | "No home network found: turn on Wi-Fi (or connect a cable). Over the internet you can still join by address." |
| nothing found for 5 s | "No games found yet. Check that:" both devices on the same Wi-Fi or network (not a guest network); the host is hosting, with "Visible on this network" ticked; any VPN is off (a VPN app on a phone: pause it); a PC host lets the game through Windows Firewall on private networks; a phone or Switch has Wi-Fi on. "Or join by address below: the host's Co-op tab shows it." |
| the search ended, games had been listed | "The search ended after a minute: press Search again to look again." |
| a host whose 2172 is taken | "Others on this network can't see this game automatically (port 2172 is in use by another program or another copy of the game). They can still join with the address below." |
| hosting, not visible | "Not in other devices' lists: they can still join with the address below." |
| a row | the table above |
| the phone or the Switch, hosting | "Leaving the game (Home, sleep) for more than about 10 seconds ends the session; the other player then joins again by themselves for a minute." |

"This phone is on mobile data" and "Wi-Fi is off (flight mode?)" need the
platforms' own network state and come with the phone (N4) and the Switch
(N5).

**The code** (`iCoop.h`): `iCoopFoundGame::version` and `::files` (the
host's codes; `Found` keeps both prefixes), the status's `lanHost`,
`lanAddressCount` and `lanAddresses`, `iCoopShowCodeEnabled` /
`iCoopSetShowCodeEnabled`, `iCoopThisPlatform`, and `iCoopSceneName` (game
side, in `iCoopPuppet.cpp`). `iGuiTestShot` (`gui/iGui.h`) writes one picture
of the overlay, or of the HUD layer alone, for the tests.

**The tab's test actions** (`TSSM_TEST_COOP_TAB`, up to 24 steps): besides
14.2's, `hostlan` (Host on this network; `host` is now the internet form's
Start hosting), `listed` (the rows in the log), `pick=NAME` (also logs the
row's sentence; it sets the pick directly, not through the row),
`click=NAME` and `tap=NAME` (a mouse click or a finger's tap on that row,
pushed as SDL mouse events, so the row's own widget picks it or not),
`joinpicked[=pad]` (the code entry, with the pad),
`pad=TEXT` (pressed on the pad's keys), `code=TEXT` (typed; the code field
lets go of the keyboard first, so either works in a later frame than
`joinpicked`), `joincode`, `padview` (whether the code pad, as last drawn, is
all on screen),
`vpad=a.right.b.start...` (a virtual SDL controller plugged in, pressing
those buttons 4 frames each with 4 between, then unplugged), `open=on|off`
(the overlay), `shot=NAME` and `hudshot=NAME` (`TSSM_CAPTURE_DIR/NAME.png`).
No action logs a code. Every run of `tools/coop_test.py` sets
`TSSM_COOP_LAN_PORT` (the game port plus 100), so any search or visible host
in any run stays on 127.0.0.1. `tryagain` after a refused list join opens the
code entry again, as the button does.

**Tests** (`tools/coop_test.py`, all through the loopback test hooks; the
pictures in each run's `frames` folders):

- `lan`: both start with co-op off. The host presses **Host on this
  network** (its tab and the HUD's code captured). The guest's tab searches
  by itself and lists the host. Through the real widgets, a virtual
  controller moves down to the row, picks it with A and opens the code
  entry with Start (its pad opens by itself), then types the first keys of
  a wrong code (A on 0, right, A on 1, down, A on 9, B deletes it); the
  rest on the pad's keys; refused (the list, the pad and the refusal
  captured). Back, the overlay closed (the search ends) and opened (the tab
  searches again); the test actions open the code entry, the search is
  stopped as when its minute ends (captured: "The search ended..."), and
  the right code on the pad and the controller's Start still join. Then 14.2's
  prober and the normal run's checks. The host's HUD code is shown while it
  waits and hidden once the guest is in; no code in either log or
  `config.ini`.
- `lan-version`, `lan-files`: the row and its sentence with both codes;
  joining anyway is refused by the handshake. In `lan-version` a tap (SDL
  touch-mouse events) picks the row.
- `lan-full`: a third copy lists the host as "has a player", "Full", and its
  join is refused as full (after the client's own 6 s of asking again); the
  session goes on. A mouse click (SDL mouse events) picks the row.
- `lan-none`: the guest's list stays empty and shows the checklist; the
  host's discovery port is held by the test, so its tab says others can't
  see it; the guest joins by address.
- `lan-hidden`: **Host over the internet**: not listed; ticking **Visible on
  this network** lists it, unticking takes it off within 4 s; the guest
  joins by address. The host's corner code is shown only while it is
  visible.
- The 32-bit guest for `lan` and `lan-version`.
- Local co-op (`--variant local` and the lifetimes): Player 1's discovery
  test port gets questions while it waits and once Player 2 is in, and never
  answers; neither game's HUD shows a code.

### 14.4 The phone and the Switch

Both are co-op builds of their own beside the normal game, on the normal
game's folders. The players' side is in
`docs/LAN.txt`; the details in `docs/ANDROID.md` ("The co-op app") and
`docs/SWITCH.md` ("Co-op on the home network").

**The phone** (the `coop-android` branch): the app `io.github.tssmport.game.coop`,
"TSSM co-op", installed beside the normal app `io.github.tssmport.game` and
signed with the same key. Its version name ends in the co-op version code,
as the Switch's does.

- The saves are the normal app's, which Android keeps private to it. The
  normal app (from build 14 on) shares them through
  `SaveShareProvider`, guarded by a signature permission, and every call
  checks the caller's certificate itself. The co-op app takes the save
  folder's one-copy lock through it, copies the saves into its own folder,
  plays on that copy with the unchanged save code, and sends each change
  back; the normal app writes it as the game does (temporary file, then
  replace, backups kept), and only over the file the co-op app last saw.
  A change not sent when co-op ends is sent at its next start, if the real
  save is still the one co-op last saw.
- `config.ini` is shared the same way: co-op starts from the normal app's at
  every start and sends its changes back over the file it started from.
- The game files are the normal app's folder, read where it is.
- Each app ends its process when its game quits from the menu, so the other
  can be opened at once.
- Overlay: three fingers on the screen, or Back+Start on a controller.
  Hosting on the home network needs Wi-Fi; on mobile data "Host on this
  network" is off.

**The Switch** (the `coop-switch` branch): `tssm-coop.nro`, "TSSM co-op (port)",
beside `tssm.nro`, version `0.9-<code>`. It shares `/switch/tssm/` (saves,
`config.ini`, game files) and keeps its log in `/switch/tssm/coop-log/`.

- Both programs claim the save folder with a lock file held open for
  writing, for the case of one in title mode and one from the Album at the
  same time.
- Sockets: six BSD sessions (three when six are refused), waits that never
  block, the sockets made again after sleep; the network thread off the
  game's core.
- The log server on TCP 28772 runs only under nxlink or the armed test kit,
  in both programs.
- Overlay: hold - and press +. The Wi-Fi safety text is in `LAN.txt` and
  `SWITCH.md`.

**What only the devices can show:** broadcast on the phone's and the
console's Wi-Fi, the keyboards' types, the time a co-op save takes on the
phone, the Quit-then-open-the-other case on the phone, whether Horizon
refuses the second write open, and the console's csrng, sessions and core.
Each platform's guide lists the first session's checks.

---

## 15. Co-op through a relay server

### 15.1 Why

Playing over the internet needed the host to forward a UDP port on their
router (`HOSTING.txt`). Many players cannot: their provider shares one public
address between many homes (CGNAT), or the router is not theirs. A relay
server fixes that. Both games connect out to it, so neither player forwards a
port or needs to know an IP address. Direct hosting, the home network and local
co-op stay as they were.

### 15.2 Topology

```
host's game  <--ENet-->  relay server  <--ENet-->  guest's game
```

The server is a small dedicated ENet program (`server/`), modelled on the
servers of partyboard and smb2-pc. Those carry lockstep input. This one carries
the datagrams of the host-authoritative co-op instead: the host's game still
runs the world, the guest's game still shows it. The server only seats the two
games in a room and passes their datagrams on. It never runs the game.

The messages are listed at the top of `include/netplay/protocol.h`. The game's
side is `src/SB/Core/pc/iNetRelay.cpp`.

### 15.3 Room code and join code

Two codes are used, and they do different jobs:

- The **room code** (4 characters, made by the server) only routes. It tells
  the server which two games belong together. It replaces the IP address and
  the port.
- The **join code** (or the host's own password) is still the session's
  password, as in direct hosting. It is checked end to end between the two
  games and never goes to the server.

The host shows both. The guest types both. A player who learns a room code
cannot join without the join code. The server cannot read the game traffic or
forge it: every handshake and session packet is authenticated with a key that
comes from the join code, which the server does not have (section 9, item 11).
At worst a hostile server can drop packets, which the game treats as a bad
connection.

### 15.4 Virtual member addresses

The session code (`iNetSession`) works with a source address on every datagram.
So that it runs unchanged, each relay member gets a virtual IPv6 address in the
`100::/64` discard prefix (RFC 6666), with the server's connection number in the
last bytes. No real traffic ever goes there. The host sees its guest at one
such address, and a guest sees the host at another. The address-based rules
(one guest only, the lockout after wrong passwords, kicking) then apply as they
do to a real address. A guest that leaves and joins again has a new connection
number, so a kick lasts for that connection only.

### 15.5 Channels and limits

- Channel 0 carries the control messages (create or join a room, leave, the
  room's state): reliable and in order.
- Channel 1 carries the co-op datagrams: **unreliable and unsequenced**. The
  session has its own acknowledgements, resends and ordering. A reliable
  channel under it would only add waiting when a packet is lost.
- Each member may send 600 datagrams a second. More are dropped. A datagram is
  at most 1,200 bytes, the same as without the server.
- A room holds two players. A third is refused with "room full".

### 15.6 When someone leaves

- **The guest leaves** (or loses the connection): the room stays open. The
  host's game says the guest left and waits. The same player or another can
  join with the same room code and join code. The automatic rejoin (section 8)
  works through the server as well.
- **The host leaves**: the room closes at once. The guest is told, and sees
  "Disconnected".
- **The server is lost** (network down, server stopped): both games see it as
  a lost connection. The rooms are gone when the server restarts, so the host
  starts a new one.

### 15.7 In the game

The Co-op tab has a "Co-op server" box, the guest's room code and join code,
and two buttons: "Host on the server" and "Join on the server". Hosting shows
the room code (and "asking the server for a room..." until the server gave it)
and the join code, each with a Copy button, and the round trip to the server.
Joining and the errors work as for a direct join.

The setting is `server` in `[coop]` of `config.ini`: "host" or "host:port",
default `10.0.0.5:7865`. It is written once a room is entered, so the next start
has it ready. Another server is another value there.

### 15.8 The server

`server/` holds the program (`main.cpp`), its build (`CMakeLists.txt`), a test
client (`test_client.cpp`), a `Dockerfile` and `portainer-stack.yml` to run it
as a container (for example from Portainer), and its own `README.md`. One UDP
port (7865 by default) must be reachable by both players.

### 15.9 Limits

- One server instance. There is no list of servers and no hand-over between
  instances.
- Rooms are in memory only. A restart ends every room.
- No finding of games on the home network for a host that is on a server: the
  list of "Games on this network" does not show it. A friend joins with the
  codes.
- The relay adds the trip to the server to the ping. A server near both
  players matters more than their bandwidth.
- The server sees who plays and when (addresses, names, room codes), not what.

---

## 16. Boss fights: the Frogfish, and one health (protocol version 9)

Until protocol 9 each game ran its own boss for its own player. Protocol 9
shares the first boss, the Frogfish (B101), the same way as the other enemies,
and carries what a snapshot cannot: the fight itself. The other bosses (Dennis
in B201 and B301, King Neptune in B401) are still each game's own.

### 16.1 How it works

- **The Frogfish and its tongue are shared enemies.** They are in the level's
  table (`EnemyType`, iCoopWorld.cpp). The host's game runs the fight; the
  guest's copy thinks nothing and is posed from the host's snapshots (5.5).
- **The fight's state goes to the guest as EV_BOSS.** On change and once a
  second (for a guest that arrives mid-fight), the host sends the boss's state,
  stage, health, current attack and camera state (iCoopBoss.cpp). The event is
  reliable and host-only; its fields are checked against fixed limits
  (iNetProto.cpp), then against the game's own enum ranges before use.
- **The guest's copy shows it** (`zNMEFrogfish::CoopGuestShow`): the health
  meter, the sounds a new state starts with (as `EnterState` plays them), and
  this game's own boss camera. The camera follows the guest's own player; when
  the host's camera locks its player still (the jump to the tongue), the
  guest's does too, at the same moment.
- **The boss can go for the guest.** On the host, the Frogfish's update runs
  with the guest standing in when the guest is the nearer player (5.4), so its
  charge, lunge and knock-back reach the guest as EV_DAMAGE. The boss's camera
  on the host follows the host's real place during a stand-in
  (`iCoopStandInRealPos`), not the guest's.
- **The guest's hits are claims** (6.2) and land through the Frogfish's own
  damage mail, which takes damage only while it teeters, as for the host's
  player. A boss's hit may land anywhere on its bound: the reach check adds the
  bound's far side to the attack's reach (`iCoopBossReach`, at most 30 units).
- **The end of the fight** is the host's: its death cutscene reaches the guest
  as a show (7.3), and the guest follows the host's exit (7.2).

### 16.2 One health for both players

While a shared boss fights (alive, not dying) and the guest is in the level,
the two players have one health between them, so neither gets the easier
fight. The host keeps it (iCoopBoss.cpp):

- **Each game's changes count, as changes.** The host's own (a hit, a pickup)
  go straight into the pool. The guest's go to the host as `EV_POOL_DELTA`
  (a signed change, never 0, at most a whole health either way). Two hits at
  once both count, in whatever order they arrive.
- **The pool's value goes to the guest** as `EV_POOL` on every change and once
  a second. Both games set their player's health to it.
- **When it runs out, both are defeated** with the game's own defeat
  (`eEventKill`). Then the usual rules apply (6.4): the host loses a life and
  its level resets, and the guest comes back beside the host. Nothing is
  counted while a player's defeat plays out, and coming back from one starts
  the pool again from the host's new health. It is not a gain.
- **It starts from the host's health** when the fight starts, and ends when
  the boss is beaten (or the guest leaves the level). From then on each player
  keeps their own health, starting from the pool's last value.
- The guest's game leaves the pool by itself 3 s after the host's last word.
- A guest's change is believed as it is. A modified guest could heal the
  pair, but it could not hurt the host other than by the pool.
- Between a guest's own hit and the host's answer, the guest can see its old
  health for one round trip.

### 16.3 Limits

- Not yet played through in a test with two games; the protocol checks are in
  tssm_nettest.
- The Frogfish's phlegm and other hazards are drawn on the host only (5.10).
  They can still hit the guest (iCoopStandInNear).
- Effects that happen in the middle of a state on the host (the dust ring and
  rumble when the lunge lands) do not play on the guest.
- The level's own scripts that react to the boss's events
  (`eEventBossStageEndedC`, `eEventDeath`) run on the host only.
