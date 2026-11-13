// PC-only, co-op (docs/MULTIPLAYER.md, 4.4 and 5): the handshake, the
// session, rate limits, lockouts and timeouts.
//
// Single-threaded and clock-free: every call takes `now` in nanoseconds (any
// monotonic clock), and datagrams go out through NetIO.send, so the same code
// runs over real sockets on the co-op I/O thread (iCoop.cpp) and inside the
// unit test and the fuzzer (tests/nettest.cpp, tests/netfuzz.cpp).
//
// The Host accepts one guest. It keeps no state for a source until that source
// shows a cookie from a CHALLENGE sent to its address, and allocates nothing
// at all: every table is fixed-size. Packets from anyone but the guest are
// dropped before any crypto once the session exists.
#ifndef INETSESSION_H
#define INETSESSION_H

#include "iNetProto.h"
#include "iNetSock.h"

namespace iNet {

struct TokenBucket
{
    double tokens = 0, rate = 0, burst = 0;
    uint64_t last = 0;
    void init(double r, double b, uint64_t now) { rate = r; burst = b; tokens = b; last = now; }
    bool take(uint64_t now);
};

struct NetIO
{
    void* ctx;
    void (*send)(void* ctx, const NetAddr& to, const uint8_t* d, size_t n);
    // An authenticated, validated payload that carried a PLAYER_STATE, a
    // SNAPSHOT, a SNAP_ACK, a STALL, an EVENT, a REL_ACK, PICKUPS or PROGRESS
    // (each from the side allowed to send it).
    void (*on_payload)(void* ctx, const Payload& p);
    // Constant text from this file only: nothing from the network is ever a
    // format string or part of a log line.
    void (*on_log)(void* ctx, const char* msg);
};

// --- the reliable layer (M3) ---------------------------------------------------
//
// Ordered, exactly-once events with a cumulative acknowledgement, from the
// prototype. On the wire since M3 (EVENT and REL_ACK): the sender puts its
// oldest unacknowledged events in every datagram until the other side's
// REL_ACK says they arrived (iCoop.cpp's I/O thread runs both ends).
typedef GameEvent Event;

enum : size_t
{
    kRelWindow = 64,
    // The most events one EVENT message can complete at the receiver: a
    // whole window already waiting behind a gap, then the rest of the
    // message's events past it (a hostile peer can arrange that; an honest
    // one never has more than one message's worth waiting). Room for this
    // many before offering a message means none is ever dropped.
    kRelMaxDelivered = kRelWindow + kMaxEventsPerMsg - 1,
};

struct ReliableOut
{
    Event q[kRelWindow];
    uint16_t base = 0; // rel_id of the oldest unacknowledged event
    uint8_t count = 0;
    bool push(Event e);               // false: the window is full (back-pressure)
    bool ack(uint16_t next_expected); // false: the peer acked something never sent
};

struct ReliableIn
{
    uint16_t expect = 0;
    Event buf[kRelWindow];
    bool present[kRelWindow] = {};
    // Delivers, in order and once each, whatever `e` completes. False: the id
    // is beyond the window (a protocol violation).
    bool offer(const Event& e, void (*deliver)(void* ctx, const Event& e), void* ctx);
};

// --- the session --------------------------------------------------------------

enum : uint64_t
{
    kNsPerMs = 1000000ull,
    kNsPerSec = 1000000000ull,
    kSessionTimeout = 10 * kNsPerSec,
    kHandshakeTimeout = 10 * kNsPerSec,
    kKeepalive = 1 * kNsPerSec,
    kResend = 250 * kNsPerMs,
    // A peer whose STALL reports go on this long with no state of its own in
    // between is given up on (END_STALLED). An honest peer ends the session
    // itself after its game has run no frame for a minute (iCoop.cpp's
    // kStallLimit, DC_STALLED); this is for one that never does.
    kPeerStallLimit = 75 * kNsPerSec,
    // A stalled game reports STALL 4 times a second for as long as it runs no
    // frame. Once its reports have stopped this long, its stall is over, even
    // with no state of its own yet: a game that runs again inside a movie
    // sends no state until the movie ends (the ending's story movie runs
    // longer than kPeerStallLimit, after a short stall when it opens).
    kPeerStallQuiet = 2 * kNsPerSec,
    // M5: a guest silent this long (nothing at all, not even a STALL) may be
    // replaced by a new join that proves the password: its game was closed
    // and started again, or its address changed, and the old session would
    // otherwise hold the slot for the rest of kSessionTimeout.
    kReplaceSilence = 3 * kNsPerSec,
};

enum : uint32_t { kMaxViolations = 16 };

struct Session
{
    bool active = false;
    bool confirmed = false; // host: the guest has sent one authenticated packet
    NetAddr peer = {};
    uint32_t id = 0;
    uint8_t slot = 0;      // the GUEST's slot (1) on both sides
    uint8_t peer_slot = 0; // what the peer's PLAYER_STATE must say: 1 on the host, 0 on the guest
    char peer_name[kMaxName + 1] = {};
    uint8_t k_send[32] = {}, k_recv[32] = {};
    uint32_t send_seq = 0;
    uint32_t recv_max = 0;
    uint64_t recv_bits = 0;
    uint64_t created = 0, last_recv = 0, last_send = 0;
    TokenBucket rate;
    uint32_t violations = 0;
    bool pong_due = false;
    uint32_t pong_value = 0;
    uint64_t sent_at[64] = {};
    double rtt_ms = 0;      // from the packet acks (includes the peer's send interval)
    uint64_t last_ping = 0; // PING once a second, whatever else is sent
    uint32_t ping_sent = 0; // the value of the newest PING, which its PONG must echo
    double ping_ms = -1;    // the newest PING's round trip, or -1
    // Since when the peer has said STALL with no PLAYER_STATE of its own
    // (this side's clock); 0 while it runs.
    uint64_t peer_stall_since = 0;
    // When the peer's newest STALL came (this side's clock): the stall is over
    // once they stop for kPeerStallQuiet.
    uint64_t peer_stall_last = 0;
    // M5: the newest acknowledgement the peer sent (its recv_max and the 32
    // packets below it), for how many of ours got through.
    uint32_t peer_ack = 0, peer_ack_bits = 0;
    // statistics
    uint64_t bytes_out = 0, bytes_in = 0, pkts_out = 0, pkts_in = 0;
    uint64_t drop_replay = 0, drop_tag = 0, drop_parse = 0, drop_rate = 0;
};

// M5: how many of the last packets were lost, in percent, or -1 before there
// are enough to tell. In: the partner's packets to this side (this side's
// replay window, the newest 8 left out as they may still be on their way).
// Out: this side's packets to the partner (the partner's acknowledgements).
int session_loss_in(const Session& s);
int session_loss_out(const Session& s);

enum OpenResult { OPEN_OK, OPEN_BAD_HEADER, OPEN_WRONG_SESSION, OPEN_REPLAY, OPEN_BAD_TAG };
OpenResult session_open(Session& s, const uint8_t* d, size_t n, DataHeader* h,
                        const uint8_t** payload, size_t* plen);
size_t session_seal(Session& s, const uint8_t* payload, size_t plen, uint8_t out[kMaxDatagram],
                    uint64_t now);

// Why a session ended, for the Co-op tab.
enum EndReason : uint8_t
{
    END_NONE,
    END_PEER_LEFT,     // the peer said goodbye (DC_QUIT)
    END_KICKED,        // the host kicked this guest (DC_KICKED)
    END_TIMEOUT,       // 10 s without an authenticated packet
    END_PROTOCOL,      // too many malformed messages (either side)
    END_LOCAL,         // this side stopped
    END_STALLED,       // the peer's game stopped running frames for too long (DC_STALLED,
                       // or STALL reports past kPeerStallLimit)
};

struct HostConfig
{
    uint8_t build_id[kBuildIdSize];
    uint8_t asset_hash[kHashSize];
    char name[kMaxName + 1];
    const char* password; // not kept: only the derived key is
    uint32_t iters;
};

struct HostStats
{
    uint64_t pre_in = 0, pre_rate_dropped = 0, pre_malformed = 0, bad_cookie = 0;
    uint64_t challenges = 0, rejects = 0, bad_password = 0, accepted = 0, nonpeer_data = 0;
    uint64_t replaced = 0; // M5: silent sessions replaced by a new join (kReplaceSilence)
    uint64_t auth_copies = 0; // copies of an AUTH already accepted: dropped (Host::auth_used)
    uint64_t bytes_in_pre = 0, bytes_out_pre = 0, max_reply_ratio_x100 = 0;
};

class Host
{
public:
    bool init(const HostConfig& cfg, const NetIO& io, uint64_t now);
    void on_datagram(const NetAddr& from, const uint8_t* d, size_t n, uint64_t now);
    void update(uint64_t now);
    // Sends p plus a pending PONG.
    bool send(Payload& p, uint64_t now);
    // Ends the session with DISCONNECT(DC_KICKED) and blocks the guest's
    // address until this Host is torn down.
    void kick(uint64_t now);
    // Says goodbye (DC_QUIT) to a connected guest; for stopping.
    // Ends the session with a DISCONNECT carrying `reason` (DC_*).
    void close(uint64_t now, uint8_t reason = DC_QUIT);
    Session& session() { return s_; }
    const HostStats& stats() const { return st_; }
    EndReason last_end() const { return end_; }

private:
    struct Src { uint8_t key[16]; bool used; TokenBucket tb; uint64_t seen; };
    struct Ban { uint8_t key[16]; bool used; uint32_t fails; uint64_t until; };

