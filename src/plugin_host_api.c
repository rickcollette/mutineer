/*
 * plugin_host_api.c - Host API Implementation for Plugins
 *
 * This file implements the bbs_host_api_t interface that plugins use
 * to interact with the BBS host.
 */

#include "bbs_plugin_api.h"
#include "bbs_session.h"
#include "bbs_log.h"
#include "bbs_util.h"
#include "bbs_db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

/* Cast opaque session to internal Session type */
#define TO_SESSION(s) ((Session*)(s))

/* ============================================================================
 * PLUGIN TASK SCHEDULER
 * ============================================================================ */

#define PLUGIN_TASK_MAX 64

typedef struct {
  uint64_t id;
  bbs_task_fn fn;
  void* user;
  uint64_t run_at_ms;
  int active;
} PluginTask;

static PluginTask g_tasks[PLUGIN_TASK_MAX];
static pthread_mutex_t g_task_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_task_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_task_thread;
static volatile int g_task_running = 0;
static uint64_t g_task_next_id = 1;

static uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void* task_thread_fn(void* arg) {
  (void)arg;
  pthread_mutex_lock(&g_task_mutex);
  while (g_task_running) {
    uint64_t now = now_ms();
    uint64_t next_wake = now + 1000;
    
    for (int i = 0; i < PLUGIN_TASK_MAX; i++) {
      if (!g_tasks[i].active) continue;
      
      if (g_tasks[i].run_at_ms <= now) {
        bbs_task_fn fn = g_tasks[i].fn;
        void* user = g_tasks[i].user;
        g_tasks[i].active = 0;
        
        pthread_mutex_unlock(&g_task_mutex);
        fn(user);
        pthread_mutex_lock(&g_task_mutex);
      } else if (g_tasks[i].run_at_ms < next_wake) {
        next_wake = g_tasks[i].run_at_ms;
      }
    }
    
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t wait_ms = next_wake - now_ms();
    if (wait_ms > 1000) wait_ms = 1000;
    ts.tv_sec += wait_ms / 1000;
    ts.tv_nsec += (wait_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
      ts.tv_sec++;
      ts.tv_nsec -= 1000000000;
    }
    pthread_cond_timedwait(&g_task_cond, &g_task_mutex, &ts);
  }
  pthread_mutex_unlock(&g_task_mutex);
  return NULL;
}

static void plugin_sched_init(void) {
  pthread_mutex_lock(&g_task_mutex);
  if (!g_task_running) {
    g_task_running = 1;
    memset(g_tasks, 0, sizeof(g_tasks));
    pthread_create(&g_task_thread, NULL, task_thread_fn, NULL);
  }
  pthread_mutex_unlock(&g_task_mutex);
}

static void plugin_sched_shutdown(void) {
  pthread_mutex_lock(&g_task_mutex);
  if (g_task_running) {
    g_task_running = 0;
    pthread_cond_signal(&g_task_cond);
    pthread_mutex_unlock(&g_task_mutex);
    pthread_join(g_task_thread, NULL);
  } else {
    pthread_mutex_unlock(&g_task_mutex);
  }
}

/* I/O Implementation */

static bbs_rc_t host_io_write(bbs_session_t* s, const void* buf, size_t n) {
  Session* sess = TO_SESSION(s);
  if (!sess || sess->fd < 0) return BBS_EIO;
  if (!buf || n == 0) return BBS_OK;
  
  if (!fd_write_all(sess->fd, buf, n)) {
    return BBS_EIO;
  }
  return BBS_OK;
}

static bbs_rc_t host_io_printf(bbs_session_t* s, const char* fmt, ...) {
  Session* sess = TO_SESSION(s);
  if (!sess || sess->fd < 0) return BBS_EIO;
  if (!fmt) return BBS_EINVAL;
  
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  int len = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  
  if (len < 0) return BBS_EINTERNAL;
  if (len == 0) return BBS_OK;
  
  size_t to_write = (size_t)len;
  if (to_write > sizeof(buf) - 1) to_write = sizeof(buf) - 1;
  
  if (!fd_write_all(sess->fd, buf, to_write)) {
    return BBS_EIO;
  }
  return BBS_OK;
}

