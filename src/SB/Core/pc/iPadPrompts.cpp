// Keeping the button prompts true. What is done and why is in iPadPrompts.h.

#include "iPadPrompts.h"

#include "iConfig.h"
#include "iFile.h"
#include "iHost.h"
#include "iPadBind.h"
#include "iPadHost.h"
#include "iPadKeyboard.h"
#include "iPadSettings.h"
#include "iPadTokens.h"
#include "xPad.h"
#include "xString.h"
#include "xstransvc.h"

#include <rwcore.h>

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// 1. The move layout

const iPadPromptMove kPadPromptMoves[] = {
    // TRIANGLE reads under the GameCube code: X on the Xbox disc.
    { "button_spinattack_text", '4' },
    { "button_patspinattack_text", '4' },
    { "button_patslamattack_text", '4' },
    { "button_car_reverse_text", '4' },
    // O reads: B on the Xbox disc.
    { "button_bowlattack_text", '3' },
    { "button_cartwheel_text", '3' },
};

const S32 kPadPromptMoveCount = (S32)(sizeof(kPadPromptMoves) / sizeof(kPadPromptMoves[0]));

S32 iPadPromptsPatchMove(const iPadPromptMove& move, char* text, U32 size, iPadLayoutKind layout)
{
    static const char kPicture[] = "button_picture_0";

    if (text == NULL || size == 0)
    {
        return FALSE;
    }

    // Find the picture's digit inside the bound, without trusting a terminator.
    const U32 n = (U32)(sizeof(kPicture) - 1);
    for (U32 i = 0; i + n < size && text[i] != '\0'; i++)
    {
        if (memcmp(text + i, kPicture, n) != 0)
        {
            continue;
        }

        char* digit = text + i + n;
        if (*digit != '3' && *digit != '4')
        {
            return FALSE;
        }

        // The gamecube layout reads the other bit, whose picture is the other
        // of the two buttons.
        char want = move.xbox;
        if (layout == IPAD_LAYOUT_GAMECUBE)
        {
            want = (move.xbox == '3') ? '4' : '3';
        }

        if (*digit == want)
        {
            return FALSE;
        }
        *digit = want;
        return TRUE;
    }

    return FALSE;
}

S32 iPadPromptsPatchVerb(U32* len, char* text, U32 capacity, bool press)
{
    if (len == NULL || text == NULL || capacity == 0)
    {
        return FALSE;
    }

    const bool upper = text[0] == 'P';
    const char* want = press ? (upper ? "Press" : "press") : (upper ? "Pull" : "pull");
    const char* other = press ? (upper ? "Pull" : "pull") : (upper ? "Press" : "press");

    if (strcmp(text, want) == 0)
    {
        return FALSE;
    }
    if (strcmp(text, other) != 0)
    {
        // Not the word this was written for: leave it.
        return FALSE;
    }

    const U32 n = (U32)strlen(want);
    if (n + 1 > capacity)
    {
        return FALSE;
    }

    memcpy(text, want, n + 1);
    *len = n;
    return TRUE;
}

namespace
{
    // Whose pictures the loaded font.HIP has: the Xbox's unless its package is
    // the GameCube release's (iFilePackagePlatformOf, asked when a new copy of
    // the pictures appears; ApplyGlyphs).
    bool sGameCubeFont;

    iPadLayoutKind sAppliedLayout = IPAD_LAYOUT_XBOX;
    void* sMoveAsset[sizeof(kPadPromptMoves) / sizeof(kPadPromptMoves[0])];

    // "Pull RT to talk": the Xbox's word for a trigger. Pressed with a key or
    // a GameCube pad's R (whose own disc says "Press"), it should say press.
    void ApplyVerb(const char* set)
    {
        static const char* const kNames[] = { "push_trigger_text", "push_trigger_text_lower" };

        // With the disc's own pictures (set NULL), the disc's own word: "Press"
        // on the GameCube's, whose font this follows.
        const bool press = set != NULL ?
                               (strcmp(set, "keyboard") == 0 || strcmp(set, "gamecube") == 0) :
                               sGameCubeFont;

        for (U32 i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++)
        {
            U32 size = 0;
            void* asset = xSTFindAsset(xStrHash(kNames[i]), &size);
            if (asset == NULL || size <= sizeof(U32))
            {
                continue;
            }
            iPadPromptsPatchVerb((U32*)asset, (char*)asset + sizeof(U32), size - (U32)sizeof(U32),
                                 press);
        }
    }

