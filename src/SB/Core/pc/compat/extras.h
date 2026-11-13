#ifndef TSSM_PC_COMPAT_EXTRAS_H
#define TSSM_PC_COMPAT_EXTRAS_H

// MSL's <extras.h>: stricmp, strnicmp and strupr. The first two come from
// compat/string.h. strupr is MSL's in-place uppercase; the Microsoft CRT has it
// as _strupr and POSIX has nothing; compat/string.h defines it for POSIX hosts,
// and it is defined here for MinGW.
#include <string.h>
#include <ctype.h>

#if defined(__MINGW32__)
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
