#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static struct termios _original_termios;

static void _disable_terminal_raw_mode() {
  // ignore pending input, flush all output
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &_original_termios);
}

static void _enable_terminal_raw_mode() {
  tcgetattr(STDIN_FILENO, &_original_termios);
  atexit(_disable_terminal_raw_mode); // reset at program exit

  struct termios raw = _original_termios;
  // don't echo input, don't wait till ENTER key to process input
  raw.c_lflag &= ~(ECHO | ICANON);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Return 0 on EOF
static void _process_user_input_from_stdin() {
  char ch;
  while (read(STDIN_FILENO, &ch, 1) && ch != 'q');
}

int main() {
  _enable_terminal_raw_mode();
  _process_user_input_from_stdin();
  return 0;
}
