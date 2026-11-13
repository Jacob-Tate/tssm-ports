// tssm_widentest: the copy-at-load widening (iAssetWiden.h) run over every
// package of an asset folder, without the game.
//
//     tssm_widentest <asset folder> [--verbose]
//
// For every asset of every widened type it makes the copy, then walks the copy
// the way the game does -- through the game's own structs, with the game's own
// pointer arithmetic -- and checks each field against the bytes on disk, read
// at their disk offsets. It also checks iPtr32 (compat/types.h): a pointer
// below 4 GB, one above it, and the flag bit the game sets on the raw slot;
// and a credits asset of its own (CheckCRDTPicture).
//
// On a 32-bit host nothing is widened; the test then checks only that and
// iPtr32. Exit code 0 when every check passed.

#include "iAssetWiden.h"
#include "iSndXbox.h"

#include "zAssetTypes.h"
#include "iAnimSKB.h"
#include "xSnd.h"
#include "zLOD.h"
#include "zCollGeom.h"
#include "xCM.h"
#include "zDestructible.h"
#include "zShrapnel.h"
#include "xLightKit.h"
#include "zScene.h"
#include "xSplineAsset.h"
#include "xJSP.h"
#include "xClumpColl.h"
#include "xCutscene.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

static S32 sFailures;
static S32 sChecks;
static S32 sPictures; // CRDT picture presets checked
static bool sVerbose;
static U32 sAid;
static U32 sType;

static const char* TypeName(U32 type)
{
    static char buf[5];
    buf[0] = (char)(type >> 24);
    buf[1] = (char)(type >> 16);
    buf[2] = (char)(type >> 8);
    buf[3] = (char)type;
    buf[4] = '\0';
    return buf;
}

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        sChecks++;                                                                                 \
        if (!(cond))                                                                               \
        {                                                                                          \
            if (sFailures < 50)                                                                    \
            {                                                                                      \
                printf("FAIL: %s %08x: ", TypeName(sType), sAid);                                  \
                printf(__VA_ARGS__);                                                               \
                printf("\n");                                                                      \
            }                                                                                      \
            sFailures++;                                                                           \
            return;                                                                                \
        }                                                                                          \
    } while (0)

static U32 D32(const U8* d, U32 off)
{
    U32 v;
    memcpy(&v, d + off, 4);
    return v;
}

static U16 D16(const U8* d, U32 off)
{
    U16 v;
    memcpy(&v, d + off, 2);
    return v;
}

static F32 DF(const U8* d, U32 off)
{
    F32 v;
    memcpy(&v, d + off, 4);
    return v;
}

static bool Aligned(const void* p, U32 a)
{
    return ((UPtr)p % a) == 0;
}

// ---------------------------------------------------------------------------

static void CheckATBL(const U8* d, U32 size, U8* w, U32 wsize)
{
    // The way ATBL_Read walks it (zAssetTypes.cpp).
    U8* zaBytes = w;
    xAnimAssetTable* zaTbl = (xAnimAssetTable*)zaBytes;
    void** zaRaw = (void**)(zaTbl + 1);
    xAnimAssetFile* zaFile = (xAnimAssetFile*)(zaRaw + zaTbl->NumRaw);
    xAnimAssetState* zaState = (xAnimAssetState*)(zaFile + zaTbl->NumFiles);

    CHECK(memcmp(w, d, 20) == 0, "table header");
    CHECK(Aligned(zaRaw, sizeof(void*)) && Aligned(zaFile, sizeof(void*)), "alignment");
    U32 nraw = D32(d, 4), nfiles = D32(d, 8), nstates = D32(d, 12);
    for (U32 i = 0; i < nraw; i++)
    {
        CHECK(*(U32*)&zaRaw[i] == D32(d, 20 + 4 * i) && (UPtr)zaRaw[i] == D32(d, 20 + 4 * i),
              "raw id %u", i);
    }
    U32 df = 20 + 4 * nraw;
    for (U32 i = 0; i < nfiles; i++)
    {
        const U8* rec = d + df + 32 * i;
        CHECK(zaFile[i].FileFlags == D32(rec, 0) && zaFile[i].Duration == DF(rec, 4) &&
                  zaFile[i].TimeOffset == DF(rec, 8) && zaFile[i].NumAnims[0] == D16(rec, 12) &&
                  zaFile[i].NumAnims[1] == D16(rec, 14) && zaFile[i].Physics == (S32)D32(rec, 20) &&
                  zaFile[i].StartPose == (S32)D32(rec, 24) && zaFile[i].EndPose == (S32)D32(rec, 28),
              "file %u fields", i);
        void** rawData = (void**)(zaBytes + (U32)(UPtr)zaFile[i].RawData);
        CHECK((U32)(UPtr)zaFile[i].RawData < wsize && Aligned(rawData, sizeof(void*)),
              "file %u index array", i);
        U32 count = zaFile[i].NumAnims[0] * zaFile[i].NumAnims[1];
        for (U32 k = 0; k < count; k++)
        {
            CHECK((U32)(UPtr)rawData[k] == D32(d, D32(rec, 16) + 4 * k), "file %u index %u", i, k);
        }
    }
    U32 ds = df + 32 * nfiles;
    for (U32 i = 0; i < nstates; i++)
    {
        const U8* rec = d + ds + 28 * i;
        CHECK(zaState[i].StateID == D32(rec, 0) && zaState[i].FileIndex == D32(rec, 4) &&
                  zaState[i].EffectCount == D32(rec, 8) && zaState[i].Speed == DF(rec, 16) &&
                  zaState[i].SubStateID == D32(rec, 20) && zaState[i].SubStateCount == D32(rec, 24),
              "state %u fields", i);
        // The effects chain, as ATBL_Read steps through it.
        xAnimAssetEffect* e = (xAnimAssetEffect*)(zaBytes + zaState[i].EffectOffset);
        U32 de = D32(rec, 12);
        for (U32 j = 0; j < zaState[i].EffectCount; j++)
        {
            U32 n = sizeof(xAnimAssetEffect) + e->UserDataSize;
            CHECK(de + n <= size && memcmp(e, d + de, n) == 0, "state %u effect %u", i, j);
            e = (xAnimAssetEffect*)((UPtr)e + e->UserDataSize + sizeof(xAnimAssetEffect));
            de += n;
        }
    }
}

