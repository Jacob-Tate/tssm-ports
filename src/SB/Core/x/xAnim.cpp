#include "xAnim.h"
#include "iAnimSKB.h"
#include "xRand.h"

static xMemPool sxAnimTempTranPool;
static xMemPool sxAnimTempStatePool;

U32 gxAnimUseGrowAlloc = 0;

#define ADD_4_BITS(x) (((x) & 1) + (((x) >> 1) & 1) + (((x) >> 2) & 1) + (((x) >> 3) & 1))

static void EffectSingleStart(xAnimSingle* single);
static void TransitionTimeInit(xAnimSingle* single, xAnimTransition* tran);
static void EffectSingleRun(xAnimSingle* single);
static void EffectSingleLoop(xAnimSingle* single);
static void LoopUpdate(xAnimSingle* single);

static inline bool _xCharIn(char ch, const char* str)
{
    for (S32 i = 0; str[i] != '\0'; i++)
    {
        if (ch == str[i])
        {
            return true;
        }
    }

    return false;
}

static inline bool _xSingleCompare(char a, char b)
{
    switch (b)
    {
    case '?':
    case '*':
    case '+':
    {
        return true;
    }
    case '#':
    {
        return (a >= '0' && a <= '9');
    }
    }

    return (a == b);
}

static inline bool _xSingleCompare(char ch, const char* str)
{
    while (*str != '\0')
    {
        if (_xSingleCompare(ch, *str))
        {
            return true;
        }

        str++;
    }

    return false;
}

static U8 _xCheckAnimNameInner(const char* name, const char* pattern, S32 patternSize, char* extra,
                               S32* nameOut, S32* extraOut)
{
    const char* startExtra = NULL;
    char* initialExtra = extra;
    S32 patternCurrent = 0;
    S32 nameCurrent = 0;
    while (patternCurrent < patternSize)
    {
        // int in the PS2 DWARF, but retail compares it unsigned (cmplwi)
        U32 check = pattern[patternCurrent];
        switch (check)
        {
        case '+':
        case '?':
            patternCurrent++;
            if (nameCurrent == 0)
            {
                return 0;
            }
            nameCurrent++;
            if (check != '?')
            {
            // '+' falls into the '*' case below; jumping into this block is the original control flow
            case '*':
                check = patternCurrent + 1;
                char nextPattern[128] = { 0 };
                nextPattern[0] = pattern[check];
                while (nextPattern[0] == '{' || nextPattern[0] == '}' || nextPattern[0] == '<' ||
                       nextPattern[0] == '>')
                {
                    check++;
                    nextPattern[0] = pattern[check];
                }

                if (nextPattern[0] == '(')
                {
                    S32 nextPatternCount = 0;
                    U8 first = 1;
                    S32 parenCount = 0;

                    while (pattern[check] != NULL && parenCount >= 0)
                    {
                        if (pattern[check] == '(')
                        {
                            parenCount++;
                        }
                        else if (pattern[check] == ')')
                        {
                            parenCount--;
                        }
                        else if (parenCount == 0)
                        {
                            const char* IGNORE_PATTERNS = "{}()<>";
                            if (pattern[check] == '|')
                            {
                                first = 1;
                            }
                            else if (first && _xCharIn(pattern[check], IGNORE_PATTERNS) == 0)
                            {
                                first = 0;
                                nextPattern[nextPatternCount++] = pattern[check];
                            }
                        }
                        check++;
                    }
                    nextPattern[nextPatternCount] = NULL;
                }

                while (name[nameCurrent] != NULL &&
                       _xSingleCompare(name[nameCurrent], nextPattern) == 0)
                {
                    nameCurrent++;
                }
                patternCurrent++;
            }
            break;
        case '#':
            if (name[nameCurrent] < '0' || name[nameCurrent] > '9')
            {
                return 0;
            }
            nameCurrent++;

            while (name[nameCurrent] >= '0' && name[nameCurrent] <= '9')
            {
                nameCurrent++;
            }
            patternCurrent++;
            break;
        case '{':
            startExtra = &name[nameCurrent];
            patternCurrent++;
            break;
        case '}':
            S32 length;
            length = &name[nameCurrent] - startExtra;
            if (extra != NULL)
            {
                memcpy(extra, startExtra, length);
                extra[length] = NULL;
                extra += length + 1;
                *extra = '\x01';
            }
            startExtra = NULL;
            patternCurrent++;
            break;
        case '(':
        {
            patternCurrent++;
            U8 done = 0;
            const char* current = &pattern[patternCurrent];
            while (*current != ')' && *current != NULL)
            {
                const char* startPattern = current;
                while (*current != NULL && *current != ')' && *current != '|')
                {
                    if (*current == '(')
                    {
                        S32 pc = 1;
                        while (*current != NULL && pc > 0)
                        {
                            if (*current == ')')
                            {
                                pc--;
                            }
                            else if (*current == '(')
                            {
                                pc++;
                            }
                            current++;
                        }
                        if (*current != NULL)
                        {
                            current++;
                        }
                    }
                    else
                    {
                        current++;
                    }
                }

                if (current != startPattern)
                {
                    S32 nameOut = 0;
                    S32 extraOut = 0;

                    // The target materializes the result of the && as a byte before testing it.
                    U8 ret = !done && _xCheckAnimNameInner(&name[nameCurrent], startPattern,
                                                           current - startPattern, extra, &nameOut,
                                                           &extraOut);

                    if (ret != 0)
                    {
                        done = 1;
                        nameCurrent += nameOut;
                        extra = extra + extraOut;
                    }
                    else if (extra != NULL)
                    {
                        *extra = '\x01';
                    }
                }

                if (*current == '|')
                {
                    current++;
                }
            }
            if (*current != NULL)
            {
                current++;
            }
            patternCurrent += current - &pattern[patternCurrent];
            if (!done)
            {
                return 0;
            }
            break;
        }
        case '<':
        {
            patternCurrent++;
            const char* current = &pattern[patternCurrent];
            // A separate copy, as in Ratatouille; retail keeps it in its own register.
            const char* positive = current;
            while (*current != NULL && *current != ';' && *current != '>')
            {
                current++;
            }

            const char* positiveEnd = current;
            const char* negative = NULL;
            const char* negativeEnd = NULL;

            if (*current == ';')
            {
                current++;
                negative = current;
                while (*current != NULL && *current != '>')
                {
                    current++;
                }
                negativeEnd = current;
            }
            S32 nameOut;
            S32 extraOut;
            U8 matched = _xCheckAnimNameInner(&name[nameCurrent], positive, positiveEnd - positive,
                                              extra, &nameOut, &extraOut);
            if (matched != 0)
            {
                if (negative != NULL &&
                    _xCheckAnimNameInner(&name[nameCurrent], negative, negativeEnd - negative, NULL,
                                         NULL, NULL) != 0)
                {
                    if (extra != NULL)
                    {
                        *extra = '\x01';
                    }
                    matched = 0;
                }
                else
                {
                    nameCurrent = nameCurrent + nameOut;
                    extra += extraOut;
                }
            }

            if (*current != NULL)
            {
                current++;
            }
            patternCurrent += current - &pattern[patternCurrent];
            if (matched == 0)
            {
                return 0;
            }
            break;
        }
        case NULL:
            return 0;
        case ')':
        default:
            if (name[nameCurrent] != pattern[patternCurrent])
            {
                return 0;
            }
            nameCurrent++;
            patternCurrent++;
            break;
        }
    }

    if (nameOut != NULL)
    {
        *nameOut = nameCurrent;
    }
    if (extraOut != NULL)
    {
        *extraOut = extra - initialExtra;
    }
    return 1;
}

void xAnimInit()
{
    iAnimInit();
    memset(&sxAnimTempTranPool, 0, sizeof(xMemPool));
    memset(&sxAnimTempStatePool, 0, sizeof(xMemPool));
}

void xAnimTempTransitionInit(U32 count)
{
    xMemPoolSetup(&sxAnimTempTranPool, xMemAllocSize(count * sizeof(xAnimTransition)), 0, 0, NULL,
                  sizeof(xAnimTransition), count, 0);
}

xAnimTransition* xAnimTempTransitionAlloc(const xAnimTransition* source)
{
    xAnimTransition* tran = (xAnimTransition*)xMemPoolAlloc(&sxAnimTempTranPool);

    if (source != NULL)
    {
        memcpy(tran, source, sizeof(xAnimTransition));
    }
    else
    {
        memset(tran, 0, sizeof(xAnimTransition));
    }

    tran->Next = NULL;
    tran->Flags |= 0x2;

    return tran;
}

void xAnimTempStateInit(U32 count)
{
    xMemPoolSetup(&sxAnimTempStatePool, xMemAllocSize(count * sizeof(xAnimState)), 0, 0, NULL,
                  sizeof(xAnimState), count, 0);
}

