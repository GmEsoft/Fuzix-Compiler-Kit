;
;	Logic helper for &=
;
;	(X) &= YD
;
	.export __xandeql
	.export __xandequl

	.code
__xandequl:
__xandeql:
	andb 3,x
	anda 2,x
	std 2,x
	exg d,y
	andb 1,x
	anda ,x
	std ,x
	exg d,y
	rts
