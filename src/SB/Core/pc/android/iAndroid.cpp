// iAndroid.cpp: treedome's pc/android/iAndroid.cpp (CC0), renamed for TSSM (BFBB_ -> TSSM_,
// tssm-log.txt, the logcat tag "tssm"). src/SB/Core/pc/PORTING.md lists it.

#include "iAndroid.h"

#include <android/log.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <SDL3/SDL.h>

#include "iHost.h"
#include "iLog.h"

// See iAndroid.h for what this file is for and why it is in two halves.

namespace
{
    const char* const kTag = "tssm";

    // ---------------------------------------------------------------------
    // stdout and stderr to logcat

    int sLogPipe[2] = { -1, -1 };

    // Set while the pump holds bytes it has read and not yet written, for
    // iAndroidLogDrain.
    volatile int sPumpBusy;

    // ---------------------------------------------------------------------
    // The same log, in a file the player can open

    // logcat is the right answer for a developer and the wrong one for
    // everybody else: reading it needs adb, which needs a computer, or a
    // device paired with itself over wireless debugging. A port whose entire
    // diagnosis is "which line came last" cannot have its log locked behind
    // that -- the first thing this build did on a real phone was close
    // instantly, and the only way to find out why was a cable.
    //
    // So every line also goes to tssm-log.txt in the application's external
    // files directory, which is the directory the assets are imported into
    // and is reachable from any file manager on the device.
    //
    // Truncated at each launch. It is a record of THIS run; the previous
    // one's is what the last launch wrote, and keeping both means explaining
    // which is which to someone reading their first one.
    pthread_mutex_t sLogLock = PTHREAD_MUTEX_INITIALIZER;
    FILE* sLogFile;

    // What was printed before the file could be opened.
    //
    // The pipe is up during dlopen and the external directory is not knowable
    // until JNI is, which is several hundred lines of startup later -- and
    // those are the lines that matter most, because a failure this early is
    // the one with nothing else to go on. So they are held here until
    // iAndroidStartup says where to put them.
    char* sBacklog;
    size_t sBacklogUsed;

    // Bounds. The backlog covers startup and not a session, and the file is
    // capped so that a build left running with a debug switch on cannot fill
    // the device.
    const size_t kBacklogMax = 64 * 1024;
    const size_t kLogFileMax = 8 * 1024 * 1024;

    size_t sLogFileWritten;
    bool sLogFileFull;

    // Caller holds sLogLock.
    void iAndroidLogToFile(const char* line)
    {
        if (sLogFile == NULL)
        {
            if (sBacklogUsed >= kBacklogMax)
            {
                return;
            }

            if (sBacklog == NULL)
            {
                sBacklog = (char*)malloc(kBacklogMax);
                if (sBacklog == NULL)
                {
                    sBacklogUsed = kBacklogMax;
                    return;
                }
            }

            size_t n = strlen(line);
            if (sBacklogUsed + n + 1 > kBacklogMax)
            {
                n = kBacklogMax - sBacklogUsed - 1;
            }

            memcpy(sBacklog + sBacklogUsed, line, n);
            sBacklogUsed += n;
            sBacklog[sBacklogUsed++] = '\n';
            return;
        }

        if (sLogFileFull)
        {
            return;
        }

        sLogFileWritten += fprintf(sLogFile, "%s\n", line);

        // Flushed per line, for the reason tssm_main.cpp unbuffers stdout: a
        // crash discards whatever is still in the buffer, and the last line
        // before a crash is the whole point of the file.
        fflush(sLogFile);

        if (sLogFileWritten >= kLogFileMax)
        {
            fputs("[log truncated: 8 MB]\n", sLogFile);
            fflush(sLogFile);
            sLogFileFull = true;
        }
    }

