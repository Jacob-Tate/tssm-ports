#include "iD0Stats.h"
#include "iD0StatsRw.h"

#include "iLowHeap.h"
#include "iScreen.h"
#include "iMemMgr.h"
#include "iSndData.h"
#include "iTime.h"
#include "xPad.h"
#include "xCam.h"
#include "xMemMgr.h"
#include "zGameState.h"
#include "zGlobals.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <new>
#include <vector>

// See iD0Stats.h. Everything here is measurement: nothing the game reads is
// changed, and with TSSM_D0 unset every entry point returns at its first line.

extern xMemHeap gxHeap[3];

// zGame.cpp's loop clock: set at the top of every zGameLoop iteration, so a
// change means the frame being presented is a game-loop frame.
extern iTime sTimeCurrent;
extern F32 sTimeElapsed;

// iSnd.cpp.
S32 iSndD0Voices(S32* playing);

namespace
{
    bool sOn;
    FILE* sFile;

    S64 sBucketUs[iD0_BUCKETS];
    U64 sBucketBytes[iD0_BUCKETS];
    U32 sBucketCount[iD0_BUCKETS];
    U32 sTexFormats[3];

    S64 sMainBegin;
    // zGame.cpp's clock is 0 until the game loop first runs.
    iTime sLastLoopStamp = 0;
    S64 sLastLoopEnd;
    U32 sFrame;

    S32 sIm2dDraws;
    S32 sIm2dPrims;
    S32 sIm3dDraws;
    S32 sIm3dPrims;

    struct Scene
    {
        U32 id;
        char name[8];

        U32 prefix;
        U32 heap0Peak;
        U32 heap1Peak;
        U32 heap2Peak;
        U32 mallocPeak;
        U32 sumPeak;

        U32 frames;
        U32 playFrames;
        S32 voicesPeak;
        S32 playingPeak;

        bool loaded;
        S64 loadWallUs;
        S64 loadUs[iD0_BUCKETS];
        U64 loadBytes[iD0_BUCKETS];
        U32 loadCount[iD0_BUCKETS];

        S32 maxAtomics;
        S32 maxMeshes;
        S32 maxTris;
        S32 maxSkinAtomics;
        S32 maxSkinVerts;
        S32 maxDir;
        S32 maxLocal;
        S32 maxAmbient;
        S32 maxBones;
        S32 maxIm2dDraws;
        S32 maxIm3dDraws;
        S32 maxIm3dPrims;
        S64 sumMeshes;
        S64 sumTris;
        S64 sumSkinVerts;
        S64 sumIm;
        S64 statFrames;

        std::vector<S32> work;
        std::vector<S32> logic;
        std::vector<S32> render;
    };

    Scene sCur;
    iD0FrameResult sLast;

    void NameOf(U32 id, char* out)
    {
        if (id == 0)
        {
            strcpy(out, "boot");
            return;
        }
        for (int i = 0; i < 4; i++)
        {
            char c = (char)(id >> (24 - 8 * i));
            out[i] = (c >= 32 && c < 127) ? c : '?';
        }
        out[4] = '\0';
    }

    U32 SceneId()
    {
        return globals.sceneCur != NULL ? globals.sceneCur->sceneID : 0;
    }

    S32 Pct(std::vector<S32> v, int pct)
    {
        if (v.empty())
        {
            return -1;
        }
        std::sort(v.begin(), v.end());
        size_t i = (v.size() - 1) * (size_t)pct / 100;
        return v[i];
    }

