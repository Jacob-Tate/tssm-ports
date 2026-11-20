// The Direct3D 9 device's resets, kept to the moments they can succeed. Why,
// and what is held back while the device is not usable, is in iDeviceReset.h.

#include <rwcore.h>

// BEFORE librw's headers, for the reason engine_start.cpp gives: rwd3d.h
// declares EngineOpenParams from whether _D3D9_H_ is defined by the time it is
// read.
#if defined(RW_D3D9) || defined(RW_D3D11)
#include <windows.h>
#define WITH_D3D
#endif
#ifdef RW_D3D9
#include <d3d9.h>
#endif
#ifdef RW_D3D11
#include <d3d11.h>
#endif

#include "rw.h"

#ifdef RW_D3D_ANY
// d3d9Globals: the window librw measures, and the present parameters it
// compares the window with and resets the device to. GETD3DRASTEREXT, for the
// texture behind a camera texture.
#include "src/d3d/rwd3dimpl.h"
#endif

#include "backend.h"
#include "iDeviceReset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RW_D3D9
// tools/librw/d3d9_exclusive_reset.patch: a reset at the size the device has,
// for exclusive full screen.
namespace rw
{
    namespace d3d
    {
        namespace impl9
        {
            bool32 resetDevice(void);
        }
    } // namespace d3d
} // namespace rw

namespace
{
    // librw's own functions, as the device and the D3D9 driver had them when
    // this was installed.
    rw::Device sLibrw;
    rw::uint8* (*sLibrwRasterLock)(rw::Raster*, rw::int32, rw::int32);
    bool sInstalled;

    void (*sRelease)();

    // TestCooperativeLevel's answer the last time it was asked: at the end of
    // every camera update and every present. D3D_OK is the only answer in
    // which librw's default-pool objects are known to exist -- a device that
    // was lost stays lost until a Reset succeeds, and librw makes them again
    // right after that Reset.
    HRESULT sState = D3D_OK;

    // A reset was let through and the device refused it: afterwards it still
    // asks to be reset, or answers something other than D3D_OK or lost, or
    // librw came out of the reset holding less than it went in with (Census).
    //
    // Nothing this file knows of makes a reset it allows fail; this is for one
    // that fails anyway (a driver fault, a default-pool object nobody
    // released). It is a last line of defence and not a recovery. librw cannot
    // be let try again: a failed Reset leaves every private depth surface (a Z
    // raster not the size of the screen -- the shadows' have one) NULL,
    // because librw releases them before the Reset and cannot make them on a
    // device that did not reset, and its next attempt releases them again
    // without looking, through NULL. So the device is left as it is, the 2D
    // drawing and the present stay held, and the log says why the picture
    // stopped. Geometry that librw instances for the first time on such a
    // device can still fault inside librw; nothing outside it can hold that
    // back.
    //
    // A device that is LOST once more by then has refused nothing: it was lost
    // again in the instant around the Reset (an exclusive full screen switched
    // away from just then). D3D9 lets objects be made on a lost device, as
    // stand-ins in system memory, so librw comes out of that reset holding all
    // it went in with -- the counts are checked to be sure -- and its next
    // reset releases objects that exist. That device is held like any lost one
    // and reset again once it can be.
    bool sBroken;

    // iDeviceResetEpoch.
    unsigned sEpoch;

    // An Im3D batch whose transform was skipped skips its draws as well, even
    // if the device came back in between.
    bool sIm3DHeld;

    // TSSM_TEST_DEVICE_LOST=<present>[:<presents>][,...]: from that present
    // on, for that many presents (60 when not given), TestCooperativeLevel's
    // answer is taken to be D3DERR_DEVICELOST, and after them
    // D3DERR_DEVICENOTRESET until the next camera update resets the device --
    // a real Reset, through librw, as after a real loss. So what a lost device
    // does to the frames, and the way back, can be seen without taking the
    // display away from everything else on it (which is what makes a real
    // D3D9 device lost: a lock screen, a mode change, a driver reset).
    //
    // `reset` in place of a present number starts that loss right after the
    // next reset this file lets through -- the end of the loss before it, a
    // window resize, F11 -- before the device is asked how the reset went: a
    // device lost again in the instant after a Reset that succeeded.
    struct Loss
    {
        long at; // 0: right after the next reset
        long presents;
    };
    const int kMaxLosses = 8;
    Loss sLosses[kMaxLosses];
    int sLossCount;
    int sLossNext;
    long sPresents;
    long sLostUntil;
    enum Simulated
    {
        SIM_NONE,
        SIM_LOST,
        SIM_NOTRESET
    };
    Simulated sSim = SIM_NONE;

