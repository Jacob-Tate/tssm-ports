// PC-only: the save store's PlayStation 2 side -- the game's folder on a
// memory card, in the wrappers it travels in (iSavePS2.h), PCSX2's
// folder-type cards, and a PlayStation 3's .psv and virtual memory cards. See
// iSaveStore.h and docs/SAVES.md sections 8 and 9.
//
// Only iHost.h's file calls and the C library: nothing here is Windows's.
#include "iSaveStore.h"
#include "iSaveAES.h"
#include "iSaveFormat.h"
#include "iSaveLE.h"
#include "iSavePS2.h"
#include "iConfig.h"
#include "iFile.h"
#include "iHost.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>
#include <utility>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static void iSSP_err(char* err, size_t errsize, const char* fmt, ...)
{
    if (err != NULL && errsize > 0)
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, errsize, fmt, ap);
        va_end(ap);
    }
}

static std::string iSSP_slashes(const char* path)
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

static bool iSSP_isDir(const std::string& path)
{
    iHostFileInfo st;
    return iHostStat(path.c_str(), &st) && st.is_dir;
}

static bool iSSP_isFile(const std::string& path)
{
    iHostFileInfo st;
    return iHostStat(path.c_str(), &st) && st.is_file;
}

static bool iSSP_endsWith(const std::string& s, const char* suffix)
{
    size_t m = strlen(suffix);
    return s.size() >= m && iHostStrCaseCmp(s.c_str() + s.size() - m, suffix) == 0;
}

// `name` in `dir`, however its letters are cased on disk. "" when not there.
static std::string iSSP_child(const std::string& dir, const char* name)
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

// --- what a file holds -------------------------------------------------------------------

S32 iSaveStorePS2Folders(const U8* p, size_t n, std::vector<iSavePS2Folder>& out, std::string* kind,
                         char* err, size_t errsize)
{
    out.clear();
    iSavePS2Folder one;

    if (iSavePS2CardIs(p, n))
    {
        if (kind != NULL)
        {
            *kind = "a PlayStation 2 memory card";
        }

        std::vector<std::string> names;
        if (!iSavePS2CardList(p, n, names, err, errsize))
        {
            return -1;
        }
        for (size_t i = 0; i < names.size(); i++)
        {
            if (!iSavePS2IsGameFolder(names[i].c_str(), NULL))
            {
                continue;
            }
            if (!iSavePS2CardReadFolder(p, n, names[i].c_str(), &one, err, errsize))
            {
                return -1;
            }
            out.push_back(one);
        }
        return 1;
    }

    bool ok;
    if (iSavePS2PsuIs(p, n))
    {
        ok = iSavePS2PsuParse(p, n, &one, err, errsize);
        if (kind != NULL)
        {
            *kind = "a .psu file";
        }
    }
    else if (iSavePS2MaxIs(p, n))
    {
        ok = iSavePS2MaxParse(p, n, &one, err, errsize);
        if (kind != NULL)
        {
            *kind = "an Action Replay Max file";
        }
    }
    else if (iSavePS2PsvIs(p, n))
    {
        ok = iSavePS2PsvParse(p, n, &one, err, errsize);
        if (kind != NULL)
        {
            *kind = "a PlayStation 3 export (.psv)";
        }
    }
    else
    {
        return 0;
    }

    if (!ok)
    {
        return -1;
    }
    if (iSavePS2IsGameFolder(one.name.c_str(), NULL))
    {
        out.push_back(one);
    }
    return 1;
}

// --- the icon ----------------------------------------------------------------------------

// The game's icon out of what `path` is: the icon file itself, the PS2 disc's
// PLAT.HIP, a PS2 save in any wrapper, or a folder holding one of the first
// two.
static bool iSSP_iconAt(const std::string& path, std::vector<U8>& icon, std::string* from)
{
    if (iSSP_isDir(path))
    {
        static const char* const kIn[] = { IPS2_ICON_FILE, "PLAT.HIP" };
        for (size_t i = 0; i < sizeof(kIn) / sizeof(kIn[0]); i++)
        {
            std::string p = iSSP_child(path, kIn[i]);
            if (!p.empty() && iSSP_isFile(p) && iSSP_iconAt(p, icon, from))
            {
                return true;
            }
        }
        return false;
    }

    std::vector<U8> data;
    if (!iSaveStoreReadFile(path.c_str(), data, NULL, 0) || data.empty())
    {
        return false;
    }

    bool got = false;
    if (iSavePS2IconCheck(&data[0], data.size(), NULL, 0))
    {
        icon = data;
        got = true;
    }
    else if (iSavePS2IconFromPackage(&data[0], data.size(), icon))
    {
        got = true;
    }
    else
    {
        std::vector<iSavePS2Folder> folders;
        if (iSaveStorePS2Folders(&data[0], data.size(), folders, NULL, NULL, 0) > 0)
        {
            for (size_t i = 0; i < folders.size() && !got; i++)
            {
                const iSavePS2File* f = iSavePS2Find(&folders[i], IPS2_ICON_FILE);
                if (f != NULL && !f->data.empty() &&
                    iSavePS2IconCheck(&f->data[0], f->data.size(), NULL, 0))
                {
                    icon = f->data;
                    got = true;
                }
            }
        }
    }

    if (got && from != NULL)
    {
        *from = path;
    }
    return got;
}

