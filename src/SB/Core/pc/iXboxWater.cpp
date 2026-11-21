// The Xbox release's water, the game half (iXboxWater.h). PC only, compiled
// with the game units because it calls game code.

#include "iXboxWater.h"

#include "iCapture.h"
#include "iDeviceReset.h"
#include "iDraw.h"
#include "iExtras.h"
#include "iGpuOpt.h"
#include "iScreen.h"
#include "iWaterBody.h"
#include "iXboxWaterDraw.h"
#include "iXboxWaterGrid.h"
#include "iXboxWaterPass.h"

#include "xEnt.h"
#include "xGroup.h"
#include "xLightKit.h"
#include "xModel.h"
#include "xString.h"
#include "xstransvc.h"
#include "xutil.h"
#include "zBase.h"
#include "zCam.h"
#include "zEntSimpleObj.h"
#include "zGlobals.h"
#include "zRenderState.h"
#include "zScene.h"

#include <rpworld.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// zRenderState.cpp's record of the current state, which zRenderStateCurrent
// reads.
extern _SDRenderState sRS;

namespace
{
    // The Xbox's table of bodies has 16 slots.
    const S32 kMaxBodies = 16;

    // Groups inside groups are flattened; a deeper nest (or a loop) is cut.
    const S32 kMaxGroupDepth = 8;

    // And a list's walk stops after this many objects (groups and members),
    // so that a modded group listing itself or its parents many times cannot
    // hold the level's load for ever. The Xbox's lists have 2 to 7.
    const S32 kMaxListVisits = 1024;

    // What changes per level. The grid is xWaterAddBody's table (0x2CBBA8
    // default, 0x2CBBB8 B301, 0x2CBBC8 SC02; the "hi" entry 0x2CBB98 is never
    // used); the clock speed, the reflection's distortion numerator and the
    // tint are iWaterBeginRender's (0x1C2CF0). On the Xbox the last three
    // carry over from the last level that set them; here a level without its
    // own values gets the defaults, which only a modded level can tell.
    struct Preset
    {
        U32 scene;
        iXboxWaterPreset grid;
        bool mask; // SC02: only tiles with a mesh vertex are drawn
        F32 speed;
        F32 reflectLod;
        F32 tint[4];
    };

    // clang-format off
    const Preset kDefault =
        { 0,      { 40000.0f,  1.0f, 16, 16 }, false, 2.4f, 20.0f, { 0.55f, 0.65f, 0.85f, 0.5f } };

    const Preset kLevels[] = {
        { 'SC02', { 40000.0f,  1.0f, 16, 16 }, true,  2.4f, 20.0f, { 0.6f,  0.8f,  0.9f,  0.5f } },
        { 'B301', { 128000.0f, 2.0f, 32, 32 }, false, 8.0f, 30.0f, { 0.55f, 0.65f, 0.85f, 0.5f } },
    };
    // clang-format on

    // One registered body (xWaterAddBody's slot, 0x45CBD0).
    struct Body
    {
        zWaterBody* owner;
        RpAtomic* atomic;
        iXboxWaterLayout layout;
        U32 tileCount;
        U32 drawable; // tiles not left out by the mask
        iXboxWaterTile* tiles;

        // The owner's two lists (malloc'd by LoadEnts, freed with the body).
        xEnt** refract;
        xEnt** reflect;

        // A tile neither masked nor culled: set with the cull every update.
        // Without one the body's passes go to the stand-in pair.
        bool inView;
    };

    Body sBodies[kMaxBodies];
    S32 sBodyCount;
    const Preset* sPreset = &kDefault;
    bool sActive;

    // From xWaterScenePrepare: the water bodies in the scene's files, those
    // whose assets check out, why the Xbox water may not run (NULL: it may),
    // and the verdict (iXboxWaterWillRun).
    S32 sFileBodies;
    S32 sAssetBodies;
    const char* sWhyNot;
    char sWhyBuf[256];
    bool sWillRun;

    // The bump clock and its frame (0x479054, 0x478EB0). Not reset between
    // levels, as on the Xbox.
    F32 sClock;
    S32 sFrame;

    bool sLtmLogged;

    // The bump frames as textures (iWaterLoadTextures, 0x1B1B30): made the
    // first time a level has the Xbox water and kept, as plat.HIP is.
    RwRaster* sBumps[iXBOXWATER_BUMP_FRAMES];
    RwTexture* sBumpTextures[iXBOXWATER_BUMP_FRAMES]; // the same, wrapped, bilinear
    bool sBumpsMade;

    // The last decision, for the overlay's Settings tab (iXboxWaterStatus).
    char sStatus[160];

    // The surface's first draw in a scene is logged with what it is drawn
    // with; the frames it has been drawn in, for the second look at the
    // draw list (LogSlots).
    bool sSurfaceLogged;
    U32 sSurfaceFrames;

    // The pairs of targets the scene has: one a body when they are drawn
    // early, else one (the Xbox's); and a spare for TSSM_WATER_VERIFY.
    S32 sPairsWanted;

    // The targets could not be made again at a new render size: no water
    // until the next level, said once.
    bool sTargetsLost;

    // For the log: body-frames whose passes went to the stand-in pair.
    U32 sStandInDraws;

    // The render state where the Xbox draws the targets: the state its passes
    // meet there (zRenderState's current one, and the colour write mask),
    // taken there every frame for the next frame's early pass. With it the
    // fog, which zRenderState gives each target's camera: the early pass runs
    // before this frame's fog blend step (iCameraUpdateFog in
    // zCamBeginRender), so while the fog changes it would draw last frame's.
    struct SceneState
    {
        iRenderStateSnap rw;
        _SDRenderState zrs;
        U32 fbmsk;
        iFogParams fog;
    };

    SceneState sMet;
    bool sMetValid;

    // This frame's early pass: drawn, the state it met and the state it left.
    bool sEarlyPending;
    SceneState sEarlyMet;
    SceneState sEarlyAfter;

    // The scene camera as the early pass copied it (0xCAF80's fields). A
    // camera moved between then and the Xbox's place (a cutscene's, say)
    // has the targets drawn there again.
    struct CameraKey
    {
        RwMatrix mat;
        RwV2d window;
        RwV2d offset;
        F32 nearPlane;
        F32 farPlane;
        F32 fogPlane;
        S32 projection;
    };

    CameraKey sEarlyCamera;

    // iDeviceResetEpoch when the early pass began: a Direct3D 9 device lost
    // or reset since (in the early pass's own camera update, or the scene's)
    // may have emptied the targets, which are then drawn again.
    unsigned sEarlyEpoch;

    // For the log: frames whose targets came from the early pass, and frames
    // that drew them again at the Xbox's place because its state had changed
    // since the frame before.
    U32 sEarlyFrames;
    U32 sLateFrames;

    // TSSM_WATER_VERIFY's counts, and the pairs that differed written out.
    U32 sVerifyDumps;
    U32 sVerifyBodies;
    U32 sVerifyRefract;
    U32 sVerifyReflect;
    U32 sVerifyState;

    const char* SceneName()
    {
        return xUtil_idtag2string(globals.sceneCur->sceneID, 0);
    }

    const char* NameOf(xBase* b)
    {
        const char* name = b != NULL ? zSceneGetName(b) : NULL;
        return name != NULL && name[0] != '\0' ? name : "(unnamed)";
    }

    // iWaterLoadTextures (0x1B1B30) reads bumps0..bumps16, RAW assets of
    // plat.HIP, and uses the first 0x8000 bytes of each (128 x 128 du/dv).
    const char* BumpsMissing()
    {
        // TSSM_WATER_FAIL=bumps: as if plat.HIP had none, for the fallback's
        // test.
        const char* fail = getenv("TSSM_WATER_FAIL");

        if (fail != NULL && strstr(fail, "bumps") != NULL)
        {
            return "plat.HIP has no bumps0 (TSSM_WATER_FAIL)";
        }

        for (S32 k = 0; k < 17; k++)
        {
            char name[16];
            snprintf(name, sizeof(name), "bumps%d", (int)k);

            U32 size = 0;
            void* data = xSTFindAsset(xStrHash(name), &size);

            if (data == NULL)
            {
                snprintf(sWhyBuf, sizeof(sWhyBuf),
                         "plat.HIP has no %s (GameCube or incomplete files)", name);
                return sWhyBuf;
            }

            if (size < 0x8000)
            {
                snprintf(sWhyBuf, sizeof(sWhyBuf), "plat.HIP's %s is %u bytes, under 32768", name,
                         (unsigned)size);
                return sWhyBuf;
            }
        }

        return NULL;
    }

