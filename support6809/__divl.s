	.export __divl
	.code

__divl:
	ldx	#0		; Sign tracking
	exg	d,y
	bita	#0x80
	beq	nosignfix
	exg	d,y
	lbsr	__negatel
	leax	1,x
	exg	d,y
nosignfix:
	sty	,--s
	std	,--s
	;
	;	Now do other argument
	;
	lda	6,s
	bpl	nosignfix2
	leax	1,x
	ldd	8,s
	subd	#1
	std	8,s
	ldd	6,s
	sbcb	#0
	sbca	#0
	coma
	comb
	std	6,s
	com	8,s
	com	9,s
nosignfix2:
	leas	-5,s		; make room for workspace
	stx	,--s		; save sign info
	leax	2,s		; point past sign info
	lbsr	div32x32
	ldd	,s++
	rorb
	bcc	nosignfix3
	ldy	11,s
	ldd	13,s
	lbsr	__negatel
out:
	ldx	9,s
	leas	15,s
	jmp	,x
nosignfix3:
	ldy	11,s
	ldd	13,s
	bra	out
