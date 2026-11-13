#include "xpkrsvc.h"
#include "xhipio.h"
#include "xordarray.h"
#include "xutil.h"
#include "xDebug.h"
#include "xMemMgr.h"

#include <string.h>
#include <stdio.h>

static st_PACKER_READ_DATA* PKR_ReadInit(void* userdata, const char* pkgfile, U32 opts, S32* cltver,
                                         PKRAssetType* typelist, S32 fileflags);
static void PKR_ReadDone(st_PACKER_READ_DATA* pr);
static S32 PKR_SetActive(st_PACKER_READ_DATA* pr, en_LAYER_TYPE layer);
static S32 PKR_parse_TOC(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr);
static S32 PKR_LoadStep_Async();
static S32 PKR_findNextLayerToLoad(st_PACKER_READ_DATA** work_on_pkg,
                                   st_PACKER_LTOC_NODE** next_layer);
static void PKR_updateLayerAssets(st_PACKER_LTOC_NODE* laynode);
static void PKR_xform_asset(st_PACKER_ATOC_NODE* assnode, S32 dumpable_layer);
static S32 PKR_LoadLayer(st_PACKER_READ_DATA* pr, en_LAYER_TYPE layer);
static void* PKR_LoadAsset(st_PACKER_READ_DATA* pr, U32 aid, const char*, void*, S32*);
static U32 PKR_GetAssetSize(st_PACKER_READ_DATA* pr, U32 aid);
static S32 PKR_AssetCount(st_PACKER_READ_DATA* pr, U32 type);
static void* PKR_AssetByType(st_PACKER_READ_DATA* pr, U32 type, S32 idx, U32* size);
static S32 PKR_IsAssetReady(st_PACKER_READ_DATA* pr, U32 aid);
static U32 PKR_getPackTimestamp(st_PACKER_READ_DATA* pr);
static void PKR_Disconnect(st_PACKER_READ_DATA* pr);
static char* PKR_AssetName(st_PACKER_READ_DATA* pr, U32 aid);
static U32 PKR_GetBaseSector(st_PACKER_READ_DATA* pr);
static S32 PKR_GetAssetInfo(st_PACKER_READ_DATA* pr, U32 aid, PKRAssetTOCInfo* tocainfo);
static S32 PKR_GetAssetInfoByType(st_PACKER_READ_DATA* pr, U32 type, S32 idx,
                                  PKRAssetTOCInfo* tocainfo);
static S32 PKR_PkgHasAsset(st_PACKER_READ_DATA* pr, U32 aid);
static S32 PKR_FRIEND_assetIsGameDup(U32 aid, const st_PACKER_READ_DATA* skippr, S32 oursize,
                                     U32 ourtype, U32 chksum, char*);
static S32 OrdComp_R_Asset(void* vkey, void* vitem);
static S32 OrdTest_R_AssetID(const void* vkey, void* vitem);
static S32 LOD_r_PACK(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr);
static S32 LOD_r_AHDR(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr);
static S32 LOD_r_LHDR(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr);
static void PKR_bld_typecnt(st_PACKER_READ_DATA* pr);
static void* PKR_getmem(U32 id, S32 amount, U32 aid, S32 align, S32 isTemp, char** memtru);

static PKRReadFuncs g_pkr_read_funcmap_original = { 1,
                                                    PKR_ReadInit,
                                                    PKR_ReadDone,
                                                    PKR_LoadLayer,
                                                    PKR_GetAssetSize,
                                                    PKR_LoadAsset,
                                                    PKR_AssetByType,
                                                    PKR_AssetCount,
                                                    PKR_IsAssetReady,
                                                    PKR_SetActive,
                                                    PKR_AssetName,
                                                    PKR_GetBaseSector,
                                                    PKR_GetAssetInfo,
                                                    PKR_GetAssetInfoByType,
                                                    PKR_PkgHasAsset,
                                                    PKR_getPackTimestamp,
                                                    PKR_Disconnect };
static PKRReadFuncs g_pkr_read_funcmap = g_pkr_read_funcmap_original;
st_PACKER_READ_DATA g_readdatainst[20] = {};

st_HIPLOADFUNCS* g_hiprf;
U32 g_loadlock;
S32 pkr_sector_size;
S32 g_packinit;
S32 g_memalloc_pair;
S32 g_memalloc_runtot;
S32 g_memalloc_runfree;

// Unreferenced in retail, but its strings open the unit's string pool.
static char* g_strz_layername[PKR_LTYPE_NOMORE] = { "DEFAULT",  "TEXTURE",     "TEXTURE_STRM",
                                                    "BSP",      "MODEL",       "ANIMATION",
                                                    "VRAM",     "SRAM",        "SNDTOC",
                                                    "CUTSCENE", "CUTSCENETOC", "JSPINFO" };

static inline void PKR_relmem(U32 id, S32 blksize, void* memptr, U32, S32 isTemp)
{
    g_memalloc_pair--;
    g_memalloc_runfree += blksize;
    if (g_memalloc_runfree < 0)
    {
        g_memalloc_runfree = blksize;
    }

    xUtil_idtag2string(id, 0);
    if (memptr != NULL && blksize > 0)
    {
        if (isTemp)
        {
            RwFree(memptr);
        }
        else
        {
            xUtil_idtag2string(id, 1);
        }
    }
}

static inline void PKR_push_memmark()
{
    xMemPushBase();
}

static inline void PKR_pop_memmark()
{
    xMemPopBase(xMemGetBase() - 1);
}

static inline en_PKR_LAYER_LOAD_DEST PKR_layerLoadDest(en_LAYER_TYPE layer)
{
    switch (layer)
    {
    case PKR_LTYPE_TEXTURE_STRM:
    case PKR_LTYPE_SRAM:
    case PKR_LTYPE_SNDTOC:
    case PKR_LTYPE_CUTSCENE:
        return PKR_LDDEST_SKIP;
    case PKR_LTYPE_TEXTURE:
    case PKR_LTYPE_BSP:
    case PKR_LTYPE_MODEL:
        return PKR_LDDEST_RWHANDOFF;
    case PKR_LTYPE_ANIMATION:
    case PKR_LTYPE_JSPINFO:
        return PKR_LDDEST_KEEPMALLOC;
    case PKR_LTYPE_DEFAULT:
    case PKR_LTYPE_VRAM:
    case PKR_LTYPE_CUTSCENETOC:
        return PKR_LDDEST_KEEPSTATIC;
    default:
        return PKR_LDDEST_KEEPSTATIC;
    }
}

