// tssm_hudtest: the arithmetic of the HUD at the screen's edges (iScreen.h's
// UI mode, anchor and safe margin) on made-up screen shapes: no game, no data,
// no window.
//
//     tssm_hudtest
//
// Which widgets move, and where the game draws them, is iHudAnchor.h's and is
// checked with captures; here only that an anchored edge of the 4:3 box lands
// on the real edge, less the margin, and that nothing moves when it must not;
// that a screen taller than 4:3 has the HUD at its edges with either setting;
// and the cut at the 4:3 area's top and bottom on such a screen (the band,
// iScreen2DClipY, and the triangles left, iClip2D.h).
// Exit code 0 when every check passed.

#include "iClip2D.h"
#include "iScreen.h"

#include <math.h>
#include <stdio.h>

static int sChecks;
static int sFailures;

static void Check(bool ok, const char* what)
{
    sChecks++;

    if (!ok)
    {
        sFailures++;
        printf("FAIL: %s\n", what);
    }
}

static bool Near(F32 a, F32 b)
{
    return fabsf(a - b) < 0.01f;
}

// Where a point of the 4:3 box (0 to 1) lands on the screen, in pixels, with
// the anchor (sideX, sideY) in force; as rw/im.cpp places a HUD widget's draw.
static F32 PixelX(F32 x, F32 sideX)
{
    iScreenSetAnchor(sideX, 0.0f);
    const F32 px = iScreenUIOriginXF() + iScreenAnchorX(x) * iScreenUIWidthF();
    iScreenClearAnchor();
    return px;
}

static F32 PixelY(F32 y, F32 sideY)
{
    iScreenSetAnchor(0.0f, sideY);
    const F32 py = iScreenUIOriginYF() + iScreenAnchorY(y) * iScreenUIHeightF();
    iScreenClearAnchor();
    return py;
}

static void TestNames()
{
    Check(iScreenUIModeFromName("native") == iSCREENUI_NATIVE, "\"native\" is NATIVE");
    Check(iScreenUIModeFromName("Native") == iSCREENUI_NATIVE, "case does not matter");
    Check(iScreenUIModeFromName("NATIVE") == iSCREENUI_NATIVE, "upper case is NATIVE");
    Check(iScreenUIModeFromName("pillarbox") == iSCREENUI_PILLARBOX, "\"pillarbox\" is PILLARBOX");
    Check(iScreenUIModeFromName("") == iSCREENUI_PILLARBOX, "empty is PILLARBOX");
    Check(iScreenUIModeFromName(NULL) == iSCREENUI_PILLARBOX, "no value is PILLARBOX");
    Check(iScreenUIModeFromName("nativeX") == iSCREENUI_PILLARBOX, "a longer word is PILLARBOX");
    Check(iScreenUIModeFromName("nat") == iSCREENUI_PILLARBOX, "a shorter word is PILLARBOX");
}

static void TestFourThree()
{
    iScreenSetSize(640, 480);
    iScreenSetUISafeMargin(0.0f);
    for (int m = 0; m < 2; m++)
    {
        iScreenSetUIMode(m == 0 ? iSCREENUI_PILLARBOX : iSCREENUI_NATIVE);
        Check(iScreenAnchorMarginXF() == 0.0f && iScreenAnchorMarginYF() == 0.0f,
              "4:3: nothing to reach in either mode");
        Check(PixelX(0.1f, -1.0f) == 64.0f && PixelX(0.9f, 1.0f) == 576.0f,
              "4:3: an anchored widget stays where it was drawn");
        Check(PixelY(0.9f, 1.0f) == 432.0f, "4:3: down stays too");
    }

    iScreenSetSize(1280, 960);
    iScreenSetUIMode(iSCREENUI_NATIVE);
    iScreenSetUISafeMargin(0.1f);
    Check(PixelX(0.0f, -1.0f) == 0.0f, "4:3 at 1280x960 with a margin: still nothing moves");
}

