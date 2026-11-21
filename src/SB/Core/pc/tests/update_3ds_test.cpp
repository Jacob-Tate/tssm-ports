// tssm_update3dstest: the 3DS updater's reading of its two package formats
// (3ds/i3DSPackage.h) on the PC: a .cia's header and TMD (the title the
// install checks against this one), a .3dsx's header (where its SMDH is), and
// the title version a TSSM_3DS_VERSION becomes (tools/3ds/cia_version.py
// writes the same number); and what a connect() call's answer means
// (i3DSConnectState, 3ds/i3DS.h). Made-up files of the real layouts, and,
// when given, real ones:
//   tssm_update3dstest [<a .cia> <its title ID, hex> <its version, a.b.c>] [<a .3dsx>]

#include "3ds/i3DS.h"
#include "3ds/i3DSPackage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace
{
    int sChecks;
    int sFailed;

    void Check(bool ok, const char* what)
    {
        sChecks++;
        if (!ok)
        {
            sFailed++;
            printf("FAIL: %s\n", what);
        }
    }

    void PutLe32(std::vector<uint8_t>& b, size_t at, uint32_t v)
    {
        b[at] = (uint8_t)v;
        b[at + 1] = (uint8_t)(v >> 8);
        b[at + 2] = (uint8_t)(v >> 16);
        b[at + 3] = (uint8_t)(v >> 24);
    }

    void PutLe64(std::vector<uint8_t>& b, size_t at, uint64_t v)
    {
        PutLe32(b, at, (uint32_t)v);
        PutLe32(b, at + 4, (uint32_t)(v >> 32));
    }

    void PutBe(std::vector<uint8_t>& b, size_t at, uint64_t v, int bytes)
    {
        for (int i = 0; i < bytes; i++)
        {
            b[at + i] = (uint8_t)(v >> (8 * (bytes - 1 - i)));
        }
    }

    size_t Align64(size_t n)
    {
        return (n + 63) & ~(size_t)63;
    }

    // The head of a .cia as makerom lays one out: a 0x2020-byte header, the
    // certificate chain, a ticket, a TMD signed RSA-2048 with one content.
    struct FakeCia
    {
        std::vector<uint8_t> head;
        uint64_t fileSize;
        size_t tmdAt;
    };

    FakeCia MakeCia(uint64_t titleId, uint16_t version, uint64_t contentSize, uint32_t metaSize)
    {
        const uint32_t certs = 0xA00;
        const uint32_t ticket = 0x350;
        const uint32_t tmd = 0x140 + 0xC4 + 0x24 * 64 + 0x30;
        FakeCia c;
        c.tmdAt = Align64(0x2020) + Align64(certs) + Align64(ticket);
        c.head.assign(c.tmdAt + Align64(tmd), 0);
        PutLe32(c.head, 0x00, 0x2020);
        PutLe32(c.head, 0x08, certs);
        PutLe32(c.head, 0x0C, ticket);
        PutLe32(c.head, 0x10, tmd);
        PutLe32(c.head, 0x14, metaSize);
        PutLe64(c.head, 0x18, contentSize);
        PutBe(c.head, c.tmdAt, 0x10004, 4);
        PutBe(c.head, c.tmdAt + 0x140 + 0x4C, titleId, 8);
        PutBe(c.head, c.tmdAt + 0x140 + 0x9C, version, 2);
        PutBe(c.head, c.tmdAt + 0x140 + 0x9E, 1, 2);
        c.fileSize = c.head.size() + Align64(contentSize) + Align64(metaSize);
        return c;
    }

    void TestCia()
    {
        char why[200];
        i3DSCiaInfo info;

        FakeCia c = MakeCia(0x000400000F755300ull, 145, 0x3D4000, 0x3AC0);
        Check(i3DSParseCia(c.head.data(), c.head.size(), c.fileSize, &info, why, sizeof(why)) == 1,
              "a well-formed .cia is read");
        Check(info.titleId == 0x000400000F755300ull, "its title ID");
        Check(info.version == 145, "its version");
        Check(info.contentCount == 1, "its content count");
        Check(info.contentSize == 0x3D4000, "its content size");

        FakeCia test = MakeCia(0x000400000F755C00ull, 129, 0x1000, 0);
        Check(i3DSParseCia(test.head.data(), test.head.size(), test.fileSize, &info, why,
                           sizeof(why)) == 1 &&
                  info.titleId == 0x000400000F755C00ull,
              "a test title's ID is its own, not the game's");

        Check(i3DSParseCia(c.head.data(), c.head.size(), c.fileSize + 64, &info, why,
                           sizeof(why)) == 0,
              "a file longer than its sections is refused");
        Check(i3DSParseCia(c.head.data(), c.head.size(), c.fileSize - 64, &info, why,
                           sizeof(why)) == 0,
              "a cut-off file is refused");
        Check(i3DSParseCia(c.head.data(), c.tmdAt + 0x100, c.fileSize, &info, why, sizeof(why)) ==
                  0,
              "a TMD past what was read is refused");
        Check(i3DSParseCia(c.head.data(), 0x10, c.fileSize, &info, why, sizeof(why)) == 0,
              "a few bytes are refused");
        Check(i3DSParseCia(NULL, 0, 0, &info, why, sizeof(why)) == 0, "nothing is refused");

        FakeCia bad = c;
        PutLe32(bad.head, 0x00, 0x2000);
        Check(i3DSParseCia(bad.head.data(), bad.head.size(), bad.fileSize, &info, why,
                           sizeof(why)) == 0,
              "another header size is refused");
        bad = c;
        PutBe(bad.head, bad.tmdAt, 0x12345, 4);
        Check(i3DSParseCia(bad.head.data(), bad.head.size(), bad.fileSize, &info, why,
                           sizeof(why)) == 0,
              "a TMD with an unknown signature type is refused");
        bad = c;
        PutBe(bad.head, bad.tmdAt + 0x140 + 0x9E, 0, 2);
        Check(i3DSParseCia(bad.head.data(), bad.head.size(), bad.fileSize, &info, why,
                           sizeof(why)) == 0,
              "a TMD with no content is refused");
        bad = c;
        PutBe(bad.head, bad.tmdAt + 0x140 + 0x9E, 500, 2);
        Check(i3DSParseCia(bad.head.data(), bad.head.size(), bad.fileSize, &info, why,
                           sizeof(why)) == 0,
              "a TMD listing more contents than it holds is refused");
        bad = c;
        PutLe64(bad.head, 0x18, 0);
        Check(i3DSParseCia(bad.head.data(), bad.head.size(), bad.fileSize, &info, why,
                           sizeof(why)) == 0,
              "a .cia without content is refused");

        // A .3dsx is not a .cia.
        std::vector<uint8_t> x(64, 0);
        memcpy(x.data(), "3DSX", 4);
        Check(i3DSParseCia(x.data(), x.size(), 1 << 20, &info, why, sizeof(why)) == 0,
              "a .3dsx is not read as a .cia");
    }

    std::vector<uint8_t> Make3dsx(uint16_t headerSize, uint32_t smdhAt, uint32_t smdhSize,
                                  uint32_t romfsAt)
    {
        std::vector<uint8_t> b(64, 0);
        memcpy(b.data(), "3DSX", 4);
        b[4] = (uint8_t)headerSize;
        b[5] = (uint8_t)(headerSize >> 8);
        PutLe32(b, 32, smdhAt);
        PutLe32(b, 36, smdhSize);
        PutLe32(b, 40, romfsAt);
        return b;
    }

    void Test3dsx()
    {
        char why[200];
        i3DS3dsxInfo info;
        const uint32_t smdhAt = 6226492;
        const uint32_t romfsAt = smdhAt + 0x36C0;
        const uint64_t size = 6280468;

        std::vector<uint8_t> h = Make3dsx(44, smdhAt, 0x36C0, romfsAt);
        Check(i3DSParse3dsx(h.data(), h.size(), size, &info, why, sizeof(why)) == 1,
              "a well-formed .3dsx is read");
        Check(info.smdhOffset == smdhAt && info.smdhSize == 0x36C0 && info.romfsOffset == romfsAt,
              "its SMDH and RomFS");

        Check(i3DSParse3dsx(h.data(), 40, size, &info, why, sizeof(why)) == 0,
              "a header cut short is refused");
        std::vector<uint8_t> b = Make3dsx(32, 0, 0, 0);
        Check(i3DSParse3dsx(b.data(), b.size(), size, &info, why, sizeof(why)) == 0,
              "a .3dsx without an SMDH is refused");
        b = Make3dsx(44, smdhAt, 0x1000, romfsAt);
        Check(i3DSParse3dsx(b.data(), b.size(), size, &info, why, sizeof(why)) == 0,
              "an SMDH of another size is refused");
        b = Make3dsx(44, smdhAt, 0x36C0, romfsAt);
        Check(i3DSParse3dsx(b.data(), b.size(), smdhAt + 0x100, &info, why, sizeof(why)) == 0,
              "an SMDH past the file's end is refused");
        b = Make3dsx(44, smdhAt, 0x36C0, smdhAt);
        Check(i3DSParse3dsx(b.data(), b.size(), size, &info, why, sizeof(why)) == 0,
              "a RomFS inside the SMDH is refused");
        b = Make3dsx(44, smdhAt, 0x36C0, (uint32_t)size);
        Check(i3DSParse3dsx(b.data(), b.size(), size, &info, why, sizeof(why)) == 0,
              "a RomFS at the file's end is refused");
        b = Make3dsx(44, 20, 0x36C0, 20 + 0x36C0);
        Check(i3DSParse3dsx(b.data(), b.size(), size, &info, why, sizeof(why)) == 0,
              "an SMDH inside the header is refused");
        b = h;
        memcpy(b.data(), "NRO0", 4);
        Check(i3DSParse3dsx(b.data(), b.size(), size, &info, why, sizeof(why)) == 0,
              "another program format is refused");
    }

    void TestVersions()
    {
        struct Case
        {
            const char* text;
            int ok;
            uint16_t value;
        };
        static const Case kCases[] = {
            { "0.9.1", 1, 145 }, { "0.9", 1, 144 },  { "0.10", 1, 160 },   { "0.8", 1, 128 },
            { "0.8.1", 1, 129 }, { "1", 1, 1024 },   { "1.2.3", 1, 1059 }, { "63.63.15", 1, 65535 },
            { "0.0.0", 1, 0 },   { "64.0.0", 0, 0 }, { "0.64", 0, 0 },     { "0.0.16", 0, 0 },
            { "1.2.3.4", 0, 0 }, { "", 0, 0 },       { "1..2", 0, 0 },     { "a", 0, 0 },
            { "1.", 0, 0 },      { ".1", 0, 0 },     { "0.9.1 ", 0, 0 },   { "-1", 0, 0 },
            { "0009.1", 0, 0 },
        };
        for (const Case& c : kCases)
        {
            uint16_t v = 0;
            const int ok = i3DSTitleVersion(c.text, &v);
            char what[96];
            snprintf(what, sizeof(what), "title version \"%s\"", c.text);
            Check(ok == c.ok && (!ok || v == c.value), what);
        }
        // Each release after the last is a bigger number, the way AM compares.
        uint16_t a = 0, b = 0, c = 0, d = 0;
        Check(i3DSTitleVersion("0.9", &a) && i3DSTitleVersion("0.9.1", &b) &&
                  i3DSTitleVersion("0.9.2", &c) && i3DSTitleVersion("0.10", &d) && a < b && b < c &&
                  c < d,
              "0.9 < 0.9.1 < 0.9.2 < 0.10");
        char text[24];
        i3DSTitleVersionText(145, text, sizeof(text));
        Check(strcmp(text, "0.9.1") == 0, "145 reads 0.9.1");
        i3DSTitleVersionText(65535, text, sizeof(text));
        Check(strcmp(text, "63.63.15") == 0, "65535 reads 63.63.15");

        // A download's title version against the version in its file name.
        Check(i3DSCiaVersionIs(146, "0.9.2"), "146 is 0.9.2");
        Check(i3DSCiaVersionIs(148, "0.9.4"), "148 is 0.9.4");
        Check(i3DSCiaVersionIs(149, "0.9.5"), "149 is 0.9.5");
        Check(i3DSCiaVersionIs(144, "0.9"), "144 is 0.9");
        Check(i3DSCiaVersionIs(129, "0.8.1"), "129 is 0.8.1");
        Check(!i3DSCiaVersionIs(1, "0.9.1"), "makerom's 0.0.1 is not 0.9.1");
        Check(!i3DSCiaVersionIs(3, "0.9.3"), "a .cia without cia_version.py's fix is refused");
        Check(!i3DSCiaVersionIs(145, "0.9.2"), "an older title version is refused");
        Check(!i3DSCiaVersionIs(147, "0.9.2"), "a newer title version is refused");
        Check(!i3DSCiaVersionIs(144, "0.9.0.1"), "four numbers are no title version");
        Check(!i3DSCiaVersionIs(0, ""), "an empty name's version is refused");
    }

    // What one connect() call said, read the way the updater's and 3dslink's
    // connects read the second one (i3DSConnectState).
    void TestConnectState()
    {
        struct Case
        {
            int rc;
            int err;
            int state;
            const char* what;
        };
        static const Case kCases[] = {
            { 0, 0, k3DSConnectUp, "connect() 0: up" },
            { -1, EISCONN, k3DSConnectUp, "EISCONN (a console's second connect): up" },
            { -1, EINPROGRESS, k3DSConnectWaiting, "EINPROGRESS: waiting" },
            { -1, EALREADY, k3DSConnectWaiting, "EALREADY: waiting" },
            { -1, EAGAIN, k3DSConnectWaiting, "EAGAIN: waiting" },
            { -1, EWOULDBLOCK, k3DSConnectWaiting, "EWOULDBLOCK: waiting" },
            { -1, -26, k3DSConnectWaiting, "the SOC service's -26 (EINPROGRESS): waiting, not failed" },
            { -1, -7, k3DSConnectWaiting, "the SOC service's -7 (EALREADY): waiting" },
            { -1, -6, k3DSConnectWaiting, "the SOC service's -6 (EAGAIN): waiting" },
            { -1, -30, k3DSConnectUp, "the SOC service's -30 (EISCONN): up" },
            { -1, ECONNREFUSED, k3DSConnectFailed, "ECONNREFUSED: failed" },
            { -1, EHOSTUNREACH, k3DSConnectFailed, "EHOSTUNREACH: failed" },
            { -1, ENETUNREACH, k3DSConnectFailed, "ENETUNREACH: failed" },
            { -1, ETIMEDOUT, k3DSConnectFailed, "ETIMEDOUT: failed" },
            { -1, -14, k3DSConnectFailed, "the SOC service's -14 (ECONNREFUSED): failed" },
            { -1, ENODEV, k3DSConnectFailed, "ENODEV (libctru: the SOC request failed): failed" },
            { -1, 0, k3DSConnectFailed, "-1 without an errno: failed" },
        };
        for (const Case& c : kCases)
        {
            Check(i3DSConnectState(c.rc, c.err) == c.state, c.what);
        }
        // The console's connect to GitHub in 0.9.2: EINPROGRESS, poll counts
        // the socket writable, SO_ERROR says -26 (not asked any more), and
        // the second connect() says EISCONN.
        Check(i3DSConnectState(-1, EINPROGRESS) == k3DSConnectWaiting &&
                  i3DSConnectState(-1, EISCONN) == k3DSConnectUp,
              "EINPROGRESS, then EISCONN: up");
    }

    bool ReadFile(const char* path, std::vector<uint8_t>* head, uint64_t* size)
    {
        FILE* f = fopen(path, "rb");
        if (f == NULL)
        {
            return false;
        }
        fseek(f, 0, SEEK_END);
        *size = (uint64_t)ftell(f);
        fseek(f, 0, SEEK_SET);
        head->assign(64 * 1024, 0);
        head->resize(fread(head->data(), 1, head->size(), f));
        fclose(f);
        return true;
    }

    void TestRealCia(const char* path, const char* tidText, const char* versionText)
    {
        std::vector<uint8_t> head;
        uint64_t size = 0;
        char what[300];
        snprintf(what, sizeof(what), "%s can be read", path);
        Check(ReadFile(path, &head, &size), what);
        i3DSCiaInfo info;
        char why[200] = "";
        const int ok = i3DSParseCia(head.data(), head.size(), size, &info, why, sizeof(why));
        snprintf(what, sizeof(what), "%s is a .cia (%s)", path, why);
        Check(ok == 1, what);
        const uint64_t tid = strtoull(tidText, NULL, 16);
        uint16_t version = 0;
        snprintf(what, sizeof(what), "%s is title %016llx (it says %016llx)", path,
                 (unsigned long long)tid, (unsigned long long)info.titleId);
        Check(info.titleId == tid, what);
        char got[24];
        i3DSTitleVersionText(info.version, got, sizeof(got));
        snprintf(what, sizeof(what), "%s is version %s (it says %s)", path, versionText, got);
        Check(i3DSTitleVersion(versionText, &version) && info.version == version, what);
        printf("%s: title %016llx, version %s, %u content(s), %llu bytes\n", path,
               (unsigned long long)info.titleId, got, info.contentCount, (unsigned long long)size);
    }

    void TestReal3dsx(const char* path)
    {
        std::vector<uint8_t> head;
        uint64_t size = 0;
        char what[300];
        snprintf(what, sizeof(what), "%s can be read", path);
        Check(ReadFile(path, &head, &size), what);
        i3DS3dsxInfo info;
        char why[200] = "";
        snprintf(what, sizeof(what), "%s is a .3dsx", path);
        Check(i3DSParse3dsx(head.data(), head.size(), size, &info, why, sizeof(why)) == 1, what);
        printf("%s: SMDH %u bytes at %u, RomFS at %u, %llu bytes\n", path, info.smdhSize,
               info.smdhOffset, info.romfsOffset, (unsigned long long)size);
    }
} // namespace

int main(int argc, char** argv)
{
    TestCia();
    Test3dsx();
    TestVersions();
    TestConnectState();
    int a = 1;
    if (argc >= a + 3 && strstr(argv[a], ".cia") != NULL)
    {
        TestRealCia(argv[a], argv[a + 1], argv[a + 2]);
        a += 3;
    }
    if (argc > a)
    {
        TestReal3dsx(argv[a]);
    }
    printf("tssm_update3dstest: %d checks, %d failed\n", sChecks, sFailed);
    return sFailed == 0 ? 0 : 1;
}