    // One log line per printf line, rather than per write.
    //
    // tssm_main.cpp sets both streams unbuffered so that a crash cannot
    // discard the tail, which means a single printf can arrive here as
    // several reads and several printfs can arrive as one. logcat has no
    // notion of a partial line -- each call is a record with its own
    // timestamp -- so the pieces are reassembled on '\n' before being handed
    // over, or the log comes out as confetti.
    void* iAndroidLogPump(void*)
    {
        char line[1024];
        size_t used = 0;

        for (;;)
        {
            char chunk[256];
            ssize_t got = read(sLogPipe[0], chunk, sizeof(chunk));

            if (got < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                break;
            }

            if (got == 0)
            {
                break;
            }

            __atomic_store_n(&sPumpBusy, 1, __ATOMIC_SEQ_CST);
            for (ssize_t i = 0; i < got; i++)
            {
                const char c = chunk[i];

                // A line longer than the buffer is split rather than dropped.
                // The port prints a few very long lines -- the asset search
                // path, a demangled template -- and half of one is worth more
                // than none of it.
                if (c == '\n' || used == sizeof(line) - 1)
                {
                    line[used] = '\0';
                    __android_log_write(ANDROID_LOG_INFO, kTag, line);

                    pthread_mutex_lock(&sLogLock);
                    iAndroidLogToFile(line);
                    pthread_mutex_unlock(&sLogLock);

                    used = 0;

                    if (c == '\n')
                    {
                        continue;
                    }
                }

                // Carriage returns come from the progress lines, which print
                // '\r' to redraw in place. There is nothing to redraw in a
                // log, and a stray CR inside a record is rendered as a box.
                if (c != '\r')
                {
                    line[used++] = c;
                }
            }
            __atomic_store_n(&sPumpBusy, 0, __ATOMIC_SEQ_CST);
        }

        // The pipe only closes when the process is going down.
        if (used != 0)
        {
            line[used] = '\0';
            __android_log_write(ANDROID_LOG_INFO, kTag, line);

            pthread_mutex_lock(&sLogLock);
            iAndroidLogToFile(line);
            pthread_mutex_unlock(&sLogLock);
        }

        return NULL;
    }

    // ---------------------------------------------------------------------
    // Directories

    // SDL hands back a pointer it owns, and NULL when the framework has not
    // made that directory available -- external storage on a device with the
    // volume unmounted, most often. NULL is a real answer and not a failure:
    // the host seam reports "the host cannot say" for whatever depends on it
    // and every caller of those has a fallback.
    void iAndroidSetDir(const char* name, const char* path)
    {
        if (path == NULL || path[0] == '\0')
        {
            return;
        }

        iHostSetEnv(name, path);
    }
}

void iAndroidOpenLog()
{
    if (sLogPipe[0] != -1)
    {
        return;
    }

    if (pipe(sLogPipe) != 0)
    {
        __android_log_write(ANDROID_LOG_ERROR, kTag,
                            "could not open a pipe for stdout; the log is lost");
        sLogPipe[0] = -1;
        return;
    }

    // dup2 onto the two standard descriptors rather than replacing the FILE*
    // streams: everything that prints does so through fd 1 or fd 2 in the
    // end, including SDL's own logging and anything librw prints, and a
    // freopen would only catch the C library's half of that.
    dup2(sLogPipe[1], STDOUT_FILENO);
    dup2(sLogPipe[1], STDERR_FILENO);

    pthread_t pump;
    if (pthread_create(&pump, NULL, iAndroidLogPump, NULL) != 0)
    {
        __android_log_write(ANDROID_LOG_ERROR, kTag,
                            "could not start the log pump; the log is lost");
        return;
    }

    // Never joined. It lives as long as the process and has nothing to
    // report when it ends, and a detached thread is one fewer handle for a
    // shutdown path to remember.
    pthread_detach(pump);
}

