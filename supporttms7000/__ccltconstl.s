;
;	r12-r15 v r2-r5
;
;	TODO: ponder instead putting the constant as the words following ?
;
	.export __ccltconstl
	.export __ccltconstul
	.export __ccgteqconstl
	.export __ccgteqconstul
	.export __ccltconstbl
	.export __ccltconstbul
	.export __ccgteqconstbl
	.export __ccgteqconstbul
	.export __ccltconst0l
	.export __ccltconst0ul
	.export __ccgteqconst0l
	.export __ccgteqconst0ul

;	r12-r15 > r0-r3 ?

__ccltconstbul:
	mov b,r13
	jmp ccltconstbu
__ccltconst0ul:
	clr r13
ccltconstbu:
	clr r12
	clr r11
	clr r10
	jmp __ccltconstul
__ccltconstbl:
	mov b,r13
	jmp ccltconstb
__ccltconst0l:
	clr r13
ccltconstb:
	clr r12
	clr r11
	clr r10
__ccltconstl:
	xor r10,r2
	jpz samesign
	xor r10,r2
	jpz true
	jmp false
samesign:
	xor r10,r2
__ccltconstul:
	cmp r2,r10
	jnc true
	jnz false
next:
	cmp r3,r11
	jnc true
	jnz false
	cmp r4,r12
	jnc true
	jnz false
	cmp r5,r13
	jc false		; C is set on equality as well.
true:
	clr r4
	mov %1,r5
	rets
false:
	clr r4
	clr r5
	rets

__ccgteqconstbl:
	mov b,r13
	jmp ccgteqconstb
__ccgteqconst0l:
	clr r13
ccgteqconstb:
	clr r12
	clr r11
	clr r10
__ccgteqconstl:
	call @__ccltconstl
	xor %1,r5
	rets

__ccgteqconstbul:
	mov b,r13
	jmp ccgteqconstbu
__ccgteqconst0ul:
	clr r13
ccgteqconstbu:
	clr r12
	clr r11
	clr r10
__ccgteqconstul:
	call @__ccltconstul
	xor %1,r5
	rets
