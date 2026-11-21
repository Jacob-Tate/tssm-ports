// The Switch's side of in-app updates (iUpdate.h; docs/SWITCH.md "Updates"):
// HTTPS through the system's own ssl service, and the install, which puts the
// checked .nro in place of the one this copy was started from.
//
// libnx side (iSwitch.h): nothing of the game's is included here.
//
// The connection: nifm says whether the console is online at all, the BSD
// sockets resolve the name and connect, and the ssl service runs TLS over that
// socket with the system's own trusted roots. Its checks stay on: the chain to
// a trusted root, the host name, and the certificate's dates. api.github.com
// and github.com chain to USERTrust ECC, the download hosts to ISRG Root X1;
// the system has both from 10.1.0. The ssl service is used non-blocking and
// polled, so a cancel or a stalled server is noticed within a tenth of a
// second; the core's HTTP/1.1 client (iUpdateHttpGet) runs over it.

#include "iSwitch.h"

#include "iUpdate.h"

// struct in_addr, which libnx's headers use without including.
#include <netinet/in.h>

#include <switch.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

// libnx's argument vector (runtime/argv.c): argv[0] is the .nro hbloader
// started, as the Homebrew Menu named it ("sdmc:/switch/tssm/tssm.nro").
extern "C" int __system_argc;
extern "C" char** __system_argv;

namespace
{
    const char kAssetSuffix[] = "-Switch.nro";

    // hbloader's buffer for the next program's path (nx-hbloader's
    // g_nextNroPath), terminator included; its argv buffer is 2048.
    const size_t kNextLoadPathMax = 512;

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

    // The .nro this copy runs from. Only a file on the SD card, named by
    // hbloader: a copy sent with nxlink lives in memory under another name.
    bool NroPath(char* out, size_t outSize, char* why, size_t whySize)
    {
        const char* argv0 = __system_argc > 0 && __system_argv != NULL ? __system_argv[0] : NULL;
        const size_t n = argv0 != NULL ? strlen(argv0) : 0;
        struct stat st;
        if (argv0 == NULL || strncmp(argv0, "sdmc:/", 6) != 0 || n < 11 || n >= outSize ||
            strcasecmp(argv0 + n - 4, ".nro") != 0 || stat(argv0, &st) != 0 || !S_ISREG(st.st_mode))
        {
            printf("tssm: update: switch: started as \"%s\", not a .nro on the SD card\n",
                   argv0 != NULL ? argv0 : "(no argv[0])");
            Say(why, whySize,
                "This copy cannot update itself: it was not started from a .nro file on the SD "
                "card. Start it from the Homebrew Menu.");
            return false;
        }
        memcpy(out, argv0, n + 1);
        return true;
    }

    uint64_t NowMs()
    {
        return armTicksToNs(armGetSystemTick()) / 1000000ULL;
    }

    // Time spent waiting for the server, step by step, each step counted as
    // a second at most. The waits poll every tenth of a second, so this is
    // the plain clock, except that a stay in the HOME menu (where the
    // program is suspended) does not read as a server that stopped
    // answering.
    class WaitClock
    {
    public:
        WaitClock() : mLast(NowMs()), mWaited(0) {}

        bool Over(int timeoutMs)
        {
            const uint64_t now = NowMs();
            const uint64_t step = now - mLast;
            mWaited += step < 1000 ? step : 1000;
            mLast = now;
            return mWaited > (uint64_t)timeoutMs;
        }

        void Reset()
        {
            mLast = NowMs();
            mWaited = 0;
        }

    private:
        uint64_t mLast;
        uint64_t mWaited;
    };

    bool IsSsl(Result rc, u32 description)
    {
        return R_MODULE(rc) == 123 && R_DESCRIPTION(rc) == description;
    }

