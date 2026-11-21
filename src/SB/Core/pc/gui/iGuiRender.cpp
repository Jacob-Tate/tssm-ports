// The overlay's renderer. See iGuiRender.h.
//
// **Where the overlay is drawn, and why there.** The game renders into a
// virtual screen at the render size, and each librw device stretches that into
// the window's back buffer as the first half of presenting it (blitVirtualScreen
// in d3ddevice.cpp, d3d11device.cpp and gl3device.cpp). The overlay belongs
// AFTER that stretch and BEFORE the flip: drawn there it is at the window's own
// resolution, so the text is sharp at a 640x480 render size on a 4K screen, and
// it can use the bars beside a 4:3 picture.
//
// librw's present is one function per device, reached through
// rw::engine->device.showRaster, and only its GL3 and Vulkan devices offer a
// hook between the stretch and the flip (setPresentOverlay, which the touch
// controls use). So this file takes over that one function pointer -- not librw
// itself -- and for the frames where the overlay is open:
//
//   GL3    sets librw's present-overlay hook to ImGui's OpenGL 3 renderer for
//          this one present, lets librw present as always, and puts the touch
//          controls' hook back (iTouchOverlayInstall). The touch controls are
//          not drawn under the open overlay, which is right: the game is not
//          taking input then.
//   D3D9   does the stretch itself -- the same StretchRect from the resolved
//          virtual screen that librw does -- then ImGui's DX9 renderer, then
//          Present. A device that is lost is left to the present behind this
//          hook (iDeviceReset.h's, then librw's), which sees it reset.
//   D3D11  binds the back buffer, draws the game's picture as the first image
//          of the ImGui frame (opaque, linear, letterboxed exactly as librw's
//          blit does) and the overlay over it, then Present with the same
//          vsync flag librw would have used.
//
//   C3D    (the 3DS) draws the overlay on the bottom screen, which has nothing
//          of the game's: rw::c3d's overlay batch at the present, inside the
//          game's frame, so librw's present shows both screens. The ImGui
//          vertices are turned into the device's own 2D ones on the CPU, and
//          a frame whose picture is the same as the last one drawn (a hash of
//          the draw data) draws nothing: the screen keeps its picture.
//
// With the overlay closed the hook calls librw's present and returns: the
// game's frames are presented by the code that always presented them.
//
// **What it must not disturb.** librw caches device state. Each ImGui renderer
// saves and restores the state it changes (a D3D9 state block; a D3D11 and a GL
// save/restore of every binding it touches), and the render target and depth
// surface this file binds are put back to the ones librw had bound, so librw's
// cache still describes the device afterwards. Under GL3 librw is also told to
// forget its cache, which is what the present-overlay hook's return value is for.
//
// **D3D9's reset.** librw resets the D3D9 device when the window's client area
// stops matching the back buffer (beginUpdate) and when the device is lost
// (showRaster), and a reset fails while anything in D3DPOOL_DEFAULT is alive.
// ImGui's DX9 renderer keeps its buffers and font texture there. So they are
// released whenever a reset can follow -- the overlay closing, any window size,
// display or fullscreen change (iGui.cpp's event watch), a lost device -- and
// ImGui makes them again on the next frame it draws. The event watch is not
// enough on its own: a window restored from another process changes before its
// events are pumped. So iDeviceReset.h, which decides when librw may reset,
// also calls iGuiRenderReleaseVolatile right before every reset it allows, and
// whenever it holds one back from a window that is not minimised (librw reads
// the window again, and a size that arrives in between still resets).
// librw's own beginUpdate and showRaster sit behind that file's, and this hook
// sits in front of both.

#include "imgui.h"

#if defined(RW_D3D9) || defined(RW_D3D11)
#include <windows.h>
#define WITH_D3D
#endif
#ifdef RW_D3D9
#include <d3d9.h>
#include "imgui_impl_dx9.h"
#endif
#ifdef RW_D3D11
#include <d3d11.h>
#include "imgui_impl_dx11.h"
#endif
#ifdef RW_GL3
#include "imgui_impl_opengl3.h"
#endif
#ifdef RW_C3D
#include "imgui_internal.h" // ImHashData
#include "3ds/c3d/rwc3d.h"
#include "3ds/i3DS.h"
#include <SDL3/SDL.h>
#endif

#include "rw.h"

#ifdef RW_D3D_ANY
#include "src/d3d/rwd3dimpl.h"
#endif

#include "rw/backend.h"

#include "iDeviceReset.h"
#include "iLoadFrame.h"
#include "iTouchOverlay.h"

