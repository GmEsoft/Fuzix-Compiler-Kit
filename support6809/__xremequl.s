;
;	,X over Y:D
;
	.export __xremequl
	.code
__xremequl:
	leas	-15,s		; Make space for the framr expected
	std	7,s
	sty	5,s
	ldd	,x
	std	11,s
	ldd	2,x
	std	13,s
	stx	,--s
	leax	2,s
	lbsr	div32x32
	ldy	1,x
	ldd	3,x
	ldx	,s++
	sty	,x
	std	2,x

	leas	15,s
	rts
