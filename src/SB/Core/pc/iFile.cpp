// PC iFile: treedome's pc/iFile.cpp (host filesystem, case-insensitive
// resolution, asset root, mod folder, queued async reads) with TSSM's
// interface. See iFile.h for the differences.
#include "iFile.h"
#include "iAudioSDLTest.h"
#include "iHangDump.h"
#include "iLoadFrame.h"
#include "iConfig.h"
#include "iHost.h"
#include "iMod.h"
#include "iModPackage.h"
#include "iGCPackage.h"

#include "xFile.h"

#include <rwcore.h>

#ifdef __ANDROID__
#include "android/iAndroid.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mutex>
#include <string>
#include <vector>

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

static bool iResolveCaseInsensitive(char* path, size_t pathsize);

// Whether `dir` directly holds boot.HIP and font.HIP, in any case.
static bool iHoldsGameFiles(const char* dir)
{
    static const char* const kRequired[] = { "boot.HIP", "font.HIP" };
    for (size_t i = 0; i < sizeof(kRequired) / sizeof(kRequired[0]); i++)
    {
        char path[600];
        size_t n = strlen(dir);
        snprintf(path, sizeof(path), "%s%s%s", dir, n > 0 && dir[n - 1] != '/' ? "/" : "",
                 kRequired[i]);
        if (!iResolveCaseInsensitive(path, sizeof(path)))
        {
            return false;
        }
    }
    return true;
}

// Dolphin's "Extract Entire Disc" writes a GameCube disc as files/ (the
// disc's own file system, where the game's files are) and sys/ (the boot
// program and the disc header). A folder laid out that way is read from its
// files/ folder, so either folder can be chosen.
static void iUseDiscFilesFolder(char* root, size_t rootsize)
{
    if (root[0] == '\0' || iHoldsGameFiles(root))
    {
        return;
    }

    char files[512];
    size_t n = strlen(root);
    if (snprintf(files, sizeof(files), "%s%sfiles", root, root[n - 1] != '/' ? "/" : "") >=
            (int)sizeof(files) ||
        !iResolveCaseInsensitive(files, sizeof(files)) || !iHoldsGameFiles(files) ||
        strlen(files) + 1 > rootsize)
    {
        return;
    }
    snprintf(root, rootsize, "%s", files);
}