#include "gui/iGuiInternal.h"
#include "gui/iGuiRender.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    enum Kind
    {
        KIND_NONE,
        KIND_D3D9,
        KIND_D3D11,
        KIND_GL3,
        KIND_C3D
    };

    Kind sKind = KIND_NONE;
    iGuiRenderFrameFn sFrame;
    void (*sLibrwShowRaster)(rw::Raster*, rw::uint32);

    char sCapturePath[512];
    bool sCaptureArmed;
    bool sCaptureDone;

    // The largest rectangle of the virtual screen's shape that fits `w` x `h`,
    // centred: librw's blitVirtualScreen arithmetic, integer and in 64 bits so
    // that a rectangle which should fill the target exactly does.
    void FitRect(int vw, int vh, int w, int h, int* x, int* y, int* fw, int* fh)
    {
        int width = w;
        int height = (int)(((long long)w * vh) / vw);
        if (height > h)
        {
            height = h;
            width = (int)(((long long)h * vw) / vh);
        }
        *x = (w - width) / 2;
        *y = (h - height) / 2;
        *fw = width;
        *fh = height;
    }

    void WriteCapture(const unsigned char* rgba, int w, int h, int stride)
    {
        sCaptureArmed = false;
        sCaptureDone = true;

        if (iGui::WritePNG(sCapturePath, rgba, w, h, stride))
        {
            printf("tssm: gui: captured the window (%dx%d) with the overlay -> %s\n", w, h,
                   sCapturePath);
        }
        else
        {
            printf("tssm: gui: could not write the capture to %s\n", sCapturePath);
        }
        fflush(stdout);
    }

    // Byte order of a 32-bit back buffer, as the swizzle into RGBA.
    void SwizzleBGRA(unsigned char* row, int w)
    {
        for (int x = 0; x < w; x++)
        {
            unsigned char* p = row + x * 4;
            unsigned char b = p[0];
            p[0] = p[2];
            p[2] = b;
        }
    }

#ifdef RW_D3D9
    // ---------------------------------------------------------------- D3D9

    void CaptureD3D9(IDirect3DDevice9* dev, IDirect3DSurface9* backBuffer,
                     const D3DSURFACE_DESC& desc)
    {
        IDirect3DSurface9* copy = NULL;
        if (FAILED(dev->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                                    D3DPOOL_SYSTEMMEM, &copy, NULL)) ||
            copy == NULL)
        {
            WriteCapture(NULL, 0, 0, 0);
            return;
        }

        D3DLOCKED_RECT locked;
        if (FAILED(dev->GetRenderTargetData(backBuffer, copy)) ||
            FAILED(copy->LockRect(&locked, NULL, D3DLOCK_READONLY)))
        {
            copy->Release();
            WriteCapture(NULL, 0, 0, 0);
            return;
        }

        const int w = (int)desc.Width;
        const int h = (int)desc.Height;
        unsigned char* rgba = (unsigned char*)malloc((size_t)w * h * 4);
        if (rgba != NULL)
        {
            for (int y = 0; y < h; y++)
            {
                unsigned char* row = rgba + (size_t)y * w * 4;
                memcpy(row, (const unsigned char*)locked.pBits + (size_t)y * locked.Pitch,
                       (size_t)w * 4);
                // X8R8G8B8 / A8R8G8B8: B, G, R, A in memory.
                SwizzleBGRA(row, w);
            }
        }

        copy->UnlockRect();
        copy->Release();

        WriteCapture(rgba, w, h, w * 4);
        free(rgba);
    }

    bool PresentD3D9(ImDrawData* drawData)
    {
        IDirect3DDevice9* dev = rw::d3d::d3ddevice;

        IDirect3DSurface9* backBuffer = NULL;
        if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) ||
            backBuffer == NULL)
        {
            return false;
        }

        D3DSURFACE_DESC desc;
        if (FAILED(backBuffer->GetDesc(&desc)))
        {
            backBuffer->Release();
            return false;
        }

        // What librw has bound, to be put back: its cache believes these are
        // the device's targets, and they must still be after this.
        IDirect3DSurface9* oldTarget = NULL;
        IDirect3DSurface9* oldDepth = NULL;
        dev->GetRenderTarget(0, &oldTarget);
        dev->GetDepthStencilSurface(&oldDepth);

        // The single-sampled picture. NULL when there is no virtual screen, in
        // which case the back buffer already IS the picture and is drawn over.
        IDirect3DSurface9* frame = rw::d3d::resolveVirtualScreen();

        // The depth surface goes first: D3D9 wants one at least as large as the
        // target, and the virtual screen's is smaller than the window.
        dev->SetDepthStencilSurface(NULL);
        dev->SetRenderTarget(0, backBuffer);

        rw::int32 vw = 0;
        rw::int32 vh = 0;
        rw::d3d::getVirtualScreen(&vw, &vh);
        if (frame != NULL && vw > 0 && vh > 0)
        {
            int x, y, w, h;
            FitRect(vw, vh, (int)desc.Width, (int)desc.Height, &x, &y, &w, &h);

            RECT dest;
            dest.left = x;
            dest.top = y;
            dest.right = x + w;
            dest.bottom = y + h;

            dev->Clear(0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
            dev->StretchRect(frame, NULL, backBuffer, &dest, D3DTEXF_LINEAR);
        }

        if (SUCCEEDED(dev->BeginScene()))
        {
            ImGui_ImplDX9_RenderDrawData(drawData);
            dev->EndScene();
        }

        if (sCaptureArmed)
        {
            CaptureD3D9(dev, backBuffer, desc);
        }

        dev->SetRenderTarget(0, oldTarget);
        dev->SetDepthStencilSurface(oldDepth);
        if (oldTarget != NULL)
        {
            oldTarget->Release();
        }
        if (oldDepth != NULL)
        {
            oldDepth->Release();
        }
        backBuffer->Release();

        // librw's D3D9 present ignores the flip flags: the interval is part of
        // the present parameters it made the device with.
        HRESULT hr = dev->Present(NULL, NULL, NULL, NULL);
        if (hr == D3DERR_DEVICELOST)
        {
            // Reset on a later frame, once it can be; see the hook.
            ImGui_ImplDX9_InvalidateDeviceObjects();
        }
        return true;
    }
