// The Nintendo 3DS homebrew build's start-up, model check, lifecycle and crash
// code. See i3DS.h; docs/3DS.md.

// The libctru side (i3DS.h): nothing of the game's is included here, and this
// file is compiled with libctru's short enums (CMakeLists.txt).
#include "i3DS.h"
#include "iStereo3DS.h"
#ifdef RW_C3D
// C types only, as i3DS.h: safe on this, libctru's, side of the enum line.
#include "c3d/rwc3d.h"
#endif

#include <3ds.h>
#if defined(TSSM_3DS_PROBE) && TSSM_3DS_PROBE == 11
// The leak ladder's citro3d layer, and its text screen's stdout.
#include <citro3d.h>
#include <sys/iosupport.h>
#endif

#include <SDL3/SDL.h>

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// SDL is built as libctru is (short enums) and the port with int enums; the
// structs that cross between them lay out the same either way. The same
// numbers are asserted on the game side (iLayout3DS.cpp).
static_assert(sizeof(SDL_AudioSpec) == 12 && offsetof(SDL_AudioSpec, channels) == 4 &&
                  offsetof(SDL_AudioSpec, freq) == 8,
              "SDL_AudioSpec differs between the port and SDL");
static_assert(sizeof(SDL_KeyboardEvent) == 40 && offsetof(SDL_KeyboardEvent, key) == 28,
              "SDL_KeyboardEvent differs between the port and SDL");
static_assert(sizeof(SDL_Event) == 128, "SDL_Event differs between the port and SDL");
// The events the overlay's ImGui backend reads (gui/iGui.cpp, imgui_impl_sdl3).
static_assert(sizeof(SDL_TouchFingerEvent) == 56 && offsetof(SDL_TouchFingerEvent, x) == 32 &&
                  offsetof(SDL_TouchFingerEvent, windowID) == 52,
              "SDL_TouchFingerEvent differs between the port and SDL");
static_assert(sizeof(SDL_MouseButtonEvent) == 40 && offsetof(SDL_MouseButtonEvent, x) == 28 &&
                  sizeof(SDL_MouseMotionEvent) == 48 && offsetof(SDL_MouseMotionEvent, x) == 28,
              "SDL's mouse events differ between the port and SDL");
static_assert(sizeof(SDL_MouseWheelEvent) == 56 && offsetof(SDL_MouseWheelEvent, direction) == 32 &&
                  offsetof(SDL_MouseWheelEvent, mouse_x) == 36,
              "SDL_MouseWheelEvent differs between the port and SDL");
static_assert(sizeof(SDL_GamepadDeviceEvent) == 24 &&
                  offsetof(SDL_GamepadDeviceEvent, which) == 16 && sizeof(SDL_WindowEvent) == 32 &&
                  offsetof(SDL_WindowEvent, data1) == 20,
              "SDL's gamepad and window events differ between the port and SDL");

// The main thread's stack. libctru's default is 32 KB, which the game was never
// written for; 1 MB, as the Windows and Switch builds have.
extern "C" {
u32 __stacksize__ = 1024 * 1024;
}

namespace
{
    int sLinkSocket = -1;
    volatile bool sLinkDropping; // i3DSLinkWrite gave up on the last write
    volatile bool sLinkDead; // the PC's end went away
    int sLogServer = -1; // the pull-log server's listening socket
    pthread_t sServerThread;
    bool sServerThreadUp;
    volatile bool sServerStop; // Teardown: the server's thread ends
    char sAddress[32]; // i3DSAddress
    u64 sAddressAsked; // the system tick gethostid was last asked at
    u32* sSocBuffer;
    bool sSocketsUp;
    bool sRomfs;
    Result sRomfsResult;
    bool sNew3DS;
    int sHomeHolds;
    // i3DSServeFile: what the pull-log server sends instead of last_run.log.
    char sServed[300];

    // ------------------------------------------------------------ the exit fix

    // Every quit of the earlier builds left its process behind:
    // ended, never destroyed, holding one of the kernel's 47 code-set and
    // process objects; the third or fourth such exit in one power-on
    // panicked the kernel at the next start. A process is kept
    // while another process holds a handle to it or to an object it made (an
    // event, a memory block), so the quit gives nothing of the game's to a
    // service that could keep it, and takes back what it can.
    // TSSM_3DS_EXITFIX=off is the old quit, for the exit-loop probe's
    // comparison.
    bool sExitFix = true;
    // The cache flushes for the DSP and the GPU made by the game itself
    // (OwnCacheOp), the one part of the exit fix that runs every frame:
    // TSSM_3DS_OWNCACHE=off gives them back to the services and keeps the
    // rest, so the exit-loop probe can tell the two apart.
    bool sOwnCache = true;
    // The application's core-1 time limit when the game started, for the
    // quit's line (SDL raises it to 30% for its audio thread on core 1).
    u32 sCpuLimitAtStart;
    bool sCpuLimitKnown;
    // Cache flushes for the DSP and the GPU made by this process itself:
    // DSP flush, DSP invalidate, GSP flush, GSP invalidate.
    u32 sOwnCacheOps[4];
    // The kernel refused one (an emulator without the call): from then on
    // the services do it, as before.
    volatile bool sCacheSvcRefused;
    Result sCacheSvcResult;

    // What earlier programs left behind at start (the start-up's held line).
    struct Held
    {
        int processes; // processes in all, -1 when the kernel would not list them
        int sameName; // other processes named as this one (3dsx_app for a .3dsx), -1 unknown
        u32 notFreeKB; // the application region's memory not free when this program began
        u32 ownKB; // this program's image and first pages
        int byMemory; // earlier programs' images, judged by the memory
        char names[200]; // the other processes named as this one, with their ids
    };

    // The kernel's process objects, and of them those named as this one.
    void CountProcesses(Held* h)
    {
        h->processes = -1;
        h->sameName = -1;
        h->names[0] = '\0';
        u32 pids[64];
        s32 n = 0;
        // Azahar has neither the list's answer checked nor Luma3DS's names.
        if (R_FAILED(svcGetProcessList(&n, pids, 64)) || n < 1 || n > 64)
        {
            return;
        }
        h->processes = (int)n;
        u32 me = 0;
        s64 myName = 0;
        svcGetProcessId(&me, CUR_PROCESS_HANDLE);
        // Luma3DS's own query (type 0x10000): the code set's 8-byte name.
        if (R_FAILED(svcGetProcessInfo(&myName, CUR_PROCESS_HANDLE, 0x10000)) || myName == 0)
        {
            return;
        }
        int same = 0;
        size_t len = 0;
        for (s32 i = 0; i < n; i++)
        {
            if (pids[i] == me)
            {
                continue;
            }
            Handle p = 0;
            s64 name = 0;
            if (R_FAILED(svcOpenProcess(&p, pids[i])))
            {
                continue;
            }
            const Result r = svcGetProcessInfo(&name, p, 0x10000);
            // Closed at once: a handle kept here would itself keep that
            // process.
            svcCloseHandle(p);
            if (R_SUCCEEDED(r) && name == myName)
            {
                same++;
                if (len < sizeof(h->names) - 16)
                {
                    len += (size_t)snprintf(h->names + len, sizeof(h->names) - len, "%s%lu",
                                            same > 1 ? ", " : "", (unsigned long)pids[i]);
                }
            }
        }
        h->sameName = same;
    }

    extern "C" char __start__[];
    extern "C" char __end__[];

    void MeasureHeld(Held* h)
    {
        CountProcesses(h);
        // libctru split the memory that was free when this program began
        // into the two heaps; what was not free is this program's image and
        // whatever earlier programs still hold.
        const u32 region = osGetMemRegionSize(MEMREGION_APPLICATION);
        const u32 heaps = envGetHeapSize() + envGetLinearHeapSize();
        h->notFreeKB = region > heaps ? (region - heaps) >> 10 : 0;
        // The image's pages, and 8 KB more: what the first launch in a
        // power-on shows (EXIT-DIFF.md, section 0).
        const u32 image = ((u32)(uintptr_t)__end__ - (u32)(uintptr_t)__start__ + 0xFFF) & ~0xFFFu;
        h->ownKB = (image >> 10) + 8;
        h->byMemory = 0;
        if (h->notFreeKB > h->ownKB + h->ownKB / 2)
        {
            h->byMemory = (int)((h->notFreeKB - h->ownKB + h->ownKB / 2) / h->ownKB);
        }
    }

    // The earlier programs held: by name when the kernel says, else by memory.
    int HeldCount(const Held* h)
    {
        return h->sameName >= 0 ? h->sameName : h->byMemory;
    }

    Held sHeldAtStart;

    u32 SecondsSincePowerOn()
    {
        return (u32)(svcGetSystemTick() / SYSCLOCK_ARM11);
    }

    // ------------------------------------------------------------ the model

    // The name on the port's own text screens (this one and the game-files
    // screen): the build's TSSM_3DS_TITLE when it has one (CMakeLists.txt).
#ifdef TSSM_3DS_TITLE_TEXT
#define TSSM_3DS_SCREEN_NAME(indent) indent TSSM_3DS_TITLE_TEXT "\n"
#else
#define TSSM_3DS_SCREEN_NAME(indent) indent "The SpongeBob SquarePants Movie\n" indent "(homebrew port)\n"
#endif

    // New 3DS only: an Old 3DS has a third of the CPU and half the memory a .3dsx gets
    // on a New one. Said on the console's own text screen, before SDL or the
    // log exist, and then the program ends.
    void Old3DSScreen()
    {
        gfxInitDefault();
        consoleInit(GFX_TOP, NULL);

        printf("\n\n"
               TSSM_3DS_SCREEN_NAME("  ")
               "\n"
               "  This console is not supported.\n"
               "\n"
               "  The port needs a New 3DS, New 3DS XL\n"
               "  or New 2DS XL. An original 3DS, 3DS XL\n"
               "  or 2DS is too slow for the game and\n"
               "  has too little memory for it.\n"
               "\n"
               "\n"
               "  Press START to exit.\n");

        while (aptMainLoop())
        {
            hidScanInput();
            if (hidKeysDown() & KEY_START)
            {
                break;
            }
            gfxFlushBuffers();
            gfxSwapBuffers();
            gspWaitForVBlank();
        }

        gfxExit();
    }

    // ---------------------------------------------------------- HOME and sleep

    aptHookCookie sHook;
    bool sAway;
    int sResumes;

    // SDL's 3DS video driver sends no background or foreground events, so the
    // hook stands in for SDL_EVENT_WILL_ENTER_BACKGROUND and
    // DID_ENTER_FOREGROUND (iPadHostSDL.cpp's WatchBackground on Android), as
    // the Switch's applet hook does: the game clock stops while the HOME menu
    // is open or the console sleeps (the lid is closed), so the first frame
    // back is an ordinary frame and not the whole absence. Coming back
    // restarts the audio devices.
    //
    // Runs on the game thread: SDL calls aptMainLoop from SDL_PumpEvents.
    void Away(const char* why)
    {
        if (!sAway)
        {
            sAway = true;
            i3DSGameAway();
            printf("tssm: 3ds: %s; the game clock is paused\n", why);
        }
    }

    void Back(const char* why)
    {
        if (sAway)
        {
            sAway = false;
            sResumes++;
            i3DSGameBack();
            printf("tssm: 3ds: %s; the game clock runs again\n", why);
        }
    }

    void OnApt(APT_HookType hook, void*)
    {
        switch (hook)
        {
        case APTHOOK_ONSUSPEND:
            Away("the HOME menu is open");
            break;
        case APTHOOK_ONSLEEP:
            Away("the console is asleep");
            break;
        case APTHOOK_ONRESTORE:
            Back("back from the HOME menu");
            break;
        case APTHOOK_ONWAKEUP:
            Back("woke from sleep");
            break;
        case APTHOOK_ONEXIT:
            printf("tssm: 3ds: the system asked the game to close\n");
            break;
        default:
            break;
        }
    }

    // ------------------------------------------------------------------ crash

