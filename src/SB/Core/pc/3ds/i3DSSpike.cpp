// The libctru half of the 3DS speed test: what only libctru can ask (the
// model, the clock, the memory split, the CPU cores) and the two screens. See
// i3DS.h and iSpike.h; the run itself is iSpike.cpp.
//
// The libctru side (i3DS.h): nothing of the game's is included, and the file
// is compiled with libctru's short enums (CMakeLists.txt).

#include "i3DS.h"

#include <3ds.h>

#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/iosupport.h>
#include <sys/stat.h>

namespace
{
    void Add(char* out, size_t size, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

    void Add(char* out, size_t size, const char* fmt, ...)
    {
        const size_t used = strlen(out);
        if (used + 1 >= size)
        {
            return;
        }
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(out + used, size - used, fmt, ap);
        va_end(ap);
    }

    double Ms(u64 ticks)
    {
        return (double)ticks * 1000.0 / (double)SYSCLOCK_ARM11;
    }

    // ------------------------------------------------------------- the clock

    // A loop of n dependent subtract-and-branch steps, timed with the system
    // tick (which runs at 268 MHz whatever the CPU's speed). The same count on
    // every core and at both speeds, so only the ratios mean anything.
    __attribute__((target("arm"), noinline)) u64 LoopTicks(u32 n)
    {
        const u64 t0 = svcGetSystemTick();
        __asm__ volatile("1:\n\tsubs %0, %0, #1\n\tbne 1b\n" : "+r"(n) : : "cc");
        return svcGetSystemTick() - t0;
    }

    const u32 kLoop = 40000000;

    struct LoopJob
    {
        u64 ticks;
        s32 core;
    };

    void LoopThread(void* arg)
    {
        LoopJob* job = (LoopJob*)arg;
        job->core = svcGetProcessorID();
        job->ticks = LoopTicks(kLoop);
    }

    // A thread on `core` running the loop; FALSE when it could not be made.
    bool RunOn(int core, LoopJob* job, char* why, size_t whysize)
    {
        s32 prio = 0x30;
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        job->ticks = 0;
        job->core = -1;
        Thread t =
            threadCreate(LoopThread, job, 16 * 1024, prio > 0x18 ? prio - 1 : prio, core, false);
        if (t == NULL)
        {
            snprintf(why, whysize, "threadCreate failed");
            return false;
        }
        threadJoin(t, U64_MAX);
        threadFree(t);
        why[0] = '\0';
        return true;
    }

    const char* ModelName(u8 model)
    {
        switch (model)
        {
        case CFG_MODEL_3DS:
            return "3DS";
        case CFG_MODEL_3DSXL:
            return "3DS XL";
        case CFG_MODEL_N3DS:
            return "New 3DS";
        case CFG_MODEL_2DS:
            return "2DS";
        case CFG_MODEL_N3DSXL:
            return "New 3DS XL";
        case CFG_MODEL_N2DSXL:
            return "New 2DS XL";
        default:
            return "unknown";
        }
    }

    // ------------------------------------------------------------ the screens

    // SDL's video (iWindow3DS.cpp) sets both screens up when it starts
    // (N3DS_VideoInit: gfxInit, RGBA8, double-buffered), and the spike's first
    // lines come before that. So nothing here keeps a screen's format or
    // buffers: the top screen is drawn in whatever it has, and the bottom
    // one's console is set up again when its screen was set up afresh.

    // The top screen: our own drawing in double-size type. The framebuffer
    // is the screen turned on its side: 240 pixels a column, the columns
    // left to right, each column's pixels from the bottom up; in wide mode
    // 800 columns, each of ours drawn twice.
    const int kTopW = 400;
    const int kTopH = 240;
    const int kScale = 2;
    const int kCols = kTopW / (8 * kScale);
    const int kRows = kTopH / (8 * kScale);
    char sTopText[kRows][kCols + 1];
    int sTopCount;

    bool sScreens;

    // The top framebuffer libctru hands out now, and its layout.
    struct Top
    {
        u8* fb;
        GSPGPU_FramebufferFormat format;
        int bpp; // bytes a pixel
        int wide; // framebuffer columns to one of ours: 2 in wide mode
    };

    bool GetTop(Top* t)
    {
        u16 fbw = 0;
        u16 fbh = 0;
        t->fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fbw, &fbh);
        t->format = gfxGetScreenFormat(GFX_TOP);
        t->bpp = t->format == GSP_RGBA8_OES ? 4 : (t->format == GSP_BGR8_OES ? 3 : 2);
        t->wide = fbh >= 2 * kTopW ? 2 : 1;
        return t->fb != NULL && fbw >= kTopH && fbh >= kTopW;
    }

