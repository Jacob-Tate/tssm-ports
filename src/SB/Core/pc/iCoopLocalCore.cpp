// PC-only, local co-op: the pipe's lines, the password, the child's
// environment and the profile folder (iCoopLocalCore.h).
#include "iCoopLocalCore.h"

#include "iNetCrypto.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

namespace iLocal
{

    const char kCodeAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

    const char* const kDropPrefixes[] = {
        "TSSM_COOP",
        "TSSM_TEST_",
        "TSSM_INPUT",
        "TSSM_CAPTURE",
        "TSSM_EXIT_AFTER",
        "TSSM_GUI_",
        "TSSM_LOCAL_",
        "TSSM_SAVE_DIR",
        "TSSM_LOG",
        "TSSM_CONFIG",
        "TSSM_CHILD_",
        // Files both copies would write at once.
        "TSSM_PERF",
        "TSSM_SND_DUMP",
        "TSSM_TEXDUMP",
        "TSSM_DUMPSHADOW",
        NULL,
    };

    const char kPassPrefix[] = "TSSM_CHILD_";

    namespace
    {

        const char kHelloHead[] = "hello 1 port=";
        const char kCodeKey[] = " code=";

        bool in_alphabet(char c)
        {
            return c != '\0' && strchr(kCodeAlphabet, c) != NULL;
        }

        char upper(char c)
        {
            return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
        }

        std::string upper_of(const std::string& s)
        {
            std::string u(s);
            for (char& c : u)
                c = upper(c);
            return u;
        }

        bool starts_ci(const std::string& s, const char* prefix)
        {
            size_t n = strlen(prefix);
            if (s.size() < n)
                return false;
            for (size_t i = 0; i < n; i++)
                if (upper(s[i]) != upper(prefix[i]))
                    return false;
            return true;
        }

        // NAME of NAME=VALUE. Windows keeps per-drive folders as "=C:=C:\x", so the
        // '=' that ends the name is the first one after the first character.
        bool name_of(const std::string& v, std::string* name)
        {
            if (v.find('\0') != std::string::npos)
                return false;
            size_t eq = v.find('=', 1);
            if (eq == std::string::npos)
                return false;
            *name = v.substr(0, eq);
            return true;
        }

        bool dropped(const std::string& name)
        {
            for (const char* const* p = kDropPrefixes; *p != NULL; p++)
                if (starts_ci(name, *p))
                    return true;
            return false;
        }

        struct Env
        {
            std::vector<std::pair<std::string, std::string> > v; // upper-case name, NAME=VALUE

            void put(const std::string& name, const std::string& entry)
            {
                std::string key = upper_of(name);
                for (auto& e : v)
                    if (e.first == key)
                    {
                        e.second = entry;
                        return;
                    }
                v.emplace_back(key, entry);
            }
        };

    } // namespace

