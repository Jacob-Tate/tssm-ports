#ifndef ZCAMCAR_H
#define ZCAMCAR_H

#include "zCamDrive.h"

// Member functions in original source order; create is the key function that emits the vtable.
struct zCamCar : zCamDrive
{
    zCamCar();
    ~zCamCar();
    void create();
    void destroy();
    void start();
    void update(xScene& scene, F32 dt);
};

#endif
