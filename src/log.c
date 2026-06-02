#include "bbs_log.h"
#include <pthread.h>
#include <time.h>
#include <string.h>

static FILE* g_log = NULL;
static pthread_mutex_t g_log_mu = PTHREAD_MUTEX_INITIALIZER;

static void ts(char* buf, size_t cap) {
  struct timespec tp;
  clock_gettime(CLOCK_REALTIME, &tp);
  struct tm tm;
  localtime_r(&tp.tv_sec, &tm);
  strftime(buf, cap, "%Y-%m-%d %H:%M:%S", &tm);
}

bool log_init(const char* path) {
  pthread_mutex_lock(&g_log_mu);
  if (g_log) { pthread_mutex_unlock(&g_log_mu); return true; }

  if (!path || path[0] == '\0') {
    g_log = stderr;
  } else {
    g_log = fopen(path, "a");
    if (!g_log) {
      g_log = stderr;
    }
  }
  pthread_mutex_unlock(&g_log_mu);
  return true;
}

void log_close(void) {
  pthread_mutex_lock(&g_log_mu);
  if (g_log && g_log != stderr) {
    fclose(g_log);
  }
  g_log = NULL;
  pthread_mutex_unlock(&g_log_mu);
}

static void vlog_write(const char* level, const char* fmt, va_list ap) {
  char tbuf[64];
  ts(tbuf, sizeof(tbuf));

  pthread_mutex_lock(&g_log_mu);
  FILE* out = g_log ? g_log : stderr;
  fprintf(out, "[%s] %s ", tbuf, level);
  vfprintf(out, fmt, ap);
  fputc('\n', out);
  fflush(out);
  pthread_mutex_unlock(&g_log_mu);
}

void log_info(const char* fmt, ...) {
  va_list ap; va_start(ap, fmt);
  vlog_write("INFO", fmt, ap);
  va_end(ap);
}

void log_warn(const char* fmt, ...) {
  va_list ap; va_start(ap, fmt);
  vlog_write("WARN", fmt, ap);
  va_end(ap);
}

void log_error(const char* fmt, ...) {
  va_list ap; va_start(ap, fmt);
  vlog_write("ERROR", fmt, ap);
  va_end(ap);
}

void log_audit(const char* user, const char* action, const char* detail) {
  pthread_mutex_lock(&g_log_mu);
  FILE* out = g_log ? g_log : stderr;
  char tbuf[64];
  ts(tbuf, sizeof(tbuf));
  fprintf(out, "[%s] AUDIT user=%s action=%s detail=%s\n",
          tbuf,
          user ? user : "unknown",
          action ? action : "",
          detail ? detail : "");
  fflush(out);
  pthread_mutex_unlock(&g_log_mu);
}

void log_trap(const char* detail) {
  /* trap goes to separate file if configured */
  FILE* f = fopen("logs/trap.log", "a");
  if (f) {
    char tbuf[64];
    ts(tbuf, sizeof(tbuf));
    fprintf(f, "[%s] TRAP %s\n", tbuf, detail ? detail : "");
    fclose(f);
  }
  log_error("TRAP %s", detail ? detail : "");
}
