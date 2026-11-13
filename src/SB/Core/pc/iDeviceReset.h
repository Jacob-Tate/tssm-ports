#ifndef IDEVICERESET_H
#define IDEVICERESET_H

// PC-only: the Direct3D 9 device's resets, kept to the moments they can
// succeed, and the drawing that cannot survive a failed one held back.
//
// **What librw does.** A D3D9 device is reset in two places in librw's
// d3ddevice.cpp: beginUpdate, whenever the window's client area is not the back
// buffer's size and the window is not minimised, and showRaster, when Present
// says the device is lost and TestCooperativeLevel says it can be reset. Both
// release everything in D3DPOOL_DEFAULT that librw made (the Im2D and Im3D
// vertex and index buffers, camera textures, private depth surfaces, the
// virtual screen), call Reset, and make it all again -- without looking at what
// Reset returned. When the reset fails, nothing in the default pool can be
// made, the dynamic buffers come back NULL, and the next 2D draw (iScrFxDrawBox
// under the screen fade, say) copies its vertices to address 0. A lock that
// failed on a buffer that does exist leaves librw an uninitialised pointer
// instead, which writes somewhere at random; the "unsupported instV3d type"
// assertion is one place that surfaces.
//
// **How resets came to fail.** Two ways, each enough on its own:
//
//   - Restoring a minimised window from another process -- the single-instance
//     check raising the first copy, the taskbar, ShowWindow from a script --
//     clears the window's minimised state BEFORE it gives the window its size
//     back, and the game's thread runs in between. For that moment the window
//     is not iconic and its client area is 0x0, which beginUpdate takes as a
//     resize to 0x0. A windowed device's back buffer of 0x0 means "the client
//     area", which is empty, and Reset refuses it (D3DERR_INVALIDCALL).
//     Borderless got away with it by timing, not by design.
//   - librw's rasterLock keeps a reference to every camera texture it reads
//     back (see RasterLock in rw/devicereset.cpp), so after the first save
//     thumbnail or TSSM_CAPTURE_FRAMES frame one default-pool texture can never
//     be destroyed, and every reset after it -- a resize, Alt+Enter, F11 --
//     failed.
//
// A reset while the device is genuinely lost (D3DERR_DEVICELOST, not yet
// resettable) fails too, as does one while the overlay's ImGui objects -- also
// in the default pool -- are alive.
//
// **And a reset that succeeds can bind a freed buffer.** librw keeps a cache
// of the index buffer, vertex streams and vertex declaration it has bound and
// sets them only when they differ from it. Destroying a geometry -- every model
// of a scene that is torn down -- leaves its buffers in that cache, alive only
// through the device's binding. releaseVideoMemory unbinds the index buffer,
// the declaration and stream 0 on the device without touching the cache, which
// frees them, and the restore after Reset binds what the cache names. A movie
// makes no indexed draws, so the last scene's index buffer was still cached,
// and resizing the window during story1.bik crashed in restoreD3d9Device.
//
// **What this does.** It takes over librw's beginUpdate and showRaster (the
// overlay's hook on showRaster then sits in front of this one) and the D3D9
// driver's rasterLock:
//
//   - librw may reset only with a client area that is not empty, a window that
//     is not minimised and a device that TestCooperativeLevel says is fine or
//     can be reset. Otherwise librw is shown the window's client area as the
//     back buffer's size for the length of the call, so it sees nothing to
//     resize, and the reset happens on a later frame when it can succeed.
//   - before any reset it allows, the overlay's default-pool objects are
//     released (the hook below), because the event that would otherwise have
//     released them has not been pumped yet when a restore races the frame.
//     They are also released when a reset is held back from a window that is
//     not minimised: librw reads the window again after this file has, and
//     if the size arrives in between -- the 0x0 of a restore ending -- librw
//     resets after all.
//   - librw's binding cache is emptied, through librw's own setters, at every
//     camera update and present, whichever of librw's reads ends up
//     resetting; the next draw binds what it draws with. Its constant vertex
//     stream (stream 2), which lives as long as the device and is bound once,
//     is left bound.
//   - a device that can be reset and has not been is reset at the next
//     beginUpdate, at the window's size, and only a device that is fine is
//     presented -- so librw's present never resets one it was handed lost.
//   - the reference librw's rasterLock keeps is given back.
//
// While the device is lost, Im2D and Im3D drawing and RwRasterRenderFast are
// skipped (their buffers or targets may not exist), so is the present, and the
// screen passes see iScreenPassReady() FALSE. The rest of the frame runs as
// always: draws into a lost device are no-ops in D3D9. When the reset succeeds
// librw has made everything again and drawing resumes.
//
// A reset that fails anyway (none does in any case known) shows as a device
// that still asks to be reset afterwards, or in librw's own counts of what it
// holds, which come out of a failed restore lower than they went in. It leaves
// the device as it is for good: librw cannot safely try again (a second
// attempt releases the private depth surfaces the first could not make,
// through NULL), so the 2D drawing and the present stay held and the log says
// the picture has stopped. That is a last line of defence, not a recovery:
// geometry librw instances for the first time on a device that failed to reset
// can still fault inside librw, which nothing outside it can prevent.
//
// A device that is merely LOST again after a reset (an exclusive full screen
// switched away from at that instant) is not that: with librw's counts whole,
// everything it releases next exists, so the device is held like any lost one
// and reset again once it can be. TSSM_TEST_DEVICE_LOST=reset walks that path.
//
// Direct3D 11, OpenGL and Vulkan have no such reset in librw; this does
// nothing on them.

// Installed by RwEngineStart once the D3D9 device is up, removed by
// RwEngineStop. Nothing on any other backend.
void iDeviceResetInstall();
void iDeviceResetRemove();

// Something outside librw that keeps objects in D3DPOOL_DEFAULT and must let
// them go before any reset: the overlay's ImGui renderer. Called right before
// every reset this file lets librw do, at every camera update that holds a
// reset back from a window that is not minimised, and before every present of
// a device that is not usable. One hook; NULL removes it.
void iDeviceResetSetReleaseHook(void (*release)());

// FALSE while the D3D9 device is lost or could not be reset, as of the last
// camera update or present. TRUE on every other backend.
bool iDeviceUsable();

#endif