    void ReadTestLosses()
    {
        sLossCount = 0;
        sLossNext = 0;
        sPresents = 0;
        sSim = SIM_NONE;

        const char* spec = getenv("TSSM_TEST_DEVICE_LOST");
        while (spec != NULL && *spec != '\0' && sLossCount < kMaxLosses)
        {
            char* end = NULL;
            long at = 0;
            if (strncmp(spec, "reset", 5) == 0)
            {
                end = (char*)spec + 5;
            }
            else
            {
                at = strtol(spec, &end, 10);
                if (end == spec || at <= 0)
                {
                    printf("tssm: d3d9: TSSM_TEST_DEVICE_LOST: '%s' is not <present>[:<presents>] "
                           "or reset[:<presents>]; the rest is ignored\n",
                           spec);
                    break;
                }
            }
            long presents = 60;
            if (*end == ':')
            {
                presents = strtol(end + 1, &end, 10);
            }
            sLosses[sLossCount].at = at;
            sLosses[sLossCount].presents = presents > 0 ? presents : 1;
            sLossCount++;
            spec = *end == ',' ? end + 1 : NULL;
        }
        if (sLossCount > 0)
        {
            if (sLosses[0].at > 0)
            {
                printf("tssm: d3d9: TSSM_TEST_DEVICE_LOST: %d simulated loss(es), the first at "
                       "present %ld\n",
                       sLossCount, sLosses[0].at);
            }
            else
            {
                printf("tssm: d3d9: TSSM_TEST_DEVICE_LOST: %d simulated loss(es), the first right "
                       "after the first reset\n",
                       sLossCount);
            }
            fflush(stdout);
        }
    }

    // Once per present, before anything asks the device how it is.
    void CountPresent()
    {
        sPresents++;
        if (sSim == SIM_NONE && sLossNext < sLossCount && sLosses[sLossNext].at > 0 &&
            sPresents >= sLosses[sLossNext].at)
        {
            sSim = SIM_LOST;
            sLostUntil = sPresents + sLosses[sLossNext].presents;
            sLossNext++;
            printf("tssm: d3d9: TSSM_TEST_DEVICE_LOST: the device is taken as lost at present "
                   "%ld\n",
                   sPresents);
            fflush(stdout);
        }
        else if (sSim == SIM_LOST && sPresents >= sLostUntil)
        {
            sSim = SIM_NOTRESET;
        }
    }

    // Right after a reset this file let through, before the device is asked
    // how it went.
    void LoseAfterReset()
    {
        if (sSim == SIM_NONE && sLossNext < sLossCount && sLosses[sLossNext].at == 0)
        {
            sSim = SIM_LOST;
            sLostUntil = sPresents + sLosses[sLossNext].presents;
            sLossNext++;
            printf("tssm: d3d9: TSSM_TEST_DEVICE_LOST: the device is taken as lost again right "
                   "after its reset, at present %ld\n",
                   sPresents);
            fflush(stdout);
        }
    }

    HRESULT Cooperative(IDirect3DDevice9* dev)
    {
        switch (sSim)
        {
        case SIM_LOST:
            return D3DERR_DEVICELOST;
        case SIM_NOTRESET:
            return D3DERR_DEVICENOTRESET;
        default:
            return dev->TestCooperativeLevel();
        }
    }

    bool Usable()
    {
        return sState == D3D_OK && !sBroken;
    }