static inline S32 PKR_layerTypeNeedsXForm(en_LAYER_TYPE layer)
{
    switch (layer)
    {
    case PKR_LTYPE_TEXTURE:
    case PKR_LTYPE_BSP:
    case PKR_LTYPE_MODEL:
        return true;
    case PKR_LTYPE_DEFAULT:
    case PKR_LTYPE_ANIMATION:
    case PKR_LTYPE_VRAM:
    case PKR_LTYPE_SNDTOC:
    case PKR_LTYPE_CUTSCENETOC:
    case PKR_LTYPE_JSPINFO:
        return true;
    case PKR_LTYPE_SRAM:
    case PKR_LTYPE_CUTSCENE:
        return false;
    default:
        return false;
    }
}

static inline char* PKR_LayerMemReserve(st_PACKER_READ_DATA* pr, st_PACKER_LTOC_NODE* layer)
{
    char* mem = NULL;
    en_PKR_LAYER_LOAD_DEST loaddest;

    if (layer->laymem != NULL)
    {
        return layer->laymem;
    }

    loaddest = PKR_layerLoadDest(layer->laytyp);
    switch (loaddest)
    {
    case PKR_LDDEST_SKIP:
        break;
    case PKR_LDDEST_KEEPSTATIC:
        mem = (char*)PKR_getmem('LYR\0', layer->laysize, layer->laytyp + 0x8000, 0x40, false, NULL);
        break;
    case PKR_LDDEST_KEEPMALLOC:
        mem = (char*)PKR_getmem('LYR\0', layer->laysize, layer->laytyp + 0x8000, 0x40, true,
                                &layer->laytru);
        break;
    case PKR_LDDEST_RWHANDOFF:
        PKR_push_memmark();
        mem = (char*)PKR_getmem('LYR\0', layer->laysize, layer->laytyp + 0x8000, 0x40, false, NULL);
        break;
    }

    return mem;
}

static inline void PKR_LayerMemRelease(st_PACKER_READ_DATA* pr, st_PACKER_LTOC_NODE* layer)
{
    switch (PKR_layerLoadDest(layer->laytyp))
    {
    case PKR_LDDEST_SKIP:
        break;
    case PKR_LDDEST_RWHANDOFF:
        PKR_relmem('LYR\0', layer->laysize, layer->laymem, layer->laytyp + 0x8000, false);
        PKR_pop_memmark();
        layer->laymem = NULL;
        break;
    case PKR_LDDEST_KEEPSTATIC:
        PKR_relmem('LYR\0', layer->laysize, layer->laymem, layer->laytyp + 0x8000, false);
        break;
    case PKR_LDDEST_KEEPMALLOC:
        PKR_relmem('LYR\0', layer->laysize, layer->laytru, layer->laytyp + 0x8000, true);
        layer->laymem = NULL;
        layer->laytru = NULL;
        break;
    }
}

static inline void PKR_drv_guardLayer(st_PACKER_LTOC_NODE*)
{
}

static inline S32 PKR_drv_guardVerify(st_PACKER_LTOC_NODE*)
{
    return 1;
}

static inline void PKR_kiilpool_anode(st_PACKER_READ_DATA* pr)
{
    S32 amount;

    if (pr->asscnt == 0)
    {
        return;
    }

    amount = pr->asscnt * sizeof(st_PACKER_ATOC_NODE);
    PKR_relmem('ANOD', amount, pr->pool_anode, 'FAKE', false);
    pr->pool_anode = NULL;
    pr->pool_nextaidx = 0;
}

static inline void PKR_oldlaynode(st_PACKER_LTOC_NODE* laynode)
{
    XOrdDone(&laynode->assref, 0);
    PKR_relmem('LNOD', sizeof(st_PACKER_LTOC_NODE), laynode, laynode->laytyp + 0x8000, false);
}

static inline st_PACKER_ATOC_NODE* PKR_newassnode(st_PACKER_READ_DATA* pr, U32 aid)
{
    S32 idx = pr->pool_nextaidx;
    st_PACKER_ATOC_NODE* newnode = (st_PACKER_ATOC_NODE*)&pr->pool_anode[idx];
    pr->pool_nextaidx++;

    memset(newnode, 0, sizeof(st_PACKER_ATOC_NODE));
    newnode->aid = aid;
    return newnode;
}

static inline st_PACKER_LTOC_NODE* PKR_newlaynode(en_LAYER_TYPE layer, S32 refcnt)
{
    st_PACKER_LTOC_NODE* newnode = (st_PACKER_LTOC_NODE*)PKR_getmem(
        'LNOD', sizeof(st_PACKER_LTOC_NODE), layer + 0x8000, 0x40, false, NULL);
    memset(newnode, 0, sizeof(st_PACKER_LTOC_NODE));

    newnode->laytyp = layer;
    XOrdInit(&newnode->assref, refcnt <= 1 ? 2 : refcnt, 0);
    return newnode;
}

static inline PKRAssetType* PKR_type2typeref(U32 asstype, PKRAssetType* types)
{
    PKRAssetType* da_type = NULL;
    if (types != NULL)
    {
        for (PKRAssetType* tmptype = types; tmptype->typetag != 0; tmptype++)
        {
            if (tmptype->typetag == asstype)
            {
                da_type = tmptype;
                break;
            }
        }
    }
    if (da_type == NULL)
    {
        xUtil_idtag2string(asstype, 0);
    }
    return da_type;
}

// Defined further down, as in the original source.
static inline S32 LOD_r_ADBG(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr,
                             st_PACKER_ATOC_NODE* assnode);

static inline S32 LOD_r_LDBG(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr,
                             st_PACKER_LTOC_NODE* laynode)
{
    S32 ivar = 0;
    if (pr->subver > 1)
    {
        g_hiprf->readLongs(pkg, &ivar, 1);
        laynode->chksum = ivar;
    }
    return 1;
}

static inline S32 PKR_typeHdlr_idx(st_PACKER_READ_DATA* pr, U32 type)
{
    S32 da_idx = -1;
    S32 cnt = 0;
    const PKRAssetType* atype = pr->types;
    while (atype->typetag != 0)
    {
        if (atype->typetag == type)
        {
            da_idx = cnt;
            break;
        }
        cnt++;
        atype++;
    }

    return da_idx;
}

static inline void PKR_alloc_chkidx()
{
}

static inline S32 PKR_makepool_anode(st_PACKER_READ_DATA* pr, S32 cnt)
{
    if (cnt == 0)
    {
        return 0;
    }

    st_PACKER_ATOC_NODE* assnode = (st_PACKER_ATOC_NODE*)PKR_getmem(
        'ANOD', cnt * sizeof(st_PACKER_ATOC_NODE), 'FAKE', 0x40, false, NULL);
    if (assnode != NULL)
    {
        pr->pool_anode = assnode;
        pr->pool_nextaidx = 0;
    }
    return assnode != NULL ? cnt * sizeof(st_PACKER_ATOC_NODE) : 0;
}

static inline S32 LOD_r_HIPA(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    pr->pkgver = 'HIPA';
    return 1;
}

