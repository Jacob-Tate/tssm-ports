#ifndef IDEBUG_H
#define IDEBUG_H

// GameCube debug print. The Ratatouille prototype (same engine, GameCube; its gc/iDebug.h) maps it to
// OSReport in debug/release builds and defines it EMPTY in the final build, so a final-build
// 'iprintf(fmt, args);' is left as the expression statement '(fmt, args);'. That expression
// generates no code and, in optimized functions, interns no string (an unoptimized function -
// '#pragma optimization_level 0', e.g. zCutsceneMgrFinishExit - still pools the format string),
// but it still references its arguments: a function
// parameter passed to it is no longer substituted by a constant argument when the function is
// inlined (see iFileGetSectorSize, whose retail inlined copies keep the unfolded switch).
#define iprintf

// A compiled-out debug statement whose ARGUMENT retail still evaluated (placeholder: Heavy Iron's
// actual macro is unknown, e.g. a debug print or validate macro that the final build reduced to its
// argument, like the empty iprintf above). Use it only where retail's code shows a read that no
// other statement explains and the read is needed to match (a discarded load of a volatile field:
// iARAMTmpMalloc). Like xASSERT_PLACEHOLDER(), it marks a debug site of unknown content; unlike it,
// its argument is evaluated, so the expression must be what retail reads.
#define iDEBUG_EVAL_PLACEHOLDER(expr) ((void)(expr))

#endif
