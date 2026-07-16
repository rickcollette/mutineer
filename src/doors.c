#include "bbs_doors.h"
#include "bbs_util.h"
#include "bbs_process.h"
#include "bbs_log.h"
#include "include/bucc_bbs.h"
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/evp.h>

static void hex_encode(const unsigned char* in, unsigned int len,
                       char* out, size_t outcap) {
  static const char hexdigits[] = "0123456789abcdef";
  size_t need = (size_t)len * 2 + 1;
  if (outcap < need) {
    if (outcap > 0) out[0] = '\0';
    return;
  }
  for (unsigned int i = 0; i < len; i++) {
    out[i * 2] = hexdigits[(in[i] >> 4) & 0x0f];
    out[i * 2 + 1] = hexdigits[in[i] & 0x0f];
  }
  out[len * 2] = '\0';
}

static void json_escape(FILE* f, const char* s) {
  fputc('"', f);
  for (const unsigned char* p = (const unsigned char*)(s ? s : ""); *p; p++) {
    if (*p == '"' || *p == '\\') {
      fputc('\\', f);
      fputc(*p, f);
    } else if (*p == '\n') {
      fputs("\\n", f);
    } else if (*p == '\r') {
      fputs("\\r", f);
    } else if (*p == '\t') {
      fputs("\\t", f);
    } else if (*p >= 32) {
      fputc(*p, f);
    }
  }
  fputc('"', f);
}

static void door_fixed_copy(unsigned char* dst, size_t len, const char* src) {
  if (!dst || len == 0) return;
  if (!src) src = "";
  size_t n = strnlen(src, len);
  memcpy(dst, src, n);
}

static bool write_mutineer_session_json(Session* s, const DbDoor* door, const char* dir) {
  if (!s || !door || !dir || !dir[0]) return false;
  /* A prior run must never be mistaken for the current game's result. */
  char leaderboard_path[1024];
  path_join(dir, "MUTINEER_LB_RESULT.JSON", leaderboard_path, sizeof(leaderboard_path));
  unlink(leaderboard_path);
  char nonce_bytes[16];
  unsigned char hmac[EVP_MAX_MD_SIZE];
  unsigned int hmac_len = 0;
  char nonce[sizeof(nonce_bytes) * 2 + 1];
  char hmac_hex[EVP_MAX_MD_SIZE * 2 + 1];
  time_t issued = time(NULL);
  time_t expires = issued + (s->time_left_min > 0 ? s->time_left_min * 60 : 300);
  const char* secret = s->cfg.door_session_hmac_secret[0]
                         ? s->cfg.door_session_hmac_secret
                         : "mutineer-dev-door-secret";

  if (RAND_bytes((unsigned char*)nonce_bytes, sizeof(nonce_bytes)) != 1) {
    for (size_t i = 0; i < sizeof(nonce_bytes); i++)
      nonce_bytes[i] = (char)(rand() & 0xff);
  }
  hex_encode((const unsigned char*)nonce_bytes, sizeof(nonce_bytes),
             nonce, sizeof(nonce));

  char canonical[2048];
  snprintf(canonical, sizeof(canonical),
           "bbs_user_id=%d\nhandle=%s\nreal_name=%s\nsecurity_level=%d\n"
           "node_num=%d\ntime_left_sec=%d\nansi=%d\nremote_ip=%s\n"
           "issued_at=%ld\nexpires_at=%ld\nnonce=%s\n",
           s->user.id,
           s->user.handle,
           s->user.real_name[0] ? s->user.real_name : s->user.handle,
           s->user.level,
           s->node_num,
           s->time_left_min * 60,
           s->ansi ? 1 : 0,
           s->ip,
           (long)issued,
           (long)expires,
           nonce);

  if (!HMAC(EVP_sha256(), secret, (int)strlen(secret),
            (const unsigned char*)canonical, strlen(canonical),
            hmac, &hmac_len))
    return false;
  hex_encode(hmac, hmac_len, hmac_hex, sizeof(hmac_hex));

  char path[512];
  path_join(dir, "MUTINEER_SESSION.JSON", path, sizeof(path));
  FILE* f = fopen(path, "w");
  if (!f) return false;
  fprintf(f, "{\n");
  fprintf(f, "  \"bbs_user_id\": %d,\n", s->user.id);
  fprintf(f, "  \"handle\": "); json_escape(f, s->user.handle); fprintf(f, ",\n");
  fprintf(f, "  \"real_name\": ");
  json_escape(f, s->user.real_name[0] ? s->user.real_name : s->user.handle);
  fprintf(f, ",\n");
  fprintf(f, "  \"security_level\": %d,\n", s->user.level);
  fprintf(f, "  \"node_num\": %d,\n", s->node_num);
  fprintf(f, "  \"time_left_sec\": %d,\n", s->time_left_min * 60);
  fprintf(f, "  \"ansi\": %d,\n", s->ansi ? 1 : 0);
  fprintf(f, "  \"remote_ip\": "); json_escape(f, s->ip); fprintf(f, ",\n");
  fprintf(f, "  \"issued_at\": %ld,\n", (long)issued);
  fprintf(f, "  \"expires_at\": %ld,\n", (long)expires);
  fprintf(f, "  \"nonce\": "); json_escape(f, nonce); fprintf(f, ",\n");
  fprintf(f, "  \"hmac\": "); json_escape(f, hmac_hex); fprintf(f, ",\n");
  fprintf(f, "  \"leaderboard\": {\n");
  fprintf(f, "    \"LB_ENABLE\": %d,\n", door->lb_enable ? 1 : 0);
  fprintf(f, "    \"LB_GAME_KEY\": "); json_escape(f, door->lb_key); fprintf(f, ",\n");
  fprintf(f, "    \"LB_SCORE_LABEL\": "); json_escape(f, door->lb_label); fprintf(f, ",\n");
  fprintf(f, "    \"LB_ORDER\": "); json_escape(f, door->lb_order); fprintf(f, ",\n");
  fprintf(f, "    \"LB_RESULT_FILE\": \"MUTINEER_LB_RESULT.JSON\"\n");
  fprintf(f, "  }\n");
  fprintf(f, "}\n");
  fclose(f);
  return true;
}

static bool ensure_dir(const char* path) {
  return mkdir(path, 0755) == 0 || errno == EEXIST;
}

static bool remove_tree_recursive(const char* path) {
  struct stat st;
  if (lstat(path, &st) != 0) return errno == ENOENT;

  if (S_ISDIR(st.st_mode)) {
    DIR* d = opendir(path);
    if (!d) return false;
    bool ok = true;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
      if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
      char child[1024];
      snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
      if (!remove_tree_recursive(child)) ok = false;
    }
    closedir(d);
    return rmdir(path) == 0 && ok;
  }

  return unlink(path) == 0;
}

static pthread_t g_door_janitor_thread;
static pthread_mutex_t g_door_janitor_mu = PTHREAD_MUTEX_INITIALIZER;
static BbsConfig g_door_janitor_cfg;
static BbsDb* g_door_janitor_db;
static int g_door_janitor_running;
static int g_door_janitor_stop;

static int janitor_node_number(const char* name) {
  char extra;
  int node = 0;
  if (!name || sscanf(name, "node%d%c", &node, &extra) != 1 || node < 1 || node > 256)
    return 0;
  return node;
}

