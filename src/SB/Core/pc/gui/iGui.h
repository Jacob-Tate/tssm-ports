#ifndef IGUI_H
#define IGUI_H

#include <types.h>

// PC-only: the port's own interface, drawn with Dear ImGui. There is no
// GameCube counterpart -- everything here is a question the console never had
// to ask (which renderer, which folder, which key). docs/GUI.md describes it
// from the player's side; this header is the contract with the rest of the
// platform layer.
//
// Two pieces, which never run at the same time:
//
//   THE LAUNCHER  a small window of its own, opened before the game's window,
//                 to choose the folder holding the game's files and the
//                 settings that only apply at startup. Shown when [gui]
//                 launcher says so -- by default only when the files are not
//                 where config.ini says. It draws through SDL's own renderer,
//                 because it runs before RenderWare exists.
//
//   THE OVERLAY   drawn over the game's picture, toggled with F1 (gui.key) or
//                 Back+Start on a controller. It draws with the ImGui backend
//                 that matches the librw device that is running (D3D9, D3D11 or
//                 OpenGL 3), at the WINDOW's resolution after the game's frame
//                 has been scaled into it, so it stays sharp whatever the render
//                 size. While it is open the game sees no input.
//
// Built only with TSSM_GUI=ON (the default). gui/iGuiNull.cpp stands in for
// this file otherwise, so the callers need no #ifdefs.

struct iPadHostState;

// --- the launcher -------------------------------------------------------------

// Shows the launcher if [gui] launcher (or TSSM_GUI_LAUNCHER) asks for it, and
// returns when the player presses Play. Quitting from it ends the process.
// Called by iSystemInit after config.ini is read and before anything reads a
// setting the launcher can change -- the asset folder and the startup-only
// video settings -- so whatever was chosen is in force for this run.
void iGuiLauncherRun();

// --- the overlay ----------------------------------------------------------------

// Hooks the overlay into the running device: librw's present, the SDL event
// queue and the ImGui renderer backend for the active librw backend. Called
// once RenderWare has started. Does nothing -- and says why, once -- on a
// backend the overlay cannot draw on (Vulkan, the null device).
void iGuiInit();
void iGuiShutdown();

bool iGuiIsOpen();
void iGuiSetOpen(bool open);

// The input the game should see on `port` this frame. iPadUpdate calls it
// with what the host backend reported, after the poll:
//
//   - the controller combo that opens the overlay is noticed here, so the
//     frame it is pressed on is already one the game does not see;
//   - while the overlay is open the answer is a connected pad with nothing
//     pressed, so the game neither acts on the overlay's navigation nor
//     reports the controller missing;
//   - once it closes, buttons that were held at the moment of closing stay
//     hidden from the game until they are let go -- otherwise the Start of
//     Back+Start would arrive as a fresh press and open the pause menu.
//
// Returns `host` itself when nothing applies.
const iPadHostState* iGuiFilterPad(S32 port, const iPadHostState* host);

// --- panels other modules fill ---------------------------------------------------
//
// The Controls, Saves and Mods tabs belong to the modules that own those
// things. Each module registers a draw function for its tab; it is called
// between ImGui::NewFrame and ImGui::Render, inside the tab, and may use any
// ImGui call. Until one is registered the tab shows the port's built-in
// placeholder (gui/iGuiTabs.cpp, gui/iGuiControls.cpp).
enum iGuiPanel
{
    IGUI_PANEL_CONTROLS,
    IGUI_PANEL_SAVES,
    IGUI_PANEL_MODS,
    IGUI_PANEL_COUNT
};

typedef void (*iGuiPanelFn)(void* user);

// NULL `draw` puts the placeholder back.
void iGuiSetPanel(iGuiPanel panel, iGuiPanelFn draw, void* user);

// The Settings tab's audio sliders are stored in config.ini ([audio]
// master_volume, music_volume, sfx_volume, movie_volume; 0 to 100) and handed
// to whoever owns the mixer through this. Called once on registration with the
// stored values and again whenever a slider moves. Volumes are 0..1.
typedef void (*iGuiAudioFn)(F32 master, F32 music, F32 sfx, F32 movies);
void iGuiSetAudioListener(iGuiAudioFn fn);

#endif