static void CheckSGRP(const U8* d, U32 size, U8* w, U32 wsize)
{
    xSndGroup* g = (xSndGroup*)w;
    CHECK(g->header.id == D32(d, 0) && g->header.uPlayedMask == D32(d, 8) &&
              g->header.uInfoCount == d[12] && g->header.uPriority == d[15] &&
              g->header.uFlags == d[16] && g->header.eSoundCategory == d[17] &&
              g->header.ePlayRule == d[18] && g->header.fInnerRadius == DF(d, 20) &&
              g->header.fOuterRadius == DF(d, 24) && (UPtr)g->header.pszGroupName == D32(d, 28),
          "header");
    CHECK(wsize == size + (sizeof(xSndGroupHeader) - 32), "size");
    for (U32 i = 0; i < g->header.uInfoCount; i++)
    {
        CHECK(memcmp(&g->aSndGroupInfo[i], d + 32 + 16 * i, 16) == 0, "info %u", i);
    }
}

static void CheckLODT(const U8* d, U32 size, U8* w, U32 wsize)
{
    // zLOD.cpp: the count, then the rows at data + 4.
    S32 count = *(S32*)w;
    zLODTable* rows = (zLODTable*)((S32*)w + 1);
    CHECK(count == (S32)D32(d, 0) && Aligned(rows, sizeof(void*)), "count");
    for (S32 i = 0; i < count; i++)
    {
        const U8* r = d + 4 + 36 * i;
        CHECK((UPtr)rows[i].baseBucket == D32(r, 0) && rows[i].noRenderDist == DF(r, 4) &&
                  rows[i].flags == D32(r, 8) && (UPtr)rows[i].lodBucket[0] == D32(r, 12) &&
                  (UPtr)rows[i].lodBucket[1] == D32(r, 16) &&
                  (UPtr)rows[i].lodBucket[2] == D32(r, 20) && rows[i].lodDist[0] == DF(r, 24) &&
                  rows[i].lodDist[1] == DF(r, 28) && rows[i].lodDist[2] == DF(r, 32),
              "row %d", i);
    }
}

static void CheckCOLL(const U8* d, U32 size, U8* w, U32 wsize)
{
    U32 count = *(U32*)w;
    zCollGeomTable* rows = (zCollGeomTable*)((U32*)w + 1);
    CHECK(count == D32(d, 0) && Aligned(rows, sizeof(void*)), "count");
    for (U32 i = 0; i < count; i++)
    {
        const U8* r = d + 4 + 12 * i;
        CHECK((UPtr)rows[i].baseModel == D32(r, 0) && (UPtr)rows[i].colModel[0] == D32(r, 4) &&
                  (UPtr)rows[i].camcolModel == D32(r, 8),
              "row %u", i);
    }
}

