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
// installed: RomFS, the nxlink connection, and the applet-mode check, which
// can end the process with a message on the console's screen.
void iSwitchEarly();

// From the start-up banner, after the log is up: reports the console's state,
// registers the applet hook (HOME, sleep, dock), makes kSwitchRoot the working
// directory and points TSSM_ASSETS at kSwitchRoot/assets when that exists.
void iSwitchStartup();

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

#endif