inline xAnimState* xAnimTempStateAlloc(const xAnimState* source)
{
    xAnimState* state = (xAnimState*)xMemPoolAlloc(&sxAnimTempStatePool);

    if (source != NULL)
    {
        memcpy(state, source, sizeof(xAnimState));
    }
    else
    {
        memset(state, 0, sizeof(xAnimState));
    }

    state->Next = NULL;
    state->Flags |= 0x800;

    return state;
}

static inline void SetTransition(xAnimTransition*& dest, xAnimTransition* tran)
{
    if (dest != tran)
    {
        if (dest != NULL && dest->Flags & 0x2)
        {
            xMemPoolFree(&sxAnimTempTranPool, dest);
        }
        dest = tran;
    }
}

static inline void SetState(xAnimState*& dest, xAnimState* state)
{
    if (state != NULL && state->Flags & 0x400)
    {
        state = xAnimTempStateAlloc(state);
    }

    if (dest != state)
    {
        if (dest != NULL && dest->Flags & 0x800)
        {
            xMemPoolFree(&sxAnimTempStatePool, dest);
        }
        dest = state;
    }
}

static void _xAnimTransitionListInsert(xAnimTransitionList** list, xAnimTransitionList* toInsert)
{
    xAnimTransitionList* curr;

    if (*list == NULL || toInsert->T->Priority > (*list)->T->Priority ||
        (toInsert->T->Priority == (*list)->T->Priority && toInsert->T->Conditional != NULL &&
         (*list)->T->Conditional == NULL))
    {
        toInsert->Next = *list;
        *list = toInsert;
    }
    else
    {
        curr = *list;
        while (curr->Next != NULL &&
               (curr->Next->T->Priority > toInsert->T->Priority ||
                (curr->Next->T->Priority == toInsert->T->Priority &&
                 (toInsert->T->Conditional ? false : curr->Next->T->Conditional != NULL))))
        {
            curr = curr->Next;
        }

        toInsert->Next = curr->Next;
        curr->Next = toInsert;
    }
}

static U32 DefaultOverride(xAnimState* state, xAnimTransition* tran)
{
    xAnimTransitionList* curr = state->Default;
    xAnimTransitionList* prev;

    while (curr != NULL)
    {
        if (curr->T->Conditional == tran->Conditional && curr->T->UserFlags == tran->UserFlags)
        {
            if (curr->T->Priority == tran->Priority)
            {
                curr->T = tran;
            }
            else if (tran->Priority > curr->T->Priority)
            {
                if (curr == state->Default)
                {
                    curr->T = tran;
                }
                else
                {
                    prev->Next = curr->Next;
                    curr->T = tran;
                    _xAnimTransitionListInsert(&state->Default, curr);
                }
            }

            return TRUE;
        }

        prev = curr;
        curr = curr->Next;
    }

    return FALSE;
}

static void TransitionTimeInit(xAnimSingle* single, xAnimTransition* tran)
{
    switch (tran->Flags & 0xE0)
    {
    case 0x20:
    {
        xAnimFile* dest = tran->Dest->Data;

        if ((single->State->Data->FileFlags ^ dest->FileFlags) & 0x1000)
        {
            single->Time = dest->Duration - single->Time;
        }
        break;
    }
    case 0x40:
    {
        xAnimFile* src = single->State->Data;
        xAnimFile* dest = tran->Dest->Data;

        single->Time *= dest->Duration / src->Duration;
        if ((src->FileFlags ^ dest->FileFlags) & 0x1000)
        {
            single->Time = dest->Duration - single->Time;
        }
        break;
    }
    case 0x80:
    {
        xAnimFile* src = single->State->Data;
        xAnimFile* dest = tran->Dest->Data;

        F32 played = single->Time / src->Duration;
        single->Time = (1.0f - played) * dest->Duration;
        break;
    }
    default:
        single->Time = ((tran->Dest->Flags & 0x100) && tran->DestTime == 0.0f) ?
                           single->State->Data->Duration * xurand() :
                           tran->DestTime;
        break;
    }

    single->LastTime = single->Time;
}

void xAnimCalcPhysicsData(xAnimFile* afile, xAnimFile* endPose)
{
    S32 numAnims = afile->NumAnims[0] * afile->NumAnims[1];

    if (((iAnimSKBHeader*)afile->RawData[0])->ExtractedData != NULL)
    {
        afile->PhysicsData = (xAnimPhysicsData*)((iAnimSKBHeader*)afile->RawData[0])->ExtractedData;
        return;
    }

    U8 axis[3];
    axis[0] = (afile->FileFlags & 0x10000) ? 1 : 0;
    if (afile->FileFlags & 0x20000)
    {
        axis[1] = 1;
    }
    else
    {
        axis[1] = 0;
    }
    if (afile->FileFlags & 0x40000)
    {
        axis[2] = 1;
    }
    else
    {
        axis[2] = 0;
    }

    S32 tranCount = _iAnimSKBNumExtract((iAnimSKBHeader*)afile->RawData[0], 1);

    U32 size = sizeof(xAnimPhysicsData) + tranCount * sizeof(xVec3);
    if (afile->FileFlags & 0x80000)
    {
        size += tranCount * sizeof(F32);
    }

    xAnimPhysicsData* physDat =
        (xAnimPhysicsData*)(gxAnimUseGrowAlloc ? xMemGrowAlloc(gActiveHeap, size) :
                                                 xMemAlloc(gActiveHeap, size, 0));
    afile->PhysicsData = physDat;

    physDat->tranTable = (xVec3*)&physDat[1];
    xVec3* tranTable = physDat->tranTable;
    if (afile->FileFlags & 0x80000)
    {
        physDat->yawTable = (F32*)&tranTable[tranCount];
    }
    else
    {
        physDat->yawTable = NULL;
    }
    F32* yawTable = physDat->yawTable;

    xVec3 tranStart[64];
    xQuat quatStart[64];
    xVec3 tranEnd[64];
    xQuat quatEnd[64];

    iAnimEval(afile->RawData[0], 0.0f, 0, tranStart, quatStart);
    if (!axis[0])
    {
        tranStart[1].x = 0.0f;
    }
    if (!axis[1])
    {
        tranStart[1].y = 0.0f;
    }
    if (!axis[2])
    {
        tranStart[1].z = 0.0f;
    }

    if (endPose != NULL)
    {
        iAnimEval(endPose->RawData[0], 0.0f, 0, tranEnd, quatEnd);
    }
    else
    {
        iAnimEval(afile->RawData[0], 0.0f, 0, tranEnd, quatEnd);
    }
    if (!axis[0])
    {
        tranEnd[1].x = 0.0f;
    }
    if (!axis[1])
    {
        tranEnd[1].y = 0.0f;
    }
    if (!axis[2])
    {
        tranEnd[1].z = 0.0f;
    }

    physDat->tranCount = _iAnimSKBExtractTranslateAxis((iAnimSKBHeader*)afile->RawData[0], 1,
                                                       tranTable, tranCount, axis);

    if (numAnims > 1)
    {
        xVec3* tempTable = (xVec3*)xMemPushTemp(tranCount * sizeof(xVec3));
        for (S32 i = 1; i < numAnims; i++)
        {
            _iAnimSKBExtractTranslateAxis((iAnimSKBHeader*)afile->RawData[i], 1, tempTable,
                                          tranCount, axis);
        }
        xMemPopTemp(tempTable);
    }

    for (S32 i = 1; i < tranCount; i++)
    {
        xVec3 subOff;
        xVec3Sub(&subOff, &tranEnd[1], &tranStart[1]);
        xVec3SMulBy(&subOff, (F32)i / (F32)(tranCount - 1));
        xVec3AddTo(&subOff, &tranTable[0]);
        xVec3SubFrom(&tranTable[i], &subOff);
    }

    xVec3Copy(tranTable, &g_O3);

    for (S32 i = 0; i < numAnims; i++)
    {
        _iAnimSKBAdjustTranslate((iAnimSKBHeader*)afile->RawData[i], 1, (F32*)&tranStart[1],
                                 (F32*)&tranEnd[1]);
    }

    if (yawTable != NULL)
    {
        F32 yawStart = QuatToYaw(&quatStart[1]);
        F32 yawEnd = QuatToYaw(&quatEnd[1]);

        physDat->tranCount =
            _iAnimSKBExtractYaw((iAnimSKBHeader*)afile->RawData[0], 1, yawTable, tranCount);

        if (numAnims > 1)
        {
            F32* tempYaw = (F32*)xMemPushTemp(tranCount * sizeof(F32));
            for (S32 i = 1; i < numAnims; i++)
            {
                _iAnimSKBExtractYaw((iAnimSKBHeader*)afile->RawData[i], 1, tempYaw, tranCount);
            }
            xMemPopTemp(tempYaw);
        }

        for (S32 i = 1; i < tranCount; i++)
        {
            F32 subOff = (F32)i * (yawEnd - yawStart) / (F32)(tranCount - 1);
            subOff += yawTable[0];
            yawTable[i] -= subOff;
        }
        yawTable[0] = 0.0f;

        for (S32 i = 0; i < numAnims; i++)
        {
            _iAnimSKBAdjustYaw((iAnimSKBHeader*)afile->RawData[i], 1, yawStart, yawEnd);
        }
    }

    for (S32 i = 0; i < numAnims; i++)
    {
        ((iAnimSKBHeader*)afile->RawData[i])->ExtractedData = physDat;
    }
}

