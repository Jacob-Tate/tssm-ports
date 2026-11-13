#include "zNMEAsset.h"
#include "zNMECommon.h"
#include "zNMETypeBuckOTron.h"
#include "zNMETypeBossDennis.h"
#include "zNMETypeBossFrogfish.h"
#include "zNMETypeBossNeptune.h"
#include "zNMETypeFrdMindy.h"
#include "zNMETypeStandard.h"
#include "zNMETypeTurret.h"
#include "zNPCMgr.h"
#include "zScene.h"
#include "xstransvc.h"
#include "xordarray.h"
#include "xString.h"

void zSceneObjHashtableAdd(U32 id, xBase* base);

// Type-specific asset data of "Enemy:SB:SupplyCrate" DYNAs. The PS2 DWARF has no type for
// it, so the name is ours; nothing reads the trailing field, the total size (0x64) is the one
// ValidCrate checks for.
struct zNMEAssetCrate : zNMEAsset
{
    U32 unk_60;
};

struct DynNMEInfo
{
    char* nam_nmeAssetType;
    S32 (*fun_dynassValidate)(const xDynAsset*, U32);
    xEnt* (*fun_nmeCreate)(const xDynAsset*, U32);
    U32 hash_nmeAssetType;
};

// BfBB's zNPCSettings_MakeDummy: builds the default settings that zNPCSettings_Find falls back on.
// Nothing calls it in TSSM, so it was dead-stripped, but its static dum still sits at the top of
// retail's .bss. The field values are BfBB's, without the fields TSSM dropped.
static zNMESettings* g_dflt_npcsettings;

void zNMESettings_MakeDummy()
{
    static zNMESettings dum;
    dum.id = 0xFEEDCAFE;
    dum.baseType = eBaseTypeNPCSettings;
    dum.linkCount = 0;
    dum.baseFlags = ((U16)(1 << 0));
    dum.type = 0xBAD0CACA;
    dum.version = 2;
    dum.handle = 0;
    dum.allowDetect = 1;
    dum.allowWander = 1;
    dum.allowPatrol = 1;
    dum.reduceCollide = 0;
    dum.useNavSplines = 1;
    dum.allowChase = 1;
    dum.allowAttack = 1;
    dum.assumeLOS = 0;
    dum.assumeFOV = 0;

    g_dflt_npcsettings = &dum;
}

static S32 ValidGeneric(const xDynAsset* dynass, U32 siz_dynass)
{
    U32 size_expected = sizeof(zNMEAsset) + dynass->linkCount * sizeof(xLinkAsset);
    return siz_dynass == size_expected;
}

static S32 ValidBucketOTron(const xDynAsset* dynass, U32 siz_dynass)
{
    U32 size_expected = sizeof(zNMEAssetBucketOTron) + dynass->linkCount * sizeof(xLinkAsset);
    return siz_dynass == size_expected;
}

static S32 ValidCastNCrew(const xDynAsset* dynass, U32 siz_dynass)
{
    U32 size_expected = sizeof(zNMEAsset) + dynass->linkCount * sizeof(xLinkAsset);
    return siz_dynass == size_expected;
}

static S32 ValidCrate(const xDynAsset* dynass, U32 siz_dynass)
{
    U32 size_expected = sizeof(zNMEAssetCrate) + dynass->linkCount * sizeof(xLinkAsset);
    return siz_dynass == size_expected;
}

static S32 ValidCritter(const xDynAsset* dynass, U32 siz_dynass)
{
    U32 size_expected = sizeof(zNMEAssetCritter) + dynass->linkCount * sizeof(xLinkAsset);
    return siz_dynass == size_expected;
}

static S32 ValidDennis(const xDynAsset* dynass, U32 siz_dynass)
{
    U32 size_expected = sizeof(zNMEAssetDennis) + dynass->linkCount * sizeof(xLinkAsset);
    return siz_dynass == size_expected;
}

static S32 ValidFrogFish(const xDynAsset* dynass, U32 siz_dynass)
{
    U32 size_expected = sizeof(zNMEAssetFrogFish) + dynass->linkCount * sizeof(xLinkAsset);
    return siz_dynass == size_expected;
}

static S32 ValidMindy(const xDynAsset* dynass, U32 siz_dynass)
{
    U32 size_expected = sizeof(zNMEAssetMindy) + dynass->linkCount * sizeof(xLinkAsset);
    return siz_dynass == size_expected;
}

static S32 ValidNeptune(const xDynAsset* dynass, U32 siz_dynass)
{
    U32 size_expected = sizeof(zNMEAssetNeptune) + dynass->linkCount * sizeof(xLinkAsset);
    return siz_dynass == size_expected;
}

static S32 ValidStandard(const xDynAsset* dynass, U32 siz_dynass)
{
    U32 size_expected = sizeof(zNMEAssetStandard) + dynass->linkCount * sizeof(xLinkAsset);
    return siz_dynass == size_expected;
}

static S32 ValidTurret(const xDynAsset* dynass, U32 siz_dynass)
{
    U32 size_expected = sizeof(zNMEAssetTurret) + dynass->linkCount * sizeof(xLinkAsset);
    return siz_dynass == size_expected;
}

