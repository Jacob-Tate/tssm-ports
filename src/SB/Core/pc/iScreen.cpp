// The render size. The argument for it being one number is in iScreen.h.

#include "iScreen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{
    // Retail's framebuffer, and the port's default. Everything the game draws
    // in pixels was authored against these two numbers.
    const S32 kDefaultWidth = 640;
    const S32 kDefaultHeight = 480;

    // D3D9's largest guaranteed texture dimension on hardware that reports
    // D3DPTEXTURECAPS_POW2 relief is 8192 on everything shipped this century,
    // and a render target is a texture. Past this the camera rasters simply
    // fail to allocate, which surfaces much further on as a black screen; a
    // number this far out is a typo rather than a request.
    const S32 kMaxDimension = 16384;

    // The shape everything the game draws was authored in.
    const F32 kUIAspect = 4.0f / 3.0f;

    S32 sWidth = kDefaultWidth;
    S32 sHeight = kDefaultHeight;

    // Derived once here rather than at every call site, all of which are in
    // per-frame drawing code.
    F32 sAspect = (F32)kDefaultHeight / (F32)kDefaultWidth;
    F32 sUIWidth = (F32)kDefaultWidth;
    F32 sUIHeight = (F32)kDefaultHeight;
    F32 sUIOriginX = 0.0f;
    F32 sUIOriginY = 0.0f;
    F32 sUIFracX = 1.0f;
    F32 sUIFracY = 1.0f;
    F32 sUIMarginX;
    F32 sUIMarginY;

    iScreenUIMode sUIMode = iSCREENUI_PILLARBOX;

    // How far short of the real edges the HUD stops, as a share of the
    // screen's width (height, on a tall screen) at each side.
    F32 sUISafeMargin = 0.0f;

    // The offset in force, in UI units. Set per widget, because which edge a
    // widget belongs to is a property of the widget and not of the screen.
    F32 sAnchorOffsetX = 0.0f;
    F32 sAnchorOffsetY = 0.0f;

    // Whether a HUD widget's anchor is set (its offset may be zero: paused,
    // or no reach), and how deep the menus' cut scope is.
    bool sAnchorSet;
    S32 sBoxClipDepth;

    // How far an anchored widget travels: the screen's reach outside the box,
    // less the safe margin, and never inwards.
    F32 anchorReach(F32 margin, F32 frac)
    {
        const F32 reach = margin - sUISafeMargin / frac;
        return reach > 0.0f ? reach : 0.0f;
    }

    // The largest 4:3 rectangle that fits, centred. The same fit iFMV.cpp puts
    // a 4:3 movie through, and for the same reason: what is left over stays
    // black rather than being filled by stretching the picture into it.
    void deriveUIBox()
    {
        F32 w = (F32)sWidth;
        F32 h = (F32)sHeight;

        sAspect = h / w;

        sUIWidth = w;
        sUIHeight = w / kUIAspect;
        if (sUIHeight > h)
        {
            sUIHeight = h;
            sUIWidth = h * kUIAspect;
        }

        sUIOriginX = 0.5f * (w - sUIWidth);
        sUIOriginY = 0.5f * (h - sUIHeight);

        sUIFracX = sUIWidth / w;
        sUIFracY = sUIHeight / h;

        // How far outside the box the screen reaches, in UI units. Zero
        // when the box IS the screen, which is every 4:3 render size.
        sUIMarginX = (1.0f - sUIFracX) / (2.0f * sUIFracX);
        sUIMarginY = (1.0f - sUIFracY) / (2.0f * sUIFracY);
    }
}

S32 iScreenWidth()
{
    return sWidth;
}

S32 iScreenHeight()
{
    return sHeight;
}

F32 iScreenWidthF()
{
    return (F32)sWidth;
}

F32 iScreenHeightF()
{
    return (F32)sHeight;
}

F32 iScreenAspectF()
{
    return sAspect;
}

F32 iScreenUIWidthF()
{
    return sUIWidth;
}

F32 iScreenUIHeightF()
{
    return sUIHeight;
}

F32 iScreenUIOriginXF()
{
    return sUIOriginX;
}

F32 iScreenUIOriginYF()
{
    return sUIOriginY;
}

F32 iScreenUIFracXF()
{
    return sUIFracX;
}

F32 iScreenUIFracYF()
{
    return sUIFracY;
}

F32 iScreenUIMarginXF()
{
    return sUIMarginX;
}

F32 iScreenUIMarginYF()
{
    return sUIMarginY;
}

