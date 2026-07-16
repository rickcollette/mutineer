/*
 * plugin_host_api.c - Host API Implementation for Plugins
 *
 * This file implements the bbs_host_api_t interface that plugins use
 * to interact with the BBS host.
 */

#include "bbs_config.h"
#include "bbs_db.h"
#include "bbs_log.h"
#include "bbs_plugin_api.h"
#include "bbs_session.h"
#include "bbs_util.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Cast opaque session to internal Session type */
#define TO_SESSION(s) ((Session *)(s))

static char g_plugin_data_root[512] = "data/plugins";
static char g_plugin_kv_root[512] = "data/plugin_kv";

static bool plugin_safe_identifier(const char *value, size_t max_len) {
  if (!value || !value[0])
    return false;
  size_t len = strlen(value);
  if (len == 0 || len > max_len)
    return false;
  if (!strcmp(value, ".") || !strcmp(value, ".."))
    return false;
  if (strstr(value, ".."))
    return false;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)value[i];
    if (!(isalnum(c) || c == '.' || c == '_' || c == '-'))
      return false;
  }
  return true;
}

static bool path_under_root(const char *root, const char *path) {
  if (!root || !path)
    return false;
  char real_root[PATH_MAX];
  char real_path[PATH_MAX];
  if (!realpath(root, real_root) || !realpath(path, real_path))
    return false;
  size_t n = strlen(real_root);
  return strncmp(real_root, real_path, n) == 0 &&
         (real_path[n] == '\0' || real_path[n] == '/');
}

static bool child_path_under_root(const char *root, const char *parent,
                                  const char *child, char *out, size_t out_sz) {
  if (!root || !parent || !child || !out || out_sz == 0)
    return false;
  char real_root[PATH_MAX];
  char real_parent[PATH_MAX];
  if (!realpath(root, real_root) || !realpath(parent, real_parent))
    return false;
  size_t n = strlen(real_root);
  if (strncmp(real_root, real_parent, n) != 0 ||
      (real_parent[n] != '\0' && real_parent[n] != '/')) {
    return false;
  }
  path_join(real_parent, child, out, out_sz);
  return true;
}

void plugin_host_api_configure(const BbsConfig *cfg) {
  const char *data_root = (cfg && cfg->data_path[0]) ? cfg->data_path : "data";
  snprintf(g_plugin_data_root, sizeof(g_plugin_data_root), "%s/plugins",
           data_root);
  snprintf(g_plugin_kv_root, sizeof(g_plugin_kv_root), "%s/plugin_kv",
           data_root);
}

/* ============================================================================
 * PLUGIN TASK SCHEDULER
 * ============================================================================
 */

#define PLUGIN_TASK_MAX 64