    // **librw's cache of the device's bindings can name buffers that are gone.**
    //
    // librw sets the index buffer, the vertex streams and the vertex
    // declaration only when they differ from what it set last (deviceCache in
    // d3ddevice.cpp), and destroying a geometry -- every model of a scene that
    // is torn down -- does not take its buffers out of that cache. The
    // device's own binding keeps the last ones alive, so nothing shows until a
    // reset: releaseVideoMemory unbinds the index buffer, the declaration and
    // stream 0 on the device (its loop over the streams unbinds stream 0 each
    // time), which frees them, and restoreD3d9Device binds again what the
    // cache names. A movie makes no indexed draws, so the index buffer of the
    // scene before it was still in the cache, and resizing the window during
    // story1.bik crashed in restoreD3d9Device on a freed buffer.
    //
    // So the cache is emptied through librw's own setters, which unbind on the
    // device too, at every camera update and present -- not only before the
    // resets this file lets through, because librw reads the window again
    // after this file has, and a size that arrives in between resets as well.
    // Every draw sets what it draws with, so the next one binds them again.
    // Stream 2 is left as it is: it is librw's constant vertex stream, bound
    // once when the device is made and never again, read by the declaration
    // of every geometry without normals, colours or texture coordinates of
    // its own, and alive as long as the device.
    void Unbind()
    {
        rw::d3d::setIndices(NULL);
        rw::d3d::setStreamSource(0, NULL, 0, 0);
        rw::d3d::setStreamSource(1, NULL, 0, 0);
        rw::d3d::setVertexDeclaration(NULL);
    }

    void ReleaseVolatile()
    {
        if (sRelease != NULL)
        {
            sRelease();
        }
    }

    // librw's own count of what it holds on the device. A reset releases
    // librw's default-pool objects and makes them again; when the Reset failed
    // the making fails, and the counts come out lower than they went in. That
    // is the one sure sign, from outside librw, that a reset happened and did
    // not take -- whatever TestCooperativeLevel says afterwards, and whichever
    // of librw's two paths did it.
    struct Census
    {
        int vertexBuffers;
        int indexBuffers;
        int textures;
    };

    Census Count()
    {
        const rw::d3d::D3d9Globals& g = rw::d3d::d3d9Globals;
        Census c = { g.numVertexBuffers, g.numIndexBuffers, g.numTextures };
        return c;
    }

    bool Shrank(const Census& before)
    {
        const Census now = Count();
        return now.vertexBuffers < before.vertexBuffers || now.indexBuffers < before.indexBuffers ||
               now.textures < before.textures;
    }

    // `failed`: a reset was let through and the device was not usable after
    // it, or librw came out of the call holding less than it went in with.
    void Settle(HRESULT state, bool failed)
    {
        if (sBroken)
        {
            sState = state;
            return;
        }

        if (failed)
        {
            sBroken = true;
            sState = state;
            printf("tssm: d3d9: the device could not be reset (0x%08lx); the picture stops "
                   "here -- restart the game\n",
                   (unsigned long)state);
            fflush(stdout);
            return;
        }

        if (state == sState)
        {
            return;
        }

        if (state == D3D_OK)
        {
            const D3DPRESENT_PARAMETERS& pp = rw::d3d::d3d9Globals.present;
            printf("tssm: d3d9: the device was reset (%ux%u); drawing again\n",
                   (unsigned)pp.BackBufferWidth, (unsigned)pp.BackBufferHeight);
        }
        else if (state == D3DERR_DEVICENOTRESET)
        {
            printf("tssm: d3d9: the device was lost and can be reset now\n");
        }
        else if (state == D3DERR_DEVICELOST)
        {
            printf("tssm: d3d9: the device was lost; 2D drawing and the present are held until "
                   "it can be reset\n");
        }
        else
        {
            printf("tssm: d3d9: the device reports 0x%08lx; 2D drawing and the present are "
                   "held\n",
                   (unsigned long)state);
        }
        fflush(stdout);
        sState = state;
    }

