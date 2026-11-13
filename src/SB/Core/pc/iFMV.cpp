// PC iFMV: treedome's pc/iFMV.cpp -- find the movie, pace frames, watch the
// skip buttons, present -- with TSSM's interface. TSSM's Xbox movies are Bink,
// decoded by FFmpeg through iFMVDecoderFFmpeg.cpp.
//
// TSSM also has a movie-TEXTURE path: iFMVOpen/iFMVClose hand xFMV.cpp a MOVIE
// whose Bink handle it drives frame by frame into a raster (screens inside
// levels). That path is not ported: iFMVOpen returns NULL (xFMV's callers
// treat that as "no movie"), and xFMV.cpp's direct Bink calls are shared code
// still to be moved behind iFMV. Suspend/Resume/Kill/ResetCallback have nothing
// to act on while no movie runs asynchronously; iFMVPlay blocks, as retail's
// does.

#include "iFMV.h"
#include "iScreen.h"
#include "iStub.h"

#include "iBoot.h"
#include "iAudioSDLTest.h"
#include "iFile.h"
#include "iFMVAudio.h"
#include "iFMVDecoder.h"
#include "iHost.h"
#include "iMod.h"
#include "iRasterFill.h"
#include "iPadHost.h"
#include "iSndMeter.h"
#include "iTime.h"
#include "iWindow.h"
#include "xFMV.h"
#include "zGameState.h"

#include <rwcore.h>

#include "iCamera.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Full-motion video.
//
// The GameCube build decodes Bink through RAD's rad3d straight into a GX
// framebuffer, and none of that ports: Bink is proprietary middleware that
// cannot be redistributed. The Xbox release this port runs on ships the same
// Bink 1 files (FMV/*.bik, 43 of them), and FFmpeg decodes Bink 1 video and its
// DCT audio, so the files are played as they are.
//
// What decodes them is behind iFMVDecoder.h, and what plays the sound is behind
// iFMVAudio.h. This file is the part that is the same whatever answers those
// two: find the file, pace the frames, watch for the skip button, put the
// picture on the screen, and give the game back the answer it expects.
//
// A build with no decoder still works. iFMVDecoderOpen returns NULL, and a
// movie that cannot be played is reported as one that finished -- which is what
// every caller already copes with, because a skipped movie and a finished movie
// are the same thing to the game. A movie with no file is the same case: five
// of zFMV.cpp's 48 table entries (promoFOP, promoJmy, promo6, MakingOf,
// Trailer1) have none on the Xbox disc.

namespace
{
    // The game passes the table name without an extension ("FMV\LogoTHQ"; the
    // console's iFMVPlay appends ".bik" itself). The Xbox files are Bink too, so
    // the extension is ".bik", not the ".xmv" of the port this file came from.
    const char* const kWantedExtension = ".bik";

    // Movies live beside the rest of the assets. The name arrives as
    // "FMV\LogoHI" -- a DOS separator, because that is what the asset table
    // in zFMV.cpp was written with.
    const char* const kMovieDirectory = "FMV/";

    // The four logos zMenuLoop plays on the way to the title screen, which
    // config.ini's game.intro_movies turns off (iBoot.h).
    const char* const kIntroMovies[] = { "LogoNicG", "LogoNicM", "LogoTHQ", "LogoHI" };

    bool isIntroMovie(const char* game_name)
    {
        const char* leaf = game_name;
        for (const char* p = game_name; *p; p++)
        {
            if (*p == '/' || *p == '\\')
            {
                leaf = p + 1;
            }
        }
        for (size_t i = 0; i < sizeof(kIntroMovies) / sizeof(kIntroMovies[0]); i++)
        {
            const size_t n = strlen(kIntroMovies[i]);
            if (strnicmp(leaf, kIntroMovies[i], n) == 0 && (leaf[n] == 0 || leaf[n] == '.'))
            {
                return true;
            }
        }
        return false;
    }

    // Build "<assets>/FMV/<name>.bik" out of what the game passed.
    //
    // Two things have to be undone: the backslash separator is not one on a
    // host that takes paths seriously, and the directory part of the game's name
    // is dropped rather than translated, because every movie is in one place.
    void buildPath(char* out, size_t outsize, const char* root, const char* game_name)
    {
        // The leaf, after the last separator of either kind.
        const char* leaf = game_name;
        for (const char* p = game_name; *p; p++)
        {
            if (*p == '/' || *p == '\\')
            {
                leaf = p + 1;
            }
        }

        char stem[128];
        snprintf(stem, sizeof(stem), "%s", leaf);

        char* dot = strrchr(stem, '.');
        if (dot != NULL)
        {
            *dot = 0;
        }

        snprintf(out, outsize, "%s%s%s%s", root, kMovieDirectory, stem, kWantedExtension);
    }

