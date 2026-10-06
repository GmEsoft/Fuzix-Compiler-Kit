	movd %0xefff,r15
	mov %0x20,b
	ldsp
	call @__install_traps
	call @_main
	mov r5,a
	movp a,p255

	.export _printint
	.export _printchar

_printint:
	pop a
	pop b
	clr r11
	call @__frame
	mov %2,r1
	call @__gargr2
	mov r5,r0
	movp a,p252
	mov r4,r0
	movp a,p253
	br @__cleanup_2

_printchar:
	pop a
	pop b
	clr r11
	call @__frame
	mov %2,r1
	call @__gargr2
	mov r5,r0
	movp a,p254
	br @__cleanup_2
