	.export __booll
	.code
__booll:
	cmpy	#0
	bne	true
	subd	#0
	beq	false	; If this passes then D is already zero
true:
	ldd	#0
false:
	rts