F32 iScreenAnchorMarginXF()
{
    return iScreenUIModeInForce() == iSCREENUI_NATIVE ? anchorReach(sUIMarginX, sUIFracX) : 0.0f;
}

F32 iScreenAnchorMarginYF()
{
    return iScreenUIModeInForce() == iSCREENUI_NATIVE ? anchorReach(sUIMarginY, sUIFracY) : 0.0f;
}

iScreenUIMode iScreenGetUIMode()
{
    return sUIMode;
}

void iScreenSetUIMode(iScreenUIMode mode)
{
    sUIMode = mode;

    // Nothing is anchored until a widget asks to be, and pillarbox never
    // anchors anything at all.
    sAnchorOffsetX = 0.0f;
    sAnchorOffsetY = 0.0f;
    sAnchorSet = false;
}

S32 iScreenUITall()
{
    // In whole pixels, so a 4:3 size is never taller by a rounding error.
    return 4 * sHeight > 3 * sWidth;
}

iScreenUIMode iScreenUIModeInForce()
{
    return iScreenUITall() ? iSCREENUI_NATIVE : sUIMode;
}

iScreenUIMode iScreenUIModeFromName(const char* name)
{
    // The config reader hands values over as written; compare without case.
    // By hand, like the aspect names below: tssm_hudtest links this file alone.
    static const char kNative[] = "native";
    if (name == NULL)
    {
        return iSCREENUI_PILLARBOX;
    }
    size_t i = 0;
    for (; kNative[i] != '\0'; i++)
    {
        const char c = name[i];
        if (((c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c) != kNative[i])
        {
            return iSCREENUI_PILLARBOX;
        }
    }
    return name[i] == '\0' ? iSCREENUI_NATIVE : iSCREENUI_PILLARBOX;
}

F32 iScreenGetUISafeMargin()
{
    return sUISafeMargin;
}

void iScreenSetUISafeMargin(F32 share)
{
    sUISafeMargin = share < 0.0f ? 0.0f : share > 0.25f ? 0.25f : share;
}

void iScreenSetAnchor(F32 sideX, F32 sideY)
{
    sAnchorOffsetX = sideX * iScreenAnchorMarginXF();
    sAnchorOffsetY = sideY * iScreenAnchorMarginYF();
    sAnchorSet = true;
}

void iScreenClearAnchor()
{
    sAnchorOffsetX = 0.0f;
    sAnchorOffsetY = 0.0f;
    sAnchorSet = false;
}

void iScreen2DBoxClipBegin()
{
    sBoxClipDepth++;
}

void iScreen2DBoxClipEnd()
{
    if (sBoxClipDepth > 0)
    {
        sBoxClipDepth--;
    }
}

S32 iScreen2DClipY(F32* top, F32* bottom)
{
    if (!iScreenUITall() || (!sAnchorSet && sBoxClipDepth == 0))
    {
        return FALSE;
    }

    // The box, moved by the anchor in force (none for the menus): the 4:3
    // screen the draw was made for.
    *top = sUIOriginY + sAnchorOffsetY * sUIHeight;
    *bottom = *top + sUIHeight;
    return TRUE;
}

F32 iScreenAnchorX(F32 x)
{
    return x + sAnchorOffsetX;
}

F32 iScreenAnchorY(F32 y)
{
    return y + sAnchorOffsetY;
}

// Samples per pixel, from config.ini's video.msaa. Held here for the reason the
// render size is: RenderWareInit reads it when it makes the render surfaces,
// and the code that does must not learn what config.ini is.
static S32 sMultiSample = 1;

S32 iScreenMultiSample()
{
    return sMultiSample;
}

void iScreenSetMultiSample(S32 samples)
{
    // 1 is off. D3D9 names its levels after the sample count, so the number is
    // the setting, and anything that is not a level the device grants falls
    // back to none when the surfaces are made.
    if (samples < 1 || samples > 16)
    {
        printf("tssm: %d is not a sample count this can render at; staying at %d\n",
               (int)samples, (int)sMultiSample);
        fflush(stdout);
        return;
    }

    sMultiSample = samples;
}

// What the device gave, once it is open; 0 before. The settings show it beside
// the request, which a card can turn down (8x on a phone that takes 4x).
static S32 sMultiSampleGranted = 0;

S32 iScreenMultiSampleGranted()
{
    return sMultiSampleGranted;
}

void iScreenSetMultiSampleGranted(S32 samples)
{
    sMultiSampleGranted = samples;
}

// Whether lighting is summed per pixel rather than per vertex. Held here with
// the other two for the same reason: RenderWareInit pushes it into librw, and
// the code that does must not learn what config.ini is.
static S32 sPerPixelLighting = 1;
static S32 sWorldLighting = 0;
static F32 sWorldLightContrast = 1.0f;
static S32 sWorldLightShadows = 0;

S32 iScreenPerPixelLighting()
{
    return sPerPixelLighting;
}

void iScreenSetPerPixelLighting(S32 on)
{
    sPerPixelLighting = on ? 1 : 0;
}

// Which D3D9 path draws. Held here with the three above for the same reason:
// RenderWareInit pushes it into librw, and the code that does must not learn
// what config.ini is. Resolved out of AUTO there, where the adapter caps are.
static iScreenPipeline sPipeline = iSCREENPIPE_AUTO;

iScreenPipeline iScreenGetPipeline()
{
    return sPipeline;
}

void iScreenSetPipeline(iScreenPipeline pipeline)
{
    sPipeline = pipeline;
}

// The render backend, the same way and for the same reason: set from config.ini
// before the window opens, and resolved out of AUTO in RenderWareInit, which is
// the only place that knows which backends this build actually carries.
static iScreenBackend sBackend = iSCREENBACKEND_AUTO;

iScreenBackend iScreenGetBackend()
{
    return sBackend;
}

void iScreenSetBackend(iScreenBackend backend)
{
    sBackend = backend;
}

const char* iScreenBackendName(iScreenBackend backend)
{
    switch (backend)
    {
    case iSCREENBACKEND_D3D9:
        return "d3d9";
    case iSCREENBACKEND_D3D11:
        return "d3d11";
    case iSCREENBACKEND_GL3:
        return "gl3";
    case iSCREENBACKEND_VULKAN:
        return "vulkan";
    case iSCREENBACKEND_NULL:
        return "null";
    default:
        return "auto";
    }
}

S32 iScreenWorldLighting()
{
    return sWorldLighting;
}

void iScreenSetWorldLighting(S32 mode)
{
    sWorldLighting = (mode < IWORLDLIGHT_OFF || mode > IWORLDLIGHT_BAKE) ? IWORLDLIGHT_OFF : mode;
}

F32 iScreenWorldLightContrast()
{
    return sWorldLightContrast;
}

void iScreenSetWorldLightContrast(F32 contrast)
{
    // A negative swing would put the lit side in shadow. Zero is flat, which is
    // a thing somebody might want to look at.
    sWorldLightContrast = contrast < 0.0f ? 0.0f : contrast;
}

S32 iScreenWorldLightShadows()
{
    return sWorldLightShadows;
}

void iScreenSetWorldLightShadows(S32 on)
{
    sWorldLightShadows = on ? 1 : 0;
}

// The cartoon look. Nothing below decides anything; iToon.h says what each one
// does, and this is only where they are kept between being read out of
// config.ini and being pushed at the renderer.
static S32 sToon;
static F32 sToonBands = 3.0f;
static F32 sToonSaturation = 1.5f;
static F32 sToonOutline = 0.05f;
static F32 sToonStrength = 1.0f;
static S32 sToonFaceLight;
static F32 sToonColors;
static F32 sToonWrap;
static F32 sToonRim;
static F32 sToonOcclusion;
static F32 sToonHardness;
static F32 sToonOutlineMin;
static F32 sToonOutlineMax;
static S32 sToonFlatRim = 1;
static S32 sToonRimBlend = ITOON_RIM_ROOM;
static S32 sToonSkyBright = 1;
static S32 sToonTikiRim;
static S32 sToonFlatSmooth = 1;
static F32 sToonGooWave = 0.35f;
static F32 sToonGooGloss = 0.5f;
static F32 sToonGooGlossEdge = 0.92f;
static F32 sToonFlatStrength = 0.75f;
static F32 sToonOutlineBias;
static F32 sToonTextBrightness = 1.0f;
static F32 sToonTextSaturation = 1.0f;
static S32 sToonAll = 1;
static S32 sSolidFlatProps = 1;
static S32 sWorldOutline = 1;
static F32 sWorldModelShade = 0.8f;
static F32 sToonRoomLevel = 1.0f;
static F32 sToonInk = 0.35f;
static F32 sToonInkSaturation = 1.0f;
static F32 sToonInkGamma = 1.0f;

S32 iScreenToon()
{
    return sToon;
}

F32 iScreenToonBands()
{
    return sToonBands;
}

F32 iScreenToonSaturation()
{
    return sToonSaturation;
}

F32 iScreenToonOutline()
{
    return sToonOutline;
}

F32 iScreenToonStrength()
{
    return sToonStrength;
}

S32 iScreenToonFaceLight()
{
    return sToonFaceLight;
}

F32 iScreenToonRoomLevel()
{
    return sToonRoomLevel;
}

void iScreenSetToonRoomLevel(F32 power)
{
    sToonRoomLevel = power < 1.0f ? 1.0f : (power > 4.0f ? 4.0f : power);
}

F32 iScreenToonInk()
{
    return sToonInk;
}

F32 iScreenToonInkSaturation()
{
    return sToonInkSaturation;
}

F32 iScreenToonInkGamma()
{
    return sToonInkGamma;
}

void iScreenSetToonInk(F32 scale, F32 saturation, F32 gamma)
{
    sToonInk = scale < 0.0f ? 0.0f : (scale > 1.0f ? 1.0f : scale);
    sToonInkSaturation = saturation < 0.0f ? 0.0f : saturation;
    sToonInkGamma = gamma < 0.05f ? 0.05f : gamma;
}

F32 iScreenWorldModelShade()
{
    // Both are what makes it reach the picture; see iScreen.h.
    if (!iScreenToon() || iScreenWorldLightShadows())
    {
        return 0.0f;
    }

    return sWorldModelShade;
}

void iScreenSetWorldModelShade(F32 amount)
{
    sWorldModelShade = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);
}