    // Open the first spelling of the name that exists.
    //
    // The asset table asks for "story1" and the file on disk is "Story1.bik".
    // Windows does not care and this would work by accident there; a
    // case-sensitive filesystem does, and the port is meant to reach one. The
    // name as asked, all lower and all upper are tried first because they cost
    // no directory read; mixed-case names on the disc (Story1, PromoSB1) need
    // iFile's resolver, the last resort.
    iFMVDecoder* openAnyCase(char* path, size_t pathsize, const char* root,
                             const char* game_name, iFMVDecoderInfo* info)
    {
        buildPath(path, pathsize, root, game_name);

        iFMVDecoder* dec = iFMVDecoderOpen(path, info);
        if (dec != NULL)
        {
            return dec;
        }

        // The leaf, lowered and raised in turn. Only the stem is touched: the
        // directory came from us and the extension is a constant.
        size_t rootlen = strlen(root) + strlen(kMovieDirectory);
        for (int pass = 0; pass < 2; pass++)
        {
            char alt[512];
            snprintf(alt, sizeof(alt), "%s", path);
            for (char* p = alt + rootlen; *p && *p != '.'; p++)
            {
                *p = (pass == 0) ? (char)tolower((unsigned char)*p)
                                 : (char)toupper((unsigned char)*p);
            }
            dec = iFMVDecoderOpen(alt, info);
            if (dec != NULL)
            {
                snprintf(path, pathsize, "%s", alt);
                return dec;
            }
        }

        // Any other mix of case: the game asks for "story1" and "promoSB1", the
        // disc has Story1.bik and PromoSB1.bik. The directory is read, as iFile
        // does for the archives.
        char resolved[512];
        snprintf(resolved, sizeof(resolved), "%s", path);
        if (iFileResolveCase(resolved, sizeof(resolved)))
        {
            dec = iFMVDecoderOpen(resolved, info);
            if (dec != NULL)
            {
                snprintf(path, pathsize, "%s", resolved);
                return dec;
            }
        }

        return NULL;
    }

    // Where the movie goes on a 640x480 screen, with its aspect kept.
    //
    // The movies are not all the shape of the screen: 640x480 is, and 720x480
    // and 720x486 are anamorphic NTSC that would be visibly stretched if the
    // quad were just the whole screen. So the picture is fitted and centred and
    // whatever is left over stays black, which is what a letterbox is.
    void fitRect(U32 vw, U32 vh, F32 sw, F32 sh, F32* x0, F32* y0, F32* x1, F32* y1)
    {
        // 720x480 and 720x486 are 4:3 content in non-square pixels; displaying
        // them at their pixel dimensions is the stretch, not the fix. Every
        // movie in this game is 4:3, so that is what they are fitted to.
        (void)vw;
        (void)vh;
        const F32 aspect = 4.0f / 3.0f;

        F32 w = sw;
        F32 h = sw / aspect;
        if (h > sh)
        {
            h = sh;
            w = sh * aspect;
        }

        *x0 = 0.5f * (sw - w);
        *y0 = 0.5f * (sh - h);
        *x1 = *x0 + w;
        *y1 = *y0 + h;
    }

    void drawFrame(RwRaster* raster, F32 sw, F32 sh, F32 x0, F32 y0, F32 x1, F32 y1)
    {
        // The movie is fitted to the whole screen in its own shape (fitFrame),
        // bars included, so none of this is UI.
        iScreenScreenSpace screenSpace;

        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
        RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);

