	.export __reml
	.code

__reml:
	cmpy	#0
	bpl	nosignfix
	lbsr	__negatel
nosignfix:
	pshs	d
	pshs	y
	;
	;	Sign check
	;
	ldx	#0
	lda	6,s
	bpl	nosignfix2
	ldx	#1
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
	leas	-5,s
	stx	,--s
	leax	2,s
	lbsr	div32x32
	;
	;	Get the result
	;
	ldx	,s++
	beq	done
	ldy	1,s
	ldd	3,s
	lbsr	__negatel
pop4:
	ldx	9,s
	leas	15,s
	jmp	,x
done:
	ldy	1,s
	ldd	3,s
	bra	pop4
