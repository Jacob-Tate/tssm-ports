// PC-only: GameCube assets into the Xbox layout. See iGCSwap.h.
//
// After the BfBB PC port's iGCSwap.cpp (CC0): the helpers, and the shape of the
// hand-written rules for ANIM, DSCO, MINF, PLYR, CTOC, JAW and CRDT. The rules
// are TSSM's: its cutscene header, its JSPINFO (version 5), its destructibles,
// and the whole-asset cutscene walk.
//
// Every rule checks a count or a length against the bytes it has before it
// swaps what that count describes, so a malformed asset ends in MALFORMED and
// never in a read or a write outside the asset.
#include "iGCSwap.h"

#include <stdlib.h>
#include <string.h>

static inline void gcsSwap16(U8* p)
{
    U8 t = p[0];
    p[0] = p[1];
    p[1] = t;
}

static inline void gcsSwap32(U8* p)
{
    U8 t = p[0];
    p[0] = p[3];
    p[3] = t;
    t = p[1];
    p[1] = p[2];
    p[2] = t;
}

static inline void gcsSwap64(U8* p)
{
    for (S32 i = 0; i < 4; i++)
    {
        U8 t = p[i];
        p[i] = p[7 - i];
        p[7 - i] = t;
    }
}

// Little-endian reads, for fields a step has already swapped.
static inline U32 gcsRead8(const U8* p)
{
    return p[0];
}

static inline U32 gcsRead16(const U8* p)
{
    return (U32)p[0] | ((U32)p[1] << 8);
}

static inline U32 gcsRead32(const U8* p)
{
    return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
}

// A big-endian read, for a field not swapped yet.
static inline U32 gcsReadBE32(const U8* p)
{
    return ((U32)p[0] << 24) | ((U32)p[1] << 16) | ((U32)p[2] << 8) | (U32)p[3];
}

// Fields the GameCube packer swapped in some assets and not in others; see
// EITHER in tools/gcswap/spec.py. Each keeps whichever byte order reads as a
// plausible value, and swaps when both do. Plausible for a float is zero or a
// magnitude between 2^-20 and 2^20.
static bool gcsPlausibleFloat(U32 bits)
{
    U32 exponent = (bits >> 23) & 0xFF;
    return (bits & 0x7FFFFFFF) == 0 || (exponent >= 127 - 20 && exponent <= 127 + 20);
}

static inline void gcsSwap32EitherFloat(U8* p)
{
    if (!gcsPlausibleFloat(gcsReadBE32(p)) && gcsPlausibleFloat(gcsRead32(p)))
    {
        return;
    }
    gcsSwap32(p);
}

static inline void gcsSwap32EitherInt(U8* p)
{
    if (gcsReadBE32(p) > 0xFFFF && gcsRead32(p) <= 0xFFFF)
    {
        return;
    }
    gcsSwap32(p);
}

// Every aligned u32 in [from, to).
static void gcsSwapWords(U8* p, U32 from, U32 to)
{
    for (U32 c = from; c + 4 <= to && c + 4 > c; c += 4)
    {
        gcsSwap32(p + c);
    }
}

static void gcs_xLinkAsset(U8* p);

// An xBaseAsset's links: `linkCount` xLinkAssets at the very end of the asset.
// linkCount is a byte, so it reads the same either way round. Sets *end to
// where the links begin -- the end of the type's own data.
static bool gcsSwapLinks(U8* p, U32 size, U32* end)
{
    const U32 kLinkSize = 0x20;

    if (size < 8)
    {
        return false;
    }

    U32 links = p[5] * kLinkSize;
    if (links > size - 8)
    {
        return false;
    }

    *end = size - links;
    for (U32 at = *end; at < size; at += kLinkSize)
    {
        gcs_xLinkAsset(p + at);
    }
    return true;
}

