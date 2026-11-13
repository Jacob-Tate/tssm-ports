#include "zParticleGenerator.h"
#include <PowerPC_EABI_Support/MSL_C/MSL_Common/bsearch.h>
#include <PowerPC_EABI_Support/MSL_C/MSL_Common/stdlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mem.h"

#include "extras.h"
#include "xColor.h"
#include "xDebug.h"
#include "xFXHighDynamicRange.h"
#include "iFile.h"
#include "xIni.h"
#include "xMath.h"
#include "xMemMgr.h"
#include "xPtankPool.h"
#include "xDecal.h"
#include "xScrFx.h"
#include "xScreenWarp.h"
#include "xSnd.h"
#include "xString.h"
#include "xstransvc.h"
#include "xTransient.h"
#include "xutil.h"

#include "zExplosion.h"
#include "zGlobals.h"
#include "xCam.h"
#include "zParticleCustom.h"
#include "zReactiveAnimation.h"

#include <rwcore.h>
#include "xRand.h"

#define MAX_MOTES 511
#define MAX_STREAKS 128
#define MAX_STREAK_EMITTERS 16
#define MAX_SPARKS 128
#define MAX_SPARK_EMITTERS 16
#define MAX_PRESENCE 32

namespace
{
    struct glare_config
    {
        F32 life;
        F32 scale;
        xColor_tag color;
    };

    struct streak_config
    {
        F32 life;
        F32 rate;
        F32 emit_delay;
        F32 emit_time;
        F32 width;
        F32 length;
        F32 vel;
        F32 radius;
        F32 gravity;
        F32 alpha;
    };

    struct spark_config
    {
        F32 life;
        F32 rate;
        F32 emit_delay;
        F32 emit_time;
        F32 scale;
        F32 vel_min;
        F32 vel_max;
        F32 radius;
        F32 gravity;
        F32 alpha;
    };

    struct shake_config
    {
        F32 time;
        F32 mag;
        F32 cycle;
        F32 rot_mag;
        F32 radius;
    };

    struct presence_config
    {
        U32 hurt_whom;
        F32 delay;
        F32 life;
        F32 knockback;
        F32 radius_begin;
        F32 radius_end;
        F32 damage_player;
        F32 damage_npc;
        F32 damage_other;
    };

    struct decal_config
    {
        S32 type;
        F32 radius;
        F32 life;
        F32 width;
        F32 max_dist;
    };

    struct _class_0
    {
        U32 type;
        F32 radius;
        F32 duration;
        F32 intensity;
        F32 freq;
    };

    struct _class_1
    {
        U32 fire;
        U32 smoke;
        U32 streak;
        U32 spark;
        U32 glare;
        U32 decal;
    };

    struct runtime_config
    {
        U32 id;
        F32 vel_decay;
        F32 emit_radius;
        U32 motes_min;
        U32 motes_max;
        F32 life_min;
        F32 life_max;
        F32 scale_min;
        F32 scale_max;
        F32 scale_fire;
        F32 scale_smoke;
        F32 vel_min;
        F32 vel_max;
        iSndGroupHandle sound;
        S32 material_index;
        S32 streak_group_index;
        S32 spark_group_index;
        xResponseCurve anim;
        glare_config glare;
        struct
        {
            S32 type;
            F32 radius;
            F32 duration;
            F32 intensity;
            F32 freq;
        } warp;
        streak_config streak;
        spark_config spark;
        shake_config shake;
        presence_config presence;
        decal_config decal;
        struct
        {
            RwTexture* fire;
            RwTexture* smoke;
            RwTexture* streak;
            RwTexture* spark;
            RwTexture* glare;
            RwTexture* decal;
        } texture;
        F32 vel_decay_mult;
    };

    struct anim_data
    {
        F32 alpha;
        F32 scale;
        F32 heat;
        F32 rise;
        F32 glow;
    };

    struct curve_node
    {
        F32 t;
        anim_data anim;
    };

    struct ini_config
    {
        char name[64];
        F32 vel_decay;
        F32 emit_radius;
        U32 motes_min;
        U32 motes_max;
        F32 life_min;
        F32 life_max;
        F32 scale_min;
        F32 scale_max;
        F32 scale_fire;
        F32 scale_smoke;
        F32 vel_min;
        F32 vel_max;
        U32 sound;
        curve_node* anim;
        U32 anim_size;
        glare_config glare;
        _class_0 warp;
        streak_config streak;
        spark_config spark;
        shake_config shake;
        presence_config presence;
        decal_config decal;
        _class_1 texture;
    };

    struct default_type_loader
    {
        char* name;
        void (*load)(ini_config&);
    };

    struct mote
    {
        U8 type;
        U8 material_index;
        U8 pad1;
        U8 pad2;
        xVec3 loc;
        xVec3 dir;
        F32 age;
        F32 age_rate;
        F32 scale;
        F32 vel;
        anim_data anim;
    };

    struct ordered_mote
    {
        xVec3 loc;
        F32 scale_fire;
        U32 sort_key;
        F32 scale_smoke;
        U8 type;
        U8 material_index;
        U8 alpha_fire;
        U8 alpha_smoke;
        U8 dirsign;
        U8 glow;
        U8 pad1;
        U8 pad2;
    };

    struct streak_particle
    {
        xVec3 loc;
        F32 width;
        xVec3 vel;
        F32 length;
        U8 group_index;
        U8 pad1;
        U8 pad2;
        U8 pad3;
        F32 life;
        F32 alpha;
        F32 alpha_vel;
        F32 length_vel;
        F32 length_max;
        F32 gravity;
        U32 pad4;
    };

    struct streak_emitter
    {
        S32 type;
        xVec3 loc;
        F32 emitted;
        F32 life;
        F32 delay;
    };

    struct streak_group
    {
        RwTexture* texture;
        U16 start;
        U16 size;
    };

    struct spark_particle
    {
        xVec3 loc;
        F32 scale;
        xVec3 vel;
        F32 life;
        U8 group_index;
        U8 pad1;
        U8 pad2;
        U8 pad3;
        F32 alpha;
        F32 alpha_vel;
        F32 gravity;
    };

    struct spark_emitter
    {
        S32 type;
        xVec3 loc;
        F32 emitted;
        F32 life;
        F32 delay;
    };

    struct spark_group
    {
        RwTexture* texture;
        U16 start;
        U16 size;
    };

    struct material_group
    {
        RwTexture* texture_fire;
        RwTexture* texture_smoke;
    };

    struct presence_node : presence_volume
    {
        S32 flags;
        F32 delay;
        F32 time;
        F32 end_time;
        F32 radius_vel;
        xSpacePartitionTree2D::entry_data* space_entry;
        zExplosion::trail_emitter* trail;
    };

    void load_runtime_configs();
    void update_sparks(F32 dt);
    void update_spark_emitters(F32 dt);
    void update_streaks(F32 dt);
    void update_streak_emitters(F32 dt);
    void load_ini_configs();
    void load_default_config_just_sparks(ini_config& cfg);
    void load_default_config_gauntlet_boom(ini_config& cfg);
    void load_default_config_fire_trail(ini_config& cfg);
    void load_default_config_plasma(ini_config& cfg);
    void load_default_config_fire(ini_config& cfg);
    void load_config(ini_config& cfg, xIniSection& sec, xIniFile& ini);
    void reset_config(ini_config& cfg);
    S32 compare_ini_values(const void* e1, const void* e2);
    S32 compare_ini_sections(const void* e1, const void* e2);
    void ptank_render();
    void ptank_render_smoke_frontface(ptank_pool__pos_color_size& pool, const ordered_mote* om,
                                      const ordered_mote* end_om);
    void ptank_render_fire(ptank_pool__pos_color_size& pool, const ordered_mote* om,
                           const ordered_mote* end_om);
    void ptank_render_smoke_backface(ptank_pool__pos_color_size& pool, const ordered_mote* om,
                                     const ordered_mote* end_om);
    void ptank_render_sparks();
    void ptank_render_streaks();
    ordered_mote* transform_motes(ordered_mote* om);
    void emit_sparks(const runtime_config& cfg, const xVec3& loc, S32 amount);
    void emit_streaks(const runtime_config& cfg, const xVec3& loc, S32 amount);
    void launch_line(S32 type, S32 size, const xVec3& loc0, const xVec3& loc1, const xVec3& offset,
                     F32 age_delta);

    const char ini_filename[] = "EXPLODE.INI";
    // Unreferenced; the linker strips it from the DOL but its strings stay in the pool.
    char* decal_type_labels[4] = { "None", "Impact", "Burn", "Plasma" };
    const U32 decal_type_ids[4] = { 0x0A887D30, 0x0B22C27E, 0x08EE6E37, 0xD8354BEA };
    const S32 decal_blend_mode[3] = { 0, 0, 2 };
    const S32 decal_flags[3] = { 8, 24, 24 };

    struct animix
    {
        F32 r;
        F32 g;
        F32 b;
        F32 a;
        F32 scale_x;
        F32 scale_y;
    };

    struct curve_node_1
    {
        F32 t;
        animix ani;
    };

    const curve_node_1 decal_anim_impact[3] = {
        { 0.0f, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f } },
        { 0.9f, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f } },
        { 1.0f, { 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f } },
    };
    const curve_node_1 decal_anim_burn[7] = {
        { 0.0f, { 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f } },
        { 0.05f, { 1.0f, 1.0f, 0.6f, 1.0f, 1.0f, 1.0f } },
        { 0.15f, { 0.8f, 0.6f, 0.2f, 1.0f, 1.0f, 1.0f } },
        { 0.3f, { 0.6f, 0.2f, 0.0f, 1.0f, 1.0f, 1.0f } },
        { 0.45f, { 0.2f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f } },
        { 0.65f, { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f } },
        { 1.0f, { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f } },
    };
    const curve_node_1 decal_anim_plasma[3] = {
        { 0.0f, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f } },
        { 0.05f, { 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f } },
        { 1.0f, { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f } },
    };

    const default_type_loader default_types[5] = {
        { "Default Fire", load_default_config_fire },
        { "Default Plasma", load_default_config_plasma },
        { "Default Fire Trail", load_default_config_fire_trail },
        { "Gauntlet Boom", load_default_config_gauntlet_boom },
        { "Just Sparks", load_default_config_just_sparks },
    };

    xResponseCurve decal_curve[3];