    bool parse_line(const char* s, size_t n, Line* out)
    {
        if (s == NULL || n == 0 || n >= kMaxLine)
            return false;
        for (size_t i = 0; i < n; i++)
            if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] > 0x7E)
                return false;
        Line l;
        memset(&l, 0, sizeof l);
        if (n == 4 && memcmp(s, "quit", 4) == 0)
        {
            l.kind = LINE_QUIT;
            *out = l;
            return true;
        }
        const size_t head = sizeof kHelloHead - 1, key = sizeof kCodeKey - 1;
        if (n < head || memcmp(s, kHelloHead, head) != 0)
            return false;
        size_t i = head, digits = 0;
        uint32_t port = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9' && digits < 5)
        {
            port = port * 10 + (uint32_t)(s[i] - '0');
            i++;
            digits++;
        }
        if (digits < 4 || s[head] == '0' || port < 1024 || port > 65535)
            return false;
        if (n - i != key + kCodeLen || memcmp(s + i, kCodeKey, key) != 0)
            return false;
        i += key;
        for (size_t k = 0; k < kCodeLen; k++)
        {
            if (!in_alphabet(s[i + k]))
                return false;
            l.code[k] = s[i + k];
        }
        l.code[kCodeLen] = '\0';
        l.kind = LINE_HELLO;
        l.port = (uint16_t)port;
        *out = l;
        return true;
    }

    size_t format_line(const Line& l, char* out, size_t cap)
    {
        int n = -1;
        if (l.kind == LINE_QUIT)
            n = snprintf(out, cap, "quit\n");
        else if (l.kind == LINE_HELLO && l.port >= 1024 && code_valid(l.code))
            n = snprintf(out, cap, "%s%u%s%s\n", kHelloHead, (unsigned)l.port, kCodeKey, l.code);
        if (n <= 0 || (size_t)n >= cap || (size_t)n > kMaxLine)
            return 0;
        return (size_t)n;
    }

    void LineReader::feed(const uint8_t* d, size_t n,
                          void (*each)(void* ctx, bool ok, const Line& l), void* ctx)
    {
        Line bad;
        memset(&bad, 0, sizeof bad);
        for (size_t i = 0; i < n; i++)
        {
            char c = (char)d[i];
            if (c == '\n')
            {
                Line l;
                bool ok = !skipping && parse_line(buf, len, &l);
                each(ctx, ok, ok ? l : bad);
                // A line may hold the session's password.
                iNet::secure_zero(buf, sizeof buf);
                iNet::secure_zero(&l, sizeof l);
                len = 0;
                skipping = false;
            }
            else if (skipping)
            {
                continue;
            }
            else if (len == kMaxLine - 1)
            {
                skipping = true; // too long: dropped up to its newline
                iNet::secure_zero(buf, sizeof buf);
                len = 0;
            }
            else
            {
                buf[len++] = c;
            }
        }
    }

    bool code_valid(const char* s)
    {
        if (s == NULL || strlen(s) != kCodeLen)
            return false;
        for (size_t i = 0; i < kCodeLen; i++)
            if (!in_alphabet(s[i]))
                return false;
        return true;
    }

    bool new_code(char out[kCodeLen + 1])
    {
        uint8_t r[kCodeLen];
        if (!iNet::os_random(r, sizeof r))
        {
            out[0] = '\0';
            return false;
        }
        for (size_t i = 0; i < kCodeLen; i++)
            out[i] = kCodeAlphabet[r[i] & 31];
        out[kCodeLen] = '\0';
        iNet::secure_zero(r, sizeof r);
        return true;
    }

    std::vector<std::string> child_env(const std::vector<std::string>& parent,
                                       const std::vector<std::string>& set)
    {
        Env env, pass;
        std::string name;
        for (const std::string& v : parent)
        {
            if (!name_of(v, &name))
                continue;
            if (starts_ci(name, kPassPrefix))
            {
                std::string inner = name.substr(sizeof kPassPrefix - 1);
                if (inner.empty() || inner[0] == '=' || starts_ci(inner, kPassPrefix))
                    continue;
                pass.put(inner, inner + v.substr(name.size()));
            }
            else if (!dropped(name))
            {
                env.put(name, v);
            }
        }
        for (auto& e : pass.v)
            env.put(e.first, e.second);
        for (const std::string& v : set)
            if (name_of(v, &name))
                env.put(name, v);
        std::vector<std::string> out;
        out.reserve(env.v.size());
        for (auto& e : env.v)
            out.push_back(e.second);
        return out;
    }

    bool env_block(const std::vector<std::string>& vars, std::vector<char>* out)
    {
        std::vector<std::pair<std::string, const std::string*> > keyed;
        std::string name;
        for (const std::string& v : vars)
        {
            if (!name_of(v, &name))
                return false;
            keyed.emplace_back(upper_of(name), &v);
        }
        std::sort(keyed.begin(), keyed.end(),
                  [](const std::pair<std::string, const std::string*>& a,
                     const std::pair<std::string, const std::string*>& b) {
                      return a.first < b.first;
                  });
        for (size_t i = 1; i < keyed.size(); i++)
            if (keyed[i].first == keyed[i - 1].first)
                return false;
        out->clear();
        for (auto& k : keyed)
        {
            out->insert(out->end(), k.second->begin(), k.second->end());
            out->push_back('\0');
        }
        if (keyed.empty())
            out->push_back('\0'); // an empty block is two NULs
        out->push_back('\0');
        return true;
    }

    bool profile_dir(const char* saveRoot, char* out, size_t cap)
    {
        if (saveRoot == NULL || saveRoot[0] == '\0')
            return false;
        std::string s(saveRoot);
        for (char& c : s)
            if (c == '\\')
                c = '/';
        while (s.size() > 1 && s.back() == '/')
            s.pop_back();
        size_t slash = s.rfind('/');
        std::string parent;
        if (slash == std::string::npos)
            parent = ".";
        else if (slash == 0 || (slash == 2 && s[1] == ':'))
            parent = s.substr(0, slash + 1); // "/" or "E:/"
        else
            parent = s.substr(0, slash);
        std::string dir = parent + (parent.back() == '/' ? "" : "/") + "local2";
        if (dir.size() + 1 > cap)
            return false;
        memcpy(out, dir.c_str(), dir.size() + 1);
        return true;
    }

    bool scene_text_valid(const char* s)
    {
        if (s == NULL || strlen(s) != 4)
            return false;
        for (int i = 0; i < 4; i++)
            if (!((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9')))
                return false;
        return true;
    }

} // namespace iLocal