// A wide shape: the box's left and right edges go to the screen's, less the
// margin, and the middle and the other axis stay.
static void TestWide(S32 w, S32 h, const char* name)
{
    char what[160];
    iScreenSetSize(w, h);
    iScreenSetUISafeMargin(0.0f);

    iScreenSetUIMode(iSCREENUI_PILLARBOX);
    snprintf(what, sizeof(what), "%s pillarbox: the left edge stays at the box", name);
    Check(Near(PixelX(0.0f, -1.0f), iScreenUIOriginXF()), what);
    snprintf(what, sizeof(what), "%s pillarbox: no reach", name);
    Check(iScreenAnchorMarginXF() == 0.0f, what);

    iScreenSetUIMode(iSCREENUI_NATIVE);
    snprintf(what, sizeof(what), "%s native: the box's left edge lands on the screen's", name);
    Check(Near(PixelX(0.0f, -1.0f), 0.0f), what);
    snprintf(what, sizeof(what), "%s native: the box's right edge lands on the screen's", name);
    Check(Near(PixelX(1.0f, 1.0f), (F32)w), what);
    snprintf(what, sizeof(what), "%s native: a widget keeps its distance from its edge", name);
    Check(Near(PixelX(0.1f, -1.0f), 0.1f * iScreenUIWidthF()), what);
    snprintf(what, sizeof(what), "%s native: the middle stays", name);
    Check(Near(PixelX(0.5f, 0.0f), 0.5f * (F32)w), what);
    snprintf(what, sizeof(what), "%s native: nothing moves up or down", name);
    Check(iScreenAnchorMarginYF() == 0.0f && Near(PixelY(0.9f, 1.0f), 0.9f * (F32)h), what);
    snprintf(what, sizeof(what), "%s native: half way is half the reach", name);
    Check(Near(PixelX(0.0f, -0.5f), 0.5f * iScreenUIOriginXF()), what);

    // 5% of the width at each side, when the screen reaches that far.
    iScreenSetUISafeMargin(0.05f);
    const F32 stop = 0.05f * (F32)w;
    const F32 expect = stop < iScreenUIOriginXF() ? stop : iScreenUIOriginXF();
    snprintf(what, sizeof(what), "%s native, 5%% margin: the left edge stops short", name);
    Check(Near(PixelX(0.0f, -1.0f), expect), what);
    snprintf(what, sizeof(what), "%s native, 5%% margin: the right edge stops short", name);
    Check(Near(PixelX(1.0f, 1.0f), (F32)w - expect), what);

    // A margin wider than the reach never pulls the HUD inside the box.
    iScreenSetUISafeMargin(0.25f);
    snprintf(what, sizeof(what), "%s native, 25%% margin: never inside the box", name);
    Check(PixelX(0.0f, -1.0f) <= iScreenUIOriginXF() + 0.01f && iScreenAnchorMarginXF() >= 0.0f,
          what);
    iScreenSetUISafeMargin(0.0f);
}

// A tall shape: the same, top and bottom, and with either setting: on a screen
// taller than 4:3 the HUD is at the edges whatever video.ui says.
static void TestTall(S32 w, S32 h, const char* name)
{
    char what[160];
    iScreenSetSize(w, h);
    snprintf(what, sizeof(what), "%s is taller than 4:3", name);
    Check(iScreenUITall() != 0, what);

    for (int m = 0; m < 2; m++)
    {
        const iScreenUIMode mode = m == 0 ? iSCREENUI_PILLARBOX : iSCREENUI_NATIVE;
        const char* setting = m == 0 ? "pillarbox" : "native";
        iScreenSetUIMode(mode);
        iScreenSetUISafeMargin(0.0f);

        snprintf(what, sizeof(what), "%s %s: the setting reads back as written", name, setting);
        Check(iScreenGetUIMode() == mode, what);
        snprintf(what, sizeof(what), "%s %s: the HUD is at the edges", name, setting);
        Check(iScreenUIModeInForce() == iSCREENUI_NATIVE, what);
        snprintf(what, sizeof(what), "%s %s: the box's top lands on the screen's", name, setting);
        Check(Near(PixelY(0.0f, -1.0f), 0.0f), what);
        snprintf(what, sizeof(what), "%s %s: the box's bottom lands on the screen's", name,
                 setting);
        Check(Near(PixelY(1.0f, 1.0f), (F32)h), what);
        snprintf(what, sizeof(what), "%s %s: a widget keeps its distance from its edge", name,
                 setting);
        Check(Near(PixelY(0.1f, -1.0f), 0.1f * iScreenUIHeightF()), what);
        snprintf(what, sizeof(what), "%s %s: the middle stays", name, setting);
        Check(Near(PixelY(0.5f, 0.0f), 0.5f * (F32)h), what);
        snprintf(what, sizeof(what), "%s %s: nothing moves across", name, setting);
        Check(iScreenAnchorMarginXF() == 0.0f && Near(PixelX(0.9f, 1.0f), 0.9f * (F32)w), what);
        snprintf(what, sizeof(what), "%s %s: the boss meter has room to go down", name, setting);
        Check(iScreenAnchorMarginYF() > 0.0f, what);

        iScreenSetUISafeMargin(0.02f);
        snprintf(what, sizeof(what), "%s %s, 2%% margin: of the height", name, setting);
        Check(Near(PixelY(1.0f, 1.0f), (F32)h - 0.02f * (F32)h) || iScreenAnchorMarginYF() == 0.0f,
              what);
        snprintf(what, sizeof(what), "%s %s, 2%% margin: at the top too", name, setting);
        Check(Near(PixelY(0.0f, -1.0f), 0.02f * (F32)h) || iScreenAnchorMarginYF() == 0.0f, what);
        iScreenSetUISafeMargin(0.25f);
        snprintf(what, sizeof(what), "%s %s, 25%% margin: never inside the box", name, setting);
        Check(PixelY(0.0f, -1.0f) <= iScreenUIOriginYF() + 0.01f && iScreenAnchorMarginYF() >= 0.0f,
              what);
        iScreenSetUISafeMargin(0.0f);
    }
}

