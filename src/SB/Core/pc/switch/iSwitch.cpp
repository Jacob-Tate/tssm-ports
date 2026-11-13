// The Nintendo Switch homebrew build's start-up, applet and crash code. See
// iSwitch.h; docs/SWITCH.md.

// The libnx side (iSwitch.h): nothing of the game's is included here.
#include "iSwitch.h"

// struct in_addr, which libnx's nxlink.h uses without including.
#include <netinet/in.h>

#include <switch.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/iosupport.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// The linker script's name for the image's first byte (libnx switch.ld).
extern "C" char __start__[];

namespace
{
    int sNxlinkSocket = -1;
    int sLogServer = -1; // the pull-log server's listening socket
    bool sSocketsUp;
    bool sRomfs;
    Result sRomfsResult;
    bool sAppletOverride; // applet mode, and the player chose to go on

    // ---------------------------------------------------------------- applet

    // Library-applet mode (hbmenu opened from the Album) has a fraction of the
    // console's memory. The game's pools alone are about 200 MB and Mesa,
    // FFmpeg and the textures come on top, so it is refused with a message on
    // the console's own text screen, before SDL or the log exist.
    bool InAppletMode()
    {
        const AppletType t = appletGetAppletType();
        return t == AppletType_LibraryApplet || t == AppletType_OverlayApplet;
    }

    // True: go on anyway.
    bool AppletModeScreen()
    {
        // consoleInit takes stdout over; put back whatever was there after.
        const devoptab_t* out = devoptab_list[STD_OUT];
        const devoptab_t* err = devoptab_list[STD_ERR];

        consoleInit(NULL);
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        PadState pad;
        padInitializeDefault(&pad);

        printf("\n\n"
               "  The SpongeBob SquarePants Movie (PC port) cannot run in applet mode.\n"
               "\n"
               "  Opened from the Album, homebrew gets only a small part of the\n"
               "  console's memory, and the game needs much more.\n"
               "\n"
               "  Start it with full memory instead: hold R while you start any\n"
               "  game from the HOME menu, then pick TSSM in the Homebrew Menu.\n"
               "\n"
               "\n"
               "  Press + to exit.\n"
               "  (Press X to try anyway. It will most likely run out of memory.)\n");
        consoleUpdate(NULL);

        bool goOn = false;
        while (appletMainLoop())
        {
            padUpdate(&pad);
            const u64 down = padGetButtonsDown(&pad);
            if (down & HidNpadButton_Plus)
            {
                break;
            }
            if (down & HidNpadButton_X)
            {
                goOn = true;
                break;
            }
            consoleUpdate(NULL);
        }

        consoleExit(NULL);
        devoptab_list[STD_OUT] = out;
        devoptab_list[STD_ERR] = err;
        return goOn;
    }

    // ---------------------------------------------------------- HOME and sleep

    AppletHookCookie sHook;
    bool sAway;

    // SDL's Switch video driver sends no background or foreground events, so
    // the hook stands in for SDL_EVENT_WILL_ENTER_BACKGROUND and
    // DID_ENTER_FOREGROUND (iPadHostSDL.cpp's WatchBackground on Android): the
    // game clock stops while the HOME menu is open, a library applet (the
    // keyboard, the controller screen) is in front, or the console sleeps, so
    // the first frame back is an ordinary frame and not the whole absence.
    // Coming back restarts the audio devices, as Android's foreground does.
    //
    // Runs on the game thread: SDL pumps applet messages (appletMainLoop) from
    // SDL_PumpEvents.
    void Away(const char* why)
    {
        if (!sAway)
        {
            sAway = true;
            iSwitchGameAway();
            printf("tssm: switch: %s; the game clock is paused\n", why);
        }
    }

    void Back(const char* why)
    {
        if (sAway)
        {
            sAway = false;
            iSwitchGameBack();
            printf("tssm: switch: %s; the game clock runs again\n", why);
        }
    }

