; vs_im2d: the game's RwIm2DVertex as it is. v0 = (x, y, z, w): pixels, screen
; z (0 near .. 1 far, the device's zNear and zFar), camera z; v2 = b, g, r, a
; bytes; v3 = u, v.
;
; As GL3's im2d.vert: pixels to clip x, y through u_scrMat (which also turns
; the picture for the sideways screen), screen z to the PICA's -1 (near) .. 0
; (far), and x, y, z times the camera z, which the hardware divides out again.
.entry vs_im2d
.proc vs_im2d
	mov r0.xy, v0.xy
	mov r0.zw, ONE
	dp4 r1.x, u_scrMat[0], r0
	dp4 r1.y, u_scrMat[1], r0
	add r1.z, MONE, v0.z
	mov r1.w, ONE
	mul outpos, v0.wwww, r1
	; Fog by the camera z, as GL3's im2d.vert: the device divided this
	; draw's w's by their largest, which u_fog.z gives back.
	mov r15, u_fog
	mul r14, r15.zzzz, v0.wwww
	mad outtc2, r14, r15.xxxx, r15.yyyy
	mul r2, INV255, v2
	mov outclr, r2.zyxw
	call uvout
	end
.end
