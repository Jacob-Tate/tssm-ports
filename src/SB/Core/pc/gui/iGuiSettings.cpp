// The Settings tab, and the parts of it the launcher shows.
//
// Every control reads and writes config.ini through the port's config layer
// (iConfig.h): iConfigGet* for what is in force, iConfigSet and iConfigSave to
// change it. iConfigSave rewrites only the lines that changed and keeps the
// file's comments and layout, so a setting changed here and one typed into the
// file by hand are the same thing.
//
// A setting that the running game can take on the spot is applied on the spot,
// with the same call iSystem.cpp's ApplyConfig makes at startup. The rest --
// the renderer, the render size, anti-aliasing, the game's folder -- are fixed
// when the device opens; those are saved, tagged "restart", and take effect the
// next time the game starts. In the launcher nothing is applied at all: there
// is no window or device yet, and everything takes effect when it closes.

#include "imgui.h"

#include <SDL3/SDL.h>

#include "gui/iGuiInternal.h"

#include "iConfig.h"
#include "iExtras.h"
#include "iHDRGlow.h"
#include "iHost.h"
#include "iSaveText.h"
#include "iScreen.h"
#include "iScreenWarp.h"
#include "iWindow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    char sSaveError[512];

    // The settings that only apply at startup, as they were when the overlay
    // came up.
    struct StartValue
    {
        const char* key;
        char value[512];
    };

    StartValue sStart[] = {
        { "assets.path", "" },          { "game.save_folder", "" },  { "video.backend", "" },
        { "video.width", "" },          { "video.height", "" },      { "video.msaa", "" },
        { "video.aspect", "" },         { "video.pipeline", "" },    { "video.mode", "" },
        { "input.touch_controls", "" }, { "game.intro_movies", "" },
    };

    const S32 kStartCount = (S32)(sizeof(sStart) / sizeof(sStart[0]));

    struct Choice
    {
        const char* value;
        const char* label;
    };

    // --- layout: a label column and a control column -------------------------

    bool BeginRows(const char* id)
    {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
        {
            return false;
        }
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFontSize() * 11.0f);
        ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    void Row(const char* label)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
    }

    // A control that leaves room for the "restart" tag beside it.
    void ControlWidth()
    {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    }

    // --- controls bound to a setting ------------------------------------------

    bool ChoiceCombo(const char* id, const char* key, const Choice* choices, S32 count,
                     const char* def, ImGuiComboFlags comboFlags = 0)
    {
        const char* current = iConfigGetString(key, def);
        S32 selected = -1;
        for (S32 i = 0; i < count; i++)
        {
            if (SDL_strcasecmp(current, choices[i].value) == 0)
            {
                selected = i;
            }
        }

        bool changed = false;
        ControlWidth();
        if (ImGui::BeginCombo(id, selected >= 0 ? choices[selected].label : current, comboFlags))
        {
            for (S32 i = 0; i < count; i++)
            {
                const bool isSelected = i == selected;
                if (ImGui::Selectable(choices[i].label, isSelected) && !isSelected)
                {
                    iGui::Commit(key, choices[i].value);
                    changed = true;
                }
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    bool BoolSetting(const char* id, const char* key, bool def)
    {
        bool on = iConfigGetBool(key, def ? TRUE : FALSE) != 0;
        if (ImGui::Checkbox(id, &on))
        {
            iGui::Commit(key, on ? "on" : "off");
            return true;
        }
        return false;
    }

    // Saved when the slider is let go, not on every frame of the drag.
    bool IntSlider(const char* id, const char* key, int def, int lo, int hi, const char* format)
    {
        int value = iConfigGetInt(key, def);
        ControlWidth();
        const bool changed =
            ImGui::SliderInt(id, &value, lo, hi, format, ImGuiSliderFlags_AlwaysClamp);
        if (changed)
        {
            iGui::CommitInt(key, value, false);
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            iGui::CommitInt(key, value, true);
        }
        return changed;
    }

    bool FloatSlider(const char* id, const char* key, float def, float lo, float hi,
                     const char* format, float* out)
    {
        float value = iConfigGetFloat(key, def);
        ControlWidth();
        const bool changed =
            ImGui::SliderFloat(id, &value, lo, hi, format, ImGuiSliderFlags_AlwaysClamp);
        if (changed)
        {
            iGui::CommitFloat(key, value, false);
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            iGui::CommitFloat(key, value, true);
        }
        *out = value;
        return changed;
    }

    // --- the frame rate setting, as iSystem.cpp's ApplyDisplayRateConfig reads it

    S32 ResolveFrameRate(const char* rate)
    {
        if (SDL_strcasecmp(rate, "display") == 0)
        {
            return iWindowGetDisplayRefreshRate();
        }
        if (SDL_strcasecmp(rate, "off") == 0 || SDL_strcasecmp(rate, "unlimited") == 0 ||
            SDL_strcasecmp(rate, "none") == 0)
        {
            return 0;
        }
        S32 fps = atoi(rate);
        return fps < 0 ? 60 : fps;
    }

    // --- folder rows ---------------------------------------------------------

    // A path, a Browse button and, when `clearLabel` is given, a button that
    // empties the setting. The picked folder is committed when it arrives.
    void FolderRow(const char* tag, const char* key, const char* emptyText, const char* clearLabel)
    {
        const char* value = iConfigGetString(key, "");

        char picked[1024];
        if (iGui::PollFolder(tag, picked, sizeof(picked)))
        {
            iGui::Commit(key, picked);
            value = iConfigGetString(key, "");
        }

        ImGui::PushID(tag);
        const float buttons = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2 +
                              (clearLabel != NULL ? ImGui::CalcTextSize(clearLabel).x +
                                                        ImGui::GetStyle().FramePadding.x * 2 +
                                                        ImGui::GetStyle().ItemSpacing.x :
                                                    0.0f) +
                              ImGui::GetStyle().ItemSpacing.x;

        char shown[1024];
        snprintf(shown, sizeof(shown), "%s", value[0] != '\0' ? value : emptyText);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttons);
        ImGui::BeginDisabled(value[0] == '\0');
        ImGui::InputText("##path", shown, sizeof(shown), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
        if (value[0] != '\0' && ImGui::BeginItemTooltip())
        {
            ImGui::TextUnformatted(value);
            ImGui::EndTooltip();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(iGui::PickingFolder());
        if (ImGui::Button("Browse..."))
        {
            iGui::PickFolder(tag, value);
        }
        ImGui::EndDisabled();

        if (clearLabel != NULL)
        {
            ImGui::SameLine();
            ImGui::BeginDisabled(value[0] == '\0');
            if (ImGui::Button(clearLabel))
            {
                iGui::Commit(key, "");
            }
            ImGui::EndDisabled();
        }
        ImGui::PopID();
    }
} // namespace

// --- committing ---------------------------------------------------------------------

void iGui::Commit(const char* key, const char* value, bool save)
{
    iConfigSet(key, value);
    if (!save)
    {
        return;
    }

    if (iConfigSave())
    {
        sSaveError[0] = '\0';
    }
    else
    {
        const char* path = iConfigPath();
        snprintf(sSaveError, sizeof(sSaveError),
                 "Could not write the settings to %s. Changes apply to this session only.",
                 path != NULL ? path : "config.ini (there is none: its folder is not writable)");
    }
}

void iGui::CommitInt(const char* key, int value, bool save)
{
    char text[32];
    snprintf(text, sizeof(text), "%d", value);
    Commit(key, text, save);
}

void iGui::CommitFloat(const char* key, float value, bool save)
{
    char text[32];
    snprintf(text, sizeof(text), "%.2f", value);
    Commit(key, text, save);
}

void iGui::Unset(const char* key)
{
    // Out of the file altogether, so the setting follows its default (for a
    // binding, the preset) instead of being pinned to today's value of it.
    iConfigUnset(key);
    SaveNow();
}

void iGui::SaveNow()
{
    if (iConfigSave())
    {
        sSaveError[0] = '\0';
    }
    else
    {
        snprintf(sSaveError, sizeof(sSaveError),
                 "Could not write the settings file. Changes apply to this session only.");
    }
}

const char* iGui::SaveError()
{
    return sSaveError[0] != '\0' ? sSaveError : NULL;
}

void iGui::SnapshotStartup()
{
    for (S32 i = 0; i < kStartCount; i++)
    {
        snprintf(sStart[i].value, sizeof(sStart[i].value), "%s", iConfigGetString(sStart[i].key, ""));
    }
}

bool iGui::ChangedSinceStartup(const char* key)
{
    for (S32 i = 0; i < kStartCount; i++)
    {
        if (strcmp(sStart[i].key, key) != 0)
        {
            continue;
        }

        const char* now = iConfigGetString(key, "");

        // Windowed and borderless switch live; only a move into or out of
        // exclusive fullscreen waits.
        if (strcmp(key, "video.mode") == 0)
        {
            return (SDL_strcasecmp(sStart[i].value, "fullscreen") == 0) !=
                   (SDL_strcasecmp(now, "fullscreen") == 0);
        }
        return SDL_strcasecmp(sStart[i].value, now) != 0;
    }
    return false;
}

bool iGui::AnyRestartPending()
{
    if (InLauncher())
    {
        return false;
    }
    for (S32 i = 0; i < kStartCount; i++)
    {
        if (ChangedSinceStartup(sStart[i].key))
        {
            return true;
        }
    }
    return false;
}

const char* iGui::RendererName()
{
    switch (iScreenGetBackend())
    {
    case iSCREENBACKEND_D3D9:
        return iScreenGetPipeline() == iSCREENPIPE_FIXED ? "Direct3D 9 (fixed function)" :
                                                           "Direct3D 9";
    case iSCREENBACKEND_D3D11:
        return "Direct3D 11";
    case iSCREENBACKEND_GL3:
        return "OpenGL 3";
    case iSCREENBACKEND_VULKAN:
        return "Vulkan";
    case iSCREENBACKEND_NULL:
        return "none";
    default:
        return iScreenBackendName(iScreenGetBackend());
    }
}

// --- sections -----------------------------------------------------------------------

void iGui::SettingsGameFiles()
{
    ImGui::SeparatorText("Game files");

    FolderRow("assets", "assets.path", "(not set: the folder the game starts in)", NULL);
    RestartTag("assets.path");

    // The folder the game will actually read: TSSM_ASSETS while it is set, the
    // setting otherwise, the working folder when the setting is empty.
    char why[512];
    const char* env = getenv("TSSM_ASSETS");
    const bool overridden = env != NULL && env[0] != '\0';
    const char* path = iConfigGetString("assets.path", "");
    const char* effective = overridden ? env : (path[0] != '\0' ? path : ".");
    const bool ok = CheckAssetFolder(effective, why, sizeof(why));
    ImGui::TextColored(ok ? ImVec4(0.55f, 0.90f, 0.55f, 1.0f) : ImVec4(1.0f, 0.55f, 0.45f, 1.0f),
                       "%s", ok ? "Ready." : "Not usable:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", why);
    if (overridden)
    {
        Note("TSSM_ASSETS is set (%s) and is used instead of the folder above for as long as it "
             "is.",
             env);
    }
    Note("The files of your own Xbox or GameCube disc: both releases work. Xbox: extract the "
         "files from your Xbox disc image (XISO) with an XISO extractor and choose the folder "
         "that directly holds boot.HIP, font.HIP and FMV. GameCube: in Dolphin, right-click the "
         "game and choose "
         "Extract Entire Disc, then choose its files folder, or the folder holding files and "
         "sys. A disc image (.iso, .rvz) is not read directly. No game files come with the "
         "port.");
}

void iGui::SettingsDisplay()
{
    ImGui::SeparatorText("Display");
    if (!BeginRows("display"))
    {
        return;
    }

    const bool live = !InLauncher();

    // The renderer: only what this build carries.
    {
        Choice choices[6];
        S32 n = 0;
        choices[n++] = { "auto", "Automatic" };
#ifdef RW_D3D11
        choices[n++] = { "d3d11", "Direct3D 11" };
#endif
#ifdef RW_D3D9
        choices[n++] = { "d3d9", "Direct3D 9" };
#endif
#ifdef RW_GL3
        choices[n++] = { "gl3", "OpenGL 3" };
#endif
#ifdef RW_VULKAN
        choices[n++] = { "vulkan", "Vulkan" };
#endif
        Row("Renderer");
        ChoiceCombo("##backend", "video.backend", choices, n, "auto");
        RestartTag("video.backend");
        if (live)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("running: %s", RendererName());
        }
    }

    // Window mode. Windowed and borderless are frames around the same surface
    // and switch live; exclusive fullscreen belongs to the device.
    {
        static const Choice kModes[] = { { "borderless", "Borderless fullscreen" },
                                         { "fullscreen", "Exclusive fullscreen" },
                                         { "windowed", "Window" } };
        Row("Window");
        if (ChoiceCombo("##mode", "video.mode", kModes, 3, "borderless") && live)
        {
            const char* mode = iConfigGetString("video.mode", "borderless");
            if (SDL_strcasecmp(mode, "windowed") == 0)
            {
                iWindowSetMode(iWINDOW_WINDOWED);
            }
            else if (SDL_strcasecmp(mode, "borderless") == 0)
            {
                iWindowSetMode(iWINDOW_BORDERLESS);
            }
        }
        RestartTag("video.mode");
    }

    // The picture's shape (iScreen.h, iScreenSizeForAspect). Like the size, it
    // applies at the next start: every screen camera is made at the render size.
    const iScreenAspect aspect = iScreenAspectFromName(iConfigGetString("video.aspect", "wide"));
    {
        static const Choice kAspects[] = { { "wide", "Widescreen (match window)" },
                                           { "4:3", "4:3 (original)" },
                                           { "custom", "Custom size" } };
        Row("Aspect ratio");
        ChoiceCombo("##aspect", "video.aspect", kAspects, 3, "wide");
        RestartTag("video.aspect");
        HelpMarker("Widescreen takes the display's shape and shows more of the world at the sides; "
                   "the menus stay in a centred 4:3 area, and the HUD goes where \"HUD position\" "
                   "says. The shape is taken when the game starts: a window resized or switched "
                   "afterwards shows bars until the next start.");
    }

    // Where the HUD goes on a screen wider than 4:3; on a taller one it is at the
    // edges either way (iScreen.h, iHudAnchor.h). Read by every frame's draws, so
    // both switch live.
    {
        static const Choice kUi[] = { { "pillarbox", "4:3 area (original)" },
                                      { "native", "Screen edges" } };
        Row("HUD position");
        if (ChoiceCombo("##ui", "video.ui", kUi, 2, "pillarbox") && live)
        {
            iScreenSetUIMode(iScreenUIModeFromName(iConfigGetString("video.ui", "pillarbox")));
        }
        HelpMarker("On a screen wider than 4:3. Screen edges: the health goes to the top "
                   "left corner of the screen, the manliness meter to the top right, the treasure "
                   "chests to the bottom left, the tokens, the nitro and the guitar's countdown to "
                   "the bottom right, each as far from its corner as on a 4:3 screen. On a screen "
                   "taller than 4:3 (a phone held upright, a tall window) the HUD is always at the "
                   "top and bottom edges, whichever is chosen here, and the bosses' health bar "
                   "goes down with it. Menus, talk boxes and the pause screen stay in the 4:3 "
                   "area; on a tall screen, what slides out of that area is not seen, as on a 4:3 "
                   "TV.");

        // The HUD is at the edges of a screen taller than 4:3 whatever is chosen
        // above: the one drawn now, or in the launcher the size the game will
        // start at (iSystem.cpp's ApplyConfig: wide takes the display's shape, so
        // an upright display starts tall).
        bool tall = live && iScreenUITall();
        if (!live && aspect != iSCREENASPECT_4_3)
        {
            S32 shapeW = 0;
            S32 shapeH = 0;
            if (aspect == iSCREENASPECT_WIDE && !iWindowGetDisplaySize(&shapeW, &shapeH))
            {
                shapeW = 0;
                shapeH = 0;
            }
            S32 startW = 0;
            S32 startH = 0;
            iScreenSizeForAspect(aspect, iConfigGetInt("video.width", 640),
                                 iConfigGetInt("video.height", 480), shapeW, shapeH, &startW,
                                 &startH);
            tall = 4 * startH > 3 * startW;
        }
        if (tall)
        {
            Note("This screen is taller than 4:3: the HUD is at its top and bottom edges "
                 "whichever is chosen.");
        }

        if (tall ||
            iScreenUIModeFromName(iConfigGetString("video.ui", "pillarbox")) == iSCREENUI_NATIVE)
        {
            float margin = 0.0f;
            Row("HUD edge margin");
            const bool moved =
                FloatSlider("##uimargin", "video.ui_margin", 0.0f, 0.0f, 25.0f, "%.0f%%", &margin);
            if (moved && live)
            {
                iScreenSetUISafeMargin(0.01f * margin);
            }
            HelpMarker("How far short of the screen's edges the HUD stops, in percent of the "
                       "screen's width (of its height, on a screen taller than 4:3), for a TV "
                       "that cuts the edges of the picture off.");
            if (live && !iScreenUIBoxed())
            {
                Note("This picture is 4:3: the HUD is already at its edges.");
            }
        }
    }

    // Render size for the two shapes: a height, and the width follows from it.
    if (aspect != iSCREENASPECT_CUSTOM)
    {
        static const int kHeights[] = { 480, 720, 960, 1080, 1440, 2160 };
        const S32 count = (S32)(sizeof(kHeights) / sizeof(kHeights[0]));

        const int w = iConfigGetInt("video.width", 640);
        const int h = iConfigGetInt("video.height", 480);

        S32 displayW = 0;
        S32 displayH = 0;
        const bool haveDisplay =
            iWindowGetDisplaySize(&displayW, &displayH) && displayW > 0 && displayH > 0;

        // "1280 x 720", as this shape makes it at that height.
        struct SizeLabel
        {
            char text[64];
            SizeLabel(iScreenAspect aspect, int w, int height, S32 displayW, S32 displayH,
                      const char* note)
            {
                S32 rw = 0;
                S32 rh = 0;
                iScreenSizeForAspect(aspect, w, height, displayW, displayH, &rw, &rh);
                snprintf(text, sizeof(text), "%d x %d%s", (int)rw, (int)rh, note);
            }
        };

        Row("Render size");
        ControlWidth();
        if (ImGui::BeginCombo(
                "##height",
                SizeLabel(aspect, w, h, displayW, displayH, h == 480 ? "  (original)" : "").text))
        {
            bool displayListed = !haveDisplay;
            for (S32 i = 0; i < count; i++)
            {
                const bool isDisplay = haveDisplay && kHeights[i] == displayH;
                displayListed |= isDisplay;
                const SizeLabel label(aspect, w, kHeights[i], displayW, displayH,
                                      kHeights[i] == 480 ? "  (original)" :
                                                           (isDisplay ? "  (this display)" : ""));
                if (ImGui::Selectable(label.text, kHeights[i] == h) && kHeights[i] != h)
                {
                    CommitInt("video.height", kHeights[i], true);
                }
            }
            if (!displayListed)
            {
                const SizeLabel label(aspect, w, displayH, displayW, displayH, "  (this display)");
                if (ImGui::Selectable(label.text, displayH == h) && displayH != h)
                {
                    CommitInt("video.height", displayH, true);
                }
            }
            ImGui::EndCombo();
        }
        RestartTag("video.height");
        Note("The picture is drawn at this size and scaled to the window, keeping its shape. "
             "Every frame costs more GPU time the bigger it is.");
    }
    // Render size, exactly, for the custom shape: the size of the picture,
    // which is then scaled to the window keeping its shape.
    else
    {
        static const struct
        {
            int w, h;
            const char* label;
        } kSizes[] = {
            { 640, 480, "640 x 480  (original)" },
            { 960, 720, "960 x 720" },
            { 1280, 960, "1280 x 960" },
            { 1440, 1080, "1440 x 1080" },
            { 1920, 1440, "1920 x 1440" },
            { 2880, 2160, "2880 x 2160" },
            { 1280, 720, "1280 x 720  (wide)" },
            { 1920, 1080, "1920 x 1080  (wide)" },
            { 2560, 1440, "2560 x 1440  (wide)" },
            { 3840, 2160, "3840 x 2160  (wide)" },
        };
        const S32 count = (S32)(sizeof(kSizes) / sizeof(kSizes[0]));

        const int w = iConfigGetInt("video.width", 640);
        const int h = iConfigGetInt("video.height", 480);

        S32 displayW = 0;
        S32 displayH = 0;
        const bool haveDisplay = iWindowGetDisplaySize(&displayW, &displayH) && displayW > 0;

        char preview[64];
        snprintf(preview, sizeof(preview), "%d x %d", w, h);
        for (S32 i = 0; i < count; i++)
        {
            if (kSizes[i].w == w && kSizes[i].h == h)
            {
                snprintf(preview, sizeof(preview), "%s", kSizes[i].label);
            }
        }

        static bool sCustom;
        static int sCustomSize[2];

        Row("Render size");
        ControlWidth();
        if (ImGui::BeginCombo("##size", preview))
        {
            for (S32 i = 0; i < count; i++)
            {
                const bool selected = kSizes[i].w == w && kSizes[i].h == h;
                if (ImGui::Selectable(kSizes[i].label, selected) && !selected)
                {
                    CommitInt("video.width", kSizes[i].w, false);
                    CommitInt("video.height", kSizes[i].h, true);
                    sCustom = false;
                }
            }
            if (haveDisplay)
            {
                char label[64];
                snprintf(label, sizeof(label), "%d x %d  (this display)", (int)displayW,
                         (int)displayH);
                if (ImGui::Selectable(label, w == displayW && h == displayH))
                {
                    CommitInt("video.width", displayW, false);
                    CommitInt("video.height", displayH, true);
                    sCustom = false;
                }
            }
            if (ImGui::Selectable("Other size...", sCustom))
            {
                sCustom = true;
                sCustomSize[0] = w;
                sCustomSize[1] = h;
            }
            ImGui::EndCombo();
        }
        if (ChangedSinceStartup("video.width") || ChangedSinceStartup("video.height"))
        {
            RestartTag(ChangedSinceStartup("video.width") ? "video.width" : "video.height");
        }

        if (sCustom)
        {
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
            ImGui::InputInt2("##custom", sCustomSize);
            ImGui::SameLine();
            if (ImGui::Button("Use"))
            {
                int cw = sCustomSize[0] < 320 ? 320 : (sCustomSize[0] > 15360 ? 15360 : sCustomSize[0]);
                int ch = sCustomSize[1] < 240 ? 240 : (sCustomSize[1] > 8640 ? 8640 : sCustomSize[1]);
                CommitInt("video.width", cw, false);
                CommitInt("video.height", ch, true);
                sCustom = false;
            }
        }
        Note("4:3 sizes keep the original framing; wider ones show more at the sides. The picture "
             "is scaled to the window either way.");
    }

    {
        static const Choice kMsaa[] = { { "1", "Off" }, { "2", "2x" }, { "4", "4x" }, { "8", "8x" } };
        Row("Anti-aliasing");
        ChoiceCombo("##msaa", "video.msaa", kMsaa, 4, "1");
        RestartTag("video.msaa");

        // What the card gave for the request the game started with, when it
        // gave less: 8x is 4x on most phones.
        const S32 asked = iScreenMultiSample();
        const S32 granted = live ? iScreenMultiSampleGranted() : 0;
        if (granted > 1 && granted < asked)
        {
            Note("This GPU gives at most %dx, so %dx is drawn as %dx.", (int)granted, (int)asked,
                 (int)granted);
        }
        else if (granted == 1 && asked > 1)
        {
            Note("This GPU turned down %dx, so anti-aliasing is off.", (int)asked);
        }
        else
        {
            Note("Smooths edges, at a GPU cost in every frame that grows with the samples.");
        }
    }

#ifdef RW_D3D9
    {
        static const Choice kPipes[] = { { "auto", "Automatic" },
                                         { "shader", "Shaders" },
                                         { "fixed", "Fixed function (old GPUs)" } };
        Row("Direct3D 9 path");
        ChoiceCombo("##pipeline", "video.pipeline", kPipes, 3, "auto");
        RestartTag("video.pipeline");
    }
#endif

    {
        Row("Vertical sync");
        if (BoolSetting("##vsync", "video.vsync", true) && live)
        {
            iWindowSetVSync(iConfigGetBool("video.vsync", TRUE));
        }
    }

    // Frame rate cap: how fast the game runs, separate from vsync (iWindow.h).
    {
        const char* rate = iConfigGetString("video.framerate", "60");
        const S32 refresh = live ? iWindowGetDisplayRefreshRate() : 0;

        char displayLabel[64];
        if (refresh > 0)
        {
            snprintf(displayLabel, sizeof(displayLabel), "Display rate (%d)", (int)refresh);
        }
        else
        {
            snprintf(displayLabel, sizeof(displayLabel), "Display rate");
        }

        const Choice choices[] = { { "30", "30" },
                                   { "60", "60  (original)" },
                                   { "display", displayLabel },
                                   { "off", "No cap" } };
        Row("Frame rate cap");
        S32 selected = -1;
        for (S32 i = 0; i < 4; i++)
        {
            if (SDL_strcasecmp(rate, choices[i].value) == 0)
            {
                selected = i;
            }
        }
        char other[32];
        snprintf(other, sizeof(other), "%s fps", rate);
        ControlWidth();
        if (ImGui::BeginCombo("##fps", selected >= 0 ? choices[selected].label : other))
        {
            for (S32 i = 0; i < 4; i++)
            {
                if (ImGui::Selectable(choices[i].label, i == selected) && i != selected)
                {
                    Commit("video.framerate", choices[i].value);
                    if (live)
                    {
                        iWindowSetFrameRate(ResolveFrameRate(choices[i].value));
                    }
                }
            }
            ImGui::EndCombo();
        }
        HelpMarker("How fast the game runs. The game was made for 60; vertical sync is a separate "
                   "question (whether a frame waits for the display).");

        // Above 60 every frame is drawn and simulated again, so the work, the
        // heat and the battery use go up with the rate. On a phone that is
        // the difference between staying cool and throttling (docs/GUI.md).
        const S32 resolved = ResolveFrameRate(rate);
        if (resolved > 60)
        {
            Note("%d frames a second is %.1f times the work of 60: the device runs hotter and "
                 "uses more power, and a phone can start dropping frames once it is warm. Movies "
                 "stay at 60.",
                 (int)resolved, resolved / 60.0);
        }
        else if (resolved == 0 && SDL_strcasecmp(rate, "display") != 0)
        {
            Note("No cap: as many frames as the display (or, with vertical sync off, the "
                 "hardware) allows, at the power that takes. Movies stay at 60.");
        }
    }

    {
        float fov = 75.0f;
        Row("Field of view");
        if (FloatSlider("##fov", "video.fov", 75.0f, 30.0f, 140.0f, "%.0f degrees", &fov) && live)
        {
            iScreenSetFOV(fov);
        }
    }

    // The console's screen passes (docs/RENDERING.md). Both are read every
    // frame, so they switch live.
    {
        Row("Glow");
        if (BoolSetting("##hdrglow", "video.hdr_glow", true) && live)
        {
            iHDRGlowSetEnabled(iConfigGetBool("video.hdr_glow", TRUE));
        }
    }

    {
        Row("Screen warp");
        if (BoolSetting("##screenwarp", "video.screen_warp", true) && live)
        {
            iScreenWarpSetEnabled(iConfigGetBool("video.screen_warp", TRUE));
        }
    }

    ImGui::EndTable();
}

void iGui::SettingsAudio()
{
    ImGui::SeparatorText("Audio");
    if (!BeginRows("audio"))
    {
        return;
    }

    bool changed = false;
    Row("Master");
    changed |= IntSlider("##master", "audio.master_volume", 100, 0, 100, "%d%%");
    Row("Music");
    changed |= IntSlider("##music", "audio.music_volume", 100, 0, 100, "%d%%");
    Row("Sound effects");
    changed |= IntSlider("##sfx", "audio.sfx_volume", 100, 0, 100, "%d%%");
    Row("Movies");
    changed |= IntSlider("##movies", "audio.movie_volume", 100, 0, 100, "%d%%");
    ImGui::EndTable();

    if (changed)
    {
        ApplyAudio();
    }
    Note("Movies is the movies and the cutscenes. The game's own music and effects volumes, in "
         "its options menu, still apply on top of these.");
}

// --- Xbox extras ------------------------------------------------------------------

namespace
{
    const Choice kLooks[] = { { "xbox", "Xbox" }, { "gamecube", "GameCube" } };

    // Whether a feature has a row here: this build has its Xbox version, and
    // the platform has been measured with it (the 3DS not yet: docs/EXTRAS.md).
    bool ExtrasRowShown(iExtrasFeature feature)
    {
#ifdef __3DS__
        (void)feature;
        return false;
#else
        return iExtrasBuilt(feature);
#endif
    }

    // "next level" after the control while the setting differs from the one
    // the current level loaded with; the RestartTag of a per-level feature.
    void NextLevelTag(iExtrasFeature feature)
    {
        if (iGui::InLauncher() || iExtrasAtLevelLoad(feature) == iExtrasSetting(feature))
        {
            return;
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.31f, 1.0f), "next level");
        if (ImGui::BeginItemTooltip())
        {
            ImGui::TextUnformatted("Saved. Takes effect when the next level loads.");
            ImGui::EndTooltip();
        }
    }

    // A row's reason lines: config.ini's value is not one of the two, or the
    // current level cannot give what the setting asks for.
    void ExtrasStatusNote(iExtrasFeature feature)
    {
        const char* bad = iExtrasBadValue(feature);
        if (bad != NULL)
        {
            ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.31f, 1.0f),
                               "config.ini has \"%s\" here, which is not xbox or gamecube: %s is "
                               "used.",
                               bad, iExtrasSetting(feature) == iEXTRAS_XBOX ? "Xbox" : "GameCube");
        }

        char why[256];
        if (!iGui::InLauncher() && iExtrasStatus(feature, why, sizeof(why)))
        {
            ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.31f, 1.0f), "%s", why);
        }
    }
} // namespace

