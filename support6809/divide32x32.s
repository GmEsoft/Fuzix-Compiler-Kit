;
;	32bit unsigned divide. Used as the core for the actual C library
;	division routines. It expects to be called with the parameters
;	offsets from X, and uses tmp/tmp2/tmp3.
;
;	tmp2/tmp3 end up holding the remainder
;
;	On entry the stack frame referenced by X looks like this
;
;	12-15	32bit dividend (C compiler TOS)
;	10,11	A return address
;	5-9	32bit divisor
;	0-4	workspace (also returned in 1-4,x)
;
;	The one trick here is that to save space and time we start
;	with DIVID,x hoilding the 32bit input value (N in the usual
;	algorithm description). Each cycle we take the top bit of N,
;	we shift it left discarding this bit from DIVID,x and we shift the
;	resulting Q(n) bit into the bottom. After 32 cycles we throw N(0)
;	out and have shifted all of Q into the result.
;

DIVIS		.equ	5
;		4/5,x are a return address and it's easier to just leave
;		the gap
DIVID		.equ	11

		.dp

		.export div32x32
		.code

div32x32:
		ldb #32		; 32 iterations for 32 bits
		stb ,x
		clra
		clrb
		; Clear the working register (tmp2/tmp3)
		; R = 0;
		std 1,x
		std 3,x
loop:		; Shift the dividend left and set bit 0 assuming that
		; R >= D
		orcc #1
		rol DIVID+3,x
		rol DIVID+2,x
		rol DIVID+1,x
		rol DIVID,x
		; N(i) is now in carry
		; R <<= 1; R(0) = N(i)
		; Capture into the working register
		rol 4,x		; capturing high bit into the
		rol 3,x		; working register bottom
		rol 2,x
		rol 1,x
		; Do a 32bit subtract but skip writing the high 16bits
		; back until we know the comparison
		;
		; R - D
		;
		ldd 3,x
		subd DIVIS+2,x
		std 3,x
		ldd 1,x
		sbcb DIVIS+1,x
		sbca DIVIS,x
		; Want to subtract (R - D >= 0)
		bcc skip
		; No subtract, so put back the low 16bits we mushed
		ldd 3,x
		addd DIVIS+2,x
		std 3,x
		; We guessed the wrong way for Q(i). Clear Q(i) which is
		; in the lowest bit and we know is set so using dec is safe
		dec DIVID+3,x
done:
		dec ,x
		bne loop
		rts
		; We do want to subtract - write back the other bits
skip:
		; R -= D
		std 1,x
		dec ,x
		bne loop
		rts
