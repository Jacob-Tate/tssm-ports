// tssm_extrastest: the Xbox extras' decisions (iExtras.h) and their settings,
// without the game.
//
//   tssm_extrastest <scratch folder>
//
// What it checks:
//   - the decision for every feature, setting, files (Xbox, GameCube) and
//     renderer (shaders, D3D9 fixed, 3DS), including scenes it knows nothing
//     about;
//   - whose files a level is, from its HIP's platform ("BB/BB02.HIP", then
//     "BB02.HIP") and before the first level from boot.HIP's, kept once
//     boot.HIP has answered;
//   - the water's own fallback (iXboxWaterActive) reaching the overlay's
//     sentence and the level's line only in a level loaded with xbox;
//   - the spotlights the GameCube keeps off, by scene (SPOTLIGHT_02 is in
//     BB02 and in B401), against xStrHash of their names;
//   - the crowd models, against xStrHash of their names;
//   - with GameCube files: the skinned crowd for each rigid one, and the
//     spotlight links turned into the Xbox files' (iExtrasGC.h), on copies of
//     the two discs' links, and refused for links that are not the disc's;
//   - a level's own fallback (iExtrasFallBack) and note reaching the overlay's
//     sentence, and cleared by the next level;
//   - the Xbox glow's strength for every scene the Xbox names, and the default
//     for the rest;
//   - a level's decisions, the "next level" difference, and the frame lock's
//     count reaching the pacer, read through the real config.ini layer;
//   - [video] water read as [extras] water, never appended over, and moved to
//     the new name by a change.
//   - a value that is neither xbox nor gamecube giving the table's default,
//     with its text for the overlay.
//
// The pacer, the renderer, the files' platforms and the water's answer are
// stand-ins here (below); the config layer is the game's own, over a config.ini
// in the scratch folder.

#include "iConfig.h"
#include "iConfigTable.h"
#include "iExtras.h"
#include "iFile.h"
#include "iPadBind.h"
#include "iScreen.h"
#include "iWindow.h"
#include "iXboxDistort.h"
#include "iXboxWaterDraw.h"
#include "xEvent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

// --- stand-ins ---------------------------------------------------------------------

// One binding row: iConfig.cpp writes and reads the binding sections from the
// pad table, which brings the pad backend with it.
const iPadBindButton kPadBindButtons[] = { { "a", 0, "a", "space", NULL, 0 } };
const S32 kPadBindButtonCount = 1;

const iPadBindButton* iPadBindFind(const char* name)
{
    return strcmp(name, "a") == 0 ? &kPadBindButtons[0] : NULL;
}

const char* iPadBindDescribe(const iPadBindButton*)
{
    return NULL;
}

const char* iPadBindPadDefault(const iPadBindButton* button)
{
    return button->pad;
}

static iScreenBackend sBackend = iSCREENBACKEND_D3D11;
static iScreenPipeline sPipeline = iSCREENPIPE_AUTO;

iScreenBackend iScreenGetBackend()
{
    return sBackend;
}

iScreenPipeline iScreenGetPipeline()
{
    return sPipeline;
}

static S32 sLock = -1;

// The packages' platforms (iFile.cpp reads each one's PLAT chunk): the names
// asked, and the answers. Every package is the Xbox's unless named here.
static char sAsked[8][32];
static int sAskedCount;
static const char* sGameCubeHip; // a scene HIP that is GameCube's
static const char* sMissingHip; // a name with no file there
static const char* sBootId; // boot.HIP's PLAT id, or NULL (cannot be read)
static int sBootReads;

iGCPlatform iFilePackagePlatformOf(const char* name)
{
    if (sAskedCount < 8)
    {
        snprintf(sAsked[sAskedCount++], sizeof(sAsked[0]), "%s", name);
    }
    if (sMissingHip != NULL && strcmp(name, sMissingHip) == 0)
    {
        return IGCPLAT_NONE;
    }
    if (sGameCubeHip != NULL && strcmp(name, sGameCubeHip) == 0)
    {
        return IGCPLAT_GAMECUBE;
    }
    return IGCPLAT_XBOX;
}

const char* iFileAssetPlatform(const char** longName)
{
    sBootReads++;
    if (longName != NULL)
    {
        *longName = NULL;
    }
    return sBootId;
}

// The Xbox water's answer for the level (iXboxWater.cpp, rw/xboxwater.cpp).
static bool sWaterActive;
static const char* sWaterStatus;

bool iXboxWaterActive()
{
    return sWaterActive;
}

const char* iXboxWaterStatus()
{
    return sWaterStatus;
}

// The guitar's distortion's own answer (iXboxDistort.cpp, rw/xboxdistort.cpp).
static const char* sDistortStatus;

const char* iXboxDistortStatus()
{
    return sDistortStatus;
}

void iWindowSetFrameLock(S32 count)
{
    sLock = count > 1 ? count : 0;
}

S32 iWindowGetFrameLock()
{
    return sLock > 1 ? sLock : 0;
}

// --- checks ------------------------------------------------------------------------

static int sFailures;
static int sChecks;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        sChecks++;                                                                                 \
        if (!(cond))                                                                               \
        {                                                                                          \
            sFailures++;                                                                           \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
        }                                                                                          \
    } while (0)

// xString.cpp's xStrHash: case-folded, times 131.
static U32 Hash(const char* s)
{
    U32 h = 0;
    for (; *s != '\0'; s++)
    {
        unsigned char c = (unsigned char)*s;
        c = (unsigned char)(c - (c & (c >> 1) & 0x20));
        h = h * 131 + c;
    }
    return h;
}

