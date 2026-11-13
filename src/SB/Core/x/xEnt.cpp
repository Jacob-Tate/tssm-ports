#include "xEnt.h"

#include "xClumpColl.h"
#include "xMath.h"
#include "xShadow.h"

#include "zDestructible.h"
#include "zEntDestructObj.h"
#include "zGrid.h"
#include "zLight.h"
#include "zPlatform.h"
#include "zSurface.h"

#include "iCollide.h"
#include "iModel.h"

// Global in the PS2 DWARF, dead-stripped from every build. Its "" initializer is still in the
// wad's string pool here.
char* g_xEnt_FMT = "";
static F32 nsn_angle = DEG2RAD(30);
static xBox all_ents_box;
static S32 all_ents_box_init;

static S32 setMaterialTextureRestore;

static void xEntCollCheckOneGrid(xEnt* p, xScene* sc, xEnt* (*hitIt)(xEnt*, xScene*, void*),
                                 xGrid* grid);

namespace
{
    namespace anim_coll
    {
        void reset(xEnt& ent)
        {
            if (!ent.anim_coll)
            {
                ent.anim_coll = (xEnt::anim_coll_data*)xMemAllocSize(sizeof(xEnt::anim_coll_data));
                ent.anim_coll->flags = 0;
                ent.anim_coll->verts = NULL;
            }

            xModelInstance& model = *ent.model;
            xMat4x3& mat = *(xMat4x3*)model.Mat;
            xEnt::anim_coll_data& ac = *ent.anim_coll;

            if (!(ac.flags & 0x8))
            {
                switch (model.BoneCount)
                {
                case 1:
                {
                    ac.flags |= 0x1;
                    ac.old_mat = mat;
                    ac.new_mat = g_I3;
                    break;
                }
                case 0:
                {
                    break;
                }
                default:
                {
                    ac.flags |= 0x2;
                    ac.old_mat = mat;
                    ac.new_mat = g_I3;

                    xModelAnimCollStart(model);

                    xBox& box = ent.bound.box.box;
                    xVec3 size = box.upper - box.lower;
                    F32 max_size = size.x;

                    if (max_size < size.y)
                    {
                        max_size = size.y;
                    }

                    if (max_size < size.z)
                    {
                        max_size = size.z;
                    }

                    max_size += 1.0f;

                    box.upper += max_size;
                    box.lower -= max_size;

                    model.Data->boundingSphere.radius *= 3.0f;
                }
                }
            }
        }

        inline void refresh(xEnt& ent)
        {
            xEnt::anim_coll_data& ac = *ent.anim_coll;
            xMat4x3& bone_mat = *(xMat4x3*)(ent.model->Mat + 1);

            xMat4x3Mul((xMat4x3*)ent.model->Mat, &bone_mat, &ac.old_mat);

            ac.new_mat = bone_mat;
            bone_mat = g_I3;
        }

        inline void pre_move(xEnt& ent)
        {
            xMat4x3& mat = *(xMat4x3*)ent.model->Mat;
            xEnt::anim_coll_data& ac = *ent.anim_coll;

            ent.frame->mat = mat = ac.old_mat;
        }

        inline void post_move(xEnt& ent)
        {
            xMat4x3& mat = *(xMat4x3*)ent.model->Mat;
            xEnt::anim_coll_data& ac = *ent.anim_coll;

            ac.old_mat = ent.frame->mat;

            xMat4x3Mul(&mat, &ac.new_mat, &ac.old_mat);

            ent.frame->mat = mat;
        }
    } // namespace anim_coll

    void drop_stacked_entity(xEnt& ent);
    void dismount_stacked_entity(xEnt& ent);
    inline void drop_driven_stacked_entity(xEnt& ent);
    void stacked_owner_destroyed(zDestructible*, void* context);
} // namespace

void xEntGrowGlobalBoundingBox(const xVec3* pos);

void xEntSceneInit()
{
    all_ents_box_init = 1;
}

void xEntSceneExit()
{
}

void xEntAddHittableFlag(xEnt* ent)
{
    if (ent->baseType == eBaseTypeNPC || ent->baseType == eBaseTypeDestructObj ||
        ent->baseType == eBaseTypeButton || ent->baseType == eBaseTypeBoulder ||
        (ent->baseType == eBaseTypePlatform && ent->subType == ZPLATFORM_SUBTYPE_PADDLE))
    {
        ent->moreFlags |= 0x10;
    }
    else
    {
        for (U32 i = 0; i < ent->linkCount; i++)
        {
            if (ent->link[i].srcEvent == eEventHit || ent->link[i].srcEvent == eEventHit_Cruise ||
                ent->link[i].srcEvent == eEventHit_Melee ||
                ent->link[i].srcEvent == eEventHit_BubbleBounce ||
                ent->link[i].srcEvent == eEventHit_BubbleBash ||
                ent->link[i].srcEvent == eEventHit_BubbleBowl ||
                ent->link[i].srcEvent == eEventHit_PatrickSlam ||
                ent->link[i].srcEvent == eEventHit_Throw ||
                ent->link[i].srcEvent == eEventHit_PaddleLeft ||
                ent->link[i].srcEvent == eEventHit_PaddleRight ||
                ent->link[i].srcEvent == eEventHit_Cartwheel)
            {
                ent->moreFlags |= 0x10;
                break;
            }
        }
    }
}

static inline void hack_receive_shadow(xEnt* ent)
{
    static U32 receive_models[15] = {
        0x923EE315, // xStrHash("db03_path_a")
        0x923EE316, // xStrHash("db03_path_b")
        0x923EE317, // xStrHash("db03_path_c")
        0x923EE318, // xStrHash("db03_path_d")
        0x923EE319, // xStrHash("db03_path_e")
        0x923EE31A, // xStrHash("db03_path_f")
        0x923EE31B, // xStrHash("db03_path_g")
        0x923EE31C, // xStrHash("db03_path_h")
        0x923EE31D, // xStrHash("db03_path_i")
        0x923EE31E, // xStrHash("db03_path_j")
        0x923EE31F, // xStrHash("db03_path_k")
        0x923EE320, // xStrHash("db03_path_l")
        0x923EE321, // xStrHash("db03_path_m"), no db03_path_n
        0x923EE323, // xStrHash("db03_path_o")
        0x923EE324, // xStrHash("db03_path_p")
    };

    U32* end = receive_models + sizeof(receive_models) / sizeof(U32);
    U32* cur = receive_models;

    while (cur != end)
    {
        if (ent->asset->modelInfoID == *cur)
        {
            ent->baseFlags |= 0x10;
            ent->asset->baseFlags |= 0x10;
            break;
        }

        cur++;
    }
}

static inline void xEntAddShadowRecFlag(xEnt* ent)
{
    switch (ent->baseType)
    {
    case eBaseTypePlatform:
    case eBaseTypeStatic:
    case eBaseTypeButton:
    case eBaseTypeDestructObj:
    case eBaseTypeNPC:
    case eBaseTypeBoulder:
    {
        if (ent->model && ent->model->Pipe.Flags & 0x0000ff00)
        {
            ent->baseFlags &= 0xffef;
        }
        break;
    }
    default:
    {
        ent->baseFlags &= 0xffef;
        break;
    }
    }

    hack_receive_shadow(ent);
}