    void FinishScene()
    {
        Scene& s = sCur;
        const U32 rwPeak = iLowHeapWanted() ? (U32)iLowHeapPeak() : 0;
        const U32 sndPeak = iSndDataPeak(false);
        const U32 sndSet = iSndDataWorkingSet(false);
        const double mb = 1.0 / 1048576.0;

        fprintf(sFile,
                "#scene\t%s\tframes=%u\tplay=%u\tarenaPrefixMB=%.2f\theap0MB=%.2f\theap1MB=%.2f\t"
                "heap2MB=%.2f\tmallocMB=%.2f\tarenaPeakMB=%.2f\trwHeapMB=%.2f\tsndCacheMB=%.2f\t"
                "sndSetMB=%.2f\tvoices=%d\tplaying=%d\t",
                s.name, s.frames, s.playFrames, s.prefix * mb, s.heap0Peak * mb, s.heap1Peak * mb,
                s.heap2Peak * mb, s.mallocPeak * mb, (s.prefix + s.sumPeak) * mb,
                iLowHeapWanted() ? rwPeak * mb : -1.0, sndPeak * mb, sndSet * mb, s.voicesPeak,
                s.playingPeak);
        fprintf(
            sFile,
            "loadMs=%.1f\tloadNetMs=%.1f\tfileMs=%.1f\tfileMB=%.2f\tgeomMs=%.1f\tgeomVerts=%llu\tinstMs=%.1f\t"
            "texMs=%.1f\ttex3dsMs=%.1f\ttex3dsMB=%.2f\ttex3dsCount=%u\t",
            s.loadWallUs / 1000.0,
            (s.loadWallUs - s.loadUs[iD0_OVERHEAD] - s.loadUs[iD0_TEX_3DS]) / 1000.0,
            s.loadUs[iD0_FILE] / 1000.0, s.loadBytes[iD0_FILE] * mb,
            s.loadUs[iD0_GEOM_CONVERT] / 1000.0, (unsigned long long)s.loadBytes[iD0_GEOM_CONVERT],
            s.loadUs[iD0_GEOM_INSTANCE] / 1000.0, s.loadUs[iD0_TEX_CONVERT] / 1000.0,
            s.loadUs[iD0_TEX_3DS] / 1000.0, s.loadBytes[iD0_TEX_3DS] * mb,
            s.loadCount[iD0_TEX_3DS]);
        const double n = s.statFrames > 0 ? (double)s.statFrames : 1.0;
        fprintf(sFile,
                "workMedUs=%d\tworkP90Us=%d\tworkMaxUs=%d\tlogicMedUs=%d\tlogicP90Us=%d\t"
                "renderMedUs=%d\trenderP90Us=%d\tmaxAtomics=%d\tmaxMeshes=%d\tavgMeshes=%.0f\t"
                "maxTris=%d\tavgTris=%.0f\tmaxSkinAtomics=%d\tmaxSkinVerts=%d\tavgSkinVerts=%.0f\t"
                "maxBones=%d\tmaxDirLights=%d\tmaxLocalLights=%d\tmaxAmbients=%d\tmaxIm2d=%d\t"
                "maxIm3d=%d\tmaxIm3dPrims=%d\tavgImDraws=%.0f\n",
                Pct(s.work, 50), Pct(s.work, 90), Pct(s.work, 100), Pct(s.logic, 50),
                Pct(s.logic, 90), Pct(s.render, 50), Pct(s.render, 90), s.maxAtomics, s.maxMeshes,
                s.sumMeshes / n, s.maxTris, s.sumTris / n, s.maxSkinAtomics, s.maxSkinVerts,
                s.sumSkinVerts / n, s.maxBones, s.maxDir, s.maxLocal, s.maxAmbient, s.maxIm2dDraws,
                s.maxIm3dDraws, s.maxIm3dPrims, s.sumIm / n);
        fflush(sFile);

        printf("tssm: d0: scene %s: arena %.1f MB, RW heap %.1f MB, sound cache %.1f MB, "
               "load %.0f ms, work median %d us\n",
               s.name, (s.prefix + s.sumPeak) * mb, rwPeak * mb, sndPeak * mb,
               s.loadWallUs / 1000.0, Pct(s.work, 50));
        fflush(stdout);
    }

    void SampleMem(Scene& s)
    {
        U32 prefix;
        U32 mallocExtent;
        iMemMgrD0Usage(&prefix, &mallocExtent);
        const U32 h0 = gxHeap[0].state[gxHeap[0].state_idx].used;
        const U32 h1 = gxHeap[1].state[gxHeap[1].state_idx].used;
        const U32 h2 = gxHeap[2].state[gxHeap[2].state_idx].used;
        const U32 sum = h0 + h1 + h2 + mallocExtent;

        s.prefix = prefix;
        s.heap0Peak = std::max(s.heap0Peak, h0);
        s.heap1Peak = std::max(s.heap1Peak, h1);
        s.heap2Peak = std::max(s.heap2Peak, h2);
        s.mallocPeak = std::max(s.mallocPeak, mallocExtent);
        s.sumPeak = std::max(s.sumPeak, sum);
    }