void iAndroidLogDrain(unsigned ms)
{
    if (sLogPipe[0] == -1)
    {
        return;
    }

    // An empty pipe and an idle pump, seen twice in a row a moment apart.
    int quiet = 0;
    for (unsigned waited = 0; waited < ms && quiet < 2; waited += 2)
    {
        int avail = 0;
        const bool idle = ioctl(sLogPipe[0], FIONREAD, &avail) == 0 && avail == 0 &&
                          __atomic_load_n(&sPumpBusy, __ATOMIC_SEQ_CST) == 0;
        quiet = idle ? quiet + 1 : 0;
        usleep(2000);
    }
}

// ---------------------------------------------------------------------------
// The one host where iHostErrorBox is a real dialog
//
// Defined HERE rather than in iHostPosix.cpp beside the other POSIX arms,
// because putting a message on an Android screen means asking SDL, and SDL is
// a library that sits ON the platform layer rather than under it. The host
// seam is the OS half and stays that way; the shim is the file that is
// already allowed to know about both.
//
// It earns the exception. On every other host a startup failure has already
// been printed to a console someone can read, and iHost.h says so: "returns
// having done nothing on a host with no way to show one". On a phone there is
// no console, the log file this shim writes is not somewhere anyone thinks to
// look before they have been told it exists, and the failure the player
// actually meets is the application closing the instant they open it. The
// first real device this port ran on did exactly that, and what it was trying
// to say was "the game's files were not found" -- a sentence that fixes the
// problem the moment it is read.
//
// SDL_ShowSimpleMessageBox blocks until the dialog is dismissed, which is what
// iHost.h promises and what the caller wants: every one of these is followed
// by exit().
void iHostErrorBox(const char* title, const char* message)
{
    const char* t = (title != NULL) ? title : "Error";
    const char* m = (message != NULL) ? message : "";

    // logcat as well as the dialog, and at ERROR so `adb logcat *:E` shows it
    // without being asked. The dialog is for the player; this is for whoever
    // they send the log to.
    __android_log_print(ANDROID_LOG_ERROR, kTag, "%s: %s", t, m);

    if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, t, m, NULL))
    {
        // No dialog: too early for the Activity, or the framework refused.
        // Nothing more to do -- the caller has printed the same text and the
        // header allows this to have reached nobody.
        __android_log_print(ANDROID_LOG_ERROR, kTag,
                            "could not show the message box: %s", SDL_GetError());
    }
}

namespace
{
    void (*sAtEnd)();
} // namespace

void iAndroidAtEndProcess(void (*fn)())
{
    sAtEnd = fn;
}

void iAndroidEndProcess(int code)
{
    // Cleared first: a fatal box raised inside it ends without it.
    void (*atEnd)() = sAtEnd;
    sAtEnd = NULL;
    if (atEnd != NULL)
    {
        atEnd();
    }
    printf("tssm: the game ends here (code %d)\n", code);
    fflush(stdout);
    iLogDrain(1000);
    _exit(code);
}

