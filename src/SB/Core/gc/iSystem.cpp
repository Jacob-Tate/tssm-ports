#include "iSystem.h"
#include "zGame.h"
#include <PowerPC_EABI_Support/MSL_C/MSL_Common/stdlib.h>
#include <dolphin/gx/GXVerify.h>
#include <string.h>

extern _tagGameVidMode gVidMode;
extern U32 FB_XRES;
extern U32 FB_YRES;

// the_heap and memInited (used by malloc/free) are defined in iMemMgr.cpp, which iWad.cpp
// includes before this file.

struct iARAMBlock
{
    U32 addr;
    U32 size;
    U32 flags; // bit 0: in use
};

// No reference survives in retail (BfBB's psGetMemoryFunctions returned it), but the object is
// in the retail .bss at this position.
static RwMemoryFunctions MemoryFunctions;

// The last block is never handed out directly: allocation may merge into block i + 1.
static iARAMBlock AramMgr[32];

GXRenderModeObj myGXNtsc480IntDf = { VI_TVMODE_NTSC_INT,
                                     640,
                                     480,
                                     480,
                                     40,
                                     0,
                                     640,
                                     480,
                                     VI_XFBMODE_DF,
                                     GX_FALSE,
                                     GX_FALSE,
                                     { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
                                       6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
                                     { 8, 8, 10, 12, 10, 8, 8 } };

GXRenderModeObj myGXPal528IntDf = { VI_TVMODE_PAL_INT,
                                    640,
                                    528,
                                    528,
                                    40,
                                    23,
                                    640,
                                    528,
                                    VI_XFBMODE_DF,
                                    GX_FALSE,
                                    GX_FALSE,
                                    { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
                                      6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
                                    { 8, 8, 10, 12, 10, 8, 8 } };

// No GC symbol (only used by NewRwDebugHandler, which retail dead-strips); from the Ratatouille GC
// DWARF.
static RwDebugHandler oldDebugHandler;
RwVideoMode sVideoMode;
RwGameCubeDeviceConfig deviceConfig;
GXRenderModeObj renderObject;

GXDrawSyncCallback old_dsc;
U16 last_error;
OSContext* last_context;

char* months[] = { "January ", "February ", "March ",     "April ",   "May ",      "June ",
                   "July ",    "August ",   "September ", "October ", "November ", "December " };

char* dotw[] = {
    "Sunday ", "Monday ", "Tuesday ", "Wednesday ", "Thursday ", "Friday ", "Saturday "
};

static RwTexture* TextureRead(const RwChar* name, const RwChar* maskName);

void iVSync()
{
    VIWaitForRetrace();
}

// No GC symbol (only used by myvcb, which retail dead-strips); from the Ratatouille GC DWARF.
S32 spew_warnings;

// GX verify callback. Nothing installs it in retail, but its format string is in the string pool
// here. Name and parameters from the Ratatouille GC DWARF; the body is reconstructed.
static void myvcb(GXWarningLevel level, u32 id, const char* msg)
{
    if (spew_warnings)
    {
        OSReport("Level %d, Warning $03d: %s\n", level, id, msg);
    }
}

void my_dsc(u16 dsc)
{
}

static void FloatingPointErrorHandler(U16 last, OSContext* ctxt, u32 dsisr, u32 dar)
{
    U32 fpscr = ctxt->fpscr;
    fpscr &= ((fpscr & 0xf8) << 0x16 | 0x1f80700);
    if (fpscr & 0x20000000)
    {
        OSReport("FPE: Invalid operation: ");

        if (fpscr & 0x1000000)
        {
            OSReport("SNaN\n");
        }
        if (fpscr & 0x800000)
        {
            OSReport("Infinity - Infinity\n");
        }
        if (fpscr & 0x400000)
        {
            OSReport("Infinity / Infinity\n");
        }
        if (fpscr & 0x200000)
        {
            OSReport("0 / 0\n");
        }
        if (fpscr & 0x100000)
        {
            OSReport("Infinity * 0\n");
        }
        if (fpscr & 0x80000)
        {
            OSReport("Invalid compare\n");
        }
        if (fpscr & 0x400)
        {
            OSReport("Software request\n");
        }
        if (fpscr & 0x200)
        {
            OSReport("Invalid square root\n");
        }
        if (fpscr & 0x100)
        {
            OSReport("Invalid integer convert\n");
        }
    }
    if (fpscr & 0x10000000)
    {
        OSReport("FPE: Overflow\n");
    }
    if (fpscr & 0x8000000)
    {
        OSReport("FPE: Underflow\n");
    }
    if (fpscr & 0x4000000)
    {
        OSReport("FPE: Zero division\n");
    }
    if (fpscr & 0x2000000)
    {
        OSReport("FPE: Inexact result\n");
    }
    ctxt->srr0 += 4;
}

static void MemoryProtectionErrorHandler(U16 last, OSContext* ctx, u32 dsisr, u32 dar)
{
    last_error = last;
    last_context = ctx;
}

static inline void TRCInit()
{
    iTRCDisk::Init();
    iTRCDisk::SetPadStopRumblingFunction(iPadStopRumble);
    iTRCDisk::SetSndSuspendFunction(iSndSuspend);
    iTRCDisk::SetSndResumeFunction(iSndResume);
    iTRCDisk::SetMovieSuspendFunction(iFMVSuspend);
    iTRCDisk::SetMovieResumeFunction(iFMVResume);
    iTRCDisk::SetAsyncLoadSuspendFunction(iAsyncSuspend);
    iTRCDisk::SetAsyncLoadResumeFunction(iAsyncResume);
    ResetButton::SetSndKillFunction(iSndKill);
    ResetButton::SetMovieKillFunction(iFMVKill);
}

void iSystemGetWidthHeight(U32& uWidth, U32& uHeight, bool bPALMode)
{
    if (bPALMode)
    {
        uWidth = 512;
        uHeight = 528;
    }
    else
    {
        uWidth = 512;
        uHeight = 480;
    }
}

static inline S32 RenderWareExit();
static inline S32 RenderWareInit();

void iSystemInit(U32 options)
{
    GXSetMisc(GX_MT_XF_FLUSH, 8);
    OSInit();
    DVDInit();
    VIInit();

    __OSFpscrEnableBits = FPSCR_ZE;
    OSSetErrorHandler(OS_ERROR_FPE, (OSErrorHandler)FloatingPointErrorHandler);
    OSSetErrorHandler(OS_ERROR_PROTECTION, (OSErrorHandler)MemoryProtectionErrorHandler);

    xDebugInit();
    xMemInit();
    iFileInit();
    iTimeInit();
    xPadInit();
    RenderWareInit();
    xSndMgrInit();
    TRCInit();
    xMathInit();
    xMath3Init();
}

static S32 iRenderWareInit2();

void iSystemInitGraphics()
{
    iRenderWareInit2();
    xShadowInit();
    xFXInit();
    old_dsc = GXSetDrawSyncCallback(my_dsc);
}

void iSystemExit()
{
    xDebugExit();
    xMathExit();
    RenderWareExit();
    xSndMgrExit();
    xPadKill();
    iFileExit();
    iTimeExit();
    xMemExit();
    OSPanic("iSystem.cpp", 0x1e7,
            "(With apologies to Jim Morrison) This the end, my only friend, The End.");
}

static inline U32 RWAttachPlugins()
{
    if (!RpWorldPluginAttach())
    {
        return TRUE;
    }
    if (!RpCollisionPluginAttach())
    {
        return TRUE;
    }
    if (!RpMatFXPluginAttach())
    {
        return TRUE;
    }
    if (!RpSkinPluginAttach())
    {
        return TRUE;
    }
    if (!RpHAnimPluginAttach())
    {
        return TRUE;
    }
    if (!RpUserDataPluginAttach())
    {
        return TRUE;
    }
    if (!RpPTankPluginAttach())
    {
        return TRUE;
    }
    return FALSE;
}

static inline S32 RenderWareInit()
{
    RwMemoryFunctions memoryFuncs = { iMemMgrMalloc, iMemMgrFree, iMemMgrRealloc, iMemMgrCalloc };

    if (!RwEngineInit(&memoryFuncs, rwENGINEINITNOFREELISTS, 0xAE000))
    {
        return TRUE;
    }
    RwResourcesSetArenaSize(0xAE000);
    if (RWAttachPlugins())
    {
        return TRUE;
    }
    return FALSE;
}

static inline S32 RenderWareExit()
{
    RwEngineStop();
    RwEngineClose();
    return RwEngineTerm();
}

static S32 iRenderWareInit2()
{
    VISetBlack(TRUE);
    VIFlush();
    VIWaitForRetrace();

    renderObject = myGXNtsc480IntDf;
    if (gVidMode == eGameVidModeNTSC)
    {
        renderObject = myGXNtsc480IntDf;
    }
    else if (gVidMode == eGameVidModePAL)
    {
        renderObject = myGXPal528IntDf;
    }

    renderObject.fbWidth = FB_XRES;
    renderObject.efbHeight = FB_YRES;
    renderObject.xfbHeight = FB_YRES;

    deviceConfig.renderMode = &renderObject;
    deviceConfig.pixFmt = 1;
    deviceConfig.fifoSize = 0x20000;

    RwEngineOpenParams params;
    params.displayID = &deviceConfig;
    if (!RwEngineOpen(&params))
    {
        RwEngineTerm();
        VISetBlack(FALSE);
        VIFlush();
        return TRUE;
    }

    RwEngineGetVideoModeInfo(&sVideoMode, RwEngineGetCurrentVideoMode());
    if (!RwEngineStart())
    {
        RwEngineClose();
        RwEngineTerm();
        VISetBlack(FALSE);
        VIFlush();
        return TRUE;
    }

    RwTextureSetReadCallBack(TextureRead);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLBACK);
    xShadowInit();
    xFXInit();
    RwTextureSetMipmapping(TRUE);
    RwTextureSetAutoMipmapping(TRUE);

    // Clear both frame buffers to black before the video is enabled.
    RwCamera* camera = iCameraCreate(FB_XRES, FB_YRES, 0);
    RwRGBA black = { 0, 0, 0, 0 };
    RwCameraClear(camera, &black, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
    RwCameraShowRaster(camera, NULL, 1);
    RwCameraShowRaster(camera, NULL, 1);
    iCameraDestroy(camera);

    VIWaitForRetrace();
    VISetBlack(FALSE);
    VIFlush();
    return FALSE;
}

void iSystemExitRWFailSafe()
{
    RwEngineStop();
    RwEngineClose();
}

static RwTexture* TextureRead(const RwChar* name, const RwChar* maskName)
{
    char tmpname[256];
    RwTexture* result;
    U32 assetid;
    U32 tmpsize;

    sprintf(tmpname, "%s.rw3", name);
    assetid = xStrHash(tmpname);
    result = (RwTexture*)xSTFindAsset(assetid, &tmpsize);
    if (result != NULL)
    {
        if (result->raster != NULL && result->raster->depth < 8)
        {
            RwGameCubeRasterExtension* ext = RWPLUGINOFFSET(
                RwGameCubeRasterExtension, result->raster, _RwGameCubeRasterExtOffset);
            if (ext == NULL || ext->format != GX_TF_CMPR)
            {
                result = NULL;
            }
        }
    }

    if (result != NULL)
    {
        strcpy(result->name, name);
        strcpy(result->mask, maskName);
    }

    return result;
}

// Ratatouille's signatures (char* malloc(long), void free(char*)).
extern "C" {

char* malloc(long msize)
{
    if (msize <= 0)
    {
        return NULL;
    }

    if (!memInited)
    {
        iMemInit();
    }

    return (char*)OSAllocFromHeap(the_heap, msize);
}

void free(char* p)
{
    if (p != NULL)
    {
        OSFreeToHeap(the_heap, p);
    }
}
}

S32 iGetMinute()
{
    OSCalendarTime td;
    OSTicksToCalendarTime(OSGetTime(), &td);
    return td.min;
}

S32 iGetHour()
{
    OSCalendarTime td;
    OSTicksToCalendarTime(OSGetTime(), &td);
    return td.hour;
}

S32 iGetDay()
{
    OSCalendarTime td;
    OSTicksToCalendarTime(OSGetTime(), &td);
    return td.mday;
}

S32 iGetMonth()
{
    OSCalendarTime td;
    OSTicksToCalendarTime(OSGetTime(), &td);
    return td.mon + 1;
}

U32 iGetCurrFormattedDate(char* str)
{
    char* start = str;
    OSTime ticks = OSGetTime();
    OSCalendarTime td;

    OSTicksToCalendarTime(ticks, &td);

    strcpy(str, dotw[td.wday]);
    strcat(str, months[td.mon]);
    str += strlen(str);

    if (td.mday >= 10)
    {
        *str++ = (td.mday / 10) + '0';
    }

    *str++ = (td.mday % 10) + '0';
    *str++ = ',';
    *str++ = ' ';
    *str++ = (td.year / 1000) + '0';
    *str++ = ((td.year / 100) % 10) + '0';
    *str++ = ((td.year / 10) % 100) + '0';
    *str++ = (td.year % 10) + '0';
    *str++ = '\0';

    return str - start;
}

U32 iGetCurrFormattedTime(char* str)
{
    char* start = str;
    S32 am = 0;

    OSTime ticks = OSGetTime();
    OSCalendarTime td;
    OSTicksToCalendarTime(ticks, &td);

    if (td.hour < 12)
    {
        am = 1;
    }
    else
    {
        td.hour -= 12;
    }

    if (td.hour == 0)
        td.hour = 12;

    if (td.hour >= 10)
    {
        *str++ = (td.hour / 10) + '0';
    }

    *str++ = (td.hour % 10) + '0';
    *str++ = ':';
    *str++ = (td.min / 10) + '0';
    *str++ = (td.min % 10) + '0';
    *str++ = ':';
    *str++ = (td.sec / 10) + '0';
    *str++ = (td.sec % 10) + '0';
    *str++ = ' ';

    if (am)
    {
        *str++ = 'A';
        *str++ = '.';
        *str++ = 'M';
        *str++ = '.';
    }
    else
    {
        *str++ = 'P';
        *str++ = '.';
        *str++ = 'M';
        *str++ = '.';
    }

    *str++ = '\0';

    return str - start;
}

void* iSystemARAMAlloc(U32 numbytes_to_use)
{
    numbytes_to_use = (numbytes_to_use + 31) & ~31;

    for (S32 i = 0; i < 31; i++)
    {
        if (!(AramMgr[i].flags & 1))
        {
            // Merge following free blocks until this one is big enough.
            for (S32 j = i + 1; numbytes_to_use > AramMgr[i].size; j++)
            {
                if (AramMgr[j].flags & 1)
                {
                    break;
                }

                AramMgr[i].size += AramMgr[j].size;
                AramMgr[j].size = 0;
                AramMgr[j].addr = AramMgr[i].addr + AramMgr[i].size;
            }

            if (numbytes_to_use <= AramMgr[i].size)
            {
                AramMgr[i].flags |= 1;
                AramMgr[i].size = numbytes_to_use;

                // Hand the remainder to the next block if it is free.
                if (!(AramMgr[i + 1].flags & 1))
                {
                    U32 end = AramMgr[i + 1].addr + AramMgr[i + 1].size;
                    AramMgr[i + 1].addr = AramMgr[i].addr + numbytes_to_use;
                    AramMgr[i + 1].size = end - AramMgr[i + 1].addr;
                }

                return (void*)AramMgr[i].addr;
            }
        }
    }

    return NULL;
}

void iSystemARAMFree(void* ptr)
{
    for (S32 i = 0; i < 31; i++)
    {
        if ((U32)ptr == AramMgr[i].addr)
        {
            AramMgr[i].flags &= ~1;
            return;
        }
    }
}

static U32 pARAMBaseAddressBink;
static U32 ARAMBytesForBink;
static U32 pARAMBaseAddressFMOD;
static U32 ARAMBytesForFMOD;

// No GC symbols (always inlined into iSndInit); names from the Ratatouille GC DWARF.
// ARAM is split between FMOD and the 32 blocks handed out by iSystemARAMAlloc (used by Bink).
inline void iSystemARAMInit()
{
    ARInit(NULL, 0);
    pARAMBaseAddressFMOD = ARGetBaseAddress();
    pARAMBaseAddressFMOD = (pARAMBaseAddressFMOD + 255) & ~255;
    ARAMBytesForFMOD = 0xD00000;
    pARAMBaseAddressBink = pARAMBaseAddressFMOD + ARAMBytesForFMOD;
    ARAMBytesForBink = 0x1000000 - pARAMBaseAddressBink;

    // Each block is 1/32 of the Bink area; retail masks the size with ~0xE0.
    for (S32 i = 0; i < 32; i++)
    {
        AramMgr[i].addr = pARAMBaseAddressBink + i * (ARAMBytesForBink / 32 & ~0xE0);
        AramMgr[i].size = ARAMBytesForBink / 32 & ~0xE0;
        AramMgr[i].flags = 0;
    }
}

inline U32 iSystemARAMBasePtrForFMOD()
{
    return pARAMBaseAddressFMOD;
}

inline U32 iSystemARAMSizeForFMOD()
{
    return ARAMBytesForFMOD;
}

// RenderWare debug message handler. Only RwDebugSetHandler installs it, which the release SDK
// compiles out, but its strings are in the string pool here. Name and parameters from the
// Ratatouille GC DWARF; the body is reconstructed.
static void NewRwDebugHandler(RwDebugType type, const RwChar* string)
{
    if (strstr(string, "bacamera.c") != NULL || strstr(string, "babinary.c") != NULL ||
        strstr(string, "baresour.c") != NULL)
    {
        return;
    }

    oldDebugHandler(type, string);
}

void iSystemInitRWFailSafe()
{
    RenderWareInit();

    switch (VIGetTvFormat())
    {
    case VI_NTSC:
        renderObject = myGXNtsc480IntDf;
        FB_XRES = 512;
        FB_YRES = 480;
        break;
    case VI_PAL:
        renderObject = myGXPal528IntDf;
        FB_XRES = 512;
        FB_YRES = 528;
        break;
    case VI_EURGB60:
        renderObject = myGXPal528IntDf;
        FB_XRES = 512;
        FB_YRES = 528;
        break;
    case VI_MPAL:
        renderObject = myGXNtsc480IntDf;
        FB_XRES = 512;
        FB_YRES = 480;
        break;
    default:
        renderObject = myGXNtsc480IntDf;
        FB_XRES = 512;
        FB_YRES = 480;
        break;
    }

    renderObject.fbWidth = FB_XRES;
    renderObject.efbHeight = FB_YRES;
    renderObject.xfbHeight = FB_YRES;

    deviceConfig.renderMode = &renderObject;
    deviceConfig.pixFmt = 1;
    deviceConfig.fifoSize = 0x20000;

    RwEngineOpenParams params;
    params.displayID = &deviceConfig;
    if (!RwEngineOpen(&params))
    {
        RwEngineTerm();
    }

    RwEngineGetVideoModeInfo(&sVideoMode, RwEngineGetCurrentVideoMode());
    if (!RwEngineStart())
    {
        RwEngineClose();
        RwEngineTerm();
    }
}

void iSystemAlarmCallBack(OSAlarm* alarm, OSContext* context)
{
    OSResumeThread(((iSystemAlarm*)alarm)->thread);
}

// No GC symbol (always inlined into loadSoundInfoAsset); name from the Ratatouille GC DWARF.
// Suspends the current thread until an alarm resumes it.
inline void iSystemSleep(S32 milliSecondsToSleep)
{
    iSystemAlarm gcAlarm;

    gcAlarm.thread = OSGetCurrentThread();
    OSCreateAlarm(&gcAlarm.alarm);
    OSSetAlarm(&gcAlarm.alarm, OSMillisecondsToTicks(milliSecondsToSleep), iSystemAlarmCallBack);
    OSSuspendThread(OSGetCurrentThread());
}
