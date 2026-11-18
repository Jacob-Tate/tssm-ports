// The 3DS updater's libctru side (i3DSUpdate.h): HTTPS with mbedtls over the
// SOC service, the CIA install through AM, and the running title's own files.
// Compiled with libctru's short enums, as devkitPro builds libctru and its
// mbedtls (CMakeLists.txt); nothing of the game's is included.
//
// Why mbedtls and not the system's HTTP service (httpc): the system's TLS
// cannot finish a handshake with GitHub's servers today. api.github.com and
// github.com offer ECDSA certificates only (Sectigo's E46 root, cross-signed
// by USERTrust ECC), and the 3DS's ssl module, which httpc uses, has neither
// ECDSA nor the roots. Open-source 3DS updaters reach GitHub through libcurl
// on devkitPro's mbedtls for that reason. Here mbedtls runs TLS 1.2 itself,
// with Mozilla's CA list from the RomFS (k3DSUpdateCaPath), the host name
// check (SNI too) and the dates against the console's clock; randomness comes
// from the system's ssl service (devkitPro's mbedtls_hardware_poll).

#include "3ds/i3DS.h"
#include "3ds/i3DSUpdate.h"

#include <3ds.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

// libctru's argument vector (system/initArgv.c): argv[0] is the .3dsx the
// Homebrew Launcher started ("sdmc:/3ds/tssm/tssm.3dsx").
extern "C" int __system_argc;
extern "C" char** __system_argv;

namespace
{
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

    uint64_t NowMs()
    {
        return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000);
    }

    bool WouldBlock()
    {
        return errno == EAGAIN || errno == EWOULDBLOCK;
    }

    // The console's own view of the network, to name a failure: not a gate.
    // ACU_GetStatus: 3 is connected.
    bool SystemSaysOnline(char* note, size_t noteSize)
    {
        if (R_FAILED(acInit()))
        {
            Say(note, noteSize, "ac: not available");
            return true;
        }
        u32 status = 0;
        const Result rc = ACU_GetStatus(&status);
        acExit();
        Say(note, noteSize, "ac status %lu (%#lx)", (unsigned long)status, (unsigned long)rc);
        return R_FAILED(rc) || status == 3;
    }

    // An mbedtls error code in words, when its strings are built in.
    void ErrorText(int code, char* out, size_t outSize)
    {
#if defined(MBEDTLS_ERROR_C)
        mbedtls_strerror(code, out, outSize);
#else
        Say(out, outSize, "mbedtls -0x%04x", (unsigned)-code);
#endif
    }

    // The whole CA list into memory, ended with the NUL mbedtls's PEM parser
    // wants. NULL when it cannot be read.
    char* ReadCaList(size_t* size)
    {
        FILE* f = fopen(k3DSUpdateCaPath, "rb");
        if (f == NULL)
        {
            return NULL;
        }
        char* buf = NULL;
        if (fseek(f, 0, SEEK_END) == 0)
        {
            const long n = ftell(f);
            if (n > 0 && n < 2 * 1024 * 1024 && fseek(f, 0, SEEK_SET) == 0)
            {
                buf = (char*)malloc((size_t)n + 1);
                if (buf != NULL && fread(buf, 1, (size_t)n, f) == (size_t)n)
                {
                    buf[n] = '\0';
                    *size = (size_t)n + 1;
                }
                else
                {
                    free(buf);
                    buf = NULL;
                }
            }
        }
        fclose(f);
        return buf;
    }
} // namespace

struct i3DSTls
{
    int fd;
    int timeoutMs;
    i3DSNetCancelFn cancelled;
    void* ctx;
    uint32_t sleeps; // i3DSSleepCount at the start: the console slept since when it differs
    int why; // what stopped the last socket call (a k3DSNet* code)
    int sysErrno; // and its errno, for the log
    bool sslc; // sslcInit held (the randomness)
    bool mbed; // the mbedtls objects initialised
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt ca;
    mbedtls_ssl_config conf;
    mbedtls_ssl_context ssl;
};

