#ifndef XVEC3_H
#define XVEC3_H

#include <types.h>
#include "rwplcore.h"
#include "xMathInlines.h"
#include "iMath.h"

//movies dwarf
// struct xVec3
// {
//     union
//     {
//         RwV3d m_RwV3d;
//         F32 x;
//     };
//     F32 y;
//     F32 z;
//     F32 a[3];
// };

struct xVec3
{
    union
    {
        RwV3d m_RwV3d;
        struct
        {
            F32 x, y, z;
        };
        F32 a[3];
    };

    static const xVec3 m_Null;
    static const xVec3 m_UnitAxisX;
    static const xVec3 m_UnitAxisY;

    static xVec3 create(F32 x, F32 y, F32 z);
    static xVec3 create(F32 f);

    xVec3& operator=(F32 f)
    {
        x = y = z = f;
        return *this;
    }
    // FIXME: This should not be declared. Declaring it causes the assignment operator for the
    //        anonymous struct "camera" in hook_asset to call it rather than just copy the vec members.
    //xVec3& operator=(const xVec3& v);
    xVec3 operator+(const xVec3& v) const
    {
        xVec3 temp = *this;
        temp += v;
        return temp;
    }

    // No GC symbol (always inlined); from BfBB.
    xVec3 operator+(F32 f) const
    {
        xVec3 temp = *this;
        temp += f;
        return temp;
    }

    xVec3 operator-(const xVec3& v) const
    {
        xVec3 temp = *this;
        temp -= v;
        return temp;
    }

    xVec3 operator-() const
    {
        xVec3 temp = *this;
        temp.x = -temp.x;
        temp.y = -temp.y;
        temp.z = -temp.z;
        return temp;
    }

    xVec3 operator*(F32 f) const
    {
        xVec3 temp = *this;
        temp *= f;
        return temp;
    }

    xVec3 operator*(const xVec3& v) const
    {
        xVec3 temp = *this;
        temp *= v;
        return temp;
    }

    // No GC symbol (always inlined); from BfBB.
    xVec3 operator/(F32 f) const
    {
        xVec3 vec = *this;
        vec /= f;
        return vec;
    }

    // No GC symbol (always inlined); from Ratatouille.
    bool operator==(const xVec3& v) const
    {
        return x == v.x && y == v.y && z == v.z;
    }

    xVec3& operator+=(const xVec3& v)
    {
        this->x += v.x;
        this->y += v.y;
        this->z += v.z;

        return *this;
    }

    xVec3& operator+=(F32 f)
    {
        this->x += f;
        this->y += f;
        this->z += f;

        return *this;
    }
    xVec3& operator-=(const xVec3& v)
    {
        this->x -= v.x;
        this->y -= v.y;
        this->z -= v.z;

        return *this;
    }

    xVec3& operator-=(F32 f)
    {
        this->x -= f;
        this->y -= f;
        this->z -= f;

        return *this;
    }

    xVec3& operator*=(F32 f)
    {
        this->x *= f;
        this->y *= f;
        this->z *= f;

        return *this;
    }

    xVec3& operator*=(const xVec3& v)
    {
        this->x *= v.x;
        this->y *= v.y;
        this->z *= v.z;

        return *this;
    }

    xVec3& operator/=(F32 f)
    {
        F32 i = 1.0f / f;

        this->x *= i;
        this->y *= i;
        this->z *= i;

        return *this;
    }

    xVec3& right_normalize()
    {
        return safe_normalize(m_UnitAxisX);
    }
    // len is const and comes from length() rather than xsqrt(length2()): both show in the register
    // and stack layout of callers that inline this.
    xVec3& safe_normalize(const xVec3& val)
    {
        const F32 len = length();
        if (len < 0.00001f)
        {
            return (*this = val);
        }
        return (*this *= 1.0f / len);
    }
    xVec3& up_normalize()
    {
        return safe_normalize(m_UnitAxisY);
    }
    // No symbol; the name follows the BfBB fork. up_normal has no locals of its own in BfBB's PS2
    // DWARF, so it forwards to this helper, which makes the copy.
    xVec3 safe_normal(const xVec3& val) const
    {
        xVec3 v = *this;
        v.safe_normalize(val);
        return v;
    }
    xVec3 up_normal() const
    {
        return safe_normal(m_UnitAxisY);
    }
    xVec3& assign(F32 x, F32 y, F32 z)
    {
        this->x = x;
        this->y = y;
        this->z = z;

        return *this;
    }
    F32 length() const
    {
        return xsqrt(length2());
    }

    F32 length2() const
    {
        return this->x * this->x + this->y * this->y + this->z * this->z;
    }
    xVec3& invert()
    {
        x = -x;
        y = -y;
        z = -z;
        return *this;
    }
    F32 dot(const xVec3& c) const
    {
        return x * c.x + y * c.y + z * c.z;
    }
    // No symbol on GC (always inlined); from Ratatouille.
    xVec3& cross(const xVec3& c0, const xVec3& c1)
    {
        x = c0.y * c1.z - c1.y * c0.z;
        y = c0.z * c1.x - c1.z * c0.x;
        z = c0.x * c1.y - c1.x * c0.y;

        return *this;
    }
    // No symbol on GC (always inlined). Retail copies v from an all-zero template, which only the
    // value-initialization xVec3() produces.
    xVec3 cross(const xVec3& c) const
    {
        xVec3 v = xVec3();

        v.x = y * c.z - c.y * z;
        v.y = z * c.x - c.z * x;
        v.z = x * c.y - c.x * y;

        return v;
    }
    // Fast normalize: scales by the reciprocal square root estimate (no Newton step, no
    // small-length check) and returns the estimated length. Register locals: iRSqrtEst is asm.
    F32 normalize()
    {
        register F32 len2 = length2();
        register F32 rlen;

        iRSqrtEst(rlen, len2);

        x *= rlen;
        y *= rlen;
        z *= rlen;

        return 1.0f / rlen;
    }
    xVec3 normal() const
    {
        xVec3 v = *this;
        v.normalize();
        return v;
    }
    xVec3& assign(F32 val);
    // No symbol on GC (always inlined)
    xVec3& set_abs()
    {
        x = iabs(x);
        y = iabs(y);
        z = iabs(z);
        return *this;
    }
    xVec3 get_abs() const
    {
        xVec3 vec = *this;
        return vec.set_abs();
    }
};

F32 xVec3Normalize(xVec3* o, const xVec3* v);
F32 xVec3NormalizeFast(xVec3* o, const xVec3* v);
void xVec3Copy(xVec3* dst, const xVec3* src);
F32 xVec3Dot(const xVec3* vec1, const xVec3* vec2);

#endif
