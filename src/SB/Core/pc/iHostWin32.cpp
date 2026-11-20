#include "iHost.h"

#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

// The Win32 half of the iHost seam. See iHost.h for what each of these is for;
// this file is only the spelling.

// ---------------------------------------------------------------------------
// Time

static const U64 NS_PER_SEC = 1000000000ULL;

// Written this way because a literal backslash in a character constant is
// exactly the sort of thing a careless sed over this tree would mangle.
static const char BS_CHAR = (char)92;

// QueryPerformanceCounter, not GetTickCount: the same reason POSIX uses
// CLOCK_MONOTONIC rather than the wall clock. QPC is monotonic and, since
// Windows 7, consistent across cores.
static U64 iQpcFreq()
{
    static U64 freq = 0;
    if (freq == 0)
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = (U64)f.QuadPart;
    }
    return freq;
}

U64 iHostMonotonicNs()
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);

    U64 freq = iQpcFreq();

    // Split the division so a machine that has been up for weeks does not
    // overflow the multiply. (ticks * 1e9) wraps a U64 after about 18 seconds
    // at a 1 GHz counter, which is not hypothetical -- QPC frequency is often
    // exactly that.
    U64 ticks = (U64)c.QuadPart;
    return (ticks / freq) * NS_PER_SEC + ((ticks % freq) * NS_PER_SEC) / freq;
}

void iHostSleepUntilNs(U64 targetNs)
{
    U64 now = iHostMonotonicNs();
    if (targetNs <= now)
    {
        return;
    }

    U64 remainingNs = targetNs - now;

    // A plain Sleep() rounds to the scheduler tick, which defaults to about
    // 15.6 ms -- most of a frame, so pacing with it would judder badly. A
    // high-resolution waitable timer honours the actual deadline. The flag
    // needs Windows 10 1803; CreateWaitableTimerEx fails on older systems, so
    // fall back to an ordinary timer, which is still better than Sleep.
    static HANDLE timer = NULL;
    if (timer == NULL)
    {
        timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                       TIMER_ALL_ACCESS);
        if (timer == NULL)
        {
            timer = CreateWaitableTimerW(NULL, FALSE, NULL);
        }
    }

    if (timer != NULL)
    {
        // Negative means relative, in 100 ns units.
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)(remainingNs / 100);
        if (due.QuadPart != 0 && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
        {
            WaitForSingleObject(timer, INFINITE);
            return;
        }
    }

    Sleep((DWORD)(remainingNs / 1000000));
}

void iHostLocalTimeOf(S64 unixSeconds, iHostCalendar* out)
{
    time_t when = (time_t)unixSeconds;
    tm t;
    localtime_s(&t, &when);

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

void* iHostReserveLow(U32 size)
{
    // Windows has no MAP_32BIT. The equivalent is to ask for specific low base
    // addresses and take the first that is free. Start above 64 MB to stay
    // clear of where images are normally based, and step by 64 MB because that
    // is the allocation granularity's practical stride for a block this large.
    for (U64 base = 0x04000000ULL; base < 0x80000000ULL; base += 0x04000000ULL)
    {
        void* p =
            VirtualAlloc((LPVOID)(uintptr_t)base, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p != NULL)
        {
            return p;
        }
    }

    // Nothing low was free. Take whatever the OS offers and let iMemInit's
    // "above 4 GB" check refuse to start, rather than truncating silently.
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

void iHostRelease(void* p, U32 size)
{
    if (p != NULL)
    {
        // MEM_RELEASE frees the whole reservation and requires size 0.
        VirtualFree(p, 0, MEM_RELEASE);
    }
}

// ---------------------------------------------------------------------------
// Filesystem

bool iHostPathExists(const char* path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

bool iHostStat(const char* path, iHostFileInfo* out)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad))
    {
        return false;
    }

    bool dir = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out->is_dir = dir;
    out->is_file = !dir;
    out->size = ((U64)fad.nFileSizeHigh << 32) | (U64)fad.nFileSizeLow;

    // FILETIME counts 100 ns units from 1601; Unix time counts seconds from
    // 1970. 11644473600 is the gap in seconds.
    U64 ft =
        ((U64)fad.ftLastWriteTime.dwHighDateTime << 32) | (U64)fad.ftLastWriteTime.dwLowDateTime;
    out->mtime = (S64)(ft / 10000000ULL) - 11644473600LL;
    return true;
}