static bbs_rc_t host_io_readline(bbs_session_t* s, char* out, size_t out_sz, int echo) {
  Session* sess = TO_SESSION(s);
  if (!sess || sess->fd < 0) return BBS_EIO;
  if (!out || out_sz == 0) return BBS_EINVAL;
  
  out[0] = '\0';
  
  /* Use fd_readline for basic line reading */
  int timeout = sess->cfg.idle_timeout_sec > 0 ? sess->cfg.idle_timeout_sec : 60;
  int rc = fd_readline(sess->fd, timeout, (uint8_t*)out, out_sz);
  
  if (rc < 0) return BBS_ETIMEOUT;
  if (rc == 0) return BBS_EIO;  /* disconnect */
  
  /* Echo the input if requested */
  if (echo) {
    fd_write_all(sess->fd, out, strlen(out));
  } else {
    /* Echo asterisks for password input */
    for (size_t i = 0; i < strlen(out); i++) {
      fd_write_all(sess->fd, "*", 1);
    }
  }
  
  /* Echo newline */
  fd_write_all(sess->fd, "\r\n", 2);
  
  return BBS_OK;
}

static bbs_rc_t host_io_cls(bbs_session_t* s) {
  Session* sess = TO_SESSION(s);
  if (!sess || sess->fd < 0) return BBS_EIO;
  
  /* ANSI clear screen and home cursor */
  const char* cls = "\x1b[2J\x1b[H";
  if (!fd_write_all(sess->fd, cls, strlen(cls))) {
    return BBS_EIO;
  }
  return BBS_OK;
}

static const bbs_io_t g_host_io = {
  .write = host_io_write,
  .printf = host_io_printf,
  .readline = host_io_readline,
  .cls = host_io_cls
};

/* Scheduler Implementation */

static bbs_rc_t host_sched_enqueue(bbs_task_fn fn, void* user) {
  if (!fn) return BBS_EINVAL;
  
  plugin_sched_init();
  
  pthread_mutex_lock(&g_task_mutex);
  for (int i = 0; i < PLUGIN_TASK_MAX; i++) {
    if (!g_tasks[i].active) {
      g_tasks[i].id = g_task_next_id++;
      g_tasks[i].fn = fn;
      g_tasks[i].user = user;
      g_tasks[i].run_at_ms = now_ms();
      g_tasks[i].active = 1;
      pthread_cond_signal(&g_task_cond);
      pthread_mutex_unlock(&g_task_mutex);
      return BBS_OK;
    }
  }
  pthread_mutex_unlock(&g_task_mutex);
  return BBS_EINTERNAL;
}

static bbs_rc_t host_sched_after_ms(uint64_t delay_ms, bbs_task_fn fn, void* user, uint64_t* task_id_out) {
  if (!fn) return BBS_EINVAL;
  
  plugin_sched_init();
  
  pthread_mutex_lock(&g_task_mutex);
  for (int i = 0; i < PLUGIN_TASK_MAX; i++) {
    if (!g_tasks[i].active) {
      uint64_t id = g_task_next_id++;
      g_tasks[i].id = id;
      g_tasks[i].fn = fn;
      g_tasks[i].user = user;
      g_tasks[i].run_at_ms = now_ms() + delay_ms;
      g_tasks[i].active = 1;
      if (task_id_out) *task_id_out = id;
      pthread_cond_signal(&g_task_cond);
      pthread_mutex_unlock(&g_task_mutex);
      return BBS_OK;
    }
  }
  pthread_mutex_unlock(&g_task_mutex);
  return BBS_EINTERNAL;
}

static bbs_rc_t host_sched_cancel(uint64_t task_id) {
  pthread_mutex_lock(&g_task_mutex);
  for (int i = 0; i < PLUGIN_TASK_MAX; i++) {
    if (g_tasks[i].active && g_tasks[i].id == task_id) {
      g_tasks[i].active = 0;
      pthread_mutex_unlock(&g_task_mutex);
      return BBS_OK;
    }
  }
  pthread_mutex_unlock(&g_task_mutex);
  return BBS_EINVAL;
}

static const bbs_sched_t g_host_sched = {
  .enqueue = host_sched_enqueue,
  .after_ms = host_sched_after_ms,
  .cancel = host_sched_cancel
};

/* Logging */

