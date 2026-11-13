#include "xPad.h"
#include "zGame.h"
#include "zGlobals.h"

#include "zMenu.h"
#include "zScene.h"

#include <string.h>

// No symbol in TSSM (GC or PS2): always inlined into xPadUpdate.
inline void xPadAnalogIsDigital(S32 idx, S32 enable)
{
    if (idx != 0)
    {
        return;
    }

    _tagxPad* pad = &mPad[idx];

    if (enable)
    {
        pad->flags |= 0x10;
    }
    else
    {
        pad->flags &= ~0x10;
    }

    if (pad->al2d_timer >= 0.35f)
    {
        pad->al2d_timer = 0.35f;
    }

    if (pad->ar2d_timer >= 0.35f)
    {
        pad->ar2d_timer = 0.35f;
    }

    if (pad->d_timer >= 0.35f)
    {
        pad->d_timer = 0.35f;
    }
}

S32 xPadInit()
{
    memset(mPad, 0, sizeof(mPad));
    //memset(mRumbleList, 0, sizeof(mRumbleList));
    if (!iPadInit())
    {
        return 0;
    }
    //gPlayerPad = mPad;
    return 1;
}

_tagxPad* xPadEnable(S32 idx)
{
    _tagxPad* p = &mPad[idx];
    if (p->state != ePad_Disabled)
    {
        return p;
    }
    if (idx != 0)
    {
        return p;
    }
    p = iPadEnable(p, idx);
    xPadRumbleEnable(idx, 1);
    return p;
}

void xPadRumbleEnable(S32 idx, S32 enable)
{
    _tagxPad* p = &mPad[idx];
    if (p->state != ePad_Enabled)
    {
        enable = 0;
    }
    if (enable)
    {
        if (p->flags & ePadInit_EnableRumble4)
        {
            p->flags |= ePadInit_Complete8a;
        }
    }
    else
    {
        if (p->flags & ePadInit_Complete8a)
        {
            p->flags ^= ePadInit_Complete8a;
            //xPadDestroyRumbleChain(mPad + idx);
        }
    }
}

