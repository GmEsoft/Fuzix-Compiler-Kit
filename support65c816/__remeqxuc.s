	.65c816
	.a16
	.i16

	.export __remeqxuc

	; A is ptr X is a value to divide by
__remeqxuc:
	stx @tmp
	tax
	lda #0
	sep #0x20
	lda 0,x
	rep #0x20
	phx
	ldx @tmp
	jsr __remx
	plx
	sep #0x20
	sta 0,x
	rep #0x20
	rts
