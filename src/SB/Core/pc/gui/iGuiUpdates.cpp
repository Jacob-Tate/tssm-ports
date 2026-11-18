// The Settings tab's Updates section, the line at the top of the window when
// an update is waiting, and the start-up glue (iUpdate.h; docs/SWITCH.md
// "Updates"). Built only with TSSM_UPDATE; the same file on every branch that
// has updates.
//
// Nothing connects unless the player asks: the "Check for updates" button, or
// the check at start-up, which is off until they turn it on.
//
// On a narrow screen (the 3DS's bottom one, 320 pixels) the section packs
// itself in: a shorter label column, the check box on a line of its own,
// buttons side by side only when they fit, and the restart popup as wide as
// the screen with its text wrapped. Wider screens keep the one layout.

#include "imgui.h"

#include "gui/iGuiInternal.h"

#include "iConfig.h"
#include "iUpdate.h"
#include "iWindow.h"

#include <stdio.h>

#if !defined(TSSM_UPDATE_REPO) || !defined(TSSM_UPDATE_VERSION)
#error "CMakeLists.txt sets TSSM_UPDATE_REPO and TSSM_UPDATE_VERSION for this file"
#endif

namespace
{
    const char kRestartPopup[] = "Restart the game?###update_restart";

    bool sStarted;
    // iUpdateCreatePlatform had nothing: this copy cannot update itself.
    bool sNoPlatform;

    // Fewer than 32 lines' heights across: the 3DS's bottom screen.
    bool Narrow()
    {
        return ImGui::GetIO().DisplaySize.x < ImGui::GetFontSize() * 32.0f;
    }

    // A popup's window: on a narrow screen the screen's width, in the middle.
    ImGuiWindowFlags PopupFlags()
    {
        if (!Narrow())
        {
            return ImGuiWindowFlags_AlwaysAutoResize;
        }
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0.0f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                                ImVec2(0.0f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 0.0f));
        return ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    }

    // Before a button: on the line of the one before it when it still fits
    // there, else on the next.
    void NextTo(bool* sameLine, const char* label)
    {
        if (*sameLine)
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            const float width =
                ImGui::CalcTextSize(label, NULL, true).x + style.FramePadding.x * 2.0f;
            const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right)
            {
                ImGui::SameLine();
            }
        }
        *sameLine = true;
    }

    bool sRestartFailed;

    void DrawRestart()
    {
        if (!ImGui::BeginPopupModal(kRestartPopup, NULL, PopupFlags()))
        {
            return;
        }
        ImGui::PushTextWrapPos(Narrow() ? 0.0f : -1.0f);
        ImGui::TextUnformatted("Quit and start the new version? Progress since the last save is "
                               "lost.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Cancel") || iGui::BackPressed())
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Restart"))
        {
            ImGui::CloseCurrentPopup();
            if (iUpdatePrepareRestart())
            {
                iWindowRequestClose();
                iGuiSetOpen(false);
            }
            else
            {
                sRestartFailed = true;
            }
        }
        ImGui::EndPopup();
    }

    void Row(const char* label)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
    }

    void Megabytes(uint64_t bytes, char* out, size_t outSize)
    {
        snprintf(out, outSize, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    }
} // namespace

void iGui::UpdatesStartup()
{
    if (sStarted)
    {
        return;
    }
    sStarted = true;

    iUpdateSettings settings;
    settings.repo = TSSM_UPDATE_REPO;
    settings.currentVersion = TSSM_UPDATE_VERSION;
    iUpdatePlatform* platform = iUpdateCreatePlatform();
    if (platform == NULL)
    {
        sNoPlatform = true;
        return;
    }
    if (!iUpdateInit(platform, settings))
    {
        return;
    }
    if (iConfigGetBool("update.check_at_start", FALSE) == 0)
    {
        return;
    }
    printf("tssm: update: checking at start-up (update.check_at_start = on)\n");
    fflush(stdout);
    iUpdateCheck();
}

void iGui::UpdatesNotice()
{
    if (!iUpdateReady())
    {
        return;
    }
    iUpdateStatus st;
    iUpdateGetStatus(&st);
    if (st.state != IUPDATE_AVAILABLE && st.state != IUPDATE_DONE_RESTART)
    {
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(120, 220, 140, 255));
    if (st.state == IUPDATE_AVAILABLE)
    {
        ImGui::TextWrapped("Version %s is available: Settings, Updates.", st.latestVersion);
    }
    else
    {
        ImGui::TextWrapped("Version %s is installed. Restart the game to use it.",
                           st.latestVersion);
    }
    ImGui::PopStyleColor();
}

