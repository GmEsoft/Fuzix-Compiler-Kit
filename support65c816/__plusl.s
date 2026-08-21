	.65c816
	.a16
	.i16

	.export __plusl
	.export __plusul	; cpu specific backend can generate this form
__plusl:
__plusul:
	clc
	adc 0,y
	tax
	lda @hireg
	adc 2,y
	sta @hireg
	txa
	rts
