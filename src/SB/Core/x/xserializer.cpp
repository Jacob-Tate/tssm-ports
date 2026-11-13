#include "xserializer.h"
#include "xDebug.h"

// PC port: a little-endian host writes multi-byte values into the bit stream in the console's
// (big-endian) byte order, so saves stay interchangeable. The GameCube build is unchanged.
#if defined(PLATFORM_PC) &&                                                                        \
    ((defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) || defined(_M_IX86) || \
     defined(_M_X64) || defined(_M_ARM64))
#define XSER_ELEMENT_BYTE(data, elesize, byteidx)                                                  \
    ((data) + ((byteidx) - (byteidx) % (elesize)) + ((elesize) - 1 - (byteidx) % (elesize)))
#endif

static S32 g_serinit;
static st_XSERIAL_DATA_PRIV g_xserdata = { 0, NULL, 0, NULL, NULL, { NULL, 0, 0, 0 } };
static S32 g_tbl_onbit[32] = {};
static S32 g_tbl_clear[32] = {};

static S32 xSER_ord_test(const void* key, void* elt);
static S32 xSER_ord_compare(void* e1, void* e2);
static void xSER_init_buffers(S32 count, st_SERIAL_PERCID_SIZE* sizeinfo);
static S32 xSER_xsgclt_svinfo_ver(void*, st_XSAVEGAME_DATA*, S32* cur_space, S32* max_fullgame);
static S32 xSER_xsgclt_svproc_ver(void*, st_XSAVEGAME_DATA* xsg, st_XSAVEGAME_WRITECONTEXT* wctxt);
static S32 xSER_xsgclt_svinfo_clt(void* cltdata, st_XSAVEGAME_DATA*, S32* cur_space,
                                  S32* max_fullgame);
static S32 xSER_xsgclt_svproc_clt(void* cltdata, st_XSAVEGAME_DATA* xsg,
                                  st_XSAVEGAME_WRITECONTEXT* wctxt);
static S32 xSER_xsgclt_svinfo_fill(void*, st_XSAVEGAME_DATA*, S32* cur_space, S32* max_fullgame);
static S32 xSER_xsgclt_svproc_fill(void*, st_XSAVEGAME_DATA* xsg, st_XSAVEGAME_WRITECONTEXT* wctxt);
static S32 xSER_xsgclt_ldproc_ver(void*, st_XSAVEGAME_DATA* xsg, st_XSAVEGAME_READCONTEXT* rctxt,
                                  U32, S32);
static S32 xSER_xsgclt_ldproc_clt(void*, st_XSAVEGAME_DATA* xsg, st_XSAVEGAME_READCONTEXT* rctxt,
                                  U32 idtag, S32);
static S32 xSER_xsgclt_ldproc_fill(void*, st_XSAVEGAME_DATA* xsg, st_XSAVEGAME_READCONTEXT* rctxt,
                                   U32, S32);

inline st_SERIAL_CLIENTINFO* XSER_get_client(U32 idtag)
{
    st_XSERIAL_DATA_PRIV* xsd = &g_xserdata;
    st_SERIAL_CLIENTINFO* clt;
    S32 idx = XOrdLookup(&xsd->cltlist, (void*)idtag, xSER_ord_test);

    if (idx < 0)
    {
        clt = xsd->cltnext;
        xsd->cltnext++;
        clt->idtag = idtag;
        XOrdInsert(&xsd->cltlist, clt, xSER_ord_compare);
    }
    else
    {
        clt = (st_SERIAL_CLIENTINFO*)xsd->cltlist.list[idx];
    }

    return clt;
}

inline void xSER_init_tables()
{
    for (S32 i = 0; i < 32; i++)
    {
        g_tbl_onbit[i] = 1 << i;
        g_tbl_clear[i] = ~(1 << i);
    }
}

S32 xSerialStartup(S32 count, st_SERIAL_PERCID_SIZE* sizeinfo)
{
    if (g_serinit++ == 0)
    {
        memset(&g_xserdata, 0, sizeof(st_XSERIAL_DATA_PRIV));
        xSER_init_tables();
        xSER_init_buffers(count, sizeinfo);
    }

    return g_serinit;
}

S32 xSerialShutdown()
{
    return --g_serinit;
}

void xSerialTraverse(S32 (*func)(U32, xSerial*))
{
    S32 i;
    st_XSERIAL_DATA_PRIV* xsd = &g_xserdata;
    st_SERIAL_CLIENTINFO* clt;
    xSerial xser;
    S32 rc;

    for (i = 0; i < xsd->cltlist.cnt; i++)
    {
        clt = (st_SERIAL_CLIENTINFO*)xsd->cltlist.list[i];
        xser.setClient(clt->idtag);
        rc = func(clt->idtag, &xser);
        if (rc == 0)
        {
            break;
        }
    }
}