bool iHostMakeDir(const char* path)
{
    if (CreateDirectoryA(path, NULL))
    {
        return true;
    }

    // Already there is success, as on the POSIX side.
    iHostFileInfo info;
    return iHostStat(path, &info) && info.is_dir;
}

bool iHostRemoveFile(const char* path)
{
    return DeleteFileA(path) != 0;
}

FILE* iHostCreateNewFile(const char* path)
{
    // CREATE_NEW is the whole point: it fails when the file exists, and it is
    // the OS that decides, so two processes racing cannot both win.
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        return NULL;
    }

    // The handle becomes the FILE*'s, and closing the FILE* closes it.
    int fd = _open_osfhandle((intptr_t)h, _O_WRONLY | _O_BINARY);
    if (fd == -1)
    {
        CloseHandle(h);
        return NULL;
    }

    FILE* f = _fdopen(fd, "wb");
    if (f == NULL)
    {
        _close(fd);
        return NULL;
    }

    return f;
}

bool iHostRemoveDir(const char* path)
{
    return RemoveDirectoryA(path) != 0;
}

bool iHostTempDir(char* out, size_t outsize)
{
    char buf[MAX_PATH + 1];
    DWORD n = GetTempPathA(sizeof(buf), buf);
    if (n == 0 || n > MAX_PATH)
    {
        return false;
    }

    // GetTempPath always ends in a backslash; the seam's paths do not carry a
    // trailing separator, and the rest of the layer uses forward slashes.
    while (n > 0 && (buf[n - 1] == BS_CHAR || buf[n - 1] == '/'))
    {
        buf[--n] = 0;
    }

    for (DWORD i = 0; i < n; i++)
    {
        if (buf[i] == BS_CHAR)
        {
            buf[i] = '/';
        }
    }

    snprintf(out, outsize, "%s", buf);
    return true;
}

bool iHostExeDir(char* out, size_t outsize)
{
    char buf[MAX_PATH + 1];

    // Cleared first: GetModuleFileName reports truncation only through
    // GetLastError, and leaves it alone when it succeeds -- so without this the
    // test below would read whatever the last unrelated Win32 call left there.
    SetLastError(ERROR_SUCCESS);

    DWORD n = GetModuleFileNameA(NULL, buf, MAX_PATH + 1);

    // It truncates rather than failing when the buffer is too small. A
    // truncated path names a different file, so it is a failure here.
    if (n == 0 || n > MAX_PATH || GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        return false;
    }

    buf[n] = 0;

    for (DWORD i = 0; i < n; i++)
    {
        if (buf[i] == BS_CHAR)
        {
            buf[i] = '/';
        }
    }

    char* slash = strrchr(buf, '/');
    if (slash == NULL)
    {
        return false;
    }
    *slash = 0;

    snprintf(out, outsize, "%s", buf);
    return true;
}

bool iHostSetEnv(const char* name, const char* value)
{
    // _putenv_s with an empty value is how Windows spells unsetenv.
    return _putenv_s(name, value != NULL ? value : "") == 0;
}

bool iHostSetChildEnv(const char* name, const char* value)
{
    // The Win32 block, which is what a child inherits. NULL removes it, which
    // is what SetEnvironmentVariable already does with a NULL value.
    return SetEnvironmentVariableA(name, value) != 0;
}

bool iHostRunDetached(const char* exe, const char* workingDir)
{
    STARTUPINFOA startup;
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);

    PROCESS_INFORMATION process;
    memset(&process, 0, sizeof(process));

    if (!CreateProcessA(exe, NULL, NULL, NULL, FALSE, 0, NULL, workingDir, &startup, &process))
    {
        return false;
    }

    // Closed, not waited on. The handles are this process's claim on the
    // child, and holding them is what would keep a finished one around.
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

