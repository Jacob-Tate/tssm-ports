// The Xbox release's water surface, vertex stage, GL3 and GLES. What it computes
// and where it comes from are in shaders/water_VS.hlsl; this is the same
// arithmetic.

// u_water[0]: x0, y, z0, cell; u_water[1]: 1 / vertices per side.
// [2] to [4] are the pixel stage's.
uniform vec4 u_water[5];

VSIN(ATTRIB_POS) vec2 in_ij;

VSOUT vec2 v_bump;
VSOUT vec3 v_screen;

void
main(void)
{
	vec4 tile = u_water[0];
	vec4 world = vec4(tile.w*in_ij.x + tile.x, tile.y, tile.w*in_ij.y + tile.z, 1.0);
	vec4 p = u_proj * u_view * u_world * world;
	gl_Position = p;

	v_bump = in_ij.yx * u_water[1].x;

	// D3D's way up (v = 0 at the top), as the Direct3D shader has it; the
	// pixel stage turns it over where it reads.
	float w = p.w >= 0.0 ? max(p.w, 5.42101e-20) : min(p.w, -5.42101e-20);
	v_screen.xy = p.xy / w * vec2(0.5, -0.5) + 0.5;

	// The fog factor: oFog through the scene's linear fog table, from the
	// camera's fog distance to its far plane (u_fogStart, u_fogEnd).
	float oFog = 1.00025 - 0.0025006*p.w;
	float range = u_fogEnd - u_fogStart;
	v_screen.z = clamp((u_fogEnd - oFog)/(range != 0.0 ? range : 1.0/8192.0), 0.0, 1.0);
}
