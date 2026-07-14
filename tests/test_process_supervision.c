#include "bbs_process.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHECK(cond, msg) do { \
  if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while (0)

int main(void) {
  int fds[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0, "socketpair");

  char* argv[] = { "/bin/sleep", "30", NULL };
  close(fds[1]);

  BbsProcessResult result;
  char err[128] = "";
  CHECK(!bbs_exec_argv_cancel(argv, "cancel-test", NULL, -1, -1, -1,
                              30, fds[0], &result, err, sizeof(err)),
        "cancelled child returns failure");
  CHECK(result.cancelled, "cancel result is recorded");
  CHECK(result.signaled || result.exit_code != 0 || result.cancelled,
        "child was terminated after cancellation");
  close(fds[0]);
  return 0;
}
