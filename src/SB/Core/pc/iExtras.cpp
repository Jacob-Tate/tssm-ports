// The Xbox release's features beside the GameCube release's; see iExtras.h.

#include "iExtras.h"

#include "iConfig.h"
#include "iConfigTable.h"
#include "iFile.h"
#include "iHost.h"
#include "iScreen.h"
#include "iWindow.h"
#include "iXboxWaterDraw.h"
#include "xEvent.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace
{
    const char* const kKeys[iEXTRAS_FEATURE_COUNT] = {
        "extras.glow", "extras.spotlights", "extras.crowds", "extras.water", "extras.frame_lock",
    };

    const char* const kNames[iEXTRAS_FEATURE_COUNT] = {
        "glow", "spotlights", "crowds", "water", "frame lock",
    };

    // Which Xbox versions this build has.
    const bool kBuilt[iEXTRAS_FEATURE_COUNT] = {
        true, // glow
        true, // spotlights
        true, // crowds
        true, // water (iXboxWater.h)
        true, // frame lock
    };

    U32 sSceneID;
    bool sHaveScene;
    iExtrasFiles sSceneFiles;

    // boot.HIP's release, the answer before the first level: asked once.
    iExtrasFiles sBootFiles;
    bool sBootFilesKnown;
    iExtrasLook sInForce[iEXTRAS_FEATURE_COUNT];
    iExtrasLook sAtLoad[iEXTRAS_FEATURE_COUNT];
    const char* sWhy[iEXTRAS_FEATURE_COUNT];
    // A level's own fallback after the decision (iExtrasFallBack), and its note
    // for the level's line (iExtrasNote).
    const char* sLateWhy[iEXTRAS_FEATURE_COUNT];
    char sNote[iEXTRAS_FEATURE_COUNT][64];
    S32 sCrowdsHeld;

    S32 sRetraceCount = 1;
    bool sFrameLockOn;

    // Whether the log has said that a key's value is not xbox or gamecube.
    bool sToldBad[iEXTRAS_FEATURE_COUNT];

    // The glow follows its setting at once (the overlay calls iExtrasApply),
    // and is asked for every frame, so it is kept here rather than read.
    iExtrasLook sGlow;
    bool sGlowKnown;

    // The four characters of a scene ID ('BB02' -> "BB02").
    void SceneName(U32 id, char out[5])
    {
        out[0] = (char)(id >> 24);
        out[1] = (char)(id >> 16);
        out[2] = (char)(id >> 8);
        out[3] = (char)id;
        out[4] = '\0';
        for (S32 i = 0; i < 4; i++)
        {
            if (out[i] < ' ' || out[i] > '~')
            {
                out[i] = '?';
            }
        }
    }

    // Whose files the level's package is: the PLAT chunk of the scene's HIP
    // (iFilePackagePlatformOf), read by the name xSTPreLoadScene opens it by
    // ("BB/BB02.HIP", else "BB02.HIP"), so a mod's copy answers when a mod
    // replaces it. A package with no PLAT is the Xbox's, as iFile.cpp reads it.
    // Asked once a level, in iExtrasSceneEnter.
    iExtrasFiles PackageFiles(U32 sceneID)
    {
        char name[5];
        SceneName(sceneID, name);

        char path[16];
        snprintf(path, sizeof(path), "%c%c/%s.HIP", name[0], name[1], name);
        iGCPlatform platform = iFilePackagePlatformOf(path);
        if (platform == IGCPLAT_NONE)
        {
            snprintf(path, sizeof(path), "%s.HIP", name);
            platform = iFilePackagePlatformOf(path);
        }
        return platform == IGCPLAT_GAMECUBE ? iEXTRAS_FILES_GAMECUBE : iEXTRAS_FILES_XBOX;
    }

    // Whose files the current level is, or before the first level, boot.HIP's
    // (iFileAssetPlatform): the overlay's answer in the menus. Kept once boot.HIP
    // has answered (not before iFileInit, when it cannot be read).
    iExtrasFiles SceneFiles()
    {
        if (sHaveScene)
        {
            return sSceneFiles;
        }
        if (!sBootFilesKnown)
        {
            const char* id = iFileAssetPlatform(NULL);
            sBootFiles = id != NULL && strcmp(id, "GC") == 0 ? iEXTRAS_FILES_GAMECUBE :
                                                               iEXTRAS_FILES_XBOX;
            sBootFilesKnown = id != NULL;
        }
        return sBootFiles;
    }

    // The water's own answer for the current level when [extras] let it try
    // and it did not run (iXboxWater.h: plat.HIP's bump frames, the renderer's
    // shaders, the targets, the body objects): its sentence, else NULL.
    // iXboxWaterStatus() is set only in a level whose files have water bodies.
    const char* WaterFellBack()
    {
        if (!sHaveScene || sInForce[iEXTRAS_WATER] != iEXTRAS_XBOX || iXboxWaterActive())
        {
            return NULL;
        }
        return iXboxWaterStatus();
    }

    // A reason of the level's own for a feature it loaded with as xbox: the
    // water's (WaterFellBack), or its files' (iExtrasFallBack).
    const char* LevelFellBack(iExtrasFeature feature)
    {
        if (!sHaveScene || sAtLoad[feature] != iEXTRAS_XBOX)
        {
            return NULL;
        }
        return feature == iEXTRAS_WATER ? WaterFellBack() : sLateWhy[feature];
    }

    iExtrasRenderer CurrentRenderer()
    {
#ifdef __3DS__
        return iEXTRAS_RENDERER_3DS;
#else
        if (iScreenGetBackend() == iSCREENBACKEND_D3D9 && iScreenGetPipeline() == iSCREENPIPE_FIXED)
        {
            return iEXTRAS_RENDERER_FIXED;
        }
        return iEXTRAS_RENDERER_SHADERS;
#endif
    }

    void ReadGlow()
    {
        const char* why;
        sGlow = iExtrasResolve(iEXTRAS_GLOW, iExtrasSetting(iEXTRAS_GLOW), SceneFiles(),
                               CurrentRenderer(), sHaveScene ? sSceneID : 0, &why);
        sGlowKnown = true;
    }

    bool ReadFrameLock()
    {
        return iExtrasSetting(iEXTRAS_FRAME_LOCK) == iEXTRAS_GAMECUBE;
    }

    void PushFrameLock()
    {
        iWindowSetFrameLock(sFrameLockOn ? sRetraceCount : 0);
    }

    // printf onto the end of `buf`, which never runs past `size`.
    void Append(char* buf, size_t size, size_t* used, const char* fmt, ...)
    {
        if (*used + 1 >= size)
        {
            return;
        }
        va_list args;
        va_start(args, fmt);
        const int wrote = vsnprintf(buf + *used, size - *used, fmt, args);
        va_end(args);
        if (wrote > 0)
        {
            *used += (size_t)wrote < size - *used ? (size_t)wrote : size - *used - 1;
        }
    }

    // The keep-off table (iExtrasSpotlightKeptOff). xStrHash of each name.
    struct KeptOff
    {
        U32 scene;
        U32 id;
    };

    const KeptOff kKeptOff[] = {
        // BB02, No Cheese!: SPOTLIGHT_01, _02.
        { 'BB02', 0xEC67F9D8 },
        { 'BB02', 0xEC67F9D9 },
        // B401, the arena of Turn the Tables on Plankton: SPOTLIGHT_02, _04 ... _12. The
        // odd-numbered ones over the tables stay on, as in both releases.
        { 'B401', 0xEC67F9D9 },
        { 'B401', 0xEC67F9DB },
        { 'B401', 0xEC67F9DD },
        { 'B401', 0xEC67F9DF },
        { 'B401', 0xEC67FA5A },
        { 'B401', 0xEC67FA5C },
        // TT01, Bubble Blowing Baby Hunt: DISCO_BALL_SPOTLIGHT_01 ... _08 and
        // DISCO_SPOTLIGHT_01 ... _04, which its GameCube files do not have.
        { 'TT01', 0x715F7AAB },
        { 'TT01', 0x715F7AAC },
        { 'TT01', 0x715F7AAD },
        { 'TT01', 0x715F7AAE },
        { 'TT01', 0x715F7AAF },
        { 'TT01', 0x715F7AB0 },
        { 'TT01', 0x715F7AB1 },
        { 'TT01', 0x715F7AB2 },
        { 'TT01', 0x7F57F415 },
        { 'TT01', 0x7F57F416 },
        { 'TT01', 0x7F57F417 },
        { 'TT01', 0x7F57F418 },
    };
} // namespace