static inline S32 LOD_r_AINF(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    S32 ival = 0;
    g_hiprf->readLongs(pkg, &ival, 1);
    return 1;
}

static inline S32 LOD_r_ATOC(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    U32 cid = g_hiprf->enter(pkg);
    while (cid != 0)
    {
        switch (cid)
        {
        case 'AINF':
            LOD_r_AINF(pkg, pr);
            break;
        case 'AHDR':
            LOD_r_AHDR(pkg, pr);
            break;
        }
        g_hiprf->exit(pkg);
        cid = g_hiprf->enter(pkg);
    }
    return 1;
}

static inline S32 LOD_r_LINF(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    S32 ival = 0;
    g_hiprf->readLongs(pkg, &ival, 1);
    return 1;
}

static inline S32 LOD_r_LTOC(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    U32 cid = g_hiprf->enter(pkg);
    while (cid != 0)
    {
        switch (cid)
        {
        case 'LINF':
            LOD_r_LINF(pkg, pr);
            break;
        case 'LHDR':
            LOD_r_LHDR(pkg, pr);
            break;
        }
        g_hiprf->exit(pkg);
        cid = g_hiprf->enter(pkg);
    }
    return 1;
}

static inline S32 LOD_r_DICT(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    U32 cid = g_hiprf->enter(pkg);
    while (cid != 0)
    {
        switch (cid)
        {
        case 'ATOC':
            LOD_r_ATOC(pkg, pr);
            XOrdSort(&pr->asstoc, OrdComp_R_Asset);
            break;
        case 'LTOC':
            LOD_r_LTOC(pkg, pr);
            break;
        }

        g_hiprf->exit(pkg);
        cid = g_hiprf->enter(pkg);
    }
    return 1;
}

static inline S32 LOD_r_DHDR(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    S32 ivar = 0;
    g_hiprf->readLongs(pkg, &ivar, 1);
    return 1;
}

static inline S32 LOD_r_DPAK(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    return 1;
}

static inline S32 LOD_r_STRM(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    U32 cid = g_hiprf->enter(pkg);
    while (cid != 0)
    {
        switch (cid)
        {
        case 'DHDR':
            LOD_r_DHDR(pkg, pr);
            break;
        case 'DPAK':
            LOD_r_DPAK(pkg, pr);
            break;
        }
        g_hiprf->exit(pkg);
        cid = g_hiprf->enter(pkg);
    }
    return 1;
}

extern _tagGameVidMode gVidMode;

static inline void PKR_spew_verhist()
{
}

static inline S32 LOD_r_PVER(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    S32 ver = 0;
    g_hiprf->readLongs(pkg, &ver, 1);
    pr->subver = ver;

    if (ver < 2)
    {
        PKR_spew_verhist();
    }

    ver = -1;
    g_hiprf->readLongs(pkg, &ver, 1);
    pr->cltver = ver;

    ver = -1;
    S32 amt = g_hiprf->readLongs(pkg, &ver, 1);
    if (amt != 1)
    {
        pr->compatver = 1;
    }
    else
    {
        pr->compatver = ver;
    }
    return 1;
}

static inline S32 LOD_r_PFLG(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    S32 flg = 0;
    g_hiprf->readLongs(pkg, &flg, 1);
    return 1;
}

static inline S32 LOD_r_PCNT(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    S32 cnt = 0;
    g_hiprf->readLongs(pkg, &cnt, 1);
    pr->asscnt = cnt;
    g_hiprf->readLongs(pkg, &cnt, 1);
    pr->laycnt = cnt;
    g_hiprf->readLongs(pkg, &cnt, 1);
    g_hiprf->readLongs(pkg, &cnt, 1);
    g_hiprf->readLongs(pkg, &cnt, 1);
    return 1;
}

static inline S32 LOD_r_PCRT(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    S32 time = 0;
    char arr[256] = {};

    g_hiprf->readLongs(pkg, &time, 1);
    pr->time_made = time;

    if (pr->subver > 1)
    {
        g_hiprf->readString(pkg, arr);
    }
    return 1;
}

static inline S32 LOD_r_PMOD(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    S32 time_mod = 0;
    g_hiprf->readLongs(pkg, &time_mod, 1);
    pr->time_mod = time_mod;
    return 1;
}

static inline S32 ValidatePlatform(st_HIPLOADDATA*, st_PACKER_READ_DATA*, char* plat, char* vid,
                                   char* lang, char* title)
{
    char fullname[128] = {};
    sprintf(fullname, "%s %s %s %s", plat, vid, lang, title);

    S32 rc = stricmp(plat, "GC") == 0;
    if (!rc)
    {
        return 0;
    }

    if (gVidMode == eGameVidModeNTSC)
    {
        rc = stricmp(vid, "NTSC") == 0;
    }
    else if (gVidMode == eGameVidModePAL)
    {
        rc = stricmp(vid, "PAL") == 0;
    }
    if (!rc)
    {
        return 0;
    }

    rc = stricmp(title, "Incredibles") == 0;
    if (!rc)
    {
        return 0;
    }
    return 1;
}

static inline S32 LOD_r_PLAT(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    S32 result = 1;
    char platname[32] = {};
    char vidname[32] = {};
    char langname[32] = {};
    char titlename[32] = {};

    g_hiprf->readString(pkg, platname);
    g_hiprf->readString(pkg, vidname);
    g_hiprf->readString(pkg, langname);
    S32 n = g_hiprf->readString(pkg, titlename);
    if (n == 0)
    {
        strcpy(titlename, "<Unknown>");
    }

    S32 rc = ValidatePlatform(pkg, pr, platname, vidname, langname, titlename);
    if (rc == 0)
    {
        result = 0;
    }
    return result;
}

static inline void* PKR_FindAsset(st_PACKER_READ_DATA* pr, U32 aid, S32* assetsize)
{
    st_PACKER_ATOC_NODE* assnode = NULL;
    S32 idx = XOrdLookup(&pr->asstoc, (void*)aid, OrdTest_R_AssetID);

    if (!(idx < 0))
    {
        assnode = (st_PACKER_ATOC_NODE*)pr->asstoc.list[idx];
    }

    if (assetsize != NULL)
    {
        if (assnode == NULL)
        {
            *assetsize = 0;
        }
        else if (assnode->memloc == NULL)
        {
            *assetsize = 0;
        }
        else if (assnode->x_size > 0)
        {
            *assetsize = assnode->x_size;
        }
        else
        {
            *assetsize = assnode->d_size;
        }
    }

    if (assnode != NULL)
    {
        return assnode->memloc;
    }
    return NULL;
}

