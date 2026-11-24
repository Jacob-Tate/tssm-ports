// Integration test for the relay server: simulated players over real UDP.
//   tssm-server-test [host] [port]
// Exits 0 if every scenario passes.

#include "netplay/protocol.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <enet/enet.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace {

int gFailures = 0;
int gChecks = 0;

#define CHECK(cond, ...)                                                                                              \
    do {                                                                                                              \
        gChecks++;                                                                                                    \
        if (!(cond)) {                                                                                                \
            std::printf("  FAIL line %d: ", __LINE__);                                                                \
            std::printf(__VA_ARGS__);                                                                                 \
            std::printf("\n");                                                                                        \
            gFailures++;                                                                                              \
        }                                                                                                             \
    } while (0)

typedef std::vector<uint8_t> Msg;

const char *kBuild = "test-build";

struct RoomInfo {
    bool valid = false;
    char code[TSSM_NET_ROOM_CODE_LEN + 1] = {0};
    int yourRole = -1;
    int count = 0;
    uint16_t ids[2] = {0, 0};
    int roles[2] = {-1, -1};
    std::string names[2];
};

struct TestClient {
    ENetHost *host = nullptr;
    ENetPeer *peer = nullptr;
    std::deque<Msg> inbox;  // control messages (channel 0)
    std::deque<Msg> relays; // RELAY_FROM (any channel)
    bool disconnected = false;
    uint16_t id = 0;

    ~TestClient() {
        if (host) enet_host_destroy(host);
    }

    bool connect(const char *hostname, uint16_t port) {
        host = enet_host_create(nullptr, 1, TSSM_NET_CHANNEL_COUNT, 0, 0);
        ENetAddress address{};
        enet_address_set_host(&address, hostname);
        address.port = port;
        peer = enet_host_connect(host, &address, TSSM_NET_CHANNEL_COUNT, 0);
        ENetEvent event;
        return enet_host_service(host, &event, 3000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT;
    }
    void sendRaw(const uint8_t *data, size_t len, uint8_t channel = TSSM_NET_CHANNEL_CONTROL,
                 uint32_t flags = ENET_PACKET_FLAG_RELIABLE) {
        enet_peer_send(peer, channel, enet_packet_create(data, len, flags));
    }
    void send(const TssmNetWriter &w) {
        sendRaw(w.buf, w.len);
        enet_host_flush(host);
    }
    void pump(int ms) {
        ENetEvent event;
        while (enet_host_service(host, &event, ms) > 0) {
            if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                Msg m(event.packet->data, event.packet->data + event.packet->dataLength);
                if (!m.empty() && m[0] == TSSM_MSG_RELAY_FROM) relays.push_back(std::move(m));
                else inbox.push_back(std::move(m));
                enet_packet_destroy(event.packet);
            } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                disconnected = true;
            }
            ms = 0;
        }
    }
    // Waits for the next message of the given type, discarding other control messages. Empty on timeout.
    Msg waitFor(uint8_t type, int timeoutMs = 2000) {
        std::deque<Msg> &queue = type == TSSM_MSG_RELAY_FROM ? relays : inbox;
        for (int waited = 0; waited <= timeoutMs; waited += 10) {
            while (!queue.empty()) {
                Msg msg = std::move(queue.front());
                queue.pop_front();
                if (!msg.empty() && msg[0] == type) return msg;
            }
            pump(10);
        }
        return {};
    }
    bool sees(uint8_t type, int ms) { return !waitFor(type, ms).empty(); }
    bool waitDisconnect(int ms) {
        for (int waited = 0; waited <= ms && !disconnected; waited += 10) pump(10);
        return disconnected;
    }
    void disconnect() {
        if (!peer || disconnected) return;
        enet_peer_disconnect(peer, 0);
        ENetEvent event;
        for (int i = 0; i < 20 && enet_host_service(host, &event, 50) > 0; i++) {
            if (event.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(event.packet);
            if (event.type == ENET_EVENT_TYPE_DISCONNECT) break;
        }
        disconnected = true;
    }
};

void sendHello(TestClient &c, const char *name, uint16_t version, const char *build) {
    uint8_t buf[128];
    TssmNetWriter w;
    tssmw_init(&w, buf, sizeof(buf));
    tssmw_u8(&w, TSSM_MSG_HELLO);
    tssmw_u16(&w, version);
    tssmw_str(&w, name);
    tssmw_str(&w, build);
    c.send(w);
}