    const char* RendererMissing()
    {
        switch (iScreenGetBackend())
        {
        case iSCREENBACKEND_D3D9:
            return iScreenGetPipeline() == iSCREENPIPE_FIXED ?
                       "Direct3D 9's fixed path has no water shaders" :
                       NULL;
        case iSCREENBACKEND_D3D11:
        case iSCREENBACKEND_GL3:
            return NULL;
        case iSCREENBACKEND_VULKAN:
            return "the Vulkan renderer has no water shaders";
        default:
            return "no renderer";
        }
    }

    const Preset* PresetFor(U32 scene)
    {
        for (size_t i = 0; i < sizeof(kLevels) / sizeof(kLevels[0]); i++)
        {
            if (kLevels[i].scene == scene)
            {
                return &kLevels[i];
            }
        }

        return &kDefault;
    }

    zWaterBody* BodyObjects(S32* count)
    {
        zScene* s = globals.sceneCur;
        *count = s != NULL ? s->baseCount[eBaseTypeWaterBody] : 0;
        return *count > 0 ? (zWaterBody*)s->baseList[eBaseTypeWaterBody] : NULL;
    }

    // An object the water may hide and draw: an entity with a model. (The Xbox
    // takes whatever the IDs name.)
    xEnt* AsModelEnt(xBase* b)
    {
        if (b == NULL || !xEntValidType(b->baseType))
        {
            return NULL;
        }

        xEnt* ent = (xEnt*)b;
        return ent->asset != NULL && ent->model != NULL && ent->model->Data != NULL &&
                       ent->model->Mat != NULL ?
                   ent :
                   NULL;
    }

    // Why a water body asset cannot be used, or NULL, from the files alone
    // (before the objects are made): BodyProblem's checks as far as the
    // assets can answer them.
    const char* AssetProblem(const xDynAsset* a, U32 size)
    {
        if (size < sizeof(zWaterBody::asset_type))
        {
            return "its asset is short";
        }

        U32 bodySize = 0;
        const xBaseAsset* body =
            (const xBaseAsset*)xSTFindAsset(((const zWaterBody::asset_type*)a)->body, &bodySize);

        if (body == NULL)
        {
            return "its body object is missing";
        }

        if (bodySize < sizeof(xEntAsset) || !xEntValidType(body->baseType) ||
            xSTFindAsset(((const xEntAsset*)body)->modelInfoID, NULL) == NULL)
        {
            return "its body is not a model";
        }

        return NULL;
    }

    // "WATER_BODY_01: its asset is short" into the list of what is left out.
    void ListProblem(char* list, size_t size, size_t* len, const char* name, const char* problem)
    {
        int w = snprintf(list + *len, size - *len, "%s%s: %s", *len ? "; " : "", name, problem);

        if (w > 0 && (size_t)w < size - *len)
        {
            *len += (size_t)w;
        }
    }

    // Why a body cannot be used, or NULL. Checked once at the decision so that
    // iXboxWaterActive() is final from there.
    const char* BodyProblem(const zWaterBody& b)
    {
        if (b.asset == NULL)
        {
            return "its asset is short";
        }

        // TSSM_WATER_FAIL=objects: as if the assets checked out and the
        // objects did not, for the test of that case (iXboxWater.h).
        const char* fail = getenv("TSSM_WATER_FAIL");

        if (fail != NULL && strstr(fail, "objects") != NULL)
        {
            return "its object is not usable (TSSM_WATER_FAIL)";
        }

        xBase* base = zSceneFindObject(b.asset->body);

        if (base == NULL)
        {
            return "its body object is missing";
        }

        xEnt* ent = AsModelEnt(base);

        if (ent == NULL)
        {
            return "its body is not a model";
        }

        RpGeometry* geom = RpAtomicGetGeometry(ent->model->Data);

        if (geom == NULL || geom->numVertices <= 0 || geom->morphTarget == NULL ||
            geom->morphTarget->verts == NULL)
        {
            return "its body's model has no vertices";
        }

        return NULL;
    }

    // count_ents (0x29EA0) and load_ents (0x29F10): a group counts its members,
    // recursively; anything else counts one. Members that are not models are
    // left out here, and group members are looked up by ID rather than through
    // the group's pointers (xGroupSetup leaves an asset pointer where no object
    // has the ID). `budget` is how many more objects the walk may visit
    // (kMaxListVisits): the count and the fill visit the same ones.
    S32 CollectEnts(xBase* base, xEnt** out, S32 depth, S32* budget)
    {
        if (base == NULL || depth > kMaxGroupDepth || *budget <= 0)
        {
            return 0;
        }

        (*budget)--;

        if (base->baseType == eBaseTypeGroup)
        {
            xGroup* group = (xGroup*)base;
            S32 total = 0;
            U32 count = group->asset != NULL ? xGroupGetCount(group) : 0;

            for (U32 i = 0; i < count && *budget > 0; i++)
            {
                xBase* child = zSceneFindObject(xGroupGetItem(group, i));
                total += CollectEnts(child, out != NULL ? out + total : NULL, depth + 1, budget);
            }

            return total;
        }

        xEnt* ent = AsModelEnt(base);

        if (ent == NULL)
        {
            return 0;
        }

        if (out != NULL)
        {
            *out = ent;
        }

        return 1;
    }

    // zWaterBody::load_ents(size&, id, type) (0x2A0B0).
    xEnt** LoadEnts(S32& size, U32 id)
    {
        size = 0;
        xBase* base = zSceneFindObject(id);
        S32 budget = kMaxListVisits;
        S32 count = CollectEnts(base, NULL, 0, &budget);

        if (budget <= 0)
        {
            printf("tssm: water: %s: the list %08X is cut after %d objects (groups and models), "
                   "%d models\n",
                   SceneName(), (unsigned)id, (int)kMaxListVisits, (int)count);
        }

        if (count <= 0)
        {
            return NULL;
        }

        xEnt** ents = (xEnt**)malloc((size_t)count * sizeof(xEnt*));

        if (ents == NULL)
        {
            return NULL;
        }

        budget = kMaxListVisits;
        size = CollectEnts(base, ents, 0, &budget);
        return ents;
    }

    // stay_hidden_dammit (0x29E00): every time the entity's visibility is
    // updated, it is hidden again, and so is its simple-object slot.
    void StayHidden(xEnt* ent)
    {
        ent->asset->flags &= ~0x1;
        ent->flags &= ~0x1;
        ent->visUpdate = StayHidden;

        if (ent->baseType == eBaseTypeStatic && ((zEntSimpleObj*)ent)->smgr != NULL)
        {
            ((zEntSimpleObj*)ent)->smgr->flags &= ~0x1;
        }
    }

    // The hide loop (0x29E40): the asset's and the entity's visible bits off,
    // the model's on (so xModelRenderSingle still draws it into a target), and
    // the visibility hook taken over. The simple-object slot is left as it is,
    // as on the Xbox, until the next visibility update.
    void HideEnts(xEnt** ents, S32 count)
    {
        for (S32 i = 0; i < count; i++)
        {
            xEnt* ent = ents[i];
            ent->asset->flags &= ~0x1;
            ent->flags &= ~0x1;
            ent->model->Flags |= 0x1;
            ent->visUpdate = StayHidden;
        }
    }

