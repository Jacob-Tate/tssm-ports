// PC-only, co-op (docs/MULTIPLAYER.md, 4.4): the wire protocol.
//
// Pure functions from bytes to validated structs and back. No sockets, no
// clocks, no allocation, no game headers, so all of it can be fuzzed in-process
// (tests/nettest.cpp, tests/netfuzz.cpp).
//
// Parsing is strict and canonical: a datagram is accepted only if it is
// exactly what encode_* would produce for the struct it decodes to. Every
// length is bounded by a constant before anything is read against it, every
// enum and index is range-checked, floats must be finite and inside their
// limit, reserved bits and padding must be zero, and trailing bytes are an
// error. The fuzz invariant is therefore: accepted => re-encode is identical.
//
// A message type is accepted only from the milestone that uses it, and only
// from the side that sends it. M1 accepts PLAYER_STATE, PING, PONG and
// DISCONNECT from either side; M2 adds SNAPSHOT (host to guest), SNAP_ACK
// (guest to host) and STALL (either side); M3 adds EVENT and REL_ACK (either
// side, each kind of event only from the side that sends it) and PICKUPS (host
// to guest). M4 adds event kinds (level changes, cutscenes, the players'
// looks) and the model a PLAYER_STATE's player is drawn with; protocol 7 adds
// PROGRESS and COUNTERS (host to guest) and the upgrade events. INPUT, reserved
// for later, is a protocol violation until its parser, tests and fuzz targets
// exist.
#ifndef INETPROTO_H
#define INETPROTO_H

#include <stddef.h>
#include <stdint.h>

