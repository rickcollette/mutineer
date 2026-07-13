#define _POSIX_C_SOURCE 200809L
#include "bbs_session.h"
#include "bbs_msg_cmds.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHECK(cond, msg) do { \
  if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while (0)

static char g_output[65536];
static size_t g_output_len;

void send_str(Session* s, const char* str) {
  (void)s;
  if (!str) return;
  size_t n = strlen(str);
  if (g_output_len + n >= sizeof(g_output)) n = sizeof(g_output) - g_output_len - 1;
  memcpy(g_output + g_output_len, str, n);
  g_output_len += n;
  g_output[g_output_len] = '\0';
}

typedef struct {
  Session s;
  char* text;
  size_t cap;
  int result;
} EditRun;

static void* run_editor(void* arg) {
  EditRun* run = arg;
  run->result = fsedit_edit(&run->s, run->text, run->cap);
  return NULL;
}

static int run_case(const char* initial, size_t cap, const char* input, char* out, size_t out_cap) {
  int fds[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0, "socketpair creates");
  memset(g_output, 0, sizeof(g_output));
  g_output_len = 0;
  memset(out, 0, out_cap);
  snprintf(out, out_cap, "%s", initial ? initial : "");

  EditRun run;
  memset(&run, 0, sizeof(run));
  run.s.fd = fds[1];
  run.text = out;
  run.cap = cap;

  pthread_t th;
  CHECK(pthread_create(&th, NULL, run_editor, &run) == 0, "editor thread starts");
  if (input && input[0]) {
    CHECK(write(fds[0], input, strlen(input)) == (ssize_t)strlen(input), "test input writes");
  }
  pthread_join(th, NULL);
  close(fds[0]);
  close(fds[1]);
  return run.result;
}

static int test_long_line_clamps(void) {
  char input[128];
  memset(input, 'A', 100);
  input[100] = 0x13;
  input[101] = '\0';
  char out[256];
  int rc = run_case("", sizeof(out), input, out, sizeof(out));
  CHECK(rc == 1, "long-line edit saves");
  CHECK(strlen(out) == 80, "long line is clamped to 78 chars plus CRLF");
  CHECK(strstr(out, "\r\n") != NULL, "saved text includes CRLF");
  return 0;
}

static int test_small_output_cap(void) {
  char out[8];
  int rc = run_case("", sizeof(out), "abcdefghi\x13", out, sizeof(out));
  CHECK(rc == 1, "small-cap edit saves");
  CHECK(out[sizeof(out) - 1] == '\0', "small output remains terminated");
  CHECK(strlen(out) < sizeof(out), "small output does not overflow");
  return 0;
}

static int test_viewport_scrolls(void) {
  char initial[1024] = {0};
  for (int i = 1; i <= 25; i++) {
    char line[32];
    snprintf(line, sizeof(line), "line%d\r\n", i);
    strncat(initial, line, sizeof(initial) - strlen(initial) - 1);
  }
  char input[128] = {0};
  size_t pos = 0;
  for (int i = 0; i < 24; i++) {
    input[pos++] = 0x1b;
    input[pos++] = '[';
    input[pos++] = 'B';
  }
  input[pos++] = 0x13;
  char out[1024];
  int rc = run_case(initial, sizeof(out), input, out, sizeof(out));
  CHECK(rc == 1, "viewport edit saves");
  CHECK(strstr(g_output, "25|line25") != NULL, "viewport redraw reaches line 25");
  return 0;
}

static int test_split_insert_delete_join(void) {
  char out[256];
  int rc = run_case("", sizeof(out), "abc\rde\x19\x7f\x13", out, sizeof(out));
  CHECK(rc == 1, "split/delete/join sequence saves");
  CHECK(strlen(out) < sizeof(out), "split/delete/join output remains bounded");
  return 0;
}

int main(void) {
  CHECK(test_long_line_clamps() == 0, "long-line clamp test passed");
  CHECK(test_small_output_cap() == 0, "small output cap test passed");
  CHECK(test_viewport_scrolls() == 0, "viewport scroll test passed");
  CHECK(test_split_insert_delete_join() == 0, "split/delete/join test passed");
  return 0;
}
