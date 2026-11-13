// tssm_main.cpp: treedome's bfbb_main.cpp, renamed for TSSM (BFBB_ -> TSSM_ in
// the environment switches, "tssm:" in the log). The executable's own source:
// stdout unbuffered before main(), the crash handler and watchdog, and the
// banner naming the render backends and the movie decoder built in. main() is
// the game's own, in src/SB/Game/zMain.cpp.
//
// Treedome's account of why each piece exists follows unchanged.

// The executable's own translation unit. The game's entry point is its own --
// main() is in src/SB/Game/zMain.cpp, and it is the retail one.
//
// Two jobs, both of which have to happen before main() runs, which is why they
// are a static constructor rather than a function anything calls:
//
//   1. Unbuffer stdout. When the port's output is redirected to a file, stdout
//      is FULLY buffered, so a crash discards everything printed since the last
//      4KB. The first attempt to boot this executable produced an empty log and
//      a segfault, and the empty log was the buffering rather than an early
//      crash -- iSystemInit's own startup line had already run. With eleven
//      platform modules still stubbed, "which line came last" is the entire
//      diagnosis, so losing the tail is losing the answer.
//
//   2. Say what this build is. All eleven platform modules are implemented now,
//      but two of them -- iFX and iFMV -- are deliberate refusals rather than
//      ports, so the banner names what is missing instead of letting someone
//      discover it by watching a cutscene that never plays.
//
// This file is also what gives the CMake executable a source of its own, and
// what makes the compiler driver name the CRT -- linking the archives with no
// driver input leaves eight ordinary libc symbols unresolved. tools/pclink.py
// does the same thing for the same reason.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

// TSSM_TEST_HANG_DUMP logs SDL's assertions too (iHangDump.h), where the
// build has SDL.
#if defined(__has_include)
#if __has_include(<SDL3/SDL_assert.h>)
#include <SDL3/SDL_assert.h>
#define TSSM_HANG_DUMP_SDL_ASSERT 1
#endif
#endif

// An AddressSanitizer build: clang's __has_feature, or the macro GCC and MSVC define.
// Nested, because a preprocessor without __has_feature rejects it even behind defined().
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TSSM_UNDER_ASAN 1
#endif
#endif
#if !defined(TSSM_UNDER_ASAN) && defined(__SANITIZE_ADDRESS__)
#define TSSM_UNDER_ASAN 1
#endif
#else
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

// The POSIX arm walks the stack through the host seam rather than writing its
// own. iHostPrintCallers is the same job -- "which code leads here", symbolised
// -- and having one implementation means a frame reads the same whether the
// crash handler or the platform layer asked for it.
#include "iHost.h"
#endif

// Named in the startup banner, so that a build says which movie decoder it
// actually has rather than the reader guessing.
#include "iFMVAudio.h"
#include "iFMVDecoder.h"

// The log the process keeps of its own output, and crash.log beside it.
#include "iLog.h"

// TSSM_TEST_HANG_DUMP: the game thread's heartbeat.
#include "iHangDump.h"

#ifdef __ANDROID__
// The log has to be opened from here rather than from the Android shim's own
// startup, because the banner below is printed from a static constructor and
// anything printed before the redirect is discarded by the platform. See
// android/iAndroid.h for the two halves.
#include "android/iAndroid.h"
#endif

#ifdef _WIN32
namespace
{
    // A crash handler, because the port is going to crash for a while yet and
    // the alternative is bisecting with printf.
    //
    // The GameCube build has Dolphin's GDB stub and OSPanic for this; a host
    // build has neither unless it brings its own. Without it a fault is a bare
    // "Segmentation fault" from the shell and a guess about which line -- which
    // is how the first three attempts at diagnosing xFXInit went, two of them
    // to wrong conclusions.
    //
    // Symbols come from the PDB via dbghelp, which is present on every Windows
    // and needs no build flags beyond the -g the port already compiles with.

    // A VECTORED handler as well as the unhandled-exception filter above.
    //
    // The filter never ran for the fault this was written to diagnose, and that
    // is itself a diagnosis: an unhandled-exception filter needs stack to run
    // on, so a STACK OVERFLOW skips it entirely. A vectored handler is called
    // first-chance, before unwinding, while the guard page is still doing its
    // job -- so it gets to say what happened when the filter cannot.

    // Which binary an address is in, and where in it. dbghelp answers the first
    // through the module base it already tracks for the stack walk, so this
    // costs nothing beyond a GetModuleFileName -- and it is the part of a
    // backtrace that never degrades, symbols or no symbols.
    // The crash report's second copy. A report goes to stdout -- the
    // console, a file a caller redirected it to, and the log (iLog.h) -- and,
    // while a crash or abort handler is writing one, to crash.log as well, so
    // that a report survives however the game was started and however many
    // runs follow it. Formatted on the stack: the heap may be what failed.
    HANDLE sReportFile = INVALID_HANDLE_VALUE;

