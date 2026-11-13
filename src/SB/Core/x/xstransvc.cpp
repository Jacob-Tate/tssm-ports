#include "xstransvc.h"

extern char* apszRegionCode[eRegionCount];

static st_STRAN_DATA g_xstdata = {};
static S32 g_straninit;
static PKRReadFuncs* g_pkrf;
static PKRAssetType* g_typeHandlers;
static xRegion g_localEnum;
static char g_localCode[4] = "_US";

static inline void XST_reset_raw()
{
    memset(&g_xstdata, 0, sizeof(st_STRAN_DATA));
}

static inline st_STRAN_SCENE* XST_get_rawinst(S32 index)
{
    return &g_xstdata.hipscn[index];
}

static inline void XST_unlock(st_STRAN_SCENE* sdata)
{
    if (sdata != NULL)
    {
        if (g_xstdata.loadlock & 1U << sdata->lockid)
        {
            g_xstdata.loadlock &= ~(1U << sdata->lockid);
            memset(sdata, 0, sizeof(st_STRAN_SCENE));
        }
    }
}

static inline void XST_unlock_all()
{
    if (g_xstdata.loadlock)
    {
        for (S32 i = 0; i < 20; i++)
        {
            if (g_xstdata.loadlock & 1 << i)
            {
                XST_unlock(XST_get_rawinst(i));
            }
        }
    }
}

static inline S32 XST_cnt_locked()
{
    S32 sum = 0;
    for (int i = 0; i < 20; i++)
    {
        if (g_xstdata.loadlock & 1 << i)
        {
            sum++;
        }
    }
    return sum;
}

static inline st_STRAN_SCENE* XST_nth_locked(S32 index)
{
    st_STRAN_SCENE* sdata = NULL;
    S32 cnt = 0;
    for (S32 i = 0; i < 20; i++)
    {
        if (g_xstdata.loadlock & 1 << i)
        {
            if (cnt == index)
            {
                sdata = &g_xstdata.hipscn[i];
                break;
            }
            cnt += 1;
        }
    }

    return sdata;
}

static inline st_STRAN_SCENE* XST_find_bySID(U32 sid, S32 flagHipHop)
{
    st_STRAN_SCENE* da_sdata = NULL;
    S32 i;

    for (i = 0; i < 20; i++)
    {
        st_STRAN_SCENE* sdata = &g_xstdata.hipscn[i];
        if (g_xstdata.loadlock & 1 << i && sdata->scnid == sid &&
            (sdata->flgHipHop & 3) == (flagHipHop & 3))
        {
            da_sdata = sdata;
            break;
        }
    }
    return da_sdata;
}

static inline st_STRAN_SCENE* XST_lock_next()
{
    st_STRAN_SCENE* sdata = NULL;
    S32 i;
    S32 uselock = -1;

    for (i = 0; i < 20; i++)
    {
        if (!(g_xstdata.loadlock & 1 << i))
        {
            g_xstdata.loadlock |= 1 << i;
            sdata = &g_xstdata.hipscn[i];
            memset(sdata, 0, sizeof(st_STRAN_SCENE));
            uselock = i;
            break;
        }
    }

    if (sdata != NULL)
    {
        sdata->lockid = uselock;
    }

    return sdata;
}

static inline S32 XST_PreLoadScene(st_STRAN_SCENE* sdata, const char* path, S32 fileflags)
{
    S32 cltver = 0;
    sdata->spkg = g_pkrf->Init(sdata->userdata, path, 0x2e, &cltver, g_typeHandlers, fileflags);
    if (sdata->spkg != NULL)
    {
        return cltver;
    }
    return 0;
}

static inline char* XST_translate_sid(U32 sid, char* exten, bool local)
{
    static char fname[64];

    fname[0] = '\0';
    strcat(fname, xUtil_idtag2string(sid, 0));
    if (local)
    {
        strcat(fname, g_localCode);
    }
    strcat(fname, exten);
    return fname;
}

