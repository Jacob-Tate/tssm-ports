#include "xFactory.h"
#include "xDebug.h"

#include <string.h>

static S32 OrdComp_infotype(void* vkey, void* vitem);

static S32 OrdTest_infotype(const void* vkey, void* vitem);

xFactory::xFactory(S32 maxTypes)
{
    U32 amt = maxTypes * sizeof(XGOFTypeInfo);
    this->infopool = (XGOFTypeInfo*)xMemAllocSize(amt);
    memset(this->infopool, 0, amt);
    XOrdInit(&this->infolist, maxTypes, 0);
}

xFactory::~xFactory()
{
    this->infopool = NULL;
    XOrdDone(&this->infolist, 0);
}

// The asserts are compiled-out placeholders (conditions unknown) at code-less lines of the PS2
// line table; six of them keep this from being auto-inlined, as retail calls it.
S32 xFactory::RegItemType(XGOFTypeInfo* info)
{
    S32 rc = 0;
    XGOFTypeInfo* tptr = info;
    XGOFTypeInfo* nextrec;

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    while (tptr->tid)
    {
        rc = 1;

        xASSERT_PLACEHOLDER();
        if (!tptr->creator)
        {
            rc = 0;
            break;
        }

        xASSERT_PLACEHOLDER();
        if (!tptr->destroyer)
        {
            rc = 0;
            break;
        }

        xASSERT_PLACEHOLDER();
        if (infolist.cnt >= infolist.max)
        {
            rc = -2;
            break;
        }

        S32 idx = XOrdLookup(&infolist, tptr, OrdTest_infotype);

        xASSERT_PLACEHOLDER();
        if (idx >= 0)
        {
            rc = -1;
            break;
        }

        nextrec = &infopool[infolist.cnt];
        nextrec->tid = tptr->tid;
        nextrec->creator = tptr->creator;
        nextrec->destroyer = tptr->destroyer;

        XOrdInsert(&infolist, nextrec, OrdComp_infotype);

        tptr++;
    }

    return rc;
}

S32 xFactory::RegItemType(S32 tid, XGOFTypeInfoCreator create, XGOFTypeInfoDestroyer destroy)
{
    XGOFTypeInfo typerec[2] = {};

    typerec[0].tid = tid;
    typerec[0].creator = create;
    typerec[0].destroyer = destroy;

    return RegItemType(typerec);
}

void xFactory::GrowDataEnable(xBase* user, S32 isResume)
{
    if (isResume)
    {
        growContextData.Resume(user);
    }
    else
    {
        growContextData.Init(user);
    }
}

void xFactory::GrowDataDisable()
{
    growContextData.Done();
}

xFactoryInst* xFactory::CreateItem(S32 typeID, void* userdata, RyzMemGrow* callerzgrow)
{
    S32 idx;
    xFactoryInst* item;
    XGOFTypeInfo pattern = {};
    XGOFTypeInfo* darec = NULL;
    RyzMemGrow* grow = callerzgrow;

    pattern.tid = typeID;

    idx = XOrdLookup(&infolist, &pattern, OrdTest_infotype);

    if (idx >= 0)
    {
        darec = (XGOFTypeInfo*)infolist.list[idx];
    }

    if (!darec)
    {
        return NULL;
    }

    if (!grow && growContextData.IsEnabled())
    {
        grow = &growContextData;
    }

    item = darec->creator(darec->tid, grow, userdata);

    if (!item)
    {
        return item;
    }

    item->itemType = darec->tid;
    item->prevprod = NULL;
    item->nextprod = NULL;

    if (products)
    {
        item->nextprod = products;
        products->prevprod = item;
        products = item;
    }
    else
    {
        products = item;
    }

    return item;
}

void xFactory::DestroyAll()
{
    while (products)
    {
        DestroyItem(products);
    }
}

void xFactory::DestroyItem(xFactoryInst* item)
{
    S32 idx;
    XGOFTypeInfo pattern = {};

    pattern.tid = item->itemType;

    if (item)
    {
        if (products == item)
        {
            products = item->nextprod;

            if (products)
            {
                products->prevprod = NULL;
            }
        }

        if (item->prevprod)
        {
            item->prevprod->nextprod = item->nextprod;
        }

        if (item->nextprod)
        {
            item->nextprod->prevprod = item->prevprod;
        }

        item->prevprod = NULL;
        item->nextprod = NULL;

        idx = XOrdLookup(&infolist, &pattern, OrdTest_infotype);

        ((XGOFTypeInfo*)infolist.list[idx])->destroyer(item);
    }
}

static S32 OrdTest_infotype(const void* vkey, void* vitem)
{
    S32 rc;

    if (((XGOFTypeInfo*)vkey)->tid < ((XGOFTypeInfo*)vitem)->tid)
    {
        rc = -1;
    }
    else if (((XGOFTypeInfo*)vkey)->tid > ((XGOFTypeInfo*)vitem)->tid)
    {
        rc = 1;
    }
    else
    {
        rc = 0;
    }

    return rc;
}

static S32 OrdComp_infotype(void* vkey, void* vitem)
{
    S32 rc;

    if (((XGOFTypeInfo*)vkey)->tid < ((XGOFTypeInfo*)vitem)->tid)
    {
        rc = -1;
    }
    else if (((XGOFTypeInfo*)vkey)->tid > ((XGOFTypeInfo*)vitem)->tid)
    {
        rc = 1;
    }
    else
    {
        rc = 0;
    }

    return rc;
}