static void TestResolve()
{
    const iExtrasFiles files[] = { iEXTRAS_FILES_XBOX, iEXTRAS_FILES_GAMECUBE };
    const iExtrasRenderer renderers[] = { iEXTRAS_RENDERER_SHADERS, iEXTRAS_RENDERER_FIXED,
                                          iEXTRAS_RENDERER_3DS };
    const U32 scenes[] = { 'BB01', 'BB02', 'TT01', 'B401', 'SC02', 'MNU3', 'ZZ99', 0 };

    for (S32 f = 0; f < iEXTRAS_FEATURE_COUNT; f++)
    {
        for (const iExtrasFiles fl : files)
        {
            for (const iExtrasRenderer r : renderers)
            {
                for (const U32 sc : scenes)
                {
                    const char* why = "unset";
                    // gamecube is always honoured, with no reason.
                    CHECK(iExtrasResolve((iExtrasFeature)f, iEXTRAS_GAMECUBE, fl, r, sc, &why) ==
                          iEXTRAS_GAMECUBE);
                    CHECK(why == NULL);

                    why = "unset";
                    const iExtrasLook got =
                        iExtrasResolve((iExtrasFeature)f, iEXTRAS_XBOX, fl, r, sc, &why);
                    // A decision either honours the setting with no reason or
                    // falls back with one.
                    CHECK((got == iEXTRAS_XBOX) == (why == NULL));

                    if (!iExtrasBuilt((iExtrasFeature)f))
                    {
                        CHECK(got == iEXTRAS_GAMECUBE);
                    }
                    else if (f == iEXTRAS_FRAME_LOCK)
                    {
                        CHECK(got == iEXTRAS_XBOX);
                    }
                    else if (f == iEXTRAS_SPOTLIGHTS)
                    {
                        // TT01's GameCube files have no disco lights; BB02's and
                        // B401's lights are switched on by iExtrasGC.h.
                        const bool gcFiles = fl == iEXTRAS_FILES_GAMECUBE && sc == 'TT01';
                        CHECK(got == (r == iEXTRAS_RENDERER_3DS || gcFiles ? iEXTRAS_GAMECUBE :
                                                                              iEXTRAS_XBOX));
                    }
                    else if (f == iEXTRAS_GLOW)
                    {
                        // Code only; every renderer but the 3DS's.
                        CHECK(got == (r == iEXTRAS_RENDERER_3DS ? iEXTRAS_GAMECUBE : iEXTRAS_XBOX));
                    }
                    else if (f == iEXTRAS_CROWDS)
                    {
                        // BB01's GameCube files have no skinned crowd; BB02's
                        // crowd is given its own (iExtrasGC.h).
                        const bool gcCrowd = fl == iEXTRAS_FILES_GAMECUBE && sc == 'BB01';
                        CHECK(got == (gcCrowd ? iEXTRAS_GAMECUBE : iEXTRAS_XBOX));
                    }
                    else if (f == iEXTRAS_WATER)
                    {
                        // Shaders and the Xbox files, in any scene: the water
                        // bodies are the files'.
                        const bool no = r != iEXTRAS_RENDERER_SHADERS || fl == iEXTRAS_FILES_GAMECUBE;
                        CHECK(got == (no ? iEXTRAS_GAMECUBE : iEXTRAS_XBOX));
                    }
                    else if (f == iEXTRAS_GUITAR_DISTORTION)
                    {
                        // A pixel shader and the Xbox files' swirl map, in any
                        // scene.
                        const bool no = r != iEXTRAS_RENDERER_SHADERS || fl == iEXTRAS_FILES_GAMECUBE;
                        CHECK(got == (no ? iEXTRAS_GAMECUBE : iEXTRAS_XBOX));
                    }
                }
            }
        }
    }

    CHECK(iExtrasBuilt(iEXTRAS_GLOW));
    CHECK(iExtrasBuilt(iEXTRAS_SPOTLIGHTS));
    CHECK(iExtrasBuilt(iEXTRAS_CROWDS));
    CHECK(iExtrasBuilt(iEXTRAS_WATER));
    CHECK(iExtrasBuilt(iEXTRAS_FRAME_LOCK));
    CHECK(iExtrasBuilt(iEXTRAS_GUITAR_DISTORTION));
    CHECK(strcmp(iExtrasKey(iEXTRAS_GUITAR_DISTORTION), "extras.guitar_distortion") == 0);

    // The guitar's distortion's three fallbacks, each with its own sentence.
    const char* gtr = NULL;
    CHECK(iExtrasResolve(iEXTRAS_GUITAR_DISTORTION, iEXTRAS_XBOX, iEXTRAS_FILES_GAMECUBE,
                         iEXTRAS_RENDERER_SHADERS, 'BB02', &gtr) == iEXTRAS_GAMECUBE);
    CHECK(gtr != NULL && strstr(gtr, "GameCube files") != NULL && strstr(gtr, "swirl map") != NULL);
    CHECK(iExtrasResolve(iEXTRAS_GUITAR_DISTORTION, iEXTRAS_XBOX, iEXTRAS_FILES_XBOX,
                         iEXTRAS_RENDERER_FIXED, 'BB02', &gtr) == iEXTRAS_GAMECUBE);
    CHECK(gtr != NULL && strstr(gtr, "fixed path") != NULL);
    CHECK(iExtrasResolve(iEXTRAS_GUITAR_DISTORTION, iEXTRAS_XBOX, iEXTRAS_FILES_XBOX,
                         iEXTRAS_RENDERER_3DS, 'BB02', &gtr) == iEXTRAS_GAMECUBE);
    CHECK(gtr != NULL && strstr(gtr, "3DS") != NULL);
    // Android's and the Switch's GL (never run there) and Vulkan (no shader):
    // none is drawn, and every other feature takes them as it takes shaders.
    CHECK(iExtrasResolve(iEXTRAS_GUITAR_DISTORTION, iEXTRAS_XBOX, iEXTRAS_FILES_XBOX,
                         iEXTRAS_RENDERER_DEVICE_GL, 'BB02', &gtr) == iEXTRAS_GAMECUBE);
    CHECK(gtr != NULL && strstr(gtr, "Android") != NULL && strstr(gtr, "Switch") != NULL);
    CHECK(iExtrasResolve(iEXTRAS_GUITAR_DISTORTION, iEXTRAS_XBOX, iEXTRAS_FILES_XBOX,
                         iEXTRAS_RENDERER_VULKAN, 'BB02', &gtr) == iEXTRAS_GAMECUBE);
    CHECK(gtr != NULL && strstr(gtr, "Vulkan") != NULL);
    for (S32 f = 0; f < iEXTRAS_FEATURE_COUNT; f++)
    {
        if (f == iEXTRAS_GUITAR_DISTORTION)
        {
            continue;
        }
        const char* a = NULL;
        const char* b = NULL;
        const iExtrasLook shaders = iExtrasResolve((iExtrasFeature)f, iEXTRAS_XBOX,
                                                   iEXTRAS_FILES_XBOX, iEXTRAS_RENDERER_SHADERS,
                                                   'BB02', &a);
        CHECK(iExtrasResolve((iExtrasFeature)f, iEXTRAS_XBOX, iEXTRAS_FILES_XBOX,
                             iEXTRAS_RENDERER_DEVICE_GL, 'BB02', &b) == shaders);
        CHECK(a == b);
    }
    CHECK(iExtrasResolve(iEXTRAS_GUITAR_DISTORTION, iEXTRAS_XBOX, iEXTRAS_FILES_XBOX,
                         iEXTRAS_RENDERER_SHADERS, 0, &gtr) == iEXTRAS_XBOX);
    CHECK(gtr == NULL);

    const char* why = NULL;
    iExtrasResolve(iEXTRAS_CROWDS, iEXTRAS_XBOX, iEXTRAS_FILES_GAMECUBE, iEXTRAS_RENDERER_SHADERS,
                   'BB01', &why);
    CHECK(why != NULL && strstr(why, "GameCube files") != NULL);
}

