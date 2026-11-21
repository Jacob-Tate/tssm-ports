// The Xbox release's water surface, vertex stage (iXboxWater.h, rw/xboxwater.cpp).
//
// Written from the behaviour of the Xbox's vertex shader (DEFAULT.XBE, created
// by 0x1B1860; 16 instructions), not translated from it: that one is NV2A
// microcode and nothing here takes it. What it does per vertex:
//
//   oPos = P * V * W * position                   (c4..c7, set at 0x1C3140)
//   oT0  = the bump coordinate                    (from the vertex)
//   oT1 = oT2 = the vertex's screen position / (screen width, height)
//                                                 (instructions 5, 7, 9, 11)
//   oFog = 1.00025 - 0.0025006 * w                (c18: 1 at 0.1 units, 0 at 400)
//
// oFog is not the fog amount. The NV2A puts a vertex shader's oFog through the
// fog table as the distance, and the water leaves the table as the scene has
// it: linear (0x1CF780 sets it, from the level's fog), from FOGSTART, the
// camera's fog distance, to FOGEND, its far plane. Direct3D programs that as
// (FOGEND - oFog) / (FOGEND - FOGSTART), with 8192 in place of the division
// when the two are equal (0x225F10). oFog is 0 to 1 and the planes are tens
// or hundreds of units, so in B301 and SC02 the surface is not fogged.
//
// The screen position is worked out per vertex and interpolated as an ordinary
// texture coordinate, which is how the Xbox has it -- the pixel stage's texbem
// does no projective divide. Taking the pixel's own screen position instead
// would move the reflections differently over a large triangle.
//
// The Xbox's vertices are world positions written per tile into a buffer of
// their own (0x1C2310). Here one buffer per grid size holds each vertex's
// (i, j), and the tile's corner, height and cell size come in a constant: the
// same numbers, two buffers instead of one a tile.

#define RW_VERTEX_STAGE
#include "rwshader.h"

// librw's world * view * projection (VSLOC_combined), the body's frame as the
// world, which is the Xbox's P * V * W.
float4x4 combinedMat : register(c0);

// c41 and up: where librw's pipelines put their own constants, uploaded right
// before each draw (rwd3d.h).
float4 tile : register(c41); // x0, y, z0, cell
float4 grid : register(c42); // 1 / vertices per side, unused, unused, unused

// librw's fog constant (VSLOC_fogData): x the camera's fog distance, y its far
// plane, the numbers the Xbox's RenderWare copies into FOGSTART and FOGEND.
float4 fogData : register(c14);

struct VS_in
{
    float2 ij : POSITION;
};

struct VS_out
{
    float4 Position : SV_POSITION;
    float2 Bump : TEXCOORD0;
    float3 Screen : TEXCOORD1; // xy: the screen position in 0..1, D3D's way up; z: the fog factor
};

VS_out main(in VS_in input)
{
    VS_out output;

    float4 world = float4(tile.w * input.ij.x + tile.x, tile.y, tile.w * input.ij.y + tile.z, 1.0);
    float4 p = mul(combinedMat, world);
    output.Position = p;

    // u along z (j), v along x (i): 0 to 16/17 (or 32/33) over a tile, so the
    // ripples have a seam at every tile edge, as on the Xbox.
    output.Bump = input.ij.yx * grid.x;

    // rcc: the reciprocal, its size held between 5.42e-20 and 1.84e19, so a
    // vertex on the camera's plane gives a large number and not an infinity.
    float w = p.w >= 0.0 ? max(p.w, 5.42101e-20) : min(p.w, -5.42101e-20);
    output.Screen.xy = p.xy / w * float2(0.5, -0.5) + 0.5;

    float oFog = 1.00025 - 0.0025006 * p.w;
    float range = fogData.y - fogData.x;
    output.Screen.z = saturate((fogData.y - oFog) / (range != 0.0 ? range : 1.0 / 8192.0));

    return output;
}
