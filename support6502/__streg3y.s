	.export __streg3y

__streg3y:
	sta (@reg3),y
	iny
	txa
	sta (@reg3),y
	rts
