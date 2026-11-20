// The frame grab; see iFrameGrab.h.
//
// The PNG is written uncompressed (deflate "stored" blocks), so it needs no
// zlib: the only arithmetic a reader checks is the CRC-32 of each chunk and
// the Adler-32 of the image data.

#include <rwcore.h>

#include "rw.h"

#include "iFrameGrab.h"
#include "iHost.h"
#include "iSnapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __3DS__
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
    U32 sCrcTable[256];
    bool sCrcReady;

    // The last grab's three parts, for iFrameGrabLastTimes.
    F32 sReadMs;
    F32 sEncodeMs;
    F32 sWriteMs;

    U32 Crc(U32 crc, const U8* p, size_t n)
    {
        if (!sCrcReady)
        {
            for (U32 i = 0; i < 256; i++)
            {
                U32 c = i;
                for (int k = 0; k < 8; k++)
                {
                    c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                }
                sCrcTable[i] = c;
            }
            sCrcReady = true;
        }

        crc = ~crc;
        for (size_t i = 0; i < n; i++)
        {
            crc = sCrcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
        }
        return ~crc;
    }

    void PutBE(U8* p, U32 v)
    {
        p[0] = (U8)(v >> 24);
        p[1] = (U8)(v >> 16);
        p[2] = (U8)(v >> 8);
        p[3] = (U8)v;
    }

    // A chunk at `o`, whose data is already at o + 8; returns past it.
    U8* Chunk(U8* o, const char* type, U32 len)
    {
        PutBE(o, len);
        memcpy(o + 4, type, 4);
        PutBE(o + 8 + len, Crc(0, o + 4, 4 + (size_t)len));
        return o + 12 + len;
    }

    F32 MsSince(U64 fromNs)
    {
        return (F32)((double)(iHostMonotonicNs() - fromNs) / 1e6);
    }

    // The whole file in one write. On the 3DS stdio would hand a large fwrite
    // to the SD card a buffer at a time, each its own file-system call that
    // grows the file; a console capture took about 10 s that way.
    bool WriteWhole(const char* path, const U8* data, size_t n)
    {
#ifdef __3DS__
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
        {
            return false;
        }
        size_t done = 0;
        while (done < n)
        {
            const ssize_t k = write(fd, data + done, n - done);
            if (k <= 0)
            {
                break;
            }
            done += (size_t)k;
        }
        return close(fd) == 0 && done == n;
#else
        FILE* f = fopen(path, "wb");
        if (f == NULL)
        {
            return false;
        }
        const bool ok = fwrite(data, 1, n, f) == n;
        return fclose(f) == 0 && ok;
#endif
    }
} // namespace

