	.export __remul
	.code
__remul:
	std	,--s
	sty	,--s
	leas	-5,s
	tfr	s,x
	lbsr	div32x32
	; Result is in 1-4,x
	ldy	1,x
	ldd	3,x
	ldx	9,x
	leas	15,s
	jmp	,x
