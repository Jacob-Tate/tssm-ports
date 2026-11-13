#include <PowerPC_EABI_Support/Runtime/MWCPlusPlusLib.h>

#include <stddef.h>

// CodeWarrior's C++ runtime array helpers, for the one game file that calls
// one by name (zParticleLocator::scene_enter; see the header). The compiler
// itself never emits calls to these on a host -- it has its own array-new --
// so only the explicit calls need them.
//
// The layout is CodeWarrior's: a 16-byte header in front of the elements, word
// 0 the element size and word 1 the count, so the block the caller allocates is
// 16 + size * n bytes (zParticleLocator asks for 0x8810 = 16 + 0x110 * 0x80).
// Words, not size_t, because the header is 16 bytes on the console and the
// caller's arithmetic says so.
//
// A constructor is called as CodeWarrior calls one, (object, 1) for a complete
// object. The game's one caller passes none, so that path is untested.

#define MW_ARRAY_HEADER 16

typedef void (*mw_ctor)(void*, int);
typedef void (*mw_dtor)(void*, int);

extern "C" void* __construct_new_array(void* block, ConstructorDestructor ctor,
                                       ConstructorDestructor dtor, size_t size, size_t n)
{
    if (block == NULL)
    {
        return NULL;
    }

    unsigned int* header = (unsigned int*)block;
    header[0] = (unsigned int)size;
    header[1] = (unsigned int)n;

    char* first = (char*)block + MW_ARRAY_HEADER;
    if (ctor != NULL)
    {
        for (size_t i = 0; i < n; i++)
        {
            ((mw_ctor)ctor)(first + i * size, 1);
        }
    }

    (void)dtor;
    return first;
}

extern "C" void __destroy_new_array(void* block, ConstructorDestructor dtor)
{
    if (block == NULL || dtor == NULL)
    {
        return;
    }

    unsigned int* header = (unsigned int*)((char*)block - MW_ARRAY_HEADER);
    size_t size = header[0];
    size_t n = header[1];

    // Last to first, as the runtime destroys them.
    for (size_t i = n; i > 0; i--)
    {
        ((mw_dtor)dtor)((char*)block + (i - 1) * size, -1);
    }
}