    // Copy `i` (0, or 1 in wide mode) of our pixel x, y (from the top left).
    u8* At(const Top& t, int x, int y, int i)
    {
        return t.fb + ((size_t)(x * t.wide + i) * kTopH + (kTopH - 1 - y)) * t.bpp;
    }

    // A pixel as it lies in memory, from 8-bit red, green and blue.
    struct Pixel
    {
        u8 b[4];
    };

    void Put(u8* q, const Pixel& p, int bpp)
    {
        q[0] = p.b[0];
        q[1] = p.b[1];
        if (bpp > 2)
        {
            q[2] = p.b[2];
        }
        if (bpp > 3)
        {
            q[3] = p.b[3];
        }
    }

    Pixel Pack(GSPGPU_FramebufferFormat format, u8 r, u8 g, u8 b)
    {
        Pixel p;
        u32 v;
        switch (format)
        {
        case GSP_RGBA8_OES: // a word 0xRRGGBBAA
            v = ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | 0xFF;
            break;
        case GSP_BGR8_OES: // the bytes B, G, R
            v = ((u32)r << 16) | ((u32)g << 8) | b;
            break;
        case GSP_RGB5_A1_OES:
            v = ((u32)(r >> 3) << 11) | ((u32)(g >> 3) << 6) | ((u32)(b >> 3) << 1) | 1;
            break;
        case GSP_RGBA4_OES:
            v = ((u32)(r >> 4) << 12) | ((u32)(g >> 4) << 8) | ((u32)(b >> 4) << 4) | 0xF;
            break;
        default: // GSP_RGB565_OES
            v = ((u32)(r >> 3) << 11) | ((u32)(g >> 2) << 5) | (u32)(b >> 3);
            break;
        }
        for (int i = 0; i < 4; i++)
        {
            p.b[i] = (u8)(v >> (8 * i));
        }
        return p;
    }

    // The other way, for the saved picture: rgb[0..2] = red, green, blue.
    void Unpack(GSPGPU_FramebufferFormat format, const u8* q, u8* rgb)
    {
        const u32 v = (u32)q[0] | ((u32)q[1] << 8);
        switch (format)
        {
        case GSP_RGBA8_OES:
            rgb[0] = q[3];
            rgb[1] = q[2];
            rgb[2] = q[1];
            break;
        case GSP_BGR8_OES:
            rgb[0] = q[2];
            rgb[1] = q[1];
            rgb[2] = q[0];
            break;
        case GSP_RGB5_A1_OES:
            rgb[0] = (u8)(((v >> 11) & 0x1F) << 3);
            rgb[1] = (u8)(((v >> 6) & 0x1F) << 3);
            rgb[2] = (u8)(((v >> 1) & 0x1F) << 3);
            break;
        case GSP_RGBA4_OES:
            rgb[0] = (u8)(((v >> 12) & 0xF) << 4);
            rgb[1] = (u8)(((v >> 8) & 0xF) << 4);
            rgb[2] = (u8)(((v >> 4) & 0xF) << 4);
            break;
        default:
            rgb[0] = (u8)(((v >> 11) & 0x1F) << 3);
            rgb[1] = (u8)(((v >> 5) & 0x3F) << 2);
            rgb[2] = (u8)((v & 0x1F) << 3);
            break;
        }
    }

