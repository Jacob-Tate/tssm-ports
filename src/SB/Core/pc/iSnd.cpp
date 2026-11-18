#include "iSnd.h"
#include "iSndData.h"
#include "iSndDsp.h"
#include "iSndGC.h"
#include "iSndHost.h"
#include "iSndXbox.h"

#include "gui/iGui.h"
#include "iFMVAudio.h"
#include "iFile.h"
#include "iHost.h"

#include "xSnd.h"
#include "xstransvc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The PC iSnd: TSSM's console iSnd (gc/iSnd.cpp) with the Xbox release's sound
// data under it and a software mixer (iSndHost.h) in place of FMOD.
//
// **The game's half is the console's, entry point by entry point.** xSndMgr
// hands this file a voice in gSnd.voice[0..47] and expects back exactly what
// FMOD gave the console: a voice that stays flagged 0x4000 while it plays and
// is cleared by iSndUpdate when it ends; looping sounds that lose their channel
// moved to a virtual voice (xSndMgr_SetInaudible); the lowest-priority one-shot
// stolen when every voice is busy; streams and cutscene tracks never stolen;
// the dialog flag (m_uSndMgrFlags 0x2) raised while category 2 plays; volume as
// the voice's level times its category's damped level, and pitch in semitones.
// Each function below says where it differs and why.
//
// **The data is the Xbox's.** The console reads FSB3 banks of GameCube
// DSP-ADPCM; the Xbox disc has a different sound table (SNDI: a 12-byte count
// header and 44-byte WAVEFORMATEX entries, iSndXbox.h) naming one SND or SNDS
// asset per sound, in Xbox ADPCM, and its cutscene tracks are 16-bit PCM inside
// the cutscene files. So the lookup, the loading and the cutscene feed are new;
// xSndMgr cannot tell, because iSndLookup still answers in the console's shape
// (uFlags 0x2 for a stream, 0x1 for a loop).
//
// **Or the GameCube's.** A package from the GameCube release -- a mod's, or
// every one when the game runs from a GameCube disc -- holds the console's own
// table (iSndGC.h): FSB3 banks of DSP-ADPCM inside the SNDI asset, and the
// cutscene tracks' DSP headers, their samples being in the cutscenes. Such a
// table is read in place and its sounds are decoded with iSndDsp.h: an effect
// whole (iSndData), a stream a few seconds ahead (iPumpStream), a cutscene
// track as its chunks come in (iPumpFeed), the history carried from block to
// block as the console's DSP does. Tables of both kinds can be loaded at once.
//
// Differences from the console a player could notice:
//
//  - Streams start at once. The console opens one stream at a time from disc
//    (pTempStreams, iSndUpdate) and plays it when it is open; here a stream
//    (an SNDS asset: music, speech, ambience) is read from the package and
//    decoded a few seconds ahead of the mixer from the first frame
//    (iPumpStream). Sound effects (SND) are decoded whole on their first play
//    and cached (iSndData.cpp).
//  - A cutscene track starts on the cutscene's first frame (iSndStartStereo,
//    which the console leaves empty), not when the cutscene's sound is set up a
//    few frames before.
//  - FMOD placed a 3D sound with its own panner, rolloff factor 0 (iSndInit:
//    no attenuation with distance; xSndMgr cuts a sound off past its outer
//    radius instead). Here the pan is equal-power on the listener's right axis
//    and the louder side kept at full level, so a centred sound is at full
//    volume in both speakers as on the console. TSSM_SND_ROLLOFF=1 adds an
//    inverse-distance falloff between the inner and outer radius, for
//    comparison.
//  - Dolby Pro Logic II (the console's default mode) is mixed as stereo.
//  - The cave reverb is an approximation of FMOD's (iSndReverb.cpp), applied
//    per voice as FMOD did: only to voices xSndMgr marks for it.
//  - The Settings tab's volumes (iSettingsVolumes), which the console does not
//    have, on top of everything the game does about volume. At 100 they change
//    nothing.
//
// Diagnostics: TSSM_SND=<n> logs the first n plays and every sound table
// loaded; TSSM_SNDMIX logs the live voices once a second, with how far ahead
// each stream is decoded; TSSM_SND_DUMP=<dir>
// writes each sound decoded, and each stream and cutscene track fed, there as
// WAV; TSSM_SND_TEST=<seconds> plays sounds of the first scene's tables
// through xSndMgr before the scene starts and reports how long each lasted
// (iSndSelfTest).

#define ISND_REAL_VOICES 48 // gSnd.voice[0..47] are played; 48..95 are virtual
#define ISND_MAX_TABLES 12 // gc/iSnd.cpp's sinfoArray

// ---------------------------------------------------------------------------
// Sound tables

struct pcentry
{
    iSndXboxEntry fmt; // the table's entry, verbatim (a GameCube sound's: made up, below)
    U32 offset; // the asset's bytes in the package; size 0 when it has none
    U32 size; // (a CSSS: its PCM is in a cutscene)
    U8 flags; // iSndFileInfo's uFlags
    const iSndGCSound* gc; // a GameCube table's sound, or NULL
};

// The format_tag a GameCube sound's made-up entry carries: no WAVEFORMATEX tag,
// so nothing that reads Xbox entries takes it for one.
#define ISND_FORMAT_GCDSP 0xD5D5

struct pctable
{
    U32 sndiID;
    char path[256]; // the package, as iFileOpen takes it
    U32 count;
    U32 numSfx;
    iSndFileInfo* info; // sorted by asset id, for iSndLookup
    pcentry* entry; // parallel to info
    bool sorted; // false only if sorting ran out of memory
    iSndGCTable gc; // a GameCube table's sounds and cutscene tracks; empty for Xbox
};

static pctable sTables[ISND_MAX_TABLES];
static U32 sTableCount;

// ---------------------------------------------------------------------------
// Voices

struct pcslot
{
    S32 host; // the host voice, or -1
    const void* data; // the iSndData block it plays, or NULL
    bool looping;
    bool paused; // by iSndPause
    U32 channels; // of the source: a stereo source is not panned

    // A stream (an SNDS asset), read and decoded ahead of the mixer.
    bool stream;
    bool streamEnded; // the last block is fed (iSndHostFeedEnd)
    bool fileOpen;
    tag_xFile file;
    U32 srcOffset; // the asset in the package
    U32 srcSize;
    U32 srcPos; // bytes of it fed so far
    U32 blockAlign;
    U32 formatTag;
    U32 rate;
    bool dumpWrapped; // a looping stream is dumped once

    // A cutscene track (voice flag 0x1), fed from the cutscene's chunks.
    bool feed;
    bool held; // waiting for iSndStartStereo
    U32 feedAsset;
    U32 feedFrameBytes;
    U8 carry[ISNDDSP_FRAME_BYTES * ISNDDSP_MAX_CHANNELS]; // a frame split across two blocks
    U32 carryBytes;

    // A stream or cutscene track of a GameCube table: DSP-ADPCM, decoded with
    // its history carried from one read to the next.
    bool dsp;
    iSndDspChannel dspCh[ISNDDSP_MAX_CHANNELS];
    iSndDspState dspState;
    U32 dspLeft; // sample frames still to play (the last block is padded)
    U32 dspTotal;

    // TSSM_SND_DUMP's copy of a fed track.
    S16* dump;
    U32 dumpFrames;
    U32 dumpCap;
    U32 dumpRate;
};

static pcslot sSlots[ISND_REAL_VOICES];

#define ISND_FEED_CAPACITY (1U << 19) // frames: 10.9 s at 48 kHz
#define ISND_FEED_MARGIN (1U << 17) // the most one pull may bring, with room

// A stream is kept this far ahead of the mixer, decoded, which covers a game
// frame of that length; its ring holds twice that.
#define ISND_STREAM_AHEAD_SEC 3
// Below this much the stream is filled at once; above it, a game frame decodes
// at least 1/ISND_STREAM_STEP_DIV of a second more (in whole reads) until it is
// ISND_STREAM_AHEAD_SEC ahead. A stream's start (music, some voices) used to
// decode all three seconds in the frame that started it, a hitch of its own
// (docs/PACING.md); now that frame decodes one second and the next frames a
// read or so each. The step is counted in sample frames, not reads, because a
// read's length depends on the format; an eighth of a second outruns a frame
// at any rate the game runs at.
#define ISND_STREAM_MUST_SEC 1
#define ISND_STREAM_STEP_DIV 8
#define ISND_STREAM_CAPACITY (1U << 19)
#define ISND_STREAM_BLOCKS 128 // ADPCM blocks per read
#define ISND_MAX_BLOCK_ALIGN (36 * 8)

