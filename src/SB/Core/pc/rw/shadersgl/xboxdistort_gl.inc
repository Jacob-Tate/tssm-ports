const char *xboxdistort_frag_src =
"// The Sonic Wave Guitar's screen distortion, GL3. What it computes is in\n"
"// shaders/xboxdistort_PS.hlsl.\n"

"// Stage 0 is the copy of the frame, stage 1 the swirl map.\n"
"uniform sampler2D tex0;\n"
"uniform sampler2D tex1;\n"

"// (A sin t / 640, A cos t / 480, A cos t / 640, -A sin t / 480).\n"
"uniform vec4 u_xboxDistort;\n"

"FSIN vec4 v_color;\n"
"FSIN vec2 v_tex0;\n"
"FSIN float v_fog;\n"

"void\n"
"main(void)\n"
"{\n"
"	// Every texture is stored with the top of its picture at v = 1 here (librw's\n"
"	// own im2d fragment shader turns v over; glow_bright.frag), the map and the\n"
"	// copy of the frame alike, so both reads turn v over, and with it the offset\n"
"	// along v.\n"
"	vec2 map = v_tex0*0.5 + 0.25;\n"
"	vec2 d = texture(tex1, vec2(map.x, 1.0 - map.y)).rg*2.0 - 1.0;\n"

"	vec2 off = d.x*u_xboxDistort.xy + d.y*u_xboxDistort.zw;\n"
"	vec2 uv = vec2(v_tex0.x + off.x, 1.0 - (v_tex0.y + off.y));\n"

"	vec4 color = texture(tex0, uv);\n"
"	DoAlphaTest(color.a);\n"
"	FRAGCOLOR(color);\n"
"}\n"
;