#endif

#ifdef RW_D3D11
    // --------------------------------------------------------------- D3D11

    ID3D11Texture2D* sFrameTexture;
    ID3D11ShaderResourceView* sFrameView;
    ID3D11BlendState* sOpaque;
    bool sFrameInDrawList;

    void ReleaseFrameView()
    {
        if (sFrameView != NULL)
        {
            sFrameView->Release();
            sFrameView = NULL;
        }
        sFrameTexture = NULL;
    }

    // The game's picture is drawn with blending off. Its alpha is whatever the
    // scene left there, and through ImGui's usual blend it would show the black
    // clear through every pixel the game drew translucent.
    void SetOpaqueBlend(const ImDrawList*, const ImDrawCmd*)
    {
        ImGui_ImplDX11_RenderState* state =
            (ImGui_ImplDX11_RenderState*)ImGui::GetPlatformIO().Renderer_RenderState;
        if (state != NULL && sOpaque != NULL)
        {
            const float factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            state->DeviceContext->OMSetBlendState(sOpaque, factor, 0xFFFFFFFF);
        }
    }

    void BackgroundD3D11()
    {
        sFrameInDrawList = false;

        ID3D11Texture2D* tex = rw::d3d::impl11::virtualScreenTexture();
        rw::d3d::impl11::D3d11Globals& g = rw::d3d::impl11::d3d11Globals;
        rw::int32 vw = 0;
        rw::int32 vh = 0;
        rw::d3d::getVirtualScreen(&vw, &vh);
        if (tex == NULL || g.backBufferWidth <= 0 || g.backBufferHeight <= 0 || vw <= 0 || vh <= 0)
        {
            // No virtual screen: the back buffer is the picture and is drawn
            // over rather than cleared.
            return;
        }

        if (tex != sFrameTexture)
        {
            ReleaseFrameView();
            if (FAILED(rw::d3d::d3d11device->CreateShaderResourceView(tex, NULL, &sFrameView)))
            {
                sFrameView = NULL;
                return;
            }
            sFrameTexture = tex;
        }

        int x, y, w, h;
        FitRect(vw, vh, g.backBufferWidth, g.backBufferHeight, &x, &y, &w, &h);

        // Back-buffer pixels into ImGui's display units.
        const ImGuiIO& io = ImGui::GetIO();
        const float sx = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
        const float sy = io.DisplayFramebufferScale.y > 0.0f ? io.DisplayFramebufferScale.y : 1.0f;

        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        bg->AddCallback(SetOpaqueBlend, NULL);
        bg->AddImage(ImTextureRef((ImTextureID)(intptr_t)sFrameView), ImVec2(x / sx, y / sy),
                     ImVec2((x + w) / sx, (y + h) / sy));
        bg->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, NULL);
        sFrameInDrawList = true;
    }

    void CaptureD3D11(rw::d3d::impl11::D3d11Globals& g)
    {
        ID3D11Texture2D* backBuffer = NULL;
        if (FAILED(g.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer)) ||
            backBuffer == NULL)
        {
            WriteCapture(NULL, 0, 0, 0);
            return;
        }

        D3D11_TEXTURE2D_DESC desc;
        backBuffer->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;

        ID3D11Texture2D* staging = NULL;
        if (desc.SampleDesc.Count != 1 ||
            FAILED(rw::d3d::d3d11device->CreateTexture2D(&desc, NULL, &staging)))
        {
            backBuffer->Release();
            WriteCapture(NULL, 0, 0, 0);
            return;
        }

        ID3D11DeviceContext* ctx = rw::d3d::d3d11context;
        ctx->CopyResource(staging, backBuffer);
        backBuffer->Release();

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
        {
            staging->Release();
            WriteCapture(NULL, 0, 0, 0);
            return;
        }

        const int w = (int)desc.Width;
        const int h = (int)desc.Height;
        const bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                          desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
                          desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM;
        unsigned char* rgba = (unsigned char*)malloc((size_t)w * h * 4);
        if (rgba != NULL)
        {
            for (int y = 0; y < h; y++)
            {
                unsigned char* row = rgba + (size_t)y * w * 4;
                memcpy(row, (const unsigned char*)mapped.pData + (size_t)y * mapped.RowPitch,
                       (size_t)w * 4);
                if (bgra)
                {
                    SwizzleBGRA(row, w);
                }
            }
        }

        ctx->Unmap(staging, 0);
        staging->Release();

        WriteCapture(rgba, w, h, w * 4);
        free(rgba);
    }

    bool PresentD3D11(ImDrawData* drawData, rw::uint32 flags)
    {
        rw::d3d::impl11::D3d11Globals& g = rw::d3d::impl11::d3d11Globals;
        ID3D11DeviceContext* ctx = rw::d3d::d3d11context;
        if (g.backBufferTarget == NULL || g.swapChain == NULL || ctx == NULL)
        {
            return false;
        }

        ID3D11RenderTargetView* oldTarget = NULL;
        ID3D11DepthStencilView* oldDepth = NULL;
        ctx->OMGetRenderTargets(1, &oldTarget, &oldDepth);

        ctx->OMSetRenderTargets(1, &g.backBufferTarget, NULL);
        if (sFrameInDrawList)
        {
            const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            ctx->ClearRenderTargetView(g.backBufferTarget, black);
        }

        ImGui_ImplDX11_RenderDrawData(drawData);

        if (sCaptureArmed)
        {
            CaptureD3D11(g);
        }

        ctx->OMSetRenderTargets(1, &oldTarget, oldDepth);
        if (oldTarget != NULL)
        {
            oldTarget->Release();
        }
        if (oldDepth != NULL)
        {
            oldDepth->Release();
        }

        g.swapChain->Present((flags & rw::Raster::FLIPWAITVSYNCH) ? 1 : 0, 0);
        return true;
    }
