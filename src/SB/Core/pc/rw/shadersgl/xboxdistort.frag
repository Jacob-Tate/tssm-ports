// The Sonic Wave Guitar's screen distortion, GL3. What it computes is in
// shaders/xboxdistort_PS.hlsl.

// Stage 0 is the copy of the frame, stage 1 the swirl map.
uniform sampler2D tex0;
uniform sampler2D tex1;

// (A sin t / 640, A cos t / 480, A cos t / 640, -A sin t / 480).
uniform vec4 u_xboxDistort;

FSIN vec4 v_color;
FSIN vec2 v_tex0;
FSIN float v_fog;

void
main(void)
{
	// Every texture is stored with the top of its picture at v = 1 here (librw's
	// own im2d fragment shader turns v over; glow_bright.frag), the map and the
	// copy of the frame alike, so both reads turn v over, and with it the offset
	// along v.
	vec2 map = v_tex0*0.5 + 0.25;
	vec2 d = texture(tex1, vec2(map.x, 1.0 - map.y)).rg*2.0 - 1.0;

	vec2 off = d.x*u_xboxDistort.xy + d.y*u_xboxDistort.zw;
	vec2 uv = vec2(v_tex0.x + off.x, 1.0 - (v_tex0.y + off.y));

	vec4 color = texture(tex0, uv);
	DoAlphaTest(color.a);
	FRAGCOLOR(color);
}