// The xEntGrowGlobalBoundingBox call compiles identically to the written-out body here, but only
// the call lets retail's inlining of xEntInit into xEntBoulder_Init happen.
void xEntInit(xEnt* ent, xEntAsset* asset)
{
    xBaseInit(ent, asset);

    ent->asset = asset;
    ent->update = xEntUpdate;
    ent->bupdate = xEntDefaultBoundUpdate;
    ent->render = xEntRender;
    ent->move = NULL;
    ent->transl = xEntDefaultTranslate;
    ent->flags = asset->flags;
    ent->miscflags = 0;
    ent->moreFlags = asset->moreFlags;
    ent->subType = asset->subtype;
    ent->pflags = asset->pflags;
    ent->ffx = NULL;
    ent->num_ffx = 0;
    ent->driver = NULL;
    ent->driven = NULL;
    ent->model = NULL;
    ent->collModel = NULL;
    ent->camcollModel = NULL;
    ent->frame = NULL;
    ent->collis = NULL;
    ent->simpShadow = NULL;
    ent->entShadow = NULL;
    ent->baseFlags |= (U16)0x20;

    xGridBoundInit(&ent->gridb, ent);

    ent->anim_coll = NULL;

    xEntGrowGlobalBoundingBox(&asset->pos);
}

void xEntGrowGlobalBoundingBox(const xVec3* pos)
{
    if (all_ents_box_init)
    {
        iBoxInitBoundVec(&all_ents_box, pos);
        all_ents_box_init = 0;
    }
    else
    {
        iBoxBoundVec(&all_ents_box, &all_ents_box, pos);
    }
}

void xEntInitForType(xEnt* ent)
{
    ent->update = xEntUpdate;
    ent->render = xEntRender;

    switch (ent->collType)
    {
    case XENT_COLLTYPE_TRIG:
        ent->pflags &= ~0x3;
        ent->chkby = XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR;
        ent->penby = 0;
        break;
    case XENT_COLLTYPE_STAT:
        ent->pflags &= ~0x3;
        ent->chkby = XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR;
        ent->penby = XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR;
        break;
    case XENT_COLLTYPE_DYN:
        ent->pflags |= 0x1;
        ent->move = NULL;
        ent->frame = (xEntFrame*)xMemAllocSize(sizeof(xEntFrame));
        memset(ent->frame, 0, sizeof(xEntFrame));
        ent->pflags &= (U8)~0x2;
        ent->chkby = XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR;
        ent->penby = XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR;
        break;
    case XENT_COLLTYPE_NPC:
        ent->pflags |= 0x1;
        ent->move = NULL;
        ent->pflags |= 0x2;
        ent->chkby = XENT_COLLTYPE_PLYR;
        ent->penby = XENT_COLLTYPE_PLYR;
        break;
    case XENT_COLLTYPE_PLYR:
        ent->pflags |= 0x1;
        ent->move = NULL;
        ent->frame = (xEntFrame*)xMemAllocSize(sizeof(xEntFrame));
        memset(ent->frame, 0, sizeof(xEntFrame));
        ent->pflags |= 0x2;
        ent->chkby = 0;
        ent->penby = 0;
        ent->collis = (xEntCollis*)xMemAllocSize(sizeof(xEntCollis));
        ent->collis->chk = 0x2F;
        ent->collis->pen = 0x2E;
        ent->collis->post = NULL;
        ent->collis->depenq = NULL;
        break;
    }

    if ((ent->moreFlags & 0x20 || ent->flags & 0x2) && !ent->frame)
    {
        ent->frame = (xEntFrame*)xMemAllocSize(sizeof(xEntFrame));
        memset(ent->frame, 0, sizeof(xEntFrame));
    }

    // A U16 flag constant like Heavy Iron's k_XBASE_IS_ENTITY; a plain int 0x20 swaps the
    // epilogue's restores.
    ent->baseFlags |= (U16)0x20;
}

namespace
{
    void stacked_owner_destroyed(zDestructible*, void* context)
    {
        drop_stacked_entity(*(xEnt*)context);
    }

    inline void drop_driven_stacked_entity(xEnt& ent)
    {
        if (ent.driven && ent.driven->driver)
        {
            ent.driven->driver->driving_count--;
            ent.driven->driver = NULL;
        }

        if (ent.driven)
        {
            ent.driven->pflags = 0x4;

            // Retail bug: this walks back down to the driver, which was just cleared,
            // rather than up to the entity stacked on top of ent.driven.
            if (ent.driven->driver)
            {
                drop_driven_stacked_entity(*ent.driven->driver);
            }
        }
    }

    void dismount_stacked_entity(xEnt& ent)
    {
        if (ent.driver)
        {
            zDestructible* durst = zDestructibleGetFromEntity(ent.driver);

            if (durst)
            {
                durst->destroy_notify = NULL;
                durst->notify_context = NULL;
            }

            if (ent.driver->driving_count)
            {
                ent.driver->driving_count--;
            }

            ent.driver = NULL;

            drop_driven_stacked_entity(ent);
        }
    }

    void drop_stacked_entity(xEnt& ent)
    {
        ent.pflags = 0x4;

        dismount_stacked_entity(ent);
    }

    inline F32 get_lower_extent(const xBound& bound)
    {
        switch (bound.type)
        {
        case XBOUND_TYPE_SPHERE:
        {
            return bound.sph.r;
        }
        case XBOUND_TYPE_BOX:
        {
            return bound.box.center.y - bound.box.box.lower.y;
        }
        case XBOUND_TYPE_OBB:
        {
            if (0.0f == bound.mat->up.x && 0.0f == bound.mat->up.z)
            {
                return bound.box.center.y -
                       (bound.mat->up.y * bound.box.box.lower.y + bound.mat->pos.y);
            }
            else
            {
                xBox box;
                xBoundGetBox(box, bound);

                return bound.box.center.y - box.lower.y;
            }
        }
        case XBOUND_TYPE_CYL:
        default:
        {
            return 0.0f;
        }
        }
    }

    inline bool collide_downward(xVec3& loc, xEnt*& hit, xScene& s, xEnt& ent, F32 max_dist)
    {
        F32 lower_bound_extent = get_lower_extent(ent.bound);
        xVec3& center = *xBoundCenter(&ent.bound);
        xRay3 ray;
        U8 old_bound_type;
        F32 old_bound_radius;
        xCollis coll;

        ray.origin = center;
        ray.dir.assign(0.0f, -1.0f, 0.0f);
        ray.min_t = 0.0f;
        ray.max_t = max_dist + lower_bound_extent;
        ray.flags = 0xC00;

        old_bound_type = ent.bound.type;
        old_bound_radius = ent.bound.sph.r;

        ent.bound.type = XBOUND_TYPE_SPHERE;
        ent.bound.sph.r = 0.0f;

        // 1000, not FLOAT_MAX: retail and the PS2 demo both load 1000.0f here.
        center.y = 1000.0f;

        coll.flags = 0x100;

        xRayHitsSceneFlags(&s, &ray, &coll, XENT_COLLTYPE_PLYR, 0x26);

        ent.bound.type = old_bound_type;
        ent.bound.sph.r = old_bound_radius;

        center.y = ray.origin.y;

        if (coll.flags & 0x1)
        {
            loc = *(xVec3*)&ent.model->Mat->pos;
            loc.y -= coll.dist - lower_bound_extent;

            hit = (xEnt*)coll.optr;

            return true;
        }

        return false;
    }