static inline char* XST_translate_sid_path(U32 sid, char* exten, bool local)
{
    char path_delimiter[2] = "/";
    char subdir[4];
    static char fname[64];

    fname[0] = '\0';
    subdir[0] = xUtil_idtag2string(sid, 0)[0];
    subdir[1] = xUtil_idtag2string(sid, 0)[1];
    subdir[2] = '\0';
    strcat(fname, subdir);
    strcat(fname, path_delimiter);
    strcat(fname, xUtil_idtag2string(sid, 0));
    if (local)
    {
        strcat(fname, g_localCode);
    }
    strcat(fname, exten);
    return fname;
}

S32 xSTStartup(PKRAssetType* handlers)
{
    if (g_straninit++ == 0)
    {
        g_typeHandlers = handlers;
        XST_reset_raw();
        PKRStartup();
        g_pkrf = PKRGetReadFuncs(1);
    }
    return g_straninit;
}

S32 xSTShutdown()
{
    g_straninit--;
    if (g_straninit == 0)
    {
        g_typeHandlers = NULL;
        XST_unlock_all();
        PKRShutdown();
    }
    return g_straninit;
}

S32 xSTPreLoadScene(U32 sid, void* userdata, S32 flg_hiphop, S32 fileflags, const char* filename)
{
    S32 result;
    st_STRAN_SCENE* sdata;
    char* sfile = NULL;
    S32 cltver;

    if (fileflags & 4)
    {
        sfile = (char*)filename;
    }

    cltver = 0;
    if ((flg_hiphop & 3) == 2)
    {
        sdata = XST_lock_next();
        sdata->scnid = sid;
        sdata->userdata = userdata;
        sdata->flgHipHop = flg_hiphop;

        if (sfile == NULL)
        {
            sfile = XST_translate_sid_path(sid, ".HOP", false);
        }
        if (sfile != NULL)
        {
            strcpy(sdata->fnam, sfile);
            if (iFileExists(sfile, fileflags))
            {
                cltver = XST_PreLoadScene(sdata, sfile, fileflags);
            }
        }

        if (cltver == 0)
        {
            sfile = XST_translate_sid(sid, ".HOP", false);
            if (sfile != NULL)
            {
                strcpy(sdata->fnam, sfile);
                if (iFileExists(sfile, fileflags))
                {
                    cltver = XST_PreLoadScene(sdata, sfile, fileflags);
                }
            }
        }

        if (cltver == 0)
        {
            XST_unlock(sdata);
            result = 0;
        }
        else
        {
            result = cltver;
        }
    }
    else
    {
        sdata = XST_lock_next();
        sdata->scnid = sid;
        sdata->userdata = userdata;
        sdata->flgHipHop = flg_hiphop;

        if (sid != 'BOOT' && sid != 'PLAT' && sid != 'DEMO' && sid != 'FONT')
        {
            if (sfile == NULL)
            {
                sfile = XST_translate_sid_path(sid, ".HIP", (flg_hiphop & 3) == 3);
            }
            if (sfile != NULL)
            {
                strcpy(sdata->fnam, sfile);
                if (iFileExists(sfile, fileflags))
                {
                    cltver = XST_PreLoadScene(sdata, sfile, fileflags);
                }
            }
        }

        if (cltver == 0)
        {
            sfile = XST_translate_sid(sid, ".HIP", (flg_hiphop & 3) == 3);
            if (sfile != NULL)
            {
                strcpy(sdata->fnam, sfile);
                if (iFileExists(sfile, fileflags))
                {
                    cltver = XST_PreLoadScene(sdata, sfile, fileflags);
                }
            }
        }

        if (cltver == 0)
        {
            XST_unlock(sdata);
            result = 0;
        }
        else
        {
            result = cltver;
        }
    }

    return result;
}

S32 xSTQueueSceneAssets(U32 sid, S32 flg_hiphop)
{
    S32 result = 1;
    st_STRAN_SCENE* sdata = XST_find_bySID(sid, flg_hiphop);
    if (sdata == NULL)
    {
        result = 0;
    }
    else
    {
        if (sdata->spkg != NULL)
        {
            g_pkrf->LoadLayer(sdata->spkg, PKR_LTYPE_ALL);
        }
    }
    return result;
}