    // sceneAlive: the owners still exist, and their pointers into what is freed
    // are cleared (at scene init the last scene's objects are gone).
    void FreeBodies(bool sceneAlive)
    {
        for (S32 i = 0; i < sBodyCount; i++)
        {
            Body& b = sBodies[i];
            free(b.tiles);
            free(b.refract);
            free(b.reflect);

            if (sceneAlive && b.owner != NULL)
            {
                b.owner->refract_ents = NULL;
                b.owner->refract_ents_size = 0;
                b.owner->reflect_ents = NULL;
                b.owner->reflect_ents_size = 0;
            }
        }

        memset(sBodies, 0, sizeof(sBodies));
        sBodyCount = 0;
    }

    void Transform(const RwMatrix& m, const RwV3d& v, F32* out)
    {
        out[0] = m.pos.x + m.right.x * v.x + m.up.x * v.y + m.at.x * v.z;
        out[1] = m.pos.y + m.right.y * v.x + m.up.y * v.y + m.at.y * v.z;
        out[2] = m.pos.z + m.right.z * v.x + m.up.z * v.y + m.at.z * v.z;
    }

    // xWaterAddBody (0x1AD7A0): the tiles over the mesh's world bounds, and in
    // SC02 the mask.
    // The caller has checked there is a free slot.
    void AddBody(zWaterBody* owner, RpAtomic* atomic, const RwMatrix& mat)
    {
        Body& b = sBodies[sBodyCount++];
        memset(&b, 0, sizeof(b));
        b.owner = owner;
        b.atomic = atomic;
        b.refract = owner->refract_ents;
        b.reflect = owner->reflect_ents;

        RpGeometry* geom = RpAtomicGetGeometry(atomic);
        U32 n = (U32)geom->numVertices;
        F32* world = (F32*)malloc(n * 3 * sizeof(F32));

        if (world == NULL)
        {
            return;
        }

        for (U32 i = 0; i < n; i++)
        {
            Transform(mat, geom->morphTarget->verts[i], &world[3 * i]);
        }

        if (!iXboxWaterLayoutFor(world, n, sPreset->grid, &b.layout))
        {
            printf("tssm: water: %s %s: its mesh gives no tiles (empty, flat on an axis, not "
                   "finite or over %u tiles)\n",
                   SceneName(), NameOf(owner), (unsigned)iXBOXWATER_MAX_TILES);
            free(world);
            return;
        }

        U32 count = b.layout.rows * b.layout.cols;
        b.tiles = (iXboxWaterTile*)malloc(count * sizeof(iXboxWaterTile));

        if (b.tiles == NULL)
        {
            free(world);
            return;
        }

        b.tileCount = count;

        for (U32 r = 0; r < b.layout.rows; r++)
        {
            for (U32 c = 0; c < b.layout.cols; c++)
            {
                iXboxWaterTileAt(b.layout, r, c, &b.tiles[r * b.layout.cols + c]);
            }
        }

        for (U32 t = 0; t < count; t++)
        {
            if (!sPreset->mask)
            {
                b.drawable++;
                continue;
            }

            if (iXboxWaterTileHasPoint(b.tiles[t], world, n))
            {
                b.tiles[t].flags |= iXBOXWATER_TILE_HASVERTS;
                b.drawable++;
            }
            else
            {
                b.tiles[t].flags |= iXBOXWATER_TILE_MASKED;
            }
        }

        // Until the first cull.
        b.inView = b.drawable > 0;
        free(world);
    }

    void FormatMatrix(char* out, size_t size, const RwMatrix* m)
    {
        if (m == NULL)
        {
            snprintf(out, size, "none");
            return;
        }

        const F32 e = 1.0e-5f;
        bool identity = fabsf(m->right.x - 1.0f) < e && fabsf(m->right.y) < e &&
                        fabsf(m->right.z) < e && fabsf(m->up.x) < e && fabsf(m->up.y - 1.0f) < e &&
                        fabsf(m->up.z) < e && fabsf(m->at.x) < e && fabsf(m->at.y) < e &&
                        fabsf(m->at.z - 1.0f) < e;

        if (identity)
        {
            snprintf(out, size, "pos (%g, %g, %g), no rotation or scale", m->pos.x, m->pos.y,
                     m->pos.z);
        }
        else
        {
            snprintf(out, size,
                     "pos (%g, %g, %g), right (%g, %g, %g), up (%g, %g, %g), at (%g, %g, %g)",
                     m->pos.x, m->pos.y, m->pos.z, m->right.x, m->right.y, m->right.z, m->up.x,
                     m->up.y, m->up.z, m->at.x, m->at.y, m->at.z);
        }
    }

    const RwMatrix* AtomicLtm(RpAtomic* atomic)
    {
        RwFrame* frame = atomic != NULL ? RpAtomicGetFrame(atomic) : NULL;
        return frame != NULL ? RwFrameGetLTM(frame) : NULL;
    }

    // The surface's vertices are world space, and the Xbox's vertex shader
    // multiplies them by the body atomic's frame LTM as well (0x1C3140). With
    // an identity LTM that is nothing; otherwise the Xbox's offset is kept.
    void LogTransform(const Body& b, const char* when)
    {
        char mat[160];
        char ltm[160];
        FormatMatrix(mat, sizeof(mat), b.owner->body_ent->model->Mat);
        FormatMatrix(ltm, sizeof(ltm), AtomicLtm(b.atomic));
        printf("tssm: water: %s %s, body %s, %s: model matrix %s; atomic frame LTM %s\n",
               SceneName(), NameOf(b.owner), NameOf(b.owner->body_ent), when, mat, ltm);
    }

    // TSSM_WATER_DEBUG's words, read once: "tiles", "rt", and for the
    // surface "flat" (no ripples: both bump matrices zero), "refract" (the
    // refraction target alone, untinted: tint (1, 1, 1, 0)), "nosurface"
    // (none drawn).
    enum
    {
        kDebugTiles = 1 << 0,
        kDebugRt = 1 << 1,
        kDebugFlat = 1 << 2,
        kDebugRefract = 1 << 3,
        kDebugNoSurface = 1 << 4
    };

    U32 ReadDebug()
    {
        static const char* const kWords[] = { "tiles", "rt", "flat", "refract", "nosurface" };
        const char* v = getenv("TSSM_WATER_DEBUG");
        U32 flags = 0;

        for (U32 i = 0; v != NULL && i < sizeof(kWords) / sizeof(kWords[0]); i++)
        {
            if (strstr(v, kWords[i]) != NULL)
            {
                flags |= 1u << i;
            }
        }

        return flags;
    }

    bool Debug(U32 flag)
    {
        static const U32 flags = ReadDebug();
        return (flags & flag) != 0;
    }

    bool DebugTiles()
    {
        return Debug(kDebugTiles);
    }

    bool EnvFlag(const char* name, const char* value)
    {
        const char* v = getenv(name);
        return v != NULL && strcmp(v, value) == 0;
    }

    // A body out of view draws into the stand-in pair (iXboxWaterPass.h);
    // TSSM_WATER_STANDIN=0 has it draw its own, full size.
    bool StandIn()
    {
        static const bool standIn = !EnvFlag("TSSM_WATER_STANDIN", "0");
        return standIn;
    }

    // The targets drawn before the scene (the default), or only where the
    // Xbox draws them (TSSM_WATER_EARLY=0).
    bool Early()
    {
        static const bool early = !EnvFlag("TSSM_WATER_EARLY", "0");
        return early;
    }

    bool Verify()
    {
        static const bool verify = EnvFlag("TSSM_WATER_VERIFY", "1");
        return verify && Early();
    }

    bool DebugRt()
    {
        return Debug(kDebugRt);
    }

    // TSSM_WATER_DUMP: whether frame `frame` is in the list.
    bool DumpFrame(S32 frame)
    {
        static const char* const list = getenv("TSSM_WATER_DUMP");
        const char* v = list;

        if (v == NULL)
        {
            return false;
        }

        while (*v != '\0')
        {
            char* end;
            long n = strtol(v, &end, 10);

            if (end == v)
            {
                return false;
            }

            if (n == frame)
            {
                return true;
            }

            v = *end == ',' ? end + 1 : end;
        }

        return false;
    }