static int janitor_scan_base(const char* base, int stale_age, time_t now) {
  DIR* doors;
  struct dirent* door_ent;
  int removed = 0;
  if (!base || !base[0] || (doors = opendir(base)) == NULL)
    return 0;
  while ((door_ent = readdir(doors)) != NULL) {
    if (!strcmp(door_ent->d_name, ".") || !strcmp(door_ent->d_name, "..")) continue;
    char door_path[1024];
    snprintf(door_path, sizeof(door_path), "%s/%s", base, door_ent->d_name);
    struct stat door_st;
    if (lstat(door_path, &door_st) != 0 || !S_ISDIR(door_st.st_mode)) continue;
    DIR* nodes = opendir(door_path);
    if (!nodes) continue;
    struct dirent* node_ent;
    while ((node_ent = readdir(nodes)) != NULL) {
      int node = janitor_node_number(node_ent->d_name);
      if (!node || online_get_node(node) != NULL) continue;
      char node_path[1024];
      path_join(door_path, node_ent->d_name, node_path, sizeof(node_path));
      DIR* launches = opendir(node_path);
      if (!launches) continue;
      struct dirent* launch_ent;
      while ((launch_ent = readdir(launches)) != NULL) {
        if (!strcmp(launch_ent->d_name, ".") || !strcmp(launch_ent->d_name, "..")) continue;
        char launch_path[1024];
        struct stat st;
        path_join(node_path, launch_ent->d_name, launch_path, sizeof(launch_path));
        if (lstat(launch_path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        if (stale_age > 0 && difftime(now, st.st_mtime) < stale_age) continue;
        if (remove_tree_recursive(launch_path)) {
          removed++;
          log_info("door janitor: removed stale offline launch %s", launch_path);
        } else {
          log_warn("door janitor: failed to remove %s: %s", launch_path, strerror(errno));
        }
      }
      closedir(launches);
      rmdir(node_path);
    }
    closedir(nodes);
    rmdir(door_path);
  }
  closedir(doors);
  return removed;
}

int door_janitor_run_once(const BbsConfig* cfg, BbsDb* db) {
  int age;
  int removed;
  if (!cfg) return 0;
  age = cfg->door_stale_age_sec < 0 ? 0 : cfg->door_stale_age_sec;
  removed = janitor_scan_base(cfg->dropfile_path, age, time(NULL));
  if (strcmp(cfg->door_runtime_path, cfg->dropfile_path))
    removed += janitor_scan_base(cfg->door_runtime_path, age, time(NULL));
  /* The configured dropfile directory can be nested beneath the runtime
   * directory. The second scan may then correctly remove it as an empty
   * child after cleaning sessions. Keep both configured base directories as
   * stable parts of the installed runtime layout. */
  if (!ensure_dir(cfg->door_runtime_path))
    log_warn("door janitor: failed to preserve runtime directory %s: %s",
             cfg->door_runtime_path, strerror(errno));
  if (!ensure_dir(cfg->dropfile_path))
    log_warn("door janitor: failed to preserve dropfile directory %s: %s",
             cfg->dropfile_path, strerror(errno));
  if (db) {
    DbNode nodes[256];
    int count = db_node_list(db, nodes, 256);
    for (int i = 0; i < count; i++)
      if (!strcmp(nodes[i].status, "online") &&
          online_get_node(nodes[i].node_num) == NULL) {
        db_node_upsert(db, nodes[i].node_num, 0, "offline", "idle", "");
        log_info("door janitor: reconciled stale online database node %d",
                 nodes[i].node_num);
      }
  }
  return removed;
}

static void* door_janitor_main(void* unused) {
  (void)unused;
  door_janitor_run_once(&g_door_janitor_cfg, g_door_janitor_db);
  for (;;) {
    int interval = g_door_janitor_cfg.door_janitor_interval_sec;
    for (int elapsed = 0; elapsed < interval; elapsed++) {
      pthread_mutex_lock(&g_door_janitor_mu);
      int stop = g_door_janitor_stop;
      pthread_mutex_unlock(&g_door_janitor_mu);
      if (stop) return NULL;
      sleep(1);
    }
    door_janitor_run_once(&g_door_janitor_cfg, g_door_janitor_db);
  }
}

void door_janitor_start(const BbsConfig* cfg, BbsDb* db) {
  if (!cfg || cfg->door_janitor_interval_sec <= 0) return;
  pthread_mutex_lock(&g_door_janitor_mu);
  if (!g_door_janitor_running) {
    g_door_janitor_cfg = *cfg;
    g_door_janitor_db = db;
    g_door_janitor_stop = 0;
    if (pthread_create(&g_door_janitor_thread, NULL, door_janitor_main, NULL) == 0)
      g_door_janitor_running = 1;
    else
      log_warn("door janitor: unable to start background thread");
  }
  pthread_mutex_unlock(&g_door_janitor_mu);
}

void door_janitor_stop(void) {
  pthread_mutex_lock(&g_door_janitor_mu);
  if (!g_door_janitor_running) {
    pthread_mutex_unlock(&g_door_janitor_mu);
    return;
  }
  g_door_janitor_stop = 1;
  pthread_t thread = g_door_janitor_thread;
  pthread_mutex_unlock(&g_door_janitor_mu);
  pthread_join(thread, NULL);
  pthread_mutex_lock(&g_door_janitor_mu);
  g_door_janitor_running = 0;
  pthread_mutex_unlock(&g_door_janitor_mu);
}

/* =========================================================================
 * Minimal JSON key-value extractor for flat manifest objects.
 * Handles string, integer, and boolean values only.
 * ========================================================================= */

static const char* json_find_key(const char* json, const char* key) {
  char search[128];
  snprintf(search, sizeof(search), "\"%s\"", key);
  const char* p = json;
  while ((p = strstr(p, search)) != NULL) {
    const char* after = p + strlen(search);
    while (*after == ' ' || *after == '\t' || *after == '\n' || *after == '\r') after++;
    if (*after == ':') return after + 1;
    p++;
  }
  return NULL;
}

static bool json_str(const char* json, const char* key, char* out, size_t cap) {
  const char* p = json_find_key(json, key);
  if (!p) return false;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  if (*p != '"') return false;
  p++;
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < cap) {
    if (*p == '\\' && *(p+1)) {
      p++;
      switch (*p) {
        case 'n':  out[i++] = '\n'; break;
        case 't':  out[i++] = '\t'; break;
        case '\\': out[i++] = '\\'; break;
        case '"':  out[i++] = '"';  break;
        case '/':  out[i++] = '/';  break;
        default:   out[i++] = *p;   break;
      }
    } else {
      out[i++] = *p;
    }
    p++;
  }
  out[i] = '\0';
  return true;
}

static bool json_int(const char* json, const char* key, int* out) {
  const char* p = json_find_key(json, key);
  if (!p) return false;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  if (!isdigit((unsigned char)*p) && *p != '-') return false;
  *out = atoi(p);
  return true;
}

static bool json_bool(const char* json, const char* key, int* out) {
  const char* p = json_find_key(json, key);
  if (!p) return false;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  if (strncmp(p, "true", 4) == 0)  { *out = 1; return true; }
  if (strncmp(p, "false", 5) == 0) { *out = 0; return true; }
  return false;
}

static void ingest_leaderboard_result(Session* s, const DbDoor* door, const char* dir) {
  if (!s || !s->db || !door || !door->lb_enable || !dir) return;
  char path[1024];
  path_join(dir, "MUTINEER_LB_RESULT.JSON", path, sizeof(path));
  struct stat st;
  if (lstat(path, &st) != 0) return;
  if (!S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 4096) {
    log_warn("door %s: rejected invalid leaderboard result file", door->name);
    return;
  }
  char* json = file_read_all(path, NULL);
  if (!json) return;
  const char* score_value = json_find_key(json, "score");
  if (!score_value) {
    log_warn("door %s: leaderboard result missing score", door->name);
    free(json);
    return;
  }
  while (isspace((unsigned char)*score_value)) score_value++;
  char* end = NULL;
  errno = 0;
  long long score = strtoll(score_value, &end, 10);
  while (end && isspace((unsigned char)*end)) end++;
  if (errno || end == score_value || (*end && *end != ',' && *end != '}')) {
    log_warn("door %s: leaderboard result has invalid score", door->name);
    free(json);
    return;
  }
  char detail[128] = "";
  json_str(json, "detail", detail, sizeof(detail));
  if (!db_door_score_submit(s->db, door->id, s->user.handle, (int64_t)score, detail))
    log_warn("door %s: leaderboard result was not accepted", door->name);
  free(json);
}

/* =========================================================================
 * Path safety helpers
 * ========================================================================= */

/* Returns false if path is empty, absolute, or contains a ".." component. */
static bool path_is_safe_relative(const char* path) {
  if (!path || !path[0]) return false;
  if (path[0] == '/') return false;
  const char* p = path;
  while (*p) {
    if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) return false;
    while (*p && *p != '/') p++;
    if (*p == '/') p++;
  }
  return true;
}

typedef struct BuccLiveCtx {
  Session* s;
  const DbDoor* door;
  char scope[128];
  char text_root[512];
} BuccLiveCtx;

static bool bucc_leaderboard_enabled_cb(void* ctx) {
  BuccLiveCtx* live = (BuccLiveCtx*)ctx;
  return live && live->door && live->door->enabled && live->door->lb_enable;
}

static bool bucc_leaderboard_submit_cb(void* ctx, int64_t score, const char* detail) {
  BuccLiveCtx* live = (BuccLiveCtx*)ctx;
  return live && live->s && live->s->db && live->door &&
         db_door_score_submit(live->s->db, live->door->id,
                              live->s->user.handle, score, detail);
}

/* =========================================================================
 * DOSBox manifest parsing and validation
 * ========================================================================= */

bool dosbox_manifest_parse(const char* path, DosboxManifest* out,
                           char* errbuf, size_t errcap) {
  if (!path || !out) {
    if (errbuf) snprintf(errbuf, errcap, "null argument");
    return false;
  }
  memset(out, 0, sizeof(*out));

  char* json = file_read_all(path, NULL);
  if (!json) {
    if (errbuf) snprintf(errbuf, errcap, "cannot read manifest: %s", path);
    return false;
  }

  json_str(json, "runner",       out->runner,       sizeof(out->runner));
  json_str(json, "name",         out->name,         sizeof(out->name));
  json_str(json, "master_dir",   out->master_dir,   sizeof(out->master_dir));
  json_str(json, "startup",      out->startup,      sizeof(out->startup));
  json_str(json, "dropfile",     out->dropfile,     sizeof(out->dropfile));
  json_str(json, "dropfile_dest",out->dropfile_dest,sizeof(out->dropfile_dest));
  json_str(json, "machine",      out->machine,      sizeof(out->machine));
  json_str(json, "core",         out->core,         sizeof(out->core));
  json_str(json, "cycles",       out->cycles,       sizeof(out->cycles));
  json_str(json, "copy_mode",    out->copy_mode,    sizeof(out->copy_mode));

  int v = 0;
  if (json_int(json, "memsize", &v))      out->memsize = v;
  if (json_int(json, "timeout_sec", &v))  out->timeout_sec = v;
  if (json_bool(json, "serial_telnet", &v)) out->serial_telnet = v;
  else out->serial_telnet = 1; /* default on */
  if (json_bool(json, "usedtr", &v))        out->usedtr = v;
  if (json_bool(json, "cleanup_on_exit", &v)) out->cleanup_on_exit = v;
  else out->cleanup_on_exit = 1; /* default on */

  /* Apply defaults for optional fields */
  if (!out->machine[0])   snprintf(out->machine,   sizeof(out->machine),   "svga_s3");
  if (out->memsize <= 0)  out->memsize = 16;
  if (!out->core[0])      snprintf(out->core,      sizeof(out->core),      "auto");
  if (!out->cycles[0])    snprintf(out->cycles,    sizeof(out->cycles),    "auto");
  if (!out->copy_mode[0]) snprintf(out->copy_mode, sizeof(out->copy_mode), "copy");
  if (!out->dropfile[0])  snprintf(out->dropfile,  sizeof(out->dropfile),  "DOOR.SYS");
  if (!out->dropfile_dest[0]) snprintf(out->dropfile_dest, sizeof(out->dropfile_dest), "game");

  free(json);
  return true;
}

bool dosbox_manifest_validate(const DosboxManifest* m, char* errbuf, size_t errcap) {
  if (!m) { if (errbuf) snprintf(errbuf, errcap, "null manifest"); return false; }

  if (!m->master_dir[0]) {
    if (errbuf) snprintf(errbuf, errcap, "manifest: master_dir is required");
    return false;
  }
  if (m->master_dir[0] != '/') {
    if (errbuf) snprintf(errbuf, errcap, "manifest: master_dir must be an absolute path");
    return false;
  }
  if (!m->startup[0]) {
    if (errbuf) snprintf(errbuf, errcap, "manifest: startup is required");
    return false;
  }
  /* startup must not escape the game/ directory */
  if (!path_is_safe_relative(m->startup)) {
    if (errbuf) snprintf(errbuf, errcap, "manifest: startup contains unsafe path: %s", m->startup);
    return false;
  }
  /* dropfile_dest must stay within runtime tree */
  if (m->dropfile_dest[0] && !path_is_safe_relative(m->dropfile_dest)) {
    if (errbuf) snprintf(errbuf, errcap, "manifest: dropfile_dest contains unsafe path: %s", m->dropfile_dest);
    return false;
  }
  return true;
}

/* =========================================================================
 * Runtime tree management
 * ========================================================================= */

static bool copy_dir_recursive(const char* src, const char* dst) {
  struct stat st;
  if (stat(src, &st) != 0 || !S_ISDIR(st.st_mode)) return false;
  if (mkdir(dst, 0755) != 0 && errno != EEXIST) return false;

  DIR* d = opendir(src);
  if (!d) return false;

  bool ok = true;
  struct dirent* ent;
  while (ok && (ent = readdir(d)) != NULL) {
    if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
    char src_p[1024], dst_p[1024];
    snprintf(src_p, sizeof(src_p), "%s/%s", src, ent->d_name);
    snprintf(dst_p, sizeof(dst_p), "%s/%s", dst, ent->d_name);
    if (stat(src_p, &st) != 0) { ok = false; break; }
    if (S_ISDIR(st.st_mode))      ok = copy_dir_recursive(src_p, dst_p);
    else if (S_ISREG(st.st_mode)) ok = file_copy(src_p, dst_p, NULL);
  }
  closedir(d);
  return ok;
}

