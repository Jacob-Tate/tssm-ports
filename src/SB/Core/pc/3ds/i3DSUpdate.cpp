// The 3DS's side of in-app updates (iUpdate.h; docs/3DS.md "Updates"), game
// side: the iUpdatePlatform object. The connection, AM and the running
// title's files are the libctru side's (i3DSUpdateSys.cpp, i3DSUpdate.h).
//
// Two kinds of copy, each updated the way it was installed:
//   the installed title (.cia)  the release's -3DS.cia, installed through AM
//                               over this title; the game can start again by
//                               itself (an application jump).
//   a .3dsx                     the release's -3DS.3dsx, renamed over the
//                               file the Homebrew Launcher started.
// A copy with pictures of its own is never replaced by the public file: one
// built with its own icon or banner (TSSM_UPDATE_OWN_ART, CMakeLists.txt),
// and one whose icon, banner or SMDH is not what the build packed it with
// (romfs:/update/art.txt), which is what a .cia remade with makerom from
// the public program and a banner of the player's own looks like.
//
// Each request runs on a thread of its own with a 64 KB stack (RunGet): the
// core's worker is a std::thread, which libctru starts with 32 KB, and the
// TLS handshake and the HTTP client go deepest. The log says how much of it
// each request used.

#include "3ds/i3DS.h"
#include "3ds/i3DSPackage.h"
#include "3ds/i3DSUpdate.h"

#include "iHost.h"
#include "iUpdate.h"
#include "iWindow.h"

#include <atomic>
#include <thread>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>

static_assert(IUPDATE_NET_OK == k3DSNetOk && IUPDATE_NET_OFFLINE == k3DSNetOffline &&
                  IUPDATE_NET_DNS == k3DSNetDns && IUPDATE_NET_CONNECT == k3DSNetConnect &&
                  IUPDATE_NET_TLS == k3DSNetTls && IUPDATE_NET_CERT == k3DSNetCert &&
                  IUPDATE_NET_CLOCK == k3DSNetClock && IUPDATE_NET_TIMEOUT == k3DSNetTimeout &&
                  IUPDATE_NET_LOST == k3DSNetLost && IUPDATE_NET_CANCELLED == k3DSNetCancelled &&
                  IUPDATE_NET_ERROR == k3DSNetError,
              "i3DSUpdate.h's k3DSNet* codes are iUpdate.h's iUpdateNet numbers");

#ifdef TSSM_UPDATE_TEST_SOURCE
// i3DSUpdateTest.cpp: a test build's stand-in for GitHub, files in a folder
// on the SD card. Never in a release (CMakeLists.txt).
iUpdateNet i3DSUpdateTestGet(const iUpdateRequest& req, iUpdateSink* sink, char* detail,
                             size_t detailSize);
#endif

namespace
{
    const char kCiaSuffix[] = "-3DS.cia";
    const char k3dsxSuffix[] = "-3DS.3dsx";

