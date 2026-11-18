// The settings table. What it is for is in iConfigTable.h.

#include "iConfigTable.h"

#include "iHost.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Every setting the port has, its default, and what it is for.
//
// This one table does four jobs, and that it does all four is the reason it
// exists rather than four lists that could disagree:
//
//   1. A key not in it is REPORTED at load rather than ignored. The accessors
//      all take a fallback, so an unknown key is otherwise indistinguishable
//      from an absent one at the point of use -- which means `glwo = off`
//      would read as "the glow is on" and nothing anywhere would say why.
//   2. It is the default. An accessor whose key is missing from the file
//      answers from here, NOT from the fallback its caller passed, so a
//      default cannot be changed in one place and not the other.
//   3. It is what gets written when there is no config.ini, comments and all,
//      so the generated file documents itself.
//   4. It is the domain: what a value may be, which is what the configurator
//      draws a control from and checks a typed value against.
//
// `section` is written as a [header] when it changes, so keep entries that
// share one together.
//
// The last three columns are the domain -- kind, the words the value may be
// besides a kind value, and the numeric range. `kNone` is the range for a
// setting that has no range worth offering.
namespace
{
    const F32 kNone = 0.0f;
}

// The default render size. 480 lines is the original's. A phone's panel is two to
// three times as tall as that, and 480 lines scaled up to it look soft; 720 is
// sharper. With the default aspect (wide) the width follows from the screen's
// shape -- 1600x720 on a 20:9 phone -- and the width here is used only by
// aspect = custom, where 960 keeps it 4:3.
#ifdef __ANDROID__
#define ICONFIG_DEFAULT_WIDTH "960"
#define ICONFIG_DEFAULT_HEIGHT "720"
#else
#define ICONFIG_DEFAULT_WIDTH "640"
#define ICONFIG_DEFAULT_HEIGHT "480"
#endif

