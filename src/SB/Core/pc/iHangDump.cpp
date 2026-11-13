// The hang dump's heartbeat (iHangDump.h). The dump itself is in tssm_main.cpp,
// beside the crash handler whose stack walk it shares. Port-only.

#include "iHangDump.h"

namespace
{
    // One writer (the game thread) and one reader (the hang thread), and a
    // count that only has to be seen to move: a torn or late read costs at most
    // one poll.
    volatile U32 sBeats;
    iHangDumpFn sDumper;
} // namespace

void iHangDumpBeat()
{
    sBeats = sBeats + 1;
}

U32 iHangDumpBeats()
{
    return sBeats;
}

void iHangDumpAll(const char* title)
{
    if (sDumper != NULL)
    {
        sDumper(title);
    }
}

void iHangDumpSetDumper(iHangDumpFn fn)
{
    sDumper = fn;
}