static U8 sStreamRead[ISND_STREAM_BLOCKS * ISND_MAX_BLOCK_ALIGN];
static S16 sStreamPcm[ISND_STREAM_BLOCKS * 64 * 8];

static bool sMono;
static bool sRolloff;
static bool sReverbOn;
static S32 sReportPlays;
static bool sReportTables;
static bool sReportMix;
static U64 sLastMixReportNs;
static bool sSaidTooManyTables;
static bool sSaidNoTrack;

// The console's cave reverb is FMOD's AUXFX "ReverbHi" (gc/iSnd.cpp:
// FSOUND_AUXFX_SetReverbHi(1.5f, 0.008f, 0.0f, 1.0f, 0.0f, 0.5f, 0)): a 1.5 s
// decay after an 8 ms pre-delay, undamped, mixed half wet. iSndReverb is an
// I3DL2 reverb, so this is that description in I3DL2's terms: the decay, the
// delay and a flat high-frequency ratio from FMOD's numbers, the levels from
// the I3DL2 cave preset at about half wet.
static const iSndHostReverb kCaveReverb = {
    -1000, // room, mB
    0, // room_hf, mB
    0.0f, // room_rolloff_factor
    1.5f, // decay_time, seconds
    1.0f, // decay_hf_ratio
    -1363, // reflections, mB relative to room
    0.008f, // reflections_delay, seconds
    -1153, // reverb, mB relative to room
    0.011f, // reverb_delay, seconds
    100.0f, // diffusion, per cent
    100.0f, // density, per cent
    5000.0f, // hf_reference, Hz
};

static pctable* iTableOf(const iSndFileInfo* fi)
{
    if (fi == NULL || fi->uTableIndex >= sTableCount)
    {
        return NULL;
    }

    pctable* t = &sTables[fi->uTableIndex];
    if (fi < t->info || fi >= t->info + t->count)
    {
        return NULL;
    }

    return t;
}

static pcentry* iEntryOf(const iSndFileInfo* fi)
{
    pctable* t = iTableOf(fi);
    return t != NULL ? &t->entry[fi - t->info] : NULL;
}

static S32 iSlotOf(const xSndVoiceInfo* vp)
{
    S32 i = (S32)(vp - gSnd.voice);
    return (i >= 0 && i < ISND_REAL_VOICES) ? i : -1;
}

static void iDumpFeedFlush(pcslot* s)
{
    if (s->dump == NULL)
    {
        return;
    }

    const char* dir = iSndDataDumpDir();
    if (dir != NULL && s->dumpFrames != 0)
    {
        char path[600];
        snprintf(path, sizeof(path), "%s/%08x_%s.wav", dir, s->feedAsset,
                 s->stream ? "stream" : "cutscene");
        iSndXboxWriteWav(path, s->dump, s->dumpFrames, s->feedFrameBytes / 2, s->dumpRate);
    }

    free(s->dump);
    s->dump = NULL;
    s->dumpFrames = 0;
    s->dumpCap = 0;
}

// Whatever slot i was playing, it is not any more. The host voice is stopped
// before its samples are unpinned: iSndHostStop drops the mixer's pointer.
static void iReleaseSlot(S32 i)
{
    pcslot* s = &sSlots[i];

    if (s->host >= 0)
    {
        iSndHostStop(s->host);
        iSndHostRelease(s->host);
    }

    if (s->data != NULL)
    {
        iSndDataRelease(s->data);
    }

    if (s->feed || s->stream)
    {
        iDumpFeedFlush(s);
    }

    if (s->fileOpen)
    {
        iFileClose(&s->file);
    }

    memset(s, 0, sizeof(*s));
    s->host = -1;
}

// ---------------------------------------------------------------------------
// The mix

// 2^(semitones / 12): the console's iSndSetPitch scales the sample's own rate
// by exactly this, and by the voice's doppler factor.
static F32 iPitchRatio(F32 semitones, F32 doppler)
{
    F32 ratio = (F32)pow(2.0, semitones / 12.0f);
    if (doppler > 0.0f)
    {
        ratio *= doppler;
    }

    return ratio < 0.01f ? 0.01f : (ratio > 16.0f ? 16.0f : ratio);
}

static void iApplyPitch(S32 i, F32 pitch)
{
    if (sSlots[i].host >= 0)
    {
        iSndHostSetPitch(sSlots[i].host, iPitchRatio(pitch, gSnd.voice[i].dopplerFreqCoef));
    }
}

// The console's iSndSetVol: the voice's level times its category's damped
// level, clamped, as FMOD's 0..255 volume. Then the PC's pan for a 3D voice
// and the side of a split cutscene track.
static void iApplyMix(S32 i, F32 vol)
{
    pcslot* s = &sSlots[i];
    xSndVoiceInfo* vp = &gSnd.voice[i];

    if (s->host < 0)
    {
        return;
    }

    F32 gain = vol * xSndMgrGetDampedVolume(vp->category);
    gain = gain < 0.0f ? 0.0f : (gain > 1.0f ? 1.0f : gain);

    F32 left = gain;
    F32 right = gain;

    if ((vp->flags & 0x8000) && s->channels < 2)
    {
        // FMOD's listener is gSnd.pos looking along gSnd.at; the console
        // passes it the voice's actualPos (not playPos), mirrored in x because
        // FMOD is left-handed. In the game's right-handed frame the listener's
        // right is at x up, which is -gSnd.right (RenderWare's "right" vector
        // points to the viewer's left).
        xVec3 d;
        xVec3Sub(&d, &vp->actualPos, &gSnd.pos);
        F32 dist = xVec3Length(&d);

        if (dist > 0.0001f)
        {
            F32 pan = -xVec3Dot(&d, &gSnd.right) / dist;
            pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);

            F32 theta = (pan + 1.0f) * 0.25f * 3.14159265f;
            F32 l = cosf(theta);
            F32 r = sinf(theta);
            F32 m = l > r ? l : r;
            left *= l / m;
            right *= r / m;
        }

        if (sRolloff)
        {
            F32 mn = vp->innerRadius > 1.0f ? vp->innerRadius : 1.0f;
            F32 mx = vp->outerRadius > mn ? vp->outerRadius : mn;
            F32 att = dist <= mn ? 1.0f : (dist >= mx ? 0.0f : mn / dist);
            left *= att;
            right *= att;
        }
    }

    // A cutscene track split over two voices (xSndMgrPlay's 0x10 and 0x20,
    // iCSLoadStep) is a left and a right channel.
    if (s->feed && s->channels < 2)
    {
        if (vp->flags & 0x10)
        {
            right = 0.0f;
        }
        else if (vp->flags & 0x20)
        {
            left = 0.0f;
        }
    }

    if (sMono)
    {
        F32 m = left > right ? left : right;
        left = m;
        right = m;
    }

    iSndHostSetVol(s->host, left, right);
}

// The console's effect level: a voice xSndMgr marked for effects (0x80000)
// that starts while the scene's effect is the cave gets the reverb.
static void iApplySend(S32 i)
{
    if (sSlots[i].host < 0)
    {
        return;
    }

    bool send = m_eCurrentEffect == xSndEffect_CAVE && (gSnd.voice[i].flags & 0x80000);
    if (send && !sReverbOn)
    {
        iSndHostSetReverb(&kCaveReverb);
        sReverbOn = true;
    }

    iSndHostSetSend(sSlots[i].host, send);

    // Which of the Settings tab's volumes it answers to: category 3 is the
    // music (the game's own music setting is on it alone), 4 a cutscene's track,
    // whose category the console's movies take their level from as well
    // (xFMVGetBinkCompliantVolume), and the rest are effects and speech.
    const S16 category = gSnd.voice[i].category;
    iSndHostSetGroup(sSlots[i].host, category == 3 ? ISNDHOST_GROUP_MUSIC :
                                     category == 4 ? ISNDHOST_GROUP_MOVIE :
                                                     ISNDHOST_GROUP_EFFECTS);
}