    void Report(const char* fmt, ...)
    {
        char line[1536];
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

        fwrite(line, 1, (size_t)n, stdout);
        if (sReportFile != INVALID_HANDLE_VALUE)
        {
            DWORD wrote = 0;
            WriteFile(sReportFile, line, (DWORD)n, &wrote, NULL);
        }
    }

    // crash.log, appended to, with a line saying when and what. Past 1 MB it
    // starts again, the old one kept as crash.prev.log.
    void OpenReport()
    {
        const char* path = iLogCrashPath();
        if (path == NULL || path[0] == '\0' || sReportFile != INVALID_HANDLE_VALUE)
        {
            return;
        }

        WIN32_FILE_ATTRIBUTE_DATA a;
        if (GetFileAttributesExA(path, GetFileExInfoStandard, &a) &&
            (a.nFileSizeHigh != 0 || a.nFileSizeLow > 1024 * 1024))
        {
            char prev[760];
            const size_t n = strlen(path);
            snprintf(prev, sizeof(prev), "%.*s.prev.log", (int)(n > 4 ? n - 4 : n), path);
            MoveFileExA(path, prev, MOVEFILE_REPLACE_EXISTING);
        }

        sReportFile = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (sReportFile == INVALID_HANDLE_VALUE)
        {
            return;
        }

        SYSTEMTIME t;
        GetLocalTime(&t);
        char exe[MAX_PATH];
        if (GetModuleFileNameA(NULL, exe, sizeof(exe)) == 0)
        {
            snprintf(exe, sizeof(exe), "tssm");
        }
        char line[1024];
        int n = snprintf(line, sizeof(line),
                         "\n==== %04u-%02u-%02u %02u:%02u:%02u  %s (process %lu, built %s %s)\n",
                         t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, exe,
                         (unsigned long)GetCurrentProcessId(), __DATE__, __TIME__);
        if (n > 0)
        {
            DWORD wrote = 0;
            WriteFile(sReportFile, line, (DWORD)(n < (int)sizeof(line) ? n : (int)sizeof(line) - 1),
                      &wrote, NULL);
        }
    }

    // The end of a report: crash.log closed, the log given what is still on
    // its way to it, and the reader told where the copy is.
    void CloseReport()
    {
        if (sReportFile != INVALID_HANDLE_VALUE)
        {
            FlushFileBuffers(sReportFile);
            CloseHandle(sReportFile);
            sReportFile = INVALID_HANDLE_VALUE;
            printf("tssm: this report is also in %s\n", iLogCrashPath());
        }
        iLogDrain(1500);
    }

    const char* ModuleName(HANDLE process, DWORD64 address)
    {
        static char name[MAX_PATH];

        DWORD64 base = SymGetModuleBase64(process, address);
        if (base == 0)
        {
            return "?";
        }

        if (GetModuleFileNameA((HMODULE)(uintptr_t)base, name, sizeof(name)) == 0)
        {
            return "?";
        }

        // The leaf, because the directory is the same for all of them and the
        // line is long enough already.
        const char* slash = strrchr(name, '\\');
        return slash != NULL ? slash + 1 : name;
    }

    DWORD64 ModuleOffset(HANDLE process, DWORD64 address)
    {
        DWORD64 base = SymGetModuleBase64(process, address);
        return base != 0 ? address - base : 0;
    }

#ifdef _M_X64
    const DWORD kStackWalkMachine = IMAGE_FILE_MACHINE_AMD64;
#else
    const DWORD kStackWalkMachine = IMAGE_FILE_MACHINE_I386;
#endif

