// tssm_savetexttest: the save screens' warning as a setting ([game] save_text,
// iSaveText.h), without the game.
//
//   tssm_savetexttest [<Xbox files> [<GameCube files>]]
//
// With no folders: every line of both discs, as the table has it, rewritten
// with every choice and checked against the words written out again here; the
// disc's own console leaves its line exactly as it is; strings that are not
// one of the lines (near misses included) are never rewritten; the setting's
// words. With the folders: every TEXT asset of every package of each disc is
// read, the table's lines are checked to be the disc's exact strings, every
// warning of that kind on the disc is checked to be in the table, and every
// TEXT asset is put through every choice to show that nothing else changes.

#include "iSaveText.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

static int sFailures;
static int sChecks;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        sChecks++;                                                                                 \
        if (!(cond))                                                                               \
        {                                                                                          \
            sFailures++;                                                                           \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

// ---------------------------------------------------------------------------
// The two discs' lines and every choice's words, written out again on their
// own: a change to the table that is not also made here fails.

namespace
{
    struct Expect
    {
        iSaveTextChoice disc;
        bool playing; // the "While playing, ..." line
        const char* before;
        const char* warning;
        const char* after;
    };

    const Expect kExpect[] = {
        { ISAVETEXT_XBOX, false, "", "Please don't turn off your Xbox console", "" },
        { ISAVETEXT_XBOX, false, "Loading Game.....{n}", "Please don't turn off your Xbox console.",
          "" },
        { ISAVETEXT_XBOX, false, "Saving Game.....{n}", "Please don't turn off your Xbox console.",
          "" },
        { ISAVETEXT_XBOX, true, "The game uses an autosave feature. While playing, ",
          "do not turn off your Xbox console.",
          "{n}{n}Would you like to create a game save to save your progress?" },
        { ISAVETEXT_GAMECUBE, false, "{var:MCAccessType}...{n}",
          "Do not touch the Memory Card or the POWER Button", "" },
        { ISAVETEXT_GAMECUBE, true, "The game uses an autosave feature. While playing, ",
          "Do not remove the Memory Card or power off the Nintendo GameCube.",
          "{n}{n}Would you like to create a game file to save your progress?" },
        { ISAVETEXT_GAMECUBE, false, "{font=0}{w*1.5}{h*1.5}Formatting...{n}",
          "Do not touch the Memory Card or POWER Button", "" },
        { ISAVETEXT_GAMECUBE, false, "{font=0}{w*1.5}{h*1.5}Deleting file(s)....{n}",
          "Do not touch the Memory Card or POWER Button.", "" },
    };
    const int kExpectCount = (int)(sizeof(kExpect) / sizeof(kExpect[0]));

    // Indexed by iSaveTextChoice; auto is checked as what it resolves to.
    struct ExpectWords
    {
        const char* word;
        const char* note;
        const char* playing;
    };

    const ExpectWords kExpectWords[ISAVETEXT_COUNT] = {
        { "auto", NULL, NULL },
        { "xbox", "Please don't turn off your Xbox console.", "do not turn off your Xbox console." },
        { "ps2",
          "Do not remove {i:PS2_MEMCARD} in {var:MCSelectedCard}, or the {i:PS2_PAD_NAME}, or "
          "reset/switch off the console.",
          "Do not remove {i:PS2_MEMCARD}, the {i:PS2_PAD_NAME}, or reset/switch off the "
          "console." },
        { "gamecube", "Do not touch the Memory Card or the POWER Button",
          "Do not remove the Memory Card or power off the Nintendo GameCube." },
        { "generic", "Please don't turn off the power or quit the game.",
          "do not turn off the power or quit the game." },
        { "pc", "Please don't close the game.", "do not close the game." },
        { "android", "Please don't close the app.", "do not close the app." },
        { "switch", "Please don't close the game or turn off the console.",
          "do not close the game or turn off the console." },
        { "3ds", "Please don't turn off the power or close the game.",
          "do not turn off the power or close the game." },
    };

    std::string Whole(const Expect& e)
    {
        return std::string(e.before) + e.warning + e.after;
    }

    // Every {...} span of `s`, in order: the markup a rewrite must keep.
    std::vector<std::string> Markup(const std::string& s)
    {
        std::vector<std::string> out;
        size_t at = 0;
        while ((at = s.find('{', at)) != std::string::npos)
        {
            size_t end = s.find('}', at);
            if (end == std::string::npos)
            {
                out.push_back(s.substr(at));
                break;
            }
            out.push_back(s.substr(at, end - at + 1));
            at = end + 1;
        }
        return out;
    }
} // namespace

static void TestTable()
{
    printf("the table\n");
    CHECK(iSaveTextLineCount() == kExpectCount, "%d lines in the table, %d written out here",
          (int)iSaveTextLineCount(), kExpectCount);

    for (int i = 0; i < kExpectCount && i < iSaveTextLineCount(); i++)
    {
        char line[512];
        const char* asset = NULL;
        iSaveTextChoice disc = ISAVETEXT_AUTO;
        CHECK(iSaveTextLine(i, line, sizeof(line), &asset, &disc), "line %d", i);
        CHECK(Whole(kExpect[i]) == line, "line %d is the disc's: %s", i, line);
        CHECK(disc == kExpect[i].disc, "line %d's disc", i);
        CHECK(asset != NULL && asset[0] != '\0', "line %d names its asset", i);
        CHECK(iSaveTextIsLine(line), "line %d is recognised", i);
    }
}

static void TestEveryChoice()
{
    printf("every line with every choice\n");
    for (int i = 0; i < kExpectCount; i++)
    {
        const Expect& e = kExpect[i];
        const std::string original = Whole(e);
        for (int c = 0; c < ISAVETEXT_COUNT; c++)
        {
            const iSaveTextChoice choice = (iSaveTextChoice)c;
            const iSaveTextChoice resolved = c == ISAVETEXT_AUTO ? ISAVETEXT_PC : choice;

            std::string want;
            if (resolved == e.disc)
            {
                want = original; // the disc's own console: exactly as it is
            }
            else
            {
                want = std::string(e.before) +
                       (e.playing ? kExpectWords[resolved].playing : kExpectWords[resolved].note) +
                       e.after;
            }

            char out[512];
            memset(out, 'X', sizeof(out));
            const S32 ok = iSaveTextRewrite(original.c_str(), choice, out, sizeof(out));
            CHECK(ok, "line %d, %s: rewritten", i, kExpectWords[c].word);
            CHECK(want == out, "line %d, %s:\n   got  %s\n   want %s", i, kExpectWords[c].word, out,
                  want.c_str());

            // What is around the warning, markup included, is the disc's.
            const std::string got(out);
            CHECK(got.compare(0, strlen(e.before), e.before) == 0, "line %d, %s: the start is kept",
                  i, kExpectWords[c].word);
            CHECK(got.size() >= strlen(e.after) &&
                      got.compare(got.size() - strlen(e.after), strlen(e.after), e.after) == 0,
                  "line %d, %s: the end is kept", i, kExpectWords[c].word);
            std::vector<std::string> before = Markup(original);
            std::vector<std::string> after = Markup(got);
            for (size_t m = 0; m < before.size(); m++)
            {
                bool found = false;
                for (size_t n = 0; n < after.size(); n++)
                {
                    found = found || after[n] == before[m];
                }
                CHECK(found, "line %d, %s: markup %s kept", i, kExpectWords[c].word,
                      before[m].c_str());
            }

            // The game's copy has 320 bytes for the string (iSaveText.cpp).
            CHECK(got.size() < 320, "line %d, %s: %d characters fit the copy", i,
                  kExpectWords[c].word, (int)got.size());

            // Rewriting a rewritten line does nothing more: it is no longer one
            // of the disc's lines unless it is the disc's own line.
            char again[512];
            const S32 twice = iSaveTextRewrite(out, ISAVETEXT_GENERIC, again, sizeof(again));
            CHECK(!twice || want == original, "line %d, %s: a rewritten line is not a line again",
                  i, kExpectWords[c].word);
        }
    }

    // A buffer too small is a refusal, not a cut string.
    char tiny[8];
    CHECK(!iSaveTextRewrite(Whole(kExpect[3]).c_str(), ISAVETEXT_GENERIC, tiny, sizeof(tiny)),
          "a rewrite that does not fit is refused");
}

static void TestNotLines()
{
    printf("what is never rewritten\n");
    const char* const kOthers[] = {
        "",
        "Please don't turn off your Xbox console.", // a sentence alone, not a line
        "Please don't turn off your Xbox console ",  // a space added
        "please don't turn off your Xbox console",  // another case
        "Loading Game.....{n}Please don't turn off your Xbox console",  // no full stop
        "Saving Game.....{n}Please don't turn off your Xbox console.\n", // a newline added
        "{var:MCAccessType}...{n}Do not touch the Memory Card or the POWER Button.",
        "WARNING: {var:MCAccessType}...{n}Do not remove {i:PS2_MEMCARD} in {var:MCSelectedCard}, "
        "or the {i:PS2_PAD_PAL}, or reset/switch off the console",
        "WARNING: {var:MCAccessType}...{n}Do not remove {i:PS2_MEMCARD} in {var:MCSelectedCard}, "
        "or the {i:PS2_PAD_PAL}, or reset/power off the console.",
        "Autosaving...",
        "Saving data",
        "{i:keyword}Unable to autosave game!{~:c}{n}Your Xbox doesn't have enough free blocks to "
        "create a new save game.",
        "Mod text: Please don't turn off your Xbox console",
    };
    for (size_t i = 0; i < sizeof(kOthers) / sizeof(kOthers[0]); i++)
    {
        char out[512] = "untouched";
        for (int c = 0; c < ISAVETEXT_COUNT; c++)
        {
            CHECK(!iSaveTextRewrite(kOthers[i], (iSaveTextChoice)c, out, sizeof(out)),
                  "\"%s\" (%s) is left alone", kOthers[i], kExpectWords[c].word);
        }
        CHECK(!iSaveTextIsLine(kOthers[i]), "\"%s\" is not a line", kOthers[i]);
    }
    char out[16];
    CHECK(!iSaveTextRewrite(NULL, ISAVETEXT_PC, out, sizeof(out)), "NULL is left alone");
}

static void TestSetting()
{
    printf("the setting\n");
    for (int c = 0; c < ISAVETEXT_COUNT; c++)
    {
        bool ok = false;
        CHECK(iSaveTextParse(kExpectWords[c].word, &ok) == (iSaveTextChoice)c && ok, "%s parses",
              kExpectWords[c].word);
        CHECK(strcmp(iSaveTextChoiceName((iSaveTextChoice)c), kExpectWords[c].word) == 0,
              "%s is its own word", kExpectWords[c].word);
        CHECK(iSaveTextChoiceLabel((iSaveTextChoice)c)[0] != '\0', "%s has a label",
              kExpectWords[c].word);
        if (c != ISAVETEXT_AUTO)
        {
            CHECK(iSaveTextResolve((iSaveTextChoice)c) == (iSaveTextChoice)c, "%s is itself",
                  kExpectWords[c].word);
            CHECK(strcmp(iSaveTextSentence((iSaveTextChoice)c), kExpectWords[c].note) == 0,
                  "%s's sentence", kExpectWords[c].word);
        }
    }

    bool ok = false;
    CHECK(iSaveTextParse("XBOX", &ok) == ISAVETEXT_XBOX && ok, "any case");
    CHECK(iSaveTextParse("GameCube", &ok) == ISAVETEXT_GAMECUBE && ok, "any case (GameCube)");
    CHECK(iSaveTextParse("3DS", &ok) == ISAVETEXT_3DS && ok, "any case (3DS)");
    CHECK(iSaveTextParse("", &ok) == ISAVETEXT_AUTO && ok, "empty is auto");
    CHECK(iSaveTextParse(NULL, &ok) == ISAVETEXT_AUTO && ok, "none is auto");
    CHECK(iSaveTextParse("dreamcast", &ok) == ISAVETEXT_AUTO && !ok, "a bad value is auto, said");
    CHECK(iSaveTextParse("xbo", &ok) == ISAVETEXT_AUTO && !ok, "a part of a word is bad");
    CHECK(iSaveTextParse("xboxx", &ok) == ISAVETEXT_AUTO && !ok, "a longer word is bad");
    CHECK(iSaveTextParse("ps2 ", &ok) == ISAVETEXT_AUTO && !ok, "a space is bad");

    // A PC build: auto is the PC's words.
    CHECK(iSaveTextPlatform() == ISAVETEXT_PC, "auto is pc on this build");
    CHECK(iSaveTextResolve(ISAVETEXT_AUTO) == ISAVETEXT_PC, "auto resolves to pc");
    CHECK(iSaveTextResolve((iSaveTextChoice)99) == ISAVETEXT_PC, "nonsense resolves to pc");
    CHECK(strcmp(iSaveTextSentence(ISAVETEXT_AUTO), "Please don't close the game.") == 0,
          "auto's sentence is the PC's");
}

// ---------------------------------------------------------------------------
// The discs themselves

namespace
{
    uint32_t BE32(const uint8_t* p)
    {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    }

    struct TextAsset
    {
        std::string package;
        uint32_t id;
        std::string text;
    };

    bool ReadFile(const std::string& path, std::vector<uint8_t>& out)
    {
        FILE* f = fopen(path.c_str(), "rb");
        if (f == NULL)
        {
            return false;
        }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        out.resize(n > 0 ? (size_t)n : 0);
        bool ok = n <= 0 || fread(&out[0], 1, (size_t)n, f) == (size_t)n;
        fclose(f);
        return ok;
    }

    // Every TEXT asset's string in a HIP/HOP: the chunk ids and sizes and the
    // AHDR fields are big-endian on both consoles; the string follows the
    // asset's 4-byte length either way.
    void ReadTexts(const std::string& path, const std::string& rel, std::vector<TextAsset>& out)
    {
        std::vector<uint8_t> b;
        if (!ReadFile(path, b) || b.size() < 8 || memcmp(&b[0], "HIPA", 4) != 0)
        {
            return;
        }

        struct Walker
        {
            const std::vector<uint8_t>& b;
            const std::string& rel;
            std::vector<TextAsset>& out;

            void Walk(size_t o, size_t end)
            {
                while (o + 8 <= end && o + 8 <= b.size())
                {
                    const uint8_t* c = &b[o];
                    const uint32_t size = BE32(c + 4);
                    const size_t body = o + 8;
                    if (memcmp(c, "HIPA", 4) == 0)
                    {
                        o += 8;
                        continue;
                    }
                    if (body + size > b.size())
                    {
                        return;
                    }
                    if (memcmp(c, "PACK", 4) == 0 || memcmp(c, "DICT", 4) == 0 ||
                        memcmp(c, "ATOC", 4) == 0 || memcmp(c, "LTOC", 4) == 0 ||
                        memcmp(c, "STRM", 4) == 0)
                    {
                        Walk(body, body + size);
                    }
                    else if (memcmp(c, "AHDR", 4) == 0 && size >= 24)
                    {
                        const uint8_t* h = &b[body];
                        if (memcmp(h + 4, "TEXT", 4) == 0)
                        {
                            const uint32_t off = BE32(h + 8);
                            const uint32_t len = BE32(h + 12);
                            if (len > 4 && (size_t)off + len <= b.size())
                            {
                                const char* s = (const char*)&b[off + 4];
                                size_t n = 0;
                                while (n < len - 4 && s[n] != '\0')
                                {
                                    n++;
                                }
                                TextAsset t;
                                t.package = rel;
                                t.id = BE32(h);
                                t.text.assign(s, n);
                                out.push_back(t);
                            }
                        }
                    }
                    o = body + size;
                }
            }
        };

        Walker w = { b, rel, out };
        w.Walk(0, b.size());
    }

    bool EndsWithPackage(const std::string& name)
    {
        if (name.size() < 4)
        {
            return false;
        }
        std::string ext = name.substr(name.size() - 4);
        for (size_t i = 0; i < ext.size(); i++)
        {
            ext[i] = (char)tolower((unsigned char)ext[i]);
        }
        return ext == ".hip" || ext == ".hop";
    }

    void ListPackages(const std::string& root, const std::string& rel,
                      std::vector<std::pair<std::string, std::string> >& out)
    {
#ifdef _WIN32
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((root + "/" + rel + "*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
        {
            return;
        }
        do
        {
            std::string name = fd.cFileName;
            if (name == "." || name == "..")
            {
                continue;
            }
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                ListPackages(root, rel + name + "/", out);
            }
            else if (EndsWithPackage(name))
            {
                out.push_back(std::make_pair(root + "/" + rel + name, rel + name));
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
#else
        DIR* d = opendir((root + "/" + rel).c_str());
        if (d == NULL)
        {
            return;
        }
        while (struct dirent* e = readdir(d))
        {
            std::string name = e->d_name;
            if (name == "." || name == "..")
            {
                continue;
            }
            struct stat st;
            std::string full = root + "/" + rel + name;
            if (stat(full.c_str(), &st) != 0)
            {
                continue;
            }
            if (S_ISDIR(st.st_mode))
            {
                ListPackages(root, rel + name + "/", out);
            }
            else if (EndsWithPackage(name))
            {
                out.push_back(std::make_pair(full, rel + name));
            }
        }
        closedir(d);
#endif
    }

    // A warning of the save screens' kind, as the discs word them: what the
    // table must hold all of.
    bool LooksLikeWarning(const std::string& s)
    {
        static const char* const kSigns[] = {
            "turn off your Xbox", "POWER Button", "power off the Nintendo GameCube",
            "reset/switch off", "reset/power off",
        };
        for (size_t i = 0; i < sizeof(kSigns) / sizeof(kSigns[0]); i++)
        {
            if (s.find(kSigns[i]) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }
} // namespace

static void TestDisc(const char* folder, iSaveTextChoice disc)
{
    const char* name = disc == ISAVETEXT_XBOX ? "Xbox" : "GameCube";
    printf("the %s disc: %s\n", name, folder);

    std::vector<std::pair<std::string, std::string> > packages;
    ListPackages(folder, "", packages);
    std::vector<TextAsset> texts;
    for (size_t i = 0; i < packages.size(); i++)
    {
        // A folder of copies some tools leave beside the files is not the disc.
        if (packages[i].second.compare(0, 7, "backup/") == 0)
        {
            continue;
        }
        ReadTexts(packages[i].first, packages[i].second, texts);
    }
    CHECK(texts.size() > 1000, "%d TEXT assets read from %d packages", (int)texts.size(),
          (int)packages.size());

    // Every line of this disc in the table is on the disc, exactly.
    for (int i = 0; i < kExpectCount; i++)
    {
        if (kExpect[i].disc != disc)
        {
            continue;
        }
        const std::string want = Whole(kExpect[i]);
        int found = 0;
        for (size_t t = 0; t < texts.size(); t++)
        {
            found += texts[t].text == want;
        }
        CHECK(found > 0, "the %s line \"%s\" is on the disc", name, want.c_str());
    }

    // Every warning on the disc is in the table, but the PS2 release's
    // European lines, which nothing shows.
    int warnings = 0;
    for (size_t t = 0; t < texts.size(); t++)
    {
        const std::string& s = texts[t].text;
        if (!LooksLikeWarning(s) || s.compare(0, 9, "WARNING: ") == 0)
        {
            continue;
        }
        warnings++;
        CHECK(iSaveTextIsLine(s.c_str()), "%s %08X \"%s\" is in the table",
              texts[t].package.c_str(), (unsigned)texts[t].id, s.c_str());
    }

    // Every TEXT asset with every choice: only the table's lines are
    // rewritten, and on this disc's own choice none of them changes.
    int rewritten = 0;
    for (size_t t = 0; t < texts.size(); t++)
    {
        const std::string& s = texts[t].text;
        const bool line = iSaveTextIsLine(s.c_str()) != 0;
        for (int c = 0; c < ISAVETEXT_COUNT; c++)
        {
            char out[1024];
            const S32 ok = iSaveTextRewrite(s.c_str(), (iSaveTextChoice)c, out, sizeof(out));
            CHECK(ok == (S32)line, "%s %08X: rewritten only if a line (%s)",
                  texts[t].package.c_str(), (unsigned)texts[t].id, kExpectWords[c].word);
            if (ok && (iSaveTextChoice)c == disc)
            {
                CHECK(s == out, "%s %08X: the %s choice leaves it as it is",
                      texts[t].package.c_str(), (unsigned)texts[t].id, name);
            }
            if (ok && s != out)
            {
                rewritten++;
            }
        }
    }
    printf("   %d TEXT assets, %d warnings, all in the table; %d rewrites across the choices\n",
           (int)texts.size(), warnings, rewritten);
}

int main(int argc, char** argv)
{
    TestTable();
    TestEveryChoice();
    TestNotLines();
    TestSetting();

    if (argc > 1 && argv[1][0] != '\0')
    {
        TestDisc(argv[1], ISAVETEXT_XBOX);
    }
    else
    {
        printf("(no Xbox folder given: the disc checks are skipped)\n");
    }
    if (argc > 2 && argv[2][0] != '\0')
    {
        TestDisc(argv[2], ISAVETEXT_GAMECUBE);
    }
    else
    {
        printf("(no GameCube folder given: the disc checks are skipped)\n");
    }

    printf("%s: %d checks, %d failed\n", sFailures == 0 ? "PASS" : "FAIL", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
