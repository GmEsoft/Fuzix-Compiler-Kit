	.setcpu 4
	.export __diveq
	.export __diveqc
	.export __divequc

	.code

__diveq:
	lda	2(s)
	lda	(a)
	stx	(-s)
	sta	(-s)
	jsr	__div
	; Removed the word we pushed. Result is in B
	ldx	(s+)
	lda	2(s)
	stb	(a)
	inr	s
	inr	s
	rsr

;	(TOS) / B
__diveqc:
	lda	2(s)		; pointer
	ldab	(a)		; get low half
	clrb	ah		; clear high half
	orib	al,al		; sign extend ?
	bp	ispve
	dcrb	ah
ispve:
	stx	(-s)
	sta	(-s)
	jsr	__div
	ldx	(s+)
	lda	2(s)
	stbb	(a)
	inr	s
	inr	s
	rsr

;	(TOS) / B
;	Byte sized operations extend to integer by C promotion rules
;	so end up being computed as signed divisons
;
__divequc:
	lda	2(s)		; pointer
	ldab	(a)		; get low half
	clrb	ah		; clear high half
	bra	ispve
