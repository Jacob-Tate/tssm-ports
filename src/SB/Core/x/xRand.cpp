#include "xRand.h"

// Mersenne Twister MT19937 (Matsumoto/Nishimura "mt19937ar-cok"). The state is unsigned int and
// the constants carry no UL suffix: with the reference code's UL suffixes, get_random_offset
// (xParEmitterType.cpp) goes over CW's inline budget.

#define XRAND_N 624
#define XRAND_M 397
#define XRAND_MATRIX_A 0x9908b0df
#define XRAND_UMASK 0x80000000
#define XRAND_LMASK 0x7fffffff
#define XRAND_MIXBITS(u, v) (((u) & XRAND_UMASK) | ((v) & XRAND_LMASK))
#define XRAND_TWIST(u, v) ((XRAND_MIXBITS(u, v) >> 1) ^ ((v) & 1 ? XRAND_MATRIX_A : 0))

static U32 state[XRAND_N];
static S32 left = 1;
static S32 initf = 0;
static U32* next;

static inline void init_genrand(U32 s)
{
    S32 j;

    state[0] = s;
    for (j = 1; j < XRAND_N; j++)
    {
        state[j] = 1812433253 * (state[j - 1] ^ (state[j - 1] >> 30)) + j;
    }

    left = 1;
    initf = 1;
}

// Not in the GC symbol table: inlined into xrand_RandomInit and deadstripped.
void init_by_array(U32* init_key, U32 key_length)
{
    S32 i;
    S32 k;
    U32 j;

    init_genrand(19650218);

    i = 1;
    j = 0;
    k = (XRAND_N > key_length ? XRAND_N : key_length);
    for (; k; k--)
    {
        state[i] = (state[i] ^ ((state[i - 1] ^ (state[i - 1] >> 30)) * 1664525)) + init_key[j] + j;
        i++;
        j++;
        if (i >= XRAND_N)
        {
            state[0] = state[XRAND_N - 1];
            i = 1;
        }
        if (j >= key_length)
        {
            j = 0;
        }
    }

    for (k = XRAND_N - 1; k; k--)
    {
        state[i] = (state[i] ^ ((state[i - 1] ^ (state[i - 1] >> 30)) * 1566083941)) - i;
        i++;
        if (i >= XRAND_N)
        {
            state[0] = state[XRAND_N - 1];
            i = 1;
        }
    }

    state[0] = 0x80000000;
    left = 1;
    initf = 1;
}

void xrand_RandomInit()
{
    U32 init[4] = { 0x123, 0x234, 0x345, 0x456 };
    init_by_array(init, 4);
}

// Not in the GC symbol table (unused, deadstripped).
void xrand_SetRandomSeed(U32 nNewSeed)
{
    init_genrand(nNewSeed);
}

static inline void next_state()
{
    U32* p = state;
    S32 j;

    left = XRAND_N;
    next = state;

    for (j = XRAND_N - XRAND_M + 1; --j; p++)
    {
        *p = p[XRAND_M] ^ XRAND_TWIST(p[0], p[1]);
    }

    for (j = XRAND_M; --j; p++)
    {
        *p = p[XRAND_M - XRAND_N] ^ XRAND_TWIST(p[0], p[1]);
    }

    *p = p[XRAND_M - XRAND_N] ^ XRAND_TWIST(p[0], state[0]);
}

U32 xrand_GenRandInt32()
{
    U32 y;

    if (--left == 0)
    {
        next_state();
    }

    y = *next++;

    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680;
    y ^= (y << 15) & 0xefc60000;
    y ^= (y >> 18);

    return y;
}

// Returns a value in [0, 1).
F32 xrand_GenRandFloat()
{
    U32 y;

    if (--left == 0)
    {
        next_state();
    }

    y = *next++;

    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680;
    y ^= (y << 15) & 0xefc60000;
    y ^= (y >> 18);

    return (F32)y * (1.0f / 4294967296.0f);
}

U32 xrand_RandomChoice(U32& chosenMask, U8& setBits, U32 maxCount, xRandomChoiceRule rule)
{
    U32 choice = 0;
    U32 ithUnplayed;
    U32 testMask;

    switch (rule)
    {
    case xRandomChoiceRule_ANY_EXCEPT_LAST:
        do
        {
            choice = xrand_RandomChoice(maxCount);
        } while (maxCount > 1 && ((1 << choice) & chosenMask));
        chosenMask = 1 << choice;
        break;

    case xRandomChoiceRule_ANY:
        choice = xrand_RandomChoice(maxCount);
        break;

    case xRandomChoiceRule_ANY_NOT_CHOSEN:
        ithUnplayed = xrand_RandomChoice(maxCount - setBits) + 1;
        testMask = 1;

        // Find the ithUnplayed-th choice that hasn't been chosen yet.
        while (true)
        {
            if (!(testMask & chosenMask))
            {
                ithUnplayed--;
                if (ithUnplayed == 0)
                {
                    break;
                }
            }
            testMask <<= 1;
            choice++;
        }

        chosenMask |= 1 << choice;
        setBits++;
        if (setBits == maxCount)
        {
            if (maxCount > 1)
            {
                // Start the next round without the last choice, so it can't repeat.
                setBits = 1;
                chosenMask = 1 << choice;
            }
            else
            {
                setBits = 0;
                chosenMask = 0;
            }
        }
        break;

    case xRandomChoiceRule_IN_ORDER:
        choice = setBits;
        chosenMask |= 1 << choice;
        setBits++;
        if (setBits == maxCount)
        {
            setBits = 0;
            chosenMask = 0;
        }
        break;

    case xRandomChoiceRule_IN_ORDER_RANDOM:
        if (chosenMask == 0)
        {
            setBits = xrand_RandomChoice(maxCount);
        }
        choice = setBits;
        setBits++;
        if (setBits == maxCount)
        {
            setBits = 0;
        }
        break;
    }

    return choice;
}