    void OnApplet(AppletHookType hook, void*)
    {
        switch (hook)
        {
        case AppletHookType_OnFocusState:
            switch (appletGetFocusState())
            {
            case AppletFocusState_InFocus:
                Back("back in focus");
                break;
            case AppletFocusState_OutOfFocus:
                Away("an applet is in front");
                break;
            case AppletFocusState_Background:
                Away("the HOME menu is open or the console is asleep");
                break;
            default:
                break;
            }
            break;
        case AppletHookType_OnResume:
            Back("woke from sleep");
            break;
        case AppletHookType_OnOperationMode:
            // The window gets a resize event from SDL; the render size is fixed
            // at boot and the final blit scales (TODO(switch): re-create the
            // render targets for the new size).
            printf("tssm: switch: now %s\n",
                   appletGetOperationMode() == AppletOperationMode_Console ? "docked"
                                                                            : "handheld");
            break;
        default:
            break;
        }
    }

    // ------------------------------------------------------------------ crash

    void Line(int fd, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

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

    bool Readable(u64 addr)
    {
        MemoryInfo info;
        u32 page = 0;
        return addr != 0 && (addr & 7) == 0 && R_SUCCEEDED(svcQueryMemory(&info, &page, addr)) &&
               (info.perm & Perm_R) != 0 && info.type != MemType_Unmapped;
    }

    int OpenCrashLog()
    {
        const char* path = iSwitchCrashLogPath();
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
        Line(fd, "\n==== %04d-%02d-%02d %02d:%02d:%02d  tssm.nro (built %s %s)\n%s\n",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, __DATE__,
             __TIME__, what);
    }
} // namespace

// libnx calls this on the faulting thread's behalf, on the stack below, for a
// data or instruction abort, a bad SVC and the like. Everything is formatted on
// the stack: the heap may be what failed. Returning hands the exception back to
// the kernel, which ends the process; Atmosphere then writes its own report to
// sd:/atmosphere/crash_reports/ as well.
//
// TODO(switch): confirm on hardware that hbloader's process takes this path
// (it should: libnx installs the entry for every homebrew).
extern "C"
{
    alignas(16) u8 __nx_exception_stack[0x8000];
    u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

    void __libnx_exception_handler(ThreadExceptionDump* ctx)
    {
        const u64 base = (u64)iSwitchImageBase();
        const int fd = OpenCrashLog();

        const char* kind = "exception";
        switch (ctx->error_desc)
        {
        case ThreadExceptionDesc_InstructionAbort:
            kind = "instruction abort";
            break;
        case ThreadExceptionDesc_MisalignedPC:
            kind = "misaligned PC";
            break;
        case ThreadExceptionDesc_MisalignedSP:
            kind = "misaligned SP";
            break;
        case ThreadExceptionDesc_SError:
            kind = "SError";
            break;
        case ThreadExceptionDesc_BadSVC:
            kind = "bad SVC";
            break;
        case ThreadExceptionDesc_Trap:
            kind = "trap";
            break;
        case ThreadExceptionDesc_Other:
            kind = "data abort or other";
            break;
        default:
            break;
        }

        char what[160];
        snprintf(what, sizeof(what), "tssm: CRASH -- %s (desc %#x) at pc %#lx [tssm.elf+%#lx]",
                 kind, (unsigned)ctx->error_desc, (unsigned long)ctx->pc.x,
                 (unsigned long)(ctx->pc.x - base));
        Stamp(fd, what);
        Line(fd, "tssm:   far %#lx  esr %#x  lr %#lx [+%#lx]  sp %#lx  fp %#lx\n",
             (unsigned long)ctx->far.x, (unsigned)ctx->esr, (unsigned long)ctx->lr.x,
             (unsigned long)(ctx->lr.x - base), (unsigned long)ctx->sp.x,
             (unsigned long)ctx->fp.x);
        for (int i = 0; i < 29; i += 4)
        {
            Line(fd, "tssm:   x%-2d %016lx", i, (unsigned long)ctx->cpu_gprs[i].x);
            for (int j = i + 1; j < i + 4 && j < 29; j++)
            {
                Line(fd, "  x%-2d %016lx", j, (unsigned long)ctx->cpu_gprs[j].x);
            }
            Line(fd, "\n");
        }

        // The frame-pointer chain: each frame record is {previous fp, return
        // address}. Every step is checked readable first.
        Line(fd, "tssm:   frames (addr2line -f -C -e tssm.elf <offset>):\n");
        Line(fd, "tssm:   #0  [tssm.elf+%#lx]\n", (unsigned long)(ctx->pc.x - base));
        Line(fd, "tssm:   #1  [tssm.elf+%#lx] (lr)\n", (unsigned long)(ctx->lr.x - base));
        u64 fp = ctx->fp.x;
        for (int depth = 2; depth < 34 && Readable(fp) && Readable(fp + 8); depth++)
        {
            const u64* rec = (const u64*)(uintptr_t)fp;
            const u64 ret = rec[1];
            if (ret == 0)
            {
                break;
            }
            Line(fd, "tssm:   #%-2d [tssm.elf+%#lx]\n", depth, (unsigned long)(ret - base));
            if (rec[0] <= fp)
            {
                break;
            }
            fp = rec[0];
        }

        if (fd >= 0)
        {
            fsync(fd);
            close(fd);
            printf("tssm: this report is also in %s\n", iSwitchCrashLogPath());
        }
        iSwitchLogDrain(500);
    }
}

void iSwitchCrashLog(const char* what)
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
                     "\n==== %04d-%02d-%02d %02d:%02d:%02d  tssm.nro (built %s %s)\n%s\n"
                     "the backtrace is in %s\n",
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
                     __DATE__, __TIME__, what,
                     iSwitchLogPath()[0] != '\0' ? iSwitchLogPath() : "the game's output");
    if (n > 0)
    {
        ssize_t wrote = write(fd, line, (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line) - 1));
        (void)wrote;
    }
    fsync(fd);
    close(fd);
}

