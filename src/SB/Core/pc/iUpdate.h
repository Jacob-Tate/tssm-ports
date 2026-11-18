#ifndef IUPDATE_H
#define IUPDATE_H

// In-app updates from the project's releases on GitHub: the settings window's
// Updates section (gui/iGuiUpdates.cpp) asks, this core does the work, and a
// small platform object (iUpdatePlatform below) supplies the HTTPS transport
// and the install. Each platform's doc (docs/SWITCH.md, docs/3DS.md,
// docs/ANDROID.md), "Updates", is the player's side.
//
// This file and iUpdate.cpp are the same on every branch that has updates.
// They use only the C and C++ libraries' types, so a platform file built
// against the system's own headers (libnx, libctru, JNI) includes this header
// as well as the game side does.
//
// What the core enforces, whatever the platform:
//   - HTTPS to four hosts only (iUpdateHostAllowed), at most kUpdateMaxRedirects
//     redirects, each one checked again;
//   - the request carries Host, User-Agent: kUpdateUserAgent, Accept and
//     Connection: close, and nothing else but Accept-Encoding: identity where
//     the system's HTTP client would otherwise ask for gzip (Android's);
//   - the file is this platform's (its name is kUpdateAssetPrefix, a version,
//     the platform's suffix), at most kUpdateMaxBytes, and its SHA-256 matches
//     its line in the same release's SHA256SUMS (and GitHub's own digest of it,
//     when the release lists one) before the platform's Install sees it;
//   - the download goes to a temporary file that is removed on any failure;
//     saves and settings are never touched;
//   - nothing runs unless asked: a check or a download starts from the
//     settings, or once at start-up when the player turned that on.

#include <stddef.h>
#include <stdint.h>

// --- limits and fixed values -------------------------------------------------------

static const char kUpdateUserAgent[] = "TSSM-port-updater";
static const char kUpdateAssetPrefix[] = "TSSM-Dehydrated-";
static const char kUpdateSumsName[] = "SHA256SUMS";
static const uint64_t kUpdateMaxBytes = 64ull * 1024 * 1024;
static const int kUpdateMaxRedirects = 5;
// Connecting, and the longest wait for the next bytes of a response.
static const int kUpdateTimeoutMs = 20000;

// --- versions ----------------------------------------------------------------------

// Up to four dot-separated numbers: 0.9, 0.9.1, 0.10.
struct iUpdateVersion
{
    uint32_t part[4];
    int count;
};

// Digits and single dots only, 1 to 4 numbers of at most 9 digits each.
bool iUpdateParseVersion(const char* text, size_t len, iUpdateVersion* out);

// <0, 0, >0. Missing numbers count as 0, so 0.9 equals 0.9.0.
int iUpdateCompareVersions(const iUpdateVersion& a, const iUpdateVersion& b);

void iUpdateFormatVersion(const iUpdateVersion& v, char* out, size_t outSize);

// --- the release (GET /repos/<owner>/<name>/releases/latest) ----------------------

enum
{
    IUPDATE_MAX_ASSETS = 64,
    IUPDATE_NAME_MAX = 128,
    IUPDATE_URL_MAX = 512,
    // A redirect's Location: GitHub's signed asset links run to about 1 KB.
    IUPDATE_LOCATION_MAX = 4096,
    // The whole JSON and SHA256SUMS, as read into memory.
    IUPDATE_JSON_MAX = 1024 * 1024,
    IUPDATE_SUMS_MAX = 64 * 1024
};

struct iUpdateAsset
{
    char name[IUPDATE_NAME_MAX];
    char url[IUPDATE_URL_MAX]; // browser_download_url; empty when too long
    uint64_t size;
    bool hasDigest; // GitHub's "digest": "sha256:<hex>", when the release has one
    uint8_t digest[32];
};

struct iUpdateRelease
{
    char tag[64];
    bool draft;
    bool prerelease;
    int assetCount;
    iUpdateAsset assets[IUPDATE_MAX_ASSETS];
};

// Only the fields above are read; everything else is skipped, whatever it is.
// Bounded: nesting at most 32 deep, assets past IUPDATE_MAX_ASSETS ignored,
// an asset whose state is not "uploaded" left out. False, with the reason in
// `why`, for anything that is not one well-formed JSON object.
bool iUpdateParseRelease(const char* json, size_t len, iUpdateRelease* out, char* why,
                         size_t whySize);

// This platform's file: the asset named prefix + version + suffix, the newest
// when there are several. Its index, or -1.
int iUpdateFindAsset(const iUpdateRelease& r, const char* prefix, const char* suffix,
                     iUpdateVersion* version);

// The asset with exactly this name, or -1.
int iUpdateFindAssetNamed(const iUpdateRelease& r, const char* name);

// --- SHA256SUMS ------------------------------------------------------------------------

