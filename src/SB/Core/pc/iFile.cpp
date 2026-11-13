// PC iFile: treedome's pc/iFile.cpp (host filesystem, case-insensitive
// resolution, asset root, mod folder, queued async reads) with TSSM's
// interface. See iFile.h for the differences.
#include "iFile.h"
#include "iAudioSDLTest.h"
#include "iHangDump.h"
#include "iConfig.h"
#include "iD0Stats.h"
#include "iHost.h"
#include "iMod.h"
#include "iSpike.h"
#include "iModPackage.h"

#include "xFile.h"

#include <rwcore.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct file_queue_entry
{
    tag_xFile* file;
    void* buf;
    U32 size;
    U32 offset;
    IFILE_READSECTOR_STATUS stat;
    void (*callback)(tag_xFile* file);
    U32 asynckey;
};

static file_queue_entry file_queue[4];

// Where relative asset names are resolved from. Retail leaves this empty: the
// DVD root is the only place a name can mean, so iFileFullPath is a strcpy and
// iFileSetPath does nothing. A host has a working directory that is not the
// asset root, so both do their jobs here.
static char sBasePath[512];

// Whether any file has ever opened. See reportMissingFile: it is what tells a
// wrong asset root from the asset system probing for a file it can do without.
static S32 sOpenedAny;

// The asset root, from the settings, kept separately from sBasePath.
//
// It exists because the GAME overwrites sBasePath: zMain.cpp:890 reads a PATH
// setting out of SB.INI and hands it to iFileSetPath. On the console that is
// harmless -- SB.INI's "PATH=." means the DVD root, which is the only place
// there is -- but on a host "." is the working directory, and the port lost
// every asset the moment it read its own INI. FONT.HIP and SB.INI loaded from
// the asset root, and BOOT.HIP was then looked for in the build directory.
//
// So a RELATIVE path from the INI is resolved against the asset root, which is
// what it means on the console, and an absolute one is honoured as given.
static char sAssetRoot[512];

// The mod folder, with a trailing slash, or "" when there is none. A file here
// is read in place of the asset root's file of the same relative name. See
// iModOverride.
static char sModRoot[512];

// Where the game's files are. No trailing slash is added -- the callers that
// print it want the folder someone named and not a decorated version of it,
// and the ones that build paths with it fix the tail themselves.
//
// A SETTING, `[assets] path`, because it is the one thing the port cannot run
// without and the one thing it cannot guess, and asking someone to set an
// environment variable before they can play is asking them to be a developer.
// config.ini writes itself on the first run, so the question is in front of
// whoever starts the game rather than in a README.
//
// TSSM_ASSETS still wins when it is set, and is how a second copy of the
// assets gets used without editing the file -- which is what a bisect over two
// extractions, or a build run against a stripped-down set, actually needs.
//
// Copied out rather than returned as iConfigGetString's pointer into the
// settings table, and read afresh on every call so that a test can change the
// environment between iFileInit calls.
//
// Backslashes become slashes on the way through. A path typed into a settings
// file on Windows is `D:\games\tssm`, and everything downstream here splits
// paths on '/' -- iResolveCaseInsensitive would take a backslash path for a
// bare leaf name and look for it in the working directory.
static void iReadFolderSetting(const char* env, const char* key, char* out, size_t outsize)
{
    const char* value = getenv(env);
    if (value == NULL || value[0] == '\0')
    {
        value = iConfigGetString(key, "");
    }

    snprintf(out, outsize, "%s", value);

    for (char* c = out; *c != '\0'; c++)
    {
        if (*c == '\\')
        {
            *c = '/';
        }
    }
}

const char* iFileAssetRoot()
{
    static char sRoot[512];
    iReadFolderSetting("TSSM_ASSETS", "assets.path", sRoot, sizeof(sRoot));
    return sRoot;
}

const char* iFileModRoot()
{
    static char sRoot[512];
    iReadFolderSetting("TSSM_MOD", "assets.mod", sRoot, sizeof(sRoot));
    return sRoot;
}

const char* iFileModName()
{
    static char sName[256];

    size_t n = strlen(sModRoot);
    while (n > 0 && sModRoot[n - 1] == '/')
    {
        n--;
    }

    size_t start = n;
    while (start > 0 && sModRoot[start - 1] != '/' && sModRoot[start - 1] != ':')
    {
        start--;
    }

    snprintf(sName, sizeof(sName), "%.*s", (int)(n - start), sModRoot + start);
    return sName;
}

// The length of a newlib device prefix such as the 3DS's and the Switch's
// "sdmc:" (or "romfs:"), with the slash after it; 0 when there is none. Two
// letters or more, so a Windows drive letter is not one.
static size_t iDevicePrefixLength(const char* path)
{
    size_t n = 0;
    while ((path[n] >= 'a' && path[n] <= 'z') || (path[n] >= 'A' && path[n] <= 'Z'))
    {
        n++;
    }
    if (n < 2 || path[n] != ':')
    {
        return 0;
    }
    return path[n + 1] == '/' ? n + 2 : n + 1;
}

