;
;	D = ,X / D signed
;
;	The rules for signed divide are
;	Dividend and remainder have the same sign
;	Quotient is negative if signs disagree
;
;	So we do the maths in unsigned then fix up
;
	.export __xdiveqc
	.export __xremeqc
	; promotion rules mean the maths is done int anyway
	.export __xdivequc
	.export __xremequc
;
;	The sign of the remainder of a division is not defined until C99.
;	C99 says it's the sign of the dividend.
;
__xremequc:
	stx ,--s		; save pointer
	ldy #0
	bsr absd
	ldx -1,x		; so the byte we want ends up low
	exg d,x
	clra
dorem:
	exg d,x
	lbsr div16x16		; do the unsigned divide
store:
	ldx ,s++
	stb ,x
	rts
	
__xremeqc:
	stx ,--s		; save pointer
	ldy #0
	bsr absd
	ldx -1,x		; so the byte we want ends up low
	exg d,x
	sex
	bita #0x80
	beq dorem
negmod:
	bsr negd
	exg d,x
	lbsr div16x16
	negb
	bra store
	

;
;	D = TOS/D signed
;
;	The sign of the result is positive if the inputs have the same
;	sign, otherwise negative
;
__xdivequc:
	stx, --s
	ldy #0			; Count number of sign changes
	ldx -1,x		; Data value
	bsr absd
	exg d,x
	clra
	bra dodiv
__xdiveqc:
	stx, --s
	ldy #0			; Count number of sign changes
	ldx -1,x		; Data value
	bsr absd
	exg d,x
	sex
	bsr absd
dodiv:
	exg d,x
	lbsr div16x16		; do the maths
				; X = quotient, D = remainder
	tfr x,d
	cmpy #1
	bne store		; low bit set -> negate
	negb
	bra store
	
absd:
	bita #$80
	beq ispos
	leay 1,y		; count sign changes in Y
negd:
	subd #1			; negate d
	coma
	comb
ispos:
	rts
