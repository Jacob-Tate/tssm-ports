#ifndef TSSM_TYPES_H
#define TSSM_TYPES_H

#include "macros.h"

// Note: only include this header inside TSSM-related headers/source code files.
// Don't include this in any RenderWare, system, bink, etc. files

#ifdef GAMECUBE
typedef signed char S8;
typedef signed short S16;
typedef signed int S32;
typedef signed long long S64;

typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int U32;
typedef unsigned long long U64;

typedef float F32;
typedef double F64;
#elif !defined(__MWERKS__)
// Host (PC port) builds: the GameCube widths the game code assumes, from <stdint.h>.
#include <stdint.h>
typedef int8_t S8;
typedef int16_t S16;
#ifdef __3DS__
// devkitARM's newlib makes int32_t long, unlike every other target: spelled out
// as the console's int, so S32 stays RwInt32's type (and the game's own).
typedef signed int S32;
#else
typedef int32_t S32;
#endif
typedef int64_t S64;
typedef uint8_t U8;
typedef uint16_t U16;
#ifdef __3DS__
typedef unsigned int U32;
#else
typedef uint32_t U32;
#endif
typedef uint64_t U64;
typedef float F32;
typedef double F64;
#endif

// Integers as wide as a pointer, for code that keeps a pointer in an integer or does address
// arithmetic: U32/S32 on the GameCube, uintptr_t/intptr_t on a host. Asset data that stores
// pointers in 4-byte fields is not widened with these; those sites are marked "copy-at-load".
#if defined(GAMECUBE) || defined(__MWERKS__)
typedef unsigned int UPtr;
typedef int SPtr;
#else
typedef uintptr_t UPtr;
typedef intptr_t SPtr;
#endif

#ifdef NULL
#undef NULL
#endif
#define NULL 0

#ifdef TRUE
#undef TRUE
#endif
#define TRUE 1

#ifdef FALSE
#undef FALSE
#endif
#define FALSE 0

// The stub is for tools that parse CodeWarrior-only sources (clangd). A PC build keeps the host's
// __declspec: the Windows CRT headers need dllimport/selectany.
#if !defined(__MWERKS__) && !defined(PLATFORM_PC)
#define __declspec(x)
// #define asm
#endif

#ifdef __MWERKS__
#define WEAK __declspec(weak)
#else
#define WEAK __attribute__((weak))
#endif

// File-scope objects that retail has as LOCAL symbols but that an earlier file of the same wad
// reaches through an `extern` declaration. CodeWarrior accepts extern-then-static in one
// translation unit; ISO C++ does not, so host builds give them external linkage.
#ifdef __MWERKS__
#define WAD_STATIC static
#else
#define WAD_STATIC
#endif

// Objects defined in a header that several wads include. CodeWarrior compiles with -common on,
// which makes every copy a common symbol and the linker keeps one; ISO C++ has no common symbols,
// so host builds make the definition a C++17 inline variable, which the linker merges the same way.
#ifdef __MWERKS__
#define WAD_COMMON
#else
#define WAD_COMMON inline
#endif

typedef signed char s8;
typedef signed short s16;
typedef signed long s32;
typedef signed long long s64;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
#ifdef __MWERKS__
typedef unsigned long size_t;
#else
#include <stddef.h> // the host's size_t (unsigned int on 32-bit MSVC targets)
#endif
typedef unsigned long long u64;

typedef unsigned short ushort;
typedef unsigned int uint;

typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile u64 vu64;
typedef volatile s8 vs8;
typedef volatile s16 vs16;
typedef volatile s32 vs32;
typedef volatile s64 vs64;

typedef float f32;
typedef double f64;
typedef volatile f32 vf32;
typedef volatile f64 vf64;

typedef int BOOL;

typedef int unknown;

#ifndef __cplusplus
typedef unsigned short wchar_t;
typedef wchar_t wint_t;
#endif

// Basic defines to allow newer-like C++ code to be written
#define TRUE 1
#define FALSE 0

#if !defined(__cplusplus) || __cplusplus < 201103L
#define nullptr 0
#endif
#define null 0

#ifdef __MWERKS__
#define ALIGNB(a) __attribute__((aligned(a)))
#else
#define ALIGNB(a)
#endif

#ifndef NULL
#define NULL 0
#endif

#ifndef UINT32_MAX
#define UINT32_MAX 0xffffffff
#endif

#endif // !TYPES_H
