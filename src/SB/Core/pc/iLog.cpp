// The game's own log; see iLog.h. Port-only.

#include "iLog.h"

#include "iHost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#ifdef __SWITCH__
#include <sys/iosupport.h>
#include "switch/iSwitch.h"
#else
#include <sys/ioctl.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
    // Set by iLogBegin, before anything else runs: plain arrays, so nothing
    // here waits on another file's static constructors.
    char sDir[600]; // the folder of the log and of crash.log
    char sLogPath[700]; // the log, when TSSM_LOG names it
    char sCrashPath[700]; // crash.log
    char sOpened[700]; // the log this run writes, once iLogStart opened it
    bool sTeeOn;
    bool sOff; // TSSM_LOG=off

    // What was printed before iLogStart, for the log's start. Bounded: a run
    // that never calls iLogStart (a command-line tool) holds at most this.
    const size_t kHoldMax = 256 * 1024;
    char* sHeld;
    size_t sHeldLen;
    bool sHolding = true;

    void SlashesForward(char* s)
    {
        for (; *s != '\0'; s++)
        {
            if (*s == '\\')
            {
                *s = '/';
            }
        }
    }

    // The folder part of `path` (no trailing separator); "." for none.
    void ParentOf(const char* path, char* out, size_t outsize)
    {
        snprintf(out, outsize, "%s", path);
        SlashesForward(out);
        size_t n = strlen(out);
        while (n > 1 && out[n - 1] == '/')
        {
            out[--n] = '\0';
        }
        char* slash = strrchr(out, '/');
        if (slash == NULL)
        {
            snprintf(out, outsize, ".");
        }
        else if (slash == out || (slash == out + 2 && out[1] == ':'))
        {
            slash[1] = '\0'; // "/" or "E:/"
        }
        else
        {
            *slash = '\0';
        }
    }

    bool IsOff(const char* v)
    {
        return strcmp(v, "off") == 0 || strcmp(v, "0") == 0 || strcmp(v, "none") == 0;
    }

    void Join(char* out, size_t outsize, const char* dir, const char* leaf)
    {
        size_t n = strlen(dir);
        const bool sep = n > 0 && dir[n - 1] != '/';
        snprintf(out, outsize, "%s%s%s", dir, sep ? "/" : "", leaf);
    }

    // Where the log goes, from the environment alone (it is decided before
    // config.ini is read): TSSM_LOG; else beside TSSM_SAVE_DIR's folder; else
    // the per-user data folder's tssm, the default save folder's parent.
    void ResolvePaths()
    {
        const char* log = getenv("TSSM_LOG");
        const char* saves = getenv("TSSM_SAVE_DIR");

        if (log != NULL && log[0] != '\0' && !IsOff(log))
        {
            snprintf(sLogPath, sizeof(sLogPath), "%s", log);
            SlashesForward(sLogPath);
            ParentOf(sLogPath, sDir, sizeof(sDir));
        }
        else
        {
            sOff = log != NULL && IsOff(log);
            if (saves != NULL && saves[0] != '\0')
            {
                ParentOf(saves, sDir, sizeof(sDir));
            }
            else
            {
                char base[512];
                if (iHostUserDataDir(base, sizeof(base)))
                {
                    SlashesForward(base);
                    Join(sDir, sizeof(sDir), base, "tssm");
                }
                else
                {
                    snprintf(sDir, sizeof(sDir), ".");
                }
            }
            Join(sLogPath, sizeof(sLogPath), sDir, "last_run.log");
        }

        Join(sCrashPath, sizeof(sCrashPath), sDir, "crash.log");
    }

    // last_run.log -> last_run.prev.log; any other name gets ".prev" added.
    void PrevName(const char* path, char* out, size_t outsize)
    {
        const size_t n = strlen(path);
        if (n > 4 && strcmp(path + n - 4, ".log") == 0)
        {
            snprintf(out, outsize, "%.*s.prev.log", (int)(n - 4), path);
        }
        else
        {
            snprintf(out, outsize, "%s.prev", path);
        }
    }

    // The claim on the log file, so that a second copy of the game started
    // while one runs neither turns the log over nor writes into it.
    bool ClaimLog(const char* path)
    {
        char abs[700];
        if (!iHostAbsolutePath(path, abs, sizeof(abs)))
        {
            snprintf(abs, sizeof(abs), "%s", path);
        }
        SlashesForward(abs);

        U32 hash = 2166136261u; // FNV-1a over the path, folded to one case
        for (const char* p = abs; *p != '\0'; p++)
        {
            char c = *p;
            if (c >= 'A' && c <= 'Z')
            {
                c = (char)(c - 'A' + 'a');
            }
            hash = (hash ^ (U8)c) * 16777619u;
        }

        char key[64];
        snprintf(key, sizeof(key), "tssm-log-%08x", (unsigned)hash);
        return iHostClaimInstance(key, NULL) != 0;
    }
} // namespace

