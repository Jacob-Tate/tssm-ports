// PC-only: mod.ini/mods.ini reading and overlays on the game's INI files. See
// iModIni.h.
#include "iModIni.h"

#include <ctype.h>
#include <string.h>

#include <map>

namespace
{
    std::string Lower(const std::string& s)
    {
        std::string out(s);
        for (size_t i = 0; i < out.size(); i++)
        {
            out[i] = (char)tolower((unsigned char)out[i]);
        }
        return out;
    }

    std::string Trim(const std::string& s)
    {
        size_t b = 0;
        size_t e = s.size();
        while (b < e && isspace((unsigned char)s[b]))
        {
            b++;
        }
        while (e > b && isspace((unsigned char)s[e - 1]))
        {
            e--;
        }
        return s.substr(b, e - b);
    }

    // Lines with their terminators, so the text can be put back as it was.
    std::vector<std::string> SplitLines(const std::string& text)
    {
        std::vector<std::string> lines;
        size_t start = 0;
        while (start < text.size())
        {
            size_t nl = text.find('\n', start);
            if (nl == std::string::npos)
            {
                lines.push_back(text.substr(start));
                break;
            }
            lines.push_back(text.substr(start, nl + 1 - start));
            start = nl + 1;
        }
        return lines;
    }

    std::string StripEol(const std::string& line)
    {
        size_t n = line.size();
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
        {
            n--;
        }
        return line.substr(0, n);
    }

    // One line of a game INI, the way xIniParse sees it.
    struct GameLine
    {
        enum
        {
            OTHER,
            SECTION,
            KEY
        } kind;
        std::string name; // section or key, as written
        std::string value;
    };

    GameLine ParseGameLine(const std::string& raw)
    {
        GameLine g;
        g.kind = GameLine::OTHER;
        std::string line = Trim(StripEol(raw));
        if (line.empty() || line[0] == '#')
        {
            return g;
        }
        if (line[0] == '[')
        {
            size_t close = line.find(']');
            if (close != std::string::npos)
            {
                std::string name = Trim(line.substr(1, close - 1));
                if (!name.empty())
                {
                    g.kind = GameLine::SECTION;
                    g.name = name;
                }
            }
            return g;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            return g;
        }
        std::string key = Trim(line.substr(0, eq));
        if (key.empty())
        {
            return g;
        }
        std::string value = line.substr(eq + 1);
        size_t hash = value.find('#');
        if (hash != std::string::npos)
        {
            value = value.substr(0, hash);
        }
        g.kind = GameLine::KEY;
        g.name = key;
        g.value = Trim(value);
        return g;
    }

    struct OverlayKey
    {
        std::string section; // as written
        std::string key; // as written
        std::vector<std::string> values;
        bool remove;
        bool placed;
        std::string was; // the original's first value, for the log
        bool existed;
    };
} // namespace

