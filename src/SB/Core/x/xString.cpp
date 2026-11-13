#include "xString.h"

#define XSTR_UPPER(c) ((c) >= 'a' && (c) <= 'z' ? (c) - 32 : (c))

U32 xStrHash(const char* str)
{
    U32 i = 0;

    for (; *str != '\0'; ++str)
    {
        char c = *str;
        c = c - (c & (c >> 1) & 0x20);
        i = i * 131 + c;
    }

    return i;
}

U32 xStrHash(const char* s, size_t size)
{
    U32 value = 0;

    for (size_t i = 0; i < size && *s != '\0'; ++i, ++s)
    {
        char c = *s;
        c = c - (c & (c >> 1) & 0x20);
        value = value * 131 + c;
    }

    return value;
}

U32 xStrHashCat(U32 prefix, const char* str)
{
    U32 i = prefix;

    for (; *str != '\0'; ++str)
    {
        char c = *str;
        c = c - (c & (c >> 1) & 0x20);
        i = i * 131 + c;
    }

    return i;
}

char* xStrTok(char* string, const char* control, char** nextoken)
{
    U8* str;
    const U8* ctrl;
    U8 map[32];
    S32 count;
    // not in the DWARF: the current character is kept in a register.
    U8 c;

    for (count = 0; count < 32; count++)
    {
        map[count] = 0;
    }

    ctrl = (const U8*)control;

    do
    {
        map[*ctrl >> 3] |= (U8)(1 << (*ctrl & 0x7));
    } while (*ctrl++ != '\0');

    str = (string) ? (U8*)string : (U8*)*nextoken;

    while (map[(*str >> 3) & 0x1F] & (1 << (*str & 0x7)) && *str != '\0')
    {
        str++;
    }

    string = (char*)str;

    for (; (c = *str) != '\0'; str++)
    {
        if (map[(c >> 3) & 0x1F] & (1 << (c & 0x7)))
        {
            *str++ = '\0';
            break;
        }
    }

    *nextoken = (char*)str;

    if (string == (char*)str)
    {
        string = NULL;
    }

    return string;
}

U32 xStrTokCount(const char* string, const char* control, U32* maxsize)
{
    U32 num = 0;
    U32 max = 0;
    U32 test;
    const U8* str;
    const U8* ctrl;
    U8 map[32];
    S32 count;
    // not in the DWARF: the current character is kept in a register.
    U8 c;

    for (count = 0; count < 32; count++)
    {
        map[count] = 0;
    }

    ctrl = (const U8*)control;

    do
    {
        map[*ctrl >> 3] |= (U8)(1 << (*ctrl & 0x7));
    } while (*ctrl++ != '\0');

    str = (const U8*)string;

    while (true)
    {
        while (map[(*str >> 3) & 0x1F] & (1 << (*str & 0x7)) && *str != '\0')
        {
            str++;
        }

        string = (char*)str;
        test = 0;

        while ((c = *str) != '\0')
        {
            if (map[(c >> 3) & 0x1F] & (1 << (c & 0x7)))
            {
                str++;
                break;
            }

            test++;
            str++;
        }

        if (string == (char*)str)
        {
            if (maxsize)
            {
                *maxsize = max;
            }

            return num;
        }

        if (test > max)
        {
            max = test;
        }

        num++;
    }
}

char* xStrTokBuffer(const char* string, const char* control, void* buffer)
{
    char** nextoken = (char**)buffer;
    char* dest = (char*)(nextoken + 1);
    // not in the DWARF: the current character is kept in a register.
    U8 c;
    const U8* str;
    const U8* ctrl;
    U8 map[32];
    S32 count;

    for (count = 0; count < 32; count++)
    {
        map[count] = 0;
    }

    ctrl = (const U8*)control;

    do
    {
        map[*ctrl >> 3] |= (U8)(1 << (*ctrl & 0x7));
    } while (*ctrl++ != '\0');

    str = (string) ? (const U8*)string : (const U8*)*nextoken;

    while (map[(*str >> 3) & 0x1F] & (1 << (*str & 0x7)) && *str != '\0')
    {
        str++;
    }

    string = (char*)str;

    while ((c = *str) != '\0')
    {
        if (map[(c >> 3) & 0x1F] & (1 << (c & 0x7)))
        {
            str++;
            break;
        }

        *dest++ = c;
        str++;
    }

    *dest = '\0';
    *nextoken = (char*)str;

    if (string == (char*)str)
    {
        return NULL;
    }

    return (char*)buffer + sizeof(char*);
}

