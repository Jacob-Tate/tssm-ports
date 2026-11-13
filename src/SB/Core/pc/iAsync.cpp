// PC counterpart of gc/iAsync.cpp, compiled inside pc/iWad.cpp.
//
// On the console zGame starts zAsync::RenderThread on a second OS thread while
// a level loads, and the two share a single core: iAsyncBeginUpdate and
// iAsyncEndUpdate disable and enable interrupts, which on that hardware is a
// lock, and the TRC disc-error screen suspends and resumes the thread.
//
// A host thread runs truly in parallel, so a faithful port needs a real lock
// around everything those brackets protect -- and the render thread draws
// through RenderWare, which librw does not allow from a second thread on every
// backend. Until that is designed,
// iAsyncStart does NOT start the function and reports it done at once. The
// load screen it would have animated is not shown; the load itself is
// unaffected, because zGame runs the load on the main thread either way, and
// zAsync::RenderThread only loops while iAsyncIsDone() is false.
//
// STUB, listed in PORTING.md.

#include "iAsync.h"

#include "iStub.h"

#include <rwcore.h>

bool asyncThreadDone = true;
RwLinkList asyncDirtyFrameList;

void iAsyncStart(void* (*func)(void*))
{
    IPORT_STUB();
    rwLinkListInitialize(&asyncDirtyFrameList);
    asyncThreadDone = true;
    (void)func;
}

S32 iAsyncIsDone()
{
    return asyncThreadDone;
}

void iAsyncTerminate()
{
    asyncThreadDone = true;
}

void iAsyncBeginUpdate()
{
}

void iAsyncEndUpdate()
{
}

void iAsyncSuspend()
{
}

void iAsyncResume()
{
}
