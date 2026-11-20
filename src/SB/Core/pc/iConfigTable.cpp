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

// The [extras] default: the Xbox release's version everywhere but the 3DS, where
// each one waits until it has been timed on the console (docs/EXTRAS.md).
#ifdef __3DS__
#define TSSM_EXTRAS_DEFAULT "gamecube"
#else
#define TSSM_EXTRAS_DEFAULT "xbox"
#endif

const iConfigSetting kConfigSettings[] = {
    // TSSM-ADAPTED: treedome's table less the settings of BfBB features this
    // port did not import (toon, hipoly, world lighting, day/night, fonts,
    // cheats, fixes, Xbox screen passes, menus, soundtrack, load-screen and
    // shadow tuning, button glyphs) and video.profile/draw_distance. A key that
    // nothing reads would be written into every config.ini as if it did
    // something. src/SB/Core/pc/PORTING.md lists what was removed.
    { "assets", "path", "",
      "Folder containing boot.HIP, font.HIP and FMV/, from the Xbox or GameCube\n"
      "; disc (for a Dolphin extraction, its files/ folder or the one above it).\n"
      "; Empty: the folder the game starts in. TSSM_ASSETS overrides this.",
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
    // TSSM: the save screens' "Please don't turn off your Xbox console" (iSaveText.h).
    { "game", "save_text", "auto",
      "What the save screens ask you not to do: auto (this platform's own words),\n"
      "; xbox or gamecube (that disc's own line, whichever disc's files you play),\n"
      "; ps2, generic (no console named), pc, android, switch or 3ds.",
      ICONFIG_ENUM, "auto|xbox|ps2|gamecube|generic|pc|android|switch|3ds", kNone, kNone },
    { "video", "mode", "borderless",
#ifdef __SWITCH__
      "Not used on the Switch: the picture always fills the screen.",
#else
      "Window mode: borderless, fullscreen (exclusive) or windowed. Alt+Enter\n"
      "; switches between windowed and borderless.",
#endif
      ICONFIG_ENUM, "borderless|fullscreen|windowed", kNone, kNone },
#ifdef __3DS__
    // The 3DS draws the top screen, 400 x 240, at 30 frames a second
    // (docs/3DS.md).
    { "video", "aspect", "custom",
#else
    { "video", "aspect", "wide",
#endif
      "Picture shape: wide (the display's shape: more of the world at the sides,\n"
      "; menus and HUD in a centered 4:3 area), 4:3 (the original picture) or custom\n"
      "; (exactly width x height).",
      ICONFIG_ENUM, "wide|4:3|custom", kNone, kNone },
#ifdef __3DS__
    { "video", "width", "400",
#else
    { "video", "width", "640",
#endif
      "Render width in pixels. Used only with aspect = custom; otherwise it follows\n"
      "; from the height and the aspect.",
      ICONFIG_INT, NULL, 320.0f, 15360.0f },
#ifdef __3DS__
    { "video", "height", "240",
#else
    { "video", "height", "480",
#endif
      "Render height in pixels (lines). The picture is scaled to the window keeping\n"
      "; its shape.",
      ICONFIG_INT, NULL, 240.0f, 8640.0f },
#ifdef __3DS__
    { "video", "framerate", "30",
#else
    { "video", "framerate", "60",
#endif
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
#ifdef __3DS__
    { "video", "backend", "auto",
      "Renderer: auto or c3d (the 3DS's GPU), or null: draw nothing (for timing the\n"
      "; game's own work).",
      ICONFIG_ENUM, "auto|c3d|null", kNone, kNone },
#else
    { "video", "backend", "auto",
#ifdef __SWITCH__
      "Renderer: auto or gl3. The Switch build has gl3 only.",
#else
      "Renderer: auto, d3d9, d3d11, gl3 or vulkan. auto picks the best one in this\n"
      "; build. Only gl3 and vulkan run outside Windows.",
#endif
      ICONFIG_ENUM, "auto|d3d9|d3d11|gl3|vulkan", kNone, kNone },
#endif
    { "video", "pipeline", "auto",
#ifdef __SWITCH__
      "Direct3D 9 only; not used on the Switch.",
#else
      "Direct3D 9 rendering path: auto, shader or fixed. fixed supports pre-2002\n"
      "; GPUs but has no glow, distortion or per-pixel lighting.",
#endif
      ICONFIG_ENUM, "auto|shader|fixed", kNone, kNone },
    // TSSM: the console's screen passes, drawn by the port (src/SB/Core/pc/iHDRGlow.h,
    // iScreenWarp.h).
    { "video", "hdr_glow", "on",
      "The glow around bright and glowing things: on (the glow [extras] glow picks,\n"
      "; the Xbox's or the GameCube's) or off (no glow).",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "video", "screen_warp", "on",
      "The ripple of the picture around shockwaves, explosions and heat, as on the\n"
      "; GameCube. Off: none.",
      ICONFIG_BOOL, NULL, kNone, kNone },
#ifdef __3DS__
    { "video", "stereo", "auto",
      "Stereoscopic 3D on the top screen. auto: the 3D slider turns it on and sets\n"
      "; how deep, and where a busy place keeps losing frames in 3D it goes 2D for\n"
      "; a while by itself and comes back. always: the slider, and 3D stays on\n"
      "; even where it runs slowly. off: always 2D. The HUD, text and movies stay\n"
      "; at the screen. The settings menu (START + SELECT) changes it too.",
      ICONFIG_ENUM, "auto|always|off", kNone, kNone },
    { "video", "stereo_depth", "50",
      "How deep the 3D is at the slider's top, 0 to 100.",
      ICONFIG_INT, NULL, 0.0f, 100.0f },
#endif
    // TSSM: features of the Xbox release, each beside the GameCube release's own
    // (src/SB/Core/pc/iExtras.h, docs/EXTRAS.md).
    { "extras", "glow", TSSM_EXTRAS_DEFAULT,
      "The glow around bright things: xbox (the Xbox release's wide, soft glow,\n"
      "; stronger in some levels) or gamecube (the GameCube release's tighter,\n"
      "; fainter glow). video.hdr_glow = off turns either off.",
      ICONFIG_ENUM, "xbox|gamecube", kNone, kNone },
    { "extras", "spotlights", TSSM_EXTRAS_DEFAULT,
      "Spotlights: xbox (as the Xbox release: the moving spotlights of No Cheese!,\n"
      "; Bubble Blowing Baby Hunt and Turn the Tables on Plankton are on, and every\n"
      "; spotlight lights what it shines on and has a lamp flare) or gamecube (as\n"
      "; the GameCube release: those lights are off, and a spotlight is its cone\n"
      "; only). Applies when a level loads.",
      ICONFIG_ENUM, "xbox|gamecube", kNone, kNone },
    { "extras", "crowds", TSSM_EXTRAS_DEFAULT,
      "The fish crowds in No Cheese! and Sandwich Driving 101: xbox (they move) or\n"
      "; gamecube (they stand still, as on the GameCube). Applies when a level loads.",
      ICONFIG_ENUM, "xbox|gamecube", kNone, kNone },
    { "extras", "water", TSSM_EXTRAS_DEFAULT,
      "Water in the levels whose Xbox files have a water body (SC02's tanks and\n"
      "; B301's sea): xbox (the Xbox release's water: reflections, refraction and\n"
      "; moving ripples) or gamecube (the GameCube release's water). Without the\n"
      "; Xbox files, on Direct3D 9's fixed path or on Vulkan, gamecube is used.\n"
      "; Applies when a level loads. An older file's [video] water is read as this.",
      ICONFIG_ENUM, "xbox|gamecube", kNone, kNone },
    { "extras", "frame_lock", TSSM_EXTRAS_DEFAULT,
      "The driving levels, Bubble Blowing Baby Hunt and Google-Eyes and Smelly\n"
      "; Knick Knacks: xbox (at the frame-rate cap, like every other level) or\n"
      "; gamecube (held at 30 frames a second, as on the GameCube). In both, the\n"
      "; Patty Wagon drives as it does at 60 frames a second, as on the Xbox.",
      ICONFIG_ENUM, "xbox|gamecube", kNone, kNone },
    { "input", "controller", "auto",
      "Controller to use: auto (the first one found) or 1 to 4 for a specific slot.",
      ICONFIG_INT, "auto", 1.0f, 4.0f },
    { "input", "preset", "auto",
#ifdef __3DS__
      "What the controller is, for its default buttons and its button pictures:\n"
      "; auto (the 3DS's own: the Xbox layout by position, B jumps), xbox,\n"
      "; playstation, switch (found by letter: A jumps), gamecube or 3ds. Lines in\n"
      "; [pad] override it.",
      ICONFIG_ENUM, "auto|xbox|playstation|switch|gamecube|3ds", kNone, kNone },
#else
      "What the controller is, for its default buttons and its button pictures:\n"
      "; auto (whatever is plugged in), xbox, playstation, switch or gamecube\n"
      "; (Z is Back, Z+L white, Z+R black). Lines in [pad] override it.",
      ICONFIG_ENUM, "auto|xbox|playstation|switch|gamecube", kNone, kNone },
#endif
    { "input", "layout", "auto",
#if defined(__SWITCH__) || defined(__3DS__)
      // The Switch and the 3DS name the buttons by the letter printed where
      // the Xbox's would be (docs/SWITCH.md and docs/3DS.md, Controls).
      "Which face button the moves use. xbox: Karate Spin on Y, SpongeBowl on A,\n"
      "; as the Xbox release. gamecube: Karate Spin on A, SpongeBowl on Y, as the\n"
      "; GameCube release. Menus are the same in both (B accept, A back). auto:\n"
      "; xbox. The game's button prompts follow. [pad] names the buttons the\n"
      "; Xbox's way: a is the bottom button (B), b the right one (A), x the left\n"
      "; one (Y), y the top one (X).",
#else
      "Which face button the moves use. xbox: Karate Spin on X, SpongeBowl on B,\n"
      "; as the Xbox release. gamecube: Karate Spin on B, SpongeBowl on X, as the\n"
      "; GameCube release. Menus are the same in both (A accept, B back). auto:\n"
      "; gamecube with a GameCube controller, xbox otherwise. The game's button\n"
      "; prompts follow.",
#endif
      ICONFIG_ENUM, "auto|xbox|gamecube", kNone, kNone },
    { "input", "button_icons", "auto",
#ifdef __3DS__
      "Button pictures in the game's prompts: auto (the 3DS's letters), xbox,\n"
      "; playstation, switch, gamecube, 3ds, keyboard, or off (the disc's own).\n"
      "; They show what each button is bound to.",
      ICONFIG_ENUM, "auto|xbox|playstation|switch|gamecube|3ds|keyboard|off", kNone, kNone },
#else
      "Button pictures in the game's prompts: auto (the device you last used),\n"
      "; xbox, playstation, switch, gamecube, keyboard, or off (the disc's own).\n"
      "; They show what each button is bound to.",
      ICONFIG_ENUM, "auto|xbox|playstation|switch|gamecube|keyboard|off", kNone, kNone },
#endif
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
#ifdef __SWITCH__
      "How far ZL and ZR must be pressed, as a percentage: auto (30) or 5 to 95.\n"
      "; The Switch's ZL and ZR only click, so every value works the same there.",
#else
      "How far LT and RT must be pulled to press, as a percentage: auto (30, where\n"
      "; the GameCube code clicks) or 5 to 95.",
#endif
      ICONFIG_INT, "auto", 5.0f, 95.0f },
    { "input", "camera_sensitivity", "1.0",
#ifdef __3DS__
      "C-Stick camera speed. 1.0 is the original speed.",
#else
      "Right-stick camera speed. 1.0 is the original speed.",
#endif
      ICONFIG_FLOAT, NULL, 0.1f, 5.0f },
    { "input", "camera_invert_x", "off",
#ifdef __3DS__
      "Turn the camera the other way left and right.",
#else
      "Turn the camera the other way left and right (the right stick and the\n"
      "; camera keys; the mouse has mouse_invert_x).",
#endif
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "input", "camera_invert_y", "off",
#ifdef __3DS__
      "Turn the camera the other way up and down.",
#else
      "Turn the camera the other way up and down (the right stick and the\n"
      "; camera keys; the mouse has mouse_invert_y).",
#endif
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "input", "mouse_look", "on",
      "Mouse look: moving the mouse turns the camera, which stays where it is\n"
      "; pointed. The game holds the mouse while you play and lets it go when\n"
      "; Escape is pressed, in menus, in the settings window, when the window\n"
      "; loses focus and on mouse_release_key. It takes it back when play goes\n"
      "; on after a menu, or on a click in the window. (Was mouse_camera.)",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "input", "mouse_sensitivity_x", "1.0",
      "Mouse look speed left and right. 1.0 is the default. (Was\n"
      "; mouse_sensitivity.)",
      ICONFIG_FLOAT, NULL, 0.05f, 20.0f },
    { "input", "mouse_sensitivity_y", "same",
      "Mouse look speed up and down: same (as left and right) or a number.",
      ICONFIG_FLOAT, "same", 0.05f, 20.0f },
    { "input", "mouse_invert_x", "off",
      "Mouse look left and right the other way. Off: mouse right looks right.\n"
      "; (A file from before this setting starts it from camera_invert_x.)",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "input", "mouse_invert_y", "off",
      "Mouse look up and down the other way. Off: mouse forward looks up.\n"
      "; (A file from before this setting starts it from camera_invert_y.)",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "input", "mouse_raw", "on",
      "Read the mouse's own movement, without the desktop's pointer speed and\n"
      "; acceleration. Off: with them.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "input", "mouse_smoothing", "0",
      "Smooths mouse look over this many milliseconds. 0 is none: the camera\n"
      "; turns in the frame the mouse moved.",
      ICONFIG_INT, NULL, 0.0f, 200.0f },
    { "input", "mouse_release_key", "f2",
      "The key that frees the mouse pointer while you play (to drag the window,\n"
      "; say) and takes it back: f2 to f7, f9, f10, f12, tilde, or none. Escape\n"
      "; always frees it as well.",
      ICONFIG_ENUM, "f2|f3|f4|f5|f6|f7|f9|f10|f12|tilde|none", kNone, kNone },
    { "input", "mouse_menus", "on",
      "The mouse pointer in the game's own menus: pointing at an item highlights\n"
      "; it, a click accepts it, a right click goes back, the wheel moves through\n"
      "; a list. Off: the mouse is still let go in menus, and its buttons are the\n"
      "; ones they are bound to.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "input", "rumble", "100",
      "Rumble strength in percent; 0 turns it off. The game's own vibration\n"
      "; option still applies.",
      ICONFIG_INT, NULL, 0.0f, 100.0f },
    { "input", "touch_controls", "auto",
#ifdef __SWITCH__
      "On-screen touch controls: auto (off on the Switch), on or off.",
#else
      "On-screen touch controls: auto (on for Android), on or off.",
#endif
      ICONFIG_ENUM, "auto|on|off", kNone, kNone },
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
#if defined(__3DS__)
      "Not used on the 3DS: the game's files are always in /3ds/tssm/files.",
#elif defined(__SWITCH__)
      "Launcher window at startup, to choose the game's folder and settings: auto\n"
      "; (only when the game's files are not found) or never. always is auto on the\n"
      "; Switch.",
#else
      "Launcher window at startup, to choose the game's folder and settings: auto\n"
      "; (only when the game's files are not found), always or never.",
#endif
      ICONFIG_ENUM, "auto|always|never", kNone, kNone },
    { "gui", "key", "F1",
#if defined(__3DS__)
      "Not used on the 3DS: START + SELECT open the settings overlay.",
#elif defined(__SWITCH__)
      "Key on a USB keyboard that opens the settings overlay in the game.",
#else
      "Key that opens the settings overlay in the game.",
#endif
      ICONFIG_STRING, NULL, kNone, kNone },
    { "gui", "pad_combo", "on",
#if defined(__3DS__)
      "Not used on the 3DS: START + SELECT always open the settings overlay.",
#elif defined(__SWITCH__)
      "Not used on the Switch: - and + (hold -, press +) always open the settings\n"
      "; overlay.",
#else
      "Back+Start on a controller (hold Back, press Start) opens the settings overlay.",
#endif
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "gui", "scale", "auto",
      "Size of the overlay and launcher: auto (from the display's scaling and the\n"
      "; window's height) or a factor from 0.5 to 4.",
      ICONFIG_FLOAT, "auto", 0.5f, 4.0f },
    { "gui", "file_picker", "auto",
      "File and folder picker for Import, Export and Browse: auto (the system's\n"
      "; dialog where there is one, the built-in one where there is not, as on the\n"
      "; Switch), system or builtin. TSSM_FILE_PICKER overrides it.",
      ICONFIG_ENUM, "auto|system|builtin", kNone, kNone },
#ifdef __3DS__
    { "gui", "overlay", "on",
      "The settings overlay on the bottom screen (START + SELECT). off leaves the\n"
      "; bottom screen dark and the two buttons to the game.",
      ICONFIG_BOOL, NULL, kNone, kNone },
    { "gui", "bottom_hint_seconds", "10",
      "Seconds the bottom screen says START + SELECT: menu after the game starts\n"
      "; (and after a tap on it) before it fades to black. -1 keeps it, 0 never\n"
      "; shows it.",
      ICONFIG_INT, NULL, -1.0f, 3600.0f },
    { "3ds", "effects", "all",
      "The drawing effects: all (the default), none, or any of fog, rtt (pictures\n"
      "; drawn into textures), shadow, glow, warp, copy (screen copies: the\n"
      "; loading-screen picture), orthoz (the menus' 3D models' depth), separated\n"
      "; by commas. shadow and glow turn on rtt, glow and warp turn on copy.\n"
      "; selftest (a GPU test at start) runs only when named. none is the fastest.",
      ICONFIG_STRING, NULL, kNone, kNone },
#endif
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