static void CheckCRDT(const U8* disk, U32 size, U8* w, U32 wsize)
{
    // The disk side decrypted as xCMstart does, and walked at disk offsets.
    std::vector<U8> d(disk, disk + size);
    U32 total = D32(&d[0], 20);
    if (D32(&d[0], 12) & 0x2)
    {
        static const char key[] = "xCMChunkHandler";
        U8 last = 0;
        for (U32 i = 24, k = 0; i < total; i++, k = (k + 1) % 12)
        {
            d[i] = d[i] ^ last ^ (U8)key[k];
            last = d[i];
        }
    }

    // The copy as xCMprep/xCMrender walk it (xCM.cpp).
    xCMheader* hdr = (xCMheader*)w;
    CHECK(hdr->magic == 0xBEEEEEEF && (hdr->state & 0x2) == 0 &&
              (hdr->state & 0x1) == (D32(&d[0], 12) & 0x1),
          "header");
    char* dp = (char*)(hdr + 1);
    U32 ddp = 24;
    U32 hunks = 0;
    while (dp - (char*)w < (S32)hdr->total_size)
    {
        xCMcredits* cp = (xCMcredits*)dp;
        xCMpreset* pp = (xCMpreset*)(cp + 1);
        xCMhunk* hp = (xCMhunk*)(pp + cp->num_presets);
        U32 dnp = D32(&d[0], ddp + 52);
        CHECK(cp->num_presets == dnp && cp->len == DF(&d[0], ddp + 4), "credits block at %u", ddp);
        for (U32 i = 0; i < dnp; i++)
        {
            const U8* dpre = &d[ddp + 56 + 76 * i];
            if (pp[i].align != 4)
            {
                CHECK(memcmp(&pp[i], dpre, 76) == 0, "credits block at %u: preset %u", ddp, i);
                continue;
            }
            // xCMrender's picture: box[0] as an xCMtexture, looked up while its texture is
            // NULL. On disk the slot is 4 bytes and 4 of pad; the rest is as it is.
            const U32 slot = offsetof(xCMpreset, box) + offsetof(xCMtexture, texture);
            const U32 end = slot + sizeof(void*); // the host pointer takes in the pad
            const xCMtexture* tex = (const xCMtexture*)&pp[i].box[0];
            CHECK(tex->texture == NULL && memcmp(&pp[i], dpre, slot) == 0 &&
                      memcmp((U8*)&pp[i] + end, dpre + end, 76 - end) == 0,
                  "credits block at %u: picture preset %u (texture %p, pad on disk %08x)", ddp, i,
                  (void*)tex->texture, D32(dpre, slot + 4));
            sPictures++;
        }
        U32 dhp = ddp + 56 + 76 * dnp;
        U32 dcs = D32(&d[0], ddp);
        while ((char*)hp - (char*)cp < (S32)cp->credits_size)
        {
            U32 dhs = D32(&d[0], dhp);
            CHECK(hp->hunk_size == dhs + (sizeof(xCMhunk) - 24) && hp->preset == D32(&d[0], dhp + 4) &&
                      hp->t0 == DF(&d[0], dhp + 8) && hp->t1 == DF(&d[0], dhp + 12) &&
                      memcmp(hp + 1, &d[dhp + 24], dhs - 24) == 0,
                  "hunk at %u", dhp);
            // The texts, as xCMprep relocates them from offsets (state bit 0).
            U16 align = pp[hp->preset].align;
            if (align <= 3 && (hdr->state & 0x1))
            {
                const char* texts[2] = { hp->text1, hp->text2 };
                const U32 dtexts[2] = { D32(&d[0], dhp + 16), D32(&d[0], dhp + 20) };
                for (S32 t = 0; t < 2; t++)
                {
                    if (dtexts[t] == 0)
                    {
                        CHECK(texts[t] == NULL, "hunk at %u: text %d", dhp, t);
                        continue;
                    }
                    const char* mine = (const char*)w + (UPtr)texts[t];
                    const char* theirs = (const char*)&d[dtexts[t]];
                    CHECK((UPtr)texts[t] < wsize && strncmp(mine, theirs, 256) == 0,
                          "hunk at %u: text %d \"%.20s\" vs \"%.20s\"", dhp, t, mine, theirs);
                }
            }
            hp = (xCMhunk*)((char*)hp + hp->hunk_size);
            dhp += dhs;
            hunks++;
        }
        CHECK(dhp - ddp == dcs, "credits block at %u: size", ddp);
        dp = (char*)hp;
        ddp = dhp;
    }
    CHECK(ddp == total && hunks > 0, "end");
}

static void CheckDEST(const U8* d, U32 size, U8* w, U32 wsize)
{
    // zDestructibleAsset_Init's walk (zDestructible.cpp).
    xDestructibleAsset* asset = (xDestructibleAsset*)w;
    CHECK(memcmp(w, d, 37) == 0, "header");
    xDestructibleAssetState* s = (xDestructibleAssetState*)&asset->states;
    U32 ds = 40;
    for (U32 i = 0; i < asset->nstates; i++)
    {
        CHECK(memcmp(s, d + ds, 48) == 0, "state %u fields", i);
        xDestructibleAssetAttachedAnimList* list = (xDestructibleAssetAttachedAnimList*)&s->animlist;
        U32 n = D32(d, ds + 48);
        CHECK(list->nanimations == n, "state %u count", i);
        U32* ids = (U32*)&list->animationIDs;
        for (U32 k = 0; k < n; k++)
        {
            CHECK(ids[k] == D32(d, ds + 52 + 4 * k), "state %u id %u", i, k);
        }
        void* next = s + 1;
        next = (U32*)next + list->nanimations;
        s = (xDestructibleAssetState*)next;
        ds += 52 + 4 * n;
    }
    U32 used = (U32)((U8*)s - w);
    CHECK(wsize - used == size - ds && memcmp(s, d + ds, size - ds) == 0, "tail");
}

