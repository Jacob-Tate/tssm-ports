#include "isavegame.h"
#include "xDebug.h"
#include "zGame.h"
#include "xstransvc.h"

// Rounds n up to a multiple of x (x a power of two).
#define ALIGN_THING(x, n) (n + x - 1 & -x)

// TPL (Dolphin texture palette file) structures; names and layouts from the Ratatouille GC DWARF.
struct st_ISG_TPL_TEXHEADER
{
    U16 height;
    U16 width;
    U32 format;
    char* data;
    GXTexWrapMode wrapS;
    GXTexWrapMode wrapT;
    GXTexFilter minFilter;
    GXTexFilter magFilter;
    F32 LODBias;
    U8 edgeLODEnable;
    U8 minLOD;
    U8 maxLOD;
    U8 unpacked;
};

struct st_ISG_TPL_CLUTHEADER
{
    U16 numEntries;
    U8 unpacked;
    U8 pad8;
    GXTlutFmt format;
    char* data;
};

struct st_ISG_TPL_TEXDESCRIP
{
    st_ISG_TPL_TEXHEADER* textureHeader;
    st_ISG_TPL_CLUTHEADER* CLUTHeader;
};

struct st_ISG_TPL_TEXPALETTE
{
    U32 versionNumber;
    U32 numDescriptors;
    st_ISG_TPL_TEXDESCRIP* descriptorArray;
};

struct IconData
{
    char game_name[0x20];
    char buf0[0x20];
    char buf1[0x1800];
    char buf2[1][0x800]; // TSSM has a single icon frame (0x2440-byte IconData)
    char pad[0x200];
    char footer[0x200];
};

extern _tagGameVidMode gVidMode;

static st_ISGSESSION g_isgdata_MAIN = { 0 };
static S32 g_isginit;
static S32 g_legalSectSize[] = { 0x2000, 0, -1 };
static st_ISG_TPL_TEXPALETTE* g_rawicon;
static st_ISG_TPL_TEXPALETTE* g_rawbanr;
static U32 g_iconsize;
static U32 g_banrsize;
static U8 cardwork[ISG_NUM_SLOTS][CARD_WORKAREA_SIZE] ATTRIBUTE_ALIGN(32);
static U8 isMounted;
static const char* ISG_GC_GAMENAME = "GGVE";

static inline S32 iSG_start_your_engines();
static inline S32 iSG_mc_exists(S32 slot);
static inline S32 iSG_mc_isformatted(st_ISG_MEMCARD_DATA* mcdata);
void CheckExCallback(s32 chan, s32 result);
static S32 iSG_mc_isGCcard(st_ISG_MEMCARD_DATA* mcdata, int* param2, int* param3);
static S32 iSG_isSpaceForFile(st_ISG_MEMCARD_DATA* mcdata, S32 fsize, const char* fname,
                              S32* amtneed, S32* amtfree, S32* needFiles);
static inline S32 iSG_mc_settgt(st_ISG_MEMCARD_DATA* mcdata, S32 slot);
static inline S32 iSG_get_finfo(st_ISG_MEMCARD_DATA* mcdata, const char* dpath);
static S32 iSG_curKosher(CARDStat* stat, CARDFileInfo* info);
static inline S32 iSG_get_fsize(st_ISG_MEMCARD_DATA* mcdata, const char* param2);
static inline S32 iSG_get_fmoddate(st_ISG_MEMCARD_DATA* mcdata, const char* fname, int* sec,
                                   int* min, int* hr, int* mon, int* day, int* yr);
static inline void iSG_timestamp(CARDStat*);
static inline S32 iSG_cubeicon_size(S32 slot, S32 sectsize);
static S32 iSG_load_icondata();
static char* iSG_bfr_icondata(char* param1, CARDStat* stat, char* param3, int param4);
static inline void iSG_upd_icostat(CARDStat*, CARDStat* stat);
static S32 iSG_tpl_unpack(st_ISG_TPL_TEXPALETTE* pal);
static S32 iSG_bnr_unpack(st_ISG_TPL_TEXPALETTE* pal);
static inline st_ISG_TPL_TEXDESCRIP* iSG_tpl_TEXGet(st_ISG_TPL_TEXPALETTE* pal, U32 idx);
static inline S32 iSG_mc_mount(S32 cardslot);
static void iSG_cb_unmount(s32 chan, s32 result);
static inline S32 iSG_mc_unmount(S32 slot);
static S32 iSG_mc_format(st_ISG_MEMCARD_DATA* mcdata, S32, S32* canRecover);
void CardCreateCallback(s32 chan, s32 result);
static S32 iSG_mc_fopen(st_ISG_MEMCARD_DATA* mcdata, const char* fname, S32 fsize,
                        en_ISG_IOMODE mode, en_ASYNC_OPERR* operr);
static inline S32 iSG_mc_fclose(st_ISG_MEMCARD_DATA* mcdata, CARDStat* stat);
static inline S32 iSG_mc_fclose(st_ISG_MEMCARD_DATA* mcdata);
void CardDeleteCallback(s32 chan, s32 result);
static S32 iSG_mc_fdel(st_ISG_MEMCARD_DATA* mcdata, const char* fname);
static inline S32 iSG_mc_fread(st_ISG_MEMCARD_DATA* mcdata, char* buf, S32 len, S32 offset);
static S32 iSG_mcqa_fread(st_ISG_MEMCARD_DATA* mcdata, char* buf, S32 bufsize, S32 skipbyte);
static inline S32 iSG_mc_fwrite(st_ISG_MEMCARD_DATA* mcdata, char* buf, S32 len);
static S32 iSG_mcqa_fwrite(st_ISG_MEMCARD_DATA* mcdata, char* buf, S32 len);

S32 iSGStartup()
{
    if (g_isginit++ != 0)
    {
        return g_isginit;
    }

    iSG_start_your_engines();
    iSG_load_icondata();
    return g_isginit;
}

S32 iSGShutdown()
{
    OSFreeToHeap(__OSCurrHeap, g_rawicon);
    OSFreeToHeap(__OSCurrHeap, g_rawbanr);
    g_rawicon = NULL;
    g_iconsize = 0;
    g_rawbanr = NULL;
    g_banrsize = 0;
    return 1;
}

char* iSGMakeName(en_NAMEGEN_TYPE type, const char* base, S32 idx)
{
    static char rotatebuf[8][32] = { 0 };
    static S32 rotate = 0;

    char* use_buf;
    const char* fmt_sd = "%s%02d";

    use_buf = rotatebuf[rotate++];
    if (rotate == 8)
    {
        rotate = 0;
    }

    *use_buf = '\0';
    switch (type)
    {
    case ISG_NGTYP_GAMEFILE:
        // save slots are numbered from 1 in the file name
        if (base != NULL)
        {
            sprintf(use_buf, fmt_sd, base, idx + 1);
        }
        else
        {
            sprintf(use_buf, fmt_sd, "SpongeBobMovie", idx + 1);
        }
        break;
    case ISG_NGTYP_GAMEDIR:
    case ISG_NGTYP_CONFIG:
    case ISG_NGTYP_ICONTHUM:
        break;
    }

    return use_buf;
}

// Dead-stripped placeholders for 10 memory-card strings that retail's string pool keeps with no
// reference. They belong to two stripped functions (iSGTgtDispName and iSGStateError in
// Ratatouille's erased DWARF) whose bodies are unknown.
static const char* __deadstripped_isavegame()
{
    return "Slot %c\0"
           "Memory Card Slot %c\0"
           "Nintendo GameCube Memory Card in Slot %c\0"
           "Nintendo GameCube%s Memory Card in Slot %c\0"
           "^\0"
           "Nintendo GameCube%s Memory Card";
}

static const char* __deadstripped_isavegame2()
{
    static const char* reasons[] = { "is damaged and cannot be used", "unsupprted sector size",
                                     "is formatted for another market" };

    return "encountered unexpected error (%d)";
}

st_ISGSESSION* iSGSessionBegin(void* cltdata, void (*chgfunc)(void*, en_CHGCODE), S32 monitor)
{
    iTRCDisk::TRCCheck();
    memset(&g_isgdata_MAIN, 0, sizeof(st_ISGSESSION));

    g_isgdata_MAIN.slot = -1;
    g_isgdata_MAIN.chgfunc = chgfunc;
    g_isgdata_MAIN.cltdata = cltdata;

    return &g_isgdata_MAIN;
}