#endif

#ifdef RW_GL3
    // ----------------------------------------------------------------- GL3

    // librw's present-overlay hook: the back buffer holds the stretched frame
    // and framebuffer 0 is bound.
    rw::bool32 DrawGL(rw::int32 winWidth, rw::int32 winHeight)
    {
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData != NULL)
        {
            // Laid out in the window's pixels, drawn into the surface's, which
            // on Android can be fewer (rw::gl3::getWindowDrawableSize).
            rw::int32 surfWidth = winWidth;
            rw::int32 surfHeight = winHeight;
            rw::gl3::getWindowDrawableSize(&surfWidth, &surfHeight);
            if ((surfWidth != winWidth || surfHeight != winHeight) && winWidth > 0 && winHeight > 0)
            {
                drawData->FramebufferScale =
                    ImVec2(drawData->FramebufferScale.x * (float)surfWidth / (float)winWidth,
                           drawData->FramebufferScale.y * (float)surfHeight / (float)winHeight);
            }
            ImGui_ImplOpenGL3_RenderDrawData(drawData);
        }

        if (sCaptureArmed && drawData != NULL)
        {
            const int w = (int)(drawData->DisplaySize.x * drawData->FramebufferScale.x);
            const int h = (int)(drawData->DisplaySize.y * drawData->FramebufferScale.y);
            unsigned char* rgba =
                (w > 0 && h > 0) ? (unsigned char*)malloc((size_t)w * h * 4) : NULL;
            if (rgba != NULL)
            {
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

                // GL's rows run bottom-up.
                unsigned char* tmp = (unsigned char*)malloc((size_t)w * 4);
                if (tmp != NULL)
                {
                    for (int y = 0; y < h / 2; y++)
                    {
                        unsigned char* a = rgba + (size_t)y * w * 4;
                        unsigned char* b = rgba + (size_t)(h - 1 - y) * w * 4;
                        memcpy(tmp, a, (size_t)w * 4);
                        memcpy(a, b, (size_t)w * 4);
                        memcpy(b, tmp, (size_t)w * 4);
                    }
                    free(tmp);
                }
            }
            WriteCapture(rgba, w, h, w * 4);
            free(rgba);
        }

        (void)winWidth;
        (void)winHeight;

        // librw forgets its GL state cache for a hook that says it drew.
        return 1;
    }

    // The GLSL the context speaks, from what librw actually got. ImGui's own
    // guess for a desktop context below 4.1 is "#version 130", which a 3.3
    // core profile is not obliged to accept.
    const char* GlslVersion()
    {
        const int v = rw::gl3::gl3Caps.glversion;
        if (rw::gl3::gl3Caps.gles)
        {
            return v >= 30 ? "#version 300 es" : "#version 100";
        }
        if (v >= 33)
        {
            return "#version 330 core";
        }
        if (v >= 32)
        {
            return "#version 150";
        }
        if (v >= 30)
        {
            return "#version 130";
        }
        return "#version 120";
    }
#endif

