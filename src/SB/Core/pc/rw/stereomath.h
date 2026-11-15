#ifndef STEREOMATH_H
#define STEREOMATH_H

// The stereo eye terms, shared by the 3DS renderer (rw::c3d's eye shift,
// docs/3DS.md "Stereoscopic 3D") and the PC's test hook (TSSM_TEST_EYE, which
// adds the same shift to GL3's projection so each eye has a reference).
// Header only, no platform code.
//
// The off-axis eye: an eye moved along the camera's right with the frustum
// skewed back so that the plane at camera z = C does not move. In clip space,
// before the 3DS's tilt:
//
//   delta x_clip = sigma * k/2 * w * (1 - C / max(w, wMin))
//
// sigma -1 the left eye, +1 the right; w the clip w (the camera z of a
// perspective camera). Nothing moves at C, things far away part by k in NDC
// (the far parallax), nearer than C they cross, and wMin caps how far.

#include <stdlib.h>
#include <string.h>

namespace stereomath
{
    // The far parallax at the slider's top and stereo_depth 100, in pixels
    // of the 400-pixel-wide top screen (to be set on the console).
    const float kMaxParallaxPx = 12.0f;
    // Pixels across the screen's half width: one NDC unit.
    const float kPxPerNdc = 200.0f;

    struct Terms
    {
        float k; // the far parallax, NDC units (R minus L)
        float wMin; // the near cap's w
    };

    // s: the slider, eased, 0..1; depth: stereo_depth, 0..100; C: the
    // convergence plane; nearCap: crossed parallax at most half the far one
    // (else only the camera's near plane caps it).
    inline Terms terms(float s, float depth, float C, bool nearCap, float camNear)
    {
        Terms t;
        t.k = s * depth / 100.0f * kMaxParallaxPx / kPxPerNdc;
        // Crossed parallax k (C / w - 1) reaches k/2 at w = C k / (k + k/2).
        t.wMin = nearCap ? C * (2.0f / 3.0f) : camNear;
        if (t.wMin < 1e-4f)
        {
            t.wMin = 1e-4f;
        }
        return t;
    }

    // The shift of clip x for eye `sigma`, as vs_*'s eye procedure adds it.
    inline float shiftClipX(const Terms& t, float sigma, float C, float w)
    {
        const float we = w > t.wMin ? w : t.wMin;
        return sigma * 0.5f * t.k * w * (1.0f - C / we);
    }

    // TSSM_C3D_STEREO / [video] stereo: off, auto (the slider), or
    // force[:depth[:C]] (the slider taken as full, depth and the convergence
    // plane held when given).
    enum
    {
        MODE_OFF,
        MODE_AUTO,
        MODE_FORCE
    };
    struct Setting
    {
        int mode;
        bool hasDepth;
        float depth;
        bool hasC;
        float C;
    };

    // FALSE: not one of those (out is left as it was).
    inline bool parse(const char* text, Setting* out)
    {
        if (text == NULL)
        {
            return false;
        }
        Setting st;
        st.mode = MODE_AUTO;
        st.hasDepth = false;
        st.depth = 0.0f;
        st.hasC = false;
        st.C = 0.0f;
        if (strcmp(text, "off") == 0)
        {
            st.mode = MODE_OFF;
        }
        else if (strcmp(text, "auto") == 0)
        {
            st.mode = MODE_AUTO;
        }
        else if (strncmp(text, "force", 5) == 0 && (text[5] == '\0' || text[5] == ':'))
        {
            st.mode = MODE_FORCE;
            const char* p = text + 5;
            if (*p == ':')
            {
                char* end;
                st.depth = (float)strtod(p + 1, &end);
                if (end == p + 1 || st.depth < 0.0f || st.depth > 100.0f)
                {
                    return false;
                }
                st.hasDepth = true;
                p = end;
                if (*p == ':')
                {
                    st.C = (float)strtod(p + 1, &end);
                    if (end == p + 1 || st.C <= 0.0f)
                    {
                        return false;
                    }
                    st.hasC = true;
                    p = end;
                }
            }
            if (*p != '\0')
            {
                return false;
            }
        }
        else
        {
            return false;
        }
        *out = st;
        return true;
    }
} // namespace stereomath

#endif
