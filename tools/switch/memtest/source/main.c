// tssm-memtest: can a homebrew process get read/write memory below 4 GB?
//
// The port keeps two pools below the 4 GB line because the game stores
// pointers in 32-bit fields: the game arena (iMemMgr.cpp, 128 MB) and
// RenderWare's heap (iLowHeap, 256 MB by default). On PC and Android that is
// VirtualAlloc/mmap with an address hint. Horizon has neither, and under
// Atmosphere's default 39-bit address-space layout the kernel puts the heap,
// alias and stack regions wherever it likes.
//
// This program tries the routes from the Switch research report, section 3.7,
// and writes what happened to the screen, to stdout (nxlink -s) and to
// sdmc:/switch/tssm-memtest.txt:
//
//   A. Code-memory alias (hbloader's own .bss sequence, at an address we pick):
//      memalign a block from the heap, find a free range below 4 GB with
//      svcQueryMemory (outside the heap/alias/stack regions), reserve it with
//      virtmemAddReservation, svcMapProcessCodeMemory it there, then
//      svcSetProcessMemoryPermission(RW). Write and read every word, unmap.
//   B. svcMapMemory into the stack region below 4 GB. Only possible when the
//      stack region reaches below 4 GB, which is the case in the 36-bit layout.
//   C. svcMapMemory below 4 GB outside the stack region: expected to fail; it
//      shows the kernel's rule.
//
// Press + to exit when it is done.

#include <inttypes.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#define KB ((u64)1024)
#define MB ((u64)1024 * 1024)
#define FOUR_GB ((u64)0x100000000)
#define PAGE ((u64)0x1000)
#define BIG_ALIGN ((u64)0x200000) // place mappings on 2 MB boundaries
#define LOG_PATH "sdmc:/switch/tssm-memtest.txt"
#define REPORT_PORT 28772 // the PC connects here to pull the report (no inbound firewall rule needed on the PC)

// newlib's heap bounds, set by libnx from the heap hbloader hands over.
extern char* fake_heap_start;
extern char* fake_heap_end;

// ---------------------------------------------------------------------------
// Output: console (stderr), nxlink (stdout) and the log file.

static FILE* g_file;
static bool g_nxlink;
static bool g_console_ok = true;
static char g_report[128 * 1024]; // everything printed, served by the report server
static size_t g_report_len;

enum { TO_ALL = 0, TO_LOG = 1 }; // TO_LOG: file and nxlink only (long tables)

static void emit(int where, const char* fmt, va_list ap)
{
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    if (where == TO_ALL && g_console_ok)
    {
        fputs(buf, stderr);
        fflush(stderr);
        consoleUpdate(NULL);
    }
    if (g_nxlink)
    {
        fputs(buf, stdout);
        fflush(stdout);
    }
    if (g_file)
    {
        fputs(buf, g_file);
        fflush(g_file); // so the file survives if a later step crashes
    }
    size_t n = strlen(buf);
    if (g_report_len + n < sizeof(g_report))
    {
        memcpy(g_report + g_report_len, buf, n);
        g_report_len += n;
    }
}

static void out(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit(TO_ALL, fmt, ap);
    va_end(ap);
}

static void logonly(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit(TO_LOG, fmt, ap);
    va_end(ap);
}

static const char* rcstr(Result rc)
{
    static char s[4][48];
    static int i;
    char* p = s[i++ & 3];
    if (R_SUCCEEDED(rc))
        snprintf(p, 48, "ok");
    else
        snprintf(p, 48, "0x%X (%04u-%04u)", rc, 2000 + R_MODULE(rc), R_DESCRIPTION(rc));
    return p;
}

static const char* memtype_name(u32 t)
{
    switch (t & 0xFF)
    {
    case MemType_Unmapped: return "free";
    case MemType_Io: return "io";
    case MemType_Normal: return "normal";
    case MemType_CodeStatic: return "code";
    case MemType_CodeMutable: return "code-rw";
    case MemType_Heap: return "heap";
    case MemType_SharedMem: return "shared";
    case MemType_WeirdMappedMem: return "mapped(old)";
    case MemType_ModuleCodeStatic: return "alias-code";
    case MemType_ModuleCodeMutable: return "alias-code-rw";
    case MemType_IpcBuffer0: return "ipc0";
    case MemType_MappedMemory: return "stack-map";
    case MemType_ThreadLocal: return "tls";
    case MemType_TransferMemIsolated: return "transfer-iso";
    case MemType_TransferMem: return "transfer";
    case MemType_ProcessMem: return "process";
    case MemType_Reserved: return "reserved";
    case MemType_IpcBuffer1: return "ipc1";
    case MemType_IpcBuffer3: return "ipc3";
    case MemType_KernelStack: return "kstack";
    case MemType_CodeReadOnly: return "jit-ro";
    case MemType_CodeWritable: return "jit-rw";
    case MemType_Coverage: return "coverage";
    case MemType_Insecure: return "insecure";
    default: return "?";
    }
}