bool iSaveStorePS2Icon(const char* given, std::vector<U8>& icon, std::string* from, char* err,
                       size_t errsize)
{
    icon.clear();
    if (from != NULL)
    {
        from->clear();
    }

    const char* named =
        (given != NULL && given[0] != '\0') ? given : iConfigGetString("game.ps2_icon", "");
    if (named != NULL && named[0] != '\0')
    {
        if (iSSP_iconAt(iSSP_slashes(named), icon, from))
        {
            return true;
        }
        iSSP_err(err, errsize,
                 "there is no PlayStation 2 save icon at %s (it is in PLAT.HIP on your own "
                 "PlayStation 2 disc, or SpongeIcon01.ico in a save made by the game); with no "
                 "place named, the port's own icon is written",
                 named);
        return false;
    }

    // The game's files in use, should they be the PlayStation 2's.
    std::string root = iSSP_slashes(iFileAssetRoot());
    if (!root.empty())
    {
        std::string plat = iSSP_child(root, "PLAT.HIP");
        if (!plat.empty() && iSSP_isFile(plat) && iSSP_iconAt(plat, icon, from))
        {
            return true;
        }
    }

    iSavePS2OwnIcon(icon);
    return true;
}

// --- PCSX2's folder cards ----------------------------------------------------------------
//
// A folder card is a host folder (named like a card file, "Mcd001.ps2"):
//
//   _pcsx2_superblock             the card's first erase block (8192 bytes);
//                                 without a formatted one PCSX2 shows the card
//                                 as empty
//   BASLUS-20904SBMovie_/         a save folder, its name made safe for a host
//       _pcsx2_meta_directory     the folder's own 512-byte card entry, kept
//                                 when the name had to change (it holds the
//                                 real one)
//       _pcsx2_index              YAML: for the folder ("$ROOT") and each file
//                                 its times, and the files' order
//       _pcsx2_meta/<file>        a file's card entry, when its name changed
//       <the files>
//
// The index's times are the card entries' clock fields read as if they were
// UTC, so nine hours ahead of the real moment.

namespace
{
    struct IndexEntry
    {
        std::string key;
        std::vector<std::pair<std::string, std::string> > fields;
    };

    struct Yaml
    {
        const std::string& s;
        size_t at;

        explicit Yaml(const std::string& text) : s(text), at(0)
        {
        }

        void blanks(bool newlines)
        {
            while (at < s.size() && (s[at] == ' ' || s[at] == '\t' ||
                                     (newlines && (s[at] == '\n' || s[at] == '\r'))))
            {
                at++;
            }
        }

        // A scalar, quoted or plain, up to one of `stops`.
        bool scalar(const char* stops, std::string* out)
        {
            out->clear();
            if (at >= s.size())
            {
                return false;
            }

            char q = s[at];
            if (q == '"' || q == '\'')
            {
                at++;
                while (at < s.size())
                {
                    char c = s[at++];
                    if (q == '"' && c == '\\' && at < s.size())
                    {
                        out->push_back(s[at++]);
                    }
                    else if (q == '\'' && c == '\'' && at < s.size() && s[at] == '\'')
                    {
                        out->push_back('\'');
                        at++;
                    }
                    else if (c == q)
                    {
                        return true;
                    }
                    else
                    {
                        out->push_back(c);
                    }
                }
                return false;
            }

            while (at < s.size() && s[at] != '\n' && s[at] != '\r')
            {
                // A colon ends a plain key only before a blank or the line's end.
                if (s[at] == ':' && strchr(stops, ':') != NULL &&
                    (at + 1 >= s.size() || strchr(" \t\r\n", s[at + 1]) != NULL))
                {
                    break;
                }
                if (s[at] != ':' && strchr(stops, s[at]) != NULL)
                {
                    break;
                }
                out->push_back(s[at++]);
            }
            while (!out->empty() &&
                   ((*out)[out->size() - 1] == ' ' || (*out)[out->size() - 1] == '\t'))
            {
                out->erase(out->size() - 1);
            }
            return !out->empty();
        }

        bool take(char c)
        {
            if (at < s.size() && s[at] == c)
            {
                at++;
                return true;
            }
            return false;
        }

        // "{k: v, k: v}"
        bool flowFields(std::vector<std::pair<std::string, std::string> >* fields)
        {
            if (!take('{'))
            {
                return false;
            }
            for (;;)
            {
                blanks(true);
                if (take('}'))
                {
                    return true;
                }
                std::string k;
                std::string v;
                if (!scalar(":,}", &k))
                {
                    return false;
                }
                blanks(true);
                if (!take(':'))
                {
                    return false;
                }
                blanks(true);
                scalar(",}", &v);
                fields->push_back(std::make_pair(k, v));
                blanks(true);
                take(',');
            }
        }

        // "{key: {...}, key: {...}}"
        bool flowDocument(std::vector<IndexEntry>* out)
        {
            if (!take('{'))
            {
                return false;
            }
            for (;;)
            {
                blanks(true);
                if (take('}'))
                {
                    return true;
                }
                IndexEntry e;
                if (!scalar(":,}", &e.key))
                {
                    return false;
                }
                blanks(true);
                if (!take(':'))
                {
                    return false;
                }
                blanks(true);
                if (at < s.size() && s[at] == '{')
                {
                    if (!flowFields(&e.fields))
                    {
                        return false;
                    }
                }
                else
                {
                    std::string skip;
                    scalar(",}", &skip);
                }
                out->push_back(e);
                blanks(true);
                take(',');
            }
        }

