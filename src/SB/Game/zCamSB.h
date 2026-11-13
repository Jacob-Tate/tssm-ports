#ifndef ZCAMSB_H
#define ZCAMSB_H

#include "xCamera.h"
#include "xCam.h"

// Member functions in original source order; create is the key function that emits the vtable.
struct zCamSB : xCam
{
    void create();
    void destroy();
    void start();
    void update(xScene& scene, F32 dt);
};

#endif
