#ifndef ISAVEZIP_H
#define ISAVEZIP_H

// PC-only: the smallest zip the save store needs -- every save in one file,
// to carry between installs (PC, Android) or keep as a backup. Entries are
// STORED, not compressed: a save is 60 KB and every zip tool reads stored
// entries. Reading takes stored entries only; a zip a tool has recompressed
// is refused with a message saying to extract it first.

#include <types.h>

#include <string>
#include <vector>

struct iSaveZipEntry
{
    std::string name;
    std::vector<U8> data;
    S64 mtime; // Unix seconds; 0 for now
};

// The zip's bytes.
void iSaveZipBuild(const std::vector<iSaveZipEntry>& entries, std::vector<U8>& out);

// The stored entries of a zip. FALSE with `err` for a zip that is damaged or
// holds compressed entries.
bool iSaveZipParse(const U8* p, size_t n, std::vector<iSaveZipEntry>& out, char* err,
                   size_t errsize);

// Whether `p` starts like a zip.
bool iSaveZipIs(const U8* p, size_t n);

#endif
