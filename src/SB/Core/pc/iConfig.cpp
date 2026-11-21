// config.ini. The argument for a second parser is in iConfig.h.

#include "iConfig.h"

#include "iConfigEdit.h"
#include "iConfigTable.h"
#include "iHost.h"
#include "iPadBind.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // Fixed storage, because this runs before xMemMgr has a heap and before
    // RenderWare has an allocator -- that is the whole point of the file. A
    // settings file is a few dozen lines; a file that exceeds these is
    // truncated with a message rather than silently losing its tail.
    // 64 was enough when every setting was a feature switch. The two binding
    // sections are one line per game button each, so a file that spells out
    // every binding is already 45 lines before anyone adds a comment.
    const S32 kMaxEntries = 128;
    const S32 kMaxKey = 64;
    const S32 kMaxValue = 512;
    const S32 kMaxLine = 512;
    const S32 kMaxPath = 512;

    struct Entry
    {
        char key[kMaxKey];
        char value[kMaxValue];
    };

    Entry sEntries[kMaxEntries];
    S32 sCount;
    S32 sLoaded;
    char sPath[kMaxPath];
    S32 sHavePath;

    void lowerInPlace(char* s)
    {
        for (; *s != '\0'; s++)
        {
            if (*s >= 'A' && *s <= 'Z')
            {
                *s = (char)(*s + ('a' - 'A'));
            }
        }
    }

    char* trim(char* s)
    {
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
        {
            s++;
        }

        if (*s == '\0')
        {
            return s;
        }

        char* end = s + strlen(s) - 1;
        while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        {
            end--;
        }
        end[1] = '\0';

        return s;
    }

    // The two binding sections are not in kConfigSettings, because their contents
    // are one row per game button and that list already exists in
    // iPadBind.cpp. Splitting it in two so the table could stay a literal
    // would be the one thing the table exists to prevent.
    //
    // Everything the table does for a normal setting, these two functions do
    // for a binding: name a key as known, and answer it with the default the
    // generated file was written with.
    const char* kPadSection = "pad";
    const char* kKeyboardSection = "keyboard";

    // "pad.a" -> the row for "a", when `key` is in `section`. NULL otherwise.
    const iPadBindButton* findBinding(const char* key, const char* section)
    {
        size_t n = strlen(section);
        if (strncmp(key, section, n) != 0 || key[n] != '.')
        {
            return NULL;
        }
        return iPadBindFind(key + n + 1);
    }

    // The default binding for "pad.a" or "keyboard.a", or NULL for a key that
    // is neither.
    const char* bindingDefault(const char* key)
    {
        const iPadBindButton* b = findBinding(key, kPadSection);
        if (b != NULL)
        {
            return iPadBindPadDefault(b);
        }

        b = findBinding(key, kKeyboardSection);
        if (b != NULL)
        {
            return b->key;
        }

        return NULL;
    }

    // Settings that moved to a new name. A file's line under the old name is
    // read as the new one, so an older config.ini keeps working; when a file
    // has both, the new name wins. The first change made to it in the game's
    // settings writes the new name and takes the old line out (iConfigSet).
    //
    // [video] water is the name the Xbox water had in the first builds that
    // had it. It is [extras] water now, beside the other features of the Xbox
    // release (iExtras.h).
    struct MovedKey
    {
        const char* from;
        const char* to;
    };

    // The mouse's two settings from when it turned the camera through the
    // right stick's value. They are mouse look's now (iMouseLook.h).
    const MovedKey kMovedKeys[] = {
        { "video.water", "extras.water" },
        { "input.mouse_camera", "input.mouse_look" },
        { "input.mouse_sensitivity", "input.mouse_sensitivity_x" },
    };

    // The new name of an old key, or NULL.
    const char* movedTo(const char* key)
    {
        for (size_t i = 0; i < sizeof(kMovedKeys) / sizeof(kMovedKeys[0]); i++)
        {
            if (iHostStrCaseCmp(kMovedKeys[i].from, key) == 0)
            {
                return kMovedKeys[i].to;
            }
        }
        return NULL;
    }

    // The old name of a key that moved, or NULL.
    const char* movedFrom(const char* key)
    {
        for (size_t i = 0; i < sizeof(kMovedKeys) / sizeof(kMovedKeys[0]); i++)
        {
            if (iHostStrCaseCmp(kMovedKeys[i].to, key) == 0)
            {
                return kMovedKeys[i].from;
            }
        }
        return NULL;
    }

    // [pad] and [keyboard]. One line per game button, and the grammar spelled
    // out once at the top of each -- a comment per binding would be thirty
    // paragraphs all saying the same thing.
    void writeBindingSection(FILE* f, const char* section, const char* inputs, const char* extra)
    {
        fprintf(f, "\n[%s]\n", section);
        fprintf(f, "; What presses each game button. Left of the '=' is the button, right\n");
        fprintf(f, "; of it is what presses it:\n");
        fprintf(f, ";\n");
        fprintf(f, ";   %s\n", inputs);
        fprintf(f, ";\n");
        fprintf(f, "; ',' is either one, '+' is both at once, '!' is NOT held, and nothing\n");
        fprintf(f, "; at all leaves the button unpressable.\n");
        fprintf(f, ";\n");
        fprintf(f, "%s", extra);

        // The [pad] lines are written COMMENTED OUT, and that is not tidiness.
        // A line here beats input.preset, so a generated file that spelled all
        // fifteen out would pin the pad to whichever preset was current when
        // the file was made and leave the setting doing nothing ever after.
        // Commented, they are what they should be: a listing of every button
        // and what is on it now, with uncommenting one the way to take it over.
        //
        // [keyboard] has no preset behind it, so its lines are the values and
        // are written as values.
        bool pad = (section == kPadSection);

        if (pad)
        {
            fprintf(f, "; These are what input.preset gives you. Uncomment a line to take that\n");
            fprintf(f, "; one button over; the rest keep following the preset.\n");
            fprintf(f, ";\n");
        }

        for (S32 i = 0; i < kPadBindButtonCount; i++)
        {
            const iPadBindButton* b = &kPadBindButtons[i];
            const char* value = pad ? iPadBindPadDefault(b) : b->key;
            const char* lead = pad ? "; " : "";

            const char* does = iPadBindDescribe(b);
            if (does != NULL)
            {
                fprintf(f, "%s%-12s = %-17s ; %s\n", lead, b->name, value, does);
            }
            else
            {
                fprintf(f, "%s%-12s = %s\n", lead, b->name, value);
            }
        }
    }

    void writeBindings(FILE* f)
    {
        // TSSM-ADAPTED: TSSM's rows (iPadBindTable.cpp) and inputs.
        writeBindingSection(
            f, kPadSection,
            "a b x y lb rb lt rt ls rs back start dpup dpdown dpleft dpright\n"
            ";   misc1 (share/capture) paddle1-paddle4 touchpad ltclick rtclick\n"
            ";   (positions: a is the bottom face button on every controller)",
            "; Left of the '=' is the button the game's prompts show (the Xbox's).\n"
            "; The '!' is for the gamecube preset, where Z turns L and R into white\n"
            "; and black. The sticks move and turn the camera; move_* and camera_*\n"
            "; rows can add buttons to them.\n"
            ";\n");

        writeBindingSection(
            f, kKeyboardSection,
            "any key by name -- letters, digits, space, enter, tab, escape,\n"
            ";   backspace, shift, ctrl, alt, up, down, left, right, f1 to f12,\n"
            ";   numpad0 to numpad9 -- and mouse1 to mouse5, wheelup, wheeldown",
            "; Keys play alongside a controller (input.keyboard). The mouse turns the\n"
            "; camera (input.mouse_look).\n"
            ";\n");
    }

    const Entry* findEntry(const char* key)
    {
        for (S32 i = 0; i < sCount; i++)
        {
            if (iHostStrCaseCmp(sEntries[i].key, key) == 0)
            {
                return &sEntries[i];
            }
        }
        return NULL;
    }

    // The file's line for `key`, or for the old name of a key that moved.
    const Entry* findEntryOrMoved(const char* key)
    {
        const Entry* e = findEntry(key);
        if (e == NULL)
        {
            const char* from = movedFrom(key);
            if (from != NULL)
            {
                e = findEntry(from);
            }
        }
        return e;
    }

    // What a query should read, in order: the file, then the table's default,
    // then the caller's fallback for a key that is in neither. The middle step
    // is what keeps the written defaults and the running defaults the same.
    const char* valueOf(const char* key)
    {
        const Entry* e = findEntryOrMoved(key);
        if (e != NULL)
        {
            return e->value;
        }

        const iConfigSetting* s = iConfigTableFind(key);
        if (s != NULL)
        {
            return s->value;
        }

        return bindingDefault(key);
    }

    // One line of the file. `section` is updated by a [header] and is what a
    // bare key is qualified with.
    void parseLine(char* line, char* section, size_t sectionSize, const char* path, S32 lineNo)
    {
        // Comments. Both spellings, because both are what people type into a
        // file called config.ini.
        char* comment = strchr(line, ';');
        if (comment != NULL)
        {
            *comment = '\0';
        }
        comment = strchr(line, '#');
        if (comment != NULL)
        {
            *comment = '\0';
        }

        char* s = trim(line);
        if (*s == '\0')
        {
            return;
        }

        if (*s == '[')
        {
            char* close = strchr(s, ']');
            if (close == NULL)
            {
                printf("tssm: %s:%d: section header with no ']', ignored\n", path, (int)lineNo);
                return;
            }
            *close = '\0';
            snprintf(section, sectionSize, "%s", trim(s + 1));
            lowerInPlace(section);
            return;
        }

        char* eq = strchr(s, '=');
        if (eq == NULL)
        {
            printf("tssm: %s:%d: no '=', ignored: %s\n", path, (int)lineNo, s);
            return;
        }

        *eq = '\0';
        char* name = trim(s);
        char* value = trim(eq + 1);

        if (*name == '\0')
        {
            printf("tssm: %s:%d: no key before '=', ignored\n", path, (int)lineNo);
            return;
        }

        if (sCount == kMaxEntries)
        {
            printf("tssm: %s:%d: more than %d settings, the rest are ignored\n", path, (int)lineNo,
                   (int)kMaxEntries);
            return;
        }

        Entry* e = &sEntries[sCount];
        if (section[0] != '\0')
        {
            snprintf(e->key, sizeof(e->key), "%s.%s", section, name);
        }
        else
        {
            snprintf(e->key, sizeof(e->key), "%s", name);
        }
        lowerInPlace(e->key);
        snprintf(e->value, sizeof(e->value), "%s", value);

        if (iConfigTableFind(e->key) == NULL && bindingDefault(e->key) == NULL)
        {
            const char* to = movedTo(e->key);
            if (to == NULL)
            {
                printf("tssm: %s:%d: unknown setting '%s', ignored\n", path, (int)lineNo, e->key);
                return;
            }
            // Kept under its old name, so the new name's own line still wins.
            printf("tssm: %s:%d: '%s' is now '%s'; read as that\n", path, (int)lineNo, e->key,
                   to);
        }

        sCount++;
    }

    // Defined below, next to the writer it shares its layout with.
    void appendMissingSettings(const char* path);

    bool readFile(const char* path)
    {
        FILE* f = fopen(path, "rb");
        if (f == NULL)
        {
            return false;
        }

        snprintf(sPath, sizeof(sPath), "%s", path);
        sHavePath = 1;

        char section[kMaxKey];
        section[0] = '\0';

        char line[kMaxLine];
        S32 lineNo = 0;
        while (fgets(line, sizeof(line), f) != NULL)
        {
            lineNo++;
            parseLine(line, section, sizeof(section), path, lineNo);
        }

        fclose(f);

        // Only now, with the whole file parsed: whether a key is missing is a
        // question about the file as a whole, not about any one line.
        appendMissingSettings(path);
        return true;
    }

    // Whether the file that was just read mentions a key at all. Distinct from
    // valueOf, which answers from the settings table when the file is silent --
    // which is exactly the case being looked for here.
    bool fileHas(const char* section, const char* name)
    {
        char key[kMaxKey];
        snprintf(key, sizeof(key), "%s.%s", section, name);
        lowerInPlace(key);
        // A line under a setting's old name counts: appending the new name at
        // its default would override what that line says.
        return findEntryOrMoved(key) != NULL;
    }

    // Settings this build has that the file does not.
    //
    // A config.ini written by an older build is missing every setting added
    // since, and iConfigWriteDefaults refuses to touch a file that already
    // exists -- so those settings stayed invisible, sitting at their defaults
    // with nothing anywhere naming them. That defeats the reason the file is
    // written at all: nobody should have to learn from documentation that a
    // setting exists.
    //
    // APPENDED, never rewritten. The existing bytes are not read back,
    // reordered or reformatted, so comments, ordering and hand edits survive
    // exactly, and the worst a failed write can do is leave a partial line at
    // the end -- which the parser reports by name rather than misreading.
    //
    // The section headers therefore repeat. The parser tracks the current
    // section as it goes and does not care, and one block under one banner is
    // easier to find than the same keys threaded back into place would be.
    //
    // A setting split off from another starts from what the file says for
    // that one, so the player keeps what they had: the mouse's two invert
    // settings were camera_invert_x and _y's ("stick and mouse") before the
    // mouse had its own.
    const char* startValue(const iConfigSetting& s)
    {
        static const char* const kFrom[][2] = {
            { "input.mouse_invert_x", "input.camera_invert_x" },
            { "input.mouse_invert_y", "input.camera_invert_y" },
        };

        char key[kMaxKey];
        snprintf(key, sizeof(key), "%s.%s", s.section, s.name);
        for (size_t i = 0; i < sizeof(kFrom) / sizeof(kFrom[0]); i++)
        {
            if (strcmp(key, kFrom[i][0]) == 0)
            {
                const Entry* e = findEntry(kFrom[i][1]);
                if (e != NULL)
                {
                    return e->value;
                }
            }
        }
        return s.value;
    }

    void appendMissingSettings(const char* path)
    {
        S32 missing = 0;
        for (size_t i = 0; i < (size_t)kConfigSettingCount; i++)
        {
            if (!fileHas(kConfigSettings[i].section, kConfigSettings[i].name))
            {
                missing++;
            }
        }
        // Only [keyboard]. [pad] is neither counted here nor written below: a
        // generated file lists the pad bindings COMMENTED, so input.preset
        // stays in charge of them, and a comment is not an entry -- fileHas
        // would call all fifteen missing on every run and append fifteen more
        // commented lines each time. Nothing is lost by leaving them out, since
        // a pad binding the file does not mention is answered by the preset,
        // which is what the setting is for.
        for (S32 i = 0; i < kPadBindButtonCount; i++)
        {
            if (!fileHas(kKeyboardSection, kPadBindButtons[i].name))
            {
                missing++;
            }
        }

        if (missing == 0)
        {
            return;
        }

        FILE* f = fopen(path, "ab");
        if (f == NULL)
        {
            printf("tssm: %s is missing %d setting%s this build has, and could not be "
                   "added to; they are at their defaults\n",
                   path, (int)missing, missing == 1 ? "" : "s");
            fflush(stdout);
            return;
        }

        fprintf(f, "\n");
        fprintf(f, "; ------------------------------------------------------------------\n");
        fprintf(f, "; Settings added by a newer build of the port. Every value below is\n");
        fprintf(f, "; its default (or what this file had for the setting it came from),\n");
        fprintf(f, "; so this block changes nothing.\n");

        const char* section = NULL;
        for (size_t i = 0; i < (size_t)kConfigSettingCount; i++)
        {
            if (fileHas(kConfigSettings[i].section, kConfigSettings[i].name))
            {
                continue;
            }
            if (section == NULL || strcmp(section, kConfigSettings[i].section) != 0)
            {
                section = kConfigSettings[i].section;
                fprintf(f, "\n[%s]\n", section);
            }
            const char* value = startValue(kConfigSettings[i]);
            fprintf(f, "\n; %s\n", kConfigSettings[i].comment);
            fprintf(f, "%s = %s\n", kConfigSettings[i].name, value);

            // This run reads what the file now says.
            if (strcmp(value, kConfigSettings[i].value) != 0 && sCount < kMaxEntries)
            {
                Entry* e = &sEntries[sCount++];
                snprintf(e->key, sizeof(e->key), "%s.%s", kConfigSettings[i].section,
                         kConfigSettings[i].name);
                lowerInPlace(e->key);
                // `value` is another entry's: copied before anything moves.
                char copy[kMaxValue];
                snprintf(copy, sizeof(copy), "%s", value);
                snprintf(e->value, sizeof(e->value), "%s", copy);
                printf("tssm: %s: %s starts as %s, the setting it was part of\n", path, e->key,
                       e->value);
            }
        }

        // The keyboard bindings, one line each, carrying their grammar in the
        // header the file already has further up. [pad] is left alone, for the
        // reason given where the count is taken.
        bool wroteHeader = false;
        for (S32 i = 0; i < kPadBindButtonCount; i++)
        {
            const iPadBindButton* b = &kPadBindButtons[i];
            if (fileHas(kKeyboardSection, b->name))
            {
                continue;
            }
            if (!wroteHeader)
            {
                fprintf(f, "\n[%s]\n", kKeyboardSection);
                wroteHeader = true;
            }

            const char* does = iPadBindDescribe(b);
            if (does != NULL)
            {
                fprintf(f, "%-12s = %-17s ; %s\n", b->name, b->key, does);
            }
            else
            {
                fprintf(f, "%-12s = %s\n", b->name, b->key);
            }
        }

        fclose(f);
        printf("tssm: %s did not have %d setting%s this build has; appended at the "
               "defaults\n",
               path, (int)missing, missing == 1 ? "" : "s");
        fflush(stdout);
    }

    // Where to write one when none was found. Beside the executable, because
    // that is where it will still be next time -- the working directory is
    // searched first but is wherever the game happened to be started from, and
    // writing there would leave a file that stops being found the moment
    // someone starts it from somewhere else.
    void createDefaultFile()
    {
        char dir[kMaxPath];
        char path[kMaxPath];

        if (iHostExeDir(dir, sizeof(dir)))
        {
            snprintf(path, sizeof(path), "%s/config.ini", dir);
        }
        else
        {
            snprintf(path, sizeof(path), "config.ini");
        }

        if (iConfigWriteDefaults(path))
        {
            snprintf(sPath, sizeof(sPath), "%s", path);
            sHavePath = 1;
            printf("tssm: no config.ini, so one was written with the defaults: %s\n", path);
        }
        else
        {
            printf("tssm: no config.ini, and one could not be written to %s; using defaults\n",
                   path);
        }
        fflush(stdout);
    }
} // namespace

