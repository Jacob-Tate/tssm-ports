#ifndef IMODPACKAGE_H
#define IMODPACKAGE_H

#include <types.h>

#include <stdio.h>

#include <string>
#include <vector>

// PC-only: a HIP or HOP package as the packer sees it once mods have replaced
// some of its assets. New code (no treedome or GameCube counterpart).
//
// **Why at the file level.** Every reader of a package goes through iFile:
// the packer's TOC parse and layer loads (xhipio/xbinio), xTextureManager's
// streamed TEXS reads (`sector * 32` from the TOC), the cutscene reader and the
// sound data reads (TOC offsets). If only the packer were told about a
// replacement, the TOC offsets the other three use would point at the old
// bytes. So the replacement is made in the file instead: iFileOpen hands out a
// *virtual* package whose DICT says where each asset now is, and whose stream
// has the new bytes there. Nothing in shared code changes, and every reader
// agrees.
//
// **The layout rule.** The packer reads a layer as one block, from its first
// asset's offset for the sum of its assets' sizes and pads (rounded up to
// 0x800), and finds each asset at `layer memory + (d_off - first d_off)`. So:
//
//   * a replacement that fits in the old asset's slot (its size plus the pad up
//     to the next asset) is written in place, and the pad absorbs the
//     difference;
//   * otherwise the asset's whole layer is copied to the end of the stream with
//     the new bytes in it, every asset keeping the alignment its old offset had
//     (0x800 for streamed textures and sounds), and the DICT is pointed there.
//     The old copy is left where it was, unreferenced.
//
// Nothing is copied into memory but the package header (PACK and DICT) and the
// replacement bytes: the virtual file is a list of extents over the original
// file, the header, the replacements and zero fill.
//
// A replaced asset that an SNDI table describes gets its table entry's
// data_size changed to match, and when the replacement is decoded audio (16-bit
// PCM) the entry's WAVEFORMATEX becomes PCM's, so the sound layer plays it.

// One asset of a package, as the replacement lookup is told about it.
struct iModPkgAsset
{
    U32 id;
    U32 type; // the packer's four-character code, e.g. 'RWTX', 'SND '
    const char* name; // the ADBG name, "" when the package has none
    U32 size;
};

// What replaces an asset.
struct iModPkgReplacement
{
    std::vector<U8> bytes;

    // `bytes` is 16-bit little-endian PCM decoded from an audio file: an SNDI
    // entry for this asset is rewritten to describe it.
    bool pcm;
    U16 channels;
    U32 rate;

    // For the log: "<mod>: <file under the mod>".
    std::string source;

    iModPkgReplacement() : pcm(false), channels(0), rate(0)
    {
    }
};

// Asked once for every asset of a package. Fill `out` and return true to
// replace the asset; return false to keep it.
typedef bool (*iModPkgLookupFn)(void* ctx, const iModPkgAsset& asset, iModPkgReplacement* out);

struct iModPkg;

// Builds the virtual package for the file at `path`. NULL when the lookup
// replaces nothing, or when the file is not a package this understands (it is
// then read as it is, which is the unmodded behaviour). Lines describing what
// was replaced, or why nothing could be, are appended to `log`. The result has
// one reference.
iModPkg* iModPkgBuild(const char* path, iModPkgLookupFn lookup, void* ctx,
                      std::vector<std::string>* log);

void iModPkgRetain(iModPkg* pkg);
void iModPkgRelease(iModPkg* pkg);

// The virtual file's length.
U32 iModPkgSize(const iModPkg* pkg);

// A positioned read of the virtual file. `original` is the caller's own handle
// on the package file the view was built from. Bytes past the end read as
// zero, as iFileRead gives them. Returns `size`.
U32 iModPkgRead(const iModPkg* pkg, FILE* original, U32 offset, void* buf, U32 size);

// How many assets the view replaces, and how many layers it had to move.
S32 iModPkgReplacedCount(const iModPkg* pkg);
S32 iModPkgMovedLayers(const iModPkg* pkg);

// --- the table of contents, for tests and tools --------------------------------

struct iModPkgTocAsset
{
    U32 id;
    U32 type;
    U32 off;
    U32 size;
    U32 pad;
    U32 flags;
    U32 align; // ADBG's alignment word
    S32 layer; // index into layers, -1 when no layer names it
    std::string name;
};

struct iModPkgTocLayer
{
    U32 type;
    std::vector<S32> assets; // indices into assets, in the layer's order
};

struct iModPkgToc
{
    U32 fileSize;
    std::vector<iModPkgTocAsset> assets; // in ATOC order
    std::vector<iModPkgTocLayer> layers; // in LTOC order
};

// Reads the table of contents of a package file, or of a virtual package when
// `pkg` is not NULL (`fp` is then the original file). False if it is not one.
bool iModPkgReadToc(FILE* fp, const iModPkg* pkg, iModPkgToc* toc);

// The packer's layer size: each asset's size plus pad but the last's, rounded
// up to 0x800 (xpkrsvc.cpp LOD_r_LHDR).
U32 iModPkgLayerSize(const iModPkgToc& toc, S32 layer);

#endif