#ifdef RW_C3D
    // ------------------------------------------------------------------ C3D

    // A texture the device could not make: drawn as nothing.
    const ImTextureID kNoTexture = (ImTextureID)0x7FFF;

    ImGuiID sLastHash;
    bool sDrawDue = true;
    int sResumes = -1;
    bool sAlways; // TSSM_3DS_GUI_ALWAYS=1: every frame drawn, for measuring
    int sBright = 256; // iGuiRenderSetBrightness
    bool sStats; // TSSM_C3D_STATS=1
    rw::c3d::OverlayCmd* sCmds;
    int sCmdCap;
    // The stats line's sums.
    U32 sDrawn;
    U32 sSkipped;
    U32 sFrames;
    U64 sFrameNs;
    U64 sFrameNsMax;
    U64 sConvertNs;
    U64 sLastStats;
    bool sFirstLogged;

    void TextureC3D(ImTextureData* tex)
    {
        const int handle = (int)(intptr_t)tex->TexID;
        if (tex->Status == ImTextureStatus_WantCreate)
        {
            const int made = rw::c3d::overlayTexCreate(tex->Width, tex->Height);
            tex->SetTexID(made != 0 ? (ImTextureID)(intptr_t)made : kNoTexture);
            tex->UpdateRect.x = 0;
            tex->UpdateRect.y = 0;
            tex->UpdateRect.w = (unsigned short)tex->Width;
            tex->UpdateRect.h = (unsigned short)tex->Height;
        }
        if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates)
        {
            const int id = (int)(intptr_t)tex->TexID;
            // The whole texture when made, else each rectangle ImGui changed.
            const int n = tex->Status == ImTextureStatus_WantCreate ? 1 : tex->Updates.Size;
            for (int i = 0; i < n; i++)
            {
                const ImTextureRect r =
                    tex->Status == ImTextureStatus_WantCreate ? tex->UpdateRect : tex->Updates[i];
                if (tex->Format == ImTextureFormat_Alpha8)
                {
                    rw::c3d::overlayTexWrite(id, r.x, r.y, r.w, r.h,
                                             (const unsigned char*)tex->GetPixelsAt(r.x, r.y),
                                             tex->GetPitch());
                    continue;
                }
                // RGBA32 (not asked for): its alpha, a row at a time.
                unsigned char row[1024];
                for (int y = 0; y < r.h && r.w <= 1024; y++)
                {
                    const unsigned char* src = (const unsigned char*)tex->GetPixelsAt(r.x, r.y + y);
                    for (int x = 0; x < r.w; x++)
                    {
                        row[x] = src[x * 4 + 3];
                    }
                    rw::c3d::overlayTexWrite(id, r.x, r.y + y, r.w, 1, row, r.w);
                }
            }
            tex->SetStatus(ImTextureStatus_OK);
            sDrawDue = true;
        }
        else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0)
        {
            if (tex->TexID != kNoTexture)
            {
                rw::c3d::overlayTexDestroy(handle);
            }
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    }

    // What the frame would put on the screen.
    ImGuiID HashC3D(const ImDrawData* dd)
    {
        ImGuiID h = 0;
        for (int l = 0; l < dd->CmdLists.Size; l++)
        {
            const ImDrawList* list = dd->CmdLists[l];
            h = ImHashData(list->VtxBuffer.Data, (size_t)list->VtxBuffer.Size * sizeof(ImDrawVert),
                           h);
            h = ImHashData(list->IdxBuffer.Data, (size_t)list->IdxBuffer.Size * sizeof(ImDrawIdx),
                           h);
            for (int c = 0; c < list->CmdBuffer.Size; c++)
            {
                const ImDrawCmd& cmd = list->CmdBuffer[c];
                const ImTextureID id = cmd.UserCallback == NULL ? cmd.GetTexID() : 0;
                h = ImHashData(&cmd.ClipRect, sizeof(cmd.ClipRect), h);
                h = ImHashData(&id, sizeof(id), h);
                h = ImHashData(&cmd.ElemCount, sizeof(cmd.ElemCount), h);
                h = ImHashData(&cmd.IdxOffset, sizeof(cmd.IdxOffset), h);
            }
        }
        return h;
    }

    void StatsC3D()
    {
        const U64 now = SDL_GetTicksNS();
        if (sLastStats == 0)
        {
            sLastStats = now;
            return;
        }
        if (now - sLastStats < 1000000000ull || sFrames == 0)
        {
            return;
        }
        rw::c3d::OverlayStats o;
        rw::c3d::overlayTakeStats(&o);
        printf("tssm: gui: overlay: %u frames: ImGui %.2f ms a frame (max %.2f), conversion %.2f "
               "ms a drawn frame; %u drawn, %u unchanged; %u draws, %u indices, ring %u KB; %u "
               "textures, %u KB\n",
               (unsigned)sFrames, (double)sFrameNs / sFrames / 1e6, (double)sFrameNsMax / 1e6,
               sDrawn > 0 ? (double)sConvertNs / sDrawn / 1e6 : 0.0, (unsigned)sDrawn,
               (unsigned)sSkipped, o.draws, o.indices, o.ringBytes >> 10, o.textures,
               o.texBytes >> 10);
        fflush(stdout);
        sLastStats = now;
        sFrames = sDrawn = sSkipped = 0;
        sFrameNs = sFrameNsMax = sConvertNs = 0;
    }

    void PresentC3D(ImDrawData* dd)
    {
        if (dd == NULL || !rw::c3d::overlayIsOpen())
        {
            return;
        }
        if (dd->Textures != NULL)
        {
            for (int i = 0; i < dd->Textures->Size; i++)
            {
                TextureC3D((*dd->Textures)[i]);
            }
        }
        // After HOME or sleep, whatever the system left on the screen.
        if (i3DSResumeCount() != sResumes)
        {
            sResumes = i3DSResumeCount();
            sDrawDue = true;
        }
        const ImGuiID hash = HashC3D(dd);
        if (!sDrawDue && !sAlways && hash == sLastHash)
        {
            sSkipped++;
            return;
        }

        const U64 t0 = SDL_GetTicksNS();
        int cmds = 0;
        for (int l = 0; l < dd->CmdLists.Size; l++)
        {
            cmds += dd->CmdLists[l]->CmdBuffer.Size;
        }
        if (cmds > sCmdCap)
        {
            const int cap = cmds + 32;
            void* more = realloc(sCmds, (size_t)cap * sizeof(rw::c3d::OverlayCmd));
            if (more == NULL)
            {
                return;
            }
            sCmds = (rw::c3d::OverlayCmd*)more;
            sCmdCap = cap;
        }

        unsigned short* idx = NULL;
        rw::c3d::Im2DVertex* v = rw::c3d::overlayBegin((unsigned)dd->TotalVtxCount,
                                                       (unsigned)dd->TotalIdxCount, cmds, &idx);
        if (v == NULL)
        {
            return;
        }
        const ImVec2 origin = dd->DisplayPos;
        int n = 0;
        unsigned firstVertex = 0;
        unsigned firstIndex = 0;
        for (int l = 0; l < dd->CmdLists.Size; l++)
        {
            const ImDrawList* list = dd->CmdLists[l];
            for (int i = 0; i < list->VtxBuffer.Size; i++)
            {
                const ImDrawVert& src = list->VtxBuffer.Data[i];
                rw::c3d::Im2DVertex& dst = v[firstVertex + i];
                dst.x = src.pos.x - origin.x;
                dst.y = src.pos.y - origin.y;
                dst.z = 0.5f;
                dst.w = 1.0f;
                // IM_COL32 is R, G, B, A from the low byte up. Blending
                // keeps the scale: a*k over b*k is (a over b)*k.
                dst.r = (unsigned char)((((src.col >> IM_COL32_R_SHIFT) & 0xFF) * sBright) >> 8);
                dst.g = (unsigned char)((((src.col >> IM_COL32_G_SHIFT) & 0xFF) * sBright) >> 8);
                dst.b = (unsigned char)((((src.col >> IM_COL32_B_SHIFT) & 0xFF) * sBright) >> 8);
                dst.a = (unsigned char)(src.col >> IM_COL32_A_SHIFT);
                dst.u = src.uv.x;
                dst.v = src.uv.y;
            }
            memcpy(idx + firstIndex, list->IdxBuffer.Data,
                   (size_t)list->IdxBuffer.Size * sizeof(ImDrawIdx));
            for (int c = 0; c < list->CmdBuffer.Size; c++)
            {
                const ImDrawCmd& cmd = list->CmdBuffer[c];
                if (cmd.UserCallback != NULL || cmd.ElemCount == 0)
                {
                    continue;
                }
                rw::c3d::OverlayCmd& o = sCmds[n++];
                o.clip[0] = cmd.ClipRect.x - origin.x;
                o.clip[1] = cmd.ClipRect.y - origin.y;
                o.clip[2] = cmd.ClipRect.z - origin.x;
                o.clip[3] = cmd.ClipRect.w - origin.y;
                const ImTextureID id = cmd.GetTexID();
                o.tex = id == kNoTexture ? 0 : (int)(intptr_t)id;
                o.firstVertex = firstVertex + cmd.VtxOffset;
                o.firstIndex = firstIndex + cmd.IdxOffset;
                o.count = cmd.ElemCount;
            }
            firstVertex += (unsigned)list->VtxBuffer.Size;
            firstIndex += (unsigned)list->IdxBuffer.Size;
        }
        const U64 t1 = SDL_GetTicksNS();

        // The window's own colour (iGuiUtil.cpp's WindowBg), under it all.
        const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        const float k = (float)sBright / 256.0f;
        const unsigned clear = ((unsigned)(bg.x * k * 255.0f) << 24) |
                               ((unsigned)(bg.y * k * 255.0f) << 16) |
                               ((unsigned)(bg.z * k * 255.0f) << 8) | 0xFFu;
        rw::c3d::overlayDraw(sCmds, n, clear);

        sConvertNs += t1 - t0;
        sDrawn++;
        sLastHash = hash;
        sDrawDue = false;
        if (!sFirstLogged)
        {
            sFirstLogged = true;
            printf("tssm: gui: overlay: the bottom screen's first picture: %d draws, %d vertices, "
                   "%d indices\n",
                   n, dd->TotalVtxCount, dd->TotalIdxCount);
            fflush(stdout);
        }
    }

    // After librw's present: both screens as the console shows them, the top
    // above and the bottom centred below it (400 x 480).
    void CaptureC3D()
    {
        const int w = 400;
        const int h = 480;
        unsigned char* rgba = (unsigned char*)calloc((size_t)w * h, 4);
        unsigned char* bottom = (unsigned char*)malloc((size_t)320 * 240 * 4);
        bool ok = rgba != NULL && bottom != NULL && rw::c3d::readScreen(rgba, 400, 240) &&
                  rw::c3d::readBottom(bottom, 320, 240);
        if (ok)
        {
            for (int y = 0; y < 240; y++)
            {
                memcpy(rgba + ((size_t)(240 + y) * w + 40) * 4, bottom + (size_t)y * 320 * 4,
                       320 * 4);
            }
            for (int i = 0; i < w * h; i++)
            {
                rgba[i * 4 + 3] = 255;
            }
        }
        WriteCapture(ok ? rgba : NULL, ok ? w : 0, ok ? h : 0, w * 4);
        free(bottom);
        free(rgba);
    }
