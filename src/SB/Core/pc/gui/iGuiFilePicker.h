#ifndef IGUIFILEPICKER_H
#define IGUIFILEPICKER_H

// The GUI's own file browser, for where the system has no file dialog: the
// Switch (SDL's Switch port answers "unsupported" to every dialog), or a PC or
// phone where the system's dialog failed. TSSM_FILE_PICKER=builtin, or
// [gui] file_picker = builtin, uses it on a PC too, for testing.
//
// iGui::PickFolder / PickOpenFile / PickSaveFile (iGuiInternal.h) decide which
// one answers; either way the answer comes back through iGui::PollFolder, so
// the tabs that ask do not know the difference. This browser:
//
//   - opens a file or a folder: a list of folders and of the files the
//     dialog's filter takes, sorted, with Up, the card's (or drive's) top
//     folder and the folder it started in a press away. A controller drives
//     it (d-pad or stick, the accept button opens or chooses, the back button
//     returns to the folder before or cancels), and so do touch and a mouse,
//     a long list scrolling under a dragged finger;
//   - saves to a file without asking: the file goes into the export folder
//     (TransferDir("export")) under the name the caller suggested, made
//     unique, and the caller's message says where it went.

#include "gui/iGuiInternal.h"

namespace iGui
{
    enum PickMode
    {
        PICK_FOLDER,
        PICK_OPEN_FILE,
        PICK_SAVE_FILE
    };

    // Whether the GUI's browser answers instead of the system's dialogs:
    // always on the Switch; on other systems when TSSM_FILE_PICKER or
    // [gui] file_picker says "builtin", or after a system file dialog has
    // failed this run. "system" forces the system's dialogs (except on the
    // Switch, which has none).
    bool UseBuiltinPicker();

    // A system dialog failed: the GUI's browser takes this request, and,
    // when it was a file dialog, every later one (a failed folder dialog --
    // Android has none -- leaves the file dialogs to the system). Safe from
    // any thread (SDL may answer on its own).
    void NoteSystemDialogFailed();

    // The folder saves are imported from or exported to: "<base>/import" or
    // "<base>/export", where <base> is TSSM_FILE_PICKER_DIR when set, else the
    // per-user data folder's tssm/ (/switch/tssm on the Switch). Made if it is
    // not there yet.
    void TransferDir(const char* which, char* out, size_t size);

    // `dir`/`name`, with "-2", "-3"... before the extension until nothing by
    // that name is there.
    void UniquePath(const char* dir, const char* name, char* out, size_t size);

    // Starts the browser for a request iGuiUtil.cpp has already claimed
    // (BeginPick). PICK_SAVE_FILE answers at once.
    void FilePickerStart(PickMode mode, const char* start, const FileFilter* filters, int count);

    // The request a system dialog is about to be shown for, kept so the
    // browser can take it over if the dialog fails.
    void FilePickerRemember(PickMode mode, const char* start, const FileFilter* filters,
                            int count);

    // The browser, when it is open: once a frame, inside the launcher's or the
    // overlay's window. Also starts the browser for a system dialog that
    // failed.
    void DrawFilePicker();

    // Whether Esc closed the browser this frame (so it closes nothing else).
    bool FilePickerTookEscape();
} // namespace iGui

#endif