bool dosbox_prepare_runtime(const DosboxManifest* m,
                            const char* runtime_base,
                            int node_num, const char* launch_id,
                            char* runtime_root_out, size_t root_cap,
                            char* errbuf, size_t errcap) {
  /* <runtime_base>/<door_name>/node<NN>/<launch_id>/ */
  char door_base[1024], node_dir[1024], root[1024], game_dir[1024], logs_dir[1024];
  char node_leaf[32];
  snprintf(node_leaf, sizeof(node_leaf), "node%02d", node_num);
  path_join(runtime_base, m->name[0] ? m->name : "door", door_base, sizeof(door_base));
  path_join(door_base, node_leaf, node_dir, sizeof(node_dir));
  path_join(node_dir, launch_id, root, sizeof(root));
  path_join(root, "game", game_dir, sizeof(game_dir));
  path_join(root, "logs", logs_dir, sizeof(logs_dir));

  if (!ensure_dir(runtime_base) || !ensure_dir(door_base) ||
      !ensure_dir(node_dir)     || !ensure_dir(root)      ||
      !ensure_dir(game_dir)     || !ensure_dir(logs_dir)) {
    if (errbuf) snprintf(errbuf, errcap, "cannot create runtime tree: %s: %s",
                         root, strerror(errno));
    return false;
  }

  if (!copy_dir_recursive(m->master_dir, game_dir)) {
    if (errbuf) snprintf(errbuf, errcap, "cannot copy master_dir %s to %s",
                         m->master_dir, game_dir);
    return false;
  }

  if (runtime_root_out) snprintf(runtime_root_out, root_cap, "%s", root);
  return true;
}

bool dosbox_build_conf(const DosboxManifest* m, const char* game_dir,
                       const char* conf_path,
                       char* errbuf, size_t errcap) {
  FILE* f = fopen(conf_path, "w");
  if (!f) {
    if (errbuf) snprintf(errbuf, errcap, "cannot create dosbox conf %s: %s",
                         conf_path, strerror(errno));
    return false;
  }

  fprintf(f, "[sdl]\n");
  fprintf(f, "fullscreen=false\n");
  fprintf(f, "output=surface\n\n");

  fprintf(f, "[dosbox]\n");
  fprintf(f, "machine=%s\n", m->machine);
  fprintf(f, "memsize=%d\n\n", m->memsize);

  fprintf(f, "[cpu]\n");
  fprintf(f, "core=%s\n", m->core);
  fprintf(f, "cycles=%s\n\n", m->cycles);

  fprintf(f, "[serial]\n");
  if (m->usedtr)
    fprintf(f, "serial1=nullmodem inhsocket:1 telnet:1 usedtr:1\n\n");
  else
    fprintf(f, "serial1=nullmodem inhsocket:1 telnet:1\n\n");

  fprintf(f, "[autoexec]\n");
  fprintf(f, "mount c \"%s\"\n", game_dir);
  fprintf(f, "c:\n");
  fprintf(f, "%s\n", m->startup);
  fprintf(f, "exit\n");

  fclose(f);
  (void)errbuf; (void)errcap;
  return true;
}

void dosbox_cleanup_runtime(const char* runtime_root) {
  if (!runtime_root || !runtime_root[0]) return;
  /* Safety: must be an absolute path to avoid rm -rf accidents */
  if (runtime_root[0] != '/') {
    log_warn("dosbox cleanup: refusing relative path: %s", runtime_root);
    return;
  }
  if (!remove_tree_recursive(runtime_root))
    log_warn("dosbox cleanup: recursive remove failed for %s: %s", runtime_root, strerror(errno));
  else
    log_info("dosbox cleanup: removed runtime tree %s", runtime_root);
}

static bool write_doorsys(Session* s, const char* dir) {
  char path[512]; path_join(dir, "DOOR.SYS", path, sizeof(path));
  FILE* f = fopen(path, "w");
  if (!f) return false;
  
  /* Full DOOR.SYS format (52 lines) */
  fprintf(f, "COM0:\n");                              /* 1: COM port */
  fprintf(f, "38400\n");                              /* 2: Baud rate */
  fprintf(f, "8\n");                                  /* 3: Data bits */
  fprintf(f, "%d\n", s->node_num);                    /* 4: Node number */
  fprintf(f, "Y\n");                                  /* 5: Screen display (Y/N) */
  fprintf(f, "Y\n");                                  /* 6: Printer on (Y/N) */
  fprintf(f, "Y\n");                                  /* 7: Page bell (Y/N) */
  fprintf(f, "Y\n");                                  /* 8: Caller alarm (Y/N) */
  fprintf(f, "%s\n", s->user.real_name[0] ? s->user.real_name : s->user.handle); /* 9: Real name */
  fprintf(f, "%s\n", s->user.street[0] ? s->user.street : "Unknown");  /* 10: City/State */
  fprintf(f, "%s\n", s->user.phone[0] ? s->user.phone : "000-000-0000"); /* 11: Home phone */
  fprintf(f, "%s\n", s->user.phone[0] ? s->user.phone : "000-000-0000"); /* 12: Work phone */
  fprintf(f, "********\n");                           /* 13: Password (masked) */
  fprintf(f, "%d\n", s->user.level);                  /* 14: Security level */
  fprintf(f, "%d\n", s->user.logged_on);              /* 15: Total calls */
  fprintf(f, "%s\n", s->user.last_login_at);          /* 16: Last call date */
  fprintf(f, "%d\n", s->time_left_min * 60);          /* 17: Seconds remaining */
  fprintf(f, "%d\n", s->time_left_min);               /* 18: Minutes remaining */
  fprintf(f, "GR\n");                                 /* 19: Graphics mode */
  fprintf(f, "24\n");                                 /* 20: Screen length */
  fprintf(f, "Y\n");                                  /* 21: Expert mode */
  fprintf(f, "ABCDEFGHIJKLMNOPQRSTUVWXYZ\n");         /* 22: Conferences registered */
  fprintf(f, "%d\n", s->current_conf);                /* 23: Current conference */
  fprintf(f, "%s\n", s->user.expires_at);             /* 24: Expiration date */
  fprintf(f, "%d\n", s->user.id);                     /* 25: User record number */
  fprintf(f, "Y\n");                                  /* 26: Default protocol */
  fprintf(f, "%d\n", s->user.uploads);                /* 27: Total uploads */
  fprintf(f, "%d\n", s->user.downloads);              /* 28: Total downloads */
  fprintf(f, "%d\n", s->user.dk);                     /* 29: Daily download K */
  fprintf(f, "%d\n", 0);                              /* 30: Max daily download K */
  fprintf(f, "%s\n", s->user.birth_date);             /* 31: Birth date */
  fprintf(f, "%s\\\n", dir);                          /* 32: Path to MAIN dir */
  fprintf(f, "%s\\\n", dir);                          /* 33: Path to GEN dir */
  fprintf(f, "%s\n", s->cfg.sysop_name[0] ? s->cfg.sysop_name : "Sysop"); /* 34: Sysop name */
  fprintf(f, "%s\n", s->user.handle);                 /* 35: User alias */
  fprintf(f, "00:00\n");                              /* 36: Event time */
  fprintf(f, "Y\n");                                  /* 37: Error correcting */
  fprintf(f, "Y\n");                                  /* 38: ANSI supported */
  fprintf(f, "Y\n");                                  /* 39: Use record locking */
  fprintf(f, "7\n");                                  /* 40: Default color */
  fprintf(f, "%d\n", s->time_left_min);               /* 41: Time credits */
  fprintf(f, "%s\n", s->user.last_login_at);          /* 42: Last new files scan */
  fprintf(f, "%s\n", s->started_at ? ctime(&s->started_at) : ""); /* 43: Time of call */
  fprintf(f, "%s\n", s->user.last_login_at);          /* 44: Time of last call */
  fprintf(f, "32767\n");                              /* 45: Max daily files */
  fprintf(f, "%d\n", s->user.downloads);              /* 46: Files downloaded today */
  fprintf(f, "%d\n", s->user.uk);                     /* 47: Total upload K */
  fprintf(f, "%d\n", s->user.dk);                     /* 48: Total download K */
  fprintf(f, "None\n");                               /* 49: Comment */
  fprintf(f, "0\n");                                  /* 50: Doors opened */
  fprintf(f, "%d\n", s->user.msg_post);               /* 51: Messages left */
  
  fclose(f);
  return true;
}

static bool write_door32sys(Session* s, const char* dir) {
  char path[512]; path_join(dir, "DOOR32.SYS", path, sizeof(path));
  FILE* f = fopen(path, "w");
  if (!f) return false;
  
  /* DOOR32.SYS format - designed for 32-bit door programs */
  fprintf(f, "2\n");                                  /* 1: Comm type (2=telnet) */
  fprintf(f, "0\n");                                  /* 2: Comm handle (stdin) */
  fprintf(f, "38400\n");                              /* 3: Baud rate */
  fprintf(f, "%s %s\n", s->cfg.bbs_name[0] ? s->cfg.bbs_name : "Mutineer BBS", 
          "Mutineer");                                /* 4: BBS software name */
  fprintf(f, "1\n");                                  /* 5: BBS software version */
  fprintf(f, "%s\n", s->user.real_name[0] ? s->user.real_name : s->user.handle); /* 6: Real name */
  fprintf(f, "%s\n", s->user.handle);                 /* 7: Alias/handle */
  fprintf(f, "%d\n", s->user.level);                  /* 8: Security level */
  fprintf(f, "%d\n", s->time_left_min);               /* 9: Time remaining (minutes) */
  fprintf(f, "1\n");                                  /* 10: Emulation (1=ANSI) */
  fprintf(f, "%d\n", s->node_num);                    /* 11: Node number */
  
  fclose(f);
  return true;
}

