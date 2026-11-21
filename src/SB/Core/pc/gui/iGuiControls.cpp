// The Controls tab: the input layer's settings and bindings, through its
// binding API (iPadControls.h).
//
// Nothing here knows a device or a token table. The rows are the input
// layer's (the game's buttons, plus the keyboard's stick rows); a binding is
// read, replaced, extended, shortened and reset through iPadControls*, which
// checks it against the backend's tokens, writes it into config.ini's [pad] or
// [keyboard] section with iConfigSet and makes it live at once; this file then
// saves the file. The [input] settings are drawn from the settings table
// (iConfigTable.h): its kind, its words and its range decide the control, and
// iPadControlsSetSetting checks and applies the value.
//
// The overlay already keeps the mouse for itself while it is open
// (iPadControlsSetMouseCapture, in gui/iGui.cpp).

#include "imgui.h"

#include <SDL3/SDL.h>

#include "gui/iGuiInternal.h"

#include "iConfig.h"
#include "iConfigTable.h"
#include "iPadControls.h"
#include "iPadHost.h"
#include "iPadTokens.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    // --- names for people ---------------------------------------------------------

    // A row's name as config.ini spells it, for a person: "a" -> "A",
    // "lt" -> "LT", "start" -> "Start", "up" -> "D-pad up", "move_up" ->
    // "Move up". On the Switch a game button is named by the Switch button in
    // its place: "a" -> "B", "lt" -> "ZL", "start" -> "+".
    void RowLabel(const char* name, char* out, size_t size)
    {
        const char* sw = iPadSwitchLabels() ? iPadSwitchRowLabel(name) : NULL;
        if (sw != NULL)
        {
            snprintf(out, size, "%s", sw);
            return;
        }

        if (strcmp(name, "up") == 0 || strcmp(name, "down") == 0 || strcmp(name, "left") == 0 ||
            strcmp(name, "right") == 0)
        {
            snprintf(out, size, "D-pad %s", name);
            return;
        }

        snprintf(out, size, "%s", name);
        const size_t n = strlen(out);
        for (size_t i = 0; i < n; i++)
        {
            if (out[i] == '_')
            {
                out[i] = ' ';
            }
            else if (n <= 2 || i == 0)
            {
                out[i] = (char)SDL_toupper((unsigned char)out[i]);
            }
        }
    }

    // One input token for people. "#A" is how a preset says "the button
    // printed A on this controller".
    const char* TokenLabel(iPadBindDevice device, const char* token, char* buf, size_t size)
    {
        if (token[0] == '#' && token[1] != '\0' && token[2] == '\0')
        {
            // On the Switch the letter is the Xbox's, found by position: the
            // input it lands on is what to name. With no pad to ask (a PC
            // labelled as the Switch), the Xbox's letter is its position.
            if (iPadSwitchLabels())
            {
                const char* input = iPadHostInputForLabel(token[1]);
                snprintf(buf, size, "%c", (char)SDL_tolower((unsigned char)token[1]));
                return iPadControlsInputLabel(device, input != NULL ? input : buf);
            }
            snprintf(buf, size, "%c", token[1]);
            return buf;
        }
        return iPadControlsInputLabel(device, token);
    }

    // A whole binding for people: "mouse1, f" -> "Left mouse, F";
    // "rt+!rb" -> "RT + not RB". Empty for nothing bound.
    void BindingLabel(iPadBindDevice device, const char* binding, char* out, size_t size)
    {
        size_t o = 0;
        out[0] = '\0';

        char token[64];
        size_t t = 0;
        bool negate = false;

        for (const char* c = binding;; c++)
        {
            const char ch = *c;
            if (ch == ',' || ch == '+' || ch == '\0')
            {
                token[t] = '\0';
                if (t > 0)
                {
                    char buf[64];
                    const char* label = TokenLabel(device, token, buf, sizeof(buf));
                    o += (size_t)snprintf(out + o, o < size ? size - o : 0, "%s%s",
                                          negate ? "not " : "", label);
                }
                t = 0;
                negate = false;
                if (ch == '\0')
                {
                    break;
                }
                o += (size_t)snprintf(out + o, o < size ? size - o : 0, "%s",
                                      ch == ',' ? ", " : " + ");
                continue;
            }
            if (ch == ' ' || ch == '\t')
            {
                continue;
            }
            if (ch == '!' && t == 0)
            {
                negate = true;
                continue;
            }
            if (t + 1 < sizeof(token))
            {
                token[t++] = ch;
            }
        }
    }

    // The binding's inputs, each on its own: "rt+rb, a" -> "rt", "rb", "a".
    S32 Tokens(const char* binding, char tokens[][48], S32 max)
    {
        S32 count = 0;
        size_t t = 0;
        for (const char* c = binding;; c++)
        {
            const char ch = *c;
            if (ch == ',' || ch == '+' || ch == '\0')
            {
                if (t > 0 && count < max)
                {
                    tokens[count][t] = '\0';
                    count++;
                }
                t = 0;
                if (ch == '\0')
                {
                    break;
                }
                continue;
            }
            if (ch == ' ' || ch == '\t' || (ch == '!' && t == 0))
            {
                continue;
            }
            if (count < max && t + 1 < 48)
            {
                tokens[count][t++] = ch;
            }
        }
        return count;
    }

    // The alternatives of a binding: "rt+rb, a" -> "rt+rb", "a".
    S32 Alternatives(const char* binding, char alts[][96], S32 max)
    {
        S32 count = 0;
        size_t t = 0;
        for (const char* c = binding;; c++)
        {
            const char ch = *c;
            if (ch == ',' || ch == '\0')
            {
                while (t > 0 && alts[count][t - 1] == ' ')
                {
                    t--;
                }
                if (t > 0 && count < max)
                {
                    alts[count][t] = '\0';
                    count++;
                }
                t = 0;
                if (ch == '\0')
                {
                    break;
                }
                continue;
            }
            if (t == 0 && ch == ' ')
            {
                continue;
            }
            if (count < max && t + 1 < 96)
            {
                alts[count][t++] = ch;
            }
        }
        return count;
    }

    // --- state ---------------------------------------------------------------------

    enum CaptureMode
    {
        CAPTURE_REPLACE,
        CAPTURE_ADD
    };

    struct Capture
    {
        bool active;
        iPadBindDevice device;
        S32 row;
        CaptureMode mode;
        bool primed;
        U64 startedNs;
        ImVec2 cancelMin;
        ImVec2 cancelMax;
    };

    Capture sCapture;
    const U64 kCaptureTimeoutNs = 6000000000ull;

    // The binding being typed by hand.
    struct Typing
    {
        iPadBindDevice device;
        S32 row;
        char text[160];
    };
    Typing sTyping;

    // Popups are opened outside the table, at the tab's own ID level.
    bool sOpenCapture;
    bool sOpenEdit;
    iPadBindDevice sEditDevice;
    S32 sEditRow = -1;

    char sMessage[320];
    bool sMessageBad;

    void Say(bool bad, const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        vsnprintf(sMessage, sizeof(sMessage), fmt, args);
        va_end(args);
        sMessageBad = bad;
    }

    void Save()
    {
        iGui::SaveNow();
    }

    void StartCapture(iPadBindDevice device, S32 row, CaptureMode mode)
    {
        sCapture.active = true;
        sCapture.device = device;
        sCapture.row = row;
        sCapture.mode = mode;
        sCapture.primed = false;
        sCapture.startedNs = SDL_GetTicksNS();
        sCapture.cancelMin = ImVec2(0.0f, 0.0f);
        sCapture.cancelMax = ImVec2(0.0f, 0.0f);
        sOpenCapture = true;
    }

    // "Space is also Start's" after a new input, so a clash is seen at once.
    void ReportShared(iPadBindDevice device, S32 row, const char* token)
    {
        S32 rows[8];
        const S32 n = iPadControlsWhoUses(device, token, rows, 8);
        char others[256] = "";
        size_t o = 0;
        S32 listed = 0;
        for (S32 i = 0; i < n && i < 8; i++)
        {
            if (rows[i] == row)
            {
                continue;
            }
            iPadControlsRowInfo info;
            if (!iPadControlsRow(rows[i], &info))
            {
                continue;
            }
            char label[48];
            RowLabel(info.name, label, sizeof(label));
            o += (size_t)snprintf(others + o, o < sizeof(others) ? sizeof(others) - o : 0, "%s%s",
                                  listed > 0 ? ", " : "", label);
            listed++;
        }

        iPadControlsRowInfo info;
        iPadControlsRow(row, &info);
        char label[48];
        RowLabel(info.name, label, sizeof(label));
        char buf[64];
        const char* input = TokenLabel(device, token, buf, sizeof(buf));
        if (listed > 0)
        {
            Say(false, "%s is now %s; it is also %s. Pressing it does both.", input, label, others);
        }
        else
        {
            Say(false, "%s is now %s.", input, label);
        }
    }

    void DrawCapturePopup()
    {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                                ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Bind##capture", NULL,
                                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
        {
            return;
        }

        iPadControlsRowInfo info;
        if (!sCapture.active || !iPadControlsRow(sCapture.row, &info))
        {
            sCapture.active = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        const bool pad = sCapture.device == IPAD_BIND_PAD;

        // The press that opened this popup is still held: the first call
        // forgets what is already down (prime), so it cannot answer itself.
        char got[64];
        const S32 prime = sCapture.primed ? FALSE : TRUE;
        sCapture.primed = true;
        const S32 captured = iPadControlsCapture(sCapture.device, prime, got, sizeof(got));

        const U64 elapsed = SDL_GetTicksNS() - sCapture.startedNs;
        bool done = false;

        if (captured)
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const bool onCancel = mouse.x >= sCapture.cancelMin.x && mouse.y >= sCapture.cancelMin.y &&
                                  mouse.x < sCapture.cancelMax.x && mouse.y < sCapture.cancelMax.y;
            if (!pad && (strcmp(got, "escape") == 0 || (strcmp(got, "mouse1") == 0 && onCancel)))
            {
                done = true; // Esc, or a click on Cancel, cancels a keyboard capture
            }
            else
            {
                char err[256] = "";
                const bool ok =
                    sCapture.mode == CAPTURE_ADD ?
                        iPadControlsAdd(sCapture.device, sCapture.row, got, err, sizeof(err)) :
                        iPadControlsSet(sCapture.device, sCapture.row, got, err, sizeof(err));
                if (ok)
                {
                    Save();
                    ReportShared(sCapture.device, sCapture.row, got);
                }
                else
                {
                    Say(true, "Not bound: %s", err);
                }
                done = true;
            }
        }
        else if (elapsed > kCaptureTimeoutNs)
        {
            done = true;
        }

        char label[48];
        RowLabel(info.name, label, sizeof(label));
        ImGui::Text("%s for %s%s", pad ? "Press a controller button" : "Press a key or mouse button",
                    label, sCapture.mode == CAPTURE_ADD ? " (added to what it has)" : "");
        if (info.does != NULL)
        {
            ImGui::TextDisabled("(%s)", info.does);
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Cancels by itself in %d s%s.",
                            (int)((kCaptureTimeoutNs -
                                   (elapsed < kCaptureTimeoutNs ? elapsed : kCaptureTimeoutNs)) /
                                  1000000000ull) +
                                1,
                            pad ? "" : ", or press Esc");
        if (ImGui::Button("Cancel"))
        {
            done = true;
        }
        sCapture.cancelMin = ImGui::GetItemRectMin();
        sCapture.cancelMax = ImGui::GetItemRectMax();

        if (done)
        {
            sCapture.active = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // The menu behind a binding's button: a new input in its place, another
    // beside it, one taken away, the binding typed, the default back.
    void DrawEditPopup()
    {
        if (!ImGui::BeginPopup("Binding##edit"))
        {
            return;
        }

        iPadControlsRowInfo info;
        if (sEditRow < 0 || !iPadControlsRow(sEditRow, &info))
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        const iPadBindDevice device = sEditDevice;
        const bool pad = device == IPAD_BIND_PAD;
        char label[48];
        RowLabel(info.name, label, sizeof(label));
        ImGui::TextDisabled("%s, %s", label, pad ? "controller" : "keyboard and mouse");
        ImGui::Separator();

        char current[160];
        bool isDefault = true;
        iPadControlsGet(device, sEditRow, current, sizeof(current), &isDefault);

        if (ImGui::MenuItem(pad ? "Press a new button..." : "Press a new key..."))
        {
            StartCapture(device, sEditRow, CAPTURE_REPLACE);
        }
        if (ImGui::MenuItem(pad ? "Add another button..." : "Add another key...", NULL, false,
                            current[0] != '\0'))
        {
            StartCapture(device, sEditRow, CAPTURE_ADD);
        }

        char alts[4][96];
        const S32 nalts = Alternatives(current, alts, 4);
        if (nalts > 1)
        {
            ImGui::Separator();
            for (S32 a = 0; a < nalts; a++)
            {
                char shown[160];
                BindingLabel(device, alts[a], shown, sizeof(shown));
                char item[200];
                snprintf(item, sizeof(item), "Remove %s##rm%d", shown, (int)a);
                if (ImGui::MenuItem(item))
                {
                    if (iPadControlsRemove(device, sEditRow, a))
                    {
                        Save();
                        Say(false, "%s no longer presses %s.", shown, label);
                    }
                }
            }
        }

        ImGui::Separator();
#ifndef __SWITCH__
        // On the Switch it would open the system keyboard; pressing the new
        // button covers a controller.
        if (ImGui::MenuItem("Type it..."))
        {
            sTyping.device = device;
            sTyping.row = sEditRow;
            snprintf(sTyping.text, sizeof(sTyping.text), "%s", current);
            sOpenEdit = true;
        }
#endif
        if (ImGui::MenuItem("Nothing (unbind)", NULL, false, current[0] != '\0'))
        {
            char err[128];
            if (iPadControlsSet(device, sEditRow, "", err, sizeof(err)))
            {
                Save();
                Say(false, "%s has nothing on the %s now.", label, pad ? "controller" : "keyboard");
            }
        }
        if (ImGui::MenuItem("Back to the default", NULL, false, !isDefault))
        {
            iPadControlsReset(device, sEditRow);
            Save();
            Say(false, "%s is back to its default.", label);
        }
        ImGui::EndPopup();
    }

    void DrawTypePopup()
    {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                                ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopupModal("Type a binding", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        static char sTypeError[256];
        iPadControlsRowInfo info;
        iPadControlsRow(sTyping.row, &info);
        char label[48];
        RowLabel(info.name, label, sizeof(label));
        ImGui::Text("%s on the %s", label,
                    sTyping.device == IPAD_BIND_PAD ? "controller" : "keyboard and mouse");
        ImGui::TextDisabled("Inputs as config.ini names them: a comma between alternatives, "
                            "+ for a chord, ! for \"not\" (rt+!rb).");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0f);
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
            sTypeError[0] = '\0';
        }
        const bool enter = ImGui::InputText("##binding", sTyping.text, sizeof(sTyping.text),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        if (sTypeError[0] != '\0')
        {
            ImGui::TextColored(ImVec4(1.0f, 0.47f, 0.43f, 1.0f), "%s", sTypeError);
        }
        if (ImGui::Button("Apply") || enter)
        {
            if (iPadControlsSet(sTyping.device, sTyping.row, sTyping.text, sTypeError,
                                sizeof(sTypeError)))
            {
                Save();
                Say(false, "%s set.", label);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // A binding's button: a controller's names are short, a keyboard's are
    // words ("Right mouse, Backspace"); the tooltip has the whole of either.
    float BindingWidth(iPadBindDevice device)
    {
        return ImGui::GetFontSize() * (device == IPAD_BIND_PAD ? 7.0f : 12.0f);
    }

    // One binding cell: the button showing it (click for the menu), and a
    // mark when an input in it also presses another row.
    void BindingCell(iPadBindDevice device, S32 row, const iPadControlsRowInfo& info)
    {
        const bool pad = device == IPAD_BIND_PAD;

        char current[160];
        bool isDefault = true;
        iPadControlsGet(device, row, current, sizeof(current), &isDefault);

        char shown[200];
        BindingLabel(device, current, shown, sizeof(shown));
        if (shown[0] == '\0')
        {
            // A stick row unbound on a controller is the controller's own
            // stick; buttons can still be bound to it.
            snprintf(shown, sizeof(shown), "%s",
                     !(pad && info.stick)                 ? "(none)" :
                     strncmp(info.name, "move", 4) == 0   ? "(left stick)" :
                     strncmp(info.name, "camera", 6) == 0 ? "(right stick)" :
                                                            "(none)");
        }

        ImGui::PushID(pad ? 0 : 1);
        char id[240];
        snprintf(id, sizeof(id), "%s%s###bind", shown, isDefault ? "" : " *");
        if (ImGui::Button(id, ImVec2(BindingWidth(device), 0.0f)))
        {
            sEditDevice = device;
            sEditRow = row;
            ImGui::OpenPopup("Binding##edit");
        }
        if (ImGui::BeginItemTooltip())
        {
            ImGui::TextUnformatted(current[0] != '\0' ? current : "(nothing bound)");
#ifdef __SWITCH__
            ImGui::TextDisabled("%s. Choose it to change it.",
                                isDefault ? "The default" : "Changed from the default (*)");
#else
            ImGui::TextDisabled("%s. Click to change.",
                                isDefault ? "The default" : "Changed from the default (*)");
#endif
            ImGui::EndTooltip();
        }
        DrawEditPopup();

        // Shared inputs: a mark with the other rows in its tooltip.
        char tokens[8][48];
        const S32 n = Tokens(current, tokens, 8);
        char shared[256] = "";
        size_t o = 0;
        for (S32 t = 0; t < n; t++)
        {
            S32 rows[8];
            const S32 users = iPadControlsWhoUses(device, tokens[t], rows, 8);
            for (S32 u = 0; u < users && u < 8; u++)
            {
                if (rows[u] == row)
                {
                    continue;
                }
                iPadControlsRowInfo other;
                if (!iPadControlsRow(rows[u], &other))
                {
                    continue;
                }
                char buf[64];
                char otherLabel[48];
                RowLabel(other.name, otherLabel, sizeof(otherLabel));
                o += (size_t)snprintf(shared + o, o < sizeof(shared) ? sizeof(shared) - o : 0,
                                      "%s%s is also %s", o > 0 ? "\n" : "",
                                      TokenLabel(device, tokens[t], buf, sizeof(buf)), otherLabel);
            }
        }
        if (shared[0] != '\0')
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.98f, 0.82f, 0.31f, 1.0f), "!");
            if (ImGui::BeginItemTooltip())
            {
                ImGui::TextUnformatted(shared);
                ImGui::EndTooltip();
            }
        }
        ImGui::PopID();
    }

    // --- the [input] settings ----------------------------------------------------

    struct Word
    {
        const char* value;
        const char* label;
    };

    struct InputSetting
    {
        const char* name;
        const char* label;
        // Labels for an enum's words, or for the word a number may be instead
        // ("auto"); NULL entries end it.
        Word words[12];
        // Applies at the next start rather than at once.
        bool restart;
    };

    const InputSetting kSettings[] = {
        { "preset",
          "Controller type",
          { { "auto", "Match the controller" },
            { "xbox", "Xbox" },
            { "playstation", "PlayStation" },
            { "switch", "Switch" },
            { "gamecube", "GameCube" } },
          false },
        { "layout",
          "Move buttons",
          { { "auto", "Automatic" },
            { "xbox", "Xbox (Karate Spin on X)" },
            { "gamecube", "GameCube (Karate Spin on B)" } },
          false },
        { "button_icons",
          "Button pictures",
          { { "auto", "The device last used" },
            { "xbox", "Xbox" },
            { "playstation", "PlayStation" },
            { "switch", "Switch" },
            { "gamecube", "GameCube" },
            { "keyboard", "Keyboard" },
            { "off", "The disc's own" } },
          false },
        { "keyboard",
          "Keyboard and mouse",
          { { "always", "Always, beside a controller" },
            { "fallback", "Only with no controller" },
            { "off", "Off" } },
          false },
        { "stick_curve",
          "Stick response",
          { { "xbox", "Xbox release" }, { "gamecube", "GameCube release" }, { "linear", "Linear" } },
          false },
        { "deadzone", "Stick deadzone", { { "auto", "The response's own" } }, false },
        { "trigger_threshold", "Trigger press point", { { "auto", "Automatic (30%)" } }, false },
        { "camera_sensitivity", "Camera speed", {}, false },
        { "camera_invert_x", "Invert camera left/right", {}, false },
        { "camera_invert_y", "Invert camera up/down", {}, false },
        { "mouse_look", "Mouse look", {}, false },
        { "mouse_sensitivity_x", "Mouse speed left/right", {}, false },
        { "mouse_sensitivity_y",
          "Mouse speed up/down",
          { { "same", "Same as left/right" } },
          false },
        { "mouse_invert_x", "Invert mouse left/right", {}, false },
        { "mouse_invert_y", "Invert mouse up/down", {}, false },
        { "mouse_raw", "Raw mouse input", {}, false },
        { "mouse_smoothing", "Mouse smoothing", {}, false },
        { "mouse_release_key",
          "Key that frees the mouse pointer",
          { { "f2", "F2" },
            { "f3", "F3" },
            { "f4", "F4" },
            { "f5", "F5" },
            { "f6", "F6" },
            { "f7", "F7" },
            { "f9", "F9" },
            { "f10", "F10" },
            { "f12", "F12" },
            { "tilde", "Tilde" },
            { "none", "No key" } },
          false },
        { "mouse_menus", "Mouse pointer in menus", {}, false },
        { "rumble", "Rumble strength", {}, false },
        { "touch_controls",
          "Touch controls",
#ifdef __SWITCH__
          { { "auto", "Automatic (off)" }, { "on", "On" }, { "off", "Off" } },
#else
          { { "auto", "Automatic (Android)" }, { "on", "On" }, { "off", "Off" } },
#endif
          true },
        { "test",
          "Input test",
          { { "off", "Off" }, { "log", "Log what the game reads" }, { "overlay", "Log and draw" } },
          false },
    };

    // On the Switch, the words and help that name an Xbox button name the
    // Switch's instead: the Xbox's X is the Switch's Y, its B the Switch's A.
    struct SwitchWord
    {
        const char* setting;
        const char* value; // NULL: the help text
        const char* text;
    };

    const SwitchWord kSwitchTexts[] = {
        { "layout", "xbox", "Xbox (Karate Spin on Y)" },
        { "layout", "gamecube", "GameCube (Karate Spin on A)" },
        { "layout", NULL,
          "Which face button the moves use. xbox: Karate Spin on Y, SpongeBowl on A, as the "
          "Xbox release. gamecube: Karate Spin on A, SpongeBowl on Y, as the GameCube release. "
          "Menus are the same in both (B accept, A back). The game's button prompts follow." },
        { "trigger_threshold", NULL,
          "How far ZL and ZR must be pressed, as a percentage: auto (30) or 5 to 95. The "
          "Switch's ZL and ZR only click, so every value works the same there." },
    };

    const char* SwitchText(const char* setting, const char* value)
    {
        if (!iPadSwitchLabels())
        {
            return NULL;
        }
        for (size_t i = 0; i < sizeof(kSwitchTexts) / sizeof(kSwitchTexts[0]); i++)
        {
            const SwitchWord& t = kSwitchTexts[i];
            if (strcmp(t.setting, setting) != 0)
            {
                continue;
            }
            if (value == NULL ? t.value == NULL :
                                (t.value != NULL && SDL_strcasecmp(t.value, value) == 0))
            {
                return t.text;
            }
        }
        return NULL;
    }

    const char* WordLabel(const InputSetting& s, const char* value)
    {
        const char* sw = SwitchText(s.name, value);
        if (sw != NULL)
        {
            return sw;
        }
        for (S32 i = 0; i < 12 && s.words[i].value != NULL; i++)
        {
            if (SDL_strcasecmp(s.words[i].value, value) == 0)
            {
                return s.words[i].label;
            }
        }
        return value;
    }

    char sSettingError[256];

    void SetSetting(const char* name, const char* value, bool save)
    {
        char err[256] = "";
        if (iPadControlsSetSetting(name, value, err, sizeof(err)))
        {
            sSettingError[0] = '\0';
            if (save)
            {
                Save();
            }
        }
        else
        {
            snprintf(sSettingError, sizeof(sSettingError), "input.%s: %s", name, err);
        }
    }

    // A number slider over the setting's range, kept as text the way the
    // file holds it. Applied while dragged, saved when let go.
    void NumberSlider(const InputSetting& s, const iConfigSetting* def, const char* value)
    {
        const bool isInt = def->kind == ICONFIG_INT;
        const char* format = isInt ? "%d" : "%.2f";
        if (strcmp(s.name, "deadzone") == 0 || strcmp(s.name, "trigger_threshold") == 0 ||
            strcmp(s.name, "rumble") == 0)
        {
            format = "%d%%";
        }
        else if (strcmp(s.name, "mouse_smoothing") == 0)
        {
            format = "%d ms";
        }
        else if (!isInt)
        {
            format = "%.2fx";
        }

        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
        bool changed;
        char text[32];
        if (isInt)
        {
            int v = atoi(value);
            if (v < (int)def->min)
            {
                v = (int)def->min;
            }
            changed = ImGui::SliderInt("##v", &v, (int)def->min, (int)def->max, format,
                                       iGui::kSliderFlags);
            snprintf(text, sizeof(text), "%d", v);
        }
        else
        {
            float v = (float)atof(value);
            changed = ImGui::SliderFloat("##v", &v, def->min, def->max, format, iGui::kSliderFlags);
            snprintf(text, sizeof(text), "%.2f", v);
        }
        if (changed)
        {
            SetSetting(s.name, text, false);
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            SetSetting(s.name, text, true);
        }
    }

    void DrawSetting(const InputSetting& s)
    {
        char key[64];
        snprintf(key, sizeof(key), "input.%s", s.name);
        const iConfigSetting* def = iConfigTableFind(key);
        if (def == NULL)
        {
            return;
        }

        const char* value = iPadControlsGetSetting(s.name);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(s.label);
        if (def->comment != NULL)
        {
            char help[600];
            iConfigTableSummary(def, help, sizeof(help));
            const char* sw = SwitchText(s.name, NULL);
            ImGui::SameLine();
            iGui::HelpMarker(sw != NULL ? sw : help);
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(s.name);

        switch (def->kind)
        {
        case ICONFIG_BOOL:
        {
            bool on =
                iConfigGetBool(key, SDL_strcasecmp(def->value, "on") == 0 ? TRUE : FALSE) != 0;
            if (ImGui::Checkbox("##b", &on))
            {
                SetSetting(s.name, on ? "on" : "off", true);
            }
            break;
        }

        case ICONFIG_ENUM:
        {
            // As wide as its longest choice.
            float widest = ImGui::GetFontSize() * 8.0f;
            for (const char* c = def->choices; c != NULL && *c != '\0';)
            {
                const char* bar = strchr(c, '|');
                const size_t len = bar != NULL ? (size_t)(bar - c) : strlen(c);
                char word[32];
                snprintf(word, sizeof(word), "%.*s", (int)len, c);
                const float w = ImGui::CalcTextSize(WordLabel(s, word)).x;
                widest = w > widest ? w : widest;
                c = bar != NULL ? bar + 1 : NULL;
            }
            ImGui::SetNextItemWidth(widest + ImGui::GetFrameHeight() +
                                    ImGui::GetStyle().FramePadding.x * 2.0f);
            if (ImGui::BeginCombo("##e", WordLabel(s, value)))
            {
                // The table's words, in its order: the domain is the table's.
                const char* c = def->choices;
                while (c != NULL && *c != '\0')
                {
                    const char* bar = strchr(c, '|');
                    const size_t len = bar != NULL ? (size_t)(bar - c) : strlen(c);
                    char word[32];
                    snprintf(word, sizeof(word), "%.*s", (int)len, c);
                    const bool selected = SDL_strcasecmp(word, value) == 0;
                    if (ImGui::Selectable(WordLabel(s, word), selected) && !selected)
                    {
                        SetSetting(s.name, word, true);
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                    c = bar != NULL ? bar + 1 : NULL;
                }
                ImGui::EndCombo();
            }
            break;
        }

        case ICONFIG_INT:
        case ICONFIG_FLOAT:
        {
            // A number, or the word beside it ("auto").
            const char* word = def->choices;
            if (word != NULL && strchr(word, '|') == NULL)
            {
                bool automatic = SDL_strcasecmp(value, word) == 0;
                if (ImGui::Checkbox(WordLabel(s, word), &automatic))
                {
                    char middle[32];
                    snprintf(middle, sizeof(middle), "%d",
                             strcmp(s.name, "trigger_threshold") == 0 ? 30 :
                                                                         (int)def->min + 15);
                    if (strcmp(s.name, "mouse_sensitivity_y") == 0)
                    {
                        // Starts from what it was following.
                        snprintf(middle, sizeof(middle), "%s",
                                 iPadControlsGetSetting("mouse_sensitivity_x"));
                    }
                    SetSetting(s.name, automatic ? word : middle, true);
                    value = iPadControlsGetSetting(s.name);
                }
                if (!automatic)
                {
                    ImGui::SameLine();
                    NumberSlider(s, def, value);
                }
            }
            else
            {
                NumberSlider(s, def, value);
            }
            break;
        }

        default:
            ImGui::TextUnformatted(value);
            break;
        }

        if (s.restart)
        {
            iGui::RestartTag(key);
        }
        ImGui::PopID();
    }

    void DrawDevices()
    {
        iPadControlsLive live;
        memset(&live, 0, sizeof(live));
        iPadControlsGetLive(&live);

        int n = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&n);
        if (ids == NULL || n == 0)
        {
            ImGui::TextUnformatted("No controller connected: the keyboard and mouse play as "
                                   "controller 1.");
        }
        else
        {
#ifdef __SWITCH__
            // SDL's Switch driver opens four controllers whatever is attached.
            n = 1;
#endif
            for (int i = 0; i < n; i++)
            {
                const char* name = SDL_GetGamepadNameForID(ids[i]);
                ImGui::BulletText("%s%s", name != NULL ? name : "Unnamed controller",
                                  i == 0 ? "  (controller 1)" : "");
            }
        }
        SDL_free(ids);

        ImGui::TextDisabled("Controller 1 is %s: %s buttons, %s move layout, %s button "
                            "pictures.",
                            live.device != NULL ? live.device : "the keyboard and mouse",
                            live.preset != NULL ? live.preset : "?",
                            live.layout != NULL ? live.layout : "?",
                            live.icons != NULL ? live.icons : "the disc's own");
    }
} // namespace

void iGui::DrawControls()
{
    ImGui::SeparatorText("Controllers");
    DrawDevices();

    ImGui::SeparatorText("Buttons");
#ifdef __SWITCH__
    Note("Choose a binding (B, or a tap) to change it. * marks one changed from the default; ! an "
         "input that also presses another button.");
#else
    Note("Click a binding to change it. * marks one changed from the default; ! an input that "
         "also presses another button.");
#endif

    if (sMessage[0] != '\0')
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(sMessageBad ? ImVec4(1.0f, 0.47f, 0.43f, 1.0f) :
                                         ImVec4(0.55f, 0.85f, 0.55f, 1.0f),
                           "%s", sMessage);
        ImGui::PopTextWrapPos();
    }

    // Columns as wide as what they hold, measured in the font in use.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float extra = style.ItemSpacing.x + ImGui::CalcTextSize("!").x + style.CellPadding.x * 2.0f;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
#ifdef __SWITCH__
    const int columns = 3;
#else
    const int columns = 4;
#endif
    if (ImGui::BeginTable("bindings", columns, flags))
    {
        ImGui::TableSetupColumn("Game button", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::CalcTextSize("Camera right").x + style.CellPadding.x * 2.0f);
        ImGui::TableSetupColumn("Does", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Controller", ImGuiTableColumnFlags_WidthFixed,
                                BindingWidth(IPAD_BIND_PAD) + extra);
#ifndef __SWITCH__
        ImGui::TableSetupColumn("Keyboard, mouse", ImGuiTableColumnFlags_WidthFixed,
                                BindingWidth(IPAD_BIND_KEYBOARD) + extra);
#endif
        ImGui::TableHeadersRow();

        const S32 rows = iPadControlsRowCount();
        for (S32 r = 0; r < rows; r++)
        {
            iPadControlsRowInfo info;
            if (!iPadControlsRow(r, &info))
            {
                continue;
            }

            ImGui::PushID(r);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            char label[48];
            RowLabel(info.name, label, sizeof(label));
            ImGui::TextUnformatted(label);

            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", info.does != NULL ? info.does : "");
            ImGui::PopTextWrapPos();

            ImGui::TableSetColumnIndex(2);
            BindingCell(IPAD_BIND_PAD, r, info);
#ifndef __SWITCH__
            ImGui::TableSetColumnIndex(3);
            BindingCell(IPAD_BIND_KEYBOARD, r, info);
#endif

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (sOpenCapture)
    {
        sOpenCapture = false;
        ImGui::OpenPopup("Bind##capture");
    }
    DrawCapturePopup();

    if (sOpenEdit)
    {
        sOpenEdit = false;
        ImGui::OpenPopup("Type a binding");
    }
    DrawTypePopup();

    ImGui::Spacing();
    if (ImGui::Button("Controller back to its defaults"))
    {
        iPadControlsResetAll(IPAD_BIND_PAD);
        Save();
        Say(false, "Every controller binding follows the controller type again.");
    }
#ifndef __SWITCH__
    ImGui::SameLine();
    if (ImGui::Button("Keyboard back to its defaults"))
    {
        iPadControlsResetAll(IPAD_BIND_KEYBOARD);
        Save();
        Say(false, "Every key is back to its default.");
    }
#endif
    Note("Bindings are kept in config.ini's [pad] and [keyboard] sections. docs/CONTROLS.md "
         "lists what every button does.");

    ImGui::SeparatorText("Settings");
    if (ImGui::BeginTable("input_settings", 2, ImGuiTableFlags_SizingStretchProp))
    {
        // The longest label and its "(?)".
        float labels = 0.0f;
        for (size_t i = 0; i < sizeof(kSettings) / sizeof(kSettings[0]); i++)
        {
            const float w = ImGui::CalcTextSize(kSettings[i].label).x;
            labels = w > labels ? w : labels;
        }
        labels += ImGui::CalcTextSize(" (?)").x + style.ItemSpacing.x + style.CellPadding.x * 2.0f;
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labels);
        ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);
        for (size_t i = 0; i < sizeof(kSettings) / sizeof(kSettings[0]); i++)
        {
#ifdef __SWITCH__
            // Keyboard and mouse rows; config.ini still has them for a USB
            // keyboard.
            if (strcmp(kSettings[i].name, "keyboard") == 0 ||
                strncmp(kSettings[i].name, "mouse_", 6) == 0)
            {
                continue;
            }
#endif
            DrawSetting(kSettings[i]);
        }
        ImGui::EndTable();
    }
    if (sSettingError[0] != '\0')
    {
        ImGui::TextColored(ImVec4(1.0f, 0.47f, 0.43f, 1.0f), "%s", sSettingError);
    }
}
