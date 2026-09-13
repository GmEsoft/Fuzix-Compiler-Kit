	.export __ldreg2y

__ldreg2y:
	lda (@reg2),y
	tax
	dey
	lda (@reg2),y
	rts