xSerial::~xSerial()
{
}

void xSerial::setClient(U32 idtag)
{
    st_SERIAL_CLIENTINFO* clt = XSER_get_client(idtag);

    this->idtag = clt->idtag;
    baseoff = clt->trueoff;
    ctxtdata = clt;
    warned = FALSE;
    curele = 0;
    bitidx = 0;
    bittally = 0;
}

// The checks are Ratatouille's debug-only validates; compiled out, they keep this over the inlining
// limit, as retail calls it from the wrappers below and from xTimerSave.
S32 xSerial::Write(char* data, S32 elesize, S32 n)
{
    S32 nbit;

    xVALIDATE(n != 0);

    if (n == 0)
    {
        return 0;
    }

    nbit = n > 0 ? n * elesize * 8 : -n;

    xVALIDATE(nbit > 0);

    if (n < 0)
    {
        xVALIDATE(elesize == 4);

        S32 bidx = 0;
        S32* iptr = (S32*)data;
        for (S32 i = 0; i < nbit; i++)
        {
            wrbit(*iptr & g_tbl_onbit[bidx]);
            if (++bidx == 32)
            {
                bidx = 0;
                iptr++;
            }
        }
    }
    else
    {
        xVALIDATE((elesize == 1) || (elesize == 2) || (elesize == 4));

        S32 bidx = 0;
        char* cptr = data;
        for (S32 i = 0; i < nbit; i++)
        {
#ifdef XSER_ELEMENT_BYTE
            wrbit(*XSER_ELEMENT_BYTE(data, elesize, i >> 3) & g_tbl_onbit[bidx]);
#else
            wrbit(*cptr & g_tbl_onbit[bidx]);
#endif
            bidx++;
            if (bidx == 8)
            {
                bidx = 0;
                cptr++;
            }
        }
    }

    return nbit;
}

S32 xSerial::Write_bn(U32 bits, S32 n)
{
    return Write((char*)&bits, 4, -n);
}

S32 xSerial::Write_b1(S32 bits)
{
    return Write((char*)&bits, 4, -1);
}

S32 xSerial::Write(U8 data)
{
    return Write((char*)&data, 1, 1);
}

S32 xSerial::Write(S16 data)
{
    return Write((char*)&data, 2, 1);
}

S32 xSerial::Write(S32 data)
{
    return Write((char*)&data, 4, 1);
}

S32 xSerial::Write(U32 data)
{
    return Write((char*)&data, 4, 1);
}

S32 xSerial::Write(F32 data)
{
    return Write((char*)&data, 4, 1);
}

S32 xSerial::Read(char* buf, S32 elesize, S32 n)
{
    S32 nbit = n > 0 ? n * elesize * 8 : -n;

    if (n < 0)
    {
        S32* iptr = (S32*)buf;
        S32 bidx = 0;
        for (S32 i = 0; i < nbit; i++)
        {
            S32 bitval = rdbit();
            if (bitval)
            {
                *iptr |= g_tbl_onbit[bidx];
            }
            else
            {
                *iptr &= g_tbl_clear[bidx];
            }

            if (++bidx == 32)
            {
                bidx = 0;
                iptr++;
            }
        }
    }
    else
    {
        char* cptr = buf;
        S32 bidx = 0;
        for (S32 i = 0; i < nbit; i++)
        {
            S32 bitval = rdbit();
#ifdef XSER_ELEMENT_BYTE
            cptr = XSER_ELEMENT_BYTE(buf, elesize, i >> 3);
#endif
            if (bitval)
            {
                *cptr |= (char)g_tbl_onbit[bidx];
            }
            else
            {
                *cptr &= (char)g_tbl_clear[bidx];
            }

            if (++bidx == 8)
            {
                bidx = 0;
                cptr++;
            }
        }
    }

    return nbit;
}

S32 xSerial::Read_bn(U32* bits, S32 n)
{
    return Read((char*)bits, 4, -n);
}

S32 xSerial::Read_b1(S32* bits)
{
    return Read((char*)bits, 4, -1);
}

S32 xSerial::Read_b1(U32* bits)
{
    return Read((char*)bits, 4, -1);
}

S32 xSerial::Read(U8* buf)
{
    return Read((char*)buf, 1, 1);
}

S32 xSerial::Read(S16* buf)
{
    return Read((char*)buf, 2, 1);
}

S32 xSerial::Read(S32* buf)
{
    return Read((char*)buf, 4, 1);
}

S32 xSerial::Read(U32* buf)
{
    return Read((char*)buf, 4, 1);
}

S32 xSerial::Read(F32* buf)
{
    return Read((char*)buf, 4, 1);
}

