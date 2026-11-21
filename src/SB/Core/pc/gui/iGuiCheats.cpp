// The Cheats tab: the game's own cheats as switches and buttons, and the
// port's (iCheats.h, docs/CHEATS.md).
//
// The game's own are not settings. A click asks for one (iCheatsRequest); the
// game's own code does it on the next frame of a running level, and every
// switch here shows what the game holds, read each frame, so a code entered
// with the buttons shows too. Before the game, and at the title screen, they
// are listed and greyed.
//
// The port's are config.ini's [cheats] keys, changed and saved like any other
// setting, and applied from the next frame.

#include "imgui.h"

#include "gui/iGuiInternal.h"

#include "iCheats.h"
#include "iConfig.h"

#include <stdio.h>
#include <string.h>

namespace
{
    const ImVec4 kWarn(0.98f, 0.82f, 0.31f, 1.0f);

    void Warn(const char* text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    void Indented(const char* text, bool warn = false)
    {
        ImGui::Indent();
        if (warn)
        {
            Warn(text);
        }
        else
        {
            iGui::Note("%s", text);
        }
        ImGui::Unindent();
    }

    // "X X Y Y X Y Y X"
    void SpacedCode(const char* code, char* out, size_t outSize)
    {
        size_t n = 0;
        for (const char* c = code; *c != '\0' && n + 2 < outSize; c++)
        {
            if (c != code)
            {
                out[n++] = ' ';
            }
            out[n++] = *c;
        }
        out[n] = '\0';
    }

    // The code whose Enter button has been pressed once and now asks "for good?",
    // or -1. Only the codes that cannot be undone ask.
    S32 sArmed = -1;
    int sArmedFrame = 0;

    void GameCodes(const iCheatsStatus& status, bool usable)
    {
        // Nothing stays asked while the tab is away or the codes are greyed.
        const int frame = ImGui::GetFrameCount();
        if (!usable || frame - sArmedFrame > 1)
        {
            sArmed = -1;
        }
        sArmedFrame = frame;

        ImGui::Spacing();
        ImGui::TextUnformatted("Button codes");
        iGui::Note("On the pause screen, hold LT and RT (L and R on a GameCube controller; by "
                   "default R and E on the keyboard, with F for X and Q for Y) and press the "
                   "eight buttons. That works as it always has; a click here does the same thing "
                   "without the buttons. A code that cannot be undone asks once more first.");

        for (S32 i = 0; i < iCheatCodeCount(); i++)
        {
            const iCheatCode* code = iCheatCodeAt(i);
            const bool on = (status.gameFlags & code->flag) != 0;
            char spaced[32];
            SpacedCode(code->code, spaced, sizeof(spaced));

            ImGui::PushID(i);
            ImGui::BeginDisabled(!usable);
            if (code->toggles)
            {
                bool box = on;
                if (ImGui::Checkbox(code->name, &box))
                {
                    iCheatsRequest(iCHEAT_REQUEST_CODE, i);
                }
            }
            else
            {
                // Two steps, as these are saved with the game: the first press
                // asks, the second does it, and a press or click anywhere else
                // (or moving off the button with a controller) takes the
                // question back.
                const bool armed = sArmed == i;
                if (ImGui::Button(armed ? "Enter for good?###enter" : "Enter###enter"))
                {
                    if (armed)
                    {
                        iCheatsRequest(iCHEAT_REQUEST_CODE, i);
                        sArmed = -1;
                    }
                    else
                    {
                        sArmed = i;
                    }
                }
                else if (armed &&
                         (!ImGui::IsItemFocused() ||
                          ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) &&
                           !ImGui::IsItemHovered())))
                {
                    sArmed = -1;
                }
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(code->name);
                if (on)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(kWarn, "entered");
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("  %s", spaced);

            if (code->toggles)
            {
                char note[512];
                snprintf(note, sizeof(note), "%s %s", code->what, code->lasts);
                Indented(note);
            }
            else
            {
                // "For good ... cannot be undone" is a warning, and drawn as one.
                Indented(code->what);
                Indented(code->lasts, true);
            }
            // The double points code: its bit goes off again, its effect does not.
            if (code->flag == 0x40000 && !on && status.pointsMultiplier > 1)
            {
                Indented("Points are doubled now, and stay doubled until you quit the game.",
                         true);
            }
            ImGui::PopID();
        }
    }

