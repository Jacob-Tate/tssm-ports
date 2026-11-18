// In-app updates: the platform-neutral core. iUpdate.h says what it does and
// what it enforces; this file is the same on every branch that has updates.

#include "iUpdate.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/sha.h>
}

// The host seam last: its types.h defines NULL and TRUE as macros.
#include "iHost.h"

// --- versions ---------------------------------------------------------------------------

bool iUpdateParseVersion(const char* text, size_t len, iUpdateVersion* out)
{
    iUpdateVersion v;
    memset(&v, 0, sizeof(v));
    size_t i = 0;
    while (true)
    {
        if (v.count == 4)
        {
            return false;
        }
        uint32_t n = 0;
        size_t digits = 0;
        while (i < len && text[i] >= '0' && text[i] <= '9')
        {
            if (++digits > 9)
            {
                return false;
            }
            n = n * 10 + (uint32_t)(text[i] - '0');
            i++;
        }
        if (digits == 0)
        {
            return false;
        }
        v.part[v.count++] = n;
        if (i == len)
        {
            break;
        }
        if (text[i] != '.')
        {
            return false;
        }
        i++;
    }
    *out = v;
    return true;
}

int iUpdateCompareVersions(const iUpdateVersion& a, const iUpdateVersion& b)
{
    for (int i = 0; i < 4; i++)
    {
        const uint32_t x = i < a.count ? a.part[i] : 0;
        const uint32_t y = i < b.count ? b.part[i] : 0;
        if (x != y)
        {
            return x < y ? -1 : 1;
        }
    }
    return 0;
}

void iUpdateFormatVersion(const iUpdateVersion& v, char* out, size_t outSize)
{
    size_t n = 0;
    out[0] = '\0';
    for (int i = 0; i < v.count && n < outSize; i++)
    {
        const int w = snprintf(out + n, outSize - n, i == 0 ? "%u" : ".%u", (unsigned)v.part[i]);
        if (w < 0)
        {
            break;
        }
        n += (size_t)w;
    }
}

// --- JSON -----------------------------------------------------------------------------------

namespace
{
    // A reader for exactly what the release needs: every value is either read
    // into a fixed buffer or skipped. Nothing is allocated.
    struct Json
    {
        const char* start;
        const char* p;
        const char* end;
        int depth;
        bool failed;
        char why[128];
    };

    const int kJsonMaxDepth = 32;

    void JsonFail(Json& j, const char* what)
    {
        if (!j.failed)
        {
            j.failed = true;
            snprintf(j.why, sizeof(j.why), "%s at byte %ld", what, (long)(j.p - j.start));
        }
    }

    void JsonSpace(Json& j)
    {
        while (j.p < j.end && (*j.p == ' ' || *j.p == '\t' || *j.p == '\n' || *j.p == '\r'))
        {
            j.p++;
        }
    }

    bool JsonPeek(Json& j, char c)
    {
        JsonSpace(j);
        return j.p < j.end && *j.p == c;
    }

    bool JsonExpect(Json& j, char c)
    {
        if (!JsonPeek(j, c))
        {
            char what[32];
            snprintf(what, sizeof(what), "expected '%c'", c);
            JsonFail(j, what);
            return false;
        }
        j.p++;
        return true;
    }

    int HexDigit(char c)
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f')
        {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F')
        {
            return c - 'A' + 10;
        }
        return -1;
    }

    bool JsonHex4(Json& j, uint32_t* out)
    {
        if (j.end - j.p < 4)
        {
            JsonFail(j, "short \\u escape");
            return false;
        }
        uint32_t v = 0;
        for (int i = 0; i < 4; i++)
        {
            const int d = HexDigit(j.p[i]);
            if (d < 0)
            {
                JsonFail(j, "bad \\u escape");
                return false;
            }
            v = (v << 4) | (uint32_t)d;
        }
        j.p += 4;
        *out = v;
        return true;
    }

    // A string, decoded to UTF-8 into `out` (NULL: skipped). *fits is false
    // when it did not fit or held a NUL; the scan goes on either way.
    bool JsonString(Json& j, char* out, size_t outSize, bool* fits)
    {
        bool ok = true;
        size_t n = 0;
        if (!JsonExpect(j, '"'))
        {
            return false;
        }

        auto put = [&](uint32_t c) {
            if (out == NULL)
            {
                return;
            }
            char b[4];
            size_t k;
            if (c == 0)
            {
                ok = false;
                return;
            }
            if (c < 0x80)
            {
                b[0] = (char)c;
                k = 1;
            }
            else if (c < 0x800)
            {
                b[0] = (char)(0xC0 | (c >> 6));
                b[1] = (char)(0x80 | (c & 0x3F));
                k = 2;
            }
            else if (c < 0x10000)
            {
                b[0] = (char)(0xE0 | (c >> 12));
                b[1] = (char)(0x80 | ((c >> 6) & 0x3F));
                b[2] = (char)(0x80 | (c & 0x3F));
                k = 3;
            }
            else
            {
                b[0] = (char)(0xF0 | (c >> 18));
                b[1] = (char)(0x80 | ((c >> 12) & 0x3F));
                b[2] = (char)(0x80 | ((c >> 6) & 0x3F));
                b[3] = (char)(0x80 | (c & 0x3F));
                k = 4;
            }
            if (n + k >= outSize)
            {
                ok = false;
                return;
            }
            memcpy(out + n, b, k);
            n += k;
        };

        while (true)
        {
            if (j.p >= j.end)
            {
                JsonFail(j, "unterminated string");
                return false;
            }
            const unsigned char c = (unsigned char)*j.p++;
            if (c == '"')
            {
                break;
            }
            if (c < 0x20)
            {
                JsonFail(j, "control character in a string");
                return false;
            }
            if (c != '\\')
            {
                // UTF-8 passes through byte by byte.
                if (out != NULL)
                {
                    if (n + 1 >= outSize)
                    {
                        ok = false;
                    }
                    else
                    {
                        out[n++] = (char)c;
                    }
                }
                continue;
            }
            if (j.p >= j.end)
            {
                JsonFail(j, "unterminated escape");
                return false;
            }
            const char e = *j.p++;
            switch (e)
            {
            case '"':
            case '\\':
            case '/':
                put((uint32_t)e);
                break;
            case 'b':
                put('\b');
                break;
            case 'f':
                put('\f');
                break;
            case 'n':
                put('\n');
                break;
            case 'r':
                put('\r');
                break;
            case 't':
                put('\t');
                break;
            case 'u':
            {
                uint32_t u;
                if (!JsonHex4(j, &u))
                {
                    return false;
                }
                if (u >= 0xD800 && u < 0xDC00 && j.end - j.p >= 6 && j.p[0] == '\\' &&
                    j.p[1] == 'u')
                {
                    j.p += 2;
                    uint32_t low;
                    if (!JsonHex4(j, &low))
                    {
                        return false;
                    }
                    if (low >= 0xDC00 && low < 0xE000)
                    {
                        u = 0x10000 + ((u - 0xD800) << 10) + (low - 0xDC00);
                    }
                    else
                    {
                        put(0xFFFD);
                        u = low;
                    }
                }
                if (u >= 0xD800 && u < 0xE000)
                {
                    u = 0xFFFD;
                }
                put(u);
                break;
            }
            default:
                JsonFail(j, "bad escape");
                return false;
            }
        }
        if (out != NULL)
        {
            out[n] = '\0';
        }
        if (fits != NULL)
        {
            *fits = ok;
        }
        return true;
    }

    // A number. *value is set (and true returned in *whole) only for a
    // non-negative integer of at most 18 digits.
    bool JsonNumber(Json& j, uint64_t* value, bool* whole)
    {
        JsonSpace(j);
        bool integer = true;
        if (j.p < j.end && *j.p == '-')
        {
            integer = false;
            j.p++;
        }
        const char* digits = j.p;
        while (j.p < j.end && *j.p >= '0' && *j.p <= '9')
        {
            j.p++;
        }
        const size_t nd = (size_t)(j.p - digits);
        if (nd == 0 || (nd > 1 && digits[0] == '0'))
        {
            JsonFail(j, "bad number");
            return false;
        }
        if (j.p < j.end && *j.p == '.')
        {
            integer = false;
            j.p++;
            const char* f = j.p;
            while (j.p < j.end && *j.p >= '0' && *j.p <= '9')
            {
                j.p++;
            }
            if (j.p == f)
            {
                JsonFail(j, "bad number");
                return false;
            }
        }
        if (j.p < j.end && (*j.p == 'e' || *j.p == 'E'))
        {
            integer = false;
            j.p++;
            if (j.p < j.end && (*j.p == '+' || *j.p == '-'))
            {
                j.p++;
            }
            const char* x = j.p;
            while (j.p < j.end && *j.p >= '0' && *j.p <= '9')
            {
                j.p++;
            }
            if (j.p == x)
            {
                JsonFail(j, "bad number");
                return false;
            }
        }
        if (integer && nd <= 18)
        {
            uint64_t v = 0;
            for (size_t i = 0; i < nd; i++)
            {
                v = v * 10 + (uint64_t)(digits[i] - '0');
            }
            *value = v;
            *whole = true;
        }
        else
        {
            *whole = false;
        }
        return true;
    }

