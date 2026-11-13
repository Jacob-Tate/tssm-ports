// PC-only, co-op (docs/MULTIPLAYER.md 14.2): finding games on the home network.
//
// A joiner broadcasts a fixed-size question (DISCOVER) on UDP 2172; a host
// that is visible answers with a short OFFER, unicast, always smaller than the
// question. Nothing in either is derived from the password: the join still
// goes through the handshake, which proves it.
//
// Pure functions and fixed tables, like iNetProto.h: no sockets, no clocks
// (the caller passes `now`), no allocation, so all of it is fuzzed in-process
// (tests/netfuzzcore.cpp). The I/O thread (iCoop.cpp) owns the sockets and
// shuttles bytes in and out of a Responder or a Finder.
//
// Every discovery version keeps the same fixed prefix in its OFFER (magic,
// disc_version, proto_version, the nonce echo), so a build can list a newer
// host as "different version" rather than lose it. A version 1 OFFER is
// parsed strictly and canonically, as every co-op parser is.
#ifndef INETDISC_H
#define INETDISC_H

#include "iNetProto.h"
#include "iNetSock.h"

#include <stddef.h>
#include <stdint.h>

namespace iNet
{

    enum : uint16_t
    {
        kDiscPort = 2172
    };
    enum : uint8_t
    {
        kDiscVersion = 1
    };

    enum : size_t
    {
        kDiscoverSize = 128, // every version's question; any other size gets no answer
        kDiscNonce = 8,
        kIdPrefix = 8, // the build ID's and the game-files hash's first bytes
        kOfferPrefix = 15, // magic[4] disc_version u8 proto u16 nonce[8], in every version
        kOfferMin = 45, // version 1 with a 1-character name
        kOfferMax = 60, // and with 16
        kMaxFound = 8, // games in a joiner's list
        kFoundPerAddress = 2, // of them from one address
        kNonceWindow = 4, // a joiner's last questions an answer may echo
        kResponderSources = 32,
    };

    extern const uint8_t kDiscQuestionMagic[4]; // "TSDQ"
    extern const uint8_t kDiscAnswerMagic[4]; // "TSDR"

    // The rate limits (docs/MULTIPLAYER.md 14.2).
    enum : uint32_t
    {
        kAnswerPerSource = 2, // answers a second to one address,
        kAnswerBurst = 4, // with this burst,
        kAnswerTotal = 20, // and to everyone together
        kFinderPerSecond = 64, // answers a joiner handles a second
    };
    static const uint64_t kFoundExpireNs =
        4ull * 1000000000ull; // a game leaves the list 4 s after its last answer

    enum DiscPlatform : uint8_t
    {
        PLAT_OTHER = 0, // never sent by a known build; any unknown value reads as "other device"
        PLAT_WINDOWS = 1,
        PLAT_ANDROID = 2,
        PLAT_SWITCH = 3,
        PLAT_LINUX = 4,
        PLAT_MACOS = 5,
    };
    uint8_t disc_this_platform();
    const char*
    disc_platform_name(uint8_t platform); // constant text; "other device" for unknown values

    // ---------------------------------------------------------------------------
    // The two messages

    // DISCOVER: magic "TSDQ", disc_version u8, flags u8, nonce[8], zero padding
    // to 128 bytes. Version 1 must have flags 0 and all-zero padding. A newer
    // version is accepted on its prefix alone (size, magic, nonce) and answered
    // with this version's OFFER, so a newer joiner still lists this host.
    struct Discover
    {
        uint8_t version; // >= 1
        uint8_t nonce[kDiscNonce];
    };
    size_t encode_discover(uint8_t* out, size_t cap, const uint8_t nonce[kDiscNonce]);
    bool parse_discover(const uint8_t* d, size_t n, Discover* out);

    enum : uint8_t
    {
        OFFER_WAITING = 0, // hosting, no partner yet
        OFFER_HAS_PLAYER = 1, // two players already
    };

