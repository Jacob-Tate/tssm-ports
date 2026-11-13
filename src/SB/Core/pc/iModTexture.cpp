// PC-only: texture replacement for mods. New code.
//
// Every texture the game has arrives through RwTexDictionaryStreamRead
// (rw/texture.cpp): RWTX assets through RWTX_Read in zAssetTypes.cpp, streamed
// TEXS ones through xTextureManager, and models find theirs by name among
// those. After converting each texture's Xbox raster for the device, that
// function calls gRwTextureReplaceHook, which this file sets. If an enabled
// mod has textures/**/<name>.<png|dds|tga|bmp> for the texture's RenderWare
// name, the raster is swapped for one made from the image; the texture object
// itself -- its name, filtering, addressing, and every pointer the game holds
// to it -- stays.
//
// The new raster has no mipmaps: librw generates them only on D3D9 and GL3,
// and a mipmapped raster whose smaller levels are left unfilled samples
// garbage at a distance on the other backends. A DDS's own smaller levels are
// not used either, for now.
// librw before anything that includes types.h, which defines `null` as a
// macro; librw has a namespace of that name (iFX.cpp says the same).
#include <rw.h>

#include "src/lodepng/lodepng.h"

#include "iMod.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// rw/texture.cpp.
extern void (*gRwTextureReplaceHook)(rw::Texture* texture);

namespace
{
    rw::uint32 Le32(const rw::uint8* p)
    {
        return (rw::uint32)p[0] | ((rw::uint32)p[1] << 8) | ((rw::uint32)p[2] << 16) |
               ((rw::uint32)p[3] << 24);
    }

    bool ReadFile(const char* path, rw::uint8** data, rw::uint32* len)
    {
        FILE* fp = fopen(path, "rb");
        if (fp == NULL)
        {
            return false;
        }
        fseek(fp, 0, SEEK_END);
        long n = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (n <= 0)
        {
            fclose(fp);
            return false;
        }
        *data = (rw::uint8*)malloc((size_t)n);
        *len = (rw::uint32)n;
        bool ok = *data != NULL && fread(*data, 1, (size_t)n, fp) == (size_t)n;
        fclose(fp);
        if (!ok)
        {
            free(*data);
        }
        return ok;
    }

    // A 32-bit RGBA librw image from tightly packed RGBA bytes.
    rw::Image* ImageFromRGBA(const rw::uint8* rgba, rw::int32 w, rw::int32 h)
    {
        rw::Image* img = rw::Image::create(w, h, 32);
        if (img == NULL)
        {
            return NULL;
        }
        img->allocate();
        for (rw::int32 y = 0; y < h; y++)
        {
            memcpy(img->pixels + y * img->stride, rgba + (size_t)y * w * 4, (size_t)w * 4);
        }
        return img;
    }

    rw::Image* ReadPng(const char* path, const char** why)
    {
        rw::uint8* file;
        rw::uint32 len;
        if (!ReadFile(path, &file, &len))
        {
            *why = "cannot be read";
            return NULL;
        }
        unsigned char* rgba = NULL;
        unsigned w = 0, h = 0;
        unsigned error = lodepng_decode32(&rgba, &w, &h, file, len);
        free(file);
        if (error != 0 || rgba == NULL)
        {
            *why = error != 0 ? lodepng_error_text(error) : "not a PNG";
            free(rgba);
            return NULL;
        }
        rw::Image* img = ImageFromRGBA(rgba, (rw::int32)w, (rw::int32)h);
        free(rgba);
        return img;
    }

