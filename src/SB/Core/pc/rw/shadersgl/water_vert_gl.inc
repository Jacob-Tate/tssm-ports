const char *water_vert_src =
"// The Xbox release's water surface, vertex stage, GL3 and GLES. What it computes\n"
"// and where it comes from are in shaders/water_VS.hlsl; this is the same\n"
"// arithmetic.\n"

"// u_water[0]: x0, y, z0, cell; u_water[1]: 1 / vertices per side.\n"
"// [2] to [4] are the pixel stage's.\n"
"uniform vec4 u_water[5];\n"

"VSIN(ATTRIB_POS) vec2 in_ij;\n"

"VSOUT vec2 v_bump;\n"
"VSOUT vec3 v_screen;\n"

"void\n"
"main(void)\n"
"{\n"
"	vec4 tile = u_water[0];\n"
"	vec4 world = vec4(tile.w*in_ij.x + tile.x, tile.y, tile.w*in_ij.y + tile.z, 1.0);\n"
"	vec4 p = u_proj * u_view * u_world * world;\n"
"	gl_Position = p;\n"

"	v_bump = in_ij.yx * u_water[1].x;\n"

"	// D3D's way up (v = 0 at the top), as the Direct3D shader has it; the\n"
"	// pixel stage turns it over where it reads.\n"
"	float w = p.w >= 0.0 ? max(p.w, 5.42101e-20) : min(p.w, -5.42101e-20);\n"
"	v_screen.xy = p.xy / w * vec2(0.5, -0.5) + 0.5;\n"

"	// The fog factor: oFog through the scene's linear fog table, from the\n"
"	// camera's fog distance to its far plane (u_fogStart, u_fogEnd).\n"
"	float oFog = 1.00025 - 0.0025006*p.w;\n"
"	float range = u_fogEnd - u_fogStart;\n"
"	v_screen.z = clamp((u_fogEnd - oFog)/(range != 0.0 ? range : 1.0/8192.0), 0.0, 1.0);\n"
"}\n"
;
