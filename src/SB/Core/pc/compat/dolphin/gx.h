// PC shadow of the Dolphin SDK's <dolphin/gx.h>: the part of GX that shared
// game code still names, declared with the SDK's own signatures.
//
// The port has no GX. The two shared units that drove it by hand,
// xFXHighDynamicRange.cpp (the HDR glow) and xScreenWarp.cpp (the screen
// warp), call the port's render passes instead on PLATFORM_PC (iHDRGlow.h,
// iScreenWarp.h), so their GX calls are not compiled here; they still include
// this header for the types their file-scope data is declared with (the
// hdr_* blend factors). What is left is GXDrawDone, which zAsync.cpp calls
// (stubs/dolphin_gx.cpp), and the GX_* constants xHudImage.cpp passes to
// RwGameCubeSetAlphaCompare.
//
// The enumerations are the SDK's (gx/GXEnum.h).
#pragma once

#include <dolphin/types.h>
#include <dolphin/gx/GXEnum.h>

#ifdef __cplusplus
extern "C" {
#endif

// GXStruct.h: the structures shared code names (GXRenderModeObj is left out).
typedef struct _GXColor
{
    u8 r;
    u8 g;
    u8 b;
    u8 a;
} GXColor;

typedef struct _GXColorS10
{
    s16 r;
    s16 g;
    s16 b;
    s16 a;
} GXColorS10;

typedef struct _GXTexObj
{
    u32 dummy[8];
} GXTexObj;

typedef struct _GXTlutObj
{
    u32 dummy[3];
} GXTlutObj;

typedef struct _GXLightObj
{
    u32 dummy[16];
} GXLightObj;

// GXManage.h
void GXDrawDone(void);

#ifdef __cplusplus
}
#endif