    void Say(char* out, size_t outSize, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
    void Say(char* out, size_t outSize, const char* fmt, ...)
    {
        if (out == NULL || outSize == 0)
        {
            return;
        }
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(out, outSize, fmt, ap);
        va_end(ap);
    }

    int CancelThunk(void* ctx)
    {
        return ((iUpdateSink*)ctx)->Cancelled() ? 1 : 0;
    }

    // The core's HTTP client over one TLS connection (i3DSUpdateSys.cpp).
    class TlsStream : public iUpdateStream
    {
    public:
        explicit TlsStream(i3DSTls* tls) : mTls(tls)
        {
        }
        iUpdateNet Write(const void* data, size_t n) override
        {
            return (iUpdateNet)i3DSTlsWrite(mTls, data, n);
        }
        iUpdateNet Read(void* data, size_t n, size_t* got) override
        {
            return (iUpdateNet)i3DSTlsRead(mTls, data, n, got);
        }

    private:
        i3DSTls* mTls;
    };

    // The first `n` bytes of a file (fewer when it is shorter), and its size.
    bool ReadHead(const char* path, uint8_t* out, size_t n, size_t* got, uint64_t* size)
    {
        *got = 0;
        *size = 0;
        FILE* f = fopen(path, "rb");
        if (f == NULL)
        {
            return false;
        }
        struct stat st;
        const bool ok = fstat(fileno(f), &st) == 0;
        if (ok)
        {
            *size = (uint64_t)st.st_size;
            *got = fread(out, 1, n, f);
        }
        fclose(f);
        return ok && *got > 0;
    }

    bool HashBytes(const uint8_t* data, size_t n, uint8_t out[32])
    {
        iUpdateSha256 sha;
        if (!sha.Ok())
        {
            return false;
        }
        sha.Update(data, n);
        sha.Final(out);
        return true;
    }

    // romfs:/update/art.txt: "<what> <64 hex digits>" lines. False when the
    // line is not there.
    bool PackedArt(const char* what, char hex[65])
    {
        FILE* f = fopen(k3DSUpdateArtPath, "r");
        if (f == NULL)
        {
            return false;
        }
        char line[160];
        bool found = false;
        const size_t n = strlen(what);
        while (!found && fgets(line, sizeof(line), f) != NULL)
        {
            if (strncmp(line, what, n) == 0 && line[n] == ' ' && strlen(line + n + 1) >= 64)
            {
                memcpy(hex, line + n + 1, 64);
                hex[64] = '\0';
                found = true;
            }
        }
        fclose(f);
        return found;
    }

    // What the comparison of this copy's pictures with the build's found.
    enum Art
    {
        ART_SAME,
        ART_DIFFERENT,
        ART_UNKNOWN // could not be read or compared
    };

    // One piece of this copy against what the build packed. `what` names the
    // line in art.txt.
    Art SameAsPacked(const char* what, const uint8_t* data, size_t n, char* note, size_t noteSize)
    {
        char packed[65];
        if (!PackedArt(what, packed))
        {
            Say(note, noteSize, "no %s line in %s", what, k3DSUpdateArtPath);
            return ART_UNKNOWN;
        }
        uint8_t sum[32];
        char hex[65];
        if (!HashBytes(data, n, sum))
        {
            Say(note, noteSize, "no SHA-256");
            return ART_UNKNOWN;
        }
        iUpdateHex(sum, 32, hex, sizeof(hex));
        if (strcmp(hex, packed) != 0)
        {
            Say(note, noteSize, "this copy's %s is %.16s..., the build packed %.16s...", what, hex,
                packed);
            return ART_DIFFERENT;
        }
        return ART_SAME;
    }

    // One request, on its own thread (i3DSUpdateRunOnStack).
    const size_t kRequestStack = 64 * 1024;

    struct GetJob
    {
        const iUpdateRequest* req;
        iUpdateSink* sink;
        char* detail;
        size_t detailSize;
        iUpdateNet net;
    };

    void RunGet(void* p)
    {
        GetJob* job = (GetJob*)p;
#ifdef TSSM_UPDATE_TEST_SOURCE
        job->net = i3DSUpdateTestGet(*job->req, job->sink, job->detail, job->detailSize);
#else
        i3DSTls* tls = NULL;
        job->net = (iUpdateNet)i3DSTlsOpen(job->req->host, job->req->timeoutMs, CancelThunk,
                                           job->sink, &tls, job->detail, job->detailSize);
        if (job->net == IUPDATE_NET_OK)
        {
            TlsStream stream(tls);
            job->net = iUpdateHttpGet(&stream, *job->req, job->sink, job->detail, job->detailSize);
        }
        i3DSTlsClose(tls);
#endif
    }

    class Update3DS : public iUpdatePlatform
    {
    public:
        Update3DS() : mCia(false), mTitleId(0), mHave3dsx(false)
        {
            mPath[0] = '\0';
            mCia = i3DSUpdateInstalled(&mTitleId) != 0;
            if (!mCia)
            {
                mHave3dsx = i3DSUpdateSelfPath(mPath, sizeof(mPath)) != 0;
            }
            printf("tssm: update: 3ds: this copy is %s\n", mCia ? "the installed title (.cia)" :
                                                           mHave3dsx ? mPath :
                                                                       "not updatable");
        }

        const char* AssetSuffix() override
        {
            return mCia ? kCiaSuffix : k3dsxSuffix;
        }

        iUpdateNet Get(const iUpdateRequest& req, iUpdateSink* sink, char* detail,
                       size_t detailSize) override
        {
            GetJob job = { &req, sink, detail, detailSize, IUPDATE_NET_ERROR };
            if (!i3DSUpdateRunOnStack(RunGet, &job, kRequestStack, req.host))
            {
                Say(detail, detailSize, "no thread for the request");
                return IUPDATE_NET_ERROR;
            }
            return job.net;
        }

        bool DownloadPath(char* out, size_t outSize, char* why, size_t whySize) override
        {
            if (mCia)
            {
                // The game's folder: AM reads it from there.
                snprintf(out, outSize, "%s/update.cia.download", k3DSRoot);
                return true;
            }
            if (!mHave3dsx)
            {
                Say(why, whySize,
                    "This copy cannot update itself: it was not started from a "
                    ".3dsx file on the SD card. Start it from the Homebrew Launcher.");
                return false;
            }
            // Beside the .3dsx, so the rename stays on the card; the Homebrew
            // Launcher lists only .3dsx files, so it never shows this one.
            snprintf(out, outSize, "%s.download", mPath);
            return true;
        }

        bool Install(const char* path, const iUpdateVersion& version, char* why,
                     size_t whySize) override
        {
            return mCia ? InstallCia(path, version, why, whySize) : Install3dsx(path, why, whySize);
        }

        bool InstallAllowed(char* why, size_t whySize) override
        {
#ifdef TSSM_UPDATE_OWN_ART
            printf("tssm: update: 3ds: built with an icon or banner of its own "
                   "(TSSM_UPDATE_OWN_ART)\n");
            Say(why, whySize,
                "This copy was built with an icon or banner of its own, so the updater does not "
                "replace it with the public version. Install the new version by hand if you want "
                "it.");
            return false;
#else
            char note[192];
            note[0] = '\0';
            Art art = ART_UNKNOWN;
            if (mCia)
            {
                art = OwnCiaArt(note, sizeof(note));
            }
            else if (mHave3dsx)
            {
                art = Own3dsxArt(note, sizeof(note));
            }
            else
            {
                Say(why, whySize,
                    "This copy cannot update itself: it was not started from a "
                    ".3dsx file on the SD card.");
                return false;
            }
            if (art == ART_DIFFERENT)
            {
                printf("tssm: update: 3ds: not replaced: %s\n", note);
                Say(why, whySize,
                    "This copy's %s is not the one this version came with (a copy with your own "
                    "banner or icon, for example), so the updater does not replace it. Install "
                    "the new version by hand if you want it.",
                    mCia ? "icon or banner" : "icon");
                return false;
            }
            if (art == ART_UNKNOWN)
            {
                printf("tssm: update: 3ds: not replaced, not checked: %s\n", note);
                Say(why, whySize,
                    "This copy could not check its own %s, so the updater does not replace it. "
                    "Install the new version by hand if you want it.",
                    mCia ? "icon and banner" : "icon");
                return false;
            }
            return true;
#endif
        }

        bool CanRestart() override
        {
            return mCia;
        }

        bool PrepareRestart() override
        {
            return mCia && i3DSUpdateRestartAfterQuit() != 0;
        }

    private:
        // The installed title's icon and banner, as the HOME Menu shows them.
        Art OwnCiaArt(char* note, size_t noteSize)
        {
            static const char* const kParts[] = { "icon", "banner" };
            for (const char* part : kParts)
            {
                uint8_t* data = NULL;
                size_t n = 0;
                if (!i3DSUpdateReadOwnExefs(part, &data, &n))
                {
                    Say(note, noteSize, "this title's %s could not be read", part);
                    return ART_UNKNOWN;
                }
                const Art art = SameAsPacked(part, data, n, note, noteSize);
                free(data);
                if (art != ART_SAME)
                {
                    return art;
                }
            }
            return ART_SAME;
        }

        // The .3dsx's own SMDH (its name and icon).
        Art Own3dsxArt(char* note, size_t noteSize)
        {
            uint8_t head[64];
            size_t got = 0;
            uint64_t size = 0;
            i3DS3dsxInfo info;
            char why[160];
            if (!ReadHead(mPath, head, sizeof(head), &got, &size) ||
                !i3DSParse3dsx(head, got, size, &info, why, sizeof(why)))
            {
                Say(note, noteSize, "%s: %s", mPath, got > 0 ? why : "could not be read");
                return ART_UNKNOWN;
            }
            uint8_t* smdh = (uint8_t*)malloc(info.smdhSize);
            FILE* f = fopen(mPath, "rb");
            const bool read = smdh != NULL && f != NULL &&
                              fseek(f, info.smdhOffset, SEEK_SET) == 0 &&
                              fread(smdh, 1, info.smdhSize, f) == info.smdhSize;
            if (f != NULL)
            {
                fclose(f);
            }
            const Art art =
                read ? SameAsPacked("smdh", smdh, info.smdhSize, note, noteSize) : ART_UNKNOWN;
            if (!read)
            {
                Say(note, noteSize, "the SMDH of %s could not be read", mPath);
            }
            free(smdh);
            return art;
        }

        bool InstallCia(const char* path, const iUpdateVersion& named, char* why, size_t whySize)
        {
            // The download's own word on what it is, before AM sees it: a
            // .cia of this very title (a test copy's ID stays a test copy's).
            static uint8_t head[64 * 1024];
            size_t got = 0;
            uint64_t size = 0;
            i3DSCiaInfo cia;
            char bad[160];
            if (!ReadHead(path, head, sizeof(head), &got, &size) ||
                !i3DSParseCia(head, got, size, &cia, bad, sizeof(bad)))
            {
                printf("tssm: update: 3ds: the download is not a CIA: %s\n",
                       got > 0 ? bad : "unreadable");
                Say(why, whySize,
                    "The downloaded file is not a 3DS title (.cia), so it was not "
                    "installed. The installed version is unchanged.");
                return false;
            }
            char version[24];
            i3DSTitleVersionText(cia.version, version, sizeof(version));
            printf("tssm: update: 3ds: the download is title %016llx, version %s (%u), %u "
                   "content(s)\n",
                   (unsigned long long)cia.titleId, version, (unsigned)cia.version,
                   (unsigned)cia.contentCount);
            if (cia.titleId != mTitleId)
            {
                Say(why, whySize,
                    "The download is another title (%016llx), not this one (%016llx), so it was "
                    "not installed. The installed version is unchanged.",
                    (unsigned long long)cia.titleId, (unsigned long long)mTitleId);
                return false;
            }
            // The title version must be the one in the file's name: the HOME
            // Menu and the next check go by it, and a .cia made without
            // tools/3ds/cia_version.py holds 0.9.3 as 0.0.3, older than 0.9.2.
            char namedText[32];
            iUpdateFormatVersion(named, namedText, sizeof(namedText));
            if (!i3DSCiaVersionIs(cia.version, namedText))
            {
                printf("tssm: update: 3ds: the download's title version %s is not %s, the one "
                       "in its name\n",
                       version, namedText);
                Say(why, whySize,
                    "The download says it is version %s, not %s as its name does, so it was not "
                    "installed. The installed version is unchanged.",
                    version, namedText);
                return false;
            }
            uint16_t now = 0;
            if (!i3DSUpdateInstallCia(path, mTitleId, cia.version, &now, why, whySize))
            {
                return false;
            }
            return true;
        }

        bool Install3dsx(const char* path, char* why, size_t whySize)
        {
            if (!mHave3dsx)
            {
                Say(why, whySize,
                    "This copy cannot update itself: it was not started from a "
                    ".3dsx file on the SD card.");
                return false;
            }
            uint8_t head[64];
            size_t got = 0;
            uint64_t size = 0;
            i3DS3dsxInfo info;
            char bad[160];
            if (!ReadHead(path, head, sizeof(head), &got, &size) ||
                !i3DSParse3dsx(head, got, size, &info, bad, sizeof(bad)))
            {
                printf("tssm: update: 3ds: the download is not a .3dsx: %s\n",
                       got > 0 ? bad : "unreadable");
                Say(why, whySize,
                    "The downloaded file is not a 3DS homebrew program (.3dsx), so "
                    "it was not installed. The installed version is unchanged.");
                return false;
            }
            // The RomFS holds the running file open.
            i3DSUpdateReleaseRomfs();
            printf("tssm: update: 3ds: replacing %s\n", mPath);
            const bool ok = iUpdateReplaceFile(path, mPath, why, whySize);
            i3DSUpdateRemountRomfs();
            return ok;
        }

        bool mCia;
        uint64_t mTitleId;
        bool mHave3dsx;
        char mPath[512];
    };
} // namespace

#ifdef TSSM_UPDATE_TEST_BUILD
// TSSM_TEST_UPDATE (test-env.txt; emulator runs, docs/3DS.md "Updates"):
// the updater driven without the menu, each state change logged as
// "tssm: update: test: ...". check: one check. install: a check, then the
// download and the install of what it found. cancel: the download cancelled
// once 256 KB are in. restart: install, then the restart the menu's Restart
// now asks for. Nothing happens without the variable. Only in a test build
// (CMakeLists.txt, TSSM_UPDATE_TEST_BUILD): a release never goes online or
// installs without the player's button.
namespace
{
    std::thread sTestDriver;
    std::atomic<bool> sTestStop{ false };