    // The top level of a DDS: DXT1/3/5, or uncompressed 32- or 24-bit RGB(A)
    // described by its channel masks.
    rw::Image* ReadDds(const char* path, const char** why)
    {
        rw::uint8* file;
        rw::uint32 len;
        if (!ReadFile(path, &file, &len))
        {
            *why = "cannot be read";
            return NULL;
        }
        rw::Image* img = NULL;
        *why = "not a DDS this reads (DXT1, DXT3, DXT5, or 24/32-bit RGB)";
        if (len >= 128 && memcmp(file, "DDS ", 4) == 0)
        {
            const rw::uint8* hdr = file + 4;
            rw::int32 h = (rw::int32)Le32(hdr + 8);
            rw::int32 w = (rw::int32)Le32(hdr + 12);
            const rw::uint8* pf = hdr + 72; // DDS_PIXELFORMAT
            rw::uint32 pfFlags = Le32(pf + 4);
            const rw::uint8* data = file + 128;
            rw::uint32 avail = len - 128;

            if ((pfFlags & 0x4) != 0) // DDPF_FOURCC
            {
                int type = 0;
                if (memcmp(pf + 8, "DXT1", 4) == 0)
                {
                    type = 1;
                }
                else if (memcmp(pf + 8, "DXT3", 4) == 0)
                {
                    type = 3;
                }
                else if (memcmp(pf + 8, "DXT5", 4) == 0)
                {
                    type = 5;
                }
                rw::uint32 need = (rw::uint32)(((w + 3) / 4) * ((h + 3) / 4) * (type == 1 ? 8 : 16));
                if (type != 0 && w >= 4 && h >= 4 && need <= avail)
                {
                    img = rw::Image::create(w, h, 32);
                    img->allocate();
                    img->setPixelsDXT(type, (rw::uint8*)data);
                }
            }
            else if ((pfFlags & 0x40) != 0) // DDPF_RGB
            {
                rw::uint32 bits = Le32(pf + 12);
                rw::uint32 mask[4] = { Le32(pf + 16), Le32(pf + 20), Le32(pf + 24),
                                       (pfFlags & 0x1) ? Le32(pf + 28) : 0 };
                rw::uint32 bpp = bits / 8;
                if ((bits == 32 || bits == 24) && (rw::uint32)(w * h) * bpp <= avail)
                {
                    rw::uint8* rgba = (rw::uint8*)malloc((size_t)w * h * 4);
                    for (rw::int32 i = 0; i < w * h; i++)
                    {
                        const rw::uint8* p = data + (size_t)i * bpp;
                        rw::uint32 v = p[0] | (p[1] << 8) | (p[2] << 16) |
                                       (bpp == 4 ? ((rw::uint32)p[3] << 24) : 0);
                        for (int c = 0; c < 4; c++)
                        {
                            rw::uint32 m = mask[c];
                            if (m == 0)
                            {
                                rgba[i * 4 + c] = c == 3 ? 255 : 0;
                                continue;
                            }
                            int shift = 0;
                            while (((m >> shift) & 1) == 0)
                            {
                                shift++;
                            }
                            rw::uint32 max = m >> shift;
                            rgba[i * 4 + c] = (rw::uint8)(((v & m) >> shift) * 255 / max);
                        }
                    }
                    img = ImageFromRGBA(rgba, w, h);
                    free(rgba);
                }
            }
        }
        free(file);
        return img;
    }

    rw::Image* ReadImage(const char* path, const char** why)
    {
        const char* dot = strrchr(path, '.');
        char ext[8] = { 0 };
        for (int i = 0; dot != NULL && dot[i + 1] != '\0' && i < 7; i++)
        {
            ext[i] = (char)tolower((unsigned char)dot[i + 1]);
        }
        if (strcmp(ext, "png") == 0)
        {
            return ReadPng(path, why);
        }
        if (strcmp(ext, "dds") == 0)
        {
            return ReadDds(path, why);
        }
        // librw's own readers, which find the file through the engine's file
        // functions; it is known to exist.
        *why = "not a TGA or BMP librw reads";
        if (strcmp(ext, "tga") == 0)
        {
            return rw::readTGA(path);
        }
        if (strcmp(ext, "bmp") == 0)
        {
            return rw::readBMP(path);
        }
        *why = "not a .png, .dds, .tga or .bmp";
        return NULL;
    }

    void Replace(rw::Texture* texture)
    {
        if (texture == NULL)
        {
            return;
        }
        const char* path = iModFindTexture(texture->name);
        if (path == NULL)
        {
            return;
        }

        const char* why = "";
        rw::Image* img = ReadImage(path, &why);
        if (img == NULL)
        {
            iModLogf("texture '%s': %s %s; the original is kept", texture->name, path, why);
            return;
        }

        rw::Raster* raster = rw::Raster::createFromImage(img);
        rw::int32 w = img->width;
        rw::int32 h = img->height;
        img->destroy();
        if (raster == NULL)
        {
            iModLogf("texture '%s': the device would not take %s (%dx%d); the original is kept",
                     texture->name, path, (int)w, (int)h);
            return;
        }

        rw::int32 oldW = texture->raster != NULL ? texture->raster->width : 0;
        rw::int32 oldH = texture->raster != NULL ? texture->raster->height : 0;
        if (texture->raster != NULL)
        {
            texture->raster->destroy();
        }
        texture->raster = raster;
        iModLogf("texture '%s' <- %s (%dx%d, was %dx%d)", texture->name, path, (int)w, (int)h,
                 (int)oldW, (int)oldH);
    }
} // namespace

void iModTextureInstall()
{
    gRwTextureReplaceHook = Replace;
}