typedef struct {
  uint64_t id;
  bbs_task_fn fn;
  void *user;
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

static void *task_thread_fn(void *arg) {
  (void)arg;
  pthread_mutex_lock(&g_task_mutex);
  while (g_task_running) {
    uint64_t now = now_ms();
    uint64_t next_wake = now + 1000;

    for (int i = 0; i < PLUGIN_TASK_MAX; i++) {
      if (!g_tasks[i].active)
        continue;

      if (g_tasks[i].run_at_ms <= now) {
        bbs_task_fn fn = g_tasks[i].fn;
        void *user = g_tasks[i].user;
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
    if (wait_ms > 1000)
      wait_ms = 1000;
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

static bbs_rc_t host_io_write(bbs_session_t *s, const void *buf, size_t n) {
  Session *sess = TO_SESSION(s);
  if (!sess || sess->fd < 0)
    return BBS_EIO;
  if (!buf || n == 0)
    return BBS_OK;

  const char *text = (const char *)buf;
  size_t start = 0;
  for (size_t i = 0; i < n; i++) {
    if (text[i] != '\n' || (i > 0 && text[i - 1] == '\r'))
      continue;
    if (i > start && !fd_write_all(sess->fd, text + start, i - start))
      return BBS_EIO;
    if (!fd_write_all(sess->fd, "\r\n", 2))
      return BBS_EIO;
    start = i + 1;
  }
  if (start < n && !fd_write_all(sess->fd, text + start, n - start))
    return BBS_EIO;
  return BBS_OK;
}

static bbs_rc_t host_io_printf(bbs_session_t *s, const char *fmt, ...) {
  Session *sess = TO_SESSION(s);
  if (!sess || sess->fd < 0)
    return BBS_EIO;
  if (!fmt)
    return BBS_EINVAL;

  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  int len = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (len < 0)
    return BBS_EINTERNAL;
  if (len == 0)
    return BBS_OK;

  size_t to_write = (size_t)len;
  if (to_write > sizeof(buf) - 1)
    to_write = sizeof(buf) - 1;

  return host_io_write(s, buf, to_write);
}

static bbs_rc_t host_io_readline(bbs_session_t *s, char *out, size_t out_sz,
                                 int echo) {
  Session *sess = TO_SESSION(s);
  if (!sess || sess->fd < 0)
    return BBS_EIO;
  if (!out || out_sz == 0)
    return BBS_EINVAL;

  out[0] = '\0';

  int timeout =
      sess->cfg.idle_timeout_sec > 0 ? sess->cfg.idle_timeout_sec : 60;
  int rc = session_readline_echo(sess, (uint8_t *)out, out_sz, timeout,
                                 echo ? 1 : 0);

  if (rc == -2)
    return BBS_ETIMEOUT;
  if (rc < 0)
    return BBS_EIO;
  (void)echo;
  send_str(sess, "\r\n");
  return BBS_OK;
}

static bbs_rc_t host_io_cls(bbs_session_t *s) {
  Session *sess = TO_SESSION(s);
  if (!sess || sess->fd < 0)
    return BBS_EIO;

  /* ANSI clear screen and home cursor */
  const char *cls = "\x1b[2J\x1b[H";
  if (!fd_write_all(sess->fd, cls, strlen(cls))) {
    return BBS_EIO;
  }
  return BBS_OK;
}

static const bbs_io_t g_host_io = {.write = host_io_write,
                                   .printf = host_io_printf,
                                   .readline = host_io_readline,
                                   .cls = host_io_cls};

/* Scheduler Implementation */

static bbs_rc_t host_sched_enqueue(bbs_task_fn fn, void *user) {
  if (!fn)
    return BBS_EINVAL;

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

static bbs_rc_t host_sched_after_ms(uint64_t delay_ms, bbs_task_fn fn,
                                    void *user, uint64_t *task_id_out) {
  if (!fn)
    return BBS_EINVAL;

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
      if (task_id_out)
        *task_id_out = id;
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

static const bbs_sched_t g_host_sched = {.enqueue = host_sched_enqueue,
                                         .after_ms = host_sched_after_ms,
                                         .cancel = host_sched_cancel};

/* Logging */

static void host_log(bbs_log_level_t lvl, const char *subsystem,
                     const char *msg) {
  const char *sub = subsystem ? subsystem : "plugin";
  const char *m = msg ? msg : "";

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

static const char *host_session_username(bbs_session_t *s) {
  Session *sess = TO_SESSION(s);
  if (!sess)
    return "";
  return sess->user.handle;
}

static uint32_t host_session_user_id(bbs_session_t *s) {
  Session *sess = TO_SESSION(s);
  if (!sess)
    return 0;
  return (uint32_t)sess->user.id;
}

static uint32_t host_session_user_flags(bbs_session_t *s) {
  Session *sess = TO_SESSION(s);
  if (!sess)
    return 0;
  return (uint32_t)sess->user.flags;
}

static uint32_t host_session_restriction_flags(bbs_session_t *s) {
  Session *sess = TO_SESSION(s);
  return sess ? (uint32_t)sess->user.ac_flags : 0;
}

static int host_session_node_number(bbs_session_t *s) {
  Session *sess = TO_SESSION(s);
  return sess ? sess->node_num : 0;
}
static int host_session_security_level(bbs_session_t *s) {
  Session *sess = TO_SESSION(s);
  return sess ? sess->user.level : 0;
}
static int host_session_has_ansi(bbs_session_t *s) {
  Session *sess = TO_SESSION(s);
  return sess ? sess->ansi : 0;
}
static int host_session_is_alive(bbs_session_t *s) {
  Session *sess = TO_SESSION(s);
  return sess ? (sess->alive && sess->fd >= 0) : 0;
}
static bbs_rc_t host_session_terminal_size(bbs_session_t *s, uint16_t *cols,
                                           uint16_t *rows) {
  Session *sess = TO_SESSION(s);
  if (!sess || !cols || !rows)
    return BBS_EINVAL;
  *cols = (uint16_t)(sess->tn.cols > 0 ? sess->tn.cols : 80);
  *rows = (uint16_t)(sess->tn.rows > 0 ? sess->tn.rows : 24);
  return BBS_OK;
}
static bbs_user_resolve_result_t
host_user_resolve(bbs_session_t *s, const char *q, bbs_user_ref_t *out) {
  Session *sess = TO_SESSION(s);
  if (!sess || !sess->db || !q || !out)
    return BBS_USER_NOT_FOUND;
  DbUser u;
  int rc = db_user_resolve_prefix(sess->db, q, &u);
  if (rc <= 0)
    return rc < 0 ? BBS_USER_AMBIGUOUS : BBS_USER_NOT_FOUND;
  out->user_id = (uint32_t)u.id;
  snprintf(out->handle, sizeof out->handle, "%s", u.handle);
  return rc == 1 ? BBS_USER_EXACT : BBS_USER_UNIQUE_PREFIX;
}
static bbs_rc_t host_user_lookup_id(bbs_session_t *s, uint32_t id,
                                    bbs_user_ref_t *out) {
  Session *sess = TO_SESSION(s);
  DbUser u;
  if (!sess || !sess->db || !out || !db_user_fetch_id(sess->db, (int)id, &u))
    return BBS_EINVAL;
  out->user_id = id;
  snprintf(out->handle, sizeof out->handle, "%s", u.handle);
  return BBS_OK;
}

static bbs_rc_t host_readline_timed(bbs_session_t *s, char *out, size_t out_sz,
                                    int echo, uint32_t timeout_ms) {
  Session *sess = TO_SESSION(s);
  if (!sess || sess->fd < 0)
    return BBS_EIO;
  if (!out || out_sz == 0)
    return BBS_EINVAL;
  out[0] = '\0';
  int seconds = (int)((timeout_ms + 999u) / 1000u);
  if (seconds < 1)
    seconds = 1;
  int rc = session_readline_echo(sess, (uint8_t *)out, out_sz, seconds,
                                 echo ? 1 : 0);
  if (rc == -2)
    return BBS_ETIMEOUT;
  if (rc < 0)
    return BBS_EIO;
  send_str(sess, "\r\n");
  return BBS_OK;
}

static void host_audit(bbs_session_t *s, const char *action,
                       const char *detail) {
  Session *sess = TO_SESSION(s);
  log_audit(sess ? sess->user.handle : "plugin", action ? action : "plugin",
            detail ? detail : "");
}

static const char *host_session_remote_addr(bbs_session_t *s) {
  Session *sess = TO_SESSION(s);
  if (!sess)
    return "";
  return sess->ip;
}

/* Key/Value Store */

static bbs_rc_t host_kv_get(const char *ns, const char *key, char *out,
                            size_t out_sz) {
  if (!ns || !key || !out || out_sz == 0)
    return BBS_EINVAL;
  if (!plugin_safe_identifier(ns, 96) || !plugin_safe_identifier(key, 160))
    return BBS_EINVAL;

  out[0] = '\0';

  if (!bbs_mkdir_p(g_plugin_kv_root, 0755))
    return BBS_EIO;
  char path[512];
  path_join(g_plugin_kv_root, ns, path, sizeof(path));
  if (!path_under_root(g_plugin_kv_root, path))
    return BBS_EINVAL;

  char filepath[768];
  if (!child_path_under_root(g_plugin_kv_root, path, key, filepath,
                             sizeof(filepath)))
    return BBS_EIO;

  FILE *f = fopen(filepath, "r");
  if (!f)
    return BBS_EINVAL; /* Key not found */

  size_t n = fread(out, 1, out_sz - 1, f);
  out[n] = '\0';
  fclose(f);

  return BBS_OK;
}

static bbs_rc_t host_kv_set(const char *ns, const char *key, const char *val) {
  if (!ns || !key)
    return BBS_EINVAL;
  if (!plugin_safe_identifier(ns, 96) || !plugin_safe_identifier(key, 160))
    return BBS_EINVAL;

  /* Create namespace directory if needed */
  char path[512];
  snprintf(path, sizeof(path), "%s", g_plugin_kv_root);
  if (!bbs_mkdir_p(path, 0755))
    return BBS_EIO;

  path_join(g_plugin_kv_root, ns, path, sizeof(path));
  if (!bbs_mkdir_p(path, 0755))
    return BBS_EIO;
  if (!path_under_root(g_plugin_kv_root, path))
    return BBS_EIO;

  char filepath[768];
  if (!child_path_under_root(g_plugin_kv_root, path, key, filepath,
                             sizeof(filepath)))
    return BBS_EIO;

  if (!val) {
    /* Delete key */
    unlink(filepath);
    return BBS_OK;
  }

  FILE *f = fopen(filepath, "w");
  if (!f)
    return BBS_EIO;

  fwrite(val, 1, strlen(val), f);
  fclose(f);

  return BBS_OK;
}

/* Plugin Data Directory */

static bbs_rc_t host_plugin_data_dir(const char *plugin_id, char *out,
                                     size_t out_sz) {
  if (!plugin_id || !out || out_sz == 0)
    return BBS_EINVAL;
  if (!plugin_safe_identifier(plugin_id, 96))
    return BBS_EINVAL;

  char path[512];
  snprintf(path, sizeof(path), "%s", g_plugin_data_root);
  if (!bbs_mkdir_p(path, 0755))
    return BBS_EIO;

  path_join(g_plugin_data_root, plugin_id, path, sizeof(path));
  if (!bbs_mkdir_p(path, 0755))
    return BBS_EIO;
  if (!path_under_root(g_plugin_data_root, path))
    return BBS_EIO;

  snprintf(out, out_sz, "%s", path);
  return BBS_OK;
}

static bbs_rc_t host_plugin_config_get(const char *plugin_id, const char *key,
                                       char *out, size_t out_sz) {
  if (!plugin_safe_identifier(plugin_id, 96) ||
      !plugin_safe_identifier(key, 96) || !out || out_sz == 0)
    return BBS_EINVAL;
  char dir[512], path[768], line[1024];
  if (host_plugin_data_dir(plugin_id, dir, sizeof(dir)) != BBS_OK)
    return BBS_EIO;
  path_join(dir, "mts.conf", path, sizeof(path));
  FILE *f = fopen(path, "r");
  if (!f)
    return BBS_EINVAL;
  bbs_rc_t result = BBS_EINVAL;
  while (fgets(line, sizeof(line), f)) {
    char *p = line;
    while (isspace((unsigned char)*p))
      p++;
    if (!*p || *p == '#' || *p == ';')
      continue;
    char *eq = strchr(p, '=');
    if (!eq)
      continue;
    *eq++ = '\0';
    char *end = p + strlen(p);
    while (end > p && isspace((unsigned char)end[-1]))
      *--end = '\0';
    while (isspace((unsigned char)*eq))
      eq++;
    end = eq + strlen(eq);
    while (end > eq && isspace((unsigned char)end[-1]))
      *--end = '\0';
    if (!strcmp(p, key)) {
      snprintf(out, out_sz, "%s", eq);
      result = BBS_OK;
      break;
    }
  }
  fclose(f);
  return result;
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

    .reserved = {NULL},
    .session_restriction_flags = host_session_restriction_flags,
    .session_node_number = host_session_node_number,
    .session_security_level = host_session_security_level,
    .session_has_ansi = host_session_has_ansi,
    .session_is_alive = host_session_is_alive,
    .readline_timed = host_readline_timed,
    .audit = host_audit,
    .plugin_config_get = host_plugin_config_get,
    .session_terminal_size = host_session_terminal_size,
    .user_resolve = host_user_resolve,
    .user_lookup_id = host_user_lookup_id};

const bbs_host_api_t *plugin_host_api_get(void) { return &g_host_api; }

void plugin_host_api_shutdown(void) { plugin_sched_shutdown(); }