void iSGSessionEnd(st_ISGSESSION* isgdata)
{
    iTRCDisk::TRCCheck();
    for (S32 i = 0; i < ISG_NUM_SLOTS; i++)
    {
        if (isgdata->mcdata[i].ismounted)
        {
            iSG_mc_unmount(i);
            isgdata->mcdata[i].ismounted = 0;
        }
    }

    memset(isgdata, 0, sizeof(st_ISGSESSION));
}

S32 iSGTgtCount(st_ISGSESSION* isgdata, S32* max)
{
    s32 memSize = 0;
    s32 sectorSize = 0;
    S32 ret = 0;

    if (max != NULL)
    {
        *max = 2;
    }

    for (S32 i = 0; i < ISG_NUM_SLOTS; ++i)
    {
        iTRCDisk::TRCCheck();
        s32 result;
        do
        {
            result = CARDProbeEx(i, &memSize, &sectorSize);
        } while (result == CARD_RESULT_BUSY);
        if (result == CARD_RESULT_READY)
        {
            ret++;
        }
    }

    return ret;
}

S32 iSGTgtPhysSlotIdx(st_ISGSESSION* isgdata, S32 tidx)
{
    S32 idx = -1;
    if (iSG_mcidx2slot(tidx, &idx, NULL))
    {
        return idx;
    }
    return -1;
}

S32 iSGTgtFormat(st_ISGSESSION* isgdata, S32 tgtidx, S32 async, S32* canRecover)
{
    S32 slot = 0;
    S32 rc = 0;
    iTRCDisk::TRCCheck();
    if (iSG_mc_exists(isgdata->slot) == 0)
    {
        return 0;
    }

    iSG_mcidx2slot(tgtidx, &slot, NULL);
    if (slot != isgdata->slot)
    {
        return 0;
    }

    st_ISG_MEMCARD_DATA* data = &isgdata->mcdata[isgdata->slot];
    if (data->ismounted == 0)
    {
        return 0;
    }

    if (iSG_mc_isGCcard(data, NULL, NULL) == 0)
    {
        return 0;
    }

    rc = iSG_mc_format(data, 0, canRecover);
    if (rc == -1)
    {
        return -1;
    }
    return rc == 0 ? 0 : 1;
}

U32 iSGTgtState(st_ISGSESSION* isgdata, S32 tgtidx, const char* dpath)
{
    U32 state = 0;
    S32 isSectSizeValid = 0;

    S32 rc = 0;

    S32 slot = 0;
    S32 x = 0;
    S32 y = 0;
    iTRCDisk::TRCCheck();

    iSG_mcidx2slot(tgtidx, &slot, NULL);
    if (slot != isgdata->slot)
    {
        iSGTgtSetActive(isgdata, tgtidx);
    }

    if (isgdata->slot < 0)
    {
        return 0x1000000;
    }

    st_ISG_MEMCARD_DATA* data = &isgdata->mcdata[isgdata->slot];
    if (data->ismounted == 0)
    {
        return 0;
    }

    if (iSG_mc_exists(isgdata->slot))
    {
        state |= 1;
    }
    else
    {
        return 0;
    }

    if (data->cardIsIll != 0)
    {
        return state | 0x1000000;
    }

    rc = iSG_mc_isGCcard(data, &x, &y);
    if (x != 0)
    {
        state |= 0x4000000;
    }

    if (y != 0)
    {
        state |= 0x2000000;
    }

    if (rc == 0)
    {
        return state;
    }

    if (g_legalSectSize[0] > 0)
    {
        for (S32 i = 0; g_legalSectSize[i] > 0; i++)
        {
            if (data->sectorSize == g_legalSectSize[i])
            {
                isSectSizeValid = 1;
                break;
            }
        }
        if (isSectSizeValid == 0)
        {
            state |= 0x8000004;
            return state & ~4;
        }
    }

    if (iSG_mc_isformatted(data) == 0)
    {
        return state | 4;
    }
    else
    {
        return state | 0xe;
    }

    return state;
}

S32 iSGTgtSetActive(st_ISGSESSION* isgdata, S32 tgtidx)
{
    S32 ret = 0;
    S32 slot = 0;

    iSG_mcidx2slot(tgtidx, &slot, NULL);
    st_ISG_MEMCARD_DATA* data = &isgdata->mcdata[slot];
    iTRCDisk::TRCCheck();

    if (data->ismounted)
    {
        ret = 1;
    }
    else if (iSG_mc_settgt(data, slot) != 0)
    {
        ret = 1;
        isgdata->slot = slot;
    }

    return ret;
}

S32 iSGTgtHaveRoom(st_ISGSESSION* isgdata, S32 tidx, S32 fsize, const char* dpath,
                   const char* fname, S32* bytesNeeded, S32* availOnDisk, S32* needFile)
{
    st_ISG_MEMCARD_DATA* data;
    S32 i;
    S32 count;
    S32 opened;
    S32 is_space;
    if (isgdata->slot < 0)
    {
        return 0;
    }
    data = &isgdata->mcdata[isgdata->slot];
    if (data->ismounted == 0)
    {
        return 0;
    }
    count = 0;
    if (fname == NULL)
    {
        for (i = 0; i < ISG_NUM_FILES; ++i)
        {
            iTRCDisk::TRCCheck();
            fname = iSGMakeName(ISG_NGTYP_GAMEFILE, NULL, i);
            if (iSG_get_finfo(data, fname) != 0)
            {
                count++;
            }
        }
    }

    is_space = iSG_isSpaceForFile(data, fsize, fname, bytesNeeded, availOnDisk, needFile);
    if (count > 0 && *bytesNeeded > *availOnDisk)
    {
        if (needFile != NULL && *bytesNeeded > *availOnDisk)
        {
            *needFile = 0;
            return 0;
        }
        return 1;
    }

    CARDFileInfo fileInfo;
    opened = 0;
    for (i = 0; i < CARD_MAX_FILE && isgdata->slot >= 0 && isgdata->slot < ISG_NUM_SLOTS; ++i)
    {
        if (CARDFastOpen(isgdata->slot, i, &fileInfo) == 0)
        {
            opened++;
            CARDClose(&fileInfo);
        }
    }

    if (opened >= CARD_MAX_FILE)
    {
        if (count > 0)
        {
            if (needFile != NULL)
            {
                *needFile = 0;
            }
            return 1;
        }
        return 0;
    }

    return (*bytesNeeded > *availOnDisk) ? 0 : is_space;
}

S32 iSGTgtHaveRoomStartup(st_ISGSESSION* isgdata, S32 tidx, S32 fsize, const char* dpath,
                          const char* fname, S32* bytesNeeded, S32* availOnDisk, S32* needFile)
{
    st_ISG_MEMCARD_DATA* data;
    S32 i;
    S32 count;
    S32 opened;
    S32 is_space;
    if (isgdata->slot < 0)
    {
        return 0;
    }
    data = &isgdata->mcdata[isgdata->slot];
    if (data->ismounted == 0)
    {
        return 0;
    }
    count = 0;
    if (fname == NULL)
    {
        for (i = 0; i < ISG_NUM_FILES; ++i)
        {
            iTRCDisk::TRCCheck();
            fname = iSGMakeName(ISG_NGTYP_GAMEFILE, NULL, i);
            if (iSG_get_finfo(data, fname) != 0)
            {
                count++;
            }
        }
    }

    is_space = iSG_isSpaceForFile(data, fsize, fname, bytesNeeded, availOnDisk, needFile);
    if (count > 0 && *bytesNeeded > *availOnDisk)
    {
        if (needFile != NULL && *bytesNeeded > *availOnDisk)
        {
            *needFile = 0;
            return 0;
        }
        return 1;
    }

    CARDFileInfo fileInfo;
    opened = 0;
    for (i = 0; i < CARD_MAX_FILE && isgdata->slot >= 0 && isgdata->slot < ISG_NUM_SLOTS; ++i)
    {
        if (CARDFastOpen(isgdata->slot, i, &fileInfo) == 0)
        {
            opened++;
            CARDClose(&fileInfo);
        }
    }

    if (opened >= CARD_MAX_FILE)
    {
        if (count > 0)
        {
            if (needFile != NULL)
            {
                *needFile = 0;
            }
            return 0;
        }
        *needFile = -1;
        return 0;
    }

    if (*bytesNeeded > *availOnDisk)
    {
        return 0;
    }
    return (count >= 3) ? 0 : is_space;
}

