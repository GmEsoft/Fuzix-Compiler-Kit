	.export __streg2y

__streg2y:
	sta (@reg2),y
	iny
	txa
	sta (@reg2),y
	rts
