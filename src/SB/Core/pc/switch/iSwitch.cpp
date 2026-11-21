// The Nintendo Switch homebrew build's start-up, applet and crash code. See
// iSwitch.h; docs/SWITCH.md.

// The libnx side (iSwitch.h): nothing of the game's is included here.
#include "iSwitch.h"

// struct in_addr, which libnx's nxlink.h uses without including.
#include <netinet/in.h>

#include <switch.h>

#include <SDL3/SDL.h>

#ifdef TSSM_UPDATE
#include "iUpdate.h"
#endif

#include <arpa/inet.h>
#include <atomic>
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
#include <sys/time.h>
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
               "  The SpongeBob SquarePants Movie (port) cannot run in applet mode.\n"
               "\n"
               "  Opened from the Album, homebrew gets only a small part of the\n"
               "  console's memory, and the game needs much more.\n"
               "\n"
               "  Start it with full memory instead: hold R while you start any\n"
               "  game from the HOME menu, then pick TSSM in the Homebrew Menu.\n"
               "\n"
               "\n"
               "  Press + to return to the Homebrew Menu.\n"
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
    bool sHooked;
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
    // connection then closes. It sends only the game's own log and reads
    // nothing from the network, and only to a test: started by nxlink.
    constexpr int kLogServerPort = 28772;

    // The exit has to stop and join this thread before socketExit: closing
    // the socket need not wake a blocked accept(), and socketExit then waits
    // for that accept for ever. So the listener is polled with a timeout and
    // a stop flag, and a send to a stalled PC gives up.
    std::atomic<bool> sLogStop;
    pthread_t sLogThread;
    bool sLogThreadUp;

    void* LogServerThread(void*)
    {
        const int ls = sLogServer;
        while (!sLogStop.load())
        {
            struct pollfd pfd = { ls, POLLIN, 0 };
            const int pr = poll(&pfd, 1, 200);
            if (pr == 0)
            {
                continue;
            }
            // A dead listener (Wi-Fi lost, sleep) returns at once every
            // time, so back off instead of spinning through the session.
            const int c = (pr > 0 && (pfd.revents & POLLIN) != 0) ? accept(ls, NULL, NULL) : -1;
            if (c < 0)
            {
                svcSleepThread(100000000LL);
                continue;
            }
            fcntl(c, F_SETFL, fcntl(c, F_GETFL, 0) & ~O_NONBLOCK);
            struct timeval tv = { 2, 0 };
            setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            FILE* f = fopen("last_run.log", "rb");
            if (f != NULL)
            {
                char buf[4096];
                size_t n;
                while (!sLogStop.load() && (n = fread(buf, 1, sizeof(buf), f)) > 0)
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
        fcntl(ls, F_SETFL, fcntl(ls, F_GETFL, 0) | O_NONBLOCK);
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
        sLogStop = false;
        // Joined at the exit, never detached: the join is what unmaps its
        // stack (libnx's pthread_detach does nothing).
        if (pthread_create(&sLogThread, NULL, LogServerThread, NULL) == 0)
        {
            sLogThreadUp = true;
        }
        else
        {
            sLogServer = -1;
            close(ls);
        }
    }

    // Bounded by the poll timeout plus the send timeout, about 2.2 s.
    void StopLogServer()
    {
        if (sLogThreadUp)
        {
            sLogStop = true;
            pthread_join(sLogThread, NULL);
            sLogThreadUp = false;
        }
        if (sLogServer >= 0)
        {
            close(sLogServer);
            sLogServer = -1;
        }
    }

    // ------------------------------------------------------------------- quit

    // Set once iSwitchQuit has stopped everything that runs or draws for the
    // game; userAppExit returns to hbloader only then.
    bool sGameStopped;

    // The step the quit is in, for the watchdog's report.
    std::atomic<const char*> sQuitStep{ "" };

    // A step that never finishes (a GPU wait in Mesa, an audio join) would
    // leave the console frozen on the last frame. After this long the
    // watchdog names the step in crash.log and ends the process, which goes
    // back to HOME instead.
    constexpr int kQuitLimitMs = 10000;

    Thread sWatchdog;
    bool sWatchdogUp;
    std::atomic<bool> sWatchdogStop;

    void Watchdog(void*)
    {
        for (int waited = 0; waited < kQuitLimitMs; waited += 50)
        {
            if (sWatchdogStop.load())
            {
                return;
            }
            svcSleepThread(50000000LL);
        }
        // Not through stdout or the log: the stuck thread may hold their lock.
        const int fd = OpenCrashLog();
        if (fd >= 0)
        {
            char line[300];
            const int n = snprintf(line, sizeof(line),
                                   "\ntssm: switch: quit: stuck at \"%s\" for %d s; ending the "
                                   "process\n",
                                   sQuitStep.load(), kQuitLimitMs / 1000);
            if (n > 0)
            {
                ssize_t wrote =
                    write(fd, line, (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line) - 1));
                (void)wrote;
            }
            fsync(fd);
            close(fd);
        }
        svcExitProcess();
    }

    void StartWatchdog()
    {
        sWatchdogStop = false;
        // Above the game thread's priority (0x2C), so a busy wait cannot
        // starve it.
        if (R_SUCCEEDED(threadCreate(&sWatchdog, Watchdog, NULL, NULL, 0x10000, 0x2B, -2)))
        {
            if (R_SUCCEEDED(threadStart(&sWatchdog)))
            {
                sWatchdogUp = true;
                return;
            }
            threadClose(&sWatchdog);
        }
        iSwitchQuitLine("no watchdog thread; the quit has no time limit");
    }

    void StopWatchdog()
    {
        if (sWatchdogUp)
        {
            sWatchdogStop = true;
            threadWaitForExit(&sWatchdog);
            threadClose(&sWatchdog);
            sWatchdogUp = false;
        }
    }

    // What runs beside the game thread on the libnx side: the applet hook
    // (a focus message would restart the audio) and the log server.
    void StopBackground()
    {
        if (sHooked)
        {
            appletUnhook(&sHook);
            sHooked = false;
        }
        StopLogServer();
    }
} // namespace

