#ifndef XSTRING_H
#define XSTRING_H

#include <types.h>
#include <stddef.h>

struct substr
{
    const char* text;
    size_t size;

    static substr create(const char* text, size_t size)
    {
        substr s = { text, size };
        return s;
    }
};

// substr constructor
#define SUBSTR(str) (str), (sizeof((str)) - 1)

U32 xStrHash(const char* str);
U32 xStrHash(const char* str, size_t size);
U32 xStrHashCat(U32 prefix, const char* str);
char* xStrupr(char* string);
S32 xStricmp(const char* string1, const char* string2);
char* xStrTok(char* string, const char* control, char** nextoken);
U32 xStrTokCount(const char* string, const char* control, U32* maxsize);
char* xStrTokBuffer(const char* string, const char* control, void* buffer);
S32 xStrParseFloatList(F32* dest, const char* strbuf, S32 max);

S32 imemcmp(void const* d1, void const* d2, size_t size);
S32 xMemicmp(const void* string1, const void* string2, size_t chars);
S32 icompare(const substr& s1, const substr& s2);

const char* find_char(const substr& s, const substr& cs);
size_t atox(const substr& s, size_t& read_size);

// The helpers below have no out-of-line copies in TSSM: they are header inlines.

inline bool is_ws(char c)
{
    return (c == ' ' || c == '\t' || c == '\n');
}

// cpp_extensions off while these two are parsed: the const char -> char conversion of is_ws's
// argument then re-extends the byte (clrlwi), as retail does in parse_split_tag and read_tag.
#pragma cpp_extensions off
inline size_t rskip_ws(const char*& text, size_t& size)
{
    while (size && is_ws(text[size - 1]))
    {
        size--;
    }

    return size;
}
#pragma cpp_extensions reset

inline size_t rskip_ws(substr& s)
{
    return rskip_ws(s.text, s.size);
}

inline const char* find_char(const substr& s, char c)
{
    if (!s.text)
    {
        return NULL;
    }

    const char* text = s.text;
    S32 size = s.size;

    while (size > 0 && *text != '\0')
    {
        if (*text == c)
        {
            return text;
        }

        size--;
        text++;
    }

    return NULL;
}

// cpp_extensions off for the same reason as rskip_ws. The char* return type (hence the cast) is
// Ratatouille's.
#pragma cpp_extensions off
inline char* skip_ws(const char*& text, size_t& size)
{
    size_t i = 0;

    for (; i < size && *text != '\0'; ++i, ++text)
    {
        if (!is_ws(*text))
        {
            size -= i;
            break;
        }
    }

    return (char*)text;
}
#pragma cpp_extensions reset

inline char* skip_ws(substr& s)
{
    return skip_ws(s.text, s.size);
}

inline size_t atox(const substr& s)
{
    size_t read_size;
    return atox(s, read_size);
}

inline void trim_ws(const char*& text, size_t& size)
{
    skip_ws(text, size);
    rskip_ws(text, size);
}

inline void trim_ws(substr& s)
{
    trim_ws(s.text, s.size);
}

extern "C" {
U32 tolower__21_esc__2_unnamed_esc__2_xString_cpp_esc__2_Fc(U32 param_1);
U32 tolower__21_esc__2_unnamed_esc__2_xString_cpp_esc__2_Fi(U32 param_1);
}

#endif