namespace
{
    // Between two non-blocking tries: k3DSNetOk to try again, or why not.
    int Nap(i3DSTls* t, uint64_t since)
    {
        if (t->cancelled != NULL && t->cancelled(t->ctx) != 0)
        {
            return k3DSNetCancelled;
        }
        if (i3DSSleepCount() != t->sleeps)
        {
            // The Wi-Fi went down with the console; whatever this socket was
            // doing will not finish.
            return k3DSNetLost;
        }
        if (NowMs() - since > (uint64_t)t->timeoutMs)
        {
            return k3DSNetTimeout;
        }
        svcSleepThread(5000000LL);
        return k3DSNetOk;
    }

    int SendCb(void* p, const unsigned char* buf, size_t len)
    {
        i3DSTls* t = (i3DSTls*)p;
        const uint64_t since = NowMs();
        while (true)
        {
            const ssize_t w = send(t->fd, buf, len, 0);
            if (w > 0)
            {
                return (int)w;
            }
            if (w < 0 && WouldBlock())
            {
                const int why = Nap(t, since);
                if (why == k3DSNetOk)
                {
                    continue;
                }
                t->why = why;
                return MBEDTLS_ERR_NET_SEND_FAILED;
            }
            t->why = k3DSNetLost;
            t->sysErrno = errno;
            return MBEDTLS_ERR_NET_SEND_FAILED;
        }
    }

    int RecvCb(void* p, unsigned char* buf, size_t len)
    {
        i3DSTls* t = (i3DSTls*)p;
        const uint64_t since = NowMs();
        while (true)
        {
            const ssize_t r = recv(t->fd, buf, len, 0);
            if (r >= 0)
            {
                return (int)r; // 0: the server closed its side
            }
            if (WouldBlock())
            {
                const int why = Nap(t, since);
                if (why == k3DSNetOk)
                {
                    continue;
                }
                t->why = why;
                return MBEDTLS_ERR_NET_RECV_FAILED;
            }
            t->why = k3DSNetLost;
            t->sysErrno = errno;
            return MBEDTLS_ERR_NET_RECV_FAILED;
        }
    }

    // A non-blocking connect, waited for in naps.
    int Connect(i3DSTls* t, const struct sockaddr* addr, socklen_t len, char* detail,
                size_t detailSize)
    {
        if (t->fd >= 0)
        {
            close(t->fd);
        }
        t->fd = socket(AF_INET, SOCK_STREAM, 0);
        if (t->fd < 0)
        {
            Say(detail, detailSize, "socket: errno %d", errno);
            return k3DSNetError;
        }
        if (fcntl(t->fd, F_SETFL, O_NONBLOCK) < 0)
        {
            Say(detail, detailSize, "fcntl: errno %d", errno);
            return k3DSNetError;
        }
        if (connect(t->fd, addr, len) != 0 && errno != EINPROGRESS && errno != EALREADY &&
            !WouldBlock())
        {
            Say(detail, detailSize, "connect: errno %d", errno);
            return k3DSNetConnect;
        }
        const uint64_t since = NowMs();
        while (true)
        {
            struct pollfd pfd = { t->fd, POLLOUT, 0 };
            const int ready = poll(&pfd, 1, 0);
            if (ready == 1)
            {
                int err = -1;
                socklen_t errLen = sizeof(err);
                if (getsockopt(t->fd, SOL_SOCKET, SO_ERROR, &err, &errLen) != 0 || err != 0)
                {
                    Say(detail, detailSize, "connect: error %d", err);
                    return k3DSNetConnect;
                }
                return k3DSNetOk;
            }
            if (ready < 0)
            {
                Say(detail, detailSize, "poll: errno %d", errno);
                return k3DSNetConnect;
            }
            const int why = Nap(t, since);
            if (why != k3DSNetOk)
            {
                Say(detail, detailSize, "connect: %s",
                    why == k3DSNetTimeout ? "timed out" : "stopped");
                return why;
            }
        }
    }

