#include "zLightWeight.h"

namespace z
{
    S32 lightweight_manager::system_count;
    lightweight_system_base* lightweight_manager::system[10];

    void lightweight_manager::setup()
    {
        system_count = 0;
    }

    void lightweight_manager::exit()
    {
        for (S32 i = 0; i < system_count; i++)
        {
            system[i]->exit();
        }
    }

    void lightweight_manager::reset()
    {
        for (S32 i = 0; i < system_count; i++)
        {
            system[i]->reset();
        }
    }

    void lightweight_manager::update(F32 dt)
    {
        for (S32 i = 0; i < system_count; i++)
        {
            system[i]->update(dt);
        }
    }

    void lightweight_manager::render()
    {
        for (S32 i = 0; i < system_count; i++)
        {
            system[i]->render();
        }
    }

    void lightweight_manager::post_render_2d()
    {
        for (S32 i = 0; i < system_count; i++)
        {
            system[i]->post_render_2d();
        }
    }

    void lightweight_manager::post_render_3d()
    {
        for (S32 i = 0; i < system_count; i++)
        {
            system[i]->post_render_3d();
        }
    }

    void lightweight_manager::scene_setup()
    {
        for (S32 i = 0; i < system_count; i++)
        {
            system[i]->scene_setup();
        }
    }
} // namespace z
