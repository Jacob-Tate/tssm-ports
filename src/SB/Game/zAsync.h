#ifndef ZASYNC_H
#define ZASYNC_H

#include <types.h>
#include <rwcore.h>
#include <rpworld.h>

namespace zAsync
{
    // Slot order from the retail vtables.
    struct Screen
    {
        virtual void Init() = 0;
        virtual void Update(F32 dt) = 0;
        virtual void Render() = 0;
        virtual void Exit() = 0;
    };

    extern RwCamera* cam;
    extern RpLight* light;
    extern RwFrame* frame;
    extern RpWorld* world;
    extern Screen* screen;

    void* RenderThread(void*);
    void Free();
    void Allocate();
    void Init(Screen* scr);
} // namespace zAsync

#endif