const iConfigSetting kConfigSettings[] = {
    // TSSM-ADAPTED: treedome's table less the settings of BfBB features this
    // port did not import (toon, hipoly, world lighting, day/night, fonts,
    // cheats, fixes, Xbox screen passes, menus, soundtrack, load-screen and
    // shadow tuning, button glyphs) and video.profile/draw_distance. A key that
    // nothing reads would be written into every config.ini as if it did
    // something. src/SB/Core/pc/PORTING.md lists what was removed.
    { "assets", "path", "",
      "Folder containing boot.HIP, font.HIP and FMV/. Empty: the folder the game\n"
      "; starts in. TSSM_ASSETS overrides this.",
      ICONFIG_FOLDER, NULL, kNone, kNone },
    { "assets", "mod", "",
      "Mod folder with the same layout as the asset folder. Its files replace the\n"
      "; originals. Empty: none. TSSM_MOD overrides this.",
      ICONFIG_FOLDER, NULL, kNone, kNone },
    // TSSM: the mods/ folder (src/SB/Core/pc/iMod.h, docs/MODDING.md).
    { "assets", "mods", "on",
      "Apply the mods in the mods folder (one folder per mod; mods.ini there holds\n"
      "; the load order). Off: play the unmodified game. TSSM_MODS overrides this.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "assets", "mods_folder", "",
      "Folder holding the mods. Empty: mods/ beside the game, else mods/ in the\n"
      "; per-user data folder. TSSM_MODS_DIR overrides this.",
      ICONFIG_FOLDER, NULL, kNone, kNone },
    { "game", "boot", "",
      "Start in this scene instead of the menu: a four-character scene ID, e.g.\n"
      "; BB01. Empty: start at the menu. Overrides SB.INI's BOOT=.",
      ICONFIG_STRING, NULL, kNone, kNone },
    { "game", "intro_movies", "on",
      "Play the logo movies at startup.", ICONFIG_BOOL, NULL,
      kNone, kNone },
    { "game", "save_folder", "",
      "Folder for saves. Empty: the per-user data folder. TSSM_SAVE_DIR overrides\n"
      "; this.",
      ICONFIG_FOLDER, NULL, kNone, kNone },
    { "video", "mode", "borderless",
      "Window mode: borderless, fullscreen (exclusive) or windowed. Alt+Enter\n"
      "; switches between windowed and borderless.",
      ICONFIG_ENUM, "borderless|fullscreen|windowed", kNone, kNone },
    { "video", "aspect", "wide",
      "Picture shape: wide (the display's shape: more of the world at the sides,\n"
      "; menus and HUD in a centered 4:3 area), 4:3 (the original picture) or custom\n"
      "; (exactly width x height).",
      ICONFIG_ENUM, "wide|4:3|custom", kNone, kNone },
    { "video", "width", ICONFIG_DEFAULT_WIDTH,
      "Render width in pixels. Used only with aspect = custom; otherwise it follows\n"
      "; from the height and the aspect.",
      ICONFIG_INT, NULL, 320.0f, 15360.0f },
    { "video", "height", ICONFIG_DEFAULT_HEIGHT,
      "Render height in pixels (lines). The picture is scaled to the window keeping\n"
      "; its shape.",
      ICONFIG_INT, NULL, 240.0f, 8640.0f },
    { "video", "framerate", "60",
      "Frame rate cap: a number, display (the monitor's refresh rate), or 0 or off\n"
      "; for no cap. Above 60 the work per second, and a phone's heat, go up with\n"
      "; the rate. Movies play at 60 at most. Below about 45 the SpongeBall keeps\n"
      "; its full speed (a change from the original game, which slows it).",
      ICONFIG_INT, "display|off", 0.0f, 1000.0f },
    { "video", "vsync", "on",
      "Wait for the display's refresh before showing a frame. Prevents tearing.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "video", "msaa", "1",
      "Anti-aliasing samples per pixel: 1 (off), 2, 4 or 8. Falls back to off if\n"
      "; the GPU doesn't support the value.",
      ICONFIG_ENUM, "1|2|4|8", kNone, kNone },
    { "video", "fov", "75",
      "Horizontal field of view in degrees at 4:3. Wider screens show more at the\n"
      "; sides.",
      ICONFIG_FLOAT, NULL, 30.0f, 140.0f },
    { "video", "backend", "auto",
      "Renderer: auto, d3d9, d3d11, gl3 or vulkan. auto picks the best one in this\n"
      "; build. Only gl3 and vulkan run outside Windows.",
      ICONFIG_ENUM, "auto|d3d9|d3d11|gl3|vulkan", kNone, kNone },
    { "video", "pipeline", "auto",
      "Direct3D 9 rendering path: auto, shader or fixed. fixed supports pre-2002\n"
      "; GPUs but has no glow, distortion or per-pixel lighting.",
      ICONFIG_ENUM, "auto|shader|fixed", kNone, kNone },
    // TSSM: the console's screen passes, drawn by the port (src/SB/Core/pc/iHDRGlow.h,
    // iScreenWarp.h).
    { "video", "hdr_glow", "on",
      "The glow around bright and glowing things, as on the GameCube. Off: no\n"
      "; glow.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "video", "screen_warp", "on",
      "The ripple of the picture around shockwaves, explosions and heat, as on the\n"
      "; GameCube. Off: none.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "input", "controller", "auto",
      "Controller to use: auto (the first one found) or 1 to 4 for a specific slot.",
      ICONFIG_INT, "auto", 1.0f, 4.0f },
    { "input", "preset", "auto",
      "What the controller is, for its default buttons and its button pictures:\n"
      "; auto (whatever is plugged in), xbox, playstation, switch or gamecube\n"
      "; (Z is Back, Z+L white, Z+R black). Lines in [pad] override it.",
      ICONFIG_ENUM, "auto|xbox|playstation|switch|gamecube", kNone, kNone },
    { "input", "layout", "auto",
      "Which face button the moves use. xbox: Karate Spin on X, SpongeBowl on B,\n"
      "; as the Xbox release. gamecube: Karate Spin on B, SpongeBowl on X, as the\n"
      "; GameCube release. Menus are the same in both (A accept, B back). auto:\n"
      "; gamecube with a GameCube controller, xbox otherwise. The game's button\n"
      "; prompts follow.",
      ICONFIG_ENUM, "auto|xbox|gamecube", kNone, kNone },
    { "input", "button_icons", "auto",
      "Button pictures in the game's prompts: auto (the device you last used),\n"
      "; xbox, playstation, switch, gamecube, keyboard, touch (the on-screen\n"
      "; buttons), or off (the disc's own). They show what each button is bound to.",
      ICONFIG_ENUM, "auto|xbox|playstation|switch|gamecube|keyboard|touch|off", kNone, kNone },
    { "input", "keyboard", "always",
      "Keyboard and mouse: always (play alongside a controller), fallback (only\n"
      "; with no controller) or off.",
      ICONFIG_ENUM, "always|fallback|off", kNone, kNone },
    { "input", "stick_curve", "xbox",
      "How far a stick must go: xbox (the Xbox release: nothing below 26%, full\n"
      "; speed at 70%), gamecube (the GameCube's octagon: full speed a little past\n"
      "; half way), or linear (full speed only at the rim).",
      ICONFIG_ENUM, "xbox|gamecube|linear", kNone, kNone },
    { "input", "deadzone", "auto",
      "Stick deadzone as a percentage of full movement: auto (the stick curve's\n"
      "; own) or 0 to 90.",
      ICONFIG_INT, "auto", 0.0f, 90.0f },
    { "input", "trigger_threshold", "auto",
      "How far LT and RT must be pulled to press, as a percentage: auto (30, where\n"
      "; the GameCube code clicks) or 5 to 95.",
      ICONFIG_INT, "auto", 5.0f, 95.0f },
    { "input", "camera_sensitivity", "1.0",
      "Right-stick camera speed. 1.0 is the original speed.",
      ICONFIG_FLOAT, NULL, 0.1f, 5.0f },
    { "input", "camera_invert_x", "off",
      "Turn the camera the other way left and right (stick and mouse).",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "input", "camera_invert_y", "off",
      "Turn the camera the other way up and down (stick and mouse).",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "input", "mouse_camera", "on",
      "Turn the camera with the mouse while the game window has focus.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "input", "mouse_sensitivity", "1.0",
      "Mouse camera speed. 1.0 is the default.",
      ICONFIG_FLOAT, NULL, 0.05f, 20.0f },
    { "input", "rumble", "100",
      "Rumble strength in percent; 0 turns it off. The game's own vibration\n"
      "; option still applies.",
      ICONFIG_INT, NULL, 0.0f, 100.0f },
    { "input", "touch_controls", "auto",
      "On-screen touch controls: auto (on for Android), on or off.",
      ICONFIG_ENUM, "auto|on|off", kNone, kNone },
    { "input", "touch_drag_look", "off",
      "Touch controls: a finger dragged on the empty right half of the screen also\n"
      "; turns the camera, as the mouse does (the right stick is there either way).",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "input", "touch_scale", "1.0",
      "Size of the touch controls, 0.5 to 1.2. 1.0 is the default.",
      ICONFIG_FLOAT, NULL, 0.5f, 1.2f },
    { "input", "touch_opacity", "100",
      "How solid the touch controls are drawn, in percent: 10 to 100.",
      ICONFIG_INT, NULL, 10.0f, 100.0f },
    { "input", "test", "off",
      "Input test: log prints what the game reads from the controller every time\n"
      "; it changes; overlay also draws it on screen (OpenGL and Vulkan).\n"
      "; TSSM_INPUT_TEST=1 or =overlay does the same.",
      ICONFIG_ENUM, "off|log|overlay", kNone, kNone },
    { "audio", "master_volume", "100",
      "Overall volume, 0 to 100. The game's own music and effects settings apply\n"
      "; on top of the four volumes here.",
      ICONFIG_INT, NULL, 0.0f, 100.0f },
    { "audio", "music_volume", "100", "Music volume, 0 to 100.", ICONFIG_INT, NULL, 0.0f,
      100.0f },
    { "audio", "sfx_volume", "100", "Sound effects and voices volume, 0 to 100.", ICONFIG_INT,
      NULL, 0.0f, 100.0f },
    { "audio", "movie_volume", "100", "Movie and cutscene volume, 0 to 100.", ICONFIG_INT, NULL,
      0.0f, 100.0f },
    { "gui", "launcher", "auto",
      "Launcher window at startup, to choose the game's folder and settings: auto\n"
      "; (only when the game's files are not found), always or never.",
      ICONFIG_ENUM, "auto|always|never", kNone, kNone },
    { "gui", "key", "F1",
      "Key that opens the settings overlay in the game.", ICONFIG_STRING, NULL, kNone,
      kNone },
    { "gui", "pad_combo", "on",
      "Back+Start on a controller (hold Back, press Start) opens the settings overlay.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "gui", "scale", "auto",
      "Size of the overlay and launcher: auto (from the display's scaling and the\n"
      "; window's height) or a factor from 0.5 to 4.",
      ICONFIG_FLOAT, "auto", 0.5f, 4.0f },
#ifdef TSSM_UPDATE
    // TSSM: in-app updates (iUpdate.h), only in builds that have them.
    { "update", "check_at_start", "off",
      "Check for a newer release when the game starts. It connects to GitHub only.\n"
      "; Off: only the Check for updates button in the settings window does.",
      ICONFIG_BOOL, NULL, kNone, kNone },
#endif
};

