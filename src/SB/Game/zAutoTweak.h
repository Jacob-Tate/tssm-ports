#ifndef ZAUTOTWEAK_H
#define ZAUTOTWEAK_H

#include <types.h>

// The tweak system first: its templates precede this header's load_param template in .rodata.
#include "xDebug.h"
#include "xDebugTweak.h"
#include "iColor.h"
#include "xMath3.h"
#include "zEnt.h"

struct xModelAssetParam;

// Tweak loaders used by cruise_bubble's register_tweaks (header name from Ratatouille). Retail has
// the specializations out of line as local symbols, which inline specializations of a static
// primary template (as in BfBB) produce.
namespace auto_tweak
{
    template <class T1, class T2>
    static void load_param(T1&, T2, T2, T2, xModelAssetParam*, U32, const char*);

    template <>
    inline void load_param<U32, S32>(U32& param, S32 scale, S32 pmin, S32 pmax,
                                     xModelAssetParam* ap, U32 apsize, const char* prop)
    {
        if (ap != NULL)
        {
            S32 v = zParamGetInt(ap, apsize, prop, param);
            if (v < pmin)
            {
                v = pmin;
            }
            else if (v > pmax)
            {
                v = pmax;
            }
            param = v * scale;
        }
    }

    template <>
    inline void load_param<xVec3, S32>(xVec3& param, S32, S32, S32, xModelAssetParam* ap,
                                       U32 apsize, const char* prop)
    {
        zParamGetVector(ap, apsize, prop, param, &param);
    }

    template <>
    inline void load_param<S32, S32>(S32& param, S32 scale, S32 pmin, S32 pmax,
                                     xModelAssetParam* ap, U32 apsize, const char* prop)
    {
        if (ap != NULL)
        {
            S32 v = zParamGetInt(ap, apsize, prop, param);
            if (v < pmin)
            {
                v = pmin;
            }
            else if (v > pmax)
            {
                v = pmax;
            }
            param = v * scale;
        }
    }

    template <>
    inline void load_param<F32, F32>(F32& param, F32 scale, F32 pmin, F32 pmax,
                                     xModelAssetParam* ap, U32 apsize, const char* prop)
    {
        if (ap != NULL)
        {
            param = zParamGetFloat(ap, apsize, prop, param);
        }

        if (param < pmin)
        {
            param = pmin;
        }
        else if (param > pmax)
        {
            param = pmax;
        }

        param *= scale;
    }

    // Unused in TSSM (BfBB uses it), but defining it here creates its 16-byte zero def template in
    // every unit that parses the header, as retail has.
    template <>
    inline void load_param<xColor_tag, S32>(xColor_tag& param, S32, S32, S32, xModelAssetParam* ap,
                                            U32 apsize, const char* prop)
    {
        F32 def[4] = { (F32)param.r, (F32)param.g, (F32)param.b, (F32)param.a };
        F32 result[4];

        zParamGetFloatList(ap, apsize, prop, 4, def, result);

        param.r = result[0];
        param.g = result[1];
        param.b = result[2];
        param.a = result[3];
    }
} // namespace auto_tweak

#endif
