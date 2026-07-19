	.export __div
	.export __rem
	.export __diveq
	.export __remeq
	.export __diveqc
	.export __remeqc
	.export __divequc
	.export __remequc

	.code

__div:
	pop r12
	pop r13
	pop r0
	pop r1
	push r13
	push r12
__dodivs:
	clr r12			; sign info
	tcm r0,#0x80		; negative ?
	jr nz, uns1
	inc r12
	com r0
	com r1
	incw r0			; negate
uns1:	tcm r2,#0x80
	jr nz, uns2
	inc r12
	com r2
	com r3
	incw r2
uns2:
	push r12		; remember sign info
	; Now we can do the divide unsigned
	call __div16x16
	pop r12
	rr r12
	jr nc, sign_ok
	com r0
	com r1
	incw r0
sign_ok:
	ld r2,r0
	ld r3,r1
	ret

__rem:
	pop r12
	pop r13
	pop r0
	pop r1
	push r13
	push r12
__dorems:
	clr r12			; sign info
	tcm r0,#0x80		; negative ?
	jr nz, uns3
	inc r12
	com r0
	com r1
	incw r0			; negate
uns3:	tcm r2,#0x80
	jr nz, uns4
	com r2
	com r3
	incw r2
uns4:
	push r12		; remember sign info
	; Now we can do the divide unsigned
	call __div16x16
	ld r2,r12
	ld r3,r13
	pop r12
	rr r12
	jr nc, sign_ok_r
	com r2
	com r3
	incw r2
sign_ok_r:
	ret

__diveq:
	pop r12		; Get arg off stack
	pop r13
	pop r14
	pop r15
	push r13
	push r12
	lde r0,@rr14
	incw rr14
	lde r1,@rr14
	push r15
	push r14
	call __dodivs	; do the division
store:
	pop r14
	pop r15
	lde @rr14,r3
	decw rr14
	lde @rr14,r2
	ret

__diveqc:
	pop r12		; Get arg off stack
	pop r13
	pop r14
	pop r15
	push r13
	push r12
	lde r1,@rr14
	call sex
dodivc:
	push r15
	push r14
	call __dodivs	; do the division
storec:
	pop r14
	pop r15
	clr r2
	lde @rr14,r3
	ret

__divequc:
	pop r12		; Get arg off stack
	pop r13
	pop r14
	pop r15
	push r13
	push r12
	lde r1,@rr14
	clr r0
	jr dodivc

__remeq:
	pop r12		; Get arg off stack
	pop r13
	pop r14
	pop r15
	push r13
	push r12
	lde r0,@rr14
	incw rr14
	lde r1,@rr14
	push r15
	push r14
	call __dorems	; do the division
	jr store

__remeqc:
	pop r12		; Get arg off stack
	pop r13
	pop r14
	pop r15
	push r13
	push r12
	lde r1,@rr14
	call sex
doremc:
	push r15
	push r14
	call __dorems	; do the division
	jr storec

__remequc:
	pop r12		; Get arg off stack
	pop r13
	pop r14
	pop r15
	push r13
	push r12
	lde r1,@rr14
	clr r0
	jr doremc

sex:
	; Sign extend r1 and r3 into r0 and r2 respectively
	clr r0
	clr r2
	or r1,r1
	jr pl, pos1
	ld r0,#255
pos1:	or r3,r3
	jr pl, pos2
	ld r2,#255
pos2:	ret