void iGui::SettingsExtras()
{
    bool any = false;
    for (S32 i = 0; i < iEXTRAS_FEATURE_COUNT; i++)
    {
        any = any || ExtrasRowShown((iExtrasFeature)i);
    }
    if (!any)
    {
        return;
    }

    ImGui::SeparatorText("Xbox extras");
    Note("The Xbox release has a few things the GameCube release cut. Each one switches between "
         "the two.");

    const bool live = !InLauncher();
    const char* all = NULL;
    if (ImGui::Button("All Xbox"))
    {
        all = "xbox";
    }
    ImGui::SameLine();
    if (ImGui::Button("All GameCube"))
    {
        all = "gamecube";
    }
    if (all != NULL)
    {
        for (S32 i = 0; i < iEXTRAS_FEATURE_COUNT; i++)
        {
            iConfigSet(iExtrasKey((iExtrasFeature)i), all);
        }
        SaveNow();
        if (live)
        {
            iExtrasApply();
        }
    }

    if (!BeginRows("extras"))
    {
        return;
    }

    if (ExtrasRowShown(iEXTRAS_GLOW))
    {
        // Read every frame, so it switches at once (iExtrasApply). video.hdr_glow
        // is the switch for both glows.
        const bool glowOn = iConfigGetBool("video.hdr_glow", TRUE) != 0;
        Row("Glow");
        ImGui::BeginDisabled(!glowOn);
        if (ChoiceCombo("##xglow", "extras.glow", kLooks, 2, "xbox") && live)
        {
            iExtrasApply();
        }
        ImGui::EndDisabled();
        Note("Xbox: a wide, soft glow, stronger in some levels. GameCube: the GameCube's "
             "tighter, fainter glow.");
        if (!glowOn)
        {
            ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.31f, 1.0f), "Glow is off (Display).");
        }
        ExtrasStatusNote(iEXTRAS_GLOW);
    }

    if (ExtrasRowShown(iEXTRAS_SPOTLIGHTS))
    {
        Row("Spotlights");
        ChoiceCombo("##spotlights", "extras.spotlights", kLooks, 2, "xbox");
        NextLevelTag(iEXTRAS_SPOTLIGHTS);
        Note("Xbox: the moving spotlights in No Cheese!, Bubble Blowing Baby Hunt and Turn the "
             "Tables on Plankton are on, and every spotlight lights what it shines on and has a "
             "lamp flare. GameCube: those lights are off, and a spotlight is its cone only.");
        ExtrasStatusNote(iEXTRAS_SPOTLIGHTS);
    }

    if (ExtrasRowShown(iEXTRAS_CROWDS))
    {
        Row("Fish crowds");
        ChoiceCombo("##crowds", "extras.crowds", kLooks, 2, "xbox");
        NextLevelTag(iEXTRAS_CROWDS);
        Note("Xbox: the crowds in No Cheese! and Sandwich Driving 101 move. GameCube: they stand "
             "still.");
        ExtrasStatusNote(iEXTRAS_CROWDS);
    }

    if (ExtrasRowShown(iEXTRAS_WATER))
    {
        // Not live: a level hides its GameCube or Xbox water models as it is
        // built (iXboxWater.h).
        Row("Water");
        ChoiceCombo("##water", "extras.water", kLooks, 2, "xbox");
        NextLevelTag(iEXTRAS_WATER);
        Note("Xbox: the tanks in Google-Eyes and Smelly Knick Knacks and the sea in Dennis Strikes "
             "Back have the Xbox release's water, with reflections, refraction and moving ripples "
             "(two more pictures drawn each frame for each water area, as large as the render "
             "size makes them). GameCube: the GameCube release's water. The Xbox water needs the "
             "Xbox files, and shaders: Direct3D 9's fixed path and Vulkan use the GameCube water.");
        ExtrasStatusNote(iEXTRAS_WATER);
    }

    if (ExtrasRowShown(iEXTRAS_GUITAR_DISTORTION))
    {
        // Read every frame, so it switches at once (iExtrasApply).
        Row("Sonic Wave Guitar");
        if (ChoiceCombo("##guitar", "extras.guitar_distortion", kLooks, 2, "xbox") && live)
        {
            iExtrasApply();
        }
        Note("Xbox: the screen swirls while you steer the wave. GameCube: no swirl.");
        ExtrasStatusNote(iEXTRAS_GUITAR_DISTORTION);
    }

    if (ExtrasRowShown(iEXTRAS_FRAME_LOCK))
    {
        Row("30 fps levels");
        if (ChoiceCombo("##framelock", "extras.frame_lock", kLooks, 2, "xbox") && live)
        {
            iExtrasApply();
        }
        Note("Xbox: these levels run at your frame-rate cap. GameCube: the driving levels, Bubble "
             "Blowing Baby Hunt and Google-Eyes and Smelly Knick Knacks run at 30 frames a "
             "second. In both, the Patty Wagon drives as it does at 60 frames a second, as on the "
             "Xbox.");
        ExtrasStatusNote(iEXTRAS_FRAME_LOCK);
    }

    ImGui::EndTable();
}

