#include "xClimate.h"

#include <types.h>
#include <string.h>
#include "xMath.h"
#include "xMath3.h"
#include "xMemMgr.h"
#include "xVec3.h"
#include "zGlobals.h"
#include "zParEmitter.h"
#include "zParPTank.h"

static _tagClimate* sClimate;

float snow_life = 3.0f;
const xVec3 snow_vel = { 0.0f, -2.0f, 0.0f };
const xVec3 snow_dvel = { 0.1f, 0.1f, 0.1f };

void xClimateInit(_tagClimate* climate)
{
    climate->rain.strength = 0.0f;

    climate->rain.rain_emitter = zParEmitterFind(0xDDA6D107); // xStrHash("PAREMIT_RAIN")
    climate->rain.rain_emitter->emit_flags &= 0xfe;

    climate->rain.snow_emitter = zParEmitterFind(0xDDCC8932); // xStrHash("PAREMIT_SNOW")
    climate->rain.snow_emitter->emit_flags &= 0xfe;
}

static inline void xClimateVecFromAngle(F32 angleDegrees, xVec3* vec)
{
    xMat3x3 Mat;

    xMat3x3Identity(&Mat);
    xMat3x3Euler(&Mat, angleDegrees * (PI / ONEEIGHTY), 0.0f, 0.0f);
    xVec3Init(vec, 0.0f, 0.0f, 1.0f);
    xMat3x3LMulVec(vec, &Mat, vec);
}

void xClimateInitAsset(_tagClimate* climate, xEnvAsset* easset)
{
    _tagWind* w = &climate->wind;

    sClimate = climate;
    w->strength = 0.0f;
    xClimateVecFromAngle(w->angle, &w->dir);

    if (easset->climateFlags == 0)
    {
        w->strength = 0.0f;
        climate->rain.strength = 0.0f;
    }
    else if (easset->climateFlags & 1)
    {
        climate->rain.rain = 1;
        climate->rain.strength = 0.5f * (easset->climateStrengthMax - easset->climateStrengthMin);
        climate->rain.strength += easset->climateStrengthMin;
    }
    else if (easset->climateFlags & 2)
    {
        climate->rain.rain = 0;
        climate->rain.strength = 0.5f * (easset->climateStrengthMax - easset->climateStrengthMin);
        climate->rain.strength += easset->climateStrengthMin;
    }
}

void xClimateSetSnow(F32 stre)
{
    sClimate->rain.rain = 0;
    sClimate->rain.strength = stre;
}

void xClimateSetRain(F32 stre)
{
    sClimate->rain.rain = 1;
    sClimate->rain.strength = stre;
}

static inline void GetPosBigDogWhattupFool(xVec3* vec)
{
    xCamGroup* cam = xglobals->cam;
    vec->x = 10.0f * cam->mat.at.x + cam->mat.pos.x;
    vec->y = 10.0f * cam->mat.at.y + cam->mat.pos.y;
    vec->z = 10.0f * cam->mat.at.z + cam->mat.pos.z;
}

static void UpdateRain(_tagClimate* climate, F32 seconds)
{
    _tagRain* r = &climate->rain;
    xParEmitterCustomSettings info;

    memset(&info, 0, sizeof(xParEmitterCustomSettings));
    info.custom_flags = 0x100;

    if (r->rain)
    {
        S32 total_rain_drops = 25.0f * r->strength;
        for (S32 i = 0; i < total_rain_drops; i++)
        {
            GetPosBigDogWhattupFool(&info.pos);
            info.pos.x += 25.0f * xurand() - 12.5f;
            info.pos.y += 8.0f;
            info.pos.z += 25.0f * xurand() - 12.5f;
            xParEmitterEmitCustom(r->rain_emitter, seconds, &info);
        }
    }
    else
    {
        S32 total_snow_flakes = 25.0f * r->strength;
        xVec3 player_pos;

        info.custom_flags |= 0x202;
        GetPosBigDogWhattupFool(&player_pos);

        if (gPTankDisable)
        {
            for (S32 i = 0; i < total_snow_flakes; i++)
            {
                info.pos = player_pos;
                info.pos.x += 45.0f * xurand() - 22.5f;
                info.pos.z += 45.0f * xurand() - 22.5f;

                F32 perc = (info.pos.x - player_pos.x) * (info.pos.z - player_pos.z) / 506.25f;
                perc = 1.0f - perc;

                info.vel.x = snow_dvel.x * xurand() + snow_vel.x;
                info.vel.y = snow_dvel.y * xurand() + snow_vel.y;
                info.vel.z = snow_dvel.z * xurand() + snow_vel.z;
                info.pos.y += 4.0f * perc + 4.0f;
                info.life.val[0] = snow_life * perc + snow_life;

                xParEmitterEmitCustom(r->snow_emitter, seconds, &info);
            }
        }
        else
        {
            S32 num = total_snow_flakes * 0.1f;
            if (num > 0)
            {
                xVec3* pos = (xVec3*)xMemPushTemp(num * 2 * sizeof(xVec3));
                xVec3* vel = pos + num;
                if (pos)
                {
                    xVec3* pp = pos;
                    xVec3* vp = vel;
                    for (S32 i = 0; i < num; i++, pp++, vp++)
                    {
                        *pp = player_pos;
                        pp->x += 45.0f * xurand() - 22.5f;
                        pp->z += 45.0f * xurand() - 22.5f;

                        F32 xx = pp->x - player_pos.x;
                        F32 zz = pp->z - player_pos.z;
                        F32 perc = (xx * xx + zz * zz) / 506.25f;
                        perc = 1.0f - perc;
                        pp->y += 4.0f * perc + 4.0f;

                        vp->x = snow_dvel.x * xurand() + snow_vel.x;
                        vp->y = snow_dvel.y * xurand() + snow_vel.y;
                        vp->z = snow_dvel.z * xurand() + snow_vel.z;
                    }

                    zParPTankSpawnSnow(pos, vel, num);
                    xMemPopTemp(pos);
                }
            }
        }
    }
}

static inline void UpdateWind(_tagClimate* climate, F32 seconds)
{
}

void xClimateUpdate(_tagClimate* climate, F32 seconds)
{
    UpdateRain(climate, seconds);
    UpdateWind(climate, seconds);
}
