#include <unistd.h>

// Return 0 on EOF
static int _read_one_char_from_stdin(char* ch) {
  return read(STDIN_FILENO, ch, 1);
}

int main() {
  char ch;
  while (_read_one_char_from_stdin(&ch) && ch != 'q');
  return 0;
}
