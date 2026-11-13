// PC-only, co-op: the Winsock2 backend of iNetSock.h. Links ws2_32 and, for
// the interface list, iphlpapi (both part of Windows, nothing to download).
#if defined(_WIN32)
#include "iNetSock.h"

#include "iNetDisc.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <iphlpapi.h>
#include <stdlib.h>
#include <string.h>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

namespace iNet {

static bool g_wsa;

bool net_init()
{
    if (g_wsa) return true;
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) return false;
    g_wsa = true;
    return true;
}

void net_shutdown()
{
    if (g_wsa) WSACleanup();
    g_wsa = false;
}

static void to_sockaddr(const UdpSocket& s, const NetAddr& a, sockaddr_storage* ss, int* len)
{
    memset(ss, 0, sizeof *ss);
    if (!s.v6)
    {
        sockaddr_in* in = (sockaddr_in*)ss;
        in->sin_family = AF_INET;
        in->sin_port = htons(a.port);
        memcpy(&in->sin_addr, a.ip + 12, 4);
        *len = sizeof *in;
    }
    else
    {
        sockaddr_in6* in6 = (sockaddr_in6*)ss;
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(a.port);
        memcpy(&in6->sin6_addr, a.ip, 16);
        *len = sizeof *in6;
    }
}

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
    SOCKET fd = INVALID_SOCKET;
    if (scope == BindScope::Any)
    {
        fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
        if (fd != INVALID_SOCKET)
        {
            DWORD off = 0; // dual-stack: IPv4 peers arrive as ::ffff:a.b.c.d
            setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&off, sizeof off);
            sockaddr_in6 in6;
            memset(&in6, 0, sizeof in6);
            in6.sin6_family = AF_INET6;
            in6.sin6_port = htons(port);
            in6.sin6_addr = in6addr_any;
            if (bind(fd, (sockaddr*)&in6, sizeof in6) != 0) { closesocket(fd); fd = INVALID_SOCKET; }
            else s.v6 = true;
        }
    }
    if (fd == INVALID_SOCKET)
    {
        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd == INVALID_SOCKET) return false;
        sockaddr_in in;
        memset(&in, 0, sizeof in);
        in.sin_family = AF_INET;
        in.sin_port = htons(port);
        in.sin_addr.s_addr = htonl(scope == BindScope::Loopback ? (INADDR_LOOPBACK & 0xFFFFFF00u) | loopback_host : INADDR_ANY);
        if (bind(fd, (sockaddr*)&in, sizeof in) != 0) { closesocket(fd); return false; }
        s.v6 = false;
    }
    u_long nb = 1;
    ioctlsocket(fd, FIONBIO, &nb);
    // Without this an ICMP port-unreachable from an earlier send makes the
    // next recvfrom fail with WSAECONNRESET: one stray packet to a closed
    // port would look like the socket breaking.
    BOOL no = FALSE;
    DWORD ret = 0;
    WSAIoctl(fd, SIO_UDP_CONNRESET, &no, sizeof no, NULL, 0, &ret, NULL, NULL);
    s.fd = (intptr_t)fd;
    *out = s;
    return true;
}

bool udp_local_port(const UdpSocket& s, uint16_t* port)
{
    sockaddr_storage ss;
    int len = sizeof ss;
    if (getsockname((SOCKET)s.fd, (sockaddr*)&ss, &len) != 0) return false;
    NetAddr a;
    if (!from_sockaddr(&ss, &a)) return false;
    *port = a.port;
    return true;
}

bool udp_wait(const UdpSocket& s, int timeout_ms)
{
    WSAPOLLFD p;
    p.fd = (SOCKET)s.fd;
    p.events = POLLRDNORM;
    p.revents = 0;
    return WSAPoll(&p, 1, timeout_ms) > 0;
}

int udp_recv(const UdpSocket& s, uint8_t* buf, size_t cap, NetAddr* from)
{
    sockaddr_storage ss;
    int len = sizeof ss;
    int n = recvfrom((SOCKET)s.fd, (char*)buf, (int)cap, 0, (sockaddr*)&ss, &len);
    if (n == SOCKET_ERROR)
    {
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) return 0;
        return -1; // WSAEMSGSIZE (larger than cap: dropped), WSAECONNRESET, ...
    }
    if (n <= 0 || (size_t)n >= cap) return -1; // empty, or possibly truncated
    if (!from_sockaddr(&ss, from)) return -1;
    return n;
}

bool udp_send(const UdpSocket& s, const NetAddr& to, const uint8_t* buf, size_t len)
{
    if (!s.v6 && !addr_is_v4(to)) return false;
    sockaddr_storage ss;
    int sl;
    to_sockaddr(s, to, &ss, &sl);
    return sendto((SOCKET)s.fd, (const char*)buf, (int)len, 0, (sockaddr*)&ss, sl) == (int)len;
}

void udp_close(UdpSocket* s)
{
    if (s->fd != -1) closesocket((SOCKET)s->fd);
    s->fd = -1;
}

