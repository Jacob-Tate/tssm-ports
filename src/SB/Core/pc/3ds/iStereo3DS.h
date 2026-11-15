#ifndef ISTEREO3DS_H
#define ISTEREO3DS_H

// The 3DS renderer's stereo hook (rw::c3d::setStereoFocusHook): the game's main
// camera, the player's position and whether the camera follows the player,
// handed to rw::c3d::stereoFocus. Set at the start (3ds/i3DS.cpp), called by
// the renderer once a frame. 3DS only.
void iStereo3DSFocus();

#endif
