// PC: the Dolphin SDK's GX enumerations, taken unchanged from the decomp's own
// copy of the SDK header (src/dolphin/include/dolphin/gx/GXEnum.h). It is plain
// enums and #defines with nothing GameCube-specific to compile, and the GX
// constants shared code uses (GX_ALWAYS, GX_TF_RGBA8, ...) must keep their SDK
// values, so it is forwarded rather than copied. See ../../dolphin.h.
#pragma once
#include "../../../../../../dolphin/include/dolphin/gx/GXEnum.h"
