// tssm_relaytest: the co-op session through the relay server (docs/MULTIPLAYER.md
// 15), against a running server/tssm-server:
//
//     tssm_relaytest 127.0.0.1:7865
//
// Two RelayLinks enter one room; a Host and a Client (iNetSession.h) then run
// over them unchanged. Checks: the room, the join, the handshake and a
// connected session both ways, a wrong join code refused end to end (the server
// cannot let anyone in), a wrong room code refused by the server, and the
// guest leaving seen by the host. Exit 0 when every check passes.
#include <stdio.h>
#include <string.h>

#include <chrono>

#include "iNetProto.h"
#include "iNetRelay.h"
#include "iNetSession.h"

using namespace iNet;

static int sFailed = 0;

#define CHECK(cond, what)                                                                                         \
    do                                                                                                            \
    {                                                                                                             \
        bool ok_ = (cond);                                                                                        \
        printf("%s: %s\n", ok_ ? "PASS" : "FAIL", what);                                                          \
        if (!ok_) sFailed++;                                                                                      \
    } while (0)

static uint64_t Now()
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static const uint8_t kBuild[kBuildIdSize] = { 'r', 'e', 'l', 'a', 'y', ' ', 't', 'e', 's', 't' };
static uint8_t sAssets[kHashSize];

static void LinkSend(void* ctx, const NetAddr& to, const uint8_t* d, size_t n)
{
    ((RelayLink*)ctx)->send(to, d, n);
}
static void NoPayload(void*, const Payload&) {}

// Runs both links until `done` or `ms` pass.
template <class F> static bool Pump(RelayLink& a, RelayLink& b, int ms, F done)
{
    uint64_t until = Now() + (uint64_t)ms * 1000000ull;
    while (Now() < until)
    {
        a.service(2);
        b.service(2);
        if (done()) return true;
    }
    return done();
}

static bool EnterRoom(const char* server, RelayLink& h, RelayLink& g, const char* code_override)
{
    if (!h.start(server, "Host", "relaytest", true, NULL)) return false;
    RelayLink dummy;
    if (!Pump(h, dummy, 3000, [&] { return h.state() == RelayState::InRoom; })) return false;
    const char* code = code_override ? code_override : h.code();
    if (!g.start(server, "Guest", "relaytest", false, code)) return false;
    NetAddr a;
    return Pump(h, g, 3000, [&] {
        return g.state() == RelayState::Failed ||
               (g.state() == RelayState::InRoom && h.partner(&a) && g.partner(&a));
    }) && g.state() == RelayState::InRoom;
}

// Runs a Host on `hl` and a Client on `gl` until the client connects or fails.
static ClientState RunSession(RelayLink& hl, RelayLink& gl, const char* hostPw, const char* guestPw, Host& host,
                           Client& client)
{
    HostConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    memcpy(cfg.build_id, kBuild, sizeof kBuild);
    memcpy(cfg.asset_hash, sAssets, kHashSize);
    strcpy(cfg.name, "Host");
    cfg.password = hostPw;
    cfg.iters = kMinIters;
    NetIO hio = { &hl, LinkSend, NoPayload, nullptr };
    NetIO cio = { &gl, LinkSend, NoPayload, nullptr };
    if (!host.init(cfg, hio, Now())) return ClientState::Failed;
    NetAddr hostAddr;
    if (!gl.partner(&hostAddr)) return ClientState::Failed;
    if (!client.connect(hostAddr, guestPw, "Guest", kBuild, sAssets, cio, Now())) return ClientState::Failed;
    uint8_t buf[2048];
    uint64_t until = Now() + 15000000000ull;
    while (Now() < until)
    {
        hl.service(2);
        gl.service(2);
        uint64_t now = Now();
        NetAddr from;
        int n;
        while ((n = hl.recv(buf, sizeof buf, &from)) != 0)
            if (n > 0) host.on_datagram(from, buf, (size_t)n, now);
        while ((n = gl.recv(buf, sizeof buf, &from)) != 0)
            if (n > 0) client.on_datagram(from, buf, (size_t)n, now);
        host.update(now);
        client.update(now);
        if (client.state() == ClientState::Connected && host.session().active) break;
        if (client.state() == ClientState::Failed) break;
    }
    return client.state();
}

int main(int argc, char** argv)
{
    const char* server = argc > 1 ? argv[1] : "127.0.0.1:7865";
    if (!net_init())
    {
        printf("FAIL: net_init\n");
        return 1;
    }
    memset(sAssets, 0x5A, sizeof sAssets);

    {
        RelayLink h, g;
        CHECK(EnterRoom(server, h, g, NULL), "host creates a room, guest joins it by code");
        CHECK(strlen(h.code()) == 4 && strcmp(h.code(), g.code()) == 0, "both see the same 4-character code");
        static Host host;
        static Client client;
        ClientState st = RunSession(h, g, "K7QX-M2PD-9TRW", "K7QX-M2PD-9TRW", host, client);
        CHECK(st == ClientState::Connected && host.session().active,
              "co-op handshake and session connect through the relay");

        // The guest leaves the room: the host sees its partner go.
        g.stop();
        NetAddr a;
        RelayLink none;
        CHECK(Pump(h, none, 3000, [&] { return !h.partner(&a); }), "host sees the guest leave; room stays open");
        CHECK(h.state() == RelayState::InRoom, "host still in its room");
        h.stop();
    }
    {
        RelayLink h, g;
        CHECK(EnterRoom(server, h, g, NULL), "second room");
        static Host host;
        static Client client;
        ClientState st = RunSession(h, g, "K7QX-M2PD-9TRW", "WRONG-CODE-0000", host, client);
        CHECK(st == ClientState::Failed && client.fail() == FAIL_REJECTED,
              "wrong join code refused end to end, though the room was entered");
        h.stop();
        g.stop();
    }
    {
        RelayLink h, g;
        bool entered = EnterRoom(server, h, g, "ZZZZ");
        // ZZZZ could exist by chance only if another test runs at once.
        CHECK(!entered && g.state() == RelayState::Failed && strstr(g.error(), "no room") != NULL,
              "unknown room code refused by the server");
        h.stop();
        g.stop();
    }
    {
        RelayLink h, g;
        CHECK(EnterRoom(server, h, g, NULL), "third room");
        h.stop(); // the host leaves: the room closes for the guest
        RelayLink none;
        CHECK(Pump(g, none, 3000, [&] { return g.state() == RelayState::Failed; }) &&
                  strstr(g.error(), "host left") != NULL,
              "host leaving closes the room for the guest");
        g.stop();
    }
    net_shutdown();
    printf(sFailed ? "relaytest: %d FAILED\n" : "relaytest: all passed\n", sFailed);
    return sFailed ? 1 : 0;
}
