		.export __bcshr
		.export __bcshrc
		.export __bcshru
		.export __bcshruc
		.code

__bcshrc:
		ld	l,c
		call	__castc_
		jr	doshft
__bcshr:
		; shift BC right by E
		ld	l,c
		ld	h,b
doshft:
		call	__shrde
		ld	c,l
		ld	b,h
		ret

__bcshruc:
		ld	b,0
__bcshru:
		; shift BC right by E
		ld	l,c
		ld	h,b
		call	__shrdeu
		ld	c,l
		ld	b,h
		ret
