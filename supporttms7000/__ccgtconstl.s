;
;	r12-r15 v r0-r3
;
;	TODO: ponder instead putting the constant as the words following ?
;
	.export __ccgtconstl
	.export __ccgtconstul
	.export __cclteqconstl
	.export __cclteqconstul
	.export __ccgtconstbl
	.export __ccgtconstbul
	.export __cclteqconstbl
	.export __cclteqconstbul
	.export __ccgtconst0l
	.export __ccgtconst0ul
	.export __cclteqconst0l
	.export __cclteqconst0ul

;	r10-r13 > r2-r5 ?

__ccgtconstbl:
	mov b,r13
	jmp ccgtconstb
__ccgtconstbul:
	mov b,r13
	jmp ccgtconstbu
__ccgtconst0ul:
	clr r13
ccgtconstbu:
	clr r12
	clr r11
	clr r10
	jmp __ccgtconstul
__ccgtconst0l:
	clr r13
ccgtconstb:
	clr r12
	clr r11
	clr r10
__ccgtconstl:
	xor r10,r2
	jpz samesign
	xor r10,r2
	jpz false
	jmp true

samesign:
	xor r10,r2
__ccgtconstul:
	cmp r2,r10		; C if r2 > r10
	jnc false
	jnz true		; NZ if r2 < r10
	cmp r3,r11
	jnc false
	jnz true
	cmp r4,r12
	jnc false
	jnz true
	cmp r5,r13
	jnc false
	jnz true
false:
	clr r5
	clr r4
	rets
true:
	clr r4
	mov %1,r5
	rets

__cclteqconstbl:
	mov b,r13
__cclteqconst0l:
	clr r13
cclteqconstb:
	clr r12
	clr r11
	clr r10
__cclteqconstl:
	call @__ccgtconstl
	xor %1,r5
	rets

__cclteqconstbul:
	mov b,r13
	jmp cclteqconstbu
__cclteqconst0ul:
	clr r13
cclteqconstbu:
	clr r12
	clr r11
	clr r10
__cclteqconstul:
	call @__ccgtconstul
	xor %1,r5
	rets
