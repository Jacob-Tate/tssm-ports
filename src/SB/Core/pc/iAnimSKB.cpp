#include "iAnimSKB.h"

void iAnimEvalSKB(iAnimSKBHeader* data, F32 time, U32 flags, xVec3* tran, xQuat* quat)
{
    U32 i, tidx, bcount, tcount;
    iAnimSKBKey* keys;
    F32* times;
    U16* offsets;
    S32 asdf; // unused

    tcount = data->TimeCount;
    bcount = data->BoneCount;

    keys = (iAnimSKBKey*)(data + 1);
    times = (F32*)(keys + data->KeyCount);
    offsets = (U16*)(times + tcount);

    if (time < 0.0f)
    {
        time = 0.0f;
    }

    if (time > times[tcount - 1])
    {
        time = times[tcount - 1];
    }

    tidx = (tcount - 1) % 4;

    while (times[tidx] < time)
    {
        tidx += 4;
    }

    while (tidx && time <= times[tidx])
    {
        tidx--;
    }

    offsets += tidx * bcount;

    if (flags & 0x1)
    {
        bcount = 1;
    }

    if (flags & 0x2)
    {
        bcount--;
        offsets++;
    }

    if (tcount == 1)
    {
        F32 scalex = data->Scale[0];
        F32 scaley = data->Scale[1];
        F32 scalez = data->Scale[2];
        iAnimSKBKey* k = keys;

        for (i = 0; i < bcount; i++)
        {
            quat->v.x = (1.0f / 32767.0f) * k->Quat[0];
            quat->v.y = (1.0f / 32767.0f) * k->Quat[1];
            quat->v.z = (1.0f / 32767.0f) * k->Quat[2];
            quat->s = (1.0f / 32767.0f) * k->Quat[3];
            quat++;

            tran->x = scalex * k->Tran[0];
            tran->y = scaley * k->Tran[1];
            tran->z = scalez * k->Tran[2];
            tran++;

            // Each bone stores a key pair; a single-time animation uses the first key.
            k += 2;
        }
    }
    else
    {
        F32 scalex = data->Scale[0];
        F32 scaley = data->Scale[1];
        F32 scalez = data->Scale[2];

        for (i = 0; i < bcount; i++)
        {
            F32 time1, time2, lerp;
            iAnimSKBKey* k = &keys[*offsets];
            RtQuat q1, q2;
            RtQuatSlerpCache qcache;
            xVec3 t1, t2;

            offsets++;

            time1 = time - times[k[0].TimeIndex];
            time2 = times[k[1].TimeIndex] - times[k[0].TimeIndex];
            lerp = time1 / time2;

            q1.imag.x = (1.0f / 32767.0f) * k[0].Quat[0];
            q1.imag.y = (1.0f / 32767.0f) * k[0].Quat[1];
            q1.imag.z = (1.0f / 32767.0f) * k[0].Quat[2];
            q1.real = (1.0f / 32767.0f) * k[0].Quat[3];

            q2.imag.x = (1.0f / 32767.0f) * k[1].Quat[0];
            q2.imag.y = (1.0f / 32767.0f) * k[1].Quat[1];
            q2.imag.z = (1.0f / 32767.0f) * k[1].Quat[2];
            q2.real = (1.0f / 32767.0f) * k[1].Quat[3];

            RtQuatSetupSlerpCache(&q1, &q2, &qcache);
            RtQuatSlerp((RtQuat*)quat, &q1, &q2, lerp, &qcache);

            t1.x = scalex * k[0].Tran[0];
            t1.y = scaley * k[0].Tran[1];
            t1.z = scalez * k[0].Tran[2];

            t2.x = scalex * k[1].Tran[0];
            t2.y = scaley * k[1].Tran[1];
            t2.z = scalez * k[1].Tran[2];

            tran->x = lerp * (t2.x - t1.x) + t1.x;
            tran->y = lerp * (t2.y - t1.y) + t1.y;
            tran->z = lerp * (t2.z - t1.z) + t1.z;

            quat++;
            tran++;
        }
    }
}