void iGui::SettingsGame()
{
    ImGui::SeparatorText("Game");
    if (!BeginRows("game"))
    {
        return;
    }

    Row("Intro movies");
    BoolSetting("Play the logo movies at startup##intro", "game.intro_movies", true);
    RestartTag("game.intro_movies");

    // The camera, sticks, triggers, mouse and touch settings belong to the
    // input layer, which applies them live (iPadControlsSetSetting): they are
    // on the Controls tab, in the launcher too.
    Row("Controls");
    ImGui::TextDisabled("Camera speed, sticks, triggers, mouse and touch: the Controls tab.");

    Row("Save folder");
    FolderRow("saves", "game.save_folder", "(default: the per-user data folder)", "Default");
    RestartTag("game.save_folder");

    // The save screens' "Please don't turn off your Xbox console" (iSaveText.h).
    // Live: a line already loaded changes at once.
    {
        Choice choices[ISAVETEXT_COUNT];
        char autoLabel[96];
        snprintf(autoLabel, sizeof(autoLabel), "Automatic (%s)",
                 iSaveTextChoiceLabel(iSaveTextPlatform()));
        for (S32 i = 0; i < ISAVETEXT_COUNT; i++)
        {
            choices[i].value = iSaveTextChoiceName((iSaveTextChoice)i);
            choices[i].label = i == ISAVETEXT_AUTO ? autoLabel : iSaveTextChoiceLabel((iSaveTextChoice)i);
        }

        // More than a combo's usual 8 rows: all of them at once.
        Row("Save screen text");
        if (ChoiceCombo("##savetext", "game.save_text", choices, ISAVETEXT_COUNT, "auto",
                        ImGuiComboFlags_HeightLarge))
        {
            iSaveTextApplyConfig();
        }

        bool ok = true;
        const char* value = iConfigGetString("game.save_text", "auto");
        const iSaveTextChoice choice = iSaveTextParse(value, &ok);
        if (!ok)
        {
            ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.31f, 1.0f),
                               "config.ini has \"%s\" here, which is not one of these: Automatic "
                               "is used.",
                               value);
        }
        const iSaveTextChoice resolved = iSaveTextResolve(choice);
        if (resolved == ISAVETEXT_XBOX || resolved == ISAVETEXT_GAMECUBE)
        {
            Note("While saving: \"%s\" On the %s disc's files, its own lines exactly; on the "
                 "other disc's, these words in their place.",
                 iSaveTextSentence(resolved),
                 resolved == ISAVETEXT_XBOX ? "Xbox" : "GameCube");
        }
        else if (resolved == ISAVETEXT_PS2)
        {
            Note("While saving: \"Do not remove memory card (8MB) (for PlayStation 2) in MEMORY "
                 "CARD slot 1, or the DUALSHOCK 2 analog controller, or reset/switch off the "
                 "console.\" The US PS2 disc's own words, in place of either disc's.");
        }
        else
        {
            Note("While saving: \"%s\"", iSaveTextSentence(resolved));
        }
    }

    ImGui::EndTable();
}

