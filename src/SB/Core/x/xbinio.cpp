#include "xbinio.h"
#include "xDebug.h"

static U32 g_loadlock = 0xFFFFFF00;
static st_FILELOADINFO g_loadinst[8] = {};
static tag_xFile g_xfload[8] = {};
static st_BINIO_XTRADATA g_xtraload[8] = {};
static st_BINIO_XTRADATA* g_async_context = NULL;

#define LITTLE_ENDIAN 0
#define BIG_ENDIAN 1

#ifdef GAMECUBE
#define ENDIAN BIG_ENDIAN
#else
#define ENDIAN LITTLE_ENDIAN
#endif

static tag_xFile* BFD_open(const char* filename, const char* mode, U32 lockid, S32, void* xtradata,
                           S32 fileflags);
static S32 BFD_AsyncRead(st_FILELOADINFO* fli, S32 pos, void* data, S32 size, S32 n, S32 endian);

static void LoadDestroy(st_FILELOADINFO* fli);
static S32 SkipBytes(st_FILELOADINFO* fli, S32 fwd);
static S32 ReadSeek(st_FILELOADINFO* fli, S32 pos);
static void DiscardBuffer(st_FILELOADINFO* fli);
static void SetBuffer(st_FILELOADINFO* fli, char* dblbuffer, S32 bufsize);
static S32 ReadBytes(st_FILELOADINFO* fli, char* data, S32 count);
static S32 ReadMShorts(st_FILELOADINFO* fli, S16* data, S32 count);
static S32 ReadMLongs(st_FILELOADINFO* fli, S32* data, S32 count);
static S32 ReadMFloats(st_FILELOADINFO* fli, F32* data, S32 count);
static S32 ReadMDoubles(st_FILELOADINFO* fli, F64* data, S32 count);
static S32 ReadIShorts(st_FILELOADINFO* fli, S16* data, S32 count);
static S32 ReadILongs(st_FILELOADINFO* fli, S32* data, S32 count);
static S32 ReadIFloats(st_FILELOADINFO* fli, F32* data, S32 count);
static S32 ReadIDoubles(st_FILELOADINFO* fli, F64* data, S32 count);
static S32 AsyncMRead(st_FILELOADINFO* fli, S32 offset, char* data, S32 size, S32 n);
static S32 AsyncIRead(st_FILELOADINFO* fli, S32 offset, char* data, S32 size, S32 n);
static en_BIO_ASYNC_ERRCODES AsyncReadStatus(st_FILELOADINFO* fli);
static S32 BFD_read(void* data, S32 elesize, S32 elecnt, tag_xFile* bffp, void* xtradata);

static inline S32 BFD_startSector(const char* filename, S32 fileflags)
{
    tag_xFile file = {};
    U32 addr = 0;
    U32 length = 0;

    if (!iFileFind(filename, fileflags, &file))
    {
        iFileGetInfo(&file, &addr, &length);
    }

    iFileClose(&file);

    return addr;
}

static inline S32 BFD_getLength(tag_xFile* bffp, void* xtradata)
{
    return iFileGetSize(bffp);
}

st_FILELOADINFO* xBinioLoadCreate(const char* filename, S32 fileflags)
{
    st_FILELOADINFO* fli = NULL;
    tag_xFile* tmp_fp;
    S32 i;
    S32 uselock = -1;

    for (i = 0; i < 8; i++)
    {
        if (!(g_loadlock & (1 << i)))
        {
            g_loadlock |= 1 << i;

            uselock = i;
            fli = &g_loadinst[i];

            break;
        }
    }

    if (fli)
    {
        memset(fli, 0, sizeof(st_FILELOADINFO));

        fli->lockid = uselock & ~(uselock >> 31);
        fli->xtradata = &g_xtraload[uselock];
        fli->destroy = LoadDestroy;
        fli->readBytes = ReadBytes;
        fli->readMShorts = ReadMShorts;
        fli->readMLongs = ReadMLongs;
        fli->readMFloats = ReadMFloats;
        fli->readMDoubles = ReadMDoubles;
        fli->readIShorts = ReadIShorts;
        fli->readILongs = ReadILongs;
        fli->readIFloats = ReadIFloats;
        fli->readIDoubles = ReadIDoubles;
        fli->asyncIRead = AsyncIRead;
        fli->asyncMRead = AsyncMRead;
        fli->asyncReadStatus = AsyncReadStatus;
        fli->skipBytes = SkipBytes;
        fli->seekSpot = ReadSeek;
        fli->setDoubleBuf = SetBuffer;
        fli->discardDblBuf = DiscardBuffer;
        fli->error = FIOERR_NONE;
        fli->remain = 0;
        fli->basesector = 0;
        fli->filesize = 0;
        fli->position = 0;
        fli->basesector = 0;

        tmp_fp = BFD_open(filename, "rb", uselock, 0, fli->xtradata, fileflags);

        if (tmp_fp)
        {
            fli->basesector = BFD_startSector(filename, fileflags);
            fli->privdata = tmp_fp;
            fli->filesize = BFD_getLength(tmp_fp, fli->xtradata);
            fli->remain = fli->filesize;
        }
        else
        {
            LoadDestroy(fli);
            fli = NULL;
        }
    }

    return fli;
}

