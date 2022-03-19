#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

// mimics what `ctrl` does in terminal
#define CTRL_KEY(k) ((k) & 0x1f)

static void _perror_and_exit(const char *s);
static void _clean_up_before_exit();

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

static int _read_or_err(int fd, void* buf, size_t count) {
  // In Cygwin, when read() times out it returns -1 with an errno of EAGAIN
  int ret = read(fd, buf, count);
  if (ret == -1 && errno != EAGAIN) {
    _perror_and_exit("read");
  }
  return ret;
}

static int _write_or_err(int fd, void* buf, size_t count) {
  int ret = write(fd, buf, count);
  if (ret == -1) {
    _perror_and_exit("write");
  }
  return ret;
}

  // look up VT100 escape sequences
static void _write_clear_screen(int fd) {
  _write_or_err(fd, "\x1b[2J", 4);
}

static void _write_set_cursor_to_topleft(int fd) {
  _write_or_err(fd, "\x1b[H", 3); // default to \x1b[1;1H
}

static void _perror_and_exit(const char *s) {
  _clean_up_before_exit();
  perror(s);
  exit(1);
}

static void _clean_up_before_exit() {
  _write_clear_screen(STDOUT_FILENO);
  _write_set_cursor_to_topleft(STDOUT_FILENO);
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

static char _read_key(int fd) {
  char ch;
  while (_read_or_err(fd, &ch, 1) != 1);
  return ch;
}

static void _process_one_key_press(int fd) {
  char ch = _read_key(fd);
  switch (ch) {
    case CTRL_KEY('q'):
      _clean_up_before_exit();
      exit(0);
  }
}

static void _refresh_screen(int fd) {
  _write_clear_screen(fd);
  _write_set_cursor_to_topleft(fd);
}

int main() {
  _enable_terminal_raw_mode();
  while (1) {
    _refresh_screen(STDOUT_FILENO);
    _process_one_key_press(STDIN_FILENO);
  }
  return 0;
}