void iGui::SettingsInterface()
{
    ImGui::SeparatorText("This window");
    if (!BeginRows("interface"))
    {
        return;
    }

    {
        Row("Size");
        const char* scale = iConfigGetString("gui.scale", "auto");
        bool automatic = SDL_strcasecmp(scale, "auto") == 0;
        if (ImGui::Checkbox("Automatic##scaleauto", &automatic))
        {
            if (automatic)
            {
                Commit("gui.scale", "auto");
            }
            else
            {
                CommitFloat("gui.scale", AutoScale(Window()));
            }
        }
        if (!automatic)
        {
            float value = (float)atof(scale);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
            if (ImGui::SliderFloat("##scale", &value, 0.5f, 4.0f, "%.2fx",
                                   ImGuiSliderFlags_AlwaysClamp))
            {
                CommitFloat("gui.scale", value, false);
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                CommitFloat("gui.scale", value, true);
            }
        }
    }

    {
        static const char* const kKeys[] = { "F1", "F2", "F3", "F4", "F5", "F6",
                                             "F7", "F8", "F9", "F10", "F12", "Insert" };
        const char* key = iConfigGetString("gui.key", "f1");
        Row("Open with");
        ControlWidth();
        if (ImGui::BeginCombo("##key", key))
        {
            for (size_t i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++)
            {
                const bool selected = SDL_strcasecmp(key, kKeys[i]) == 0;
                if (ImGui::Selectable(kKeys[i], selected) && !selected)
                {
                    Commit("gui.key", kKeys[i]);
                }
            }
            ImGui::EndCombo();
        }
        HelpMarker("F11 and Alt+Enter switch between a window and borderless fullscreen, and F8 is "
                   "taken, so neither is offered.");
    }

    Row("Controller");
    BoolSetting("Back+Start opens this window##combo", "gui.pad_combo", true);
    HelpMarker("Hold Back, then press Start. The buttons stay hidden from the game until they are "
               "let go, so closing this window does not pause the game.");

    {
        static const Choice kLauncher[] = { { "auto", "Only when the game's files are missing" },
                                            { "always", "Every time" },
                                            { "never", "Never" } };
        Row("Launcher at startup");
        ChoiceCombo("##launcher", "gui.launcher", kLauncher, 3, "auto");
    }

    ImGui::EndTable();

    const char* path = iConfigPath();
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Settings file: %s", path != NULL ? path : "none (defaults only)");
    if (path != NULL)
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("Show"))
        {
            char dir[1024];
            snprintf(dir, sizeof(dir), "%s", path);
            char* slash = strrchr(dir, '/');
            char* back = strrchr(dir, '\\');
            if (back != NULL && (slash == NULL || back > slash))
            {
                slash = back;
            }
            if (slash != NULL)
            {
                *slash = '\0';
                OpenFolder(dir);
            }
        }
    }
}

void iGui::DrawSettings()
{
    // In the launcher the game's files come first: without them there is no
    // Play, and on a first run they are the one thing to do.
    if (InLauncher())
    {
        SettingsGameFiles();
        ImGui::Spacing();
    }
    SettingsDisplay();
    ImGui::Spacing();
    SettingsExtras();
    ImGui::Spacing();
    SettingsAudio();
    ImGui::Spacing();
    SettingsGame();
    if (!InLauncher())
    {
        ImGui::Spacing();
        SettingsGameFiles();
    }
    ImGui::Spacing();
    SettingsInterface();
}