    // Walks one thread's stack and prints it, symbolised. Shared by the crash
    // handler and the watchdog: a crash and a hang want the same answer --
    // "where is it" -- and differ only in how they come by a CONTEXT.
    void PrintBacktrace(HANDLE thread, CONTEXT* context)
    {
        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(process, NULL, TRUE);

                STACKFRAME64 frame;
        memset(&frame, 0, sizeof(frame));
#ifdef _M_X64
        frame.AddrPC.Offset = context->Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context->Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context->Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
#else
        frame.AddrPC.Offset = context->Eip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context->Ebp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context->Esp;
        frame.AddrStack.Mode = AddrModeFlat;
#endif

        char symbolBuffer[sizeof(SYMBOL_INFO) + 512];
        SYMBOL_INFO* symbol = (SYMBOL_INFO*)symbolBuffer;
        memset(symbolBuffer, 0, sizeof(symbolBuffer));
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 500;

        for (int depth = 0; depth < 32; depth++)
        {
            if (!StackWalk64(kStackWalkMachine, process, thread, &frame,
                             context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            {
                break;
            }

            if (frame.AddrPC.Offset == 0)
            {
                break;
            }

            DWORD64 displacement = 0;
            const char* name = "?";
            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
            {
                name = symbol->Name;
            }

            IMAGEHLP_LINE64 line;
            memset(&line, 0, sizeof(line));
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;

            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line))
            {
                Report("tssm:   #%-2d %s  (%s:%lu)\n", depth, name, line.FileName,
                       (unsigned long)line.LineNumber);
            }
            else
            {
                // The module, and the address inside it, alongside the symbol.
                //
                // Without this a frame in a DLL that ships no symbols reads as
                // the nearest preceding EXPORT plus a five-digit offset --
                // `SetDependencyInfo + 0x262590` -- which names a function the
                // code has nothing to do with and hides the one thing that is
                // always knowable: which binary it was in. A crash on a worker
                // thread is mostly diagnosed by that alone.
                Report("tssm:   #%-2d %s + 0x%llx   [%s+0x%llx]\n", depth, name,
                       (unsigned long long)displacement, ModuleName(process, frame.AddrPC.Offset),
                       (unsigned long long)ModuleOffset(process, frame.AddrPC.Offset));
            }
        }

        fflush(stdout);
    }

    HANDLE sMainThread;

    // Recorded unconditionally, unlike sMainThread, which only the
    // watchdog needs and only sets when it is asked for. The crash handler
    // runs on whichever thread faulted and wants to say which that was.
    DWORD sMainThreadId;

    // The watchdog, for HANGS.
    //
    // A crash announces itself; a hang does not. The port reaches further into
    // startup with every module ported and then stops somewhere without saying
    // where, and lldb would not attach on this machine -- so the process reports
    // on itself: a thread that wakes every few seconds, suspends the main
    // thread, walks its stack with the same code the crash handler uses, and
    // prints it.
    //
    // Repeating rather than firing once is deliberate. One trace cannot tell a
    // deadlock from slow progress. Two identical traces can; two different ones
    // say it is running and simply not finishing.
    //
    // Off unless TSSM_WATCHDOG names a number of seconds. Suspending the main
    // thread is not free, and nothing should do it to a build that works.
    DWORD WINAPI WatchdogThread(LPVOID param)
    {
        const DWORD seconds = (DWORD)(uintptr_t)param;

        for (int sample = 1;; sample++)
        {
            Sleep(seconds * 1000);

            printf("\ntssm: WATCHDOG sample %d -- main thread stack:\n", sample);

            if (SuspendThread(sMainThread) == (DWORD)-1)
            {
                printf("tssm:   (could not suspend the main thread)\n");
                continue;
            }

            CONTEXT context;
            memset(&context, 0, sizeof(context));
            context.ContextFlags = CONTEXT_FULL;

            if (GetThreadContext(sMainThread, &context))
            {
                PrintBacktrace(sMainThread, &context);
            }
            else
            {
                printf("tssm:   (could not read the main thread context)\n");
            }

            ResumeThread(sMainThread);
        }

        return 0;
    }

    // TSSM_TEST_HANG_DUMP (iHangDump.h): EVERY thread's stack, once, when the
    // game thread stops making progress, and then the end of the process.
    //
    // The watchdog above samples the main thread alone, which says where the
    // game is stuck but not on what: a deadlock is at least two threads, and
    // the other one is usually SDL's (an audio device thread, the WASAPI
    // management thread). And an unattended run that hangs should end with
    // the answer in its log rather than at its harness's timeout.
    //
    // Each thread is suspended only for as long as its stack is walked into a
    // list of addresses; the symbols are looked up and printed after it runs
    // again, so a thread caught holding stdout's lock cannot stall the dump
    // that is trying to describe it. (The walk itself can still allocate
    // inside dbghelp, so a thread suspended while holding the process heap's
    // lock could; a hung thread waiting on a mutex does not hold it.)

    // One dump at a time: the hang thread and an SDL assertion can both ask.
    SRWLOCK sDumpLock = SRWLOCK_INIT;

    typedef HRESULT(WINAPI* GetThreadDescriptionFn)(HANDLE, PWSTR*);

