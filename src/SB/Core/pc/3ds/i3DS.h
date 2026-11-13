#ifndef I3DS_H
#define I3DS_H

// The Nintendo 3DS homebrew build's own start-up, model check, lifecycle and
// crash code (i3DS.cpp). Compiled only by CMakeLists.txt's TSSM_3DS branch;
// the iHost seam's 3DS spelling is iHost3DS.cpp, which uses these.
// docs/3DS.md is the player's and the tester's side of it.

#include <stddef.h>
#include <stdint.h>

// The one folder on the SD card the port reads and writes (iHost3DS.cpp lists
// what is in it). Prefixed: libctru's SD device is "sdmc:", and it is also the
// default device, so iFile's "/"-rooted paths would work too; the prefix keeps
// every path the log prints unambiguous.
static const char k3DSRoot[] = "sdmc:/3ds/tssm";

// What iHostUserDataDir answers: the callers append "/tssm", which makes
// k3DSRoot.
static const char k3DSDataParent[] = "sdmc:/3ds";

// Two kinds of translation unit, never mixed: the game's headers (types.h)
// and libctru's (3ds.h).
//   libctru side: i3DS.cpp. Compiled with libctru's short enums
//                 (CMakeLists.txt, TSSM_3DS); it includes nothing of the game's.
//   game side:    iHost3DS.cpp (and iLog.cpp, tssm_main.cpp, iWindow3DS.cpp).
//                 Compiled with int-sized enums.
// So this header uses only the C library's types, and no enum: an enum in
// here would have a different size on each side.

// From the executable's start-up banner (tssm_main.cpp), before the log tee is
// installed: RomFS, 804 MHz and the L2 cache, the network log (3dslink, and
// the pull-log server on TCP 28772), and the model check, which ends the
// process on an Old 3DS after a screen saying why.
void i3DSEarly();

// From the start-up banner, after the log is up: reports the console's state
// and the memory split, registers the APT hook (HOME, sleep), installs the
// exception handler (every thread's, until the quit's last step), makes
// k3DSRoot the working directory and points TSSM_ASSETS at k3DSRoot/files
// when that exists.
void i3DSStartup();

// The log tee's copy of stdout and stderr for the PC, when the game was sent
// with `3dslink -s` (iLog.cpp); nothing otherwise. Never waits more than a
// moment: what the network cannot take at once is dropped (the log file has
// it all).
void i3DSLinkWrite(const char* data, size_t len);

// The console's address on the local network, "" while it has none. Asked at
// start-up, then again at most every 10 s (one request that never waits); an
// unusable answer keeps the last good one.
const char* i3DSAddress();

// The system tick in nanoseconds (unaffected by the wall clock, and by the
// New 3DS's clock speed), and a sleep, for iHostMonotonicNs /
// iHostSleepUntilNs.
uint64_t i3DSMonotonicNs();
void i3DSSleepNs(uint64_t ns);

// iHostErrorBox: the message on the console's bottom screen (libctru's text
// console) until START is pressed.
void i3DSErrorBox(const char* title, const char* message);

// The game files are not where the game looks: a screen on both screens
// saying what is missing, the SD card folder they go in and what goes in it,
// until START. `folder` is the folder looked in; `file` the first file not
// found or not readable there, NULL when the folder itself is missing;
// `fromConfig` TRUE when config.ini's [assets] path chose the folder.
void i3DSGameFilesScreen(const char* folder, const char* file, int unreadable, int fromConfig);

// Where closing the game goes back to: "HOME Menu" for the installed title
// (the .cia), "Homebrew Launcher" for the .3dsx.
const char* i3DSMenuName();

// A line in crash.log (iLog.h) with the time and `what`, from the abort
// handler.
void i3DSCrashLog(const char* what);