    bool JsonWord(Json& j, const char* word)
    {
        JsonSpace(j);
        const size_t n = strlen(word);
        if ((size_t)(j.end - j.p) < n || memcmp(j.p, word, n) != 0)
        {
            JsonFail(j, "bad literal");
            return false;
        }
        j.p += n;
        return true;
    }

    bool JsonSkip(Json& j);

    bool JsonSkipContainer(Json& j, char open, char close)
    {
        if (++j.depth > kJsonMaxDepth)
        {
            JsonFail(j, "nested too deep");
            return false;
        }
        j.p++; // open
        if (JsonPeek(j, close))
        {
            j.p++;
            j.depth--;
            return true;
        }
        while (true)
        {
            if (open == '{')
            {
                if (!JsonString(j, NULL, 0, NULL) || !JsonExpect(j, ':'))
                {
                    return false;
                }
            }
            if (!JsonSkip(j))
            {
                return false;
            }
            if (JsonPeek(j, ','))
            {
                j.p++;
                continue;
            }
            if (!JsonExpect(j, close))
            {
                return false;
            }
            break;
        }
        j.depth--;
        return true;
    }

    bool JsonSkip(Json& j)
    {
        JsonSpace(j);
        if (j.p >= j.end)
        {
            JsonFail(j, "unexpected end");
            return false;
        }
        switch (*j.p)
        {
        case '{':
            return JsonSkipContainer(j, '{', '}');
        case '[':
            return JsonSkipContainer(j, '[', ']');
        case '"':
            return JsonString(j, NULL, 0, NULL);
        case 't':
            return JsonWord(j, "true");
        case 'f':
            return JsonWord(j, "false");
        case 'n':
            return JsonWord(j, "null");
        default:
        {
            uint64_t v;
            bool whole;
            return JsonNumber(j, &v, &whole);
        }
        }
    }

    // A key; empty when it is longer than any key read here.
    bool JsonKey(Json& j, char* key, size_t keySize)
    {
        bool fits = true;
        if (!JsonString(j, key, keySize, &fits) || !JsonExpect(j, ':'))
        {
            return false;
        }
        if (!fits)
        {
            key[0] = '\0';
        }
        return true;
    }

    // A boolean value, or anything else skipped (and *out left alone).
    bool JsonBool(Json& j, bool* out)
    {
        if (JsonPeek(j, 't'))
        {
            *out = true;
            return JsonWord(j, "true");
        }
        if (JsonPeek(j, 'f'))
        {
            *out = false;
            return JsonWord(j, "false");
        }
        return JsonSkip(j);
    }

    // A string value into out; anything else skipped. *fits false when it was
    // not a string or did not fit.
    bool JsonStringValue(Json& j, char* out, size_t outSize, bool* fits)
    {
        if (JsonPeek(j, '"'))
        {
            return JsonString(j, out, outSize, fits);
        }
        out[0] = '\0';
        *fits = false;
        return JsonSkip(j);
    }

    bool ParseDigest(const char* text, uint8_t out[32])
    {
        static const char kPrefix[] = "sha256:";
        if (strncmp(text, kPrefix, sizeof(kPrefix) - 1) != 0)
        {
            return false;
        }
        const char* hex = text + sizeof(kPrefix) - 1;
        if (strlen(hex) != 64)
        {
            return false;
        }
        for (int i = 0; i < 32; i++)
        {
            const int hi = HexDigit(hex[2 * i]);
            const int lo = HexDigit(hex[2 * i + 1]);
            if (hi < 0 || lo < 0)
            {
                return false;
            }
            out[i] = (uint8_t)((hi << 4) | lo);
        }
        return true;
    }

    bool JsonAsset(Json& j, iUpdateAsset* a, bool* usable)
    {
        memset(a, 0, sizeof(*a));
        bool nameOk = false;
        bool uploaded = true;
        bool haveSize = false;

        if (++j.depth > kJsonMaxDepth)
        {
            JsonFail(j, "nested too deep");
            return false;
        }
        if (!JsonExpect(j, '{'))
        {
            return false;
        }
        if (!JsonPeek(j, '}'))
        {
            while (true)
            {
                char key[32];
                if (!JsonKey(j, key, sizeof(key)))
                {
                    return false;
                }
                bool fits = false;
                if (strcmp(key, "name") == 0)
                {
                    if (!JsonStringValue(j, a->name, sizeof(a->name), &fits))
                    {
                        return false;
                    }
                    nameOk = fits && a->name[0] != '\0';
                }
                else if (strcmp(key, "browser_download_url") == 0)
                {
                    if (!JsonStringValue(j, a->url, sizeof(a->url), &fits))
                    {
                        return false;
                    }
                    if (!fits)
                    {
                        a->url[0] = '\0';
                    }
                }
                else if (strcmp(key, "size") == 0)
                {
                    if (JsonPeek(j, '"') || JsonPeek(j, '{') || JsonPeek(j, '[') ||
                        JsonPeek(j, 't') || JsonPeek(j, 'f') || JsonPeek(j, 'n'))
                    {
                        if (!JsonSkip(j))
                        {
                            return false;
                        }
                    }
                    else
                    {
                        uint64_t v = 0;
                        bool whole = false;
                        if (!JsonNumber(j, &v, &whole))
                        {
                            return false;
                        }
                        if (whole)
                        {
                            a->size = v;
                            haveSize = true;
                        }
                    }
                }
                else if (strcmp(key, "digest") == 0)
                {
                    char digest[80];
                    if (!JsonStringValue(j, digest, sizeof(digest), &fits))
                    {
                        return false;
                    }
                    a->hasDigest = fits && ParseDigest(digest, a->digest);
                }
                else if (strcmp(key, "state") == 0)
                {
                    char state[32];
                    if (!JsonStringValue(j, state, sizeof(state), &fits))
                    {
                        return false;
                    }
                    uploaded = fits && strcmp(state, "uploaded") == 0;
                }
                else if (!JsonSkip(j))
                {
                    return false;
                }

                if (JsonPeek(j, ','))
                {
                    j.p++;
                    continue;
                }
                if (!JsonExpect(j, '}'))
                {
                    return false;
                }
                break;
            }
        }
        else
        {
            j.p++;
        }
        j.depth--;
        *usable = nameOk && uploaded && haveSize;
        return true;
    }
} // namespace

bool iUpdateParseRelease(const char* json, size_t len, iUpdateRelease* out, char* why,
                         size_t whySize)
{
    memset(out, 0, sizeof(*out));
    Json j;
    memset(&j, 0, sizeof(j));
    j.start = json;
    j.p = json;
    j.end = json + len;

    if (len > IUPDATE_JSON_MAX)
    {
        snprintf(why, whySize, "larger than %d bytes", IUPDATE_JSON_MAX);
        return false;
    }

    bool tagOk = false;
    if (JsonExpect(j, '{'))
    {
        j.depth = 1;
        if (JsonPeek(j, '}'))
        {
            j.p++;
        }
        else
        {
            while (!j.failed)
            {
                char key[32];
                if (!JsonKey(j, key, sizeof(key)))
                {
                    break;
                }
                bool fits = false;
                if (strcmp(key, "tag_name") == 0)
                {
                    if (!JsonStringValue(j, out->tag, sizeof(out->tag), &fits))
                    {
                        break;
                    }
                    tagOk = fits;
                }
                else if (strcmp(key, "draft") == 0)
                {
                    if (!JsonBool(j, &out->draft))
                    {
                        break;
                    }
                }
                else if (strcmp(key, "prerelease") == 0)
                {
                    if (!JsonBool(j, &out->prerelease))
                    {
                        break;
                    }
                }
                else if (strcmp(key, "assets") == 0 && JsonPeek(j, '['))
                {
                    j.p++;
                    if (++j.depth > kJsonMaxDepth)
                    {
                        JsonFail(j, "nested too deep");
                        break;
                    }
                    if (JsonPeek(j, ']'))
                    {
                        j.p++;
                    }
                    else
                    {
                        while (!j.failed)
                        {
                            if (JsonPeek(j, '{'))
                            {
                                iUpdateAsset a;
                                bool usable = false;
                                if (!JsonAsset(j, &a, &usable))
                                {
                                    break;
                                }
                                if (usable && out->assetCount < IUPDATE_MAX_ASSETS)
                                {
                                    out->assets[out->assetCount++] = a;
                                }
                            }
                            else if (!JsonSkip(j))
                            {
                                break;
                            }
                            if (JsonPeek(j, ','))
                            {
                                j.p++;
                                continue;
                            }
                            JsonExpect(j, ']');
                            break;
                        }
                    }
                    j.depth--;
                }
                else if (!JsonSkip(j))
                {
                    break;
                }

                if (j.failed)
                {
                    break;
                }
                if (JsonPeek(j, ','))
                {
                    j.p++;
                    continue;
                }
                JsonExpect(j, '}');
                break;
            }
        }
    }
    if (!j.failed)
    {
        JsonSpace(j);
        if (j.p != j.end)
        {
            JsonFail(j, "data after the object");
        }
    }
    if (j.failed)
    {
        snprintf(why, whySize, "%s", j.why);
        return false;
    }
    if (!tagOk)
    {
        snprintf(why, whySize, "no tag_name");
        return false;
    }
    return true;
}

