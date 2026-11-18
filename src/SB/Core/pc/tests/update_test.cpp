// tssm_updatetest: the updater's core (iUpdate.cpp) on the PC, with a stand-in
// for the network.
//
// What it checks:
//   1. versions: parsing, and 0.9 < 0.9.1 < 0.10;
//   2. the release's JSON, from GitHub's API as saved in tests/update/
//      (tools/update_fixtures.py): the 0.9 release, and 0.9.1, which ships the
//      Switch's and Android's files unchanged under their 0.9 names;
//   3. each platform's file chosen from those (Switch, 3DS .cia and .3dsx,
//      Android and Android 32-bit);
//   4. JSON that is wrong in every way the parser has a branch for, and the
//      real fixture cut short at many points: refused, never a crash;
//   5. SHA256SUMS: the real files, CR LF, '*', a missing line, two lines that
//      disagree;
//   6. SHA-256 against its published test vectors;
//   7. URLs: the four hosts and nothing else;
//   8. the HTTP/1.1 client: the request it sends (nothing but Host,
//      User-Agent, Accept, Connection), Content-Length, chunked, redirects,
//      1xx, a cut-off body, bad framing, a cancel;
//   9. whole checks and downloads through a stand-in platform: up to date,
//      available, installed, and every way one is refused (404, rate limit,
//      bad JSON, no checksum file or line, checksums that disagree, a hash
//      mismatch, a redirect off the allowed hosts, a redirect loop, a file too
//      large or the wrong size, the network down, the install failing,
//      cancels) -- each leaving the installed file as it was.
//
// Run: tssm_updatetest <fixture folder> [scratch folder]

#include "iUpdate.h"

#include <algorithm>
#include <string>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iHost.h"

static int sChecks;
static int sFailures;

#define CHECK(cond, ...)                                                                      \
    do                                                                                        \
    {                                                                                         \
        sChecks++;                                                                            \
        if (!(cond))                                                                          \
        {                                                                                     \
            sFailures++;                                                                      \
            printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);                            \
            printf(__VA_ARGS__);                                                              \
            printf("\n");                                                                     \
        }                                                                                     \
    } while (0)

static std::string sFixtures;
static std::string sScratch;

static bool ReadFile(const std::string& path, std::string* out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (f == NULL)
    {
        return false;
    }
    out->clear();
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        out->append(buf, n);
    }
    fclose(f);
    return true;
}

static bool WriteFile(const std::string& path, const std::string& data)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (f == NULL)
    {
        return false;
    }
    const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    return fclose(f) == 0 && ok;
}

static std::string Fixture(const char* name)
{
    std::string data;
    if (!ReadFile(sFixtures + "/" + name, &data))
    {
        printf("cannot read fixture %s/%s\n", sFixtures.c_str(), name);
        exit(2);
    }
    return data;
}

static std::string Replace(std::string s, const std::string& from, const std::string& to)
{
    size_t at = 0;
    while ((at = s.find(from, at)) != std::string::npos)
    {
        s.replace(at, from.size(), to);
        at += to.size();
    }
    return s;
}

static std::string ToCrLf(const std::string& s)
{
    return Replace(Replace(s, "\r\n", "\n"), "\n", "\r\n");
}

static std::string Sha(const std::string& data)
{
    iUpdateSha256 sha;
    sha.Update(data.data(), data.size());
    uint8_t d[32];
    sha.Final(d);
    char hex[65];
    iUpdateHex(d, 32, hex, sizeof(hex));
    return hex;
}

static iUpdateVersion V(const char* text)
{
    iUpdateVersion v;
    if (!iUpdateParseVersion(text, strlen(text), &v))
    {
        memset(&v, 0, sizeof(v));
    }
    return v;
}

// --- 1. versions --------------------------------------------------------------------------

static void TestVersions()
{
    const char* good[] = { "0.9", "0.9.1", "0.10", "1", "1.2.3.4", "0", "999999999.0" };
    for (const char* g : good)
    {
        iUpdateVersion v;
        CHECK(iUpdateParseVersion(g, strlen(g), &v), "\"%s\" should parse", g);
        char back[32];
        iUpdateFormatVersion(v, back, sizeof(back));
        CHECK(strcmp(back, g) == 0, "\"%s\" formats back as \"%s\"", g, back);
    }
    const char* bad[] = { "",     ".",       "0.",        ".9",         "0..9",   "a",
                          "0.9a", "0.9-rc1", "1.2.3.4.5", "1234567890", "-1",     " 0.9",
                          "0.9 ", "v0.9",    "0,9",       "1.2.3.4.",   "0x10" };
    for (const char* b : bad)
    {
        iUpdateVersion v;
        CHECK(!iUpdateParseVersion(b, strlen(b), &v), "\"%s\" should not parse", b);
    }

    CHECK(iUpdateCompareVersions(V("0.9"), V("0.9.1")) < 0, "0.9 < 0.9.1");
    CHECK(iUpdateCompareVersions(V("0.9.1"), V("0.10")) < 0, "0.9.1 < 0.10");
    CHECK(iUpdateCompareVersions(V("0.9"), V("0.10")) < 0, "0.9 < 0.10");
    CHECK(iUpdateCompareVersions(V("0.10"), V("0.9.9")) > 0, "0.10 > 0.9.9");
    CHECK(iUpdateCompareVersions(V("0.9"), V("0.9.0")) == 0, "0.9 == 0.9.0");
    CHECK(iUpdateCompareVersions(V("1.0"), V("0.10")) > 0, "1.0 > 0.10");
    CHECK(iUpdateCompareVersions(V("0.8"), V("0.9")) < 0, "0.8 < 0.9");
    CHECK(iUpdateCompareVersions(V("0.9.1"), V("0.9.1")) == 0, "0.9.1 == 0.9.1");
}

// --- 2. and 3. the real releases ----------------------------------------------------------

static void FindIn(const iUpdateRelease& r, const char* suffix, const char* name, const char* version)
{
    iUpdateVersion v;
    const int i = iUpdateFindAsset(r, kUpdateAssetPrefix, suffix, &v);
    if (name == NULL)
    {
        CHECK(i < 0, "%s: no file expected in %s, found %s", suffix, r.tag,
              i >= 0 ? r.assets[i].name : "");
        return;
    }
    CHECK(i >= 0 && strcmp(r.assets[i].name, name) == 0, "%s in %s: expected %s, got %s", suffix,
          r.tag, name, i >= 0 ? r.assets[i].name : "(none)");
    char text[32];
    iUpdateFormatVersion(v, text, sizeof(text));
    CHECK(i < 0 || strcmp(text, version) == 0, "%s in %s: version %s, expected %s", suffix, r.tag,
          text, version);
}

