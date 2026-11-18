// The Xbox release's water surface, pixel stage, GL3 and GLES. What it computes
// and where it comes from are in shaders/water_PS.hlsl; this is the same
// arithmetic.

// Unit 0 the bump frame, unit 1 the refraction target, unit 4 the reflection
// target (rw/xboxwater.cpp binds it there: units 2 and 3 hold the shadow map and
// the toon ramp for the whole frame, header.frag).
uniform sampler2D tex0;
uniform sampler2D tex1;
uniform sampler2D u_waterReflect;

// [2]: the tint; [3], [4]: the refraction's and the reflection's bump matrix
// as (m00, m01, m10, m11), scaled for the tile's distance.
uniform vec4 u_water[5];

FSIN vec2 v_bump;
FSIN vec3 v_screen;

// Every texture librw's GL3 backend holds has the top of its picture at v = 1
// (rasterFromImage stores the rows bottom up, and a camera's target is drawn
// that way up), so a coordinate worked out D3D's way is turned over to read it.
vec4
Read(sampler2D t, vec2 uv)
{
	return texture(t, vec2(uv.x, 1.0 - uv.y));
}

void
main(void)
{
	vec2 d = Read(tex0, v_bump).rg * (255.0/128.0) - 1.0;

	vec4 m1 = u_water[3];
	vec4 m2 = u_water[4];
	vec2 o1 = vec2(m1.x*d.x + m1.z*d.y, m1.y*d.x + m1.w*d.y);
	vec2 o2 = vec2(m2.x*d.x + m2.z*d.y, m2.y*d.x + m2.w*d.y);

	vec4 r = Read(tex1, v_screen.xy + o1);
	vec4 l = Read(u_waterReflect, v_screen.xy + o2);

	vec4 tint = u_water[2];
	vec3 rgb = clamp(tint.rgb*r.rgb + tint.rgb*(tint.a*l.rgb), 0.0, 1.0);
	rgb = mix(u_fogColor.rgb, rgb, v_screen.z);

	// No alpha test, as in the Direct3D shader.
	FRAGCOLOR(vec4(rgb, r.a));
}