        // key:\n  k: v\n ...
        bool blockDocument(std::vector<IndexEntry>* out)
        {
            while (at < s.size())
            {
                size_t lineStart = at;
                blanks(false);
                size_t indent = at - lineStart;

                if (at >= s.size() || s[at] == '\n' || s[at] == '\r' || s[at] == '#' ||
                    s.compare(at, 3, "---") == 0)
                {
                    while (at < s.size() && s[at] != '\n')
                    {
                        at++;
                    }
                    take('\n');
                    continue;
                }

                std::string k;
                if (!scalar(":", &k))
                {
                    return false;
                }
                blanks(false);
                if (!take(':'))
                {
                    return false;
                }
                blanks(false);

                if (indent == 0)
                {
                    IndexEntry e;
                    e.key = k;
                    if (at < s.size() && s[at] == '{' && !flowFields(&e.fields))
                    {
                        return false;
                    }
                    out->push_back(e);
                }
                else
                {
                    if (out->empty())
                    {
                        return false;
                    }
                    std::string v;
                    scalar("#", &v);
                    out->back().fields.push_back(std::make_pair(k, v));
                }

                while (at < s.size() && s[at] != '\n')
                {
                    at++;
                }
                take('\n');
            }
            return true;
        }
    };
} // namespace

static bool iSSP_indexParse(const std::string& text, std::vector<IndexEntry>& out)
{
    out.clear();
    Yaml y(text);
    y.blanks(true);
    if (y.at >= text.size())
    {
        return true;
    }
    return text[y.at] == '{' ? y.flowDocument(&out) : y.blockDocument(&out);
}

static void iSSP_indexSet(IndexEntry* e, const char* key, S64 value)
{
    char text[32];
    snprintf(text, sizeof(text), "%lld", (long long)value);
    for (size_t i = 0; i < e->fields.size(); i++)
    {
        if (e->fields[i].first == key)
        {
            e->fields[i].second = text;
            return;
        }
    }
    e->fields.push_back(std::make_pair(std::string(key), std::string(text)));
}

static IndexEntry* iSSP_indexFind(std::vector<IndexEntry>& index, const std::string& key)
{
    for (size_t i = 0; i < index.size(); i++)
    {
        if (index[i].key == key)
        {
            return &index[i];
        }
    }
    return NULL;
}

