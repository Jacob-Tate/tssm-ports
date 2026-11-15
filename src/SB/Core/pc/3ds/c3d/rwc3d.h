#ifndef RWC3D_H
#define RWC3D_H

// rw::c3d: librw's render device for the Nintendo 3DS's PICA200, on citro3d.
// docs/3DS.md says what is drawn and how.
//
// The backend is compiled into the librw target (CMakeLists.txt, TSSM_3DS with
// C3D in TSSM_RENDER_BACKENDS), with libctru's and citro3d's short enums. This
// header is the part the rest of the port sees, which is compiled with
// int-sized enums: so it uses only C types and no enum, as 3ds/i3DS.h does.
// librw's own types appear only as pointers.

namespace rw
{
    struct Raster;
    struct Device;
    struct Camera;

    namespace c3d
    {
        // librw's hooks (tools/librw/c3d_platform.patch).
        void registerPlatformPlugins(void);
        void initSkin(void);
        void initMatFX(void);
        extern Device renderdevice;

        // What RwEngineOpen hands Engine::open. The top screen's size is
        // fixed (400 x 240); the rest has defaults when zero.
        struct OpenParams
        {
            int cmdBufBytes; // citro3d's command buffer; 0: 2 MB
            int frameRate; // video.framerate, as setFrameRate takes it (0: every vblank)
            // [3ds] effects: the effects not yet shown to work on a console
            // that are turned on ("fog,rtt,..."; nil or "none": none).
            const char* effects;
            // [video] stereo (auto, always or off; nil: auto) and stereo_depth
            // (0..100).
            const char* stereo;
            int stereoDepth;
        };

        // TRUE once the device is open (citro3d up, the top screen ours).
        int deviceOpen(void);

        // TRUE when `sizes` (c3dlayout.h's list, measured by the caller with
        // int-sized enums) is what this side, compiled with short enums,
        // measures; else says which entry differs.
        int layoutAgrees(const unsigned* sizes, int count);

        // The device paces frames on the display's vblank itself
        // (C3D_FrameBegin), so iWindowPaceFrame stands down while it is open.
        int pacesFrames(void);

        // video.framerate, as citro3d's frame counter's rate (0: every vblank).
        // Before or after the device opens.
        void setFrameRate(int fps);

        // The 2D vertices, as the device reads them: the same bytes as GL3's
        // and D3D's (x, y, z, w; b, g, r, a; u, v). rw/layout_im2d.cpp
        // asserts RwIm2DVertex and RwIm3DVertex against these.
        struct Im2DVertex
        {
            float x, y, z, w;
            unsigned char b, g, r, a;
            float u, v;
        };

        struct Im3DVertex
        {
            float x, y, z;
            float nx, ny, nz;
            unsigned char b, g, r, a;
            float u, v;
        };

        // A texture read off the Xbox files, as the 3DS keeps it: decoded on
        // the CPU, half size (full size for the palettised font atlases),
        // RGB565, RGBA5551 or RGBA4 by what its top level holds, mips down to
        // 8 x 8, tiled, in VRAM (linear memory when VRAM is full). NULL when
        // the raster is not one it can read; the caller keeps the source
        // raster then. `paddedRGB`: rw/texture.cpp's C888-in-32-bits case,
        // whose fourth byte is padding, not alpha.
        Raster* convertXboxRaster(Raster* xbox, int paddedRGB);

        // The frame just presented, read back from the top screen's colour
        // buffer: `width` x `height` (400 x 240) RGBA bytes, top row first.
        // Only between a present and the next frame's first draw (the frame
        // hook, after RwCameraShowRaster). FALSE when there is nothing to read.
        int readScreen(unsigned char* rgba, int width, int height);

        // Counters for the log (one line a second with TSSM_C3D_STATS=1, and
        // after each level load).
        struct Stats
        {
            unsigned frames;
            unsigned draws; // last frame
            unsigned drawsPeak; // since the last report
            unsigned cmdBufPeak; // bytes, since the last report
            unsigned ringPeak; // bytes, since the last report
            unsigned queuePeak; // GX queue entries in a frame, since the last report
            unsigned textures; // alive
            unsigned texVram; // bytes
            unsigned texLinear; // bytes (VRAM was full, or made inside a frame)
            unsigned vramFree;
            unsigned linearFree;
            // Per frame, peaks since the last report:
            unsigned skinVertsPeak; // vertices skinned on the GPU
            unsigned skinPiecesPeak; // skin pieces drawn
            unsigned boneRegsPeak; // palette registers sent
            unsigned envDrawsPeak; // draws with an environment map
            unsigned skinUsPeak; // microseconds of CPU for the bone matrices
            unsigned hardSplits; // since the device opened
            unsigned targets; // camera textures with storage
            unsigned targetVram; // bytes, their colour and depth
            unsigned fogDrawsPeak; // draws through the fog stage
            unsigned targetBindsPeak; // render targets bound in a frame
            unsigned rttDrawsPeak; // draws into camera textures
            unsigned rttClearsPeak; // camera texture clears
            unsigned readbacksPeak; // camera textures read back (hard splits)
            unsigned copiesPeak; // screen copies (rw/screenpass.cpp's, the still's)
        };
        void getStats(Stats* out);

        // The picture so far into `dst`, a camera texture (rw/screenpass.cpp's
        // copyFrame, the loading-screen still): the top screen copied by the
        // GX engine into a 256 x 512 block, which the raster samples turned
        // back to landscape. The passes' copies share one block (each is drawn
        // from before the next copy); `own` gives the raster one of its own,
        // for a picture that must outlast the frame. FALSE: not a camera
        // texture, or no memory.
        int copyScreen(Raster* dst, int own);