int iUpdateFindAsset(const iUpdateRelease& r, const char* prefix, const char* suffix,
                     iUpdateVersion* version)
{
    const size_t pl = strlen(prefix);
    const size_t sl = strlen(suffix);
    int best = -1;
    iUpdateVersion bestVersion;
    memset(&bestVersion, 0, sizeof(bestVersion));
    for (int i = 0; i < r.assetCount; i++)
    {
        const char* name = r.assets[i].name;
        const size_t n = strlen(name);
        if (n <= pl + sl || strncmp(name, prefix, pl) != 0 || strcmp(name + n - sl, suffix) != 0)
        {
            continue;
        }
        iUpdateVersion v;
        if (!iUpdateParseVersion(name + pl, n - pl - sl, &v))
        {
            continue;
        }
        if (best < 0 || iUpdateCompareVersions(v, bestVersion) > 0)
        {
            best = i;
            bestVersion = v;
        }
    }
    if (best >= 0 && version != NULL)
    {
        *version = bestVersion;
    }
    return best;
}

int iUpdateFindAssetNamed(const iUpdateRelease& r, const char* name)
{
    for (int i = 0; i < r.assetCount; i++)
    {
        if (strcmp(r.assets[i].name, name) == 0)
        {
            return i;
        }
    }
    return -1;
}

// --- SHA256SUMS -----------------------------------------------------------------------

iUpdateSumResult iUpdateFindSum(const char* sums, size_t len, const char* name, uint8_t out[32])
{
    const size_t nameLen = strlen(name);
    bool found = false;
    uint8_t first[32];
    size_t i = 0;
    while (i < len)
    {
        size_t e = i;
        while (e < len && sums[e] != '\n')
        {
            e++;
        }
        size_t lineEnd = e;
        if (lineEnd > i && sums[lineEnd - 1] == '\r')
        {
            lineEnd--;
        }
        const char* line = sums + i;
        const size_t n = lineEnd - i;
        i = e + 1;

        if (n < 67 || line[64] != ' ' || (line[65] != ' ' && line[65] != '*'))
        {
            continue;
        }
        if (n - 66 != nameLen || memcmp(line + 66, name, nameLen) != 0)
        {
            continue;
        }
        uint8_t sum[32];
        bool ok = true;
        for (int k = 0; k < 32 && ok; k++)
        {
            const int hi = HexDigit(line[2 * k]);
            const int lo = HexDigit(line[2 * k + 1]);
            ok = hi >= 0 && lo >= 0;
            sum[k] = (uint8_t)((hi << 4) | (lo & 0xF));
        }
        if (!ok)
        {
            continue;
        }
        if (found && memcmp(first, sum, 32) != 0)
        {
            return IUPDATE_SUM_CONFLICTING;
        }
        memcpy(first, sum, 32);
        found = true;
    }
    if (!found)
    {
        return IUPDATE_SUM_MISSING;
    }
    memcpy(out, first, 32);
    return IUPDATE_SUM_FOUND;
}

// --- SHA-256 ---------------------------------------------------------------------------

iUpdateSha256::iUpdateSha256()
{
    AVSHA* sha = av_sha_alloc();
    if (sha != NULL && av_sha_init(sha, 256) != 0)
    {
        av_free(sha);
        sha = NULL;
    }
    mCtx = sha;
}

iUpdateSha256::~iUpdateSha256()
{
    av_free(mCtx);
}

void iUpdateSha256::Update(const void* data, size_t n)
{
    if (mCtx == NULL)
    {
        return;
    }
    const uint8_t* p = (const uint8_t*)data;
    while (n > 0)
    {
        // av_sha_update takes a size_t in FFmpeg 7 and later, an unsigned int
        // before: chunks keep either right.
        const size_t step = n > (1u << 30) ? (1u << 30) : n;
        av_sha_update((AVSHA*)mCtx, p, (unsigned)step);
        p += step;
        n -= step;
    }
}

void iUpdateSha256::Final(uint8_t out[32])
{
    if (mCtx == NULL)
    {
        memset(out, 0, 32);
        return;
    }
    av_sha_final((AVSHA*)mCtx, out);
}

void iUpdateHex(const uint8_t* bytes, size_t n, char* out, size_t outSize)
{
    static const char kHex[] = "0123456789abcdef";
    size_t k = 0;
    for (size_t i = 0; i < n && k + 2 < outSize; i++)
    {
        out[k++] = kHex[bytes[i] >> 4];
        out[k++] = kHex[bytes[i] & 15];
    }
    if (outSize > 0)
    {
        out[k] = '\0';
    }
}

// --- URLs -------------------------------------------------------------------------------

bool iUpdateHostAllowed(const char* host)
{
    static const char* const kHosts[] = { "api.github.com", "github.com",
                                          "objects.githubusercontent.com",
                                          "release-assets.githubusercontent.com" };
    for (size_t i = 0; i < sizeof(kHosts) / sizeof(kHosts[0]); i++)
    {
        if (strcmp(host, kHosts[i]) == 0)
        {
            return true;
        }
    }
    return false;
}

bool iUpdateSplitUrl(const char* url, char* host, size_t hostSize, char* path, size_t pathSize)
{
    static const char kScheme[] = "https://";
    if (strncmp(url, kScheme, sizeof(kScheme) - 1) != 0)
    {
        return false;
    }
    const char* p = url + sizeof(kScheme) - 1;

    size_t h = 0;
    while (*p != '\0' && *p != '/' && *p != ':' && *p != '?' && *p != '#')
    {
        char c = *p++;
        if (c >= 'A' && c <= 'Z')
        {
            c = (char)(c - 'A' + 'a');
        }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-'))
        {
            return false; // '@' (user information) included
        }
        if (h + 1 >= hostSize)
        {
            return false;
        }
        host[h++] = c;
    }
    host[h] = '\0';
    if (h == 0 || !iUpdateHostAllowed(host))
    {
        return false;
    }
    if (*p == ':')
    {
        if (strncmp(p, ":443", 4) != 0)
        {
            return false;
        }
        p += 4;
        if (*p != '\0' && *p != '/' && *p != '?' && *p != '#')
        {
            return false;
        }
    }

    size_t n = 0;
    if (*p != '/')
    {
        if (pathSize < 2)
        {
            return false;
        }
        path[n++] = '/';
    }
    for (; *p != '\0' && *p != '#'; p++)
    {
        const unsigned char c = (unsigned char)*p;
        if (c <= 0x20 || c >= 0x7F)
        {
            return false;
        }
        if (n + 1 >= pathSize)
        {
            return false;
        }
        path[n++] = (char)c;
    }
    path[n] = '\0';
    return true;
}

// --- HTTP/1.1 over a stream -------------------------------------------------------------