// The hand-written rules the spec names, after the generated table, whose
// struct swaps they use. Each gets the asset, the end of its body (the links,
// if any, are already swapped) and where the generated steps stopped.
static S32 iGCSwapAnim(U8* p, U32 end, U32 c);
static S32 iGCSwapDisco(U8* p, U32 end, U32 c);
static S32 iGCSwapModelInfo(U8* p, U32 end, U32 c);
static S32 iGCSwapPlayer(U8* p, U32 end, U32 c);
static S32 iGCSwapCutsceneToc(U8* p, U32 end, U32 c);
static S32 iGCSwapCutscene(U8* p, U32 end, U32 c);
static S32 iGCSwapJaw(U8* p, U32 end, U32 c);
static S32 iGCSwapJspInfo(U8* p, U32 end, U32 c);
static S32 iGCSwapCredits(U8* p, U32 end, U32 c);
static S32 iGCSwapDestructible(U8* p, U32 end, U32 c);
static S32 iGCSwapAnimTableRest(U8* p, U32 end, U32 c);

#include "iGCSwapTable.inc"

// ATBL after its states, as ATBL_Read (zAssetTypes.cpp) reads it: the files'
// index arrays, all words, and each state's EffectCount effects from its
// EffectOffset -- an xAnimAssetEffect, then UserDataSize bytes the effect's
// callback reads. Sound effects (types 1 and 2, soundEffectCB) carry a
// zAnimFxSoundGroup, a u32 id and a count byte; type 0 carries nothing it
// reads. effectFuncList has three entries, so no other type can play.
static S32 iGCSwapAnimTableRest(U8* p, U32 end, U32 c)
{
    const U32 kState = 0x1C;
    const U32 kEffect = 0x14;

    U32 states = gcsRead32(p + 0xC);
    if (c < 0x14 || states > (c - 0x14) / kState)
    {
        return 0;
    }
    U32 first = c - states * kState;

    // Which words belong to an effect: those are swapped by the effect's
    // layout, every other word as a word. Effects may be shared by states.
    U32 words = (end - c) / 4;
    U8* mine = (U8*)calloc(words != 0 ? words : 1, 1);
    if (mine == NULL)
    {
        return 0;
    }
    bool ok = true;
    for (U32 s = 0; s < states && ok; s++)
    {
        const U8* st = p + first + s * kState;
        U32 count = gcsRead32(st + 0x8);
        U32 at = gcsRead32(st + 0xC);
        for (U32 k = 0; k < count; k++)
        {
            if (at < c || at > end || end - at < kEffect || ((at - c) & 3) != 0)
            {
                ok = false;
                break;
            }
            U32 w = (at - c) / 4;
            bool done = mine[w] != 0;
            U32 userSize = done ? gcsRead32(p + at + 0xC) : gcsReadBE32(p + at + 0xC);
            U32 type = p[at + 0x11];
            if (userSize > end - at - kEffect || (userSize & 3) != 0 || type > 2)
            {
                ok = false;
                break;
            }
            if (!done)
            {
                gcs_xAnimAssetEffect(p + at);
                if (type != 0 && userSize >= 4)
                {
                    gcsSwap32(p + at + kEffect); // zAnimFxSoundGroup.ID; then bytes
                }
                for (U32 m = 0; m < (kEffect + userSize) / 4; m++)
                {
                    mine[w + m] = 1;
                }
            }
            at += kEffect + userSize;
        }
    }
    if (ok)
    {
        for (U32 w = 0; w < words; w++)
        {
            if (!mine[w])
            {
                gcsSwap32(p + c + w * 4);
            }
        }
    }
    free(mine);
    return ok ? 1 : 0;
}

// ANIM, an SKB1 skeletal animation, laid out as iAnimEvalSKB reads it: the
// iAnimSKBHeader -- Magic, ExtractedData, BoneCount and TimeCount (16-bit),
// KeyCount, Scale[3] -- then KeyCount iAnimSKBKeys of eight 16-bit fields
// each, then TimeCount float times, then 16-bit key offsets to the end. An
// empty asset (retail has 207) is left empty.
static bool gcsSwapSKB(U8* p, U32 size)
{
    const U32 kHeader = 0x1C;
    const U32 kKey = 0x10;

    if (size < kHeader)
    {
        return false;
    }
    gcsSwap32(p + 0x0);
    gcsSwap32(p + 0x4);
    gcsSwap16(p + 0x8);
    gcsSwap16(p + 0xA);
    gcsSwap32(p + 0xC);
    gcsSwap32(p + 0x10);
    gcsSwap32(p + 0x14);
    gcsSwap32(p + 0x18);

    U32 keys = gcsRead32(p + 0xC);
    U32 times = gcsRead16(p + 0xA);
    if (keys > (size - kHeader) / kKey || times > (size - kHeader - keys * kKey) / 4)
    {
        return false;
    }

    U32 c = kHeader;
    for (U32 e = c + keys * kKey; c < e; c += 2)
    {
        gcsSwap16(p + c);
    }
    for (U32 e = c + times * 4; c < e; c += 4)
    {
        gcsSwap32(p + c);
    }
    for (; c + 2 <= size; c += 2)
    {
        gcsSwap16(p + c);
    }
    return true;
}

