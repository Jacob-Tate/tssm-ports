#ifndef ILOG_H
#define ILOG_H

#include <types.h>

// PC-only: the game keeps its own log.
//
// Everything the port prints goes to stdout (and a little to stderr), and the
// crash handler's backtrace with it. A player starts the game from a shortcut
// or a .bat, and the console that output went to closes with the game -- so a
// crash took its own report with it. Now the process copies its output to a
// file as it runs:
//
//   %APPDATA%\tssm\last_run.log       this run (~/.local/share/tssm on Linux)
//   %APPDATA%\tssm\last_run.prev.log  the run before it
//   %APPDATA%\tssm\crash.log          every crash report, appended (tssm_main.cpp)
//
// The folder is the one the save folder is in: a run given its own saves with
// TSSM_SAVE_DIR (a test run, the regression harness) keeps its log beside that
// folder instead, so it never turns the player's log over. TSSM_LOG names the
// log file outright, or is "off" for none (crash.log is still written).
//
// The copy is a tee: stdout and stderr still go where they went (the console,
// or a file a caller redirected them to), each through a pipe that a thread
// empties into both. Output before iLogStart is held in memory, so the log
// starts with the banner; a command-line run (tssm --saves ...) never calls
// iLogStart and writes no log. Only one process at a time writes a given log:
// a second copy of the game started while one runs leaves it alone.

// First thing in the process (tssm_main.cpp's static constructor): starts the
// tee and works out where crash.log goes. Nothing else may have printed yet.
void iLogBegin();

// This run is the game: the log file is opened (the last one kept as
// .prev.log) and gets what was held and everything after. iSystemInit calls
// it once the command-line tools have had their turn.
void iLogStart();

// Waits, up to `ms` milliseconds, until everything printed so far has reached
// the log and the original stdout. For the crash handler, whose process ends
// right after it prints.
void iLogDrain(U32 ms);

// Where the crash handler appends its report: crash.log in the log's folder.
// Set by iLogBegin; an empty string when there is nowhere.
const char* iLogCrashPath();

// The log file this run writes, or an empty string.
const char* iLogPath();

// A host whose log is the platform's own: Android, where stdout and stderr go
// to logcat and to tssm-log.txt in the app's external files folder
// (android/iAndroid.cpp), and iLogBegin starts no tee. Names that file for
// iLogPath and puts crash.log beside it. Until this is called Android has no
// crash.log: the app's folders are not known before SDL's JNI is up.
void iLogUsePlatformLog(const char* path);

#endif
