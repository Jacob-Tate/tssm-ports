# Co-op server, boss fights and the friend build: handoff

(Named this way because the repo's `.gitignore` ignores `*HANDOFF*` files.)

This is the handoff for the `coop-server` branch of the fork
(`github.com/Jacob-Tate/tssm-ports`). It is written for whoever makes the next
version. Read it before changing the co-op code. `docs/MULTIPLAYER.md` is the
full design: sections 1 to 14 are upstream's (`aidp2/tssm-ports`, branch
`coop`), and sections 15 and 16 are this branch's.

## 1. What this branch adds to upstream `coop`

| Area | What | Where |
|---|---|---|
| Relay server | Dedicated ENet relay with 4-letter rooms and two seats. Modelled on the user's partyboard and smb2-pc servers, but it carries the existing host-authoritative co-op datagrams instead of lockstep input. | `server/`, `include/netplay/protocol.h`, MULTIPLAYER.md 15 |
| Game side of the relay | `RelayLink` stands in for the co-op UDP socket. Each room member is a virtual address, so `iNetSession` (the handshake, the join code, authentication, the reliable layer) runs unchanged end to end. | `src/SB/Core/pc/iNetRelay.{h,cpp}`, `iCoop.cpp` (`IoStartRelay`, `IoRelayTick`, `IoSockSend`/`IoSockClose`) |
| UI | Co-op tab, section "Co-op server (no port forwarding)": the server, the room code and the join code. `[coop] server` (default `10.0.0.5:7865`). | `gui/iGuiCoop.cpp`, `iConfigTable.cpp` |
| Boss fights | The Frogfish (B101) and its tongue are shared enemies. `EV_BOSS` carries the fight state, and the guest's copy shows it (meter, sounds, its own boss camera, the control lock). | `iCoopBoss.cpp`, `zNMETypeBossFrogfish.{h,cpp}` (`CoopGuestShow`, `FFCamPlayerPos`), MULTIPLAYER.md 16 |
| Shared health | During a boss fight both players have one health: `EV_POOL` (host to guest) and `EV_POOL_DELTA` (guest to host). A level reset restarts the pool, and either player's defeat defeats both. | `iCoopBoss.cpp` (`HostPool`, `GuestPool`, `iCoopBossPoolEvent`, `iCoopBossSceneReset`), MULTIPLAYER.md 16.2 |
| Friend build | One self-extracting `TSSM-Coop.exe`, as in partyboard and smb2-pc. | `tools/friends/`, `tools/package_friends.py`, section 5 here |
| Tests | `tssm_relaytest` (the session through a live server), `server/test_client.cpp` (the server's scenarios), `tssm_nettest` (protocol 9 additions) | see section 4 |

The co-op protocol is at **version 9** (`kProtoVersion`, `iNetProto.h`). The
relay protocol is separate, at `TSSM_NET_PROTOCOL_VERSION 1`. Both players
need the same build: the co-op version (`coop_id.h`) is a fingerprint of the
code.

## 2. How the pieces fit

```
game A (host)                     tssm-server (relay)                game B (guest)
iCoop I/O thread                  ENet, rooms ABCD                   iCoop I/O thread
  Host session (iNetSession)        RELAY / RELAY_FROM                 Client session
  RelayLink  ----- ENet ch1 (unsequenced) -----> ----- ENet ch1 -----> RelayLink
             ----- ENet ch0 (control: HELLO, CREATE/JOIN, ROOM_STATE) ---
```

- The server only relays opaque datagrams of at most 1200 bytes, at most 600
  per second per member, between the two members of a room. It cannot read or
  forge the co-op traffic, because every session packet is HMAC'd with the
  join code.
- **The room code** routes to the host. **The join code** is the session
  password (unchanged from direct play). The guest types both.
- Virtual member addresses are `100::/64` plus the connection id
  (`RelayLink::addr_of`). They are never real and never loopback.
- Boss sync rides on the existing shared-enemy machinery:
  - The table (`EnemyType`, `iCoopWorld.cpp`).
  - Snapshots pose the guest's copy (`iCoopWorldGuestPose`, then
    `iCoopBossGuestShow`).
  - The stand-in lets the host's boss target the guest
    (`iCoopStandInBegin`, `iCoopStandInRealPos` for the host's camera).
  - The guest's hits are claims (`HostHit`). For bosses the reach is extended
    by `iCoopBossReach`.

## 3. Building

- Game: `build-debug.bat [D3D11]` or `build-release.bat [D3D11]` at the root.
  They put `tssm.exe` and the tests in `bin\` (`bin\BUILD-INFO.txt` says which
  configuration). It needs VS 2022 or later (VS 18 is installed here) and the
  submodules (`git submodule update --init --depth 1`).
  - **FFmpeg** (the movies) comes from vcpkg at `%USERPROFILE%\vcpkg\installed\x64-windows`
    (`TSSM_VCPKG` overrides). It is not installed on the dev PC yet, so builds
    there skip the movies (the logos at boot and a few story clips).
    `vcpkg install ffmpeg[avcodec,avformat,swscale,swresample]:x64-windows`
    fixes it, and the friend build then picks up the DLLs by itself.
  - ENet comes from FetchContent (`lsalzman/enet` v1.3.18). It is downloaded
    only, and built from its sources in the root `CMakeLists.txt`, because its
    own CMakeLists predates CMake 3.5 and CMake 4 refuses it.
- Server: `cmake -S server -B build/server -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/server`
  from a VS developer prompt.
  - The server's CMake copies `protocol.h` into the build dir instead of
    adding `include/` to the include path. The repo's `include/` (the
    decompilation's) has `intrin.h` and `std/`, which shadow the compiler's
    own headers.
  - Docker: `server/Dockerfile`. CI is `.github/workflows/server.yml`, which
    builds, tests in the container and pushes
    `ghcr.io/jacob-tate/tssm-server:{latest,<sha>}` on pushes to
    `coop-server` that touch `server/**` or `include/netplay/**`. Deploy with
    `server/portainer-stack.yml` (UDP 7865, host mode, replicas 1: rooms live
    in memory). The ghcr package is private by default, so make it public or
    give Portainer credentials.

## 4. Testing

| What | How |
|---|---|
| Protocol, crypto, session | `bin\tssm_nettest.exe`: 767 checks, about 8 s |
| Session through the relay | Start `build/server/tssm-server.exe --port 38811`, then `bin\tssm_relaytest.exe 127.0.0.1:38811`: 10 checks |
| Server scenarios | `build/server/tssm-server-test.exe 127.0.0.1 <port>` against a running server: 12 scenarios, also run in CI |
| Frogfish in two games | `bin\Frogfish co-op test.bat` (not in git: `bin/` is ignored). It sets `TSSM_ASSETS` to `..\..\tssm-xbox` (the extracted Xbox disc) and `TSSM_MOD` to `..\..\mod_b101` (the game's `sb04.ini` with `BOOT = B101`, `ShowMenuOnBoot = 0`), sets `TSSM_COOP_LOG=60`, and starts `tssm.exe --local-coop`. Player 1's log is `%APPDATA%\tssm\last_run.log`. Player 2's is `%APPDATA%\tssm\mods\mod_b101\local2\last_run.log`, without co-op logging, because `TSSM_COOP*` and `TSSM_TEST_*` are not passed to Player 2 (`kDropPrefixes`, `iCoopLocalCore.cpp`). |
| Any level directly | The same boot-mod trick with another `BOOT =` scene, or `TSSM_TEST_SCENE=<frame>:<SCEN>` (see `src/SB/Core/pc/README.md`, "Unattended runs") |
| Unattended screenshot | `TSSM_CAPTURE_FRAMES=1400 TSSM_CAPTURE_DIR=... TSSM_EXIT_AFTER=1410 tssm.exe` |

When the protocol version changes, the known answers in `nettest.cpp`
`test_identity` (the build IDs and version code for "proto N") change too.
The failing check prints the new value. Update them deliberately.

## 5. The friend build

`TSSM-Coop.exe` is the launcher (`tools/friends/launcher.cpp`) with the game
appended to it, LZMS-compressed. On a friend's PC:

1. It unpacks to `%LOCALAPPDATA%\TSSM-Coop\builds\<build id>\` (and removes
   older builds).
2. It runs `tssm.exe` there with the settings in `%APPDATA%\TSSM-Coop\config.ini`
   (`TSSM_CONFIG`), so they survive updates. The co-op server is seeded on the
   first run. Saves stay in the game's own `%APPDATA%\tssm\saves`.
3. Shift-start, or `--uninstall`, removes the two TSSM-Coop folders.
   `--unpack-only` is for tests.

What the packager ships: `tssm.exe`, every `bin\*.dll` (FFmpeg's, when the
build has them), the VC++ runtime DLLs those actually import (found through
vswhere, the newest toolset: `msvcp140`, `vcruntime140`, `vcruntime140_1`
today), and `friend.ini` (the server). It refuses a build whose
`bin\BUILD-INFO.txt` is not Release, or one that imports a debug runtime.
The build id is the first 16 hex characters of the archive's SHA-256.

Checked on the dev PC (first version, 3.6 MB): the unpacked files are
byte-identical to the packager's selection, and a second start reuses the
folder and removes older builds. A real start ran the game and seeded
`%APPDATA%\TSSM-Coop\config.ini` with `server`, which the game completed with
its defaults (`appendMissingSettings`, iConfig.cpp). That file then has a
second `[coop]` header lower down, which the game reads fine. Not yet tried
on a PC without Visual Studio.

To make a new version:

1. `build-release.bat` (all three renderers; the first friend build was
   `D3D11` only). `package_friends.py` refuses a Debug build,
   whose debug C runtime does not exist on other PCs.
2. `python -I tools/package_friends.py --server <address friends can reach>`.
   This builds the launcher on first use. The output is
   `build/package/friends/TSSM-Coop.exe` and `README.txt`.
3. Send both. Everyone must use the same exe: the co-op version check refuses
   a different build, and so does the server's build check.

`--server` defaults to `TSSM_FRIEND_SERVER`, then `10.0.0.5:7865` (the home
LAN server). For friends outside the LAN, give the server's public address
and forward UDP 7865 to it.

## 6. Known limits and next steps

- **Bosses not shared yet:** Dennis (B201, B301) and King Neptune (B401).
  Dennis 1 and 2 are near-duplicates (`zNMETypeBossDennis.cpp`,
  `zNMETypeBossDenToo.cpp`), so one piece of work covers both. Their fight
  state is a psyche goal plus `stageCurr` plus `pts_health`. Watch their stage
  events (`eEventBossStage*`): the guest's copy must not send them. Neptune is
  the hardest (about 6,000 lines, a scope camera, reflectors, minions, and the
  cruise-bubble damage path). Add a `BOSS_*` kind, a `Read` case and a guest
  show method for each, and add their NPC types to `EnemyType`.
- **The Frogfish has only been played once with two copies on one PC.**
  Its hazards (phlegm) are drawn on the host only. Mid-state effects (the dust
  ring and rumble on the lunge) don't play on the guest. Level scripts that
  react to the boss's events run on the host only.
- **Shared health:** the guest's changes are trusted as sent (a modified
  guest could heal the pair). The guest can see its old health for one round
  trip after a hit.
- **Relay:** a single server instance with rooms in memory. There are no LAN
  answers for a relay host. The UI's room-code field accepts O and I, but
  codes never use them, so the server refuses such a code as "no such room".
- **The upstream branch moves on** (`aidp2/tssm-ports`, `coop`). To take new
  upstream work, merge `origin/coop` into `coop-server`. The conflicts will be
  in `iCoop.cpp` (the I/O thread), `iNetProto.*` (the event list, the
  protocol version) and `MULTIPLAYER.md`.

## 7. Things that cost time (so they don't again)

- **Running batch files from Git Bash:** `cmd.exe //c "build-debug.bat"`
  runs in the wrong folder, and inline quoting breaks. Write a small `.bat`
  with CRLF line endings that `call`s the full path, and run
  `cmd.exe //c "C:\full\path.bat"`. Bash `printf` eats `\U` in Windows paths.
- **Patching C files from a Python heredoc:** `"\n"` in the Python string
  becomes a real newline inside C string literals. Use `\\n`, or a script
  file, or the Edit tool.
- **Debug vs Release CRT:** Debug `tssm.exe` imports `MSVCP140D` and
  `ucrtbased`, which only exist where Visual Studio is installed.
- **Bosses and the stand-in:** anything in a boss's update that reads
  `xglobals->___player_ent_dont_use_directly` for this game's own view (the
  camera) must use the real position during a stand-in
  (`iCoopStandInRealPos`).
- **Falls lose a chance without the health reaching 0.** The level reset is
  the signal (`iCoopWorldSceneReset`, from `zScene.cpp`).
