#ifndef ID0STATSRW_H
#define ID0STATSRW_H

#include <types.h>

#include <stdio.h>

// Between iD0Stats.cpp and its librw half, rw/d0rw.cpp. Nothing else uses it.

// What the object pipelines were asked to draw since the last take.
struct iD0RwFrame
{
    S32 atomics;
    S32 meshes;
    S32 tris;
    S32 skinAtomics;
    S32 skinVerts;
    S32 maxBones;
    S32 litAtomics;
    S32 maxDirLights;
    S32 maxLocalLights;
    S32 maxAmbients;
    S32 dirLightSum;
    S32 localLightSum;
};

void iD0RwWrapPipelines();
void iD0RwTakeFrame(iD0RwFrame* out);
void iD0RwReport(FILE* f);

FILE* iD0File();
const char* iD0SceneName();
void iD0AddTex3DS(S64 startUs, U32 bytes, S32 format);

#endif
