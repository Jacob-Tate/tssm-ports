#include "xutil.h"

#include <stdio.h>
#include <stdlib.h>

static S32 g_xutilinit;
static S32 g_crc_needinit = 1;
static U32 g_crc32_table[256] = {};

// Shift-JIS tables of strtosjis; retail strips its code but keeps the tables in .data.
static U16 ascii_table[3][2] = { { 0x824F, 0x0030 }, { 0x8260, 0x0041 }, { 0x8281, 0x0061 } };
static U16 ascii_k_table[33] = { 0x8140, 0x8149, 0x8168, 0x8194, 0x8190, 0x8193, 0x8195,
                                 0x8166, 0x8169, 0x816A, 0x8196, 0x817B, 0x8143, 0x817C,
                                 0x8144, 0x815E, 0x8146, 0x8147, 0x8171, 0x8181, 0x8172,
                                 0x8148, 0x8197, 0x816D, 0x818F, 0x816E, 0x814F, 0x8151,
                                 0x8165, 0x816F, 0x8162, 0x8170, 0x8150 };
static U16 ascii_special_table[33] = { 0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026,
                                       0x0027, 0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D,
                                       0x002E, 0x002F, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E,
                                       0x003F, 0x0040, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
                                       0x0060, 0x007B, 0x007C, 0x007D, 0x007E };

namespace
{
    struct sort_float_functor
    {
        size_t key_offset;

        sort_float_functor(size_t key_offset) : key_offset(key_offset)
        {
        }

        S32 operator()(void* e1, void* e2) const
        {
            F32 diff = *(F32*)((U8*)e1 + key_offset) - *(F32*)((U8*)e2 + key_offset);

            // the sign of the difference, read from its bit pattern
            return *(S32*)&diff;
        }
    };

    struct sort_int_functor
    {
        size_t key_offset;

        sort_int_functor(size_t key_offset) : key_offset(key_offset)
        {
        }

        S32 operator()(void* e1, void* e2) const
        {
            return *(S32*)((U8*)e1 + key_offset) - *(S32*)((U8*)e2 + key_offset);
        }
    };
} // namespace

S32 xUtilStartup()
{
    if (!g_xutilinit++)
    {
        xUtil_crc_init();
    }

    return g_xutilinit;
}

S32 xUtilShutdown()
{
    return --g_xutilinit;
}

char* xUtil_idtag2string(U32 srctag, S32 bufidx)
{
    U32 tag = srctag;
    char* strptr;
    char* uc = (char*)&tag;
    S32 l;
    char t;
    static char buf[6][10] = {};

    if (bufidx < 0 || bufidx >= 7)
    {
        strptr = buf[0];
    }
    else
    {
        strptr = buf[bufidx];
    }

    // convert tag to big endian

    l = 1;

    if ((S32)((char*)&l)[3] != 0)
    {
        t = uc[0];
        uc[0] = uc[3];
        uc[3] = t;

        t = uc[1];
        uc[1] = uc[2];
        uc[2] = t;
    }

    switch (bufidx)
    {
    case 4:
    case 5:
        strptr[0] = isprint(uc[0]) ? uc[0] : '?';
        strptr[1] = isprint(uc[1]) ? uc[1] : '?';
        strptr[2] = isprint(uc[2]) ? uc[2] : '?';
        strptr[3] = isprint(uc[3]) ? uc[3] : '?';
        break;
    case 6:
    default:
        strptr[0] = isprint(uc[3]) ? uc[3] : '?';
        strptr[1] = isprint(uc[2]) ? uc[2] : '?';
        strptr[2] = isprint(uc[1]) ? uc[1] : '?';
        strptr[3] = isprint(uc[0]) ? uc[0] : '?';
        break;
    }

    strptr[4] = '\0';

    if (bufidx == 6)
    {
        strptr[4] = '/';
        strptr[5] = isprint(uc[0]) ? uc[0] : '?';
        strptr[6] = isprint(uc[1]) ? uc[1] : '?';
        strptr[7] = isprint(uc[2]) ? uc[2] : '?';
        strptr[8] = isprint(uc[3]) ? uc[3] : '?';
        strptr[9] = '\0';
    }

    return strptr;
}