#endif

    // ------------------------------------------------------------------ hook

    void HookShowRaster(rw::Raster* raster, rw::uint32 flags)
    {
#ifdef RW_D3D9
        // A lost device is reset behind this hook (iDeviceReset.h), and the
        // reset needs ImGui's default-pool objects gone first.
        if (sKind == KIND_D3D9 && rw::d3d::d3ddevice->TestCooperativeLevel() != D3D_OK)
        {
            ImGui_ImplDX9_InvalidateDeviceObjects();
            sLibrwShowRaster(raster, flags);
            return;
        }
#endif

        // A loading frame (iLoadFrame.h) is the game's picture alone: the overlay
        // waits for the next frame of the game's own, and so does its count of
        // presents, which the test scripts open it by.
        if (iLoadFrameDrawing())
        {
            sLibrwShowRaster(raster, flags);
            return;
        }

#ifdef RW_C3D
        const U64 frameAt = sKind == KIND_C3D ? SDL_GetTicksNS() : 0;
#endif
        if (sFrame == NULL || !sFrame())
        {
            sLibrwShowRaster(raster, flags);
            return;
        }
#ifdef RW_C3D
        if (sKind == KIND_C3D)
        {
            const U64 spent = SDL_GetTicksNS() - frameAt;
            sFrames++;
            sFrameNs += spent;
            sFrameNsMax = spent > sFrameNsMax ? spent : sFrameNsMax;
        }
#endif

        ImDrawData* drawData = ImGui::GetDrawData();
        bool presented = false;

        switch (sKind)
        {
#ifdef RW_D3D9
        case KIND_D3D9:
            presented = PresentD3D9(drawData);
            break;
#endif
#ifdef RW_D3D11
        case KIND_D3D11:
            presented = PresentD3D11(drawData, flags);
            break;
#endif
#ifdef RW_GL3
        case KIND_GL3:
            rw::gl3::setPresentOverlay(DrawGL);
            sLibrwShowRaster(raster, flags);
            iTouchOverlayInstall();
            presented = true;
            break;
#endif
#ifdef RW_C3D
        case KIND_C3D:
            PresentC3D(drawData);
            sLibrwShowRaster(raster, flags);
            if (sCaptureArmed)
            {
                CaptureC3D();
            }
            if (sStats)
            {
                StatsC3D();
            }
            presented = true;
            break;
#endif
        default:
            break;
        }

        if (!presented)
        {
            sLibrwShowRaster(raster, flags);
        }
    }
} // namespace

