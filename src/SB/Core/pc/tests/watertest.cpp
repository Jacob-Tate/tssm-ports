// tssm_watertest: the Xbox water's arithmetic (iXboxWaterGrid.h) on its own,
// with made-up meshes: no game, no data, no window.
//
//     tssm_watertest
//
// The level numbers (SC02's 8 x 7 and 10 x 9 tiles, B301's 12 x 12) come from
// the shipped meshes and are checked in the game's log; here the same rules
// are checked on points that only share their bounds. Exit code 0 when every
// check passed.

#include "iXboxWaterGrid.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <vector>

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

static const iXboxWaterPreset kDefault = { 40000.0f, 1.0f, 16, 16 };
static const iXboxWaterPreset kB301 = { 128000.0f, 2.0f, 32, 32 };

static void TestCorners()
{
    // -350 % 16 is -14 in C, so -336, then one tile down for being negative.
    F32 a[] = { -350.0f, 26.72f, -174.7f, -241.8f, 26.72f, -49.3f };
    iXboxWaterLayout l;
    Check(iXboxWaterLayoutFor(a, 2, kDefault, &l), "SC02-sized bounds give a grid");
    Check(l.x0 == -352.0f && l.z0 == -176.0f, "the corner snaps down one more tile when negative");
    Check(l.rows == 8 && l.cols == 7, "127 x 111 cells round up to 8 x 7 tiles");
    Check(l.y == 26.72f, "the height is the first point's");
    Check(l.tileW == 16.0f && l.tileD == 16.0f, "16-unit tiles");

    // Already on a tile edge and negative: still one more tile down.
    F32 b[] = { -352.0f, 0.0f, -16.0f, -340.0f, 0.0f, -1.0f };
    Check(iXboxWaterLayoutFor(b, 2, kDefault, &l), "edge bounds give a grid");
    Check(l.x0 == -368.0f && l.z0 == -32.0f, "a negative corner on an edge still moves down");

    // Positive: snapped down only.
    F32 c[] = { 5.5f, 1.0f, 17.0f, 30.0f, 1.0f, 40.0f };
    Check(iXboxWaterLayoutFor(c, 2, kDefault, &l), "positive bounds give a grid");
    Check(l.x0 == 0.0f && l.z0 == 16.0f, "a positive corner snaps down to the tile");
    Check(l.rows == 2 && l.cols == 2, "positive bounds: 2 x 2 tiles");

    // B301-sized, 64-unit tiles.
    F32 d[] = { -380.0f, 1.7f, -380.0f, 380.0f, 1.7f, 380.0f };
    Check(iXboxWaterLayoutFor(d, 2, kB301, &l), "B301-sized bounds give a grid");
    Check(l.x0 == -384.0f && l.z0 == -384.0f, "B301 corner");
    Check(l.rows == 12 && l.cols == 12, "B301: 12 x 12 tiles of 64");
    Check(l.tileW == 64.0f, "64-unit tiles");

    // The ratio rule: x cells follow z cells scaled by width / depth.
    F32 e[] = { 0.0f, 0.0f, 0.0f, 40.0f, 0.0f, 100.0f };
    Check(iXboxWaterLayoutFor(e, 2, kDefault, &l), "a long thin mesh gives a grid");
    Check(l.rows == 7 && l.cols == 3, "100 x 40 units: 7 x 3 tiles");
}

