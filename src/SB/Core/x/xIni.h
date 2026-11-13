#ifndef XINI_H
#define XINI_H

#include <types.h>

#include "xString.h"

struct xIniValue
{
    char* tok;
    char* val;
};

struct xIniSection
{
    char* sec;
    S32 first;
    S32 count;
};

struct xIniFile
{
    S32 NumValues;
    S32 NumSections;
    xIniValue* Values;
    xIniSection* Sections;
    void* mem;
    char name[256];
    char pathname[256];
};

xIniFile* xIniParse(char* buf, S32 len);
void xIniDestroy(xIniFile* ini);

inline S32 xIniGetIndex(xIniFile* ini, const char* tok)
{
    for (S32 i = 0; i < ini->NumValues; i++)
    {
        if (xStricmp(ini->Values[i].tok, tok) == 0)
        {
            return i;
        }
    }

    return -1;
}

inline S32 xIniGetIndex(xIniFile* ini, const char* tok, S32 idx)
{
    for (S32 i = idx; i < ini->NumValues; i++)
    {
        if (xStricmp(ini->Values[i].tok, tok) == 0)
        {
            return i;
        }
    }

    return -1;
}

S32 xIniGetInt(xIniFile* ini, const char* tok, S32 def);
F32 xIniGetFloat(xIniFile* ini, const char* tok, F32 def);
char* xIniGetString(xIniFile* ini, const char* tok, const char* def, S32& idx);
const char* xIniGetString(xIniFile* ini, const char* tok, const char* def);

#endif