const char* iUpdateNetName(iUpdateNet n)
{
    switch (n)
    {
    case IUPDATE_NET_OK:
        return "ok";
    case IUPDATE_NET_OFFLINE:
        return "offline";
    case IUPDATE_NET_DNS:
        return "dns";
    case IUPDATE_NET_CONNECT:
        return "connect";
    case IUPDATE_NET_TLS:
        return "tls";
    case IUPDATE_NET_CERT:
        return "certificate";
    case IUPDATE_NET_CLOCK:
        return "certificate dates";
    case IUPDATE_NET_TIMEOUT:
        return "timeout";
    case IUPDATE_NET_LOST:
        return "connection lost";
    case IUPDATE_NET_PROTOCOL:
        return "protocol";
    case IUPDATE_NET_CANCELLED:
        return "cancelled";
    case IUPDATE_NET_STOPPED:
        return "stopped";
    default:
        return "error";
    }
}

size_t iUpdateHttpRequestText(const iUpdateRequest& req, char* out, size_t outSize)
{
    const char* fields[] = { req.host, req.path, req.accept };
    for (size_t i = 0; i < 3; i++)
    {
        if (fields[i] == NULL || fields[i][0] == '\0')
        {
            return 0;
        }
        for (const char* c = fields[i]; *c != '\0'; c++)
        {
            if ((unsigned char)*c < 0x20 || (unsigned char)*c == 0x7F ||
                (i < 2 && *c == ' '))
            {
                return 0;
            }
        }
    }
    const int n = snprintf(out, outSize,
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "User-Agent: %s\r\n"
                           "Accept: %s\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           req.path, req.host, kUpdateUserAgent, req.accept);
    if (n <= 0 || (size_t)n >= outSize)
    {
        return 0;
    }
    return (size_t)n;
}

namespace
{
    const size_t kHttpBuffer = 16 * 1024;
    // All of a response's header lines together. github.com's redirect to a
    // download carries a Content-Security-Policy line of over 4 KB.
    const size_t kHttpHeaderTotal = 64 * 1024;
    // What is kept of one header line: a Location with room to spare. A
    // longer line is read to its end and dropped, unless it is one of the
    // headers the client reads.
    const size_t kHttpLine = IUPDATE_LOCATION_MAX + 64;

    class HttpReader
    {
    public:
        HttpReader(iUpdateStream* s)
            : mStream(s), mBuf(kHttpBuffer, '\0'), mPos(0), mLen(0), mEof(false)
        {
        }

        // Some bytes in the buffer, unless the stream has ended.
        iUpdateNet Fill()
        {
            if (mPos < mLen || mEof)
            {
                return IUPDATE_NET_OK;
            }
            size_t got = 0;
            const iUpdateNet net = mStream->Read(&mBuf[0], mBuf.size(), &got);
            if (net != IUPDATE_NET_OK)
            {
                return net;
            }
            mPos = 0;
            mLen = got;
            if (got == 0)
            {
                mEof = true;
            }
            return IUPDATE_NET_OK;
        }

        // One line without its CR LF (a bare LF is taken too); LOST when the
        // stream ends first. Longer than `cap` - 1: PROTOCOL, or, given
        // `whole`, the line's first `cap` - 1 bytes with the rest read and
        // dropped (up to kHttpHeaderTotal in all), and its whole length in
        // *whole (more than *len only then).
        iUpdateNet Line(char* out, size_t cap, size_t* len, size_t* whole = NULL)
        {
            size_t n = 0;
            size_t all = 0;
            while (true)
            {
                const iUpdateNet net = Fill();
                if (net != IUPDATE_NET_OK)
                {
                    return net;
                }
                if (mEof)
                {
                    return IUPDATE_NET_LOST;
                }
                const char c = mBuf[mPos++];
                if (c == '\n')
                {
                    if (all == n && n > 0 && out[n - 1] == '\r')
                    {
                        n--;
                        all--;
                    }
                    out[n] = '\0';
                    *len = n;
                    if (whole != NULL)
                    {
                        *whole = all;
                    }
                    return IUPDATE_NET_OK;
                }
                all++;
                if (n + 1 >= cap)
                {
                    if (whole == NULL || all > kHttpHeaderTotal)
                    {
                        return IUPDATE_NET_PROTOCOL;
                    }
                    continue;
                }
                out[n++] = c;
            }
        }

        // Up to `want` bytes: a pointer into the buffer and its length, 0 at
        // the end of the stream.
        iUpdateNet Some(size_t want, const char** p, size_t* n)
        {
            const iUpdateNet net = Fill();
            if (net != IUPDATE_NET_OK)
            {
                return net;
            }
            if (mEof)
            {
                *n = 0;
                return IUPDATE_NET_OK;
            }
            size_t k = mLen - mPos;
            if (k > want)
            {
                k = want;
            }
            *p = &mBuf[mPos];
            *n = k;
            mPos += k;
            return IUPDATE_NET_OK;
        }

    private:
        iUpdateStream* mStream;
        std::string mBuf;
        size_t mPos;
        size_t mLen;
        bool mEof;
    };

    bool StartsWithNoCase(const char* s, const char* prefix)
    {
        for (; *prefix != '\0'; s++, prefix++)
        {
            char a = *s;
            char b = *prefix;
            if (a >= 'A' && a <= 'Z')
            {
                a = (char)(a - 'A' + 'a');
            }
            if (a != b)
            {
                return false;
            }
        }
        return true;
    }

    void Detail(char* detail, size_t detailSize, const char* fmt, ...)
    {
        if (detail == NULL || detailSize == 0)
        {
            return;
        }
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(detail, detailSize, fmt, ap);
        va_end(ap);
    }

    // The value after "name:", trimmed.
    const char* HeaderValue(char* line, size_t nameLen)
    {
        char* v = line + nameLen + 1;
        while (*v == ' ' || *v == '\t')
        {
            v++;
        }
        size_t n = strlen(v);
        while (n > 0 && (v[n - 1] == ' ' || v[n - 1] == '\t'))
        {
            v[--n] = '\0';
        }
        return v;
    }

    bool ParseDecimal(const char* s, int64_t* out)
    {
        if (*s == '\0')
        {
            return false;
        }
        int64_t v = 0;
        for (size_t i = 0; s[i] != '\0'; i++)
        {
            if (s[i] < '0' || s[i] > '9' || i >= 18)
            {
                return false;
            }
            v = v * 10 + (s[i] - '0');
        }
        *out = v;
        return true;
    }

    iUpdateNet Deliver(iUpdateSink* sink, const char* p, size_t n)
    {
        if (sink->Cancelled())
        {
            return IUPDATE_NET_CANCELLED;
        }
        if (n > 0 && !sink->OnBody(p, n))
        {
            return sink->Cancelled() ? IUPDATE_NET_CANCELLED : IUPDATE_NET_STOPPED;
        }
        return IUPDATE_NET_OK;
    }
} // namespace

iUpdateNet iUpdateHttpGet(iUpdateStream* stream, const iUpdateRequest& req, iUpdateSink* sink,
                          char* detail, size_t detailSize)
{
    char request[IUPDATE_LOCATION_MAX + 512];
    const size_t requestLen = iUpdateHttpRequestText(req, request, sizeof(request));
    if (requestLen == 0)
    {
        Detail(detail, detailSize, "request does not fit");
        return IUPDATE_NET_PROTOCOL;
    }
    iUpdateNet net = stream->Write(request, requestLen);
    if (net != IUPDATE_NET_OK)
    {
        Detail(detail, detailSize, "sending the request: %s", iUpdateNetName(net));
        return net;
    }

    HttpReader r(stream);
    std::string lineBuf(kHttpLine, '\0');
    char* line = &lineBuf[0];
    size_t len = 0;
    size_t headerBytes = 0;

    int status = 0;
    int64_t contentLength = -1;
    bool chunked = false;
    std::string location;
    bool haveLocation = false;

    // Interim 1xx responses come before the real one.
    while (true)
    {
        net = r.Line(line, 1024, &len);
        if (net != IUPDATE_NET_OK)
        {
            Detail(detail, detailSize, "reading the status line: %s", iUpdateNetName(net));
            return net;
        }
        headerBytes += len + 2;
        if (len < 12 || strncmp(line, "HTTP/1.", 7) != 0 || line[7] < '0' || line[7] > '9' ||
            line[8] != ' ' || line[9] < '1' || line[9] > '5' || line[10] < '0' ||
            line[10] > '9' || line[11] < '0' || line[11] > '9' ||
            (len > 12 && line[12] != ' '))
        {
            line[len < 40 ? len : 40] = '\0';
            Detail(detail, detailSize, "not an HTTP status line: \"%s\"", line);
            return IUPDATE_NET_PROTOCOL;
        }
        status = (line[9] - '0') * 100 + (line[10] - '0') * 10 + (line[11] - '0');

        contentLength = -1;
        chunked = false;
        haveLocation = false;
        location.clear();
        bool otherCoding = false;
        for (int lines = 0;; lines++)
        {
            size_t whole = 0;
            net = r.Line(line, kHttpLine, &len, &whole);
            if (net != IUPDATE_NET_OK)
            {
                Detail(detail, detailSize, "reading the headers: %s", iUpdateNetName(net));
                return net;
            }
            headerBytes += whole + 2;
            if (headerBytes > kHttpHeaderTotal || lines > 200)
            {
                Detail(detail, detailSize, "headers too long");
                return IUPDATE_NET_PROTOCOL;
            }
            if (len == 0)
            {
                break;
            }
            if (whole > len)
            {
                // Cut short: dropped, unless the client reads it.
                if (StartsWithNoCase(line, "content-length:") ||
                    StartsWithNoCase(line, "transfer-encoding:") ||
                    StartsWithNoCase(line, "content-encoding:") ||
                    StartsWithNoCase(line, "location:"))
                {
                    line[24] = '\0';
                    Detail(detail, detailSize, "header too long: \"%s...\"", line);
                    return IUPDATE_NET_PROTOCOL;
                }
                continue;
            }
            if (StartsWithNoCase(line, "content-length:"))
            {
                int64_t v;
                if (!ParseDecimal(HeaderValue(line, 14), &v) ||
                    (contentLength >= 0 && contentLength != v))
                {
                    Detail(detail, detailSize, "bad Content-Length");
                    return IUPDATE_NET_PROTOCOL;
                }
                contentLength = v;
            }
            else if (StartsWithNoCase(line, "transfer-encoding:"))
            {
                const char* v = HeaderValue(line, 17);
                const size_t n = strlen(v);
                if (n >= 7 && StartsWithNoCase(v + n - 7, "chunked") &&
                    (n == 7 || v[n - 8] == ' ' || v[n - 8] == ','))
                {
                    chunked = true;
                }
                // Anything before the chunking (gzip, chunked twice) is a
                // coding this client never asked for.
                if (n != 7)
                {
                    otherCoding = true;
                }
            }
            else if (StartsWithNoCase(line, "content-encoding:"))
            {
                const char* v = HeaderValue(line, 16);
                if (v[0] != '\0' && !StartsWithNoCase(v, "identity"))
                {
                    otherCoding = true;
                }
            }
            else if (StartsWithNoCase(line, "location:"))
            {
                const char* v = HeaderValue(line, 8);
                if (strlen(v) >= IUPDATE_LOCATION_MAX)
                {
                    Detail(detail, detailSize, "Location too long");
                    return IUPDATE_NET_PROTOCOL;
                }
                location = v;
                haveLocation = true;
            }
        }
        if (otherCoding)
        {
            Detail(detail, detailSize, "a content or transfer coding this client did not ask for");
            return IUPDATE_NET_PROTOCOL;
        }
        if (status >= 100 && status < 200)
        {
            if (status == 101)
            {
                Detail(detail, detailSize, "unexpected 101");
                return IUPDATE_NET_PROTOCOL;
            }
            continue;
        }
        break;
    }

    if (!sink->OnResponse(status, chunked ? -1 : contentLength,
                          haveLocation ? location.c_str() : NULL))
    {
        return sink->Cancelled() ? IUPDATE_NET_CANCELLED : IUPDATE_NET_OK;
    }
    if (status == 204 || status == 304)
    {
        return IUPDATE_NET_OK;
    }

    if (chunked)
    {
        while (true)
        {
            net = r.Line(line, 128, &len);
            if (net != IUPDATE_NET_OK)
            {
                Detail(detail, detailSize, "reading a chunk size: %s", iUpdateNetName(net));
                return net;
            }
            uint64_t size = 0;
            size_t i = 0;
            for (; i < len && HexDigit(line[i]) >= 0; i++)
            {
                if (i >= 12)
                {
                    Detail(detail, detailSize, "chunk too large");
                    return IUPDATE_NET_PROTOCOL;
                }
                size = (size << 4) | (uint64_t)HexDigit(line[i]);
            }
            while (i < len && (line[i] == ' ' || line[i] == '\t'))
            {
                i++;
            }
            if (i == 0 || (i < len && line[i] != ';'))
            {
                Detail(detail, detailSize, "bad chunk size");
                return IUPDATE_NET_PROTOCOL;
            }
            if (size == 0)
            {
                // The trailer, up to its empty line.
                for (int lines = 0;; lines++)
                {
                    size_t whole = 0;
                    net = r.Line(line, kHttpLine, &len, &whole);
                    if (net != IUPDATE_NET_OK)
                    {
                        Detail(detail, detailSize, "reading the trailer: %s", iUpdateNetName(net));
                        return net;
                    }
                    if (len == 0)
                    {
                        return IUPDATE_NET_OK;
                    }
                    if (lines > 100)
                    {
                        Detail(detail, detailSize, "trailer too long");
                        return IUPDATE_NET_PROTOCOL;
                    }
                }
            }
            while (size > 0)
            {
                const char* p = NULL;
                size_t n = 0;
                net = r.Some(size > kHttpBuffer ? kHttpBuffer : (size_t)size, &p, &n);
                if (net != IUPDATE_NET_OK)
                {
                    Detail(detail, detailSize, "reading a chunk: %s", iUpdateNetName(net));
                    return net;
                }
                if (n == 0)
                {
                    Detail(detail, detailSize, "the connection ended inside a chunk");
                    return IUPDATE_NET_LOST;
                }
                net = Deliver(sink, p, n);
                if (net != IUPDATE_NET_OK)
                {
                    return net;
                }
                size -= n;
            }
            net = r.Line(line, 8, &len);
            if (net != IUPDATE_NET_OK || len != 0)
            {
                Detail(detail, detailSize, "no line end after a chunk");
                return net != IUPDATE_NET_OK ? net : IUPDATE_NET_PROTOCOL;
            }
        }
    }

    uint64_t left = contentLength >= 0 ? (uint64_t)contentLength : UINT64_MAX;
    while (left > 0)
    {
        const char* p = NULL;
        size_t n = 0;
        net = r.Some(left > kHttpBuffer ? kHttpBuffer : (size_t)left, &p, &n);
        if (net != IUPDATE_NET_OK)
        {
            Detail(detail, detailSize, "reading the body: %s", iUpdateNetName(net));
            return net;
        }
        if (n == 0)
        {
            if (contentLength >= 0)
            {
                Detail(detail, detailSize, "the connection ended %llu bytes early",
                       (unsigned long long)left);
                return IUPDATE_NET_LOST;
            }
            return IUPDATE_NET_OK;
        }
        net = Deliver(sink, p, n);
        if (net != IUPDATE_NET_OK)
        {
            return net;
        }
        if (contentLength >= 0)
        {
            left -= n;
        }
    }
    return IUPDATE_NET_OK;
}

// --- files -----------------------------------------------------------------------------------

bool iUpdateReplaceFile(const char* from, const char* to, char* why, size_t whySize)
{
    if (iHostRenameReplace(from, to))
    {
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s", to);
        char* slash = strrchr(dir, '/');
        if (slash != NULL)
        {
            *slash = '\0';
            iHostFlushDir(dir);
        }
        return true;
    }

    // iHostRenameReplace puts the old file back when the new one will not go
    // in. Where even that failed, the old file is at <to>.bak (iHostSwitch).
    if (!iHostPathExists(to))
    {
        char bak[1024];
        snprintf(bak, sizeof(bak), "%s.bak", to);
        if (iHostPathExists(bak) && !iHostRenameReplace(bak, to))
        {
            snprintf(why, whySize,
                     "The new file could not be put in place, and the old one is now %s. Rename "
                     "it back to %s.",
                     bak, to);
            return false;
        }
    }
    snprintf(why, whySize,
             "The new file could not be put in place of %s. The installed version is "
             "unchanged.",
             to);
    return false;
}

// --- the state machine -------------------------------------------------------------------------

const char* iUpdateStateName(iUpdateState s)
{
    switch (s)
    {
    case IUPDATE_IDLE:
        return "idle";
    case IUPDATE_CHECKING:
        return "checking";
    case IUPDATE_UP_TO_DATE:
        return "up to date";
    case IUPDATE_AVAILABLE:
        return "available";
    case IUPDATE_DOWNLOADING:
        return "downloading";
    case IUPDATE_VERIFYING:
        return "verifying";
    case IUPDATE_INSTALLING:
        return "installing";
    case IUPDATE_DONE_RESTART:
        return "done";
    default:
        return "error";
    }
}

namespace
{
    std::mutex sLock;
    iUpdatePlatform* sPlatform;
    char sRepo[128];
    iUpdateVersion sCurrent;
    iUpdateStatus sStatus;
    std::thread sWorker;
    std::atomic<bool> sBusy{ false };
    std::atomic<bool> sCancel{ false };
    std::atomic<uint64_t> sDone{ 0 };

    // What the check found, for the download.
    iUpdateAsset sAsset;
    iUpdateVersion sAssetVersion;
    char sSumsUrl[IUPDATE_URL_MAX];

    void Log(const char* fmt, ...)
    {
        char text[600];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(text, sizeof(text), fmt, ap);
        va_end(ap);
        printf("tssm: update: %s\n", text);
        fflush(stdout);
    }

    // The state, and the message the player sees with it.
    void Set(iUpdateState state, const char* fmt, ...)
    {
        char text[sizeof(sStatus.message)];
        text[0] = '\0';
        if (fmt != NULL)
        {
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(text, sizeof(text), fmt, ap);
            va_end(ap);
        }
        {
            std::lock_guard<std::mutex> lock(sLock);
            sStatus.state = state;
            snprintf(sStatus.message, sizeof(sStatus.message), "%s", text);
        }
        Log("%s%s%s", iUpdateStateName(state), text[0] != '\0' ? ": " : "", text);
    }

    void FormatSize(uint64_t bytes, char* out, size_t outSize)
    {
        snprintf(out, outSize, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    }

    // The player's words for a failed connection.
    const char* NetMessage(iUpdateNet net)
    {
        switch (net)
        {
        case IUPDATE_NET_OFFLINE:
            return "Not connected to the internet. Connect, then try again.";
        case IUPDATE_NET_DNS:
            return "Could not find the update server. Check the internet connection, then try "
                   "again.";
        case IUPDATE_NET_CONNECT:
            return "Could not connect to the update server. Try again later.";
        case IUPDATE_NET_TLS:
            return "The secure connection to the update server failed. Try again later.";
        case IUPDATE_NET_CERT:
            return "The update server's certificate was not accepted, so nothing was "
                   "downloaded.";
        case IUPDATE_NET_CLOCK:
            return "The update server's certificate was not accepted for its dates. Check this "
                   "system's date and time, then try again.";
        case IUPDATE_NET_TIMEOUT:
            return "The update server stopped answering. Try again later.";
        case IUPDATE_NET_LOST:
            return "The connection to the update server broke off. Try again.";
        case IUPDATE_NET_PROTOCOL:
            return "The update server's answer could not be read.";
        default:
            return "Something went wrong talking to the update server. Try again later.";
        }
    }

    // Redirects, each one checked; the caller's sink gets the final response.
    class RedirectSink : public iUpdateSink
    {
    public:
        explicit RedirectSink(iUpdateSink* inner) : mInner(inner), mStatus(0), mRedirect(false)
        {
        }
        bool OnResponse(int status, int64_t contentLength, const char* location) override
        {
            mStatus = status;
            if (status == 301 || status == 302 || status == 303 || status == 307 ||
                status == 308)
            {
                mRedirect = true;
                mLocation = location != NULL ? location : "";
                return false;
            }
            return mInner->OnResponse(status, contentLength, location);
        }
        bool OnBody(const void* data, size_t n) override { return mInner->OnBody(data, n); }
        bool Cancelled() override { return mInner->Cancelled(); }

        iUpdateSink* mInner;
        int mStatus;
        bool mRedirect;
        std::string mLocation;
    };

    enum FetchResult
    {
        FETCH_OK,       // *status is the final response's
        FETCH_NET,      // *net says why
        FETCH_BAD_HOST, // a redirect to somewhere this updater does not go
    };

    FetchResult Fetch(const char* url, const char* accept, iUpdateSink* sink, int* status,
                      iUpdateNet* net)
    {
        std::string current = url;
        for (int hop = 0;; hop++)
        {
            char host[64];
            std::string path(IUPDATE_LOCATION_MAX, '\0');
            if (!iUpdateSplitUrl(current.c_str(), host, sizeof(host), &path[0], path.size()))
            {
                const size_t q = current.find('?');
                Log("refused %s: not https on an allowed host",
                    current.substr(0, q == std::string::npos ? current.size() : q).c_str());
                return FETCH_BAD_HOST;
            }
            path.resize(strlen(path.c_str()));

            // The signed query of a download link is long and says nothing.
            const size_t q = path.find('?');
            Log("GET https://%s%s%s", host, path.substr(0, q).c_str(),
                q != std::string::npos ? "?..." : "");

            RedirectSink rs(sink);
            iUpdateRequest req;
            req.host = host;
            req.path = path.c_str();
            req.accept = accept;
            req.timeoutMs = kUpdateTimeoutMs;
            char detail[256];
            detail[0] = '\0';
            *net = sPlatform->Get(req, &rs, detail, sizeof(detail));
            if (*net != IUPDATE_NET_OK)
            {
                Log("%s%s%s", iUpdateNetName(*net), detail[0] != '\0' ? ": " : "", detail);
                return FETCH_NET;
            }
            if (!rs.mRedirect)
            {
                Log("HTTP %d", rs.mStatus);
                *status = rs.mStatus;
                return FETCH_OK;
            }
            if (hop >= kUpdateMaxRedirects)
            {
                Log("more than %d redirects", kUpdateMaxRedirects);
                *net = IUPDATE_NET_PROTOCOL;
                return FETCH_NET;
            }
            Log("HTTP %d, redirected", rs.mStatus);
            if (rs.mLocation.compare(0, 1, "/") == 0 && rs.mLocation.compare(0, 2, "//") != 0)
            {
                current = std::string("https://") + host + rs.mLocation;
            }
            else
            {
                current = rs.mLocation;
            }
        }
    }

    class MemorySink : public iUpdateSink
    {
    public:
        explicit MemorySink(size_t limit) : mLimit(limit), mStatus(0), mTooBig(false) {}
        bool OnResponse(int status, int64_t contentLength, const char*) override
        {
            mStatus = status;
            mData.clear();
            if (status != 200)
            {
                return false;
            }
            if (contentLength > (int64_t)mLimit)
            {
                mTooBig = true;
                return false;
            }
            return true;
        }
        bool OnBody(const void* data, size_t n) override
        {
            if (mData.size() + n > mLimit)
            {
                mTooBig = true;
                return false;
            }
            mData.append((const char*)data, n);
            return true;
        }
        bool Cancelled() override { return sCancel.load(); }

        size_t mLimit;
        int mStatus;
        bool mTooBig;
        std::string mData;
    };

    class FileSink : public iUpdateSink
    {
    public:
        FileSink(const char* path, uint64_t expected)
            : mPath(path), mExpected(expected), mFile(NULL), mWritten(0), mWriteFailed(false),
              mWrongLength(false), mTooBig(false)
        {
        }
        ~FileSink() { Close(); }

        bool OnResponse(int status, int64_t contentLength, const char*) override
        {
            if (status != 200)
            {
                return false;
            }
            if (contentLength >= 0 && (uint64_t)contentLength != mExpected)
            {
                mWrongLength = true;
                return false;
            }
            Close();
            iHostRemoveFile(mPath.c_str());
            mFile = fopen(mPath.c_str(), "wb");
            if (mFile == NULL)
            {
                mWriteFailed = true;
                return false;
            }
            setvbuf(mFile, NULL, _IOFBF, 64 * 1024);
            mWritten = 0;
            sDone = 0;
            return true;
        }
        bool OnBody(const void* data, size_t n) override
        {
            if (mWritten + n > mExpected || mWritten + n > kUpdateMaxBytes)
            {
                mTooBig = true;
                return false;
            }
            if (fwrite(data, 1, n, mFile) != n)
            {
                mWriteFailed = true;
                return false;
            }
            mWritten += n;
            sDone = mWritten;
            return true;
        }
        bool Cancelled() override { return sCancel.load(); }

        // Flushed to storage and closed; false when either failed.
        bool Finish()
        {
            if (mFile == NULL)
            {
                return false;
            }
            const bool flushed = iHostFlushFile(mFile);
            const bool closed = fclose(mFile) == 0;
            mFile = NULL;
            if (!flushed || !closed)
            {
                mWriteFailed = true;
            }
            return flushed && closed;
        }
        void Close()
        {
            if (mFile != NULL)
            {
                fclose(mFile);
                mFile = NULL;
            }
        }

        std::string mPath;
        uint64_t mExpected;
        FILE* mFile;
        uint64_t mWritten;
        bool mWriteFailed;
        bool mWrongLength;
        bool mTooBig;
    };

    // The message for a fetch that did not end in a 200.
    void FailFetch(FetchResult fr, iUpdateNet net, int status, iUpdateState cancelTo,
                   const char* cancelText, bool notFoundIsNoInfo)
    {
        if (fr == FETCH_BAD_HOST)
        {
            Set(IUPDATE_ERROR, "The update server pointed somewhere this updater does not go, "
                               "so nothing was downloaded. Nothing was changed.");
        }
        else if (fr == FETCH_NET && net == IUPDATE_NET_CANCELLED)
        {
            Set(cancelTo, "%s", cancelText);
        }
        else if (fr == FETCH_NET)
        {
            Set(IUPDATE_ERROR, "%s", NetMessage(net));
        }
        else if (status == 404 && notFoundIsNoInfo)
        {
            Set(IUPDATE_ERROR, "No update information found.");
        }
        else if (status == 403 || status == 429)
        {
            Set(IUPDATE_ERROR, "The update server is not taking more checks from this network "
                               "for now. Try again in an hour.");
        }
        else
        {
            Set(IUPDATE_ERROR, "The update server answered with an error (HTTP %d). Try again "
                               "later.",
                status);
        }
    }

    // --- the jobs ---------------------------------------------------------------------------

    void CheckJob()
    {
        sDone = 0;
        char url[256];
        snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest", sRepo);

        MemorySink sink(IUPDATE_JSON_MAX);
        int status = 0;
        iUpdateNet net = IUPDATE_NET_OK;
        const FetchResult fr = Fetch(url, "application/vnd.github+json", &sink, &status, &net);
        // Before the transfer's own result: a body without a length that
        // grows past the limit ends the transfer as "stopped".
        if (sink.mTooBig && net != IUPDATE_NET_CANCELLED)
        {
            Set(IUPDATE_ERROR, "The update information could not be read.");
            Log("the release's JSON is larger than %d bytes", IUPDATE_JSON_MAX);
            return;
        }
        if (fr != FETCH_OK || status != 200)
        {
            FailFetch(fr, net, status, IUPDATE_IDLE, "Cancelled.", true);
            return;
        }

        iUpdateRelease* release = new iUpdateRelease;
        char why[160];
        if (!iUpdateParseRelease(sink.mData.data(), sink.mData.size(), release, why,
                                 sizeof(why)))
        {
            delete release;
            Set(IUPDATE_ERROR, "The update information could not be read.");
            Log("the release's JSON: %s", why);
            return;
        }
        Log("latest release %s: %d files%s%s", release->tag, release->assetCount,
            release->draft ? ", draft" : "", release->prerelease ? ", pre-release" : "");
        if (release->draft || release->prerelease)
        {
            delete release;
            Set(IUPDATE_ERROR, "No update information found.");
            return;
        }

        char current[32];
        iUpdateFormatVersion(sCurrent, current, sizeof(current));
        iUpdateVersion version;
        const int index =
            iUpdateFindAsset(*release, kUpdateAssetPrefix, sPlatform->AssetSuffix(), &version);
        if (index < 0)
        {
            Log("no %s<version>%s in %s", kUpdateAssetPrefix, sPlatform->AssetSuffix(),
                release->tag);
            delete release;
            Set(IUPDATE_UP_TO_DATE, "You have the newest version (%s).", current);
            return;
        }
        const iUpdateAsset asset = release->assets[index];
        char latest[32];
        iUpdateFormatVersion(version, latest, sizeof(latest));
        Log("%s: version %s, %llu bytes", asset.name, latest, (unsigned long long)asset.size);
        if (iUpdateCompareVersions(version, sCurrent) <= 0)
        {
            delete release;
            Set(IUPDATE_UP_TO_DATE, "You have the newest version (%s).", current);
            return;
        }

        // Only this repository's own release files.
        char prefix[256];
        snprintf(prefix, sizeof(prefix), "https://github.com/%s/releases/download/", sRepo);
        const int sums = iUpdateFindAssetNamed(*release, kUpdateSumsName);
        const bool assetUrlOk = strncmp(asset.url, prefix, strlen(prefix)) == 0;
        const bool sumsUrlOk =
            sums >= 0 && strncmp(release->assets[sums].url, prefix, strlen(prefix)) == 0;
        if (sums >= 0)
        {
            snprintf(sSumsUrl, sizeof(sSumsUrl), "%s", release->assets[sums].url);
        }
        delete release;

        if (!assetUrlOk)
        {
            Set(IUPDATE_ERROR, "The update information could not be read.");
            Log("%s is not under %s", asset.name, prefix);
            return;
        }
        if (asset.size == 0 || asset.size > kUpdateMaxBytes)
        {
            Set(IUPDATE_ERROR, "Version %s is out, but its file is larger than this updater "
                               "takes. Nothing was changed.",
                latest);
            return;
        }
        if (!sumsUrlOk)
        {
            Set(IUPDATE_ERROR, "Version %s is out, but its release has no checksum file, so it "
                               "cannot be installed safely. Nothing was changed.",
                latest);
            return;
        }

        char blocked[256];
        blocked[0] = '\0';
        const bool allowed = sPlatform->InstallAllowed(blocked, sizeof(blocked));
        {
            std::lock_guard<std::mutex> lock(sLock);
            sAsset = asset;
            sAssetVersion = version;
            snprintf(sStatus.latestVersion, sizeof(sStatus.latestVersion), "%s", latest);
            snprintf(sStatus.assetName, sizeof(sStatus.assetName), "%s", asset.name);
            sStatus.assetSize = asset.size;
            sStatus.installAllowed = allowed;
            snprintf(sStatus.installBlocked, sizeof(sStatus.installBlocked), "%s", blocked);
        }
        char size[32];
        FormatSize(asset.size, size, sizeof(size));
        Set(IUPDATE_AVAILABLE, "Version %s is available (%s).", latest, size);
    }

    bool HashFile(const char* path, uint8_t out[32], bool* cancelled)
    {
        *cancelled = false;
        FILE* f = fopen(path, "rb");
        if (f == NULL)
        {
            return false;
        }
        iUpdateSha256 sha;
        std::string buf(64 * 1024, '\0');
        uint64_t total = 0;
        bool ok = sha.Ok();
        while (ok)
        {
            if (sCancel.load())
            {
                *cancelled = true;
                ok = false;
                break;
            }
            const size_t n = fread(&buf[0], 1, buf.size(), f);
            if (n == 0)
            {
                ok = ferror(f) == 0;
                break;
            }
            sha.Update(buf.data(), n);
            total += n;
            sDone = total;
        }
        fclose(f);
        if (ok)
        {
            sha.Final(out);
        }
        return ok;
    }

    void DownloadJob()
    {
        iUpdateAsset asset;
        iUpdateVersion version;
        {
            std::lock_guard<std::mutex> lock(sLock);
            asset = sAsset;
            version = sAssetVersion;
        }
        char latest[32];
        iUpdateFormatVersion(version, latest, sizeof(latest));
        const char* cancelled = "Download cancelled. Nothing was changed.";

        char path[1024];
        char why[512];
        why[0] = '\0';
        if (!sPlatform->DownloadPath(path, sizeof(path), why, sizeof(why)))
        {
            Set(IUPDATE_ERROR, "%s", why);
            return;
        }

        // Room for the whole file before anything is written.
        {
            char dir[1024];
            snprintf(dir, sizeof(dir), "%s", path);
            char* slash = strrchr(dir, '/');
            if (slash != NULL)
            {
                *slash = '\0';
            }
            U64 avail = 0;
            if (slash != NULL && iHostFreeBytes(dir, &avail) &&
                (uint64_t)avail < asset.size + 1024 * 1024)
            {
                char need[32];
                FormatSize(asset.size, need, sizeof(need));
                Set(IUPDATE_ERROR, "Not enough free space for the download (it needs %s). "
                                   "Nothing was changed.",
                    need);
                return;
            }
        }

        // The checksum first: without its line nothing is downloaded.
        MemorySink sums(IUPDATE_SUMS_MAX);
        int status = 0;
        iUpdateNet net = IUPDATE_NET_OK;
        FetchResult fr = Fetch(sSumsUrl, "application/octet-stream", &sums, &status, &net);
        // Too big is "no usable line", whether the transfer was stopped for it
        // or not (as for the release's JSON above).
        const bool sumsTooBig = sums.mTooBig && net != IUPDATE_NET_CANCELLED;
        if (!sumsTooBig && (fr != FETCH_OK || status != 200))
        {
            FailFetch(fr, net, status, IUPDATE_AVAILABLE, cancelled, false);
            return;
        }
        uint8_t expected[32];
        const iUpdateSumResult sr = sumsTooBig ? IUPDATE_SUM_MISSING :
                                                   iUpdateFindSum(sums.mData.data(),
                                                                  sums.mData.size(), asset.name,
                                                                  expected);
        if (sr != IUPDATE_SUM_FOUND)
        {
            Log("%s: %s", kUpdateSumsName,
                sr == IUPDATE_SUM_CONFLICTING ? "two different lines for the file" :
                                                "no line for the file");
            Set(IUPDATE_ERROR, "The release's checksum file has no usable line for %s, so "
                               "nothing was downloaded. Nothing was changed.",
                asset.name);
            return;
        }
        char hex[65];
        iUpdateHex(expected, 32, hex, sizeof(hex));
        Log("%s says %s", kUpdateSumsName, hex);
        if (asset.hasDigest && memcmp(asset.digest, expected, 32) != 0)
        {
            Set(IUPDATE_ERROR, "The release's checksums disagree, so nothing was downloaded. "
                               "Nothing was changed.");
            return;
        }

        sDone = 0;
        Set(IUPDATE_DOWNLOADING, "Downloading version %s...", latest);
        {
            FileSink file(path, asset.size);
            fr = Fetch(asset.url, "application/octet-stream", &file, &status, &net);
            const bool finished = fr == FETCH_OK && status == 200 && !file.mWrongLength &&
                                  file.mWritten == asset.size && file.Finish();
            file.Close();
            if (!finished)
            {
                iHostRemoveFile(path);
                if (file.mWriteFailed)
                {
                    Log("could not write %s", path);
                    Set(IUPDATE_ERROR, "Could not save the download (the storage may be full). "
                                       "Nothing was changed.");
                }
                else if (file.mTooBig || file.mWrongLength)
                {
                    Set(IUPDATE_ERROR, "The download was not the size the release lists, so it "
                                       "was deleted. Nothing was changed.");
                }
                else if (fr == FETCH_OK && status == 200)
                {
                    Set(IUPDATE_ERROR, "The download was incomplete and was deleted. Nothing was "
                                       "changed.");
                }
                else
                {
                    FailFetch(fr, net, status, IUPDATE_AVAILABLE, cancelled, false);
                }
                return;
            }
            Log("downloaded %llu bytes to %s", (unsigned long long)file.mWritten, path);
        }

        // From storage, so what is checked is what is installed.
        sDone = 0;
        Set(IUPDATE_VERIFYING, "Checking the download...");
        uint8_t actual[32];
        bool wasCancelled = false;
        if (!HashFile(path, actual, &wasCancelled))
        {
            iHostRemoveFile(path);
            if (wasCancelled)
            {
                Set(IUPDATE_AVAILABLE, "%s", cancelled);
            }
            else
            {
                Set(IUPDATE_ERROR, "Could not read the download back to check it, so it was "
                                   "deleted. Nothing was changed.");
            }
            return;
        }
        iUpdateHex(actual, 32, hex, sizeof(hex));
        Log("the download is %s", hex);
        if (memcmp(actual, expected, 32) != 0)
        {
            iHostRemoveFile(path);
            Set(IUPDATE_ERROR, "The downloaded file did not match its checksum and was deleted. "
                               "Nothing was changed.");
            return;
        }
        if (sCancel.load())
        {
            iHostRemoveFile(path);
            Set(IUPDATE_AVAILABLE, "%s", cancelled);
            return;
        }

        Set(IUPDATE_INSTALLING, "Installing version %s...", latest);
        why[0] = '\0';
        const bool installed = sPlatform->Install(path, version, why, sizeof(why));
        if (iHostPathExists(path))
        {
            iHostRemoveFile(path);
        }
        if (!installed)
        {
            Set(IUPDATE_ERROR, "%s", why[0] != '\0' ? why :
                                                      "The update could not be installed. The "
                                                      "installed version is unchanged.");
            return;
        }
        const bool restart = sPlatform->CanRestart();
        {
            std::lock_guard<std::mutex> lock(sLock);
            sStatus.canRestart = restart;
        }
        Set(IUPDATE_DONE_RESTART, "Version %s is installed. Restart the game to use it.", latest);
    }

    void Start(void (*job)())
    {
        if (sWorker.joinable())
        {
            sWorker.join();
        }
        sCancel = false;
        sBusy = true;
        sWorker = std::thread([job]() {
            job();
            sBusy = false;
        });
    }

    iUpdateState State()
    {
        std::lock_guard<std::mutex> lock(sLock);
        return sStatus.state;
    }
} // namespace

bool iUpdateInit(iUpdatePlatform* platform, const iUpdateSettings& settings)
{
    // Again (the tests): from idle, with the new platform and version.
    if (platform == NULL || sBusy.load())
    {
        return false;
    }
    iUpdateWait();
    sPlatform = NULL;
    const char* repo = settings.repo != NULL ? settings.repo : "";
    const char* slash = strchr(repo, '/');
    bool repoOk = slash != NULL && slash != repo && slash[1] != '\0' &&
                  strchr(slash + 1, '/') == NULL && strlen(repo) < sizeof(sRepo);
    for (const char* c = repo; repoOk && *c != '\0'; c++)
    {
        repoOk = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
                 *c == '-' || *c == '_' || *c == '.' || *c == '/';
    }
    const char* version = settings.currentVersion != NULL ? settings.currentVersion : "";
    iUpdateVersion current;
    if (!repoOk || !iUpdateParseVersion(version, strlen(version), &current))
    {
        Log("off: the build's repository or version (\"%s\") is not usable", version);
        return false;
    }

    snprintf(sRepo, sizeof(sRepo), "%s", repo);
    sCurrent = current;
    memset(&sStatus, 0, sizeof(sStatus));
    sStatus.state = IUPDATE_IDLE;
    iUpdateFormatVersion(current, sStatus.currentVersion, sizeof(sStatus.currentVersion));
    sPlatform = platform;
    Log("ready: version %s, this system's file %s<version>%s", sStatus.currentVersion,
        kUpdateAssetPrefix, platform->AssetSuffix());
    return true;
}

bool iUpdateReady()
{
    return sPlatform != NULL;
}

bool iUpdateCanCheck()
{
    if (sPlatform == NULL || sBusy.load())
    {
        return false;
    }
    const iUpdateState s = State();
    return s == IUPDATE_IDLE || s == IUPDATE_UP_TO_DATE || s == IUPDATE_AVAILABLE ||
           s == IUPDATE_ERROR;
}

void iUpdateCheck()
{
    if (!iUpdateCanCheck())
    {
        return;
    }
    Set(IUPDATE_CHECKING, "Checking for updates...");
    Start(CheckJob);
}

bool iUpdateCanDownload()
{
    if (sPlatform == NULL || sBusy.load())
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(sLock);
    return sStatus.state == IUPDATE_AVAILABLE && sStatus.installAllowed;
}

void iUpdateDownload()
{
    if (!iUpdateCanDownload())
    {
        return;
    }
    sDone = 0;
    Set(IUPDATE_DOWNLOADING, "Starting the download...");
    Start(DownloadJob);
}

bool iUpdateCanCancel()
{
    if (!sBusy.load())
    {
        return false;
    }
    const iUpdateState s = State();
    return s == IUPDATE_CHECKING || s == IUPDATE_DOWNLOADING || s == IUPDATE_VERIFYING;
}

void iUpdateCancel()
{
    if (iUpdateCanCancel())
    {
        Log("cancel asked");
        sCancel = true;
    }
}

void iUpdateGetStatus(iUpdateStatus* out)
{
    std::lock_guard<std::mutex> lock(sLock);
    *out = sStatus;
    out->done = sDone.load();
}

bool iUpdatePrepareRestart()
{
    if (sPlatform == NULL || State() != IUPDATE_DONE_RESTART || !sPlatform->CanRestart())
    {
        return false;
    }
    const bool ok = sPlatform->PrepareRestart();
    Log("restart %s", ok ? "arranged" : "could not be arranged");
    return ok;
}

void iUpdateWait()
{
    if (sWorker.joinable())
    {
        sWorker.join();
    }
}

void iUpdateShutdown()
{
    if (sPlatform != NULL)
    {
        sPlatform->Stop();
    }
    if (sWorker.joinable())
    {
        if (sBusy.load())
        {
            Log("stopping: waiting for the %s to end", iUpdateStateName(State()));
        }
        sCancel = true;
        sWorker.join();
    }
}