S32 iScreenWorldOutline()
{
    return sWorldOutline;
}

void iScreenSetWorldOutline(S32 on)
{
    sWorldOutline = on ? 1 : 0;
}

S32 iScreenSolidFlatProps()
{
    return sSolidFlatProps;
}

void iScreenSetSolidFlatProps(S32 on)
{
    sSolidFlatProps = on ? 1 : 0;
}

S32 iScreenToonAll()
{
    return sToonAll;
}

void iScreenSetToonAll(S32 on)
{
    sToonAll = on ? 1 : 0;
}

void iScreenSetToon(S32 on, F32 bands, F32 saturation, F32 outline, F32 strength, S32 faceLight)
{
    sToon = on ? 1 : 0;
    sToonBands = bands;
    sToonSaturation = saturation;
    sToonOutline = on ? outline : 0.0f;
    sToonStrength = strength;
    sToonFaceLight = on ? faceLight : 0;
}

F32 iScreenToonColors()
{
    return sToonColors;
}

void iScreenSetToonFlatten(F32 colors)
{
    sToonColors = colors;
}

F32 iScreenToonWrap()
{
    return sToonWrap;
}

F32 iScreenToonRim()
{
    return sToonRim;
}

F32 iScreenToonOcclusion()
{
    return sToonOcclusion;
}

