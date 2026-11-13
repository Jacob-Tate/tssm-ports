#include "iHost.h"

// The Nintendo Switch half of the iHost seam (homebrew: devkitA64, libnx,
// newlib). See iHost.h for what each of these is for; this file is only the
// spelling, plus the two things the Switch makes hard: memory below 4 GB
// (iHostReserveLow) and a rename that does not replace (iHostRenameReplace).
//
// Built only by CMakeLists.txt's TSSM_SWITCH branch, in place of
// iHostPosix.cpp: libnx's newlib has no mmap, fork/exec, flock, sigaction,
// dladdr or execinfo, and its rename() fails on an existing target. The
// start-up, applet and crash code is in switch/iSwitch.cpp.
//
// Paths. Everything the port keeps is in one folder on the SD card,
// /switch/tssm (iSwitch.h, kSwitchRoot), the folder hbmenu lists the .nro in:
//   /switch/tssm/tssm.nro      the program
//   /switch/tssm/assets/       the player's own Xbox game files, laid out as
//                              the Android port's assets folder
//   /switch/tssm/config.ini    settings (iConfig.cpp: "beside the executable")
//   /switch/tssm/saves/        saves (isavegame: <user data>/tssm/saves)
//   /switch/tssm/mods/         mods
//   /switch/tssm/last_run.log  the log; crash.log beside it
// Unprefixed absolute paths ("/switch/...") are used throughout, not
// "sdmc:/...": libnx resolves them against the SD card, which it makes the
// default device, and iFile's path walker knows "/" as a root but not
// "sdmc:".

#include "iAudioSDL.h"
#include "iLog.h"
#include "iTime.h"
#include "switch/iSwitch.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <unwind.h>

// ---------------------------------------------------------------------------
// Time

U64 iHostMonotonicNs()
{
    // The system counter (19.2 MHz), which nothing the user does can move and
    // which keeps counting through sleep -- iTime's suspend/resume is what
    // keeps a nap out of the game clock (switch/iSwitch.cpp, the applet hook).
    return iSwitchMonotonicNs();
}

void iHostSleepUntilNs(U64 targetNs)
{
    // svcSleepThread takes a duration. Re-reading the clock each time round is
    // what keeps an absolute deadline: a slow caller cannot make the pacing
    // drift, and a wake-up a little early just sleeps the rest.
    for (;;)
    {
        U64 now = iHostMonotonicNs();
        if (now >= targetNs)
        {
            return;
        }
        iSwitchSleepNs(targetNs - now);
    }
}

void iHostLocalTimeOf(S64 unixSeconds, iHostCalendar* out)
{
    // TODO(switch): check on hardware that newlib's localtime_r gives the
    // console's time zone and not UTC (the save list's dates show it).
    time_t when = (time_t)unixSeconds;
    tm t;
    memset(&t, 0, sizeof(t));
    localtime_r(&when, &t);

    out->sec = t.tm_sec;
    out->min = t.tm_min;
    out->hour = t.tm_hour;
    out->mday = t.tm_mday;
    out->mon = t.tm_mon;
    out->year = t.tm_year;
    out->wday = t.tm_wday;
}

void iHostLocalTime(iHostCalendar* out)
{
    iHostLocalTimeOf((S64)time(NULL), out);
}

// ---------------------------------------------------------------------------
// Virtual memory
//
// The game keeps pointers in 32-bit fields, so its arena (iMemMgr.cpp) and
// RenderWare's heap (iLowHeap.cpp) have to sit below 4 GB. How that is done on
// a Switch -- and why it is the riskiest part of the port -- is in
// switch/iSwitchLowMem.cpp, which is the one place the hardware spike's
// findings go. A block that could not be put low comes back anyway, from the
// heap, and the callers refuse it as above 4 GB with a clear message.

void* iHostReserveLow(U32 size)
{
    return iSwitchReserveLow(size);
}

void iHostRelease(void* p, U32)
{
    iSwitchReleaseLow(p);
}

// ---------------------------------------------------------------------------
// Threads
//
// libnx's pthreads default to a small stack, and the threads here were written
// against desktop defaults (Ship of Harkinian's Switch port crashed on exactly
// this). The link wraps pthread_create (CMakeLists.txt, -Wl,--wrap) so every
// thread made without an explicit stack of at least this size gets this size.
// The main thread's stack is hbloader's (1 MB), as on Windows.

extern "C" int __real_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                                     void* (*start)(void*), void* arg);

extern "C" int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                                     void* (*start)(void*), void* arg)
{
    const size_t kStack = 2u * 1024u * 1024u;

    pthread_attr_t local;
    if (attr == NULL)
    {
        pthread_attr_init(&local);
    }
    else
    {
        size_t have = 0;
        if (pthread_attr_getstacksize(attr, &have) == 0 && have >= kStack)
        {
            return __real_pthread_create(thread, attr, start, arg);
        }
        local = *attr;
    }

    pthread_attr_setstacksize(&local, kStack);
    int rc = __real_pthread_create(thread, &local, start, arg);
    if (attr == NULL)
    {
        pthread_attr_destroy(&local);
    }
    return rc;
}