uint64_t iSwitchMonotonicNs()
{
    // The system counter (19.2 MHz), which nothing the user does can move and
    // which keeps counting through sleep -- the applet hook above is what keeps
    // a nap out of the game clock.
    return armTicksToNs(armGetSystemTick());
}

void iSwitchSleepNs(uint64_t ns)
{
    svcSleepThread((s64)ns);
}

namespace
{
    // nxlinkConnectToHost() blocks until TCP gives up when the PC's firewall
    // drops the connection, with nothing on screen yet. Give it a few seconds.
    int ConnectNxlink(int ms)
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
        {
            return -1;
        }
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(NXLINK_CLIENT_PORT);
        sa.sin_addr = __nxlink_host;
        fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
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
        fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) & ~O_NONBLOCK);
        return sock;
    }

    // The PC pulls the log instead of nxlink pushing it: Windows Firewall blocks
    // nxlink's incoming connection by default, but not the PC connecting out.
    // Whoever connects to TCP 28772 gets last_run.log as it is now; the
    // connection then closes. A test-build convenience on the local network: it
    // sends only the game's own log and reads nothing from the network.
    constexpr int kLogServerPort = 28772;

    void* LogServerThread(void*)
    {
        for (;;)
        {
            const int ls = sLogServer;
            if (ls < 0)
            {
                break;
            }
            const int c = accept(ls, NULL, NULL);
            if (c < 0)
            {
                if (sLogServer < 0)
                {
                    break;
                }
                svcSleepThread(100000000ull);
                continue;
            }
            FILE* f = fopen("last_run.log", "rb");
            if (f != NULL)
            {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
                {
                    size_t off = 0;
                    while (off < n)
                    {
                        const ssize_t w = send(c, buf + off, n - off, 0);
                        if (w <= 0)
                        {
                            break;
                        }
                        off += (size_t)w;
                    }
                    if (off < n)
                    {
                        break;
                    }
                }
                fclose(f);
            }
            close(c);
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
        int one = 1;
        setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(kLogServerPort);
        sa.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(ls, (struct sockaddr*)&sa, sizeof(sa)) != 0 || listen(ls, 2) != 0)
        {
            close(ls);
            return;
        }
        sLogServer = ls;
        pthread_t t;
        if (pthread_create(&t, NULL, LogServerThread, NULL) == 0)
        {
            pthread_detach(t);
        }
        else
        {
            sLogServer = -1;
            close(ls);
        }
    }
} // namespace

uintptr_t iSwitchImageBase()
{
    return (uintptr_t)__start__;
}

int iSwitchNxlinkSocket()
{
    return sNxlinkSocket;
}

