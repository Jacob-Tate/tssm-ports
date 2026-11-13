// The game thread's sampling profiler; see iPerfSampler.h. Port-only.

#include "iPerfSampler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) && (defined(__aarch64__) || defined(__x86_64__) || defined(__arm__))

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#ifndef SIGEV_THREAD_ID
#define SIGEV_THREAD_ID 4
#endif
#ifndef sigev_notify_thread_id
#define sigev_notify_thread_id _sigev_un._tid
#endif

namespace
{
    const int kDepth = 48; // addresses per sample: PC, LR, then the frame chain

    uint64_t* sBuf;
    size_t sCap; // words
    volatile size_t sUsed; // words
    volatile unsigned sSamples;
    volatile unsigned sLost; // the buffer was full
    volatile int sPhase;
    uintptr_t sStackLo, sStackHi;
    timer_t sTimer;
    bool sRunning;
    bool sWritten;
    int sHz;
    int sTid;
    char sOut[512];

    bool InStack(uintptr_t p)
    {
        return p >= sStackLo && p + 2 * sizeof(uintptr_t) <= sStackHi &&
               (p & (sizeof(uintptr_t) - 1)) == 0;
    }

    void OnProf(int, siginfo_t*, void* ctx)
    {
        const int savedErrno = errno;
        ucontext_t* uc = (ucontext_t*)ctx;
        uintptr_t pc, lr, fp;
#if defined(__aarch64__)
        pc = (uintptr_t)uc->uc_mcontext.pc;
        lr = (uintptr_t)uc->uc_mcontext.regs[30];
        fp = (uintptr_t)uc->uc_mcontext.regs[29];
#elif defined(__x86_64__)
        pc = (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
        lr = 0;
        fp = (uintptr_t)uc->uc_mcontext.gregs[REG_RBP];
#else
        // 32-bit ARM: Thumb and ARM code keep their frame records in different
        // registers, so the chain is not walked; PC and LR only.
        pc = (uintptr_t)uc->uc_mcontext.arm_pc;
        lr = (uintptr_t)uc->uc_mcontext.arm_lr;
        fp = 0;
#endif
        const size_t used = sUsed;
        if (used + 1 + kDepth > sCap)
        {
            sLost++;
            errno = savedErrno;
            return;
        }
        uint64_t* out = sBuf + used + 1;
        int n = 0;
        out[n++] = pc;
        out[n++] = lr;
        // The frame chain: each record is {previous fp, return address}, and
        // each lies higher on the stack than the one before.
        while (n < kDepth && InStack(fp))
        {
            const uintptr_t* rec = (const uintptr_t*)fp;
            const uintptr_t ret = rec[1];
            const uintptr_t next = rec[0];
            if (ret == 0)
            {
                break;
            }
            out[n++] = ret;
            if (next <= fp)
            {
                break;
            }
            fp = next;
        }
        sBuf[used] = ((uint64_t)(unsigned)sPhase << 8) | (uint64_t)n;
        sUsed = used + 1 + n;
        sSamples++;
        errno = savedErrno;
    }

    uint64_t HashWords(const uint64_t* w, size_t n)
    {
        uint64_t h = 1469598103934665603ULL;
        for (size_t i = 0; i < n; i++)
        {
            h ^= w[i];
            h *= 1099511628211ULL;
        }
        return h;
    }

    struct Stack
    {
        size_t at; // offset of the sample's header in sBuf
        unsigned count;
    };

    void Stop()
    {
        if (sRunning)
        {
            timer_delete(sTimer);
            sRunning = false;
        }
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGPROF);
        pthread_sigmask(SIG_BLOCK, &set, NULL);
    }
} // namespace

S32 iPerfSamplerStart(S32 hz, const char* outPath)
{
    if (sBuf != NULL || hz <= 0)
    {
        return sBuf != NULL;
    }
    sHz = hz > 10000 ? 10000 : hz;
    snprintf(sOut, sizeof(sOut), "%s", outPath);

    // 64 MB of words, enough for over 150000 deep samples.
    sCap = (64u << 20) / sizeof(uint64_t);
    sBuf = (uint64_t*)malloc(sCap * sizeof(uint64_t));
    if (sBuf == NULL)
    {
        printf("tssm: perf: no memory for the profiler's samples\n");
        return FALSE;
    }

    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) == 0)
    {
        void* lo = NULL;
        size_t size = 0;
        pthread_attr_getstack(&attr, &lo, &size);
        sStackLo = (uintptr_t)lo;
        sStackHi = (uintptr_t)lo + size;
        pthread_attr_destroy(&attr);
    }
    sTid = (int)syscall(SYS_gettid);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = OnProf;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPROF, &sa, NULL) != 0)
    {
        printf("tssm: perf: SIGPROF handler refused (%s)\n", strerror(errno));
        return FALSE;
    }

    struct sigevent sev;
    memset(&sev, 0, sizeof(sev));
    sev.sigev_notify = SIGEV_THREAD_ID;
    sev.sigev_signo = SIGPROF;
    sev.sigev_notify_thread_id = sTid;
    if (timer_create(CLOCK_THREAD_CPUTIME_ID, &sev, &sTimer) != 0)
    {
        printf("tssm: perf: no CPU-time timer for the game thread (%s)\n", strerror(errno));
        return FALSE;
    }
    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 1000000000L / sHz;
    its.it_value = its.it_interval;
    if (timer_settime(sTimer, 0, &its, NULL) != 0)
    {
        printf("tssm: perf: the profiler's timer did not start (%s)\n", strerror(errno));
        timer_delete(sTimer);
        return FALSE;
    }
    sRunning = true;
    atexit(iPerfSamplerWrite);
    printf("tssm: perf: profiling thread %d at %d Hz of its CPU time (stack %p-%p) into %s\n", sTid,
           sHz, (void*)sStackLo, (void*)sStackHi, sOut);
    fflush(stdout);
    return TRUE;
}

