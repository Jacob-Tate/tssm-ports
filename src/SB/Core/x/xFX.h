#ifndef XFX_H
#define XFX_H

#include "xMath2.h"
#include "xMath3.h"
#include "containers.h"
#include "iColor.h"
#include "iMath.h"

#include <rwcore.h>
#include <rpworld.h>

struct xModelInstance;

struct xFXRing
{
    U32 texture;
    F32 lifetime;
    xVec3 pos;
    F32 time; // 0x14
    F32 ring_radius;
    F32 ring_radius_delta; // 0x1c
    F32 ring_tilt;
    F32 ring_tilt_delta; // 0x24?
    F32 ring_height; // 0x28
    F32 ring_height_delta;
    iColor_tag ring_color;
    U16 ring_segs;
    U8 u_repeat;
    U8 v_repeat;
    xFXRing** parent;
    RwRaster* rast_ring;
};

struct xFXRibbon
{
    struct curve_node
    {
        F32 time;
        xColor_tag color;
        F32 scale;
    };

    struct config_type
    {
        F32 life_time;
        U32 blend_src;
        U32 blend_dst;
        F32 pivot;
        S32 flags;
        const curve_node* curve;
        S32 curve_size;
        RwRaster* raster;
    };

    struct joint_data
    {
        S32 flags;
        U32 born;
        xVec3 loc;
        xVec3 norm;
        F32 orient;
        F32 scale;
        F32 alpha;
    };

    struct activity_data
    {
        xFXRibbon* owner;
        activity_data** position;
        tier_queue<joint_data> joints;
        S32 curve_index;
        F32 ilife;
        U32 mtime;
        U32 mlife;
    };

    F32 texture_offset;
    F32 texture_increment_scale;
    activity_data* act;
    const config_type* cfg;

    static tier_queue_allocator joint_alloc;
    static activity_data* activities[32];
    static S32 activities_used;
    static bool need_sort;

    static void render_all();
    static void sort_active_ribbons();
    static void update_all(F32 dt);
    static void scene_enter();
    static S32 compare_ribbons(const void* e1, const void* e2);
    void render_strip(RxObjSpace3DVertex* verts, tier_queue<joint_data>::iterator first,
                      size_t size);
    void eval_joint(const joint_data& joint, xColor_tag& color, F32& width);
    void refresh_joint(joint_data& joint, const tier_queue<joint_data>::iterator& it);
    void get_normal(xVec3& norm, const xVec3& dir, F32 orient);
    void render();
    void update(F32 dt);
    void insert(const xVec3& loc, const xVec3& norm, F32 scale, F32 alpha, S32 flags);
    void set_config(const config_type* cfg);
    static void load_default_config(config_type& cfg);
    void clear();
    void create(S32);

    tier_queue<joint_data>::iterator begin() const
    {
        return (act == NULL) ? tier_queue<joint_data>::dummy_iterator() : act->joints.begin();
    }

    U32 size() const
    {
        return (act == NULL) ? 0 : act->joints.size();
    }

    F32 get_age(const joint_data& joint) const
    {
        return 0.001f * (act->mtime - joint.born);
    }

    F32 time_remaining()
    {
        return cfg->life_time - get_age(*begin());
    }

    void activate()
    {
        if (act != NULL || activities_used >= 32)
        {
            return;
        }

        act = activities[activities_used];
        activities_used++;
        act->owner = this;
        act->mtime = 0;
        set_config(cfg);
    }

    void deactivate()
    {
        if (act == NULL)
        {
            return;
        }

        activities_used--;

        activity_data** pos = act->position;
        activity_data** last = &activities[activities_used];
        activity_data* tmp = *pos;

        *pos = *last;
        (*pos)->position = pos;
        *last = tmp;
        (*last)->position = last;

        act = NULL;
        need_sort = true;
    }
};

struct xFXStreakElem
{
    U32 flag;
    xVec3 p[2];
    F32 a;
};

struct xFXStreak
{
    U32 flags;
    F32 frequency;
    F32 alphaFadeRate;
    F32 alphaStart;
    F32 elapsed;
    F32 lifetime;
    U32 head;
    xColor_tag color_a;
    xColor_tag color_b;
    RwTexture* texturePtr;
    RwRaster* textureRasterPtr;
    xFXStreakElem elem[50];
};