static S32 iGCSwapAnim(U8* p, U32 end, U32 c)
{
    (void)c;
    if (end == 0)
    {
        return 1;
    }
    return gcsSwapSKB(p, end);
}

// DSCO, z_disco_floor. Unlike every other base asset its links come straight
// after the header -- zDiscoFloor.cpp takes `&asset + 1` -- and the data after
// them is addressed by offsets from byte 8 (`(xBaseAsset*)&asset + 1`): the
// three prefix strings, a table of `states_size` u32 offsets at
// `states_offset`, and the state masks, which are bytes.
static S32 iGCSwapDisco(U8* p, U32 end, U32 c)
{
    const U32 kHeader = 0x2C;
    const U32 kDataBase = 0x8;
    (void)c;

    if (end < kHeader)
    {
        return 0;
    }
    gcs_z_disco_floor_asset(p);

    U32 links = p[5] * 0x20;
    if (links > end - kHeader)
    {
        return 0;
    }
    for (U32 at = kHeader; at < kHeader + links; at += 0x20)
    {
        gcs_xLinkAsset(p + at);
    }

    U32 offset = gcsRead32(p + 0x24);
    U32 count = gcsRead32(p + 0x28);
    if (offset > end - kDataBase)
    {
        return 0;
    }
    U32 table = kDataBase + offset;
    if (count > (end - table) / 4)
    {
        return 0;
    }
    for (U32 i = 0; i < count; i++)
    {
        gcsSwap32(p + table + i * 4);
    }
    return 1;
}

// MINF, xModelAssetInfo: the header, NumModelInst xModelAssetInsts, then the
// parameters zParamGetString walks -- each a u32 hash, a WordLength byte, and
// text filling WordLength * 4 + 4 more bytes.
static S32 iGCSwapModelInfo(U8* p, U32 end, U32 c)
{
    const U32 kInfo = 0x14;
    const U32 kInst = 0x38;

    if (end < kInfo)
    {
        return 0;
    }
    gcs_xModelAssetInfo(p);

    U32 n = gcsRead32(p + 0x4);
    if (n > (end - kInfo) / kInst)
    {
        return 0;
    }
    c = kInfo;
    for (U32 i = 0; i < n; i++, c += kInst)
    {
        gcs_xModelAssetInst(p + c);
    }

    while (c + 8 <= end)
    {
        U32 step = p[c + 4] * 4 + 8;
        gcsSwap32(p + c);
        if (step > end - c)
        {
            break; // the game stops at the size it is given too
        }
        c += step;
    }
    return 1;
}

// PLYR: an xEntAsset, its links, then the light kit id. The links are not at
// the end as in other base assets: zEntPlayer_Init reads the light kit id
// after them.
static S32 iGCSwapPlayer(U8* p, U32 end, U32 c)
{
    const U32 kEnt = 0x50;
    (void)c;

    if (end < kEnt)
    {
        return 0;
    }
    U32 links = p[5] * 0x20;
    if (links > end - kEnt)
    {
        return 0;
    }
    gcs_xEntAsset(p);
    for (U32 at = kEnt; at < kEnt + links; at += 0x20)
    {
        gcs_xLinkAsset(p + at);
    }
    gcsSwapWords(p, kEnt + links, end);
    return 1;
}