bool local_address(NetAddr* out)
{
    SOCKET fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == INVALID_SOCKET) return false;
    sockaddr_in to;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons(9);
    to.sin_addr.s_addr = htonl(0xCB007101u); // 203.0.113.1 (TEST-NET-3): never contacted
    bool ok = false;
    if (connect(fd, (sockaddr*)&to, sizeof to) == 0)
    {
        sockaddr_storage ss;
        int len = sizeof ss;
        if (getsockname(fd, (sockaddr*)&ss, &len) == 0 && from_sockaddr(&ss, out))
        {
            out->port = 0;
            ok = !(out->ip[12] == 0 && out->ip[13] == 0 && out->ip[14] == 0 && out->ip[15] == 0);
        }
    }
    closesocket(fd);
    return ok;
}

bool udp_open_v4(UdpSocket* out, const uint8_t ip[4], uint16_t port, bool broadcast, bool exclusive)
{
    SOCKET fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == INVALID_SOCKET)
        return false;
    BOOL on = TRUE;
    // Without it a program that sets SO_REUSEADDR could bind the same port
    // and take the questions meant for this one.
    if (exclusive &&
        setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&on, sizeof on) != 0)
    {
        closesocket(fd);
        return false;
    }
    if (broadcast && setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof on) != 0)
    {
        closesocket(fd);
        return false;
    }
    sockaddr_in in;
    memset(&in, 0, sizeof in);
    in.sin_family = AF_INET;
    in.sin_port = htons(port);
    memcpy(&in.sin_addr, ip, 4);
    if (bind(fd, (sockaddr*)&in, sizeof in) != 0)
    {
        closesocket(fd);
        return false;
    }
    u_long nb = 1;
    ioctlsocket(fd, FIONBIO, &nb);
    BOOL no = FALSE;
    DWORD ret = 0;
    WSAIoctl(fd, SIO_UDP_CONNRESET, &no, sizeof no, NULL, 0, &ret, NULL, NULL);
    out->fd = (intptr_t)fd;
    out->v6 = false;
    return true;
}

bool udp_wait_any(const UdpSocket* const* s, size_t n, int timeout_ms)
{
    WSAPOLLFD p[kMaxIfs + 4];
    ULONG k = 0;
    for (size_t i = 0; i < n && k < sizeof p / sizeof p[0]; i++)
    {
        if (s[i] == NULL || s[i]->fd == -1)
            continue;
        p[k].fd = (SOCKET)s[i]->fd;
        p[k].events = POLLRDNORM;
        p[k].revents = 0;
        k++;
    }
    if (k == 0)
    {
        if (timeout_ms > 0)
            Sleep((DWORD)timeout_ms);
        return false;
    }
    return WSAPoll(p, k, timeout_ms) > 0;
}

size_t if_list(IfV4* out, size_t cap)
{
    // Adapters with their IPv4 addresses; the buffer is grown once or twice
    // if more adapters appeared between the size query and the read.
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER |
                  GAA_FLAG_SKIP_FRIENDLY_NAME;
    ULONG size = 32 * 1024;
    IP_ADAPTER_ADDRESSES* list = NULL;
    ULONG r = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && r == ERROR_BUFFER_OVERFLOW; tries++)
    {
        free(list);
        list = (IP_ADAPTER_ADDRESSES*)malloc(size);
        if (list == NULL)
            return 0;
        r = GetAdaptersAddresses(AF_INET, flags, NULL, list, &size);
    }
    if (r != NO_ERROR)
    {
        free(list);
        return 0;
    }
    IfRecord recs[32];
    size_t n = 0;
    for (IP_ADAPTER_ADDRESSES* a = list; a != NULL && n < 32; a = a->Next)
    {
        IfRecord& c = recs[n++];
        memset(&c, 0, sizeof c);
        c.up = a->OperStatus == IfOperStatusUp;
        c.loopback = a->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
        // PPP, tunnels and the virtual adapters VPNs use (type 53).
        c.point_to_point = a->IfType == IF_TYPE_PPP || a->IfType == IF_TYPE_TUNNEL ||
                           a->IfType == IF_TYPE_PROP_VIRTUAL;
        c.broadcast = !c.loopback && !c.point_to_point;
        MIB_IF_ROW2 row;
        memset(&row, 0, sizeof row);
        row.InterfaceLuid = a->Luid;
        c.hardware =
            GetIfEntry2(&row) == NO_ERROR && row.InterfaceAndOperStatusFlags.HardwareInterface;
        for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u != NULL && c.count < 4;
             u = u->Next)
        {
            if (u->Address.lpSockaddr == NULL || u->Address.lpSockaddr->sa_family != AF_INET)
                continue;
            const sockaddr_in* in = (const sockaddr_in*)u->Address.lpSockaddr;
            memcpy(c.addr[c.count].ip, &in->sin_addr, 4);
            c.addr[c.count].prefix = u->OnLinkPrefixLength;
            c.addr[c.count].preferred = u->DadState == IpDadStatePreferred;
            c.count++;
        }
    }
    free(list);
    return if_filter(recs, n, out, cap);
}

} // namespace iNet
#endif