bool iGuiRenderInit(iGuiRenderFrameFn frame, const char** name)
{
    *name = "none";
    if (rw::engine == NULL || sKind != KIND_NONE)
    {
        return false;
    }

    switch (iScreenGetBackend())
    {
#ifdef RW_D3D9
    case iSCREENBACKEND_D3D9:
        *name = "Direct3D 9";
        if (rw::d3d::d3ddevice == NULL || !ImGui_ImplDX9_Init(rw::d3d::d3ddevice))
        {
            return false;
        }
        sKind = KIND_D3D9;
        iDeviceResetSetReleaseHook(iGuiRenderReleaseVolatile);
        break;
#endif
#ifdef RW_D3D11
    case iSCREENBACKEND_D3D11:
    {
        *name = "Direct3D 11";
        if (rw::d3d::d3d11device == NULL ||
            !ImGui_ImplDX11_Init(rw::d3d::d3d11device, rw::d3d::d3d11context))
        {
            return false;
        }

        D3D11_BLEND_DESC blend;
        memset(&blend, 0, sizeof(blend));
        blend.RenderTarget[0].BlendEnable = FALSE;
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(rw::d3d::d3d11device->CreateBlendState(&blend, &sOpaque)))
        {
            sOpaque = NULL;
        }
        sKind = KIND_D3D11;
        break;
    }
#endif
#ifdef RW_GL3
    case iSCREENBACKEND_GL3:
        *name = "OpenGL 3";
        if (!ImGui_ImplOpenGL3_Init(GlslVersion()))
        {
            return false;
        }
        sKind = KIND_GL3;
        break;
#endif
#ifdef RW_C3D
    case iSCREENBACKEND_C3D:
    {
        *name = "citro3d, the bottom screen";
        const char* depth = getenv("TSSM_3DS_GUI_DEPTH");
        if (!rw::c3d::overlayOpen(depth != NULL && atoi(depth) != 0))
        {
            return false;
        }
        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererName = "tssm_c3d";
        // ImGui's texture requests; no vertex offset, so each list keeps under
        // 64K vertices and is a draw base of its own.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
        const char* always = getenv("TSSM_3DS_GUI_ALWAYS");
        sAlways = always != NULL && atoi(always) != 0;
        const char* stats = getenv("TSSM_C3D_STATS");
        sStats = stats != NULL && stats[0] != '\0' && stats[0] != '0';
        sDrawDue = true;
        sKind = KIND_C3D;
        break;
    }
#endif
    case iSCREENBACKEND_VULKAN:
        *name = "Vulkan";
        return false;
    default:
        *name = iScreenBackendName(iScreenGetBackend());
        return false;
    }

    sFrame = frame;
    sLibrwShowRaster = rw::engine->device.showRaster;
    rw::engine->device.showRaster = HookShowRaster;
    return true;
}

