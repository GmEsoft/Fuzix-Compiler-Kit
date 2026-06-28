;
;	Right shift TOS left by HL
;
	.export __shreqc
	.setcpu 8080
	.code

__shreqc:
	call	__popeq
	mov	a,e
	ani	15
	mov	e,a
	mov	a,m
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
	jmp	__reteq