namespace iNet {

// ---------------------------------------------------------------------------
// Constants

// 2: M2's SNAPSHOT and SNAP_ACK; 3: STALL, DC_STALLED; 4: M3's EVENT, REL_ACK, PICKUPS; 5: M4's events and
// PLAYER_STATE's model; 6: the same messages on the 0.9 port, whose game differs from 0.8's (the Neptune
// cutscene's disc seek among others), so a 0.8 co-op copy and this one refuse each other by version; 7: one
// shared progress (PROGRESS, EV_UPGRADE), the car's and the lightning's hits, hits on breakables, PICKUPS'
// second bit array, EV_PICKUP's way, and COUNTERS (the challenges' counts and texts); 8: the build ID from
// the shared code instead of the commit (tools/coop_id.cmake), one for every platform, and O, I and L read
// as 0 and 1 in a join code
enum : uint16_t
{
    kProtoVersion = 9 // 9: EV_BOSS (docs/MULTIPLAYER.md 16)
};
extern const uint8_t kMagic[4]; // "TSCP"

enum : uint16_t { kDefaultPort = 2171 };

enum : size_t
{
    kMaxDatagram = 1200,  // never sent larger; anything larger received is dropped unread
    kRecvBuffer = 2048,   // what a receive reads into, so an oversized datagram is seen as such
    kPreHeader = 7,       // magic[4] version u16 type u8
    kHelloSize = 1200,    // HELLO is padded to the maximum: replies are never larger than requests
    kTagSize = 16,        // truncated HMAC-SHA256 on every session packet
    kDataHeader = 17,     // type u8, session u32, seq u32, ack u32, ack_bits u32
    kMaxPayload = kMaxDatagram - kDataHeader - kTagSize,
    kBuildIdSize = 16,
    kHashSize = 32,
    kNonceSize = 16,
    kSaltSize = 16,
    kCookieSize = 32,
    kProofSize = 32,
    kMaxName = 16,
    kMaxPassword = 63,
    kMaxPlayers = 2,
    kPlayerStateSize = 43,
    // M2 (docs/MULTIPLAYER.md 5.3)
    kMaxNetIds = 1024,       // things in one level's shared table
    kMaxSnapEntries = 96,    // entries in one SNAPSHOT
    kSnapHeaderSize = 24,    // SNAPSHOT before its entries
    kSnapBaseWindow = 64,    // a base at most this many snapshots back
    kSnapAckSize = 4,
    // The most a SNAPSHOT body is made of: with its own 3-byte message header,
    // a PLAYER_STATE (46), a PING and a PONG (7 each), a STALL (5) and a
    // REL_ACK (5) it stays under kMaxPayload. The host's builder takes less
    // when EVENTs or PICKUPS go in the same datagram (iCoopWorld.cpp).
    kSnapBodyMax = 1094,
    // M3 (docs/MULTIPLAYER.md 6.3)
    kMaxEventsPerMsg = 8,    // events in one EVENT
    kEventHeaderSize = 3,    // EVENT before its events: first u16, count u8
    kMaxPickups = 1024,      // pickups in one level's pickup table
    kPickupsHeaderSize = 10, // PICKUPS before its bits: scene u32, table u32, count u16
    kProgressSize = 20, // PROGRESS (protocol 7)
    kCountersHeaderSize = 11, // COUNTERS before its entries: scene, table, three counts
    kCounterEntrySize = 3, // index u8, value i16
    // M4 (docs/MULTIPLAYER.md 7)
    kMaxShows = 256,         // cutscenes and camera fly-throughs in one level's show table
};

enum : uint16_t { kNoThing = 0xFFFF }; // EV_DAMAGE: no shared enemy behind the hit

enum : uint32_t { kMinIters = 10000, kMaxIters = 1000000, kDefaultIters = 100000 };

static const float kWorldLimit = 100000.0f;
static const float kAnimTimeLimit = 600.0f;
static const float kPushLimit = 200.0f; // EV_DAMAGE DK_PUSH: a knockback velocity, units a second, per axis

enum PktType : uint8_t
{
    PKT_HELLO = 1,     // guest to host
    PKT_CHALLENGE = 2, // host to guest
    PKT_AUTH = 3,      // guest to host
    PKT_WELCOME = 4,   // host to guest
    PKT_REJECT = 5,    // host to guest
    PKT_DATA = 0x80,   // both, after the handshake
};

enum RejectReason : uint8_t
{
    REJ_VERSION = 1,
    REJ_BUILD = 2,
    REJ_ASSETS = 3,
    REJ_FULL = 4,
    REJ_PASSWORD = 5,
    REJ_BLOCKED = 6,
    REJ_BUSY = 7,
    REJ_END
};

// The refusal in plain words, for the Co-op tab. Constant strings only.
const char* reject_text(uint8_t reason);

// The build ID both games compare: the first 16 bytes of SHA-256 over the
// shared code's fingerprint (tools/coop_id.cmake, "unknown" without git) and
// kProtoVersion. test_salt gives a different one (TSSM_TEST_COOP_BUILD_SALT).
void build_id(const char* coop_id, bool test_salt, uint8_t out[kBuildIdSize]);
// The co-op version code the Co-op tab and the log show: the ID's first 4
// bytes in lower-case hex.
void version_code(const uint8_t id[kBuildIdSize], char out[9]);

// A join code or password as both sides hash it: spaces, tabs and dashes
// dropped, letters upper case, and O read as 0, I and L as 1 (the codes'
// alphabet has none of the three). FALSE when empty or over kMaxPassword.
bool normalize_code(const char* in, char* out, size_t out_size);

enum MsgType : uint8_t
{
    MSG_INPUT = 1,        // reserved (later milestones): rejected
    MSG_PLAYER_STATE = 2, // both
    MSG_SNAPSHOT = 3,     // host to guest (M2)
    MSG_EVENT = 4,        // both (M3): reliable, ordered game events (EV_*)
    MSG_REL_ACK = 5,      // both (M3): the next event id the sender expects
    MSG_SNAP_ACK = 6,     // guest to host (M2)
    MSG_PING = 7,         // both
    MSG_PONG = 8,         // both
    MSG_DISCONNECT = 9,   // both
    MSG_STALL = 10,       // both (M2): the sender's game has not run a frame for this long
    MSG_PICKUPS = 11,     // host to guest (M3): which of the level's pickups are taken on the host
    MSG_PROGRESS =
        12, // host to guest (protocol 7): the shared progress (tokens, shiny objects, moves)
    MSG_COUNTERS = 13, // host to guest (protocol 7): counters a text shows, and texts
    MSG_END
};

enum DisconnectReason : uint8_t
{
    DC_QUIT = 1,
    DC_KICKED = 2,
    DC_TIMEOUT = 3,
    DC_PROTOCOL = 4,
    DC_STALLED = 5, // the sender's game stopped running frames for longer than kStallLimit
    DC_END
};

enum class Role { Host, Client }; // who SENT the payload being parsed

// ---------------------------------------------------------------------------
// Handshake messages

struct Hello
{
    uint16_t version;
    uint8_t client_nonce[kNonceSize]; // frozen prefix: offsets 7..22 in every version
    uint8_t build_id[kBuildIdSize];
    uint8_t asset_hash[kHashSize];
};

struct Challenge
{
    uint8_t client_nonce[kNonceSize]; // echo: an off-path spoofer does not know it
    uint8_t cookie[kCookieSize];
    uint32_t cookie_time;
    uint8_t salt[kSaltSize];
    uint32_t iters;
    uint8_t host_nonce[kNonceSize];
};

struct Auth
{
    uint8_t client_nonce[kNonceSize];
    uint8_t host_nonce[kNonceSize];
    uint8_t cookie[kCookieSize];
    uint32_t cookie_time;
    uint8_t build_id[kBuildIdSize];
    uint8_t asset_hash[kHashSize];
    uint8_t name_len;
    char name[kMaxName + 1];
    uint8_t proof[kProofSize];
};

struct Welcome
{
    uint32_t session_id;
    uint8_t slot;
    uint8_t name_len;
    char name[kMaxName + 1];
    uint8_t proof[kProofSize];
};

struct Reject
{
    uint16_t version; // the host's, so "update your game" can say which
    uint8_t client_nonce[kNonceSize];
    uint8_t reason;
};

// Names: 1..16 of [A-Za-z0-9 _.-], no leading/trailing space. Shown as text,
// never used as a path, a format string, or a key.
bool name_valid(const char* s, size_t n);

size_t encode_hello(uint8_t* out, size_t cap, const Hello& m);
size_t encode_challenge(uint8_t* out, size_t cap, const Challenge& m);
size_t encode_auth(uint8_t* out, size_t cap, const Auth& m);
size_t encode_welcome(uint8_t* out, size_t cap, const Welcome& m);
size_t encode_reject(uint8_t* out, size_t cap, const Reject& m);

// Header only: magic, version, type. False if not ours.
bool peek_pre(const uint8_t* d, size_t n, uint16_t* version, uint8_t* type);
// The version-independent HELLO prefix, for replying REJECT_VERSION.
bool peek_hello_nonce(const uint8_t* d, size_t n, uint8_t nonce[kNonceSize]);

bool parse_hello(const uint8_t* d, size_t n, Hello* m);
bool parse_challenge(const uint8_t* d, size_t n, Challenge* m);
bool parse_auth(const uint8_t* d, size_t n, Auth* m);
bool parse_welcome(const uint8_t* d, size_t n, Welcome* m);
bool parse_reject(const uint8_t* d, size_t n, Reject* m);

// ---------------------------------------------------------------------------
// Session payload messages

enum : uint8_t
{
    PS_VISIBLE = 1 << 0,
    PS_SPECIAL = 1 << 1,  // bungee, cruise bubble, out of bounds, slide, car, Spongeball, boulder
    PS_PAUSED = 1 << 2,
    PS_CUTSCENE = 1 << 3,
    PS_DEAD = 1 << 4,     // dead or respawning
    PS_FLAG_MASK = 0x1F,  // bits 5-7 reserved, zero
};

enum : uint8_t
{
    CHAR_SPONGEBOB = 0,
    CHAR_PATRICK = 1,
    CHAR_END
};

enum : uint8_t { kPlayerHealthMax = 32 };

// PLAYER_STATE's `model` (M4): which of the game's 27 player models (the
// game's ePlayerEnum: SpongeBob's eleven outfits, Patrick's eleven, the three
// SpongeBob-and-Patrick slide models, the Patty Wagon, the Spongeball) the
// sender's player is drawn with, or kModelUnknown. The receiver draws the
// partner with its own copy of that model when its level has it loaded.
enum : uint8_t
{
    kModelEnd = 27,
    kModelUnknown = 0xFF,
};

// EV_LOOK's costumes: 0 is the game's own outfit (the level's), 1..4 the
// extras costumes of that character, in the order of the game's own list
// (docs/MULTIPLAYER.md 7.4).
enum : uint8_t { kCostumeChoices = 5 };

// PLAYER_STATE: the sender's own player, 30 times a second (4.4).
struct PlayerState
{
    uint8_t slot;       // 0 host, 1 guest; the session layer checks it is the sender's
    uint32_t frame;     // the sender's frame counter: newest wins (frame_newer)
    uint32_t scene;     // the 4-character scene ID, each byte A-Z or 0-9
    uint8_t character;  // CHAR_*
    uint8_t flags;      // PS_*
    float pos[3];       // finite, |v| <= kWorldLimit
    uint16_t yaw;       // 65536 = full turn
    int16_t vel[3];     // hundredths of a unit per second
    uint32_t anim;      // an xAnimState::ID, looked up by the receiver
    float anim_time;    // finite, 0..kAnimTimeLimit
    uint8_t health;     // <= kPlayerHealthMax
    uint16_t parts;     // bit i: model i of the character's chain is visible
    uint8_t model;      // M4: below kModelEnd, or kModelUnknown
};

bool scene_valid(uint32_t scene);

// Serial-number comparison (RFC 1982 style) of two frame counters: true when
// `a` is after `b`, across a wrap.
inline bool frame_newer(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }

// --- SNAPSHOT (M2, docs/MULTIPLAYER.md 5.3) -----------------------------------

enum : uint8_t
{
    SE_POS = 1 << 0,   // f32 x 3
    SE_ROT = 1 << 1,   // i16 x 4: a quaternion scaled to 32767
    SE_ANIM = 1 << 2,  // u32 state ID + f32 time
    SE_FLAGS = 1 << 3, // u8 WF_*
    SE_ALPHA = 1 << 4, // u8
    SE_MASK = 0x1F,    // bits 5-7 reserved, zero
};

enum : uint8_t
{
    WF_SHOWN = 1 << 0,
    WF_ENABLED = 1 << 1,
    WF_BROKEN = 1 << 2,
    WF_STILL = 1 << 3,   // its animation is not running on the host (not updated, or stopped)
    WF_DEATHS = 3 << 4,  // how many death effects it has played, modulo 4
    WF_DEATHS_SHIFT = 4,
    WF_MASK = 0x3F,      // bits 6-7 reserved, zero
};

enum : uint8_t
{
    SNAP_COMPLETE = 1 << 0, // nothing that changed was left out for room
    SNAP_FLAG_MASK = 0x01,
};

struct SnapEntry
{
    uint16_t id;   // net ID: < Snapshot::things, ascending within a snapshot
    uint8_t mask;  // SE_*, not 0
    float pos[3];  // SE_POS: finite, |v| <= kWorldLimit
    int16_t rot[4]; // SE_ROT: x y z w, each in [-32767, 32767], not all zero
    uint32_t anim; // SE_ANIM: an xAnimState::ID, looked up by the receiver
    float anim_time; // SE_ANIM: finite, 0..kAnimTimeLimit
    uint8_t flags; // SE_FLAGS: WF_*
    uint8_t alpha; // SE_ALPHA
};

struct Snapshot
{
    uint32_t scene;   // scene_valid
    uint32_t table;   // the hash of the level's shared table
    uint16_t things;  // the table's length: 1..kMaxNetIds
    uint32_t seq;     // not 0
    uint32_t base;    // 0, or before seq by 1..kSnapBaseWindow (serial order)
    uint32_t host_ms; // the host's clock
    uint8_t flags;    // SNAP_*
    uint8_t count;    // 0..kMaxSnapEntries
    SnapEntry e[kMaxSnapEntries];
};

// The bytes one entry takes on the wire (2 + 1 + its fields).
size_t snap_entry_size(uint8_t mask);

// --- EVENT, REL_ACK, PICKUPS (M3, docs/MULTIPLAYER.md 6.3) ---------------------

enum EventType : uint8_t
{
    EV_HIT = 1,    // guest to host: the guest's attack hit a shared enemy (a claim the host checks)
    EV_DAMAGE = 2, // host to guest: an enemy's attack hit the guest (the guest's game applies it)
    EV_PICKUP = 3, // guest to host: the guest touched a shared pickup (a claim the host checks)
    EV_GRANT = 4,  // host to guest: the answer to an EV_PICKUP
    // M4 (docs/MULTIPLAYER.md 7)
    EV_LEVEL = 5,  // host to guest: the host is going to the level `scene`
    EV_READY = 6,  // guest to host: the guest has loaded `scene` and waits to start it
    EV_GO = 7,     // host to guest: the host starts `scene` now
    EV_SHOW = 8,   // host to guest: a cutscene or fly-through of `scene` started or ended on the host
    EV_LOOK = 9,   // either side: the sender's character and costumes
    // Protocol 7
    EV_UPGRADE =
        10, // guest to host: the guest's game would give its player a move (a claim the host checks)
    EV_UPGRADE_ACK = 11, // host to guest: the answer to an EV_UPGRADE
    // Protocol 9 (docs/MULTIPLAYER.md 16)
    EV_BOSS = 12, // host to guest: a shared boss's fight state (BOSS_*: state, stage, camera, health, attack)
    EV_POOL = 13, // host to guest: during a boss fight, the players' one health (dir[0], 0..kPlayerHealthMax)
    EV_POOL_DELTA = 14, // guest to host: the guest's own health changed by dir[0] (nonzero, +-kPlayerHealthMax)
    EV_END
};

// EV_SHOW: what happened to show `id` of the level's show table.
enum ShowKind : uint8_t
{
    SHOW_CUTSCENE_START = 1,
    SHOW_CUTSCENE_END = 2,
    SHOW_FLY_START = 3,
    SHOW_FLY_END = 4,
    SHOW_END
};

// EV_SHOW's `level` for SHOW_CUTSCENE_START: how the host's game started it.
enum : uint8_t
{
    SHOWF_PAUSE_MUSIC = 1 << 0, // the music pauses while it plays
    SHOWF_LOAD_NOW = 1 << 1,    // loaded at once (the game waits for it)
    SHOWF_MASK = 0x03,
};

// EV_HIT: which of the player's attacks. The game's own damage types are
// mapped to these on each side (iCoopWorld.cpp); nothing else is accepted.
enum AttackKind : uint8_t
{
    ATK_SIDE = 1,      // SpongeBob's Karate Spin; Patrick's Star Spin and belly
    ATK_BELOW = 2,     // SpongeBob's Bash
    ATK_ABOVE = 3,     // Patrick's Smash (from above)
    ATK_TOSS = 4,      // something Patrick threw
    ATK_BOWL = 5,      // SpongeBob's SpongeBowl
    ATK_CRUISE = 6,    // SpongeBob's cruise bubble
    ATK_CARTWHEEL = 7, // Patrick's Cartwheel
    ATK_SLIDE = 8,     // sliding into it
    ATK_CAR = 9, // the Patty Wagon running into it (protocol 7)
    ATK_ZAP = 10, // the upgraded SpongeBowl's lightning (protocol 7)
    ATK_END
};

// EV_DAMAGE: how the guest's game applies the hit.
enum DamageKind : uint8_t
{
    DK_PLAIN = 0, // no knockback (zEntPlayer_Damage)
    DK_FROM = 1,  // knocked back away from `pos` (zEntPlayer_DamageNPCKnockBack; `sub` its kind)
    DK_PUSH = 2,  // knocked back with the velocity `pos` (zEntPlayer_Damage with a knockback)
    DK_END
};

enum : uint8_t
{
    kDamageSubEnd = 4, // DK_FROM: the game's ePlayerNMEDamage (default, laser, projectile, particle)
    kPowerupMin = 1,   // EV_HIT: the attack's power-up level, as the game's damage table has them
    kPowerupMax = 3,
};

// EV_GRANT: the host's answer to the guest's EV_PICKUP.
enum GrantResult : uint8_t
{
    GR_YOURS = 1, // the guest took it: its game plays the pickup (and gets the health)
    GR_NO = 2,    // not taken (gone already, too far, too fast, another level)
    GR_END
};

// EV_PICKUP's `kind`: how the guest's game came to take it (protocol 7).
enum PickupWay : uint8_t
{
    PW_TOUCHED = 0, // its player touched one the host's game also has there
    PW_REVEALED = 1, // touched, and the guest's own game showed it (a task or challenge of its own)
    PW_GIVEN = 2, // the guest's own game gave it (a script, a race's end)
    PW_END
};

// EV_UPGRADE's `kind` and `sub` (protocol 7): a move a talk teaches (sub
// 0..7, the game's PowerUp index), or an upgrade bought in the pause menu
// (sub 0..9; 8 and 9 are SpongeBob's and Patrick's health).
enum UpgradeKind : uint8_t
{
    UPG_TAUGHT = 1,
    UPG_BOUGHT = 2,
    UPG_END
};
enum : uint8_t
{
    kPowerUps = 10,
    kMoves = 8
};

// EV_BOSS (protocol 9): which boss (`kind`), and the limits of its fields:
// dir[0] the boss's own state number, dir[1] its health, dir[2] its attack;
// level its stage, sub its camera state.
enum BossKind : uint8_t
{
    BOSS_FROGFISH = 1,
    BOSS_END
};
enum : int16_t
{
    kBossStateMax = 0x1FFF, // Neptune's states go to 0x1005
    kBossHealthMax = 999,
    kBossAttackMax = 63,
};
enum : uint8_t
{
    kBossStageMax = 15,
    kBossCamMax = 31,
};

// One game event. Only the fields its type uses are on the wire; the rest are
// zero after parsing and ignored when encoding.
struct GameEvent
{
    uint16_t rel_id;  // the reliable layer's: EVENT's first id plus its place in the message
    uint8_t type;     // EV_*
    uint32_t scene;   // every type: the level it is about (scene_valid); LOOK: the sender's, or 0
    uint32_t table;   // HIT: the shared-thing table's hash; PICKUP, GRANT: the pickup table's; SHOW: the
                      // show table's
    uint16_t id; // HIT: a net ID (< kMaxNetIds: an enemy, or a breakable); DAMAGE: one, or kNoThing;
    // PICKUP, GRANT: < kMaxPickups; SHOW: < kMaxShows
    uint8_t
        kind; // HIT: ATK_*; DAMAGE: DK_*; PICKUP: PW_*; GRANT: GR_*; SHOW: SHOW_*; LOOK: the character,
    // CHAR_*; UPGRADE, UPGRADE_ACK: UPG_*; BOSS: BOSS_*
    uint8_t level;    // HIT: kPowerupMin..kPowerupMax; DAMAGE: health taken, 1..kPlayerHealthMax; SHOW:
                      // SHOWF_* (SHOW_CUTSCENE_START only, 0 otherwise); LOOK: SpongeBob's costume,
    // < kCostumeChoices; UPGRADE_ACK: GR_YOURS (given) or GR_NO
    uint8_t sub; // DAMAGE: below kDamageSubEnd with DK_FROM, 0 otherwise; LOOK: Patrick's costume;
    // UPGRADE, UPGRADE_ACK: the PowerUp index (below kMoves for UPG_TAUGHT, kPowerUps for
    // UPG_BOUGHT)
    int16_t dir[3];   // HIT: the hit's direction scaled to 32767 (no -32768), or all zero
    float pos[3];     // HIT: where the guest drew the target; DAMAGE: DK_FROM the point, DK_PUSH the
                      // velocity (each within kPushLimit), DK_PLAIN all zero
};

// The bytes one event of this type takes on the wire (0: not a type).
size_t event_size(uint8_t type);

// PICKUPS: bit i of `bits` set while the host's pickup i is taken (on its
// way to a player, or collected); bit i of `avail` (protocol 7) while it is
// there to be taken on the host (shown, its collision on). Never both.
struct Pickups
{
    uint32_t scene;  // scene_valid
    uint32_t table;  // the pickup table's hash
    uint16_t count;  // the table's length, 1..kMaxPickups
    uint8_t bits[kMaxPickups / 8]; // (count + 7) / 8 on the wire; the bits past count zero
    uint8_t avail[kMaxPickups / 8]; // as many again, after `bits`
};

// PROGRESS (protocol 7, host to guest): the host's save's progress, which the
// guest's game shows and plays by while it is in the session (one shared
// progress, docs/MULTIPLAYER.md 13). Fixed length, each field in its range.
struct Progress
{
    uint16_t tokens; // Goofy Goober tokens, <= kMaxTokens
    uint32_t shiny; // shiny objects, <= kMaxShiny
    uint8_t range; // upgrade points earned so far (the shiny objects' range), <= kMaxRangeIdx
    uint8_t unused; // upgrade points not spent, <= kMaxRange
    int8_t
        spent; // upgrade points spent, -kMaxRange..kMaxRange (a save can start with points to spend)
    uint8_t powerup[kPowerUps]; // moves 0..7: <= 2; 8, 9 (health): <= 3
    uint8_t chests; // this level's treasure chests found, <= kMaxChests
};
enum : uint32_t
{
    kMaxTokens = 999,
    kMaxShiny = 99999,
    kMaxRange = 15,
    kMaxRangeIdx = 14, // the game's own cap: zRewardsMgr's table has 14 entries
    kMaxChests = 99
};
bool progress_valid(const Progress& g);

// COUNTERS (protocol 7, host to guest): the level's counters a text shows
// ("{counter:NAME}", the arenas' "N left") and the text boxes the level's
// logic for them writes ("Wave 1 Complete!"), which the guest's game shows
// as the host has them (docs/MULTIPLAYER.md 13). Both games make the same
// table of them from the level; the guest checks each value against its own
// copy's range and each text against its own list.
enum : uint32_t
{
    kMaxCounters = 32, // counters in one level's table
    kMaxTextBoxes = 8, // text boxes in it
    kMaxBoxTexts = 16, // texts one box may show
    kCounterLimit = 9999 // |value|, and the top of a counter something counts up
};
struct CounterEntry
{
    uint8_t index; // < Counters::counters, ascending within a message
    int16_t value; // -kCounterLimit..kCounterLimit
};
struct Counters
{
    uint32_t scene; // scene_valid
    uint32_t table; // the counter table's hash
    uint8_t counters; // the table's counters, 0..kMaxCounters
    uint8_t boxes; // its text boxes, 0..kMaxTextBoxes; not both 0
    uint8_t count; // entries: those that changed lately, or all; 0..counters
    CounterEntry e[kMaxCounters];
    uint8_t text[kMaxTextBoxes]; // per box: 0 not shown, k (1..kMaxBoxTexts) its list's text k
};
bool counters_valid(const Counters& c);
// The bytes a COUNTERS message (with its 3-byte message header) takes.
inline size_t counters_msg_size(unsigned count, unsigned boxes)
{
    return 3 + kCountersHeaderSize + kCounterEntrySize * count + boxes;
}
// A newer COUNTERS on top of one not taken yet: the same table's entries
// together (the newer's value where both have one) and the newer's texts;
// another table's replaces it.
void counters_merge(Counters* into, const Counters& newer);

enum : uint32_t
{
    HAS_PSTATE = 1u << MSG_PLAYER_STATE,
    HAS_SNAPSHOT = 1u << MSG_SNAPSHOT,
    HAS_SNAP_ACK = 1u << MSG_SNAP_ACK,
    HAS_PING = 1u << MSG_PING,
    HAS_PONG = 1u << MSG_PONG,
    HAS_DISCONNECT = 1u << MSG_DISCONNECT,
    HAS_STALL = 1u << MSG_STALL,
    HAS_EVENT = 1u << MSG_EVENT,
    HAS_REL_ACK = 1u << MSG_REL_ACK,
    HAS_PICKUPS = 1u << MSG_PICKUPS,
    HAS_PROGRESS = 1u << MSG_PROGRESS,
    HAS_COUNTERS = 1u << MSG_COUNTERS,
    HAS_M1_ALL = HAS_PSTATE | HAS_PING | HAS_PONG | HAS_DISCONNECT,
    HAS_ALL = HAS_M1_ALL | HAS_SNAPSHOT | HAS_SNAP_ACK | HAS_STALL | HAS_EVENT | HAS_REL_ACK |
        HAS_PICKUPS | HAS_PROGRESS | HAS_COUNTERS,
    // only the host sends these
    HAS_HOST_ONLY = HAS_SNAPSHOT | HAS_PICKUPS | HAS_PROGRESS | HAS_COUNTERS,
    HAS_GUEST_ONLY = HAS_SNAP_ACK,              // only the guest sends these
};

struct Payload
{
    uint32_t has;
    PlayerState pstate;
    Snapshot snap;
    uint32_t snap_ack; // SNAP_ACK: the newest snapshot seq the guest applied; not 0
    uint32_t ping, pong;
    uint8_t disconnect_reason; // DC_*
    // STALL: how long the sender's game thread has not run a frame, in ms,
    // 1..65535 (saturated). Sent by the sender's I/O thread, which keeps the
    // session up through a stall (a held window, a hitch, a level load).
    uint16_t stall_ms;
    // EVENT (M3): ev_count (1..kMaxEventsPerMsg) events with the ids ev_first
    // on; each type only from the side that sends it.
    uint16_t ev_first;
    uint8_t ev_count;
    GameEvent ev[kMaxEventsPerMsg];
    // REL_ACK (M3): the next event id the sender expects (every one before it arrived).
    uint16_t rel_ack;
    // PICKUPS (M3, host only).
    Pickups pick;
    // PROGRESS (protocol 7, host only).
    Progress prog;
    // COUNTERS (protocol 7, host only).
    Counters cnt;
};

void payload_clear(Payload* p);

// Message framing is [type u8][len u16][body], types in strictly ascending
// order (each at most once), no trailing bytes. `from` is who sent it: M1's
// four types, STALL, EVENT and REL_ACK come from either side (EV_HIT and
// EV_PICKUP only from the guest, EV_DAMAGE and EV_GRANT only from the host),
// SNAPSHOT, PICKUPS, PROGRESS and COUNTERS only from the host and SNAP_ACK
// only from the guest.
bool parse_payload(const uint8_t* d, size_t n, Role from, Payload* out);
// Canonical encoding of a whole payload. 0 when it does not fit or a field is
// out of its range (nothing invalid is ever sent).
size_t encode_payload(const Payload& p, Role from, uint8_t* out, size_t cap);

// One PLAYER_STATE body on its own (42 bytes), for tests.
size_t encode_player_state(const PlayerState& s, uint8_t* out, size_t cap);
bool parse_player_state(const uint8_t* d, size_t n, PlayerState* s);

// One SNAPSHOT body on its own, for tests and the snapshot builder's budget.
size_t encode_snapshot(const Snapshot& s, uint8_t* out, size_t cap);
bool parse_snapshot(const uint8_t* d, size_t n, Snapshot* s);
// The bytes a SNAPSHOT body with these entries takes.
size_t snapshot_size(const Snapshot& s);

// The bytes the EVENT message (with its 3-byte message header) takes for the
// first `count` of these events, the PICKUPS message for this many pickups,
// and the PROGRESS message.
size_t event_msg_size(const GameEvent* ev, unsigned count);
size_t pickups_msg_size(uint16_t count);
inline size_t progress_msg_size()
{
    return 3 + kProgressSize;
}
// One event's rules (its type's fields), for `from` the side sending it.
bool event_valid(const GameEvent& e, Role from);
// EV_HIT's direction as a unit vector (any non-zero dir, whatever its length,
// gives length 1). FALSE, and `out` all zero, for a hit with no direction.
bool hit_dir_unit(const int16_t dir[3], float out[3]);

// The partner's look as the receiver keeps it (EV_LOOK, docs/MULTIPLAYER.md
// 7.4). A look is state: the newest one replaces the one before, so every
// look that passed event_valid is taken, however many come at once (a
// receiver back from a stall drains a whole backlog in one frame, and the
// sender sends a look only when it changes, so a dropped newest look would
// never come again). Taking one is a few stores; what a flood could make
// costly is the receiver's log, so arrivals past kLooksPerSecond in a
// second are counted for it (`over`), and the caller logs those sparingly.
enum : int32_t { kLooksPerSecond = 8 };
struct LookInbox
{
    bool known;          // a look has come this session
    uint8_t character;   // CHAR_*
    uint8_t costume[2];  // SpongeBob's, Patrick's (< kCostumeChoices)
    uint32_t scene;      // the level it was sent from, or 0
    uint64_t window_ns;  // the start of the current one-second window
    int32_t in_window;   // looks taken in it
    uint32_t over;       // looks past kLooksPerSecond in their second, this session
};
void look_inbox_reset(LookInbox* in);
// Takes `e` (an EV_LOOK) at `now_ns`. Returns TRUE when it changed what is
// known; `*over_rate` (may be NULL) says whether this look came past the
// rate (it is taken all the same).
bool look_inbox_take(LookInbox* in, const GameEvent& e, uint64_t now_ns, bool* over_rate);

// The partner's level signals as the receiver keeps them (docs/MULTIPLAYER.md
// 7.2): the newest EV_READY's and EV_GO's level, apart from the event inbox
// (the game thread waits for them between levels). An EV_LEVEL to a level
// forgets an EV_GO for that same level from an earlier visit, so the trip's
// start waits for the trip's own EV_GO (events are delivered in order, so it
// comes after the EV_LEVEL); an EV_GO for another level is kept.
struct LevelSignals
{
    uint32_t ready_scene; // 0: none
    uint32_t go_scene;    // 0: none
};
void level_signals_reset(LevelSignals* s);
// TRUE when `e` was an EV_READY or EV_GO and is kept here (not for the
// inbox); every other event, an EV_LEVEL too, goes on to the inbox.
bool level_signals_take(LevelSignals* s, const GameEvent& e);

// Data packet header (cleartext, covered by the tag).
struct DataHeader
{
    uint32_t session_id;
    uint32_t seq;
    uint32_t ack;
    uint32_t ack_bits;
};
void encode_data_header(uint8_t out[kDataHeader], const DataHeader& h);
bool parse_data_header(const uint8_t* d, size_t n, DataHeader* h);

} // namespace iNet

#endif
