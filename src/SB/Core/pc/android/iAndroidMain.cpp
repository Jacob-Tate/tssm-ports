// iAndroidMain.cpp: treedome's pc/android/iAndroidMain.cpp (CC0), renamed for TSSM (BFBB_ -> TSSM_,
// tssm-log.txt, the logcat tag "tssm"). src/SB/Core/pc/PORTING.md lists it.

#include "iAndroid.h"

// The entry point the Android framework actually calls.
//
// SDLActivity loads libmain.so, looks up "SDL_main" in it with dlsym, and runs
// it on a thread of its own through SDL_RunApp -- so the name and the C
// linkage are an interface with Java and not a style choice. The game's own
// main is renamed out of the way by iAndroidGameMain.h; this is what stands
// in front of it.
//
// Everything here has to happen with SDL's JNI plumbing already up and the
// game not yet started, which is exactly one moment wide. The other half of
// the setup -- the log -- cannot wait for it and runs from a static
// constructor in tssm_main.cpp instead; iAndroid.h has the reasoning.
//
// The default visibility is explicit because dlsym is what finds this. A
// hidden symbol is a link that succeeds and an application that starts, logs
// "Couldn't find function SDL_main", and shows a black screen.

extern "C" int tssmGameMain(int argc, char** argv);

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char** argv)
{
    iAndroidAdoptGameThread();
    iAndroidStartup();
    iAndroidPerfStart();
    return tssmGameMain(argc, argv);
}