static const char* perm_name(u32 p)
{
    static char s[4][4];
    static int i;
    char* q = s[i++ & 3];
    q[0] = (p & Perm_R) ? 'r' : '-';
    q[1] = (p & Perm_W) ? 'w' : '-';
    q[2] = (p & Perm_X) ? 'x' : '-';
    q[3] = 0;
    return q;
}

static void size_str(char* buf, size_t n, u64 v)
{
    if (v >= (1ull << 30) && (v % (1ull << 30)) == 0)
        snprintf(buf, n, "%" PRIu64 " GB", v >> 30);
    else if (v >= MB)
        snprintf(buf, n, "%.1f MB", (double)v / (double)MB);
    else
        snprintf(buf, n, "%" PRIu64 " KB", v / KB);
}

// ---------------------------------------------------------------------------
// Address-space facts

typedef struct
{
    u64 start, end; // [start, end)
} Range;

static Range g_aslr, g_heap, g_alias, g_stack;
static Handle g_proc = INVALID_HANDLE;
static const char* g_proc_src = "none";
static const char* g_layout = "unknown";
static int g_layout_bits;

static Range get_region(InfoType addr_t, InfoType size_t_, const char* name)
{
    Range r = {0, 0};
    u64 a = 0, s = 0;
    Result rc1 = svcGetInfo(&a, addr_t, CUR_PROCESS_HANDLE, 0);
    Result rc2 = svcGetInfo(&s, size_t_, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(rc1) || R_FAILED(rc2))
    {
        out("  %-6s region: svcGetInfo failed %s / %s\n", name, rcstr(rc1), rcstr(rc2));
        return r;
    }
    r.start = a;
    r.end = a + s;
    char sz[32];
    size_str(sz, sizeof(sz), s);
    out("  %-6s 0x%010" PRIX64 "-0x%010" PRIX64 " %9s %s\n", name, r.start, r.end, sz,
        r.end <= FOUR_GB ? "(below 4 GB)" : (r.start < FOUR_GB ? "(crosses 4 GB)" : "(above 4 GB)"));
    return r;
}

// Finds `size` bytes of unmapped address space in [lo, hi), 2 MB aligned,
// not overlapping any range in `avoid`. Lowest fit, or highest with `high`.
// Call with virtmemLock held so nothing else maps there before we reserve it.
static u64 find_free(u64 lo, u64 hi, u64 size, const Range* avoid, int navoid, bool high)
{
    u64 best = 0;
    u64 addr = lo;
    while (addr < hi)
    {
        MemoryInfo mi;
        u32 pi;
        if (R_FAILED(svcQueryMemory(&mi, &pi, addr)))
            break;
        u64 bstart = mi.addr, bend = mi.addr + mi.size;
        if (bend <= addr)
            break;
        if ((mi.type & 0xFF) == MemType_Unmapped)
        {
            u64 s = bstart < lo ? lo : bstart;
            u64 e = bend > hi ? hi : bend;
            // Walk the free block, stepping over the regions to avoid.
            u64 cur = s;
            while (cur < e)
            {
                bool inside = false;
                u64 piece_end = e;
                for (int i = 0; i < navoid; i++)
                {
                    const Range* r = &avoid[i];
                    if (r->end <= r->start)
                        continue;
                    if (r->start <= cur && cur < r->end)
                    {
                        cur = r->end;
                        inside = true;
                        break;
                    }
                    if (cur < r->start && r->start < piece_end)
                        piece_end = r->start;
                }
                if (inside)
                    continue;
                u64 a = (cur + BIG_ALIGN - 1) & ~(BIG_ALIGN - 1);
                if (a < piece_end && size <= piece_end - a)
                {
                    if (!high)
                        return a;
                    u64 top = (piece_end - size) & ~(BIG_ALIGN - 1);
                    if (top >= a && top > best)
                        best = top;
                }
                cur = piece_end;
            }
        }
        addr = bend;
    }
    return best;
}

