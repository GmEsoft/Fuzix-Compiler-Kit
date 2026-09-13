	.export __ldreg1y

__ldreg1y:
	lda (@reg1),y
	tax
	dey
	lda (@reg1),y
	rts