enum iUpdateSumResult
{
    IUPDATE_SUM_FOUND,
    IUPDATE_SUM_MISSING,    // no line for the name
    IUPDATE_SUM_CONFLICTING // two lines for the name with different sums
};

// sha256sum's format: 64 hex digits, a space, a space or '*', the name.
iUpdateSumResult iUpdateFindSum(const char* sums, size_t len, const char* name, uint8_t out[32]);

// --- SHA-256 (FFmpeg's libavutil, linked into every build that has movies) ----------

class iUpdateSha256
{
public:
    iUpdateSha256();
    ~iUpdateSha256();
    bool Ok() const { return mCtx != NULL; }
    void Update(const void* data, size_t n);
    void Final(uint8_t out[32]);

    iUpdateSha256(const iUpdateSha256&) = delete;
    iUpdateSha256& operator=(const iUpdateSha256&) = delete;

private:
    void* mCtx;
};

void iUpdateHex(const uint8_t* bytes, size_t n, char* out, size_t outSize);

// --- URLs -------------------------------------------------------------------------------

// api.github.com, github.com, objects.githubusercontent.com,
// release-assets.githubusercontent.com. Lower case only.
bool iUpdateHostAllowed(const char* host);

// "https://<host>[:443]<path>" into its parts. False for anything else: another
// scheme or port, user information, a host not allowed, control characters.
bool iUpdateSplitUrl(const char* url, char* host, size_t hostSize, char* path, size_t pathSize);

// --- the transport ------------------------------------------------------------------------

enum iUpdateNet
{
    IUPDATE_NET_OK,        // one whole response was read (whatever its status)
    IUPDATE_NET_OFFLINE,   // not connected to the internet
    IUPDATE_NET_DNS,       // the server's name did not resolve
    IUPDATE_NET_CONNECT,   // no connection to the server
    IUPDATE_NET_TLS,       // the secure connection failed
    IUPDATE_NET_CERT,      // the server's certificate was refused
    IUPDATE_NET_CLOCK,     // refused for its dates: this device's clock looks wrong
    IUPDATE_NET_TIMEOUT,   // nothing arrived for kUpdateTimeoutMs
    IUPDATE_NET_LOST,      // the connection broke off
    IUPDATE_NET_PROTOCOL,  // not an HTTP response this client reads
    IUPDATE_NET_CANCELLED, // the sink asked to stop (the player cancelled)
    IUPDATE_NET_STOPPED,   // the sink refused the body (too big, a write failed)
    IUPDATE_NET_ERROR      // anything else
};

const char* iUpdateNetName(iUpdateNet n);

struct iUpdateRequest
{
    const char* host;   // one of iUpdateHostAllowed's
    const char* path;   // from '/', with the query
    const char* accept; // the Accept header's value
    int timeoutMs;      // kUpdateTimeoutMs
};

// What a transport reports while a response comes in.
class iUpdateSink
{
public:
    virtual ~iUpdateSink() {}

    // The status and the headers the core reads. contentLength is -1 when the
    // response has none; location is NULL when there is none. False: the body
    // is not wanted, and the transport returns IUPDATE_NET_OK without it.
    virtual bool OnResponse(int status, int64_t contentLength, const char* location) = 0;

    // Body bytes, with any chunked framing removed. False: stop, and the
    // transport returns IUPDATE_NET_STOPPED.
    virtual bool OnBody(const void* data, size_t n) = 0;

    // Asked at least every quarter of a second while the transport waits.
    // True: stop, and the transport returns IUPDATE_NET_CANCELLED.
    virtual bool Cancelled() = 0;
};

// A TLS connection, for a platform whose network layer has no HTTP of its own
// (the Switch's ssl service): the core's HTTP/1.1 client runs over it.
class iUpdateStream
{
public:
    virtual ~iUpdateStream() {}
    // All `n` bytes, or an error.
    virtual iUpdateNet Write(const void* data, size_t n) = 0;
    // Up to `n` bytes; *got 0 with IUPDATE_NET_OK is the end of the stream.
    // Waits at most the request's timeout, asking the sink about a cancel.
    virtual iUpdateNet Read(void* data, size_t n, size_t* got) = 0;
};

// One GET over `stream` (already connected to req.host): the request, then
// the status line, the headers (16 KB at most), and the body by
// Content-Length, chunked, or to the end of the stream.
iUpdateNet iUpdateHttpGet(iUpdateStream* stream, const iUpdateRequest& req, iUpdateSink* sink,
                          char* detail, size_t detailSize);

// The request iUpdateHttpGet sends. 0 when it does not fit or a field holds a
// character that cannot go into a request line.
size_t iUpdateHttpRequestText(const iUpdateRequest& req, char* out, size_t outSize);

// --- the platform ----------------------------------------------------------------------------