static void CheckFragBase(const zFragAsset* f, const U8* r)
{
    CHECK((U32)f->type == D32(r, 0) && f->id == D32(r, 4) && f->parentID[0] == D32(r, 8) &&
              f->parentID[1] == D32(r, 12) && f->lifetime == DF(r, 16) && f->delay == DF(r, 20),
          "fragment base");
}

static void CheckSHRP(const U8* d, U32 size, U8* w, U32 wsize)
{
    // zShrapnel_SceneInit's walk (zShrapnel.cpp).
    zShrapnelAsset* sa = (zShrapnelAsset*)w;
    CHECK(sa->fassetCount == (S32)D32(d, 0) && sa->shrapnelID == D32(d, 4) &&
              (UPtr)sa->initCB == D32(d, 8),
          "header");
    zFragAsset* fa = (zFragAsset*)(sa + 1);
    U32 dr = 12;
    for (S32 j = 0; j < sa->fassetCount; j++)
    {
        const U8* r = d + dr;
        CHECK(Aligned(fa, 4), "fragment %d alignment", j);
        CheckFragBase(fa, r);
        switch (fa->type)
        {
        case eFragProjectile:
        {
            zFragProjectileAsset* p = (zFragProjectileAsset*)fa;
            CHECK(p->modelInfoID == D32(r, 24) && (UPtr)p->modelFile == D32(r, 28) &&
                      memcmp(&p->launch, r + 32, 200) == 0 && p->bounce == DF(r, 232) &&
                      p->maxBounces == (S32)D32(r, 236) && p->flags == D32(r, 240) &&
                      p->childID == D32(r, 244) && (UPtr)p->child == D32(r, 248) &&
                      p->minScale == DF(r, 252) && p->maxScale == DF(r, 256) &&
                      p->scaleCurveID == D32(r, 260) && (UPtr)p->scaleCurve == D32(r, 264) &&
                      p->gravity == DF(r, 268),
                  "projectile %d", j);
            fa = p + 1;
            dr += 272;
            break;
        }
        case eFragParticle:
        {
            zFragParticleAsset* p = (zFragParticleAsset*)fa;
            CHECK(memcmp(&p->source, r + 24, 80) == 0 &&
                      memcmp((xParEmitterPropsAsset*)&p->emit, r + 104, 312) == 0 &&
                      p->emit.custom_flags == D32(r, 416) && p->emit.attachToID == D32(r, 420) &&
                      p->emit.radius == DF(r, 480) && p->emit.emit_interval_current == DF(r, 484) &&
                      (UPtr)p->emit.emit_volume == D32(r, 488) && p->flags == D32(r, 492) &&
                      p->parEmitterID == D32(r, 496) && (UPtr)p->parEmitter == D32(r, 500),
                  "particle %d", j);
            fa = p + 1;
            dr += 504;
            break;
        }
        case eFragShrapnel:
        {
            zFragShrapnelAsset* p = (zFragShrapnelAsset*)fa;
            CHECK(p->shrapnelID == D32(r, 24) && (UPtr)p->shrapAsset == D32(r, 28),
                  "shrapnel %d", j);
            fa = p + 1;
            dr += 32;
            break;
        }
        case eFragSound:
            CHECK(memcmp(fa, r, 68) == 0, "sound %d", j);
            fa = (zFragSoundAsset*)fa + 1;
            dr += 68;
            break;
        case eFragLightning:
            CHECK(memcmp(fa, r, 112) == 0, "lightning %d", j);
            fa = (zFragLightningAsset*)fa + 1;
            dr += 112;
            break;
        case eFragExplosion:
            CHECK(memcmp(fa, r, 72) == 0, "explosion %d", j);
            fa = (zFragExplosionAsset*)fa + 1;
            dr += 72;
            break;
        case eFragDistortion:
            CHECK(memcmp(fa, r, 92) == 0, "distortion %d", j);
            fa = (zFragDistortionAsset*)fa + 1;
            dr += 92;
            break;
        case eFragFire:
            CHECK(memcmp(fa, r, 92) == 0, "fire %d", j);
            fa = (zFragFireAsset*)fa + 1;
            dr += 92;
            break;
        default:
            CHECK(false, "fragment %d of type %u, which zShrapnel_SceneInit does not walk", j,
                  (U32)fa->type);
        }
    }
    CHECK(dr == size && (U8*)fa - w == (S32)wsize, "end");
}