    void SwitchScene(U32 id)
    {
        FinishScene();

        sCur.~Scene();
        new (&sCur) Scene();
        sCur.id = id;
        NameOf(id, sCur.name);

        if (iLowHeapWanted())
        {
            iLowHeapPeakReset();
        }
        iSndDataPeak(true);
        iSndDataWorkingSet(true);
        SampleMem(sCur);
    }

    void AtExit()
    {
        if (!sOn)
        {
            return;
        }
        FinishScene();
        fprintf(sFile, "#texformats\t565=%u\t5551=%u\t4444=%u\n", sTexFormats[0], sTexFormats[1],
                sTexFormats[2]);
        iD0RwReport(sFile);
        fclose(sFile);
        sOn = false;
    }

    S32 Prims(S32 primType, S32 count)
    {
        switch (primType)
        {
        case 1: // line list
            return count / 2;
        case 2: // polyline
            return count > 1 ? count - 1 : 0;
        case 3: // triangle list
            return count / 3;
        case 4: // strip
        case 5: // fan
            return count > 2 ? count - 2 : 0;
        default:
            return count;
        }
    }
} // namespace

S32 iD0Enabled()
{
    return sOn;
}

FILE* iD0File()
{
    return sFile;
}

const char* iD0SceneName()
{
    return sCur.name;
}

void iD0Init()
{
    const char* path = getenv("TSSM_D0");
    if (path == NULL || path[0] == '\0')
    {
        return;
    }

    sFile = fopen(path, "w");
    if (sFile == NULL)
    {
        printf("tssm: d0: could not open %s; no measurements\n", path);
        return;
    }
    // A line per frame: on a console's SD card every write is slow, so they
    // are gathered (the 3DS spike; the writes come after a frame's work is
    // timed, but they still take the run's time). Made only for a run that
    // measures, so the game's own runs keep those 256 KB.
    const size_t bufferSize = 256 * 1024;
    char* buffer = (char*)malloc(bufferSize);
    if (buffer != NULL)
    {
        setvbuf(sFile, buffer, _IOFBF, bufferSize);
    }

    new (&sCur) Scene();
    NameOf(0, sCur.name);
    sOn = true;
    atexit(AtExit);

    fprintf(sFile, "frame\tt_ms\tscene\tmode\tstate\tloop\tcut\tdt_ms\twork_us\tlogic_us\t"
                   "render_us\tatomics\tmeshes\ttris\tskin_atomics\tskin_verts\tlit\t"
                   "dir_max\tlocal_max\tdir_sum\tim2d\tim2d_prims\tim3d\tim3d_prims\tvoices\t"
                   "playing\tpx\tpy\tpz\tcx\tcy\tcz\tfile_us\tfile_kb\tgeom_us\tinst_us\t"
                   "tex_us\ttex3ds_us\tslide\n");
    // The PICA profile: the port's own extras off, as a 3DS v1 would have them.
    const char* pica = getenv("TSSM_D0_PICA");
    if (pica != NULL && atoi(pica) > 0)
    {
        iScreenSetPerPixelLighting(FALSE);
        iScreenSetWorldLightShadows(FALSE);
        printf("tssm: d0: PICA profile (16-bit half-size textures, per-pixel lighting and "
               "world-light shadows off)\n");
    }

    printf("tssm: d0: measuring into %s\n", path);
    fflush(stdout);
}

S64 iD0Now()
{
    return sOn ? (S64)iTimeGet() : 0;
}

void iD0Add(iD0Bucket bucket, S64 startUs, U32 bytes)
{
    if (!sOn)
    {
        return;
    }
    sBucketUs[bucket] += (S64)iTimeGet() - startUs;
    sBucketBytes[bucket] += bytes;
    sBucketCount[bucket]++;
}

void iD0LastFrame(iD0FrameResult* out)
{
    *out = sLast;
}

void iD0ScenePeaksNow(iD0ScenePeaks* out)
{
    memset(out, 0, sizeof(*out));
    if (!sOn)
    {
        out->rwHeapBytes = -1;
        return;
    }
    const Scene& s = sCur;
    memcpy(out->name, s.name, sizeof(out->name));
    out->arenaBytes = s.prefix + s.sumPeak;
    out->heap0Bytes = s.heap0Peak;
    out->rwHeapBytes = iLowHeapWanted() ? (S64)iLowHeapPeak() : -1;
    out->sndCacheBytes = iSndDataPeak(false);
    out->voices = s.voicesPeak;
    out->loadUs = s.loadWallUs;
    out->overheadUs = s.loadUs[iD0_OVERHEAD];
    out->fileUs = s.loadUs[iD0_FILE];
    out->fileBytes = s.loadBytes[iD0_FILE];
}