bool login(TestClient &c, const char *host, uint16_t port, const char *name, const char *build = kBuild) {
    if (!c.connect(host, port)) return false;
    sendHello(c, name, TSSM_NET_PROTOCOL_VERSION, build);
    Msg m = c.waitFor(TSSM_MSG_WELCOME);
    if (m.size() != 3) return false;
    TssmNetReader r;
    tssmr_init(&r, m.data() + 1, m.size() - 1);
    c.id = tssmr_u16(&r);
    return c.id != 0;
}

void sendSimple(TestClient &c, uint8_t type) {
    uint8_t buf[1] = {type};
    c.sendRaw(buf, 1);
    enet_host_flush(c.host);
}

void sendJoin(TestClient &c, const char *code) {
    uint8_t buf[8];
    TssmNetWriter w;
    tssmw_init(&w, buf, sizeof(buf));
    tssmw_u8(&w, TSSM_MSG_JOIN_ROOM);
    tssmw_bytes(&w, code, TSSM_NET_ROOM_CODE_LEN);
    c.send(w);
}

void sendRelay(TestClient &c, uint16_t to, const uint8_t *data, size_t len,
               uint8_t channel = TSSM_NET_CHANNEL_RELAY, bool flush = true) {
    std::vector<uint8_t> buf(3 + len);
    TssmNetWriter w;
    tssmw_init(&w, buf.data(), buf.size());
    tssmw_u8(&w, TSSM_MSG_RELAY);
    tssmw_u16(&w, to);
    tssmw_bytes(&w, data, len);
    c.sendRaw(w.buf, w.len, channel, ENET_PACKET_FLAG_UNSEQUENCED);
    if (flush) enet_host_flush(c.host);
}

// Returns the error code of an ERROR message (0 if none arrived).
int waitError(TestClient &c, int ms = 2000) {
    Msg m = c.waitFor(TSSM_MSG_ERROR, ms);
    return m.size() >= 2 ? m[1] : 0;
}

RoomInfo parseRoomState(const Msg &m) {
    RoomInfo info;
    if (m.size() < 1 + TSSM_NET_ROOM_CODE_LEN + 2 || m[0] != TSSM_MSG_ROOM_STATE) return info;
    TssmNetReader r;
    tssmr_init(&r, m.data() + 1, m.size() - 1);
    tssmr_bytes(&r, info.code, TSSM_NET_ROOM_CODE_LEN);
    info.yourRole = tssmr_u8(&r);
    info.count = tssmr_u8(&r);
    for (int i = 0; i < info.count && i < 2; i++) {
        info.ids[i] = tssmr_u16(&r);
        info.roles[i] = tssmr_u8(&r);
        char name[TSSM_NET_MAX_NAME];
        tssmr_str(&r, name, sizeof(name));
        info.names[i] = name;
        tssmr_u16(&r); // ping
    }
    info.valid = !r.error;
    return info;
}

RoomInfo waitRoomState(TestClient &c, int ms = 2000) { return parseRoomState(c.waitFor(TSSM_MSG_ROOM_STATE, ms)); }

// ---- scenarios ----

const char *gHost = "127.0.0.1";
uint16_t gPort = 7865;

void versionMismatch() {
    TestClient c;
    CHECK(c.connect(gHost, gPort), "connect");
    sendHello(c, "Old", TSSM_NET_PROTOCOL_VERSION + 1, kBuild);
    Msg m = c.waitFor(TSSM_MSG_ERROR);
    CHECK(m.size() >= 3 && m[1] == TSSM_ERR_VERSION, "expected ERROR(VERSION)");
    if (m.size() >= 3) {
        const std::string text(m.begin() + 3, m.end());
        std::printf("    server says: %s\n", text.c_str());
        CHECK(text.find("update") != std::string::npos, "message should say what to update");
    }
    CHECK(c.waitDisconnect(3000), "server should close the connection");
}

void noHello() {
    TestClient c;
    CHECK(c.connect(gHost, gPort), "connect");
    sendSimple(c, TSSM_MSG_CREATE_ROOM);
    CHECK(waitError(c) == TSSM_ERR_NO_HELLO, "expected ERROR(NO_HELLO)");
    CHECK(c.waitDisconnect(3000), "server should close the connection");
}

void badHello() {
    TestClient c;
    CHECK(c.connect(gHost, gPort), "connect");
    uint8_t buf[64];
    TssmNetWriter w;
    tssmw_init(&w, buf, sizeof(buf));
    tssmw_u8(&w, TSSM_MSG_HELLO);
    tssmw_u16(&w, TSSM_NET_PROTOCOL_VERSION);
    tssmw_str(&w, ""); // empty name
    tssmw_str(&w, kBuild);
    c.send(w);
    CHECK(waitError(c) == TSSM_ERR_BAD_MESSAGE, "expected ERROR(BAD_MESSAGE) for an empty name");
}