    // OFFER, version 1 (little-endian):
    //   0 magic "TSDR" | 4 disc_version u8 = 1 | 5 proto_version u16 | 7 nonce[8]
    //   15 build prefix[8] | 23 files prefix[8] | 31 game_port u16 (1024-65535)
    //   33 instance u32 (not 0) | 37 state u8 (0, 1) | 38 platform u8 (any)
    //   39 scene u32 (0 or scene_valid) | 43 name_len u8 (1-16) | 44 name (name_valid)
    // 45 to 60 bytes, nothing after the name.
    struct Offer
    {
        uint8_t disc_version;
        uint16_t proto;
        uint8_t nonce[kDiscNonce];
        uint8_t build[kIdPrefix];
        uint8_t files[kIdPrefix];
        uint16_t game_port;
        uint32_t instance;
        uint8_t state;
        uint8_t platform;
        uint32_t scene;
        char name[kMaxName + 1];
    };
    // 0 when a field is out of range (the encoder refuses what the parser would).
    size_t encode_offer(uint8_t* out, size_t cap, const Offer& o);

    enum OfferKind
    {
        OFFER_BAD, // silently dropped
        OFFER_V1, // every field valid
        OFFER_NEWER, // a later disc_version: only disc_version, proto and nonce are known
    };
    // A newer OFFER is 15 to 127 bytes (still smaller than any question).
    OfferKind parse_offer(const uint8_t* d, size_t n, Offer* out);

    // ---------------------------------------------------------------------------
    // Addresses and interfaces

    // The IPv4 address of `a` (a plain or ::ffff: mapped one); FALSE for IPv6.
    bool v4_of(const NetAddr& a, uint8_t ip[4]);
    // 10/8, 172.16/12, 192.168/16, 169.254/16.
    bool v4_private(const uint8_t ip[4]);
    bool v4_loopback(const uint8_t ip[4]);
    bool v4_in_subnet(const uint8_t ip[4], const IfV4& i);
    void v4_broadcast(const IfV4& i, uint8_t out[4]);
    NetAddr v4_addr(const uint8_t ip[4], uint16_t port);

    // Where a joiner's question goes from the socket of interface `i`: the
    // limited broadcast and that network's own broadcast. Both, because some
    // systems take 255.255.255.255 into a VPN app's tunnel and others drop a
    // subnet broadcast. Returns kAskTargets.
    enum : size_t
    {
        kAskTargets = 2
    };
    size_t disc_ask_targets(const IfV4& i, uint16_t disc_port, NetAddr out[kAskTargets]);

    // One network adapter as the operating system describes it, for if_filter.
    struct IfRecord
    {
        bool up;
        bool hardware; // a real adapter (Windows: HardwareInterface); POSIX and the Switch: always
        bool loopback;
        bool point_to_point; // tunnels, PPP, most VPNs
        bool broadcast; // can broadcast (POSIX: IFF_BROADCAST)
        uint8_t count;
        struct
        {
            uint8_t ip[4];
            uint8_t prefix;
            bool preferred; // in use (Windows: not tentative, duplicate or deprecated)
        } addr[4];
    };

    // The interfaces that may hold a home network: up, hardware, broadcast,
    // neither loopback nor point-to-point; their usable private IPv4 addresses
    // with a prefix of 8 to 30; a 169.254 address only on an adapter that has no
    // other. Duplicates dropped, at most `cap` (kMaxIfs).
    size_t if_filter(const IfRecord* recs, size_t n, IfV4* out, size_t cap);

    // The question to the last host joined ([coop] address), sent unicast with
    // the broadcasts: only for a private IPv4 address (127/8 too in the loopback
    // test mode), never to the internet. FALSE otherwise.
    bool disc_probe_target(const char* address, uint16_t disc_port, bool loopback_test,
                           NetAddr* out);

