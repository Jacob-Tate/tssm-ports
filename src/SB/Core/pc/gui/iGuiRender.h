#ifndef IGUIRENDER_H
#define IGUIRENDER_H

#include <types.h>

// PC-only: the overlay's renderer, the one part of the GUI that knows which
// librw device is running. See iGuiRender.cpp for how each backend gets the
// overlay onto the screen without disturbing the game's own rendering.

// Called at every present while the overlay is in use. Builds one ImGui frame
// -- NewFrame through Render -- and returns TRUE when there is draw data to put
// on screen. FALSE leaves the present to librw exactly as if the GUI did not
// exist, which is the closed overlay's whole per-frame cost.
typedef bool (*iGuiRenderFrameFn)();

// The ImGui renderer backend for the running device, and the hook on librw's
// present. FALSE, with nothing installed, when the device is one the overlay
// cannot draw on. `name` gets a word for the log either way.
bool iGuiRenderInit(iGuiRenderFrameFn frame, const char** name);
void iGuiRenderShutdown();

// Inside the frame, in this order: before ImGui::NewFrame, and right after it.
// The second puts the game's picture into the background draw list on a
// backend that presents it through ImGui (D3D11); nothing on the others.
void iGuiRenderNewFrame();
void iGuiRenderBackground();

// The GUI's device objects that would not survive a device reset are released
// here and made again on the next frame that needs them. Called when the
// overlay closes and whenever the window changes in a way that makes librw
// reset the D3D9 device (size, display, fullscreen). Nothing on the others.
void iGuiRenderReleaseVolatile();

// The 3DS's bottom screen: the next pictures drawn there scaled toward black,
// 256 as drawn, 0 black (the closed overlay's hint fading out). A new value
// draws the next picture even if ImGui's is unchanged. Nothing elsewhere.
void iGuiRenderSetBrightness(int bright);

// The next present that draws the overlay also writes the window's back
// buffer -- the game's picture and the overlay on it, exactly as presented --
// to `path` as a PNG. For TSSM_GUI_CAPTURE.
void iGuiRenderRequestCapture(const char* path);

// TRUE once the capture asked for above has been written (or has failed,
// which is logged).
bool iGuiRenderCaptureDone();

#endif