class iUpdatePlatform
{
public:
    virtual ~iUpdatePlatform() {}

    // This platform's file in a release: kUpdateAssetPrefix, the version,
    // then this ("-Switch.nro").
    virtual const char* AssetSuffix() = 0;

    // One HTTPS GET of https://req.host + req.path, with certificate checks
    // (the chain, the name and the dates) always on. No redirect is followed:
    // a 3xx comes to the sink like any other status. Sends Host, User-Agent:
    // kUpdateUserAgent, Accept: req.accept, Connection: close and nothing
    // else (Accept-Encoding: identity aside, where the system's client would
    // ask for gzip). `detail` gets codes and names for the log.
    virtual iUpdateNet Get(const iUpdateRequest& req, iUpdateSink* sink, char* detail,
                           size_t detailSize) = 0;

    // Where the download is written before it is checked: on the volume the
    // install needs (the Switch: beside the .nro). False, with plain words in
    // `why`, when this copy cannot update itself.
    virtual bool DownloadPath(char* out, size_t outSize, char* why, size_t whySize) = 0;

    // Puts the checked file at `path` in place of the running program. False,
    // with plain words in `why`, leaving the installed program as it was. The
    // core removes whatever is left at `path` afterwards.
    virtual bool Install(const char* path, const iUpdateVersion& version, char* why,
                         size_t whySize) = 0;

    // False, with plain words in `why`, when this copy should not be replaced
    // by the public file (a build of someone's own, a test copy): the check
    // still reports what is new, and nothing is downloaded.
    virtual bool InstallAllowed(char* why, size_t whySize)
    {
        (void)why;
        (void)whySize;
        return true;
    }

    // After an install: whether PrepareRestart can start the new version, and
    // arranging it (the caller then quits the game the usual way).
    virtual bool CanRestart() { return false; }
    virtual bool PrepareRestart() { return false; }

    // The game is quitting (iUpdateShutdown, on the game's thread, while the
    // worker may be inside Get, DownloadPath or Install): any wait there for
    // the player or the system ends now, so the worker can be joined.
    virtual void Stop() {}
};

// The platform's object (switch/iSwitchUpdate.cpp), made once. NULL when this
// copy has no way to update itself; the Updates section then says so.
iUpdatePlatform* iUpdateCreatePlatform();

// --- the state machine ---------------------------------------------------------------------

enum iUpdateState
{
    IUPDATE_IDLE,
    IUPDATE_CHECKING,
    IUPDATE_UP_TO_DATE,
    IUPDATE_AVAILABLE,
    IUPDATE_DOWNLOADING,
    IUPDATE_VERIFYING,
    IUPDATE_INSTALLING,
    IUPDATE_DONE_RESTART,
    IUPDATE_ERROR
};

const char* iUpdateStateName(iUpdateState s);

struct iUpdateSettings
{
    const char* repo;           // "owner/name" (CMake's TSSM_UPDATE_REPO)
    const char* currentVersion; // this build's ("0.9")
};

struct iUpdateStatus
{
    iUpdateState state;
    char currentVersion[32];
    char latestVersion[32]; // the newer version found, once one was
    char assetName[IUPDATE_NAME_MAX];
    uint64_t assetSize;
    uint64_t done; // bytes downloaded, or checked while verifying
    bool installAllowed;
    char installBlocked[256]; // why not, when not
    bool canRestart;
    char message[384]; // the last result, in plain words
};

// Before anything else. False (and nothing else works) when the version or
// the repository is not one the core can use. Called again while nothing
// runs (the tests), it starts over from idle with the new platform.
bool iUpdateInit(iUpdatePlatform* platform, const iUpdateSettings& settings);
bool iUpdateReady();

// Each starts one job on a worker thread and returns at once; nothing happens
// when the state does not allow it (iUpdateCanCheck, iUpdateCanDownload).
bool iUpdateCanCheck();
void iUpdateCheck();
bool iUpdateCanDownload();
void iUpdateDownload();

// Stops a check, a download or the verifying; an install is left to finish.
// Nothing installed changes.
bool iUpdateCanCancel();
void iUpdateCancel();

void iUpdateGetStatus(iUpdateStatus* out);

// After IUPDATE_DONE_RESTART, when canRestart: true when the platform has
// arranged for the new version to start once the game quits.
bool iUpdatePrepareRestart();

// Waits for the worker thread to finish its job.
void iUpdateWait();

// Ends the platform's waits (iUpdatePlatform::Stop), cancels and waits; from
// the quit.
void iUpdateShutdown();

// The core's helpers for a platform's Install: `from` renamed over `to`,
// keeping `to` when it fails (iHostRenameReplace). Plain words in `why`.
bool iUpdateReplaceFile(const char* from, const char* to, char* why, size_t whySize);

#endif