bool iConfigWriteDefaults(const char* path)
{
    // Exclusive, so this can never overwrite a file -- including one that
    // appeared between the load's read and here. The argument for writing one
    // at all is in iConfig.h; why it is not fopen's "wx" is in iHost.h.
    FILE* f = iHostCreateNewFile(path);
    if (f == NULL)
    {
        return false;
    }

    // TSSM-ADAPTED: the program this file belongs to.
#ifdef __ANDROID__
    fprintf(f, "; %s (port) -- settings.\n", TSSM_GAME_TITLE);
#else
    fprintf(f, "; %s, PC port -- settings.\n", TSSM_GAME_TITLE);
#endif
    fprintf(f, "; Every value here is the default, so deleting this file changes nothing.\n");
    fprintf(f, "; Booleans take on/off, true/false, yes/no or 1/0.\n");

    iConfigTableWriteDefaults(f);
    writeBindings(f);

    fclose(f);
    return true;
}

void iConfigLoad()
{
    if (sLoaded)
    {
        return;
    }

    // Set before the search, not after. Every accessor calls this function, so
    // a report printed during parsing that ever grows a getter behind it would
    // otherwise recurse.
    sLoaded = 1;

    const char* explicitPath = getenv("TSSM_CONFIG");
    if (explicitPath != NULL && explicitPath[0] != '\0')
    {
        if (!readFile(explicitPath))
        {
            // Named explicitly and not there: create it where it was asked
            // for. A path someone typed is a stronger statement about where
            // the file belongs than the search order is.
            if (iConfigWriteDefaults(explicitPath))
            {
                snprintf(sPath, sizeof(sPath), "%s", explicitPath);
                sHavePath = 1;
                printf("tssm: TSSM_CONFIG names '%s', which did not exist; wrote the defaults "
                       "there\n",
                       explicitPath);
            }
            else
            {
                printf("tssm: TSSM_CONFIG names '%s', which could not be opened or created; "
                       "using defaults\n",
                       explicitPath);
            }
            fflush(stdout);
        }
        return;
    }

    if (readFile("config.ini"))
    {
        return;
    }

    char beside[kMaxPath];
    if (iHostExeDir(beside, sizeof(beside)))
    {
        char path[kMaxPath];
        snprintf(path, sizeof(path), "%s/config.ini", beside);
        if (readFile(path))
        {
            return;
        }
    }

    createDefaultFile();
}