const S32 kConfigSettingCount = (S32)(sizeof(kConfigSettings) / sizeof(kConfigSettings[0]));

const iConfigSetting* iConfigTableFind(const char* key)
{
    for (S32 i = 0; i < kConfigSettingCount; i++)
    {
        char full[128];
        snprintf(full, sizeof(full), "%s.%s", kConfigSettings[i].section, kConfigSettings[i].name);
        if (iHostStrCaseCmp(full, key) == 0)
        {
            return &kConfigSettings[i];
        }
    }
    return NULL;
}

namespace
{
    // Whether `value` is one of the '|'-separated words in `choices`.
    bool isChoice(const char* choices, const char* value)
    {
        if (choices == NULL)
        {
            return false;
        }

        const char* p = choices;
        while (*p != '\0')
        {
            const char* bar = strchr(p, '|');
            size_t n = (bar != NULL) ? (size_t)(bar - p) : strlen(p);

            char word[64];
            if (n < sizeof(word))
            {
                memcpy(word, p, n);
                word[n] = '\0';
                if (iHostStrCaseCmp(word, value) == 0)
                {
                    return true;
                }
            }

            if (bar == NULL)
            {
                break;
            }
            p = bar + 1;
        }

        return false;
    }

    bool isBool(const char* value)
    {
        static const char* const kWords[] = { "1", "true", "yes", "on", "0", "false", "no", "off" };
        for (size_t i = 0; i < sizeof(kWords) / sizeof(kWords[0]); i++)
        {
            if (iHostStrCaseCmp(value, kWords[i]) == 0)
            {
                return true;
            }
        }
        return false;
    }