void iD0AddTex3DS(S64 startUs, U32 bytes, S32 format)
{
    iD0Add(iD0_TEX_3DS, startUs, bytes);
    if (format >= 0 && format < 3)
    {
        sTexFormats[format]++;
    }
}

void iD0CameraBegin(RwCamera* camera)
{
    if (!sOn || sMainBegin != 0)
    {
        return;
    }
    if (globals.screen != NULL && camera == globals.screen->icam)
    {
        sMainBegin = iTimeGet();
    }
}

void iD0MemSample()
{
    if (!sOn)
    {
        return;
    }

    const U32 id = SceneId();
    if (id != 0 && id != sCur.id)
    {
        SwitchScene(id);
    }
    SampleMem(sCur);
}

void iD0CountIm(S32 is3d, S32 primType, S32 count)
{
    if (!sOn)
    {
        return;
    }
    if (is3d)
    {
        sIm3dDraws++;
        sIm3dPrims += Prims(primType, count);
    }
    else
    {
        sIm2dDraws++;
        sIm2dPrims += Prims(primType, count);
    }
}

void iD0FrameEnd()
{
    if (!sOn)
    {
        return;
    }

    const S64 now = iTimeGet();
    sFrame++;
    if (sFrame % 60 == 1)
    {
        // Pipelines appear as the engine and the game set them up; wrapping is
        // once per pipeline, so asking again costs a few compares.
        iD0RwWrapPipelines();
    }

    const U32 id = SceneId();
    if (id != 0 && id != sCur.id)
    {
        SwitchScene(id);
    }
    Scene& s = sCur;

    const bool loop = sTimeCurrent != sLastLoopStamp;
    if (loop)
    {
        sLastLoopStamp = sTimeCurrent;
    }

    iD0RwFrame rf;
    iD0RwTakeFrame(&rf);

    S32 playing = 0;
    const S32 voices = iSndD0Voices(&playing);
    s.voicesPeak = std::max(s.voicesPeak, voices);
    s.playingPeak = std::max(s.playingPeak, playing);

    S64 work = -1;
    S64 logic = -1;
    S64 render = -1;
    if (loop)
    {
        work = now - sTimeCurrent;
        if (sMainBegin != 0 && sMainBegin >= sTimeCurrent)
        {
            logic = sMainBegin - sTimeCurrent;
            render = now - sMainBegin;
        }
    }

    const bool cut = globals.cmgr != NULL;
    const bool play = loop && gGameMode == eGameMode_Game && gGameState == eGameState_Play && !cut;

    // The load is everything since the last game-loop frame, charged to the
    // scene whose first game-loop frame this is.
    if (loop && !s.loaded)
    {
        // Where the scene's slide tracks are, for runs that put the player on
        // one (TSSM_TEST_PLACE).
        fprintf(sFile, "#slides\t%s\tcount=%u", s.name, globals.player.SlideTrackCount);
        for (U32 i = 0; i < globals.player.SlideTrackCount && i < 8; i++)
        {
            xEnt* ent = globals.player.SlideTrackEnt[i];
            if (ent != NULL && ent->frame != NULL)
            {
                fprintf(sFile, "\t%.1f,%.1f,%.1f", ent->frame->mat.pos.x, ent->frame->mat.pos.y,
                        ent->frame->mat.pos.z);
            }
            else if (ent != NULL && ent->model != NULL && ent->model->Mat != NULL)
            {
                fprintf(sFile, "\tm%.1f,%.1f,%.1f", ent->model->Mat->pos.x, ent->model->Mat->pos.y,
                        ent->model->Mat->pos.z);
            }
        }
        fprintf(sFile, "\n");

        s.loaded = true;
        s.loadWallUs = sTimeCurrent - sLastLoopEnd;
        for (int b = 0; b < iD0_BUCKETS; b++)
        {
            s.loadUs[b] = sBucketUs[b];
            s.loadBytes[b] = sBucketBytes[b];
            s.loadCount[b] = sBucketCount[b];
        }
    }

    F32 px = 0.0f;
    F32 py = 0.0f;
    F32 pz = 0.0f;
    if (xglobals != NULL && xglobals->___player_ent_dont_use_directly != NULL &&
        xglobals->___player_ent_dont_use_directly->frame != NULL)
    {
        const xVec3& p = xglobals->___player_ent_dont_use_directly->frame->mat.pos;
        px = p.x;
        py = p.y;
        pz = p.z;
    }
    F32 cx = 0.0f;
    F32 cy = 0.0f;
    F32 cz = 0.0f;
    if (globals.cam != NULL)
    {
        cx = globals.cam->mat.pos.x;
        cy = globals.cam->mat.pos.y;
        cz = globals.cam->mat.pos.z;
    }

    fprintf(sFile,
            "%u\t%.1f\t%s\t%d\t%d\t%d\t%d\t%.2f\t%lld\t%lld\t%lld\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t"
            "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%lld\t%llu\t%lld\t"
            "%lld\t%lld\t%lld\t%u\n",
            sFrame, now / 1000.0, s.name, (int)gGameMode, (int)gGameState, loop ? 1 : 0,
            cut ? 1 : 0, loop ? sTimeElapsed * 1000.0f : 0.0f, (long long)work, (long long)logic,
            (long long)render, rf.atomics, rf.meshes, rf.tris, rf.skinAtomics, rf.skinVerts,
            rf.litAtomics, rf.maxDirLights, rf.maxLocalLights, rf.dirLightSum, sIm2dDraws,
            sIm2dPrims, sIm3dDraws, sIm3dPrims, voices, playing, px, py, pz, cx, cy, cz,
            (long long)sBucketUs[iD0_FILE], (unsigned long long)(sBucketBytes[iD0_FILE] / 1024),
            (long long)sBucketUs[iD0_GEOM_CONVERT], (long long)sBucketUs[iD0_GEOM_INSTANCE],
            (long long)sBucketUs[iD0_TEX_CONVERT], (long long)sBucketUs[iD0_TEX_3DS],
            (unsigned)globals.player.SlideTrackSliding);

    sLast.loop = loop ? 1 : 0;
    sLast.play = play ? 1 : 0;
    sLast.workUs = work;
    sLast.logicUs = logic;
    sLast.renderUs = render;
    sLast.meshes = rf.meshes;
    sLast.tris = rf.tris;
    sLast.voices = voices;

    s.frames++;
    if (play)
    {
        s.playFrames++;
        // The first second after a load pays for whatever was left to do lazily.
        if (s.playFrames > 30)
        {
            s.work.push_back((S32)work);
            if (logic >= 0)
            {
                s.logic.push_back((S32)logic);
                s.render.push_back((S32)render);
            }
        }

        s.maxAtomics = std::max(s.maxAtomics, rf.atomics);
        s.maxMeshes = std::max(s.maxMeshes, rf.meshes);
        s.maxTris = std::max(s.maxTris, rf.tris);
        s.maxSkinAtomics = std::max(s.maxSkinAtomics, rf.skinAtomics);
        s.maxSkinVerts = std::max(s.maxSkinVerts, rf.skinVerts);
        s.maxBones = std::max(s.maxBones, rf.maxBones);
        s.maxDir = std::max(s.maxDir, rf.maxDirLights);
        s.maxLocal = std::max(s.maxLocal, rf.maxLocalLights);
        s.maxAmbient = std::max(s.maxAmbient, rf.maxAmbients);
        s.maxIm2dDraws = std::max(s.maxIm2dDraws, sIm2dDraws);
        s.maxIm3dDraws = std::max(s.maxIm3dDraws, sIm3dDraws);
        s.maxIm3dPrims = std::max(s.maxIm3dPrims, sIm3dPrims);
        s.sumMeshes += rf.meshes;
        s.sumTris += rf.tris;
        s.sumSkinVerts += rf.skinVerts;
        s.sumIm += sIm2dDraws + sIm3dDraws;
        s.statFrames++;
    }

    if (loop)
    {
        memset(sBucketUs, 0, sizeof(sBucketUs));
        memset(sBucketBytes, 0, sizeof(sBucketBytes));
        memset(sBucketCount, 0, sizeof(sBucketCount));
        sLastLoopEnd = now;
    }

    sMainBegin = 0;
    sIm2dDraws = 0;
    sIm2dPrims = 0;
    sIm3dDraws = 0;
    sIm3dPrims = 0;

    // Every ten seconds or so: often enough for a crash to leave most of the
    // run, rarely enough not to stall a console on its SD card.
    if (sFrame % 300 == 0)
    {
        fflush(sFile);
    }
}
