#include "xordarray.h"
#include "xMemMgr.h"
#include "xMath.h"

void XOrdInit(st_XORDEREDARRAY* array, S32 size, S32 tempAlloc)
{
    size = (size < 1) ? 1 : size;
    if (tempAlloc)
    {
        array->list = (void**)xMemPushTemp(size * sizeof(void*));
    }
    else
    {
        array->list = (void**)xMemAlloc(gActiveHeap, size * sizeof(void*), 0);
    }
    array->cnt = 0;
    array->max = size;
    array->warnlvl = 0.95f * size;
    if (array->warnlvl == array->max)
    {
        array->warnlvl = MAX(0, array->max - 1);
    }
}

void XOrdReset(st_XORDEREDARRAY* array)
{
    array->cnt = 0;
}

void XOrdDone(st_XORDEREDARRAY* array, S32 wasTempAlloc)
{
    if (array->max != 0 && wasTempAlloc != 0)
    {
        xMemPopTemp(array->list);
    }
    array->list = NULL;
    array->cnt = 0;
    array->max = 0;
    array->warnlvl = 0;
}

void XOrdAppend(st_XORDEREDARRAY* array, void* elt)
{
    if (array->cnt >= array->max)
    {
        return;
    }
    array->list[array->cnt++] = elt;
}

void XOrdInsert(st_XORDEREDARRAY* array, void* elt, XOrdCompareCallback compare)
{
    S32 i = 0;

    if (array->cnt >= array->max)
        return;
    array->cnt++;

    for (i = array->cnt - 1; i > 0; i--)
    {
        if (compare(array->list[i - 1], elt) <= 0)
        {
            array->list[i] = elt;
            return;
        }
        array->list[i] = array->list[i - 1];
    }

    array->list[0] = elt;
}

void* XOrdRemove(st_XORDEREDARRAY* array, void* elt, S32 index)
{
    if (elt == NULL)
    {
        if (index < 0)
        {
            return NULL;
        }
        if (index >= array->max)
        {
            return NULL;
        }
    }

    if ((index >= 0) && (index < array->max))
    {
        elt = array->list[index];
    }
    else if (elt != NULL)
    {
        index = -1;
        for (S32 i = 0; i < array->cnt; i++)
        {
            if (array->list[i] == elt)
            {
                index = i;
                break;
            }
        }
    }

    if (index < 0)
    {
        return NULL;
    }

    array->cnt--;
    for (; index < array->cnt; index++)
    {
        array->list[index] = array->list[index + 1];
    }

    return elt;
}

void XOrdCopy(st_XORDEREDARRAY* tgt, const st_XORDEREDARRAY* src)
{
    S32 i;

    for (i = 0; i < src->cnt; i++)
    {
        tgt->list[i] = src->list[i];
    }
    tgt->cnt = src->cnt;
}

S32 XOrdFind(st_XORDEREDARRAY* array, const void* key, XOrdTestCallback test)
{
    S32 da_idx = -1;
    S32 i;
    S32 rc;

    for (i = 0; i < array->cnt; i++)
    {
        if (test)
        {
            rc = test(key, array->list[i]);
            if (rc == 0)
            {
                da_idx = i;
                break;
            }
        }
        else if (key == array->list[i])
        {
            da_idx = i;
            break;
        }
    }

    return da_idx;
}

S32 XOrdLookup(st_XORDEREDARRAY* array, const void* key, XOrdTestCallback test)
{
    S32 da_idx = -1;
    S32 k0 = 0;
    S32 k1 = 0;
    S32 k = 0;
    S32 v = 0;

    k0 = 0;
    k1 = array->cnt;
    while (k1 > k0)
    {
        k = (k0 + k1) / 2;
        v = test(key, array->list[k]);
        if (v == 0)
        {
            da_idx = k;
            break;
        }
        if (v > 0)
            k0 = k + 1;
        else
            k1 = k;
    }

    return da_idx;
}

void XOrdSort(st_XORDEREDARRAY* array, S32 (*test)(void*, void*))
{
    void** list = array->list;
    S32 num = array->cnt;
    S32 i = 0;
    S32 j = 0;
    S32 h = 1;
    void* v = NULL;

    while (h <= num)
        h = h * 3 + 1;
    while (h != 1)
    {
        h /= 3;
        for (i = h; i < num; i++)
        {
            v = list[i];
            for (j = i; j >= h && test(v, list[j - h]) < 0; j -= h)
            {
                list[j] = list[j - h];
            }
            list[j] = v;
        }
    }
}
