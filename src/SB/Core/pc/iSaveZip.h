#ifndef ISAVEZIP_H
#define ISAVEZIP_H

// PC-only: the smallest zip the save store needs -- every save in one file,
// to carry between installs (PC, Android) or keep as a backup. Entries are
// STORED, not compressed: a save is 60 KB and every zip tool reads stored
// entries. Reading also takes deflated entries, which is what Windows, 7-Zip
// and the rest make of a folder; a zip packed any other way (or with a
// password) is refused with a message saying to extract it first.

#include <types.h>

#include <string>
#include <vector>

struct iSaveZipEntry
{
    std::string name;
    std::vector<U8> data;
    S64 mtime; // Unix seconds. Building: 0 is now. Parsed: 0 is an entry with no date
};

// The zip's bytes.
void iSaveZipBuild(const std::vector<iSaveZipEntry>& entries, std::vector<U8>& out);

// The entries of a zip (folders and entries over 64 MB left out). FALSE with
// `err` for a zip that is damaged or packed in a way this does not read.
bool iSaveZipParse(const U8* p, size_t n, std::vector<iSaveZipEntry>& out, char* err,
                   size_t errsize);

// Whether `p` starts like a zip.
bool iSaveZipIs(const U8* p, size_t n);

#endif
