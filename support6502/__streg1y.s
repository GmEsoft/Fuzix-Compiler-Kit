	.export __streg1y

__streg1y:
	sta (@reg1),y
	iny
	txa
	sta (@reg1),y
	rts