    // The TEXT assets that are loaded right now, rewritten for the layout. An
    // xTextAsset is a U32 length and then the string.
    void ApplyLayout(bool force)
    {
        const iPadLayoutKind layout = iPadLayoutActive();

        for (S32 i = 0; i < kPadPromptMoveCount; i++)
        {
            U32 size = 0;
            void* asset = xSTFindAsset(xStrHash(kPadPromptMoves[i].name), &size);
            if (asset == NULL || size <= sizeof(U32))
            {
                sMoveAsset[i] = NULL;
                continue;
            }

            if (!force && asset == sMoveAsset[i] && layout == sAppliedLayout)
            {
                continue;
            }

            char* text = (char*)asset + sizeof(U32);
            iPadPromptsPatchMove(kPadPromptMoves[i], text, size - (U32)sizeof(U32), layout);
            sMoveAsset[i] = asset;
        }

        sAppliedLayout = layout;
    }
} // namespace

// ---------------------------------------------------------------------------
// 2. The glyphs

namespace
{
    // The prompt pictures in font.HIP, what each shows on the Xbox disc, and
    // the row of kPadBindButtons whose binding decides what to draw instead.
    struct Slot
    {
        const char* texture;
        const char* row;
        U32 bits;
        // The pad input the disc's picture shows, or a stick (-1 left,
        // -2 right).
        S32 input;
    };

    const Slot kSlots[] = {
        { "pad_button1", "a", XPAD_BUTTON_X, PADIN_A },
        { "pad_button2", "y", XPAD_BUTTON_SQUARE, PADIN_Y },
        { "pad_button3", "b", XPAD_BUTTON_TRIANGLE, PADIN_B },
        { "pad_button4", "x", XPAD_BUTTON_O, PADIN_X },
        { "pad_button_L1", "lt", XPAD_BUTTON_L1, PADIN_LT },
        { "pad_button_R1", "rt", XPAD_BUTTON_R1, PADIN_RT },
        { "pad_button_L2", "white", XPAD_BUTTON_L2, PADIN_LB },
        { "pad_button_R2", "black", XPAD_BUTTON_R2, PADIN_RB },
        { "pad_button_start", "start", XPAD_BUTTON_START, PADIN_START },
        { "pad_button_select", "back", XPAD_BUTTON_SELECT, PADIN_BACK },
        { "pad_button_L_analog", "move_up", 0, -1 },
        { "pad_button_R_analog", "camera_up", 0, -2 },
    };

    const S32 kSlotCount = (S32)(sizeof(kSlots) / sizeof(kSlots[0]));

    // A picture that shows no button of the pad its font is for.
    const S32 kNoInput = -50;

    // The same slots in the GameCube release's font.HIP: the same names, rows
    // and bits, other pictures. Measured on the disc: A, Y, B and X in slots
    // 1-4, L and R in L1 and R1, Z in R2, the start button, the control stick
    // and the C stick; L2 and SELECT are the PS2's, left in the font, and show
    // nothing a GameCube pad has. The inputs are where an adapter puts those
    // buttons (iPadTokens.cpp: B west, X east, Z on the right bumper).
    const S32 kGameCubeInput[] = { PADIN_A,  PADIN_Y,  PADIN_X,  PADIN_B,
                                   PADIN_LT, PADIN_RT, kNoInput, PADIN_RB,
                                   PADIN_START, kNoInput, -1, -2 };

    TSSM_STATIC_ASSERT(sizeof(kGameCubeInput) / sizeof(kGameCubeInput[0]) ==
                           sizeof(kSlots) / sizeof(kSlots[0]),
                       "one GameCube input per slot");

    // The set whose pictures the disc's are.
    const char* NativeSet()
    {
        return sGameCubeFont ? "gamecube" : "xbox";
    }

    // What slot i's picture shows on the loaded font.
    S32 SlotInput(S32 i)
    {
        return sGameCubeFont ? kGameCubeInput[i] : kSlots[i].input;
    }

    enum Style
    {
        STYLE_KEY,      // a keycap
        STYLE_ROUND,    // a round button with a letter
        STYLE_PILL,     // a shoulder or a small button: text on a rounded bar
        STYLE_PS_CROSS, // the PlayStation's four shapes
        STYLE_PS_CIRCLE,
        STYLE_PS_SQUARE,
        STYLE_PS_TRIANGLE,
        STYLE_STICK // a stick: a ring with a letter
    };

    struct Glyph
    {
        Style style;
        char label[8];
        U8 r, g, b; // the button's colour (the text is white or black)
    };

    // 5x7, top row first, the most significant of the five bits on the left.
    struct FontGlyph
    {
        char c;
        U8 rows[7];
    };