static void TestRealReleases()
{
    iUpdateRelease* r = new iUpdateRelease;
    char why[160];

    // 0.9: every platform's file at 0.9.
    std::string json = Fixture("release-dehydrated-0.9.json");
    CHECK(iUpdateParseRelease(json.data(), json.size(), r, why, sizeof(why)), "0.9: %s", why);
    CHECK(strcmp(r->tag, "dehydrated-0.9") == 0, "0.9 tag %s", r->tag);
    CHECK(!r->draft && !r->prerelease, "0.9 flags");
    CHECK(r->assetCount == 9, "0.9 has %d files", r->assetCount);
    FindIn(*r, "-Switch.nro", "TSSM-Dehydrated-0.9-Switch.nro", "0.9");
    FindIn(*r, "-3DS.cia", "TSSM-Dehydrated-0.9-3DS.cia", "0.9");
    FindIn(*r, "-3DS.3dsx", "TSSM-Dehydrated-0.9-3DS.3dsx", "0.9");
    FindIn(*r, "-Android.apk", "TSSM-Dehydrated-0.9-Android.apk", "0.9");
    FindIn(*r, "-Android-32bit.apk", "TSSM-Dehydrated-0.9-Android-32bit.apk", "0.9");
    FindIn(*r, "-Wii.dol", NULL, NULL);
    const int nro = iUpdateFindAssetNamed(*r, "TSSM-Dehydrated-0.9-Switch.nro");
    CHECK(nro >= 0 && r->assets[nro].size == 13225663, "0.9 Switch size");
    CHECK(nro >= 0 && strcmp(r->assets[nro].url,
                             "https://github.com/example-owner/tssm-ports/releases/download/"
                             "dehydrated-0.9/TSSM-Dehydrated-0.9-Switch.nro") == 0,
          "0.9 Switch url %s", nro >= 0 ? r->assets[nro].url : "");
    char hex[65] = "";
    if (nro >= 0)
    {
        iUpdateHex(r->assets[nro].digest, 32, hex, sizeof(hex));
    }
    CHECK(nro >= 0 && r->assets[nro].hasDigest &&
              strcmp(hex, "b8cbc98120f3ffeaa3eb2f4e80fc26237a3bf7340e4810f40dd64ce018e32751") == 0,
          "0.9 Switch digest %s", hex);
    CHECK(iUpdateFindAssetNamed(*r, kUpdateSumsName) >= 0, "0.9 has SHA256SUMS");

    // 0.9.1: new 3DS files; the Switch's and Android's carried over as 0.9.
    json = Fixture("release-dehydrated-0.9.1.json");
    CHECK(iUpdateParseRelease(json.data(), json.size(), r, why, sizeof(why)), "0.9.1: %s", why);
    CHECK(strcmp(r->tag, "dehydrated-0.9.1") == 0, "0.9.1 tag %s", r->tag);
    CHECK(r->assetCount == 8, "0.9.1 has %d files", r->assetCount);
    FindIn(*r, "-Switch.nro", "TSSM-Dehydrated-0.9-Switch.nro", "0.9");
    FindIn(*r, "-3DS.cia", "TSSM-Dehydrated-0.9.1-3DS.cia", "0.9.1");
    FindIn(*r, "-3DS.3dsx", "TSSM-Dehydrated-0.9.1-3DS.3dsx", "0.9.1");
    FindIn(*r, "-Android.apk", "TSSM-Dehydrated-0.9-Android.apk", "0.9");
    FindIn(*r, "-Android-32bit.apk", "TSSM-Dehydrated-0.9-Android-32bit.apk", "0.9");

    // The same with CR LF (a checkout can make it so) and indented differently.
    std::string crlf = ToCrLf(json);
    CHECK(iUpdateParseRelease(crlf.data(), crlf.size(), r, why, sizeof(why)) && r->assetCount == 8,
          "0.9.1 in CR LF: %s", why);

    // Two Switch files in one release: the newer.
    std::string two = Replace(json, "\"name\": \"TSSM-Dehydrated-0.9.1-3DS.cia\"",
                              "\"name\": \"TSSM-Dehydrated-0.10-Switch.nro\"");
    CHECK(iUpdateParseRelease(two.data(), two.size(), r, why, sizeof(why)), "two: %s", why);
    FindIn(*r, "-Switch.nro", "TSSM-Dehydrated-0.10-Switch.nro", "0.10");

    // Names that are near misses are not this platform's file.
    std::string near = Replace(json, "TSSM-Dehydrated-0.9-Switch.nro", "TSSM-Dehydrated-0.9-Switch.nro.zip");
    CHECK(iUpdateParseRelease(near.data(), near.size(), r, why, sizeof(why)), "near: %s", why);
    FindIn(*r, "-Switch.nro", NULL, NULL);
    near = Replace(json, "TSSM-Dehydrated-0.9-Switch.nro", "TSSM-Dehydrated-0.9-beta-Switch.nro");
    CHECK(iUpdateParseRelease(near.data(), near.size(), r, why, sizeof(why)), "near2: %s", why);
    FindIn(*r, "-Switch.nro", NULL, NULL);
    near = Replace(json, "TSSM-Dehydrated-0.9-Switch.nro", "tssm-dehydrated-0.9-switch.nro");
    CHECK(iUpdateParseRelease(near.data(), near.size(), r, why, sizeof(why)), "near3: %s", why);
    FindIn(*r, "-Switch.nro", NULL, NULL);

    // An upload that has not finished is not a file.
    std::string starter = Replace(json, "\"state\": \"uploaded\"", "\"state\": \"starter\"");
    CHECK(iUpdateParseRelease(starter.data(), starter.size(), r, why, sizeof(why)) &&
              r->assetCount == 0,
          "unfinished uploads left out: %d", r->assetCount);

    delete r;
}

// --- 4. JSON that is wrong ----------------------------------------------------------------

static void TestBadJson()
{
    iUpdateRelease* r = new iUpdateRelease;
    char why[160];
    const char* bad[] = {
        "",
        "{",
        "[]",
        "null",
        "\"text\"",
        "{\"tag_name\": }",
        "{\"tag_name\": \"x\",}",
        "{\"tag_name\" \"x\"}",
        "{\"tag_name\": \"x\"} trailing",
        "{\"tag_name\": \"x\"}{}",
        "{\"tag_name\": \"x\", \"assets\": [}",
        "{\"tag_name\": \"x\", \"assets\": [{\"name\": \"a\", \"size\": 01}]}",
        "{\"tag_name\": \"x\", \"assets\": [{\"name\": \"a\", \"size\": 1.}]}",
        "{\"tag_name\": \"x\", \"n\": -}",
        "{\"tag_name\": \"x\", \"n\": tru}",
        "{\"tag_name\": \"a\tb\"}",
        "{\"tag_name\": \"\\x\"}",
        "{\"tag_name\": \"\\u12\"}",
        "{\"tag_name\": \"unterminated}",
        "{\"assets\": []}", // no tag
        "{\"tag_name\": 5}",
        "{\"tag_name\": \"x\" \"draft\": false}",
    };
    for (const char* b : bad)
    {
        CHECK(!iUpdateParseRelease(b, strlen(b), r, why, sizeof(why)), "accepted: %s", b);
    }

    // Deeper than 32: refused, not a stack overflow.
    std::string deep = "{\"tag_name\": \"x\", \"n\": ";
    for (int i = 0; i < 100000; i++)
    {
        deep += "[";
    }
    CHECK(!iUpdateParseRelease(deep.data(), deep.size(), r, why, sizeof(why)), "deep accepted");

    // Bigger than the limit.
    std::string big = "{\"tag_name\": \"x\", \"n\": \"" + std::string(IUPDATE_JSON_MAX, 'a') + "\"}";
    CHECK(!iUpdateParseRelease(big.data(), big.size(), r, why, sizeof(why)), "too big accepted");

    // Fine JSON with fields of the wrong kind or size: parsed, the asset left out.
    const std::string longName(200, 'n');
    const std::string longUrl(600, 'u');
    std::string odd = "{\"tag_name\": \"t\", \"draft\": \"no\", \"assets\": {\"x\": 1}}";
    CHECK(iUpdateParseRelease(odd.data(), odd.size(), r, why, sizeof(why)) && r->assetCount == 0 &&
              !r->draft,
          "assets not an array: %s", why);
    odd = "{\"tag_name\": \"t\", \"assets\": [1, \"s\", null, [], {\"name\": \"" + longName +
          "\", \"size\": 5}, {\"name\": \"a\"}, {\"name\": \"b\", \"size\": \"5\"}, "
          "{\"name\": \"c\", \"size\": -5}, {\"name\": \"d\", \"size\": 5e3}, "
          "{\"name\": \"e\\u0000\", \"size\": 5}, {\"name\": \"\", \"size\": 5}, "
          "{\"name\": \"ok\", \"size\": 7, \"browser_download_url\": \"" + longUrl +
          "\", \"digest\": \"md5:00\", \"uploader\": {\"a\": [1, {\"b\": null}]}}]}";
    CHECK(iUpdateParseRelease(odd.data(), odd.size(), r, why, sizeof(why)), "odd: %s", why);
    CHECK(r->assetCount == 1 && strcmp(r->assets[0].name, "ok") == 0 && r->assets[0].size == 7 &&
              r->assets[0].url[0] == '\0' && !r->assets[0].hasDigest,
          "odd assets: %d", r->assetCount);

    // More assets than the table holds: the first ones kept, the rest ignored.
    std::string many = "{\"tag_name\": \"t\", \"assets\": [";
    for (int i = 0; i < 100; i++)
    {
        char a[96];
        snprintf(a, sizeof(a), "%s{\"name\": \"f%d\", \"size\": %d}", i ? ", " : "", i, i + 1);
        many += a;
    }
    many += "]}";
    CHECK(iUpdateParseRelease(many.data(), many.size(), r, why, sizeof(why)) &&
              r->assetCount == IUPDATE_MAX_ASSETS,
          "many: %d", r->assetCount);

    // The real file cut short anywhere: refused every time.
    const std::string json = Fixture("release-dehydrated-0.9.1.json");
    size_t trimmed = json.size();
    while (trimmed > 0 && (json[trimmed - 1] == '\n' || json[trimmed - 1] == '\r'))
    {
        trimmed--;
    }
    int accepted = 0;
    for (size_t cut = 0; cut < trimmed; cut += 7)
    {
        if (iUpdateParseRelease(json.data(), cut, r, why, sizeof(why)))
        {
            accepted++;
        }
    }
    CHECK(accepted == 0, "%d cut-short copies accepted", accepted);

    // Every byte replaced by a brace, a quote or a NUL in turn: whatever the
    // answer, no crash, and nothing longer than its buffer.
    for (size_t at = 0; at < json.size(); at += 13)
    {
        std::string bent = json;
        bent[at] = "{\"\0["[at % 4];
        if (iUpdateParseRelease(bent.data(), bent.size(), r, why, sizeof(why)))
        {
            for (int i = 0; i < r->assetCount; i++)
            {
                CHECK(strlen(r->assets[i].name) < IUPDATE_NAME_MAX, "name length");
            }
        }
    }
    delete r;
}