#ifdef _WIN32

namespace
{
    CRITICAL_SECTION sLock;
    HANDLE sLogFile = INVALID_HANDLE_VALUE;

    // One per stream: the pipe the stream now writes into, where it wrote
    // before, and the thread between them.
    struct Tee
    {
        HANDLE read;
        HANDLE original; // INVALID_HANDLE_VALUE when it went nowhere
        volatile LONG busy;
    };
    Tee sTee[2];

    void Keep(const char* data, DWORD n)
    {
        EnterCriticalSection(&sLock);
        if (sLogFile != INVALID_HANDLE_VALUE)
        {
            DWORD wrote = 0;
            WriteFile(sLogFile, data, n, &wrote, NULL);
        }
        else if (sHolding)
        {
            if (sHeld == NULL)
            {
                sHeld = (char*)malloc(kHoldMax);
            }
            if (sHeld != NULL && sHeldLen + n <= kHoldMax)
            {
                memcpy(sHeld + sHeldLen, data, n);
                sHeldLen += n;
            }
        }
        LeaveCriticalSection(&sLock);
    }

    // The read end is overlapped. Synchronous I/O on one handle is done one
    // call at a time, so with a synchronous ReadFile waiting on the pipe,
    // iLogDrain's PeekNamedPipe on it would wait for that read -- for ever,
    // once the game has stopped printing. That is also why this is a named
    // pipe: an anonymous one cannot be made overlapped.
    DWORD WINAPI TeeThread(LPVOID param)
    {
        Tee* t = (Tee*)param;
        char buf[4096];
        OVERLAPPED ov;
        memset(&ov, 0, sizeof(ov));
        ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (ov.hEvent == NULL)
        {
            return 0;
        }
        for (;;)
        {
            DWORD n = 0;
            ResetEvent(ov.hEvent);
            if (!ReadFile(t->read, buf, sizeof(buf), &n, &ov))
            {
                if (GetLastError() != ERROR_IO_PENDING ||
                    !GetOverlappedResult(t->read, &ov, &n, TRUE))
                {
                    return 0;
                }
            }
            else if (!GetOverlappedResult(t->read, &ov, &n, FALSE))
            {
                return 0;
            }
            InterlockedExchange(&t->busy, 1);
            if (t->original != INVALID_HANDLE_VALUE)
            {
                DWORD wrote = 0;
                WriteFile(t->original, buf, n, &wrote, NULL);
            }
            Keep(buf, n);
            InterlockedExchange(&t->busy, 0);
        }
    }

