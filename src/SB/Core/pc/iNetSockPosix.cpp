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
#include <switch.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#endif

namespace iNet {

bool net_init() { return true; }
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

bool udp_open(UdpSocket* out, BindScope scope, uint16_t port, uint8_t loopback_host)
{
    UdpSocket s;
    int fd = -1;
#if !defined(__SWITCH__) // Horizon's stack is IPv4
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
#endif
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

bool udp_wait(const UdpSocket& s, int timeout_ms)
{
    pollfd p = { (int)s.fd, POLLIN, 0 };
#if defined(__SWITCH__)
    // A blocking BSD call holds one of libnx's few service sessions for as
    // long as it waits, and the log's sends need one too: poll without
    // waiting, and sleep between polls.
    for (int left = timeout_ms;; left -= 2)
    {
        p.revents = 0;
        if (poll(&p, 1, 0) > 0)
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
    sockaddr_storage ss;
    socklen_t len = sizeof ss;
    ssize_t n = recvfrom((int)s.fd, buf, cap, MSG_TRUNC, (sockaddr*)&ss, &len);
    if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    if (n == 0 || (size_t)n >= cap) return -1; // MSG_TRUNC reports the real size
    if (!from_sockaddr(&ss, from)) return -1;
    return (int)n;
}

bool udp_send(const UdpSocket& s, const NetAddr& to, const uint8_t* buf, size_t len)
{
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
    return sendto((int)s.fd, buf, len, 0, (sockaddr*)&ss, sl) == (ssize_t)len;
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
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return false;
    int on = 1;
    if (broadcast && setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof on) != 0)
    {
        close(fd);
        return false;
    }
    sockaddr_in in;
    memset(&in, 0, sizeof in);
    in.sin_family = AF_INET;
    in.sin_port = htons(port);
    memcpy(&in.sin_addr, ip, 4);
    if (bind(fd, (sockaddr*)&in, sizeof in) != 0)
    {
        close(fd);
        return false;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    out->fd = fd;
    out->v6 = false;
    return true;
}

bool udp_wait_any(const UdpSocket* const* s, size_t n, int timeout_ms)
{
    pollfd p[kMaxIfs + 4];
    nfds_t k = 0;
    for (size_t i = 0; i < n && k < sizeof p / sizeof p[0]; i++)
    {
        if (s[i] == NULL || s[i]->fd == -1)
            continue;
        p[k].fd = (int)s[i]->fd;
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