static void TestTiles()
{
    F32 a[] = { -350.0f, 26.72f, -174.7f, -241.8f, 26.72f, -49.3f };
    iXboxWaterLayout l;
    iXboxWaterLayoutFor(a, 2, kDefault, &l);

    iXboxWaterTile t;
    iXboxWaterTileAt(l, 2, 3, &t);
    Check(t.x0 == -352.0f + 3 * 16.0f && t.z0 == -176.0f + 2 * 16.0f, "tile (r, c) is at (c, r)");
    Check(t.cx == t.x0 + 8.0f && t.cz == t.z0 + 8.0f && t.cy == 26.72f, "the sphere's centre");
    Check(fabsf(t.radius - sqrtf(128.0f)) < 1.0e-5f, "the sphere's radius is the half diagonal");
    Check(t.flags == 0, "a new tile has no flags");

    F32 inside[] = { t.x0 + 1.0f, 0.0f, t.z0 + 1.0f };
    F32 edge[] = { t.x0, 0.0f, t.z0 + 1.0f };
    F32 far[] = { t.x0 + 16.0f, 0.0f, t.z0 + 1.0f };
    Check(iXboxWaterTileHasPoint(t, inside, 1), "a point inside counts");
    Check(!iXboxWaterTileHasPoint(t, edge, 1), "a point on the low edge does not");
    Check(!iXboxWaterTileHasPoint(t, far, 1), "a point on the high edge does not");
}

static void TestBadInput()
{
    iXboxWaterLayout l;
    memset(&l, 0x55, sizeof(l));
    F32 nan[] = { 0.0f, 0.0f, NAN, 10.0f, 0.0f, 10.0f };
    F32 flat[] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 10.0f };
    F32 huge[] = { -1.0e5f, 0.0f, -1.0e5f, 1.0e5f, 0.0f, 1.0e5f };
    F32 big[] = { -1.0e7f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f };
    F32 one[] = { 1.0f, 2.0f, 3.0f };
    iXboxWaterPreset zero = { 40000.0f, 0.0f, 16, 16 };

    Check(!iXboxWaterLayoutFor(NULL, 2, kDefault, &l), "no points");
    Check(!iXboxWaterLayoutFor(one, 0, kDefault, &l), "zero points");
    Check(!iXboxWaterLayoutFor(nan, 2, kDefault, &l), "a NaN coordinate");
    Check(!iXboxWaterLayoutFor(flat, 2, kDefault, &l), "no extent in x");
    Check(!iXboxWaterLayoutFor(huge, 2, kDefault, &l), "more than the tile limit");
    Check(!iXboxWaterLayoutFor(big, 2, kDefault, &l), "a coordinate out of range");
    Check(!iXboxWaterLayoutFor(one, 1, zero, &l), "a zero cell");
    Check(iXboxWaterLayoutFor(one, 1, kDefault, &l) && l.rows == 1 && l.cols == 1,
          "a single point off the tile edges still gets its tile");
}

static void TestStrips()
{
    Check(iXboxWaterStripIndices(17, NULL) == 574, "17 a side: 574 indices");
    Check(iXboxWaterStripIndices(33, NULL) == 2174, "33 a side: 2174 indices");
    Check(iXboxWaterStripIndices(1, NULL) == 0 && iXboxWaterStripIndices(256, NULL) == 0,
          "sizes out of range: none");

    std::vector<U16> idx(574);
    U32 n = iXboxWaterStripIndices(17, &idx[0]);
    Check(n == 574, "17: as many written as counted");
    Check(idx[0] == 0 && idx[1] == 17 && idx[2] == 1 && idx[3] == 18, "a row starts 0, 17, 1, 18");
    Check(idx[32] == 16 && idx[33] == 33, "the first row ends 16, 33");
    Check(idx[34] == 33 && idx[35] == 17, "the join repeats 33, then 17");
    Check(idx[36] == 17 && idx[37] == 34, "the second row starts 17, 34");
    Check(idx[572] == 271 && idx[573] == 288, "the last pair is 271, 288");

    U32 maxIndex = 0;
    for (U32 i = 0; i < n; i++)
    {
        maxIndex = idx[i] > maxIndex ? idx[i] : maxIndex;
    }
    Check(maxIndex == 288, "every index is a vertex of the 17 x 17 patch");

    std::vector<U16> big(2174);
    Check(iXboxWaterStripIndices(33, &big[0]) == 2174 && big[2173] == 1088,
          "33: written, the last is 1088");
}

