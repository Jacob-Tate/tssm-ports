#include "xDebug.h"
#include "xDebugTweak.h"

#include <rwcore.h>

// The debug-tweak system is compiled out of the retail build: the callbacks survive as empty
// functions referenced by the callback tables below.

void tweak_rtod(const tweak_info& info, void* value)
{
}

void tweak_dtor(const tweak_info& info, void* value)
{
}

const tweak_callback cb_tweak_convert_degrees = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    (void (*)(tweak_info&, void*))tweak_rtod,
    (void (*)(tweak_info&, void*))tweak_dtor,
};

void tweak_force_lt(const tweak_info& info)
{
}

void tweak_force_gt(const tweak_info& info)
{
}

void tweak_force_lteq(const tweak_info& info)
{
}

void tweak_force_gteq(const tweak_info& info)
{
}

const tweak_callback cb_tweak_force_lt = { (void (*)(tweak_info&))tweak_force_lt };
const tweak_callback cb_tweak_force_gt = { (void (*)(tweak_info&))tweak_force_gt };
const tweak_callback cb_tweak_force_lteq = { (void (*)(tweak_info&))tweak_force_lteq };
const tweak_callback cb_tweak_force_gteq = { (void (*)(tweak_info&))tweak_force_gteq };

// Debug-tweak blend mode tables (PS2 DWARF order). The label array is unreferenced in TSSM's
// GameCube build and was dead-stripped by the linker, but its strings open the wad's string pool;
// the value tables survive in .rodata.
char* tweak_blend_mode_labels[11] = { "ZERO",      "ONE",          "SRCCOLOR",   "INVSRCCOLOR",
                                      "SRCALPHA",  "INVSRCALPHA",  "DESTALPHA",  "INVDESTALPHA",
                                      "DESTCOLOR", "INVDESTCOLOR", "SRCALPHASAT" };
const U32 tweak_blend_mode_values[11] = {
    rwBLENDZERO,      rwBLENDONE,          rwBLENDSRCCOLOR,   rwBLENDINVSRCCOLOR,
    rwBLENDSRCALPHA,  rwBLENDINVSRCALPHA,  rwBLENDDESTALPHA,  rwBLENDINVDESTALPHA,
    rwBLENDDESTCOLOR, rwBLENDINVDESTCOLOR, rwBLENDSRCALPHASAT
};
const U8 tweak_blend_mode_values8[11] = {
    rwBLENDZERO,      rwBLENDONE,          rwBLENDSRCCOLOR,   rwBLENDINVSRCCOLOR,
    rwBLENDSRCALPHA,  rwBLENDINVSRCALPHA,  rwBLENDDESTALPHA,  rwBLENDINVDESTALPHA,
    rwBLENDDESTCOLOR, rwBLENDINVDESTCOLOR, rwBLENDSRCALPHASAT
};