    void DumpPath(char* out, size_t size, S32 frame, const char* what)
    {
        const char* dir = getenv("TSSM_CAPTURE_DIR");
        const char* sep = "";

        if (dir == NULL)
        {
            dir = "";
        }
        else if (dir[0] != '\0')
        {
            char last = dir[strlen(dir) - 1];
            sep = last == '/' || last == '\\' ? "" : "/";
        }

        snprintf(out, size, "%s%swater_%05d_%s.png", dir, sep, (int)frame, what);
    }

    void TakeState(SceneState* s)
    {
        iRenderStateSave(&s->rw);
        s->zrs = zRenderStateCurrent();
        s->fbmsk = lastSetFBMSK;
        s->fog = xglobals->fog;
    }

    // zRenderState's record of the current state (sRS) is set back here with
    // the rest.
    void PutState(const SceneState& s, bool keepTexture)
    {
        iRenderStateRestore(&s.rw, keepTexture ? TRUE : FALSE);
        sRS = s.zrs;
        lastSetFBMSK = s.fbmsk;
    }

    bool SameFog(const iFogParams& a, const iFogParams& b)
    {
        return a.type == b.type && a.start == b.start && a.stop == b.stop &&
               a.density == b.density && a.table == b.table &&
               memcmp(&a.fogcolor, &b.fogcolor, sizeof(a.fogcolor)) == 0;
    }

    void TakeCamera(RwCamera* cam, CameraKey* k)
    {
        const RwMatrix* m = RwFrameGetMatrix(RwCameraGetFrame(cam));
        memset(k, 0, sizeof(*k));
        k->mat.right = m->right;
        k->mat.up = m->up;
        k->mat.at = m->at;
        k->mat.pos = m->pos;
        k->window = *RwCameraGetViewWindow(cam);
        k->offset = *RwCameraGetViewOffset(cam);
        k->nearPlane = RwCameraGetNearClipPlane(cam);
        k->farPlane = RwCameraGetFarClipPlane(cam);
        k->fogPlane = RwCameraGetFogDistance(cam);
        k->projection = (S32)RwCameraGetProjection(cam);
    }

    bool SameCamera(const CameraKey& a, const CameraKey& b)
    {
        return memcmp(&a, &b, sizeof(a)) == 0;
    }

    bool SameState(const SceneState& a, const SceneState& b, bool ignoreTexture)
    {
        return a.zrs == b.zrs && a.fbmsk == b.fbmsk && SameFog(a.fog, b.fog) &&
               iRenderStateSame(&a.rw, &b.rw, ignoreTexture ? TRUE : FALSE);
    }

    // iWaterLoadTextures (0x1B1B30): bumps0..bumps16 as 128x128 textures.
    // NULL when they are made, else why not.
    const char* MakeBumps()
    {
        if (sBumpsMade)
        {
            return NULL;
        }

        static U8 rgba[iXBOXWATER_BUMP_SIZE * iXBOXWATER_BUMP_SIZE * 4];
        const S32 size = (S32)iXBOXWATER_BUMP_SIZE;
        const char* why = NULL;
        RwImage* image = RwImageCreate(size, size, 32);

        if (image == NULL || RwImageAllocatePixels(image) == NULL)
        {
            why = "no memory for the bump frames";
        }

        for (U32 k = 0; why == NULL && k < iXBOXWATER_BUMP_FRAMES; k++)
        {
            char name[16];
            snprintf(name, sizeof(name), "bumps%u", (unsigned)k);

            U32 bytes = 0;
            const U8* data = (const U8*)xSTFindAsset(xStrHash(name), &bytes);

            if (!iXboxWaterBumpToRGBA(data, bytes, rgba))
            {
                snprintf(sWhyBuf, sizeof(sWhyBuf), "plat.HIP's %s is missing or short", name);
                why = sWhyBuf;
                break;
            }

            U8* pixels = RwImageGetPixels(image);
            const S32 stride = RwImageGetStride(image);

            for (S32 y = 0; y < size; y++)
            {
                memcpy(pixels + y * stride, rgba + y * size * 4, size * 4);
            }

            RwRaster* raster =
                RwRasterCreate(size, size, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);

            if (raster != NULL && RwRasterSetFromImage(raster, image) == NULL)
            {
                RwRasterDestroy(raster);
                raster = NULL;
            }

            if (raster == NULL)
            {
                why = "the bump frames' textures could not be made";
                break;
            }

            sBumps[k] = raster;
        }

        if (image != NULL)
        {
            RwImageDestroy(image);
        }

        if (why != NULL)
        {
            for (U32 k = 0; k < iXBOXWATER_BUMP_FRAMES; k++)
            {
                if (sBumps[k] != NULL)
                {
                    RwRasterDestroy(sBumps[k]);
                    sBumps[k] = NULL;
                }
            }

            return why;
        }

        // iWaterBeginRender's stage 0: wrap, bilinear (one level, so the
        // Xbox's linear mip filter has nothing to choose between).
        for (U32 k = 0; k < iXBOXWATER_BUMP_FRAMES; k++)
        {
            sBumpTextures[k] = RwTextureCreate(sBumps[k]);

            if (sBumpTextures[k] == NULL)
            {
                // All or none. A texture takes its raster with it.
                for (U32 n = 0; n < iXBOXWATER_BUMP_FRAMES; n++)
                {
                    if (sBumpTextures[n] != NULL)
                    {
                        RwTextureDestroy(sBumpTextures[n]);
                    }
                    else if (sBumps[n] != NULL)
                    {
                        RwRasterDestroy(sBumps[n]);
                    }

                    sBumpTextures[n] = NULL;
                    sBumps[n] = NULL;
                }

                return "the bump frames' textures could not be made";
            }

            RwTextureSetFilterMode(sBumpTextures[k], rwFILTERLINEAR);
            RwTextureSetAddressing(sBumpTextures[k], rwTEXTUREADDRESSWRAP);
        }

        sBumpsMade = true;
        return NULL;
    }

    // The pair body `i` draws into: its own when the targets are drawn early,
    // the one pair otherwise.
    S32 PairFor(S32 i)
    {
        return Early() ? i : 0;
    }

    // What body i's passes draw. False for a body with no tiles, which has no
    // height either (the Xbox reads tile 0's).
    bool FacadesFor(S32 i, iXboxWaterFacades* f)
    {
        const Body& b = sBodies[i];

        if (b.tileCount == 0 || b.owner == NULL)
        {
            return false;
        }

        f->refract = b.owner->refract_ents;
        f->refractCount = b.owner->refract_ents_size;
        f->reflect = b.owner->reflect_ents;
        f->reflectCount = b.owner->reflect_ents_size;
        f->height = b.tiles[0].y;
        return true;
    }

    // iWaterPatchLOD's distance for the refraction (0x2D2418): 10 units in
    // every level.
    const F32 kRefractLodDist = 10.0f;

    void LogSurface(const iXboxWaterSurface& s)
    {
        RwUInt32 fog = 0;
        RwBool fogOn = FALSE;
        RwRenderStateGet(rwRENDERSTATEFOGCOLOR, &fog);
        RwRenderStateGet(rwRENDERSTATEFOGENABLE, &fogOn);

        S32 w;
        S32 h;
        iXboxWaterTargetSizeNow(&w, &h);

        printf("tssm: water: %s: the surface is drawn, tint (%g, %g, %g, %g), %u vertices a tile "
               "edge, fog colour (%u, %u, %u) (the scene's fog is %s), %dx%d targets\n",
               SceneName(), s.tint[0], s.tint[1], s.tint[2], s.tint[3], (unsigned)s.vertsPerSide,
               (unsigned)((fog >> 16) & 0xFF), (unsigned)((fog >> 8) & 0xFF),
               (unsigned)(fog & 0xFF), fogOn ? "on" : "off", (int)w, (int)h);
    }

