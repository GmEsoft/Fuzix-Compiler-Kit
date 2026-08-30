;
;	Logic helper for ^=
;
;	(X) ^= YD
;
	.export __xhateql
	.export __xhatequl

	.code
__xhatequl:
__xhateql:
	eorb 3,x
	eora 2,x
	std 2,x
	exg d,y
	eorb 1,x
	eora ,x
	std ,x
	exg d,y
	rts