static inline void BFD_close(tag_xFile* bffp, void* xtradata)
{
    st_BINIO_XTRADATA* xtra = (st_BINIO_XTRADATA*)xtradata;

    iFileClose(bffp);

    xtra->fpos = 0;
    xtra->dbl_buf = NULL;
    xtra->dblbuf_size = 0;
    xtra->dbl_beg = 0;
    xtra->dbl_end = 0;
    xtra->dbl_amt = 0;
}

static void LoadDestroy(st_FILELOADINFO* fli)
{
    tag_xFile* fp = (tag_xFile*)fli->privdata;

    if (fp)
    {
        BFD_close(fp, fli->xtradata);
    }

    U32 lockid = fli->lockid;

    memset(fli, 0, sizeof(st_FILELOADINFO));

    g_loadlock &= ~(1 << lockid);
}

static inline S32 BFD_seek(tag_xFile* bffp, S32 offset, S32 whence, void* xtradata)
{
    st_BINIO_XTRADATA* xtra = (st_BINIO_XTRADATA*)xtradata;
    S32 pos = iFileSeek(bffp, offset, whence);
    S32 ret = (pos == -1) ? 1 : 0;

    xtra->fpos = offset;

    return ret;
}

static S32 SkipBytes(st_FILELOADINFO* fli, S32 fwd)
{
    tag_xFile* file = (tag_xFile*)fli->privdata;

    if (fli->error != FIOERR_NONE)
    {
        return 0;
    }

    if (fwd == 0)
    {
        return 1;
    }

    if (fwd < 0)
    {
        fwd = 0;
    }

    if (BFD_seek(file, fli->position + fwd, IFILE_SEEK_SET, fli->xtradata) != 0)
    {
        fli->error = FIOERR_SEEKFAIL;
    }
    else
    {
        fli->remain -= fwd;
        fli->position += fwd;
    }

    if (fli->error != FIOERR_NONE)
    {
        return 0;
    }

    return 1;
}

static S32 ReadSeek(st_FILELOADINFO* fli, S32 pos)
{
    tag_xFile* file = (tag_xFile*)fli->privdata;

    if (fli->error != FIOERR_NONE)
    {
        return 0;
    }

    pos = MIN(pos, fli->filesize);

    if (BFD_seek(file, pos, IFILE_SEEK_SET, fli->xtradata) != 0)
    {
        fli->error = FIOERR_SEEKFAIL;
    }
    else
    {
        fli->position = pos;
        fli->remain = fli->filesize - pos;
    }

    if (fli->error != FIOERR_NONE)
    {
        return 0;
    }

    return 1;
}

static void SetBuffer(st_FILELOADINFO* fli, char* dblbuffer, S32 bufsize)
{
    st_BINIO_XTRADATA* xtra = (st_BINIO_XTRADATA*)fli->xtradata;

    if (xtra)
    {
        xtra->dbl_buf = dblbuffer;
        xtra->dblbuf_size = bufsize;
        xtra->dbl_beg = 0;
        xtra->dbl_end = 0;
        xtra->dbl_amt = 0;
    }
}

static void DiscardBuffer(st_FILELOADINFO* fli)
{
    SetBuffer(fli, NULL, 0);
}

