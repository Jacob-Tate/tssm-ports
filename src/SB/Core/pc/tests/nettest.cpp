// tssm_nettest: the co-op network code on its own (docs/MULTIPLAYER.md, 4.7).
//
// No game code, no window, no GPU. What it checks:
//   1. crypto known-answer tests: SHA-256 (FIPS 180-4), HMAC-SHA256 (RFC 4231),
//      PBKDF2-HMAC-SHA256, HKDF (RFC 5869);
//   2. the wire format: the numeric-only address parser, the name rules, the
//      exact HELLO size and its zero padding, every PLAYER_STATE rule, every
//      SNAPSHOT and SNAP_ACK rule (M2) and their directions, the payload
//      framing, and the later-milestone message types refused;
//   3. the reliable layer kept for later milestones (in memory, with loss);
//   4. a host and a guest over real UDP on 127.0.0.1 with 10% loss each way:
//      the handshake, PLAYER_STATE both ways, newest wins, keepalives;
//   5. hostile cases: wrong password then blocked (and the right password
//      still works), a third player, wrong build / files / version, a forged
//      cookie, short HELLOs, replayed / tampered / wrong-address packets, HELLO
//      floods, amplification, a fake host, protocol violations from an
//      authenticated peer, kick, timeout;
//   6. a fixed-seed mutation fuzz loop over every parser and over a live host
//      and guest (tests/netfuzzcore.h);
//   7. local co-op: the control pipe's lines, the one-time password, Player
//      2's environment block and folder, and a host on 127.0.0.1 at a port the
//      system picks, with the wrong password and Bring back;
//   8. the home network: DISCOVER and OFFER, the responder's sources, rate
//      limits and silence, the finder's nonces, list and expiry, the interface
//      filter over recorded adapter lists, the last-host probe, and a
//      responder and a finder over 127.0.0.1.
//
// Sockets bind to 127.0.0.1 only, so nothing is reachable from the network and
// the firewall is not involved.
//
// Run: tssm_nettest [--fuzz N] [--seed S] [--seeds DIR]. --seeds writes the
// valid seed messages as a libFuzzer corpus for tssm_netfuzz and exits.
// tssm_nettest --wrong-proofs ADDR BUILD FILES N: a client with a wrong
// password against a local co-op host (tools/coop_test.py).

#include "iCoopLocalCore.h"
#include "iNetCrypto.h"
#include "iNetDisc.h"
#include "iNetProto.h"
#include "iNetSession.h"
#include "iNetSock.h"
#include "iNetWire.h"
#include "netfuzzcore.h"

#include "coop_id.h" // generated at build time by tools/coop_id.cmake

#include <chrono>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

using namespace iNet;
using iLocal::code_valid;
using iLocal::format_line;
using iLocal::kCodeLen;
using iLocal::kMaxLine;
using iLocal::Line;
using iLocal::LINE_HELLO;
using iLocal::LINE_QUIT;
using iLocal::LineReader;
using iLocal::new_code;
using iLocal::parse_line;
using iLocal::child_env;
using iLocal::env_block;
using iLocal::profile_dir;
using iLocal::scene_text_valid;

static int sFailures, sChecks;

#define EXPECT(c, ...)                                                                  \
    do                                                                                  \
    {                                                                                   \
        sChecks++;                                                                      \
        if (!(c))                                                                       \
        {                                                                               \
            sFailures++;                                                                \
            printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #c);                     \
            printf(__VA_ARGS__);                                                        \
            printf("\n");                                                               \
        }                                                                               \
    } while (0)

static double wall_ms()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

struct Rng
{
    uint64_t s;
    uint64_t next()
    {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    uint32_t below(uint32_t n) { return (uint32_t)(next() % n); }
    double unit() { return (double)(next() >> 11) / 9007199254740992.0; }
};

static std::string hex(const uint8_t* d, size_t n)
{
    static const char* x = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++)
    {
        s += x[d[i] >> 4];
        s += x[d[i] & 15];
    }
    return s;
}

static const uint32_t kBB02 = 'B' | 'B' << 8 | '0' << 16 | '2' << 24;

// ---------------------------------------------------------------------------
// 1. Crypto

