#include "xModelBlur.h"

#include "xMathInlines.h"
#include "xMemMgr.h"

#include <types.h>

extern const xColor_tag g_CLEAR;

#define MAX_ACTIVITIES 6
#define MAX_PATH 32
#define MAX_CARDS 32

namespace
{
    struct path_node
    {
        U8 just_added;
        F32 age;
        xVec3 center;
    };

    struct card_data
    {
        F32 alpha;
        xVec3 center;
    };

    struct interpolator
    {
        F32 value;
        F32 start;
        F32 end;
        F32 t;
        F32 trate;

        void set(F32 v)
        {
            value = start = end = v;
            t = trate = 0.0f;
        }

        void set(F32 v, F32 time)
        {
            if (time <= 0.0f || v == value)
            {
                set(v);
                return;
            }

            start = value;
            end = v;
            t = 0.0f;
            trate = 1.0f / time;
        }

        void update(F32 dt)
        {
            if (trate == 0.0f)
            {
                return;
            }

            t += trate * dt;

            if (t >= 1.0f)
            {
                value = start = end;
                t = 0.0f;
                trate = 0.0f;
            }
            else
            {
                value = (1.0f - t) * start + t * end;
            }
        }
    };

    U8 sModelBlurEnabled = true;
    xFXCameraTexture camtex;
} // namespace

struct xModelBlur::activity_data
{
    S32 index;
    xModelBlur* owner;
    const config_data* cfg;
    xModelInstance* model;
    xVec3 center;
    path_node path[MAX_PATH];
    card_data cards[MAX_CARDS];
    S32 path_start;
    S32 path_size;
    S32 cards_size;
    interpolator lifetime;
    interpolator alpha;
};

xModelBlur::activity_data* xModelBlur::activity_buffer;
xModelBlur::activity_data* xModelBlur::activities[MAX_ACTIVITIES];
S32 xModelBlur::activities_used;

namespace
{
    // Swaps a with the last active entry, shrinking the active list by one. Retail compares
    // the slot address with the address of the by-value parameter, so the test is always
    // true (harmless when a is already last).
    inline void free_activity(xModelBlur::activity_data* a)
    {
        xModelBlur::activity_data*& last = xModelBlur::activities[--xModelBlur::activities_used];
        if (&last != &a)
        {
            xModelBlur::activity_data* other = last;
            xModelBlur::activities[a->index] = other;
            last = a;
            other->index = a->index;
            a->index = xModelBlur::activities_used;
        }
    }
} // namespace

void xModelBlur::scene_enter()
{
    activity_data* activity;

    camtex.create(8, 8, true, 32, 32);
    camtex.set_background(g_CLEAR);

    activity_buffer = (activity_data*)xMemAllocSize(MAX_ACTIVITIES * sizeof(activity_data));

    for (S32 i = 0; i < MAX_ACTIVITIES; ++i)
    {
        activity = &activity_buffer[i];
        activities[i] = activity;
        activity->index = i;
        activity->owner = NULL;
    }

    activities_used = 0;
}

void xModelBlur::scene_exit()
{
    camtex.destroy();
}

void xModelBlur::reset_all()
{
    S32 i;
    activity_data* activity;

    for (i = 0; i < activities_used; ++i)
    {
        activity = activities[i];
        if (activity->owner != NULL)
        {
            activity->owner->activity = NULL;
        }
    }

    for (i = 0; i < MAX_ACTIVITIES; ++i)
    {
        activity = &activity_buffer[i];
        activities[i] = activity;
        activity->index = i;
        activity->owner = NULL;
    }

    activities_used = 0;
}

void xModelBlur::update_all(F32 dt)
{
    activity_data** activity = activities;
    activity_data** end_activity = activity + activities_used;

    while (activity != end_activity)
    {
        if ((*activity)->cfg->flags & 0x8)
        {
            ++activity;
        }
        else if ((*activity)->owner->update(dt))
        {
            ++activity;
        }
        else
        {
            --end_activity;
        }
    }
}

void xModelBlur::render_all()
{
    if (!sModelBlurEnabled)
    {
        return;
    }

    activity_data** activity = activities;
    activity_data** end_activity = activity + activities_used;
    for (; activity != end_activity; ++activity)
    {
        // The per-activity card rendering (PS2 render()/render_cards()) is not
        // part of the GameCube build; only the empty loop remains.
    }
}

void xModelBlur::render_enable(bool enabled)
{
    sModelBlurEnabled = enabled;
}

void xModelBlur::create()
{
    activity = NULL;
}

