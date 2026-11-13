#ifndef _MSL_ALLOC_H
#define _MSL_ALLOC_H

#include "types.h"

#ifdef __cplusplus
// MSL declares free in namespace std and brings it into the global namespace with a
// using-declaration (see MSL_Common/stdlib.h).
namespace std
{
    extern "C" void free(void*);
} // namespace std

using std::free;
#else
void free(void*);
#endif

#endif
