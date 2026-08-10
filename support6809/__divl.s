	.export __divl
	.code

__divl:
	clr	@tmp4		; Sign tracking
	exg	d,y
	bita	#0x80
	beq	nosignfix
	exg	d,y
	lbsr	__negatel
	inc	@tmp4
	exg	d,y
nosignfix:
	sty	,--s
	std	,--s
	;
	;	Now do other argument
	;
	lda	6,s
	bpl	nosignfix2
	inc	@tmp4
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
	tfr	s,x
	lbsr	div32x32
	ldb	@tmp4
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