const char* iExtrasKey(iExtrasFeature feature)
{
    return feature >= 0 && feature < iEXTRAS_FEATURE_COUNT ? kKeys[feature] : "";
}

const char* iExtrasName(iExtrasFeature feature)
{
    return feature >= 0 && feature < iEXTRAS_FEATURE_COUNT ? kNames[feature] : "";
}

bool iExtrasBuilt(iExtrasFeature feature)
{
    return feature >= 0 && feature < iEXTRAS_FEATURE_COUNT && kBuilt[feature];
}

iExtrasLook iExtrasSetting(iExtrasFeature feature)
{
    const char* key = iExtrasKey(feature);
    const char* value = iConfigGetString(key, "xbox");
    if (iHostStrCaseCmp(value, "gamecube") == 0)
    {
        return iEXTRAS_GAMECUBE;
    }
    if (iHostStrCaseCmp(value, "xbox") == 0)
    {
        return iEXTRAS_XBOX;
    }

    // Anything else is a typo: the table's default (gamecube on the 3DS),
    // said once.
    const iConfigSetting* row = iConfigTableFind(key);
    const iExtrasLook look = row != NULL && iHostStrCaseCmp(row->value, "gamecube") == 0 ?
                                 iEXTRAS_GAMECUBE :
                                 iEXTRAS_XBOX;
    if (feature >= 0 && feature < iEXTRAS_FEATURE_COUNT && !sToldBad[feature])
    {
        sToldBad[feature] = true;
        printf("tssm: config: %s is not xbox or gamecube, using %s: %s\n", key,
               look == iEXTRAS_XBOX ? "xbox" : "gamecube", value);
        fflush(stdout);
    }
    return look;
}

