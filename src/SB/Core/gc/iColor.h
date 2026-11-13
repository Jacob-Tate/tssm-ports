#ifndef ICOLOR_H
#define ICOLOR_H

#include <types.h>
#include "rwplcore.h"

// TSSM renamed iColor_tag to xColor_tag (only xColor_tag appears in the target's mangled names).
// Unlike BfBB's iColor_tag it has no user-defined operator=: the target copies it as a single word.
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