static void TestTables()
{
    char name[64];

    CHECK(iExtrasSpotlightKeptOff('BB02', Hash("SPOTLIGHT_01")));
    CHECK(iExtrasSpotlightKeptOff('BB02', Hash("SPOTLIGHT_02")));
    CHECK(!iExtrasSpotlightKeptOff('BB02', Hash("SPOTLIGHT_03")));
    CHECK(!iExtrasSpotlightKeptOff('BB02', Hash("SPOTLIGHT_04")));
    for (S32 i = 1; i <= 13; i++)
    {
        snprintf(name, sizeof(name), "SPOTLIGHT_%02d", (int)i);
        // The arena's even-numbered lights 2-12; the odd ones over the tables
        // stay on.
        CHECK(iExtrasSpotlightKeptOff('B401', Hash(name)) == (i % 2 == 0 && i <= 12));
        CHECK(!iExtrasSpotlightKeptOff('TT01', Hash(name)));
        CHECK(!iExtrasSpotlightKeptOff('ZZ99', Hash(name)));
    }
    for (S32 i = 1; i <= 9; i++)
    {
        snprintf(name, sizeof(name), "DISCO_BALL_SPOTLIGHT_%02d", (int)i);
        CHECK(iExtrasSpotlightKeptOff('TT01', Hash(name)) == (i <= 8));
        CHECK(!iExtrasSpotlightKeptOff('BB02', Hash(name)));
        snprintf(name, sizeof(name), "DISCO_SPOTLIGHT_%02d", (int)i);
        CHECK(iExtrasSpotlightKeptOff('TT01', Hash(name)) == (i <= 4));
    }
    CHECK(Hash("SPOTLIGHT_02") == 0xEC67F9D9);

    CHECK(iExtrasCrowdModel(Hash("crowd1_bind")));
    CHECK(iExtrasCrowdModel(Hash("crowd2_bind")));
    CHECK(iExtrasCrowdModel(Hash("crowd3_bind")));
    CHECK(Hash("crowd1_bind") == 0xC49163C8);
    // The GameCube files' rigid crowds are not held: they have nothing to hold.
    CHECK(!iExtrasCrowdModel(Hash("crowd01")));
    CHECK(!iExtrasCrowdModel(Hash("crowd02")));
    CHECK(!iExtrasCrowdModel(0));

    // The glow's strength (DEFAULT.XBE 0x1b0eb4-0x1b0f57; the floats at 0x2cc364,
    // 0x28d488, 0x2a7678, 0x28d464, 0x28d48c, 0x28d430 and 1.0 at 0x28cc9c).
    struct Strength
    {
        U32 scene;
        F32 value;
    };
    const Strength strengths[] = {
        { 'JK01', 0.3f },  { 'JK02', 0.3f },  { 'BB02', 0.55f }, { 'TR01', 0.55f },
        { 'TR02', 0.55f }, { 'TR03', 0.55f }, { 'B401', 0.6f },  { 'PT01', 0.6f },
        { 'PT02', 0.6f },  { 'PT03', 0.6f },  { 'SC02', 0.6f },  { 'TT01', 0.7f },
        { 'TT02', 0.7f },  { 'GG02', 0.75f }, { 'BB03', 1.0f },  { 'B201', 1.0f },
        // Everything else, the menus and the scenes either side of each range
        // included.
        { 'BB01', 0.45f }, { 'BB04', 0.45f }, { 'JK03', 0.45f }, { 'TR04', 0.45f },
        { 'TT03', 0.45f }, { 'PT04', 0.45f }, { 'PT00', 0.45f }, { 'SC01', 0.45f },
        { 'GG01', 0.45f }, { 'B101', 0.45f }, { 'B301', 0.45f }, { 'DE01', 0.45f },
        { 'MNU3', 0.45f }, { 'MNUS', 0.45f }, { 'ZZ99', 0.45f }, { 0, 0.45f },
    };
    for (const Strength& st : strengths)
    {
        CHECK(iExtrasGlowStrength(st.scene) == st.value);
    }
}

// One link as the files have it (params, widget and check 0, as in all of these).
static xLinkAsset Link(U16 src, U16 dst, U32 id)
{
    xLinkAsset l;
    memset(&l, 0, sizeof(l));
    l.srcEvent = src;
    l.dstEvent = dst;
    l.dstAssetID = id;
    return l;
}

static bool SameLinks(const xLinkAsset* a, S32 na, const xLinkAsset* b, S32 nb)
{
    return na == nb && (na == 0 || memcmp(a, b, na * sizeof(xLinkAsset)) == 0);
}