// A cutscene header, as xCutscene_Create lays it out from an xCutsceneInfo:
// the info, NumData xCutsceneDatas, NumTime + 1 time chunk offsets, VisSize
// visibility words and BreakCount xCutsceneBreaks, all inside HeaderSize. The
// CTOC holds one per cutscene; each CSN asset starts with its own.
static bool gcsSwapCutsceneHeader(U8* p, U32 size, U32* headerSize)
{
    const U32 kInfo = 0x830; // TSSM's xCutsceneInfo: 32 audio tracks
    const U32 kData = 0x10;

    if (size < kInfo)
    {
        return false;
    }
    gcs_xCutsceneInfo(p);

    U32 header = gcsRead32(p + 0x1C);
    U32 numData = gcsRead32(p + 0x8);
    U32 numTime = gcsRead32(p + 0xC);
    U32 visSize = gcsRead32(p + 0x24);
    U32 breaks = gcsRead32(p + 0x28);
    if (header < kInfo || header > size)
    {
        return false;
    }

    U32 room = (header - kInfo) / 4;
    if (numData > room / 4 || numTime >= room - numData * 4 ||
        visSize > room - numData * 4 - (numTime + 1) ||
        breaks > (room - numData * 4 - (numTime + 1) - visSize) / 2)
    {
        return false;
    }

    U32 c = kInfo;
    for (U32 i = 0; i < numData; i++, c += kData)
    {
        gcs_xCutsceneData(p + c);
    }
    gcsSwapWords(p, c, header);
    *headerSize = header;
    return true;
}

// CTOC, the table xCutscene_Init takes: a count, then that many cutscene
// headers, each HeaderSize bytes.
static S32 iGCSwapCutsceneToc(U8* p, U32 end, U32 c)
{
    if (end < 4)
    {
        return 0;
    }
    gcsSwap32(p);

    U32 n = gcsRead32(p);
    c = 4;
    for (U32 i = 0; i < n; i++)
    {
        U32 header;
        if (!gcsSwapCutsceneHeader(p + c, end - c, &header))
        {
            return 0;
        }
        c += header;
    }
    return 1;
}

// A cutscene's JDELTAMODEL, as JDeltaEval reads it: the four characters JDTM
// and a count, then JDeltaTargets, each skipSize bytes long -- skipSize,
// numRuns, numVerts and scale, numRuns JDeltaRuns, and at the next 16 bytes
// (from the start of the model) numVerts positions: three S16s each when scale
// is nonzero, three floats otherwise.
static bool gcsSwapDeltaModel(U8* p, U32 size)
{
    const U32 kHeader = 0x8;
    const U32 kTarget = 0x10;

    if (size < kHeader)
    {
        return false;
    }
    gcsSwap32(p + 0x4);

    U32 c = kHeader;
    while (c + kTarget <= size)
    {
        gcsSwapWords(p, c, c + kTarget);

        U32 skip = gcsRead32(p + c);
        U32 runs = gcsRead32(p + c + 0x4);
        U32 verts = gcsRead32(p + c + 0x8);
        bool packed = gcsRead32(p + c + 0xC) != 0;

        U32 r = c + kTarget;
        if (runs > (size - r) / 8)
        {
            return false;
        }
        gcsSwapWords(p, r, r + runs * 8);

        U32 v = r + runs * 8;
        v = (v + 15) & ~15u;
        U32 vsize = packed ? 6 : 12;
        if (v > size || verts > (size - v) / vsize)
        {
            return false;
        }
        if (packed)
        {
            for (U32 k = 0; k < verts * 3; k++)
            {
                gcsSwap16(p + v + k * 2);
            }
        }
        else
        {
            gcsSwapWords(p, v, v + verts * 12);
        }

        if (skip == 0)
        {
            break;
        }
        if (skip < kTarget || skip > size - c)
        {
            return false;
        }
        c += skip;
    }
    return true;
}

// A morph target record of a time chunk, as xCutscene.cpp's morph code reads
// it: NumFrame and NumRun, the xCutsceneMphFrames and xCutsceneMphRuns, padded
// to four words, an xMorphTargetFile, then S16 vertex components.
static bool gcsSwapMorph(U8* p, U32 size)
{
    if (size < 8)
    {
        return false;
    }
    gcsSwap32(p);
    gcsSwap32(p + 4);
    U32 frames = gcsRead32(p);
    U32 runs = gcsRead32(p + 4);
    if (frames > (size - 8) / 8 || runs > (size - 8 - frames * 8) / 8)
    {
        return false;
    }
    gcsSwapWords(p, 8, 8 + frames * 8 + runs * 8);
    U32 file = ((2 + frames * 2 + runs * 2 + 3) & ~3u) * 4;
    if (file > size || size - file < 0x20)
    {
        return false;
    }
    gcs_xMorphTargetFile(p + file);
    for (U32 c = file + 0x20; c + 2 <= size; c += 2)
    {
        gcsSwap16(p + c);
    }
    return true;
}