// The Settings tab's volumes, 0..1, at the start and each time a slider moves
// (iGuiSetAudioListener). They sit under the game's: a voice's level and the
// game's own music and effects settings are applied by iApplyMix as ever, and
// the movie's by iFMV.cpp. All of it multiplies, and 1 times anything is that
// thing to the bit.
static F32 iVolume01(F32 v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static void iSettingsVolumes(F32 master, F32 music, F32 sfx, F32 movies)
{
    // config.ini can be edited by hand.
    master = iVolume01(master);
    movies = master * iVolume01(movies);

    iSndHostSetGroupGain(ISNDHOST_GROUP_EFFECTS, master * iVolume01(sfx));
    iSndHostSetGroupGain(ISNDHOST_GROUP_MUSIC, master * iVolume01(music));
    iSndHostSetGroupGain(ISNDHOST_GROUP_MOVIE, movies);
    iFMVAudioSetVolume(movies);
}

// ---------------------------------------------------------------------------
// Lifecycle

static void iFreeTables()
{
    for (U32 i = 0; i < sTableCount; i++)
    {
        free(sTables[i].info);
        free(sTables[i].entry);
        iSndGCFreeTable(&sTables[i].gc);
    }

    memset(sTables, 0, sizeof(sTables));
    sTableCount = 0;
}

void iSndInit()
{
    xSndVoiceInfo* voice;

    for (voice = gSnd.voice; voice < gSnd.voice + 96; voice++)
    {
        voice->ps.channel = -1;
    }

    for (S32 i = 0; i < ISND_REAL_VOICES; i++)
    {
        memset(&sSlots[i], 0, sizeof(sSlots[i]));
        sSlots[i].host = -1;
    }

    iFreeTables();
    iSndDataReset();
    iSndHostInit();
    iGuiSetAudioListener(iSettingsVolumes);

    sReverbOn = false;
    sRolloff = getenv("TSSM_SND_ROLLOFF") != NULL && getenv("TSSM_SND_ROLLOFF")[0] == '1';

    sReportPlays = 0;
    sReportTables = false;
    {
        const char* env = getenv("TSSM_SND");
        if (env != NULL)
        {
            sReportTables = true;
            sReportPlays = atoi(env);
            if (sReportPlays <= 0)
            {
                sReportPlays = 32;
            }
        }
    }

    sReportMix = getenv("TSSM_SNDMIX") != NULL;

    // The console asks the IPL whether the console is set to mono; a host has
    // no such setting, so it starts in the console's other answer.
    gSnd.sndMode = xSndMode_Dolby;
    iSndSetMode();

    printf("iSnd: audio backend is %s\n", iSndHostName());
    if (iSndDataDumpDir() != NULL)
    {
        printf("iSnd: TSSM_SND_DUMP: decoded sounds go to %s\n", iSndDataDumpDir());
    }
    fflush(stdout);
}

void iSndExit()
{
    iGuiSetAudioListener(NULL);

    for (S32 i = 0; i < ISND_REAL_VOICES; i++)
    {
        iReleaseSlot(i);
    }

    iSndHostExit();
    iSndDataReset();
    iFreeTables();
}

// The console's speaker mode. Mono mixes every voice to the centre; stereo and
// Dolby Pro Logic II are both stereo here.
void iSndSetMode()
{
    sMono = gSnd.sndMode == xSndMode_Mono;
}

void iSndSetEnvironmentalEffect(S32 effectType)
{
    switch (effectType)
    {
    case iSND_EFFECT_NONE:
        // FSOUND_AUXFX_SetLevels(FSOUND_ALL, 0, 0): every voice dry.
        for (S32 i = 0; i < ISND_REAL_VOICES; i++)
        {
            if (sSlots[i].host >= 0)
            {
                iSndHostSetSend(sSlots[i].host, false);
            }
        }
        if (sReverbOn)
        {
            iSndHostSetReverb(NULL);
            sReverbOn = false;
        }
        break;
    case iSND_EFFECT_CAVE:
        // As on the console: nothing until a voice for it starts (iApplySend).
        break;
    }
}

void iSndEffectShutdown()
{
    if (sReverbOn)
    {
        iSndHostSetReverb(NULL);
        sReverbOn = false;
    }
}

// The console sets the Dolby reverb's parameters here and turns no voice's
// level up; the reverb is still started by the first voice that wants it.
void iSndEnableEffects(bool enable)
{
}

// ---------------------------------------------------------------------------
// Loading the tables

struct iPkgReader
{
    tag_xFile* file;
};

static U32 iReadPkg(void* ctx, U32 offset, void* buf, U32 size)
{
    tag_xFile* file = ((iPkgReader*)ctx)->file;
    if (iFileSeek(file, (S32)offset, IFILE_SEEK_SET) < 0)
    {
        return 0;
    }

    return iFileRead(file, buf, size);
}

// The reader iSndGCReadTable is given: the SNDI asset, in place in its
// package. A GameCube table holds its sound banks (mnuc.HIP's is 131 MB), so
// it is not read whole.
struct iGCTableReader
{
    iPkgReader pkg;
    U32 base;
    U32 size;
};

static U32 iReadGCTable(void* ctx, U32 offset, void* buf, U32 size)
{
    iGCTableReader* r = (iGCTableReader*)ctx;
    if (offset > r->size || size > r->size - offset)
    {
        return 0;
    }
    return iReadPkg(&r->pkg, r->base + offset, buf, size);
}

static const iSndHipAsset* iFindHipAsset(const iSndHipAsset* list, S32 n, U32 aid)
{
    S32 lo = 0;
    S32 hi = n;
    while (hi > lo)
    {
        S32 mid = (lo + hi) / 2;
        if (list[mid].aid == aid)
        {
            return &list[mid];
        }
        if (list[mid].aid < aid)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }

    return NULL;
}

static pctable* sSortTable;

static int iCompareByAid(const void* a, const void* b)
{
    U32 x = sSortTable->entry[*(const U16*)a].fmt.assetID;
    U32 y = sSortTable->entry[*(const U16*)b].fmt.assetID;
    return x < y ? -1 : (x > y ? 1 : 0);
}

// One SNDI asset, which the packer does not load (its layer, SNDTOC, is
// PKR_LDDEST_SKIP), read from its package as gc/iSnd.cpp's loadSoundInfoAsset
// reads it. The sound assets' byte ranges come from the same package's table
// of contents, read here: an id can be in several loaded packages at different
// offsets, and the packer's lookups (xSTGetAssetInfo, xST_xAssetID_HIPFullPath)
// may answer from two different ones. Every sound a table names is in the
// table's own package (all 3,356 entries on the Xbox disc); a GameCube table
// holds its sounds itself.
static bool iLoadTable(U32 index, const PKRAssetTOCInfo& sndi)
{
    pctable* t = &sTables[index];
    const char* path = xST_xAssetID_HIPFullPath(sndi.aid);

    memset(t, 0, sizeof(*t));
    if (path == NULL)
    {
        return false;
    }

    t->sndiID = sndi.aid;
    snprintf(t->path, sizeof(t->path), "%s", path);

    tag_xFile file;
    memset(&file, 0, sizeof(file));
    if (iFileOpen(t->path, IFILE_OPEN_READ, &file) != 0)
    {
        return false;
    }

    iPkgReader reader = { &file };

    // The package's sound assets and the table itself.
    static const U32 kTypes[] = { 'SNDI', 'SND ', 'SNDS', 0 };
    const S32 maxAssets = 8192;
    iSndHipAsset* assets = (iSndHipAsset*)malloc(sizeof(iSndHipAsset) * maxAssets);
    S32 nAssets =
        assets != NULL ? iSndHipFindAssets(iReadPkg, &reader, kTypes, assets, maxAssets) : -1;

    // The table's bytes: from the package's own contents if it is there, from
    // the packer's sector and offset (gc/iSnd.cpp's arithmetic) if not.
    U32 sndiOffset = sndi.sector * iFileGetSectorSize(XFILE_DRIVE_CDVD) + sndi.plus_offset;
    U32 sndiSize = sndi.size;
    const iSndHipAsset* self = nAssets > 0 ? iFindHipAsset(assets, nAssets, sndi.aid) : NULL;
    if (self != NULL)
    {
        sndiOffset = self->offset;
        sndiSize = self->size;
    }

    // Which release's table: an Xbox one is exactly 12 + 44 * n bytes by its
    // own counts, a GameCube one starts with the console's header (iSndGC.h).
    U8 head[0x20];
    const U32 headBytes = sndiSize < sizeof(head) ? sndiSize : (U32)sizeof(head);
    const bool headOk =
        headBytes != 0 && iReadPkg(&reader, sndiOffset, head, headBytes) == headBytes;
    U32 count = 0;
    const bool xbox = headOk && headBytes >= sizeof(iSndXboxTable) &&
                      iSndXboxTableEntries(head, sndiSize, &count) != NULL;
    const bool gamecube = !xbox && headOk && iSndGCLooksLikeTable(head, headBytes, sndiSize);

    U8* raw = NULL;
    const iSndXboxEntry* entries = NULL;
    bool gcOk = false;
    char why[160];
    why[0] = '\0';

    if (xbox)
    {
        raw = (U8*)malloc(sndiSize);
        if (raw != NULL && iReadPkg(&reader, sndiOffset, raw, sndiSize) == sndiSize)
        {
            entries = iSndXboxTableEntries(raw, sndiSize, &count);
        }
    }
    else if (gamecube)
    {
        iGCTableReader gr = { reader, sndiOffset, sndiSize };
        gcOk = iSndGCReadTable(iReadGCTable, &gr, sndiSize, &t->gc, why, sizeof(why)) &&
               t->gc.numSounds <= 0xFFFF;
        count = gcOk ? t->gc.numSounds : 0;
    }
    iFileClose(&file);

    if ((entries == NULL && !gcOk) || count > 0xFFFF)
    {
        if (gamecube)
        {
            printf("iSnd: sound table %08x in %s is a GameCube table the port cannot read (%s); "
                   "its sounds are silent\n",
                   sndi.aid, t->path, why[0] != '\0' ? why : "too many sounds");
        }
        else
        {
            printf("iSnd: sound table %08x in %s is not an Xbox or GameCube SNDI (%u bytes); its "
                   "sounds are silent\n",
                   sndi.aid, t->path, sndiSize);
        }
        fflush(stdout);
        free(raw);
        free(assets);
        iSndGCFreeTable(&t->gc);
        memset(t, 0, sizeof(*t));
        return false;
    }

    t->count = count;
    t->numSfx = gcOk ? count - t->gc.numStreams : ((const iSndXboxTable*)raw)->num_sfx;
    t->info = (iSndFileInfo*)calloc(count != 0 ? count : 1, sizeof(iSndFileInfo));
    t->entry = (pcentry*)calloc(count != 0 ? count : 1, sizeof(pcentry));
    U16* order = (U16*)malloc(sizeof(U16) * (count != 0 ? count : 1));

    if (t->info == NULL || t->entry == NULL || order == NULL)
    {
        free(t->info);
        free(t->entry);
        free(order);
        free(raw);
        free(assets);
        iSndGCFreeTable(&t->gc);
        memset(t, 0, sizeof(*t));
        return false;
    }

    U32 located = 0;
    for (U32 k = 0; k < count; k++)
    {
        pcentry* e = &t->entry[k];

        if (gcOk)
        {
            // An entry made up from the GameCube sound, so that what reads
            // entries (the reports, the self test) has its rate and channels.
            const iSndGCSound* g = &t->gc.sounds[k];
            memset(&e->fmt, 0, sizeof(e->fmt));
            e->fmt.format_tag = ISND_FORMAT_GCDSP;
            e->fmt.channels = g->channels;
            e->fmt.samples_per_sec = g->rate;
            e->fmt.block_align = (U16)(ISNDDSP_FRAME_BYTES * g->channels);
            e->fmt.bits_per_sample = 4;
            e->fmt.data_size = g->size;
            e->fmt.assetID = g->assetID;
            e->fmt.loop_flag = g->flags & 0x1;
            e->flags = g->flags & 0x3;
            e->gc = g;

            // The samples are inside the table itself.
            e->offset = sndiOffset + g->offset;
            e->size = g->size;
            located++;
        }
        else
        {
            e->fmt = entries[k];
            e->flags = (k >= t->numSfx ? 0x2 : 0) | (entries[k].loop_flag != 0 ? 0x1 : 0);

            const iSndHipAsset* a =
                nAssets > 0 ? iFindHipAsset(assets, nAssets, entries[k].assetID) : NULL;
            if (a != NULL && a->type != 'SNDI')
            {
                e->offset = a->offset;
                e->size = a->size;
                located++;
            }
        }

        order[k] = (U16)k;
    }

    // Sorted by id, for the binary search the console's iSndLookup does (its
    // tables come sorted; the Xbox's are sound effects first, then streams).
    sSortTable = t;
    qsort(order, count, sizeof(U16), iCompareByAid);

    pcentry* sorted = (pcentry*)malloc(sizeof(pcentry) * (count != 0 ? count : 1));
    if (sorted != NULL)
    {
        for (U32 k = 0; k < count; k++)
        {
            sorted[k] = t->entry[order[k]];

            iSndFileInfo* fi = &t->info[k];
            fi->uAssetID = sorted[k].fmt.assetID;
            fi->uFlags = sorted[k].flags;
            fi->uTableIndex = (U8)index;
            fi->uEntryIndex = (U16)k;
        }

        free(t->entry);
        t->entry = sorted;
        t->sorted = true;
    }
    else
    {
        // Unsorted is still correct, only slower: iSndLookup walks it.
        for (U32 k = 0; k < count; k++)
        {
            iSndFileInfo* fi = &t->info[k];
            fi->uAssetID = t->entry[k].fmt.assetID;
            fi->uFlags = t->entry[k].flags;
            fi->uTableIndex = (U8)index;
            fi->uEntryIndex = (U16)k;
        }
    }

    if (sReportTables)
    {
        printf("iSnd: table %08x from %s: %u sounds, %u streams; %u of them in the package%s\n",
               sndi.aid, t->path, t->numSfx, count - t->numSfx, located,
               gcOk ? " (a GameCube table)" : "");
        if (gcOk && t->gc.numCutscene != 0)
        {
            printf("iSnd:   and %u cutscene track(s)\n", t->gc.numCutscene);
        }
        fflush(stdout);
    }

    free(order);
    free(raw);
    free(assets);
    return true;
}

// A sound's length in sample frames, from its table entry alone.
static U32 iEntryFrames(const pcentry* e)
{
    if (e == NULL)
    {
        return 0;
    }
    return e->gc != NULL ? e->gc->samples : iSndXboxFrames(&e->fmt);
}

// A GameCube table's cutscene track of that id, or NULL.
static const iSndGCCutscene* iFindGCCutscene(U32 id)
{
    for (U32 i = 0; i < sTableCount; i++)
    {
        const iSndGCTable* g = &sTables[i].gc;
        for (U32 j = 0; j < g->numCutscene; j++)
        {
            if (g->cutscenes[j].assetID == id)
            {
                return &g->cutscenes[j];
            }
        }
    }
    return NULL;
}

// TSSM_SND_TEST: the game's whole sound path, once, on the first tables
// loaded. One sound effect and one stream of each table (a stereo stream if
// there is one) are played through xSndMgrPlay -- sound group, iSndLookup,
// iSndGetFreeVoiceInfo, iSndPlay, the mixer -- and xSndMgrUpdate is run until
// each has ended, as the game loop would, or `seconds` have passed. Each
// sound's measured length is reported against the length its table entry
// implies. With TSSM_SND_DUMP the decoded audio is written out as well.
//
// Diagnostics only: it holds up the scene's start while it runs.
static void iSndSelfTest(S32 seconds)
{
    if (seconds <= 0)
    {
        seconds = 20;
    }
    if (seconds > 60)
    {
        seconds = 60;
    }

    struct pick
    {
        U32 aid;
        bool stream;
        F32 expect;
        iSndHandle handle;
        U64 endNs;
    };

    pick picks[2 * ISND_MAX_TABLES];
    S32 n = 0;

    for (U32 t = 0; t < sTableCount; t++)
    {
        pctable* tb = &sTables[t];
        S32 sfx = -1;
        S32 stream = -1;

        for (U32 k = 0; k < tb->count; k++)
        {
            const pcentry* e = &tb->entry[k];
            if (e->size == 0)
            {
                continue;
            }

            if (!(tb->info[k].uFlags & 0x2))
            {
                if (sfx < 0 && !(tb->info[k].uFlags & 0x1))
                {
                    sfx = (S32)k;
                }
            }
            else if (stream < 0 || (e->fmt.channels == 2 && tb->entry[stream].fmt.channels != 2))
            {
                stream = (S32)k;
            }
        }

        S32 chosen[2] = { sfx, stream };
        for (S32 c = 0; c < 2; c++)
        {
            if (chosen[c] < 0)
            {
                continue;
            }

            const pcentry* e = &tb->entry[chosen[c]];
            picks[n].aid = e->fmt.assetID;
            picks[n].stream = c == 1;
            picks[n].expect = e->fmt.samples_per_sec != 0 ?
                                  (F32)iEntryFrames(e) / (F32)e->fmt.samples_per_sec :
                                  0.0f;
            picks[n].handle = (iSndHandle)-1;
            picks[n].endNs = 0;
            n++;
        }
    }

    printf("iSnd: TSSM_SND_TEST: playing %d sounds through xSndMgr (up to %d s)\n", n, seconds);
    fflush(stdout);

    U64 start = iHostMonotonicNs();
    for (S32 k = 0; k < n; k++)
    {
        xSndGroup* g = xSndMgr_AllocGroup(1);
        g->aSndGroupInfo[0].uSoundNameHash = picks[k].aid;
        g->header.eSoundCategory = 0;
        g->header.uPriority = 0x80;
        F32 vol = 0.5f;
        F32 pitch = 0.0f;
        picks[k].handle = xSndMgrPlay((iSndGroupHandle)(UPtr)g, 0, NULL, NULL, NULL, &vol, &pitch);
        if (picks[k].handle == (iSndHandle)-1)
        {
            picks[k].endNs = start;
        }
    }

    U64 limit = start + (U64)seconds * 1000000000ULL;
    for (;;)
    {
        U64 now = iHostMonotonicNs();
        bool any = false;

        xSndMgrUpdate();

        for (S32 k = 0; k < n; k++)
        {
            if (picks[k].endNs != 0)
            {
                continue;
            }
            if (!xSndMgrIsPlaying(picks[k].handle))
            {
                picks[k].endNs = now;
            }
            else
            {
                any = true;
            }
        }

        if (!any || now >= limit)
        {
            break;
        }

        iHostSleepUntilNs(now + 16000000ULL);
    }

    S32 bad = 0;
    for (S32 k = 0; k < n; k++)
    {
        bool started = picks[k].handle != (iSndHandle)-1;
        bool ended = picks[k].endNs != 0;
        F32 took = ended ? (F32)((double)(picks[k].endNs - start) / 1e9) : -1.0f;
        bool ok =
            started && ended && took > picks[k].expect - 0.1f && took < picks[k].expect + 0.25f;

        // Longer than the test runs for: it only has to be still playing.
        if (started && !ended && picks[k].expect > (F32)seconds)
        {
            ok = true;
        }

        bad += !ok;
        printf("iSnd: TSSM_SND_TEST: %08x %-6s %s; table length %.2f s, played %s%.2f s  %s\n",
               picks[k].aid, picks[k].stream ? "stream" : "sound",
               started ? "started" : "DID NOT START", picks[k].expect, ended ? "" : "> ",
               ended ? took : (F32)seconds, ok ? "ok" : "WRONG");

        if (started && !ended)
        {
            xSndMgrStop(picks[k].handle);
        }
    }

    xSndMgrUpdate();
    printf("iSnd: TSSM_SND_TEST: %d of %d as expected\n", n - bad, n);
    fflush(stdout);
}

void iSndInitSceneLoaded()
{
    S32 count = xSTAssetCountByType('SNDI');

    for (S32 i = 0; i < count; i++)
    {
        PKRAssetTOCInfo xinfo;
        if (!xSTGetAssetInfoByType('SNDI', i, &xinfo))
        {
            continue;
        }

        bool loaded = false;
        for (U32 j = 0; j < sTableCount; j++)
        {
            if (sTables[j].sndiID == xinfo.aid)
            {
                loaded = true;
                break;
            }
        }

        if (loaded)
        {
            continue;
        }

        if (sTableCount >= ISND_MAX_TABLES)
        {
            // The console writes past sinfoArray here. Refusing costs this
            // table its sounds rather than the process its memory.
            if (!sSaidTooManyTables)
            {
                sSaidTooManyTables = true;
                printf("iSnd: more than %d sound tables loaded; %08x and any after it are "
                       "ignored\n",
                       ISND_MAX_TABLES, xinfo.aid);
                fflush(stdout);
            }
            break;
        }

        if (iLoadTable(sTableCount, xinfo))
        {
            sTableCount++;
        }
    }

    static bool sSelfTested = false;
    if (!sSelfTested && sTableCount != 0 && getenv("TSSM_SND_TEST") != NULL)
    {
        sSelfTested = true;
        iSndSelfTest(atoi(getenv("TSSM_SND_TEST")));
    }
}

// The console frees every table here and loads them all again at the next
// iSndInitSceneLoaded; xSndMgrSceneExit has stopped every sound first. A voice
// still holding an entry would hold a dangling pointer, so any left is ended
// here, which the console would not need to do.
void iSndSceneExit()
{
    for (S32 i = 0; i < 96; i++)
    {
        xSndVoiceInfo* vp = &gSnd.voice[i];
        if (vp->pSndFileInfo != NULL && iTableOf(vp->pSndFileInfo) != NULL)
        {
            if (i < ISND_REAL_VOICES)
            {
                iReleaseSlot(i);
            }
            vp->flags = 0;
            vp->ps.channel = -1;
            vp->pSndFileInfo = NULL;
        }
    }

    iFreeTables();
    iSndEffectShutdown();
}

// ---------------------------------------------------------------------------
// Lookup

iSndFileInfo* iSndLookup(U32 id)
{
    for (U32 i = 0; i < sTableCount; i++)
    {
        pctable* t = &sTables[i];
        S32 lo = 0;
        S32 hi = (S32)t->count;

        if (!t->sorted)
        {
            for (U32 j = 0; j < t->count; j++)
            {
                if (t->info[j].uAssetID == id)
                {
                    return &t->info[j];
                }
            }
            continue;
        }

        while (hi > lo)
        {
            S32 mid = (lo + hi) / 2;
            if (t->info[mid].uAssetID == id)
            {
                return &t->info[mid];
            }
            if (id > t->info[mid].uAssetID)
            {
                lo = mid + 1;
            }
            else
            {
                hi = mid;
            }
        }
    }

    return NULL;
}

// The table entry of any sound a table names, cutscene tracks included (they
// are entries of the stream section with no asset bytes).
static pcentry* iFindEntry(U32 id)
{
    iSndFileInfo* fi = iSndLookup(id);
    return fi != NULL ? iEntryOf(fi) : NULL;
}

// ---------------------------------------------------------------------------
// Voices

xSndVoiceInfo* iSndGetFreeVoiceInfo(U32 priority, U32 flags)
{
    U32 lo_priority = 0xFFFFFFFF;
    U32 lo_voice = 0;
    S32 voiceIsUnused = FALSE;
    U32 i;

    for (i = 0; i < ISND_REAL_VOICES; i++)
    {
        if (gSnd.voice[i].flags != 0)
        {
            if (gSnd.voice[i].priority < lo_priority)
            {
                lo_priority = gSnd.voice[i].priority;
                lo_voice = i;
            }
        }
        else
        {
            lo_voice = i;
            lo_priority = 0;
            voiceIsUnused = TRUE;
            break;
        }
    }

    if (voiceIsUnused || priority > lo_priority)
    {
        xSndVoiceInfo* v = &gSnd.voice[lo_voice];
        pcslot* s = &sSlots[lo_voice];

        // The console steals a playing one-shot; a loop or a stream is kept,
        // and then the caller gets nothing. The host slot, not ps.channel, says
        // whether something is playing: shared code copies voices by value, so
        // a voice's channel can be another slot's.
        if (s->host >= 0)
        {
            bool isstreaming =
                (v->flags & 0x1) || (v->pSndFileInfo != NULL && (v->pSndFileInfo->uFlags & 0x2));
            if (!s->looping && !isstreaming)
            {
                iSndStop(v);
            }
        }

        if (s->host >= 0 && iSndHostIsPlaying(s->host))
        {
            return NULL;
        }

        // Finished but not yet retired by iSndUpdate.
        if (s->host >= 0)
        {
            iReleaseSlot((S32)lo_voice);
        }

        v->ps.channel = -1;
        return v;
    }

    return NULL;
}

static void iReport(const xSndVoiceInfo* vp, const char* kind, const pcentry* e, U32 frames,
                    bool looping, bool nodata, U32 rate = 0, U32 channels = 0)
{
    if (sReportPlays <= 0)
    {
        return;
    }

    sReportPlays--;
    printf("iSnd: play %08x %-8s %5u Hz %u ch %8u frames%s%s cat %d vol %.2f pitch %+.2f%s\n",
           vp->assetID, kind, e != NULL ? e->fmt.samples_per_sec : rate,
           e != NULL ? e->fmt.channels : channels, frames, looping ? " loop" : "",
           (vp->flags & 0x8000) ? " 3d" : "", vp->category, vp->vol, vp->xpitch,
           nodata ? "  NO DATA (silent, timed)" : "");
    if (sReportPlays == 0)
    {
        printf("iSnd:   (the last play TSSM_SND reports)\n");
    }
    fflush(stdout);
}

static bool iPlayCutscene(S32 i, xSndVoiceInfo* vp);
static void iDumpFeed(pcslot* s, const S16* frames, U32 count);
static bool iPlayStream(S32 i, xSndVoiceInfo* vp, S32 host, const pcentry* e, bool looping);

bool iSndPlay(xSndVoiceInfo* vp)
{
    S32 i = iSlotOf(vp);
    if (i < 0)
    {
        return FALSE;
    }

    iReleaseSlot(i);
    vp->ps.channel = -1;

    if (vp->flags & 0x1)
    {
        return iPlayCutscene(i, vp);
    }

    iSndFileInfo* fi = vp->pSndFileInfo;
    pcentry* e = iEntryOf(fi);
    if (e == NULL)
    {
        return FALSE;
    }

    // The console's loop rule: a sound loops when its table says so; a stream
    // also when the caller asks (0x800, zMusic's tracks).
    bool streamed = (fi->uFlags & 0x2) != 0;
    bool looping = streamed ? ((fi->uFlags & 0x1) || (vp->flags & 0x800)) : (fi->uFlags & 0x1) != 0;

    S32 host = iSndHostAcquire((255 - vp->priority) >> 3);
    if (host < 0)
    {
        return FALSE;
    }

    if (streamed && iPlayStream(i, vp, host, e, looping))
    {
        return TRUE;
    }

    pctable* t = iTableOf(fi);
    iSndDataSource src;
    src.path = t->path;
    src.offset = e->offset;
    src.size = e->size;
    src.format_tag = e->fmt.format_tag;
    src.channels = e->fmt.channels != 0 ? e->fmt.channels : 1;
    src.block_align = e->fmt.block_align;
    src.bits = e->fmt.bits_per_sample;
    src.sample_rate = e->fmt.samples_per_sec;
    src.gc = e->gc;

    U32 bytes = 0;
    const void* data = iSndDataAcquire(vp->assetID, &src, &bytes);

    pcslot* s = &sSlots[i];
    s->host = host;
    s->data = data;
    s->looping = looping;
    s->channels = src.channels;

    iSndHostSample sample;
    memset(&sample, 0, sizeof(sample));
    sample.data = data;
    sample.bytes = bytes;
    sample.channels = src.channels;
    sample.bits = 16;
    sample.sample_rate = e->fmt.samples_per_sec;
    sample.looping = looping;
    sample.num_samples = data != NULL ? bytes / (2 * src.channels) : iEntryFrames(e);

    vp->ps.channel = i;

    // Pitch, level and send before the start: the backend takes the voice's
    // current gain as its starting gain.
    iApplyPitch(i, vp->xpitch);
    iApplyMix(i, vp->vol);
    iApplySend(i);
    iSndHostStart(host, &sample);

    iReport(vp, streamed ? "stream" : "sound", e, sample.num_samples, looping, data == NULL);
    return TRUE;
}

// A cutscene track: the console opens an FMOD stream fed by iSndExternalCB from
// the cutscene's chunks. Here the voice is fed from iSndUpdate (iPumpFeed) and
// held until the cutscene's first frame (iSndStartStereo).
static bool iPlayCutscene(S32 i, xSndVoiceInfo* vp)
{
    // An Xbox table's 16-bit PCM entry, or a GameCube table's DSP-ADPCM header
    // (gc/iSnd.cpp's iSndFindCutsceneHeader).
    pcentry* e = iFindEntry(vp->assetID);
    const iSndGCCutscene* gct = NULL;
    if (e == NULL || e->fmt.format_tag != ISNDXBOX_FORMAT_PCM || e->fmt.bits_per_sample != 16)
    {
        e = NULL;
        gct = iFindGCCutscene(vp->assetID);
    }

    if (e == NULL && gct == NULL)
    {
        // The console fails the play too when the track has no header.
        if (!sSaidNoTrack)
        {
            sSaidNoTrack = true;
            printf("iSnd: cutscene track %08x is not in a loaded sound table as 16-bit PCM or "
                   "GameCube DSP-ADPCM; the cutscene is silent\n",
                   vp->assetID);
            fflush(stdout);
        }
        return FALSE;
    }

    S32 host = iSndHostAcquire(0);
    if (host < 0)
    {
        return FALSE;
    }

    pcslot* s = &sSlots[i];
    U32 channels = gct != NULL ? 1 : (e->fmt.channels != 0 ? e->fmt.channels : 1);
    U32 rate = gct != NULL ? gct->rate : e->fmt.samples_per_sec;

    s->host = host;
    s->feed = true;
    s->held = true;
    s->feedAsset = vp->assetID;
    s->feedFrameBytes = 2 * channels;
    s->channels = channels;
    s->dumpRate = rate;

    if (gct != NULL)
    {
        // A GameCube track is one channel of DSP-ADPCM (FSOUND_MONO), decoded
        // across the chunks with its history carried.
        s->dsp = true;
        s->dspCh[0] = gct->ch;
        iSndDspReset(&s->dspState, s->dspCh, 1, false);
        s->dspLeft = gct->samples;
        s->dspTotal = gct->samples;
    }

    vp->ps.channel = i;

    // Pitch, level and send first: the backend starts a voice at its current
    // gain rather than ramping up to it.
    iApplyPitch(i, vp->xpitch);
    iApplyMix(i, vp->vol);
    iApplySend(i);
    iSndHostStartFeed(host, channels, rate, ISND_FEED_CAPACITY, true);

    iReport(vp, "cutscene", e, e != NULL ? iEntryFrames(e) : gct->samples, false, false, rate,
            channels);
    return TRUE;
}

static void iDumpFeed(pcslot* s, const S16* frames, U32 count)
{
    if (iSndDataDumpDir() == NULL || count == 0)
    {
        return;
    }

    U32 ch = s->feedFrameBytes / 2;
    if (s->dumpFrames + count > s->dumpCap)
    {
        U32 cap = s->dumpCap != 0 ? s->dumpCap : 1U << 18;
        while (cap < s->dumpFrames + count)
        {
            cap <<= 1;
        }

        S16* grown = (S16*)realloc(s->dump, (size_t)cap * ch * sizeof(S16));
        if (grown == NULL)
        {
            return;
        }
        s->dump = grown;
        s->dumpCap = cap;
    }

    memcpy(s->dump + (size_t)s->dumpFrames * ch, frames, (size_t)count * ch * sizeof(S16));
    s->dumpFrames += count;
}

// Keeps a stream ISND_STREAM_AHEAD_SEC ahead of the mixer: reads whole blocks
// from the package, decodes them and feeds them, wrapping a looping stream to
// its start (Xbox ADPCM blocks stand alone, so the seam is exact).
static void iPumpStream(S32 i)
{
    pcslot* s = &sSlots[i];
    if (!s->stream || s->host < 0 || s->streamEnded)
    {
        return;
    }

    U32 ch = s->feedFrameBytes / 2;
    U32 ahead = s->rate * ISND_STREAM_AHEAD_SEC;
    U32 must = s->rate * ISND_STREAM_MUST_SEC;
    U32 step = s->rate / ISND_STREAM_STEP_DIV;
    U32 fed = 0;

    for (S32 guard = 0; guard < 256; guard++)
    {
        U32 queued = iSndHostFeedQueued(s->host);
        if (queued >= ahead || (queued >= must && fed >= step))
        {
            return;
        }

        if (s->srcPos >= s->srcSize || (s->dsp && s->dspLeft == 0))
        {
            if (s->looping && s->srcSize >= s->blockAlign)
            {
                s->srcPos = 0;
                s->dumpWrapped = true;
                if (s->dsp)
                {
                    // The DSP goes back to the loop's own history.
                    iSndDspReset(&s->dspState, s->dspCh, ch, true);
                    s->dspLeft = s->dspTotal;
                }
            }
            else
            {
                iSndHostFeedEnd(s->host);
                s->streamEnded = true;
                return;
            }
        }

        // DSP-ADPCM blocks are small (8 bytes a channel), and 16-bit PCM's are
        // one sample frame, so a read of either takes as many as the buffers
        // hold.
        const bool small = s->dsp || s->formatTag != ISNDXBOX_FORMAT_ADPCM;
        U32 want = small ? (U32)sizeof(sStreamRead) / s->blockAlign * s->blockAlign :
                           ISND_STREAM_BLOCKS * s->blockAlign;
        if (want > s->srcSize - s->srcPos)
        {
            want = s->srcSize - s->srcPos;
        }
        want -= want % s->blockAlign;

        if (want == 0)
        {
            // A trailing partial block: not played (none on the disc).
            s->srcPos = s->srcSize;
            continue;
        }

        iFileSeek(&s->file, (S32)(s->srcOffset + s->srcPos), IFILE_SEEK_SET);
        iFileRead(&s->file, sStreamRead, want);
        s->srcPos += want;

        U32 frames = 0;
        if (s->dsp)
        {
            frames = iSndDspDecode(sStreamRead, want, ch, s->dspCh, &s->dspState, sStreamPcm,
                                   want / s->blockAlign);
            // The last block's padding is not played.
            if (frames > s->dspLeft)
            {
                frames = s->dspLeft;
            }
            s->dspLeft -= frames;
        }
        else if (s->formatTag == ISNDXBOX_FORMAT_ADPCM)
        {
            frames = iSndXboxDecodeAdpcmBlocks(sStreamRead, want, ch, s->blockAlign, sStreamPcm,
                                               ISND_STREAM_BLOCKS);
        }
        else
        {
            frames = want / s->feedFrameBytes;
            memcpy(sStreamPcm, sStreamRead, (size_t)frames * s->feedFrameBytes);
        }

        fed += frames;
        iSndHostFeed(s->host, sStreamPcm, frames);
        if (!s->dumpWrapped)
        {
            iDumpFeed(s, sStreamPcm, frames);
        }
    }
}

// A stream's voice: fed (iSndHost.h), from its own handle on the package.
// False if it cannot be read, and the caller plays it as a silent timed sound.
static bool iPlayStream(S32 i, xSndVoiceInfo* vp, S32 host, const pcentry* e, bool looping)
{
    pctable* t = iTableOf(vp->pSndFileInfo);
    U32 ch = e->fmt.channels != 0 ? e->fmt.channels : 1;
    bool dsp = e->gc != NULL;
    bool adpcm = !dsp && e->fmt.format_tag == ISNDXBOX_FORMAT_ADPCM;
    bool pcm16 = !dsp && e->fmt.format_tag == ISNDXBOX_FORMAT_PCM && e->fmt.bits_per_sample == 16;
    U32 align = dsp ? ISNDDSP_FRAME_BYTES * ch : (adpcm ? e->fmt.block_align : 2 * ch);

    if (t == NULL || e->size == 0 || ch > 8 || (!adpcm && !pcm16 && !dsp) || align == 0 ||
        align > ISND_MAX_BLOCK_ALIGN || (adpcm && (align % ch != 0 || align / ch < 5)) ||
        (dsp && ch > ISNDDSP_MAX_CHANNELS))
    {
        return false;
    }

    pcslot* s = &sSlots[i];
    memset(&s->file, 0, sizeof(s->file));
    if (iFileOpen(t->path, IFILE_OPEN_READ, &s->file) != 0)
    {
        return false;
    }

    s->host = host;
    s->fileOpen = true;
    s->stream = true;
    s->looping = looping;
    s->channels = ch;
    s->feedAsset = vp->assetID;
    s->feedFrameBytes = 2 * ch;
    s->srcOffset = e->offset;
    s->srcSize = e->size;
    s->blockAlign = align;
    s->formatTag = e->fmt.format_tag;
    s->rate = e->fmt.samples_per_sec != 0 ? e->fmt.samples_per_sec : 44100;
    s->dumpRate = s->rate;

    if (dsp)
    {
        s->dsp = true;
        memcpy(s->dspCh, e->gc->ch, sizeof(s->dspCh));
        iSndDspReset(&s->dspState, s->dspCh, ch, false);
        s->dspLeft = e->gc->samples;
        s->dspTotal = e->gc->samples;
    }

    vp->ps.channel = i;

    iApplyPitch(i, vp->xpitch);
    iApplyMix(i, vp->vol);
    iApplySend(i);
    iSndHostStartFeed(host, ch, s->rate, ISND_STREAM_CAPACITY, false);
    iPumpStream(i);

    iReport(vp, "stream", e, iEntryFrames(e), looping, false);
    return true;
}

// A block of a GameCube cutscene track: DSP-ADPCM, decoded into the voice's
// ring with the history carried on from the block before. A frame split
// across two blocks is carried over (none is on the disc: every SOUND record
// is whole frames). The padding of the last frame is not played.
static void iFeedDsp(pcslot* s, const U8* p, U32 size)
{
    const U32 frame = ISNDDSP_FRAME_BYTES; // one channel
    const U32 most = (U32)(sizeof(sStreamPcm) / sizeof(sStreamPcm[0])) / ISNDDSP_FRAME_SAMPLES;

    if (s->carryBytes != 0 && size != 0)
    {
        U32 take = frame - s->carryBytes;
        if (take > size)
        {
            take = size;
        }
        memcpy(s->carry + s->carryBytes, p, take);
        s->carryBytes += take;
        p += take;
        size -= take;

        if (s->carryBytes == frame)
        {
            U32 n = iSndDspDecode(s->carry, frame, 1, s->dspCh, &s->dspState, sStreamPcm, 1);
            n = n < s->dspLeft ? n : s->dspLeft;
            s->dspLeft -= n;
            iSndHostFeed(s->host, sStreamPcm, n);
            iDumpFeed(s, sStreamPcm, n);
            s->carryBytes = 0;
        }
    }

    while (size >= frame && s->dspLeft != 0)
    {
        U32 blocks = size / frame;
        if (blocks > most)
        {
            blocks = most;
        }
        U32 n = iSndDspDecode(p, blocks * frame, 1, s->dspCh, &s->dspState, sStreamPcm, blocks);
        n = n < s->dspLeft ? n : s->dspLeft;
        s->dspLeft -= n;
        iSndHostFeed(s->host, sStreamPcm, n);
        iDumpFeed(s, sStreamPcm, n);
        p += blocks * frame;
        size -= blocks * frame;
    }

    if (size != 0 && size < frame && s->dspLeft != 0)
    {
        memcpy(s->carry, p, size);
        s->carryBytes = size;
    }
}

// Copies whatever the cutscene has read of this track into the voice's ring.
static void iPumpFeed(S32 i)
{
    pcslot* s = &sSlots[i];
    if (!s->feed || s->host < 0)
    {
        return;
    }

    for (S32 guard = 0; guard < 8; guard++)
    {
        if (iSndHostFeedQueued(s->host) + ISND_FEED_MARGIN > ISND_FEED_CAPACITY)
        {
            return;
        }

        U32 size = 0;
        const U8* p = (const U8*)iCSSoundPull(s->feedAsset, &size);
        if (p == NULL)
        {
            return;
        }

        if (s->dsp)
        {
            iFeedDsp(s, p, size);
            continue;
        }

        // A frame split across two blocks is carried over.
        if (s->carryBytes != 0 && size != 0)
        {
            U32 need = s->feedFrameBytes - s->carryBytes;
            U32 take = need < size ? need : size;
            memcpy(s->carry + s->carryBytes, p, take);
            s->carryBytes += take;
            p += take;
            size -= take;

            if (s->carryBytes == s->feedFrameBytes)
            {
                iSndHostFeed(s->host, (const S16*)s->carry, 1);
                iDumpFeed(s, (const S16*)s->carry, 1);
                s->carryBytes = 0;
            }
        }

        U32 frames = size / s->feedFrameBytes;
        if (frames != 0)
        {
            // The ring has room by the check above. The block is only ever
            // copied (into the ring, and the dump), never read in place, so a
            // carried byte that leaves it unaligned does no harm.
            iSndHostFeed(s->host, (const S16*)p, frames);
            iDumpFeed(s, (const S16*)p, frames);
        }

        U32 rest = size - frames * s->feedFrameBytes;
        if (rest != 0)
        {
            memcpy(s->carry, p + frames * s->feedFrameBytes, rest);
            s->carryBytes = rest;
        }
    }
}

void iSndStartStereo(iSndHandle id1, iSndHandle id2)
{
    // The console leaves this empty: its cutscene track is already running.
    // Here the cutscene's voices were held for it, and start together.
    S32 hosts[2];
    S32 n = 0;

    for (S32 i = 0; i < ISND_REAL_VOICES; i++)
    {
        xSndVoiceInfo* vp = &gSnd.voice[i];
        pcslot* s = &sSlots[i];

        if (!(vp->flags & 0x4000) || !s->feed || !s->held || s->host < 0)
        {
            continue;
        }

        if ((vp->sndUniqueID == id1 || vp->sndUniqueID == id2) && vp->sndUniqueID != (iSndHandle)-1)
        {
            s->held = false;
            if (!s->paused && n < 2)
            {
                hosts[n++] = s->host;
            }
        }
    }

    iSndHostPauseGroup(hosts, n, false);
}

void iSndStop(xSndVoiceInfo* vp)
{
    S32 i = iSlotOf(vp);
    if (i >= 0)
    {
        iReleaseSlot(i);
    }

    vp->ps.channel = -1;
    vp->flags = 0;
}

void iSndPause(xSndVoiceInfo* vp, U32 pause)
{
    S32 i = iSlotOf(vp);
    if (i < 0 || sSlots[i].host < 0)
    {
        return;
    }

    sSlots[i].paused = pause != 0;

    // A held cutscene track stays held whatever the pause says.
    iSndHostPause(sSlots[i].host, sSlots[i].paused || sSlots[i].held);
}

void iSndSetVol(xSndVoiceInfo* vp, F32 vol)
{
    S32 i = iSlotOf(vp);
    if (i >= 0)
    {
        iApplyMix(i, vol);
    }
}

void iSndSetPitch(xSndVoiceInfo* vp, F32 pitch)
{
    S32 i = iSlotOf(vp);
    if (i >= 0)
    {
        iApplyPitch(i, pitch);
    }
}

U8 iSndIsReady(iSndHandle id)
{
    return TRUE;
}

static void iReportMix();

void iSndUpdate()
{
    iSndHostUpdate();

    for (S32 i = 0; i < ISND_REAL_VOICES; i++)
    {
        iPumpFeed(i);
        iPumpStream(i);
    }

    U8 bDialogActive = FALSE;
    for (S32 i = 0; i < ISND_REAL_VOICES; i++)
    {
        xSndVoiceInfo* vp = &gSnd.voice[i];
        pcslot* s = &sSlots[i];

        if (!(vp->flags & 0x4000))
        {
            // Nothing the game knows of may keep a host voice: the console's
            // FMOD channel would be reused, here it would leak.
            if (s->host >= 0)
            {
                iReleaseSlot(i);
            }
            continue;
        }

        if (vp->flags & 0x800000)
        {
            continue;
        }

        bool playing = s->host >= 0 && iSndHostIsPlaying(s->host);
        bool paused = s->host >= 0 && (s->paused || s->held);

        if (!playing && !paused)
        {
            if ((vp->flags & 0x1) || (vp->pSndFileInfo != NULL && (vp->pSndFileInfo->uFlags & 0x2)))
            {
                // iSndCloseStream.
                iReleaseSlot(i);
                vp->ps.channel = -1;
                vp->flags = 0;
            }
            else if (s->looping && vp->pSndFileInfo != NULL)
            {
                // A loop that lost its voice lives on as a virtual one.
                xSndMgr_SetInaudible(vp);
                iReleaseSlot(i);
                vp->ps.channel = -1;
                vp->flags = 0;
            }
            else
            {
                iReleaseSlot(i);
                vp->ps.channel = -1;
                vp->flags = 0;
            }
        }
        else
        {
            if ((vp->flags & 0x8000) && (vp->flags & 0x1000))
            {
                iApplyPitch(i, vp->xpitch);
            }

            iSndSetVol(vp, vp->vol);
            bDialogActive = bDialogActive | (vp->category == 2);
        }
    }

    if (bDialogActive)
    {
        gSnd.m_uSndMgrFlags |= 0x2;
    }
    else
    {
        gSnd.m_uSndMgrFlags &= ~0x2;
    }

    iReportMix();
}

void iSndKill()
{
    xSndMgrStopSounds(-3, false);
    for (S32 i = 0; i < ISND_REAL_VOICES; i++)
    {
        iReleaseSlot(i);
    }
}

void iSndSetExternalCallback(void* func_ptr)
{
}

void iSndSuspendCD(U32)
{
}

// iSndSuspend and iSndResume are the console's (iTRC calls them around a disc
// error, which the PC does not have, and the reset path).
static F32 sOldVolume[6];
static bool sOldPauseState[6];

void iSndSuspend()
{
    for (S16 i = 0; i < 6; i++)
    {
        sOldPauseState[i] = xSndMgrIsPaused(i);
        sOldVolume[i] = xSndMgrGetUserVolume(i);
    }
    xSndMgrPauseSounds(-3, true, true);
    xSndMgrUpdate();
}

void iSndResume()
{
    for (S16 i = 0; i < 6; i++)
    {
        if (!sOldPauseState[i])
        {
            xSndMgrPauseSounds(i, false, true);
        }
        xSndMgrSetUserVolume(i, sOldVolume[i]);
    }
}

// ---------------------------------------------------------------------------
// TSSM_SNDMIX

static void iReportMix()
{
    if (!sReportMix)
    {
        return;
    }

    U64 now = iHostMonotonicNs();
    if (now - sLastMixReportNs < 1000000000ULL)
    {
        return;
    }
    sLastMixReportNs = now;

    U32 cached = 0;
    U32 cachedBytes = 0;
    U32 pinned = 0;
    iSndDataStats(&cached, &cachedBytes, &pinned);

    printf("iSnd: --- %u tables, cache %u sounds %u KB (%u playing), reverb %s\n", sTableCount,
           cached, cachedBytes / 1024, pinned, sReverbOn ? "on" : "off");

    for (S32 i = 0; i < ISND_REAL_VOICES; i++)
    {
        pcslot* s = &sSlots[i];
        if (s->host < 0)
        {
            continue;
        }

        // A stream's decoded lead over the mixer (iPumpStream).
        char ahead[32] = "";
        if (s->stream && s->rate != 0)
        {
            snprintf(ahead, sizeof(ahead), " %.2f s ahead",
                     (double)iSndHostFeedQueued(s->host) / (double)s->rate);
        }

        xSndVoiceInfo* vp = &gSnd.voice[i];
        printf("iSnd:   [%2d] %08x cat %d vol %.2f flags %08x %s%s%s%s%s\n", i, vp->assetID,
               vp->category, vp->vol, vp->flags, iSndHostIsPlaying(s->host) ? "playing" : "ended",
               s->feed ? " cutscene" : (s->stream ? " stream" : ""), ahead, s->held ? " held" : "",
               s->paused ? " paused" : "");
    }

    fflush(stdout);
}
