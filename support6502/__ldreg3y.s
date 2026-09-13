	.export __ldreg3y

__ldreg3y:
	lda (@reg3),y
	tax
	dey
	lda (@reg3),y
	rts