    // The core's word for a failed handshake. 2123-0303 is NSS's "expired"
    // (or not yet valid): with the server's certificates current, the clock.
    iUpdateNet HandshakeError(Result rc, Result verify)
    {
        const Result r = R_FAILED(verify) ? verify : rc;
        if (IsSsl(r, 303))
        {
            return IUPDATE_NET_CLOCK;
        }
        if (R_MODULE(r) == 123 && ((R_DESCRIPTION(r) >= 301 && R_DESCRIPTION(r) <= 330) ||
                                   (R_DESCRIPTION(r) >= 1509 && R_DESCRIPTION(r) <= 1515)))
        {
            return IUPDATE_NET_CERT;
        }
        if (R_MODULE(r) == 123 && R_DESCRIPTION(r) >= 208 && R_DESCRIPTION(r) <= 212)
        {
            return IUPDATE_NET_LOST;
        }
        return IUPDATE_NET_TLS;
    }

    // The console's own view of the connection. Not a gate: the system's own
    // connection test can fail on a network that still reaches GitHub. So it
    // only names the failure when the name or the connection then fails:
    // "not connected to the internet" rather than a server that does not
    // answer.
    bool SystemSaysOnline(char* note, size_t noteSize)
    {
        Result rc = nifmInitialize(NifmServiceType_User);
        if (R_FAILED(rc))
        {
            Say(note, noteSize, "nifmInitialize %#x", rc);
            return true;
        }
        NifmInternetConnectionType type = (NifmInternetConnectionType)0;
        u32 strength = 0;
        NifmInternetConnectionStatus status = (NifmInternetConnectionStatus)0;
        rc = nifmGetInternetConnectionStatus(&type, &strength, &status);
        nifmExit();
        Say(note, noteSize, "nifm %#x, type %d, status %d", rc, R_FAILED(rc) ? -1 : (int)type,
            R_FAILED(rc) ? -1 : (int)status);
        return R_SUCCEEDED(rc) && status == NifmInternetConnectionStatus_Connected;
    }

    // One TLS connection on one socket.
    class Tls : public iUpdateStream
    {
    public:
        Tls(iUpdateSink* sink, int timeoutMs)
            : mSink(sink), mTimeoutMs(timeoutMs), mFd(-1), mGiven(-1), mSsl(false),
              mHaveContext(false), mHaveConnection(false)
        {
        }

        ~Tls()
        {
            if (mGiven >= 0)
            {
                close(mGiven);
            }
            if (mHaveConnection)
            {
                sslConnectionClose(&mConnection);
            }
            if (mHaveContext)
            {
                sslContextClose(&mContext);
            }
            if (mFd >= 0)
            {
                close(mFd);
            }
            if (mSsl)
            {
                sslExit();
            }
        }

