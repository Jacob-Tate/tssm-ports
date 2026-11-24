#ifndef TSSM_NETPLAY_PROTOCOL_H
#define TSSM_NETPLAY_PROTOCOL_H

/*
 * TSSM co-op relay protocol, shared by the game client (src/SB/Core/pc/iNetRelay.cpp) and the relay
 * server (server/main.cpp). Adapted from smb2-pc's and partyboard's include/netplay/protocol.h, with
 * one difference in what the server does: there is no lockstep. TSSM co-op is host-authoritative
 * (docs/MULTIPLAYER.md, section 2): the host's game simulates the world and the guest's game shows
 * it. The relay server only seats the two games in a room and carries their co-op datagrams between
 * them, so neither player forwards a port. See docs/MULTIPLAYER.md, section 15.
 *
 * The datagrams the server carries (RELAY) are the co-op protocol's own packets (iNetProto.h):
 * the handshake and every session packet are authenticated end to end with the room's code, so the
 * server can neither read the game state's meaning nor forge or alter it undetected. It treats
 * them as opaque bytes of at most TSSM_NET_MAX_RELAY.
 *
 * Transport: ENet (UDP). Channel 0 carries control messages (reliable, ordered). Channel 1 carries
 * RELAY (unsequenced, unreliable): the co-op session has its own acknowledgements, resends and
 * ordering, and a reliable channel would only add head-of-line stalls. Every packet starts with a
 * one-byte message type, then the fields in order, little-endian, no padding. Strings are a u8
 * length followed by that many bytes (no terminator).
 *
 * Rooms hold TSSM_NET_MAX_PLAYERS (2): the member that created it is the host, the other the guest.
 * The host leaving closes the room (ROOM_CLOSED to the guest). The guest leaving keeps it open, so
 * the same or another guest can join again (the co-op session handles the rejoin).
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TSSM_NET_PROTOCOL_VERSION 1
#define TSSM_NET_DEFAULT_PORT 7865 /* next to partyboard (7845) and smb2 (7855) */
#define TSSM_NET_CHANNEL_CONTROL 0
#define TSSM_NET_CHANNEL_RELAY 1
#define TSSM_NET_CHANNEL_COUNT 2
#define TSSM_NET_MAX_PLAYERS 2
#define TSSM_NET_MAX_NAME 16      /* iNetProto.h kMaxName */
#define TSSM_NET_MAX_BUILD_ID 40
#define TSSM_NET_ROOM_CODE_LEN 4
#define TSSM_NET_MAX_RELAY 1200   /* iNetProto.h kMaxDatagram */
#define TSSM_NET_CONN_NONE 0      /* no member */
#define TSSM_NET_RELAY_RATE 600   /* RELAY packets a member may send per second (burst: the same) */

/* Room codes use these letters only (no O/I/L/0/1, like the co-op join code). Codes are
 * case-insensitive on input and always sent upper-case. */
#define TSSM_NET_CODE_ALPHABET "ABCDEFGHJKMNPQRSTUVWXYZ23456789"

/* ROOM_STATE role */
#define TSSM_ROLE_HOST 0
#define TSSM_ROLE_GUEST 1

typedef enum TssmNetMsg {
    /* client -> server */
    TSSM_MSG_HELLO = 1,       /* u16 protocol, str name, str buildId */
    TSSM_MSG_CREATE_ROOM = 2, /* (empty). Answer: ROOM_STATE (the sender is the host). */
    TSSM_MSG_JOIN_ROOM = 3,   /* char code[4]. Answer: ROOM_STATE to both members, or ERROR. */
    TSSM_MSG_LEAVE_ROOM = 4,  /* (empty) */
    TSSM_MSG_RELAY = 5,       /* u16 toConn, then 1..TSSM_NET_MAX_RELAY bytes: to the member with that conn id */

    /* server -> client */
    TSSM_MSG_WELCOME = 64,     /* u16 connId (this connection's id, never 0, not reused while the server runs) */
    TSSM_MSG_ERROR = 65,       /* u8 TssmNetError, str message */
    /* char code[4], u8 yourRole (TSSM_ROLE_*), u8 count, count x (u16 connId, u8 role, str name, u16 pingMs).
     * Sent to every member whenever the membership changes, and every 2 s while in a room (pings). */
    TSSM_MSG_ROOM_STATE = 66,
    TSSM_MSG_RELAY_FROM = 67,  /* u16 fromConn, then the bytes as sent */
    TSSM_MSG_PEER_LEFT = 68,   /* u16 connId, u8 TssmLeaveReason. The guest left; the room stays open. */
    TSSM_MSG_ROOM_CLOSED = 69  /* u8 TssmLeaveReason: the host left. The receiver is no longer in a room. */
} TssmNetMsg;