// --- 5. SHA256SUMS -----------------------------------------------------------------------------

static void TestSums()
{
    uint8_t d[32];
    char hex[65];
    for (int pass = 0; pass < 2; pass++)
    {
        std::string sums = Fixture("SHA256SUMS-dehydrated-0.9.1");
        if (pass == 1)
        {
            sums = ToCrLf(sums);
        }
        CHECK(iUpdateFindSum(sums.data(), sums.size(), "TSSM-Dehydrated-0.9-Switch.nro", d) ==
                  IUPDATE_SUM_FOUND,
              "Switch line (pass %d)", pass);
        iUpdateHex(d, 32, hex, sizeof(hex));
        CHECK(strcmp(hex, "b8cbc98120f3ffeaa3eb2f4e80fc26237a3bf7340e4810f40dd64ce018e32751") == 0,
              "Switch sum %s", hex);
        CHECK(iUpdateFindSum(sums.data(), sums.size(), "TSSM-Dehydrated-0.9.1-3DS.cia", d) ==
                  IUPDATE_SUM_FOUND,
              "3DS line");
        CHECK(iUpdateFindSum(sums.data(), sums.size(), "TSSM-Dehydrated-0.9.1-Switch.nro", d) ==
                  IUPDATE_SUM_MISSING,
              "no 0.9.1 Switch line");
        CHECK(iUpdateFindSum(sums.data(), sums.size(), "TSSM-Dehydrated-0.9-Switch.nr", d) ==
                  IUPDATE_SUM_MISSING,
              "a prefix of a name is not the name");
        CHECK(iUpdateFindSum(sums.data(), sums.size(), "Dehydrated-0.9-Switch.nro", d) ==
                  IUPDATE_SUM_MISSING,
              "a suffix of a name is not the name");
    }
    const std::string a(64, 'a');
    const std::string b(64, 'b');
    std::string s = a + " *x.nro\n";
    CHECK(iUpdateFindSum(s.data(), s.size(), "x.nro", d) == IUPDATE_SUM_FOUND && d[0] == 0xAA,
          "binary-mode marker");
    s = a + "  x.nro\n" + b + "  x.nro\n";
    CHECK(iUpdateFindSum(s.data(), s.size(), "x.nro", d) == IUPDATE_SUM_CONFLICTING,
          "two different lines");
    s = a + "  x.nro\n" + a + "  x.nro";
    CHECK(iUpdateFindSum(s.data(), s.size(), "x.nro", d) == IUPDATE_SUM_FOUND, "the same line twice");
    s = a.substr(1) + "  x.nro\n" + std::string(63, 'g') + "g  x.nro\n" + a + "x.nro\n" + a +
        " x.nro\n";
    CHECK(iUpdateFindSum(s.data(), s.size(), "x.nro", d) == IUPDATE_SUM_MISSING, "malformed lines");
    CHECK(iUpdateFindSum("", 0, "x.nro", d) == IUPDATE_SUM_MISSING, "empty");
}

// --- 6. SHA-256 -----------------------------------------------------------------------------------