S32 iHostClaimInstance(const char* key, U32* holder)
{
    if (holder != NULL)
    {
        *holder = 0;
    }

    // A named section of four bytes in this logon session's namespace: the
    // first process to create it holds the claim and writes its id there. The
    // kernel destroys it when the last handle goes, and the system closes a
    // process's handles however it ends.
    char name[160];
    snprintf(name, sizeof(name), "Local\\%s", key != NULL ? key : "tssm");

    HANDLE h =
        CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(DWORD), name);
    if (h == NULL)
    {
        return -1;
    }
    const bool taken = GetLastError() == ERROR_ALREADY_EXISTS;

    volatile DWORD* id = (volatile DWORD*)MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, sizeof(DWORD));
    if (taken)
    {
        // 0 when the holder has not written it yet.
        if (id != NULL && holder != NULL)
        {
            *holder = *id;
        }
        if (id != NULL)
        {
            UnmapViewOfFile((const void*)id);
        }
        CloseHandle(h);
        return 0;
    }

    if (id != NULL)
    {
        *id = GetCurrentProcessId();
    }

    // Neither closed nor unmapped: the open handle is the claim.
    return 1;
}

bool iHostInstanceHeld(const char* key, U32* holder)
{
    if (holder != NULL)
    {
        *holder = 0;
    }

    char name[160];
    snprintf(name, sizeof(name), "Local\\%s", key != NULL ? key : "tssm");

    // Opened, never created: a section nobody holds does not exist.
    HANDLE h = OpenFileMappingA(FILE_MAP_READ, FALSE, name);
    if (h == NULL)
    {
        return false;
    }

    DWORD id = 0;
    volatile DWORD* view = (volatile DWORD*)MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(DWORD));
    if (view != NULL)
    {
        id = *view;
        UnmapViewOfFile((const void*)view);
    }
    CloseHandle(h);

    if (id == GetCurrentProcessId())
    {
        return false;
    }
    if (holder != NULL)
    {
        *holder = id;
    }
    return true;
}

namespace
{
    struct RaiseSearch
    {
        DWORD pid;
        HWND found;
    };

    BOOL CALLBACK RaiseFind(HWND hwnd, LPARAM param)
    {
        RaiseSearch* s = (RaiseSearch*)param;

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != s->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != NULL)
        {
            return TRUE;
        }

        // The game's window, not the console it may have been given.
        char cls[64];
        if (GetClassNameA(hwnd, cls, sizeof(cls)) > 0 && strcmp(cls, "ConsoleWindowClass") == 0)
        {
            return TRUE;
        }

        s->found = hwnd;
        return FALSE;
    }

    // The file name of process `pid`'s executable, without its folder.
    bool ProcessExeName(DWORD pid, char* out, size_t outsize)
    {
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (p == NULL)
        {
            return false;
        }
        char path[MAX_PATH + 1];
        DWORD n = sizeof(path);
        const bool ok = QueryFullProcessImageNameA(p, 0, path, &n) != 0;
        CloseHandle(p);
        if (!ok)
        {
            return false;
        }
        const char* slash = strrchr(path, '\\');
        snprintf(out, outsize, "%s", slash != NULL ? slash + 1 : path);
        return true;
    }
} // namespace

S32 iHostRaiseProcessWindow(U32 pid)
{
    if (pid == 0 || pid == GetCurrentProcessId())
    {
        return 0;
    }

    RaiseSearch s = { (DWORD)pid, NULL };
    EnumWindows(RaiseFind, (LPARAM)&s);
    if (s.found == NULL)
    {
        return 0;
    }

    if (IsHungAppWindow(s.found))
    {
        return 2;
    }

    if (IsIconic(s.found))
    {
        ShowWindow(s.found, SW_RESTORE);
    }
    SetForegroundWindow(s.found);
    return 1;
}

