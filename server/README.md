# TSSM co-op relay server

Seats two TSSM players in a room and relays their co-op datagrams, so neither player has to forward a
port. The game is host-authoritative: the host's game simulates the world and the guest's shows it. The
server therefore does no lockstep, frames, checksums or input handling. It carries opaque datagrams of at
most 1200 bytes between the two members of a room. They are authenticated end to end with the room code,
so the server can neither read nor forge them.

- Protocol: [`include/netplay/protocol.h`](../include/netplay/protocol.h), shared with the game (the source of truth)
- Transport: [ENet](http://enet.bespin.org/) over UDP, default port **7865** (`--port N` or `TSSM_SERVER_PORT`)
- Channel 0: control messages (reliable). Channel 1: RELAY / RELAY_FROM (unsequenced, unreliable)

## Protocol summary

| Client to server | Meaning |
| --- | --- |
| `HELLO(u16 protocol, str name, str buildId)` | First message. Anything else first gets `ERROR(NO_HELLO)` and a disconnect. A protocol mismatch gets `ERROR(VERSION)` naming the side to update. Answer: `WELCOME(connId)` |
| `CREATE_ROOM` | Random 4-letter code, the sender is the host, the room is pinned to its buildId. Answer: `ROOM_STATE` |
| `JOIN_ROOM(code[4])` | `ROOM_STATE` to both members, or `ERROR` (`NO_SUCH_ROOM`, `ROOM_FULL`, `BUILD`, `ALREADY_IN_ROOM`, `BAD_MESSAGE`) |
| `LEAVE_ROOM` | Guest leaves: host gets `PEER_LEFT` and `ROOM_STATE`, the room stays open. Host leaves: guest gets `ROOM_CLOSED`, the room is deleted. Not in a room: `ERROR(NOT_IN_ROOM)` |
| `RELAY(u16 toConn, bytes)` | 1..1200 bytes, only to the other member of the sender's room, as `RELAY_FROM(u16 fromConn, bytes)`. Anything else is dropped silently |

- A connection is in at most one room. Connection ids are monotonic from 1 and not reused while the server runs.
- `ROOM_STATE` (with ping, from ENet's round trip time) is also sent to every member every 2 seconds.
- Each member may send 600 RELAY packets per second (burst 600); the excess is dropped and counted.
- Disconnects behave like `LEAVE_ROOM` with reason `DISCONNECT`. On SIGINT/SIGTERM every member gets
  `ROOM_CLOSED(SERVER)` before the server exits.
- The log has one line per connect, hello, create, join, leave and disconnect, and a stats line every 60 s
  (rooms, members, relayed packets and bytes, packets dropped by the rate limit). Relay payloads are never logged.

## Build and test

Linux / WSL:

```sh
tools/server_test.sh          # builds, starts a server on a free port, runs tssm-server-test
# or by hand:
cmake -S server -B build/server -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/server
build/server/tssm-server --port 7865
build/server/tssm-server-test 127.0.0.1 7865
```

Windows (Visual Studio developer prompt, Ninja on PATH): the same `cmake` commands; the executables are
`build/server/tssm-server.exe` and `tssm-server-test.exe`.

ENet 1.3.18 is fetched at configure time (needs network access and git). `tssm-server-test` runs every
scenario (version mismatch, no HELLO, create/join, wrong code, room full, build mismatch, relay both ways,
dropped relays, guest leave, host disconnect, rate limit), prints PASS or FAIL for each and exits 0 if all pass.

## Run with Docker

Every push to the `coop-server` branch that passes the integration test publishes
`ghcr.io/<owner>/tssm-server` (tags: `latest` and the short commit hash).

```sh
docker run -d --name tssm-server --restart unless-stopped -p 7865:7865/udp \
    ghcr.io/jacob-tate/tssm-server:latest
docker logs -f tssm-server
```

To update: `docker pull`, `docker rm -f tssm-server`, then run it again.

Build the image yourself from the repository root: `docker build -f server/Dockerfile -t tssm-server .`
(the root `.dockerignore` keeps the context to `server/` and `include/netplay/`). To use another port:
`-p 9000:9000/udp -e TSSM_SERVER_PORT=9000`.

Check a running deployment with the bundled test client (about 2 seconds):

```sh
docker exec tssm-server tssm-server-test 127.0.0.1 7865
```

## Docker Swarm / Portainer

[`portainer-stack.yml`](portainer-stack.yml) runs one instance pinned to the node labelled
`partyboard=true`, with the UDP port published in host mode.

1. If the node is not labelled yet (from a manager):
   `docker node update --label-add partyboard=true <node-name>`
2. Portainer: **Stacks > Add stack**, name it `tssm`, paste the file (or point **Repository** at
   `server/portainer-stack.yml`), **Deploy the stack**.
3. Open UDP 7865 on that node (firewall and router). Players connect to that node's address, port 7865.

To update: open the stack, **Update the stack** with **Re-pull image** enabled. Keep `replicas: 1`:
rooms live in memory, so a second instance would split players between them.
