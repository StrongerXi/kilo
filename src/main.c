#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define KILO_VERSION "0.0.1"
// mimics what `ctrl` does in terminal
#define CTRL_KEY(k) ((k) & 0x1f)

enum editor_key {
  // a hack to avoid mixing up of arrow keys and regular chars
  // Ideally we should probably emulate ADT
  ARROW_UP = 1000,
  ARROW_DOWN,
  ARROW_LEFT,
  ARROW_RIGHT,
};

typedef struct {
  char* buf;
  unsigned int size;
  unsigned int capacity;
} byte_buf_t;

typedef struct {
  unsigned int cursor_row;
  unsigned int cursor_col;
  unsigned int screen_rows;
  unsigned int screen_cols;
  int input_fd;
  int output_fd;
  byte_buf_t* paint_buf;
} editor_state_t;

static void _perror_and_exit(const char *s);
static void _clean_up_before_exit();

static byte_buf_t* _new_byte_buf() {
  byte_buf_t* buf = (byte_buf_t*) malloc(sizeof(byte_buf_t));
  if (buf == NULL) {
    return NULL;
  }
  buf->size = 0;
  buf->capacity = 512;
  buf->buf = (char*) malloc(buf->capacity);
  if (buf->buf == NULL) {
    free(buf);
    return NULL;
  }
  return buf;
}

static void _append_byte_buf(byte_buf_t* buf, const char* data, int count) {
  if (buf->size + count > buf->capacity) {
    buf->capacity *= 2;
    buf->buf = realloc(buf->buf, buf->capacity);
  }
  memcpy(buf->buf + buf->size, data, count);
  buf->size += count;
}

static void _clear_byte_buf(byte_buf_t* buf) {
  buf->size = 0;
}

static int _flush_byte_buf(byte_buf_t* buf, int fd) {
  return write(fd, buf->buf, buf->size);
}

