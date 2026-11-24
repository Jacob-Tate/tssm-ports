// PC-only, co-op through a relay server (docs/MULTIPLAYER.md 15): the game's
// side of include/netplay/protocol.h.
//
// A RelayLink stands in for the co-op socket. Each member of a room is a
// virtual address (RelayLink::addr_of), so the Host and Client sessions
// (iNetSession.h) run over it unchanged: the handshake, the join code, the
// authentication of every packet and the reliable layer are end to end between
// the two games, and the server only carries sealed datagrams.
//
// Single-threaded: only the co-op I/O thread (iCoop.cpp) and the tests call it.
// Nothing from the network is ever a format string or part of a log line: the
// server's error text is ignored, and its codes map to constant sentences.
#ifndef INETRELAY_H
#define INETRELAY_H

#include <stddef.h>
#include <stdint.h>

#include "iNetSock.h"

namespace iNet {

enum class RelayState : uint8_t
{
    Off,
    Connecting, // ENet connect sent
    Hello,      // connected, HELLO sent, waiting for WELCOME
    Entering,   // CREATE_ROOM or JOIN_ROOM sent
    InRoom,
    Failed,     // see error(); stop() before starting again
};

class RelayLink
{
public:
    enum : size_t
    {
        kQueue = 128,
        kMaxCodeText = 5,
    };

    RelayLink();
    ~RelayLink();
    RelayLink(const RelayLink&) = delete;
    RelayLink& operator=(const RelayLink&) = delete;

    // `server`: "host", "host:port", "1.2.3.4:5" or "[v6]:port"; names are
    // looked up (this blocks the calling thread for the lookup). `create`: a
    // new room, this game its host; else `code` (4 letters) is joined. False,
    // with error() set, when the server cannot be reached at all.
    bool start(const char* server, const char* name, const char* build_id, bool create, const char* code);
    // Leaves the room and closes the connection (waits at most ~200 ms for
    // the goodbye to go out). Safe to call in any state.
    void stop();

    // Runs the connection: sends and receives for up to `timeout_ms` (0: no
    // waiting), queueing the partner's datagrams for recv().
    void service(int timeout_ms);

    // Like udp_recv: the next queued datagram (its length), 0 when none.
    int recv(uint8_t* buf, size_t cap, NetAddr* from);
    // Like udp_send: to a member's virtual address. False when not in a room
    // or `to` is not the other member.
    bool send(const NetAddr& to, const uint8_t* d, size_t n);

    RelayState state() const { return state_; }
    bool host() const { return create_; }
    // The room code ("ABCD"), "" until in a room.
    const char* code() const { return code_; }
    // The other member's virtual address: false while alone in the room.
    bool partner(NetAddr* out) const;
    // The round trip to the server in ms, -1 before it is known.
    int ping_ms() const { return ping_; }
    // Why the link failed or the room ended: constant text, "" if not.
    const char* error() const { return error_; }
    // Changes since the last call: the partner came or went, the room closed.
    bool take_partner_changed();

    static NetAddr addr_of(uint16_t conn);
    static bool conn_of(const NetAddr& a, uint16_t* conn);

private:
    struct Datagram
    {
        uint16_t from;
        uint16_t len;
        uint8_t data[1200];
    };

    void on_packet(const uint8_t* d, size_t n);
    void send_control(const uint8_t* d, size_t n);
    void fail(const char* why);

    void* host_ = nullptr; // ENetHost*
    void* peer_ = nullptr; // ENetPeer*
    bool enet_ = false;
    RelayState state_ = RelayState::Off;
    bool create_ = false;
    char want_code_[kMaxCodeText] = {};
    char code_[kMaxCodeText] = {};
    char name_[17] = {};
    char build_[41] = {};
    uint16_t me_ = 0, partner_ = 0;
    bool partner_changed_ = false;
    int ping_ = -1;
    const char* error_ = "";
    Datagram* q_ = nullptr; // kQueue, allocated by start()
    size_t qhead_ = 0, qcount_ = 0;
};

} // namespace iNet

#endif
