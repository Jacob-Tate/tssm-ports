// PC-only: the save store. See iSaveStore.h and docs/SAVES.md.
#include "iSaveStore.h"
#include "iSaveFormat.h"
#include "iSaveLE.h"
#include "iSavePS2.h"
#include "iSaveXbox.h"
#include "iSaveZip.h"
#include "iHost.h"
#include "iFile.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ISS_HEAD_MAGIC "TSSMGCH1"
#define ISS_HEAD_FILE_SIZE (8 + 4 + 4 + ISF_DENTRY_SIZE + ISF_HEAD_SIZE)

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static void iSS_err(char* err, size_t errsize, const char* fmt, ...)
{
    if (err != NULL && errsize > 0)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, errsize, fmt, ap);
        va_end(ap);
    }
}

// --- where ---------------------------------------------------------------------------

static char sRoot[512];
static char sOverride[512];
static char sConfigRoot[512];
static bool sResolved;

void iSaveStoreSetRoot(const char* dir)
{
    snprintf(sOverride, sizeof(sOverride), "%s", dir != NULL ? dir : "");
    sResolved = false;
}

void iSaveStoreSetConfigRoot(const char* dir)
{
    snprintf(sConfigRoot, sizeof(sConfigRoot), "%s", dir != NULL ? dir : "");
    sResolved = false;
}

// The mod in use, by folder name: iFileModName once iFileInit has run, and the
// same thing worked out from the setting before it has (the command line runs
// before iFileInit).
static void iSS_modName(char* out, size_t outsize)
{
    out[0] = '\0';

    const char* name = iFileModName();
    if (name != NULL && name[0] != '\0')
    {
        snprintf(out, outsize, "%s", name);
        return;
    }

    const char* root = iFileModRoot();
    if (root == NULL || root[0] == '\0' || !iHostPathExists(root))
    {
        return;
    }

    size_t n = strlen(root);
    while (n > 0 && (root[n - 1] == '/' || root[n - 1] == '\\'))
    {
        n--;
    }
    size_t start = n;
    while (start > 0 && root[start - 1] != '/' && root[start - 1] != '\\' && root[start - 1] != ':')
    {
        start--;
    }
    snprintf(out, outsize, "%.*s", (int)(n - start), root + start);
}

static void iSS_slashes(char* s)
{
    for (; *s != '\0'; s++)
    {
        if (*s == '\\')
        {
            *s = '/';
        }
    }
}

const char* iSaveStoreRoot()
{
    if (sResolved)
    {
        return sRoot;
    }

    const char* env = getenv("TSSM_SAVE_DIR");
    if (sOverride[0] != '\0')
    {
        snprintf(sRoot, sizeof(sRoot), "%s", sOverride);
    }
    else if (env != NULL && env[0] != '\0')
    {
        snprintf(sRoot, sizeof(sRoot), "%s", env);
    }
    else if (sConfigRoot[0] != '\0')
    {
        snprintf(sRoot, sizeof(sRoot), "%s", sConfigRoot);
    }
    else
    {
        char base[512];
        if (!iHostUserDataDir(base, sizeof(base)))
        {
            base[0] = '\0';
        }
        const char* sep = base[0] != '\0' ? "/tssm/" : "";

        char mod[256];
        iSS_modName(mod, sizeof(mod));
        if (mod[0] != '\0')
        {
            snprintf(sRoot, sizeof(sRoot), "%s%smods/%s/saves", base, sep, mod);
        }
        else
        {
            snprintf(sRoot, sizeof(sRoot), "%s%ssaves", base, sep);
        }
    }

    iSS_slashes(sRoot);
    size_t n = strlen(sRoot);
    while (n > 1 && sRoot[n - 1] == '/')
    {
        sRoot[--n] = '\0';
    }

    sResolved = true;
    return sRoot;
}

// The first folder is the root itself, the second "second" inside it, the rest
// "more/<n>" -- isavegame.cpp's layout from before this file, unchanged.
void iSaveStoreTargetDir(S32 target, char* out, size_t outsize)
{
    const char* root = iSaveStoreRoot();
    if (target <= 0)
    {
        snprintf(out, outsize, "%s", root);
    }
    else if (target == 1)
    {
        snprintf(out, outsize, "%s/second", root);
    }
    else
    {
        snprintf(out, outsize, "%s/more/%d", root, (int)target);
    }
}

static void iSS_fileName(S32 game, char* out, size_t outsize)
{
    snprintf(out, outsize, "%s%02d", ISF_FILE_BASE, (int)game + 1);
}

void iSaveStoreSlotPath(S32 target, S32 game, char* out, size_t outsize)
{
    char dir[512];
    char name[32];
    iSaveStoreTargetDir(target, dir, sizeof(dir));
    iSS_fileName(game, name, sizeof(name));
    snprintf(out, outsize, "%s/%s", dir, name);
}

static void iSS_backupPath(S32 target, S32 game, S32 which, char* out, size_t outsize)
{
    char dir[512];
    char name[32];
    iSaveStoreTargetDir(target, dir, sizeof(dir));
    iSS_fileName(game, name, sizeof(name));
    snprintf(out, outsize, "%s/backup/%s.%d", dir, name, (int)which);
}

void iSaveStoreSlotName(S32 target, S32 game, char* out, size_t outsize)
{
    snprintf(out, outsize, "%c%d", 'A' + (int)target, (int)game + 1);
}

bool iSaveStoreParseSlot(const char* text, S32* target, S32* game)
{
    if (text == NULL)
    {
        return false;
    }

    S32 t = 0;
    const char* p = text;
    if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))
    {
        t = (*p >= 'a') ? *p - 'a' : *p - 'A';
        p++;
    }

    if (p[0] < '1' || p[0] > '0' + ISS_GAMES || p[1] != '\0' || t >= ISS_MAX_TARGETS)
    {
        return false;
    }

    *target = t;
    *game = p[0] - '1';
    return true;
}

// --- files -----------------------------------------------------------------------------

static bool iSS_mkdirs(const char* path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    iSS_slashes(tmp);

    for (char* p = tmp + 1; *p != '\0'; p++)
    {
        if (*p == '/' && p[-1] != ':')
        {
            *p = '\0';
            iHostMakeDir(tmp);
            *p = '/';
        }
    }
    return iHostMakeDir(tmp);
}

static void iSS_parentDir(const char* path, char* out, size_t outsize)
{
    snprintf(out, outsize, "%s", path);
    iSS_slashes(out);
    char* slash = strrchr(out, '/');
    if (slash != NULL && slash != out)
    {
        *slash = '\0';
    }
    else
    {
        snprintf(out, outsize, ".");
    }
}