xAnimFile* xAnimFileNewBilinear(void** rawData, const char* name, U32 hashed_name_id, U32 flags,
                                xAnimFile** linkedList, U32 numX, U32 numY)
{
    S32 i;
    xAnimFile* afile =
        (xAnimFile*)(gxAnimUseGrowAlloc ?
                         xMemGrowAlloc(gActiveHeap,
                                       numX * numY * sizeof(void*) + sizeof(xAnimFile)) :
                         xMemAlloc(gActiveHeap, numX * numY * sizeof(void*) + sizeof(xAnimFile),
                                   0));

    if (numX > 1 || numY > 1)
    {
        flags |= 0x4000;
    }

    if (**(U32**)rawData == 'QSPM')
    {
        flags |= 0x8000;
    }

    if (linkedList != NULL)
    {
        afile->Next = *linkedList;
        *linkedList = afile;
    }
    else
    {
        afile->Next = NULL;
    }

    afile->RawData = (void**)&afile[1];
    for (i = 0; i < (S32)numX * (S32)numY; ++i)
    {
        afile->RawData[i] = rawData[i];
    }

    afile->Name = name;
    afile->ID = hashed_name_id;
    afile->FileFlags = flags;

    afile->Duration =
        (afile->FileFlags & 0x2000) ?
            2.0f * ((afile->FileFlags & 0x8000) ?
                        xMorphSeqDuration(*(xMorphSeqFile**)afile->RawData) :
                        iAnimDuration(*afile->RawData)) :
            ((afile->FileFlags & 0x8000) ? xMorphSeqDuration(*(xMorphSeqFile**)afile->RawData) :
                                           iAnimDuration(*afile->RawData));
    afile->TimeOffset = 0.0f;

    afile->BoneCount = flags & 0x8000 ? 0 : iAnimBoneCount(*rawData);

    afile->NumAnims[0] = numX;
    afile->NumAnims[1] = numY;
    afile->PhysicsData = NULL;

    return afile;
}

xAnimFile* xAnimFileNew(void* rawData, const char* name, U32 flags, xAnimFile** linkedList)
{
    return xAnimFileNewBilinear(&rawData, name, xStrHash(name), flags, linkedList, 1, 1);
}

void xAnimFileSetTime(xAnimFile* data, float duration, float timeOffset)
{
    F32 rawDuration;
    if (data->FileFlags & 0x8000)
    {
        rawDuration = xMorphSeqDuration(*(xMorphSeqFile**)data->RawData);
    }
    else
    {
        rawDuration = iAnimDuration(*data->RawData);
    }

    if (timeOffset > rawDuration - 0.1f)
    {
        timeOffset = rawDuration - 0.1f;
    }

    data->TimeOffset = timeOffset;
    if (duration > rawDuration - timeOffset)
    {
        duration = rawDuration - timeOffset;
    }

    data->Duration = (data->FileFlags & 0x2000) ? 2.0f * duration : duration;
}

void xAnimFileEval(xAnimFile* data, F32 time, F32* bilinear, U32 flags, xVec3* tran, xQuat* quat,
                   F32* arg6)
{
    S32 i;
    U32 numBones;
    U32 fileflags;
    F32 bilerp[2];
    U32 biindex[2];
    U32 biplus[2];
    xQuat* q0;
    xVec3* t0;
    xQuat* q1;
    xVec3* t1;

    fileflags = data->FileFlags;
    time = xAnimFileRawTime(data, CLAMP(time, 0.0f, data->Duration));
    if (fileflags & 0x8000)
    {
        return;
    }

    if (flags & 0x1)
    {
        numBones = 1;
    }
    else
    {
        numBones = data->BoneCount;
    }

    if (flags & 0x2)
    {
        numBones--;
    }

    if (numBones == 0)
    {
        return;
    }

    if (bilinear != NULL && fileflags & 0x4000)
    {
        for (i = 0; i < 2; ++i)
        {
            F32 tmpf = CLAMP(bilinear[i], 0.0f, data->NumAnims[i] - 1);
            F32 t = std::floorf(tmpf);
            bilerp[i] = tmpf - t;
            biindex[i] = t;
            biplus[i] = (biindex[i] + 1 < data->NumAnims[i]) ? biindex[i] + 1 : biindex[i];
        }

        q0 = (xQuat*)(g_iAnim_workBuffer + 0x1560);
        t0 = (xVec3*)(q0 + IANIM_MAXBONES);
        if (bilerp[0] && bilerp[1])
        {
            q1 = (xQuat*)(g_iAnim_workBuffer + 0x1c80);
            t1 = (xVec3*)(q1 + IANIM_MAXBONES);

            iAnimEval(data->RawData[biindex[0] + biindex[1] * data->NumAnims[0]], time, flags, tran,
                      quat);
            iAnimEval(data->RawData[biplus[0] + biindex[1] * data->NumAnims[0]], time, flags, t0,
                      q0);
            iAnimBlend(bilerp[0], 1.0f, NULL, NULL, numBones, tran, quat, t0, q0, tran, quat);

            iAnimEval(data->RawData[biindex[0] + biplus[1] * data->NumAnims[0]], time, flags, t0,
                      q0);
            iAnimEval(data->RawData[biplus[0] + biplus[1] * data->NumAnims[0]], time, flags, t1,
                      q1);
            iAnimBlend(bilerp[0], 1.0f, NULL, NULL, numBones, t0, q0, t1, q1, t0, q0);
            iAnimBlend(bilerp[1], 1.0f, NULL, NULL, numBones, tran, quat, t0, q0, tran, quat);
        }
        else if (bilerp[0])
        {
            iAnimEval(data->RawData[biindex[0] + biindex[1] * data->NumAnims[0]], time, flags, tran,
                      quat);
            iAnimEval(data->RawData[biplus[0] + biindex[1] * data->NumAnims[0]], time, flags, t0,
                      q0);
            iAnimBlend(bilerp[0], 1.0f, NULL, NULL, numBones, tran, quat, t0, q0, tran, quat);
        }
        else if (bilerp[1])
        {
            iAnimEval(data->RawData[biindex[0] + biindex[1] * data->NumAnims[0]], time, flags, tran,
                      quat);
            iAnimEval(data->RawData[biindex[0] + biplus[1] * data->NumAnims[0]], time, flags, t0,
                      q0);
            iAnimBlend(bilerp[1], 1.0f, NULL, NULL, numBones, tran, quat, t0, q0, tran, quat);
        }
        else
        {
            iAnimEval(data->RawData[biindex[0] + biindex[1] * data->NumAnims[0]], time, flags, tran,
                      quat);
        }
    }
    else
    {
        iAnimEval(data->RawData[0], time, flags, tran, quat);
    }
}

xAnimEffect* xAnimStateNewEffect(xAnimState* state, U16 flags, U16 probability, F32 startTime,
                                 F32 endTime, xAnimEffectCallback callback, U32 userDataSize)
{
    xAnimEffect* curr;
    xAnimEffect** prev;
    xAnimEffect* effect;

    effect =
        (gxAnimUseGrowAlloc ? (xAnimEffect*)xMemGrowAllocSize(userDataSize + sizeof(xAnimEffect)) :
                              (xAnimEffect*)xMemAllocSize(userDataSize + sizeof(xAnimEffect)));

    effect->Flags = flags;
    effect->Probability = probability;
    effect->StartTime = startTime;
    effect->EndTime = endTime;
    effect->Callback = callback;

    prev = &state->Effects;
    curr = state->Effects;

    while (curr && startTime > curr->StartTime)
    {
        prev = &curr->Next;
        curr = curr->Next;
    }

    effect->Next = curr;
    *prev = effect;

    return effect;
}

xAnimTable* xAnimTableNew(const char* name, U32 userFlags)
{
    xAnimTable* table;

    table = (xAnimTable*)xMemAllocSize(sizeof(xAnimTable));

    table->Name = name;
    table->TransitionList = NULL;
    table->StateList = NULL;
    table->AnimIndex = 0;
    table->MorphIndex = 0;
    table->UserFlags = userFlags;

    return table;
}

void xAnimDefaultBeforeEnter(xAnimPlay* play, xAnimState* state, void* object)
{
    U32 entry;

    if (state->MultiFile)
    {
        entry = rand() % state->MultiFile->Count;

        state->Data = state->MultiFile->Files[entry].File;
    }
}