    void handle_hello(const NetAddr& from, const uint8_t* d, size_t n, uint64_t now);
    void handle_auth(const NetAddr& from, const uint8_t* d, size_t n, uint64_t now);
    void handle_data(const NetAddr& from, const uint8_t* d, size_t n, uint64_t now);
    void reply(const NetAddr& to, const uint8_t* d, size_t n, size_t request_len);
    void reject(const NetAddr& to, const uint8_t nonce[kNonceSize], uint8_t reason, size_t request_len);
    void make_cookie(const NetAddr& a, uint32_t t, const uint8_t nonce[kNonceSize], uint8_t out[kCookieSize]);
    bool source_ok(const NetAddr& a, uint64_t now);
    Ban* ban_find(const NetAddr& a, bool create);
    bool auth_used(const uint8_t nonce[kNonceSize]) const;
    void end(EndReason why) { s_ = Session(); end_ = why; }
    void log(const char* m) { if (io_.on_log) io_.on_log(io_.ctx, m); }

    HostConfig cfg_ = {};
    NetIO io_ = {};
    uint8_t secret_[32] = {}, salt_[kSaltSize] = {}, key_[32] = {};
    TokenBucket global_;
    Src src_[64] = {};
    Ban bans_[64] = {};
    uint64_t fail_window_ = 0;
    uint32_t fail_count_ = 0;
    uint64_t lockout_until_ = 0;
    Session s_;
    uint8_t welcome_[kMaxDatagram] = {};
    size_t welcome_len_ = 0;
    uint8_t welcome_nonce_[kNonceSize] = {};
    // The client nonces of the last joins let in. A client makes a new nonce
    // for every join, and a cookie lasts at most 20 s, so this many cover any
    // copy of an AUTH that could still pass its cookie.
    enum { kAuthsKept = 8 };
    uint8_t auth_nonces_[kAuthsKept][kNonceSize] = {};
    uint32_t auths_seen_ = 0;
    HostStats st_;
    EndReason end_ = END_NONE;
};

enum class ClientState { Idle, HelloSent, AuthSent, Connected, Failed };

// Why a join failed or a session ended, on the guest's side.
enum ClientFail : uint8_t
{
    FAIL_NONE,
    FAIL_REJECTED,  // see fail_reason(): a REJECT_* code
    FAIL_NO_ANSWER, // nothing (or nothing valid) came back within 10 s
    FAIL_ENDED,     // a session that was up ended: see end_reason()
};

class Client
{
public:
    bool connect(const NetAddr& host, const char* password, const char* name,
                 const uint8_t build_id[kBuildIdSize], const uint8_t asset_hash[kHashSize],
                 const NetIO& io, uint64_t now);
    void on_datagram(const NetAddr& from, const uint8_t* d, size_t n, uint64_t now);
    void update(uint64_t now);
    bool send(Payload& p, uint64_t now);
    // Says goodbye (DC_QUIT) if connected, and goes idle.
    // Says goodbye with `reason` (DC_*) when connected, and forgets the session.
    void leave(uint64_t now, uint8_t reason = DC_QUIT);
    ClientState state() const { return state_; }
    ClientFail fail() const { return fail_; }
    uint8_t fail_reason() const { return fail_reason_; }
    EndReason end_reason() const { return end_; }
    Session& session() { return s_; }

private:
    void send_pre(const uint8_t* d, size_t n);
    void failed(ClientFail f, EndReason e);
    void log(const char* m) { if (io_.on_log) io_.on_log(io_.ctx, m); }

    NetIO io_ = {};
    NetAddr host_ = {};
    ClientState state_ = ClientState::Idle;
    ClientFail fail_ = FAIL_NONE;
    uint8_t fail_reason_ = 0;
    EndReason end_ = END_NONE;
    char password_[kMaxPassword + 1] = {};
    char name_[kMaxName + 1] = {};
    uint8_t name_len_ = 0;
    uint8_t build_[kBuildIdSize] = {}, assets_[kHashSize] = {};
    uint8_t nonce_[kNonceSize] = {}, host_nonce_[kNonceSize] = {};
    uint8_t key_[32] = {};
    uint8_t key_salt_[kSaltSize] = {};
    uint32_t key_iters_ = 0;
    uint8_t pw_hash_[32] = {};
    uint8_t th_[32] = {};
    uint8_t pkt_[kMaxDatagram] = {};
    size_t pkt_len_ = 0;
    uint64_t started_ = 0, last_tx_ = 0;
    Session s_;
};

} // namespace iNet

#endif