U8 xModelBlur::activate(F32 lifetime, F32 alpha, const config_data& cfg, xModelInstance* model)
{
    if (activity != NULL)
    {
        activity->lifetime.set(lifetime);
        activity->alpha.set(alpha);
        return true;
    }

    if (activities_used >= MAX_ACTIVITIES)
    {
        return false;
    }

    activity = activities[activities_used++];
    activity->owner = this;
    activity->cfg = &cfg;
    activity->model = model;
    activity->path_start = 0;
    activity->path_size = 0;
    activity->lifetime.set(lifetime);
    activity->alpha.set(alpha);

    return true;
}

void xModelBlur::deactivate()
{
    if (activity == NULL)
    {
        return;
    }

    activity->owner = NULL;

    free_activity(activity);

    activity = NULL;
}

void xModelBlur::move(const xVec3& center)
{
    if (activity->path_size > 1)
    {
        S32 second_index = (activity->path_start + 1) & (MAX_PATH - 1);
        xVec3& A = activity->path[activity->path_start].center;
        xVec3& B = activity->path[second_index].center;
        F32 ABx = B.x - A.x;
        F32 ABy = B.y - A.y;
        F32 ABz = B.z - A.z;
        F32 min_dist = activity->cfg->card_dist;

        if (ABx * ABx + ABy * ABy + ABz * ABz < min_dist * min_dist)
        {
            --activity->path_size;
            activity->path_start = second_index;
        }
    }

    if (activity->path_size >= MAX_PATH)
    {
        activity->path_size = MAX_PATH - 1;
    }

    activity->path_start = (activity->path_start - 1) & (MAX_PATH - 1);
    ++activity->path_size;

    path_node& p = activity->path[activity->path_start];
    p.just_added = true;
    p.age = 0.0f;
    p.center = center;

    activity->center = center;
}

void xModelBlur::set_lifetime(F32 value, F32 time)
{
    activity->lifetime.set(value, time);
}

U8 xModelBlur::update(F32 dt)
{
    S32 i;

    activity->lifetime.update(dt);
    activity->alpha.update(dt);

    F32 lifetime = activity->lifetime.value;
    if (lifetime <= 0.0f && activity->lifetime.end <= 0.0f && !(activity->cfg->flags & 0x2))
    {
        deactivate();
        return false;
    }

    for (i = 0; i < activity->path_size; ++i)
    {
        path_node& p = activity->path[(i + activity->path_start) & (MAX_PATH - 1)];
        if (p.just_added)
        {
            p.just_added = false;
            continue;
        }

        p.age += dt;
        if (p.age >= lifetime)
        {
            if (i == 0)
            {
                activity->path_size = 1;
                break;
            }

            if (activity->path[(i - 1 + activity->path_start) & (MAX_PATH - 1)].age >= lifetime)
            {
                activity->path_size = i;
                break;
            }
        }
    }

    F32 card_dist = activity->cfg->card_dist;
    S32 max_card_renders = activity->cfg->max_card_renders;
    F32 alpha_scale = activity->cfg->color_alpha * activity->alpha.value / lifetime;
    card_data* cards = activity->cards;
    S32 cards_size = 0;
    F32 path_dist = 0.0f;

    for (i = 1; i < activity->path_size; ++i)
    {
        if (cards_size >= max_card_renders)
        {
            break;
        }

        path_node& p0 = activity->path[(i - 1 + activity->path_start) & (MAX_PATH - 1)];
        path_node& p1 = activity->path[(i + activity->path_start) & (MAX_PATH - 1)];
        xVec3& A = p0.center;
        xVec3& B = p1.center;
        F32 ABx = B.x - A.x;
        F32 ABy = B.y - A.y;
        F32 ABz = B.z - A.z;
        F32 idist = xsqrt(ABx * ABx + ABy * ABy + ABz * ABz);

        if (idist < 0.001f)
        {
            path_dist += idist;
            continue;
        }

        F32 age0 = p0.age;
        F32 dage = p1.age - p0.age;
        F32 dist = -path_dist;
        path_dist += idist;
        idist = 1.0f / idist;

        while (cards_size < max_card_renders)
        {
            if (path_dist < card_dist)
            {
                break;
            }

            dist += card_dist;
            path_dist -= card_dist;

            F32 t = dist * idist;
            F32 age = age0 + dage * t;
            if (age >= lifetime)
            {
                break;
            }

            cards[cards_size].alpha = alpha_scale * (lifetime - age);
            cards[cards_size].center.assign(A.x + ABx * t, A.y + ABy * t, A.z + ABz * t);
            ++cards_size;
        }
    }

    activity->cards_size = cards_size;
    if (activity->path_size > i + 1)
    {
        activity->path_size = i + 1;
    }

    return true;
}