void _iAnimSKBAdjustTranslate(iAnimSKBHeader* data, U32 bone, F32* starttran, F32* endtran)
{
    S32 ipos;
    U32 i, idx, keyfirst, keylast, kcount, bcount, tcount;
    F32 outScale[3];
    F32 pos;
    F32 factor[3];
    F32 oldmax[3] = { 0.0f, 0.0f, 0.0f };
    F32 newmax[3] = { 0.0f, 0.0f, 0.0f };
    F32 timefirst, timelast;
    iAnimSKBKey* keys;
    F32* times;
    U16* offsets;

    tcount = data->TimeCount;
    bcount = data->BoneCount;
    kcount = data->KeyCount;

    keys = (iAnimSKBKey*)(data + 1);
    times = (F32*)(keys + kcount);
    offsets = (U16*)(times + tcount);

    keyfirst = offsets[bone];
    keylast = offsets[bone + (tcount - 2) * bcount] + 1;

    timefirst = times[0];
    timelast = times[tcount - 1];

    for (i = 0; i < kcount; i++)
    {
        for (idx = 0; idx < 3; idx++)
        {
            if (starttran[idx] || endtran[idx])
            {
                pos = data->Scale[idx] * keys[i].Tran[idx];

                if (xabs(pos) > oldmax[idx])
                {
                    oldmax[idx] = xabs(pos);
                }

                if (i >= keyfirst && i <= keylast)
                {
                    pos += starttran[idx] + (endtran[idx] - starttran[idx]) *
                                                (times[keys[i].TimeIndex] - timefirst) /
                                                (timelast - timefirst);

                    if (xabs(pos) > newmax[idx])
                    {
                        newmax[idx] = xabs(pos);
                    }
                }
            }
        }
    }

    for (idx = 0; idx < 3; idx++)
    {
        if (starttran[idx] || endtran[idx])
        {
            if (newmax[idx] > oldmax[idx])
            {
                outScale[idx] = data->Scale[idx] * newmax[idx] / oldmax[idx];
            }
            else
            {
                outScale[idx] = data->Scale[idx];
            }

            factor[idx] = 1.0f / outScale[idx];
        }
    }

    for (i = 0; i < kcount; i++)
    {
        for (idx = 0; idx < 3; idx++)
        {
            if (starttran[idx] || endtran[idx])
            {
                if (i >= keyfirst && i <= keylast)
                {
                    pos = starttran[idx] + (endtran[idx] - starttran[idx]) *
                                               (times[keys[i].TimeIndex] - timefirst) /
                                               (timelast - timefirst);

                    ipos = factor[idx] * (data->Scale[idx] * keys[i].Tran[idx] + pos);

                    if (ipos < -32767)
                    {
                        ipos = -32767;
                    }
                    else if (ipos > 32767)
                    {
                        ipos = 32767;
                    }

                    keys[i].Tran[idx] = ipos;
                }
                else if (data->Scale[idx] != outScale[idx])
                {
                    pos = data->Scale[idx] * keys[i].Tran[idx];

                    ipos = pos * factor[idx];

                    if (ipos < -32767)
                    {
                        ipos = -32767;
                    }
                    else if (ipos > 32767)
                    {
                        ipos = 32767;
                    }

                    keys[i].Tran[idx] = ipos;
                }
            }
        }
    }

    for (idx = 0; idx < 3; idx++)
    {
        if (starttran[idx] || endtran[idx])
        {
            data->Scale[idx] = outScale[idx];
        }
    }
}

S32 _iAnimSKBNumExtract(iAnimSKBHeader* data, U32 bone)
{
    U32 i, keylast, tcount;
    iAnimSKBKey* keys;
    F32* times;
    U16* offsets;
    S32 tranFound;
    S32 lastTime, currTime;

    tranFound = 0;
    lastTime = -1;

    tcount = data->TimeCount;

    keys = (iAnimSKBKey*)(data + 1);
    times = (F32*)(keys + data->KeyCount);
    offsets = (U16*)(times + tcount);

    keylast = offsets[bone + (tcount - 2) * data->BoneCount] + 1;

    for (i = offsets[bone]; i <= keylast; i++)
    {
        currTime = 30.0f * times[keys[i].TimeIndex];

        if (lastTime >= 0 && currTime > lastTime + 1)
        {
            tranFound += currTime - (lastTime + 1);
        }

        if (lastTime != currTime)
        {
            tranFound++;
        }

        lastTime = currTime;
    }

    return tranFound;
}

S32 _iAnimSKBExtractTranslate(iAnimSKBHeader* data, U32 bone, xVec3* tranArray, S32 tranCount)
{
    U8 axis[3];

    axis[0] = 1;
    axis[1] = 1;
    axis[2] = 1;

    return _iAnimSKBExtractTranslateAxis(data, bone, tranArray, tranCount, axis);
}