static void CheckLKIT(const U8* d, U32 size, U8* w, U32 wsize)
{
    // xLightKit_Prepare: the lights follow the header.
    xLightKit* lkit = (xLightKit*)w;
    xLightKitLight* lights = (xLightKitLight*)(lkit + 1);
    CHECK(lkit->tagID == D32(d, 0) && lkit->groupID == D32(d, 4) && lkit->lightCount == D32(d, 8) &&
              (UPtr)lkit->lightList == D32(d, 12),
          "header");
    for (U32 i = 0; i < lkit->lightCount; i++)
    {
        const U8* r = d + 16 + 96 * i;
        CHECK(memcmp(&lights[i], r, 92) == 0 && (UPtr)lights[i].platLight == D32(r, 92),
              "light %u", i);
    }
}

static void CheckDYNA(const U8* d, U32 size, U8* w, U32 wsize)
{
    // zScene.cpp's parameters.
    zSceneParameters* p = (zSceneParameters*)w;
    CHECK(memcmp(w, d, 16) == 0 && p->idle03ExtraCount == D32(d, 16) &&
              p->idle04ExtraCount == D32(d, 24) && p->bombCount == d[32] &&
              p->extraIdleDelay == d[33] && p->hdrGlow == d[34] && p->hdrDarken == d[35] &&
              p->uDefaultMusicHash == D32(d, 36) && p->flags == D32(d, 40) &&
              p->waterTileWidth == DF(d, 44) && p->lodFadeDistance == DF(d, 48) &&
              memcmp(p->pad, d + 52, 16) == 0,
          "fields");
    U32 counts[2] = { p->idle03ExtraCount, p->idle04ExtraCount };
    xAnimFile** arrays[2] = {
        (xAnimFile**)((UPtr)p->idle03Extras + (UPtr)&p->idle03ExtraCount),
        (xAnimFile**)((UPtr)p->idle04Extras + (UPtr)&p->idle03ExtraCount),
    };
    U32 doffs[2] = { D32(d, 20), D32(d, 28) };
    for (S32 a = 0; a < 2; a++)
    {
        for (U32 k = 0; k < counts[a]; k++)
        {
            CHECK((U32)(UPtr)arrays[a][k] == D32(d, 16 + doffs[a] + 4 * k), "extras %d id %u", a, k);
        }
    }
}

static void CheckSPLN(const U8* d, U32 size, U8* w, U32 wsize)
{
    // x::Spline_Read (zAssetTypes.cpp).
    x::NURBS* n = (x::NURBS*)w;
    n->control = (xVec3*)(n + 1);
    n->knot = (F32*)(n->control + n->n + 1);
    CHECK(memcmp(w, d, 20) == 0 && n->p == (S32)D32(d, 8) && n->n == (S32)D32(d, 16), "header");
    U32 rest = size - 28;
    CHECK(wsize - sizeof(x::NURBS) == rest && memcmp(n->control, d + 28, rest) == 0, "points");
    CHECK((U8*)(n->knot + n->m + 1) - w <= (S32)wsize, "knots in range");
}

