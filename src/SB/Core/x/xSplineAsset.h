#ifndef XSPLINEASSET_H
#define XSPLINEASSET_H

#include <types.h>
#include "xBase.h"
#include "xVec3.h"

namespace x
{
    struct NURBS : xBaseAsset
    {
        S32 p; // 0x8
        S32 m; // 0xc
        S32 n; // 0x10
        F32* knot; // 0x14
        xVec3* control; // 0x18

        U8 advance_u(F32 start_u, F32 distance, bool forward, F32& new_u);
        xVec3 evaluate(F32 u, S32 d);
        void evaluate(F32 u, xVec3& point);
        F32 start();
        void find_derivative_basis_functions(F32 u, S32 i, S32 order, F32 ders[][5]);
        void find_basis_functions(F32 u, S32 i, F32* N);

        // No GC/PS2 symbol (always inlined).
        F32 end()
        {
            return knot[n + 1];
        }

        // No GC/PS2 symbol (always inlined).
        S32 find_span(F32 u)
        {
            if (u == knot[n + 1])
            {
                return n;
            }

            S32 low = p;
            S32 high = n + 1;
            S32 mid = (low + high) / 2;

            while (u < knot[mid] || u >= knot[mid + 1])
            {
                if (u < knot[mid])
                {
                    high = mid;
                }
                else
                {
                    low = mid;
                }
                mid = (low + high) / 2;
            }

            return mid;
        }

        // No GC/PS2 symbol (always inlined).
        void evaluate(F32 u, S32 d, xVec3& point)
        {
            point = xVec3::m_Null;

            if (d > p)
            {
                return;
            }

            S32 span = find_span(u);
            F32 ders[5][5];
            find_derivative_basis_functions(u, span, d, ders);

            for (S32 j = 0; j <= p; j++)
            {
                point += control[span - p + j] * ders[d][j];
            }
        }
    };

    void* Spline_Read(void* userdata, U32 assetid, void* indata, U32 insize, U32* outsize);
} // namespace x

#endif