    // The models the water hides that the simple-object draw list still
    // draws in the scene: the hide leaves each one's slot as it is until its
    // next visibility update, as on the Xbox (StayHidden). Logged at the
    // surface's first frame in a scene and 600 frames later.
    void LogSlots(const char* when)
    {
        char names[256];
        size_t len = 0;
        S32 still = 0;
        S32 total = 0;
        names[0] = '\0';

        for (S32 i = 0; i < sBodyCount; i++)
        {
            const zWaterBody* owner = sBodies[i].owner;

            for (S32 list = 0; list < 2 && owner != NULL; list++)
            {
                xEnt** ents = list == 0 ? owner->refract_ents : owner->reflect_ents;
                S32 count = list == 0 ? owner->refract_ents_size : owner->reflect_ents_size;

                for (S32 k = 0; k < count; k++)
                {
                    xEnt* ent = ents[k];
                    total++;

                    if (ent->baseType != eBaseTypeStatic || ((zEntSimpleObj*)ent)->smgr == NULL ||
                        !(((zEntSimpleObj*)ent)->smgr->flags & 0x1))
                    {
                        continue;
                    }

                    still++;
                    int w = snprintf(names + len, sizeof(names) - len, "%s%s", len ? ", " : "",
                                     NameOf(ent));

                    if (w > 0 && (size_t)w < sizeof(names) - len)
                    {
                        len += (size_t)w;
                    }
                }
            }
        }

        printf("tssm: water: %s: %s, %d of the %d hidden models are still in the simple-object "
               "draw list%s%s%s\n",
               SceneName(), when, (int)still, (int)total, still ? " (" : "", names,
               still ? ")" : "");
    }

    // The rest of render_body (0x1AD570): iWaterBeginRender, the camera
    // constants, the targets as textures, then each tile that is neither
    // masked nor culled with its bump matrices scaled for its distance, then
    // iWaterEndRender.
    void DrawSurface(S32 body, S32 pair, RwCamera* cam)
    {
        const Body& b = sBodies[body];

        if (b.tileCount == 0 || b.drawable == 0 || cam == NULL || Debug(kDebugNoSurface))
        {
            return;
        }

        iXboxWaterSurface s;
        s.bump = sBumpTextures[sFrame];
        s.refraction = iXboxWaterRefractTexture(pair);
        s.reflection = iXboxWaterReflectTexture(pair);
        s.world = AtomicLtm(b.atomic);
        memcpy(s.tint, sPreset->tint, sizeof(s.tint));
        s.vertsPerSide = sPreset->grid.qx + 1;

        if (Debug(kDebugRefract))
        {
            s.tint[0] = s.tint[1] = s.tint[2] = 1.0f;
            s.tint[3] = 0.0f;
        }

        if (!iXboxWaterDrawBegin(s))
        {
            return;
        }

        if (!sSurfaceLogged)
        {
            sSurfaceLogged = true;
            LogSurface(s);
        }

        const RwV3d& eye = RwFrameGetLTM(RwCameraGetFrame(cam))->pos;
        const bool flat = Debug(kDebugFlat);

        for (U32 t = 0; t < b.tileCount; t++)
        {
            const iXboxWaterTile& tile = b.tiles[t];

            if (tile.flags & (iXBOXWATER_TILE_MASKED | iXBOXWATER_TILE_CULLED))
            {
                continue;
            }

            F32 dx = tile.cx - eye.x;
            F32 dy = tile.cy - eye.y;
            F32 dz = tile.cz - eye.z;
            F32 s1;
            F32 s2;
            iXboxWaterLodScales(sqrtf(dx * dx + dy * dy + dz * dz), kRefractLodDist,
                                sPreset->reflectLod, &s1, &s2);

            F32 refract[4];
            F32 reflect[4];

            for (S32 k = 0; k < 4; k++)
            {
                refract[k] = flat ? 0.0f : iXBOXWATER_BUMP_REFRACT[k] * s1;
                reflect[k] = flat ? 0.0f : iXBOXWATER_BUMP_REFLECT[k] * s2;
            }

            const F32 corner[4] = { tile.x0, tile.y, tile.z0, sPreset->grid.cell };
            iXboxWaterDrawTile(corner, refract, reflect);
        }

        iXboxWaterDrawEnd();
    }

    // Where body i's passes go this frame: its pair, or the stand-in when none
    // of its tiles is in view (the same calls, no pixels anyone reads).
    bool UseStandIn(S32 i)
    {
        return !sBodies[i].inView && StandIn() && iXboxWaterHasStandIn();
    }

    void DrawTargets(S32 body, S32 pair, RwCamera* cam)
    {
        iXboxWaterFacades f;

        if (!FacadesFor(body, &f))
        {
            return;
        }

        if (UseStandIn(body))
        {
            sStandInDraws++;
            pair = iXBOXWATER_PAIR_STANDIN;

            if (sStandInDraws == 1 || sStandInDraws % 1000 == 0)
            {
                printf("tssm: water: %s: frame %d, %s has no tile in view: its targets go to the "
                       "16x16 stand-in (%u times in this level)\n",
                       SceneName(), (int)iCaptureFrameCount() + 1, NameOf(sBodies[body].owner),
                       (unsigned)sStandInDraws);
            }
        }

        iXboxWaterTargetsDraw(pair, f, cam);
    }

    // TSSM_WATER_VERIFY: every body's targets drawn again here, at the Xbox's
    // place, into the spare pair, and compared with what the early pass drew.
    // The scene goes on from the state this draw leaves.
    void VerifyEarly(RwCamera* cam)
    {
        const S32 spare = sPairsWanted - 1;
        U32 refract = 0;
        U32 reflect = 0;
        U32 bodies = 0;

        for (S32 i = 0; i < sBodyCount && i < spare; i++)
        {
            iXboxWaterFacades f;

            if (!FacadesFor(i, &f))
            {
                continue;
            }

            // Out of view: the stand-in again, as early, and nothing to
            // compare (its own pair was not drawn).
            if (UseStandIn(i))
            {
                iXboxWaterTargetsDraw(iXBOXWATER_PAIR_STANDIN, f, cam);
                continue;
            }

            iXboxWaterTargetsDraw(spare, f, cam);

            S32 differ[2] = { 0, 0 };
            S32 most[2] = { 0, 0 };
            bool read =
                iXboxWaterCompareRasters(iXboxWaterRefractRaster(i), iXboxWaterRefractRaster(spare),
                                         &differ[0], &most[0]) &&
                iXboxWaterCompareRasters(iXboxWaterReflectRaster(i), iXboxWaterReflectRaster(spare),
                                         &differ[1], &most[1]);

            bodies++;
            refract += !read || differ[0] != 0;
            reflect += !read || differ[1] != 0;

            if ((!read || differ[0] != 0 || differ[1] != 0) &&
                sVerifyRefract + sVerifyReflect + refract + reflect < 40)
            {
                S32 frame = iCaptureFrameCount() + 1;
                printf("tssm: water: check: frame %d, %s %s: the early and the in-scene targets "
                       "differ: refraction %d pixels (by up to %d), reflection %d pixels (by up "
                       "to %d)%s\n",
                       (int)frame, SceneName(), NameOf(sBodies[i].owner), (int)differ[0],
                       (int)most[0], (int)differ[1], (int)most[1], read ? "" : " (not read back)");

                // The first few written out, both of each, to see what moved.
                if (sVerifyDumps < 4)
                {
                    static const char* const kWhat[4] = { "refract_early", "refract_scene",
                                                          "reflect_early", "reflect_scene" };
                    RwRaster* rasters[4] = { iXboxWaterRefractRaster(i),
                                             iXboxWaterRefractRaster(spare),
                                             iXboxWaterReflectRaster(i),
                                             iXboxWaterReflectRaster(spare) };
                    char path[600];
                    char what[64];
                    sVerifyDumps++;

                    for (S32 k = 0; k < 4; k++)
                    {
                        snprintf(what, sizeof(what), "b%d_%s", (int)i, kWhat[k]);
                        DumpPath(path, sizeof(path), frame, what);
                        iXboxWaterWriteRaster(path, rasters[k]);
                    }
                }
            }
        }

        SceneState after;
        TakeState(&after);
        bool same = SameState(after, sEarlyAfter, false);
        U32 before = sVerifyBodies;
        sVerifyBodies += bodies;
        sVerifyRefract += refract;
        sVerifyReflect += reflect;
        sVerifyState += !same;

        if (!same && sVerifyState < 20)
        {
            printf("tssm: water: check: %s: the render state after the early and the in-scene "
                   "targets differs\n",
                   SceneName());
        }

        if (sVerifyBodies / 300 != before / 300)
        {
            printf("tssm: water: check: %u bodies' targets drawn early and again in the scene: "
                   "the refraction differed %u times, the reflection %u, the state after %u; "
                   "%u frames drew them in the scene only\n",
                   (unsigned)sVerifyBodies, (unsigned)sVerifyRefract, (unsigned)sVerifyReflect,
                   (unsigned)sVerifyState, (unsigned)sLateFrames);
            fflush(stdout);
        }
    }