const char* iFileAssetRoot()
{
    static char sRoot[512];
    iReadFolderSetting("TSSM_ASSETS", "assets.path", sRoot, sizeof(sRoot));
    iUseDiscFilesFolder(sRoot, sizeof(sRoot));
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

static bool iPathIsAbsolute(const char* path)
{
    if (path == NULL || path[0] == 0)
    {
        return false;
    }

    // "/..." or "X:..." -- the second covers Windows drive letters, which the
    // console never had to think about.
    return path[0] == '/' || path[0] == '\\' ||
           (path[1] == ':' && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')));
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
// The port reads the Xbox release and the GameCube one, whose packages are
// converted to the Xbox layout as they are opened (iMod.h). Neither a GameCube
// nor a PS2 package announces itself -- the packer's container is the same on
// every platform and only what is inside it differs -- and read as Xbox data a
// GameCube boot.HIP gets as far as the animation tables before
// zAssetTypes.cpp's `(xAnimAssetTable*)indata` reads a big-endian count of 74
// as 1,241,513,984 and walks off the end of memory. A PS2 set, which the port
// cannot read, would do the same, far from the cause.
//
// The PLAT chunk says which: iGCPackage.h reads it. iHipPlatform asks it of
// any package; iFileAssetPlatform of the asset root's boot.HIP. Writes "" to
// both outputs when there is no answer.
static iGCPlatform iHipPlatform(const char* path, char* id, size_t idsize, char* name,
                                size_t namesize)
{
    id[0] = '\0';
    name[0] = '\0';

    FILE* fp = fopen(path, "rb");
    if (fp == NULL)
    {
        return IGCPLAT_NONE;
    }

    U8 head[512];
    size_t got = fread(head, 1, sizeof(head), fp);
    fclose(fp);

    return iGCPackagePlatform(head, (U32)got, id, (U32)idsize, name, (U32)namesize);
}

iGCPlatform iFilePackagePlatform(const char* path)
{
    // A package is asked for each reader that opens it (the packer, the sound
    // and cutscene readers), so the answer is kept by path until the file's
    // size or time changes.
    struct Entry
    {
        std::string path;
        U64 size;
        S64 mtime;
        iGCPlatform platform;
    };
    static std::vector<Entry> sCache;
    static std::mutex sCacheLock;

    iHostFileInfo info;
    if (path == NULL || !iHostStat(path, &info) || !info.is_file)
    {
        return IGCPLAT_NONE;
    }

    std::lock_guard<std::mutex> hold(sCacheLock);
    for (size_t i = 0; i < sCache.size(); i++)
    {
        if (sCache[i].path == path)
        {
            if (sCache[i].size == info.size && sCache[i].mtime == info.mtime)
            {
                return sCache[i].platform;
            }
            sCache.erase(sCache.begin() + i);
            break;
        }
    }

    char id[8];
    char name[32];
    Entry e = { path, info.size, info.mtime, iHipPlatform(path, id, sizeof(id), name, sizeof(name)) };
    if (sCache.size() < 512)
    {
        sCache.push_back(e);
    }
    return e.platform;
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

    iGCPlatform platform = iHipPlatform(path, sId, sizeof(sId), sName, sizeof(sName));
    if (platform == IGCPLAT_NONE || platform == IGCPLAT_NOTPACKAGE)
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

// Whether a mod's package that is not a GameCube one (those are checked as
// they are converted) has a table of contents the packer can read
// (iModPkgCheck): a mod cut short, or damaged in its header, is refused before
// the packer runs off it. Kept by path until the file's size or time changes,
// as the sound layer opens a package again for each stream it plays.
static bool iModPackageHoldsTogether(const char* path, std::string* why)
{
    struct Entry
    {
        std::string path;
        U64 size;
        S64 mtime;
        bool ok;
        std::string why;
    };
    static std::vector<Entry> sCache;
    static std::mutex sCacheLock;

    iHostFileInfo info;
    if (!iHostStat(path, &info) || !info.is_file)
    {
        *why = "it cannot be opened";
        return false;
    }

    std::lock_guard<std::mutex> hold(sCacheLock);
    for (size_t i = 0; i < sCache.size(); i++)
    {
        if (sCache[i].path == path)
        {
            if (sCache[i].size == info.size && sCache[i].mtime == info.mtime)
            {
                *why = sCache[i].why;
                return sCache[i].ok;
            }
            sCache.erase(sCache.begin() + i);
            break;
        }
    }

    Entry e = { path, info.size, info.mtime, false, std::string() };
    e.ok = iModPkgCheck(path, false, &e.why);
    *why = e.why;
    if (sCache.size() < 512)
    {
        sCache.push_back(e);
    }
    return e.ok;
}

// The mod folder's copy of a file under the asset root. When the mod has one,
// its path is written over `path` and the result is true.
//
// Matched on the path relative to the asset root, so `gl/gl01.HIP` in the mod
// folder replaces `gl/gl01.HIP` in the assets, and whatever SB.INI's PATH= did
// to the base path applies to both. A path outside the asset root is left
// alone.
//
// A GameCube package is read through its converted view (iModOpenPackage),
// and is used only if it converts: one that cannot (a table of contents that
// does not hold together, an asset with no rule, or one that does not fit its
// own counts) is refused with the reason, and the original is read. An Xbox
// package is used only if its table of contents holds together
// (iModPkgCheck). A file with a package's name that is not a package (empty,
// or not a HIPA file) is refused, and so is a package from any other
// console's release: the port understands the Xbox and GameCube layouts only,
// and a PS2 HIP loads and then crashes far from the cause. See
// iFileAssetPlatform.
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
        // TSSM's Xbox packs say "BX" where BfBB's say "XB" (heavyironmodding.org).
        // A package with no PLAT is read as Xbox, as before.
        iGCPlatform platform = iFilePackagePlatform(candidate);
        if (platform == IGCPLAT_NOTPACKAGE)
        {
            if (iModFirstReport(rel))
            {
                printf("tssm: mod: %s is not a HIP/HOP package (empty, or damaged at the "
                       "start); using the original\n",
                       rel);
                fflush(stdout);
            }
            return false;
        }
        if (platform == IGCPLAT_OTHER)
        {
            if (iModFirstReport(rel))
            {
                char id[8];
                char unused[32];
                iHipPlatform(candidate, id, sizeof(id), unused, sizeof(unused));
                printf("tssm: mod: %s is from the %s release, not the Xbox or GameCube one; "
                       "using the original\n",
                       rel, id);
                fflush(stdout);
            }
            return false;
        }
        if (platform == IGCPLAT_GAMECUBE)
        {
            // Converted now, so that a package that cannot be is refused
            // before anything reads it; the view is kept for the open that
            // follows.
            bool failed = false;
            iModPkgRelease(iModOpenPackage(candidate, true, &failed));
            if (failed)
            {
                if (iModFirstReport(rel))
                {
                    printf("tssm: mod: %s is a GameCube package that cannot be converted "
                           "(above); using the original\n",
                           rel);
                    fflush(stdout);
                }
                return false;
            }
        }
        else
        {
            std::string why;
            if (!iModPackageHoldsTogether(candidate, &why))
            {
                if (iModFirstReport(rel))
                {
                    printf("tssm: mod: %s cannot be read: %s; using the original\n", rel,
                           why.c_str());
                    fflush(stdout);
                }
                return false;
            }
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

iGCPlatform iFilePackagePlatformOf(const char* name)
{
    if (name == NULL)
    {
        return IGCPLAT_NONE;
    }
    char path[sizeof(((tag_iFile*)0)->path)];
    iFileFullPath(name, path);
    if (!iModOverride(path, sizeof(path)))
    {
        iResolveCaseInsensitive(path, sizeof(path));
    }
    return iFilePackagePlatform(path);
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
               "not from the Xbox or GameCube release).\n");
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
#ifdef __ANDROID__
    iAndroidEndProcess(1);
#else
    exit(1);
#endif
}

// The number of world assets (JSP and BSP) in a package's table of contents,
// read through the port's own files (a mod's copy when a mod replaces it); -1
// when the package cannot be opened, -2 when it opens but has no table of
// contents (empty, cut short before its DICT, or not a package). Only the
// front of the file is read: the PACK chunk and the DICT with every AHDR.
static S32 PackageWorldAssets(const char* name)
{
    tag_xFile file = {};
    if (iFileOpen(name, 0, &file) != 0)
    {
        return -1;
    }

    // iFileRead never comes up short -- past the end it gives zeros, as the
    // DVD gave padding -- so the walk stops at the file's length instead.
    const U32 length = file.ps.length > 0 ? (U32)file.ps.length : 0;
    U32 at = 0;
    S32 count = -2;
    U8 head[8];
    std::vector<U8> body;
    while (at + 8 <= length)
    {
        iFileRead(&file, head, 8);
        at += 8;
        const U32 size = ((U32)head[4] << 24) | ((U32)head[5] << 16) | ((U32)head[6] << 8) | head[7];
        if (memcmp(head, "HIPA", 4) == 0)
        {
            continue;
        }
        // A chunk past the end, or a table of contents larger than this, is
        // not a package's.
        if (size > length - at || size > (16u << 20))
        {
            break;
        }
        body.resize(size);
        if (size > 0)
        {
            iFileRead(&file, &body[0], size);
        }
        at += size;
        if (memcmp(head, "DICT", 4) != 0)
        {
            continue;
        }

        // DICT holds ATOC, which holds AINF and one AHDR per asset: id, then
        // the type, big-endian chunk sizes on every console.
        count = 0;
        for (U32 o = 0; o + 8 <= size;)
        {
            const U32 n = ((U32)body[o + 4] << 24) | ((U32)body[o + 5] << 16) |
                          ((U32)body[o + 6] << 8) | body[o + 7];
            if (memcmp(&body[o], "ATOC", 4) == 0)
            {
                o += 8;
                continue;
            }
            if (memcmp(&body[o], "AHDR", 4) == 0 && n >= 8 && o + 16 <= size &&
                (memcmp(&body[o + 12], "JSP ", 4) == 0 || memcmp(&body[o + 12], "BSP ", 4) == 0))
            {
                count++;
            }
            // A chunk that runs past the table is not a package's, and one
            // large enough would bring the walk back to where it has been.
            if (n > size - o - 8)
            {
                break;
            }
            o += 8 + n;
        }
        break;
    }

    iFileClose(&file);
    return count;
}

void iFileOptionalPackage(U32 sid, S32 options, const char* filename)
{
    char tag[5];
    for (S32 i = 0; i < 4; i++)
    {
        char c = (char)(sid >> (24 - 8 * i));
        tag[i] = (c >= ' ' && c <= '~') ? c : '?';
    }
    tag[4] = '\0';

    char subdir[64];
    char root[64];
    if (filename != NULL)
    {
        snprintf(subdir, sizeof(subdir), "%s", filename);
        root[0] = '\0';
    }
    else
    {
        snprintf(subdir, sizeof(subdir), "%.2s/%s.HOP", tag, tag);
        snprintf(root, sizeof(root), "%s.HOP", tag);
    }

    // There but not opened by the packer (cut short, say), which retail would
    // only crash on later: the same message as a HIP's. A HOP that opens but
    // holds no game data is not caught here, and crashes later as retail would.
    if (iFileExists(subdir, 0) || (root[0] != '\0' && iFileExists(root, 0)))
    {
        iFileMissingPackage(sid, options, filename);
        return;
    }

    // The HIP xSTPreLoadScene will open next, as it looks for it: a level made
    // as one HIP carries its own world (JSP assets); every level of both discs
    // keeps its world in the HOP and none in the HIP, and its HIP alone crashes
    // the packer on the first asset that needs the HOP (ATBL_Read). A HIP
    // without a world, or one that is not a package at all, needs the HOP that
    // is not there: a copied file set that lost it, named as missing. No HIP at
    // all is left to the HIP's own check.
    char hip[64];
    char hipRoot[64];
    if (filename != NULL)
    {
        snprintf(hip, sizeof(hip), "%s", filename);
        char* dot = strrchr(hip, '.');
        if (dot != NULL && (size_t)(dot - hip) + 4 < sizeof(hip))
        {
            strcpy(dot, ".HIP");
        }
        hipRoot[0] = '\0';
    }
    else
    {
        snprintf(hip, sizeof(hip), "%.2s/%s.HIP", tag, tag);
        snprintf(hipRoot, sizeof(hipRoot), "%s.HIP", tag);
    }
    S32 world = PackageWorldAssets(hip);
    if (world == -1 && hipRoot[0] != '\0')
    {
        world = PackageWorldAssets(hipRoot);
    }
    if (world == 0 || world == -2)
    {
        printf("tssm: %s is not there, and the level's HIP %s\n", subdir,
               world == 0 ? "has no world of its own" : "is not a package");
        iFileMissingPackage(sid, options, filename);
        return;
    }

    if (world > 0)
    {
        printf("tssm: %s is not there; the level loads from its HIP alone, as on the console\n",
               subdir);
        fflush(stdout);
    }
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
        printf("tssm:   than an error. (A GameCube disc extracted with Dolphin: its\n");
        printf("tssm:   files/ folder, or the folder holding files/ and sys/.)\n");
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

// A GameCube game file that cannot be converted (iMod.h has why, printed
// already): the same box as for a scene package that does not load, and the
// end of the run, rather than a game reading half-converted data.
static void reportUnreadablePackage(const char* path)
{
    printf("tssm: FATAL -- a game file could not be read: %s\n", path);
    printf("tssm:   it is there, but it is a GameCube package the port cannot convert "
           "(damaged, or changed by a tool).\n");
    fflush(stdout);

#ifdef __3DS__
    const char* closing = "";
#else
    const char* closing = "\n\nThe game will close now.";
#endif
    char message[1536];
    snprintf(message, sizeof(message),
             "A game file could not be read:\n\n%s\n\n"
             "It is there, but the game could not load it. Copy it again from your "
             "game files.%s",
             path, closing);
    iHostErrorBox(TSSM_GAME_TITLE, message);
    exit(1);
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

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    sOpenedAny = 1;

    // A package a mod replaces assets in is read through the modded view of
    // it, whoever opens it: the packer, xTextureManager, the sound and
    // cutscene readers. NULL -- the file as it is -- otherwise.
    ps->view = NULL;
    if (!(flags & IFILE_OPEN_WRITE) && iHasPackageExtension(ps->path))
    {
        // TSSM_TEST_AUDIO_RESTART's "load" moment (iAudioSDLTest.h).
        iAudioSDLTestLoad(ps->path);

        // A GameCube package is converted to the Xbox layout in its view
        // (iMod.h), mods or not.
        const bool gamecube = iFilePackagePlatform(ps->path) == IGCPLAT_GAMECUBE;
        bool failed = false;
        ps->view = iModOpenPackage(ps->path, gamecube, &failed);
        if (failed)
        {
            // Only a game file gets here: a mod's package that cannot be
            // converted was refused in iModOverride.
            reportUnreadablePackage(ps->path);
        }
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

    // And a chance for the loading screen's next frame (iLoadFrame.h).
    iLoadFramePump(iLOADPUMP_READ);

    tag_iFile* ps = &file->ps;
    FILE* fp = (FILE*)ps->handle;

    if (fp == NULL)
    {
        return 0;
    }

    if (ps->view != NULL)
    {
        iModPkgRead(ps->view, fp, (U32)ps->offset, buf, size);
        ps->offset += (S32)size;
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

    // xSTLoadStep ends here: one package layer is in, and the loading screen
    // can have a frame (iLoadFrame.h).
    iLoadFramePump(iLOADPUMP_STEP);
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
