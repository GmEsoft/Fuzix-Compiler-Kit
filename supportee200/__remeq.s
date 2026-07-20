	.setcpu 4
	.export __remeq
	.export __remeqc
	.export __remequc

	.code

__remeq:
	lda	2(s)
	lda	(a)
	stx	(-s)
	sta	(-s)
	jsr	__rem
	; Removed the word we pushed. Result is in B
	ldx	(s+)
	lda	2(s)
	stb	(a)
	inr	s
	inr	s
	rsr

__remeqc:
	lda	2(s)
	ldab	(a)
	clrb	ah
	orib	al,al
	bp	ispve
	dcrb	ah
ispve:
	stx	(-s)
	sta	(-s)
	jsr	__rem
	ldx	(s+)
	lda	2(s)
	stbb	(a)
	inr	s
	inr	s
	rsr

__remequc:
	lda	2(s)
	ldab	(a)
	clrb	ah
	bra	ispve
