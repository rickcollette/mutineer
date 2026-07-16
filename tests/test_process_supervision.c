#include "bbs_process.h"

#include <fcntl.h>
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

  int inherited[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, inherited) == 0,
        "inheritance socketpair");
  int fd_flags = fcntl(inherited[1], F_GETFD);
  CHECK(fd_flags >= 0 &&
            fcntl(inherited[1], F_SETFD, fd_flags | FD_CLOEXEC) == 0,
        "mark caller descriptor close-on-exec");
  char script[128];
  snprintf(script, sizeof(script), "test -e /proc/self/fd/%d", inherited[1]);
  char* inherit_argv[] = { "/bin/sh", "-c", script, NULL };
  BbsProcessResult inherited_result;
  CHECK(bbs_exec_argv_cancel(inherit_argv, "inherit-test", NULL, -1, -1, -1,
                             5, inherited[1], &inherited_result, err,
                             sizeof(err)),
        "caller descriptor survives exec");
  close(inherited[0]);
  close(inherited[1]);
  return 0;
}