static void TestClock()
{
    F32 t = 0.0f;
    Check(iXboxWaterClockStep(&t, 2.4f, 0.0f) == 0, "t 0 is frame 0");

    t = 0.0f;
    S32 f = iXboxWaterClockStep(&t, 2.4f, 0.25f);
    Check(fabsf(t - 0.6f) < 1.0e-6f && f == 5, "0.6 is frame 5 (0.6 / (2 / 17))");

    t = 1.99f;
    f = iXboxWaterClockStep(&t, 1.0f, 0.0f);
    Check(f == 16, "just under 2 is frame 16");

    t = 1.9f;
    f = iXboxWaterClockStep(&t, 2.4f, 0.1f);
    Check(fabsf(t - 0.14f) < 1.0e-5f && f == 1, "2.14 wraps to 0.14, frame 1");

    t = 1.9f;
    f = iXboxWaterClockStep(&t, 8.0f, 0.2f);
    Check(fabsf(t - 0.5f) < 1.0e-5f && f == 4, "3.5 loses its whole part, as the Xbox computes it");

    t = NAN;
    f = iXboxWaterClockStep(&t, 2.4f, 0.016f);
    Check(t == 0.0f && f == 0, "a NaN clock starts again");

    t = 0.0f;
    f = iXboxWaterClockStep(&t, 2.4f, -1.0f);
    Check(t == 0.0f && f == 0, "a negative step starts again");

    // 17 frames take 2 / speed seconds: at 60 fps and speed 2.4, 0.04 a step.
    t = 0.0f;
    S32 changes = 0;
    S32 last = 0;
    for (S32 i = 0; i < 49; i++)
    {
        f = iXboxWaterClockStep(&t, 2.4f, 1.0f / 60.0f);
        changes += f != last;
        last = f;
    }
    Check(changes == 16 && last == 16, "49 steps of 0.04: through frames 0 to 16 once");
}

static void TestBumps()
{
    // A made-up frame: texel i has du = i - 128 (mod 256) and dv = 127 - i,
    // as signed bytes, and the tail past 0x8000 is filled with 0x55.
    std::vector<U8> src(43008, 0x55);
    for (U32 i = 0; i < 128 * 128; i++)
    {
        src[2 * i + 0] = (U8)(i - 128);
        src[2 * i + 1] = (U8)(127 - i);
    }

    std::vector<U8> out(128 * 128 * 4, 0xAA);
    Check(iXboxWaterBumpToRGBA(src.data(), (U32)src.size(), out.data()), "a whole frame converts");

    bool biased = true;
    for (U32 i = 0; i < 128 * 128; i++)
    {
        S32 du = (S8)src[2 * i + 0];
        S32 dv = (S8)src[2 * i + 1];
        biased = biased && out[4 * i + 0] == du + 128 && out[4 * i + 1] == dv + 128 &&
                 out[4 * i + 2] == 0 && out[4 * i + 3] == 0xFF;
    }
    Check(biased, "R = du + 128, G = dv + 128, B = 0, A = 255 for every texel");
    Check(out[0] == 0 && out[1] == 255, "du -128 is 0, dv 127 is 255");

    // The data's quirk (dv = -1 where du < 0) is copied, not mended.
    std::vector<U8> quirk(0x8000, 0);
    quirk[0] = (U8)-5;
    quirk[1] = 0xFF;
    Check(iXboxWaterBumpToRGBA(quirk.data(), 0x8000, out.data()), "exactly 0x8000 bytes is enough");
    Check(out[0] == 123 && out[1] == 127, "du -5, dv -1 stay as they are");

    std::fill(out.begin(), out.end(), 0xAA);
    Check(!iXboxWaterBumpToRGBA(src.data(), 0x7FFF, out.data()), "under 0x8000 bytes is refused");
    Check(out[0] == 0xAA, "a refused frame writes nothing");
    Check(!iXboxWaterBumpToRGBA(NULL, 0x8000, out.data()), "no data is refused");
}