static void CheckJSP(const U8* d, U32 size, U8* w, U32 wsize)
{
    // xJSP_MultiStreamRead's version-5 path (xJSP.cpp).
    __rwMark mark;
    __rwMark* mp = (__rwMark*)w;
    memmove(&mark, mp, sizeof(mark));
    CHECK(mark.type == 0xBEEF01, "first mark");
    U8* data = (U8*)(mp + 1);
    U32* header = (U32*)data;
    U32 numBranch = header[1];
    U32 numTri = header[2];
    xClumpCollBSPBranchNode* branches = (xClumpCollBSPBranchNode*)(header + 3);
    xClumpCollBSPTriangle* tris =
        numBranch ? (xClumpCollBSPTriangle*)(branches + numBranch) : (xClumpCollBSPTriangle*)(header + 3);
    U32 d1 = 12;
    CHECK(memcmp(header, d + d1, 12) == 0 && memcmp(branches, d + d1 + 12, 16 * numBranch) == 0,
          "collision header and branches");
    const U8* dt = d + d1 + 12 + 16 * numBranch;
    for (U32 i = 0; i < numTri; i++)
    {
        CHECK(tris[i].v.rawIdx == D32(dt, 8 * i) && tris[i].flags == dt[8 * i + 4] &&
                  tris[i].detailed_info_cache_index == dt[8 * i + 5] && tris[i].matIndex == D16(dt, 8 * i + 6),
              "triangle %u", i);
    }
    CHECK(Aligned(tris, sizeof(void*)), "triangle alignment");

    mp = (__rwMark*)(data + mark.length);
    memmove(&mark, mp, sizeof(mark));
    CHECK(mark.type == 0xBEEF02, "second mark");
    data = (U8*)(mp + 1);
    xJSPHeader* tmphdr = (xJSPHeader*)data;
    U32 d2 = 12 + D32(d, 4) + 12;
    CHECK(Aligned(tmphdr, sizeof(void*)), "header alignment");
    CHECK(memcmp(tmphdr->idtag, d + d2, 4) == 0 && tmphdr->version == 5 &&
              tmphdr->jspNodeCount == D32(d, d2 + 8) && tmphdr->stripVecCount == D32(d, d2 + 24) &&
              tmphdr->vertDataFlags == D16(d, d2 + 32) && tmphdr->vertDataStride == D16(d, d2 + 34),
          "header fields");
    xJSPNodeInfo* nodes = (xJSPNodeInfo*)(tmphdr + 1);
    CHECK(memcmp(nodes, d + d2 + 44, 8 * tmphdr->jspNodeCount) == 0, "node infos");
    xJSPNodeTree* tree = (xJSPNodeTree*)(nodes + tmphdr->jspNodeCount);
    U32 dtree = d2 + 44 + 8 * tmphdr->jspNodeCount;
    CHECK(tree->numBranchNodes == (S32)D32(d, dtree) && tree->numLeafNodes == (S32)D32(d, dtree + 8),
          "node tree counts");
    xJSPNodeTreeBranch* tb = (xJSPNodeTreeBranch*)(tree + 1);
    xJSPNodeTreeLeaf* tl = (xJSPNodeTreeLeaf*)(tb + tree->numBranchNodes);
    U32 dtb = dtree + 16;
    U32 dtl = dtb + 16 * tree->numBranchNodes;
    CHECK(memcmp(tb, d + dtb, 16 * tree->numBranchNodes) == 0 &&
              memcmp(tl, d + dtl, 32 * tree->numLeafNodes) == 0,
          "node tree branches and leaves");
    U32* stripvec = (U32*)(tl + tree->numLeafNodes);
    U32 dsv = dtl + 32 * tree->numLeafNodes;
    CHECK(*stripvec == D32(d, dsv) && memcmp(stripvec + 1, d + dsv + 4, 12 * *stripvec) == 0,
          "strip vectors");
    for (U32 i = 0; i < numTri; i++)
    {
        CHECK(tris[i].v.rawIdx < *stripvec, "triangle %u vertex in range", i);
    }

    __rwMark* mp2 = (__rwMark*)(data + mark.length);
    __rwMark mark2;
    memmove(&mark2, mp2, sizeof(mark2));
    U32 d4 = d2 + D32(d, d2 - 8);
    CHECK(mark2.type == D32(d, d4) && memcmp(mp2, d + d4, size - d4) == 0, "the rest");
}

// ---------------------------------------------------------------------------

namespace
{
    struct TypeCheck
    {
        U32 type;
        void (*check)(const U8* d, U32 size, U8* w, U32 wsize);
        U32 widened;
        U32 asIs;
        U64 diskBytes;
        U64 hostBytes;
    };
} // namespace

static TypeCheck sTypes[] = {
    { 'ATBL', CheckATBL }, { 'SGRP', CheckSGRP }, { 'LODT', CheckLODT }, { 'COLL', CheckCOLL },
    { 'CRDT', CheckCRDT }, { 'DEST', CheckDEST }, { 'SHRP', CheckSHRP }, { 'LKIT', CheckLKIT },
    { 'DYNA', CheckDYNA }, { 'SPLN', CheckSPLN }, { 'JSP ', CheckJSP },
};
#define NUM_TYPES ((S32)(sizeof(sTypes) / sizeof(sTypes[0])))

static FILE* sPkg;

static U32 ReadPkg(void*, U32 offset, void* buf, U32 size)
{
    if (fseek(sPkg, (long)offset, SEEK_SET) != 0)
    {
        return 0;
    }
    return (U32)fread(buf, 1, size, sPkg);
}