    // The name SDL (SetThreadDescription) or the CRT gave a thread: SDL names
    // its device threads "SDLAudioP<n>" and WASAPI's "SDLWASAPIMgmt".
    void ThreadName(HANDLE thread, char* out, size_t outsize)
    {
        out[0] = '\0';
        static GetThreadDescriptionFn getDescription =
            (GetThreadDescriptionFn)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                          "GetThreadDescription");
        if (getDescription == NULL)
        {
            return;
        }
        PWSTR wide = NULL;
        if (SUCCEEDED(getDescription(thread, &wide)) && wide != NULL)
        {
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int)outsize, NULL, NULL);
            LocalFree(wide);
        }
    }

    const int kDumpDepth = 48;

    // The return addresses of a thread's stack, from `context`. No output and
    // no symbol lookups: the thread may be suspended.
    int CollectBacktrace(HANDLE thread, CONTEXT* context, DWORD64* pcs, int max)
    {
        HANDLE process = GetCurrentProcess();

        STACKFRAME64 frame;
        memset(&frame, 0, sizeof(frame));
#ifdef _M_X64
        frame.AddrPC.Offset = context->Rip;
        frame.AddrFrame.Offset = context->Rbp;
        frame.AddrStack.Offset = context->Rsp;
#else
        frame.AddrPC.Offset = context->Eip;
        frame.AddrFrame.Offset = context->Ebp;
        frame.AddrStack.Offset = context->Esp;
#endif
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;

        int n = 0;
        while (n < max && StackWalk64(kStackWalkMachine, process, thread, &frame, context, NULL,
                                      SymFunctionTableAccess64, SymGetModuleBase64, NULL))
        {
            if (frame.AddrPC.Offset == 0)
            {
                break;
            }
            pcs[n++] = frame.AddrPC.Offset;
        }
        return n;
    }

    // The same lines PrintBacktrace writes, for addresses already collected.
    void PrintFrames(const DWORD64* pcs, int n)
    {
        HANDLE process = GetCurrentProcess();

        char symbolBuffer[sizeof(SYMBOL_INFO) + 512];
        SYMBOL_INFO* symbol = (SYMBOL_INFO*)symbolBuffer;

        for (int depth = 0; depth < n; depth++)
        {
            memset(symbolBuffer, 0, sizeof(symbolBuffer));
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 500;

            DWORD64 displacement = 0;
            const char* name = "?";
            if (SymFromAddr(process, pcs[depth], &displacement, symbol))
            {
                name = symbol->Name;
            }

            IMAGEHLP_LINE64 line;
            memset(&line, 0, sizeof(line));
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;

            if (SymGetLineFromAddr64(process, pcs[depth], &lineDisplacement, &line))
            {
                Report("tssm:   #%-2d %s  (%s:%lu)\n", depth, name, line.FileName,
                       (unsigned long)line.LineNumber);
            }
            else
            {
                Report("tssm:   #%-2d %s + 0x%llx   [%s+0x%llx]\n", depth, name,
                       (unsigned long long)displacement, ModuleName(process, pcs[depth]),
                       (unsigned long long)ModuleOffset(process, pcs[depth]));
            }
        }
    }

    void DumpAllThreads(const char* title)
    {
        AcquireSRWLockExclusive(&sDumpLock);

        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(process, NULL, TRUE);
        // DLLs loaded since (the audio driver's, FFmpeg's).
        SymRefreshModuleList(process);

        const DWORD self = GetCurrentThreadId();
        Report("\ntssm: ==== %s\n", title);
        Report("tssm: every thread of process %lu follows; the game thread is %lu\n",
               (unsigned long)GetCurrentProcessId(), (unsigned long)sMainThreadId);

        DWORD64 pcs[kDumpDepth];
        char name[128];

        // This thread first: for an assertion it is the one that failed.
        {
            CONTEXT context;
            memset(&context, 0, sizeof(context));
            RtlCaptureContext(&context);
            int n = CollectBacktrace(GetCurrentThread(), &context, pcs, kDumpDepth);
            ThreadName(GetCurrentThread(), name, sizeof(name));
            Report("\ntssm: thread %lu%s%s%s%s (the one reporting):\n", (unsigned long)self,
                   name[0] ? " \"" : "", name, name[0] ? "\"" : "",
                   self == sMainThreadId ? " (the game thread)" : "");
            PrintFrames(pcs, n);
        }

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap != INVALID_HANDLE_VALUE)
        {
            THREADENTRY32 te;
            te.dwSize = sizeof(te);
            for (BOOL more = Thread32First(snap, &te); more; more = Thread32Next(snap, &te))
            {
                if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == self)
                {
                    continue;
                }

                HANDLE t =
                    OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                   THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION,
                               FALSE, te.th32ThreadID);
                if (t == NULL)
                {
                    Report("\ntssm: thread %lu: (could not open it)\n",
                           (unsigned long)te.th32ThreadID);
                    continue;
                }

                ThreadName(t, name, sizeof(name));
                Report("\ntssm: thread %lu%s%s%s%s:\n", (unsigned long)te.th32ThreadID,
                       name[0] ? " \"" : "", name, name[0] ? "\"" : "",
                       te.th32ThreadID == sMainThreadId ? " (the game thread)" : "");

                int n = -1;
                if (SuspendThread(t) != (DWORD)-1)
                {
                    CONTEXT context;
                    memset(&context, 0, sizeof(context));
                    context.ContextFlags = CONTEXT_FULL;
                    if (GetThreadContext(t, &context))
                    {
                        n = CollectBacktrace(t, &context, pcs, kDumpDepth);
                    }
                    ResumeThread(t);
                }

                if (n < 0)
                {
                    Report("tssm:   (could not suspend it or read its context)\n");
                }
                else
                {
                    PrintFrames(pcs, n);
                }
                CloseHandle(t);
            }
            CloseHandle(snap);
        }

        Report("tssm: ==== end of the thread dump\n");
        fflush(stdout);

        ReleaseSRWLockExclusive(&sDumpLock);
    }

    DWORD WINAPI HangThread(LPVOID param)
    {
        const DWORD seconds = (DWORD)(uintptr_t)param;

        // Nothing is judged before the first heartbeat: startup (the settings,
        // the window, the asset checks) is not the game thread's loop yet.
        while (iHangDumpBeats() == 0)
        {
            Sleep(100);
        }

        printf("tssm: hang dump armed: every thread's stack, then exit code 3, after %lu s "
               "with no progress on the game thread (TSSM_TEST_HANG_DUMP)\n",
               (unsigned long)seconds);
        fflush(stdout);

        U32 last = iHangDumpBeats();
        ULONGLONG lastMoved = GetTickCount64();

        for (;;)
        {
            Sleep(100);

            const U32 now = iHangDumpBeats();
            const ULONGLONG t = GetTickCount64();
            if (now != last)
            {
                last = now;
                lastMoved = t;
                continue;
            }
            if (t - lastMoved < (ULONGLONG)seconds * 1000)
            {
                continue;
            }

            char title[200];
            snprintf(title, sizeof(title),
                     "HANG -- the game thread has made no progress for %.1f s "
                     "(TSSM_TEST_HANG_DUMP=%lu)",
                     (double)(t - lastMoved) / 1000.0, (unsigned long)seconds);
            DumpAllThreads(title);

            printf("tssm: HANG -- ending the process after the dump (exit code 3)\n");
            fflush(stdout);
            iLogDrain(2000);
            TerminateProcess(GetCurrentProcess(), 3);
            return 0;
        }
    }