    void Line(int fd, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    // The installed title's own ID (tools/3ds/tssm.rsf's UniqueId). A .3dsx
    // runs under another title's (the Homebrew Launcher's host, or none in
    // an emulator), so the ID, not envIsHomebrew, tells the two apart.
    const u64 kCiaTitleId = 0x000400000F755300ull;
    bool sInstalled;

    const char* ProgramName()
    {
        return sInstalled ? "tssm.cia" : "tssm.3dsx";
    }

    void Line(int fd, const char* fmt, ...)
    {
        char line[512];
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(line, sizeof(line), fmt, ap);
        va_end(ap);
        if (n <= 0)
        {
            return;
        }
        if (n >= (int)sizeof(line))
        {
            n = (int)sizeof(line) - 1;
        }
        // crash.log first: stdout goes through the log tee (iLog.cpp), which
        // takes a lock that the faulting thread may be holding.
        if (fd >= 0)
        {
            ssize_t wrote = write(fd, line, (size_t)n);
            (void)wrote;
        }
        fwrite(line, 1, (size_t)n, stdout);
    }

    int OpenCrashLog()
    {
        const char* path = i3DSCrashLogPath();
        if (path == NULL || path[0] == '\0')
        {
            return -1;
        }
        return open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    }

    void Stamp(int fd, const char* what)
    {
        time_t now = time(NULL);
        struct tm t;
        memset(&t, 0, sizeof(t));
        localtime_r(&now, &t);
        Line(fd, "\n==== %04d-%02d-%02d %02d:%02d:%02d  %s (built %s %s)\n%s\n", t.tm_year + 1900,
             t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, ProgramName(), __DATE__,
             __TIME__, what);
    }

    // The image's code, for telling return addresses from data on the stack.
    // A .3dsx runs at the address it was linked at, so these are the ELF's
    // addresses too and addr2line reads them as they are. 3dsx.ld has no
    // end-of-text symbol; the unwind index comes after .text and .rodata, so
    // it bounds the code from above, loosely.
    extern "C" char __exidx_start[];
    // libctru's heap bases, for the start-up line.
    extern "C" u32 __ctru_heap;
    extern "C" u32 __ctru_linear_heap;

    bool InText(u32 a)
    {
        return a >= (u32)(uintptr_t)__start__ && a < (u32)(uintptr_t)__exidx_start;
    }

    alignas(8) u8 sExceptionStack[0x4000];

    // The kernel calls this on the faulting thread's behalf, on the stack
    // above, for a data or prefetch abort, an undefined instruction or a VFP
    // fault. Everything is formatted on the stack: the heap may be what
    // failed. Then the handler takes itself out and breaks, so that Luma3DS's
    // own exception screen and dump (sd:/luma/dumps/arm11/) follow, as they
    // would have without it.
    __attribute__((target("arm"), noreturn)) void OnException(ERRF_ExceptionInfo* excep,
                                                              CpuRegisters* regs)
    {
        const int fd = OpenCrashLog();

        const char* kind = "exception";
        switch (excep->type)
        {
        case ERRF_EXCEPTION_PREFETCH_ABORT:
            kind = "prefetch abort";
            break;
        case ERRF_EXCEPTION_DATA_ABORT:
            kind = "data abort";
            break;
        case ERRF_EXCEPTION_UNDEFINED:
            kind = "undefined instruction";
            break;
        case ERRF_EXCEPTION_VFP:
            kind = "VFP exception";
            break;
        default:
            break;
        }

        char what[160];
        snprintf(what, sizeof(what), "tssm: CRASH -- %s at pc %#lx", kind, (unsigned long)regs->pc);
        Stamp(fd, what);
        Line(fd, "tssm:   far %#lx  fsr %#lx  lr %#lx  sp %#lx  cpsr %#lx\n",
             (unsigned long)excep->far, (unsigned long)excep->fsr, (unsigned long)regs->lr,
             (unsigned long)regs->sp, (unsigned long)regs->cpsr);
        for (int i = 0; i < 13; i += 4)
        {
            Line(fd, "tssm:   r%-2d %08lx", i, (unsigned long)regs->r[i]);
            for (int j = i + 1; j < i + 4 && j < 13; j++)
            {
                Line(fd, "  r%-2d %08lx", j, (unsigned long)regs->r[j]);
            }
            Line(fd, "\n");
        }

        // No frame pointers in an optimised ARM build, so the caller chain is
        // a scan: words on the stack that point into the code. Some are stale,
        // the first few are usually right.
        // arm-none-eabi-addr2line -f -C -e tssm.elf <address>
        Line(fd, "tssm:   pc %#lx, lr %#lx; code addresses on the stack:\n",
             (unsigned long)regs->pc, (unsigned long)regs->lr);
        const u32* sp = (const u32*)(uintptr_t)(regs->sp & ~3u);
        int found = 0;
        for (int i = 0; i < 512 && found < 24; i++)
        {
            const u32 v = sp[i];
            if (InText(v))
            {
                Line(fd, "tssm:     [sp+%#x] %#lx\n", i * 4, (unsigned long)v);
                found++;
            }
        }

        if (fd >= 0)
        {
            fsync(fd);
            close(fd);
            printf("tssm: this report is also in %s\n", i3DSCrashLogPath());
        }
        i3DSLogDrain(500);

        threadOnException(NULL, NULL, NULL);
        svcBreak(USERBREAK_PANIC);
        for (;;)
        {
        }
    }

    // ---------------------------------------------------------------- network

    // A non-blocking socket call that would have had to wait (StartLogServer
    // says why none may).
    bool WouldBlock()
    {
        return errno == EAGAIN || errno == EWOULDBLOCK;
    }

    // 3dslink's connection back to the PC (`3dslink -s`), with a timeout:
    // link3dsConnectToHost blocks until TCP gives up when the PC's firewall
    // drops it, with nothing on screen yet. The poll holds the SOC session
    // for up to `ms`, before any other thread uses it. The socket stays
    // non-blocking for the log tee (i3DSLinkWrite).
    int ConnectLink(int ms)
    {
        if (__3dslink_host.s_addr == 0)
        {
            return -1;
        }
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
        {
            return -1;
        }
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(LINK3DS_COMM_PORT);
        sa.sin_addr = __3dslink_host;
        if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0)
        {
            close(sock);
            return -1;
        }
        connect(sock, (struct sockaddr*)&sa, sizeof(sa));
        struct pollfd pfd = { sock, POLLOUT, 0 };
        int err = -1;
        socklen_t len = sizeof(err);
        if (poll(&pfd, 1, ms) != 1 || getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len) != 0 ||
            err != 0)
        {
            close(sock);
            return -1;
        }
        return sock;
    }

    // gethostid, for i3DSAddress: at start-up, then at most every 10 s, so
    // that an address that changed (sleep, the Wi-Fi coming back) or a bad
    // answer does not stay: when the service fails, libctru's gethostid can
    // hand its error code back as the address. An answer of 0 or -1 keeps
    // the last good one.
    void AskAddress()
    {
        const u64 now = svcGetSystemTick();
        if (sAddressAsked != 0 && now - sAddressAsked < 10ull * SYSCLOCK_ARM11)
        {
            return;
        }
        sAddressAsked = now;
        const long id = gethostid();
        if (id != 0 && id != -1)
        {
            struct in_addr a;
            a.s_addr = (in_addr_t)id;
            snprintf(sAddress, sizeof(sAddress), "%s", inet_ntoa(a));
        }
    }

    // The PC pulls the log instead of 3dslink pushing it, as on the Switch:
    // Windows Firewall blocks 3dslink's incoming connection by default, but
    // not the PC connecting out. Whoever connects to TCP 28772 gets
    // last_run.log as it is now; the connection then closes. A test-build
    // convenience on the local network: it sends only the game's own log and
    // reads nothing from the network.
    constexpr int kLogServerPort = 28772;

    // All `n` bytes to the non-blocking socket `c`. A full send buffer (the
    // PC reading slower than this sends) is waited out here, in 10 ms naps;
    // FALSE when the PC went away, took nothing for 10 s, or the program is
    // closing.
    bool SendAll(int c, const char* p, size_t n)
    {
        int idleMs = 0;
        while (n > 0)
        {
            const ssize_t w = send(c, p, n, 0);
            if (w > 0)
            {
                p += w;
                n -= (size_t)w;
                idleMs = 0;
            }
            else if (w < 0 && WouldBlock() && idleMs < 10000 && !sServerStop)
            {
                svcSleepThread(10000000ll);
                idleMs += 10;
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    void Serve(int c)
    {
        // Non-blocking whether or not it inherited that from the listening
        // socket, so a send never waits in the SOC service (SendAll waits).
        if (fcntl(c, F_SETFL, O_NONBLOCK) < 0)
        {
            close(c);
            return;
        }
        char path[300];
        if (sServed[0] != '\0')
        {
            snprintf(path, sizeof(path), "%s", sServed);
        }
        else
        {
            snprintf(path, sizeof(path), "%s/last_run.log", k3DSRoot);
        }
        FILE* f = fopen(path, "rb");
        if (f != NULL)
        {
            char buf[4096];
            size_t n;
            while (!sServerStop && (n = fread(buf, 1, sizeof(buf), f)) > 0 && SendAll(c, buf, n))
            {
            }
            fclose(f);
        }
        // The end of the file, then up to 2 s for the PC to read to it and
        // close its side, so that closing here cannot cut the file short.
        shutdown(c, SHUT_WR);
        for (int i = 0; i < 40 && !sServerStop; i++)
        {
            char b[64];
            const ssize_t r = recv(c, b, sizeof(b), 0);
            if (r == 0 || (r < 0 && !WouldBlock()))
            {
                break;
            }
            svcSleepThread(50000000ll);
        }
        close(c);
    }

    void* LogServerThread(void*)
    {
        while (!sServerStop)
        {
            const int c = accept(sLogServer, NULL, NULL);
            if (c >= 0)
            {
                Serve(c);
            }
            else
            {
                // EAGAIN / EWOULDBLOCK: nobody yet. Any other error (no
                // network, say) is tried again the same way.
                svcSleepThread(150000000ll);
            }
        }
        return NULL;
    }

    void StartLogServer()
    {
        const int ls = socket(AF_INET, SOCK_STREAM, 0);
        if (ls < 0)
        {
            return;
        }
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(kLogServerPort);
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
        // The console closes each connection first, which leaves the port in
        // TIME_WAIT for a while; without this a quick restart could not bind
        // it. A refusal changes nothing else.
        const int yes = 1;
        setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        // Non-blocking, as every socket here: libctru's socket calls share one
        // SOC session that answers one request at a time, so a call that waits
        // in it (a blocking accept) stalls every thread's socket calls.
        if (fcntl(ls, F_SETFL, O_NONBLOCK) < 0 ||
            bind(ls, (struct sockaddr*)&sa, sizeof(sa)) != 0 || listen(ls, 2) != 0)
        {
            close(ls);
            return;
        }
        sLogServer = ls;
        // A pthread is a libctru thread on core 0 (the app's core) at the
        // lowest priority, 0x3F: its tries run while the main thread waits or
        // sleeps (the spike sleeps a moment every frame).
        if (pthread_create(&sServerThread, NULL, LogServerThread, NULL) == 0)
        {
            sServerThreadUp = true;
        }
        else
        {
            sLogServer = -1;
            close(ls);
        }
    }

    // The sockets: 3dslink's connection back to the PC (`3dslink -s`), and
    // the pull-log server on TCP 28772. The SOC service wants a page-aligned
    // buffer of the program's that it maps until socExit: the only memory
    // block this program gives a service, so the sockets start only when
    // something uses them (3dslink, the spike, TSSM_3DS_LOG_SERVER=on).
    void StartSockets(bool link)
    {
        if (sSocketsUp)
        {
            return;
        }
        const u32 kSocBufferSize = 0x100000;
        sSocBuffer = (u32*)memalign(0x1000, kSocBufferSize);
        if (sSocBuffer != NULL && R_SUCCEEDED(socInit(sSocBuffer, kSocBufferSize)))
        {
            sSocketsUp = true;
            if (link)
            {
                sLinkSocket = ConnectLink(3000);
            }
            AskAddress();
            StartLogServer();
        }
        else
        {
            free(sSocBuffer);
            sSocBuffer = NULL;
        }
    }

    // TSSM_3DS_LOG_SERVER=on|off (test-env.txt); the spike's report needs it.
    bool LogServerWanted()
    {
        const char* e = getenv("TSSM_3DS_LOG_SERVER");
        if (e != NULL && (strcmp(e, "on") == 0 || strcmp(e, "1") == 0))
        {
            return true;
        }
        if (e != NULL && (strcmp(e, "off") == 0 || strcmp(e, "0") == 0))
        {
            return false;
        }
#ifdef TSSM_3DS_SPIKE
        return true;
#else
        return false;
#endif
    }

    // ------------------------------------------------------------------ files

    // iHost3DS.cpp's iHostRenameReplace moves the old file to `x.bak` before
    // renaming the new one in; a power cut between the two leaves `x.bak` and
    // no `x`. Put those back before anything reads them.
    void RecoverBak(const char* dir)
    {
        DIR* d = opendir(dir);
        if (d == NULL)
        {
            return;
        }
        for (struct dirent* e = readdir(d); e != NULL; e = readdir(d))
        {
            const size_t n = strlen(e->d_name);
            if (n <= 4 || strcmp(e->d_name + n - 4, ".bak") != 0)
            {
                continue;
            }
            char bak[600];
            char orig[600];
            snprintf(bak, sizeof(bak), "%s/%s", dir, e->d_name);
            snprintf(orig, sizeof(orig), "%s/%.*s", dir, (int)(n - 4), e->d_name);
            struct stat st;
            if (stat(orig, &st) != 0 && rename(bak, orig) == 0)
            {
                printf("tssm: 3ds: restored %s from an interrupted save\n", orig);
            }
        }
        closedir(d);
    }

    // ------------------------------------------------------- the error screen

    // A line of i3DSErrorBox's text, to stdout. In the spike the bottom
    // screen is already libctru's console, holding the spike's log, and
    // stdout stays the log's there (i3DSSpike.cpp): a second consoleInit
    // does not hand stdout to the console again, so the lines also go after
    // the log's on that console.
    void BoxLine(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

    void BoxLine(const char* fmt, ...)
    {
        char line[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(line, sizeof(line), fmt, ap);
        va_end(ap);
        fputs(line, stdout);
#ifdef TSSM_3DS_SPIKE
        i3DSSpikeBottom(line);
#endif
    }

    // The screens for a box, before or after SDL's video: TRUE when gfx was
    // already up.
    bool BoxOpen()
    {
        // On the console's own text screen, not libctru's error applet:
        // launching an applet captures the screens first
        // (aptConvertScreenForCapture), which reads address 0 when nothing has
        // set up the framebuffers yet -- as when the game files are missing,
        // which is reported before SDL's video starts (seen in the Azahar
        // emulator; a data abort on a console). The bottom screen, so whatever
        // the top one shows stays.
#ifdef RW_C3D
        // citro3d owns the top screen and swaps it; consoleInit swaps both
        // screens, so the GPU is waited for and citro3d stopped first. Nothing
        // draws after the box.
        rw::c3d::shutdown();
#endif
        const bool hadGfx = gspHasGpuRight();
        if (!hadGfx)
        {
            gfxInitDefault();
        }
#ifdef RW_C3D
        // The top screen's two buffers hold citro3d's last two frames, and the
        // swaps below (consoleInit's, the loop's) would show them in turn every
        // vblank. Single-buffered, a swap shows the buffer on the screen now.
        gfxSetDoubleBuffering(GFX_TOP, false);
#endif
#ifndef TSSM_3DS_SPIKE
        // The first consoleInit: stdout is the console's from here on.
        static PrintConsole sConsole;
        consoleInit(GFX_BOTTOM, &sConsole);
#endif
        return hadGfx;
    }

    // Text on the bottom screen, wrapped at spaces to its 40 columns.
    void BoxWrap(const char* text)
    {
        const char* p = text != NULL ? text : "";
        while (*p != '\0')
        {
            int n = 0;
            int cut = 0;
            while (p[n] != '\0' && p[n] != '\n' && n < 38)
            {
                if (p[n] == ' ')
                {
                    cut = n;
                }
                n++;
            }
            if (p[n] != '\0' && p[n] != '\n' && cut > 0)
            {
                n = cut;
            }
            BoxLine(" %.*s\n", n, p);
            p += n;
            while (*p == ' ')
            {
                p++;
            }
            // One newline ends the line; each one after it is an empty line.
            if (*p == '\n')
            {
                p++;
            }
        }
    }

    void BoxWait(bool hadGfx)
    {
        fflush(stdout);
        while (aptMainLoop())
        {
            hidScanInput();
            if (hidKeysDown() & KEY_START)
            {
                break;
            }
            gfxFlushBuffers();
            gfxSwapBuffers();
            gspWaitForVBlank();
        }

        if (!hadGfx)
        {
            gfxExit();
        }
    }

    // ------------------------------------------------- the game files' screen

    // The top screen in big type: libctru's console font, scaled, drawn into
    // the buffer in whatever format it has.
    void TopPut(u8* fb, GSPGPU_FramebufferFormat format, int x, int y, u32 rgb)
    {
        if (x < 0 || x >= 400 || y < 0 || y >= 240)
        {
            return;
        }
        // Stored a column at a time, bottom to top.
        const size_t at = (size_t)x * 240 + (size_t)(239 - y);
        const u8 r = (u8)(rgb >> 16), g = (u8)(rgb >> 8), b = (u8)rgb;
        switch (format)
        {
        case GSP_RGBA8_OES:
            fb[at * 4 + 0] = 0xFF;
            fb[at * 4 + 1] = b;
            fb[at * 4 + 2] = g;
            fb[at * 4 + 3] = r;
            break;
        case GSP_BGR8_OES:
            fb[at * 3 + 0] = b;
            fb[at * 3 + 1] = g;
            fb[at * 3 + 2] = r;
            break;
        case GSP_RGB565_OES:
            ((u16*)fb)[at] = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            break;
        default:
            break;
        }
    }

    // One line, centred, `scale` times the font's 8 pixels.
    void TopText(u8* fb, GSPGPU_FramebufferFormat format, int y, int scale, u32 rgb,
                 const char* text)
    {
        const ConsoleFont& font = consoleGetDefault()->font;
        const int n = (int)strlen(text);
        const int x0 = (400 - n * 8 * scale) / 2;
        for (int i = 0; i < n; i++)
        {
            const int c = (unsigned char)text[i] - font.asciiOffset;
            if (c < 0 || c >= font.numChars)
            {
                continue;
            }
            const u8* glyph = font.gfx + 8 * c;
            for (int gy = 0; gy < 8 * scale; gy++)
            {
                for (int gx = 0; gx < 8 * scale; gx++)
                {
                    if (glyph[gy / scale] & (0x80 >> (gx / scale)))
                    {
                        TopPut(fb, format, x0 + i * 8 * scale + gx, y + gy, rgb);
                    }
                }
            }
        }
    }

    // A path as a PC shows it on the SD card: without "sdmc:/".
    const char* OnCard(const char* path)
    {
        if (strncmp(path, "sdmc:/", 6) == 0)
        {
            return path + 6;
        }
        return path[0] == '/' ? path + 1 : path;
    }

    // What the folder holds instead, when that explains it: a disc image, or
    // the files a folder further down. "" otherwise; *empty when it holds
    // nothing at all.
    void FolderHint(const char* folder, char* out, size_t size, bool* empty)
    {
        out[0] = '\0';
        *empty = false;
        DIR* d = opendir(folder);
        if (d == NULL)
        {
            return;
        }
        *empty = true;
        struct dirent* e;
        while ((e = readdir(d)) != NULL)
        {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            {
                continue;
            }
            *empty = false;
            const size_t n = strlen(e->d_name);
            if (n > 4 && strcasecmp(e->d_name + n - 4, ".iso") == 0)
            {
                snprintf(out, size,
                         "%s is a disc image, which the game cannot read: copy the files that "
                         "are on the disc instead.",
                         e->d_name);
                break;
            }
            char sub[512];
            snprintf(sub, sizeof(sub), "%s/%s", folder, e->d_name);
            DIR* s = opendir(sub);
            if (s == NULL)
            {
                continue;
            }
            bool boot = false;
            struct dirent* f;
            while (!boot && (f = readdir(s)) != NULL)
            {
                boot = strcasecmp(f->d_name, "boot.HIP") == 0;
            }
            closedir(s);
            if (boot)
            {
                snprintf(out, size,
                         "The files are a folder too far down, in %s/%s: move what is in %s up "
                         "into %s.",
                         OnCard(folder), e->d_name, e->d_name, OnCard(folder));
                break;
            }
        }
        closedir(d);
    }

    // ------------------------------------------------------------------- quit

    u32 sMainThreadId;
    bool sHooked;
    bool sQuitting;
    bool sStopped; // everything that runs beside the main thread is stopped

    bool OnMainThread()
    {
        u32 self = 0;
        svcGetThreadId(&self, CUR_THREAD_HANDLE);
        return sMainThreadId == 0 || self == sMainThreadId;
    }

    void Step(const char* what)
    {
        i3DSQuitLine("%s", what);
    }

    // The port's own threads, for the quit's log: the main one and every
    // one made through threadCreate (wrapped below; SDL's, ndsp's, GSP's,
    // APT's and pthreads all are). Not the kernel's list of the process's
    // threads: svcGetThreadList with a process handle takes a reference to
    // the process and never gives it back, and a process with a reference
    // left is never freed after it ends (3dbrew, 3DS System Flaws).
    constexpr int kMaxThreadIds = 64;
    u32 sThreadIds[kMaxThreadIds];
    u32 sThreadsMade; // past kMaxThreadIds only counted

    struct OwnThread
    {
        u32 id;
        s32 prio;
        s32 core;
        bool ended; // exited, its object kept by a handle not yet closed
    };

    // The port's threads the kernel still has, the main one first; each
    // opened by id and closed at once (svcOpenThread gives back its own
    // reference to the process). -1: the kernel would not open the main
    // thread, so none of them is known.
    int OwnThreads(OwnThread* out, int max)
    {
        u32 mainId = sMainThreadId;
        if (mainId == 0)
        {
            svcGetThreadId(&mainId, CUR_THREAD_HANDLE);
        }
        const u32 made = __atomic_load_n(&sThreadsMade, __ATOMIC_ACQUIRE);
        const int known = made < (u32)kMaxThreadIds ? (int)made : kMaxThreadIds;
        int n = 0;
        for (int i = -1; i < known && n < max; i++)
        {
            const u32 id = i < 0 ? mainId : __atomic_load_n(&sThreadIds[i], __ATOMIC_ACQUIRE);
            Handle h = 0;
            if (id == 0 || R_FAILED(svcOpenThread(&h, CUR_PROCESS_HANDLE, id)))
            {
                if (i < 0)
                {
                    return -1;
                }
                continue; // gone: ended and freed
            }
            OwnThread& t = out[n++];
            t.id = id;
            // Outside the kernel's ranges: not answered (Azahar leaves the
            // registers as they were).
            if (R_FAILED(svcGetThreadPriority(&t.prio, h)) || t.prio < 0 || t.prio > 0x3F)
            {
                t.prio = -1;
            }
            if (R_FAILED(svcGetThreadIdealProcessor(&t.core, h)) || t.core < -2 || t.core > 3)
            {
                t.core = -9;
            }
            // A thread object is signalled once its thread has exited.
            t.ended = i >= 0 && svcWaitSynchronization(h, 0) == 0;
            svcCloseHandle(h);
        }
        return n;
    }

    // The port's threads with their priorities and cores, after `head`. On
    // a console the quit starts with the game's, SDL's and libctru's and
    // should end with the main thread and APT's.
    void FormatThreads(char* line, size_t size, const char* head)
    {
        OwnThread t[kMaxThreadIds + 1];
        const int n = OwnThreads(t, kMaxThreadIds + 1);
        if (n < 0)
        {
            snprintf(line, size, "%sno thread of the port's opened by the kernel", head);
            return;
        }
        int live = 0;
        for (int i = 0; i < n; i++)
        {
            live += t[i].ended ? 0 : 1;
        }
        int len = snprintf(line, size, "%s%d thread(s) of the port's:", head, live);
        for (int i = 0; i < n && len < (int)size - 48; i++)
        {
            len += snprintf(line + len, size - (size_t)len, " %lu%s (0x%lx, core %ld)%s",
                            (unsigned long)t[i].id, i == 0 ? " main" : "",
                            (unsigned long)t[i].prio, (long)t[i].core,
                            t[i].ended ? " ended, handle open" : "");
        }
        const u32 made = __atomic_load_n(&sThreadsMade, __ATOMIC_ACQUIRE);
        if (made > (u32)kMaxThreadIds && len < (int)size - 40)
        {
            snprintf(line + len, size - (size_t)len, "; %lu made, the first %d known",
                     (unsigned long)made, kMaxThreadIds);
        }
    }

    void LogThreads(const char* when)
    {
        char head[64];
        snprintf(head, sizeof(head), "%s: ", when);
        char line[440];
        FormatThreads(line, sizeof(line), head);
        i3DSQuitLine("%s", line);
    }

    // The port's running threads on `core` (the main one never is on core
    // 1); -1 unknown.
    int ThreadsOnCore(s32 core)
    {
        OwnThread t[kMaxThreadIds + 1];
        const int n = OwnThreads(t, kMaxThreadIds + 1);
        if (n < 0 || __atomic_load_n(&sThreadsMade, __ATOMIC_ACQUIRE) > (u32)kMaxThreadIds)
        {
            return -1;
        }
        int on = 0;
        for (int i = 0; i < n; i++)
        {
            if (!t[i].ended && t[i].core == -9)
            {
                return -1;
            }
            on += !t[i].ended && t[i].core == core ? 1 : 0;
        }
        return on;
    }

    void StopAudio()
    {
        // SDL's audio thread runs on the system core and mixes the game's
        // sound state; ndsp's thread wakes every DSP frame. Each user of
        // audio holds a reference, so it is released until SDL has stopped
        // both (SDL's CloseDevice ends in ndspExit, which waits for the DSP
        // to stop, unregisters the game's event and unloads the component).
        if (SDL_WasInit(SDL_INIT_AUDIO) != 0)
        {
            Step("stopping audio (SDL's audio thread, then ndsp)");
            for (int i = 0; i < 16 && SDL_WasInit(SDL_INIT_AUDIO) != 0; i++)
            {
                SDL_QuitSubSystem(SDL_INIT_AUDIO);
            }
        }
    }

    // Logged, not set: pm sets it to 0 itself once the program is gone
    // (Luma3DS's pm, at termination and at the next launch), and 0 is meant
    // for before an application is created; set while the game still runs
    // with a limit in effect, what it does is undefined (pm's reslimit.c).
    void LogCpuLimit()
    {
        u32 limit = 0;
        if (!sNew3DS || R_FAILED(APT_GetAppCpuTimeLimit(&limit)))
        {
            return;
        }
        if (sCpuLimitKnown)
        {
            i3DSQuitLine("the system core's time limit is %lu%% (%lu%% at start); left as it is, pm "
                         "sets it once the program is gone",
                         (unsigned long)limit, (unsigned long)sCpuLimitAtStart);
        }
        else
        {
            i3DSQuitLine("the system core's time limit is %lu%%; left as it is, pm sets it once the "
                         "program is gone",
                         (unsigned long)limit);
        }
    }

    void StopLogServer()
    {
        if (sServerThreadUp)
        {
            // Within one nap (150 ms at most), even halfway through sending
            // a file: no socket call is in flight when the sockets go.
            Step("stopping the log server's thread");
            sServerStop = true;
            pthread_join(sServerThread, NULL);
            sServerThreadUp = false;
        }
        if (sLogServer >= 0)
        {
            const int ls = sLogServer;
            sLogServer = -1;
            close(ls);
        }
    }

    void CloseSockets()
    {
        if (sLinkSocket >= 0)
        {
            // Unhooked from the log tee before it goes.
            const int fd = sLinkSocket;
            sLinkSocket = -1;
            close(fd);
        }
        if (!sSocketsUp)
        {
            return;
        }
        if (sExitFix)
        {
            // Every socket the SOC service still has for this program, shut
            // and closed in the service before its session and the
            // program's memory block go (libctru's socExit leaves that to
            // the caller).
            const int sh = SOCU_ShutdownSockets();
            const int cl = SOCU_CloseSockets();
            i3DSQuitLine("closing the sockets (SOC): every socket shut (%d) and closed (%d) in "
                         "the service first",
                         sh, cl);
        }
        else
        {
            Step("closing the sockets (SOC)");
        }
        const Result r = socExit();
        sSocketsUp = false;
        if (R_SUCCEEDED(r))
        {
            free(sSocBuffer);
        }
        else
        {
            // SOC may still map it: kept, the heap's end frees it.
            i3DSQuitLine("socExit failed: %#lx", (unsigned long)r);
        }
        sSocBuffer = NULL;
    }

    // The quit with the exit fix: the system core's thread first, then every
    // other thread, then the services, each waited for.
    void StopEverythingFixed()
    {
        LogThreads("at the start");

        StopAudio();
        const int core1 = ThreadsOnCore(1);
        if (core1 == 0)
        {
            Step("core 1: no thread of the game's left");
        }
        else if (core1 > 0)
        {
            i3DSQuitLine("core 1: %d thread(s) of the game's still there", core1);
        }
        else
        {
            Step("core 1: not known (the kernel did not give every thread's core)");
        }
        LogCpuLimit();
        if (sOwnCache)
        {
            i3DSQuitLine("cache flushes for the DSP and the GPU done by the game itself, no "
                         "process handle given to either service: DSP %lu + %lu, GSP %lu + %lu%s",
                         (unsigned long)sOwnCacheOps[0], (unsigned long)sOwnCacheOps[1],
                         (unsigned long)sOwnCacheOps[2], (unsigned long)sOwnCacheOps[3],
                         sCacheSvcRefused ? "; the kernel refused one, so the services did the rest"
                                          : "");
        }
        else
        {
            i3DSQuitLine("cache flushes for the DSP and the GPU done by the services "
                         "(TSSM_3DS_OWNCACHE=off), as the old quit did; by the game itself before the "
                         "switch was read: DSP %lu + %lu, GSP %lu + %lu",
                         (unsigned long)sOwnCacheOps[0], (unsigned long)sOwnCacheOps[1],
                         (unsigned long)sOwnCacheOps[2], (unsigned long)sOwnCacheOps[3]);
        }

        StopLogServer();

        if (sHooked)
        {
            // No HOME or sleep callback into the game while it comes down.
            Step("unhooking from APT");
            aptUnhook(&sHook);
            sHooked = false;
        }

#ifdef RW_C3D
        // Before SDL's video: citro3d draws through gfx and GSP. Its own
        // steps are lines of their own (c3dsubmit.cpp); it waits for the GPU
        // and the GX queue before it frees anything.
        if (rw::c3d::deviceOpen())
        {
            Step("closing citro3d");
            rw::c3d::shutdown();
        }
#endif
        // The screens show the last frame's buffers until the display
        // transfer and the next vblank are done with them. A sleep longer
        // than a vblank rather than a vblank wait: it cannot hang the quit.
        Step("waiting 20 ms for the last display transfer");
        svcSleepThread(20000000ll);

        if (SDL_WasInit(0) != 0)
        {
            Step("closing SDL (gfxExit and the GSP event thread, input)");
            SDL_Quit();
        }

        CloseSockets();
        if (sRomfs)
        {
            Step("closing the RomFS");
            romfsExit();
            sRomfs = false;
        }

        LogThreads("stopped");
        // Type 4: the handle table's count (Azahar answers with whatever
        // was in the registers).
        s64 handles = -1;
        if (R_FAILED(svcGetProcessInfo(&handles, CUR_PROCESS_HANDLE, 4)) || handles < 0 ||
            handles > 0x1000)
        {
            handles = -1;
        }
        Held h;
        CountProcesses(&h);
        i3DSQuitLine("%lld handle(s) open (libctru's own services among them; -1: not known); %d "
                     "process(es) on the console, %d of them named as this one besides it; %lu s "
                     "since power-on",
                     (long long)handles, h.processes, h.sameName,
                     (unsigned long)SecondsSincePowerOn());
        sStopped = true;
    }

    // Everything that runs beside the main thread, or reads memory the heap
    // frees take away, in the order that lets each go safely.
    void StopEverything()
    {
        if (sExitFix)
        {
            StopEverythingFixed();
            return;
        }
        i3DSQuitLine("the exit fix is off (TSSM_3DS_EXITFIX=off): the old quit");
        LogThreads("at the start");

        if (sServerThreadUp)
        {
            // Within one nap (150 ms at most), even halfway through sending
            // a file: no socket call is in flight when the sockets go.
            Step("stopping the log server's thread");
            sServerStop = true;
            pthread_join(sServerThread, NULL);
            sServerThreadUp = false;
        }
        if (sLogServer >= 0)
        {
            const int ls = sLogServer;
            sLogServer = -1;
            close(ls);
        }

        if (sHooked)
        {
            // No HOME or sleep callback into the game while it comes down.
            Step("unhooking from APT");
            aptUnhook(&sHook);
            sHooked = false;
        }

        // SDL's audio thread runs on the system core and mixes the game's
        // sound state; ndsp's thread wakes every DSP frame. Each user of
        // audio holds a reference, so it is released until SDL has stopped
        // both (SDL's CloseDevice ends in ndspExit).
        if (SDL_WasInit(SDL_INIT_AUDIO) != 0)
        {
            Step("stopping audio (SDL's audio thread, then ndsp)");
            for (int i = 0; i < 16 && SDL_WasInit(SDL_INIT_AUDIO) != 0; i++)
            {
                SDL_QuitSubSystem(SDL_INIT_AUDIO);
            }
        }
        u32 limit = 0;
        if (sNew3DS && R_SUCCEEDED(APT_GetAppCpuTimeLimit(&limit)))
        {
            i3DSQuitLine("the system core's time limit is %lu%%; left as it is",
                         (unsigned long)limit);
        }

#ifdef RW_C3D
        // Before SDL's video: citro3d draws through gfx and GSP. Its own
        // steps are lines of their own (c3dsubmit.cpp).
        if (rw::c3d::deviceOpen())
        {
            Step("closing citro3d");
            rw::c3d::shutdown();
        }
#endif

        if (SDL_WasInit(0) != 0)
        {
            Step("closing SDL (gfxExit and the GSP event thread, input)");
            SDL_Quit();
        }

        CloseSockets();
        if (sRomfs)
        {
            Step("closing the RomFS");
            romfsExit();
            sRomfs = false;
        }

        LogThreads("stopped");
        sStopped = true;
    }
} // namespace

namespace
{
    void ChainNext(const char* lastLine);
} // namespace

extern "C" void __real_exit(int code) __attribute__((noreturn));

// -Wl,--wrap=exit (CMakeLists.txt): every exit() in the program, libraries
// and return from main included.
extern "C" void __wrap_exit(int code)
{
    i3DSQuit(code);
}

extern "C" Thread __real_threadCreate(ThreadFunc entrypoint, void* arg, size_t stack_size, int prio,
                                      int core_id, bool detached);

// -Wl,--wrap=threadCreate: every libctru thread (SDL's, ndsp's, GSP's,
// pthreads) in the log with its memory block, which holds its stack, so that
// a fault address in a Luma3DS dump can be told apart from the game's.
//
// Every thread asked for on core 1 moves, not only SDL's audio thread; the
// logs show SDL's is the only one today (SDL puts it on the system core of a
// New 3DS, under a 30% time limit). The mixer needs more than that limit
// lends at the busy spots, so on a New 3DS it goes to core 2, which is the
// game's alone; core 1 if core 2 is refused.
// TSSM_AUDIO_CORE=1 keeps SDL's choice.
static int sAudioCore = 1;

int i3DSAudioCore()
{
    return __atomic_load_n(&sAudioCore, __ATOMIC_ACQUIRE);
}

extern "C" Thread __wrap_threadCreate(ThreadFunc entrypoint, void* arg, size_t stack_size, int prio,
                                      int core_id, bool detached)
{
    Thread t = NULL;
    if (core_id == 1 && sNew3DS)
    {
        const char* e = getenv("TSSM_AUDIO_CORE");
        if (e == NULL || strcmp(e, "1") != 0)
        {
            t = __real_threadCreate(entrypoint, arg, stack_size, prio, 2, detached);
            printf("tssm: 3ds: a thread asked for on core 1 (SDL's audio): %s\n",
                   t != NULL ? "on core 2 instead" : "core 2 refused, on core 1");
            if (t != NULL)
            {
                core_id = 2;
                __atomic_store_n(&sAudioCore, 2, __ATOMIC_RELEASE);
            }
        }
    }
    if (t == NULL)
    {
        t = __real_threadCreate(entrypoint, arg, stack_size, prio, core_id, detached);
    }
    u32 id = 0;
    if (t != NULL && R_SUCCEEDED(svcGetThreadId(&id, threadGetHandle(t))))
    {
        const u32 slot = __atomic_fetch_add(&sThreadsMade, 1u, __ATOMIC_ACQ_REL);
        if (slot < (u32)kMaxThreadIds)
        {
            __atomic_store_n(&sThreadIds[slot], id, __ATOMIC_RELEASE);
        }
    }
    printf("tssm: 3ds: thread: entry %p, stack %u, priority 0x%x, core %d: block at %p\n",
           (void*)entrypoint, (unsigned)stack_size, prio, core_id, (void*)t);
    return t;
}

// -Wl,--wrap for the DSP's and GSP's cache calls (CMakeLists.txt): each one
// sends the service a handle to this process, and a service that kept one
// would keep the process after it ends. Both services only run the kernel's
// cache call on the process they are given (3dbrew); the game runs it on
// itself. SDL's audio, ndsp, gfx and citro3d all come through here.
extern "C" Result __real_DSP_FlushDataCache(const void* address, u32 size);
extern "C" Result __real_DSP_InvalidateDataCache(const void* address, u32 size);
extern "C" Result __real_GSPGPU_FlushDataCache(const void* adr, u32 size);
extern "C" Result __real_GSPGPU_InvalidateDataCache(const void* adr, u32 size);

namespace
{
    bool OwnCacheOp(int which, const void* address, u32 size)
    {
        if (!sOwnCache || sCacheSvcRefused)
        {
            return false;
        }
        const Result r = which == 0 || which == 2
                             ? svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)address, size)
                             : svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE, (u32)address, size);
        if (R_FAILED(r))
        {
            sCacheSvcResult = r;
            sCacheSvcRefused = true;
            return false;
        }
        __atomic_fetch_add(&sOwnCacheOps[which], 1u, __ATOMIC_RELAXED);
        return true;
    }
} // namespace

extern "C" Result __wrap_DSP_FlushDataCache(const void* address, u32 size)
{
    return OwnCacheOp(0, address, size) ? 0 : __real_DSP_FlushDataCache(address, size);
}

extern "C" Result __wrap_DSP_InvalidateDataCache(const void* address, u32 size)
{
    return OwnCacheOp(1, address, size) ? 0 : __real_DSP_InvalidateDataCache(address, size);
}

extern "C" Result __wrap_GSPGPU_FlushDataCache(const void* adr, u32 size)
{
    return OwnCacheOp(2, adr, size) ? 0 : __real_GSPGPU_FlushDataCache(adr, size);
}

extern "C" Result __wrap_GSPGPU_InvalidateDataCache(const void* adr, u32 size)
{
    return OwnCacheOp(3, adr, size) ? 0 : __real_GSPGPU_InvalidateDataCache(adr, size);
}

int i3DSQuitting()
{
    return sQuitting;
}

void i3DSQuitLine(const char* fmt, ...)
{
    char line[480];
    int n = snprintf(line, sizeof(line), "tssm: 3ds: quit: ");
    va_list ap;
    va_start(ap, fmt);
    const int m = vsnprintf(line + n, sizeof(line) - (size_t)n - 1, fmt, ap);
    va_end(ap);
    if (m < 0)
    {
        return;
    }
    n += m;
    if (n > (int)sizeof(line) - 2)
    {
        n = (int)sizeof(line) - 2;
    }
    line[n++] = '\n';
    line[n] = '\0';
    i3DSLogWrite(line, (size_t)n);
    i3DSLinkWrite(line, (size_t)n);
}

int i3DSResumeCount()
{
    return sResumes;
}

void i3DSQuit(int code)
{
    // Another thread cannot stop the main one, nor the threads it would have
    // to join. The kernel ends them all with their memory still mapped.
    if (!OnMainThread())
    {
        u32 self = 0;
        svcGetThreadId(&self, CUR_THREAD_HANDLE);
        i3DSQuitLine("exit(%d) on thread %lu, not the main one; ending the process", code,
                     (unsigned long)self);
        i3DSLogClose();
        svcExitProcess();
    }

    // exit() again, from an atexit handler or a step below.
    if (sQuitting)
    {
        i3DSQuitLine("exit(%d) again during the quit; on to the C library's exit", code);
        __real_exit(code);
    }
    sQuitting = true;

    fflush(stdout);
    fflush(stderr);
    i3DSQuitLine("exit(%d)", code);
    StopEverything();
    Step("the C library's exit (atexit handlers, stdio)");
    __real_exit(code);
}

// libctru calls this from every exit, _exit() included, after the atexit
// handlers and stdio, before it closes its services and frees the heaps.
extern "C" void userAppExit(void)
{
    if (!sStopped)
    {
        // A quit that did not finish (exit again from inside a step): what
        // may still run is ended by the kernel, with the heaps still mapped.
        u32 self = 0;
        svcGetThreadId(&self, CUR_THREAD_HANDLE);
        if (sQuitting || !OnMainThread())
        {
            i3DSQuitLine("libctru's exit before the quit finished (thread %lu); ending the process",
                         (unsigned long)self);
            i3DSLogClose();
            svcExitProcess();
        }
        // _exit() (the abort handler) on the main thread.
        sQuitting = true;
        i3DSQuitLine("_exit without the quit; stopping what runs first");
        StopEverything();
    }
    // A self-starting probe: the next run, before the SD card goes.
    static const char kLast[] = "libctru's exit: the exception handler comes off; the SD card, fs, "
                                "hid, APT and srv close; the heaps are freed; the process ends";
    ChainNext(kLast);
    // A fault from here on goes to Luma3DS's own screen: the handler would
    // need the heap and the SD card, which go next.
    Step(kLast);
    threadOnException(NULL, NULL, NULL);
    i3DSLogClose();
}

void i3DSLinkWrite(const char* data, size_t len)
{
    const int fd = sLinkSocket;
    if (fd < 0 || sLinkDead)
    {
        return;
    }
    // Called from inside whatever printed: errno is left as that found it.
    const int saved = errno;
    // A full send buffer (the PC not keeping up) gets 20 naps of 1 ms, then
    // the rest is dropped; after a drop nothing waits until a write goes
    // through whole again.
    int naps = sLinkDropping ? 0 : 20;
    bool whole = true;
    while (len > 0)
    {
        const ssize_t w = send(fd, data, len, 0);
        if (w > 0)
        {
            data += w;
            len -= (size_t)w;
        }
        else if (w < 0 && WouldBlock() && naps > 0)
        {
            naps--;
            svcSleepThread(1000000ll);
        }
        else
        {
            sLinkDead = w < 0 && !WouldBlock();
            whole = false;
            break;
        }
    }
    sLinkDropping = !whole;
    errno = saved;
}

const char* i3DSAddress()
{
    if (sSocketsUp)
    {
        AskAddress();
    }
    return sAddress;
}

// --------------------------------------------------------- the console probes

// A probe build (TSSM_3DS_PROBE=<n>, CMakeLists.txt; docs/3DS.md) turns on one
// drawing effect not yet shown to work on a console, with everything it needs
// compiled in: the switches, a level to boot, an exit frame and a log of its
// own, so that it runs from the Homebrew Launcher with no test-env.txt and
// whatever config.ini holds. Its saves go to a folder of their own. The bottom
// screen names it.
#ifndef TSSM_3DS_PROBE
#define TSSM_3DS_PROBE 0
#endif

namespace
{
    enum
    {
        CHAIN_NONE,
        CHAIN_EXITLOOP, // starts itself again through Luma3DS's loader, N times
        CHAIN_FPS, // BB02, then GG02, each measured
        CHAIN_LADDER, // one more layer of the program each run, until one is held
        CHAIN_SPEED, // probes 14, 15: one level or spot each run, measured part by part
    };

    struct Probe
    {
        int number; // TSSM_3DS_PROBE's (12 and 13 are not in this build)
        const char* name; // the log's: probe-<name>.log
        const char* tests;
        const char* env; // NAME=VALUE;... over kProbeBase (kChainBase for a chain)
        int chain;
    };

    // Every effect off and named, so neither config.ini's [3ds] effects nor a
    // left-over switch decides; BB02 (shadows, the siren's glow) to the exit
    // frame, about a minute on a console.
    const char kProbeBase[] =
        "TSSM_C3D_FOG=off;TSSM_C3D_RTT=off;TSSM_C3D_SHADOW=off;TSSM_C3D_GLOW=off;"
        "TSSM_C3D_WARP=off;TSSM_C3D_COPY=off;TSSM_C3D_ORTHOZ=off;TSSM_C3D_SELFTEST=off;"
        "TSSM_3DS_GUI=off;TSSM_BOOT=BB02;TSSM_INTRO_MOVIES=off;TSSM_EXIT_AFTER=1500;"
        "TSSM_SAVE_DIR=sdmc:/3ds/tssm/probe-saves";

    // Most telling first.
    const Probe kProbes[] = {
        { 1, "1-rtt", "drawing into textures, and its start-up self-test",
          "TSSM_C3D_RTT=on;TSSM_C3D_SELFTEST=on" },
        { 2, "2-overlay", "the settings overlay on the bottom screen",
          "TSSM_3DS_GUI=on;TSSM_GUI_OPEN_AT=300;TSSM_3DS_GUI_ALWAYS=1" },
        // No self-test: its fog step draws into a texture target (probe 1's
        // mechanism); BB02's own fog tests the fog alone.
        { 3, "3-fog", "the fog in BB02", "TSSM_C3D_FOG=on" },
        { 4, "4-shadow", "the shadows (drawing into a texture with depth)",
          "TSSM_C3D_RTT=on;TSSM_C3D_SHADOW=on" },
        { 5, "5-copy", "screen copies, and their start-up self-test",
          "TSSM_C3D_COPY=on;TSSM_C3D_SELFTEST=on" },
        { 6, "6-glow", "the glow (textures and screen copies)",
          "TSSM_C3D_RTT=on;TSSM_C3D_COPY=on;TSSM_C3D_GLOW=on" },
        { 7, "7-warp", "the heat warp (screen copies)",
          "TSSM_C3D_COPY=on;TSSM_C3D_WARP=on;TSSM_TEST_WARP=1000:shockwave:8:1:5" },
        { 8, "8-orthoz", "the depth of the HUD's and menus' 3D models (orthoz)",
          "TSSM_C3D_ORTHOZ=on" },
        { 9, "9-exitloop", "that quitting gives the console its memory back, 20 times in a row", "",
          CHAIN_EXITLOOP },
        { 10, "10-fps", "the frame rate with every effect on, in BB02 and in GG02", "", CHAIN_FPS },
        { 11, "11-leakladder", "which part of the program keeps its process after it quits", "",
          CHAIN_LADDER },
        { 14, "14-speed", "where the time of a frame goes, level by level, on the CPU and the GPU",
          "", CHAIN_SPEED },
        // Probe 14's survey again, after speed2's fixes, and three runs that
        // switch each fix off and on at the same spot.
        { 15, "15-speed", "where the time of a frame goes now, and what each speed fix gave", "",
          CHAIN_SPEED },
        // Probe 15's runs again after speed3's changes, and three runs in 3D
        // that switch each of them off and on at the same still spot.
        { 16, "16-speed", "probe 15's survey again, and what each 3D speed change gives", "",
          CHAIN_SPEED },
    };
    const int kNumProbes = (int)(sizeof(kProbes) / sizeof(kProbes[0]));

    // By number, as the numbers have a gap (12 and 13).
    const Probe* ThisProbe()
    {
        for (int i = 0; i < kNumProbes; i++)
        {
            if (kProbes[i].number == TSSM_3DS_PROBE)
            {
                return &kProbes[i];
            }
        }
        return NULL;
    }

    // ------------------------------------------------- the self-starting probes

    // A chain probe quits through the game's normal quit and has Luma3DS's
    // loader start it again (its hb:ldr port, as the Homebrew Launcher uses
    // it: the next .3dsx to load, once), so that one launch from the
    // Homebrew Launcher is many runs. Each run's settings come from the run
    // number, kept on the SD card. Nothing of the player's is touched: its
    // own config.ini, saves and logs, under sdmc:/3ds/tssm/<probe>/.
    // Defaults otherwise, so every effect is on. The overlay is on in the
    // frame-rate probe, closed, as the player has it; the exit loop keeps
    // it off so the bottom screen goes on showing the run number.
    const char kChainBase[] = "TSSM_INTRO_MOVIES=off";
    const char kExitLoopBase[] = "TSSM_3DS_GUI=off";
    // The run after a chained start has this as its second argument; a start
    // from the Homebrew Launcher has none and begins at run 1, or carries on
    // a search the last power-on stopped (state.txt's resume).
    const char kChainArg[] = "tssm-chain-next";
    const int kExitLoopRuns = 20;
    // However many power-ons a search takes.
    const int kExitLoopMaxRuns = 26;

    // The dialogs at a level's start, as in the measuring runs (BB02).
    const char kIntroKeys[] =
        "90:a/6,135:a/6,180:b/6,210:b/6,240:b/6,270:b/6,300:b/6,330:b/6,360:b/6";

    struct Chain
    {
        int run; // this run, from 1
        int base; // programs held before this power-on's first run (not this launch's)
        int heldPrev; // this launch's runs held at the start of the run before
        int held; // ... at the start of this run
        bool last; // this run starts no other
        bool resumed; // the first run of a search carried on from the last power-on
        bool resume; // this run ends a power-on's part of the search, not the search
        bool finished; // the exit loop ended with a verdict or its last run
        bool nothing; // a start after the loop finished: nothing left to do
        char cfg[16]; // this run's settings' name
        char mode[16]; // where the exit loop is: normal, bisect1, bisect2, unfixed
        char note[200]; // what the run before showed, for the log
        char dir[64]; // sdmc:/3ds/tssm/<probe>
        bool fresh;
        bool only; // probe 14: only-run.txt's one run (an emulator's check)
    };
    Chain sChain;

    extern "C" int __system_argc;
    extern "C" char** __system_argv;

    const char* ChainDirName()
    {
        return ThisProbe()->chain == CHAIN_FPS    ? "fps-effects" :
               ThisProbe()->chain == CHAIN_LADDER ? "leakladder" :
               ThisProbe()->chain == CHAIN_SPEED  ? (TSSM_3DS_PROBE == 16   ? "speed16" :
                                                     TSSM_3DS_PROBE == 15 ? "speed15" :
                                                                            "speed14") :
                                                    "exitloop";
    }

    // The summary log: sdmc:/3ds/tssm/exitloop.log or fps-effects.log,
    // opened and closed for every line, so a later crash cannot lose one.
    void ChainLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

    void ChainLog(const char* fmt, ...)
    {
        char path[96];
        snprintf(path, sizeof(path), "%s/%s.log", k3DSRoot, ChainDirName());
        FILE* f = fopen(path, "a");
        if (f == NULL)
        {
            return;
        }
        va_list ap;
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fputc('\n', f);
        fclose(f);
    }

    // What the last run wrote: run, base, held, its settings (into note for
    // now), mode, and whether the search goes on at the next power-on.
    struct ChainState
    {
        int run;
        int base;
        int held;
        int resume;
        int finished;
        char cfg[16];
        char mode[16];
    };

    bool ChainLoad(ChainState* st)
    {
        memset(st, 0, sizeof(*st));
        char path[96];
        snprintf(path, sizeof(path), "%s/state.txt", sChain.dir);
        FILE* f = fopen(path, "r");
        if (f == NULL)
        {
            return false;
        }
        char line[128];
        while (fgets(line, sizeof(line), f) != NULL)
        {
            char* eq = strchr(line, '=');
            if (eq == NULL)
            {
                continue;
            }
            *eq = '\0';
            char* v = eq + 1;
            v[strcspn(v, "\r\n")] = '\0';
            if (strcmp(line, "run") == 0)
            {
                st->run = atoi(v);
            }
            else if (strcmp(line, "base") == 0)
            {
                st->base = atoi(v);
            }
            else if (strcmp(line, "held") == 0)
            {
                st->held = atoi(v);
            }
            else if (strcmp(line, "resume") == 0)
            {
                st->resume = atoi(v);
            }
            else if (strcmp(line, "finished") == 0)
            {
                st->finished = atoi(v);
            }
            else if (strcmp(line, "cfg") == 0)
            {
                snprintf(st->cfg, sizeof(st->cfg), "%s", v);
            }
            else if (strcmp(line, "mode") == 0)
            {
                snprintf(st->mode, sizeof(st->mode), "%s", v);
            }
        }
        fclose(f);
        return true;
    }

    void ChainSave()
    {
        char path[96];
        snprintf(path, sizeof(path), "%s/state.txt", sChain.dir);
        FILE* f = fopen(path, "w");
        if (f == NULL)
        {
            return;
        }
        fprintf(f, "run=%d\nbase=%d\nheld=%d\ncfg=%s\nmode=%s\nresume=%d\nfinished=%d\n",
                sChain.run, sChain.base, sChain.held, sChain.cfg, sChain.mode,
                sChain.resume ? 1 : 0, sChain.finished ? 1 : 0);
        fclose(f);
    }

    // The settings of each of the exit loop's configurations.
    const char* ChainEnv(const char* cfg)
    {
        if (strcmp(cfg, "fixed-soc") == 0)
        {
            return "TSSM_3DS_LOG_SERVER=on";
        }
        if (strcmp(cfg, "svccache") == 0)
        {
            return "TSSM_3DS_OWNCACHE=off";
        }
        if (strcmp(cfg, "noaudio") == 0)
        {
            return "TSSM_AUDIO=0";
        }
        if (strcmp(cfg, "dummyaudio") == 0)
        {
            return "SDL_AUDIO_DRIVER=dummy";
        }
        if (strcmp(cfg, "unfixed") == 0)
        {
            return "TSSM_3DS_EXITFIX=off;TSSM_3DS_LOG_SERVER=on";
        }
        if (strcmp(cfg, "bb02") == 0)
        {
            return "TSSM_BOOT=BB02";
        }
        if (strcmp(cfg, "gg02") == 0)
        {
            return "TSSM_BOOT=GG02";
        }
        return ""; // fixed: the game as the player has it
    }

    // What the run before showed, and what this run is. The exit loop's
    // plan: 10 runs of the game as the player has it, 4 with the log server
    // (the sockets), 3 with the services doing the cache flushes (the exit
    // fix without its cache part), then the old quit for the comparison.
    // A fixed run whose process stayed behind turns it into a search: no
    // audio and no sockets, then SDL's silent audio (its thread on core 1
    // and the 30% limit, without the DSP). A run that starts with one held
    // is the last of its power-on (ChainPlan), so a search the leak
    // interrupts goes on at the next one: the first run there is the run
    // that was planned, and the run after it tells.
    void ChainPlanExitLoop()
    {
        const bool leaked = !sChain.resumed && sChain.run > 1 && sChain.held > sChain.heldPrev;
        char prev[16];
        snprintf(prev, sizeof(prev), "%s", sChain.note);
        sChain.note[0] = '\0';
        if (sChain.resumed)
        {
            // cfg and mode are the planned run's, from state.txt.
            snprintf(sChain.note, sizeof(sChain.note),
                     "carries on the search the last power-on stopped (its run %d, %s, went unread)",
                     sChain.run, sChain.cfg);
            return;
        }
        if (sChain.run > 1)
        {
            snprintf(sChain.note, sizeof(sChain.note), "run %d (%s): its process %s", sChain.run - 1,
                     prev, leaked ? "STAYED BEHIND (held)" : "was freed");
        }
        const char* verdict = NULL;
        if (sChain.mode[0] == '\0')
        {
            snprintf(sChain.mode, sizeof(sChain.mode), "normal");
        }
        if (leaked && strcmp(sChain.mode, "normal") == 0)
        {
            if (strcmp(prev, "fixed-soc") == 0)
            {
                verdict = "the fixed game frees its process, but not with the log server on: the "
                          "sockets (SOC) hold it";
            }
            else if (strcmp(prev, "svccache") == 0)
            {
                verdict = "the fixed game frees its process, but not when the services do its "
                          "cache flushes (TSSM_3DS_OWNCACHE=off): the cache part of the exit fix "
                          "is the part that matters (a process handle kept by the DSP or GSP "
                          "service)";
            }
            else
            {
                snprintf(sChain.mode, sizeof(sChain.mode), "bisect1");
            }
        }
        else if (strcmp(sChain.mode, "bisect1") == 0 && strcmp(prev, "noaudio") == 0)
        {
            if (leaked)
            {
                verdict = "held even with no audio and no sockets: the graphics side (GSP, gfx, "
                          "citro3d) or something outside the game";
            }
            else
            {
                snprintf(sChain.mode, sizeof(sChain.mode), "bisect2");
            }
        }
        else if (strcmp(sChain.mode, "bisect2") == 0 && strcmp(prev, "dummyaudio") == 0)
        {
            verdict = leaked ? "held with SDL's silent audio: its thread on core 1 or the 30% "
                               "time limit"
                             : "freed with SDL's silent audio, held with ndsp: the DSP holds it";
        }
        else if (strcmp(sChain.mode, "unfixed") == 0 && strcmp(prev, "unfixed") == 0 && leaked)
        {
            verdict = "the old quit leaves its process behind; the fixed quit did not, with or "
                      "without its own cache flushes: the exit fix works, through its other "
                      "steps (the cache part is not needed)";
        }

        if (strcmp(sChain.mode, "normal") == 0)
        {
            const char* c = sChain.run <= 10   ? "fixed"
                            : sChain.run <= 14 ? "fixed-soc"
                            : sChain.run <= 17 ? "svccache"
                                               : "unfixed";
            if (sChain.run >= 18)
            {
                snprintf(sChain.mode, sizeof(sChain.mode), "unfixed");
            }
            snprintf(sChain.cfg, sizeof(sChain.cfg), "%s", c);
        }
        else if (strcmp(sChain.mode, "unfixed") == 0)
        {
            snprintf(sChain.cfg, sizeof(sChain.cfg), "unfixed");
        }
        else if (strcmp(sChain.mode, "bisect1") == 0)
        {
            snprintf(sChain.cfg, sizeof(sChain.cfg), "noaudio");
        }
        else
        {
            snprintf(sChain.cfg, sizeof(sChain.cfg), "dummyaudio");
        }

        if (verdict == NULL && sChain.run >= kExitLoopRuns &&
            (strcmp(sChain.mode, "normal") == 0 || strcmp(sChain.mode, "unfixed") == 0))
        {
            // Every run before this one was freed, the old quit (from run
            // 18) included.
            verdict = "nothing was held in this loop, the old quit included: the loop, which goes "
                      "from one run straight to the next without the Homebrew Launcher, does not "
                      "show the leak, so it cannot tell whether the exit fix works (the start-up "
                      "held line of launches from the Homebrew Launcher can)";
        }
        else if (verdict == NULL && sChain.run >= kExitLoopMaxRuns)
        {
            verdict = "no verdict within the runs allowed; the runs' lines above are all there is";
        }
        if (verdict != NULL)
        {
            ChainLog("RESULT: %s.", verdict);
            sChain.last = true;
            sChain.finished = true;
            snprintf(sChain.cfg, sizeof(sChain.cfg), "fixed");
        }
    }

    // ----------------------------------------------- probe 14, the speed survey

    // One level or spot a run, every effect on as the player has it, each run
    // a row of segments TSSM_C3D_AB switches between (c3dab.cpp): the same
    // spot plain, with the parts of each draw's recording timed (profile),
    // with the GPU's time per pass (gpusplit), with nothing filled (fill=off),
    // and, in BB02, with one effect at a time left out. TSSM_FIXED_STEP=30
    // holds the game's clock to the frames, so every segment of a spot sees
    // the same picture however fast the console draws it. One summary,
    // speed14.log, a block of lines a segment (docs/3DS.md, probe 14).

    // Every spot gains three segments: 2D, both eyes forced (stereo=force:
    // force:50, no guard) and with the speed guard (stereo=guard), through
    // c3dstereo.cpp's stereoProbeSet. 0 leaves them out.
#ifndef TSSM_SPEED14_STEREO
#define TSSM_SPEED14_STEREO 1
#endif

    enum
    {
        SPEED_INPUT_NONE, // no presses: the opening fly-through, the title screen
        SPEED_INPUT_BB02, // the dialogs; the A/B measuring run's walk to the calm spot
        SPEED_INPUT_D0, // the speed test's script, version 4
        SPEED_INPUT_PAUSE // the dialogs, then START
    };

    struct SpeedSeg
    {
        const char* set; // over kSpeedReset
        const char* label; // no comma: TSSM_C3D_AB's separator; under 40 with a spot
    };

    struct SpeedRun
    {
        const char* cfg; // state.txt's name for it (16 at most)
        const char* what; // the bottom screen's
        const char* boot; // TSSM_BOOT ("" the title screen)
        int input;
        int first; // the first switch
        int segLen; // frames
        const SpeedSeg* a; // the first spot
        int na;
        const SpeedSeg* b; // BB02's calm spot, after the walk (nil: none)
        int nb;
        const char* extra; // NAME=VALUE;... for this run
    };

    // Each segment's settings start from these: every effect on (a player's
    // [3ds] effects = all), no measuring word, speed2's fixes on (an A/B
    // word holds until a segment sets it again).
    const char kSpeedReset[] = "glow=on+warp=on+fog=on+shadow=on+rtt=on+copy=on+env=on+"
                               "profile=off+gpusplit=off+fill=on+flush=heap+statsline=on+"
                               "stereo=off+refresh=on+fast=on+pipe=on+eyeskip=on+alphafill=off+skintrim=on+glowclear=off+eyetest=normal+savevram=on";

    // BB02's busy spot (about 440 draws, held still from frame 370 while
    // nothing is pressed): plain at both ends for the drift, the
    // measuring words, one effect at a time left out, the L2 flush, and the
    // stats line's own SD write left out.
    const SpeedSeg kSpeedBusy[] = {
        { "", "busy: all on (first)" },
        { "profile=on", "busy: profile" },
        { "gpusplit=on", "busy: gpusplit" },
        { "fill=off", "busy: fill=off" },
        { "glow=off", "busy: glow off" },
        { "fog=off", "busy: fog off" },
        { "shadow=off", "busy: shadow off" },
        { "env=off", "busy: env maps off" },
        { "flush=cmdlist", "busy: L2 flush=cmdlist" },
        { "statsline=off", "busy: stats line off" },
        { "", "busy: all on (last)" },
    };
    // The calm spot (about 55 draws), after the walk.
    const SpeedSeg kSpeedCalm[] = {
        { "", "calm: all on (first)" }, { "profile=on", "calm: profile" },
        { "gpusplit=on", "calm: gpusplit" }, { "fill=off", "calm: fill=off" },
        { "", "calm: all on (last)" },
    };
    // Run 2's busy spot with the models' vertices in VRAM.
    const SpeedSeg kSpeedVb[] = {
        { "", "busy vbvram: all on" },
        { "gpusplit=on", "busy vbvram: gpusplit" },
        { "fill=off", "busy vbvram: fill=off" },
        { "profile=on", "busy vbvram: profile" },
    };
    // A level played by the speed test's script, or a stretch of a cutscene: plain and
    // profile in turn, one gpusplit.
    const SpeedSeg kSpeedLevel[] = {
        { "", "all on" },       { "profile=on", "profile" }, { "", "all on" },
        { "gpusplit=on", "gpusplit" }, { "", "all on" }, { "profile=on", "profile" },
        { "", "all on" },       { "profile=on", "profile" }, { "", "all on" },
    };
    // BB02's busy spot without sound: audio's cost on the game's core by
    // the difference with run 1.
    const SpeedSeg kSpeedQuiet[] = {
        { "", "busy without audio: all on" },     { "profile=on", "busy without audio: profile" },
        { "", "busy without audio: all on" },     { "gpusplit=on", "busy without audio: gpusplit" },
        { "", "busy without audio: all on" },     { "profile=on", "busy without audio: profile" },
        { "", "busy without audio: all on" },
    };
    const SpeedSeg kSpeedShort[] = {
        { "", "all on" },
        { "profile=on", "profile" },
        { "", "all on" },
    };
    // Probe 15: each speed2 fix off, then on again, at the same spot. pipe=off
    // waits for the frame before (no L4), fast=off leaves L3's caches out,
    // refresh=off instances a model whose vertices changed from the start.
    const SpeedSeg kSpeedAbBusy[] = {
        { "", "busy ab: all on" },
        { "pipe=off", "busy ab: pipe off" },
        { "", "busy ab: all on" },
        { "fast=off", "busy ab: fast off" },
        { "", "busy ab: all on" },
        { "refresh=off", "busy ab: refresh off" },
        { "", "busy ab: all on" },
        { "stereo=force+pipe=off", "busy ab: force:50 pipe off" },
        { "stereo=force", "busy ab: force:50 pipe on" },
        { "profile=on", "busy ab: profile" },
    };
    const SpeedSeg kSpeedAbFly[] = {
        { "", "all on" },         { "refresh=off", "refresh off" }, { "", "all on" },
        { "pipe=off", "pipe off" }, { "", "all on" },             { "refresh=off", "refresh off" },
        { "", "all on" },
    };
    const SpeedSeg kSpeedAbLevel[] = {
        { "", "all on" },
        { "pipe=off", "pipe off" },
        { "", "all on" },
        { "fast=off", "fast off" },
        { "", "all on" },
        { "stereo=force+pipe=off", "force:50 pipe off" },
        { "stereo=force", "force:50 pipe on" },
    };
    // Probe 16: in 3D (force:50), each speed3 change off (alphafill, off by
    // default, on), "all on" between; eyetest's words; 2D without and with
    // the eye code (eyetest=2d); then all of speed3 off at once.
    const SpeedSeg kSpeedAb3[] = {
        { "stereo=force", "3d ab: all on" },
        { "stereo=force+eyeskip=off", "3d ab: eyeskip off" },
        { "stereo=force", "3d ab: all on" },
        { "stereo=force+skintrim=off", "3d ab: skintrim off" },
        { "stereo=force", "3d ab: all on" },
        { "stereo=force+glowclear=on", "3d ab: glowclear on" },
        { "stereo=force", "3d ab: all on" },
        { "stereo=force+savevram=off", "3d ab: savevram off" },
        { "stereo=force", "3d ab: all on" },
        { "stereo=force+alphafill=on", "3d ab: alphafill on" },
        { "stereo=force+eyetest=noshift", "3d ab: eyetest noshift" },
        { "stereo=force+eyetest=noright", "3d ab: eyetest noright" },
        { "stereo=force+eyetest=nosave", "3d ab: eyetest nosave" },
        { "stereo=force", "3d ab: all on" },
        { "", "3d ab: 2d" },
        { "eyetest=2d", "3d ab: 2d eyetest 2d" },
        { "stereo=force+eyeskip=off+skintrim=off+glowclear=on+savevram=off",
          "3d ab: speed3 all off" },
        { "stereo=force", "3d ab: all on" },
        { "stereo=force+profile=on", "3d ab: profile" },
    };
#define SPEED_N(a) (int)(sizeof(a) / sizeof(a[0]))
#if TSSM_3DS_PROBE == 16
#define PROBE_SPEED_MINUTES "50"
#elif TSSM_3DS_PROBE == 15
#define PROBE_SPEED_MINUTES "35"
#else
#define PROBE_SPEED_MINUTES "25"
#endif

    const SpeedRun kSpeedRuns[] = {
        { "bb02", "BB02: the busy spot, then the calm one", "BB02", SPEED_INPUT_BB02, 390, 120,
          kSpeedBusy, SPEED_N(kSpeedBusy), kSpeedCalm, SPEED_N(kSpeedCalm), "" },
        { "bb02-vbvram", "BB02 with the models in VRAM", "BB02", SPEED_INPUT_BB02, 390, 120,
          kSpeedVb, SPEED_N(kSpeedVb), NULL, 0, "TSSM_C3D_VBVRAM=on" },
        { "gg02", "GG02", "GG02", SPEED_INPUT_D0, 400, 150, kSpeedLevel, SPEED_N(kSpeedLevel),
          NULL, 0, "" },
        { "tr01", "TR01", "TR01", SPEED_INPUT_D0, 400, 150, kSpeedLevel, SPEED_N(kSpeedLevel),
          NULL, 0, "" },
        { "jk01", "JK01", "JK01", SPEED_INPUT_D0, 400, 150, kSpeedLevel, SPEED_N(kSpeedLevel),
          NULL, 0, "" },
        { "bb01", "BB01", "BB01", SPEED_INPUT_D0, 400, 150, kSpeedLevel, SPEED_N(kSpeedLevel),
          NULL, 0, "" },
        { "bb02-fly", "BB02's opening fly-through", "BB02", SPEED_INPUT_NONE, 100, 150,
          kSpeedLevel, SPEED_N(kSpeedLevel), NULL, 0, "" },
        { "bb02-noaudio", "BB02 without sound", "BB02", SPEED_INPUT_BB02, 390, 120, kSpeedQuiet,
          SPEED_N(kSpeedQuiet), NULL, 0, "TSSM_AUDIO=0" },
        { "title", "The title screen, and the draw benchmark", "", SPEED_INPUT_NONE, 120, 150,
          kSpeedShort, SPEED_N(kSpeedShort), NULL, 0, "TSSM_C3D_BENCH=2" },
        { "bb02-pause", "BB02's pause menu", "BB02", SPEED_INPUT_PAUSE, 450, 150, kSpeedShort,
          SPEED_N(kSpeedShort), NULL, 0, "" },
#if TSSM_3DS_PROBE >= 15
        // The mixer's own timing (TSSM_AUDIO_PROF) goes to the run's log.
        { "bb02-ab", "BB02: each speed fix off, then on", "BB02", SPEED_INPUT_BB02, 390, 120,
          kSpeedAbBusy, SPEED_N(kSpeedAbBusy), NULL, 0, "TSSM_AUDIO_PROF=1" },
        { "bb02-fly-ab", "BB02's fly-through: each fix off, then on", "BB02", SPEED_INPUT_NONE,
          100, 150, kSpeedAbFly, SPEED_N(kSpeedAbFly), NULL, 0, "" },
        { "gg02-ab", "GG02: each speed fix off, then on", "GG02", SPEED_INPUT_D0, 400, 150,
          kSpeedAbLevel, SPEED_N(kSpeedAbLevel), NULL, 0, "TSSM_AUDIO_PROF=1" },
#endif
#if TSSM_3DS_PROBE == 16
        // GG02 and BB01 from frame 1800: the speed test's script has ended (1761), the
        // player stands where probe 15's 3D pair measured them.
        { "bb02-ab3", "BB02 in 3D: each new speed change off, then on", "BB02", SPEED_INPUT_BB02,
          390, 120, kSpeedAb3, SPEED_N(kSpeedAb3), NULL, 0, "" },
        { "gg02-ab3", "GG02 in 3D: each new speed change off, then on", "GG02", SPEED_INPUT_D0,
          1800, 150, kSpeedAb3, SPEED_N(kSpeedAb3), NULL, 0, "" },
        { "bb01-ab3", "BB01 in 3D: each new speed change off, then on", "BB01", SPEED_INPUT_D0,
          1800, 150, kSpeedAb3, SPEED_N(kSpeedAb3), NULL, 0, "" },
#endif
    };
    const int kNumSpeedRuns = SPEED_N(kSpeedRuns);

    // The dialogs at BB02's start, then (from `walk`) the A/B measuring run's walk
    // from the busy spot to the calm one: as its script, from its frame 1260.
    const char kSpeedWalk[][16] = { "0:up/120",  "60:a/9",   "150:right/60", "225:b/6",
                                    "270:up/120", "300:a/9", "360:a/9",      "420:left/90" };
    // The walk takes 510 frames; the calm spot is still from about 520 on.
    const int kSpeedWalkToCalm = 530;
    const char kSpeedD0[] =
        "90:a/6,135:a/6,180:b/6,210:b/6,240:b/6,270:b/6,300:b/6,330:b/6,360:b/6,420:up/120,"
        "480:a/9,570:right/60,645:b/6,690:up/120,720:a/9,780:a/9,840:left/90,945:b/6,990:x/6,"
        "1020:x/6,1050:down/90,1155:up/180,1200:a/9,1260:a/9,1344:b/6,1380:right/150,1440:a/9,"
        "1545:b/6,1590:up/150,1650:a/9,1755:b/6";

    void SpeedAdd(char* ab, size_t size, size_t* used, int frame, const char* set,
                  const char* label)
    {
        if (*used >= size)
        {
            return;
        }
        *used += (size_t)snprintf(ab + *used, size - *used, "%s%d:%s%s%s#%s",
                                  *used > 0 ? "," : "", frame, kSpeedReset,
                                  set[0] != '\0' ? "+" : "", set, label);
    }

    // A spot's segments from `frame`, and with the stereo code the three
    // that compare 2D and 3D there; the frame after the last.
    int SpeedBlock(char* ab, size_t size, size_t* used, int frame, int segLen,
                   const SpeedSeg* segs, int n, const char* spot)
    {
        for (int i = 0; i < n; i++)
        {
            SpeedAdd(ab, size, used, frame, segs[i].set, segs[i].label);
            frame += segLen;
        }
        if (TSSM_SPEED14_STEREO)
        {
            static const char* kSet[3] = { "stereo=off", "stereo=force", "stereo=guard" };
            static const char* kWhat[3] = { "3D pair: 2D", "3D pair: force:50",
                                            "3D pair: force:50 with the guard" };
            for (int i = 0; i < 3; i++)
            {
                char label[40];
                snprintf(label, sizeof(label), "%s%s%s", spot, spot[0] != '\0' ? ": " : "",
                         kWhat[i]);
                SpeedAdd(ab, size, used, frame, kSet[i], label);
                frame += segLen;
            }
        }
        return frame;
    }

    // The run's TSSM_C3D_AB and TSSM_INPUT, and its last frame.
    int SpeedSchedule(const SpeedRun* r, char* ab, size_t abSize, char* in, size_t inSize)
    {
        size_t used = 0;
        ab[0] = '\0';
        in[0] = '\0';
        int frame = SpeedBlock(ab, abSize, &used, r->first, r->segLen, r->a, r->na,
                               r->b != NULL ? "busy" : "");
        int walk = 0;
        if (r->b != NULL)
        {
            // The walk between the spots is a segment of its own, so the
            // last one at the busy spot ends where the walk starts.
            walk = frame;
            SpeedAdd(ab, abSize, &used, walk, "", "the walk to the calm spot (no spot)");
            frame = SpeedBlock(ab, abSize, &used, walk + kSpeedWalkToCalm, r->segLen, r->b,
                               r->nb, "calm");
        }
        if (r->input == SPEED_INPUT_D0)
        {
            snprintf(in, inSize, "%s", kSpeedD0);
        }
        else if (r->input == SPEED_INPUT_BB02 || r->input == SPEED_INPUT_PAUSE)
        {
            size_t n = (size_t)snprintf(in, inSize, "%s", kIntroKeys);
            if (r->input == SPEED_INPUT_PAUSE && n < inSize)
            {
                n += (size_t)snprintf(in + n, inSize - n, ",420:start/6");
            }
            for (int i = 0; walk > 0 && i < SPEED_N(kSpeedWalk) && n < inSize; i++)
            {
                const int at = atoi(kSpeedWalk[i]);
                n += (size_t)snprintf(in + n, inSize - n, ",%d%s", walk + at,
                                      strchr(kSpeedWalk[i], ':'));
            }
        }
        return frame;
    }

    // The bottom screen's and the log's name of a run.
    const SpeedRun* SpeedThisRun()
    {
        const int i = sChain.run >= 1 && sChain.run <= kNumSpeedRuns ? sChain.run - 1 : 0;
        return &kSpeedRuns[i];
    }

    // A start from the Homebrew Launcher with speed14/only-run.txt (speed15/,
    // speed16/ for probes 15, 16) holding a run number runs that one alone and stops: for checks in an emulator,
    // which has no Luma3DS loader to chain with. The console's card has none.
    void SpeedOnlyRun()
    {
        char path[96];
        snprintf(path, sizeof(path), "%s/only-run.txt", sChain.dir);
        FILE* f = fopen(path, "r");
        if (f == NULL)
        {
            return;
        }
        char line[16] = { 0 };
        const bool read = fgets(line, sizeof(line), f) != NULL;
        fclose(f);
        const int n = read ? atoi(line) : 0;
        if (n >= 1 && n <= kNumSpeedRuns)
        {
            sChain.run = n;
            sChain.only = true;
        }
    }

    void SpeedPlan()
    {
        const SpeedRun* r = SpeedThisRun();
        snprintf(sChain.cfg, sizeof(sChain.cfg), "%s", r->cfg);
        sChain.last = sChain.only || sChain.run >= kNumSpeedRuns;
        if (sChain.only)
        {
            snprintf(sChain.note, sizeof(sChain.note),
                     "only this run (%s/only-run.txt, for an emulator's check)",
                     ChainDirName());
        }
    }

    // In i3DSEarly, before the log opens: which run this is and its settings.
    void ChainPlan()
    {
        const Probe* p = ThisProbe();
        // The leak ladder plans its run before everything else (LadderStart).
        if (p == NULL || p->chain == CHAIN_NONE || p->chain == CHAIN_LADDER)
        {
            return;
        }
        snprintf(sChain.dir, sizeof(sChain.dir), "%s/%s", k3DSRoot, ChainDirName());
        mkdir(k3DSDataParent, 0777);
        mkdir(k3DSRoot, 0777);
        mkdir(sChain.dir, 0777);
        sChain.run = 1;
        sChain.fresh = !(__system_argc >= 2 && __system_argv != NULL &&
                         __system_argv[1] != NULL && strcmp(__system_argv[1], kChainArg) == 0);
        ChainState st;
        const bool haveState = ChainLoad(&st);
        if (!haveState)
        {
            // A chained start with its state gone begins again.
            sChain.fresh = true;
        }
        if (!sChain.fresh && haveState)
        {
            sChain.run = st.run + 1;
            sChain.base = st.base;
            sChain.heldPrev = st.held;
            snprintf(sChain.note, sizeof(sChain.note), "%s", st.cfg); // the last run's, for now
            snprintf(sChain.mode, sizeof(sChain.mode), "%s", st.mode);
        }
        else if (sChain.fresh && haveState && p->chain == CHAIN_EXITLOOP)
        {
            if (st.resume != 0)
            {
                // The run the last power-on planned and could not read.
                sChain.resumed = true;
                sChain.run = st.run;
                snprintf(sChain.cfg, sizeof(sChain.cfg), "%s", st.cfg);
                snprintf(sChain.mode, sizeof(sChain.mode), "%s", st.mode);
            }
            else if (st.finished != 0)
            {
                // A short title-screen run that says so; state.txt stays,
                // and deleting it starts the loop afresh.
                sChain.nothing = true;
                sChain.finished = true;
                sChain.last = true;
                sChain.run = st.run;
                snprintf(sChain.cfg, sizeof(sChain.cfg), "fixed");
                snprintf(sChain.mode, sizeof(sChain.mode), "%s", st.mode);
            }
        }
        if (p->chain == CHAIN_SPEED && sChain.fresh)
        {
            SpeedOnlyRun();
        }
        Held h;
        MeasureHeld(&h);
        int now = HeldCount(&h);
        // The program before (the run before, or the Homebrew Launcher, a
        // 3dsx_app too) may still be on its way out: pm closes its handle to
        // it after telling the system it ended. Counted again for up to 3 s
        // before it counts as held.
        const int expect = sChain.fresh ? 0 : sChain.base + sChain.heldPrev;
        for (int i = 0; i < 6 && h.sameName >= 0 && now > expect; i++)
        {
            svcSleepThread(500000000ll);
            CountProcesses(&h);
            now = HeldCount(&h) < now ? HeldCount(&h) : now;
        }
        if (sChain.fresh)
        {
            sChain.base = now;
            sChain.heldPrev = 0;
            time_t t = time(NULL);
            struct tm tm;
            memset(&tm, 0, sizeof(tm));
            localtime_r(&t, &tm);
            ChainLog("\n==== %04d-%02d-%02d %02d:%02d:%02d  %s (built %s %s): started from the "
                     "Homebrew Launcher%s",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                     p->name, __DATE__, __TIME__,
                     sChain.resumed   ? "; carries on the last power-on's search"
                     : sChain.nothing ? "; the loop finished before, nothing left to do"
                                      : "");
        }
        sChain.held = now - sChain.base;
        if (sChain.held < 0)
        {
            sChain.held = 0;
        }

        if (sChain.nothing)
        {
            snprintf(sChain.note, sizeof(sChain.note),
                     "probe 9 has finished (state.txt says so; delete it to run the loop again)");
        }
        else if (p->chain == CHAIN_EXITLOOP)
        {
            ChainPlanExitLoop();
        }
        else
        {
            snprintf(sChain.note, sizeof(sChain.note), "%s",
                     sChain.run > 1 ? (sChain.held > sChain.heldPrev ? "the run before: its process "
                                                                       "STAYED BEHIND (held)"
                                                                     : "the run before: its process "
                                                                       "was freed")
                                    : "");
            if (p->chain == CHAIN_SPEED)
            {
                SpeedPlan();
            }
            else
            {
                snprintf(sChain.cfg, sizeof(sChain.cfg), "%s", sChain.run == 1 ? "bb02" : "gg02");
                sChain.last = sChain.run >= 2;
            }
        }
        // Seen on the console: the third or the fourth held program in one
        // power-on panicked the kernel at the next start. A run that starts
        // with one held ends the chain, so the Homebrew Launcher starts with
        // two at most; an exit-loop search it cuts short goes on at the
        // next power-on (resume).
        const int total = sChain.base + sChain.held;
        if (total >= 1 && !sChain.last)
        {
            sChain.last = true;
            if (p->chain == CHAIN_EXITLOOP)
            {
                sChain.resume = true;
                ChainLog("STOP: %d earlier program(s) are held at this start; this run is the last "
                         "of this power-on. Restart the console (hold POWER), then start the probe "
                         "once more: it carries on with this run (%d, %s).",
                         total, sChain.run, sChain.cfg);
            }
            else
            {
                ChainLog("STOP: %d earlier program(s) are held at this start; this run is the "
                         "last. Restart the console (hold POWER) before anything else.",
                         total);
            }
        }
        if (sChain.fresh && sChain.base >= 1)
        {
            ChainLog("NOTE: %d earlier program(s) were already held before this launch "
                     "(something ran since the console was switched on); best run this probe "
                     "right after switching the console on.",
                     sChain.base);
        }
        ChainSave();

        char line[600];
        snprintf(line, sizeof(line),
                 "run %d%s, %s; %lu s since power-on; %d process(es) on the console, %d of them "
                 "named as this one besides it; not free at start %lu KB (this program %lu KB); "
                 "held by earlier programs: %d (%d of them this launch's runs)%s%s",
                 sChain.run,
                 sChain.nothing  ? " (nothing to do)"
                 : sChain.resume ? " (the last of this power-on)"
                 : sChain.last   ? " (the last)"
                                 : "",
                 sChain.cfg, (unsigned long)SecondsSincePowerOn(), h.processes, h.sameName,
                 (unsigned long)h.notFreeKB, (unsigned long)h.ownKB, sChain.base + sChain.held,
                 sChain.held, sChain.note[0] != '\0' ? "; " : "", sChain.note);
        ChainLog("%s", line);
    }

    void SetPairs(const char* list);

    // Each run: its settings, the log of its own, a short play.
    void ApplyChain(const Probe* p)
    {
        SetPairs(kChainBase);
        if (p->chain == CHAIN_EXITLOOP)
        {
            SetPairs(kExitLoopBase);
        }
        char pairs[512];
        char log[128];
        // A carried-on run keeps the unread run's log, and a start with
        // nothing to do keeps the last run's.
        snprintf(log, sizeof(log), "%s/run-%02d%s.log", sChain.dir, sChain.run,
                 sChain.nothing ? "-finished" : sChain.resumed ? "-resumed" : "");
        if (sChain.nothing)
        {
            snprintf(pairs, sizeof(pairs),
                     "TSSM_BOOT=;TSSM_EXIT_AFTER=150;TSSM_SAVE_DIR=%s/saves;TSSM_CONFIG=%s/config.ini",
                     sChain.dir, sChain.dir);
        }
        else if (p->chain == CHAIN_FPS)
        {
            // The level loads and its dialogs go in the first 400 frames;
            // frames 401-1300 (30 s at 30 frames a second) are measured as
            // one stretch, the player standing at the level's start.
            snprintf(pairs, sizeof(pairs),
                     "%s;TSSM_INPUT=%s;TSSM_C3D_AB=400:skin=gpu;TSSM_C3D_AB_TAG=%s;"
                     "TSSM_C3D_AB_LOG=%s/fps-effects.log;TSSM_EXIT_AFTER=1300;"
                     "TSSM_SAVE_DIR=%s/saves;TSSM_CONFIG=%s/config.ini",
                     ChainEnv(sChain.cfg), kIntroKeys, sChain.run == 1 ? "BB02" : "GG02",
                     k3DSRoot, sChain.dir, sChain.dir);
        }
        else if (p->chain == CHAIN_SPEED)
        {
            // The schedule and the script are longer than SetPairs takes.
            const SpeedRun* r = SpeedThisRun();
            static char ab[8192];
            static char input[2048];
            const int last = SpeedSchedule(r, ab, sizeof(ab), input, sizeof(input));
            setenv("TSSM_C3D_AB", ab, 1);
            if (input[0] != '\0')
            {
                setenv("TSSM_INPUT", input, 1);
            }
            else
            {
                unsetenv("TSSM_INPUT");
            }
            char tag[64];
            snprintf(tag, sizeof(tag), "run %d %s", sChain.run, r->cfg);
            setenv("TSSM_C3D_AB_TAG", tag, 1);
            char boot[32];
            snprintf(boot, sizeof(boot), "TSSM_BOOT=%s", r->boot);
            SetPairs(boot);
            SetPairs("TSSM_FIXED_STEP=30;TSSM_C3D_STATS=5;TSSM_C3D_AB_DETAIL=1");
            SetPairs(r->extra);
            snprintf(pairs, sizeof(pairs),
                     "TSSM_C3D_AB_LOG=%s/%s.log;TSSM_EXIT_AFTER=%d;TSSM_SAVE_DIR=%s/saves;"
                     "TSSM_CONFIG=%s/config.ini",
                     k3DSRoot, ChainDirName(), last, sChain.dir, sChain.dir);
        }
        else
        {
            // Odd runs play BB02 for a few seconds, even runs only the title
            // screen: the same quit, sooner.
            const bool level = (sChain.run & 1) != 0;
            snprintf(pairs, sizeof(pairs),
                     "%s;%s;TSSM_EXIT_AFTER=%d;TSSM_SAVE_DIR=%s/saves;TSSM_CONFIG=%s/config.ini",
                     ChainEnv(sChain.cfg), level ? "TSSM_BOOT=BB02" : "TSSM_BOOT=",
                     level ? 420 : 240, sChain.dir, sChain.dir);
        }
        SetPairs(pairs);
        setenv("TSSM_LOG", log, 1);
    }

    // Luma3DS's loader: the .3dsx it loads next, once (3ds-hbmenu's
    // loaders/rosalina.c), with its arguments.
    Result ChainToSelf(const char* self)
    {
        Handle h = 0;
        Result r = svcConnectToPort(&h, "hb:ldr");
        if (R_FAILED(r))
        {
            return r;
        }
        const char* path = strncmp(self, "sdmc:/", 6) == 0 ? self + 5 : self;
        u32* cmd = getThreadCommandBuffer();
        cmd[0] = IPC_MakeHeader(2, 0, 2); // SetTarget
        cmd[1] = IPC_Desc_StaticBuffer(strlen(path) + 1, 0);
        cmd[2] = (u32)(uintptr_t)path;
        r = svcSendSyncRequest(h);
        if (R_SUCCEEDED(r))
        {
            r = (Result)cmd[1];
        }
        if (R_SUCCEEDED(r))
        {
            // argc, then the arguments one after another (libctru's argv).
            static u32 argv[0x400 / 4];
            memset(argv, 0, sizeof(argv));
            argv[0] = 2;
            char* d = (char*)&argv[1];
            const size_t a = strlen(self) + 1;
            const size_t b = strlen(kChainArg) + 1;
            if (a + b < sizeof(argv) - 4)
            {
                memcpy(d, self, a);
                memcpy(d + a, kChainArg, b);
            }
            cmd = getThreadCommandBuffer();
            cmd[0] = IPC_MakeHeader(3, 0, 2); // SetArgv
            cmd[1] = IPC_Desc_StaticBuffer(sizeof(argv), 1);
            cmd[2] = (u32)(uintptr_t)argv;
            r = svcSendSyncRequest(h);
            if (R_SUCCEEDED(r))
            {
                r = (Result)cmd[1];
            }
        }
        svcCloseHandle(h);
        return r;
    }

    int BottomText(u8* fb, GSPGPU_FramebufferFormat format, int y, int scale, u32 rgb,
                   const char* text);

    // ------------------------------------------------------ the leak ladder

    // Probe 11 finds the first part of the program whose quit leaves its
    // process held (every quit of the game's did, the fixed one too). One
    // launch from the Homebrew Launcher climbs a ladder of layers, each run
    // the one before plus one part, starting itself again through Luma3DS's
    // loader as probes 9 and 10 do; each start measures what the run before
    // left behind as the start-up's held line does. The first held layer
    // stops the climb for that power-on; the next start after a restart
    // repeats it once to confirm. L0-L2b run in a constructor that runs
    // before every other one (LadderStart), with nothing of the port's
    // around them, and quit there; L3-L6 are the port's own start-up and
    // quit.
#if TSSM_3DS_PROBE == 11
    enum
    {
        L0,
        L1,
        L2,
        L2A,
        L2B,
        L3,
        L4,
        L5,
        L6,
        kLayers,
        LCHECK = -1, // no layer: a run that only measures, says why it stopped and quits
    };

    struct Layer
    {
        const char* name;
        const char* adds; // for the summary lines
        const char* detail; // for the run's log and the bottom screen
    };

    const Layer kLayer[kLayers] = {
        { "L0", "nothing of the port started: gfx and a text screen",
          "this program with nothing of the port started: libctru's start-up services, "
          "gfxInitDefault and a text screen for 2 s, gfxExit (and, as every run, the ladder's "
          "logs on the SD card, the process count and the restart through the loader)" },
        { "L1", "+ RomFS and SD card files",
          "L0 + romfsInit and romfsExit, a file read from the RomFS, and on the SD card a file "
          "written and renamed over the old one, read back, and a file appended to and synced, as "
          "the config and the log are" },
        { "L2", "+ citro3d",
          "L1 + citro3d: C3D_Init, a render target on the top screen, a cleared frame every vblank "
          "for 2 s, C3D_Fini" },
        { "L2a", "+ the speed-up, the APT hook, the exception handler",
          "L2 + the port's own system calls at start: the New 3DS speed-up, the APT hook, the "
          "exception handler, the APT queries (program id, model, time limit)" },
        { "L2b", "+ SDL video, events and gamepad on their own",
          "L2a + SDL on its own, called as the port calls it: the gamepad subsystem and its pads, "
          "video (gfx) and the bottom screen's window, events pumped for 2 s, SDL_Quit; no "
          "audio" },
        { "L3", "the port's start-up to before the game, audio off",
          "the port's own start-up up to before the game (config, log, game files, SDL's video, "
          "events and pads, librw on citro3d), audio off, 2 s of events, then the port's normal "
          "quit" },
        { "L4", "+ SDL audio (ndsp, core-1 thread, time limit)",
          "L3 + SDL audio as the port has it: ndsp, SDL's audio thread on core 1, the 30% time "
          "limit" },
        { "L5", "the port's full start-up to the title screen",
          "the port's full start-up to the title screen (240 frames), then its normal quit" },
        { "L6", "the full game in BB02",
          "the full game in BB02 for 420 frames, then its normal quit" },
    };

    // What a run with no layer is for.
    enum
    {
        CHECK_NONE,
        CHECK_LEAK, // the run before was held: the climb stops for this power-on
        CHECK_RESULT, // the ladder's verdict is in
        CHECK_FINISHED, // a start after the verdict
        CHECK_RESTART, // a start with programs already held: restart first
        CHECK_STOP, // two or more held at a chained start, or a state it cannot carry on from
    };

    // state.txt: what the run wrote at its start, done=1 once its quit got
    // to the loader, and chained=1 once the loader was told to start the
    // next run.
    struct LadderState
    {
        char phase[12]; // climb, leak (stopped at a held layer), confirm, finished
        int run;
        int layer; // that run's layer; LCHECK a run with no layer
        int leak; // the held layer (leak, confirm, finished)
        int done;
        int chained;
        char skipped[64]; // layers skipped: no quit reached, or the next run never started
        char result[300];
    };

    struct LadderRun
    {
        int layer;
        int check;
        bool forced; // force-layer.txt (for an emulator, which has no loader to chain through)
        bool confirm; // repeats a held layer once, after a restart
        bool last; // starts no other run
        bool early; // L0-L2b and the runs with no layer: before the port, in LadderStart
        bool stopBeforeGame; // L3, L4: quit at i3DSLadderCheckpoint
        char log[96]; // this run's log
        char screen[400]; // a run with no layer: what the bottom screen says
        LadderState st; // written to state.txt
    };
    LadderRun sL;

    const char* LayerName(int l)
    {
        return l >= 0 && l < kLayers ? kLayer[l].name : "check";
    }

    int LayerIndex(const char* name)
    {
        for (int i = 0; i < kLayers; i++)
        {
            if (strcasecmp(name, kLayer[i].name) == 0)
            {
                return i;
            }
        }
        return LCHECK;
    }

    // One line of an early run's own log (L0-L2b and the runs with no layer;
    // L3-L6 have the port's log), opened and closed for each line.
    void LLine(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

    void LLine(const char* fmt, ...)
    {
        FILE* f = fopen(sL.log, "a");
        if (f == NULL)
        {
            return;
        }
        fputs("tssm: ladder: ", f);
        va_list ap;
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fputc('\n', f);
        fclose(f);
    }

    bool LadderLoad(LadderState* st)
    {
        memset(st, 0, sizeof(*st));
        st->layer = LCHECK;
        st->leak = LCHECK;
        char path[96];
        snprintf(path, sizeof(path), "%s/state.txt", sChain.dir);
        FILE* f = fopen(path, "r");
        if (f == NULL)
        {
            return false;
        }
        char line[400];
        while (fgets(line, sizeof(line), f) != NULL)
        {
            char* eq = strchr(line, '=');
            if (eq == NULL)
            {
                continue;
            }
            *eq = '\0';
            char* v = eq + 1;
            v[strcspn(v, "\r\n")] = '\0';
            if (strcmp(line, "phase") == 0)
            {
                snprintf(st->phase, sizeof(st->phase), "%s", v);
            }
            else if (strcmp(line, "run") == 0)
            {
                st->run = atoi(v);
            }
            else if (strcmp(line, "layer") == 0)
            {
                st->layer = LayerIndex(v);
            }
            else if (strcmp(line, "leak") == 0)
            {
                st->leak = LayerIndex(v);
            }
            else if (strcmp(line, "done") == 0)
            {
                st->done = atoi(v);
            }
            else if (strcmp(line, "chained") == 0)
            {
                st->chained = atoi(v);
            }
            else if (strcmp(line, "skipped") == 0)
            {
                snprintf(st->skipped, sizeof(st->skipped), "%s", v);
            }
            else if (strcmp(line, "result") == 0)
            {
                snprintf(st->result, sizeof(st->result), "%s", v);
            }
        }
        fclose(f);
        return true;
    }

    // Not for a forced run, nor for a start that tests nothing because
    // programs are already held: that one leaves the state as it found it,
    // so that a freeze before it is still skipped at the next clean start.
    void LadderSave()
    {
        if (sL.forced || sL.check == CHECK_RESTART)
        {
            return;
        }
        char path[96];
        snprintf(path, sizeof(path), "%s/state.txt", sChain.dir);
        FILE* f = fopen(path, "w");
        if (f == NULL)
        {
            return;
        }
        fprintf(f,
                "phase=%s\nrun=%d\nlayer=%s\nleak=%s\ndone=%d\nchained=%d\nskipped=%s\nresult=%s\n",
                sL.st.phase, sL.st.run, LayerName(sL.st.layer), LayerName(sL.st.leak), sL.st.done,
                sL.st.chained, sL.st.skipped, sL.st.result);
        fclose(f);
    }

    // force-layer.txt in the ladder's folder: one run of that layer (or
    // "check"), once, with no state kept; it becomes force-layer.last.txt.
    // A test setting for an emulator, which has no loader to chain through.
    bool ReadForce(char* out, size_t size)
    {
        out[0] = '\0';
        char path[96];
        char last[96];
        snprintf(path, sizeof(path), "%s/force-layer.txt", sChain.dir);
        snprintf(last, sizeof(last), "%s/force-layer.last.txt", sChain.dir);
        FILE* f = fopen(path, "r");
        if (f == NULL)
        {
            return false;
        }
        char line[32] = "";
        if (fgets(line, sizeof(line), f) == NULL)
        {
            line[0] = '\0';
        }
        fclose(f);
        line[strcspn(line, " \r\n")] = '\0';
        remove(last);
        if (rename(path, last) != 0)
        {
            remove(path);
        }
        snprintf(out, size, "%s", line);
        return out[0] != '\0';
    }

    // ------------------------------------------- the early layers, L0-L2b

    aptHookCookie sLadderHook;

    void LadderOnApt(APT_HookType hook, void*)
    {
        if (hook == APTHOOK_ONEXIT)
        {
            LLine("the system asked the program to close");
        }
    }

    // `n` vblanks of the screens gfx set up; FALSE when the system wants
    // the program to close.
    bool LadderFrames(int n)
    {
        for (int i = 0; i < n; i++)
        {
            if (!aptMainLoop())
            {
                return false;
            }
            gfxFlushBuffers();
            gfxSwapBuffers();
            gspWaitForVBlank();
        }
        return true;
    }

    void LadderText()
    {
        BoxLine("\n TSSM probe 11: the leak ladder\n\n");
        if (sL.layer >= 0)
        {
            char t[600];
            snprintf(t, sizeof(t), "Run %d, layer %s%s: %s.", sL.st.run, LayerName(sL.layer),
                     sL.confirm ? " (once more, to confirm)" : "", kLayer[sL.layer].detail);
            BoxWrap(t);
            BoxLine("\n");
            BoxWrap(sL.forced  ? "A test run: it quits after this layer." :
                    sL.confirm ? "It quits, starts itself once more to measure, then goes back "
                                 "to the Homebrew Launcher. Don't press anything." :
                                 "It quits and starts itself again with one more part each "
                                 "time, then goes back to the Homebrew Launcher. Don't press "
                                 "anything.");
            BoxLine("\n\x1b[33;1m");
            BoxWrap("If this freezes (nothing moves for 30 seconds and HOME does nothing), hold "
                    "POWER, switch it on and start probe 11 again: it skips that part.");
            BoxLine("\x1b[0m\n");
        }
        else
        {
            BoxLine("\x1b[33;1m");
            BoxWrap(sL.screen);
            BoxLine("\x1b[0m\n");
            BoxWrap("Back to the Homebrew Launcher in a few seconds.");
            BoxLine("\n");
        }
        BoxWrap("Log: 3ds/tssm/leakladder.log");
        fflush(stdout);
    }

    // L1: a RomFS file read; on the SD card a file written as the config is
    // (a new file renamed over the old one) and read back, and a log
    // appended to and synced as the log is.
    void LadderFiles()
    {
        int romBytes = -1;
        char romName[64] = "";
        DIR* d = opendir("romfs:/licenses");
        if (d != NULL)
        {
            for (struct dirent* e = readdir(d); e != NULL; e = readdir(d))
            {
                if (e->d_name[0] == '.')
                {
                    continue;
                }
                char path[128];
                snprintf(path, sizeof(path), "romfs:/licenses/%s", e->d_name);
                FILE* f = fopen(path, "rb");
                if (f != NULL)
                {
                    char buf[512];
                    romBytes = (int)fread(buf, 1, sizeof(buf), f);
                    fclose(f);
                    snprintf(romName, sizeof(romName), "%s", e->d_name);
                    break;
                }
            }
            closedir(d);
        }

        char cfg[128], tmp[128], bak[128], logp[128];
        snprintf(cfg, sizeof(cfg), "%s/l1-config.ini", sChain.dir);
        snprintf(tmp, sizeof(tmp), "%s/l1-config.ini.new", sChain.dir);
        snprintf(bak, sizeof(bak), "%s/l1-config.ini.bak", sChain.dir);
        snprintf(logp, sizeof(logp), "%s/l1-log.txt", sChain.dir);
        int wrote = -1, readBack = -1, appended = -1;
        FILE* f = fopen(tmp, "w");
        if (f != NULL)
        {
            wrote = fprintf(f, "; leak ladder L1\n[l1]\nrun = %d\n", sL.st.run);
            fclose(f);
            remove(bak);
            rename(cfg, bak);
            if (rename(tmp, cfg) == 0)
            {
                remove(bak);
            }
        }
        f = fopen(cfg, "r");
        if (f != NULL)
        {
            char buf[256];
            readBack = (int)fread(buf, 1, sizeof(buf), f);
            fclose(f);
        }
        const int fd = open(logp, O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (fd >= 0)
        {
            char line[64];
            const int n = snprintf(line, sizeof(line), "run %d\n", sL.st.run);
            appended = (int)write(fd, line, (size_t)n);
            fsync(fd);
            close(fd);
        }
        LLine("files: RomFS licenses/%s %d bytes read; SD card: l1-config.ini %d bytes written "
              "(renamed over the old one), %d read back; l1-log.txt %d bytes appended and synced",
              romName, romBytes, wrote, readBack, appended);
    }

    // L2: citro3d on the top screen, a cleared frame every vblank.
    void LadderCitro3d()
    {
        if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))
        {
            LLine("citro3d: C3D_Init failed");
            return;
        }
        C3D_RenderTarget* t =
            C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
        const u32 flags = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |
                          GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                          GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
                          GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
        if (t != NULL)
        {
            C3D_RenderTargetSetOutput(t, GFX_TOP, GFX_LEFT, flags);
        }
        LLine("citro3d: C3D_Init, %s render target on the top screen",
              t != NULL ? "a 400x240 RGBA8 + depth" : "NO");
        int frames = 0;
        const u64 end = svcGetSystemTick() + 2ull * SYSCLOCK_ARM11;
        while (t != NULL && svcGetSystemTick() < end && aptMainLoop())
        {
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C3D_RenderTargetClear(t, C3D_CLEAR_ALL, 0x204060FFu + ((u32)(frames & 63) << 16), 0);
            C3D_FrameDrawOn(t);
            C3D_FrameEnd(0);
            frames++;
        }
        if (t != NULL)
        {
            C3D_RenderTargetDelete(t);
        }
        C3D_Fini();
        LLine("citro3d: %d cleared frames, the target deleted, C3D_Fini", frames);
    }

    // L2b: SDL's video, events and gamepad as the port starts them (pads
    // first, as iPadHostInit; then video, as iWindowOpen, with the bottom
    // screen's window), and SDL_Quit as its quit has it.
    void LadderSdl()
    {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        const bool pads = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        int opened = 0;
        if (pads)
        {
            int count = 0;
            SDL_JoystickID* ids = SDL_GetGamepads(&count);
            for (int i = 0; ids != NULL && i < count; i++)
            {
                opened += SDL_OpenGamepad(ids[i]) != NULL ? 1 : 0;
            }
            SDL_free(ids);
        }
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
        const bool video = SDL_InitSubSystem(SDL_INIT_VIDEO);
        bool window = false;
        if (video)
        {
            int count = 0;
            SDL_DisplayID* displays = SDL_GetDisplays(&count);
            SDL_DisplayID bottom = 0;
            for (int i = 0; displays != NULL && i < count; i++)
            {
                const char* name = SDL_GetDisplayName(displays[i]);
                if (name != NULL && SDL_strstr(name, "bottom") != NULL)
                {
                    bottom = displays[i];
                }
            }
            SDL_free(displays);
            if (bottom != 0)
            {
                SDL_PropertiesID props = SDL_CreateProperties();
                SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "TSSM overlay");
                SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER,
                                      SDL_WINDOWPOS_UNDEFINED_DISPLAY(bottom));
                SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER,
                                      SDL_WINDOWPOS_UNDEFINED_DISPLAY(bottom));
                SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 320);
                SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 240);
                SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, true);
                window = SDL_CreateWindowWithProperties(props) != NULL;
                SDL_DestroyProperties(props);
            }
            i3DSProbeBanner();
        }
        LLine("SDL: gamepad subsystem %s (%d pad(s) opened), video %s, bottom-screen window %s",
              pads ? "up" : "NOT up", opened, video ? "up" : "NOT up",
              window ? "made" : "NOT made");
        int pumps = 0;
        const u64 end = svcGetSystemTick() + 2ull * SYSCLOCK_ARM11;
        while (svcGetSystemTick() < end)
        {
            SDL_PumpEvents();
            SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
            pumps++;
            svcSleepThread(16666667ll);
        }
        SDL_Quit();
        LLine("SDL: %d event pumps, SDL_Quit", pumps);
    }

    // The end of an early run: the threads and handles left, then _exit:
    // the port's static objects were never made, so neither their
    // destructors nor its atexit handlers may run; libctru's part of the
    // exit (userAppExit) chains the next run.
    __attribute__((noreturn)) void LadderEarlyEnd()
    {
        char threads[300];
        FormatThreads(threads, sizeof(threads), "");
        s64 handles = -1;
        if (R_FAILED(svcGetProcessInfo(&handles, CUR_PROCESS_HANDLE, 4)) || handles < 0 ||
            handles > 0x1000)
        {
            handles = -1;
        }
        Held h;
        CountProcesses(&h);
        LLine("quit: stopped: %s; %lld handle(s) open (libctru's own services among them; -1: not "
              "known); %d process(es) on the console, %d of them named as this one besides it; %lu "
              "s since power-on",
              threads, (long long)handles, h.processes, h.sameName,
              (unsigned long)SecondsSincePowerOn());
        LLine("quit: _exit (the port's static objects were never made)");
        sQuitting = true;
        sStopped = true;
        _exit(0);
    }

    void LadderEarly()
    {
        const int L = sL.layer;
        bool romfs = false;
        if (L >= L1)
        {
            const Result r = romfsInit();
            romfs = R_SUCCEEDED(r);
            LLine("romfsInit: %#lx", (unsigned long)r);
        }
        if (L >= L2A)
        {
            u64 programId = 0;
            bool isNew = false;
            u32 limit = 0;
            const Result a = APT_GetProgramID(&programId);
            const Result b = APT_CheckNew3DS(&isNew);
            const Result c = APT_GetAppCpuTimeLimit(&limit);
            osSetSpeedupEnable(true);
            aptHook(&sLadderHook, LadderOnApt, NULL);
            threadOnException(OnException, sExceptionStack + sizeof(sExceptionStack),
                              WRITE_DATA_TO_HANDLER_STACK);
            LLine("system: program id %016llx (%#lx), %s (%#lx), core-1 time limit %lu%% (%#lx); "
                  "the speed-up on, the APT hook and the exception handler in",
                  (unsigned long long)programId, (unsigned long)a,
                  isNew ? "New 3DS" : "not a New 3DS", (unsigned long)b, (unsigned long)limit,
                  (unsigned long)c);
        }

        // The text screen. stdout and stderr are the console's while gfx is
        // up, and are put back after, so that nothing prints into freed
        // framebuffers.
        const devoptab_t* out = devoptab_list[STD_OUT];
        const devoptab_t* err = devoptab_list[STD_ERR];
        gfxInitDefault();
        static PrintConsole sLadderConsole;
        consoleInit(GFX_BOTTOM, &sLadderConsole);
        LadderText();
        LLine("gfx: gfxInitDefault, the text screen on the bottom");
        const bool go = LadderFrames(L == LCHECK ? 600 : 120);
        if (go && L >= L1)
        {
            LadderFiles();
        }
        if (go && L >= L2)
        {
            LadderCitro3d();
        }
        fflush(stdout);
        fflush(stderr);
        devoptab_list[STD_OUT] = out;
        devoptab_list[STD_ERR] = err;
        gfxExit();
        LLine("gfx: gfxExit%s", go ? "" : " (the system asked the program to close)");
        if (go && L >= L2B)
        {
            LadderSdl();
        }
        if (L >= L2A)
        {
            aptUnhook(&sLadderHook);
            LLine("system: the APT hook out");
        }
        if (romfs)
        {
            romfsExit();
            LLine("romfsExit");
        }
        LadderEarlyEnd();
    }

    // ------------------------------------------------------- the run's plan

    void LadderStart()
    {
        const Probe* p = ThisProbe();
        if (p == NULL || p->chain != CHAIN_LADDER)
        {
            return;
        }
        snprintf(sChain.dir, sizeof(sChain.dir), "%s/%s", k3DSRoot, ChainDirName());
        mkdir(k3DSDataParent, 0777);
        mkdir(k3DSRoot, 0777);
        mkdir(sChain.dir, 0777);

        LadderState st;
        const bool have = LadderLoad(&st);
        bool chained = __system_argc >= 2 && __system_argv != NULL && __system_argv[1] != NULL &&
                       strcmp(__system_argv[1], kChainArg) == 0;
        char force[32] = "";
        bool forced = !(chained && have) && ReadForce(force, sizeof(force));
        // force-layer.txt's "sim-chained-held=N" or "sim-fresh-held=N": this
        // start taken as a chained one or one from the Homebrew Launcher,
        // with N held, and planned as such with state.txt (an emulator has
        // neither the chain nor the held programs).
        int simHeld = -1;
        if (forced && strncmp(force, "sim-", 4) == 0)
        {
            forced = false;
            chained = strncmp(force + 4, "chained", 7) == 0;
            const char* eq = strchr(force, '=');
            simHeld = eq != NULL ? atoi(eq + 1) : 0;
        }
        // A chained start with its state gone begins again.
        const bool fresh = !chained || !have;

        // The held count as the start-up's held line has it, with the same
        // 3 s for the program before (the Homebrew Launcher, or the run
        // before) to be on its way out.
        Held h;
        MeasureHeld(&h);
        int held = HeldCount(&h);
        for (int i = 0; i < 6 && h.sameName >= 0 && held > 0; i++)
        {
            svcSleepThread(500000000ll);
            CountProcesses(&h);
            held = HeldCount(&h) < held ? HeldCount(&h) : held;
        }
        if (held < 0)
        {
            held = 0;
        }
        if (simHeld >= 0)
        {
            held = simHeld;
        }

        memset(&sL, 0, sizeof(sL));
        sL.layer = LCHECK;
        sL.st = st;
        sL.st.done = 0;
        sL.st.chained = 0;
        // The run before reached its quit and told the loader to start the
        // next one, but that one never wrote its state: a crash in libctru's
        // exit or while the loader started it.
        const bool lostChain = have && st.done != 0 && st.chained != 0;
        char note[400] = "";
        bool verdict = false;
        // A LEAK or STOP line, after the run's line.
        char endLine[400] = "";

        if (fresh)
        {
            time_t t = time(NULL);
            struct tm tm;
            memset(&tm, 0, sizeof(tm));
            localtime_r(&t, &tm);
            ChainLog("\n==== %04d-%02d-%02d %02d:%02d:%02d  %s (built %s %s): started from the "
                     "Homebrew Launcher%s",
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                     p->name, __DATE__, __TIME__, forced ? " (force-layer.txt)" : "");
        }
        sL.st.run = (have ? st.run : 0) + 1;

        if (forced)
        {
            sL.forced = true;
            sL.last = true;
            sL.layer = LayerIndex(force);
            sL.check = sL.layer == LCHECK ? CHECK_STOP : CHECK_NONE;
            snprintf(note, sizeof(note),
                     "forced by force-layer.txt (%s); no state kept, no next run", force);
            snprintf(sL.screen, sizeof(sL.screen),
                     "A test run forced by force-layer.txt: it measures, waits and quits.");
        }
        else if (fresh)
        {
            if (held >= 1)
            {
                // No run with programs already held: another held one would
                // bring the console nearer the panic, and the count would
                // not say which run it came from.
                sL.check = CHECK_RESTART;
                sL.last = true;
                snprintf(endLine, sizeof(endLine),
                         "STOP: %d earlier program(s) are held at this start (something ran since "
                         "the console was switched on and was never freed); nothing is tested. "
                         "Restart the console (hold POWER), then start the probe right away.",
                         held);
                snprintf(sL.screen, sizeof(sL.screen),
                         "%d program(s) that ran before are still in memory, so this start tests "
                         "nothing.\n\nRestart the console (hold POWER until it turns off, then "
                         "switch it on) and start TSSM probe 11 right away.",
                         held);
            }
            else if (have && strcmp(st.phase, "finished") == 0)
            {
                sL.check = CHECK_FINISHED;
                sL.last = true;
                snprintf(note, sizeof(note),
                         "the ladder finished before (state.txt; delete it to climb again)");
                snprintf(sL.screen, sizeof(sL.screen),
                         "Probe 11 has finished; there is nothing left to do.\n\n%s\n\nRestart "
                         "the console, then open ftpd.",
                         st.result);
            }
            else if (have && strcmp(st.phase, "confirm") == 0 && st.leak >= 0 &&
                     (st.done == 0 || lostChain))
            {
                // The repeat froze (or the console was switched off in it),
                // or the run after it never started: not repeated a third
                // time.
                sL.check = CHECK_RESULT;
                sL.last = true;
                snprintf(sL.st.phase, sizeof(sL.st.phase), "finished");
                snprintf(sL.st.result, sizeof(sL.st.result),
                         "RESULT: layer %s (%s) was held in the first power-on; its repeat after "
                         "a restart %s, so it is not confirmed.",
                         LayerName(st.leak), kLayer[st.leak].adds,
                         st.done == 0 ? "did not reach its quit (a freeze, a crash, or the console "
                                        "was switched off)" :
                                        "reached its quit but the run after it never started (a "
                                        "crash in the exit or while the loader started it)");
                verdict = true;
                snprintf(sL.screen, sizeof(sL.screen),
                         "Probe 11 has finished.\n\n%s\n\nRestart the console, then open ftpd.",
                         sL.st.result);
            }
            else if (have && (strcmp(st.phase, "leak") == 0 || strcmp(st.phase, "confirm") == 0) &&
                     st.leak >= 0)
            {
                // The held layer once more, after a restart (a repeat cut
                // short is repeated again).
                sL.layer = st.leak;
                sL.confirm = true;
                snprintf(sL.st.phase, sizeof(sL.st.phase), "confirm");
                snprintf(note, sizeof(note),
                         "repeats %s once to confirm (it was held in the last power-on)",
                         LayerName(st.leak));
            }
            else if (have && strcmp(st.phase, "climb") == 0 && (st.done == 0 || lostChain) &&
                     st.layer >= 0)
            {
                // A layer whose run froze (or the console was switched off
                // in it), or whose next run never started: the climb goes on
                // above it, so that a freeze or a crash is not met again at
                // every start.
                const int k = st.layer;
                const size_t n = strlen(sL.st.skipped);
                snprintf(sL.st.skipped + n, sizeof(sL.st.skipped) - n, "%s%s", n > 0 ? "," : "",
                         LayerName(k));
                snprintf(note, sizeof(note), "run %d (%s) %s: %s is skipped", st.run, LayerName(k),
                         st.done == 0 ? "did not reach its quit (a freeze, a crash, or the console "
                                        "was switched off)" :
                                        "reached its quit but the next run never started (a crash "
                                        "in the exit or while the loader started it)",
                         LayerName(k));
                if (k + 1 < kLayers)
                {
                    sL.layer = k + 1;
                }
                else
                {
                    sL.check = CHECK_RESULT;
                    sL.last = true;
                    snprintf(sL.st.phase, sizeof(sL.st.phase), "finished");
                    snprintf(sL.st.result, sizeof(sL.st.result),
                             "RESULT: nothing was held from L0 to L5; skipped (did not reach "
                             "their quit, or the run after them never started): %s.",
                             sL.st.skipped);
                    verdict = true;
                    snprintf(sL.screen, sizeof(sL.screen),
                             "Probe 11 has finished.\n\n%s\n\nRestart the console, then open "
                             "ftpd.",
                             sL.st.result);
                }
            }
            else
            {
                sL.layer = L0;
                snprintf(sL.st.phase, sizeof(sL.st.phase), "climb");
                sL.st.leak = LCHECK;
                sL.st.result[0] = '\0';
                sL.st.skipped[0] = '\0';
                if (have && strcmp(st.phase, "climb") == 0)
                {
                    snprintf(note, sizeof(note),
                             "the climb before stopped without a verdict (its run %d, %s, %s); "
                             "it starts again at L0",
                             st.run, LayerName(st.layer),
                             st.done ? "reached its quit" : "did not reach its quit");
                }
            }
        }
        else
        {
            const int prev = st.layer;
            if (held >= 2)
            {
                sL.check = CHECK_STOP;
                sL.last = true;
                if (strcmp(st.phase, "climb") == 0 && prev >= 0)
                {
                    snprintf(sL.st.phase, sizeof(sL.st.phase), "leak");
                    sL.st.leak = prev;
                }
                snprintf(endLine, sizeof(endLine),
                         "STOP: %d programs are held at this start; no run starts with two or "
                         "more held. Restart the console (hold POWER), then start the probe once "
                         "more.",
                         held);
                snprintf(sL.screen, sizeof(sL.screen),
                         "Probe 11 stopped for now: two programs are still in memory.\n\nRestart "
                         "the console (hold POWER until it turns off, then switch it on) and "
                         "start TSSM probe 11 once more.");
            }
            else if (strcmp(st.phase, "climb") == 0 && prev >= 0)
            {
                snprintf(note, sizeof(note), "%s (%s): its process %s", LayerName(prev),
                         kLayer[prev].adds, held >= 1 ? "STAYED BEHIND (held)" : "was freed");
                if (held >= 1)
                {
                    sL.check = CHECK_LEAK;
                    sL.last = true;
                    snprintf(sL.st.phase, sizeof(sL.st.phase), "leak");
                    sL.st.leak = prev;
                    snprintf(endLine, sizeof(endLine),
                             "LEAK: layer %s (%s) leaves the process held. STOP for this "
                             "power-on: back to the Homebrew Launcher. Restart the console (hold "
                             "POWER), then start the probe once more: it repeats %s once to "
                             "confirm.",
                             LayerName(prev), kLayer[prev].adds, LayerName(prev));
                    snprintf(sL.screen, sizeof(sL.screen),
                             "Probe 11 found the part that stays in memory: %s (%s).\n\nNow "
                             "restart the console (hold POWER until it turns off, then switch it "
                             "on) and start TSSM probe 11 once more. It checks that part again.",
                             LayerName(prev), kLayer[prev].adds);
                }
                else if (prev + 1 >= kLayers)
                {
                    snprintf(sL.st.result, sizeof(sL.st.result),
                             "RESULT: nothing was held from L0 to L6, the full game included: "
                             "every run's process in this chain was freed (in probe 9's chain "
                             "they were held)%s%s.",
                             sL.st.skipped[0] != '\0' ?
                                 "; skipped (did not reach their quit, or the run after them "
                                 "never started): " :
                                 "",
                             sL.st.skipped);
                    verdict = true;
                }
                else
                {
                    sL.layer = prev + 1;
                }
            }
            else if (strcmp(st.phase, "confirm") == 0 && st.leak >= 0)
            {
                const int k = st.leak;
                snprintf(note, sizeof(note), "%s (%s), repeated: its process %s", LayerName(k),
                         kLayer[k].adds, held >= 1 ? "STAYED BEHIND (held)" : "was freed");
                if (held >= 1 && k == L0)
                {
                    snprintf(sL.st.result, sizeof(sL.st.result),
                             "RESULT: even L0 (%s) is held, in both power-ons: every run of "
                             "this program is held, or the environment (the system, Luma3DS, "
                             "the loader) holds it; the control program tells which.",
                             kLayer[k].adds);
                }
                else if (held >= 1)
                {
                    snprintf(sL.st.result, sizeof(sL.st.result),
                             "RESULT: layer %s (%s) leaves the process held, in both power-ons; "
                             "every layer below it (%s%s) was freed.",
                             LayerName(k), kLayer[k].adds, k > 1 ? "L0 to " : "", LayerName(k - 1));
                }
                else
                {
                    snprintf(sL.st.result, sizeof(sL.st.result),
                             "RESULT: layer %s (%s) was held in the first power-on but freed when "
                             "it was repeated after a restart: not repeatable; the layers below "
                             "it were freed.",
                             LayerName(k), kLayer[k].adds);
                }
                verdict = true;
            }
            else
            {
                sL.check = CHECK_STOP;
                sL.last = true;
                snprintf(endLine, sizeof(endLine),
                         "STOP: a chained start with a state it cannot carry on from (phase "
                         "'%s'); back to the Homebrew Launcher. Deleting leakladder/state.txt "
                         "starts it afresh.",
                         st.phase);
                snprintf(sL.screen, sizeof(sL.screen),
                         "Probe 11 stopped: its notes on the SD card do not say what comes "
                         "next.\n\nRestart the console, then open ftpd.");
            }
            if (verdict)
            {
                sL.check = CHECK_RESULT;
                sL.last = true;
                snprintf(sL.st.phase, sizeof(sL.st.phase), "finished");
                snprintf(sL.screen, sizeof(sL.screen),
                         "Probe 11 has finished.\n\n%s\n\nRestart the console, then open ftpd.",
                         sL.st.result);
            }
        }
        sL.st.layer = sL.layer;
        sL.early = sL.layer < L3;
        sL.stopBeforeGame = sL.layer == L3 || sL.layer == L4;
        if (sL.forced)
        {
            snprintf(sL.log, sizeof(sL.log), "%s/forced-%s.log", sChain.dir, LayerName(sL.layer));
            remove(sL.log);
        }
        else
        {
            snprintf(sL.log, sizeof(sL.log), "%s/run-%02d-%s%s.log", sChain.dir, sL.st.run,
                     LayerName(sL.layer), sL.confirm ? "-confirm" : "");
        }
        sChain.run = sL.st.run;
        sChain.last = sL.last;
        snprintf(sChain.cfg, sizeof(sChain.cfg), "%s", LayerName(sL.layer));
        snprintf(sChain.note, sizeof(sChain.note), "%s",
                 sL.layer >= 0 ? kLayer[sL.layer].detail : "no layer: it measures and says why");

        LadderSave();
        char line[900];
        snprintf(line, sizeof(line),
                 "run %d, %s (%s)%s; %lu s since power-on; %d process(es) on the console, %d of "
                 "them named as this one besides it; not free at start %lu KB (this program %lu "
                 "KB); held by earlier programs: %d%s%s",
                 sL.st.run, LayerName(sL.layer),
                 sL.layer >= 0 ? kLayer[sL.layer].adds : "measures only",
                 sL.confirm ? ", repeated to confirm" :
                 sL.last    ? ", the last" :
                              "",
                 (unsigned long)SecondsSincePowerOn(), h.processes, h.sameName,
                 (unsigned long)h.notFreeKB, (unsigned long)h.ownKB, held,
                 note[0] != '\0' ? "; " : "", note);
        ChainLog("%s", line);
        if (simHeld >= 0)
        {
            ChainLog("(simulated by force-layer.txt: a %s start with %d held)",
                     chained ? "chained" : "Homebrew Launcher", simHeld);
        }
        if (endLine[0] != '\0')
        {
            ChainLog("%s", endLine);
        }
        if (verdict)
        {
            ChainLog("%s", sL.st.result);
        }
        if (sL.early)
        {
            LLine("%s", line);
            LLine("held by earlier programs: %lu KB (not free at start %lu KB, this program %lu "
                  "KB), about %d program(s) by the memory; %d process(es) on the console, %d of "
                  "them named as this one besides it%s%s%s; %lu s since power-on",
                  (unsigned long)(h.notFreeKB > h.ownKB ? h.notFreeKB - h.ownKB : 0),
                  (unsigned long)h.notFreeKB, (unsigned long)h.ownKB, h.byMemory, h.processes,
                  h.sameName, h.names[0] != '\0' ? " (ids " : "", h.names,
                  h.names[0] != '\0' ? ")" : "", (unsigned long)SecondsSincePowerOn());
            if (sL.layer >= 0)
            {
                LLine("layer %s: %s", LayerName(sL.layer), kLayer[sL.layer].detail);
            }
            LadderEarly();
        }
    }

    // At the very end of the quit (ChainNext): the next run, or the end.
    void LadderNext(const char* lastLine)
    {
        if (sL.early)
        {
            LLine("quit: %s", lastLine);
        }
        if (!sL.forced)
        {
            sL.st.done = 1;
            LadderSave();
        }
        if (sL.last)
        {
            ChainLog("run %d (%s): quit finished; back to the Homebrew Launcher.%s", sChain.run,
                     LayerName(sL.layer),
                     sL.forced ?
                         "" :
                     sL.check == CHECK_RESTART ?
                         " Restart the console, then start the probe." :
                     sL.check == CHECK_FINISHED || sL.check == CHECK_RESULT ?
                         " DONE: restart the console, then open ftpd." :
                         " Restart the console (hold POWER), then start the probe once more.");
            i3DSQuitLine("the probe stops; back to the Homebrew Launcher");
            return;
        }
        const char* self = __system_argc >= 1 && __system_argv != NULL ? __system_argv[0] : NULL;
        const Result r = self != NULL && self[0] != '\0' ? ChainToSelf(self) : -1;
        if (R_SUCCEEDED(r))
        {
            if (!sL.forced)
            {
                sL.st.chained = 1;
                LadderSave();
            }
            ChainLog("run %d (%s): quit finished; Luma3DS's loader starts the probe again (run %d)",
                     sChain.run, LayerName(sL.layer), sChain.run + 1);
            i3DSQuitLine("Luma3DS's loader starts %s again (run %d)", self, sChain.run + 1);
        }
        else
        {
            ChainLog("STOP: the loader could not be told to start the probe again (%#lx%s); back "
                     "to the Homebrew Launcher",
                     (unsigned long)r, self == NULL ? ", no path in argv" : "");
            i3DSQuitLine("no next run (%#lx); back to the Homebrew Launcher", (unsigned long)r);
        }
    }

    // The port's settings for L3-L6, over kChainBase: the ladder's folder,
    // config.ini and saves, the run's log, no overlay (the banner shows the
    // run).
    void ApplyLadder()
    {
        if (sL.early)
        {
            return;
        }
        SetPairs(kChainBase);
        SetPairs(kExitLoopBase);
        char pairs[512];
        const char* layer = sL.layer == L3 ? "TSSM_AUDIO=0;TSSM_BOOT=" :
                            sL.layer == L4 ? "TSSM_BOOT=" :
                            sL.layer == L5 ? "TSSM_BOOT=;TSSM_EXIT_AFTER=240" :
                                             "TSSM_BOOT=BB02;TSSM_EXIT_AFTER=420";
        snprintf(pairs, sizeof(pairs), "%s;TSSM_SAVE_DIR=%s/saves;TSSM_CONFIG=%s/config.ini", layer,
                 sChain.dir, sChain.dir);
        SetPairs(pairs);
        setenv("TSSM_LOG", sL.log, 1);
    }

    // The bottom screen's banner for L2b-L6, after its first lines.
    int LadderBanner(u8* fb, GSPGPU_FramebufferFormat format, int y)
    {
        char line[600];
        snprintf(line, sizeof(line), "Run %d, layer %s%s: %s.", sL.st.run, LayerName(sL.layer),
                 sL.confirm ? " (once more, to confirm)" : "",
                 sL.layer >= 0 ? kLayer[sL.layer].detail : "");
        y = BottomText(fb, format, y + 10, 1, 0xFFFFFF, line);
        return BottomText(fb, format, y + 6, 1, 0xFFFFFF,
                          sL.forced  ? "A test run: it quits after this layer." :
                          sL.confirm ? "It quits, starts itself once more to measure, then "
                                       "goes back to the Homebrew Launcher." :
                                       "It quits and starts itself again with one more part "
                                       "each time, then goes back to the Homebrew Launcher.");
    }

    __attribute__((constructor(101))) void LadderConstructor()
    {
        LadderStart();
    }