const char* iExtrasBadValue(iExtrasFeature feature)
{
    const char* value = iConfigGetString(iExtrasKey(feature), "xbox");
    if (iHostStrCaseCmp(value, "gamecube") == 0 || iHostStrCaseCmp(value, "xbox") == 0)
    {
        return NULL;
    }
    return value;
}

iExtrasLook iExtrasResolve(iExtrasFeature feature, iExtrasLook setting, iExtrasFiles files,
                           iExtrasRenderer renderer, U32 sceneID, const char** why)
{
    *why = NULL;
    if (setting == iEXTRAS_GAMECUBE)
    {
        return iEXTRAS_GAMECUBE;
    }

    if (!iExtrasBuilt(feature))
    {
        *why = feature == iEXTRAS_WATER ? "The Xbox water is not in this build." :
                                          "Not in this build yet.";
        return iEXTRAS_GAMECUBE;
    }

    switch (feature)
    {
    case iEXTRAS_CROWDS:
        // The animation is the files' (an animation list on each crowd object);
        // the code is the same on both releases.
        // BB01's GameCube files have no skinned crowd at all; BB02's have one
        // their crowd objects do not use, which iExtrasGC.h gives them.
        if (files == iEXTRAS_FILES_GAMECUBE && sceneID == 'BB01')
        {
            *why = "This level's files are GameCube files: they have no moving crowd, so it "
                   "stands still.";
            return iEXTRAS_GAMECUBE;
        }
        return iEXTRAS_XBOX;

    case iEXTRAS_FRAME_LOCK:
        // Code only: the files and the renderer do not matter.
        (void)renderer;
        return iEXTRAS_XBOX;

    case iEXTRAS_SPOTLIGHTS:
        // The Xbox's light volumes need a stencil buffer, which every PC
        // renderer has; the 3DS has not been measured with them.
        if (renderer == iEXTRAS_RENDERER_3DS)
        {
            *why = "Not on the 3DS yet.";
            return iEXTRAS_GAMECUBE;
        }
        // TT01's GameCube files have no disco lights at all. BB02's and B401's
        // switch their lights off with events of their own, which iExtrasGC.h
        // gives the Xbox files' links instead.
        if (files == iEXTRAS_FILES_GAMECUBE && sceneID == 'TT01')
        {
            *why = "This level's files are GameCube files: they have no disco lights, so "
                   "they stay off.";
            return iEXTRAS_GAMECUBE;
        }
        return iEXTRAS_XBOX;

    case iEXTRAS_WATER:
        // The Xbox water needs shaders (iXboxWaterDraw.h) and the Xbox files:
        // the levels' water bodies and plat.HIP's bump frames, of which the
        // GameCube files have neither. Its other reasons (Vulkan, a short bump
        // frame, shaders or targets that could not be made) are the water's own,
        // found as the level loads (iXboxWater.h; iExtrasStatus).
        if (renderer == iEXTRAS_RENDERER_3DS)
        {
            *why = "Not on the 3DS yet.";
            return iEXTRAS_GAMECUBE;
        }
        if (renderer == iEXTRAS_RENDERER_FIXED)
        {
            *why = "Direct3D 9's fixed path has no water shaders: the GameCube water is used.";
            return iEXTRAS_GAMECUBE;
        }
        if (files == iEXTRAS_FILES_GAMECUBE)
        {
            *why = sceneID != 0 ? "This level's files are GameCube files: they have no Xbox water." :
                                  "The game's files are GameCube files: they have no Xbox water.";
            return iEXTRAS_GAMECUBE;
        }
        return iEXTRAS_XBOX;

    case iEXTRAS_GLOW:
        // Code only, on the frame's alpha, which both releases' files fill
        // nearly the same way (TT01's talk window is one known difference,
        // docs/RENDERING.md). Every renderer with the GameCube glow has the Xbox one
        // (D3D9's fixed path draws its taps one at a time) except the 3DS's,
        // which has not been measured with it.
        if (renderer == iEXTRAS_RENDERER_3DS)
        {
            *why = "Not on the 3DS yet.";
            return iEXTRAS_GAMECUBE;
        }
        return iEXTRAS_XBOX;

    default:
        return iEXTRAS_XBOX;
    }
}