static inline S32 ReadRaw(st_FILELOADINFO* fli, void* data, S32 size, S32 count)
{
    tag_xFile* file = (tag_xFile*)fli->privdata;

    if (fli->error != FIOERR_NONE)
    {
        return 0;
    }

    S32 amt = (size * count > fli->remain) ? fli->remain / size : count;

    if (amt)
    {
        S32 n = BFD_read(data, size, amt, file, fli->xtradata);

        if (n != amt)
        {
            fli->error = FIOERR_READFAIL;
        }

        fli->remain -= size * amt;
        fli->position += size * amt;
    }

    return amt;
}

static S32 ReadBytes(st_FILELOADINFO* fli, char* data, S32 count)
{
    S32 act = ReadRaw(fli, data, sizeof(char), count);
    return act;
}

static inline void Swap2(char* d, S32 n)
{
    char t;

    while (n--)
    {
        t = d[0];
        d[0] = d[1];
        d[1] = t;

        d += 2;
    }
}

static S32 ReadMShorts(st_FILELOADINFO* fli, S16* data, S32 count)
{
    S32 act = ReadRaw(fli, data, sizeof(S16), count);

#if ENDIAN == LITTLE_ENDIAN
    Swap2((char*)data, act);
#endif

    return act;
}

static inline void Swap4(char* d, S32 n)
{
    char t;

    while (n--)
    {
        t = d[0];
        d[0] = d[3];
        d[3] = t;

        t = d[1];
        d[1] = d[2];
        d[2] = t;

        d += 4;
    }
}

static S32 ReadMLongs(st_FILELOADINFO* fli, S32* data, S32 count)
{
    S32 act = ReadRaw(fli, data, sizeof(S32), count);

#if ENDIAN == LITTLE_ENDIAN
    Swap4((char*)data, act);
#endif

    return act;
}

static S32 ReadMFloats(st_FILELOADINFO* fli, F32* data, S32 count)
{
    S32 act = ReadRaw(fli, data, sizeof(F32), count);

#if ENDIAN == LITTLE_ENDIAN
    Swap4((char*)data, act);
#endif

    return act;
}

static inline void Swap8(char* d, S32 n)
{
    char t;

    while (n--)
    {
        t = d[0];
        d[0] = d[7];
        d[7] = t;

        t = d[1];
        d[1] = d[6];
        d[6] = t;

        t = d[2];
        d[2] = d[5];
        d[5] = t;

        t = d[3];
        d[3] = d[4];
        d[4] = t;

        d += 8;
    }
}

static S32 ReadMDoubles(st_FILELOADINFO* fli, F64* data, S32 count)
{
    S32 act = ReadRaw(fli, data, sizeof(F64), count);

#if ENDIAN == LITTLE_ENDIAN
    Swap8((char*)data, act);
#endif

    return act;
}

static S32 ReadIShorts(st_FILELOADINFO* fli, S16* data, S32 count)
{
    S32 act = ReadRaw(fli, data, sizeof(S16), count);

#if ENDIAN == BIG_ENDIAN
    Swap2((char*)data, act);
#endif

    return act;
}

static S32 ReadILongs(st_FILELOADINFO* fli, S32* data, S32 count)
{
    S32 act = ReadRaw(fli, data, sizeof(S32), count);

#if ENDIAN == BIG_ENDIAN
    Swap4((char*)data, act);
#endif

    return act;
}

static S32 ReadIFloats(st_FILELOADINFO* fli, F32* data, S32 count)
{
    S32 act = ReadRaw(fli, data, sizeof(F32), count);

#if ENDIAN == BIG_ENDIAN
    Swap4((char*)data, act);
#endif

    return act;
}

static S32 ReadIDoubles(st_FILELOADINFO* fli, F64* data, S32 count)
{
    S32 act = ReadRaw(fli, data, sizeof(F64), count);

#if ENDIAN == BIG_ENDIAN
    Swap8((char*)data, act);
#endif

    return act;
}

static S32 AsyncMRead(st_FILELOADINFO* fli, S32 offset, char* data, S32 size, S32 n)
{
    return BFD_AsyncRead(fli, offset, data, size, n, BIG_ENDIAN);
}