static void print_low_map(void)
{
    logonly("\nMemory map below 4 GB (svcQueryMemory):\n");
    u64 addr = 0;
    int nfree = 0;
    u64 largest = 0, free_total = 0;
    while (addr < FOUR_GB)
    {
        MemoryInfo mi;
        u32 pi;
        Result rc = svcQueryMemory(&mi, &pi, addr);
        if (R_FAILED(rc))
        {
            logonly("  svcQueryMemory(0x%" PRIX64 ") failed %s\n", addr, rcstr(rc));
            break;
        }
        char sz[32];
        size_str(sz, sizeof(sz), mi.size);
        logonly("  0x%010" PRIX64 "-0x%010" PRIX64 " %10s %-13s %s attr=%X\n", mi.addr,
                mi.addr + mi.size, sz, memtype_name(mi.type), perm_name(mi.perm), mi.attr);
        if ((mi.type & 0xFF) == MemType_Unmapped)
        {
            u64 s = mi.addr < g_aslr.start ? g_aslr.start : mi.addr;
            u64 e = mi.addr + mi.size;
            if (e > FOUR_GB)
                e = FOUR_GB;
            if (e > s)
            {
                nfree++;
                free_total += e - s;
                if (e - s > largest)
                    largest = e - s;
            }
        }
        u64 next = mi.addr + mi.size;
        if (next <= addr)
            break;
        addr = next;
    }
    char a[32], b[32];
    size_str(a, sizeof(a), free_total);
    size_str(b, sizeof(b), largest);
    out("  Free below 4 GB (inside the address space): %s in %d ranges, largest %s\n", a, nfree, b);
}

// ---------------------------------------------------------------------------
// Fill/verify

typedef struct
{
    double write_mbs, read_mbs;
    u64 bad;
} Speed;

static Speed fill_verify(volatile u64* p, u64 size, u64 salt)
{
    Speed sp = {0, 0, 0};
    u64 n = size / 8;
    u64 t0 = armGetSystemTick();
    for (u64 i = 0; i < n; i++)
        p[i] = ((u64)(uintptr_t)&p[i]) ^ salt;
    u64 t1 = armGetSystemTick();
    u64 bad = 0;
    for (u64 i = 0; i < n; i++)
        if (p[i] != (((u64)(uintptr_t)&p[i]) ^ salt))
            bad++;
    u64 t2 = armGetSystemTick();
    double wns = (double)armTicksToNs(t1 - t0), rns = (double)armTicksToNs(t2 - t1);
    sp.write_mbs = wns > 0 ? ((double)size / MB) / (wns / 1e9) : 0;
    sp.read_mbs = rns > 0 ? ((double)size / MB) / (rns / 1e9) : 0;
    sp.bad = bad;
    return sp;
}

// ---------------------------------------------------------------------------
// Route A: code-memory alias below 4 GB

typedef struct
{
    u64 size;
    void* src;
    u64 dst;
    VirtmemReservation* rv;
    bool mapped, rw;
} CodeMap;

typedef enum
{
    A_OK = 0,
    A_NO_HEAP,
    A_NO_RANGE,
    A_MAP_FAILED,
    A_PERM_FAILED,
    A_VERIFY_FAILED,
    A_UNMAP_FAILED,
} AResult;

static const char* aresult_name(AResult r)
{
    switch (r)
    {
    case A_OK: return "WORKS";
    case A_NO_HEAP: return "no heap (memalign failed)";
    case A_NO_RANGE: return "no free range below 4 GB";
    case A_MAP_FAILED: return "svcMapProcessCodeMemory FAILED";
    case A_PERM_FAILED: return "svcSetProcessMemoryPermission FAILED";
    case A_VERIFY_FAILED: return "data check FAILED";
    case A_UNMAP_FAILED: return "unmap FAILED";
    }
    return "?";
}