static void TestSha()
{
    CHECK(Sha("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty");
    CHECK(Sha("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc");
    CHECK(Sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "448 bits");
    iUpdateSha256 sha;
    const std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; i++)
    {
        sha.Update(chunk.data(), chunk.size());
    }
    uint8_t d[32];
    sha.Final(d);
    char hex[65];
    iUpdateHex(d, 32, hex, sizeof(hex));
    CHECK(strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0") == 0,
          "a million a: %s", hex);
}

// --- 7. URLs ----------------------------------------------------------------------------------------

static void TestUrls()
{
    char host[64];
    char path[IUPDATE_LOCATION_MAX];
    struct
    {
        const char* url;
        const char* host;
        const char* path;
    } good[] = {
        { "https://api.github.com/repos/o/n/releases/latest", "api.github.com",
          "/repos/o/n/releases/latest" },
        { "https://GitHub.com/o/n", "github.com", "/o/n" },
        { "https://github.com:443/o/n", "github.com", "/o/n" },
        { "https://github.com", "github.com", "/" },
        { "https://github.com?x=1", "github.com", "/?x=1" },
        { "https://github.com/a#frag", "github.com", "/a" },
        { "https://objects.githubusercontent.com/x", "objects.githubusercontent.com", "/x" },
        { "https://release-assets.githubusercontent.com/github-production-release-asset/1?sp=r&"
          "sig=a%3D&rscd=attachment%3B+filename%3DSHA256SUMS",
          "release-assets.githubusercontent.com",
          "/github-production-release-asset/1?sp=r&sig=a%3D&rscd=attachment%3B+filename%"
          "3DSHA256SUMS" },
    };
    for (const auto& g : good)
    {
        const bool ok = iUpdateSplitUrl(g.url, host, sizeof(host), path, sizeof(path));
        CHECK(ok && strcmp(host, g.host) == 0 && strcmp(path, g.path) == 0, "%s -> %s %s", g.url,
              ok ? host : "(refused)", ok ? path : "");
    }
    const char* bad[] = {
        "http://github.com/x",
        "HTTPS://github.com/x",
        "https://github.com.example.com/x",
        "https://example.com/github.com",
        "https://user@github.com/x",
        "https://github.com@example.com/x",
        "https://github.com:8443/x",
        "https://github.com:443x/",
        "https://github.com:/x",
        "ftp://github.com/x",
        "https://raw.githubusercontent.com/x",
        "https://gist.github.com/x",
        "https://github.com/a b",
        "https://github.com/a\x01",
        "https://github.com/\xc3\xa9",
        "https:///x",
        "https://",
        "github.com/x",
        "//github.com/x",
        "https://xn--github-.com/x",
        "https://140.82.112.3/x",
    };
    for (const char* b : bad)
    {
        CHECK(!iUpdateSplitUrl(b, host, sizeof(host), path, sizeof(path)), "accepted %s", b);
    }
    // A path longer than the buffer is refused, not cut.
    std::string longPath = "https://github.com/" + std::string(5000, 'p');
    CHECK(!iUpdateSplitUrl(longPath.c_str(), host, sizeof(host), path, sizeof(path)), "long path");
}

// --- 8. the HTTP client ---------------------------------------------------------------------------

// Serves a fixed response in pieces of `piece` bytes and keeps what was sent.
class FakeStream : public iUpdateStream
{
public:
    FakeStream(const std::string& response, size_t piece, iUpdateSink* sink)
        : mResponse(response), mPiece(piece), mAt(0), mSink(sink), mCancelAt(0), mCancelled(false)
    {
    }
    iUpdateNet Write(const void* data, size_t n) override
    {
        mSent.append((const char*)data, n);
        return IUPDATE_NET_OK;
    }
    iUpdateNet Read(void* data, size_t n, size_t* got) override
    {
        if (mSink->Cancelled())
        {
            return IUPDATE_NET_CANCELLED;
        }
        size_t k = mResponse.size() - mAt;
        if (k > n)
        {
            k = n;
        }
        if (k > mPiece)
        {
            k = mPiece;
        }
        memcpy(data, mResponse.data() + mAt, k);
        mAt += k;
        *got = k;
        if (mCancelAt > 0 && mAt >= mCancelAt && !mCancelled)
        {
            mCancelled = true;
            iUpdateCancel();
        }
        return IUPDATE_NET_OK;
    }

    std::string mResponse;
    size_t mPiece;
    size_t mAt;
    iUpdateSink* mSink;
    size_t mCancelAt;
    bool mCancelled;
    std::string mSent;
};

class RecordSink : public iUpdateSink
{
public:
    RecordSink() : status(0), length(-2), hasLocation(false), want(true), stopAfter(0), cancel(false)
    {
    }
    bool OnResponse(int s, int64_t cl, const char* loc) override
    {
        status = s;
        length = cl;
        hasLocation = loc != NULL;
        location = loc != NULL ? loc : "";
        return want;
    }
    bool OnBody(const void* d, size_t n) override
    {
        body.append((const char*)d, n);
        return stopAfter == 0 || body.size() < stopAfter;
    }
    bool Cancelled() override { return cancel; }

    int status;
    int64_t length;
    bool hasLocation;
    std::string location;
    std::string body;
    bool want;
    size_t stopAfter;
    bool cancel;
};

static iUpdateNet Http(const std::string& response, RecordSink* sink, size_t piece = 3,
                       std::string* sent = NULL)
{
    iUpdateRequest req;
    req.host = "api.github.com";
    req.path = "/repos/o/n/releases/latest";
    req.accept = "application/vnd.github+json";
    req.timeoutMs = 1000;
    FakeStream s(response, piece, sink);
    char detail[256] = "";
    const iUpdateNet net = iUpdateHttpGet(&s, req, sink, detail, sizeof(detail));
    if (sent != NULL)
    {
        *sent = s.mSent;
    }
    return net;
}

static void TestHttp()
{
    {
        RecordSink sink;
        std::string sent;
        const iUpdateNet net =
            Http("HTTP/1.1 200 OK\r\nContent-Type: x\r\nContent-Length: 5\r\n\r\nhello", &sink, 1,
                 &sent);
        CHECK(net == IUPDATE_NET_OK && sink.status == 200 && sink.length == 5 &&
                  sink.body == "hello",
              "content-length: %s %d '%s'", iUpdateNetName(net), sink.status, sink.body.c_str());
        CHECK(sent == "GET /repos/o/n/releases/latest HTTP/1.1\r\n"
                      "Host: api.github.com\r\n"
                      "User-Agent: TSSM-port-updater\r\n"
                      "Accept: application/vnd.github+json\r\n"
                      "Connection: close\r\n\r\n",
              "the request is exactly: %s", sent.c_str());
    }
    {
        RecordSink sink;
        const iUpdateNet net = Http("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                                    "5;ext=1\r\nhello\r\nA\r\n, chunked!\r\n0\r\nX-Trailer: y\r\n\r\n",
                                    &sink);
        CHECK(net == IUPDATE_NET_OK && sink.body == "hello, chunked!" && sink.length == -1,
              "chunked: %s '%s'", iUpdateNetName(net), sink.body.c_str());
    }
    {
        RecordSink sink;
        sink.want = false;
        const iUpdateNet net =
            Http("HTTP/1.1 302 Found\r\nlocation: https://github.com/next\r\nContent-Length: 999\r\n\r\n",
                 &sink);
        CHECK(net == IUPDATE_NET_OK && sink.status == 302 && sink.location == "https://github.com/next",
              "redirect: %s %d %s", iUpdateNetName(net), sink.status, sink.location.c_str());
    }
    {
        RecordSink sink;
        sink.want = false;
        const iUpdateNet net = Http(
            "HTTP/1.1 302 Found\r\nLocation: /x\r\nContent-Length: 999\r\n\r\n", &sink);
        CHECK(net == IUPDATE_NET_OK && sink.body.empty(), "an unwanted body is not read");
    }
    {
        RecordSink sink;
        const iUpdateNet net = Http("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\n"
                                    "CONTENT-length: 2\r\n\r\nok",
                                    &sink);
        CHECK(net == IUPDATE_NET_OK && sink.status == 200 && sink.body == "ok", "100 then 200");
    }
    {
        RecordSink sink;
        const iUpdateNet net = Http("HTTP/1.0 200 OK\n\nto the end", &sink);
        CHECK(net == IUPDATE_NET_OK && sink.body == "to the end" && sink.length == -1,
              "no length, bare LF: '%s'", sink.body.c_str());
    }
    struct
    {
        const char* response;
        iUpdateNet expect;
        const char* what;
    } bad[] = {
        { "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort", IUPDATE_NET_LOST, "cut-off body" },
        { "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n", IUPDATE_NET_LOST, "cut-off headers" },
        { "", IUPDATE_NET_LOST, "nothing" },
        { "SSH-2.0-x\r\n\r\n", IUPDATE_NET_PROTOCOL, "not HTTP" },
        { "HTTP/1.1 2000 OK\r\n\r\n", IUPDATE_NET_PROTOCOL, "four-digit status" },
        { "HTTP/1.1 600 X\r\n\r\n", IUPDATE_NET_PROTOCOL, "status 600" },
        { "HTTP/2 200\r\n\r\n", IUPDATE_NET_PROTOCOL, "HTTP/2 text" },
        { "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nab",
          IUPDATE_NET_PROTOCOL, "two lengths" },
        { "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n", IUPDATE_NET_PROTOCOL, "negative length" },
        { "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\nx", IUPDATE_NET_PROTOCOL, "gzip" },
        { "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n0\r\n\r\n",
          IUPDATE_NET_PROTOCOL, "gzip chunked" },
        { "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n", IUPDATE_NET_PROTOCOL,
          "bad chunk size" },
        { "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhelloX\r\n0\r\n\r\n",
          IUPDATE_NET_PROTOCOL, "no CR LF after a chunk" },
        { "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhel", IUPDATE_NET_LOST,
          "cut-off chunk" },
        { "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nFFFFFFFFFFFFFFFF\r\n",
          IUPDATE_NET_PROTOCOL, "huge chunk" },
        { "HTTP/1.1 101 Switching\r\n\r\n", IUPDATE_NET_PROTOCOL, "101" },
    };
    for (const auto& b : bad)
    {
        RecordSink sink;
        const iUpdateNet net = Http(b.response, &sink);
        CHECK(net == b.expect, "%s: %s, expected %s", b.what, iUpdateNetName(net),
              iUpdateNetName(b.expect));
    }
    {
        // A header line longer than the client keeps is dropped (github.com's
        // redirect to a download has a Content-Security-Policy line of over
        // 4 KB), unless it is one the client reads.
        RecordSink sink;
        const std::string longLine = "HTTP/1.1 200 OK\r\nX: " + std::string(10000, 'x') +
                                     "\r\nContent-Length: 2\r\n\r\nok";
        iUpdateNet net = Http(longLine, &sink);
        CHECK(net == IUPDATE_NET_OK && sink.status == 200 && sink.body == "ok",
              "a long header line is skipped: %s '%s'", iUpdateNetName(net), sink.body.c_str());
        RecordSink gh;
        gh.want = false;
        const std::string ghLocation =
            "https://release-assets.githubusercontent.com/github-production-release-asset/1/2?sp=r&sig=" +
            std::string(800, 's');
        net = Http("HTTP/1.1 302 Found\r\nDate: x\r\nVary: X-PJAX\r\nLocation: " + ghLocation +
                       "\r\nCache-Control: no-cache\r\nContent-Security-Policy: default-src 'none'; " +
                       std::string(4140, 'c') + "\r\nServer: github.com\r\nContent-Length: 0\r\n" +
                       "connection: close\r\n\r\n",
                   &gh, 4096);
        CHECK(net == IUPDATE_NET_OK && gh.status == 302 && gh.location == ghLocation,
              "github.com's redirect with its 4 KB policy line: %s %d", iUpdateNetName(net),
              gh.status);
        RecordSink cut;
        net = Http("HTTP/1.1 200 OK\r\nContent-Length: " + std::string(5000, '1') + "\r\n\r\n",
                   &cut);
        CHECK(net == IUPDATE_NET_PROTOCOL, "a Content-Length too long to read: %s",
              iUpdateNetName(net));
        net = Http("HTTP/1.1 200 OK\r\nTransfer-Encoding: " + std::string(5000, ' ') +
                       "chunked\r\n\r\n0\r\n\r\n",
                   &cut);
        CHECK(net == IUPDATE_NET_PROTOCOL, "a Transfer-Encoding too long to read: %s",
              iUpdateNetName(net));
        net = Http("HTTP/1.1 200 OK\r\nX: " + std::string(70000, 'x') + "\r\n\r\n", &cut, 4096);
        CHECK(net == IUPDATE_NET_PROTOCOL, "a header line over 64 KB: %s", iUpdateNetName(net));
        std::string big = "HTTP/1.1 200 OK\r\n";
        for (int i = 0; i < 20; i++)
        {
            big += "X: " + std::string(4000, 'x') + "\r\n";
        }
        big += "\r\n";
        net = Http(big, &cut, 4096);
        CHECK(net == IUPDATE_NET_PROTOCOL, "headers over 64 KB in all: %s", iUpdateNetName(net));
        net = Http("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nok\r\n0\r\nX-T: " +
                       std::string(6000, 't') + "\r\n\r\n",
                   &cut, 4096);
        CHECK(net == IUPDATE_NET_OK, "a long trailer line is skipped: %s", iUpdateNetName(net));
        std::string many = "HTTP/1.1 200 OK\r\n";
        for (int i = 0; i < 300; i++)
        {
            many += "X: y\r\n";
        }
        many += "\r\n";
        CHECK(Http(many, &sink) == IUPDATE_NET_PROTOCOL, "too many header lines");
        const std::string longLocation =
            "HTTP/1.1 302 Found\r\nLocation: https://github.com/" + std::string(5000, 'l') + "\r\n\r\n";
        CHECK(Http(longLocation, &sink) == IUPDATE_NET_PROTOCOL, "a Location too long");
    }
    {
        RecordSink sink;
        sink.stopAfter = 3;
        CHECK(Http("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n0123456789", &sink, 1) ==
                  IUPDATE_NET_STOPPED,
              "the sink stops it");
        RecordSink c;
        c.cancel = true;
        CHECK(Http("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n0123456789", &c) ==
                  IUPDATE_NET_CANCELLED,
              "a cancel");
    }
    {
        iUpdateRequest req;
        req.host = "api.github.com";
        req.accept = "x";
        req.timeoutMs = 1;
        char out[512];
        req.path = "/a\r\nX-Injected: 1";
        CHECK(iUpdateHttpRequestText(req, out, sizeof(out)) == 0, "CR LF in a path");
        req.path = "/a b";
        CHECK(iUpdateHttpRequestText(req, out, sizeof(out)) == 0, "a space in a path");
        req.path = "/a";
        req.accept = "x\ny";
        CHECK(iUpdateHttpRequestText(req, out, sizeof(out)) == 0, "LF in Accept");
    }
}

// --- 9. whole checks and downloads ------------------------------------------------------------------

struct Route
{
    std::string host;
    std::string path;
    std::string response;
};

static std::string Ok(const std::string& body, bool chunked = false)
{
    if (chunked)
    {
        std::string r = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nServer: x\r\n\r\n";
        for (size_t at = 0; at < body.size(); at += 1000)
        {
            const std::string part = body.substr(at, 1000);
            char size[16];
            snprintf(size, sizeof(size), "%x\r\n", (unsigned)part.size());
            r += size + part + "\r\n";
        }
        return r + "0\r\n\r\n";
    }
    char head[128];
    snprintf(head, sizeof(head), "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n", body.size());
    return head + body;
}

static std::string Status(int code)
{
    char r[96];
    snprintf(r, sizeof(r), "HTTP/1.1 %d X\r\nContent-Length: 2\r\n\r\n{}", code);
    return r;
}

// As github.com redirects a download: a Content-Security-Policy line of
// over 4 KB among the headers.
static std::string Redirect(const std::string& to)
{
    return "HTTP/1.1 302 Found\r\nLocation: " + to +
           "\r\nContent-Security-Policy: default-src 'none'; " + std::string(4200, 'c') +
           "\r\nContent-Length: 0\r\n\r\n";
}

class FakePlatform : public iUpdatePlatform
{
public:
    FakePlatform()
        : suffix("-Switch.nro"), piece(4096), cancelAt(0), fail(IUPDATE_NET_OK), installOk(true),
          allowed(true), installs(0), stops(0)
    {
    }

    const char* AssetSuffix() override { return suffix.c_str(); }

    iUpdateNet Get(const iUpdateRequest& req, iUpdateSink* sink, char* detail,
                   size_t detailSize) override
    {
        requests.push_back(std::string(req.host) + req.path);
        accepts.push_back(req.accept);
        if (fail != IUPDATE_NET_OK)
        {
            return fail;
        }
        std::string response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        for (const Route& r : routes)
        {
            if (r.host == req.host && r.path == req.path)
            {
                response = r.response;
                break;
            }
        }
        FakeStream s(response, piece, sink);
        s.mCancelAt = cancelAt;
        const iUpdateNet net = iUpdateHttpGet(&s, req, sink, detail, detailSize);
        sent.push_back(s.mSent);
        return net;
    }

    bool DownloadPath(char* out, size_t outSize, char*, size_t) override
    {
        if (!downloadPath.empty())
        {
            snprintf(out, outSize, "%s", downloadPath.c_str());
            return true;
        }
        snprintf(out, outSize, "%s.download", installed.c_str());
        return true;
    }

    bool Install(const char* path, const iUpdateVersion& version, char* why,
                 size_t whySize) override
    {
        installs++;
        iUpdateFormatVersion(version, installedVersion, sizeof(installedVersion));
        if (!installOk)
        {
            snprintf(why, whySize, "The stand-in install failed. The installed version is unchanged.");
            return false;
        }
        return iUpdateReplaceFile(path, installed.c_str(), why, whySize);
    }

    bool InstallAllowed(char* why, size_t whySize) override
    {
        if (!allowed)
        {
            snprintf(why, whySize, "This copy is a build of your own.");
        }
        return allowed;
    }

    bool CanRestart() override { return true; }

    void Stop() override { stops++; }

    std::string suffix;
    std::vector<Route> routes;
    std::vector<std::string> requests;
    std::vector<std::string> accepts;
    std::vector<std::string> sent;
    size_t piece;
    size_t cancelAt;
    iUpdateNet fail;
    bool installOk;
    bool allowed;
    int installs;
    int stops;
    char installedVersion[32];
    std::string installed;
    std::string downloadPath; // empty: beside the installed file
};

static const char kApi[] = "api.github.com";
static const char kLatest[] = "/repos/example-owner/tssm-ports/releases/latest";
static const char kAssets[] = "release-assets.githubusercontent.com";

struct FakeAsset
{
    std::string name;
    uint64_t size;
    std::string url;
    std::string digest; // hex, or empty for none
};

static std::string MakeRelease(const char* tag, const std::vector<FakeAsset>& assets)
{
    std::string j = "{\n  \"url\": \"https://api.github.com/x\",\n  \"id\": 7,\n";
    j += "  \"author\": {\"login\": \"example-owner\", \"id\": 1, \"site_admin\": false},\n";
    j += std::string("  \"tag_name\": \"") + tag + "\",\n  \"draft\": false,\n" +
         "  \"prerelease\": false,\n  \"assets\": [\n";
    for (size_t i = 0; i < assets.size(); i++)
    {
        char size[32];
        snprintf(size, sizeof(size), "%llu", (unsigned long long)assets[i].size);
        j += std::string(i ? ",\n" : "") + "    {\"url\": \"https://api.github.com/a\", \"id\": 9, " +
             "\"name\": \"" + assets[i].name + "\", \"label\": null, " +
             "\"uploader\": {\"login\": \"example-owner\", \"id\": 1}, " +
             "\"content_type\": \"application/octet-stream\", \"state\": \"uploaded\", " +
             "\"size\": " + size + ", " +
             (assets[i].digest.empty() ? "" : "\"digest\": \"sha256:" + assets[i].digest + "\", ") +
             "\"download_count\": 3, \"browser_download_url\": \"" + assets[i].url + "\"}";
    }
    j += "\n  ],\n  \"body\": \"notes \\u00e9\"\n}\n";
    return j;
}

static std::string Download(const char* tag, const std::string& name)
{
    return std::string("/example-owner/tssm-ports/releases/download/") + tag + "/" + name;
}

static FakePlatform* sFake;
static const std::string kOld = "the installed program, version 0.9";

// A fresh platform and core, the installed file put back.
static FakePlatform* Fresh(const char* current)
{
    delete sFake;
    sFake = new FakePlatform;
    sFake->installed = sScratch + "/tssm.nro";
    WriteFile(sFake->installed, kOld);
    iHostRemoveFile((sFake->installed + ".download").c_str());
    iUpdateSettings s;
    s.repo = "example-owner/tssm-ports";
    s.currentVersion = current;
    CHECK(iUpdateInit(sFake, s), "init %s", current);
    return sFake;
}

static iUpdateStatus Run(void (*start)())
{
    start();
    iUpdateWait();
    iUpdateStatus st;
    iUpdateGetStatus(&st);
    return st;
}

static bool Untouched()
{
    std::string now;
    return ReadFile(sFake->installed, &now) && now == kOld &&
           !iHostPathExists((sFake->installed + ".download").c_str());
}

static bool Has(const char* message, const char* part)
{
    return strstr(message, part) != NULL;
}

// A release with a Switch file `body`, its SHA256SUMS and redirects as GitHub
// serves them.
static void Serve(FakePlatform* p, const char* tag, const char* version, const std::string& body,
                  const std::string& sumsText, bool digest = true)
{
    const std::string name = std::string("TSSM-Dehydrated-") + version + "-Switch.nro";
    std::vector<FakeAsset> assets = {
        { "SHA256SUMS", sumsText.size(), "https://github.com" + Download(tag, "SHA256SUMS"), Sha(sumsText) },
        { name, body.size(), "https://github.com" + Download(tag, name), digest ? Sha(body) : "" },
        { std::string("TSSM-Dehydrated-") + version + "-3DS.cia", 4, "https://github.com" + Download(tag, "c"), "" },
    };
    p->routes.push_back({ kApi, kLatest, Ok(MakeRelease(tag, assets), true) });
    p->routes.push_back({ "github.com", Download(tag, "SHA256SUMS"),
                          Redirect("https://release-assets.githubusercontent.com/"
                                   "github-production-release-asset/1?sp=r&sig=s%3D") });
    p->routes.push_back({ kAssets, "/github-production-release-asset/1?sp=r&sig=s%3D", Ok(sumsText) });
    p->routes.push_back({ "github.com", Download(tag, name),
                          Redirect("https://release-assets.githubusercontent.com/"
                                   "github-production-release-asset/2?sp=r&sig=n%3D") });
    p->routes.push_back({ kAssets, "/github-production-release-asset/2?sp=r&sig=n%3D", Ok(body) });
}

static std::string Body(size_t n)
{
    std::string b(n, '\0');
    memcpy(&b[0], "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0NRO0", n < 20 ? n : 20);
    for (size_t i = 20; i < n; i++)
    {
        b[i] = (char)(i * 131 + (i >> 9));
    }
    return b;
}

static std::string SumsFor(const std::string& name, const std::string& body)
{
    return std::string(64, '0') + "  TSSM-Dehydrated-9.9-3DS.cia\n" + Sha(body) + "  " + name + "\n";
}

static void CheckFlows()
{
    // The real releases: a 0.9 build is up to date with both.
    for (const char* tag : { "dehydrated-0.9", "dehydrated-0.9.1" })
    {
        FakePlatform* p = Fresh("0.9");
        p->routes.push_back({ kApi, kLatest, Ok(Fixture((std::string("release-") + tag + ".json").c_str()), true) });
        iUpdateStatus st = Run(iUpdateCheck);
        CHECK(st.state == IUPDATE_UP_TO_DATE && Has(st.message, "newest version (0.9)"),
              "%s, current 0.9: %s: %s", tag, iUpdateStateName(st.state), st.message);
        CHECK(p->requests.size() == 1 && p->requests[0] == std::string(kApi) + kLatest &&
                  p->accepts[0] == std::string("application/vnd.github+json"),
              "one request to the API");
        CHECK(p->sent.size() == 1 && p->sent[0].find("User-Agent: TSSM-port-updater\r\n") != std::string::npos &&
                  p->sent[0].find("Authorization") == std::string::npos &&
                  p->sent[0].find("Cookie") == std::string::npos,
              "only the updater's own headers");
    }

    // 0.9.1 seen from 0.8: its Switch file is 0.9, which is newer. The body
    // served is not the real file, so the checksum refuses it after download.
    {
        FakePlatform* p = Fresh("0.8");
        const std::string json = Fixture("release-dehydrated-0.9.1.json");
        const std::string sums = Fixture("SHA256SUMS-dehydrated-0.9.1");
        p->routes.push_back({ kApi, kLatest, Ok(json, true) });
        p->routes.push_back({ "github.com", Download("dehydrated-0.9.1", "SHA256SUMS"),
                              Redirect("https://release-assets.githubusercontent.com/s?sig=1") });
        p->routes.push_back({ kAssets, "/s?sig=1", Ok(sums) });
        p->routes.push_back({ "github.com", Download("dehydrated-0.9.1", "TSSM-Dehydrated-0.9-Switch.nro"),
                              Redirect("https://release-assets.githubusercontent.com/n?sig=2") });
        p->routes.push_back({ kAssets, "/n?sig=2", Ok(Body(13225663)) });
        p->piece = 65536;
        iUpdateStatus st = Run(iUpdateCheck);
        CHECK(st.state == IUPDATE_AVAILABLE && strcmp(st.latestVersion, "0.9") == 0 &&
                  st.assetSize == 13225663 &&
                  strcmp(st.assetName, "TSSM-Dehydrated-0.9-Switch.nro") == 0 && st.installAllowed,
              "0.9.1 from 0.8: %s %s %s", iUpdateStateName(st.state), st.latestVersion, st.message);
        CHECK(Has(st.message, "Version 0.9 is available (12.6 MB)"), "message: %s", st.message);
        st = Run(iUpdateDownload);
        CHECK(st.state == IUPDATE_ERROR && Has(st.message, "did not match its checksum"),
              "a body that is not the release's: %s: %s", iUpdateStateName(st.state), st.message);
        CHECK(p->installs == 0 && Untouched(), "nothing installed, the download deleted");
        CHECK(p->requests.size() == 5, "requests %zu", p->requests.size());
    }

    // A good update, start to end.
    {
        FakePlatform* p = Fresh("0.9");
        const std::string body = Body(300000);
        const std::string name = "TSSM-Dehydrated-0.9.2-Switch.nro";
        Serve(p, "dehydrated-0.9.2", "0.9.2", body, SumsFor(name, body));
        p->piece = 777;
        iUpdateStatus st = Run(iUpdateCheck);
        CHECK(st.state == IUPDATE_AVAILABLE && strcmp(st.latestVersion, "0.9.2") == 0,
              "good: %s %s", iUpdateStateName(st.state), st.message);
        CHECK(iUpdateCanDownload() && iUpdateCanCheck() && !iUpdateCanCancel(), "buttons");
        st = Run(iUpdateDownload);
        std::string now;
        CHECK(st.state == IUPDATE_DONE_RESTART && Has(st.message, "0.9.2 is installed") &&
                  st.canRestart,
              "good: %s: %s", iUpdateStateName(st.state), st.message);
        CHECK(p->installs == 1 && strcmp(p->installedVersion, "0.9.2") == 0 &&
                  ReadFile(p->installed, &now) && now == body &&
                  !iHostPathExists((p->installed + ".download").c_str()),
              "the new file is in place, the download gone");
        CHECK(!iUpdateCanCheck() && !iUpdateCanDownload(), "nothing more until a restart");
        CHECK(p->requests.size() == 5 && p->accepts[1] == std::string("application/octet-stream"),
              "requests %zu", p->requests.size());
        for (const std::string& s : p->sent)
        {
            CHECK(s.find("\r\n\r\n") == s.size() - 4 &&
                      std::count(s.begin(), s.end(), '\n') == 6,
                  "request has five lines: %s", s.c_str());
        }
        CHECK(iUpdatePrepareRestart() == false, "the stand-in arranges no restart");
    }

    // Refusals at the check.
    struct
    {
        std::string response;
        const char* expect;
        iUpdateState state;
    } checks[] = {
        { Status(404), "No update information found.", IUPDATE_ERROR },
        { Status(403), "Try again in an hour", IUPDATE_ERROR },
        { Status(429), "Try again in an hour", IUPDATE_ERROR },
        { Status(500), "HTTP 500", IUPDATE_ERROR },
        { Ok("{\"tag_name\": \"x\", \"assets\": [") , "could not be read", IUPDATE_ERROR },
        { Ok("<html>rate limited</html>"), "could not be read", IUPDATE_ERROR },
        { Redirect("https://example.com/latest"), "does not go", IUPDATE_ERROR },
        { Redirect("http://api.github.com/latest"), "does not go", IUPDATE_ERROR },
        { Redirect(kLatest), "update server's answer could not be read", IUPDATE_ERROR },
        // Over the limit, with a length and without one (stopped part-way).
        { Ok(std::string(IUPDATE_JSON_MAX + 1, ' ')), "update information could not be read",
          IUPDATE_ERROR },
        { Ok(std::string(IUPDATE_JSON_MAX + 1, ' '), true), "update information could not be read",
          IUPDATE_ERROR },
    };
    for (const auto& c : checks)
    {
        FakePlatform* p = Fresh("0.9");
        p->routes.push_back({ kApi, kLatest, c.response });
        const iUpdateStatus st = Run(iUpdateCheck);
        CHECK(st.state == c.state && Has(st.message, c.expect), "check: expected \"%s\", got %s: %s",
              c.expect, iUpdateStateName(st.state), st.message);
        CHECK(Untouched(), "untouched");
    }
    {
        FakePlatform* p = Fresh("0.9");
        p->fail = IUPDATE_NET_OFFLINE;
        iUpdateStatus st = Run(iUpdateCheck);
        CHECK(st.state == IUPDATE_ERROR && Has(st.message, "Not connected to the internet"),
              "offline: %s", st.message);
        p->fail = IUPDATE_NET_CLOCK;
        st = Run(iUpdateCheck);
        CHECK(st.state == IUPDATE_ERROR && Has(st.message, "date and time"), "clock: %s", st.message);
    }

    // Releases that are not usable as they stand.
    {
        const std::string body = Body(1000);
        const std::string name = "TSSM-Dehydrated-0.9.2-Switch.nro";
        const std::string url = "https://github.com" + Download("t", name);
        const std::string sumsUrl = "https://github.com" + Download("t", "SHA256SUMS");
        struct
        {
            std::string json;
            const char* expect;
            iUpdateState state;
        } rel[] = {
            { MakeRelease("t", { { name, body.size(), url, "" } }), "no checksum file", IUPDATE_ERROR },
            { MakeRelease("t", { { "SHA256SUMS", 10, sumsUrl, "" }, { name, 70ull << 20, url, "" } }),
              "larger than this updater takes", IUPDATE_ERROR },
            { MakeRelease("t", { { "SHA256SUMS", 10, sumsUrl, "" },
                                 { name, body.size(),
                                   "https://github.com/someone-else/tssm-ports/releases/download/t/" + name, "" } }),
              "could not be read", IUPDATE_ERROR },
            { MakeRelease("t", { { "SHA256SUMS", 10, "https://github.com/someone-else/x/releases/download/t/SHA256SUMS", "" },
                                 { name, body.size(), url, "" } }),
              "no checksum file", IUPDATE_ERROR },
            { Replace(MakeRelease("t", { { "SHA256SUMS", 10, sumsUrl, "" }, { name, body.size(), url, "" } }),
                      "\"prerelease\": false", "\"prerelease\": true"),
              "No update information found.", IUPDATE_ERROR },
            { Replace(MakeRelease("t", { { "SHA256SUMS", 10, sumsUrl, "" }, { name, body.size(), url, "" } }),
                      "\"draft\": false", "\"draft\": true"),
              "No update information found.", IUPDATE_ERROR },
            { MakeRelease("t", { { "SHA256SUMS", 10, sumsUrl, "" },
                                 { "TSSM-Dehydrated-0.9.2-3DS.cia", 5, url, "" } }),
              "newest version (0.9)", IUPDATE_UP_TO_DATE },
            { MakeRelease("t", { { "SHA256SUMS", 10, sumsUrl, "" },
                                 { "TSSM-Dehydrated-0.9.0-Switch.nro", 5, url, "" } }),
              "newest version (0.9)", IUPDATE_UP_TO_DATE },
        };
        for (const auto& r : rel)
        {
            FakePlatform* p = Fresh("0.9");
            p->routes.push_back({ kApi, kLatest, Ok(r.json) });
            const iUpdateStatus st = Run(iUpdateCheck);
            CHECK(st.state == r.state && Has(st.message, r.expect),
                  "release: expected \"%s\", got %s: %s", r.expect, iUpdateStateName(st.state),
                  st.message);
            CHECK(p->requests.size() == 1, "nothing but the check: %zu", p->requests.size());
        }
        // A newer version than 0.10 is not older because it is shorter.
        FakePlatform* p = Fresh("0.10");
        p->routes.push_back({ kApi, kLatest, Ok(Fixture("release-dehydrated-0.9.1.json")) });
        const iUpdateStatus st = Run(iUpdateCheck);
        CHECK(st.state == IUPDATE_UP_TO_DATE && Has(st.message, "(0.10)"), "0.10: %s", st.message);
    }

    // Refusals during the download; each leaves the installed file alone.
    {
        const std::string body = Body(50000);
        const std::string name = "TSSM-Dehydrated-0.9.2-Switch.nro";
        const char* tag = "dehydrated-0.9.2";
        const char* sumsPath = "/github-production-release-asset/1?sp=r&sig=s%3D";
        const char* bodyPath = "/github-production-release-asset/2?sp=r&sig=n%3D";

        struct Case
        {
            const char* what;
            const char* expect;
            iUpdateState state;
            int fetches; // requests after the check
        };
        auto setup = [&](FakePlatform* p, const std::string& sums, bool digest) {
            Serve(p, tag, "0.9.2", body, sums, digest);
        };
        auto route = [](FakePlatform* p, const std::string& host, const std::string& path,
                        const std::string& response) {
            for (Route& r : p->routes)
            {
                if (r.host == host && r.path == path)
                {
                    r.response = response;
                }
            }
        };
        auto run = [&](FakePlatform* p, const Case& c) {
            iUpdateStatus st = Run(iUpdateCheck);
            CHECK(st.state == IUPDATE_AVAILABLE, "%s: check %s", c.what, st.message);
            const size_t before = p->requests.size();
            st = Run(iUpdateDownload);
            CHECK(st.state == c.state && Has(st.message, c.expect), "%s: expected \"%s\", got %s: %s",
                  c.what, c.expect, iUpdateStateName(st.state), st.message);
            CHECK(p->installs == 0 && Untouched(), "%s: untouched", c.what);
            CHECK(c.fetches < 0 || (int)(p->requests.size() - before) == c.fetches,
                  "%s: %zu requests", c.what, p->requests.size() - before);
        };

        FakePlatform* p = Fresh("0.9");
        setup(p, std::string(64, 'a') + "  some-other-file\n", true);
        run(p, { "no line", "no usable line", IUPDATE_ERROR, 2 });

        p = Fresh("0.9");
        setup(p, SumsFor(name, body) + std::string(64, 'b') + "  " + name + "\n", true);
        run(p, { "two lines", "no usable line", IUPDATE_ERROR, 2 });

        // Over the limit, with a length and without one (stopped part-way).
        p = Fresh("0.9");
        setup(p, SumsFor(name, body), true);
        route(p, kAssets, sumsPath, Ok(SumsFor(name, body) + std::string(IUPDATE_SUMS_MAX, '\n')));
        run(p, { "SHA256SUMS too big", "no usable line", IUPDATE_ERROR, 2 });

        p = Fresh("0.9");
        setup(p, SumsFor(name, body), true);
        route(p, kAssets, sumsPath,
              Ok(SumsFor(name, body) + std::string(IUPDATE_SUMS_MAX, '\n'), true));
        run(p, { "SHA256SUMS too big, chunked", "no usable line", IUPDATE_ERROR, 2 });

        p = Fresh("0.9");
        setup(p, std::string(64, 'c') + "  " + name + "\n", true);
        run(p, { "digest and sums disagree", "checksums disagree", IUPDATE_ERROR, 2 });

        p = Fresh("0.9");
        setup(p, std::string(64, 'c') + "  " + name + "\n", false);
        run(p, { "sum wrong, no digest", "did not match its checksum", IUPDATE_ERROR, 4 });

        p = Fresh("0.9");
        setup(p, SumsFor(name, body), true);
        route(p, kAssets, sumsPath, Status(404));
        run(p, { "SHA256SUMS gone", "HTTP 404", IUPDATE_ERROR, 2 });

        p = Fresh("0.9");
        setup(p, SumsFor(name, body), true);
        route(p, "github.com", Download(tag, name), Redirect("https://evil.example.com/x.nro"));
        run(p, { "redirect off the hosts", "does not go", IUPDATE_ERROR, 3 });

        p = Fresh("0.9");
        setup(p, SumsFor(name, body), true);
        route(p, "github.com", Download(tag, name), Redirect("https://github.com" + Download(tag, name)));
        run(p, { "redirect loop", "could not be read", IUPDATE_ERROR, 3 + kUpdateMaxRedirects });

        p = Fresh("0.9");
        setup(p, SumsFor(name, body), true);
        route(p, kAssets, bodyPath, Ok(body.substr(0, 40000)));
        run(p, { "shorter than listed", "not the size the release lists", IUPDATE_ERROR, 4 });

        p = Fresh("0.9");
        setup(p, SumsFor(name, body), true);
        route(p, kAssets, bodyPath, Ok(body + "extra"));
        run(p, { "longer than listed", "not the size the release lists", IUPDATE_ERROR, 4 });

        p = Fresh("0.9");
        setup(p, SumsFor(name, body), true);
        route(p, kAssets, bodyPath, Ok(body + "extra", true));
        run(p, { "longer than listed, chunked", "not the size the release lists", IUPDATE_ERROR, 4 });

        p = Fresh("0.9");
        setup(p, SumsFor(name, body), true);
        route(p, kAssets, bodyPath, Ok(body).substr(0, 30000));
        run(p, { "cut off", "broke off", IUPDATE_ERROR, 4 });

        p = Fresh("0.9");
        setup(p, SumsFor(name, body), true);
        p->cancelAt = 20000;
        p->piece = 1000;
        iUpdateStatus st = Run(iUpdateCheck);
        p->cancelAt = 0;
        CHECK(st.state == IUPDATE_AVAILABLE || st.state == IUPDATE_IDLE, "cancel setup: %s", st.message);
        if (st.state == IUPDATE_IDLE)
        {
            // The check's own response was long enough to reach the cancel.
            CHECK(Has(st.message, "Cancelled."), "check cancelled: %s", st.message);
            st = Run(iUpdateCheck);
        }
        p->cancelAt = 20000;
        p->requests.clear();
        st = Run(iUpdateDownload);
        CHECK(st.state == IUPDATE_AVAILABLE && Has(st.message, "Download cancelled. Nothing was changed."),
              "cancelled download: %s: %s", iUpdateStateName(st.state), st.message);
        CHECK(p->installs == 0 && Untouched(), "cancel: untouched");
        p->cancelAt = 0;
        st = Run(iUpdateDownload);
        CHECK(st.state == IUPDATE_DONE_RESTART, "after a cancel it can go again: %s", st.message);

        p = Fresh("0.9");
        setup(p, SumsFor(name, body), true);
        p->installOk = false;
        st = Run(iUpdateCheck);
        st = Run(iUpdateDownload);
        CHECK(st.state == IUPDATE_ERROR && Has(st.message, "stand-in install failed") && p->installs == 1 &&
                  Untouched(),
              "install failed: %s", st.message);

        p = Fresh("0.9");
        setup(p, SumsFor(name, body), true);
        p->allowed = false;
        st = Run(iUpdateCheck);
        CHECK(st.state == IUPDATE_AVAILABLE && !st.installAllowed && !iUpdateCanDownload() &&
                  Has(st.installBlocked, "your own"),
              "install not allowed: %s", st.installBlocked);
        iUpdateDownload();
        iUpdateWait();
        CHECK(p->requests.size() == 1 && Untouched(), "nothing downloaded");

        // Cancel during the check.
        p = Fresh("0.9");
        setup(p, SumsFor(name, body), true);
        p->cancelAt = 100;
        p->piece = 10;
        st = Run(iUpdateCheck);
        CHECK(st.state == IUPDATE_IDLE && Has(st.message, "Cancelled."), "check cancelled: %s: %s",
              iUpdateStateName(st.state), st.message);

        // A download that cannot be written: plain words, no path.
        p = Fresh("0.9");
        setup(p, SumsFor(name, body), true);
        p->downloadPath = sScratch + "/no-such-folder/tssm.nro.download";
        st = Run(iUpdateCheck);
        st = Run(iUpdateDownload);
        CHECK(st.state == IUPDATE_ERROR &&
                  Has(st.message, "Could not save the download (the storage may be full).") &&
                  !Has(st.message, "no-such-folder") && p->installs == 0 && Untouched(),
              "unwritable download: %s: %s", iUpdateStateName(st.state), st.message);

        // The quit ends the platform's waits, then joins the worker.
        p = Fresh("0.9");
        iUpdateShutdown();
        CHECK(p->stops == 1, "Stop at the quit: %d", p->stops);
    }

    // The core refuses a version or a repository it cannot use.
    {
        FakePlatform p;
        iUpdateSettings s;
        s.repo = "example-owner/tssm-ports";
        s.currentVersion = "0.9-test";
        CHECK(!iUpdateInit(&p, s), "a version with a word");
        s.currentVersion = "0.9";
        s.repo = "no-slash";
        CHECK(!iUpdateInit(&p, s), "a repository without an owner");
        s.repo = "a/b/c";
        CHECK(!iUpdateInit(&p, s), "a repository path");
        s.repo = "a/b?x";
        CHECK(!iUpdateInit(&p, s), "a repository with a query");
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("usage: tssm_updatetest <fixture folder> [scratch folder]\n");
        return 2;
    }
    sFixtures = argv[1];
    if (argc >= 3)
    {
        sScratch = argv[2];
    }
    else
    {
        char tmp[1024];
        if (!iHostTempDir(tmp, sizeof(tmp)))
        {
            printf("no temp folder; pass a scratch folder\n");
            return 2;
        }
        sScratch = std::string(tmp) + "/tssm_updatetest";
    }
    iHostMakeDir(sScratch.c_str());

    TestVersions();
    TestRealReleases();
    TestBadJson();
    TestSums();
    TestSha();
    TestUrls();
    TestHttp();
    CheckFlows();
    iUpdateShutdown();
    delete sFake;

    printf("tssm_updatetest: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
