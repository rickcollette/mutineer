#include "bbs_acs.h"
#include "bbs_auth.h"
#include "bbs_console_service.h"
#include "bbs_hash.h"
#include "bbs_log.h"
#include "bbs_plugin_loader.h"
#include "bbs_process.h"
#include "bbs_session.h"
#include "bbs_util.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define CONSOLE_MAX_LINE 8192
#define CONSOLE_MAX_JSON 65536

extern volatile sig_atomic_t g_stop;

typedef struct ConsoleServiceCtx {
  BbsConfig cfg;
  int listen_fd;
  int running;
  pthread_t thread;
} ConsoleServiceCtx;

typedef struct ConsoleClient {
  int fd;
  BbsConfig cfg;
  BbsDb *db;
  DbUser user;
  int authenticated;
  char ip[64];
} ConsoleClient;

static pthread_mutex_t g_console_clients_mu = PTHREAD_MUTEX_INITIALIZER;
static ConsoleClient *g_console_clients[64];

static ConsoleServiceCtx g_console = {.listen_fd = -1};

static int make_listener(const char *bind_ip, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;

  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) {
    close(fd);
    return -1;
  }
  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }
  if (listen(fd, 16) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static void json_escape(char *out, size_t cap, const char *s) {
  size_t o = 0;
  if (!out || cap == 0)
    return;
  out[o++] = '"';
  for (const unsigned char *p = (const unsigned char *)(s ? s : "");
       *p && o + 8 < cap; p++) {
    if (*p == '"' || *p == '\\') {
      out[o++] = '\\';
      out[o++] = (char)*p;
    } else if (*p == '\n') {
      out[o++] = '\\';
      out[o++] = 'n';
    } else if (*p == '\r') {
      out[o++] = '\\';
      out[o++] = 'r';
    } else if (*p == '\t') {
      out[o++] = '\\';
      out[o++] = 't';
    } else if (*p < 0x20) {
      o += (size_t)snprintf(out + o, cap - o, "\\u%04x", *p);
    } else {
      out[o++] = (char)*p;
    }
  }
  if (o + 2 <= cap)
    out[o++] = '"';
  out[o] = '\0';
}

static void send_json(ConsoleClient *cc, const char *json) {
  if (!cc || cc->fd < 0 || !json)
    return;
  fd_write_all(cc->fd, json, strlen(json));
  fd_write_all(cc->fd, "\n", 1);
}

static void console_clients_add(ConsoleClient *cc) {
  pthread_mutex_lock(&g_console_clients_mu);
  for (int i = 0; i < 64; i++) {
    if (!g_console_clients[i]) {
      g_console_clients[i] = cc;
      break;
    }
  }
  pthread_mutex_unlock(&g_console_clients_mu);
}

static void console_clients_remove(ConsoleClient *cc) {
  pthread_mutex_lock(&g_console_clients_mu);
  for (int i = 0; i < 64; i++)
    if (g_console_clients[i] == cc)
      g_console_clients[i] = NULL;
  pthread_mutex_unlock(&g_console_clients_mu);
}

static void console_broadcast_event(const char *json) {
  pthread_mutex_lock(&g_console_clients_mu);
  for (int i = 0; i < 64; i++)
    if (g_console_clients[i] && g_console_clients[i]->authenticated)
      send_json(g_console_clients[i], json);
  pthread_mutex_unlock(&g_console_clients_mu);
}

static void send_ok(ConsoleClient *cc, const char *id, const char *payload) {
  char idj[128];
  json_escape(idj, sizeof(idj), id ? id : "");
  if (!cc || cc->fd < 0)
    return;
  fd_write_all(cc->fd, "{\"id\":", 6);
  fd_write_all(cc->fd, idj, strlen(idj));
  fd_write_all(cc->fd, ",\"ok\":true", 10);
  if (payload && payload[0]) {
    fd_write_all(cc->fd, ",", 1);
    fd_write_all(cc->fd, payload, strlen(payload));
  }
  fd_write_all(cc->fd, "}\n", 2);
}

static void send_error(ConsoleClient *cc, const char *id, const char *err) {
  char idj[128], errj[512], out[1024];
  json_escape(idj, sizeof(idj), id ? id : "");
  json_escape(errj, sizeof(errj), err ? err : "error");
  snprintf(out, sizeof(out), "{\"id\":%s,\"ok\":false,\"error\":%s}", idj,
           errj);
  send_json(cc, out);
}

static int read_line_timeout(int fd, char *buf, size_t cap, int timeout_sec) {
  size_t o = 0;
  while (o + 1 < cap) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    struct timeval tv = {.tv_sec = timeout_sec > 0 ? timeout_sec : 600,
                         .tv_usec = 0};
    int sel = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (sel == 0)
      return -2;
    if (sel < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    char c;
    ssize_t n = read(fd, &c, 1);
    if (n == 0)
      return 0;
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (c == '\n')
      break;
    if (c != '\r')
      buf[o++] = c;
  }
  buf[o] = '\0';
  return (int)o;
}

static const char *json_value_start(const char *json, const char *key) {
  static char pattern[96];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *p = strstr(json, pattern);
  if (!p)
    return NULL;
  p += strlen(pattern);
  while (*p && isspace((unsigned char)*p))
    p++;
  if (*p != ':')
    return NULL;
  p++;
  while (*p && isspace((unsigned char)*p))
    p++;
  return p;
}

static int json_get_string(const char *json, const char *key, char *out,
                           size_t cap) {
  const char *p = json_value_start(json, key);
  if (!p || *p != '"' || !out || cap == 0)
    return 0;
  p++;
  size_t o = 0;
  while (*p && *p != '"' && o + 1 < cap) {
    if (*p == '\\' && p[1]) {
      p++;
      if (*p == 'n')
        out[o++] = '\n';
      else if (*p == 'r')
        out[o++] = '\r';
      else if (*p == 't')
        out[o++] = '\t';
      else
        out[o++] = *p;
      p++;
    } else {
      out[o++] = *p++;
    }
  }
  out[o] = '\0';
  return 1;
}

static int json_get_int(const char *json, const char *key, int fallback) {
  const char *p = json_value_start(json, key);
  if (!p)
    return fallback;
  return atoi(p);
}

static bool json_looks_valid(const char *line) {
  int depth = 0;
  bool in_str = false;
  bool esc = false;
  bool saw_colon = false;
  for (const char *p = line; *p; p++) {
    char c = *p;
    if (in_str) {
      if (esc)
        esc = false;
      else if (c == '\\')
        esc = true;
      else if (c == '"')
        in_str = false;
      continue;
    }
    if (c == '"')
      in_str = true;
    else if (c == '{')
      depth++;
    else if (c == '}')
      depth--;
    else if (c == ':')
      saw_colon = true;
    if (depth < 0)
      return false;
  }
  return depth == 0 && !in_str && saw_colon && line[0] == '{';
}

static bool valid_node_num(int node) { return node > 0 && node <= 256; }

static bool require_valid_node(ConsoleClient *cc, const char *id, int node) {
  if (valid_node_num(node))
    return true;
  send_error(cc, id, "invalid node");
  return false;
}

static int64_t disk_free_kb(const char *path) {
  struct statvfs st;
  if (statvfs(path ? path : ".", &st) != 0)
    return 0;
  return (int64_t)(st.f_bavail * st.f_frsize) / 1024;
}

static void os_name(char *out, size_t cap) {
  struct utsname un;
  const char *name = uname(&un) == 0 ? un.sysname : "Unknown";
  if (!out || cap == 0)
    return;
  size_t n = strlen(name);
  if (n >= cap)
    n = cap - 1;
  memcpy(out, name, n);
  out[n] = '\0';
}

static void payload_stats(ConsoleClient *cc, char *out, size_t cap) {
  DbDailyStats daily;
  DbSystemTotals totals;
  DbUser sysop;
  char os[64];
  memset(&daily, 0, sizeof(daily));
  memset(&totals, 0, sizeof(totals));
  memset(&sysop, 0, sizeof(sysop));
  db_daily_stats_get(cc->db, &daily);
  db_system_totals_get(cc->db, &totals);
  db_user_fetch(cc->db, "sysop", &sysop);
  os_name(os, sizeof(os));
  snprintf(
      out, cap,
      "\"stats\":{\"calls_today\":%d,\"posts_today\":%d,\"emails_today\":%d,"
      "\"newusers_today\":%d,\"feedback_today\":%d,\"uploads_today\":%d,"
      "\"downloads_today\":%d,\"ul_kb_today\":%lld,\"dl_kb_today\":%lld,"
      "\"minutes_today\":%d,\"errors\":%d,\"total_calls\":%d,\"total_posts\":%"
      "d,"
      "\"total_uploads\":%d,\"total_downloads\":%d,\"days_online\":%d,"
      "\"total_users\":%d,\"node_num\":0,\"mail_waiting\":%d,"
      "\"disk_free_kb\":%lld,\"os\":\"%s\"}",
      daily.calls, daily.posts, daily.emails, daily.newusers, daily.feedback,
      daily.uploads, daily.downloads, (long long)daily.ul_kb,
      (long long)daily.dl_kb, daily.minutes, daily.errors, totals.total_calls,
      totals.total_posts, totals.total_uploads, totals.total_downloads,
      totals.days_online, totals.total_users, sysop.smw,
      (long long)disk_free_kb(cc->cfg.data_path), os);
}

static void payload_nodes(ConsoleClient *cc, char *out, size_t cap) {
  DbNode nodes[64];
  int n = db_node_list(cc->db, nodes, 64);
  size_t o = 0;
  o += (size_t)snprintf(out + o, cap - o, "\"nodes\":[");
  for (int i = 0; i < n && o + 256 < cap; i++) {
    char h[160], st[64], ip[160], act[256];
    json_escape(h, sizeof(h), nodes[i].handle);
    json_escape(st, sizeof(st), nodes[i].status);
    json_escape(ip, sizeof(ip), nodes[i].ip);
    json_escape(act, sizeof(act), nodes[i].activity);
    o += (size_t)snprintf(
        out + o, cap - o,
        "%s{\"node\":%d,\"user_id\":%d,\"handle\":%s,\"status\":%s,"
        "\"ip\":%s,\"activity\":%s,\"locked\":%s}",
        i ? "," : "", nodes[i].node_num, nodes[i].user_id, h, st, ip, act,
        online_node_is_locked(nodes[i].node_num) ? "true" : "false");
  }
  snprintf(out + o, cap - o, "]");
}

static void payload_history(ConsoleClient *cc, char *out, size_t cap) {
  DbDailyStats rows[30];
  int n = db_history_list(cc->db, rows, 30);
  size_t o = 0;
  o += (size_t)snprintf(out + o, cap - o, "\"history\":[");
  for (int i = 0; i < n && o + 256 < cap; i++) {
    char date[64];
    json_escape(date, sizeof(date), rows[i].date);
    o += (size_t)snprintf(
        out + o, cap - o,
        "%s{\"date\":%s,\"calls\":%d,\"posts\":%d,\"emails\":%d,"
        "\"uploads\":%d,\"downloads\":%d,\"newusers\":%d}",
        i ? "," : "", date, rows[i].calls, rows[i].posts, rows[i].emails,
        rows[i].uploads, rows[i].downloads, rows[i].newusers);
  }
  snprintf(out + o, cap - o, "]");
}

static void payload_callers(ConsoleClient *cc, char *out, size_t cap) {
  DbCallHistory rows[30];
  int n = db_call_history_list(cc->db, rows, 30);
  size_t o = 0;
  o += (size_t)snprintf(out + o, cap - o, "\"callers\":[");
  for (int i = 0; i < n && o + 320 < cap; i++) {
    char handle[160], ip[160], login[80], logout[80];
    json_escape(handle, sizeof(handle), rows[i].handle);
    json_escape(ip, sizeof(ip), rows[i].ip_address);
    json_escape(login, sizeof(login), rows[i].login_at);
    json_escape(logout, sizeof(logout), rows[i].logout_at);
    o += (size_t)snprintf(
        out + o, cap - o,
        "%s{\"id\":%d,\"user_id\":%d,\"handle\":%s,\"node\":%d,"
        "\"login_at\":%s,\"logout_at\":%s,\"duration_min\":%d,\"ip\":%s}",
        i ? "," : "", rows[i].id, rows[i].user_id, handle, rows[i].node_num,
        login, logout, rows[i].duration_min, ip);
  }
  snprintf(out + o, cap - o, "]");
}

static void payload_logs(ConsoleClient *cc, char *out, size_t cap) {
  char ring[40][256];
  int count = 0;
  FILE *f = fopen(cc->cfg.logs_path, "r");
  if (f) {
    while (fgets(ring[count % 40], sizeof(ring[0]), f))
      count++;
    fclose(f);
  }
  size_t o = 0;
  o += (size_t)snprintf(out + o, cap - o, "\"lines\":[");
  int start = count > 40 ? count - 40 : 0;
  for (int i = start; i < count && o + 512 < cap; i++) {
    char line[600];
    json_escape(line, sizeof(line), ring[i % 40]);
    o += (size_t)snprintf(out + o, cap - o, "%s%s", i > start ? "," : "", line);
  }
  snprintf(out + o, cap - o, "]");
}

static void payload_inspect(ConsoleClient *cc, int node, char *out,
                            size_t cap) {
  DbNode nodes[64];
  int n = db_node_list(cc->db, nodes, 64);
  DbNode *found = NULL;
  for (int i = 0; i < n; i++)
    if (nodes[i].node_num == node)
      found = &nodes[i];

  size_t o = 0;
  o += (size_t)snprintf(out + o, cap - o, "\"node\":{\"node\":%d,", node);
  if (!found) {
    o += (size_t)snprintf(out + o, cap - o,
                          "\"status\":\"inactive\",\"handle\":\"\",\"ip\":\"\","
                          "\"activity\":\"\"");
  } else {
    char h[160], st[80], ip[160], act[256];
    json_escape(h, sizeof(h), found->handle);
    json_escape(st, sizeof(st), found->status);
    json_escape(ip, sizeof(ip), found->ip);
    json_escape(act, sizeof(act), found->activity);
    o += (size_t)snprintf(
        out + o, cap - o,
        "\"status\":%s,\"handle\":%s,\"ip\":%s,\"activity\":%s", st, h, ip,
        act);
  }
  Session *s = online_get_node(node);
  if (s) {
    char h[160], ip[160];
    json_escape(h, sizeof(h), s->user.handle);
    json_escape(ip, sizeof(ip), s->ip);
    o += (size_t)snprintf(
        out + o, cap - o,
        ",\"session\":{\"user_id\":%d,\"handle\":%s,\"level\":%d,"
        "\"time_left_min\":%d,\"ip\":%s}",
        s->user.id, h, s->user.level, s->time_left_min, ip);
  }
  snprintf(out + o, cap - o, ",\"locked\":%s}",
           online_node_is_locked(node) ? "true" : "false");
}

static bool authenticate_console(ConsoleClient *cc, const char *user,
                                 const char *password) {
  DbUser u;
  memset(&u, 0, sizeof(u));
  if (bbs_login_throttled(&cc->cfg, cc->ip, user)) {
    log_audit(user && user[0] ? user : "?", "console_login_throttled", cc->ip);
    return false;
  }
  if (!db_user_fetch(cc->db, user, &u)) {
    bbs_login_record(&cc->cfg, cc->ip, user, false);
    log_audit(user && user[0] ? user : "?", "console_login_failed", cc->ip);
    return false;
  }
  if (!pw_hash_verify(password, u.pw_hash)) {
    bbs_login_record(&cc->cfg, cc->ip, user, false);
    log_audit(u.handle, "console_login_failed", cc->ip);
    return false;
  }

  Session fake;
  memset(&fake, 0, sizeof(fake));
  fake.cfg = cc->cfg;
  fake.db = cc->db;
  fake.user = u;
  if (!acs_allows(&fake, "+A")) {
    bbs_login_record(&cc->cfg, cc->ip, user, false);
    log_audit(u.handle, "console_login_non_sysop", cc->ip);
    return false;
  }

  bbs_login_record(&cc->cfg, cc->ip, user, true);
  cc->user = u;
  cc->authenticated = 1;
  log_audit(u.handle, "console_login", cc->ip);
  return true;
}

static void emit_node_event(const char *kind, int node) {
  char out[256];
  snprintf(out, sizeof(out), "{\"event\":\"node.%s\",\"node\":%d}", kind, node);
  console_broadcast_event(out);
}

static void passthrough_action(ConsoleClient *cc, const char *action) {
  Session s;
  memset(&s, 0, sizeof(s));
  s.fd = cc->fd;
  s.cfg = cc->cfg;
  s.db = cc->db;
  s.user = cc->user;
  s.ansi = 1;
  s.alive = 1;
  s.started_at = time(NULL);
  s.time_left_min = cc->user.time_limit_min > 0
                        ? cc->user.time_limit_min
                        : cc->cfg.session_time_limit_min;
  snprintf(s.ip, sizeof(s.ip), "%s", cc->ip);
  pthread_mutex_init(&s.chat_inbox_lock, NULL);

  send_json(cc, "{\"event\":\"passthrough.begin\"}");
  bbs_handle_action(&s, action);
  send_json(cc, "{\"event\":\"passthrough.end\"}");
  pthread_mutex_destroy(&s.chat_inbox_lock);
}

static void run_shell(ConsoleClient *cc, const char *id) {
  if (!cc->cfg.wfc_shell_enabled || !cc->cfg.wfc_shell_command[0]) {
    send_error(cc, id, "shell disabled");
    return;
  }
  char errbuf[256] = {0};
  char **argv = NULL;
  if (!bbs_argv_parse_template(cc->cfg.wfc_shell_command, NULL, &argv, errbuf,
                               sizeof(errbuf))) {
    send_error(cc, id, errbuf[0] ? errbuf : "shell command rejected");
    return;
  }
  send_json(cc, "{\"event\":\"passthrough.begin\"}");
  BbsProcessResult result;
  bbs_exec_argv(argv, "console-shell", NULL, cc->fd, cc->fd, cc->fd, 0, &result,
                errbuf, sizeof(errbuf));
  bbs_argv_free(argv);
  send_json(cc, "{\"event\":\"passthrough.end\"}");
  send_ok(cc, id, "\"exit\":true");
}

static void send_snapshot(ConsoleClient *cc) {
  char stats[4096], nodes[12000], out[CONSOLE_MAX_JSON];
  payload_stats(cc, stats, sizeof(stats));
  payload_nodes(cc, nodes, sizeof(nodes));
  snprintf(out, sizeof(out), "{\"event\":\"snapshot\",%s,%s}", stats, nodes);
  send_json(cc, out);
}

static void handle_command(ConsoleClient *cc, const char *line) {
  char id[64] = {0}, cmd[64] = {0}, payload[CONSOLE_MAX_JSON];
  if (!json_looks_valid(line)) {
    send_error(cc, "", "malformed_json");
    return;
  }
  json_get_string(line, "id", id, sizeof(id));
  json_get_string(line, "cmd", cmd, sizeof(cmd));
  if (!cmd[0])
    json_get_string(line, "type", cmd, sizeof(cmd));

  if (!strcmp(cmd, "hello")) {
    send_ok(cc, id, "\"server\":\"mutineer-console\",\"version\":1");
    return;
  }

  if (!strcmp(cmd, "login")) {
    char user[64] = {0}, password[128] = {0};
    json_get_string(line, "user", user, sizeof(user));
    json_get_string(line, "password", password, sizeof(password));
    if (authenticate_console(cc, user, password)) {
      send_ok(cc, id, "\"authenticated\":true");
      send_snapshot(cc);
    } else {
      send_error(cc, id, "authentication failed");
    }
    return;
  }

  if (!cc->authenticated) {
    send_error(cc, id, "not authenticated");
    return;
  }

  if (!strcmp(cmd, "stats.get")) {
    payload_stats(cc, payload, sizeof(payload));
    send_ok(cc, id, payload);
  } else if (!strcmp(cmd, "nodes.list")) {
    payload_nodes(cc, payload, sizeof(payload));
    send_ok(cc, id, payload);
  } else if (!strcmp(cmd, "node.inspect")) {
    int node = json_get_int(line, "node", 0);
    if (!require_valid_node(cc, id, node))
      return;
    payload_inspect(cc, node, payload, sizeof(payload));
    send_ok(cc, id, payload);
  } else if (!strcmp(cmd, "node.kick")) {
    int node = json_get_int(line, "node", 0);
    if (!require_valid_node(cc, id, node))
      return;
    Session *target = online_get_node(node);
    if (target)
      plugin_loader_dispatch_event(BBS_EVT_FORCED_DISCONNECT,
                                   (bbs_session_t *)target, NULL);
    bool ok = online_mark_node_dead(
        node, NULL, "\r\nYou have been disconnected by the sysop.\r\n");
    if (ok)
      db_node_upsert(cc->db, node, 0, "kicked", "console", cc->ip);
    if (ok)
      emit_node_event("kick", node);
    send_ok(cc, id, ok ? "\"kicked\":true" : "\"kicked\":false");
  } else if (!strcmp(cmd, "node.lock") || !strcmp(cmd, "node.unlock")) {
    int node = json_get_int(line, "node", 0);
    if (!require_valid_node(cc, id, node))
      return;
    bool lock = !strcmp(cmd, "node.lock");
    online_set_node_locked(node, lock);
    if (!db_node_lock_set(cc->db, node, lock, cc->user.handle)) {
      send_error(cc, id, db_last_error(cc->db));
      return;
    }
    db_node_upsert(cc->db, node, 0, lock ? "locked" : "idle", "console",
                   cc->ip);
    log_audit(cc->user.handle,
              lock ? "console_node_lock" : "console_node_unlock", cc->ip);
    emit_node_event(lock ? "lock" : "unlock", node);
    send_ok(cc, id, lock ? "\"locked\":true" : "\"locked\":false");
  } else if (!strcmp(cmd, "broadcast.send")) {
    char msg[512], full[700];
    json_get_string(line, "message", msg, sizeof(msg));
    snprintf(full, sizeof(full), "\r\n[SYSOP] %s\r\n", msg);
    online_broadcast(full);
    log_audit(cc->user.handle, "console_broadcast", msg);
    console_broadcast_event("{\"event\":\"broadcast\"}");
    send_ok(cc, id, "\"sent\":true");
  } else if (!strcmp(cmd, "callers.list")) {
    payload_callers(cc, payload, sizeof(payload));
    send_ok(cc, id, payload);
  } else if (!strcmp(cmd, "history.list")) {
    payload_history(cc, payload, sizeof(payload));
    send_ok(cc, id, payload);
  } else if (!strcmp(cmd, "logs.tail")) {
    payload_logs(cc, payload, sizeof(payload));
    send_ok(cc, id, payload);
  } else if (!strcmp(cmd, "system.status")) {
    payload_stats(cc, payload, sizeof(payload));
    send_ok(cc, id, payload);
  } else if (!strcmp(cmd, "system.shutdown")) {
    g_stop = 1;
    console_broadcast_event("{\"event\":\"system.shutdown\"}");
    send_ok(cc, id, "\"shutdown\":true");
  } else if (!strcmp(cmd, "shell.run")) {
    run_shell(cc, id);
  } else if (!strcmp(cmd, "menu.session.start")) {
    char action[64] = {0};
    json_get_string(line, "action", action, sizeof(action));
    if (!action[0])
      send_error(cc, id, "missing action");
    else {
      send_ok(cc, id, "\"passthrough\":true");
      passthrough_action(cc, action);
    }
  } else {
    send_error(cc, id, "unknown command");
  }
}

static void *console_client_thread(void *arg) {
  ConsoleClient *cc = (ConsoleClient *)arg;
  console_clients_add(cc);
  char line[CONSOLE_MAX_LINE];
  for (;;) {
    int n = read_line_timeout(cc->fd, line, sizeof(line),
                              cc->cfg.console_idle_timeout_sec);
    if (n <= 0)
      break;
    handle_command(cc, line);
  }
  if (cc->db)
    db_close(cc->db);
  console_clients_remove(cc);
  close(cc->fd);
  free(cc);
  return NULL;
}

static void *console_service_thread(void *arg) {
  ConsoleServiceCtx *ctx = (ConsoleServiceCtx *)arg;
  while (ctx->running) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(ctx->listen_fd, &rfds);
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    int sel = select(ctx->listen_fd + 1, &rfds, NULL, NULL, &tv);
    if (sel <= 0)
      continue;

    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    int cfd = accept(ctx->listen_fd, (struct sockaddr *)&peer, &plen);
    if (cfd < 0)
      continue;

    ConsoleClient *cc = calloc(1, sizeof(*cc));
    if (!cc) {
      close(cfd);
      continue;
    }
    cc->fd = cfd;
    cc->cfg = ctx->cfg;
    inet_ntop(AF_INET, &peer.sin_addr, cc->ip, sizeof(cc->ip));
    cc->db = db_open(ctx->cfg.db_path);
    if (!cc->db) {
      close(cfd);
      free(cc);
      continue;
    }
    pthread_t th;
    if (pthread_create(&th, NULL, console_client_thread, cc) == 0)
      pthread_detach(th);
    else {
      db_close(cc->db);
      close(cfd);
      free(cc);
    }
  }
  return NULL;
}

