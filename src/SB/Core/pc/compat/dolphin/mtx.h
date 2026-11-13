// PC shadow of the Dolphin SDK's <dolphin/mtx.h>: the matrix types only.
// xFXHighDynamicRange.cpp and xScreenWarp.cpp include it; the GX code that
// called PSMTXIdentity and C_MTXOrtho is the GameCube branch of each, and the
// PC branch calls the port's render passes instead. See ../dolphin.h.
#pragma once

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef float Mtx[3][4];
typedef float (*MtxPtr)[4];
typedef float Mtx44[4][4];

#ifdef __cplusplus
}
#endif