    // The value as a number, or false when it is not one at all. Whole-number
    // strictness is the caller's, because "1.5" is a number and is still not a
    // frame count.
    bool asNumber(const char* value, double* out)
    {
        char* end = NULL;
        double parsed = strtod(value, &end);
        if (end == value || end == NULL || *end != '\0')
        {
            return false;
        }
        *out = parsed;
        return true;
    }

    void say(char* why, size_t whySize, const char* text)
    {
        if (why != NULL && whySize > 0)
        {
            snprintf(why, whySize, "%s", text);
        }
    }
} // namespace

bool iConfigTableValidate(const iConfigSetting* setting, const char* value, char* why,
                          size_t whySize)
{
    if (setting == NULL || value == NULL)
    {
        return false;
    }

    if (isChoice(setting->choices, value))
    {
        return true;
    }

    switch (setting->kind)
    {
    case ICONFIG_BOOL:
        if (!isBool(value))
        {
            say(why, whySize, "Must be on or off.");
            return false;
        }
        return true;

    case ICONFIG_ENUM:
        // isChoice above was the whole test, so reaching here is a failure.
        {
            char text[256];
            snprintf(text, sizeof(text), "Must be one of: %s", setting->choices);
            for (char* p = text; *p != '\0'; p++)
            {
                if (*p == '|')
                {
                    *p = ' ';
                }
            }
            say(why, whySize, text);
        }
        return false;

    case ICONFIG_INT:
    case ICONFIG_FLOAT:
    {
        double n = 0.0;
        if (!asNumber(value, &n))
        {
            char text[256];
            if (setting->choices != NULL)
            {
                snprintf(text, sizeof(text), "Must be a number or one of: %s", setting->choices);
                for (char* p = text; *p != '\0'; p++)
                {
                    if (*p == '|')
                    {
                        *p = ' ';
                    }
                }
            }
            else
            {
                snprintf(text, sizeof(text), "Must be a number.");
            }
            say(why, whySize, text);
            return false;
        }

        if (setting->kind == ICONFIG_INT && n != (double)(long)n)
        {
            say(why, whySize, "Must be a whole number.");
            return false;
        }

        if (setting->min != setting->max && (n < setting->min || n > setting->max))
        {
            char text[128];
            snprintf(text, sizeof(text), "Must be between %g and %g.", setting->min,
                     setting->max);
            say(why, whySize, text);
            return false;
        }

        return true;
    }

    case ICONFIG_STRING:
    case ICONFIG_FOLDER:
    case ICONFIG_FONT:
        // Anything, including empty. Whether the path is there is a question
        // for whoever is about to open it, not for the table -- a config
        // written on one machine and copied to another is ordinary.
        return true;
    }

    return true;
}