void console_service_start(const BbsConfig *cfg, BbsDb *db) {
  (void)db;
  if (!cfg || !cfg->console_enabled)
    return;
  if (g_console.running)
    return;

  g_console.cfg = *cfg;
  if (g_console.cfg.console_idle_timeout_sec <= 0)
    g_console.cfg.console_idle_timeout_sec = 600;
  if (strcmp(g_console.cfg.console_bind, "127.0.0.1") &&
      strcmp(g_console.cfg.console_bind, "localhost")) {
    log_warn("console service binding to non-loopback address %s",
             g_console.cfg.console_bind);
  }
  g_console.listen_fd =
      make_listener(g_console.cfg.console_bind, g_console.cfg.console_port);
  if (g_console.listen_fd < 0) {
    log_error("console service bind failed on %s:%d",
              g_console.cfg.console_bind, g_console.cfg.console_port);
    return;
  }
  g_console.running = 1;
  if (pthread_create(&g_console.thread, NULL, console_service_thread,
                     &g_console) != 0) {
    close(g_console.listen_fd);
    g_console.listen_fd = -1;
    g_console.running = 0;
    log_error("console service thread start failed");
    return;
  }
  log_info("console service listening on %s:%d", g_console.cfg.console_bind,
           g_console.cfg.console_port);
}

void console_service_stop(void) {
  if (!g_console.running)
    return;
  g_console.running = 0;
  if (g_console.listen_fd >= 0) {
    shutdown(g_console.listen_fd, SHUT_RDWR);
    close(g_console.listen_fd);
    g_console.listen_fd = -1;
  }
  pthread_join(g_console.thread, NULL);
  log_info("console service stopped");
}
