;
;	Multiply 3,x by sreg:d
;
;
;	Stack on entry
;
;	3-7,x	Argument
;
;
		.export __mull
		.export __mulul

		.code

__mull:
__mulul:
	leas	-6,s		; working space ,x->5,x
				; moves argument to 6-9,x
	std	0,s
;
;	Do 32bit x low 8bits
;	This gives us a 32bit result plus 8bits discarded
;
	lda	11,s
	ldb	1,s
	mul			; calculate low 8 and overflow
	std	4,s		; fill in low 16bits
	lda	10,s
	ldb	1,s
	mul
	addb	4,s		; next 16 bits plus carry
	adca	#0
	std	3,s		; and save
	lda	9,s
	ldb	1,s
	mul
	addb	3,s		; next 16bits plus carry
	adca	#0
	std	2,s		; and save
	lda	8,s
	ldb	1,s
	mul			; top 8bits (and overflow is lost)
	addb	2,s
	stb	2,s
;
;	Now repeat this with the next 8bits but we only need to do 24bits
;	as the top 8 will overflow
;
	lda	11,s
	ldb	,s
	mul
	addb	4,s		; add in the existing
	adca	3,s
	std	3,s
	bcc	norip16
	inc	2,s		; carry into the top byte
norip16:
	lda	10,s
	ldb	,s	; again
	mul
	addb	3,s
	adca	2,s
	std	2,s
	lda	9,s
	ldb	,s
	mul
	addb	2,s
	stb	2,s	; rest overflows, all of top byte overflows
;
;	Now repeat for the next 8bits but we only need to do 16bit as the
;	top 16 will overflow. Spot a 16bit zero and short cut as this
;	is common (eg for uint * ulong cases)
;
	tfr	y,d	; again (b = sreg + 1)
	beq	is_done
	lda	11,s
	mul
	addb	3,s
	adca	2,s
	std	2,s
	tfr	y,d
	lda	10,s
	mul
	addb	2,s
	stb	2,s	; rest overflows, all of top byte overflows
;
;	And finally the top 8bits so almost everything overflows
;
	tfr	y,d
	beq	is_done
	ldb	11,s
	mul
	addb	2,s
	stb	2,s	; rest overflows, all of top byte overflows
;
;	Into our working register
;
is_done:
	ldd ,s++	; discard
	ldy ,s++
	ldd ,s++
	ldx ,s++
	leas 4,s
	jmp ,x