static inline F32 CalcRecipBlendMax(U16* timeOffs)
{
    float max = 0.0f;
    while (timeOffs[0] != 0xFFFF)
    {
        float f3;
        if (timeOffs[1] == 0)
        {
            f3 = 0.0f;
        }
        else
        {
            f3 = 1.0f / (timeOffs[1] / 1024.0f);
        }

        f3 = timeOffs[0] / 1024.0f + f3;
        if (f3 > max)
        {
            max = f3;
        }
        timeOffs += 2;
    }

    if (max == 0.0f)
    {
        return 0.0f;
    }
    return 1.0f / max;
}

xAnimState* xAnimTableNewState(xAnimTable* table, const char* name, U32 flags, U32 userFlags,
                               F32 speed, F32* boneBlend, F32* timeSnap, F32 fadeRecip,
                               U16* fadeOffset, void* callbackData,
                               xAnimStateBeforeEnterCallback beforeEnter,
                               xAnimStateCallback stateCallback,
                               xAnimStateBeforeAnimMatricesCallback beforeAnimMatrices)
{
    xAnimState* state;

    state = (gxAnimUseGrowAlloc ? (xAnimState*)xMemGrowAllocSize(sizeof(xAnimState)) :
                                  (xAnimState*)xMemAllocSize(sizeof(xAnimState)));

    if (!table->StateList)
    {
        state->Next = NULL;
        table->StateList = state;
    }
    else
    {
        state->Next = table->StateList->Next;
        table->StateList->Next = state;
    }

    state->Name = name;
    state->ID = xStrHash(name);
    state->Flags = flags;
    state->UserFlags = userFlags;
    state->Speed = speed;
    state->Data = NULL;
    state->Effects = NULL;
    state->Default = NULL;
    state->List = NULL;
    state->BoneBlend = boneBlend;
    state->TimeSnap = timeSnap;
    state->FadeRecip = (fadeOffset ? CalcRecipBlendMax(fadeOffset) : fadeRecip);
    state->FadeOffset = fadeOffset;
    state->MultiFile = NULL;
    state->CallbackData = callbackData;
    state->BeforeEnter = beforeEnter;
    state->StateCallback = stateCallback;
    state->BeforeAnimMatrices = beforeAnimMatrices;

    return state;
}

static inline bool _xCheckAnimName(const char* name, const char* pattern, char* extra)
{
    S32 patternSize, nameOut;
    U8 rc;

    *extra = 1;
    patternSize = strlen(pattern);

    rc = _xCheckAnimNameInner(name, pattern, patternSize, extra, &nameOut, NULL);

    return (rc && name[nameOut] == '\0');
}

static inline U32 DefaultHasTransition(xAnimState* state, xAnimTransition* tran, U32* allocCount)
{
    U32 needalloc = 1;
    xAnimTransitionList* curr = state->Default;

    while (curr)
    {
        if (curr->T == tran)
        {
            return TRUE;
        }

        if (curr->T->Conditional == tran->Conditional && curr->T->UserFlags == tran->UserFlags)
        {
            needalloc = 0;
        }

        curr = curr->Next;
    }

    *allocCount += needalloc;
    return FALSE;
}

static inline U32 StateHasTransition(xAnimState* state, xAnimTransition* tran)
{
    xAnimTransitionList* curr = state->List;

    while (curr)
    {
        if (curr->T == tran)
        {
            return TRUE;
        }

        curr = curr->Next;
    }

    return FALSE;
}

static inline void _xAnimTableAddTransitionHelper(xAnimState* state, xAnimTransition* tran,
                                                  U32& allocCount, U32& stateCount,
                                                  xAnimState** stateList)
{
    if (tran->Dest == state && (state->Flags & 0x70) != 0x40)
    {
        return;
    }

    if (tran->Flags & 0x10)
    {
        if (!DefaultHasTransition(state, tran, &allocCount))
        {
            stateList[stateCount] = state;
            stateCount++;
        }
    }
    else
    {
        if (!StateHasTransition(state, tran))
        {
            stateList[stateCount] = state;
            stateCount++;
            allocCount++;
        }
    }
}

static void _xAnimTableAddTransition(xAnimTable* table, xAnimTransition* tran, const char* source,
                                     const char* dest)
{
    U8* buffer;
    xAnimState** stateList;
    U32 i;
    U32 stateCount;
    U32 allocCount = 0;
    char* stateName;
    xAnimTransitionList* tlist = NULL;
    xAnimTransition* substTransitionList[32];
    U32 substTransitionCount = 0;
    U8 hasSubst;

    buffer = (U8*)xMemScratchPush(0x4000, 0);
    stateList = (xAnimState**)(buffer + 0x400);
    stateCount = 0;
    hasSubst = FALSE;

    if (dest != NULL)
    {
        for (S32 i = 0; dest[i] != '\0'; i++)
        {
            if (dest[i] == '@' || dest[i] == '~')
            {
                hasSubst = TRUE;
                break;
            }
        }
    }

    for (stateName = xStrTokBuffer(source, " ,\t\n\r", buffer); stateName != NULL;
         stateName = xStrTokBuffer(NULL, " ,\t\n\r", buffer))
    {
        bool isComplex = dest != NULL;

        if (!isComplex)
        {
            const char* COMPLEX_PATTERNS = "#+*?{}()<>|;";

            for (char* search = stateName; *search != '\0'; search++)
            {
                if (_xCharIn(*search, COMPLEX_PATTERNS))
                {
                    isComplex = true;
                    break;
                }
            }
        }

        if (isComplex)
        {
            for (xAnimState* state = table->StateList; state != NULL; state = state->Next)
            {
                char extra[128];
                char tempName[128];

                if (!_xCheckAnimName(state->Name, stateName, extra))
                {
                    continue;
                }

                if (hasSubst)
                {
                    char* tempIterator = tempName;
                    char* extraIterator = extra;
                    bool allowMissingState = false;

                    for (S32 i = 0; dest[i] != '\0'; i++)
                    {
                        if (dest[i] == '@' || dest[i] == '~')
                        {
                            allowMissingState = dest[i] == '~';
                            U32 extraIteratorLength = strlen(extraIterator);
                            strcpy(tempIterator, extraIterator);
                            tempIterator += extraIteratorLength;
                            extraIterator += extraIteratorLength + 1;
                        }
                        else
                        {
                            *tempIterator = dest[i];
                            tempIterator++;
                        }
                    }
                    *tempIterator = '\0';

                    xAnimState* tempState = xAnimTableGetState(table, tempName);
                    if (allowMissingState && tempState == NULL)
                    {
                        continue;
                    }

                    if (substTransitionCount != 0)
                    {
                        xAnimTransition* duplicatedTransition;
                        if (gxAnimUseGrowAlloc)
                        {
                            duplicatedTransition =
                                (xAnimTransition*)xMemGrowAllocSize(sizeof(xAnimTransition));
                        }
                        else
                        {
                            duplicatedTransition =
                                (xAnimTransition*)xMemAllocSize(sizeof(xAnimTransition));
                        }
                        memcpy(duplicatedTransition, tran, sizeof(xAnimTransition));
                        tran = duplicatedTransition;
                    }

                    tran->Dest = tempState;
                    substTransitionList[substTransitionCount++] = tran;
                }

                _xAnimTableAddTransitionHelper(state, tran, allocCount, stateCount, stateList);
            }
        }
        else
        {
            xAnimState* state = xAnimTableGetState(table, stateName);
            if (state != NULL)
            {
                _xAnimTableAddTransitionHelper(state, tran, allocCount, stateCount, stateList);
            }
        }
    }

    if (allocCount != 0)
    {
        tlist =
            (xAnimTransitionList*)(gxAnimUseGrowAlloc ?
                                       xMemGrowAllocSize(allocCount * sizeof(xAnimTransitionList)) :
                                       xMemAllocSize(allocCount * sizeof(xAnimTransitionList)));
    }

    if (tran->Flags & 0x10)
    {
        for (i = 0; i < stateCount; i++)
        {
            xAnimTransition* t = hasSubst ? substTransitionList[i] : tran;
            if (!DefaultOverride(stateList[i], t))
            {
                tlist->T = t;
                _xAnimTransitionListInsert(&stateList[i]->Default, tlist);
                tlist++;
            }
        }
    }
    else
    {
        for (i = 0; i < stateCount; i++)
        {
            tlist->T = hasSubst ? substTransitionList[i] : tran;
            _xAnimTransitionListInsert(&stateList[i]->List, tlist);
            tlist++;
        }
    }

    xMemScratchPop(buffer);
}

void xAnimTableAddTransition(xAnimTable* table, xAnimTransition* tran, const char* source)
{
    _xAnimTableAddTransition(table, tran, source, NULL);
}

