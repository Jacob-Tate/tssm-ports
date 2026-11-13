#ifndef IEXCEPTION_H
#define IEXCEPTION_H

#include "dolphin.h"
#include "dolphin/os.h"
#include <types.h>

void iExceptionMemCrash(const char* loc, U32 size, const char* arg3);

#endif