// A cutscene's time chunk, laid out as xCutscene.cpp reads it: an
// xCutsceneTime, then NumData records of a 16-byte xCutsceneData header and a
// payload padded to 16. The payloads:
//
//   ANIMATION    an xCutsceneAnimHdr, then an SKB1 animation, as ANIM
//   CAMERA       a key count, then zFlyKeys: all 32-bit
//   MORPHTARGET  gcsSwapMorph
//   SOUND        DSP ADPCM, which the sound layer decodes; left alone
//   JDELTAMODEL  gcsSwapDeltaModel
//   JDELTAANIM   the four characters JDTA, then counts and floats
//   RW_MODEL     a RenderWare stream; left alone
//   0            nothing: some retail chunks count an empty record at the end
static bool gcsSwapCutsceneTime(U8* p, U32 size)
{
    const U32 kTime = 0x10;
    const U32 kRecord = 0x10;
    const U32 kAnimHdr = 0x10;

    if (size < kTime)
    {
        return false;
    }
    gcsSwapWords(p, 0, kTime);

    U32 n = gcsRead32(p + 0x8);
    U32 c = kTime;
    for (U32 i = 0; i < n; i++)
    {
        if (c > size || size - c < kRecord)
        {
            return false;
        }
        gcs_xCutsceneData(p + c);

        U32 type = gcsRead32(p + c);
        U32 len = gcsRead32(p + c + 0x8);
        U32 body = c + kRecord;
        if (len > size - body)
        {
            return false;
        }

        switch (type)
        {
        case 0: // an empty record; retail counts some
        case 1: // RW_MODEL
        case 5: // SOUND
            break;
        case 2: // ANIMATION
            if (len < kAnimHdr)
            {
                return false;
            }
            gcs_xCutsceneAnimHdr(p + body);
            if (len > kAnimHdr && !gcsSwapSKB(p + body + kAnimHdr, len - kAnimHdr))
            {
                return false;
            }
            break;
        case 3: // CAMERA
            gcsSwapWords(p, body, body + len);
            break;
        case 4: // MORPHTARGET
            if (!gcsSwapMorph(p + body, len))
            {
                return false;
            }
            break;
        case 6: // JDELTAMODEL
            if (!gcsSwapDeltaModel(p + body, len))
            {
                return false;
            }
            break;
        case 7: // JDELTAANIM
            if (len < 4)
            {
                return false;
            }
            gcsSwapWords(p, body + 4, body + len);
            break;
        default:
            return false;
        }

        U32 padded = (len + 15) & ~15u;
        if (padded < len || padded > size - body)
        {
            c = size; // the last record, unpadded
        }
        else
        {
            c = body + padded;
        }
    }
    return true;
}

// CSN, a whole cutscene, as iCutscene.cpp streams it: the header (as in the
// CTOC), then from the next 2048 bytes each xCutsceneData's chunk, ChunkSize
// bytes rounded up to 2048 -- RenderWare models, read as they are, and
// JDELTAMODELs -- then the time chunks back to back, chunk i being
// TimeChunkOffs[i + 1] - TimeChunkOffs[i] bytes.
static S32 iGCSwapCutscene(U8* p, U32 end, U32 c)
{
    const U32 kSector = 2048;
    const U32 kInfo = 0x830;
    (void)c;

    U32 header;
    if (!gcsSwapCutsceneHeader(p, end, &header))
    {
        return 0;
    }

    U32 numData = gcsRead32(p + 0x8);
    U32 numTime = gcsRead32(p + 0xC);
    U32 offs = kInfo + numData * 0x10;

    U32 pos = (header + kSector - 1) / kSector * kSector;
    if (pos < header)
    {
        return 0;
    }
    for (U32 i = 0; i < numData; i++)
    {
        const U8* d = p + kInfo + i * 0x10;
        U32 type = gcsRead32(d) & 0x0FFFFFFF;
        U32 len = gcsRead32(d + 0x8);
        if (len == 0)
        {
            continue;
        }
        if (pos > end || len > end - pos)
        {
            return 0;
        }
        if (type == 6 && !gcsSwapDeltaModel(p + pos, len))
        {
            return 0;
        }
        U32 next = pos + (len + kSector - 1) / kSector * kSector;
        if (next < pos)
        {
            return 0;
        }
        pos = next;
    }

    for (U32 i = 0; i < numTime; i++)
    {
        U32 from = gcsRead32(p + offs + i * 4);
        U32 to = gcsRead32(p + offs + i * 4 + 4);
        if (to < from || pos > end || to - from > end - pos)
        {
            return 0;
        }
        if (!gcsSwapCutsceneTime(p + pos, to - from))
        {
            return 0;
        }
        pos += to - from;
    }
    return 1;
}

