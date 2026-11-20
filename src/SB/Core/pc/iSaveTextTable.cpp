// The save screens' warning lines and what each [game] save_text choice puts
// in their place; see iSaveText.h. Port-only, and nothing but tables and string
// work, so tests/savetexttest.cpp links it on its own.

#include "iSaveText.h"

#include <stdio.h>
#include <string.h>

namespace
{
    // Where a line's warning sits, which decides how a choice words it: a
    // sentence of its own, or the end of "While playing, ..." in the New Game
    // question.
    enum Kind
    {
        KIND_NOTE,
        KIND_PLAYING
    };

    // Every line of the two US discs that tells the player what not to do
    // while the game reads or writes a save, found by searching both discs'
    // TEXT assets. Each is the disc's string exactly, cut in three: what comes
    // before the warning, the warning, and what comes after. Only a string that
    // is one of these, byte for byte, is ever rewritten, and only its warning:
    // the rest of it, markup included, is left as the disc has it.
    //
    // The PS2 release's own lines are still on both discs ("SV MC DONTREMOVE
    // PAL TXT", "LD MC DONTREMOVE PAL TXT"), for its European version; nothing
    // shows them in this game, and they are left alone.
    struct Line
    {
        const char* asset;   // the TEXT asset's name, for the reader
        iSaveTextChoice disc; // whose disc has it: ISAVETEXT_XBOX or ISAVETEXT_GAMECUBE
        Kind kind;
        const char* before;
        const char* warning;
        const char* after;
    };

    const Line kLines[] = {
        // The Xbox disc.
        { "AUTO SAVE TXT (MN/mnuc_US.hip)", ISAVETEXT_XBOX, KIND_NOTE, "",
          "Please don't turn off your Xbox console", "" },
        { "MC DONTREMOVE TXT (MN/mnuc_US.hip), LD MC DONTREMOVE TXT (MN/mnus_US.hip)",
          ISAVETEXT_XBOX, KIND_NOTE, "Loading Game.....{n}",
          "Please don't turn off your Xbox console.", "" },
        { "SV MC DONTREMOVE TXT (MN/mnuc_US.hip)", ISAVETEXT_XBOX, KIND_NOTE,
          "Saving Game.....{n}", "Please don't turn off your Xbox console.", "" },
        { "CREATE NEW GAME TXT (MN/mnus_US.hip)", ISAVETEXT_XBOX, KIND_PLAYING,
          "The game uses an autosave feature. While playing, ",
          "do not turn off your Xbox console.",
          "{n}{n}Would you like to create a game save to save your progress?" },

        // The GameCube disc.
        { "SV MC DONTREMOVE TXT (MN/mnuc_US.hip), LD MC DONTREMOVE TXT (MN/mnus_US.hip)",
          ISAVETEXT_GAMECUBE, KIND_NOTE, "{var:MCAccessType}...{n}",
          "Do not touch the Memory Card or the POWER Button", "" },
        { "CREATE NEW GAME TXT (MN/mnus_US.hip)", ISAVETEXT_GAMECUBE, KIND_PLAYING,
          "The game uses an autosave feature. While playing, ",
          "Do not remove the Memory Card or power off the Nintendo GameCube.",
          "{n}{n}Would you like to create a game file to save your progress?" },
        { "text_mem_card_formatting (font_US.hip)", ISAVETEXT_GAMECUBE, KIND_NOTE,
          "{font=0}{w*1.5}{h*1.5}Formatting...{n}", "Do not touch the Memory Card or POWER Button",
          "" },
        { "text_mem_card_deleting_file (font_US.hip)", ISAVETEXT_GAMECUBE, KIND_NOTE,
          "{font=0}{w*1.5}{h*1.5}Deleting file(s)....{n}",
          "Do not touch the Memory Card or POWER Button.", "" },
    };

    const S32 kLineCount = (S32)(sizeof(kLines) / sizeof(kLines[0]));

    // What each choice says, as a sentence of its own and as the end of "While
    // playing, ...". Xbox and GameCube are their discs' own words; on the disc
    // that has the line, choosing that disc leaves the line exactly as it is
    // (iSaveTextRewrite), so these are only what the OTHER disc's lines get.
    //
    // PS2: there is no dump of the PS2 release here yet. Its words below are
    // the PS2's standard memory card warning, built from the PS2 text both
    // discs still carry: {i:PS2_MEMCARD} is the PS2 release's own "memory card
    // (8MB) (for PlayStation(R)2)" asset (font_US.hip on both discs), and the
    // rest follows its European lines ("...or reset/switch off the console").
    // Correct this row from the PS2 disc's mnuc_US/mnus_US lines when it is
    // dumped; nothing else needs to change.
    struct Words
    {
        const char* note;
        const char* playing;
    };

