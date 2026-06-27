int main(int argc, char **argv)
{
  int i = 0;
  char arr[1];
  int s;

  arr[i] = 1;
  s = 1;
  if ((arr[i] << 1) != (1 << 1)) {
    return 1;
  }
  if ((arr[i] << s) != (1 << 1)) {
    return 2;
  }
  return 0;
}
