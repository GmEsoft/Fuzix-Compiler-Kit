		.export __muldeb
		.export __mulde
		.export __mulde0d
		.export __mul

		.code
;
;	DE * HL
;
__mul:
	ex	de,hl
	pop	hl
	ex	(sp),hl
	; full 16x16, DE holds the right operand
__mulde:
	push	bc
	ld	b,h		; copy value over
	ld	c,l
	ld	hl,0
	ld	a,b		; upper half of work in A for speed
	ld	b,16
loop:
	add	hl,hl		; shift result
	rl	c
	rla
	jr	nc, noset	; not a 1 bit in this column
	add	hl,de		; add in the other half
noset:	djnz	loop
	; result is in HL
	pop	bc
	ret

__muldeb:
	ld	h,0
	jr	__mulde
;
;		HL * DE with only the low half of the right operand
;
__mulde0d:
	ld	d,0
	jr	__mulde