#else
    void LadderNext(const char*)
    {
    }

    void ApplyLadder()
    {
    }

    int LadderBanner(u8*, GSPGPU_FramebufferFormat, int y)
    {
        return y;
    }
#endif

    // At the very end of the quit, before libctru's exit: the next run, or
    // the end of the chain.
    void ChainNext(const char* lastLine)
    {
        const Probe* p = ThisProbe();
        if (p == NULL || p->chain == CHAIN_NONE || sChain.run < 1)
        {
            return;
        }
        if (p->chain == CHAIN_LADDER)
        {
            LadderNext(lastLine);
            return;
        }
        ChainLog("run %d: last quit line: %s", sChain.run, lastLine);
        if (sChain.last && sChain.resume)
        {
            ChainLog("PAUSED after run %d: back to the Homebrew Launcher. Restart the console "
                     "(hold POWER), then start the probe once more: it carries on.",
                     sChain.run);
            i3DSQuitLine("the probe stops for this power-on; back to the Homebrew Launcher");
            return;
        }
        if (sChain.last && p->chain == CHAIN_SPEED)
        {
            ChainLog("DONE after run %d%s: probe %d has finished. Restart the console (hold "
                     "POWER), then open ftpd.",
                     sChain.run, sChain.only ? " (only-run.txt's one run)" : "", TSSM_3DS_PROBE);
            i3DSQuitLine(TSSM_3DS_PROBE == 16   ?
                             "probe 16 has finished; back to the Homebrew Launcher" :
                         TSSM_3DS_PROBE == 15 ?
                             "probe 15 has finished; back to the Homebrew Launcher" :
                             "probe 14 has finished; back to the Homebrew Launcher");
            return;
        }
        if (sChain.last)
        {
            ChainLog("DONE after run %d: back to the Homebrew Launcher. Restart the console (hold "
                     "POWER) before starting anything else.",
                     sChain.run);
            i3DSQuitLine("the probe is done; back to the Homebrew Launcher");
            return;
        }
        const char* self = __system_argc >= 1 && __system_argv != NULL ? __system_argv[0] : NULL;
        const Result r = self != NULL && self[0] != '\0' ? ChainToSelf(self) : -1;
        if (R_SUCCEEDED(r))
        {
            i3DSQuitLine("Luma3DS's loader starts %s again (run %d)", self, sChain.run + 1);
        }
        else
        {
            ChainLog("STOP: the loader could not be told to start the probe again (%#lx%s); "
                     "back to the Homebrew Launcher",
                     (unsigned long)r, self == NULL ? ", no path in argv" : "");
            i3DSQuitLine("no next run (%#lx); back to the Homebrew Launcher", (unsigned long)r);
        }
    }

    void SetPairs(const char* list)
    {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s", list);
        char* save = NULL;
        for (char* p = strtok_r(buf, ";", &save); p != NULL; p = strtok_r(NULL, ";", &save))
        {
            char* eq = strchr(p, '=');
            if (eq != NULL && eq != p)
            {
                *eq = '\0';
                setenv(p, eq + 1, 1);
            }
        }
    }

    // Before the log opens (its name), and again after test-env.txt, which
    // must not change what the probe is.
    void ApplyProbe()
    {
        const Probe* p = ThisProbe();
        if (p == NULL)
        {
            return;
        }
        if (p->chain == CHAIN_LADDER)
        {
            ApplyLadder();
            return;
        }
        if (p->chain != CHAIN_NONE)
        {
            ApplyChain(p);
            return;
        }
        SetPairs(kProbeBase);
        SetPairs(p->env);
        char log[96];
        snprintf(log, sizeof(log), "%s/probe-%s.log", k3DSRoot, p->name);
        setenv("TSSM_LOG", log, 1);
    }

    // A pixel of the bottom screen's buffer (320 x 240, stored a column at a
    // time, bottom to top), in whatever format it has.
    void BottomPut(u8* fb, GSPGPU_FramebufferFormat format, int x, int y, u32 rgb)
    {
        if (x < 0 || x >= 320 || y < 0 || y >= 240)
        {
            return;
        }
        const size_t at = (size_t)x * 240 + (size_t)(239 - y);
        const u8 r = (u8)(rgb >> 16), g = (u8)(rgb >> 8), b = (u8)rgb;
        switch (format)
        {
        case GSP_RGBA8_OES:
            fb[at * 4 + 0] = 0xFF;
            fb[at * 4 + 1] = b;
            fb[at * 4 + 2] = g;
            fb[at * 4 + 3] = r;
            break;
        case GSP_BGR8_OES:
            fb[at * 3 + 0] = b;
            fb[at * 3 + 1] = g;
            fb[at * 3 + 2] = r;
            break;
        case GSP_RGB565_OES:
            ((u16*)fb)[at] = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            break;
        default:
            break;
        }
    }

    // Text from (8, y), `scale` times the font's 8 pixels, wrapped at spaces;
    // the y after it.
    int BottomText(u8* fb, GSPGPU_FramebufferFormat format, int y, int scale, u32 rgb,
                   const char* text)
    {
        const ConsoleFont& font = consoleGetDefault()->font;
        const int cols = (320 - 16) / (8 * scale);
        const char* p = text;
        while (*p != '\0')
        {
            int n = 0, cut = 0;
            while (p[n] != '\0' && n < cols)
            {
                if (p[n] == ' ')
                {
                    cut = n;
                }
                n++;
            }
            if (p[n] != '\0' && cut > 0)
            {
                n = cut;
            }
            for (int i = 0; i < n; i++)
            {
                const int c = (unsigned char)p[i] - font.asciiOffset;
                if (c < 0 || c >= font.numChars)
                {
                    continue;
                }
                const u8* glyph = font.gfx + 8 * c;
                for (int gy = 0; gy < 8 * scale; gy++)
                {
                    for (int gx = 0; gx < 8 * scale; gx++)
                    {
                        if (glyph[gy / scale] & (0x80 >> (gx / scale)))
                        {
                            BottomPut(fb, format, 8 + i * 8 * scale + gx, y + gy, rgb);
                        }
                    }
                }
            }
            y += 8 * scale + 2;
            p += n;
            while (*p == ' ')
            {
                p++;
            }
        }
        return y;
    }

    void DrawBanner(u8* fb, GSPGPU_FramebufferFormat format, const Probe* p)
    {
        for (int y = 0; y < 240; y++)
        {
            for (int x = 0; x < 320; x++)
            {
                BottomPut(fb, format, x, y, 0x101830);
            }
        }
        char line[160];
        snprintf(line, sizeof(line), "TSSM probe %d", TSSM_3DS_PROBE);
        int y = BottomText(fb, format, 10, 2, 0xFFFFFF, line);
        snprintf(line, sizeof(line), "Tests %s.", p->tests);
        y = BottomText(fb, format, y + 6, 1, 0xC0D0FF, line);
        if (p->chain == CHAIN_EXITLOOP && sChain.nothing)
        {
            y = BottomText(fb, format, y + 10, 2, 0x80FF80, "Probe 9 has finished.");
            y = BottomText(fb, format, y + 6, 1, 0xFFFFFF,
                           "Nothing left to do: back to the Homebrew Launcher in a few seconds.");
        }
        else if (p->chain == CHAIN_EXITLOOP)
        {
            snprintf(line, sizeof(line),
                     "Run %d of up to %d%s. It quits and starts itself again (about 15 minutes in "
                     "all), then goes back to the Homebrew Launcher.",
                     sChain.run, kExitLoopRuns,
                     sChain.resumed ? ", carrying on from last time" : "");
            y = BottomText(fb, format, y + 10, 1, 0xFFFFFF, line);
            if (sChain.resume)
            {
                y = BottomText(fb, format, y + 6, 1, 0xFFD040,
                               "Last run for now. Afterwards: switch the console off and on, then "
                               "start TSSM probe 9 once more. It carries on.");
            }
        }
        else if (p->chain == CHAIN_FPS)
        {
            snprintf(line, sizeof(line),
                     "Run %d of 2 (%s). It plays each level by itself for about a minute, then "
                     "goes back to the Homebrew Launcher.",
                     sChain.run, sChain.run == 1 ? "BB02" : "GG02");
            y = BottomText(fb, format, y + 10, 1, 0xFFFFFF, line);
        }
        else if (p->chain == CHAIN_LADDER)
        {
            y = LadderBanner(fb, format, y);
        }
        else if (p->chain == CHAIN_SPEED)
        {
            snprintf(line, sizeof(line), "Run %d of %d", sChain.run, kNumSpeedRuns);
            y = BottomText(fb, format, y + 8, 2, 0xFFFFFF, line);
            snprintf(line, sizeof(line), "%s.", SpeedThisRun()->what);
            y = BottomText(fb, format, y + 2, 1, 0xFFFFFF, line);
            y = BottomText(fb, format, y + 4, 1, 0xFFFFFF,
                           sChain.only ? "Only this run (only-run.txt), then back to the "
                                         "Homebrew Launcher." :
                                         "It plays by itself, then quits and starts the next "
                                         "run: about " PROBE_SPEED_MINUTES " minutes in all. "
                                         "Leave it alone.");
            if (sChain.last && !sChain.only)
            {
                y = BottomText(fb, format, y + 4, 1, 0x80FF80,
                               TSSM_3DS_PROBE == 16 ?
                                   "The last run. When the Homebrew Launcher is back, probe 16 "
                                   "has finished: restart the console, then open ftpd." :
                               TSSM_3DS_PROBE == 15 ?
                                   "The last run. When the Homebrew Launcher is back, probe 15 "
                                   "has finished: restart the console, then open ftpd." :
                                   "The last run. When the Homebrew Launcher is back, probe 14 "
                                   "has finished: restart the console, then open ftpd.");
            }
        }
        else
        {
            y = BottomText(fb, format, y + 10, 1, 0xFFFFFF,
                           "It plays by itself for about a minute, then goes back to the "
                           "Homebrew Launcher.");
        }
        y = BottomText(fb, format, y + 10, 2, 0xFFD040, "If this freezes,");
        y = BottomText(fb, format, y, 2, 0xFFD040, "hold POWER.");
        y = BottomText(fb, format, y + 6, 1, 0xFFFFFF,
                       p->chain == CHAIN_LADDER ?
                           "(Frozen: nothing moves for 30 seconds and HOME does nothing.) Then "
                           "switch it on and start probe 11 again: it skips that part." :
                       p->chain == CHAIN_SPEED ?
                           "(Frozen: nothing moves for 30 seconds and HOME does nothing.) Note "
                           "the run's number above first." :
                           "(Frozen: nothing moves for 30 seconds and HOME does nothing.) Then "
                           "start the next probe.");
        if (p->chain != CHAIN_NONE)
        {
            snprintf(line, sizeof(line), "Log: 3ds/tssm/%s.log", ChainDirName());
        }
        else
        {
            snprintf(line, sizeof(line), "Log: 3ds/tssm/probe-%s.log", p->name);
        }
        BottomText(fb, format, y + 10, 1, 0x80FF80, line);
    }
} // namespace