static bool iPathIsAbsolute(const char* path)
{
    if (path == NULL || path[0] == 0)
    {
        return false;
    }

    // "/..." or "X:..." -- the second covers Windows drive letters, which the
    // console never had to think about -- or "sdmc:/..." on the 3DS, where
    // TSSM_ASSETS names the SD card that way (3ds/i3DS.cpp).
    return path[0] == '/' || path[0] == '\\' ||
           (path[1] == ':' &&
            ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))) ||
           iDevicePrefixLength(path) > 0;
}

void iFileInit()
{
    sBasePath[0] = '\0';

    // **Where the game's assets are.**
    //
    // On the console this question does not exist: iFileFullPath is a strcpy
    // because the DVD root is the only place a name can mean. A host has a
    // working directory that is almost never the asset root, and NOTHING in the
    // game ever calls iFileSetPath -- the console had no use for it, so the
    // port has to answer the question itself.
    //
    // Without this the game asks for "FONT.HIP" relative to the working
    // directory, does not find it, and retries FOREVER inside zMainLoadFontHIP:
    // no error, no message, just a process sitting there. That was the first
    // hang the port ever reached, and it took a watchdog to find.
    //
    // A setting rather than a command-line argument, because main() is the
    // game's own in zMain.cpp and its argv handling is retail's. See
    // iFileAssetRoot for where the answer comes from.
    sAssetRoot[0] = 0;

    const char* assets = iFileAssetRoot();
    if (assets[0] != 0)
    {
        snprintf(sAssetRoot, sizeof(sAssetRoot), "%s", assets);

        size_t n = strlen(sAssetRoot);
        if (n > 0 && sAssetRoot[n - 1] != '/' && n + 1 < sizeof(sAssetRoot))
        {
            sAssetRoot[n] = '/';
            sAssetRoot[n + 1] = 0;
        }

        iFileSetPath(assets);
    }

    sModRoot[0] = 0;

    const char* mod = iFileModRoot();
    if (mod[0] != 0)
    {
        if (!iHostPathExists(mod))
        {
            printf("tssm: mod folder not found, ignored: %s\n", mod);
            fflush(stdout);
        }
        else
        {
            size_t n = strlen(mod);
            snprintf(sModRoot, sizeof(sModRoot), "%s%s", mod, mod[n - 1] == '/' ? "" : "/");
            printf("tssm: mod folder: %s\n", mod);
            fflush(stdout);
        }
    }

    // The mods/ folder: every mod in it, their load order, and the index of
    // what they replace (iMod.h). Nothing changes when there are none.
    iModInit();

    for (S32 i = 0; i < 4; i++)
    {
        file_queue[i].stat = IFILE_RDSTAT_NOOP;
    }
}

void iFileExit()
{
}

void iFileSetPath(const char* path)
{
    if (path == NULL || path[0] == '\0')
    {
        sBasePath[0] = '\0';
        return;
    }

    // A relative path means "relative to the asset root", because that is what
    // it means on the console. "." is the common case and means exactly the
    // root. See the note at sAssetRoot.
    if (!iPathIsAbsolute(path) && sAssetRoot[0] != 0)
    {
        if (path[0] == '.' && (path[1] == 0 || path[1] == '/'))
        {
            snprintf(sBasePath, sizeof(sBasePath), "%s%s", sAssetRoot,
                     path[1] == '/' ? path + 2 : "");
        }
        else
        {
            snprintf(sBasePath, sizeof(sBasePath), "%s%s", sAssetRoot, path);
        }
    }
    else
    {
        snprintf(sBasePath, sizeof(sBasePath), "%s", path);
    }

    size_t n = strlen(sBasePath);
    if (n > 0 && sBasePath[n - 1] != '/' && n + 1 < sizeof(sBasePath))
    {
        sBasePath[n] = '/';
        sBasePath[n + 1] = '\0';
    }
}

void iFileFullPath(const char* relname, char* fullname)
{
    snprintf(fullname, sizeof(((tag_iFile*)0)->path), "%s%s", sBasePath, relname);

    // TSSM's gc/iFile.cpp turns backslashes into slashes here (the DVD
    // filesystem takes only '/'), and the asset names it builds use both.
    for (char* c = fullname; *c != '\0'; c++)
    {
        if (*c == '\\')
        {
            *c = '/';
        }
    }
}

// One component of a path, matched against a directory without regard to case.
// Writes the directory's own spelling into `name` -- which is safe in place,
// because a case-insensitive match is the same length -- and says whether it
// found one.
static bool iResolveComponent(const char* dir, char* name)
{
    iHostDir* d = iHostDirOpen(dir[0] != '\0' ? dir : ".");
    if (d == NULL)
    {
        return false;
    }

    bool found = false;
    for (const char* entry = iHostDirNext(d); entry != NULL; entry = iHostDirNext(d))
    {
        if (iHostStrCaseCmp(entry, name) == 0)
        {
            memcpy(name, entry, strlen(name));
            found = true;
            break;
        }
    }

    iHostDirClose(d);
    return found;
}

