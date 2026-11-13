// PC-only, co-op: the wire protocol (iNetProto.h, docs/MULTIPLAYER.md 4.4).
#include "iNetProto.h"

#include "iNetCrypto.h"
#include "iNetWire.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace iNet {

const uint8_t kMagic[4] = { 'T', 'S', 'C', 'P' };

const char* reject_text(uint8_t r)
{
    switch (r)
    {
    case REJ_VERSION:
    case REJ_BUILD:
        return "the host's game is a different version of the port. Both players need the same release: the "
               "\"co-op version\" at the bottom of both Co-op tabs must be the same";
    case REJ_ASSETS:
        return "your game files differ from the host's. Both players need the same release and region of the "
               "game, and the same mods turned on (or none)";
    case REJ_FULL: return "the host's game already has a player (co-op is for two)";
    case REJ_PASSWORD:
        return "wrong join code or password. Check it with the host: their Co-op tab shows the join code "
               "(it changes when they make a new one)";
    case REJ_BLOCKED:
        return "this address was blocked by the host (too many wrong passwords, or the host removed you). Wait a "
               "minute and try again, or ask the host";
    case REJ_BUSY: return "the host is refusing joins for a minute (too many wrong passwords). Try again in a minute";
    default: return "refused";
    }
}

void build_id(const char* coop_id, bool test_salt, uint8_t out[kBuildIdSize])
{
    char text[160];
    snprintf(text, sizeof(text), "tssm-coop build/2 %s proto %u%s", coop_id,
             (unsigned)kProtoVersion, test_salt ? " test-salt" : "");
    uint8_t full[32];
    sha256(text, strlen(text), full);
    memcpy(out, full, kBuildIdSize);
}

void version_code(const uint8_t id[kBuildIdSize], char out[9])
{
    static const char kHex[] = "0123456789abcdef";
    for (int i = 0; i < 4; i++)
    {
        out[2 * i] = kHex[id[i] >> 4];
        out[2 * i + 1] = kHex[id[i] & 15];
    }
    out[8] = '\0';
}

bool normalize_code(const char* in, char* out, size_t out_size)
{
    if (out_size == 0)
        return false;
    size_t n = 0;
    for (const char* c = in; *c != '\0'; c++)
    {
        char ch = *c;
        if (ch == ' ' || ch == '-' || ch == '\t')
            continue;
        if (ch >= 'a' && ch <= 'z')
            ch = (char)(ch - 'a' + 'A');
        if (ch == 'O')
            ch = '0';
        else if (ch == 'I' || ch == 'L')
            ch = '1';
        if (n + 1 >= out_size)
        {
            memset(out, 0, out_size); // no part of a password left behind
            return false;
        }
        out[n++] = ch;
    }
    out[n] = '\0';
    return n >= 1 && n <= kMaxPassword;
}