void xSTUnLoadSceneMany(U32 sceneID, S32 type)
{
    if (type & 1)
    {
        st_STRAN_SCENE* sdata = XST_find_bySID(sceneID, 1);
        if (sdata != NULL)
        {
            if (sdata->spkg != NULL)
            {
                g_pkrf->Done(sdata->spkg);
            }
            sdata->spkg = NULL;
            XST_unlock(sdata);
        }
    }

    if (type & 4)
    {
        st_STRAN_SCENE* sdata = XST_find_bySID(sceneID, 3);
        if (sdata != NULL)
        {
            if (sdata->spkg != NULL)
            {
                g_pkrf->Done(sdata->spkg);
            }
            sdata->spkg = NULL;
            XST_unlock(sdata);
        }
    }

    if (type & 2)
    {
        st_STRAN_SCENE* sdata = XST_find_bySID(sceneID, 2);
        if (sdata != NULL)
        {
            if (sdata->spkg != NULL)
            {
                g_pkrf->Done(sdata->spkg);
            }
            sdata->spkg = NULL;
            XST_unlock(sdata);
        }
    }
}

F32 xSTLoadStep(U32)
{
    F32 pct = PKRLoadStep(0) != 0 ? 0.0f : 1.00001f;

    iTRCDisk::TRCCheck();
    iFileAsyncService();
    return pct;
}

void xSTDisconnect(U32 sid, S32 flg_hiphop)
{
    st_STRAN_SCENE* sdata = XST_find_bySID(sid, flg_hiphop);
    if (sdata != NULL)
    {
        g_pkrf->PkgDisconnect(sdata->spkg);
    }
}

S32 xSTSwitchScene(U32 sid, void* userdata, S32 (*progmon)(void*, F32))
{
    st_STRAN_SCENE* sdata;
    S32 rc = 0;
    S32 i;
    S32 types[3] = { 1, 2, 3 };

    for (i = 2; i >= 0; i--)
    {
        sdata = XST_find_bySID(sid, types[i]);
        if (sdata != NULL)
        {
            if (progmon != NULL)
            {
                progmon(userdata, 0.0f);
            }
            rc = g_pkrf->SetActive(sdata->spkg, PKR_LTYPE_ALL);
            if (progmon != NULL)
            {
                progmon(userdata, 1.0f);
            }
        }
    }

    return rc;
}

char* xSTAssetName(U32 aid)
{
    char* aname = NULL;

    S32 cnt = XST_cnt_locked();
    for (int i = 0; i < cnt; i++)
    {
        st_STRAN_SCENE* sdata = XST_nth_locked(i);
        if (sdata->spkg != NULL)
        {
            aname = g_pkrf->AssetName(sdata->spkg, aid);
            if (aname != NULL)
            {
                break;
            }
        }
    }

    return aname;
}

char* xSTAssetName(void* raw_HIP_asset)
{
    char* aname = NULL;

    S32 cnt = XST_cnt_locked();
    for (int i = 0; i < cnt; i++)
    {
        st_STRAN_SCENE* sdata = XST_nth_locked(i);
        aname = g_pkrf->AssetName(sdata->spkg, PKRAssetIDFromInst(raw_HIP_asset));
        if (aname != NULL)
        {
            break;
        }
    }

    return aname;
}

void* xSTFindAsset(U32 aid, U32* size)
{
    void* memloc;
    S32 i;
    S32 asssize;

    if (aid == 0)
    {
        return NULL;
    }

    memloc = NULL;

    S32 found = XST_cnt_locked();
    for (i = 0; i < found; i++)
    {
        st_STRAN_SCENE* sdata = XST_nth_locked(i);
        asssize = 0;
        memloc = g_pkrf->LoadAsset(sdata->spkg, aid, NULL, NULL, &asssize);
        if (memloc != NULL)
        {
            if (size)
            {
                *size = asssize;
            }
            break;
        }
    }
    return memloc;
}