    void Fill(const Top& t, const Pixel& p)
    {
        const size_t n = (size_t)kTopW * t.wide * kTopH;
        if (t.bpp == 4)
        {
            const u32 v =
                (u32)p.b[0] | ((u32)p.b[1] << 8) | ((u32)p.b[2] << 16) | ((u32)p.b[3] << 24);
            u32* d = (u32*)t.fb;
            for (size_t i = 0; i < n; i++)
            {
                d[i] = v;
            }
        }
        else if (t.bpp == 2)
        {
            const u16 v = (u16)(p.b[0] | (p.b[1] << 8));
            u16* d = (u16*)t.fb;
            for (size_t i = 0; i < n; i++)
            {
                d[i] = v;
            }
        }
        else
        {
            u8* d = t.fb;
            for (size_t i = 0; i < n; i++, d += 3)
            {
                d[0] = p.b[0];
                d[1] = p.b[1];
                d[2] = p.b[2];
            }
        }
    }

    // The bottom screen: libctru's console, 40 columns by 30 lines. It takes
    // stdout over (its devoptab entry), which would cut the game's log off,
    // so the log's entries are put back and the console is written to
    // directly. It draws RGB565 into the buffer it was handed at consoleInit,
    // so it is set up again (with its text) when the screen no longer shows
    // that buffer in that format.
    PrintConsole sBottomConsole;
    const devoptab_t* sConsoleDev;
    char sBottomText[1200]; // the latest of it, whole lines, for that
    size_t sBottomLen;

    void StartConsole()
    {
        const devoptab_t* out = devoptab_list[STD_OUT];
        const devoptab_t* err = devoptab_list[STD_ERR];
        consoleInit(GFX_BOTTOM, &sBottomConsole);
        // The first consoleInit installs the console's device; later ones
        // leave the table alone.
        if (sConsoleDev == NULL)
        {
            sConsoleDev = devoptab_list[STD_OUT];
        }
        devoptab_list[STD_OUT] = out;
        devoptab_list[STD_ERR] = err;
    }

    void BottomWrite(const char* text, size_t n)
    {
        if (sConsoleDev == NULL || sConsoleDev->write_r == NULL || n == 0)
        {
            return;
        }
        // The console's own write, not stdout's.
        consoleSelect(&sBottomConsole);
        sConsoleDev->write_r(_REENT, NULL, text, n);
    }

    void KeepText(const char* text, size_t n)
    {
        const size_t cap = sizeof(sBottomText);
        if (n > cap)
        {
            text += n - cap;
            n = cap;
        }
        if (sBottomLen + n > cap)
        {
            // The oldest lines go, whole.
            size_t drop = sBottomLen + n - cap;
            while (drop < sBottomLen && sBottomText[drop - 1] != '\n')
            {
                drop++;
            }
            memmove(sBottomText, sBottomText + drop, sBottomLen - drop);
            sBottomLen -= drop;
        }
        memcpy(sBottomText + sBottomLen, text, n);
        sBottomLen += n;
    }

    void KeepBottom()
    {
        // Single-buffered, as consoleInit leaves it: libctru then hands out
        // the buffer on screen, so a bottom screen left double-buffered and
        // flipped cannot pass the check below while showing the other one.
        gfxSetDoubleBuffering(GFX_BOTTOM, false);
        const u8* fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        if (fb == NULL || (gfxGetScreenFormat(GFX_BOTTOM) == GSP_RGB565_OES &&
                           (const u16*)fb == sBottomConsole.frameBuffer))
        {
            return;
        }
        StartConsole();
        BottomWrite(sBottomText, sBottomLen);
    }

    void SetUpScreens()
    {
        if (sScreens)
        {
            return;
        }
        sScreens = true;

        // Before SDL's video starts, the spike sets the screens up itself, as
        // SDL will: then SDL's gfxInit finds the buffers big enough and keeps
        // them, and the console's never goes away under it.
        if (!gspHasGpuRight())
        {
            gfxInit(GSP_RGBA8_OES, GSP_RGBA8_OES, false);
        }
        StartConsole();
    }