S32 xStricmp(const char* string1, const char* string2)
{
    S32 result = 0;

    while (XSTR_UPPER(*string1) == XSTR_UPPER(*string2) && !result)
    {
        if (*string1 == '\0' || *string2 == '\0')
        {
            result = 1;
        }
        else
        {
            string1++;
            string2++;
        }
    }

    result = 0;
    if (*string1 != *string2)
    {
        result = 1;
        if (XSTR_UPPER(*string1) < XSTR_UPPER(*string2))
        {
            result = -1;
        }
    }

    return result;
}

S32 xMemicmp(const void* string1, const void* string2, size_t chars)
{
    const char* str1 = (const char*)string1;
    const char* str2 = (const char*)string2;
    S32 ret;

    while (chars != 0)
    {
        ret = XSTR_UPPER(*str2) - XSTR_UPPER(*str1);
        if (ret != 0)
        {
            return ret;
        }

        str1++;
        str2++;
        chars--;
    }

    return 0;
}

char* xStrupr(char* string)
{
    char* p = string;

    while (*p != '\0')
    {
        *p = (*p >= 'a' && *p <= 'z' ? *p - 32 : *p);

        p++;
    }

    return string;
}

S32 xStrParseFloatList(F32* dest, const char* strbuf, S32 max)
{
    char* str = (char*)strbuf;
    S32 index;
    S32 digits;
    S32 negate;
    char* numstart;
    char savech;

    if (strbuf == NULL)
    {
        return 0;
    }

    for (index = 0; *str != '\0' && index < max; index++)
    {
        while (*str == '\t' || *str == ' ' || *str == '[' || *str == ']' || *str == '{' ||
               *str == '}' || *str == '(' || *str == ')' || *str == '+' || *str == ',' ||
               *str == ':' || *str == ';')
        {
            str++;
        }

        if (*str == '\0')
        {
            return index;
        }

        if (*str == '-')
        {
            negate = TRUE;
            str++;

            while (*str == '\t' || *str == ' ')
            {
                str++;
            }
        }
        else
        {
            negate = FALSE;
        }

        numstart = str;
        digits = 0;

        while ((*str >= '0' && *str <= '9') || *str == '.' || *str == 'E' || *str == 'e' ||
               *str == 'f')
        {
            if (*str >= '0' && *str <= '9')
            {
                digits++;
            }

            str++;
        }

        if (digits == 0)
        {
            return index;
        }

        savech = *str;

        *str = '\0';
        dest[index] = xatof(numstart);

        if (negate)
        {
            dest[index] = -dest[index];
        }

        *str = savech;
    }

    return index;
}

// Defined ahead of icompare so that it can be inlined there, as in the target.
S32 imemcmp(const void* d1, const void* d2, size_t size)
{
    char* s1 = (char*)d1;
    char* s2 = (char*)d2;
    U32 i = 0;
    while (i < size)
    {
        S32 c1 = *s1 | ((*s1 >> 1) & 0x20);
        S32 c2 = *s2 | ((*s2 >> 1) & 0x20);
        if (c1 != c2)
        {
            return c1 - c2;
        }
        s1++;
        s2++;
        i++;
    }
    return 0;
}

S32 icompare(const substr& s1, const substr& s2)
{
    U32 len = MIN(s1.size, s2.size);
    S32 result = imemcmp(s1.text, s2.text, len);

    if (result != 0)
    {
        return result;
    }

    if (s1.size == s2.size)
    {
        return 0;
    }

    return (s1.size < s2.size) ? -1 : 1;
}