// The disc filesystem is case-insensitive and the asset names in the game data
// do not agree with each other on case; a host filesystem usually does not
// forgive that. Only used when the exact name misses, so a correctly-cased
// tree costs nothing.
//
// **Every component, not only the last one.** The asset system asks for
// `GL/GL01.HIP`, because xUtil_idtag2string spells a scene tag the way the tag
// is spelled and the tags are upper case, while an extraction has the
// directory as `gl`. Resolving the leaf alone meant opening `<root>/GL` to
// look in, which does not exist, so the function gave up before it ever
// reached the name it was there to fix: the pack was invisible and
// xSTPreLoadScene span on it forever. Windows never saw this -- its filesystem
// answers for `GL` and `gl` alike -- and Android's does not.
//
// The walk starts after the root (`/`, or a Windows drive) and resolves each
// component against the one above it. A component that already exists is left
// alone, so the only directories ever scanned are the ones actually spelled
// wrong.
static bool iResolveCaseInsensitive(char* path, size_t pathsize)
{
    if (iHostPathExists(path))
    {
        return true;
    }

    char out[512];
    size_t len = 0;
    const char* p = path;

    // The root, taken as given. Everything after it is what the game spells
    // for itself and so what can disagree with the disk.
    if (p[0] == '/')
    {
        out[len++] = *p++;
    }
    else if (p[0] != '\0' && p[1] == ':')
    {
        out[len++] = *p++;
        out[len++] = *p++;
        if (*p == '/')
        {
            out[len++] = *p++;
        }
    }
    else if (iDevicePrefixLength(p) > 0)
    {
        // "sdmc:/" (3DS, Switch).
        const size_t n = iDevicePrefixLength(p);
        memcpy(out, p, n);
        len = n;
        p += n;
    }
    out[len] = '\0';

    while (*p != '\0')
    {
        const char* end = strchr(p, '/');
        size_t n = (end != NULL) ? (size_t)(end - p) : strlen(p);

        if (n == 0)
        {
            p++; // a doubled separator
            continue;
        }

        // The separator this component needs, unless the root already ended in
        // one or nothing has been written yet.
        size_t sep = (len > 0 && out[len - 1] != '/') ? 1 : 0;

        if (len + sep + n + 1 > sizeof(out))
        {
            return false;
        }

        // Where the component goes, and the parent to look in if it is not
        // there under this spelling.
        char parent[512];
        memcpy(parent, out, len);
        parent[len] = '\0';
        if (sep)
        {
            out[len++] = '/';
        }
        char* component = out + len;
        memcpy(component, p, n);
        len += n;
        out[len] = '\0';

        if (!iHostPathExists(out) && !iResolveComponent(parent, component))
        {
            return false;
        }

        p += n;
    }

    if (len >= pathsize)
    {
        return false;
    }

    memcpy(path, out, len + 1);
    return true;
}

// See iFile.h. The resolver above, for host code outside this file (the movies).
bool iFileResolveCase(char* path, size_t pathsize)
{
    return iResolveCaseInsensitive(path, pathsize);
}

// Whether the asset root actually holds the game.
//
// Asked once, at startup, so that a wrong asset path is an error the player
// sees rather than the hang described below. By the time the game reaches
// zMainLoadFontHIP it is too late to say anything: the loop there has no exit
// and no caller to return a failure to, and both are retail's.
//
// FONT.HIP first, because it is the one the game blocks on and so the one whose
// absence has already cost people an afternoon. boot.HIP second, because a
// directory holding only the font is a half-extracted disc rather than the
// wrong directory, and saying which file is missing separates those.
//
// Case-insensitive, like every other lookup here: a real Xbox extraction has
// `font.HIP` on disk and the game asks for `FONT.HIP`.
//
// Returns the full path of the first one that is not there -- the path is the
// useful half, since the whole failure is that it is not where it was looked
// for -- or NULL when both are present.
const char* iFileMissingAssetPath()
{
    static const char* const kRequired[] = { "FONT.HIP", "boot.HIP" };
    static char sTried[512];

    for (size_t i = 0; i < sizeof(kRequired) / sizeof(kRequired[0]); i++)
    {
        snprintf(sTried, sizeof(sTried), "%s%s", sBasePath, kRequired[i]);

        if (!iResolveCaseInsensitive(sTried, sizeof(sTried)))
        {
            return sTried;
        }
    }

    return NULL;
}