    // librw's resize test, read the way it reads it, and the decision on it.
    //
    // beginUpdate resets when the window is not minimised and its client area
    // differs from the back buffer. Whether a reset should happen is decided
    // here. When one should not, librw is shown the client area just read as
    // the back buffer's size for the length of the call, so it finds nothing
    // to do.
    //
    // librw reads the window again, and a minimise or restore driven from
    // another process moves it on that process's thread, so the two reads can
    // differ. Windows changes the minimised state first and the size second,
    // in both directions (the 0x0 restore is exactly that order seen from
    // between the two steps), so a second read that differs from the first is
    // either a window now minimised, which librw leaves alone, or a client
    // area that has become real, which resets as it should.
    void BeginUpdate(rw::Camera* cam)
    {
        IDirect3DDevice9* dev = rw::d3d::d3ddevice;
        rw::d3d::D3d9Globals& g = rw::d3d::d3d9Globals;
        D3DPRESENT_PARAMETERS& pp = g.present;

        const HRESULT before = Cooperative(dev);

        RECT r;
        GetClientRect(g.window, &r);
        const bool iconic = IsIconic(g.window) != 0;
        const UINT w = r.right > 0 ? (UINT)r.right : 0;
        const UINT h = r.bottom > 0 ? (UINT)r.bottom : 0;

        // Exclusive full screen is the display mode's size and never the
        // window's: brought back from another program its window is, for a
        // while, no longer minimised and still the size of a title bar, and a
        // reset to that is refused. It is reset only when the device asks,
        // at the size it has (tools/librw/d3d9_exclusive_reset.patch).
        const bool exclusive = !pp.Windowed;
        const bool resize =
            !exclusive && !iconic && (w != pp.BackBufferWidth || h != pp.BackBufferHeight);
        const bool resettable = before == D3D_OK || before == D3DERR_DEVICENOTRESET;
        const bool reset = !sBroken && !iconic && w > 0 && h > 0 && resettable &&
                           (resize || before == D3DERR_DEVICENOTRESET);

        if (before != D3D_OK || reset || resize)
        {
            sEpoch++;
        }

        const UINT keptW = pp.BackBufferWidth;
        const UINT keptH = pp.BackBufferHeight;
        const Census census = Count();
        bool held = false;
        bool forced = false;

        Unbind();

        if (reset)
        {
            ReleaseVolatile();

            // A simulated loss ends with the real reset it asked for.
            if (sSim == SIM_NOTRESET)
            {
                sSim = SIM_NONE;
            }

            if (exclusive)
            {
                rw::d3d::impl9::resetDevice();
            }
            // A device that can be reset and has not been: librw resets on a
            // size mismatch and on nothing else, so it is given one. It then
            // takes the window's size, which is the size wanted anyway.
            else if (!resize)
            {
                pp.BackBufferWidth = 0;
                forced = true;
            }
        }
        else if (!exclusive && (w != pp.BackBufferWidth || h != pp.BackBufferHeight))
        {
            // Including a minimised window's 0x0, so that librw reading the
            // window again halfway through a restore -- no longer minimised,
            // not yet given its size back -- still sees no change.
            pp.BackBufferWidth = w;
            pp.BackBufferHeight = h;
            held = true;

            // A window that is not minimised can get its size back between
            // this read and librw's -- the 0x0 of a restore is that moment --
            // and librw then resets after all. So what has to go before a
            // reset goes now too.
            if (!iconic)
            {
                ReleaseVolatile();
            }
        }

        sLibrw.beginUpdate(cam);

        // Put back only what librw did not replace. It replaces the pair when
        // it resets, and it resets only on a mismatch with what it was shown.
        if (held && pp.BackBufferWidth == w && pp.BackBufferHeight == h)
        {
            pp.BackBufferWidth = keptW;
            pp.BackBufferHeight = keptH;
        }
        if (forced && pp.BackBufferWidth == 0)
        {
            // The window was minimised between the two reads and librw left it.
            pp.BackBufferWidth = keptW;
        }

        if (reset)
        {
            LoseAfterReset();
        }

        // Lost again rather than refused: see sBroken.
        const HRESULT after = Cooperative(dev);
        const bool shrank = Shrank(census);
        if (reset && after == D3DERR_DEVICELOST && !shrank && !sBroken)
        {
            printf("tssm: d3d9: the device was lost again as it was reset; it is reset again "
                   "once it can be\n");
            fflush(stdout);
        }
        Settle(after, (reset && after != D3D_OK && after != D3DERR_DEVICELOST) || shrank);
    }

    // Only a device that is fine is presented. A lost one has nothing to show,
    // and one that can be reset is reset by the next camera update above --
    // where the window is measured first -- rather than by librw's present,
    // which resets at whatever size it last had once Present has said the
    // device is lost. That path is still there for a device lost DURING this
    // Present that can be reset at once; the census catches it if it fails.
    void ShowRaster(rw::Raster* raster, rw::uint32 flags)
    {
        IDirect3DDevice9* dev = rw::d3d::d3ddevice;

        CountPresent();
        const HRESULT before = Cooperative(dev);

        if (sBroken || before != D3D_OK)
        {
            sEpoch++;
            ReleaseVolatile();
            Settle(before, false);
            return;
        }

        // librw's present resets a device lost during it that can be reset at
        // once (see Unbind).
        Unbind();

        const Census census = Count();
        sLibrw.showRaster(raster, flags);
        const HRESULT after = dev->TestCooperativeLevel();

        // Lost during Present: librw may have reset there and then.
        if (after != D3D_OK)
        {
            sEpoch++;
        }

        Settle(after, Shrank(census));
    }