S32 iSGFileSize(st_ISGSESSION* isgdata, const char* fname)
{
    S32 ret = 0;
    if (isgdata->slot < 0)
    {
        return -1;
    }
    st_ISG_MEMCARD_DATA* data = &isgdata->mcdata[isgdata->slot];
    iTRCDisk::TRCCheck();
    ret = iSG_get_fsize(data, fname);
    if (ret >= 0)
    {
        ret -= iSG_cubeicon_size(data->chan, data->sectorSize);
        if (ret < 0)
        {
            ret = -1;
        }
    }
    return ret;
}

char* iSGFileModDate(st_ISGSESSION* isgdata, const char* fname)
{
    return iSGFileModDate(isgdata, fname, NULL, NULL, NULL, NULL, NULL, NULL);
}

// The compiled-out checks keep GC/2.6 from auto-inlining this, as retail calls it. The validate is
// Ratatouille's; the other conditions are unknown.
char* iSGFileModDate(st_ISGSESSION* isgdata, const char* fname, S32* sec, S32* min, S32* hr,
                     S32* mon, S32* day, S32* yr)
{
    static char datestr[0x40] = { 0 };
    xVALIDATE(!(isgdata->slot < 0));
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    st_ISG_MEMCARD_DATA* mcdata = &isgdata->mcdata[isgdata->slot];
    int sec_str = 0;
    int min_str = 0;
    int hr_str = 0;
    int mon_str = 0;
    int day_str = 0;
    int yr_str = 0;
    if (iSG_get_fmoddate(mcdata, fname, &sec_str, &min_str, &hr_str, &mon_str, &day_str, &yr_str) !=
        0)
    {
        if (gVidMode == eGameVidModePAL)
        {
            sprintf(datestr, "%02d.%02d.%04d %02d:%02d:%02d", day_str, mon_str, yr_str, hr_str,
                    min_str, sec_str);
        }
        else
        {
            sprintf(datestr, "%02d/%02d/%04d %02d:%02d:%02d", mon_str, day_str, yr_str, hr_str,
                    min_str, sec_str);
        }

        if (sec != NULL)
        {
            *sec = sec_str;
        }
        if (min != NULL)
        {
            *min = min_str;
        }
        if (hr != NULL)
        {
            *hr = hr_str;
        }
        if (mon != NULL)
        {
            *mon = mon_str;
        }
        if (day != NULL)
        {
            *day = day_str;
        }
        if (yr != NULL)
        {
            *yr = yr_str;
        }
    }
    else
    {
        sprintf(datestr, "<Unknown Modification>");
    }
    return datestr;
}

void iSGMakeTimeStamp(char* str)
{
    OSCalendarTime calendar_time = { 0 };
    OSTicksToCalendarTime(OSGetTime(), &calendar_time);
    if (gVidMode == eGameVidModePAL)
    {
        sprintf(str, "%02d.%02d.%04d %02d:%02d:%02d", calendar_time.mday, calendar_time.mon + 1,
                calendar_time.year, calendar_time.hour, calendar_time.min, calendar_time.sec);
    }
    else
    {
        sprintf(str, "%02d/%02d/%04d %02d:%02d:%02d", calendar_time.mon + 1, calendar_time.mday,
                calendar_time.year, calendar_time.hour, calendar_time.min, calendar_time.sec);
    }
}

S32 iSGSelectGameDir(st_ISGSESSION* isgdata, const char* dname)
{
    if (isgdata->slot < 0)
    {
        return 0;
    }
    st_ISG_MEMCARD_DATA* data = &isgdata->mcdata[isgdata->slot];

    if (data->ismounted == 0)
    {
        return 0;
    }

    if (iSG_mc_isGCcard(data, NULL, NULL) == 0)
    {
        return 0;
    }

    if (iSG_mc_isformatted(data) == 0)
    {
        return 0;
    }

    S32 count = 0;
    for (S32 idx = 0; idx < ISG_NUM_FILES; ++idx)
    {
        const char* n = iSGMakeName(ISG_NGTYP_GAMEFILE, NULL, idx);
        if (iSG_get_finfo(data, n) != 0)
        {
            count++;
        }
    }

    return count != 0;
}

S32 iSGSetupGameDir(st_ISGSESSION* isgdata, const char* dname, S32 force_iconfix)
{
    return 1;
}

S32 iSGSaveFile(st_ISGSESSION* isgdata, const char* fname, char* data, S32 n, S32 async,
                char* label)
{
    S32 result = 0;
    en_ASYNC_OPERR whyFail = ISG_OPERR_NONE;
    st_ISG_MEMCARD_DATA* mcdata;
    CARDStat dumystat = { 0 };
    CARDStat poststat = { 0 };
    char* fulbuf;
    S32 fulsiz;
    char* bufptr;
    S32 bufsiz;
    S32 iconpart;
    S32 sizemult;

    ResetButton::DisableReset();
    if (isgdata->slot < 0)
    {
        isgdata->as_fakestat = ISG_OPSTAT_FAILURE;
        isgdata->as_operr = ISG_OPERR_NOCARD;
        return 0;
    }

    mcdata = &isgdata->mcdata[isgdata->slot];
    if (mcdata->cardIsIll != 0)
    {
        isgdata->as_fakestat = ISG_OPSTAT_FAILURE;
        isgdata->as_operr = ISG_OPERR_DAMAGE;
        return 0;
    }

    iTRCDisk::TRCCheck();
    iconpart = iSG_cubeicon_size(isgdata->slot, mcdata->sectorSize);
    sizemult = ALIGN_THING(mcdata->sectorSize, 0x200);
    bufsiz = iconpart + ALIGN_THING(sizemult, n);

    fulsiz = bufsiz + 0x1f;
    fulbuf = (char*)xMemPushTemp(fulsiz);
    memset(fulbuf, 0, fulsiz);
    bufptr = (char*)((U32)fulbuf + 0x1f & ~0x1f);

    memcpy(iSG_bfr_icondata(bufptr, &dumystat, label, mcdata->sectorSize), data, n);
    iTRCDisk::TRCCheck();

    if (iSG_mc_fopen(mcdata, fname, bufsiz, ISG_IOMODE_WRITE, &whyFail) != 0)
    {
        if (iSG_mc_fwrite(mcdata, bufptr, bufsiz) != 0)
        {
            result = 1;
        }
        else
        {
            result = 0;
            whyFail = ISG_OPERR_SVWRITE;
        }

        if (result != 0)
        {
            iSG_upd_icostat(&dumystat, &mcdata->fstat);
            iSG_timestamp(&mcdata->fstat);
            s32 rc;
            do
            {
                rc = CARDSetStatus(mcdata->chan, mcdata->finfo.fileNo, &mcdata->fstat);
            } while (rc == CARD_RESULT_BUSY);

            if (rc != CARD_RESULT_READY)
            {
                result = 0;
            }
        }

        if (result != 0)
        {
            s32 rc;
            do
            {
                rc = CARDSetAttributes(mcdata->chan, mcdata->finfo.fileNo, CARD_ATTR_PUBLIC);
            } while (rc == CARD_RESULT_BUSY);
        }

        iSG_mc_fclose(mcdata, &poststat);
    }
    xMemPopTemp(fulbuf);

    if (result == 0 && mcdata->cardIsIll == 0)
    {
        iSG_mc_fdel(mcdata, fname);
    }

    ResetButton::EnableReset();
    iTRCDisk::TRCCheck();

    isgdata->as_operr = ISG_OPERR_NONE;
    if (result != 0)
    {
        isgdata->as_fakestat = ISG_OPSTAT_SUCCESS;
    }
    else
    {
        isgdata->as_fakestat = ISG_OPSTAT_FAILURE;
        if (whyFail != ISG_OPERR_NONE)
        {
            isgdata->as_operr = whyFail;
        }
        else
        {
            isgdata->as_operr = ISG_OPERR_UNKNOWN;
        }
    }
    return result;
}

