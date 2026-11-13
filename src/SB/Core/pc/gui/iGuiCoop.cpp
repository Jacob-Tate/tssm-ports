// The Co-op tab and the co-op HUD layer (docs/MULTIPLAYER.md 4.5), on the
// co-op service's own interface (iCoop.h).
//
// The tab hosts or joins, shows the join code, the address to forward and
// what is happening, and the partner. The HUD layer is drawn on every frame
// while co-op is on, even with the overlay closed: the partner's name above
// their head and short notices. It never takes input.
//
// The partner's name is the only text in here that came over the network. It
// has passed iNet::name_valid (letters, digits, space, _ . -) and is drawn
// with TextUnformatted / AddText only, never as a format.

#include "imgui.h"
#include "imgui_internal.h" // the code pad: the controller's cursor and its input source

#include <SDL3/SDL.h>

#include "gui/iGui.h"
#include "gui/iGuiInternal.h"

#include "iCoop.h"
#include "iCoopLocal.h"
#include "iNetProto.h"
#ifdef __SWITCH__
#include "iNetSock.h"
#endif
#include "iScreen.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    const ImVec4 kRed(1.0f, 0.47f, 0.43f, 1.0f);
    const ImVec4 kYellow(0.98f, 0.82f, 0.31f, 1.0f);
    const ImVec4 kGreen(0.55f, 0.85f, 0.55f, 1.0f);

    // A touch screen's words and sizes: a phone, or TSSM_GUI_TOUCH=1 on a desktop.
    bool TouchWords()
    {
        return (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_IsTouchScreen) != 0;
    }

    bool sInit;
    S32 sMode; // 1: the game started in join mode (Join by address opens)
    char sName[iNet::kMaxName + 1];
    S32 sPort;
    bool sUseCode = true;
    char sCode[16];
    char sOwnPassword[iNet::kMaxPassword + 1];
    char sAddress[64];
    char sJoinPassword[iNet::kMaxPassword + 1];
    char sWhy[200];
    // The code a running host was started with, to show and copy.
    char sActiveCode[16];
    bool sHostForm; // "Host over the internet": today's form is open
    bool sHostedLan; // the running host came from "Host on this network"
    bool sFromList; // the running join came from the list's code entry
    bool sByAddress; // open "Join by address" (the game started in join mode)
    bool sCodeHudLogged; // the HUD's join code was logged as shown
    U64 sTabAt; // when the tab was last drawn (SDL_GetTicks), to see it open
    bool sTabDrawn; // since the last iGui::CoopFrame
    S32 sTabMissed; // frames in a row without the tab

    void Init()
    {
        if (sInit)
        {
            return;
        }
        sInit = true;
        snprintf(sName, sizeof(sName), "%s", iCoopDefaultName());
        sPort = iCoopDefaultPort();
        snprintf(sAddress, sizeof(sAddress), "%s", iCoopDefaultAddress());
        iCoopNewJoinCode(sCode);
        snprintf(sActiveCode, sizeof(sActiveCode), "%s", iCoopStartupJoinCode());
        sMode = iCoopNeedsPassword() ? 1 : 0;
        iCoopStatus st;
        iCoopGetStatus(&st);
        if (st.role == ICOOP_ROLE_JOIN)
        {
            sMode = 1;
        }
        sByAddress = sMode == 1;
    }

    // Typing filters: only what the field can hold.
    int NameFilter(ImGuiInputTextCallbackData* data)
    {
        ImWchar c = data->EventChar;
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' ||
                  c == '_' || c == '.' || c == '-';
        return ok ? 0 : 1;
    }

    int AddressFilter(ImGuiInputTextCallbackData* data)
    {
        ImWchar c = data->EventChar;
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == '.' ||
                  c == ':' || c == '[' || c == ']';
        return ok ? 0 : 1;
    }

    int PasswordFilter(ImGuiInputTextCallbackData* data)
    {
        ImWchar c = data->EventChar;
        return (c >= 32 && c < 127) ? 0 : 1;
    }

    const char* PhaseText(const iCoopStatus& st)
    {
        switch (st.phase)
        {
        case ICOOP_PHASE_OFF: return "Off";
        case ICOOP_PHASE_HASHING: return "Checking your game files...";
        case ICOOP_PHASE_WAITING: return "Waiting for a player";
        case ICOOP_PHASE_CONNECTING: return "Connecting...";
        case ICOOP_PHASE_CHECKING: return "Checking the password...";
        case ICOOP_PHASE_CONNECTED: return "Connected";
        case ICOOP_PHASE_REFUSED: return "Refused";
        case ICOOP_PHASE_NO_ANSWER: return "No answer from the host";
        case ICOOP_PHASE_ENDED: return "Disconnected";
        case ICOOP_PHASE_ERROR: return "Could not start";
        case ICOOP_PHASE_RECONNECTING: return "Connection lost: joining again...";
        }
        return "?";
    }

    ImVec4 PhaseColor(const iCoopStatus& st)
    {
        switch (st.phase)
        {
        case ICOOP_PHASE_CONNECTED: return kGreen;
        case ICOOP_PHASE_REFUSED:
        case ICOOP_PHASE_NO_ANSWER:
        case ICOOP_PHASE_ENDED:
        case ICOOP_PHASE_ERROR: return kRed;
        case ICOOP_PHASE_OFF: return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        default: return kYellow;
        }
    }

    // A reason (constant text of the port's, lower case, no full stop) as a
    // sentence, in `color`.
    void Sentence(const char* prefix, const char* text, const ImVec4& color)
    {
        char line[512];
        snprintf(line, sizeof(line), "%s%s.", prefix, text);
        if (prefix[0] == '\0' && line[0] >= 'a' && line[0] <= 'z')
        {
            line[0] = (char)(line[0] - 'a' + 'A');
        }
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", line);
        ImGui::PopStyleColor();
    }

    void SceneText(U32 id, char out[5])
    {
        for (int i = 0; i < 4; i++)
        {
            char c = (char)(id >> (24 - 8 * i));
            out[i] = ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) ? c : '?';
        }
        out[4] = '\0';
    }

    // M5: the connection's quality in words and its colour.
    const char* QualityText(iCoopQuality q)
    {
        switch (q)
        {
        case ICOOP_QUALITY_GOOD: return "good";
        case ICOOP_QUALITY_FAIR: return "fair";
        case ICOOP_QUALITY_POOR: return "poor";
        case ICOOP_QUALITY_INTERRUPTED: return "interrupted";
        default: return "measuring...";
        }
    }

    ImVec4 QualityColor(iCoopQuality q)
    {
        switch (q)
        {
        case ICOOP_QUALITY_GOOD: return kGreen;
        case ICOOP_QUALITY_FAIR: return kYellow;
        case ICOOP_QUALITY_POOR:
        case ICOOP_QUALITY_INTERRUPTED: return kRed;
        default: return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        }
    }

    // The two codes both players compare when a join is refused: the
    // version (the same on every device of one co-op release) and the game
    // files (known once this game has hosted or joined).
    void DrawVersions(bool explain)
    {
        char version[9], files[9];
        U32 count = 0;
        iCoopVersionCode(version);
        ImGui::TextDisabled("Co-op version: %s", version);
        ImGui::SameLine(0.0f, ImGui::GetFontSize() * 2.0f);
        if (iCoopGameFilesCode(files, &count))
        {
            ImGui::TextDisabled("Game files: %s, %u files", files, (unsigned)count);
        }
        else
        {
            ImGui::TextDisabled(
                "Game files: checked when the list searches, or when you host or join");
        }
        if (explain)
        {
            ImGui::TextDisabled("Both players' codes must be the same.");
        }
    }

    void DrawPartner(const iCoopStatus& st)
    {
        ImGui::SeparatorText("Partner");
        if (!st.connected)
        {
            ImGui::TextDisabled("Nobody yet.");
            return;
        }
        if (ImGui::BeginTable("partner", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Name");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(st.partnerName);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Ping");
            ImGui::TableSetColumnIndex(1);
            if (st.pingMs < 0)
            {
                ImGui::TextDisabled("measuring...");
            }
            else
            {
                ImVec4 c = st.pingMs < 120 ? kGreen : (st.pingMs < 250 ? kYellow : kRed);
                ImGui::TextColored(c, "%d ms", (int)st.pingMs);
            }

            // M5: the connection's quality: ping and lost packets.
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Connection");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(QualityColor(st.quality), "%s", QualityText(st.quality));
            if (st.lossInPct >= 0 || st.lossOutPct >= 0)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(lost: %d%% coming in, %d%% going out)", st.lossInPct < 0 ? 0 : (int)st.lossInPct,
                                    st.lossOutPct < 0 ? 0 : (int)st.lossOutPct);
            }
            if (st.quality == ICOOP_QUALITY_INTERRUPTED)
            {
                ImGui::SameLine();
                ImGui::TextColored(kRed, "- nothing heard for %u s", (unsigned)(st.partnerSilentMs / 1000));
            }

            if (st.partnerScene != 0)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Playing");
                ImGui::TableSetColumnIndex(1);
                const char* model = iCoopModelName(st.partnerModel);
                ImGui::TextUnformatted(model[0] != '\0' ? model
                                                         : (st.partnerCharacter == iNet::CHAR_PATRICK ? "Patrick"
                                                                                                       : "SpongeBob"));

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Level");
                ImGui::TableSetColumnIndex(1);
                char scene[5];
                SceneText(st.partnerScene, scene);
                ImGui::Text("%s  (%s)", st.partnerLevelName[0] != '\0' ? st.partnerLevelName : "?", scene);
                ImGui::SameLine();
                if (st.partnerStallMs >= 1000)
                {
                    // docs/MULTIPLAYER.md 5.11: its window held, a hitch, a load.
                    ImGui::TextColored(kYellow, "- their game is not running; waiting (%u s)",
                                       (unsigned)(st.partnerStallMs / 1000));
                }
                else if (!st.partnerFresh)
                {
                    ImGui::TextColored(kYellow, "- not heard from for a moment");
                }
                else if (st.partnerInMyLevel)
                {
                    ImGui::TextColored(kGreen, "- in your level");
                }
                else
                {
                    ImGui::TextDisabled("- in another level");
                }
            }
            ImGui::EndTable();
        }

        // M2: the shared world (docs/MULTIPLAYER.md 5).
        if (st.worldShared)
        {
            ImGui::TextColored(kGreen, "Shared world: %d enemies, platforms and breakables", (int)st.worldThings);
        }
        else if (st.partnerInMyLevel && st.partnerFresh)
        {
            ImGui::TextDisabled("Shared world: starting...");
        }
        else
        {
            ImGui::TextDisabled("Shared world: when you are both in the same level");
        }
        if (st.worldMismatch > 0)
        {
            ImGui::TextColored(kYellow, "The host's level differs from yours (%d updates ignored)", (int)st.worldMismatch);
        }
        if (st.kbOut >= 0.0f)
        {
            ImGui::TextDisabled("Traffic: %.1f KB/s out, %.1f KB/s in", st.kbOut, st.kbIn);
        }
        if (st.role == ICOOP_ROLE_JOIN && st.partnerScene != 0 && !st.partnerInMyLevel && st.partnerFresh)
        {
            if (st.following)
            {
                ImGui::TextColored(kYellow, "Going to the host's level...");
            }
            else if (ImGui::Button("Go to the host's level"))
            {
                iCoopGoToHost();
            }
        }
    }

    // M4 (docs/MULTIPLAYER.md 7.4): this player's character and costumes, and
    // the partner's. Reachable with a controller (Back+Start opens the
    // overlay; the d-pad moves through it), and LB in play does the same.
    void DrawCustomize(const iCoopStatus& st)
    {
        ImGui::SeparatorText("Customize");
        iCoopLookView v;
        iCoopGetLookView(&v);

        ImGui::TextUnformatted("Character:");
        S32 setting = iCoopCharacterSetting();
        ImGui::SameLine();
        if (ImGui::RadioButton("Automatic", setting == ICOOP_CHAR_AUTO))
        {
            iCoopSetCharacterSetting(ICOOP_CHAR_AUTO);
        }
        iGui::HelpMarker("The guest takes the other character than the host's when it joins and when a level "
                         "starts. The host keeps the level's own.");
        ImGui::SameLine();
        if (ImGui::RadioButton("SpongeBob", setting == ICOOP_CHAR_SPONGEBOB))
        {
            iCoopSetCharacterSetting(ICOOP_CHAR_SPONGEBOB);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Patrick", setting == ICOOP_CHAR_PATRICK))
        {
            iCoopSetCharacterSetting(ICOOP_CHAR_PATRICK);
        }
        if (v.inLevel)
        {
            ImGui::Text("You play: %s", v.modelName != NULL && v.modelName[0] != '\0' ? v.modelName : "?");
            if (v.switchWhyNot != NULL)
            {
                ImGui::SameLine();
                ImGui::TextColored(kYellow, "(switching: %s)", v.switchWhyNot);
            }
        }

        const char* who[2] = { "SpongeBob's costume", "Patrick's costume" };
        for (S32 ch = 0; ch < 2; ch++)
        {
            S32 cur = iCoopCostumeSetting(ch);
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
            ImGui::PushID(ch);
            if (ImGui::BeginCombo(who[ch], iCoopCostumeName(ch, cur)))
            {
                for (S32 c = 0; c < 5; c++)
                {
                    bool offered = iCoopCostumeOffered(ch, c);
                    char label[64];
                    snprintf(label, sizeof(label), "%s%s", iCoopCostumeName(ch, c), offered ? "" : " (locked)");
                    if (!offered)
                    {
                        ImGui::BeginDisabled();
                    }
                    if (ImGui::Selectable(label, c == cur) && offered)
                    {
                        iCoopSetCostumeSetting(ch, c);
                    }
                    if (!offered)
                    {
                        ImGui::EndDisabled();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
            if (v.inLevel && v.effective[ch] != cur && cur != 0)
            {
                ImGui::SameLine();
                ImGui::TextColored(kYellow, "(locked in this save)");
            }
        }
#ifdef __SWITCH__
        const char* inPlay =
            "In play: tap L to switch between SpongeBob and Patrick, hold it to change the costume.";
#else
        // The on-screen controls have no LB.
        const char* inPlay =
            TouchWords() ?
                "In play with a controller: tap LB to switch between SpongeBob and Patrick, hold it to change "
                "the costume. With the touch controls, choose them here." :
                "In play: tap LB (Z on the keyboard) to switch between SpongeBob and Patrick, hold it to change "
                "the costume.";
#endif
        iGui::Note(
            "A costume is put on when a level loads, as in the game's own extras menu: a new one is worn "
            "from the next level. Costumes your save has unlocked are offered (the extras screen's, and the "
            "cheats' while they are on). %s",
            inPlay);
        if (st.connected)
        {
            if (v.partnerKnown)
            {
                ImGui::Text("%s's costumes: SpongeBob %s, Patrick %s", st.partnerName,
                            iCoopCostumeName(0, v.partnerCostume[0]), iCoopCostumeName(1, v.partnerCostume[1]));
                if (v.partnerFallback)
                {
                    // 7.5: the partner's outfit loads with a level this game
                    // loads knowing it (the first level of a session, and one
                    // after a costume change, show another of the same
                    // character).
                    ImGui::TextColored(kYellow, "%s's costume shows here from the next level", st.partnerName);
                }
            }
            else
            {
                ImGui::TextDisabled("The other player's costumes: not heard yet");
            }
        }
    }

    // The Options switches: on at once, and kept in config.ini ([coop]).
    void SetOption(const char* key, bool on)
    {
        char full[32];
        snprintf(full, sizeof(full), "coop.%s", key);
        if (strcmp(key, "nametag") == 0)
        {
            iCoopSetNameTagEnabled(on);
        }
        else if (strcmp(key, "follow") == 0)
        {
            iCoopSetFollowEnabled(on);
        }
        else if (strcmp(key, "reconnect") == 0)
        {
            iCoopSetReconnectEnabled(on);
        }
        else if (strcmp(key, "quality") == 0)
        {
            iCoopSetQualityHudEnabled(on);
        }
        else if (strcmp(key, "show_code") == 0)
        {
            iCoopSetShowCodeEnabled(on);
        }
        else
        {
            return;
        }
        iGui::Commit(full, on ? "on" : "off");
    }

    // The joining view shows Try again (and Back) in these phases.
    bool TryAgainShown(const iCoopStatus& st)
    {
        return st.role == ICOOP_ROLE_JOIN &&
               (st.phase == ICOOP_PHASE_REFUSED || st.phase == ICOOP_PHASE_NO_ANSWER ||
                st.phase == ICOOP_PHASE_ENDED || st.phase == ICOOP_PHASE_ERROR);
    }

    // M5: the name, port and address are kept in config.ini for the next
    // time (never the password). `lan`: "Host on this network" (a new code,
    // visible); otherwise the internet form's host, not visible until ticked.
    void StartHost(bool lan)
    {
        if (lan)
        {
            iCoopNewJoinCode(sCode);
            sUseCode = true;
        }
        const char* password = sUseCode ? sCode : sOwnPassword;
        iCoopSetLanVisible(lan);
        if (iCoopHost((U16)sPort, password, sName, iCoopDefaultLoopback(), sWhy, sizeof(sWhy)))
        {
            snprintf(sActiveCode, sizeof(sActiveCode), "%s", sUseCode ? sCode : "");
            sWhy[0] = '\0';
            sHostForm = false;
            sHostedLan = lan;
            sCodeHudLogged = false;
            iGui::Commit("coop.name", sName, false);
            iGui::CommitInt("coop.port", sPort);
        }
    }

    // The address is kept only once the session is up (the host proved the
    // password): iCoop.cpp writes it then, from the parsed numbers.
    void StartJoin()
    {
        if (iCoopJoin(sAddress, sJoinPassword, sName, sWhy, sizeof(sWhy), true))
        {
            sWhy[0] = '\0';
            sFromList = false;
            iGui::Commit("coop.name", sName);
        }
    }

    // --- the home network (docs/MULTIPLAYER.md 14.3) ------------------------------

    // The join-code alphabet (iCoopNewJoinCode), 4 rows of 8 on the code pad.
    const char kPadKeys[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    const size_t kPadMax = 12; // a join code's characters; longer passwords are typed

    // The game picked from the list, by its address, and the code entry for it.
    char sPicked[48];
    char sPickedName[iNet::kMaxName + 1];
    U8 sPickedPlatform;
    bool sEntry;
    bool sPad; // the code pad is shown
    bool sPadFocus; // put the controller's cursor on its first key
    bool sCodeFocus; // put the keyboard in the code field
    char sEntryCode[iNet::kMaxPassword + 1];
    // The tab's searches: one starts when the tab opens with co-op off.
    iCoopRole sTabRole = ICOOP_ROLE_OFF;
    bool sSearchAsked, sSawFinding;
    bool sSearchPending; // asked, not yet seen running (the files are checked first)
    U64 sFindingAt, sSearchAt;
    bool sPadScroll; // bring the pad into view once it opens
    float sPadTop = -1.0f; // where the pad began in the tab last frame; -1: not shown
    bool sCodeSetByTest; // a test action wrote the code: the field lets go of its own copy
    // Tests (padview): the pad's keys to the line under them as last drawn,
    // and the part of the tab on screen.
    float sPadShownTop, sPadShownBottom, sTabShownTop, sTabShownBottom;
    bool sListHadGames; // this search listed a game, so its end is said as such
    bool sChecklistSeen; // "Join by address" was opened with the checklist
    S32 sEntryFrame = -1; // the code entry opened in this frame: its Start is not the pad's
    bool sScrollToStatus; // a join's refusal: bring it into view
    iCoopPhase sSeenPhase;

    // Tests: the list's rows as last drawn, and a click (or tap) on one sent
    // as SDL mouse events: the position and the press, then the release.
    struct RowRect
    {
        char name[iNet::kMaxName + 1];
        ImVec2 min, max;
    } sRows[8];
    S32 sRowCount;
    S32 sClickStep;
    ImVec2 sClickAt;
    bool sClickTouch;

    // Tests: a virtual controller pressing buttons, 4 frames down and 4 up.
    SDL_Joystick* sVPad;
    SDL_JoystickID sVPadId;
    S32 sVPadButtons[48];
    S32 sVPadCount, sVPadPos, sVPadTick;

    void StartSearch()
    {
        sSearchAsked = iCoopFindGames(true);
        sSearchPending = sSearchAsked;
        sSearchAt = SDL_GetTicks();
        sSawFinding = false;
        sFindingAt = 0;
        sListHadGames = false;
    }

    void CodeShown(const char* code, char* out, size_t outSize)
    {
        size_t n = 0, k = 0;
        for (const char* p = code; *p != '\0' && n + 2 < outSize; p++)
        {
            char c = *p;
            if (c == '-' || c == ' ')
            {
                continue;
            }
            if (c >= 'a' && c <= 'z')
            {
                c = (char)(c - 'a' + 'A');
            }
            if (k > 0 && k % 4 == 0)
            {
                out[n++] = '-';
            }
            out[n++] = c;
            k++;
        }
        out[n] = '\0';
    }

    void PadType(char c)
    {
        size_t n = strlen(sEntryCode);
        if (n < kPadMax && n + 1 < sizeof(sEntryCode))
        {
            sEntryCode[n] = c;
            sEntryCode[n + 1] = '\0';
        }
    }

    void PadDelete()
    {
        size_t n = strlen(sEntryCode);
        while (n > 0 && (sEntryCode[n - 1] == '-' || sEntryCode[n - 1] == ' '))
        {
            n--;
        }
        if (n > 0)
        {
            n--;
        }
        sEntryCode[n] = '\0';
    }

    // The code field asks the system keyboard for a visible password (no
    // learning or suggestions), capitals and no autocorrect. The ImGui
    // backend does not start text input again while it is active.
    void StartCodeKeyboard()
    {
        SDL_Window* w = iGui::Window();
        if (w == NULL)
        {
            return;
        }
        SDL_PropertiesID props = SDL_CreateProperties();
        if (props == 0)
        {
            return;
        }
        SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER,
                              SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_VISIBLE);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER,
                              SDL_CAPITALIZE_LETTERS);
        SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
        SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_MULTILINE_BOOLEAN, false);
        if (SDL_TextInputActive(w))
        {
            SDL_StopTextInput(w);
        }
        SDL_StartTextInputWithProperties(w, props);
        SDL_DestroyProperties(props);
    }

    // A password field: the system keyboard hides what is typed and learns
    // nothing of it.
    void StartPasswordKeyboard()
    {
        SDL_Window* w = iGui::Window();
        SDL_PropertiesID props = w != NULL ? SDL_CreateProperties() : 0;
        if (props == 0)
        {
            return;
        }
        SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER,
                              SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER, SDL_CAPITALIZE_NONE);
        SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
        SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_MULTILINE_BOOLEAN, false);
        if (SDL_TextInputActive(w))
        {
            SDL_StopTextInput(w);
        }
        SDL_StartTextInputWithProperties(w, props);
        SDL_DestroyProperties(props);
    }

    // Back to the defaults for the next field (ImGui starts it again).
    void StopCodeKeyboard()
    {
        SDL_Window* w = iGui::Window();
        if (w != NULL && SDL_TextInputActive(w))
        {
            SDL_StopTextInput(w);
        }
    }

    // A controller's or a finger's last use: the pad opens by itself then.
    bool PadFriendly()
    {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        return (g.NavCursorVisible && g.NavInputSource == ImGuiInputSource_Gamepad) ||
               ImGui::GetIO().MouseSource == ImGuiMouseSource_TouchScreen;
    }

    // The code entry for the picked game (sPicked and its name and device).
    void OpenPickedEntry(bool pad)
    {
        printf("tssm: gui: coop tab: the code entry for %s's game%s\n", sPickedName,
               pad ? ", with the code pad" : "");
        fflush(stdout);
        sEntry = true;
        sEntryFrame = ImGui::GetFrameCount();
        sEntryCode[0] = '\0';
        sWhy[0] = '\0';
        sPad = pad;
        sPadScroll = pad;
        sPadFocus = pad && ImGui::GetCurrentContext()->NavInputSource == ImGuiInputSource_Gamepad;
        sCodeFocus = !pad;
    }

    void OpenEntry(const iCoopFoundGame& g, bool pad)
    {
        snprintf(sPicked, sizeof(sPicked), "%s", g.address);
        snprintf(sPickedName, sizeof(sPickedName), "%s", g.name);
        sPickedPlatform = g.platform;
        iCoopKeepFound(g.address);
        OpenPickedEntry(pad);
    }

    void CloseEntry()
    {
        sEntry = false;
        sPad = false;
        sPadTop = -1.0f;
        memset(sEntryCode, 0, sizeof(sEntryCode));
        StopCodeKeyboard();
    }

    // The code entry's Join. The address form gets the picked address and
    // the code; Try again opens the code entry again (RetryPicked).
    void JoinPicked()
    {
        sWhy[0] = '\0';
        printf("tssm: gui: coop tab: joining %s's game from the list\n", sPickedName);
        fflush(stdout);
        if (iCoopJoinFound(sPicked, sEntryCode, sName, sWhy, sizeof(sWhy)))
        {
            sWhy[0] = '\0';
            snprintf(sAddress, sizeof(sAddress), "%s", sPicked);
            snprintf(sJoinPassword, sizeof(sJoinPassword), "%s", sEntryCode);
            iGui::Commit("coop.name", sName);
            CloseEntry();
            sFromList = true;
        }
    }

    // Try again after a list join the host refused: the code entry again,
    // empty, so a wrong code is not sent twice (each counts toward the host's
    // lockout). Other failures try again with the same code.
    bool RetryOpensEntry(const iCoopStatus& st)
    {
        return sFromList && st.phase == ICOOP_PHASE_REFUSED;
    }

    void RetryPicked()
    {
        iCoopStop();
        sWhy[0] = '\0';
        OpenPickedEntry(PadFriendly());
    }

    // The row's reason in words, when it is not Ready.
    void RowReason(const iCoopFoundGame& g, char* out, size_t outSize)
    {
        out[0] = '\0';
        char mine[9], files[9];
        U32 count = 0;
        if (g.newer)
        {
            snprintf(
                out, outSize, "%s",
                "A newer co-op version: both need the same co-op version. Update this game to join it.");
        }
        else if (g.status == ICOOP_FOUND_VERSION)
        {
            iCoopVersionCode(mine);
            snprintf(out, outSize,
                     "Different version: both need the same co-op version (yours %s, theirs %s).",
                     mine, g.version);
        }
        else if (g.status == ICOOP_FOUND_FILES)
        {
            if (iCoopGameFilesCode(files, &count))
            {
                snprintf(
                    out, outSize,
                    "Different game files: both need the same release and region, and the same mods (or "
                    "none). Game files codes: yours %s, theirs %s.",
                    files, g.files);
            }
            else
            {
                snprintf(
                    out, outSize, "%s",
                    "Different game files: both need the same release and region, and the same mods (or "
                    "none).");
            }
        }
        else if (g.hasPlayer)
        {
            snprintf(out, outSize, "%s", "This game already has two players. Co-op is for two.");
        }
    }

    void DrawPad()
    {
        const bool touch = TouchWords();
        float key = ImGui::GetFontSize() * (touch ? 2.6f : 2.1f);
        // The whole pad within the tab's height (4.9 keys of keys, 5 gaps of
        // 0.14 key, and the line under them), so its last row is never out of
        // reach on a short screen.
        const float room = ImGui::GetWindowHeight() - ImGui::GetTextLineHeightWithSpacing() -
                           2.0f * ImGui::GetStyle().WindowPadding.y;
        if (key * 5.6f > room)
        {
            key = fmaxf(room / 5.6f, ImGui::GetFontSize() * 1.6f);
        }
        const float gap = key * 0.14f;

        // Something above it grew or shrank while it is open (a game listed
        // once the search after Try again answers): the pad stays where it
        // was on screen.
        const float top = ImGui::GetCursorPosY();
        if (!sPadScroll && sPadTop >= 0.0f && fabsf(top - sPadTop) > 0.5f)
        {
            ImGui::SetScrollY(ImGui::GetScrollY() + (top - sPadTop));
        }
        sPadTop = top;
        sPadShownTop = ImGui::GetCursorScreenPos().y;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
        ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * 1.3f);
        for (int i = 0; i < 32; i++)
        {
            char label[2] = { kPadKeys[i], '\0' };
            ImGui::PushID(i);
            if (ImGui::Button(label, ImVec2(key, key)))
            {
                PadType(kPadKeys[i]);
            }
            if (i == 0 && sPadFocus)
            {
                ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
                ImGui::SetNavCursorVisible(true);
                sPadFocus = false;
            }
            ImGui::PopID();
            if (i % 8 != 7)
            {
                ImGui::SameLine();
            }
        }
        ImGui::PopFont();
        const ImVec2 wide(key * 2.0f + gap, key * 0.9f);
        if (ImGui::Button("Delete", wide))
        {
            PadDelete();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear", wide))
        {
            sEntryCode[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button("Keyboard", wide))
        {
            sCodeFocus = true;
        }
        ImGui::SameLine();
        bool join = ImGui::Button("Join##pad", wide);
        ImGui::PopStyleVar();
#ifdef __SWITCH__
        ImGui::TextDisabled("B types, A deletes, X opens the keyboard, + joins.");
#else
        if (touch && !SDL_HasGamepad())
        {
            ImGui::TextDisabled("Tap the keys, then Join. Keyboard opens the phone's keyboard.");
        }
        else
        {
            ImGui::TextDisabled(
                "Controller: A types, B deletes, Y opens the keyboard, Start joins.");
        }
#endif
        sPadShownBottom = ImGui::GetItemRectMax().y;
        const ImRect shown = ImGui::GetCurrentWindow()->InnerClipRect;
        sTabShownTop = shown.Min.y;
        sTabShownBottom = shown.Max.y;
        if (sPadScroll)
        {
            ImGui::SetScrollHereY(1.0f);
            sPadScroll = false;
        }

        // B deletes (and leaves the pad on an empty code), Y is the keyboard,
        // Start joins. Routed here, so ImGui's own B does not drop the cursor.
        if (ImGui::Shortcut(ImGuiKey_GamepadFaceRight, ImGuiInputFlags_RouteGlobal))
        {
            if (sEntryCode[0] == '\0')
            {
                sPad = false;
            }
            else
            {
                PadDelete();
            }
        }
        if (ImGui::Shortcut(ImGuiKey_GamepadFaceUp, ImGuiInputFlags_RouteGlobal))
        {
            sCodeFocus = true;
        }
        // Not the Start that opened the entry from the list.
        if (ImGui::Shortcut(ImGuiKey_GamepadStart, ImGuiInputFlags_RouteGlobal) &&
            !ImGui::IsKeyDown(ImGuiKey_GamepadBack) && ImGui::GetFrameCount() != sEntryFrame)
        {
            join = true;
        }
        if (join)
        {
            JoinPicked();
        }
    }

    void DrawEntry()
    {
        char head[96];
        snprintf(head, sizeof(head), "Join %s's game (%s)", sPickedName,
                 iCoopPlatformName(sPickedPlatform));
        ImGui::SeparatorText(head);
        ImGui::TextUnformatted("Type the join code shown on the host's screen.");
        char shown[24];
        CodeShown(sEntryCode, shown, sizeof(shown));
        ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * 2.2f);
        if (shown[0] != '\0')
        {
            ImGui::TextUnformatted(shown);
        }
        else
        {
            ImGui::TextDisabled("XXXX-XXXX-XXXX");
        }
        ImGui::PopFont();

        if (sCodeFocus)
        {
            ImGui::SetKeyboardFocusHere();
            sCodeFocus = false;
        }
        if (sCodeSetByTest)
        {
            // A field being edited shows its own copy of the text and would
            // keep it: it lets go, as a tap on the pad makes it.
            sCodeSetByTest = false;
            if (ImGui::GetActiveID() == ImGui::GetID("Join code"))
            {
                ImGui::ClearActiveID();
                StopCodeKeyboard();
            }
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
        bool enter = ImGui::InputText("Join code", sEntryCode, sizeof(sEntryCode),
                                      ImGuiInputTextFlags_CallbackCharFilter |
                                          ImGuiInputTextFlags_EnterReturnsTrue,
                                      PasswordFilter);
        if (ImGui::IsItemActivated())
        {
            StartCodeKeyboard();
        }
        if (ImGui::IsItemDeactivated())
        {
            StopCodeKeyboard();
        }
        iGui::HelpMarker(
            "Capitals, spaces and dashes do not matter; O, I and L count as 0, 1 and 1.");
        ImGui::SameLine();
        if (ImGui::SmallButton(sPad ? "Hide the code pad" : "Code pad"))
        {
            sPad = !sPad;
            sPadScroll = sPad;
            sPadFocus =
                sPad && ImGui::GetCurrentContext()->NavInputSource == ImGuiInputSource_Gamepad;
        }
        if (sPad)
        {
            DrawPad();
        }
        else
        {
            sPadTop = -1.0f;
        }
        ImGui::Spacing();
        // The pad has its own Join.
        if ((!sPad && ImGui::Button("Join")) || enter)
        {
            JoinPicked();
        }
        if (!sPad)
        {
            ImGui::SameLine();
        }
        if (ImGui::Button("Cancel"))
        {
            CloseEntry();
        }
        if (sWhy[0] != '\0')
        {
            Sentence("", sWhy, kRed);
        }
    }

    // "Games on this network": name, device, level, players, and Ready or
    // why not. Every text from the network passed iNet::name_valid or is a
    // scene ID of A-Z 0-9; all are drawn unformatted.
    void DrawList(const iCoopStatus& st)
    {
        iCoopFoundGame found[8];
        S32 n = iCoopGetFound(found, 8);
        sRowCount = n;
        U64 now = SDL_GetTicks();
        if (st.finding && !sSawFinding)
        {
            sSawFinding = true;
            sSearchPending = false;
            sFindingAt = now;
        }

        ImGui::TextUnformatted("Games on this network");
        ImGui::SameLine();
        if (st.finding)
        {
            ImGui::TextDisabled("searching... (%u s)", (unsigned)st.findLeftSec);
        }
        else if (sSearchAsked && !sSawFinding)
        {
            ImGui::TextDisabled("checking your game files...");
        }
        else
        {
            ImGui::TextDisabled("not searching");
        }

#ifdef __SWITCH__
        // Why the console has no home network, asked of the system (a local
        // query) every two seconds.
        static U64 sOfflineAt;
        static int sOffline;
        if (sOfflineAt == 0 || now - sOfflineAt >= 2000)
        {
            sOfflineAt = now;
            sOffline = iNet::net_offline_reason();
        }
        if (sOffline != 0)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
            ImGui::TextWrapped("%s",
                               sOffline == 1 ?
                                   "Wi-Fi is off (Airplane Mode, or Flight Mode, may be on). Turn "
                                   "it on in System Settings to play on your home network." :
                                   "This Switch is not connected to a network. Connect it to your "
                                   "home Wi-Fi in System Settings.");
            ImGui::PopStyleColor();
        }
#endif

        if (n > 0)
        {
            sListHadGames = true;
            const bool touch = TouchWords();
            const float rowH = ImGui::GetFontSize() * (touch ? 2.5f : 1.7f);
            const float pad = (rowH - ImGui::GetTextLineHeight()) * 0.5f;
            if (ImGui::BeginTable("games", 5,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                      ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.1f);
                ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthStretch, 0.8f);
                ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthStretch, 1.3f);
                ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthStretch, 0.9f);
                ImGui::TableSetupColumn("Can join", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                // Plain text headers: not stops for the controller's cursor.
                ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                for (int c = 0; c < 5; c++)
                {
                    ImGui::TableSetColumnIndex(c);
                    ImGui::TextUnformatted(ImGui::TableGetColumnName(c));
                }
                for (S32 i = 0; i < n; i++)
                {
                    const iCoopFoundGame& g = found[i];
                    bool picked = sPicked[0] != '\0' && strcmp(g.address, sPicked) == 0;
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
                    ImGui::TableSetColumnIndex(0);
                    char label[48];
                    snprintf(label, sizeof(label), "%s##row%d",
                             g.newer ? "(newer version)" : g.name, (int)i);
                    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
                    if (ImGui::Selectable(label, picked, ImGuiSelectableFlags_SpanAllColumns,
                                          ImVec2(0.0f, rowH)))
                    {
                        if (!picked)
                        {
                            printf("tssm: gui: coop tab: picked %s's game in the list\n",
                                   g.newer ? "a newer version" : g.name);
                            fflush(stdout);
                        }
                        snprintf(sPicked, sizeof(sPicked), "%s", g.address);
                        if (sEntry)
                        {
                            CloseEntry();
                        }
                    }
                    bool open = ImGui::IsItemHovered() &&
                                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                    if (i < 8)
                    {
                        snprintf(sRows[i].name, sizeof(sRows[i].name), "%s", g.name);
                        sRows[i].min = ImGui::GetItemRectMin();
                        sRows[i].max = ImGui::GetItemRectMax();
                    }
                    ImGui::PopStyleVar();
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad);
                    ImGui::TextUnformatted(g.newer ? "-" : iCoopPlatformName(g.platform));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad);
                    char level[64];
                    iCoopSceneName(g.scene, level, sizeof(level));
                    ImGui::TextUnformatted(level[0] != '\0' ? level : "not in a level");
                    ImGui::TableSetColumnIndex(3);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad);
                    if (g.newer)
                    {
                        ImGui::TextUnformatted("-");
                    }
                    else if (g.hasPlayer)
                    {
                        ImGui::TextColored(kYellow, "has a player");
                    }
                    else
                    {
                        ImGui::TextUnformatted("waiting");
                    }
                    ImGui::TableSetColumnIndex(4);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad);
                    if (g.newer || g.status == ICOOP_FOUND_VERSION)
                    {
                        ImGui::TextColored(kYellow, "Different version");
                    }
                    else if (g.status == ICOOP_FOUND_FILES)
                    {
                        ImGui::TextColored(kYellow, "Different game files");
                    }
                    else if (g.hasPlayer)
                    {
                        ImGui::TextColored(kYellow, "Full");
                    }
                    else
                    {
                        ImGui::TextColored(kGreen, "Ready");
                    }
                    if (open && !g.newer && st.role == ICOOP_ROLE_OFF)
                    {
                        OpenEntry(g, PadFriendly());
                    }
                }
                ImGui::EndTable();
            }
        }
        else if (st.lanNote[0] != '\0')
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
            ImGui::TextWrapped("%s", st.lanNote);
            ImGui::PopStyleColor();
        }
        else if (sSawFinding && !st.finding && sListHadGames)
        {
            ImGui::TextUnformatted(
                "The search ended after a minute: press Search again to look again.");
        }
        else if (sSawFinding && (now - sFindingAt >= 5000 || !st.finding))
        {
            // Nothing after 5 s: what to check (docs/LAN.txt says the same),
            // and the address field opened once beside it.
            if (!sChecklistSeen)
            {
                sChecklistSeen = true;
                sByAddress = true;
            }
            ImGui::TextUnformatted("No games found yet. Check that:");
            ImGui::BulletText(
                "both devices are on the same Wi-Fi or network (not a guest network);");
            ImGui::BulletText("the host is hosting, with \"Visible on this network\" ticked;");
            ImGui::BulletText("any VPN is off (a VPN app on a phone: pause it);");
            ImGui::BulletText(
                "a PC host lets the game through Windows Firewall on private networks;");
            ImGui::BulletText("a phone or Switch has Wi-Fi on.");
            ImGui::TextDisabled("Or join by address below: the host's Co-op tab shows it.");
        }
        else
        {
            ImGui::TextDisabled("Looking for games on this network...");
        }

        // Checked after the rows, so a pick made by a row this frame stays.
        const iCoopFoundGame* pick = NULL;
        for (S32 i = 0; i < n; i++)
        {
            if (sPicked[0] != '\0' && strcmp(found[i].address, sPicked) == 0)
            {
                pick = &found[i];
            }
        }
        if (sPicked[0] != '\0' && pick == NULL && !sEntry)
        {
            sPicked[0] = '\0';
        }
        if (pick != NULL)
        {
            char why[256];
            RowReason(*pick, why, sizeof(why));
            if (why[0] != '\0')
            {
                ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
                ImGui::TextWrapped("%s", why);
                ImGui::PopStyleColor();
            }
        }

        if (!sEntry)
        {
            // A to pick, Start to join (or the Join button).
            bool can = pick != NULL && !pick->newer;
            bool start = can &&
                         ImGui::Shortcut(ImGuiKey_GamepadStart, ImGuiInputFlags_RouteGlobal) &&
                         !ImGui::IsKeyDown(ImGuiKey_GamepadBack);
            ImGui::BeginDisabled(!can);
            if (ImGui::Button("Join") || start)
            {
                OpenEntry(*pick, PadFriendly());
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button("Search again"))
        {
            StartSearch();
        }
        if (sEntry)
        {
            DrawEntry();
        }
    }

    void DrawHostForm()
    {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        if (ImGui::InputInt("UDP port", &sPort, 0, 0))
        {
            sPort = sPort < 1024 ? 1024 : (sPort > 65535 ? 65535 : sPort);
        }
        iGui::HelpMarker(
            "The port your router forwards to this device. 2171 unless you changed it.");

        ImGui::Checkbox("Use a join code", &sUseCode);
        if (sUseCode)
        {
            ImGui::Text("Join code:  %s", sCode);
            ImGui::SameLine();
            if (ImGui::SmallButton("New code"))
            {
                iCoopNewJoinCode(sCode);
            }
#ifndef __SWITCH__
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy"))
            {
                ImGui::SetClipboardText(sCode);
            }
#endif
        }
        else
        {
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
            ImGui::InputText("Password", sOwnPassword, sizeof(sOwnPassword),
                             ImGuiInputTextFlags_Password | ImGuiInputTextFlags_CallbackCharFilter,
                             PasswordFilter);
            if (ImGui::IsItemActivated())
            {
                StartPasswordKeyboard();
            }
            if (ImGui::IsItemDeactivated())
            {
                StopCodeKeyboard();
            }
            if (strlen(sOwnPassword) < 8)
            {
                ImGui::TextColored(kYellow,
                                   "A short password is easy to guess; the join code is safer.");
            }
        }
        iGui::Note(
            "Your router must forward this UDP port to this device (HOSTING.txt). Give the other player "
            "your public address, the port and the code (or password) yourself: in person or in a "
            "message. Spaces, dashes and capitals do not matter when typing it.");
        ImGui::Spacing();
        if (ImGui::Button("Start hosting"))
        {
            StartHost(false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##host"))
        {
            sHostForm = false;
        }
    }

    void DrawForm(const iCoopStatus& st)
    {
        ImGui::TextUnformatted(
            "Play together on your home network or over the internet: one player hosts, the "
            "other joins.");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
        ImGui::InputText("Your name", sName, sizeof(sName), ImGuiInputTextFlags_CallbackCharFilter,
                         NameFilter);
        iGui::HelpMarker(
            "1 to 16 letters, digits, spaces, _ . or -. Shown above your head on the other "
            "player's screen, and in the list of games on this network.");

        ImGui::SeparatorText("Join a game");
        DrawList(st);
        ImGui::Spacing();
        if (sByAddress)
        {
            ImGui::SetNextItemOpen(true);
            sByAddress = false;
        }
        if (ImGui::TreeNode("Join by address (over the internet, or when the list stays empty)"))
        {
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
            ImGui::InputText("Host's address", sAddress, sizeof(sAddress), ImGuiInputTextFlags_CallbackCharFilter,
                             AddressFilter);
            iGui::HelpMarker(
                "Numbers only, with the port: the host's public address over the internet "
                "(203.0.113.7:2171), or on the same network one the host's Co-op tab shows under "
                "\"On this network\".");
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
            ImGui::InputText("Join code or password", sJoinPassword, sizeof(sJoinPassword),
                             ImGuiInputTextFlags_Password | ImGuiInputTextFlags_CallbackCharFilter, PasswordFilter);
            if (ImGui::IsItemActivated())
            {
                StartPasswordKeyboard();
            }
            if (ImGui::IsItemDeactivated())
            {
                StopCodeKeyboard();
            }
            if (ImGui::Button("Join##address"))
            {
                StartJoin();
            }
            ImGui::TreePop();
        }

        ImGui::SeparatorText("Host a game");
        if (ImGui::Button("Host on this network"))
        {
            StartHost(true);
        }
        iGui::HelpMarker(
            "One press: a new join code, shown large on your screen. Devices on the same Wi-Fi "
            "or network find this game in their list.");
        ImGui::SameLine();
        if (ImGui::Button("Host over the internet"))
        {
            sHostForm = !sHostForm;
        }
        iGui::HelpMarker(
            "For a player somewhere else: your router forwards a UDP port to this device "
            "(HOSTING.txt).");
        if (sHostForm)
        {
            DrawHostForm();
        }
        if (sWhy[0] != '\0' && !sEntry)
        {
            Sentence("", sWhy, kRed);
        }
    }

    void DrawHosting(const iCoopStatus& st)
    {
        if (st.phase == ICOOP_PHASE_ERROR)
        {
            Sentence("Hosting could not start: ", st.reason, kRed);
            if (ImGui::Button("Back"))
            {
                iCoopStop();
            }
            return;
        }
        if (sActiveCode[0] != '\0')
        {
            ImGui::TextUnformatted("Join code:");
            ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * 2.6f);
            ImGui::TextUnformatted(sActiveCode);
            ImGui::PopFont();
#ifndef __SWITCH__
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy"))
            {
                ImGui::SetClipboardText(sActiveCode);
            }
#endif
        }
        else
        {
            ImGui::TextDisabled("Joining needs the password you chose.");
        }
        if (st.lanHost)
        {
            if (st.lanAnswering)
            {
                char who[64];
                snprintf(who, sizeof(who), "%s (%s)", sName,
                         iCoopPlatformName(iCoopThisPlatform()));
                ImGui::TextUnformatted("Others on this network see");
                ImGui::SameLine();
                ImGui::TextColored(kGreen, "%s", who);
                ImGui::SameLine();
                ImGui::TextUnformatted("in their list.");
            }
            else if (!iCoopLanVisible())
            {
                ImGui::TextDisabled(
                    "Not in other devices' lists: they can still join with the address below.");
            }
            else if (st.lanNote[0] != '\0')
            {
                ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
                ImGui::TextWrapped("%s", st.lanNote);
                ImGui::PopStyleColor();
            }
            bool visible = iCoopLanVisible();
            if (ImGui::Checkbox("Visible on this network", &visible))
            {
                iCoopSetLanVisible(visible);
            }
            iGui::HelpMarker(
                "Devices on the same Wi-Fi or network see this game in their list. Nothing is "
                "shown to the internet.");
            if (st.lanAddressCount > 0)
            {
                ImGui::TextUnformatted("On this network:");
                for (U8 i = 0; i < st.lanAddressCount && i < 4; i++)
                {
                    ImGui::SameLine();
                    ImGui::Text("%s:%u", st.lanAddresses[i], (unsigned)st.port);
                }
            }
        }
        if (st.localAddress[0] != '\0' && !sHostedLan)
        {
            ImGui::Text("Hosting on UDP port %u.", (unsigned)st.port);
            iGui::Note(
                "Over the internet: on your router, forward UDP port %u to this device, %s. The other "
                "player then joins with your public address (your router's status page shows it) and "
                "the port.",
                (unsigned)st.port, st.localAddress);
        }
        else if (st.localAddress[0] == '\0' && !st.lanHost)
        {
            iGui::Note("Only this PC can join (TSSM_COOP_BIND=loopback).");
        }
#if defined(__ANDROID__) || defined(__SWITCH__)
        iGui::Note(
            "Leaving the game (Home, sleep) for more than about 10 seconds ends the session; the other "
            "player then joins again by themselves for a minute.");
#endif
        if (st.phase == ICOOP_PHASE_HASHING)
        {
            ImGui::TextDisabled("Checking your game files...");
        }
        if (st.reason[0] != '\0' && st.phase == ICOOP_PHASE_WAITING)
        {
            Sentence("Last session: ", st.reason, kYellow);
        }
        if (st.wrongPasswords > 0)
        {
            ImGui::TextColored(kYellow, "Wrong code or password attempts: %u",
                               (unsigned)st.wrongPasswords);
        }
        ImGui::Spacing();
        if (st.connected)
        {
            if (ImGui::Button("Kick"))
            {
                iCoopKick();
            }
            iGui::HelpMarker("Removes the other player from your game and blocks their address until you stop "
                             "hosting (they cannot join again by themselves).");
            ImGui::SameLine();
        }
        if (ImGui::Button("Stop hosting"))
        {
            iCoopStop();
        }
    }

    // Local co-op (iCoopLocal.h): in place of the online form, which is off
    // meanwhile. Player 1 ends it or brings Player 2 back; Player 2 leaves.
    void DrawLocal(const iCoopStatus& st, const iCoopLocalView& lv)
    {
        ImGui::SeparatorText("Local co-op (this PC)");
        if (lv.role == ICOOP_LOCAL_PLAYER2)
        {
            ImGui::TextUnformatted(
                "Local co-op: you are Player 2. Player 1's game holds the settings and saves for "
                "both.");
        }
        else
        {
            ImGui::TextUnformatted(
                "Two players on this PC. Online co-op is off while local co-op runs.");
            ImGui::TextUnformatted("Player 2:");
            ImGui::SameLine();
            ImGui::TextColored(st.connected ? kGreen : kYellow, "%s", lv.state);
            if (st.connected && st.partnerName[0] != '\0')
            {
                ImGui::SameLine();
                ImGui::TextUnformatted("-");
                ImGui::SameLine();
                ImGui::TextUnformatted(st.partnerName);
            }
            if (lv.detail[0] != '\0')
            {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", lv.detail);
                ImGui::PopStyleColor();
            }
        }
        ImGui::Spacing();
        if (lv.canEnd && ImGui::Button("End local co-op"))
        {
            iCoopLocalEnd();
        }
        if (lv.canBringBack && ImGui::Button("Bring Player 2 back"))
        {
            iCoopLocalBringBack();
        }
        if (lv.canPlayAlone)
        {
            ImGui::SameLine();
            if (ImGui::Button("Play alone"))
            {
                iCoopLocalPlayAlone();
            }
        }
        if (lv.canLeave && ImGui::Button("Leave local co-op"))
        {
            iCoopLocalLeave();
        }
    }

    void DrawJoining(const iCoopStatus& st)
    {
        if (st.phase == ICOOP_PHASE_RECONNECTING)
        {
            // M5: the connection was lost; the game joins again by itself.
            ImGui::TextColored(kYellow, "The connection to the host was lost. Joining again by itself (%u s left)...",
                               (unsigned)st.reconnectLeftSec);
            iGui::Note("Your game keeps going meanwhile. Once back, you are put in the host's level again.");
            if (ImGui::Button("Leave"))
            {
                iCoopStop();
            }
            return;
        }
        if (TryAgainShown(st))
        {
            if (sScrollToStatus)
            {
                // The view may still be where the code pad was.
                ImGui::SetScrollHereY(0.0f);
                sScrollToStatus = false;
            }
            Sentence("", st.reason[0] != '\0' ? st.reason : PhaseText(st), kRed);
            if (ImGui::Button("Try again"))
            {
                if (RetryOpensEntry(st))
                {
                    RetryPicked();
                }
                else
                {
                    StartJoin();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Back"))
            {
                iCoopStop();
            }
            return;
        }
        if (ImGui::Button("Leave"))
        {
            iCoopStop();
        }
    }
    // Tests: the virtual controller's button for a name of the vpad action.
    S32 VPadButton(const char* name)
    {
        static const struct
        {
            const char* name;
            S32 button;
        } kNames[] = {
            { "a", SDL_GAMEPAD_BUTTON_SOUTH },          { "b", SDL_GAMEPAD_BUTTON_EAST },
            { "y", SDL_GAMEPAD_BUTTON_NORTH },          { "x", SDL_GAMEPAD_BUTTON_WEST },
            { "start", SDL_GAMEPAD_BUTTON_START },      { "up", SDL_GAMEPAD_BUTTON_DPAD_UP },
            { "down", SDL_GAMEPAD_BUTTON_DPAD_DOWN },   { "left", SDL_GAMEPAD_BUTTON_DPAD_LEFT },
            { "right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
        };
        for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++)
        {
            if (strcmp(kNames[i].name, name) == 0)
            {
                return kNames[i].button;
            }
        }
        return -1;
    }

    // One step of the virtual controller's presses, every frame the tab is
    // drawn; it is unplugged after the last.
    void VPadStep()
    {
        if (sVPad == NULL)
        {
            return;
        }
        if (sVPadPos >= sVPadCount)
        {
            SDL_CloseJoystick(sVPad);
            SDL_DetachVirtualJoystick(sVPadId);
            sVPad = NULL;
            printf(
                "tssm: gui: coop tab (test): the virtual controller's presses are done; the code reads %u "
                "characters\n",
                (unsigned)strlen(sEntryCode));
            fflush(stdout);
            return;
        }
        // A few frames for the controller to be opened first.
        if (sVPadTick < 0)
        {
            sVPadTick++;
            return;
        }
        S32 b = sVPadButtons[sVPadPos];
        SDL_SetJoystickVirtualButton(sVPad, b, sVPadTick < 4);
        if (++sVPadTick >= 8)
        {
            sVPadTick = 0;
            sVPadPos++;
        }
    }

    // Tests: the click's events, through the same path as a real mouse's.
    void ClickStep()
    {
        SDL_Window* w = iGui::Window();
        if (sClickStep == 0 || w == NULL)
        {
            sClickStep = 0;
            return;
        }
        SDL_MouseID which = sClickTouch ? SDL_TOUCH_MOUSEID : 0;
        SDL_Event e;
        if (sClickStep == 1)
        {
            SDL_zero(e);
            e.motion.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.timestamp = SDL_GetTicksNS();
            e.motion.windowID = SDL_GetWindowID(w);
            e.motion.which = which;
            e.motion.x = sClickAt.x;
            e.motion.y = sClickAt.y;
            SDL_PushEvent(&e);
        }
        if (sClickStep == 1 || sClickStep == 4)
        {
            SDL_zero(e);
            e.button.type =
                sClickStep == 1 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.timestamp = SDL_GetTicksNS();
            e.button.windowID = SDL_GetWindowID(w);
            e.button.which = which;
            e.button.button = SDL_BUTTON_LEFT;
            e.button.down = sClickStep == 1;
            e.button.clicks = 1;
            e.button.x = sClickAt.x;
            e.button.y = sClickAt.y;
            SDL_PushEvent(&e);
        }
        sClickStep = sClickStep >= 4 ? 0 : sClickStep + 1;
    }
} // namespace

void iGuiCoopTest(const char* actions)
{
    Init();
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", actions != NULL ? actions : "");
    for (char* a = strtok(buf, ","); a != NULL; a = strtok(NULL, ","))
    {
        char* eq = strchr(a, '=');
        const char* v = "";
        if (eq != NULL)
        {
            *eq = '\0';
            v = eq + 1;
        }
        bool on = strcmp(v, "on") == 0;
        iCoopStatus st;
        iCoopGetStatus(&st);
        if (strcmp(a, "name") == 0)
        {
            snprintf(sName, sizeof(sName), "%s", v);
            printf("tssm: gui: coop tab (test): name typed: %s\n", sName);
        }
        else if (strcmp(a, "port") == 0)
        {
            sPort = atoi(v);
            printf("tssm: gui: coop tab (test): port typed: %d\n", (int)sPort);
        }
        else if (strcmp(a, "address") == 0)
        {
            snprintf(sAddress, sizeof(sAddress), "%s", v);
            printf("tssm: gui: coop tab (test): address typed: %s\n", sAddress);
        }
        else if (strcmp(a, "password") == 0)
        {
            // The host's own password (the join code switched off) and the
            // join form's; the value is not logged.
            sUseCode = false;
            snprintf(sOwnPassword, sizeof(sOwnPassword), "%s", v);
            snprintf(sJoinPassword, sizeof(sJoinPassword), "%s", v);
            printf("tssm: gui: coop tab (test): password typed\n");
        }
        else if (strcmp(a, "host") == 0 || strcmp(a, "join") == 0 || strcmp(a, "hostlan") == 0)
        {
            // host: the internet form's Start hosting; hostlan: Host on this
            // network; join: Join by address.
            bool host = a[0] == 'h';
            bool lan = strcmp(a, "hostlan") == 0;
            if (st.role != ICOOP_ROLE_OFF)
            {
                printf("tssm: gui: coop tab (test): %s: not shown (co-op is on)\n", a);
                continue;
            }
            sWhy[0] = '\0';
            if (host)
            {
                StartHost(lan);
            }
            else
            {
                StartJoin();
            }
            printf("tssm: gui: coop tab (test): %s pressed%s%s\n",
                   lan ? "Host on this network" : (host ? "Host" : "Join"), sWhy[0] ? ": " : "",
                   sWhy);
        }
        else if (strcmp(a, "tryagain") == 0)
        {
            if (!TryAgainShown(st))
            {
                printf("tssm: gui: coop tab (test): Try again: not shown (phase %s)\n", PhaseText(st));
                continue;
            }
            sWhy[0] = '\0';
            bool list = RetryOpensEntry(st);
            if (list)
            {
                RetryPicked();
            }
            else
            {
                StartJoin();
            }
            printf("tssm: gui: coop tab (test): Try again pressed (the tab said: %s)%s%s%s\n",
                   PhaseText(st), list ? ": the code entry again" : "", sWhy[0] ? ": " : "", sWhy);
        }
        else if (strcmp(a, "stop") == 0)
        {
            iCoopStop();
            printf("tssm: gui: coop tab (test): Stop pressed\n");
        }
        else if (strcmp(a, "end") == 0 || strcmp(a, "bringback") == 0 || strcmp(a, "alone") == 0 ||
                 strcmp(a, "leave") == 0)
        {
            // Local co-op's buttons, pressed only where the tab shows them.
            iCoopLocalView lv;
            iCoopLocalGetView(&lv);
            bool shown = a[0] == 'e' ? lv.canEnd :
                         a[0] == 'b' ? lv.canBringBack :
                         a[0] == 'a' ? lv.canPlayAlone :
                                       lv.canLeave;
            if (!shown)
            {
                printf("tssm: gui: coop tab (test): %s: not shown (%s)\n", a,
                       lv.state != NULL ? lv.state : "");
                continue;
            }
            if (a[0] == 'e')
            {
                iCoopLocalEnd();
            }
            else if (a[0] == 'b')
            {
                iCoopLocalBringBack();
            }
            else if (a[0] == 'a')
            {
                iCoopLocalPlayAlone();
            }
            else
            {
                iCoopLocalLeave();
            }
            printf("tssm: gui: coop tab (test): %s pressed\n", a);
        }
        else if (strcmp(a, "find") == 0)
        {
            // The list's Search again, or no more.
            bool ok = on ? (StartSearch(), sSearchAsked) : iCoopFindGames(false);
            printf("tssm: gui: coop tab (test): search %s%s\n", on ? "started" : "stopped",
                   ok ? "" : ": not offered while co-op is on");
        }
        else if (strcmp(a, "pick") == 0)
        {
            // A row of the list, by the host's name, as a click on it.
            iCoopFoundGame found[8];
            S32 n = iCoopGetFound(found, 8);
            sPicked[0] = '\0';
            for (S32 i = 0; i < n && sPicked[0] == '\0'; i++)
            {
                if (strcmp(found[i].name, v) == 0)
                {
                    snprintf(sPicked, sizeof(sPicked), "%s", found[i].address);
                    char level[64], why[256];
                    iCoopSceneName(found[i].scene, level, sizeof(level));
                    RowReason(found[i], why, sizeof(why));
                    printf(
                        "tssm: gui: coop tab (test): picked %s (%s, %s, %s, %s) of %d listed%s%s\n",
                        found[i].name, iCoopPlatformName(found[i].platform),
                        level[0] ? level : "not in a level",
                        found[i].hasPlayer ? "has a player" : "waiting",
                        found[i].status == ICOOP_FOUND_READY ? "ready" :
                        found[i].status == ICOOP_FOUND_FILES ? "different game files" :
                                                               "different version",
                        (int)n, why[0] ? "; the tab says: " : "", why);
                }
            }
            if (sPicked[0] == '\0')
            {
                printf("tssm: gui: coop tab (test): pick: no game named %s among %d listed\n", v,
                       (int)n);
            }
        }
        else if (strcmp(a, "click") == 0 || strcmp(a, "tap") == 0)
        {
            // A mouse click or a finger's tap on a row of the list, by the
            // host's name, as SDL events (the row picks itself, or not).
            S32 at = -1;
            for (S32 i = 0; i < sRowCount && i < 8 && at < 0; i++)
            {
                if (strcmp(sRows[i].name, v) == 0)
                {
                    at = i;
                }
            }
            if (at < 0 || sClickStep != 0)
            {
                printf("tssm: gui: coop tab (test): %s: no row named %s shown%s\n", a, v,
                       sClickStep != 0 ? " (or still clicking)" : "");
                continue;
            }
            sClickAt = ImVec2(sRows[at].min.x + 12.0f, (sRows[at].min.y + sRows[at].max.y) * 0.5f);
            sClickTouch = a[0] == 't';
            sClickStep = 1;
            printf("tssm: gui: coop tab (test): %s on the row of %s\n",
                   a[0] == 't' ? "a tap" : "a click", v);
        }
        else if (strcmp(a, "listed") == 0)
        {
            // What the list shows now, row by row.
            iCoopFoundGame found[8];
            S32 n = iCoopGetFound(found, 8);
            printf("tssm: gui: coop tab (test): the list shows %d game%s%s\n", (int)n,
                   n == 1 ? "" : "s", st.lanNote[0] ? "; the tab says: " : "", st.lanNote);
            for (S32 i = 0; i < n; i++)
            {
                printf("tssm: gui: coop tab (test):   %s, %s, %s: %s\n",
                       found[i].newer ? "(newer)" : found[i].name,
                       iCoopPlatformName(found[i].platform),
                       found[i].hasPlayer ? "has a player" : "waiting",
                       found[i].newer || found[i].status == ICOOP_FOUND_VERSION ?
                           "different version" :
                       found[i].status == ICOOP_FOUND_FILES ? "different game files" :
                       found[i].hasPlayer                   ? "full" :
                                                              "ready");
            }
        }
        else if (strcmp(a, "joinpicked") == 0)
        {
            // The list's Join: the code entry for the picked game.
            iCoopFoundGame found[8];
            S32 n = iCoopGetFound(found, 8);
            S32 at = -1;
            for (S32 i = 0; i < n && at < 0; i++)
            {
                if (sPicked[0] != '\0' && strcmp(found[i].address, sPicked) == 0 && !found[i].newer)
                {
                    at = i;
                }
            }
            if (st.role != ICOOP_ROLE_OFF || at < 0)
            {
                printf("tssm: gui: coop tab (test): Join (list): not shown\n");
                continue;
            }
            OpenEntry(found[at], strcmp(v, "pad") == 0);
            sPadFocus = sPad;
            printf("tssm: gui: coop tab (test): Join (list) pressed: the code entry for %s%s\n",
                   sPickedName, sPad ? ", with the code pad" : "");
        }
        else if (strcmp(a, "pad") == 0 || strcmp(a, "code") == 0)
        {
            // pad: each character pressed on the code pad's keys (a dash or
            // space is skipped, as the pad has none); code: typed as a keyboard
            // would. The value is not logged.
            if (!sEntry)
            {
                printf("tssm: gui: coop tab (test): %s: no code entry open\n", a);
                continue;
            }
            sCodeSetByTest = true;
            if (a[0] == 'p')
            {
                sPad = true;
                size_t pressed = 0;
                for (const char* c = v; *c != '\0'; c++)
                {
                    char u = (*c >= 'a' && *c <= 'z') ? (char)(*c - 'a' + 'A') : *c;
                    if (u != '\0' && strchr(kPadKeys, u) != NULL)
                    {
                        PadType(u);
                        pressed++;
                    }
                }
                printf("tssm: gui: coop tab (test): %u code pad keys pressed\n", (unsigned)pressed);
            }
            else
            {
                snprintf(sEntryCode, sizeof(sEntryCode), "%s", v);
                printf("tssm: gui: coop tab (test): code typed\n");
            }
        }
        else if (strcmp(a, "padview") == 0)
        {
            // Whether the code pad, as last drawn, is all on screen.
            if (!sEntry || !sPad || sPadTop < 0.0f)
            {
                printf("tssm: gui: coop tab (test): padview: no code pad shown\n");
                continue;
            }
            bool in =
                sPadShownTop >= sTabShownTop - 0.5f && sPadShownBottom <= sTabShownBottom + 0.5f;
            printf(
                "tssm: gui: coop tab (test): padview: the code pad %s (from %.0f to %.0f; the tab shows %.0f "
                "to %.0f)\n",
                in ? "is all in view" : "is NOT all in view", sPadShownTop, sPadShownBottom,
                sTabShownTop, sTabShownBottom);
        }
        else if (strcmp(a, "joincode") == 0)
        {
            if (!sEntry || st.role != ICOOP_ROLE_OFF)
            {
                printf("tssm: gui: coop tab (test): Join (code entry): not shown\n");
                continue;
            }
            JoinPicked();
            printf("tssm: gui: coop tab (test): Join (code entry) pressed%s%s\n",
                   sWhy[0] ? ": " : "", sWhy);
        }
        else if (strcmp(a, "vpad") == 0)
        {
            // A virtual controller plugged in, pressing these buttons in turn
            // (a.b.start...) on the overlay, then unplugged.
            if (sVPad != NULL)
            {
                printf("tssm: gui: coop tab (test): vpad: still pressing\n");
                continue;
            }
            char seq[160];
            snprintf(seq, sizeof(seq), "%s", v);
            sVPadCount = 0;
            for (char* t = seq; t != NULL && *t != '\0' && sVPadCount < 48;)
            {
                char* dot = strchr(t, '.');
                if (dot != NULL)
                {
                    *dot = '\0';
                }
                S32 b = VPadButton(t);
                if (b >= 0)
                {
                    sVPadButtons[sVPadCount++] = b;
                }
                t = dot != NULL ? dot + 1 : NULL;
            }
            SDL_VirtualJoystickDesc desc;
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1u;
            desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1u;
            desc.name = "TSSM test controller";
            sVPadId = SDL_AttachVirtualJoystick(&desc);
            sVPad = sVPadId != 0 ? SDL_OpenJoystick(sVPadId) : NULL;
            sVPadPos = 0;
            sVPadTick = -20;
            printf("tssm: gui: coop tab (test): a virtual controller %s, %d presses\n",
                   sVPad != NULL ? "plugged in" : "could not be plugged in", (int)sVPadCount);
        }
        else if (strcmp(a, "open") == 0)
        {
            iGuiSetOpen(on);
            printf("tssm: gui: coop tab (test): overlay %s\n", on ? "opened" : "closed");
        }
        else if (strcmp(a, "shot") == 0 || strcmp(a, "hudshot") == 0)
        {
            // A picture of the overlay (hudshot: the HUD layer, overlay closed)
            // to TSSM_CAPTURE_DIR/<name>.png; the name is letters, digits, - and _.
            const char* dir = getenv("TSSM_CAPTURE_DIR");
            bool ok = dir != NULL && v[0] != '\0' && strlen(v) < 48;
            for (const char* c = v; ok && *c != '\0'; c++)
            {
                ok = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_';
            }
            if (!ok)
            {
                printf(
                    "tssm: gui: coop tab (test): %s: no TSSM_CAPTURE_DIR or not a plain name; ignored\n",
                    a);
                continue;
            }
            char path[512];
            snprintf(path, sizeof(path), "%s/%s.png", dir, v);
            iGuiTestShot(path, a[0] == 'h');
            printf("tssm: gui: coop tab (test): %s %s asked for\n", a, v);
        }
        else if (strcmp(a, "visible") == 0)
        {
            iCoopSetLanVisible(on);
            printf("tssm: gui: coop tab (test): visible on this network switched %s\n",
                   on ? "on" : "off");
        }
        else if (strcmp(a, "nametag") == 0 || strcmp(a, "follow") == 0 || strcmp(a, "reconnect") == 0 ||
                 strcmp(a, "quality") == 0)
        {
            SetOption(a, on);
            printf("tssm: gui: coop tab (test): %s switched %s\n", a, on ? "on" : "off");
        }
        else
        {
            printf("tssm: gui: coop tab (test): '%s' is not one of the tab's actions; ignored\n", a);
        }
    }
    fflush(stdout);
}

void iGui::DrawCoop()
{
    Init();
    iCoopStatus st;
    iCoopGetStatus(&st);

    iCoopLocalView lv;
    iCoopLocalGetView(&lv);
    if (lv.role != ICOOP_LOCAL_OFF)
    {
        DrawLocal(st, lv);
        if (st.connected)
        {
            DrawPartner(st);
        }
        DrawCustomize(st);
        ImGui::SeparatorText("Options");
        bool tag = iCoopNameTagEnabled();
        if (ImGui::Checkbox("Show the partner's name tag", &tag))
        {
            SetOption("nametag", tag);
        }
        DrawVersions(false);
        return;
    }

    VPadStep();
    ClickStep();
    // The list searches by itself when the tab opens with co-op off, and
    // again when a host or join ends. Debug captures open no socket unless a
    // test asked for loopback discovery.
    U64 nowMs = SDL_GetTicks();
    bool opened = sTabMissed > 0 && nowMs - sTabAt > 500; // not a long frame while shown
    sTabAt = nowMs;
    sTabDrawn = true;
    bool quiet = getenv("TSSM_GUI_CAPTURE") != NULL && getenv("TSSM_COOP_LAN_PORT") == NULL;
    if (sSearchPending && nowMs - sSearchAt > 15000)
    {
        sSearchPending = false; // never ran (the network or the files): Search again is there
    }
    if (st.role == ICOOP_ROLE_OFF && (opened || sTabRole != ICOOP_ROLE_OFF) && !st.finding &&
        !sSearchPending && !quiet)
    {
        StartSearch();
        printf("tssm: gui: coop tab: searching for games on this network (%s)\n",
               opened ? "the tab opened" : "co-op stopped");
        fflush(stdout);
    }
    sTabRole = st.role;
    if (st.phase != sSeenPhase)
    {
        sSeenPhase = st.phase;
        sScrollToStatus = TryAgainShown(st);
    }

    ImGui::SeparatorText("Online co-op");
    ImGui::TextUnformatted("Status:");
    ImGui::SameLine();
    ImGui::TextColored(PhaseColor(st), "%s", PhaseText(st));
    ImGui::Spacing();

    if (st.role == ICOOP_ROLE_OFF)
    {
        DrawForm(st);
    }
    else if (st.role == ICOOP_ROLE_HOST)
    {
        DrawHosting(st);
    }
    else
    {
        DrawJoining(st);
    }

    if (st.role != ICOOP_ROLE_OFF)
    {
        DrawPartner(st);
    }
    DrawCustomize(st);

    ImGui::SeparatorText("Options");
    bool tag = iCoopNameTagEnabled();
    if (ImGui::Checkbox("Show the partner's name tag", &tag))
    {
        SetOption("nametag", tag);
    }
    bool follow = iCoopFollowEnabled();
    if (ImGui::Checkbox("When joining, follow the host to their level", &follow))
    {
        SetOption("follow", follow);
    }
    bool rejoin = iCoopReconnectEnabled();
    if (ImGui::Checkbox("When joining, join again by itself if the connection is lost", &rejoin))
    {
        SetOption("reconnect", rejoin);
    }
    iGui::HelpMarker("For up to a minute after the connection drops (nothing heard for 10 seconds). Not after a "
                     "kick, after the host stops, or when the host refuses.");
    bool quality = iCoopQualityHudEnabled();
    if (ImGui::Checkbox("Show the connection quality on screen", &quality))
    {
        SetOption("quality", quality);
    }
    bool code = iCoopShowCodeEnabled();
    if (ImGui::Checkbox("When hosting on this network, show the join code on screen", &code))
    {
        SetOption("show_code", code);
    }
    iGui::HelpMarker(
        "In a corner of the picture until a player joins, so it can be read off the TV.");

    ImGui::Spacing();
    iGui::Note("Co-op sends game state only. Each player uses their own game files. Traffic is protected "
               "against tampering but not encrypted. The host's game runs the enemies, moving platforms, "
               "breakable objects and pickups for both of you, leads the way from level to level and starts "
               "the cutscenes (and skips them for both). While joined, only the host's game saves: the "
               "guest's game does not save, nor afterwards in a level it followed the host to, until it "
               "leaves that level.");
    DrawVersions(true);
}

void iGui::CoopFrame()
{
    // The list's socket is open only while the list is on screen: frames
    // without the tab, not one long frame (a load) with it.
    if (sTabDrawn)
    {
        sTabDrawn = false;
        sTabMissed = 0;
    }
    else if (sTabMissed < 1000)
    {
        sTabMissed++;
    }
    if ((sSearchAsked || sSearchPending) && sTabAt != 0 && sTabMissed >= 3 &&
        SDL_GetTicks() - sTabAt > 500)
    {
        iCoopFindGames(false);
        sSearchAsked = false;
        sSearchPending = false;
        sSawFinding = false;
        printf("tssm: gui: coop tab: no longer shown; no more searching\n");
        fflush(stdout);
    }
}

void iGui::DrawCoopHud()
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float pad = 6.0f;

    // The game's picture inside the window (librw's blitVirtualScreen fit).
    float pw = io.DisplaySize.x, ph = io.DisplaySize.y;
    float sw = (float)iScreenWidth(), sh = (float)iScreenHeight();
    if (sw > 0.0f && sh > 0.0f && pw > 0.0f && ph > 0.0f)
    {
        float h = pw * sh / sw;
        if (h > ph)
        {
            pw = ph * sw / sh;
        }
        else
        {
            ph = h;
        }
    }
    const float px = (io.DisplaySize.x - pw) * 0.5f, py = (io.DisplaySize.y - ph) * 0.5f;

    iCoopStatus st;
    iCoopGetStatus(&st);

    F32 tx, ty;
    bool front;
    if (!iGuiIsOpen() && st.connected && st.partnerName[0] != '\0' && iCoopNameTag(&tx, &ty, &front))
    {
        const char* name = st.partnerName;
        ImVec2 size = ImGui::CalcTextSize(name);
        float x = px + tx * pw, y = py + ty * ph;
        bool onScreen = front && tx >= 0.0f && tx <= 1.0f && ty >= 0.0f && ty <= 1.0f;
        if (!onScreen)
        {
            // At the edge of the picture, on the partner's side.
            float dx = tx - 0.5f, dy = ty - 0.5f;
            if (!front)
            {
                dx = -dx;
                dy = -dy;
            }
            float m = fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy);
            if (m < 1e-4f)
            {
                dx = 0.0f;
                dy = 0.5f;
                m = 0.5f;
            }
            x = px + (0.5f + dx / m * 0.5f) * pw;
            y = py + (0.5f + dy / m * 0.5f) * ph;
        }
        float minX = px + pad + size.x * 0.5f, maxX = px + pw - pad - size.x * 0.5f;
        float minY = py + pad + size.y * 0.5f, maxY = py + ph - pad - size.y * 0.5f;
        x = x < minX ? minX : (x > maxX ? maxX : x);
        y = y < minY ? minY : (y > maxY ? maxY : y);
        ImVec2 tl(x - size.x * 0.5f, y - size.y * 0.5f);
        dl->AddRectFilled(ImVec2(tl.x - 4.0f, tl.y - 2.0f), ImVec2(tl.x + size.x + 4.0f, tl.y + size.y + 2.0f),
                          IM_COL32(0, 20, 40, onScreen ? 150 : 110), 4.0f);
        dl->AddText(tl, onScreen ? IM_COL32(255, 240, 120, 255) : IM_COL32(255, 240, 120, 170), name);
    }

    // The join code, bottom right, while a host visible on the home network
    // waits ([coop] show_code): never for a loopback or local co-op host
    // (lanHost), a host not visible (one over the internet), nor for a typed
    // password.
    bool showCode = iCoopShowCodeEnabled() && st.role == ICOOP_ROLE_HOST && st.lanHost &&
                    iCoopLanVisible() && st.phase == ICOOP_PHASE_WAITING &&
                    sActiveCode[0] != '\0' && !iGuiIsOpen();
    if (showCode)
    {
        ImGui::PushFont(NULL, ImGui::GetStyle().FontSizeBase * 1.8f);
        char text[48];
        snprintf(text, sizeof(text), "Join code: %s", sActiveCode);
        ImVec2 size = ImGui::CalcTextSize(text);
        ImVec2 tl(px + pw - pad - size.x - 16.0f, py + ph - pad - size.y - 10.0f);
        dl->AddRectFilled(tl, ImVec2(tl.x + size.x + 16.0f, tl.y + size.y + 10.0f),
                          IM_COL32(0, 20, 40, 170), 6.0f);
        dl->AddText(ImVec2(tl.x + 8.0f, tl.y + 5.0f), IM_COL32(255, 240, 120, 255), text);
        ImGui::PopFont();
    }
    if (showCode != sCodeHudLogged)
    {
        sCodeHudLogged = showCode;
        printf("tssm: gui: coop hud: the join code is %s\n",
               showCode ? "shown (waiting for a player)" : "hidden");
        fflush(stdout);
    }

    // Notices, top left of the picture, fading out; first, while the partner's
    // game reports a stall (5.11: its window held, a hitch, a load), a line
    // that stays until it runs again. While the overlay is open its window
    // covers the top of the picture, so they go at the bottom instead.
    struct Line
    {
        char text[200];
        float alpha;
        bool stall;
    };
    Line lines[8];
    S32 count = 0;
    // M5: the connection interrupted (nothing at all heard, not a stall the
    // partner's game reported), and a guest joining again after a loss.
    if (st.connected && st.partnerStallMs == 0 && st.partnerSilentMs >= 2000)
    {
        snprintf(lines[count].text, sizeof(lines[count].text), "Co-op: no word from %s for %u s (connection interrupted)...",
                 st.partnerName[0] != '\0' ? st.partnerName : "the other player", (unsigned)(st.partnerSilentMs / 1000));
        lines[count].alpha = 1.0f;
        lines[count].stall = true;
        count++;
    }
    if (st.phase == ICOOP_PHASE_RECONNECTING)
    {
        snprintf(lines[count].text, sizeof(lines[count].text),
                 "Co-op: the connection was lost; joining the host's game again (%u s left)...",
                 (unsigned)st.reconnectLeftSec);
        lines[count].alpha = 1.0f;
        lines[count].stall = true;
        count++;
    }
    if (st.connected && st.partnerStallMs >= 2000)
    {
        snprintf(lines[count].text, sizeof(lines[count].text), "Co-op: waiting for %s's game (%u s)...",
                 st.partnerName[0] != '\0' ? st.partnerName : "the other player", (unsigned)(st.partnerStallMs / 1000));
        lines[count].alpha = 1.0f;
        lines[count].stall = true;
        count++;
    }
    for (S32 i = 0; count < 8; i++)
    {
        const char* text;
        F32 age;
        if (!iCoopNotice(i, &text, &age))
        {
            break;
        }
        float a = age < 4.0f ? 1.0f : (6.0f - age) / 2.0f;
        snprintf(lines[count].text, sizeof(lines[count].text), "Co-op: %s", text);
        lines[count].alpha = a < 0.0f ? 0.0f : a;
        lines[count].stall = false;
        count++;
    }
    // M5: the connection's quality, top right of the picture: a dot in its
    // colour and the ping ([coop] quality; hidden while the overlay is open,
    // whose Co-op tab says the same).
    if (st.connected && iCoopQualityHudEnabled() && !iGuiIsOpen())
    {
        char q[48];
        if (st.quality == ICOOP_QUALITY_INTERRUPTED)
        {
            snprintf(q, sizeof(q), "no signal");
        }
        else if (st.pingMs >= 0)
        {
            snprintf(q, sizeof(q), "%d ms", (int)st.pingMs);
        }
        else
        {
            snprintf(q, sizeof(q), "...");
        }
        ImVec2 size = ImGui::CalcTextSize(q);
        float r = size.y * 0.3f;
        float w = size.x + r * 2.0f + 14.0f;
        ImVec2 tl(px + pw - pad - w, py + pad);
        dl->AddRectFilled(tl, ImVec2(tl.x + w, tl.y + size.y + 4.0f), IM_COL32(0, 20, 40, 130), 4.0f);
        ImVec4 c = QualityColor(st.quality);
        dl->AddCircleFilled(ImVec2(tl.x + 4.0f + r, tl.y + 2.0f + size.y * 0.5f), r,
                            ImGui::ColorConvertFloat4ToU32(c));
        dl->AddText(ImVec2(tl.x + 10.0f + r * 2.0f, tl.y + 2.0f), IM_COL32(255, 255, 255, 220), q);
    }

    const float step = ImGui::GetTextLineHeight() + 8.0f;
    const bool bottom = iGuiIsOpen();
    float y = bottom ? py + ph - pad - step * (float)count : py + pad;
    for (S32 i = 0; i < count; i++)
    {
        const Line& l = lines[i];
        ImVec2 size = ImGui::CalcTextSize(l.text);
        dl->AddRectFilled(ImVec2(px + pad, y), ImVec2(px + pad + size.x + 8.0f, y + size.y + 4.0f),
                          l.stall ? IM_COL32(40, 30, 0, 170) : IM_COL32(0, 20, 40, (int)(150 * l.alpha)), 4.0f);
        dl->AddText(ImVec2(px + pad + 4.0f, y + 2.0f),
                    l.stall ? IM_COL32(255, 230, 120, 255) : IM_COL32(255, 255, 255, (int)(255 * l.alpha)), l.text);
        y += step;
    }
}