bool iHostOtherCopyInFront()
{
    HWND front = GetForegroundWindow();
    if (front == NULL)
    {
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(front, &pid);
    if (pid == 0 || pid == GetCurrentProcessId())
    {
        return false;
    }

    // Both names are asked of the system the same way, so a renamed copy of
    // the executable counts as another game only when it has the same name.
    static char self[MAX_PATH + 1] = "";
    if (self[0] == '\0' && !ProcessExeName(GetCurrentProcessId(), self, sizeof(self)))
    {
        return false;
    }
    char other[MAX_PATH + 1];
    return ProcessExeName(pid, other, sizeof(other)) && _stricmp(self, other) == 0;
}

bool iHostAbsolutePath(const char* path, char* out, size_t outsize)
{
    char buf[MAX_PATH + 1];

    DWORD n = GetFullPathNameA(path, (DWORD)sizeof(buf), buf, NULL);
    if (n == 0 || n >= sizeof(buf))
    {
        return false;
    }

    for (DWORD i = 0; i < n; i++)
    {
        if (buf[i] == BS_CHAR)
        {
            buf[i] = '/';
        }
    }

    snprintf(out, outsize, "%s", buf);
    return true;
}

bool iHostRenameReplace(const char* from, const char* to)
{
    // Not rename(): the CRT's fails when the destination exists. MOVEFILE_
    // REPLACE_EXISTING is the behaviour POSIX rename() already has, and
    // WRITE_THROUGH makes the replacement durable before this returns.
    //
    // A virus scanner, the search indexer or a sync client (OneDrive and the
    // like) opens a file it has just seen change for a moment, without
    // FILE_SHARE_DELETE, and while it has it open the replace fails with a
    // sharing or access error. The save would be lost for a scan of a few
    // hundred milliseconds, so those errors are tried again, every 50 ms for
    // up to two seconds. Any other error (no such file, a full disk) fails at
    // once.
    const int RETRIES = 40;
    const DWORD RETRY_MS = 50;
    for (int attempt = 0;; attempt++)
    {
        if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            return true;
        }

        const DWORD e = GetLastError();
        const bool held =
            e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION;
        if (!held || attempt >= RETRIES)
        {
            return false;
        }
        Sleep(RETRY_MS);
    }
}

bool iHostFlushFile(FILE* f)
{
    // _commit is the CRT's FlushFileBuffers on the descriptor's handle.
    return f != NULL && fflush(f) == 0 && _commit(_fileno(f)) == 0;
}

bool iHostFlushDir(const char* path)
{
    (void)path;
    return true;
}

bool iHostFreeBytes(const char* path, U64* out)
{
    ULARGE_INTEGER avail;
    if (!GetDiskFreeSpaceExA(path, &avail, NULL, NULL))
    {
        return false;
    }

    *out = (U64)avail.QuadPart;
    return true;
}

