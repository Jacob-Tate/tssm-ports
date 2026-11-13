// PC-only, co-op: the BSD/POSIX backend of iNetSock.h: Linux, Android (needs the INTERNET
// permission in the manifest), macOS, and the Switch (libnx's BSD sockets,
// which the Switch build brings up at boot: IPv4 only there). The interface
// list is getifaddrs, or nifm on the Switch.
#if !defined(_WIN32)
#include "iNetSock.h"

#include "iNetDisc.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(__SWITCH__)
#include "switch/iSwitch.h"

#include <atomic>
#include <stdio.h>
#include <switch.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#endif

namespace iNet {

#if defined(__SWITCH__)
// The sockets come up at boot (switch/iSwitch.cpp); co-op never starts them
// again.
bool net_init() { return iSwitchSocketsUp(); }
#else
bool net_init() { return true; }
#endif
void net_shutdown() {}

static bool from_sockaddr(const sockaddr_storage* ss, NetAddr* a)
{
    memset(a, 0, sizeof *a);
    if (ss->ss_family == AF_INET)
    {
        const sockaddr_in* in = (const sockaddr_in*)ss;
        a->ip[10] = a->ip[11] = 0xff;
        memcpy(a->ip + 12, &in->sin_addr, 4);
        a->port = ntohs(in->sin_port);
        return true;
    }
    if (ss->ss_family == AF_INET6)
    {
        const sockaddr_in6* in6 = (const sockaddr_in6*)ss;
        memcpy(a->ip, &in6->sin6_addr, 16);
        a->port = ntohs(in6->sin6_port);
        return true;
    }
    return false;
}

// An IPv4 UDP socket bound to ip:port, non-blocking; -1 when it cannot be.
static int open_v4(const uint8_t ip[4], uint16_t port, bool broadcast)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    int on = 1;
    if (broadcast && setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof on) != 0)
    {
        close(fd);
        return -1;
    }
    sockaddr_in in;
    memset(&in, 0, sizeof in);
    in.sin_family = AF_INET;
    in.sin_port = htons(port);
    memcpy(&in.sin_addr, ip, 4);
    if (bind(fd, (sockaddr*)&in, sizeof in) != 0)
    {
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

#if defined(__SWITCH__)
// On the Switch a UdpSocket's fd is a slot in this table, not the descriptor:
// a descriptor can die while the console sleeps (Wi-Fi goes down with it),
// and the slot's socket is then made again in place, bound as before, while
// the co-op code above keeps its handle. Only the co-op I/O thread opens,
// uses and closes sockets, so the table needs no lock; the applet hook only
// moves the counters below.
namespace {

struct Slot
{
    bool used;
    int fd; // -1 while broken
    uint8_t ip[4];
    uint16_t port; // as asked (0: any)
    uint16_t bound; // the one it got, asked for first when made again
    bool broadcast;
    uint64_t retryAt; // a broken slot is tried again from then
    bool said; // a failure to make it again was logged
    bool broke; // a dead descriptor was logged
};

const int kSlots = 16; // a session, the responder and kMaxIfs finder sockets
Slot sSlots[kSlots];

std::atomic<bool> sAway{ false };
std::atomic<unsigned> sBacks{ 0 }; // returns from HOME, sleep or an applet
std::atomic<unsigned> sWakes{ 0 }; // wakes from sleep
unsigned sSeenBacks, sSeenWakes; // the I/O thread's

uint64_t NowNs() { return armTicksToNs(armGetSystemTick()); }

Slot* SlotOf(const UdpSocket& s)
{
    return (s.fd >= 0 && s.fd < kSlots && sSlots[s.fd].used) ? &sSlots[s.fd] : NULL;
}

uint16_t BoundPort(int fd)
{
    sockaddr_storage ss;
    socklen_t len = sizeof ss;
    NetAddr a;
    if (getsockname(fd, (sockaddr*)&ss, &len) != 0 || !from_sockaddr(&ss, &a))
        return 0;
    return a.port;
}

// The finder binds to the console's address so its broadcasts leave by that
// network. The console has one network at a time, so where that bind is
// refused (or the address changed while it slept), any address is the same
// network. Never for loopback, which must stay loopback.
int OpenSlotSocket(const Slot& s, uint16_t port)
{
    int fd = open_v4(s.ip, port, s.broadcast);
    const bool specific = (s.ip[0] | s.ip[1] | s.ip[2] | s.ip[3]) != 0 && s.ip[0] != 127;
    if (fd < 0 && specific)
    {
        const uint8_t any[4] = { 0, 0, 0, 0 };
        fd = open_v4(any, port, s.broadcast);
    }
    return fd;
}

bool Adopt(UdpSocket* out, int fd, const uint8_t ip[4], uint16_t port, bool broadcast)
{
    for (int i = 0; i < kSlots; i++)
    {
        Slot& s = sSlots[i];
        if (s.used)
            continue;
        memset(&s, 0, sizeof s);
        s.used = true;
        s.fd = fd;
        memcpy(s.ip, ip, 4);
        s.port = port;
        s.bound = BoundPort(fd);
        s.broadcast = broadcast;
        out->fd = i;
        out->v6 = false;
        return true;
    }
    close(fd);
    return false;
}

// A descriptor that no longer works, as opposed to one datagram's error.
bool Dead(int err)
{
    return err == EBADF || err == ENOTSOCK || err == ENETDOWN || err == ENETUNREACH || err == EPIPE;
}

// Made again a second later, not at once: while Wi-Fi is down every send
// fails, and the socket must not be remade at every tick.
void Break(Slot* s, int err)
{
    if (s->fd >= 0)
        close(s->fd);
    s->fd = -1;
    s->retryAt = NowNs() + 1000000000ull;
    if (!s->broke)
    {
        s->broke = true;
        printf("tssm: coop: switch: a co-op socket (port %u) stopped working (%d); made again each "
               "second until it works\n",
               (unsigned)(s->bound != 0 ? s->bound : s->port), err);
        fflush(stdout);
    }
}

// The old port first, so a host keeps its port and a guest's partner still
// knows it; a guest takes any port if that one went.
bool Remake(Slot* s, uint64_t now)
{
    if (s->fd >= 0)
        close(s->fd);
    int fd = OpenSlotSocket(*s, s->bound != 0 ? s->bound : s->port);
    if (fd < 0 && s->port == 0 && s->bound != 0)
        fd = OpenSlotSocket(*s, 0);
    s->fd = fd;
    if (fd < 0)
    {
        s->retryAt = now + 1000000000ull;
        if (!s->said)
        {
            s->said = true;
            printf("tssm: coop: switch: a co-op socket (port %u) could not be made again (%d); trying "
                   "each second\n",
                   (unsigned)(s->bound != 0 ? s->bound : s->port), errno);
            fflush(stdout);
        }
        return false;
    }
    s->bound = BoundPort(fd);
    s->said = false;
    return true;
}

// Before each wait: after a sleep every socket is made again; after the HOME
// menu or an applet, those that report an error; a broken one each second.
void Check()
{
    if (sAway.load(std::memory_order_relaxed))
        return; // Wi-Fi may be on its way down: wait until the game is back
    const unsigned wakes = sWakes.load(std::memory_order_relaxed);
    const unsigned backs = sBacks.load(std::memory_order_relaxed);
    const bool woke = wakes != sSeenWakes;
    const bool back = backs != sSeenBacks;
    sSeenWakes = wakes;
    sSeenBacks = backs;
    const uint64_t now = NowNs();
    int remade = 0, failed = 0;
    for (int i = 0; i < kSlots; i++)
    {
        Slot& s = sSlots[i];
        if (!s.used)
            continue;
        bool redo = woke || (s.fd < 0 && now >= s.retryAt);
        if (!redo && back && s.fd >= 0)
        {
            int err = 0;
            socklen_t len = sizeof err;
            redo = getsockopt(s.fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || Dead(err);
        }
        if (redo)
            (Remake(&s, now) ? remade : failed)++;
    }
    if ((woke || back) && remade + failed > 0)
    {
        printf("tssm: coop: switch: %s: %d co-op socket%s made again%s\n",
               woke ? "woke from sleep" : "back in the game", remade, remade == 1 ? "" : "s",
               failed > 0 ? ", some not yet" : "");
        fflush(stdout);
    }
}

} // namespace

void net_note_away(bool away)
{
    sAway.store(away, std::memory_order_relaxed);
    if (!away)
        sBacks.fetch_add(1, std::memory_order_relaxed);
}

void net_note_wake() { sWakes.fetch_add(1, std::memory_order_relaxed); }

int net_offline_reason()
{
    if (R_FAILED(nifmInitialize(NifmServiceType_User)))
        return 0; // unknown: say nothing
    u32 addr = 0, mask = 0, gateway = 0, dns1 = 0, dns2 = 0;
    int why = 0;
    if (R_FAILED(nifmGetCurrentIpConfigInfo(&addr, &mask, &gateway, &dns1, &dns2)) || addr == 0)
    {
        bool wireless = true;
        why = (R_SUCCEEDED(nifmIsWirelessCommunicationEnabled(&wireless)) && !wireless) ? 1 : 2;
    }
    nifmExit();
    return why;
}

static int sock_fd(const UdpSocket& s)
{
    const Slot* slot = SlotOf(s);
    return slot != NULL ? slot->fd : -1;
}

static void sock_failed(const UdpSocket& s, int err)
{
    Slot* slot = SlotOf(s);
    if (slot != NULL && Dead(err))
        Break(slot, err);
}

static void sock_check() { Check(); }

bool udp_open(UdpSocket* out, BindScope scope, uint16_t port, uint8_t loopback_host)
{
    const uint8_t lo[4] = { 127, 0, 0, loopback_host };
    const uint8_t any[4] = { 0, 0, 0, 0 };
    const uint8_t* ip = scope == BindScope::Loopback ? lo : any;
    int fd = open_v4(ip, port, false);
    return fd >= 0 && Adopt(out, fd, ip, port, false);
}

bool udp_local_port(const UdpSocket& s, uint16_t* port)
{
    const Slot* slot = SlotOf(s);
    if (slot == NULL || slot->bound == 0)
        return false;
    *port = slot->bound;
    return true;
}

void udp_close(UdpSocket* s)
{
    Slot* slot = SlotOf(*s);
    if (slot != NULL)
    {
        if (slot->fd >= 0)
            close(slot->fd);
        memset(slot, 0, sizeof *slot);
    }
    s->fd = -1;
}

bool local_address(NetAddr* out)
{
    // nifm's current network; nothing is sent.
    if (R_FAILED(nifmInitialize(NifmServiceType_User)))
        return false;
    u32 addr = 0, mask = 0, gateway = 0, dns1 = 0, dns2 = 0;
    const bool ok = R_SUCCEEDED(nifmGetCurrentIpConfigInfo(&addr, &mask, &gateway, &dns1, &dns2)) && addr != 0;
    nifmExit();
    if (!ok)
        return false;
    memset(out, 0, sizeof *out);
    out->ip[10] = out->ip[11] = 0xff;
    memcpy(out->ip + 12, &addr, 4);
    return true;
}

bool udp_open_v4(UdpSocket* out, const uint8_t ip[4], uint16_t port, bool broadcast, bool exclusive)
{
    (void)exclusive; // without SO_REUSEADDR, which nothing here sets, no other socket can share the port
    Slot probe;
    memset(&probe, 0, sizeof probe);
    memcpy(probe.ip, ip, 4);
    probe.broadcast = broadcast;
    int fd = OpenSlotSocket(probe, port);
    return fd >= 0 && Adopt(out, fd, ip, port, broadcast);
}
#else
static int sock_fd(const UdpSocket& s) { return (int)s.fd; }
static void sock_failed(const UdpSocket&, int) {}
static void sock_check() {}

bool udp_open(UdpSocket* out, BindScope scope, uint16_t port, uint8_t loopback_host)
{
    UdpSocket s;
    int fd = -1;
    if (scope == BindScope::Any)
    {
        fd = socket(AF_INET6, SOCK_DGRAM, 0);
        if (fd >= 0)
        {
            int off = 0;
            setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
            sockaddr_in6 in6;
            memset(&in6, 0, sizeof in6);
            in6.sin6_family = AF_INET6;
            in6.sin6_port = htons(port);
            in6.sin6_addr = in6addr_any;
            if (bind(fd, (sockaddr*)&in6, sizeof in6) != 0) { close(fd); fd = -1; }
            else s.v6 = true;
        }
    }
    if (fd < 0)
    {
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) return false;
        sockaddr_in in;
        memset(&in, 0, sizeof in);
        in.sin_family = AF_INET;
        in.sin_port = htons(port);
        in.sin_addr.s_addr = htonl(scope == BindScope::Loopback ? (INADDR_LOOPBACK & 0xFFFFFF00u) | loopback_host : INADDR_ANY);
        if (bind(fd, (sockaddr*)&in, sizeof in) != 0) { close(fd); return false; }
        s.v6 = false;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    s.fd = fd;
    *out = s;
    return true;
}

bool udp_local_port(const UdpSocket& s, uint16_t* port)
{
    sockaddr_storage ss;
    socklen_t len = sizeof ss;
    if (getsockname((int)s.fd, (sockaddr*)&ss, &len) != 0) return false;
    NetAddr a;
    if (!from_sockaddr(&ss, &a)) return false;
    *port = a.port;
    return true;
}

void udp_close(UdpSocket* s)
{
    if (s->fd != -1) close((int)s->fd);
    s->fd = -1;
}

bool local_address(NetAddr* out)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    sockaddr_in to;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(9);
    to.sin_addr.s_addr = htonl(0xCB007101u); // 203.0.113.1 (TEST-NET-3): never contacted
    bool ok = false;
    if (connect(fd, (sockaddr*)&to, sizeof to) == 0)
    {
        sockaddr_storage ss;
        socklen_t len = sizeof ss;
        if (getsockname(fd, (sockaddr*)&ss, &len) == 0 && from_sockaddr(&ss, out))
        {
            out->port = 0;
            ok = !(out->ip[12] == 0 && out->ip[13] == 0 && out->ip[14] == 0 && out->ip[15] == 0);
        }
    }
    close(fd);
    return ok;
}

bool udp_open_v4(UdpSocket* out, const uint8_t ip[4], uint16_t port, bool broadcast, bool exclusive)
{
    (void)
        exclusive; // without SO_REUSEADDR, which nothing here sets, no other socket can share the port
    int fd = open_v4(ip, port, broadcast);
    if (fd < 0)
        return false;
    out->fd = fd;
    out->v6 = false;
    return true;
}
#endif

bool udp_wait(const UdpSocket& s, int timeout_ms)
{
    sock_check();
    pollfd p = { sock_fd(s), POLLIN, 0 };
#if defined(__SWITCH__)
    // A blocking BSD call holds one of libnx's few service sessions for as
    // long as it waits, and the log's sends need one too: poll without
    // waiting, and sleep between polls.
    for (int left = timeout_ms;; left -= 2)
    {
        p.revents = 0;
        if (p.fd >= 0 && poll(&p, 1, 0) > 0)
            return true;
        if (left <= 0)
            return false;
        svcSleepThread((int64_t)(left < 2 ? left : 2) * 1000000);
    }
#else
    return poll(&p, 1, timeout_ms) > 0;
#endif
}

int udp_recv(const UdpSocket& s, uint8_t* buf, size_t cap, NetAddr* from)
{
    const int fd = sock_fd(s);
    if (fd < 0) return 0; // broken (Switch): nothing until it is made again
    sockaddr_storage ss;
    socklen_t len = sizeof ss;
    ssize_t n = recvfrom(fd, buf, cap, MSG_TRUNC, (sockaddr*)&ss, &len);
    if (n < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        sock_failed(s, errno);
        return sock_fd(s) < 0 ? 0 : -1;
    }
    if (n == 0 || (size_t)n >= cap) return -1; // MSG_TRUNC reports the real size
    if (!from_sockaddr(&ss, from)) return -1;
    return (int)n;
}

bool udp_send(const UdpSocket& s, const NetAddr& to, const uint8_t* buf, size_t len)
{
    const int fd = sock_fd(s);
    if (fd < 0) return false;
    sockaddr_storage ss;
    memset(&ss, 0, sizeof ss);
    socklen_t sl;
    if (!s.v6)
    {
        if (!addr_is_v4(to)) return false;
        sockaddr_in* in = (sockaddr_in*)&ss;
        in->sin_family = AF_INET;
        in->sin_port = htons(to.port);
        memcpy(&in->sin_addr, to.ip + 12, 4);
        sl = sizeof *in;
    }
    else
    {
        sockaddr_in6* in6 = (sockaddr_in6*)&ss;
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(to.port);
        memcpy(&in6->sin6_addr, to.ip, 16);
        sl = sizeof *in6;
    }
    if (sendto(fd, buf, len, 0, (sockaddr*)&ss, sl) == (ssize_t)len) return true;
    sock_failed(s, errno);
    return false;
}

bool udp_wait_any(const UdpSocket* const* s, size_t n, int timeout_ms)
{
    sock_check();
    pollfd p[kMaxIfs + 4];
    nfds_t k = 0;
    for (size_t i = 0; i < n && k < sizeof p / sizeof p[0]; i++)
    {
        if (s[i] == NULL || s[i]->fd == -1 || sock_fd(*s[i]) < 0)
            continue;
        p[k].fd = sock_fd(*s[i]);
        p[k].events = POLLIN;
        p[k].revents = 0;
        k++;
    }
#if defined(__SWITCH__)
    // As udp_wait: no blocking call, so no BSD session is held while waiting.
    for (int left = timeout_ms;; left -= 2)
    {
        if (k > 0 && poll(p, k, 0) > 0)
            return true;
        if (left <= 0)
            return false;
        svcSleepThread((int64_t)(left < 2 ? left : 2) * 1000000);
    }
#else
    return poll(k > 0 ? p : NULL, k, timeout_ms) > 0;
#endif
}

static uint8_t prefix_of(uint32_t mask_be)
{
    // A contiguous mask only; anything else is not a subnet this can use.
    uint32_t m = ntohl(mask_be);
    uint8_t n = 0;
    while (n < 32 && (m & (0x80000000u >> n)) != 0)
        n++;
    return (n < 32 && (m << n) != 0) ? 0 : n;
}

#if defined(__SWITCH__)
size_t if_list(IfV4* out, size_t cap)
{
    // The console has one network at a time: nifm's current one.
    if (R_FAILED(nifmInitialize(NifmServiceType_User)))
        return 0;
    u32 addr = 0, mask = 0, gateway = 0, dns1 = 0, dns2 = 0;
    IfRecord c;
    memset(&c, 0, sizeof c);
    if (R_SUCCEEDED(nifmGetCurrentIpConfigInfo(&addr, &mask, &gateway, &dns1, &dns2)) && addr != 0)
    {
        c.up = c.hardware = c.broadcast = true;
        c.count = 1;
        memcpy(c.addr[0].ip, &addr, 4);
        c.addr[0].prefix = prefix_of(mask);
        c.addr[0].preferred = true;
    }
    nifmExit();
    return if_filter(&c, 1, out, cap);
}
#else
size_t if_list(IfV4* out, size_t cap)
{
    ifaddrs* list = NULL;
    if (getifaddrs(&list) != 0)
        return 0;
    // One record per interface name, so its 169.254 address is weighed
    // against its others.
    IfRecord recs[32];
    const char* names[32];
    size_t n = 0;
    for (ifaddrs* a = list; a != NULL; a = a->ifa_next)
    {
        if (a->ifa_addr == NULL || a->ifa_addr->sa_family != AF_INET || a->ifa_netmask == NULL ||
            a->ifa_name == NULL)
            continue;
        size_t r = 0;
        while (r < n && strcmp(names[r], a->ifa_name) != 0)
            r++;
        if (r == n)
        {
            if (n == 32)
                continue;
            memset(&recs[n], 0, sizeof recs[n]);
            names[n] = a->ifa_name;
            unsigned f = a->ifa_flags;
            recs[n].up = (f & IFF_UP) != 0 && (f & IFF_RUNNING) != 0;
            recs[n].hardware = true;
            recs[n].loopback = (f & IFF_LOOPBACK) != 0;
            recs[n].point_to_point = (f & IFF_POINTOPOINT) != 0; // tun VPNs
            recs[n].broadcast = (f & IFF_BROADCAST) != 0;
            n++;
        }
        IfRecord& c = recs[r];
        if (c.count == 4)
            continue;
        const sockaddr_in* in = (const sockaddr_in*)a->ifa_addr;
        const sockaddr_in* mk = (const sockaddr_in*)a->ifa_netmask;
        memcpy(c.addr[c.count].ip, &in->sin_addr, 4);
        c.addr[c.count].prefix = prefix_of(mk->sin_addr.s_addr);
        c.addr[c.count].preferred = true;
        c.count++;
    }
    size_t k = if_filter(recs, n, out, cap);
    freeifaddrs(list);
    return k;
}
#endif

} // namespace iNet
#endif
