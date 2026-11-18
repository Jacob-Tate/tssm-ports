// A test build's stand-in for GitHub (CMakeLists.txt, TSSM_UPDATE_TEST_SOURCE;
// docs/3DS.md "Updates", the test kit): the updater's requests answered from
// files in a folder on the SD card instead of the network, so an install of a
// test .cia can be tried without the public .cia, whose title is the game's
// own. Only a build with a test title ID can have it; nothing here goes
// online.
//
//   GET api.github.com/repos/<owner>/<name>/releases/latest
//       -> <folder>/release.json
//   GET github.com/<owner>/<name>/releases/download/<tag>/<file>
//       -> 302 to release-assets.githubusercontent.com/test/<file>, as GitHub
//          answers, so the redirect is followed the real way
//   GET release-assets.githubusercontent.com/test/<file>
//       -> <folder>/<file>
// Anything else is a 404. The body comes in 16 KB pieces with a 2 ms nap
// between them, so the progress bar and Cancel can be seen working.

#include "3ds/i3DS.h"

#include "iUpdate.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/stat.h>

#ifndef TSSM_UPDATE_TEST_SOURCE
#error "CMakeLists.txt sets TSSM_UPDATE_TEST_SOURCE for this file"
#endif

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

    // A plain file name: letters, digits, '.', '-', '_', and no "..".
    bool PlainName(const std::string& name)
    {
        if (name.empty() || name.size() > 120 || name.find("..") != std::string::npos)
        {
            return false;
        }
        for (char c : name)
        {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '.' || c == '-' || c == '_'))
            {
                return false;
            }
        }
        return true;
    }

    iUpdateNet Serve(const std::string& file, iUpdateSink* sink, char* detail, size_t detailSize)
    {
        const std::string path = std::string(TSSM_UPDATE_TEST_SOURCE) + "/" + file;
        FILE* f = fopen(path.c_str(), "rb");
        struct stat st;
        if (f == NULL || fstat(fileno(f), &st) != 0)
        {
            if (f != NULL)
            {
                fclose(f);
            }
            printf("tssm: update: test source: %s: not there (404)\n", path.c_str());
            sink->OnResponse(404, 0, NULL);
            return IUPDATE_NET_OK;
        }
        printf("tssm: update: test source: %s, %lld bytes\n", path.c_str(), (long long)st.st_size);
        if (!sink->OnResponse(200, (int64_t)st.st_size, NULL))
        {
            fclose(f);
            return IUPDATE_NET_OK;
        }
        static char buf[16 * 1024];
        iUpdateNet net = IUPDATE_NET_OK;
        while (true)
        {
            if (sink->Cancelled())
            {
                net = IUPDATE_NET_CANCELLED;
                break;
            }
            const size_t n = fread(buf, 1, sizeof(buf), f);
            if (n == 0)
            {
                if (ferror(f))
                {
                    Say(detail, detailSize, "reading %s", path.c_str());
                    net = IUPDATE_NET_LOST;
                }
                break;
            }
            if (!sink->OnBody(buf, n))
            {
                net = sink->Cancelled() ? IUPDATE_NET_CANCELLED : IUPDATE_NET_STOPPED;
                break;
            }
            i3DSSleepNs(2000000);
        }
        fclose(f);
        return net;
    }
} // namespace

iUpdateNet i3DSUpdateTestGet(const iUpdateRequest& req, iUpdateSink* sink, char* detail,
                             size_t detailSize)
{
    const std::string host = req.host;
    const std::string path = req.path;
    printf("tssm: update: test source: GET https://%s%s\n", req.host, req.path);
    if (host == "api.github.com" && path.compare(0, 7, "/repos/") == 0 && path.size() > 23 &&
        path.compare(path.size() - 16, 16, "/releases/latest") == 0)
    {
        return Serve("release.json", sink, detail, detailSize);
    }
    const std::string::size_type download = path.find("/releases/download/");
    if (host == "github.com" && download != std::string::npos)
    {
        const std::string file = path.substr(path.rfind('/') + 1);
        if (PlainName(file))
        {
            const std::string location =
                "https://release-assets.githubusercontent.com/test/" + file;
            sink->OnResponse(302, 0, location.c_str());
            return IUPDATE_NET_OK;
        }
    }
    if (host == "release-assets.githubusercontent.com" && path.compare(0, 6, "/test/") == 0 &&
        PlainName(path.substr(6)))
    {
        return Serve(path.substr(6), sink, detail, detailSize);
    }
    sink->OnResponse(404, 0, NULL);
    return IUPDATE_NET_OK;
}