bool iHostSetFileTime(const char* path, S64 unixSeconds)
{
    HANDLE h = CreateFileA(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    // The other way from iHostStat: seconds from 1970 to 100 ns from 1601.
    U64 ft = (U64)(unixSeconds + 11644473600LL) * 10000000ULL;
    FILETIME t;
    t.dwLowDateTime = (DWORD)ft;
    t.dwHighDateTime = (DWORD)(ft >> 32);

    bool ok = SetFileTime(h, NULL, &t, &t) != 0;
    CloseHandle(h);
    return ok;
}

bool iHostCommandLine(S32* argc, char*** argv)
{
    // The CRT's own copy, in the ANSI code page like every path through here
    // (the *A calls above).
    *argc = (S32)__argc;
    *argv = __argv;
    return __argv != NULL;
}

struct iHostDir
{
    HANDLE h;
    WIN32_FIND_DATAA fd;
    bool pending;
};

iHostDir* iHostDirOpen(const char* path)
{
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s/*", path);

    iHostDir* d = (iHostDir*)malloc(sizeof(iHostDir));
    if (d == NULL)
    {
        return NULL;
    }

    d->h = FindFirstFileA(pattern, &d->fd);
    if (d->h == INVALID_HANDLE_VALUE)
    {
        free(d);
        return NULL;
    }

    // FindFirstFile has already produced the first entry, so the first call to
    // iHostDirNext must return it rather than advancing past it.
    d->pending = true;
    return d;
}

const char* iHostDirNext(iHostDir* d)
{
    if (d == NULL)
    {
        return NULL;
    }

    for (;;)
    {
        if (!d->pending)
        {
            if (!FindNextFileA(d->h, &d->fd))
            {
                return NULL;
            }
        }
        d->pending = false;

        if (strcmp(d->fd.cFileName, ".") == 0 || strcmp(d->fd.cFileName, "..") == 0)
        {
            continue;
        }

        return d->fd.cFileName;
    }
}

void iHostDirClose(iHostDir* d)
{
    if (d != NULL)
    {
        FindClose(d->h);
        free(d);
    }
}

bool iHostUserDataDir(char* out, size_t outsize)
{
    const char* appdata = getenv("APPDATA");
    if (appdata != NULL && appdata[0] != '\0')
    {
        snprintf(out, outsize, "%s", appdata);
        return true;
    }

    return false;
}

S32 iHostStrCaseCmp(const char* a, const char* b)
{
    return _stricmp(a, b);
}

const char* iHostName()
{
    return "win32";
}

// Only when the message would otherwise be lost.
//
// A dialog blocks until someone clicks it. Someone who started the game from a
// prompt has already read the same text there, so putting one in front of them
// turns an immediate exit into a process waiting on a click -- and does the
// same to a script, which will never make one.
//
// The test is NOT whether a console exists. This executable is console
// subsystem, so double-clicking it gets one either way; what differs is who
// else is attached to it. GetConsoleProcessList counting one means the console
// was created for this process alone and closes the moment it exits, taking
// every line printed with it -- a flash of text and then nothing, which is the
// case the box exists for. More than one means a shell is there too and the
// text stays on the screen after the game is gone.
void iHostErrorBox(const char* title, const char* message)
{
    DWORD pids[2];
    DWORD attached = GetConsoleProcessList(pids, 2);

    if (attached > 1)
    {
        return;
    }

    // MB_SETFOREGROUND because the failure this exists for happens while the
    // game window is coming up or already covering the screen, and a dialog
    // behind it is a dialog nobody sees. MB_TOPMOST for the exclusive
    // fullscreen case, where there is a display the game owns to get out from
    // under.
    //
    // A null owner rather than the game window: this is called when there may
    // not be one, and one that exists is about to be torn down anyway.
    MessageBoxA(NULL, message != NULL ? message : "", title != NULL ? title : "tssm",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
}

// Symbolised caller stack, for diagnostics. See iHost.h.
//
// CaptureStackBackTrace rather than StackWalk64: this is called from ordinary
// running code with no CONTEXT to hand, and the frame-pointer walk in
// tssm_main.cpp's crash handler needs one. Symbols come from the PDB through
// dbghelp, the same way, and a build without one prints addresses -- still
// enough to tell two different callers apart.
void iHostPrintCallers(const char* why, S32 maxFrames)
{
    static bool symbolsReady = false;
    if (!symbolsReady)
    {
        symbolsReady = true;
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(GetCurrentProcess(), NULL, TRUE);
    }

    if (maxFrames < 1)
    {
        maxFrames = 1;
    }
    else if (maxFrames > 48)
    {
        maxFrames = 48;
    }

    void* frames[48];
    USHORT got = CaptureStackBackTrace(1, (DWORD)maxFrames, frames, NULL);

    printf("tssm: callers -- %s\n", why != NULL ? why : "");

    char symbolBuffer[sizeof(SYMBOL_INFO) + 512];
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)symbolBuffer;
    memset(symbolBuffer, 0, sizeof(symbolBuffer));
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 500;

    HANDLE process = GetCurrentProcess();

    for (USHORT i = 0; i < got; i++)
    {
        DWORD64 addr = (DWORD64)(uintptr_t)frames[i];
        DWORD64 displacement = 0;
        const char* name = "?";

        if (SymFromAddr(process, addr, &displacement, symbol))
        {
            name = symbol->Name;
        }

        IMAGEHLP_LINE64 line;
        memset(&line, 0, sizeof(line));
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;

        if (SymGetLineFromAddr64(process, addr, &lineDisplacement, &line))
        {
            printf("tssm:   #%-2d %s  (%s:%lu)\n", (int)i, name, line.FileName,
                   (unsigned long)line.LineNumber);
        }
        else
        {
            printf("tssm:   #%-2d %s + 0x%llx\n", (int)i, name, (unsigned long long)displacement);
        }
    }

    fflush(stdout);
}