std::vector<iModIniEntry> iModIniRead(const char* text, size_t len)
{
    std::vector<iModIniEntry> out;
    std::string section;
    std::vector<std::string> lines = SplitLines(std::string(text, len));
    for (size_t i = 0; i < lines.size(); i++)
    {
        std::string line = Trim(StripEol(lines[i]));
        if (i == 0 && line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
        {
            line = Trim(line.substr(3)); // a UTF-8 byte order mark
        }
        if (line.empty() || line[0] == ';' || line[0] == '#')
        {
            continue;
        }
        if (line[0] == '[')
        {
            size_t close = line.find(']');
            section = Lower(Trim(line.substr(1, close == std::string::npos ? std::string::npos
                                                                             : close - 1)));
            continue;
        }
        size_t eq = line.find('=');
        iModIniEntry e;
        e.section = section;
        e.line = (int)i + 1;
        if (eq == std::string::npos)
        {
            e.key = line;
        }
        else
        {
            e.key = Trim(line.substr(0, eq));
            e.value = Trim(line.substr(eq + 1));
        }
        if (e.value.size() >= 2 && e.value[0] == '"' && e.value[e.value.size() - 1] == '"')
        {
            e.value = e.value.substr(1, e.value.size() - 2);
        }
        if (!e.key.empty())
        {
            out.push_back(e);
        }
    }
    return out;
}

std::string iModIniGet(const std::vector<iModIniEntry>& ini, const char* section, const char* key,
                       const char* def)
{
    std::string s = Lower(section);
    std::string k = Lower(key);
    for (size_t i = 0; i < ini.size(); i++)
    {
        if (ini[i].section == s && Lower(ini[i].key) == k)
        {
            return ini[i].value;
        }
    }
    return def;
}

std::string iModIniMerge(const std::string& original, const std::string& overlay,
                         std::vector<std::string>* log)
{
    // --- the overlay -----------------------------------------------------------
    std::vector<OverlayKey> keys;
    std::map<std::string, size_t> index; // "section\nkey", lower case
    std::vector<std::string> sectionOrder; // as written, first appearance
    {
        std::string section;
        std::vector<std::string> lines = SplitLines(overlay);
        for (size_t i = 0; i < lines.size(); i++)
        {
            std::string line = Trim(StripEol(lines[i]));
            if (i == 0 && line.size() >= 3 && (unsigned char)line[0] == 0xEF)
            {
                line = Trim(line.substr(3));
            }
            if (line.empty() || line[0] == '#' || line[0] == ';')
            {
                continue;
            }
            if (line[0] == '[')
            {
                size_t close = line.find(']');
                section = Trim(line.substr(1, close == std::string::npos ? std::string::npos
                                                                           : close - 1));
                continue;
            }

            bool remove = false;
            std::string key;
            std::string value;
            if (line[0] == '-' && line.find('=') == std::string::npos)
            {
                remove = true;
                key = Trim(line.substr(1));
            }
            else
            {
                size_t eq = line.find('=');
                if (eq == std::string::npos)
                {
                    continue;
                }
                key = Trim(line.substr(0, eq));
                value = Trim(line.substr(eq + 1));
            }
            if (key.empty())
            {
                continue;
            }

            std::string id = Lower(section) + "\n" + Lower(key);
            std::map<std::string, size_t>::iterator it = index.find(id);
            if (it == index.end())
            {
                OverlayKey k;
                k.section = section;
                k.key = key;
                k.remove = false;
                k.placed = false;
                k.existed = false;
                index[id] = keys.size();
                keys.push_back(k);
                it = index.find(id);

                bool known = false;
                for (size_t s = 0; s < sectionOrder.size(); s++)
                {
                    known = known || Lower(sectionOrder[s]) == Lower(section);
                }
                if (!known)
                {
                    sectionOrder.push_back(section);
                }
            }
            OverlayKey& k = keys[it->second];
            if (remove)
            {
                k.remove = true;
                k.values.clear();
            }
            else
            {
                k.remove = false;
                k.values.push_back(value);
            }
        }
    }

    if (keys.empty())
    {
        return original;
    }

    // --- the original, with the overlay applied ----------------------------------
    const std::string eol = original.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::vector<std::string> lines = SplitLines(original);
    std::string out;
    out.reserve(original.size() + overlay.size() + 64);

    auto emitKey = [&](const OverlayKey& k) {
        for (size_t v = 0; v < k.values.size(); v++)
        {
            out += k.key + " = " + k.values[v] + eol;
        }
    };

    // Adds what the overlay sets in `section` that has not been placed yet.
    auto flushSection = [&](const std::string& section) {
        std::string s = Lower(section);
        for (size_t i = 0; i < keys.size(); i++)
        {
            OverlayKey& k = keys[i];
            if (k.placed || Lower(k.section) != s)
            {
                continue;
            }
            k.placed = true;
            if (!k.remove)
            {
                if (!out.empty() && out[out.size() - 1] != '\n')
                {
                    out += eol;
                }
                emitKey(k);
            }
        }
    };

    std::string section;
    std::vector<std::string> seen; // sections the original has, lower case
    seen.push_back("");
    for (size_t i = 0; i < lines.size(); i++)
    {
        GameLine g = ParseGameLine(lines[i]);
        if (g.kind == GameLine::SECTION)
        {
            flushSection(section);
            section = g.name;
            seen.push_back(Lower(section));
        }
        else if (g.kind == GameLine::KEY)
        {
            std::map<std::string, size_t>::iterator it =
                index.find(Lower(section) + "\n" + Lower(g.name));
            if (it != index.end())
            {
                OverlayKey& k = keys[it->second];
                if (!k.existed)
                {
                    k.existed = true;
                    k.was = g.value;
                }
                if (!k.placed)
                {
                    k.placed = true;
                    emitKey(k);
                }
                continue; // the original line goes
            }
        }
        out += lines[i];
    }
    flushSection(section);

    // Sections the original does not have.
    for (size_t s = 0; s < sectionOrder.size(); s++)
    {
        bool have = false;
        for (size_t j = 0; j < seen.size(); j++)
        {
            have = have || seen[j] == Lower(sectionOrder[s]);
        }
        if (have)
        {
            continue;
        }
        bool any = false;
        for (size_t i = 0; i < keys.size(); i++)
        {
            any = any || (!keys[i].remove && Lower(keys[i].section) == Lower(sectionOrder[s]));
        }
        if (!any)
        {
            continue;
        }
        if (!out.empty() && out[out.size() - 1] != '\n')
        {
            out += eol;
        }
        out += "[" + sectionOrder[s] + "]" + eol;
        flushSection(sectionOrder[s]);
    }

    if (log != NULL)
    {
        for (size_t i = 0; i < keys.size(); i++)
        {
            const OverlayKey& k = keys[i];
            std::string name = k.section.empty() ? k.key : "[" + k.section + "] " + k.key;
            std::string now;
            for (size_t v = 0; v < k.values.size(); v++)
            {
                now += (v ? ", " : "") + k.values[v];
            }
            if (k.remove)
            {
                log->push_back(k.existed ? "  " + name + " removed (was " + k.was + ")"
                                         : "  " + name + " removed (was not set)");
            }
            else if (k.existed)
            {
                log->push_back("  " + name + " = " + now + " (was " + k.was + ")");
            }
            else
            {
                log->push_back("  " + name + " = " + now + " (added)");
            }
        }
    }

    return out;
}