    inline void setup_stacked_entity(xEnt& ent)
    {
        ent.pflags = 0x4;
    }

    inline void mount_stacked_entity(xEnt& ent, xEnt& driver)
    {
        if (ent.driver)
        {
            ent.driver->driving_count--;
        }

        ent.driver = &driver;
        driver.driving_count++;
        driver.driven = &ent;

        if (driver.pflags & 0x4)
        {
            setup_stacked_entity(*driver.driven);
        }

        zDestructible* durst = zDestructibleGetFromEntity(&driver);

        if (durst)
        {
            durst->destroy_notify = stacked_owner_destroyed;
            durst->notify_context = &ent;
        }
    }

    inline void stop_stacked_entity(xEnt& ent)
    {
        ent.pflags = 0;
    }

    void update_stacked_entity(xScene& s, xEnt& ent, F32 dt)
    {
        if (ent.driver && ent.driver->pflags & 0x4)
        {
            return;
        }

        xEntApplyPhysics(&ent, &s, dt);
        xEntMove(&ent, &s, dt);

        F32 dist = ent.model->Mat->pos.y - ent.frame->mat.pos.y;
        xVec3 hitloc;
        xEnt* hitent;

        if (!(dist <= 0.0f) && collide_downward(hitloc, hitent, s, ent, dist))
        {
            ent.frame->mat.pos.y = hitloc.y;
            stop_stacked_entity(ent);

            if (hitent)
            {
                mount_stacked_entity(ent, *hitent);
            }
        }
    }
} // namespace

void xEntSetup(xEnt* ent)
{
    xSurface* surf;
    xModelInstance* minst;
    S32 i;
    xLinkAsset* la;
    xEnt* dent;

    xBaseSetup(ent);

    ent->baseFlags |= (U16)0x20;

    if (ent->asset->surfaceID)
    {
        surf = (xSurface*)xSceneResolvID(g_xSceneCur, ent->asset->surfaceID);

        if (surf)
        {
            surf->type = XSURFACE_TYPE_1;
            surf->ent = ent;

            minst = ent->model;

            while (minst)
            {
                minst->Surf = surf;
                minst = minst->Next;
            }
        }
    }

    for (i = 0; i < ent->linkCount; i++)
    {
        la = &ent->link[i];

        if (la->dstEvent == eEventDrivenby)
        {
            dent = (xEnt*)xSceneResolvID(g_xSceneCur, la->dstAssetID);

            if (dent)
            {
                if (!dent->frame)
                {
                    dent->collType = XENT_COLLTYPE_DYN;
                    xEntInitForType(dent);
                }

                ent->driver = dent;
                ent->driveMode = (S32)la->param[0];

                dent->driving_count++;
            }
        }
    }

    ent->model->RedMultiplier = ent->asset->redMult;
    ent->model->GreenMultiplier = ent->asset->greenMult;
    ent->model->BlueMultiplier = ent->asset->blueMult;
    ent->model->Alpha = ent->asset->seeThru;

    xEntAddHittableFlag(ent);
    xEntAddShadowRecFlag(ent);

    zCollGeom_EntSetup(ent);

    if (ent->model)
    {
        if (ent->bound.type == XBOUND_TYPE_BOX)
        {
            xBoxForModel(ent->bound.box.box, ent->collModel ? ent->collModel : ent->model, false);
        }
        else if (ent->bound.type == XBOUND_TYPE_OBB)
        {
            xBoxForModelLocal(ent->bound.box.box, ent->collModel ? ent->collModel : ent->model,
                              false);
        }
    }

    if (ent->moreFlags & 0x20)
    {
        anim_coll::reset(*ent);
    }

    if (ent->flags & 0x2)
    {
        setup_stacked_entity(*ent);
    }
}

void xEntSave(xEnt* ent, xSerial* s)
{
    xBaseSave(ent, s);

    if (ent->flags & 0x1)
    {
        s->Write_b1(1);
    }
    else
    {
        s->Write_b1(0);
    }
}

void xEntLoad(xEnt* ent, xSerial* s)
{
    S32 b;

    xBaseLoad(ent, s);

    b = 0;
    s->Read_b1(&b);

    if (b)
    {
        xEntShow(ent);
    }
    else
    {
        xEntHide(ent);
    }
}

void xEntReset(xEnt* ent)
{
    xMat4x3 frame;
    xModelInstance* minst;
    xBox* combinedAnimBound;
    U16 old_flags;

    xBaseReset(ent, ent->asset);

    ent->baseFlags |= (U16)0x20;
    ent->flags = ent->asset->flags;
    ent->miscflags = 0;
    ent->moreFlags = ent->asset->moreFlags;

    xEntAddHittableFlag(ent);
    xEntAddShadowRecFlag(ent);

    xMat3x3Euler(&frame, ent->asset->ang.x, ent->asset->ang.y, ent->asset->ang.z);

    xVec3SMulBy(&frame.right, ent->asset->scale.x);
    xVec3SMulBy(&frame.up, ent->asset->scale.y);
    xVec3SMulBy(&frame.at, ent->asset->scale.z);
    xVec3Copy(&frame.pos, &ent->asset->pos);

    frame.flags = 0;

    if (ent->model)
    {
        xModelSetFrame(ent->model, &frame);

        if (ent->collModel)
        {
            xModelSetFrame(ent->collModel, &frame);
        }

        if (ent->bound.type == XBOUND_TYPE_BOX)
        {
            xBoxForModel(ent->bound.box.box, ent->collModel ? ent->collModel : ent->model, false);
        }
        else if (ent->bound.type == XBOUND_TYPE_OBB)
        {
            xBoxForModelLocal(ent->bound.box.box, ent->collModel ? ent->collModel : ent->model,
                              false);
        }

        if (ent->moreFlags & 0x20)
        {
            anim_coll::reset(*ent);
        }

        minst = ent->model;

        while (minst)
        {
            minst->RedMultiplier = ent->asset->redMult;
            minst->GreenMultiplier = ent->asset->greenMult;
            minst->BlueMultiplier = ent->asset->blueMult;

            minst->Alpha = minst->Data->geometry->matList.materials[0]->color.alpha / 255.0f;
            minst->Scale.x = 0.0f;
            minst->Scale.y = 0.0f;
            minst->Scale.z = 0.0f;

            minst = minst->Next;
        }

        minst = ent->model;
        combinedAnimBound = &minst->combinedAnimBound;
        combinedAnimBound->upper.x = -1e38f;

        while (minst)
        {
            old_flags = minst->Flags;
            minst->Flags |= 0x1;

            xModelEvalSingle(minst, combinedAnimBound);

            minst->Flags = old_flags;
            minst = minst->Next;
        }
    }

    if (ent->frame)
    {
        xMat4x3Copy(&ent->frame->mat, &frame);

        ent->frame->oldmat = ent->frame->mat;

        xVec3Copy(&ent->frame->dpos, &g_O3);
        xVec3Copy(&ent->frame->dvel, &g_O3);
        xVec3Copy(&ent->frame->vel, &g_O3);
        xVec3Copy(&ent->frame->oldvel, &g_O3);
        xVec3Copy(&ent->frame->rot.axis, &ent->asset->ang);

        ent->frame->rot.angle = 0.0f;

        xRotCopy(&ent->frame->oldrot, &ent->frame->rot);
    }

    if (ent->bupdate && ent->model)
    {
        ent->bupdate(ent, (xVec3*)&ent->model->Mat->pos);
    }

    if (ent->flags & 0x2)
    {
        ent->pflags = 0x4;
    }

    if (ent->baseType == eBaseTypeStatic && ent->visUpdate)
    {
        ent->visUpdate(ent);
    }
}