F32 iScreenToonHardness()
{
    return sToonHardness;
}

F32 iScreenToonOutlineMin()
{
    return sToonOutlineMin;
}

F32 iScreenToonOutlineMax()
{
    return sToonOutlineMax;
}

F32 iScreenToonGooWave()
{
    return sToonGooWave;
}

void iScreenSetToonGooWave(F32 slope)
{
    sToonGooWave = slope;
}

F32 iScreenToonGooGloss()
{
    return sToonGooGloss;
}

void iScreenSetToonGooGloss(F32 amount)
{
    sToonGooGloss = amount;
}

F32 iScreenToonGooGlossEdge()
{
    return sToonGooGlossEdge;
}

void iScreenSetToonGooGlossEdge(F32 edge)
{
    sToonGooGlossEdge = edge;
}

F32 iScreenToonFlatStrength()
{
    return sToonFlatStrength;
}

void iScreenSetToonFlatStrength(F32 strength)
{
    sToonFlatStrength = strength;
}

S32 iScreenToonFlatSmooth()
{
    return sToonFlatSmooth;
}

void iScreenSetToonFlatSmooth(S32 on)
{
    sToonFlatSmooth = on ? 1 : 0;
}

S32 iScreenToonRimBlend()
{
    return sToonRimBlend;
}

void iScreenSetToonRimBlend(S32 mode)
{
    sToonRimBlend = mode;
}

S32 iScreenToonSkyBright()
{
    return sToonSkyBright;
}

void iScreenSetToonSkyBright(S32 on)
{
    sToonSkyBright = on ? 1 : 0;
}

S32 iScreenToonTikiRim()
{
    return sToonTikiRim;
}

void iScreenSetToonTikiRim(S32 on)
{
    sToonTikiRim = on ? 1 : 0;
}

S32 iScreenToonFlatRim()
{
    return sToonFlatRim;
}