        // The bars. Drawn rather than cleared so that this does not depend on
        // the camera's clear colour being black, and so a movie narrower than
        // the screen cannot leave the previous frame showing down the sides.
        if (x0 > 0.0f || y0 > 0.0f)
        {
            rwGameCube2DVertex bar[4];
            F32 bz = RwIm2DGetFarScreenZ();
            const F32 bx[4] = { 0.0f, 0.0f, sw, sw };
            const F32 by[4] = { 0.0f, sh, 0.0f, sh };
            for (int i = 0; i < 4; i++)
            {
                bar[i].x = bx[i];
                bar[i].y = by[i];
                bar[i].z = bz;
                bar[i].emissiveColor.red = 0;
                bar[i].emissiveColor.green = 0;
                bar[i].emissiveColor.blue = 0;
                bar[i].emissiveColor.alpha = 255;
                bar[i].u = 0.0f;
                bar[i].v = 0.0f;
            }
            RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, &bar[0], 4);
        }

        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)raster);

        rwGameCube2DVertex vx[4];
        F32 z = RwIm2DGetFarScreenZ();
        const F32 px[4] = { x0, x0, x1, x1 };
        const F32 py[4] = { y0, y1, y0, y1 };
        const F32 pu[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
        const F32 pv[4] = { 0.0f, 1.0f, 0.0f, 1.0f };

        for (int i = 0; i < 4; i++)
        {
            vx[i].x = px[i];
            vx[i].y = py[i];
            vx[i].z = z;
            // White: the frame is the picture, not something being tinted.
            vx[i].emissiveColor.red = 255;
            vx[i].emissiveColor.green = 255;
            vx[i].emissiveColor.blue = 255;
            vx[i].emissiveColor.alpha = 255;
            vx[i].u = pu[i];
            vx[i].v = pv[i];
        }

        RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, &vx[0], 4);

        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)NULL);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
    }

    // Copy one decoded frame into the raster.
    //
    // Row by row rather than one memcpy: the decoder's pitch and the raster's
    // stride are both padded, and by different amounts -- and the rows do not
    // necessarily go in the order they arrive. See iRasterFill.h.
    void uploadFrame(RwRaster* raster, const void* pixels, U32 pitch, U32 w, U32 h)
    {
        RwUInt8* dst = RwRasterLock(raster, 0, rwRASTERLOCKWRITE | rwRASTERLOCKNOFETCH);
        if (dst == NULL)
        {
            return;
        }

        U32 stride = (U32)raster->stride;
        U32 rowbytes = w * 4;
        const RwUInt8* src = (const RwUInt8*)pixels;

        for (U32 y = 0; y < h; y++)
        {
            const size_t row = (size_t)IRASTERFILL_ROW((S32)y, (S32)h);
            memcpy(dst + row * stride, src + (size_t)y * pitch, rowbytes);
        }

        RwRasterUnlock(raster);
    }
}