void iGui::SettingsUpdates()
{
    ImGui::SeparatorText("Updates");
    if (!iUpdateReady())
    {
        if (sNoPlatform)
        {
            Note("Updates are not available in this copy of the game.");
        }
        else
        {
            Note("Updates are off: this build's version or repository is not one the updater "
                 "can use.");
        }
        return;
    }

    iUpdateStatus st;
    iUpdateGetStatus(&st);

    const bool narrow = Narrow();
    bool atStart = iConfigGetBool("update.check_at_start", FALSE) != 0;
    bool atStartChanged = false;
    if (ImGui::BeginTable("updates", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFontSize() * (narrow ? 7.5f : 11.0f));
        ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);

        Row("This version");
        ImGui::TextUnformatted(st.currentVersion);

        if (!narrow)
        {
            Row("At start-up");
            atStartChanged = ImGui::Checkbox(
                "Check for updates when the game starts##update_at_start", &atStart);
        }
        ImGui::EndTable();
    }
    if (narrow)
    {
        atStartChanged =
            ImGui::Checkbox("Check for updates when the game starts##update_at_start", &atStart);
    }
    if (atStartChanged)
    {
        Commit("update.check_at_start", atStart ? "on" : "off");
    }

    // The buttons the state allows.
    bool sameLine = false;
    if (iUpdateCanCheck())
    {
        NextTo(&sameLine, "Check for updates");
        if (ImGui::Button("Check for updates"))
        {
            iUpdateCheck();
        }
    }
    if (iUpdateCanDownload())
    {
        char label[64];
        snprintf(label, sizeof(label), "Download and install %s", st.latestVersion);
        NextTo(&sameLine, label);
        if (ImGui::Button(label))
        {
            iUpdateDownload();
        }
    }
    if (iUpdateCanCancel())
    {
        NextTo(&sameLine, "Cancel##update_cancel");
        if (ImGui::Button("Cancel##update_cancel"))
        {
            iUpdateCancel();
        }
    }
    if (st.state == IUPDATE_DONE_RESTART && st.canRestart)
    {
        if (ImGui::Button("Restart now"))
        {
            ImGui::OpenPopup(kRestartPopup);
        }
    }
    DrawRestart();

    // Where it is.
    if (st.state == IUPDATE_DOWNLOADING || st.state == IUPDATE_VERIFYING)
    {
        char done[32];
        char total[32];
        Megabytes(st.done, done, sizeof(done));
        Megabytes(st.assetSize, total, sizeof(total));
        char overlay[96];
        snprintf(overlay, sizeof(overlay), "%s of %s%s", done, total,
                 st.state == IUPDATE_VERIFYING ? " checked" : "");
        const float fraction =
            st.assetSize > 0 ? (float)((double)st.done / (double)st.assetSize) : 0.0f;
        ImGui::ProgressBar(fraction > 1.0f ? 1.0f : fraction, ImVec2(-1.0f, 0.0f), overlay);
    }
    if (st.message[0] != '\0')
    {
        ImU32 colour = ImGui::GetColorU32(ImGuiCol_Text);
        if (st.state == IUPDATE_ERROR)
        {
            colour = IM_COL32(255, 120, 110, 255);
        }
        else if (st.state == IUPDATE_AVAILABLE || st.state == IUPDATE_DONE_RESTART)
        {
            colour = IM_COL32(120, 220, 140, 255);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, colour);
        ImGui::TextWrapped("%s", st.message);
        ImGui::PopStyleColor();
    }
    if (st.state == IUPDATE_AVAILABLE && !st.installAllowed && st.installBlocked[0] != '\0')
    {
        Note("%s", st.installBlocked);
    }
    if (st.state == IUPDATE_DONE_RESTART && (!st.canRestart || sRestartFailed))
    {
        Note("Quit the game and start it again.");
    }

    Note("Checking connects to GitHub only and sends nothing about you or this system. An update "
         "replaces only the game's program; saves and settings stay as they are.");
}
