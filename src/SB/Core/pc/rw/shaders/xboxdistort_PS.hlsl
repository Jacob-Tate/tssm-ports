// The Sonic Wave Guitar's screen distortion (iXboxDistort.h): the frame read
// at an offset the swirl map gives.
//
// The Xbox's pixel shader (DEFAULT.XBE 0x2cbcf0) is a texture-addressing
// description, not code: stage 0 reads the map, stages 1 and 2 are dot
// products of their coordinates with that texel expanded to -1..1
// (PS_DOTMAPPING_MINUS1_TO_1_D3D), and the frame is read at the two results.
// The coordinates are (A sin t, A cos t, x) and (A cos t, -A sin t, y), and the
// map's blue is 255 everywhere, so the read is at (x, y) plus the offset below.
//
// Interface is librw's im2d vertex shader. Fog and the vertex colour are not
// used: the pass overwrites the frame (ONE/ZERO, white).

#include "rwshader.h"

struct VS_out
{
    float4 Position  : SV_POSITION;
    float3 TexCoord0 : TEXCOORD0;
    float4 Color     : COLOR0;
};

// Stage 0 is the copy of the frame (the one librw's render state binds), stage
// 1 the swirl map.
RW_TEXTURE(screen, 0);
RW_TEXTURE(swirl, 1);

// (A sin t / 640, A cos t / 480, A cos t / 640, -A sin t / 480): the offset is
// red times xy plus green times zw, in shares of the picture. c1 because librw
// keeps c0 for the fog colour.
float4 displace : register(c1);

float4 main(VS_out input) : SV_Target
{
    float2 uv = input.TexCoord0.xy;

    // The quad's map coordinates run from 0.25 to 0.75: the middle half.
    float2 d = RW_SAMPLE(swirl, uv * 0.5f + 0.25f).rg * 2.0f - 1.0f;

    float4 c = RW_SAMPLE(screen, uv + d.x * displace.xy + d.y * displace.zw);
    RW_ALPHA_TEST(c.a);
    return c;
}