xModelInstance* xEntLoadModel(xEnt* ent, RpAtomic* imodel, xModelInstance* reuse_instance)
{
    xModelInstance* model;

    model = xModelInstanceAllocReuse(imodel, ent, 0, 0, NULL, reuse_instance);

    while (imodel = iModelFile_RWMultiAtomic(imodel))
    {
        xModelInstanceAttach(xModelInstanceAlloc(imodel, ent, 0x8, 0, NULL), model);
    }

    if (ent)
    {
        ent->model = model;
    }

    return model;
}

// Dead-stripped placeholder for six strings that retail's string pool keeps with no reference
// (BfBB's xEnt.cpp has the same set). They belong to stripped model and animation loaders whose
// bodies are unknown.
static const char* __deadstripped_xEnt()
{
    return "idle\0"
           "anim-file\0"
           "anim-table\0"
           "%s.SKA\0"
           "ent-table\0"
           "%s.DFF";
}

void xEntSetupPipeline(xModelInstance* model)
{
    xEntSetupPipeline(model->Surf, model->Data);
}

// No TSSM symbol (inlined); name from the Ratatouille GC DWARF. group_idx is read into a local
// before the NULL test, as retail does.
static inline RwTexture* LocateSurfTexture_TexAnim(zSurfaceProps* surfProp, S32 idx_pass)
{
    RwTexture* txtr = NULL;
    U32 idx_grpItem = surfProp->texanim[idx_pass].group_idx;

    if (surfProp->texanim[idx_pass].txtr_animList)
    {
        txtr = surfProp->texanim[idx_pass].txtr_animList[idx_grpItem];
    }

    return txtr;
}

void xEntSetupPipeline(xSurface* surf, RpAtomic* model)
{
    setMaterialTextureRestore = 0;

    S32 setPipeline = 0;
    zSurfaceProps* pp = NULL;
    RwTexture* texptr = NULL;

    if (surf && surf->moprops)
    {
        pp = (zSurfaceProps*)surf->moprops;

        if (pp->texanim_flags & 0x1)
        {
            texptr = LocateSurfTexture_TexAnim(pp, 0);

            if (texptr)
            {
                iModelSetMaterialTexture(model, texptr);
                setMaterialTextureRestore = 1;
            }
        }

        texptr = NULL;

        if (pp->texanim_flags & 0x2)
        {
            texptr = LocateSurfTexture_TexAnim(pp, 1);
        }
        else if (pp->uvfx_flags & 0x2)
        {
            texptr = pp->txtr_matFXDualMap;
        }

        xFXanimUV2PSetTexture(texptr);

        if (texptr)
        {
            setPipeline = 1;
        }

        if (pp->uvfx_flags & 0x3)
        {
            setPipeline = 1;

            xFXanimUVSetTranslation(&pp->uvfx[0].trans);
            xFXanimUV2PSetTranslation(&pp->uvfx[1].trans);
            xFXanimUVSetScale(&pp->uvfx[0].scale);
            xFXanimUV2PSetScale(&pp->uvfx[1].scale);
            xFXanimUVSetAngle((PI / 180.0f) * pp->uvfx[0].rot);
            xFXanimUV2PSetAngle((PI / 180.0f) * pp->uvfx[1].rot);
        }

        if (setPipeline)
        {
            xFXanimUVAtomicSetup(model, (pp->uvfx_flags >> 1) & 1);
        }
    }
}

void xEntRestorePipeline(xModelInstance* model)
{
    xEntRestorePipeline(model->Surf, model->Data);
}

void xEntRestorePipeline(xSurface*, RpAtomic* model)
{
    if (setMaterialTextureRestore)
    {
        iModelResetMaterial(model);
        setMaterialTextureRestore = 0;
    }
}

void xEntRender(xEnt* ent)
{
    S32 shadowOutside;

    if (ent->model && xEntIsVisible(ent) && !(ent->model->Flags & 0x400))
    {
        ent->_isCulled = 0;

        if (ent->baseType == eBaseTypePlayer ||
            (ent->baseType == eBaseTypeNPC && !(ent->flags & 0x40)))
        {
            if (xModelCullGroupPlusShadow(ent->model, 10.0f, &shadowOutside))
            {
                if (shadowOutside)
                {
                    ent->_isCulled = 1;
                    return;
                }
                else
                {
                    // goto as in BfBB; without it xModelRender needs two calls.
                    goto postrender;
                }
            }
        }
        else
        {
            if (xModelCullGroup(ent->model))
            {
                ent->_isCulled = 1;
                return;
            }
        }

        xModelRender(ent->model);

    postrender:

        if ((ent->baseType == eBaseTypeNPC && ent->entShadow && !(ent->flags & 0x40)) ||
            ent->baseType == eBaseTypePlayer)
        {
            zLightAddLocal(ent);
            xShadow_ListAdd(ent);
        }
    }
}

void xEntUpdate(xEnt* ent, xScene* sc, F32 dt)
{
    xEntBeginUpdate(ent, sc, dt);

    if (ent->pflags & 0x2)
    {
        xEntApplyPhysics(ent, sc, dt);
    }

    if (ent->pflags & 0x1)
    {
        xEntMove(ent, sc, dt);
    }

    xFFXApply(ent, sc, dt);

    if (ent->collis)
    {
        xEntCollide(ent, sc, dt);
    }

    if (ent->flags & 0x2 && ent->pflags & 0x4)
    {
        update_stacked_entity(*sc, *ent, dt);
    }

    xEntEndUpdate(ent, sc, dt);
}

void xEntBeginUpdate(xEnt* ent, xScene* sc, F32 dt)
{
    if (ent->model)
    {
        xModelUpdate(ent->model, dt);

        if (ent->frame)
        {
            xVec3Copy(&ent->frame->oldvel, &ent->frame->vel);

            ent->frame->oldmat = ent->frame->mat;

            xRotCopy(&ent->frame->oldrot, &ent->frame->rot);
            xMat4x3Copy(&ent->frame->mat, xModelGetFrame(ent->model));

            ent->frame->mode = 0;
        }
    }
}

