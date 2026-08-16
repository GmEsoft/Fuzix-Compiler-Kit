;
;	6809 comparison conversions. These are much easier as we have a
;	16biit comparison and load.
;

	.code

	.export booleq
	.export boolne
	.export boolle
	.export boollt
	.export boolge
	.export boolgt
	.export boolule
	.export boolult
	.export booluge
	.export boolugt
	.export booleq0
	.export boolne0
	.export boolle0
	.export boollt0
	.export boolge0
	.export boolgt0
	.export boolule0
	.export boolult0
	.export booluge0
	.export boolugt0

	.export __bool
	.export __boolc
	.export __not
	.export __notc

;
;	Turn  val op test into 1 for true 0 for false. Ensure the Z flag
;	is appropriately set
;

__notc:
	tstb
	bra	booleq
__not:
booleq0:
	subd	#0
booleq:
	bne	ret0
ret1:
	ldd	#1
	rts

__boolc:
	tstb
	bra	boolne
__bool:
boolne0:
	subd	#0
boolne:
	bne	ret1
ret0:
	clra
	clrb
	rts

boollt0:
	subd	#0
	bra	boollt
boolle0:
	subd	#0
boolle:
	beq	ret1
boollt:
	blt	ret1
	clra
	clrb
	rts

boolgt0:
	subd	#0
	bra	boolgt
boolge0:
	subd	#0
boolge:
	beq	ret1
boolgt:	bgt	ret1
	clra
	clrb
	rts

booluge0:
	subd	#0
	bra	booluge
boolugt0:
	subd	#0
boolugt:
	beq	ret0
booluge:
	bcc	ret1
	clra
	clrb
	rts

boolult0:
	subd	#0
	bra	boolult
boolule0:
	subd	#0
boolule:
	beq	ret1
boolult:			; use C flag
	ldd	#0
	rolb
	rts