#ifdef TSSM_HANG_DUMP_SDL_ASSERT
    // An SDL assertion, with TSSM_TEST_HANG_DUMP set: written to the log with
    // the stacks, and ignored -- what a release build of SDL, which compiles
    // its assertions out, does at the same place. SDL's own handler would show
    // a message box and wait on it, which in an unattended run is a hang of
    // its own.
    int sAssertDumps;

    SDL_AssertState SDLCALL HangAssertHandler(const SDL_AssertData* data, void*)
    {
        const DWORD self = GetCurrentThreadId();
        Report("\ntssm: SDL ASSERTION at %s (%s:%d), triggered %u time(s): '%s' -- on thread "
               "%lu%s\n",
               data->function != NULL ? data->function : "?",
               data->filename != NULL ? data->filename : "?", data->linenum,
               (unsigned)data->trigger_count, data->condition != NULL ? data->condition : "?",
               (unsigned long)self, self == sMainThreadId ? " (the game thread)" : "");

        if (sAssertDumps < 3)
        {
            sAssertDumps++;
            DumpAllThreads("SDL assertion (TSSM_TEST_HANG_DUMP): every thread's stack, the "
                           "asserting one first; the assertion is then ignored");
        }
        else
        {
            CONTEXT context;
            memset(&context, 0, sizeof(context));
            RtlCaptureContext(&context);
            PrintBacktrace(GetCurrentThread(), &context);
        }
        fflush(stdout);

        return SDL_ASSERTION_IGNORE;
    }