inline void xSerial::wrbit(S32 is_on)
{
    st_SERIAL_CLIENTINFO* clt = ctxtdata;

    if (bittally + 1 > clt->actsize * 8)
    {
        warned = TRUE;
        return;
    }

    clt->membuf[curele] &= g_tbl_clear[bitidx];
    if (is_on)
    {
        clt->membuf[curele] |= g_tbl_onbit[bitidx];
    }

    bitidx++;
    if (bitidx == 32)
    {
        curele++;
        bitidx = 0;
    }
    bittally++;
}

inline S32 xSerial::rdbit()
{
    st_SERIAL_CLIENTINFO* clt = ctxtdata;
    bool is_on;

    if (bittally + 1 > clt->actsize * 8)
    {
        warned = TRUE;
        return 0;
    }

    is_on = clt->membuf[curele] & g_tbl_onbit[bitidx];

    bitidx++;
    if (bitidx == 32)
    {
        curele++;
        bitidx = 0;
    }
    bittally++;

    return is_on;
}

void xSerialWipeMainBuffer()
{
    memset(g_xserdata.bitbuf, 0, g_xserdata.buf_bytcnt);
}

static void xSER_init_buffers(S32 count, st_SERIAL_PERCID_SIZE* sizeinfo)
{
    st_XSERIAL_DATA_PRIV* xsd = &g_xserdata;
    S32 i = 0;
    S32 tally = 0;
    S32 sicnt = 0;
    st_SERIAL_PERCID_SIZE* sitmp = NULL;
    st_SERIAL_CLIENTINFO* tmp_clt = NULL;

    XOrdInit(&g_xserdata.cltlist, count, 0);

    // count * 16 rather than sizeof at the memset: with sizeof at both sites the compiler reuses
    // the product, but retail computes it again.
    xsd->cltbuf =
        (st_SERIAL_CLIENTINFO*)xMemAlloc(gActiveHeap, count * sizeof(st_SERIAL_CLIENTINFO), 0);
    memset(xsd->cltbuf, 0, count * 16);
    xsd->cltnext = xsd->cltbuf;

    sitmp = sizeinfo;
    while (sitmp->idtag != 0)
    {
        tally += (sitmp->needsize + 3) & ~3;
        sicnt++;
        sitmp++;
    }
    tally += (count - sicnt) * 400;

    xsd->bitbuf = (S32*)xMemAlloc(gActiveHeap, tally, 0);
    memset(xsd->bitbuf, 0, tally);

    xsd->buf_bytcnt = tally;

    sitmp = sizeinfo;
    tally = 0;
    tmp_clt = xsd->cltnext;
    while (sitmp->idtag != 0)
    {
        tmp_clt->idtag = sitmp->idtag;
        tmp_clt->trueoff = tally;
        tmp_clt->actsize = (sitmp->needsize + 3) & ~3;
        tmp_clt->membuf = xsd->bitbuf + tally / 4;
        XOrdAppend(&xsd->cltlist, tmp_clt);
        tally += tmp_clt->actsize;
        sitmp++;
        tmp_clt++;
    }
    XOrdSort(&xsd->cltlist, xSER_ord_compare);
    xsd->cltnext = tmp_clt;

    for (i = sicnt; i < count; i++)
    {
        tmp_clt->idtag = 0;
        tmp_clt->trueoff = tally;
        tmp_clt->actsize = 400;
        tmp_clt->membuf = xsd->bitbuf + tally / 4;
        tally += tmp_clt->actsize;
        tmp_clt++;
    }
}

static S32 xSER_ord_compare(void* e1, void* e2)
{
    S32 rc;

    if (((st_SERIAL_CLIENTINFO*)e1)->idtag < ((st_SERIAL_CLIENTINFO*)e2)->idtag)
    {
        rc = -1;
    }
    else if (((st_SERIAL_CLIENTINFO*)e1)->idtag > ((st_SERIAL_CLIENTINFO*)e2)->idtag)
    {
        rc = 1;
    }
    else
    {
        rc = 0;
    }

    return rc;
}

static S32 xSER_ord_test(const void* key, void* elt)
{
    S32 rc;
    U32 idtag = (U32)(UPtr)key;

    if (idtag < ((st_SERIAL_CLIENTINFO*)elt)->idtag)
    {
        rc = -1;
    }
    else if (idtag > ((st_SERIAL_CLIENTINFO*)elt)->idtag)
    {
        rc = 1;
    }
    else
    {
        rc = 0;
    }

    return rc;
}