xAnimTransition* xAnimTableNewTransition(xAnimTable* table, const char* source, const char* dest,
                                         xAnimTransitionConditionalCallback conditional,
                                         xAnimTransitionCallback callback, U32 flags, U32 userFlags,
                                         F32 srcTime, F32 destTime, U16 priority, U16 queuePriority,
                                         F32 fBlendTime, U16* blendOffset)
{
    xAnimTransition* tran;

    tran = (gxAnimUseGrowAlloc ? (xAnimTransition*)xMemGrowAllocSize(sizeof(xAnimTransition)) :
                                 (xAnimTransition*)xMemAllocSize(sizeof(xAnimTransition)));

    fBlendTime = (fBlendTime == 0.0f) ? 0.0f : 1.0f / fBlendTime;

    tran->Next = table->TransitionList;
    table->TransitionList = tran;

    U8 isComplex = FALSE;
    if (dest == NULL || *dest == NULL)
    {
        tran->Dest = NULL;
    }
    else
    {
        for (S32 i = 0; dest[i] != NULL; ++i)
        {
            if (dest[i] == '@' || dest[i] == '~')
            {
                isComplex = true;
                break;
            }
        }
        tran->Dest = isComplex ? NULL : xAnimTableGetState(table, dest);
    }

    tran->Conditional = conditional;
    tran->Callback = callback;
    tran->Flags = flags;
    tran->UserFlags = userFlags;
    tran->SrcTime = srcTime;
    tran->DestTime = destTime;
    tran->Priority = priority;
    tran->QueuePriority = queuePriority;

    tran->BlendRecip = blendOffset != NULL ? CalcRecipBlendMax(blendOffset) : fBlendTime;
    tran->BlendOffset = blendOffset;

    _xAnimTableAddTransition(table, tran, source, isComplex ? dest : NULL);

    return tran;
}

void xAnimTableAddFile(xAnimTable* table, xAnimFile* file, const char* states)
{
    U8* buffer = (U8*)xMemScratchPush(0x4000, 0);
    char* stateName = xStrTokBuffer(states, " ,\t\n\r", buffer);

    while (stateName != NULL)
    {
        xAnimTableAddFileID(table, file, xStrHash(stateName), 0, 0);
        stateName = xStrTokBuffer(NULL, " ,\t\n\r", buffer);
    }

    xMemScratchPop(buffer);
}

xAnimState* xAnimTableAddFileID(xAnimTable* table, xAnimFile* file, U32 stateID, U32 subStateID,
                                U32 subStateCount)
{
    xAnimState* state;

    state = xAnimTableGetStateID(table, stateID);

    if (state)
    {
        if (subStateID)
        {
            if (!state->MultiFile)
            {
                state->MultiFile =
                    (gxAnimUseGrowAlloc ? (xAnimMultiFile*)xMemGrowAllocSize(
                                              subStateCount * sizeof(xAnimMultiFileEntry) +
                                              sizeof(xAnimMultiFileBase)) :
                                          (xAnimMultiFile*)xMemAllocSize(
                                              subStateCount * sizeof(xAnimMultiFileEntry) +
                                              sizeof(xAnimMultiFileBase)));

                state->MultiFile->Count = 0;
            }

            U32 current = state->MultiFile->Count;

            state->MultiFile->Files[current].ID = subStateID;
            state->MultiFile->Files[current].File = file;

            state->MultiFile->Count++;
        }

        state->Data = file;

        if (file->FileFlags & 0x8000)
        {
            table->MorphIndex |= (1 << (state->Flags & 0xF));
        }
        else
        {
            table->AnimIndex |= (1 << (state->Flags & 0xF));
        }
    }

    return state;
}

xAnimState* xAnimTableGetStateID(xAnimTable* table, U32 ID)
{
    xAnimState* curr;

    curr = table->StateList;

    while (curr)
    {
        if (curr->ID == ID)
        {
            return curr;
        }

        curr = curr->Next;
    }

    return NULL;
}

xAnimState* xAnimTableGetState(xAnimTable* table, const char* name)
{
    return xAnimTableGetStateID(table, xStrHash(name));
}

static inline void EffectActiveInsert(xAnimSingle* single, xAnimActiveEffect* active)
{
    U32 index = 0;
    U32 count = single->ActiveCount;
    xAnimActiveEffect* curr = &single->ActiveList[0];

    while (index < count && curr->Effect)
    {
        curr++;
        index++;
    }

    *curr = *active;

    if (index < count - 1)
    {
        (curr + 1)->Effect = NULL;
    }
}

static void EffectSingleStart(xAnimSingle* single)
{
    xAnimEffect* effect = single->State->Effects;
    xAnimActiveEffect tempActive;
    F32 time = single->Time;
    U32 flags;

    while (effect && effect->StartTime < time)
    {
        flags = effect->Flags;

        if (flags & 0x1)
        {
            if (flags & 0x4)
            {
                tempActive.Effect = effect;
                tempActive.Handle = effect->Callback(1, &tempActive, single, single->Play->Object);

                flags = effect->Flags;

                if (flags & 0x2)
                {
                    if (effect->EndTime <= time)
                    {
                        effect->Callback(3, &tempActive, single, single->Play->Object);
                    }
                    else
                    {
                        EffectActiveInsert(single, &tempActive);
                    }
                }
                else if (flags & 0x20)
                {
                    EffectActiveInsert(single, &tempActive);
                }
            }
            else if ((flags & 0x2) && !(flags & 0x10) && effect->EndTime > time)
            {
                tempActive.Effect = effect;
                tempActive.Handle = effect->Callback(1, &tempActive, single, single->Play->Object);

                EffectActiveInsert(single, &tempActive);
            }
        }

        effect = effect->Next;
    }

    single->Effect = effect;
}

static inline U32 EffectPlaying(xAnimSingle* single, xAnimEffect* effect)
{
    U32 index = 0;

    while (index < single->ActiveCount && single->ActiveList[index].Effect)
    {
        if (single->ActiveList[index].Effect == effect)
        {
            return TRUE;
        }

        index++;
    }

    return FALSE;
}

static void EffectSingleRun(xAnimSingle* single)
{
    xAnimEffect* effect = single->Effect;
    xAnimActiveEffect tempActive;
    F32 time = single->Time;
    U32 flags;

    while (effect && effect->StartTime <= time)
    {
        flags = effect->Flags;

        if ((flags & 0x1) && (!(flags & 0x20) || !EffectPlaying(single, effect)))
        {
            tempActive.Effect = effect;
            tempActive.Handle = effect->Callback(1, &tempActive, single, single->Play->Object);

            if (flags & 0x2)
            {
                if (effect->EndTime <= time)
                {
                    effect->Callback(3, &tempActive, single, single->Play->Object);
                }
                else
                {
                    EffectActiveInsert(single, &tempActive);
                }
            }
            else if (flags & 0x20)
            {
                EffectActiveInsert(single, &tempActive);
            }
        }

        effect = effect->Next;
    }

    single->Effect = effect;
}

static inline void EffectActiveRemove(xAnimActiveEffect* active, U32 index, U32 count)
{
    count--;

    if (index == count || !(active + 1)->Effect)
    {
        active->Effect = NULL;
    }
    else
    {
        xAnimActiveEffect* curr = active;

        while (index < count && (curr + 1)->Effect)
        {
            curr++;
            index++;
        }

        *active = *curr;
        curr->Effect = NULL;
    }
}

static void EffectSingleLoop(xAnimSingle* single)
{
    EffectSingleRun(single);
    xAnimActiveEffect* alist = single->ActiveList;
    U32 index = 0;
    U32 count = single->ActiveCount;
    while (index < count && alist->Effect != NULL)
    {
        if (!(alist->Effect->Flags & 0x20))
        {
            alist->Effect->Callback(3, alist, single, single->Play->Object);

            EffectActiveRemove(alist, index, count);
        }
        else
        {
            alist++;
            index++;
        }
    }

    xAnimEffect* effect = single->State->Effects;
    while (effect != NULL && effect->StartTime < 0.0f)
    {
        effect = effect->Next;
    }
    single->Effect = effect;
}

void xAnimPhysicsEval(xAnimPhysicsData* phys, F32 time, xVec3* disp, F32* yaw)
{
    F32 interp;
    S32 index;

    if (phys == NULL)
    {
        return;
    }

    if (time < 0.0f)
    {
        time = 0.0f;
    }

    interp = time * 30.0f;
    index = interp;

    if (index >= phys->tranCount - 1)
    {
        index = phys->tranCount - 1;
        xVec3Copy(disp, &phys->tranTable[index]);

        if (phys->yawTable != NULL)
        {
            *yaw = phys->yawTable[index];
        }
        else
        {
            *yaw = 0.0f;
        }
    }
    else
    {
        interp -= index;

        xVec3SMul(disp, &phys->tranTable[index + 1], interp);
        xVec3AddScaled(disp, &phys->tranTable[index], 1.0f - interp);

        if (phys->yawTable != NULL)
        {
            *yaw = phys->yawTable[index] +
                   interp * (phys->yawTable[index + 1] - phys->yawTable[index]);
        }
        else
        {
            *yaw = 0.0f;
        }
    }
}