// Where the setting still chooses: every shape that is not taller than 4:3.
static void TestSettingChooses(S32 w, S32 h, const char* name)
{
    char what[160];
    iScreenSetSize(w, h);
    snprintf(what, sizeof(what), "%s is not taller than 4:3", name);
    Check(iScreenUITall() == 0, what);
    for (int m = 0; m < 2; m++)
    {
        const iScreenUIMode mode = m == 0 ? iSCREENUI_PILLARBOX : iSCREENUI_NATIVE;
        iScreenSetUIMode(mode);
        snprintf(what, sizeof(what), "%s: the HUD goes where the setting says (%s)", name,
                 m == 0 ? "pillarbox" : "native");
        Check(iScreenUIModeInForce() == mode, what);
    }
    iScreenSetUIMode(iSCREENUI_PILLARBOX);
    snprintf(what, sizeof(what), "%s pillarbox: the left edge stays at the box", name);
    Check(Near(PixelX(0.0f, -1.0f), iScreenUIOriginXF()), what);
}

// The band a menu's or a HUD group's 2D is cut to (iScreen2DClipY).
static void TestCutBand()
{
    char what[160];
    F32 top = -1.0f;
    F32 bottom = -1.0f;

    // Never on a shape that is not taller than 4:3, with either setting.
    static const S32 kNotTall[][2] = {
        { 640, 480 }, { 1440, 1080 }, { 1920, 1080 }, { 2560, 1080 }
    };
    for (const auto& size : kNotTall)
    {
        for (int m = 0; m < 2; m++)
        {
            const char* setting = m == 0 ? "pillarbox" : "native";
            iScreenSetSize(size[0], size[1]);
            iScreenSetUIMode(m == 0 ? iSCREENUI_PILLARBOX : iSCREENUI_NATIVE);
            iScreen2DBoxClipBegin();
            snprintf(what, sizeof(what), "%dx%d %s: the menus are not cut", (int)size[0],
                     (int)size[1], setting);
            Check(!iScreen2DClipY(&top, &bottom), what);
            iScreen2DBoxClipEnd();
            iScreenSetAnchor(-1.0f, -1.0f);
            snprintf(what, sizeof(what), "%dx%d %s: the HUD is not cut", (int)size[0], (int)size[1],
                     setting);
            Check(!iScreen2DClipY(&top, &bottom), what);
            iScreenClearAnchor();
        }
    }

    // 1080x1920: the box is 1080x810, 555 px from the top.
    iScreenSetSize(1080, 1920);
    iScreenSetUISafeMargin(0.0f);
    for (int m = 0; m < 2; m++)
    {
        const char* setting = m == 0 ? "pillarbox" : "native";
        iScreenSetUIMode(m == 0 ? iSCREENUI_PILLARBOX : iSCREENUI_NATIVE);

        snprintf(what, sizeof(what), "9:16 %s: nothing is cut outside the scopes", setting);
        Check(!iScreen2DClipY(&top, &bottom), what);

        iScreen2DBoxClipBegin();
        iScreen2DBoxClipBegin();
        iScreen2DBoxClipEnd();
        snprintf(what, sizeof(what), "9:16 %s: the menus are cut to the box (scopes nest)",
                 setting);
        Check(iScreen2DClipY(&top, &bottom) && Near(top, 555.0f) && Near(bottom, 1365.0f), what);
        iScreen2DBoxClipEnd();
        snprintf(what, sizeof(what), "9:16 %s: the scope closes", setting);
        Check(!iScreen2DClipY(&top, &bottom), what);

        iScreenSetAnchor(0.0f, -1.0f);
        snprintf(what, sizeof(what), "9:16 %s: a top group is cut to its 4:3 screen at the top",
                 setting);
        Check(iScreen2DClipY(&top, &bottom) && Near(top, 0.0f) && Near(bottom, 810.0f), what);
        iScreenSetAnchor(1.0f, 1.0f);
        snprintf(what, sizeof(what), "9:16 %s: a bottom group at the bottom", setting);
        Check(iScreen2DClipY(&top, &bottom) && Near(top, 1110.0f) && Near(bottom, 1920.0f), what);
        iScreenSetAnchor(0.0f, 0.0f);
        snprintf(what, sizeof(what), "9:16 %s: a widget in the middle, or paused, to the box",
                 setting);
        Check(iScreen2DClipY(&top, &bottom) && Near(top, 555.0f) && Near(bottom, 1365.0f), what);
        iScreenClearAnchor();
        snprintf(what, sizeof(what), "9:16 %s: not once the anchor is cleared", setting);
        Check(!iScreen2DClipY(&top, &bottom), what);

        // With a 5% margin the top group stops 96 px down, and is cut there.
        iScreenSetUISafeMargin(0.05f);
        iScreenSetAnchor(0.0f, -1.0f);
        snprintf(what, sizeof(what), "9:16 %s, 5%% margin: a top group is cut at the margin",
                 setting);
        Check(iScreen2DClipY(&top, &bottom) && Near(top, 96.0f) && Near(bottom, 906.0f), what);
        iScreenClearAnchor();
        iScreenSetUISafeMargin(0.0f);
    }

    // 720x1080: the box is 720x540, 270 px from the top.
    iScreenSetSize(720, 1080);
    iScreenSetUIMode(iSCREENUI_PILLARBOX);
    iScreen2DBoxClipBegin();
    Check(iScreen2DClipY(&top, &bottom) && Near(top, 270.0f) && Near(bottom, 810.0f),
          "2:3 pillarbox: the menus are cut to the box");
    iScreen2DBoxClipEnd();
    iScreenSetAnchor(-1.0f, -1.0f);
    Check(iScreen2DClipY(&top, &bottom) && Near(top, 0.0f) && Near(bottom, 540.0f),
          "2:3 pillarbox: the top left group is cut at the screen's top");
    iScreenClearAnchor();
}