#endif

    // Aborts, which a crash handler does not see.
    //
    // An assert() inside librw ends in _wassert, which prints one line naming
    // the file and raises SIGABRT. No exception is raised, so neither of the
    // handlers below runs and the process dies having said only which assertion
    // failed -- not which of the game's calls tripped it, which is the part
    // worth knowing when the assertion is inside a library the game drives
    // through a shim.
    void AbortHandler(int)
    {
        OpenReport();
        Report("\ntssm: ABORT -- assertion or abort() call\n");

        CONTEXT context;
        memset(&context, 0, sizeof(context));
        RtlCaptureContext(&context);

        // The first few frames are this handler and the CRT's raise path.
        PrintBacktrace(GetCurrentThread(), &context);

        CloseReport();
        _exit(3);
    }

    LONG WINAPI FirstChanceHandler(EXCEPTION_POINTERS* info)
    {
        const DWORD code = info->ExceptionRecord->ExceptionCode;

        // Only the fatal ones. First-chance C++ exceptions and the debugger's
        // own breakpoints come through here too and are not interesting.
        if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_STACK_OVERFLOW ||
            code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_INT_DIVIDE_BY_ZERO)
        {
            // Module and offset as well as the raw address. The full report
            // below allocates, and a corrupt heap takes the process down
            // before it prints; this line is the one that always survives, so
            // it carries the part a PDB can be pointed at.
            // GetModuleHandle, not dbghelp: this runs before SymInitialize
            // and has to work when the report below cannot run at all.
            DWORD64 address = (DWORD64)(uintptr_t)info->ExceptionRecord->ExceptionAddress;
            DWORD64 base = (DWORD64)(uintptr_t)GetModuleHandleW(NULL);

            printf("\ntssm: first-chance exception 0x%08lx at %p  [tssm.exe+0x%llx]\n",
                   (unsigned long)code, info->ExceptionRecord->ExceptionAddress,
                   (unsigned long long)(address - base));

            if (code == EXCEPTION_STACK_OVERFLOW)
            {
                printf("tssm:   STACK OVERFLOW -- runaway recursion, not a bad pointer\n");
            }

            fflush(stdout);
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    LONG WINAPI CrashHandler(EXCEPTION_POINTERS* info)
    {
        const DWORD code = info->ExceptionRecord->ExceptionCode;

        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(process, NULL, TRUE);

        OpenReport();

        const DWORD64 pc = (DWORD64)(uintptr_t)info->ExceptionRecord->ExceptionAddress;
        Report("\ntssm: CRASH -- exception 0x%08lx at %p  [%s+0x%llx]\n", (unsigned long)code,
               info->ExceptionRecord->ExceptionAddress, ModuleName(process, pc),
               (unsigned long long)ModuleOffset(process, pc));

        // Which thread. A crash on a worker thread and one on the game's own
        // are different bugs, and the stack alone does not always say which
        // this was -- the main thread's is the one that ends in main().
        Report("tssm:   thread %lu%s\n", (unsigned long)GetCurrentThreadId(),
               GetCurrentThreadId() == sMainThreadId ? " (the main thread)" : " (a worker)");

        if (code == EXCEPTION_ACCESS_VIOLATION)
        {
            // The second parameter is the address that was touched, and it is
            // usually the whole diagnosis: 0 is a null dereference, a small
            // value is a null plus a field offset, garbage is a wild pointer.
            Report("tssm:   access violation %s address %p\n",
                   info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                   (void*)info->ExceptionRecord->ExceptionInformation[1]);
        }

        PrintBacktrace(GetCurrentThread(), info->ContextRecord);
        CloseReport();
        return EXCEPTION_EXECUTE_HANDLER;
    }

    void InstallDiagnostics()
    {
        sMainThreadId = GetCurrentThreadId();

#ifndef TSSM_UNDER_ASAN
        // Not under AddressSanitizer: ASan commits its shadow memory on demand from its own
        // vectored handler, and an instrumented handler first in line faults on that shadow
        // and recurses until the stack is gone, before the game starts.
        AddVectoredExceptionHandler(1, FirstChanceHandler);
#endif
        signal(SIGABRT, AbortHandler);

        const char* watchdogSeconds = getenv("TSSM_WATCHDOG");
        if (watchdogSeconds != NULL)
        {
            DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                            &sMainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
            CreateThread(NULL, 0, WatchdogThread, (LPVOID)(uintptr_t)atoi(watchdogSeconds), 0,
                         NULL);
        }

        // TSSM_TEST_HANG_DUMP=<seconds> (iHangDump.h).
        const char* hangSeconds = getenv("TSSM_TEST_HANG_DUMP");
        if (hangSeconds != NULL && hangSeconds[0] != '\0')
        {
            int seconds = atoi(hangSeconds);
            if (seconds <= 0)
            {
                seconds = 5;
            }
            iHangDumpSetDumper(DumpAllThreads);
#ifdef TSSM_HANG_DUMP_SDL_ASSERT
            SDL_SetAssertionHandler(HangAssertHandler, NULL);
#endif
            CreateThread(NULL, 0, HangThread, (LPVOID)(uintptr_t)seconds, 0, NULL);
        }

        SetUnhandledExceptionFilter(CrashHandler);
    }
}