size_t atox(const substr& s, size_t& read_size)
{
    const char* p = s.text;
    size_t size = s.size;
    size_t total;
    size_t v;

    if (p == NULL)
    {
        return 0;
    }

    total = 0;

    if (size > 8)
    {
        size = 8;
    }

    for (read_size = 0; read_size < size; read_size++)
    {
        if (*p >= '0' && *p <= '9')
        {
            v = *p - '0';
        }
        else if (*p >= 'a' && *p <= 'f')
        {
            v = *p - 'a' + 10;
        }
        else if (*p >= 'A' && *p <= 'F')
        {
            v = *p - 'A' + 10;
        }
        else
        {
            break;
        }

        total = (total << 4) + v;
        p++;
    }

    return total;
}

const char* find_char(const substr& s, const substr& cs)
{
    if (!s.text || !cs.text)
    {
        return NULL;
    }

    const char* p = s.text;
    const char* d = cs.text;

    // The unrolled cases index cs.text directly; only the generic default case walks d
    // (retail's code for the cases matches cs.text[k], not d[k]).
    switch (cs.size)
    {
#define FIND_CHAR_CASE(n, test)                                                                    \
    case n:                                                                                        \
        for (S32 i = s.size; i > 0 && *p != '\0'; i--, p++)                                        \
        {                                                                                          \
            if (test)                                                                              \
            {                                                                                      \
                return p;                                                                          \
            }                                                                                      \
        }                                                                                          \
        break;

    case 0:
        break;
        FIND_CHAR_CASE(1, *p == cs.text[0])
        FIND_CHAR_CASE(2, *p == cs.text[0] || *p == cs.text[1])
        FIND_CHAR_CASE(3, *p == cs.text[0] || *p == cs.text[1] || *p == cs.text[2])
        FIND_CHAR_CASE(4,
                       *p == cs.text[0] || *p == cs.text[1] || *p == cs.text[2] || *p == cs.text[3])
        FIND_CHAR_CASE(5, *p == cs.text[0] || *p == cs.text[1] || *p == cs.text[2] ||
                              *p == cs.text[3] || *p == cs.text[4])
        FIND_CHAR_CASE(6, *p == cs.text[0] || *p == cs.text[1] || *p == cs.text[2] ||
                              *p == cs.text[3] || *p == cs.text[4] || *p == cs.text[5])
        FIND_CHAR_CASE(7, *p == cs.text[0] || *p == cs.text[1] || *p == cs.text[2] ||
                              *p == cs.text[3] || *p == cs.text[4] || *p == cs.text[5] ||
                              *p == cs.text[6])
        FIND_CHAR_CASE(8, *p == cs.text[0] || *p == cs.text[1] || *p == cs.text[2] ||
                              *p == cs.text[3] || *p == cs.text[4] || *p == cs.text[5] ||
                              *p == cs.text[6] || *p == cs.text[7])
        FIND_CHAR_CASE(9, *p == cs.text[0] || *p == cs.text[1] || *p == cs.text[2] ||
                              *p == cs.text[3] || *p == cs.text[4] || *p == cs.text[5] ||
                              *p == cs.text[6] || *p == cs.text[7] || *p == cs.text[8])
        FIND_CHAR_CASE(10, *p == cs.text[0] || *p == cs.text[1] || *p == cs.text[2] ||
                               *p == cs.text[3] || *p == cs.text[4] || *p == cs.text[5] ||
                               *p == cs.text[6] || *p == cs.text[7] || *p == cs.text[8] ||
                               *p == cs.text[9])
        FIND_CHAR_CASE(11, *p == cs.text[0] || *p == cs.text[1] || *p == cs.text[2] ||
                               *p == cs.text[3] || *p == cs.text[4] || *p == cs.text[5] ||
                               *p == cs.text[6] || *p == cs.text[7] || *p == cs.text[8] ||
                               *p == cs.text[9] || *p == cs.text[10])

#undef FIND_CHAR_CASE

    default:
        for (S32 i = s.size; i > 0 && *p != '\0'; i--, p++)
        {
            for (const char* s = d; *s != '\0'; s++)
            {
                if (*p == *s)
                {
                    return p;
                }
            }
        }
        break;
    }

    return NULL;
}

#undef XSTR_UPPER