    void DumpTargets(S32 frame)
    {
        char path[600];
        char what[64];
        S32 pairs = iXboxWaterTargetPairs();

        for (S32 i = 0; i < sBodyCount; i++)
        {
            S32 pair = PairFor(i);

            if (pair >= pairs)
            {
                continue;
            }

            snprintf(what, sizeof(what), "b%d_refract", (int)i);
            DumpPath(path, sizeof(path), frame, what);
            bool ok = iXboxWaterWriteRaster(path, iXboxWaterRefractRaster(pair));

            snprintf(what, sizeof(what), "b%d_reflect", (int)i);
            DumpPath(path, sizeof(path), frame, what);
            ok = iXboxWaterWriteRaster(path, iXboxWaterReflectRaster(pair)) && ok;

            printf("tssm: water: dump: frame %d, %s's targets (pair %d)%s%s\n", (int)frame,
                   NameOf(sBodies[i].owner), (int)pair,
                   UseStandIn(i) ? " (out of view: not drawn this frame)" : "",
                   ok ? "" : ": could not be written");
        }

        snprintf(what, sizeof(what), "bump%d", (int)sFrame);
        DumpPath(path, sizeof(path), frame, what);

        if (!iXboxWaterWriteRaster(path, sBumps[sFrame]))
        {
            printf("tssm: water: dump: frame %d, the bump frame could not be written\n",
                   (int)frame);
        }
    }

    // TSSM_WATER_DEBUG=tiles: each tile's outline a little above the water.
    void DrawTileOutlines()
    {
        const S32 kBatch = 1024;
        static RwIm3DVertex verts[kBatch];
        S32 used = 0;

        void* raster = NULL;
        void* ztest = NULL;
        void* zwrite = NULL;
        void* valpha = NULL;
        RwRenderStateGet(rwRENDERSTATETEXTURERASTER, &raster);
        RwRenderStateGet(rwRENDERSTATEZTESTENABLE, &ztest);
        RwRenderStateGet(rwRENDERSTATEZWRITEENABLE, &zwrite);
        RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &valpha);
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);

        for (S32 i = 0; i < sBodyCount; i++)
        {
            const Body& b = sBodies[i];

            for (U32 t = 0; t < b.tileCount; t++)
            {
                const iXboxWaterTile& tile = b.tiles[t];
                U8 r = 0x30, g = 0xFF, bl = 0x30;

                if (tile.flags & iXBOXWATER_TILE_MASKED)
                {
                    r = g = bl = 0x80;
                }
                else if (tile.flags & iXBOXWATER_TILE_CULLED)
                {
                    r = 0xFF;
                    g = bl = 0x30;
                }

                F32 y = tile.y + 0.05f;
                F32 xs[4] = { tile.x0, tile.x0 + tile.w, tile.x0 + tile.w, tile.x0 };
                F32 zs[4] = { tile.z0, tile.z0, tile.z0 + tile.d, tile.z0 + tile.d };

                if (used + 8 > kBatch)
                {
                    if (RwIm3DTransform(verts, used, NULL, rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
                    {
                        RwIm3DRenderPrimitive(rwPRIMTYPELINELIST);
                        RwIm3DEnd();
                    }
                    used = 0;
                }

                for (S32 e = 0; e < 4; e++)
                {
                    S32 f = (e + 1) & 3;
                    RwIm3DVertexSetPos(&verts[used], xs[e], y, zs[e]);
                    RwIm3DVertexSetRGBA(&verts[used], r, g, bl, 0xFF);
                    used++;
                    RwIm3DVertexSetPos(&verts[used], xs[f], y, zs[f]);
                    RwIm3DVertexSetRGBA(&verts[used], r, g, bl, 0xFF);
                    used++;
                }
            }
        }

        if (used > 0 && RwIm3DTransform(verts, used, NULL, rwIM3D_VERTEXXYZ | rwIM3D_VERTEXRGBA))
        {
            RwIm3DRenderPrimitive(rwPRIMTYPELINELIST);
            RwIm3DEnd();
        }

        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE, ztest);
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, zwrite);
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, valpha);
    }

    // The Xbox's handler is empty, so the Visible that B301's WATER_XBOX_DISP
    // sends does nothing. PC: SceneBegin gives the GameCube look
    // (iWaterBody.h) to every body the Xbox water does not take: all of them
    // where it does not run, and where it does, one whose object could not be
    // used (usable is final from xWaterSceneInit, before the first SceneBegin).
    void Event(xBase*, xBase* to, U32 toEvent, const F32*, xBase*, U32)
    {
        if (toEvent == eEventSceneBegin && !((zWaterBody*)to)->usable)
        {
            iWaterBodySceneBegin(*(zWaterBody*)to);
        }
    }
} // namespace

void zWaterBody::load(xBase& data, xDynAsset& asset, size_t size)
{
    // 0x2A120.
    zWaterBody& b = (zWaterBody&)data;
    memset(&b, 0, sizeof(b));
    xBaseInit(&b, &asset);
    b.baseType = eBaseTypeWaterBody;
    b.eventFunc = Event;

    // Untrusted data: a short asset is kept as an object but never used, and
    // links that run past the asset are dropped.
    if (size >= sizeof(asset_type))
    {
        b.asset = (const asset_type*)&asset;
    }

    if (b.linkCount && size >= sizeof(asset_type) + b.linkCount * sizeof(xLinkAsset))
    {
        b.link = (xLinkAsset*)((U8*)&asset + sizeof(asset_type));
    }
    else
    {
        b.linkCount = 0;
    }
}

void zWaterBody::setup()
{
    // 0x2A160. xWaterSceneInit has left out every body past kMaxBodies; the
    // count is checked again for the array's sake.
    if (!usable || sBodyCount == kMaxBodies)
    {
        return;
    }

    body_ent = (xEnt*)zSceneFindObject(asset->body);
    body_ent->chkby |= XENT_COLLTYPE_TRIG;
    refract_ents = LoadEnts(refract_ents_size, asset->facade_refract);
    reflect_ents = LoadEnts(reflect_ents_size, asset->facade_reflect);

    HideEnts(&body_ent, 1);
    HideEnts(refract_ents, refract_ents_size);
    HideEnts(reflect_ents, reflect_ents_size);

    AddBody(this, body_ent->model->Data, *body_ent->model->Mat);
}