static inline void PKR_xformLayerAssets(st_PACKER_LTOC_NODE* laynode)
{
    S32 i;
    S32 will_be_dumped = false;
    en_PKR_LAYER_LOAD_DEST loaddest = PKR_layerLoadDest(laynode->laytyp);

    if (loaddest == PKR_LDDEST_RWHANDOFF)
    {
        will_be_dumped = true;
    }

    for (i = 0; i < laynode->assref.cnt; i++)
    {
        st_PACKER_ATOC_NODE* tmpass = (st_PACKER_ATOC_NODE*)laynode->assref.list[i];
        if ((tmpass->loadflag & 0x100000) == 0)
        {
            PKR_xform_asset(tmpass, will_be_dumped);
            if (will_be_dumped && tmpass->x_size < 1)
            {
                tmpass->Name();
                tmpass->memloc = NULL;
            }
        }
    }
}

PKRReadFuncs* PKRGetReadFuncs(S32 apiver)
{
    switch (apiver)
    {
    case 1:
        return &g_pkr_read_funcmap;
    default:
        return NULL;
    }
}

S32 PKRStartup()
{
    if (g_packinit++ == 0)
    {
        g_pkr_read_funcmap = g_pkr_read_funcmap_original;
        g_hiprf = get_HIPLFuncs();
        pkr_sector_size = 32;
    }
    return g_packinit;
}

S32 PKRShutdown()
{
    g_packinit--;
    return g_packinit;
}

S32 PKRLoadStep(S32)
{
    return PKR_LoadStep_Async();
}

static st_PACKER_READ_DATA* PKR_ReadInit(void* userdata, const char* pkgfile, U32 opts, S32* cltver,
                                         PKRAssetType* typelist, S32 fileflags)
{
    st_PACKER_READ_DATA* pr = NULL;
    S32 i;
    S32 uselock = -1;
    char* tocbuf_RAW = NULL;
    char* tocbuf_aligned;

    PKR_alloc_chkidx();
    tocbuf_aligned = (char*)PKR_getmem('PTOC', 0x8000, 'PTOC', 0x40, 1, &tocbuf_RAW);

    for (i = 0; i < 20; i++)
    {
        if (!(g_loadlock & 1 << i))
        {
            g_loadlock |= 1 << i;
            pr = &g_readdatainst[i];
            uselock = i;
            break;
        }
    }

    if (pr != NULL)
    {
        memset(pr, 0, sizeof(st_PACKER_READ_DATA));
        pr->lockid = uselock;
        pr->userdata = userdata;
        pr->opts = opts;
        pr->types = typelist;
        pr->cltver = -1;
        strncpy(pr->packfile, pkgfile, 0x80);

        if (tocbuf_aligned == NULL)
        {
            pr->pkg = g_hiprf->create(pkgfile, NULL, 0, fileflags);
        }
        else
        {
            pr->pkg = g_hiprf->create(pkgfile, tocbuf_aligned, 0x8000, fileflags);
        }

        if (pr->pkg != NULL)
        {
            pr->base_sector = g_hiprf->basesector(pr->pkg);
            PKR_parse_TOC(pr->pkg, pr);
            *cltver = pr->cltver;
            g_hiprf->setBypass(pr->pkg, 1, 1);
        }
        else
        {
            PKR_ReadDone(pr);
            pr = NULL;
            *cltver = -1;
        }
    }
    else
    {
        *cltver = -1;
        pr = NULL;
    }

    PKR_relmem('PTOC', 0x8000, tocbuf_RAW, 'PTOC', 1);
    tocbuf_RAW = NULL;
    return pr;
}

static void PKR_ReadDone(st_PACKER_READ_DATA* pr)
{
    S32 i;
    S32 j;
    S32 lockid;
    st_PACKER_ATOC_NODE* assnode;
    st_PACKER_LTOC_NODE* laynode;
    st_XORDEREDARRAY* tmplist;

    if (pr == NULL)
    {
        return;
    }
    for (j = pr->laytoc.cnt - 1; j >= 0; j--)
    {
        laynode = (st_PACKER_LTOC_NODE*)pr->laytoc.list[j];
        for (i = laynode->assref.cnt - 1; i >= 0; i--)
        {
            assnode = (st_PACKER_ATOC_NODE*)laynode->assref.list[i];
            if (assnode->typeref != NULL && assnode->typeref->assetUnloaded != NULL &&
                !(assnode->loadflag & 0x100000))
            {
                assnode->typeref->assetUnloaded(assnode->memloc, assnode->aid);
            }
        }
    }

    for (j = 0; j < pr->laytoc.cnt; j++)
    {
        laynode = (st_PACKER_LTOC_NODE*)pr->laytoc.list[j];
        if (laynode->laymem != NULL)
        {
            PKR_LayerMemRelease(pr, laynode);
            laynode->laymem = NULL;
        }
    }

    PKR_kiilpool_anode(pr);

    for (j = 0; j < pr->laytoc.cnt; j++)
    {
        laynode = (st_PACKER_LTOC_NODE*)pr->laytoc.list[j];
        PKR_oldlaynode(laynode);
    }

    XOrdDone(&pr->asstoc, false);
    XOrdDone(&pr->laytoc, false);

    for (i = 0; i < 129; i++)
    {
        tmplist = &pr->typelist[i];
        if (tmplist->max != 0)
        {
            XOrdDone(tmplist, false);
        }
    }

    if (pr->pkg != NULL)
    {
        g_hiprf->destroy(pr->pkg);
        pr->pkg = NULL;
    }

    lockid = pr->lockid;
    memset(pr, 0, sizeof(st_PACKER_READ_DATA));
    g_loadlock &= ~(1 << lockid);
}

static S32 PKR_SetActive(st_PACKER_READ_DATA* pr, en_LAYER_TYPE layer)
{
    S32 result;
    S32 rc = 1;
    S32 i;
    S32 j;
    st_PACKER_ATOC_NODE* assnode;
    st_PACKER_LTOC_NODE* laynode;

    if (pr == NULL)
    {
        return 0;
    }

    for (i = 0; i < pr->laytoc.cnt; i++)
    {
        laynode = (st_PACKER_LTOC_NODE*)pr->laytoc.list[i];
        if (layer > PKR_LTYPE_DEFAULT && laynode->laytyp != layer)
        {
            continue;
        }

        for (j = 0; j < laynode->assref.cnt; j++)
        {
            assnode = (st_PACKER_ATOC_NODE*)laynode->assref.list[j];
            result = assnode->loadflag & 0x80000;
            if (assnode->loadflag & 0x10000 || result == 0)
            {
                continue;
            }

            if (assnode->typeref == NULL)
            {
                assnode->Name();
                xUtil_idtag2string(assnode->asstype, 0);
            }
            else if (assnode->typeref->assetLoaded != NULL)
            {
                if (!(assnode->typeref->assetLoaded(pr->userdata, assnode->aid, assnode->memloc,
                                                    assnode->d_size)))
                {
                    rc = 0;
                }
                else
                {
                    assnode->loadflag |= 0x10000;
                }
            }
        }
    }

    return rc;
}

