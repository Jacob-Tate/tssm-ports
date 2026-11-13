; vs_skinenv: vs_skin plus the environment map of vs_env.
.out outtc1 texcoord1
.entry vs_skinenv
.proc vs_skinenv
	call skin
	call xform
	call envout
	mul r1, INV255, v2
	call light
	call uvout
	end
.end
