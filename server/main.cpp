// TSSM co-op relay server.
//
// Clients create or join 4-letter rooms of up to two members (host and guest). The server only seats the
// two games in a room and forwards their opaque co-op datagrams (RELAY -> RELAY_FROM) between them. There
// is no lockstep, no frames and no game logic here; the game is host-authoritative.

#include "netplay/protocol.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <enet/enet.h>

#include <algorithm>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr size_t kMaxClients = 256;
constexpr uint32_t kRoomStateIntervalMs = 2000;
constexpr uint32_t kStatsIntervalMs = 60000;
constexpr uint32_t kHelloDeadlineMs = 10000; // a connection that never says HELLO is dropped
constexpr double kRelayBurst = TSSM_NET_RELAY_RATE;

#ifdef __GNUC__
void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#endif
void logf(const char *fmt, ...) {
    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::gmtime(&now));
    std::printf("%s ", stamp);
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
    std::fflush(stdout);
}

volatile std::sig_atomic_t gStop = 0;
void onSignal(int) { gStop = 1; }

struct Room;

struct Client {
    uint16_t id = 0;
    ENetPeer *peer = nullptr;
    bool helloed = false;
    bool closing = false; // an error was sent and the connection is being closed: ignore further input
    uint32_t connectedMs = 0;
    std::string name;
    std::string buildId;
    Room *room = nullptr;
    double tokens = kRelayBurst; // RELAY token bucket
    uint32_t refillMs = 0;
};

struct Room {
    std::string code;
    std::string buildId;
    uint16_t hostId = 0;
    uint16_t guestId = TSSM_NET_CONN_NONE;
    uint32_t lastStateMs = 0;
};

struct Stats {
    uint64_t relayedPackets = 0;
    uint64_t relayedBytes = 0;
    uint64_t droppedRate = 0;
};

std::map<uint16_t, std::unique_ptr<Client>> gClients;
std::map<std::string, std::unique_ptr<Room>> gRooms;
Stats gStats;
uint16_t gNextId = 1;

