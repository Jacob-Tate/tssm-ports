#ifndef IMODINI_H
#define IMODINI_H

#include <string>
#include <vector>

// PC-only: the two INI dialects the mod system reads. New code.
//
// 1. Its own files, mod.ini and mods.ini: `[section]`, `key = value`, comments
//    starting with ';' or '#', values optionally in double quotes.
//
// 2. Overlays on the GAME's INI files (sb04.ini, explode.ini, water.ini), which
//    xIniParse reads: `key = value`, '#' comments (also after a value),
//    optional `[section]`s, and a key may repeat (LanguageSetting) -- the game
//    takes the first, or walks them all. A key is the same key in any case
//    (xStricmp).

struct iModIniEntry
{
    std::string section; // lower case, "" before the first header
    std::string key; // as written
    std::string value; // trimmed, quotes removed
    int line; // 1-based
};

// Every `key = value` of a mod.ini-style file, in file order.
std::vector<iModIniEntry> iModIniRead(const char* text, size_t len);

// The value of `section.key` (case-insensitive), or `def`.
std::string iModIniGet(const std::vector<iModIniEntry>& ini, const char* section, const char* key,
                       const char* def);

// Applies an overlay to the text of one of the game's INI files.
//
// For each key the overlay sets, within its [section] (keys before any header
// belong to the part of the file before the first header):
//
//   * every line of that key in that section of the original is dropped, and
//     the overlay's lines for the key -- one or several, as the game allows --
//     go where the first of them was;
//   * a key the original does not have is added at the end of its section, and
//     a section the original does not have is added at the end of the file;
//   * `-key` (a minus sign, no value) removes the key's lines and adds none.
//
// Everything else -- comments, blank lines, other keys, line endings -- is
// kept as it was. `log` gets one line per change.
std::string iModIniMerge(const std::string& original, const std::string& overlay,
                         std::vector<std::string>* log);

#endif