static void ListPackages(const std::string& dir, std::vector<std::string>& out)
{
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        return;
    }
    do
    {
        std::string name = fd.cFileName;
        if (name == "." || name == "..")
        {
            continue;
        }
        std::string path = dir + "\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            ListPackages(path, out);
        }
        else if (name.size() > 4)
        {
            std::string ext = name.substr(name.size() - 4);
            for (size_t i = 0; i < ext.size(); i++)
            {
                ext[i] = (char)toupper((unsigned char)ext[i]);
            }
            if (ext == ".HIP" || ext == ".HOP")
            {
                out.push_back(path);
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    (void)dir;
    (void)out;
#endif
}

static void CheckPtr32()
{
    sType = 'PT32';
    sAid = 0;

    struct Slot
    {
        iPtr32<void> p;
    };
    static_assert(sizeof(Slot) == 4, "an iPtr32 is 4 bytes");
    static_assert(sizeof(iAnimSKBHeader) == 28, "iAnimSKBHeader keeps the disk layout");
    static_assert(sizeof(xCutsceneData) == 16, "xCutsceneData keeps the disk layout");

    Slot s;
    s.p = NULL;
    CHECK(s.p == NULL && !s.p, "NULL");

    static int sStatic; // the executable's statics are above 4 GB on x64
    void* low = (void*)(UPtr)0x0C001230u;
    s.p = low;
    CHECK((void*)s.p == low && (UPtr)s.p == 0x0C001230u, "a pointer below 4 GB");

    s.p = &sStatic;
    CHECK((int*)s.p == &sStatic, "a static (%p)", (void*)&sStatic);
    // zEntPlayer and Neptune: flag bit 31 on the raw slot, then cleared.
    s.p = (void*)((UPtr)s.p | 0x80000000);
    CHECK(((UPtr)s.p & 0x80000000) != 0, "the flag bit");
    s.p = (void*)((UPtr)s.p & 0x7fffffff);
    CHECK((int*)s.p == &sStatic, "the static after the flag bit");
}

static void W32(std::vector<U8>& v, U32 off, U32 x)
{
    memcpy(&v[off], &x, 4);
}

static void WF(std::vector<U8>& v, U32 off, F32 x)
{
    memcpy(&v[off], &x, 4);
}

// A credits asset of the test's own, so the picture preset is checked without
// the game's data: a text hunk and a picture hunk, with the picture's pad set as
// the game's files set it (0.1f). Its 8 bytes read as the host's texture
// pointer were not NULL, and xCMrender drew through them. Run with the picture
// as either preset (its slot then 4 past or on an 8-byte boundary in the copy),
// encrypted as the files are or already decrypted.
static void CheckCRDTPicture(U32 picture, bool encrypted)
{
    const U32 state = encrypted ? 0x3 : 0x1; // text offsets, and encrypted or not
    sType = 'CRDT';
    sAid = state; // names the variant in a FAIL line

    std::vector<U8> a(296);
    W32(a, 0, 0xBEEEEEEF);
    W32(a, 4, 1);
    W32(a, 12, state);
    WF(a, 16, 10.0f);
    W32(a, 20, 296);

    W32(a, 24, 272); // credits_size
    WF(a, 28, 10.0f);
    W32(a, 76, 2); // num_presets

    // Presets at 80 and 156: one text (align 0), one the picture.
    const U32 pic = 80 + 76 * picture;
    a[pic + 2] = 4;
    W32(a, pic + 12, 0xD01F5953); // ui_heavy_iron_logo
    W32(a, pic + 16, 0xFFFFFFFF);
    WF(a, pic + 20, 0.35f);
    WF(a, pic + 24, 0.7f);
    WF(a, pic + 28, 0.3f);
    WF(a, pic + 32, 0.4f);
    W32(a, pic + 36, 0); // texture
    W32(a, pic + 40, 0x3DCCCCCD); // pad

    // Hunk at 232 (the text preset, text1 at 256) and at 264 (the picture's caption).
    W32(a, 232, 32);
    W32(a, 236, 1 - picture);
    WF(a, 244, 5.0f);
    W32(a, 248, 256);
    memcpy(&a[256], "credits", 8);
    W32(a, 264, 32);
    W32(a, 268, picture);
    WF(a, 272, 1.0f);
    WF(a, 276, 3.0f);
    memcpy(&a[288], "logo", 5);

    std::vector<U8> disk(a);
    if (encrypted)
    {
        static const char key[] = "xCMChunkHandler";
        U8 last = 0;
        for (U32 i = 24, k = 0; i < 296; i++, k = (k + 1) % 12)
        {
            disk[i] = a[i] ^ last ^ (U8)key[k];
            last = a[i];
        }
    }

    U32 wsize = 0;
    const char* why = "";
    void* copy = iAssetWiden('CRDT', 0, &disk[0], 296, &wsize, &why);
    if (sizeof(void*) == 4)
    {
        CHECK(copy == NULL, "the test asset was widened on a 32-bit host");
        return;
    }
    CHECK(copy != NULL, "the test asset (picture preset %u) was not widened: %s", picture, why);
    if (copy == NULL)
    {
        return;
    }
    S32 pictures = sPictures;
    CheckCRDT(&disk[0], 296, (U8*)copy, wsize);
    iAssetWidenFree(copy);
    CHECK(sPictures == pictures + 1 && wsize == 296 + 2 * (sizeof(xCMhunk) - 24),
          "the test asset (picture preset %u): %d picture presets, %u bytes", picture,
          (int)(sPictures - pictures), wsize);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("usage: tssm_widentest <asset folder> [--verbose]\n");
        return 2;
    }
    for (S32 i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--verbose") == 0)
        {
            sVerbose = true;
        }
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    CheckPtr32();
    for (U32 picture = 0; picture < 2; picture++)
    {
        CheckCRDTPicture(picture, true);
        CheckCRDTPicture(picture, false);
    }
    const S32 ownPictures = sPictures;

    std::vector<std::string> pkgs;
    ListPackages(argv[1], pkgs);
    if (pkgs.empty())
    {
        printf("FAIL: no .HIP or .HOP under %s\n", argv[1]);
        return 1;
    }

    U32 types[NUM_TYPES + 1];
    for (S32 i = 0; i < NUM_TYPES; i++)
    {
        types[i] = sTypes[i].type;
    }
    types[NUM_TYPES] = 0;

    std::vector<iSndHipAsset> assets(200000);
    S32 packages = 0;
    for (size_t p = 0; p < pkgs.size(); p++)
    {
        sPkg = fopen(pkgs[p].c_str(), "rb");
        if (sPkg == NULL)
        {
            printf("FAIL: cannot open %s\n", pkgs[p].c_str());
            sFailures++;
            continue;
        }
        S32 n = iSndHipFindAssets(ReadPkg, NULL, types, &assets[0], (S32)assets.size());
        if (n < 0)
        {
            fclose(sPkg);
            continue;
        }
        packages++;

        for (S32 i = 0; i < n; i++)
        {
            const iSndHipAsset& a = assets[i];
            TypeCheck* tc = NULL;
            for (S32 k = 0; k < NUM_TYPES; k++)
            {
                if (sTypes[k].type == a.type)
                {
                    tc = &sTypes[k];
                }
            }
            if (tc == NULL || a.size == 0)
            {
                continue;
            }

            std::vector<U8> disk(a.size);
            if (ReadPkg(NULL, a.offset, &disk[0], a.size) != a.size)
            {
                printf("FAIL: %s: short read of %08x\n", pkgs[p].c_str(), a.aid);
                sFailures++;
                continue;
            }

            sAid = a.aid;
            sType = a.type;
            U32 wsize = 0;
            const char* why = "";
            void* copy = iAssetWiden(a.type, a.aid, &disk[0], a.size, &wsize, &why);
            if (copy == NULL)
            {
                tc->asIs++;
                if (*why != '\0')
                {
                    printf("FAIL: %s %08x in %s: not widened: %s\n", TypeName(a.type), a.aid,
                           pkgs[p].c_str(), why);
                    sFailures++;
                }
                else if (iAssetWidenType(a.type) && a.type != 'DYNA' && a.type != 'JSP ')
                {
                    printf("FAIL: %s %08x: not widened, and no reason given\n", TypeName(a.type),
                           a.aid);
                    sFailures++;
                }
                continue;
            }

            tc->widened++;
            tc->diskBytes += a.size;
            tc->hostBytes += wsize;
            S32 before = sFailures;
            tc->check(&disk[0], a.size, (U8*)copy, wsize);
            if (sFailures != before)
            {
                printf("      (in %s)\n", pkgs[p].c_str());
            }
            iAssetWidenFree(copy);
        }
        fclose(sPkg);
        if (sVerbose)
        {
            printf("%s: %d assets of the widened types\n", pkgs[p].c_str(), n);
        }
    }

    printf("tssm_widentest: %d packages, %d-bit host\n", packages, (int)(sizeof(void*) * 8));
    for (S32 k = 0; k < NUM_TYPES; k++)
    {
        printf("  %s  widened %5u (%8llu -> %8llu bytes)  read as they are %5u\n",
               TypeName(sTypes[k].type), sTypes[k].widened, (unsigned long long)sTypes[k].diskBytes,
               (unsigned long long)sTypes[k].hostBytes, sTypes[k].asIs);
    }
    if (sizeof(void*) == 8)
    {
        printf("  CRDT picture presets checked: %d (%d of them the test's own)\n", (int)sPictures,
               (int)ownPictures);
    }

    U32 live, bytes, peak;
    iAssetWidenStats(&live, &bytes, &peak);
    if (live != 0)
    {
        printf("FAIL: %u copies not freed\n", live);
        sFailures++;
    }
    if (sizeof(void*) == 8)
    {
        // Every type with assets in the data has to have been widened.
        for (S32 k = 0; k < NUM_TYPES; k++)
        {
            if (sTypes[k].widened == 0 && sTypes[k].asIs != 0 && sTypes[k].type != 'DYNA')
            {
                printf("FAIL: no %s asset was widened\n", TypeName(sTypes[k].type));
                sFailures++;
            }
        }
    }

    printf("%s: %d checks, %d failures\n", sFailures == 0 ? "PASS" : "FAIL", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
