; vs_lit: a lit model (LIGHT flag): prelit colour (or black), the ambient and
; up to four directional lights, clamped, times the material colour.
; v0 position (rebased), v1 normal bytes (signed; u_surf.w scales them to
; units), v2 colour bytes r, g, b, a, v3 uv. A geometry without normals or
; without colours gets a fixed attribute of zero (no light) or black there.
.entry vs_lit
.proc vs_lit
	mov r0.xyz, v0
	mov r0.w, ONE
	call xform
	mov r6, u_surf
	mul r3.xyz, r6.wwww, v1
	mul r1, INV255, v2
	call light
	call uvout
	end
.end
