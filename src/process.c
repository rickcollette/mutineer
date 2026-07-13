#define _POSIX_C_SOURCE 200809L
#include "bbs_process.h"
#include "bbs_log.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bool command_contains_shell_meta(const char* command, char* errbuf, size_t errcap) {
  for (const char* p = command; p && *p; p++) {
    if (*p == '\\' && p[1]) {
      p++;
      continue;
    }
    if (*p == '`' || *p == ';' || *p == '<' || *p == '>') {
      if (errbuf) snprintf(errbuf, errcap, "unsupported shell metacharacter '%c'", *p);
      return true;
    }
    if (*p == '|') {
      if (errbuf) snprintf(errbuf, errcap, p[1] == '|' ? "unsupported shell operator ||" : "unsupported shell metacharacter '|'");
      return true;
    }
    if (*p == '$' && p[1] == '(') {
      if (errbuf) snprintf(errbuf, errcap, "unsupported command substitution");
      return true;
    }
    if (*p == '&' && p[1] == '&') {
      if (errbuf) snprintf(errbuf, errcap, "unsupported shell operator &&");
      return true;
    }
  }
  return false;
}

void bbs_argv_free(char** argv) {
  if (!argv) return;
  for (size_t i = 0; argv[i]; i++) free(argv[i]);
  free(argv);
}

static bool argv_push(char*** argv, size_t* argc, size_t* cap, char* value) {
  if (*argc + 2 > *cap) {
    size_t ncap = *cap ? *cap * 2 : 8;
    char** nargv = realloc(*argv, ncap * sizeof(char*));
    if (!nargv) return false;
    *argv = nargv;
    *cap = ncap;
  }
  (*argv)[(*argc)++] = value;
  (*argv)[*argc] = NULL;
  return true;
}

static char* replace_percent_f(const char* token, const char* filepath) {
  const char* marker = strstr(token, "%f");
  if (!marker) return strdup(token);

  size_t prefix = (size_t)(marker - token);
  size_t suffix = strlen(marker + 2);
  size_t flen = strlen(filepath);
  char* out = malloc(prefix + flen + suffix + 1);
  if (!out) return NULL;
  memcpy(out, token, prefix);
  memcpy(out + prefix, filepath, flen);
  memcpy(out + prefix + flen, marker + 2, suffix + 1);
  return out;
}

bool bbs_argv_parse_template(const char* command, const char* filepath,
                             char*** argv_out, char* errbuf, size_t errcap) {
  if (!argv_out) return false;
  *argv_out = NULL;
  if (!command || !command[0]) {
    if (errbuf) snprintf(errbuf, errcap, "empty command");
    return false;
  }
  if (command_contains_shell_meta(command, errbuf, errcap)) return false;

  char** argv = NULL;
  size_t argc = 0, cap = 0;
  bool saw_file_marker = false;
  const char* p = command;

  while (*p) {
    while (isspace((unsigned char)*p)) p++;
    if (!*p) break;

    char token[1024];
    size_t len = 0;
    char quote = '\0';
    while (*p && (quote || !isspace((unsigned char)*p))) {
      if (!quote && (*p == '\'' || *p == '"')) {
        quote = *p++;
        continue;
      }
      if (quote && *p == quote) {
        quote = '\0';
        p++;
        continue;
      }
      if (*p == '\\' && p[1]) p++;
      if (len + 1 >= sizeof(token)) {
        if (errbuf) snprintf(errbuf, errcap, "argument too long");
        bbs_argv_free(argv);
        return false;
      }
      token[len++] = *p++;
    }
    if (quote) {
      if (errbuf) snprintf(errbuf, errcap, "unterminated quote");
      bbs_argv_free(argv);
      return false;
    }
    token[len] = '\0';

    char* arg = NULL;
    if (filepath && strstr(token, "%f")) {
      saw_file_marker = true;
      arg = replace_percent_f(token, filepath);
    } else {
      arg = strdup(token);
    }
    if (!arg || !argv_push(&argv, &argc, &cap, arg)) {
      free(arg);
      bbs_argv_free(argv);
      if (errbuf) snprintf(errbuf, errcap, "out of memory");
      return false;
    }
  }

  if (filepath && !saw_file_marker) {
    char* arg = strdup(filepath);
    if (!arg || !argv_push(&argv, &argc, &cap, arg)) {
      free(arg);
      bbs_argv_free(argv);
      if (errbuf) snprintf(errbuf, errcap, "out of memory");
      return false;
    }
  }

  if (argc == 0) {
    if (errbuf) snprintf(errbuf, errcap, "empty command");
    bbs_argv_free(argv);
    return false;
  }
  *argv_out = argv;
  return true;
}

bool bbs_exec_argv(char** argv, const char* label, const char* workdir,
                   int stdin_fd, int stdout_fd, int stderr_fd,
                   int timeout_sec, BbsProcessResult* result,
                   char* errbuf, size_t errcap) {
  if (result) memset(result, 0, sizeof(*result));
  if (!argv || !argv[0]) {
    if (errbuf) snprintf(errbuf, errcap, "empty argv");
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    if (errbuf) snprintf(errbuf, errcap, "fork failed: %s", strerror(errno));
    return false;
  }

  if (pid == 0) {
    setpgid(0, 0);
    if (workdir && workdir[0] && chdir(workdir) != 0) _exit(126);
    if (stdin_fd >= 0) dup2(stdin_fd, STDIN_FILENO);
    if (stdout_fd >= 0) dup2(stdout_fd, STDOUT_FILENO);
    if (stderr_fd >= 0) dup2(stderr_fd, STDERR_FILENO);
    execvp(argv[0], argv);
    _exit(errno == ENOENT ? 127 : 126);
  }

  setpgid(pid, pid);
  int status = 0;
  time_t start = time(NULL);
  while (1) {
    pid_t w = waitpid(pid, &status, WNOHANG);
    if (w == pid) break;
    if (w < 0) {
      if (errno == EINTR) continue;
      if (errbuf) snprintf(errbuf, errcap, "waitpid failed: %s", strerror(errno));
      return false;
    }
    if (timeout_sec > 0 && difftime(time(NULL), start) >= (double)timeout_sec) {
      if (result) result->timed_out = true;
      log_warn("%s timed out after %ds; terminating process group %d",
               label ? label : argv[0], timeout_sec, (int)pid);
      kill(-pid, SIGTERM);
      sleep(2);
      if (waitpid(pid, &status, WNOHANG) != pid) {
        kill(-pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
      }
      break;
    }
    struct timespec delay;
    delay.tv_sec = 0;
    delay.tv_nsec = 100000000;
    nanosleep(&delay, NULL);
  }

  if (WIFEXITED(status)) {
    int rc = WEXITSTATUS(status);
    if (result) {
      result->exited = true;
      result->exit_code = rc;
    }
    if (rc == 0 && !(result && result->timed_out)) {
      log_info("%s completed successfully", label ? label : argv[0]);
      return true;
    }
    log_warn("%s exited with code %d", label ? label : argv[0], rc);
    return false;
  }
  if (WIFSIGNALED(status)) {
    int sig = WTERMSIG(status);
    if (result) {
      result->signaled = true;
      result->term_signal = sig;
    }
    log_warn("%s terminated by signal %d", label ? label : argv[0], sig);
    return false;
  }

  log_warn("%s terminated abnormally", label ? label : argv[0]);
  return false;
}
