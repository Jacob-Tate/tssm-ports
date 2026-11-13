#include "zPickupTable.h"

#include "zGlobals.h"
#include "xString.h"

struct PickupTranslEntry
{
    char* tname;
    U32 hval;
    U8 type;
    U8 subtype;
    U16 flags;
};

// clang-format off
static PickupTranslEntry ptranstbl[] =
{
    { "Nitro", 0, 0, 0, 0 },
    { "ManlinessRed", 0, 1, 0, 0 },
    { "ManlinessYellow", 0, 1, 1, 0 },
    { "ManlinessGreen", 0, 1, 2, 0 },
    { "ManlinessBlue", 0, 1, 3, 0 },
    { "ManlinessPurple", 0, 1, 4, 0 },
    { "GoofyGooberToken", 0, 0, 2, 0 },
    { "KrabbyPatty", 0, 0, 3, 0 },
    { "TreasureChest", 0, 0, 4, 0 }
};
// clang-format on

static U32 ptranstbl_size = sizeof(ptranstbl) / sizeof(ptranstbl[0]);

void zPickupTableInit()
{
    zAssetPickup* ptbl = (zAssetPickup*)(globals.pickupTable + 1);
    U32 i, j;

    for (i = 0; i < ptranstbl_size; i++)
    {
        ptranstbl[i].hval = xStrHash(ptranstbl[i].tname);

        for (j = 0; j < globals.pickupTable->Count; j++)
        {
            if (ptbl[j].pickupHash == ptranstbl[i].hval)
            {
                break;
            }
        }

        if (j != globals.pickupTable->Count)
        {
            ptbl[j].pickupType = ptranstbl[i].type;
            ptbl[j].pickupIndex = ptranstbl[i].subtype;
        }
    }
}