void iScreenSetToonFlatRim(S32 on)
{
    sToonFlatRim = on ? 1 : 0;
}

F32 iScreenToonOutlineBias()
{
    return sToonOutlineBias;
}

void iScreenSetToonOutlineBias(F32 widths)
{
    sToonOutlineBias = widths;
}

F32 iScreenToonTextBrightness()
{
    return sToonTextBrightness;
}

F32 iScreenToonTextSaturation()
{
    return sToonTextSaturation;
}

void iScreenSetToonText(F32 brightness, F32 saturation)
{
    sToonTextBrightness = brightness;
    sToonTextSaturation = saturation;
}

void iScreenSetToonLook(F32 wrap, F32 rim, F32 occlusion, F32 hardness, F32 outlineMin,
                        F32 outlineMax)
{
    sToonWrap = wrap;
    sToonRim = rim;
    sToonOcclusion = occlusion;
    sToonHardness = hardness;
    sToonOutlineMin = outlineMin;
    sToonOutlineMax = outlineMax;
}

// The field of view every camera in the game is built around. zCamera resets to
// it, xCameraCreate starts at it, and the cutscene and Cruise Bubble values are
// authored relative to it.
static const F32 kGameFOV = 75.0f;

static F32 sFOVOffset;

F32 iScreenFOVOffset()
{
    return sFOVOffset;
}

void iScreenSetFOV(F32 degrees)
{
    // The far end is the frustum going flat: vw.x is a tangent, and it runs
    // away at 180. The near end is arbitrary and generous.
    if (degrees < 20.0f || degrees > 150.0f)
    {
        printf("tssm: %g is not a field of view this can render at; staying at %g\n",
               (double)degrees, (double)(kGameFOV + sFOVOffset));
        fflush(stdout);
        return;
    }

    sFOVOffset = degrees - kGameFOV;
}

void iScreenSetSize(S32 width, S32 height)
{
    if (width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension)
    {
        printf("tssm: %dx%d is not a resolution this can render at; staying at %dx%d\n",
               (int)width, (int)height, (int)sWidth, (int)sHeight);
        fflush(stdout);
        return;
    }

    sWidth = width;
    sHeight = height;

    deriveUIBox();
}

namespace
{
    // How many screen-space scopes are open. The game draws from one thread
    // (iAsync.cpp draws the loading screen on it too), so a plain counter.
    S32 sScreenSpaceDepth = 0;
} // namespace

void iScreen2DScreenSpaceBegin()
{
    sScreenSpaceDepth++;
}

void iScreen2DScreenSpaceEnd()
{
    if (sScreenSpaceDepth > 0)
    {
        sScreenSpaceDepth--;
    }
}

S32 iScreen2DInScreenSpace()
{
    return sScreenSpaceDepth > 0;
}

S32 iScreenUIBoxed()
{
    return sUIFracX != 1.0f || sUIFracY != 1.0f;
}

iScreenAspect iScreenAspectFromName(const char* name)
{
    if (name != NULL)
    {
        if (strcmp(name, "4:3") == 0)
        {
            return iSCREENASPECT_4_3;
        }

        // The config reader hands values over as written; compare without case.
        char lower[8];
        size_t i = 0;
        for (; name[i] != '\0' && i < sizeof(lower) - 1; i++)
        {
            char c = name[i];
            lower[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }
        lower[i] = '\0';
        if (name[i] == '\0' && strcmp(lower, "custom") == 0)
        {
            return iSCREENASPECT_CUSTOM;
        }
    }

    return iSCREENASPECT_WIDE;
}

void iScreenSizeForAspect(iScreenAspect aspect, S32 width, S32 height, S32 shapeW, S32 shapeH,
                          S32* outWidth, S32* outHeight)
{
    if (height < 240)
    {
        height = 240;
    }
    if (height > 8640)
    {
        height = 8640;
    }

    if (aspect != iSCREENASPECT_CUSTOM)
    {
        F32 shape = kUIAspect;
        if (aspect == iSCREENASPECT_WIDE && shapeW > 0 && shapeH > 0)
        {
            shape = (F32)shapeW / (F32)shapeH;
        }

        // Even, so that half the width left over beside the UI box is a whole
        // number of pixels. 640 at 480 lines stays 640.
        width = 2 * (S32)floorf(0.5f * (F32)height * shape + 0.5f);
    }

    if (width < 320)
    {
        width = 320;
    }
    if (width > 15360)
    {
        width = 15360;
    }

    *outWidth = width;
    *outHeight = height;
}