S32 xPadUpdate(S32 idx, F32 time_passed)
{
    U32 new_on = 0;
    _tagxPad* p;
    S32 ret;
    U32 fake_dpad;
    S32 i;

    if (idx != 0)
    {
        return 0;
    }

    if (zScene_ScreenAdjustMode())
    {
        xPadAnalogIsDigital(idx, 0);
    }
    else if (zMenuRunning() || zGameIsPaused())
    {
        xPadAnalogIsDigital(idx, 1);
    }
    else
    {
        xPadAnalogIsDigital(idx, 0);
    }

    p = &mPad[idx];
    ret = iPadUpdate(p, &new_on);
    if (!ret)
    {
        p->pressed = 0;
        p->released = 0;
        return 1;
    }

    if (p->flags & 0x10)
    {
        if (p->flags & 0x1)
        {
            fake_dpad = 0;
            if (p->analog1.x >= 50)
            {
                fake_dpad |= XPAD_BUTTON_RIGHT;
            }
            else if (p->analog1.x <= -50)
            {
                fake_dpad |= XPAD_BUTTON_LEFT;
            }
            if (p->analog1.y >= 50)
            {
                fake_dpad |= XPAD_BUTTON_DOWN;
            }
            else if (p->analog1.y <= -50)
            {
                fake_dpad |= XPAD_BUTTON_UP;
            }

            if (fake_dpad == 0)
            {
                p->al2d_timer = 0.0f;
            }
            else
            {
                p->al2d_timer -= time_passed;
                if (p->al2d_timer <= 0.0f)
                {
                    new_on |= fake_dpad;
                    p->al2d_timer = 0.35f;
                }
            }
        }

        if (p->flags & 0x2)
        {
            if (p->analog2.x > -50 && p->analog2.x < 50 && p->analog2.y > -50 && p->analog2.y < 50)
            {
                p->ar2d_timer = 0.0f;
            }
            else
            {
                p->ar2d_timer -= time_passed;
                if (p->ar2d_timer <= 0.0f)
                {
                    p->ar2d_timer = 0.35f;
                    if (p->analog2.x >= 50)
                    {
                        new_on |= XPAD_BUTTON_RIGHT;
                    }
                    else if (p->analog2.x <= -50)
                    {
                        new_on |= XPAD_BUTTON_LEFT;
                    }
                    if (p->analog2.y >= 50)
                    {
                        new_on |= XPAD_BUTTON_DOWN;
                    }
                    else if (p->analog2.y <= -50)
                    {
                        new_on |= XPAD_BUTTON_UP;
                    }
                }
            }
        }
    }

    p->pressed = new_on & ~p->on;
    p->released = p->on & ~new_on;
    p->on = new_on;

    for (i = 0; i < 22; i++)
    {
        if (p->pressed & (1 << i))
        {
            p->down_tmr[i] = 0.0f;
        }
        else if (p->released & (1 << i))
        {
            p->up_tmr[i] = 0.0f;
        }

        if (p->on & (1 << i))
        {
            p->down_tmr[i] += time_passed;
        }
        else
        {
            p->up_tmr[i] += time_passed;
        }
    }

    if (p->flags & 0x10)
    {
        if (!(p->on & XPAD_BUTTON_UP) && !(p->on & XPAD_BUTTON_DOWN) &&
            !(p->on & XPAD_BUTTON_LEFT) && !(p->on & XPAD_BUTTON_RIGHT))
        {
            p->d_timer = 0.0f;
        }
        else
        {
            p->d_timer -= time_passed;
            if (p->d_timer <= 0.0f)
            {
                p->d_timer = 0.35f;
                if (p->on & XPAD_BUTTON_UP)
                {
                    p->pressed |= XPAD_BUTTON_UP;
                }
                else if (p->on & XPAD_BUTTON_DOWN)
                {
                    p->pressed |= XPAD_BUTTON_DOWN;
                }
                if (p->on & XPAD_BUTTON_LEFT)
                {
                    p->pressed |= XPAD_BUTTON_LEFT;
                }
                else if (p->on & XPAD_BUTTON_RIGHT)
                {
                    p->pressed |= XPAD_BUTTON_RIGHT;
                }
            }
        }
    }

    return 1;
}

void xPadNormalizeAnalog(_tagxPad& pad, S32 inner_zone, S32 outer_zone)
{
    const _tagPadAnalog* src = &pad.analog1;
    for (S32 i = 0; i < 2; i++)
    {
        pad.analog[i].offset.x =
            normalize_analog(src[i].x, -outer_zone, outer_zone, 0, -inner_zone, inner_zone);
        pad.analog[i].offset.y =
            normalize_analog(src[i].y, -outer_zone, outer_zone, 0, -inner_zone, inner_zone);
        if (pad.analog[i].offset.x == 0.0f && pad.analog[i].offset.y == 0.0f)
        {
            pad.analog[i].mag = 0.0f;
            pad.analog[i].dir.assign(1.0f, 0.0f);
            pad.analog[i].ang = 0.0f;
        }
        else
        {
            if (pad.analog[i].offset.x > 1.0f)
            {
                pad.analog[i].offset.x = 1.0f;
            }
            else if (pad.analog[i].offset.x < -1.0f)
            {
                pad.analog[i].offset.x = -1.0f;
            }
            if (pad.analog[i].offset.y > 1.0f)
            {
                pad.analog[i].offset.y = 1.0f;
            }
            else if (pad.analog[i].offset.y < -1.0f)
            {
                pad.analog[i].offset.y = -1.0f;
            }
            pad.analog[i].mag = pad.analog[i].offset.length();
            pad.analog[i].dir = pad.analog[i].offset;
            pad.analog[i].dir /= pad.analog[i].mag;
            pad.analog[i].ang = xatan2(pad.analog[i].dir.y, pad.analog[i].dir.x);
        }
    }
}

void xPadKill()
{
    iPadKill();
}