void xEntEndUpdate(xEnt* ent, xScene* sc, F32 dt)
{
    if (ent->model)
    {
        if (ent->frame)
        {
            if (!(ent->frame->mode & 0x20000))
            {
                xMat3x3Copy((xMat4x3*)ent->model->Mat, &ent->frame->mat);
            }

            if (!(ent->frame->mode & 0x10000))
            {
                xVec3* mpos = (xVec3*)&ent->model->Mat->pos;
                xVec3Copy(mpos, &ent->frame->mat.pos);
            }

            if (ent->collModel && ent->collModel != ent->model)
            {
                xMat4x3Copy((xMat4x3*)ent->collModel->Mat, (xMat4x3*)ent->model->Mat);
            }

            if (ent->camcollModel && ent->camcollModel != ent->model)
            {
                xMat4x3Copy((xMat4x3*)ent->camcollModel->Mat, (xMat4x3*)ent->model->Mat);
            }
        }

        xModelEval(ent->model);

        if (ent->bupdate)
        {
            xVec3* upos = (xVec3*)&ent->model->Mat->pos;
            ent->bupdate(ent, upos);
        }

        if (ent->moreFlags & 0x20)
        {
            anim_coll::refresh(*ent);
        }

        if (ent->endUpdate)
        {
            ent->endUpdate(ent, sc, dt);
        }
    }
}

void xEntDefaultBoundUpdate(xEnt* ent, xVec3* pos)
{
    xBound* bound = &ent->bound;

    if (bound->type == XBOUND_TYPE_SPHERE)
    {
        xVec3Copy(&bound->sph.center, pos);

        bound->sph.center.y += 0.7f;
        bound->sph.r = 0.7f;
    }

    xBoundUpdate(bound);
    zGridUpdateEnt(ent);
}

void xEntDefaultTranslate(xEnt* ent, xVec3* dpos, xMat4x3* dmat)
{
    if (dmat)
    {
        if (ent->model)
        {
            xMat4x3Mul((xMat4x3*)ent->model->Mat, (xMat4x3*)ent->model->Mat, dmat);
        }

        if (ent->frame)
        {
            xMat4x3Mul(&ent->frame->mat, &ent->frame->mat, dmat);
        }

        xMat4x3Toworld(xEntGetCenter(ent), dmat, xEntGetCenter(ent));
    }
    else
    {
        if (ent->model)
        {
            xVec3AddTo(xEntGetPos(ent), dpos);
        }

        if (ent->frame)
        {
            xVec3AddTo(&ent->frame->mat.pos, dpos);
        }

        xVec3AddTo(xEntGetCenter(ent), dpos);
    }
}

static inline void xEntRotationToMatrix(xEntFrame* frame)
{
    if (frame->mode & 0x20)
    {
        if (frame->mode & 0x400)
        {
            xVec3AddTo(&frame->rot.axis, &frame->drot.axis);
            xMat3x3Euler(&frame->mat, frame->rot.axis.x, frame->rot.axis.y, frame->rot.axis.z);
        }
        else
        {
            frame->rot.angle = xAngleClamp(frame->rot.angle + frame->drot.angle);

            xMat3x3Rot(&frame->mat, &frame->rot.axis, frame->rot.angle);
        }

        frame->drot.angle = 0.0f;
    }
}

void xEntMotionToMatrix(xEnt* ent, xEntFrame* frame)
{
    if (frame->mode & 0x1000)
    {
        xEntRotationToMatrix(frame);
    }

    if (frame->mode & 0x2)
    {
        if (frame->mode & 0x800)
        {
            xMat3x3RMulVec(&frame->dpos, &frame->mat, &frame->dpos);
        }

        xEntAddToPos(ent, &frame->dpos);
        xVec3Init(&ent->frame->dpos, 0.0f, 0.0f, 0.0f);
    }

    if (frame->mode & 0x8)
    {
        if (frame->mode & 0x800)
        {
            xMat3x3RMulVec(&frame->dvel, &frame->mat, &frame->dvel);
        }

        xVec3AddTo(&frame->vel, &frame->dvel);
        xVec3Init(&ent->frame->dvel, 0.0f, 0.0f, 0.0f);
    }

    if (!(frame->mode & 0x1000))
    {
        xEntRotationToMatrix(frame);
    }
}

void xEntMove(xEnt* ent, xScene* sc, F32 dt)
{
    if (ent->moreFlags & 0x20)
    {
        anim_coll::pre_move(*ent);
    }

    if (ent->move)
    {
        ent->move(ent, sc, dt, ent->frame);
    }

    xEntMotionToMatrix(ent, ent->frame);

    if (ent->driver)
    {
        xEntFrame* dframe = ent->driver->frame;

        if (dframe)
        {
            if (ent->driveMode == 0)
            {
                xVec3 dpos;
                xVec3Sub(&dpos, &dframe->mat.pos, &dframe->oldmat.pos);

                ent->transl(ent, &dpos, NULL);
            }
            else if (ent->driveMode == 1)
            {
                RwMatrixUpdate((RwMatrix*)&dframe->oldmat);

                xMat4x3 invOldmat;
                RwMatrixInvert((RwMatrix*)&invOldmat, (RwMatrix*)&dframe->oldmat);

                xMat4x3 deltaMat;
                xMat4x3Mul(&deltaMat, &invOldmat, &dframe->mat);

                ent->transl(ent, NULL, &deltaMat);
            }
        }
    }

    if (ent->moreFlags & 0x20)
    {
        anim_coll::post_move(*ent);
    }
}

// The assert after the dposvel initialization is Ratatouille's, and the two placeholders stand
// for its debug prints after the clipping scales. They make xEntUpdate and update_stacked_entity
// call this out of line, as retail does.
void xEntApplyPhysics(xEnt* ent, xScene* sc, F32 dt)
{
    xVec3 dposvel = { 0, 0, 0 };

    xASSERT(ent->frame);

    if (ent->pflags & 0x4 && sc->flags & 0x1)
    {
        ent->frame->vel.y += sc->gravity * dt;
    }

    if (ent->pflags & 0x10 && sc->flags & 0x2)
    {
        F32 tfric = -(sc->friction * dt - 1.0f);
        xVec3SMulBy(&ent->frame->vel, tfric);
    }

    if (ent->pflags & 0x8 && sc->flags & 0x4)
    {
        F32 tdrag = -(sc->drag * dt - 1.0f);
        xVec3SMulBy(&ent->frame->vel, tdrag);
    }

    xVec3Add(&dposvel, &ent->frame->vel, &ent->frame->oldvel);
    xVec3SMulBy(&dposvel, 0.5f * dt);

    if (dposvel.y < 0.0f)
    {
        F32 dposXZ = xsqrt(SQR(dposvel.x) + SQR(dposvel.z));
        F32 scaleXZ = (dposXZ > 0.00001f) ? ((0.63f * (30.0f * dt)) / dposXZ) : 0.0f;
        F32 scaleY = (0.63f * (30.0f * dt)) / (F32)iabs(dposvel.y);

        if (scaleXZ < 1.0f)
        {
            dposvel.x *= scaleXZ;
            dposvel.z *= scaleXZ;
            xASSERT_PLACEHOLDER();
        }

        if (scaleY < 1.0f)
        {
            dposvel.y *= scaleY;
            xASSERT_PLACEHOLDER();
        }
    }

    xEntAddToPos(ent, &dposvel);
}