// The skinned crowd for the GameCube files' rigid one, and the spotlight links
// of BB02 and B401 turned into the Xbox files'. The links are those of the two
// USA discs' files (bb/bb02.HIP and b4/b401.HIP of each, read with
// tools/modkit.py), with the IDs taken from xStrHash of the names.
static void TestGameCubeFiles()
{
    U32 model = 0;
    U32 list = 0;
    CHECK(iExtrasCrowdSkinned(Hash("crowd01"), &model, &list));
    CHECK(model == Hash("crowd1_bind") && list == Hash("CROWD1_ANIMLIST"));
    CHECK(iExtrasCrowdSkinned(Hash("crowd02"), &model, &list));
    CHECK(model == Hash("crowd2_bind") && list == Hash("CROWD2_ANIMLIST"));
    CHECK(iExtrasCrowdSkinned(Hash("crowd03"), &model, &list));
    CHECK(model == Hash("crowd3_bind") && list == Hash("CROWD3_ANIMLIST"));
    CHECK(iExtrasCrowdModel(model));
    CHECK(!iExtrasCrowdSkinned(Hash("crowd1_bind"), &model, &list));
    CHECK(!iExtrasCrowdSkinned(Hash("crowd04"), &model, &list));
    CHECK(!iExtrasCrowdSkinned(0, &model, &list));

    // The objects whose links change.
    U32 ids[8];
    U32 types[8];
    CHECK(iExtrasSpotlightLinkObjects('BB02', ids, types, 8) == 1);
    CHECK(ids[0] == Hash("SPOTLIGHT_DISP_GC_ONLY") && types[0] == 'DPAT');
    CHECK(iExtrasSpotlightLinkObjects('B401', ids, types, 8) == 2);
    CHECK(ids[0] == Hash("INTRO_CSNMANAGER") && types[0] == 'CSNM');
    CHECK(ids[1] == Hash("SCENE_RESTART_DISP") && types[1] == 'DPAT');
    CHECK(iExtrasSpotlightLinkObjects('B401', ids, types, 1) == 1);
    CHECK(iExtrasSpotlightLinkObjects('TT01', ids, types, 8) == 0);
    CHECK(iExtrasSpotlightLinkObjects('ZZ99', ids, types, 8) == 0);

    const U32 spot01 = Hash("SPOTLIGHT_01");
    const U32 spot02 = Hash("SPOTLIGHT_02");
    const U32 group = Hash("SPOTLIGHT_GROUP");
    const U32 script = Hash("SPOTLIGHT_SCRIPT");
    const U32 world = Hash("ENV_THEWORLD");
    const U32 ambient = Hash("AMBIENT_SFX");
    const U32 tables = Hash("TABLE_SPOTLIGHTS");
    const U32 music = Hash("MUSIC_ROCKSONG_DISP");
    xLinkAsset out[16];

    // BB02: the GameCube-only dispatcher's two Off links are dropped.
    const U32 bb02 = Hash("SPOTLIGHT_DISP_GC_ONLY");
    const xLinkAsset bbGC[] = { Link(eEventSceneBegin, eEventOff, spot01),
                                Link(eEventSceneBegin, eEventOff, spot02) };
    CHECK(iExtrasSpotlightLinks('BB02', bb02, bbGC, 2, out, 16) == 0);
    // Not in B401, where SPOTLIGHT_02 is too.
    CHECK(iExtrasSpotlightLinks('B401', bb02, bbGC, 2, out, 16) == 2);
    CHECK(SameLinks(out, 2, bbGC, 2));

    // B401's intro: GameCube 8 links, Xbox 9, in this order.
    const U32 intro = Hash("INTRO_CSNMANAGER");
    xLinkAsset introGC[] = {
        Link(eEventPlay, eEventOn, world),         Link(eEventPlay, eEventOff, world),
        Link(eEventDone, eEventOn, world),         Link(eEventDone, eEventOff, world),
        Link(eEventDone, eEventPlay, ambient),     Link(eEventDone, eEventOn, tables),
        Link(eEventDone, eEventOff, group),        Link(eEventDone, eEventDisable, music),
    };
    // The ENV_THEWORLD links carry a widget on the discs: kept as they are.
    introGC[0].paramWidgetAssetID = 0x68EB8D0B;
    introGC[1].paramWidgetAssetID = 0x339309B6;
    introGC[2].paramWidgetAssetID = 0x339309B6;
    introGC[3].paramWidgetAssetID = 0x68EB8D0B;
    xLinkAsset introXbox[9];
    memcpy(introXbox, introGC, 6 * sizeof(xLinkAsset));
    introXbox[6] = Link(eEventDone, eEventOn, group);
    introXbox[7] = Link(eEventDone, eEventRun, script);
    introXbox[8] = introGC[7];
    CHECK(iExtrasSpotlightLinks('B401', intro, introGC, 8, out, 16) == 9);
    CHECK(SameLinks(out, 9, introXbox, 9));
    // Room for 8 only: refused, not cut short.
    CHECK(iExtrasSpotlightLinks('B401', intro, introGC, 8, out, 8) == -1);

    // B401's restart: GameCube 7 links, Xbox 8.
    const U32 restart = Hash("SCENE_RESTART_DISP");
    xLinkAsset restartGC[7];
    for (S32 i = 0; i < 6; i++)
    {
        restartGC[i] = introGC[i];
        restartGC[i].srcEvent = eEventSceneReset;
    }
    restartGC[6] = Link(eEventSceneReset, eEventOff, group);
    xLinkAsset restartXbox[8];
    memcpy(restartXbox, restartGC, 6 * sizeof(xLinkAsset));
    restartXbox[6] = Link(eEventSceneReset, eEventOn, group);
    restartXbox[7] = Link(eEventSceneReset, eEventRun, script);
    CHECK(iExtrasSpotlightLinks('B401', restart, restartGC, 7, out, 16) == 8);
    CHECK(SameLinks(out, 8, restartXbox, 8));

    // Links that are not the disc's: the Off link missing (the Xbox files'
    // own, say), there twice, or with a parameter. Nothing is changed.
    CHECK(iExtrasSpotlightLinks('B401', restart, restartXbox, 8, out, 16) == -1);
    CHECK(iExtrasSpotlightLinks('B401', intro, introXbox, 9, out, 16) == -1);
    xLinkAsset twice[8];
    memcpy(twice, restartGC, 7 * sizeof(xLinkAsset));
    twice[7] = restartGC[6];
    CHECK(iExtrasSpotlightLinks('B401', restart, twice, 8, out, 16) == -1);
    xLinkAsset param[7];
    memcpy(param, restartGC, sizeof(param));
    param[6].param[0] = 1.0f;
    CHECK(iExtrasSpotlightLinks('B401', restart, param, 7, out, 16) == -1);
    CHECK(iExtrasSpotlightLinks('BB02', bb02, bbGC, 1, out, 16) == -1);
    CHECK(iExtrasSpotlightLinks('BB02', bb02, NULL, 0, out, 16) == -1);
    CHECK(iExtrasSpotlightLinks('BB02', bb02, bbGC, -1, out, 16) == -1);
    // An object the table does not name is copied as it is.
    CHECK(iExtrasSpotlightLinks('B401', Hash("TABLE_SPOTLIGHTS"), introGC, 8, out, 16) == 8);
    CHECK(SameLinks(out, 8, introGC, 8));
    CHECK(iExtrasSpotlightLinks('B401', Hash("TABLE_SPOTLIGHTS"), NULL, 0, out, 16) == 0);
}

