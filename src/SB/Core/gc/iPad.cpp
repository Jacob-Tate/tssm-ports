#include "iPad.h"

F32 g_rumblingThreshold = 0.5f;
static PADStatus sPadData[PAD_MAX_CONTROLLERS] = { 0 };
static U32 sPadMotor[PAD_MAX_CONTROLLERS];
static S32 sPadCurrLevel[PAD_MAX_CONTROLLERS];
static S32 sPadDeltaLevel[PAD_MAX_CONTROLLERS];

static inline U32 iPadConvFromGCN(U32 buttons, U32 gcn_flag, U32 x_flag);
static inline void iPadUpdateRumble(_tagxPad* pad);

S32 iPadInit()
{
    S32 i;

    PADInit();
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++)
    {
        sPadCurrLevel[i] = 0;
        sPadDeltaLevel[i] = 0;
        sPadMotor[i] = PAD_MOTOR_STOP;
    }
    return 1;
}

_tagxPad* iPadEnable(_tagxPad* pad, S16 port)
{
    U32 new_on;

    pad->port = port;
    pad->slot = 0;
    pad->state = ePad_Enabled;
    gTrcPad[pad->port].state = TRC_PadInserted;
    pad->flags |= 3;
    pad->flags |= 4;
    iPadUpdate(pad, &new_on);
    pad->on = new_on;
    pad->pressed = 0;
    pad->released = 0;
    return pad;
}

static inline S8 iPadConvStick(F32 in)
{
    F32 fin;

    if (in > 40.0f)
    {
        in = 40.0f;
    }
    else if (in < -40.0f)
    {
        in = -40.0f;
    }

    fin = 3.2f * in;
    if (fin > 127.0f)
    {
        fin = 127.0f;
    }
    else if (fin < -127.0f)
    {
        fin = -127.0f;
    }

    return fin;
}

static inline void iPadInHandReset(_tagxPad* pad, U32* on)
{
    static iTime held_start = 0;
    static U8 held_through_reset = 1;

    if (pad->port == 0)
    {
        // B + X + Start held together
        bool held = (*on & 0xA0001) == 0xA0001;
        if (held && !held_through_reset)
        {
            if (iTimeDiffSec(held_start, iTimeGet()) >= 0.5f)
            {
                ResetButton::ResetSystem();
            }
        }
        else
        {
            held_start = iTimeGet();
            if (!held)
            {
                held_through_reset = 0;
            }
        }
    }
}