bool iSaveStoreReadFile(const char* path, std::vector<U8>& out, char* err, size_t errsize)
{
    out.clear();

    FILE* f = fopen(path, "rb");
    if (f == NULL)
    {
        iSS_err(err, errsize, "cannot open %s", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0 || (unsigned long)size > ISS_MAX_INPUT)
    {
        fclose(f);
        iSS_err(err, errsize, "%s is too large to be a save, a card or a zip of saves", path);
        return false;
    }

    out.resize((size_t)size);
    size_t got = size > 0 ? fread(&out[0], 1, (size_t)size, f) : 0;
    fclose(f);

    if (got != (size_t)size)
    {
        iSS_err(err, errsize, "cannot read %s", path);
        out.clear();
        return false;
    }

    return true;
}

static bool iSS_copyFile(const char* from, const char* to)
{
    std::vector<U8> data;
    if (!iSaveStoreReadFile(from, data, NULL, 0))
    {
        return false;
    }
    return iSaveStoreWriteFile(to, data.empty() ? NULL : &data[0], data.size(), false, NULL, 0);
}

// backup/<name>.1 is the newest. Rotates the older ones down and copies (does
// not move) the current file in, so the current file is never missing.
static void iSS_backup(const char* path)
{
    iHostFileInfo st;
    if (!iHostStat(path, &st) || !st.is_file)
    {
        return;
    }

    char dir[512];
    iSS_parentDir(path, dir, sizeof(dir));
    const char* name = strrchr(path, '/');
    name = (name != NULL) ? name + 1 : path;

    char bdir[540];
    snprintf(bdir, sizeof(bdir), "%s/backup", dir);
    iSS_mkdirs(bdir);

    char older[600];
    char newer[600];
    snprintf(older, sizeof(older), "%s/%s.%d", bdir, name, ISS_BACKUPS);
    iHostRemoveFile(older);

    for (S32 i = ISS_BACKUPS - 1; i >= 1; i--)
    {
        snprintf(newer, sizeof(newer), "%s/%s.%d", bdir, name, (int)i);
        snprintf(older, sizeof(older), "%s/%s.%d", bdir, name, (int)i + 1);
        if (iHostPathExists(newer))
        {
            iHostRenameReplace(newer, older);
        }
    }

    snprintf(newer, sizeof(newer), "%s/%s.1", bdir, name);
    iSS_copyFile(path, newer);
}

bool iSaveStoreWriteFile(const char* path, const U8* data, size_t n, bool backup, char* err,
                         size_t errsize)
{
    char dir[512];
    iSS_parentDir(path, dir, sizeof(dir));
    if (!iSS_mkdirs(dir))
    {
        iSS_err(err, errsize, "cannot make the folder %s", dir);
        return false;
    }

    if (backup)
    {
        iSS_backup(path);
    }

    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE* f = fopen(tmp, "wb");
    if (f == NULL)
    {
        iSS_err(err, errsize, "cannot write %s", tmp);
        return false;
    }

    bool ok = (n == 0) || (fwrite(data, 1, n, f) == n);

    // The rename is only atomic if the bytes are on the disk first. fflush
    // alone hands them to the OS, and after a power cut or a system crash the
    // new name can come back with nothing behind it: an empty or zeroed save
    // where the old one was. iHostFlushFile waits for the disk. A file system
    // that cannot do that (some network shares) still gets the save.
    if (ok)
    {
        ok = (fflush(f) == 0);
    }
    if (ok)
    {
        iHostFlushFile(f);
    }
    if (fclose(f) != 0)
    {
        ok = false;
    }

    if (!ok || !iHostRenameReplace(tmp, path))
    {
        iHostRemoveFile(tmp);
        iSS_err(err, errsize, "cannot write %s", path);
        return false;
    }

    iHostFlushDir(dir);
    return true;
}

// --- the card entry kept beside an imported save ---------------------------------------

struct iSS_Head
{
    U32 payloadSize;
    U32 payloadCRC;
    U8 dentry[ISF_DENTRY_SIZE];
    std::vector<U8> head;
};

static void iSS_headPath(S32 target, S32 game, char* out, size_t outsize)
{
    char path[512];
    iSaveStoreSlotPath(target, game, path, sizeof(path));
    snprintf(out, outsize, "%s.gchead", path);
}

static bool iSS_readHead(S32 target, S32 game, iSS_Head* h)
{
    char path[540];
    iSS_headPath(target, game, path, sizeof(path));

    std::vector<U8> data;
    if (!iSaveStoreReadFile(path, data, NULL, 0) || data.size() != ISS_HEAD_FILE_SIZE ||
        memcmp(&data[0], ISS_HEAD_MAGIC, 8) != 0)
    {
        return false;
    }

    h->payloadSize = iSaveGetU32(&data[8]);
    h->payloadCRC = iSaveGetU32(&data[12]);
    memcpy(h->dentry, &data[16], ISF_DENTRY_SIZE);
    h->head.assign(data.begin() + 16 + ISF_DENTRY_SIZE, data.end());
    return true;
}

static bool iSS_writeHead(S32 target, S32 game, const U8* payload, const U8* dentry, const U8* head)
{
    std::vector<U8> data(ISS_HEAD_FILE_SIZE);
    memcpy(&data[0], ISS_HEAD_MAGIC, 8);
    iSavePutU32(&data[8], iSaveGetU32(payload + 8));
    iSavePutU32(&data[12], iSaveGetU32(payload + 12));
    memcpy(&data[16], dentry, ISF_DENTRY_SIZE);
    memcpy(&data[16 + ISF_DENTRY_SIZE], head, ISF_HEAD_SIZE);

    char path[540];
    iSS_headPath(target, game, path, sizeof(path));
    return iSaveStoreWriteFile(path, &data[0], data.size(), false, NULL, 0);
}

static void iSS_removeHead(S32 target, S32 game)
{
    char path[540];
    iSS_headPath(target, game, path, sizeof(path));
    iHostRemoveFile(path);
}

static void iSS_removeStill(S32 target, S32 game)
{
    char path[512];
    char still[540];
    iSaveStoreSlotPath(target, game, path, sizeof(path));
    snprintf(still, sizeof(still), "%s.tga", path);
    iHostRemoveFile(still);
}

// --- what is there -----------------------------------------------------------------------

bool iSaveStoreSlot(S32 target, S32 game, iSaveSlotInfo* out)
{
    memset(out, 0, sizeof(*out));
    out->target = target;
    out->game = game;
    out->progress = -1;
    out->levelIndex = -1;
    iSaveStoreSlotName(target, game, out->name, sizeof(out->name));
    iSaveStoreSlotPath(target, game, out->path, sizeof(out->path));

    if (target < 0 || target >= ISS_MAX_TARGETS || game < 0 || game >= ISS_GAMES)
    {
        return false;
    }

    snprintf(out->thumbPath, sizeof(out->thumbPath), "%s.tga", out->path);
    out->hasThumb = iHostPathExists(out->thumbPath);

    for (S32 i = 1; i <= ISS_BACKUPS; i++)
    {
        char b[512];
        iSS_backupPath(target, game, i, b, sizeof(b));
        if (iHostPathExists(b))
        {
            out->backups++;
        }
    }

    iHostFileInfo st;
    if (!iHostStat(out->path, &st) || !st.is_file)
    {
        return true;
    }

    out->exists = true;
    out->mtime = st.mtime;
    out->size = (S32)st.size;

    std::vector<U8> data;
    if (!iSaveStoreReadFile(out->path, data, out->problem, sizeof(out->problem)))
    {
        return true;
    }

    iSavePayloadInfo info;
    out->valid = iSavePayloadInspect(data.empty() ? NULL : &data[0], data.size(), &info,
                                     out->problem, sizeof(out->problem));
    snprintf(out->label, sizeof(out->label), "%s", info.label);
    out->progress = info.progress;
    out->levelIndex = info.levelIndex;
    snprintf(out->scene, sizeof(out->scene), "%s", info.scene);

    iSS_Head h;
    out->fromCard = iSS_readHead(target, game, &h);
    return true;
}

void iSaveStoreList(std::vector<iSaveSlotInfo>& out, bool allTargets)
{
    out.clear();
    S32 targets = allTargets ? ISS_MAX_TARGETS : 2;
    for (S32 t = 0; t < targets; t++)
    {
        for (S32 g = 0; g < ISS_GAMES; g++)
        {
            iSaveSlotInfo info;
            iSaveStoreSlot(t, g, &info);
            out.push_back(info);
        }
    }
}

// --- reading and writing --------------------------------------------------------------

bool iSaveStoreRead(S32 target, S32 game, std::vector<U8>& payload, char* err, size_t errsize)
{
    char path[512];
    iSaveStoreSlotPath(target, game, path, sizeof(path));

    if (!iHostPathExists(path))
    {
        char name[4];
        iSaveStoreSlotName(target, game, name, sizeof(name));
        iSS_err(err, errsize, "slot %s is empty", name);
        return false;
    }

    return iSaveStoreReadFile(path, payload, err, errsize);
}

bool iSaveStoreWrite(S32 target, S32 game, const U8* payload, size_t n, char* err, size_t errsize)
{
    if (target < 0 || target >= ISS_MAX_TARGETS || game < 0 || game >= ISS_GAMES)
    {
        iSS_err(err, errsize, "no such slot");
        return false;
    }

    iSavePayloadInfo info;
    if (!iSavePayloadInspect(payload, n, &info, err, errsize))
    {
        return false;
    }

    char path[512];
    iSaveStoreSlotPath(target, game, path, sizeof(path));
    if (!iSaveStoreWriteFile(path, payload, info.size, true, err, errsize))
    {
        return false;
    }

    iSS_removeStill(target, game);
    return true;
}

// --- import ------------------------------------------------------------------------------

void iSaveImportDefaults(iSaveImportOptions* o)
{
    o->target = -1;
    o->game = -1;
    o->pick = 0;
    o->force = false;
    o->dryRun = false;
    o->xboxXbe = NULL;
    o->xboxKeys = NULL;
    o->psvKeys = NULL;
}

namespace
{
    // One save found in the file being imported.
    struct Found
    {
        std::string source;
        S32 target; // -1: the options'
        S32 game; // -1: none known
        std::vector<U8> payload;
        bool haveCard;
        U8 dentry[ISF_DENTRY_SIZE];
        std::vector<U8> head;
        bool ok;
        std::string problem;
        std::string detail; // for a card's file: its name, region and date
        S64 mtime; // when it was saved, when the file says (an Xbox save's); else 0
    };
} // namespace

static bool iSS_lowerEnds(const std::string& s, const char* suffix)
{
    size_t n = strlen(suffix);
    if (s.size() < n)
    {
        return false;
    }
    for (size_t i = 0; i < n; i++)
    {
        char c = s[s.size() - n + i];
        if (c >= 'A' && c <= 'Z')
        {
            c = (char)(c - 'A' + 'a');
        }
        if (c != suffix[i])
        {
            return false;
        }
    }
    return true;
}

// A card file (entry + blocks): one found save if it is this game's, nothing
// if it is another game's (a card holds every game's saves).
static void iSS_fromGCI(const U8* p, size_t n, const std::string& source, S32 target,
                        bool skipOthers, std::vector<Found>& found)
{
    char err[200] = "";
    iSaveGCI gci;
    Found f;
    f.source = source;
    f.target = target;
    f.game = -1;
    f.haveCard = false;
    f.ok = false;
    f.mtime = 0;

    if (!iSaveGCIParse(p, n, &gci, err, sizeof(err)))
    {
        f.problem = err;
        found.push_back(f);
        return;
    }

    f.game = iSaveGCIGameIndex(&gci);
    if (f.game < 0 && skipOthers)
    {
        return;
    }

    if (!iSaveGCIPayload(&gci, f.payload, NULL, err, sizeof(err)))
    {
        f.problem = err;
        found.push_back(f);
        return;
    }

    f.haveCard = true;
    memcpy(f.dentry, gci.dentry, ISF_DENTRY_SIZE);
    f.head.assign(gci.data, gci.data + ISF_HEAD_SIZE);
    f.ok = true;

    // What the card says of it: the file, the region, when it was saved.
    char stamp[32];
    iSaveFormatStamp(gci.time, gci.gamecode[3] == 'P', stamp, sizeof(stamp));
    char detail[160];
    snprintf(detail, sizeof(detail), "%s (%s, saved %s)", gci.filename, gci.gamecode, stamp);
    f.detail = detail;

    found.push_back(f);
}

// The folder part of a zip entry's name ("" for none), and its last part.
static std::string iSS_entryDir(const std::string& name)
{
    size_t slash = name.find_last_of('/');
    return slash == std::string::npos ? "" : name.substr(0, slash);
}

static std::string iSS_entryBase(const std::string& name)
{
    size_t slash = name.find_last_of('/');
    return slash == std::string::npos ? name : name.substr(slash + 1);
}

// A save in the little-endian consoles' order (iSaveLE.h): an Xbox's
// GameData.xsv, with its 20-byte signature after the payload, or the payload
// alone. `saveName` is the game's name for it when something says
// ("SpongeBobMovie02": the Xbox's SaveMeta.xbx, or the file's own name),
// `folder` the Xbox save folder's name when there is one. `wrapper` is what
// the file it came in says of itself (a .psv's signature), or NULL.
static void iSS_fromLE(const U8* p, size_t n, const std::string& source, const char* saveName,
                       const char* folder, S64 mtime, std::vector<Found>& found,
                       const char* wrapper = NULL)
{
    char err[400] = "";
    Found f;
    f.source = source;
    f.target = -1;
    f.game = -1;
    f.haveCard = false;
    f.mtime = mtime;

    std::string signature;
    std::string notes;
    f.ok = iSaveStoreXboxDecode(p, n, f.payload, &signature, &notes, err, sizeof(err)) &&
           iSavePayloadInspect(&f.payload[0], f.payload.size(), NULL, err, sizeof(err));
    f.problem = err;

    if (saveName != NULL)
    {
        f.game = iSaveXboxGameOfName(saveName);
    }
    for (S32 g = 0; f.game < 0 && folder != NULL && g < ISS_GAMES; g++)
    {
        // The folder's name is worked out from the save's (iSaveXbox.h).
        char name[32];
        char hashed[16];
        iSaveXboxSaveName(g, name, sizeof(name));
        iSaveXboxFolderName(name, hashed);
        if (iHostStrCaseCmp(folder, hashed) == 0)
        {
            f.game = g;
        }
    }

    // Which console's: an Xbox save's folder or its file's name says so even
    // when the file is damaged; a bare file is told by the 20 bytes of
    // signature after the payload.
    std::string file = source;
    size_t slash = file.find_last_of("/\\");
    if (slash != std::string::npos)
    {
        file.erase(0, slash + 1);
    }
    bool xbox = folder != NULL || iHostStrCaseCmp(file.c_str(), ISX_SAVE_FILE) == 0 ||
                (n >= ISF_PAYLOAD_HEADER && iSavePayloadIsLE(p, n) &&
                 n == (size_t)iSavePayloadLESize(p) + ISX_SIGNATURE_SIZE);
    f.detail = xbox ? "an Xbox save" : "a PlayStation 2 save";
    if (saveName != NULL && saveName[0] != 0)
    {
        f.detail += std::string(" (") + saveName + ")";
    }
    if (xbox && !signature.empty())
    {
        f.detail += ", " + signature;
    }
    if (wrapper != NULL && wrapper[0] != '\0')
    {
        f.detail += std::string(", ") + wrapper;
    }
    // The conversion's note belongs to a save that reads. A refused one has
    // its reason, and a note worked out from damaged data would only read as
    // a second one.
    if (f.ok && !notes.empty())
    {
        f.detail += "; note: " + notes;
    }

    found.push_back(f);
}

static void iSS_find(const U8* p, size_t n, const char* name, std::vector<Found>& found, char* err,
                     size_t errsize, bool* understood)
{
    *understood = true;
    char why[200] = "";

    // The file's own name, without its folder.
    const char* base = strrchr(name, '/');
    const char* base2 = strrchr(name, '\\');
    base = (base2 > base) ? base2 : base;
    base = (base != NULL) ? base + 1 : name;

    // A PlayStation 2 save in one of the forms it travels in: a memory card
    // image, a .psu, a .max or a .psv (iSavePS2.h). Each save file in the
    // game's folder is the little-endian payload and nothing else.
    {
        std::vector<iSavePS2Folder> folders;
        std::string kind;
        S32 ps2 = iSaveStorePS2Folders(p, n, folders, &kind, why, sizeof(why));
        if (ps2 < 0)
        {
            iSS_err(err, errsize, "%s: %s", name, why);
            *understood = false;
            return;
        }
        if (ps2 > 0)
        {
            // A .psv carries a PlayStation 3's signature: which one is said,
            // and nothing hangs on it.
            std::string psvSignature;
            if (iSavePS2PsvIs(p, n))
            {
                psvSignature = iSaveStorePsvSignature(p, n);
            }
            for (size_t i = 0; i < folders.size(); i++)
            {
                for (size_t k = 0; k < folders[i].files.size(); k++)
                {
                    const iSavePS2File& file = folders[i].files[k];
                    if (iSavePS2GameOfName(file.name.c_str()) < 0)
                    {
                        continue;
                    }
                    iSS_fromLE(file.data.empty() ? NULL : &file.data[0], file.data.size(), kind,
                               file.name.c_str(), NULL, file.modified, found, psvSignature.c_str());
                }
            }
            return;
        }
    }

    // A zip from iSaveStoreExportZip (or any zip of .gci files).
    if (iSaveZipIs(p, n))
    {
        std::vector<iSaveZipEntry> entries;
        if (!iSaveZipParse(p, n, entries, why, sizeof(why)))
        {
            iSS_err(err, errsize, "%s", why);
            *understood = false;
            return;
        }

        for (size_t i = 0; i < entries.size(); i++)
        {
            const iSaveZipEntry& e = entries[i];

            // An Xbox save folder: GameData.xsv, with SaveMeta.xbx beside it
            // saying which save it is.
            if (iHostStrCaseCmp(iSS_entryBase(e.name).c_str(), ISX_SAVE_FILE) == 0)
            {
                std::string dir = iSS_entryDir(e.name);
                char saveName[64] = "";
                for (size_t k = 0; k < entries.size(); k++)
                {
                    if (iSS_entryDir(entries[k].name) == dir &&
                        iHostStrCaseCmp(iSS_entryBase(entries[k].name).c_str(), ISX_SAVE_META) ==
                            0 &&
                        !entries[k].data.empty())
                    {
                        iSaveXboxMetaValue(&entries[k].data[0], entries[k].data.size(), "Name",
                                           saveName, sizeof(saveName));
                    }
                }
                iSS_fromLE(e.data.empty() ? NULL : &e.data[0], e.data.size(), e.name,
                           saveName[0] != 0 ? saveName : NULL, iSS_entryBase(dir).c_str(), e.mtime,
                           found);
                continue;
            }

            // A PlayStation 2 save file, out of the game's folder on a
            // folder-type card or any copy of it.
            if (iSavePS2GameOfName(iSS_entryBase(e.name).c_str()) >= 0 && !e.data.empty() &&
                iSavePayloadIsLE(&e.data[0], e.data.size()))
            {
                iSS_fromLE(&e.data[0], e.data.size(), e.name, iSS_entryBase(e.name).c_str(), NULL,
                           e.mtime, found);
                continue;
            }

            if (!iSS_lowerEnds(e.name, ".gci") && !iSS_lowerEnds(e.name, ".gcs"))
            {
                continue;
            }

            // "B/78-GGVE-SpongeBobMovie01.gci" goes to folder B.
            S32 target = -1;
            if (e.name.size() > 2 && e.name[1] == '/' && e.name[0] >= 'A' &&
                e.name[0] < 'A' + ISS_MAX_TARGETS)
            {
                target = e.name[0] - 'A';
            }

            iSS_fromGCI(e.data.empty() ? NULL : &e.data[0], e.data.size(), e.name, target, false,
                        found);
        }
        return;
    }

    // A whole card: the console's own bytes, a multiple of its 8 KB blocks.
    // (A .gci is 64 bytes past a multiple.)
    if (n >= 5 * ISF_BLOCK_SIZE && n % ISF_BLOCK_SIZE == 0)
    {
        std::vector<std::vector<U8> > files;
        if (!iSaveCardFiles(p, n, files, why, sizeof(why)))
        {
            iSS_err(err, errsize, "%s", why);
            *understood = false;
            return;
        }

        for (size_t i = 0; i < files.size(); i++)
        {
            char source[64];
            snprintf(source, sizeof(source), "card file %d", (int)i + 1);
            iSS_fromGCI(&files[i][0], files[i].size(), source, -1, true, found);
        }
        return;
    }

    // The same save in the little-endian consoles' order: an Xbox's
    // GameData.xsv on its own, or a bare payload.
    if (iSavePayloadIsLE(p, n))
    {
        iSS_fromLE(p, n, base, iSaveXboxGameOfName(base) >= 0 ? base : NULL, NULL, 0, found);
        return;
    }

    // A save file as the PC keeps it, from another install's save folder.
    if (n >= 4 && iSaveGetU32(p) == ISF_TAG('G', 'D', 'A', 'T'))
    {
        Found f;
        f.source = base;
        f.target = -1;
        f.game = -1;
        f.haveCard = false;
        f.mtime = 0;
        f.ok = iSavePayloadInspect(p, n, NULL, why, sizeof(why));
        f.problem = why;
        if (f.ok)
        {
            f.payload.assign(p, p + iSaveGetU32(p + 8));
        }

        // Its name says its slot, when it still has the game's.
        size_t bl = strlen(ISF_FILE_BASE);
        if (strncmp(base, ISF_FILE_BASE, bl) == 0 && base[bl] == '0' && base[bl + 1] >= '1' &&
            base[bl + 1] <= '0' + ISS_GAMES && base[bl + 2] == '\0')
        {
            f.game = base[bl + 1] - '1';
        }

        found.push_back(f);
        return;
    }

    // A .gci or .gcs.
    iSaveGCI gci;
    if (iSaveGCIParse(p, n, &gci, why, sizeof(why)))
    {
        iSS_fromGCI(p, n, base, -1, false, found);
        return;
    }

    iSS_err(err, errsize,
            "%s is not a save this game knows: not a .gci/.gcs, a GameCube or PlayStation 2 "
            "memory card image, a .psu/.max/.psv, an Xbox save, a zip of saves or a save file",
            name);
    *understood = false;
}

bool iSaveStoreImportData(const U8* data, size_t n, const char* name, const iSaveImportOptions* o,
                          std::vector<iSaveImportItem>& items, char* err, size_t errsize)
{
    items.clear();

    iSaveImportOptions defaults;
    if (o == NULL)
    {
        iSaveImportDefaults(&defaults);
        o = &defaults;
    }

    std::vector<Found> found;
    bool understood;
    iSaveStoreXboxLookWith(o->xboxXbe, o->xboxKeys);
    iSaveStorePsvLookWith(o->psvKeys);
    iSS_find(data, n, name, found, err, errsize, &understood);
    iSaveStoreXboxLookWith(NULL, NULL);
    iSaveStorePsvLookWith(NULL);
    if (!understood)
    {
        return false;
    }

    if (found.empty())
    {
        iSS_err(err, errsize, "%s holds no SpongeBob Movie saves", name);
        return false;
    }

    if (o->pick > 0)
    {
        if ((size_t)o->pick > found.size())
        {
            iSS_err(err, errsize, "there are only %d saves in %s", (int)found.size(), name);
            return false;
        }
        Found keep = found[(size_t)o->pick - 1];
        found.clear();
        found.push_back(keep);
    }

    if (o->game >= 0 && found.size() > 1)
    {
        iSS_err(err, errsize,
                "%s holds more than one save; choose one with --pick (--check lists them)", name);
        return false;
    }

    bool all = true;
    for (size_t i = 0; i < found.size(); i++)
    {
        Found& f = found[i];
        iSaveImportItem item;
        item.source = f.detail.empty() ? f.source : f.source + ", " + f.detail;
        item.target = (o->target >= 0) ? o->target : (f.target >= 0) ? f.target : 0;
        item.game = (o->game >= 0) ? o->game : f.game;
        item.ok = false;
        item.progress = -1;

        char slot[4] = "?";
        if (item.game >= 0)
        {
            iSaveStoreSlotName(item.target, item.game, slot, sizeof(slot));
        }

        if (!f.ok)
        {
            item.message = "not imported: " + f.problem;
        }
        else if (item.game < 0)
        {
            item.message =
                "not imported: nothing says which slot it belongs in; name one with --slot";
        }
        else if (item.target < 0 || item.target >= ISS_MAX_TARGETS)
        {
            item.message = "not imported: no such folder";
        }
        else
        {
            iSavePayloadInfo info;
            iSavePayloadInspect(&f.payload[0], f.payload.size(), &info, NULL, 0);
            item.label = info.label;
            item.progress = (S32)info.progress;

            char what[160];
            snprintf(what, sizeof(what), "%s, %d%%", info.label, (int)info.progress);

            iSaveSlotInfo existing;
            iSaveStoreSlot(item.target, item.game, &existing);

            if (existing.exists && !o->force)
            {
                item.message = std::string("not imported: slot ") + slot +
                               " already holds a save (" + existing.label +
                               "); add --force to replace it (it is kept as a backup)";
            }
            else if (o->dryRun)
            {
                item.ok = true;
                item.message = std::string("would go to slot ") + slot + ": " + what;
            }
            else
            {
                char why[200] = "";
                if (!iSaveStoreWrite(item.target, item.game, &f.payload[0], f.payload.size(), why,
                                     sizeof(why)))
                {
                    item.message = std::string("not imported: ") + why;
                }
                else
                {
                    if (f.haveCard)
                    {
                        iSS_writeHead(item.target, item.game, &f.payload[0], f.dentry, &f.head[0]);

                        // The file's date: when the console saved it.
                        char path[512];
                        iSaveStoreSlotPath(item.target, item.game, path, sizeof(path));
                        iHostSetFileTime(path, iSaveUnixFromGCTime(iSaveGetU32(f.dentry + 0x28)));
                    }
                    else
                    {
                        iSS_removeHead(item.target, item.game);

                        // An Xbox save's date is its file's.
                        if (f.mtime > 0)
                        {
                            char path[512];
                            iSaveStoreSlotPath(item.target, item.game, path, sizeof(path));
                            iHostSetFileTime(path, f.mtime);
                        }
                    }

                    item.ok = true;
                    item.message = std::string("imported to slot ") + slot + ": " + what +
                                   (existing.exists ? " (the save it replaced is backup 1)" : "");
                }
            }
        }

        all = all && item.ok;
        items.push_back(item);
    }

    return all;
}

bool iSaveStoreImport(const char* file, const iSaveImportOptions* o,
                      std::vector<iSaveImportItem>& items, char* err, size_t errsize)
{
    items.clear();

    std::vector<U8> data;

    // An Xbox save is a folder: the saves under one (or beside a GameData.xsv
    // that was named itself) are gathered and imported as one bundle.
    iHostFileInfo st;
    if (iHostStat(file, &st) && st.is_dir)
    {
        if (!iSaveStoreReadFolder(file, data, err, errsize))
        {
            return false;
        }
        return iSaveStoreImportData(&data[0], data.size(), file, o, items, err, errsize);
    }

    {
        const char* base = strrchr(file, '/');
        const char* base2 = strrchr(file, 0x5C);
        base = (base2 > base) ? base2 : base;
        if (base != NULL && base != file && iHostStrCaseCmp(base + 1, ISX_SAVE_FILE) == 0)
        {
            // Its own folder only: what else is below it was not asked for.
            std::string dir(file, (size_t)(base - file));
            if (iSaveStoreReadFolder(dir.c_str(), data, NULL, 0, false))
            {
                return iSaveStoreImportData(&data[0], data.size(), file, o, items, err, errsize);
            }
        }
    }

    if (!iSaveStoreReadFile(file, data, err, errsize))
    {
        return false;
    }

    if (data.empty())
    {
        iSS_err(err, errsize, "%s is empty", file);
        return false;
    }

    return iSaveStoreImportData(&data[0], data.size(), file, o, items, err, errsize);
}

// --- export -----------------------------------------------------------------------------

void iSaveExportDefaults(iSaveExportOptions* o)
{
    o->gamecode[0] = '\0';
}

// The banner and icon from the disc's TPLs, if someone has put them in the save
// folder. The port reads the Xbox disc, which has neither.
static bool iSS_discArt(std::vector<U8>& banner, std::vector<U8>& icon)
{
    char path[600];
    const U8* data;
    size_t size;
    U16 w;
    U16 h;
    U32 fmt;

    std::vector<U8> tpl;
    snprintf(path, sizeof(path), "%s/SBGCBanner.tpl", iSaveStoreRoot());
    if (!iSaveStoreReadFile(path, tpl, NULL, 0) || tpl.empty() ||
        !iSaveTPLImage(&tpl[0], tpl.size(), &data, &size, &w, &h, &fmt) || size < ISF_BANNER_SIZE)
    {
        return false;
    }
    banner.assign(data, data + ISF_BANNER_SIZE);

    snprintf(path, sizeof(path), "%s/SBGCIcon.tpl", iSaveStoreRoot());
    if (!iSaveStoreReadFile(path, tpl, NULL, 0) || tpl.empty() ||
        !iSaveTPLImage(&tpl[0], tpl.size(), &data, &size, &w, &h, &fmt) || size < ISF_ICON_SIZE)
    {
        return false;
    }
    icon.assign(data, data + ISF_ICON_SIZE);
    return true;
}

// Any imported save's head, for its banner and icon: they are the same in every
// save the game writes.
static bool iSS_anyHead(iSS_Head* h)
{
    for (S32 t = 0; t < ISS_MAX_TARGETS; t++)
    {
        for (S32 g = 0; g < ISS_GAMES; g++)
        {
            if (iSS_readHead(t, g, h))
            {
                return true;
            }
        }
    }
    return false;
}

bool iSaveStoreExportGCIData(S32 target, S32 game, const iSaveExportOptions* o,
                             std::vector<U8>& out, char* err, size_t errsize)
{
    std::vector<U8> payload;
    if (!iSaveStoreRead(target, game, payload, err, errsize))
    {
        return false;
    }

    iSavePayloadInfo info;
    char why[200];
    if (!iSavePayloadInspect(payload.empty() ? NULL : &payload[0], payload.size(), &info, why,
                             sizeof(why)))
    {
        char name[4];
        iSaveStoreSlotName(target, game, name, sizeof(name));
        iSS_err(err, errsize, "slot %s does not hold a save the game would load: %s", name, why);
        return false;
    }

    iSaveGCIBuild b;
    iSaveGCIBuildDefaults(&b);
    b.game = game;

    iSS_Head h;
    bool haveHead = iSS_readHead(target, game, &h);
    bool sameSave = haveHead && h.payloadSize == info.size && h.payloadCRC == info.crcStored;

    if (o != NULL && o->gamecode[0] != '\0')
    {
        memcpy(b.gamecode, o->gamecode, 4);
    }
    else if (haveHead && strncmp((const char*)h.dentry, "GGV", 3) == 0)
    {
        memcpy(b.gamecode, h.dentry, 4);
    }
    b.gamecode[4] = '\0';

    if (sameSave && memcmp(h.dentry, b.gamecode, 4) == 0)
    {
        // The card's own entry and head: the .gci it came in.
        b.verbatimDentry = h.dentry;
        b.verbatimHead = &h.head[0];
    }
    else
    {
        char path[512];
        iSaveStoreSlotPath(target, game, path, sizeof(path));
        iHostFileInfo st;
        S64 when = iHostStat(path, &st) ? st.mtime : 0;

        b.time = iSaveGCTimeFromUnix(when);
        iSaveFormatStamp(b.time, b.gamecode[3] == 'P', b.comment, sizeof(b.comment));

        if (haveHead)
        {
            b.startBlock = iSaveGetU16(h.dentry + 0x36);
        }

        std::vector<U8> banner;
        std::vector<U8> icon;
        iSS_Head any;
        if (haveHead || iSS_anyHead(&any))
        {
            const iSS_Head& src = haveHead ? h : any;
            banner.assign(&src.head[ISF_BANNER_OFFSET],
                          &src.head[ISF_BANNER_OFFSET] + ISF_BANNER_SIZE);
            icon.assign(&src.head[ISF_ICON_OFFSET], &src.head[ISF_ICON_OFFSET] + ISF_ICON_SIZE);
        }
        else
        {
            iSS_discArt(banner, icon);
        }

        b.banner = banner.empty() ? NULL : &banner[0];
        b.icon = icon.empty() ? NULL : &icon[0];

        // Made here, while the pixels it points at are in scope.
        return iSaveGCIMake(&payload[0], info.size, &b, out);
    }

    return iSaveGCIMake(&payload[0], info.size, &b, out);
}

bool iSaveStoreExportGCI(S32 target, S32 game, const iSaveExportOptions* o, const char* path,
                         char* err, size_t errsize)
{
    std::vector<U8> gci;
    if (!iSaveStoreExportGCIData(target, game, o, gci, err, errsize))
    {
        return false;
    }
    return iSaveStoreWriteFile(path, &gci[0], gci.size(), false, err, errsize);
}

S32 iSaveStoreExportZipData(const iSaveExportOptions* o, std::vector<U8>& out, char* err,
                            size_t errsize)
{
    std::vector<iSaveZipEntry> entries;

    for (S32 t = 0; t < ISS_MAX_TARGETS; t++)
    {
        for (S32 g = 0; g < ISS_GAMES; g++)
        {
            iSaveSlotInfo info;
            iSaveStoreSlot(t, g, &info);
            if (!info.exists || !info.valid)
            {
                continue;
            }

            iSaveZipEntry e;
            if (!iSaveStoreExportGCIData(t, g, o, e.data, err, errsize))
            {
                return -1;
            }

            iSaveGCI gci;
            iSaveGCIParse(&e.data[0], e.data.size(), &gci, NULL, 0);

            char name[64];
            char dolphin[48];
            iSaveGCIDolphinName(gci.gamecode, g, dolphin, sizeof(dolphin));
            snprintf(name, sizeof(name), "%c/%s", 'A' + (int)t, dolphin);
            e.name = name;
            e.mtime = info.mtime;
            entries.push_back(e);
        }
    }

    if (entries.empty())
    {
        iSS_err(err, errsize, "there are no saves to export in %s", iSaveStoreRoot());
        return -1;
    }

    iSaveZipBuild(entries, out);
    return (S32)entries.size();
}

S32 iSaveStoreExportFolder(const iSaveExportOptions* o, const char* dir,
                           std::vector<iSaveExportItem>& items)
{
    items.clear();
    S32 count = 0;
    char err[512];

    for (S32 t = 0; t < ISS_MAX_TARGETS; t++)
    {
        for (S32 g = 0; g < ISS_GAMES; g++)
        {
            iSaveSlotInfo info;
            iSaveStoreSlot(t, g, &info);
            if (!info.exists)
            {
                continue;
            }

            iSaveExportItem item;
            snprintf(item.name, sizeof(item.name), "%s", info.name);
            item.ok = false;

            if (!info.valid)
            {
                item.message = std::string("skipped, damaged (") + info.problem + ")";
                items.push_back(item);
                continue;
            }

            std::vector<U8> gci;
            err[0] = '\0';
            if (!iSaveStoreExportGCIData(t, g, o, gci, err, sizeof(err)))
            {
                item.message = err;
                items.push_back(item);
                continue;
            }

            iSaveGCI parsed;
            iSaveGCIParse(&gci[0], gci.size(), &parsed, NULL, 0);
            char name[64];
            iSaveGCIDolphinName(parsed.gamecode, g, name, sizeof(name));

            // One card cannot hold two SpongeBobMovie01s: folder A's saves go
            // in `dir` itself, the other folders' in B/, C/... beside them.
            char path[1024];
            if (t == 0)
            {
                snprintf(path, sizeof(path), "%s/%s", dir, name);
            }
            else
            {
                snprintf(path, sizeof(path), "%s/%c/%s", dir, 'A' + (int)t, name);
            }

            err[0] = '\0';
            if (!iSaveStoreWriteFile(path, &gci[0], gci.size(), false, err, sizeof(err)))
            {
                item.message = err;
                items.push_back(item);
                continue;
            }

            item.ok = true;
            item.path = path;
            items.push_back(item);
            count++;
        }
    }

    return count;
}

S32 iSaveStoreExportZip(const iSaveExportOptions* o, const char* path, char* err, size_t errsize)
{
    std::vector<U8> zip;
    S32 count = iSaveStoreExportZipData(o, zip, err, errsize);
    if (count < 0)
    {
        return -1;
    }
    if (!iSaveStoreWriteFile(path, &zip[0], zip.size(), false, err, errsize))
    {
        return -1;
    }
    return count;
}

// --- backups ------------------------------------------------------------------------------

void iSaveStoreBackups(S32 target, S32 game, std::vector<iSaveBackupInfo>& out)
{
    out.clear();
    for (S32 i = 1; i <= ISS_BACKUPS; i++)
    {
        iSaveBackupInfo b;
        memset(&b, 0, sizeof(b));
        b.which = i;
        b.progress = -1;
        iSS_backupPath(target, game, i, b.path, sizeof(b.path));

        iHostFileInfo st;
        if (!iHostStat(b.path, &st) || !st.is_file)
        {
            continue;
        }
        b.mtime = st.mtime;

        std::vector<U8> data;
        if (iSaveStoreReadFile(b.path, data, NULL, 0) && !data.empty())
        {
            iSavePayloadInfo info;
            b.valid = iSavePayloadInspect(&data[0], data.size(), &info, NULL, 0);
            snprintf(b.label, sizeof(b.label), "%s", info.label);
            b.progress = info.progress;
        }
        out.push_back(b);
    }
}

bool iSaveStoreRestore(S32 target, S32 game, S32 which, char* err, size_t errsize)
{
    char path[512];
    iSS_backupPath(target, game, which, path, sizeof(path));

    std::vector<U8> data;
    if (which < 1 || which > ISS_BACKUPS || !iHostPathExists(path) ||
        !iSaveStoreReadFile(path, data, err, errsize) || data.empty())
    {
        iSS_err(err, errsize, "there is no backup %d of that slot", (int)which);
        return false;
    }

    iHostFileInfo st;
    S64 when = iHostStat(path, &st) ? st.mtime : 0;

    if (!iSaveStoreWrite(target, game, &data[0], data.size(), err, errsize))
    {
        return false;
    }

    char slot[512];
    iSaveStoreSlotPath(target, game, slot, sizeof(slot));
    if (when > 0)
    {
        iHostSetFileTime(slot, when);
    }
    return true;
}

bool iSaveStoreDelete(S32 target, S32 game, char* err, size_t errsize)
{
    char path[512];
    iSaveStoreSlotPath(target, game, path, sizeof(path));

    if (!iHostPathExists(path))
    {
        iSS_err(err, errsize, "that slot is already empty");
        return false;
    }

    iSS_backup(path);
    if (!iHostRemoveFile(path))
    {
        iSS_err(err, errsize, "cannot remove %s", path);
        return false;
    }

    iSS_removeStill(target, game);
    return true;
}

// --- after a crash -----------------------------------------------------------------------

// TRUE, with the reason, when the game would refuse the save in `d`. The game's
// own two checks and nothing stricter: the header as iSGIsGameCorrupt reads it
// (the magic, version 1, a size that is the file's) and the checksum, as
// xSG_ld_validate checks it. A save the game would load is never called
// damaged here, so nothing the game can use is ever replaced.
static bool iSS_unusable(const std::vector<U8>& d, char* why, size_t whysize)
{
    if (d.empty())
    {
        iSS_err(why, whysize, "it is empty");
        return true;
    }
    if (d.size() < ISF_PAYLOAD_HEADER || iSaveGetU32(&d[0]) != ISF_TAG('G', 'D', 'A', 'T'))
    {
        iSS_err(why, whysize, "it does not start with a save's header");
        return true;
    }
    if (iSaveGetU32(&d[4]) != 1)
    {
        iSS_err(why, whysize, "its header has version %lu", (unsigned long)iSaveGetU32(&d[4]));
        return true;
    }
    U32 size = iSaveGetU32(&d[8]);
    if (size != d.size())
    {
        iSS_err(why, whysize, "its header says %lu bytes and the file has %lu", (unsigned long)size,
                (unsigned long)d.size());
        return true;
    }
    if (iSaveCRC(0xFFFFFFFFu, &d[0] + ISF_PAYLOAD_HEADER, size - ISF_PAYLOAD_HEADER) !=
        iSaveGetU32(&d[12]))
    {
        iSS_err(why, whysize, "its checksum does not match");
        return true;
    }
    return false;
}

// A copy of a damaged save, as backup/<name>.damaged (then .damaged2 to
// .damaged9, the last reused): outside the three the backups rotate through,
// so keeping it pushes no good save out.
static bool iSS_keepDamaged(const char* path, char* kept, size_t keptsize)
{
    char dir[512];
    iSS_parentDir(path, dir, sizeof(dir));
    const char* name = strrchr(path, '/');
    name = (name != NULL) ? name + 1 : path;

    char to[600] = "";
    for (S32 i = 1; i <= 9; i++)
    {
        if (i == 1)
        {
            snprintf(to, sizeof(to), "%s/backup/%s.damaged", dir, name);
        }
        else
        {
            snprintf(to, sizeof(to), "%s/backup/%s.damaged%d", dir, name, (int)i);
        }
        if (!iHostPathExists(to))
        {
            break;
        }
    }

    snprintf(kept, keptsize, "backup/%s", strrchr(to, '/') + 1);
    return iSS_copyFile(path, to);
}

static void iSS_when(S64 t, char* out, size_t outsize)
{
    iHostCalendar c;
    iHostLocalTimeOf(t, &c);
    snprintf(out, outsize, "%04d-%02d-%02d %02d:%02d", c.year + 1900, c.mon + 1, c.mday, c.hour,
             c.min);
}

// The temporary beside a good save in slot `target`/`game` (whose bytes are
// `cur`). iSaveStoreWriteFile makes backup 1 of the save, writes <name>.tmp,
// flushes it and renames it over <name>; a process that dies anywhere in that
// leaves the old save in place. Cut off part way, the temporary is reported and
// left (the next save writes over it). Whole and no older than the save, it is
// the newer save -- the game stopped between the flush and the rename -- and
// goes in, with the save it replaces as backup 1 (made now if the write had not
// made it), so putting it back can never be what loses a save.
static bool iSS_promoteTmp(S32 target, S32 game, const std::vector<U8>& cur, char* report,
                           size_t reportsize)
{
    char slot[4];
    char path[512];
    char tmp[540];
    iSaveStoreSlotName(target, game, slot, sizeof(slot));
    iSaveStoreSlotPath(target, game, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    const char* name = strrchr(path, '/') + 1;

    std::vector<U8> next;
    char ignore[160];
    if (!iSaveStoreReadFile(tmp, next, NULL, 0) || iSS_unusable(next, ignore, sizeof(ignore)))
    {
        iSS_err(report, reportsize,
                "%s: %s.tmp is a save that was cut off before it was finished; the save before "
                "it is untouched",
                slot, name);
        return false;
    }
    if (next == cur)
    {
        return false;
    }

    iHostFileInfo saved;
    iHostFileInfo written;
    if (!iHostStat(path, &saved) || !iHostStat(tmp, &written) || written.mtime < saved.mtime)
    {
        iSS_err(report, reportsize,
                "%s: %s.tmp is a whole save but older than the one in the slot; left as it is",
                slot, name);
        return false;
    }

    char first[512];
    iSS_backupPath(target, game, 1, first, sizeof(first));
    std::vector<U8> b1;
    if (!iSaveStoreReadFile(first, b1, NULL, 0) || b1 != cur)
    {
        iSS_backup(path);
    }

    if (!iHostRenameReplace(tmp, path))
    {
        iSS_err(report, reportsize,
                "%s: %s.tmp holds a whole save that could not be moved into place", slot, name);
        return false;
    }

    iSS_err(report, reportsize,
            "%s: put back the save that was being written when the game stopped (%s.tmp, "
            "whole but not yet renamed); the one it replaces is backup 1",
            slot, name);
    return true;
}

bool iSaveStoreRecover(S32 target, S32 game, char* report, size_t reportsize)
{
    iSS_err(report, reportsize, "%s", "");
    if (target < 0 || target >= ISS_MAX_TARGETS || game < 0 || game >= ISS_GAMES)
    {
        return false;
    }

    char slot[4];
    char path[512];
    char tmp[540];
    iSaveStoreSlotName(target, game, slot, sizeof(slot));
    iSaveStoreSlotPath(target, game, path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    const char* name = strrchr(path, '/') + 1;

    iHostFileInfo st;
    const bool have = iHostStat(path, &st) && st.is_file;
    const bool haveTmp = iHostStat(tmp, &st) && st.is_file;

    // An empty slot stays empty, even with backups: emptying one keeps them
    // (iSaveStoreDelete), and bringing a deleted save back is the player's
    // call. Only a save that was being written for the first time when the
    // game stopped -- its temporary complete, the rename not done -- is put in.
    if (!have && !haveTmp)
    {
        return false;
    }

    char why[160] = "";
    bool empty = false;
    if (have)
    {
        std::vector<U8> cur;
        if (!iSaveStoreReadFile(path, cur, why, sizeof(why)))
        {
            // Held open by another program, or not ours to read: not damage,
            // and nothing here can tell what it holds.
            iSS_err(report, reportsize, "%s: %s; left as it is", slot, why);
            return false;
        }
        if (!iSS_unusable(cur, why, sizeof(why)))
        {
            // A save the game can use, which stays a save the game can use. A
            // temporary beside it is a write the game did not finish.
            return haveTmp && iSS_promoteTmp(target, game, cur, report, reportsize);
        }
        empty = cur.empty();
    }

    // Newest first: the temporary (a whole save whose rename was cut off),
    // then the backups. With no save in the slot, only the temporary.
    std::vector<U8> best;
    char from[64] = "";
    S64 fromTime = 0;
    char ignore[160];

    if (haveTmp && iSaveStoreReadFile(tmp, best, NULL, 0) &&
        !iSS_unusable(best, ignore, sizeof(ignore)))
    {
        snprintf(from, sizeof(from), "the save that was being written (%s.tmp)", name);
    }
    else
    {
        best.clear();
    }

    for (S32 i = 1; have && best.empty() && i <= ISS_BACKUPS; i++)
    {
        char b[512];
        iSS_backupPath(target, game, i, b, sizeof(b));
        std::vector<U8> data;
        if (iHostStat(b, &st) && st.is_file && iSaveStoreReadFile(b, data, NULL, 0) &&
            !iSS_unusable(data, ignore, sizeof(ignore)))
        {
            best.swap(data);
            fromTime = st.mtime;
            char when[32];
            iSS_when(st.mtime, when, sizeof(when));
            snprintf(from, sizeof(from), "backup %d, saved %s", (int)i, when);
        }
    }

    if (best.empty())
    {
        if (have)
        {
            iSS_err(report, reportsize,
                    "%s: %s is damaged (%s) and there is no good copy to put back; %s", slot, name,
                    why, empty ? "the game lists the slot as empty" : "the game cannot load it");
        }
        return false;
    }

    char kept[96] = "";
    if (have && !iSS_keepDamaged(path, kept, sizeof(kept)))
    {
        // Never replace what cannot be kept: it may still be worth something.
        iSS_err(report, reportsize,
                "%s: %s is damaged (%s), and a copy of it could not be kept, "
                "so it was left as it is",
                slot, name, why);
        return false;
    }

    char err[256] = "";
    if (!iSaveStoreWriteFile(path, &best[0], best.size(), false, err, sizeof(err)))
    {
        iSS_err(report, reportsize, "%s: could not put back %s: %s", slot, from, err);
        return false;
    }
    if (fromTime > 0)
    {
        // The date the list shows is the save's own.
        iHostSetFileTime(path, fromTime);
    }

    if (have)
    {
        iSS_err(report, reportsize,
                "%s: %s was damaged (%s); put back %s. The damaged file is "
                "kept as %s",
                slot, name, why, from, kept);
    }
    else
    {
        iSS_err(report, reportsize, "%s: put back %s", slot, from);
    }
    return true;
}

// --- one game at a time ------------------------------------------------------------------

// The folder as an absolute path, folded to one case (Windows paths are
// case-blind; a host whose paths are not would at worst refuse two copies that
// differ only in case), hashed into the key: FNV-1a.
static void iSS_claimKey(char* key, size_t keysize)
{
    char path[600];
    if (!iHostAbsolutePath(iSaveStoreRoot(), path, sizeof(path)))
    {
        snprintf(path, sizeof(path), "%s", iSaveStoreRoot());
    }
    iSS_slashes(path);

    U32 hash = 2166136261u;
    for (const char* p = path; *p != '\0'; p++)
    {
        char c = *p;
        if (c >= 'A' && c <= 'Z')
        {
            c = (char)(c - 'A' + 'a');
        }
        hash = (hash ^ (U8)c) * 16777619u;
    }

    snprintf(key, keysize, "tssm-saves-%08x", (unsigned)hash);
}

S32 iSaveStoreClaimFolder(U32* holder)
{
    char key[64];
    iSS_claimKey(key, sizeof(key));
    return iHostClaimInstance(key, holder);
}

bool iSaveStoreFolderHolder(U32* holder)
{
    char key[64];
    iSS_claimKey(key, sizeof(key));
    return iHostInstanceHeld(key, holder);
}
