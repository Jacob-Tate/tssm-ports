#ifndef ZHUD_H
#define ZHUD_H

#include <types.h>
#include <rwcore.h>
#include "xBase.h"
#include "xHud.h"
#include "xLightKit.h"

namespace zhud
{
    void render();
    void update(F32 dt);
    void show();
    void hide();
    void init();
    void destroy();
    void setup();
} // namespace zhud

namespace zHud
{
    extern RwCamera* HudCamera;
    extern xLightKit* HudLightKit;

    struct hud_element : xBase
    {
        hud_element* _next;

        virtual void init();
        virtual void setup()
        {
        }
        virtual void update(F32 dt)
        {
        }
        virtual void destroy()
        {
        }
        virtual void show(bool autohide) = 0;
        virtual void hide() = 0;
        virtual void handle_event(xBase* from, U32 toEvent, const F32* toParam,
                                  xBase* toParamWidget, U32 toParamWidgetID)
        {
        }
    };

    struct hud_system
    {
        hud_element* elements;
        U8 last_paused;

        hud_system()
        {
        }

        static hud_system& instance();
        void destroy();
        void add(hud_element* element);
        void hide();
        void show(bool autohide);
        static void render();
        void update(F32 dt);
        void init();
        static void shutdown();
        static void startup();
    };
} // namespace zHud

#endif