#define NUM_DEFAULT_TYPES (sizeof(default_types) / sizeof(default_types[0]))

    mote* motes;
    S32 motes_used;
    streak_particle* streaks;
    S32 streaks_used;
    streak_emitter* streak_emitters;
    S32 streak_emitters_used;
    streak_group* streak_groups;
    S32 streak_groups_used;
    spark_particle* sparks;
    S32 sparks_used;
    spark_emitter* spark_emitters;
    S32 spark_emitters_used;
    spark_group* spark_groups;
    S32 spark_groups_used;
    ini_config* ini_configs;
    runtime_config* runtime_configs;
    material_group* materials;
    // The PS2 DWARF says uint32, but GC find_type compares it signed (cmpwi).
    S32 configs_size;
    S32 materials_used;
    presence_node presi[MAX_PRESENCE];
    S32 presi_size;

    // No GC symbols for destroy_presence and move_presence (always inlined); the names are
    // Ratatouille's.
    inline void destroy_presence(presence_node* presence)
    {
        if (presence->trail != NULL)
        {
            presence->trail->presence = NULL;
        }

        xTransient::space_tree.erase(presence->space_entry);

        --presi_size;
        presence_node& last = presi[presi_size];
        if (presence != &last)
        {
            *presence = last;
            presence->space_entry->value = presence;
            if (presence->trail != NULL)
            {
                presence->trail->presence = presence;
            }
        }
    }

    inline void move_presence(presence_node* presence, const xVec3& center, F32 radius)
    {
        presence->center = center;
        presence->radius = radius;

        xTransient::space_tree.move(presence->space_entry, center.x, center.z, radius);
    }

    inline void kill_spark(spark_particle& spark)
    {
        spark_group& group = spark_groups[spark.group_index];
        --group.size;
        spark_particle* hole = sparks + group.start + group.size;
        spark = *hole;

        spark_group* end_group = spark_groups + spark_groups_used;
        for (spark_group* g = &group + 1; g < end_group; ++g)
        {
            --g->start;
            spark_particle* last = sparks + g->start + g->size;
            *hole = *last;
            hole = last;
        }

        --sparks_used;
    }

    inline void kill_streak(streak_particle& streak)
    {
        streak_group& group = streak_groups[streak.group_index];
        --group.size;
        streak_particle* hole = streaks + group.start + group.size;
        streak = *hole;

        streak_group* end_group = streak_groups + streak_groups_used;
        for (streak_group* g = &group + 1; g < end_group; ++g)
        {
            --g->start;
            streak_particle* last = streaks + g->start + g->size;
            *hole = *last;
            hole = last;
        }

        --streaks_used;
    }

    inline presence_node* create_presence(const xVec3& center, F32 delay, F32 life, F32 knockback,
                                          F32 radius_begin, F32 radius_end, F32 damage_player,
                                          F32 damage_npc, F32 damage_other, bool infinite,
                                          zExplosion::trail_emitter* trail)
    {
        if (presi_size >= MAX_PRESENCE)
        {
            return NULL;
        }

        if (xTransient::space_tree.full())
        {
            return NULL;
        }

        if (damage_player + damage_npc + damage_other <= 0.0f)
        {
            return NULL;
        }

        presence_node& node = presi[presi_size];
        ++presi_size;
        node.flags = 0;
        node.knockback = knockback;
        node.radius = radius_begin;
        node.center = center;
        node.delay = delay;
        node.time = 0.0f;
        node.end_time = life;

        if (life > 0.0f)
        {
            node.radius_vel = (radius_end - radius_begin) / life;
        }

        node.damage_player = damage_player;
        node.damage_npc = damage_npc;
        node.damage_other = damage_other;
        node.trail = trail;

        if (infinite)
        {
            node.flags |= 0x1;
        }

        node.space_entry = xTransient::space_tree.insert(&node, 2, center.x, center.z,
                                                         MAX(radius_begin, radius_end));
        return &node;
    }

    inline void refresh_streak_groups()
    {
        runtime_config* cfg = runtime_configs;
        runtime_config* end_cfg = cfg + configs_size;
        streak_groups_used = 0;

        for (; cfg != end_cfg; ++cfg)
        {
            if (cfg->texture.streak == NULL)
            {
                cfg->streak_group_index = -1;
                continue;
            }

            S32 i;
            for (i = 0; i < streak_groups_used; ++i)
            {
                if (cfg->texture.streak == streak_groups[i].texture)
                {
                    cfg->streak_group_index = i;
                    break;
                }
            }

            if (i == streak_groups_used)
            {
                cfg->streak_group_index = i;
                streak_groups[i].texture = cfg->texture.streak;
                ++streak_groups_used;
            }
        }

        streaks_used = 0;
        streak_group* sg = streak_groups;
        streak_group* endsg = sg + streak_groups_used;
        for (; sg != endsg; ++sg)
        {
            sg->start = sg->size = 0;
        }
    }

    inline void refresh_spark_groups()
    {
        runtime_config* cfg = runtime_configs;
        runtime_config* end_cfg = cfg + configs_size;
        spark_groups_used = 0;

        for (; cfg != end_cfg; ++cfg)
        {
            if (cfg->texture.spark == NULL)
            {
                cfg->spark_group_index = -1;
                continue;
            }

            S32 i;
            for (i = 0; i < spark_groups_used; ++i)
            {
                if (cfg->texture.spark == spark_groups[i].texture)
                {
                    cfg->spark_group_index = i;
                    break;
                }
            }

            if (i == spark_groups_used)
            {
                cfg->spark_group_index = i;
                spark_groups[i].texture = cfg->texture.spark;
                ++spark_groups_used;
            }
        }

        sparks_used = 0;
        spark_group* sg = spark_groups;
        spark_group* endsg = sg + spark_groups_used;
        for (; sg != endsg; ++sg)
        {
            sg->start = sg->size = 0;
        }
    }

    // No GC symbol: always inlined (name and locals from the PS2 DWARF).
    inline void refresh_materials()
    {
        runtime_config* cfg = runtime_configs;
        materials_used = 0;
        runtime_config* end_cfg = cfg + configs_size;

        for (; cfg != end_cfg; ++cfg)
        {
            S32 mat_index;
            for (mat_index = 0; mat_index < materials_used; ++mat_index)
            {
                if (cfg->texture.fire == materials[mat_index].texture_fire &&
                    cfg->texture.smoke == materials[mat_index].texture_smoke)
                {
                    cfg->material_index = mat_index;
                    break;
                }
            }

            if (mat_index >= materials_used)
            {
                cfg->material_index = materials_used;
                materials[materials_used].texture_fire = cfg->texture.fire;
                materials[materials_used].texture_smoke = cfg->texture.smoke;
                ++materials_used;
            }
        }
    }

    // No GC or PS2 symbol (always inlined); the name is Ratatouille's.
    inline void load_config_textures(runtime_config& ocfg, const ini_config& icfg)
    {
        ocfg.texture.fire =
            icfg.texture.fire == 0 ? NULL : (RwTexture*)xSTFindAsset(icfg.texture.fire, NULL);
        ocfg.texture.smoke =
            icfg.texture.smoke == 0 ? NULL : (RwTexture*)xSTFindAsset(icfg.texture.smoke, NULL);
        ocfg.texture.streak =
            icfg.texture.streak == 0 ? NULL : (RwTexture*)xSTFindAsset(icfg.texture.streak, NULL);
        ocfg.texture.spark =
            icfg.texture.spark == 0 ? NULL : (RwTexture*)xSTFindAsset(icfg.texture.spark, NULL);
        ocfg.texture.glare =
            icfg.texture.glare == 0 ? NULL : (RwTexture*)xSTFindAsset(icfg.texture.glare, NULL);
        ocfg.texture.decal =
            icfg.texture.decal == 0 ? NULL : (RwTexture*)xSTFindAsset(icfg.texture.decal, NULL);

        if (ocfg.texture.fire != NULL)
        {
            xFXHighDynamicRangeTunePalette(ocfg.texture.fire->raster, 1.0f, true);
        }

        if (ocfg.texture.streak != NULL)
        {
            xFXHighDynamicRangeTunePalette(ocfg.texture.streak->raster, 1.0f, true);
        }

        if (ocfg.texture.spark != NULL)
        {
            xFXHighDynamicRangeTunePalette(ocfg.texture.spark->raster, 1.0f, true);
        }
    }

    // No GC or PS2 symbol: on PS2 this whole sequence is attributed to a single line of
    // scene_enter, so it was an inlined helper (name unknown).
    inline void reset_all()
    {
        presence_node* node = presi;
        presence_node* end_node = node + presi_size;
        for (; node != end_node; ++node)
        {
            node->trail->presence = NULL;
        }

        motes_used = 0;
        streak_emitters_used = 0;
        presi_size = 0;
        refresh_streak_groups();
        spark_emitters_used = 0;
        refresh_spark_groups();
    }

    // No GC symbols for the helpers below: retail inlines them all into zExplosion::update.
    inline void update_configs(F32 dt)
    {
        runtime_config* cfg = runtime_configs;
        runtime_config* end_cfg = cfg + configs_size;
        for (; cfg != end_cfg; ++cfg)
        {
            cfg->vel_decay_mult = exp(-cfg->vel_decay * dt);
        }
    }

    inline void damage_reactives(presence_node& presence)
    {
        zEntSimpleObj* reactive_hits[16];
        xSphere sphere;
        sphere.center = presence.center;
        sphere.r = presence.radius;

        S32 reactive_hits_size = zReactiveHitTestSphere(reactive_hits, 16, sphere);
        for (S32 i = 0; i < reactive_hits_size; ++i)
        {
            zReactiveDamage(*reactive_hits[i], zHS_EXPLOSION, presence.damage_other);
        }
    }

    inline void update_presence(F32 dt)
    {
        presence_node* presence = presi;
        presence_node* end = presence + presi_size;
        while (presence != end)
        {
            damage_reactives(*presence);

            if (!(presence->flags & 0x1))
            {
                presence->delay -= dt;
                if (presence->delay > 0.0f)
                {
                    ++presence;
                    continue;
                }

                presence->time += dt;
                if (presence->time >= presence->end_time)
                {
                    destroy_presence(presence);
                    --end;
                    continue;
                }

                presence->radius += presence->radius_vel * dt;
            }

            ++presence;
        }
    }

    inline void update_motes(F32 dt)
    {
        mote* m = motes;
        mote* endm = m + motes_used;
        while (m != endm)
        {
            m->age += m->age_rate * dt;
            if (m->age >= 1.0f)
            {
                --endm;
                --motes_used;
                memcpy(m, endm, sizeof(mote));
                continue;
            }

            const runtime_config& cfg = runtime_configs[m->type];
            cfg.anim.eval_linear(m->age, &m->anim.alpha);
            m->vel *= cfg.vel_decay_mult;

            F32 dloc = m->vel * dt;
            m->loc.x += dloc * m->dir.x;
            m->loc.y += dloc * m->dir.y + m->anim.rise * dt;
            m->loc.z += dloc * m->dir.z;
            ++m;
        }
    }
} // namespace