U32 xUtil_crc_init()
{
    S32 i, j;
    U32 crc_accum;

    if (g_crc_needinit)
    {
        for (i = 0; i < 256; i++)
        {
            crc_accum = (U32)i << 24;

            for (j = 0; j < 8; j++)
            {
                if (crc_accum & (1 << 31))
                {
                    crc_accum = (crc_accum << 1) ^ 0x04C11DB7;
                }
                else
                {
                    crc_accum = (crc_accum << 1);
                }
            }

            g_crc32_table[i] = crc_accum;
        }

        g_crc_needinit = 0;
    }

    return 0xFFFFFFFF;
}

U32 xUtil_crc_update(U32 crc_accum, char* data, S32 datasize)
{
    S32 i, j;

    if (g_crc_needinit)
    {
        xUtil_crc_init();
    }

    for (i = 0; i < datasize; i++)
    {
        j = ((crc_accum >> 24) ^ *data++) & 0xff;
        crc_accum = (crc_accum << 8) ^ g_crc32_table[j];
    }

    return crc_accum;
}

// Dead-stripped in retail (its tables and strings survive); body from the TSSM PS2 demo, where it
// is live.
void strtosjis(U8* string, U8* dest)
{
    S32 i;
    S32 sjis_code;
    S32 ascii_code;
    U8 stmp = '0';
    U8 stmp2 = '0';
    U8* dest2 = dest;

    for (i = 0; i < 32; i++)
    {
        dest2[0] = 0;
        dest2[1] = 0;
        dest2 += 2;
    }

    while (*string)
    {
        ascii_code = *string++;
        stmp2 = 0;

        if (ascii_code >= ' ' && ascii_code <= '/')
        {
            stmp2 = 1;
        }
        else if (ascii_code >= '0' && ascii_code <= '9')
        {
            stmp = 0;
        }
        else if (ascii_code >= ':' && ascii_code <= '@')
        {
            stmp2 = 11;
        }
        else if (ascii_code >= 'A' && ascii_code <= 'Z')
        {
            stmp = 1;
        }
        else if (ascii_code >= '[' && ascii_code <= '`')
        {
            stmp2 = 37;
        }
        else if (ascii_code >= 'a' && ascii_code <= 'z')
        {
            stmp = 2;
        }
        else if (ascii_code >= '{' && ascii_code <= '~')
        {
            stmp2 = 63;
        }
        else
        {
            printf("bad ASCII code 0x%x\n", ascii_code);
            exit(1);
        }

        if (stmp2)
        {
            sjis_code = ascii_k_table[(ascii_code - ' ') - (stmp2 - 1)];
        }
        else
        {
            sjis_code = ascii_code + ascii_table[stmp][0] - ascii_table[stmp][1];
        }

        dest[0] = sjis_code >> 8;
        dest[1] = sjis_code;
        dest += 2;
    }
}

// Placeholder for dead-stripped code (probably sjistostr, the reverse conversion) whose two
// messages remain, unreferenced, in retail's string pool.
static const char* __deadstripped_xutil()
{
    return "Unknown SJIS code, possibly NULL %x,%x,%x\n\0"
           "BAD CODE\n";
}

// Dead-stripped in retail (its "%x" survives); body from the TSSM PS2 demo, where it is live.
U8 BCDtoi(U8 hex)
{
    char c[16];

    sprintf(c, "%x", hex);
    return atoi(c);
}

S32 xUtil_yesno(F32 wt_yes)
{
    if (0.0f == wt_yes)
    {
        return 0;
    }

    if (1.0f == wt_yes)
    {
        return 1;
    }

    return (xurand() <= wt_yes);
}

void quicksort_int(void* base, size_t num, size_t width, size_t key_offset)
{
    quicksort(base, num, width, sort_int_functor(key_offset));
}

void quicksort_float(void* base, size_t num, size_t width, size_t key_offset)
{
    quicksort(base, num, width, sort_float_functor(key_offset));
}

void isort_float(void* base, size_t num, size_t width, size_t key_offset)
{
    isort(base, num, width, sort_float_functor(key_offset));
}