#else

namespace
{
    // The POSIX half of the same job. Everything above is Win32 structured
    // exceptions and dbghelp, neither of which has an equivalent here: a fault
    // arrives as a SIGNAL, and there is one chance to report on it rather than
    // the first-chance/unhandled pair, so this is one handler and not two.
    //
    // Neither backtrace() nor printf is async-signal-safe, and both are called
    // below. The Win32 arm has the same property -- SymInitialize allocates --
    // and for the same reason: this runs when the process is already dying and
    // a report that usually arrives beats a rule that never lets it.

    pthread_t sMainThread;

    // Space for the fatal handler to run on, because the fault it most needs to
    // report is a STACK OVERFLOW and the thread's own stack is by then the
    // thing that ran out. SA_ONSTACK plus this is what Windows gets from a
    // vectored handler being called before unwinding.
    //
    // A literal rather than SIGSTKSZ. Since glibc 2.34 that macro expands to a
    // sysconf() call, so it cannot size an array, and it is a few kilobytes
    // either way -- not enough for backtrace_symbols, which allocates and
    // formats.
    char sSignalStack[65536];

#ifdef __ANDROID__
    // The game thread's own. An alternate signal stack is per thread, and on
    // Android the thread that ran InstallDiagnostics (the UI thread, inside
    // System.loadLibrary) is not the one the game runs on; sharing one buffer
    // between two threads that can fault at once would be a second bug.
    char sGameSignalStack[65536];
#endif

    // crash.log (iLog.h): the fault and where the full report is. The
    // backtrace itself goes to stdout, and so to the log, through
    // iHostPrintCallers.
    void ReportToCrashLog(const char* what)
    {
        const char* path = iLogCrashPath();
        if (path == NULL || path[0] == '\0')
        {
            return;
        }
        int fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
        if (fd < 0)
        {
            return;
        }
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        char line[1024];
        int n =
            snprintf(line, sizeof(line),
                     "\n==== %04d-%02d-%02d %02d:%02d:%02d  tssm (process %d, built %s %s)\n%s\n"
                     "the backtrace is in %s\n",
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
                     (int)getpid(), __DATE__, __TIME__, what,
                     iLogPath()[0] != '\0' ? iLogPath() : "the game's output");
        if (n > 0)
        {
            ssize_t wrote =
                write(fd, line, (size_t)(n < (int)sizeof(line) ? n : (int)sizeof(line) - 1));
            (void)wrote;
        }
        close(fd);
    }

    void FatalSignal(int sig, siginfo_t* info, void*)
    {
        printf("\ntssm: CRASH -- %s\n", strsignal(sig));

        // The address that was touched, which is usually the whole diagnosis:
        // 0 is a null dereference, a small value is a null plus a field offset,
        // garbage is a wild pointer. Only the memory faults set it.
        if (sig == SIGSEGV || sig == SIGBUS)
        {
            printf("tssm:   faulting address %p\n", info->si_addr);
        }

        // Which thread. A crash on a worker and one on the game's own are
        // different bugs, and the stack alone does not always say which.
        printf("tssm:   %s\n", pthread_equal(pthread_self(), sMainThread) ? "the main thread"
                                                                          : "a worker thread");

        iHostPrintCallers("crash", 32);
        char what[256];
        snprintf(what, sizeof(what), "tssm: CRASH -- %s, address %p", strsignal(sig),
                 (sig == SIGSEGV || sig == SIGBUS) ? info->si_addr : NULL);
        ReportToCrashLog(what);
        iLogDrain(1500);
        _exit(3);
    }

    // Aborts, which are a signal here as well but not a fault: assert() inside
    // librw ends in abort() having printed only which assertion failed, and the
    // part worth knowing is which of the game's calls tripped it.
    void AbortSignal(int)
    {
        printf("\ntssm: ABORT -- assertion or abort() call\n");
        iHostPrintCallers("abort", 32);
        ReportToCrashLog("tssm: ABORT -- assertion or abort() call");
        iLogDrain(1500);
        _exit(3);
    }

    // The watchdog's sample, running ON the main thread.
    //
    // Windows suspends the main thread and walks it from outside. POSIX has no
    // portable way to do that -- there is no ptrace-free equivalent of
    // GetThreadContext -- so the direction is inverted: the watchdog thread
    // interrupts the main thread with a signal and the main thread reports on
    // itself. The stack it prints is the one it was executing, with this
    // handler and the trampoline on top of it.
    void SampleSignal(int)
    {
        iHostPrintCallers("watchdog", 32);
        fflush(stdout);
    }