const char* iConfigPath()
{
    iConfigLoad();
    return sHavePath ? sPath : NULL;
}

namespace
{
    // Settings changed with iConfigSet since the last iConfigSave.
    bool sChanged[kMaxEntries];

    // Settings taken out with iConfigUnset since the last iConfigSave, whose
    // lines the save removes.
    const S32 kMaxRemoved = 64;
    char sRemoved[kMaxRemoved][kMaxKey];
    S32 sRemovedCount;

    void forgetRemoved(const char* key)
    {
        for (S32 i = 0; i < sRemovedCount; i++)
        {
            if (strcmp(sRemoved[i], key) == 0)
            {
                memmove(sRemoved[i], sRemoved[i + 1], (size_t)(sRemovedCount - i - 1) * kMaxKey);
                sRemovedCount--;
                return;
            }
        }
    }

    bool splitKey(const char* key, char* section, char** name)
    {
        snprintf(section, kMaxKey, "%s", key);
        char* dot = strchr(section, '.');
        if (dot == NULL)
        {
            return false;
        }
        *dot = '\0';
        *name = dot + 1;
        return true;
    }

    // Takes `lower` out, and has the next save remove its line.
    void removeEntry(const char* lower)
    {
        for (S32 i = 0; i < sCount; i++)
        {
            if (strcmp(sEntries[i].key, lower) == 0)
            {
                memmove(&sEntries[i], &sEntries[i + 1], (size_t)(sCount - i - 1) * sizeof(Entry));
                memmove(&sChanged[i], &sChanged[i + 1], (size_t)(sCount - i - 1) * sizeof(bool));
                sCount--;
                break;
            }
        }

        forgetRemoved(lower);
        if (sRemovedCount < kMaxRemoved)
        {
            snprintf(sRemoved[sRemovedCount++], kMaxKey, "%s", lower);
        }
    }

