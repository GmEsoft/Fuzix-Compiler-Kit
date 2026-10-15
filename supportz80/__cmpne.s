		.export __cmpne
		.export __cmpne0d
		.export __cmpnela
		.code
;
;	Tighter version with the other value in DE
;
__cmpne0d:
		ld	d,0
__cmpne:
		or	a
		sbc	hl,de
		ret	z
		jp	__true		; not equal -> 1

; Used by optimizer peephole to squash 8 v 8 compare into cmp a with l
__cmpnela:
		sub	l
		ld	hl,0		; if they were equal false
		ret	z		; Z and 0
		inc	l
		ret			; NZ and 1