    void* WatchdogThread(void* param)
    {
        const unsigned seconds = (unsigned)(uintptr_t)param;

        for (int sample = 1;; sample++)
        {
            sleep(seconds);

            printf("\ntssm: WATCHDOG sample %d -- main thread stack:\n", sample);
            fflush(stdout);

            pthread_kill(sMainThread, SIGUSR1);
        }

        return NULL;
    }

    void InstallDiagnostics()
    {
        sMainThread = pthread_self();

        stack_t altstack;
        memset(&altstack, 0, sizeof(altstack));
        altstack.ss_sp = sSignalStack;
        altstack.ss_size = sizeof(sSignalStack);
        sigaltstack(&altstack, NULL);

        struct sigaction fatal;
        memset(&fatal, 0, sizeof(fatal));
        fatal.sa_sigaction = FatalSignal;
        fatal.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&fatal.sa_mask);

        // SIGBUS as well as SIGSEGV: on macOS an unmapped address raises
        // SIGBUS where Linux raises SIGSEGV, so a handler for one of them
        // catches half the faults depending on the host.
        sigaction(SIGSEGV, &fatal, NULL);
        sigaction(SIGBUS, &fatal, NULL);
        sigaction(SIGILL, &fatal, NULL);
        sigaction(SIGFPE, &fatal, NULL);

        signal(SIGABRT, AbortSignal);

        const char* watchdogSeconds = getenv("TSSM_WATCHDOG");
        if (watchdogSeconds != NULL)
        {
            signal(SIGUSR1, SampleSignal);

            pthread_t watchdog;
            pthread_create(&watchdog, NULL, WatchdogThread,
                           (void*)(uintptr_t)atoi(watchdogSeconds));
            pthread_detach(watchdog);
        }
    }
}

#ifdef __ANDROID__
void iAndroidAdoptGameThread()
{
    // What InstallDiagnostics did for the thread it ran on, done again for
    // this one. The handlers themselves are process-wide and stay installed.
    sMainThread = pthread_self();

    stack_t altstack;
    memset(&altstack, 0, sizeof(altstack));
    altstack.ss_sp = sGameSignalStack;
    altstack.ss_size = sizeof(sGameSignalStack);
    if (sigaltstack(&altstack, NULL) != 0)
    {
        printf("tssm: the game thread has no signal stack; a stack overflow will not be "
               "reported\n");
    }
}
#endif

#endif


namespace
{
    // An ordinary exit, exit(0) included, ends the process while the log's
    // threads may still hold its last lines; this lets them finish.
    void DrainAtExit()
    {
        iLogDrain(1000);
    }

    // What is BUILT IN, not what is running: this banner is printed from a
    // static constructor, before config.ini has been read, so video.backend has
    // not been answered yet. RwEngineStart prints the one that opened.
    //
    // librw exports these as public compile definitions, and CMakeLists.txt
    // explains why every target of the port has to see them.
    //
    // A list with a separator in front of every name, and the first one skipped
    // where it is printed: one line per backend, where a sentence would need a
    // case for every combination.
    const char* const kRenderBackend = ""
#ifdef RW_D3D9
        ", D3D9"
#endif
#ifdef RW_D3D11
        ", D3D11"
#endif
#ifdef RW_GL3
        ", OpenGL"
#endif
#ifdef RW_VULKAN
        ", Vulkan"
#endif
        ;

    struct StartupBanner
    {
        StartupBanner()
        {
#ifdef __ANDROID__
            // FIRST. Everything below this line prints, and on Android
            // nothing printed before it survives.
            iAndroidOpenLog();
#endif
            // Before anything prints: stdout and stderr through the log's
            // tee (iLog.h), and crash.log's place settled for the handlers.
            iLogBegin();
            atexit(DrainAtExit);
            InstallDiagnostics();
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
            // Names what is actually linked, renderer included. A banner
            // claiming a gap that has since been filled -- or a backend that is
            // not the one running -- is worse than no banner: it is the first
            // thing anyone reads when something does not work.
            printf("tssm: PC port, renderers: %s. Movie decoder: %s, movie audio: %s.\n",
                   kRenderBackend[0] ? kRenderBackend + 2 : "none", iFMVDecoderName(),
                   iFMVAudioName());
            // TSSM_TEST_CRASH: at startup, or at a frame when it is a number
            // above 1 (iTestEvents.cpp).
            const char* testCrash = getenv("TSSM_TEST_CRASH");
            if (testCrash != NULL && atoi(testCrash) <= 1)
            {
                *(volatile int*)0 = 1;
            }
        }
    };

    StartupBanner sBanner;
}