static bool write_chaintxt(Session* s, const char* dir) {
  char path[512]; path_join(dir, "CHAIN.TXT", path, sizeof(path));
  FILE* f = fopen(path, "w");
  if (!f) return false;
  
  /* CHAIN.TXT format (WWIV style) */
  fprintf(f, "%d\n", s->user.id);                     /* 1: User number */
  fprintf(f, "%s\n", s->user.handle);                 /* 2: User alias */
  fprintf(f, "%s\n", s->user.real_name[0] ? s->user.real_name : s->user.handle); /* 3: Real name */
  fprintf(f, "\n");                                   /* 4: Call sign (ham radio) */
  fprintf(f, "%d\n", 0);                              /* 5: Age (calculated) */
  fprintf(f, "%c\n", s->user.sex ? s->user.sex : 'M'); /* 6: Sex */
  fprintf(f, "%d\n", s->user.credits);                /* 7: Gold */
  fprintf(f, "%s\n", s->user.last_login_at);          /* 8: Last call date */
  fprintf(f, "80\n");                                 /* 9: Screen width */
  fprintf(f, "24\n");                                 /* 10: Screen lines */
  fprintf(f, "%d\n", s->user.level);                  /* 11: Security level */
  fprintf(f, "0\n");                                  /* 12: Co-sysop (0/1) */
  fprintf(f, "1\n");                                  /* 13: Sysop (0/1) */
  fprintf(f, "1\n");                                  /* 14: ANSI (0/1) */
  fprintf(f, "0\n");                                  /* 15: Remote (0=local, 1=remote) */
  fprintf(f, "%d\n", s->time_left_min * 60);          /* 16: Seconds remaining */
  fprintf(f, "%s\\\n", dir);                          /* 17: Path to GFILES */
  fprintf(f, "%s\\\n", dir);                          /* 18: Path to DATA */
  fprintf(f, "00000000.LOG\n");                       /* 19: Log file name */
  fprintf(f, "38400\n");                              /* 20: Baud rate */
  fprintf(f, "0\n");                                  /* 21: COM port (0=local) */
  fprintf(f, "%s %s\n", s->cfg.bbs_name[0] ? s->cfg.bbs_name : "Mutineer BBS", 
          "Mutineer");                                /* 22: BBS name */
  fprintf(f, "%s\n", s->cfg.sysop_name[0] ? s->cfg.sysop_name : "Sysop"); /* 23: Sysop name */
  fprintf(f, "%d\n", s->started_at ? (int)s->started_at : 0); /* 24: Time user logged on */
  fprintf(f, "%d\n", s->time_left_min * 60);          /* 25: Seconds remaining */
  fprintf(f, "%d\n", s->user.uploads);                /* 26: Uploads */
  fprintf(f, "%d\n", s->user.downloads);              /* 27: Downloads */
  fprintf(f, "0\n");                                  /* 28: Parity */
  fprintf(f, "0\n");                                  /* 29: DSZ log */
  fprintf(f, "0\n");                                  /* 30: Instance number */
  
  fclose(f);
  return true;
}

static bool write_pcboardsys(Session* s, const char* dir) {
  char path[512]; path_join(dir, "PCBOARD.SYS", path, sizeof(path));
  FILE* f = fopen(path, "wb");  /* Binary file */
  if (!f) return false;
  
  /* PCBoard 15.x PCBOARD.SYS format (128 bytes) */
  unsigned char buf[128];
  memset(buf, 0, sizeof(buf));
  
  /* Display (2 bytes) - offset 0 */
  buf[0] = (unsigned char)(-1);  /* -1 = local, 0+ = COM port */
  buf[1] = 0;
  
  /* Printer (2 bytes) - offset 2 */
  buf[2] = 0;
  buf[3] = 0;
  
  /* Page bell (2 bytes) - offset 4 */
  buf[4] = 0;
  buf[5] = 0;
  
  /* Caller alarm (2 bytes) - offset 6 */
  buf[6] = 0;
  buf[7] = 0;
  
  /* Sysop next (char) - offset 8 */
  buf[8] = ' ';
  
  /* Error correcting (char) - offset 9 */
  buf[9] = 'Y';
  
  /* Graphics (char) - offset 10 */
  buf[10] = 'Y';
  
  /* Node chat available (char) - offset 11 */
  buf[11] = 'Y';
  
  /* Baud rate (5 bytes) - offset 12 */
  memcpy(&buf[12], "38400", 5);
  
  /* User name (25 bytes) - offset 17 */
  door_fixed_copy(&buf[17], 25, s->user.handle);
  
  /* First name (15 bytes) - offset 42 */
  door_fixed_copy(&buf[42], 15, s->user.handle);
  
  /* Password (12 bytes) - offset 57 */
  door_fixed_copy(&buf[57], 12, "********");           /* Password (masked) */
  
  /* User record number (2 bytes) - offset 69 */
  buf[69] = (unsigned char)(s->user.id & 0xFF);
  buf[70] = (unsigned char)((s->user.id >> 8) & 0xFF);
  
  /* Time on (2 bytes) - offset 71 */
  int time_on = (int)(time(NULL) - s->started_at) / 60;
  buf[71] = (unsigned char)(time_on & 0xFF);
  buf[72] = (unsigned char)((time_on >> 8) & 0xFF);
  
  /* Time logged on (5 bytes HH:MM) - offset 73 */
  time_t lt = s->started_at;
  struct tm* tm = localtime(&lt);
  if (tm) {
    snprintf((char*)&buf[73], 6, "%02d:%02d", tm->tm_hour, tm->tm_min);
  }
  
  /* Time limit (2 bytes) - offset 78 */
  buf[78] = (unsigned char)(s->time_left_min & 0xFF);
  buf[79] = (unsigned char)((s->time_left_min >> 8) & 0xFF);
  
  /* Node number (2 bytes) - offset 80 */
  buf[80] = (unsigned char)(s->node_num & 0xFF);
  buf[81] = (unsigned char)((s->node_num >> 8) & 0xFF);
  
  /* Event time (5 bytes HH:MM) - offset 82 */
  memcpy(&buf[82], "00:00", 5);
  
  /* Event active (char) - offset 87 */
  buf[87] = 'N';
  
  /* Event slide (char) - offset 88 */
  buf[88] = 'N';
  
  /* Memorized message (4 bytes) - offset 89 */
  buf[89] = 0; buf[90] = 0; buf[91] = 0; buf[92] = 0;
  
  /* COM port (char) - offset 93 */
  buf[93] = '0';
  
  /* Reserved (2 bytes) - offset 94 */
  buf[94] = 0; buf[95] = 0;
  
  /* Use ANSI (char) - offset 96 */
  buf[96] = 'Y';
  
  /* Security level (2 bytes) - offset 97 */
  buf[97] = (unsigned char)(s->user.level & 0xFF);
  buf[98] = (unsigned char)((s->user.level >> 8) & 0xFF);
  
  /* Total calls (2 bytes) - offset 99 */
  buf[99] = (unsigned char)(s->user.logged_on & 0xFF);
  buf[100] = (unsigned char)((s->user.logged_on >> 8) & 0xFF);
  
  /* Page length (char) - offset 101 */
  buf[101] = 24;
  
  /* Expert mode (char) - offset 102 */
  buf[102] = 'N';
  
  /* Registered conferences (9 bytes) - offset 103 */
  memset(&buf[103], 0xFF, 9);
  
  /* Date last on (8 bytes) - offset 112 */
  door_fixed_copy(&buf[112], 8, s->user.last_login_at);
  
  /* Time remaining (2 bytes) - offset 120 */
  buf[120] = (unsigned char)(s->time_left_min & 0xFF);
  buf[121] = (unsigned char)((s->time_left_min >> 8) & 0xFF);
  
  /* Time remaining (2 bytes, again) - offset 122 */
  buf[122] = (unsigned char)(s->time_left_min & 0xFF);
  buf[123] = (unsigned char)((s->time_left_min >> 8) & 0xFF);
  
  /* Uploads (2 bytes) - offset 124 */
  buf[124] = (unsigned char)(s->user.uploads & 0xFF);
  buf[125] = (unsigned char)((s->user.uploads >> 8) & 0xFF);
  
  /* Downloads (2 bytes) - offset 126 */
  buf[126] = (unsigned char)(s->user.downloads & 0xFF);
  buf[127] = (unsigned char)((s->user.downloads >> 8) & 0xFF);
  
  fwrite(buf, 1, 128, f);
  fclose(f);
  return true;
}

static bool write_dorinfo(Session* s, const char* dir) {
  char path[512]; path_join(dir, "DORINFO1.DEF", path, sizeof(path));
  FILE* f = fopen(path, "w");
  if (!f) return false;
  
  /* DORINFO1.DEF format */
  fprintf(f, "%s\n", s->cfg.bbs_name[0] ? s->cfg.bbs_name : "Mutineer BBS"); /* 1: BBS name */
  fprintf(f, "%s\n", s->cfg.sysop_name[0] ? s->cfg.sysop_name : "Sysop");    /* 2: Sysop first name */
  fprintf(f, "\n");                                                           /* 3: Sysop last name */
  fprintf(f, "COM0\n");                                                       /* 4: COM port */
  fprintf(f, "38400 BAUD,N,8,1\n");                                           /* 5: Baud/parity */
  fprintf(f, "0\n");                                                          /* 6: Network type */
  fprintf(f, "%s\n", s->user.handle);                                         /* 7: User first name */
  fprintf(f, "\n");                                                           /* 8: User last name */
  fprintf(f, "%s\n", s->user.street[0] ? s->user.street : "Unknown");         /* 9: City/State */
  fprintf(f, "1\n");                                                          /* 10: ANSI (0/1/2) */
  fprintf(f, "%d\n", s->user.level);                                          /* 11: Security level */
  fprintf(f, "%d\n", s->time_left_min);                                       /* 12: Minutes remaining */
  
  fclose(f);
  return true;
}

static bool write_callinfo(Session* s, const char* dir) {
  char path[512]; path_join(dir, "CALLINFO.BBS", path, sizeof(path));
  FILE* f = fopen(path, "w");
  if (!f) return false;
  
  /* CALLINFO.BBS format (Wildcat style) */
  fprintf(f, "%s\n", s->user.handle);                 /* 1: User name */
  fprintf(f, "%d\n", 1);                              /* 2: Speed (1=local) */
  fprintf(f, "%s\n", s->user.street[0] ? s->user.street : "Unknown"); /* 3: City */
  fprintf(f, "%d\n", s->user.level);                  /* 4: Security level */
  fprintf(f, "%d\n", s->time_left_min);               /* 5: Minutes remaining */
  fprintf(f, "COLOR\n");                              /* 6: ANSI/COLOR/MONO */
  fprintf(f, "********\n");                           /* 7: Password (masked) */
  fprintf(f, "%d\n", s->user.id);                     /* 8: User record number */
  fprintf(f, "%d\n", s->started_at ? (int)s->started_at : 0); /* 9: Time logged on */
  fprintf(f, "%d\n", s->user.logged_on);              /* 10: Total calls */
  fprintf(f, "%d\n", s->user.msg_post);               /* 11: Messages posted */
  fprintf(f, "%s\n", s->user.last_login_at);          /* 12: Last call date */
  fprintf(f, "24\n");                                 /* 13: Screen lines */
  fprintf(f, "Y\n");                                  /* 14: Expert mode */
  fprintf(f, "ABCDEFGHIJKLMNOPQRSTUVWXYZ\n");         /* 15: Conferences */
  fprintf(f, "%d\n", s->current_conf);                /* 16: Current conference */
  fprintf(f, "%s\n", s->user.expires_at);             /* 17: Expiration date */
  fprintf(f, "%d\n", s->node_num);                    /* 18: Node number */
  fprintf(f, "%s\n", s->user.phone[0] ? s->user.phone : "000-000-0000"); /* 19: Home phone */
  fprintf(f, "%s\n", s->user.phone[0] ? s->user.phone : "000-000-0000"); /* 20: Work phone */
  fprintf(f, "%s\n", s->user.birth_date);             /* 21: Birth date */
  fprintf(f, "%d\n", s->user.uploads);                /* 22: Uploads */
  fprintf(f, "%d\n", s->user.downloads);              /* 23: Downloads */
  fprintf(f, "%d\n", s->user.uk);                     /* 24: Upload K */
  fprintf(f, "%d\n", s->user.dk);                     /* 25: Download K */
  
  fclose(f);
  return true;
}