void iSwitchQuitLine(const char* fmt, ...)
{
    char line[400];
    int n = snprintf(line, sizeof(line), "tssm: switch: quit: ");
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

    iSwitchLogWrite(line, (size_t)n);
    if (sNxlinkSocket >= 0)
    {
        ssize_t wrote = write(sNxlinkSocket, line, (size_t)n);
        (void)wrote;
    }
}

namespace
{
    void Step(const char* what)
    {
        sQuitStep = what;
        iSwitchQuitLine("%s", what);
    }
} // namespace

extern "C" void __real_exit(int code) __attribute__((noreturn));

// -Wl,--wrap=exit (CMakeLists.txt): every exit() in the program, libraries
// included.
extern "C" void __wrap_exit(int code)
{
    iSwitchQuit(code);
}

void iSwitchQuit(int code)
{
    // Another thread cannot join the ones it would have to stop, and the game
    // thread may be using what it would tear down.
    if (threadGetCurHandle() != envGetMainThreadHandle())
    {
        iSwitchQuitLine("exit(%d) on a thread other than the game's; ending the process", code);
        svcExitProcess();
    }

    // exit() again, from an atexit handler or from inside the steps below:
    // userAppExit decides by sGameStopped.
    static bool sQuitting;
    if (sQuitting)
    {
        __real_exit(code);
    }
    sQuitting = true;

    iSwitchQuitLine("exit(%d): back to the Homebrew Menu", code);
    StartWatchdog();

#ifdef TSSM_UPDATE
    // A check or a download on its worker thread uses the sockets and the
    // file system userAppExit closes. Cancelled, it ends within a second or
    // so; an install already under way is left to finish.
    Step("stopping the updater");
    iUpdateShutdown();
#endif

    Step("stopping the log server");
    StopBackground();

    // The SDL audio thread runs the port's mixers, which read the game's
    // sound state, and hbloader unmaps the code it runs. Each user of audio
    // holds a reference, so it is released until SDL has really stopped it.
    if (SDL_WasInit(SDL_INIT_AUDIO) != 0)
    {
        Step("stopping audio");
        for (int i = 0; i < 16 && SDL_WasInit(SDL_INIT_AUDIO) != 0; i++)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
    }

    // librw's context, made current on this thread: Mesa releases the GPU
    // (nvdrv and its memory) only once no context is left. A launcher's
    // renderer owns its own context and is left to SDL.
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0)
    {
        SDL_Window* window = SDL_GL_GetCurrentWindow();
        SDL_GLContext context = SDL_GL_GetCurrentContext();
        if (window != NULL && context != NULL && SDL_GetRenderer(window) == NULL)
        {
            Step("destroying the GL context");
            SDL_GL_MakeCurrent(window, NULL);
            SDL_GL_DestroyContext(context);
        }
    }

    // The window, EGL (eglTerminate takes Mesa and the GPU down), the
    // controllers, touch and the software keyboard.
    if (SDL_WasInit(0) != 0)
    {
        Step("stopping SDL (window, EGL, controllers)");
        SDL_Quit();
    }

    Step("exiting (atexit handlers, then libnx)");
    sGameStopped = true;
    __real_exit(code);
}