static int sProbeBannerShown;

int i3DSProbeBannerShown()
{
    return sProbeBannerShown;
}

void i3DSProbeBanner()
{
    const Probe* p = ThisProbe();
    if (p == NULL || !gspHasGpuRight())
    {
        return;
    }
    sProbeBannerShown = 1;
    // Both of the screen's buffers, so that a swap shows it either way;
    // nothing else draws on the bottom screen unless the overlay is on.
    const GSPGPU_FramebufferFormat format = gfxGetScreenFormat(GFX_BOTTOM);
    for (int i = 0; i < 2; i++)
    {
        u8* fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        if (fb != NULL)
        {
            DrawBanner(fb, format, p);
        }
        gfxFlushBuffers();
        gfxScreenSwapBuffers(GFX_BOTTOM, false);
        gspWaitForVBlank();
    }
    printf("tssm: probe %d: the bottom screen names it\n", TSSM_3DS_PROBE);
    fflush(stdout);
}

void i3DSLadderCheckpoint()
{
#if TSSM_3DS_PROBE == 11
    if (!sL.stopBeforeGame)
    {
        return;
    }
    // L3 and L4: the port's start-up is done and the game has not begun;
    // 2 s of events (SDL's pump runs the APT main loop), then the normal
    // quit.
    printf("tssm: probe 11: layer %s: the port's start-up is done, before the game; 2 s of "
           "events, then the normal quit\n",
           LayerName(sL.layer));
    fflush(stdout);
    const u64 end = svcGetSystemTick() + 2ull * SYSCLOCK_ARM11;
    while (svcGetSystemTick() < end)
    {
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
        svcSleepThread(16666667ll);
    }
    exit(0);
#endif
}