static void LoopUpdate(xAnimSingle* single)
{
    F32 time = single->Time;
    F32 duration = single->State->Data->Duration;

    if (time > duration)
    {
        time -= duration;
        single->Time = duration;

        EffectSingleLoop(single);

        xAnimFile* data = single->State->Data;
        if (data->FileFlags & 0xF0000)
        {
            xVec3 disp;
            F32 yaw;

            xAnimPhysicsEval(data->PhysicsData, single->Time, &disp, &yaw);

            xVec3AddTo(&single->PhysDisp, &disp);
            if (yaw != 0.0f)
            {
                xMat3x3 rot;
                xMat3x3RotY(&rot, -yaw);
                xMat3x3RMulVec(&single->PhysDisp, &rot, &single->PhysDisp);
            }
            single->YawDisp += yaw;
            single->SingleFlags |= 0x4;
        }

        single->Time = time;
    }
}

static inline void EffectSingleStop(xAnimSingle* single)
{
    if (single->State == NULL || single->LastTime == -1.0f)
    {
        return;
    }

    for (U32 i = 0; single->ActiveCount > i && single->ActiveList[i].Effect != NULL; ++i)
    {
        if (single->ActiveList[i].Effect->Flags & 0x22)
        {
            single->ActiveList[i].Effect->Callback(3, &single->ActiveList[i], single,
                                                   single->Play->Object);
        }
    }

    single->ActiveList->Effect = NULL;
    for (xAnimEffect* effect = single->Effect; effect != NULL; effect = effect->Next)
    {
        xAnimActiveEffect tempActive;
        if ((effect->Flags & 0x9) == 0x9)
        {
            tempActive.Effect = effect;
            tempActive.Handle = effect->Callback(1, &tempActive, single, single->Play->Object);

            if (effect->Flags & 0x2)
            {
                effect->Callback(3, &tempActive, single, single->Play->Object);
            }
        }
    }

    single->Effect = NULL;
}

void xAnimPlaySetState(xAnimSingle* single, xAnimState* state, F32 startTime)
{
    EffectSingleStop(single);
    if (single->Blend)
    {
        EffectSingleStop(single->Blend);
        SetState(single->Blend->State, NULL);
    }

    SetState(single->State, state);
    if (state == NULL)
    {
        return;
    }

    single->Time =
        (state->Flags & 0x100 && startTime == 0.0f) ? state->Data->Duration * xurand() : startTime;
    single->CurrentSpeed = state->Speed;
    single->BilinearLerp[0] = 0.0f;
    single->BilinearLerp[1] = 0.0f;
    single->Effect = NULL;
    memset(single->ActiveList, 0, single->ActiveCount * sizeof(xAnimActiveEffect));
    single->LastTime = -1.0f;

    SetTransition(single->Sync, NULL);
    SetTransition(single->Tran, NULL);
    single->BlendFactor = 0.0f;
}

static inline void StopUpdate(xAnimSingle* single)
{
    if (single->Time > single->State->Data->Duration)
    {
        single->Time = single->State->Data->Duration;
        single->CurrentSpeed = 0.0f;
    }
}

static inline void EffectSingleDuration(xAnimSingle* single)
{
    F32 time = single->Time;
    xAnimActiveEffect* alist = single->ActiveList;
    U32 index = 0;
    U32 count = single->ActiveCount;

    while (index < count && alist->Effect)
    {
        if (!(alist->Effect->Flags & 0x20) && alist->Effect->EndTime <= time)
        {
            alist->Effect->Callback(3, alist, single, single->Play->Object);

            EffectActiveRemove(alist, index, count);
        }
        else
        {
            alist->Effect->Callback(2, alist, single, single->Play->Object);

            alist++;
            index++;
        }
    }
}

static void SingleUpdate(xAnimSingle* single, F32 timeDelta)
{
    U32 foundBlendstop = 0;
    U32 firstStep = 1;
    xAnimTransition* foundTransition = NULL;
    xAnimSingle* bl = NULL;
    F32 tranDelta = 0.0f;
    F32 blendDelta = 0.0f;
    F32 singleTime;
    void* object;
    F32 duration;

    single->PhysDisp.x = 0.0f;
    single->PhysDisp.y = 0.0f;
    single->PhysDisp.z = 0.0f;
    single->YawDisp = 0.0f;
    single->SingleFlags &= ~0x4;

    if (single->State == NULL)
    {
        return;
    }

    object = single->Play->Object;
    if (single->LastTime == -1.0f)
    {
        EffectSingleStart(single);
    }

    single->LastTime = single->Time;
    singleTime = timeDelta * single->CurrentSpeed + single->Time;

    if (single->State->Data->FileFlags & 0xF0000)
    {
        xAnimPhysicsEval(single->State->Data->PhysicsData, single->LastTime, &single->PhysDisp,
                         &single->YawDisp);
        single->PhysDisp.x = -single->PhysDisp.x;
        single->PhysDisp.y = -single->PhysDisp.y;
        single->PhysDisp.z = -single->PhysDisp.z;
        single->YawDisp = -single->YawDisp;
    }

    if (single->Blend != NULL && single->Blend->State != NULL)
    {
        bl = single->Blend;
        if (bl->LastTime == -1.0f)
        {
            EffectSingleStart(bl);
        }
        bl->LastTime = bl->Time;
    }

    duration = single->State->Data->Duration;
    if (single->Sync != NULL)
    {
        F32 timeCmp;
        if ((timeCmp = single->Sync->SrcTime) != 0.0f)
        {
            if (timeCmp > duration)
            {
                timeCmp = duration;
            }
            if (single->LastTime <= timeCmp && singleTime >= timeCmp)
            {
                foundTransition = single->Sync;
                tranDelta = (singleTime - timeCmp) / single->CurrentSpeed;
                timeDelta = timeDelta - tranDelta;
                if (timeDelta < 0.0f)
                {
                    timeDelta = 0.0f;
                }
            }
            else
            {
                timeCmp += duration;
                if (single->LastTime <= timeCmp && singleTime >= timeCmp)
                {
                    foundTransition = single->Sync;
                    tranDelta = (singleTime - timeCmp) / single->CurrentSpeed;
                    timeDelta = timeDelta - tranDelta;
                    if (timeDelta < 0.0f)
                    {
                        timeDelta = 0.0f;
                    }
                }
            }
        }
        else if (bl == NULL)
        {
            foundTransition = single->Sync;
            tranDelta = timeDelta;
            timeDelta = 0.0f;
        }
    }
    else
    {
        if ((single->State->Flags & 0x70) == 0x20 || (single->State->Flags & 0x70) == 0x40)
        {
            F32 timeCmp = single->State->Default->T->SrcTime;

            if (timeCmp == 0.0f || timeCmp > duration)
            {
                timeCmp = duration;
            }

            if (singleTime >= timeCmp &&
                ((single->State->Default->T->Flags & 0x4) == 0 || bl == NULL))
            {
                xAnimTransitionList* curr = single->State->Default;
                while (curr != NULL && curr->T->Conditional != NULL &&
                       curr->T->Conditional(curr->T, single, object) == 0)
                {
                    curr = curr->Next;
                }

                if (curr == NULL)
                {
                    curr = single->State->Default;
                }

                foundTransition = curr->T;
                if (single->LastTime < timeCmp)
                {
                    tranDelta = (singleTime - timeCmp) / single->CurrentSpeed;
                    timeDelta = timeDelta - tranDelta;
                    if (timeDelta < 0.0f)
                    {
                        timeDelta = 0.0f;
                    }
                }
                else
                {
                    tranDelta = timeDelta;
                    timeDelta = 0.0f;
                }
            }
        }
    }

    if (single->BlendFactor != 0.0f)
    {
        F32 recip;
        if (single->Tran != NULL)
        {
            recip = single->Tran->BlendRecip;
        }
        else
        {
            recip = single->State->FadeRecip;
        }

        if (recip * (single->BlendFactor + timeDelta) > 1.0f)
        {
            foundBlendstop = 1;
            blendDelta = (single->BlendFactor + timeDelta) - (1.0f / recip);
            timeDelta = timeDelta - blendDelta;
            if (timeDelta < 0.0f)
            {
                timeDelta = 0.0f;
            }
            if (blendDelta < 0.0f)
            {
                blendDelta = 0.01f;
            }
        }
    }

    do
    {
        if (!firstStep)
        {
            if (foundBlendstop)
            {
                single->BlendFactor = 0.0f;
                if (single->Tran->Flags & 0x8)
                {
                    single->CurrentSpeed = single->State->Speed;
                }

                if (bl != NULL)
                {
                    EffectSingleStop(bl);
                    SetState(bl->State, NULL);
                    bl = NULL;
                    SetTransition(single->Tran, NULL);
                }
                else
                {
                    if (single->Tran != NULL)
                    {
                        SetTransition(single->Tran, NULL);
                    }
                    else
                    {
                        EffectSingleStop(single);
                        SetState(single->State, NULL);
                        return;
                    }
                }
                foundBlendstop = FALSE;
                timeDelta = blendDelta;
            }
            else
            {
                U8 transferredState = FALSE;
                if (bl != NULL)
                {
                    EffectSingleStop(bl);
                    SetState(bl->State, NULL);
                    bl = NULL;
                    single->BlendFactor = 0.0f;
                }

                if (foundTransition->BlendRecip == 0.0f || single->Blend == NULL)
                {
                    EffectSingleStop(single);
                    SetTransition(single->Tran, NULL);
                }
                else
                {
                    bl = single->Blend;
                    transferredState = TRUE;
                    bl->State = single->State;
                    bl->Time = single->Time;
                    bl->CurrentSpeed = single->CurrentSpeed;
                    bl->BilinearLerp[0] = single->BilinearLerp[0];
                    bl->BilinearLerp[1] = single->BilinearLerp[1];
                    bl->Effect = single->Effect;
                    bl->LastTime = single->LastTime;
                    memcpy(bl->ActiveList, single->ActiveList,
                           single->ActiveCount * sizeof(xAnimActiveEffect));
                    single->ActiveList->Effect = NULL;

                    SetTransition(single->Tran, foundTransition);
                    single->BlendFactor = 0.0000001f;
                }

                if (single->State->Data->FileFlags & 0xF0000)
                {
                    xVec3 disp;
                    F32 yaw;

                    xAnimPhysicsEval(single->State->Data->PhysicsData, single->Time, &disp, &yaw);

                    xVec3AddTo(&single->PhysDisp, &disp);
                    if (yaw != 0.0f)
                    {
                        xMat3x3 rot;
                        xMat3x3RotY(&rot, -yaw);
                        xMat3x3RMulVec(&single->PhysDisp, &rot, &single->PhysDisp);
                    }
                    single->YawDisp += yaw;
                    single->SingleFlags |= 0x4;
                }

                TransitionTimeInit(single, foundTransition);
                if (transferredState)
                {
                    single->State = NULL;
                }
                SetState(single->State, foundTransition->Dest);
                single->CurrentSpeed = single->State->Speed;
                single->BilinearLerp[0] = 0.0f;
                single->BilinearLerp[1] = 0.0f;
                SetTransition(single->Sync, NULL);
                if (transferredState && foundTransition->Flags & 0x8)
                {
                    single->CurrentSpeed = 0.0f;
                }
                EffectSingleStart(single);

                if (single->State->BeforeEnter != NULL)
                {
                    single->State->BeforeEnter(single->Play, single->State, single->Play->Object);
                }
                if (foundTransition->Callback != NULL)
                {
                    foundTransition->Callback(foundTransition, single, single->Play->Object);
                }
                timeDelta = tranDelta;
                foundTransition = NULL;
            }
        }

        single->Time = timeDelta * single->CurrentSpeed + single->Time;
        if (single->BlendFactor != 0.0f)
        {
            single->BlendFactor += timeDelta;
        }
        if ((single->State->Flags & 0x70) == 0x10)
        {
            LoopUpdate(single);
        }
        else
        {
            StopUpdate(single);
        }
        EffectSingleRun(single);

        if (bl != NULL)
        {
            if ((bl->State->Flags & 0x70) == 0x10)
            {
                LoopUpdate(bl);
            }
            else
            {
                StopUpdate(bl);
            }
            EffectSingleRun(bl);
        }

        firstStep = FALSE;
    } while (foundBlendstop || foundTransition != NULL);

    if (single->Tran == NULL && single->BlendFactor == 0.0f)
    {
        if ((single->State->Flags & 0x70) == 0x30)
        {
            if (single->State->Flags & 0x200)
            {
                if (single->Time >= duration)
                {
                    single->BlendFactor = 0.0000001f;
                }
            }
            else if (single->State->FadeRecip * (duration - single->Time) < 1.0f)
            {
                single->BlendFactor = 0.0000001f;
            }
        }
    }
    EffectSingleDuration(single);
    if (bl != NULL)
    {
        EffectSingleDuration(bl);
    }

    if (single->State->Data->FileFlags & 0xF0000)
    {
        xVec3 disp;
        F32 yaw;

        xAnimPhysicsEval(single->State->Data->PhysicsData, single->Time, &disp, &yaw);

        xVec3AddTo(&single->PhysDisp, &disp);
        if (yaw != 0.0f)
        {
            xMat3x3 rot;
            xMat3x3RotY(&rot, -yaw);
            xMat3x3RMulVec(&single->PhysDisp, &rot, &single->PhysDisp);
        }
        single->YawDisp += yaw;
        single->SingleFlags |= 0x4;
    }
}

