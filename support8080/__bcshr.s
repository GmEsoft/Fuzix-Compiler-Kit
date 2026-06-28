		.export __bcshr
		.export __bcshru
		.export __bcshrc
		.export __bcshruc

		.setcpu 8080
		.code

__bcshrc:
		mov	l,c
		call	__castc_
		jmp	doshft
__bcshr:
		; shift BC right by E
		mov	l,c
		mov	h,b
doshft:
		call	__shrde
		mov	c,l
		mov	b,h
		ret

__bcshruc:
		mvi	b,0
__bcshru:
		; shift BC right by E
		mov	l,c
		mov	h,b
		call	__shrdeu
		mov	c,l
		mov	b,h
		ret