void createAndJoin() {
    TestClient host, guest;
    CHECK(login(host, gHost, gPort, "Alice"), "host login");
    CHECK(login(guest, gHost, gPort, "Bob"), "guest login");
    CHECK(host.id != guest.id, "distinct conn ids");
    sendSimple(host, TSSM_MSG_CREATE_ROOM);
    RoomInfo h = waitRoomState(host);
    CHECK(h.valid && h.yourRole == TSSM_ROLE_HOST && h.count == 1 && h.ids[0] == host.id, "host sees itself as host");
    CHECK(h.valid && tssm_code_valid(h.code) != 0, "room code is valid: %s", h.code);
    sendJoin(guest, h.code);
    RoomInfo g = waitRoomState(guest);
    CHECK(g.valid && std::strcmp(g.code, h.code) == 0, "guest sees the same code");
    CHECK(g.valid && g.yourRole == TSSM_ROLE_GUEST && g.count == 2, "guest role and count");
    CHECK(g.valid && g.ids[0] == host.id && g.roles[0] == TSSM_ROLE_HOST && g.names[0] == "Alice", "host entry");
    CHECK(g.valid && g.ids[1] == guest.id && g.roles[1] == TSSM_ROLE_GUEST && g.names[1] == "Bob", "guest entry");
    RoomInfo h2 = waitRoomState(host);
    CHECK(h2.valid && h2.yourRole == TSSM_ROLE_HOST && h2.count == 2, "host gets updated state");
    // Already in a room: create and join again.
    sendSimple(host, TSSM_MSG_CREATE_ROOM);
    CHECK(waitError(host) == TSSM_ERR_ALREADY_IN_ROOM, "create while in room");
    sendJoin(guest, h.code);
    CHECK(waitError(guest) == TSSM_ERR_ALREADY_IN_ROOM, "join while in room");
    // Lowercase codes are accepted.
    TestClient c;
    CHECK(login(c, gHost, gPort, "Carol"), "login");
    char lower[5];
    for (int i = 0; i < 4; i++) lower[i] = static_cast<char>(h.code[i] >= 'A' && h.code[i] <= 'Z' ? h.code[i] + 32 : h.code[i]);
    lower[4] = 0;
    sendJoin(c, lower);
    CHECK(waitError(c) == TSSM_ERR_ROOM_FULL, "lowercase code resolves to the room (full)");
}

void wrongCode() {
    TestClient c;
    CHECK(login(c, gHost, gPort, "Dave"), "login");
    sendJoin(c, "AAAA");
    CHECK(waitError(c) == TSSM_ERR_NO_SUCH_ROOM, "expected NO_SUCH_ROOM");
    sendJoin(c, "OIL0"); // letters outside the alphabet
    CHECK(waitError(c) == TSSM_ERR_BAD_MESSAGE, "expected BAD_MESSAGE for an invalid code");
    sendSimple(c, TSSM_MSG_LEAVE_ROOM);
    CHECK(waitError(c) == TSSM_ERR_NOT_IN_ROOM, "leave outside a room");
}

void roomFullAndBuild() {
    TestClient host, guest, third, other;
    CHECK(login(host, gHost, gPort, "H"), "host");
    CHECK(login(guest, gHost, gPort, "G"), "guest");
    CHECK(login(third, gHost, gPort, "T"), "third");
    CHECK(login(other, gHost, gPort, "O", "other-build"), "other build");
    sendSimple(host, TSSM_MSG_CREATE_ROOM);
    RoomInfo h = waitRoomState(host);
    sendJoin(other, h.code);
    CHECK(waitError(other) == TSSM_ERR_BUILD, "expected BUILD");
    sendJoin(guest, h.code);
    CHECK(waitRoomState(guest).valid, "guest joined");
    sendJoin(third, h.code);
    CHECK(waitError(third) == TSSM_ERR_ROOM_FULL, "expected ROOM_FULL");
}

struct Pair {
    TestClient host, guest;
    char code[5] = {0};
    bool open(const char *prefix) {
        if (!login(host, gHost, gPort, (std::string(prefix) + "H").c_str())) return false;
        if (!login(guest, gHost, gPort, (std::string(prefix) + "G").c_str())) return false;
        sendSimple(host, TSSM_MSG_CREATE_ROOM);
        RoomInfo h = waitRoomState(host);
        if (!h.valid) return false;
        std::memcpy(code, h.code, 5);
        sendJoin(guest, code);
        if (!waitRoomState(guest).valid) return false;
        waitRoomState(host);
        return true;
    }
};