    // **A camera texture read back keeps a reference librw never gives back.**
    //
    // librw's D3D9 rasterLock reads a camera texture through
    // GetSurfaceLevel(level, &rt) and never releases rt (d3d.cpp, the
    // CAMERATEXTURE arm), so every read leaves the texture one reference
    // heavier. Its releaseVideoMemory then cannot destroy it ("texture wasn't
    // destroyed"), and a Reset with a default-pool texture alive fails. The
    // port reads camera textures back for every save thumbnail and every
    // TSSM_CAPTURE_FRAMES frame, so after the first save any reset -- a
    // resize, Alt+Enter, F11 -- failed.
    //
    // The reference is measured rather than assumed, so a librw that gives it
    // back makes this do nothing.
    ULONG References(IDirect3DTexture9* tex)
    {
        tex->AddRef();
        return tex->Release();
    }

    rw::uint8* RasterLock(rw::Raster* raster, rw::int32 level, rw::int32 lockMode)
    {
        IDirect3DTexture9* tex = NULL;
        ULONG before = 0;

        // librw returns before reading anything from a raster already locked.
        if (raster->type == rw::Raster::CAMERATEXTURE &&
            (raster->privateFlags &
             (rw::Raster::PRIVATELOCK_READ | rw::Raster::PRIVATELOCK_WRITE)) == 0)
        {
            tex = (IDirect3DTexture9*)GETD3DRASTEREXT(raster)->texture;
            if (tex != NULL)
            {
                before = References(tex);
            }
        }

        rw::uint8* pixels = sLibrwRasterLock(raster, level, lockMode);

        if (tex != NULL && References(tex) > before)
        {
            tex->Release();
        }
        return pixels;
    }

    // The copy into a camera texture, whose texture may not exist.
    rw::bool32 RasterRenderFast(rw::Raster* raster, rw::int32 x, rw::int32 y)
    {
        return Usable() ? sLibrw.rasterRenderFast(raster, x, y) : 0;
    }

    // Im2D and Im3D go through librw's dynamic buffers, which a failed reset
    // leaves NULL.

    void Im2DRenderLine(void* vertices, rw::int32 numVertices, rw::int32 vert1, rw::int32 vert2)
    {
        if (Usable())
        {
            sLibrw.im2DRenderLine(vertices, numVertices, vert1, vert2);
        }
    }

    void Im2DRenderTriangle(void* vertices, rw::int32 numVertices, rw::int32 vert1, rw::int32 vert2,
                            rw::int32 vert3)
    {
        if (Usable())
        {
            sLibrw.im2DRenderTriangle(vertices, numVertices, vert1, vert2, vert3);
        }
    }

    void Im2DRenderPrimitive(rw::PrimitiveType primType, void* vertices, rw::int32 numVertices)
    {
        if (Usable())
        {
            sLibrw.im2DRenderPrimitive(primType, vertices, numVertices);
        }
    }

    void Im2DRenderIndexedPrimitive(rw::PrimitiveType primType, void* vertices,
                                    rw::int32 numVertices, void* indices, rw::int32 numIndices)
    {
        if (Usable())
        {
            sLibrw.im2DRenderIndexedPrimitive(primType, vertices, numVertices, indices, numIndices);
        }
    }

    void Im3DTransform(void* vertices, rw::int32 numVertices, rw::Matrix* world, rw::uint32 flags)
    {
        sIm3DHeld = !Usable();
        if (!sIm3DHeld)
        {
            sLibrw.im3DTransform(vertices, numVertices, world, flags);
        }
    }

    void Im3DRenderPrimitive(rw::PrimitiveType primType)
    {
        if (!sIm3DHeld && Usable())
        {
            sLibrw.im3DRenderPrimitive(primType);
        }
    }