static S32 AsyncIRead(st_FILELOADINFO* fli, S32 offset, char* data, S32 size, S32 n)
{
    return BFD_AsyncRead(fli, offset, data, size, n, LITTLE_ENDIAN);
}

static inline en_BIO_ASYNC_ERRCODES BFD_AsyncReadStatus(st_FILELOADINFO* fli)
{
    en_BIO_ASYNC_ERRCODES status;
    st_BINIO_XTRADATA* xtra = (st_BINIO_XTRADATA*)fli->xtradata;

    if (!g_async_context)
    {
        status = BINIO_ASYNC_NOOP;
    }
    else if (xtra != g_async_context)
    {
        status = BINIO_ASYNC_INPROG;
    }
    else
    {
        if (xtra->asyn_status == BINIO_ASYNC_INPROG)
        {
            XFILE_READSECTOR_STATUS xrdstat;
            S32 amtsofar = 0;

            xrdstat = xFileReadAsyncStatus(xtra->gcaskey, &amtsofar);

            switch (xrdstat)
            {
            case XFILE_RDSTAT_INPROG:
            case XFILE_RDSTAT_QUEUED:
            {
                xtra->asyn_status = BINIO_ASYNC_INPROG;
                break;
            }
            case XFILE_RDSTAT_DONE:
            {
                xtra->asyn_status = BINIO_ASYNC_DONE;
                break;
            }
            case XFILE_RDSTAT_FAIL:
            case XFILE_RDSTAT_EXPIRED:
            default:
            {
                xtra->asyn_status = BINIO_ASYNC_FAIL;
                break;
            }
            }
        }

        status = xtra->asyn_status;

        if (status == BINIO_ASYNC_FAIL || status == BINIO_ASYNC_DONE)
        {
            xtra->asyn_status = BINIO_ASYNC_NOOP;
            g_async_context = NULL;
        }
    }

    return status;
}

static en_BIO_ASYNC_ERRCODES AsyncReadStatus(st_FILELOADINFO* fli)
{
    return BFD_AsyncReadStatus(fli);
}

// Retail calls this rather than inlining it: the compiled-out checks (Ratatouille's validates, plus
// placeholders at two of its warning sites) keep it over the inlining limit. XFILE_RELNAME_MAX is
// Ratatouille's name and is not defined in TSSM; the compiled-out macro drops it.
static tag_xFile* BFD_open(const char* filename, const char* mode, U32 lockid, S32, void* xtradata,
                           S32 fileflags)
{
    tag_xFile* bffp;
    U32 orc; // open return code
    st_BINIO_XTRADATA* xtra = (st_BINIO_XTRADATA*)xtradata;
    S32 xfflg = IFILE_OPEN_READ;

    xVALIDATE(strlen(filename) < (XFILE_RELNAME_MAX - 1));

    if (strcmp(mode, "rb") == 0)
    {
        bffp = &g_xfload[lockid];
        xfflg = IFILE_OPEN_READ;
    }
    else if (strcmp(mode, "wb") == 0)
    {
        xASSERT_PLACEHOLDER(); // Rat: "WARNING: BINIO write operation nonsupported"
        bffp = NULL;
        xfflg = IFILE_OPEN_WRITE;
    }
    else
    {
        xASSERT_PLACEHOLDER(); // Rat: "Unknown/Unsupported file open state request (%s)"
        bffp = NULL;
    }

    xVALIDATE(bffp);

    if (bffp)
    {
        strncpy(bffp->relname, filename, sizeof(bffp->relname) - 1);
        bffp->relname[sizeof(bffp->relname) - 1] = '\0';

        if (fileflags)
        {
            xfflg |= fileflags;
        }

        orc = iFileOpen(filename, xfflg, bffp);

        if (orc != 0)
        {
            bffp = NULL;
        }
        else
        {
            xtra->fpos = 0;
            xtra->dbl_buf = NULL;
            xtra->dblbuf_size = 0;
            xtra->dbl_beg = 0;
            xtra->dbl_end = 0;
            xtra->dbl_amt = 0;
        }
    }

    return bffp;
}