// Which console's packer wrote the assets, out of boot.HIP's own header.
//
// The port reads the Xbox release. A GameCube or PS2 set does not announce
// itself -- it loads, and then fails somewhere far from the cause, because the
// packer's container is the same on every platform and only what is inside it
// differs. A GameCube boot.HIP gets as far as the animation tables before
// zAssetTypes.cpp's `(xAnimAssetTable*)indata` reads a big-endian count of 74
// as 1,241,513,984 and walks off the end of memory. That crash says nothing
// about what is actually wrong.
//
// HIPA holds PACK, PACK holds PLAT, and PLAT is a run of NUL-terminated
// strings: a two-letter id, then the long name, then the video standard, the
// region and the project. Chunk sizes are big-endian on every platform -- the
// container is the packer's, not the console's -- so this reads the same way
// whatever wrote it. Every chunk here is at a known small offset, so a few
// hundred bytes off the front of the file is enough.
//
// iHipPlatform reads any package's PLAT; iFileAssetPlatform asks it of the asset
// root's boot.HIP. Writes "" to both outputs when there is no answer.
static bool iHipPlatform(const char* path, char* id, size_t idsize, char* name, size_t namesize)
{
    id[0] = '\0';
    name[0] = '\0';

    FILE* fp = fopen(path, "rb");
    if (fp == NULL)
    {
        return false;
    }

    U8 head[512];
    size_t got = fread(head, 1, sizeof(head), fp);
    fclose(fp);

    if (got < 16 || memcmp(head, "HIPA", 4) != 0)
    {
        return false;
    }

    // HIPA's own payload is the rest of the file, so step into it and walk the
    // top-level chunks; PACK's children are walked the same way.
    size_t o = 8;
    size_t end = got;

    for (int depth = 0; depth < 2; depth++)
    {
        bool descended = false;

        while (o + 8 <= end)
        {
            const U8* c = head + o;
            U32 size = ((U32)c[4] << 24) | ((U32)c[5] << 16) | ((U32)c[6] << 8) | c[7];
            size_t body = o + 8;

            if (memcmp(c, "PACK", 4) == 0)
            {
                o = body;
                end = (body + size < got) ? body + size : got;
                descended = true;
                break;
            }

            if (memcmp(c, "PLAT", 4) == 0)
            {
                size_t stop = (body + size < got) ? body + size : got;
                snprintf(id, idsize, "%.*s", (int)(stop - body), (const char*)head + body);

                // The long name is the string after it.
                size_t n = body + strlen(id);
                while (n < stop && head[n] == '\0')
                {
                    n++;
                }
                snprintf(name, namesize, "%.*s", (int)(stop - n), (const char*)head + n);
                return id[0] != '\0';
            }

            if (size == 0)
            {
                break;
            }
            o = body + size;
        }

        if (!descended)
        {
            break;
        }
    }

    return false;
}

const char* iFileAssetPlatform(const char** longName)
{
    static char sId[8];
    static char sName[32];

    if (longName != NULL)
    {
        *longName = NULL;
    }

    char path[512];
    snprintf(path, sizeof(path), "%sboot.HIP", sBasePath);
    if (!iResolveCaseInsensitive(path, sizeof(path)))
    {
        return NULL;
    }

    if (!iHipPlatform(path, sId, sizeof(sId), sName, sizeof(sName)))
    {
        return NULL;
    }

    if (longName != NULL && sName[0] != '\0')
    {
        *longName = sName;
    }
    return sId;
}

// Whether `name` has been reported yet, remembering it if not. Bounded: past
// the table's size nothing more is reported.
static bool iModFirstReport(const char* name)
{
    enum
    {
        kMaxReported = 256,
        kMaxName = 64
    };

    static char sReported[kMaxReported][kMaxName];
    static S32 sCount;

    for (S32 i = 0; i < sCount; i++)
    {
        if (iHostStrCaseCmp(sReported[i], name) == 0)
        {
            return false;
        }
    }

    if (sCount == kMaxReported)
    {
        return false;
    }

    snprintf(sReported[sCount++], kMaxName, "%s", name);
    return true;
}

static bool iHasPackageExtension(const char* path)
{
    size_t n = strlen(path);
    return n >= 4 &&
           (iHostStrCaseCmp(path + n - 4, ".HIP") == 0 || iHostStrCaseCmp(path + n - 4, ".HOP") == 0);
}

// The mod folder's copy of a file under the asset root. When the mod has one,
// its path is written over `path` and the result is true.
//
// Matched on the path relative to the asset root, so `gl/gl01.HIP` in the mod
// folder replaces `gl/gl01.HIP` in the assets, and whatever SB.INI's PATH= did
// to the base path applies to both. A path outside the asset root is left
// alone.
//
// A package from another console's release is refused and the original is
// read: the port only understands the Xbox layout, and a GameCube HIP loads
// and then crashes far from the cause. See iFileAssetPlatform.
//
// TSSM: the mods/ folder's loose files (iMod.h, `files/` in each mod) are
// asked after the single mod folder, with the same platform check.
static bool iModOverride(char* path, size_t pathsize)
{
    const char* rel;
    size_t rootlen = strlen(sAssetRoot);
    if (rootlen > 0)
    {
        if (strncmp(path, sAssetRoot, rootlen) != 0)
        {
            return false;
        }
        rel = path + rootlen;
    }
    else
    {
        if (iPathIsAbsolute(path))
        {
            return false;
        }
        rel = path;
    }

    while (rel[0] == '.' && rel[1] == '/')
    {
        rel += 2;
    }

    char candidate[512];
    bool found = false;
    if (sModRoot[0] != 0 &&
        snprintf(candidate, sizeof(candidate), "%s%s", sModRoot, rel) < (int)sizeof(candidate))
    {
        found = iResolveCaseInsensitive(candidate, sizeof(candidate));
    }
    if (!found)
    {
        found = iModResolveFile(rel, candidate, sizeof(candidate));
    }
    if (!found)
    {
        return false;
    }

    if (iHasPackageExtension(candidate))
    {
        char id[8];
        char longName[32];
        // TSSM's Xbox packs say "BX" where BfBB's say "XB" (heavyironmodding.org).
        if (iHipPlatform(candidate, id, sizeof(id), longName, sizeof(longName)) &&
            strcmp(id, "BX") != 0 && strcmp(id, "XB") != 0)
        {
            if (iModFirstReport(rel))
            {
                printf("tssm: mod: %s is from the %s release, not the Xbox one; using the "
                       "original\n",
                       rel, longName[0] != '\0' ? longName : id);
                fflush(stdout);
            }
            return false;
        }
    }

    // `rel` points into `path`, so it is reported before path is overwritten.
    size_t len = strlen(candidate);
    if (len >= pathsize)
    {
        return false;
    }

    if (iModFirstReport(rel))
    {
        printf("tssm: mod: %s\n", rel);
        fflush(stdout);
    }

    memcpy(path, candidate, len + 1);
    return true;
}

