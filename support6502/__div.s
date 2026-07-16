	.export __div
	.export __divtmp
	.export __rem
	.export __remtmp
	.export __diveq
	.export __remeq
	.export __diveqc
	.export __remeqc
	.export __divequc
	.export __remequc
	.export __l_div
	.export __l_rem

;
;	 XA/local
;
__l_div:
	jsr	__ytmp
	jmp	__divtmp
;
;	TOS / XA
;
__div:
	jsr	__poptmp
	; Now @tmp/XA
__divtmp:
	; Y is used to count sign swaps
	ldy	#0
	jsr	neg
	; Ensure XA is positive
	sta	@tmp1
	stx	@tmp1+1
	; Save XA into @tmp1
	lda	@tmp
	ldx	@tmp+1
	; Get the dividend
	jsr	neg
	sta	@tmp
	stx	@tmp+1
	; Again fix the sign up and track in Y
	sty	@tmp3
	; Do the unsigned divide
	jsr	__dodivu
	;	Result is now in XA
	ror	@tmp3
	bcc	ispve
	;	Fix the resulting sign up
	jsr	doneg
ispve:	rts

__l_rem:
	jsr	__ytmp
	jmp	__remtmp
__rem:
	jsr	__poptmp
	; Now @tmp % XA
__remtmp:
	; Fix up the sign of XA
	jsr	neg
	; Save XA
	sta	@tmp1
	stx	@tmp1+1
	; Get the divisor and sort out the sign with tracking
	lda	@tmp
	ldx	@tmp+1
	ldy	#0
	jsr	neg
	sta	@tmp
	stx	@tmp+1
	sty	@tmp3
	; Do the unsigned maths
	jsr	__dodivu
	;	Result is now in @tmp2
	ldx	@tmp2+1
	lda	@tmp2
	ror	@tmp3
	bcc	ispve
	jmp	doneg

;
;	Turn a value positive if needed. Count sign changes in Y
;
neg:
	cpx	#0x00
	bpl	nowork
	iny
doneg:
	clc
	eor	#0xFF
	adc	#1
	pha
	txa
	eor	#0xFF
	adc	#0
	tax
	pla
nowork:
	rts

;
;	(TOS) / XA
;
__diveq:
	jsr	__eqget			; @tmp is the value
	jsr	__divtmp
	jmp	__eqput			; pop TOS into @tmp,
					; write (XA) back into TOS
					; return result in XA
;
;	(TOS) % XA
;
__remeq:
	jsr	__eqget			; @tmp is the value
	jsr	__remtmp
	jmp	__eqput			; pop TOS into @tmp,
					; write (XA) back into TOS
					; return result in XA

;
;	These are slow but almost never used
;

;
;	(TOS) / XA
;
__diveqc:
	jsr	__eqgetcs		; @tmp is the value
	jsr	__divtmp
	jsr	__poptmp
	sta	(@tmp),y
	rts
;
;	(TOS) % XA
;
__remeqc:
	jsr	__eqgetcs		; @tmp is the value
	jsr	__remtmp
	jsr	__poptmp
	sta	(@tmp),y
	rts

__divequc:
	jsr	__eqgetc		; @tmp is the value
	jsr	__divtmp
	jsr	__poptmp
	sta	(@tmp),y
	rts
;
;	(TOS) % XA
;
__remequc:
	jsr	__eqgetc		; @tmp is the value
	jsr	__remtmp
	jsr	__poptmp
	sta	(@tmp),y
	rts

sex:
	ldx	#0
	cmp	#0x80
	bcc	noneg
	dex
noneg:	rts

__eqgetcs:
	jsr	__eqgetc
	pha
	lda	@tmp			; @tmp+1 is currenty zero
	bpl	nosex
	dec	@tmp+1			; upper to $FF
nosex:	pla
	rts