static void query_line(const char* what, u64 addr)
{
    MemoryInfo mi;
    u32 pi;
    Result rc = svcQueryMemory(&mi, &pi, addr);
    if (R_FAILED(rc))
        logonly("      %s: query failed %s\n", what, rcstr(rc));
    else
        logonly("      %s: 0x%010" PRIX64 " is %s %s attr=%X (block 0x%" PRIX64 "+0x%" PRIX64 ")\n", what,
                addr, memtype_name(mi.type), perm_name(mi.perm), mi.attr, mi.addr, mi.size);
}

static AResult codemap_open(CodeMap* m, u64 size, bool high)
{
    memset(m, 0, sizeof(*m));
    m->size = size;
    m->src = memalign(BIG_ALIGN, size);
    if (!m->src)
        m->src = memalign(PAGE, size);
    if (!m->src)
        return A_NO_HEAP;

    Range avoid[3] = {g_heap, g_alias, g_stack};
    u64 lo = g_aslr.start ? g_aslr.start : 0x8000000ull;
    virtmemLock();
    m->dst = find_free(lo, FOUR_GB, size, avoid, 3, high);
    if (m->dst)
        m->rv = virtmemAddReservation((void*)(uintptr_t)m->dst, size);
    virtmemUnlock();
    if (!m->dst)
    {
        free(m->src);
        m->src = NULL;
        return A_NO_RANGE;
    }

    logonly("      heap block 0x%010" PRIX64 " -> target 0x%010" PRIX64 "\n", (u64)(uintptr_t)m->src, m->dst);
    // Touch the source so its pages are known-good before they are aliased.
    memset(m->src, 0, size);

    Result rc = svcMapProcessCodeMemory(g_proc, m->dst, (u64)(uintptr_t)m->src, size);
    logonly("      svcMapProcessCodeMemory: %s\n", rcstr(rc));
    if (R_FAILED(rc))
    {
        out("    svcMapProcessCodeMemory(0x%" PRIX64 ") -> %s\n", m->dst, rcstr(rc));
        return A_MAP_FAILED;
    }
    m->mapped = true;
    query_line("after map", m->dst);
    query_line("source   ", (u64)(uintptr_t)m->src);

    rc = svcSetProcessMemoryPermission(g_proc, m->dst, size, Perm_Rw);
    logonly("      svcSetProcessMemoryPermission(RW): %s\n", rcstr(rc));
    if (R_FAILED(rc))
    {
        out("    svcSetProcessMemoryPermission(RW) -> %s\n", rcstr(rc));
        return A_PERM_FAILED;
    }
    m->rw = true;
    query_line("after RW ", m->dst);
    return A_OK;
}

static AResult codemap_close(CodeMap* m, u64 salt, bool check_alias)
{
    AResult res = A_OK;
    if (m->mapped)
    {
        Result rc = svcUnmapProcessCodeMemory(g_proc, m->dst, (u64)(uintptr_t)m->src, m->size);
        logonly("      svcUnmapProcessCodeMemory: %s\n", rcstr(rc));
        if (R_FAILED(rc))
        {
            out("    svcUnmapProcessCodeMemory -> %s (heap block leaked)\n", rcstr(rc));
            res = A_UNMAP_FAILED;
            m->src = NULL; // still borrowed by the kernel: do not free it
        }
        else
        {
            query_line("source after unmap", (u64)(uintptr_t)m->src);
            if (check_alias && m->rw)
            {
                // Same physical pages: what we wrote through the low alias
                // must now read back through the heap address (with the low
                // address baked into the pattern).
                volatile u64* s = (volatile u64*)m->src;
                u64 n = m->size / 8, bad = 0;
                for (u64 i = 0; i < n; i += 4096)
                    if (s[i] != ((m->dst + i * 8) ^ salt))
                        bad++;
                logonly("      alias check through the heap address: %s\n", bad ? "MISMATCH" : "same pages");
                if (bad)
                    res = A_VERIFY_FAILED;
            }
        }
    }
    if (m->rv)
    {
        virtmemLock();
        virtmemRemoveReservation(m->rv);
        virtmemUnlock();
        m->rv = NULL;
    }
    if (m->src)
        free(m->src);
    m->src = NULL;
    m->mapped = m->rw = false;
    return res;
}