namespace
{
    // iSwitchClaimFile's files, held open until the exit.
    FsFile sClaims[4];
    char sClaimPaths[4][FS_MAX_PATH];
    int sClaimCount;

    void ReleaseClaims()
    {
        if (sClaimCount > 0)
        {
            iSwitchQuitLine("releasing the save folder's claim");
        }
        for (int i = 0; i < sClaimCount; i++)
        {
            fsFileClose(&sClaims[i]);
        }
        sClaimCount = 0;
    }
} // namespace

int iSwitchClaimFile(const char* path)
{
    FsFileSystem* fs = NULL;
    char fsPath[FS_MAX_PATH];
    if (sClaimCount == 4 || fsdevTranslatePath(path, &fs, fsPath) != 0 || fs == NULL)
    {
        printf("tssm: switch: claim: %s cannot be held; going on without it\n", path);
        return -1;
    }
    fsFsCreateFile(fs, fsPath, 0, 0); // there already: fine
    const Result rc =
        fsFsOpenFile(fs, fsPath, FsOpenMode_Read | FsOpenMode_Write, &sClaims[sClaimCount]);
    if (R_SUCCEEDED(rc))
    {
        snprintf(sClaimPaths[sClaimCount], FS_MAX_PATH, "%s", fsPath);
        sClaimCount++;
        printf("tssm: switch: claim: %s held open\n", path);
        return 1;
    }
    // The file system's TargetLocked (2002-0007): open for writing elsewhere.
    if (R_MODULE(rc) == 2 && R_DESCRIPTION(rc) == 7)
    {
        printf("tssm: switch: claim: %s is held by another program (%#x)\n", path, rc);
        return 0;
    }
    printf("tssm: switch: claim: %s could not be opened (%#x); going on without it\n", path, rc);
    return -1;
}

bool iSwitchFileHeld(const char* path)
{
    FsFileSystem* fs = NULL;
    char fsPath[FS_MAX_PATH];
    if (fsdevTranslatePath(path, &fs, fsPath) != 0 || fs == NULL)
    {
        return false;
    }
    for (int i = 0; i < sClaimCount; i++)
    {
        if (strcmp(sClaimPaths[i], fsPath) == 0)
        {
            return false;
        }
    }
    // Opened, never created, and closed at once when nobody holds it.
    FsFile file;
    const Result rc = fsFsOpenFile(fs, fsPath, FsOpenMode_Read | FsOpenMode_Write, &file);
    if (R_SUCCEEDED(rc))
    {
        fsFileClose(&file);
        return false;
    }
    // TargetLocked (2002-0007), as in iSwitchClaimFile.
    return R_MODULE(rc) == 2 && R_DESCRIPTION(rc) == 7;
}

