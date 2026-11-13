#ifndef TSSM_PC_COMPAT_MWCPLUSPLUSLIB_H
#define TSSM_PC_COMPAT_MWCPLUSPLUSLIB_H

// PC shadow of MSL's C++ runtime header. One game file calls the runtime's
// array constructor directly: zParticleLocator::scene_enter carves its
// activity array out of a heap block with __construct_new_array. The runtime
// helper does not exist on a host, so src/SB/Core/pc/mwcplusplus.cpp defines it
// with CodeWarrior's semantics (a 16-byte header holding the element size and
// count, then the elements).
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* ConstructorDestructor;

void* __construct_new_array(void* block, ConstructorDestructor ctor, ConstructorDestructor dtor,
                            size_t size, size_t n);
void __destroy_new_array(void* block, ConstructorDestructor dtor);

#ifdef __cplusplus
}
#endif

#endif
