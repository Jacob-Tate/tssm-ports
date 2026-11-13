#ifndef XMATH2_H
#define XMATH2_H

#include <types.h>

#include "xMath.h"
#include "xMathInlines.h"

template <class T> struct basic_rect
{
    T x;
    T y;
    T w;
    T h;

    const static basic_rect m_Null;
    const static basic_rect m_Unit;

    // No symbol in TSSM (inlined into parse_tag_model); Ratatouille's GC DWARF lists it in xMath2.h.
    basic_rect& assign(T x, T y, T w, T h)
    {
        this->x = x;
        this->y = y;
        this->w = w;
        this->h = h;
        return *this;
    }
    basic_rect& contract(T s)
    {
        return expand(-s);
    }
    // No symbol in TSSM (inlined into zUIText::DoRender); body as in BfBB.
    basic_rect& contract(T x, T y, T w, T h)
    {
        return expand(-x, -y, -w, -h);
    }
    basic_rect& expand(T s)
    {
        return expand(s, s, s, s);
    }
    basic_rect& expand(T x, T y, T w, T h)
    {
        this->x -= x;
        this->w += x + w;
        this->y -= y;
        this->h += y + h;
        return *this;
    }
    basic_rect& move(T x, T y)
    {
        this->x += x;
        this->y += y;
        return *this;
    }
    basic_rect& scale(T s)
    {
        return scale(s, s, s, s);
    }
    basic_rect& scale(T x, T y)
    {
        return scale(x, y, x, y);
    }
    basic_rect& scale(T x, T y, T w, T h)
    {
        this->x *= x;
        this->y *= y;
        this->w *= w;
        this->h *= h;
        return *this;
    }
    // No symbol in TSSM (inlined into tex_render). Clips r1 to this rect and shrinks r2 (r1's
    // texture-space rect) by the same proportion.
    void clip(basic_rect& r1, basic_rect& r2) const
    {
        T xratio = r2.w / r1.w;
        T yratio = r2.h / r1.h;

        if (r1.x < x)
        {
            T d1 = x - r1.x;
            T d2 = xratio * d1;

            r1.x = x;
            r1.w -= d1;
            r2.x += d2;
            r2.w -= d2;
        }

        if (r1.y < y)
        {
            T d1 = y - r1.y;
            T d2 = yratio * d1;

            r1.y = y;
            r1.h -= d1;
            r2.y += d2;
            r2.h -= d2;
        }

        T r = r1.x + r1.w;
        T cr = x + w;

        if (r > cr)
        {
            r2.w -= xratio * (r - cr);
            r1.w = cr - r1.x;
        }

        T b = r1.y + r1.h;
        T cb = y + h;

        if (b > cb)
        {
            r2.h -= yratio * (b - cb);
            r1.h = cb - r1.y;
        }
    }
    void set_bounds(T x1, T y1, T x2, T y2)
    {
        x = x1;
        w = x2 - x1;
        y = y1;
        h = y2 - y1;
    }
    void get_bounds(T& x1, T& y1, T& x2, T& y2) const
    {
        x1 = x;
        x2 = x + w;
        y1 = y;
        y2 = y + h;
    }
    // No symbol in TSSM (always inlined).
    bool empty() const
    {
        return w <= 0 || h <= 0;
    }
    // No symbol in TSSM (inlined into cruise_bubble's render_hud, which stores w before h).
    void set_size(T w, T h)
    {
        this->w = w;
        this->h = h;
    }
    // No symbol in TSSM (always inlined). Returns *this like the other basic_rect modifiers
    // (BfBB's show_hud chains center() on it).
    basic_rect& set_size(T s)
    {
        w = h = s;
        return *this;
    }
    void center(T x, T y)
    {
        this->x = x - 0.5f * w;
        this->y = y - 0.5f * h;
    }

    basic_rect& operator|=(const basic_rect& other)
    {
        T x1, y1, x2, y2;
        T _x1, _y1, _x2, _y2;

        this->get_bounds(x1, y1, x2, y2);
        other.get_bounds(_x1, _y1, _x2, _y2);

        if (x1 > _x1)
        {
            x1 = _x1;
        }

        if (y1 > _y1)
        {
            y1 = _y1;
        }

        if (x2 < _x2)
        {
            x2 = _x2;
        }

        if (y2 < _y2)
        {
            y2 = _y2;
        }

        set_bounds(x1, y1, x2, y2);

        return *this;
    }
};

struct xVec2
{
    F32 x;
    F32 y;

    xVec2& assign(F32 xy)
    {
        return assign(xy, xy);
    }
    xVec2& assign(F32 x, F32 y)
    {
        this->x = x;
        this->y = y;
        return *this;
    }
    F32 length() const
    {
        return xsqrt(length2());
    }

    // Fast normal: scales by the raw reciprocal square root estimate (no Newton step), as the TSSM
    // callers do (oob_state grab_state_type::start).
    xVec2 normal() const
    {
        xVec2 tmp = *this;
        tmp.normalize();
        return tmp;
    }

    // Raw reciprocal square root estimate (iRSqrtEst), as xVec3::normalize. len2/rlen exist
    // because the macro's operands must be register variables.
    xVec2& normalize()
    {
        register F32 len2 = length2();
        register F32 rlen;

        iRSqrtEst(rlen, len2);

        x *= rlen;
        y *= rlen;

        return *this;
    }

    F32 dot(const xVec2& b) const
    {
        return x * b.x + y * b.y;
    }

    xVec2& operator=(F32 f)
    {
        x = y = f;
        return *this;
    }
    xVec2 operator*(F32 f) const
    {
        xVec2 tmp = *this;
        tmp *= f;
        return tmp;
    }

    xVec2& operator/=(F32 f)
    {
        F32 i = 1.0f / f;
        x *= i;
        y *= i;
        return *this;
    }

    xVec2& operator+=(const xVec2& v)
    {
        x += v.x;
        y += v.y;
        return *this;
    }

    xVec2& operator*=(F32 f)
    {
        x *= f;
        y *= f;
        return *this;
    }

    F32 length2() const
    {
        return x * x + y * y;
    }

    xVec2& operator-=(const xVec2& v)
    {
        x -= v.x;
        y -= v.y;
        return *this;
    }

    xVec2 operator-(const xVec2& v) const
    {
        xVec2 vec = *this;
        vec -= v;
        return vec;
    }
};

inline F32 xVec2Dist(F32 x1, F32 y1, F32 x2, F32 y2)
{
    F32 x = x1 - x2;
    F32 y = y1 - y2;

    return xsqrt(SQR(x) + SQR(y));
}

// No symbols in TSSM (always inlined); Ratatouille's GC DWARF has both in xMath2.h.
inline F32 xVec2Dot(const xVec2* v1, const xVec2* v2)
{
    return v1->x * v2->x + v1->y * v2->y;
}

inline void xVec2Init(xVec2* v, F32 x, F32 y)
{
    v->x = x;
    v->y = y;
}

struct _xMat2x2
{
    xVec2 right;
    xVec2 up;
};

typedef struct _xMat2x2 xMat2x2;

struct _xMat3x2
{
    xVec2 right;
    xVec2 up;
    xVec2 pos;
};

typedef struct _xMat3x2 xMat3x2;

extern const xMat3x2 g_I2;

#endif
