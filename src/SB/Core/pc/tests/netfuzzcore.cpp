// PC-only, co-op: the network fuzz targets (netfuzzcore.h).
#include "netfuzzcore.h"

#include "iCoopLocalCore.h"
#include "iNetCrypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace iNet {

void fuzz_fail(const char* what)
{
    fprintf(stderr, "FUZZ INVARIANT BROKEN: %s\n", what);
    fflush(stderr);
    abort();
}

#define CHECK(c)                     \
    do                               \
    {                                \
        if (!(c)) fuzz_fail(#c);     \
    } while (0)

static void check_float(float f, float limit) { CHECK(f == f && f >= -limit && f <= limit); }

static void check_local_line(void* ctx, bool ok, const iLocal::Line& l)
{
    (*(size_t*)ctx)++;
    if (ok) CHECK(l.kind == iLocal::LINE_QUIT || (l.kind == iLocal::LINE_HELLO && l.port >= 1024 && iLocal::code_valid(l.code)));
}

static bool starts_ci(const std::string& s, const char* prefix)
{
    size_t n = strlen(prefix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (toupper((unsigned char)s[i]) != toupper((unsigned char)prefix[i])) return false;
    return true;
}

static void validate_snapshot(const Snapshot& s)
{
    CHECK(scene_valid(s.scene));
    CHECK(s.things >= 1 && s.things <= kMaxNetIds);
    CHECK(s.seq != 0);
    CHECK(s.base == 0 || (s.seq - s.base >= 1 && s.seq - s.base <= kSnapBaseWindow));
    CHECK((s.flags & ~SNAP_FLAG_MASK) == 0);
    CHECK(s.count <= kMaxSnapEntries);
    for (unsigned i = 0; i < s.count; i++)
    {
        const SnapEntry& e = s.e[i];
        CHECK(e.id < s.things);
        CHECK(i == 0 || e.id > s.e[i - 1].id);
        CHECK(e.mask != 0 && (e.mask & ~SE_MASK) == 0);
        if (e.mask & SE_POS)
            for (int k = 0; k < 3; k++) check_float(e.pos[k], kWorldLimit);
        if (e.mask & SE_ROT)
        {
            bool any = false;
            for (int k = 0; k < 4; k++)
            {
                CHECK(e.rot[k] != -32768);
                any = any || e.rot[k] != 0;
            }
            CHECK(any);
        }
        if (e.mask & SE_ANIM)
        {
            check_float(e.anim_time, kAnimTimeLimit);
            CHECK(e.anim_time >= 0.0f);
        }
        if (e.mask & SE_FLAGS) CHECK((e.flags & ~WF_MASK) == 0);
    }
    CHECK(snapshot_size(s) <= kMaxPayload);
}

void validate_payload(const Payload& p, Role from)
{
    CHECK((p.has & ~HAS_ALL) == 0);
    if (p.has & HAS_HOST_ONLY) CHECK(from == Role::Host);
    if (p.has & HAS_GUEST_ONLY) CHECK(from == Role::Client);
    if (p.has & HAS_SNAPSHOT) validate_snapshot(p.snap);
    if (p.has & HAS_SNAP_ACK) CHECK(p.snap_ack != 0);
    if (p.has & HAS_PSTATE)
    {
        const PlayerState& s = p.pstate;
        CHECK(s.slot < kMaxPlayers);
        CHECK(scene_valid(s.scene));
        CHECK(s.character < CHAR_END);
        CHECK((s.flags & ~PS_FLAG_MASK) == 0);
        for (int i = 0; i < 3; i++) check_float(s.pos[i], kWorldLimit);
        check_float(s.anim_time, kAnimTimeLimit);
        CHECK(s.anim_time >= 0.0f);
        CHECK(s.health <= kPlayerHealthMax);
        CHECK(s.model < kModelEnd || s.model == kModelUnknown);
    }
    if (p.has & HAS_DISCONNECT) CHECK(p.disconnect_reason >= 1 && p.disconnect_reason < DC_END);
    if (p.has & HAS_STALL) CHECK(p.stall_ms != 0);
    // M3: every event's own rules, checked here field by field (not with the
    // parser's event_valid), and the pickup bits.
    if (p.has & HAS_EVENT)
    {
        CHECK(p.ev_count >= 1 && p.ev_count <= kMaxEventsPerMsg);
        for (unsigned i = 0; i < p.ev_count; i++)
        {
            const GameEvent& e = p.ev[i];
            CHECK(e.rel_id == (uint16_t)(p.ev_first + i));
            CHECK(scene_valid(e.scene) || (e.type == EV_LOOK && e.scene == 0));
            switch (e.type)
            {
            case EV_HIT:
                CHECK(from == Role::Client);
                CHECK(e.id < kMaxNetIds && e.kind >= ATK_SIDE && e.kind < ATK_END);
                CHECK(e.level >= kPowerupMin && e.level <= kPowerupMax);
                for (int k = 0; k < 3; k++)
                {
                    CHECK(e.dir[k] != -32768);
                    check_float(e.pos[k], kWorldLimit);
                }
                break;
            case EV_DAMAGE:
                CHECK(from == Role::Host);
                CHECK(e.id < kMaxNetIds || e.id == kNoThing);
                CHECK(e.kind < DK_END && e.level >= 1 && e.level <= kPlayerHealthMax);
                CHECK(e.kind == DK_FROM ? e.sub < kDamageSubEnd : e.sub == 0);
                for (int k = 0; k < 3; k++)
                    check_float(e.pos[k], e.kind == DK_FROM ? kWorldLimit : (e.kind == DK_PUSH ? kPushLimit : 0.0f));
                break;
            case EV_PICKUP:
                CHECK(from == Role::Client && e.id < kMaxPickups && e.kind < PW_END);
                break;
            case EV_GRANT:
                CHECK(from == Role::Host && e.id < kMaxPickups && e.kind >= GR_YOURS && e.kind < GR_END);
                break;
            // Protocol 7
            case EV_UPGRADE:
                CHECK(from == Role::Client && e.kind >= UPG_TAUGHT && e.kind < UPG_END);
                CHECK(e.sub < (e.kind == UPG_TAUGHT ? kMoves : kPowerUps));
                break;
            case EV_UPGRADE_ACK:
                CHECK(from == Role::Host && e.kind >= UPG_TAUGHT && e.kind < UPG_END);
                CHECK(e.sub < (e.kind == UPG_TAUGHT ? kMoves : kPowerUps));
                CHECK(e.level >= GR_YOURS && e.level < GR_END);
                break;
            // M4
            case EV_LEVEL:
            case EV_GO:
                CHECK(from == Role::Host && scene_valid(e.scene));
                break;
            case EV_READY:
                CHECK(from == Role::Client && scene_valid(e.scene));
                break;
            case EV_SHOW:
                CHECK(from == Role::Host && e.id < kMaxShows && e.kind >= SHOW_CUTSCENE_START && e.kind < SHOW_END);
                CHECK(e.kind == SHOW_CUTSCENE_START ? (e.level & ~SHOWF_MASK) == 0 : e.level == 0);
                break;
            case EV_LOOK:
                CHECK(e.kind < CHAR_END && e.level < kCostumeChoices && e.sub < kCostumeChoices);
                break;
            default:
                fuzz_fail("an event of no known type was accepted");
            }
        }
    }
    if (p.has & HAS_PICKUPS)
    {
        CHECK(from == Role::Host);
        CHECK(scene_valid(p.pick.scene) && p.pick.count >= 1 && p.pick.count <= kMaxPickups);
        for (unsigned i = p.pick.count; i < kMaxPickups; i++)
        {
            CHECK(((p.pick.bits[i >> 3] >> (i & 7)) & 1) == 0);
            CHECK(((p.pick.avail[i >> 3] >> (i & 7)) & 1) == 0);
        }
        for (unsigned i = 0; i < kMaxPickups / 8; i++)
            CHECK((p.pick.bits[i] & p.pick.avail[i]) == 0);
    }
    if (p.has & HAS_PROGRESS)
    {
        CHECK(from == Role::Host);
        const Progress& g = p.prog;
        CHECK(g.tokens <= kMaxTokens && g.shiny <= kMaxShiny && g.range <= kMaxRangeIdx &&
              g.unused <= kMaxRange && g.spent <= (int)kMaxRange && g.spent >= -(int)kMaxRange &&
              g.chests <= kMaxChests);
        for (unsigned i = 0; i < kPowerUps; i++)
            CHECK(g.powerup[i] <= (i < kMoves ? 2 : 3));
    }
    if (p.has & HAS_COUNTERS)
    {
        CHECK(from == Role::Host);
        const Counters& c = p.cnt;
        CHECK(scene_valid(c.scene) && c.counters <= kMaxCounters && c.boxes <= kMaxTextBoxes &&
              (c.counters || c.boxes) && c.count <= c.counters);
        for (unsigned i = 0; i < c.count; i++)
        {
            CHECK(c.e[i].index < c.counters && (i == 0 || c.e[i].index > c.e[i - 1].index));
            CHECK(c.e[i].value <= (int)kCounterLimit && c.e[i].value >= -(int)kCounterLimit);
        }
        for (unsigned i = 0; i < kMaxTextBoxes; i++)
            CHECK(c.text[i] <= (i < c.boxes ? kMaxBoxTexts : 0u));
    }
}

// --- in-memory transport ------------------------------------------------------

static void host_send(void* ctx, const NetAddr& to, const uint8_t* d, size_t n)
{
    FuzzRig* r = (FuzzRig*)ctx;
    CHECK(n <= kMaxDatagram);
    if (addr_equal(to, r->client_addr)) r->to_client.push_back({ r->host_addr, to, std::vector<uint8_t>(d, d + n) });
}

static void client_send(void* ctx, const NetAddr& to, const uint8_t* d, size_t n)
{
    FuzzRig* r = (FuzzRig*)ctx;
    CHECK(n <= kMaxDatagram);
    r->to_host.push_back({ r->client_addr, to, std::vector<uint8_t>(d, d + n) });
}

// What the host receives came from the guest, and the other way round.
// M3: what the I/O thread does with a hostile peer's events and
// acknowledgements (iCoop.cpp), on the rig's own reliable queues: nothing it
// sends can push them past their window or deliver an event twice.
static void fuzz_deliver(void* ctx, const Event& e)
{
    FuzzRig* r = (FuzzRig*)ctx;
    r->events_delivered++;
    // M4 (review): a hostile peer's level signals through the receiver's
    // keeping of them: only EV_READY and EV_GO are kept, each naming a level.
    {
        bool kept = level_signals_take(&r->levels, e);
        CHECK(kept == (e.type == EV_READY || e.type == EV_GO));
        CHECK(r->levels.ready_scene == 0 || scene_valid(r->levels.ready_scene));
        CHECK(r->levels.go_scene == 0 || scene_valid(r->levels.go_scene));
        CHECK(e.type != EV_LEVEL || r->levels.go_scene != e.scene);
    }
    // M4: a hostile peer's looks through the receiver's inbox, many at the
    // same moment: each one is taken (the newest is what is known), and what
    // is known stays in range.
    if (e.type == EV_LOOK)
    {
        look_inbox_take(&r->looks, e, r->now, NULL);
        r->looks_taken++;
        CHECK(r->looks.known && r->looks.character == e.kind && r->looks.costume[0] == e.level &&
              r->looks.costume[1] == e.sub && r->looks.scene == e.scene);
        CHECK(r->looks.character < CHAR_END && r->looks.costume[0] < kCostumeChoices &&
              r->looks.costume[1] < kCostumeChoices && r->looks.in_window <= kLooksPerSecond);
    }
}

static void reliable_side(FuzzRig* r, ReliableOut& out, ReliableIn& in, const Payload& p)
{
    if (p.has & HAS_REL_ACK)
    {
        uint16_t base0 = out.base;
        uint8_t count0 = out.count;
        bool ok = out.ack(p.rel_ack);
        CHECK(out.count <= count0 && out.count <= kRelWindow);
        CHECK(ok || (out.base == base0 && out.count == count0)); // a bad one changes nothing
    }
    if (p.has & HAS_EVENT)
    {
        for (unsigned i = 0; i < p.ev_count; i++)
        {
            uint16_t expect0 = in.expect;
            in.offer(p.ev[i], fuzz_deliver, r);
            CHECK((uint16_t)(in.expect - expect0) <= kRelWindow);
        }
    }
    // Keep a few of this side's own events waiting, so acknowledgements have
    // something to clear.
    while (out.count < 4)
    {
        Event e;
        memset(&e, 0, sizeof e);
        e.type = EV_PICKUP;
        out.push(e);
    }
}

static void host_on_payload(void* ctx, const Payload& p)
{
    validate_payload(p, Role::Client);
    FuzzRig* r = (FuzzRig*)ctx;
    r->payloads_seen++;
    if (p.has & HAS_SNAP_ACK) r->acks_delivered++;
    if (p.has & (HAS_EVENT | HAS_REL_ACK)) r->events_seen++;
    reliable_side(r, r->host_out, r->host_in, p);
}

static void client_on_payload(void* ctx, const Payload& p)
{
    validate_payload(p, Role::Host);
    FuzzRig* r = (FuzzRig*)ctx;
    r->payloads_seen++;
    if (p.has & HAS_COUNTERS)
        r->take_counters(p.cnt, (uint8_t)r->payloads_seen);
    if (p.has & HAS_SNAPSHOT) r->snapshots_delivered++;
    if (p.has & (HAS_EVENT | HAS_REL_ACK | HAS_PICKUPS)) r->events_seen++;
    reliable_side(r, r->client_out, r->client_in, p);
}

void FuzzRig::pump()
{
    for (int round = 0; round < 8 && (!to_host.empty() || !to_client.empty()); round++)
    {
        std::vector<Q> h;
        h.swap(to_host);
        for (Q& q : h) host.on_datagram(q.from, q.d.data(), q.d.size(), now);
        std::vector<Q> c;
        c.swap(to_client);
        for (Q& q : c) client.on_datagram(q.from, q.d.data(), q.d.size(), now);
    }
    to_host.clear();
    to_client.clear();
}

static const uint8_t kBuild[kBuildIdSize] = { 'f', 'u', 'z', 'z', 'b', 'u', 'i', 'l', 'd' };
static uint8_t sAssets[kHashSize];

// The fixed nonce the fuzzing finder asked with, and the seeds carry.
static const uint8_t kFuzzNonce[kDiscNonce] = { 0x5E, 0xED, 0xD1, 0x5C, 0x00, 0x01, 0x02, 0x03 };

static Offer fuzz_offer()
{
    Offer o;
    memset(&o, 0, sizeof o);
    o.disc_version = kDiscVersion;
    o.proto = kProtoVersion;
    memcpy(o.nonce, kFuzzNonce, kDiscNonce);
    memcpy(o.build, kBuild, kIdPrefix);
    o.game_port = kDefaultPort;
    o.instance = 0x12345678;
    o.platform = PLAT_WINDOWS;
    o.scene = 'B' | 'B' << 8 | '0' << 16 | '2' << 24;
    snprintf(o.name, sizeof o.name, "Host");
    return o;
}

bool FuzzRig::init()
{
    addr_parse("192.0.2.1:2171", 0, &host_addr);
    addr_parse("198.51.100.7:50000", 0, &client_addr);
    addr_parse("203.0.113.9:40000", 0, &strangers[0]);
    addr_parse("[2001:db8::5]:40001", 0, &strangers[1]);
    addr_parse("198.51.100.7:50001", 0, &strangers[2]); // same IP, other port
    // The home network, on 192.168.0.0/24.
    memset(&responder, 0, sizeof responder);
    responder.offer = fuzz_offer();
    const uint8_t home[4] = { 192, 168, 0, 3 };
    memcpy(responder.ifs[0].ip, home, 4);
    responder.ifs[0].prefix = 24;
    responder.ifs_count = 1;
    memset(&finder, 0, sizeof finder);
    memcpy(finder.build, kBuild, kIdPrefix);
    finder.files_known = true;
    finder.disc_port = kDiscPort;
    finder.ifs[0] = responder.ifs[0];
    finder.ifs_count = 1;
    finder.asked(kFuzzNonce);
    sha256("assets", 6, sAssets);
    HostConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    memcpy(cfg.build_id, kBuild, sizeof kBuild);
    memcpy(cfg.asset_hash, sAssets, sizeof sAssets);
    strcpy(cfg.name, "Host");
    cfg.password = "fuzz-password";
    cfg.iters = kMinIters;
    NetIO io = { this, host_send, host_on_payload, nullptr };
    if (!host.init(cfg, io, now)) return false;
    return ensure_session();
}

bool FuzzRig::ensure_session()
{
    if (host.session().active && client.state() == ClientState::Connected) return true;
    if (host.session().active) host.close(now);
    to_host.clear();
    to_client.clear();
    NetIO io = { this, client_send, client_on_payload, nullptr };
    now += 30 * kNsPerSec; // let any ban or lockout from earlier fuzz input lapse
    if (!client.connect(host_addr, "fuzz-password", "Brother", kBuild, sAssets, io, now)) return false;
    pump();
    rehandshakes++;
    return client.state() == ClientState::Connected && host.session().active;
}

// A guest's COUNTERS not taken yet, with a newer on top (iCoop.cpp): still
// valid; the same table's holds every newer entry and the older ones it
// lacks, each once, and the newer's texts; another table's is the newer.
// Taken now and then (`seed`), as the game thread does.
void FuzzRig::take_counters(const Counters& c, uint8_t seed)
{
    CHECK(counters_valid(c));
    if (!cnt_have || (seed & 7) == 0)
    {
        cnt = c;
        cnt_have = true;
        return;
    }
    const Counters old = cnt;
    bool same = old.scene == c.scene && old.table == c.table && old.counters == c.counters &&
                old.boxes == c.boxes;
    counters_merge(&cnt, c);
    CHECK(counters_valid(cnt));
    CHECK(cnt.scene == c.scene && cnt.table == c.table && cnt.counters == c.counters &&
          cnt.boxes == c.boxes && !memcmp(cnt.text, c.text, sizeof c.text));
    if (!same)
    {
        CHECK(cnt.count == c.count);
        for (unsigned i = 0; i < c.count; i++)
            CHECK(cnt.e[i].index == c.e[i].index && cnt.e[i].value == c.e[i].value);
        return;
    }
    cnt_merged++;
    unsigned want = c.count;
    for (unsigned i = 0; i < old.count; i++)
    {
        bool inNewer = false;
        for (unsigned k = 0; k < c.count; k++)
            inNewer = inNewer || c.e[k].index == old.e[i].index;
        want += inNewer ? 0 : 1;
    }
    CHECK(cnt.count == want);
    for (unsigned i = 0; i < cnt.count; i++)
    {
        const CounterEntry* from = nullptr;
        for (unsigned k = 0; k < c.count && !from; k++)
            if (c.e[k].index == cnt.e[i].index)
                from = &c.e[k];
        for (unsigned k = 0; k < old.count && !from; k++)
            if (old.e[k].index == cnt.e[i].index)
                from = &old.e[k];
        CHECK(from != nullptr && from->value == cnt.e[i].value);
    }
}

void FuzzRig::run(uint8_t target, const uint8_t* d, size_t n)
{
    target %= FT_COUNT;
    tried[target]++;
    now += 1 + (n ? d[0] : 0) * kNsPerMs; // time moves, buckets refill, cookies age
    uint8_t buf[kMaxDatagram + 64];
    switch (target)
    {
    case FT_PARSE_PRE:
    {
        Hello h;
        Challenge c;
        Auth a;
        Welcome w;
        Reject r;
        size_t m;
        if (parse_hello(d, n, &h))
        {
            accepted[target]++;
            m = encode_hello(buf, sizeof buf, h);
            CHECK(m == n && !memcmp(buf, d, n));
        }
        if (parse_challenge(d, n, &c))
        {
            accepted[target]++;
            CHECK(c.iters >= kMinIters && c.iters <= kMaxIters);
            m = encode_challenge(buf, sizeof buf, c);
            CHECK(m == n && !memcmp(buf, d, n));
        }
        if (parse_auth(d, n, &a))
        {
            accepted[target]++;
            CHECK(name_valid(a.name, a.name_len) && a.name[a.name_len] == 0);
            m = encode_auth(buf, sizeof buf, a);
            CHECK(m == n && !memcmp(buf, d, n));
        }
        if (parse_welcome(d, n, &w))
        {
            accepted[target]++;
            CHECK(w.slot >= 1 && w.slot < kMaxPlayers && w.session_id != 0 && name_valid(w.name, w.name_len));
            m = encode_welcome(buf, sizeof buf, w);
            CHECK(m == n && !memcmp(buf, d, n));
        }
        if (parse_reject(d, n, &r))
        {
            accepted[target]++;
            CHECK(r.reason >= 1 && r.reason < REJ_END);
            m = encode_reject(buf, sizeof buf, r);
            CHECK(m == n && !memcmp(buf, d, n));
        }
        DataHeader dh;
        if (parse_data_header(d, n, &dh)) CHECK(n >= kDataHeader + kTagSize && n <= kMaxDatagram);
        PlayerState ps;
        if (parse_player_state(d, n, &ps))
        {
            accepted[target]++;
            m = encode_player_state(ps, buf, sizeof buf);
            CHECK(m == n && !memcmp(buf, d, n));
        }
        static Snapshot snap;
        if (parse_snapshot(d, n, &snap))
        {
            accepted[target]++;
            snapshots_parsed++;
            validate_snapshot(snap);
            CHECK(snapshot_size(snap) == n);
            m = encode_snapshot(snap, buf, sizeof buf);
            CHECK(m == n && !memcmp(buf, d, n));
        }
        break;
    }
    case FT_PARSE_PAYLOAD:
    {
        static Payload p;
        for (int role = 0; role < 2; role++)
        {
            Role from = role ? Role::Client : Role::Host;
            if (!parse_payload(d, n, from, &p)) continue;
            accepted[target]++;
            if (p.has & HAS_SNAPSHOT) snapshots_parsed++;
            validate_payload(p, from);
            if (p.has & HAS_COUNTERS)
                take_counters(p.cnt, n ? d[n - 1] : 0);
            size_t m = encode_payload(p, from, buf, sizeof buf);
            CHECK(m == n && (n == 0 || !memcmp(buf, d, n)));
        }
        break;
    }
    case FT_HOST_RAW:
    case FT_CLIENT_RAW:
    {
        if (!ensure_session()) fuzz_fail("could not re-establish the session");
        size_t len = n > kMaxDatagram + 32 ? kMaxDatagram + 32 : n;
        uint32_t before = host.session().id;
        uint64_t ok_before = host.session().pkts_in;
        if (target == FT_HOST_RAW)
        {
            const NetAddr& from = n > 1 ? ((d[1] & 3) == 3 ? client_addr : strangers[d[1] & 3]) : client_addr;
            host.on_datagram(from, d, len, now);
            // Raw bytes cannot carry a valid tag: the session must be untouched.
            CHECK(host.session().active && host.session().id == before && host.session().pkts_in == ok_before);
        }
        else
        {
            uint64_t c_before = client.session().pkts_in;
            client.on_datagram(host_addr, d, len, now);
            CHECK(client.state() == ClientState::Connected && client.session().pkts_in == c_before);
        }
        // Only replies to pre-session traffic come from raw input; they must
        // never be bigger than what provoked them.
        CHECK(host.stats().max_reply_ratio_x100 <= 100);
        for (const Q& q : to_client) CHECK(q.d.size() <= kMaxDatagram);
        to_client.clear();
        to_host.clear();
        break;
    }
    case FT_HOST_SEALED:
    case FT_CLIENT_SEALED:
    {
        if (!ensure_session()) fuzz_fail("could not re-establish the session");
        if (n > kMaxPayload) n = kMaxPayload;
        uint8_t pkt[kMaxDatagram];
        if (target == FT_HOST_SEALED)
        {
            size_t m = session_seal(client.session(), d, n, pkt, now);
            CHECK(m == kDataHeader + n + kTagSize);
            uint64_t bad = host.session().drop_parse;
            host.on_datagram(client_addr, pkt, m, now);
            if (host.session().active && host.session().drop_parse == bad) accepted[target]++;
        }
        else
        {
            size_t m = session_seal(host.session(), d, n, pkt, now);
            uint64_t bad = client.session().drop_parse;
            client.on_datagram(host_addr, pkt, m, now);
            if (client.state() == ClientState::Connected && client.session().drop_parse == bad) accepted[target]++;
        }
        // An authenticated peer can at worst get itself disconnected; its
        // bytes still went through the same parser and the same bounds.
        to_client.clear();
        to_host.clear();
        break;
    }
    case FT_LOCAL_LINE:
    {
        iLocal::Line l;
        if (iLocal::parse_line((const char*)d, n, &l))
        {
            accepted[target]++;
            char back[iLocal::kMaxLine + 1];
            size_t m = iLocal::format_line(l, back, sizeof back);
            CHECK(m == n + 1 && !memcmp(back, d, n) && back[n] == '\n');
        }
        size_t lines = 0, newlines = 0;
        for (size_t i = 0; i < n; i++) newlines += d[i] == '\n';
        iLocal::LineReader r;
        r.feed(d, n, check_local_line, &lines);
        CHECK(lines == newlines);
        break;
    }
    case FT_LOCAL_ENV:
    {
        std::vector<std::string> parent;
        size_t from = 0;
        for (size_t i = 0; i <= n; i++)
            if (i == n || d[i] == '\n')
            {
                parent.emplace_back((const char*)d + from, i - from);
                from = i + 1;
            }
        std::vector<std::string> set = { "TSSM_LOCAL_COOP=2", "TSSM_SAVE_DIR=E:/run/local2/saves" };
        std::vector<std::string> env = iLocal::child_env(parent, set);
        bool local = false, saves = false;
        for (const std::string& v : env)
        {
            CHECK(v.find('=', 1) != std::string::npos && v.find('\0') == std::string::npos);
            CHECK(!starts_ci(v, iLocal::kPassPrefix));
            local = local || v == set[0];
            saves = saves || v == set[1];
            CHECK(!starts_ci(v, "TSSM_SAVE_DIR=") || v == set[1]);
        }
        CHECK(local && saves);
        std::vector<char> block;
        CHECK(iLocal::env_block(env, &block)); // unique, well-formed names by construction
        CHECK(block.size() >= 2 && block[block.size() - 1] == 0 && block[block.size() - 2] == 0);
        accepted[target]++;
        break;
    }
    case FT_ADDR:
    {
        char s[80];
        size_t m = n < sizeof s - 1 ? n : sizeof s - 1;
        memcpy(s, d, m);
        s[m] = 0;
        NetAddr a;
        if (addr_parse(s, kDefaultPort, &a))
        {
            accepted[target]++;
            CHECK(a.port != 0);
            char back[80];
            addr_format(a, back, sizeof back);
            NetAddr b;
            CHECK(addr_parse(back, 0, &b) && addr_equal(a, b));
        }
        break;
    }
    case FT_DISC_PARSE:
    {
        Discover q;
        if (parse_discover(d, n, &q))
        {
            accepted[target]++;
            CHECK(n == kDiscoverSize && q.version >= 1 && !memcmp(q.nonce, d + 6, kDiscNonce));
            if (q.version == kDiscVersion)
            {
                size_t m = encode_discover(buf, sizeof buf, q.nonce);
                CHECK(m == n && !memcmp(buf, d, n));
            }
        }
        Offer o;
        OfferKind k = parse_offer(d, n, &o);
        if (k == OFFER_V1)
        {
            accepted[target]++;
            CHECK(n >= kOfferMin && n <= kOfferMax && o.game_port >= 1024 && o.instance != 0 &&
                  o.state <= OFFER_HAS_PLAYER && (o.scene == 0 || scene_valid(o.scene)) &&
                  name_valid(o.name, strlen(o.name)));
            size_t m = encode_offer(buf, sizeof buf, o);
            CHECK(m == n && !memcmp(buf, d, n));
        }
        else if (k == OFFER_NEWER)
        {
            CHECK(n >= kOfferPrefix && n < kDiscoverSize && o.disc_version > kDiscVersion);
        }
        break;
    }
    case FT_DISC_RESPONDER:
    {
        // Silence for every bad source; for the good one, an answer smaller
        // than the question that echoes its nonce.
        disc_now += 100 * kNsPerMs;
        static const char* bad[] = { "203.0.113.9:50000",   "192.168.0.51:1000",
                                     "10.5.0.3:50000",      "[fe80::1]:50000",
                                     "192.168.0.255:50000", "192.168.0.0:50000",
                                     "192.168.0.3:50000" };
        for (const char* b : bad)
        {
            NetAddr from;
            addr_parse(b, 0, &from);
            CHECK(responder.on_question(from, d, n, disc_now, buf, sizeof buf) == 0);
        }
        NetAddr from;
        addr_parse("192.168.0.50:50000", 0, &from);
        size_t m = responder.on_question(from, d, n, disc_now, buf, sizeof buf);
        if (m != 0)
        {
            accepted[target]++;
            disc_answered++;
            Offer o;
            CHECK(m < n && n == kDiscoverSize && parse_offer(buf, m, &o) == OFFER_V1 &&
                  !memcmp(o.nonce, d + 6, kDiscNonce));
        }
        break;
    }
    case FT_DISC_FINDER:
    {
        // An answer from the internet is never listed; from the home
        // network, only a well-formed one with the question's nonce.
        disc_now += kNsPerSec;
        finder.expire(disc_now);
        NetAddr from;
        addr_parse("203.0.113.9:2172", 0, &from);
        uint8_t before = finder.count;
        CHECK(!finder.on_answer(from, d, n, disc_now) && finder.count == before);
        addr_parse("192.168.0.23:2172", 0, &from);
        if (finder.on_answer(from, d, n, disc_now))
            accepted[target]++;
        CHECK(finder.count <= kMaxFound);
        for (uint8_t i = 0; i < finder.count; i++)
        {
            const Found& f = finder.list[i];
            if (f.newer)
                CHECK(f.addr.port == 0 && f.name[0] == 0 && f.kind == FOUND_VERSION);
            else
                CHECK(f.addr.port >= 1024 && f.state <= OFFER_HAS_PLAYER && f.instance != 0 &&
                      (f.scene == 0 || scene_valid(f.scene)) && name_valid(f.name, strlen(f.name)));
        }
        disc_listed += finder.count;
        break;
    }
    case FT_CODE:
    {
        char s[96];
        size_t m = n < sizeof s - 1 ? n : sizeof s - 1;
        memcpy(s, d, m);
        s[m] = 0;
        char out[kMaxPassword + 1];
        if (normalize_code(s, out, sizeof out))
        {
            accepted[target]++;
            size_t k = strlen(out);
            CHECK(k >= 1 && k <= kMaxPassword);
            for (size_t i = 0; i < k; i++)
            {
                char c = out[i];
                CHECK(c != ' ' && c != '-' && c != '\t' && !(c >= 'a' && c <= 'z') && c != 'O' &&
                      c != 'I' && c != 'L');
            }
            char again[kMaxPassword + 1];
            CHECK(normalize_code(out, again, sizeof again) && strcmp(out, again) == 0);
        }
        else
        {
            CHECK(out[0] == 0); // refused: no code left to use
        }
        break;
    }
    }
}

std::vector<std::vector<uint8_t>> fuzz_seeds(FuzzRig& rig)
{
    std::vector<std::vector<uint8_t>> s;
    uint8_t b[kMaxDatagram];
    size_t n;
    Hello h;
    memset(&h, 0, sizeof h);
    h.version = kProtoVersion;
    n = encode_hello(b, sizeof b, h);
    s.emplace_back(b, b + n);
    Challenge c;
    memset(&c, 0, sizeof c);
    c.iters = kDefaultIters;
    n = encode_challenge(b, sizeof b, c);
    s.emplace_back(b, b + n);
    Auth a;
    memset(&a, 0, sizeof a);
    a.name_len = 7;
    memcpy(a.name, "Brother", 7);
    n = encode_auth(b, sizeof b, a);
    s.emplace_back(b, b + n);
    Welcome w;
    memset(&w, 0, sizeof w);
    w.session_id = 7;
    w.slot = 1;
    w.name_len = 4;
    memcpy(w.name, "Host", 4);
    n = encode_welcome(b, sizeof b, w);
    s.emplace_back(b, b + n);
    Reject r;
    memset(&r, 0, sizeof r);
    r.version = 1;
    r.reason = REJ_FULL;
    n = encode_reject(b, sizeof b, r);
    s.emplace_back(b, b + n);

    Payload p;
    payload_clear(&p);
    p.has = HAS_PSTATE | HAS_PING;
    p.pstate.slot = 1;
    p.pstate.frame = 9;
    p.pstate.scene = 'B' | 'B' << 8 | '0' << 16 | '2' << 24;
    p.pstate.character = CHAR_PATRICK;
    p.pstate.flags = PS_VISIBLE;
    p.pstate.pos[1] = 12.5f;
    p.pstate.anim = 0x1234;
    p.pstate.anim_time = 0.5f;
    p.pstate.health = 3;
    p.pstate.parts = 0x3FF;
    p.ping = 1;
    n = encode_payload(p, Role::Client, b, sizeof b);
    s.emplace_back(b, b + n);
    n = encode_player_state(p.pstate, b, sizeof b);
    s.emplace_back(b, b + n);
    p.pstate.slot = 0;
    p.has = HAS_PSTATE | HAS_PONG | HAS_DISCONNECT;
    p.pong = 5;
    p.disconnect_reason = DC_QUIT;
    n = encode_payload(p, Role::Host, b, sizeof b);
    s.emplace_back(b, b + n);
    // M2: a snapshot with every kind of entry, and its acknowledgement.
    static Snapshot sn;
    memset(&sn, 0, sizeof sn);
    sn.scene = p.pstate.scene;
    sn.table = 0xC0FFEE;
    sn.things = 300;
    sn.seq = 40;
    sn.base = 38;
    sn.host_ms = 123456;
    sn.flags = SNAP_COMPLETE;
    sn.count = 5;
    for (int i = 0; i < 5; i++)
    {
        SnapEntry& e = sn.e[i];
        e.id = (uint16_t)(i * 50 + 3);
        e.mask = (uint8_t)(i == 0 ? SE_MASK : (1 << (i - 1)));
        e.pos[0] = 10.0f * i;
        e.pos[1] = -2.5f;
        e.pos[2] = 7.0f;
        e.rot[3] = 32767;
        e.anim = 0xABCD0000u + i;
        e.anim_time = 0.25f * i;
        e.flags = WF_SHOWN | WF_ENABLED;
        e.alpha = 200;
    }
    static Payload sp;
    payload_clear(&sp);
    sp.has = HAS_PSTATE | HAS_SNAPSHOT | HAS_PING;
    sp.pstate = p.pstate;
    sp.snap = sn;
    sp.ping = 3;
    n = encode_payload(sp, Role::Host, b, sizeof b);
    s.emplace_back(b, b + n);
    n = encode_snapshot(sn, b, sizeof b);
    s.emplace_back(b, b + n);
    payload_clear(&sp);
    sp.has = HAS_PSTATE | HAS_SNAP_ACK;
    sp.pstate = p.pstate;
    sp.pstate.slot = 1;
    sp.snap_ack = 40;
    n = encode_payload(sp, Role::Client, b, sizeof b);
    s.emplace_back(b, b + n);
    // STALL from either side, with a goodbye before it.
    payload_clear(&sp);
    sp.has = HAS_PING | HAS_DISCONNECT | HAS_STALL;
    sp.ping = 9;
    sp.disconnect_reason = DC_STALLED;
    sp.stall_ms = 61000;
    n = encode_payload(sp, Role::Client, b, sizeof b);
    s.emplace_back(b, b + n);
    // M3: every event type from its side, acknowledgements, PICKUPS.
    {
        GameEvent g[4];
        memset(g, 0, sizeof g);
        g[0].type = EV_HIT;
        g[0].scene = p.pstate.scene;
        g[0].table = 0xC0FFEE;
        g[0].id = 126;
        g[0].kind = ATK_CARTWHEEL;
        g[0].level = 2;
        g[0].dir[0] = 32767;
        g[0].pos[0] = 11.5f;
        g[1].type = EV_PICKUP;
        g[1].scene = p.pstate.scene;
        g[1].table = 0xAFD4;
        g[1].id = 303;
        g[1].kind = PW_REVEALED;
        payload_clear(&sp);
        sp.has = HAS_PSTATE | HAS_EVENT | HAS_REL_ACK | HAS_SNAP_ACK;
        sp.pstate = p.pstate;
        sp.pstate.slot = 1;
        sp.ev_first = 0xFFFF;
        sp.ev_count = 2;
        sp.ev[0] = g[0];
        sp.ev[1] = g[1];
        sp.rel_ack = 5;
        sp.snap_ack = 40;
        n = encode_payload(sp, Role::Client, b, sizeof b);
        s.emplace_back(b, b + n);
        g[2].type = EV_DAMAGE;
        g[2].scene = p.pstate.scene;
        g[2].id = kNoThing;
        g[2].kind = DK_PUSH;
        g[2].level = 1;
        g[2].pos[1] = 12.0f;
        g[3].type = EV_GRANT;
        g[3].scene = p.pstate.scene;
        g[3].table = 0xAFD4;
        g[3].id = 303;
        g[3].kind = GR_YOURS;
        payload_clear(&sp);
        sp.has = HAS_PSTATE | HAS_EVENT | HAS_REL_ACK | HAS_PICKUPS;
        sp.pstate = p.pstate;
        sp.ev_first = 0; // the first ids, so the rig's queue delivers them
        sp.ev_count = 2;
        sp.ev[0] = g[2];
        sp.ev[1] = g[3];
        sp.rel_ack = 2;
        sp.pick.scene = p.pstate.scene;
        sp.pick.table = 0xAFD4;
        sp.pick.count = 37;
        sp.pick.bits[0] = 0x5A;
        sp.pick.bits[4] = 0x1F;
        sp.pick.avail[0] = 0x81;
        sp.pick.avail[3] = 0xFF;
        n = encode_payload(sp, Role::Host, b, sizeof b);
        s.emplace_back(b, b + n);
    }
    // Protocol 7: the guest's car and lightning hits, a hit on a breakable,
    // a pickup its own script gave, its two kinds of move claims; the host's
    // answers and its PROGRESS.
    {
        GameEvent g[6];
        memset(g, 0, sizeof g);
        g[0].type = EV_HIT;
        g[0].scene = p.pstate.scene;
        g[0].table = 0xC0FFEE;
        g[0].id = 165;
        g[0].kind = ATK_CAR;
        g[0].level = 1;
        g[0].pos[2] = 308.0f;
        g[1] = g[0];
        g[1].id = 26;
        g[1].kind = ATK_ZAP;
        g[1].level = 2;
        g[2].type = EV_PICKUP;
        g[2].scene = p.pstate.scene;
        g[2].table = 0xAFD4;
        g[2].id = 209;
        g[2].kind = PW_GIVEN;
        g[3].type = EV_UPGRADE;
        g[3].scene = p.pstate.scene;
        g[3].kind = UPG_TAUGHT;
        g[3].sub = 1;
        g[4] = g[3];
        g[4].kind = UPG_BOUGHT;
        g[4].sub = 9;
        payload_clear(&sp);
        sp.has = HAS_PSTATE | HAS_EVENT;
        sp.pstate = p.pstate;
        sp.pstate.slot = 1;
        sp.ev_first = 0;
        sp.ev_count = 5;
        for (int i = 0; i < 5; i++)
            sp.ev[i] = g[i];
        n = encode_payload(sp, Role::Client, b, sizeof b);
        s.emplace_back(b, b + n);
        g[5].type = EV_UPGRADE_ACK;
        g[5].scene = p.pstate.scene;
        g[5].kind = UPG_BOUGHT;
        g[5].sub = 8;
        g[5].level = GR_NO;
        payload_clear(&sp);
        sp.has = HAS_PSTATE | HAS_EVENT | HAS_PROGRESS;
        sp.pstate = p.pstate;
        sp.ev_first = 0;
        sp.ev_count = 1;
        sp.ev[0] = g[5];
        sp.prog.tokens = 5;
        sp.prog.shiny = 1250;
        sp.prog.range = 3;
        sp.prog.unused = 2;
        sp.prog.spent = -1;
        sp.prog.powerup[0] = 2;
        sp.prog.powerup[1] = 1;
        sp.prog.powerup[9] = 3;
        sp.prog.chests = 1;
        n = encode_payload(sp, Role::Host, b, sizeof b);
        s.emplace_back(b, b + n);
        // COUNTERS: an arena's table, two counts that changed and both boxes;
        // the fullest one; one with boxes only.
        payload_clear(&sp);
        sp.has = HAS_PSTATE | HAS_COUNTERS;
        sp.pstate = p.pstate;
        sp.cnt.scene = p.pstate.scene;
        sp.cnt.table = 0xC7A16113;
        sp.cnt.counters = 6;
        sp.cnt.boxes = 2;
        sp.cnt.count = 2;
        sp.cnt.e[0].index = 1;
        sp.cnt.e[0].value = 49;
        sp.cnt.e[1].index = 4;
        sp.cnt.e[1].value = 0;
        sp.cnt.text[0] = 9;
        sp.cnt.text[1] = 1;
        n = encode_payload(sp, Role::Host, b, sizeof b);
        s.emplace_back(b, b + n);
        // The same table's next: one count again, two others (merged with
        // the one before when the guest has not taken it yet).
        sp.cnt.count = 3;
        sp.cnt.e[0].index = 0;
        sp.cnt.e[0].value = 50;
        sp.cnt.e[1].index = 4;
        sp.cnt.e[1].value = 1;
        sp.cnt.e[2].index = 5;
        sp.cnt.e[2].value = (int16_t)(-(int)kCounterLimit);
        sp.cnt.text[0] = 3;
        sp.cnt.text[1] = 0;
        n = encode_payload(sp, Role::Host, b, sizeof b);
        s.emplace_back(b, b + n);
        sp.cnt.counters = kMaxCounters;
        sp.cnt.boxes = kMaxTextBoxes;
        sp.cnt.count = kMaxCounters;
        for (unsigned i = 0; i < kMaxCounters; i++)
        {
            sp.cnt.e[i].index = (uint8_t)i;
            sp.cnt.e[i].value = (int16_t)(i & 1 ? kCounterLimit : -(int)i);
        }
        for (unsigned i = 0; i < kMaxTextBoxes; i++)
            sp.cnt.text[i] = (uint8_t)(i * 2);
        n = encode_payload(sp, Role::Host, b, sizeof b);
        s.emplace_back(b, b + n);
        sp.cnt.counters = 0;
        sp.cnt.count = 0;
        sp.cnt.boxes = 1;
        memset(sp.cnt.text, 0, sizeof sp.cnt.text);
        sp.cnt.text[0] = kMaxBoxTexts;
        n = encode_payload(sp, Role::Host, b, sizeof b);
        s.emplace_back(b, b + n);
    }
    // M4: the host's level change, its go, a cutscene and a fly-through, and
    // its look; the guest's ready and its look; a state with a model.
    {
        GameEvent g[5];
        memset(g, 0, sizeof g);
        g[0].type = EV_LEVEL;
        g[0].scene = 'D' | 'E' << 8 | '0' << 16 | '1' << 24;
        g[1].type = EV_GO;
        g[1].scene = g[0].scene;
        g[2].type = EV_SHOW;
        g[2].scene = g[0].scene;
        g[2].table = 0x5EED;
        g[2].id = 3;
        g[2].kind = SHOW_CUTSCENE_START;
        g[2].level = SHOWF_PAUSE_MUSIC | SHOWF_LOAD_NOW;
        g[3].type = EV_SHOW;
        g[3].scene = g[0].scene;
        g[3].table = 0x5EED;
        g[3].id = 255;
        g[3].kind = SHOW_FLY_END;
        g[4].type = EV_LOOK;
        g[4].scene = g[0].scene;
        g[4].kind = CHAR_PATRICK;
        g[4].level = 2;
        g[4].sub = 4;
        payload_clear(&sp);
        sp.has = HAS_PSTATE | HAS_EVENT;
        sp.pstate = p.pstate;
        sp.pstate.model = 11;
        sp.ev_first = 0;
        sp.ev_count = 5;
        for (int i = 0; i < 5; i++) sp.ev[i] = g[i];
        n = encode_payload(sp, Role::Host, b, sizeof b);
        s.emplace_back(b, b + n);
        g[0].type = EV_READY;
        g[1] = g[4];
        g[1].scene = 0;
        g[1].kind = CHAR_SPONGEBOB;
        payload_clear(&sp);
        sp.has = HAS_PSTATE | HAS_EVENT;
        sp.pstate = p.pstate;
        sp.pstate.slot = 1;
        sp.pstate.model = kModelUnknown;
        sp.ev_first = 0;
        sp.ev_count = 2;
        sp.ev[0] = g[0];
        sp.ev[1] = g[1];
        n = encode_payload(sp, Role::Client, b, sizeof b);
        s.emplace_back(b, b + n);
    }
    // A reserved later-milestone message, so the loop keeps knocking on it.
    uint8_t ev[8] = { MSG_INPUT, 4, 0, 1, 0, 0, 0, 0 };
    s.emplace_back(ev, ev + 7);
    // a sealed data packet each way
    uint8_t pl[7] = { MSG_PING, 4, 0, 1, 0, 0, 0 };
    n = session_seal(rig.client.session(), pl, 7, b, rig.now);
    s.emplace_back(b, b + n);
    n = session_seal(rig.host.session(), pl, 0, b, rig.now);
    s.emplace_back(b, b + n);
    // Local co-op's pipe lines, and an environment for Player 2's.
    const char* lines[] = { "hello 1 port=50123 code=K7QXM2PD9TRW0123456789AB", "quit",
                            "hello 1 port=1024 code=0123456789ABCDEFGHJKMNPQ\nquit\n",
                            "PATH=C:/Windows\n=C:=C:/games\nTSSM_COOP=host\nTSSM_CHILD_TSSM_INPUT=10:a\n"
                            "tssm_child_tssm_save_dir=x\nTSSM_LOCAL_PIPE=5\nTSSM_AUDIO=0" };
    for (const char* t : lines) s.emplace_back((const uint8_t*)t, (const uint8_t*)t + strlen(t));
    const char* addrs[] = { "203.0.113.5:2171", "[2001:db8::1]:7", "[::]:1", "1.2.3.4", "[::ffff:1]:9" };
    for (const char* t : addrs) s.emplace_back((const uint8_t*)t, (const uint8_t*)t + strlen(t));
    // The home network: questions of this version and a newer one; answers
    // with the shortest and longest names, a full game on another device,
    // and a newer version's.
    {
        uint8_t q[kDiscoverSize];
        n = encode_discover(q, sizeof q, kFuzzNonce);
        s.emplace_back(q, q + n);
        q[4] = 2;
        q[5] = 0x80;
        q[100] = 7;
        s.emplace_back(q, q + n);
        Offer o = fuzz_offer();
        snprintf(o.name, sizeof o.name, "H");
        n = encode_offer(b, sizeof b, o);
        s.emplace_back(b, b + n);
        snprintf(o.name, sizeof o.name, "Sixteen chars ok");
        o.state = OFFER_HAS_PLAYER;
        o.platform = 9;
        o.scene = 0;
        o.game_port = 65535;
        n = encode_offer(b, sizeof b, o);
        s.emplace_back(b, b + n);
        uint8_t newer[24] = { 'T', 'S', 'D', 'R', 2, 9, 0 };
        memcpy(newer + 7, kFuzzNonce, kDiscNonce);
        s.emplace_back(newer, newer + sizeof newer);
    }
    // Join codes as players type them.
    const char* codes[] = { "K7QX-M2PD-9TRW", "k7qx m2pd 9tro", "o1il-OIL0 lL",
                            "my own password!" };
    for (const char* t : codes)
        s.emplace_back((const uint8_t*)t, (const uint8_t*)t + strlen(t));
    return s;
}

} // namespace iNet