static std::string iSSP_indexText(const std::vector<IndexEntry>& index)
{
    std::string out;
    for (size_t i = 0; i < index.size(); i++)
    {
        const std::string& k = index[i].key;
        bool plain = !k.empty();
        for (size_t c = 0; c < k.size(); c++)
        {
            char ch = k[c];
            bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                      (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '-' ||
                      (ch == '$' && c == 0);
            plain = plain && ok && !(c == 0 && (ch == '-' || ch == '.'));
        }
        if (plain)
        {
            out += k;
        }
        else
        {
            out += '"';
            for (size_t c = 0; c < k.size(); c++)
            {
                if (k[c] == '"' || k[c] == '\\')
                {
                    out += '\\';
                }
                out += k[c];
            }
            out += '"';
        }
        out += ":\n";
        for (size_t f = 0; f < index[i].fields.size(); f++)
        {
            out += "  " + index[i].fields[f].first + ": " + index[i].fields[f].second + "\n";
        }
    }
    return out;
}

// A card entry's time as the index holds it.
static S64 iSSP_indexTime(S64 unixSeconds)
{
    return unixSeconds + 9 * 3600;
}

static void iSSP_entry(U8* e, U32 mode, U32 length, S64 created, S64 modified, const char* name)
{
    memset(e, 0, IPS2_ENTRY_SIZE);
    e[0] = (U8)mode;
    e[1] = (U8)(mode >> 8);
    e[4] = (U8)length;
    e[5] = (U8)(length >> 8);
    e[6] = (U8)(length >> 16);
    e[7] = (U8)(length >> 24);
    iSavePS2TimePack(created, e + 8);
    iSavePS2TimePack(modified, e + 0x18);
    size_t n = strlen(name);
    memcpy(e + 0x40, name, n < 32 ? n : 32);
}

// Whether the folder card at `root` has a formatted superblock.
static bool iSSP_folderCardFormatted(const std::string& root)
{
    std::string path = iSSP_child(root, "_pcsx2_superblock");
    std::vector<U8> sb;
    return !path.empty() && iSaveStoreReadFile(path.c_str(), sb, NULL, 0) &&
           iSavePS2CardIs(sb.empty() ? NULL : &sb[0], sb.size());
}

// The names of the files in the game's folder of a folder card (their real
// names, where a card entry beside them says what it is).
static void iSSP_folderCardNames(const std::string& gameDir, std::vector<std::string>& names)
{
    names.clear();
    iHostDir* d = iHostDirOpen(gameDir.c_str());
    if (d == NULL)
    {
        return;
    }
    const char* e;
    while ((e = iHostDirNext(d)) != NULL && names.size() < 4096)
    {
        if (strncmp(e, "_pcsx2_", 7) == 0 || e[0] == '.' || !iSSP_isFile(gameDir + "/" + e))
        {
            continue;
        }

        std::string real = e;
        std::vector<U8> meta;
        if (iSaveStoreReadFile((gameDir + "/_pcsx2_meta/" + e).c_str(), meta, NULL, 0) &&
            meta.size() >= 0x60)
        {
            size_t n = 0;
            while (n < 32 && meta[0x40 + n] != 0)
            {
                n++;
            }
            real.assign((const char*)&meta[0x40], n);
        }
        names.push_back(real);
    }
    iHostDirClose(d);
}

static bool iSSP_folderCardWrite(const std::string& root, const iSavePS2Folder* changed,
                                 size_t filesAfter, S64 now, char* err, size_t errsize)
{
    // A card PCSX2 has not formatted yet gets a formatted first block, or it
    // would show as an empty card.
    if (!iSSP_folderCardFormatted(root))
    {
        std::vector<U8> card;
        iSavePS2CardFormat(card, false, now);
        if (!iSaveStoreWriteFile((root + "/_pcsx2_superblock").c_str(), &card[0], 8192, false, err,
                                 errsize))
        {
            return false;
        }
    }

    std::string gameDir = root + "/" + iSavePS2HostName(changed->name.c_str());
    U8 e[IPS2_ENTRY_SIZE];

    // The folder's own entry: its real name ends in a space, which the host
    // folder's cannot.
    std::string metaDir = gameDir + "/_pcsx2_meta_directory";
    S64 folderCreated = now;
    {
        std::vector<U8> old;
        if (iSaveStoreReadFile(metaDir.c_str(), old, NULL, 0) && old.size() >= 0x60)
        {
            S64 was = iSavePS2TimeUnpack(&old[8]);
            folderCreated = was > 0 ? was : now;
        }
    }
    iSSP_entry(e, IPS2_MODE_DIR, (U32)filesAfter + 2, folderCreated, now, changed->name.c_str());
    if (!iSaveStoreWriteFile(metaDir.c_str(), e, sizeof(e), false, err, errsize))
    {
        return false;
    }

    for (size_t i = 0; i < changed->files.size(); i++)
    {
        const iSavePS2File& f = changed->files[i];
        std::string host = iSavePS2HostName(f.name.c_str());
        std::string path = gameDir + "/" + host;
        if (!iSaveStoreWriteFile(path.c_str(), f.data.empty() ? NULL : &f.data[0], f.data.size(),
                                 false, err, errsize))
        {
            return false;
        }
        if (f.modified > 0)
        {
            iHostSetFileTime(path.c_str(), f.modified);
        }

        if (host != f.name)
        {
            iSSP_entry(e, IPS2_MODE_FILE, (U32)f.data.size(), f.created > 0 ? f.created : now,
                       f.modified > 0 ? f.modified : now, f.name.c_str());
            if (!iSaveStoreWriteFile((gameDir + "/_pcsx2_meta/" + host).c_str(), e, sizeof(e),
                                     false, err, errsize))
            {
                return false;
            }
        }
    }

    // The index: each file's order and times. One that cannot be read is left
    // alone (PCSX2 then takes the host's own file times).
    std::string indexPath = gameDir + "/_pcsx2_index";
    std::vector<IndexEntry> index;
    std::vector<U8> old;
    bool had = iSaveStoreReadFile(indexPath.c_str(), old, NULL, 0) && !old.empty();
    if (had && !iSSP_indexParse(std::string((const char*)&old[0], old.size()), index))
    {
        return true;
    }

    IndexEntry* rootEntry = iSSP_indexFind(index, "$ROOT");
    if (rootEntry == NULL && (rootEntry = iSSP_indexFind(index, "%ROOT")) != NULL)
    {
        rootEntry->key = "$ROOT";
    }
    if (rootEntry == NULL)
    {
        IndexEntry r;
        r.key = "$ROOT";
        index.insert(index.begin(), r);
        rootEntry = &index[0];
        iSSP_indexSet(rootEntry, "timeCreated", iSSP_indexTime(folderCreated));
    }
    iSSP_indexSet(rootEntry, "timeModified", iSSP_indexTime(now));

    for (size_t i = 0; i < changed->files.size(); i++)
    {
        const iSavePS2File& f = changed->files[i];
        std::string host = iSavePS2HostName(f.name.c_str());

        IndexEntry* entry = iSSP_indexFind(index, host);
        if (entry == NULL)
        {
            long maxOrder = 0;
            for (size_t k = 0; k < index.size(); k++)
            {
                for (size_t q = 0; q < index[k].fields.size(); q++)
                {
                    if (index[k].fields[q].first == "order")
                    {
                        long v = atol(index[k].fields[q].second.c_str());
                        maxOrder = v > maxOrder ? v : maxOrder;
                    }
                }
            }
            IndexEntry made;
            made.key = host;
            index.push_back(made);
            entry = &index.back();
            iSSP_indexSet(entry, "order", maxOrder + 1);
            iSSP_indexSet(entry, "timeCreated", iSSP_indexTime(f.created > 0 ? f.created : now));
        }
        iSSP_indexSet(entry, "timeModified", iSSP_indexTime(f.modified > 0 ? f.modified : now));
    }

    std::string text = iSSP_indexText(index);
    return iSaveStoreWriteFile(indexPath.c_str(), (const U8*)text.c_str(), text.size(), false, err,
                               errsize);
}

// --- export ------------------------------------------------------------------------------

void iSavePS2ExportDefaults(iSavePS2ExportOptions* o)
{
    o->kind = ISS_PS2_AUTO;
    o->pal = false;
    o->icon[0] = '\0';
    o->number = -1;
    o->replace = false;
    o->psvSignature = ISS_PSV_STANDIN;
    o->psvKeys[0] = '\0';
}

// --- the PS3 key file --------------------------------------------------------------------

bool iSaveStorePsvKeys(const char* given, char* path, size_t pathsize, char* err, size_t errsize)
{
    path[0] = '\0';

    const char* named =
        (given != NULL && given[0] != '\0') ? given : iConfigGetString("game.psv_keys", "");
    if (named == NULL || named[0] == '\0')
    {
        iSSP_err(err, errsize,
                 "a .psv with the PS3's own signature needs your PS3 key file: the 32-byte file "
                 "docs/SAVES.md describes (the port has no key of its own). Choose the file, or "
                 "use the stand-in signature");
        return false;
    }

    std::string p = iSSP_slashes(named);
    if (p.empty() || !iSSP_isFile(p))
    {
        iSSP_err(err, errsize, "there is no PS3 key file at %s", named);
        return false;
    }
    snprintf(path, pathsize, "%s", p.c_str());
    return true;
}

// The key values out of the player's file. They stay in `keys`, which the
// caller clears; only paths go into messages. A file of another size is not
// read at all: its size is the reason.
static bool iSSP_psvKeysLoad(const char* given, iSavePS2PsvKeys* keys, char* path, size_t pathsize,
                             char* err, size_t errsize)
{
    iSavePS2PsvKeysClear(keys);
    if (!iSaveStorePsvKeys(given, path, pathsize, err, errsize))
    {
        return false;
    }

    char why[300] = "";
    bool ok = false;
    iHostFileInfo st;
    if (!iHostStat(path, &st) || !st.is_file)
    {
        snprintf(why, sizeof(why), "the file cannot be read");
    }
    else if (st.size != IPS2_PSV_KEYS_FILE_SIZE)
    {
        U8 none = 0;
        iSavePS2PsvKeysParse(&none, (size_t)(st.size > 0x7FFFFFFF ? 0x7FFFFFFF : st.size), keys,
                             why, sizeof(why));
    }
    else
    {
        std::vector<U8> file;
        ok = iSaveStoreReadFile(path, file, why, sizeof(why)) &&
             iSavePS2PsvKeysParse(file.empty() ? NULL : &file[0], file.size(), keys, why,
                                  sizeof(why));
        if (!file.empty())
        {
            iSaveWipe(&file[0], file.size());
        }
    }

    if (!ok)
    {
        iSavePS2PsvKeysClear(keys);
        iSSP_err(err, errsize, "%s: %s", path, why);
    }
    return ok;
}

bool iSaveStorePsvKeysUsable(const char* given, char* path, size_t pathsize, char* err,
                             size_t errsize)
{
    iSavePS2PsvKeys keys;
    bool ok = iSSP_psvKeysLoad(given, &keys, path, pathsize, err, errsize);
    iSavePS2PsvKeysClear(&keys);
    return ok;
}

// The key file an import was told to check signatures with. A path only.
static std::string sLookPsvKeys;

void iSaveStorePsvLookWith(const char* keys)
{
    sLookPsvKeys = keys != NULL ? keys : "";
}

std::string iSaveStorePsvSignature(const U8* psv, size_t n)
{
    if (!iSavePS2PsvIs(psv, n))
    {
        return "";
    }
    if (iSavePS2PsvVerify(psv, n, NULL))
    {
        return "stand-in signature (a PS3 needs it re-signed)";
    }

    iSavePS2PsvKeys keys;
    char path[600];
    char why[768] = "";
    const char* given = sLookPsvKeys.empty() ? NULL : sLookPsvKeys.c_str();
    const char* named = given != NULL ? given : iConfigGetString("game.psv_keys", "");
    std::string said = "signature unknown (not the stand-in; telling more needs your PS3 key file)";
    if (iSSP_psvKeysLoad(given, &keys, path, sizeof(path), why, sizeof(why)))
    {
        said = iSavePS2PsvVerify(psv, n, &keys) ? "real signature (matches your PS3 key file)" :
                                                  "signature not recognised";
    }
    else if (named != NULL && named[0] != '\0')
    {
        // A key file is set but cannot serve: say why (a path and a size, never its bytes).
        said = std::string("signature unknown (not the stand-in; the PS3 key file cannot be "
                           "used: ") +
               why + ")";
    }
    iSavePS2PsvKeysClear(&keys);
    return said;
}

namespace
{
    struct Put
    {
        S32 number; // which of the card's three it becomes
        std::vector<U8> le;
        S64 mtime;
    };
} // namespace

// A copy of the card as it is, beside it, before anything is written to it.
static bool iSSP_backupCard(const std::string& path, const std::vector<U8>& data,
                            std::string* backup, char* err, size_t errsize)
{
    std::string newest;
    for (int i = 1; i < 100; i++)
    {
        char suffix[16];
        if (i == 1)
        {
            snprintf(suffix, sizeof(suffix), ".bak");
        }
        else
        {
            snprintf(suffix, sizeof(suffix), ".bak%d", i);
        }
        std::string to = path + suffix;
        if (iHostPathExists(to.c_str()))
        {
            newest = to;
            continue;
        }

        // The newest copy already is the card as it is now (the last export
        // was refused further on, or the card was put back from it): that
        // copy serves, and they do not pile up.
        iHostFileInfo st;
        if (!newest.empty() && iHostStat(newest.c_str(), &st) && st.is_file &&
            st.size == data.size())
        {
            std::vector<U8> old;
            if (iSaveStoreReadFile(newest.c_str(), old, NULL, 0) && old == data)
            {
                *backup = newest;
                return true;
            }
        }

        if (!iSaveStoreWriteFile(to.c_str(), &data[0], data.size(), false, NULL, 0) ||
            !iHostStat(to.c_str(), &st) || st.size != data.size())
        {
            iHostRemoveFile(to.c_str());
            iSSP_err(err, errsize,
                     "could not make a backup copy of the card (%s), so the card was not "
                     "touched",
                     to.c_str());
            return false;
        }
        *backup = to;
        return true;
    }
    iSSP_err(err, errsize, "there are already 99 backup copies beside %s; clear some away",
             path.c_str());
    return false;
}

// `skipped` (if not NULL) takes the numbers of the saves that were left out
// because one of that number is already there and replacing was not asked
// for, and the rest are written; without it one such save refuses the lot.
static bool iSSP_write(std::vector<Put>& puts, const iSavePS2ExportOptions* o, const char* destIn,
                       std::string* where, char* err, size_t errsize,
                       std::vector<S32>* skipped = NULL)
{
    const char* folderName = o->pal ? IPS2_FOLDER_PAL : IPS2_FOLDER_NTSC;
    std::string dest = iSSP_slashes(destIn);
    S64 now = (S64)time(NULL);

    if (dest.empty())
    {
        iSSP_err(err, errsize, "nowhere to export to");
        return false;
    }

    // A .psv with the PS3's own signature: the key file is read before
    // anything else, so that without it nothing is written. The key values
    // stay in `psvKeys` until the end, which clears them.
    struct KeysHold
    {
        iSavePS2PsvKeys keys;
        bool real;
        ~KeysHold()
        {
            iSavePS2PsvKeysClear(&keys);
        }
    } psvKeys;
    iSavePS2PsvKeysClear(&psvKeys.keys);
    psvKeys.real = false;

    // What the destination is.
    S32 kind = o->kind;
    bool isDir = iSSP_isDir(dest);
    bool isFile = iSSP_isFile(dest);
    std::vector<U8> destData;

    if (isFile && !iSaveStoreReadFile(dest.c_str(), destData, err, errsize))
    {
        return false;
    }

    if (kind == ISS_PS2_AUTO)
    {
        const U8* d = destData.empty() ? NULL : &destData[0];
        if (isDir)
        {
            kind =
                !iSSP_child(dest, "_pcsx2_superblock").empty() ? ISS_PS2_FOLDER_CARD : ISS_PS2_PSU;
        }
        else if (isFile)
        {
            kind = iSavePS2CardIs(d, destData.size()) ? ISS_PS2_CARD :
                   iSavePS2PsvIs(d, destData.size())  ? ISS_PS2_PSV :
                                                        ISS_PS2_PSU;
        }
        else
        {
            kind = (iSSP_endsWith(dest, ".ps2") || iSSP_endsWith(dest, ".vm2")) ? ISS_PS2_CARD :
                   iSSP_endsWith(dest, ".psv")                                  ? ISS_PS2_PSV :
                                                                                  ISS_PS2_PSU;
        }
    }

    if ((kind == ISS_PS2_PSU || kind == ISS_PS2_PSV) && isDir)
    {
        dest += "/";
        dest += kind == ISS_PS2_PSV ? iSavePS2PsvName(folderName) :
                o->pal              ? "BESLES-52895SBMovie.psu" :
                                      "BASLUS-20904SBMovie.psu";
        isDir = false;
        isFile = iSSP_isFile(dest);
        if (isFile && !iSaveStoreReadFile(dest.c_str(), destData, err, errsize))
        {
            return false;
        }
    }

    if (kind == ISS_PS2_PSV && o->psvSignature == ISS_PSV_REAL)
    {
        char keysAt[600];
        if (!iSSP_psvKeysLoad(o->psvKeys, &psvKeys.keys, keysAt, sizeof(keysAt), err, errsize))
        {
            return false;
        }
        psvKeys.real = true;
    }

    // What is there already.
    iSavePS2Folder have;
    iSavePS2FolderClear(&have);
    have.name = folderName;
    std::vector<std::string> names;
    bool fresh = true;

    // A .psv being added to keeps its seed: only the signature is made again.
    U8 seed[IPS2_PSV_SEED_SIZE];
    bool haveSeed = false;

    if (kind == ISS_PS2_PSU)
    {
        if (isFile)
        {
            if (!iSavePS2PsuIs(destData.empty() ? NULL : &destData[0], destData.size()))
            {
                iSSP_err(err, errsize,
                         "%s is there already and is not a .psu file; choose another name",
                         dest.c_str());
                return false;
            }
            if (!iSavePS2PsuParse(&destData[0], destData.size(), &have, err, errsize))
            {
                return false;
            }
            if (have.name != folderName)
            {
                iSSP_err(err, errsize, "%s holds another save folder (\"%s\"); choose another name",
                         dest.c_str(), have.name.c_str());
                return false;
            }
            fresh = false;
        }
    }
    else if (kind == ISS_PS2_PSV)
    {
        if (isFile)
        {
            if (!iSavePS2PsvIs(destData.empty() ? NULL : &destData[0], destData.size()))
            {
                iSSP_err(err, errsize,
                         "%s is there already and is not a PlayStation 2 save exported by a "
                         "PlayStation 3 (.psv); choose another name",
                         dest.c_str());
                return false;
            }
            if (!iSavePS2PsvParse(&destData[0], destData.size(), &have, err, errsize))
            {
                return false;
            }
            if (have.name != folderName)
            {
                iSSP_err(err, errsize, "%s holds another save folder (\"%s\"); choose another name",
                         dest.c_str(), have.name.c_str());
                return false;
            }
            memcpy(seed, &destData[IPS2_PSV_SEED], sizeof(seed));
            haveSeed = true;
            fresh = false;
        }
    }
    else if (kind == ISS_PS2_CARD)
    {
        if (isDir)
        {
            iSSP_err(err, errsize, "%s is a folder, not a memory card file", dest.c_str());
            return false;
        }
        if (isFile)
        {
            if (!iSavePS2CardIs(destData.empty() ? NULL : &destData[0], destData.size()))
            {
                iSSP_err(err, errsize,
                         "%s is not a formatted PlayStation 2 memory card (a card PCSX2 has "
                         "just made is blank until a game or the console's browser formats it)",
                         dest.c_str());
                return false;
            }
            std::vector<std::string> folders;
            if (!iSavePS2CardList(&destData[0], destData.size(), folders, err, errsize))
            {
                return false;
            }
            for (size_t i = 0; i < folders.size(); i++)
            {
                if (folders[i] == folderName)
                {
                    if (!iSavePS2CardReadFolder(&destData[0], destData.size(), folderName, &have,
                                                err, errsize))
                    {
                        return false;
                    }
                    fresh = false;
                }
            }
        }
    }
    else
    {
        if (isFile)
        {
            iSSP_err(err, errsize, "%s is a file, not a PCSX2 folder memory card", dest.c_str());
            return false;
        }

        // A folder that is neither a folder card nor a likely new one (empty,
        // or named like a card) is probably the folder the cards are kept in.
        if (isDir && iSSP_child(dest, "_pcsx2_superblock").empty() && !iSSP_endsWith(dest, ".ps2"))
        {
            bool empty = true;
            iHostDir* d = iHostDirOpen(dest.c_str());
            if (d != NULL)
            {
                const char* e;
                while ((e = iHostDirNext(d)) != NULL)
                {
                    if (strcmp(e, ".") != 0 && strcmp(e, "..") != 0)
                    {
                        empty = false;
                    }
                }
                iHostDirClose(d);
            }
            if (!empty)
            {
                iSSP_err(err, errsize,
                         "%s is not a PCSX2 folder memory card (it has no _pcsx2_superblock); "
                         "choose the card's own folder, the one named like a card file "
                         "(Mcd001.ps2)",
                         dest.c_str());
                return false;
            }
        }
        iSSP_folderCardNames(dest + "/" + iSavePS2HostName(folderName), names);
        for (size_t i = 0; i < names.size(); i++)
        {
            iSavePS2File f;
            f.name = names[i];
            f.mode = IPS2_MODE_FILE;
            f.attr = 0;
            f.created = 0;
            f.modified = 0;
            have.files.push_back(f);
            fresh = false;
        }
    }

    // A save already there stays unless replacing was asked for.
    for (size_t i = 0; i < puts.size();)
    {
        char name[32];
        iSavePS2SaveName(puts[i].number, name, sizeof(name));
        if (!o->replace && iSavePS2Find(&have, name) != NULL)
        {
            if (skipped == NULL)
            {
                iSSP_err(err, errsize,
                         "%s is already there (%s); export as another save number, or allow "
                         "replacing it",
                         name, dest.c_str());
                return false;
            }
            skipped->push_back(puts[i].number);
            puts.erase(puts.begin() + i);
            continue;
        }
        i++;
    }
    if (puts.empty())
    {
        iSSP_err(err, errsize, "every save is already there (%s); allow replacing them",
                 dest.c_str());
        return false;
    }

    // What has to be written: the three files the game keeps beside its saves,
    // where they are missing, then the saves.
    iSavePS2Folder changed;
    iSavePS2FolderClear(&changed);
    changed.name = folderName;
    changed.created = now;
    changed.modified = now;

    S64 stamp = now;
    for (size_t i = 0; i < puts.size(); i++)
    {
        stamp = (puts[i].mtime > 0 && puts[i].mtime < stamp) ? puts[i].mtime : stamp;
    }

    iSavePS2File f;
    f.mode = IPS2_MODE_FILE;
    f.attr = 0;
    f.created = stamp;
    f.modified = stamp;

    if (iSavePS2Find(&have, folderName) == NULL)
    {
        f.name = folderName;
        iSavePS2Filler(f.data);
        changed.files.push_back(f);
    }
    if (iSavePS2Find(&have, IPS2_ICON_SYS) == NULL)
    {
        f.name = IPS2_ICON_SYS;
        iSavePS2IconSys(f.data);
        changed.files.push_back(f);
    }
    if (iSavePS2Find(&have, IPS2_ICON_FILE) == NULL)
    {
        f.name = IPS2_ICON_FILE;
        if (!iSaveStorePS2Icon(o->icon, f.data, NULL, err, errsize))
        {
            return false;
        }
        changed.files.push_back(f);
    }
    for (size_t i = 0; i < puts.size(); i++)
    {
        char name[32];
        iSavePS2SaveName(puts[i].number, name, sizeof(name));
        f.name = name;
        f.data = puts[i].le;
        f.created = puts[i].mtime > 0 ? puts[i].mtime : now;
        f.modified = f.created;
        changed.files.push_back(f);
    }

    size_t filesAfter = have.files.size();
    for (size_t i = 0; i < changed.files.size(); i++)
    {
        if (iSavePS2Find(&have, changed.files[i].name.c_str()) == NULL)
        {
            filesAfter++;
        }
    }

    std::string said = dest;

    if (kind == ISS_PS2_PSU || kind == ISS_PS2_PSV)
    {
        if (fresh)
        {
            have.created = stamp;
        }
        have.modified = now;
        for (size_t i = 0; i < changed.files.size(); i++)
        {
            iSavePS2File* old = iSavePS2Find(&have, changed.files[i].name.c_str());
            if (old != NULL)
            {
                S64 created = old->created;
                *old = changed.files[i];
                old->created = created > 0 ? created : old->created;
            }
            else
            {
                have.files.push_back(changed.files[i]);
            }
        }

        std::vector<U8> out;
        if (kind == ISS_PS2_PSU)
        {
            iSavePS2PsuBuild(&have, out);
        }
        else
        {
            // A card's folder is changed when a file in it is: its time is
            // the newest of theirs, as on the card the .psv is taken from.
            have.modified = 0;
            for (size_t i = 0; i < have.files.size(); i++)
            {
                if (have.files[i].modified > have.modified)
                {
                    have.modified = have.files[i].modified;
                }
            }
            if (have.modified <= 0)
            {
                have.modified = now;
            }
            iSavePS2PsvBuild(&have, haveSeed ? seed : NULL, psvKeys.real ? &psvKeys.keys : NULL,
                             out);
        }
        if (!iSaveStoreWriteFile(dest.c_str(), &out[0], out.size(), isFile, err, errsize))
        {
            return false;
        }
    }
    else if (kind == ISS_PS2_CARD)
    {
        std::vector<U8> card;
        if (isFile)
        {
            card = destData;
        }
        else
        {
            // A new card, formatted, as PCSX2 keeps one (with the spare bytes),
            // or as a PS3 keeps its virtual cards (.VM2: without them).
            iSavePS2CardFormat(card, !iSSP_endsWith(dest, ".vm2"), now);
        }

        if (!iSavePS2CardWriteFolder(card, &changed, now, err, errsize))
        {
            return false;
        }

        // Read back what was just made before it goes anywhere near the
        // player's file.
        iSavePS2Folder back;
        if (!iSavePS2CardReadFolder(&card[0], card.size(), folderName, &back, err, errsize))
        {
            return false;
        }
        for (size_t i = 0; i < changed.files.size(); i++)
        {
            const iSavePS2File* b = iSavePS2Find(&back, changed.files[i].name.c_str());
            if (b == NULL || b->data != changed.files[i].data)
            {
                iSSP_err(err, errsize, "the card did not read back as written; nothing changed");
                return false;
            }
        }

        if (isFile)
        {
            std::string backup;
            if (!iSSP_backupCard(dest, destData, &backup, err, errsize))
            {
                return false;
            }
            said += " (the card as it was: " + backup + ")";
        }
        if (!iSaveStoreWriteFile(dest.c_str(), &card[0], card.size(), false, err, errsize))
        {
            return false;
        }
    }
    else
    {
        if (!iSSP_folderCardWrite(dest, &changed, filesAfter, now, err, errsize))
        {
            return false;
        }
        said = dest + "/" + iSavePS2HostName(folderName);
    }

    if (where != NULL)
    {
        *where = said;
    }
    return true;
}

// A slot's save as the PlayStation 2 stores it.
static bool iSSP_put(S32 target, S32 game, S32 number, Put* put, char* err, size_t errsize)
{
    if (number < 0 || number >= ISS_GAMES)
    {
        iSSP_err(err, errsize, "a PlayStation 2 card holds saves 1 to %d", ISS_GAMES);
        return false;
    }

    iSaveSlotInfo info;
    iSaveStoreSlot(target, game, &info);

    std::vector<U8> payload;
    if (!iSaveStoreRead(target, game, payload, err, errsize))
    {
        return false;
    }
    if (!iSaveStoreToLE(&payload[0], payload.size(), put->le, NULL, err, errsize))
    {
        return false;
    }
    put->number = number;
    put->mtime = info.mtime;
    return true;
}

bool iSaveStoreExportPS2(S32 target, S32 game, const iSavePS2ExportOptions* o, const char* dest,
                         std::string* where, char* err, size_t errsize)
{
    iSavePS2ExportOptions defaults;
    if (o == NULL)
    {
        iSavePS2ExportDefaults(&defaults);
        o = &defaults;
    }

    std::vector<Put> puts(1);
    if (!iSSP_put(target, game, o->number >= 0 ? o->number : game, &puts[0], err, errsize))
    {
        return false;
    }
    return iSSP_write(puts, o, dest != NULL ? dest : "", where, err, errsize);
}

S32 iSaveStoreExportPS2Folder(S32 target, const iSavePS2ExportOptions* o, const char* dest,
                              std::vector<iSaveExportItem>& items, std::string* where, char* err,
                              size_t errsize)
{
    items.clear();

    iSavePS2ExportOptions opt;
    if (o != NULL)
    {
        opt = *o;
    }
    else
    {
        iSavePS2ExportDefaults(&opt);
    }
    opt.number = -1;

    // All three go in at once: one file written, one backup of a card.
    std::vector<Put> puts;
    std::vector<size_t> itemOf;
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

        char why[512] = "";
        Put put;
        if (!info.valid)
        {
            item.message = std::string("damaged: ") + info.problem;
        }
        else if (iSSP_put(target, g, g, &put, why, sizeof(why)))
        {
            puts.push_back(put);
            itemOf.push_back(items.size());
        }
        else
        {
            item.message = why;
        }
        items.push_back(item);
    }

    if (puts.empty())
    {
        iSSP_err(err, errsize, "there is no save to export in that folder");
        return 0;
    }

    // Which item each save is, by its number: the write takes the ones
    // already at the destination out of `puts`.
    std::vector<S32> numbers;
    for (size_t i = 0; i < puts.size(); i++)
    {
        numbers.push_back(puts[i].number);
    }

    std::string said;
    std::string shown = iSSP_slashes(dest);
    char why[512] = "";
    std::vector<S32> skipped;
    bool ok = iSSP_write(puts, &opt, dest != NULL ? dest : "", &said, why, sizeof(why), &skipped);
    for (size_t i = 0; i < itemOf.size(); i++)
    {
        iSaveExportItem& item = items[itemOf[i]];
        bool wasSkipped = false;
        for (size_t k = 0; k < skipped.size(); k++)
        {
            wasSkipped = wasSkipped || skipped[k] == numbers[i];
        }

        if (wasSkipped)
        {
            // Each save answers for itself: the others are written.
            char name[32];
            iSavePS2SaveName(numbers[i], name, sizeof(name));
            item.ok = false;
            item.message = std::string(name) + " is already there (" + shown +
                           "); allow replacing it to write over it";
        }
        else if (ok)
        {
            item.ok = true;
            item.path = said;
        }
        else
        {
            item.ok = false;
            item.message = why;
        }
    }
    if (!ok)
    {
        iSSP_err(err, errsize, "%s", why);
        return 0;
    }
    if (where != NULL)
    {
        *where = said;
    }
    return (S32)puts.size();
}