S32 xSTAssetCountByType(U32 type)
{
    S32 sum = 0;
    S32 cnt = XST_cnt_locked();
    for (S32 i = 0; i < cnt; i++)
    {
        st_STRAN_SCENE* sdata = XST_nth_locked(i);
        sum += g_pkrf->AssetCount(sdata->spkg, type);
    }
    return sum;
}

void* xSTFindAssetByType(U32 type, S32 idx, U32* size)
{
    void* memptr = NULL;
    S32 i;
    S32 sum = 0;
    S32 cnt = XST_cnt_locked();
    for (i = 0; i < cnt; i++)
    {
        st_STRAN_SCENE* sdata = XST_nth_locked(i);
        S32 scncnt = g_pkrf->AssetCount(sdata->spkg, type);
        if (idx >= sum && idx < sum + scncnt)
        {
            memptr = g_pkrf->AssetByType(sdata->spkg, type, idx - sum, size);
            break;
        }

        sum += scncnt;
    }
    return memptr;
}

// rc is declared first, as in BfBB: retail allocates it before i.
S32 xSTGetAssetInfo(U32 aid, PKRAssetTOCInfo* tocainfo)
{
    S32 rc = 0;
    S32 found;
    S32 i;

    found = XST_cnt_locked();
    for (i = 0; i < found; i++)
    {
        st_STRAN_SCENE* sdata = XST_nth_locked(i);
        if (g_pkrf->GetAssetInfo(sdata->spkg, aid, tocainfo))
        {
            rc = 1;
            break;
        }
    }
    return rc;
}

S32 xSTGetAssetInfoByType(U32 type, S32 idx, PKRAssetTOCInfo* tocainfo)
{
    S32 rc = 0;
    S32 sum = 0;
    S32 found;
    S32 i;
    S32 cnt;

    found = XST_cnt_locked();
    for (i = 0; i < found; i++)
    {
        st_STRAN_SCENE* sdata = XST_nth_locked(i);
        cnt = g_pkrf->AssetCount(sdata->spkg, type);
        if (idx >= sum && idx < sum + cnt)
        {
            if (g_pkrf->GetAssetInfoByType(sdata->spkg, type, idx - sum, tocainfo))
            {
                rc = 1;
                break;
            }
        }
        sum += cnt;
    }

    return rc;
}

char* xST_xAssetID_HIPFullPath(U32 aid)
{
    return xST_xAssetID_HIPFullPath(aid, NULL);
}

inline char* xST_xAssetID_HIPFullPath(U32 aid, U32* sceneID)
{
    char* da_hipname = NULL;
    S32 rc;
    S32 i;

    rc = XST_cnt_locked();
    for (i = 0; i < rc; i++)
    {
        st_STRAN_SCENE* sdata = XST_nth_locked(i);
        if (g_pkrf->PkgHasAsset(sdata->spkg, aid))
        {
            da_hipname = sdata->fnam;
            if (sceneID != NULL)
            {
                *sceneID = sdata->scnid;
            }
            break;
        }
    }

    return da_hipname;
}

// Placeholder for dead-stripped code (probably xSTReportLoadTimes, a scene load-time report) whose
// two format strings remain, unreferenced, in retail's string pool before xSTPreLoadScene's ".HOP".
static const char* __deadstripped_xstransvc()
{
    return "%-30s %1.2f ms cls=%2d %1.2f%\n\0"
           "%-30s %1.2f ms cls=%2d \n";
}

void xSTSetLocalizationCode(const char* code)
{
    g_localCode[0] = '_';
    strcpy(&g_localCode[1], code);

    for (xRegion iRegion = eRegionUS; iRegion < eRegionCount; iRegion = (xRegion)(iRegion + 1))
    {
        if (stricmp(apszRegionCode[iRegion], code) == 0)
        {
            g_localEnum = iRegion;
            break;
        }
    }
}

char* xSTGetLocalizationCode()
{
    return g_localCode;
}

xRegion xSTGetLocalizationEnum()
{
    return g_localEnum;
}
