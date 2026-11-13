#ifndef ISCREENWARP_H
#define ISCREENWARP_H

#include <rwcore.h>

#include <types.h>

// PC-only: xScreenWarp's draw, the screen ripple of shockwaves, explosions,
// heat haze and the rest (xScreenWarp.cpp's render_uvmap), with a render
// target instead of GX. On PC render_uvmap calls this in place of its GameCube
// body (PLATFORM_PC).
//
// The game keeps a (cells + 1) x (cells + 1) grid of texture coordinates
// over the screen, 16 cells a side, each point starting where it lies (u =
// i / 16, v = j / 16) and pushed about by every active warp's animation.
// The console then:
//
//   - copies the whole EFB into a 640x480 RGB565 texture (GXCopyTex),
//   - draws it back over the screen as 16 columns of triangle strips, one per
//     column of cells, each vertex at its grid point and sampling the copy at
//     the grid's coordinates there; texture colour only, blended ONE/ZERO, no
//     depth test, and the alpha write left as iDrawSetFBMSK set it.
//
// (Retail draws a 17th strip that starts at the right edge and reads one row
// past the map; it is off the screen and is not drawn here.)
//
// This copies the virtual screen instead and draws the same strips with
// librw's 2D pipeline, at the render size. The copy keeps 8 bits per channel
// where the console's keeps 5-6-5.
//
//   cam    the camera being rendered, in an update; its update is ended and
//          begun again around the copy.
//   uv     the grid, as xScreenWarp keeps it: point (i, j) -- column i, row j
//          -- is uv[2 * (i * (cells + 1) + j)] and the float after it.
//   cells  cells per side (16).
void iScreenWarpRender(RwCamera* cam, const F32* uv, S32 cells);

// Whether iScreenWarpRender does anything; config.ini's video.screen_warp,
// pushed down from iSystem.cpp at startup. On by default, as on the console.
void iScreenWarpSetEnabled(S32 enabled);

#endif
