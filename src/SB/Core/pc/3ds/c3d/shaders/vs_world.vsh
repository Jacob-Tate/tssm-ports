; vs_world: a model that is not lit (no LIGHT flag): its prelit colour, or the
; black of a model without one (a fixed attribute), times the material colour.
; v0 position (rebased), v2 colour bytes r, g, b, a, v3 uv.
;
; This file also holds the procedures every program calls: picasso links the
; programs into one code blob, so they are written once.
.entry vs_world
.proc vs_world
	mov r0.xyz, v0
	mov r0.w, ONE
	call xform
	mul r1, INV255, v2
	mul outclr, u_matColor, r1
	call uvout
	end
.end

; xform: r0 = object position (w = 1) -> outpos, and outtc2 the fog ramp's
; coordinate from the clip w (GL3's DoFog(gl_Position.w)). Uses r14, r15.
.proc xform
	dp4 outpos.x, u_mvp[0], r0
	dp4 outpos.y, u_mvp[1], r0
	dp4 outpos.z, u_mvp[2], r0
	dp4 r14.w, u_mvp[3], r0
	mov outpos.w, r14.w
	mov r15, u_fog
	mad outtc2, r14.wwww, r15.xxxx, r15.yyyy
.end

; uvout: v3 through u_uvMat -> outtc0.
.proc uvout
	mov r2.xy, v3
	mov r2.zw, ONE
	dp4 outtc0.x, u_uvMat[0], r2
	dp4 outtc0.y, u_uvMat[1], r2
	; Every part of an output must be written: the PICA200 locks up on a
	; partial write (found on a New 3DS; Azahar does not care).
	mov outtc0.zw, ZERO
.end

; light: r1 = base colour (prelit, 0..1), r3 = object-space normal -> outclr.
; GL3's default.vert: clamp(prelit + ambient * surfAmbient
;   + sum(max(0, N . -L) * colour) * surfDiffuse, 0, 1) * material colour.
.proc light
	mov r6, u_surf
	mov r7, u_ambient
	mad r1.xyz, r6.xxxx, r7, r1
	dp3 r4.x, u_lightDir[0], r3
	dp3 r4.y, u_lightDir[1], r3
	dp3 r4.z, u_lightDir[2], r3
	dp3 r4.w, u_lightDir[3], r3
	max r4, ZERO, r4
	mul r5, u_lightCol[0], r4.xxxx
	mad r5, r4.yyyy, u_lightCol[1], r5
	mad r5, r4.zzzz, u_lightCol[2], r5
	mad r5, r4.wwww, u_lightCol[3], r5
	mad r1.xyz, r5, r6.zzzz, r1
	max r1, ZERO, r1
	min r1, ONE, r1
	mul outclr, u_matColor, r1
.end
