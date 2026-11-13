#ifndef XSCRFX_H
#define XSCRFX_H

#include "xCamera.h"
#include "iColor.h"

#include <rwcore.h>

void xScrFxInit();
void xScrFxReset();
void xScrFxUpdate(RwCamera* cam, F32 dt);
void xScrFxRender(RwCamera*);
void xScrFxDrawScreenSizeRectangle();
void xScrFxFade(const xColor_tag* base, const xColor_tag* dest, F32 seconds, S32 hold);
void xScrFxStopFade();
U8 xScrFxIsFading();
void xScrFxLetterboxReset();
void xScrFxLetterBoxSetSize(F32 size);
void xScrFxLetterBoxSetAlpha(U8 alpha);
void xScrFxLetterbox(S32 enable);
void xScrFxDrawSafeArea();
S32 xScrFXGlareAdd(const xVec3* pos, F32 life, F32 intensity, F32 size, F32 r, F32 g, F32 b, F32 a,
                   RwRaster* raster);
void xScrFXGlareDisable(S32 id);
void xScrFXGlareChange(S32 id, const xVec3* pos, F32 life, F32 intensity);
U8 xScrFXGlareActive(S32 id);
void xScrFXFullScreenGlareRender();
void xScrFxDrawBox(F32 x1, F32 y1, F32 x2, F32 y2, U8 red, U8 green, U8 blue, U8 alpha, F32 ushift,
                   F32 vshift);
void xScrFXGlareRender(const xMat4x3& mat);
void xScrFxLensFlare_SceneExit();
void xScrFxLensFlare_SceneEnter();
void xScrFxLensFlare_Render(RwCamera* cam);
void xScrFxRenderLetterBox(RwCamera* cam);

#endif
