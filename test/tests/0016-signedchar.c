signed char gsc;

int main(int argc, char *argv[])
{
	signed char sc;
	int s;

	sc = 1;
	sc >>= 8;
	if (sc != (signed char) (1 >> 8))
		return 1;

	sc = 1;
	s = 8;
	sc >>= s;
	if (sc != (signed char) (1 >> 8))
		return 2;

	gsc = 1;
	gsc >>= 8;
	if (gsc != (signed char) (1 >> 8))
		return 3;

	gsc = 1;
	s = 8;
	gsc >>= s;
	if (gsc != (signed char) (1 >> 8))
		return 4;
	return 0;
}
