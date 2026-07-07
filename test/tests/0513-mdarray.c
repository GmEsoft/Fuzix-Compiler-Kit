/*
 * Multi-dimensional array parameter miscompile.
 *
 * Fixes a bug where a T[N][M] that should decompose to T(*)[M] got
 * canonicalised to T**. Caused pointer sized stride instead of a row
 * sized stride. 
 */

typedef unsigned short u16;

/* T[4][4] -> u16(*)[4]: row stride 4*2 = 8 bytes */
static void fill2(u16 b[4][4], unsigned *count)
{
	int r, c;
	for (r = 0; r < 4; r++)
		for (c = 0; c < 4; c++) {
			b[r][c] = r * 10 + c;
			(*count)++;		/* scalar out-param */
		}
}

/* T[2][3][4] -> u16(*)[3][4]: two levels of row stride (24, then 8) */
static void fill3(u16 b[2][3][4])
{
	int r, c, d;
	for (r = 0; r < 2; r++)
		for (c = 0; c < 3; c++)
			for (d = 0; d < 4; d++)
				b[r][c][d] = r * 100 + c * 10 + d;
}

/* array of pointers: a genuine T**, must be unaffected */
static u16 va, vb, vc;
static void setp(u16 *p[3])
{
	*p[0] = 11;
	*p[1] = 22;
	*p[2] = 33;
}

/* sub-array of a genuine 3-D array: cube[i] is u16[3][4] -> u16(*)[4].
   A partial subscript that stays multi-dimensional must decay to a
   pointer-to-array too, not to u16**. */
static void fillplane(u16 plane[3][4], int base)
{
	int r, c;
	for (r = 0; r < 3; r++)
		for (c = 0; c < 4; c++)
			plane[r][c] = base + r * 4 + c;
}

int main(void)
{
	static u16 m[4][4];
	static u16 m3[2][3][4];
	static u16 cube[2][3][4];
	u16 *arr[3];
	u16 (*plane)[4];
	unsigned count = 0;

	fill2(m, &count);
	if (m[0][0] != 0)
		return 1;
	if (m[1][2] != 12)		/* 1*8 + 2*2 bytes in */
		return 2;
	if (m[3][3] != 33)
		return 3;
	if (count != 16)
		return 4;

	fill3(m3);
	if (m3[0][0][0] != 0)
		return 5;
	if (m3[1][0][0] != 100)		/* outer stride 24 bytes */
		return 6;
	if (m3[0][2][3] != 23)		/* middle stride 8 bytes */
		return 7;
	if (m3[1][2][3] != 123)
		return 8;

	arr[0] = &va;
	arr[1] = &vb;
	arr[2] = &vc;
	setp(arr);
	if (va != 11)
		return 9;
	if (vb != 22)
		return 10;
	if (vc != 33)
		return 11;

	/* pass a sub-array of a genuine 3-D array: cube[1] is u16[3][4] */
	fillplane(cube[1], 100);
	if (cube[0][0][0] != 0)		/* other plane untouched */
		return 12;
	if (cube[1][0][0] != 100)
		return 13;
	if (cube[1][2][3] != 111)	/* 100 + 2*4 + 3 */
		return 14;

	plane = cube[1];		/* pointer-to-array from a sub-array */
	if (plane != cube[1])		/* comparison was rejected before the fix */
		return 15;
	if (plane[2][3] != 111)
		return 16;

	return 0;
}