static bool write_sfdoors(Session* s, const char* dir) {
  char path[512]; path_join(dir, "SFDOORS.DAT", path, sizeof(path));
  FILE* f = fopen(path, "w");
  if (!f) return false;
  
  /* SFDOORS.DAT format (Spitfire style) */
  fprintf(f, "%d\n", s->user.id);                     /* 1: User number */
  fprintf(f, "%s\n", s->user.handle);                 /* 2: User name */
  fprintf(f, "********\n");                           /* 3: Password (masked) */
  fprintf(f, "1\n");                                  /* 4: Graphics (1=ANSI) */
  fprintf(f, "%d\n", s->time_left_min);               /* 5: Minutes remaining */
  fprintf(f, "%s\n", s->user.real_name[0] ? s->user.real_name : s->user.handle); /* 6: Real name */
  fprintf(f, "%d\n", s->user.level);                  /* 7: Security level */
  fprintf(f, "%s\n", s->user.birth_date);             /* 8: Birth date */
  fprintf(f, "%s\n", s->user.phone[0] ? s->user.phone : "000-000-0000"); /* 9: Phone */
  fprintf(f, "%s\n", s->user.street[0] ? s->user.street : "Unknown"); /* 10: City */
  fprintf(f, "0\n");                                  /* 11: COM port */
  fprintf(f, "38400\n");                              /* 12: Baud rate */
  fprintf(f, "%d\n", s->node_num);                    /* 13: Node number */
  
  fclose(f);
  return true;
}

/* =========================================================================
 * Native door launcher
 * ========================================================================= */

static bool door_launch_native(Session* s, const DbDoor* door) {
  char dir[512];
  char door_dir[512];
  char node_dir[512];
  char launch_id[96];

  snprintf(launch_id, sizeof(launch_id), "%ld_%d_%08lx",
           (long)time(NULL), (int)getpid(), (unsigned long)rand());
  path_join(s->cfg.dropfile_path, door->name, door_dir, sizeof(door_dir));
  char node_leaf[32];
  snprintf(node_leaf, sizeof(node_leaf), "node%02d", s->node_num);
  path_join(door_dir, node_leaf, node_dir, sizeof(node_dir));
  path_join(node_dir, launch_id, dir, sizeof(dir));
  ensure_dir(s->cfg.dropfile_path);
  ensure_dir(door_dir);
  ensure_dir(node_dir);
  ensure_dir(dir);

  write_doorsys(s, dir);
  write_door32sys(s, dir);
  write_dorinfo(s, dir);
  write_chaintxt(s, dir);
  write_pcboardsys(s, dir);
  write_callinfo(s, dir);
  write_sfdoors(s, dir);
  if (!write_mutineer_session_json(s, door, dir)) {
    log_error("native door %s: failed to write MUTINEER_SESSION.JSON", door->name);
    send_str(s, "\r\nDoor session setup failed. Contact the sysop.\r\n");
    return false;
  }

  char errbuf[256];
  char** argv = NULL;
  if (!bbs_argv_parse_door_template(door->command, dir, &argv, errbuf, sizeof(errbuf))) {
    log_error("native door %s: invalid command template: %s", door->name, errbuf);
    send_str(s, "\r\nDoor command is invalid. Contact the sysop.\r\n");
    return false;
  }

  char activity[128];
  snprintf(activity, sizeof(activity), "door:%s", door->name);
  if (s->db) db_node_upsert(s->db, s->node_num, s->user.id, "online", activity, s->ip);

  log_info("launching native door %s: %s", door->name, argv[0]);
  int timeout = door->timeout_sec > 0 ? door->timeout_sec : s->cfg.door_default_timeout_sec;
  int caller_fd = (s && s->fd > STDERR_FILENO) ? s->fd : -1;
  BbsProcessResult pres;
  errbuf[0] = '\0';
  bool ok = bbs_exec_argv_cancel(argv, door->name, door->workdir,
                                 caller_fd, caller_fd, caller_fd, timeout,
                                 caller_fd,
                                 &pres, errbuf, sizeof(errbuf));
  if (!ok && errbuf[0]) log_error("native door %s: %s", door->name, errbuf);
  if (ok) ingest_leaderboard_result(s, door, dir);
  bbs_argv_free(argv);
  if (s->db) db_node_upsert(s->db, s->node_num, s->user.id, "online", "menu", s->ip);
  if (ok && s->cfg.door_cleanup_on_exit) {
    if (!remove_tree_recursive(dir))
      log_warn("native door cleanup: recursive remove failed for %s: %s", dir, strerror(errno));
    else
      log_info("native door cleanup: removed launch dropdir %s", dir);
  } else if (!ok && !s->cfg.door_keep_failed_runs) {
    log_info("native door cleanup: preserving failed launch dropdir %s for diagnostics", dir);
  }
  return ok;
}

/* =========================================================================
 * DOSBox door launcher
 * ========================================================================= */

static void dosbox_write_dropfile(Session* s, const DbDoor* door,
                                  const DosboxManifest* m,
                                  const char* runtime_root) {
  char dest[1024];
  if (m->dropfile_dest[0])
    path_join(runtime_root, m->dropfile_dest, dest, sizeof(dest));
  else
    path_join(runtime_root, "game", dest, sizeof(dest));
  ensure_dir(dest);

  const char* fmt = m->dropfile[0] ? m->dropfile : door->dropfile;
  if (!fmt || !fmt[0]) fmt = "DOOR.SYS";

  /* Always write the primary format to the destination */
  if (strcasecmp(fmt, "DOOR.SYS") == 0)       write_doorsys(s, dest);
  else if (strcasecmp(fmt, "DOOR32.SYS") == 0) write_door32sys(s, dest);
  else if (strcasecmp(fmt, "DORINFO1.DEF") == 0) write_dorinfo(s, dest);
  else if (strcasecmp(fmt, "CHAIN.TXT") == 0)  write_chaintxt(s, dest);
  else if (strcasecmp(fmt, "PCBOARD.SYS") == 0) write_pcboardsys(s, dest);
  else if (strcasecmp(fmt, "CALLINFO.BBS") == 0) write_callinfo(s, dest);
  else if (strcasecmp(fmt, "SFDOORS.DAT") == 0) write_sfdoors(s, dest);
  else {
    /* Unknown format: write DOOR.SYS as fallback */
    log_warn("door %s: unknown dropfile format '%s', falling back to DOOR.SYS", door->name, fmt);
    write_doorsys(s, dest);
  }
}