void iGuiRenderShutdown()
{
    if (sKind == KIND_NONE)
    {
        return;
    }

    if (rw::engine != NULL && rw::engine->device.showRaster == HookShowRaster)
    {
        rw::engine->device.showRaster = sLibrwShowRaster;
    }

    switch (sKind)
    {
#ifdef RW_D3D9
    case KIND_D3D9:
        iDeviceResetSetReleaseHook(NULL);
        ImGui_ImplDX9_Shutdown();
        break;
#endif
#ifdef RW_D3D11
    case KIND_D3D11:
        ReleaseFrameView();
        if (sOpaque != NULL)
        {
            sOpaque->Release();
            sOpaque = NULL;
        }
        ImGui_ImplDX11_Shutdown();
        break;
#endif
#ifdef RW_GL3
    case KIND_GL3:
        ImGui_ImplOpenGL3_Shutdown();
        break;
#endif
#ifdef RW_C3D
    case KIND_C3D:
    {
        // ImGui's textures, while the device can still take them (after
        // citro3d's close they are gone with it).
        ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        for (int i = 0; i < pio.Textures.Size; i++)
        {
            ImTextureData* tex = pio.Textures[i];
            if (tex->TexID != ImTextureID_Invalid && tex->TexID != kNoTexture &&
                rw::c3d::overlayIsOpen())
            {
                rw::c3d::overlayTexDestroy((int)(intptr_t)tex->TexID);
            }
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
        free(sCmds);
        sCmds = NULL;
        sCmdCap = 0;
        ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_RendererHasTextures;
        break;
    }
#endif
    default:
        break;
    }

    sKind = KIND_NONE;
    sFrame = NULL;
}

void iGuiRenderNewFrame()
{
    switch (sKind)
    {
#ifdef RW_D3D9
    case KIND_D3D9:
        ImGui_ImplDX9_NewFrame();
        break;
#endif
#ifdef RW_D3D11
    case KIND_D3D11:
        ImGui_ImplDX11_NewFrame();
        break;
#endif
#ifdef RW_GL3
    case KIND_GL3:
        ImGui_ImplOpenGL3_NewFrame();
        break;
#endif
    default:
        break;
    }
}

void iGuiRenderBackground()
{
#ifdef RW_D3D11
    if (sKind == KIND_D3D11)
    {
        BackgroundD3D11();
    }
#endif
}

void iGuiRenderReleaseVolatile()
{
#ifdef RW_D3D9
    if (sKind == KIND_D3D9)
    {
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }
#endif
#ifdef RW_C3D
    // The overlay closed: the next picture (the closed screen's) is drawn.
    sDrawDue = true;
#endif
}

void iGuiRenderSetBrightness(int bright)
{
#ifdef RW_C3D
    bright = bright < 0 ? 0 : (bright > 256 ? 256 : bright);
    if (bright != sBright)
    {
        sBright = bright;
        sDrawDue = true;
    }
#else
    (void)bright;
#endif
}

void iGuiRenderRequestCapture(const char* path)
{
    snprintf(sCapturePath, sizeof(sCapturePath), "%s", path != NULL ? path : "");
    sCaptureArmed = sCapturePath[0] != '\0';
    sCaptureDone = false;
}

bool iGuiRenderCaptureDone()
{
    return sCaptureDone;
}
