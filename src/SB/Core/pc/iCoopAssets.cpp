// PC-only, co-op: the game-files hash both players must agree on
// (docs/MULTIPLAYER.md, 2.5). Runs on the co-op I/O thread, from paths the game
// thread copied out; touches nothing of the game's.
//
// Only the hash ever leaves this PC, never a file or a name from these
// folders. The per-file hashes are cached so a start after the first costs a
// directory walk: the cache is a text file of lines
//
//     <size> <mtime> <sha-256 hex> <relative path>
//
// and an entry is used only when the size and modification time still match.

#include "iCoopInternal.h"

#include "iHost.h"
#include "iNetCrypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace
{
    // Limits, so a folder that is not what it should be (a whole drive, a link
    // loop) cannot keep the I/O thread busy for ever.
    const S32 kMaxDepth = 12;
    const size_t kMaxFiles = 20000;

    struct FileEntry
    {
        std::string rel;  // lower case, '/'-separated
        std::string full;
        U64 size;
        S64 mtime;
    };

    std::string Lower(const std::string& s)
    {
        std::string out = s;
        for (char& c : out)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = (char)(c - 'A' + 'a');
            }
        }
        return out;
    }

    bool HasGameExtension(const std::string& lowerName)
    {
        size_t dot = lowerName.rfind('.');
        if (dot == std::string::npos)
        {
            return false;
        }
        std::string ext = lowerName.substr(dot + 1);
        return ext == "hip" || ext == "hop" || ext == "ini";
    }

    // Every file under `root` (or only the game's packages and INIs), with
    // its path relative to `root`. FALSE when `root` cannot be read at all.
    bool Walk(const std::string& root, const std::string& rel, S32 depth, bool onlyGameFiles,
              std::vector<FileEntry>& out)
    {
        if (depth > kMaxDepth || out.size() >= kMaxFiles)
        {
            return true;
        }
        std::string dir = rel.empty() ? root : root + "/" + rel;
        iHostDir* d = iHostDirOpen(dir.c_str());
        if (d == NULL)
        {
            return depth > 0;
        }
        std::vector<std::string> names;
        for (const char* e = iHostDirNext(d); e != NULL; e = iHostDirNext(d))
        {
            names.push_back(e);
        }
        iHostDirClose(d);

        for (const std::string& name : names)
        {
            if (out.size() >= kMaxFiles)
            {
                break;
            }
            std::string full = dir + "/" + name;
            std::string childRel = rel.empty() ? name : rel + "/" + name;
            iHostFileInfo info;
            if (!iHostStat(full.c_str(), &info))
            {
                continue;
            }
            if (info.is_dir)
            {
                Walk(root, childRel, depth + 1, onlyGameFiles, out);
            }
            else if (info.is_file)
            {
                std::string lowerRel = Lower(childRel);
                if (onlyGameFiles && !HasGameExtension(lowerRel))
                {
                    continue;
                }
                FileEntry f;
                f.rel = lowerRel;
                f.full = full;
                f.size = info.size;
                f.mtime = info.mtime;
                out.push_back(f);
            }
        }
        return true;
    }

    bool HashFile(const std::string& path, U8 out[32])
    {
        FILE* fp = fopen(path.c_str(), "rb");
        if (fp == NULL)
        {
            return false;
        }
        iNet::Sha256 s;
        iNet::sha256_init(&s);
        std::vector<U8> buf(1 << 20);
        for (;;)
        {
            size_t n = fread(buf.data(), 1, buf.size(), fp);
            if (n > 0)
            {
                iNet::sha256_update(&s, buf.data(), n);
            }
            if (n < buf.size())
            {
                break;
            }
        }
        bool ok = !ferror(fp);
        fclose(fp);
        iNet::sha256_final(&s, out);
        return ok;
    }

    void ToHex(const U8* d, size_t n, char* out)
    {
        static const char* x = "0123456789abcdef";
        for (size_t i = 0; i < n; i++)
        {
            out[2 * i] = x[d[i] >> 4];
            out[2 * i + 1] = x[d[i] & 15];
        }
        out[2 * n] = '\0';
    }

    bool FromHex(const char* s, U8 out[32])
    {
        for (int i = 0; i < 32; i++)
        {
            int v = 0;
            for (int k = 0; k < 2; k++)
            {
                char c = s[2 * i + k];
                int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
                if (d < 0)
                {
                    return false;
                }
                v = v * 16 + d;
            }
            out[i] = (U8)v;
        }
        return true;
    }

    struct CacheEntry
    {
        U64 size;
        S64 mtime;
        U8 hash[32];
    };

    // Keyed by the full path, so the same cache serves the asset folder and
    // every mod folder.
    typedef std::map<std::string, CacheEntry> Cache;

    void LoadCache(const std::string& path, Cache& cache)
    {
        if (path.empty())
        {
            return;
        }
        FILE* fp = fopen(path.c_str(), "rb");
        if (fp == NULL)
        {
            return;
        }
        char line[1200];
        while (fgets(line, sizeof(line), fp) != NULL)
        {
            unsigned long long size = 0;
            long long mtime = 0;
            char hex[65];
            int used = 0;
            if (sscanf(line, "%llu %lld %64s %n", &size, &mtime, hex, &used) != 3 || used <= 0)
            {
                continue;
            }
            std::string name = line + used;
            while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
            {
                name.pop_back();
            }
            CacheEntry e;
            e.size = size;
            e.mtime = mtime;
            if (strlen(hex) == 64 && FromHex(hex, e.hash) && !name.empty())
            {
                cache[name] = e;
            }
        }
        fclose(fp);
    }

    void SaveCache(const std::string& path, const Cache& cache)
    {
        if (path.empty())
        {
            return;
        }
        std::string temp = path + ".tmp";
        FILE* fp = fopen(temp.c_str(), "wb");
        if (fp == NULL)
        {
            return;
        }
        for (const auto& kv : cache)
        {
            char hex[65];
            ToHex(kv.second.hash, 32, hex);
            fprintf(fp, "%llu %lld %s %s\n", (unsigned long long)kv.second.size, (long long)kv.second.mtime,
                    hex, kv.first.c_str());
        }
        bool ok = fclose(fp) == 0;
        if (ok)
        {
            iHostRenameReplace(temp.c_str(), path.c_str());
        }
        else
        {
            iHostRemoveFile(temp.c_str());
        }
    }

    // Adds one folder's sorted file list to the running hash, under `tag`.
    bool HashFolder(iNet::Sha256& total, const char* tag, const std::string& root, bool onlyGameFiles,
                    Cache& cache, bool* cacheChanged, size_t* files)
    {
        std::vector<FileEntry> list;
        if (!Walk(root, "", 0, onlyGameFiles, list))
        {
            return false;
        }
        std::sort(list.begin(), list.end(), [](const FileEntry& a, const FileEntry& b) { return a.rel < b.rel; });

        iNet::sha256_update(&total, tag, strlen(tag) + 1);
        for (const FileEntry& f : list)
        {
            U8 hash[32];
            auto it = cache.find(f.full);
            if (it != cache.end() && it->second.size == f.size && it->second.mtime == f.mtime)
            {
                memcpy(hash, it->second.hash, 32);
            }
            else
            {
                if (!HashFile(f.full, hash))
                {
                    // Unreadable: counted as a file with no content, so both
                    // sides still have to agree on its name and size.
                    memset(hash, 0, sizeof(hash));
                }
                else
                {
                    CacheEntry e;
                    e.size = f.size;
                    e.mtime = f.mtime;
                    memcpy(e.hash, hash, 32);
                    cache[f.full] = e;
                    *cacheChanged = true;
                }
            }
            U8 size[8];
            for (int i = 0; i < 8; i++)
            {
                size[i] = (U8)(f.size >> (8 * i));
            }
            iNet::sha256_update(&total, f.rel.c_str(), f.rel.size() + 1);
            iNet::sha256_update(&total, size, 8);
            iNet::sha256_update(&total, hash, 32);
        }
        *files += list.size();
        return true;
    }
} // namespace

bool iCoopHashAssets(const iCoopAssetJob& job, uint8_t out[iNet::kHashSize], char* why,
                     size_t whySize, size_t* fileCount)
{
    Cache cache;
    LoadCache(job.cachePath, cache);
    bool changed = false;
    size_t files = 0;

    iNet::Sha256 total;
    iNet::sha256_init(&total);
    iNet::sha256_update(&total, "tssm-coop assets/1", 19);

    std::string root = job.assetRoot.empty() ? "." : job.assetRoot;
    if (!HashFolder(total, "game", root, true, cache, &changed, &files) || files == 0)
    {
        snprintf(why, whySize, "the game's files could not be read (check [assets] path)");
        return false;
    }
    if (!job.modRoot.empty())
    {
        HashFolder(total, "mod", job.modRoot, false, cache, &changed, &files);
    }
    for (size_t i = 0; i < job.mods.size(); i++)
    {
        HashFolder(total, "mods", job.mods[i], false, cache, &changed, &files);
    }
    iNet::sha256_final(&total, out);

    if (changed)
    {
        SaveCache(job.cachePath, cache);
    }
    *fileCount = files;
    printf("tssm: coop: game files checked (%u files)\n", (unsigned)files);
    fflush(stdout);
    return true;
}