bool iFileModReplaces(const char* rel)
{
    char path[512];
    if (snprintf(path, sizeof(path), "%s%s", sAssetRoot, rel) >= (int)sizeof(path))
    {
        return false;
    }
    return iModOverride(path, sizeof(path));
}

// A scene package the game cannot load, and the end of the run.
//
// TSSM's xSTPreLoadScene returns 0 for a pack it cannot open, and the game goes
// on without it: a level's missing HIP is found only in zTransient::scene_enter,
// as a null world geometry, far from the cause. The disc always had the file;
// a copied file set may not. So the file is named, with where it should be, and
// the run stops before anything reads the missing scene.
void iFileMissingPackage(U32 sid, S32 options, const char* filename)
{
    const char* ext = (options & 3) == 2 ? ".HOP" : ".HIP";
    char tag[5];
    for (S32 i = 0; i < 4; i++)
    {
        char c = (char)(sid >> (24 - 8 * i));
        tag[i] = (c >= ' ' && c <= '~') ? c : '?';
    }
    tag[4] = '\0';

    // As xSTPreLoadScene asks: <tag's first two letters>/<tag>.HIP, then the
    // root; BOOT, PLAT, DEMO and FONT only at the root.
    char subdir[64];
    char root[64];
    if (filename != NULL)
    {
        snprintf(subdir, sizeof(subdir), "%s", filename);
        root[0] = '\0';
    }
    else
    {
        const bool rootOnly = (options & 3) != 2 &&
                              (sid == 'BOOT' || sid == 'PLAT' || sid == 'DEMO' || sid == 'FONT');
        snprintf(subdir, sizeof(subdir), "%.2s/%s%s", tag, tag, ext);
        snprintf(root, sizeof(root), "%s%s", tag, ext);
        if (rootOnly)
        {
            snprintf(subdir, sizeof(subdir), "%s", root);
            root[0] = '\0';
        }
    }

    char want[512];
    snprintf(want, sizeof(want), "%s%s", sBasePath, subdir);
    for (char* c = want; *c != '\0'; c++)
    {
        if (*c == '\\')
        {
            *c = '/';
        }
    }

    // There but unreadable is a different fault from not there.
    const bool present = iFileExists(subdir, 0) || (root[0] != '\0' && iFileExists(root, 0));

    if (present)
    {
        printf("tssm: FATAL -- a game file could not be read: %s\n", want);
        printf("tssm:   it is there, but the game could not load it (damaged, cut short, or "
               "not from the Xbox release).\n");
    }
    else
    {
        printf("tssm: FATAL -- a game file is missing: %s\n", subdir);
        printf("tssm:   it should be here: %s\n", want);
        if (root[0] != '\0')
        {
            printf("tssm:   (also looked for: %s%s)\n", sBasePath, root);
        }
        printf("tssm:   copy the game's files again: every .HIP and .HOP, in the folders "
               "they came in.\n");
    }
    fflush(stdout);

    // The 3DS box ends with its own "Press START to close."
#ifdef __3DS__
    const char* closing = "";
#else
    const char* closing = "\n\nThe game will close now.";
#endif
    char message[1536];
    if (present)
    {
        snprintf(message, sizeof(message),
                 "A game file could not be read:\n\n%s\n\n"
                 "It is there, but the game could not load it. Copy it again from your "
                 "game files.%s",
                 want, closing);
    }
    else
    {
        snprintf(message, sizeof(message),
                 "A game file is missing:\n\n%s\n\nIt should be here:\n\n%s\n\n"
                 "Copy the game's files again: every .HIP and .HOP, in the folders they "
                 "came in.%s",
                 subdir, want, closing);
    }

    iHostErrorBox(TSSM_GAME_TITLE, message);
    exit(1);
}

