#ifndef ZEXPLOSION_H
#define ZEXPLOSION_H

#include <types.h>

#include "xVec3.h"
#include "iSnd.h"

struct presence_volume
{
    F32 knockback;
    xVec3 center;
    F32 radius;
    F32 damage_player;
    F32 damage_npc;
    F32 damage_other;
};

namespace zExplosion
{
    struct trail_emitter
    {
        S32 type;
        xVec3 loc;
        F32 emitted;
        S32 glare_id;
        F32 streak_delay;
        F32 streaks_emitted;
        F32 spark_delay;
        F32 sparks_emitted;
        presence_volume* presence;
        iSndHandle tracking_sound;

        void update(const xVec3& loc, F32 dt, F32 intensity);
        void stop();
        void start(S32 type, const xVec3& loc);
    };

    void emit(S32 type, const xVec3& loc);
    S32 find_type(U32 id);
    void render();
    void pre_render();
    void update(F32 dt);
    void scene_exit();
    void scene_enter();
    void startup();
} // namespace zExplosion

#endif