// A vertex with RwIm2DVertex's fields, for iClip2D.h.
struct TestVertex
{
    F32 x, y, z, w;
    struct
    {
        U8 blue, green, red, alpha;
    } emissiveColor;
    F32 u, v;
};

static TestVertex Vertex(F32 x, F32 y, F32 u, F32 v, U8 alpha, F32 w = 1.0f)
{
    TestVertex t;
    t.x = x;
    t.y = y;
    t.z = 0.5f;
    t.w = w;
    t.emissiveColor.red = 200;
    t.emissiveColor.green = 100;
    t.emissiveColor.blue = 50;
    t.emissiveColor.alpha = alpha;
    t.u = u;
    t.v = v;
    return t;
}

// Twice the signed area: its sign is the winding.
static F32 Winding(const TestVertex& a, const TestVertex& b, const TestVertex& c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static void TestCutTriangles()
{
    TestVertex out[9];

    // Wholly inside: as given.
    const TestVertex a = Vertex(10, 10, 0, 0, 255);
    const TestVertex b = Vertex(100, 10, 1, 0, 255);
    const TestVertex c = Vertex(10, 90, 0, 1, 255);
    S32 n = iClip2DTriangle(a, b, c, 0.0f, 100.0f, out);
    Check(n == 3 && out[0].x == 10 && out[1].x == 100 && out[2].y == 90,
          "a triangle inside the band is kept whole");

    // Wholly above or below: nothing.
    Check(iClip2DTriangle(a, b, c, 200.0f, 300.0f, out) == 0,
          "a triangle above the band is dropped");
    Check(iClip2DTriangle(a, b, c, -300.0f, 0.0f, out) == 0,
          "a triangle below the band is dropped");

    // A panel half above the top: y -100 to 100, cut at 0. The cut edge is
    // half way down the texture and the alpha.
    const TestVertex p0 = Vertex(0, -100, 0, 0, 0);
    const TestVertex p1 = Vertex(200, -100, 1, 0, 0);
    const TestVertex p2 = Vertex(0, 100, 0, 1, 200);
    n = iClip2DTriangle(p0, p1, p2, 0.0f, 1000.0f, out);
    bool inBand = n > 0;
    bool halfway = false;
    for (S32 i = 0; i < n; i++)
    {
        inBand = inBand && out[i].y >= 0.0f && out[i].y <= 1000.0f;
        if (out[i].x == 0.0f && Near(out[i].y, 0.0f))
        {
            halfway = Near(out[i].v, 0.5f) && out[i].emissiveColor.alpha == 100 &&
                      out[i].emissiveColor.red == 200 && Near(out[i].z, 0.5f);
        }
    }
    Check(n == 3, "a corner cut off leaves one triangle");
    Check(inBand, "what is left is inside the band");
    Check(halfway, "the cut edge has the texture and colour of that place");
    Check(n == 3 && Winding(out[0], out[1], out[2]) * Winding(p0, p1, p2) > 0.0f,
          "the winding is kept");

    // The other half of the quad: two triangles left.
    const TestVertex q0 = Vertex(200, -100, 1, 0, 0);
    const TestVertex q1 = Vertex(200, 100, 1, 1, 200);
    const TestVertex q2 = Vertex(0, 100, 0, 1, 200);
    n = iClip2DTriangle(q0, q1, q2, 0.0f, 1000.0f, out);
    Check(n == 6, "a corner kept leaves two triangles");
    bool wound = n == 6;
    for (S32 i = 0; i + 2 < n; i += 3)
    {
        wound = wound && Winding(out[i], out[i + 1], out[i + 2]) * Winding(q0, q1, q2) > 0.0f;
    }
    Check(wound, "both keep the winding");

    // Cut at both lines: three triangles, all in the band.
    const TestVertex t0 = Vertex(0, -50, 0, 0, 255);
    const TestVertex t1 = Vertex(100, 50, 1, 0, 255);
    const TestVertex t2 = Vertex(0, 150, 0, 1, 255);
    n = iClip2DTriangle(t0, t1, t2, 0.0f, 100.0f, out);
    inBand = n == 9;
    for (S32 i = 0; i < n; i++)
    {
        inBand = inBand && out[i].y >= -0.001f && out[i].y <= 100.001f;
    }
    Check(inBand, "a triangle across both lines leaves three, all in the band");

    // A different camera z at each end: u goes over 1/w, as the rasteriser
    // takes it. Half way down the screen between w = 1 and w = 3, u is 0.25
    // and w is 1.5.
    const TestVertex w0 = Vertex(0, -100, 0, 0, 255, 1.0f);
    const TestVertex w1 = Vertex(100, 100, 1, 0, 255, 3.0f);
    const TestVertex w2 = Vertex(0, 100, 0, 0, 255, 3.0f);
    n = iClip2DTriangle(w0, w1, w2, 0.0f, 1000.0f, out);
    bool persp = false;
    for (S32 i = 0; i < n; i++)
    {
        if (Near(out[i].x, 50.0f) && Near(out[i].y, 0.0f))
        {
            persp = Near(out[i].u, 0.25f) && Near(out[i].w, 1.5f);
        }
    }
    Check(persp, "u and w at the cut go as the rasteriser takes them");
}

static void TestModeChange()
{
    iScreenSetSize(1920, 1080);
    iScreenSetUIMode(iSCREENUI_NATIVE);
    iScreenSetAnchor(-1.0f, 0.0f);
    iScreenSetUIMode(iSCREENUI_PILLARBOX);
    Check(iScreenAnchorX(0.25f) == 0.25f, "a mode change clears the anchor in force");

    iScreenSetUISafeMargin(-1.0f);
    Check(iScreenGetUISafeMargin() == 0.0f, "a negative margin is none");
    iScreenSetUISafeMargin(3.0f);
    Check(iScreenGetUISafeMargin() == 0.25f, "a margin above a quarter is a quarter");
    iScreenSetUISafeMargin(0.0f);
}

int main()
{
    TestNames();
    TestFourThree();
    TestWide(1280, 720, "16:9");
    TestWide(1920, 1200, "16:10");
    TestWide(2560, 1080, "21:9");
    TestWide(3840, 1080, "32:9");
    TestWide(854, 480, "16:9 at 480 lines");
    TestTall(720, 1080, "2:3");
    TestTall(1080, 1920, "9:16");
    TestTall(1080, 2400, "9:20 (a phone held upright)");
    TestTall(641, 481, "a pixel taller than 4:3");
    TestSettingChooses(640, 480, "4:3");
    TestSettingChooses(1440, 1080, "4:3 at 1440x1080");
    TestSettingChooses(1920, 1080, "16:9");
    TestSettingChooses(3840, 1080, "32:9");
    TestCutBand();
    TestCutTriangles();
    TestModeChange();

    printf("tssm_hudtest: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