S32 iSGLoadFile(st_ISGSESSION* isgdata, const char* fname, char* databuf, S32 async)
{
    S32 numBytes = iSGFileSize(isgdata, fname);
    return iSGReadLeader(isgdata, fname, databuf, numBytes, async);
}

S32 iSGReadLeader(st_ISGSESSION* isgdata, const char* fname, char* databuf, S32 numbytes, S32 async)
{
    S32 result = 0;
    st_ISG_MEMCARD_DATA* mcdata;
    S32 offset;
    char* fulbuf = NULL;
    S32 fulsiz;
    char* bufptr;
    S32 bufsiz;
    S32 sizemult;
    en_ASYNC_OPERR whyFail = ISG_OPERR_NONE;

    if (isgdata->slot < 0)
    {
        isgdata->as_fakestat = ISG_OPSTAT_FAILURE;
        isgdata->as_operr = ISG_OPERR_NOCARD;
        return 0;
    }
    mcdata = &isgdata->mcdata[isgdata->slot];

    if (mcdata->cardIsIll != 0)
    {
        isgdata->as_fakestat = ISG_OPSTAT_FAILURE;
        isgdata->as_operr = ISG_OPERR_DAMAGE;
        return 0;
    }

    iTRCDisk::TRCCheck();
    offset = iSG_cubeicon_size(mcdata->chan, mcdata->sectorSize);
    sizemult = ALIGN_THING(mcdata->sectorSize, 0x200);
    if ((S32)databuf % 32 != 0 || numbytes % sizemult != 0)
    {
        fulsiz = ALIGN_THING(0x200, numbytes) + 0x1f;
        fulbuf = (char*)xMemPushTemp(fulsiz);
        memset(fulbuf, 0, fulsiz);
        bufptr = (char*)((U32)fulbuf + 0x1f & ~0x1f);
        bufsiz = ALIGN_THING(0x200, numbytes);
    }
    else
    {
        bufptr = databuf;
        bufsiz = numbytes;
    }

    iTRCDisk::TRCCheck();

    if (iSG_mc_fopen(mcdata, fname, -1, ISG_IOMODE_READ, &whyFail) != 0)
    {
        if (iSG_mc_fread(mcdata, bufptr, bufsiz, offset))
        {
            result = 1;
        }
        else
        {
            result = 0;
        }
        iSG_mc_fclose(mcdata);
    }

    if (result != 0 && fulbuf != NULL)
    {
        memcpy(databuf, bufptr, numbytes);
    }
    if (fulbuf != NULL)
    {
        xMemPopTemp(fulbuf);
    }

    isgdata->as_operr = ISG_OPERR_NONE;
    if (result != 0)
    {
        isgdata->as_fakestat = ISG_OPSTAT_SUCCESS;
    }
    else
    {
        isgdata->as_fakestat = ISG_OPSTAT_FAILURE;
        if (mcdata->cardIsIll != 0)
        {
            isgdata->as_operr = ISG_OPERR_DAMAGE;
        }
        else
        {
            isgdata->as_operr = ISG_OPERR_OTHER;
        }
    }
    return result;
}

en_ASYNC_OPSTAT iSGPollStatus(st_ISGSESSION* isgdata, en_ASYNC_OPCODE* curop, S32 block)
{
    return isgdata->as_fakestat;
}

en_ASYNC_OPERR iSGOpError(st_ISGSESSION* isgdata, char* errmsg)
{
    static char* errmsgs[ISG_OPERR_NOMORE + 1] = {
        "No current error",
        "No operation in async queue",
        "Too many async ops queued simultaneously",
        "Init Failed",
        "Unable to access Save Game Directory",
        "Access Error - no card ?!? (eg yanked out)",
        "Access Error - no room on card (file handles free bytes, etc)",
        "Access Error - card is damaged or something bad",
        "Access Error - file being loaded appears to be corrupt (I-Level)",
        "Access Error - general problem",
        "Save Error - Not enough free space to save file",
        "Save Error - during initalization (async queue)",
        "Save Error - during write",
        "Save Error - opening file",
        "Load Error - during initalization (async queue)",
        "Load Error - during read",
        "Load Error - opening file",
        "Target problem (general error)",
        "Target Error - media removed or changed",
        "Target Error - Not ready for I/O (unformatted?)",
        "Operation encountered unknown error",
        NULL
    };

    if (errmsg == NULL)
    {
        return isgdata->as_operr;
    }

    if (isgdata->as_operr < ISG_OPERR_NOMORE)
    {
        strncpy(errmsg, errmsgs[isgdata->as_operr], 0x80);
    }
    else
    {
        strncpy(errmsg, errmsgs[ISG_OPERR_UNKNOWN], 0x80);
    }
    errmsg[0x7f] = '\0';
    return isgdata->as_operr;
}

static inline S32 iSG_start_your_engines()
{
    CARDInit();
    return 1;
}

S32 iSG_mcidx2slot(S32 param1, S32* out_slot, S32* param3)
{
    S32 ret = 0;
    S32 idk = 0;
    s32 memSize = 0;
    s32 sectorSize = 0;
    s32 cardReady[ISG_NUM_SLOTS] = {};
    *out_slot = -1;

    for (S32 i = 0; i < ISG_NUM_SLOTS; i++)
    {
        s32 result;
        do
        {
            result = CARDProbeEx(i, &memSize, &sectorSize);
        } while (result == CARD_RESULT_BUSY);

        if (result == CARD_RESULT_READY)
        {
            cardReady[i] = TRUE;
        }
    }

    for (S32 i = 0; i < ISG_NUM_SLOTS; i++)
    {
        if (cardReady[i])
        {
            if (idk == param1)
            {
                *out_slot = i;
                ret = 1;
                break;
            }
            idk++;
        }
    }

    if (param3)
    {
        for (S32 i = 0; i < ISG_NUM_SLOTS; i++)
        {
            param3[i] = cardReady[i];
        }
    }

    return ret;
}

static inline S32 iSG_mc_exists(S32 slot)
{
    S32 ret = 0;
    s32 memSize = 0;
    s32 sectorSize = 0;

    if (slot < -1)
    {
        return 0;
    }

    S32 result;
    do
    {
        result = CARDProbeEx(slot, &memSize, &sectorSize);
    } while (result == CARD_RESULT_BUSY);

    if (result == CARD_RESULT_READY)
    {
        ret = 1;
    }

    return ret;
}

static S32 callbackResult;
static bool checkExInProgress;

// No TSSM symbol (always inlined); name and signature from the Ratatouille GC DWARF. Waits
// (servicing TRC) for the async check.
inline S32 iSG_CARDCheckEx(S32 chan, s32* xferBytes)
{
    checkExInProgress = true;
    S32 rc = CARDCheckExAsync(chan, xferBytes, CheckExCallback);
    if (rc != CARD_RESULT_READY)
    {
        checkExInProgress = false;
        return rc;
    }

    while (checkExInProgress)
    {
        iTRCDisk::TRCCheck();
    }
    return callbackResult;
}

static inline S32 iSG_mc_tryRepair(st_ISG_MEMCARD_DATA* mcdata)
{
    S32 result = 0;
    S32 rc = 0;
    s32 xferBytes = 0;

    if (mcdata->ismounted == 0)
    {
        rc = 0;
    }
    else if (mcdata->cardIsIll)
    {
        rc = 0;
    }
    else
    {
        do
        {
            result = iSG_CARDCheckEx(mcdata->chan, &xferBytes);
        } while (result == CARD_RESULT_BUSY);

        if (result == CARD_RESULT_READY)
        {
            rc = 1;
        }
        else if (result == CARD_RESULT_ENCODING)
        {
            rc = 1;
        }
        else
        {
            if (result != CARD_RESULT_BROKEN)
            {
                mcdata->cardIsIll = 1;
            }
            rc = 0;
        }
    }

    return rc;
}

static inline S32 iSG_mc_isformatted(st_ISG_MEMCARD_DATA* mcdata)
{
    S32 result = 0;
    S32 rc = 0;
    s32 xferBytes = 0;

    if (mcdata->ismounted == 0)
    {
        rc = 0;
    }
    else if (mcdata->cardIsIll)
    {
        rc = 0;
    }
    else
    {
        do
        {
            result = CARDCheckEx(mcdata->chan, &xferBytes);
        } while (result == CARD_RESULT_BUSY);

        if (result == CARD_RESULT_READY)
        {
            return 1;
        }
        else if (result == CARD_RESULT_BROKEN)
        {
            rc = 0;
        }
        else
        {
            rc = 0;
        }
    }

    return rc;
}

