#include "iHost.h"

// The Nintendo 3DS half of the iHost seam (homebrew: devkitARM, libctru,
// newlib). See iHost.h for what each of these is for; this file is only the
// spelling. Cloned from iHostSwitch.cpp: both are newlib targets with no mmap,
// fork/exec, flock, sigaction, dladdr or execinfo, and on both the SD card's
// rename() does not replace an existing file.
//
// Unlike the Switch, nothing here is about memory below 4 GB: the 3DS is a
// 32-bit machine, so every pointer fits the game's 32-bit fields, as in the
// Windows x86 build (TSSM_PTR64 is off, and with it the asset widening).
//
// Built only by CMakeLists.txt's TSSM_3DS branch. The start-up, model check,
// lifecycle and crash code is in 3ds/i3DS.cpp (libctru's side).
//
// Paths. Everything the port keeps is in one folder on the SD card,
// sdmc:/3ds/tssm (i3DS.h, k3DSRoot), the folder the Homebrew Launcher lists
// the .3dsx in:
//   sdmc:/3ds/tssm/tssm.3dsx      the program
//   sdmc:/3ds/tssm/files/         the player's own Xbox game files
//   sdmc:/3ds/tssm/config.ini     settings (iConfig.cpp: "beside the executable")
//   sdmc:/3ds/tssm/saves/         saves (isavegame: <user data>/tssm/saves)
//   sdmc:/3ds/tssm/mods/          mods
//   sdmc:/3ds/tssm/last_run.log   the log; crash.log beside it

#include "iAudioSDL.h"
#include "iLog.h"
#include "iSndHost.h"
#include "iTime.h"
#include "3ds/i3DS.h"

#include <3ds/types.h>
#include <3ds/result.h>
extern "C" {
// No C++ guard of its own.
#include <3ds/archive.h>
}
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
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
    // The system tick, which nothing the user does can move and which keeps
    // counting through sleep -- iTime's suspend/resume is what keeps a nap out
    // of the game clock (3ds/i3DS.cpp, the APT hook).
    return i3DSMonotonicNs();
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
        i3DSSleepNs(targetNs - now);
    }
}