    // A setting set or unset under its new name: the line under its old name
    // goes, so the file says it once.
    void removeMovedLine(const char* lower)
    {
        const char* from = movedFrom(lower);
        if (from != NULL && findEntry(from) != NULL)
        {
            removeEntry(from);
        }
    }
} // namespace

void iConfigUnset(const char* key)
{
    iConfigLoad();

    char lower[kMaxKey];
    snprintf(lower, sizeof(lower), "%s", key);
    lowerInPlace(lower);
    removeMovedLine(lower);
    removeEntry(lower);
}

void iConfigSet(const char* key, const char* value)
{
    iConfigLoad();

    char lower[kMaxKey];
    snprintf(lower, sizeof(lower), "%s", key);
    lowerInPlace(lower);
    forgetRemoved(lower);
    removeMovedLine(lower);

    for (S32 i = 0; i < sCount; i++)
    {
        if (strcmp(sEntries[i].key, lower) == 0)
        {
            if (strcmp(sEntries[i].value, value) != 0)
            {
                snprintf(sEntries[i].value, sizeof(sEntries[i].value), "%s", value);
                sChanged[i] = true;
            }
            return;
        }
    }

    if (sCount == kMaxEntries)
    {
        printf("tssm: config: no room to set '%s'\n", lower);
        return;
    }

    Entry* e = &sEntries[sCount];
    snprintf(e->key, sizeof(e->key), "%s", lower);
    snprintf(e->value, sizeof(e->value), "%s", value);
    sChanged[sCount] = true;
    sCount++;
}

