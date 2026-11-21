// PC-only: the save store's other consoles -- the little-endian payload the
// PlayStation 2 and Xbox share, with the level packages read off the disk to
// convert it, and the Xbox's save folders. See iSaveStore.h, iSaveLE.h,
// iSaveXbox.h and docs/SAVES.md sections 6 and 7.
//
// Only iHost.h's file calls and the C library: nothing here is Windows's.
#include "iSaveStore.h"
#include "iSaveFormat.h"
#include "iSaveLE.h"
#include "iSavePS2.h"
#include "iSaveXbox.h"
#include "iSaveZip.h"
#include "iConfig.h"
#include "iFile.h"
#include "iHost.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static void iSSX_err(char* err, size_t errsize, const char* fmt, ...)
{
    if (err != NULL && errsize > 0)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, errsize, fmt, ap);
        va_end(ap);
    }
}

// --- finding files -----------------------------------------------------------------------

static std::string iSSX_slashes(const char* path)
{
    std::string s = path != NULL ? path : "";
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '\\')
        {
            s[i] = '/';
        }
    }
    while (s.size() > 1 && s[s.size() - 1] == '/' && s[s.size() - 2] != ':')
    {
        s.erase(s.size() - 1);
    }
    return s;
}

static std::string iSSX_baseName(const std::string& path)
{
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static bool iSSX_isDir(const std::string& path)
{
    iHostFileInfo st;
    return iHostStat(path.c_str(), &st) && st.is_dir;
}

static bool iSSX_isFile(const std::string& path)
{
    iHostFileInfo st;
    return iHostStat(path.c_str(), &st) && st.is_file;
}

// `name` in `dir`, however its letters are cased on disk (the discs' own files
// are "bb01.HIP" on one and "BB01.HIP" on another). "" when it is not there.
static std::string iSSX_child(const std::string& dir, const char* name)
{
    std::string direct = dir + "/" + name;
    if (iHostPathExists(direct.c_str()))
    {
        return direct;
    }

    std::string found;
    iHostDir* d = iHostDirOpen(dir.c_str());
    if (d != NULL)
    {
        const char* e;
        while ((e = iHostDirNext(d)) != NULL)
        {
            if (iHostStrCaseCmp(e, name) == 0)
            {
                found = dir + "/" + e;
                break;
            }
        }
        iHostDirClose(d);
    }
    return found;
}

// A path of several components under `root`, each found whatever its case.
static std::string iSSX_find(const std::string& root, const char* rel)
{
    std::string at = root;
    std::string part;
    for (const char* c = rel;; c++)
    {
        if (*c == '/' || *c == '\0')
        {
            if (!part.empty())
            {
                at = iSSX_child(at, part.c_str());
                if (at.empty())
                {
                    return at;
                }
                part.clear();
            }
            if (*c == '\0')
            {
                break;
            }
        }
        else
        {
            part.push_back(*c);
        }
    }
    return at;
}

// --- the level packages ---------------------------------------------------------------------

static std::string sAssetRoot;
static bool sAssetRootSet;

void iSaveStoreSetAssetRoot(const char* dir)
{
    sAssetRootSet = (dir != NULL && dir[0] != '\0');
    sAssetRoot = sAssetRootSet ? iSSX_slashes(dir) : "";
}

static std::string iSSX_assetRoot()
{
    if (sAssetRootSet)
    {
        return sAssetRoot;
    }
    std::string root = iSSX_slashes(iFileAssetRoot());
    return root.empty() ? "." : root;
}

static bool iSSX_fileRead(void* ctx, U64 offset, void* buf, size_t n)
{
    FILE* f = (FILE*)ctx;
    if (offset > 0x7FFFFFFFu || fseek(f, (long)offset, SEEK_SET) != 0)
    {
        return false;
    }
    return fread(buf, 1, n, f) == n;
}

namespace
{
    struct Scanned
    {
        U64 size;
        S64 mtime;
        bool ok;
        iSaveObjectCounts counts;
    };

    // What each package scanned so far holds, until the file changes: the
    // Saves tab looks at a save and then imports it, which asks twice.
    std::map<std::string, Scanned> sScanned;

    // What a set of field maps is worked out from.
    struct Maps
    {
        std::string root;
        std::string mod;
        bool haveGlobals;
        bool globalsOK;
        std::string globalsWhy;
        iSaveObjectCounts globals;
    };
} // namespace

static void iSSX_add(iSaveObjectCounts* to, const iSaveObjectCounts* from)
{
    for (int k = 0; k < ISLE_KINDS; k++)
    {
        to->n[k] += from->n[k];
    }
    if (from->taskCounters >= 0)
    {
        to->taskCounters = from->taskCounters;
    }
}

// The package at `rel` (under the mod folder if a mod supplies it, else the
// asset folder): 1 scanned into `counts`, 0 not there, -1 unreadable (`why`).
static S32 iSSX_scan(const Maps* m, const char* rel, iSaveObjectCounts* counts, std::string* why)
{
    std::string path;
    if (!m->mod.empty())
    {
        path = iSSX_find(m->mod, rel);
    }
    if (path.empty() || !iSSX_isFile(path))
    {
        path = iSSX_find(m->root, rel);
    }
    if (path.empty() || !iSSX_isFile(path))
    {
        return 0;
    }

    iHostFileInfo st;
    if (!iHostStat(path.c_str(), &st))
    {
        return 0;
    }

    std::map<std::string, Scanned>::iterator it = sScanned.find(path);
    if (it == sScanned.end() || it->second.size != st.size || it->second.mtime != st.mtime)
    {
        Scanned s;
        s.size = st.size;
        s.mtime = st.mtime;
        iSaveObjectCountsClear(&s.counts);

        char err[200] = "";
        FILE* f = fopen(path.c_str(), "rb");
        s.ok = (f != NULL) && iSaveHipScan(iSSX_fileRead, f, st.size, &s.counts, err, sizeof(err));
        if (f != NULL)
        {
            fclose(f);
        }
        else
        {
            snprintf(err, sizeof(err), "cannot open it");
        }
        if (!s.ok)
        {
            *why = path + ": " + err;
            return -1;
        }
        sScanned[path] = s;
        it = sScanned.find(path);
    }

    iSSX_add(counts, &it->second.counts);
    return 1;
}

// The packages that stay loaded whatever the level: their saved objects are in
// every level's list, and MNUC has the task counters.
static bool iSSX_globals(Maps* m, char* err, size_t errsize)
{
    if (!m->haveGlobals)
    {
        m->haveGlobals = true;
        m->globalsOK = true;
        iSaveObjectCountsClear(&m->globals);

        static const char* const kAlways[] = { "boot.HIP", "boot_US.HIP", "font.HIP",
                                               "font_US.HIP", "plat.HIP", "MN/mnuc_US.HIP" };
        for (size_t i = 0; i < sizeof(kAlways) / sizeof(kAlways[0]) && m->globalsOK; i++)
        {
            if (iSSX_scan(m, kAlways[i], &m->globals, &m->globalsWhy) < 0)
            {
                m->globalsOK = false;
            }
        }

        if (m->globalsOK)
        {
            S32 got = iSSX_scan(m, "MN/mnuc.HIP", &m->globals, &m->globalsWhy);
            if (got == 0)
            {
                m->globalsWhy = "MN/mnuc.HIP is not in " + m->root +
                                " (the game's own files are needed to convert a save between "
                                "consoles; set the asset folder)";
            }
            else if (got > 0 && m->globals.taskCounters < 0)
            {
                m->globalsWhy = "MN/mnuc.HIP in " + m->root + " has no task counter group";
            }
            m->globalsOK = (got > 0 && m->globals.taskCounters >= 0);
        }
    }

    if (!m->globalsOK)
    {
        iSSX_err(err, errsize, "%s", m->globalsWhy.c_str());
    }
    return m->globalsOK;
}

static bool iSSX_fieldMap(void* ctx, U32 tag, iSaveFieldMap* map, char* err, size_t errsize)
{
    Maps* m = (Maps*)ctx;

    if (tag == ISF_TAG('P', 'L', 'Y', 'R'))
    {
        iSaveFieldsPlayer(map);
        return true;
    }

    if (!iSSX_globals(m, err, errsize))
    {
        return false;
    }

    if (tag == ISF_TAG('C', 'N', 'T', 'R'))
    {
        iSaveFieldsCounters(m->globals.taskCounters, map);
        return true;
    }

    // A level: its id is its package's name, in the folder named by its first
    // two characters. The id comes out of a save file, so it is checked before
    // it is made into a path.
    char id[5];
    for (int i = 0; i < 4; i++)
    {
        char c = (char)(tag >> (24 - 8 * i));
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
        {
            iSSX_err(err, errsize, "its name is not a level's");
            return false;
        }
        id[i] = c;
    }
    id[4] = '\0';

    iSaveObjectCounts counts = m->globals;
    std::string why;
    char rel[32];

    snprintf(rel, sizeof(rel), "%c%c/%s.HIP", id[0], id[1], id);
    S32 got = iSSX_scan(m, rel, &counts, &why);
    if (got == 0)
    {
        iSSX_err(err, errsize,
                 "the level's file %s is not in %s (the game's own files are needed to convert "
                 "a save between consoles)",
                 rel, m->root.c_str());
        return false;
    }

    if (got > 0)
    {
        snprintf(rel, sizeof(rel), "%c%c/%s.HOP", id[0], id[1], id);
        got = iSSX_scan(m, rel, &counts, &why);
    }
    if (got >= 0)
    {
        snprintf(rel, sizeof(rel), "%c%c/%s_US.HIP", id[0], id[1], id);
        got = iSSX_scan(m, rel, &counts, &why);
    }
    if (got < 0)
    {
        iSSX_err(err, errsize, "%s", why.c_str());
        return false;
    }

    iSaveFieldsScene(&counts, map);
    return true;
}

static void iSSX_maps(Maps* m)
{
    m->root = iSSX_assetRoot();
    m->mod = sAssetRootSet ? "" : iSSX_slashes(iFileModRoot());
    if (!m->mod.empty() && !iSSX_isDir(m->mod))
    {
        m->mod.clear();
    }
    m->haveGlobals = false;
    m->globalsOK = false;
}

bool iSaveStoreToLE(const U8* canonical, size_t n, std::vector<U8>& le, std::string* notes,
                    char* err, size_t errsize)
{
    Maps m;
    iSSX_maps(&m);
    return iSavePayloadLE(ISLE_TO_LE, canonical, n, le, iSSX_fieldMap, &m, notes, err, errsize);
}

bool iSaveStoreFromLE(const U8* le, size_t n, std::vector<U8>& canonical, std::string* notes,
                      char* err, size_t errsize)
{
    Maps m;
    iSSX_maps(&m);
    return iSavePayloadLE(ISLE_FROM_LE, le, n, canonical, iSSX_fieldMap, &m, notes, err, errsize);
}

// --- the player's Xbox files ----------------------------------------------------------------

void iSaveXboxExportDefaults(iSaveXboxExportOptions* o)
{
    o->signature = ISS_XBOX_EMULATOR;
    o->xbe[0] = '\0';
    o->keys[0] = '\0';
    o->number = -1;
    o->replace = false;
}

bool iSaveStoreXboxProgram(const char* given, char* path, size_t pathsize, char* err,
                           size_t errsize)
{
    path[0] = '\0';

    const char* named = (given != NULL && given[0] != '\0') ? given :
                                                               iConfigGetString("game.xbox_xbe", "");
    if (named != NULL && named[0] != '\0')
    {
        std::string p = iSSX_slashes(named);
        if (iSSX_isDir(p))
        {
            p = iSSX_child(p, "default.xbe");
        }
        if (p.empty() || !iSSX_isFile(p))
        {
            iSSX_err(err, errsize, "there is no Xbox program at %s (it is the DEFAULT.XBE from "
                                   "your own copy of the Xbox game)",
                     named);
            return false;
        }
        snprintf(path, pathsize, "%s", p.c_str());
        return true;
    }

    std::string root = iSSX_assetRoot();
    std::string p = iSSX_child(root, "default.xbe");
    if (p.empty() || !iSSX_isFile(p))
    {
        iSSX_err(err, errsize,
                 "an Xbox save has to be signed, and that needs the Xbox program: DEFAULT.XBE "
                 "from your own copy of the Xbox game. There is none in the game's files (%s); "
                 "choose the file",
                 root.c_str());
        return false;
    }
    snprintf(path, pathsize, "%s", p.c_str());
    return true;
}

bool iSaveStoreXboxKeys(const char* given, char* path, size_t pathsize, char* err, size_t errsize)
{
    path[0] = '\0';

    const char* named = (given != NULL && given[0] != '\0') ? given :
                                                               iConfigGetString("game.xbox_keys", "");
    if (named == NULL || named[0] == '\0')
    {
        iSSX_err(err, errsize,
                 "a save for a real Xbox or xemu needs your keys.bin: the 32-byte file made "
                 "from your own Xbox that Cxbx-Reloaded also reads. Choose the file (the port "
                 "has no key of its own)");
        return false;
    }

    std::string p = iSSX_slashes(named);
    if (iSSX_isDir(p))
    {
        p = iSSX_child(p, "keys.bin");
    }
    if (p.empty() || !iSSX_isFile(p))
    {
        iSSX_err(err, errsize, "there is no keys.bin at %s", named);
        return false;
    }
    snprintf(path, pathsize, "%s", p.c_str());
    return true;
}

static bool iSSX_loadTitle(const char* given, iSaveXboxTitle* title, char* err, size_t errsize)
{
    char path[600];
    if (!iSaveStoreXboxProgram(given, path, sizeof(path), err, errsize))
    {
        return false;
    }

    std::vector<U8> xbe;
    char why[300] = "";
    if (!iSaveStoreReadFile(path, xbe, why, sizeof(why)) || xbe.empty())
    {
        iSSX_err(err, errsize, "%s", why[0] != '\0' ? why : "the Xbox program is empty");
        return false;
    }
    if (!iSaveXbeParse(&xbe[0], xbe.size(), title, why, sizeof(why)))
    {
        iSSX_err(err, errsize, "%s: %s", path, why);
        return false;
    }
    return true;
}

// The title's signature key for one of the two signatures. The key bytes stay
// in `key`, which the caller clears; only paths go into messages.
static bool iSSX_titleKey(S32 signature, const char* givenKeys, const iSaveXboxTitle* title,
                          U8 key[ISX_KEY_SIZE], char* err, size_t errsize)
{
    if (signature == ISS_XBOX_EMULATOR)
    {
        iSaveXboxTitleKey(title, NULL, key);
        return true;
    }

    char path[600];
    if (!iSaveStoreXboxKeys(givenKeys, path, sizeof(path), err, errsize))
    {
        return false;
    }

    std::vector<U8> file;
    char why[300] = "";
    if (!iSaveStoreReadFile(path, file, why, sizeof(why)))
    {
        iSSX_err(err, errsize, "%s", why);
        return false;
    }

    U8 cert[ISX_KEY_SIZE];
    bool ok = iSaveXboxKeysParse(file.empty() ? NULL : &file[0], file.size(), cert, why,
                                 sizeof(why));
    if (ok)
    {
        iSaveXboxTitleKey(title, cert, key);
    }
    else
    {
        iSSX_err(err, errsize, "%s: %s", path, why);
    }

    memset(cert, 0, sizeof(cert));
    if (!file.empty())
    {
        memset(&file[0], 0, file.size());
    }
    return ok;
}

bool iSaveStoreXboxProgramUsable(const char* given, char* path, size_t pathsize, char* err,
                                 size_t errsize)
{
    if (!iSaveStoreXboxProgram(given, path, pathsize, err, errsize))
    {
        return false;
    }

    iSaveXboxTitle title;
    memset(title.certificateField, 0, sizeof(title.certificateField));
    bool ok = iSSX_loadTitle(path, &title, err, errsize);
    memset(title.certificateField, 0, sizeof(title.certificateField));
    return ok;
}

bool iSaveStoreXboxKeysUsable(const char* given, char* path, size_t pathsize, char* err,
                              size_t errsize)
{
    if (!iSaveStoreXboxKeys(given, path, pathsize, err, errsize))
    {
        return false;
    }

    U8 cert[ISX_KEY_SIZE];
    char why[300] = "";
    bool ok = false;

    // A file of another size is not read at all: its size is the reason.
    iHostFileInfo st;
    if (!iHostStat(path, &st) || !st.is_file)
    {
        snprintf(why, sizeof(why), "the file cannot be read");
    }
    else if (st.size != ISX_KEYS_FILE_SIZE)
    {
        iSaveXboxKeysParse(NULL, (size_t)(st.size > 0x7FFFFFFF ? 0x7FFFFFFF : st.size), cert, why,
                           sizeof(why));
    }
    else
    {
        std::vector<U8> file;
        ok = iSaveStoreReadFile(path, file, why, sizeof(why)) &&
             iSaveXboxKeysParse(file.empty() ? NULL : &file[0], file.size(), cert, why,
                                sizeof(why));
        if (!file.empty())
        {
            memset(&file[0], 0, file.size());
        }
    }
    memset(cert, 0, sizeof(cert));

    if (!ok)
    {
        iSSX_err(err, errsize, "%s: %s", path, why);
    }
    return ok;
}

// --- reading an Xbox save -------------------------------------------------------------------

// The files an import was told to check signatures with. Paths only.
static std::string sLookXbe;
static std::string sLookKeys;

void iSaveStoreXboxLookWith(const char* xbe, const char* keys)
{
    sLookXbe = xbe != NULL ? xbe : "";
    sLookKeys = keys != NULL ? keys : "";
}

bool iSaveStoreXboxDecode(const U8* xsv, size_t n, std::vector<U8>& canonical,
                          std::string* signature, std::string* notes, char* err, size_t errsize)
{
    canonical.clear();
    if (signature != NULL)
    {
        *signature = "";
    }

    if (!iSavePayloadIsLE(xsv, n))
    {
        iSSX_err(err, errsize, "not a PlayStation 2 or Xbox save: it does not start with the "
                               "game's header");
        return false;
    }

    U32 size = iSavePayloadLESize(xsv);
    if ((size_t)size > n)
    {
        iSSX_err(err, errsize, "the save is cut short (%lu bytes of %lu)", (unsigned long)n,
                 (unsigned long)size);
        return false;
    }

    if (!iSaveStoreFromLE(xsv, size, canonical, notes, err, errsize))
    {
        return false;
    }

    if (signature != NULL && n == (size_t)size + ISX_SIGNATURE_SIZE)
    {
        // Which signature it carries, when the files to tell are at hand.
        // Nothing is asked for and nothing is refused here.
        *signature = "signature not checked";

        iSaveXboxTitle title;
        memset(title.certificateField, 0, sizeof(title.certificateField));
        if (iSSX_loadTitle(sLookXbe.empty() ? NULL : sLookXbe.c_str(), &title, NULL, 0))
        {
            U8 key[ISX_KEY_SIZE];
            *signature = "signature not recognised";

            iSaveXboxTitleKey(&title, NULL, key);
            if (iSaveXboxVerify(key, xsv, n))
            {
                *signature = "emulator signature";
            }
            else if (iSSX_titleKey(ISS_XBOX_REAL, sLookKeys.empty() ? NULL : sLookKeys.c_str(),
                                   &title, key, NULL, 0) &&
                     iSaveXboxVerify(key, xsv, n))
            {
                *signature = "real signature (matches your keys.bin)";
            }
            memset(key, 0, sizeof(key));
        }
        memset(title.certificateField, 0, sizeof(title.certificateField));
    }
    else if (signature != NULL)
    {
        *signature = "no signature";
    }

    return true;
}

namespace
{
    struct Walk
    {
        bool deep; // the folders below too
        std::string root;
        std::vector<iSaveZipEntry> entries;
        S32 visited;
        S32 saves;
    };
} // namespace

static void iSSX_addFile(Walk* w, const std::string& path, const std::string& rel)
{
    iHostFileInfo st;
    if (!iHostStat(path.c_str(), &st) || !st.is_file || st.size > 1024 * 1024)
    {
        return;
    }
    iSaveZipEntry e;
    if (!iSaveStoreReadFile(path.c_str(), e.data, NULL, 0))
    {
        return;
    }
    e.name = rel;
    e.mtime = st.mtime;
    w->entries.push_back(e);
}

// Every folder under `dir` that holds a GameData.xsv, a few levels down: far
// enough for an emulator's data folder (EmuDisk/Partition1/UDATA/<title>/<save>).
static void iSSX_walk(Walk* w, const std::string& dir, const std::string& rel, S32 depth)
{
    if (depth > 7 || w->visited > 20000 || w->saves >= 64)
    {
        return;
    }

    std::vector<std::string> names;
    iHostDir* d = iHostDirOpen(dir.c_str());
    if (d == NULL)
    {
        return;
    }
    const char* e;
    while ((e = iHostDirNext(d)) != NULL && names.size() < 4096)
    {
        names.push_back(e);
    }
    iHostDirClose(d);

    std::string meta;
    for (size_t i = 0; i < names.size(); i++)
    {
        if (iHostStrCaseCmp(names[i].c_str(), ISX_SAVE_META) == 0)
        {
            meta = names[i];
        }
    }

    for (size_t i = 0; i < names.size(); i++)
    {
        w->visited++;
        std::string path = dir + "/" + names[i];
        std::string r = rel.empty() ? names[i] : rel + "/" + names[i];

        if (iHostStrCaseCmp(names[i].c_str(), ISX_SAVE_FILE) == 0)
        {
            size_t before = w->entries.size();
            iSSX_addFile(w, path, r);
            if (w->entries.size() > before)
            {
                w->saves++;
                if (!meta.empty())
                {
                    iSSX_addFile(w, dir + "/" + meta, rel.empty() ? meta : rel + "/" + meta);
                }
            }
        }
        else if (iSavePS2GameOfName(names[i].c_str()) >= 0)
        {
            // A PlayStation 2 save file (a folder-type card's, or a copy of
            // the game's folder): the little-endian payload, bare. The PC's
            // own save files have the same names and are not these.
            size_t before = w->entries.size();
            iSSX_addFile(w, path, r);
            if (w->entries.size() > before)
            {
                const std::vector<U8>& data = w->entries.back().data;
                if (!data.empty() && iSavePayloadIsLE(&data[0], data.size()))
                {
                    w->saves++;
                }
                else
                {
                    w->entries.pop_back();
                }
            }
        }
        else if (w->deep && iSSX_isDir(path))
        {
            iSSX_walk(w, path, r, depth + 1);
        }
    }
}

bool iSaveStoreReadFolder(const char* dir, std::vector<U8>& zip, char* err, size_t errsize,
                          bool deep)
{
    zip.clear();

    Walk w;
    w.deep = deep;
    w.root = iSSX_slashes(dir);
    w.visited = 0;
    w.saves = 0;

    if (!iSSX_isDir(w.root))
    {
        iSSX_err(err, errsize, "%s is not a folder", dir);
        return false;
    }

    // The folder's own name leads each entry's path, so a save's folder picked
    // on its own still says which save it is.
    iSSX_walk(&w, w.root, iSSX_baseName(w.root), 0);

    if (w.saves == 0)
    {
        iSSX_err(err, errsize,
                 "there is no Xbox or PlayStation 2 save under %s: an Xbox save is a folder "
                 "holding GameData.xsv (on an Xbox, E:\\UDATA\\54510027\\<12 characters>), a "
                 "PlayStation 2 one a folder holding SpongeBobMovie01 to 03. GameCube saves "
                 "are files: import the .gci files themselves",
                 dir);
        return false;
    }

    iSaveZipBuild(w.entries, zip);
    return true;
}

// --- writing one ---------------------------------------------------------------------------

// The game's folder for a destination the player chose.
static std::string iSSX_titleDir(const std::string& dir, U32 titleId)
{
    char id[16];
    snprintf(id, sizeof(id), "%08lx", (unsigned long)titleId);

    // Cxbx-Reloaded's data folder keeps the hard disk's partitions as folders.
    std::string emu = iSSX_child(dir, "EmuDisk");
    if (!emu.empty() && iSSX_isDir(emu))
    {
        std::string part = iSSX_child(emu, "Partition1");
        if (!part.empty() && iSSX_isDir(part))
        {
            std::string udata = iSSX_child(part, "UDATA");
            if (udata.empty())
            {
                udata = part + "/UDATA";
            }
            std::string title = iSSX_child(udata, id);
            return title.empty() ? udata + "/" + id : title;
        }
    }

    std::string base = iSSX_baseName(dir);
    if (iHostStrCaseCmp(base.c_str(), id) == 0)
    {
        return dir;
    }
    if (iHostStrCaseCmp(base.c_str(), "UDATA") == 0)
    {
        std::string title = iSSX_child(dir, id);
        return title.empty() ? dir + "/" + id : title;
    }
    return dir + "/UDATA/" + id;
}

static bool iSSX_writeIfMissing(const std::string& path, const std::vector<U8>& data, char* err,
                                size_t errsize)
{
    iHostFileInfo st;
    if (data.empty() || (iHostStat(path.c_str(), &st) && st.is_file && st.size > 0))
    {
        return true;
    }
    return iSaveStoreWriteFile(path.c_str(), &data[0], data.size(), false, err, errsize);
}

bool iSaveStoreExportXbox(S32 target, S32 game, const iSaveXboxExportOptions* o, const char* dir,
                          std::string* where, char* err, size_t errsize)
{
    iSaveXboxExportOptions defaults;
    if (o == NULL)
    {
        iSaveXboxExportDefaults(&defaults);
        o = &defaults;
    }

    S32 number = (o->number >= 0) ? o->number : game;
    if (number < 0 || number >= ISS_GAMES)
    {
        iSSX_err(err, errsize, "an Xbox holds saves 1 to %d", ISS_GAMES);
        return false;
    }
    if (dir == NULL || dir[0] == '\0')
    {
        iSSX_err(err, errsize, "no folder to export to");
        return false;
    }

    iSaveSlotInfo info;
    iSaveStoreSlot(target, game, &info);

    std::vector<U8> payload;
    if (!iSaveStoreRead(target, game, payload, err, errsize))
    {
        return false;
    }

    std::vector<U8> le;
    if (!iSaveStoreToLE(&payload[0], payload.size(), le, NULL, err, errsize))
    {
        return false;
    }

    iSaveXboxTitle title;
    if (!iSSX_loadTitle(o->xbe, &title, err, errsize))
    {
        return false;
    }

    U8 key[ISX_KEY_SIZE];
    std::vector<U8> xsv;
    bool signedOK = iSSX_titleKey(o->signature, o->keys, &title, key, err, errsize);
    if (signedOK)
    {
        iSaveXboxFile(key, &le[0], le.size(), xsv);
    }
    memset(key, 0, sizeof(key));
    memset(title.certificateField, 0, sizeof(title.certificateField));
    if (!signedOK)
    {
        return false;
    }

    char name[32];
    char folder[16];
    iSaveXboxSaveName(number, name, sizeof(name));
    iSaveXboxFolderName(name, folder);

    std::string titleDir = iSSX_titleDir(iSSX_slashes(dir), title.titleId);
    std::string saveDir = titleDir + "/" + folder;
    std::string file = saveDir + "/" + ISX_SAVE_FILE;

    if (!o->replace && iHostPathExists(file.c_str()))
    {
        iSSX_err(err, errsize,
                 "%s is already there (%s); export as another save number, or allow replacing it",
                 name, saveDir.c_str());
        return false;
    }

    // The game's own three files, as its startup code makes them from the XBE
    // when they are missing; then the save's two.
    std::vector<U8> meta;
    iSaveXboxMeta("TitleName", title.titleName, meta);
    if (!iSSX_writeIfMissing(titleDir + "/" + ISX_TITLE_META, meta, err, errsize) ||
        !iSSX_writeIfMissing(titleDir + "/" + ISX_TITLE_IMAGE, title.titleImage, err, errsize) ||
        !iSSX_writeIfMissing(titleDir + "/" + ISX_SAVE_IMAGE, title.saveImage, err, errsize))
    {
        return false;
    }

    iSaveXboxMeta("Name", name, meta);
    if (!iSaveStoreWriteFile((saveDir + "/" + ISX_SAVE_META).c_str(), &meta[0], meta.size(), false,
                             err, errsize) ||
        !iSaveStoreWriteFile(file.c_str(), &xsv[0], xsv.size(), false, err, errsize))
    {
        return false;
    }

    // The load screen's date is this file's.
    if (info.mtime > 0)
    {
        iHostSetFileTime(file.c_str(), info.mtime);
    }

    if (where != NULL)
    {
        *where = saveDir;
    }
    return true;
}

S32 iSaveStoreExportXboxFolder(S32 target, const iSaveXboxExportOptions* o, const char* dir,
                               std::vector<iSaveExportItem>& items)
{
    items.clear();

    iSaveXboxExportOptions opt;
    if (o != NULL)
    {
        opt = *o;
    }
    else
    {
        iSaveXboxExportDefaults(&opt);
    }
    opt.number = -1;

    S32 written = 0;
    for (S32 g = 0; g < ISS_GAMES; g++)
    {
        iSaveSlotInfo info;
        iSaveStoreSlot(target, g, &info);
        if (!info.exists)
        {
            continue;
        }

        iSaveExportItem item;
        snprintf(item.name, sizeof(item.name), "%s", info.name);
        item.ok = false;

        char err[512] = "";
        if (!info.valid)
        {
            item.message = std::string("damaged: ") + info.problem;
        }
        else if (iSaveStoreExportXbox(target, g, &opt, dir, &item.path, err, sizeof(err)))
        {
            item.ok = true;
            written++;
        }
        else
        {
            item.message = err;
        }
        items.push_back(item);
    }
    return written;
}