    const Words kWords[ISAVETEXT_COUNT] = {
        /* auto: resolved before use */ { NULL, NULL },
        /* xbox */ { "Please don't turn off your Xbox console.", "do not turn off your Xbox console." },
        /* ps2 */
        { "Do not remove the {i:PS2_MEMCARD}, or reset/switch off the console.",
          "do not remove the {i:PS2_MEMCARD}, or reset/switch off the console." },
        /* gamecube */
        { "Do not touch the Memory Card or the POWER Button",
          "Do not remove the Memory Card or power off the Nintendo GameCube." },
        /* generic */
        { "Please don't turn off the power or quit the game.",
          "do not turn off the power or quit the game." },
        /* pc */ { "Please don't close the game.", "do not close the game." },
        /* android */ { "Please don't close the app.", "do not close the app." },
        /* switch */
        { "Please don't close the game or turn off the console.",
          "do not close the game or turn off the console." },
        /* 3ds */
        { "Please don't turn off the power or close the game.",
          "do not turn off the power or close the game." },
    };

    struct Name
    {
        const char* word;  // config.ini
        const char* label; // the Settings row
    };

    const Name kNames[ISAVETEXT_COUNT] = {
        { "auto", "This platform's own" },
        { "xbox", "Xbox (original)" },
        { "ps2", "PlayStation 2" },
        { "gamecube", "GameCube (original)" },
        { "generic", "Any console" },
        { "pc", "PC" },
        { "android", "Android" },
        { "switch", "Switch" },
        { "3ds", "3DS" },
    };

    bool Valid(iSaveTextChoice c)
    {
        return (S32)c >= 0 && (S32)c < ISAVETEXT_COUNT;
    }

    // The line `text` is, or -1.
    S32 FindLine(const char* text)
    {
        if (text == NULL)
        {
            return -1;
        }
        const size_t n = strlen(text);
        for (S32 i = 0; i < kLineCount; i++)
        {
            const Line& l = kLines[i];
            const size_t b = strlen(l.before);
            const size_t w = strlen(l.warning);
            const size_t a = strlen(l.after);
            if (n == b + w + a && memcmp(text, l.before, b) == 0 &&
                memcmp(text + b, l.warning, w) == 0 && memcmp(text + b + w, l.after, a) == 0)
            {
                return i;
            }
        }
        return -1;
    }
} // namespace

const char* iSaveTextChoiceName(iSaveTextChoice choice)
{
    return Valid(choice) ? kNames[choice].word : "auto";
}

const char* iSaveTextChoiceLabel(iSaveTextChoice choice)
{
    return Valid(choice) ? kNames[choice].label : "";
}

iSaveTextChoice iSaveTextParse(const char* value, bool* ok)
{
    if (ok != NULL)
    {
        *ok = true;
    }
    if (value == NULL || value[0] == '\0')
    {
        return ISAVETEXT_AUTO;
    }
    for (S32 i = 0; i < ISAVETEXT_COUNT; i++)
    {
        const char* a = value;
        const char* b = kNames[i].word;
        while (*a != '\0' && *b != '\0' &&
               (*a == *b || (*a >= 'A' && *a <= 'Z' && *a - 'A' + 'a' == *b)))
        {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0')
        {
            return (iSaveTextChoice)i;
        }
    }
    if (ok != NULL)
    {
        *ok = false;
    }
    return ISAVETEXT_AUTO;
}

iSaveTextChoice iSaveTextPlatform()
{
#if defined(__ANDROID__)
    return ISAVETEXT_ANDROID;
#elif defined(__SWITCH__)
    return ISAVETEXT_SWITCH;
#elif defined(__3DS__)
    return ISAVETEXT_3DS;
#else
    return ISAVETEXT_PC;
#endif
}

iSaveTextChoice iSaveTextResolve(iSaveTextChoice choice)
{
    if (!Valid(choice) || choice == ISAVETEXT_AUTO)
    {
        return iSaveTextPlatform();
    }
    return choice;
}

const char* iSaveTextSentence(iSaveTextChoice choice)
{
    return kWords[iSaveTextResolve(choice)].note;
}

S32 iSaveTextIsLine(const char* text)
{
    return FindLine(text) >= 0;
}

S32 iSaveTextRewrite(const char* text, iSaveTextChoice choice, char* out, size_t outSize)
{
    const S32 i = FindLine(text);
    if (i < 0 || out == NULL || outSize == 0)
    {
        return FALSE;
    }

    const Line& l = kLines[i];
    const iSaveTextChoice c = iSaveTextResolve(choice);

    // The disc's own console: its line exactly as it is, punctuation and all.
    const char* warning = c == l.disc ? l.warning :
                          (l.kind == KIND_PLAYING ? kWords[c].playing : kWords[c].note);

    const int n = snprintf(out, outSize, "%s%s%s", l.before, warning, l.after);
    if (n < 0 || (size_t)n >= outSize)
    {
        out[0] = '\0';
        return FALSE;
    }
    return TRUE;
}

S32 iSaveTextLineCount()
{
    return kLineCount;
}

S32 iSaveTextLine(S32 index, char* out, size_t outSize, const char** asset, iSaveTextChoice* disc)
{
    if (index < 0 || index >= kLineCount || out == NULL || outSize == 0)
    {
        return FALSE;
    }
    const Line& l = kLines[index];
    snprintf(out, outSize, "%s%s%s", l.before, l.warning, l.after);
    if (asset != NULL)
    {
        *asset = l.asset;
    }
    if (disc != NULL)
    {
        *disc = l.disc;
    }
    return TRUE;
}