static S32 PKR_parse_TOC(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    // not in the DWARF order (is_ok, cid, done): that order changes the generated code
    S32 done = 0;
    S32 is_ok = 0;
    U32 cid = g_hiprf->enter(pkg);
    while (cid != false)
    {
        switch (cid)
        {
        case 'HIPA':
            done = LOD_r_HIPA(pkg, pr);
            break;
        case 'PACK':
            LOD_r_PACK(pkg, pr);
            if (done)
            {
                if (pr->asscnt > 0)
                {
                    XOrdInit(&pr->asstoc, pr->asscnt, 0);
                    PKR_makepool_anode(pr, pr->asscnt);
                }
                if (pr->laycnt > 0)
                {
                    XOrdInit(&pr->laytoc, pr->laycnt, 0);
                }
            }
            break;
        case 'DICT':
            LOD_r_DICT(pkg, pr);
            PKR_bld_typecnt(pr);
            is_ok = true;
            break;
        case 'STRM':
            LOD_r_STRM(pkg, pr);
            break;
        }

        if (!done)
        {
            break;
        }

        g_hiprf->exit(pkg);

        if (is_ok)
        {
            break;
        }

        cid = g_hiprf->enter(pkg);
    }
    return done;
}

static S32 PKR_LoadStep_Async()
{
    S32 moretodo;
    S32 rc;
    static st_PACKER_READ_DATA* curpr = NULL;
    static st_PACKER_LTOC_NODE* asynlay = NULL;

    if (asynlay == NULL)
    {
        PKR_findNextLayerToLoad(&curpr, &asynlay);

        if (asynlay != NULL)
        {
            if (PKR_layerLoadDest(asynlay->laytyp) != PKR_LDDEST_SKIP && asynlay->laysize > 1 &&
                asynlay->assref.cnt > 0)
            {
                asynlay->laymem = PKR_LayerMemReserve(curpr, asynlay);
                PKR_drv_guardLayer(asynlay);
                st_PACKER_ATOC_NODE* tmpass = (st_PACKER_ATOC_NODE*)asynlay->assref.list[0];
                g_hiprf->setSpot(tmpass->ownpkg, tmpass->d_off);

                if (g_hiprf->readBytes(tmpass->ownpkg, asynlay->laymem, asynlay->laysize))
                {
                    asynlay->flg_ldstat |= 0x1000000;
                }
                else
                {
                    PKR_LayerMemRelease(curpr, asynlay);
                    asynlay->flg_ldstat &= 0xfcffffff;
                    curpr = NULL;
                    asynlay = NULL;
                }

                rc = 1;
            }
            else
            {
                rc = 1;
                asynlay->flg_ldstat |= 0x2000000;
                asynlay = NULL;
            }
        }
        else
        {
            rc = 0;
            curpr = NULL;
            asynlay = NULL;
        }
    }
    else
    {
        moretodo = g_hiprf->pollRead(curpr->pkg);
        if (moretodo == 1)
        {
            moretodo = PKR_drv_guardVerify(asynlay);
        }

        if (!moretodo)
        {
            rc = 1;
        }
        else if (moretodo == 1)
        {
            PKR_updateLayerAssets(asynlay);

            if (PKR_layerTypeNeedsXForm(asynlay->laytyp))
            {
                PKR_xformLayerAssets(asynlay);
            }

            if (PKR_layerLoadDest(asynlay->laytyp) == PKR_LDDEST_RWHANDOFF)
            {
                PKR_LayerMemRelease(curpr, asynlay);
            }

            rc = 1;
            asynlay->flg_ldstat |= 0x2000000;
            asynlay = NULL;
        }
        else
        {
            en_PKR_LAYER_LOAD_DEST loaddest = PKR_layerLoadDest(asynlay->laytyp);
            if (asynlay->laymem != NULL && loaddest == PKR_LDDEST_RWHANDOFF)
            {
                PKR_LayerMemRelease(curpr, asynlay);
            }

            rc = 1;
            asynlay->flg_ldstat &= 0xfcffffff;
            asynlay = NULL;
            curpr = NULL;
        }
    }

    return rc;
}

static S32 PKR_findNextLayerToLoad(st_PACKER_READ_DATA** work_on_pkg,
                                   st_PACKER_LTOC_NODE** next_layer)
{
    st_PACKER_READ_DATA* tmppr;
    st_PACKER_LTOC_NODE* tmplay;
    S32 i;
    S32 j;

    *next_layer = NULL;
    if (*work_on_pkg != NULL)
    {
        tmppr = *work_on_pkg;
        for (j = 0; j < tmppr->laytoc.cnt; j++)
        {
            tmplay = (st_PACKER_LTOC_NODE*)tmppr->laytoc.list[j];
            if (!(tmplay->flg_ldstat & 0x2000000))
            {
                *next_layer = tmplay;
                *work_on_pkg = tmppr;
                break;
            }
        }
    }

    if (*next_layer == NULL)
    {
        tmppr = g_readdatainst;
        for (i = 0; i < 20; i++, tmppr++)
        {
            if ((g_loadlock & 1 << i) == 0 || tmppr == *work_on_pkg)
            {
                continue;
            }

            for (j = 0; j < tmppr->laytoc.cnt; j++)
            {
                tmplay = (st_PACKER_LTOC_NODE*)tmppr->laytoc.list[j];
                if (!(tmplay->flg_ldstat & 0x2000000))
                {
                    *next_layer = tmplay;
                    *work_on_pkg = tmppr;
                    break;
                }
            }

            if (*next_layer != NULL)
            {
                break;
            }
        }
    }

    return *next_layer != NULL;
}

// The asserts, the alignment check and reloff are Ratatouille's; they keep this over the inlining
// limit, as retail calls it from PKR_LoadStep_Async.
static void PKR_updateLayerAssets(st_PACKER_LTOC_NODE* laynode)
{
    S32 i = 0;
    st_PACKER_ATOC_NODE* tmpass = NULL;
    S32 lay_hip_pos;
    S32 reloff;

    for (i = 0; i < laynode->assref.cnt; i++)
    {
        tmpass = (st_PACKER_ATOC_NODE*)laynode->assref.list[i];
        if (tmpass->d_off > 0 && tmpass->d_size > 0)
        {
            break;
        }
        tmpass = NULL;
    }

    // NOTE: retail does not check tmpass for NULL here
    xASSERT(tmpass);
    xASSERT(i == 0);

    lay_hip_pos = tmpass->d_off;
    if (lay_hip_pos % pkr_sector_size)
    {
        xASSERT(0 && "Detected layer out of alignment");
    }

    for (i = 0; i < laynode->assref.cnt; i++)
    {
        tmpass = (st_PACKER_ATOC_NODE*)laynode->assref.list[i];
        if (!(tmpass->loadflag & 0x100000))
        {
            if (tmpass->loadflag & 0x200000)
            {
                tmpass->Name();
                tmpass->memloc = NULL;
            }
            else
            {
                reloff = tmpass->d_off - lay_hip_pos;
                tmpass->memloc = laynode->laymem + reloff;
                tmpass->loadflag |= 0x80000;
                tmpass->Name();
            }
        }
    }
}

