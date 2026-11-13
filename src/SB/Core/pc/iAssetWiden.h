#ifndef IASSETWIDEN_H
#define IASSETWIDEN_H

#include <types.h>

// PC-only: copy-at-load widening of the asset types that keep pointers in
// 4-byte slots. New code (treedome widens BfBB's in readXForm hooks inside its
// zAssetTypes.cpp; TSSM keeps the game code as it is and does it here).
//
// **Why.** The game reads most assets in place, through the structs it was
// written with, and several of those structs hold pointers: some are asset ids
// on disk that the game resolves into the same slot (ATBL's raw animations,
// LODT and COLL rows, the scene parameters' idle extras), some are byte
// offsets it relocates (CRDT texts), some are pointers it fills in later
// (SHRP fragments, LKIT lights, the JSP header and trees, a sound group's
// name, a CRDT picture's texture), and DEST keeps a count where its struct has
// a pointer. On the console and on a 32-bit host a pointer is 4 bytes and the
// slot fits it. On a 64-bit host the struct is wider than the record on disk,
// so every field after the first pointer is read from the wrong place, and
// every pointer written lands on the next field.
//
// **What.** On a 64-bit host each such asset is copied, as it is loaded, into
// the host's layout: the same fields in the same order, every pointer slot 8
// bytes (holding the disk value, zero-extended), and the offsets that point
// inside the asset moved with the data. (A CRDT picture's preset keeps its disk
// size: its texture slot takes in the pad after it and is cleared; see
// iAssetWiden.cpp.) The game then reads and writes the copy exactly as it would
// the asset on the console. On a 32-bit host nothing is installed and the
// assets are read as they are.
//
// **How it is hooked.** zAssetStartup (zAssetTypes.cpp) hands its handler
// table to iAssetTypesInstall under PLATFORM_PC before the packer sees it
// (iAssetHooks.cpp). Three kinds of hook:
//
//   * a type with no readXForm of its own (SGRP, LODT, COLL, CRDT, DEST, SHRP,
//     LKIT, DYNA) gets one that returns the widened copy, which becomes the
//     asset's memory (xSTFindAsset returns it), and an assetUnloaded that
//     frees it after the game's own unload ran. DYNA is widened only for the
//     scene parameters (0xFABDB3B3); every other DYNA is handed back as it is.
//   * ATBL's readXForm (ATBL_Read) is run on a widened copy, which is freed
//     when it returns: ATBL_Read builds an xAnimTable and keeps nothing of the
//     asset.
//   * JSP's and SPLN's readXForms are run on a widened copy, which they keep
//     pointers into; it is freed after their unload.
//
// The copies come from RenderWare's low heap (iLowHeap.h), so they are below
// 4 GB like everything else the game keeps in a 32-bit field
// (iSndGroupHandle holds an SGRP pointer, for one).
//
// **Kept at 4 bytes instead.** Two structs are also read in place out of the
// cutscene stream buffers, where nothing can be widened: an animation's
// iAnimSKBHeader (ExtractedData) and the cutscene records' xCutsceneData
// (DataPtr). Their pointer is an iPtr32 (compat/types.h): 4 bytes on every
// host, a pointer below 4 GB stored as it is and one above it (a static of the
// executable) through a small handle table (iPtr32Far, here). ANIM assets are
// therefore read as they are.
//
// **Not covered** (no such asset in the Xbox data; a 64-bit load of one is
// reported): MPHT morph sequences (xMorph.cpp), CRV curves, VOLU volumes and
// SPLP spline paths.

struct PKRAssetType;

// Called by zAssetStartup with the game's handler table. Does nothing on a
// 32-bit host.
void iAssetTypesInstall(PKRAssetType* handlers);

// ---------------------------------------------------------------------------
// The transforms on their own, without the packer (tests/widentest.cpp runs
// them over every package of an asset folder).

// TRUE when assets of `type` are widened on this host (always FALSE on a
// 32-bit host).
S32 iAssetWidenType(U32 type);

// A widened copy of `size` bytes of asset `type` in the disk layout, and its
// size in *outsize. NULL when this asset needs no copy (another DYNA subtype,
// a JSP clump) or is not one this understands, which *reason says (a static
// string; "" when nothing is wrong). The copy is freed with iAssetWidenFree.
void* iAssetWiden(U32 type, U32 aid, const void* disk, U32 size, U32* outsize,
                  const char** reason);
void iAssetWidenFree(void* copy);

// TRUE when `p` is a copy iAssetWiden made and has not freed.
S32 iAssetWidenIsCopy(const void* p);

// Copies alive and their bytes, and the most there have been.
void iAssetWidenStats(U32* live, U32* bytes, U32* peakBytes);

#endif