void iConfigTableWriteDefaults(FILE* f)
{
    const char* section = NULL;
    for (S32 i = 0; i < kConfigSettingCount; i++)
    {
        if (section == NULL || strcmp(section, kConfigSettings[i].section) != 0)
        {
            section = kConfigSettings[i].section;
            fprintf(f, "\n[%s]\n", section);
        }

        fprintf(f, "\n; %s\n", kConfigSettings[i].comment);
        fprintf(f, "%s = %s\n", kConfigSettings[i].name, kConfigSettings[i].value);
    }
}

void iConfigTableSummary(const iConfigSetting* setting, char* out, size_t outSize)
{
    if (out == NULL || outSize == 0)
    {
        return;
    }
    out[0] = '\0';
    if (setting == NULL || setting->comment == NULL)
    {
        return;
    }

    size_t written = 0;
    const char* p = setting->comment;

    while (*p != '\0')
    {
        const char* nl = strchr(p, '\n');
        size_t n = (nl != NULL) ? (size_t)(nl - p) : strlen(p);

        // Continuation lines carry the writer's "; " and are joined on with a
        // space. A line that is only "; " ends the summary: it is the blank
        // line before the paragraphs that belong in the file.
        if (p != setting->comment)
        {
            const char* text = p;
            size_t left = n;
            if (left >= 1 && text[0] == ';')
            {
                text++;
                left--;
            }
            while (left > 0 && (*text == ' ' || *text == '\t'))
            {
                text++;
                left--;
            }
            if (left == 0)
            {
                break;
            }

            if (written + 1 < outSize)
            {
                out[written++] = ' ';
            }
            p = text;
            n = left;
        }

        for (size_t i = 0; i < n && written + 1 < outSize; i++)
        {
            out[written++] = p[i];
        }

        if (nl == NULL)
        {
            break;
        }
        p = nl + 1;
    }

    out[written < outSize ? written : outSize - 1] = '\0';
}