    // The handshake's failure as the core names it, with the details.
    int HandshakeError(i3DSTls* t, int ret, const char* host, char* detail, size_t detailSize)
    {
        if (ret == MBEDTLS_ERR_NET_RECV_FAILED || ret == MBEDTLS_ERR_NET_SEND_FAILED)
        {
            Say(detail, detailSize, "handshake with %s: %s (errno %d)", host,
                t->why == k3DSNetCancelled ? "cancelled" :
                t->why == k3DSNetTimeout   ? "timed out" :
                                             "connection lost",
                t->sysErrno);
            return t->why != k3DSNetOk ? t->why : k3DSNetLost;
        }
        if (ret == MBEDTLS_ERR_SSL_CONN_EOF)
        {
            Say(detail, detailSize, "handshake with %s: the server closed the connection", host);
            return k3DSNetLost;
        }
        if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
        {
            const uint32_t flags = mbedtls_ssl_get_verify_result(&t->ssl);
            char info[160];
            info[0] = '\0';
#if !defined(MBEDTLS_X509_REMOVE_INFO)
            mbedtls_x509_crt_verify_info(info, sizeof(info), "", flags);
            for (char* c = info; *c != '\0'; c++)
            {
                if (*c == '\n')
                {
                    *c = ';';
                }
            }
#endif
            Say(detail, detailSize, "certificate of %s refused: flags %#lx %s", host,
                (unsigned long)flags, info);
            // Only the dates wrong (expired, or not valid yet), with the
            // server's certificates current: the console's clock.
            const uint32_t dates = MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE;
            if ((flags & dates) != 0 && (flags & ~dates) == 0)
            {
                return k3DSNetClock;
            }
            return k3DSNetCert;
        }
        char text[96];
        ErrorText(ret, text, sizeof(text));
        Say(detail, detailSize, "handshake with %s: -0x%04x %s", host, (unsigned)-ret, text);
        return k3DSNetTls;
    }
} // namespace

int i3DSTlsOpen(const char* host, int timeoutMs, i3DSNetCancelFn cancelled, void* ctx,
                i3DSTls** out, char* detail, size_t detailSize)
{
    *out = NULL;
    if (!i3DSNetStart())
    {
        Say(detail, detailSize, "the SOC service could not be started (not enough memory?)");
        return k3DSNetError;
    }
    i3DSTls* t = (i3DSTls*)calloc(1, sizeof(i3DSTls));
    if (t == NULL)
    {
        Say(detail, detailSize, "out of memory");
        return k3DSNetError;
    }
    t->fd = -1;
    t->timeoutMs = timeoutMs > 0 ? timeoutMs : 20000;
    t->cancelled = cancelled;
    t->ctx = ctx;
    t->sleeps = i3DSSleepCount();
    t->why = k3DSNetOk;
    *out = t;

    char net[64];
    const bool online = SystemSaysOnline(net, sizeof(net));

    // The name: the one call that waits inside the SOC service (its own
    // limit, the system's resolver's).
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* found = NULL;
    const int gai = getaddrinfo(host, "443", &hints, &found);
    if (gai != 0 || found == NULL)
    {
        Say(detail, detailSize, "getaddrinfo(%s): %d; %s", host, gai, net);
        return online ? k3DSNetDns : k3DSNetOffline;
    }
    if (cancelled != NULL && cancelled(ctx) != 0)
    {
        freeaddrinfo(found);
        return k3DSNetCancelled;
    }
    int r = k3DSNetConnect;
    for (struct addrinfo* a = found; a != NULL && r == k3DSNetConnect; a = a->ai_next)
    {
        r = Connect(t, a->ai_addr, a->ai_addrlen, detail, detailSize);
    }
    freeaddrinfo(found);
    if (r != k3DSNetOk)
    {
        return !online && r == k3DSNetConnect ? k3DSNetOffline : r;
    }

    // mbedtls_hardware_poll (devkitPro's) asks the ssl service for random bytes.
    const Result src = sslcInit(0);
    if (R_FAILED(src))
    {
        Say(detail, detailSize, "sslcInit %#lx (the system's random numbers)", (unsigned long)src);
        return k3DSNetTls;
    }
    t->sslc = true;

    mbedtls_entropy_init(&t->entropy);
    mbedtls_ctr_drbg_init(&t->drbg);
    mbedtls_x509_crt_init(&t->ca);
    mbedtls_ssl_config_init(&t->conf);
    mbedtls_ssl_init(&t->ssl);
    t->mbed = true;

    static const char kPersonal[] = "tssm-update";
    int ret = mbedtls_ctr_drbg_seed(&t->drbg, mbedtls_entropy_func, &t->entropy,
                                    (const unsigned char*)kPersonal, sizeof(kPersonal) - 1);
    if (ret != 0)
    {
        Say(detail, detailSize, "random seed: -0x%04x", (unsigned)-ret);
        return k3DSNetTls;
    }

    size_t caSize = 0;
    char* caText = ReadCaList(&caSize);
    if (caText == NULL)
    {
        Say(detail, detailSize, "the CA list %s could not be read", k3DSUpdateCaPath);
        return k3DSNetCert;
    }
    // Positive: that many certificates of the list were not understood (a
    // key type this mbedtls lacks); the others are used.
    ret = mbedtls_x509_crt_parse(&t->ca, (const unsigned char*)caText, caSize);
    free(caText);
    if (ret < 0 || t->ca.version == 0)
    {
        Say(detail, detailSize, "the CA list could not be parsed: -0x%04x", (unsigned)-ret);
        return k3DSNetCert;
    }

    ret = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret == 0)
    {
        // The chain, the name and the dates: always.
        mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&t->conf, &t->ca, NULL);
        mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->drbg);
        // TLS 1.2, what GitHub's servers speak with this mbedtls.
        mbedtls_ssl_conf_min_version(&t->conf, MBEDTLS_SSL_MAJOR_VERSION_3,
                                     MBEDTLS_SSL_MINOR_VERSION_3);
        ret = mbedtls_ssl_setup(&t->ssl, &t->conf);
    }
    if (ret == 0)
    {
        ret = mbedtls_ssl_set_hostname(&t->ssl, host);
    }
    if (ret != 0)
    {
        Say(detail, detailSize, "setting up TLS: -0x%04x", (unsigned)-ret);
        return k3DSNetTls;
    }
    mbedtls_ssl_set_bio(&t->ssl, t, SendCb, RecvCb, NULL);

    const uint64_t start = NowMs();
    while ((ret = mbedtls_ssl_handshake(&t->ssl)) != 0)
    {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            return HandshakeError(t, ret, host, detail, detailSize);
        }
        const int why = Nap(t, start);
        if (why != k3DSNetOk)
        {
            Say(detail, detailSize, "handshake with %s stopped", host);
            return why;
        }
    }
    printf("tssm: update: 3ds: %s: %s, %s, in %llu ms\n", host, mbedtls_ssl_get_version(&t->ssl),
           mbedtls_ssl_get_ciphersuite(&t->ssl), (unsigned long long)(NowMs() - start));
    return k3DSNetOk;
}

