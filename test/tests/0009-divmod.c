
int mul(int a, int b)
{
    return a * b;
}

unsigned divu(unsigned a, unsigned b)
{
    return a / b;
}

unsigned modu(unsigned a, unsigned b)
{
    return a % b;
}

int divs(int a, int b)
{
    return a / b;
}

int mods(int a, int b)
{
    return a % b;
}

int mul100(int a)
{
    return a * 100;
}

unsigned divu100(unsigned a)
{
    return a / 100;
}

unsigned modu100(unsigned a)
{
    return a % 100;
}

int divs100(int a)
{
    return a / 100;
}

int mods100(int a)
{
    return a % 100;
}

int divs100n(int a)
{
    return a / -100;
}

int mods100n(int a)
{
    return a % -100;
}


int main(int argc, char *argv[])
{
    unsigned n;
    signed i;
    unsigned char u;
    signed char c;

    if (mul(0xFF,0xFF) != 65025U)
        return 1;
    if (mul(0xFF,0) != 0)
        return 2;
    if (divu(200, 10) != 20)
        return 3;
    if (divu(209, 10) != 20)
        return 4;
    if (divs(200, 10) != 20)
        return 5;
    if (divs(200, -10) != -20)
        return 6;
    if (divs(-200, 10) != -20)
        return 7;
    if (modu(1000, 10) != 0)
        return 8;
    if (modu(1006, 10) != 6)
        return 9;
    if (mods(1000, 10) != 0)
        return 10;
    if (mods(1006, 10) != 6)
        return 11;
    if (mods(-1006, 10) != -6)
        return 12;
    if (mods(-1006, -10) != -6)
        return 13;
    if (mods(1006, -10) != 6)
        return 14;
    /* Do tests with constants - this trigers a different code generator
       path in most backends */
    if (mul100(10) != 1000)
        return 15;
    if (mul100(0) != 0)
        return 16;
    if (divu100(200) != 2)
        return 17;
    if (divu100(209) != 2)
        return 18;
    if (divs100(200) != 2)
        return 19;
    if (divs100n(200) != -2)
        return 20;
    if (divs100(-200) != -2)
        return 21;
    if (modu100(1000) != 0)
        return 22;
    if (modu100(1006) != 6)
        return 23;
    if (mods100(1000) != 0)
        return 24;
    if (mods100(1006) != 6)
        return 25;
    if (mods100(-1006) != -6)
        return 26;
    if (mods100n(-1006) != -6)
        return 27;
    if (mods100n(1006) != 6)
        return 28;
    n = 32;
    n /= 4;
    if (n != 8)
        return 29;
    n %= 12;
    if (n != 8)
        return 30;
    i = -32;
    i /= 4;
    if (i != -8)
        return 31;
    i %= 10;
    if (i != -8)
        return 32;
    /* Signed divide round towards zero check */
    i = -1;
    if (i / 2)
        return 33;
    i = 1;
    if (i / 2)
        return 34;

    u = 32;
    u /= 4;
    if (u != 8)
        return 35;
    u %= 12;
    if (u != 8)
        return 36;
    c = -32;
    c /= 4;
    if (c != -8)
        return 37;
    c %= 10;
    if (c != -8)
        return 38;
    /* Signed divide round towards zero check */
    c = -1;
    if (c / 2)
        return 39;
    c = 1;
    if (c / 2)
        return 40;

    u = 5;
    u %= -3;
    if (u != 2)
        return 41;
    u = 5;
    if (u %= -3 != 2)
        return 42;

    u = 255;
    u /= 1;
    if (u != 255)
        return 43;

    u = 255;
    u %= 254;
    if (u != 1)
        return 44;

    c = -1;
    c /= 1;
    if (c != -1)
        return 45;

    n = 10000;
    if ((n /= 4) != 2500)
        return 46;
    if ((n %= 1000) != 500)
        return 47;
    i = -10000;
    if ((i /= 4) != -2500)
        return 48;
    if ((i %= 1000)  != -500)
        return 49;

    u = 32;
    if ((u /=4) != 8)
        return 50;
    if ((u %= 12) != 8)
        return 51;
    c = -32;
    if ((c /= 4) != -8)
        return 52;
    if ((c %= 10) != -8)
        return 53;
    return 0;
}