uintptr_t iSwitchImageBase()
{
    return (uintptr_t)__start__;
}

void iSwitchReleaseRomfs()
{
    if (sRomfs)
    {
        romfsExit();
        sRomfs = false;
        printf("tssm: switch: RomFS unmounted\n");
    }
}

int iSwitchNxlinkSocket()
{
    return sNxlinkSocket;
}

void iSwitchEarly()
{
    // First, so that refusing starts nothing the exit would have to stop.
    if (InAppletMode())
    {
        if (!AppletModeScreen())
        {
            exit(0);
        }
        sAppletOverride = true;
    }

    // RomFS: the port's own files packed into the .nro (CMakeLists.txt). Never
    // the game's.
    sRomfsResult = romfsInit();
    sRomfs = R_SUCCEEDED(sRomfsResult);

    // Sent with `nxlink -s`: stdout and stderr go back to the PC as well as to
    // last_run.log. The socket is the log tee's to write to (iLog.cpp), not a
    // redirection of its own, so the log still gets everything. The PC can also
    // pull last_run.log from TCP 28772 (StartLogServer), only when nxlink
    // started the game.
    if (R_SUCCEEDED(socketInitializeDefault()))
    {
        sSocketsUp = true;
        if (__nxlink_host.s_addr != 0)
        {
            sNxlinkSocket = ConnectNxlink(3000);
            StartLogServer();
        }
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
    printf("tssm: switch: romfs %s; nxlink %s; log server %s\n", sRomfs ? "mounted" : "not mounted",
           sNxlinkSocket >= 0 ? "connected" : "not used",
           sLogThreadUp ? "on TCP 28772 (started by nxlink)" : "off");
    if (!sRomfs)
    {
        printf("tssm: switch: romfsInit failed: %#x\n", sRomfsResult);
    }

    appletHook(&sHook, OnApplet, NULL);
    sHooked = true;

    // A tap is a click for the overlay; SDL's Switch driver turns that off by
    // default. iPadKeyboardSDL ignores touch's mouse, so the game never sees it.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");

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

// libnx calls this from every exit, _exit() included, after the atexit
// handlers and stdio's cleanup and before its own services close. Returning
// goes back to hbloader, which loads the Homebrew Menu into the same memory.
extern "C" void userAppExit(void)
{
    // _exit() from the abort handler, or exit() from inside the quit: the
    // audio thread, the GPU and the low memory are all still live, and
    // hbloader would hand the Homebrew Menu a heap the game still uses.
    if (!sGameStopped)
    {
        iSwitchQuitLine("the game did not stop; ending the process instead of returning to "
                        "the Homebrew Menu");
        svcExitProcess();
    }

    StopBackground();
    if (sNxlinkSocket >= 0)
    {
        // Unhooked from the log tee before it goes.
        const int fd = sNxlinkSocket;
        sNxlinkSocket = -1;
        close(fd);
    }
    if (sSocketsUp)
    {
        iSwitchQuitLine("closing sockets and the RomFS");
        socketExit();
        sSocketsUp = false;
    }
    if (sRomfs)
    {
        romfsExit();
        sRomfs = false;
    }

    // After the atexit handlers (they may still save), and before libnx's
    // file system goes: the Homebrew Menu runs on in this process, and a
    // claim left open would refuse the next start of the game.
    ReleaseClaims();

    // Last: the atexit handlers above could still have reached the game's
    // memory.
    if (!iSwitchReleaseAllLow())
    {
        iSwitchQuitLine("low memory still mapped; ending the process instead of returning to "
                        "the Homebrew Menu");
        svcExitProcess();
    }

    StopWatchdog();
    iSwitchQuitLine("returning to the Homebrew Menu");
    iSwitchLogClose();
}
