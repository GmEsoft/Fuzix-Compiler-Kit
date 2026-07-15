;
;	Compare A with @tmp
;
	.export __netmpc
	.export __netmpuc
	.export __l_netmpc
	.export __l_netmpuc
	.export __ccnec
	.export __ccneuc
	.export __nexa

__l_netmpc:
__l_netmpuc:
	jsr __ytmpc
	jmp __netmpc
__ccnec:
__ccneuc:
	jsr __poptmpc
__netmpc:
__netmpuc:
	ldx	#0
	cmp	@tmp
eqbool:
	beq	false	; already 0
true:	lda	#1
	rts
false:	txa
	rts
__nexa:	stx	@tmp2	; so we don't mash @tmp for better code
	ldx	#0
	cmp	@tmp2
	bne	true
	txa
	rts

