#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

// mimics what `ctrl` does in terminal
#define CTRL_KEY(k) ((k) & 0x1f)

static void _perror_and_exit(const char *s) {
  perror(s);
  exit(1);
}

static void _tcgetattr_or_err(int fd, struct termios* termios) {
  if (tcgetattr(fd, termios) == -1) {
    _perror_and_exit("tcgetattr");
  }
}

static void _tcssetattr_or_err(int fd, int flags, const struct termios* termios) {
  if (tcsetattr(fd, flags, termios) == -1) {
    _perror_and_exit("tcsetattr");
  }
}

static void _read_or_err(int fd, void* buf, size_t count) {
  // In Cygwin, when read() times out it returns -1 with an errno of EAGAIN
  if (read(fd, buf, count) == -1 && errno != EAGAIN) {
    _perror_and_exit("tcsetattr");
  }
}

static struct termios _original_termios;

static void _disable_terminal_raw_mode() {
  // ignore pending input, flush all output
  _tcssetattr_or_err(STDIN_FILENO, TCSAFLUSH, &_original_termios);
}

static void _enable_terminal_raw_mode() {
  _tcgetattr_or_err(STDIN_FILENO, &_original_termios);
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
  raw.c_cc[VMIN] = 1;    // `read` on terminal return immediately on any input
  raw.c_cc[VTIME] = 100; // `read` on terminal input waits for at most 0.1s
  _tcssetattr_or_err(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Return 0 on EOF
static void _process_user_input_from_stdin() {
  while (1) {
    char ch = '\0';
    _read_or_err(STDIN_FILENO, &ch, 1);
    if (iscntrl(ch)) {
      printf("%d\r\n", ch);
    } else {
      printf("%d ('%c')\r\n", ch, ch);
    }
    if (ch == CTRL_KEY('q')) {
      break;
    }
  }
}

int main() {
  _enable_terminal_raw_mode();
  _process_user_input_from_stdin();
  return 0;
}