uint64_t i3DSMonotonicNs()
{
    // The system tick runs at SYSCLOCK_ARM11 whatever the CPU's speed, and
    // nothing the user does moves it. Split so that ticks * 1e9 cannot
    // overflow (it would after about 68 seconds).
    const uint64_t t = svcGetSystemTick();
    const uint64_t hz = SYSCLOCK_ARM11;
    return (t / hz) * 1000000000ull + ((t % hz) * 1000000000ull) / hz;
}

void i3DSSleepNs(uint64_t ns)
{
    svcSleepThread((s64)ns);
}

void i3DSEarly()
{
    // RomFS: the port's own files packed into the .3dsx or the .cia (the
    // license texts). Never the game's.
    sRomfsResult = romfsInit();
    sRomfs = R_SUCCEEDED(sRomfsResult);
    // The thread i3DSQuit stops the others from.
    svcGetThreadId(&sMainThreadId, CUR_THREAD_HANDLE);

    u64 programId = 0;
    sInstalled = R_SUCCEEDED(APT_GetProgramID(&programId)) && programId == kCiaTitleId;

    bool isNew = false;
    if (R_SUCCEEDED(APT_CheckNew3DS(&isNew)))
    {
        sNew3DS = isNew;
    }
    if (!sNew3DS)
    {
        Old3DSScreen();
        exit(0);
    }

    // What earlier programs left behind (the start-up's held line), and the
    // time limit to put back at the quit.
    MeasureHeld(&sHeldAtStart);
    sCpuLimitKnown = R_SUCCEEDED(APT_GetAppCpuTimeLimit(&sCpuLimitAtStart));

    // A self-starting probe's run, then a probe's switches and log name,
    // before the log opens.
    ChainPlan();
    ApplyProbe();

    // 804 MHz and the L2 cache. A .3dsx starts at 268 MHz unless it asks;
    // SDL's own main would ask, but the game's main is its own.
    osSetSpeedupEnable(true);

    // Sent with `3dslink -s`: stdout and stderr go back to the PC as well as
    // to last_run.log. The socket is the log tee's to write to (iLog.cpp),
    // not a redirection of its own, so the log still gets everything. The PC
    // can also pull last_run.log from TCP 28772 (StartLogServer), when
    // TSSM_3DS_LOG_SERVER=on (i3DSStartup).
    if (__3dslink_host.s_addr != 0)
    {
        StartSockets(true);
    }
}