static inline void xEntCollideCeiling(xEnt* p, xScene* sc, F32 dt)
{
    xCollis* coll = &p->collis->colls[1];
    U8 idx;
    xCollis* ml = coll;
    F32 sbr;

    if (p->bound.type == XBOUND_TYPE_SPHERE)
    {
        sbr = p->bound.sph.r;
    }
    else
    {
        sbr = 0.7f;
    }

    for (idx = 6; idx < p->collis->idx; idx++)
    {
        xCollis* mf = &p->collis->colls[idx];
        xEnt* fent = (xEnt*)mf->optr;

        if (fent)
        {
            if (!((p->collis->depenq) ?
                      p->collis->depenq(p, fent, sc, dt, mf) :
                      (fent->collType & p->collis->pen && fent->penby & p->collType)))
            {
                continue;
            }
        }
        else if (!(p->collis->pen & 0x20))
        {
            continue;
        }

        if (mf->hdng.y > icos(0.78539819f) && mf->dist < ml->dist)
        {
            ml = mf;
        }
    }

    if (ml != coll)
    {
        F32 ceil_dist = ml->dist * (F32)iabs(ml->hdng.y);

        *coll = *ml;

        ml->flags |= 0xA;

        if (ceil_dist < sbr)
        {
            p->frame->mat.pos.y -= sbr - ceil_dist;
            p->frame->vel.y = 0.0f;
        }
    }
}

void xEntCollide(xEnt* ent, xScene* sc, F32 dt)
{
    if (ent->model)
    {
        if (ent->collis->chk & 0x2E)
        {
            xEntBeginCollide(ent, sc, dt);
        }

        if (ent->collis->chk & 0x8)
        {
            xEntCollCheckNPCsByGrid(ent, sc, xEntCollCheckOneEntNoDepen);
        }

        if (ent->collis->chk & 0x6)
        {
            xEntCollCheckByGrid(ent, sc, xEntCollCheckOneEntNoDepen);
        }

        if (ent->collis->chk & 0x20)
        {
            xEntCollCheckEnv(ent, sc);
        }

        xCollis* coll = &ent->collis->colls[0];

        if (ent->collis->chk & 0x2E)
        {
            F32 h_dot_n;

            if (ent->bound.type == XBOUND_TYPE_SPHERE)
            {
                h_dot_n = ent->bound.sph.r;
            }
            else
            {
                h_dot_n = 0.7f;
            }

            if (ent->pflags & 0x80 && coll->flags & 0x1)
            {
                F32 depen_len = xVec3Dot(&coll->hdng, &coll->norm);

                if (depen_len > 0.0f)
                {
                    xVec3Inv(&coll->norm, &coll->norm);
                    depen_len = -depen_len;
                }

                depen_len = depen_len * coll->dist + h_dot_n;

                if (depen_len < 0.0f || depen_len > h_dot_n)
                {
                    depen_len = CLAMP(depen_len, 0.0f, h_dot_n);
                }

                xVec3SMul(&coll->depen, &coll->norm, depen_len);
            }

            if (ent->frame->vel.y <= 0.0f)
            {
                xEntCollideFloor(ent, sc, dt);
            }
            else
            {
                xEntCollideCeiling(ent, sc, dt);
            }

            xEntCollideWalls(ent, sc, dt);
            xEntEndCollide(ent, sc, dt);
        }
    }
}

void xEntBeginCollide(xEnt* ent, xScene*, F32)
{
    U8 idx;
    xCollis* coll;

    if (ent->bupdate)
    {
        ent->bupdate(ent, &ent->frame->mat.pos);
    }

    for (idx = 0; idx < 18; idx++)
    {
        coll = &ent->collis->colls[idx];

        coll->flags = 0x1F00;
        coll->optr = NULL;
        coll->mptr = NULL;
        coll->dist = FLOAT_MAX;
    }

    ent->collis->idx = 6;
    ent->collis->stat_sidx = 6;
    ent->collis->stat_eidx = 6;
    ent->collis->dyn_sidx = 6;
    ent->collis->dyn_eidx = 6;
    ent->collis->npc_sidx = 6;
    ent->collis->npc_eidx = 6;
    ent->collis->env_sidx = 6;
    ent->collis->env_eidx = 6;
}

void xEntEndCollide(xEnt* ent, xScene* sc, F32 dt)
{
    if (ent->collis->post)
    {
        ent->collis->post(ent, sc, dt, ent->collis);
    }
}

void xEntCollCheckEnv(xEnt* p, xScene* sc)
{
    xCollis* coll;
    U8 ncolls;

    p->collis->env_sidx = p->collis->idx;

    coll = &p->collis->colls[p->collis->idx];
    coll->flags = 0x1F00;

    ncolls = xSphereHitsEnv3(&p->bound.sph, sc->env, coll, 18 - p->collis->idx, 0.78539819f);

    p->collis->idx = p->collis->idx + ncolls;
    p->collis->env_eidx = p->collis->idx;
}

static void xEntCollCheckOneGrid(xEnt* p, xScene* sc, xEnt* (*hitIt)(xEnt*, xScene*, void*),
                                 xGrid* grid)
{
    xVec3* pcenter = xEntGetCenter(p);
    xGridIterator it;
    S32 px, pz;

    xGridBound* cell = xGridIterFirstCell(grid, pcenter->x, pcenter->y, pcenter->z, px, pz, it);

    while (cell)
    {
        if (xQuickCullIsects(&p->bound.qcd, (xQCData*)(cell + 1)))
        {
            hitIt((xEnt*)cell->data, sc, p);
        }

        cell = xGridIterNextCell(it);
    }

    xBox clbox;
    clbox.lower.x = grid->csizex * px;
    clbox.lower.z = grid->csizez * pz;
    clbox.lower.x += grid->minx;
    clbox.lower.z += grid->minz;

    F32 clcenterx = 0.5f * grid->csizex;
    clcenterx += clbox.lower.x;

    F32 clcenterz = 0.5f * grid->csizez;
    clcenterz += clbox.lower.z;

    static S32 k;

    if (pcenter->x < clcenterx)
    {
        if (pcenter->z < clcenterz)
        {
            k = 0;
        }
        else
        {
            k = 1;
        }
    }
    else
    {
        if (pcenter->z < clcenterz)
        {
            k = 3;
        }
        else
        {
            k = 2;
        }
    }

    static S32 offs[4][3][2] = {
        { { -1, 0 }, { -1, -1 }, { 0, -1 } },
        { { 0, -1 }, { 1, -1 }, { 1, 0 } },
        { { 1, 0 }, { 1, 1 }, { 0, 1 } },
        { { 0, 1 }, { -1, 1 }, { -1, 0 } },
    };

    for (S32 i = 0; i < 3; i++)
    {
        S32 _x = px + offs[k][i][1];
        S32 _z = pz + offs[k][i][0];

        cell = xGridIterFirstCell(grid, _x, _z, it);

        while (cell)
        {
            if (xQuickCullIsects(&p->bound.qcd, (xQCData*)(cell + 1)))
            {
                hitIt((xEnt*)cell->data, sc, p);
            }

            cell = xGridIterNextCell(it);
        }
    }

    cell = xGridIterFirstCell(&grid->other, it);

    while (cell)
    {
        if (xQuickCullIsects(&p->bound.qcd, (xQCData*)(cell + 1)))
        {
            hitIt((xEnt*)cell->data, sc, p);
        }

        cell = xGridIterNextCell(it);
    }
}

