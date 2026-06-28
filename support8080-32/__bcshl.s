		.export __bcshl
		.setcpu 8080
		.code

__bcshl:
		xchg
		; shift BC right by E
		mov	l,c
		mov	h,b
		call	__shlde
		mov	c,l
		mov	b,h
		ret
