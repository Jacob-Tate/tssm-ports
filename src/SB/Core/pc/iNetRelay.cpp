// PC-only, co-op through a relay server: see iNetRelay.h and
// docs/MULTIPLAYER.md 15.
#include "iNetRelay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <enet/enet.h>

#include "netplay/protocol.h"

namespace iNet {

namespace {

// ENet's library state is process-wide; the I/O thread is its only user, but
// the tests may run several links at once.
int sEnetUsers = 0;

bool EnetUp()
{
    if (sEnetUsers == 0 && enet_initialize() != 0)
    {
        return false;
    }
    sEnetUsers++;
    return true;
}

void EnetDown()
{
    if (sEnetUsers > 0 && --sEnetUsers == 0)
    {
        enet_deinitialize();
    }
}

// The members' virtual addresses: 100::/64 (RFC 6666, the discard prefix),
// never a real host, never loopback, the member's connection id at the end.
const uint8_t kVirtPrefix[14] = { 0x01, 0x00 };

const char* ErrorText(uint8_t code)
{
    switch (code)
    {
    case TSSM_ERR_VERSION:
        return "the co-op server runs another version (update the game, or ask whoever runs the server to)";
    case TSSM_ERR_NO_SUCH_ROOM:
        return "there is no room with that code on the server";
    case TSSM_ERR_ROOM_FULL:
        return "that room already has two players";
    case TSSM_ERR_BUILD:
        return "the host plays another version of the game";
    case TSSM_ERR_SERVER_FULL:
        return "the co-op server is full";
    default:
        return "the co-op server refused (a protocol error)";
    }
}

// "host", "host:port", "[v6]" or "[v6]:port" into a name and a port.
bool SplitServer(const char* text, char* hostOut, size_t cap, uint16_t* port)
{
    *port = TSSM_NET_DEFAULT_PORT;
    if (text == NULL || text[0] == '\0')
    {
        return false;
    }
    const char* h = text;
    size_t hn;
    const char* p = NULL;
    if (text[0] == '[')
    {
        const char* close = strchr(text, ']');
        if (close == NULL)
        {
            return false;
        }
        h = text + 1;
        hn = (size_t)(close - h);
        if (close[1] == ':')
        {
            p = close + 2;
        }
        else if (close[1] != '\0')
        {
            return false;
        }
    }
    else
    {
        const char* colon = strchr(text, ':');
        if (colon != NULL && strchr(colon + 1, ':') != NULL)
        {
            hn = strlen(text); // a bare IPv6 address: no port
        }
        else if (colon != NULL)
        {
            hn = (size_t)(colon - text);
            p = colon + 1;
        }
        else
        {
            hn = strlen(text);
        }
    }
    if (hn == 0 || hn >= cap)
    {
        return false;
    }
    memcpy(hostOut, h, hn);
    hostOut[hn] = '\0';
    if (p != NULL)
    {
        char* end = NULL;
        unsigned long v = strtoul(p, &end, 10);
        if (end == p || *end != '\0' || v == 0 || v > 65535)
        {
            return false;
        }
        *port = (uint16_t)v;
    }
    return true;
}

} // namespace

RelayLink::RelayLink() {}

RelayLink::~RelayLink()
{
    stop();
}

NetAddr RelayLink::addr_of(uint16_t conn)
{
    NetAddr a;
    memset(&a, 0, sizeof(a));
    memcpy(a.ip, kVirtPrefix, sizeof(kVirtPrefix));
    a.ip[14] = (uint8_t)(conn >> 8);
    a.ip[15] = (uint8_t)conn;
    a.port = TSSM_NET_DEFAULT_PORT;
    return a;
}

bool RelayLink::conn_of(const NetAddr& a, uint16_t* conn)
{
    if (memcmp(a.ip, kVirtPrefix, sizeof(kVirtPrefix)) != 0 || a.port != TSSM_NET_DEFAULT_PORT)
    {
        return false;
    }
    *conn = (uint16_t)((a.ip[14] << 8) | a.ip[15]);
    return *conn != TSSM_NET_CONN_NONE;
}

bool RelayLink::start(const char* server, const char* name, const char* build_id, bool create, const char* code)
{
    stop();
    error_ = "";
    create_ = create;
    snprintf(name_, sizeof(name_), "%s", name ? name : "");
    snprintf(build_, sizeof(build_), "%s", build_id ? build_id : "");
    memset(want_code_, 0, sizeof(want_code_));
    if (!create)
    {
        if (code == NULL || strlen(code) != TSSM_NET_ROOM_CODE_LEN)
        {
            fail("a room code is 4 letters or digits");
            return false;
        }
        memcpy(want_code_, code, TSSM_NET_ROOM_CODE_LEN);
        // O and I typed for 0 and 1 (which codes never use) mean the letters
        // that look alike in no code either: refuse rather than guess.
        if (!tssm_code_valid(want_code_))
        {
            fail("a room code uses the letters A-Z (no I, L or O) and the digits 2-9");
            return false;
        }
    }
    if (q_ == nullptr)
    {
        q_ = (Datagram*)calloc(kQueue, sizeof(Datagram));
        if (q_ == nullptr)
        {
            fail("out of memory");
            return false;
        }
    }
    qhead_ = qcount_ = 0;

    char hostText[256];
    uint16_t port;
    if (!SplitServer(server, hostText, sizeof(hostText), &port))
    {
        fail("the co-op server's address is not valid (host or host:port)");
        return false;
    }
    if (!EnetUp())
    {
        fail("the network could not be started");
        return false;
    }
    enet_ = true;
    ENetAddress addr;
    if (enet_address_set_host(&addr, hostText) != 0)
    {
        fail("the co-op server's name could not be found");
        return false;
    }
    addr.port = port;
    ENetHost* h = enet_host_create(NULL, 1, TSSM_NET_CHANNEL_COUNT, 0, 0);
    if (h == NULL)
    {
        fail("the network could not be opened");
        return false;
    }
    host_ = h;
    ENetPeer* p = enet_host_connect(h, &addr, TSSM_NET_CHANNEL_COUNT, 0);
    if (p == NULL)
    {
        fail("the co-op server could not be reached");
        return false;
    }
    enet_peer_timeout(p, 0, 5000, 15000);
    peer_ = p;
    state_ = RelayState::Connecting;
    return true;
}

void RelayLink::stop()
{
    ENetHost* h = (ENetHost*)host_;
    ENetPeer* p = (ENetPeer*)peer_;
    if (h != NULL && p != NULL && (state_ == RelayState::Hello || state_ == RelayState::Entering ||
                                   state_ == RelayState::InRoom))
    {
        if (state_ == RelayState::InRoom)
        {
            uint8_t m = TSSM_MSG_LEAVE_ROOM;
            send_control(&m, 1);
        }
        enet_peer_disconnect_later(p, 0);
        ENetEvent ev;
        for (int i = 0; i < 4 && enet_host_service(h, &ev, 50) >= 0; i++)
        {
            if (ev.type == ENET_EVENT_TYPE_RECEIVE)
            {
                enet_packet_destroy(ev.packet);
            }
            else if (ev.type == ENET_EVENT_TYPE_DISCONNECT)
            {
                break;
            }
        }
        enet_peer_reset(p);
    }
    else if (p != NULL)
    {
        enet_peer_reset(p);
    }
    if (h != NULL)
    {
        enet_host_destroy(h);
    }
    host_ = peer_ = nullptr;
    if (enet_)
    {
        EnetDown();
        enet_ = false;
    }
    if (state_ != RelayState::Failed)
    {
        state_ = RelayState::Off;
    }
    code_[0] = '\0';
    me_ = partner_ = 0;
    ping_ = -1;
    qhead_ = qcount_ = 0;
}

void RelayLink::fail(const char* why)
{
    error_ = why;
    state_ = RelayState::Failed;
    if (partner_ != 0)
    {
        partner_ = 0;
        partner_changed_ = true;
    }
}

bool RelayLink::take_partner_changed()
{
    bool c = partner_changed_;
    partner_changed_ = false;
    return c;
}

bool RelayLink::partner(NetAddr* out) const
{
    if (state_ != RelayState::InRoom || partner_ == 0)
    {
        return false;
    }
    *out = addr_of(partner_);
    return true;
}

void RelayLink::send_control(const uint8_t* d, size_t n)
{
    ENetPeer* p = (ENetPeer*)peer_;
    if (p == NULL)
    {
        return;
    }
    ENetPacket* pk = enet_packet_create(d, n, ENET_PACKET_FLAG_RELIABLE);
    if (pk != NULL && enet_peer_send(p, TSSM_NET_CHANNEL_CONTROL, pk) != 0)
    {
        enet_packet_destroy(pk);
    }
}

bool RelayLink::send(const NetAddr& to, const uint8_t* d, size_t n)
{
    uint16_t conn;
    ENetPeer* p = (ENetPeer*)peer_;
    if (state_ != RelayState::InRoom || p == NULL || n == 0 || n > TSSM_NET_MAX_RELAY || !conn_of(to, &conn) ||
        conn != partner_)
    {
        return false;
    }
    uint8_t buf[3 + TSSM_NET_MAX_RELAY];
    buf[0] = TSSM_MSG_RELAY;
    buf[1] = (uint8_t)conn;
    buf[2] = (uint8_t)(conn >> 8);
    memcpy(buf + 3, d, n);
    ENetPacket* pk = enet_packet_create(buf, 3 + n, ENET_PACKET_FLAG_UNSEQUENCED);
    if (pk == NULL)
    {
        return false;
    }
    if (enet_peer_send(p, TSSM_NET_CHANNEL_RELAY, pk) != 0)
    {
        enet_packet_destroy(pk);
        return false;
    }
    return true;
}

int RelayLink::recv(uint8_t* buf, size_t cap, NetAddr* from)
{
    if (qcount_ == 0)
    {
        return 0;
    }
    Datagram& g = q_[qhead_];
    qhead_ = (qhead_ + 1) % kQueue;
    qcount_--;
    if (g.len > cap)
    {
        return -1;
    }
    memcpy(buf, g.data, g.len);
    *from = addr_of(g.from);
    return (int)g.len;
}

void RelayLink::service(int timeout_ms)
{
    ENetHost* h = (ENetHost*)host_;
    if (h == NULL || state_ == RelayState::Failed || state_ == RelayState::Off)
    {
        return;
    }
    ENetEvent ev;
    int r = enet_host_service(h, &ev, (enet_uint32)(timeout_ms > 0 ? timeout_ms : 0));
    while (r > 0)
    {
        switch (ev.type)
        {
        case ENET_EVENT_TYPE_CONNECT:
        {
            state_ = RelayState::Hello;
            uint8_t b[2 + 2 + 1 + 16 + 1 + 40];
            TssmNetWriter w;
            tssmw_init(&w, b, sizeof(b));
            tssmw_u8(&w, TSSM_MSG_HELLO);
            tssmw_u16(&w, TSSM_NET_PROTOCOL_VERSION);
            tssmw_str(&w, name_);
            tssmw_str(&w, build_);
            send_control(b, w.len);
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE:
            on_packet(ev.packet->data, ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            peer_ = nullptr;
            fail(state_ == RelayState::Connecting ? "the co-op server could not be reached"
                                                  : "the connection to the co-op server was lost");
            return;
        default:
            break;
        }
        if (state_ == RelayState::Failed || host_ == nullptr)
        {
            return;
        }
        r = enet_host_check_events(h, &ev);
    }
    ENetPeer* p = (ENetPeer*)peer_;
    if (p != NULL && state_ == RelayState::InRoom)
    {
        ping_ = (int)p->roundTripTime;
    }
}

void RelayLink::on_packet(const uint8_t* d, size_t n)
{
    TssmNetReader r;
    tssmr_init(&r, d, n);
    uint8_t type = tssmr_u8(&r);
    switch (type)
    {
    case TSSM_MSG_WELCOME:
    {
        uint16_t id = tssmr_u16(&r);
        if (r.error || id == 0 || state_ != RelayState::Hello)
        {
            fail("the co-op server sent something wrong");
            return;
        }
        me_ = id;
        state_ = RelayState::Entering;
        if (create_)
        {
            uint8_t m = TSSM_MSG_CREATE_ROOM;
            send_control(&m, 1);
        }
        else
        {
            uint8_t b[1 + TSSM_NET_ROOM_CODE_LEN];
            b[0] = TSSM_MSG_JOIN_ROOM;
            memcpy(b + 1, want_code_, TSSM_NET_ROOM_CODE_LEN);
            send_control(b, sizeof(b));
        }
        return;
    }
    case TSSM_MSG_ERROR:
    {
        uint8_t code = tssmr_u8(&r); // the text after it is not shown (iNetRelay.h)
        fail(ErrorText(code));
        return;
    }
    case TSSM_MSG_ROOM_STATE:
    {
        char code[TSSM_NET_ROOM_CODE_LEN];
        tssmr_bytes(&r, code, sizeof(code));
        uint8_t myRole = tssmr_u8(&r);
        uint8_t count = tssmr_u8(&r);
        uint16_t other = 0;
        bool sawMe = false;
        for (uint8_t i = 0; i < count && !r.error; i++)
        {
            uint16_t id = tssmr_u16(&r);
            tssmr_u8(&r); // role
            char name[TSSM_NET_MAX_NAME];
            tssmr_str(&r, name, sizeof(name));
            tssmr_u16(&r); // ping
            if (id == me_)
            {
                sawMe = true;
            }
            else if (id != 0)
            {
                other = id;
            }
        }
        if (r.error || count > TSSM_NET_MAX_PLAYERS || !sawMe || !tssm_code_valid(code) ||
            myRole != (create_ ? TSSM_ROLE_HOST : TSSM_ROLE_GUEST) ||
            (state_ != RelayState::Entering && state_ != RelayState::InRoom) ||
            (state_ == RelayState::InRoom && memcmp(code, code_, TSSM_NET_ROOM_CODE_LEN) != 0))
        {
            fail("the co-op server sent something wrong");
            return;
        }
        if (state_ == RelayState::Entering)
        {
            memcpy(code_, code, TSSM_NET_ROOM_CODE_LEN);
            code_[TSSM_NET_ROOM_CODE_LEN] = '\0';
            state_ = RelayState::InRoom;
        }
        if (other != partner_)
        {
            partner_ = other;
            partner_changed_ = true;
        }
        return;
    }
    case TSSM_MSG_RELAY_FROM:
    {
        uint16_t from = tssmr_u16(&r);
        size_t len = tssmr_left(&r);
        if (r.error || state_ != RelayState::InRoom || from == 0 || len == 0 || len > TSSM_NET_MAX_RELAY)
        {
            return; // dropped, as a bad datagram would be
        }
        if (from != partner_)
        {
            // A guest that joined after the last ROOM_STATE: the host takes
            // it as its partner (the server only relays between members).
            if (!create_)
            {
                return;
            }
            partner_ = from;
            partner_changed_ = true;
        }
        if (qcount_ == kQueue)
        {
            return; // full: dropped, the session resends
        }
        Datagram& g = q_[(qhead_ + qcount_) % kQueue];
        g.from = from;
        g.len = (uint16_t)len;
        memcpy(g.data, d + r.pos, len);
        qcount_++;
        return;
    }
    case TSSM_MSG_PEER_LEFT:
    {
        uint16_t id = tssmr_u16(&r);
        if (!r.error && id == partner_)
        {
            partner_ = 0;
            partner_changed_ = true;
        }
        return;
    }
    case TSSM_MSG_ROOM_CLOSED:
    {
        uint8_t why = tssmr_u8(&r);
        fail(why == TSSM_LEAVE_SERVER ? "the co-op server shut down" : "the host left the room");
        return;
    }
    default:
        return; // unknown messages from a newer server are ignored
    }
}

} // namespace iNet
