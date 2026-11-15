; vs_skin: a skinned model, lit as vs_lit. Its four bone matrices are blended
; first and applied once. v0 position, v1 normal bytes, v2 colour bytes, v3 uv,
; v4 four palette slots x 3 (bytes: the first of each bone's three rows in
; u_bones), v5 four weights (bytes, summing to 255).
.entry vs_skin
.proc vs_skin
	ifu b_trim
		call skin1
		call xform
		mul r1, INV255, v2
		call light1
		call uvout1
	.else
		call skin
		call xform
		mul r1, INV255, v2
		call light
		call uvout
	.end
	end
.end

; skin1 (skintrim): skin's result, shorter. The other three bones only when a
; weight past the first is over 0 (a vertex of one bone adds 0 times each of
; them: nothing), and the position through dph (v0.xyz with a w of 1).
; Leaves r6 = u_surf for light1. Uses r7-r13.
.proc skin1
	mul r10, INV255, v5
	mova a0.xy, v4.xy
	mul r11, u_bones[a0.x+0], r10.xxxx
	mul r12, u_bones[a0.x+1], r10.xxxx
	mul r13, u_bones[a0.x+2], r10.xxxx
	dp3 r7.x, ONE, r10.yzww
	cmp ZERO, ne, ne, r7.xxxx
	ifc cmp.x
		mad r11, r10.yyyy, u_bones[a0.y+0], r11
		mad r12, r10.yyyy, u_bones[a0.y+1], r12
		mad r13, r10.yyyy, u_bones[a0.y+2], r13
		mova a0.xy, v4.zw
		mad r11, r10.zzzz, u_bones[a0.x+0], r11
		mad r12, r10.zzzz, u_bones[a0.x+1], r12
		mad r13, r10.zzzz, u_bones[a0.x+2], r13
		mad r11, r10.wwww, u_bones[a0.y+0], r11
		mad r12, r10.wwww, u_bones[a0.y+1], r12
		mad r13, r10.wwww, u_bones[a0.y+2], r13
	.end
	dph r0.x, v0, r11
	dph r0.y, v0, r12
	dph r0.z, v0, r13
	mov r0.w, ONE
	mov r6, u_surf
	mul r8.xyz, r6.wwww, v1
	dp3 r3.x, r11, r8
	dp3 r3.y, r12, r8
	dp3 r3.z, r13, r8
.end

; skin: -> r0 the skinned position (w = 1), r3 the skinned normal (w free).
; Uses r8-r13.
.proc skin
	mul r10, INV255, v5
	mova a0.xy, v4.xy
	mul r11, u_bones[a0.x+0], r10.xxxx
	mul r12, u_bones[a0.x+1], r10.xxxx
	mul r13, u_bones[a0.x+2], r10.xxxx
	mad r11, r10.yyyy, u_bones[a0.y+0], r11
	mad r12, r10.yyyy, u_bones[a0.y+1], r12
	mad r13, r10.yyyy, u_bones[a0.y+2], r13
	mova a0.xy, v4.zw
	mad r11, r10.zzzz, u_bones[a0.x+0], r11
	mad r12, r10.zzzz, u_bones[a0.x+1], r12
	mad r13, r10.zzzz, u_bones[a0.x+2], r13
	mad r11, r10.wwww, u_bones[a0.y+0], r11
	mad r12, r10.wwww, u_bones[a0.y+1], r12
	mad r13, r10.wwww, u_bones[a0.y+2], r13
	mov r9.xyz, v0
	mov r9.w, ONE
	dp4 r0.x, r11, r9
	dp4 r0.y, r12, r9
	dp4 r0.z, r13, r9
	mov r0.w, ONE
	mov r6, u_surf
	mul r8.xyz, r6.wwww, v1
	dp3 r3.x, r11, r8
	dp3 r3.y, r12, r8
	dp3 r3.z, r13, r8
.end