void relayBothWays() {
    Pair p;
    CHECK(p.open("R"), "room setup");
    uint8_t data[300];
    for (size_t i = 0; i < sizeof(data); i++) data[i] = static_cast<uint8_t>(i * 7 + 3);
    sendRelay(p.host, p.guest.id, data, sizeof(data));
    Msg m = p.guest.waitFor(TSSM_MSG_RELAY_FROM);
    CHECK(m.size() == 3 + sizeof(data) && std::memcmp(m.data() + 3, data, sizeof(data)) == 0, "host->guest bytes exact");
    CHECK(m.size() >= 3 && (m[1] | (m[2] << 8)) == p.host.id, "fromConn is host id");
    uint8_t data2[5] = {9, 8, 7, 6, 5};
    sendRelay(p.guest, p.host.id, data2, sizeof(data2), TSSM_NET_CHANNEL_CONTROL); // either channel is accepted
    m = p.host.waitFor(TSSM_MSG_RELAY_FROM);
    CHECK(m.size() == 3 + sizeof(data2) && std::memcmp(m.data() + 3, data2, sizeof(data2)) == 0, "guest->host bytes exact");
    CHECK(m.size() >= 3 && (m[1] | (m[2] << 8)) == p.guest.id, "fromConn is guest id");
    // Maximum size.
    std::vector<uint8_t> big(TSSM_NET_MAX_RELAY, 0xAB);
    sendRelay(p.host, p.guest.id, big.data(), big.size());
    m = p.guest.waitFor(TSSM_MSG_RELAY_FROM);
    CHECK(m.size() == 3 + big.size(), "max-size relay delivered");
}

void relayDropped() {
    Pair p;
    CHECK(p.open("D"), "room setup");
    TestClient outsider;
    CHECK(login(outsider, gHost, gPort, "Out"), "outsider login");
    uint8_t data[4] = {1, 2, 3, 4};
    sendRelay(p.host, 60000, data, sizeof(data));            // no such connection
    sendRelay(p.host, p.host.id, data, sizeof(data));        // to itself
    sendRelay(outsider, p.host.id, data, sizeof(data));      // sender is not a member
    sendRelay(outsider, p.guest.id, data, sizeof(data));
    CHECK(!p.guest.sees(TSSM_MSG_RELAY_FROM, 500), "guest got nothing");
    CHECK(!p.host.sees(TSSM_MSG_RELAY_FROM, 300), "host got nothing");
    // Oversized and empty payloads.
    std::vector<uint8_t> big(TSSM_NET_MAX_RELAY + 1, 0x11);
    sendRelay(p.host, p.guest.id, big.data(), big.size());
    sendRelay(p.host, p.guest.id, data, 0);
    CHECK(!p.guest.sees(TSSM_MSG_RELAY_FROM, 500), "oversized/empty relay dropped");
    // Still alive afterwards.
    sendRelay(p.host, p.guest.id, data, sizeof(data));
    CHECK(p.guest.sees(TSSM_MSG_RELAY_FROM, 1000), "normal relay still works");
    CHECK(!p.host.disconnected && !p.guest.disconnected, "nobody was disconnected");
}

void guestLeaves() {
    Pair p;
    CHECK(p.open("L"), "room setup");
    const uint16_t guestId = p.guest.id;
    sendSimple(p.guest, TSSM_MSG_LEAVE_ROOM);
    Msg m = p.host.waitFor(TSSM_MSG_PEER_LEFT);
    CHECK(m.size() == 4 && (m[1] | (m[2] << 8)) == guestId && m[3] == TSSM_LEAVE_LEFT, "PEER_LEFT(guest, LEFT)");
    RoomInfo h = waitRoomState(p.host);
    CHECK(h.valid && h.count == 1, "host sees a one-member room");
    // The room is still joinable, by the same connection and by a new one.
    sendJoin(p.guest, p.code);
    RoomInfo g = waitRoomState(p.guest);
    CHECK(g.valid && g.count == 2 && g.yourRole == TSSM_ROLE_GUEST, "rejoin works");
    // Disconnect reports DISCONNECT, and another guest can join.
    p.guest.disconnect();
    m = p.host.waitFor(TSSM_MSG_PEER_LEFT);
    CHECK(m.size() == 4 && m[3] == TSSM_LEAVE_DISCONNECT, "PEER_LEFT(DISCONNECT)");
    TestClient g2;
    CHECK(login(g2, gHost, gPort, "NewGuest"), "login");
    sendJoin(g2, p.code);
    CHECK(waitRoomState(g2).valid, "a new guest can join");
}

