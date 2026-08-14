	.export __notl
	.code
__notl:
	cmpy	#0
	bne	false
	subd	#0
	beq	true
false:
	clra
	clrb
	rts
true:
	; D is zero
	incb
	rts