static xEnt* MakeNMEGeneric(const xDynAsset* dynass, U32 siz_dynass)
{
    xEntAsset* entass;
    xEnt* ent;

    // The entity asset embedded in the DYNA gets its base info from the DYNA header.
    entass = &((zNMEAsset*)dynass)->ent_asset;
    entass->id = dynass->id;
    entass->baseFlags = dynass->baseFlags;
    entass->linkCount = dynass->linkCount;
    entass->baseType = eBaseTypeNPC;

    ent = zNPCMgr_createDynNMEInst(entass, dynass, siz_dynass);
    return ent;
}

static DynNMEInfo g_nmeAssInfo[] = {
    { "Enemy:SB", ValidGeneric, MakeNMEGeneric, 0 },
    { "Enemy:SB:BucketOTron", ValidBucketOTron, MakeNMEGeneric, 0 },
    { "Enemy:SB:CastNCrew", ValidCastNCrew, MakeNMEGeneric, 0 },
    { "Enemy:SB:SupplyCrate", ValidCrate, MakeNMEGeneric, 0 },
    { "Enemy:SB:Critter", ValidCritter, MakeNMEGeneric, 0 },
    { "Enemy:SB:Dennis", ValidDennis, MakeNMEGeneric, 0 },
    { "Enemy:SB:FrogFish", ValidFrogFish, MakeNMEGeneric, 0 },
    { "Enemy:SB:Mindy", ValidMindy, MakeNMEGeneric, 0 },
    { "Enemy:SB:Neptune", ValidNeptune, MakeNMEGeneric, 0 },
    { "Enemy:SB:Standard", ValidStandard, MakeNMEGeneric, 0 },
    { "Enemy:SB:Turret", ValidTurret, MakeNMEGeneric, 0 },
    { NULL, NULL, NULL, 0 },
};

// Hashes the asset type names once. No symbol: auto-inlined into zNMEAsset_CountEnemies and
// dead-stripped (Ratatouille's zNPCAsset_Startup is the same helper).
static void zNMEAsset_Startup()
{
    DynNMEInfo* nmeinf_next;
    DynNMEInfo* nmeinf;
    static S32 needToHash = 1;

    if (needToHash)
    {
        needToHash = 0;

        nmeinf_next = g_nmeAssInfo;
        while (nmeinf_next->nam_nmeAssetType != NULL)
        {
            nmeinf = nmeinf_next++;
            nmeinf->hash_nmeAssetType = xStrHash(nmeinf->nam_nmeAssetType);
        }
    }
}

S32 zNMEAsset_CountEnemies(S32* num_nme, S32* num_INnpc)
{
    S32 count_NME;
    S32 num_alldyn;
    S32 i;
    U32 siz_dynass;
    xDynAsset* dynass;
    S32 isOurs;
    DynNMEInfo* nmeinf_next;
    DynNMEInfo* nmeinf;

    zNMEAsset_Startup();

    count_NME = 0;
    num_alldyn = xSTAssetCountByType('DYNA');
    for (i = 0; i < num_alldyn; i++)
    {
        dynass = (xDynAsset*)xSTFindAssetByType('DYNA', i, &siz_dynass);

        nmeinf_next = g_nmeAssInfo;
        isOurs = 0;
        while (nmeinf_next->nam_nmeAssetType != NULL)
        {
            nmeinf = nmeinf_next++;
            if (nmeinf->hash_nmeAssetType == dynass->type)
            {
                isOurs = 1;
                break;
            }
        }

        if (isOurs)
        {
            count_NME++;
        }
    }

    if (num_nme != NULL)
    {
        *num_nme = count_NME;
    }
    if (num_INnpc != NULL)
    {
        *num_INnpc = 0;
    }

    return count_NME;
}

void zNMEAsset_CreateEnemies()
{
    S32 num_alldyn;
    S32 i;
    U32 siz_dynass;
    xDynAsset* dynass;
    DynNMEInfo* nmeinf_next;
    DynNMEInfo* nmeinf;

    num_alldyn = xSTAssetCountByType('DYNA');
    for (i = 0; i < num_alldyn; i++)
    {
        dynass = (xDynAsset*)xSTFindAssetByType('DYNA', i, &siz_dynass);
        if (dynass == NULL)
        {
            continue;
        }

        nmeinf_next = g_nmeAssInfo;
        while (nmeinf_next->nam_nmeAssetType != NULL)
        {
            nmeinf = nmeinf_next++;
            if (nmeinf->hash_nmeAssetType == dynass->type)
            {
                nmeinf->fun_nmeCreate(dynass, siz_dynass);
                break;
            }
        }
    }
}

void zNMEAsset_RegisterEnemies(zScene* zscn, S32* base_idx)
{
    st_XORDEREDARRAY* npclist;
    S32 num_npc;
    S32 i;
    zNPC::base* npc;

    npclist = zNPCMgr_GetNPCList();
    num_npc = npclist->cnt;

    for (i = 0; i < num_npc; i++)
    {
        npc = (zNPC::base*)npclist->list[i];

        zSceneSet(npc, *base_idx);
        xSceneAddEnt(zscn, (xEnt*)zscn->base[*base_idx]);
        zSceneObjHashtableAdd(npc->id, npc);

        (*base_idx)++;
    }
}