static AResult route_a(u64 size, bool high, const char* label)
{
    char sz[32];
    size_str(sz, sizeof(sz), size);
    out("  A %-8s %s: ", sz, label);
    logonly("\n");
    CodeMap m;
    AResult r = codemap_open(&m, size, high);
    if (r == A_OK)
    {
        u64 salt = 0x5A5A0000A5A5ull ^ size;
        Speed sp = fill_verify((volatile u64*)(uintptr_t)m.dst, size, salt);
        logonly("      write %.0f MB/s, read %.0f MB/s, bad words %" PRIu64 "\n", sp.write_mbs, sp.read_mbs, sp.bad);
        AResult c = codemap_close(&m, salt, true);
        if (sp.bad)
            r = A_VERIFY_FAILED;
        else
            r = c;
        if (r == A_OK)
            out("WORKS at 0x%08" PRIX64 " (w %.0f / r %.0f MB/s)\n", m.dst, sp.write_mbs, sp.read_mbs);
        else
            out("%s\n", aresult_name(r));
    }
    else
    {
        codemap_close(&m, 0, false);
        out("%s\n", aresult_name(r));
    }
    return r;
}

// The port needs both pools at once: arena + RenderWare heap, kept mapped.
static AResult route_a_both(u64 a_size, u64 b_size)
{
    char s1[32], s2[32];
    size_str(s1, sizeof(s1), a_size);
    size_str(s2, sizeof(s2), b_size);
    out("  A %s + %s together: ", s1, s2);
    logonly("\n");
    CodeMap a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    AResult ra = codemap_open(&a, a_size, false);
    AResult rb = ra == A_OK ? codemap_open(&b, b_size, false) : A_NO_HEAP;
    AResult res = ra != A_OK ? ra : rb;
    bool filled = false;
    if (res == A_OK)
    {
        Speed sa = fill_verify((volatile u64*)(uintptr_t)a.dst, a_size, 0x1111);
        Speed sb = fill_verify((volatile u64*)(uintptr_t)b.dst, b_size, 0x2222);
        filled = true;
        // Re-check the first after writing the second (no overlap).
        u64 bad = sa.bad + sb.bad;
        volatile u64* pa = (volatile u64*)(uintptr_t)a.dst;
        for (u64 i = 0; i < a_size / 8; i += 512)
            if (pa[i] != (((u64)(uintptr_t)&pa[i]) ^ 0x1111))
                bad++;
        if (bad)
            res = A_VERIFY_FAILED;
    }
    u64 adst = a.dst, bdst = b.dst;
    AResult cb = codemap_close(&b, 0x2222, filled);
    AResult ca = codemap_close(&a, 0x1111, filled);
    if (res == A_OK)
        res = cb != A_OK ? cb : ca;
    if (res == A_OK)
        out("WORKS at 0x%08" PRIX64 " and 0x%08" PRIX64 "\n", adst, bdst);
    else
        out("%s\n", aresult_name(res));
    return res;
}

// ---------------------------------------------------------------------------
// Routes B and C: svcMapMemory

typedef enum
{
    B_OK = 0,
    B_NA,
    B_NO_HEAP,
    B_NO_RANGE,
    B_FAILED,
} BResult;

static BResult map_memory_at(u64 dst_lo, u64 dst_hi, u64 size, bool expect_fail, Result* out_rc, u64* out_dst)
{
    void* src = memalign(PAGE, size);
    if (!src)
        return B_NO_HEAP;
    memset(src, 0, size);
    Range avoid[3] = {g_heap, g_alias, {0, 0}};
    int navoid = 2;
    if (expect_fail)
    {
        avoid[2] = g_stack;
        navoid = 3;
    }
    virtmemLock();
    u64 dst = find_free(dst_lo, dst_hi, size, avoid, navoid, false);
    VirtmemReservation* rv = dst ? virtmemAddReservation((void*)(uintptr_t)dst, size) : NULL;
    virtmemUnlock();
    *out_dst = dst;
    if (!dst)
    {
        free(src);
        return B_NO_RANGE;
    }
    Result rc = svcMapMemory((void*)(uintptr_t)dst, src, size);
    *out_rc = rc;
    BResult res = B_FAILED;
    if (R_SUCCEEDED(rc))
    {
        Speed sp = fill_verify((volatile u64*)(uintptr_t)dst, size, 0x3333);
        res = sp.bad ? B_FAILED : B_OK;
        Result urc = svcUnmapMemory((void*)(uintptr_t)dst, src, size);
        logonly("      svcUnmapMemory: %s\n", rcstr(urc));
        if (R_FAILED(urc))
            src = NULL; // leaked: still borrowed
    }
    virtmemLock();
    if (rv)
        virtmemRemoveReservation(rv);
    virtmemUnlock();
    if (src)
        free(src);
    return res;
}

