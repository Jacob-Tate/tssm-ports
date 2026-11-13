// PC-only, co-op: the handshake, the session, rate limits, lockouts and
// timeouts (iNetSession.h, docs/MULTIPLAYER.md 4.4 and 5).
#include "iNetSession.h"

#include "iNetCrypto.h"
#include "iNetWire.h"

#include <string.h>

namespace iNet {

// ---------------------------------------------------------------------------
// Rate limiting and reliability

bool TokenBucket::take(uint64_t now)
{
    if (now > last)
    {
        tokens += (double)(now - last) / 1e9 * rate;
        if (tokens > burst) tokens = burst;
        last = now;
    }
    if (tokens < 1.0) return false;
    tokens -= 1.0;
    return true;
}

bool ReliableOut::push(Event e)
{
    if (count >= kRelWindow) return false; // back-pressure: the caller retries next frame
    e.rel_id = (uint16_t)(base + count);
    q[e.rel_id & (kRelWindow - 1)] = e;
    count++;
    return true;
}

bool ReliableOut::ack(uint16_t next_expected)
{
    uint16_t d = (uint16_t)(next_expected - base);
    if (d >= 0x8000) return true; // an older acknowledgement that came late: nothing new
    if (d > count) return false;
    base = (uint16_t)(base + d);
    count = (uint8_t)(count - d);
    return true;
}

bool ReliableIn::offer(const Event& e, void (*deliver)(void* ctx, const Event& e), void* ctx)
{
    uint16_t d = (uint16_t)(e.rel_id - expect);
    if (d >= 0x8000) return true; // already delivered: a resend whose ack was lost
    if (d >= kRelWindow) return false;
    unsigned idx = e.rel_id & (kRelWindow - 1);
    if (!present[idx]) { buf[idx] = e; present[idx] = true; }
    while (present[expect & (kRelWindow - 1)])
    {
        unsigned i = expect & (kRelWindow - 1);
        present[i] = false;
        if (deliver) deliver(ctx, buf[i]);
        expect++;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Packets

size_t session_seal(Session& s, const uint8_t* payload, size_t plen, uint8_t out[kMaxDatagram], uint64_t now)
{
    if (plen > kMaxPayload) return 0;
    DataHeader h;
    h.session_id = s.id;
    h.seq = ++s.send_seq;
    h.ack = s.recv_max;
    h.ack_bits = (uint32_t)(s.recv_bits >> 1);
    encode_data_header(out, h);
    memcpy(out + kDataHeader, payload, plen);
    uint8_t tag[32];
    hmac_sha256(s.k_send, 32, out, kDataHeader + plen, tag);
    memcpy(out + kDataHeader + plen, tag, kTagSize);
    s.sent_at[h.seq & 63] = now;
    return kDataHeader + plen + kTagSize;
}

OpenResult session_open(Session& s, const uint8_t* d, size_t n, DataHeader* h, const uint8_t** payload, size_t* plen)
{
    if (!parse_data_header(d, n, h) || h->seq == 0) return OPEN_BAD_HEADER;
    if (h->session_id != s.id) return OPEN_WRONG_SESSION;
    // Cheap replay pre-check, before spending a MAC on it.
    if (h->seq <= s.recv_max)
    {
        uint32_t back = s.recv_max - h->seq;
        if (back >= 64 || (s.recv_bits >> back) & 1) return OPEN_REPLAY;
    }
    uint8_t tag[32];
    hmac_sha256(s.k_recv, 32, d, n - kTagSize, tag);
    if (!ct_equal(tag, d + n - kTagSize, kTagSize)) return OPEN_BAD_TAG;
    // Authenticated: commit the replay window.
    if (h->seq > s.recv_max)
    {
        uint32_t shift = h->seq - s.recv_max;
        s.recv_bits = shift >= 64 ? 0 : s.recv_bits << shift;
        s.recv_bits |= 1;
        s.recv_max = h->seq;
    }
    else
        s.recv_bits |= 1ull << (s.recv_max - h->seq);
    *payload = d + kDataHeader;
    *plen = n - kDataHeader - kTagSize;
    return OPEN_OK;
}

// Adds a pending PONG to p, encodes, seals and sends.
static bool session_send(Session& s, Payload& p, Role me, const NetIO& io, uint64_t now)
{
    if (!s.active) return false;
    if (s.pong_due) { p.has |= HAS_PONG; p.pong = s.pong_value; }
    uint8_t buf[kMaxPayload];
    size_t len = encode_payload(p, me, buf, sizeof buf);
    if (!len) return false;
    s.pong_due = false;
    uint8_t out[kMaxDatagram];
    size_t n = session_seal(s, buf, len, out, now);
    if (!n) return false;
    io.send(io.ctx, s.peer, out, n);
    s.last_send = now;
    s.bytes_out += n;
    s.pkts_out++;
    return true;
}

static void send_bye(Session& s, uint8_t reason, Role me, const NetIO& io, uint64_t now)
{
    Payload p;
    payload_clear(&p);
    p.has = HAS_DISCONNECT;
    p.disconnect_reason = reason;
    s.pong_due = false;
    session_send(s, p, me, io, now);
}

// The peer's stall is over once its STALL reports have stopped for
// kPeerStallQuiet, state or not; true while it is still past the limit.
static bool peer_stall_check(Session& s, uint64_t now)
{
    if (s.peer_stall_since != 0 && now - s.peer_stall_last > kPeerStallQuiet) s.peer_stall_since = 0;
    return s.peer_stall_since != 0 && now - s.peer_stall_since > kPeerStallLimit;
}

// Applies an authenticated, parsed payload. False: the peer broke protocol.
// *bye: the peer said goodbye, with *why.
static bool session_apply(Session& s, const DataHeader& h, const Payload& p, const NetIO& io, uint64_t now,
                          bool* bye, uint8_t* why)
{
    *bye = false;
    if ((p.has & HAS_PSTATE) && p.pstate.slot != s.peer_slot) return false;
    if (h.ack && h.ack <= s.send_seq && (s.peer_ack == 0 || h.ack >= s.peer_ack))
    {
        // M5: for the loss this side's packets see (session_loss_out).
        s.peer_ack = h.ack;
        s.peer_ack_bits = h.ack_bits;
    }
    if (h.ack && h.ack <= s.send_seq && s.send_seq - h.ack < 64 && s.sent_at[h.ack & 63])
    {
        double sample = (double)(now - s.sent_at[h.ack & 63]) / 1e6;
        s.rtt_ms = s.rtt_ms == 0 ? sample : s.rtt_ms * 0.9 + sample * 0.1;
    }
    if (p.has & HAS_PING) { s.pong_due = true; s.pong_value = p.ping; }
    if ((p.has & HAS_PONG) && s.ping_sent != 0 && p.pong == s.ping_sent)
    {
        // Only the echo of our own newest PING counts; anything else is ignored.
        uint32_t ms = (uint32_t)(now / kNsPerMs) - s.ping_sent;
        if (ms < 60000) s.ping_ms = ms;
        s.ping_sent = 0;
    }
    if (p.has & HAS_DISCONNECT) { *bye = true; *why = p.disconnect_reason; return true; }
    // How long the peer's game has been stalled, by its own reports: a STALL
    // starts (or continues) it, a state without one ends it, and so does
    // kPeerStallQuiet without a STALL (peer_stall_check). update() gives up
    // past kPeerStallLimit.
    if (p.has & HAS_STALL)
    {
        if (s.peer_stall_since == 0) s.peer_stall_since = now ? now : 1;
        s.peer_stall_last = now;
    }
    else if (p.has & HAS_PSTATE)
        s.peer_stall_since = 0;
    if ((p.has & (HAS_PSTATE | HAS_SNAPSHOT | HAS_SNAP_ACK | HAS_STALL | HAS_EVENT | HAS_REL_ACK |
                  HAS_PICKUPS | HAS_PROGRESS)) &&
        io.on_payload)
        io.on_payload(io.ctx, p);
    return true;
}

static int popcount64(uint64_t v)
{
    int n = 0;
    for (; v != 0; v &= v - 1) n++;
    return n;
}

int session_loss_in(const Session& s)
{
    // Bits 8..63 of the replay window: packets recv_max-8 .. recv_max-63.
    if (!s.active || s.recv_max < 64) return -1;
    uint64_t window = s.recv_bits >> 8;
    return (56 - popcount64(window)) * 100 / 56;
}

int session_loss_out(const Session& s)
{
    // The partner's newest ack_bits: our packets peer_ack-1 .. peer_ack-32.
    if (!s.active || s.peer_ack < 40) return -1;
    return (32 - popcount64(s.peer_ack_bits)) * 100 / 32;
}

// A PING once a second: the keepalive, and the round trip the Co-op tab shows.
static void session_ping(Session& s, Role me, const NetIO& io, uint64_t now)
{
    if (s.last_ping != 0 && now - s.last_ping < kKeepalive) return;
    Payload p;
    payload_clear(&p);
    p.has = HAS_PING;
    uint32_t v = (uint32_t)(now / kNsPerMs);
    p.ping = v ? v : 1;
    if (session_send(s, p, me, io, now))
    {
        s.ping_sent = p.ping;
        s.last_ping = now;
    }
}

static EndReason end_for(uint8_t dc)
{
    switch (dc)
    {
    case DC_KICKED: return END_KICKED;
    case DC_TIMEOUT: return END_TIMEOUT;
    case DC_PROTOCOL: return END_PROTOCOL;
    case DC_STALLED: return END_STALLED;
    default: return END_PEER_LEFT;
    }
}

static void derive_keys(const uint8_t key[32], const uint8_t cn[kNonceSize], const uint8_t hn[kNonceSize],
                        bool host, Session& s)
{
    uint8_t salt[2 * kNonceSize];
    memcpy(salt, cn, kNonceSize);
    memcpy(salt + kNonceSize, hn, kNonceSize);
    uint8_t c2h[32], h2c[32];
    hkdf_sha256(salt, sizeof salt, key, 32, "tssm-coop c2h", 13, c2h, 32);
    hkdf_sha256(salt, sizeof salt, key, 32, "tssm-coop h2c", 13, h2c, 32);
    memcpy(s.k_send, host ? h2c : c2h, 32);
    memcpy(s.k_recv, host ? c2h : h2c, 32);
    secure_zero(c2h, 32);
    secure_zero(h2c, 32);
}

// The transcript: everything both sides agreed on, hashed, so the proofs bind
// the build, the assets, both nonces, the salt and the name together.
static void transcript(const uint8_t build[kBuildIdSize], const uint8_t assets[kHashSize], const uint8_t cn[kNonceSize],
                       const uint8_t hn[kNonceSize], const uint8_t salt[kSaltSize], uint32_t iters, uint8_t name_len,
                       const char* name, uint8_t out[32])
{
    uint8_t b[16 + kBuildIdSize + kHashSize + 2 * kNonceSize + kSaltSize + 4 + 1 + kMaxName];
    Writer w(b, sizeof b);
    w.bytes("TSSM-coop/1\0\0\0\0", 16);
    w.bytes(build, kBuildIdSize);
    w.bytes(assets, kHashSize);
    w.bytes(cn, kNonceSize);
    w.bytes(hn, kNonceSize);
    w.bytes(salt, kSaltSize);
    w.u32(iters);
    w.u8(name_len);
    w.bytes(name, name_len);
    sha256(b, w.len, out);
}

static void proof_client(const uint8_t key[32], const uint8_t th[32], uint8_t out[32])
{
    Hmac h;
    hmac_init(&h, key, 32);
    hmac_update(&h, "client", 6);
    hmac_update(&h, th, 32);
    hmac_final(&h, out);
}

static void proof_host(const uint8_t key[32], const uint8_t th[32], uint32_t sid, uint8_t slot, uint8_t out[32])
{
    uint8_t tail[5] = { (uint8_t)sid, (uint8_t)(sid >> 8), (uint8_t)(sid >> 16), (uint8_t)(sid >> 24), slot };
    Hmac h;
    hmac_init(&h, key, 32);
    hmac_update(&h, "host", 4);
    hmac_update(&h, th, 32);
    hmac_update(&h, tail, 5);
    hmac_final(&h, out);
}

// ---------------------------------------------------------------------------
// Host

static void addr_key(const NetAddr& a, uint8_t key[16])
{
    memcpy(key, a.ip, 16);
    if (!addr_is_v4(a)) memset(key + 8, 0, 8); // one IPv6 subscriber owns a whole /64
}

bool Host::init(const HostConfig& cfg, const NetIO& io, uint64_t now)
{
    cfg_ = cfg;
    io_ = io;
    if (cfg.iters < kMinIters || cfg.iters > kMaxIters) return false;
    if (!name_valid(cfg.name, strnlen(cfg.name, kMaxName + 1))) return false;
    size_t pwlen = cfg.password ? strnlen(cfg.password, kMaxPassword + 1) : 0;
    if (pwlen < 1 || pwlen > kMaxPassword) return false;
    if (!os_random(secret_, sizeof secret_) || !os_random(salt_, sizeof salt_)) return false;
    // Once per hosting session: AUTH checks then cost one HMAC, not a PBKDF2.
    pbkdf2_sha256((const uint8_t*)cfg.password, pwlen, salt_, kSaltSize, cfg.iters, key_, 32);
    cfg_.password = nullptr; // not kept
    global_.init(100, 200, now);
    memset(src_, 0, sizeof src_);
    memset(bans_, 0, sizeof bans_);
    fail_window_ = 0;
    fail_count_ = 0;
    lockout_until_ = 0;
    s_ = Session();
    welcome_len_ = 0;
    auths_seen_ = 0;
    st_ = HostStats();
    end_ = END_NONE;
    return true;
}

// The address's own allowance first, the global one second: a datagram its
// own source has already used up its allowance for must not also spend the
// global one, or one flooding address (no spoofing needed) would starve every
// other joiner, the real guest included.
bool Host::source_ok(const NetAddr& a, uint64_t now)
{
    uint8_t key[16];
    addr_key(a, key);
    Src* hit = nullptr;
    Src* oldest = &src_[0];
    for (Src& e : src_)
    {
        if (e.used && memcmp(e.key, key, 16) == 0) { hit = &e; break; }
        if (!e.used || (oldest->used && e.seen < oldest->seen)) oldest = &e;
    }
    if (!hit)
    {
        hit = oldest;
        memcpy(hit->key, key, 16);
        hit->used = true;
        hit->tb.init(4, 8, now);
    }
    hit->seen = now;
    if (!hit->tb.take(now)) return false;
    return global_.take(now);
}

// Bans are only ever created from a cookie-verified AUTH (an address that
// really answered our CHALLENGE) or by the host's own Kick, so spoofed traffic
// cannot fill or flush this table.
Host::Ban* Host::ban_find(const NetAddr& a, bool create)
{
    uint8_t key[16];
    addr_key(a, key);
    Ban* victim = &bans_[0];
    for (Ban& b : bans_)
    {
        if (b.used && memcmp(b.key, key, 16) == 0) return &b;
        if (!b.used || (victim->used && b.until < victim->until)) victim = &b;
    }
    if (!create) return nullptr;
    memset(victim, 0, sizeof *victim);
    memcpy(victim->key, key, 16);
    victim->used = true;
    return victim;
}

bool Host::auth_used(const uint8_t nonce[kNonceSize]) const
{
    uint32_t n = auths_seen_ < kAuthsKept ? auths_seen_ : (uint32_t)kAuthsKept;
    for (uint32_t i = 0; i < n; i++)
    {
        if (memcmp(auth_nonces_[i], nonce, kNonceSize) == 0) return true;
    }
    return false;
}

void Host::reply(const NetAddr& to, const uint8_t* d, size_t n, size_t request_len)
{
    // Never answer an unauthenticated datagram with more bytes than it had:
    // the host cannot be used to amplify a flood at a spoofed address.
    if (!n || n > request_len) return;
    uint64_t ratio = (uint64_t)n * 100 / request_len;
    if (ratio > st_.max_reply_ratio_x100) st_.max_reply_ratio_x100 = ratio;
    st_.bytes_out_pre += n;
    io_.send(io_.ctx, to, d, n);
}

void Host::reject(const NetAddr& to, const uint8_t nonce[kNonceSize], uint8_t reason, size_t request_len)
{
    Reject r;
    r.version = kProtoVersion;
    memcpy(r.client_nonce, nonce, kNonceSize);
    r.reason = reason;
    uint8_t out[64];
    st_.rejects++;
    reply(to, out, encode_reject(out, sizeof out, r), request_len);
}

void Host::make_cookie(const NetAddr& a, uint32_t t, const uint8_t nonce[kNonceSize], uint8_t out[kCookieSize])
{
    uint8_t b[6 + 16 + 2 + 4 + kNonceSize];
    Writer w(b, sizeof b);
    w.bytes("cookie", 6);
    w.bytes(a.ip, 16);
    w.u16(a.port);
    w.u32(t);
    w.bytes(nonce, kNonceSize);
    hmac_sha256(secret_, 32, b, w.len, out);
}

static void host_nonce_of(const uint8_t secret[32], const uint8_t cookie[kCookieSize], uint8_t out[kNonceSize])
{
    uint8_t b[6 + kCookieSize], full[32];
    memcpy(b, "hnonce", 6);
    memcpy(b + 6, cookie, kCookieSize);
    hmac_sha256(secret, 32, b, sizeof b, full);
    memcpy(out, full, kNonceSize);
}

void Host::on_datagram(const NetAddr& from, const uint8_t* d, size_t n, uint64_t now)
{
    if (n == 0 || n > kMaxDatagram) return; // dropped unread
    if (d[0] == PKT_DATA) { handle_data(from, d, n, now); return; }
    st_.pre_in++;
    st_.bytes_in_pre += n;
    if (!source_ok(from, now)) { st_.pre_rate_dropped++; return; }
    uint16_t version;
    uint8_t type;
    if (!peek_pre(d, n, &version, &type)) { st_.pre_malformed++; return; }
    if (type == PKT_HELLO) handle_hello(from, d, n, now);
    else if (type == PKT_AUTH) handle_auth(from, d, n, now);
    else st_.pre_malformed++; // host-to-guest types, or unknown: silence
}

void Host::handle_hello(const NetAddr& from, const uint8_t* d, size_t n, uint64_t now)
{
    uint16_t version;
    uint8_t type;
    peek_pre(d, n, &version, &type);
    if (version != kProtoVersion)
    {
        uint8_t nonce[kNonceSize];
        if (peek_hello_nonce(d, n, nonce)) reject(from, nonce, REJ_VERSION, n);
        else st_.pre_malformed++;
        return;
    }
    Hello h;
    if (!parse_hello(d, n, &h)) { st_.pre_malformed++; return; }
    if (memcmp(h.build_id, cfg_.build_id, kBuildIdSize) != 0) { reject(from, h.client_nonce, REJ_BUILD, n); return; }
    if (memcmp(h.asset_hash, cfg_.asset_hash, kHashSize) != 0) { reject(from, h.client_nonce, REJ_ASSETS, n); return; }
    Ban* b = ban_find(from, false);
    if (b && b->until > now) { reject(from, h.client_nonce, REJ_BLOCKED, n); return; }
    // M5: a guest silent for kReplaceSilence may be replaced by a new join
    // (its AUTH must still prove the password).
    bool stale = s_.active && now - s_.last_recv >= kReplaceSilence;
    if (s_.active && !addr_equal(s_.peer, from) && !stale) { reject(from, h.client_nonce, REJ_FULL, n); return; }
    Challenge c;
    memcpy(c.client_nonce, h.client_nonce, kNonceSize);
    c.cookie_time = (uint32_t)(now / (10 * kNsPerSec));
    make_cookie(from, c.cookie_time, h.client_nonce, c.cookie);
    memcpy(c.salt, salt_, kSaltSize);
    c.iters = cfg_.iters;
    host_nonce_of(secret_, c.cookie, c.host_nonce);
    uint8_t out[128];
    st_.challenges++;
    reply(from, out, encode_challenge(out, sizeof out, c), n);
}

void Host::handle_auth(const NetAddr& from, const uint8_t* d, size_t n, uint64_t now)
{
    Auth a;
    if (!parse_auth(d, n, &a)) { st_.pre_malformed++; return; }
    // 1. The cookie proves the sender received our CHALLENGE at this address
    //    in the last 10-20 s. Until it checks out nothing is stored and
    //    nothing is sent.
    uint32_t t = (uint32_t)(now / (10 * kNsPerSec));
    if (a.cookie_time != t && a.cookie_time + 1 != t) { st_.bad_cookie++; return; }
    uint8_t want[kCookieSize], hn[kNonceSize];
    make_cookie(from, a.cookie_time, a.client_nonce, want);
    if (!ct_equal(want, a.cookie, kCookieSize)) { st_.bad_cookie++; return; }
    host_nonce_of(secret_, a.cookie, hn);
    if (!ct_equal(hn, a.host_nonce, kNonceSize)) { st_.bad_cookie++; return; }

    // 2. Address-level refusals.
    Ban* b = ban_find(from, false);
    if (b && b->until > now) { reject(from, a.client_nonce, REJ_BLOCKED, n); return; }
    if (now < lockout_until_) { reject(from, a.client_nonce, REJ_BUSY, n); return; }
    if (memcmp(a.build_id, cfg_.build_id, kBuildIdSize) != 0) { reject(from, a.client_nonce, REJ_BUILD, n); return; }
    if (memcmp(a.asset_hash, cfg_.asset_hash, kHashSize) != 0) { reject(from, a.client_nonce, REJ_ASSETS, n); return; }
    bool retransmit = s_.active && addr_equal(s_.peer, from) && !s_.confirmed &&
                      memcmp(a.client_nonce, welcome_nonce_, kNonceSize) == 0;
    // A copy of an AUTH that was let in (a late duplicate, or a replay by
    // someone on the path: traffic is not encrypted, and the copy passes the
    // cookie and the proof for up to 20 s). It is not a new join: it must not
    // replace a silent session or start one nobody is behind.
    if (!retransmit && auth_used(a.client_nonce)) { st_.auth_copies++; return; }
    bool stale = s_.active && !retransmit && now - s_.last_recv >= kReplaceSilence;
    if (s_.active && !retransmit && !stale) { reject(from, a.client_nonce, REJ_FULL, n); return; }

    // 3. The password proof.
    uint8_t th[32], proof[32];
    transcript(a.build_id, a.asset_hash, a.client_nonce, a.host_nonce, salt_, cfg_.iters, a.name_len, a.name, th);
    proof_client(key_, th, proof);
    if (!ct_equal(proof, a.proof, kProofSize))
    {
        st_.bad_password++;
        Ban* nb = ban_find(from, true);
        nb->fails++;
        if (nb->fails >= 5 && nb->until != UINT64_MAX)
        {
            uint32_t doublings = nb->fails - 5 > 6 ? 6 : nb->fails - 5; // 1 min .. 64 min
            nb->until = now + (60ull << doublings) * kNsPerSec;
        }
        if (now - fail_window_ > 60 * kNsPerSec) { fail_window_ = now; fail_count_ = 0; }
        if (++fail_count_ >= 20) lockout_until_ = now + 60 * kNsPerSec; // distributed guessing
        log("coop: a joining player gave a wrong password");
        reject(from, a.client_nonce, nb->until > now ? REJ_BLOCKED : REJ_PASSWORD, n);
        return;
    }
    if (retransmit) { reply(from, welcome_, welcome_len_, n); return; }

    // 4. Accepted. A silent session it replaces (M5: the guest's game came
    //    back after a crash or a restart, or from another address) ends
    //    without a goodbye: its peer is gone or no longer listening.
    if (stale)
    {
        st_.replaced++;
        log("coop: a silent player's session was replaced by a new join");
    }
    Session s;
    s.active = true;
    s.peer = from;
    do { os_random(&s.id, sizeof s.id); } while (s.id == 0);
    s.slot = 1;
    s.peer_slot = 1;
    memcpy(s.peer_name, a.name, a.name_len);
    s.peer_name[a.name_len] = 0;
    derive_keys(key_, a.client_nonce, a.host_nonce, true, s);
    s.created = s.last_recv = s.last_send = now;
    s.rate.init(240, 480, now);
    s_ = s;
    end_ = END_NONE;
    Welcome w;
    w.session_id = s.id;
    w.slot = s.slot;
    size_t hl = strnlen(cfg_.name, kMaxName);
    w.name_len = (uint8_t)hl;
    memcpy(w.name, cfg_.name, hl);
    w.name[hl] = 0;
    proof_host(key_, th, s.id, s.slot, w.proof);
    welcome_len_ = encode_welcome(welcome_, sizeof welcome_, w);
    memcpy(welcome_nonce_, a.client_nonce, kNonceSize);
    memcpy(auth_nonces_[auths_seen_++ % kAuthsKept], a.client_nonce, kNonceSize);
    st_.accepted++;
    log("coop: a player joined");
    reply(from, welcome_, welcome_len_, n);
}

void Host::handle_data(const NetAddr& from, const uint8_t* d, size_t n, uint64_t now)
{
    if (!s_.active || !addr_equal(s_.peer, from)) { st_.nonpeer_data++; return; }
    DataHeader h;
    const uint8_t* pl;
    size_t plen;
    OpenResult r = session_open(s_, d, n, &h, &pl, &plen);
    if (r == OPEN_REPLAY) { s_.drop_replay++; return; }
    if (r != OPEN_OK) { s_.drop_tag++; return; }
    // Rate-limited after the tag check, so packets spoofed with the guest's
    // address cannot use up the guest's own allowance.
    if (!s_.rate.take(now)) { s_.drop_rate++; return; }
    s_.bytes_in += n;
    s_.pkts_in++;
    s_.confirmed = true;
    s_.last_recv = now;
    Payload p;
    bool bye = false;
    uint8_t why = 0;
    if (!parse_payload(pl, plen, Role::Client, &p) || !session_apply(s_, h, p, io_, now, &bye, &why))
    {
        s_.drop_parse++;
        if (++s_.violations > kMaxViolations)
        {
            log("coop: the player was dropped (protocol errors)");
            send_bye(s_, DC_PROTOCOL, Role::Host, io_, now);
            end(END_PROTOCOL);
        }
        return;
    }
    if (bye) { log("coop: the player left"); end(end_for(why)); }
}

bool Host::send(Payload& p, uint64_t now) { return session_send(s_, p, Role::Host, io_, now); }

void Host::kick(uint64_t now)
{
    if (!s_.active) return;
    Ban* b = ban_find(s_.peer, true);
    b->until = UINT64_MAX; // until this Host is torn down (hosting stops)
    send_bye(s_, DC_KICKED, Role::Host, io_, now);
    log("coop: the player was kicked");
    end(END_LOCAL);
}

void Host::close(uint64_t now, uint8_t reason)
{
    if (!s_.active) return;
    send_bye(s_, reason >= 1 && reason < DC_END ? reason : (uint8_t)DC_QUIT, Role::Host, io_, now);
    end(END_LOCAL);
}

void Host::update(uint64_t now)
{
    if (!s_.active) return;
    if ((!s_.confirmed && now - s_.created > kHandshakeTimeout) || now - s_.last_recv > kSessionTimeout)
    {
        log("coop: the player timed out");
        end(END_TIMEOUT);
        return;
    }
    if (peer_stall_check(s_, now))
    {
        log("coop: the player's game reported a stall for too long");
        send_bye(s_, DC_TIMEOUT, Role::Host, io_, now);
        end(END_STALLED);
        return;
    }
    session_ping(s_, Role::Host, io_, now);
}

// ---------------------------------------------------------------------------
// Client

bool Client::connect(const NetAddr& host, const char* password, const char* name, const uint8_t build_id[kBuildIdSize],
                     const uint8_t asset_hash[kHashSize], const NetIO& io, uint64_t now)
{
    io_ = io;
    host_ = host;
    if (!password || !name) return false;
    size_t pl = strnlen(password, kMaxPassword + 1);
    size_t nl = strnlen(name, kMaxName + 1);
    if (pl < 1 || pl > kMaxPassword || !name_valid(name, nl)) return false;
    memset(password_, 0, sizeof password_);
    memcpy(password_, password, pl);
    memcpy(name_, name, nl);
    name_[nl] = 0;
    name_len_ = (uint8_t)nl;
    memcpy(build_, build_id, kBuildIdSize);
    memcpy(assets_, asset_hash, kHashSize);
    if (!os_random(nonce_, kNonceSize)) return false;
    Hello h;
    h.version = kProtoVersion;
    memcpy(h.client_nonce, nonce_, kNonceSize);
    memcpy(h.build_id, build_, kBuildIdSize);
    memcpy(h.asset_hash, assets_, kHashSize);
    pkt_len_ = encode_hello(pkt_, sizeof pkt_, h);
    state_ = ClientState::HelloSent;
    fail_ = FAIL_NONE;
    fail_reason_ = 0;
    end_ = END_NONE;
    started_ = now;
    // The derived key is cached across retries to the same hosting session
    // (same salt), keyed by a hash of the password, so a retry costs no PBKDF2.
    uint8_t pwh[32];
    sha256(password_, pl, pwh);
    if (memcmp(pwh, pw_hash_, 32) != 0) { key_iters_ = 0; memcpy(pw_hash_, pwh, 32); }
    s_ = Session();
    send_pre(pkt_, pkt_len_);
    last_tx_ = now;
    return true;
}

void Client::send_pre(const uint8_t* d, size_t n) { io_.send(io_.ctx, host_, d, n); }

void Client::failed(ClientFail f, EndReason e)
{
    state_ = ClientState::Failed;
    fail_ = f;
    end_ = e;
    s_ = Session();
    secure_zero(password_, sizeof password_);
}

void Client::on_datagram(const NetAddr& from, const uint8_t* d, size_t n, uint64_t now)
{
    if (n == 0 || n > kMaxDatagram || !addr_equal(from, host_)) return;
    if (d[0] == PKT_DATA)
    {
        if (state_ != ClientState::Connected) return;
        DataHeader h;
        const uint8_t* pl;
        size_t plen;
        OpenResult r = session_open(s_, d, n, &h, &pl, &plen);
        if (r == OPEN_REPLAY) { s_.drop_replay++; return; }
        if (r != OPEN_OK) { s_.drop_tag++; return; }
        if (!s_.rate.take(now)) { s_.drop_rate++; return; }
        s_.bytes_in += n;
        s_.pkts_in++;
        s_.last_recv = now;
        Payload p;
        bool bye = false;
        uint8_t why = 0;
        if (!parse_payload(pl, plen, Role::Host, &p) || !session_apply(s_, h, p, io_, now, &bye, &why))
        {
            s_.drop_parse++;
            if (++s_.violations > kMaxViolations)
            {
                log("coop: left the host's game (protocol errors)");
                send_bye(s_, DC_PROTOCOL, Role::Client, io_, now);
                failed(FAIL_ENDED, END_PROTOCOL);
            }
            return;
        }
        if (bye) { log("coop: the host ended the session"); failed(FAIL_ENDED, end_for(why)); }
        return;
    }
    uint16_t version;
    uint8_t type;
    if (!peek_pre(d, n, &version, &type)) return;
    if (type == PKT_CHALLENGE && (state_ == ClientState::HelloSent || state_ == ClientState::AuthSent))
    {
        Challenge c;
        if (!parse_challenge(d, n, &c) || memcmp(c.client_nonce, nonce_, kNonceSize) != 0) return;
        bool new_key = key_iters_ != c.iters || memcmp(key_salt_, c.salt, kSaltSize) != 0;
        // One host session has one salt and one iteration count, so a later
        // CHALLENGE (an answer to a resent HELLO) never needs a new key. One
        // that would is not from the host the first came from: ignored, so a
        // stream of them cannot keep this thread in PBKDF2 (up to kMaxIters
        // each) for the whole handshake.
        if (new_key && state_ == ClientState::AuthSent) return;
        if (new_key)
        {
            pbkdf2_sha256((const uint8_t*)password_, strnlen(password_, kMaxPassword), c.salt, kSaltSize, c.iters,
                          key_, 32);
            memcpy(key_salt_, c.salt, kSaltSize);
            key_iters_ = c.iters;
        }
        memcpy(host_nonce_, c.host_nonce, kNonceSize);
        Auth a;
        memset(&a, 0, sizeof a);
        memcpy(a.client_nonce, nonce_, kNonceSize);
        memcpy(a.host_nonce, c.host_nonce, kNonceSize);
        memcpy(a.cookie, c.cookie, kCookieSize);
        a.cookie_time = c.cookie_time;
        memcpy(a.build_id, build_, kBuildIdSize);
        memcpy(a.asset_hash, assets_, kHashSize);
        a.name_len = name_len_;
        memcpy(a.name, name_, name_len_);
        transcript(build_, assets_, nonce_, c.host_nonce, c.salt, c.iters, name_len_, name_, th_);
        proof_client(key_, th_, a.proof);
        pkt_len_ = encode_auth(pkt_, sizeof pkt_, a);
        state_ = ClientState::AuthSent;
        send_pre(pkt_, pkt_len_);
        last_tx_ = now;
    }
    else if (type == PKT_WELCOME && state_ == ClientState::AuthSent)
    {
        Welcome w;
        if (!parse_welcome(d, n, &w)) return;
        uint8_t want[32];
        proof_host(key_, th_, w.session_id, w.slot, want);
        if (!ct_equal(want, w.proof, kProofSize)) return; // not the host we share a password with
        Session s;
        s.active = true;
        s.confirmed = true;
        s.peer = host_;
        s.id = w.session_id;
        s.slot = w.slot;
        s.peer_slot = 0;
        memcpy(s.peer_name, w.name, w.name_len);
        s.peer_name[w.name_len] = 0;
        derive_keys(key_, nonce_, host_nonce_, false, s);
        s.created = s.last_recv = s.last_send = now;
        s.rate.init(240, 480, now);
        s_ = s;
        state_ = ClientState::Connected;
        secure_zero(password_, sizeof password_);
        session_ping(s_, Role::Client, io_, now); // confirms the session to the host
        log("coop: joined the host's game");
    }
    else if (type == PKT_REJECT && (state_ == ClientState::HelloSent || state_ == ClientState::AuthSent))
    {
        Reject r;
        if (!parse_reject(d, n, &r) || memcmp(r.client_nonce, nonce_, kNonceSize) != 0) return;
        fail_reason_ = r.reason;
        failed(FAIL_REJECTED, END_NONE);
        log("coop: the host refused the join");
    }
}

void Client::update(uint64_t now)
{
    if (state_ == ClientState::HelloSent || state_ == ClientState::AuthSent)
    {
        if (now - started_ > kHandshakeTimeout) { failed(FAIL_NO_ANSWER, END_NONE); return; }
        if (now - last_tx_ >= kResend) { send_pre(pkt_, pkt_len_); last_tx_ = now; }
    }
    else if (state_ == ClientState::Connected)
    {
        if (now - s_.last_recv > kSessionTimeout)
        {
            log("coop: the host timed out");
            failed(FAIL_ENDED, END_TIMEOUT);
            return;
        }
        if (peer_stall_check(s_, now))
        {
            log("coop: the host's game reported a stall for too long");
            send_bye(s_, DC_TIMEOUT, Role::Client, io_, now);
            failed(FAIL_ENDED, END_STALLED);
            return;
        }
        session_ping(s_, Role::Client, io_, now);
    }
}

bool Client::send(Payload& p, uint64_t now)
{
    if (state_ != ClientState::Connected) return false;
    return session_send(s_, p, Role::Client, io_, now);
}

void Client::leave(uint64_t now, uint8_t reason)
{
    if (state_ == ClientState::Connected)
        send_bye(s_, reason >= 1 && reason < DC_END ? reason : (uint8_t)DC_QUIT, Role::Client, io_, now);
    s_ = Session();
    state_ = ClientState::Idle;
    fail_ = FAIL_NONE;
    end_ = END_LOCAL;
    secure_zero(password_, sizeof password_);
}

} // namespace iNet
