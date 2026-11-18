#ifndef ISWITCH_H
#define ISWITCH_H

// The Nintendo Switch homebrew build's own start-up, applet and crash code
// (iSwitch.cpp). Compiled only by CMakeLists.txt's TSSM_SWITCH branch; the
// iHost seam's Switch spelling is iHostSwitch.cpp, which uses these.
// docs/SWITCH.md is the player's and the tester's side of it.

#include <stddef.h>
#include <stdint.h>

// The one folder on the SD card the port reads and writes (iHostSwitch.cpp
// lists what is in it). Unprefixed: libnx resolves "/..." against the SD card.
static const char kSwitchRoot[] = "/switch/tssm";

// What iHostUserDataDir answers: the callers append "/tssm", which makes
// kSwitchRoot.
static const char kSwitchDataParent[] = "/switch";

// Two kinds of translation unit, never mixed: the game's headers (types.h)
// and libnx's (switch.h) both typedef u32 and u64, differently. This header
// uses only the C library's types, so either side can include it.
//   libnx side:  iSwitch.cpp, iSwitchLowMem.cpp
//   game side:   iHostSwitch.cpp (and iLog.cpp, tssm_main.cpp)

// From the executable's start-up banner (tssm_main.cpp), before the log tee is
// installed: the applet-mode check, which can go back to the Homebrew Menu
// after a message on the console's screen, then RomFS and the nxlink
// connection.
void iSwitchEarly();

// From the start-up banner, after the log is up: reports the console's state,
// registers the applet hook (HOME, sleep, dock), makes kSwitchRoot the working
// directory and points TSSM_ASSETS at kSwitchRoot/assets when that exists.
void iSwitchStartup();

// Unmounts the RomFS, which holds the running .nro open, so the updater can
// replace that file (iSwitchUpdate.cpp). Nothing reads the RomFS after start-up.
void iSwitchReleaseRomfs();

// The nxlink host's socket when the game was sent with `nxlink -s`, else -1.
// The log tee writes stdout and stderr to it (iLog.cpp).
int iSwitchNxlinkSocket();

// The .nro image's load address, so that diagnostics can print addresses as
// offsets that addr2line reads against tssm.elf.
uintptr_t iSwitchImageBase();

// The system counter in nanoseconds (unaffected by the wall clock), and a
// sleep, for iHostMonotonicNs / iHostSleepUntilNs.
uint64_t iSwitchMonotonicNs();
void iSwitchSleepNs(uint64_t ns);

// iHostReserveLow / iHostRelease: `size` bytes of zeroed read/write memory,
// below 4 GB when it can be done (iSwitchLowMem.cpp says how); otherwise from
// the heap, wherever that is, for the caller to refuse. NULL when there is not
// even that.
void* iSwitchReserveLow(uint64_t size);
void iSwitchReleaseLow(void* p);

// Every mapping iSwitchReserveLow made, undone, at the very end of the exit
// (userAppExit): hbloader gives the same heap pages to the Homebrew Menu.
// False when one could not be unmapped.
bool iSwitchReleaseAllLow();

// The one way out (iSwitch.cpp). The link wraps exit() (CMakeLists.txt), so
// every exit() comes here: the window's close, the overlay's and the
// launcher's Quit, a fatal error. It stops the log server, the audio thread,
// the GL context and SDL, then runs exit() as usual; userAppExit then releases
// the low memory and returns to the Homebrew Menu. Each step is a line in the
// log, written before the step runs.
void iSwitchQuit(int code) __attribute__((noreturn));

// One "tssm: switch: quit: ..." line, written to the log file and synced
// without stdio (exit() has closed it by the time userAppExit runs).
void iSwitchQuitLine(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// iHostClaimInstance: `path` (on the SD card) opened for writing and held
// until the exit. 1: held. 0: another program holds it open, which Horizon is
// expected to refuse a second open for. -1: it cannot be held; the caller
// goes on as if claimed.
int iSwitchClaimFile(const char* path);

// iHostErrorBox: the system's error applet, where the program is an
// application (title takeover); nothing in applet mode.
void iSwitchErrorBox(const char* title, const char* message);

// A line in crash.log (iLog.h) with the time and `what`, from the abort
// handler and the exception handler.
void iSwitchCrashLog(const char* what);

// The game side's half of the applet hook (iHostSwitch.cpp): the clock
// stops while the game is not in front (iTimeSuspend), and coming back starts
// it again and restarts the audio devices (iTimeResume, iAudioSDLRestart).
void iSwitchGameAway();
void iSwitchGameBack();

// The log's paths and drain (iLog.h), for the libnx side's crash handler.
const char* iSwitchCrashLogPath();
const char* iSwitchLogPath();
void iSwitchLogDrain(unsigned ms);

// The log file itself, past stdio (iLog.cpp): `n` bytes written and synced.
// iSwitchLogClose closes it, last thing before the return to hbloader, so the
// next start of the game can rename and reopen it.
void iSwitchLogWrite(const char* text, size_t n);
void iSwitchLogClose();

#endif
