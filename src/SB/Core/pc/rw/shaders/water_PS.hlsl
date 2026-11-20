// The Xbox release's water surface, pixel stage (iXboxWater.h, rw/xboxwater.cpp).
//
// Written from the behaviour of the Xbox's pixel shader definition (DEFAULT.XBE,
// created by 0x1B1860), not translated from it. On the Xbox it is three texture
// stages and three combiner stages:
//
//   t0 = the bump frame (V8U8: signed du, dv)
//   t1 = texbem from t0 into the refraction target, the stage-1 bump matrix
//   t2 = texbem from t0 into the reflection target, the stage-2 bump matrix
//   r0 = tint * t1                                  combiner stage 0
//   r1 = tint.a * t2                                combiner stage 1
//   r0 = r1 * tint + r0                             combiner stage 2
//   rgb = fog.a * r0 + (1 - fog.a) * fog colour     final combiner, clamped
//   alpha = t1.a                                    the refraction's alpha
//
// tint is pixel constant 4, (0.55, 0.65, 0.85, 0.5), SC02's rgb (0.6, 0.8, 0.9).
// A fourth stage has the fresnel texture bound, but the shader never reads it.
//
// ps_2_0 has no texbem: the bump matrix is applied in arithmetic and the two
// target reads depend on the bump read (two levels; ps_2_0 allows four).

#include "rwshader.h"

struct VS_out
{
    float4 Position : SV_POSITION;
    float2 Bump : TEXCOORD0;
    float3 Screen : TEXCOORD1; // xy: the screen position, z: fog
};

RW_TEXTURE(bump, 0);
RW_TEXTURE(refraction, 1);
RW_TEXTURE(reflection, 2);

// c0 is librw's fog colour (PSLOC_fogColor); c1 to c3 are a pass's own.
float4 fogColor : register(c0);
float4 tint : register(c1);
// Each bump matrix as (m00, m01, m10, m11), already scaled for the tile's
// distance: du' = m00 du + m10 dv, dv' = m01 du + m11 dv (texbem's order).
float4 bumpRefract : register(c2);
float4 bumpReflect : register(c3);

float4 main(VS_out input) : SV_Target
{
    // The frame is stored as unsigned bytes, du + 128 and dv + 128, so that
    // every renderer can filter it; filtering those and taking 128 off after
    // gives what filtering the signed bytes does.
    float2 d = RW_SAMPLE(bump, input.Bump).rg * (255.0 / 128.0) - 1.0;

    float2 o1 = float2(bumpRefract.x * d.x + bumpRefract.z * d.y,
                       bumpRefract.y * d.x + bumpRefract.w * d.y);
    float2 o2 = float2(bumpReflect.x * d.x + bumpReflect.z * d.y,
                       bumpReflect.y * d.x + bumpReflect.w * d.y);

    float4 r = RW_SAMPLE(refraction, input.Screen.xy + o1);
    float4 l = RW_SAMPLE(reflection, input.Screen.xy + o2);

    float3 rgb = saturate(tint.rgb * r.rgb + tint.rgb * (tint.a * l.rgb));
    rgb = lerp(fogColor.rgb, rgb, input.Screen.z);

    // No alpha test: the Xbox switches it off for the water, and the alpha is
    // the glow's mask, not a coverage.
    return float4(rgb, r.a);
}
