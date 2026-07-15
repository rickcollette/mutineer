#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct BbsProcessResult {
  int exit_code;
  int term_signal;
  bool exited;
  bool signaled;
  bool timed_out;
  bool cancelled;
} BbsProcessResult;

bool bbs_argv_parse_template(const char* command, const char* filepath,
                             char*** argv_out, char* errbuf, size_t errcap);
bool bbs_argv_parse_door_template(const char* command, const char* dropdir,
                                  char*** argv_out, char* errbuf, size_t errcap);
void bbs_argv_free(char** argv);

bool bbs_exec_argv(char** argv, const char* label, const char* workdir,
                   int stdin_fd, int stdout_fd, int stderr_fd,
                   int timeout_sec, BbsProcessResult* result,
                   char* errbuf, size_t errcap);

bool bbs_exec_argv_cancel(char** argv, const char* label, const char* workdir,
                          int stdin_fd, int stdout_fd, int stderr_fd,
                          int timeout_sec, int cancel_fd,
                          BbsProcessResult* result,
                          char* errbuf, size_t errcap);
