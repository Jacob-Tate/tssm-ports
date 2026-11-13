// The leak ladder's control (probe 11, docs/3DS.md): a plain libctru program
// with nothing of the port's in it. At start it measures what earlier
// programs left behind, as the port's start-up held line does, appends one
// line to sdmc:/3ds/tssm/leakladder-control.log, shows it for 3 s and quits
// back to the Homebrew Launcher. Started twice in one power-on, the second
// line says whether the first start's process was freed.
//
// Built by build.sh beside it (devkitARM, libctru): probe-11-control.3dsx.

#include <3ds.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

extern char __start__[];
extern char __end__[];

typedef struct
{
    int processes; // -1 when the kernel would not list them
    int sameName; // other processes named as this one (3dsx_app for a .3dsx), -1 unknown
    char names[160];
} Count;

// The kernel's process objects, and of them those named as this one
// (Luma3DS's svcGetProcessInfo type 0x10000: the code set's name).
static void CountProcesses(Count* c)
{
    c->processes = -1;
    c->sameName = -1;
    c->names[0] = '\0';
    u32 pids[64];
    s32 n = 0;
    if (R_FAILED(svcGetProcessList(&n, pids, 64)) || n < 1 || n > 64)
    {
        return;
    }
    c->processes = (int)n;
    u32 me = 0;
    s64 myName = 0;
    svcGetProcessId(&me, CUR_PROCESS_HANDLE);
    if (R_FAILED(svcGetProcessInfo(&myName, CUR_PROCESS_HANDLE, 0x10000)) || myName == 0)
    {
        return;
    }
    int same = 0;
    size_t len = 0;
    for (s32 i = 0; i < n; i++)
    {
        if (pids[i] == me)
        {
            continue;
        }
        Handle p = 0;
        s64 name = 0;
        if (R_FAILED(svcOpenProcess(&p, pids[i])))
        {
            continue;
        }
        const Result r = svcGetProcessInfo(&name, p, 0x10000);
        // Closed at once: a handle kept here would itself keep that process.
        svcCloseHandle(p);
        if (R_SUCCEEDED(r) && name == myName)
        {
            same++;
            if (len < sizeof(c->names) - 16)
            {
                len += (size_t)snprintf(c->names + len, sizeof(c->names) - len, "%s%lu",
                                        same > 1 ? ", " : "", (unsigned long)pids[i]);
            }
        }
    }
    c->sameName = same;
}

int main(void)
{
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);

    // The program before (the Homebrew Launcher) may still be on its way
    // out: counted again for up to 3 s before it counts as held.
    Count c;
    CountProcesses(&c);
    int held = c.sameName;
    for (int i = 0; i < 6 && held > 0; i++)
    {
        svcSleepThread(500000000ll);
        Count again;
        CountProcesses(&again);
        if (again.sameName >= 0 && again.sameName < held)
        {
            held = again.sameName;
            c = again;
        }
    }

    // libctru split the memory that was free at start into the two heaps;
    // what was not free is this program's image and whatever earlier
    // programs still hold.
    const u32 region = osGetMemRegionSize(MEMREGION_APPLICATION);
    const u32 heaps = envGetHeapSize() + envGetLinearHeapSize();
    const u32 notFreeKB = region > heaps ? (region - heaps) >> 10 : 0;
    const u32 image = ((u32)(uintptr_t)__end__ - (u32)(uintptr_t)__start__ + 0xFFF) & ~0xFFFu;
    const u32 ownKB = (image >> 10) + 8;
    const u32 extraKB = notFreeKB > ownKB ? notFreeKB - ownKB : 0;
    const u32 seconds = (u32)(svcGetSystemTick() / SYSCLOCK_ARM11);

    char line[600];
    time_t t = time(NULL);
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    localtime_r(&t, &tm);
    snprintf(line, sizeof(line),
             "%04d-%02d-%02d %02d:%02d:%02d  control (built %s %s): %lu s since power-on; %d "
             "process(es) on the console, %d of them named as this one besides it%s%s%s; not free "
             "at start %lu KB (this program %lu KB); held by earlier programs: %d by name, %lu KB "
             "by the memory (the port's image is about 9304 KB)",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
             __DATE__, __TIME__, (unsigned long)seconds, c.processes, c.sameName,
             c.names[0] != '\0' ? " (ids " : "", c.names, c.names[0] != '\0' ? ")" : "",
             (unsigned long)notFreeKB, (unsigned long)ownKB, held, (unsigned long)extraKB);

    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/tssm", 0777);
    FILE* f = fopen("sdmc:/3ds/tssm/leakladder-control.log", "a");
    if (f != NULL)
    {
        fprintf(f, "%s\n", line);
        fclose(f);
    }

    printf("\n TSSM probe 11 control\n\n");
    printf(" A plain libctru program: it measures\n what earlier programs left in memory\n"
           " and quits.\n\n");
    printf(" Held by earlier programs: %d\n (%lu KB not given back)\n\n", held,
           (unsigned long)extraKB);
    printf(" %d processes on the console.\n\n", c.processes);
    printf(" Log: 3ds/tssm/leakladder-control.log\n\n");
    printf(" Back to the Homebrew Launcher\n in a few seconds.\n");

    for (int i = 0; i < 180 && aptMainLoop(); i++)
    {
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    gfxExit();
    return 0;
}