// A file that is not there, said out loud.
//
// The game does not check, and cannot be made to: zMainLoadFontHIP ends in
//
//     do { } while (xSTLoadStep('FONT') < 1.0f);
//
// which never leaves that loop if FONT.HIP never loads. On the console that
// cannot happen -- the DVD root is the only place a name can mean, and the disc
// IS the game -- so retail has no handling here to port. On a host, a wrong
// asset path produces a hang on a white window with a startup log that looks
// perfectly normal right up to the last line, and it is the single most common
// way a first run fails. One line of output is the whole difference.
//
// **Only until the first file opens.** A miss is not by itself a problem: the
// game probes. `PL/PLAT.HIP` is looked for and missed on a perfectly good
// install, because the asset system tries a per-scene subdirectory before the
// root. Reporting those would be noise, and noise is what teaches people to
// ignore the line that matters.
//
// What is never normal is reaching the font with nothing opened at all. That
// separates a wrong asset root from ordinary probing exactly, needs no list of
// which names are speculative, and cannot go stale as more of the game is
// ported.
//
// Once per NAME as well, because the loop above asks for the same file
// thousands of times a second, and capped in case a root is wrong in a way that
// still lets the first open through.
static void reportMissingFile(const char* name, const char* path)
{
    enum
    {
        kMaxReported = 8,
        kMaxName = 64
    };

    static char sReported[kMaxReported][kMaxName];
    static S32 sCount;
    static S32 sSilenced;

    if (sSilenced || sOpenedAny)
    {
        return;
    }

    for (S32 i = 0; i < sCount; i++)
    {
        if (iHostStrCaseCmp(sReported[i], name) == 0)
        {
            return;
        }
    }

    if (sCount == 0)
    {
        printf("tssm: no asset file has opened yet, and the game is asking for them.\n");
        printf("tssm:   [assets] path in config.ini -- or TSSM_ASSETS -- must name the\n");
        printf("tssm:   directory that DIRECTLY contains boot.HIP, FONT.HIP and fmv/ --\n");
        printf("tssm:   not a folder above it, and not a disc image. The game does not\n");
        printf("tssm:   check, so the wrong path is a hang on a blank window rather\n");
        printf("tssm:   than an error.\n");
    }

    printf("tssm:   not found: %s\n", path);

    snprintf(sReported[sCount], kMaxName, "%s", name);
    sCount++;

    if (sCount == kMaxReported)
    {
        sSilenced = 1;
        printf("tssm:   (further missing files are not reported)\n");
    }

    fflush(stdout);
}

U32 iFileOpen(const char* name, S32 flags, tag_xFile* file)
{
    tag_iFile* ps = &file->ps;

    if (flags & IFILE_OPEN_ABSPATH)
    {
        snprintf(ps->path, sizeof(ps->path), "%s", name);
    }
    else
    {
        iFileFullPath(name, ps->path);
    }

    const char* mode = (flags & IFILE_OPEN_WRITE) ? "wb" : "rb";

    if (!(flags & IFILE_OPEN_WRITE) && !iModOverride(ps->path, sizeof(ps->path)))
    {
        iResolveCaseInsensitive(ps->path, sizeof(ps->path));
    }

    FILE* fp = fopen(ps->path, mode);
    if (fp == NULL)
    {
        // Reads only. A write that fails is a read-only directory rather than a
        // missing asset, and the advice above would be the wrong advice.
        if (!(flags & IFILE_OPEN_WRITE))
        {
            reportMissingFile(name, ps->path);
        }

        ps->handle = NULL;
        ps->view = NULL;
        ps->flags = 0;
        ps->length = 0;
        return 1;
    }

#ifdef __3DS__
    // libctru's SD device has no read cache, and every small read is a round
    // trip to the FS service. A 128 KB stdio buffer makes the game's small
    // reads 128 KB ones; reads at least that big go straight through.
    if (!(flags & IFILE_OPEN_WRITE))
    {
        setvbuf(fp, NULL, _IOFBF, 128 * 1024);
    }
#endif

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    sOpenedAny = 1;
    if (!(flags & IFILE_OPEN_WRITE))
    {
        iSpikeFileOpened(ps->path, len);
    }

    // A package a mod replaces assets in is read through the modded view of
    // it, whoever opens it: the packer, xTextureManager, the sound and
    // cutscene readers. NULL -- the file as it is -- otherwise.
    ps->view = NULL;
    if (!(flags & IFILE_OPEN_WRITE) && iHasPackageExtension(ps->path))
    {
        // TSSM_TEST_AUDIO_RESTART's "load" moment (iAudioSDLTest.h).
        iAudioSDLTestLoad(ps->path);

        ps->view = iModOpenPackage(ps->path);
        if (ps->view != NULL)
        {
            len = (long)iModPkgSize(ps->view);
        }
    }

    ps->handle = fp;
    ps->length = (S32)len;
    ps->offset = 0;
    ps->asynckey = -1;
    ps->flags = 0x1;

    return 0;
}

