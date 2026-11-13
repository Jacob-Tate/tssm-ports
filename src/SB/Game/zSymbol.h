#ifndef ZSYMBOL_H
#define ZSYMBOL_H

#include <types.h>

struct xModelInstance;

struct symbol_map
{
    symbol_map* next;
    U32 id;
    xModelInstance* model;
};

namespace z
{
    class symbol
    {
    public:
        static symbol_map* map;

        // No GC symbol: inlined into zSceneInit.
        static void init()
        {
            map = NULL;
        }
    };
} // namespace z

#endif