void i3DSStartup()
{
#ifdef RW_C3D
    // The renderer asks the game once a frame where the 3D's eyes converge
    // (not in the speed test, which draws nothing).
    rw::c3d::setStereoFocusHook(iStereo3DSFocus);
#endif
    // What a first hardware session needs to know: the memory a .3dsx really
    // gets on the console it runs on, and how libctru split it.
    const u32 appTotal = osGetMemRegionSize(MEMREGION_APPLICATION);
    const u32 appUsed = osGetMemRegionUsed(MEMREGION_APPLICATION);
    const struct mallinfo mi = mallinfo();
    printf("tssm: 3ds: New 3DS family, %s, system %lu.%lu.%lu\n",
           osIsHeadsetConnected() ? "headphones in" : "speakers",
           (unsigned long)GET_VERSION_MAJOR(osGetFirmVersion()),
           (unsigned long)GET_VERSION_MINOR(osGetFirmVersion()),
           (unsigned long)GET_VERSION_REVISION(osGetFirmVersion()));
    printf("tssm: 3ds: running as %s, closing returns to the %s\n", ProgramName(), i3DSMenuName());
    // As the kernel reports them. Azahar answers with numbers that do not add
    // up (more used than the region holds), so nothing is derived from them.
    printf("tssm: 3ds: application memory region %lu KB, %lu KB of it in use\n",
           (unsigned long)(appTotal >> 10), (unsigned long)(appUsed >> 10));
    // libctru's split of it, made before main: the heap malloc draws from and
    // the linear heap the GPU and the DSP read (nothing has used that yet).
    printf("tssm: 3ds: heap %lu KB at %#lx (malloc has %lu KB in use, %lu KB obtained), linear "
           "heap %lu KB at %#lx\n",
           (unsigned long)(envGetHeapSize() >> 10), (unsigned long)__ctru_heap,
           (unsigned long)(mi.uordblks >> 10), (unsigned long)(mi.arena >> 10),
           (unsigned long)(envGetLinearHeapSize() >> 10), (unsigned long)__ctru_linear_heap);
    // A program that quit and was never freed keeps its image's memory and
    // one of the kernel's 47 process objects; the third or fourth in one
    // power-on panicked the kernel at the next start.
    {
        const Held& h = sHeldAtStart;
        const u32 extra = h.notFreeKB > h.ownKB ? h.notFreeKB - h.ownKB : 0;
        printf("tssm: 3ds: held by earlier programs: %lu KB (not free at start %lu KB, this "
               "program %lu KB), about %d program(s) by the memory; %d process(es) on the "
               "console, %d of them named as this one besides it%s%s%s; %lu s since power-on\n",
               (unsigned long)extra, (unsigned long)h.notFreeKB, (unsigned long)h.ownKB,
               h.byMemory, h.processes, h.sameName, h.names[0] != '\0' ? " (ids " : "", h.names,
               h.names[0] != '\0' ? ")" : "", (unsigned long)SecondsSincePowerOn());
        if (HeldCount(&h) >= 2)
        {
            printf("tssm: 3ds: %d programs that quit were never freed: restart the console "
                   "before starting another program\n",
                   HeldCount(&h));
        }
        if (sCpuLimitKnown)
        {
            printf("tssm: 3ds: the system core's time limit at start: %lu%%\n",
                   (unsigned long)sCpuLimitAtStart);
        }
    }
    if (ThisProbe() != NULL && ThisProbe()->chain != CHAIN_NONE)
    {
        printf("tssm: probe %s: run %d%s, settings %s; %s\n", ThisProbe()->name, sChain.run,
               sChain.last ? " (the last)" : "", sChain.cfg, sChain.note);
    }
    if (!sRomfs)
    {
        printf("tssm: 3ds: romfsInit failed: %#lx\n", (unsigned long)sRomfsResult);
    }

    aptHook(&sHook, OnApt, NULL);
    sHooked = true;
    threadOnException(OnException, sExceptionStack + sizeof(sExceptionStack),
                      WRITE_DATA_TO_HANDLER_STACK);

    // The folder everything lives in, and the working directory, so a relative
    // path means the same however the game was started (hbmenu or 3dslink).
    mkdir(k3DSDataParent, 0777);
    mkdir(k3DSRoot, 0777);
    if (chdir(k3DSRoot) != 0)
    {
        printf("tssm: 3ds: cannot enter %s (%d)\n", k3DSRoot, errno);
    }
    char saves[300];
    snprintf(saves, sizeof(saves), "%s/saves", k3DSRoot);
    RecoverBak(k3DSRoot);
    RecoverBak(saves);

    // Test settings: the console has no environment, so the PC's test hooks
    // (TSSM_INPUT, TSSM_CAPTURE_FRAMES, TSSM_EXIT_AFTER, ...; iCapture.h)
    // read theirs from k3DSRoot/test-env.txt, NAME=VALUE a line ('#' starts a
    // comment). Absent, nothing changes; each one set is logged. Once read it
    // becomes test-env.last.txt, so a file a test leaves behind cannot steer
    // the next normal launch. A self-starting probe reads none: its runs
    // are its own, and the player's file is left alone.
    if (ThisProbe() == NULL || ThisProbe()->chain == CHAIN_NONE)
    {
        char envPath[300];
        snprintf(envPath, sizeof(envPath), "%s/test-env.txt", k3DSRoot);
        FILE* f = fopen(envPath, "r");
        if (f != NULL)
        {
            char line[512];
            while (fgets(line, sizeof(line), f) != NULL)
            {
                size_t n = strlen(line);
                while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' '))
                {
                    line[--n] = '\0';
                }
                char* eq = strchr(line, '=');
                if (line[0] == '#' || line[0] == '\0' || eq == NULL || eq == line)
                {
                    continue;
                }
                *eq = '\0';
                setenv(line, eq + 1, 1);
                printf("tssm: 3ds: test-env.txt: %s=%s\n", line, eq + 1);
            }
            fclose(f);

            char lastPath[300];
            snprintf(lastPath, sizeof(lastPath), "%s/test-env.last.txt", k3DSRoot);
            remove(lastPath); // the SD card's rename does not replace a file
            if (rename(envPath, lastPath) != 0 && remove(envPath) != 0)
            {
                printf("tssm: 3ds: cannot rename or remove %s (%d)\n", envPath, errno);
            }
            else
            {
                printf("tssm: 3ds: test-env.txt is for this run only, now test-env.last.txt\n");
            }
        }
    }
    if (ThisProbe() != NULL)
    {
        ApplyProbe();
        printf("tssm: probe %d (%s): tests %s; switches: %s;%s\n", TSSM_3DS_PROBE,
               ThisProbe()->name, ThisProbe()->tests,
               ThisProbe()->chain == CHAIN_EXITLOOP || ThisProbe()->chain == CHAIN_LADDER ?
                   "TSSM_INTRO_MOVIES=off;TSSM_3DS_GUI=off" :
               ThisProbe()->chain != CHAIN_NONE ? kChainBase :
                                                  kProbeBase,
               ThisProbe()->env);
        fflush(stdout);
    }

    // TSSM_3DS_EXITFIX=off: the old quit (the exit-loop probe's comparison).
    {
        const char* fix = getenv("TSSM_3DS_EXITFIX");
        sExitFix = !(fix != NULL && (strcmp(fix, "off") == 0 || strcmp(fix, "0") == 0));
        printf("tssm: 3ds: the exit fix is %s\n",
               sExitFix ? "on" : "OFF (TSSM_3DS_EXITFIX=off: the old quit)");
        // Its cache part on its own (and off with the rest).
        const char* own = getenv("TSSM_3DS_OWNCACHE");
        sOwnCache = sExitFix && !(own != NULL && (strcmp(own, "off") == 0 || strcmp(own, "0") == 0));
        printf("tssm: 3ds: cache flushes for the DSP and the GPU: %s\n",
               sOwnCache ? "by the game itself (TSSM_3DS_OWNCACHE=off: by the services)"
                         : "by the services, as the old quit did");
    }
    // The sockets only when something uses them: off, the program gives the
    // SOC service no memory block at all. The old quit always had them.
    if (LogServerWanted() || !sExitFix)
    {
        StartSockets(false);
    }
    {
        char server[96] = "off (TSSM_3DS_LOG_SERVER=on in test-env.txt turns it on)";
        if (sLogServer >= 0)
        {
            snprintf(server, sizeof(server), "on TCP %d at %s", kLogServerPort,
                     sAddress[0] != '\0' ? sAddress : "(no network address yet)");
        }
        printf("tssm: 3ds: romfs %s; 3dslink %s; log server %s\n",
               sRomfs ? "mounted" : "not mounted", sLinkSocket >= 0 ? "connected" : "not used",
               server);
    }
    // A self-starting probe's other audio (SDL_AUDIO_DRIVER=dummy: SDL's
    // silent driver, its thread on core 1 as ndsp's driver has, no DSP).
    {
        const char* drv = getenv("SDL_AUDIO_DRIVER");
        if (drv != NULL && drv[0] != '\0')
        {
            SDL_SetHint(SDL_HINT_AUDIO_DRIVER, drv);
            printf("tssm: 3ds: audio: SDL's %s driver (SDL_AUDIO_DRIVER)\n", drv);
        }
    }

    // The player's own Xbox game files, laid out as on every other platform.
    // TSSM_ASSETS wins over config.ini's [assets] path, as on Android and the
    // Switch; without the folder, config.ini decides.
    const char* env = getenv("TSSM_ASSETS");
    if (env == NULL || env[0] == '\0')
    {
        char files[256];
        snprintf(files, sizeof(files), "%s/files", k3DSRoot);
        struct stat st;
        if (stat(files, &st) == 0 && S_ISDIR(st.st_mode))
        {
            setenv("TSSM_ASSETS", files, 1);
            printf("tssm: 3ds: game files: %s\n", files);
        }
        else
        {
            printf("tssm: 3ds: no %s; copy the game's files there (docs/3DS.md)\n", files);
        }
    }

    // Without the console's DSP firmware dump there is no sound (SDL's audio
    // opens ndsp, which loads it).
    struct stat dsp;
    if (stat("sdmc:/3ds/dspfirm.cdc", &dsp) != 0)
    {
        printf("tssm: 3ds: no sdmc:/3ds/dspfirm.cdc, so no sound. Make it once from the "
               "Rosalina menu (L + Down + SELECT): Miscellaneous options, Dump DSP firmware "
               "(docs/3DS.md)\n");
        // ndsp without its firmware never asks for sound, and closing that
        // device waits forever (a movie with sound hung there). SDL's silent
        // driver keeps time instead.
        if (SDL_getenv("SDL_AUDIO_DRIVER") == NULL)
        {
            SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
            printf("tssm: 3ds: audio: SDL's silent driver\n");
        }
    }
    fflush(stdout);
}