void iExtrasSceneEnter(U32 sceneID)
{
    sSceneID = sceneID;
    sHaveScene = true;
    sCrowdsHeld = 0;
    sSceneFiles = PackageFiles(sceneID);
    for (S32 i = 0; i < iEXTRAS_FEATURE_COUNT; i++)
    {
        sLateWhy[i] = NULL;
        sNote[i][0] = '\0';
    }

    const iExtrasFiles files = sSceneFiles;
    const iExtrasRenderer renderer = CurrentRenderer();
    for (S32 i = 0; i < iEXTRAS_FEATURE_COUNT; i++)
    {
        const iExtrasFeature f = (iExtrasFeature)i;
        sAtLoad[i] = iExtrasSetting(f);
        sInForce[i] = iExtrasResolve(f, sAtLoad[i], files, renderer, sceneID, &sWhy[i]);
    }

    sFrameLockOn = sInForce[iEXTRAS_FRAME_LOCK] == iEXTRAS_GAMECUBE;
    sGlow = sInForce[iEXTRAS_GLOW];
    sGlowKnown = true;
}

void iExtrasSceneReady(U32 sceneID, U32 minVSyncCnt)
{
    char name[5];
    SceneName(sceneID, name);

    char line[768];
    size_t n = 0;
    Append(line, sizeof(line), &n, "tssm: extras: %s (%s files):", name,
           sSceneFiles == iEXTRAS_FILES_GAMECUBE ? "GameCube" : "Xbox");

    bool first = true;
    for (S32 i = 0; i < iEXTRAS_FEATURE_COUNT; i++)
    {
        if (!iExtrasBuilt((iExtrasFeature)i))
        {
            continue;
        }
        // The water's own fallback in a level with water bodies (its own line
        // says the same), or the level's files' (iExtrasFallBack): what the
        // level got.
        const char* late = LevelFellBack((iExtrasFeature)i);
        Append(line, sizeof(line), &n, "%s %s %s", first ? "" : ",", kNames[i],
               sInForce[i] == iEXTRAS_XBOX && late == NULL ? "xbox" : "gamecube");
        first = false;
        if (late != NULL)
        {
            Append(line, sizeof(line), &n, " [asked for xbox: %s]",
                   strncmp(late, "Now: ", 5) == 0 ? late + 5 : late);
        }
        if (sNote[i][0] != '\0')
        {
            Append(line, sizeof(line), &n, " %s", sNote[i]);
        }

        if (i == iEXTRAS_GLOW && sInForce[i] == iEXTRAS_XBOX)
        {
            Append(line, sizeof(line), &n, " %.2f", iExtrasGlowStrength(sceneID));
        }
        if (i == iEXTRAS_CROWDS && sCrowdsHeld > 0)
        {
            Append(line, sizeof(line), &n, " (%d held still)", (int)sCrowdsHeld);
        }
        if (i == iEXTRAS_FRAME_LOCK && minVSyncCnt > 1)
        {
            Append(line, sizeof(line), &n,
                   sInForce[i] == iEXTRAS_GAMECUBE ? " (held at %.2f fps)" :
                                                     " (a %.2f fps level, at the cap)",
                   59.94 / minVSyncCnt);
        }
        if (sWhy[i] != NULL)
        {
            Append(line, sizeof(line), &n, " [asked for xbox: %s]", sWhy[i]);
        }
    }

    bool anyMissing = false;
    for (S32 i = 0; i < iEXTRAS_FEATURE_COUNT; i++)
    {
        if (!iExtrasBuilt((iExtrasFeature)i))
        {
            Append(line, sizeof(line), &n, "%s %s", anyMissing ? "," : "; not built:", kNames[i]);
            anyMissing = true;
        }
    }

    printf("%s\n", line);
    fflush(stdout);
}

