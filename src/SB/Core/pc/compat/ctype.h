#ifndef TSSM_PC_COMPAT_CTYPE_H
#define TSSM_PC_COMPAT_CTYPE_H

// MSL's <ctype.h> carries table-lookup helpers under underscored names, and
// zMain.cpp (the SB.INI reader) and zRewardsMgr.cpp call two of them. The host
// has neither as MSL means it: the Microsoft CRT has no _isspace at all, and
// its _toupper is the unchecked `c - 'a' + 'A'`, which turns a digit or an
// upper-case letter into garbage where MSL's returns it unchanged. So both are
// defined here with MSL's meaning, over the host's own classification.
#include_next <ctype.h>

#ifdef _toupper
#undef _toupper
#endif
#ifdef _tolower
#undef _tolower
#endif

static inline int tssm_msl_isspace(int c)
{
    return c == -1 ? 0 : isspace((unsigned char)c);
}

static inline int tssm_msl_toupper(int c)
{
    return c == -1 ? -1 : toupper((unsigned char)c);
}

static inline int tssm_msl_tolower(int c)
{
    return c == -1 ? -1 : tolower((unsigned char)c);
}

#define _isspace(c) tssm_msl_isspace(c)
#define _toupper(c) tssm_msl_toupper(c)
#define _tolower(c) tssm_msl_tolower(c)

#endif
