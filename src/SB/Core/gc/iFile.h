#ifndef IFILE_H
#define IFILE_H

#include <types.h>
#include <dolphin.h>

enum IFILE_READSECTOR_STATUS
{
    IFILE_RDSTAT_NOOP,
    IFILE_RDSTAT_INPROG,
    IFILE_RDSTAT_DONE,
    IFILE_RDSTAT_FAIL,
    IFILE_RDSTAT_QUEUED,
    IFILE_RDSTAT_EXPIRED
};

enum xFileDriveType
{
    XFILE_DRIVE_CDVD,
    XFILE_DRIVE_CONSOLE_HD
};

#ifdef GAMECUBE
struct tag_xFile;

struct tag_iFile
{
    U32 flags;
    char path[128];
    S32 entrynum;
    DVDFileInfo fileInfo;
    void (*cb)(tag_xFile*); // 0xC4
    S32 asynckey;
    S32 buffer[8]; // 0xCC
    S32 offset; // 0xEC
};
#else
#ifdef PS2
struct tag_iFile
{
    U32 flags;
    char path[128];
    S32 fd;
    S32 offset;
    S32 length;
};
#endif
#endif

#define IFILE_OPEN_READ 0x1
#define IFILE_OPEN_WRITE 0x2
#define IFILE_OPEN_ABSPATH 0x4

#define IFILE_SEEK_SET 0
#define IFILE_SEEK_CUR 1
#define IFILE_SEEK_END 2

struct tag_xFile;

void iFileInit();
void iFileExit();
U32* iFileLoad(const char* name, U32* buffer, U32* size);
U32 iFileOpen(const char* name, S32 flags, tag_xFile* file);
S32 iFileSeek(tag_xFile* file, S32 offset, S32 whence);
U32 iFileRead(tag_xFile* file, void* buf, U32 size);
S32 iFileReadAsync(tag_xFile* file, void* buf, U32 aSize, void (*callback)(tag_xFile*),
                   S32 priority);
IFILE_READSECTOR_STATUS iFileReadAsyncStatus(S32 key, S32* amtToFar);
U32 iFileClose(tag_xFile* file);
U32 iFileGetSize(tag_xFile* file);
void iFileReadStop();
U8 iFileExists(const char* name, S32 flags);
void iFileFullPath(const char* relname, char* fullname);
void iFileSetPath(const char* path);
U32 iFileFind(const char* name, int fileflags, tag_xFile* file);
void iFileGetInfo(tag_xFile* file, U32* starting_sector, U32* size_in_bytes);
void iFileAsyncService();
U32 iFileGetSectorSize(xFileDriveType drive);

#endif
