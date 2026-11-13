// PC-only, co-op (docs/MULTIPLAYER.md): the UDP socket seam.
//
// One non-blocking UDP socket per endpoint, plus IPv4-only ones for finding
// games on the home network (iNetDisc.h). Addresses are kept as 16-byte
// IPv6, with IPv4 as ::ffff:a.b.c.d, so everything above this seam handles one
// kind. Backends: Winsock2 (iNetSockWin32.cpp) and BSD/POSIX
// (iNetSockPosix.cpp: Linux, Android, macOS; libnx exposes the same BSD calls
// after socketInitializeDefault()). Name resolution is deliberately absent:
// the join box takes a numeric address, so nothing typed in it is ever handed
// to a resolver.
//
// Nothing here is game code and nothing includes a game header, so the unit
// test (tests/nettest.cpp) links it on its own.
#ifndef INETSOCK_H
#define INETSOCK_H

#include <stddef.h>
#include <stdint.h>

namespace iNet {

struct NetAddr
{
    uint8_t ip[16];
    uint16_t port;
};

bool addr_equal(const NetAddr& a, const NetAddr& b);
bool addr_is_v4(const NetAddr& a);
// "1.2.3.4:5", "1.2.3.4" (default port), "[2001:db8::1]:5". Numeric only;
// rejects anything else, including names, and port 0.
bool addr_parse(const char* text, uint16_t default_port, NetAddr* out);
void addr_format(const NetAddr& a, char* out, size_t cap);

bool net_init();
void net_shutdown();

enum class BindScope
{
    Loopback, // 127.0.0.1 only: tests, never visible to the network or the firewall
    Any,      // every interface, dual-stack IPv6 + IPv4: hosting
};

struct UdpSocket
{
    intptr_t fd = -1;
    bool v6 = false;
};

// loopback_host picks 127.0.0.N for Loopback (tests use several "machines").
bool udp_open(UdpSocket* s, BindScope scope, uint16_t port, uint8_t loopback_host = 1);
bool udp_local_port(const UdpSocket& s, uint16_t* port);
// Waits up to timeout_ms for the socket to become readable. 0 polls.
bool udp_wait(const UdpSocket& s, int timeout_ms);
// > 0: a datagram of that many bytes. 0: nothing waiting. < 0: one datagram
// was discarded (too large for cap, or an ICMP error surfaced) -- call again.
int udp_recv(const UdpSocket& s, uint8_t* buf, size_t cap, NetAddr* from);
bool udp_send(const UdpSocket& s, const NetAddr& to, const uint8_t* buf, size_t len);
void udp_close(UdpSocket* s);

// This PC's address on the network that holds the default route (the one a
// router's port forward points at), for the Co-op tab's "forward UDP N to
// 192.168.1.23". Found by connect()ing a UDP socket to a documentation address
// and asking which local address it picked: a UDP connect sends nothing, and
// nothing is resolved. False when the PC has no IPv4 route.
bool local_address(NetAddr* out);

// --- the home network (iNetDisc.h) ------------------------------------------

enum : size_t
{
    kMaxIfs = 8
};

struct IfV4
{
    uint8_t ip[4];
    uint8_t prefix; // 8..30
};

// This device's interfaces that may hold a home network, through
// iNetDisc.h's if_filter: Windows' adapters (iphlpapi), getifaddrs, or the
// Switch's one current network (nifm). Local queries only; nothing is sent.
size_t if_list(IfV4* out, size_t cap);

// An IPv4-only socket bound to `ip` (4 bytes) and `port` (0: any), with
// SO_BROADCAST when `broadcast`. `exclusive`: no other program may bind the
// same port while this one is open (Windows; elsewhere the default).
bool udp_open_v4(UdpSocket* s, const uint8_t ip[4], uint16_t port, bool broadcast, bool exclusive);

// Waits up to timeout_ms until one of the open sockets among `s` (fd -1 is
// skipped) is readable. 0 polls.
bool udp_wait_any(const UdpSocket* const* s, size_t n, int timeout_ms);

#if defined(__SWITCH__)
// The applet hook's flags (switch/iSwitch.cpp, on the game thread). Nothing
// is sent from the hook: the co-op I/O thread looks at its sockets at its
// next wait, every socket after a sleep (Wi-Fi drops while the console
// sleeps), and only broken ones after the HOME menu or an applet.
void net_note_away(bool away);
void net_note_wake();

// Why there is no home network, for the Co-op tab: 0 there is one, 1 Wi-Fi
// is switched off (flight mode), 2 not connected. Local queries only.
int net_offline_reason();
#endif

} // namespace iNet

#endif