static S32 BFD_read(void* data, S32 elesize, S32 elecnt, tag_xFile* bffp, void* xtradata)
{
    st_BINIO_XTRADATA* xtra = (st_BINIO_XTRADATA*)xtradata;
    char* dest = (char*)data;
    S32 readbeg;
    S32 refill = 0;
    S32 remain;
    S32 actual;
    U32 holdpos;
    // not in the DWARF order (safety, numBytes): that order changes the generated code
    U32 numBytes = 0;
    U32 safety = 0;
    // not in the DWARF: the byte count is used throughout.
    S32 total = elesize * elecnt;

    if (total == 0)
    {
        return 0;
    }

    if (!xtra->dbl_buf || xtra->dblbuf_size < 1 || total > xtra->dblbuf_size)
    {
        iFileSeek(bffp, xtra->fpos, IFILE_SEEK_SET);
        numBytes = iFileRead(bffp, dest, total);

        xtra->fpos += numBytes;
    }
    else
    {
        remain = total;

        while (true)
        {
            if (xtra->fpos < xtra->dbl_beg)
            {
                refill = 1;

                xtra->dbl_beg = xtra->fpos - (xtra->fpos % xtra->dblbuf_size);
                xtra->dbl_end = xtra->dbl_beg + xtra->dblbuf_size;
                xtra->dbl_amt = 0;
            }
            else if (xtra->fpos >= xtra->dbl_end)
            {
                refill = 1;

                xtra->dbl_beg = xtra->fpos - (xtra->fpos % xtra->dblbuf_size);
                xtra->dbl_end = xtra->dbl_beg + xtra->dblbuf_size;
                xtra->dbl_amt = 0;
            }

            if (refill)
            {
                holdpos = xtra->fpos;

                iFileSeek(bffp, xtra->dbl_beg, IFILE_SEEK_SET);

                xtra->fpos = holdpos;
                xtra->dbl_amt = iFileRead(bffp, xtra->dbl_buf, xtra->dblbuf_size);
            }

            readbeg = xtra->fpos - xtra->dbl_beg;

            if (xtra->dbl_amt > readbeg)
            {
                actual = MIN(remain, xtra->dbl_amt - readbeg);

                memcpy(dest + numBytes, xtra->dbl_buf + readbeg, actual);

                numBytes += actual;
                remain -= actual;

                xtra->fpos += actual;
            }

            if ((S32)numBytes == total || xtra->dbl_amt < 1 || safety++ > 60000)
            {
                break;
            }
        }
    }

    return (S32)numBytes / elesize;
}

static void BFD_cb_GCP2_readasync(tag_xFile* bffp)
{
}

// Retail calls this from AsyncIRead / AsyncMRead rather than inlining it: the compiled-out
// checks (Ratatouille's validates, plus placeholders at its warning sites) keep it over the
// inlining limit.
static S32 BFD_AsyncRead(st_FILELOADINFO* fli, S32 pos, void* data, S32 size, S32 n, S32 endian)
{
    tag_xFile* file = (tag_xFile*)fli->privdata;
    st_BINIO_XTRADATA* xtra = (st_BINIO_XTRADATA*)fli->xtradata;
    S32 result;
    S32 rc;

    xASSERT_PLACEHOLDER();
    xASSERT_PLACEHOLDER();
    xVALIDATE(pos > 0);

    if (fli->error != FIOERR_NONE)
    {
        return 0;
    }

    xVALIDATE(g_async_context == NULL);
    if (g_async_context)
    {
        return 0;
    }

    g_async_context = xtra;

    xtra->asyn_status = BINIO_ASYNC_INPROG;
    xtra->asyn_data = (char*)data;
    xtra->asyn_elesize = size;
    xtra->asyn_amt = n;
    xtra->asyn_ismot = 1;

    rc = iFileSeek(file, pos, IFILE_SEEK_SET);
    xVALIDATE(rc == pos);
    xFileSetUserData(file, fli);

    rc = iFileReadAsync(file, data, size * n, BFD_cb_GCP2_readasync, 0);
    xVALIDATE(rc >= 0);

    if (rc < 0)
    {
        result = 0;
    }
    else
    {
        xtra->gcaskey = rc;
        result = 1;
    }

    if (result == 0)
    {
        g_async_context = NULL;
        xtra->asyn_status = BINIO_ASYNC_NOOP;
        xASSERT_PLACEHOLDER(); // Rat: "XBIN: GCAsync queue failed"
    }

    return 1;
}
