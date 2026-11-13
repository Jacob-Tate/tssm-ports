#ifndef XUTIL_H
#define XUTIL_H

#include <types.h>
#include "xMath.h"
#include "xRand.h"

S32 xUtilStartup();
S32 xUtilShutdown();
char* xUtil_idtag2string(U32 srctag, S32 bufidx);
U32 xUtil_crc_init();
U32 xUtil_crc_update(U32 crc_accum, char* data, S32 datasize);
S32 xUtil_yesno(F32 wt_yes);
void xUtil_wtadjust(F32* wts, S32 cnt, F32 arbref);
void quicksort_float(void* base, size_t num, size_t width, size_t key_offset);
void quicksort_int(void* base, size_t num, size_t width, size_t key_offset);
void isort_float(void* base, size_t num, size_t width, size_t key_offset);

template <typename T> inline T* xUtil_select(T** data, S32 size, const F32* arg2)
{
    T* selected = NULL;
    S32 selectIdx = 0;
    F32 tempValue;

    if (!data)
    {
        return NULL;
    }
    else if (size < 1)
    {
        return NULL;
    }

    F32 randOffset = xurand();
    if (arg2 == NULL)
    {
        selectIdx = (S32)(randOffset * (F32)size);
    }
    else
    {
        F32* roundingData = (F32*)arg2;
        F32 threshold = 0.0f;
        S32 counter = 0;

        for (S32 i = size; i > 0; i--)
        {
            tempValue = threshold;
            threshold += *roundingData;
            if (randOffset >= tempValue && randOffset <= threshold)
            {
                selectIdx = counter;
                break;
            }
            roundingData++;
            counter++;
        }
    }

    if (selectIdx >= size)
    {
        selectIdx = size - 1;
    }

    if (selectIdx < 0)
    {
        selectIdx = 0;
    }

    return data[selectIdx];
}

// No symbol in TSSM (always inlined). Picks an entry of list at random, weighted by wts (evenly
// when wts is NULL).
template <class T> inline T xUtil_choose(const T* list, S32 cnt, const F32* wts)
{
    S32 idx;
    F32 pt;
    F32 hi;
    S32 i;
    F32 lo;

    if (list == NULL)
    {
        return 0;
    }

    if (cnt < 1)
    {
        return 0;
    }

    idx = 0;
    pt = xurand();
    if (wts == NULL)
    {
        idx = pt * cnt;
    }
    else
    {
        hi = 0.0f;
        for (i = 0; i < cnt; i++)
        {
            lo = hi;
            hi += wts[i];
            if (pt >= lo && pt <= hi)
            {
                idx = i;
                break;
            }
        }
    }

    if (idx >= cnt)
    {
        idx = cnt - 1;
    }

    if (idx < 0)
    {
        idx = 0;
    }

    return list[idx];
}

// Swaps two elements of 'size' bytes (a multiple of 4) one word at a time.
#define XUTIL_SWAP(a, b, size)                                                                     \
    {                                                                                              \
        U8* _b0 = (a);                                                                             \
        U8* _b1 = (b);                                                                             \
        U8* _endb0 = (a) + (size);                                                                 \
        while (_b0 != _endb0)                                                                      \
        {                                                                                          \
            U32 _temp = *(U32*)_b0;                                                                \
            *(U32*)_b0 = *(U32*)_b1;                                                               \
            *(U32*)_b1 = _temp;                                                                    \
            _b0 += 4;                                                                              \
            _b1 += 4;                                                                              \
        }                                                                                          \
    }

// Binary search of the sorted range [first, last); cmp(value, element) returns <0, 0 or >0.
// Returns last when no element compares equal.
template <class T, class V, class C> T binary_find(T first, T last, const V& value, C cmp)
{
    T notfound = last;

    while (first < last)
    {
        T mid = first + (last - first) / 2;
        S32 c = cmp(value, *mid);

        if (c < 0)
        {
            last = mid;
        }
        else if (c > 0)
        {
            first = mid + 1;
        }
        else
        {
            return mid;
        }
    }

    return notfound;
}

// Non-recursive quicksort (median of three, insertion sort for small ranges);
// cmp(e1, e2) returns <0, 0 or >0.
template <class T> void quicksort(void* base, size_t num, size_t width, T cmp)
{
    size_t thresh = width * 7;
    U8* stack[40];
    U8** sp = stack;
    U8* pivot = (U8*)base;
    U8* tail = pivot + num * width;

    for (;;)
    {
        if ((size_t)(tail - pivot) <= thresh)
        {
            U8* v = pivot;
            U8* next = pivot + width;

            while (next < tail)
            {
                while (cmp(v, v + width) > 0)
                {
                    XUTIL_SWAP(v, v + width, width);

                    if (v == pivot)
                    {
                        break;
                    }

                    v -= width;
                }

                v = next;
                next += width;
            }

            if (sp == stack)
            {
                break;
            }

            sp -= 2;
            pivot = sp[0];
            tail = sp[1];
        }
        else
        {
            size_t half = (size_t)(tail - pivot) / 2;
            half -= half % width;
            XUTIL_SWAP(pivot + half, pivot, width);

            U8* left = pivot + width;
            U8* right = tail - width;

            if (cmp(left, right) > 0)
            {
                XUTIL_SWAP(left, right, width);
            }

            if (cmp(pivot, right) > 0)
            {
                XUTIL_SWAP(pivot, right, width);
            }
            else if (cmp(left, pivot) > 0)
            {
                XUTIL_SWAP(left, pivot, width);
            }

            for (;;)
            {
                do
                {
                    left += width;
                } while (cmp(left, pivot) < 0);

                do
                {
                    right -= width;
                } while (cmp(right, pivot) > 0);

                if (left > right)
                {
                    break;
                }

                XUTIL_SWAP(left, right, width);
            }

            XUTIL_SWAP(pivot, right, width);

            if (right - pivot > tail - left)
            {
                sp[0] = pivot;
                sp[1] = right;
                pivot = left;
            }
            else
            {
                sp[0] = left;
                sp[1] = tail;
                tail = right;
            }

            sp += 2;
        }
    }
}

// Insertion sort; cmp(e1, e2) returns <0, 0 or >0.
template <class T> void isort(void* base, size_t num, size_t width, T cmp)
{
    U8* v = (U8*)base;
    U8* next = v + width;
    U8* end = v + num * width;

    while (next != end)
    {
        while (cmp(v, v + width) > 0)
        {
            XUTIL_SWAP(v, v + width, width);

            if (v == base)
            {
                break;
            }

            v -= width;
        }

        v = next;
        next += width;
    }
}

#endif