static bool ReadFile(const char* path, char* out, size_t outSize)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL)
    {
        return false;
    }
    size_t n = fread(out, 1, outSize - 1, f);
    out[n] = '\0';
    fclose(f);
    return true;
}

static int CountLines(const char* text, const char* line)
{
    int count = 0;
    const char* p = text;
    const size_t n = strlen(line);
    while ((p = strstr(p, line)) != NULL)
    {
        const bool start = p == text || p[-1] == '\n';
        const bool end = p[n] == '\r' || p[n] == '\n' || p[n] == '\0';
        count += start && end ? 1 : 0;
        p += n;
    }
    return count;
}

static bool Asked(const char* name)
{
    for (int i = 0; i < sAskedCount; i++)
    {
        if (strcmp(sAsked[i], name) == 0)
        {
            return true;
        }
    }
    return false;
}

// Whose files a level is, and the water's own answer. Run with the settings
// TestConfig has left: every key but the water's xbox, the frame lock xbox.
static void TestFiles()
{
    char why[256];
    iConfigSet("extras.crowds", "xbox");
    iConfigSet("extras.spotlights", "xbox");
    iConfigSet("extras.water", "xbox");

    // The scene's HIP by the name xSTPreLoadScene reads it by.
    sAskedCount = 0;
    sGameCubeHip = "BB/BB02.HIP";
    iExtrasSceneEnter('BB02');
    CHECK(Asked("BB/BB02.HIP"));
    CHECK(!Asked("BB02.HIP"));
    CHECK(iExtrasLevelFiles() == iEXTRAS_FILES_GAMECUBE);
    CHECK(iExtrasInForce(iEXTRAS_CROWDS) == iEXTRAS_XBOX);
    CHECK(iExtrasInForce(iEXTRAS_SPOTLIGHTS) == iEXTRAS_XBOX);
    CHECK(iExtrasInForce(iEXTRAS_WATER) == iEXTRAS_GAMECUBE);
    CHECK(iExtrasInForce(iEXTRAS_GLOW) == iEXTRAS_XBOX);
    CHECK(!iExtrasStatus(iEXTRAS_CROWDS, why, sizeof(why)));
    CHECK(!iExtrasStatus(iEXTRAS_SPOTLIGHTS, why, sizeof(why)));
    CHECK(iExtrasStatus(iEXTRAS_WATER, why, sizeof(why)) && strstr(why, "level's files") != NULL);
    // The level's files turning out not to allow one after all
    // (iExtrasGC.h): the GameCube version, the sentence in the overlay.
    iExtrasNote(iEXTRAS_CROWDS, "(%d given the skinned crowd)", 11);
    iExtrasFallBack(iEXTRAS_SPOTLIGHTS, "This level's GameCube files are not the disc's.");
    CHECK(iExtrasInForce(iEXTRAS_SPOTLIGHTS) == iEXTRAS_GAMECUBE);
    CHECK(iExtrasInForce(iEXTRAS_CROWDS) == iEXTRAS_XBOX);
    CHECK(iExtrasStatus(iEXTRAS_SPOTLIGHTS, why, sizeof(why)) && strstr(why, "not the disc") != NULL);
    CHECK(!iExtrasStatus(iEXTRAS_CROWDS, why, sizeof(why)));
    // A change of the setting to gamecube is the next level's: no reason now.
    iConfigSet("extras.spotlights", "gamecube");
    CHECK(!iExtrasStatus(iEXTRAS_SPOTLIGHTS, why, sizeof(why)));
    iConfigSet("extras.spotlights", "xbox");
    CHECK(iExtrasStatus(iEXTRAS_SPOTLIGHTS, why, sizeof(why)));
    // Even with the water's own answer there, the files' reason is the one given.
    sWaterActive = false;
    sWaterStatus = "Now: GameCube water in BB02 -- the Vulkan renderer has no water shaders.";
    CHECK(iExtrasStatus(iEXTRAS_WATER, why, sizeof(why)) && strstr(why, "level's files") != NULL);
    iExtrasSceneReady('BB02', 1);

    // The next level forgets the fallback.
    iExtrasSceneEnter('BB02');
    CHECK(iExtrasInForce(iEXTRAS_SPOTLIGHTS) == iEXTRAS_XBOX);
    CHECK(!iExtrasStatus(iEXTRAS_SPOTLIGHTS, why, sizeof(why)));
    iExtrasSceneReady('BB02', 1);
    // Nor is a fallback taken from a level loaded with gamecube, which keeps
    // its own decision.
    iConfigSet("extras.spotlights", "gamecube");
    iExtrasSceneEnter('BB02');
    iExtrasFallBack(iEXTRAS_SPOTLIGHTS, "not used");
    CHECK(iExtrasInForce(iEXTRAS_SPOTLIGHTS) == iEXTRAS_GAMECUBE);
    iConfigSet("extras.spotlights", "xbox");
    CHECK(!iExtrasStatus(iEXTRAS_SPOTLIGHTS, why, sizeof(why)));
    // BB01's GameCube files have no skinned crowd.
    sGameCubeHip = "BB/BB01.HIP";
    iExtrasSceneEnter('BB01');
    CHECK(iExtrasInForce(iEXTRAS_CROWDS) == iEXTRAS_GAMECUBE);
    CHECK(iExtrasStatus(iEXTRAS_CROWDS, why, sizeof(why)) && strstr(why, "no moving crowd") != NULL);
    // B401's lights are switched on with them.
    sGameCubeHip = "B4/B401.HIP";
    iExtrasSceneEnter('B401');
    CHECK(iExtrasInForce(iEXTRAS_SPOTLIGHTS) == iEXTRAS_XBOX);
    CHECK(!iExtrasStatus(iEXTRAS_SPOTLIGHTS, why, sizeof(why)));

    // Where the folder form is missing, the flat name xSTPreLoadScene tries next.
    sAskedCount = 0;
    sMissingHip = "TT/TT01.HIP";
    sGameCubeHip = "TT01.HIP";
    iExtrasSceneEnter('TT01');
    CHECK(Asked("TT/TT01.HIP") && Asked("TT01.HIP"));
    CHECK(iExtrasInForce(iEXTRAS_SPOTLIGHTS) == iEXTRAS_GAMECUBE);
    CHECK(iExtrasStatus(iEXTRAS_SPOTLIGHTS, why, sizeof(why)) && strstr(why, "disco") != NULL);
    sMissingHip = NULL;
    sGameCubeHip = NULL;

    // The Xbox files: the water's own answer is the reason when the Xbox water
    // did not run in a level with water bodies.
    sWaterActive = false;
    sWaterStatus = "Now: GameCube water in SC02 -- the Vulkan renderer has no water shaders.";
    iExtrasSceneEnter('SC02');
    CHECK(iExtrasInForce(iEXTRAS_WATER) == iEXTRAS_XBOX);
    CHECK(iExtrasInForce(iEXTRAS_CROWDS) == iEXTRAS_XBOX);
    CHECK(iExtrasStatus(iEXTRAS_WATER, why, sizeof(why)) && strstr(why, "Vulkan") != NULL);
    iExtrasSceneReady('SC02', 2);
    // Not once it runs, nor in a level without water bodies.
    sWaterActive = true;
    sWaterStatus = "Now: Xbox water in SC02 (2 water areas).";
    CHECK(!iExtrasStatus(iEXTRAS_WATER, why, sizeof(why)));
    sWaterActive = false;
    sWaterStatus = NULL;
    CHECK(!iExtrasStatus(iEXTRAS_WATER, why, sizeof(why)));
    // A level loaded with gamecube gives no reason, its change to xbox
    // either: that is the "next level" tag.
    iConfigSet("extras.water", "gamecube");
    sWaterStatus = "Now: GameCube water in SC02 -- extras.water is gamecube.";
    iExtrasSceneEnter('SC02');
    CHECK(!iExtrasStatus(iEXTRAS_WATER, why, sizeof(why)));
    iConfigSet("extras.water", "xbox");
    CHECK(iExtrasAtLevelLoad(iEXTRAS_WATER) == iEXTRAS_GAMECUBE);
    CHECK(!iExtrasStatus(iEXTRAS_WATER, why, sizeof(why)));
    sWaterStatus = NULL;

    iConfigSet("extras.crowds", "xbox");
    iConfigSet("extras.water", "gamecube");
}