static void host_log(bbs_log_level_t lvl, const char* subsystem, const char* msg) {
  const char* sub = subsystem ? subsystem : "plugin";
  const char* m = msg ? msg : "";
  
  switch (lvl) {
    case BBS_LOG_DEBUG:
      log_info("[%s] %s", sub, m);
      break;
    case BBS_LOG_INFO:
      log_info("[%s] %s", sub, m);
      break;
    case BBS_LOG_WARN:
      log_warn("[%s] %s", sub, m);
      break;
    case BBS_LOG_ERROR:
      log_error("[%s] %s", sub, m);
      break;
    default:
      log_info("[%s] %s", sub, m);
      break;
  }
}

/* Session Info Helpers */

static const char* host_session_username(bbs_session_t* s) {
  Session* sess = TO_SESSION(s);
  if (!sess) return "";
  return sess->user.handle;
}

static uint32_t host_session_user_id(bbs_session_t* s) {
  Session* sess = TO_SESSION(s);
  if (!sess) return 0;
  return (uint32_t)sess->user.id;
}

static uint32_t host_session_user_flags(bbs_session_t* s) {
  Session* sess = TO_SESSION(s);
  if (!sess) return 0;
  return (uint32_t)sess->user.ac_flags;
}

static const char* host_session_remote_addr(bbs_session_t* s) {
  Session* sess = TO_SESSION(s);
  if (!sess) return "";
  return sess->ip;
}

/* Key/Value Store */

static bbs_rc_t host_kv_get(const char* ns, const char* key, char* out, size_t out_sz) {
  if (!ns || !key || !out || out_sz == 0) return BBS_EINVAL;
  
  out[0] = '\0';
  
  /* Use a simple file-based KV store in data/plugin_kv/ */
  char path[512];
  snprintf(path, sizeof(path), "data/plugin_kv/%s", ns);
  
  char filepath[768];
  snprintf(filepath, sizeof(filepath), "%s/%s", path, key);
  
  FILE* f = fopen(filepath, "r");
  if (!f) return BBS_EINVAL;  /* Key not found */
  
  size_t n = fread(out, 1, out_sz - 1, f);
  out[n] = '\0';
  fclose(f);
  
  return BBS_OK;
}

static bbs_rc_t host_kv_set(const char* ns, const char* key, const char* val) {
  if (!ns || !key) return BBS_EINVAL;
  
  /* Create namespace directory if needed */
  char path[512];
  snprintf(path, sizeof(path), "data/plugin_kv");
  mkdir(path, 0755);
  
  snprintf(path, sizeof(path), "data/plugin_kv/%s", ns);
  mkdir(path, 0755);
  
  char filepath[768];
  snprintf(filepath, sizeof(filepath), "%s/%s", path, key);
  
  if (!val) {
    /* Delete key */
    unlink(filepath);
    return BBS_OK;
  }
  
  FILE* f = fopen(filepath, "w");
  if (!f) return BBS_EIO;
  
  fwrite(val, 1, strlen(val), f);
  fclose(f);
  
  return BBS_OK;
}

/* Plugin Data Directory */

static bbs_rc_t host_plugin_data_dir(const char* plugin_id, char* out, size_t out_sz) {
  if (!plugin_id || !out || out_sz == 0) return BBS_EINVAL;
  
  /* Create data/plugins/<plugin_id>/ directory */
  char path[512];
  snprintf(path, sizeof(path), "data/plugins");
  mkdir(path, 0755);
  
  snprintf(path, sizeof(path), "data/plugins/%s", plugin_id);
  mkdir(path, 0755);
  
  snprintf(out, out_sz, "%s", path);
  return BBS_OK;
}

/* Global Host API Instance */

static bbs_host_api_t g_host_api = {
  .abi_version = BBS_PLUGIN_ABI_VERSION,
  .size = sizeof(bbs_host_api_t),
  .magic = BBS_PLUGIN_MAGIC,
  
  .log = host_log,
  
  .io = &g_host_io,
  .sched = &g_host_sched,
  
  .session_username = host_session_username,
  .session_user_id = host_session_user_id,
  .session_user_flags = host_session_user_flags,
  .session_remote_addr = host_session_remote_addr,
  
  .kv_get = host_kv_get,
  .kv_set = host_kv_set,
  
  .plugin_data_dir = host_plugin_data_dir,
  
  .reserved = {NULL}
};

const bbs_host_api_t* plugin_host_api_get(void) {
  return &g_host_api;
}

void plugin_host_api_shutdown(void) {
  plugin_sched_shutdown();
}