    void GameExtras(const iCheatsStatus& status, bool usable)
    {
        ImGui::Spacing();
        ImGui::TextUnformatted("Extras screen");
        if (status.inGame)
        {
            iGui::Note("The costumes and functions of the pause menu's Extras screen, which opens "
                       "them one by one as you collect treasure chests. You have %u. A click here "
                       "is the Extras screen's own switch. All of them go off when you go back to "
                       "the title screen, as in the original.",
                       (unsigned)status.chests);
        }
        else
        {
            iGui::Note("The costumes and functions of the pause menu's Extras screen, which opens "
                       "them one by one as you collect treasure chests. A click here is the Extras "
                       "screen's own switch. All of them go off when you go back to the title "
                       "screen, as in the original.");
        }

        const bool unlocked = iCheatsInForce(iCHEAT_UNLOCK_EXTRAS);
        const S32 rows = iCheatsExtraRows();
        for (S32 i = 0; i < iCheatExtraCount(); i++)
        {
            const iCheatExtra* extra = iCheatExtraAt(i);

            // Its row of the game's list, once SB04.INI has been read.
            S32 row = -1;
            S32 chests = 0;
            bool on = false;
            for (S32 r = 0; r < rows; r++)
            {
                S32 type, id, needed;
                bool rowOn;
                if (iCheatsExtraRow(r, &type, &id, &needed, &rowOn) && type == extra->type &&
                    id == extra->id)
                {
                    row = r;
                    chests = needed;
                    on = rowOn;
                    break;
                }
            }

            // An item that is on can always be switched off: one switched on with
            // "Extras without treasure chests" is not left stuck when that goes off.
            const bool locked = row >= 0 && (U32)chests > status.chests && !unlocked;
            ImGui::PushID(100 + i);
            ImGui::BeginDisabled(!usable || row < 0 || (locked && !on));
            bool box = on;
            if (ImGui::Checkbox(extra->name, &box))
            {
                iCheatsRequest(iCHEAT_REQUEST_EXTRA, row);
            }
            ImGui::EndDisabled();
            if (row >= 0)
            {
                ImGui::SameLine();
                if (locked && status.inGame)
                {
                    ImGui::TextColored(kWarn, "  needs %d treasure chests", (int)chests);
                }
                else
                {
                    ImGui::TextDisabled("  %d treasure chests", (int)chests);
                }
            }

            char note[512];
            snprintf(note, sizeof(note), "%s%s", extra->what,
                     extra->nextLevel ? " Shows from the next level you load." : "");
            Indented(note);
            ImGui::PopID();
        }
    }

    void PortCheats(const iCheatsStatus& status, bool following)
    {
        ImGui::SeparatorText("Port cheats");
        iGui::Note("Cheats this port adds. Each one is off unless you turn it on here, is kept "
                   "in the settings file and never in a save, and applies at once.");

        ImGui::BeginDisabled(following);
        if (ImGui::Button("Turn all port cheats off"))
        {
            iCheatsAllPortOff();
            iGui::SaveNow();
        }

        for (S32 i = 0; i < iCHEAT_PORT_COUNT; i++)
        {
            const iCheatPortInfo* info = iCheatPortAt((iCheatPort)i);
            // While a host is followed the box shows what is in force.
            bool on = following ? iCheatsInForce((iCheatPort)i) :
                                  iConfigGetBool(info->key, FALSE) != 0;
            ImGui::PushID(200 + i);
            if (ImGui::Checkbox(info->name, &on))
            {
                iGui::Commit(info->key, on ? "on" : "off");
                iCheatsApply();
            }
            Indented(info->what);
            if (info->caution != NULL)
            {
                Indented(info->caution, true);
            }
            if (i == iCHEAT_FLY && status.inGame && status.flying)
            {
                Indented(on ? "You are flying." :
                              "You are still flying: hold white and press black to land.",
                         true);
            }
            ImGui::PopID();
        }
        ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::BeginDisabled(following || !status.inGame || status.health == 0 ||
                             status.health >= status.maxHealth);
        if (ImGui::Button("Refill health"))
        {
            iCheatsRequest(iCHEAT_REQUEST_REFILL, 0);
        }
        ImGui::EndDisabled();
        if (status.inGame)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("  %u of %u", (unsigned)status.health, (unsigned)status.maxHealth);
        }
        Indented("Once: the character you are playing gets full health. Works while a level is "
                 "running.");
    }
} // namespace

void iGui::DrawCheats()
{
    iCheatsStatus status;
    iCheatsGetStatus(&status);
    const bool following = iCheatsFollowing();
    const bool usable = status.inGame && !following;

    if (following)
    {
        Warn("Co-op: the host's cheats are in force in this game. The switches here are the "
             "host's to change.");
        ImGui::Spacing();
    }

    ImGui::SeparatorText("The game's own cheats");
    iGui::Note("The original game's cheats, done by the game's own code: they are saved, and "
               "forgotten, exactly as on the console.");
    if (!status.inGame)
    {
        Warn(InLauncher() ? "These work while a level is running: start the game, load or begin "
                            "a save, then open this window there." :
                            "These work while a level is running: load or begin a save first.");
    }
    GameCodes(status, usable);
    GameExtras(status, usable);

    ImGui::Spacing();
    PortCheats(status, following);
}