    // Draws the text into the buffer libctru hands out; with `show`, presents
    // it and draws it again into the buffer handed out next, so the picture
    // is the same whether the top screen is single- or double-buffered.
    void DrawTop(bool show = true);

    void DrawTopInto()
    {
        Top t;
        if (!GetTop(&t))
        {
            return;
        }
        Fill(t, Pack(t.format, 0x00, 0x00, 0x42)); // near black, a little blue
        const Pixel fg = Pack(t.format, 0xFF, 0xFF, 0xFF); // white
        const Pixel head = Pack(t.format, 0xFF, 0xFF, 0x00); // yellow, the first line

        const PrintConsole* def = consoleGetDefault();
        const u8* font = def->font.gfx;
        for (int row = 0; row < sTopCount && row < kRows; row++)
        {
            const Pixel& color = row == 0 ? head : fg;
            for (int col = 0; col < kCols && sTopText[row][col] != '\0'; col++)
            {
                const int c = (unsigned char)sTopText[row][col] - def->font.asciiOffset;
                if (c < 0 || c >= def->font.numChars)
                {
                    continue;
                }
                const u8* glyph = font + 8 * c;
                for (int gy = 0; gy < 8; gy++)
                {
                    const u8 bits = glyph[gy];
                    for (int gx = 0; gx < 8; gx++)
                    {
                        if (!(bits & (0x80 >> gx)))
                        {
                            continue;
                        }
                        for (int sy = 0; sy < kScale; sy++)
                        {
                            for (int sx = 0; sx < kScale; sx++)
                            {
                                const int x = (col * 8 + gx) * kScale + sx;
                                const int y = (row * 8 + gy) * kScale + sy;
                                for (int i = 0; i < t.wide; i++)
                                {
                                    Put(At(t, x, y, i), color, t.bpp);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    void DrawTop(bool show)
    {
        SetUpScreens();
        KeepBottom();
        DrawTopInto();
        if (show)
        {
            gfxFlushBuffers();
            gfxScreenSwapBuffers(GFX_TOP, false);
            DrawTopInto();
            gfxFlushBuffers();
        }
    }

    // ------------------------------------------------------------- the threads

    // A thread made for `core`, as svcGetProcessorID placed it. Azahar does
    // not implement that call (the number is whatever was in the register).
    // On a New 3DS, Luma3DS's "redirect app. syscore threads to core2"
    // setting runs an app's core-1 threads on core 2.
    void AddCore(char* out, size_t size, int core, const LoopJob& job, u64 main, const char* note)
    {
        const double x = main > 0 ? (double)job.ticks / (double)main : 0.0;
        if (job.core == core)
        {
            Add(out, size, "threads: core %d: YES, the thread ran there, loop %.1f ms (x%.2f%s)\n",
                core, Ms(job.ticks), x, note);
        }
        else if (job.core >= 0 && job.core <= 3)
        {
            Add(out, size,
                "threads: core %d: MOVED, the thread made for it ran on core %d, loop %.1f ms "
                "(x%.2f), so core %d itself was not measured%s\n",
                core, (int)job.core, Ms(job.ticks), x, core,
                core == 1 && job.core == 2 ?
                    " (Luma3DS's \"redirect app. syscore threads to core2\" setting does that)" :
                    "");
        }
        else
        {
            Add(out, size,
                "threads: core %d: YES?, a thread made for it ran, on a core svcGetProcessorID "
                "did not say (%ld), loop %.1f ms (x%.2f%s)\n",
                core, (long)job.core, Ms(job.ticks), x, note);
        }
    }
} // namespace

void i3DSSpikeConsole(char* out, size_t size)
{
    bool isNew = false;
    APT_CheckNew3DS(&isNew);
    u8 model = 0xFF;
    if (R_SUCCEEDED(cfguInit()))
    {
        CFGU_GetSystemModel(&model);
        cfguExit();
    }
    Add(out, size, "model: %s (APT says %s)\n", ModelName(model),
        isNew ? "New 3DS family" : "not a New 3DS");
    Add(out, size, "system: %lu.%lu.%lu, kernel %lu.%lu\n",
        (unsigned long)GET_VERSION_MAJOR(osGetFirmVersion()),
        (unsigned long)GET_VERSION_MINOR(osGetFirmVersion()),
        (unsigned long)GET_VERSION_REVISION(osGetFirmVersion()),
        (unsigned long)GET_VERSION_MAJOR(osGetKernelVersion()),
        (unsigned long)GET_VERSION_MINOR(osGetKernelVersion()));

    // The clock: libctru cannot read the setting back, so the same loop runs
    // at the slow speed and then at the fast one. 804 MHz against 268 is a
    // ratio of 3.
    osSetSpeedupEnable(false);
    svcSleepThread(20000000ll);
    const u64 slow = LoopTicks(kLoop);
    osSetSpeedupEnable(true);
    svcSleepThread(20000000ll);
    const u64 fast = LoopTicks(kLoop);
    const double ratio = fast > 0 ? (double)slow / (double)fast : 0.0;
    Add(out, size,
        "clock: a %lu-step loop takes %.1f ms at the start-up speed, %.1f ms with the "
        "speed-up on (x%.2f: %s)\n",
        (unsigned long)kLoop, Ms(slow), Ms(fast), ratio,
        ratio > 2.6 ? "804 MHz is on" : (ratio > 1.3 ? "faster, but not x3" : "NOT faster"));
    Add(out, size, "clock: so the core runs about %.0f MHz if the loop is 1 cycle a step\n",
        fast > 0 ? (double)kLoop * (double)SYSCLOCK_ARM11 / (double)fast / 1e6 : 0.0);

    const struct mallinfo mi = mallinfo();
    Add(out, size,
        "memory: application region %lu KB, %lu KB used, %lu KB free (as the kernel says)\n",
        (unsigned long)(osGetMemRegionSize(MEMREGION_APPLICATION) >> 10),
        (unsigned long)(osGetMemRegionUsed(MEMREGION_APPLICATION) >> 10),
        (unsigned long)(osGetMemRegionFree(MEMREGION_APPLICATION) >> 10));
    Add(out, size, "memory: libctru heap %lu KB, linear heap %lu KB (%lu KB of it free)\n",
        (unsigned long)(envGetHeapSize() >> 10), (unsigned long)(envGetLinearHeapSize() >> 10),
        (unsigned long)(linearSpaceFree() >> 10));
    Add(out, size, "memory: malloc %lu KB in use, %lu KB taken from the heap so far\n",
        (unsigned long)(mi.uordblks >> 10), (unsigned long)(mi.arena >> 10));
}

void i3DSSpikeThreads(char* out, size_t size)
{
    const u64 main = LoopTicks(kLoop);
    const s32 mainCore = svcGetProcessorID();
    if (mainCore >= 0 && mainCore <= 3)
    {
        Add(out, size, "threads: the main thread (core %d) runs the loop in %.1f ms\n",
            (int)mainCore, Ms(main));
    }
    else
    {
        Add(out, size, "threads: the main thread (core not said: %ld) runs the loop in %.1f ms\n",
            (long)mainCore, Ms(main));
    }

    char why[64];
    LoopJob job;
    if (RunOn(2, &job, why, sizeof(why)))
    {
        AddCore(out, size, 2, job, main, "");
    }
    else
    {
        Add(out, size, "threads: core 2: NO (%s)\n", why);
    }

    // Core 1 is the system's; an app gets a share of it once it asks.
    u32 before = 0;
    APT_GetAppCpuTimeLimit(&before);
    const Result r = APT_SetAppCpuTimeLimit(30);
    Add(out, size, "threads: APT_SetAppCpuTimeLimit(30): %s (was %lu%%)\n",
        R_SUCCEEDED(r) ? "ok" : "failed", (unsigned long)before);
    if (RunOn(1, &job, why, sizeof(why)))
    {
        AddCore(out, size, 1, job, main, "; 30% of the core would be x3.3");
    }
    else
    {
        Add(out, size, "threads: core 1: NO (%s)\n", why);
    }
}

void i3DSSpikeMemory(uint32_t* appFree, uint32_t* linearFree, uint32_t* mallocUsed,
                     uint32_t* mallocArena)
{
    const struct mallinfo mi = mallinfo();
    *appFree = osGetMemRegionFree(MEMREGION_APPLICATION);
    *linearFree = linearSpaceFree();
    *mallocUsed = (uint32_t)mi.uordblks;
    *mallocArena = (uint32_t)mi.arena;
}

int i3DSSpikeHasDsp()
{
    struct stat st;
    return stat("sdmc:/3ds/dspfirm.cdc", &st) == 0;
}

void i3DSSpikeTop(const char* const* lines, int count)
{
    sTopCount = count < kRows ? count : kRows;
    for (int i = 0; i < sTopCount; i++)
    {
        snprintf(sTopText[i], sizeof(sTopText[i]), "%s", lines[i] != NULL ? lines[i] : "");
    }
    DrawTop();
}

void i3DSSpikeBottom(const char* text)
{
    if (text == NULL)
    {
        return;
    }
    SetUpScreens();
    const size_t n = strlen(text);
    // After SDL's video has quit (gfxExit) there is no bottom buffer, and
    // the console's old one is freed: the text is only kept.
    if (gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL) != NULL)
    {
        KeepBottom();
        BottomWrite(text, n);
        gfxFlushBuffers();
    }
    KeepText(text, n);
}

void i3DSSpikeScreensReset()
{
    if (sScreens)
    {
        DrawTop();
    }
}

int i3DSSpikeSaveTop(const char* path)
{
    // The picture is drawn afresh into the buffer about to be shown, saved
    // from there, then shown: the one on screen is not the one libctru
    // hands out. Read back in the screen's own format, as it is shown.
    DrawTop(false);
    Top t;
    if (!GetTop(&t))
    {
        return 0;
    }
    FILE* f = fopen(path, "wb");
    if (f == NULL)
    {
        return 0;
    }
    // A 24-bit BMP, bottom row first, as the format has it.
    const u32 row = kTopW * 3;
    const u32 size = 54 + row * kTopH;
    u8 head[54];
    memset(head, 0, sizeof(head));
    head[0] = 'B';
    head[1] = 'M';
    const u32 fields[] = { size, 0, 54, 40, (u32)kTopW, (u32)kTopH };
    for (int i = 0; i < 6; i++)
    {
        for (int b = 0; b < 4; b++)
        {
            head[2 + i * 4 + b] = (u8)(fields[i] >> (8 * b));
        }
    }
    head[26] = 1;
    head[28] = 24;
    fwrite(head, 1, sizeof(head), f);
    static u8 line[kTopW * 3];
    for (int y = kTopH - 1; y >= 0; y--)
    {
        for (int x = 0; x < kTopW; x++)
        {
            u8 rgb[3];
            Unpack(t.format, At(t, x, y, 0), rgb);
            line[x * 3 + 0] = rgb[2];
            line[x * 3 + 1] = rgb[1];
            line[x * 3 + 2] = rgb[0];
        }
        fwrite(line, 1, row, f);
    }
    fclose(f);
    DrawTop(true);
    return 1;
}

void i3DSSpikeWaitStart()
{
    SetUpScreens();
    int frames = 0;
    while (aptMainLoop())
    {
        hidScanInput();
        if (hidKeysDown() & KEY_START)
        {
            break;
        }
        // Drawn again now and then: coming back from HOME or sleep, the
        // screens are the system's to restore, and a redraw costs nothing here.
        if (++frames % 60 == 0)
        {
            DrawTop();
        }
        gspWaitForVBlank();
    }
}

void i3DSSpikeYield()
{
    svcSleepThread(100000ll);
}