void iSwitchEarly()
{
    // RomFS: the port's own files packed into the .nro (CMakeLists.txt). Never
    // the game's.
    sRomfsResult = romfsInit();
    sRomfs = R_SUCCEEDED(sRomfsResult);

    // Sent with `nxlink -s`: stdout and stderr go back to the PC as well as to
    // last_run.log. The socket is the log tee's to write to (iLog.cpp), not a
    // redirection of its own, so the log still gets everything. The PC can also
    // pull last_run.log from TCP 28772 (StartLogServer).
    if (R_SUCCEEDED(socketInitializeDefault()))
    {
        sSocketsUp = true;
        if (__nxlink_host.s_addr != 0)
        {
            sNxlinkSocket = ConnectNxlink(3000);
        }
        StartLogServer();
    }

    if (InAppletMode())
    {
        if (!AppletModeScreen())
        {
            exit(0);
        }
        sAppletOverride = true;
    }
}

void iSwitchStartup()
{
    u64 total = 0;
    u64 used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);

    const AppletType type = appletGetAppletType();
    printf("tssm: switch: applet type %d%s, %s, memory %llu MB of %llu MB in use, "
           "system %u.%u.%u%s\n",
           (int)type, sAppletOverride ? " (applet mode, continued anyway)" : "",
           appletGetOperationMode() == AppletOperationMode_Console ? "docked" : "handheld",
           (unsigned long long)(used >> 20), (unsigned long long)(total >> 20),
           (unsigned)HOSVER_MAJOR(hosversionGet()), (unsigned)HOSVER_MINOR(hosversionGet()),
           (unsigned)HOSVER_MICRO(hosversionGet()), hosversionIsAtmosphere() ? ", Atmosphere" : "");
    printf("tssm: switch: romfs %s; nxlink %s\n", sRomfs ? "mounted" : "not mounted",
           sNxlinkSocket >= 0 ? "connected" : "not used");
    if (!sRomfs)
    {
        printf("tssm: switch: romfsInit failed: %#x\n", sRomfsResult);
    }

    appletHook(&sHook, OnApplet, NULL);

    // The folder everything lives in, and the working directory, so a relative
    // path means the same however the game was started (hbmenu or nxlink).
    mkdir(kSwitchDataParent, 0777);
    mkdir(kSwitchRoot, 0777);
    if (chdir(kSwitchRoot) != 0)
    {
        printf("tssm: switch: cannot enter %s (%d)\n", kSwitchRoot, errno);
    }

    // The game files, laid out as the Android port's assets folder. TSSM_ASSETS
    // wins over config.ini's [assets] path, as on Android; without the folder,
    // config.ini decides.
    const char* env = getenv("TSSM_ASSETS");
    if (env == NULL || env[0] == '\0')
    {
        char assets[256];
        snprintf(assets, sizeof(assets), "%s/assets", kSwitchRoot);
        struct stat st;
        if (stat(assets, &st) == 0 && S_ISDIR(st.st_mode))
        {
            setenv("TSSM_ASSETS", assets, 1);
            printf("tssm: switch: game files: %s\n", assets);
        }
        else
        {
            printf("tssm: switch: no %s; copy the game's files there (docs/SWITCH.md)\n",
                   assets);
        }
    }
    fflush(stdout);
}

void iSwitchErrorBox(const char* title, const char* message)
{
    const AppletType t = appletGetAppletType();
    if (t != AppletType_Application && t != AppletType_SystemApplication)
    {
        return;
    }

    ErrorApplicationConfig c;
    if (R_SUCCEEDED(errorApplicationCreate(&c, title != NULL ? title : "The SpongeBob SquarePants Movie",
                                           message != NULL ? message : "")))
    {
        errorApplicationShow(&c);
    }
}

// libnx calls this as the process exits.
extern "C" void userAppExit(void)
{
    if (sLogServer >= 0)
    {
        const int ls = sLogServer;
        sLogServer = -1;
        close(ls);
    }
    if (sNxlinkSocket >= 0)
    {
        // Unhooked from the log tee before it goes.
        const int fd = sNxlinkSocket;
        sNxlinkSocket = -1;
        close(fd);
    }
    if (sSocketsUp)
    {
        socketExit();
        sSocketsUp = false;
    }
    if (sRomfs)
    {
        romfsExit();
        sRomfs = false;
    }
}
