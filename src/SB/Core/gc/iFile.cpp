#include "iFile.h"

#include "iDebug.h"
#include "xDebug.h"

#include <string.h>

struct file_queue_entry
{
    tag_xFile* file;
    void* buf;
    U32 size;
    U32 offset;
    IFILE_READSECTOR_STATUS stat;
    void (*callback)(tag_xFile* file);
    U32 asynckey;
};

extern file_queue_entry file_queue[32];
char gHostPath[256];

static U32 tbuffer[1024 + 8];
static U32* buffer32;
U32 iFileSyncAsyncReadActive;

inline void iFileInit()
{
    buffer32 = (U32*)OSRoundUp32B(tbuffer);
}

inline void iFileExit()
{
}

// The compiled-out asserts keep GC/2.6 from auto-inlining this, as retail calls it.
// 'buffer != NULL' is Ratatouille's; the other conditions are unknown.
U32* iFileLoad(const char* name, U32* buffer, U32* size)
{
    char path[128];
    tag_xFile file;
    S32 fileSize, alignedSize;

    iFileFullPath(name, path);
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    iFileOpen(name, IFILE_OPEN_ABSPATH, &file);
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    fileSize = iFileGetSize(&file);
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (!buffer)
    {
        buffer = (U32*)RwMalloc(OSRoundUp32B(fileSize), 0);
    }
    xASSERT(buffer != NULL);
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    alignedSize = OSRoundUp32B(fileSize);
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    iFileRead(&file, buffer, alignedSize);
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    if (size)
    {
        *size = alignedSize;
    }
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    iFileClose(&file);

    return buffer;
}

U32 iFileOpen(const char* name, S32 flags, tag_xFile* file)
{
    tag_iFile* ps = &file->ps;

    if (flags & IFILE_OPEN_ABSPATH)
    {
        strcpy(ps->path, name);
    }
    else
    {
        iFileFullPath(name, ps->path);
    }

    ps->entrynum = DVDConvertPathToEntrynum(ps->path);

    if (ps->entrynum == -1)
    {
        return 1;
    }

    if (!DVDFastOpen(ps->entrynum, &ps->fileInfo))
    {
        ps->entrynum = -1;
        return 1;
    }

    ps->cb = NULL;
    ps->flags = 0x1;

    iFileSeek(file, 0, IFILE_SEEK_SET);

    return 0;
}

S32 iFileSeek(tag_xFile* file, S32 offset, S32 whence)
{
    tag_iFile* ps = &file->ps;
    S32 position;
    S32 new_pos;

    switch (whence)
    {
    case IFILE_SEEK_SET:
        new_pos = offset;
        break;
    case IFILE_SEEK_CUR:
        if (DVDGetCommandBlockStatus(&ps->fileInfo.cb) == DVD_STATE_BUSY)
        {
            return -1;
        }

        new_pos = offset + ps->offset;
        break;
    case IFILE_SEEK_END:
        new_pos = ps->fileInfo.length - offset;

        if (new_pos < 0)
        {
            new_pos = 0;
        }

        break;
    default:
        new_pos = offset;
        break;
    }

    ps->offset = new_pos;

    return ps->offset;
}

static void ifilereadCB(tag_xFile* file)
{
    iFileSyncAsyncReadActive = 0;
}

// The compiled-out asserts keep GC/2.6 from auto-inlining this, as retail calls it. The four with
// conditions are Ratatouille's; the others are placeholders.
U32 iFileRead(tag_xFile* file, void* buf, U32 size)
{
    tag_iFile* ps = &file->ps;
    S32 num;
    xASSERT(file != NULL);
    xASSERT(buf != NULL);
    xASSERT(ps->flags & 1);
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT(!(size & 0x3));

    iFileSyncAsyncReadActive = 1;
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    while (TRUE)
    {
        num = iFileReadAsync(file, buf, size, ifilereadCB, 0);
        if (num != -1)
        {
            break;
        }

        iTRCDisk::TRCCheck();
        iFileAsyncService();
    }
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();

    while (iFileSyncAsyncReadActive)
    {
        iTRCDisk::TRCCheck();
        iFileAsyncService();
    }

    ps->offset += size;

    return size;
}

// bytes is a long: an S32 is merged into the second bounds check. The roundings are one
// statement per step and p is set to buf before the offset is added, as in Ratatouille's debug
// build, so next_read and p are updated in place.
static void async_cb(s32 result, DVDFileInfo* finfo)
{
    S32 i = (S32)finfo->cb.userData;
    file_queue_entry* fq = &file_queue[i];
    long bytes = -1;

    switch (result)
    {
    case DVD_RESULT_FATAL_ERROR:
        xTRCDisk(TRC_DiskFatal);
        return;
    case DVD_RESULT_CANCELED:
        break;
    case DVD_RESULT_IGNORED:
    default:
        if (result >= DVD_RESULT_GOOD)
        {
            bytes = result;
        }
        break;
    }

    if (bytes < 0)
    {
        fq->stat = IFILE_RDSTAT_FAIL;
    }
    else if (bytes + fq->offset >= fq->size ||
             bytes + fq->offset + fq->file->ps.offset >= fq->file->ps.fileInfo.length)
    {
        fq->stat = IFILE_RDSTAT_DONE;
        fq->offset = fq->size;
    }
    else
    {
        fq->offset += bytes;
        fq->stat = IFILE_RDSTAT_INPROG;

        long next_read;
        if (fq->size - fq->offset < 0x8000)
        {
            next_read = fq->size - fq->offset;
            next_read += 3;
            next_read &= ~3;
        }
        else
        {
            next_read = 0x8000;
        }

        if (next_read + fq->offset + fq->file->ps.offset > fq->file->ps.fileInfo.length)
        {
            next_read = fq->file->ps.fileInfo.length;
            next_read -= fq->file->ps.offset;
            next_read -= fq->offset;
            next_read += 31;
            next_read &= ~31;
        }

        char* p = (char*)fq->buf;
        p += fq->offset;
        DVDReadAsync(&fq->file->ps.fileInfo, p, next_read, fq->file->ps.offset + fq->offset,
                     async_cb);
    }
}