S32 iFileSeek(tag_xFile* file, S32 offset, S32 whence)
{
    tag_iFile* ps = &file->ps;
    S32 new_pos;

    switch (whence)
    {
    case IFILE_SEEK_SET:
        new_pos = offset;
        break;
    case IFILE_SEEK_CUR:
        new_pos = offset + ps->offset;
        break;
    case IFILE_SEEK_END:
        // Retail subtracts, rather than adding a negative offset the way the
        // C library does. Kept, because every caller was written against it.
        new_pos = ps->length - offset;
        if (new_pos < 0)
        {
            new_pos = 0;
        }
        break;
    default:
        new_pos = offset;
        break;
    }

    ps->offset = new_pos;

    return ps->offset;
}

U32 iFileRead(tag_xFile* file, void* buf, U32 size)
{
    // TSSM_TEST_HANG_DUMP: a load is progress too (iHangDump.h).
    iHangDumpBeat();

    tag_iFile* ps = &file->ps;
    FILE* fp = (FILE*)ps->handle;

    if (fp == NULL)
    {
        return 0;
    }

    const S64 d0Start = iD0Now();

    if (ps->view != NULL)
    {
        iModPkgRead(ps->view, fp, (U32)ps->offset, buf, size);
        ps->offset += (S32)size;
        iD0Add(iD0_FILE, d0Start, size);
        return size;
    }

    fseek(fp, ps->offset, SEEK_SET);

    // Retail reads whole aligned sectors and lets the tail run past the end of
    // the file, because the DVD hardware simply returns whatever is on the
    // sector. fread stops at EOF instead, so the caller would see stale bytes
    // where the console gave it padding. Zero the shortfall.
    size_t got = fread(buf, 1, size, fp);
    if (got < size)
    {
        memset((U8*)buf + got, 0, size - got);
    }

    ps->offset += (S32)size;

    iD0Add(iD0_FILE, d0Start, size);
    return size;
}

// Retail's asynchronous read is a DVD interrupt chain: iFileReadAsync starts
// it, the completion callback issues the next chunk, and iFileAsyncService --
// weakly defined as empty in xstransvc.cpp -- does nothing at all.
//
// A host read is fast enough that chunking buys nothing, but the callback must
// still not fire inside iFileReadAsync: game code written against an interrupt
// does not expect its completion handler to run before the call that started
// it has returned. So the request is queued here and completed from the
// service function, which the load loops already call every step. Polling
// drains too, so a caller that only checks status still makes progress.
S32 iFileReadAsync(tag_xFile* file, void* buf, U32 aSize, void (*callback)(tag_xFile*),
                   S32 priority)
{
    static S32 fopcount = 1;
    tag_iFile* ps = &file->ps;

    for (S32 i = 0; i < 4; i++)
    {
        if (file_queue[i].stat != IFILE_RDSTAT_QUEUED && file_queue[i].stat != IFILE_RDSTAT_INPROG)
        {
            S32 id = fopcount++ << 2;
            S32 asynckey = id + i;

            file_queue[i].file = file;
            file_queue[i].buf = buf;
            file_queue[i].size = aSize;
            file_queue[i].offset = 0;
            file_queue[i].stat = IFILE_RDSTAT_QUEUED;
            file_queue[i].callback = callback;
            file_queue[i].asynckey = asynckey;

            ps->asynckey = asynckey;

            return i + id;
        }
    }

    return -1;
}

static void iFileServiceEntry(file_queue_entry* entry)
{
    if (entry->stat != IFILE_RDSTAT_QUEUED && entry->stat != IFILE_RDSTAT_INPROG)
    {
        return;
    }

    tag_xFile* file = entry->file;

    // A POSITIONED read: it must not move the file's own offset.
    //
    // The console's asynchronous read is DVDReadAsync(&fileInfo, buf, size,
    // ps->offset, cb) -- it takes the offset as an argument and leaves
    // ps->offset alone, so advancing past what was read is the COMPLETION
    // CALLBACK's job. iCutscene.cpp:iCSAsyncReadCB does exactly that, seeking
    // forward by the amount iFileReadAsyncStatus reports.
    //
    // Servicing this through iFileRead, which advances ps->offset itself, made
    // the position run ahead by a whole chunk on every asynchronous read: the
    // read advanced it once and the callback advanced it again. The cutscene
    // loader read its time chunk from a chunk's distance past where the chunk
    // actually was, so csn->Play was filled with whatever happened to be there
    // -- or with the 0xDEADBEEF the loader poisons the buffer with, if the read
    // ran off the end of the HIP -- and iCSSoundSetup walked off it.
    S32 mark = file->ps.offset;
    U32 done = iFileRead(file, entry->buf, entry->size);
    file->ps.offset = mark;

    entry->offset = done;
    entry->stat = (done == entry->size) ? IFILE_RDSTAT_DONE : IFILE_RDSTAT_FAIL;

    if (entry->callback)
    {
        entry->callback(file);
    }

    file->ps.asynckey = -1;
}

void iFileAsyncService()
{
    for (S32 i = 0; i < 4; i++)
    {
        iFileServiceEntry(&file_queue[i]);
    }
}