namespace
{
    // No GC symbols for the helpers below (always inlined); their names are Ratatouille's.
    inline xIniValue* get_value(const char* name, const xIniValue* values, S32 values_size)
    {
        xIniValue key = { (char*)name, NULL };
        return (xIniValue*)bsearch(&key, values, values_size, sizeof(xIniValue),
                                   compare_ini_values);
    }

    inline void read_value(F32& v, const char* name, const xIniValue* values, S32 values_size)
    {
        const xIniValue* iv = get_value(name, values, values_size);
        if (iv != NULL)
        {
            v = atof(iv->val);
        }
    }

    inline void read_value(U32& v, const char* name, const xIniValue* values, S32 values_size)
    {
        const xIniValue* iv = get_value(name, values, values_size);
        if (iv != NULL)
        {
            v = atoi(iv->val);
        }
    }

    inline void read_hashed_text(U32& v, const char* name, const xIniValue* values, S32 values_size)
    {
        const xIniValue* iv = get_value(name, values, values_size);
        if (iv != NULL)
        {
            v = xStrHash(iv->val);
        }
    }

    inline void read_value(xColor_tag& v, const char* name, const xIniValue* values,
                           S32 values_size)
    {
        const xIniValue* iv = get_value(name, values, values_size);
        if (iv == NULL)
        {
            return;
        }

        char* nextoken;
        char* s = xStrTok(iv->val, " \t,;{}", &nextoken);
        U8* c = &v.r;
        U8* endc = c + 4;
        while (c != endc && s != NULL)
        {
            *c = atoi(s);
            s = xStrTok(NULL, " \t,;{}", &nextoken);
            ++c;
        }
    }

    inline U32 read_list(F32* v, U32 vsize, const char* name, const xIniValue* values,
                         S32 values_size)
    {
        const xIniValue* iv = get_value(name, values, values_size);
        if (iv == NULL)
        {
            return 0;
        }

        char* nextoken;
        char* s = xStrTok(iv->val, " \t,;{}", &nextoken);
        U32 total = 0;
        while (total < vsize && s != NULL)
        {
            v[total] = atof(s);
            s = xStrTok(NULL, " \t,;{}", &nextoken);
            ++total;
        }
        return total;
    }

    // No GC symbol: always inlined into load_config.
    inline S32 find_decal_type(U32 id)
    {
        const U32* it = decal_type_ids;
        const U32* end = it + 4;
        for (; it != end; ++it)
        {
            if (id == *it)
            {
                return it - decal_type_ids;
            }
        }
        return 0;
    }

    // No GC symbol: always inlined into zExplosion::emit.
    inline void spawn_streak_emitter(S32 type, const xVec3& loc)
    {
        if (streak_emitters_used >= MAX_STREAK_EMITTERS)
        {
            return;
        }

        const runtime_config& cfg = runtime_configs[type];
        if (cfg.streak_group_index < 0 || cfg.streak.rate <= 0.0f || cfg.streak.life <= 0.0f)
        {
            return;
        }

        streak_emitter* emitter = streak_emitters + streak_emitters_used;
        ++streak_emitters_used;
        emitter->type = type;
        emitter->loc = loc;
        emitter->emitted = 0.0f;
        emitter->delay = cfg.streak.emit_delay;
        emitter->life = cfg.streak.emit_time;
    }

    // No GC symbol: always inlined into zExplosion::emit.
    inline void spawn_spark_emitter(S32 type, const xVec3& loc)
    {
        if (spark_emitters_used >= MAX_SPARK_EMITTERS)
        {
            return;
        }

        const runtime_config& cfg = runtime_configs[type];
        if (cfg.spark_group_index < 0 || cfg.spark.rate <= 0.0f || cfg.spark.life <= 0.0f)
        {
            return;
        }

        spark_emitter* emitter = spark_emitters + spark_emitters_used;
        ++spark_emitters_used;
        emitter->type = type;
        emitter->loc = loc;
        emitter->emitted = 0.0f;
        emitter->delay = cfg.spark.emit_delay;
        emitter->life = cfg.spark.emit_time;
    }

    // No GC symbol: always inlined.
    inline void random_direction(xVec3& dir)
    {
        F32 ang = 2 * PI * xurand();
        F32 uz = 2.0f * xurand() - 1.0f;
        F32 r = xsqrt(1.0f - uz * uz);
        dir.assign(r * icos(ang), r * isin(ang), uz);
    }

    // No GC symbol: always inlined.
    inline streak_particle* alloc_streak(S32 group_index)
    {
        streak_group& group = streak_groups[group_index];
        streak_particle* streak = streaks + group.start + group.size;
        ++group.size;

        streak_particle* tail = streaks + streaks_used;
        streak_group* mg = streak_groups + streak_groups_used - 1;
        streak_group* endmg = streak_groups + group_index;
        for (; mg > endmg; --mg)
        {
            streak_particle* next = streaks + mg->start;
            *tail = *next;
            tail = next;
            ++mg->start;
        }

        ++streaks_used;
        return streak;
    }

    // No GC symbol: always inlined (same shape as alloc_streak).
    inline spark_particle* alloc_spark(S32 group_index)
    {
        spark_group& group = spark_groups[group_index];
        spark_particle* spark = sparks + group.start + group.size;
        ++group.size;

        spark_particle* tail = sparks + sparks_used;
        spark_group* mg = spark_groups + spark_groups_used - 1;
        spark_group* endmg = spark_groups + group_index;
        for (; mg > endmg; --mg)
        {
            spark_particle* next = sparks + mg->start;
            *tail = *next;
            tail = next;
            ++mg->start;
        }

        ++sparks_used;
        return spark;
    }