void iFileAsyncService()
{
    S32 i;

    for (i = 0; i < 32; i++)
    {
        if (file_queue[i].callback &&
            (file_queue[i].stat == IFILE_RDSTAT_DONE || file_queue[i].stat == IFILE_RDSTAT_FAIL))
        {
            file_queue[i].callback(file_queue[i].file);
            file_queue[i].callback = NULL;
            file_queue[i].file->ps.asynckey = -1;
        }
    }
}

// '(S32)key + i' recomputes key + i, as retail and BfBB do, instead of reusing the stored sum.
// The key and the read size are built statement by statement, like async_cb's roundings.
S32 iFileReadAsync(tag_xFile* file, void* buf, U32 aSize, void (*callback)(tag_xFile*),
                   S32 priority)
{
    static U32 fopcount = 1;
    S32 i;

    for (i = 0; i < 32; i++)
    {
        file_queue_entry& fq = file_queue[i];
        long read_size;
        unsigned long key;

        if (fq.callback == NULL && fq.stat != IFILE_RDSTAT_QUEUED && fq.stat != IFILE_RDSTAT_INPROG)
        {
            key = fopcount++;
            key <<= 2;

            fq.file = file;
            fq.buf = buf;
            fq.size = aSize;
            fq.offset = 0;
            fq.stat = IFILE_RDSTAT_QUEUED;
            fq.callback = callback;
            fq.asynckey = key + i;

            read_size = 32;
            if (aSize < 32)
            {
                read_size = aSize;
                read_size += 3;
                read_size &= ~3;
            }

            DVDSetUserData((DVDCommandBlock*)&file->ps.fileInfo, (void*)i);

            DVDReadAsync(&file->ps.fileInfo, buf, read_size, file->ps.offset, async_cb);

            file->ps.asynckey = key + i;

            return (S32)key + i;
        }
    }

    return -1;
}

IFILE_READSECTOR_STATUS iFileReadAsyncStatus(S32 key, S32* amtToFar)
{
    // The unsigned index and mask give retail's register order in the copy inlined into
    // iCSAsyncReadCB.
    unsigned long i = key & 0x3U;

    if (key != file_queue[i].asynckey)
    {
        return IFILE_RDSTAT_EXPIRED;
    }

    if (amtToFar)
    {
        *amtToFar = file_queue[i].offset;
    }

    return file_queue[i].stat;
}

U32 iFileClose(tag_xFile* file)
{
    tag_iFile* ps = &file->ps;
    S32 ret;

    ret = DVDClose(&ps->fileInfo);
    ret = DVDClose(&ps->fileInfo);

    if (!ret)
    {
        return 1;
    }

    ps->flags = 0;
    return 0;
}

U32 iFileGetSize(tag_xFile* file)
{
    return file->ps.fileInfo.length;
}

U8 iFileExists(const char* name, S32 flags)
{
    char fullname[128];
    S32 index;

    iFileFullPath(name, fullname);
    index = DVDConvertPathToEntrynum(fullname);

    return index != -1;
}

inline void iFileFullPath(const char* relname, char* fullname)
{
    const char* inchar = relname;
    char* outchar = fullname;

    while (*inchar != '\0')
    {
        if (*inchar == '\\')
        {
            *outchar = '/';
        }
        else
        {
            *outchar = *inchar;
        }

        inchar++;
        outchar++;
    }

    *outchar = '\0';
}

void iFileSetPath(const char* path)
{
    U32 len = strlen(path);

    strcpy(gHostPath, path);
    if (len != 0 && gHostPath[len - 1] != '/' && gHostPath[len - 1] != '\\')
    {
        gHostPath[len] = '/';
        gHostPath[len + 1] = '\0';
    }
}

U32 iFileFind(const char* name, S32 fileflags, tag_xFile* file)
{
    return iFileOpen(name, fileflags, file);
}

void iFileGetInfo(tag_xFile* file, U32* starting_sector, U32* size_in_bytes)
{
    if (starting_sector)
    {
        *starting_sector = file->ps.fileInfo.startAddr;
    }

    if (size_in_bytes)
    {
        *size_in_bytes = file->ps.fileInfo.length;
    }
}

U32 iFileGetSectorSize(xFileDriveType drive)
{
    switch (drive)
    {
    case XFILE_DRIVE_CDVD:
        return 32;
    default:
        // Ratatouille's debug build prints this before an xFAIL. The compiled-out iprintf still
        // references drive, which is why the inlined copies keep the switch unfolded.
        iprintf("Unknown drive type in iFileGetSectorSize:  %d\n", drive);
        return 0;
    }
}
