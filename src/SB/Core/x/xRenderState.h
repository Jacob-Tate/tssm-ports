#ifndef XRENDERSTATE_H
#define XRENDERSTATE_H

#include <rwcore.h>
#include <rpworld.h>
#include <types.h>

void xRenderStateSetDstBlendMode(S32 xmode);
void xRenderStateSetSrcBlendMode(S32 xmode);
void xRenderStateSetTexture(RwTexture* texture);
void xRenderStateSetAlphaDiscard(S32 value);
void xRenderStateResetAlphaDiscard();
void xRenderSceneExit();
void xRenderSceneEnter();
void xRenderFixIMEnd();
void xRenderFixIMBegin();
void xRenderFixUntexturedEnd(RpAtomic* atomic);
void xRenderFixUntexturedBegin(RpAtomic* atomic);

#endif
