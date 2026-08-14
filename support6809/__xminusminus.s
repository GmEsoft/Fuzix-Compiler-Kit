;
;	,X - D
;
	.export __xmminus
	.export __xmminusu
	.code

__xmminus:
__xmminusu:
	pshs d
	ldd ,x
	tfr d,y
	subd ,s++
	std ,x
	tfr y,d
	rts

