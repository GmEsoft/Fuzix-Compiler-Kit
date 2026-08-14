	.export __switchl

__switchl:
	; X holds the switch table, Y:D the value
	; Juggle as we are short of regs here - TODO find a nicer approach
	pshs d,u		; trick: reserve 2 bytes; save D
	ldd ,x++		; Get count
	std 2,s			; Save count in reserved space
	puls d			; Recover D
	inc 1,s
	bra moveon
next:
	cmpy ,x++
	bne bump
	cmpd ,x++
	beq gotit
	leax 2,x
moveon:
	dec 1,s			; We know < 256 entries per switch
	bne next
gotit:
	ldx ,x
	leas 2,s
	jmp ,x
bump:	leax 4,x
	bra moveon