static void _free_byte_buf(byte_buf_t* buf) {
  free(buf->buf);
  free(buf);
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

static int _read_or_err(int fd, void* buf, size_t count) {
  // In Cygwin, when read() times out it returns -1 with an errno of EAGAIN
  int ret = read(fd, buf, count);
  if (ret == -1 && errno != EAGAIN) {
    _perror_and_exit("read");
  }
  return ret;
}

// look up VT100 escape sequences
static void _write_clear_screen(int fd) {
  write(fd, "\x1b[2J", 4);
}

// Return 0 on success
static int _write_set_cursor_pos(
  int out_fd, unsigned int row, unsigned int col
) {
  char buf[80];
  int count = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
  return write(out_fd, buf, count) != count;
}

// TODO better to use a paint_command buffer, with emulated ADTs
static void _append_erase_line(byte_buf_t* buf) {
  _append_byte_buf(buf, "\x1b[K", 3);
}

static void _append_set_cursor_to_topleft(byte_buf_t* buf) {
  _append_byte_buf(buf, "\x1b[H", 4); // default to (1, 1)
}

static void _append_set_cursor_to_pos(
  byte_buf_t* buf, unsigned int row, unsigned int col
) {
  char msg[80];
  int count = snprintf(msg, sizeof(msg), "\x1b[%d;%dH", row, col);
  _append_byte_buf(buf, msg, count);
}

static void _append_welcome_message(byte_buf_t* buf, unsigned int screen_cols) {
  const char* msg = "Kilo editor -- version " KILO_VERSION;
  unsigned int msg_len = strlen(msg);
  if (msg_len >= screen_cols) {
    _append_byte_buf(buf, msg, screen_cols);
  } else { // center message
    int num_left_space = (screen_cols - msg_len) / 2;
    char left_spaces[num_left_space];
    memset(left_spaces, ' ', num_left_space);
    _append_byte_buf(buf, left_spaces, num_left_space);
    _append_byte_buf(buf, msg, msg_len);
  }
}

static void _append_draw_rows(const editor_state_t* state) {
  for (unsigned int y = 1; y <= state->screen_rows; y++) {
    _append_erase_line(state->paint_buf);
    if (y == state->screen_rows / 3) {
      _append_welcome_message(state->paint_buf, state->screen_cols);
    } else {
      _append_byte_buf(state->paint_buf, "~", 1);
    }
    if (y < state->screen_rows) { // prevent forced terminal scrolling
      _append_byte_buf(state->paint_buf, "\r\n", 2);
    }
  }
}

static void _perror_and_exit(const char *s) {
  _clean_up_before_exit();
  perror(s);
  exit(1);
}

static void _clean_up_before_exit() {
  _write_clear_screen(STDOUT_FILENO);
  _write_set_cursor_pos(STDOUT_FILENO, 1, 1);
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
  raw.c_cc[VMIN] = 0;  // `read` on terminal return immediately on any key press
  raw.c_cc[VTIME] = 1; // `read` on terminal input waits for at most 0.1s
  _tcssetattr_or_err(STDIN_FILENO, TCSAFLUSH, &raw);
}

static int _read_key(int fd) {
  char ch;
  while (_read_or_err(fd, &ch, 1) != 1);
  if (ch == '\x1b') { // escape key
    char seq[2];
    if (read(STDIN_FILENO, &seq[0], 1) != 1
        || seq[0] != '['
        || read(STDIN_FILENO, &seq[1], 1) != 1
       ) {
      return '\x1b';
    }
    switch (seq[1]) { // based on standard arrow key mapping
      case 'A': return ARROW_UP;
      case 'B': return ARROW_DOWN;
      case 'C': return ARROW_RIGHT;
      case 'D': return ARROW_LEFT;
    }
    return '\x1b';
  } else {
    return ch;
  }
}

static void _process_one_key_press(editor_state_t* state) {
  int ch = _read_key(state->input_fd);
  switch (ch) {
    case ARROW_UP:
      if (state->cursor_row > 1) state->cursor_row -= 1;
      break;
    case ARROW_DOWN:
      if (state->cursor_row < state->screen_rows) state->cursor_row += 1;
      break;
    case ARROW_LEFT:
      if (state->cursor_col > 1) state->cursor_col -= 1;
      break;
    case ARROW_RIGHT:
      if (state->cursor_col < state->screen_rows) state->cursor_col += 1;
      break;
    case CTRL_KEY('q'):
      _clean_up_before_exit();
      exit(0);
  }
}

static void _refresh_screen(const editor_state_t* state) {
  _append_set_cursor_to_topleft(state->paint_buf);
  _append_draw_rows(state);
  _append_set_cursor_to_pos(
    state->paint_buf, state->cursor_row, state->cursor_col
  );
  _flush_byte_buf(state->paint_buf, state->output_fd);
  _clear_byte_buf(state->paint_buf);
}

int _write_get_cursor_pos(
  int in_fd, int out_fd, unsigned int *row, unsigned int *col
) {
  char buf[80];
  // query cursor position, response "\x1b[rows;cols"
  if (write(out_fd, "\x1b[6n", 4) != 4) return -1;
  for (unsigned int i = 0; i < sizeof(buf) - 1; i++) {
    if (read(in_fd, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
  }
  buf[sizeof(buf) - 1] = '\0';
  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", row, col) != 2) return -1;
  return 0;
}

static void _write_query_screen_size(
  int in_fd, int out_fd, unsigned int* rows, unsigned int* cols
) {
  unsigned int original_row, original_col;
  _write_get_cursor_pos(in_fd, out_fd, &original_row, &original_col);
  if (write(out_fd, "\x1b[9999C\x1b[9999B", 14) != 14) {
    _perror_and_exit("_write_query_screen_size");
  }
  _write_get_cursor_pos(in_fd, out_fd, rows, cols);
  if (_write_set_cursor_pos(out_fd, original_row, original_col)) {
    _perror_and_exit("_write_query_screen_size");
  }
}

static void _get_screen_size(
  int in_fd, int out_fd, unsigned int* rows, unsigned int* cols
) {
  struct winsize ws;
  if (ioctl(out_fd, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    _write_query_screen_size(in_fd, out_fd, rows, cols);
  } else {
    *rows = ws.ws_row;
    *cols = ws.ws_col;
  }
}

static editor_state_t* _new_editor_state() {
  editor_state_t* state = (editor_state_t*) malloc(sizeof(editor_state_t));
  if (state == NULL) {
    _perror_and_exit("_new_editor_state");
  }
  state->input_fd = STDIN_FILENO;
  state->output_fd = STDOUT_FILENO;
  _get_screen_size(
    state->input_fd, state->output_fd, &state->screen_rows, &state->screen_cols
  );
  state->cursor_row = 1;
  state->cursor_col = 1;
  state->paint_buf = _new_byte_buf();
  if (state->paint_buf == NULL) {
    _perror_and_exit("_init_editor_state -> _new_byte_buf");
  }
  return state;
}

static void _free_editor_state(editor_state_t* state) {
  _free_byte_buf(state->paint_buf);
  free(state);
}

int main() {
  _enable_terminal_raw_mode();
  editor_state_t* state = _new_editor_state();
  while (1) {
    _refresh_screen(state);
    _process_one_key_press(state);
  }
  _free_editor_state(state);
  return 0;
}