static void PKR_xform_asset(st_PACKER_ATOC_NODE* assnode, S32 dumpable_layer)
{
    char* xformloc;
    PKRAssetType* atype;

    if (!(assnode->infoflag & 4))
    {
        if (assnode->typeref == NULL)
        {
            return;
        }

        if (assnode->typeref->readXForm != NULL)
        {
            assnode->Name();
            xUtil_idtag2string(assnode->asstype, 0);
        }
        else
        {
            return;
        }
    }

    atype = assnode->typeref;
    if (atype == NULL)
    {
        assnode->Name();
    }
    else if (atype->readXForm == NULL)
    {
        assnode->Name();
    }
    else
    {
        if (assnode->d_size == 0)
        {
            assnode->Name();
            xUtil_idtag2string(assnode->asstype, 0);
            assnode->memloc = NULL;
        }

        xformloc = (char*)atype->readXForm(assnode->ownpr->userdata, assnode->aid, assnode->memloc,
                                           assnode->d_size, (U32*)&assnode->x_size);
        if (!dumpable_layer && assnode->memloc == xformloc && assnode->x_size != 0)
        {
            assnode->Name();
        }
        else if (assnode->d_size == 0 || assnode->x_size == 0)
        {
            assnode->memloc = NULL;
            assnode->loadflag |= 0x200000;
        }
        else
        {
            assnode->memloc = xformloc;
        }
    }
}

static S32 PKR_LoadLayer(st_PACKER_READ_DATA* pr, en_LAYER_TYPE layer)
{
    return 0;
}

static void* PKR_LoadAsset(st_PACKER_READ_DATA* pr, U32 aid, const char*, void*, S32* assetsize)
{
    return PKR_FindAsset(pr, aid, assetsize);
}

static U32 PKR_GetAssetSize(st_PACKER_READ_DATA* pr, U32 aid)
{
    S32 idx;
    st_PACKER_ATOC_NODE* assnode = NULL;

    idx = XOrdLookup(&pr->asstoc, (void*)aid, OrdTest_R_AssetID);

    if (idx > -1)
    {
        assnode = (st_PACKER_ATOC_NODE*)pr->asstoc.list[idx];
    }

    if (assnode != NULL)
    {
        if (assnode->x_size > 0)
        {
            return assnode->x_size;
        }
        return assnode->d_size;
    }
    return 0;
}

static S32 PKR_AssetCount(st_PACKER_READ_DATA* pr, U32 type)
{
    S32 cnt = 0;

    if (type == 0)
    {
        return pr->asstoc.cnt;
    }

    S32 typeidx = PKR_typeHdlr_idx(pr, type);
    if (typeidx >= 0)
    {
        st_XORDEREDARRAY* typlist = &pr->typelist[typeidx];
        cnt = typlist->cnt;
    }
    return cnt;
}

static void* PKR_AssetByType(st_PACKER_READ_DATA* pr, U32 type, S32 idx, U32* size)
{
    st_XORDEREDARRAY* typlist;
    st_PACKER_ATOC_NODE* assnode;

    if (size != NULL)
    {
        *size = 0;
    }

    if (idx < 0)
    {
        idx = 0;
    }

    S32 typeidx = PKR_typeHdlr_idx(pr, type);
    if (typeidx < 0)
    {
        return NULL;
    }

    typlist = &pr->typelist[typeidx];
    if (idx >= typlist->cnt)
    {
        return NULL;
    }

    assnode = (st_PACKER_ATOC_NODE*)typlist->list[idx];
    if (size != NULL)
    {
        *size = assnode->d_size;
    }
    return assnode->memloc;
}

static S32 PKR_IsAssetReady(st_PACKER_READ_DATA* pr, U32 aid)
{
    S32 is_ok = false;
    S32 idx = XOrdLookup(&pr->asstoc, (void*)aid, OrdTest_R_AssetID);
    if (idx >= 0)
    {
        st_PACKER_ATOC_NODE* assnode = (st_PACKER_ATOC_NODE*)pr->asstoc.list[idx];
        if (assnode->loadflag & 0x80000)
        {
            is_ok = true;
        }
        else
        {
            is_ok = false;
        }
    }

    return is_ok;
}

static U32 PKR_getPackTimestamp(st_PACKER_READ_DATA* pr)
{
    return pr->time_made;
}

static void PKR_Disconnect(st_PACKER_READ_DATA* pr)
{
    if (pr->pkg != NULL)
    {
        g_hiprf->destroy(pr->pkg);
        pr->pkg = NULL;
    }
}

U32 PKRAssetIDFromInst(void* asset_inst)
{
    return ((st_PACKER_ATOC_NODE*)asset_inst)->aid;
}

static char* PKR_AssetName(st_PACKER_READ_DATA* pr, U32 aid)
{
    char* da_name = NULL;
    S32 idx;

    if (aid == 0)
    {
        return NULL;
    }

    idx = XOrdLookup(&pr->asstoc, (void*)aid, OrdTest_R_AssetID);
    if (idx >= 0)
    {
        da_name = ((st_PACKER_ATOC_NODE*)pr->asstoc.list[idx])->Name();
    }

    return da_name;
}

static U32 PKR_GetBaseSector(st_PACKER_READ_DATA* pr)
{
    return pr->base_sector;
}

static S32 PKR_GetAssetInfo(st_PACKER_READ_DATA* pr, U32 aid, PKRAssetTOCInfo* tocinfo)
{
    S32 idx;
    st_PACKER_ATOC_NODE* assnode;

    memset(tocinfo, 0, sizeof(PKRAssetTOCInfo));
    idx = XOrdLookup(&pr->asstoc, (void*)aid, OrdTest_R_AssetID);
    if (idx < 0)
    {
        return 0;
    }

    assnode = (st_PACKER_ATOC_NODE*)pr->asstoc.list[idx];
    tocinfo->aid = aid;
    tocinfo->typeref = assnode->typeref;
    tocinfo->sector = assnode->d_off / pkr_sector_size;
    tocinfo->plus_offset = assnode->d_off % pkr_sector_size;
    tocinfo->size = assnode->d_size;
    tocinfo->mempos = assnode->memloc;

    return 1;
}