// ---------------------------------------------------------------------------

// nxlinkConnectToHost blocks until TCP gives up when the PC's firewall drops
// the connection, which leaves the screen black. Connect with a short timeout.
static bool nxlink_connect_timeout(int ms)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return false;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(NXLINK_CLIENT_PORT);
    sa.sin_addr = __nxlink_host;
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
    connect(sock, (struct sockaddr*)&sa, sizeof(sa));
    struct pollfd pfd = {sock, POLLOUT, 0};
    int err = -1;
    socklen_t len = sizeof(err);
    if (poll(&pfd, 1, ms) != 1 || getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0)
    {
        close(sock);
        return false;
    }
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) & ~O_NONBLOCK);
    fflush(stdout);
    dup2(sock, STDOUT_FILENO);
    close(sock);
    return true;
}

// A tiny one-shot server: whoever connects to REPORT_PORT gets the report text,
// then the connection closes. Only the text this program printed is sent.
static int report_listen(void)
{
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0)
        return -1;
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(REPORT_PORT);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(ls, (struct sockaddr*)&sa, sizeof(sa)) != 0 || listen(ls, 2) != 0)
    {
        close(ls);
        return -1;
    }
    fcntl(ls, F_SETFL, fcntl(ls, F_GETFL, 0) | O_NONBLOCK);
    return ls;
}

static void report_serve(int ls)
{
    int c = accept(ls, NULL, NULL);
    if (c < 0)
        return;
    fcntl(c, F_SETFL, fcntl(c, F_GETFL, 0) & ~O_NONBLOCK);
    size_t off = 0;
    while (off < g_report_len)
    {
        ssize_t w = send(c, g_report + off, g_report_len - off, 0);
        if (w <= 0)
            break;
        off += (size_t)w;
    }
    close(c);
    fprintf(stderr, "(report sent to the PC)\n");
    consoleUpdate(NULL);
}

