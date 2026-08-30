;
;	Logic helper for |=
;
;	(X) |= YD
;
	.export __xoreql
	.export __xorequl

	.code
__xorequl:
__xoreql:
	orb 3,x
	ora 2,x
	std 2,x
	exg d,y
	orb 1,x
	ora ,x
	std ,x
	exg d,y
	rts
