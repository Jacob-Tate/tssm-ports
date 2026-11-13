#ifndef ICOLOR_H
#define ICOLOR_H

#include <types.h>
#include "rwplcore.h"

// TSSM renamed iColor_tag to xColor_tag (only xColor_tag appears in the target's mangled
// names); the PS2 DWARF lists r, g, b, a and rgba, which overlap (4-byte color, see xScrFxFade).
// Unlike BfBB's iColor_tag it has no user-defined operator=: TSSM has no __as__10xColor_tag and the
// target copies it as a single word (xScrFxFade, zMainShowProgressBar, zUI::DoInitMotion,
// zUI::DoApplyMotionFrame).
struct xColor_tag
{
    union
    {
        struct
        {
            U8 r;
            U8 g;
            U8 b;
            U8 a;
        };
        RwRGBA rgba;
    };
};

typedef xColor_tag iColor_tag;

#endif