struct xFXShineElem
{
    U32 flag;
    xVec3 p;
    xVec3 vel;
    F32 lifetime;
    F32 a;
    xColor_tag cola;
    xColor_tag colb;
};

struct xFXShine
{
    U32 flags;
    xVec3* ppos;
    xVec3 pos;
    F32 spd;
    F32 width;
    F32 frequency;
    F32 elapsed;
    F32 lifetimeElemMax;
    F32 lifetimeMax;
    F32 lifetime;
    F32 rotateSpeed;
    F32 rotateZ;
    xColor_tag color_a;
    xColor_tag color_b;
    RwTexture* texturePtr;
    RwRaster* textureRasterPtr;
    xFXShineElem elem[100];
};

struct xFRect
{
    F32 x;
    F32 y;
    F32 w;
    F32 h;
};

struct xFXCameraTexture
{
    RwCamera* cam;
    RwRaster* raster;
    RwRaster* bgraster;
    RpWorld* world;
    RwTexture* texture;
    U32 vert_buffer_used;
    xColor_tag bgcolor;
    RwCamera* oldcam;
    RpWorld* oldworld;
    F32 rcz;
    F32 w;
    F32 h;
    RwRaster* zraster;
    S32 max_tw;
    S32 max_th;
    F32 old_near_plane;
    F32 old_far_plane;
    // PS2 DWARF also has F32 old_znear here; the GC build lacks it (camtex is 0x44 bytes)

    void zbuffer_map_and_clear(RwRaster* fraster, RwRaster* zraster, U32 fbmsk, S32 clear_flags);
    void setup_subview(RwCamera* src_cam, const xFRect& src_rect, const xFRect& dst_rect);
    void get_subview(xMat4x3& ltm, xVec2& view_window, xVec2& view_offset, RwCamera* src_cam,
                     const xFRect& src_rect, const xFRect& dst_rect) const;
    void render_background();
    void end_render();
    void begin_render(S32 clear_flags, U32 fbmsk_write, U32 fbmsk_finish, F32 near_plane,
                      F32 far_plane);
    void destroy();
    U8 create(S32 w, S32 h, bool need_zwrite, S32 bpp, S32 zbpp);

    // Inline on GC (no symbol). Same shape as BfBB's zNPCTypePrawn television::set_background;
    // xModelBlur::scene_enter copies the colour through the by-value parameter.
    void set_background(iColor_tag color)
    {
        bgraster = NULL;
        bgcolor = color;
    }
};

// Layout from the PS2 DWARF (size 0x14). Nothing references it on GC, so its functions in
// xFXCameraTexture.cpp are dead-stripped.
struct xFXFastRaster
{
    RwRaster* src;
    RwRaster* dst;
    U32 fbmsk;
    U32 test;
    U32 old_alpha_1;

    // static: the PS2 DWARF gives them no 'this' (dst_rect is the first argument register).
    static bool clip_rect(xFRect& dst_rect, RwRaster* dst_raster);
    static bool clip_rect(xFRect& dst_rect, RwRaster* dst_raster, xFRect& src_rect);
};

struct xFXBubbleParams
{
    U32 pass1 : 1;
    U32 pass2 : 1;
    U32 pass3 : 1;
    U32 padding : 5;
    U8 pass1_alpha;
    U8 pass2_alpha;
    U8 pass3_alpha;
    U32 pass1_fbmsk;
    U32 fresnel_map;
    F32 fresnel_map_coeff;
    U32 env_map;
    F32 env_map_coeff;
};

#define RING_COUNT 8

extern xFXRing ringlist[RING_COUNT];
extern xFXStreak sStreakList[10];
extern xFXShine sShineList[2];
extern RpAtomicCallBackRender gAtomicRenderCallBack;