        // Whether a screen pass may run (TSSM_C3D_GLOW, TSSM_C3D_WARP and the
        // A/B switches; the glow also needs rtt): 0 the HDR glow, 1 the
        // screen warp.
        int screenPassOn(int pass);
        // Whether the glow clears its halving and bright targets before drawing
        // each over whole (glowclear; off: the PICA draws them for nothing).
        int screenPassInvalidates(void);
        // rw's iScreenPassBegin (1) and iScreenPassEnd (0): the pass
        // screenPassOn let run is drawing in between (probe 14's gpusplit).
        void screenPassScope(int begin);

        // TSSM_C3D_AB's switches (c3dab.cpp): called after each present with
        // the capture hook's frame number.
        void testFrame(int frame);

        // ------------------------------------------- stereoscopic 3D
        // (c3dstereo.cpp; docs/3DS.md "Stereoscopic 3D"). The right eye is the
        // frame's command lists sent to the GPU a second time with the other
        // eye's shift.

        // The settings menu (gui/iGuiSettings.cpp): [video] stereo (auto,
        // always or off; anything else is auto) and stereo_depth (0..100) while
        // the game runs. Taken at the next present; 3D eases in or out from
        // there, and auto's speed guard starts afresh.
        void stereoSetMode(const char* setting);
        void stereoSetDepth(int depth);

        // Once a frame, at the present before it, the device asks the game
        // (3ds/iStereo3DS.cpp) for the convergence plane: the hook calls
        // stereoFocus with the game's main camera (only it is drawn per eye),
        // the player's position (nil: none), whether the camera follows the
        // player (`fixed`: a fly-through, a cutscene, tracking off; the plane
        // is then held), and whether the game is outside play (`flat`: the
        // title, the pause menu and the other menus, whose 3D models stay at
        // the screen with the text).
        typedef void (*StereoFocusHook)(void);
        void setStereoFocusHook(StereoFocusHook hook);
        void stereoFocus(Camera* mainCamera, const float* playerPos, int fixed, int flat);

        // The per-eye captures (iCapture.cpp): whether the frame just
        // presented had two eyes; that the next frame keep its left eye for
        // readScreen; and which eye readScreen reads (1 the left of a 3D
        // frame, 0 the right one, or the only one of a 2D frame).
        int frameWasStereo(void);
        void stereoCaptureLeft(int on);
        void readScreenEye(int left);

        // For the error box and the quit (3ds/i3DS.cpp): the GPU is waited
        // for, what it read freed and citro3d shut down. Nothing draws
        // afterwards.
        void shutdown(void);

        // ------------------------------------------------ the bottom screen
        // The settings overlay's (gui/iGuiRender.cpp, c3doverlay.cpp): a
        // target of its own on the bottom screen, ImGui's one-byte-a-texel
        // textures in linear memory, and one batch of 2D draws a frame,
        // through the device's 2D program (no program of its own). Drawn at
        // the present, inside the frame, so one frame end shows both screens;
        // a frame that does not draw it leaves its picture as it was.

        // The target, once, right after the device opens (TRUE), or FALSE
        // with the reason logged: the overlay then stays off. `depth` adds a
        // D16 buffer (TSSM_3DS_GUI_DEPTH=1).
        int overlayOpen(int depth);
        int overlayIsOpen(void);

        // A w x h texture of one alpha byte a texel (ImGui's font atlas),
        // padded to powers of two (8 to 1024); 0: no memory. Written by the
        // CPU in place, rows top first, `pitch` bytes apart; at the present
        // (the GPU is not reading it then). Destroyed after the GPU is done.
        int overlayTexCreate(int w, int h);
        void overlayTexWrite(int tex, int x, int y, int w, int h, const unsigned char* alpha,
                             int pitch);
        void overlayTexDestroy(int tex);

        // One draw: an index range of one list's vertices with one texture,
        // clipped to a rectangle of landscape pixels (0..320, 0..240).
        struct OverlayCmd
        {
            float clip[4]; // x0, y0, x1, y1
            int tex; // overlayTexCreate's
            unsigned firstVertex; // the list's first vertex in the batch
            unsigned firstIndex;
            unsigned count; // indices, a multiple of 3
        };

        // Room in this frame for `vertices` 2D vertices (pixels, z 0, w 1)
        // and `indices` 16-bit indices, *indexOut set to where those go, with
        // room in the command buffer for `cmds` draws: nothing can split the
        // frame between here and overlayDraw. nil: there is no overlay.
        Im2DVertex* overlayBegin(unsigned vertices, unsigned indices, int cmds,
                                 unsigned short** indexOut);
        // The batch on the bottom screen, over a fill of `clear` (0xRRGGBBAA).
        void overlayDraw(const OverlayCmd* cmds, int count, unsigned clear);

        // The bottom screen's picture as drawn last, 320 x 240 RGBA, top row
        // first; between a present and the next draw, as readScreen. FALSE:
        // nothing to read.
        int readBottom(unsigned char* rgba, int width, int height);

        // What the overlay drew, since the last call: frames, draws, indices
        // and frame-ring bytes; textures alive and their bytes.
        struct OverlayStats
        {
            unsigned frames;
            unsigned draws;
            unsigned indices;
            unsigned ringBytes;
            unsigned textures;
            unsigned texBytes;
        };
        void overlayTakeStats(OverlayStats* out);
    } // namespace c3d
} // namespace rw

#endif
