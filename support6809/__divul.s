	.export __divul
	.code
__divul:
	pshs	d
	pshs	cc,a,b,x,y	; trick: push y, reserve 5 byte workspace
	tfr	s,x
	lbsr	div32x32
	ldy	11,x
	ldd	13,x
	ldx	9,x
	leas	15,s		; clean up including work space
	jmp	,x
