#ifndef IFRAMEGRAB_H
#define IFRAMEGRAB_H

#include <types.h>

// PC-only. Writes the frame about to be presented to `path` as a PNG,
// area-averaged down to at most `maxWidth` pixels wide; 0 keeps the full size.
// FALSE when there is no frame to read or the file cannot be written. Call it
// from iSnapshotCapture's frame hook, which runs before the flip.
//
// For iTour.h. Reads the game's own render target, never the desktop.
S32 iFrameGrabWrite(const char* path, S32 maxWidth);

// Writes any raster librw can read back (a texture, a camera texture) to
// `path` as an RGB PNG at its full size. FALSE when it cannot be read or the
// file cannot be written. For the Xbox water's target dumps (iXboxWater.h).
S32 iFrameGrabWriteRaster(const char* path, struct RwRaster* raster);

#endif