void i3DSServeFile(const char* path)
{
    snprintf(sServed, sizeof(sServed), "%s", path != NULL ? path : "");
}

void i3DSHoldHome(int hold)
{
    sHomeHolds += hold ? 1 : -1;
    if (sHomeHolds < 0)
    {
        sHomeHolds = 0;
    }
    const bool allowed = sHomeHolds == 0;
    aptSetHomeAllowed(allowed);
    aptSetSleepAllowed(allowed);
}

void i3DSErrorBox(const char* title, const char* message)
{
    const bool hadGfx = BoxOpen();
    BoxLine("\n %s\n\n", title != NULL ? title : "The SpongeBob SquarePants Movie");
    BoxWrap(message);
    BoxLine("\n Press START to close.\n");
    BoxWait(hadGfx);
}

void i3DSGameFilesScreen(const char* folder, const char* file, int unreadable, int fromConfig)
{
    char hint[600];
    bool empty = false;
    FolderHint(folder, hint, sizeof(hint), &empty);
    const bool noFolder = file == NULL;
    const char* card = OnCard(folder);

    const bool hadGfx = BoxOpen();
    gfxSetDoubleBuffering(GFX_TOP, false);

    // The top screen: what is wrong and where the files go, big.
    u8* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    const GSPGPU_FramebufferFormat format = gfxGetScreenFormat(GFX_TOP);
    if (fb != NULL)
    {
        for (int x = 0; x < 400; x++)
        {
            for (int y = 0; y < 240; y++)
            {
                TopPut(fb, format, x, y, 0x10204A);
            }
        }
        TopText(fb, format, 18, 2, 0xFFE14D,
                unreadable ? "Game files can't be read" : "Game files not found");
        TopText(fb, format, 60, 2, 0xFFFFFF, "Copy the files of your");
        TopText(fb, format, 80, 2, 0xFFFFFF, "Xbox game disc to the");
        TopText(fb, format, 100, 2, 0xFFFFFF, "SD card's folder");
        const int len = (int)strlen(card);
        const int scale = len <= 16 ? 3 : len <= 24 ? 2 : 1;
        TopText(fb, format, 132 + (3 - scale) * 4, scale, 0x7FD8FF, card);
        TopText(fb, format, 172, 1, 0xA8B8D0, "More on the bottom screen");
        TopText(fb, format, 200, 2, 0xFFFFFF, "Press START to close");
        gfxFlushBuffers();
    }

    // The bottom screen: the details.
    char text[1400];
    BoxLine("\n" TSSM_3DS_SCREEN_NAME(" ") "\n");
    if (noFolder)
    {
        snprintf(text, sizeof(text), "There is no %s folder on the SD card.", card);
    }
    else if (unreadable)
    {
        snprintf(text, sizeof(text), "%s is in %s, but cannot be read: copy it again.", file, card);
    }
    else if (empty)
    {
        snprintf(text, sizeof(text), "The %s folder is empty.", card);
    }
    else
    {
        snprintf(text, sizeof(text), "%s is not in the %s folder.", file, card);
    }
    BoxLine("\x1b[33;1m");
    BoxWrap(text);
    BoxLine("\x1b[0m\n");
    snprintf(text, sizeof(text),
             "Put the SD card in a PC and copy the files into the SD card's %s folder. Make the "
             "folder if it is not there.\n\n"
             "What goes in: the files of your Xbox game disc, as they are at the top of the "
             "disc: boot.HIP, font.HIP, plat.HIP, the level folders (B1, BB, JK, MN, ...) and "
             "FMV. Not the folder above them, and not a disc image.",
             card);
    BoxWrap(text);
    if (hint[0] != '\0')
    {
        BoxLine("\n\x1b[33;1m");
        BoxWrap(hint);
        BoxLine("\x1b[0m");
    }
    if (fromConfig)
    {
        BoxLine("\n");
        BoxWrap("config.ini's [assets] path names this folder. Empty it to use 3ds/tssm/files.");
    }
    BoxLine("\n Press START to close.\n");
    BoxWait(hadGfx);
}

const char* i3DSMenuName()
{
    return sInstalled ? "HOME Menu" : "Homebrew Launcher";
}

void i3DSCrashLog(const char* what)
{
    const int fd = OpenCrashLog();
    if (fd < 0)
    {
        return;
    }
    time_t now = time(NULL);
    struct tm t;
    memset(&t, 0, sizeof(t));
    localtime_r(&now, &t);
    char line[700];
    int n = snprintf(line, sizeof(line),
                     "\n==== %04d-%02d-%02d %02d:%02d:%02d  %s (built %s %s)\n%s\n"
                     "the backtrace is in %s\n",
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
                     ProgramName(), __DATE__, __TIME__, what,
                     i3DSLogPath()[0] != '\0' ? i3DSLogPath() : "the game's output");
    if (n > 0)
    {
        ssize_t wrote =
            write(fd, line, (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line) - 1));
        (void)wrote;
    }
    fsync(fd);
    close(fd);
}