// ---------------------------------------------------------------------------
// Filesystem

bool iHostPathExists(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

bool iHostStat(const char* path, iHostFileInfo* out)
{
    struct stat st;
    if (stat(path, &st) != 0)
    {
        return false;
    }

    out->is_dir = S_ISDIR(st.st_mode) != 0;
    out->is_file = S_ISREG(st.st_mode) != 0;
    out->size = (U64)st.st_size;
    out->mtime = (S64)st.st_mtime;
    return true;
}

bool iHostMakeDir(const char* path)
{
    if (mkdir(path, 0777) == 0)
    {
        return true;
    }

    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool iHostRemoveFile(const char* path)
{
    return unlink(path) == 0;
}

FILE* iHostCreateNewFile(const char* path)
{
    // libnx's fsdev turns O_CREAT|O_EXCL into "create, and fail if it exists"
    // (FsCreateOption on fsFsCreateFile), which is the exclusivity iHost.h
    // asks for. Checked as well, in case a future libnx stops doing so.
    if (iHostPathExists(path))
    {
        return NULL;
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0)
    {
        return NULL;
    }

    FILE* f = fdopen(fd, "wb");
    if (f == NULL)
    {
        close(fd);
        return NULL;
    }

    return f;
}

bool iHostRemoveDir(const char* path)
{
    return rmdir(path) == 0;
}

bool iHostTempDir(char* out, size_t outsize)
{
    snprintf(out, outsize, "%s/tmp", kSwitchRoot);
    iHostMakeDir(out);
    return true;
}

bool iHostExeDir(char* out, size_t outsize)
{
    // Not argv[0]'s folder: hbloader passes the .nro's path, but nxlink passes
    // only its name, and the folder that config.ini and the mods are looked
    // for in should not depend on how the game was started.
    snprintf(out, outsize, "%s", kSwitchRoot);
    return true;
}

bool iHostSetEnv(const char* name, const char* value)
{
    if (value == NULL)
    {
        return unsetenv(name) == 0;
    }
    return setenv(name, value, 1) == 0;
}

bool iHostSetChildEnv(const char* name, const char* value)
{
    return iHostSetEnv(name, value);
}

bool iHostRunDetached(const char*, const char*)
{
    // There is no second process to start. (envSetNextLoad could chain to
    // another .nro after this one exits; nothing here wants that.)
    return false;
}

bool iHostAbsolutePath(const char* path, char* out, size_t outsize)
{
    // "/x" and "sdmc:/x" are absolute already.
    if (path[0] == '/' || strchr(path, ':') != NULL)
    {
        snprintf(out, outsize, "%s", path);
        return true;
    }

    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        return false;
    }

    // libnx reports the working directory with its device ("sdmc:/switch/x").
    // Keep everything above the seam on unprefixed paths, as kSwitchRoot is.
    const char* rest = cwd;
    if (strncmp(rest, "sdmc:", 5) == 0)
    {
        rest += 5;
    }
    const size_t n = strlen(rest);
    snprintf(out, outsize, "%s%s%s", rest, (n > 0 && rest[n - 1] == '/') ? "" : "/", path);
    return true;
}

bool iHostRenameReplace(const char* from, const char* to)
{
    // The SD card's file system does not replace: libnx passes rename()
    // straight to fsFsRenameFile, which fails when `to` exists (Ship of
    // Harkinian's settings stopped saving after the first save because of
    // this). So the old file is moved aside first and removed only once the
    // new one is in place. A power cut between the two renames leaves
    // `to.bak` and no `to`; isavegame's backups (docs/SAVES.md) are the
    // recovery for that, as for any interrupted write.
    if (rename(from, to) == 0)
    {
        return true;
    }

    if (!iHostPathExists(to))
    {
        return false;
    }

    char bak[1024];
    snprintf(bak, sizeof(bak), "%s.bak", to);
    unlink(bak);
    if (rename(to, bak) != 0)
    {
        return false;
    }

    if (rename(from, to) != 0)
    {
        // Put the old one back.
        rename(bak, to);
        return false;
    }

    unlink(bak);
    return true;
}

bool iHostFlushFile(FILE* f)
{
    // fsync reaches fsFileFlush through libnx's fsdev.
    return f != NULL && fflush(f) == 0 && fsync(fileno(f)) == 0;
}

bool iHostFlushDir(const char*)
{
    // libnx cannot open() a directory, and the SD card's file system commits a
    // rename itself; there is nothing to flush.
    return true;
}

S32 iHostClaimInstance(const char*, U32* holder)
{
    // One program runs at a time on a Switch.
    if (holder != NULL)
    {
        *holder = 0;
    }
    return 1;
}

S32 iHostRaiseProcessWindow(U32)
{
    return 0;
}

bool iHostOtherCopyInFront()
{
    return false;
}

bool iHostFreeBytes(const char* path, U64* out)
{
    struct statvfs vfs;
    if (statvfs(path, &vfs) != 0)
    {
        return false;
    }

    *out = (U64)vfs.f_bavail * (U64)vfs.f_frsize;
    return true;
}

bool iHostSetFileTime(const char* path, S64 unixSeconds)
{
    // TODO(switch): libnx's fsdev has no utimes; an imported memory-card save
    // keeps today's date instead of the console's. False tells the caller.
    (void)path;
    (void)unixSeconds;
    return false;
}

extern "C"
{
    extern int __system_argc;
    extern char** __system_argv;
}

bool iHostCommandLine(S32* argc, char*** argv)
{
    // hbloader and nxlink both pass arguments (nxlink: `nxlink tssm.nro -- a b`).
    *argc = (S32)__system_argc;
    *argv = __system_argv;
    return __system_argv != NULL;
}

struct iHostDir
{
    DIR* d;
};

iHostDir* iHostDirOpen(const char* path)
{
    DIR* d = opendir(path);
    if (d == NULL)
    {
        return NULL;
    }

    iHostDir* h = (iHostDir*)malloc(sizeof(iHostDir));
    if (h == NULL)
    {
        closedir(d);
        return NULL;
    }

    h->d = d;
    return h;
}

const char* iHostDirNext(iHostDir* h)
{
    if (h == NULL)
    {
        return NULL;
    }

    for (dirent* e = readdir(h->d); e != NULL; e = readdir(h->d))
    {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
        {
            continue;
        }
        return e->d_name;
    }

    return NULL;
}

void iHostDirClose(iHostDir* h)
{
    if (h != NULL)
    {
        closedir(h->d);
        free(h);
    }
}

bool iHostUserDataDir(char* out, size_t outsize)
{
    // The callers add "/tssm" (saves: <this>/tssm/saves; the log:
    // <this>/tssm/last_run.log), so this is the folder above kSwitchRoot and
    // everything lands in /switch/tssm. Not the console's own save system:
    // that belongs to the game used for the title takeover.
    snprintf(out, outsize, "%s", kSwitchDataParent);
    return true;
}

S32 iHostStrCaseCmp(const char* a, const char* b)
{
    return strcasecmp(a, b);
}

const char* iHostName()
{
    return "switch";
}

void iHostErrorBox(const char* title, const char* message)
{
    iSwitchErrorBox(title, message);
}

// ---------------------------------------------------------------------------
// The game side of switch/iSwitch.cpp (iSwitch.h says why the two are apart).

void iSwitchGameAway()
{
    iTimeSuspend();
}

void iSwitchGameBack()
{
    iTimeResume();
    // As Android's foreground: every audio device closed and opened again, so
    // whatever the system did to them while the game was away cannot matter.
    iAudioSDLRestart(iAUDIOSDL_FOREGROUND);
}

const char* iSwitchCrashLogPath()
{
    return iLogCrashPath();
}

const char* iSwitchLogPath()
{
    return iLogPath();
}

void iSwitchLogDrain(unsigned ms)
{
    iLogDrain((U32)ms);
}

// ---------------------------------------------------------------------------
// Diagnostics

namespace
{
    struct UnwindState
    {
        uintptr_t* frames;
        int count;
        int max;
    };

    _Unwind_Reason_Code UnwindFrame(_Unwind_Context* ctx, void* arg)
    {
        UnwindState* state = (UnwindState*)arg;
        uintptr_t pc = (uintptr_t)_Unwind_GetIP(ctx);
        if (pc == 0)
        {
            return _URC_NO_REASON;
        }
        if (state->count >= state->max)
        {
            return _URC_END_OF_STACK;
        }
        state->frames[state->count++] = pc;
        return _URC_NO_REASON;
    }
} // namespace

// No symbols on the console: each frame is printed as an offset into the
// .nro's image, which `aarch64-none-elf-addr2line -f -C -e tssm.elf <offset>`
// turns back into a function and line on the PC that built it.
void iHostPrintCallers(const char* why, S32 maxFrames)
{
    if (maxFrames < 1)
    {
        maxFrames = 1;
    }
    else if (maxFrames > 48)
    {
        maxFrames = 48;
    }

    uintptr_t frames[49];
    UnwindState state;
    state.frames = frames;
    state.count = 0;
    state.max = maxFrames + 1;
    _Unwind_Backtrace(UnwindFrame, &state);

    printf("tssm: callers -- %s\n", why != NULL ? why : "");
    const uintptr_t base = iSwitchImageBase();
    for (int i = 1; i < state.count; i++)
    {
        printf("tssm:   #%-2d %#lx  [tssm.elf+%#lx]\n", i - 1, (unsigned long)frames[i],
               (unsigned long)(frames[i] - base));
    }
    fflush(stdout);
}