static S32 PKR_GetAssetInfoByType(st_PACKER_READ_DATA* pr, U32 type, S32 idx,
                                  PKRAssetTOCInfo* tocinfo)
{
    st_PACKER_ATOC_NODE* assnode;
    st_XORDEREDARRAY* typlist;

    memset(tocinfo, 0, sizeof(PKRAssetTOCInfo));
    if (idx < 0)
    {
        idx = 0;
    }

    S32 typeidx = PKR_typeHdlr_idx(pr, type);
    if (typeidx < 0)
    {
        return 0;
    }

    typlist = &pr->typelist[typeidx];
    if (idx >= typlist->cnt)
    {
        return 0;
    }

    assnode = (st_PACKER_ATOC_NODE*)typlist->list[idx];
    tocinfo->aid = assnode->aid;
    tocinfo->typeref = assnode->typeref;
    tocinfo->sector = assnode->d_off / pkr_sector_size;
    tocinfo->plus_offset = assnode->d_off % pkr_sector_size;
    tocinfo->size = assnode->d_size;
    tocinfo->mempos = assnode->memloc;

    return 1;
}

static S32 PKR_PkgHasAsset(st_PACKER_READ_DATA* pr, U32 aid)
{
    S32 rc;
    S32 idx = XOrdLookup(&pr->asstoc, (void*)aid, OrdTest_R_AssetID);
    if (idx < 0)
    {
        return 0;
    }

    rc = 1;
    st_PACKER_ATOC_NODE* assnode = (st_PACKER_ATOC_NODE*)pr->asstoc.list[idx];
    if (assnode->loadflag & 0x100000)
    {
        rc = 0;
    }
    else if (assnode->loadflag & 0x200000)
    {
        rc = 0;
    }
    return rc;
}

static S32 PKR_FRIEND_assetIsGameDup(U32 aid, const st_PACKER_READ_DATA* skippr, S32 oursize,
                                     U32 ourtype, U32 chksum, char*)
{
    S32 is_dup = 0;
    st_PACKER_ATOC_NODE* tmp_ass;
    S32 i;
    S32 idx;

    if (aid == 0x98a3f56c)
    {
        return 0;
    }

    for (i = 0; i < 20; i++)
    {
        if ((g_loadlock & 1 << i) == 0 || &g_readdatainst[i] == skippr)
        {
            continue;
        }

        idx = XOrdLookup(&g_readdatainst[i].asstoc, (void*)aid, OrdTest_R_AssetID);
        if (idx < 0)
        {
            continue;
        }

        tmp_ass = (st_PACKER_ATOC_NODE*)g_readdatainst[i].asstoc.list[idx];
        if ((tmp_ass->loadflag & 0x80000) == 0 && tmp_ass->asstype != 0x534e4420 &&
            tmp_ass->asstype != 0x534e4453)
        {
            continue;
        }

        tmp_ass->Name();
        xUtil_idtag2string(tmp_ass->asstype, 0);

        if (ourtype != 0 && ourtype != tmp_ass->asstype)
        {
            tmp_ass->Name();
            xUtil_idtag2string(tmp_ass->asstype, 0);
            xUtil_idtag2string(ourtype, 1);
        }
        if (oursize >= 0 && oursize != tmp_ass->d_size)
        {
            tmp_ass->Name();
            xUtil_idtag2string(tmp_ass->asstype, 0);
        }
        if (chksum != 0 && chksum != tmp_ass->d_chksum)
        {
            tmp_ass->Name();
            xUtil_idtag2string(tmp_ass->asstype, 0);
        }
        is_dup = 1;
        break;
    }

    return is_dup;
}

static S32 OrdComp_R_Asset(void* vkey, void* vitem)
{
    S32 rc;
    if (*(U32*)vkey < *(U32*)vitem)
    {
        rc = -1;
    }
    else if (*(U32*)vkey > *(U32*)vitem)
    {
        rc = 1;
    }
    else
    {
        rc = 0;
    }
    return rc;
}

static S32 OrdTest_R_AssetID(const void* vkey, void* vitem)
{
    S32 rc;
    if ((U32)(UPtr)vkey < *(U32*)vitem)
    {
        rc = -1;
    }
    else if ((U32)(UPtr)vkey > *(U32*)vitem)
    {
        rc = 1;
    }
    else
    {
        rc = 0;
    }
    return rc;
}

// result and the dead 'cid = 0' are Ratatouille's. With the placeholder (a check whose final
// form is unknown) they keep this over the inlining limit, as retail calls it from PKR_parse_TOC.
static S32 LOD_r_PACK(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    S32 result = 1;
    U32 cid = 0;

    cid = g_hiprf->enter(pkg);
    while (cid != 0)
    {
        switch (cid)
        {
        case 'PVER':
            LOD_r_PVER(pkg, pr);
            break;
        case 'PFLG':
            LOD_r_PFLG(pkg, pr);
            break;
        case 'PCNT':
            LOD_r_PCNT(pkg, pr);
            break;
        case 'PCRT':
            LOD_r_PCRT(pkg, pr);
            break;
        case 'PMOD':
            LOD_r_PMOD(pkg, pr);
            break;
        case 'PLAT':
            LOD_r_PLAT(pkg, pr);
            break;
        default:
            xASSERT_PLACEHOLDER();
            break;
        }
        g_hiprf->exit(pkg);
        cid = g_hiprf->enter(pkg);
    }
    return result;
}

static S32 LOD_r_AHDR(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    U32 cid;
    S32 ival = 0;
    S32 isdup;
    st_PACKER_ATOC_NODE* assnode;

    g_hiprf->readLongs(pkg, &ival, 1);
    assnode = PKR_newassnode(pr, ival);
    assnode->ownpr = pr;
    assnode->ownpkg = pkg;
    XOrdAppend(&pr->asstoc, assnode);

    g_hiprf->readLongs(pkg, &ival, 1);
    assnode->asstype = ival;
    assnode->typeref = PKR_type2typeref(assnode->asstype, pr->types);

    g_hiprf->readLongs(pkg, &ival, 1);
    assnode->d_off = ival;

    g_hiprf->readLongs(pkg, &ival, 1);
    assnode->d_size = ival;
    assnode->readrem = ival;
    assnode->readcnt = 0;
    if (assnode->d_size < 1)
    {
        assnode->loadflag |= 0x200000;
    }

    g_hiprf->readLongs(pkg, &ival, 1);
    assnode->d_pad = ival;

    g_hiprf->readLongs(pkg, &ival, 1);
    assnode->infoflag = ival;

    cid = g_hiprf->enter(pkg);
    while (cid != 0)
    {
        switch (cid)
        {
        case 'ADBG':
            LOD_r_ADBG(pkg, pr, assnode);
        }
        g_hiprf->exit(pkg);
        cid = g_hiprf->enter(pkg);
    }

    isdup = PKR_FRIEND_assetIsGameDup(assnode->aid, pr, assnode->d_size, assnode->asstype,
                                      assnode->d_chksum, NULL);
    if (isdup)
    {
        assnode->loadflag |= 0x100000;
    }
    return 1;
}