// `img` as an RGB PNG at `path`, area-averaged down to at most `maxWidth`
// pixels wide (0: the full size). Destroys `img`.
static S32 WriteImage(const char* path, rw::Image* img, S32 maxWidth)
{
    if (img->depth != 32 || img->width <= 0 || img->height <= 0)
    {
        img->destroy();
        return FALSE;
    }
    const U64 encodeFrom = iHostMonotonicNs();

    const int sw = img->width;
    const int sh = img->height;
    int w = sw;
    int h = sh;
    if (maxWidth > 0 && w > maxWidth)
    {
        w = maxWidth;
        h = (int)((long long)sh * w / sw);
        if (h < 1)
        {
            h = 1;
        }
    }

    // Each row is a filter byte (0, none) and the pixels.
    const size_t rowBytes = 1 + (size_t)w * 3;
    const size_t rawSize = rowBytes * h;

    // The file: signature, IHDR, one IDAT (a zlib stream of stored blocks of
    // at most 65535 bytes), IEND.
    const size_t blocks = rawSize / 65535 + 1;
    const size_t zSize = 2 + rawSize + blocks * 5 + 4;
    const size_t fileSize = 8 + (12 + 13) + (12 + zSize) + 12;
    U8* file = (U8*)malloc(fileSize);
    U8* raw = (U8*)malloc(rawSize);
    if (file == NULL || raw == NULL)
    {
        free(file);
        free(raw);
        img->destroy();
        return FALSE;
    }

    for (int y = 0; y < h; y++)
    {
        U8* row = raw + rowBytes * y;
        row[0] = 0;

        if (w == sw && h == sh)
        {
            const U8* p = img->pixels + y * img->stride;
            for (int x = 0; x < w; x++, p += 4)
            {
                row[1 + x * 3] = p[0];
                row[2 + x * 3] = p[1];
                row[3 + x * 3] = p[2];
            }
            continue;
        }

        // An area average, as the save stills do it.
        const int y0 = (int)((long long)y * sh / h);
        int y1 = (int)((long long)(y + 1) * sh / h);
        if (y1 <= y0)
        {
            y1 = y0 + 1;
        }

        for (int x = 0; x < w; x++)
        {
            const int x0 = (int)((long long)x * sw / w);
            int x1 = (int)((long long)(x + 1) * sw / w);
            if (x1 <= x0)
            {
                x1 = x0 + 1;
            }

            unsigned r = 0, g = 0, b = 0, n = 0;
            for (int sy = y0; sy < y1; sy++)
            {
                const U8* p = img->pixels + sy * img->stride + x0 * 4;
                for (int sx = x0; sx < x1; sx++, p += 4)
                {
                    r += p[0];
                    g += p[1];
                    b += p[2];
                    n++;
                }
            }

            U8* d = row + 1 + x * 3;
            d[0] = (U8)(r / n);
            d[1] = (U8)(g / n);
            d[2] = (U8)(b / n);
        }
    }

    img->destroy();

    static const U8 kSignature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    U8* o = file;
    memcpy(o, kSignature, 8);
    o += 8;

    U8* ihdr = o + 8;
    PutBE(ihdr, (U32)w);
    PutBE(ihdr + 4, (U32)h);
    ihdr[8] = 8; // bits per channel
    ihdr[9] = 2; // RGB
    ihdr[10] = 0; // deflate
    ihdr[11] = 0; // adaptive filtering
    ihdr[12] = 0; // no interlace
    o = Chunk(o, "IHDR", 13);

    U8* z = o + 8;
    size_t zo = 0;
    z[zo++] = 0x78;
    z[zo++] = 0x01;

    U32 a = 1;
    U32 b = 0;
    size_t left = rawSize;
    const U8* src = raw;
    do
    {
        const U32 n = left > 65535 ? 65535 : (U32)left;
        z[zo++] = left == n ? 1 : 0;
        z[zo++] = (U8)n;
        z[zo++] = (U8)(n >> 8);
        z[zo++] = (U8)~n;
        z[zo++] = (U8)(~n >> 8);
        memcpy(z + zo, src, n);
        zo += n;

        // zlib's way: 5552 bytes cannot overflow the sums before the modulo.
        for (U32 i = 0; i < n;)
        {
            const U32 run = n - i < 5552 ? n - i : 5552;
            for (U32 k = 0; k < run; k++)
            {
                a += src[i + k];
                b += a;
            }
            a %= 65521;
            b %= 65521;
            i += run;
        }

        src += n;
        left -= n;
    } while (left > 0);

    PutBE(z + zo, (b << 16) | a);
    zo += 4;
    free(raw);
    o = Chunk(o, "IDAT", (U32)zo);
    o = Chunk(o, "IEND", 0);
    sEncodeMs = MsSince(encodeFrom);

    const U64 writeFrom = iHostMonotonicNs();
    const bool ok = WriteWhole(path, file, (size_t)(o - file));
    sWriteMs = MsSince(writeFrom);
    free(file);
    return ok ? TRUE : FALSE;
}

S32 iFrameGrabWrite(const char* path, S32 maxWidth)
{
    // Kept between calls; the copy target is a render target, not worth
    // making per shot.
    static RwRaster* sFrame;

    sReadMs = sEncodeMs = sWriteMs = 0.0f;

    if (path == NULL)
    {
        return FALSE;
    }

    const U64 readFrom = iHostMonotonicNs();
    RwRaster* frame = iSnapshotCopyFrame(sFrame);
    sFrame = frame;
    if (frame == NULL)
    {
        return FALSE;
    }

    rw::Image* img = reinterpret_cast<rw::Raster*>(frame)->toImage();
    if (img == NULL)
    {
        return FALSE;
    }
    sReadMs = MsSince(readFrom);

    return WriteImage(path, img, maxWidth);
}

S32 iFrameGrabWriteRaster(const char* path, RwRaster* raster)
{
    if (path == NULL || raster == NULL)
    {
        return FALSE;
    }

    sReadMs = sEncodeMs = sWriteMs = 0.0f;

    rw::Image* img = reinterpret_cast<rw::Raster*>(raster)->toImage();
    if (img == NULL)
    {
        return FALSE;
    }

    return WriteImage(path, img, 0);
}

void iFrameGrabLastTimes(F32* readMs, F32* encodeMs, F32* writeMs)
{
    *readMs = sReadMs;
    *encodeMs = sEncodeMs;
    *writeMs = sWriteMs;
}
