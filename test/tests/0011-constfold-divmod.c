
/* Signed / and % where both operands are constant, so the compiler folds
   them instead of calling the runtime divide. A negative operand has to
   fold signed - it used to come out unsigned and wrong (-7 / 2 gave -4). */

static const int q0 = -7 / 2;
static const int q1 = 7 / -2;
static const int q2 = -8 / -2;
static const int q3 = -9 / 4;

static const int r0 = -7 % 2;
static const int r1 = 7 % -2;
static const int r2 = -100 % 7;
static const int r3 = 100 % -7;

int main(int argc, char *argv[])
{
    if (q0 != -3)
        return 1;
    if (q1 != -3)
        return 2;
    if (q2 != 4)
        return 3;
    if (q3 != -2)
        return 4;
    if (r0 != -1)
        return 5;
    if (r1 != 1)
        return 6;
    if (r2 != -2)
        return 7;
    if (r3 != 2)
        return 8;

    /* Same folds inside an expression rather than an initializer */
    if ((-7 / 2) != -3)
        return 9;
    if ((7 / -2) != -3)
        return 10;
    if ((-8 / -2) != 4)
        return 11;
    if ((-100 % 7) != -2)
        return 12;
    if ((7 % -2) != 1)
        return 13;

    /* a == (a / b) * b + a % b */
    if ((-7 / 2) * 2 + (-7 % 2) != -7)
        return 14;
    if ((7 / -2) * -2 + (7 % -2) != 7)
        return 15;

    return 0;
}
