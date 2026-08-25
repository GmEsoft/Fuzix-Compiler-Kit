	.code ; (at 0x0100)

start:
	lds	#$FDFF
	jsr	_main
	; return and exit (value is in XA)
	stb	$FEFF
	; Write to FEFF terminates

	.export _printint
_printint:
	std	$FEFC
	rts

	.export _printchar
_printchar:
	stb	$FEFE
	rts