void xAnimPlaySetup(xAnimPlay* play, void* object, xAnimTable* table, xModelInstance* modelInst)
{
    S32 i;

    play->BoneCount = modelInst->BoneCount;
    play->Object = object;
    play->Table = table;
    play->ModelInst = modelInst;

    modelInst->Anim = play;
    modelInst->Flags |= 0x104;

    if (table->MorphIndex != 0)
    {
        modelInst->Flags |= 0x80;
    }

    for (i = 0; i < play->NumSingle; i++)
    {
        play->Single[i].SingleFlags = (1 << i & table->MorphIndex) ? 0x8000 : 0x1;
        play->Single[i].State = NULL;
        play->Single[i].Tran = NULL;
        if (play->Single[i].Blend != NULL)
        {
            SetState(play->Single[i].Blend->State, NULL);
        }
    }

    xAnimPlaySetState(play->Single, table->StateList, 0.0f);
}

void xAnimPlayChooseTransition(xAnimPlay* play)
{
    U32 i;
    void* object = play->Object;
    xAnimTransition** found;
    xAnimTransitionList* curr;

    static U8* tmpBuffer = g_iAnim_workBuffer;

    found = (xAnimTransition**)tmpBuffer;
    memset(found, 0, play->NumSingle * sizeof(xAnimTransition*));

    for (i = 0; i < play->NumSingle; i++)
    {
        if (play->Single[i].State == NULL)
        {
            continue;
        }

        curr = play->Single[i].State->List;
        if (curr == NULL || curr->T->Conditional == NULL)
        {
            continue;
        }

        while (curr != NULL && (play->Single[i].Sync == NULL ||
                                curr->T->Priority > play->Single[i].Sync->QueuePriority))
        {
            if (curr->T->Conditional(curr->T, &play->Single[i], object))
            {
                found[curr->T->Dest->Flags & 0xF] = curr->T;
                break;
            }

            curr = curr->Next;
        }
    }

    for (i = 0; i < play->NumSingle; i++)
    {
        if (found[i] != NULL)
        {
            xAnimPlayStartTransition(play, found[i]);
        }
    }
}

void xAnimPlayStartTransition(xAnimPlay* play, xAnimTransition* transition)
{
    xAnimSingle* single = &play->Single[transition->Dest->Flags & 0xf];
    xAnimSingle* bl = single->Blend;
    U8 transferredState;
    U8 tookOwnership;

    if (transition->SrcTime != 0.0f || (transition->Flags & 0x4 && bl != NULL && bl->State != NULL))
    {
        SetTransition(single->Sync, transition);
        return;
    }

    if (bl != NULL && bl->State != NULL)
    {
        EffectSingleStop(bl);
        SetState(bl->State, NULL);
        single->BlendFactor = 0.0f;
    }

    transferredState = FALSE;
    tookOwnership = FALSE;

    if (transition->BlendRecip == 0.0f || single->Blend == NULL)
    {
        EffectSingleStop(single);
        SetTransition(single->Tran, NULL);
    }
    else
    {
        if (single->State != NULL)
        {
            bl->State = single->State;
            transferredState = TRUE;
            bl->Time = single->Time;
            bl->CurrentSpeed = single->CurrentSpeed;
            bl->BilinearLerp[0] = single->BilinearLerp[0];
            bl->BilinearLerp[1] = single->BilinearLerp[1];
            bl->Effect = single->Effect;
            bl->LastTime = single->LastTime;
            memcpy(bl->ActiveList, single->ActiveList,
                   single->ActiveCount * sizeof(xAnimActiveEffect));
            single->ActiveList[0].Effect = NULL;
        }

        SetTransition(single->Tran, transition);
        tookOwnership = TRUE;
        single->BlendFactor = 0.0000001f;
    }

    TransitionTimeInit(single, transition);

    if (transferredState)
    {
        single->State = NULL;
    }

    SetState(single->State, transition->Dest);
    single->CurrentSpeed = single->State->Speed;
    single->BilinearLerp[0] = 0.0f;
    single->BilinearLerp[1] = 0.0f;
    SetTransition(single->Sync, NULL);

    if (transferredState && transition->Flags & 0x8)
    {
        single->CurrentSpeed = 0.0f;
    }

    EffectSingleStart(single);

    if (single->State->BeforeEnter != NULL)
    {
        single->State->BeforeEnter(play, single->State, play->Object);
    }

    if (transition->Callback != NULL)
    {
        transition->Callback(transition, single, single->Play->Object);
    }

    if (!tookOwnership)
    {
        SetTransition(transition, NULL);
    }
}

