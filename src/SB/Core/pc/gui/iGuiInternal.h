#ifndef IGUIINTERNAL_H
#define IGUIINTERNAL_H

// Shared between the GUI's own files, and nothing else. ImGui before types.h:
// types.h defines NULL, TRUE and `null` as macros, which no ImGui header
// expects to meet.
#include "imgui.h"

#include <types.h>

#include <stddef.h>

#include <vector>

#include "gui/iGui.h"

struct SDL_Window;

namespace iGui
{
    // --- where the GUI is drawing ----------------------------------------------

    // The window the current ImGui context belongs to: the launcher's, or the
    // game's once the overlay is up. Dialogs are parented to it.
    SDL_Window* Window();
    void SetWindow(SDL_Window* window);

    // TRUE in the launcher: nothing may be applied live, because there is no
    // game window or device yet, and nothing needs a restart to apply.
    bool InLauncher();
    void SetInLauncher(bool on);

    // --- look ---------------------------------------------------------------------

    // The overlay's scale: [gui] scale, or for "auto" the display's content
    // scale, raised for a large window so the text is readable from a sofa.
    float AutoScale(SDL_Window* window);

    // Fonts and colours, once per context; then the size, whenever it changes.
    void SetupContext();
    void ApplyScale(float scale);

    // --- settings -------------------------------------------------------------------

    // iConfigSet, and iConfigSave unless `save` is false -- a slider being
    // dragged sets on every frame and saves once, when it is let go. A save
    // that fails is remembered and shown at the top of the window.
    void Commit(const char* key, const char* value, bool save = true);
    void CommitInt(const char* key, int value, bool save = true);
    void CommitFloat(const char* key, float value, bool save = true);
    void Unset(const char* key);
    void SaveNow();
    const char* SaveError();

    // The values the startup-only settings had when the overlay came up, so
    // the Settings tab can say which changes wait for a restart.
    void SnapshotStartup();
    bool ChangedSinceStartup(const char* key);
    bool AnyRestartPending();

    // The Settings tab, and its sections on their own for the launcher.
    void DrawSettings();
    void SettingsGameFiles();
    void SettingsDisplay();
    void SettingsAudio();
    void SettingsGame();
    void SettingsInterface();

    // The Updates section, the line at the top of the window when an update
    // waits, and the start-up check (gui/iGuiUpdates.cpp; TSSM_UPDATE only).
    void SettingsUpdates();
    void UpdatesNotice();
    void UpdatesStartup();

    // The other tabs.
    void DrawControls();
    void DrawSaves();
    void DrawMods();
    void DrawAbout();

    // The running renderer as a player would name it: "Direct3D 11".
    const char* RendererName();

    // Another module's panel, or NULL.
    iGuiPanelFn Panel(iGuiPanel panel, void** user);

    // Pushes the audio settings to the listener, if one is registered.
    void ApplyAudio();

    // --- small widgets ----------------------------------------------------------

    // "(?)" with `text` as its tooltip.
    void HelpMarker(const char* text);

    // A modal's Cancel: Esc, and on the Switch the controller's back button.
    bool BackPressed();

    // Every slider's flags. On the Switch a held button must not turn a
    // slider into a text field: that opens the system keyboard.
#ifdef __SWITCH__
    const ImGuiSliderFlags kSliderFlags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput;
#else
    const ImGuiSliderFlags kSliderFlags = ImGuiSliderFlags_AlwaysClamp;
#endif

    // The controller's open-the-overlay combo by its buttons' names: "Back+Start",
    // or on the Switch (iPadSwitchLabels) "- and +".
    const char* PadComboName();

    // A dimmed line of explanation under a control.
    void Note(const char* fmt, ...);

    // A highlighted "applies after a restart" tag after the last item, when
    // `key` differs from the value the session started with.
    void RestartTag(const char* key);

    // --- the game's files -------------------------------------------------------

    // Whether `dir` holds the game's files: boot.HIP and FONT.HIP, in any case,
    // from the Xbox release. `why` gets a sentence either way.
    bool CheckAssetFolder(const char* dir, char* why, size_t whySize);

    // --- folders and files --------------------------------------------------------

    // The system's folder picker, through SDL. Asynchronous: the answer
    // arrives on a later frame, and PollFolder hands it over once, tagged with
    // the `tag` it was asked with so the caller knows which field it was for.
    // Where the system has no dialogs (the Switch), or with
    // TSSM_FILE_PICKER=builtin, the GUI's own browser answers instead
    // (iGuiFilePicker.h), through the same PollFolder.
    void PickFolder(const char* tag, const char* start);
    bool PickingFolder();
    bool PollFolder(const char* tag, char* out, size_t outSize);

    // The same for a file to open or a file to save to; the answer comes
    // through PollFolder too, and PickingFolder is true while any of the three
    // dialogs is open (there is one at a time). `pattern` is SDL's:
    // extensions without the dot, ';'-separated ("gci;gcs"), or "*".
    struct FileFilter
    {
        const char* name;
        const char* pattern;
    };
    void PickOpenFile(const char* tag, const char* start, const FileFilter* filters, int count);
    void PickSaveFile(const char* tag, const char* start, const FileFilter* filters, int count);

    // The answer to the request open now: a path, or NULL for Cancel. For the
    // GUI's own browser (iGuiFilePicker.cpp).
    void AnswerPick(const char* path);

    // A whole file in or out, through SDL's file streams, so a path the
    // system's dialog answered with works whatever it is (an Android
    // content:// URI included). `why` gets a sentence on failure.
    bool ReadWholeFile(const char* path, std::vector<unsigned char>& out, char* why,
                       size_t whySize);
    bool WriteWholeFile(const char* path, const unsigned char* data, size_t n, char* why,
                        size_t whySize);

    // Opens `path` in the system's file browser.
    void OpenFolder(const char* path);

    // --- debug captures -----------------------------------------------------------

    // RGBA rows in, an RGB PNG out (alpha is the frame's, and means nothing
    // on screen). Uncompressed, so it needs no zlib.
    bool WritePNG(const char* path, const unsigned char* rgba, int width, int height,
                  int stride);
} // namespace iGui

#endif