static void test_crypto()
{
    printf("[1] crypto known-answer tests\n");
    uint8_t o[64];
    sha256("", 0, o);
    EXPECT(hex(o, 32) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256('')");
    sha256("abc", 3, o);
    EXPECT(hex(o, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256(abc)");
    const char* m448 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha256(m448, strlen(m448), o);
    EXPECT(hex(o, 32) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "sha256(448 bits)");
    {
        Sha256 s;
        sha256_init(&s);
        std::vector<uint8_t> a(1000, 'a');
        for (int i = 0; i < 1000; i++) sha256_update(&s, a.data(), a.size());
        sha256_final(&s, o);
        EXPECT(hex(o, 32) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "sha256(1M a)");
    }
    uint8_t k[131];
    memset(k, 0x0b, 20);
    hmac_sha256(k, 20, "Hi There", 8, o);
    EXPECT(hex(o, 32) == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", "RFC 4231 #1");
    hmac_sha256((const uint8_t*)"Jefe", 4, "what do ya want for nothing?", 28, o);
    EXPECT(hex(o, 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", "RFC 4231 #2");
    memset(k, 0xaa, 131);
    const char* t6 = "Test Using Larger Than Block-Size Key - Hash Key First";
    hmac_sha256(k, 131, t6, strlen(t6), o);
    EXPECT(hex(o, 32) == "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", "RFC 4231 #6");
    pbkdf2_sha256((const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 1, o, 32);
    EXPECT(hex(o, 32) == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b", "PBKDF2 c=1");
    pbkdf2_sha256((const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 2, o, 32);
    EXPECT(hex(o, 32) == "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43", "PBKDF2 c=2");
    pbkdf2_sha256((const uint8_t*)"password", 8, (const uint8_t*)"salt", 4, 4096, o, 32);
    EXPECT(hex(o, 32) == "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a", "PBKDF2 c=4096");
    uint8_t ikm[22], salt[13], info[10];
    memset(ikm, 0x0b, 22);
    for (int i = 0; i < 13; i++) salt[i] = (uint8_t)i;
    for (int i = 0; i < 10; i++) info[i] = (uint8_t)(0xf0 + i);
    hkdf_sha256(salt, 13, ikm, 22, info, 10, o, 42);
    EXPECT(hex(o, 42) == "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865",
           "RFC 5869 #1");
    uint8_t a[4] = { 1, 2, 3, 4 }, b[4] = { 1, 2, 3, 5 };
    EXPECT(ct_equal(a, a, 4) && !ct_equal(a, b, 4), "ct_equal");
    uint8_t r1[16] = {}, r2[16] = {};
    EXPECT(os_random(r1, 16) && os_random(r2, 16) && memcmp(r1, r2, 16) != 0, "os_random");
    double t0 = wall_ms();
    pbkdf2_sha256((const uint8_t*)"correct horse", 13, salt, 13, kDefaultIters, o, 32);
    printf("  PBKDF2-HMAC-SHA256 x%u (once per host start / per join): %.0f ms\n", (unsigned)kDefaultIters,
           wall_ms() - t0);
}

// ---------------------------------------------------------------------------
// 2. Wire

static PlayerState good_state(uint8_t slot)
{
    PlayerState s;
    memset(&s, 0, sizeof s);
    s.slot = slot;
    s.frame = 1234;
    s.scene = kBB02;
    s.character = CHAR_PATRICK;
    s.flags = PS_VISIBLE;
    s.pos[0] = 12.5f;
    s.pos[1] = -3.25f;
    s.pos[2] = 99999.0f;
    s.yaw = 0x8000;
    s.vel[0] = -1200;
    s.vel[2] = 700;
    s.anim = 0xCAFEF00D;
    s.anim_time = 1.75f;
    s.health = 5;
    s.parts = 0x03FF;
    s.model = 11; // COSTUME_PAT_1
    return s;
}

// Encodes `s` (bypassing the encoder's own checks for the fields a test
// corrupts afterwards), patches `len` bytes at `at`, and parses it back.
static bool parse_patched(const PlayerState& s, size_t at, const void* bytes, size_t len)
{
    uint8_t buf[64];
    size_t n = encode_player_state(s, buf, sizeof buf);
    if (n != kPlayerStateSize) return true; // a failure the caller sees as "accepted"
    memcpy(buf + at, bytes, len);
    PlayerState out;
    return parse_player_state(buf, n, &out);
}

static void test_wire()
{
    printf("[2] wire format\n");
    NetAddr a;
    EXPECT(addr_parse("203.0.113.5:2171", 1, &a) && a.port == 2171 && addr_is_v4(a), "v4:port");
    EXPECT(addr_parse("203.0.113.5", kDefaultPort, &a) && a.port == kDefaultPort, "v4 default port");
    EXPECT(addr_parse("[2001:db8::1]:7", 1, &a) && a.port == 7 && !addr_is_v4(a), "v6");
    EXPECT(addr_parse("2001:db8::1", 9, &a) && a.port == 9, "bare v6");
    EXPECT(!addr_parse("example.com:2171", 1, &a) && !addr_parse("localhost", 1, &a), "names refused");
    EXPECT(!addr_parse("1.2.3.256", 1, &a) && !addr_parse("1.2.3.4:0", 1, &a) && !addr_parse("1.2.3.4:65536", 1, &a),
           "ranges");
    EXPECT(!addr_parse("../../etc/passwd", 1, &a) && !addr_parse("%s%n", 1, &a) && !addr_parse("", 1, &a) &&
               !addr_parse("1.2.3.4 ", 1, &a) && !addr_parse("[::1", 1, &a),
           "junk");
    char back[64];
    addr_parse("10.0.0.7:5", 1, &a);
    addr_format(a, back, sizeof back);
    EXPECT(!strcmp(back, "10.0.0.7:5"), "format %s", back);
    EXPECT(name_valid("Brother", 7) && name_valid("A-b_c.d 9", 9) && !name_valid(" a", 2) && !name_valid("a ", 2) &&
               !name_valid("a/b", 3) && !name_valid("%n", 2) && !name_valid("", 0) &&
               !name_valid("abcdefghijklmnopq", 17) && !name_valid("caf\xc3\xa9", 5),
           "names");

    // HELLO: exactly 1200 bytes, zero padded.
    Hello h;
    h.version = kProtoVersion;
    memset(h.client_nonce, 1, sizeof h.client_nonce);
    memset(h.build_id, 2, sizeof h.build_id);
    memset(h.asset_hash, 3, sizeof h.asset_hash);
    uint8_t buf[kMaxDatagram + 8];
    size_t n = encode_hello(buf, sizeof buf, h);
    Hello h2;
    EXPECT(n == kHelloSize && parse_hello(buf, n, &h2) && !memcmp(&h, &h2, sizeof h), "hello round trip");
    EXPECT(!parse_hello(buf, n - 1, &h2), "short hello refused");
    buf[n] = 0;
    EXPECT(!parse_hello(buf, n + 1, &h2), "long hello refused");
    buf[n - 1] = 1;
    EXPECT(!parse_hello(buf, n, &h2), "non-zero padding refused");

    // Challenge: the iteration bounds a hostile host could abuse.
    Challenge c;
    memset(&c, 0, sizeof c);
    c.iters = kMaxIters + 1;
    n = encode_challenge(buf, sizeof buf, c);
    Challenge c2;
    EXPECT(!parse_challenge(buf, n, &c2), "challenge with too many iterations refused");
    c.iters = kMinIters - 1;
    n = encode_challenge(buf, sizeof buf, c);
    EXPECT(!parse_challenge(buf, n, &c2), "challenge with too few iterations refused");

    // Auth: the name rules apply on the wire.
    Auth au;
    memset(&au, 0, sizeof au);
    au.name_len = 3;
    memcpy(au.name, "a%n", 3);
    n = encode_auth(buf, sizeof buf, au);
    Auth au2;
    EXPECT(n && !parse_auth(buf, n, &au2), "auth with a bad name refused");

    // PLAYER_STATE: the 43-byte body and every rule of 4.4 (and M4's model).
    PlayerState s = good_state(1), t;
    n = encode_player_state(s, buf, sizeof buf);
    EXPECT(n == kPlayerStateSize && parse_player_state(buf, n, &t) && !memcmp(&s, &t, sizeof s),
           "player state round trip (%zu bytes)", n);
    EXPECT(!parse_player_state(buf, n - 1, &t) && !parse_player_state(buf, n + 1, &t), "player state size");
    const uint32_t nan = 0x7FC00000, inf = 0x7F800000, big = 0x49742400 /* 1e6 */, neg = 0xBF800000 /* -1 */,
                   over = 0x44164000 /* 601 */;
    EXPECT(!parse_patched(s, 11, &nan, 4) && !parse_patched(s, 15, &inf, 4) && !parse_patched(s, 19, &big, 4),
           "position NaN / inf / out of range refused");
    EXPECT(!parse_patched(s, 35, &nan, 4) && !parse_patched(s, 35, &neg, 4) && !parse_patched(s, 35, &over, 4),
           "anim time NaN / negative / over 600 refused");
    const uint8_t two = 2, flags = 0x20, hp = 33, lower = 'b', zero = 0, dash = '-';
    EXPECT(!parse_patched(s, 0, &two, 1), "slot 2 refused");
    EXPECT(!parse_patched(s, 9, &two, 1), "unknown character refused");
    EXPECT(!parse_patched(s, 10, &flags, 1), "reserved flag bits refused");
    EXPECT(!parse_patched(s, 39, &hp, 1), "health over 32 refused");
    {
        const uint8_t m27 = 27, mFE = 0xFE, mFF = 0xFF, m26 = 26;
        EXPECT(!parse_patched(s, 42, &m27, 1) && !parse_patched(s, 42, &mFE, 1), "a model of 27 or 0xFE refused");
        EXPECT(parse_patched(s, 42, &mFF, 1) && parse_patched(s, 42, &m26, 1), "model 26 and unknown (0xFF) accepted");
        PlayerState bm = s;
        bm.model = kModelEnd;
        EXPECT(encode_player_state(bm, buf, sizeof buf) == 0, "the encoder never writes a model past the last");
    }
    EXPECT(!parse_patched(s, 5, &lower, 1) && !parse_patched(s, 7, &zero, 1) && !parse_patched(s, 8, &dash, 1),
           "bad scene bytes refused");
    PlayerState bad = s;
    bad.pos[1] = NAN;
    EXPECT(encode_player_state(bad, buf, sizeof buf) == 0, "the encoder never writes a NaN");
    bad = s;
    bad.flags = 0x80;
    EXPECT(encode_player_state(bad, buf, sizeof buf) == 0, "the encoder never writes reserved bits");

    // Payload framing.
    static Payload p, q;
    payload_clear(&p);
    p.has = HAS_PSTATE | HAS_PING | HAS_PONG | HAS_DISCONNECT;
    p.pstate = s;
    p.ping = 77;
    p.pong = 66;
    p.disconnect_reason = DC_QUIT;
    n = encode_payload(p, Role::Client, buf, sizeof buf);
    EXPECT(n == 3 + 43 + 7 + 7 + 4 && parse_payload(buf, n, Role::Client, &q) && q.has == p.has && q.ping == 77 &&
               q.pong == 66 && q.disconnect_reason == DC_QUIT && !memcmp(&q.pstate, &s, sizeof s),
           "payload round trip");
    EXPECT(parse_payload(buf, n, Role::Host, &q), "both roles send the same M1 types");
    EXPECT(!parse_payload(buf, n - 1, Role::Client, &q), "truncated payload refused");
    buf[n] = 0;
    EXPECT(!parse_payload(buf, n + 1, Role::Client, &q), "trailing byte refused");
    // Every type that is not M1's or M2's, the reserved later-milestone ones
    // first. (A 4-byte SNAP_ACK body is valid, so it is left out here; its
    // direction is checked in test_snapshot.)
    bool all_refused = true;
    for (int type = 0; type < 256; type++)
    {
        if (type == MSG_PLAYER_STATE || type == MSG_PING || type == MSG_PONG || type == MSG_DISCONNECT ||
            type == MSG_SNAP_ACK || type == MSG_STALL)
            continue;
        uint8_t m[7] = { (uint8_t)type, 4, 0, 1, 2, 3, 4 };
        if (parse_payload(m, 7, Role::Client, &q) || parse_payload(m, 7, Role::Host, &q)) all_refused = false;
    }
    EXPECT(all_refused, "unknown and later-milestone message types refused");
    uint8_t twice[14] = { MSG_PING, 4, 0, 1, 0, 0, 0, MSG_PING, 4, 0, 2, 0, 0, 0 };
    EXPECT(!parse_payload(twice, 14, Role::Client, &q), "a message type twice refused");
    uint8_t order[14] = { MSG_PONG, 4, 0, 1, 0, 0, 0, MSG_PING, 4, 0, 2, 0, 0, 0 };
    EXPECT(!parse_payload(order, 14, Role::Client, &q), "descending types refused");
    uint8_t ping5[8] = { MSG_PING, 5, 0, 1, 0, 0, 0, 0 };
    EXPECT(!parse_payload(ping5, 8, Role::Client, &q), "a body with trailing bytes refused");
    uint8_t ping3[6] = { MSG_PING, 3, 0, 1, 0, 0 };
    EXPECT(!parse_payload(ping3, 6, Role::Client, &q), "a short body refused");
    uint8_t len_over[7] = { MSG_PING, 0xFF, 0xFF, 1, 0, 0, 0 };
    EXPECT(!parse_payload(len_over, 7, Role::Client, &q), "a length past the end refused");
    uint8_t dc0[4] = { MSG_DISCONNECT, 1, 0, 0 }, dc6[4] = { MSG_DISCONNECT, 1, 0, 6 };
    uint8_t dc5[4] = { MSG_DISCONNECT, 1, 0, DC_STALLED };
    EXPECT(!parse_payload(dc0, 4, Role::Client, &q) && !parse_payload(dc6, 4, Role::Client, &q) &&
               parse_payload(dc5, 4, Role::Client, &q) && q.disconnect_reason == DC_STALLED,
           "disconnect reasons outside 1-5 refused (5: the sender's game stalled too long)");
    // STALL (M2): either side, 2 bytes, not 0.
    payload_clear(&p);
    p.has = HAS_PING | HAS_STALL;
    p.ping = 5;
    p.stall_ms = 65535;
    n = encode_payload(p, Role::Host, buf, sizeof buf);
    EXPECT(n == 7 + 5 && parse_payload(buf, n, Role::Host, &q) && q.stall_ms == 65535 &&
               parse_payload(buf, n, Role::Client, &q) && q.has == p.has,
           "STALL round trip, from either side");
    p.stall_ms = 0;
    EXPECT(encode_payload(p, Role::Client, buf, sizeof buf) == 0, "the encoder writes no STALL of 0 ms");
    uint8_t st0[5] = { MSG_STALL, 2, 0, 0, 0 }, st3[6] = { MSG_STALL, 3, 0, 1, 0, 0 }, st1[4] = { MSG_STALL, 1, 0, 1 };
    EXPECT(!parse_payload(st0, 5, Role::Client, &q) && !parse_payload(st3, 6, Role::Host, &q) &&
               !parse_payload(st1, 4, Role::Client, &q),
           "a STALL of 0 ms, or of 1 or 3 bytes, refused");
    uint8_t st_order[10] = { MSG_STALL, 2, 0, 1, 0, MSG_DISCONNECT, 1, 0, 1, 0 };
    EXPECT(!parse_payload(st_order, 9, Role::Client, &q), "STALL before DISCONNECT refused (ascending types)");
    uint8_t ps41[3 + 41];
    ps41[0] = MSG_PLAYER_STATE;
    ps41[1] = 41;
    ps41[2] = 0;
    memset(ps41 + 3, 0, 41);
    EXPECT(!parse_payload(ps41, sizeof ps41, Role::Client, &q), "a 41-byte player state refused");
    payload_clear(&p);
    p.has = 1u << MSG_INPUT;
    EXPECT(encode_payload(p, Role::Host, buf, sizeof buf) == 0, "the encoder writes no later-milestone type");
    payload_clear(&p);
    p.has = HAS_EVENT;
    EXPECT(encode_payload(p, Role::Host, buf, sizeof buf) == 0, "the encoder writes no EVENT without events");

    // Newest wins, across the wrap.
    EXPECT(frame_newer(2, 1) && !frame_newer(1, 2) && !frame_newer(5, 5) && frame_newer(3, 0xFFFFFFF0u) &&
               !frame_newer(0xFFFFFFF0u, 3),
           "frame_newer");
    EXPECT(scene_valid(kBB02) && !scene_valid(0) && !scene_valid('b' | 'B' << 8 | '0' << 16 | '2' << 24), "scene_valid");

    // Data header bounds.
    DataHeader dh;
    uint8_t pkt[kMaxDatagram + 1];
    memset(pkt, 0, sizeof pkt);
    pkt[0] = PKT_DATA;
    pkt[1] = 1;
    EXPECT(parse_data_header(pkt, kDataHeader + kTagSize, &dh) && !parse_data_header(pkt, kDataHeader + kTagSize - 1, &dh) &&
               !parse_data_header(pkt, kMaxDatagram + 1, &dh),
           "data header sizes");
    pkt[1] = 0;
    EXPECT(!parse_data_header(pkt, kDataHeader + kTagSize, &dh), "session id 0 refused");

    // The bounded reader.
    uint8_t three[3] = { 1, 2, 3 };
    Reader r(three, 3);
    (void)r.u16();
    (void)r.u16();
    EXPECT(!r.ok && r.left() == 0 && r.u32() == 0, "reader stays failed");
    uint8_t out2[2];
    Writer w(out2, 2);
    w.u32(5);
    EXPECT(!w.ok && w.len == 0, "writer refuses an overflow");
}

// ---------------------------------------------------------------------------
// 2b. SNAPSHOT and SNAP_ACK (M2, docs/MULTIPLAYER.md 5.3)

static void good_snapshot(Snapshot* s, int entries)
{
    memset(s, 0, sizeof *s);
    s->scene = kBB02;
    s->table = 0x12345678;
    s->things = 200;
    s->seq = 1000;
    s->base = 990;
    s->host_ms = 777777;
    s->flags = SNAP_COMPLETE;
    s->count = (uint8_t)entries;
    for (int i = 0; i < entries; i++)
    {
        SnapEntry& e = s->e[i];
        e.id = (uint16_t)(i * 2 + 1);
        e.mask = SE_MASK;
        e.pos[0] = 1.5f * i;
        e.pos[1] = -40.0f;
        e.pos[2] = 99999.5f;
        e.rot[0] = 0;
        e.rot[1] = 23170;
        e.rot[2] = 0;
        e.rot[3] = -23170;
        e.anim = 0xB0B00000u + i;
        e.anim_time = 0.5f + i;
        e.flags = WF_SHOWN | WF_ENABLED;
        e.alpha = 255;
    }
}

// The one-entry snapshot of good_snapshot, `len` bytes patched at `at`
// (offsets: 0 scene, 4 table, 8 things, 10 seq, 14 base, 18 host time,
// 22 flags, 23 count; the entry: 24 id, 26 mask, 27 position, 39 rotation,
// 47 animation, 51 time, 55 flags, 56 fade).
static bool snap_patched(size_t at, const void* bytes, size_t len)
{
    static Snapshot s, out;
    good_snapshot(&s, 1);
    uint8_t buf[128];
    size_t n = encode_snapshot(s, buf, sizeof buf);
    if (n != 57) return true; // a failure the caller sees as "accepted"
    memcpy(buf + at, bytes, len);
    return parse_snapshot(buf, n, &out);
}

static void test_snapshot()
{
    printf("[2b] snapshots\n");
    static Snapshot s, t;
    uint8_t buf[4096];

    good_snapshot(&s, 1);
    size_t n = encode_snapshot(s, buf, sizeof buf);
    EXPECT(n == 57 && n == snapshot_size(s), "a one-entry snapshot is 57 bytes (%zu)", n);
    EXPECT(parse_snapshot(buf, n, &t) && !memcmp(&s, &t, offsetof(Snapshot, e) + sizeof(SnapEntry)),
           "snapshot round trip");
    EXPECT(!parse_snapshot(buf, n - 1, &t), "a truncated snapshot refused");
    buf[n] = 0;
    EXPECT(!parse_snapshot(buf, n + 1, &t), "a trailing byte refused");

    // Every combination of fields, one per entry.
    good_snapshot(&s, 31);
    for (int i = 0; i < 31; i++) s.e[i].mask = (uint8_t)(1 + i);
    n = encode_snapshot(s, buf, sizeof buf);
    bool same = n == snapshot_size(s) && parse_snapshot(buf, n, &t) && t.count == 31;
    for (int i = 0; same && i < 31; i++)
    {
        const SnapEntry& a = s.e[i];
        const SnapEntry& b = t.e[i];
        same = a.id == b.id && a.mask == b.mask;
        if (a.mask & SE_POS) same = same && !memcmp(a.pos, b.pos, sizeof a.pos);
        if (a.mask & SE_ROT) same = same && !memcmp(a.rot, b.rot, sizeof a.rot);
        if (a.mask & SE_ANIM) same = same && a.anim == b.anim && a.anim_time == b.anim_time;
        if (a.mask & SE_FLAGS) same = same && a.flags == b.flags;
        if (a.mask & SE_ALPHA) same = same && a.alpha == b.alpha;
    }
    EXPECT(same, "every mask combination round-trips (%zu bytes)", n);

    // The header.
    const uint8_t lower = 'b', zero8 = 0, bit1 = 2, count97 = 97, count2 = 2;
    const uint16_t things0 = 0, things1025 = 1025;
    const uint32_t zero32 = 0, base_same = 1000, base_far = 1000 - 65, base_64 = 1000 - 64, base_ahead = 1001;
    EXPECT(!snap_patched(0, &lower, 1) && !snap_patched(3, &zero8, 1), "bad scene bytes refused");
    EXPECT(!snap_patched(8, &things0, 2) && !snap_patched(8, &things1025, 2), "a table of 0 or over 1024 refused");
    EXPECT(!snap_patched(10, &zero32, 4), "seq 0 refused");
    EXPECT(!snap_patched(14, &base_same, 4) && !snap_patched(14, &base_far, 4) && !snap_patched(14, &base_ahead, 4),
           "a base equal to, after, or more than 64 before seq refused");
    EXPECT(snap_patched(14, &base_64, 4) && snap_patched(14, &zero32, 4), "a base 64 back, or none, accepted");
    EXPECT(!snap_patched(22, &bit1, 1), "reserved snapshot flags refused");
    EXPECT(!snap_patched(23, &count97, 1), "more than 96 entries refused");
    EXPECT(!snap_patched(23, &count2, 1), "a count past the end refused");
    // The entry.
    const uint16_t id200 = 200, id199 = 199;
    const uint8_t mask0 = 0, mask_bit5 = 0x20 | SE_POS, wf_bit6 = 0x40;
    const uint32_t nan = 0x7FC00000, inf = 0xFF800000, big = 0x49742400 /* 1e6 */, neg = 0xBF800000 /* -1 */,
                   over = 0x44164000 /* 601 */;
    const int16_t minus32768 = -32768;
    const int16_t zero4[4] = { 0, 0, 0, 0 };
    EXPECT(!snap_patched(24, &id200, 2), "an ID past the table refused");
    EXPECT(snap_patched(24, &id199, 2), "the last ID accepted");
    EXPECT(!snap_patched(26, &mask0, 1) && !snap_patched(26, &mask_bit5, 1),
           "a mask of 0 or with reserved bits refused");
    EXPECT(!snap_patched(27, &nan, 4) && !snap_patched(31, &inf, 4) && !snap_patched(35, &big, 4),
           "position NaN / inf / out of range refused");
    EXPECT(!snap_patched(39, &minus32768, 2) && !snap_patched(39, zero4, 8), "rotation -32768 or all zero refused");
    EXPECT(!snap_patched(51, &nan, 4) && !snap_patched(51, &neg, 4) && !snap_patched(51, &over, 4),
           "anim time NaN / negative / over 600 refused");
    EXPECT(!snap_patched(55, &wf_bit6, 1), "reserved entry flags refused");
    // Order: ascending IDs, no repeats.
    good_snapshot(&s, 3);
    n = encode_snapshot(s, buf, sizeof buf);
    uint16_t one = 1, three = 3;
    memcpy(buf + 24 + 33, &one, 2); // the second ID equal to the first
    EXPECT(!parse_snapshot(buf, n, &t), "a repeated ID refused");
    memcpy(buf + 24 + 33, &three, 2);
    memcpy(buf + 24 + 66, &one, 2); // the third below the second
    EXPECT(!parse_snapshot(buf, n, &t), "IDs out of order refused");
    // The encoder writes nothing the parser would refuse.
    good_snapshot(&s, 2);
    s.e[1].id = s.e[0].id;
    EXPECT(encode_snapshot(s, buf, sizeof buf) == 0, "the encoder refuses repeated IDs");
    good_snapshot(&s, 1);
    s.e[0].pos[2] = NAN;
    EXPECT(encode_snapshot(s, buf, sizeof buf) == 0, "the encoder refuses a NaN");
    good_snapshot(&s, 1);
    s.e[0].rot[0] = -32768;
    EXPECT(encode_snapshot(s, buf, sizeof buf) == 0, "the encoder refuses -32768");
    good_snapshot(&s, 0);
    s.seq = 5;
    s.base = 0;
    n = encode_snapshot(s, buf, sizeof buf);
    EXPECT(n == kSnapHeaderSize && parse_snapshot(buf, n, &t) && t.count == 0, "an empty snapshot is valid");

    // In a payload: the host sends SNAPSHOT, the guest SNAP_ACK, never the
    // other way round.
    static Payload p, q;
    payload_clear(&p);
    p.has = HAS_PSTATE | HAS_SNAPSHOT | HAS_PING;
    p.pstate = good_state(0);
    good_snapshot(&p.snap, 28);
    p.ping = 9;
    n = encode_payload(p, Role::Host, buf, sizeof buf);
    EXPECT(n > 900 && n <= kMaxPayload && parse_payload(buf, n, Role::Host, &q) && q.has == p.has &&
               q.snap.count == 28 && q.snap.seq == p.snap.seq,
           "a 28-entry snapshot with a player state fits a datagram (%zu bytes)", n);
    EXPECT(!parse_payload(buf, n, Role::Client, &q), "a SNAPSHOT from the guest refused");
    EXPECT(encode_payload(p, Role::Client, buf, sizeof buf) == 0, "the guest's encoder writes no SNAPSHOT");
    good_snapshot(&p.snap, 40);
    EXPECT(encode_payload(p, Role::Host, buf, sizeof buf) == 0, "a snapshot that does not fit is not encoded");
    payload_clear(&p);
    p.has = HAS_PSTATE | HAS_SNAP_ACK;
    p.pstate = good_state(1);
    p.snap_ack = 1000;
    n = encode_payload(p, Role::Client, buf, sizeof buf);
    EXPECT(n == 46 + 7 && parse_payload(buf, n, Role::Client, &q) && q.snap_ack == 1000, "SNAP_ACK round trip");
    EXPECT(!parse_payload(buf, n, Role::Host, &q), "a SNAP_ACK from the host refused");
    EXPECT(encode_payload(p, Role::Host, buf, sizeof buf) == 0, "the host's encoder writes no SNAP_ACK");
    uint8_t ack0[7] = { MSG_SNAP_ACK, 4, 0, 0, 0, 0, 0 }, ack5[8] = { MSG_SNAP_ACK, 5, 0, 1, 0, 0, 0, 0 };
    EXPECT(!parse_payload(ack0, 7, Role::Client, &q), "SNAP_ACK 0 refused");
    EXPECT(!parse_payload(ack5, 8, Role::Client, &q), "a 5-byte SNAP_ACK refused");
    uint8_t snapshort[3 + 23];
    memset(snapshort, 0, sizeof snapshort);
    snapshort[0] = MSG_SNAPSHOT;
    snapshort[1] = 23;
    EXPECT(!parse_payload(snapshort, sizeof snapshort, Role::Host, &q), "a 23-byte SNAPSHOT refused");
    // The largest snapshot the builder makes (iCoopWorld.cpp's budget) with a
    // player state, a ping and a pong still fits.
    good_snapshot(&s, 0);
    size_t body = kSnapHeaderSize;
    int k = 0;
    while (body + snap_entry_size(SE_MASK) <= kSnapBodyMax && k < kMaxSnapEntries)
    {
        s.e[k] = SnapEntry();
        s.e[k].id = (uint16_t)k;
        s.e[k].mask = SE_MASK;
        s.e[k].rot[3] = 32767;
        body += snap_entry_size(SE_MASK);
        k++;
    }
    s.count = (uint8_t)k;
    payload_clear(&p);
    p.has = HAS_PSTATE | HAS_SNAPSHOT | HAS_PING | HAS_PONG;
    p.pstate = good_state(0);
    p.snap = s;
    p.ping = p.pong = 1;
    n = encode_payload(p, Role::Host, buf, sizeof buf);
    EXPECT(n > 0 && n <= kMaxPayload, "a full snapshot budget fits beside a state, a ping and a pong (%zu)", n);
}

// ---------------------------------------------------------------------------
// 2c. M3: EVENT, REL_ACK, PICKUPS (docs/MULTIPLAYER.md 6.3)

static GameEvent good_event(uint8_t type)
{
    GameEvent e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.scene = kBB02;
    switch (type)
    {
    case EV_HIT:
        e.table = 0x12345678;
        e.id = 126;
        e.kind = ATK_SIDE;
        e.level = 1;
        e.dir[0] = 32767;
        e.dir[2] = -32767;
        e.pos[0] = 11.5f;
        e.pos[1] = 6.0f;
        e.pos[2] = -65.25f;
        break;
    case EV_DAMAGE:
        e.id = 126;
        e.kind = DK_FROM;
        e.level = 1;
        e.sub = 2;
        e.pos[0] = 1.0f;
        e.pos[1] = -2.0f;
        e.pos[2] = 3.0f;
        break;
    case EV_PICKUP:
        e.table = 0xAFD40CA7;
        e.id = 303;
        e.kind = PW_GIVEN;
        break;
    case EV_UPGRADE:
        e.kind = UPG_BOUGHT;
        e.sub = 9;
        break;
    case EV_UPGRADE_ACK:
        e.kind = UPG_TAUGHT;
        e.sub = 7;
        e.level = GR_NO;
        break;
    case EV_GRANT:
        e.table = 0xAFD40CA7;
        e.id = 303;
        e.kind = GR_YOURS;
        break;
    case EV_SHOW:
        e.table = 0x5EED0001;
        e.id = 7;
        e.kind = SHOW_CUTSCENE_START;
        e.level = SHOWF_PAUSE_MUSIC;
        break;
    case EV_LOOK:
        e.kind = CHAR_PATRICK;
        e.level = 1;
        e.sub = 4;
        break;
    case EV_BOSS:
        e.table = 0x5EED0002;
        e.id = 41;
        e.kind = BOSS_FROGFISH;
        e.level = 2;   // stage
        e.sub = 6;     // camera state
        e.dir[0] = 18; // state
        e.dir[1] = 7;  // health
        e.dir[2] = 1;  // attack
        break;
    }
    return e;
}

// A payload of one EVENT with these events, from `from`; 0 when refused.
static size_t encode_events(const GameEvent* ev, unsigned n, Role from, uint8_t* out, size_t cap, uint16_t first = 7)
{
    static Payload p;
    payload_clear(&p);
    p.has = HAS_EVENT;
    p.ev_first = first;
    p.ev_count = (uint8_t)n; // more than a message holds is the encoder's to refuse
    for (unsigned i = 0; i < n && i < kMaxEventsPerMsg; i++) p.ev[i] = ev[i];
    return encode_payload(p, from, out, cap);
}

// Encodes `e` alone from `from`, patches byte `at` of the event (0 = its
// type) with `v`, and parses it back.
static bool event_patched(const GameEvent& e, Role from, size_t at, uint8_t v)
{
    uint8_t buf[kMaxPayload];
    size_t n = encode_events(&e, 1, from, buf, sizeof buf);
    if (n == 0 || 3 + kEventHeaderSize + at >= n) return false;
    buf[3 + kEventHeaderSize + at] = v;
    static Payload q;
    return parse_payload(buf, n, from, &q);
}

static void test_events()
{
    printf("[2c] M3 events, acknowledgements, pickups\n");
    uint8_t buf[kMaxPayload];
    static Payload q;
    // Each type round-trips from its own side, and is refused from the other.
    const uint8_t types[7] = { EV_HIT, EV_DAMAGE, EV_PICKUP, EV_GRANT, EV_UPGRADE, EV_UPGRADE_ACK, EV_BOSS };
    const Role sides[7] = { Role::Client, Role::Host,   Role::Client, Role::Host,
                            Role::Client, Role::Host,   Role::Host };
    const size_t sizes[7] = { 31, 22, 12, 12, 7, 8, 20 };
    for (int k = 0; k < 7; k++)
    {
        GameEvent e = good_event(types[k]);
        Role other = sides[k] == Role::Host ? Role::Client : Role::Host;
        size_t n = encode_events(&e, 1, sides[k], buf, sizeof buf);
        uint8_t again[kMaxPayload];
        bool ok = n == 3 + kEventHeaderSize + sizes[k] && event_size(types[k]) == sizes[k] &&
                  parse_payload(buf, n, sides[k], &q) && q.ev_count == 1 && q.ev[0].rel_id == 7 &&
                  q.ev[0].type == types[k] && q.ev[0].id == e.id &&
                  encode_payload(q, sides[k], again, sizeof again) == n && !memcmp(again, buf, n);
        EXPECT(ok, "event type %d round trip from its own side (%zu bytes)", (int)types[k], n);
        EXPECT(!parse_payload(buf, n, other, &q) && encode_events(&e, 1, other, buf, sizeof buf) == 0,
               "event type %d refused from the other side (parser and encoder)", (int)types[k]);
    }
    GameEvent e = good_event(EV_HIT);
    // Field rules, through the parser (offsets in the event: type 0, scene 1,
    // table 5, id 9, kind 11, level 12, dir 13, pos 19).
    EXPECT(!event_patched(e, Role::Client, 0, EV_END) && !event_patched(e, Role::Client, 0, 0) &&
               !event_patched(e, Role::Client, 0, 0xFF),
           "an unknown event type, or 0, refused");
    EXPECT(!event_patched(e, Role::Client, 1, 'b'), "a bad scene byte refused");
    EXPECT(!event_patched(e, Role::Client, 10, 0x04), "a net ID of 1024 or more refused");
    EXPECT(!event_patched(e, Role::Client, 11, 0) && !event_patched(e, Role::Client, 11, ATK_END),
           "an attack kind of 0 or past the last refused");
    // Protocol 7: the car (9) and the SpongeBowl's lightning (10) are
    // attacks; 11 is not.
    EXPECT(ATK_CAR == 9 && ATK_ZAP == 10 && ATK_END == 11 &&
               event_patched(e, Role::Client, 11, ATK_CAR) &&
               event_patched(e, Role::Client, 11, ATK_ZAP) &&
               !event_patched(e, Role::Client, 11, 11),
           "attack 9 and 10 accepted, 11 refused");
    EXPECT(!event_patched(e, Role::Client, 12, 0) && !event_patched(e, Role::Client, 12, kPowerupMax + 1),
           "a power-up level of 0 or past the last refused");
    {
        GameEvent d = e;
        d.dir[1] = -32768;
        EXPECT(encode_events(&d, 1, Role::Client, buf, sizeof buf) == 0, "a direction of -32768 refused (encoder)");
        uint8_t nan[4] = { 0, 0, 0xC0, 0x7F };
        size_t n = encode_events(&e, 1, Role::Client, buf, sizeof buf);
        memcpy(buf + 3 + kEventHeaderSize + 19, nan, 4);
        EXPECT(!parse_payload(buf, n, Role::Client, &q), "a NaN position refused");
        d = e;
        d.pos[1] = 200000.0f;
        EXPECT(encode_events(&d, 1, Role::Client, buf, sizeof buf) == 0, "a position past the world limit refused");
    }
    {
        GameEvent d = good_event(EV_DAMAGE);
        d.kind = DK_END;
        EXPECT(encode_events(&d, 1, Role::Host, buf, sizeof buf) == 0, "a damage kind past the last refused");
        d = good_event(EV_DAMAGE);
        d.level = 0;
        GameEvent d2 = good_event(EV_DAMAGE);
        d2.level = kPlayerHealthMax + 1;
        EXPECT(encode_events(&d, 1, Role::Host, buf, sizeof buf) == 0 &&
                   encode_events(&d2, 1, Role::Host, buf, sizeof buf) == 0,
               "damage of 0 or past the health limit refused");
        d = good_event(EV_DAMAGE);
        d.sub = kDamageSubEnd;
        EXPECT(encode_events(&d, 1, Role::Host, buf, sizeof buf) == 0, "a damage sub-kind past the last refused");
        d = good_event(EV_DAMAGE);
        d.kind = DK_PUSH;
        d.sub = 0;
        d.pos[0] = 250.0f;
        EXPECT(encode_events(&d, 1, Role::Host, buf, sizeof buf) == 0, "a knockback past its limit refused");
        d.pos[0] = -200.0f;
        EXPECT(encode_events(&d, 1, Role::Host, buf, sizeof buf) > 0, "a knockback at its limit accepted");
        d = good_event(EV_DAMAGE);
        d.kind = DK_PLAIN;
        d.sub = 0;
        EXPECT(encode_events(&d, 1, Role::Host, buf, sizeof buf) == 0, "plain damage with a point refused");
        d.pos[0] = d.pos[1] = d.pos[2] = 0.0f;
        d.id = kNoThing;
        EXPECT(encode_events(&d, 1, Role::Host, buf, sizeof buf) > 0, "plain damage from no enemy accepted");
        d.id = kMaxNetIds;
        EXPECT(encode_events(&d, 1, Role::Host, buf, sizeof buf) == 0, "damage from a net ID past the table refused");
        GameEvent g = good_event(EV_GRANT);
        g.kind = 0;
        GameEvent g2 = good_event(EV_GRANT);
        g2.kind = GR_END;
        GameEvent k = good_event(EV_PICKUP);
        k.id = kMaxPickups;
        EXPECT(encode_events(&g, 1, Role::Host, buf, sizeof buf) == 0 &&
                   encode_events(&g2, 1, Role::Host, buf, sizeof buf) == 0 &&
                   encode_events(&k, 1, Role::Client, buf, sizeof buf) == 0,
               "a grant of 0 or past the last, a pickup ID of 1024, refused");
        // Protocol 7: EV_PICKUP's way, 0..2 (offset 11).
        k = good_event(EV_PICKUP);
        EXPECT(event_patched(k, Role::Client, 11, PW_TOUCHED) &&
                   event_patched(k, Role::Client, 11, PW_REVEALED) &&
                   event_patched(k, Role::Client, 11, PW_GIVEN) &&
                   !event_patched(k, Role::Client, 11, PW_END) &&
                   !event_patched(k, Role::Client, 11, 0xFF),
               "a pickup's way of 0-2 accepted, 3 and 255 refused");
        // EV_UPGRADE (offsets: kind 5, sub 6) and its answer (level 7).
        GameEvent u = good_event(EV_UPGRADE);
        EXPECT(!event_patched(u, Role::Client, 5, 0) &&
                   !event_patched(u, Role::Client, 5, UPG_END) &&
                   event_patched(u, Role::Client, 6, 0) &&
                   !event_patched(u, Role::Client, 6, kPowerUps),
               "an upgrade's kind of 0 or past the last, a move of 10 or more, refused");
        u.kind = UPG_TAUGHT;
        u.sub = 0;
        EXPECT(event_patched(u, Role::Client, 6, kMoves - 1) &&
                   !event_patched(u, Role::Client, 6, kMoves),
               "a taught move of 8 or more (health is bought, never taught) refused");
        GameEvent a = good_event(EV_UPGRADE_ACK);
        EXPECT(event_patched(a, Role::Host, 7, GR_YOURS) && !event_patched(a, Role::Host, 7, 0) &&
                   !event_patched(a, Role::Host, 7, GR_END),
               "an upgrade's answer of yes or no only");
    }
    // Protocol 9, EV_BOSS (offsets: type 0, scene 1, table 5, id 9, kind 11,
    // stage 12, camera 13, state 14, health 16, attack 18).
    {
        GameEvent b = good_event(EV_BOSS);
        EXPECT(event_patched(b, Role::Host, 11, BOSS_FROGFISH) && !event_patched(b, Role::Host, 11, 0) &&
                   !event_patched(b, Role::Host, 11, BOSS_END),
               "a boss's kind of 0 or past the last refused");
        EXPECT(event_patched(b, Role::Host, 12, kBossStageMax) && !event_patched(b, Role::Host, 12, kBossStageMax + 1) &&
                   event_patched(b, Role::Host, 13, kBossCamMax) && !event_patched(b, Role::Host, 13, kBossCamMax + 1),
               "a boss's stage past 15 or camera state past 31 refused");
        EXPECT(!event_patched(b, Role::Host, 15, 0x80) && !event_patched(b, Role::Host, 17, 0x80) &&
                   !event_patched(b, Role::Host, 19, 0x80),
               "a boss's negative state, health or attack refused");
        EXPECT(!event_patched(b, Role::Host, 17, 0x04) && !event_patched(b, Role::Host, 18, 64) &&
                   event_patched(b, Role::Host, 18, 63) && !event_patched(b, Role::Host, 15, 0x20),
               "a boss's health past 999, attack past 63 or state past 0x1FFF refused");
        b.id = kMaxNetIds;
        EXPECT(encode_events(&b, 1, Role::Host, buf, sizeof buf) == 0, "a boss's net ID past the table refused");
    }
    // The message: 1-8 events, ids from `first` on (across the wrap), no
    // trailing bytes, nothing cut short.
    {
        GameEvent many[kMaxEventsPerMsg + 1];
        for (unsigned i = 0; i <= kMaxEventsPerMsg; i++) many[i] = good_event(i % 2 ? EV_PICKUP : EV_HIT);
        size_t n = encode_events(many, kMaxEventsPerMsg, Role::Client, buf, sizeof buf, 0xFFFE);
        bool ok = n > 0 && parse_payload(buf, n, Role::Client, &q) && q.ev_count == kMaxEventsPerMsg &&
                  q.ev[0].rel_id == 0xFFFE && q.ev[1].rel_id == 0xFFFF && q.ev[2].rel_id == 0;
        EXPECT(ok, "8 events of mixed types, ids across the wrap");
        EXPECT(n == event_msg_size(many, kMaxEventsPerMsg), "event_msg_size matches the encoder (%zu)", n);
        EXPECT(encode_events(many, kMaxEventsPerMsg + 1, Role::Client, buf, sizeof buf) == 0 &&
                   encode_events(many, 0, Role::Client, buf, sizeof buf) == 0,
               "9 events, or none, refused (encoder)");
        n = encode_events(many, 2, Role::Client, buf, sizeof buf);
        uint8_t m[kMaxPayload];
        memcpy(m, buf, n);
        m[5] = 3; // says 3 events, carries 2
        EXPECT(!parse_payload(m, n, Role::Client, &q), "an event count past what is there refused");
        memcpy(m, buf, n);
        m[5] = 0;
        EXPECT(!parse_payload(m, n, Role::Client, &q), "an event count of 0 refused");
        memcpy(m, buf, n);
        m[5] = 9;
        EXPECT(!parse_payload(m, n, Role::Client, &q), "an event count of 9 refused");
        memcpy(m, buf, n);
        m[n] = 0;
        m[1] = (uint8_t)(m[1] + 1);
        EXPECT(!parse_payload(m, n + 1, Role::Client, &q), "a trailing byte after the events refused");
        EXPECT(!parse_payload(buf, n - 1, Role::Client, &q), "an event cut short refused");
    }
    // REL_ACK from either side; order among the messages.
    {
        static Payload p;
        payload_clear(&p);
        p.has = HAS_PSTATE | HAS_EVENT | HAS_REL_ACK | HAS_SNAP_ACK | HAS_PING;
        p.pstate = good_state(1);
        p.ev_first = 3;
        p.ev_count = 1;
        p.ev[0] = good_event(EV_HIT);
        p.rel_ack = 0xFFFF;
        p.snap_ack = 4;
        p.ping = 5;
        size_t n = encode_payload(p, Role::Client, buf, sizeof buf);
        EXPECT(n > 0 && parse_payload(buf, n, Role::Client, &q) && q.rel_ack == 0xFFFF && q.ev_count == 1 &&
                   q.snap_ack == 4,
               "a guest's datagram with a state, an event, its acknowledgements and a ping");
        uint8_t ra[5] = { MSG_REL_ACK, 2, 0, 1, 0 }, ra3[6] = { MSG_REL_ACK, 3, 0, 1, 0, 0 };
        EXPECT(parse_payload(ra, 5, Role::Host, &q) && q.rel_ack == 1 && parse_payload(ra, 5, Role::Client, &q) &&
                   !parse_payload(ra3, 6, Role::Host, &q),
               "REL_ACK from either side; 3 bytes refused");
        uint8_t order[12] = { MSG_REL_ACK, 2, 0, 1, 0, MSG_PICKUPS, 0, 0, 0, 0, 0, 0 };
        uint8_t back[10] = { MSG_SNAP_ACK, 4, 0, 1, 0, 0, 0, MSG_REL_ACK, 2, 0 };
        EXPECT(!parse_payload(back, 10, Role::Client, &q), "REL_ACK after SNAP_ACK refused (ascending types)");
        (void)order;
    }
    // PICKUPS: host only, a count of 1-1024, its bits, nothing past the count.
    {
        static Payload p;
        payload_clear(&p);
        p.has = HAS_PICKUPS;
        p.pick.scene = kBB02;
        p.pick.table = 0xAFD40CA7;
        p.pick.count = 384;
        p.pick.bits[0] = 0x81;
        p.pick.bits[47] = 0x80;
        size_t n = encode_payload(p, Role::Host, buf, sizeof buf);
        uint8_t again[kMaxPayload];
        p.pick.avail[0] = 0x7E;
        p.pick.avail[47] = 0x01;
        size_t n7 = encode_payload(p, Role::Host, buf, sizeof buf);
        EXPECT(n7 == n && n == pickups_msg_size(384) && n == 3 + 10 + 2 * 48 &&
                   parse_payload(buf, n, Role::Host, &q) && q.pick.count == 384 &&
                   q.pick.bits[0] == 0x81 && q.pick.bits[47] == 0x80 && q.pick.avail[0] == 0x7E &&
                   q.pick.avail[47] == 0x01 &&
                   encode_payload(q, Role::Host, again, sizeof again) == n &&
                   !memcmp(again, buf, n),
               "PICKUPS round trip (384 pickups, 48 bytes of taken and 48 of there to be taken)");
        p.pick.avail[0] = 0xFF; // pickup 0: taken and there
        EXPECT(encode_payload(p, Role::Host, buf, sizeof buf) == 0,
               "a pickup both taken and there refused (encoder)");
        n = encode_payload(p, Role::Host, buf, sizeof buf);
        p.pick.avail[0] = 0x7E;
        n = encode_payload(p, Role::Host, buf, sizeof buf);
        {
            uint8_t m[kMaxPayload];
            memcpy(m, buf, n);
            m[3 + 10 + 48] |= 0x01; // avail bit 0 over taken bit 0
            EXPECT(!parse_payload(m, n, Role::Host, &q),
                   "a pickup both taken and there refused (parser)");
        }
        EXPECT(!parse_payload(buf, n, Role::Client, &q) && encode_payload(p, Role::Client, buf, sizeof buf) == 0,
               "PICKUPS from the guest refused (parser and encoder)");
        p.pick.count = 381; // bit 383 is past it
        EXPECT(encode_payload(p, Role::Host, buf, sizeof buf) == 0, "a bit past the count refused (encoder)");
        p.pick.bits[47] = 0x10; // bit 380: inside
        p.pick.avail[47] = 0x01;
        n = encode_payload(p, Role::Host, buf, sizeof buf);
        EXPECT(n == 3 + 10 + 2 * 48 && parse_payload(buf, n, Role::Host, &q),
               "a count that is not a multiple of 8");
        uint8_t m[kMaxPayload];
        memcpy(m, buf, n);
        m[n - 1] |= 0x20; // bit 381 of the second array, past the count
        EXPECT(!parse_payload(m, n, Role::Host, &q), "a bit past the count refused (parser)");
        memcpy(m, buf, n);
        m[3 + 10 + 47] |= 0x20; // bit 381 of the first
        EXPECT(!parse_payload(m, n, Role::Host, &q), "a taken bit past the count refused (parser)");
        memcpy(m, buf, n);
        m[11] = 0;
        m[12] = 0; // a count of 0
        EXPECT(!parse_payload(m, n, Role::Host, &q), "a count of 0 refused");
        memcpy(m, buf, n);
        m[11] = 0x01;
        m[12] = 0x04; // 1025
        EXPECT(!parse_payload(m, n, Role::Host, &q), "a count past 1024 refused");
        EXPECT(!parse_payload(buf, n - 1, Role::Host, &q), "PICKUPS cut short refused");
        p.pick.count = kMaxPickups;
        memset(p.pick.bits, 0xFF, sizeof p.pick.bits);
        memset(p.pick.avail, 0, sizeof p.pick.avail);
        n = encode_payload(p, Role::Host, buf, sizeof buf);
        EXPECT(n == pickups_msg_size(kMaxPickups) && parse_payload(buf, n, Role::Host, &q) &&
                   q.pick.bits[127] == 0xFF,
               "1024 pickups, all taken");
    }
    // PROGRESS (protocol 7): host only, 20 bytes, each field in its range.
    {
        static Payload p;
        payload_clear(&p);
        p.has = HAS_PROGRESS;
        p.prog.tokens = kMaxTokens;
        p.prog.shiny = kMaxShiny;
        p.prog.range = kMaxRangeIdx;
        p.prog.unused = 4;
        p.prog.spent = -(int8_t)kMaxRange;
        for (unsigned i = 0; i < kPowerUps; i++)
            p.prog.powerup[i] = i < kMoves ? 2 : 3;
        p.prog.chests = kMaxChests;
        size_t n = encode_payload(p, Role::Host, buf, sizeof buf);
        uint8_t again[kMaxPayload];
        EXPECT(n == progress_msg_size() && n == 3 + kProgressSize &&
                   parse_payload(buf, n, Role::Host, &q) && q.prog.tokens == kMaxTokens &&
                   q.prog.shiny == kMaxShiny && q.prog.range == kMaxRangeIdx &&
                   q.prog.unused == 4 && q.prog.powerup[9] == 3 && q.prog.chests == kMaxChests &&
                   encode_payload(q, Role::Host, again, sizeof again) == n &&
                   !memcmp(again, buf, n),
               "PROGRESS round trip at every field's limit (%zu bytes)", n);
        EXPECT(!parse_payload(buf, n, Role::Client, &q) &&
                   encode_payload(p, Role::Client, buf, sizeof buf) == 0,
               "PROGRESS from the guest refused (parser and encoder)");
        n = encode_payload(p, Role::Host, buf, sizeof buf);
        // Each field one past its limit, through the parser (offsets in the
        // body: tokens 0, shiny 2, range 6, unused 7, spent 8, moves 9-18,
        // chests 19).
        struct Over
        {
            size_t at;
            uint8_t v;
            const char* what;
        };
        const Over over[] = { { 0, 0xE8, "tokens 1000" },
                              { 1, 0xFF, "tokens 65535" },
                              { 2, 0xA0, "shiny objects 100000" },
                              { 6, kMaxRangeIdx + 1, "upgrade points earned 15" },
                              { 7, kMaxRange + 1, "upgrade points left" },
                              { 8, kMaxRange + 1, "upgrade points spent" },
                              { 8, (uint8_t)(-(int)kMaxRange - 1),
                                "upgrade points spent below -15" },
                              { 9, 3, "a move's level 3" },
                              { 16, 3, "the last move's level 3" },
                              { 17, 4, "SpongeBob's health 4" },
                              { 18, 4, "Patrick's health 4" },
                              { 19, kMaxChests + 1, "chests" } };
        bool all = true;
        for (const Over& o : over)
        {
            uint8_t m[kMaxPayload];
            memcpy(m, buf, n);
            if (o.at == 0)
            {
                m[3] = 0xE8;
                m[4] = 0x03; // 1000
            }
            else if (o.at == 2)
            {
                m[5] = 0xA0;
                m[6] = 0x86;
                m[7] = 0x01;
                m[8] = 0x00; // 100000
            }
            else
            {
                m[3 + o.at] = o.v;
            }
            if (parse_payload(m, n, Role::Host, &q))
            {
                all = false;
                printf("  accepted: %s\n", o.what);
            }
        }
        EXPECT(all, "PROGRESS: every field past its range refused");
        uint8_t m[kMaxPayload];
        memcpy(m, buf, n);
        m[3 + 6] = 15;
        EXPECT(!parse_payload(m, n, Role::Host, &q),
               "PROGRESS upgrade points earned 15 refused (the game's table has 14)");
        memcpy(m, buf, n);
        m[1] = (uint8_t)(kProgressSize - 1);
        EXPECT(!parse_payload(m, n - 1, Role::Host, &q), "PROGRESS a byte short refused");
        memcpy(m, buf, n);
        m[1] = (uint8_t)(kProgressSize + 1);
        m[n] = 0;
        EXPECT(!parse_payload(m, n + 1, Role::Host, &q), "PROGRESS a byte long refused");
        p.prog.powerup[2] = 3;
        EXPECT(encode_payload(p, Role::Host, buf, sizeof buf) == 0,
               "PROGRESS past a range refused (encoder)");
    }
    // COUNTERS (protocol 7): host only; the table's counters (0-32) and text
    // boxes (0-8), not both none; entries ascending, each below the table's
    // counters, each value within +-9999; a text per box, 0-16.
    {
        static Payload p;
        payload_clear(&p);
        p.has = HAS_COUNTERS;
        p.cnt.scene = kBB02;
        p.cnt.table = 0xC7A16113;
        p.cnt.counters = kMaxCounters;
        p.cnt.boxes = kMaxTextBoxes;
        p.cnt.count = kMaxCounters;
        for (unsigned i = 0; i < kMaxCounters; i++)
        {
            p.cnt.e[i].index = (uint8_t)i;
            p.cnt.e[i].value = (int16_t)(i % 2 ? kCounterLimit : -(int)kCounterLimit);
        }
        for (unsigned i = 0; i < kMaxTextBoxes; i++)
            p.cnt.text[i] = (uint8_t)(i % 2 ? kMaxBoxTexts : 0);
        size_t n = encode_payload(p, Role::Host, buf, sizeof buf);
        uint8_t again[kMaxPayload];
        EXPECT(n == counters_msg_size(kMaxCounters, kMaxTextBoxes) && n == 3 + 11 + 3 * 32 + 8 &&
                   parse_payload(buf, n, Role::Host, &q) && q.cnt.count == kMaxCounters &&
                   q.cnt.e[31].index == 31 && q.cnt.e[31].value == (int16_t)kCounterLimit &&
                   q.cnt.e[0].value == -(int16_t)kCounterLimit && q.cnt.text[7] == kMaxBoxTexts &&
                   encode_payload(q, Role::Host, again, sizeof again) == n &&
                   !memcmp(again, buf, n),
               "COUNTERS round trip at every field's limit (%zu bytes)", n);
        EXPECT(!parse_payload(buf, n, Role::Client, &q) &&
                   encode_payload(p, Role::Client, buf, sizeof buf) == 0,
               "COUNTERS from the guest refused (parser and encoder)");
        // A small one: AM01's table (6 counters, 2 boxes), one count that
        // changed, the boxes' texts. Body offsets: scene 0, table 4, counters
        // 8, boxes 9, count 10, entries from 11 (index, value lo, value hi),
        // the boxes' texts after them.
        payload_clear(&p);
        p.has = HAS_COUNTERS;
        p.cnt.scene = kBB02;
        p.cnt.table = 0xC7A16113;
        p.cnt.counters = 6;
        p.cnt.boxes = 2;
        p.cnt.count = 2;
        p.cnt.e[0].index = 1;
        p.cnt.e[0].value = 7;
        p.cnt.e[1].index = 4;
        p.cnt.e[1].value = 29;
        p.cnt.text[0] = 3;
        n = encode_payload(p, Role::Host, buf, sizeof buf);
        EXPECT(n == counters_msg_size(2, 2) && parse_payload(buf, n, Role::Host, &q) &&
                   q.cnt.e[1].value == 29 && q.cnt.text[0] == 3 && q.cnt.text[1] == 0,
               "COUNTERS of two changed counts and two boxes (%zu bytes)", n);
        struct Bad
        {
            size_t at;
            uint8_t v;
            const char* what;
        };
        const Bad bad[] = { { 0, 'b', "a scene that is no level ID" },
                            { 8, kMaxCounters + 1, "counters 33" },
                            { 9, kMaxTextBoxes + 1, "text boxes 9" },
                            { 10, 7, "more entries than counters" },
                            { 11, 6, "an entry's index past the table" },
                            { 14, 1, "an index again (not ascending)" },
                            { 14, 0, "indices descending" },
                            { 12, 0x10, "a value 10000 (0x2710)" },
                            { 12, 0xF0, "a value -10000 (0xD8F0)" },
                            { 17, kMaxBoxTexts + 1, "a box's text 17" },
                            { 18, 0xFF, "the last box's text 255" } };
        bool all = true;
        for (const Bad& o : bad)
        {
            uint8_t m[kMaxPayload];
            memcpy(m, buf, n);
            m[3 + o.at] = o.v;
            if (o.at == 12 && o.v == 0x10)
                m[3 + 13] = 0x27; // 10000
            if (o.at == 12 && o.v == 0xF0)
                m[3 + 13] = 0xD8; // -10000
            if (parse_payload(m, n, Role::Host, &q))
            {
                all = false;
                printf("  accepted: %s\n", o.what);
            }
        }
        EXPECT(all, "COUNTERS: every field past its range refused");
        {
            uint8_t m[kMaxPayload];
            memcpy(m, buf, n);
            m[3 + 8] = 0;
            m[3 + 9] = 0;
            m[3 + 10] = 0;
            m[1] = (uint8_t)(kCountersHeaderSize); // no entries, no boxes
            EXPECT(!parse_payload(m, 3 + kCountersHeaderSize, Role::Host, &q),
                   "COUNTERS of no counters and no boxes refused");
            memcpy(m, buf, n);
            m[1] = (uint8_t)(n - 3 - 1);
            EXPECT(!parse_payload(m, n - 1, Role::Host, &q), "COUNTERS a byte short refused");
            memcpy(m, buf, n);
            m[1] = (uint8_t)(n - 3 + 1);
            m[n] = 0;
            EXPECT(!parse_payload(m, n + 1, Role::Host, &q), "COUNTERS a byte long refused");
        }
        static Payload w;
        w = p;
        w.cnt.e[1].value = (int16_t)(kCounterLimit + 1);
        EXPECT(encode_payload(w, Role::Host, buf, sizeof buf) == 0,
               "COUNTERS past a range refused (encoder)");
        w = p;
        w.cnt.text[2] = 1; // a third box in a table of two
        EXPECT(encode_payload(w, Role::Host, buf, sizeof buf) == 0,
               "COUNTERS with a text past its boxes refused (encoder)");
        // Merging what came before the game took it: the same table's
        // counts together, the newer's where both have one; another
        // table's replaces it.
        Counters a = p.cnt, b = p.cnt;
        b.count = 2;
        b.e[0].index = 0;
        b.e[0].value = 30;
        b.e[1].index = 4;
        b.e[1].value = 28;
        b.text[0] = 0;
        counters_merge(&a, b);
        EXPECT(a.count == 3 && a.e[0].index == 0 && a.e[0].value == 30 && a.e[1].index == 1 &&
                   a.e[1].value == 7 && a.e[2].index == 4 && a.e[2].value == 28 && a.text[0] == 0 &&
                   counters_valid(a),
               "COUNTERS merged: indices 0, 1, 4 with the newer's 28");
        b.table ^= 1;
        b.count = 1;
        counters_merge(&a, b);
        EXPECT(a.table == b.table && a.count == 1, "COUNTERS of another table replace the older");
    }
    // The largest datagram the host builds: a state, the snapshot budget less
    // what the events, PICKUPS, PROGRESS and COUNTERS take, 8 damage events,
    // its acknowledgement, PICKUPS of 1024, PROGRESS, the fullest COUNTERS, a
    // ping, a pong and a STALL.
    {
        static Payload p;
        payload_clear(&p);
        p.has = HAS_PSTATE | HAS_SNAPSHOT | HAS_EVENT | HAS_REL_ACK | HAS_PING | HAS_PONG |
                HAS_STALL | HAS_PICKUPS | HAS_PROGRESS | HAS_COUNTERS;
        p.pstate = good_state(0);
        p.ev_first = 1;
        p.ev_count = kMaxEventsPerMsg;
        for (unsigned i = 0; i < kMaxEventsPerMsg; i++) p.ev[i] = good_event(EV_DAMAGE);
        p.rel_ack = 1;
        p.ping = p.pong = 1;
        p.stall_ms = 1;
        p.pick.scene = kBB02;
        p.pick.count = kMaxPickups;
        p.cnt.scene = kBB02;
        p.cnt.counters = kMaxCounters;
        p.cnt.boxes = kMaxTextBoxes;
        p.cnt.count = kMaxCounters;
        for (unsigned i = 0; i < kMaxCounters; i++)
            p.cnt.e[i].index = (uint8_t)i;
        size_t reserve = event_msg_size(p.ev, kMaxEventsPerMsg) + pickups_msg_size(kMaxPickups) +
                         progress_msg_size() + counters_msg_size(kMaxCounters, kMaxTextBoxes);
        good_snapshot(&p.snap, 1);
        // Fill the snapshot up to the budget less the reserve, as the builder does.
        size_t body = snapshot_size(p.snap);
        while (p.snap.count < kMaxSnapEntries && body + snap_entry_size(SE_POS | SE_ROT | SE_ANIM | SE_FLAGS) +
                                                     reserve <= kSnapBodyMax)
        {
            SnapEntry& se = p.snap.e[p.snap.count];
            se = p.snap.e[0];
            se.id = (uint16_t)(p.snap.e[p.snap.count - 1].id + 1);
            se.mask = SE_POS | SE_ROT | SE_ANIM | SE_FLAGS;
            p.snap.count++;
            body += snap_entry_size(se.mask);
        }
        p.snap.things = kMaxNetIds;
        size_t n = encode_payload(p, Role::Host, buf, sizeof buf);
        EXPECT(n > 0 && n <= kMaxPayload, "the fullest host datagram fits (%zu bytes, %u entries)", n,
               (unsigned)p.snap.count);
    }
    // EV_HIT's direction as the host uses it: a unit vector whatever length
    // the claim's has (a guest cannot knock an enemy further with a longer
    // one), none for all zero.
    {
        struct DirCase
        {
            int16_t d[3];
            float want[3];
        };
        const float r3 = 0.57735027f;
        const DirCase cases[] = {
            { { 32767, 0, 0 }, { 1.0f, 0.0f, 0.0f } },
            { { 0, -32767, 0 }, { 0.0f, -1.0f, 0.0f } },
            { { 32767, 32767, 32767 }, { r3, r3, r3 } },
            { { -32767, 32767, -32767 }, { -r3, r3, -r3 } },
            { { 1, 0, 0 }, { 1.0f, 0.0f, 0.0f } },
            { { 3, 4, 0 }, { 0.6f, 0.8f, 0.0f } },
            { { 0, 0, -1 }, { 0.0f, 0.0f, -1.0f } },
        };
        for (const DirCase& c : cases)
        {
            float u[3];
            bool have = hit_dir_unit(c.d, u);
            float len = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
            bool near = true;
            for (int k = 0; k < 3; k++)
                if (fabsf(u[k] - c.want[k]) > 1e-5f) near = false;
            EXPECT(have && near && fabsf(len - 1.0f) < 1e-5f, "hit direction %d %d %d is unit length (%.6f)",
                   (int)c.d[0], (int)c.d[1], (int)c.d[2], (double)len);
        }
        const int16_t zero[3] = { 0, 0, 0 };
        float u[3] = { 9.0f, 9.0f, 9.0f };
        EXPECT(!hit_dir_unit(zero, u) && u[0] == 0.0f && u[1] == 0.0f && u[2] == 0.0f,
               "an all-zero hit direction is none");
        // Every valid direction the parser lets through gives a finite unit vector.
        Rng dr = { 0xD1Cull };
        int bad = 0;
        for (int i = 0; i < 20000; i++)
        {
            int16_t d[3];
            for (int k = 0; k < 3; k++)
            {
                int v = (int)(dr.next() % 65535u) - 32767; // -32767..32767 (the parser refuses -32768)
                d[k] = (int16_t)(i % 7 == 0 ? (v % 3) : v);
            }
            bool any = d[0] != 0 || d[1] != 0 || d[2] != 0;
            float v3[3];
            bool have = hit_dir_unit(d, v3);
            float len = sqrtf(v3[0] * v3[0] + v3[1] * v3[1] + v3[2] * v3[2]);
            if (have != any || (have && !(fabsf(len - 1.0f) < 1e-5f)) || (!have && len != 0.0f)) bad++;
        }
        EXPECT(bad == 0, "20000 random hit directions: unit length or none (%d bad)", bad);
    }
}

// ---------------------------------------------------------------------------
// 3. The reliable layer (M3's events)

static std::vector<Event> sDelivered;
static void deliver(void*, const Event& e) { sDelivered.push_back(e); }

// M4 (docs/MULTIPLAYER.md 7): the level change, the start together, the
// shows, the players' looks.
static void test_m4_events()
{
    printf("[2d] M4 events: level, ready, go, show, look\n");
    uint8_t buf[kMaxPayload];
    static Payload q;
    const uint8_t types[5] = { EV_LEVEL, EV_READY, EV_GO, EV_SHOW, EV_LOOK };
    const Role sides[5] = { Role::Host, Role::Client, Role::Host, Role::Host, Role::Client };
    const size_t sizes[5] = { 5, 5, 5, 13, 8 };
    for (int k = 0; k < 5; k++)
    {
        GameEvent e = good_event(types[k]);
        Role other = sides[k] == Role::Host ? Role::Client : Role::Host;
        size_t n = encode_events(&e, 1, sides[k], buf, sizeof buf);
        uint8_t again[kMaxPayload];
        bool ok = n == 3 + kEventHeaderSize + sizes[k] && event_size(types[k]) == sizes[k] &&
                  parse_payload(buf, n, sides[k], &q) && q.ev_count == 1 && q.ev[0].type == types[k] &&
                  q.ev[0].scene == e.scene && q.ev[0].id == e.id && q.ev[0].kind == e.kind &&
                  q.ev[0].level == e.level && q.ev[0].sub == e.sub && q.ev[0].table == e.table &&
                  encode_payload(q, sides[k], again, sizeof again) == n && !memcmp(again, buf, n);
        EXPECT(ok, "M4 event type %d round trip from its side (%zu bytes)", (int)types[k], n);
        if (types[k] == EV_LOOK)
        {
            EXPECT(parse_payload(buf, n, other, &q) && encode_events(&e, 1, other, buf, sizeof buf) == n,
                   "EV_LOOK from either side");
        }
        else
        {
            EXPECT(!parse_payload(buf, n, other, &q) && encode_events(&e, 1, other, buf, sizeof buf) == 0,
                   "M4 event type %d refused from the other side (parser and encoder)", (int)types[k]);
        }
        // Every one of them names a level: scene 0 or bad bytes refused (LOOK
        // may say 0: not in a level).
        GameEvent z = e;
        z.scene = 0;
        size_t zn = encode_events(&z, 1, sides[k], buf, sizeof buf);
        EXPECT(types[k] == EV_LOOK ? zn > 0 && parse_payload(buf, zn, sides[k], &q) : zn == 0,
               "M4 event type %d with scene 0 (%s)", (int)types[k], types[k] == EV_LOOK ? "accepted" : "refused");
        EXPECT(!event_patched(e, sides[k], 1, 'b') && !event_patched(e, sides[k], 2, '-'),
               "M4 event type %d with a bad scene byte refused", (int)types[k]);
    }
    // EV_SHOW's fields (offsets: type 0, scene 1, table 5, id 9, kind 11, level 12).
    {
        GameEvent s = good_event(EV_SHOW);
        EXPECT(!event_patched(s, Role::Host, 10, 0x01), "a show id of 256 or more refused");
        EXPECT(event_patched(s, Role::Host, 9, 0xFF), "show id 255 accepted");
        EXPECT(!event_patched(s, Role::Host, 11, 0) && !event_patched(s, Role::Host, 11, SHOW_END),
               "a show kind of 0 or past the last refused");
        EXPECT(!event_patched(s, Role::Host, 12, 0x04) && !event_patched(s, Role::Host, 12, 0x80),
               "a cutscene start's unknown flag bits refused");
        EXPECT(event_patched(s, Role::Host, 12, SHOWF_MASK), "a cutscene start with both flags accepted");
        GameEvent end = s;
        end.kind = SHOW_CUTSCENE_END;
        EXPECT(encode_events(&end, 1, Role::Host, buf, sizeof buf) == 0, "flags on a show's end refused (encoder)");
        end.level = 0;
        EXPECT(encode_events(&end, 1, Role::Host, buf, sizeof buf) > 0, "a show's end without flags accepted");
        EXPECT(!event_patched(end, Role::Host, 12, 1), "flags on a show's end refused (parser)");
        GameEvent fly = end;
        fly.kind = SHOW_FLY_START;
        EXPECT(encode_events(&fly, 1, Role::Host, buf, sizeof buf) > 0 && !event_patched(fly, Role::Host, 12, 2),
               "a fly-through's start: no flags");
        GameEvent big = s;
        big.id = kMaxShows;
        EXPECT(encode_events(&big, 1, Role::Host, buf, sizeof buf) == 0, "a show id past the table refused (encoder)");
    }
    // EV_LOOK's fields (offsets: type 0, scene 1, character 5, SpongeBob's costume 6, Patrick's 7).
    {
        GameEvent l = good_event(EV_LOOK);
        EXPECT(!event_patched(l, Role::Client, 5, CHAR_END) && !event_patched(l, Role::Client, 5, 0xFF),
               "a look's unknown character refused");
        EXPECT(!event_patched(l, Role::Client, 6, kCostumeChoices) && !event_patched(l, Role::Client, 7, kCostumeChoices),
               "a costume past the last refused");
        EXPECT(event_patched(l, Role::Client, 6, kCostumeChoices - 1) && event_patched(l, Role::Client, 5, CHAR_SPONGEBOB),
               "the last costume, and SpongeBob, accepted");
        GameEvent bad = l;
        bad.sub = kCostumeChoices;
        EXPECT(encode_events(&bad, 1, Role::Host, buf, sizeof buf) == 0, "a costume past the last refused (encoder)");
        size_t n = encode_events(&l, 1, Role::Client, buf, sizeof buf);
        EXPECT(n > 0 && !parse_payload(buf, n - 1, Role::Client, &q), "a look cut short refused");
    }
    // The receiver's look inbox: a backlog drained in one frame (a receiver
    // back from a stall) keeps the newest look, however many came; past the
    // rate they are counted, not dropped.
    {
        LookInbox in;
        look_inbox_reset(&in);
        const uint64_t t0 = 5000000000ull;
        unsigned overs = 0;
        bool changedAll = true;
        for (int i = 0; i < 20; i++)
        {
            GameEvent l = good_event(EV_LOOK);
            l.kind = (uint8_t)(i & 1);
            l.level = (uint8_t)(i % kCostumeChoices);
            l.sub = (uint8_t)((i / 2) % kCostumeChoices);
            bool over = false;
            changedAll = look_inbox_take(&in, l, t0, &over) && changedAll;
            overs += over ? 1 : 0;
        }
        EXPECT(in.known && in.character == 1 && in.costume[0] == 19 % kCostumeChoices &&
                   in.costume[1] == (19 / 2) % kCostumeChoices && changedAll,
               "20 looks drained at once: the newest is kept (character %d, costumes %d %d)", (int)in.character,
               (int)in.costume[0], (int)in.costume[1]);
        EXPECT(overs == 20 - kLooksPerSecond && in.over == overs, "looks past %d a second are counted (%u)",
               (int)kLooksPerSecond, overs);
        GameEvent same = good_event(EV_LOOK);
        same.kind = in.character;
        same.level = in.costume[0];
        same.sub = in.costume[1];
        same.scene = in.scene;
        bool over = true;
        EXPECT(!look_inbox_take(&in, same, t0 + 1000000000ull, &over) && !over,
               "a second later the same look again: no change, and in the rate");
        GameEvent bad = same;
        bad.sub = kCostumeChoices;
        EXPECT(!look_inbox_take(&in, bad, t0 + 1000000001ull, NULL) && in.costume[1] == same.sub,
               "a look with a costume past the last is not taken");
        look_inbox_reset(&in);
        EXPECT(!in.known && in.over == 0, "a new session forgets the partner's look");
    }
    // The receiver's level signals (review): EV_READY and EV_GO are kept apart
    // from the inbox; an EV_LEVEL to a level forgets an EV_GO for that level
    // from an earlier visit (else the guest's next arrival there would start
    // without waiting), but not one for another level (still loading it).
    {
        LevelSignals ls;
        level_signals_reset(&ls);
        GameEvent go = good_event(EV_GO), lv = good_event(EV_LEVEL), rd = good_event(EV_READY);
        go.scene = lv.scene = rd.scene = 'D' << 24 | 'E' << 16 | '0' << 8 | '1';
        GameEvent other = lv;
        other.scene = 'B' << 24 | 'B' << 16 | '0' << 8 | '1';
        EXPECT(level_signals_take(&ls, go) && ls.go_scene == go.scene && level_signals_take(&ls, rd) &&
                   ls.ready_scene == rd.scene,
               "EV_GO and EV_READY are kept apart from the inbox");
        EXPECT(!level_signals_take(&ls, other) && ls.go_scene == go.scene,
               "an EV_LEVEL to another level keeps the EV_GO (and goes on to the inbox)");
        EXPECT(!level_signals_take(&ls, lv) && ls.go_scene == 0 && ls.ready_scene == rd.scene,
               "an EV_LEVEL to the EV_GO's own level forgets that stale EV_GO");
        EXPECT(level_signals_take(&ls, go) && ls.go_scene == go.scene, "the trip's own EV_GO after it is kept");
        GameEvent look = good_event(EV_LOOK), show = good_event(EV_SHOW);
        EXPECT(!level_signals_take(&ls, look) && !level_signals_take(&ls, show) && ls.go_scene == go.scene,
               "other events go on to the inbox and change nothing");
        level_signals_reset(&ls);
        EXPECT(ls.go_scene == 0 && ls.ready_scene == 0, "a new session forgets the level signals");
    }
    // A full message of M4 events among M3's, from the host.
    {
        GameEvent many[kMaxEventsPerMsg];
        const uint8_t hostTypes[4] = { EV_LEVEL, EV_GO, EV_SHOW, EV_DAMAGE };
        for (unsigned i = 0; i < kMaxEventsPerMsg; i++) many[i] = good_event(hostTypes[i % 4]);
        many[3].type = EV_LOOK;
        many[3] = good_event(EV_LOOK);
        size_t n = encode_events(many, kMaxEventsPerMsg, Role::Host, buf, sizeof buf);
        EXPECT(n > 0 && n == event_msg_size(many, kMaxEventsPerMsg) && parse_payload(buf, n, Role::Host, &q) &&
                   q.ev_count == kMaxEventsPerMsg && q.ev[3].type == EV_LOOK && q.ev[2].type == EV_SHOW,
               "8 mixed M3 and M4 events from the host (%zu bytes)", n);
    }
}

// ---------------------------------------------------------------------------
// 2e. Protocol 8: one build ID on every platform, the version code, join codes

static void test_identity()
{
    printf("[2e] protocol 9: the build ID, the version code, join codes\n");
    EXPECT(kProtoVersion == 9, "protocol version %u", (unsigned)kProtoVersion);

    // Known answers: SHA-256 of "tssm-coop build/2 <id> proto 9[ test-salt]".
    uint8_t id[kBuildIdSize];
    build_id("unknown", false, id);
    EXPECT(hex(id, kBuildIdSize) == "30c937fd183328ac8c500292025dcadc",
           "the ID of an unknown build: %s", hex(id, kBuildIdSize).c_str());
    build_id("unknown", true, id);
    EXPECT(hex(id, kBuildIdSize) == "2547473705a31a0d5ac246dd4a27f6ad", "the test salt's: %s",
           hex(id, kBuildIdSize).c_str());
    build_id("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", false, id);
    EXPECT(hex(id, kBuildIdSize) == "015c443e0ff0b73c931d040285ae12dd", "a fingerprint's: %s",
           hex(id, kBuildIdSize).c_str());
    char code[9];
    version_code(id, code);
    EXPECT(strcmp(code, "015c443e") == 0, "its version code: %s", code);

    // This build's: tools/coop_id.cmake's own SHA-256 of the same text gives
    // the code the Co-op tab shows.
    build_id(TSSM_COOP_ID, false, id);
    version_code(id, code);
    EXPECT(TSSM_COOP_PROTO == kProtoVersion && strcmp(code, TSSM_COOP_CODE) == 0,
           "this build's code %s, coop_id.cmake's %s (protocol %d)", code, TSSM_COOP_CODE,
           TSSM_COOP_PROTO);
    EXPECT(strlen(TSSM_COOP_ID) == 64 || strcmp(TSSM_COOP_ID, "unknown") == 0, "fingerprint %s",
           TSSM_COOP_ID);

    // Join codes: O, I and L are read as 0, 1 and 1.
    struct
    {
        const char* in;
        const char* out;
    } good[] = {
        { "K7QX-M2PD-9TRW", "K7QXM2PD9TRW" },
        { "k7qx m2pd\t9trw", "K7QXM2PD9TRW" },
        { "o1il-OIL0 lL", "0111011011" },
        { "BOIL", "B011" },
        { "uvwxyz", "UVWXYZ" },
        { "a_b.c!", "A_B.C!" }, // a typed password keeps its other characters
    };
    for (const auto& g : good)
    {
        char out[kMaxPassword + 1];
        bool ok = normalize_code(g.in, out, sizeof out);
        EXPECT(ok && strcmp(out, g.out) == 0, "\"%s\" -> \"%s\"", g.in, ok ? out : "(refused)");
    }
    char a[kMaxPassword + 1], b[kMaxPassword + 1];
    EXPECT(normalize_code("K7QX-M2PD-9TR0", a, sizeof a) &&
               normalize_code("k7qx m2pd 9tro", b, sizeof b) && strcmp(a, b) == 0,
           "an O typed for a 0 opens the game");
    char out[kMaxPassword + 1];
    EXPECT(!normalize_code("", out, sizeof out) && !normalize_code(" - -\t", out, sizeof out),
           "nothing but separators is refused");
    std::string longest(kMaxPassword, 'k');
    EXPECT(normalize_code(longest.c_str(), out, sizeof out) && strlen(out) == kMaxPassword,
           "63 characters");
    std::string over(kMaxPassword + 1, 'k');
    EXPECT(!normalize_code(over.c_str(), out, sizeof out), "64 characters are refused");
    char small[5];
    memset(small, 'x', sizeof small);
    bool zeroed = !normalize_code("ABCDEFG", small, sizeof small);
    for (size_t i = 0; i < sizeof small; i++)
        zeroed = zeroed && small[i] == 0;
    EXPECT(zeroed, "a code too long for the buffer is refused and nothing of it is left");
}

// ---------------------------------------------------------------------------
// 8. The home network: finding games (iNetDisc.h, docs/MULTIPLAYER.md 14.2)

static NetAddr A(const char* text)
{
    NetAddr a;
    memset(&a, 0, sizeof a);
    if (!addr_parse(text, 1, &a))
    {
        sFailures++;
        printf("  FAIL: the test's own address %s does not parse\n", text);
    }
    return a;
}

static Offer disc_offer(const char* name)
{
    Offer o;
    memset(&o, 0, sizeof o);
    o.disc_version = kDiscVersion;
    o.proto = kProtoVersion;
    for (int i = 0; i < 8; i++)
    {
        o.nonce[i] = (uint8_t)(0xA0 + i);
        o.build[i] = (uint8_t)(0x10 + i);
        o.files[i] = (uint8_t)(0x30 + i);
    }
    o.game_port = 2171;
    o.instance = 0xC0FFEE01;
    o.state = OFFER_WAITING;
    o.platform = PLAT_WINDOWS;
    o.scene = kBB02;
    snprintf(o.name, sizeof o.name, "%s", name);
    return o;
}

static size_t enc(const Offer& o, uint8_t* b)
{
    return encode_offer(b, 128, o);
}

static IfRecord rec(bool up, bool hw, bool loop, bool p2p, bool bc)
{
    IfRecord r;
    memset(&r, 0, sizeof r);
    r.up = up;
    r.hardware = hw;
    r.loopback = loop;
    r.point_to_point = p2p;
    r.broadcast = bc;
    return r;
}

static void rec_addr(IfRecord& r, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t prefix,
                     bool preferred = true)
{
    uint8_t ip[4] = { a, b, c, d };
    memcpy(r.addr[r.count].ip, ip, 4);
    r.addr[r.count].prefix = prefix;
    r.addr[r.count].preferred = preferred;
    r.count++;
}

static std::string if_text(const IfV4* l, size_t n)
{
    std::string s;
    for (size_t i = 0; i < n; i++)
    {
        char t[32];
        snprintf(t, sizeof t, "%s%u.%u.%u.%u/%u", i ? " " : "", l[i].ip[0], l[i].ip[1], l[i].ip[2],
                 l[i].ip[3], l[i].prefix);
        s += t;
    }
    return s;
}

static void test_discovery()
{
    printf(
        "[8] the home network: DISCOVER and OFFER, the responder, the finder, interfaces, the probe\n");
    uint8_t nonce[kDiscNonce] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t q[256], b[256];
    Discover dq;

    // DISCOVER: exactly 128 bytes, strict for version 1, the prefix only for a newer one.
    size_t qn = encode_discover(q, sizeof q, nonce);
    EXPECT(qn == kDiscoverSize && !memcmp(q, "TSDQ", 4) && q[4] == 1 && q[5] == 0 &&
               !memcmp(q + 6, nonce, 8),
           "DISCOVER is %zu bytes", qn);
    EXPECT(parse_discover(q, qn, &dq) && dq.version == 1 && !memcmp(dq.nonce, nonce, 8),
           "DISCOVER parses");
    for (size_t n : { (size_t)0, (size_t)14, (size_t)127, (size_t)129, (size_t)200 })
        EXPECT(!parse_discover(q, n, &dq), "a %zu-byte question is refused", n);
    memcpy(b, q, qn);
    b[0] = 'X';
    EXPECT(!parse_discover(b, qn, &dq), "wrong magic");
    memcpy(b, q, qn);
    b[4] = 0;
    EXPECT(!parse_discover(b, qn, &dq), "version 0");
    memcpy(b, q, qn);
    b[5] = 1;
    EXPECT(!parse_discover(b, qn, &dq), "version 1 with flags");
    for (size_t at : { (size_t)14, (size_t)70, (size_t)127 })
    {
        memcpy(b, q, qn);
        b[at] = 0x40;
        EXPECT(!parse_discover(b, qn, &dq), "version 1 with padding byte %zu set", at);
    }
    memcpy(b, q, qn);
    b[4] = 2;
    b[5] = 0xFF;
    b[100] = 9;
    EXPECT(parse_discover(b, qn, &dq) && dq.version == 2 && !memcmp(dq.nonce, nonce, 8),
           "a newer version's question is read by its prefix (so it is answered)");

    // OFFER: 45 to 60 bytes, strict and canonical.
    Offer o = disc_offer("H"), back;
    size_t on = enc(o, b);
    EXPECT(on == kOfferMin, "the shortest OFFER is %zu bytes", on);
    EXPECT(parse_offer(b, on, &back) == OFFER_V1 && enc(back, q) == on && !memcmp(q, b, on),
           "it re-encodes");
    o = disc_offer("Sixteen chars ok");
    on = enc(o, b);
    EXPECT(on == kOfferMax && on < kDiscoverSize,
           "the longest is %zu bytes, smaller than the question", on);
    EXPECT(parse_offer(b, on, &back) == OFFER_V1 && !strcmp(back.name, "Sixteen chars ok") &&
               back.game_port == 2171 && back.instance == 0xC0FFEE01 && back.scene == kBB02 &&
               back.platform == PLAT_WINDOWS && !memcmp(back.nonce, o.nonce, 8) &&
               !memcmp(back.build, o.build, 8) && !memcmp(back.files, o.files, 8),
           "every field comes back");
    EXPECT(!memcmp(b, "TSDR", 4) && b[4] == 1 && b[5] == kProtoVersion && b[6] == 0 &&
               !memcmp(b + 7, o.nonce, 8),
           "the fixed prefix: magic, disc_version, proto_version, nonce at 0, 4, 5, 7");
    o = disc_offer("Host");
    on = enc(o, b);
    EXPECT(parse_offer(b, on - 1, &back) == OFFER_BAD, "a short one");
    b[on] = 0;
    EXPECT(parse_offer(b, on + 1, &back) == OFFER_BAD, "a trailing byte");
    for (size_t n = kOfferPrefix; n < kOfferMin; n++)
        EXPECT(parse_offer(b, n, &back) == OFFER_BAD, "a version 1 offer cut to %zu bytes", n);
    struct
    {
        size_t at;
        uint8_t v;
        const char* what;
    } bad[] = {
        { 0, 'X', "magic" },
        { 4, 0, "disc_version 0" },
        { 31, 0xFF, "game port 1023" }, // with [32] below
        { 37, 2, "state 2" },
        { 39, 'b', "a lower-case scene" },
        { 43, 0, "name length 0" },
        { 43, 5, "name length past the end" },
        { 44, ' ', "a leading space" },
        { 45, '!', "a name character outside the rule" },
        { 46, 0, "a NUL inside the name" },
    };
    for (const auto& x : bad)
    {
        uint8_t c[128];
        memcpy(c, b, on);
        c[x.at] = x.v;
        if (x.at == 31)
            c[32] = 0x03;
        EXPECT(parse_offer(c, on, &back) == OFFER_BAD, "OFFER with %s is refused", x.what);
    }
    o = disc_offer("Host");
    o.instance = 0;
    EXPECT(enc(o, b) == 0, "the encoder refuses instance 0");
    o = disc_offer("Host");
    o.game_port = 1023;
    EXPECT(enc(o, b) == 0, "and a port below 1024");
    o = disc_offer("");
    EXPECT(enc(o, b) == 0, "and an empty name");
    o = disc_offer("Host");
    o.game_port = 65535;
    o.state = OFFER_HAS_PLAYER;
    o.scene = 0;
    on = enc(o, b);
    EXPECT(on && parse_offer(b, on, &back) == OFFER_V1 && back.state == 1 && back.scene == 0 &&
               back.game_port == 65535,
           "a full game between levels on port 65535");
    for (uint8_t p : { (uint8_t)0, (uint8_t)6, (uint8_t)200 })
    {
        o = disc_offer("Host");
        o.platform = p;
        on = enc(o, b);
        EXPECT(on && parse_offer(b, on, &back) == OFFER_V1 && back.platform == p &&
                   !strcmp(disc_platform_name(p), "other device"),
               "an unknown platform %u is kept as \"other device\"", p);
    }
    EXPECT(!strcmp(disc_platform_name(PLAT_WINDOWS), "Windows") &&
               !strcmp(disc_platform_name(PLAT_ANDROID), "Android") &&
               !strcmp(disc_platform_name(PLAT_SWITCH), "Switch") &&
               disc_this_platform() == PLAT_WINDOWS,
           "platform names, and this build says Windows");
    uint8_t newer[40] = { 'T', 'S', 'D', 'R', 2, 9, 0, 7, 7, 7, 7, 7, 7, 7, 7, 0xEE };
    EXPECT(parse_offer(newer, 16, &back) == OFFER_NEWER && back.disc_version == 2 &&
               back.proto == 9 && back.nonce[0] == 7,
           "a newer version's offer: its prefix only");
    EXPECT(parse_offer(newer, 14, &back) == OFFER_BAD, "shorter than the prefix");
    uint8_t big[128] = { 'T', 'S', 'D', 'R', 2 };
    EXPECT(parse_offer(big, 128, &back) == OFFER_BAD && parse_offer(big, 127, &back) == OFFER_NEWER,
           "never as large as a question, even a newer one");

    // Addresses.
    uint8_t ip[4];
    struct
    {
        const char* a;
        bool priv;
    } pr[] = { { "10.0.0.1", true },    { "172.16.5.4", true },    { "172.31.255.1", true },
               { "172.32.0.1", false }, { "192.168.0.3", true }, { "169.254.10.1", true },
               { "100.64.0.1", false }, { "203.0.113.7", false },  { "127.0.0.1", false },
               { "8.8.8.8", false },    { "192.169.0.1", false } };
    for (const auto& x : pr)
        EXPECT(v4_of(A(x.a), ip) && v4_private(ip) == x.priv, "%s private: %d", x.a, (int)x.priv);
    EXPECT(!v4_of(A("[2001:db8::1]:5"), ip), "IPv6 is not IPv4");
    IfV4 home = { { 192, 168, 0, 3 }, 24 };
    uint8_t bc[4];
    v4_broadcast(home, bc);
    EXPECT(bc[0] == 192 && bc[1] == 168 && bc[2] == 0 && bc[3] == 255, "the subnet broadcast");
    IfV4 wide = { { 10, 5, 0, 2 }, 16 };
    v4_broadcast(wide, bc);
    EXPECT(bc[2] == 255 && bc[3] == 255, "a /16's");
    // A question goes to both broadcasts on every network: a VPN app may take
    // the limited one into its tunnel.
    {
        NetAddr to[kAskTargets];
        size_t m = disc_ask_targets(home, kDiscPort, to);
        EXPECT(m == 2 && addr_equal(to[0], A("255.255.255.255:2172")) &&
                   addr_equal(to[1], A("192.168.0.255:2172")),
               "a question: the limited broadcast and the subnet's (%u targets)", (unsigned)m);
        m = disc_ask_targets(wide, 24850, to);
        EXPECT(m == 2 && addr_equal(to[0], A("255.255.255.255:24850")) &&
                   addr_equal(to[1], A("10.5.255.255:24850")),
               "a /16's question: both broadcasts, on the port asked");
    }
    v4_of(A("192.168.0.9"), ip);
    EXPECT(v4_in_subnet(ip, home) && !v4_in_subnet(ip, wide), "subnets");

    // The interface filter, over recorded adapter lists. A Windows PC's (as
    // Get-NetAdapter / Get-NetIPAddress show it): Wi-Fi in; the VPN adapter
    // (type 53, not hardware, disconnected, still holding 10.5.0.2/16) out;
    // the disconnected Ethernet port and the virtual adapters with only
    // 169.254 out; loopback out.
    {
        IfRecord pc[8];
        size_t n = 0;
        pc[n] = rec(false, false, false, true, false); // a VPN adapter
        rec_addr(pc[n], 10, 5, 0, 2, 16, false);
        rec_addr(pc[n++], 169, 254, 5, 254, 16, false);
        pc[n] = rec(true, true, false, false, true); // Wi-Fi
        rec_addr(pc[n++], 192, 168, 0, 3, 24);
        pc[n] = rec(false, true, false, false, true); // Ethernet, unplugged
        rec_addr(pc[n++], 169, 254, 223, 10, 16, false);
        pc[n] = rec(false, false, false, false, true); // Wi-Fi Direct virtual adapter
        rec_addr(pc[n++], 169, 254, 205, 25, 16, false);
        pc[n++] =
            rec(true, false, false, false, true); // WAN Miniport (IP): up, virtual, no address
        pc[n] = rec(true, false, true, false, false); // loopback
        rec_addr(pc[n++], 127, 0, 0, 1, 8);
        IfV4 out[kMaxIfs];
        size_t k = if_filter(pc, n, out, kMaxIfs);
        EXPECT(k == 1 && if_text(out, k) == "192.168.0.3/24", "a Windows PC: Wi-Fi only: %s",
               if_text(out, k).c_str());
        // The VPN connected: up, its address in use, still not hardware.
        pc[0].up = true;
        pc[0].addr[0].preferred = true;
        k = if_filter(pc, n, out, kMaxIfs);
        EXPECT(k == 1 && if_text(out, k) == "192.168.0.3/24", "with the VPN connected: %s",
               if_text(out, k).c_str());
        // Even a VPN that claimed to be hardware is a point-to-point link.
        pc[0].hardware = true;
        k = if_filter(pc, n, out, kMaxIfs);
        EXPECT(k == 1, "a point-to-point adapter never counts: %s", if_text(out, k).c_str());
    }
    {
        IfRecord r[16];
        size_t n = 0;
        r[n] = rec(true, true, false, false, true); // a cable between two devices: 169.254 only
        rec_addr(r[n++], 169, 254, 9, 9, 16);
        r[n] = rec(true, true, false, false, true); // Ethernet with a router and a stale 169.254
        rec_addr(r[n], 169, 254, 1, 1, 16);
        rec_addr(r[n++], 192, 168, 1, 5, 24);
        r[n] = rec(true, true, false, true, false); // tun0 (Android or Linux VPN)
        rec_addr(r[n++], 10, 8, 0, 2, 24);
        r[n] = rec(true, true, false, false, false); // no IFF_BROADCAST
        rec_addr(r[n++], 10, 9, 0, 2, 24);
        r[n] = rec(true, true, false, false, true); // a public address, /31, /32 and /7
        rec_addr(r[n], 203, 0, 113, 7, 24);
        rec_addr(r[n], 10, 1, 0, 1, 31);
        rec_addr(r[n], 10, 2, 0, 1, 32);
        rec_addr(r[n++], 10, 3, 0, 1, 7);
        r[n] = rec(true, true, false, false,
                   true); // a tentative address, and the network's own and broadcast
        rec_addr(r[n], 192, 168, 7, 7, 24, false);
        rec_addr(r[n], 192, 168, 8, 0, 24);
        rec_addr(r[n++], 192, 168, 9, 255, 24);
        r[n] = rec(true, true, false, false, true); // the same address twice
        rec_addr(r[n++], 192, 168, 1, 5, 24);
        IfV4 out[kMaxIfs];
        size_t k = if_filter(r, n, out, kMaxIfs);
        EXPECT(if_text(out, k) == "169.254.9.9/16 192.168.1.5/24", "Android / Linux style list: %s",
               if_text(out, k).c_str());
        // At most 8, however many adapters.
        IfRecord many[12];
        for (int i = 0; i < 12; i++)
        {
            many[i] = rec(true, true, false, false, true);
            rec_addr(many[i], 10, (uint8_t)i, 0, 2, 24);
        }
        k = if_filter(many, 12, out, kMaxIfs);
        EXPECT(k == kMaxIfs, "capped at %zu: %zu", (size_t)kMaxIfs, k);
    }
    {
        // This PC's own list, as the game reads it: only private addresses
        // with a usable prefix (printed for the record).
        IfV4 live[kMaxIfs];
        size_t k = if_list(live, kMaxIfs);
        bool ok = k <= kMaxIfs;
        for (size_t i = 0; i < k; i++)
            ok = ok && v4_private(live[i].ip) && live[i].prefix >= 8 && live[i].prefix <= 30;
        printf("  this PC's networks: %s\n", k ? if_text(live, k).c_str() : "(none)");
        EXPECT(ok, "every interface the game uses is private: %s", if_text(live, k).c_str());
    }

    // The last-host probe: private IPv4 only (127.x in the test mode too).
    NetAddr pa;
    EXPECT(disc_probe_target("192.168.0.23:2171", kDiscPort, false, &pa) &&
               addr_equal(pa, A("192.168.0.23:2172")),
           "a private address is asked on 2172");
    EXPECT(disc_probe_target("10.1.2.3", 3000, false, &pa) && pa.port == 3000, "without a port");
    for (const char* t :
         { "203.0.113.7:2171", "8.8.8.8", "[2001:db8::1]:2171", "[::ffff:192.168.0.2]:5", "",
           "nonsense", "255.255.255.255", "127.0.0.1:2171", "100.64.1.1" })
        EXPECT(!disc_probe_target(t, kDiscPort, false, &pa) ||
                   (strstr(t, "::ffff:") && v4_of(pa, ip) && v4_private(ip)),
               "no probe to \"%s\"", t);
    EXPECT(!disc_probe_target(NULL, kDiscPort, false, &pa), "no address");
    EXPECT(disc_probe_target("127.0.0.1:2171", 4000, true, &pa) &&
               addr_equal(pa, A("127.0.0.1:4000")),
           "127.x only in the test mode");

    // Who answers at all.
    EXPECT(disc_host_visible(true, true, false, false, false), "a network host with Visible on");
    EXPECT(!disc_host_visible(true, false, false, false, false), "Visible off");
    EXPECT(!disc_host_visible(false, true, false, false, false), "not hosting");
    EXPECT(!disc_host_visible(true, true, true, false, false),
           "a loopback host, outside the test mode");
    EXPECT(disc_host_visible(true, true, true, false, true), "a loopback host in the test mode");
    EXPECT(!disc_host_visible(true, true, true, true, true) &&
               !disc_host_visible(true, true, false, true, false),
           "local co-op's host, never");

    // Buckets.
    {
        Bucket bk;
        memset(&bk, 0, sizeof bk);
        int got = 0;
        for (int i = 0; i < 10; i++)
            got += bucket_take(&bk, 0, 2, 4);
        EXPECT(got == 4, "a burst of 4 at time 0: %d", got);
        EXPECT(!bucket_take(&bk, 400 * kNsPerMs, 2, 4) && bucket_take(&bk, 500 * kNsPerMs, 2, 4),
               "then 2 a second");
        // Calls closer together than one token's time still add up.
        for (uint32_t rate : { 20u, 64u })
        {
            memset(&bk, 0, sizeof bk);
            got = 0;
            for (uint64_t t = 0; t < kNsPerSec; t += 10 * 1000)
                got += bucket_take(&bk, t, rate, rate);
            EXPECT(got >= (int)(2 * rate) - 1 && got <= (int)(2 * rate),
                   "calls 10 us apart for 1 s at %u a second: %d (burst + rate)", rate, got);
        }
    }

    // The responder: who gets an answer, how often, and how large.
    Responder r;
    memset(&r, 0, sizeof r);
    r.offer = disc_offer("Host");
    r.ifs[0] = home;
    r.ifs_count = 1;
    uint64_t now = 1000 * kNsPerSec;
    qn = encode_discover(q, sizeof q, nonce);
    size_t an = r.on_question(A("192.168.0.50:50000"), q, qn, now, b, sizeof b);
    EXPECT(an > 0 && an < qn && parse_offer(b, an, &back) == OFFER_V1 &&
               !memcmp(back.nonce, nonce, 8) && !strcmp(back.name, "Host"),
           "a neighbour's question is answered (%zu bytes) with its nonce", an);
    uint8_t dq2[kDiscoverSize];
    memcpy(dq2, q, qn);
    dq2[4] = 3;
    dq2[5] = 0x55;
    dq2[90] = 1;
    an = r.on_question(A("192.168.0.51:50000"), dq2, qn, now, b, sizeof b);
    EXPECT(an > 0 && parse_offer(b, an, &back) == OFFER_V1,
           "a newer joiner's question gets this version's answer");
    struct
    {
        const char* from;
        const char* what;
    } silent[] = { { "203.0.113.5:50000", "the internet" },
                   { "10.5.0.9:50000", "a private address on no network of this device (a VPN's)" },
                   { "192.168.1.9:50000", "another private subnet" },
                   { "192.168.0.50:1023", "a source port below 1024" },
                   { "192.168.0.255:50000", "the subnet's broadcast address" },
                   { "192.168.0.0:50000", "the subnet's network address" },
                   { "192.168.0.3:50000", "this device's own address" },
                   { "127.0.0.1:50000", "loopback, outside the test mode" },
                   { "[fe80::1]:50000", "IPv6" },
                   { "[::ffff:cb00:7105]:50000", "a mapped internet address" } };
    for (const auto& x : silent)
        EXPECT(r.on_question(A(x.from), q, qn, now, b, sizeof b) == 0, "silence for %s", x.what);
    // Malformed questions: silence, and no table entry for their source.
    {
        uint64_t used = 0;
        for (const auto& s : r.src)
            used += s.used;
        uint8_t m[kDiscoverSize + 8];
        size_t bad_n = 0;
        for (int k = 0; k < 7; k++)
        {
            memcpy(m, q, qn);
            size_t mn = qn;
            if (k == 0)
                mn = 127;
            if (k == 1)
                mn = 129;
            if (k == 2)
                m[1] = 'X';
            if (k == 3)
                m[4] = 0;
            if (k == 4)
                m[5] = 2;
            if (k == 5)
                m[127] = 1;
            if (k == 6)
                mn = 1;
            bad_n += r.on_question(A("192.168.0.77:50000"), m, mn, now, b, sizeof b) != 0;
        }
        uint64_t after = 0;
        for (const auto& s : r.src)
            after += s.used;
        EXPECT(bad_n == 0 && after == used,
               "malformed questions: silence (%zu answered), no rate-table work", bad_n);
    }
    // One address's flood: at most a burst of 4, then 2 a second.
    {
        Responder f = r;
        f.reset();
        f.ifs[0] = home;
        f.ifs_count = 1;
        int got = 0;
        for (int i = 0; i < 100; i++)
        {
            size_t n = f.on_question(A("192.168.0.60:50000"), q, qn,
                                     now + (uint64_t)i * 10 * kNsPerMs, b, sizeof b);
            EXPECT(n < qn, "never larger than the question");
            got += n > 0;
        }
        EXPECT(got >= 5 && got <= 6,
               "100 questions in a second from one address: %d answers (4 + 2 a second)", got);
        EXPECT(f.st.drop_rate == (uint64_t)(100 - got) && f.st.answered == (uint64_t)got,
               "counted: %llu dropped", (unsigned long long)f.st.drop_rate);
        // Another address is not held back by it.
        EXPECT(f.on_question(A("192.168.0.61:50000"), q, qn, now + kNsPerSec, b, sizeof b) > 0,
               "a second address still gets its answer");
    }
    // Everyone together: 20 a second.
    {
        Responder f = r;
        f.reset();
        f.ifs[0] = home;
        f.ifs_count = 1;
        int got = 0;
        for (int t = 0; t < 40; t++)
            for (int s = 0; s < 40; s++)
            {
                char a[32];
                snprintf(a, sizeof a, "192.168.0.%d:50000", 100 + s);
                got +=
                    f.on_question(A(a), q, qn, now + (uint64_t)t * 50 * kNsPerMs, b, sizeof b) > 0;
            }
        EXPECT(got >= 55 && got <= 60,
               "40 addresses for 2 s: %d answers (20 at once + 20 a second)", got);
        uint64_t used = 0;
        for (const auto& s : f.src)
            used += s.used;
        EXPECT(used == kResponderSources, "the table of %zu addresses, oldest replaced: %llu",
               (size_t)kResponderSources, (unsigned long long)used);
    }
    // The test mode: 127.x only.
    {
        Responder t = r;
        t.reset();
        t.loopback_test = true;
        EXPECT(t.on_question(A("127.0.0.1:50000"), q, qn, now, b, sizeof b) > 0 &&
                   t.on_question(A("192.168.0.50:50000"), q, qn, now, b, sizeof b) == 0,
               "the loopback test mode answers 127.x and nothing else");
    }

    // The finder.
    Finder fd;
    memset(&fd, 0, sizeof fd);
    memcpy(fd.build, o.build, 8);
    memcpy(fd.files, o.files, 8);
    fd.files_known = true;
    fd.disc_port = kDiscPort;
    fd.ifs[0] = home;
    fd.ifs_count = 1;
    uint8_t n1[8] = { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7 }; // disc_offer's
    fd.asked(n1);
    o = disc_offer("Host");
    on = enc(o, b);
    NetAddr src = A("192.168.0.23:2172");
    EXPECT(fd.on_answer(src, b, on, now) && fd.count == 1 && fd.list[0].kind == FOUND_READY &&
               addr_equal(fd.list[0].addr, A("192.168.0.23:2171")) &&
               !strcmp(fd.list[0].name, "Host"),
           "a game is listed at its address and game port, Ready");
    EXPECT(!fd.on_answer(src, b, on, now + kNsPerMs) && fd.count == 1,
           "the same answer again (over a second interface) is the same game");
    EXPECT(!fd.on_answer(A("192.168.0.23:2173"), b, on, now) &&
               !fd.on_answer(A("203.0.113.7:2172"), b, on, now) &&
               !fd.on_answer(A("[fe80::1]:2172"), b, on, now) &&
               !fd.on_answer(A("127.0.0.1:2172"), b, on, now),
           "not from another port, the internet, IPv6 or loopback");
    {
        Offer w = o;
        w.nonce[0] ^= 1;
        uint8_t wb[128];
        size_t wn = enc(w, wb);
        EXPECT(!fd.on_answer(A("192.168.0.24:2172"), wb, wn, now) && fd.st.drop_nonce == 1,
               "an answer to no question of this finder (wrong nonce) is dropped");
    }
    // Nonces: the last four questions only.
    {
        Finder g = fd;
        g.reset();
        uint8_t nn[5][8];
        for (int i = 0; i < 5; i++)
        {
            memset(nn[i], 0x50 + i, 8);
            g.asked(nn[i]);
        }
        Offer w = o;
        memcpy(w.nonce, nn[0], 8);
        uint8_t wb[128];
        size_t wn = enc(w, wb);
        EXPECT(!g.on_answer(src, wb, wn, now), "the fifth question back is forgotten");
        memcpy(w.nonce, nn[1], 8);
        wn = enc(w, wb);
        EXPECT(g.on_answer(src, wb, wn, now), "the fourth is still answered");
    }
    // What the list says.
    {
        Finder g = fd;
        g.reset();
        g.asked(n1);
        Offer v = o;
        v.instance = 2;
        v.build[0] ^= 1;
        size_t vn = enc(v, b);
        EXPECT(g.on_answer(A("192.168.0.30:2172"), b, vn, now) && g.list[0].kind == FOUND_VERSION,
               "another version");
        v = o;
        v.instance = 3;
        v.proto = kProtoVersion + 1;
        vn = enc(v, b);
        EXPECT(g.on_answer(A("192.168.0.31:2172"), b, vn, now) && g.list[1].kind == FOUND_VERSION,
               "another protocol");
        v = o;
        v.instance = 4;
        v.files[7] ^= 1;
        vn = enc(v, b);
        EXPECT(g.on_answer(A("192.168.0.32:2172"), b, vn, now) && g.list[2].kind == FOUND_FILES,
               "other game files");
        v = o;
        v.instance = 5;
        v.platform = 42;
        v.state = OFFER_HAS_PLAYER;
        vn = enc(v, b);
        EXPECT(g.on_answer(A("192.168.0.33:2172"), b, vn, now) && g.list[3].platform == 42 &&
                   g.list[3].state == OFFER_HAS_PLAYER &&
                   !strcmp(disc_platform_name(g.list[3].platform), "other device"),
               "an unknown device with a player: listed as \"other device\", \"has a player\"");
        uint8_t nb[24] = { 'T', 'S', 'D', 'R', 2, 9, 0 };
        memcpy(nb + 7, n1, 8);
        EXPECT(g.on_answer(A("192.168.0.34:2172"), nb, sizeof nb, now) && g.list[4].newer &&
                   g.list[4].kind == FOUND_VERSION && g.list[4].addr.port == 0 &&
                   g.list[4].name[0] == 0,
               "a newer version's host: \"different version\", its address only");
        // The tab's codes for a row ("yours xxxxxxxx, theirs yyyyyyyy"): the
        // host's own prefixes, none for a newer version.
        {
            Offer ov = o;
            ov.build[0] ^= 1;
            const uint8_t zero[kIdPrefix] = {};
            uint8_t id[kBuildIdSize] = {};
            memcpy(id, g.list[0].build, kIdPrefix);
            char code[9], want[9];
            version_code(id, code);
            memset(id, 0, sizeof id);
            memcpy(id, ov.build, kIdPrefix);
            version_code(id, want);
            EXPECT(!memcmp(g.list[0].build, ov.build, kIdPrefix) && !strcmp(code, want),
                   "a row keeps the host's version prefix (its code is the host's version code)");
            Offer of = o;
            of.files[7] ^= 1;
            EXPECT(!memcmp(g.list[2].files, of.files, kIdPrefix) &&
                       !memcmp(g.list[2].build, o.build, kIdPrefix),
                   "a row keeps the host's game-files prefix");
            EXPECT(!memcmp(g.list[4].build, zero, kIdPrefix) &&
                       !memcmp(g.list[4].files, zero, kIdPrefix),
                   "a newer version's row has no prefixes");
        }
        v = o;
        v.instance = 6;
        vn = enc(v, b);
        EXPECT(g.on_answer(A("192.168.0.33:2172"), b, vn, now) && g.count == 6,
               "another hosting at a known address is another row");
        // Two more fill the list; a ninth is dropped.
        for (int i = 0; i < 3; i++)
        {
            v = o;
            v.instance = 10 + (uint32_t)i;
            vn = enc(v, b);
            char a[32];
            snprintf(a, sizeof a, "192.168.0.%d:2172", 40 + i);
            bool added = g.on_answer(A(a), b, vn, now);
            EXPECT(added == (i < 2), "game %d: %s", 7 + i, added ? "listed" : "dropped");
        }
        EXPECT(g.count == kMaxFound && g.st.drop_full == 1, "a ninth game is dropped: %u listed",
               g.count);
        {
            // One address changing its instance gets two rows; a real host
            // after it still gets in.
            Finder h = fd;
            h.reset();
            h.asked(n1);
            int rows = 0;
            for (uint32_t i = 0; i < 20; i++)
            {
                v = o;
                v.instance = 100 + i;
                vn = enc(v, b);
                rows += h.on_answer(A("192.168.0.90:2172"), b, vn, now);
            }
            v = o;
            v.instance = 7;
            vn = enc(v, b);
            EXPECT(rows == kFoundPerAddress && h.on_answer(A("192.168.0.91:2172"), b, vn, now) &&
                       h.count == kFoundPerAddress + 1,
                   "one address fills at most %d rows (%d), another still listed",
                   (int)kFoundPerAddress, rows);
        }
        // An answer refreshes its row; the others go 4 s after their last.
        v = o;
        v.instance = 4;
        v.files[7] ^= 1;
        vn = enc(v, b);
        EXPECT(!g.on_answer(A("192.168.0.32:2172"), b, vn, now + 3 * kNsPerSec),
               "a refresh changes nothing shown");
        EXPECT(!g.expire(now + 3900 * kNsPerMs) && g.count == kMaxFound, "kept under 4 s");
        EXPECT(g.expire(now + 4 * kNsPerSec) && g.count == 1 && g.list[0].instance == 4,
               "the others gone at 4 s, the refreshed one kept: %u left", g.count);
        EXPECT(g.expire(now + 7 * kNsPerSec) && g.count == 0, "and it 4 s after its last answer");
    }
    {
        Finder g = fd;
        g.reset();
        g.asked(n1);
        g.files_known = false;
        Offer v = o;
        v.files[0] ^= 1;
        size_t vn = enc(v, b);
        EXPECT(g.on_answer(src, b, vn, now) && g.list[0].kind == FOUND_READY,
               "this game's files unknown: not called different");
        // At most 64 answers handled a second.
        int handled = 0;
        for (int i = 0; i < 100; i++)
        {
            uint64_t before = g.st.drop_rate;
            g.on_answer(src, b, vn, now + 1000);
            handled += g.st.drop_rate == before;
        }
        EXPECT(handled == 63 && g.st.drop_rate == 37,
               "100 answers at once: %d handled (64 a second)", handled + 1);
        // Strangers do not use up the allowance.
        Finder s = fd;
        s.reset();
        s.asked(n1);
        for (int i = 0; i < 200; i++)
            s.on_answer(A("203.0.113.7:2172"), b, vn, now);
        EXPECT(s.st.drop_rate == 0 && s.on_answer(src, b, vn, now),
               "answers from the internet are dropped before the rate");
    }
    {
        Finder t = fd;
        t.reset();
        t.loopback_test = true;
        t.asked(n1);
        EXPECT(t.on_answer(A("127.0.0.1:2172"), b, enc(o, b), now) &&
                   !t.on_answer(A("192.168.0.23:2172"), b, on, now),
               "the loopback test mode lists 127.x and nothing else");
    }

    // The address kept after a join: the parsed numbers written out again.
    {
        char t[64];
        NetAddr x = A("192.168.0.23:2171"), y;
        addr_format(x, t, sizeof t);
        EXPECT(!strcmp(t, "192.168.0.23:2171") && addr_parse(t, 1, &y) && addr_equal(x, y),
               "saved as %s", t);
        x = A("[2001:db8::7]:2171");
        addr_format(x, t, sizeof t);
        EXPECT(addr_parse(t, 1, &y) && addr_equal(x, y), "and an IPv6 one as %s", t);
    }

    // Over real sockets on 127.0.0.1: a responder and a finder, the
    // exclusive port, and the wait over several sockets.
    {
        const uint8_t lo[4] = { 127, 0, 0, 1 };
        UdpSocket rs, rs2, fs;
        EXPECT(udp_open_v4(&rs, lo, 0, false, true), "the responder's socket");
        uint16_t rport = 0;
        udp_local_port(rs, &rport);
        EXPECT(!udp_open_v4(&rs2, lo, rport, false, true), "the discovery port is exclusive");
        EXPECT(udp_open_v4(&fs, lo, 0, true, false), "the finder's socket, with broadcast");
        Responder rr;
        memset(&rr, 0, sizeof rr);
        rr.offer = disc_offer("LoopHost");
        rr.loopback_test = true;
        Finder ff;
        memset(&ff, 0, sizeof ff);
        memcpy(ff.build, rr.offer.build, 8);
        memcpy(ff.files, rr.offer.files, 8);
        ff.files_known = true;
        ff.disc_port = rport;
        ff.loopback_test = true;
        uint8_t nn[8] = { 9, 8, 7, 6, 5, 4, 3, 2 };
        ff.asked(nn);
        size_t m = encode_discover(q, sizeof q, nn);
        EXPECT(udp_send(fs, v4_addr(lo, rport), q, m), "a question sent");
        const UdpSocket* w[3] = { &fs, &rs, NULL };
        double t0 = wall_ms();
        bool got = false;
        while (!got && wall_ms() - t0 < 2000)
        {
            udp_wait_any(w, 3, 50);
            NetAddr from;
            uint8_t in[256], out[128];
            int n = udp_recv(rs, in, sizeof in, &from);
            if (n > 0)
            {
                size_t len = rr.on_question(from, in, (size_t)n, 2000 * kNsPerSec, out, sizeof out);
                EXPECT(len > 0 && udp_send(rs, from, out, len), "answered over UDP");
            }
            n = udp_recv(fs, in, sizeof in, &from);
            if (n > 0)
                got = ff.on_answer(from, in, (size_t)n, 2000 * kNsPerSec);
        }
        EXPECT(got && ff.count == 1 && !strcmp(ff.list[0].name, "LoopHost") &&
                   ff.list[0].kind == FOUND_READY,
               "the finder lists the loopback host (%.0f ms)", wall_ms() - t0);
        UdpSocket none;
        const UdpSocket* only[1] = { &none };
        t0 = wall_ms();
        EXPECT(!udp_wait_any(only, 1, 30) && wall_ms() - t0 >= 20,
               "a wait with no open socket sleeps");
        udp_close(&rs);
        udp_close(&fs);
    }
}

static void test_reliable(Rng& rng)
{
    printf("[3] reliable layer (M3's events): in memory, 30%% loss\n");
    ReliableOut out;
    ReliableIn in;
    sDelivered.clear();
    uint32_t pushed = 0;
    bool ok = true;
    for (int round = 0; round < 4000; round++)
    {
        for (int k = 0; k < 2; k++)
        {
            Event e;
            memset(&e, 0, sizeof e);
            e.type = EV_HIT;
            e.table = pushed; // the order they were pushed in
            if (pushed < 3000 && out.push(e)) pushed++;
        }
        // "send" every pending event, each lost with 30%, in a scrambled order
        int idx[8];
        int cnt = out.count < 8 ? out.count : 8;
        for (int i = 0; i < cnt; i++) idx[i] = i;
        for (int i = cnt - 1; i > 0; i--)
        {
            int j = (int)rng.below((uint32_t)i + 1);
            int t = idx[i];
            idx[i] = idx[j];
            idx[j] = t;
        }
        for (int i = 0; i < cnt; i++)
        {
            if (rng.unit() < 0.3) continue;
            ok &= in.offer(out.q[(out.base + idx[i]) & (kRelWindow - 1)], deliver, nullptr);
        }
        if (rng.unit() >= 0.3) ok &= out.ack(in.expect);
    }
    bool order = sDelivered.size() == pushed;
    for (size_t i = 0; order && i < sDelivered.size(); i++) order = sDelivered[i].table == i && sDelivered[i].rel_id == (uint16_t)i;
    EXPECT(ok, "no false violation");
    EXPECT(order, "every event once, in order (%zu of %u)", sDelivered.size(), pushed);
    ReliableOut o2;
    EXPECT(!o2.ack(1), "an ack for something never sent is a violation");
    ReliableIn i2;
    Event far;
    memset(&far, 0, sizeof far);
    far.rel_id = (uint16_t)kRelWindow;
    EXPECT(!i2.offer(far, deliver, nullptr), "an id beyond the window is a violation");
    // The most one EVENT message can complete (iCoop.cpp makes room for this
    // many before offering one): ids 1..63 waiting behind a gap, then a
    // message with the gap's id and the 7 after the window.
    {
        ReliableIn i3;
        Event ev;
        memset(&ev, 0, sizeof ev);
        bool all = true;
        for (unsigned id = 1; id < kRelWindow; id++)
        {
            ev.rel_id = (uint16_t)id;
            all = all && i3.offer(ev, deliver, nullptr);
        }
        sDelivered.clear();
        ev.rel_id = 0;
        all = all && i3.offer(ev, deliver, nullptr);
        for (unsigned k = 1; k < kMaxEventsPerMsg; k++)
        {
            ev.rel_id = (uint16_t)(kRelWindow - 1 + k);
            all = all && i3.offer(ev, deliver, nullptr);
        }
        bool inOrder = true;
        for (size_t k = 0; k < sDelivered.size(); k++)
            if (sDelivered[k].rel_id != (uint16_t)k) inOrder = false;
        EXPECT(all && inOrder && sDelivered.size() == kRelMaxDelivered && i3.expect == kRelMaxDelivered,
               "one hostile EVENT message completes at most kRelMaxDelivered (%zu, in order)", sDelivered.size());
        // And no more: the next id past those is beyond the window only when
        // nothing waits, so a further message's first event is the next one.
        ev.rel_id = (uint16_t)(i3.expect + kRelWindow);
        EXPECT(!i3.offer(ev, deliver, nullptr) && sDelivered.size() == kRelMaxDelivered,
               "past the window after it: a violation, nothing delivered");
    }
}

// ---------------------------------------------------------------------------
// Loopback endpoints

struct Receiver;

struct Endpoint
{
    UdpSocket sock;
    NetAddr addr;
    Receiver* recv = nullptr;               // where PLAYER_STATEs arriving here go
    std::vector<std::string>* logs = nullptr; // where the session's log lines go
    double loss = 0;
    Rng* rng = nullptr;
    uint64_t sent = 0, dropped = 0, bytes = 0;
    bool capture = false;
    std::vector<std::vector<uint8_t>> captured;

    bool open(uint8_t host = 1)
    {
        if (!udp_open(&sock, BindScope::Loopback, 0, host)) return false;
        uint16_t port = 0;
        udp_local_port(sock, &port);
        char ip[32];
        snprintf(ip, sizeof ip, "127.0.0.%u", host);
        return addr_parse(ip, port, &addr);
    }
    ~Endpoint() { udp_close(&sock); }
};

static void ep_send(void* ctx, const NetAddr& to, const uint8_t* d, size_t n)
{
    Endpoint* e = (Endpoint*)ctx;
    if (e->capture) e->captured.emplace_back(d, d + n);
    if (e->rng && e->rng->unit() < e->loss)
    {
        e->dropped++;
        return;
    }
    if (udp_send(e->sock, to, d, n))
    {
        e->sent++;
        e->bytes += n;
    }
}

template <class F> static int drain(Endpoint& e, F&& f, int wait_ms = 0)
{
    uint8_t buf[kRecvBuffer];
    int got = 0;
    if (wait_ms && !udp_wait(e.sock, wait_ms)) return 0;
    for (;;)
    {
        NetAddr from;
        int n = udp_recv(e.sock, buf, sizeof buf, &from);
        if (n == 0) break;
        if (n < 0) continue;
        f(from, buf, (size_t)n);
        got++;
    }
    return got;
}

static const uint8_t kBuildId[kBuildIdSize] = { 'p', 'o', 'r', 't', '/', 'c', 'o', 'o', 'p', ' ', 't', 'e', 's', 't' };

// What one side's game would do with PLAYER_STATEs: keep the newest.
struct Receiver
{
    uint32_t last_frame = 0;
    bool have = false;
    uint64_t accepted = 0, stale = 0;
    PlayerState latest;
    uint64_t snaps = 0, acks = 0, stalls = 0;
    uint32_t last_snap = 0, last_ack = 0;
    uint16_t last_stall = 0;
    uint8_t last_snap_count = 0;
    // M3: this side's reliable events, run as iCoop.cpp's I/O thread does.
    struct EvSide* ev = nullptr;
    uint64_t picks = 0;
    Pickups last_pick;
    uint64_t progs = 0; // protocol 7
    Progress last_prog;
    uint64_t cnts = 0;
    Counters last_cnt;
};

struct EvSide
{
    ReliableOut out;
    ReliableIn in;
    bool ack_due = false;
    uint32_t bad = 0;
    std::vector<GameEvent> got;
};

static void ev_deliver(void* ctx, const Event& e) { ((EvSide*)ctx)->got.push_back(e); }

static void ev_apply(EvSide& me, const Payload& p)
{
    if ((p.has & HAS_REL_ACK) && !me.out.ack(p.rel_ack)) me.bad++;
    if (p.has & HAS_EVENT)
    {
        me.ack_due = true;
        for (unsigned i = 0; i < p.ev_count; i++)
            if (!me.in.offer(p.ev[i], ev_deliver, &me)) me.bad++;
    }
}

// What the I/O thread adds to a datagram: how far the other side's events
// came, and this side's oldest unacknowledged ones (up to 8).
static void ev_add(EvSide& me, Payload& p)
{
    if (me.ack_due)
    {
        p.has |= HAS_REL_ACK;
        p.rel_ack = me.in.expect;
    }
    unsigned n = me.out.count < kMaxEventsPerMsg ? me.out.count : kMaxEventsPerMsg;
    if (n)
    {
        p.has |= HAS_EVENT;
        p.ev_first = me.out.base;
        p.ev_count = (uint8_t)n;
        for (unsigned i = 0; i < n; i++) p.ev[i] = me.out.q[(uint16_t)(me.out.base + i) & (kRelWindow - 1)];
    }
}

static void on_state(void* ctx, const Payload& p)
{
    Receiver* r = (Receiver*)ctx;
    if (r->ev) ev_apply(*r->ev, p);
    if (p.has & HAS_PICKUPS)
    {
        r->picks++;
        r->last_pick = p.pick;
    }
    if (p.has & HAS_PROGRESS)
    {
        r->progs++;
        r->last_prog = p.prog;
    }
    if (p.has & HAS_COUNTERS)
    {
        r->cnts++;
        r->last_cnt = p.cnt;
    }
    if (p.has & HAS_SNAPSHOT)
    {
        r->snaps++;
        if (frame_newer(p.snap.seq, r->last_snap)) r->last_snap = p.snap.seq;
        r->last_snap_count = p.snap.count;
    }
    if (p.has & HAS_SNAP_ACK)
    {
        r->acks++;
        if (frame_newer(p.snap_ack, r->last_ack)) r->last_ack = p.snap_ack;
    }
    if (p.has & HAS_STALL)
    {
        r->stalls++;
        r->last_stall = p.stall_ms;
    }
    if (!(p.has & HAS_PSTATE)) return;
    if (r->have && !frame_newer(p.pstate.frame, r->last_frame))
    {
        r->stale++;
        return;
    }
    r->have = true;
    r->last_frame = p.pstate.frame;
    r->latest = p.pstate;
    r->accepted++;
}

struct Rig
{
    Endpoint he, ce;
    Host host;
    Client client;
    Receiver hrecv, crecv;
    uint64_t now = 1000 * kNsPerSec;
    uint8_t assets[kHashSize];
    std::vector<std::string> logs;
};

static void ep_state(void* ctx, const Payload& p)
{
    Endpoint* e = (Endpoint*)ctx;
    if (e->recv) on_state(e->recv, p);
}

static void ep_log(void* ctx, const char* m)
{
    Endpoint* e = (Endpoint*)ctx;
    if (e->logs) e->logs->push_back(m);
}

static void step(Rig& r, Client& c, Endpoint& ce, int wait_ms)
{
    drain(r.he, [&](const NetAddr& f, const uint8_t* d, size_t n) { r.host.on_datagram(f, d, n, r.now); }, wait_ms);
    drain(ce, [&](const NetAddr& f, const uint8_t* d, size_t n) { c.on_datagram(f, d, n, r.now); }, wait_ms);
    r.host.update(r.now);
    c.update(r.now);
}

static bool host_start(Rig& r, uint8_t ip, const char* password, uint32_t iters)
{
    if (!r.he.open(ip)) return false;
    HostConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    memcpy(cfg.build_id, kBuildId, sizeof kBuildId);
    memcpy(cfg.asset_hash, r.assets, kHashSize);
    strcpy(cfg.name, "Host");
    cfg.password = password;
    cfg.iters = iters;
    r.he.recv = &r.hrecv;
    r.he.logs = &r.logs;
    NetIO hio = { &r.he, ep_send, ep_state, ep_log };
    return r.host.init(cfg, hio, r.now);
}

static bool connect(Rig& r, Client& c, Endpoint& ce, Receiver* recv, const char* pw, const char* name,
                    const uint8_t* build, const uint8_t* assets, int max_frames = 400)
{
    ce.recv = recv;
    NetIO io = { &ce, ep_send, ep_state, nullptr };
    if (!c.connect(r.he.addr, pw, name, build, assets, io, r.now)) return false;
    for (int i = 0; i < max_frames; i++)
    {
        step(r, c, ce, 2);
        if (c.state() == ClientState::Connected || c.state() == ClientState::Failed) break;
        r.now += 16666667;
    }
    // one more round so the host sees the confirming ping
    drain(r.he, [&](const NetAddr& f, const uint8_t* d, size_t n) { r.host.on_datagram(f, d, n, r.now); }, 2);
    return c.state() == ClientState::Connected;
}

// ---------------------------------------------------------------------------
// 4. Host and guest over loopback

static Rig* sRig;

static void test_session(Rng& rng)
{
    printf("[4] host + guest over UDP on 127.0.0.1, 10%% loss each way\n");
    static Rig r;
    sRig = &r;
    sha256("the same level files", 20, r.assets);
    double t0 = wall_ms();
    EXPECT(host_start(r, 1, "K7QX-M2PD-9TRW", kDefaultIters), "host start");
    double t1 = wall_ms();
    EXPECT(r.ce.open(3), "guest socket");
    r.ce.recv = &r.crecv;
    NetIO cio = { &r.ce, ep_send, ep_state, nullptr };
    EXPECT(r.client.connect(r.he.addr, "K7QX-M2PD-9TRW", "Brother", kBuildId, r.assets, cio, r.now), "connect starts");
    for (int i = 0; i < 400 && r.client.state() != ClientState::Connected && r.client.state() != ClientState::Failed; i++)
    {
        step(r, r.client, r.ce, 2);
        r.now += 16666667;
    }
    drain(r.he, [&](const NetAddr& f, const uint8_t* d, size_t n) { r.host.on_datagram(f, d, n, r.now); }, 2);
    double t2 = wall_ms();
    EXPECT(r.client.state() == ClientState::Connected, "connected");
    EXPECT(r.host.session().active && r.host.session().confirmed, "the host has the session");
    EXPECT(!strcmp(r.host.session().peer_name, "Brother") && !strcmp(r.client.session().peer_name, "Host"), "names");
    printf("  host start (PBKDF2) %.0f ms, join handshake %.0f ms wall\n", t1 - t0, t2 - t1);

    r.he.rng = r.ce.rng = &rng;
    r.he.loss = r.ce.loss = 0.10;
    uint64_t he0 = r.he.bytes, ce0 = r.ce.bytes;
    const uint32_t kFrames = 1800; // 30 s at 60 fps
    uint32_t hsent = 0, csent = 0;
    for (uint32_t f = 1; f <= kFrames + 120; f++)
    {
        if (f == kFrames + 1) r.he.loss = r.ce.loss = 0; // a quiet tail
        if (f % 2 == 0 && f <= kFrames)
        {
            // The host's state with a snapshot of 24 things, the guest's
            // with the acknowledgement of the newest snapshot it has (M2).
            static Payload p;
            payload_clear(&p);
            p.has = HAS_PSTATE | HAS_SNAPSHOT;
            p.pstate = good_state(0);
            p.pstate.frame = f;
            p.pstate.character = CHAR_SPONGEBOB;
            p.pstate.pos[0] = 100 * sinf(f / 60.0f);
            good_snapshot(&p.snap, 24);
            p.snap.seq = f / 2;
            p.snap.base =
                r.hrecv.last_ack && p.snap.seq - r.hrecv.last_ack <= kSnapBaseWindow ? r.hrecv.last_ack : 0;
            if (r.host.send(p, r.now)) hsent++;
            payload_clear(&p);
            p.has = HAS_PSTATE;
            p.pstate = good_state(1);
            p.pstate.frame = f;
            p.pstate.pos[2] = 50 * cosf(f / 60.0f);
            if (r.crecv.last_snap)
            {
                p.has |= HAS_SNAP_ACK;
                p.snap_ack = r.crecv.last_snap;
            }
            if (r.client.send(p, r.now)) csent++;
        }
        r.now += 16666667;
        // No wait: loopback delivers at once, and what is late is read next frame.
        step(r, r.client, r.ce, f % 16 == 0 ? 1 : 0);
    }
    printf("  states sent: host %u, guest %u; applied: by the guest %llu, by the host %llu\n", hsent, csent,
           (unsigned long long)r.crecv.accepted, (unsigned long long)r.hrecv.accepted);
    printf("  host->guest %.1f KB/s, guest->host %.1f KB/s; rtt %.1f ms (virtual clock)\n",
           (r.he.bytes - he0 + 0.0) / 1024 / ((kFrames + 120) / 60.0),
           (r.ce.bytes - ce0 + 0.0) / 1024 / ((kFrames + 120) / 60.0), r.client.session().rtt_ms);
    EXPECT(hsent >= kFrames / 2 - 1 && csent >= kFrames / 2 - 1, "every state was sent");
    EXPECT(r.crecv.accepted > hsent * 8 / 10, "most host states arrived (%llu of %u)",
           (unsigned long long)r.crecv.accepted, hsent);
    EXPECT(r.crecv.latest.slot == 0 && r.crecv.latest.frame == kFrames, "the guest has the newest host state (%u)",
           r.crecv.latest.frame);
    EXPECT(r.hrecv.accepted > csent * 8 / 10, "most guest states arrived (%llu of %u)",
           (unsigned long long)r.hrecv.accepted, csent);
    EXPECT(r.hrecv.latest.slot == 1 && r.hrecv.latest.frame == kFrames, "the host has the newest guest state (%u)",
           r.hrecv.latest.frame);
    EXPECT(r.crecv.snaps > hsent * 8 / 10 && r.crecv.last_snap == kFrames / 2 && r.crecv.last_snap_count == 24,
           "the guest has the host's snapshots, the newest included (%llu, newest %u)",
           (unsigned long long)r.crecv.snaps, r.crecv.last_snap);
    EXPECT(r.hrecv.acks > csent * 7 / 10 && r.hrecv.last_ack + 2 >= kFrames / 2,
           "the host has the guest's acknowledgements (%llu, newest %u)", (unsigned long long)r.hrecv.acks,
           r.hrecv.last_ack);
    EXPECT(r.client.state() == ClientState::Connected && r.host.session().active, "still connected");
    EXPECT(r.host.session().drop_parse == 0 && r.client.session().drop_parse == 0, "no protocol errors");
    EXPECT(r.client.session().ping_ms >= 0 && r.host.session().ping_ms >= 0, "both sides measured a ping");
    // A PONG that does not echo our newest PING is ignored.
    {
        Session& hs = r.host.session();
        double before = r.client.session().ping_ms;
        r.client.session().ping_sent = 7777;
        uint8_t pong[7] = { MSG_PONG, 4, 0, 1, 2, 3, 4 };
        uint8_t pkt[kMaxDatagram];
        size_t m = session_seal(hs, pong, 7, pkt, r.now);
        udp_send(r.he.sock, r.ce.addr, pkt, m);
        step(r, r.client, r.ce, 5);
        EXPECT(r.client.session().ping_ms == before && r.client.session().ping_sent == 7777,
               "a PONG that is not our PING's echo is ignored");
        r.client.session().ping_sent = 0;
    }

    // Newest wins: an old state, sent again later, is not applied.
    uint64_t stale0 = r.crecv.stale;
    Payload p;
    payload_clear(&p);
    p.has = HAS_PSTATE;
    p.pstate = good_state(0);
    p.pstate.frame = 10;
    r.host.send(p, r.now);
    step(r, r.client, r.ce, 5);
    EXPECT(r.crecv.stale == stale0 + 1 && r.crecv.latest.frame == kFrames, "an older frame is dropped (newest wins)");

    // The directions are enforced on a live session too: a SNAP_ACK from the
    // host is a violation on the guest, and the guest's snapshots never apply.
    {
        uint32_t v0 = r.client.session().violations;
        uint64_t acks0 = r.crecv.acks;
        uint8_t ack[7] = { MSG_SNAP_ACK, 4, 0, 9, 0, 0, 0 };
        uint8_t pkt[kMaxDatagram];
        size_t m = session_seal(r.host.session(), ack, 7, pkt, r.now);
        udp_send(r.he.sock, r.ce.addr, pkt, m);
        step(r, r.client, r.ce, 5);
        EXPECT(r.client.session().violations == v0 + 1 && r.crecv.acks == acks0,
               "a SNAP_ACK from the host is a violation and is not applied");
    }

    // STALL from either side reaches the other's game (the I/O thread sends
    // it while its own game is stalled).
    {
        uint64_t c0 = r.crecv.stalls;
        payload_clear(&p);
        p.has = HAS_STALL;
        p.stall_ms = 4321;
        EXPECT(r.host.send(p, r.now), "the host sends a STALL");
        step(r, r.client, r.ce, 5);
        EXPECT(r.crecv.stalls == c0 + 1 && r.crecv.last_stall == 4321, "the guest hears the host's STALL");
        uint64_t h0 = r.hrecv.stalls;
        payload_clear(&p);
        p.has = HAS_PSTATE | HAS_STALL;
        p.pstate = good_state(1);
        p.pstate.frame = 100000;
        p.stall_ms = 300;
        EXPECT(r.client.send(p, r.now), "the guest sends a STALL with a state");
        step(r, r.client, r.ce, 5);
        EXPECT(r.hrecv.stalls == h0 + 1 && r.hrecv.last_stall == 300, "the host hears the guest's STALL");
    }

    // M3: reliable events both ways over the live sessions with 10% loss:
    // the guest's hits and pickups, the host's damage and grants, each
    // delivered once and in order, the acknowledgements clearing the queues.
    {
        static EvSide hev, gev;
        uint64_t hdrop0 = r.host.session().drop_parse, cdrop0 = r.client.session().drop_parse;
        r.hrecv.ev = &hev;
        r.crecv.ev = &gev;
        r.he.loss = r.ce.loss = 0.10;
        uint32_t gpushed = 0, hpushed = 0;
        uint32_t frame = 200000;
        for (int f = 0; f < 1200; f++)
        {
            if (f == 1000) r.he.loss = r.ce.loss = 0; // a quiet tail
            if (f < 900 && f % 3 == 0)
            {
                GameEvent g = good_event(gpushed % 2 ? EV_PICKUP : EV_HIT);
                g.table = gpushed; // its order
                if (gev.out.push(g)) gpushed++;
                GameEvent h = good_event(hpushed % 2 ? EV_GRANT : EV_DAMAGE);
                h.scene = kBB02;
                h.id = (uint16_t)(hpushed % 1000); // its order (both types carry an id)
                if (hev.out.push(h)) hpushed++;
            }
            if (f % 2 == 0)
            {
                static Payload p;
                payload_clear(&p);
                p.has = HAS_PSTATE;
                p.pstate = good_state(0);
                p.pstate.frame = ++frame;
                ev_add(hev, p);
                r.host.send(p, r.now);
                payload_clear(&p);
                p.has = HAS_PSTATE;
                p.pstate = good_state(1);
                p.pstate.frame = frame;
                ev_add(gev, p);
                r.client.send(p, r.now);
            }
            r.now += 16666667;
            step(r, r.client, r.ce, f % 16 == 0 ? 1 : 0);
        }
        bool horder = hev.got.size() == gpushed, gorder = gev.got.size() == hpushed;
        for (size_t i = 0; horder && i < hev.got.size(); i++)
            horder = hev.got[i].table == i && hev.got[i].type == (i % 2 ? EV_PICKUP : EV_HIT);
        for (size_t i = 0; gorder && i < gev.got.size(); i++)
            gorder = gev.got[i].id == i % 1000 && gev.got[i].type == (i % 2 ? EV_GRANT : EV_DAMAGE);
        EXPECT(horder, "the host got every guest event once, in order (%zu of %u)", hev.got.size(), gpushed);
        EXPECT(gorder, "the guest got every host event once, in order (%zu of %u)", gev.got.size(), hpushed);
        EXPECT(hev.out.count == 0 && gev.out.count == 0 && !hev.bad && !gev.bad,
               "both queues emptied by the acknowledgements, no bad ones (%u, %u waiting)", (unsigned)hev.out.count,
               (unsigned)gev.out.count);
        EXPECT(r.host.session().drop_parse == hdrop0 && r.client.session().drop_parse == cdrop0 &&
                   r.client.state() == ClientState::Connected && r.host.session().active,
               "no protocol errors, still connected");
        // PICKUPS from the host reach the guest; from the guest they are a violation.
        static Payload p;
        payload_clear(&p);
        p.has = HAS_PSTATE | HAS_PICKUPS;
        p.pstate = good_state(0);
        p.pstate.frame = ++frame;
        p.pick.scene = kBB02;
        p.pick.table = 99;
        p.pick.count = 10;
        p.pick.bits[1] = 0x02;
        uint64_t k0 = r.crecv.picks;
        EXPECT(r.host.send(p, r.now), "the host sends PICKUPS");
        step(r, r.client, r.ce, 5);
        EXPECT(r.crecv.picks == k0 + 1 && r.crecv.last_pick.count == 10 && r.crecv.last_pick.bits[1] == 0x02,
               "the guest gets the host's PICKUPS");
        uint32_t v0 = r.host.session().violations;
        uint8_t pk[3 + 12] = { MSG_PICKUPS, 12, 0, 'B', 'B', '0', '2', 0, 0, 0, 0, 1, 0, 1, 0 };
        uint8_t pkt[kMaxDatagram];
        size_t m = session_seal(r.client.session(), pk, sizeof pk, pkt, r.now);
        udp_send(r.ce.sock, r.he.addr, pkt, m);
        step(r, r.client, r.ce, 5);
        EXPECT(r.host.session().violations == v0 + 1, "PICKUPS from the guest are a violation on the host");
        // Protocol 7: PROGRESS the same way.
        payload_clear(&p);
        p.has = HAS_PSTATE | HAS_PROGRESS;
        p.pstate = good_state(0);
        p.pstate.frame = ++frame;
        p.prog.tokens = 5;
        p.prog.powerup[1] = 1;
        uint64_t g0 = r.crecv.progs;
        EXPECT(r.host.send(p, r.now), "the host sends PROGRESS");
        step(r, r.client, r.ce, 5);
        EXPECT(r.crecv.progs == g0 + 1 && r.crecv.last_prog.tokens == 5 &&
                   r.crecv.last_prog.powerup[1] == 1,
               "the guest gets the host's PROGRESS");
        v0 = r.host.session().violations;
        uint8_t pg[3 + kProgressSize] = { MSG_PROGRESS, kProgressSize, 0, 5 };
        m = session_seal(r.client.session(), pg, sizeof pg, pkt, r.now);
        udp_send(r.ce.sock, r.he.addr, pkt, m);
        step(r, r.client, r.ce, 5);
        EXPECT(r.host.session().violations == v0 + 1,
               "PROGRESS from the guest is a violation on the host");
        // COUNTERS the same way.
        payload_clear(&p);
        p.has = HAS_PSTATE | HAS_COUNTERS;
        p.pstate = good_state(0);
        p.pstate.frame = ++frame;
        p.cnt.scene = kBB02;
        p.cnt.table = 0xC7A16113;
        p.cnt.counters = 6;
        p.cnt.boxes = 2;
        p.cnt.count = 1;
        p.cnt.e[0].index = 4;
        p.cnt.e[0].value = 12;
        p.cnt.text[1] = 2;
        uint64_t c0 = r.crecv.cnts;
        EXPECT(r.host.send(p, r.now), "the host sends COUNTERS");
        step(r, r.client, r.ce, 5);
        EXPECT(r.crecv.cnts == c0 + 1 && r.crecv.last_cnt.count == 1 &&
                   r.crecv.last_cnt.e[0].value == 12 && r.crecv.last_cnt.text[1] == 2,
               "the guest gets the host's COUNTERS");
        v0 = r.host.session().violations;
        uint8_t ct[3 + 11 + 3 + 2] = { MSG_COUNTERS, 16, 0, 'B', 'B', '0', '2', 0x13, 0x61, 0xA1,
                                       0xC7,         6,  2, 1,   4,   12,  0,   0,    2 };
        m = session_seal(r.client.session(), ct, sizeof ct, pkt, r.now);
        udp_send(r.ce.sock, r.he.addr, pkt, m);
        step(r, r.client, r.ce, 5);
        EXPECT(r.host.session().violations == v0 + 1,
               "COUNTERS from the guest are a violation on the host");
        r.hrecv.ev = r.crecv.ev = nullptr;
    }
}

// ---------------------------------------------------------------------------
// 5. Hostile scenarios

static void test_hostile(Rng& rng)
{
    printf("[5] hostile scenarios\n");
    Rig& r = *sRig; // the host from [4], with the guest still connected
    r.he.loss = r.ce.loss = 0;
    uint8_t buf[kMaxDatagram + 64];

    // Ban-list and lockout tests need an empty slot: a second host.
    static Rig r2;
    memcpy(r2.assets, r.assets, kHashSize);
    EXPECT(host_start(r2, 8, "right-password", kMinIters), "host2 start");

    Endpoint stranger;
    EXPECT(stranger.open(2), "stranger socket");
    static Client sc;
    uint8_t reasons[7];
    for (int i = 0; i < 7; i++)
    {
        connect(r2, sc, stranger, nullptr, "guess-123", "Stranger", kBuildId, r2.assets);
        reasons[i] = sc.state() == ClientState::Failed ? sc.fail_reason() : 0;
    }
    EXPECT(reasons[0] == REJ_PASSWORD && reasons[3] == REJ_PASSWORD, "wrong password refused (%u)", reasons[0]);
    EXPECT(reasons[4] == REJ_BLOCKED && reasons[6] == REJ_BLOCKED, "the 5th failure blocks the address (%u %u)",
           reasons[4], reasons[6]);
    EXPECT(sc.fail() == FAIL_REJECTED, "the guest knows it was refused");
    EXPECT(!r2.host.session().active, "no session for the stranger");
    EXPECT(r2.host.stats().bad_password == 5, "wrong passwords counted (%llu)",
           (unsigned long long)r2.host.stats().bad_password);
    bool logged = false;
    for (auto& l : r2.logs) logged |= l.find("wrong password") != std::string::npos;
    EXPECT(logged, "the host logs a wrong password (constant text)");
    printf("  wrong password: refused x4, then blocked (reasons %u %u %u %u %u %u %u)\n", reasons[0], reasons[1],
           reasons[2], reasons[3], reasons[4], reasons[5], reasons[6]);

    // The real guest still gets in while the stranger is blocked.
    Endpoint bro;
    EXPECT(bro.open(3), "bro socket");
    static Client bc;
    EXPECT(connect(r2, bc, bro, nullptr, "right-password", "Brother", kBuildId, r2.assets),
           "the guest joins despite the attack");

    // Game full.
    Endpoint third;
    EXPECT(third.open(4), "third socket");
    static Client tc;
    connect(r2, tc, third, nullptr, "right-password", "Third", kBuildId, r2.assets);
    EXPECT(tc.state() == ClientState::Failed && tc.fail_reason() == REJ_FULL, "full (%u)", tc.fail_reason());

    // Different build, different files.
    Endpoint other;
    EXPECT(other.open(5), "other socket");
    static Client oc;
    uint8_t wrong_build[kBuildIdSize] = { 'o', 'l', 'd' };
    connect(r2, oc, other, nullptr, "right-password", "Other", wrong_build, r2.assets);
    EXPECT(oc.state() == ClientState::Failed && oc.fail_reason() == REJ_BUILD, "build (%u)", oc.fail_reason());
    uint8_t wrong_assets[kHashSize] = { 9 };
    connect(r2, oc, other, nullptr, "right-password", "Other", kBuildId, wrong_assets);
    EXPECT(oc.state() == ClientState::Failed && oc.fail_reason() == REJ_ASSETS, "assets (%u)", oc.fail_reason());

    // HELLOs of the two versions before this one (7, the bug-fix co-op zip's;
    // 6, the 0.9 zip's), each from its own address: REJECT(VERSION) echoing
    // the nonce.
    r2.now += 3 * kNsPerSec; // refill this address's pre-session allowance
    Endpoint older6;
    EXPECT(older6.open(10), "version 6 socket");
    Hello h;
    size_t n = 0;
    for (int back = 1; back <= 2; back++)
    {
        Endpoint& from = back == 1 ? other : older6;
        h.version = (uint16_t)(kProtoVersion - back);
        for (int i = 0; i < 16; i++)
            h.client_nonce[i] = (uint8_t)(0x80 + 32 * back + i);
        memcpy(h.build_id, kBuildId, kBuildIdSize);
        memcpy(h.asset_hash, r2.assets, kHashSize);
        n = encode_hello(buf, sizeof buf, h);
        udp_send(from.sock, r2.he.addr, buf, n);
        drain(
            r2.he,
            [&](const NetAddr& f, const uint8_t* d, size_t m) {
                r2.host.on_datagram(f, d, m, r2.now);
            },
            5);
        Reject rj;
        bool got = false;
        drain(
            from,
            [&](const NetAddr&, const uint8_t* d, size_t m) { got = parse_reject(d, m, &rj); }, 5);
        EXPECT(got && rj.reason == REJ_VERSION && rj.version == kProtoVersion &&
                   rj.client_nonce[0] == h.client_nonce[0],
               "a version %u HELLO is refused by version (%d)", (unsigned)h.version,
               got ? rj.reason : -1);
    }

    // The other way: this game joins a host of each of those versions. That
    // host answers the HELLO with a REJECT(VERSION) of its own version, and
    // the guest reads it: refused at once, with the reason.
    Endpoint old_host;
    EXPECT(old_host.open(9), "older host socket");
    static Client vc[2];
    for (int back = 1; back <= 2; back++)
    {
        Endpoint& self = back == 1 ? other : older6;
        Client& c = vc[back - 1];
        NetIO vio = { &self, ep_send, ep_state, nullptr };
        EXPECT(c.connect(old_host.addr, "right-password", "Other", kBuildId, r2.assets, vio,
                         r2.now),
               "joining an older host");
        bool older_answered = false;
        drain(
            old_host,
            [&](const NetAddr& f, const uint8_t* d, size_t m) {
                Reject older;
                older.version = (uint16_t)(kProtoVersion - back);
                older.reason = REJ_VERSION;
                if (!peek_hello_nonce(d, m, older.client_nonce))
                    return;
                size_t k = encode_reject(buf, sizeof buf, older);
                older_answered = k != 0 && udp_send(old_host.sock, f, buf, k);
            },
            50);
        drain(
            self,
            [&](const NetAddr& f, const uint8_t* d, size_t m) { c.on_datagram(f, d, m, r2.now); },
            50);
        EXPECT(older_answered && c.state() == ClientState::Failed && c.fail() == FAIL_REJECTED &&
                   c.fail_reason() == REJ_VERSION,
               "a version %u host's version refusal is read (%u)", (unsigned)(kProtoVersion - back),
               c.fail_reason());
        EXPECT(strstr(reject_text(c.fail_reason()), "different version") != nullptr,
               "the guest is told the host's game is a different version");
    }

    // A HELLO shorter than 1200 bytes, an oversized one, and a forged AUTH:
    // no reply at all.
    r2.now += 3 * kNsPerSec;
    uint64_t out0 = r2.host.stats().bytes_out_pre;
    h.version = kProtoVersion;
    n = encode_hello(buf, sizeof buf, h);
    udp_send(other.sock, r2.he.addr, buf, 200);
    uint8_t huge[1400];
    memset(huge, 0, sizeof huge);
    memcpy(huge, buf, n);
    udp_send(other.sock, r2.he.addr, huge, sizeof huge);
    Auth fa;
    memset(&fa, 0, sizeof fa);
    memcpy(fa.build_id, kBuildId, kBuildIdSize);
    memcpy(fa.asset_hash, r2.assets, kHashSize);
    fa.cookie_time = (uint32_t)(r2.now / (10 * kNsPerSec));
    for (int i = 0; i < 32; i++) fa.cookie[i] = (uint8_t)rng.next();
    fa.name_len = 1;
    fa.name[0] = 'x';
    n = encode_auth(buf, sizeof buf, fa);
    udp_send(other.sock, r2.he.addr, buf, n);
    drain(r2.he, [&](const NetAddr& f, const uint8_t* d, size_t m) { r2.host.on_datagram(f, d, m, r2.now); }, 5);
    int replies = drain(other, [&](const NetAddr&, const uint8_t*, size_t) {}, 5);
    EXPECT(replies == 0 && r2.host.stats().bytes_out_pre == out0, "short / oversized HELLO, forged cookie: silence");
    EXPECT(r2.host.stats().bad_cookie >= 1, "forged cookie counted");

    // Replay, tamper and a wrong source on the live session from [4].
    r.ce.capture = true;
    Payload p;
    payload_clear(&p);
    p.has = HAS_PING;
    p.ping = 99;
    r.client.send(p, r.now);
    step(r, r.client, r.ce, 5);
    uint64_t in0 = r.host.session().pkts_in, rep0 = r.host.session().drop_replay, tag0 = r.host.session().drop_tag;
    std::vector<uint8_t> old = r.ce.captured.back();
    udp_send(r.ce.sock, r.he.addr, old.data(), old.size());
    udp_send(r.ce.sock, r.he.addr, old.data(), old.size());
    step(r, r.client, r.ce, 5);
    EXPECT(r.host.session().pkts_in == in0 && r.host.session().drop_replay == rep0 + 2, "replay dropped");
    uint8_t pl[7] = { MSG_PING, 4, 0, 1, 0, 0, 0 };
    uint8_t pkt[kMaxDatagram];
    size_t m = session_seal(r.client.session(), pl, 7, pkt, r.now);
    pkt[kDataHeader + 3] ^= 0x01;
    udp_send(r.ce.sock, r.he.addr, pkt, m);
    step(r, r.client, r.ce, 5);
    EXPECT(r.host.session().pkts_in == in0 && r.host.session().drop_tag == tag0 + 1, "tampered packet dropped");
    uint64_t np0 = r.host.stats().nonpeer_data;
    m = session_seal(r.client.session(), pl, 7, pkt, r.now);
    udp_send(stranger.sock, r.he.addr, pkt, m);
    step(r, r.client, r.ce, 5);
    EXPECT(r.host.stats().nonpeer_data == np0 + 1 && r.host.session().pkts_in == in0, "spoofed source dropped");
    printf("  replay, tamper, wrong source: dropped (replay %llu, bad tag %llu, non-peer %llu)\n",
           (unsigned long long)r.host.session().drop_replay, (unsigned long long)r.host.session().drop_tag,
           (unsigned long long)r.host.stats().nonpeer_data);

    // HELLO flood: 3000 in one virtual second from one address.
    uint64_t ch0 = r2.host.stats().challenges, rj0 = r2.host.stats().rejects, rd0 = r2.host.stats().pre_rate_dropped;
    n = encode_hello(buf, sizeof buf, h);
    for (int i = 0; i < 3000; i++) r2.host.on_datagram(other.addr, buf, n, r2.now + (uint64_t)i * 333333);
    uint64_t answered = (r2.host.stats().challenges - ch0) + (r2.host.stats().rejects - rj0);
    printf("  HELLO flood: 3000 in 1 s from one address -> %llu answered, %llu rate-dropped\n",
           (unsigned long long)answered, (unsigned long long)(r2.host.stats().pre_rate_dropped - rd0));
    EXPECT(answered <= 13, "flood answered at most burst + rate (%llu)", (unsigned long long)answered);
    // The packets that one address's own limit refused did not spend the
    // global allowance: five other joiners, right after, are all answered.
    {
        uint64_t c0 = r2.host.stats().challenges + r2.host.stats().rejects; // REJECT(FULL): a guest is in
        for (int i = 0; i < 5; i++)
        {
            NetAddr a;
            char s[40];
            snprintf(s, sizeof s, "198.19.0.%d:4000", i + 1);
            addr_parse(s, 0, &a);
            r2.host.on_datagram(a, buf, n, r2.now + kNsPerSec);
        }
        uint64_t got5 = r2.host.stats().challenges + r2.host.stats().rejects - c0;
        EXPECT(got5 == 5, "a flood from one address does not starve other joiners (%llu of 5 answered)",
               (unsigned long long)got5);
    }
    // Spoofed sources all over the internet: the global bucket.
    ch0 = r2.host.stats().challenges + r2.host.stats().rejects;
    for (int i = 0; i < 3000; i++)
    {
        NetAddr a;
        char s[40];
        snprintf(s, sizeof s, "198.18.%d.%d:%d", (i >> 8) & 255, i & 255, 1000 + i);
        addr_parse(s, 0, &a);
        r2.host.on_datagram(a, buf, n, r2.now + 2 * kNsPerSec + (uint64_t)i * 333333);
    }
    answered = r2.host.stats().challenges + r2.host.stats().rejects - ch0;
    printf("  spoofed-source flood: 3000 in 1 s from 3000 addresses -> %llu answered (cap 100/s + 200 burst)\n",
           (unsigned long long)answered);
    EXPECT(answered <= 310, "global cap (%llu)", (unsigned long long)answered);
    EXPECT(r2.host.stats().max_reply_ratio_x100 <= 100 && r.host.stats().max_reply_ratio_x100 <= 100,
           "no pre-session reply larger than its request (max %llu%%)",
           (unsigned long long)r2.host.stats().max_reply_ratio_x100);
    printf("  amplification: largest pre-session reply = %llu%% of its request\n",
           (unsigned long long)r2.host.stats().max_reply_ratio_x100);

    // A hostile "host": asks for 50 million PBKDF2 rounds, then sends a fake
    // WELCOME.
    {
        static Client vc;
        Endpoint fake, victim;
        EXPECT(fake.open(6) && victim.open(7), "fake sockets");
        NetIO io = { &victim, ep_send, nullptr, nullptr };
        EXPECT(vc.connect(fake.addr, "pw", "Victim", kBuildId, r2.assets, io, r2.now), "victim connect");
        Hello vh;
        bool have = false;
        drain(fake, [&](const NetAddr&, const uint8_t* d, size_t k) { have = parse_hello(d, k, &vh); }, 5);
        EXPECT(have, "the fake host got a HELLO");
        Challenge c;
        memset(&c, 0, sizeof c);
        memcpy(c.client_nonce, vh.client_nonce, kNonceSize);
        c.iters = 50000000;
        size_t k = encode_challenge(buf, sizeof buf, c);
        double t = wall_ms();
        vc.on_datagram(fake.addr, buf, k, r2.now);
        EXPECT(vc.state() == ClientState::HelloSent && wall_ms() - t < 100, "an absurd iteration count is ignored");
        c.iters = kMinIters;
        k = encode_challenge(buf, sizeof buf, c);
        vc.on_datagram(fake.addr, buf, k, r2.now);
        EXPECT(vc.state() == ClientState::AuthSent, "a sane challenge is answered");
        int auths = drain(fake, [&](const NetAddr&, const uint8_t*, size_t) {}, 5);
        EXPECT(auths == 1, "one AUTH for the challenge (%d)", auths);
        // Then a stream of CHALLENGEs with new salts at the most rounds the
        // parser allows: none may cost another PBKDF2 in this join attempt.
        Challenge c2 = c;
        c2.iters = kMaxIters;
        t = wall_ms();
        for (int i = 0; i < 5; i++)
        {
            c2.salt[0] = (uint8_t)(i + 1);
            k = encode_challenge(buf, sizeof buf, c2);
            vc.on_datagram(fake.addr, buf, k, r2.now);
        }
        double spent = wall_ms() - t;
        auths = drain(fake, [&](const NetAddr&, const uint8_t*, size_t) {}, 5);
        EXPECT(vc.state() == ClientState::AuthSent && auths == 0 && spent < 100,
               "a CHALLENGE needing a second key in one join is ignored (%d replies, %.0f ms)", auths, spent);
        // The same challenge again (an answer to a resent HELLO) is still answered.
        k = encode_challenge(buf, sizeof buf, c);
        vc.on_datagram(fake.addr, buf, k, r2.now);
        auths = drain(fake, [&](const NetAddr&, const uint8_t*, size_t) {}, 5);
        EXPECT(auths == 1, "a repeated CHALLENGE gets the AUTH again (%d)", auths);
        Welcome w;
        memset(&w, 0, sizeof w);
        w.session_id = 1234;
        w.slot = 1;
        w.name_len = 4;
        memcpy(w.name, "Fake", 4);
        k = encode_welcome(buf, sizeof buf, w);
        vc.on_datagram(fake.addr, buf, k, r2.now);
        EXPECT(vc.state() == ClientState::AuthSent, "a WELCOME without the password proof is ignored");
        // A REJECT that does not echo our nonce (an off-path spoofer).
        Reject fr;
        memset(&fr, 0, sizeof fr);
        fr.version = kProtoVersion;
        fr.reason = REJ_FULL;
        k = encode_reject(buf, sizeof buf, fr);
        vc.on_datagram(fake.addr, buf, k, r2.now);
        EXPECT(vc.state() == ClientState::AuthSent, "a REJECT without our nonce is ignored");
        // Ten seconds of nothing: no answer.
        vc.update(r2.now + kHandshakeTimeout + kNsPerSec);
        EXPECT(vc.state() == ClientState::Failed && vc.fail() == FAIL_NO_ANSWER, "no answer within 10 s");
    }

    // Protocol violations from an authenticated guest: a wrong slot, and
    // SNAPSHOTs (only the host sends those). More than 16 drop the session,
    // with a goodbye.
    {
        Session& gs = bc.session();
        uint32_t v0 = r2.host.session().violations;
        Payload wp;
        payload_clear(&wp);
        wp.has = HAS_PSTATE;
        wp.pstate = good_state(0); // the guest claiming to be the host
        uint8_t body[kMaxPayload];
        size_t bl = encode_payload(wp, Role::Client, body, sizeof body);
        m = session_seal(gs, body, bl, pkt, r2.now);
        udp_send(bro.sock, r2.he.addr, pkt, m);
        drain(r2.he, [&](const NetAddr& f, const uint8_t* d, size_t k) { r2.host.on_datagram(f, d, k, r2.now); }, 5);
        EXPECT(r2.host.session().violations == v0 + 1, "a state with the host's slot from the guest is a violation");
        uint8_t snap[7] = { MSG_SNAPSHOT, 4, 0, 1, 0, 0, 0 };
        for (int i = 0; i < 20; i++)
        {
            m = session_seal(gs, snap, 7, pkt, r2.now);
            udp_send(bro.sock, r2.he.addr, pkt, m);
        }
        drain(r2.he, [&](const NetAddr& f, const uint8_t* d, size_t k) { r2.host.on_datagram(f, d, k, r2.now); }, 5);
        EXPECT(!r2.host.session().active && r2.host.last_end() == END_PROTOCOL,
               "more than 16 violations end the session");
        drain(bro, [&](const NetAddr& f, const uint8_t* d, size_t k) { bc.on_datagram(f, d, k, r2.now); }, 5);
        EXPECT(bc.state() == ClientState::Failed && bc.end_reason() == END_PROTOCOL,
               "the guest is told why (%u)", bc.end_reason());
    }

    // Kick: the guest is told, and its address stays blocked while hosting.
    r2.now += 30 * kNsPerSec;
    EXPECT(connect(r2, bc, bro, nullptr, "right-password", "Brother", kBuildId, r2.assets), "the guest rejoins");
    r2.host.kick(r2.now);
    drain(bro, [&](const NetAddr& f, const uint8_t* d, size_t k) { bc.on_datagram(f, d, k, r2.now); }, 5);
    EXPECT(bc.state() == ClientState::Failed && bc.end_reason() == END_KICKED, "kicked (%u)", bc.end_reason());
    r2.now += 120 * kNsPerSec;
    connect(r2, bc, bro, nullptr, "right-password", "Brother", kBuildId, r2.assets);
    EXPECT(bc.state() == ClientState::Failed && bc.fail_reason() == REJ_BLOCKED,
           "a kicked address stays blocked (%u)", bc.fail_reason());

    // Timeout: a guest goes silent for 11 s and the slot frees up.
    r2.now += 30 * kNsPerSec;
    EXPECT(connect(r2, tc, third, nullptr, "right-password", "Third", kBuildId, r2.assets), "another guest joins");
    for (int i = 0; i < 12; i++)
    {
        r2.now += kNsPerSec;
        drain(r2.he, [&](const NetAddr& f, const uint8_t* d, size_t k) { r2.host.on_datagram(f, d, k, r2.now); }, 1);
        r2.host.update(r2.now);
    }
    EXPECT(!r2.host.session().active && r2.host.last_end() == END_TIMEOUT, "a silent guest times out");
    EXPECT(connect(r2, oc, other, nullptr, "right-password", "Other", kBuildId, r2.assets),
           "the slot can be used again after a timeout");

    // A clean goodbye from the guest.
    oc.leave(r2.now);
    drain(r2.he, [&](const NetAddr& f, const uint8_t* d, size_t k) { r2.host.on_datagram(f, d, k, r2.now); }, 5);
    EXPECT(!r2.host.session().active && r2.host.last_end() == END_PEER_LEFT, "the guest left (%u)",
           r2.host.last_end());

    // A game stalled past the limit says so: each side learns why.
    EXPECT(connect(r2, oc, other, nullptr, "right-password", "Other", kBuildId, r2.assets), "the guest joins again");
    oc.leave(r2.now, DC_STALLED);
    drain(r2.he, [&](const NetAddr& f, const uint8_t* d, size_t k) { r2.host.on_datagram(f, d, k, r2.now); }, 5);
    EXPECT(!r2.host.session().active && r2.host.last_end() == END_STALLED, "the guest's game stalled (%u)",
           r2.host.last_end());
    EXPECT(connect(r2, oc, other, nullptr, "right-password", "Other", kBuildId, r2.assets), "and joins once more");
    r2.host.close(r2.now, DC_STALLED);
    drain(other, [&](const NetAddr& f, const uint8_t* d, size_t k) { oc.on_datagram(f, d, k, r2.now); }, 5);
    EXPECT(oc.state() == ClientState::Failed && oc.end_reason() == END_STALLED, "the host's game stalled (%u)",
           oc.end_reason());

    // A peer that says STALL forever (and never runs, nor ends the session
    // itself after a minute as an honest one does) is given up on after
    // kPeerStallLimit; a state of its own in between starts the count again.
    {
        static Payload sp;
        auto tick = [&](bool guest_stalls, bool host_stalls, bool guest_state) {
            r2.now += kNsPerSec;
            payload_clear(&sp);
            sp.has = HAS_STALL;
            sp.stall_ms = 65535;
            if (guest_state)
            {
                payload_clear(&sp);
                sp.has = HAS_PSTATE;
                sp.pstate = good_state(1);
            }
            if (guest_stalls || guest_state) oc.send(sp, r2.now);
            if (host_stalls)
            {
                payload_clear(&sp);
                sp.has = HAS_STALL;
                sp.stall_ms = 65535;
                r2.host.send(sp, r2.now);
            }
            drain(r2.he, [&](const NetAddr& f, const uint8_t* d, size_t k) { r2.host.on_datagram(f, d, k, r2.now); }, 5);
            r2.host.update(r2.now);
            drain(other, [&](const NetAddr& f, const uint8_t* d, size_t k) { oc.on_datagram(f, d, k, r2.now); }, 5);
            oc.update(r2.now);
            drain(r2.he, [&](const NetAddr& f, const uint8_t* d, size_t k) { r2.host.on_datagram(f, d, k, r2.now); }, 1);
        };
        EXPECT(connect(r2, oc, other, nullptr, "right-password", "Other", kBuildId, r2.assets), "a guest joins to stall");
        for (int i = 0; i < 70; i++) tick(true, false, false);
        EXPECT(r2.host.session().active && r2.host.session().peer_stall_since != 0,
               "70 s of a guest's STALL reports: still waiting");
        tick(false, false, true);
        EXPECT(r2.host.session().active && r2.host.session().peer_stall_since == 0,
               "a state of the guest's own ends its stall");
        for (int i = 0; i < 70; i++) tick(true, false, false);
        EXPECT(r2.host.session().active, "the count starts again after a state");
        for (int i = 0; i < 10 && r2.host.session().active; i++) tick(true, false, false);
        EXPECT(!r2.host.session().active && r2.host.last_end() == END_STALLED,
               "a guest that reports a stall past the limit is given up on (%u)", r2.host.last_end());
        EXPECT(oc.state() == ClientState::Failed && oc.end_reason() == END_TIMEOUT,
               "and hears that the host stopped waiting (%u)", oc.end_reason());

        EXPECT(connect(r2, oc, other, nullptr, "right-password", "Other", kBuildId, r2.assets),
               "a guest joins a stalling host");
        for (int i = 0; i < 70; i++) tick(false, true, true);
        EXPECT(oc.state() == ClientState::Connected, "70 s of the host's STALL reports: the guest still waits");
        for (int i = 0; i < 10 && oc.state() == ClientState::Connected; i++) tick(false, true, true);
        EXPECT(oc.state() == ClientState::Failed && oc.end_reason() == END_STALLED,
               "a host that reports a stall past the limit is given up on (%u)", oc.end_reason());
        EXPECT(!r2.host.session().active && r2.host.last_end() == END_TIMEOUT,
               "and the host hears that the guest stopped waiting (%u)", r2.host.last_end());

        // A short stall on each side, then games that run again but send no
        // state for longer than the limit (the ending's story movie: the game
        // thread runs, no play frame does). Once the STALL reports stop, the
        // stall is over and the session stays up.
        EXPECT(connect(r2, oc, other, nullptr, "right-password", "Other", kBuildId, r2.assets),
               "a guest joins, then both watch a long movie");
        tick(true, true, false);
        EXPECT(r2.host.session().peer_stall_since != 0 && oc.session().peer_stall_since != 0,
               "a short stall on each side is heard");
        for (int i = 0; i < 90; i++) tick(false, false, false);
        EXPECT(r2.host.session().active && oc.state() == ClientState::Connected &&
                   r2.host.session().peer_stall_since == 0 && oc.session().peer_stall_since == 0,
               "90 s without a state after it: the stall is over and the session stays up");
    }
}

// ---------------------------------------------------------------------------
// 5b. M5: joining again after a lost connection, the quality figures, the
//     refusals in words (docs/MULTIPLAYER.md 8)

static void test_m5()
{
    printf("[5b] M5: joining again, a silent session replaced, loss figures, refusal texts\n");
    static Rig r;
    sha256("the same level files", 20, r.assets);
    EXPECT(host_start(r, 10, "M5-CODE-1234", kMinIters), "host start");

    Endpoint g1, g2;
    EXPECT(g1.open(11) && g2.open(12), "guest sockets");
    static Client c1, c2;
    EXPECT(connect(r, c1, g1, nullptr, "M5-CODE-1234", "Brother", kBuildId, r.assets), "the guest joins");
    uint32_t id1 = r.host.session().id;
    EXPECT(r.host.session().active && addr_equal(r.host.session().peer, g1.addr), "the host's session is the guest's");

    // A second join with the password while the guest is heard from: full.
    connect(r, c2, g2, nullptr, "M5-CODE-1234", "Brother", kBuildId, r.assets);
    EXPECT(c2.state() == ClientState::Failed && c2.fail_reason() == REJ_FULL, "full while the guest is heard (%u)",
           c2.fail_reason());
    EXPECT(r.host.session().id == id1 && r.host.stats().replaced == 0, "the guest's session is kept");

    // The guest goes silent (its game closed, its address changed) for
    // longer than kReplaceSilence, less than the session timeout.
    r.now += kReplaceSilence + kNsPerSec / 2;
    r.host.update(r.now);
    EXPECT(r.host.session().active && r.host.session().id == id1, "a silent guest is not yet timed out");

    // A wrong password cannot take the silent guest's place.
    r.now += 3 * kNsPerSec / 10;
    connect(r, c2, g2, nullptr, "M5-CODE-9999", "Brother", kBuildId, r.assets);
    EXPECT(c2.state() == ClientState::Failed && c2.fail_reason() == REJ_PASSWORD, "a wrong password is refused (%u)",
           c2.fail_reason());
    EXPECT(r.host.session().active && r.host.session().id == id1 && r.host.stats().replaced == 0,
           "the silent guest's session is kept after a wrong password");

    // The right password from the new address replaces it.
    r.now += 3 * kNsPerSec / 10;
    EXPECT(connect(r, c2, g2, nullptr, "M5-CODE-1234", "Brother", kBuildId, r.assets),
           "a new join replaces a silent session");
    EXPECT(r.host.session().active && r.host.session().id != id1 && addr_equal(r.host.session().peer, g2.addr),
           "the new session is the new address's");
    EXPECT(r.host.stats().replaced == 1, "one session replaced (%llu)", (unsigned long long)r.host.stats().replaced);
    bool logged = false;
    for (auto& l : r.logs) logged |= l.find("replaced") != std::string::npos;
    EXPECT(logged, "the host logs the replacement");

    // The old address's packets (its game still thinks it is joined) are not
    // the partner's any more.
    uint64_t np0 = r.host.stats().nonpeer_data, in0 = r.host.session().pkts_in;
    Payload pp;
    payload_clear(&pp);
    pp.has = HAS_PING;
    pp.ping = 5;
    EXPECT(c1.send(pp, r.now), "the old guest still sends");
    drain(r.he, [&](const NetAddr& f, const uint8_t* d, size_t n) { r.host.on_datagram(f, d, n, r.now); }, 5);
    EXPECT(r.host.stats().nonpeer_data == np0 + 1 && r.host.session().pkts_in == in0,
           "the replaced session's packets are dropped");

    // Both sides time out (a pulled cable for over 10 s); the guest joins
    // again from the same address and port, and is let in.
    r.now += kSessionTimeout + kNsPerSec;
    r.host.update(r.now);
    c2.update(r.now);
    EXPECT(!r.host.session().active && r.host.last_end() == END_TIMEOUT, "the host timed out (%u)",
           (unsigned)r.host.last_end());
    EXPECT(c2.state() == ClientState::Failed && c2.fail() == FAIL_ENDED && c2.end_reason() == END_TIMEOUT,
           "the guest timed out");
    EXPECT(connect(r, c2, g2, nullptr, "M5-CODE-1234", "Brother", kBuildId, r.assets),
           "the guest joins again after a timeout");
    EXPECT(r.host.stats().replaced == 1, "nothing replaced for a join after a timeout");

    // A kicked guest is blocked from joining again.
    r.host.kick(r.now);
    for (int i = 0; i < 4; i++) step(r, c2, g2, 2);
    EXPECT(c2.state() == ClientState::Failed && c2.end_reason() == END_KICKED, "the guest hears the kick (%u)",
           (unsigned)c2.end_reason());
    r.now += kNsPerSec;
    connect(r, c2, g2, nullptr, "M5-CODE-1234", "Brother", kBuildId, r.assets);
    EXPECT(c2.state() == ClientState::Failed && c2.fail_reason() == REJ_BLOCKED, "a kicked guest is blocked (%u)",
           c2.fail_reason());

    // A copy of an accepted AUTH (a late duplicate, or a replay by someone on
    // the path: it passes the cookie and the proof for up to 20 s) is not a new
    // join: it neither replaces a silent session nor starts one.
    {
        static Rig rc;
        sha256("the same level files", 20, rc.assets);
        EXPECT(host_start(rc, 13, "M5-CODE-1234", kMinIters), "host start (copies)");
        Endpoint ga;
        EXPECT(ga.open(14), "guest socket (copies)");
        ga.capture = true;
        static Client ca;
        EXPECT(connect(rc, ca, ga, nullptr, "M5-CODE-1234", "Brother", kBuildId, rc.assets), "the guest joins (copies)");
        std::vector<uint8_t> auth;
        for (auto& d : ga.captured)
        {
            uint16_t ver;
            uint8_t type;
            if (peek_pre(d.data(), d.size(), &ver, &type) && type == PKT_AUTH) auth = d;
        }
        EXPECT(!auth.empty(), "the guest's AUTH was seen");
        uint32_t idc = rc.host.session().id;

        // While the guest is heard from: nothing, not even a refusal.
        uint64_t rej0 = rc.host.stats().rejects;
        rc.host.on_datagram(ga.addr, auth.data(), auth.size(), rc.now);
        EXPECT(rc.host.session().id == idc && rc.host.stats().auth_copies == 1 && rc.host.stats().rejects == rej0,
               "a copy of the AUTH is dropped while the guest is heard");

        // The guest silent past kReplaceSilence, the cookie still good.
        rc.now += kReplaceSilence + kNsPerSec / 2;
        rc.host.update(rc.now);
        rc.host.on_datagram(ga.addr, auth.data(), auth.size(), rc.now);
        EXPECT(rc.host.session().active && rc.host.session().id == idc && rc.host.stats().replaced == 0 &&
                   rc.host.stats().auth_copies == 2,
               "a copy of the AUTH does not replace the silent guest's session");

        // The session over: a copy starts no session.
        ca.leave(rc.now);
        drain(rc.he, [&](const NetAddr& f, const uint8_t* d, size_t n) { rc.host.on_datagram(f, d, n, rc.now); }, 5);
        EXPECT(!rc.host.session().active, "the guest left");
        rc.host.on_datagram(ga.addr, auth.data(), auth.size(), rc.now);
        EXPECT(!rc.host.session().active && rc.host.stats().auth_copies == 3 && rc.host.stats().accepted == 1,
               "a copy of the AUTH starts no session");

        // The same player joining again has a new nonce: let in.
        ga.capture = false;
        EXPECT(connect(rc, ca, ga, nullptr, "M5-CODE-1234", "Brother", kBuildId, rc.assets),
               "a new join from the same address is let in");
        EXPECT(rc.host.stats().accepted == 2 && rc.host.stats().auth_copies == 3, "and is not taken for a copy");

        // A resend of the AUTH before the session is confirmed still gets the
        // WELCOME again (the first was lost).
        rc.host.close(rc.now);
        Endpoint gb;
        EXPECT(gb.open(15), "guest socket (resend)");
        static Client cb;
        NetIO iob = { &gb, ep_send, ep_state, nullptr };
        EXPECT(cb.connect(rc.he.addr, "M5-CODE-1234", "Brother", kBuildId, rc.assets, iob, rc.now), "a join starts");
        for (int i = 0; i < 4 && cb.state() != ClientState::AuthSent; i++)
        {
            drain(rc.he, [&](const NetAddr& f, const uint8_t* d, size_t n) { rc.host.on_datagram(f, d, n, rc.now); }, 5);
            drain(gb, [&](const NetAddr& f, const uint8_t* d, size_t n) { cb.on_datagram(f, d, n, rc.now); }, 5);
        }
        drain(rc.he, [&](const NetAddr& f, const uint8_t* d, size_t n) { rc.host.on_datagram(f, d, n, rc.now); }, 5);
        EXPECT(rc.host.session().active && addr_equal(rc.host.session().peer, gb.addr), "the host let it in");
        drain(gb, [](const NetAddr&, const uint8_t*, size_t) {}, 5); // the WELCOME is lost
        EXPECT(cb.state() == ClientState::AuthSent, "the guest did not hear the WELCOME");
        rc.now += kResend;
        cb.update(rc.now); // resends the AUTH
        drain(rc.he, [&](const NetAddr& f, const uint8_t* d, size_t n) { rc.host.on_datagram(f, d, n, rc.now); }, 5);
        drain(gb, [&](const NetAddr& f, const uint8_t* d, size_t n) { cb.on_datagram(f, d, n, rc.now); }, 5);
        EXPECT(cb.state() == ClientState::Connected && rc.host.stats().accepted == 3 && rc.host.stats().auth_copies == 3,
               "the resent AUTH gets the same WELCOME (state %d)", (int)cb.state());
    }

    // A welcome and a goodbye that come in one batch: the join ends without
    // ever looking connected to the game (iCoop.cpp's status says it ended).
    {
        static Rig rw;
        sha256("the same level files", 20, rw.assets);
        EXPECT(host_start(rw, 16, "M5-CODE-1234", kMinIters), "host start (one batch)");
        Endpoint gw;
        EXPECT(gw.open(17), "guest socket (one batch)");
        static Client cw;
        NetIO iow = { &gw, ep_send, ep_state, nullptr };
        EXPECT(cw.connect(rw.he.addr, "M5-CODE-1234", "Brother", kBuildId, rw.assets, iow, rw.now), "a join starts");
        for (int i = 0; i < 4 && cw.state() != ClientState::AuthSent; i++)
        {
            drain(rw.he, [&](const NetAddr& f, const uint8_t* d, size_t n) { rw.host.on_datagram(f, d, n, rw.now); }, 5);
            drain(gw, [&](const NetAddr& f, const uint8_t* d, size_t n) { cw.on_datagram(f, d, n, rw.now); }, 5);
        }
        drain(rw.he, [&](const NetAddr& f, const uint8_t* d, size_t n) { rw.host.on_datagram(f, d, n, rw.now); }, 5);
        rw.host.kick(rw.now); // the goodbye right after the welcome
        drain(gw, [&](const NetAddr& f, const uint8_t* d, size_t n) { cw.on_datagram(f, d, n, rw.now); }, 5);
        EXPECT(cw.state() == ClientState::Failed && cw.fail() == FAIL_ENDED && cw.end_reason() == END_KICKED,
               "ended without being seen connected (state %d, fail %d, end %d)", (int)cw.state(), (int)cw.fail(),
               (int)cw.end_reason());
    }

    // Loss figures.
    Session s;
    EXPECT(session_loss_in(s) == -1 && session_loss_out(s) == -1, "no figures without a session");
    s.active = true;
    s.recv_max = 50;
    s.recv_bits = ~0ull;
    EXPECT(session_loss_in(s) == -1, "no figure before 64 packets");
    s.recv_max = 1000;
    EXPECT(session_loss_in(s) == 0, "nothing lost (%d)", session_loss_in(s));
    s.recv_bits = ~0ull & ~(0x3FFFull << 8); // 14 of the 56 counted packets missing
    EXPECT(session_loss_in(s) == 25, "a quarter lost (%d)", session_loss_in(s));
    s.recv_bits = 0xFF; // only the newest 8: not counted
    EXPECT(session_loss_in(s) == 100, "all counted lost (%d)", session_loss_in(s));
    s.peer_ack = 30;
    s.peer_ack_bits = ~0u;
    EXPECT(session_loss_out(s) == -1, "no figure out before 40 packets");
    s.peer_ack = 500;
    EXPECT(session_loss_out(s) == 0, "nothing lost out (%d)", session_loss_out(s));
    s.peer_ack_bits = 0xFFFF0000u;
    EXPECT(session_loss_out(s) == 50, "half lost out (%d)", session_loss_out(s));

    // Refusals in words: one for each reason, constant text, no format.
    const char* seen[REJ_END] = {};
    for (uint8_t why = 1; why < REJ_END; why++)
    {
        const char* t = reject_text(why);
        seen[why] = t;
        EXPECT(t != nullptr && strlen(t) > 20 && strchr(t, '%') == nullptr, "refusal %u in words", why);
    }
    EXPECT(seen[REJ_PASSWORD] != seen[REJ_FULL] && seen[REJ_ASSETS] != seen[REJ_BUILD], "the refusals differ");
    EXPECT(strstr(seen[REJ_BUILD], "version") != nullptr && strstr(seen[REJ_ASSETS], "game files") != nullptr &&
               strstr(seen[REJ_PASSWORD], "password") != nullptr && strstr(seen[REJ_FULL], "already") != nullptr,
           "each refusal names its cause");
    printf("  silent session replaced by a new join with the password (not by a wrong one, not while heard,\n"
           "  not by a copy of an AUTH);\n"
           "  joined again after a timeout; a kicked guest blocked; loss figures; refusals in words\n");
}

// ---------------------------------------------------------------------------
// 7. Local co-op (docs/MULTIPLAYER.md 12): the control pipe's lines, the
// one-time password, Player 2's environment and folder, and the session on a
// port the system picks.

static bool line_of(const char* text, Line* out)
{
    return parse_line(text, strlen(text), out);
}

struct Collected
{
    std::vector<std::pair<bool, Line>> got;
};

static void collect(void* ctx, bool ok, const Line& l)
{
    ((Collected*)ctx)->got.emplace_back(ok, l);
}

static bool has_var(const std::vector<std::string>& env, const char* entry)
{
    for (auto& v : env)
        if (v == entry) return true;
    return false;
}

static bool has_name(const std::vector<std::string>& env, const char* name)
{
    size_t n = strlen(name);
    for (auto& v : env)
        if (v.size() > n && v[n] == '=' && strncmp(v.c_str(), name, n) == 0) return true;
    return false;
}

static void test_local()
{
    printf("[7] local co-op: the pipe's lines, the password, the child's environment, a port-0 host\n");
    const char* code = "K7QXM2PD9TRW0123456789AB";
    char text[160];
    snprintf(text, sizeof text, "hello 1 port=50123 code=%s", code);
    Line l;
    EXPECT(line_of(text, &l) && l.kind == LINE_HELLO && l.port == 50123 && !strcmp(l.code, code), "hello parsed");
    char back[160];
    size_t n = format_line(l, back, sizeof back);
    EXPECT(n == strlen(text) + 1 && !memcmp(back, text, n - 1) && back[n - 1] == '\n', "hello formatted back");
    EXPECT(line_of("quit", &l) && l.kind == LINE_QUIT, "quit parsed");
    n = format_line(l, back, sizeof back);
    EXPECT(n == 5 && !memcmp(back, "quit\n", 5), "quit formatted");
    EXPECT(line_of("hello 1 port=1024 code=K7QXM2PD9TRW0123456789AB", &l) && l.port == 1024, "lowest port");
    EXPECT(line_of("hello 1 port=65535 code=K7QXM2PD9TRW0123456789AB", &l) && l.port == 65535, "highest port");

    const char* bad[] = {
        "", "quit ", " quit", "QUIT", "quit\r", "quitx", "hello", "hello 1",
        "hello 2 port=50123 code=K7QXM2PD9TRW0123456789AB",  // another version
        "hello 1 port=1023 code=K7QXM2PD9TRW0123456789AB",   // under 1024
        "hello 1 port=65536 code=K7QXM2PD9TRW0123456789AB",  // over the range
        "hello 1 port=0 code=K7QXM2PD9TRW0123456789AB",
        "hello 1 port=05012 code=K7QXM2PD9TRW0123456789AB",  // a leading zero
        "hello 1 port=123456 code=K7QXM2PD9TRW0123456789AB", // six digits
        "hello 1 port=-5012 code=K7QXM2PD9TRW0123456789AB",
        "hello 1 port=50123  code=K7QXM2PD9TRW0123456789AB", // two spaces
        "hello 1 port=50123 code=K7QXM2PD9TRW0123456789A",   // 23 symbols
        "hello 1 port=50123 code=K7QXM2PD9TRW0123456789ABC", // 25
        "hello 1 port=50123 code=K7QXM2PD9TRW0123456789AI",  // I is not in the alphabet
        "hello 1 port=50123 code=K7QXM2PD9TRW0123456789AL",
        "hello 1 port=50123 code=K7QXM2PD9TRW0123456789AO",
        "hello 1 port=50123 code=K7QXM2PD9TRW0123456789AU",
        "hello 1 port=50123 code=k7qxm2pd9trw0123456789ab",  // lower case
        "hello 1 port=50123 code=K7QXM2PD9TRW0123456789AB ", // a trailing space
        "hello 1 code=K7QXM2PD9TRW0123456789AB port=50123",  // the keys swapped
        "hello 1 port=50123 pass=K7QXM2PD9TRW0123456789AB",
        "pad keyboard",                                      // not in this version
    };
    int refused = 0;
    for (const char* b : bad) refused += !line_of(b, &l);
    EXPECT(refused == (int)(sizeof bad / sizeof bad[0]), "malformed lines refused (%d of %d)", refused,
           (int)(sizeof bad / sizeof bad[0]));
    char nul[64] = "hello 1 port=50123 code=K7QXM2PD9TRW0123456789AB";
    nul[20] = '\0';
    EXPECT(!parse_line(nul, strlen("hello 1 port=50123 code=K7QXM2PD9TRW0123456789AB"), &l), "a NUL refused");
    char hi[] = "quit";
    hi[1] = (char)0xD5;
    EXPECT(!parse_line(hi, 4, &l), "a byte above ASCII refused");
    std::string longline(200, 'q');
    EXPECT(!parse_line(longline.c_str(), longline.size(), &l), "an oversize line refused");
    std::string ok127 = std::string("hello 1 port=50123 code=") + code;
    EXPECT(ok127.size() < kMaxLine, "the longest valid line fits (%u)", (unsigned)ok127.size());

    // The reader: lines across reads, an oversize line dropped to its newline.
    {
        Collected c;
        LineReader r;
        std::string all = std::string(text) + "\nqu";
        r.feed((const uint8_t*)all.data(), all.size(), collect, &c);
        r.feed((const uint8_t*)"it\n", 3, collect, &c);
        std::string big(300, 'x');
        big += "\nquit\n\n";
        r.feed((const uint8_t*)big.data(), big.size(), collect, &c);
        EXPECT(c.got.size() == 5, "five lines (%u)", (unsigned)c.got.size());
        EXPECT(c.got.size() == 5 && c.got[0].first && c.got[0].second.kind == LINE_HELLO && c.got[1].first &&
                   c.got[1].second.kind == LINE_QUIT && !c.got[2].first && c.got[3].first &&
                   c.got[3].second.kind == LINE_QUIT && !c.got[4].first,
               "hello, quit across reads, the long line refused, the next good, an empty line refused");
        Collected d;
        LineReader r2;
        std::string with_nul = std::string("qu") + '\0' + "it\nquit\n";
        r2.feed((const uint8_t*)with_nul.data(), with_nul.size(), collect, &d);
        EXPECT(d.got.size() == 2 && !d.got[0].first && d.got[1].first, "a line with a NUL refused, the next taken");
        Collected e;
        LineReader r3;
        std::string hello_line = std::string(text) + "\n";
        r3.feed((const uint8_t*)hello_line.data(), hello_line.size(), collect, &e);
        bool zero = true;
        for (size_t i = 0; i < sizeof r3.buf; i++)
            zero = zero && r3.buf[i] == 0;
        EXPECT(e.got.size() == 1 && e.got[0].first && zero,
               "the reader keeps no copy of a line once it is taken");
    }

    // The password: 24 symbols of the alphabet, never the same twice.
    {
        char a[kCodeLen + 1], b[kCodeLen + 1];
        bool valid = true, differ = true;
        for (int i = 0; i < 100; i++)
        {
            valid = valid && new_code(a) && code_valid(a) && new_code(b) && code_valid(b);
            differ = differ && strcmp(a, b) != 0;
        }
        EXPECT(valid && differ, "fresh passwords: 24 symbols, all different");
        EXPECT(!code_valid("K7QXM2PD9TRW0123456789A") && !code_valid("K7QXM2PD9TRW0123456789AI") && !code_valid(""),
               "code_valid refuses short and foreign symbols");
        Line h;
        memset(&h, 0, sizeof h);
        h.kind = LINE_HELLO;
        h.port = 1023;
        memcpy(h.code, a, sizeof h.code);
        EXPECT(format_line(h, back, sizeof back) == 0, "no hello is written for a port under 1024");
        h.port = 40000;
        EXPECT(format_line(h, back, 20) == 0, "nor into a buffer too small");
    }

    // Player 2's environment.
    {
        std::vector<std::string> parent = {
            "PATH=C:\\Windows",
            "=C:=C:\\games",
            "TSSM_ASSETS=E:\\assets",
            "TSSM_COOP=host",
            "tssm_coop_password=secret",
            "TSSM_TEST_KILL=2050:30",
            "TSSM_SAVE_DIR=E:\\run\\save",
            "TSSM_LOG=E:\\run\\tssm.log",
            "TSSM_CONFIG=E:\\run\\config.ini",
            "TSSM_INPUT=10:a",
            "TSSM_PERF_CSV=E:\\run\\perf.csv",
            "TSSM_DUMPSHADOW=1",
            "TSSM_LOCAL_COOP=1",
            "TSSM_AUDIO=1",
            "TSSM_CHILD_TSSM_INPUT=20:b",
            "TSSM_CHILD_TSSM_AUDIO=0",
            "TSSM_CHILD_TSSM_SAVE_DIR=E:\\elsewhere",
            "TSSM_CHILD_TSSM_CHILD_X=1",
            "TSSM_CHILD_=1",
            "NOEQUALS",
        };
        std::vector<std::string> set = { "TSSM_LOCAL_COOP=2", "TSSM_SAVE_DIR=E:\\run\\local2\\saves",
                                         "TSSM_LOG=E:\\run\\local2\\last_run.log" };
        std::vector<std::string> env = child_env(parent, set);
        EXPECT(has_var(env, "PATH=C:\\Windows") && has_var(env, "=C:=C:\\games") && has_var(env, "TSSM_ASSETS=E:\\assets"),
               "ordinary names kept, the drive folder too");
        EXPECT(!has_name(env, "TSSM_COOP") && !has_name(env, "tssm_coop_password") &&
                   !has_name(env, "TSSM_TEST_KILL") && !has_name(env, "TSSM_CONFIG") &&
                   !has_name(env, "TSSM_PERF_CSV") && !has_name(env, "TSSM_DUMPSHADOW"),
               "Player 1's co-op, test, config and file names dropped (any case)");
        EXPECT(has_var(env, "TSSM_INPUT=20:b") && has_var(env, "TSSM_AUDIO=0"),
               "TSSM_CHILD_ names passed on, over Player 1's own");
        EXPECT(has_var(env, "TSSM_SAVE_DIR=E:\\run\\local2\\saves") && has_var(env, "TSSM_LOG=E:\\run\\local2\\last_run.log") &&
                   has_var(env, "TSSM_LOCAL_COOP=2") && !has_var(env, "TSSM_SAVE_DIR=E:\\elsewhere"),
               "the set names win over everything");
        bool clean = true;
        for (auto& v : env) clean = clean && v.compare(0, 11, "TSSM_CHILD_") != 0 && v.find('=', 1) != std::string::npos;
        EXPECT(clean && !has_name(env, "NOEQUALS"), "no TSSM_CHILD_ name and no entry without '=' reaches Player 2");

        std::vector<char> block;
        EXPECT(env_block({ "b=2", "A=1", "_x=3", "=C:=C:\\" }, &block), "block made");
        const char want[] = "=C:=C:\\\0A=1\0b=2\0_x=3\0";
        EXPECT(block.size() == sizeof want && !memcmp(block.data(), want, sizeof want),
               "sorted by name ignoring case, a NUL after each, one more at the end (%u bytes)",
               (unsigned)block.size());
        EXPECT(!env_block({ "A=1", "a=2" }, &block), "two of one name refused");
        EXPECT(!env_block({ "A=1", "B" }, &block), "an entry without '=' refused");
        EXPECT(!env_block({ std::string("A=1\0x", 5) }, &block), "an entry with a NUL refused");
        EXPECT(env_block({}, &block) && block.size() == 2 && block[0] == 0 && block[1] == 0, "an empty block is two NULs");
        EXPECT(env_block(env, &block) && block.size() > 2 && block[block.size() - 1] == 0 && block[block.size() - 2] == 0,
               "Player 2's block made");
    }

    // Player 2's folder.
    {
        char out[256];
        EXPECT(profile_dir("C:/Users/A/AppData/Roaming/tssm/saves", out, sizeof out) &&
                   !strcmp(out, "C:/Users/A/AppData/Roaming/tssm/local2"),
               "the default save folder: %s", out);
        EXPECT(profile_dir("E:\\run\\host\\save\\", out, sizeof out) && !strcmp(out, "E:/run/host/local2"),
               "a test's folder, backslashes and a trailing one: %s", out);
        EXPECT(profile_dir("E:/save", out, sizeof out) && !strcmp(out, "E:/local2"), "a drive's top: %s", out);
        EXPECT(profile_dir("saves", out, sizeof out) && !strcmp(out, "./local2"), "a relative folder: %s", out);
        EXPECT(!profile_dir("", out, sizeof out) && !profile_dir("E:/a/save", out, 6), "empty or too long refused");
        EXPECT(scene_text_valid("DE01") && scene_text_valid("BB02") && !scene_text_valid("de01") &&
                   !scene_text_valid("soak") && !scene_text_valid("DE0") && !scene_text_valid("DE012") &&
                   !scene_text_valid("DE 1"),
               "boot scenes: four capitals or digits");
    }

    // Hosting on 127.0.0.1 at port 0 (the system picks), as Player 1 does; a
    // join with the one-time password; a wrong one refused; five wrong ones
    // block the address, and hosting again (Bring Player 2 back: a new port
    // and password) lets Player 2 in from that same address.
    {
        static Rig r;
        sha256("local co-op files", 17, r.assets);
        char code1[kCodeLen + 1], code2[kCodeLen + 1];
        EXPECT(new_code(code1) && new_code(code2), "two passwords");
        EXPECT(host_start(r, 1, code1, kMinIters), "host on 127.0.0.1, port 0");
        uint16_t port1 = r.he.addr.port;
        char where[64];
        addr_format(r.he.addr, where, sizeof where);
        EXPECT(port1 >= 1024 && port1 != kDefaultPort, "the system's port read back: %s", where);
        NetAddr lo;
        addr_parse("127.0.0.1", port1, &lo);
        EXPECT(addr_equal(lo, r.he.addr), "the host's address is 127.0.0.1 (%s)", where);

        Endpoint p2;
        EXPECT(p2.open(1), "Player 2's socket on 127.0.0.1");
        static Client c;
        uint8_t reasons[6] = {};
        for (int i = 0; i < 6; i++)
        {
            char wrong[kCodeLen + 1];
            new_code(wrong);
            connect(r, c, p2, nullptr, wrong, "Prober", kBuildId, r.assets);
            reasons[i] = c.state() == ClientState::Failed ? c.fail_reason() : 0;
        }
        EXPECT(reasons[0] == REJ_PASSWORD && reasons[3] == REJ_PASSWORD && reasons[4] == REJ_BLOCKED,
               "wrong passwords refused, the 5th blocks 127.0.0.1 (%u %u %u %u %u)", reasons[0], reasons[1],
               reasons[2], reasons[3], reasons[4]);
        connect(r, c, p2, nullptr, code1, "Player 2", kBuildId, r.assets);
        EXPECT(c.state() == ClientState::Failed && c.fail_reason() == REJ_BLOCKED,
               "then even the right password from 127.0.0.1 is blocked (%u)", c.fail_reason());

        // Bring back: the same Host hosts again, on a fresh port-0 socket.
        udp_close(&r.he.sock);
        EXPECT(host_start(r, 1, code2, kMinIters), "hosting again");
        EXPECT(r.he.addr.port >= 1024 && r.he.addr.port != kDefaultPort, "a new port read back (%u, was %u)",
               (unsigned)r.he.addr.port, (unsigned)port1);
        connect(r, c, p2, nullptr, code1, "Player 2", kBuildId, r.assets);
        EXPECT(c.state() == ClientState::Failed && c.fail_reason() == REJ_PASSWORD,
               "the old password is refused by the new session (%u)", c.fail_reason());
        EXPECT(connect(r, c, p2, nullptr, code2, "Player 2", kBuildId, r.assets) && r.host.session().active,
               "Player 2 joins the new session from 127.0.0.1");
        printf("  port-0 host on %s, then port %u; wrong passwords %u %u %u %u %u; Player 2 back in\n", where,
               (unsigned)r.he.addr.port, reasons[0], reasons[1], reasons[2], reasons[3], reasons[4]);
    }
}

// tssm_nettest --wrong-proofs 127.0.0.1:PORT BUILDHEX FILESHEX N: a client
// with a wrong password, N times, against a game hosting for local co-op
// (tools/coop_test.py's local-bringback). Loopback only.
static int wrong_proofs(const char* address, const char* buildHex, const char* filesHex, int count)
{
    auto unhex = [](const char* h, uint8_t* out, size_t n) {
        if (strlen(h) != 2 * n) return false;
        for (size_t i = 0; i < n; i++)
        {
            unsigned v;
            if (sscanf(h + 2 * i, "%2x", &v) != 1) return false;
            out[i] = (uint8_t)v;
        }
        return true;
    };
    uint8_t build[kBuildIdSize], files[kHashSize];
    NetAddr to;
    if (!addr_parse(address, 0, &to) || !addr_is_v4(to) || to.ip[12] != 127 || !unhex(buildHex, build, sizeof build) ||
        !unhex(filesHex, files, sizeof files) || count < 1 || count > 20)
    {
        printf("--wrong-proofs: 127.x.x.x:PORT, the build id and the game files in hex, a count 1-20\n");
        return 2;
    }
    if (!net_init()) return 2;
    Endpoint e;
    if (!e.open(1)) return 2;
    int refused = 0;
    for (int i = 0; i < count; i++)
    {
        static Client c;
        char wrong[kCodeLen + 1];
        new_code(wrong);
        NetIO io = { &e, ep_send, ep_state, nullptr };
        auto ns = [] { return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch()).count(); };
        if (!c.connect(to, wrong, "Prober", build, files, io, ns())) return 2;
        double t0 = wall_ms();
        while (c.state() != ClientState::Failed && c.state() != ClientState::Connected && wall_ms() - t0 < 12000)
        {
            drain(e, [&](const NetAddr& f, const uint8_t* d, size_t n) { c.on_datagram(f, d, n, ns()); }, 5);
            c.update(ns());
        }
        bool no = c.state() == ClientState::Failed && c.fail() == FAIL_REJECTED;
        refused += no;
        printf("wrong proof %d: %s\n", i + 1,
               no ? reject_text(c.fail_reason()) : c.state() == ClientState::Connected ? "LET IN" : "no answer");
    }
    net_shutdown();
    return refused == count ? 0 : 1;
}

// ---------------------------------------------------------------------------
// 6. Fuzz loop

static void mutate(std::vector<uint8_t>& d, Rng& rng, const std::vector<std::vector<uint8_t>>& seeds)
{
    static const uint8_t interesting8[] = { 0, 1, 2, 0x7F, 0x80, 0xFF, 0x10, 0x40, 0x60, 0x1F, 0x20, 0x5F, 41, 42, 43, 33 };
    static const uint16_t interesting16[] = { 0, 1, 0xFFFF, 0x8000, 0x7FFF, 41, 42, 43, 1023, 1024, 1200, 1201, 1167, 4096 };
    static const uint32_t floats[] = { 0x7FC00000, 0x7F800000, 0xFF800000, 0x7F7FFFFF, 0x47C35000, 0x47C35001,
                                       0x80000000, 0xBF800000, 0x44160000, 0x44164000 };
    int rounds = 1 + (int)rng.below(4);
    for (int k = 0; k < rounds; k++)
    {
        size_t n = d.size();
        switch (rng.below(9))
        {
        case 0:
            if (n)
                for (int i = 0, m = 1 + (int)rng.below(8); i < m; i++) d[rng.below((uint32_t)n)] ^= (uint8_t)(1u << rng.below(8));
            break;
        case 1:
            if (n) d[rng.below((uint32_t)n)] = interesting8[rng.below(sizeof interesting8)];
            break;
        case 2:
            if (n) d.resize(rng.below((uint32_t)n + 1));
            break;
        case 3:
        {
            size_t add = rng.below(64);
            while (add-- && d.size() < kMaxDatagram + 40) d.push_back((uint8_t)rng.next());
            break;
        }
        case 4:
            if (n >= 2)
            {
                size_t at = rng.below((uint32_t)n - 1);
                uint16_t v = interesting16[rng.below(sizeof interesting16 / 2)];
                d[at] = (uint8_t)v;
                d[at + 1] = (uint8_t)(v >> 8);
            }
            break;
        case 5:
            if (d.size() < kMaxDatagram + 40)
            {
                size_t at = n ? rng.below((uint32_t)n + 1) : 0;
                d.insert(d.begin() + at, (uint8_t)rng.next());
            }
            break;
        case 6:
        {
            const auto& o = seeds[rng.below((uint32_t)seeds.size())];
            if (!o.empty() && n)
            {
                size_t at = rng.below((uint32_t)n);
                size_t from = rng.below((uint32_t)o.size());
                size_t len = rng.below(32);
                for (size_t i = 0; i < len && at + i < n && from + i < o.size(); i++) d[at + i] = o[from + i];
            }
            break;
        }
        case 7:
            if (rng.below(16) == 0)
            {
                d.resize(rng.below(kMaxDatagram + 40));
                for (auto& x : d) x = (uint8_t)rng.next();
            }
            break;
        case 8:
            if (n >= 4)
            {
                size_t at = rng.below((uint32_t)n - 3);
                uint32_t v = floats[rng.below(sizeof floats / 4)];
                memcpy(&d[at], &v, 4);
            }
            break;
        }
    }
}

static void test_fuzz(uint64_t iters, Rng& rng)
{
    printf("[6] fuzz: %llu mutated inputs\n", (unsigned long long)iters);
    static FuzzRig rig;
    if (!rig.init())
    {
        EXPECT(false, "fuzz rig");
        return;
    }
    auto seeds = fuzz_seeds(rig);
    double t0 = wall_ms();
    std::vector<uint8_t> d;
    for (uint64_t i = 0; i < iters; i++)
    {
        uint8_t target = (uint8_t)rng.below(FT_COUNT);
        d = seeds[rng.below((uint32_t)seeds.size())];
        if (rng.below(32)) mutate(d, rng, seeds); // 1 in 32 unmutated, so the deep paths are reached
        rig.run(target, d.data(), d.size());
    }
    double ms = wall_ms() - t0;
    static const char* names[] = { "handshake parsers",     "payload parser",
                                   "host, raw datagram",    "guest, raw datagram",
                                   "host, sealed hostile",  "guest, sealed hostile",
                                   "address parser",        "local pipe lines",
                                   "local environment",     "join codes",
                                   "home network messages", "home network host",
                                   "home network joiner" };
    for (int t = 0; t < FT_COUNT; t++)
        printf("  %-24s tried %8llu  accepted %8llu\n", names[t], (unsigned long long)rig.tried[t],
               (unsigned long long)rig.accepted[t]);
    printf("  %.1f s; the session was set up again %llu times after hostile input ended it\n", ms / 1000,
           (unsigned long long)rig.rehandshakes);
    printf("  snapshots: %llu parsed, %llu delivered to a guest; %llu acknowledgements delivered to a host\n",
           (unsigned long long)rig.snapshots_parsed, (unsigned long long)rig.snapshots_delivered,
           (unsigned long long)rig.acks_delivered);
    EXPECT(rig.snapshots_parsed > 0 && rig.snapshots_delivered > 0 && rig.acks_delivered > 0,
           "the fuzzer reached the snapshot and acknowledgement paths");
    printf("  M3: %llu payloads with events, acknowledgements or pickups reached a session; %llu events "
           "delivered\n",
           (unsigned long long)rig.events_seen, (unsigned long long)rig.events_delivered);
    printf("  M4: %llu looks taken by the receiver's inbox (%u past the rate)\n", (unsigned long long)rig.looks_taken,
           (unsigned)rig.looks.over);
    EXPECT(rig.events_seen > 0 && rig.events_delivered > 0, "the fuzzer reached the event and pickup paths");
    printf("  the home network: %llu answers to questions, %llu rows listed over the inputs\n",
           (unsigned long long)rig.disc_answered, (unsigned long long)rig.disc_listed);
    printf("  protocol 7: %llu COUNTERS merged with an older one of the same table\n",
           (unsigned long long)rig.cnt_merged);
    EXPECT(rig.cnt_merged > 0, "the fuzzer reached the COUNTERS merge");
    EXPECT(rig.accepted[FT_PARSE_PAYLOAD] > 0 && rig.accepted[FT_PARSE_PRE] > 0 &&
               rig.accepted[FT_HOST_SEALED] > 0 && rig.accepted[FT_CLIENT_SEALED] > 0 &&
               rig.accepted[FT_ADDR] > 0 && rig.accepted[FT_LOCAL_LINE] > 0 &&
               rig.accepted[FT_LOCAL_ENV] > 0 && rig.accepted[FT_CODE] > 0 &&
               rig.accepted[FT_DISC_PARSE] > 0 && rig.accepted[FT_DISC_RESPONDER] > 0 &&
               rig.accepted[FT_DISC_FINDER] > 0,
           "the fuzzer reached the accepting paths");
}

int main(int argc, char** argv)
{
    if (argc == 6 && !strcmp(argv[1], "--wrong-proofs"))
    {
        setvbuf(stdout, nullptr, _IONBF, 0);
        return wrong_proofs(argv[2], argv[3], argv[4], atoi(argv[5]));
    }
    uint64_t fuzz = 200000, seed = 0x5EED;
    const char* seeds_dir = nullptr;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        if (!strcmp(argv[i], "--fuzz")) fuzz = strtoull(argv[i + 1], nullptr, 10);
        else if (!strcmp(argv[i], "--seed")) seed = strtoull(argv[i + 1], nullptr, 0);
        else if (!strcmp(argv[i], "--seeds")) seeds_dir = argv[i + 1];
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (seeds_dir)
    {
        // The valid seed messages as a libFuzzer corpus, one file per (target,
        // seed): the target byte first, as tssm_netfuzz expects.
        static FuzzRig rig;
        if (!rig.init()) return 2;
        auto seeds = fuzz_seeds(rig);
        int k = 0;
        for (int t = 0; t < FT_COUNT; t++)
            for (auto& sd : seeds)
            {
                char path[512];
                snprintf(path, sizeof path, "%s/seed_%02d_%03d", seeds_dir, t, k++);
                FILE* f = fopen(path, "wb");
                if (!f) return 2;
                uint8_t tb = (uint8_t)t;
                fwrite(&tb, 1, 1, f);
                fwrite(sd.data(), 1, sd.size(), f);
                fclose(f);
            }
        printf("wrote %d seeds\n", k);
        return 0;
    }
    if (!net_init())
    {
        printf("tssm_nettest: the socket library did not start\n");
        return 2;
    }
    Rng rng{ seed };
    double t0 = wall_ms();
    test_crypto();
    test_wire();
    test_snapshot();
    test_events();
    test_m4_events();
    test_identity();
    test_discovery();
    test_reliable(rng);
    test_session(rng);
    test_hostile(rng);
    test_m5();
    test_fuzz(fuzz, rng);
    test_local();
    net_shutdown();
    printf("tssm_nettest: %d of %d checks passed (%.1f s)\n", sChecks - sFailures, sChecks, (wall_ms() - t0) / 1000);
    return sFailures ? 1 : 0;
}
