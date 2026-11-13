#include "xRenderState.h"

static S32 sRenderStateBlendTable[11] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

// No symbols in the retail DOL: always inlined (only caller is xParSysRender).
inline void xRenderStateSetTexture(RwTexture* texture)
{
    if (texture != NULL)
    {
        if (texture->raster != NULL)
        {
            RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)texture->raster);
        }
    }
    else
    {
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
    }
}

inline void xRenderStateSetSrcBlendMode(S32 xmode)
{
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)sRenderStateBlendTable[xmode]);
}

inline void xRenderStateSetDstBlendMode(S32 xmode)
{
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)sRenderStateBlendTable[xmode]);
}

// Type and linkage from Ratatouille's GC DWARF (retail's symbol is local too).
static void* old_alpha_discard_value_gc;
bool alphaDiscardCurrentlySet;

void xRenderSceneEnter()
{
}

void xRenderSceneExit()
{
}

void xRenderStateSetAlphaDiscard(S32 value)
{
    alphaDiscardCurrentlySet = true;
    RwEngineInstance->dOpenDevice.fpRenderStateGet(rwRENDERSTATEALPHATESTFUNCTIONREF,
                                                   &old_alpha_discard_value_gc);
    RwGameCubeSetAlphaCompare(6, value, 0, 6, value);
    if (value > 0)
    {
        _rwDlRenderStateSetZCompLoc(FALSE);
    }
    else
    {
        _rwDlRenderStateSetZCompLoc(TRUE);
    }
}

void xRenderStateResetAlphaDiscard()
{
    alphaDiscardCurrentlySet = false;
#ifdef PLATFORM_PC
    // PLATFORM_PC: a pointer cast straight to a byte is an error for clang in GNU mode (the
    // Android NDK); MSVC-mode clang only warns. The same low byte, through size_t.
    // RwRenderStateGet stores the reference as 4 bytes into the zero-initialised static.
    RwGameCubeSetAlphaCompare(6, (RwUInt8)(size_t)old_alpha_discard_value_gc, 0, 6,
                              (RwUInt8)(size_t)old_alpha_discard_value_gc);
    if ((RwUInt8)(size_t)old_alpha_discard_value_gc > 0)
#else
    RwGameCubeSetAlphaCompare(6, (RwUInt8)old_alpha_discard_value_gc, 0, 6,
                              (RwUInt8)old_alpha_discard_value_gc);
    if ((RwUInt8)old_alpha_discard_value_gc > 0)
#endif
    {
        _rwDlRenderStateSetZCompLoc(FALSE);
    }
    else
    {
        _rwDlRenderStateSetZCompLoc(TRUE);
    }
}

void xRenderFixUntexturedBegin(RpAtomic* atomic)
{
}

void xRenderFixUntexturedEnd(RpAtomic* atomic)
{
}

void xRenderFixIMBegin()
{
}

void xRenderFixIMEnd()
{
}
