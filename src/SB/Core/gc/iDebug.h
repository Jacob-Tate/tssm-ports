#ifndef IDEBUG_H
#define IDEBUG_H

// GameCube debug print, defined empty in the final build (as in Ratatouille's gc/iDebug.h), so
// 'iprintf(fmt, args);' is left as the expression '(fmt, args);'. That generates no code but still
// references its arguments: a parameter passed to it is not replaced by a constant argument when
// the function is inlined (see iFileGetSectorSize).
#define iprintf

// A compiled-out debug statement whose argument retail still evaluated (Heavy Iron's actual macro
// is unknown). Use it only for a read that no other statement explains, such as the discarded load
// of a volatile field in iARAMTmpMalloc.
#define iDEBUG_EVAL_PLACEHOLDER(expr) ((void)(expr))

#endif