static bool door_launch_dosbox(Session* s, const DbDoor* door) {
  if (!s->cfg.dosbox_path[0]) {
    log_error("door %s: dosbox_path not configured", door->name);
    send_str(s, "\r\nDOSBox is not configured on this system. Contact the sysop.\r\n");
    return false;
  }
  if (!door->manifest[0]) {
    log_error("door %s: no manifest path set", door->name);
    send_str(s, "\r\nDoor manifest is missing. Contact the sysop.\r\n");
    return false;
  }

  char errbuf[256];
  DosboxManifest m;
  if (!dosbox_manifest_parse(door->manifest, &m, errbuf, sizeof(errbuf))) {
    log_error("door %s: manifest parse failed: %s", door->name, errbuf);
    send_str(s, "\r\nDoor configuration error. Contact the sysop.\r\n");
    return false;
  }
  if (!dosbox_manifest_validate(&m, errbuf, sizeof(errbuf))) {
    log_error("door %s: manifest invalid: %s", door->name, errbuf);
    send_str(s, "\r\nDoor configuration error. Contact the sysop.\r\n");
    return false;
  }

  /* Determine timeout */
  int timeout_sec = door->timeout_sec > 0 ? door->timeout_sec
                  : m.timeout_sec > 0      ? m.timeout_sec
                  : s->cfg.door_default_timeout_sec;

  /* Generate a unique launch ID */
  char launch_id[64];
  snprintf(launch_id, sizeof(launch_id), "%ld_%d", (long)time(NULL), (int)getpid());

  /* Use manifest name, fallback to door name */
  if (!m.name[0]) snprintf(m.name, sizeof(m.name), "%s", door->name);

  /* Create runtime tree */
  char runtime_root[1024];
  const char* runtime_base = s->cfg.door_runtime_path[0]
                             ? s->cfg.door_runtime_path
                             : "data/door_runtime";
  if (!dosbox_prepare_runtime(&m, runtime_base, s->node_num, launch_id,
                              runtime_root, sizeof(runtime_root),
                              errbuf, sizeof(errbuf))) {
    log_error("door %s: runtime prep failed: %s", door->name, errbuf);
    send_str(s, "\r\nFailed to prepare door runtime. Contact the sysop.\r\n");
    return false;
  }
  log_info("door %s: runtime tree at %s", door->name, runtime_root);

  /* Write primary dropfile into runtime tree */
  dosbox_write_dropfile(s, door, &m, runtime_root);

  /* Generate DOSBox config */
  char game_dir[1024], conf_path[1024];
  path_join(runtime_root, "game", game_dir, sizeof(game_dir));
  path_join(runtime_root, "dosbox.conf", conf_path, sizeof(conf_path));
  if (!write_mutineer_session_json(s, door, game_dir)) {
    log_error("door %s: failed to write leaderboard session metadata", door->name);
    dosbox_cleanup_runtime(runtime_root);
    return false;
  }

  if (!dosbox_build_conf(&m, game_dir, conf_path, errbuf, sizeof(errbuf))) {
    log_error("door %s: conf generation failed: %s", door->name, errbuf);
    send_str(s, "\r\nFailed to configure DOSBox. Contact the sysop.\r\n");
    dosbox_cleanup_runtime(runtime_root);
    return false;
  }

  log_info("door %s: launching dosbox pid conf=%s socket_fd=%d timeout=%ds",
           door->name, conf_path, s->fd, timeout_sec);

  /* Update node activity */
  char activity[80];
  snprintf(activity, sizeof(activity), "door:%s", door->name);
  db_node_upsert(s->db, s->node_num, s->user.id, "online", activity, s->ip);

  /* Build argv for execv */
  char fd_str[16];
  snprintf(fd_str, sizeof(fd_str), "%d", s->fd);
  char* argv[8];
  int argc = 0;
  argv[argc++] = s->cfg.dosbox_path;
  argv[argc++] = "-conf";
  argv[argc++] = conf_path;
  argv[argc++] = "-socket";
  argv[argc++] = fd_str;
  argv[argc++] = "-exit";
  argv[argc]   = NULL;

  pid_t pid = fork();
  if (pid < 0) {
    log_error("door %s: fork failed: %s", door->name, strerror(errno));
    send_str(s, "\r\nFailed to launch DOSBox. Contact the sysop.\r\n");
    db_node_upsert(s->db, s->node_num, s->user.id, "online", "menu", s->ip);
    dosbox_cleanup_runtime(runtime_root);
    return false;
  }

  if (pid == 0) {
    /* Child: clear CLOEXEC on the telnet socket so DOSBox inherits it */
    int flags = fcntl(s->fd, F_GETFD);
    if (flags >= 0) fcntl(s->fd, F_SETFD, flags & ~FD_CLOEXEC);

    /* Redirect stdout/stderr to logs dir */
    char log_path[1024];
    char logs_dir[1024];
    path_join(runtime_root, "logs", logs_dir, sizeof(logs_dir));
    path_join(logs_dir, "dosbox.log", log_path, sizeof(log_path));
    int log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd >= 0) {
      dup2(log_fd, STDOUT_FILENO);
      dup2(log_fd, STDERR_FILENO);
      close(log_fd);
    }

    execv(s->cfg.dosbox_path, argv);
    /* If execv fails, write to stderr (now the log file) and exit */
    fprintf(stderr, "execv failed: %s\n", strerror(errno));
    _exit(127);
  }

  /* Parent: supervise child with timeout */
  time_t start = time(NULL);
  bool timed_out = false;
  int status = 0;

  while (1) {
    pid_t w = waitpid(pid, &status, WNOHANG);
    if (w == pid) break;
    if (w < 0 && errno != EINTR) break;

    sleep(1);

    if (timeout_sec > 0 && difftime(time(NULL), start) >= (double)timeout_sec) {
      log_warn("door %s: timeout after %ds, terminating dosbox pid %d",
               door->name, timeout_sec, (int)pid);
      send_str(s, "\r\n\r\nDoor session timed out.\r\n");
      kill(pid, SIGTERM);
      sleep(2);
      if (waitpid(pid, &status, WNOHANG) != pid) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
      }
      timed_out = true;
      break;
    }
  }

  bool success = !timed_out && WIFEXITED(status) && WEXITSTATUS(status) == 0;
  if (success) ingest_leaderboard_result(s, door, game_dir);

  if (WIFEXITED(status))
    log_info("door %s: dosbox exited with code %d", door->name, WEXITSTATUS(status));
  else if (WIFSIGNALED(status))
    log_warn("door %s: dosbox killed by signal %d", door->name, WTERMSIG(status));

  /* Restore node activity */
  db_node_upsert(s->db, s->node_num, s->user.id, "online", "menu", s->ip);

  /* Cleanup */
  bool do_cleanup = success
    ? (m.cleanup_on_exit || s->cfg.door_cleanup_on_exit)
    : !s->cfg.door_keep_failed_runs;

  if (do_cleanup) {
    dosbox_cleanup_runtime(runtime_root);
  } else {
    log_info("door %s: keeping runtime tree at %s", door->name, runtime_root);
  }

  return success;
}

/* =========================================================================
 * Buccaneer door launcher
 * ========================================================================= */

static void bucc_term_print_cb(void* ctx, const char* text) {
  Session* s = (Session*)ctx;
  if (s && text) send_str(s, text);
}

static void bucc_term_println_cb(void* ctx, const char* text) {
  Session* s = (Session*)ctx;
  if (!s) return;
  if (text) send_str(s, text);
  send_str(s, "\r\n");
}

static void bucc_term_cls_cb(void* ctx) {
  Session* s = (Session*)ctx;
  if (s) send_str(s, "\x1b[2J\x1b[H");
}

static void bucc_term_color_cb(void* ctx, int fg, int bg) {
  Session* s = (Session*)ctx;
  if (!s) return;
  char buf[64];
  int fg_code = 30 + (fg % 8);
  int bg_code = 40 + (bg % 8);
  snprintf(buf, sizeof(buf), "\x1b[%d;%dm", fg_code, bg_code);
  send_str(s, buf);
}

static void bucc_term_gotoxy_cb(void* ctx, int x, int y) {
  Session* s = (Session*)ctx;
  if (!s) return;
  char buf[64];
  if (x < 1) x = 1;
  if (y < 1) y = 1;
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y, x);
  send_str(s, buf);
}

static char* bucc_term_getkey_cb(void* ctx) {
  Session* s = (Session*)ctx;
  uint8_t ch = 0;
  if (!s || session_readline(s, &ch, sizeof(ch), 300) <= 0) return strdup("");
  char out[2] = { (char)ch, '\0' };
  return strdup(out);
}

static char* bucc_term_input_cb(void* ctx, const char* prompt, int maxlen) {
  Session* s = (Session*)ctx;
  if (!s) return strdup("");
  if (maxlen <= 0 || maxlen > 512) maxlen = 512;
  char* out = calloc((size_t)maxlen + 1, 1);
  if (!out) return NULL;
  if (prompt_line(s, prompt ? prompt : "", out, (size_t)maxlen + 1) <= 0) out[0] = '\0';
  return out;
}

static char* bucc_term_input_password_cb(void* ctx, const char* prompt) {
  return bucc_term_input_cb(ctx, prompt, 128);
}

static void bucc_term_pause_cb(void* ctx, const char* prompt) {
  Session* s = (Session*)ctx;
  if (!s) return;
  char line[8];
  prompt_line(s, prompt ? prompt : "\r\nPress Enter to continue...", line, sizeof(line));
}

static int bucc_term_width_cb(void* ctx) {
  Session* s = (Session*)ctx;
  return (s && s->tn.cols > 0) ? s->tn.cols : 80;
}

static int bucc_term_height_cb(void* ctx) {
  Session* s = (Session*)ctx;
  return (s && s->tn.rows > 0) ? s->tn.rows : 24;
}

static bool bucc_term_supports_ansi_cb(void* ctx) {
  Session* s = (Session*)ctx;
  return s ? s->ansi != 0 : true;
}

static const char* bucc_user_name_cb(void* ctx) {
  Session* s = (Session*)ctx;
  return s ? s->user.real_name : "";
}

static const char* bucc_user_alias_cb(void* ctx) {
  Session* s = (Session*)ctx;
  return s ? s->user.handle : "";
}

static int64_t bucc_user_id_cb(void* ctx) {
  Session* s = (Session*)ctx;
  return s ? s->user.id : 0;
}

static int bucc_user_security_cb(void* ctx) {
  Session* s = (Session*)ctx;
  return s ? s->user.level : 0;
}

static int bucc_user_time_left_cb(void* ctx) {
  Session* s = (Session*)ctx;
  return s ? s->time_left_min : 0;
}

static int bucc_user_flags_cb(void* ctx) {
  Session* s = (Session*)ctx;
  return s ? (int)s->user.flags : 0;
}

static int bucc_bbs_node_cb(void* ctx) {
  Session* s = (Session*)ctx;
  return s ? s->node_num : 1;
}

static bool bucc_bbs_send_msg_cb(void* ctx, int node, const char* msg) {
  (void)node;
  Session* s = (Session*)ctx;
  if (!s || !msg) return false;
  send_str(s, msg);
  return true;
}

static bucc_array_t* bucc_bbs_online_cb(void* ctx) {
  Session* s = (Session*)ctx;
  bucc_array_t* arr = bucc_array_new(8);
  if (!arr) return NULL;
  if (!s) return arr;
  DbNode nodes[64];
  int n = s->db ? db_node_list(s->db, nodes, 64) : 0;
  for (int i = 0; i < n; i++) {
    bucc_map_t* item = bucc_map_new(8);
    if (!item) continue;
    bucc_map_set_cstr(item, "node", BUCC_I64_VAL(nodes[i].node_num));
    bucc_map_set_cstr(item, "user_id", BUCC_I64_VAL(nodes[i].user_id));
    bucc_map_set_cstr(item, "handle", bucc_make_string(nodes[i].handle));
    bucc_map_set_cstr(item, "status", bucc_make_string(nodes[i].status));
    bucc_map_set_cstr(item, "activity", bucc_make_string(nodes[i].activity));
    bucc_map_set_cstr(item, "ip", bucc_make_string(nodes[i].ip));
    bucc_value_t v;
    v.kind = BUCC_VAL_MAP;
    v.as.map = item;
    bucc_array_push(arr, v);
  }
  return arr;
}

static Session* bucc_live_session(void* ctx) {
  BuccLiveCtx* live = (BuccLiveCtx*)ctx;
  return live ? live->s : NULL;
}

static const char* bucc_live_scope(void* ctx) {
  BuccLiveCtx* live = (BuccLiveCtx*)ctx;
  return live && live->scope[0] ? live->scope : "bbs";
}

static char* bucc_kv_get_cb(void* ctx, const char* key, const char* default_val) {
  Session* s = bucc_live_session(ctx);
  char value[1024];
  if (!s || !s->db || !key || !db_bucc_kv_get(s->db, bucc_live_scope(ctx), key, value, sizeof(value)))
    return strdup(default_val ? default_val : "");
  return strdup(value);
}

static void bucc_kv_set_cb(void* ctx, const char* key, const char* value) {
  Session* s = bucc_live_session(ctx);
  if (s && s->db && key) db_bucc_kv_set(s->db, bucc_live_scope(ctx), key, value ? value : "");
}

static void bucc_kv_delete_cb(void* ctx, const char* key) {
  Session* s = bucc_live_session(ctx);
  if (s && s->db && key) db_bucc_kv_delete(s->db, bucc_live_scope(ctx), key);
}

static bool bucc_kv_exists_cb(void* ctx, const char* key) {
  Session* s = bucc_live_session(ctx);
  return s && s->db && key && db_bucc_kv_exists(s->db, bucc_live_scope(ctx), key);
}

static bool bucc_text_resolve(void* ctx, const char* rel, char* out, size_t out_cap) {
  BuccLiveCtx* live = (BuccLiveCtx*)ctx;
  if (!live || !rel || !path_is_safe_relative(rel)) return false;
  char root[512];
  snprintf(root, sizeof(root), "%s", live->text_root);
  if (!ensure_dir(root)) return false;
  path_join(root, rel, out, out_cap);
  return true;
}