        iUpdateNet Open(const char* host, char* detail, size_t detailSize)
        {
            char nifm[64];
            const bool online = SystemSaysOnline(nifm, sizeof(nifm));

            struct addrinfo hints;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            struct addrinfo* found = NULL;
            const int gai = getaddrinfo(host, "443", &hints, &found);
            if (gai != 0 || found == NULL)
            {
                Say(detail, detailSize, "getaddrinfo(%s): %d; %s", host, gai, nifm);
                return online ? IUPDATE_NET_DNS : IUPDATE_NET_OFFLINE;
            }
            iUpdateNet net = IUPDATE_NET_CONNECT;
            for (struct addrinfo* a = found; a != NULL && net == IUPDATE_NET_CONNECT; a = a->ai_next)
            {
                net = Connect(a->ai_addr, a->ai_addrlen, detail, detailSize);
            }
            freeaddrinfo(found);
            if (net != IUPDATE_NET_OK)
            {
                if (!online && net == IUPDATE_NET_CONNECT)
                {
                    net = IUPDATE_NET_OFFLINE;
                }
                return net;
            }
            if (!online)
            {
                printf("tssm: update: switch: %s, but %s answers\n", nifm, host);
            }

            Result rc = sslInitialize(1);
            if (R_FAILED(rc))
            {
                Say(detail, detailSize, "sslInitialize %#x", rc);
                return IUPDATE_NET_TLS;
            }
            mSsl = true;
            rc = sslCreateContext(&mContext, SslVersion_Auto);
            if (R_FAILED(rc))
            {
                Say(detail, detailSize, "sslCreateContext %#x", rc);
                return IUPDATE_NET_TLS;
            }
            mHaveContext = true;
            rc = sslContextCreateConnection(&mContext, &mConnection);
            if (R_FAILED(rc))
            {
                Say(detail, detailSize, "sslContextCreateConnection %#x", rc);
                return IUPDATE_NET_TLS;
            }
            mHaveConnection = true;

            // The service works on its own duplicate of the socket; this side
            // keeps its one descriptor and closes it after the connection.
            rc = sslConnectionSetOption(&mConnection, SslOptionType_DoNotCloseSocket, true);
            if (R_FAILED(rc))
            {
                Say(detail, detailSize, "DoNotCloseSocket %#x", rc);
                return IUPDATE_NET_TLS;
            }
            const int given = socketSslConnectionSetSocketDescriptor(&mConnection, mFd);
            if (given < 0 && errno != ENOENT)
            {
                Say(detail, detailSize, "SetSocketDescriptor errno %d, %#x", errno,
                    socketGetLastResult());
                return IUPDATE_NET_TLS;
            }
            if (given >= 0)
            {
                // Not expected with DoNotCloseSocket: a second handle to the
                // same socket, which libnx says to close before the
                // connection (the destructor does).
                printf("tssm: update: switch: the ssl service handed back descriptor %d\n", given);
                mGiven = given;
            }
            // The name's length without the end-of-string byte, as devkitPro's
            // libcurl passes it (proven on hardware): a counted end byte could
            // end up in the name the service sends and checks.
            rc = sslConnectionSetHostName(&mConnection, host, (u32)strlen(host));
            if (R_SUCCEEDED(rc))
            {
                rc = sslConnectionSetVerifyOption(&mConnection, SslVerifyOption_PeerCa |
                                                                     SslVerifyOption_HostName |
                                                                     SslVerifyOption_DateCheck);
            }
            if (R_SUCCEEDED(rc))
            {
                rc = sslConnectionSetIoMode(&mConnection, SslIoMode_NonBlocking);
            }
            if (R_FAILED(rc))
            {
                Say(detail, detailSize, "setting up the connection %#x", rc);
                return IUPDATE_NET_TLS;
            }

            WaitClock waited;
            while (true)
            {
                if (mSink->Cancelled())
                {
                    return IUPDATE_NET_CANCELLED;
                }
                rc = sslConnectionDoHandshake(&mConnection, NULL, NULL, NULL, 0);
                if (R_SUCCEEDED(rc))
                {
                    return IUPDATE_NET_OK;
                }
                if (!IsSsl(rc, 204))
                {
                    const Result verify = sslConnectionGetVerifyCertError(&mConnection);
                    Say(detail, detailSize, "handshake with %s: %#x (2%03u-%04u), certificate %#x%s",
                        host, rc, R_MODULE(rc), R_DESCRIPTION(rc), verify,
                        hosversionBefore(10, 1, 0) ?
                            "; system before 10.1.0 lacks GitHub's root certificates" :
                            "");
                    return HandshakeError(rc, verify);
                }
                if (waited.Over(mTimeoutMs))
                {
                    Say(detail, detailSize, "handshake with %s timed out", host);
                    return IUPDATE_NET_TIMEOUT;
                }
                // The client's messages go out inside DoHandshake; what it
                // waits for is the server. (The socket is nearly always
                // writable, so waiting for that too would spin.)
                Wait(SslPollEvent_Read);
            }
        }

