#include "bbs_telnet.h"

#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHECK(cond, msg) do { \
  if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while (0)

static int has_triplet(const uint8_t* buf, ssize_t n, uint8_t a, uint8_t b, uint8_t c) {
  for (ssize_t i = 0; i + 2 < n; i++) {
    if (buf[i] == a && buf[i + 1] == b && buf[i + 2] == c) return 1;
  }
  return 0;
}

int main(void) {
  int fds[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0, "socketpair");

  telnet_password_begin(fds[0]);
  uint8_t buf[32];
  ssize_t n = read(fds[1], buf, sizeof(buf));
  CHECK(n >= 6, "password begin sends negotiation bytes");
  CHECK(has_triplet(buf, n, 255, 251, 1), "password begin requests server echo");

  telnet_password_end(fds[0]);
  n = read(fds[1], buf, sizeof(buf));
  CHECK(n >= 6, "password end sends negotiation bytes");
  CHECK(has_triplet(buf, n, 255, 251, 1), "password end keeps server echo policy");

  close(fds[0]);
  close(fds[1]);
  return 0;
}