static void TestConfig(const char* scratch)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/config.ini", scratch);
    FILE* f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f == NULL)
    {
        return;
    }
    // An older file: [video] water, as the first builds with the water named it, and no
    // [extras] water.
    fputs("[video]\nwater = gamecube\n[extras]\ncrowds = gamecube\nframe_lock = gamecube\n", f);
    fclose(f);

    static char env[1100];
    snprintf(env, sizeof(env), "TSSM_CONFIG=%s", path);
    putenv(env);
    iConfigLoad();

    static char text[65536];
    CHECK(ReadFile(path, text, sizeof(text)));
    // The missing keys were appended at their defaults; water was not, since
    // the old line answers for it.
    CHECK(CountLines(text, "glow = xbox") == 1);
    CHECK(CountLines(text, "spotlights = xbox") == 1);
    CHECK(strstr(text, "water = xbox") == NULL);
    CHECK(strcmp(iConfigGetString("extras.water", "?"), "gamecube") == 0);
    CHECK(iExtrasSetting(iEXTRAS_WATER) == iEXTRAS_GAMECUBE);

    // Before the first level, the files are boot.HIP's: not kept while it
    // cannot be read (before iFileInit), kept once it has answered.
    char why[256];
    iConfigSet("extras.water", "xbox");
    sBootId = NULL;
    CHECK(iExtrasInForce(iEXTRAS_WATER) == iEXTRAS_XBOX);

    // The guitar's distortion: appended at its default, in force at once, and
    // the pass's own reason (no swirl map, say) reaches the row while the
    // setting is xbox.
    CHECK(CountLines(text, "guitar_distortion = xbox") == 1);
    CHECK(iExtrasSetting(iEXTRAS_GUITAR_DISTORTION) == iEXTRAS_XBOX);
    CHECK(iExtrasInForce(iEXTRAS_GUITAR_DISTORTION) == iEXTRAS_XBOX);
    CHECK(!iExtrasStatus(iEXTRAS_GUITAR_DISTORTION, why, sizeof(why)));
    sDistortStatus = "The game's files have no swirl map: none is drawn.";
    CHECK(iExtrasStatus(iEXTRAS_GUITAR_DISTORTION, why, sizeof(why)) &&
          strstr(why, "no swirl map") != NULL);
    iConfigSet("extras.guitar_distortion", "gamecube");
    CHECK(!iExtrasStatus(iEXTRAS_GUITAR_DISTORTION, why, sizeof(why)));
    CHECK(iExtrasInForce(iEXTRAS_GUITAR_DISTORTION) == iEXTRAS_XBOX); // until the overlay applies
    iExtrasApply();
    CHECK(iExtrasInForce(iEXTRAS_GUITAR_DISTORTION) == iEXTRAS_GAMECUBE);
    sDistortStatus = NULL;
    iConfigSet("extras.guitar_distortion", "xbox");
    iExtrasApply();
    CHECK(iExtrasInForce(iEXTRAS_GUITAR_DISTORTION) == iEXTRAS_XBOX);
    sBootId = "GC";
    CHECK(iExtrasInForce(iEXTRAS_WATER) == iEXTRAS_GAMECUBE);
    CHECK(iExtrasStatus(iEXTRAS_WATER, why, sizeof(why)) && strstr(why, "game's files") != NULL);
    const int reads = sBootReads;
    sBootId = "BX";
    CHECK(iExtrasInForce(iEXTRAS_WATER) == iEXTRAS_GAMECUBE);
    CHECK(sBootReads == reads);
    // The crowds and the spotlights name levels; before one, nothing to say.
    CHECK(iExtrasInForce(iEXTRAS_CROWDS) == iEXTRAS_GAMECUBE); // the file's own value
    CHECK(iExtrasInForce(iEXTRAS_SPOTLIGHTS) == iEXTRAS_XBOX);
    iConfigSet("extras.water", "gamecube");

    // The game's files are GameCube's from here on: no swirl map, whatever a
    // level's own files are (every level below is from Xbox files unless
    // named), and no "next level" difference.
    iExtrasApply();
    CHECK(iExtrasInForce(iEXTRAS_GUITAR_DISTORTION) == iEXTRAS_GAMECUBE);
    CHECK(iExtrasStatus(iEXTRAS_GUITAR_DISTORTION, why, sizeof(why)) &&
          strstr(why, "game's files are GameCube files") != NULL);
    iExtrasSceneEnter('BB02');
    CHECK(iExtrasLevelFiles() == iEXTRAS_FILES_XBOX);
    CHECK(iExtrasInForce(iEXTRAS_GUITAR_DISTORTION) == iEXTRAS_GAMECUBE);
    CHECK(iExtrasAtLevelLoad(iEXTRAS_GUITAR_DISTORTION) == iEXTRAS_XBOX);
    CHECK(iExtrasStatus(iEXTRAS_GUITAR_DISTORTION, why, sizeof(why)) &&
          strstr(why, "swirl map") != NULL);
    iExtrasSceneReady('BB02', 1);

    // A level with crowds = gamecube.
    iExtrasSceneEnter('BB02');
    CHECK(iExtrasInForce(iEXTRAS_CROWDS) == iEXTRAS_GAMECUBE);
    CHECK(iExtrasAtLevelLoad(iEXTRAS_CROWDS) == iEXTRAS_GAMECUBE);
    CHECK(iExtrasCrowdHeld(0xC49163C8));
    CHECK(iExtrasCrowdHeld(0xBB99FAAE));
    CHECK(!iExtrasCrowdHeld(0x51947E72));
    iExtrasSceneReady('BB02', 1);

    // A change waits for the next level ("next level" in the overlay).
    iConfigSet("extras.crowds", "xbox");
    CHECK(iExtrasSetting(iEXTRAS_CROWDS) == iEXTRAS_XBOX);
    CHECK(iExtrasAtLevelLoad(iEXTRAS_CROWDS) == iEXTRAS_GAMECUBE);
    CHECK(iExtrasInForce(iEXTRAS_CROWDS) == iEXTRAS_GAMECUBE);
    CHECK(!iExtrasStatus(iEXTRAS_CROWDS, why, sizeof(why)));
    iExtrasSceneEnter('BB01');
    CHECK(iExtrasInForce(iEXTRAS_CROWDS) == iEXTRAS_XBOX);
    CHECK(!iExtrasCrowdHeld(0xC49163C8));

    // The glow follows its setting at once, between levels too (the overlay
    // calls iExtrasApply after a change).
    CHECK(iExtrasInForce(iEXTRAS_GLOW) == iEXTRAS_XBOX);
    CHECK(!iExtrasStatus(iEXTRAS_GLOW, why, sizeof(why)));
    iConfigSet("extras.glow", "gamecube");
    iExtrasApply();
    CHECK(iExtrasInForce(iEXTRAS_GLOW) == iEXTRAS_GAMECUBE);
    iConfigSet("extras.glow", "xbox");
    iExtrasApply();
    CHECK(iExtrasInForce(iEXTRAS_GLOW) == iEXTRAS_XBOX);
    // The water: the Xbox files on shaders give it; the GameCube files and
    // D3D9's fixed path give the reason.
    const char* waterWhy = "unset";
    CHECK(iExtrasResolve(iEXTRAS_WATER, iEXTRAS_XBOX, iEXTRAS_FILES_XBOX, iEXTRAS_RENDERER_SHADERS,
                         'SC02', &waterWhy) == iEXTRAS_XBOX);
    CHECK(waterWhy == NULL);
    CHECK(iExtrasResolve(iEXTRAS_WATER, iEXTRAS_XBOX, iEXTRAS_FILES_GAMECUBE,
                         iEXTRAS_RENDERER_SHADERS, 'SC02', &waterWhy) == iEXTRAS_GAMECUBE);
    CHECK(waterWhy != NULL && strstr(waterWhy, "GameCube files") != NULL);
    CHECK(iExtrasResolve(iEXTRAS_WATER, iEXTRAS_XBOX, iEXTRAS_FILES_XBOX, iEXTRAS_RENDERER_FIXED,
                         'B301', &waterWhy) == iEXTRAS_GAMECUBE);
    CHECK(waterWhy != NULL && strstr(waterWhy, "fixed path") != NULL);

    // The frame lock: the count reaches the pacer only with gamecube, every
    // frame, and a change made through the overlay applies at once. (The
    // level just entered was entered with frame_lock = gamecube.)
    iExtrasSetRetraceCount(2);
    CHECK(sLock == 2);
    CHECK(iExtrasInForce(iEXTRAS_FRAME_LOCK) == iEXTRAS_GAMECUBE);
    iExtrasSetRetraceCount(1);
    CHECK(sLock == 0);
    iExtrasSetRetraceCount(2);
    iConfigSet("extras.frame_lock", "xbox");
    iExtrasApply(); // the overlay, in a menu after a held level
    CHECK(sLock == 0);
    CHECK(iExtrasInForce(iEXTRAS_FRAME_LOCK) == iEXTRAS_XBOX);
    iConfigSet("extras.frame_lock", "gamecube");
    iExtrasApply(); // the count of the last level is still held
    CHECK(sLock == 2);
    iConfigSet("extras.frame_lock", "xbox");
    iExtrasApply();
    iExtrasSetRetraceCount(2);
    CHECK(sLock == 0);
    // A new level reads it too.
    iConfigSet("extras.frame_lock", "gamecube");
    iExtrasSceneEnter('BB01');
    iExtrasSetRetraceCount(2);
    CHECK(sLock == 2);
    iConfigSet("extras.frame_lock", "xbox");
    iExtrasSceneEnter('BB01');
    iExtrasSetRetraceCount(2);
    CHECK(sLock == 0);

    // The car under the hold: one step per 1/60 s, whatever ceilf made of the
    // frame's jitter; without the hold, the game's own count.
    sLock = 2;
    CHECK(iExtrasHeldCarSteps(2.0f * 1001.0f / 60000.0f, 3) == 2);
    CHECK(iExtrasHeldCarSteps(2.0f / 60.0f - 0.0002f, 2) == 2);
    CHECK(iExtrasHeldCarSteps(3.0f * 1001.0f / 60000.0f, 4) == 3);
    CHECK(iExtrasHeldCarSteps(0.016f, 1) == 1);
    sLock = 0;
    CHECK(iExtrasHeldCarSteps(2.0f * 1001.0f / 60000.0f, 3) == 3);
    CHECK(iExtrasHeldCarSteps(0.008f, 1) == 1);

    TestFiles();

    // Changing the water from the game writes the new name and takes the old
    // line out.
    iConfigSet("extras.water", "xbox");
    CHECK(iConfigSave());
    CHECK(ReadFile(path, text, sizeof(text)));
    CHECK(strstr(text, "water = gamecube") == NULL);
    CHECK(CountLines(text, "water = xbox") == 1);
    const char* extras = strstr(text, "[extras]");
    const char* water = strstr(text, "water = xbox");
    CHECK(extras != NULL && water != NULL && water > extras);
    CHECK(strcmp(iConfigGetString("extras.water", "?"), "xbox") == 0);

    // The renderer, as far as the decision is concerned.
    sBackend = iSCREENBACKEND_D3D9;
    sPipeline = iSCREENPIPE_FIXED;
    iExtrasSceneEnter('BB02');
    CHECK(iExtrasInForce(iEXTRAS_CROWDS) == iEXTRAS_XBOX);
    // D3D9's fixed path draws the Xbox glow's taps one at a time, and has a
    // stencil buffer for the light volumes.
    CHECK(iExtrasInForce(iEXTRAS_GLOW) == iEXTRAS_XBOX);
    CHECK(iExtrasInForce(iEXTRAS_SPOTLIGHTS) == iEXTRAS_XBOX);

    // A value that is neither xbox nor gamecube is the table's default (xbox
    // here, gamecube on the 3DS), whichever key, and the overlay gets the text;
    // the case of a good one does not matter.
    sBackend = iSCREENBACKEND_D3D11;
    sPipeline = iSCREENPIPE_AUTO;
    const char* const bad[] = { "on", "1", "30", "ps2", "gc" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        for (S32 k = 0; k < iEXTRAS_FEATURE_COUNT; k++)
        {
            const iExtrasFeature f = (iExtrasFeature)k;
            const iConfigSetting* row = iConfigTableFind(iExtrasKey(f));
            CHECK(row != NULL);
            iConfigSet(iExtrasKey(f), bad[i]);
            const iExtrasLook want =
                row != NULL && strcmp(row->value, "gamecube") == 0 ? iEXTRAS_GAMECUBE : iEXTRAS_XBOX;
            CHECK(iExtrasSetting(f) == want);
            CHECK(want == iEXTRAS_XBOX);
            CHECK(iExtrasBadValue(f) != NULL && strcmp(iExtrasBadValue(f), bad[i]) == 0);
        }
    }
    iConfigSet("extras.crowds", "GameCube");
    CHECK(iExtrasSetting(iEXTRAS_CROWDS) == iEXTRAS_GAMECUBE);
    CHECK(iExtrasBadValue(iEXTRAS_CROWDS) == NULL);
    iConfigSet("extras.crowds", "XBOX");
    CHECK(iExtrasSetting(iEXTRAS_CROWDS) == iEXTRAS_XBOX);
    CHECK(iExtrasBadValue(iEXTRAS_CROWDS) == NULL);
    // A level loaded with a bad frame_lock runs at the cap, as with xbox.
    iConfigSet("extras.frame_lock", "30");
    iExtrasSceneEnter('BB01');
    iExtrasSetRetraceCount(2);
    CHECK(sLock == 0);
    CHECK(iExtrasInForce(iEXTRAS_FRAME_LOCK) == iEXTRAS_XBOX);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("usage: tssm_extrastest <scratch folder>\n");
        return 2;
    }

#ifdef _WIN32
    _mkdir(argv[1]);
#else
    mkdir(argv[1], 0777);
#endif

    TestResolve();
    TestTables();
    TestGameCubeFiles();
    TestConfig(argv[1]);

    if (sFailures != 0)
    {
        printf("tssm_extrastest: %d of %d checks failed\n", sFailures, sChecks);
        return 1;
    }
    printf("tssm_extrastest: all %d checks passed\n", sChecks);
    return 0;
}