void CheckExCallback(s32 chan, s32 result)
{
    callbackResult = result;
    checkExInProgress = false;
}

static S32 iSG_mc_isGCcard(st_ISG_MEMCARD_DATA* mcdata, int* param2, int* param3)
{
    S32 result = 0;
    S32 rc = 0;

    s32 xferBytes = 0;
    u16 encoding = 0;
    s32 memSize = 0;
    s32 sectorSize = 0;

    if (param2)
    {
        *param2 = 0;
    }

    if (param3)
    {
        *param3 = 0;
    }

    if (mcdata->ismounted == 0)
    {
        return 0;
    }
    if (mcdata->cardIsIll)
    {
        return 0;
    }

    do
    {
        result = CARDProbeEx(mcdata->chan, &memSize, &sectorSize);
    } while (result == CARD_RESULT_BUSY);

    if (result == CARD_RESULT_READY)
    {
        rc = 1;
    }

    if (rc != 0)
    {
        do
        {
            result = CARDCheckEx(mcdata->chan, &xferBytes);
        } while (result == CARD_RESULT_BUSY);

        if (result == CARD_RESULT_READY)
        {
            rc = 1;
        }
        else if (result == CARD_RESULT_BROKEN)
        {
            rc = 2;
        }
        else
        {
            if (result == CARD_RESULT_ENCODING)
            {
                rc = 3;
                if (param2)
                {
                    *param2 = 1;
                }
            }
            else
            {
                rc = 0;
                if (result == CARD_RESULT_ENCODING && param2)
                {
                    *param2 = 1;
                }
                if (result == CARD_RESULT_IOERROR && param3)
                {
                    *param3 = 1;
                }
            }
        }
    }

    if (rc == 1)
    {
        do
        {
            result = CARDGetEncoding(mcdata->chan, &encoding);
        } while (result == CARD_RESULT_BUSY);

        if (result == CARD_RESULT_READY && encoding && param2)
        {
            *param2 = 1;
        }
    }

    return rc;
}

// The compiled-out check (Ratatouille's validate) keeps GC/2.6 from auto-inlining this, as retail
// calls it.
static S32 iSG_isSpaceForFile(st_ISG_MEMCARD_DATA* mcdata, S32 fsize, const char* fname,
                              S32* amtneed, S32* amtfree, S32* needFiles)
{
    S32 rc = 0;
    S32 result = 0;
    s32 byteNotUsed = 0;
    s32 filesNotUsed = 0;
    S32 len;

    xVALIDATE(mcdata->ismounted);

    if (mcdata->ismounted == 0)
    {
        return 0;
    }
    len = iSG_cubeicon_size(mcdata->chan, mcdata->sectorSize);
    len = len + fsize;
    len = ALIGN_THING(mcdata->sectorSize, len);

    do
    {
        result = CARDFreeBlocks(mcdata->chan, &byteNotUsed, &filesNotUsed);
    } while (result == CARD_RESULT_BUSY);

    if (result == CARD_RESULT_READY)
    {
        if (amtfree)
        {
            *amtfree = byteNotUsed / mcdata->sectorSize;
        }
        if (amtneed)
        {
            *amtneed = len / mcdata->sectorSize;
        }
    }
    else
    {
        return 0;
    }

    if (needFiles)
    {
        *needFiles = 1;
    }

    if (iSG_get_finfo(mcdata, fname))
    {
        if (needFiles)
        {
            *needFiles = *needFiles - 1 < 0 ? 0 : *needFiles - 1;
        }
        if (len <= mcdata->statcache.length + byteNotUsed)
        {
            rc = 1;
        }
    }
    else
    {
        if (len <= byteNotUsed && filesNotUsed > 0)
        {
            rc = 1;
        }
    }

    return rc;
}

static inline S32 iSG_mc_settgt(st_ISG_MEMCARD_DATA* mcdata, S32 slot)
{
    if (iSG_mc_mount(slot))
    {
        mcdata->ismounted = 1;
        mcdata->chan = slot;

        iSG_mc_tryRepair(mcdata);
        CARDGetSectorSize(slot, (u32*)&mcdata->sectorSize);
    }
    else
    {
        mcdata->ismounted = 0;
    }

    return mcdata->ismounted != 0 ? 1 : 0;
}

static inline S32 iSG_get_finfo(st_ISG_MEMCARD_DATA* mcdata, const char* dpath)
{
    S32 rc = 0;
    en_ASYNC_OPERR operr = ISG_OPERR_NONE;

    if (iSG_mc_fopen(mcdata, dpath, -1, ISG_IOMODE_READ, &operr))
    {
        rc = 1;
        memcpy(&mcdata->statcache, &mcdata->fstat, sizeof(CARDStat));
        memcpy(&mcdata->finfcache, &mcdata->finfo, sizeof(CARDFileInfo));
        iSG_mc_fclose(mcdata);
    }

    return rc;
}

// The compiled-out checks keep GC/2.6 from auto-inlining this, as retail calls it. The two
// validates are Ratatouille's; the other conditions are unknown.
static S32 iSG_curKosher(CARDStat* stat, CARDFileInfo* info) // FIX
{
    S32 rc = 1;
    xVALIDATE(stat);
    xVALIDATE(info);
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if ((stat->iconAddr < 1 || stat->iconAddr > 0x7fffffff) &&
        (stat->commentAddr < 1 || stat->commentAddr > 0x7fffffff))
    {
        rc = 0;
    }
    else
    {
        char stuff[0x200] = { 0 };
        sprintf(stuff, "SPONGEBOB2:PATRICK'SREVENGE::RyanNeilDan");
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        void* alloc = xMemPushTemp(0x2600 + 0x1f);
        xASSERT_PLACEHOLDER();
        // align buf address to 32 bytes
        char* buf = (char*)((U32)alloc + 0x1f & ~0x1f);
        xASSERT_PLACEHOLDER();

        S32 result;
        do
        {
            result = CARDRead(info, buf, 0x2600, 0);
        } while (result == CARD_RESULT_BUSY);
        xASSERT_PLACEHOLDER();

        if (result == CARD_RESULT_READY)
        {
            if (memcmp(buf + 0x2240, stuff, strlen(stuff) - 1))
            {
                rc = 0;
            }

            if (memcmp(buf, "The SpongeBob Movie", strlen("The SpongeBob Movie") - 1))
            {
                rc = 0;
            }
        }

        xMemPopTemp(alloc);
    }

    return rc;
}

static inline S32 iSG_fileKosher(st_ISG_MEMCARD_DATA* mcdata, const char* param2, int param3,
                                 int* param4)
{
    S32 rc = 0;
    en_ASYNC_OPERR operr = ISG_OPERR_NONE;

    if (param4)
    {
        *param4 = 0;
    }

    if (iSG_mc_fopen(mcdata, param2, -1, ISG_IOMODE_READ, &operr) == 0)
    {
        return -1;
    }

    S32 ret = iSG_curKosher(&mcdata->fstat, &mcdata->finfo);
    iSG_mc_fclose(mcdata);

    if (ret == 0)
    {
        rc = 0;
        if (param3 && iSG_mc_fdel(mcdata, param2) && param4)
        {
            *param4 = 1;
        }
    }
    else
    {
        rc = 1;
    }
    return rc;
}

static inline S32 iSG_get_fsize(st_ISG_MEMCARD_DATA* mcdata, const char* param2)
{
    S32 rc = -1;

    if (iSG_get_finfo(mcdata, param2))
    {
        rc = mcdata->statcache.length;
    }

    if (rc < 0)
    {
        rc = -1;
    }

    return rc;
}