void hostDisconnects() {
    Pair p;
    CHECK(p.open("X"), "room setup");
    p.host.disconnect();
    Msg m = p.guest.waitFor(TSSM_MSG_ROOM_CLOSED);
    CHECK(m.size() == 2 && m[1] == TSSM_LEAVE_DISCONNECT, "ROOM_CLOSED(DISCONNECT)");
    // The room is gone; the guest is out of the room and can create one.
    TestClient c;
    CHECK(login(c, gHost, gPort, "Late"), "login");
    sendJoin(c, p.code);
    CHECK(waitError(c) == TSSM_ERR_NO_SUCH_ROOM, "room deleted");
    sendSimple(p.guest, TSSM_MSG_CREATE_ROOM);
    CHECK(waitRoomState(p.guest).valid, "guest can create a room afterwards");
}

void hostLeaves() {
    Pair p;
    CHECK(p.open("Y"), "room setup");
    sendSimple(p.host, TSSM_MSG_LEAVE_ROOM);
    Msg m = p.guest.waitFor(TSSM_MSG_ROOM_CLOSED);
    CHECK(m.size() == 2 && m[1] == TSSM_LEAVE_LEFT, "ROOM_CLOSED(LEFT)");
}

void rateLimit() {
    Pair p;
    CHECK(p.open("T"), "room setup");
    const int kSent = 2000;
    uint8_t data[64] = {0};
    const uint32_t start = enet_time_get();
    for (int i = 0; i < kSent; i++) {
        sendRelay(p.host, p.guest.id, data, sizeof(data), TSSM_NET_CHANNEL_RELAY, false);
        if (i % 25 == 24) {
            enet_host_flush(p.host.host);
            p.guest.pump(0);
        }
    }
    enet_host_flush(p.host.host);
    int received = 0;
    for (int quiet = 0; quiet < 30;) { // drain until 300 ms of silence
        const size_t before = p.guest.relays.size();
        p.guest.pump(10);
        if (p.guest.relays.size() == before) quiet++;
        else quiet = 0;
    }
    received = static_cast<int>(p.guest.relays.size());
    const double elapsed = (enet_time_get() - start) / 1000.0;
    const double allowed = TSSM_NET_RELAY_RATE + TSSM_NET_RELAY_RATE * elapsed + 20;
    std::printf("    sent %d, received %d in %.2fs (allowed <= %.0f)\n", kSent, received, elapsed, allowed);
    CHECK(received > 0, "some relay packets arrive");
    CHECK(received <= allowed, "rate limit exceeded: %d > %.0f", received, allowed);
    CHECK(received < kSent, "excess was dropped");
}

struct Scenario {
    const char *name;
    void (*fn)();
};

} // namespace

int main(int argc, char **argv) {
    if (argc > 1) gHost = argv[1];
    if (argc > 2) gPort = static_cast<uint16_t>(std::atoi(argv[2]));
    if (enet_initialize() != 0) {
        std::fprintf(stderr, "enet_initialize failed\n");
        return 2;
    }
    const Scenario scenarios[] = {
        {"version mismatch rejected", versionMismatch},
        {"no HELLO rejected", noHello},
        {"malformed HELLO rejected", badHello},
        {"create and join, roles, already-in-room", createAndJoin},
        {"wrong code -> NO_SUCH_ROOM", wrongCode},
        {"third joiner -> ROOM_FULL, build mismatch -> BUILD", roomFullAndBuild},
        {"relay host<->guest exact bytes and fromConn", relayBothWays},
        {"relay to non-member / oversized / empty dropped", relayDropped},
        {"guest leave -> PEER_LEFT, room still joinable", guestLeaves},
        {"host disconnect -> ROOM_CLOSED", hostDisconnects},
        {"host leave -> ROOM_CLOSED(LEFT)", hostLeaves},
        {"relay rate limit", rateLimit},
    };
    std::printf("testing relay server at %s:%u\n", gHost, gPort);
    int failedScenarios = 0;
    for (const Scenario &s : scenarios) {
        const int before = gFailures;
        s.fn();
        const bool ok = gFailures == before;
        std::printf("%s: %s\n", ok ? "PASS" : "FAIL", s.name);
        if (!ok) failedScenarios++;
    }
    std::printf("%d checks, %d failed scenario(s)\n", gChecks, failedScenarios);
    enet_deinitialize();
    return failedScenarios == 0 ? 0 : 1;
}
