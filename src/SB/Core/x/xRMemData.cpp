#include "xRMemData.h"
#include "xMemMgr.h"

#include <string.h>

void* RyzMemData::operator new(size_t amt, S32, RyzMemGrow* growCtxt)
{
    void* mem;
    S32 dogrow = true;
    if (growCtxt == NULL)
    {
        dogrow = false;
    }
    else if (growCtxt->IsEnabled() == 0)
    {
        dogrow = false;
    }

    if (dogrow)
    {
        mem = xMemGrowAllocSize(amt);
    }
    else
    {
        mem = xMemAllocSize(amt);
    }
    memset(mem, 0, 4);
    return mem;
}

void RyzMemData::operator delete(void*)
{
}

RyzMemGrow* RyzMemGrow::Init(xBase* growuser)
{
    if (this->ptr != NULL)
    {
        return this;
    }
    this->amt_last = 0;
    this->ptr_last = NULL;
    this->user_last = NULL;
    this->amt = 0x20;
    this->ptr = (char*)xMemAllocSize(this->amt);
    this->user = growuser;
    this->flg_grow = 1;
    return this;
}

RyzMemGrow* RyzMemGrow::Resume(xBase*)
{
    this->amt = this->amt_last;
    this->ptr = this->ptr_last;
    this->user = this->user_last;
    this->flg_grow = 0b11;
    return this;
}

void RyzMemGrow::Done()
{
    this->amt_last = this->amt;
    this->ptr_last = this->ptr;
    this->user_last = this->user;
    this->amt = 0;
    this->ptr = NULL;
    this->user = NULL;
    this->flg_grow = 0;
}