static inline S32 iSG_get_fmoddate(st_ISG_MEMCARD_DATA* mcdata, const char* fname, int* sec,
                                   int* min, int* hr, int* mon, int* day, int* yr)
{
    S32 rc = 1;
    OSCalendarTime time = { 0 };

    if (iSG_get_finfo(mcdata, fname) == 0)
    {
        rc = 0;
    }
    else
    {
        OSTime t = mcdata->statcache.time;
        t = OSSecondsToTicks(t);
        OSTicksToCalendarTime(t, &time);

        if (sec)
        {
            *sec = time.sec;
        }
        if (min)
        {
            *min = time.min;
        }
        if (hr)
        {
            *hr = time.hour;
        }
        if (mon)
        {
            *mon = time.mon + 1;
        }
        if (day)
        {
            *day = time.mday;
        }
        if (mon)
        {
            *mon = time.mon + 1;
        }
        if (day)
        {
            *day = time.mday;
        }
        if (yr)
        {
            *yr = time.year;
        }
    }

    return rc;
}

static inline void iSG_timestamp(CARDStat*)
{
}

static inline S32 iSG_cubeicon_size(S32 slot, S32 sectsize)
{
    S32 amt = 0;
    S32 jump = 0;
    S32 fixed = 0;

    if ((U32)slot > 1)
    {
        return -1;
    }

    jump = ALIGN_THING(sectsize, 0x200);
    fixed = sizeof(IconData);
    amt = ALIGN_THING(jump, fixed);

    return amt;
}

static S32 iSG_chk_icondata()
{
    return 1;
}

// The checks and rc are Ratatouille's; with the placeholders (conditions unknown) they keep GC/2.6
// from auto-inlining this into iSGStartup, as retail calls it.
static S32 iSG_load_icondata()
{
    S32 rc = 0;

    g_rawicon = (st_ISG_TPL_TEXPALETTE*)iFileLoad("/SBGCIcon.tpl", NULL, &g_iconsize);
    xVALIDATE(g_rawicon);
    xVALIDATE(g_iconsize);

    g_rawbanr = (st_ISG_TPL_TEXPALETTE*)iFileLoad("/SBGCBanner.tpl", NULL, &g_banrsize);
    xVALIDATE(g_rawbanr);
    xVALIDATE(g_banrsize);

    xASSERT(g_rawicon && g_iconsize && g_rawbanr && g_banrsize);

    rc = iSG_tpl_unpack(g_rawicon);
    xVALIDATE(rc);
    rc = iSG_bnr_unpack(g_rawbanr);
    xVALIDATE(rc);

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    return g_rawicon && (S32)g_iconsize && g_rawbanr && (S32)g_banrsize ? 1 : 0;
}

static char* iSG_bfr_icondata(char* param1, CARDStat* stat, char* param3, int param4)
{
    IconData data = { 0 };
    static st_ISG_TPL_TEXPALETTE* ico_pal;
    static st_ISG_TPL_TEXDESCRIP* ico_desc;
    static U32 i = 0;

    sprintf(data.footer, "SPONGEBOB2:PATRICK'SREVENGE::RyanNeilDan");
    strncpy(data.game_name, "The SpongeBob Movie", sizeof(data.game_name));
    data.game_name[0x1f] = NULL;

    if (param3)
    {
        strncpy(data.buf0, param3, sizeof(data.buf0));
    }
    data.buf0[0x1f] = NULL;

    ico_pal = g_rawbanr;
    ico_desc = iSG_tpl_TEXGet(ico_pal, 0);
    memcpy(data.buf1, ico_desc->textureHeader->data, sizeof(data.buf1));

    ico_pal = g_rawicon;
    for (i = 0; i < 1; i++)
    {
        ico_desc = iSG_tpl_TEXGet(ico_pal, i);
        memcpy(data.buf2[i], ico_desc->textureHeader->data, sizeof(data.buf2[i]));
    }

    memcpy(param1, &data, sizeof(data));

    S32 t = ALIGN_THING(param4, 0x200);
    return param1 + (t + (sizeof(IconData) - 1) & -t);
}

static inline void iSG_upd_icostat(CARDStat*, CARDStat* stat)
{
    CARDSetCommentAddress(stat, 0);
    CARDSetBannerFormat(stat, CARD_STAT_BANNER_RGB5A3);
    CARDSetIconAddress(stat, 0x40);
    CARDSetIconAnim(stat, CARD_STAT_ANIM_LOOP);

    // TSSM has a single icon frame; the frame after the last one ends the animation.
    S32 i;
    for (i = 0; i < 1; ++i)
    {
        CARDSetIconFormat(stat, i, CARD_STAT_BANNER_RGB5A3);
        CARDSetIconSpeed(stat, i, CARD_STAT_SPEED_MIDDLE);
    }
    CARDSetIconFormat(stat, i, CARD_STAT_ICON_NONE);
    CARDSetIconSpeed(stat, i, CARD_STAT_SPEED_END);
}

// Converts the file offsets of a loaded TPL into pointers (like the SDK's TPLBind). The locals and
// checks are Ratatouille's; with one placeholder they keep GC/2.6 from auto-inlining this into
// iSG_load_icondata, as retail calls it.
static S32 iSG_tpl_unpack(st_ISG_TPL_TEXPALETTE* pal)
{
    S32 i = 0;
    S32 rc = 0; // unused
    st_ISG_TPL_TEXDESCRIP* tmpdesc = NULL;
    U32 palpos = (U32)pal;
    U32 tmppos = 0;

    xVALIDATE(pal);
    xVALIDATE(pal->versionNumber == 2142000);
    xASSERT_PLACEHOLDER();

    if (pal->versionNumber != 2142000)
    {
        return 0;
    }

    tmppos = palpos + (U32)pal->descriptorArray;
    pal->descriptorArray = (st_ISG_TPL_TEXDESCRIP*)tmppos;
    for (i = 0; i < pal->numDescriptors; i++)
    {
        tmpdesc = &pal->descriptorArray[i];
        if (tmpdesc->textureHeader != NULL)
        {
            tmppos = palpos + (U32)tmpdesc->textureHeader;
            tmpdesc->textureHeader = (st_ISG_TPL_TEXHEADER*)tmppos;
            if (tmpdesc->textureHeader->unpacked == 0)
            {
                tmppos = palpos + (U32)tmpdesc->textureHeader->data;
                tmpdesc->textureHeader->data = (char*)tmppos;
                tmpdesc->textureHeader->unpacked = 1;
            }
        }
        if (tmpdesc->CLUTHeader != NULL)
        {
            tmppos = palpos + (U32)tmpdesc->CLUTHeader;
            tmpdesc->CLUTHeader = (st_ISG_TPL_CLUTHEADER*)tmppos;
            if (tmpdesc->CLUTHeader->unpacked == 0)
            {
                // sic: the file base, not base + offset, is stored for CLUT data (the icon/banner
                // TPLs have no CLUTs)
                tmpdesc->CLUTHeader->data = (char*)palpos;
                tmpdesc->CLUTHeader->unpacked = 1;
            }
        }
    }
    return 1;
}

// Inlined into iSG_load_icondata; retail has no out-of-line copy.
static S32 iSG_bnr_unpack(st_ISG_TPL_TEXPALETTE* pal)
{
    return iSG_tpl_unpack(pal);
}

static inline st_ISG_TPL_TEXDESCRIP* iSG_tpl_TEXGet(st_ISG_TPL_TEXPALETTE* pal, U32 idx)
{
    return &pal->descriptorArray[idx];
}

static inline S32 iSG_mc_mount(S32 cardslot)
{
    S32 result = 0;
    // volatile: retail stores CARDMount's result to the stack and reloads it before every
    // comparison.
    volatile S32 rc = 0;

    do
    {
        rc = CARDMount(cardslot, cardwork[cardslot], iSG_cb_unmount);
        isMounted = 1;
    } while (rc == CARD_RESULT_BUSY);

    if (rc == CARD_RESULT_READY)
    {
        result = 1;
    }
    else if (rc == CARD_RESULT_BROKEN || rc == CARD_RESULT_ENCODING)
    {
        result = 1;
    }

    return result;
}

static inline S32 iSG_mc_unmount(S32 slot)
{
    S32 rc = 0;
    S32 result;

    do
    {
        result = CARDUnmount(slot);
    } while (result == CARD_RESULT_BUSY);

    if (result == CARD_RESULT_READY)
    {
        rc = 1;
    }
    else if (result == CARD_RESULT_NOCARD)
    {
        rc = 1;
    }

    return rc;
}