// JAW, the lip-sync table xJaw_FindData walks: a count, then that many
// xJawDataTables of three u32s. The curves after them are left alone: each
// starts with a count the GameCube disc already ships little-endian, and
// xJaw_EvalData turns one around itself when it reads as over 0xFFFF.
static S32 iGCSwapJaw(U8* p, U32 end, U32 c)
{
    (void)c;
    if (end < 4)
    {
        return 0;
    }
    gcsSwap32(p);

    U32 n = gcsRead32(p);
    if (n > (end - 4) / 12)
    {
        return 0;
    }
    gcsSwapWords(p, 4, 4 + n * 12);
    return 1;
}

// JSP. Most are RenderWare clump streams, little-endian everywhere, and are
// left alone (iGCSwapJsp, below, sorts them out before this runs). The
// JSPINFO one is a run of blocks xJSP_MultiStreamRead walks,
// each behind a 12-byte little-endian mark -- type, length, library version --
// whose bodies the GameCube ships big-endian:
//
//   0xBEEF01  the collision tree: a tag, the branch count and the triangle
//             count, the xClumpCollBSPBranchNodes, then xClumpCollBSPTriangles
//   0xBEEF02  an xJSPHeader (version 5), jspNodeCount xJSPNodeInfos, an
//             xJSPNodeTree and its branches and leaves, then the strip vector
//             count and the vectors, all 32-bit
//   0xBEEF04  an xJSPExtraInfo
static S32 iGCSwapJspInfo(U8* p, U32 end, U32 c)
{
    const U32 kMark = 12;
    const U32 kTree = 12;
    const U32 kBranch = 0x10;
    const U32 kTriangle = 0x8;
    const U32 kHeader = 0x2C;

    if (end < kMark || gcsRead32(p) != 0xBEEF01)
    {
        return 0; // iGCSwapJsp passes only JSPINFO here
    }

    c = 0;
    while (c + kMark <= end)
    {
        U32 type = gcsRead32(p + c);
        U32 len = gcsRead32(p + c + 4);
        U32 body = c + kMark;
        if (len > end - body)
        {
            return 0;
        }

        switch (type)
        {
        case 0xBEEF01:
        {
            if (len < kTree)
            {
                return 0;
            }
            gcsSwapWords(p, body, body + kTree);
            U32 branches = gcsRead32(p + body + 4);
            U32 tris = gcsRead32(p + body + 8);
            if (branches > (len - kTree) / kBranch ||
                tris > (len - kTree - branches * kBranch) / kTriangle)
            {
                return 0;
            }
            U32 t = body + kTree;
            for (U32 i = 0; i < branches; i++, t += kBranch)
            {
                gcs_xClumpCollBSPBranchNode(p + t);
            }
            for (U32 i = 0; i < tris; i++, t += kTriangle)
            {
                gcs_xClumpCollBSPTriangle(p + t);
            }
            break;
        }
        case 0xBEEF02:
        {
            if (len < kHeader)
            {
                return 0;
            }
            gcs_xJSPHeader(p + body);
            if (gcsRead32(p + body + 4) != 5)
            {
                return 0; // TSSM's retail JSPINFO is version 5 on both discs
            }
            U32 nodes = gcsRead32(p + body + 8);
            U32 t = body + kHeader;
            U32 stop = body + len;
            if (nodes > (stop - t) / 8)
            {
                return 0;
            }
            for (U32 i = 0; i < nodes; i++, t += 8)
            {
                gcs_xJSPNodeInfo(p + t);
            }
            if (stop - t < 0x10)
            {
                return 0;
            }
            gcs_xJSPNodeTree(p + t);
            U32 treeBranches = gcsRead32(p + t);
            U32 treeLeaves = gcsRead32(p + t + 8);
            t += 0x10;
            if (treeBranches > (stop - t) / 0x10 ||
                treeLeaves > (stop - t - treeBranches * 0x10) / 0x20)
            {
                return 0;
            }
            for (U32 i = 0; i < treeBranches; i++, t += 0x10)
            {
                gcs_xJSPNodeTreeBranch(p + t);
            }
            for (U32 i = 0; i < treeLeaves; i++, t += 0x20)
            {
                gcs_xJSPNodeTreeLeaf(p + t);
            }
            // The strip vectors, then vertDataStride bytes of vertex data per
            // vector, which are bytes on both discs (vertDataFlags 2, stride 4).
            if (stop - t < 4)
            {
                return 0;
            }
            gcsSwap32(p + t);
            U32 vecs = gcsRead32(p + t);
            t += 4;
            if (vecs > (stop - t) / 12)
            {
                return 0;
            }
            gcsSwapWords(p, t, t + vecs * 12);
            break;
        }
        case 0xBEEF04:
            if (len < 0x1C)
            {
                return 0;
            }
            gcs_xJSPExtraInfo(p + body);
            break;
        default:
            return 0;
        }

        c = body + len;
    }
    return 1;
}

