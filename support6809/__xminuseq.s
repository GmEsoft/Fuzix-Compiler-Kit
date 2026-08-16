;
;	,X -= D
;
	.export __xminuseq
	.export __xminusequ

__xminuseq:
__xminusequ:
	pshs d
	ldd ,x
	subd ,s++
	std ,x
	rts