static void iSG_cb_unmount(s32 chan, s32 result)
{
    st_ISGSESSION* session = &g_isgdata_MAIN;
    if (chan != session->slot)
    {
        return;
    }
    session->slot = -1;
    if (session->chgfunc != NULL)
    {
        session->chgfunc(session->cltdata, ISG_CHG_TARGET);
    }
    memset(&session->mcdata[chan], 0, sizeof(st_ISG_MEMCARD_DATA));
}

// The locals and the validate are Ratatouille's; with 4 placeholders (the last one with the break
// of the default case) they keep GC/2.6 from auto-inlining this, as retail calls it.
static S32 iSG_mc_format(st_ISG_MEMCARD_DATA* mcdata, S32 force, S32* canRecover)
{
    S32 result = 0;
    S32 rc = 0;

    xVALIDATE(!(mcdata->cardIsIll));
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (mcdata->cardIsIll != 0)
    {
        if (canRecover != NULL)
        {
            *canRecover = 0;
        }
        return 0;
    }

    if (canRecover != NULL)
    {
        *canRecover = 1;
    }

    do
    {
        rc = CARDFormat(mcdata->chan);
    } while (rc == CARD_RESULT_BUSY);

    if (rc == CARD_RESULT_READY)
    {
        result = 1;
    }
    else
    {
        result = 0;

        switch (rc)
        {
        case CARD_RESULT_IOERROR:
            mcdata->cardIsIll = 1;
            if (canRecover != NULL)
            {
                *canRecover = 0;
            }
            break;
        case CARD_RESULT_NOCARD:
            if (canRecover != NULL)
            {
                *canRecover = -1;
            }
            result = -1;
            break;
        case CARD_RESULT_READY:
        case CARD_RESULT_BUSY:
        case CARD_RESULT_NOFILE:
        case CARD_RESULT_FATAL_ERROR:
            break;
        default:
            xASSERT_PLACEHOLDER();
            break;
        }
    }
    return result;
}

static bool cardCreateInProgress;

void CardCreateCallback(s32 chan, s32 result)
{
    callbackResult = result;
    cardCreateInProgress = false;
}

// No TSSM symbol (always inlined); name and signature from the Ratatouille GC DWARF. Waits
// (servicing TRC) for the async create to finish.
inline S32 iSG_CARDCreate(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo)
{
    cardCreateInProgress = true;
    S32 rc = CARDCreateAsync(chan, fileName, size, fileInfo, CardCreateCallback);
    if (rc != CARD_RESULT_READY)
    {
        cardCreateInProgress = false;
        return rc;
    }

    while (cardCreateInProgress)
    {
        iTRCDisk::TRCCheck();
    }
    return callbackResult;
}

static S32 iSG_mc_fopen(st_ISG_MEMCARD_DATA* mcdata, const char* fname, S32 fsize,
                        en_ISG_IOMODE mode, en_ASYNC_OPERR* operr)
{
    S32 ret = 0;
    CARDFileInfo* finfo = &mcdata->finfo;
    CARDStat* stat = &mcdata->fstat;
    if (operr != NULL)
    {
        *operr = ISG_OPERR_NONE;
    }

    if (mode == ISG_IOMODE_READ)
    {
        s32 result;
        do
        {
            result = CARDOpen(mcdata->chan, (char*)fname, finfo);
        } while (result == CARD_RESULT_BUSY);

        if (result == CARD_RESULT_READY)
        {
            ret = 1;
        }
    }
    else
    {
        S32 res = iSG_mc_fdel(mcdata, fname);
        iTRCDisk::TRCCheck();
        if (mcdata->cardIsIll == 0 && res > 0)
        {
            s32 result;
            do
            {
                result = iSG_CARDCreate(mcdata->chan, fname, fsize, finfo);
            } while (result == CARD_RESULT_BUSY);

            if (result == CARD_RESULT_READY)
            {
                ret = 1;
                mcdata->off_nextwrite = 0;
            }
            else if (operr != NULL)
            {
                switch (result)
                {
                case CARD_RESULT_WRONGDEVICE:
                case CARD_RESULT_NOCARD:
                    *operr = ISG_OPERR_NOCARD;
                    break;
                case CARD_RESULT_IOERROR:
                case CARD_RESULT_BROKEN:
                    mcdata->cardIsIll = 1;
                    *operr = ISG_OPERR_DAMAGE;
                    break;
                case CARD_RESULT_NOENT:
                case CARD_RESULT_INSSPACE:
                    *operr = ISG_OPERR_NOROOM;
                    break;
                default:
                    *operr = ISG_OPERR_OTHER;
                    break;
                case CARD_RESULT_READY:
                case CARD_RESULT_BUSY:
                case CARD_RESULT_EXIST:
                case CARD_RESULT_NOPERM:
                    break;
                }
            }
        }
    }

    if (ret != 0)
    {
        s32 result;
        do
        {
            result = CARDGetStatus(mcdata->chan, finfo->fileNo, stat);
            iTRCDisk::TRCCheck();
        } while (result == CARD_RESULT_BUSY);

        if (result == CARD_RESULT_READY)
        {
            ret = 1;
        }
    }

    if (gVidMode == eGameVidModeNTSC)
    {
        ISG_GC_GAMENAME = "GGVE";
    }
    else if (gVidMode == eGameVidModePAL)
    {
        switch (xSTGetLocalizationEnum())
        {
        case eRegionDE:
            ISG_GC_GAMENAME = "GGVD";
            break;
        case eRegionFR:
            ISG_GC_GAMENAME = "GGVF";
            break;
        case eRegionNL:
            ISG_GC_GAMENAME = "GGVN";
            break;
        case eRegionUK:
        default:
            ISG_GC_GAMENAME = "GGVP";
            break;
        }
    }

    if (ret != 0)
    {
        if (memcmp(stat->gameName, ISG_GC_GAMENAME, 4) != 0 || memcmp(stat->company, "78", 2) != 0)
        {
            iSG_mc_fclose(mcdata);
            ret = 0;
        }
    }

    return ret;
}

static inline S32 iSG_mc_fclose(st_ISG_MEMCARD_DATA* mcdata, CARDStat* stat)
{
    S32 ret = 0;
    s32 result;
    do
    {
        result = CARDClose(&mcdata->finfo);
    } while (result == CARD_RESULT_BUSY);

    if (result == CARD_RESULT_READY)
    {
        ret = 1;
    }

    memcpy(stat, &mcdata->fstat, sizeof(CARDStat));

    memset(&mcdata->finfo, 0, sizeof(CARDFileInfo));
    memset(&mcdata->fstat, 0, sizeof(CARDStat));

    return ret;
}

static inline S32 iSG_mc_fclose(st_ISG_MEMCARD_DATA* mcdata)
{
    S32 ret = 0;
    s32 result;
    do
    {
        result = CARDClose(&mcdata->finfo);
    } while (result == CARD_RESULT_BUSY);

    if (result == CARD_RESULT_READY)
    {
        ret = 1;
    }

    memset(&mcdata->finfo, 0, sizeof(CARDFileInfo));
    memset(&mcdata->fstat, 0, sizeof(CARDStat));

    return ret;
}

static bool cardDeleteInProgress;

void CardDeleteCallback(s32 chan, s32 result)
{
    callbackResult = result;
    cardDeleteInProgress = false;
}

// No TSSM symbol (always inlined); name and signature from the Ratatouille GC DWARF. Waits
// (servicing TRC) for the async delete.
inline S32 iSG_CARDDelete(s32 chan, const char* fileName)
{
    cardDeleteInProgress = true;
    S32 rc = CARDDeleteAsync(chan, fileName, CardDeleteCallback);
    if (rc != CARD_RESULT_READY)
    {
        cardDeleteInProgress = false;
        return rc;
    }

    while (cardDeleteInProgress)
    {
        iTRCDisk::TRCCheck();
    }
    return callbackResult;
}