void xFX_SceneShutdown();
void xFX_SceneSetup();
void xFXInit();
xFXRing* xFXRingCreate(const xVec3* pos, const xFXRing* params);
void xFXRingRender();
void xFX_SceneEnter(RpWorld* world);
void xFX_SceneExit(RpWorld* world);
void xFXUpdate(F32 dt);
RpAtomic* AtomicDisableMatFX(RpAtomic* atomic);
void xFXPreAllocMatFX(RpClump* clump);

RpAtomic* xFXBubbleRender(RpAtomic* atomic);

extern F32 xFXanimUVRotMat0[2];
extern F32 xFXanimUVRotMat1[2];
extern F32 xFXanimUVTrans[2];
extern F32 xFXanimUVScale[2];
extern F32 xFXanimUV2PRotMat0[2];
extern F32 xFXanimUV2PRotMat1[2];
extern F32 xFXanimUV2PTrans[2];
extern F32 xFXanimUV2PScale[2];
extern RwTexture* xFXanimUV2PTexture;

void xFXanimUV2PSetTexture(RwTexture* texture);
RpAtomic* xFXanimUVAtomicSetup(RpAtomic* atomic, bool dualPass);

// These have no symbols in the GC build: always inlined.
inline void xFXanimUVSetTranslation(const xVec3* trans)
{
    xFXanimUVTrans[0] = trans->x;
    xFXanimUVTrans[1] = trans->y;
}

inline void xFXanimUV2PSetTranslation(const xVec3* trans)
{
    xFXanimUV2PTrans[0] = trans->x;
    xFXanimUV2PTrans[1] = trans->y;
}

inline void xFXanimUVSetScale(const xVec3* scale)
{
    xFXanimUVScale[0] = scale->x;
    xFXanimUVScale[1] = scale->y;
}

inline void xFXanimUV2PSetScale(const xVec3* scale)
{
    xFXanimUV2PScale[0] = scale->x;
    xFXanimUV2PScale[1] = scale->y;
}

inline void xFXanimUVSetAngle(F32 angle)
{
    F32 sin = isin(angle);
    F32 cos = icos(angle);

    xFXanimUVRotMat0[0] = cos;
    xFXanimUVRotMat0[1] = -sin;
    xFXanimUVRotMat1[0] = sin;
    xFXanimUVRotMat1[1] = cos;
}

inline void xFXanimUV2PSetAngle(F32 angle)
{
    F32 sin = isin(angle);
    F32 cos = icos(angle);

    xFXanimUV2PRotMat0[0] = cos;
    xFXanimUV2PRotMat0[1] = -sin;
    xFXanimUV2PRotMat1[0] = sin;
    xFXanimUV2PRotMat1[1] = cos;
}
U32 xFXanimUVCreate();
void xFXFireworksInit(const char* fireworksTrailEmitter, const char* fireworksEmitter1,
                      const char* fireworksEmitter2, const char* fireworksSound,
                      const char* fireworksLaunchSound);
void xFXFireworksLaunch(F32 countdownTime, const xVec3* pos, F32 fuelTime);
void xFXFireworksUpdate(F32 dt);
void xFXStreakInit();
void xFXStreakUpdate(F32 dt);
void xFXStreakUpdate(U32 streakID, const xVec3*, const xVec3*);
void xFXStreakRender();
U32 xFXStreakStart(F32 frequency, F32 alphaFadeRate, F32 alphaStart, U32 textureID,
                   const xColor_tag* edge_a, const xColor_tag* edge_b, S32 taper);
void xFXStreakStop(U32);
void xFXShineInit();
void xFXShineUpdate(F32 dt);
void xFXShineRender();
RpAtomic* xFXAtomicEnvMapSetup(RpAtomic* atomic, U32 envmapID, F32 shininess);
void xFXRibbonSceneEnter();
void xFXRibbonRender();
void xFXAuraInit();
void xFXAuraAdd(xVec3* pos, xColor_tag* color, F32 size, bool doTrivialCull);
void xFXAuraUpdate(F32 dt);
void xFXAuraRender();
void xFXAuraClearCount();
void xFXRenderProximityFade(const xModelInstance& model, F32 near_dist, F32 far_dist);
void xFXSceneInit();
void xFXSceneSetup();
void xFXSceneReset();
void xFXScenePrepare();
void xFXSceneFinish();

#endif