typedef enum TssmNetError {
    TSSM_ERR_BAD_MESSAGE = 1,
    TSSM_ERR_VERSION = 2,      /* protocol version mismatch */
    TSSM_ERR_NO_SUCH_ROOM = 3,
    TSSM_ERR_ROOM_FULL = 4,
    TSSM_ERR_BUILD = 5,        /* buildId differs from the room host's */
    TSSM_ERR_NOT_IN_ROOM = 6,
    TSSM_ERR_ALREADY_IN_ROOM = 7,
    TSSM_ERR_NO_HELLO = 8,     /* anything before HELLO */
    TSSM_ERR_SERVER_FULL = 9
} TssmNetError;

typedef enum TssmLeaveReason {
    TSSM_LEAVE_LEFT = 1,       /* LEAVE_ROOM */
    TSSM_LEAVE_DISCONNECT = 2, /* the connection closed or timed out */
    TSSM_LEAVE_SERVER = 3      /* the server is shutting down */
} TssmLeaveReason;

/* ---- bounds-checked writer and reader (same shape as smb2-pc's smbw_* / smbr_*) ---- */

typedef struct TssmNetWriter {
    uint8_t* buf;
    size_t cap;
    size_t len;
    int overflow;
} TssmNetWriter;

typedef struct TssmNetReader {
    const uint8_t* buf;
    size_t len;
    size_t pos;
    int error;
} TssmNetReader;

static inline void tssmw_init(TssmNetWriter* w, uint8_t* buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->overflow = 0;
}
static inline void tssmw_bytes(TssmNetWriter* w, const void* p, size_t n)
{
    if (w->overflow || n > w->cap - w->len) {
        w->overflow = 1;
        return;
    }
    if (n) memcpy(w->buf + w->len, p, n);
    w->len += n;
}
static inline void tssmw_u8(TssmNetWriter* w, uint8_t v) { tssmw_bytes(w, &v, 1); }
static inline void tssmw_u16(TssmNetWriter* w, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    tssmw_bytes(w, b, 2);
}
static inline void tssmw_u32(TssmNetWriter* w, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    tssmw_bytes(w, b, 4);
}
/* Strings longer than 255 bytes are cut (callers pass bounded strings). */
static inline void tssmw_str(TssmNetWriter* w, const char* s)
{
    size_t n = s ? strlen(s) : 0;
    if (n > 255) n = 255;
    tssmw_u8(w, (uint8_t)n);
    tssmw_bytes(w, s, n);
}

static inline void tssmr_init(TssmNetReader* r, const uint8_t* buf, size_t len)
{
    r->buf = buf;
    r->len = len;
    r->pos = 0;
    r->error = 0;
}
static inline size_t tssmr_left(const TssmNetReader* r) { return r->error ? 0 : r->len - r->pos; }
static inline int tssmr_bytes(TssmNetReader* r, void* out, size_t n)
{
    if (r->error || n > r->len - r->pos) {
        r->error = 1;
        if (out && n) memset(out, 0, n);
        return 0;
    }
    if (out && n) memcpy(out, r->buf + r->pos, n);
    r->pos += n;
    return 1;
}
static inline uint8_t tssmr_u8(TssmNetReader* r)
{
    uint8_t v = 0;
    tssmr_bytes(r, &v, 1);
    return v;
}
static inline uint16_t tssmr_u16(TssmNetReader* r)
{
    uint8_t b[2];
    if (!tssmr_bytes(r, b, 2)) return 0;
    return (uint16_t)(b[0] | (b[1] << 8));
}
static inline uint32_t tssmr_u32(TssmNetReader* r)
{
    uint8_t b[4];
    if (!tssmr_bytes(r, b, 4)) return 0;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
/* Reads a string into out (always terminated). A string longer than cap-1 is an error, not a cut:
 * names and build ids have fixed maxima on the wire. Bytes below 0x20 and 0x7F are an error too. */
static inline int tssmr_str(TssmNetReader* r, char* out, size_t cap)
{
    uint8_t n = tssmr_u8(r);
    size_t i;
    if (cap == 0) {
        r->error = 1;
        return 0;
    }
    out[0] = 0;
    if (r->error || (size_t)n >= cap || n > r->len - r->pos) {
        r->error = 1;
        return 0;
    }
    for (i = 0; i < n; i++) {
        uint8_t c = r->buf[r->pos + i];
        if (c < 0x20 || c == 0x7F) {
            r->error = 1;
            return 0;
        }
        out[i] = (char)c;
    }
    out[n] = 0;
    r->pos += n;
    return 1;
}

/* Upper-cases a room code in place and checks it against TSSM_NET_CODE_ALPHABET (O and I read as 0
 * and 1 are NOT accepted here; the client maps them before sending). */
static inline int tssm_code_valid(char code[TSSM_NET_ROOM_CODE_LEN])
{
    int i;
    for (i = 0; i < TSSM_NET_ROOM_CODE_LEN; i++) {
        char c = code[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (!c || !strchr(TSSM_NET_CODE_ALPHABET, c)) return 0;
        code[i] = c;
    }
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* TSSM_NETPLAY_PROTOCOL_H */
