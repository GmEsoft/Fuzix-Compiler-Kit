	.setcpu 4
	.export __remequ

	.code

__remequ:
	lda	2(s)
	lda	(a)
	stx	(-s)
	sta	(-s)
	jsr	__remu
	; Removed the word we pushed. Result is in B
	ldx	(s+)
	lda	2(s)
	stb	(a)
	inr	s
	inr	s
	rsr