    // No GC symbol: always inlined.
    inline U8 fast_cull(ordered_mote* om)
    {
        RwSphere worldsph;
        worldsph.center.x = om->loc.x;
        worldsph.center.y = om->loc.y;
        worldsph.center.z = om->loc.z;
        worldsph.radius = om->scale_fire;
        if (RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
        {
            return 0;
        }
        return 1;
    }

    // No GC symbol: always inlined into zExplosion::emit.
    inline void launch_sphere(S32 type, S32 size, const xVec3& loc)
    {
        const runtime_config& cfg = runtime_configs[type];
        mote* m = motes + motes_used;
        mote* endm = m + size;
        motes_used += size;

        for (; m != endm; ++m)
        {
            m->age = 0.0f;
            m->type = type;
            m->material_index = cfg.material_index;

            random_direction(m->dir);

            F32 r = cfg.emit_radius * xurand();
            m->loc.x = loc.x + r * m->dir.x;
            m->loc.y = loc.y + r * m->dir.y;
            m->loc.z = loc.z + r * m->dir.z;

            m->scale = cfg.scale_min + (cfg.scale_max - cfg.scale_min) * xurand();

            r = xurand();
            m->vel = cfg.vel_min + r * (cfg.vel_max - cfg.vel_min);
            m->age_rate = 1.0f / (cfg.life_min + (1.0f - r) * (cfg.life_max - cfg.life_min));
        }
    }

    void launch_line(S32 type, S32 size, const xVec3& loc0, const xVec3&, const xVec3& offset,
                     F32 age_delta)
    {
        const runtime_config& cfg = runtime_configs[type];
        xMat3x3 lookmat;
        xMat3x3LookVec(&lookmat, &offset);

        mote* m = motes + motes_used;
        mote* endm = m + size;
        motes_used += size;

        for (; m != endm; ++m)
        {
            m->type = type;
            m->material_index = cfg.material_index;

            F32 s = xurand();

            xMat3x3 rotmat;
            xMat3x3RotC(&rotmat, lookmat.at.x, lookmat.at.y, lookmat.at.z, 2 * PI * xurand());
            xMat3x3RMulVec(&m->dir, &rotmat, &lookmat.right);

            F32 r = cfg.emit_radius * xurand();
            m->loc.x = loc0.x + s * offset.x + r * m->dir.x;
            m->loc.y = loc0.y + s * offset.y + r * m->dir.y;
            m->loc.z = loc0.z + s * offset.z + r * m->dir.z;

            m->scale = cfg.scale_min + (cfg.scale_max - cfg.scale_min) * xurand();

            r = xurand();
            m->vel = cfg.vel_min + r * (cfg.vel_max - cfg.vel_min);
            m->age_rate = 1.0f / (cfg.life_min + (1.0f - r) * (cfg.life_max - cfg.life_min));
            m->age = m->age_rate * ((1.0f - s) * age_delta);

            if (m->age > 0.0001f && m->age < 1.0f)
            {
                cfg.anim.eval_linear(m->age, &m->anim.alpha);
                m->vel *= xexp(-cfg.vel_decay * m->age);

                F32 dloc = m->vel * m->age;
                m->loc.x += dloc * m->dir.x;
                m->loc.y += dloc * m->dir.y + m->anim.rise * m->age;
                m->loc.z += dloc * m->dir.z;
            }
        }
    }

    void emit_streaks(const runtime_config& cfg, const xVec3& loc, S32 amount)
    {
        S32 remaining = MAX_STREAKS - streaks_used;
        if (amount > remaining)
        {
            amount = remaining;
        }

        S32 group = cfg.streak_group_index;
        if (group < 0)
        {
            return;
        }

        F32 alpha_vel = -cfg.streak.alpha / cfg.streak.life;

        for (S32 i = 0; i < amount; ++i)
        {
            streak_particle* streak = alloc_streak(group);
            streak->group_index = group;
            streak->width = cfg.streak.width;
            streak->life = cfg.streak.life;
            streak->length = 0.0f;
            streak->length_max = cfg.streak.length;
            streak->length_vel = cfg.streak.vel;
            streak->alpha = cfg.streak.alpha;
            streak->alpha_vel = alpha_vel;
            streak->gravity = cfg.streak.gravity;

            random_direction(streak->vel);
            streak->loc = loc;

            if (cfg.streak.radius > 0.0f)
            {
                F32 r = cfg.streak.radius * xurand();
                streak->loc.x += r * streak->vel.x;
                streak->loc.y += r * streak->vel.y;
                streak->loc.z += r * streak->vel.z;
            }

            streak->vel *= cfg.streak.vel;
        }
    }

    void emit_sparks(const runtime_config& cfg, const xVec3& loc, S32 amount)
    {
        S32 remaining = MAX_SPARKS - sparks_used;
        if (amount > remaining)
        {
            amount = remaining;
        }

        S32 group = cfg.spark_group_index;
        if (group < 0)
        {
            return;
        }

        F32 alpha_vel = -cfg.spark.alpha / cfg.spark.life;
        F32 velrange = cfg.spark.vel_max - cfg.spark.vel_min;

        for (S32 i = 0; i < amount; ++i)
        {
            spark_particle* spark = alloc_spark(group);
            spark->group_index = group;
            spark->scale = cfg.spark.scale;
            spark->life = cfg.spark.life;
            spark->alpha = cfg.spark.alpha;
            spark->alpha_vel = alpha_vel;
            spark->gravity = cfg.spark.gravity;

            random_direction(spark->vel);
            spark->loc = loc;

            if (cfg.spark.radius > 0.0f)
            {
                F32 r = cfg.spark.radius * xurand();
                spark->loc.x += r * spark->vel.x;
                spark->loc.y += r * spark->vel.y;
                spark->loc.z += r * spark->vel.z;
            }

            F32 velmag = cfg.spark.vel_min + velrange * xurand();
            spark->vel *= velmag;
        }
    }

    ordered_mote* transform_motes(ordered_mote* om)
    {
        RwFrame* cam_frame = RwCameraGetFrame(globals.screen->icam);
        const xMat4x3& cam_mat = *(const xMat4x3*)RwFrameGetMatrix(cam_frame);
        F32 dx = cam_mat.at.x;
        F32 dy = cam_mat.at.y;
        F32 dz = cam_mat.at.z;
        F32 cam_depth = cam_mat.pos.dot(cam_mat.at);

        mote* m = motes;
        mote* endm = m + motes_used;
        for (; m != endm; ++m)
        {
            const runtime_config& cfg = runtime_configs[m->type];
            om->loc = m->loc;

            F32 scale = m->scale * m->anim.scale;

            if (m->anim.heat <= 0.0f)
            {
                om->scale_fire = scale * cfg.scale_smoke;

                RwSphere worldsph;
                worldsph.center.x = om->loc.x;
                worldsph.center.y = om->loc.y;
                worldsph.center.z = om->loc.z;
                worldsph.radius = om->scale_fire;
                if (!RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
                {
                    continue;
                }

                om->scale_smoke = om->scale_fire;
                om->alpha_fire = 0;
                om->alpha_smoke = 255.99f * ((1.0f - m->anim.heat) * m->anim.alpha);
            }
            else if (m->anim.heat >= 1.0f)
            {
                om->scale_fire = scale * cfg.scale_fire;

                RwSphere worldsph;
                worldsph.center.x = om->loc.x;
                worldsph.center.y = om->loc.y;
                worldsph.center.z = om->loc.z;
                worldsph.radius = om->scale_fire;
                if (!RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
                {
                    continue;
                }

                om->alpha_smoke = 0;
                om->alpha_fire = 255.99f * (m->anim.heat * m->anim.alpha);
                om->glow = 255.99f * (m->anim.heat * m->anim.glow);
            }
            else
            {
                // Both the PS2 and GC builds clear the culled half with a single (unaligned) word
                // store over the U8 fields, so the original wrote it as one 32-bit store.
                om->scale_fire = scale * cfg.scale_smoke;
                if (fast_cull(om))
                {
                    *(U32*)&om->alpha_smoke = 0;
                }
                else
                {
                    om->scale_smoke = om->scale_fire;
                    om->alpha_smoke = 255.99f * ((1.0f - m->anim.heat) * m->anim.alpha);
                }

                om->scale_fire = scale * cfg.scale_fire;
                if (fast_cull(om))
                {
                    *(U32*)&om->alpha_fire = 0;
                }
                else
                {
                    om->alpha_fire = 255.99f * (m->anim.heat * m->anim.alpha);
                    om->glow = 255.99f * (m->anim.heat * m->anim.glow);
                }
            }

            if (om->alpha_fire != 0 || om->alpha_smoke != 0)
            {
                F32 dirdot = m->dir.x * dx + m->dir.y * dy + m->dir.z * dz;
                om->dirsign = dirdot < 0.0f ? 1 : 0;
                om->type = m->type;
                om->material_index = m->material_index;

                F32 zdepth = om->loc.x * dx + om->loc.y * dy + om->loc.z * dz - cam_depth;
                om->sort_key = *(U32*)&zdepth;
                om->sort_key = ~((om->sort_key >> 4) | (m->material_index << 27));
                ++om;
            }
        }

        return om;
    }

    void ptank_render_streaks()
    {
        if (streaks_used <= 0)
        {
            return;
        }

        RwFrame* cam_frame = RwCameraGetFrame(globals.screen->icam);
        xMat4x3& cam_mat = *(xMat4x3*)RwFrameGetMatrix(cam_frame);

        ptank_pool__color_mat pool;
        pool.rs.flags = 0xC;
        pool.rs.src_blend = rwBLENDONE;
        pool.rs.dst_blend = rwBLENDONE;
        pool.order_group = 'HOBO';
        pool.order_index = 5;

        U8 min_alpha = xFXHighDynamicRangeGetBackgroundGlow();

        streak_group* group = streak_groups;
        streak_group* end_group = streak_groups + streak_groups_used;
        for (; group != end_group; ++group)
        {
            if (group->size == 0)
            {
                continue;
            }

            pool.rs.texture = group->texture;
            pool.reset();

            streak_particle* streak = streaks + group->start;
            streak_particle* end_streak = streak + group->size;
            for (; streak != end_streak; ++streak)
            {
                RwSphere worldsph;
                worldsph.center.x = streak->loc.x;
                worldsph.center.y = streak->loc.y;
                worldsph.center.z = streak->loc.z;
                worldsph.radius = streak->width;

                if (!RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
                {
                    continue;
                }

                pool.next();
                if (!pool.valid())
                {
                    break;
                }

                pool.mat->up = streak->vel;
                pool.mat->at = cam_mat.at;
                xVec3Cross(&pool.mat->right, &pool.mat->up, &pool.mat->at);
                pool.mat->right.safe_normalize(cam_mat.right);
                pool.mat->up.safe_normalize(cam_mat.up);

                pool.mat->pos = streak->loc;
                pool.mat->up *= streak->length;
                pool.mat->right *= streak->width;
                pool.mat->pos.x -= 0.5f * pool.mat->up.x;
                pool.mat->pos.y -= 0.5f * pool.mat->up.y;
                pool.mat->pos.z -= 0.5f * pool.mat->up.z;

                U8 alpha;
                if (streak->alpha >= 1.0f)
                {
                    alpha = 0xFF;
                }
                else
                {
                    alpha = (U8)(255.0f * streak->alpha + 0.5f);
                }

                pool.color->r = pool.color->g = pool.color->b = alpha;
                pool.color->a = MAX(alpha, min_alpha);
            }

            pool.flush();
        }
    }

    void ptank_render_sparks()
    {
        if (sparks_used <= 0)
        {
            return;
        }

        ptank_pool__pos_color_size pool;
        pool.rs.flags = 0xC;
        pool.rs.src_blend = rwBLENDONE;
        pool.rs.dst_blend = rwBLENDONE;
        pool.order_group = 'HOBO';
        pool.order_index = 8;

        U8 min_alpha = xFXHighDynamicRangeGetBackgroundGlow();

        spark_group* group = spark_groups;
        spark_group* end_group = spark_groups + spark_groups_used;
        for (; group != end_group; ++group)
        {
            if (group->size == 0)
            {
                continue;
            }

            pool.rs.texture = group->texture;
            pool.reset();

            spark_particle* spark = sparks + group->start;
            spark_particle* end_spark = spark + group->size;
            for (; spark != end_spark; ++spark)
            {
                RwSphere worldsph;
                worldsph.center.x = spark->loc.x;
                worldsph.center.y = spark->loc.y;
                worldsph.center.z = spark->loc.z;
                worldsph.radius = spark->scale;

                if (!RwCameraFrustumTestSphere(xglobals->screen->icam, &worldsph))
                {
                    continue;
                }

                pool.next();
                if (!pool.valid())
                {
                    break;
                }

                *pool.pos = spark->loc;
                pool.size->assign(spark->scale);

                U8 alpha;
                if (spark->alpha >= 1.0f)
                {
                    alpha = 0xFF;
                }
                else
                {
                    alpha = (U8)(255.0f * spark->alpha + 0.5f);
                }

                pool.color->r = pool.color->g = pool.color->b = alpha;
                pool.color->a = MAX(alpha, min_alpha);
            }

            pool.flush();
        }
    }

    void ptank_render_smoke_backface(ptank_pool__pos_color_size& pool, const ordered_mote* om,
                                     const ordered_mote* end_om)
    {
        pool.rs.flags &= ~0x8;
        pool.rs.texture = NULL;
        pool.rs.src_blend = rwBLENDSRCALPHA;
        pool.rs.dst_blend = rwBLENDINVSRCALPHA;
        pool.order_index = 4;

        S32 material_index = -1;

        for (; om != end_om; ++om)
        {
            if (om->alpha_smoke == 0 || om->dirsign != 0)
            {
                continue;
            }

            if (material_index != om->material_index)
            {
                runtime_config& cfg = runtime_configs[om->type];
                if (material_index == -1)
                {
                    pool.rs.texture = cfg.texture.smoke;
                    pool.reset();
                }
                else if (pool.rs.texture != cfg.texture.smoke)
                {
                    pool.flush();
                    pool.rs.texture = cfg.texture.smoke;
                    pool.reset();
                }

                material_index = om->material_index;
            }

            pool.next();
            if (!pool.valid())
            {
                break;
            }

            *pool.pos = om->loc;
            pool.color->r = pool.color->g = pool.color->b = 0xFF;
            pool.color->a = om->alpha_smoke;
            pool.size->assign(om->scale_smoke);
        }

        if (material_index != -1)
        {
            pool.flush();
        }
    }

    void ptank_render_fire(ptank_pool__pos_color_size& pool, const ordered_mote* om,
                           const ordered_mote* end_om)
    {
        pool.rs.flags |= 0x8;
        pool.rs.texture = NULL;
        pool.rs.src_blend = rwBLENDONE;
        pool.rs.dst_blend = rwBLENDONE;
        pool.order_index = 5;

        S32 material_index = -1;
        U8 min_glow = xFXHighDynamicRangeGetBackgroundGlow();

        for (; om != end_om; ++om)
        {
            if (om->alpha_fire == 0)
            {
                continue;
            }

            if (material_index != om->material_index)
            {
                runtime_config& cfg = runtime_configs[om->type];
                if (material_index == -1)
                {
                    pool.rs.texture = cfg.texture.fire;
                    pool.reset();
                }
                else if (pool.rs.texture != cfg.texture.fire)
                {
                    pool.flush();
                    pool.rs.texture = cfg.texture.fire;
                    pool.reset();
                }

                material_index = om->material_index;
            }

            pool.next();
            if (!pool.valid())
            {
                break;
            }

            *pool.pos = om->loc;
            pool.color->r = pool.color->g = pool.color->b = om->alpha_fire;
            pool.color->a = MAX(om->glow, min_glow);
            pool.size->assign(om->scale_fire);
        }

        if (material_index != -1)
        {
            pool.flush();
        }
    }

    void ptank_render_smoke_frontface(ptank_pool__pos_color_size& pool, const ordered_mote* om,
                                      const ordered_mote* end_om)
    {
        pool.rs.flags &= ~0x8;
        pool.rs.texture = NULL;
        pool.rs.src_blend = rwBLENDSRCALPHA;
        pool.rs.dst_blend = rwBLENDINVSRCALPHA;
        pool.order_index = 6;

        S32 material_index = -1;

        for (; om != end_om; ++om)
        {
            if (om->alpha_smoke == 0 || om->dirsign == 0)
            {
                continue;
            }

            if (material_index != om->material_index)
            {
                runtime_config& cfg = runtime_configs[om->type];
                if (material_index == -1)
                {
                    pool.rs.texture = cfg.texture.smoke;
                    pool.reset();
                }
                else if (pool.rs.texture != cfg.texture.smoke)
                {
                    pool.flush();
                    pool.rs.texture = cfg.texture.smoke;
                    pool.reset();
                }

                material_index = om->material_index;
            }

            pool.next();
            if (!pool.valid())
            {
                break;
            }

            *pool.pos = om->loc;
            pool.color->r = pool.color->g = pool.color->b = 0xFF;
            pool.color->a = om->alpha_smoke;
            pool.size->assign(om->scale_smoke);
        }

        if (material_index != -1)
        {
            pool.flush();
        }
    }

    // 16 compiled-out placeholders keep GC/2.6 from auto-inlining this into zExplosion::pre_render
    // (>= 30 statements), as retail calls it. What they stood for is unknown.
    void ptank_render()
    {
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();

        ordered_mote* om_buffer = (ordered_mote*)xMemScratchPush(0x4000, 16);
        if (om_buffer == NULL)
        {
            return;
        }
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();

        ordered_mote* end_om = transform_motes(om_buffer);
        // The target computes the key offset at runtime from om_buffer, not as a constant offsetof.
        quicksort_int(om_buffer, end_om - om_buffer, sizeof(ordered_mote),
                      (U8*)&om_buffer->sort_key - (U8*)om_buffer);
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();
        xASSERT_PLACEHOLDER();

        ptank_pool__pos_color_size pool;
        pool.rs.flags = 4;
        pool.order_group = 'HOBO';
        pool.rs.src_blend = rwBLENDSRCALPHA;

        ptank_render_smoke_backface(pool, om_buffer, end_om);
        ptank_render_fire(pool, om_buffer, end_om);
        ptank_render_streaks();
        ptank_render_smoke_frontface(pool, om_buffer, end_om);
        ptank_render_sparks();

        xMemScratchPop(om_buffer);
    }

    S32 compare_ini_sections(const void* e1, const void* e2)
    {
        return stricmp(*(char* const*)e1, *(char* const*)e2);
    }

    S32 compare_ini_values(const void* e1, const void* e2)
    {
        return stricmp(*(char* const*)e1, *(char* const*)e2);
    }

    void reset_config(ini_config& cfg)
    {
        cfg.vel_decay = 10.0f;
        cfg.emit_radius = 0.1f;
        cfg.motes_min = 32;
        cfg.motes_max = 32;
        cfg.life_min = 1.0f;
        cfg.life_max = 2.0f;
        cfg.scale_min = 1.0f;
        cfg.scale_max = 2.0f;
        cfg.scale_fire = 1.0f;
        cfg.scale_smoke = 1.0f;
        cfg.vel_min = 1.0f;
        cfg.vel_max = 2.0f;
        cfg.sound = 0;
        cfg.anim = NULL;
        cfg.anim_size = 0;
        cfg.glare.life = 0.0f;
        cfg.glare.scale = 1.0f;
        cfg.glare.color.r = 0;
        cfg.glare.color.g = 0;
        cfg.glare.color.b = 0;
        cfg.glare.color.a = 0;
        cfg.warp.type = 0;
        cfg.warp.radius = 1.0f;
        cfg.warp.duration = 1.0f;
        cfg.warp.intensity = 1.0f;
        cfg.warp.freq = 1.0f;
        cfg.streak.life = 1.0f;
        cfg.streak.rate = 0.0f;
        cfg.streak.emit_delay = 0.0f;
        cfg.streak.emit_time = 0.0f;
        cfg.streak.width = 0.1f;
        cfg.streak.length = 1.0f;
        cfg.streak.vel = 10.0f;
        cfg.streak.radius = 0.1f;
        cfg.streak.gravity = -9.8f;
        cfg.streak.alpha = 1.0f;
        cfg.spark.life = 1.0f;
        cfg.spark.rate = 0.0f;
        cfg.spark.emit_delay = 0.0f;
        cfg.spark.emit_time = 0.0f;
        cfg.spark.scale = 1.0f;
        cfg.spark.vel_min = 1.0f;
        cfg.spark.vel_max = 1.0f;
        cfg.spark.radius = 0.1f;
        cfg.spark.gravity = -9.8f;
        cfg.spark.alpha = 1.0f;
        cfg.shake.time = 0.0f;
        cfg.shake.mag = 0.0f;
        cfg.shake.cycle = 1.0f;
        cfg.shake.rot_mag = 1.0f;
        cfg.shake.radius = 1.0f;
        cfg.presence.delay = 0.0f;
        cfg.presence.life = 0.0f;
        cfg.presence.damage_player = 0.0f;
        cfg.presence.damage_npc = 0.0f;
        cfg.presence.damage_other = 0.0f;
        cfg.presence.knockback = 0.0f;
        cfg.presence.radius_begin = 0.0f;
        cfg.presence.radius_end = 0.0f;
        cfg.decal.type = 0;
        cfg.decal.radius = 0.0f;
        cfg.decal.life = 0.0f;
        cfg.decal.width = 0.0f;
        cfg.decal.max_dist = 0.0f;
        cfg.texture.fire = 0;
        cfg.texture.smoke = 0;
        cfg.texture.streak = 0;
        cfg.texture.spark = 0;
        cfg.texture.glare = 0;
        cfg.texture.decal = 0;
    }

    void load_config(ini_config& cfg, xIniSection& sec, xIniFile& ini)
    {
        xIniValue* values = &ini.Values[sec.first];
        qsort(values, sec.count, sizeof(xIniValue), compare_ini_values);
        read_value(cfg.vel_decay, "vel_decay", values, sec.count);
        read_value(cfg.emit_radius, "emit_radius", values, sec.count);
        read_value(cfg.motes_min, "motes_min", values, sec.count);
        read_value(cfg.motes_max, "motes_max", values, sec.count);
        read_value(cfg.life_min, "life_min", values, sec.count);
        read_value(cfg.life_max, "life_max", values, sec.count);
        read_value(cfg.scale_min, "scale_min", values, sec.count);
        read_value(cfg.scale_max, "scale_max", values, sec.count);
        read_value(cfg.scale_fire, "scale_fire", values, sec.count);
        read_value(cfg.scale_smoke, "scale_smoke", values, sec.count);
        read_value(cfg.vel_min, "vel_min", values, sec.count);
        read_value(cfg.vel_max, "vel_max", values, sec.count);
        read_hashed_text(cfg.sound, "sound", values, sec.count);
        read_value(cfg.glare.life, "glare.life", values, sec.count);
        read_value(cfg.glare.scale, "glare.scale", values, sec.count);
        read_value(cfg.glare.color, "glare.color", values, sec.count);
        read_hashed_text(cfg.warp.type, "warp.type", values, sec.count);
        read_value(cfg.warp.radius, "warp.radius", values, sec.count);
        read_value(cfg.warp.duration, "warp.duration", values, sec.count);
        read_value(cfg.warp.intensity, "warp.intensity", values, sec.count);
        read_value(cfg.warp.freq, "warp.freq", values, sec.count);
        read_value(cfg.streak.life, "streak.life", values, sec.count);
        read_value(cfg.streak.rate, "streak.rate", values, sec.count);
        read_value(cfg.streak.emit_delay, "streak.emit_delay", values, sec.count);
        read_value(cfg.streak.emit_time, "streak.emit_time", values, sec.count);
        read_value(cfg.streak.width, "streak.width", values, sec.count);
        read_value(cfg.streak.length, "streak.length", values, sec.count);
        read_value(cfg.streak.vel, "streak.vel", values, sec.count);
        read_value(cfg.streak.radius, "streak.radius", values, sec.count);
        read_value(cfg.streak.gravity, "streak.gravity", values, sec.count);
        read_value(cfg.streak.alpha, "streak.alpha", values, sec.count);
        read_value(cfg.spark.life, "spark.life", values, sec.count);
        read_value(cfg.spark.rate, "spark.rate", values, sec.count);
        read_value(cfg.spark.emit_delay, "spark.emit_delay", values, sec.count);
        read_value(cfg.spark.emit_time, "spark.emit_time", values, sec.count);
        read_value(cfg.spark.scale, "spark.scale", values, sec.count);
        read_value(cfg.spark.vel_min, "spark.vel_min", values, sec.count);
        read_value(cfg.spark.vel_max, "spark.vel_max", values, sec.count);
        read_value(cfg.spark.radius, "spark.radius", values, sec.count);
        read_value(cfg.spark.gravity, "spark.gravity", values, sec.count);
        read_value(cfg.spark.alpha, "spark.alpha", values, sec.count);
        read_value(cfg.shake.time, "shake.time", values, sec.count);
        read_value(cfg.shake.mag, "shake.mag", values, sec.count);
        read_value(cfg.shake.cycle, "shake.cycle", values, sec.count);
        read_value(cfg.shake.rot_mag, "shake.rot_mag", values, sec.count);
        read_value(cfg.shake.radius, "shake.radius", values, sec.count);
        read_value(cfg.presence.delay, "presence.delay", values, sec.count);
        read_value(cfg.presence.life, "presence.life", values, sec.count);
        read_value(cfg.presence.knockback, "presence.knockback", values, sec.count);
        read_value(cfg.presence.radius_begin, "presence.radius_begin", values, sec.count);
        read_value(cfg.presence.radius_end, "presence.radius_end", values, sec.count);
        read_value(cfg.presence.damage_player, "presence.damage_player", values, sec.count);
        read_value(cfg.presence.damage_npc, "presence.damage_npc", values, sec.count);
        read_value(cfg.presence.damage_other, "presence.damage_other", values, sec.count);
        // Original bug: decal_type_hash stays uninitialized when the section has no
        // "decal.type" key (retail reads whatever the register held).
        U32 decal_type_hash;
        read_hashed_text(decal_type_hash, "decal.type", values, sec.count);
        cfg.decal.type = find_decal_type(decal_type_hash);
        read_value(cfg.decal.radius, "decal.radius", values, sec.count);
        read_value(cfg.decal.life, "decal.life", values, sec.count);
        read_value(cfg.decal.width, "decal.width", values, sec.count);
        read_value(cfg.decal.max_dist, "decal.max_dist", values, sec.count);
        read_hashed_text(cfg.texture.fire, "texture.fire", values, sec.count);
        read_hashed_text(cfg.texture.smoke, "texture.smoke", values, sec.count);
        read_hashed_text(cfg.texture.streak, "texture.streak", values, sec.count);
        read_hashed_text(cfg.texture.spark, "texture.spark", values, sec.count);
        read_hashed_text(cfg.texture.glare, "texture.glare", values, sec.count);
        read_hashed_text(cfg.texture.decal, "texture.decal", values, sec.count);
        char buffer[32];
        U32 total;
        for (total = 0; total < 10; ++total)
        {
            sprintf(buffer, "anim %d", total);
            if (get_value(buffer, values, sec.count) == NULL)
            {
                break;
            }
        }

        if (total != 0)
        {
            cfg.anim = (curve_node*)xMemAlloc(gActiveHeap, total * sizeof(curve_node), 0);
            cfg.anim_size = total;
            memset(cfg.anim, 0, total * sizeof(curve_node));

            for (U32 i = 0; i < total; ++i)
            {
                sprintf(buffer, "anim %d", i);
                // Each node is six floats: t, alpha, scale, heat, rise, glow.
                read_list(&cfg.anim[i].t, 6, buffer, values, sec.count);
            }
        }
    }

    void load_default_config_fire(ini_config& cfg)
    {
        cfg.vel_decay = 18.0f;
        cfg.emit_radius = 0.1f;
        cfg.motes_min = 32;
        cfg.motes_max = 32;
        cfg.life_min = 0.6f;
        cfg.life_max = 1.3f;
        cfg.scale_min = 0.4f;
        cfg.scale_max = 0.5f;
        cfg.scale_fire = 1.1f;
        cfg.scale_smoke = 1.4f;
        cfg.vel_min = 3.0f;
        cfg.vel_max = 14.0f;
        cfg.sound = 0;
        static curve_node anim[] = {
            { 0.0f, { 1.0f, 1.0f, 1.0f, 0.0f, 1.0f } },
            { 0.18f, { 1.0f, 2.0f, 1.0f, 0.0f, 1.0f } },
            { 0.5f, { 1.0f, 2.5f, 0.2f, 0.4f, 1.0f } },
            { 1.0f, { 0.0f, 3.0f, 0.0f, 0.4f, 1.0f } },
        };
        cfg.anim = anim;
        cfg.anim_size = 4;
        cfg.glare.life = 0.5f;
        cfg.glare.scale = 8.0f;
        cfg.glare.color.r = 0xff;
        cfg.glare.color.g = 0xff;
        cfg.glare.color.b = 0xff;
        cfg.glare.color.a = 200;
        cfg.warp.type = xStrHash("Shockwave");
        cfg.warp.radius = 1.0f;
        cfg.warp.duration = 0.5f;
        cfg.warp.intensity = 1.0f;
        cfg.warp.freq = 1.0f;
        cfg.streak.life = 0.15f;
        cfg.streak.rate = 200.0f;
        cfg.streak.emit_delay = 0.0f;
        cfg.streak.emit_time = 0.2f;
        cfg.streak.width = 0.02f;
        cfg.streak.length = 1.5f;
        cfg.streak.vel = 15.0f;
        cfg.streak.radius = 1.0f;
        cfg.streak.gravity = -30.0f;
        cfg.streak.alpha = 2.0f;
        cfg.spark.life = 0.75f;
        cfg.spark.rate = 50.0f;
        cfg.spark.emit_delay = 0.0f;
        cfg.spark.emit_time = 0.2f;
        cfg.spark.scale = 0.15f;
        cfg.spark.vel_min = 2.0f;
        cfg.spark.vel_max = 15.0f;
        cfg.spark.radius = 1.0f;
        cfg.spark.gravity = -9.8f;
        cfg.spark.alpha = 2.0f;
        cfg.shake.time = 1.0f;
        cfg.shake.mag = 0.2f;
        cfg.shake.cycle = 0.05f;
        cfg.shake.rot_mag = 0.2f;
        cfg.shake.radius = 10.0f;
        cfg.presence.delay = 0.0f;
        cfg.presence.life = 0.0f;
        cfg.presence.knockback = 0.0f;
        cfg.presence.radius_begin = 1.0f;
        cfg.presence.radius_end = 1.0f;
        cfg.presence.damage_player = 0.0f;
        cfg.presence.damage_npc = 0.0f;
        cfg.presence.damage_other = 1.0f;
        cfg.decal.type = 1;
        cfg.decal.radius = 1.0f;
        cfg.decal.life = 5.0f;
        cfg.decal.width = 0.5f;
        cfg.decal.max_dist = 2.0f;
        cfg.texture.fire = xStrHash("ExplosionFlash_Tan_64");
        cfg.texture.smoke = xStrHash("ExplosionFlash_Smoke_tan_64");
        cfg.texture.streak = xStrHash("spark_single2");
        cfg.texture.spark = xStrHash("spark_single2");
        cfg.texture.glare = xStrHash("LensFlare01");
        cfg.texture.decal = xStrHash("WallCrackDecal");
    }

    void load_default_config_plasma(ini_config& cfg)
    {
        cfg.vel_decay = 10.0f;
        cfg.emit_radius = 0.1f;
        cfg.motes_min = 32;
        cfg.motes_max = 32;
        cfg.life_min = 0.5f;
        cfg.life_max = 1.5f;
        cfg.scale_min = 0.4f;
        cfg.scale_max = 0.5f;
        cfg.scale_fire = 1.1f;
        cfg.scale_smoke = 1.4f;
        cfg.vel_min = 2.0f;
        cfg.vel_max = 10.0f;
        cfg.sound = 0;
        static curve_node anim[] = {
            { 0.0f, { 1.0f, 1.0f, 1.0f, 0.0f, 1.0f } },
            { 0.03f, { 1.0f, 2.0f, 1.0f, 0.0f, 1.0f } },
            { 0.5f, { 1.0f, 2.5f, 0.2f, 0.4f, 1.0f } },
            { 1.0f, { 0.0f, 3.0f, 0.0f, 0.4f, 1.0f } },
        };
        cfg.anim = anim;
        cfg.anim_size = 4;
        cfg.glare.life = 0.4f;
        cfg.glare.scale = 9.0f;
        cfg.glare.color.r = 0xff;
        cfg.glare.color.g = 0xff;
        cfg.glare.color.b = 0xff;
        cfg.glare.color.a = 128;
        cfg.warp.type = xStrHash("Shockwave");
        cfg.warp.radius = 1.0f;
        cfg.warp.duration = 0.5f;
        cfg.warp.intensity = 1.0f;
        cfg.warp.freq = 1.0f;
        cfg.streak.life = 0.2f;
        cfg.streak.rate = 200.0f;
        cfg.streak.emit_delay = 0.0f;
        cfg.streak.emit_time = 0.1f;
        cfg.streak.width = 0.05f;
        cfg.streak.length = 2.0f;
        cfg.streak.vel = 15.0f;
        cfg.streak.radius = 0.2f;
        cfg.streak.gravity = -9.8f;
        cfg.streak.alpha = 2.0f;
        cfg.spark.life = 0.75f;
        cfg.spark.rate = 0.0f;
        cfg.spark.emit_delay = 0.0f;
        cfg.spark.emit_time = 0.0f;
        cfg.spark.scale = 0.2f;
        cfg.spark.vel_min = 2.0f;
        cfg.spark.vel_max = 15.0f;
        cfg.spark.radius = 1.0f;
        cfg.spark.gravity = -9.8f;
        cfg.spark.alpha = 2.0f;
        cfg.shake.time = 0.0f;
        cfg.shake.mag = 0.0f;
        cfg.shake.cycle = 0.05f;
        cfg.shake.rot_mag = 0.2f;
        cfg.shake.radius = 7.0f;
        cfg.presence.delay = 0.0f;
        cfg.presence.life = 0.0f;
        cfg.presence.knockback = 0.0f;
        cfg.presence.radius_begin = 1.0f;
        cfg.presence.radius_end = 1.0f;
        cfg.presence.damage_player = 0.0f;
        cfg.presence.damage_npc = 0.0f;
        cfg.presence.damage_other = 1.0f;
        cfg.decal.type = 3;
        cfg.decal.radius = 1.0f;
        cfg.decal.life = 5.0f;
        cfg.decal.width = 0.5f;
        cfg.decal.max_dist = 2.0f;
        cfg.texture.fire = xStrHash("ExplosionFlash_Tan_64");
        cfg.texture.smoke = xStrHash("ExplosionFlash_Smoke_tan_64");
        cfg.texture.streak = 0;
        cfg.texture.spark = 0;
        cfg.texture.glare = xStrHash("LensFlare01");
        cfg.texture.decal = xStrHash("WallCrackDecal");
    }

    void load_default_config_fire_trail(ini_config& cfg)
    {
        cfg.vel_decay = 12.0f;
        cfg.emit_radius = 0.0f;
        cfg.motes_min = 50;
        cfg.motes_max = 60;
        cfg.life_min = 0.6f;
        cfg.life_max = 1.3f;
        cfg.scale_min = 0.4f;
        cfg.scale_max = 0.5f;
        cfg.scale_fire = 0.5f;
        cfg.scale_smoke = 0.69999999f;
        cfg.vel_min = 1.0f;
        cfg.vel_max = 2.0f;
        cfg.sound = 0;
        static curve_node anim[] = {
            { 0.0f, { 1.0f, 1.0f, 1.0f, 0.0f, 1.0f } }, { 0.18f, { 1.0f, 2.0f, 0.6f, 0.0f, 1.0f } },
            { 0.5f, { 1.0f, 2.5f, 0.3f, 0.4f, 1.0f } }, { 0.75f, { 1.0f, 3.0f, 0.0f, 0.4f, 1.0f } },
            { 1.0f, { 0.0f, 3.5f, 0.0f, 0.4f, 1.0f } },
        };
        cfg.anim = anim;
        cfg.anim_size = 5;
        cfg.glare.life = 1.0f;
        cfg.glare.scale = 4.0f;
        cfg.glare.color.r = 0xff;
        cfg.glare.color.g = 0xff;
        cfg.glare.color.b = 0xff;
        cfg.glare.color.a = 100;
        cfg.warp.type = 0;
        cfg.warp.radius = 0.6f;
        cfg.warp.duration = 0.5f;
        cfg.warp.intensity = 0.3f;
        cfg.warp.freq = 1.0f;
        cfg.streak.life = 0.15f;
        cfg.streak.rate = 40.0f;
        cfg.streak.emit_delay = 0.0f;
        cfg.streak.emit_time = 0.0f;
        cfg.streak.width = 0.015f;
        cfg.streak.length = 6.0f;
        cfg.streak.vel = 7.5f;
        cfg.streak.radius = 0.0f;
        cfg.streak.gravity = -9.8f;
        cfg.streak.alpha = 1.0f;
        cfg.spark.life = 0.75f;
        cfg.spark.rate = 0.0f;
        cfg.spark.emit_delay = 0.0f;
        cfg.spark.emit_time = 0.0f;
        cfg.spark.scale = 0.2f;
        cfg.spark.vel_min = 2.0f;
        cfg.spark.vel_max = 15.0f;
        cfg.spark.radius = 1.0f;
        cfg.spark.gravity = -9.8f;
        cfg.spark.alpha = 2.0f;
        cfg.shake.time = 0.0f;
        cfg.shake.mag = 0.0f;
        cfg.shake.cycle = 0.05f;
        cfg.shake.rot_mag = 0.2f;
        cfg.shake.radius = 7.0f;
        cfg.presence.delay = 0.0f;
        cfg.presence.life = 0.0f;
        cfg.presence.knockback = 0.0f;
        cfg.presence.radius_begin = 0.0f;
        cfg.presence.radius_end = 0.0f;
        cfg.presence.damage_player = 0.0f;
        cfg.presence.damage_npc = 0.0f;
        cfg.presence.damage_other = 0.0f;
        cfg.decal.type = 0;
        cfg.decal.radius = 0.0f;
        cfg.decal.life = 0.0f;
        cfg.decal.width = 0.0f;
        cfg.decal.max_dist = 0.0f;
        cfg.texture.fire = xStrHash("ExplosionFlash_Tan_64");
        cfg.texture.smoke = xStrHash("ExplosionFlash_Smoke_tan_64");
        cfg.texture.streak = xStrHash("spark_single2");
        cfg.texture.spark = xStrHash("spark_single2");
        cfg.texture.glare = xStrHash("LensFlare01");
        cfg.texture.decal = 0;
    }

    void load_default_config_gauntlet_boom(ini_config& cfg)
    {
        load_default_config_plasma(cfg);
        cfg.texture.fire = xStrHash("ExplosionFlash_Tan_64");
        cfg.texture.smoke = xStrHash("ExplosionFlash_Smoke_tan_64");
        cfg.texture.streak = xStrHash("spark_single2");
        cfg.texture.spark = xStrHash("spark_single2");
        cfg.texture.glare = xStrHash("LensFlare01");
        cfg.texture.decal = 0;
        cfg.spark.life = 0.75f;
        cfg.spark.rate = 50.0f;
        cfg.spark.emit_delay = 0.0f;
        cfg.spark.emit_time = 0.2f;
        cfg.spark.scale = 0.15f;
        cfg.spark.vel_min = 2.0f;
        cfg.spark.vel_max = 15.0f;
        cfg.spark.radius = 1.0f;
        cfg.spark.gravity = -9.8f;
        cfg.spark.alpha = 2.0f;
        memset(&cfg.shake, 0, sizeof(shake_config));
    }

    void load_default_config_just_sparks(ini_config& cfg)
    {
        load_default_config_fire(cfg);
        cfg.vel_decay = 0.0f;
        cfg.emit_radius = 0.0f;
        cfg.motes_min = 50;
        cfg.motes_max = 60;
        cfg.life_min = 0.6f;
        cfg.life_max = 1.3f;
        cfg.scale_min = 0.4f;
        cfg.scale_max = 0.5f;
        cfg.scale_fire = 0.0f;
        cfg.scale_smoke = 0.0f;
        cfg.vel_min = 1.0f;
        cfg.vel_max = 2.0f;
        cfg.sound = 0;
        cfg.glare.life = 0.0f;
        cfg.glare.scale = 0.0f;
        cfg.glare.color.r = 0xFF;
        cfg.glare.color.g = 0xFF;
        cfg.glare.color.b = 0xFF;
        cfg.glare.color.a = 0;
        memset(&cfg.warp, 0, sizeof(_class_0));
        memset(&cfg.streak, 0, sizeof(streak_config));
        memset(&cfg.shake, 0, sizeof(shake_config));
        memset(&cfg.presence, 0, sizeof(presence_config));
        memset(&cfg.decal, 0, sizeof(decal_config));
        memset(&cfg.texture, 0, sizeof(_class_1));
        cfg.texture.spark = xStrHash("spark_single2");
        cfg.spark.life = 0.75f;
        cfg.spark.rate = 40.0f;
        cfg.spark.emit_delay = 0.0f;
        cfg.spark.emit_time = 0.2f;
        cfg.spark.scale = 0.15f;
        cfg.spark.vel_min = 1.0f;
        cfg.spark.vel_max = 0.5f;
        cfg.spark.radius = 0.2f;
        cfg.spark.gravity = -9.8f;
        cfg.spark.alpha = 2.0f;
    }

    void load_ini_configs()
    {
        xIniFile* ini = NULL;
        xIniSection* sections = NULL;
        U32 memsize;
        configs_size = NUM_DEFAULT_TYPES;
        void* mem = iFileLoad(ini_filename, NULL, &memsize);

        if (mem != NULL)
        {
            ini = xIniParse((char*)mem, memsize);
            sections = (xIniSection*)RwMalloc(
                (ini->NumSections + NUM_DEFAULT_TYPES) * sizeof(xIniSection), 0);
            memset(sections, 0, NUM_DEFAULT_TYPES * sizeof(xIniSection));
            qsort(ini->Sections, ini->NumSections, sizeof(xIniSection), compare_ini_sections);

            xIniSection* isec = ini->Sections;
            xIniSection* end_isec = isec + ini->NumSections;
            xIniSection* osec = sections + NUM_DEFAULT_TYPES;

            for (; isec != end_isec; ++isec)
            {
                // Skip duplicate section names.
                if (isec > ini->Sections && stricmp(isec->sec, isec[-1].sec) == 0)
                {
                    continue;
                }

                U8 is_default = false;
                const default_type_loader* dtl = default_types;
                const default_type_loader* end_dtl = default_types + NUM_DEFAULT_TYPES;
                for (; dtl != end_dtl; ++dtl)
                {
                    if (stricmp(isec->sec, dtl->name) == 0)
                    {
                        is_default = true;
                        sections[dtl - default_types] = *isec;
                        break;
                    }
                }

                if (!is_default)
                {
                    *osec = *isec;
                    ++osec;
                    ++configs_size;
                }
            }
        }

        ini_configs = (ini_config*)xMemAlloc(gActiveHeap, configs_size * sizeof(ini_config), 0);

        {
            ini_config* cfg = ini_configs;
            ini_config* end_cfg = cfg + configs_size;
            for (; cfg != end_cfg; ++cfg)
            {
                reset_config(*cfg);
            }
        }

        for (S32 i = 0; i < NUM_DEFAULT_TYPES; ++i)
        {
            strcpy(ini_configs[i].name, default_types[i].name);
            default_types[i].load(ini_configs[i]);
        }

        if (ini != NULL)
        {
            ini_config* cfg = ini_configs;
            ini_config* end_cfg = cfg + configs_size;
            xIniSection* sec = sections;
            for (; cfg != end_cfg; ++cfg, ++sec)
            {
                if (sec->sec != NULL)
                {
                    strcpy(cfg->name, sec->sec);
                    load_config(*cfg, *sec, *ini);
                }
            }
        }

        if (mem != NULL)
        {
            RwFree(mem);
        }

        if (ini != NULL)
        {
            xIniDestroy(ini);
        }

        if (sections != NULL)
        {
            RwFree(sections);
        }
    }

    void update_streak_emitters(F32 dt)
    {
        streak_emitter* emitter = streak_emitters;
        streak_emitter* end_emitter = emitter + streak_emitters_used;

        while (emitter != end_emitter)
        {
            emitter->delay -= dt;
            if (emitter->delay >= 0.0f)
            {
                continue;
            }

            F32 time = dt;
            emitter->life -= dt;
            if (emitter->life < 0.0f)
            {
                time = dt - emitter->life;
            }

            runtime_config& cfg = runtime_configs[emitter->type];
            emitter->emitted += cfg.streak.rate * time;
            S32 emit = (S32)emitter->emitted;
            if (emit > 0)
            {
                emit_streaks(cfg, emitter->loc, emit);
            }
            emitter->emitted -= emit;

            if (emitter->life > 0.0f)
            {
                ++emitter;
            }
            else
            {
                --streak_emitters_used;
                memcpy(emitter, &streak_emitters[streak_emitters_used], sizeof(streak_emitter));
                --end_emitter;
            }
        }
    }

    void update_streaks(F32 dt)
    {
        streak_particle* streak = streaks;
        streak_particle* end_streak = streak + streaks_used;

        while (streak != end_streak)
        {
            streak->life -= dt;
            if (streak->life <= 0.0f)
            {
                --end_streak;
                kill_streak(*streak);
                continue;
            }

            streak->vel.y += streak->gravity * dt;
            streak->loc.x += streak->vel.x * dt;
            streak->loc.y += streak->vel.y * dt;
            streak->loc.z += streak->vel.z * dt;
            streak->alpha += streak->alpha_vel * dt;
            streak->length += streak->length_vel * dt;
            if (streak->length > streak->length_max)
            {
                streak->length = streak->length_max;
            }
            ++streak;
        }
    }

    void update_spark_emitters(F32 dt)
    {
        spark_emitter* emitter = spark_emitters;
        spark_emitter* end_emitter = emitter + spark_emitters_used;

        while (emitter != end_emitter)
        {
            // Same as the target: a delayed emitter is not advanced, so the loop keeps
            // subtracting dt until its delay runs out (it spins forever if dt is 0).
            emitter->delay -= dt;
            if (emitter->delay >= 0.0f)
            {
                continue;
            }

            F32 time = dt;
            emitter->life -= dt;
            if (emitter->life < 0.0f)
            {
                time = dt - emitter->life;
            }

            runtime_config& cfg = runtime_configs[emitter->type];
            emitter->emitted += cfg.spark.rate * time;
            S32 emit = (S32)emitter->emitted;
            if (emit > 0)
            {
                emit_sparks(cfg, emitter->loc, emit);
            }
            emitter->emitted -= emit;

            if (emitter->life > 0.0f)
            {
                ++emitter;
            }
            else
            {
                --spark_emitters_used;
                memcpy(emitter, &spark_emitters[spark_emitters_used], sizeof(spark_emitter));
                --end_emitter;
            }
        }
    }

    void update_sparks(F32 dt)
    {
        spark_particle* spark = sparks;
        spark_particle* end_spark = spark + sparks_used;

        while (spark != end_spark)
        {
            spark->life -= dt;
            if (spark->life <= 0.0f)
            {
                --end_spark;
                kill_spark(*spark);
                continue;
            }

            spark->vel.y += spark->gravity * dt;
            spark->loc.x += spark->vel.x * dt;
            spark->loc.y += spark->vel.y * dt;
            spark->loc.z += spark->vel.z * dt;
            spark->alpha += spark->alpha_vel * dt;
            ++spark;
        }
    }

    void load_runtime_configs()
    {
        runtime_configs = new (gActiveHeap) runtime_config[configs_size];

        ini_config* icfg = ini_configs;
        ini_config* end_icfg = icfg + configs_size;
        runtime_config* ocfg = runtime_configs;
        for (; icfg != end_icfg; ++icfg, ++ocfg)
        {
            ocfg->id = xStrHash(icfg->name);
            ocfg->vel_decay = icfg->vel_decay;
            ocfg->emit_radius = icfg->emit_radius;
            ocfg->motes_min = icfg->motes_min;
            ocfg->motes_max = icfg->motes_max;
            ocfg->life_min = icfg->life_min;
            ocfg->life_max = icfg->life_max;
            ocfg->scale_min = icfg->scale_min;
            ocfg->scale_max = icfg->scale_max;
            ocfg->scale_fire = icfg->scale_fire;
            ocfg->scale_smoke = icfg->scale_smoke;
            ocfg->vel_min = icfg->vel_min;
            ocfg->vel_max = icfg->vel_max;
            ocfg->sound =
                icfg->sound == 0 ? (iSndGroupHandle)-1 : xSndMgrGetSoundGroup(icfg->sound);

            ocfg->glare = icfg->glare;
            ocfg->warp.type = xScreenWarp::find_type(icfg->warp.type);
            ocfg->warp.radius = icfg->warp.radius;
            ocfg->warp.duration = icfg->warp.duration;
            ocfg->warp.intensity = icfg->warp.intensity;
            ocfg->warp.freq = icfg->warp.freq;
            ocfg->streak = icfg->streak;
            ocfg->spark = icfg->spark;
            ocfg->shake = icfg->shake;
            ocfg->presence = icfg->presence;
            ocfg->decal = icfg->decal;

            load_config_textures(*ocfg, *icfg);
            ocfg->anim.reset(5, icfg->anim, icfg->anim_size);
        }

        materials =
            (material_group*)xMemAlloc(gActiveHeap, configs_size * sizeof(material_group), 0);
        refresh_materials();

        streak_groups =
            (streak_group*)xMemAlloc(gActiveHeap, configs_size * sizeof(streak_group), 0);
        spark_groups = (spark_group*)xMemAlloc(gActiveHeap, configs_size * sizeof(spark_group), 0);
    }

    // No GC symbol: always inlined into zExplosion::emit.
    inline void paint_decals(const xVec3& loc, const runtime_config& cfg)
    {
        if (cfg.texture.decal == NULL)
        {
            return;
        }

        xDecal::emit_context ec;
        memset(&ec, 0, sizeof(ec));
        ec.owner = 'XPLD';
        ec.flags = decal_flags[cfg.decal.type - 1];
        ec.priority = 0;
        ec.life = cfg.decal.life;
        ec.max_dist = cfg.decal.max_dist;
        ec.size.x = ec.size.y = cfg.decal.width;
        ec.blend_mode = decal_blend_mode[cfg.decal.type - 1];
        ec.texture = cfg.texture.decal;
        ec.uv[0].x = ec.uv[0].y = 0.0f;
        ec.uv[1].x = ec.uv[1].y = 1.0f;
        ec.curve = &decal_curve[cfg.decal.type - 1];
        xDecal::sphere_emit(ec, loc, cfg.decal.radius, 0x3e);
    }
} // namespace

void zExplosion::startup()
{
    load_ini_configs();
}

void zExplosion::scene_enter()
{
    motes = (mote*)xMemAlloc(gActiveHeap, MAX_MOTES * sizeof(mote), 0);
    streaks = (streak_particle*)xMemAlloc(gActiveHeap, MAX_STREAKS * sizeof(streak_particle), 0);
    streak_emitters =
        (streak_emitter*)xMemAlloc(gActiveHeap, MAX_STREAK_EMITTERS * sizeof(streak_emitter), 0);
    sparks = (spark_particle*)xMemAlloc(gActiveHeap, MAX_SPARKS * sizeof(spark_particle), 0);
    spark_emitters =
        (spark_emitter*)xMemAlloc(gActiveHeap, MAX_SPARK_EMITTERS * sizeof(spark_emitter), 0);
    presi_size = 0;

    decal_curve[0].reset(6, decal_anim_impact, 3);
    decal_curve[1].reset(6, decal_anim_burn, 7);
    decal_curve[2].reset(6, decal_anim_plasma, 3);

    load_runtime_configs();

    reset_all();
}

void zExplosion::scene_exit()
{
}

void zExplosion::update(F32 dt)
{
    update_configs(dt);
    update_presence(dt);
    update_motes(dt);
    update_streak_emitters(dt);
    update_streaks(dt);
    update_spark_emitters(dt);
    update_sparks(dt);
}

void zExplosion::pre_render()
{
    ptank_render();
}

void zExplosion::render()
{
}

S32 zExplosion::find_type(U32 id)
{
    S32 i;

    for (i = 0; i < configs_size; i++)
    {
        if (id == runtime_configs[i].id)
        {
            return i;
        }
    }

    return 0;
}

void zExplosion::emit(S32 type, const xVec3& loc)
{
    const runtime_config& cfg = runtime_configs[type];
    U32 motes_available = MAX_MOTES - motes_used;

    if (cfg.motes_min <= motes_available)
    {
        create_presence(loc, cfg.presence.delay, cfg.presence.life, cfg.presence.knockback,
                        cfg.presence.radius_begin, cfg.presence.radius_end,
                        cfg.presence.damage_player, cfg.presence.damage_npc,
                        cfg.presence.damage_other, false, NULL);

        U32 motes_required;
        if (cfg.motes_min >= cfg.motes_max)
        {
            motes_required = cfg.motes_min;
        }
        else
        {
            motes_required = cfg.motes_min + xrand_RandomInt32() % (cfg.motes_max - cfg.motes_min);
        }

        if (motes_required > motes_available)
        {
            motes_required = motes_available;
        }

        launch_sphere(type, motes_required, loc);
        spawn_streak_emitter(type, loc);
        spawn_spark_emitter(type, loc);

        if (cfg.warp.type != xScreenWarp::TYPE_INVALID)
        {
            xScreenWarp::emit(loc, cfg.warp.radius, cfg.warp.duration,
                              (xScreenWarp::type_enum)cfg.warp.type, cfg.warp.intensity,
                              cfg.warp.freq, 0);
        }

        if (cfg.shake.time > 0.0f && cfg.shake.mag > 0.0f)
        {
            xCameraFXShake(cfg.shake.time, cfg.shake.mag, cfg.shake.cycle, cfg.shake.rot_mag,
                           cfg.shake.radius, &loc, NULL, false);
        }

        if (cfg.sound != (iSndGroupHandle)-1)
        {
            xSndMgrPlay(cfg.sound, 0, &loc, NULL, NULL, NULL, NULL);
        }

        if (cfg.decal.type != 0)
        {
            paint_decals(loc, cfg);
        }
    }
}

void zExplosion::trail_emitter::start(S32 type, const xVec3& loc)
{
    this->type = type;
    this->loc = loc;
    emitted = 0.0f;
    glare_id = -1;

    const runtime_config& cfg = runtime_configs[type];
    streak_delay = cfg.streak.emit_delay;
    streaks_emitted = 0.0f;
    spark_delay = cfg.spark.emit_delay;
    sparks_emitted = 0.0f;

    // The target really passes radius_begin as both radius_begin and radius_end here.
    presence = create_presence(loc, cfg.presence.delay, cfg.presence.life, cfg.presence.knockback,
                               cfg.presence.radius_begin, cfg.presence.radius_begin,
                               cfg.presence.damage_player, cfg.presence.damage_npc,
                               cfg.presence.damage_other, true, this);

    if (cfg.sound != (iSndGroupHandle)-1)
    {
        tracking_sound = xSndMgrPlay(cfg.sound, 2, &loc, NULL, NULL, NULL, NULL);
    }
    else
    {
        tracking_sound = (iSndHandle)-1;
    }
}

void zExplosion::trail_emitter::stop()
{
    if (presence != NULL)
    {
        destroy_presence((presence_node*)presence);
    }

    if (glare_id >= 0)
    {
        xScrFXGlareDisable(glare_id);
    }

    if (tracking_sound != (iSndHandle)-1)
    {
        xSndMgrStop(tracking_sound);
    }

    type = -1;
}

void zExplosion::trail_emitter::update(const xVec3& loc, F32 dt, F32 intensity)
{
    const runtime_config& cfg = runtime_configs[type];

    if (presence != NULL)
    {
        move_presence((presence_node*)presence, loc, cfg.presence.radius_begin);
    }

    S32 emit_rand;
    if (cfg.motes_min >= cfg.motes_max)
    {
        emit_rand = cfg.motes_min;
    }
    else
    {
        emit_rand = cfg.motes_min + xrand_RandomInt32() % (cfg.motes_max - cfg.motes_min);
    }

    emitted += dt * emit_rand;

    S32 emit_total = (S32)emitted;
    S32 motes_available = MAX_MOTES - motes_used;
    if (emit_total > motes_available)
    {
        emitted -= emit_total - motes_available;
        emit_total = motes_available;
    }

    if (emit_total > 0)
    {
        emitted -= emit_total;

        const xVec3& loc0 = this->loc;
        xVec3 offset = { 0.0f, 0.0f, 0.0f };
        offset.x = loc.x - loc0.x;
        offset.y = loc.y - loc0.y;
        offset.z = loc.z - loc0.z;
        launch_line(type, emit_total, loc0, loc, offset, dt);
    }

    streak_delay -= dt;
    if (streak_delay < 0.0f)
    {
        streaks_emitted += cfg.streak.rate * dt;
        S32 streak_amount = (S32)streaks_emitted;
        if (streak_amount > 0)
        {
            emit_streaks(cfg, loc, streak_amount);
            streaks_emitted -= streak_amount;
        }
    }

    spark_delay -= dt;
    if (spark_delay < 0.0f)
    {
        sparks_emitted += cfg.spark.rate * dt;
        S32 spark_amount = (S32)sparks_emitted;
        if (spark_amount > 0)
        {
            emit_sparks(cfg, loc, spark_amount);
            sparks_emitted -= spark_amount;
        }
    }

    if (cfg.warp.type != xScreenWarp::TYPE_INVALID)
    {
        xScreenWarp::emit(loc, cfg.warp.radius, cfg.warp.duration,
                          (xScreenWarp::type_enum)cfg.warp.type, cfg.warp.intensity, cfg.warp.freq,
                          0);
    }

    if (glare_id >= 0)
    {
        xScrFXGlareChange(glare_id, &loc, 1e38f, intensity);
    }

    this->loc = loc;
}
