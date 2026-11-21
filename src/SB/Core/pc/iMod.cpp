// PC-only: the mod manager. See iMod.h for the folder layout and the hooks, and
// docs/MODDING.md for the modder's side.
#include "iMod.h"

#include "iGCPackage.h"
#include "iGCSwap.h"

#include "iConfig.h"
#include "iHost.h"
#include "iModAudio.h"
#include "iModIni.h"
#include "iModPackage.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace
{
    // --- strings and paths ----------------------------------------------------------

    std::string Lower(const std::string& s)
    {
        std::string out(s);
        for (size_t i = 0; i < out.size(); i++)
        {
            out[i] = (char)tolower((unsigned char)out[i]);
        }
        return out;
    }

    std::string Slashes(const char* path)
    {
        std::string s(path != NULL ? path : "");
        for (size_t i = 0; i < s.size(); i++)
        {
            if (s[i] == '\\')
            {
                s[i] = '/';
            }
        }
        return s;
    }

    std::string StripTrailingSlash(std::string s)
    {
        while (s.size() > 1 && s[s.size() - 1] == '/')
        {
            s.erase(s.size() - 1);
        }
        return s;
    }

    std::string Leaf(const std::string& path)
    {
        size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    std::string Stem(const std::string& name)
    {
        size_t dot = name.rfind('.');
        return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
    }

    std::string Ext(const std::string& name)
    {
        size_t dot = name.rfind('.');
        return dot == std::string::npos ? "" : Lower(name.substr(dot + 1));
    }

    // "1D327E54" or "0x1D327E54": exactly eight hex digits.
    bool ParseId(const std::string& stem, U32* id)
    {
        std::string s = stem;
        if (s.size() == 10 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        {
            s = s.substr(2);
        }
        if (s.size() != 8)
        {
            return false;
        }
        U32 v = 0;
        for (size_t i = 0; i < 8; i++)
        {
            char c = s[i];
            U32 d;
            if (c >= '0' && c <= '9')
            {
                d = (U32)(c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                d = (U32)(c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F')
            {
                d = (U32)(c - 'A' + 10);
            }
            else
            {
                return false;
            }
            v = (v << 4) | d;
        }
        *id = v;
        return true;
    }

    std::string IdKey(U32 id)
    {
        char s[16];
        snprintf(s, sizeof(s), "id:%08X", id);
        return s;
    }

    bool ReadWhole(const std::string& path, std::vector<U8>* out)
    {
        FILE* fp = fopen(path.c_str(), "rb");
        if (fp == NULL)
        {
            return false;
        }
        fseek(fp, 0, SEEK_END);
        long len = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (len < 0)
        {
            fclose(fp);
            return false;
        }
        out->resize((size_t)len);
        bool ok = len == 0 || fread(&(*out)[0], 1, (size_t)len, fp) == (size_t)len;
        fclose(fp);
        return ok;
    }

    bool IsDir(const std::string& path)
    {
        iHostFileInfo info;
        return iHostStat(path.c_str(), &info) && info.is_dir;
    }

    bool IsFile(const std::string& path)
    {
        iHostFileInfo info;
        return iHostStat(path.c_str(), &info) && info.is_file;
    }

    // A directory's entries, sorted case-insensitively so every host lists a
    // mod in the same order.
    std::vector<std::string> List(const std::string& dir)
    {
        std::vector<std::string> out;
        iHostDir* d = iHostDirOpen(dir.c_str());
        if (d == NULL)
        {
            return out;
        }
        for (const char* e = iHostDirNext(d); e != NULL; e = iHostDirNext(d))
        {
            if (e[0] != '.')
            {
                out.push_back(e);
            }
        }
        iHostDirClose(d);
        std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
            return Lower(a) < Lower(b);
        });
        return out;
    }

    void Copy(char* dst, size_t size, const std::string& src)
    {
        snprintf(dst, size, "%s", src.c_str());
    }

    // --- state --------------------------------------------------------------------------

    const U32 kTypeSND = 0x534E4420; // 'SND '
    const U32 kTypeSNDS = 0x534E4453; // 'SNDS'
    const U32 kTypeTEXT = 0x54455854; // 'TEXT'

    // One thing a mod replaces.
    struct Entry
    {
        S32 kind;
        std::string key; // the index key (see AddEntries)
        std::string path; // full path
        std::string rel; // under the mod folder, for the log
        std::string scope; // assets/sounds: the package stem it is limited to, or ""
    };

    struct Mod
    {
        iModInfo info;
        std::vector<Entry> entries;
    };

    // The winner for a key, and the mod it came from (index into sMods).
    struct Override
    {
        const Entry* entry;
        S32 mod;
    };

    typedef std::map<std::string, Override> Index;

    bool sInitialised;
    bool sGlobalOn = true;
    std::string sFolder; // in use, "" for none
    std::string sDefaultFolder;
    std::vector<Mod> sMods;

    Index sIndex[IMOD_KIND_COUNT];
    std::map<std::string, std::vector<Override> > sInis; // every overlay, in load order
    std::vector<std::string> sScopes; // package stems with asset/sound overrides ("" = any)
    std::vector<iModConflict> sConflicts;

    // Built packages by lower-case path; NULL for one nothing replaces.
    std::map<std::string, iModPkg*> sPackages;

    // GameCube packages' views (iModOpenPackage), newest first, each with a
    // reference of its own. A view holds every converted asset of its package
    // (a level's HOP up to about 10 MB; boot, menus, player and level about
    // 7-25 MB together), and one level load opens some 15 packages, which the
    // sound layer opens again for each stream it plays. So the views of a
    // level load and the shared ones are kept, bounded by count and bytes, not
    // converted again on the main thread while the level plays. The key
    // carries the file's size and time, so a changed file is converted again.
    struct GCView
    {
        std::string key;
        iModPkg* pkg; // NULL when the package needed no conversion
        bool failed; // it cannot be converted; said so once
    };
    std::vector<GCView> sGCViews;
    const size_t kMaxGCViews = 32;
    const U64 kMaxGCViewBytes = 64ull << 20;
    bool sOpenedPackage;
    bool sChangedAfterOpen;

    std::vector<std::string> sLog;
    FILE* sLogFile;
    const size_t kMaxLog = 8192;

    std::map<std::string, bool> sReported; // texture and movie names already logged

    void AddConflict(S32 kind, const std::string& key, S32 winner, S32 loser)
    {
        for (size_t i = 0; i < sConflicts.size(); i++)
        {
            const iModConflict& c = sConflicts[i];
            if (c.kind == kind && c.winner == winner && c.loser == loser && key == c.key)
            {
                return;
            }
        }
        iModConflict c;
        c.kind = kind;
        Copy(c.key, sizeof(c.key), key);
        c.winner = winner;
        c.loser = loser;
        sConflicts.push_back(c);
        sMods[winner].info.conflicts++;
        sMods[loser].info.conflicts++;
        iModLogf("conflict: %s '%s' -- '%s' wins over '%s' (later in the load order)",
                 iModKindName(kind), key.c_str(), sMods[winner].info.name,
                 sMods[loser].info.name);
    }

    void Problem(Mod& m, const std::string& what)
    {
        iModLogf("%s: %s", m.info.folder, what.c_str());
        if (m.info.problem[0] == '\0')
        {
            Copy(m.info.problem, sizeof(m.info.problem), what);
        }
    }

    // --- reading a mod ---------------------------------------------------------------

    const char* const kContentDirs[] = { "files", "assets", "textures", "sounds", "fmv", "ini" };

    bool IsTextureExt(const std::string& ext)
    {
        return ext == "png" || ext == "dds" || ext == "tga" || ext == "bmp";
    }

    void AddEntry(Mod& m, S32 kind, const std::string& key, const std::string& path,
                  const std::string& rel, const std::string& scope)
    {
        for (size_t i = 0; i < m.entries.size(); i++)
        {
            if (m.entries[i].kind == kind && m.entries[i].key == key && kind != IMOD_KIND_INI)
            {
                Problem(m, rel + " replaces the same " + iModKindName(kind) + " as " +
                               m.entries[i].rel + "; ignored");
                return;
            }
        }
        Entry e;
        e.kind = kind;
        e.key = key;
        e.path = path;
        e.rel = rel;
        e.scope = scope;
        m.entries.push_back(e);
        m.info.counts[kind]++;
    }

    // files/: every file, any depth, keyed by its lower-case path.
    void ScanFiles(Mod& m, const std::string& dir, const std::string& rel, S32 depth)
    {
        if (depth > 8)
        {
            return;
        }
        std::vector<std::string> names = List(dir);
        for (size_t i = 0; i < names.size(); i++)
        {
            std::string path = dir + "/" + names[i];
            std::string r = rel.empty() ? names[i] : rel + "/" + names[i];
            if (IsDir(path))
            {
                ScanFiles(m, path, r, depth + 1);
            }
            else
            {
                AddEntry(m, IMOD_KIND_FILE, Lower(r), path, "files/" + r, "");
            }
        }
    }

    // assets/ and sounds/: files at the top, or one folder down for a package.
    void ScanAssets(Mod& m, const std::string& dir, const char* top, S32 kind,
                    const std::string& scope)
    {
        std::vector<std::string> names = List(dir);
        for (size_t i = 0; i < names.size(); i++)
        {
            std::string path = dir + "/" + names[i];
            std::string rel = std::string(top) + "/" + (scope.empty() ? "" : scope + "/") + names[i];
            if (IsDir(path))
            {
                if (scope.empty())
                {
                    ScanAssets(m, path, top, kind, names[i]);
                }
                else
                {
                    Problem(m, rel + ": folders go one level deep (" + top +
                                   "/<package>/<file>); ignored");
                }
                continue;
            }
            std::string stem = Stem(names[i]);
            U32 id;
            std::string key = ParseId(stem, &id) ? IdKey(id) : "name:" + Lower(stem);
            AddEntry(m, kind, Lower(scope) + "|" + key, path, rel, Lower(scope));
        }
    }

    // textures/ and fmv/: any depth, keyed by the file's stem.
    void ScanByStem(Mod& m, const std::string& dir, const std::string& rel, S32 kind, S32 depth)
    {
        if (depth > 8)
        {
            return;
        }
        std::vector<std::string> names = List(dir);
        for (size_t i = 0; i < names.size(); i++)
        {
            std::string path = dir + "/" + names[i];
            std::string r = rel + "/" + names[i];
            if (IsDir(path))
            {
                ScanByStem(m, path, r, kind, depth + 1);
                continue;
            }
            if (kind == IMOD_KIND_TEXTURE && !IsTextureExt(Ext(names[i])))
            {
                Problem(m, r + ": not a .png, .dds, .tga or .bmp; ignored");
                continue;
            }
            AddEntry(m, kind, Lower(Stem(names[i])), path, r, "");
        }
    }

    void ScanInis(Mod& m, const std::string& dir)
    {
        std::vector<std::string> names = List(dir);
        for (size_t i = 0; i < names.size(); i++)
        {
            std::string path = dir + "/" + names[i];
            if (!IsDir(path))
            {
                AddEntry(m, IMOD_KIND_INI, Lower(names[i]), path, "ini/" + names[i], "");
            }
        }
    }

    bool ReadMod(const std::string& folder, Mod* m)
    {
        std::string dir = sFolder + "/" + folder;
        bool hasIni = IsFile(dir + "/mod.ini");
        bool hasContent = false;
        for (size_t i = 0; i < sizeof(kContentDirs) / sizeof(kContentDirs[0]); i++)
        {
            hasContent = hasContent || IsDir(dir + "/" + kContentDirs[i]);
        }
        if (!hasIni && !hasContent)
        {
            return false; // not a mod: some other folder (saves, a download, ...)
        }

        memset(&m->info, 0, sizeof(m->info));
        Copy(m->info.folder, sizeof(m->info.folder), folder);
        Copy(m->info.path, sizeof(m->info.path), dir + "/");
        Copy(m->info.name, sizeof(m->info.name), folder);
        m->info.enabled = 1;

        if (hasIni)
        {
            std::vector<U8> text;
            ReadWhole(dir + "/mod.ini", &text);
            std::vector<iModIniEntry> ini =
                iModIniRead(text.empty() ? "" : (const char*)&text[0], text.size());
            // [mod], or no section at all.
            for (size_t i = 0; i < ini.size(); i++)
            {
                if (ini[i].section == "")
                {
                    ini[i].section = "mod";
                }
            }
            std::string name = iModIniGet(ini, "mod", "name", "");
            if (!name.empty())
            {
                Copy(m->info.name, sizeof(m->info.name), name);
            }
            Copy(m->info.author, sizeof(m->info.author), iModIniGet(ini, "mod", "author", ""));
            Copy(m->info.version, sizeof(m->info.version), iModIniGet(ini, "mod", "version", ""));
            Copy(m->info.description, sizeof(m->info.description),
                 iModIniGet(ini, "mod", "description", ""));
            std::string on = Lower(iModIniGet(ini, "mod", "enabled", "1"));
            m->info.enabled = !(on == "0" || on == "off" || on == "false" || on == "no");
            m->info.priority = atoi(iModIniGet(ini, "mod", "priority", "0").c_str());
        }

        if (IsDir(dir + "/files"))
        {
            ScanFiles(*m, dir + "/files", "", 0);
        }
        if (IsDir(dir + "/assets"))
        {
            ScanAssets(*m, dir + "/assets", "assets", IMOD_KIND_ASSET, "");
        }
        if (IsDir(dir + "/sounds"))
        {
            ScanAssets(*m, dir + "/sounds", "sounds", IMOD_KIND_SOUND, "");
        }
        if (IsDir(dir + "/textures"))
        {
            ScanByStem(*m, dir + "/textures", "textures", IMOD_KIND_TEXTURE, 0);
        }
        if (IsDir(dir + "/fmv"))
        {
            ScanByStem(*m, dir + "/fmv", "fmv", IMOD_KIND_MOVIE, 0);
        }
        if (IsDir(dir + "/ini"))
        {
            ScanInis(*m, dir + "/ini");
        }
        return true;
    }

    // --- the index ---------------------------------------------------------------------

    void ClearPackages()
    {
        for (std::map<std::string, iModPkg*>::iterator it = sPackages.begin();
             it != sPackages.end(); ++it)
        {
            iModPkgRelease(it->second); // open files keep their own reference
        }
        sPackages.clear();
        for (size_t i = 0; i < sGCViews.size(); i++)
        {
            iModPkgRelease(sGCViews[i].pkg);
        }
        sGCViews.clear();
    }

    void BuildIndex()
    {
        for (S32 k = 0; k < IMOD_KIND_COUNT; k++)
        {
            sIndex[k].clear();
        }
        sInis.clear();
        sScopes.clear();
        sConflicts.clear();
        sReported.clear();
        ClearPackages();
        for (size_t i = 0; i < sMods.size(); i++)
        {
            sMods[i].info.conflicts = 0;
        }

        if (!sGlobalOn)
        {
            return;
        }

        for (size_t m = 0; m < sMods.size(); m++)
        {
            if (!sMods[m].info.enabled)
            {
                continue;
            }
            for (size_t e = 0; e < sMods[m].entries.size(); e++)
            {
                const Entry& entry = sMods[m].entries[e];
                Override o = { &entry, (S32)m };
                if (entry.kind == IMOD_KIND_INI)
                {
                    sInis[entry.key].push_back(o);
                    continue;
                }
                Index& index = sIndex[entry.kind];
                Index::iterator it = index.find(entry.key);
                if (it != index.end())
                {
                    std::string shown = entry.key;
                    size_t bar = shown.find('|');
                    if (bar != std::string::npos)
                    {
                        shown = (bar == 0 ? "any package" : shown.substr(0, bar)) + ": " +
                                shown.substr(bar + 1);
                    }
                    AddConflict(entry.kind, shown, (S32)m, it->second.mod);
                    it->second = o;
                }
                else
                {
                    index[entry.key] = o;
                }
                if (entry.kind == IMOD_KIND_ASSET || entry.kind == IMOD_KIND_SOUND)
                {
                    if (std::find(sScopes.begin(), sScopes.end(), entry.scope) == sScopes.end())
                    {
                        sScopes.push_back(entry.scope);
                    }
                }
            }
        }
    }

    // --- the folder ---------------------------------------------------------------------

    void FindFolder()
    {
        char exe[512];
        sDefaultFolder = iHostExeDir(exe, sizeof(exe)) ? Slashes(exe) + "/mods" : "mods";
        sFolder.clear();

#ifdef __ANDROID__
        // Android: iHostExeDir is the app's internal storage, which only
        // `adb shell run-as` reaches. The mods go in the external files folder
        // instead (Android/data/<package>/files/mods), beside the imported game
        // files and the log, where a computer can copy them over USB.
        std::string internalFolder = sDefaultFolder;
        const char* external = getenv("TSSM_ANDROID_EXTERNAL");
        if (external != NULL && external[0] != '\0')
        {
            sDefaultFolder = StripTrailingSlash(Slashes(external)) + "/mods";
        }
#endif

        std::string named;
        const char* env = getenv("TSSM_MODS_DIR");
        if (env != NULL && env[0] != '\0')
        {
            named = env;
        }
        else
        {
            named = iConfigGetString("assets.mods_folder", "");
        }
        named = StripTrailingSlash(Slashes(named.c_str()));

        if (!named.empty())
        {
            sDefaultFolder = named;
            if (IsDir(named))
            {
                sFolder = named;
            }
            return;
        }

        char data[512];
        std::vector<std::string> candidates;
        candidates.push_back(sDefaultFolder);
#ifdef __ANDROID__
        if (internalFolder != sDefaultFolder)
        {
            candidates.push_back(internalFolder);
        }
#endif
        if (iHostUserDataDir(data, sizeof(data)))
        {
            candidates.push_back(Slashes(data) + "/tssm/mods");
        }
        for (size_t i = 0; i < candidates.size(); i++)
        {
            if (IsDir(candidates[i]))
            {
                sFolder = candidates[i];
                return;
            }
        }
    }

    void ReadGlobalSwitch()
    {
        const char* env = getenv("TSSM_MODS");
        if (env != NULL && env[0] != '\0')
        {
            std::string v = Lower(env);
            sGlobalOn = !(v == "0" || v == "off" || v == "false" || v == "no");
            return;
        }
        sGlobalOn = iConfigGetBool("assets.mods", 1) != 0;
    }

    void OpenLog(bool append)
    {
        if (sLogFile != NULL)
        {
            fclose(sLogFile);
            sLogFile = NULL;
        }
        if (!sFolder.empty())
        {
            sLogFile = fopen((sFolder + "/mods.log").c_str(), append ? "a" : "w");
        }
    }

    // --- package lookups -------------------------------------------------------------

    struct PkgCtx
    {
        std::string scope; // the package's stem, lower case
        std::string label; // its file name, for the log
    };

    // The TEXT asset a .txt file stands for: a 32-bit length, the characters
    // and a NUL, padded to four bytes (what every TEXT in the game is). UTF-8
    // is turned into the game's 8-bit characters where it fits.
    std::vector<U8> EncodeText(const std::vector<U8>& file)
    {
        size_t b = 0;
        size_t e = file.size();
        if (e >= 3 && file[0] == 0xEF && file[1] == 0xBB && file[2] == 0xBF)
        {
            b = 3;
        }
        if (e > b && file[e - 1] == '\n')
        {
            e--;
        }
        if (e > b && file[e - 1] == '\r')
        {
            e--;
        }

        std::string text;
        for (size_t i = b; i < e; i++)
        {
            U8 c = file[i];
            if ((c == 0xC2 || c == 0xC3) && i + 1 < e && (file[i + 1] & 0xC0) == 0x80)
            {
                text += (char)(((c & 0x03) << 6) | (file[i + 1] & 0x3F));
                i++;
            }
            else
            {
                text += (char)c;
            }
        }

        U32 len = (U32)text.size();
        std::vector<U8> out(4 + ((len + 1 + 3) & ~3u), 0);
        out[0] = (U8)len;
        out[1] = (U8)(len >> 8);
        out[2] = (U8)(len >> 16);
        out[3] = (U8)(len >> 24);
        if (len > 0)
        {
            memcpy(&out[4], text.data(), len);
        }
        return out;
    }

    bool PkgLookup(void* vctx, const iModPkgAsset& a, iModPkgReplacement* out)
    {
        PkgCtx* ctx = (PkgCtx*)vctx;
        const bool sound = a.type == kTypeSND || a.type == kTypeSNDS;

        std::vector<std::string> keys;
        keys.push_back(IdKey(a.id));
        if (a.name != NULL && a.name[0] != '\0')
        {
            keys.push_back("name:" + Lower(a.name));
            keys.push_back("name:" + Lower(Stem(a.name)));
        }

        // Every match, most specific first: this package before any package,
        // assets/ before sounds/, ID before name.
        std::vector<Override> found;
        const char* scopes[2] = { ctx->scope.c_str(), "" };
        for (S32 s = 0; s < 2; s++)
        {
            if (std::find(sScopes.begin(), sScopes.end(), scopes[s]) == sScopes.end())
            {
                continue;
            }
            for (S32 k = 0; k < 2; k++)
            {
                if (k == 1 && !sound)
                {
                    continue;
                }
                const Index& index = sIndex[k == 0 ? IMOD_KIND_ASSET : IMOD_KIND_SOUND];
                for (size_t i = 0; i < keys.size(); i++)
                {
                    Index::const_iterator it = index.find(std::string(scopes[s]) + "|" + keys[i]);
                    if (it != index.end())
                    {
                        found.push_back(it->second);
                    }
                }
            }
        }
        if (found.empty())
        {
            return false;
        }

        // The latest mod wins; within one mod, the most specific key.
        size_t best = 0;
        for (size_t i = 1; i < found.size(); i++)
        {
            if (found[i].mod > found[best].mod)
            {
                best = i;
            }
        }
        for (size_t i = 0; i < found.size(); i++)
        {
            if (found[i].mod != found[best].mod)
            {
                char key[160];
                snprintf(key, sizeof(key), "%s: %08X '%s'", ctx->label.c_str(), a.id,
                         a.name != NULL ? a.name : "");
                AddConflict(found[best].entry->kind, key, found[best].mod, found[i].mod);
            }
        }

        const Entry& e = *found[best].entry;
        out->source = std::string(sMods[found[best].mod].info.name) + ": " + e.rel;

        if (e.kind == IMOD_KIND_SOUND)
        {
            iModPcm pcm;
            std::string error;
            if (!iModDecodeAudio(e.path.c_str(), &pcm, &error))
            {
                iModLogf("%s: %s: %s; the original sound is kept", ctx->label.c_str(),
                         out->source.c_str(), error.c_str());
                return false;
            }
            out->bytes.swap(pcm.bytes);
            out->pcm = true;
            out->channels = pcm.channels;
            out->rate = pcm.rate;
            return true;
        }

        std::vector<U8> data;
        if (!ReadWhole(e.path, &data))
        {
            iModLogf("%s: %s cannot be read; the original is kept", ctx->label.c_str(),
                     out->source.c_str());
            return false;
        }
        if (a.type == kTypeTEXT && Ext(e.path) == "txt")
        {
            data = EncodeText(data);
        }
        out->bytes.swap(data);
        return true;
    }

    // --- GameCube packages ---------------------------------------------------------------

    const U32 kTypeRWTX = 0x52575458; // 'RWTX'
    const U32 kTypeTEXS = 0x54455853; // 'TEXS'
    const U32 kTypeMODL = 0x4D4F444C; // 'MODL'

    // The sources of a GameCube view's own replacements, for the log.
#define kGCConverted "the GameCube conversion"
#define kGCNamed "its texture name"

    std::string Format(const char* fmt, ...)
    {
        char buf[768];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        return buf;
    }

    struct GCCtx
    {
        PkgCtx mod; // for PkgLookup
        bool mods; // whether any mod replaces assets in this package
        FILE* fp; // the package itself, for each asset's own bytes
        U32 fileSize;
        S32 converted;
        S32 named;
        S32 asIs;
        S32 replaced; // by a mod
        bool failed;
        std::string why;
    };

    // Each asset of a GameCube package as the port reads it: a mod's
    // replacement if there is one (Xbox layout, like every mod's assets),
    // otherwise the asset converted to the Xbox layout (iGCSwap.h), with the
    // texture names the disc lacks (iGCPackage.h). RenderWare and sound data
    // are left as they are; librw and the sound layer read the GameCube's own.
    // An asset with no rule, or one that does not fit its own counts, fails
    // the whole package: a package half converted is never handed out.
    bool GCLookup(void* vctx, const iModPkgAsset& a, iModPkgReplacement* out)
    {
        GCCtx* ctx = (GCCtx*)vctx;
        if (ctx->failed)
        {
            return false;
        }
        if (ctx->mods && PkgLookup(&ctx->mod, a, out))
        {
            ctx->replaced++;
            return true;
        }

        char tn[5];
        const char* name = a.name != NULL ? a.name : "";
        const bool texture = a.type == kTypeRWTX || a.type == kTypeTEXS;
        const bool model = a.type == kTypeMODL;
        const iGCSwapResult rule = iGCSwapRule(a.type);
        if (rule == IGCSWAP_UNKNOWN)
        {
            ctx->failed = true;
            ctx->why = Format("asset %08X '%s' is a %s, which the port cannot convert from the "
                              "GameCube layout yet",
                              a.id, name, iGCSwapTypeName(a.type, tn));
            return false;
        }
        if ((rule == IGCSWAP_ASIS && !texture && !model) || a.size == 0)
        {
            ctx->asIs++;
            return false;
        }

        // Checked before anything is allocated: a damaged size would
        // otherwise ask for up to 4 GB, which a 32-bit build cannot have.
        if (a.off > ctx->fileSize || a.size > ctx->fileSize - a.off)
        {
            ctx->failed = true;
            ctx->why = Format("asset %08X '%s' runs past the end of the file", a.id, name);
            return false;
        }
        std::vector<U8> bytes(a.size);
        if (fseek(ctx->fp, (long)a.off, SEEK_SET) != 0 ||
            fread(&bytes[0], 1, a.size, ctx->fp) != a.size)
        {
            ctx->failed = true;
            ctx->why = Format("asset %08X '%s' runs past the end of the file", a.id, name);
            return false;
        }

        // A model is read as it is, but read through by the game whatever
        // librw makes of it (zAssetTypes.cpp, Model_Read): one that does not
        // hold together fails the package here, as a damaged asset of a
        // converted type does.
        if (model)
        {
            if (!iGCPackageModelFits(&bytes[0], a.size))
            {
                ctx->failed = true;
                ctx->why = Format("asset %08X '%s' (MODL) holds no model that can be read", a.id,
                                  name);
                return false;
            }
            ctx->asIs++;
            return false;
        }

        if (texture)
        {
            if (iGCPackageRestoreTextureNames(&bytes[0], a.size, name) == 0)
            {
                ctx->asIs++;
                return false;
            }
            ctx->named++;
            out->bytes.swap(bytes);
            out->source = kGCNamed;
            return true;
        }

        const iGCSwapResult r = iGCSwapAsset(a.type, &bytes[0], a.size);
        if (r == IGCSWAP_ASIS)
        {
            ctx->asIs++;
            return false;
        }
        if (r != IGCSWAP_DONE)
        {
            ctx->failed = true;
            ctx->why = Format("asset %08X '%s' (%s) %s", a.id, name, iGCSwapTypeName(a.type, tn),
                              r == IGCSWAP_UNKNOWN ? "is of a kind the port cannot convert from "
                                                     "the GameCube layout yet"
                                                   : "does not fit its own sizes and counts");
            return false;
        }
        ctx->converted++;
        out->bytes.swap(bytes);
        out->source = kGCConverted;
        return true;
    }

    iModPkg* OpenGameCubePackage(const char* path, bool* failed)
    {
        iHostFileInfo info;
        if (!iHostStat(path, &info) || !info.is_file)
        {
            return NULL;
        }
        std::string key = Format("%s|%llu|%lld", Lower(Slashes(path)).c_str(),
                                 (unsigned long long)info.size, (long long)info.mtime);
        for (size_t i = 0; i < sGCViews.size(); i++)
        {
            if (sGCViews[i].key == key)
            {
                GCView v = sGCViews[i];
                sGCViews.erase(sGCViews.begin() + i);
                sGCViews.insert(sGCViews.begin(), v);
                if (v.failed && failed != NULL)
                {
                    *failed = true;
                }
                iModPkgRetain(v.pkg);
                return v.pkg;
            }
        }

        const U64 t0 = iHostMonotonicNs();
        GCCtx ctx;
        ctx.mod.label = Leaf(Slashes(path));
        ctx.mod.scope = Lower(Stem(ctx.mod.label));
        ctx.mods = sGlobalOn && !sScopes.empty() &&
                   (std::find(sScopes.begin(), sScopes.end(), std::string()) != sScopes.end() ||
                    std::find(sScopes.begin(), sScopes.end(), ctx.mod.scope) != sScopes.end());
        ctx.fp = fopen(path, "rb");
        ctx.fileSize = 0;
        ctx.converted = ctx.named = ctx.asIs = ctx.replaced = 0;
        ctx.failed = ctx.fp == NULL;
        if (ctx.fp == NULL)
        {
            ctx.why = "it cannot be opened";
        }
        else if (fseek(ctx.fp, 0, SEEK_END) == 0)
        {
            long len = ftell(ctx.fp);
            ctx.fileSize = len > 0 && (unsigned long)len <= 0xFFFFFFFFul ? (U32)len : 0;
        }
        if (ctx.mods)
        {
            sOpenedPackage = true;
        }

        iModPkg* pkg = NULL;
        std::vector<std::string> log;
        if (!ctx.failed)
        {
            // Read through the view whether or not anything changes, so a
            // package that cannot be parsed is a failure, never "nothing to
            // convert": read as it is, its big-endian data would reach the
            // game (iModPackage.h, IMODPKG_GAMECUBE).
            iModPkgStatus status = IMODPKG_UNREADABLE;
            pkg = iModPkgBuild(path, GCLookup, &ctx, &log, IMODPKG_GAMECUBE, &status);
            fclose(ctx.fp);
            if (!ctx.failed && status == IMODPKG_UNREADABLE)
            {
                ctx.failed = true;
                ctx.why = "its table of contents does not hold together";
                if (!log.empty())
                {
                    size_t b = log.back().find_first_not_of(' ');
                    ctx.why = b == std::string::npos ? ctx.why : log.back().substr(b);
                }
            }
        }
        if (ctx.failed)
        {
            iModPkgRelease(pkg);
            pkg = NULL;
            printf("tssm: GameCube package %s cannot be read: %s\n", path, ctx.why.c_str());
        }
        else
        {
            printf("tssm: GameCube package %s: %d asset(s) converted, %d texture name(s) "
                   "restored, %d left as they are (%.1f ms)\n",
                   ctx.mod.label.c_str(), (int)ctx.converted, (int)ctx.named, (int)ctx.asIs,
                   (double)(iHostMonotonicNs() - t0) / 1e6);
            if (ctx.replaced > 0)
            {
                iModLogf("%s: %d asset(s) replaced", ctx.mod.label.c_str(), (int)ctx.replaced);
            }
            // The build's line for each asset it was given; the converted
            // ones are counted above.
            for (size_t i = 0; i < log.size(); i++)
            {
                if (log[i].find("<- " kGCConverted) != std::string::npos ||
                    log[i].find("<- " kGCNamed) != std::string::npos)
                {
                    continue;
                }
                iModLogf("%s", log[i].c_str());
            }
        }
        fflush(stdout);
        if (failed != NULL)
        {
            *failed = ctx.failed;
        }

        GCView v = { key, pkg, ctx.failed };
        sGCViews.insert(sGCViews.begin(), v);
        U64 bytes = 0;
        for (size_t i = 0; i < sGCViews.size(); i++)
        {
            bytes += sGCViews[i].pkg != NULL ? iModPkgMemory(sGCViews[i].pkg) : 0;
        }
        while (sGCViews.size() > 1 && (sGCViews.size() > kMaxGCViews || bytes > kMaxGCViewBytes))
        {
            bytes -= sGCViews.back().pkg != NULL ? iModPkgMemory(sGCViews.back().pkg) : 0;
            iModPkgRelease(sGCViews.back().pkg);
            sGCViews.pop_back();
        }
        iModPkgRetain(pkg);
        return pkg;
    }
} // namespace

// --- life cycle --------------------------------------------------------------------------

void iModInit()
{
    bool first = !sInitialised;
    sInitialised = true;
    ReadGlobalSwitch();
    FindFolder();
    OpenLog(!first);
    iModTextureInstall();
    iModRescan();
}

S32 iModRescan()
{
    if (!sInitialised)
    {
        iModInit();
        return iModCount();
    }

    bool hadOpened = sOpenedPackage;
    std::string before = sFolder;
    FindFolder();
    if (sFolder != before)
    {
        OpenLog(false);
    }

    sMods.clear();
    if (!sFolder.empty())
    {
        // mods.ini first: its order and switches win over each mod.ini's.
        std::vector<std::pair<std::string, S32> > listed;
        std::vector<U8> text;
        if (ReadWhole(sFolder + "/mods.ini", &text))
        {
            std::vector<iModIniEntry> ini =
                iModIniRead(text.empty() ? "" : (const char*)&text[0], text.size());
            for (size_t i = 0; i < ini.size(); i++)
            {
                if (ini[i].section != "" && ini[i].section != "order")
                {
                    continue;
                }
                std::string v = Lower(ini[i].value);
                S32 on = !(v == "0" || v == "off" || v == "false" || v == "no");
                listed.push_back(std::make_pair(ini[i].key, on));
            }
        }

        std::vector<Mod> found;
        std::vector<std::string> dirs = List(sFolder);
        for (size_t i = 0; i < dirs.size(); i++)
        {
            Mod m;
            if (IsDir(sFolder + "/" + dirs[i]) && ReadMod(dirs[i], &m))
            {
                found.push_back(m);
            }
        }

        std::vector<bool> taken(found.size(), false);
        for (size_t l = 0; l < listed.size(); l++)
        {
            for (size_t f = 0; f < found.size(); f++)
            {
                if (!taken[f] && Lower(found[f].info.folder) == Lower(listed[l].first))
                {
                    taken[f] = true;
                    found[f].info.enabled = listed[l].second;
                    sMods.push_back(found[f]);
                    break;
                }
            }
        }

        // New mods, after the listed ones: by priority (higher loads later
        // and so wins), then by folder name.
        std::vector<Mod> rest;
        for (size_t f = 0; f < found.size(); f++)
        {
            if (!taken[f])
            {
                rest.push_back(found[f]);
            }
        }
        std::stable_sort(rest.begin(), rest.end(), [](const Mod& a, const Mod& b) {
            return a.info.priority < b.info.priority;
        });
        sMods.insert(sMods.end(), rest.begin(), rest.end());
    }

    BuildIndex();

    if (hadOpened)
    {
        sChangedAfterOpen = true;
    }

    if (sFolder.empty())
    {
        iModLogf("no mods folder (looked for %s); playing the unmodified game",
                 sDefaultFolder.c_str());
    }
    else
    {
        S32 on = 0;
        for (size_t i = 0; i < sMods.size(); i++)
        {
            on += sMods[i].info.enabled ? 1 : 0;
        }
        iModLogf("%s: %d mod(s), %d enabled%s; sounds: %s", sFolder.c_str(), (int)sMods.size(),
                 (int)on, sGlobalOn ? "" : " -- but [assets] mods is off, so none is applied",
                 iModAudioDecoders());
        for (size_t i = 0; i < sMods.size(); i++)
        {
            const iModInfo& m = sMods[i].info;
            iModLogf("  %d. %s%s%s%s [%s]: %d file(s), %d asset(s), %d texture(s), %d sound(s), "
                     "%d movie(s), %d ini overlay(s)",
                     (int)i + 1, m.name, m.version[0] ? " " : "", m.version,
                     m.enabled ? "" : " (off)", m.folder, m.counts[IMOD_KIND_FILE],
                     m.counts[IMOD_KIND_ASSET], m.counts[IMOD_KIND_TEXTURE],
                     m.counts[IMOD_KIND_SOUND], m.counts[IMOD_KIND_MOVIE],
                     m.counts[IMOD_KIND_INI]);
        }
    }
    return (S32)sMods.size();
}

const char* iModFolder()
{
    return sFolder.c_str();
}

const char* iModDefaultFolder()
{
    return sDefaultFolder.c_str();
}

S32 iModsEnabled()
{
    return sGlobalOn ? 1 : 0;
}

// --- the list ----------------------------------------------------------------------------

const char* iModKindName(S32 kind)
{
    static const char* const kNames[IMOD_KIND_COUNT] = { "file",  "asset", "texture",
                                                          "sound", "movie", "ini" };
    return kind >= 0 && kind < IMOD_KIND_COUNT ? kNames[kind] : "?";
}

S32 iModCount()
{
    return (S32)sMods.size();
}

const iModInfo* iModGet(S32 index)
{
    return index >= 0 && index < (S32)sMods.size() ? &sMods[index].info : NULL;
}

S32 iModFind(const char* folder)
{
    for (size_t i = 0; i < sMods.size(); i++)
    {
        if (Lower(sMods[i].info.folder) == Lower(folder != NULL ? folder : ""))
        {
            return (S32)i;
        }
    }
    return -1;
}

bool iModSetEnabled(S32 index, S32 enabled)
{
    if (index < 0 || index >= (S32)sMods.size())
    {
        return false;
    }
    sMods[index].info.enabled = enabled ? 1 : 0;
    BuildIndex();
    sChangedAfterOpen = sChangedAfterOpen || sOpenedPackage;
    iModLogf("%s %s", sMods[index].info.name, enabled ? "enabled" : "disabled");
    return true;
}

bool iModMove(S32 index, S32 newIndex)
{
    S32 n = (S32)sMods.size();
    if (index < 0 || index >= n || newIndex < 0 || newIndex >= n)
    {
        return false;
    }
    if (index != newIndex)
    {
        Mod m = sMods[index];
        sMods.erase(sMods.begin() + index);
        sMods.insert(sMods.begin() + newIndex, m);
        BuildIndex(); // the entries were copied with the mod; the index points at the new ones
        sChangedAfterOpen = sChangedAfterOpen || sOpenedPackage;
    }
    return true;
}

bool iModSaveList()
{
    std::string folder = sFolder.empty() ? sDefaultFolder : sFolder;
    if (!IsDir(folder) && !iHostMakeDir(folder.c_str()))
    {
        iModLogf("cannot create %s", folder.c_str());
        return false;
    }

    std::string path = folder + "/mods.ini";
    std::string temp = path + ".new";
    FILE* fp = fopen(temp.c_str(), "wb");
    if (fp == NULL)
    {
        iModLogf("cannot write %s", path.c_str());
        return false;
    }
    fprintf(fp, "; The mods in this folder, in load order: a mod further down wins where two\n"
                "; replace the same thing. 1 = on, 0 = off. Written by the mod manager; a\n"
                "; mod folder not listed here is added at the end (by its mod.ini priority).\n"
                "[order]\n");
    for (size_t i = 0; i < sMods.size(); i++)
    {
        fprintf(fp, "%s = %d\n", sMods[i].info.folder, sMods[i].info.enabled ? 1 : 0);
    }
    bool ok = fclose(fp) == 0 && iHostRenameReplace(temp.c_str(), path.c_str());
    if (ok && sFolder.empty())
    {
        sFolder = folder;
        OpenLog(true);
    }
    return ok;
}

bool iModRestartAdvised()
{
    return sChangedAfterOpen;
}

S32 iModConflictCount()
{
    return (S32)sConflicts.size();
}

const iModConflict* iModGetConflict(S32 index)
{
    return index >= 0 && index < (S32)sConflicts.size() ? &sConflicts[index] : NULL;
}

// --- the log -----------------------------------------------------------------------------

S32 iModLogCount()
{
    return (S32)sLog.size();
}

const char* iModLogLine(S32 index)
{
    return index >= 0 && index < (S32)sLog.size() ? sLog[index].c_str() : "";
}

void iModLogf(const char* fmt, ...)
{
    char line[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    printf("tssm: mod: %s\n", line);
    fflush(stdout);

    if (sLog.size() < kMaxLog)
    {
        sLog.push_back(line);
    }
    else if (sLog.size() == kMaxLog)
    {
        sLog.push_back("(the log is full; mods.log has the rest)");
    }

    if (sLogFile != NULL)
    {
        fprintf(sLogFile, "%s\n", line);
        fflush(sLogFile);
    }
}

// --- for the platform layer --------------------------------------------------------

bool iModResolveFile(const char* rel, char* out, size_t outsize)
{
    if (!sGlobalOn || sIndex[IMOD_KIND_FILE].empty() || rel == NULL)
    {
        return false;
    }
    std::string r = Slashes(rel);
    while (r.size() >= 2 && r[0] == '.' && r[1] == '/')
    {
        r = r.substr(2);
    }
    while (!r.empty() && r[0] == '/')
    {
        r = r.substr(1);
    }
    Index::const_iterator it = sIndex[IMOD_KIND_FILE].find(Lower(r));
    if (it == sIndex[IMOD_KIND_FILE].end())
    {
        return false;
    }
    if (!sReported[Lower("file:" + r)])
    {
        sReported[Lower("file:" + r)] = true;
        iModLogf("%s <- %s: %s", r.c_str(), sMods[it->second.mod].info.name,
                 it->second.entry->rel.c_str());
    }
    snprintf(out, outsize, "%s", it->second.entry->path.c_str());
    return true;
}

iModPkg* iModOpenPackage(const char* path, bool gamecube, bool* failed)
{
    if (failed != NULL)
    {
        *failed = false;
    }
    if (gamecube && path != NULL)
    {
        return OpenGameCubePackage(path, failed);
    }
    if (!sGlobalOn || sScopes.empty() || path == NULL)
    {
        return NULL;
    }

    std::string key = Lower(Slashes(path));
    std::map<std::string, iModPkg*>::iterator it = sPackages.find(key);
    if (it != sPackages.end())
    {
        iModPkgRetain(it->second);
        return it->second;
    }

    sOpenedPackage = true;

    PkgCtx ctx;
    ctx.label = Leaf(Slashes(path));
    ctx.scope = Lower(Stem(ctx.label));

    iModPkg* pkg = NULL;
    if (std::find(sScopes.begin(), sScopes.end(), std::string()) != sScopes.end() ||
        std::find(sScopes.begin(), sScopes.end(), ctx.scope) != sScopes.end())
    {
        std::vector<std::string> log;
        pkg = iModPkgBuild(path, PkgLookup, &ctx, &log);
        if (pkg != NULL)
        {
            iModLogf("%s: %d asset(s) replaced%s", ctx.label.c_str(),
                     (int)iModPkgReplacedCount(pkg),
                     iModPkgMovedLayers(pkg) > 0 ? ", some moved to the end of the package" : "");
        }
        for (size_t i = 0; i < log.size(); i++)
        {
            iModLogf("%s", log[i].c_str());
        }
    }

    sPackages[key] = pkg;
    iModPkgRetain(pkg);
    return pkg;
}

const char* iModFindTexture(const char* name)
{
    if (!sGlobalOn || name == NULL || sIndex[IMOD_KIND_TEXTURE].empty())
    {
        return NULL;
    }
    Index::const_iterator it = sIndex[IMOD_KIND_TEXTURE].find(Lower(name));
    return it == sIndex[IMOD_KIND_TEXTURE].end() ? NULL : it->second.entry->path.c_str();
}

bool iModFindMovie(const char* gameName, char* out, size_t outsize)
{
    if (!sGlobalOn || gameName == NULL)
    {
        return false;
    }
    std::string stem = Lower(Stem(Leaf(Slashes(gameName))));

    const Override* o = NULL;
    Index::const_iterator it = sIndex[IMOD_KIND_MOVIE].find(stem);
    if (it != sIndex[IMOD_KIND_MOVIE].end())
    {
        o = &it->second;
    }
    Index::const_iterator f = sIndex[IMOD_KIND_FILE].find("fmv/" + stem + ".bik");
    if (f != sIndex[IMOD_KIND_FILE].end() && (o == NULL || f->second.mod > o->mod))
    {
        o = &f->second;
    }
    if (o == NULL)
    {
        return false;
    }
    if (!sReported["movie:" + stem])
    {
        sReported["movie:" + stem] = true;
        iModLogf("movie %s <- %s: %s", stem.c_str(), sMods[o->mod].info.name,
                 o->entry->rel.c_str());
    }
    snprintf(out, outsize, "%s", o->entry->path.c_str());
    return true;
}

void* iModApplyIniOverlays(const char* rel, const char* text, U32 len, U32* outSize)
{
    if (!sGlobalOn || rel == NULL || sInis.empty())
    {
        return NULL;
    }
    std::string leaf = Lower(Leaf(Slashes(rel)));
    std::map<std::string, std::vector<Override> >::const_iterator it = sInis.find(leaf);
    if (it == sInis.end())
    {
        return NULL;
    }

    std::string merged(text, len);
    // What the loader padded with is not part of the file.
    while (!merged.empty() && merged[merged.size() - 1] == '\0')
    {
        merged.erase(merged.size() - 1);
    }
    for (size_t i = 0; i < it->second.size(); i++)
    {
        const Override& o = it->second[i];
        std::vector<U8> overlay;
        if (!ReadWhole(o.entry->path, &overlay))
        {
            iModLogf("%s: %s: %s cannot be read", leaf.c_str(), sMods[o.mod].info.name,
                     o.entry->rel.c_str());
            continue;
        }
        std::vector<std::string> log;
        merged = iModIniMerge(
            merged, std::string(overlay.empty() ? "" : (const char*)&overlay[0], overlay.size()),
            &log);
        iModLogf("%s <- %s: %s", leaf.c_str(), sMods[o.mod].info.name, o.entry->rel.c_str());
        for (size_t l = 0; l < log.size(); l++)
        {
            iModLogf("%s", log[l].c_str());
        }
    }

    U32 size = ((U32)merged.size() + 31) & ~31u;
    if (size == 0)
    {
        size = 32;
    }
    char* buf = (char*)malloc(size);
    if (buf == NULL)
    {
        return NULL;
    }
    memset(buf, 0, size);
    memcpy(buf, merged.data(), merged.size());
    *outSize = size;
    return buf;
}