// CRDT, the credits, as xCM.cpp reads them: an xCMheader, then blocks to its
// total_size -- an xCMcredits, num_presets xCMpresets, and hunks to the
// block's credits_size. A preset is two U16s and then 32-bit fields; the
// packer wrote each textbox's colour as one u32, so it turns around with them.
// A hunk is six 32-bit fields and then its text.
//
// The body is encrypted on the disc when state bit 1 is set (xCMstart decrypts
// it on first use): each byte is XORed with the plain byte before it and with
// xCM.cpp's key. It is decrypted, swapped and encrypted again.
static const char kCreditsKey[] = "xCMChunkHandler";

static void gcsCreditsCrypt(U8* p, U32 from, U32 to, bool decrypt)
{
    U8 last = 0;
    U32 key = 0;
    for (U32 i = from; i < to; i++)
    {
        U8 plain = decrypt ? (U8)(p[i] ^ last ^ (U8)kCreditsKey[key]) : p[i];
        p[i] = decrypt ? plain : (U8)(p[i] ^ last ^ (U8)kCreditsKey[key]);
        last = plain;
        key = (key + 1) % 12;
    }
}

static S32 iGCSwapCredits(U8* p, U32 end, U32 c)
{
    const U32 kHeader = 0x18;
    const U32 kCredits = 0x38;
    const U32 kPreset = 0x4C;
    const U32 kHunk = 0x18;

    if (end < kHeader)
    {
        return 0;
    }
    gcsSwapWords(p, 0, kHeader);

    U32 state = gcsRead32(p + 0xC);
    U32 total = gcsRead32(p + 0x14);
    if (total > end || total < kHeader)
    {
        return 0;
    }
    bool encrypted = (state & 0x2) != 0;
    if (encrypted)
    {
        gcsCreditsCrypt(p, kHeader, total, true);
    }

    c = kHeader;
    bool ok = true;
    while (ok && c < total)
    {
        if (total - c < kCredits)
        {
            ok = false;
            break;
        }
        gcsSwapWords(p, c, c + kCredits);

        U32 block = c;
        U32 blockSize = gcsRead32(p + c);
        U32 presets = gcsRead32(p + c + 0x34);
        if (blockSize < kCredits || blockSize > total - block ||
            presets > (blockSize - kCredits) / kPreset)
        {
            ok = false;
            break;
        }

        c += kCredits;
        for (U32 i = 0; i < presets; i++, c += kPreset)
        {
            gcsSwap16(p + c);
            gcsSwap16(p + c + 2);
            gcsSwapWords(p, c + 4, c + kPreset);
        }

        while (c - block < blockSize)
        {
            if (block + blockSize - c < kHunk)
            {
                ok = false;
                break;
            }
            gcsSwapWords(p, c, c + kHunk);

            U32 hunkSize = gcsRead32(p + c);
            if (hunkSize < kHunk || hunkSize > block + blockSize - c)
            {
                ok = false;
                break;
            }
            c += hunkSize;
        }
    }

    if (encrypted)
    {
        gcsCreditsCrypt(p, kHeader, total, false);
    }
    return ok ? 1 : 0;
}