S32 iPadUpdate(_tagxPad* pad, U32* on)
{
    U32 temp_on;

    if (pad->port == 0)
    {
        PADRead(sPadData);
        PADClamp(sPadData);
    }

    switch (sPadData[pad->port].err)
    {
    case PAD_ERR_NO_CONTROLLER:
        xTRCPad(pad->port, TRC_PadMissing);
        PADReset(0x80000000 >> pad->port);
        pad->released = pad->on;
        pad->on = 0;
        pad->analog1.x = 0;
        pad->analog1.y = 0;
        pad->analog2.x = 0;
        pad->analog2.y = 0;
        // goto as in BfBB: retail branches over the NOT_READY/TRANSFER block into the default
        // case's return block.
        goto defaultError;

    case PAD_ERR_NOT_READY:
    case PAD_ERR_TRANSFER:
        return 0;

    default:
    defaultError:
        return 0;

    case PAD_ERR_NONE:
        temp_on = iPadConvFromGCN(sPadData[pad->port].button, 1, 0x80);
        temp_on |= iPadConvFromGCN(sPadData[pad->port].button, 2, 0x20);
        temp_on |= iPadConvFromGCN(sPadData[pad->port].button, 4, 0x40);
        temp_on |= iPadConvFromGCN(sPadData[pad->port].button, 8, 0x10);

        if (sPadData[pad->port].button & 0x10)
        {
            temp_on |= iPadConvFromGCN(sPadData[pad->port].button, 0x20, 0x2000);
            temp_on |= iPadConvFromGCN(sPadData[pad->port].button, 0x40, 0x200);
            if (sPadData[pad->port].triggerLeft >= 0x18)
            {
                temp_on |= 0x200;
            }
            if (sPadData[pad->port].triggerRight >= 0x18)
            {
                temp_on |= 0x2000;
            }
            temp_on |= 0x100000;
        }
        else
        {
            temp_on |= iPadConvFromGCN(sPadData[pad->port].button, 0x20, 0x1000);
            temp_on |= iPadConvFromGCN(sPadData[pad->port].button, 0x40, 0x100);
            if (sPadData[pad->port].triggerLeft >= 0x18)
            {
                temp_on |= 0x100;
            }
            if (sPadData[pad->port].triggerRight >= 0x18)
            {
                temp_on |= 0x1000;
            }
        }

        temp_on |= iPadConvFromGCN(sPadData[pad->port].button, 0x100, 0x10000);
        temp_on |= iPadConvFromGCN(sPadData[pad->port].button, 0x200, 0x80000);
        temp_on |= iPadConvFromGCN(sPadData[pad->port].button, 0x800, 0x40000);
        temp_on |= iPadConvFromGCN(sPadData[pad->port].button, 0x400, 0x20000);
        temp_on |= iPadConvFromGCN(sPadData[pad->port].button, 0x1000, 0x1);
        (*on) = temp_on;

        pad->analog1.x = iPadConvStick(sPadData[pad->port].stickX);
        pad->analog1.y = -iPadConvStick(sPadData[pad->port].stickY);
        pad->analog2.x = iPadConvStick(sPadData[pad->port].substickX);
        pad->analog2.y = -iPadConvStick(sPadData[pad->port].substickY);
        if (gTrcPad[pad->port].state != TRC_PadInserted)
        {
            xTRCPad(pad->port, TRC_PadInserted);
        }
        iPadUpdateRumble(pad);
        iPadInHandReset(pad, on);
    }
    return 1;
}

static inline U32 iPadConvFromGCN(U32 buttons, U32 gcn_flag, U32 x_flag)
{
    return (buttons & gcn_flag) ? x_flag : 0;
}

void iPadStopRumble(_tagxPad* pad)
{
    sPadCurrLevel[pad->port] = 0;
    sPadDeltaLevel[pad->port] = 0;
    sPadMotor[pad->port] = PAD_MOTOR_STOP;
    PADControlMotor(pad->port, PAD_MOTOR_STOP);
}

void iPadStopRumble()
{
    _tagxPad* pad = &mPad[globals.currentActivePad];

    sPadCurrLevel[pad->port] = 0;
    sPadDeltaLevel[pad->port] = 0;
    sPadMotor[pad->port] = PAD_MOTOR_STOP;
    PADControlMotor(pad->port, PAD_MOTOR_STOP);
}

void iPadStartRumble(_tagxPad* pad, F32 intensity)
{
    if (pad->flags & 8)
    {
        if (intensity < g_rumblingThreshold)
        {
            sPadCurrLevel[pad->port] = 0;
            sPadDeltaLevel[pad->port] = 0;
            sPadMotor[pad->port] = PAD_MOTOR_STOP;
        }
        else
        {
            sPadCurrLevel[pad->port] = 0;
            sPadDeltaLevel[pad->port] = 3600.0 * (0.05 + intensity);
            sPadMotor[pad->port] = PAD_MOTOR_RUMBLE;
        }
        PADControlMotor(pad->port, sPadMotor[pad->port]);
    }
}

static inline void iPadUpdateRumble(_tagxPad* pad)
{
    if (pad->flags & 8)
    {
        sPadCurrLevel[pad->port] += sPadDeltaLevel[pad->port];
        if (sPadCurrLevel[pad->port] > 3600)
        {
            sPadCurrLevel[pad->port] = 0;
            if (sPadMotor[pad->port] != PAD_MOTOR_RUMBLE)
            {
                sPadMotor[pad->port] = PAD_MOTOR_RUMBLE;
                PADControlMotor(pad->port, sPadMotor[pad->port]);
            }
        }
        else if (sPadMotor[pad->port] != PAD_MOTOR_STOP)
        {
            sPadMotor[pad->port] = PAD_MOTOR_STOP;
            PADControlMotor(pad->port, sPadMotor[pad->port]);
        }
    }
}

void iPadKill()
{
}
