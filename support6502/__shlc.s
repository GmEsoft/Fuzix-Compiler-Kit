;
;	Only used for non constant cases
;
	.export __shlc
	.export __lstmpc
	.export __l_ltltc

__l_ltltc:
	; A << local
	sta	@tmp
	lda	(@sp),y
;	tmp << A
__lstmpc:
	ldy	#0
	and	#7
	beq	nowork
	;  A << X
	tax
	lda	@tmp
loop:
	asl	a
	dex
	bne	loop
	rts
nowork:
	lda	@tmp
	rts
;	TOS << A
__shlc:
	jsr	__poptmpc
	; tmp << A
	jmp	__lstmpc
