#ifndef I3DSPACKAGE_H
#define I3DSPACKAGE_H

// The 3DS's two package formats as far as the updater reads them
// (3ds/i3DSUpdate.cpp): a .cia's header and title metadata, and a .3dsx's
// header. Plain bytes in, no system calls, so tssm_update3dstest runs it on
// the PC against real files. C library types only (i3DS.h's rule).

#include <stddef.h>
#include <stdint.h>

// From a .cia: the title (from its TMD) and the version that title has.
struct i3DSCiaInfo
{
    uint64_t titleId;
    uint16_t version; // the TMD's: major << 10 | minor << 4 | micro
    uint16_t contentCount;
    uint64_t contentSize;
};

// `head` is the start of the file (64 KB is plenty), `fileSize` the whole
// file's. Nonzero when it is a .cia whose header sizes add up to the file and
// whose TMD lies within `head`; otherwise zero, with the reason in `why`.
int i3DSParseCia(const uint8_t* head, size_t len, uint64_t fileSize, struct i3DSCiaInfo* out,
                 char* why, size_t whySize);

// From a .3dsx: where its SMDH (the name and icon the Homebrew Launcher shows)
// and its RomFS are.
struct i3DS3dsxInfo
{
    uint32_t smdhOffset;
    uint32_t smdhSize;
    uint32_t romfsOffset;
};

// `head` is at least the first 44 bytes. Nonzero for a .3dsx with the
// extended header, an SMDH of the usual size and a RomFS, all within
// `fileSize`.
int i3DSParse3dsx(const uint8_t* head, size_t len, uint64_t fileSize, struct i3DS3dsxInfo* out,
                  char* why, size_t whySize);

// "0.9.1" as a title version: 0 << 10 | 9 << 4 | 1. Up to three numbers,
// at most 63, 63 and 15 (the TMD's fields). Nonzero when it fits.
int i3DSTitleVersion(const char* text, uint16_t* out);

// A title version as "0.9.1".
void i3DSTitleVersionText(uint16_t version, char* out, size_t outSize);

// Nonzero when a .cia's title version is the one `text` (a release file
// name's version, "0.9.2") stands for. Zero for any other, and for a text
// that is no title version (four numbers, 0.64).
int i3DSCiaVersionIs(uint16_t version, const char* text);

#endif
