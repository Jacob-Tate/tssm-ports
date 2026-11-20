#ifndef ILIGHTVOLUME_H
#define ILIGHTVOLUME_H

#include <rwcore.h>
#include <rpworld.h>

#include <types.h>

// PC-only: the Xbox release's spotlight light volumes, the bright patch a beam
// casts on what it shines on. The GameCube release never draws them: its
// xLightVolume::render_all and render_atomic are empty, and so are the port's
// unless [extras] spotlights = xbox (iExtras.h, docs/EXTRAS.md).
//
// What the Xbox does (DEFAULT.XBE: xLightVolume::render_all 0x185640,
// render_atomic 0x17ece0, its state save 0x17e9f0 and restore 0x17eaa0):
//
//   render_all, after the scene's opaque and alpha models: alpha writes on,
//   the light kit off, zRenderState(SDRS_LightVolume); then for each active
//   volume the stencil buffer is cleared and the volume drawn; then the states
//   are put back, culling off and zRenderState(SDRS_OpaqueModels).
//
//   render_atomic draws the spotlight's cone three times, z writes off, with
//   the stencil "z-fail" method (a global switch at 0x2cb564 is 1, so the
//   volume's own "inside" flag is never read):
//
//     1. The cone's inner faces, depth tested: where a face is hidden the
//        stencil goes up by one. Colour is written too, with the blend of
//        pass 3 and the colours pass 3 left behind (below).
//     2. The outer faces, no colour: where hidden, the stencil goes down.
//     3. The inner faces again with no depth test, where the stencil is 1 --
//        the scene inside the cone -- in the light's colour (a black light
//        becomes white).
//
//   The blend is DESTCOLOR / CONSTANTALPHA: the picture times (colour + the
//   light's alpha), with the light's colour set as the cone's material
//   colour and as the blend constant in pass 3. In B401 (Turn the Tables on
//   Plankton) a check on the scene ID (0x17ed01) switches to plain adding,
//   ONE/ONE, with no alpha write in pass 3.
//
//   Pass 1 runs before pass 3 sets anything, so it draws with the material
//   colour this cone shape last had and the blend constant the last volume
//   drawn left: the "odd blending" of No Cheese!'s lights that TCRF mentions.
//   It brightens the far side of the whole beam, not only the lit patch.
//
// Here:
//
//   - The cone is closed with a fan at each end (the Xbox's 0x5f5a0 closes it
//     too; the GameCube's cone is an open tube, which the z-fail count needs
//     closed), and drawn as an Im3D triangle list.
//   - librw has no constant blend colour, so the light's alpha rides in the
//     vertex alpha and the blend is DESTCOLOR / SRCALPHA, which is the same
//     sum. Pass 1's leftovers are kept here: the colour per cone shape, the
//     alpha across all of them.
//   - The Xbox's z bias of 1 (0x17e9f0) is not applied.
//   - GLES with anti-aliasing through EXT_multisampled_render_to_texture
//     (TSSM_GPU_MSRTT, off by default) has no stencil buffer: no volumes there,
//     said once in the log.

// Whether a device is open whose frame has a stencil buffer. (Whether the
// volumes are wanted, spotlights = xbox, is xLightVolume.cpp's question.)
bool iLightVolumeReady();

// Around the volumes of a frame: saves the render states the passes change,
// and puts them back.
void iLightVolumeBegin();
void iLightVolumeEnd();

// Clears the stencil buffer of the camera being rendered (0x185640 does this
// before each volume).
void iLightVolumeClearStencil();

// The three passes for one volume: `atomic` is the spotlight's cone, placed by
// its frame; `color` the light's colour; `additive` the B401 rule.
void iLightVolumeRender(RpAtomic* atomic, RwRGBA color, bool additive);

// Called as a level makes its spotlights' cones (zSpotlight::setup_all), which
// start white: forgets the colours pass 1 kept for the last level's cones,
// whose atomics are gone and whose addresses a new cone may reuse.
void iLightVolumeForgetCones();

// How many cone shapes have a remembered colour (TSSM_TEST_SPOTLOG).
S32 iLightVolumeConeCount();

#endif
