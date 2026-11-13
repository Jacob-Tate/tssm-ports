#ifndef _MSL_STDLIB
#define _MSL_STDLIB

#include <PowerPC_EABI_Support\MSL_C\MSL_Common\size_t.h>

#ifdef __cplusplus
extern "C" {
#endif

int rand(void);
int atoi(const char* nptr);
double atof(const char*); // double (const char*)?
void exit(int __status);
void qsort(void*, size_t, size_t, int (*)(const void*, const void*));
long abs(long n);

#ifdef __cplusplus
}
#endif

// MSL declares the allocation functions in namespace std (cstdlib) and brings them into the
// global namespace with using-declarations. A call through std::free is not linked to a
// global-scope definition of free for inlining (iMemExit calls free out of line in retail).
#ifdef __cplusplus
namespace std
{
    extern "C" {
#endif

    void* malloc(size_t size);
    void free(void* ptr);

#ifdef __cplusplus
    }
} // namespace std

using std::free;
using std::malloc;
#endif

#endif