#include "zFMV.h"
#include <stdio.h>
#include "iFMV.h"
#include "xSnd.h"
#include "zGameState.h"
#include "zGlobals.h"
#include "zMenu.h"
#include "xPad.h"
#include "xTextureManager.h"

// Contents from the target's .data (49 entries; local to this file).
static zFMVFile zFMVFileTable[] = {
    { eFMVFile_NoMovie, "", 0 },
    { eFMVFile_NickLOGO, "FMV\\LogoNicG", 0 },
    { eFMVFile_THQLOGO, "FMV\\LogoTHQ", 0 },
    { eFMVFile_HeavyIronLOGO, "FMV\\LogoHI", 0 },
    { eFMVFile_NickMovieLOGO, "FMV\\LogoNicM", 0 },
    { eFMVFile_AttractMode, "FMV\\SBAttr", 0 },
    { eFMVFile_Pwr_SB_Karate2, "FMV\\ps_krat2", 0 },
    { eFMVFile_Pwr_SB_Bash1, "FMV\\ps_bash1", 0 },
    { eFMVFile_Pwr_SB_Bash2, "FMV\\ps_bash2", 0 },
    { eFMVFile_Pwr_SB_Bowl1, "FMV\\ps_bowl1", 0 },
    { eFMVFile_Pwr_SB_Bowl2, "FMV\\ps_bowl2", 0 },
    { eFMVFile_Pwr_SB_Guitar1, "FMV\\ps_guit1", 0 },
    { eFMVFile_Pwr_SB_Guitar2, "FMV\\ps_guit2", 0 },
    { eFMVFile_Pwr_SB_Health4, "FMV\\ps_hlth4", 0 },
    { eFMVFile_Pwr_SB_Health5, "FMV\\ps_hlth5", 0 },
    { eFMVFile_Pwr_SB_Health6, "FMV\\ps_hlth6", 0 },
    { eFMVFile_Pwr_Pat_Spin2, "FMV\\pp_spin2", 0 },
    { eFMVFile_Pwr_Pat_Cartwheel1, "FMV\\pp_cart1", 0 },
    { eFMVFile_Pwr_Pat_Cartwheel2, "FMV\\pp_cart2", 0 },
    { eFMVFile_Pwr_Pat_Bellyflop1, "FMV\\pp_flop1", 0 },
    { eFMVFile_Pwr_Pat_Bellyflop2, "FMV\\pp_flop2", 0 },
    { eFMVFile_Pwr_Pat_Throw1, "FMV\\pp_thrw1", 0 },
    { eFMVFile_Pwr_Pat_Throw2, "FMV\\pp_thrw2", 0 },
    { eFMVFile_Pwr_Pat_Health4, "FMV\\pp_hlth4", 0 },
    { eFMVFile_Pwr_Pat_Health5, "FMV\\pp_hlth5", 0 },
    { eFMVFile_Pwr_Pat_Health6, "FMV\\pp_hlth6", 0 },
    { eFMVFile_Story1, "FMV\\story1", 0 },
    { eFMVFile_Story2, "FMV\\story2", 1 },
    { eFMVFile_Story3, "FMV\\story3", 1 },
    { eFMVFile_Story4, "FMV\\story4", 1 },
    { eFMVFile_Story5, "FMV\\story5", 1 },
    { eFMVFile_Story6, "FMV\\story6", 1 },
    { eFMVFile_Story7, "FMV\\story7", 1 },
    { eFMVFile_Story8, "FMV\\story8", 1 },
    { eFMVFile_Story9, "FMV\\story9", 1 },
    { eFMVFile_Story10, "FMV\\story10", 1 },
    { eFMVFile_Story11, "FMV\\story11", 1 },
    { eFMVFile_Story12, "FMV\\story12", 0 },
    { eFMVFile_Promo1, "FMV\\promoSB1", 0 },
    { eFMVFile_Promo2, "FMV\\promoAva", 0 },
    { eFMVFile_Promo3, "FMV\\promoFOP", 0 },
    { eFMVFile_Promo4, "FMV\\promoJmy", 0 },
    { eFMVFile_Promo5, "FMV\\promoTak", 0 },
    { eFMVFile_Promo6, "FMV\\promo6", 0 },
    { eFMVFile_Making_Of, "FMV\\MakingOf", 0 },
    { eFMVFile_Trailer1, "FMV\\Trailer1", 0 },
    { eFMVFile_Trailer2, "FMV\\Trailer2", 0 },
    { eFMVFile_Trailer3, "FMV\\Trailer3", 0 },
    { eFMVFile_Pwr_Wagon, "FMV\\pp_wagon", 0 },
};

zFMVFile* zFMVFileGetFile(eFMVFile fmvId)
{
    for (S32 i = 0; i < (S32)(sizeof(zFMVFileTable) / sizeof(zFMVFileTable[0])); i++)
    {
        if (fmvId == zFMVFileTable[i].fmvCode)
        {
            return &zFMVFileTable[i];
        }
    }

    return NULL;
}

char* zFMVFileGetName(eFMVFile fmvId)
{
    zFMVFile* file = zFMVFileGetFile(fmvId);
    if (file == NULL)
    {
        return NULL;
    }

    return file->fileName;
}

U32 zFMVPlay(const char* name, U32 buttons, F32 time, U32 uSubtitlesAID, bool skippable,
             bool lockController)
{
    U32 retval = 0;
    xSndEffect currentEffect;

    if (!globals.noMovies)
    {
        xTextureManager::Get().FreeMemoryBlockingCall();
        zMenuPause(true);
        currentEffect = xSndMgrGetEffect();
        xSndMgrSetEffect(xSndEffect_NONE);
        retval = iFMVPlay(name, buttons, time, uSubtitlesAID, skippable, lockController);
        xSndMgrSetEffect(currentEffect);
        xPadUpdate(0, 0.0001f);
        xPadUpdate(0, 0.0001f);
        zMenuPause(false);
    }

    return retval;
}