U32 iFMVPlay(const char* filename, U32 buttons, F32 time, U32 uSubtitlesAID, bool skippable,
             bool lockController)
{
    // TSSM: the subtitle track (an asset id the console draws per frame with
    // xSubTitles) is not drawn yet. PORTING.md lists it.
    (void)uSubtitlesAID;
    (void)lockController;

    if (filename == NULL)
    {
        // Retail's own answer to a null filename.
        return 1;
    }

    if (!iBootIntroMovies() && isIntroMovie(filename))
    {
        return 0;
    }

    const char* root = iFileAssetRoot();
    char rootbuf[512];
    if (root[0] == 0)
    {
        rootbuf[0] = 0;
    }
    else
    {
        size_t n = strlen(root);
        bool slash = (n > 0) && (root[n - 1] == '/' || root[n - 1] == '\\');
        snprintf(rootbuf, sizeof(rootbuf), "%s%s", root, slash ? "" : "/");
    }

    char path[512];
    iFMVDecoderInfo info;
    iFMVDecoder* dec = NULL;

    // TSSM: a movie from the mods/ folder (fmv/<name>.* or files/FMV/<name>.bik),
    // in any container and codec FFmpeg reads. See iMod.h.
    char modded[512];
    if (iModFindMovie(filename, modded, sizeof(modded)))
    {
        dec = iFMVDecoderOpen(modded, &info);
        if (dec != NULL)
        {
            snprintf(path, sizeof(path), "%s", modded);
        }
        else
        {
            printf("tssm: mod movie %s will not open (decoder: %s); playing the original\n",
                   modded, iFMVDecoderName());
            fflush(stdout);
        }
    }

    const char* mod = iFileModRoot();
    if (dec == NULL && mod[0] != 0)
    {
        char modbuf[512];
        size_t n = strlen(mod);
        snprintf(modbuf, sizeof(modbuf), "%s%s", mod, mod[n - 1] == '/' ? "" : "/");
        dec = openAnyCase(path, sizeof(path), modbuf, filename, &info);
    }

    if (dec == NULL)
    {
        dec = openAnyCase(path, sizeof(path), rootbuf, filename, &info);
    }

    if (dec == NULL)
    {
        // Not an error the game should see: five of the movies the table names
        // have no file on the Xbox disc, so this is a normal outcome and not
        // only a "no decoder" one.
        printf("tssm: no movie for '%s' (decoder: %s); reported as finished\n", filename,
               iFMVDecoderName());
        fflush(stdout);
        return 0;
    }

    if (info.sample_rate != 0 && info.channels != 0)
    {
        printf("tssm: movie %s (%ux%u, sound %u Hz, %u ch)\n", path, info.width, info.height,
               info.sample_rate, info.channels);
    }
    else
    {
        printf("tssm: movie %s (%ux%u, no sound track)\n", path, info.width, info.height);
    }
    fflush(stdout);

    // The game's own sounds are paused for the movie, as the console's iFMVPlay
    // does (gc/iFMV.cpp), and only once a movie has opened, as there.
    //
    // Nothing else stops them. zFMVPlay does not suspend the mixer, and the
    // story movie at the end of a level is started by a script while the level
    // is still running, so its music, ambience and looping effects are all live.
    // The game's mixer runs on its own audio thread, so without this a looping
    // effect plays under the whole movie and the streams play out the few
    // seconds they are decoded ahead. The callback is the game's own
    // (SB04FMVPauseSoundCB): every voice, and any started meanwhile, or only the
    // music when the game is already paused.
    xFMVPauseSound(true);
    _GameOstrich ostrich = zGameGetOstrich();
    zGameSetOstrich(eGameOstrich_PlayingMovie);

    // The screen, which is the virtual screen rather than the window: the
    // picture is composited at the size the game renders at and scaled to the
    // window with everything else at present time.
    RwVideoMode mode;
    RwEngineGetVideoModeInfo(&mode, RwEngineGetCurrentVideoMode());
    F32 sw = (F32)mode.width;
    F32 sh = (F32)mode.height;

    RwCamera* cam = RwCameraGetCurrentCamera();
    RwCamera* owned = NULL;
    if (cam == NULL)
    {
        // The boot logos play before the game has a camera. One is made for the
        // movie and destroyed with it.
        owned = iCameraCreate((S32)mode.width, (S32)mode.height, 0);
        cam = owned;
    }

    // The format is named rather than left to the backend. librw's d3d fills a
    // zero format in for you -- rasterSetFormat picks C8888 for a texture --
    // and its gl3 does not: rasterCreateTexture switches on the format bits and
    // rejects zero outright, so every movie frame failed to make a texture and
    // GL3 played no movies at all.
    RwRaster* raster = RwRasterCreate((RwInt32)info.width, (RwInt32)info.height, 32,
                                      rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);

    S32 audio = FALSE;
    if (info.sample_rate != 0 && info.channels != 0)
    {
        // The console's BinkSetVolume(hBinkMovie, uAudioTrack,
        // xFMVGetBinkCompliantVolume()), where 32768 is full volume: the game's
        // own effects setting, which covers the movies as well.
        iFMVAudioSetGameVolume((F32)xFMVGetBinkCompliantVolume() / 32768.0f);
        audio = iFMVAudioOpen(info.sample_rate, info.channels);
        if (!audio)
        {
            printf("tssm: movie: no audio device would take its sound; playing it silent\n");
            fflush(stdout);
        }
    }

    // TSSM_TEST_AUDIO_RESTART's "movie" moment (iAudioSDLTest.h).
    iAudioSDLTestMovie(path, 0);

    F32 x0, y0, x1, y1;
    fitRect(info.width, info.height, sw, sh, &x0, &y0, &x1, &y1);

    U32 ended_with = 0;
    U32 audio_written = 0;

    // Audio decoded but not yet accepted by the device.
    //
    // iFMVAudioWrite takes what fits in its free blocks and no more, so a read
    // that outruns it leaves a remainder. That remainder is the movie's sound:
    // dropping it would skip through the audio while the picture kept time, and
    // the two would come apart. It is held here and offered again next time.
    S16 abuf[8192 * 2];
    U32 apend = 0;
    U32 aoff = 0;

    // The audio clock stops when the track does, and the video usually outlasts
    // it by a frame or two. See the handover below.
    bool audio_drained = false;
    bool handed_over = false;
    F32 clock_at_handover = 0.0f;
    iTime wall_at_handover = 0;
    iTime started = iTimeGet();
    F32 next_frame_at = 0.0f;
    F32 shown_at = 0.0f;
    bool have_frame = false;

    // TSSM_SND_METER: where a loop's time goes, per second.
    U32 m_loops = 0;
    U32 m_frames = 0;
    U32 m_skipped = 0;
    U64 m_audio = 0;
    U64 m_decode = 0;
    U64 m_upload = 0;
    U64 m_present = 0;
    U64 m_t = 0;

    // Tops the device up. Called again after each decode in a pass, so a slow
    // pass (frames skipped to catch up) does not leave the device dry. A short
    // read: getting the sound decodes the video interleaved with it.
    auto feedSound = [&]() {
        if (!audio)
        {
            return;
        }
        if (apend == 0)
        {
            apend = iFMVDecoderReadAudio(dec, abuf, 2048);
            aoff = 0;
            if (apend == 0)
            {
                if (!audio_drained)
                {
                    // TSSM_TEST_AUDIO_RESTART's "drained" moment.
                    iAudioSDLTestMovie(path, 1);
                }
                audio_drained = true;
            }
        }
        if (apend > 0)
        {
            U32 wrote = iFMVAudioWrite(abuf + (size_t)aoff * info.channels, apend);
            audio_written += wrote;
            aoff += wrote;
            apend -= wrote;
        }
    };

    // At most 60 passes a second while the movie plays. Every pass draws and
    // presents, and the movies are 30 frames a second or less, so above 60 (a
    // 120 Hz phone at video.framerate = display, or no cap) the extra passes
    // only showed the same picture again, at full cost. At 60 and below
    // nothing changes. The cap the settings chose comes back afterwards.
    iWindowSetFrameRateCeiling(60);

    if (cam != NULL && raster != NULL)
    {
        while (true)
        {
            iWindowPump();
            if (iWindowShouldClose())
            {
                break;
            }

            const bool metering = iSndMeterOn();
            if (metering)
            {
                m_t = iHostMonotonicNs();
            }

            // Sound first: the device is what the picture is paced against, and
            // it must not be allowed to run dry while a frame is decoded.
            feedSound();

            // The clock. The audio device's is used when there is one, because
            // that is the one the player hears; a movie with no sound falls
            // back to the wall clock.
            F32 now;
            if (audio && info.sample_rate != 0)
            {
                U32 queued = iFMVAudioQueued();
                U32 played = (audio_written > queued) ? (audio_written - queued) : 0;
                now = (F32)played / (F32)info.sample_rate;

                // Hand over to the wall clock when the sound runs out.
                //
                // This clock is everything written minus everything still
                // queued, so once the track has ended and the device has
                // drained it stops: played stops rising and queued is already
                // zero. The picture is normally a frame or two longer than the
                // sound, and a movie paced on a stopped clock never reaches
                // those frames -- so it never decodes, never sees the end of
                // the file, and sits on the last thing it drew. It hangs.
                //
                // Taking over from where the audio clock stopped rather than
                // from the start of the movie keeps the two continuous, so the
                // last frames play at the same rate as everything before them.
                if (audio_drained && queued == 0)
                {
                    if (!handed_over)
                    {
                        handed_over = true;
                        clock_at_handover = now;
                        wall_at_handover = iTimeGet();
                    }
                    now = clock_at_handover + iTimeDiffSec(wall_at_handover, iTimeGet());
                }
            }
            else
            {
                now = iTimeDiffSec(started, iTimeGet());
            }

            if (metering)
            {
                U64 t = iHostMonotonicNs();
                m_audio += t - m_t;
                m_t = t;
            }

            if (!have_frame || now >= next_frame_at)
            {
                const void* pixels = NULL;
                U32 pitch = 0;
                F32 pts = -1.0f;
                bool ended = false;

                // A frame already a whole frame late is decoded but not shown,
                // so a display that presents no faster than the movie's own
                // rate (the 3DS at 30) can catch up with the sound. The sound
                // is topped up after each; a bounded number a pass keeps the
                // window serviced after a long stall.
                for (S32 skips = 0;
                     skips < 8 && have_frame && now >= next_frame_at + info.frame_time; skips++)
                {
                    if (!iFMVDecoderNextFrame(dec, NULL, NULL, &pts))
                    {
                        ended = true;
                        break;
                    }
                    next_frame_at =
                        (pts >= 0.0f) ? (pts + info.frame_time) : (next_frame_at + info.frame_time);
                    m_skipped++;
                    feedSound();
                }

                if (ended || !iFMVDecoderNextFrame(dec, &pixels, &pitch, &pts))
                {
                    break;
                }

                if (metering)
                {
                    U64 t = iHostMonotonicNs();
                    m_decode += t - m_t;
                    m_t = t;
                }

                uploadFrame(raster, pixels, pitch, info.width, info.height);
                have_frame = true;
                m_frames++;
                feedSound();

                if (metering)
                {
                    U64 t = iHostMonotonicNs();
                    m_upload += t - m_t;
                    m_t = t;
                }

                // The decoder's own timestamp when it has one, because a
                // container's frame durations are not always uniform; the
                // nominal frame time when it does not.
                next_frame_at = (pts >= 0.0f) ? (pts + info.frame_time)
                                              : (next_frame_at + info.frame_time);
                shown_at = next_frame_at - info.frame_time;
            }

            RwCameraBeginUpdate(cam);
            drawFrame(raster, sw, sh, x0, y0, x1, y1);
            RwCameraEndUpdate(cam);
            RwCameraShowRaster(cam, NULL, rwRASTERFLIPWAITVSYNC);

            if (metering)
            {
                m_present += iHostMonotonicNs() - m_t;
                m_loops++;

                char clock[256];
                double per = m_loops != 0 ? 1e6 * (double)m_loops : 1.0;
                snprintf(clock, sizeof(clock),
                         "movie %s clock %.3f s, picture %.3f s; %u loops, %u frames shown, %u "
                         "skipped; ms a loop: audio %.2f, decode %.2f, upload %.2f, draw and "
                         "present %.2f",
                         audio ? "audio" : "wall", now, shown_at, m_loops, m_frames, m_skipped,
                         (double)m_audio / per, (double)m_decode / per, (double)m_upload / per,
                         (double)m_present / per);
                if (iSndMeterTick(clock))
                {
                    m_loops = m_frames = m_skipped = 0;
                    m_audio = m_decode = m_upload = m_present = 0;
                }
            }

            // The skip button, and the one place `time` is honoured.
            //
            // Every caller passes a `time` -- 2.0 for the boot logos, 0.1 for
            // the attract demo -- and the console passes it down into Bink's
            // player, where what it does is not visible from here. Read as the
            // period before a skip is accepted it explains both numbers: a logo
            // cannot be dismissed the instant it appears, and the demo can be.
            // Recorded as an interpretation rather than a fact, because the
            // Bink side is not in the decomp.
            if (skippable && buttons != 0 && iTimeDiffSec(started, iTimeGet()) >= time)
            {
                iPadHostPoll();
                const iPadHostState* pad = iPadHostGet(0);
                if (pad != NULL && pad->connected && (pad->buttons & buttons) != 0)
                {
                    ended_with = pad->buttons & buttons;
                    break;
                }
            }
        }
    }

    iWindowSetFrameRateCeiling(0);

    if (audio)
    {
        iFMVAudioClose();
    }

    // TSSM_TEST_AUDIO_RESTART's "closed" moment.
    iAudioSDLTestMovie(path, 2);

    if (raster != NULL)
    {
        RwRasterDestroy(raster);
    }
    if (owned != NULL)
    {
        iCameraDestroy(owned);
    }
    iFMVDecoderClose(dec);

    // The movie's device is closed before the game's sounds come back.
    zGameSetOstrich(ostrich);
    xFMVPauseSound(false);

    // 0 is "ran to the end", and the button that stopped it otherwise.
    return ended_with;
}

// iFMVDecoder.h: asked here so the decoder needs no game header.
S32 iFMVRasterRed()
{
    return IRASTERFILL_RED;
}

// --- TSSM's other entry points ----------------------------------------------

MOVIE* iFMVOpen(PKRAssetTOCInfo& rAssetInfo)
{
    IPORT_STUB();
    return NULL;
}

void iFMVClose(MOVIE*& pMovie)
{
    pMovie = NULL;
}

void iFMVSuspend()
{
}

void iFMVResume()
{
}

void iFMVResetCallback()
{
}

void iFMVKill()
{
}

// zMain calls this once at boot; on the console it sets up Bink's memory and
// the VI. Nothing to do on a host: the decoder opens per movie.
void iFMVSystemInit()
{
}
