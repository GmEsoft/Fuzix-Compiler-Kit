;
;		(TOS) /= HL
;

		.export __diveqc
		.code

__diveqc:
		ex	de,hl
		pop	hl
		ex	(sp),hl
		; Now we are doing (HL) / DE
		push	hl
		ld	l,(hl)
		; We are now doing HL / DE and the address we want is TOS
		call	__sex
		call	__divde
		; Return is in HL
		pop	de
		ld	a,l
		ld	(de),a
		ret