static void TestLod()
{
    F32 s1 = 0.0f;
    F32 s2 = 0.0f;

    iXboxWaterLodScales(5.0f, 10.0f, 20.0f, &s1, &s2);
    Check(s1 == 1.0f && s2 == 4.0f, "at 5 units: refraction 1 (nearer than 10), reflection 20/5");

    iXboxWaterLodScales(10.0f, 10.0f, 20.0f, &s1, &s2);
    Check(s1 == 1.0f && s2 == 2.0f, "at 10 units: 10/10 and 20/10");

    iXboxWaterLodScales(100.0f, 10.0f, 30.0f, &s1, &s2);
    Check(fabsf(s1 - 0.1f) < 1.0e-7f && fabsf(s2 - 0.3f) < 1.0e-7f,
          "at 100 units, B301: 10/100 and 30/100");

    iXboxWaterLodScales(0.0f, 10.0f, 20.0f, &s1, &s2);
    Check(s1 == 1.0f && s2 == 20000.0f, "at 0 units the distance is taken as 0.001");

    iXboxWaterLodScales(NAN, 10.0f, 20.0f, &s1, &s2);
    Check(s1 == 1.0f && s2 == 20000.0f, "a distance that is not a number is taken as 0.001");

    Check(iXBOXWATER_BUMP_REFRACT[0] == -0.02f && iXBOXWATER_BUMP_REFRACT[1] == 0.05f &&
              iXBOXWATER_BUMP_REFRACT[2] == 0.05f && iXBOXWATER_BUMP_REFRACT[3] == -0.02f,
          "the refraction matrix is 0x471DB0's");
    Check(iXBOXWATER_BUMP_REFLECT[0] == -0.02f && iXBOXWATER_BUMP_REFLECT[1] == 0.25f &&
              iXBOXWATER_BUMP_REFLECT[2] == 0.25f && iXBOXWATER_BUMP_REFLECT[3] == -0.02f,
          "the reflection matrix is 0x471DC0's");
}

static void TestTargetSize()
{
    S32 w = 0;
    S32 h = 0;

    iXboxWaterTargetSize(640, 480, 2048, &w, &h);
    Check(w == 512 && h == 512, "640 x 480 is the Xbox's 512 x 512");

    iXboxWaterTargetSize(1920, 1080, 2048, &w, &h);
    Check(w == 1536 && h == 1152, "1920 x 1080 is 1536 x 1152");

    iXboxWaterTargetSize(2340, 1080, 1024, &w, &h);
    Check(w == 1024 && h == 1024, "a phone's 2340 x 1080 is capped at 1024 on each axis");

    iXboxWaterTargetSize(3840, 2160, 2048, &w, &h);
    Check(w == 2048 && h == 2048, "4K is capped at 2048");

    iXboxWaterTargetSize(1280, 720, 2048, &w, &h);
    Check(w == 1024 && h == 768, "1280 x 720 is 1024 x 768");

    iXboxWaterTargetSize(1000, 700, 2050, &w, &h);
    Check(w % 16 == 0 && h % 16 == 0 && w == 800 && h == 752, "rounded to the nearest 16");

    iXboxWaterTargetSize(0, -5, 2048, &w, &h);
    Check(w == 16 && h == 16, "no size is 16 x 16");

    iXboxWaterTargetSize(0x7FFFFFFF, 0x7FFFFFFF, 1000, &w, &h);
    Check(w == 992 && h == 992, "a huge size is capped, the cap taken down to a multiple of 16");
}

int main()
{
    TestCorners();
    TestTiles();
    TestBadInput();
    TestStrips();
    TestClock();
    TestBumps();
    TestLod();
    TestTargetSize();

    printf("tssm_watertest: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