void zWaterBody::setup_all()
{
    // 0x2A220, from zGameSetup.
    if (!sActive)
    {
        return;
    }

    FreeBodies(true);

    S32 count;
    zWaterBody* bodies = BodyObjects(&count);

    for (S32 i = 0; i < count; i++)
    {
        bodies[i].setup();
    }

    U32 tiles = 0;
    U32 drawn = 0;
    char detail[512];
    size_t len = 0;
    detail[0] = '\0';

    for (S32 i = 0; i < sBodyCount; i++)
    {
        const Body& b = sBodies[i];
        tiles += b.tileCount;
        drawn += b.drawable;
        int w = snprintf(detail + len, sizeof(detail) - len,
                         "%s%s %ux%u tiles (z by x) of %g units from (%g, %g) at y %.2f, %u "
                         "drawn, %d refract and %d reflect models",
                         i ? "; " : "", NameOf(b.owner), (unsigned)b.layout.rows,
                         (unsigned)b.layout.cols, b.layout.tileW, b.layout.x0, b.layout.z0,
                         b.layout.y, (unsigned)b.drawable, (int)b.owner->refract_ents_size,
                         (int)b.owner->reflect_ents_size);

        if (w > 0 && (size_t)w < sizeof(detail) - len)
        {
            len += (size_t)w;
        }
    }

    printf("tssm: water: %s: %u of %u tiles drawn (%s)\n", SceneName(), (unsigned)drawn,
           (unsigned)tiles, detail);

    for (S32 i = 0; i < sBodyCount; i++)
    {
        LogTransform(sBodies[i], "at setup");
    }
}

void zWaterBody::update_all()
{
    // 0x2A060: the body is hidden again every frame.
    if (!sActive)
    {
        return;
    }

    S32 count;
    zWaterBody* bodies = BodyObjects(&count);

    for (S32 i = 0; i < count; i++)
    {
        xEnt* ent = bodies[i].body_ent;

        if (ent == NULL)
        {
            continue;
        }

        ent->asset->flags &= ~0x1;
        ent->flags &= ~0x1;

        if (ent->visUpdate)
        {
            ent->visUpdate(ent);
        }
    }
}

void zWaterBody::reset_all()
{
    // 0x29F70: after the scene's reset events, the three lists are hidden again.
    if (!sActive)
    {
        return;
    }

    S32 count;
    zWaterBody* bodies = BodyObjects(&count);

    for (S32 i = 0; i < count; i++)
    {
        zWaterBody& b = bodies[i];

        if (b.body_ent == NULL)
        {
            continue;
        }

        b.body_ent->chkby |= XENT_COLLTYPE_TRIG;
        HideEnts(&b.body_ent, 1);
        HideEnts(b.refract_ents, b.refract_ents_size);
        HideEnts(b.reflect_ents, b.reflect_ents_size);
    }
}

void xWaterScenePrepare()
{
    // PC: before the pipe tables are read, so that iXboxWaterWillRun() is
    // there for the GameCube-water fallback's asset step (iXboxWater.h).
    sFileBodies = 0;
    sAssetBodies = 0;
    sWhyNot = NULL;
    sWillRun = false;

    const U32 type = xStrHash("effect:water_body");
    const S32 dynas = xSTAssetCountByType('DYNA');
    char skipped[200];
    size_t len = 0;
    skipped[0] = '\0';

    for (S32 i = 0; i < dynas; i++)
    {
        U32 size = 0;
        const xDynAsset* a = (const xDynAsset*)xSTFindAssetByType('DYNA', i, &size);

        if (a == NULL || size < sizeof(xDynAsset) || a->type != type)
        {
            continue;
        }

        sFileBodies++;
        const char* problem = AssetProblem(a, size);

        if (problem == NULL)
        {
            sAssetBodies++;
            continue;
        }

        const char* name = xSTAssetName(a->id);
        char id[16];
        snprintf(id, sizeof(id), "%08X", (unsigned)a->id);
        ListProblem(skipped, sizeof(skipped), &len, name != NULL && name[0] != '\0' ? name : id,
                    problem);
    }

    if (sFileBodies == 0)
    {
        return;
    }

    // [extras] water as the level's extras decision has it (iExtras.h): the
    // setting (a value that is neither is the default, said once there), then
    // what the files and the renderer decide there.
    if (iExtrasAtLevelLoad(iEXTRAS_WATER) != iEXTRAS_XBOX)
    {
        sWhyNot = "extras.water is gamecube";
    }
    else if (iExtrasInForce(iEXTRAS_WATER) != iEXTRAS_XBOX)
    {
        char why[160];
        iExtrasStatus(iEXTRAS_WATER, why, sizeof(why));
        snprintf(sWhyBuf, sizeof(sWhyBuf), "extras: \"%s\"", why);
        sWhyNot = sWhyBuf;
    }
    else if ((sWhyNot = BumpsMissing()) == NULL && (sWhyNot = RendererMissing()) == NULL &&
             (sWhyNot = MakeBumps()) == NULL)
    {
        // The shaders and the grids' buffers: made the first time, and a
        // failure is the GameCube water from here on.
        sWhyNot = iXboxWaterDrawOpen();
    }

    if (sWhyNot == NULL && sAssetBodies == 0)
    {
        snprintf(sWhyBuf, sizeof(sWhyBuf), "no usable water body (%s)", skipped);
        sWhyNot = sWhyBuf;
    }

    if (sWhyNot == NULL)
    {
        // xWaterSceneEnter (0x1AD1D0) makes the targets in every scene; here
        // only where the Xbox water runs, a pair a body when they are drawn
        // early, and here so that iXboxWaterWillRun() is final.
        S32 bodies = sAssetBodies < kMaxBodies ? sAssetBodies : kMaxBodies;
        sPairsWanted = (Early() ? bodies : 1) + (Verify() ? 1 : 0);

        if (!iXboxWaterTargetsMake(sPairsWanted))
        {
            sWhyNot = "the refraction and reflection targets could not be made";
        }
    }

    sWillRun = sWhyNot == NULL;
}

bool iXboxWaterWillRun()
{
    return sWillRun;
}

void xWaterSceneInit()
{
    // 0x1AD160 (the bodies are forgotten), then the port's decision.
    FreeBodies(false);
    sActive = false;
    sTargetsLost = false;
    sLtmLogged = false;
    sSurfaceLogged = false;
    sSurfaceFrames = 0;
    sMetValid = false;
    sEarlyPending = false;
    sStandInDraws = 0;
    sPreset = PresetFor(globals.sceneCur->sceneID);

    if (sFileBodies <= 0)
    {
        // No water body here: the last level's answer is not this one's.
        iXboxWaterSetStatus(NULL);
        return;
    }

    if (!sWillRun)
    {
        printf("tssm: water: GameCube water in %s -- %s; %s\n", SceneName(), sWhyNot,
               iWaterBodyPrepared());
        snprintf(sStatus, sizeof(sStatus), "Now: GameCube water in %s -- %s.", SceneName(),
                 sWhyNot);
        iXboxWaterSetStatus(sStatus);
        return;
    }

    S32 count;
    zWaterBody* bodies = BodyObjects(&count);
    S32 usable = 0;
    char skipped[256];
    size_t len = 0;
    skipped[0] = '\0';

    char tooMany[64];
    snprintf(tooMany, sizeof(tooMany), "the level has more than %d water bodies", (int)kMaxBodies);

    for (S32 i = 0; i < count; i++)
    {
        const char* problem = BodyProblem(bodies[i]);

        if (problem == NULL && usable == kMaxBodies)
        {
            problem = tooMany;
        }

        bodies[i].usable = problem == NULL;

        if (problem == NULL)
        {
            usable++;
            continue;
        }

        ListProblem(skipped, sizeof(skipped), &len, NameOf(&bodies[i]), problem);
    }

    if (usable == 0)
    {
        // Only modded files get here: the assets checked out in
        // xWaterScenePrepare, the objects do not (iXboxWater.h says what the
        // fallback does then).
        iXboxWaterTargetsFree();
        printf("tssm: water: GameCube water in %s -- no usable water body (%s); the GameCube "
               "look from SceneBegin, pipe settings and surfaces as in the files\n",
               SceneName(), skipped);
        snprintf(sStatus, sizeof(sStatus),
                 "Now: GameCube water in %s -- its water body is not usable.", SceneName());
        iXboxWaterSetStatus(sStatus);
        return;
    }

    sActive = true;
    snprintf(sStatus, sizeof(sStatus), "Now: Xbox water in %s (%d water area%s).", SceneName(),
             (int)usable, usable == 1 ? "" : "s");
    iXboxWaterSetStatus(sStatus);
    printf("tssm: water: Xbox water in %s: %d bod%s%s%s%s; targets drawn %s\n", SceneName(),
           (int)usable, usable == 1 ? "y" : "ies",
           len ? " (left out, the GameCube look from SceneBegin with the files' pipe settings "
                 "and surfaces: " :
                 "",
           skipped, len ? ")" : "",
           Early() ?
               (Verify() ? "before the scene, and again in it to compare" : "before the scene") :
               "in the scene, as on the Xbox");
}