std::mt19937_64 makeRng() {
    std::random_device rd;
    std::seed_seq seq{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
    return std::mt19937_64(seq);
}
std::mt19937_64 gRng = makeRng();

void sendPacket(Client &c, const TssmNetWriter &w) {
    if (!c.peer) return;
    if (w.overflow) {
        logf("BUG: outgoing message overflowed its buffer");
        return;
    }
    ENetPacket *packet = enet_packet_create(w.buf, w.len, ENET_PACKET_FLAG_RELIABLE);
    if (packet) enet_peer_send(c.peer, TSSM_NET_CHANNEL_CONTROL, packet);
}

void sendError(Client &c, TssmNetError code, const char *message) {
    uint8_t buf[256];
    TssmNetWriter w;
    tssmw_init(&w, buf, sizeof(buf));
    tssmw_u8(&w, TSSM_MSG_ERROR);
    tssmw_u8(&w, static_cast<uint8_t>(code));
    tssmw_str(&w, message);
    sendPacket(c, w);
}

// Sends an error, then closes the connection once it has been delivered.
void rejectAndClose(Client &c, TssmNetError code, const char *message) {
    sendError(c, code, message);
    c.closing = true;
    if (c.peer) enet_peer_disconnect_later(c.peer, 0);
}

Client *findClient(uint16_t id) {
    auto it = gClients.find(id);
    return it == gClients.end() ? nullptr : it->second.get();
}

uint16_t allocateId() {
    // Monotonic from 1, skipping 0 on wrap, never reusing an id that is still connected.
    for (int tries = 0; tries < 65536; tries++) {
        const uint16_t id = gNextId++;
        if (gNextId == 0) gNextId = 1;
        if (id != 0 && !gClients.count(id)) return id;
    }
    return 0;
}

void sendRoomState(Room &room) {
    room.lastStateMs = enet_time_get();
    const uint16_t ids[2] = {room.hostId, room.guestId};
    for (uint16_t to : ids) {
        Client *target = findClient(to);
        if (!target) continue;
        uint8_t buf[256];
        TssmNetWriter w;
        tssmw_init(&w, buf, sizeof(buf));
        tssmw_u8(&w, TSSM_MSG_ROOM_STATE);
        tssmw_bytes(&w, room.code.data(), TSSM_NET_ROOM_CODE_LEN);
        tssmw_u8(&w, to == room.hostId ? TSSM_ROLE_HOST : TSSM_ROLE_GUEST);
        uint8_t count = 0;
        for (uint16_t id : ids) count += findClient(id) ? 1 : 0;
        tssmw_u8(&w, count);
        for (uint16_t id : ids) {
            Client *m = findClient(id);
            if (!m) continue;
            tssmw_u16(&w, id);
            tssmw_u8(&w, id == room.hostId ? TSSM_ROLE_HOST : TSSM_ROLE_GUEST);
            tssmw_str(&w, m->name.c_str());
            tssmw_u16(&w, static_cast<uint16_t>(m->peer ? std::min<enet_uint32>(m->peer->roundTripTime, 65535) : 0));
        }
        sendPacket(*target, w);
    }
}

std::string newRoomCode() {
    const char *alphabet = TSSM_NET_CODE_ALPHABET;
    std::uniform_int_distribution<size_t> pick(0, std::strlen(alphabet) - 1);
    for (;;) {
        std::string code;
        for (int i = 0; i < TSSM_NET_ROOM_CODE_LEN; i++) code += alphabet[pick(gRng)];
        if (!gRooms.count(code)) return code;
    }
}

// Removes a client from its room (it stays connected). Guest: the host is told and the room stays open.
// Host: the guest is told the room is closed and the room is deleted.
void leaveRoom(Client &c, TssmLeaveReason reason) {
    Room *room = c.room;
    if (!room) return;
    c.room = nullptr;
    const std::string code = room->code;
    if (room->hostId == c.id) {
        if (Client *guest = findClient(room->guestId)) {
            uint8_t buf[8];
            TssmNetWriter w;
            tssmw_init(&w, buf, sizeof(buf));
            tssmw_u8(&w, TSSM_MSG_ROOM_CLOSED);
            tssmw_u8(&w, static_cast<uint8_t>(reason));
            sendPacket(*guest, w);
            guest->room = nullptr;
        }
        logf("room %s: host %u left (reason %d), room closed", code.c_str(), c.id, reason);
        gRooms.erase(code); // invalidates room
    } else {
        room->guestId = TSSM_NET_CONN_NONE;
        logf("room %s: guest %u left (reason %d)", code.c_str(), c.id, reason);
        if (Client *host = findClient(room->hostId)) {
            uint8_t buf[8];
            TssmNetWriter w;
            tssmw_init(&w, buf, sizeof(buf));
            tssmw_u8(&w, TSSM_MSG_PEER_LEFT);
            tssmw_u16(&w, c.id);
            tssmw_u8(&w, static_cast<uint8_t>(reason));
            sendPacket(*host, w);
            sendRoomState(*room);
        }
    }
}

void handleHello(Client &c, TssmNetReader &r) {
    if (c.helloed) {
        sendError(c, TSSM_ERR_BAD_MESSAGE, "already said hello");
        return;
    }
    const uint16_t version = tssmr_u16(&r);
    if (r.error) {
        rejectAndClose(c, TSSM_ERR_BAD_MESSAGE, "malformed hello");
        return;
    }
    if (version != TSSM_NET_PROTOCOL_VERSION) {
        char msg[160];
        std::snprintf(msg, sizeof(msg), "protocol mismatch: server speaks %d, client speaks %u; update the %s",
                      TSSM_NET_PROTOCOL_VERSION, static_cast<unsigned>(version),
                      version < TSSM_NET_PROTOCOL_VERSION ? "game" : "server");
        logf("client %u: hello with protocol %u rejected", c.id, static_cast<unsigned>(version));
        rejectAndClose(c, TSSM_ERR_VERSION, msg);
        return;
    }
    char name[TSSM_NET_MAX_NAME];
    char build[TSSM_NET_MAX_BUILD_ID];
    tssmr_str(&r, name, sizeof(name));
    tssmr_str(&r, build, sizeof(build));
    if (r.error || name[0] == 0) {
        rejectAndClose(c, TSSM_ERR_BAD_MESSAGE, "malformed hello");
        return;
    }
    c.name = name;
    c.buildId = build;
    c.helloed = true;
    logf("client %u: hello name=\"%s\" build=\"%s\"", c.id, c.name.c_str(), c.buildId.c_str());
    uint8_t buf[8];
    TssmNetWriter w;
    tssmw_init(&w, buf, sizeof(buf));
    tssmw_u8(&w, TSSM_MSG_WELCOME);
    tssmw_u16(&w, c.id);
    sendPacket(c, w);
}

void handleCreate(Client &c) {
    if (c.room) {
        sendError(c, TSSM_ERR_ALREADY_IN_ROOM, "already in a room");
        return;
    }
    auto room = std::make_unique<Room>();
    room->code = newRoomCode();
    room->buildId = c.buildId;
    room->hostId = c.id;
    Room *raw = room.get();
    gRooms[room->code] = std::move(room);
    c.room = raw;
    logf("client %u: created room %s", c.id, raw->code.c_str());
    sendRoomState(*raw);
}

void handleJoin(Client &c, TssmNetReader &r) {
    char code[TSSM_NET_ROOM_CODE_LEN];
    tssmr_bytes(&r, code, sizeof(code));
    if (r.error || !tssm_code_valid(code)) {
        sendError(c, TSSM_ERR_BAD_MESSAGE, "malformed room code");
        return;
    }
    if (c.room) {
        sendError(c, TSSM_ERR_ALREADY_IN_ROOM, "already in a room");
        return;
    }
    const std::string key(code, TSSM_NET_ROOM_CODE_LEN);
    auto it = gRooms.find(key);
    if (it == gRooms.end()) {
        logf("client %u: join %s failed (no such room)", c.id, key.c_str());
        sendError(c, TSSM_ERR_NO_SUCH_ROOM, "no such room");
        return;
    }
    Room &room = *it->second;
    if (room.guestId != TSSM_NET_CONN_NONE) {
        logf("client %u: join %s failed (full)", c.id, key.c_str());
        sendError(c, TSSM_ERR_ROOM_FULL, "room is full");
        return;
    }
    if (room.buildId != c.buildId) {
        logf("client %u: join %s failed (build mismatch)", c.id, key.c_str());
        sendError(c, TSSM_ERR_BUILD, "build mismatch: both players need the same game build as the host");
        return;
    }
    room.guestId = c.id;
    c.room = &room;
    logf("client %u: joined room %s (host %u)", c.id, key.c_str(), room.hostId);
    sendRoomState(room);
}

void handleRelay(Client &c, TssmNetReader &r) {
    const uint16_t to = tssmr_u16(&r);
    const size_t len = tssmr_left(&r);
    if (r.error || !c.room || len < 1 || len > TSSM_NET_MAX_RELAY) return;
    Room &room = *c.room;
    const uint16_t other = room.hostId == c.id ? room.guestId : room.hostId;
    if (other == TSSM_NET_CONN_NONE || to != other) return;
    Client *dest = findClient(other);
    if (!dest || !dest->peer) return;

    // Token bucket: TSSM_NET_RELAY_RATE per second, burst of the same size.
    const uint32_t now = enet_time_get();
    c.tokens = std::min(kRelayBurst, c.tokens + (now - c.refillMs) * (TSSM_NET_RELAY_RATE / 1000.0));
    c.refillMs = now;
    if (c.tokens < 1.0) {
        gStats.droppedRate++;
        return;
    }
    c.tokens -= 1.0;

    uint8_t buf[3 + TSSM_NET_MAX_RELAY];
    TssmNetWriter w;
    tssmw_init(&w, buf, sizeof(buf));
    tssmw_u8(&w, TSSM_MSG_RELAY_FROM);
    tssmw_u16(&w, c.id);
    tssmw_bytes(&w, r.buf + r.pos, len);
    ENetPacket *packet = enet_packet_create(w.buf, w.len, ENET_PACKET_FLAG_UNSEQUENCED);
    if (!packet) return;
    if (enet_peer_send(dest->peer, TSSM_NET_CHANNEL_RELAY, packet) == 0) {
        gStats.relayedPackets++;
        gStats.relayedBytes += len;
    } else {
        enet_packet_destroy(packet);
    }
}

void onReceive(Client &c, const ENetPacket *packet) {
    if (c.closing) return;
    TssmNetReader r;
    tssmr_init(&r, packet->data, packet->dataLength);
    const uint8_t type = tssmr_u8(&r);
    if (r.error) {
        if (c.helloed) sendError(c, TSSM_ERR_BAD_MESSAGE, "empty message");
        else rejectAndClose(c, TSSM_ERR_NO_HELLO, "expected hello");
        return;
    }
    if (!c.helloed) {
        if (type != TSSM_MSG_HELLO) {
            logf("client %u: message %u before hello, rejected", c.id, static_cast<unsigned>(type));
            rejectAndClose(c, TSSM_ERR_NO_HELLO, "expected hello");
            return;
        }
        handleHello(c, r);
        return;
    }
    switch (type) {
    case TSSM_MSG_HELLO:
        handleHello(c, r);
        break;
    case TSSM_MSG_CREATE_ROOM:
        handleCreate(c);
        break;
    case TSSM_MSG_JOIN_ROOM:
        handleJoin(c, r);
        break;
    case TSSM_MSG_LEAVE_ROOM:
        if (!c.room) sendError(c, TSSM_ERR_NOT_IN_ROOM, "not in a room");
        else leaveRoom(c, TSSM_LEAVE_LEFT);
        break;
    case TSSM_MSG_RELAY:
        handleRelay(c, r);
        break;
    default:
        sendError(c, TSSM_ERR_BAD_MESSAGE, "unknown message");
        break;
    }
}

void dropClient(Client &c) {
    logf("client %u: disconnected%s%s", c.id, c.room ? " from room " : "", c.room ? c.room->code.c_str() : "");
    leaveRoom(c, TSSM_LEAVE_DISCONNECT);
    if (c.peer) c.peer->data = nullptr;
    gClients.erase(c.id); // invalidates c
}

void logStats() {
    size_t members = 0;
    for (auto &entry : gRooms) members += entry.second->guestId != TSSM_NET_CONN_NONE ? 2 : 1;
    logf("stats: rooms=%zu members=%zu clients=%zu relayed=%llu packets %llu bytes dropped_by_rate=%llu", gRooms.size(),
         members, gClients.size(), static_cast<unsigned long long>(gStats.relayedPackets),
         static_cast<unsigned long long>(gStats.relayedBytes), static_cast<unsigned long long>(gStats.droppedRate));
}

void tick() {
    static uint32_t lastStats = enet_time_get();
    const uint32_t now = enet_time_get();
    for (auto &entry : gRooms) {
        if (now - entry.second->lastStateMs >= kRoomStateIntervalMs) sendRoomState(*entry.second);
    }
    for (auto &entry : gClients) {
        Client &c = *entry.second;
        if (!c.helloed && !c.closing && c.peer && now - c.connectedMs >= kHelloDeadlineMs) {
            logf("client %u: no hello in time", c.id);
            rejectAndClose(c, TSSM_ERR_NO_HELLO, "expected hello");
        }
    }
    if (now - lastStats >= kStatsIntervalMs) {
        lastStats = now;
        logStats();
    }
}

void shutdownAll(ENetHost *host) {
    logf("shutting down");
    for (auto &entry : gClients) {
        Client &c = *entry.second;
        if (c.room && c.peer) {
            uint8_t buf[8];
            TssmNetWriter w;
            tssmw_init(&w, buf, sizeof(buf));
            tssmw_u8(&w, TSSM_MSG_ROOM_CLOSED);
            tssmw_u8(&w, TSSM_LEAVE_SERVER);
            sendPacket(c, w);
        }
        if (c.peer) enet_peer_disconnect_later(c.peer, 0);
    }
    enet_host_flush(host);
    ENetEvent event;
    for (int i = 0; i < 20 && !gClients.empty(); i++) {
        while (enet_host_service(host, &event, 50) > 0) {
            if (event.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(event.packet);
            if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                if (auto *c = static_cast<Client *>(event.peer->data)) {
                    event.peer->data = nullptr;
                    gClients.erase(c->id);
                }
            }
        }
    }
    logStats();
}

} // namespace

