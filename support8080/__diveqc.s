;
;		(TOS) /= L
;

			.export __diveqc
			.export __divequc
			.setcpu 8080
			.code
__divequc:
	xchg
	pop	h
	xthl
	; Now we are doing (HL) / DE
	push	h
	mov	l,m
	mvi	h,0
	jmp	dodiv
__diveqc:
	xchg
	pop	h
	xthl
	; Now we are doing (HL) / DE
	push	h
	mov	l,m
	call	__sex
dodiv:
	; We are now doing HL / DE and the address we want is TOS
	call __divdeu
	; Return is in HL
	pop	d
	mov	a,l
	stax	d
	ret
