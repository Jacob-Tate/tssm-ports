#include "xRegionSupport.h"
#include "xstransvc.h"

#include <extras.h>

char* apszRegionCode[eRegionCount] = { "US", "BE", "CH", "CZ", "DE", "DK", "ES",
                                       "FI", "FR", "IT", "JP", "KR", "NL", "NO",
                                       "PL", "PT", "RU", "SE", "SK", "TW", "UK" };

U8 xRegionCodeIsValid(const char* pszRegionCode)
{
    U8 bRegionOk = FALSE;

    for (xRegion iRegion = eRegionUS; iRegion < eRegionCount; iRegion = (xRegion)(iRegion + 1))
    {
        if (stricmp(apszRegionCode[iRegion], pszRegionCode) == 0)
        {
            bRegionOk = TRUE;
            break;
        }
    }

    return bRegionOk;
}
