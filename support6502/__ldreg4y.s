	.export __ldreg4y

__ldreg4y:
	lda (@reg4),y
	tax
	dey
	lda (@reg4),y
	rts