void iHostLocalTimeOf(S64 unixSeconds, iHostCalendar* out)
{
    // libctru's time() is the console's clock, which is local time (the 3DS
    // has no time zone setting), and newlib's zone is UTC, so localtime_r
    // gives the console's clock back as it is.
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
// Memory
//
// The game's arena (iMemMgr.cpp) and RenderWare's heap when TSSM_RWHEAP=1
// (iLowHeap.cpp) come from here. Every address is below 4 GB on a 32-bit
// console, so this is a plain allocation; but on a 3DS the memory is real
// from the start (no reserve-and-touch as on a PC), so the callers' sizes are
// the 3DS's own (iMemMgr.cpp, iLowHeap.cpp).

void* iHostReserveLow(U32 size)
{
    void* p = memalign(64, size);
    if (p != NULL)
    {
        memset(p, 0, size);
    }
    return p;
}

void iHostRelease(void* p, U32)
{
    free(p);
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
    // libctru's stat leaves the time at 0; the SD card's own is asked for.
    // libctru counts from 2000: exactly that is no time at all (Azahar).
    u64 mtime = 0;
    if (out->mtime == 0 && out->is_file && R_SUCCEEDED(archive_getmtime(path, &mtime)) &&
        mtime > 946684800u)
    {
        out->mtime = (S64)mtime;
    }
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
    // Checked first: whether libctru's SD device honours O_EXCL is not
    // something to rely on (it opens with FS_OPEN_CREATE, which does not fail
    // on an existing file).
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
    snprintf(out, outsize, "%s/tmp", k3DSRoot);
    iHostMakeDir(out);
    return true;
}

bool iHostExeDir(char* out, size_t outsize)
{
    // Not argv[0]'s folder: the Homebrew Launcher passes the .3dsx's path,
    // 3dslink its name, and the folder that config.ini and the mods are looked
    // for in should not depend on how the game was started.
    snprintf(out, outsize, "%s", k3DSRoot);
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
    // There is no second process to start.
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

    const size_t n = strlen(cwd);
    snprintf(out, outsize, "%s%s%s", cwd, (n > 0 && cwd[n - 1] == '/') ? "" : "/", path);
    return true;
}

bool iHostRenameReplace(const char* from, const char* to)
{
    // The SD card's file system does not replace: libctru's rename() fails
    // when `to` exists, or deletes it and retries, which is not atomic either.
    // So the old file is moved aside first and removed only once the new one
    // is in place, as on the Switch. A power cut between the two renames
    // leaves `to.bak` and no `to`; i3DSStartup puts those back at the next
    // boot, and isavegame's backups (docs/SAVES.md) cover the rest. HOME and
    // sleep wait while it runs.
    if (!iHostPathExists(to))
    {
        return rename(from, to) == 0;
    }

    i3DSHoldHome(1);

    char bak[1024];
    snprintf(bak, sizeof(bak), "%s.bak", to);
    unlink(bak);
    bool ok = rename(to, bak) == 0;
    if (ok)
    {
        if (rename(from, to) == 0)
        {
            unlink(bak);
        }
        else
        {
            // Put the old one back.
            rename(bak, to);
            ok = false;
        }
    }

    i3DSHoldHome(0);
    return ok;
}

bool iHostFlushFile(FILE* f)
{
    // fsync reaches FSFILE_Flush through libctru's SD device.
    return f != NULL && fflush(f) == 0 && fsync(fileno(f)) == 0;
}

bool iHostFlushDir(const char*)
{
    // libctru cannot open() a directory, and the SD card's file system commits
    // a rename itself; there is nothing to flush.
    return true;
}

S32 iHostClaimInstance(const char*, U32* holder)
{
    // One program runs at a time on a 3DS.
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
    // libctru's SD device has no utimes; an imported memory-card save keeps
    // today's date instead of the console's. False tells the caller.
    (void)path;
    (void)unixSeconds;
    return false;
}

extern "C" {
extern int __system_argc;
extern char** __system_argv;
}

bool iHostCommandLine(S32* argc, char*** argv)
{
    // The Homebrew Launcher and 3dslink both pass arguments
    // (`3dslink tssm.3dsx -- a b`).
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
    // <this>/tssm/last_run.log), so this is the folder above k3DSRoot and
    // everything lands in sdmc:/3ds/tssm.
    snprintf(out, outsize, "%s", k3DSDataParent);
    return true;
}

S32 iHostStrCaseCmp(const char* a, const char* b)
{
    return strcasecmp(a, b);
}

const char* iHostName()
{
    return "3ds";
}

void iHostErrorBox(const char* title, const char* message)
{
    i3DSErrorBox(title, message);
}

// ---------------------------------------------------------------------------
// The game side of 3ds/i3DS.cpp (i3DS.h says why the two are apart).

void i3DSGameAway()
{
    iTimeSuspend();
}

void i3DSGameBack()
{
    iTimeResume();
    // As Android's foreground and the Switch's: every audio device closed and
    // opened again, so whatever the system did to them while the game was
    // away cannot matter.
    iAudioSDLRestart(iAUDIOSDL_FOREGROUND);
}

int i3DSAudioMixTake(uint32_t* calls, uint64_t* totalNs, uint64_t* maxNs)
{
    iSndHostMixStats st;
    const bool up = iSndHostMixStatsTake(&st, true);
    *calls = up ? st.calls : 0;
    *totalNs = up ? st.totalNs : 0;
    *maxNs = up ? st.maxNs : 0;
    return up ? 1 : 0;
}

const char* i3DSCrashLogPath()
{
    return iLogCrashPath();
}

const char* i3DSLogPath()
{
    return iLogPath();
}

void i3DSLogDrain(unsigned ms)
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

// No symbols on the console. A .3dsx runs at the address it was linked at,
// so each frame's address is the ELF's:
// `arm-none-eabi-addr2line -f -C -e tssm.elf <address>` turns it back into a
// function and line on the PC that built it. The walk uses the ARM unwind
// tables (.ARM.exidx), which C++ code has; it stops at a C frame without them.
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
    for (int i = 1; i < state.count; i++)
    {
        printf("tssm:   #%-2d %#lx\n", i - 1, (unsigned long)frames[i]);
    }
    fflush(stdout);
}