    // Whether a host answers questions at all: hosting with "Visible" on, never
    // local co-op's host, and a loopback host only in the loopback test mode.
    bool disc_host_visible(bool hosting, bool visible, bool loopback_host, bool local_host,
                           bool loopback_test);

    // ---------------------------------------------------------------------------
    // Rate limiting

    struct Bucket
    {
        uint64_t at;
        uint32_t milli; // tokens x 1000
        bool started; // false: full
    };
    bool bucket_take(Bucket* b, uint64_t now, uint32_t per_second, uint32_t burst);

    // ---------------------------------------------------------------------------
    // The host's side

    struct ResponderStats
    {
        uint64_t answered, drop_size, drop_bad, drop_source, drop_rate;
    };

    struct Responder
    {
        // What the answer says, nonce aside; the I/O thread keeps it current.
        Offer offer;
        IfV4 ifs[kMaxIfs];
        uint8_t ifs_count;
        bool loopback_test; // TSSM_COOP_LAN_PORT: 127/8 sources only, and only they
        struct Source
        {
            uint8_t ip[4];
            Bucket b;
            uint64_t seen;
            bool used;
        } src[kResponderSources];
        Bucket all;
        ResponderStats st;

        // Empties the tables and the counters; keeps the offer.
        void reset();
        // A question from `from`: the answer's length in `out` (always smaller
        // than the question), or 0 for silence. Checks, in order: the size, the
        // message, the source (IPv4, port 1024 or above, private and inside one
        // of this device's subnets, neither a subnet's network or broadcast
        // address nor this device's own), that source's rate, everyone's rate.
        size_t on_question(const NetAddr& from, const uint8_t* d, size_t n, uint64_t now,
                           uint8_t* out, size_t cap);
    };

    // ---------------------------------------------------------------------------
    // The joiner's side

    enum FoundKind : uint8_t
    {
        FOUND_READY, // same version, same game files
        FOUND_VERSION, // a different co-op version (or a newer discovery version: address only)
        FOUND_FILES, // same version, different game files
    };

    struct Found
    {
        NetAddr addr; // the answer's source address with the game port (port 0: a newer version's)
        uint8_t kind;
        bool newer; // only the prefix was understood
        uint8_t state, platform;
        uint32_t scene;
        uint32_t instance;
        char name[kMaxName + 1];
        uint8_t build[kIdPrefix],
            files[kIdPrefix]; // the host's prefixes (version 1), for the tab's codes
        uint64_t last;
    };

    struct FinderStats
    {
        uint64_t answers, added, drop_rate, drop_source, drop_bad, drop_nonce, drop_full;
    };

    struct Finder
    {
        uint8_t build[kIdPrefix], files[kIdPrefix]; // this game's, to say Ready or why not
        bool files_known; // false: this game's files could not be hashed, so they are not compared
        IfV4 ifs[kMaxIfs];
        uint8_t ifs_count;
        bool loopback_test;
        uint16_t disc_port; // answers come from it
        uint8_t nonces[kNonceWindow][kDiscNonce];
        uint8_t nonce_count, nonce_next;
        Found list[kMaxFound];
        uint8_t count;
        Bucket rate;
        FinderStats st;

        // Empties the list, the questions and the counters; keeps this game's
        // prefixes, the interfaces and the mode.
        void reset();
        // A question went out with this nonce (the oldest of kNonceWindow goes).
        void asked(const uint8_t nonce[kDiscNonce]);
        // An answer from `from`: TRUE when the list changed. Checks, in order:
        // the source (private IPv4, from the discovery port), the rate, the
        // message, the nonce. At most kFoundPerAddress rows per address.
        bool on_answer(const NetAddr& from, const uint8_t* d, size_t n, uint64_t now);
        // Drops games not heard from for kFoundExpireNs: TRUE when any went.
        bool expire(uint64_t now);
    };

} // namespace iNet

#endif
