// The switches of iGpuOpt.h (TSSM_GPU_OPT) and the shadow pre-pass's count.
// Port-only.

#include "iGpuOpt.h"

#include <rwcore.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    S32 sOff = -1;
    S32 sLog = -1;

    struct Name
    {
        const char* name;
        U32 opt;
    };

    const Name kNames[] = {
        { "prepass", IGPUOPT_PREPASS },     { "msrtt", IGPUOPT_MSRTT },
        { "post", IGPUOPT_POST },           { "invalidate", IGPUOPT_INVALIDATE },
        { "readview", IGPUOPT_READVIEW },   { "fixedsize", IGPUOPT_FIXEDSIZE },
        { "msdiscard", IGPUOPT_MSDISCARD }, { "all", 0xFFFFFFFFu },
    };

    void Load()
    {
        if (sOff >= 0)
        {
            return;
        }
        sOff = 0;
        sLog = getenv("TSSM_GPU_LOG") != NULL;

        const char* env = getenv("TSSM_GPU_OPT");
        if (env == NULL || *env == '\0')
        {
            return;
        }

        char buf[256];
        snprintf(buf, sizeof(buf), "%s", env);
        for (char* tok = strtok(buf, ", "); tok != NULL; tok = strtok(NULL, ", "))
        {
            bool known = false;
            for (const Name& n : kNames)
            {
                if (strcmp(tok, n.name) == 0)
                {
                    sOff |= (S32)n.opt;
                    known = true;
                }
            }
            if (!known)
            {
                printf("tssm: TSSM_GPU_OPT: no saving called '%s'\n", tok);
            }
        }
        printf("tssm: TSSM_GPU_OPT=%s: turned off:", env);
        for (const Name& n : kNames)
        {
            if (n.opt != 0xFFFFFFFFu && (sOff & (S32)n.opt))
            {
                printf(" %s", n.name);
            }
        }
        printf("\n");
        fflush(stdout);
    }

    S32 sFrames;
    S32 sEarly;
    S32 sLate;

    S32 sVerify = -1;
    S32 sChecked;
    S32 sStateDiffers;
    S32 sPixelsDiffer;
} // namespace

S32 iGpuOptVerify()
{
    if (sVerify < 0)
    {
        sVerify = getenv("TSSM_GPU_VERIFY") != NULL;
    }
    return sVerify;
}

S32 iGpuOptSameRaster(RwRaster* a, RwRaster* b)
{
    if (a == NULL || b == NULL || RwRasterGetWidth(a) != RwRasterGetWidth(b) ||
        RwRasterGetHeight(a) != RwRasterGetHeight(b))
    {
        return FALSE;
    }
    RwUInt8* pa = RwRasterLock(a, 0, rwRASTERLOCKREAD);
    if (pa == NULL)
    {
        return FALSE;
    }
    const RwInt32 size = RwRasterGetStride(a) * RwRasterGetHeight(a);
    unsigned char* copy = (unsigned char*)malloc(size);
    if (copy != NULL)
    {
        memcpy(copy, pa, size);
    }
    RwRasterUnlock(a);
    RwUInt8* pb = RwRasterLock(b, 0, rwRASTERLOCKREAD);
    S32 same = FALSE;
    if (pb != NULL && copy != NULL && RwRasterGetStride(b) * RwRasterGetHeight(b) == size)
    {
        same = memcmp(copy, pb, size) == 0;
    }
    if (pb != NULL)
    {
        RwRasterUnlock(b);
    }
    free(copy);
    return same;
}

void iGpuOptVerifyResult(S32 sameState, S32 samePixels)
{
    sChecked++;
    sStateDiffers += !sameState;
    sPixelsDiffer += !samePixels;
    if (sChecked % 300 == 0 || !sameState || !samePixels)
    {
        if (sChecked % 300 == 0 || sStateDiffers + sPixelsDiffer < 20)
        {
            printf("tssm: shadow pre-pass check: %d shadows, the render state differed after "
                   "%d, the pixels in %d\n",
                   (int)sChecked, (int)sStateDiffers, (int)sPixelsDiffer);
            fflush(stdout);
        }
    }
}

S32 iGpuOptOn(U32 opt)
{
    Load();
    if (opt == IGPUOPT_FIXEDSIZE)
    {
        // Off until it has been measured and checked on a phone.
        static const bool on =
            getenv("TSSM_GPU_FIXEDSIZE") != NULL && strcmp(getenv("TSSM_GPU_FIXEDSIZE"), "1") == 0;
        if (!on)
        {
            return FALSE;
        }
    }
    if (opt == IGPUOPT_MSDISCARD)
    {
        // Off unless asked for: the next frame reads the samples it discards
        // (iGpuOpt.h).
        static const bool on =
            getenv("TSSM_GPU_MSDISCARD") != NULL && strcmp(getenv("TSSM_GPU_MSDISCARD"), "1") == 0;
        if (!on)
        {
            return FALSE;
        }
    }
    if (opt == IGPUOPT_MSRTT)
    {
        // Off unless asked for: on the phone's Adreno the multisampled depth
        // is not kept across a camera restart inside the scene (iGpuOpt.h).
        static const bool on =
            getenv("TSSM_GPU_MSRTT") != NULL && strcmp(getenv("TSSM_GPU_MSRTT"), "1") == 0;
        if (!on)
        {
            return FALSE;
        }
    }
    return (sOff & (S32)opt) == 0;
}

void iGpuOptShadowFrame(S32 early, S32 late)
{
    Load();
    if (!sLog)
    {
        return;
    }
    sFrames++;
    sEarly += early;
    sLate += late;
    if (sFrames >= 300)
    {
        printf("tssm: shadows: %d drawn before the scene and %d in it, over %d frames\n",
               (int)sEarly, (int)sLate, (int)sFrames);
        fflush(stdout);
        sFrames = 0;
        sEarly = 0;
        sLate = 0;
    }
}
