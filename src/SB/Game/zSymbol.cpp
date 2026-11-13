#include "zSymbol.h"

// The PS2 DWARF lists map between zSurface's and zThrown's data; Incredibles and Ratatouille
// define it in zSymbol.cpp.
symbol_map* z::symbol::map;

// Dead-stripped placeholder, not recovered code: retail's string pool keeps ".minf" and ".dff"
// between zTaxi's and zSurface's strings, from zSymbol.cpp's stripped model lookups (no
// zSymbol.cpp function survives). Body unknown.
static const char* __deadstripped_zSymbol(bool minf)
{
    if (minf)
    {
        return ".minf";
    }
    return ".dff";
}