iExtrasLook iExtrasInForce(iExtrasFeature feature)
{
    if (feature < 0 || feature >= iEXTRAS_FEATURE_COUNT)
    {
        return iEXTRAS_GAMECUBE;
    }
    if (feature == iEXTRAS_GLOW)
    {
        if (!sGlowKnown)
        {
            ReadGlow();
        }
        return sGlow;
    }
    if (!sHaveScene)
    {
        const char* why;
        return iExtrasResolve(feature, iExtrasSetting(feature), SceneFiles(), CurrentRenderer(), 0,
                              &why);
    }
    if (feature == iEXTRAS_FRAME_LOCK)
    {
        return sFrameLockOn ? iEXTRAS_GAMECUBE : iEXTRAS_XBOX;
    }
    return sInForce[feature];
}

iExtrasLook iExtrasAtLevelLoad(iExtrasFeature feature)
{
    if (feature < 0 || feature >= iEXTRAS_FEATURE_COUNT)
    {
        return iEXTRAS_GAMECUBE;
    }
    return sHaveScene ? sAtLoad[feature] : iExtrasSetting(feature);
}

iExtrasFiles iExtrasLevelFiles()
{
    return SceneFiles();
}

void iExtrasFallBack(iExtrasFeature feature, const char* why)
{
    if (!sHaveScene || feature < 0 || feature >= iEXTRAS_FEATURE_COUNT || why == NULL)
    {
        return;
    }
    sInForce[feature] = iEXTRAS_GAMECUBE;
    sLateWhy[feature] = why;
    sNote[feature][0] = '\0';
}