int i3DSTlsWrite(i3DSTls* t, const void* data, size_t n)
{
    const unsigned char* p = (const unsigned char*)data;
    while (n > 0)
    {
        t->why = k3DSNetOk;
        const int w = mbedtls_ssl_write(&t->ssl, p, n);
        if (w > 0)
        {
            p += w;
            n -= (size_t)w;
            continue;
        }
        if (w == MBEDTLS_ERR_SSL_WANT_READ || w == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            continue;
        }
        if (w == MBEDTLS_ERR_NET_SEND_FAILED && t->why != k3DSNetOk)
        {
            return t->why;
        }
        printf("tssm: update: 3ds: write -0x%04x\n", (unsigned)-w);
        return k3DSNetLost;
    }
    return k3DSNetOk;
}

int i3DSTlsRead(i3DSTls* t, void* data, size_t n, size_t* got)
{
    *got = 0;
    while (true)
    {
        t->why = k3DSNetOk;
        const int r = mbedtls_ssl_read(&t->ssl, (unsigned char*)data, n);
        if (r > 0)
        {
            *got = (size_t)r;
            return k3DSNetOk;
        }
        // The end: close_notify, or the server closing without it (GitHub's
        // do after "Connection: close"; the core knows each body's length).
        if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || r == MBEDTLS_ERR_SSL_CONN_EOF)
        {
            return k3DSNetOk;
        }
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            continue;
        }
        if (r == MBEDTLS_ERR_NET_RECV_FAILED && t->why != k3DSNetOk)
        {
            return t->why;
        }
        printf("tssm: update: 3ds: read -0x%04x\n", (unsigned)-r);
        return k3DSNetLost;
    }
}

void i3DSTlsClose(i3DSTls* t)
{
    if (t == NULL)
    {
        return;
    }
    if (t->mbed)
    {
        mbedtls_ssl_free(&t->ssl);
        mbedtls_ssl_config_free(&t->conf);
        mbedtls_x509_crt_free(&t->ca);
        mbedtls_ctr_drbg_free(&t->drbg);
        mbedtls_entropy_free(&t->entropy);
    }
    if (t->fd >= 0)
    {
        close(t->fd);
    }
    if (t->sslc)
    {
        sslcExit();
    }
    free(t);
}