void xEntCollCheckByGrid(xEnt* p, xScene* sc, xEnt* (*hitIt)(xEnt*, xScene*, void*))
{
    p->collis->stat_sidx = p->collis->idx;
    p->collis->dyn_sidx = p->collis->idx;

    xEntCollCheckOneGrid(p, sc, hitIt, &colls_grid);
    xEntCollCheckOneGrid(p, sc, hitIt, &colls_oso_grid);

    p->collis->stat_eidx = p->collis->idx;
    p->collis->dyn_eidx = p->collis->idx;
}

void xEntCollCheckNPCsByGrid(xEnt* p, xScene* sc, xEnt* (*hitIt)(xEnt*, xScene*, void*))
{
    p->collis->npc_sidx = p->collis->idx;

    xEntCollCheckOneGrid(p, sc, hitIt, &npcs_grid);

    p->collis->npc_eidx = p->collis->idx;
}

void xEntCollCheckStats(xEnt* p, xScene* sc, xEnt* (*hitIt)(xEnt*, xScene*, void*))
{
    p->collis->stat_sidx = p->collis->idx;

    xSceneForAllStatics(sc, hitIt, p);

    p->collis->stat_eidx = p->collis->idx;
}

void xEntCollCheckDyns(xEnt* p, xScene* sc, xEnt* (*hitIt)(xEnt*, xScene*, void*))
{
    p->collis->dyn_sidx = p->collis->idx;

    xSceneForAllDynamics(sc, hitIt, p);

    p->collis->dyn_eidx = p->collis->idx;
}

void xEntCollCheckNPCs(xEnt* p, xScene* sc, xEnt* (*hitIt)(xEnt*, xScene*, void*))
{
    p->collis->npc_sidx = p->collis->idx;

    xSceneForAllNPCs(sc, hitIt, p);

    p->collis->npc_eidx = p->collis->idx;
}

S32 xent_entent = 0;

xEnt* xEntCollCheckOneEntNoDepen(xEnt* ent, xScene* sc, void* data)
{
    xent_entent = 1;

    xEnt* p = (xEnt*)data;
    xCollis* coll;
    U32 modl_coll = 0;

    if (p->collis->idx >= 15)
    {
        xent_entent = 0;
        return NULL;
    }

    if ((ent->chkby & p->collType) == 0)
    {
        xent_entent = 0;
        return ent;
    }

    if (ent == p)
    {
        xent_entent = 0;
        return ent;
    }

    if (ent->id == p->id && ent->baseType != eBaseTypeBoulder && ent->baseType != eBaseTypeProjBomb)
    {
        xent_entent = 0;
        return ent;
    }

    coll = &p->collis->colls[p->collis->idx];

    if (ent->collLev == 5 && p->collType & (XENT_COLLTYPE_NPC | XENT_COLLTYPE_PLYR))
    {
        modl_coll = 1;
    }

    if (modl_coll)
    {
        coll->flags = 0;
    }
    else
    {
        coll->flags = 0x1F00;
    }

    xBoundHitsBound(&p->bound, &ent->bound, coll);

    if (coll->flags & 0x1)
    {
        if (modl_coll)
        {
            xBound tmp;
            xBound* bptr;
            U8 ncolls = 0;
            xVec3 *upper, *lower;
            U8 idx;

            coll->flags = 0x1F00;

            if (p->bound.type == XBOUND_TYPE_SPHERE)
            {
                ncolls =
                    xSphereHitsModel3(&p->bound.sph, ent->collModel ? ent->collModel : ent->model,
                                      coll, 15 - p->collis->idx, 0.78539819f);
            }
            else if (p->bound.type == XBOUND_TYPE_BOX || p->bound.type == XBOUND_TYPE_OBB)
            {
                upper = &p->bound.box.box.upper;
                lower = &p->bound.box.box.lower;

                tmp.type = XBOUND_TYPE_SPHERE;
                xVec3Add(&tmp.sph.center, upper, lower);
                xVec3SMulBy(&tmp.sph.center, 0.5f);
                tmp.sph.r =
                    0.167f * (upper->x + upper->y + upper->z - lower->x - lower->y - lower->z);
                // set but never read: retail still keeps tmp on the stack
                bptr = &tmp;

                ncolls =
                    xSphereHitsModel3(&p->bound.sph, ent->collModel ? ent->collModel : ent->model,
                                      coll, 15 - p->collis->idx, 0.78539819f);
            }

            for (idx = 0; idx < ncolls; idx++)
            {
                coll[idx].optr = ent;
                coll[idx].mptr = ent->model;

                p->collis->idx++;
            }

            xent_entent = 0;
            return ent;
        }
        else
        {
            coll->oid = ent->id;
            coll->optr = ent;
            coll->mptr = ent->model;

            p->collis->idx++;

            if (ent->pflags & 0x20 && ent->bound.type == XBOUND_TYPE_SPHERE &&
                p->bound.type == XBOUND_TYPE_SPHERE && coll->hdng.y < -0.866025f)
            {
                F32 rsum = p->bound.sph.r + ent->bound.sph.r;
                F32 dx = p->bound.sph.center.x - ent->bound.sph.center.x;
                F32 dy = p->bound.sph.center.y - ent->bound.sph.center.y;
                F32 dz = p->bound.sph.center.z - ent->bound.sph.center.z;

                F32 hsqr = SQR(rsum) - (SQR(dx) + SQR(dz));

                if (hsqr >= 0.0f)
                {
                    coll->depen.x = 0.0f;
                    coll->depen.y = xsqrt(hsqr) - dy;
                    coll->depen.z = 0.0f;
                    coll->dist = 0.7f - coll->depen.y;
                    coll->hdng.x = 0.0f;
                    coll->hdng.y = -1.0f;
                    coll->hdng.z = 0.0f;
                }
            }
        }
    }

    xent_entent = 0;
    return ent;
}

