	.export __cleanup
	.export __cleanupb
	.export __cleanup_0
	.export __cleanup_1
	.export __cleanup_2
	.export __cleanup_3
	.export __cleanup_4
	.export __cleanup_5
	.export __cleanup_6
	.export __cleanup_7
	.export __cleanup_8
	.export __cleanupa_0
	.export __cleanupa_1
	.export __cleanupa_2
	.export __cleanupa_3
	.export __cleanupa_4
	.export __cleanupa_5
	.export __cleanupa_6
	.export __cleanupa_7
	.export __cleanupa_8

	.code

__cleanupa_8:
	mov	%9,b
	jmp	__cleanupba
__cleanupa_7:
	mov	%8,b
	jmp	__cleanupba
__cleanupa_6:
	mov	%7,b
	jmp	__cleanupba
__cleanupa_5:
	mov	%6,b
	jmp	__cleanupba
__cleanupa_4:			; 4 is common, 3 is not
	mov	%5,b
	jmp	__cleanupba
__cleanupa_3:
	mov	%4,b
	jmp	__cleanupba
__cleanupa_2:
	mov	%3,b
	jmp	__cleanupba
__cleanupa_1:
	mov	%2,b
	jmp	__cleanupba

; Always one more so we fold in the last bit of the return pop adjustment
__cleanup_8:
	mov	%9,b
	jmp	__cleanupb
__cleanup_7:
	mov	%8,b
	jmp	__cleanupb
__cleanup_6:
	mov	%7,b
	jmp	__cleanupb
__cleanup_5:
	mov	%6,b
	jmp	__cleanupb
__cleanup_4:
	mov	%5,b
	jmp	__cleanupb
__cleanup_3:
	mov	%4,b
	jmp	__cleanupb
__cleanup_2:
	mov	%3,b
	jmp	__cleanupb
__cleanup_1:
	mov	%2,b
	jmp	__cleanupb
__cleanupba:
	adc	%0,r14
__cleanupb:
	clr	r10
__cleanup:
	lda	*r15
	add	%1,r15
	adc	%0,r14
	mov	a,r12
	lda	*r15
	mov	a,r13
	add	b,r15
	adc	r10,r14
	br	*r13		; and back to the caller
__cleanupa_0:
	adc	%0,r14
__cleanup_0:
	lda	*r15
	add	%1,r15
	adc	%0,r14
	mov	a,r12
	lda	*r15
	add	%1,r15
	adc	%0,r14
	mov	a,r13
	br	*r13