bool iXboxWaterActive()
{
    return sActive;
}

void xWaterUpdate(F32 dt)
{
    // 0x1AD190: the clock, then each tile's cull against the camera
    // (0x1ACFE0): beyond the level's distance, or outside the view.
    if (!sActive)
    {
        return;
    }

    sFrame = iXboxWaterClockStep(&sClock, sPreset->speed, dt);

    const xVec3& eye = zCamGetMatrix().pos;
    RwCamera* cam = globals.screen != NULL ? globals.screen->icam : NULL;

    for (S32 i = 0; i < sBodyCount; i++)
    {
        Body& b = sBodies[i];
        b.inView = false;

        for (U32 t = 0; t < b.tileCount; t++)
        {
            iXboxWaterTile& tile = b.tiles[t];
            tile.flags &= ~iXBOXWATER_TILE_CULLED;

            F32 dx = tile.cx - eye.x;
            F32 dy = tile.cy - eye.y;
            F32 dz = tile.cz - eye.z;

            if (dx * dx + dy * dy + dz * dz > sPreset->grid.cullDist2)
            {
                tile.flags |= iXBOXWATER_TILE_CULLED;
                continue;
            }

            if (cam != NULL)
            {
                RwSphere sphere;
                sphere.center.x = tile.cx;
                sphere.center.y = tile.cy;
                sphere.center.z = tile.cz;
                sphere.radius = tile.radius;

                if (RwCameraFrustumTestSphere(cam, &sphere) == rwSPHEREOUTSIDE)
                {
                    tile.flags |= iXBOXWATER_TILE_CULLED;
                    continue;
                }
            }

            if (!(tile.flags & iXBOXWATER_TILE_MASKED))
            {
                b.inView = true;
            }
        }
    }
}

void xWaterPreRender(RwCamera* cam)
{
    // The targets drawn before the scene: from this frame's camera, under the
    // render state the Xbox's place had in the frame before. Everything the
    // passes change is put back, so the scene starts as it would have.
    sEarlyPending = false;

    if (!sActive || sTargetsLost || cam == NULL)
    {
        return;
    }

    // The render size can change while a level runs; the targets follow it
    // here, where no camera is in an update. If they cannot be made again,
    // the level goes on without its water (its bodies stay hidden) rather
    // than trying every frame.
    if (!iXboxWaterTargetsMake(sPairsWanted))
    {
        sTargetsLost = true;
        printf("tssm: water: %s: the targets could not be made again for a %dx%d render size; "
               "no water until the next level\n",
               SceneName(), (int)iScreenWidth(), (int)iScreenHeight());
        snprintf(sStatus, sizeof(sStatus),
                 "Now: no water in %s -- its targets could not be made again; it comes back "
                 "when a level loads.",
                 SceneName());
        iXboxWaterSetStatus(sStatus);
        return;
    }

    if (!Early() || !sMetValid || sBodyCount == 0 || RwCameraGetCurrentCamera() != NULL ||
        globals.currWorld == NULL || !iDeviceUsable())
    {
        return;
    }

    sEarlyEpoch = iDeviceResetEpoch();

    SceneState before;
    TakeState(&before);
    RpWorld* world = globals.currWorld;
    xLightKit* kit = xLightKit_GetCurrent(world);

    PutState(sMet, true);

    for (S32 i = 0; i < sBodyCount; i++)
    {
        DrawTargets(i, PairFor(i), cam);
    }

    TakeState(&sEarlyAfter);
    sEarlyMet = sMet;
    TakeCamera(cam, &sEarlyCamera);
    sEarlyPending = true;

    globals.currWorld = world;
    xLightKit_Enable(kit, world);
    PutState(before, false);
}

void xWaterRender(RwCamera* cam)
{
    // 0x1AD760: for each body, its two targets, then its surface.
    if (!sActive || sTargetsLost)
    {
        return;
    }

    if (!sLtmLogged)
    {
        sLtmLogged = true;

        for (S32 i = 0; i < sBodyCount; i++)
        {
            LogTransform(sBodies[i], "at the first draw");
        }
    }

    // The state the passes meet here, for the next frame's early pass.
    SceneState met;
    TakeState(&met);

    bool early = sEarlyPending;
    sEarlyPending = false;

    CameraKey now;
    TakeCamera(cam, &now);
    const bool sameCamera = SameCamera(now, sEarlyCamera);
    const bool sameDevice = iDeviceResetEpoch() == sEarlyEpoch;

    if (early && SameState(met, sEarlyMet, true) && sameCamera && sameDevice)
    {
        sEarlyFrames++;

        if (Verify())
        {
            VerifyEarly(cam);
        }
        else
        {
            // What the passes would have left here: their last state, and
            // the scene's world without a light kit (begin_render's
            // xLightKit_Enable(NULL, ...)).
            PutState(sEarlyAfter, false);
            xLightKit_Enable(NULL, globals.currWorld);
        }

        for (S32 i = 0; i < sBodyCount; i++)
        {
            DrawSurface(i, PairFor(i), cam);
        }
    }
    else
    {
        if (early)
        {
            sLateFrames++;

            if (sLateFrames <= 3 || sLateFrames % 1000 == 0)
            {
                printf("tssm: water: %s: the targets drawn again in the scene, the %s since "
                       "they were drawn early (%u times; %u frames used the early ones)\n",
                       SceneName(),
                       !sameDevice ? "Direct3D 9 device lost or reset" :
                       !sameCamera ? "camera moved" :
                                     "render state or fog changed",
                       (unsigned)sLateFrames, (unsigned)sEarlyFrames);
            }
        }

        // In the Xbox's order: a body's surface reads its pair before the
        // next body's targets are drawn, which with one pair (TSSM_WATER_EARLY
        // =0) is into the same two.
        for (S32 i = 0; i < sBodyCount; i++)
        {
            DrawTargets(i, PairFor(i), cam);
            DrawSurface(i, PairFor(i), cam);
        }
    }

    sMet = met;
    sMetValid = true;

    if (sSurfaceLogged)
    {
        sSurfaceFrames++;

        if (sSurfaceFrames == 1 || sSurfaceFrames == 600)
        {
            LogSlots(sSurfaceFrames == 1 ? "at the surface's first frame" : "600 frames later");
        }
    }

    if (DebugTiles())
    {
        DrawTileOutlines();
    }

    if (DebugRt())
    {
        iXboxWaterTargetsDebugDraw(Early() ? sBodyCount : 1, sBumps[sFrame]);
    }

    S32 frame = iCaptureFrameCount() + 1;

    if (DumpFrame(frame))
    {
        DumpTargets(frame);
    }
}

void xWaterSceneExit()
{
    // 0x1AD210.
    if (sActive && Early() && sEarlyFrames + sLateFrames > 0)
    {
        printf("tssm: water: %s: the targets came from before the scene in %u frames and were "
               "drawn again in the scene in %u\n",
               SceneName(), (unsigned)sEarlyFrames, (unsigned)sLateFrames);
    }

    if (sActive && sStandInDraws > 0)
    {
        printf("tssm: water: %s: a body out of view drew its targets into the stand-in %u "
               "times\n",
               SceneName(), (unsigned)sStandInDraws);
    }

    sEarlyFrames = 0;
    sLateFrames = 0;
    sStandInDraws = 0;
    FreeBodies(true);
    iXboxWaterTargetsFree();
    sActive = false;
    sTargetsLost = false;
    sFileBodies = 0;
    sAssetBodies = 0;
    sWhyNot = NULL;
    sWillRun = false;
    sMetValid = false;
    sEarlyPending = false;
}
