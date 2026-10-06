char sub(char sw, char a, char b) {
        if (sw ? a : !b) return 1;
        return 0;
}

int main(int argc, char *argv[])
{
        if (!sub(1, 2, 3))
                return 1;
        return 0;
}
