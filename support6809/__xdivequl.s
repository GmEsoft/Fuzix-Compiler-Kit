;
;	,X over Y:D
;
	.export __xdivequl
	.code
__xdivequl:
	leas	-15,s		; Make space for the frame expected
	std	7,s
	sty	5,s
	ldd	,x
	std	11,s
	ldd	2,x
	std	13,s
	stx	,--s
	leax	2,s
	lbsr	div32x32
	ldy	11,x
	ldd	13,x
	ldx	,s++
	sty	,x
	std	2,x

	leas	15,s
	rts
