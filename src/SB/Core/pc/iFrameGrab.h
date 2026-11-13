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

// The last iFrameGrabWrite's parts, in milliseconds: reading the frame back,
// building the PNG, and writing the file.
void iFrameGrabLastTimes(F32* readMs, F32* encodeMs, F32* writeMs);

#endif
