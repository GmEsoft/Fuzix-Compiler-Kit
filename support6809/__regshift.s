	.export __reglsl
	.export __regshr
	.export __regshru

	.code

__reglsl:
	clra
	andb	#15
	beq	nowork
	exg	d,u
lsln:
	lslb
	rola
	leau	-1,u
	cmpu	#0
	bne	lsln
	tfr	d,u
	rts

__regshru:
	clra
	andb	#15
	beq	nowork
	exg	d,u
lsrn:
	lsra
	rorb
	leau	-1,u
	cmpu	#0
	bne	lsrn
	tfr	d,u
	rts

__regshr:
	clra
	andb	#15
	beq	nowork
	exg	d,u
asrn:
	asra
	rorb
	leau	-1,u
	cmpu	#0
	bne	asrn
	tfr	d,u
	rts

nowork:
	tfr	u,d		; result may be needed
	rts
