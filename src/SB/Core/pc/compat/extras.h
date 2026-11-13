#ifndef TSSM_PC_COMPAT_EXTRAS_H
#define TSSM_PC_COMPAT_EXTRAS_H

// MSL's <extras.h>: stricmp, strnicmp and strupr. The first two come from
// compat/string.h. strupr is MSL's in-place uppercase; the Microsoft CRT has it
// as _strupr and POSIX has nothing, so it is defined here.
#include <string.h>
#include <ctype.h>

// newlib (the Switch's C library) has its own, declared in <string.h>.
#if (!defined(_WIN32) || defined(__MINGW32__)) && !defined(__SWITCH__)
static inline char* strupr(char* str)
{
    for (char* p = str; *p != '\0'; p++)
    {
        *p = (char)toupper((unsigned char)*p);
    }
    return str;
}
#endif

#endif
