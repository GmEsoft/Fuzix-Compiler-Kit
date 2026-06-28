;
;	(TOS) << HL
;
	.export __shreqc

__shreqc:
	call	__eqprepc
	push	hl
	and	15		; count
	ld	e,a
	ld	a,(hl)		; working value
	jr	z, nowork
loop:	sra	a
	dec	e
	jr	nz,loop
nowork:
	jp	__eqpopoutc

