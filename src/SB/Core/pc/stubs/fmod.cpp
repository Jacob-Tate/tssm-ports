// The FMOD call shared code makes directly, on PC.
//
// Caller: xSndMgr.cpp's xSndMgrStopSounds(-3), which shuts down FMOD's
// auxiliary (reverb) effect when every sound stops. FMOD is the GameCube's
// sound library; the port has none (the Xbox assets carry Xbox ADPCM, not FMOD
// banks), so this forwards to pc/iSnd.cpp, which turns its reverb off until a
// voice asks for it again -- what the console's next FSOUND_AUXFX_SetReverbHi
// does. The call should still move behind an i* function (gc keeping FMOD)
// through the GameCube gate.
//
// Listed in stubs/STUBS.md (tools/pcstubs.py).

#include <fmod/fmod.h>

#include "iSnd.h"

extern "C" signed char FSOUND_AUXFX_ShutdownEffect(int effect)
{
    iSndEffectShutdown();
    return 1;
}