void iPerfSamplerSetPhase(S32 phase)
{
    sPhase = phase;
}

void iPerfSamplerWrite()
{
    if (sBuf == NULL || sWritten)
    {
        return;
    }
    Stop();
    sWritten = true;

    FILE* f = fopen(sOut, "w");
    if (f == NULL)
    {
        printf("tssm: perf: could not write %s\n", sOut);
        return;
    }
    char name[64] = "";
    {
        char path[64];
        snprintf(path, sizeof(path), "/proc/self/task/%d/comm", sTid);
        FILE* c = fopen(path, "r");
        if (c != NULL)
        {
            if (fgets(name, sizeof(name), c) != NULL)
            {
                name[strcspn(name, "\r\n")] = 0;
            }
            fclose(c);
        }
    }
    fprintf(f, "# tssm perf samples 1\n");
    fprintf(f, "hz %d\nthread %d %s\nsamples %u\nlost %u\n", sHz, sTid, name, sSamples, sLost);

    // Identical stacks merged.
    const unsigned n = sSamples;
    size_t tableSize = 1;
    while (tableSize < (size_t)n * 2 + 16)
    {
        tableSize <<= 1;
    }
    Stack* table = (Stack*)calloc(tableSize, sizeof(Stack));
    size_t at = 0;
    for (unsigned i = 0; i < n && table != NULL; i++)
    {
        const size_t words = 1 + (size_t)(sBuf[at] & 0xff);
        size_t h = (size_t)HashWords(sBuf + at, words) & (tableSize - 1);
        while (table[h].count != 0 &&
               ((sBuf[table[h].at] != sBuf[at]) ||
                memcmp(sBuf + table[h].at, sBuf + at, words * sizeof(uint64_t)) != 0))
        {
            h = (h + 1) & (tableSize - 1);
        }
        if (table[h].count == 0)
        {
            table[h].at = at;
        }
        table[h].count++;
        at += words;
    }

    // The executable mappings, for the libraries.
    fprintf(f, "maps\n");
    FILE* m = fopen("/proc/self/maps", "r");
    if (m != NULL)
    {
        char line[1024];
        while (fgets(line, sizeof(line), m) != NULL)
        {
            char perms[8] = "";
            if (sscanf(line, "%*s %7s", perms) == 1 && strchr(perms, 'x') != NULL)
            {
                fputs(line, f);
            }
        }
        fclose(m);
    }
    fprintf(f, "endmaps\n");

    // dladdr's names for every distinct address outside this library (the
    // host symbolizes this one from its unstripped copy).
    Dl_info self;
    memset(&self, 0, sizeof(self));
    dladdr((void*)iPerfSamplerStart, &self);
    fprintf(f, "self %s %p\n", self.dli_fname ? self.dli_fname : "?", self.dli_fbase);
    fprintf(f, "names\n");
    size_t setSize = 1;
    while (setSize < 65536)
    {
        setSize <<= 1;
    }
    uint64_t* seen = (uint64_t*)calloc(setSize, sizeof(uint64_t));
    at = 0;
    for (unsigned i = 0; i < n && seen != NULL; i++)
    {
        const size_t count = (size_t)(sBuf[at] & 0xff);
        for (size_t k = 0; k < count; k++)
        {
            const uint64_t a = sBuf[at + 1 + k];
            if (a == 0)
            {
                continue;
            }
            size_t h = (size_t)((a * 0x9E3779B97F4A7C15ULL) >> 20) & (setSize - 1);
            size_t probes = 0;
            while (seen[h] != 0 && seen[h] != a && probes < setSize)
            {
                h = (h + 1) & (setSize - 1);
                probes++;
            }
            if (seen[h] == a || probes == setSize)
            {
                continue;
            }
            seen[h] = a;
            Dl_info info;
            if (dladdr((void*)(uintptr_t)a, &info) == 0 || info.dli_fname == NULL)
            {
                continue;
            }
            if (self.dli_fname != NULL && strcmp(info.dli_fname, self.dli_fname) == 0)
            {
                continue;
            }
            fprintf(f, "%llx %s %s %llx\n", (unsigned long long)a, info.dli_fname,
                    info.dli_sname ? info.dli_sname : "?",
                    (unsigned long long)(info.dli_saddr ? a - (uint64_t)(uintptr_t)info.dli_saddr :
                                                          0));
        }
        at += 1 + count;
    }
    free(seen);
    fprintf(f, "endnames\n");

    // count phase addresses...
    fprintf(f, "stacks\n");
    for (size_t h = 0; h < tableSize && table != NULL; h++)
    {
        if (table[h].count == 0)
        {
            continue;
        }
        const uint64_t* s = sBuf + table[h].at;
        const size_t count = (size_t)(s[0] & 0xff);
        fprintf(f, "%u %u", table[h].count, (unsigned)(s[0] >> 8));
        for (size_t k = 0; k < count; k++)
        {
            fprintf(f, " %llx", (unsigned long long)s[1 + k]);
        }
        fputc('\n', f);
    }
    fprintf(f, "end\n");
    free(table);
    fclose(f);
    printf("tssm: perf: %u samples (%u lost) written to %s\n", sSamples, sLost, sOut);
    fflush(stdout);
}

#else

S32 iPerfSamplerStart(S32, const char*)
{
    printf("tssm: perf: TSSM_PERF_PROFILE works on Linux and Android only\n");
    return FALSE;
}

void iPerfSamplerSetPhase(S32)
{
}

void iPerfSamplerWrite()
{
}

#endif
