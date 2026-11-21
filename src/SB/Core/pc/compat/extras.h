#ifndef TSSM_PC_COMPAT_EXTRAS_H
#define TSSM_PC_COMPAT_EXTRAS_H

// MSL's <extras.h>: stricmp, strnicmp and strupr. The first two come from
// compat/string.h. strupr is MSL's in-place uppercase. The Microsoft CRT and
// MinGW's <string.h> both declare it (MinGW's is an extern declaration, so a
// static one here is an error under GCC); POSIX has nothing, and
// compat/string.h defines it there (zMain.cpp calls it with only <string.h>
// included).
#include <string.h>
#include <ctype.h>

#endif