bool iConfigSave()
{
    iConfigLoad();

    bool any = sRemovedCount > 0;
    for (S32 i = 0; i < sCount; i++)
    {
        any = any || sChanged[i];
    }
    if (!any)
    {
        return true;
    }
    if (!sHavePath)
    {
        return false;
    }

    iConfigEditFile* file = iConfigEditOpen(sPath);
    if (file == NULL)
    {
        printf("tssm: config: %s could not be read to save settings\n", sPath);
        return false;
    }

    bool ok = true;
    for (S32 i = 0; i < sCount && ok; i++)
    {
        if (!sChanged[i])
        {
            continue;
        }

        char section[kMaxKey];
        char* name;
        if (!splitKey(sEntries[i].key, section, &name))
        {
            continue;
        }

        ok = iConfigEditSet(file, section, name, sEntries[i].value);
    }

    for (S32 i = 0; i < sRemovedCount && ok; i++)
    {
        char section[kMaxKey];
        char* name;
        if (splitKey(sRemoved[i], section, &name))
        {
            iConfigEditUnset(file, section, name);
        }
    }

    ok = ok && iConfigEditSave(file, sPath);
    iConfigEditClose(file);

    if (ok)
    {
        memset(sChanged, 0, sizeof(sChanged));
        sRemovedCount = 0;
    }
    else
    {
        printf("tssm: config: settings could not be saved to %s\n", sPath);
    }
    return ok;
}

