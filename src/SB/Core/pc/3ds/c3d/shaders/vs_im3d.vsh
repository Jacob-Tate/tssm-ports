; vs_im3d: the game's RwIm3DVertex as it is. v0 position, v1 float normal (not
; read: im3d::LIGHTING is not drawn lit yet), v2 = b, g, r, a bytes, v3 uv.
; As GL3's im3d.vert: the vertex colour, no material colour.
.entry vs_im3d
.proc vs_im3d
	mov r0.xyz, v0
	mov r0.w, ONE
	call xform
	mul r2, INV255, v2
	mov outclr, r2.zyxw
	call uvout
	end
.end
