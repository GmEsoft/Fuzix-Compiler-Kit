	.export __streg4y

__streg4y:
	sta (@reg4),y
	iny
	txa
	sta (@reg4),y
	rts
