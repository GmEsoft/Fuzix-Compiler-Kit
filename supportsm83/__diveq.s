;
;	Divison /= %= operators
;
;	(TOS) /= HL etc
;

	.export __divequ
	.export __remequ
	.export __diveq
	.export __remeq
	.export __divequc
	.export __remequc
	.export __diveqc
	.export __remeqc

__divequ:
	call	__eqprep
	; HL is now the pointer
	push	hl
	push	de
	ld	e,(hl)
	inc	hl
	ld	d,(hl)
	pop	hl
	call	__divdehl
	; result is in DE
	jp	__eqpopout

__remequ:
	call	__eqprep
	; HL is now the pointer
	push	hl
	push	de
	ld	e,(hl)
	inc	hl
	ld	d,(hl)
	pop	hl
	call	__divdehl
	; result is in HL
	jp	__eqpopouthl

__diveq:
	call	__eqprep
	push	hl
	push	de
	ld	e,(hl)
	inc	hl
	ld	d,(hl)
	pop	hl
	call	__div2opcon
	jp	__eqpopout

__remeq:
	call	__eqprep
	push	hl
	push	de
	ld	e,(hl)
	inc	hl
	ld	d,(hl)
	pop	hl
	call	__rem2opcon
	jp	__eqpopout

__divequc:
	call	__eqprep
	push	hl
	push	de
	ld	e,(hl)
	ld	d,0
	pop	hl
	call	__div2opcon
	ld	a,e
	jp	__eqpopoutc

__remequc:
	call	__eqprep
	push	hl
	push	de
	ld	e,(hl)
	ld	d,0
	pop	hl
	call	__rem2opcon
	ld	a,e
	jp	__eqpopoutc

__diveqc:
	call	__eqprep
	push	hl
	push	de
	ld	e,(hl)
	ld	d,0
	bit	7,e
	jr	z,nosex
	dec	d
nosex:
	pop	hl
	call	__div2opcon
	ld	a,e
	jp	__eqpopoutc

__remeqc:
	call	__eqprep
	push	hl
	push	de
	ld	e,(hl)
	ld	d,0
	bit	7,e
	jr	z,nosex2
	dec	d
nosex2:
	pop	hl
	call	__rem2opcon
	ld	a,e
	jp	__eqpopoutc
