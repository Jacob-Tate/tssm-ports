#ifndef ISAVELE_H
#define ISAVELE_H

// PC-only: the save payload as the little-endian consoles write it. The
// PlayStation 2 and the Xbox store the same payload the GameCube does
// (iSaveFormat.h), with every integer the other way round:
//
//   header, block headers   little-endian words: the file starts "TADG", the
//                           tags read "RDEL", "MOOR", ...
//   leader                  the same offsets; progress and play time swapped
//   PREF, SVID              little-endian words
//   ROOM, SFIL              the same bytes
//   serializer blocks       stored as little-endian words, and every value
//                           wider than a byte goes into the bit stream least
//                           significant byte first
//
// The last line is why a word swap is not a conversion. The canonical
// (GameCube) stream carries a U32 most significant byte first, and the stream
// is not byte aligned -- a block starts with one bit -- so each such value has
// to be found in the stream and its bytes turned round. Where they are depends
// on what the block's scene saves: the objects flagged as saved, by type, in
// the order zSceneInit lists them. That is read out of the level's own package
// (iSaveHipScan), so a conversion needs the game's files for every level the
// save has visited. The files of any release will do: the lists are the same
// on the GameCube, PlayStation 2 and Xbox discs.
//
// Nothing here touches the disk or the game: the caller hands in bytes and a
// way to read a package. Only <types.h>, the C library and std::vector.
// docs/SAVES.md section 6 has the layout.

#include <types.h>

#include <stddef.h>
#include <string>
#include <vector>

// --- where the multi-byte values are -----------------------------------------------------

// One value of 2 or 4 bytes in a block's bit stream.
struct iSaveStreamField
{
    U32 bit; // where it starts
    U32 bytes;
};

struct iSaveFieldMap
{
    std::vector<iSaveStreamField> fields;
    U32 bits; // the stream's length: nothing past it is ever written
};

// PLYR: zSceneSave's first client. The same for every save.
void iSaveFieldsPlayer(iSaveFieldMap* map);

// CNTR: the task counters of the "TASK COUNTER GROUP" (MN/MNUC.HIP; 114 on
// every disc), then the cheats' flags.
void iSaveFieldsCounters(S32 taskCounters, iSaveFieldMap* map);

// The kinds of object zSceneSave writes something for, in the order they come
// in a scene's list (zScene.cpp's sInitTable, then its dynamic types).
enum iSaveObjectKind
{
    ISLE_TRIGGER,
    ISLE_MOVEPOINT,
    ISLE_PICKUP,
    ISLE_SIMPLE,
    ISLE_PLATFORM,
    ISLE_PENDULUM,
    ISLE_HANGABLE,
    ISLE_BUTTON,
    ISLE_EGENERATOR,
    ISLE_TIMER,
    ISLE_COUNTER,
    ISLE_GROUP,
    ISLE_PORTAL,
    ISLE_CAMERA,
    ISLE_SURFACE,
    ISLE_VOLUME,
    ISLE_CONDITIONAL,
    ISLE_ENV,
    ISLE_DISPATCHER,
    ISLE_FOG,
    ISLE_LIGHT,
    ISLE_SCRIPT,
    ISLE_TASKBOX,
    ISLE_FLYTHROUGH,
    ISLE_CAMERATWEAK,
    ISLE_TELEPORT,
    ISLE_RACETIMER,
    ISLE_KINDS
};

// How many saved objects of each kind a set of packages holds, and the task
// counter group's size if one of them has the group (-1 if none does).
struct iSaveObjectCounts
{
    U32 n[ISLE_KINDS];
    S32 taskCounters;
};

void iSaveObjectCountsClear(iSaveObjectCounts* c);

// A level's block: one bit, the two F32 offsets, then each saved object.
void iSaveFieldsScene(const iSaveObjectCounts* counts, iSaveFieldMap* map);

// --- packages -------------------------------------------------------------------------------

// Reads `n` bytes at `offset` of a package (HIP/HOP). FALSE when they are not
// all there.
typedef bool (*iSaveReadFn)(void* ctx, U64 offset, void* buf, size_t n);

// Adds a package's saved objects to `counts`: its asset table is walked and
// the head of every asset of a saved kind read (the flags are in the
// console's byte order, which the package's PLAT chunk gives). FALSE with
// `err` for something that is not a package or is cut short.
bool iSaveHipScan(iSaveReadFn read, void* ctx, U64 size, iSaveObjectCounts* counts, char* err,
                  size_t errsize);

// The same for a package in memory.
bool iSaveHipScanData(const U8* p, size_t n, iSaveObjectCounts* counts, char* err, size_t errsize);

// xStrHash, for the tests' packages.
U32 iSaveStrHash(const char* s);

// --- the conversion ---------------------------------------------------------------------------

// The field map of serializer block `tag` ("PLYR", "CNTR", or a scene's id).
// FALSE with `err` when it cannot be worked out (the level's package is not
// there).
typedef bool (*iSaveFieldMapFn)(void* ctx, U32 tag, iSaveFieldMap* map, char* err, size_t errsize);

enum iSaveLEDir
{
    ISLE_TO_LE, // canonical (GameCube) -> PlayStation 2 / Xbox
    ISLE_FROM_LE
};

// Whether `p` starts like a little-endian payload ("TADG", version 1).
bool iSavePayloadIsLE(const U8* p, size_t n);

// The size field of a little-endian payload's header.
U32 iSavePayloadLESize(const U8* p);

// Converts a whole payload. `src` is `n` bytes, of which the header's size is
// the payload. The result is exactly the payload. Each direction undoes the
// other byte for byte. The CRC is rewritten for the converted bytes when the
// source's held and made to fail when it did not, as iSavePayloadConvert does.
//
// A serializer block that is all zeros (a level entered and not yet left) needs
// no map and `maps` is not asked for it. FALSE with `err` when the payload's
// structure is broken or a map could not be had.
//
// A block that holds data past the end of its map was written by a game whose
// level saved more than this copy of the level's package says (another
// release of the disc, or a mod's level). It is still converted: the values
// the map knows are turned, and what lies past it is kept bit for bit, which
// is right for everything but a multi-byte value out there. `notes` (if not
// NULL) gets a line saying so for each such block.
bool iSavePayloadLE(iSaveLEDir dir, const U8* src, size_t n, std::vector<U8>& dst,
                    iSaveFieldMapFn maps, void* ctx, std::string* notes, char* err,
                    size_t errsize);

// Turns the bytes of each of `map`'s values round in `block`, a serializer
// block in little-endian form (stream bit k is bit k%8 of byte k/8). Its own
// inverse. FALSE when a value does not fit in the block.
bool iSaveStreamReverse(U8* block, size_t n, const iSaveFieldMap* map);

// The last set bit of a little-endian-form block, or -1 for all zeros.
S32 iSaveStreamLastBit(const U8* block, size_t n);

#endif