static char* bucc_text_read_all_cb(void* ctx, const char* path) {
  char resolved[1024];
  if (!bucc_text_resolve(ctx, path, resolved, sizeof(resolved))) return NULL;
  return file_read_all(resolved, NULL);
}

static bucc_array_t* bucc_text_read_lines_cb(void* ctx, const char* path) {
  char* text = bucc_text_read_all_cb(ctx, path);
  bucc_array_t* arr = bucc_array_new(8);
  if (!arr) {
    free(text);
    return NULL;
  }
  if (!text) return arr;
  char* save = NULL;
  for (char* line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
  {
    size_t n = strlen(line);
    if (n && line[n - 1] == '\r') line[n - 1] = '\0';
    bucc_array_push(arr, bucc_make_string(line));
  }
  free(text);
  return arr;
}

static bool bucc_text_write_all_cb(void* ctx, const char* path, const char* content) {
  char resolved[1024];
  if (!bucc_text_resolve(ctx, path, resolved, sizeof(resolved))) return false;
  FILE* f = fopen(resolved, "wb");
  if (!f) return false;
  const char* s = content ? content : "";
  bool ok = fwrite(s, 1, strlen(s), f) == strlen(s);
  fclose(f);
  return ok;
}

static bool bucc_text_append_cb(void* ctx, const char* path, const char* content) {
  char resolved[1024];
  if (!bucc_text_resolve(ctx, path, resolved, sizeof(resolved))) return false;
  FILE* f = fopen(resolved, "ab");
  if (!f) return false;
  const char* s = content ? content : "";
  bool ok = fwrite(s, 1, strlen(s), f) == strlen(s);
  fclose(f);
  return ok;
}

static bool bucc_text_exists_cb(void* ctx, const char* path) {
  char resolved[1024];
  struct stat st;
  return bucc_text_resolve(ctx, path, resolved, sizeof(resolved)) && stat(resolved, &st) == 0 && S_ISREG(st.st_mode);
}

static bucc_map_t* bucc_user_map(const DbUser* u) {
  bucc_map_t* m = bucc_map_new(12);
  if (!m || !u) return m;
  bucc_map_set_cstr(m, "id", BUCC_I64_VAL(u->id));
  bucc_map_set_cstr(m, "handle", bucc_make_string(u->handle));
  bucc_map_set_cstr(m, "real_name", bucc_make_string(u->real_name));
  bucc_map_set_cstr(m, "level", BUCC_I64_VAL(u->level));
  bucc_map_set_cstr(m, "flags", BUCC_I64_VAL(u->flags));
  bucc_map_set_cstr(m, "city_state", bucc_make_string(u->city_state));
  return m;
}

static bucc_array_t* bucc_users_find_cb(void* ctx, bucc_map_t* query, int limit) {
  (void)query;
  Session* s = bucc_live_session(ctx);
  bucc_array_t* arr = bucc_array_new(1);
  if (!arr || !s) return arr;
  if (limit == 0 || limit > 0) {
    bucc_value_t v = {.kind = BUCC_VAL_MAP, .as.map = bucc_user_map(&s->user)};
    bucc_array_push(arr, v);
  }
  return arr;
}

static bucc_map_t* bucc_users_get_cb(void* ctx, int64_t id) {
  Session* s = bucc_live_session(ctx);
  if (!s || id != s->user.id) return NULL;
  return bucc_user_map(&s->user);
}

static bucc_map_t* bucc_msg_map(const DbMessage* msg) {
  bucc_map_t* m = bucc_map_new(12);
  if (!m || !msg) return m;
  bucc_map_set_cstr(m, "id", BUCC_I64_VAL(msg->id));
  bucc_map_set_cstr(m, "area_id", BUCC_I64_VAL(msg->area_id));
  bucc_map_set_cstr(m, "user_id", BUCC_I64_VAL(msg->user_id));
  bucc_map_set_cstr(m, "from", bucc_make_string(msg->from_name[0] ? msg->from_name : msg->user_handle));
  bucc_map_set_cstr(m, "to", bucc_make_string(msg->to_name));
  bucc_map_set_cstr(m, "subject", bucc_make_string(msg->subject));
  bucc_map_set_cstr(m, "body", bucc_make_string(msg->body));
  bucc_map_set_cstr(m, "posted_at", bucc_make_string(msg->created_at));
  return m;
}

static int bucc_area_id(const char* area) {
  if (!area || !area[0]) return 1;
  char* end = NULL;
  long id = strtol(area, &end, 10);
  return end && *end == '\0' && id > 0 ? (int)id : 1;
}

static bucc_map_t* bucc_msg_read_cb(void* ctx, const char* area, int64_t id) {
  (void)area;
  Session* s = bucc_live_session(ctx);
  DbMessage msg;
  if (!s || !s->db || !db_message_get(s->db, (int)id, &msg)) return NULL;
  return bucc_msg_map(&msg);
}

static bucc_array_t* bucc_msg_list_cb(void* ctx, const char* area, int limit, int offset) {
  (void)offset;
  Session* s = bucc_live_session(ctx);
  bucc_array_t* arr = bucc_array_new(8);
  if (!arr || !s || !s->db) return arr;
  if (limit <= 0 || limit > 50) limit = 50;
  DbMessage msgs[50];
  int n = db_messages_list(s->db, bucc_area_id(area), msgs, limit);
  for (int i = 0; i < n; i++) {
    bucc_value_t v = {.kind = BUCC_VAL_MAP, .as.map = bucc_msg_map(&msgs[i])};
    bucc_array_push(arr, v);
  }
  return arr;
}

static int64_t bucc_msg_post_cb(void* ctx, const char* area, bucc_map_t* msg) {
  Session* s = bucc_live_session(ctx);
  if (!s || !s->db || !msg) return 0;
  bucc_value_t* subject = bucc_map_get_cstr(msg, "subject");
  bucc_value_t* body = bucc_map_get_cstr(msg, "body");
  const char* subj = (subject && BUCC_IS_STRING(*subject)) ? subject->as.str->data : "Buccaneer post";
  const char* text = (body && BUCC_IS_STRING(*body)) ? body->as.str->data : "";
  if (!db_message_post(s->db, bucc_area_id(area), s->user.id, subj, text, 0)) return 0;
  return db_last_insert_id(s->db);
}

static bucc_map_t* bucc_file_map(const DbFileRec* file) {
  bucc_map_t* m = bucc_map_new(12);
  if (!m || !file) return m;
  bucc_map_set_cstr(m, "id", BUCC_I64_VAL(file->id));
  bucc_map_set_cstr(m, "area_id", BUCC_I64_VAL(file->area_id));
  bucc_map_set_cstr(m, "filename", bucc_make_string(file->filename));
  bucc_map_set_cstr(m, "description", bucc_make_string(file->desc));
  bucc_map_set_cstr(m, "size_bytes", BUCC_I64_VAL(file->size_bytes));
  bucc_map_set_cstr(m, "uploaded_at", bucc_make_string(file->uploaded_at));
  bucc_map_set_cstr(m, "uploader", bucc_make_string(file->uploader));
  bucc_map_set_cstr(m, "downloads", BUCC_I64_VAL(file->download_count));
  return m;
}

static bucc_array_t* bucc_file_list_cb(void* ctx, const char* area, int limit, int offset) {
  (void)offset;
  Session* s = bucc_live_session(ctx);
  bucc_array_t* arr = bucc_array_new(8);
  if (!arr || !s || !s->db) return arr;
  if (limit <= 0 || limit > 50) limit = 50;
  DbFileArea fa;
  if (!db_file_area_get(s->db, bucc_area_id(area), &fa)) return arr;
  DbFileRec files[50];
  int n = db_file_list(&fa, s->db, files, limit);
  for (int i = 0; i < n; i++) {
    bucc_value_t v = {.kind = BUCC_VAL_MAP, .as.map = bucc_file_map(&files[i])};
    bucc_array_push(arr, v);
  }
  return arr;
}

static bucc_map_t* bucc_file_info_cb(void* ctx, const char* area, int64_t id) {
  (void)area;
  Session* s = bucc_live_session(ctx);
  DbFileRec file;
  if (!s || !s->db || !db_file_get(s->db, (int)id, &file)) return NULL;
  return bucc_file_map(&file);
}

static int64_t bucc_data_insert_cb(void* ctx, const char* dataset, bucc_map_t* record) {
  Session* s = bucc_live_session(ctx);
  if (!s || !s->db || !dataset || !record) return 0;
  bucc_value_t v = {.kind = BUCC_VAL_MAP, .as.map = record};
  char* value = bucc_value_to_cstring(v);
  int64_t id = db_bucc_data_insert(s->db, bucc_live_scope(ctx), dataset, value ? value : "");
  free(value);
  return id;
}

static int bucc_data_update_cb(void* ctx, const char* dataset, int64_t id, bucc_map_t* fields) {
  Session* s = bucc_live_session(ctx);
  if (!s || !s->db || !dataset || !fields) return 0;
  bucc_value_t v = {.kind = BUCC_VAL_MAP, .as.map = fields};
  char* value = bucc_value_to_cstring(v);
  bool ok = db_bucc_data_update(s->db, bucc_live_scope(ctx), dataset, id, value ? value : "");
  free(value);
  return ok ? 1 : 0;
}

static int bucc_data_delete_cb(void* ctx, const char* dataset, int64_t id) {
  Session* s = bucc_live_session(ctx);
  return s && s->db && dataset && db_bucc_data_delete(s->db, bucc_live_scope(ctx), dataset, id) ? 1 : 0;
}

static bucc_map_t* bucc_data_get_cb(void* ctx, const char* dataset, int64_t id) {
  Session* s = bucc_live_session(ctx);
  char value[512];
  if (!s || !s->db || !dataset || !db_bucc_data_get(s->db, bucc_live_scope(ctx), dataset, id, value, sizeof(value))) return NULL;
  bucc_map_t* m = bucc_map_new(4);
  if (!m) return NULL;
  bucc_map_set_cstr(m, "id", BUCC_I64_VAL(id));
  bucc_map_set_cstr(m, "dataset", bucc_make_string(dataset));
  bucc_map_set_cstr(m, "value", bucc_make_string(value));
  return m;
}

static bucc_array_t* bucc_data_find_cb(void* ctx, const char* dataset, bucc_map_t* query, const char* order, int limit, int offset) {
  (void)query; (void)order; (void)offset;
  Session* s = bucc_live_session(ctx);
  bucc_array_t* arr = bucc_array_new(8);
  if (!arr || !s || !s->db || !dataset) return arr;
  if (limit <= 0 || limit > 50) limit = 50;
  int64_t ids[50];
  char values[50][512];
  int n = db_bucc_data_find(s->db, bucc_live_scope(ctx), dataset, ids, values, limit);
  for (int i = 0; i < n; i++) {
    bucc_map_t* m = bucc_map_new(4);
    if (!m) continue;
    bucc_map_set_cstr(m, "id", BUCC_I64_VAL(ids[i]));
    bucc_map_set_cstr(m, "dataset", bucc_make_string(dataset));
    bucc_map_set_cstr(m, "value", bucc_make_string(values[i]));
    bucc_value_t v = {.kind = BUCC_VAL_MAP, .as.map = m};
    bucc_array_push(arr, v);
  }
  return arr;
}

static int64_t bucc_data_count_cb(void* ctx, const char* dataset, bucc_map_t* query) {
  (void)query;
  Session* s = bucc_live_session(ctx);
  return s && s->db && dataset ? db_bucc_data_count(s->db, bucc_live_scope(ctx), dataset) : 0;
}

static bool bucc_data_tx_cb(void* ctx) {
  (void)ctx;
  return true;
}

static bool db_door_find_by_name_or_id(BbsDb* db, const char* target, DbDoor* out) {
  if (!db || !target || !target[0] || !out) return false;
  char* end = NULL;
  long id = strtol(target, &end, 10);
  if (end && *end == '\0' && id > 0 && db_door_get(db, (int)id, out)) return true;
  DbDoor doors[128];
  int n = db_doors_list(db, doors, 128);
  for (int i = 0; i < n; i++) {
    if (!strcasecmp(doors[i].name, target)) {
      *out = doors[i];
      return true;
    }
  }
  return false;
}

static bool door_launch_bucc(Session* s, const DbDoor* door) {
  static __thread int chain_depth = 0;
  if (!door->manifest[0]) {
    log_error("door %s: no Buccaneer manifest path set", door->name);
    send_str(s, "\r\nDoor manifest is missing. Contact the sysop.\r\n");
    return false;
  }

  bucc_door_runner_t* runner = bucc_door_runner_new();
  if (!runner) {
    send_str(s, "\r\nUnable to start Buccaneer door.\r\n");
    return false;
  }

  bucc_door_status_t load_status = bucc_door_load(runner, door->manifest);
  if (load_status != DOOR_OK) {
    log_error("door %s: Buccaneer load failed: %s", door->name, bucc_door_status_string(load_status));
    send_str(s, "\r\nDoor configuration error. Contact the sysop.\r\n");
    bucc_door_runner_free(runner);
    return false;
  }

  bucc_session_info_t info = {
    .user_id = s->user.id,
    .user_name = s->user.real_name[0] ? s->user.real_name : s->user.handle,
    .user_alias = s->user.handle,
    .user_security = s->user.level,
    .time_remaining = s->time_left_min,
    .node_number = s->node_num,
    .ansi_enabled = s->ansi != 0,
    .term_width = s->tn.cols > 0 ? s->tn.cols : 80,
    .term_height = s->tn.rows > 0 ? s->tn.rows : 24
  };
  bucc_door_set_session(runner, &info);

  bucc_term_api_t term_api = {
    .print = bucc_term_print_cb,
    .println = bucc_term_println_cb,
    .cls = bucc_term_cls_cb,
    .color = bucc_term_color_cb,
    .gotoxy = bucc_term_gotoxy_cb,
    .getkey = bucc_term_getkey_cb,
    .input = bucc_term_input_cb,
    .input_password = bucc_term_input_password_cb,
    .pause = bucc_term_pause_cb,
    .get_width = bucc_term_width_cb,
    .get_height = bucc_term_height_cb,
    .supports_ansi = bucc_term_supports_ansi_cb
  };
  bucc_user_api_t user_api = {
    .get_name = bucc_user_name_cb,
    .get_alias = bucc_user_alias_cb,
    .get_id = bucc_user_id_cb,
    .get_security = bucc_user_security_cb,
    .get_time_left = bucc_user_time_left_cb,
    .get_flags = bucc_user_flags_cb
  };
  bucc_bbs_api_t bbs_api = {
    .send_msg = bucc_bbs_send_msg_cb,
    .get_online = bucc_bbs_online_cb,
    .get_node = bucc_bbs_node_cb
  };
  bucc_kv_api_t kv_api = {
    .get = bucc_kv_get_cb,
    .set = bucc_kv_set_cb,
    .delete_key = bucc_kv_delete_cb,
    .exists = bucc_kv_exists_cb
  };
  bucc_text_api_t text_api = {
    .read_all = bucc_text_read_all_cb,
    .read_lines = bucc_text_read_lines_cb,
    .write_all = bucc_text_write_all_cb,
    .append = bucc_text_append_cb,
    .exists = bucc_text_exists_cb
  };
  bucc_users_api_t users_api = {
    .find = bucc_users_find_cb,
    .get = bucc_users_get_cb
  };
  bucc_msg_api_t msg_api = {
    .read = bucc_msg_read_cb,
    .list = bucc_msg_list_cb,
    .post = bucc_msg_post_cb
  };
  bucc_file_api_t file_api = {
    .list = bucc_file_list_cb,
    .info = bucc_file_info_cb
  };
  bucc_data_api_t data_api = {
    .insert = bucc_data_insert_cb,
    .update = bucc_data_update_cb,
    .delete_record = bucc_data_delete_cb,
    .get = bucc_data_get_cb,
    .find = bucc_data_find_cb,
    .count = bucc_data_count_cb,
    .begin_tx = bucc_data_tx_cb,
    .commit_tx = bucc_data_tx_cb,
    .rollback_tx = bucc_data_tx_cb
  };
  bucc_leaderboard_api_t leaderboard_api = {
    .enabled = bucc_leaderboard_enabled_cb,
    .submit = bucc_leaderboard_submit_cb
  };

  BuccLiveCtx live;
  memset(&live, 0, sizeof(live));
  live.s = s;
  live.door = door;
  snprintf(live.scope, sizeof(live.scope), "door:%d:%s", door->id, door->name);
  char bucc_root[512];
  path_join(s->cfg.data_path, "buccaneer", bucc_root, sizeof(bucc_root));
  ensure_dir(bucc_root);
  path_join(bucc_root, door->name[0] ? door->name : "door", live.text_root, sizeof(live.text_root));
  ensure_dir(live.text_root);

  bucc_host_set_term_api(runner->host_ctx, &term_api, s);
  bucc_host_set_user_api(runner->host_ctx, &user_api, s);
  bucc_host_set_bbs_api(runner->host_ctx, &bbs_api, s);
  bucc_host_set_kv_api(runner->host_ctx, &kv_api, &live);
  bucc_host_set_text_api(runner->host_ctx, &text_api, &live);
  bucc_host_set_users_api(runner->host_ctx, &users_api, &live);
  bucc_host_set_msg_api(runner->host_ctx, &msg_api, &live);
  bucc_host_set_file_api(runner->host_ctx, &file_api, &live);
  bucc_host_set_data_api(runner->host_ctx, &data_api, &live);
  bucc_host_set_leaderboard_api(runner->host_ctx, &leaderboard_api, &live);

  char activity[128];
  snprintf(activity, sizeof(activity), "door:%s", door->name);
  if (s->db) db_node_upsert(s->db, s->node_num, s->user.id, "online", activity, s->ip);

  bucc_door_result_t result = bucc_door_run(runner);
  bool ok = result.status == DOOR_OK || result.status == DOOR_CHAIN_REQUESTED;

  if (result.status == DOOR_CHAIN_REQUESTED) {
    log_info("door %s: Buccaneer chain requested: %s", door->name,
             result.chain_target ? result.chain_target : "(none)");
    if (chain_depth >= 8) {
      log_error("door %s: Buccaneer chain depth exceeded", door->name);
      ok = false;
    } else if (result.chain_target && result.chain_target[0]) {
      DbDoor next;
      if (db_door_find_by_name_or_id(s->db, result.chain_target, &next)) {
        chain_depth++;
        ok = door_launch(s, &next);
        chain_depth--;
      } else {
        log_error("door %s: Buccaneer chain target not found: %s", door->name, result.chain_target);
        send_str(s, "\r\nChained door target was not found.\r\n");
        ok = false;
      }
    }
  } else if (result.status != DOOR_OK) {
    log_error("door %s: Buccaneer error: %s%s%s", door->name,
              bucc_door_status_string(result.status),
              result.error_message ? ": " : "",
              result.error_message ? result.error_message : "");
    send_str(s, "\r\nBuccaneer door error: ");
    send_str(s, result.error_message ? result.error_message : bucc_door_status_string(result.status));
    send_str(s, "\r\n");
  }

  free(result.error_message);
  free(result.chain_target);
  bucc_value_release(&result.chain_args);
  bucc_door_runner_free(runner);

  if (s->db) db_node_upsert(s->db, s->node_num, s->user.id, "online", "menu", s->ip);
  return ok;
}

/* =========================================================================
 * Dispatcher
 * ========================================================================= */

bool door_launch(Session* s, const DbDoor* door) {
  if (!s || !door) return false;

  if (!door->enabled) {
    log_warn("door %s is disabled", door->name);
    send_str(s, "\r\nThis door is currently disabled.\r\n");
    return false;
  }

  if (strcasecmp(door->runner, "dosbox") == 0) {
    return door_launch_dosbox(s, door);
  }
  if (strcasecmp(door->runner, "bucc") == 0) {
    return door_launch_bucc(s, door);
  }

  /* Default: native runner */
  if (!door->command[0]) {
    log_error("door %s: no command configured for native runner", door->name);
    return false;
  }
  return door_launch_native(s, door);
}

bool protocol_launch(Session* s, const DbProtocol* proto, const char* filepath, const char* direction) {
  if (!s || !proto || !filepath) return false;
  
  log_info("launch protocol %s (%s): %s", proto->name, direction, filepath);

  char errbuf[256];
  char** argv = NULL;
  if (!bbs_argv_parse_template(proto->command, filepath, &argv, errbuf, sizeof(errbuf))) {
    log_error("protocol %s: invalid command template: %s", proto->name, errbuf);
    return false;
  }

  char activity[128];
  snprintf(activity, sizeof(activity), "protocol:%s:%s", proto->name, direction ? direction : "");
  if (s->db) db_node_upsert(s->db, s->node_num, s->user.id, "online", activity, s->ip);

  int timeout = s->cfg.protocol_timeout_sec > 0
              ? s->cfg.protocol_timeout_sec
              : s->cfg.door_default_timeout_sec;
  BbsProcessResult pres;
  errbuf[0] = '\0';
  bool ok = bbs_exec_argv_cancel(argv, proto->name, NULL,
                                 s->fd, s->fd, s->fd, timeout, s->fd,
                                 &pres, errbuf, sizeof(errbuf));
  if (!ok && errbuf[0]) log_error("protocol %s: %s", proto->name, errbuf);
  bbs_argv_free(argv);
  if (s->db) db_node_upsert(s->db, s->node_num, s->user.id, "online", "menu", s->ip);
  return ok;
}