    void Im3DRenderIndexedPrimitive(rw::PrimitiveType primType, void* indices, rw::int32 numIndices)
    {
        if (!sIm3DHeld && Usable())
        {
            sLibrw.im3DRenderIndexedPrimitive(primType, indices, numIndices);
        }
    }

    void Im3DEnd()
    {
        sIm3DHeld = false;
        sLibrw.im3DEnd();
    }
} // namespace
#endif

void iDeviceResetInstall()
{
#ifdef RW_D3D9
    if (sInstalled || !iBackendIsD3D9() || rw::engine == NULL || rw::d3d::d3ddevice == NULL ||
        rw::engine->driver[rw::PLATFORM_D3D9] == NULL)
    {
        return;
    }

    rw::Device& d = rw::engine->device;
    sLibrw = d;
    d.beginUpdate = BeginUpdate;
    d.showRaster = ShowRaster;
    d.rasterRenderFast = RasterRenderFast;
    d.im2DRenderLine = Im2DRenderLine;
    d.im2DRenderTriangle = Im2DRenderTriangle;
    d.im2DRenderPrimitive = Im2DRenderPrimitive;
    d.im2DRenderIndexedPrimitive = Im2DRenderIndexedPrimitive;
    d.im3DTransform = Im3DTransform;
    d.im3DRenderPrimitive = Im3DRenderPrimitive;
    d.im3DRenderIndexedPrimitive = Im3DRenderIndexedPrimitive;
    d.im3DEnd = Im3DEnd;

    rw::Driver* driver = rw::engine->driver[rw::PLATFORM_D3D9];
    sLibrwRasterLock = driver->rasterLock;
    driver->rasterLock = RasterLock;

    sState = D3D_OK;
    sBroken = false;
    sIm3DHeld = false;
    ReadTestLosses();
    sInstalled = true;
#endif
}

void iDeviceResetRemove()
{
#ifdef RW_D3D9
    if (!sInstalled || rw::engine == NULL)
    {
        sInstalled = false;
        return;
    }

    // Each one put back only while it is still this file's: the overlay takes
    // showRaster over after this, and puts its own back when it shuts down.
    rw::Device& d = rw::engine->device;
#define IDEVICERESET_PUT_BACK(field, mine)                                                         \
    if (d.field == mine)                                                                           \
    {                                                                                              \
        d.field = sLibrw.field;                                                                    \
    }
    IDEVICERESET_PUT_BACK(beginUpdate, BeginUpdate)
    IDEVICERESET_PUT_BACK(showRaster, ShowRaster)
    IDEVICERESET_PUT_BACK(rasterRenderFast, RasterRenderFast)
    IDEVICERESET_PUT_BACK(im2DRenderLine, Im2DRenderLine)
    IDEVICERESET_PUT_BACK(im2DRenderTriangle, Im2DRenderTriangle)
    IDEVICERESET_PUT_BACK(im2DRenderPrimitive, Im2DRenderPrimitive)
    IDEVICERESET_PUT_BACK(im2DRenderIndexedPrimitive, Im2DRenderIndexedPrimitive)
    IDEVICERESET_PUT_BACK(im3DTransform, Im3DTransform)
    IDEVICERESET_PUT_BACK(im3DRenderPrimitive, Im3DRenderPrimitive)
    IDEVICERESET_PUT_BACK(im3DRenderIndexedPrimitive, Im3DRenderIndexedPrimitive)
    IDEVICERESET_PUT_BACK(im3DEnd, Im3DEnd)
#undef IDEVICERESET_PUT_BACK

    rw::Driver* driver = rw::engine->driver[rw::PLATFORM_D3D9];
    if (driver != NULL && driver->rasterLock == RasterLock)
    {
        driver->rasterLock = sLibrwRasterLock;
    }

    sInstalled = false;
#endif
}

void iDeviceResetSetReleaseHook(void (*release)())
{
#ifdef RW_D3D9
    sRelease = release;
#else
    (void)release;
#endif
}

unsigned iDeviceResetEpoch()
{
#ifdef RW_D3D9
    return sEpoch;
#else
    return 0;
#endif
}

bool iDeviceUsable()
{
#ifdef RW_D3D9
    if (sInstalled)
    {
        return Usable();
    }
#endif
    return true;
}
