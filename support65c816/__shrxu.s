	.65c816
	.a16
	.i16

	.export __shrx
	.export __shrxu

__shrxu:
	; A >> X
	inx
	bra doshft
nextu:	clc
	ror a
doshftu:dex
	bne next
	rts

__shrx:
	; A >> X
	inx
	bra doshft
next:	cmp #0x80
	ror a
doshft:	dex
	bne next
	rts