S32 xSerial_svgame_register(st_XSAVEGAME_DATA* sgctxt, en_SAVEGAME_MODE mode)
{
    st_XSERIAL_DATA_PRIV* xsd = &g_xserdata;
    st_SERIAL_CLIENTINFO* clt;
    S32 i;

    xsd->flg_info &= ~1;

    if (mode == XSG_MODE_SAVE)
    {
        xSGAddSaveClient(sgctxt, 'SVID', xsd, xSER_xsgclt_svinfo_ver, xSER_xsgclt_svproc_ver);

        for (i = 0; i < xsd->cltlist.cnt; i++)
        {
            clt = (st_SERIAL_CLIENTINFO*)xsd->cltlist.list[i];
            xSGAddSaveClient(sgctxt, clt->idtag, clt, xSER_xsgclt_svinfo_clt,
                             xSER_xsgclt_svproc_clt);
        }

        xSGAddSaveClient(sgctxt, 'SFIL', &g_xserdata, xSER_xsgclt_svinfo_fill,
                         xSER_xsgclt_svproc_fill);
    }
    else if (mode == XSG_MODE_LOAD)
    {
        xSGAddLoadClient(sgctxt, 'SVID', xsd, xSER_xsgclt_ldproc_ver);
        xSGAddLoadClient(sgctxt, 0, &g_xserdata, xSER_xsgclt_ldproc_clt);
        xSGAddLoadClient(sgctxt, 'SFIL', &g_xserdata, xSER_xsgclt_ldproc_fill);
    }

    return 1;
}

static S32 xSER_xsgclt_svinfo_ver(void*, st_XSAVEGAME_DATA*, S32* cur_space, S32* max_fullgame)
{
    *cur_space = 4;
    *max_fullgame = 4;
    return 1;
}

static S32 xSER_xsgclt_svproc_ver(void*, st_XSAVEGAME_DATA* xsg, st_XSAVEGAME_WRITECONTEXT* wctxt)
{
    S32 ver = 1;

    xSGWriteData(xsg, wctxt, &ver, 1);

    return 1;
}

static S32 xSER_xsgclt_ldproc_ver(void*, st_XSAVEGAME_DATA* xsg, st_XSAVEGAME_READCONTEXT* rctxt,
                                  U32, S32)
{
    st_XSERIAL_DATA_PRIV* xsd = &g_xserdata;
    S32 ver = 0;

    xSGReadData(xsg, rctxt, &ver, 1);

    if (ver != 1)
    {
        xsd->flg_info |= 1;
    }

    return 1;
}

static S32 xSER_xsgclt_svinfo_clt(void* cltdata, st_XSAVEGAME_DATA*, S32* cur_space,
                                  S32* max_fullgame)
{
    st_SERIAL_CLIENTINFO* clt = (st_SERIAL_CLIENTINFO*)cltdata;
    *cur_space = clt->actsize;
    *max_fullgame = clt->actsize;
    return 1;
}

static S32 xSER_xsgclt_svproc_clt(void* cltdata, st_XSAVEGAME_DATA* xsg,
                                  st_XSAVEGAME_WRITECONTEXT* wctxt)
{
    st_SERIAL_CLIENTINFO* clt = XSER_get_client(((st_SERIAL_CLIENTINFO*)cltdata)->idtag);

    xSGWriteData(xsg, wctxt, (char*)clt->membuf, clt->actsize);

    return 1;
}

static S32 xSER_xsgclt_ldproc_clt(void*, st_XSAVEGAME_DATA* xsg, st_XSAVEGAME_READCONTEXT* rctxt,
                                  U32 idtag, S32)
{
    st_SERIAL_CLIENTINFO* clt;

    if (g_xserdata.flg_info & 1)
    {
        return 1;
    }

    clt = XSER_get_client(idtag);
    xSGReadData(xsg, rctxt, (char*)clt->membuf, clt->actsize);

    return 1;
}

static S32 xSER_xsgclt_svinfo_fill(void*, st_XSAVEGAME_DATA*, S32* cur_space, S32* max_fullgame)
{
    st_XSERIAL_DATA_PRIV* xsd = &g_xserdata;
    S32 i;
    S32 tally = 0;
    S32 size;

    for (i = 0; i < xsd->cltlist.cnt; i++)
    {
        tally += ((st_SERIAL_CLIENTINFO*)xsd->cltlist.list[i])->actsize;
    }

    size = xsd->buf_bytcnt - tally + 8;
    *cur_space = size;
    *max_fullgame = size;

    return 1;
}

static S32 xSER_xsgclt_svproc_fill(void*, st_XSAVEGAME_DATA* xsg, st_XSAVEGAME_WRITECONTEXT* wctxt)
{
    char filbuf[9] = { 'R', 'y', 'a', 'n', 'N', 'e', 'i', 'l', '\0' };

    xSGWriteData(xsg, wctxt, filbuf, 8);

    return 1;
}

static S32 xSER_xsgclt_ldproc_fill(void*, st_XSAVEGAME_DATA* xsg, st_XSAVEGAME_READCONTEXT* rctxt,
                                   U32, S32)
{
    char filbuf[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    if (g_xserdata.flg_info & 1)
    {
        return 1;
    }

    xSGReadData(xsg, rctxt, filbuf, 8);

    return 1;
}
