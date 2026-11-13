; vs_env: vs_lit plus a MatFX environment map: texcoord1 from the
; object-space normal through u_envMat (MatFX::setupEnv's matrix with the
; world's normal matrix folded in).
.out outtc1 texcoord1
.entry vs_env
.proc vs_env
	mov r0.xyz, v0
	mov r0.w, ONE
	call xform
	mov r6, u_surf
	mul r3.xyz, r6.wwww, v1
	call envout
	mul r1, INV255, v2
	call light
	call uvout
	end
.end

; envout: r3.xyz = object-space normal -> outtc1 (all four parts).
.proc envout
	mov r3.w, ONE
	dp4 outtc1.x, u_envMat[0], r3
	dp4 outtc1.y, u_envMat[1], r3
	mov outtc1.zw, ZERO
.end