// Original source position: its tmpbuf initializer comes after LOD_r_PACK's in retail .rodata.
static inline S32 LOD_r_ADBG(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr,
                             st_PACKER_ATOC_NODE* assnode)
{
    S32 ival = 0;
    char tmpbuf[256] = {};

    g_hiprf->readLongs(pkg, &ival, 1);
    assnode->assalign = ival;

    g_hiprf->readString(pkg, tmpbuf);
    strncpy(assnode->name, tmpbuf, sizeof(assnode->name) - 1);
    assnode->name[sizeof(assnode->name) - 1] = 0;
    tmpbuf[0] = 0;

    g_hiprf->readString(pkg, tmpbuf);

    if (pr->subver > 1)
    {
        g_hiprf->readLongs(pkg, &ival, 1);
        assnode->d_chksum = ival;
    }

    return 1;
}

// result is Ratatouille's local; its initializer keeps this over the inlining limit, as retail
// calls it from PKR_parse_TOC.
static S32 LOD_r_LHDR(st_HIPLOADDATA* pkg, st_PACKER_READ_DATA* pr)
{
    S32 result = 1;
    U32 cid;
    S32 ival = 0;
    en_LAYER_TYPE laytyp;
    S32 refcnt = 0;
    S32 idx;
    S32 i;
    st_PACKER_LTOC_NODE* laynode;
    st_PACKER_ATOC_NODE* assnode;

    g_hiprf->readLongs(pkg, &ival, 1);
    laytyp = (en_LAYER_TYPE)ival;
    g_hiprf->readLongs(pkg, &refcnt, 1);
    laynode = PKR_newlaynode(laytyp, refcnt);
    XOrdAppend(&pr->laytoc, laynode);

    for (i = 0; i < refcnt; i++)
    {
        g_hiprf->readLongs(pkg, &ival, 1);
        idx = XOrdLookup(&pr->asstoc, (void*)ival, OrdTest_R_AssetID);
        assnode = (st_PACKER_ATOC_NODE*)pr->asstoc.list[idx];
        XOrdAppend(&laynode->assref, assnode);

        if (i != refcnt - 1)
        {
            laynode->laysize += assnode->d_size + assnode->d_pad;
        }
        else
        {
            laynode->laysize += assnode->d_size;
        }
    }

    if (laynode->laysize > 0)
    {
        laynode->laysize = laynode->laysize + 0x7ff & 0xfffff800;
    }

    cid = g_hiprf->enter(pkg);
    while (cid != NULL)
    {
        switch (cid)
        {
        case 'LDBG':
            LOD_r_LDBG(pkg, pr, laynode);
            break;
        }
        g_hiprf->exit(pkg);
        cid = g_hiprf->enter(pkg);
    }
    return result;
}

static void PKR_bld_typecnt(st_PACKER_READ_DATA* pr)
{
    st_PACKER_LTOC_NODE* laynode = NULL;
    st_PACKER_ATOC_NODE* assnode;
    S32 i;
    S32 j;
    S32 idx;
    S32 typcnt[129] = {};
    st_XORDEREDARRAY* tmplist;
    U32 lasttype = 0;
    S32 lasttidx = 0;

    for (j = 0; j < pr->laytoc.cnt; j++)
    {
        laynode = (st_PACKER_LTOC_NODE*)pr->laytoc.list[j];
        for (i = 0; i < laynode->assref.cnt; i++)
        {
            assnode = (st_PACKER_ATOC_NODE*)laynode->assref.list[i];
            if (!(assnode->loadflag & 0x100000) && !(assnode->loadflag & 0x200000))
            {
                if (lasttype != 0 && assnode->asstype == lasttype)
                {
                    idx = lasttidx;
                }
                else
                {
                    idx = PKR_typeHdlr_idx(pr, assnode->asstype);
                    lasttidx = idx;
                    lasttype = assnode->asstype;
                }

                if (idx < 0)
                {
                    xUtil_idtag2string(assnode->asstype, 0);
                    assnode->Name();
                    typcnt[128]++;
                }
                else
                {
                    typcnt[idx]++;
                }
            }
        }
    }

    for (i = 0; i < 129; i++)
    {
        if (typcnt[i] >= 1)
        {
            tmplist = &pr->typelist[i];
            XOrdInit(tmplist, typcnt[i] > 1 ? typcnt[i] : 2, false);
        }
    }

    for (i = 0; i < pr->laytoc.cnt; i++)
    {
        laynode = (st_PACKER_LTOC_NODE*)pr->laytoc.list[i];
        for (j = 0; j < laynode->assref.cnt; j++)
        {
            assnode = (st_PACKER_ATOC_NODE*)laynode->assref.list[j];
            if (!(assnode->loadflag & 0x100000) && !(assnode->loadflag & 0x200000))
            {
                if (lasttype != 0 && assnode->asstype == lasttype)
                {
                    idx = lasttidx;
                }
                else
                {
                    idx = PKR_typeHdlr_idx(pr, assnode->asstype);
                    lasttidx = idx;
                    lasttype = assnode->asstype;
                }

                if (idx < 0)
                {
                    tmplist = &pr->typelist[128];
                }
                else
                {
                    tmplist = &pr->typelist[idx];
                }
                XOrdAppend(tmplist, assnode);
            }
        }
    }
}

// The asserts and 'memptr = NULL' are Ratatouille's. With three placeholders (checks whose final
// form is unknown) they keep this over the inlining limit, as retail calls it from every caller.
static void* PKR_getmem(U32 id, S32 amount, U32 aid, S32 align, S32 isTemp, char** memtru)
{
    void* memptr = NULL;

    xASSERT(id);
    xASSERT(aid);
    xASSERT(amount > 0);
    if (amount == 0)
    {
        return memptr;
    }

    xASSERT(!isTemp || (isTemp && memtru));
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (isTemp)
    {
        memptr = RwMalloc(amount + align, 0);

        if (memtru != NULL)
        {
            *memtru = (char*)memptr;
        }

        if (align != 0)
        {
            // round memptr up to the next multiple of align (a power of two)
            memptr = (void*)(-(SPtr)align & (UPtr)((SPtr)memptr + align - 1));
        }
    }
    else
    {
        memptr = xMemAlloc(gActiveHeap, amount, align);
    }

    xASSERT(!(((SPtr)memptr) % align));
    xASSERT(memptr);

    if (memptr != NULL)
    {
        memset(memptr, 0, amount);
    }

    g_memalloc_pair++;
    g_memalloc_runtot += amount;
    if (g_memalloc_runtot < 0)
    {
        g_memalloc_runtot = amount;
    }

    if (memptr != NULL)
    {
        xUtil_idtag2string(id, 0);
    }
    else
    {
        xUtil_idtag2string(id, 0);
    }

    return memptr;
}
