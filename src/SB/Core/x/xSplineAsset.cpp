#include "xSplineAsset.h"

#include "xVec3Inlines.h"

namespace x
{
    void NURBS::find_basis_functions(F32 u, S32 i, F32* N)
    {
        F32 left[5];
        F32 right[5];

        N[0] = 1.0f;

        for (S32 j = 1; j <= p; j++)
        {
            left[j] = u - knot[i + 1 - j];
            right[j] = knot[i + j] - u;
            F32 saved = 0.0f;

            for (S32 r = 0; r < j; r++)
            {
                F32 temp = N[r] / (right[r + 1] + left[j - r]);
                N[r] = saved + right[r + 1] * temp;
                saved = left[j - r] * temp;
            }

            N[j] = saved;
        }
    }

    void NURBS::find_derivative_basis_functions(F32 u, S32 i, S32 order, F32 ders[][5])
    {
        F32 ndu[5][5];
        ndu[0][0] = 1.0f;

        F32 left[5];
        F32 right[5];
        S32 j;
        for (j = 1; j <= p; j++)
        {
            left[j] = u - knot[i + 1 - j];
            right[j] = knot[i + j] - u;
            F32 saved = 0.0f;

            for (S32 r = 0; r < j; r++)
            {
                ndu[j][r] = right[r + 1] + left[j - r];
                F32 temp = ndu[r][j - 1] / ndu[j][r];
                ndu[r][j] = saved + right[r + 1] * temp;
                saved = left[j - r] * temp;
            }

            ndu[j][j] = saved;
        }

        for (j = 0; j <= p; j++)
        {
            ders[0][j] = ndu[j][p];
        }

        F32 a[2][5];
        S32 r;
        for (r = 0; r <= p; r++)
        {
            S32 s1 = 0;
            S32 s2 = 1;
            a[0][0] = 1.0f;

            for (S32 k = 1; k <= order; k++)
            {
                F32 d = 0.0f;
                S32 rk = r - k;
                S32 pk = p - k;

                if (r >= k)
                {
                    a[s2][0] = a[s1][0] / ndu[pk + 1][rk];
                    d = a[s2][0] * ndu[rk][pk];
                }

                S32 j1;
                S32 j2;

                if (rk >= -1)
                {
                    j1 = 1;
                }
                else
                {
                    j1 = -rk;
                }

                if (r - 1 <= pk)
                {
                    j2 = k - 1;
                }
                else
                {
                    j2 = p - r;
                }

                for (S32 j = j1; j <= j2; j++)
                {
                    a[s2][j] = (a[s1][j] - a[s1][j - 1]) / ndu[pk + 1][rk + j];
                    d += a[s2][j] * ndu[rk + j][pk];
                }

                if (r <= pk)
                {
                    a[s2][k] = -a[s1][k - 1] / ndu[pk + 1][r];
                    d += a[s2][k] * ndu[r][pk];
                }

                ders[k][r] = d;

                S32 temp = s1;
                s1 = s2;
                s2 = temp;
            }
        }

        r = p;
        for (S32 k = 1; k <= order; k++)
        {
            for (S32 j = 0; j <= p; j++)
            {
                ders[k][j] *= r;
            }
            r *= p - k;
        }
    }

    F32 NURBS::start()
    {
        return knot[p];
    }

    void NURBS::evaluate(F32 u, xVec3& point)
    {
        S32 span = find_span(u);
        F32 N[5];
        find_basis_functions(u, span, N);

        point = xVec3::m_Null;

        for (S32 i = 0; i <= p; i++)
        {
            point += control[span - p + i] * N[i];
        }
    }

    xVec3 NURBS::evaluate(F32 u, S32 d)
    {
        xVec3 point;
        evaluate(u, d, point);
        return point;
    }

    // Dead-stripped placeholder, not recovered code: retail's .sdata2 has 0.001f and 0.01f right
    // after advance_u's literal, from stripped functions defined just above it (The Incredibles
    // has NURBS::circle_intersect and NURBS::project_point there). Their bodies are unknown.
    static void __deadstripped_xSplineAsset(F32* out)
    {
        out[0] = 0.001f;
        out[1] = 0.01f;
    }

    U8 NURBS::advance_u(F32 start_u, F32 distance, bool forward, F32& new_u)
    {
        F32 spline_start = start();
        F32 spline_end = end();

        if (start_u < spline_start)
        {
            start_u = spline_start;
        }
        else if (start_u > spline_end)
        {
            start_u = spline_end;
        }

        new_u = start_u;

        xVec3 velocity_vec;
        evaluate(start_u, 1, velocity_vec);
        F32 vel_mag = xVec3NormalizeSafe(velocity_vec);

        if (vel_mag == 0.0f)
        {
            return false;
        }

        if (forward == true)
        {
            new_u = start_u + distance / vel_mag;
            if (new_u > spline_end)
            {
                new_u = spline_end;
                return false;
            }
        }
        else
        {
            new_u = start_u - distance / vel_mag;
            if (new_u < spline_start)
            {
                new_u = spline_start;
                return false;
            }
        }

        return true;
    }
} // namespace x
