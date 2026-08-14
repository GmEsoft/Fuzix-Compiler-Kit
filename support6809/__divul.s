	.export __divul
	.code
__divul:
	pshs	d
	pshs	y
	leas	-5,s		; required workspace
	tfr	s,x
	lbsr	div32x32
	ldy	11,x
	ldd	13,x
	ldx	9,x
	leas	15,s		; clean up including work space
	jmp	,x