    // Puts C runtime descriptor `fd` (1 or 2) and the matching standard handle
    // through a pipe into `t`.
    bool StartTee(Tee* t, int fd, DWORD stdId)
    {
        static LONG sSerial;
        char name[96];
        snprintf(name, sizeof(name), "\\\\.\\pipe\\tssm-log-%lu-%ld",
                 (unsigned long)GetCurrentProcessId(), (long)InterlockedIncrement(&sSerial));

        HANDLE r = CreateNamedPipeA(
            name, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 0,
            64 * 1024, 0, NULL);
        if (r == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        HANDLE w = CreateFileA(name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (w == INVALID_HANDLE_VALUE)
        {
            CloseHandle(r);
            return false;
        }

        // The original, duplicated, because the descriptor that holds it is
        // about to be pointed at the pipe.
        t->original = INVALID_HANDLE_VALUE;
        HANDLE orig = GetStdHandle(stdId);
        if (orig != NULL && orig != INVALID_HANDLE_VALUE)
        {
            HANDLE dup = NULL;
            if (DuplicateHandle(GetCurrentProcess(), orig, GetCurrentProcess(), &dup, 0, FALSE,
                                DUPLICATE_SAME_ACCESS))
            {
                t->original = dup;
            }
        }

        int pfd = _open_osfhandle((intptr_t)w, _O_WRONLY | _O_BINARY);
        if (pfd < 0 || _dup2(pfd, fd) != 0)
        {
            CloseHandle(r);
            if (pfd >= 0)
            {
                _close(pfd);
            }
            else
            {
                CloseHandle(w);
            }
            if (t->original != INVALID_HANDLE_VALUE)
            {
                CloseHandle(t->original);
            }
            return false;
        }
        _close(pfd);
        SetStdHandle(stdId, (HANDLE)_get_osfhandle(fd));

        t->read = r;
        t->busy = 0;
        HANDLE th = CreateThread(NULL, 64 * 1024, TeeThread, t, 0, NULL);
        if (th != NULL)
        {
            CloseHandle(th);
        }
        return th != NULL;
    }
} // namespace

void iLogBegin()
{
    ResolvePaths();
    if (sOff)
    {
        return;
    }

    InitializeCriticalSection(&sLock);
    fflush(stdout);
    fflush(stderr);
    sTeeOn = StartTee(&sTee[0], 1, STD_OUTPUT_HANDLE);
    StartTee(&sTee[1], 2, STD_ERROR_HANDLE);
}

void iLogStart()
{
    if (!sTeeOn || sOpened[0] != '\0')
    {
        return;
    }

    bool claimed = ClaimLog(sLogPath);
    char prev[720];
    PrevName(sLogPath, prev, sizeof(prev));
    HANDLE f = INVALID_HANDLE_VALUE;
    if (claimed)
    {
        iHostMakeDir(sDir);
        if (iHostPathExists(sLogPath))
        {
            MoveFileExA(sLogPath, prev, MOVEFILE_REPLACE_EXISTING);
        }
        f = CreateFileA(sLogPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }

    EnterCriticalSection(&sLock);
    if (f != INVALID_HANDLE_VALUE)
    {
        if (sHeldLen > 0)
        {
            DWORD wrote = 0;
            WriteFile(f, sHeld, (DWORD)sHeldLen, &wrote, NULL);
        }
        sLogFile = f;
        snprintf(sOpened, sizeof(sOpened), "%s", sLogPath);
    }
    sHolding = false;
    free(sHeld);
    sHeld = NULL;
    sHeldLen = 0;
    LeaveCriticalSection(&sLock);

    if (f != INVALID_HANDLE_VALUE)
    {
        printf("tssm: log: %s (the run before is in %s; crash reports in %s)\n", sOpened, prev,
               sCrashPath);
    }
    else if (!claimed)
    {
        printf("tssm: log: another copy of the game is writing %s; this one keeps no log\n",
               sLogPath);
    }
    else
    {
        printf("tssm: log: cannot write %s; this run keeps no log\n", sLogPath);
    }
    fflush(stdout);
}

void iLogDrain(U32 ms)
{
    fflush(stdout);
    fflush(stderr);
    if (!sTeeOn)
    {
        return;
    }

    // Empty pipes and idle threads, seen twice in a row a moment apart: a
    // thread that has just read the last bytes sets `busy` right after.
    S32 quiet = 0;
    for (U32 waited = 0; waited < ms && quiet < 2; waited += 2)
    {
        bool idle = true;
        for (S32 i = 0; i < 2; i++)
        {
            DWORD avail = 0;
            if (sTee[i].read != NULL &&
                (!PeekNamedPipe(sTee[i].read, NULL, 0, NULL, &avail, NULL) || avail != 0 ||
                 sTee[i].busy != 0))
            {
                idle = false;
            }
        }
        quiet = idle ? quiet + 1 : 0;
        Sleep(2);
    }

    if (sLogFile != INVALID_HANDLE_VALUE)
    {
        FlushFileBuffers(sLogFile);
    }
}

#else

namespace
{
    pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
    int sLogFd = -1;

    struct Tee
    {
        int read;
        int original; // -1 when it went nowhere
        volatile int busy;
    };
    Tee sTee[2] = { { -1, -1, 0 }, { -1, -1, 0 } };

    void Keep(const char* data, size_t n)
    {
        pthread_mutex_lock(&sLock);
        if (sLogFd >= 0)
        {
            ssize_t wrote = write(sLogFd, data, n);
            (void)wrote;
        }
        else if (sHolding)
        {
            if (sHeld == NULL)
            {
                sHeld = (char*)malloc(kHoldMax);
            }
            if (sHeld != NULL && sHeldLen + n <= kHoldMax)
            {
                memcpy(sHeld + sHeldLen, data, n);
                sHeldLen += n;
            }
        }
        pthread_mutex_unlock(&sLock);
    }

#ifdef __SWITCH__
    // The Switch has no pipe(). newlib sends descriptors 1 and 2 through
    // devoptab_list[STD_OUT] / [STD_ERR], so the tee is a device in those two
    // slots: each write goes to the device that was there before (nothing,
    // unless something installed a console), to the nxlink host when the game
    // was sent with `nxlink -s` (switch/iSwitch.cpp), and to the log. No
    // thread, so nothing is ever in flight and iLogDrain only syncs the file.
    const devoptab_t* sPrevDev[2];
    devoptab_t sTeeDev[2];

    ssize_t TeeWrite(S32 which, struct _reent* r, void* fd, const char* ptr, size_t len)
    {
        const devoptab_t* prev = sPrevDev[which];
        if (prev != NULL && prev->write_r != NULL)
        {
            prev->write_r(r, fd, ptr, len);
        }
        const int net = iSwitchNxlinkSocket();
        if (net >= 0)
        {
            ssize_t wrote = write(net, ptr, len);
            (void)wrote;
        }
        Keep(ptr, len);
        return (ssize_t)len;
    }

    ssize_t TeeOut(struct _reent* r, void* fd, const char* ptr, size_t len)
    {
        return TeeWrite(0, r, fd, ptr, len);
    }

    ssize_t TeeErr(struct _reent* r, void* fd, const char* ptr, size_t len)
    {
        return TeeWrite(1, r, fd, ptr, len);
    }

    bool StartTee(Tee*, int fd)
    {
        const S32 which = fd == 2 ? 1 : 0;
        const int slot = which == 0 ? STD_OUT : STD_ERR;
        sPrevDev[which] = devoptab_list[slot];
        memset(&sTeeDev[which], 0, sizeof(sTeeDev[which]));
        sTeeDev[which].name = which == 0 ? "tssmout" : "tssmerr";
        sTeeDev[which].write_r = which == 0 ? TeeOut : TeeErr;
        devoptab_list[slot] = &sTeeDev[which];
        return true;
    }
#else
    void* TeeThread(void* param)
    {
        Tee* t = (Tee*)param;
        char buf[4096];
        for (;;)
        {
            ssize_t n = read(t->read, buf, sizeof(buf));
            if (n < 0 && errno == EINTR)
            {
                continue;
            }
            if (n <= 0)
            {
                return NULL;
            }
            __atomic_store_n(&t->busy, 1, __ATOMIC_SEQ_CST);
            if (t->original >= 0)
            {
                ssize_t wrote = write(t->original, buf, (size_t)n);
                (void)wrote;
            }
            Keep(buf, (size_t)n);
            __atomic_store_n(&t->busy, 0, __ATOMIC_SEQ_CST);
        }
    }

    bool StartTee(Tee* t, int fd)
    {
        int p[2];
        if (pipe(p) != 0)
        {
            return false;
        }
        t->original = dup(fd);
        if (dup2(p[1], fd) < 0)
        {
            close(p[0]);
            close(p[1]);
            if (t->original >= 0)
            {
                close(t->original);
            }
            return false;
        }
        close(p[1]);
        t->read = p[0];

        pthread_t th;
        if (pthread_create(&th, NULL, TeeThread, t) != 0)
        {
            return false;
        }
        pthread_detach(th);
        return true;
    }
#endif
} // namespace

void iLogBegin()
{
    ResolvePaths();
#ifdef __ANDROID__
    // Android's log is the platform's (android/iAndroid.cpp); no tee here.
    sOff = true;
#endif
    if (sOff)
    {
        return;
    }

    fflush(stdout);
    fflush(stderr);
    sTeeOn = StartTee(&sTee[0], 1);
    StartTee(&sTee[1], 2);
}

void iLogStart()
{
    if (!sTeeOn || sOpened[0] != '\0')
    {
        return;
    }

    bool claimed = ClaimLog(sLogPath);
    char prev[720];
    PrevName(sLogPath, prev, sizeof(prev));
    int fd = -1;
    if (claimed)
    {
        iHostMakeDir(sDir);
        if (iHostPathExists(sLogPath))
        {
            // Through the host: the Switch's rename does not replace.
            iHostRenameReplace(sLogPath, prev);
        }
        fd = open(sLogPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    }

    pthread_mutex_lock(&sLock);
    if (fd >= 0)
    {
        if (sHeldLen > 0)
        {
            ssize_t wrote = write(fd, sHeld, sHeldLen);
            (void)wrote;
        }
        sLogFd = fd;
        snprintf(sOpened, sizeof(sOpened), "%s", sLogPath);
    }
    sHolding = false;
    free(sHeld);
    sHeld = NULL;
    sHeldLen = 0;
    pthread_mutex_unlock(&sLock);

    if (fd >= 0)
    {
        printf("tssm: log: %s (the run before is in %s; crash reports in %s)\n", sOpened, prev,
               sCrashPath);
    }
    else if (!claimed)
    {
        printf("tssm: log: another copy of the game is writing %s; this one keeps no log\n",
               sLogPath);
    }
    else
    {
        printf("tssm: log: cannot write %s; this run keeps no log\n", sLogPath);
    }
    fflush(stdout);
}

void iLogDrain(U32 ms)
{
    fflush(stdout);
    fflush(stderr);
    if (!sTeeOn)
    {
        return;
    }

#ifndef __SWITCH__
    S32 quiet = 0;
    for (U32 waited = 0; waited < ms && quiet < 2; waited += 2)
    {
        bool idle = true;
        for (S32 i = 0; i < 2; i++)
        {
            int avail = 0;
            if (sTee[i].read >= 0 && (ioctl(sTee[i].read, FIONREAD, &avail) != 0 || avail != 0 ||
                                      __atomic_load_n(&sTee[i].busy, __ATOMIC_SEQ_CST) != 0))
            {
                idle = false;
            }
        }
        quiet = idle ? quiet + 1 : 0;
        usleep(2000);
    }
#else
    (void)ms;
#endif

    if (sLogFd >= 0)
    {
        fsync(sLogFd);
    }
}

#ifdef __SWITCH__
void iSwitchLogWrite(const char* text, size_t n)
{
    Keep(text, n);
    if (sLogFd >= 0)
    {
        fsync(sLogFd);
    }
}

void iSwitchLogClose()
{
    pthread_mutex_lock(&sLock);
    if (sLogFd >= 0)
    {
        fsync(sLogFd);
        close(sLogFd);
        sLogFd = -1;
    }
    sHolding = false;
    pthread_mutex_unlock(&sLock);
}
#endif

#endif

const char* iLogCrashPath()
{
    return sCrashPath;
}

const char* iLogPath()
{
    return sOpened;
}