// DEST, a destructible, as zDestructible.cpp reads it: the first 0x28 bytes of
// an xDestructibleAsset (its last field, `states`, is where the first state
// starts), then nstates xDestructibleAssetStates, each followed by the
// animation ids its last field counts.
static S32 iGCSwapDestructible(U8* p, U32 end, U32 c)
{
    const U32 kHeader = 0x28;
    const U32 kState = 0x34;

    if (end < kHeader)
    {
        return 0;
    }
    gcs_xDestructibleAsset_upto(p, kHeader);

    U32 states = gcsRead32(p + 0x4);
    c = kHeader;
    for (U32 i = 0; i < states; i++)
    {
        if (c > end || end - c < kState)
        {
            return 0;
        }
        gcs_xDestructibleAssetState(p + c);
        U32 anims = gcsRead32(p + c + 0x30);
        c += kState;
        if (anims > (end - c) / 4)
        {
            return 0;
        }
        gcsSwapWords(p, c, c + anims * 4);
        c += anims * 4;
    }
    gcsSwapWords(p, c, end);
    return 1;
}

// A JSP asset is one of two things on both discs: a RenderWare clump stream
// (rwID_CLUMP, 0x10, in its little-endian chunk header), left as it is, or the
// JSPINFO blocks, the first of them the collision tree (0xBEEF01), converted.
// Every JSP on the GameCube disc is one or the other. Anything else is damage:
// read as a clump it reaches librw's stream reader as garbage.
static iGCSwapResult iGCSwapJsp(const U8* p, U32 size)
{
    const U32 kChunk = 12;
    if (size == 0)
    {
        return IGCSWAP_ASIS;
    }
    if (size < kChunk)
    {
        return IGCSWAP_MALFORMED;
    }
    U32 first = gcsRead32(p);
    if (first == 0x10)
    {
        return gcsRead32(p + 4) <= size - kChunk ? IGCSWAP_ASIS : IGCSWAP_MALFORMED;
    }
    return first == 0xBEEF01 ? IGCSWAP_DONE : IGCSWAP_MALFORMED;
}

iGCSwapResult iGCSwapAsset(U32 type, void* data, U32 size)
{
    if (iGCSwapGeneratedAsIs(type))
    {
        return IGCSWAP_ASIS;
    }
    if (data == NULL && size != 0)
    {
        return IGCSWAP_MALFORMED;
    }
    if (type == 0x4A535020) // 'JSP '
    {
        iGCSwapResult kind = iGCSwapJsp((const U8*)data, size);
        if (kind != IGCSWAP_DONE)
        {
            return kind;
        }
    }
    S32 r = iGCSwapGenerated(type, (U8*)data, size);
    if (r < 0)
    {
        return IGCSWAP_UNKNOWN;
    }
    return r ? IGCSWAP_DONE : IGCSWAP_MALFORMED;
}

iGCSwapResult iGCSwapRule(U32 type)
{
    if (iGCSwapGeneratedAsIs(type))
    {
        return IGCSWAP_ASIS;
    }
    return iGCSwapGeneratedKnows(type) ? IGCSWAP_DONE : IGCSWAP_UNKNOWN;
}

iGCSwapResult iGCSwapCheck(U32 type, const void* data, U32 size)
{
    if (iGCSwapGeneratedAsIs(type))
    {
        return IGCSWAP_ASIS;
    }
    U8* copy = (U8*)malloc(size != 0 ? size : 1);
    if (copy == NULL)
    {
        return IGCSWAP_MALFORMED;
    }
    if (size != 0)
    {
        memcpy(copy, data, size);
    }
    iGCSwapResult r = iGCSwapAsset(type, copy, size);
    free(copy);
    return r;
}

const char* iGCSwapTypeName(U32 type, char buf[5])
{
    for (S32 i = 0; i < 4; i++)
    {
        U8 ch = (U8)(type >> (24 - 8 * i));
        buf[i] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '?';
    }
    buf[4] = '\0';
    return buf;
}