void xEntCollideFloor(xEnt* p, xScene* sc, F32 dt)
{
    xCollis* coll = &p->collis->colls[0];
    U8 idx;
    xCollis* ml = coll;
    xVec3 motion;
    F32 mlen;
    S32 stepping = 0;
    F32 sbr;

    if (p->bound.type == XBOUND_TYPE_SPHERE)
    {
        sbr = p->bound.sph.r;
    }
    else
    {
        sbr = 0.7f;
    }

    xVec3Copy(&motion, &p->frame->mat.pos);
    xVec3SubFrom(&motion, &p->frame->oldmat.pos);

    motion.y = 0.0f;
    mlen = xVec3Length(&motion);

    for (idx = 6; idx < p->collis->idx; idx++)
    {
        xCollis* mf = &p->collis->colls[idx];

        if (mf->flags & 0x1)
        {
            xEnt* fent = (xEnt*)mf->optr;

            if (fent)
            {
                if ((fent->collType == XENT_COLLTYPE_DYN || fent->collType == XENT_COLLTYPE_STAT) ||
                    (fent->pflags & 0x20 && 0.0f == mf->hdng.x && 0.0f == mf->hdng.z))
                {
                    if (!((p->collis->depenq) ?
                              p->collis->depenq(p, fent, sc, dt, mf) :
                              (fent->collType & p->collis->pen && fent->penby & p->collType)))
                    {
                        continue;
                    }
                }
                else
                {
                    continue;
                }
            }
            else if (!(p->collis->pen & 0x20))
            {
                continue;
            }

            if (mf->dist < ml->dist)
            {
                if (mf->hdng.y < -icos(PI / 3) &&
                    (mf->norm.y > icos(nsn_angle) ||
                     p->frame->oldmat.pos.y > dt * (sc->gravity * dt) + p->frame->mat.pos.y))
                {
                    ml = mf;
                    stepping = 0;
                }
                else if (mlen > 0.001f && mf->hdng.y < 0.65f / sbr - 1.0f &&
                         mf->norm.y > icos(nsn_angle))
                {
                    stepping = 1;
                    ml = mf;
                }
            }
        }
    }

    if (ml != coll)
    {
        F32 flr_dist = ml->dist * (F32)iabs(ml->hdng.y);
        xEnt* fent; // unused

        *coll = *ml;

        if (flr_dist < sbr)
        {
            ml->flags |= 0x6;

            if (stepping)
            {
                p->frame->mat.pos.y += 1.5f * dt;
                p->frame->mat.pos.x += ml->depen.x;
                p->frame->mat.pos.z += ml->depen.z;
            }
            else
            {
                p->frame->mat.pos.y += ml->depen.y;
            }

            p->frame->vel.y = 0.0f;
        }
    }
}

void xEntCollideWalls(xEnt* p, xScene* sc, F32 dt)
{
    xCollis* coll;
    xEnt* cent;
    // Not in the DWARF order (sidx, idx): that order changes the generated code.
    U8 idx, sidx;
    F32 sbr;

    if (p->bound.type == XBOUND_TYPE_SPHERE)
    {
        sbr = p->bound.sph.r;
    }
    else
    {
        sbr = 0.7f;
    }

    if (p->collis->pen & 0x8)
    {
        idx = p->collis->npc_eidx;
        sidx = p->collis->npc_sidx;

        while (sidx < idx)
        {
            coll = &p->collis->colls[sidx];
            cent = (xEnt*)coll->optr;

            if (!(coll->flags & 0x2) && coll->dist < sbr &&
                ((p->collis->depenq) ? p->collis->depenq(p, cent, sc, dt, coll) :
                                       cent->penby & p->collType))
            {
                if (0.0f != coll->depen.x || 0.0f != coll->depen.z)
                {
                    coll->depen.y = 0.0f;
                }

                xEntAddToPos(p, &coll->depen);
            }

            sidx++;
        }
    }

    if (p->collis->pen & 0x4)
    {
        idx = p->collis->dyn_eidx;
        sidx = p->collis->dyn_sidx;

        while (sidx < idx)
        {
            coll = &p->collis->colls[sidx];
            cent = (xEnt*)coll->optr;

            if (!(coll->flags & 0x2) && coll->dist < sbr &&
                ((p->collis->depenq) ? p->collis->depenq(p, cent, sc, dt, coll) :
                                       cent->penby & p->collType))
            {
                coll->depen.y = 0.0f;

                xEntAddToPos(p, &coll->depen);
            }

            sidx++;
        }
    }

    if (p->collis->pen & 0x2)
    {
        idx = p->collis->stat_eidx;
        sidx = p->collis->stat_sidx;

        while (sidx < idx)
        {
            coll = &p->collis->colls[sidx];
            cent = (xEnt*)coll->optr;

            if (!(coll->flags & 0x2) && coll->dist < sbr &&
                ((p->collis->depenq) ? p->collis->depenq(p, cent, sc, dt, coll) :
                                       cent->penby & p->collType))
            {
                coll->depen.y = 0.0f;

                xEntAddToPos(p, &coll->depen);
            }

            sidx++;
        }
    }

    if (p->collis->pen & 0x20)
    {
        idx = p->collis->env_eidx;
        sidx = p->collis->env_sidx;

        while (sidx < idx)
        {
            coll = &p->collis->colls[sidx];
            cent = (xEnt*)coll->optr;

            if (!(coll->flags & 0x2) && coll->dist < sbr)
            {
                coll->depen.y = 0.0f;

                xEntAddToPos(p, &coll->depen);
            }

            sidx++;
        }
    }
}

void xEntSetNostepNormAngle(F32 angle)
{
    nsn_angle = angle;
}

xBox* xEntGetAllEntsBox()
{
    return &all_ents_box;
}

bool xEntValidType(U8 type)
{
    return type == eBaseTypeTrigger || type == eBaseTypeVillain || type == eBaseTypePlayer ||
           type == eBaseTypePickup || type == eBaseTypePlatform || type == eBaseTypeDoor ||
           type == eBaseTypeSavePoint || type == eBaseTypeItem || type == eBaseTypeStatic ||
           type == eBaseTypeDynamic || type == eBaseTypeBubble || type == eBaseTypePendulum ||
           type == eBaseTypeHangable || type == eBaseTypeButton || type == eBaseTypeProj ||
           type == eBaseTypeProjBomb || type == eBaseTypeDestructObj || type == eBaseTypeUI ||
           type == eBaseTypeUIFont || type == eBaseTypeProjectileType ||
           type == eBaseTypeEGenerator || type == eBaseTypeNPC || type == eBaseTypeBoulder ||
           type == eBaseTypeTeleportBox || type == eBaseTypeZipLine;
}

void xEntOpacityInit(xEntOpacity* opacity, xEnt*)
{
    opacity->delta_opacity_2 = opacity->delta_opacity = 0.0f;
}

void xEntOpacityUpdate(xEntOpacity* opacity, xEnt* ent, F32 dt)
{
    opacity->delta_opacity += opacity->delta_opacity_2 * dt;

    if (ent->model)
    {
        ent->model->Alpha += opacity->delta_opacity * dt;
        ent->model->Alpha = CLAMP(ent->model->Alpha, 0.0f, 1.0f);

        if ((ent->model->Pipe.Flags & 0x30000) != 0x10000)
        {
            if (ent->model->Alpha != 1.0f && ent->model->Alpha != 0.0f)
            {
                ent->model->Pipe.Flags |= 0x20000;
            }
            else
            {
                ent->model->Pipe.Flags &= ~0x20000;
            }
        }
    }
}

void xEntInitShadow(xEnt& ent, xEntShadow& shadow)
{
    ent.entShadow = &shadow;

    shadow.vec.assign(0.0f, 1.0f, 0.0f);
    shadow.pos = ent.asset->pos;
    shadow.shadowModel = NULL;
    shadow.dst_cast = -1.0f;
    shadow.radius[0] = -1.0f;
    shadow.radius[1] = -1.0f;
}

void xEntStoreCheckPoint()
{
}