        iUpdateNet Write(const void* data, size_t n) override
        {
            const char* p = (const char*)data;
            WaitClock waited;
            while (n > 0)
            {
                if (mSink->Cancelled())
                {
                    return IUPDATE_NET_CANCELLED;
                }
                u32 wrote = 0;
                const Result rc = sslConnectionWrite(&mConnection, p, (u32)n, &wrote);
                if (R_SUCCEEDED(rc) && wrote > 0)
                {
                    p += wrote;
                    n -= wrote;
                    waited.Reset();
                    continue;
                }
                if (R_SUCCEEDED(rc) || IsSsl(rc, 204))
                {
                    if (waited.Over(mTimeoutMs))
                    {
                        return IUPDATE_NET_TIMEOUT;
                    }
                    Wait(SslPollEvent_Write);
                    continue;
                }
                printf("tssm: update: switch: write %#x\n", rc);
                return IUPDATE_NET_LOST;
            }
            return IUPDATE_NET_OK;
        }

        iUpdateNet Read(void* data, size_t n, size_t* got) override
        {
            *got = 0;
            if (n > 0x10000)
            {
                n = 0x10000;
            }
            WaitClock waited;
            while (true)
            {
                if (mSink->Cancelled())
                {
                    return IUPDATE_NET_CANCELLED;
                }
                u32 read = 0;
                const Result rc = sslConnectionRead(&mConnection, data, (u32)n, &read);
                if (R_SUCCEEDED(rc))
                {
                    *got = read; // 0: the server closed the connection
                    return IUPDATE_NET_OK;
                }
                if (IsSsl(rc, 1501))
                {
                    return IUPDATE_NET_OK; // close_notify: the end
                }
                if (!IsSsl(rc, 204))
                {
                    printf("tssm: update: switch: read %#x (2%03u-%04u)\n", rc, R_MODULE(rc),
                           R_DESCRIPTION(rc));
                    return IsSsl(rc, 205) ? IUPDATE_NET_TIMEOUT : IUPDATE_NET_LOST;
                }
                if (waited.Over(mTimeoutMs))
                {
                    return IUPDATE_NET_TIMEOUT;
                }
                Wait(SslPollEvent_Read);
            }
        }

    private:
        iUpdateNet Connect(const struct sockaddr* addr, socklen_t len, char* detail,
                           size_t detailSize)
        {
            if (mFd >= 0)
            {
                close(mFd);
            }
            mFd = socket(AF_INET, SOCK_STREAM, 0);
            if (mFd < 0)
            {
                Say(detail, detailSize, "socket: errno %d", errno);
                return IUPDATE_NET_ERROR;
            }
            fcntl(mFd, F_SETFL, fcntl(mFd, F_GETFL, 0) | O_NONBLOCK);
            if (connect(mFd, addr, len) != 0 && errno != EINPROGRESS)
            {
                Say(detail, detailSize, "connect: errno %d", errno);
                return IUPDATE_NET_CONNECT;
            }
            WaitClock waited;
            while (true)
            {
                if (mSink->Cancelled())
                {
                    return IUPDATE_NET_CANCELLED;
                }
                struct pollfd pfd = { mFd, POLLOUT, 0 };
                const int ready = poll(&pfd, 1, 100);
                if (ready == 1)
                {
                    int err = -1;
                    socklen_t errLen = sizeof(err);
                    if (getsockopt(mFd, SOL_SOCKET, SO_ERROR, &err, &errLen) != 0 || err != 0)
                    {
                        Say(detail, detailSize, "connect: error %d", err);
                        return IUPDATE_NET_CONNECT;
                    }
                    break;
                }
                if (ready < 0 && errno != EINTR)
                {
                    Say(detail, detailSize, "poll: errno %d", errno);
                    return IUPDATE_NET_CONNECT;
                }
                if (waited.Over(mTimeoutMs))
                {
                    Say(detail, detailSize, "connect timed out");
                    return IUPDATE_NET_TIMEOUT;
                }
            }
            fcntl(mFd, F_SETFL, fcntl(mFd, F_GETFL, 0) & ~O_NONBLOCK);
            return IUPDATE_NET_OK;
        }

