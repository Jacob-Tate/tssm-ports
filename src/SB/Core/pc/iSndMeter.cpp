#include "iSndMeter.h"
#include "iHost.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace
{
    // Counters written by several threads and read and cleared by the game
    // thread, so every access is atomic.
    volatile long sFrames[iSNDMETER_COUNT];
    volatile long sPeak[iSNDMETER_COUNT]; // 0..32768

    int sOn = -1;
    U64 sLastNs;
    U64 sStartNs;

    long Exchange(volatile long* p, long v)
    {
#ifdef _MSC_VER
        return _InterlockedExchange(p, v);
#else
        return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST);
#endif
    }

    void Add(volatile long* p, long v)
    {
#ifdef _MSC_VER
        _InterlockedExchangeAdd(p, v);
#else
        __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST);
#endif
    }

    void Max(volatile long* p, long v)
    {
#ifdef _MSC_VER
        long old = *p;
        while (v > old)
        {
            long seen = _InterlockedCompareExchange(p, v, old);
            if (seen == old)
            {
                break;
            }
            old = seen;
        }
#else
        long old = __atomic_load_n(p, __ATOMIC_SEQ_CST);
        while (v > old &&
               !__atomic_compare_exchange_n(p, &old, v, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        {
        }
#endif
    }

    const char* const kNames[iSNDMETER_COUNT] = { "movie",     "movie out",  "cutscene", "stream",
                                                  "mix feeds", "mix sounds", "out" };
} // namespace

bool iSndMeterOn()
{
    if (sOn < 0)
    {
        const char* env = getenv("TSSM_SND_METER");
        sOn = (env != NULL && env[0] != '\0' && env[0] != '0') ? 1 : 0;
    }
    return sOn != 0;
}

void iSndMeterS16(S32 source, const S16* data, U32 frames, U32 channels)
{
    if (!iSndMeterOn() || source < 0 || source >= iSNDMETER_COUNT || data == NULL)
    {
        return;
    }

    long peak = 0;
    for (U32 i = 0; i < frames * channels; i++)
    {
        long v = data[i] < 0 ? -(long)data[i] : (long)data[i];
        if (v > peak)
        {
            peak = v;
        }
    }

    Add(&sFrames[source], (long)frames);
    Max(&sPeak[source], peak);
}

void iSndMeterF32(S32 source, const float* data, U32 frames, U32 channels)
{
    if (!iSndMeterOn() || source < 0 || source >= iSNDMETER_COUNT || data == NULL)
    {
        return;
    }

    float peak = 0.0f;
    for (U32 i = 0; i < frames * channels; i++)
    {
        float v = data[i] < 0.0f ? -data[i] : data[i];
        if (v > peak)
        {
            peak = v;
        }
    }

    Add(&sFrames[source], (long)frames);
    Max(&sPeak[source], (long)(peak >= 1.0f ? 32768.0f : peak * 32768.0f));
}

void iSndMeterFrames(S32 source, U32 frames)
{
    if (!iSndMeterOn() || source < 0 || source >= iSNDMETER_COUNT)
    {
        return;
    }

    Add(&sFrames[source], (long)frames);
}

bool iSndMeterTick(const char* clock)
{
    if (!iSndMeterOn())
    {
        return false;
    }

    U64 now = iHostMonotonicNs();
    if (sStartNs == 0)
    {
        // The first line covers one second, not everything since sound started.
        for (S32 i = 0; i < iSNDMETER_COUNT; i++)
        {
            Exchange(&sFrames[i], 0);
            Exchange(&sPeak[i], 0);
        }
        sStartNs = now;
        sLastNs = now;
        return false;
    }
    if (now - sLastNs < 1000000000ULL)
    {
        return false;
    }

    char line[512];
    int len = snprintf(line, sizeof(line),
                       "tssm: snd meter %.1f s (%.2f s):", (double)(now - sStartNs) / 1e9,
                       (double)(now - sLastNs) / 1e9);
    sLastNs = now;

    for (S32 i = 0; i < iSNDMETER_COUNT && len > 0 && len < (int)sizeof(line); i++)
    {
        long frames = Exchange(&sFrames[i], 0);
        long peak = Exchange(&sPeak[i], 0);
        len += snprintf(line + len, sizeof(line) - (size_t)len, "%s %s %ld/%.3f", i == 0 ? "" : ",",
                        kNames[i], frames, (double)peak / 32768.0);
    }

    printf("%s%s%s\n", line, clock != NULL ? "; " : "", clock != NULL ? clock : "");
    fflush(stdout);
    return true;
}