    void TestDriver(std::string mode)
    {
        printf("tssm: update: test: TSSM_TEST_UPDATE=%s\n", mode.c_str());
        bool checked = false;
        bool downloading = false;
        bool cancelled = false;
        iUpdateState last = (iUpdateState)-1;
        for (int i = 0; i < 20 * 600 && !sTestStop.load(); i++)
        {
            i3DSSleepNs(50000000);
            if (!iUpdateReady())
            {
                continue;
            }
            iUpdateStatus st;
            iUpdateGetStatus(&st);
            if (st.state != last)
            {
                printf("tssm: update: test: %s: %s\n", iUpdateStateName(st.state), st.message);
                last = st.state;
            }
            if (!checked && iUpdateCanCheck())
            {
                checked = true;
                iUpdateCheck();
                continue;
            }
            if (mode == "check" && checked &&
                (st.state == IUPDATE_UP_TO_DATE || st.state == IUPDATE_AVAILABLE ||
                 st.state == IUPDATE_ERROR))
            {
                break;
            }
            if (!downloading && st.state == IUPDATE_AVAILABLE && mode != "check")
            {
                if (!st.installAllowed)
                {
                    printf("tssm: update: test: not allowed: %s\n", st.installBlocked);
                    break;
                }
                downloading = true;
                iUpdateDownload();
                continue;
            }
            if (mode == "cancel" && downloading && !cancelled && st.state == IUPDATE_DOWNLOADING &&
                st.done >= 256 * 1024)
            {
                cancelled = true;
                printf("tssm: update: test: cancelling at %llu bytes\n",
                       (unsigned long long)st.done);
                iUpdateCancel();
                continue;
            }
            if (downloading && (st.state == IUPDATE_ERROR ||
                                (st.state == IUPDATE_AVAILABLE && iUpdateCanDownload())))
            {
                break;
            }
            if (st.state == IUPDATE_DONE_RESTART)
            {
                if (mode == "restart")
                {
                    const bool ok = st.canRestart && iUpdatePrepareRestart();
                    printf("tssm: update: test: restart %s\n",
                           ok ? "arranged; quitting" : "not possible");
                    if (ok)
                    {
                        iWindowRequestClose();
                    }
                }
                break;
            }
        }
        printf("tssm: update: test: done\n");
    }
} // namespace
#endif

iUpdatePlatform* iUpdateCreatePlatform()
{
    static Update3DS sPlatform;
#ifdef TSSM_UPDATE_TEST_BUILD
    const char* test = getenv("TSSM_TEST_UPDATE");
    if (test != NULL && test[0] != '\0' && !sTestDriver.joinable())
    {
        sTestDriver = std::thread(TestDriver, std::string(test));
    }
#endif
    return &sPlatform;
}

void i3DSUpdateStop()
{
#ifdef TSSM_UPDATE_TEST_BUILD
    if (sTestDriver.joinable())
    {
        sTestStop = true;
        sTestDriver.join();
    }
#endif
    iUpdateShutdown();
}