S32 _iAnimSKBExtractTranslateAxis(iAnimSKBHeader* data, U32 bone, xVec3* tranArray, S32 tranCount,
                                  U8* axis)
{
    U32 i, j, keylast, tcount;
    iAnimSKBKey* keys;
    F32* times;
    U16* offsets;
    xVec3 currTran;
    xVec3* lastTran;
    S32 tranFound;
    S32 lastTime, currTime;
    F32 lerp;

    tranFound = 0;
    lastTime = -1;

    tcount = data->TimeCount;

    keys = (iAnimSKBKey*)(data + 1);
    times = (F32*)(keys + data->KeyCount);
    offsets = (U16*)(times + tcount);

    keylast = offsets[bone + (tcount - 2) * data->BoneCount] + 1;

    for (i = offsets[bone]; i <= keylast; i++)
    {
        currTime = 30.0f * times[keys[i].TimeIndex];

        currTran.x = axis[0] ? data->Scale[0] * keys[i].Tran[0] : 0.0f;
        currTran.y = axis[1] ? data->Scale[1] * keys[i].Tran[1] : 0.0f;
        currTran.z = axis[2] ? data->Scale[2] * keys[i].Tran[2] : 0.0f;

        if (lastTime >= 0 && currTime > lastTime + 1)
        {
            lastTran = tranArray - 1;

            for (j = 1; j < currTime - lastTime; j++)
            {
                lerp = (F32)j / (currTime - lastTime);

                tranArray->x = lerp * (currTran.x - lastTran->x) + lastTran->x;
                tranArray->y = lerp * (currTran.y - lastTran->y) + lastTran->y;
                tranArray->z = lerp * (currTran.z - lastTran->z) + lastTran->z;

                tranArray++;
                tranFound++;
            }
        }

        if (lastTime != currTime)
        {
            tranArray->x = currTran.x;
            tranArray->y = currTran.y;
            tranArray->z = currTran.z;
            tranArray++;
            tranFound++;
        }

        lastTime = currTime;

        for (j = 0; j < 3; j++)
        {
            if (axis[j])
            {
                keys[i].Tran[j] = 0;
            }
        }
    }

    return tranFound;
}

F32 QuatToYaw(xQuat* q)
{
    // Retail bug: the last term is z + z where the yaw formula needs z * z.
    return xAngleClampFast(
        atan2(2.0f * (q->s * q->v.y - q->v.x * q->v.z),
              q->s * q->s + q->v.x * q->v.x - q->v.y * q->v.y - (q->v.z + q->v.z)));
}

static inline void YawToQuat(F32 yaw, xQuat* q)
{
    q->s = cosf(0.5f * yaw);
    q->v.x = 0.0f;
    q->v.y = sinf(0.5f * yaw);
    q->v.z = 0.0f;
}

static inline void QuatMul(xQuat* o, xQuat* a, xQuat* b)
{
    F32 s = a->s * b->s - a->v.x * b->v.x - a->v.y * b->v.y - a->v.z * b->v.z;
    F32 x = a->s * b->v.x + a->v.x * b->s + a->v.y * b->v.z - a->v.z * b->v.y;
    F32 y = a->s * b->v.y + a->v.y * b->s + a->v.z * b->v.x - a->v.x * b->v.z;
    F32 z = a->s * b->v.z + a->v.z * b->s + a->v.x * b->v.y - a->v.y * b->v.x;

    o->v.x = x;
    o->v.y = y;
    o->v.z = z;
    o->s = s;
}

S32 _iAnimSKBExtractYaw(iAnimSKBHeader* data, U32 bone, F32* yawArray, S32 yawCount)
{
    U32 i, j, keylast, tcount;
    iAnimSKBKey* keys;
    F32* times;
    U16* offsets;
    F32 currYaw;
    F32 lastYaw;
    F32 lastCurrYaw;
    S32 yawFound;
    S32 lastTime, currTime;

    yawFound = 0;
    lastTime = -1;
    lastCurrYaw = PI;

    tcount = data->TimeCount;

    keys = (iAnimSKBKey*)(data + 1);
    times = (F32*)(keys + data->KeyCount);
    offsets = (U16*)(times + tcount);

    keylast = offsets[bone + (tcount - 2) * data->BoneCount] + 1;

    for (i = offsets[bone]; i <= keylast; i++)
    {
        xQuat currQuat, antiYaw;

        currTime = 30.0f * times[keys[i].TimeIndex];

        currQuat.v.x = (1.0f / 32767.0f) * keys[i].Quat[0];
        currQuat.v.y = (1.0f / 32767.0f) * keys[i].Quat[1];
        currQuat.v.z = (1.0f / 32767.0f) * keys[i].Quat[2];
        currQuat.s = (1.0f / 32767.0f) * keys[i].Quat[3];

        currYaw = QuatToYaw(&currQuat);

        while (currYaw + PI < lastCurrYaw)
        {
            currYaw += 2.0f * PI;
        }

        while (currYaw - PI > lastCurrYaw)
        {
            currYaw -= 2.0f * PI;
        }

        if (lastTime >= 0 && currTime > lastTime + 1)
        {
            lastYaw = yawArray[-1];

            for (j = 1; j < currTime - lastTime; j++)
            {
                *yawArray = (F32)j / (currTime - lastTime) * (currYaw - lastYaw) + lastYaw;
                yawArray++;
                yawFound++;
            }
        }

        if (lastTime != currTime)
        {
            *yawArray = currYaw;
            yawArray++;
            yawFound++;
        }

        lastTime = currTime;

        YawToQuat(-currYaw, &antiYaw);
        QuatMul(&currQuat, &currQuat, &antiYaw);

        keys[i].Quat[0] = CLAMP((S32)(32767.0f * currQuat.v.x), -32767.0f, 32767.0f);
        keys[i].Quat[1] = CLAMP((S32)(32767.0f * currQuat.v.y), -32767.0f, 32767.0f);
        keys[i].Quat[2] = CLAMP((S32)(32767.0f * currQuat.v.z), -32767.0f, 32767.0f);
        keys[i].Quat[3] = CLAMP((S32)(32767.0f * currQuat.s), -32767.0f, 32767.0f);

        lastCurrYaw = currYaw;
    }

    return yawFound;
}