// The one way out. The link wraps exit() (CMakeLists.txt), so every exit()
// comes here, return from main included. On the main thread it stops, in
// order, the log server, the APT hook, SDL's audio (its thread on the system
// core, ndsp's thread), citro3d (the GPU waited for first), the rest of SDL
// (gfxExit, the GSP event thread), the sockets and the RomFS, then runs the C
// library's exit. libctru frees the heaps only after that, with no thread of
// ours left to touch them. Each step is a "tssm: 3ds: quit:" line in the log,
// written and synced before the step runs.
void i3DSQuit(int code) __attribute__((noreturn));
// TRUE once the quit has begun.
int i3DSQuitting();
// One "tssm: 3ds: quit: ..." line: to the log file, synced, and to 3dslink.
void i3DSQuitLine(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// The log file past stdio (iLog.cpp), for the quit: `n` bytes written and
// synced; closed, last thing before libctru's own exit.
void i3DSLogWrite(const char* text, size_t n);
void i3DSLogClose();

// While a save is written: TRUE keeps HOME and sleep from taking the game
// away mid-write (aptSetHomeAllowed / aptSetSleepAllowed), FALSE allows them
// again. Nests.
void i3DSHoldHome(int hold);

// The game side's half of the APT hook (iHost3DS.cpp): the clock stops while
// the game is not in front (iTimeSuspend), and coming back starts it again and
// restarts the audio devices (iTimeResume, iAudioSDLRestart).
void i3DSGameAway();
void i3DSGameBack();

// How many times the game has come back from the HOME menu or sleep, for
// what is drawn only when it changes (the overlay's bottom screen).
int i3DSResumeCount();

// The log's paths and drain (iLog.h), for the libctru side's crash handler.
const char* i3DSCrashLogPath();
const char* i3DSLogPath();
void i3DSLogDrain(unsigned ms);

// ---------------------------------------------------------------- the spike
// The libctru half of the speed test (iSpike.h; i3DSSpike.cpp). Text comes
// back in the caller's buffer, a line per finding, ending in a newline.

// Model, clock (a timed loop at 268 and at 804 MHz), memory.
void i3DSSpikeConsole(char* out, size_t size);
// A thread on core 2; one on core 1 after APT_SetAppCpuTimeLimit(30); the same
// timed loop on each, against the main thread's.
void i3DSSpikeThreads(char* out, size_t size);
// Memory now, in bytes: the application region's free part, the linear
// heap's free part, malloc's bytes in use and what it has taken from the heap.
void i3DSSpikeMemory(uint32_t* appFree, uint32_t* linearFree, uint32_t* mallocUsed,
                     uint32_t* mallocArena);
// TRUE when sdmc:/3ds/dspfirm.cdc exists (without it there is no sound).
int i3DSSpikeHasDsp();
// The screens. The top one shows `count` lines of at most 25 characters in
// double-size type (15 fit); the bottom one is a 40-column log.
void i3DSSpikeTop(const char* const* lines, int count);
void i3DSSpikeBottom(const char* text);
// After SDL's video started (iWindow3DS.cpp), which sets both screens up
// afresh: what the spike showed on them is put back. Nothing when the spike
// has shown nothing.
void i3DSSpikeScreensReset();
// A probe build's name and what to do if it freezes, on the bottom screen
// (once SDL's video has set the screens up); nothing in the game's build.
void i3DSProbeBanner();
// Probe 11 (the leak ladder), its layers L3 and L4: called once the port's
// start-up is done (iSystemInitGraphics), before the game; quits there after
// 2 s of events. Nothing in any other build.
void i3DSLadderCheckpoint();
// Nonzero once that banner is on the bottom screen: the closed overlay leaves it.
int i3DSProbeBannerShown();
// After the last frame: the top screen stays until START is pressed or the
// system closes the program; HOME and sleep come and go meanwhile.
void i3DSSpikeWaitStart();
// The top screen as it is now, as a 24-bit BMP; FALSE when it could not be
// written.
int i3DSSpikeSaveTop(const char* path);
// A moment for the other threads on core 0 (the log server), between frames.
void i3DSSpikeYield();
// What TCP 28772 sends: this file instead of last_run.log.
void i3DSServeFile(const char* path);

#endif
