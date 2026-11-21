const char *water_frag_src =
"// The Xbox release's water surface, pixel stage, GL3 and GLES. What it computes\n"
"// and where it comes from are in shaders/water_PS.hlsl; this is the same\n"
"// arithmetic.\n"

"// Unit 0 the bump frame, unit 1 the refraction target, unit 4 the reflection\n"
"// target (rw/xboxwater.cpp binds it there: units 2 and 3 hold the shadow map and\n"
"// the toon ramp for the whole frame, header.frag).\n"
"uniform sampler2D tex0;\n"
"uniform sampler2D tex1;\n"
"uniform sampler2D u_waterReflect;\n"

"// [2]: the tint; [3], [4]: the refraction's and the reflection's bump matrix\n"
"// as (m00, m01, m10, m11), scaled for the tile's distance.\n"
"uniform vec4 u_water[5];\n"

"FSIN vec2 v_bump;\n"
"FSIN vec3 v_screen;\n"

"// Every texture librw's GL3 backend holds has the top of its picture at v = 1\n"
"// (rasterFromImage stores the rows bottom up, and a camera's target is drawn\n"
"// that way up), so a coordinate worked out D3D's way is turned over to read it.\n"
"vec4\n"
"Read(sampler2D t, vec2 uv)\n"
"{\n"
"	return texture(t, vec2(uv.x, 1.0 - uv.y));\n"
"}\n"

"void\n"
"main(void)\n"
"{\n"
"	vec2 d = Read(tex0, v_bump).rg * (255.0/128.0) - 1.0;\n"

"	vec4 m1 = u_water[3];\n"
"	vec4 m2 = u_water[4];\n"
"	vec2 o1 = vec2(m1.x*d.x + m1.z*d.y, m1.y*d.x + m1.w*d.y);\n"
"	vec2 o2 = vec2(m2.x*d.x + m2.z*d.y, m2.y*d.x + m2.w*d.y);\n"

"	vec4 r = Read(tex1, v_screen.xy + o1);\n"
"	vec4 l = Read(u_waterReflect, v_screen.xy + o2);\n"

"	vec4 tint = u_water[2];\n"
"	vec3 rgb = clamp(tint.rgb*r.rgb + tint.rgb*(tint.a*l.rgb), 0.0, 1.0);\n"
"	rgb = mix(u_fogColor.rgb, rgb, v_screen.z);\n"

"	// No alpha test, as in the Direct3D shader.\n"
"	FRAGCOLOR(vec4(rgb, r.a));\n"
"}\n"
;
