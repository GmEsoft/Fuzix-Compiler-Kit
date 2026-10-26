;
;	For 8080 with RST 8085 style experiment
;
	.code
	.export __ldsi
	.export __ldhi
	.export __lhlx
	.export __shlx
	.export __dsub
	.export __arhl
	.export __ldsilhlx
	.export __ldsishlx

__ldsi:			; DE = SP + n8  n8 in E
	mvi	d,0
	xchg
	dad	sp
	xchg
	ret
__ldhi:			; DE = HL + n8 n8 in E
	xchg
	mvi	h,0
	dad	d
	xchg
	ret
__ldsilhlx:
	call	__ldsi
__lhlx:			; HL = (DE)
	xchg
	mov	e,m
	inx	h
	mov	d,m
	inx	h
	xchg
	ret
__ldsishlx:
	call	__ldsi
__shlx:			; (DE) = HL
	xchg
	mov	m,e
	inx	h
	mov	m,d
	inx	h
	xchg
	ret
__dsub:			; doesnt get flags correct but do we care ?
	mov	a,l
	sub	c
	mov	l,a
	mov	a,h
	sbb	b
	mov	h,a
	ret
__arhl:
	; Rotate HL right. We don't do flags but that is fine
	push	psw
	mov	a,h
	add	a	; carry is now top bit of input
	mov	a,h
	rar		; rotate and add in sign bit
	mov	h,a
	mov	a,l
	rar
	mov	l,a
	pop	psw
	ret
