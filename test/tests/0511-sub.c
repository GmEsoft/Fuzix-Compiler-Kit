/* Test byte/word substitutions */

int sub(char *p) {
        int v = 100, d = 3;
        *p = v - 10 * d;
        return v;
}

char subc(void) {
        char d = 0x00, m = 0xf0, x = 0xff;
        return m & d | ~m & x;
}

char sub3(void) {
        char x = 1, y = 2;
        char tmp = 3 - (y << 1);
        return x;
}

int xorneg(void)
{
        int x = -128;
        signed char cx = x;

        if ((cx ^ -128) != (x ^ -128))
                return 1;

        return 0;
}

int main(int argc, char *argv[])
{
        char x;
        if (sub(&x) != 100)
                return 1;
        if (subc() != 0x0F)
                return 2;
        if (sub3() != 1)
                return 3;
        if (xorneg())
                return 4;
        return 0;
}
