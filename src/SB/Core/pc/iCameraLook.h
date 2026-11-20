#ifndef ICAMERALOOK_H
#define ICAMERALOOK_H

#include <types.h>

// PC-only: the camera turned by a displacement -- a finger dragged across the
// screen, a mouse moved -- rather than by a stick setting a rate. zCamera.cpp's
// orbit reads it and owns the angles; this only says how far the hand moved
// this frame. From treedome, the BfBB PC port.
//
// Not fed through the C-stick. The retail camera's pitch is a blend between two
// fixed poses that eases back when the stick is let go, and it swings behind
// the player on its own, so a drag written as a stick deflection springs back.
// The stick path also has a deadzone at 32 and a clamp at 110, and its constant
// converts a RATE, so a mouse fed through it turns no further however fast the
// hand moves.

// Who is turning the camera. Each source is set on its own and the camera sees
// the sum, so a touchscreen laptop with a mouse plugged in can use either.
enum iCameraLookSource
{
    iCAMERALOOK_TOUCH,
    iCAMERALOOK_MOUSE,
    iCAMERALOOK_SOURCE_COUNT
};

// This frame's turn from one source, in radians, replacing that source's last.
// Yaw: positive turns the view to the right. Pitch: positive raises the camera,
// which looks DOWN -- the engine's pitch is positive-down.
void iCameraLookSet(iCameraLookSource source, F32 yaw, F32 pitch);

// Whether a source is steering. The orbit owns the camera while any source is,
// and the retail camera does not swing behind the player by itself.
void iCameraLookSetActive(iCameraLookSource source, S32 on);

// Declared again in xPad.h for zCamera.cpp, which is game code and already
// includes that header. iCameraLookTake hands over the turn (consumed by the
// read) and answers whether any source is steering; calling it also says that
// the on-foot camera ran this frame.
S32 iCameraLookActive();
S32 iCameraLookTake(F32* yaw, F32* pitch);

// Once per input poll, before the sources. iCameraLookTaken then answers
// whether the on-foot camera asked for the turn since the poll before: it does
// not during a cutscene or a fly-through, or while another camera (a boss
// fight's, say) has the view, and those read only the C-stick.
void iCameraLookPoll();
S32 iCameraLookTaken();

#endif