void xAnimPlayUpdate(xAnimPlay* play, F32 timeDelta)
{
    U32 i;
    xAnimSingle* single;

    for (i = 0; i < play->NumSingle; i++)
    {
        single = &play->Single[i];

        SingleUpdate(single, timeDelta);

        if (single->State && single->State->StateCallback)
        {
            single->State->StateCallback(single->State, single, play->Object);
        }
    }
}

static inline void SingleEval(xAnimSingle* single, xVec3* tran, xQuat* quat)
{
    if (single->State != NULL && single->Play->BoneCount > 1)
    {
        xAnimFileEval(single->State->Data, single->Time, single->BilinearLerp, 0x2, tran + 1,
                      quat + 1, NULL);

        if (single->Blend && single->Blend->State)
        {
            xQuat* blendquat = (xQuat*)(g_iAnim_workBuffer + 0xE40);
            xVec3* blendtran = (xVec3*)(blendquat + 65);

            xAnimFileEval(single->Blend->State->Data, single->Blend->Time,
                          single->Blend->BilinearLerp, 0x2, blendtran, blendquat, NULL);

            iAnimBlend(single->BlendFactor, single->Tran->BlendRecip, single->Tran->BlendOffset,
                       NULL, single->Play->BoneCount - 1, blendtran, blendquat, tran + 1, quat + 1,
                       tran + 1, quat + 1);
        }
    }
}

void xAnimPlayEval(xAnimPlay* play)
{
    U32 i;
    U32 bone;
    xQuat* quatresult;
    xVec3* tranresult;

    if (play->Single->State == NULL)
    {
        return;
    }

    quatresult = (xQuat*)g_iAnim_workBuffer;
    tranresult = (xVec3*)(quatresult + 65);

    if (play->BoneCount > 1)
    {
        xQuat* quatblend = quatresult + 114;
        xVec3* tranblend = (xVec3*)(quatblend + 65);
        SingleEval(play->Single, tranresult, (xQuat*)g_iAnim_workBuffer);
        for (i = 1; i < play->NumSingle; ++i)
        {
            xAnimSingle* si = &play->Single[i];
            if (si->State == NULL || si->SingleFlags & 0x8000)
            {
                continue;
            }
            F32 blendF = 1.0f;
            F32 blendR = 1.0f;
            U16* blendO = NULL;
            SingleEval(si, tranblend, quatblend);

            if ((si->Blend == NULL || si->Blend->State == NULL) && si->BlendFactor)
            {
                if (si->Tran != NULL)
                {
                    blendF = si->BlendFactor;
                    blendR = si->Tran->BlendRecip;
                    blendO = si->Tran->BlendOffset;
                }
                else
                {
                    blendF = -si->BlendFactor;
                    blendR = si->State->FadeRecip;
                    blendO = si->State->FadeOffset;
                }
            }

            if ((si->SingleFlags & 0x3) == 2)
            {
                iAnimBlend(blendF, blendR, blendO, si->State->BoneBlend, play->BoneCount - 1,
                           tranblend + 1, quatblend + 1, NULL, NULL, tranblend + 1, quatblend + 1);

                for (bone = 1; bone < play->BoneCount; ++bone)
                {
                    tranresult[bone].x += tranblend[bone].x;
                    tranresult[bone].y += tranblend[bone].y;
                    tranresult[bone].z += tranblend[bone].z;
                    xQuatMul(&quatresult[bone], &quatresult[bone], &quatblend[bone]);
                }
            }
            else
            {
                iAnimBlend(blendF, blendR, blendO, si->State->BoneBlend, play->BoneCount - 1,
                           tranresult + 1, quatresult + 1, tranblend + 1, quatblend + 1,
                           tranresult + 1, quatresult + 1);
            }
        }
    }
    memset(tranresult, 0, sizeof(xVec3));
    memset(quatresult, 0, sizeof(xQuat));
    if (play->Single->State->BeforeAnimMatrices != NULL)
    {
        play->Single->State->BeforeAnimMatrices(play, quatresult, tranresult, play->BoneCount);
    }

    if (play->BeforeAnimMatrices != NULL)
    {
        play->BeforeAnimMatrices(play, quatresult, tranresult, play->BoneCount);
    }
    iModelAnimMatrices(play->ModelInst->Data, quatresult, tranresult, &play->ModelInst->Mat[1]);
}

void xAnimPoolCB(xMemPool* pool, void* data)
{
    S32 i;
    xAnimPlay* clone = (xAnimPlay*)data;
    xAnimPlay* play = (xAnimPlay*)pool->Buffer;
    xAnimSingle* clonesingle = (xAnimSingle*)(clone + 1);
    xAnimSingle* currsingle;
    xAnimActiveEffect* curract;

    clone->NumSingle = play->NumSingle;
    clone->Single = clonesingle;

    clonesingle += play->NumSingle;

    for (i = 0; i < play->NumSingle; i++)
    {
        clone->Single[i].Play = clone;

        if (play->Single[i].Blend)
        {
            clone->Single[i].Blend = clonesingle;

            clonesingle->Blend = NULL;
            clonesingle->Play = clone;

            clonesingle++;
        }
        else
        {
            clone->Single[i].Blend = NULL;
        }
    }

    curract = (xAnimActiveEffect*)clonesingle;

    for (i = 0; i < clone->NumSingle; i++)
    {
        clonesingle = &clone->Single[i];
        currsingle = &play->Single[i];

        while (clonesingle)
        {
            clonesingle->ActiveCount = currsingle->ActiveCount;

            if (currsingle->ActiveCount)
            {
                clonesingle->ActiveList = curract;

                curract += currsingle->ActiveCount;
            }
            else
            {
                clonesingle->ActiveList = NULL;
            }

            currsingle = currsingle->Blend;
            clonesingle = clonesingle->Blend;
        }
    }

    clone->Pool = pool;
}

void xAnimPoolInit(xMemPool* pool, U32 count, U32 singles, U32 blendFlags, U32 effectMax)
{
    effectMax += effectMax & 1;

    U32 size =
        (effectMax * sizeof(xAnimActiveEffect) + sizeof(xAnimSingle)) *
            (ADD_4_BITS((blendFlags & 0xffff) & ((int)(1 << singles) - 1 >> 0x0)) +
             ADD_4_BITS((blendFlags & 0xffff) & ((int)(1 << singles) - 1 >> 0x4)) +
             ADD_4_BITS((blendFlags & 0xffff) & ((int)(1 << singles) - 1 >> 0x8)) +
             ADD_4_BITS((blendFlags & 0xffff) & ((int)(1 << singles) - 1 >> 0xC)) + singles) +
        sizeof(xAnimPlay);

    U32 i;
    void* buffer = xMemAllocSize(count * size);

    xAnimPlay* play = (xAnimPlay*)buffer;
    play->NumSingle = singles;

    xAnimSingle* currsingle;
    play->Single = currsingle = (xAnimSingle*)(play + 1);
    currsingle += singles;

    for (i = 0; i < singles; ++i)
    {
        if (blendFlags & (1 << i))
        {
            play->Single[i].Blend = currsingle;
            currsingle->Blend = NULL;
            currsingle++;
        }
        else
        {
            play->Single[i].Blend = NULL;
        }
    }

    xAnimActiveEffect* curract = (xAnimActiveEffect*)currsingle;
    for (i = 0; i < play->NumSingle; ++i)
    {
        currsingle = &play->Single[i];
        while (currsingle)
        {
            currsingle->ActiveCount = effectMax;
            if (effectMax != 0)
            {
                currsingle->ActiveList = curract;
                curract += effectMax;
            }
            else
            {
                currsingle->ActiveList = NULL;
            }

            currsingle = currsingle->Blend;
        }
    }

    play->Pool = pool;
    xMemPoolSetup(pool, buffer, 0, 1, xAnimPoolCB, size, count, count / 2);
}

xAnimPlay* xAnimPoolAlloc(xMemPool* pool, void* object, xAnimTable* table,
                          xModelInstance* modelInst)
{
    xAnimPlay* play;

    play = (xAnimPlay*)xMemPoolAlloc(pool);

    xAnimPlaySetup(play, object, table, modelInst);

    return play;
}

void xAnimPoolFree(xAnimPlay* play)
{
    U32 i;

    for (i = 0; i < play->NumSingle; i++)
    {
        EffectSingleStop(&play->Single[i]);

        if (play->Single[i].Blend)
        {
            EffectSingleStop(play->Single[i].Blend);
        }
    }

    if (play->Single->Tran != NULL && play->Single->Tran->Flags & 0x2)
    {
        SetTransition(play->Single->Tran, NULL);
    }

    xMemPoolFree(play->Pool, play);
}
