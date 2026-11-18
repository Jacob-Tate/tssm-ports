#ifndef I3DSUPDATE_H
#define I3DSUPDATE_H

// The 3DS's side of in-app updates (iUpdate.h; docs/3DS.md "Updates"), in two
// halves that i3DS.h's rule keeps apart:
//   i3DSUpdate.cpp     game side (int enums): the iUpdatePlatform object, the
//                      checks of this copy and of the download.
//   i3DSUpdateSys.cpp  libctru side (short enums, as libctru and devkitPro's
//                      mbedtls are built): the TLS connection, the CIA install
//                      through AM, the running title's own files.
// So this header has only the C library's types and no enum. Results are
// ints with the numbers of iUpdate.h's iUpdateNet (i3DSUpdate.cpp checks
// that they agree).

#include <stddef.h>
#include <stdint.h>

static const int k3DSNetOk = 0;
static const int k3DSNetOffline = 1;
static const int k3DSNetDns = 2;
static const int k3DSNetConnect = 3;
static const int k3DSNetTls = 4;
static const int k3DSNetCert = 5;
static const int k3DSNetClock = 6;
static const int k3DSNetTimeout = 7;
static const int k3DSNetLost = 8;
static const int k3DSNetCancelled = 10;
static const int k3DSNetError = 12;

// The Mozilla CA list the connection trusts, packed into the RomFS by
// CMakeLists.txt (TSSM_UPDATE).
static const char k3DSUpdateCaPath[] = "romfs:/update/cacert.pem";
// What the build packed this copy with (CMakeLists.txt): "smdh <sha256>",
// "icon <sha256>" and "banner <sha256>" lines.
static const char k3DSUpdateArtPath[] = "romfs:/update/art.txt";

// Asked at least every quarter of a second while a call waits: nonzero stops
// it with k3DSNetCancelled.
typedef int (*i3DSNetCancelFn)(void* ctx);

// One TLS connection to host:443 over the SOC service, with mbedtls: the
// chain to one of the CA list's roots, the host name and the certificates'
// dates are always checked. Every wait is a 5 ms nap between non-blocking
// tries (one blocking call would hold the SOC session every thread shares),
// so a cancel, a stall longer than `timeoutMs` or a sleep of the console
// ends it soon. The name lookup is the one call that waits in the service.
// `detail` gets codes for the log.
struct i3DSTls;
int i3DSTlsOpen(const char* host, int timeoutMs, i3DSNetCancelFn cancelled, void* ctx,
                struct i3DSTls** out, char* detail, size_t detailSize);
int i3DSTlsWrite(struct i3DSTls* t, const void* data, size_t n);
// *got 0 with k3DSNetOk: the server closed the connection.
int i3DSTlsRead(struct i3DSTls* t, void* data, size_t n, size_t* got);
void i3DSTlsClose(struct i3DSTls* t);

// This copy: nonzero when it runs as the installed title (the .cia), with
// its ID. A .3dsx runs under the Homebrew Launcher's host title.
int i3DSUpdateInstalled(uint64_t* titleId);

// The .3dsx this copy was started from (argv[0]), when the Homebrew Launcher
// named one on the SD card: nonzero and the path in `out`.
int i3DSUpdateSelfPath(char* out, size_t outSize);

// One of the running title's own ExeFS files ("icon", "banner"), read through
// the SelfNCCH archive into a malloc'd buffer the caller frees. Nonzero when
// it was read.
int i3DSUpdateReadOwnExefs(const char* name, uint8_t** data, size_t* size);

// Installs the CIA at `path` (already checked against SHA256SUMS) through AM:
// AM must read the same title ID as `titleId` and the title version `version`
// from it, and the SD card must have room. HOME and sleep wait while AM
// writes and commits. Nonzero when installed, with the title's version as AM
// now reports it; otherwise plain words in `why` and the installed title as
// it was.
int i3DSUpdateInstallCia(const char* path, uint64_t titleId, uint16_t version,
                         uint16_t* versionOut, char* why, size_t whySize);

// The RomFS keeps the running .3dsx open (libctru's romfsMountSelf); it is
// released before the file is replaced, and mounted again after (from the new
// file when it went in). Nothing but the updater reads it after start-up.
void i3DSUpdateReleaseRomfs(void);
void i3DSUpdateRemountRomfs(void);

// Runs fn(arg) on a thread of its own with a `stackSize` stack, at the calling
// thread's priority and on its core, and waits for it to end. The core's
// worker is a std::thread, which libctru starts with 32 KB; each request (the
// TLS handshake and the HTTP client) runs here instead. The stack is filled
// with a pattern first, and the log then says how deep fn went and how the
// heap stands ("tssm: update: 3ds: <what>: stack ..."). Zero when the thread
// could not be made: fn did not run.
int i3DSUpdateRunOnStack(void (*fn)(void*), void* arg, size_t stackSize, const char* what);

// After an installed title updated itself: libctru's chainloader starts the
// title again when the game quits (APT's application jump, through the HOME
// Menu). Nonzero when set.
int i3DSUpdateRestartAfterQuit(void);

// From i3DS.cpp's quit, first: the updater's worker thread stopped (a check or
// a download cancelled, an install let finish) before the sockets and the
// file system go. In i3DSUpdate.cpp.
void i3DSUpdateStop(void);

#endif