IFILE_READSECTOR_STATUS iFileReadAsyncStatus(S32 key, S32* amtToFar)
{
    file_queue_entry* entry = &file_queue[key & 0x3];

    if (key != (S32)entry->asynckey)
    {
        return IFILE_RDSTAT_EXPIRED;
    }

    iFileServiceEntry(entry);

    if (amtToFar)
    {
        *amtToFar = entry->offset;
    }

    return entry->stat;
}

void iFileReadStop()
{
    for (S32 i = 0; i < 4; i++)
    {
        if (file_queue[i].stat == IFILE_RDSTAT_QUEUED || file_queue[i].stat == IFILE_RDSTAT_INPROG)
        {
            file_queue[i].stat = IFILE_RDSTAT_NOOP;
        }
    }
}

U32 iFileClose(tag_xFile* file)
{
    tag_iFile* ps = &file->ps;

    if (ps->handle == NULL)
    {
        return 1;
    }

    fclose((FILE*)ps->handle);
    ps->handle = NULL;
    ps->flags = 0;

    iModPkgRelease(ps->view);
    ps->view = NULL;

    return 0;
}

U32 iFileGetSize(tag_xFile* file)
{
    return file->ps.length;
}

U32 iFileFind(const char* name, int fileflags, tag_xFile* file)
{
    return iFileOpen(name, fileflags, file);
}

// TSSM: whether a file is there, without opening it. The console asks the DVD
// filesystem for an entry number; this asks the host, through the same
// case-insensitive resolution iFileOpen uses.
U8 iFileExists(const char* name, S32 flags)
{
    tag_xFile file = {};
    if (iFileOpen(name, flags, &file) != 0)
    {
        return 0;
    }
    iFileClose(&file);
    return 1;
}

// TSSM: the unit xTextureManager seeks by when it streams a texture out of a
// HIP. The console's answer for the disc is 32 bytes and the port keeps it;
// whether the Xbox packs' TEXS sector numbers use the same unit has not been
// checked yet (PORTING.md).
U32 iFileGetSectorSize(xFileDriveType drive)
{
    switch (drive)
    {
    case XFILE_DRIVE_CDVD:
        return 32;
    default:
        return 1;
    }
}

// Retail reports the file's start sector on the disc, which xbinio uses only
// as a read-ordering hint (BFD_startSector). There is no such thing on a host
// filesystem, and a constant 0 makes every file compare equal, which is the
// correct answer when seeking is free.
void iFileGetInfo(tag_xFile* file, U32* starting_sector, U32* size_in_bytes)
{
    if (starting_sector)
    {
        *starting_sector = 0;
    }

    if (size_in_bytes)
    {
        *size_in_bytes = (U32)file->ps.length;
    }
}

U32* iFileLoad(const char* name, U32* buffer, U32* size)
{
    char path[512];
    tag_xFile file = {};

    iFileFullPath(name, path);

    // Retail passes `name` here, not `path`, so the full path it just built is
    // thrown away. That is invisible on the GameCube because iFileFullPath is
    // a strcpy, but it would defeat iFileSetPath entirely on a host.
    if (iFileOpen(path, IFILE_OPEN_ABSPATH, &file) != 0)
    {
        return NULL;
    }

    S32 fileSize = (S32)iFileGetSize(&file);
    S32 alignedSize = (fileSize + 31) & ~31;

    const bool ours = buffer == NULL;
    if (!buffer)
    {
        // RwMalloc, as the console's iFileLoad: callers give the buffer back
        // with RwFree (zMainReadINI), and on a 64-bit host RenderWare's heap is
        // not the C library's (iLowHeap.h). The C library only before
        // RenderWare is up.
        buffer = RwEngineInstance != NULL ? (U32*)RwMalloc(alignedSize, 0)
                                          : (U32*)malloc(alignedSize);
        if (buffer == NULL)
        {
            iFileClose(&file);
            return NULL;
        }
    }

    iFileRead(&file, buffer, alignedSize);

    iFileClose(&file);

    // Mods' overlays on the game's INI files (sb04.ini, explode.ini), merged
    // into a new buffer. Only into one allocated here: a caller's buffer has
    // the room the file had and no more.
    size_t n = strlen(name);
    if (ours && n >= 4 && iHostStrCaseCmp(name + n - 4, ".ini") == 0)
    {
        U32 mergedSize = 0;
        void* merged = iModApplyIniOverlays(name, (const char*)buffer, (U32)fileSize, &mergedSize);
        if (merged != NULL)
        {
            // Into a buffer from the allocator the caller frees with, above.
            U32* copy = RwEngineInstance != NULL ? (U32*)RwMalloc(mergedSize, 0)
                                                 : (U32*)malloc(mergedSize);
            if (copy != NULL)
            {
                memcpy(copy, merged, mergedSize);
                if (RwEngineInstance != NULL)
                {
                    RwFree(buffer);
                }
                else
                {
                    free(buffer);
                }
                buffer = copy;
                alignedSize = (S32)mergedSize;
            }
            free(merged);
        }
    }

    if (size)
    {
        *size = alignedSize;
    }

    return buffer;
}