static const char* applet_name(AppletType t)
{
    switch (t)
    {
    case AppletType_None: return "None";
    case AppletType_Default: return "Default";
    case AppletType_Application: return "Application";
    case AppletType_SystemApplet: return "SystemApplet";
    case AppletType_LibraryApplet: return "LibraryApplet";
    case AppletType_OverlayApplet: return "OverlayApplet";
    case AppletType_SystemApplication: return "SystemApplication";
    }
    return "?";
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    fprintf(stderr, "TSSM memory test starting...\n");
    consoleUpdate(NULL);
    g_file = fopen(LOG_PATH, "w");
    // Sockets for the report server; stdout also goes to nxlink when the PC
    // accepts the connection within 3 seconds (it may be firewalled).
    bool sockets = R_SUCCEEDED(socketInitializeDefault());
    if (sockets && __nxlink_host.s_addr != 0)
    {
        fprintf(stderr, "Trying to send the log to the PC (3 s)...\n");
        consoleUpdate(NULL);
        g_nxlink = nxlink_connect_timeout(3000);
    }

    out("TSSM memory test 0.2 - can the port get RW memory below 4 GB?\n");
    out("Log file: %s (%s)  nxlink: %s\n", LOG_PATH, g_file ? "open" : "COULD NOT OPEN",
        g_nxlink ? "connected" : "not used");

    // --- System and mode ---------------------------------------------------
    u32 hv = hosversionGet();
    out("\n[System]\n");
    out("  Firmware %u.%u.%u%s\n", HOSVER_MAJOR(hv), HOSVER_MINOR(hv), HOSVER_MICRO(hv),
        hosversionIsAtmosphere() ? " (Atmosphere)" : "");
    AppletType at = appletGetAppletType();
    bool full_ram = at == AppletType_Application || at == AppletType_SystemApplication;
    out("  Applet type: %s -> %s\n", applet_name(at),
        full_ram ? "FULL-RAM mode (title takeover)" : "APPLET mode (small memory budget)");
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    char t1[32], t2[32], t3[32];
    size_str(t1, sizeof(t1), total);
    size_str(t2, sizeof(t2), used);
    size_str(t3, sizeof(t3), (u64)(fake_heap_end - fake_heap_start));
    out("  Process memory: total %s, used %s; newlib heap %s\n", t1, t2, t3);
    u64 pid = 0;
    if (R_SUCCEEDED(svcGetInfo(&pid, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0)))
        logonly("  Program id: %016" PRIX64 "\n", pid);

    // --- Address space -----------------------------------------------------
    out("\n[Address space]\n");
    g_aslr = get_region(InfoType_AslrRegionAddress, InfoType_AslrRegionSize, "aslr");
    g_heap = get_region(InfoType_HeapRegionAddress, InfoType_HeapRegionSize, "heap");
    g_alias = get_region(InfoType_AliasRegionAddress, InfoType_AliasRegionSize, "alias");
    g_stack = get_region(InfoType_StackRegionAddress, InfoType_StackRegionSize, "stack");
    if (g_aslr.end == 0)
        g_layout = "unknown", g_layout_bits = 0;
    else if (g_aslr.end <= FOUR_GB)
        g_layout = "32-bit", g_layout_bits = 32;
    else if (g_aslr.end <= (1ull << 36))
        g_layout = "36-bit", g_layout_bits = 36;
    else
        g_layout = "39-bit", g_layout_bits = 39;
    out("  Layout: %s%s\n", g_layout, g_layout_bits == 39 ? " (Atmosphere's default for homebrew)" : "");
    out("  This program: code 0x%010" PRIX64 ", data 0x%010" PRIX64 "\n", (u64)(uintptr_t)&main,
        (u64)(uintptr_t)&g_file);
    out("                main stack 0x%010" PRIX64 ", newlib heap 0x%010" PRIX64 "\n", (u64)(uintptr_t)&pad,
        (u64)(uintptr_t)fake_heap_start);

    // --- Syscalls and handle -----------------------------------------------
    struct
    {
        unsigned id;
        const char* name;
    } svcs[] = {
        {0x04, "MapMemory"},
        {0x05, "UnmapMemory"},
        {0x06, "QueryMemory"},
        {0x73, "SetProcessMemoryPermission"},
        {0x77, "MapProcessCodeMemory"},
        {0x78, "UnmapProcessCodeMemory"},
    };
    out("  Syscalls hinted by the loader:");
    for (size_t i = 0; i < sizeof(svcs) / sizeof(svcs[0]); i++)
        out(" %s=%s", svcs[i].name, envIsSyscallHinted(svcs[i].id) ? "y" : "N");
    out("\n");
    g_proc = envGetOwnProcessHandle();
    if (g_proc != INVALID_HANDLE && g_proc != 0)
        g_proc_src = "own handle from the loader";
    else
    {
        g_proc = CUR_PROCESS_HANDLE;
        g_proc_src = "pseudo-handle (the loader gave none)";
    }
    out("  Process handle: 0x%X, %s\n", g_proc, g_proc_src);

    print_low_map();

    // Heap speed, as a baseline for the alias speeds below.
    {
        u64 sz = 64 * MB;
        void* p = memalign(PAGE, sz);
        if (p)
        {
            Speed sp = fill_verify((volatile u64*)p, sz, 0x4444);
            out("  Normal heap baseline (64 MB at 0x%010" PRIX64 "): w %.0f / r %.0f MB/s\n",
                (u64)(uintptr_t)p, sp.write_mbs, sp.read_mbs);
            free(p);
        }
    }

    // --- Route A ----------------------------------------------------------
    out("\n[Route A: heap block aliased below 4 GB with svcMapProcessCodeMemory]\n");
    static const u64 a_sizes[] = {1 * MB, 16 * MB, 64 * MB, 128 * MB, 256 * MB};
    AResult a_res[5];
    for (int i = 0; i < 5; i++)
        a_res[i] = route_a(a_sizes[i], false, "lowest free");
    AResult a_high = route_a(256 * MB, true, "highest fit");
    AResult a_both = route_a_both(128 * MB, 256 * MB);

    // --- Route B / C --------------------------------------------------------
    out("\n[Route B: svcMapMemory into the stack region below 4 GB]\n");
    BResult b_res = B_NA;
    Result b_rc = 0;
    u64 b_dst = 0;
    {
        virtmemLock();
        void* fs = virtmemFindStack(16 * MB, 0);
        virtmemUnlock();
        out("  virtmemFindStack(16 MB) would pick 0x%010" PRIX64 " (%s 4 GB)\n", (u64)(uintptr_t)fs,
            (u64)(uintptr_t)fs < FOUR_GB ? "below" : "above");
    }
    if (g_stack.start < FOUR_GB && g_stack.end > g_stack.start)
    {
        u64 hi = g_stack.end < FOUR_GB ? g_stack.end : FOUR_GB;
        static const u64 b_sizes[] = {16 * MB, 128 * MB, 256 * MB};
        for (int i = 0; i < 3; i++)
        {
            char sz[32];
            size_str(sz, sizeof(sz), b_sizes[i]);
            Result rc = 0;
            u64 dst = 0;
            BResult r = map_memory_at(g_stack.start, hi, b_sizes[i], false, &rc, &dst);
            out("  B %-8s: ", sz);
            switch (r)
            {
            case B_OK: out("WORKS at 0x%08" PRIX64 "\n", dst); break;
            case B_NO_HEAP: out("no heap (memalign failed)\n"); break;
            case B_NO_RANGE: out("no free range in the low stack region\n"); break;
            default: out("svcMapMemory(0x%" PRIX64 ") FAILED %s\n", dst, rcstr(rc)); break;
            }
            if (b_res == B_NA || (b_res == B_OK && r != B_OK))
            {
                // B_OK only if every size worked; otherwise the first failure
                b_res = r;
                b_rc = rc;
                b_dst = dst;
            }
        }
    }
    else
    {
        out("  Not available: the stack region is above 4 GB in this layout.\n");
    }

    out("\n[Route C: svcMapMemory below 4 GB outside the stack region; expected to fail]\n");
    {
        Result rc = 0;
        u64 dst = 0;
        u64 lo = g_aslr.start ? g_aslr.start : 0x8000000ull;
        BResult r = map_memory_at(lo, FOUR_GB, 1 * MB, true, &rc, &dst);
        if (r == B_OK)
            out("  1 MB at 0x%08" PRIX64 ": unexpectedly WORKED\n", dst);
        else if (r == B_FAILED)
            out("  1 MB at 0x%08" PRIX64 ": refused, %s\n", dst, rcstr(rc));
        else
            out("  skipped (%s)\n", r == B_NO_HEAP ? "no heap" : "no free range");
    }

    // --- Summary ------------------------------------------------------------
    bool a_ok_all = true;
    for (int i = 0; i < 5; i++)
        a_ok_all = a_ok_all && a_res[i] == A_OK;
    out("\n==================== SUMMARY ====================\n");
    out("Mode:    %s (%s)\n", full_ram ? "full RAM" : "APPLET - use title takeover (hold R on a game)",
        applet_name(at));
    out("Layout:  %s\n", g_layout);
    out("Route A (code alias below 4 GB): 1-256 MB %s; 256 MB high %s;\n",
        a_ok_all ? "all WORK" : "NOT all work", a_high == A_OK ? "works" : aresult_name(a_high));
    out("         128+256 MB together (what the port needs): %s\n", aresult_name(a_both));
    if (b_res == B_NA)
        out("Route B (stack-region svcMapMemory): not available in this layout\n");
    else if (b_res == B_OK)
        out("Route B (stack-region svcMapMemory): works (0x%08" PRIX64 ")\n", b_dst);
    else
        out("Route B (stack-region svcMapMemory): failed %s\n", rcstr(b_rc));
    if (a_both == A_OK)
        out("VERDICT: the port's low-4 GB plan WORKS on this console.\n");
    else if (b_res == B_OK)
        out("VERDICT: route A failed, route B works (36-bit layout only).\n");
    else if (!full_ram && (a_both == A_NO_HEAP))
        out("VERDICT: not enough memory in applet mode; try again via title takeover.\n");
    else
        out("VERDICT: NO working route here. Send %s to the port team.\n", LOG_PATH);
    out("=================================================\n");
    int ls = sockets ? report_listen() : -1;
    if (ls >= 0)
        out("The PC can now fetch this report (port %d). Leave this on screen, then press + to exit.\n", REPORT_PORT);
    else
        out("Press + to exit.\n");

    if (g_file)
    {
        fclose(g_file);
        g_file = NULL;
    }

    while (appletMainLoop())
    {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        if (ls >= 0)
            report_serve(ls);
        consoleUpdate(NULL);
    }

    if (ls >= 0)
        close(ls);
    if (sockets)
        socketExit();
    consoleExit(NULL);
    return 0;
}
