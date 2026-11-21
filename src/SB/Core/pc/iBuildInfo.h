#ifndef IBUILDINFO_H
#define IBUILDINFO_H

// PC-only: what this build is. tools/build_info.cmake writes the definitions
// into the build directory (iBuildInfo.cpp) on every build, so they are true
// for the executable they are in; __DATE__ and __TIME__ only changed when the
// one file using them recompiled. Read by crash.log's and last_run.log's first
// line (tssm_main.cpp) and the About tab (gui/iGuiTabs.cpp).

// TSSM_PC_VERSION, as in tssm.exe's version information.
extern const char iBuildVersion[];

// The git commit built, ten hex digits, with "-dirty" when tracked files
// differed from it; "unknown" when built outside a git checkout.
extern const char iBuildCommit[];

// When it was built, local time: "2026-11-21 14:05:33".
extern const char iBuildTime[];

#endif