int main(int argc, char **argv) {
    uint16_t port = TSSM_NET_DEFAULT_PORT;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else {
            std::fprintf(stderr, "usage: %s [--port N]   (default %d)\n", argv[0], TSSM_NET_DEFAULT_PORT);
            return 2;
        }
    }
    // Like smb2-server, the environment variable is applied last.
    if (const char *env = std::getenv("TSSM_SERVER_PORT")) {
        if (*env) port = static_cast<uint16_t>(std::atoi(env));
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    if (enet_initialize() != 0) {
        std::fprintf(stderr, "enet_initialize failed\n");
        return 1;
    }
    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    ENetHost *host = enet_host_create(&address, kMaxClients, TSSM_NET_CHANNEL_COUNT, 0, 0);
    if (!host) {
        std::fprintf(stderr, "could not listen on UDP port %u\n", port);
        return 1;
    }
    logf("TSSM co-op relay server, protocol %d, listening on UDP %u", TSSM_NET_PROTOCOL_VERSION, port);

    while (!gStop) {
        tick();
        ENetEvent event;
        while (!gStop && enet_host_service(host, &event, 50) > 0) {
            switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                const uint16_t id = allocateId();
                if (id == 0) {
                    enet_peer_disconnect_now(event.peer, 0);
                    break;
                }
                auto client = std::make_unique<Client>();
                client->id = id;
                client->peer = event.peer;
                client->connectedMs = enet_time_get();
                client->refillMs = client->connectedMs;
                event.peer->data = client.get();
                enet_peer_timeout(event.peer, 0, 5000, 15000);
                char ip[64];
                enet_address_get_host_ip(&event.peer->address, ip, sizeof(ip));
                logf("client %u: connected from %s", id, ip);
                gClients[id] = std::move(client);
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE:
                if (auto *c = static_cast<Client *>(event.peer->data)) onReceive(*c, event.packet);
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                if (auto *c = static_cast<Client *>(event.peer->data)) dropClient(*c);
                event.peer->data = nullptr;
                break;
            default:
                break;
            }
        }
    }
    shutdownAll(host);
    enet_host_destroy(host);
    enet_deinitialize();
    return 0;
}
