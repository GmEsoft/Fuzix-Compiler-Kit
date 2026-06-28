;
;	Right shift TOS left by HL
;
			.export __shreqc
			.setcpu 8080
			.code
__shreqc:
	xchg
	pop	h
	xthl
	mov	a,e
	ani	15		; rolling sign into low bits
	mov	e,a		; is allowed but a corner case
	mov	a,m		; so don't optimize
	jz	noop
	ora	a
loop:
	ora	a
	jp	pve
	stc
pve:
	rar
	dcr	e
	jnz	loop
	mov	m,a
noop:
	mov	l,a
	ret