void iExtrasNote(iExtrasFeature feature, const char* fmt, ...)
{
    if (!sHaveScene || feature < 0 || feature >= iEXTRAS_FEATURE_COUNT || fmt == NULL)
    {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(sNote[feature], sizeof(sNote[feature]), fmt, args);
    va_end(args);
}

bool iExtrasStatus(iExtrasFeature feature, char* out, size_t outSize)
{
    if (out == NULL || outSize == 0)
    {
        return false;
    }
    out[0] = '\0';
    if (feature < 0 || feature >= iEXTRAS_FEATURE_COUNT)
    {
        return false;
    }

    // The current level's answer to the current setting: a change not yet in
    // force has its own "next level" tag, and is not a reason.
    const iExtrasLook setting = iExtrasSetting(feature);
    const char* why = NULL;
    iExtrasResolve(feature, setting, SceneFiles(), CurrentRenderer(), sHaveScene ? sSceneID : 0,
                   &why);
    // The level may still have fallen back, for a reason of its own, when it
    // loaded with xbox: the water's (iXboxWaterActive) or its files'
    // (iExtrasFallBack).
    if (why == NULL && setting == iEXTRAS_XBOX)
    {
        why = LevelFellBack(feature);
    }
    if (why == NULL)
    {
        return false;
    }
    snprintf(out, outSize, "%s", why);
    return true;
}

void iExtrasSetRetraceCount(S32 count)
{
    // Once a game frame. The setting is the one read when the level loaded or
    // when the overlay last changed it (iExtrasApply): reading config.ini here
    // would cost a search of it every frame.
    sRetraceCount = count > 0 ? count : 1;
    PushFrameLock();
}

S32 iExtrasHeldRetraces()
{
    return iWindowGetFrameLock();
}

S32 iExtrasHeldCarSteps(F32 dt, S32 steps)
{
    if (iWindowGetFrameLock() <= 1)
    {
        return steps;
    }
    const S32 n = (S32)(dt * 60.0f + 0.5f);
    return n > 0 ? n : 1;
}

void iExtrasApply()
{
    sFrameLockOn = ReadFrameLock();
    PushFrameLock();
    ReadGlow();
}

F32 iExtrasGlowStrength(U32 sceneID)
{
    switch (sceneID)
    {
    case 'JK01':
    case 'JK02':
        return 0.3f;
    case 'BB02':
    case 'TR01':
    case 'TR02':
    case 'TR03':
        return 0.55f;
    case 'B401':
    case 'PT01':
    case 'PT02':
    case 'PT03':
    case 'SC02':
        return 0.6f;
    case 'TT01':
    case 'TT02':
        return 0.7f;
    case 'GG02':
        return 0.75f;
    case 'BB03':
    case 'B201':
        return 1.0f;
    default:
        return 0.45f;
    }
}

bool iExtrasCrowdModel(U32 modelID)
{
    return modelID == 0xC49163C8 || // crowd1_bind
           modelID == 0xC015AF3B || // crowd2_bind
           modelID == 0xBB99FAAE;   // crowd3_bind
}

bool iExtrasCrowdHeld(U32 modelID)
{
    if (!iExtrasCrowdModel(modelID) || iExtrasInForce(iEXTRAS_CROWDS) != iEXTRAS_GAMECUBE)
    {
        return false;
    }
    sCrowdsHeld++;
    return true;
}

bool iExtrasSpotlightKeptOff(U32 sceneID, U32 id)
{
    for (size_t i = 0; i < sizeof(kKeptOff) / sizeof(kKeptOff[0]); i++)
    {
        if (kKeptOff[i].scene == sceneID && kKeptOff[i].id == id)
        {
            return true;
        }
    }
    return false;
}

// --- a level's GameCube files ----------------------------------------------------------

bool iExtrasCrowdSkinned(U32 rigidModel, U32* skinnedModel, U32* animList)
{
    struct Crowd
    {
        U32 rigid;
        U32 skinned;
        U32 list;
    };
    static const Crowd kCrowds[] = {
        { 0x51947E72, 0xC49163C8, 0x9AD84F76 }, // crowd01: crowd1_bind, CROWD1_ANIMLIST
        { 0x51947E73, 0xC015AF3B, 0x6EAEF0D9 }, // crowd02: crowd2_bind, CROWD2_ANIMLIST
        { 0x51947E74, 0xBB99FAAE, 0x4285923C }, // crowd03: crowd3_bind, CROWD3_ANIMLIST
    };
    for (size_t i = 0; i < sizeof(kCrowds) / sizeof(kCrowds[0]); i++)
    {
        if (kCrowds[i].rigid == rigidModel)
        {
            *skinnedModel = kCrowds[i].skinned;
            *animList = kCrowds[i].list;
            return true;
        }
    }
    return false;
}

namespace
{
    // One link of a level's GameCube files and what the Xbox files have in its
    // place: nothing, or one or two links from the same event.
    struct LinkEdit
    {
        U32 scene;
        U32 object;
        U32 type; // the object's asset type
        U16 srcEvent;
        U16 dstEvent; // the GameCube file's
        U32 dst;
        S32 xboxCount;
        U16 xboxEvent[2];
        U32 xboxDst[2];
    };

    const U32 kSpotlight01 = 0xEC67F9D8;     // SPOTLIGHT_01
    const U32 kSpotlight02 = 0xEC67F9D9;     // SPOTLIGHT_02
    const U32 kSpotlightGroup = 0x30067BA0;  // SPOTLIGHT_GROUP
    const U32 kSpotlightScript = 0x569AE23E; // SPOTLIGHT_SCRIPT

    const LinkEdit kLinkEdits[] = {
        // BB02: SPOTLIGHT_DISP_GC_ONLY, which only the GameCube files have.
        { 'BB02', 0xE9B0DDB1, 'DPAT', eEventSceneBegin, eEventOff, kSpotlight01, 0, {}, {} },
        { 'BB02', 0xE9B0DDB1, 'DPAT', eEventSceneBegin, eEventOff, kSpotlight02, 0, {}, {} },
        // B401: the intro's end and the restart.
        { 'B401', 0x2F702A12, 'CSNM', eEventDone, eEventOff, kSpotlightGroup, 2,
          { eEventOn, eEventRun }, { kSpotlightGroup, kSpotlightScript } },
        { 'B401', 0xA18EA13D, 'DPAT', eEventSceneReset, eEventOff, kSpotlightGroup, 2,
          { eEventOn, eEventRun }, { kSpotlightGroup, kSpotlightScript } },
    };
    const size_t kLinkEditCount = sizeof(kLinkEdits) / sizeof(kLinkEdits[0]);

    bool Matches(const LinkEdit& e, const xLinkAsset& l)
    {
        return l.srcEvent == e.srcEvent && l.dstEvent == e.dstEvent && l.dstAssetID == e.dst &&
               l.param[0] == 0.0f && l.param[1] == 0.0f && l.param[2] == 0.0f &&
               l.param[3] == 0.0f && l.paramWidgetAssetID == 0 && l.chkAssetID == 0;
    }
} // namespace

S32 iExtrasSpotlightLinkObjects(U32 sceneID, U32* ids, U32* types, S32 max)
{
    S32 n = 0;
    for (size_t i = 0; i < kLinkEditCount; i++)
    {
        const LinkEdit& e = kLinkEdits[i];
        if (e.scene != sceneID)
        {
            continue;
        }
        bool seen = false;
        for (S32 k = 0; k < n && !seen; k++)
        {
            seen = ids[k] == e.object;
        }
        if (!seen && n < max)
        {
            ids[n] = e.object;
            types[n] = e.type;
            n++;
        }
    }
    return n;
}

S32 iExtrasSpotlightLinks(U32 sceneID, U32 objectID, const xLinkAsset* in, S32 count,
                          xLinkAsset* out, S32 outMax)
{
    if (count < 0 || (count > 0 && in == NULL))
    {
        return -1;
    }

    // Every edit of the object has to find its link exactly once.
    for (size_t i = 0; i < kLinkEditCount; i++)
    {
        const LinkEdit& e = kLinkEdits[i];
        if (e.scene != sceneID || e.object != objectID)
        {
            continue;
        }
        S32 found = 0;
        for (S32 k = 0; k < count; k++)
        {
            found += Matches(e, in[k]) ? 1 : 0;
        }
        if (found != 1)
        {
            return -1;
        }
    }

    S32 n = 0;
    for (S32 k = 0; k < count; k++)
    {
        const LinkEdit* edit = NULL;
        for (size_t i = 0; i < kLinkEditCount && edit == NULL; i++)
        {
            const LinkEdit& e = kLinkEdits[i];
            if (e.scene == sceneID && e.object == objectID && Matches(e, in[k]))
            {
                edit = &e;
            }
        }
        if (edit == NULL)
        {
            if (n >= outMax)
            {
                return -1;
            }
            out[n++] = in[k];
            continue;
        }
        for (S32 j = 0; j < edit->xboxCount; j++)
        {
            if (n >= outMax)
            {
                return -1;
            }
            out[n] = in[k];
            out[n].dstEvent = edit->xboxEvent[j];
            out[n].dstAssetID = edit->xboxDst[j];
            n++;
        }
    }
    return n;
}
