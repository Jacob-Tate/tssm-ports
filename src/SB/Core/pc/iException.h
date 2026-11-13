#ifndef IEXCEPTION_H
#define IEXCEPTION_H

// PC counterpart of gc/iException.h, without the Dolphin includes.
#include <types.h>

void iExceptionMemCrash(const char* loc, U32 size, const char* arg3);

#endif