// ------------------------------------------------------------------ this copy

int i3DSUpdateSelfPath(char* out, size_t outSize)
{
    const char* argv0 = __system_argc > 0 && __system_argv != NULL ? __system_argv[0] : NULL;
#ifdef TSSM_UPDATE_TEST_BUILD
    // An emulator that boots the .3dsx itself gives no argv[0]; a test run
    // names the file instead (test-env.txt). Test builds only.
    const char* test = getenv("TSSM_TEST_UPDATE_SELF");
    if (argv0 == NULL && test != NULL && test[0] != '\0')
    {
        printf("tssm: update: 3ds: no argv[0]; TSSM_TEST_UPDATE_SELF names %s\n", test);
        argv0 = test;
    }
#endif
    const size_t n = argv0 != NULL ? strlen(argv0) : 0;
    struct stat st;
    if (argv0 == NULL || strncmp(argv0, "sdmc:/", 6) != 0 || n < 12 || n >= outSize ||
        strcasecmp(argv0 + n - 5, ".3dsx") != 0 || stat(argv0, &st) != 0 || !S_ISREG(st.st_mode))
    {
        printf("tssm: update: 3ds: started as \"%s\", not a .3dsx on the SD card\n",
               argv0 != NULL ? argv0 : "(no argv[0])");
        return 0;
    }
    memcpy(out, argv0, n + 1);
    return 1;
}

int i3DSUpdateReadOwnExefs(const char* name, uint8_t** data, size_t* size)
{
    *data = NULL;
    *size = 0;
    // SelfNCCH's file path: a type word (2: the ExeFS) and the ExeFS name.
    struct
    {
        u32 type;
        char name[8];
    } path;
    memset(&path, 0, sizeof(path));
    path.type = 2;
    strncpy(path.name, name, sizeof(path.name));
    const FS_Path archive = { PATH_EMPTY, 1, "" };
    const FS_Path file = { PATH_BINARY, sizeof(path), &path };
    Handle h = 0;
    Result rc = FSUSER_OpenFileDirectly(&h, ARCHIVE_ROMFS, archive, file, FS_OPEN_READ, 0);
    if (R_FAILED(rc))
    {
        printf("tssm: update: 3ds: this title's ExeFS %s: %#lx\n", name, (unsigned long)rc);
        return 0;
    }
    u64 n = 0;
    rc = FSFILE_GetSize(h, &n);
    if (R_FAILED(rc) || n == 0 || n > 4 * 1024 * 1024)
    {
        printf("tssm: update: 3ds: this title's ExeFS %s: size %llu (%#lx)\n", name,
               (unsigned long long)n, (unsigned long)rc);
        FSFILE_Close(h);
        return 0;
    }
    uint8_t* buf = (uint8_t*)malloc((size_t)n);
    u32 got = 0;
    rc = buf != NULL ? FSFILE_Read(h, &got, 0, buf, (u32)n) : -1;
    FSFILE_Close(h);
    if (R_FAILED(rc) || got != n)
    {
        printf("tssm: update: 3ds: this title's ExeFS %s: read %lu of %llu (%#lx)\n", name,
               (unsigned long)got, (unsigned long long)n, (unsigned long)rc);
        free(buf);
        return 0;
    }
    *data = buf;
    *size = (size_t)n;
    return 1;
}

// --------------------------------------------------------------- the install

