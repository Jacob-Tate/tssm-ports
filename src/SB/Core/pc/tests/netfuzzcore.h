// PC-only, co-op: the fuzz targets of the network code, shared by the built-in
// mutation loop of tssm_nettest (tests/nettest.cpp) and the optional libFuzzer
// harness tssm_netfuzz (tests/netfuzz.cpp, built with TSSM_NETFUZZ=ON).
//
// Every target runs bytes through the real parsers and the real Host and
// Client, and checks invariants that must hold for ANY input: accepted input
// re-encodes byte for byte, every bound of an accepted payload holds when
// checked again independently, raw bytes never change a live session, and no
// reply before the session is larger than the datagram that provoked it.
#ifndef NETFUZZCORE_H
#define NETFUZZCORE_H

#include "iNetDisc.h"
#include "iNetSession.h"

#include <stddef.h>
#include <stdint.h>

#include <vector>

namespace iNet {

[[noreturn]] void fuzz_fail(const char* what);

// Independent re-check of every documented bound on an accepted payload.
void validate_payload(const Payload& p, Role from);

enum FuzzTarget : uint8_t
{
    FT_PARSE_PRE,     // every handshake parser, canonical re-encode
    FT_PARSE_PAYLOAD, // payload parser, both roles, canonical re-encode
    FT_HOST_RAW,      // raw datagram into a host with a live session, from 4 addresses
    FT_CLIENT_RAW,    // raw datagram into a connected client, from the host's address
    FT_HOST_SEALED,   // payload sealed with the real guest key: an authenticated hostile guest
    FT_CLIENT_SEALED, // payload sealed with the real host key: an authenticated hostile host
    FT_ADDR,          // the join box's address parser
    FT_LOCAL_LINE,    // local co-op's pipe: the line reader and parser, canonical re-format
    FT_LOCAL_ENV,     // local co-op: Player 2's environment and its block
    FT_CODE, // protocol 8: a join code or password as both sides hash it
    FT_DISC_PARSE, // the home network: DISCOVER and OFFER, canonical re-encode
    FT_DISC_RESPONDER, // a question into a host's responder, from a good source and three bad ones
    FT_DISC_FINDER, // an answer into a joiner's finder, from a good source and a bad one
    FT_COUNT
};

struct FuzzRig
{
    struct Q
    {
        NetAddr from, to;
        std::vector<uint8_t> d;
    };
    std::vector<Q> to_host, to_client;
    NetAddr host_addr, client_addr, strangers[3];
    Host host;
    Client client;
    uint64_t now = 1000 * kNsPerSec;
    uint64_t tried[FT_COUNT] = {}, accepted[FT_COUNT] = {};
    uint64_t rehandshakes = 0, payloads_seen = 0;
    uint64_t snapshots_parsed = 0, snapshots_delivered = 0, acks_delivered = 0; // M2's messages reached
    uint64_t events_seen = 0, events_delivered = 0; // M3's
    ReliableOut host_out, client_out; // M3: each side's reliable queues, as iCoop.cpp's I/O thread keeps them
    ReliableIn host_in, client_in;
    LookInbox looks = {}; // M4: delivered looks, as iCoopLook.cpp keeps the partner's
    uint64_t looks_taken = 0;
    LevelSignals levels = {}; // M4 (review): delivered EV_READY / EV_GO, as iCoop.cpp keeps them
    Counters cnt = {}; // COUNTERS not taken yet, merged as iCoop.cpp's I/O thread does
    bool cnt_have = false;
    uint64_t cnt_merged = 0; // merges of two of the same table
    // The home network: a host's responder and a joiner's finder, each with
    // its own clock (they keep rate limits of their own).
    Responder responder;
    Finder finder;
    uint64_t disc_now = 1000 * kNsPerSec;
    uint64_t disc_answered = 0, disc_listed = 0;

    bool init();
    bool ensure_session();
    void run(uint8_t target, const uint8_t* d, size_t n);
    void pump();
    void take_counters(const Counters& c, uint8_t seed);
};

// Valid messages of every kind, for seeding a mutation loop or a corpus.
std::vector<std::vector<uint8_t>> fuzz_seeds(FuzzRig& rig);

} // namespace iNet

#endif
