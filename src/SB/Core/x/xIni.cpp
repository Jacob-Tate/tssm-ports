#include "xIni.h"
#include "xString.h"

#include <PowerPC_EABI_Support/MSL_C/MSL_Common/stdlib.h>
#include <PowerPC_EABI_Support/MSL_C/MSL_Common/cstring>
#include <string.h>

inline char* TrimWhitespace(char* string)
{
    // Skip through any leading whitespace
    for (; *string == ' ' || *string == '\t'; string++)
    {
    }
    // Return early if there's no other characters to go through
    if (*string == '\0')
    {
        return string;
    }

    // Find first non-whitespace character at the end of the string
    char* substring = &string[strlen(string) - 1];
    for (; *substring == ' ' || *substring == '\t'; substring--)
    {
    }

    // Set the null-terminator accordingly
    substring[1] = '\0';

    return string;
}

xIniFile* xIniParse(char* buf, S32 len)
{
    S32 i;
    S32 ccr = 1;
    S32 clf = 1;
    S32 copen = 0;
    S32 lastCRLF = -1;
    char* c;
    char* lastLine;
    char* line;
    char* ltoken;
    xIniFile* ini;

    for (i = 0; i < len; i++)
    {
        switch (buf[i])
        {
        case '\n':
            lastCRLF = i;
            clf++;
            break;
        case '\r':
            lastCRLF = i;
            ccr++;
            break;
        case '[':
            copen++;
            break;
        }
    }

    if (clf > ccr)
    {
        ccr = clf;
    }

    S32 sectionAlloc = copen;
    S32 valueAlloc = ccr;

    ini = (xIniFile*)RwMalloc(sizeof(xIniFile) + (valueAlloc * sizeof(xIniValue)) +
                                  (sectionAlloc * sizeof(xIniSection)) + (len - lastCRLF),
                              0);
    ini->mem = NULL;
    ini->NumValues = 0;
    ini->NumSections = 0;
    ini->Values = (xIniValue*)(ini + 1);
    ini->Sections = (xIniSection*)(ini->Values + valueAlloc);

    lastLine = (char*)(ini->Sections + sectionAlloc);
    strncpy(lastLine, buf + (lastCRLF + 1), len - (lastCRLF + 1));
    lastLine[len - (lastCRLF + 1)] = '\0';

    if (lastCRLF >= 0)
    {
        buf[lastCRLF] = '\0';
    }
    else
    {
        buf[0] = '\0';
    }

    line = xStrTok(buf, "\n\r", &ltoken);
    if (line == NULL)
    {
        line = xStrTok(lastLine, "\n\r", &ltoken);
        lastLine = NULL;
    }

    while (line != NULL)
    {
        line = TrimWhitespace(line);
        if (*line != '#' && *line != '\0')
        {
            if (*line == '[')
            {
                c = std::strstr(line, "]");
                if (c != NULL)
                {
                    *c = '\0';
                    c = TrimWhitespace(line + 1);
                    if (*c != '\0')
                    {
                        ini->Sections[ini->NumSections].sec = c;
                        ini->Sections[ini->NumSections].first = ini->NumValues;
                        ini->Sections[ini->NumSections].count = 0;
                        ini->NumSections++;
                    }
                }
            }
            else
            {
                c = std::strstr(line, "=");
                if (c != NULL)
                {
                    *c = '\0';
                    char* tok = TrimWhitespace(line);
                    if (*tok != '\0')
                    {
                        line = c + 1;
                        c = std::strstr(line, "#");
                        if (c != NULL)
                        {
                            *c = '\0';
                        }
                        char* val = TrimWhitespace(line);
                        ini->Values[ini->NumValues].tok = tok;
                        ini->Values[ini->NumValues].val = val;
                        ini->NumValues++;
                        if (ini->NumSections != 0)
                        {
                            ini->Sections[ini->NumSections - 1].count++;
                        }
                    }
                }
            }
        }

        line = xStrTok(NULL, "\n\r", &ltoken);
        if (line == NULL && lastLine != NULL)
        {
            line = xStrTok(lastLine, "\n\r", &ltoken);
            lastLine = NULL;
        }
    }

    return ini;
}

void xIniDestroy(xIniFile* ini)
{
    RwFree(ini->mem);
    RwFree(ini);
}

S32 xIniGetInt(xIniFile* ini, const char* tok, S32 def)
{
    S32 index = xIniGetIndex(ini, tok);
    if (index == -1)
    {
        return def;
    }
    return atoi(ini->Values[index].val);
}

F32 xIniGetFloat(xIniFile* ini, const char* tok, F32 def)
{
    S32 index = xIniGetIndex(ini, tok);
    if (index == -1)
    {
        return def;
    }
    return atof(ini->Values[index].val);
}

const char* xIniGetString(xIniFile* ini, const char* tok, const char* def)
{
    S32 index = xIniGetIndex(ini, tok);
    if (index == -1)
    {
        return def;
    }
    return ini->Values[index].val;
}

char* xIniGetString(xIniFile* ini, const char* tok, const char* def, S32& idx)
{
    idx = xIniGetIndex(ini, tok, idx);
    if (idx == -1)
    {
        return (char*)def;
    }
    return ini->Values[idx++].val;
}