int i3DSUpdateInstallCia(const char* path, uint64_t titleId, uint16_t version,
                         uint16_t* versionOut, char* why, size_t whySize)
{
    *versionOut = 0;
    if (strncmp(path, "sdmc:/", 6) != 0)
    {
        Say(why, whySize,
            "The download is not on the SD card. The installed version is unchanged.");
        return 0;
    }
    Result rc = amInit();
    if (R_FAILED(rc))
    {
        printf("tssm: update: 3ds: amInit %#lx\n", (unsigned long)rc);
        Say(why, whySize,
            "The system's title installer could not be reached (%#lx). The installed version is "
            "unchanged.",
            (unsigned long)rc);
        return 0;
    }
    FS_Archive sd = 0;
    Handle file = 0;
    rc = FSUSER_OpenArchive(&sd, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
    if (R_SUCCEEDED(rc))
    {
        rc = FSUSER_OpenFile(&file, sd, fsMakePath(PATH_ASCII, path + 5), FS_OPEN_READ, 0);
    }
    u64 size = 0;
    if (R_SUCCEEDED(rc))
    {
        rc = FSFILE_GetSize(file, &size);
    }
    if (R_FAILED(rc))
    {
        printf("tssm: update: 3ds: opening %s: %#lx\n", path, (unsigned long)rc);
        Say(why, whySize,
            "The download could not be opened again. The installed version is "
            "unchanged.");
        if (file != 0)
        {
            FSFILE_Close(file);
        }
        if (sd != 0)
        {
            FSUSER_CloseArchive(sd);
        }
        amExit();
        return 0;
    }

    int ok = 0;
    // AM's own reading of the file: the same title as this one, and the
    // version the file's header and name say.
    AM_TitleInfo info;
    memset(&info, 0, sizeof(info));
    rc = AM_GetCiaFileInfo(MEDIATYPE_SD, &info, file);
    u64 need = 0;
    const Result needRc = R_SUCCEEDED(rc) ? AM_GetCiaRequiredSpace(&need, MEDIATYPE_SD, file) : rc;
    printf("tssm: update: 3ds: AM reads the download as title %016llx version %u (%#lx), "
           "%llu bytes to install (%#lx)\n",
           (unsigned long long)info.titleID, (unsigned)info.version, (unsigned long)rc,
           (unsigned long long)need, (unsigned long)needRc);
    struct statvfs vfs;
    const unsigned long long freeBytes =
        statvfs("sdmc:/", &vfs) == 0 ? (unsigned long long)vfs.f_bavail * vfs.f_frsize : 0;
    if (R_FAILED(rc))
    {
        Say(why, whySize,
            "The system did not accept the download as a CIA (%#lx). The installed "
            "version is unchanged.",
            (unsigned long)rc);
    }
    else if (info.titleID != titleId)
    {
        Say(why, whySize,
            "The download is another title (%016llx), not this one (%016llx), so it "
            "was not installed. The installed version is unchanged.",
            (unsigned long long)info.titleID, (unsigned long long)titleId);
    }
    else if (info.version != version)
    {
        Say(why, whySize,
            "The system reads the download as another version (%u, not %u), so it was not "
            "installed. The installed version is unchanged.",
            (unsigned)info.version, (unsigned)version);
    }
    else if (R_SUCCEEDED(needRc) && freeBytes != 0 && need + 1024 * 1024 > freeBytes)
    {
        Say(why, whySize,
            "Not enough free space on the SD card to install it (it needs %.1f MB). "
            "The installed version is unchanged.",
            (double)need / (1024.0 * 1024.0));
    }
    else
    {
        const u32 kChunk = 128 * 1024;
        u8* buf = (u8*)malloc(kChunk);
        Handle cia = 0;
        // HOME and sleep wait while AM writes and commits: a few seconds.
        i3DSHoldHome(1);
        rc = buf != NULL ? AM_StartCiaInstall(MEDIATYPE_SD, &cia) : -1;
        if (R_FAILED(rc))
        {
            printf("tssm: update: 3ds: AM_StartCiaInstall %#lx\n", (unsigned long)rc);
            Say(why, whySize,
                "The system's title installer did not start (%#lx). The installed "
                "version is unchanged.",
                (unsigned long)rc);
        }
        else
        {
            u64 off = 0;
            while (off < size && R_SUCCEEDED(rc))
            {
                const u32 want = size - off < kChunk ? (u32)(size - off) : kChunk;
                u32 got = 0;
                u32 wrote = 0;
                rc = FSFILE_Read(file, &got, off, buf, want);
                if (R_SUCCEEDED(rc) && got != want)
                {
                    rc = -1;
                }
                if (R_SUCCEEDED(rc))
                {
                    rc = FSFILE_Write(cia, &wrote, off, buf, got, 0);
                }
                if (R_SUCCEEDED(rc) && wrote != got)
                {
                    rc = -1;
                }
                off += got;
            }
            if (R_FAILED(rc))
            {
                printf("tssm: update: 3ds: writing to AM at %llu: %#lx\n", (unsigned long long)off,
                       (unsigned long)rc);
                AM_CancelCIAInstall(cia);
                Say(why, whySize,
                    "Installing stopped partway (%#lx). The installed version is "
                    "unchanged.",
                    (unsigned long)rc);
            }
            else
            {
                rc = AM_FinishCiaInstall(cia);
                if (R_FAILED(rc))
                {
                    printf("tssm: update: 3ds: AM_FinishCiaInstall %#lx\n", (unsigned long)rc);
                    Say(why, whySize,
                        "The system did not finish the install (%#lx). The "
                        "installed version is unchanged.",
                        (unsigned long)rc);
                }
                else
                {
                    ok = 1;
                }
            }
        }
        i3DSHoldHome(0);
        free(buf);
    }
    FSFILE_Close(file);
    FSUSER_CloseArchive(sd);

    if (ok)
    {
        u64 id = titleId;
        AM_TitleInfo after;
        memset(&after, 0, sizeof(after));
        rc = AM_GetTitleInfo(MEDIATYPE_SD, 1, &id, &after);
        if (R_SUCCEEDED(rc))
        {
            *versionOut = after.version;
        }
        printf("tssm: update: 3ds: installed; the title is now version %u (%#lx)\n",
               (unsigned)after.version, (unsigned long)rc);
    }
    amExit();
    return ok;
}

int i3DSUpdateRestartAfterQuit(void)
{
    // libctru's aptExit jumps to this title again instead of closing.
    aptSetChainloaderToSelf();
    printf("tssm: update: 3ds: the game starts again after the quit (chainload to self)\n");
    return 1;
}

// ------------------------------------------------------------ a request's thread

namespace
{
    struct StackRun
    {
        void (*fn)(void*);
        void* arg;
        size_t size;
        size_t used; // the deepest the pattern was overwritten, in bytes
        bool all; // overwritten down to its bottom: the stack may have run out
    };

    const u32 kStackPattern = 0x7A3C5E91u;
    // Left alone at each end: the start of the thread's own frames at the
    // top, and room at the bottom for what the thread's start used above it.
    const size_t kStackMargin = 1024;

    __attribute__((noinline)) void StackEntry(void* p)
    {
        StackRun* run = (StackRun*)p;
        // libctru's threadCreate puts the stack's `size` bytes right below the
        // address the thread starts at, so all of it from a little below this
        // frame to a little above that bottom is unused yet.
        volatile u32 here = 0;
        const uintptr_t sp = (uintptr_t)&here;
        volatile u32* const top = (volatile u32*)((sp - kStackMargin) & ~(uintptr_t)3);
        volatile u32* const bottom =
            (volatile u32*)((sp - run->size + kStackMargin + 3) & ~(uintptr_t)3);
        for (volatile u32* w = bottom; w < top; w++)
        {
            *w = kStackPattern;
        }
        run->fn(run->arg);
        volatile u32* w = bottom;
        while (w < top && *w == kStackPattern)
        {
            w++;
        }
        run->used = (size_t)(sp - (uintptr_t)w);
        run->all = w == bottom;
    }
} // namespace

int i3DSUpdateRunOnStack(void (*fn)(void*), void* arg, size_t stackSize, const char* what)
{
    s32 prio = 0x3F;
    if (R_FAILED(svcGetThreadPriority(&prio, CUR_THREAD_HANDLE)) || prio < 0x18 || prio > 0x3F)
    {
        prio = 0x3F;
    }
    StackRun run = { fn, arg, stackSize, 0, false };
    Thread t = threadCreate(StackEntry, &run, stackSize, prio, svcGetProcessorID(), false);
    if (t == NULL)
    {
        printf("tssm: update: 3ds: %s: no thread with a %u KB stack\n", what,
               (unsigned)(stackSize >> 10));
        return 0;
    }
    threadJoin(t, U64_MAX);
    threadFree(t);
    const struct mallinfo mi = mallinfo();
    printf("tssm: update: 3ds: %s: stack %u of %u KB used%s; malloc %lu KB in use of the %lu KB "
           "heap, linear heap %lu KB free\n",
           what, (unsigned)((run.used + 1023) >> 10), (unsigned)(stackSize >> 10),
           run.all ? " (ALL of it: it may have run out)" : "", (unsigned long)(mi.uordblks >> 10),
           (unsigned long)(envGetHeapSize() >> 10), (unsigned long)(linearSpaceFree() >> 10));
    return 1;
}
