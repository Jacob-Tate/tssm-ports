#include "zCamCar.h"

#include "zCar.h"

zCamCar::zCamCar()
{
}

zCamCar::~zCamCar()
{
}

void zCamCar::create()
{
    zCamDrive::create();
    input.rest_distance = 7.0f;
    input.rest_height = 4.5f;
    target_offset = 3.5f;
    input.angle_change_max = 0.0f;
    input.up_distance = input.down_distance = input.rest_distance;
    input.up_height = input.down_height = input.rest_height;
    angle_speed = 3.0f;
    calculatedAngleSpeed = angle_speed;
    input.min_responsiveness = 0.2f;
    velocity_speed = 20.0f;
}

void zCamCar::destroy()
{
}

void zCamCar::start()
{
    zCamDrive::start();
}

void zCamCar::update(xScene& scene, F32 dt)
{
    if (dt < 1e-5f)
    {
        return;
    }

    zCar* theCar = zEntPlayerCarGetCar();

    // The boost and normal speeds are both 20 in retail; the target still branches on
    // boostTimeRemaining and loads the same constant on each arm.
    velocity_speed = theCar->boostTimeRemaining > 0.0f ? 20.0f : 20.0f;

    F32 boostTimePassed = BOOST_DURATION - theCar->boostTimeRemaining;
    if (theCar->boostTimeRemaining > 0.0f && boostTimePassed >= BOOST_DURATION * 0.2f)
    {
        boostCamFOV = 1.7453293f;
    }
    else
    {
        boostCamFOV = theCar->boostTimeRemaining > 0.0f ? 1.8325957f : 1.3962634f;
    }

    if (theCar->animReversing)
    {
        hardTurnCamTargetOffsetX = -2.0f;
    }
    else
    {
        hardTurnCamTargetOffsetX = 2.0f;
    }

    zCamDrive::update(scene, dt);
}