        // A tenth of a second at most, so a cancel is seen soon.
        void Wait(u32 events)
        {
            u32 out = 0;
            if (R_FAILED(sslConnectionPoll(&mConnection, events, &out, 100)))
            {
                svcSleepThread(20000000LL);
            }
        }

        iUpdateSink* mSink;
        int mTimeoutMs;
        int mFd;
        int mGiven; // a descriptor the ssl service handed back, if it ever does
        bool mSsl;
        bool mHaveContext;
        bool mHaveConnection;
        SslContext mContext;
        SslConnection mConnection;
    };

    class SwitchUpdate : public iUpdatePlatform
    {
    public:
        const char* AssetSuffix() override { return kAssetSuffix; }

        iUpdateNet Get(const iUpdateRequest& req, iUpdateSink* sink, char* detail,
                       size_t detailSize) override
        {
            Tls tls(sink, req.timeoutMs);
            const iUpdateNet net = tls.Open(req.host, detail, detailSize);
            if (net != IUPDATE_NET_OK)
            {
                return net;
            }
            return iUpdateHttpGet(&tls, req, sink, detail, detailSize);
        }

        bool DownloadPath(char* out, size_t outSize, char* why, size_t whySize) override
        {
            char nro[768];
            if (!NroPath(nro, sizeof(nro), why, whySize))
            {
                return false;
            }
            // Beside the .nro, so the rename stays on the card; the Homebrew
            // Menu lists only .nro files, so it never shows this one.
            snprintf(out, outSize, "%s.download", nro);
            return true;
        }

        bool Install(const char* path, const iUpdateVersion& version, char* why,
                     size_t whySize) override
        {
            (void)version;
            char nro[768];
            if (!NroPath(nro, sizeof(nro), why, whySize))
            {
                return false;
            }

            // An NRO says so at 0x10. The checksum already matched; this only
            // keeps a release mix-up from replacing the game with something
            // that is not a Switch program at all.
            unsigned char head[0x14];
            FILE* f = fopen(path, "rb");
            const bool read = f != NULL && fread(head, 1, sizeof(head), f) == sizeof(head);
            if (f != NULL)
            {
                fclose(f);
            }
            if (!read || memcmp(head + 0x10, "NRO0", 4) != 0)
            {
                Say(why, whySize,
                    "The downloaded file is not a Switch program, so it was not installed. The "
                    "installed version is unchanged.");
                return false;
            }

            // The RomFS keeps the running .nro open (libnx's romfsMountSelf),
            // and the card does not rename an open file. Nothing reads the
            // RomFS after start-up.
            iSwitchReleaseRomfs();

            printf("tssm: update: switch: replacing %s\n", nro);
            return iUpdateReplaceFile(path, nro, why, whySize);
        }

        bool CanRestart() override
        {
            char nro[768];
            char why[256];
            return envHasNextLoad() && NroPath(nro, sizeof(nro), why, sizeof(why)) &&
                   strlen(nro) < kNextLoadPathMax;
        }

        bool PrepareRestart() override
        {
            char nro[768];
            char why[256];
            if (!envHasNextLoad() || !NroPath(nro, sizeof(nro), why, sizeof(why)))
            {
                return false;
            }
            // envSetNextLoad copies without a length check.
            if (strlen(nro) >= kNextLoadPathMax)
            {
                printf("tssm: update: switch: %s is too long a path for hbloader to start\n", nro);
                return false;
            }
            // As the Homebrew Menu starts a program: the path, and argv[0]
            // the same path in quotes.
            char argv[800];
            snprintf(argv, sizeof(argv), "\"%s\"", nro);
            const Result rc = envSetNextLoad(nro, argv);
            printf("tssm: update: switch: after the quit hbloader starts %s (%#x)\n", nro, rc);
            return R_SUCCEEDED(rc);
        }
    };
} // namespace

iUpdatePlatform* iUpdateCreatePlatform()
{
    static SwitchUpdate sPlatform;
    return &sPlatform;
}