// The locals are Ratatouille's (isgdata is stored but never read); with 9 placeholders they keep
// GC/2.6 from auto-inlining this, as retail calls it.
static S32 iSG_mc_fdel(st_ISG_MEMCARD_DATA* mcdata, const char* fname)
{
    S32 result = 0;
    S32 rc = 0;

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (mcdata->chan < 0 || mcdata->chan > 1)
    {
        return 0;
    }

    st_ISGSESSION* isgdata = &g_isgdata_MAIN;
    if (isMounted == 0)
    {
        return 0;
    }

    do
    {
        rc = iSG_CARDDelete(mcdata->chan, fname);
    } while (rc == CARD_RESULT_BUSY);

    if (rc == CARD_RESULT_NOFILE)
    {
        result = 1;
    }
    else if (rc == CARD_RESULT_READY)
    {
        result = 1;
    }
    else
    {
        result = 0;
        switch (rc)
        {
        case CARD_RESULT_IOERROR:
            mcdata->cardIsIll = 1;
            break;
        case CARD_RESULT_READY:
        case CARD_RESULT_BUSY:
        case CARD_RESULT_NOCARD:
        case CARD_RESULT_NOFILE:
        case CARD_RESULT_NOPERM:
        case CARD_RESULT_FATAL_ERROR:
            break;
        }
    }
    return result;
}

static void iSG_cb_asyndone(s32, s32)
{
}

static inline S32 iSG_mc_fread(st_ISG_MEMCARD_DATA* mcdata, char* buf, S32 len, S32 offset)
{
    return iSG_mcqa_fread(mcdata, buf, len, offset);
}

// The locals and the validate are Ratatouille's; with 5 placeholders they keep GC/2.6 from
// auto-inlining this, as retail calls it.
static S32 iSG_mcqa_fread(st_ISG_MEMCARD_DATA* mcdata, char* buf, S32 bufsize, S32 skipbyte)
{
    S32 rc = 0;
    S32 amt_start = 0;
    S32 amt = 0;
    S32 updintv = 1000;
    S32 polltillready = 1;

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (polltillready)
    {
        rc = CARDGetResultCode(mcdata->chan);
        do
        {
            rc = CARDGetResultCode(mcdata->chan);
        } while (rc == CARD_RESULT_BUSY);

        if (rc != CARD_RESULT_READY)
        {
            return 0;
        }
    }

    amt_start = CARDGetXferredBytes(mcdata->chan);
    do
    {
        rc = CARDReadAsync(&mcdata->finfo, buf, bufsize, skipbyte, iSG_cb_asyndone);
    } while (rc == CARD_RESULT_BUSY);
    xVALIDATE(rc == CARD_RESULT_READY);

    if (rc != CARD_RESULT_READY)
    {
        return 0;
    }

    rc = CARD_RESULT_BUSY;
    do
    {
        if (updintv++ > 500)
        {
            xSndMgrUpdate();
            amt = CARDGetXferredBytes(mcdata->chan);
            rc = CARDGetResultCode(mcdata->chan);
            updintv = 0;
        }
        iTRCDisk::TRCCheck();
    } while (rc == CARD_RESULT_BUSY);
    return rc != 0 ? 0 : 1;
}

static inline S32 iSG_mc_fwrite(st_ISG_MEMCARD_DATA* mcdata, char* buf, S32 len)
{
    return iSG_mcqa_fwrite(mcdata, buf, len);
}

// The locals (as in Ratatouille's debug build) keep GC/2.6 from auto-inlining this, as retail
// calls it.
static S32 iSG_mcqa_fwrite(st_ISG_MEMCARD_DATA* mcdata, char* buf, S32 len)
{
    S32 ret = 0;
    S32 updintv = 1000;
    S32 amt_start = 0;
    S32 amt = 0;
    CARDStat* stat = &mcdata->fstat;
    S32 polltillready = 1;
    s32 result;

    iTRCDisk::TRCCheck();
    if (polltillready)
    {
        do
        {
            result = CARDGetResultCode(mcdata->chan);
        } while (result == CARD_RESULT_BUSY);

        if (result != CARD_RESULT_READY)
        {
            return 0;
        }
    }

    amt_start = CARDGetXferredBytes(mcdata->chan);
    s32 asynresult;
    do
    {
        asynresult =
            CARDWriteAsync(&mcdata->finfo, buf, len, mcdata->off_nextwrite, iSG_cb_asyndone);
    } while (asynresult == CARD_RESULT_BUSY);

    if (asynresult != CARD_RESULT_READY)
    {
        return 0;
    }

    result = CARD_RESULT_BUSY;
    iTRCDisk::TRCCheck();
    do
    {
        if (updintv++ > 500)
        {
            result = CARDGetResultCode(mcdata->chan);
            xSndMgrUpdate();
            amt = CARDGetXferredBytes(mcdata->chan);
            updintv = 0;
        }
        iTRCDisk::TRCCheck();
    } while (result == CARD_RESULT_BUSY);

    if (asynresult == CARD_RESULT_READY)
    {
        mcdata->off_nextwrite += len;

        do
        {
            result = CARDGetStatus(mcdata->chan, mcdata->finfo.fileNo, stat);
        } while (result == CARD_RESULT_BUSY);

        if (result == CARD_RESULT_READY)
        {
            ret = 1;
        }
    }
    return ret;
}

S32 iSGCheckForWrongDevice()
{
    char* workarea = (char*)RwMalloc(0x10000 - 0x6000, 0);

    S32 ret = -1;
    for (S32 i = 0; i < ISG_NUM_SLOTS; ++i)
    {
        s32 result = CARDMount(i, workarea, NULL);

        switch (result)
        {
        case CARD_RESULT_READY:
        case CARD_RESULT_BROKEN:
        case CARD_RESULT_ENCODING:
            CARDUnmount(i);
            break;
        case CARD_RESULT_WRONGDEVICE:
            ret = i;
            i = ISG_NUM_SLOTS;
            break;
        }
    }

    RwFree(workarea);

    return ret;
}

U8 iSGCheckForCorruptFiles(st_ISGSESSION* isgdata, char files[][64])
{
    if (isgdata->slot < 0)
    {
        return 0;
    }

    S32 i;
    char* name;
    st_ISG_MEMCARD_DATA* mcdata = &isgdata->mcdata[isgdata->slot];
    S32 ret = 0;
    // retail clears only ISG_NUM_FILES pointers' worth of bytes, not the whole name table
    memset(files, 0, ISG_NUM_FILES * sizeof(char*));

    for (i = 0; i < ISG_NUM_FILES; ++i)
    {
        name = iSGMakeName(ISG_NGTYP_GAMEFILE, NULL, i);
        if (iSG_fileKosher(mcdata, name, 0, NULL) == 0)
        {
            strcpy(files[ret], name);
            ret++;
        }
    }
    return ret > 0;
}

U8 iSGIsGameCorrupt(st_ISGSESSION* isgdata, S32 idx)
{
    if (isgdata->slot < 0)
    {
        return 0;
    }

    st_ISG_MEMCARD_DATA* mcdata = &isgdata->mcdata[isgdata->slot];
    if (iSG_fileKosher(mcdata, iSGMakeName(ISG_NGTYP_GAMEFILE, NULL, idx), 0, NULL) == 0)
    {
        return 1;
    }
    return 0;
}

U8 iSGCheckMemoryCard(st_ISGSESSION* isgdata, S32 index)
{
    s32 memSize;
    s32 sectorSize;

    switch (CARDProbeEx(index, &memSize, &sectorSize))
    {
    case CARD_RESULT_READY:
        return 1;
    }
    return 0;
}

void iSGAutoSave_Startup()
{
}

st_ISGSESSION* iSGAutoSave_Connect(S32 idx_target, void* cltdata, void (*chg)(void*, en_CHGCODE))
{
    st_ISGSESSION* isg = iSGSessionBegin(cltdata, chg, 1);
    if (isg == NULL)
    {
        return isg;
    }

    if (iSGTgtSetActive(isg, idx_target) == 0)
    {
        iSGSessionEnd(isg);
        isg = NULL;
    }

    return isg;
}

void iSGAutoSave_Disconnect(st_ISGSESSION* isg)
{
    iSGSessionEnd(isg);
}

S32 iSGAutoSave_Monitor(st_ISGSESSION* isg, S32 idx_target)
{
    if (isg == NULL)
    {
        return 0;
    }

    U32 ret = iSGTgtState(isg, idx_target, NULL);
    if (ret == 0 || (ret & 1) == 0)
    {
        globals.autoSaveFeature = 0;
        return 0;
    }
    return 1;
}