bool name_valid(const char* s, size_t n)
{
    if (n < 1 || n > kMaxName) return false;
    if (s[0] == ' ' || s[n - 1] == ' ') return false;
    for (size_t i = 0; i < n; i++)
    {
        char c = s[i];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                  c == ' ' || c == '_' || c == '.' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Handshake

static void put_pre(Writer& w, uint8_t type, uint16_t version = kProtoVersion)
{
    w.bytes(kMagic, 4);
    w.u16(version);
    w.u8(type);
}

// Reads the header and requires this version and this type.
static bool get_pre(Reader& r, uint8_t type)
{
    const uint8_t* m = r.take(4);
    if (!m || memcmp(m, kMagic, 4) != 0) return false;
    if (r.u16() != kProtoVersion) return false;
    if (r.u8() != type) return false;
    return r.ok;
}

bool peek_pre(const uint8_t* d, size_t n, uint16_t* version, uint8_t* type)
{
    if (n < kPreHeader || memcmp(d, kMagic, 4) != 0) return false;
    *version = (uint16_t)(d[4] | d[5] << 8);
    *type = d[6];
    return true;
}

bool peek_hello_nonce(const uint8_t* d, size_t n, uint8_t nonce[kNonceSize])
{
    if (n != kHelloSize || memcmp(d, kMagic, 4) != 0 || d[6] != PKT_HELLO) return false;
    memcpy(nonce, d + kPreHeader, kNonceSize);
    return true;
}

static void put_name(Writer& w, uint8_t len, const char* name)
{
    w.u8(len);
    w.bytes(name, len);
}

static bool get_name(Reader& r, uint8_t* len, char* name)
{
    uint8_t n = r.u8();
    if (!r.ok || n < 1 || n > kMaxName) return false;
    r.bytes(name, n);
    name[n] = 0;
    if (!r.ok || !name_valid(name, n)) return false;
    *len = n;
    return true;
}

size_t encode_hello(uint8_t* out, size_t cap, const Hello& m)
{
    Writer w(out, cap);
    put_pre(w, PKT_HELLO, m.version);
    w.bytes(m.client_nonce, kNonceSize);
    w.bytes(m.build_id, kBuildIdSize);
    w.bytes(m.asset_hash, kHashSize);
    if (w.ok && w.len < kHelloSize) w.zeros(kHelloSize - w.len);
    return w.ok ? w.len : 0;
}

bool parse_hello(const uint8_t* d, size_t n, Hello* m)
{
    if (n != kHelloSize) return false; // anti-amplification: only full-size HELLOs get an answer
    Reader r(d, n);
    if (!get_pre(r, PKT_HELLO)) return false;
    m->version = kProtoVersion;
    r.bytes(m->client_nonce, kNonceSize);
    r.bytes(m->build_id, kBuildIdSize);
    r.bytes(m->asset_hash, kHashSize);
    r.zeros(r.left());
    return r.done();
}

size_t encode_challenge(uint8_t* out, size_t cap, const Challenge& m)
{
    Writer w(out, cap);
    put_pre(w, PKT_CHALLENGE);
    w.bytes(m.client_nonce, kNonceSize);
    w.bytes(m.cookie, kCookieSize);
    w.u32(m.cookie_time);
    w.bytes(m.salt, kSaltSize);
    w.u32(m.iters);
    w.bytes(m.host_nonce, kNonceSize);
    return w.ok ? w.len : 0;
}

bool parse_challenge(const uint8_t* d, size_t n, Challenge* m)
{
    Reader r(d, n);
    if (!get_pre(r, PKT_CHALLENGE)) return false;
    r.bytes(m->client_nonce, kNonceSize);
    r.bytes(m->cookie, kCookieSize);
    m->cookie_time = r.u32();
    r.bytes(m->salt, kSaltSize);
    m->iters = r.u32();
    // A hostile "host" must not be able to make the client spin for minutes.
    if (m->iters < kMinIters || m->iters > kMaxIters) r.fail();
    r.bytes(m->host_nonce, kNonceSize);
    return r.done();
}

size_t encode_auth(uint8_t* out, size_t cap, const Auth& m)
{
    Writer w(out, cap);
    put_pre(w, PKT_AUTH);
    w.bytes(m.client_nonce, kNonceSize);
    w.bytes(m.host_nonce, kNonceSize);
    w.bytes(m.cookie, kCookieSize);
    w.u32(m.cookie_time);
    w.bytes(m.build_id, kBuildIdSize);
    w.bytes(m.asset_hash, kHashSize);
    put_name(w, m.name_len, m.name);
    w.bytes(m.proof, kProofSize);
    return w.ok ? w.len : 0;
}

bool parse_auth(const uint8_t* d, size_t n, Auth* m)
{
    Reader r(d, n);
    memset(m, 0, sizeof *m);
    if (!get_pre(r, PKT_AUTH)) return false;
    r.bytes(m->client_nonce, kNonceSize);
    r.bytes(m->host_nonce, kNonceSize);
    r.bytes(m->cookie, kCookieSize);
    m->cookie_time = r.u32();
    r.bytes(m->build_id, kBuildIdSize);
    r.bytes(m->asset_hash, kHashSize);
    if (!get_name(r, &m->name_len, m->name)) return false;
    r.bytes(m->proof, kProofSize);
    return r.done();
}

size_t encode_welcome(uint8_t* out, size_t cap, const Welcome& m)
{
    Writer w(out, cap);
    put_pre(w, PKT_WELCOME);
    w.u32(m.session_id);
    w.u8(m.slot);
    put_name(w, m.name_len, m.name);
    w.bytes(m.proof, kProofSize);
    return w.ok ? w.len : 0;
}

bool parse_welcome(const uint8_t* d, size_t n, Welcome* m)
{
    Reader r(d, n);
    memset(m, 0, sizeof *m);
    if (!get_pre(r, PKT_WELCOME)) return false;
    m->session_id = r.u32();
    if (m->session_id == 0) r.fail();
    m->slot = r.u8();
    if (m->slot < 1 || m->slot >= kMaxPlayers) r.fail(); // slot 0 is the host
    if (!get_name(r, &m->name_len, m->name)) return false;
    r.bytes(m->proof, kProofSize);
    return r.done();
}

size_t encode_reject(uint8_t* out, size_t cap, const Reject& m)
{
    Writer w(out, cap);
    put_pre(w, PKT_REJECT, m.version);
    w.bytes(m.client_nonce, kNonceSize);
    w.u8(m.reason);
    return w.ok ? w.len : 0;
}

bool parse_reject(const uint8_t* d, size_t n, Reject* m)
{
    // Accepted from any version, so a client can say "update the game".
    Reader r(d, n);
    const uint8_t* mg = r.take(4);
    if (!mg || memcmp(mg, kMagic, 4) != 0) return false;
    m->version = r.u16();
    if (r.u8() != PKT_REJECT) return false;
    r.bytes(m->client_nonce, kNonceSize);
    m->reason = r.u8();
    if (m->reason < 1 || m->reason >= REJ_END) r.fail();
    return r.done();
}

// ---------------------------------------------------------------------------
// Payload

// Everything but the snapshot's entries, which are only ever read up to its
// count (3.5 KB that a payload without a SNAPSHOT never touches).
void payload_clear(Payload* p)
{
    memset(p, 0, offsetof(Payload, snap) + offsetof(Snapshot, e));
    memset((uint8_t*)p + offsetof(Payload, snap_ack), 0, sizeof(Payload) - offsetof(Payload, snap_ack));
}

bool scene_valid(uint32_t t)
{
    for (int i = 0; i < 4; i++)
    {
        uint8_t c = (uint8_t)(t >> (8 * i));
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

static bool pstate_fields_valid(const PlayerState& s)
{
    if (s.slot >= kMaxPlayers || !scene_valid(s.scene) || s.character >= CHAR_END) return false;
    if (s.flags & ~PS_FLAG_MASK) return false;
    if (s.model >= kModelEnd && s.model != kModelUnknown) return false;
    for (int i = 0; i < 3; i++)
        if (!(s.pos[i] >= -kWorldLimit && s.pos[i] <= kWorldLimit)) return false; // NaN fails too
    if (!(s.anim_time >= 0.0f && s.anim_time <= kAnimTimeLimit)) return false;
    return s.health <= kPlayerHealthMax;
}

static void put_pstate(Writer& w, const PlayerState& s)
{
    w.u8(s.slot);
    w.u32(s.frame);
    w.u32(s.scene);
    w.u8(s.character);
    w.u8(s.flags);
    for (int i = 0; i < 3; i++) w.f32(s.pos[i]);
    w.u16(s.yaw);
    for (int i = 0; i < 3; i++) w.i16(s.vel[i]);
    w.u32(s.anim);
    w.f32(s.anim_time);
    w.u8(s.health);
    w.u16(s.parts);
    w.u8(s.model);
}

static void get_pstate(Reader& r, PlayerState* s)
{
    memset(s, 0, sizeof *s);
    s->slot = r.u8_below(kMaxPlayers);
    s->frame = r.u32();
    s->scene = r.u32();
    if (!scene_valid(s->scene)) r.fail();
    s->character = r.u8_below(CHAR_END);
    s->flags = r.u8();
    if (s->flags & ~PS_FLAG_MASK) r.fail();
    for (int i = 0; i < 3; i++) s->pos[i] = r.f32(kWorldLimit);
    s->yaw = r.u16();
    for (int i = 0; i < 3; i++) s->vel[i] = r.i16();
    s->anim = r.u32();
    s->anim_time = r.f32(kAnimTimeLimit);
    if (s->anim_time < 0.0f) r.fail();
    s->health = r.u8();
    if (s->health > kPlayerHealthMax) r.fail();
    s->parts = r.u16();
    s->model = r.u8();
    if (s->model >= kModelEnd && s->model != kModelUnknown) r.fail();
}

size_t encode_player_state(const PlayerState& s, uint8_t* out, size_t cap)
{
    if (!pstate_fields_valid(s)) return 0;
    Writer w(out, cap);
    put_pstate(w, s);
    return w.ok ? w.len : 0;
}

bool parse_player_state(const uint8_t* d, size_t n, PlayerState* s)
{
    if (n != kPlayerStateSize) return false;
    Reader r(d, n);
    get_pstate(r, s);
    return r.done();
}

// --- SNAPSHOT ----------------------------------------------------------------------

size_t snap_entry_size(uint8_t mask)
{
    size_t n = 3;
    if (mask & SE_POS) n += 12;
    if (mask & SE_ROT) n += 8;
    if (mask & SE_ANIM) n += 8;
    if (mask & SE_FLAGS) n += 1;
    if (mask & SE_ALPHA) n += 1;
    return n;
}

size_t snapshot_size(const Snapshot& s)
{
    size_t n = kSnapHeaderSize;
    for (unsigned i = 0; i < s.count && i < kMaxSnapEntries; i++) n += snap_entry_size(s.e[i].mask);
    return n;
}

static bool rot_valid(const int16_t r[4])
{
    bool any = false;
    for (int i = 0; i < 4; i++)
    {
        if (r[i] == -32768) return false; // so +-32767 is the only full scale (canonical)
        if (r[i] != 0) any = true;
    }
    return any;
}

// The base: none, or a snapshot 1..kSnapBaseWindow before this one.
static bool snap_base_valid(uint32_t seq, uint32_t base)
{
    if (base == 0) return true;
    uint32_t back = seq - base;
    return back >= 1 && back <= kSnapBaseWindow;
}

static bool snap_entry_valid(const SnapEntry& e, uint16_t things)
{
    if (e.id >= things || e.mask == 0 || (e.mask & ~SE_MASK)) return false;
    if (e.mask & SE_POS)
        for (int i = 0; i < 3; i++)
            if (!(e.pos[i] >= -kWorldLimit && e.pos[i] <= kWorldLimit)) return false;
    if ((e.mask & SE_ROT) && !rot_valid(e.rot)) return false;
    if ((e.mask & SE_ANIM) && !(e.anim_time >= 0.0f && e.anim_time <= kAnimTimeLimit)) return false;
    if ((e.mask & SE_FLAGS) && (e.flags & ~WF_MASK)) return false;
    return true;
}

static bool snapshot_valid(const Snapshot& s)
{
    if (!scene_valid(s.scene) || s.things < 1 || s.things > kMaxNetIds || s.seq == 0) return false;
    if (!snap_base_valid(s.seq, s.base) || (s.flags & ~SNAP_FLAG_MASK) || s.count > kMaxSnapEntries) return false;
    for (unsigned i = 0; i < s.count; i++)
    {
        if (!snap_entry_valid(s.e[i], s.things)) return false;
        if (i > 0 && s.e[i].id <= s.e[i - 1].id) return false; // ascending, no repeats
    }
    return true;
}

static void put_snapshot(Writer& w, const Snapshot& s)
{
    w.u32(s.scene);
    w.u32(s.table);
    w.u16(s.things);
    w.u32(s.seq);
    w.u32(s.base);
    w.u32(s.host_ms);
    w.u8(s.flags);
    w.u8(s.count);
    for (unsigned i = 0; i < s.count; i++)
    {
        const SnapEntry& e = s.e[i];
        w.u16(e.id);
        w.u8(e.mask);
        if (e.mask & SE_POS)
            for (int k = 0; k < 3; k++) w.f32(e.pos[k]);
        if (e.mask & SE_ROT)
            for (int k = 0; k < 4; k++) w.i16(e.rot[k]);
        if (e.mask & SE_ANIM)
        {
            w.u32(e.anim);
            w.f32(e.anim_time);
        }
        if (e.mask & SE_FLAGS) w.u8(e.flags);
        if (e.mask & SE_ALPHA) w.u8(e.alpha);
    }
}

// Reads a whole SNAPSHOT body; every rule is checked as it is read, the count
// against kMaxSnapEntries before the loop and each entry against the bytes left.
static void get_snapshot(Reader& r, Snapshot* s)
{
    s->scene = r.u32();
    if (!scene_valid(s->scene)) r.fail();
    s->table = r.u32();
    s->things = r.u16();
    if (s->things < 1 || s->things > kMaxNetIds) r.fail();
    s->seq = r.u32();
    if (s->seq == 0) r.fail();
    s->base = r.u32();
    if (!snap_base_valid(s->seq, s->base)) r.fail();
    s->host_ms = r.u32();
    s->flags = r.u8();
    if (s->flags & ~SNAP_FLAG_MASK) r.fail();
    s->count = r.u8();
    if (s->count > kMaxSnapEntries) r.fail();
    if (!r.ok)
    {
        s->count = 0;
        return;
    }
    for (unsigned i = 0; i < s->count && r.ok; i++)
    {
        SnapEntry& e = s->e[i];
        memset(&e, 0, sizeof e);
        e.id = r.u16();
        if (e.id >= s->things || (i > 0 && e.id <= s->e[i - 1].id)) r.fail();
        e.mask = r.u8();
        if (e.mask == 0 || (e.mask & ~SE_MASK)) r.fail();
        if (r.left() < snap_entry_size(e.mask) - 3) r.fail();
        if (!r.ok) break;
        if (e.mask & SE_POS)
            for (int k = 0; k < 3; k++) e.pos[k] = r.f32(kWorldLimit);
        if (e.mask & SE_ROT)
        {
            for (int k = 0; k < 4; k++) e.rot[k] = r.i16();
            if (!rot_valid(e.rot)) r.fail();
        }
        if (e.mask & SE_ANIM)
        {
            e.anim = r.u32();
            e.anim_time = r.f32(kAnimTimeLimit);
            if (e.anim_time < 0.0f) r.fail();
        }
        if (e.mask & SE_FLAGS)
        {
            e.flags = r.u8();
            if (e.flags & ~WF_MASK) r.fail();
        }
        if (e.mask & SE_ALPHA) e.alpha = r.u8();
    }
    if (!r.ok) s->count = 0;
}

size_t encode_snapshot(const Snapshot& s, uint8_t* out, size_t cap)
{
    if (!snapshot_valid(s)) return 0;
    Writer w(out, cap);
    put_snapshot(w, s);
    return w.ok ? w.len : 0;
}

bool parse_snapshot(const uint8_t* d, size_t n, Snapshot* s)
{
    memset(s, 0, offsetof(Snapshot, e));
    Reader r(d, n);
    get_snapshot(r, s);
    return r.done();
}

// --- EVENT, REL_ACK, PICKUPS (M3) ---------------------------------------------------

size_t event_size(uint8_t type)
{
    switch (type)
    {
    case EV_HIT: return 1 + 4 + 4 + 2 + 1 + 1 + 6 + 12;
    case EV_DAMAGE: return 1 + 4 + 2 + 1 + 1 + 1 + 12;
    case EV_PICKUP:
        return 1 + 4 + 4 + 2 + 1;
    case EV_GRANT: return 1 + 4 + 4 + 2 + 1;
    case EV_LEVEL:
    case EV_READY:
    case EV_GO: return 1 + 4;
    case EV_SHOW: return 1 + 4 + 4 + 2 + 1 + 1;
    case EV_LOOK: return 1 + 4 + 1 + 1 + 1;
    case EV_UPGRADE:
        return 1 + 4 + 1 + 1;
    case EV_UPGRADE_ACK:
        return 1 + 4 + 1 + 1 + 1;
    default: return 0;
    }
}

size_t event_msg_size(const GameEvent* ev, unsigned count)
{
    size_t n = 3 + kEventHeaderSize;
    for (unsigned i = 0; i < count; i++) n += event_size(ev[i].type);
    return n;
}

size_t pickups_msg_size(uint16_t count)
{
    return 3 + kPickupsHeaderSize + 2 * ((count + 7u) / 8u);
}

static bool dir_valid(const int16_t d[3])
{
    for (int i = 0; i < 3; i++)
        if (d[i] == -32768) return false; // +-32767 is the only full scale (canonical)
    return true;
}

static bool in_limit(const float v[3], float limit)
{
    for (int i = 0; i < 3; i++)
        if (!(v[i] >= -limit && v[i] <= limit)) return false;
    return true;
}

// Who may send which: the guest claims hits, pickups and moves, the host
// answers with damage, grants and its answers to those (M3, protocol 7); the
// host leads level changes and cutscenes, the guest says when it has loaded,
// and either tells the other how it looks (M4).
static bool event_from_ok(uint8_t type, Role from)
{
    switch (type)
    {
    case EV_HIT:
    case EV_PICKUP:
    case EV_UPGRADE:
    case EV_READY: return from == Role::Client;
    case EV_DAMAGE:
    case EV_GRANT:
    case EV_UPGRADE_ACK:
    case EV_LEVEL:
    case EV_GO:
    case EV_SHOW: return from == Role::Host;
    case EV_LOOK: return true;
    default: return false;
    }
}

bool event_valid(const GameEvent& e, Role from)
{
    if (!event_from_ok(e.type, from)) return false;
    if (!scene_valid(e.scene) && !(e.type == EV_LOOK && e.scene == 0)) return false;
    switch (e.type)
    {
    case EV_HIT:
        return e.id < kMaxNetIds && e.kind >= ATK_SIDE && e.kind < ATK_END && e.level >= kPowerupMin &&
               e.level <= kPowerupMax && dir_valid(e.dir) && in_limit(e.pos, kWorldLimit);
    case EV_DAMAGE:
        if (!(e.id < kMaxNetIds || e.id == kNoThing) || e.kind >= DK_END || e.level < 1 || e.level > kPlayerHealthMax)
            return false;
        if (e.kind == DK_FROM) return e.sub < kDamageSubEnd && in_limit(e.pos, kWorldLimit);
        if (e.sub != 0) return false;
        if (e.kind == DK_PUSH) return in_limit(e.pos, kPushLimit);
        return e.pos[0] == 0.0f && e.pos[1] == 0.0f && e.pos[2] == 0.0f && in_limit(e.pos, 0.0f);
    case EV_PICKUP:
        return e.id < kMaxPickups && e.kind < PW_END;
    case EV_GRANT: return e.id < kMaxPickups && e.kind >= GR_YOURS && e.kind < GR_END;
    case EV_UPGRADE:
    case EV_UPGRADE_ACK:
        if (e.kind < UPG_TAUGHT || e.kind >= UPG_END ||
            e.sub >= (e.kind == UPG_TAUGHT ? kMoves : kPowerUps))
            return false;
        return e.type == EV_UPGRADE ? true : (e.level >= GR_YOURS && e.level < GR_END);
    case EV_LEVEL:
    case EV_READY:
    case EV_GO: return true;
    case EV_SHOW:
        if (e.id >= kMaxShows || e.kind < SHOW_CUTSCENE_START || e.kind >= SHOW_END) return false;
        return e.kind == SHOW_CUTSCENE_START ? (e.level & ~SHOWF_MASK) == 0 : e.level == 0;
    case EV_LOOK: return e.kind < CHAR_END && e.level < kCostumeChoices && e.sub < kCostumeChoices;
    default: return false;
    }
}

bool hit_dir_unit(const int16_t dir[3], float out[3])
{
    double len = 0.0;
    for (int i = 0; i < 3; i++) len += (double)dir[i] * (double)dir[i];
    len = sqrt(len);
    for (int i = 0; i < 3; i++) out[i] = len > 0.0 ? (float)(dir[i] / len) : 0.0f;
    return len > 0.0;
}

void look_inbox_reset(LookInbox* in)
{
    memset(in, 0, sizeof(*in));
}

bool look_inbox_take(LookInbox* in, const GameEvent& e, uint64_t now_ns, bool* over_rate)
{
    if (over_rate != NULL) *over_rate = false;
    if (e.type != EV_LOOK || e.kind >= CHAR_END || e.level >= kCostumeChoices || e.sub >= kCostumeChoices)
    {
        return false;
    }
    if (in->in_window == 0 || now_ns - in->window_ns >= 1000000000ull)
    {
        in->window_ns = now_ns;
        in->in_window = 0;
    }
    if (in->in_window >= kLooksPerSecond)
    {
        in->over++;
        if (over_rate != NULL) *over_rate = true;
    }
    else
    {
        in->in_window++;
    }
    bool changed = !in->known || in->character != e.kind || in->costume[0] != e.level || in->costume[1] != e.sub ||
                   in->scene != e.scene;
    in->known = true;
    in->character = e.kind;
    in->costume[0] = e.level;
    in->costume[1] = e.sub;
    in->scene = e.scene;
    return changed;
}

void level_signals_reset(LevelSignals* s)
{
    memset(s, 0, sizeof(*s));
}

bool level_signals_take(LevelSignals* s, const GameEvent& e)
{
    switch (e.type)
    {
    case EV_READY:
        s->ready_scene = e.scene;
        return true;
    case EV_GO:
        s->go_scene = e.scene;
        return true;
    case EV_LEVEL:
        if (s->go_scene == e.scene)
        {
            s->go_scene = 0;
        }
        return false;
    default:
        return false;
    }
}

static void put_event(Writer& w, const GameEvent& e)
{
    w.u8(e.type);
    w.u32(e.scene);
    switch (e.type)
    {
    case EV_HIT:
        w.u32(e.table);
        w.u16(e.id);
        w.u8(e.kind);
        w.u8(e.level);
        for (int i = 0; i < 3; i++) w.i16(e.dir[i]);
        for (int i = 0; i < 3; i++) w.f32(e.pos[i]);
        break;
    case EV_DAMAGE:
        w.u16(e.id);
        w.u8(e.kind);
        w.u8(e.level);
        w.u8(e.sub);
        for (int i = 0; i < 3; i++) w.f32(e.pos[i]);
        break;
    case EV_PICKUP:
        w.u32(e.table);
        w.u16(e.id);
        w.u8(e.kind);
        break;
    case EV_GRANT:
        w.u32(e.table);
        w.u16(e.id);
        w.u8(e.kind);
        break;
    case EV_UPGRADE:
        w.u8(e.kind);
        w.u8(e.sub);
        break;
    case EV_UPGRADE_ACK:
        w.u8(e.kind);
        w.u8(e.sub);
        w.u8(e.level);
        break;
    case EV_SHOW:
        w.u32(e.table);
        w.u16(e.id);
        w.u8(e.kind);
        w.u8(e.level);
        break;
    case EV_LOOK:
        w.u8(e.kind);
        w.u8(e.level);
        w.u8(e.sub);
        break;
    }
}

// One event: its type first (and whether `from` may send it), then the bytes
// that type takes checked against what is left, then each field.
static void get_event(Reader& r, Role from, GameEvent* e)
{
    memset(e, 0, sizeof *e);
    e->type = r.u8();
    size_t sz = event_size(e->type);
    if (sz == 0 || !event_from_ok(e->type, from) || r.left() < sz - 1) r.fail();
    if (!r.ok) return;
    e->scene = r.u32();
    switch (e->type)
    {
    case EV_HIT:
        e->table = r.u32();
        e->id = r.u16();
        e->kind = r.u8();
        e->level = r.u8();
        for (int i = 0; i < 3; i++) e->dir[i] = r.i16();
        for (int i = 0; i < 3; i++) e->pos[i] = r.f32(kWorldLimit);
        break;
    case EV_DAMAGE:
        e->id = r.u16();
        e->kind = r.u8();
        e->level = r.u8();
        e->sub = r.u8();
        for (int i = 0; i < 3; i++) e->pos[i] = r.f32(kWorldLimit);
        break;
    case EV_PICKUP:
        e->table = r.u32();
        e->id = r.u16();
        e->kind = r.u8();
        break;
    case EV_GRANT:
        e->table = r.u32();
        e->id = r.u16();
        e->kind = r.u8();
        break;
    case EV_UPGRADE:
        e->kind = r.u8();
        e->sub = r.u8();
        break;
    case EV_UPGRADE_ACK:
        e->kind = r.u8();
        e->sub = r.u8();
        e->level = r.u8();
        break;
    case EV_SHOW:
        e->table = r.u32();
        e->id = r.u16();
        e->kind = r.u8();
        e->level = r.u8();
        break;
    case EV_LOOK:
        e->kind = r.u8();
        e->level = r.u8();
        e->sub = r.u8();
        break;
    }
    if (r.ok && !event_valid(*e, from)) r.fail();
}

static bool pickups_valid(const Pickups& k)
{
    if (!scene_valid(k.scene) || k.count < 1 || k.count > kMaxPickups) return false;
    unsigned bytes = (k.count + 7u) / 8u;
    if (k.count % 8 && ((k.bits[bytes - 1] | k.avail[bytes - 1]) >> (k.count % 8)))
        return false; // past the count
    for (unsigned i = 0; i < bytes; i++)
        if (k.bits[i] & k.avail[i])
            return false; // taken and there to be taken
    return true;
}

bool progress_valid(const Progress& g)
{
    if (g.tokens > kMaxTokens || g.shiny > kMaxShiny || g.range > kMaxRangeIdx ||
        g.unused > kMaxRange || g.spent > (int)kMaxRange || g.spent < -(int)kMaxRange ||
        g.chests > kMaxChests)
        return false;
    for (unsigned i = 0; i < kPowerUps; i++)
        if (g.powerup[i] > (i < kMoves ? 2 : 3))
            return false;
    return true;
}

static void put_progress(Writer& w, const Progress& g)
{
    w.u16(g.tokens);
    w.u32(g.shiny);
    w.u8(g.range);
    w.u8(g.unused);
    w.u8((uint8_t)g.spent);
    w.bytes(g.powerup, kPowerUps);
    w.u8(g.chests);
}

static void get_progress(Reader& r, Progress* g)
{
    memset(g, 0, sizeof *g);
    g->tokens = r.u16();
    g->shiny = r.u32();
    g->range = r.u8();
    g->unused = r.u8();
    g->spent = (int8_t)r.u8();
    r.bytes(g->powerup, kPowerUps);
    g->chests = r.u8();
    if (r.ok && !progress_valid(*g))
        r.fail();
}

bool counters_valid(const Counters& c)
{
    if (!scene_valid(c.scene) || c.counters > kMaxCounters || c.boxes > kMaxTextBoxes ||
        (c.counters == 0 && c.boxes == 0) || c.count > c.counters)
        return false;
    for (unsigned i = 0; i < c.count; i++)
    {
        if (c.e[i].index >= c.counters || (i > 0 && c.e[i].index <= c.e[i - 1].index))
            return false; // past the table, or not ascending (each once)
        if (c.e[i].value > (int)kCounterLimit || c.e[i].value < -(int)kCounterLimit)
            return false;
    }
    for (unsigned i = 0; i < kMaxTextBoxes; i++)
        if (c.text[i] > (i < c.boxes ? kMaxBoxTexts : 0))
            return false;
    return true;
}

void counters_merge(Counters* into, const Counters& newer)
{
    if (into->scene != newer.scene || into->table != newer.table ||
        into->counters != newer.counters || into->boxes != newer.boxes)
    {
        *into = newer;
        return;
    }
    Counters m = newer;
    m.count = 0;
    unsigned a = 0, b = 0;
    while (a < into->count || b < newer.count)
    {
        if (b < newer.count && (a >= into->count || newer.e[b].index <= into->e[a].index))
        {
            if (a < into->count && into->e[a].index == newer.e[b].index)
                a++;
            m.e[m.count++] = newer.e[b++];
        }
        else
        {
            m.e[m.count++] = into->e[a++];
        }
    }
    *into = m;
}

static void put_counters(Writer& w, const Counters& c)
{
    w.u32(c.scene);
    w.u32(c.table);
    w.u8(c.counters);
    w.u8(c.boxes);
    w.u8(c.count);
    for (unsigned i = 0; i < c.count; i++)
    {
        w.u8(c.e[i].index);
        w.i16(c.e[i].value);
    }
    w.bytes(c.text, c.boxes);
}

static void get_counters(Reader& r, Counters* c)
{
    memset(c, 0, sizeof *c);
    c->scene = r.u32();
    c->table = r.u32();
    c->counters = r.u8();
    c->boxes = r.u8();
    c->count = r.u8();
    if (c->counters > kMaxCounters || c->boxes > kMaxTextBoxes || c->count > c->counters)
        r.fail();
    if (!r.ok)
    {
        c->count = c->boxes = 0;
        return;
    }
    for (unsigned i = 0; i < c->count; i++)
    {
        c->e[i].index = r.u8();
        c->e[i].value = r.i16();
    }
    r.bytes(c->text, c->boxes); // both bounded above, and take() checks what is left
    if (r.ok && !counters_valid(*c))
        r.fail();
}

static void get_pickups(Reader& r, Pickups* k)
{
    memset(k, 0, sizeof *k);
    k->scene = r.u32();
    if (!scene_valid(k->scene)) r.fail();
    k->table = r.u32();
    k->count = r.u16();
    if (k->count < 1 || k->count > kMaxPickups) r.fail();
    if (!r.ok)
    {
        k->count = 0;
        return;
    }
    unsigned bytes = (k->count + 7u) / 8u;
    r.bytes(k->bits, bytes); // the count is bounded above, and take() checks what is left
    r.bytes(k->avail, bytes);
    if (r.ok && !pickups_valid(*k)) r.fail();
}

// M1's four types, STALL, EVENT and REL_ACK from either side; SNAPSHOT,
// PICKUPS, PROGRESS and COUNTERS only from the host and SNAP_ACK only from
// the guest (M2, M3, protocol 7).
// Everything else, INPUT (reserved) included, is a protocol violation.
static bool msg_allowed(uint8_t type, Role from)
{
    switch (type)
    {
    case MSG_PLAYER_STATE:
    case MSG_PING:
    case MSG_PONG:
    case MSG_DISCONNECT:
    case MSG_STALL:
    case MSG_EVENT:
    case MSG_REL_ACK: return true;
    case MSG_SNAPSHOT:
    case MSG_PICKUPS:
    case MSG_PROGRESS:
    case MSG_COUNTERS:
        return from == Role::Host;
    case MSG_SNAP_ACK: return from == Role::Client;
    default: return false;
    }
}

bool parse_payload(const uint8_t* d, size_t n, Role from, Payload* p)
{
    payload_clear(p);
    if (n > kMaxPayload) return false;
    Reader r(d, n);
    uint8_t last = 0;
    while (r.ok && r.left())
    {
        uint8_t type = r.u8();
        uint16_t len = r.u16();
        if (!r.ok || !msg_allowed(type, from)) return false;
        if (type <= last) return false; // canonical order, no repeats
        last = type;
        const uint8_t* body = r.take(len);
        if (!body) return false;
        Reader b(body, len);
        switch (type)
        {
        case MSG_PLAYER_STATE:
            if (len != kPlayerStateSize) return false;
            get_pstate(b, &p->pstate);
            break;
        case MSG_SNAPSHOT: get_snapshot(b, &p->snap); break;
        case MSG_SNAP_ACK:
            p->snap_ack = b.u32();
            if (p->snap_ack == 0) b.fail();
            break;
        case MSG_PING: p->ping = b.u32(); break;
        case MSG_PONG: p->pong = b.u32(); break;
        case MSG_DISCONNECT:
            p->disconnect_reason = b.u8();
            if (p->disconnect_reason < 1 || p->disconnect_reason >= DC_END) b.fail();
            break;
        case MSG_STALL:
            p->stall_ms = b.u16();
            if (p->stall_ms == 0) b.fail();
            break;
        case MSG_EVENT:
            p->ev_first = b.u16();
            p->ev_count = b.u8();
            if (p->ev_count < 1 || p->ev_count > kMaxEventsPerMsg) b.fail();
            for (unsigned i = 0; i < p->ev_count && b.ok; i++)
            {
                get_event(b, from, &p->ev[i]);
                p->ev[i].rel_id = (uint16_t)(p->ev_first + i);
            }
            if (!b.ok) p->ev_count = 0;
            break;
        case MSG_REL_ACK: p->rel_ack = b.u16(); break;
        case MSG_PICKUPS: get_pickups(b, &p->pick); break;
        case MSG_PROGRESS:
            if (len != kProgressSize)
                return false;
            get_progress(b, &p->prog);
            break;
        case MSG_COUNTERS:
            get_counters(b, &p->cnt);
            break;
        }
        if (!b.done()) return false; // short, malformed, or trailing bytes
        p->has |= 1u << type;
    }
    return r.ok;
}

static size_t begin_msg(Writer& w, uint8_t type)
{
    w.u8(type);
    size_t at = w.len;
    w.u16(0);
    return at;
}

static void end_msg(Writer& w, size_t at)
{
    if (!w.ok) return;
    size_t body = w.len - at - 2;
    if (body > 0xFFFF) { w.ok = false; return; }
    w.buf[at] = (uint8_t)body;
    w.buf[at + 1] = (uint8_t)(body >> 8);
}

size_t encode_payload(const Payload& p, Role from, uint8_t* out, size_t cap)
{
    if (p.has & ~HAS_ALL) return 0;
    if ((p.has & HAS_HOST_ONLY) && from != Role::Host) return 0;
    if ((p.has & HAS_GUEST_ONLY) && from != Role::Client) return 0;
    if (cap > kMaxPayload) cap = kMaxPayload;
    Writer w(out, cap);
    size_t at;
    if (p.has & HAS_PSTATE)
    {
        if (!pstate_fields_valid(p.pstate)) return 0;
        at = begin_msg(w, MSG_PLAYER_STATE);
        put_pstate(w, p.pstate);
        end_msg(w, at);
    }
    if (p.has & HAS_SNAPSHOT)
    {
        if (!snapshot_valid(p.snap)) return 0;
        at = begin_msg(w, MSG_SNAPSHOT);
        put_snapshot(w, p.snap);
        end_msg(w, at);
    }
    if (p.has & HAS_EVENT)
    {
        if (p.ev_count < 1 || p.ev_count > kMaxEventsPerMsg) return 0;
        for (unsigned i = 0; i < p.ev_count; i++)
            if (!event_valid(p.ev[i], from)) return 0;
        at = begin_msg(w, MSG_EVENT);
        w.u16(p.ev_first);
        w.u8(p.ev_count);
        for (unsigned i = 0; i < p.ev_count; i++) put_event(w, p.ev[i]);
        end_msg(w, at);
    }
    if (p.has & HAS_REL_ACK)
    {
        at = begin_msg(w, MSG_REL_ACK);
        w.u16(p.rel_ack);
        end_msg(w, at);
    }
    if (p.has & HAS_SNAP_ACK)
    {
        if (p.snap_ack == 0) return 0;
        at = begin_msg(w, MSG_SNAP_ACK);
        w.u32(p.snap_ack);
        end_msg(w, at);
    }
    if (p.has & HAS_PING) { at = begin_msg(w, MSG_PING); w.u32(p.ping); end_msg(w, at); }
    if (p.has & HAS_PONG) { at = begin_msg(w, MSG_PONG); w.u32(p.pong); end_msg(w, at); }
    if (p.has & HAS_DISCONNECT)
    {
        if (p.disconnect_reason < 1 || p.disconnect_reason >= DC_END) return 0;
        at = begin_msg(w, MSG_DISCONNECT);
        w.u8(p.disconnect_reason);
        end_msg(w, at);
    }
    if (p.has & HAS_STALL)
    {
        if (p.stall_ms == 0) return 0;
        at = begin_msg(w, MSG_STALL);
        w.u16(p.stall_ms);
        end_msg(w, at);
    }
    if (p.has & HAS_PICKUPS)
    {
        if (!pickups_valid(p.pick)) return 0;
        at = begin_msg(w, MSG_PICKUPS);
        w.u32(p.pick.scene);
        w.u32(p.pick.table);
        w.u16(p.pick.count);
        w.bytes(p.pick.bits, (p.pick.count + 7u) / 8u);
        w.bytes(p.pick.avail, (p.pick.count + 7u) / 8u);
        end_msg(w, at);
    }
    if (p.has & HAS_PROGRESS)
    {
        if (!progress_valid(p.prog))
            return 0;
        at = begin_msg(w, MSG_PROGRESS);
        put_progress(w, p.prog);
        end_msg(w, at);
    }
    if (p.has & HAS_COUNTERS)
    {
        if (!counters_valid(p.cnt))
            return 0;
        at = begin_msg(w, MSG_COUNTERS);
        put_counters(w, p.cnt);
        end_msg(w, at);
    }
    return w.ok ? w.len : 0;
}

void encode_data_header(uint8_t out[kDataHeader], const DataHeader& h)
{
    Writer w(out, kDataHeader);
    w.u8(PKT_DATA);
    w.u32(h.session_id);
    w.u32(h.seq);
    w.u32(h.ack);
    w.u32(h.ack_bits);
}

bool parse_data_header(const uint8_t* d, size_t n, DataHeader* h)
{
    if (n < kDataHeader + kTagSize || n > kMaxDatagram) return false;
    Reader r(d, kDataHeader);
    if (r.u8() != PKT_DATA) return false;
    h->session_id = r.u32();
    h->seq = r.u32();
    h->ack = r.u32();
    h->ack_bits = r.u32();
    return r.done() && h->session_id != 0;
}

} // namespace iNet