void _iAnimSKBAdjustYaw(iAnimSKBHeader* data, U32 bone, F32 yawStart, F32 yawEnd)
{
    U32 i, keyfirst, keylast, kcount, tcount;
    F32 timefirst;
    iAnimSKBKey* keys;
    F32* times;
    U16* offsets;
    F32 invTimeDiff;

    tcount = data->TimeCount;
    kcount = data->KeyCount;

    keys = (iAnimSKBKey*)(data + 1);
    times = (F32*)(keys + kcount);
    offsets = (U16*)(times + tcount);

    keyfirst = offsets[bone];
    keylast = offsets[bone + (tcount - 2) * data->BoneCount] + 1;

    timefirst = times[0];
    invTimeDiff = 1.0f / (times[tcount - 1] - timefirst);

    for (i = 0; i < kcount; i++)
    {
        if (i >= keyfirst && i <= keylast)
        {
            xQuat quatA, quatB;
            F32 yaw = invTimeDiff * ((yawEnd - yawStart) * (times[keys[i].TimeIndex] - timefirst)) +
                      yawStart;

            quatB.v.x = (1.0f / 32767.0f) * keys[i].Quat[0];
            quatB.v.y = (1.0f / 32767.0f) * keys[i].Quat[1];
            quatB.v.z = (1.0f / 32767.0f) * keys[i].Quat[2];
            quatB.s = (1.0f / 32767.0f) * keys[i].Quat[3];

            YawToQuat(yaw, &quatA);
            QuatMul(&quatB, &quatB, &quatA);

            keys[i].Quat[0] = CLAMP((S32)(32767.0f * quatB.v.x), -32767.0f, 32767.0f);
            keys[i].Quat[1] = CLAMP((S32)(32767.0f * quatB.v.y), -32767.0f, 32767.0f);
            keys[i].Quat[2] = CLAMP((S32)(32767.0f * quatB.v.z), -32767.0f, 32767.0f);
            keys[i].Quat[3] = CLAMP((S32)(32767.0f * quatB.s), -32767.0f, 32767.0f);
        }
    }
}

// Dead-stripped placeholder (the decomp's __deadstripped_<File> convention). Retail numbers -32767.0f
// (@16276) and 32767.0f (@16277) about 95 labels BEFORE _iAnimSKBAdjustYaw's first new literal
// (1.0f / 32767.0f, @16371), although AdjustYaw (100%) loads 32767.0f first and both only after
// that multiply; inside one function CW numbers literals in instruction order (probed: named
// locals and inline helpers with the bounds as parameters keep that order). Code generation runs
// in reverse source order, so a function after AdjustYaw loaded -32767.0f then 32767.0f and the
// linker stripped it (no TSSM symbol; BfBB/Incredibles have only extern CmpBoneTime/CmpTime).
// The Rat erased DWARF (dtk --include-erased) names it: right after _iAnimSKBAdjustYaw comes the
// dead-stripped 'static void _iAnimSKBFlip180(iAnimSKBHeader* data, U32 bone)' (locals i,
// keyfirst, keylast, tranCount, kcount, bcount, tcount, keys, times, tran, offsets, float
// quat[4], dQuat, _s/_x/_y/_z, tmpTran): a compressed-quaternion rewrite that needs the +-32767
// bounds. Its body is unknown. Every iAnimSKB.cpp function matches, with its other literals in
// retail's order.
static void __deadstripped_iAnimSKB(F32* out)
{
    out[0] = -32767.0f;
    out[1] = 32767.0f;
}