void iAndroidStartup()
{
    // Landscape either way up. With no hint, SDL gives a resizable window
    // SCREEN_ORIENTATION_FULL_USER when it creates it, which overrides the
    // manifest's sensorLandscape and leaves a rotation-locked phone in portrait.
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");

    // The back button to the game as a key press rather than to the activity,
    // which would finish. iPadHostSDL.cpp turns it into Start.
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");

    const char* internal = SDL_GetAndroidInternalStoragePath();
    const char* cache = SDL_GetAndroidCachePath();
    const char* external = SDL_GetAndroidExternalStoragePath();

    iAndroidSetDir("TSSM_ANDROID_INTERNAL", internal);
    iAndroidSetDir("TSSM_ANDROID_CACHE", cache);
    iAndroidSetDir("TSSM_ANDROID_EXTERNAL", external);

    // The log, somewhere a file manager can reach. Everything printed since
    // dlopen has been held in memory waiting for this.
    //
    // External rather than internal: internal storage is private to the
    // package and a player cannot open it at all without root, which would
    // defeat the point. The file sits beside the assets directory they
    // already have to navigate to.
    //
    // A test launch from adb (TestLaunch.java) names its own log, in its
    // private perf-test folder, so the player's tssm-log.txt is left alone.
    const char* testLog = getenv("TSSM_LOG");
    const bool testLaunch = testLog != NULL && testLog[0] != '\0';
    if (testLaunch || (external != NULL && external[0] != '\0'))
    {
        char path[512];
        char prev[520];
        if (testLaunch)
        {
            snprintf(path, sizeof(path), "%s", testLog);
            snprintf(prev, sizeof(prev), "%s.prev", testLog);
        }
        else
        {
            // The run before is kept as tssm-log.prev.txt, as the desktop
            // keeps last_run.prev.log: crash.log, beside it, says the
            // backtrace is in tssm-log.txt, and a player who reopens the game
            // after a crash has already started the next run.
            snprintf(path, sizeof(path), "%s/tssm-log.txt", external);
            snprintf(prev, sizeof(prev), "%s/tssm-log.prev.txt", external);
        }
        rename(path, prev);

        FILE* f = fopen(path, "w");

        pthread_mutex_lock(&sLogLock);
        if (f != NULL)
        {
            sLogFile = f;

            if (sBacklog != NULL)
            {
                fwrite(sBacklog, 1, sBacklogUsed, sLogFile);
                sLogFileWritten += sBacklogUsed;
                fflush(sLogFile);

                free(sBacklog);
                sBacklog = NULL;
                sBacklogUsed = 0;
            }
        }
        pthread_mutex_unlock(&sLogLock);

        if (f == NULL)
        {
            __android_log_print(ANDROID_LOG_ERROR, kTag,
                                "could not open %s: %s", path, strerror(errno));
        }
        else
        {
            // iLogPath names this file, and the crash handler appends its
            // reports to crash.log beside it (iLog.h, tssm_main.cpp).
            iLogUsePlatformLog(path);
        }
    }

    // Where the assets are, unless something already said.
    //
    // getExternalFilesDir is the only place on a modern Android that both the
    // player and the game can reach: it needs no permission from this side
    // and it is visible over USB and to a file manager from the other, which
    // is how a gigabyte of retail data gets onto the device at all.
    // fopen on a path the player picked somewhere else has not worked since
    // API 30.
    //
    // Only when the directory is actually there, and only when TSSM_ASSETS is
    // not already set. Absent both, `[assets] path` in config.ini decides, as
    // it does on every other host -- so a build that keeps its data somewhere
    // unusual is still configurable, and the documented precedence
    // (TSSM_ASSETS beats the settings file) is not quietly inverted here.
    if (external != NULL && external[0] != '\0')
    {
        const char* already = getenv("TSSM_ASSETS");

        if (already == NULL || already[0] == '\0')
        {
            char assets[512];
            snprintf(assets, sizeof(assets), "%s/assets", external);

            if (iHostPathExists(assets))
            {
                iHostSetEnv("TSSM_ASSETS", assets);
            }
        }
    }

    // Printed rather than merely set, because "the game says it has no
    // assets" is the first thing that will go wrong on a device and the
    // answer is always one of these three lines.
    printf("tssm: android -- internal %s\n", internal != NULL ? internal : "(none)");
    printf("tssm: android -- external %s\n", external != NULL ? external : "(none)");

    const char* assets = getenv("TSSM_ASSETS");
    printf("tssm: android -- assets %s\n",
           assets != NULL && assets[0] != '\0' ? assets : "(from config.ini)");

    // Printed rather than assumed, and printed INTO the file it names, so
    // that the file says what it is to whoever opens it first.
    if (testLaunch)
    {
        printf("tssm: android -- a test launch; this log is also %s\n", testLog);
    }
    else
    {
        printf("tssm: android -- this log is also %s/tssm-log.txt\n",
               external != NULL ? external : "(nowhere: no external storage)");
    }
    if (iLogCrashPath()[0] != '\0')
    {
        printf("tssm: android -- crash reports are also added to %s\n", iLogCrashPath());
    }
    fflush(stdout);
}
