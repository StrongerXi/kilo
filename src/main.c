#include <unistd.h>

// Return 0 on EOF
static int _read_one_char_from_stdin() {
  char c;
  return read(STDIN_FILENO, &c, 1);
}

int main() {
  while (_read_one_char_from_stdin());
  return 0;
}
