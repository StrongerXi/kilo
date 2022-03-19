#include <ctype.h>
#include <stdio.h>
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

  raw.c_iflag &= ~(
      IXON     // enable output control flow (c-s, c-q)
      | ICRNL  // map '\r' to '\n'
      );
  raw.c_oflag &= ~(
      OPOST    // no output processing, e.g., '\n' to '\r\n'
      );
  raw.c_lflag &= ~(
      ECHO     // echo input
      | ICANON // wait till ENTER key to process input
      | ISIG   // enable signals INTR, QUIT, [D]SUSP (c-z, c-y, c-c)
      | IEXTEN // enable DISCARD and LNEXT (c-v, c-o)
      );
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Return 0 on EOF
static void _process_user_input_from_stdin() {
  char ch;
  while (read(STDIN_FILENO, &ch, 1) && ch != 'q') {
    if (iscntrl(ch)) {
      printf("%d\r\n", ch);
    } else {
      printf("%d ('%c')\r\n", ch, ch);
    }
  }
}

int main() {
  _enable_terminal_raw_mode();
  _process_user_input_from_stdin();
  return 0;
}
