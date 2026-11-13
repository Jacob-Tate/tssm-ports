#include "xBehaviour.h"

xBase* xGoal::GetOwner() const
{
    return this->psyche->clt_owner;
}

void xGoal::Clear()
{
    this->stat = GOAL_STAT_UNKNOWN;
}

S32 xGoal::Goal_Process(en_trantype* trantype, F32 dt, void* ctxt)
{
    if (this->fun_process)
    {
        return this->fun_process(this, this->cbdata, trantype, dt, ctxt);
    }

    return 0;
}