    const FontGlyph kFont[] = {
        { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
        { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
        { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
        { 'D', { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C } },
        { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
        { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
        { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
        { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
        { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
        { 'J', { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C } },
        { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
        { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
        { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
        { 'N', { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 } },
        { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
        { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
        { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
        { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
        { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
        { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
        { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
        { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
        { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
        { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
        { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
        { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
        { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
        { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
        { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
        { '3', { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E } },
        { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
        { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
        { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
        { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
        { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
        { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
        { '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } },
        { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
        { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
        { '/', { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 } },
        { '<', { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 } },
        { '>', { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 } },
        // Arrows: '\x01' up, '\x02' down; '<' and '>' serve as left and right.
        { '\x01', { 0x04, 0x0E, 0x15, 0x04, 0x04, 0x04, 0x04 } },
        { '\x02', { 0x04, 0x04, 0x04, 0x04, 0x15, 0x0E, 0x04 } },
        { '^', { 0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00 } },
        { '[', { 0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E } },
        { ']', { 0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E } },
        { ';', { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08 } },
        { ',', { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08 } },
        { '=', { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 } },
        { '\'', { 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 } },
        { '`', { 0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00 } },
        { '\\', { 0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01 } },
        { '*', { 0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00 } },
    };

    const FontGlyph* FindFont(char c)
    {
        if (c >= 'a' && c <= 'z')
        {
            c = (char)(c - 'a' + 'A');
        }
        for (U32 i = 0; i < sizeof(kFont) / sizeof(kFont[0]); i++)
        {
            if (kFont[i].c == c)
            {
                return &kFont[i];
            }
        }
        return NULL;
    }

    // ---- drawing into a 32x32 RGBA image --------------------------------------

    const S32 kSize = 32;

    struct Canvas
    {
        U8 px[kSize * kSize * 4];

        void clear()
        {
            memset(px, 0, sizeof(px));
        }

        void put(S32 x, S32 y, U8 r, U8 g, U8 b, U8 a)
        {
            if (x < 0 || y < 0 || x >= kSize || y >= kSize)
            {
                return;
            }
            U8* p = px + (y * kSize + x) * 4;
            // Over what is there.
            U32 ia = 255 - a;
            p[0] = (U8)((r * a + p[0] * ia) / 255);
            p[1] = (U8)((g * a + p[1] * ia) / 255);
            p[2] = (U8)((b * a + p[2] * ia) / 255);
            p[3] = (U8)(a + (p[3] * ia) / 255);
        }

        // A filled rounded rectangle, anti-aliased by 4x4 supersampling.
        void roundRect(F32 x0, F32 y0, F32 x1, F32 y1, F32 rad, U8 r, U8 g, U8 b, U8 a)
        {
            for (S32 y = (S32)y0; y <= (S32)y1; y++)
            {
                for (S32 x = (S32)x0; x <= (S32)x1; x++)
                {
                    S32 hits = 0;
                    for (S32 sy = 0; sy < 4; sy++)
                    {
                        for (S32 sx = 0; sx < 4; sx++)
                        {
                            F32 fx = x + (sx + 0.5f) / 4.0f;
                            F32 fy = y + (sy + 0.5f) / 4.0f;
                            F32 cx = fx < x0 + rad ? x0 + rad : (fx > x1 - rad ? x1 - rad : fx);
                            F32 cy = fy < y0 + rad ? y0 + rad : (fy > y1 - rad ? y1 - rad : fy);
                            F32 dx = fx - cx;
                            F32 dy = fy - cy;
                            if (fx >= x0 && fx <= x1 && fy >= y0 && fy <= y1 &&
                                dx * dx + dy * dy <= rad * rad)
                            {
                                hits++;
                            }
                        }
                    }
                    if (hits)
                    {
                        put(x, y, r, g, b, (U8)(a * hits / 16));
                    }
                }
            }
        }

        void circle(F32 cx, F32 cy, F32 rad, U8 r, U8 g, U8 b, U8 a)
        {
            roundRect(cx - rad, cy - rad, cx + rad, cy + rad, rad, r, g, b, a);
        }

        // A one-pixel-wide (times `w`) line, by stepping.
        void line(F32 x0, F32 y0, F32 x1, F32 y1, F32 w, U8 r, U8 g, U8 b)
        {
            F32 dx = x1 - x0;
            F32 dy = y1 - y0;
            S32 steps = (S32)((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy)) * 2 + 1;
            for (S32 i = 0; i <= steps; i++)
            {
                F32 t = (F32)i / steps;
                circle(x0 + dx * t, y0 + dy * t, w * 0.5f, r, g, b, 255);
            }
        }

        // Text in the 5x7 font, centred, at `scale` pixels per font pixel.
        void text(const char* s, S32 scale, U8 r, U8 g, U8 b)
        {
            S32 n = (S32)strlen(s);
            S32 w = n * 6 * scale - scale;
            S32 x = (kSize - w) / 2;
            S32 y = (kSize - 7 * scale) / 2;
            for (S32 i = 0; i < n; i++, x += 6 * scale)
            {
                const FontGlyph* g7 = FindFont(s[i]);
                if (g7 == NULL)
                {
                    continue;
                }
                for (S32 row = 0; row < 7; row++)
                {
                    for (S32 col = 0; col < 5; col++)
                    {
                        if (!(g7->rows[row] & (0x10 >> col)))
                        {
                            continue;
                        }
                        for (S32 py = 0; py < scale; py++)
                        {
                            for (S32 pxx = 0; pxx < scale; pxx++)
                            {
                                put(x + col * scale + pxx, y + row * scale + py, r, g, b, 255);
                            }
                        }
                    }
                }
            }
        }
    };

    void Draw(const Glyph& gl, Canvas& c)
    {
        c.clear();
        const S32 len = (S32)strlen(gl.label);
        const S32 scale = (len <= 2) ? 2 : 1;

        switch (gl.style)
        {
        case STYLE_KEY:
            c.roundRect(1.0f, 4.0f, 30.0f, 28.0f, 4.0f, 40, 40, 40, 255);
            c.roundRect(2.5f, 5.0f, 28.5f, 25.0f, 3.0f, 235, 235, 235, 255);
            c.text(gl.label, len <= 2 ? 2 : 1, 20, 20, 20);
            break;

        case STYLE_ROUND:
            c.circle(16.0f, 16.0f, 14.5f, 20, 20, 20, 255);
            c.circle(16.0f, 16.0f, 13.0f, gl.r, gl.g, gl.b, 255);
            c.text(gl.label, scale, 255, 255, 255);
            break;

        case STYLE_PILL:
            c.roundRect(0.5f, 7.0f, 31.0f, 25.0f, 8.0f, 20, 20, 20, 255);
            c.roundRect(2.0f, 8.5f, 29.5f, 23.5f, 7.0f, gl.r, gl.g, gl.b, 255);
            c.text(gl.label, len <= 2 ? 2 : 1, 255, 255, 255);
            break;

        case STYLE_STICK:
            c.circle(16.0f, 16.0f, 14.5f, 20, 20, 20, 255);
            c.circle(16.0f, 16.0f, 13.0f, gl.r, gl.g, gl.b, 255);
            c.circle(16.0f, 16.0f, 9.0f, (U8)(gl.r / 2), (U8)(gl.g / 2), (U8)(gl.b / 2), 255);
            c.text(gl.label, scale, 255, 255, 255);
            break;

        default:
            c.circle(16.0f, 16.0f, 14.5f, 15, 15, 15, 255);
            c.circle(16.0f, 16.0f, 13.0f, 50, 50, 55, 255);
            if (gl.style == STYLE_PS_CROSS)
            {
                c.line(10, 10, 22, 22, 2.5f, 120, 170, 240);
                c.line(22, 10, 10, 22, 2.5f, 120, 170, 240);
            }
            else if (gl.style == STYLE_PS_CIRCLE)
            {
                c.circle(16.0f, 16.0f, 7.5f, 240, 90, 90, 255);
                c.circle(16.0f, 16.0f, 5.0f, 50, 50, 55, 255);
            }
            else if (gl.style == STYLE_PS_SQUARE)
            {
                c.roundRect(9.5f, 9.5f, 22.5f, 22.5f, 1.0f, 230, 130, 200, 255);
                c.roundRect(12.0f, 12.0f, 20.0f, 20.0f, 0.5f, 50, 50, 55, 255);
            }
            else
            {
                c.line(16, 8, 24, 22, 2.5f, 90, 210, 160);
                c.line(24, 22, 8, 22, 2.5f, 90, 210, 160);
                c.line(8, 22, 16, 8, 2.5f, 90, 210, 160);
            }
            break;
        }
    }

    // ---- what to draw ------------------------------------------------------------

    void SetLabel(Glyph& g, const char* s)
    {
        snprintf(g.label, sizeof(g.label), "%s", s);
    }

    void Colour(Glyph& g, U8 r, U8 gg, U8 b)
    {
        g.r = r;
        g.g = gg;
        g.b = b;
    }

    // A keyboard key's cap text: short enough for a 32-pixel cap.
    void KeyLabel(const char* token, char* out, S32 size)
    {
        struct Short
        {
            const char* token;
            const char* cap;
        };
        static const Short kShort[] = {
            { "space", "SPC" },    { "enter", "ENT" },     { "escape", "ESC" },
            { "backspace", "BKS" }, { "tab", "TAB" },      { "shift", "SHF" },
            { "lshift", "SHF" },   { "rshift", "SHF" },    { "ctrl", "CTL" },
            { "lctrl", "CTL" },    { "rctrl", "CTL" },     { "alt", "ALT" },
            { "lalt", "ALT" },     { "ralt", "ALT" },      { "up", "\x01" },
            { "down", "\x02" },       { "left", "<" },        { "right", ">" },
            { "mouse1", "LMB" },   { "mouse2", "RMB" },    { "mouse3", "MMB" },
            { "mouse4", "M4" },    { "mouse5", "M5" },     { "wheelup", "WH\x01" },
            { "wheeldown", "WH\x02" }, { "capslock", "CAP" }, { "insert", "INS" },
            { "delete", "DEL" },   { "home", "HOM" },      { "end", "END" },
            { "pageup", "PGU" },   { "pagedown", "PGD" },  { "comma", "," },
            { "period", "." },     { "minus", "-" },       { "equals", "=" },
            { "semicolon", ";" },  { "slash", "/" },       { "tilde", "`" },
            { "lbracket", "[" },   { "rbracket", "]" },    { "backslash", "\\" },
            { "quote", "'" },      { "numpadenter", "ENT" }, { "numpadplus", "N+" },
            { "numpadminus", "N-" }, { "numpadstar", "N*" }, { "numpadslash", "N/" },
            { "numpaddot", "N." },
        };

        for (U32 i = 0; i < sizeof(kShort) / sizeof(kShort[0]); i++)
        {
            if (iHostStrCaseCmp(kShort[i].token, token) == 0)
            {
                snprintf(out, (size_t)size, "%s", kShort[i].cap);
                return;
            }
        }

        // numpad7 -> N7, f10 -> F10, letters and digits as they are.
        if (strncmp(token, "numpad", 6) == 0)
        {
            snprintf(out, (size_t)size, "N%s", token + 6);
            return;
        }
        snprintf(out, (size_t)size, "%.4s", token);
    }

    // A pad input drawn the way `set` prints it.
    void PadGlyph(const char* set, S32 input, Glyph& g)
    {
        const char* label = iPadInputLabel(input, set);

        bool face = input == PADIN_A || input == PADIN_B || input == PADIN_X || input == PADIN_Y;
        g.style = face ? STYLE_ROUND : STYLE_PILL;
        Colour(g, 70, 70, 75);

        if (iHostStrCaseCmp(set, "playstation") == 0)
        {
            switch (input)
            {
            case PADIN_A:
                g.style = STYLE_PS_CROSS;
                break;
            case PADIN_B:
                g.style = STYLE_PS_CIRCLE;
                break;
            case PADIN_X:
                g.style = STYLE_PS_SQUARE;
                break;
            case PADIN_Y:
                g.style = STYLE_PS_TRIANGLE;
                break;
            case PADIN_BACK:
                label = "SHR";
                break;
            case PADIN_START:
                label = "OPT";
                break;
            default:
                break;
            }
        }
        else if (iHostStrCaseCmp(set, "gamecube") == 0)
        {
            switch (input)
            {
            case PADIN_A:
                Colour(g, 30, 160, 110);
                break;
            case PADIN_B:
                Colour(g, 200, 40, 40);
                break;
            case PADIN_RB:
                Colour(g, 90, 60, 170);
                break;
            case PADIN_START:
                label = "ST";
                break;
            default:
                break;
            }
        }
        else if (iHostStrCaseCmp(set, "xbox") == 0)
        {
            switch (input)
            {
            case PADIN_A:
                Colour(g, 40, 170, 60);
                break;
            case PADIN_B:
                Colour(g, 210, 40, 40);
                break;
            case PADIN_X:
                Colour(g, 40, 100, 220);
                break;
            case PADIN_Y:
                Colour(g, 220, 170, 30);
                break;
            case PADIN_BACK:
                label = "<";
                break;
            case PADIN_START:
                label = ">";
                break;
            default:
                break;
            }
        }

        switch (input)
        {
        case PADIN_DPUP:
            label = "\x01";
            break;
        case PADIN_DPDOWN:
            label = "\x02";
            break;
        case PADIN_DPLEFT:
            label = "<";
            break;
        case PADIN_DPRIGHT:
            label = ">";
            break;
        case PADIN_LS:
        case PADIN_RS:
            g.style = STYLE_STICK;
            break;
        default:
            break;
        }

        SetLabel(g, label != NULL ? label : "?");
    }

    // ---- state -------------------------------------------------------------------

    struct Applied
    {
        RwTexture* texture;
        RwRaster* retail;
        RwRaster* drawn;
        Glyph glyph;
        bool swapped;
    };

    Applied sApplied[sizeof(kSlots) / sizeof(kSlots[0])];

    struct Cached
    {
        Glyph glyph;
        RwRaster* raster;
    };

    const S32 kCacheMax = 96;
    Cached sCache[kCacheMax];
    S32 sCacheCount;

    enum IconChoice
    {
        ICONS_AUTO,
        ICONS_XBOX,
        ICONS_PLAYSTATION,
        ICONS_SWITCH,
        ICONS_GAMECUBE,
        ICONS_KEYBOARD,
        ICONS_OFF
    };

    const char* const kIconNames[] = { "auto",     "xbox",     "playstation", "switch",
                                       "gamecube", "keyboard", "off" };

    IconChoice sIcons = ICONS_AUTO;
    bool sIconsRead;
    bool sLastWasKeyboard;
    const char* sActiveSet;
    U32 sFrames;
    bool sDirty = true;
    U32 sSignature;

    bool SameGlyph(const Glyph& a, const Glyph& b)
    {
        return a.style == b.style && strcmp(a.label, b.label) == 0 && a.r == b.r && a.g == b.g &&
               a.b == b.b;
    }

    RwRaster* RasterFor(const Glyph& g)
    {
        for (S32 i = 0; i < sCacheCount; i++)
        {
            if (SameGlyph(sCache[i].glyph, g))
            {
                return sCache[i].raster;
            }
        }

        if (sCacheCount >= kCacheMax)
        {
            return NULL;
        }

        static Canvas canvas;
        Draw(g, canvas);

        RwImage* image = RwImageCreate(kSize, kSize, 32);
        if (image == NULL || RwImageAllocatePixels(image) == NULL)
        {
            if (image != NULL)
            {
                RwImageDestroy(image);
            }
            return NULL;
        }

        U8* dst = RwImageGetPixels(image);
        const S32 stride = RwImageGetStride(image);
        for (S32 y = 0; y < kSize; y++)
        {
            memcpy(dst + y * stride, canvas.px + y * kSize * 4, kSize * 4);
        }

        RwRaster* raster =
            RwRasterCreate(kSize, kSize, 32, rwRASTERTYPETEXTURE | rwRASTERFORMAT8888);
        if (raster != NULL && RwRasterSetFromImage(raster, image) == NULL)
        {
            RwRasterDestroy(raster);
            raster = NULL;
        }
        RwImageDestroy(image);

        if (raster == NULL)
        {
            return NULL;
        }

        sCache[sCacheCount].glyph = g;
        sCache[sCacheCount].raster = raster;
        sCacheCount++;
        return raster;
    }

    void ReadIcons()
    {
        if (!sIconsRead)
        {
            sIconsRead = true;
            iPadPromptsSetIcons(iConfigGetString("input.button_icons", "auto"));
        }
    }

    // The set to draw: a choice, or under auto the device last used.
    const char* ChooseSet()
    {
        switch (sIcons)
        {
        case ICONS_OFF:
            return NULL;
        case ICONS_AUTO:
            break;
        default:
            return kIconNames[sIcons];
        }

        const char* kind = iPadHostPadKind();
        const char* device = iPadHostDeviceName(0);
        bool padPresent = device != NULL && iHostStrCaseCmp(device, "keyboard") != 0;

        if (sLastWasKeyboard || !padPresent)
        {
            return "keyboard";
        }
        return kind != NULL ? kind : "xbox";
    }

    const iPadBind* RowBind(const iPadBind* binds, const char* row)
    {
        for (S32 i = 0; i < kPadBindButtonCount; i++)
        {
            if (iHostStrCaseCmp(kPadBindButtons[i].name, row) == 0)
            {
                return &binds[i];
            }
        }
        return NULL;
    }

    // What `set` draws for slot `index`. FALSE: keep the disc's picture.
    bool Decide(const char* set, S32 index, Glyph& g, S32* retailInput)
    {
        const Slot& slot = kSlots[index];
        const S32 shows = SlotInput(index);
        memset(&g, 0, sizeof(g));
        *retailInput = -100;

        if (iHostStrCaseCmp(set, "keyboard") == 0)
        {
            S32 count;
            const iPadBindToken* tokens = iPadKeyTokens(&count);

            if (slot.input == -1 || slot.input == -2)
            {
                // The stick pictures: the four keys, or the mouse.
                g.style = STYLE_KEY;
                if (slot.input == -2 && iPadSettingsGet().mouse_camera)
                {
                    SetLabel(g, "MOUS");
                    return true;
                }

                char word[8] = { 0 };
                const char* rows[4];
                if (slot.input == -1)
                {
                    rows[0] = "move_up";
                    rows[1] = "move_left";
                    rows[2] = "move_down";
                    rows[3] = "move_right";
                }
                else
                {
                    rows[0] = "camera_up";
                    rows[1] = "camera_left";
                    rows[2] = "camera_down";
                    rows[3] = "camera_right";
                }
                for (S32 i = 0; i < 4; i++)
                {
                    const iPadBind* b = RowBind(iPadKeyboardBinds(), rows[i]);
                    S16 id = (b != NULL) ? iPadBindSoleInput(*b) : -1;
                    const char* name = (id >= 0) ? iPadBindTokenName(id, tokens, count) : NULL;
                    word[i] = (name != NULL && name[1] == '\0') ? name[0] : '?';
                }
                SetLabel(g, word);
                return true;
            }

            const iPadBind* b = RowBind(iPadKeyboardBinds(), slot.row);
            S16 id = (b != NULL) ? iPadBindSoleInput(*b) : -1;
            if (b != NULL && id < 0 && b->alts > 0)
            {
                // Several keys: the first one's.
                id = b->id[0][0];
            }
            const char* name = (id >= 0) ? iPadBindTokenName(id, tokens, count) : NULL;

            g.style = STYLE_KEY;
            char cap[8];
            KeyLabel(name != NULL ? name : "-", cap, sizeof(cap));
            SetLabel(g, cap);
            return true;
        }

        // A pad set: whatever the row is bound to on the pad.
        if (slot.input < 0)
        {
            if (iHostStrCaseCmp(set, NativeSet()) == 0)
            {
                return false;
            }
            g.style = STYLE_STICK;
            Colour(g, 70, 70, 75);
            if (iHostStrCaseCmp(set, "gamecube") == 0)
            {
                SetLabel(g, slot.input == -1 ? "L" : "C");
                if (slot.input == -2)
                {
                    Colour(g, 200, 170, 30);
                }
            }
            else
            {
                SetLabel(g, slot.input == -1 ? "L" : "R");
            }
            return true;
        }

        const iPadBind* b = RowBind(iPadHostPadBinds(), slot.row);
        S16 input = (b != NULL) ? iPadBindSoleInput(*b) : -1;

        if (input < 0 && b != NULL && b->alts > 0)
        {
            // A chord, as the GameCube's Z+L: its members' labels joined.
            char joined[8] = { 0 };
            S32 used = 0;
            for (S32 i = 0; i < b->length[0]; i++)
            {
                if (b->negate[0][i])
                {
                    continue;
                }
                const char* l = iPadInputLabel(b->id[0][i], set);
                S32 w = snprintf(joined + used, sizeof(joined) - (size_t)used, "%s%s",
                                 used ? "+" : "", l);
                if (w < 0 || used + w >= (S32)sizeof(joined))
                {
                    break;
                }
                used += w;
            }
            g.style = STYLE_PILL;
            Colour(g, 70, 70, 75);
            SetLabel(g, joined);
            return true;
        }

        if (input < 0)
        {
            // Unbound: say so.
            g.style = STYLE_PILL;
            Colour(g, 70, 70, 75);
            SetLabel(g, "-");
            return true;
        }

        if (iHostStrCaseCmp(set, NativeSet()) == 0)
        {
            // The disc has a picture of this button (an Xbox one on the Xbox
            // disc, a GameCube one on the GameCube's): use it, from whichever
            // slot holds it.
            for (S32 i = 0; i < kSlotCount; i++)
            {
                if (SlotInput(i) == input)
                {
                    *retailInput = input;
                    return true;
                }
            }
        }

        PadGlyph(set, input, g);
        return true;
    }
} // namespace

const char* iPadPromptsLabelFor(const char* set, const char* row)
{
    for (S32 i = 0; i < kSlotCount; i++)
    {
        if (iHostStrCaseCmp(kSlots[i].row, row) != 0)
        {
            continue;
        }

        static Glyph g;
        S32 retail;
        if (!Decide(set, i, g, &retail))
        {
            return NULL;
        }
        if (retail != -100)
        {
            static char buf[16];
            snprintf(buf, sizeof(buf), "%s:%s", NativeSet(), iPadInputLabel(retail, NativeSet()));
            return buf;
        }
        return g.label;
    }
    return NULL;
}

U32 iPadPromptsTextureBits(const char* name)
{
    for (S32 i = 0; i < kSlotCount; i++)
    {
        if (iHostStrCaseCmp(kSlots[i].texture, name) == 0)
        {
            return kSlots[i].bits;
        }
    }
    return 0;
}

void iPadPromptsSetIcons(const char* choice)
{
    sIconsRead = true;
    sIcons = ICONS_AUTO;
    for (S32 i = 0; i < (S32)(sizeof(kIconNames) / sizeof(kIconNames[0])); i++)
    {
        if (choice != NULL && iHostStrCaseCmp(choice, kIconNames[i]) == 0)
        {
            sIcons = (IconChoice)i;
        }
    }
    sDirty = true;
}

const char* iPadPromptsIconsChoice()
{
    return kIconNames[sIcons];
}

void iPadPromptsSetGameCubeFont(bool gamecube)
{
    sGameCubeFont = gamecube;
    sDirty = true;
}

const char* iPadPromptsActiveSet()
{
    return sActiveSet;
}

void iPadPromptsInvalidate()
{
    sDirty = true;
    for (S32 i = 0; i < kPadPromptMoveCount; i++)
    {
        sMoveAsset[i] = NULL;
    }
}

static void Restore(Applied& a)
{
    if (a.texture != NULL && a.swapped)
    {
        RwTextureSetRaster(a.texture, a.retail);
    }
    a.swapped = false;
}

static void ApplyGlyphs(const char* set)
{
    // Whose pictures they are, asked again whenever a new copy of them comes
    // in: a mod's font.HIP can be either release's.
    RwTexture* first = (RwTexture*)xSTFindAsset(xStrHash(kSlots[0].texture), NULL);
    if (first != NULL && first != sApplied[0].texture)
    {
        const char* pkg = xST_xAssetID_HIPFullPath(xStrHash(kSlots[0].texture));
        sGameCubeFont = iFilePackagePlatformOf(pkg) == IGCPLAT_GAMECUBE;
    }

    // First pass: find every slot's texture, and its retail raster.
    RwRaster* retailFor[PADIN_COUNT];
    memset(retailFor, 0, sizeof(retailFor));

    for (S32 i = 0; i < kSlotCount; i++)
    {
        Applied& a = sApplied[i];
        RwTexture* t = (RwTexture*)xSTFindAsset(xStrHash(kSlots[i].texture), NULL);

        if (t != a.texture)
        {
            // A new copy (or the first): what it holds now is the disc's.
            a.texture = t;
            a.retail = (t != NULL) ? t->raster : NULL;
            a.swapped = false;
        }
        else if (a.texture != NULL && !a.swapped)
        {
            a.retail = a.texture->raster;
        }

        if (SlotInput(i) >= 0 && a.retail != NULL)
        {
            retailFor[SlotInput(i)] = a.retail;
        }
    }

    for (S32 i = 0; i < kSlotCount; i++)
    {
        Applied& a = sApplied[i];
        if (a.texture == NULL || a.retail == NULL)
        {
            continue;
        }

        Glyph g;
        S32 retailInput;
        RwRaster* want = a.retail;

        if (set != NULL && Decide(set, i, g, &retailInput))
        {
            if (retailInput >= 0)
            {
                want = retailFor[retailInput] != NULL ? retailFor[retailInput] : a.retail;
            }
            else
            {
                RwRaster* drawn = RasterFor(g);
                if (drawn != NULL)
                {
                    want = drawn;
                }
            }
        }

        if (want == a.retail)
        {
            Restore(a);
        }
        else if (a.texture->raster != want)
        {
            RwTextureSetRaster(a.texture, want);
            a.swapped = true;
        }
    }
}

void iPadPromptsUpdate(const iPadHostState* host)
{
    ReadIcons();

    // The device last used, for icons = auto. Only a deliberate input counts:
    // a key or a button, not a stick resting off-centre.
    if (host != NULL)
    {
        bool keys = (host->sources & (IPAD_HOST_SOURCE_KEYBOARD | IPAD_HOST_SOURCE_MOUSE)) != 0;
        bool pad = (host->sources & IPAD_HOST_SOURCE_PAD) != 0 &&
                   (host->buttons != 0 || host->stick_x * host->stick_x +
                                                  host->stick_y * host->stick_y >
                                              0.25f);
        if (keys && !sLastWasKeyboard)
        {
            sLastWasKeyboard = true;
        }
        else if (pad && !keys && sLastWasKeyboard)
        {
            sLastWasKeyboard = false;
        }
    }

    const char* set = ChooseSet();
    U32 signature = (U32)iPadLayoutActive() | (sLastWasKeyboard ? 2u : 0u) | (U32)sIcons << 2 |
                    iPadHostBindGeneration() << 8;
    if (set != sActiveSet || signature != sSignature)
    {
        sDirty = true;
    }

    // Twice a second whatever happens, to catch a level's own copies.
    if (!sDirty && (++sFrames % 30) != 0)
    {
        return;
    }

    ApplyLayout(sDirty);
    ApplyVerb(set);
    ApplyGlyphs(set);

    sActiveSet = set;
    sSignature = signature;
    sDirty = false;
}

RwRaster* iPadPromptsDiscRaster(RwTexture* texture)
{
    if (texture == NULL)
    {
        return NULL;
    }
    for (S32 i = 0; i < kSlotCount; i++)
    {
        const Applied& a = sApplied[i];
        if (a.texture == texture && a.swapped)
        {
            return a.retail;
        }
    }
    return texture->raster;
}

void iPadPromptsShutdown()
{
    for (S32 i = 0; i < kSlotCount; i++)
    {
        Restore(sApplied[i]);
        sApplied[i].texture = NULL;
    }

    for (S32 i = 0; i < sCacheCount; i++)
    {
        RwRasterDestroy(sCache[i].raster);
    }
    sCacheCount = 0;
    sActiveSet = NULL;
}
