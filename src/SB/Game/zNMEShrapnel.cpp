#include "zNMEShrapnel.h"
#include "zNMESupport.h"
#include "xString.h"

namespace
{
    zNMEShrapnelTableEntry sNMEShrap[37] = {
        { NME_TYPE_BUCKETJELLY, "shrap_bucketjelly_death", NULL },
        { NME_TYPE_FLINGER_V1, "shrap_flinger_v1_death", NULL },
        { NME_TYPE_FLINGER_V2, "shrap_flinger_v2_death", NULL },
        { NME_TYPE_FLINGER_V3, "shrap_flinger_v3_death", NULL },
        { NME_TYPE_FOGGER_V1, "shrap_fogger_v1_death", NULL },
        { NME_TYPE_FOGGER_V2, "shrap_fogger_v2_death", NULL },
        { NME_TYPE_FOGGER_V3, "shrap_fogger_v3_death", NULL },
        { NME_TYPE_ZAPPER_V1, "shrap_minimerv_v1_death", NULL },
        { NME_TYPE_ZAPPER_V2, "shrap_minimerv_v2_death", NULL },
        { NME_TYPE_ZAPPER_V3, "shrap_minimerv_v3_death", NULL },
        { NME_TYPE_MERVYN_V1, "shrap_mervyn_v1_death", NULL },
        { NME_TYPE_MERVYN_V2, "shrap_mervyn_v2_death", NULL },
        { NME_TYPE_MERVYN_V3, "shrap_mervyn_v3_death", NULL },
        { NME_TYPE_POPPER_V1, "shrap_popper_v1_death", NULL },
        { NME_TYPE_POPPER_V2, "shrap_popper_v2_death", NULL },
        { NME_TYPE_POPPER_V3, "shrap_popper_v3_death", NULL },
        { NME_TYPE_SLAMMER_V1, "shrap_slammer_v1_death", NULL },
        { NME_TYPE_SLAMMER_V2, "shrap_slammer_v2_death", NULL },
        { NME_TYPE_SLAMMER_V3, "shrap_slammer_v3_death", NULL },
        { NME_TYPE_SPINNER_V1, "shrap_spinner_v1_death", NULL },
        { NME_TYPE_SPINNER_V2, "shrap_spinner_v2_death", NULL },
        { NME_TYPE_SPINNER_V3, "shrap_spinner_v3_death", NULL },
        { NME_TYPE_TURBARREL_V1, "shrap_turret_v1_death", NULL },
        { NME_TYPE_TURBARREL_V2, "shrap_turret_v2_death", NULL },
        { NME_TYPE_TURBARREL_V3, "shrap_turret_v3_death", NULL },
        { NME_TYPE_BUCKOTRON_V1, "shrap_buckotron_bb_death", NULL },
        { NME_TYPE_BUCKOTRON_V2, "shrap_buckotron_de_death", NULL },
        { NME_TYPE_BUCKOTRON_V3, "shrap_buckotron_gg_death", NULL },
        { NME_TYPE_BUCKOTRON_V4, "shrap_buckotron_jk_death", NULL },
        { NME_TYPE_BUCKOTRON_V5, "shrap_buckotron_pt_death", NULL },
        { NME_TYPE_BUCKOTRON_V6, "shrap_buckotron_tr_death", NULL },
        { NME_TYPE_BUCKOTRON_V7, "shrap_buckotron_death", NULL },
        { NME_TYPE_TIKI_WOOD, "shrap_crate_wood", NULL },
        { NME_TYPE_TIKI_LOVEY, "shrap_crate_hover", NULL },
        { NME_TYPE_TIKI_QUIET, "shrap_crate_shrink", NULL },
        { NME_TYPE_TIKI_THUNDER, "shrap_crate_explode", NULL },
        { NME_TYPE_TIKI_STONE, "shrap_crate_steel", NULL },
    };
    S32 sShrapnelTableSize = sizeof(sNMEShrap) / sizeof(sNMEShrap[0]);
} // namespace

zShrapnelAsset* zNMEShrapnelRetrieveShrapnel(en_npctyp wantedType)
{
    S32 i;

    for (i = 0; i < sShrapnelTableSize; i++)
    {
        if (wantedType == sNMEShrap[i].nmeType)
        {
            return sNMEShrap[i].data;
        }
    }

    return NULL;
}

void zNMEShrapnelScenePostInit()
{
    S32 i;

    for (i = 0; i < sShrapnelTableSize; i++)
    {
        sNMEShrap[i].data = NULL;
        if (sNMEShrap[i].name != NULL && sNMEShrap[i].name[0] != '\0')
        {
            sNMEShrap[i].data = (zShrapnelAsset*)NMEC_FindAsset(xStrHash(sNMEShrap[i].name));
        }
    }
}