namespace
{
    // A value that cannot be read is reported once, not on every read: the
    // settings window reads its keys each frame it is drawn.
    bool firstReport(const char* key, const char* value)
    {
        static char sReported[16][kMaxKey + kMaxValue];
        static S32 sNext;

        char what[kMaxKey + kMaxValue];
        snprintf(what, sizeof(what), "%s=%s", key, value);
        for (S32 i = 0; i < 16; i++)
        {
            if (strcmp(sReported[i], what) == 0)
            {
                return false;
            }
        }
        snprintf(sReported[sNext], sizeof(sReported[sNext]), "%s", what);
        sNext = (sNext + 1) % 16;
        return true;
    }
} // namespace

S32 iConfigGetBool(const char* key, S32 def)
{
    iConfigLoad();

    const char* v = valueOf(key);
    if (v == NULL)
    {
        return def;
    }

    static const char* const kTrue[] = { "1", "true", "yes", "on" };
    static const char* const kFalse[] = { "0", "false", "no", "off" };

    for (size_t i = 0; i < sizeof(kTrue) / sizeof(kTrue[0]); i++)
    {
        if (iHostStrCaseCmp(v, kTrue[i]) == 0)
        {
            return TRUE;
        }
    }

    for (size_t i = 0; i < sizeof(kFalse) / sizeof(kFalse[0]); i++)
    {
        if (iHostStrCaseCmp(v, kFalse[i]) == 0)
        {
            return FALSE;
        }
    }

    if (firstReport(key, v))
    {
        printf("tssm: config: %s is '%s', not on or off; using the default\n", key, v);
    }
    return def;
}

S32 iConfigGetInt(const char* key, S32 def)
{
    iConfigLoad();

    const char* v = valueOf(key);
    if (v == NULL)
    {
        return def;
    }

    char* end = NULL;
    long parsed = strtol(v, &end, 10);
    if (end == v || (end != NULL && *end != '\0'))
    {
        if (firstReport(key, v))
        {
            printf("tssm: config: %s is '%s', not a whole number; using the default\n", key, v);
        }
        return def;
    }

    return (S32)parsed;
}

F32 iConfigGetFloat(const char* key, F32 def)
{
    iConfigLoad();

    const char* v = valueOf(key);
    if (v == NULL)
    {
        return def;
    }

    char* end = NULL;
    double parsed = strtod(v, &end);
    if (end == v || (end != NULL && *end != '\0'))
    {
        if (firstReport(key, v))
        {
            printf("tssm: config: %s is '%s', not a number; using the default\n", key, v);
        }
        return def;
    }

    return (F32)parsed;
}

const char* iConfigGetString(const char* key, const char* def)
{
    iConfigLoad();

    const char* v = valueOf(key);
    return v != NULL ? v : def;
}
