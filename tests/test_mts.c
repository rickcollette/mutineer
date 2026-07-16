#include "mts.h"
#include "mts_command.h"
#include "mts_render.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern bbs_rc_t bbs_plugin_query(uint32_t, const bbs_host_api_t *,
                                 bbs_plugin_desc_t *);
typedef struct {
  const char **lines;
  size_t at;
  char output[16384];
  size_t used;
  char dir[512];
} fake_session_t;
static bbs_rc_t fw(bbs_session_t *s, const void *p, size_t n) {
  fake_session_t *f = (fake_session_t *)s;
  if (n > sizeof f->output - f->used - 1)
    n = sizeof f->output - f->used - 1;
  memcpy(f->output + f->used, p, n);
  f->used += n;
  f->output[f->used] = 0;
  return BBS_OK;
}
static bbs_rc_t fp(bbs_session_t *s, const char *fmt, ...) {
  char b[1024];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  return fw(s, b, (size_t)(n < 0 ? 0 : n));
}
static bbs_rc_t fr(bbs_session_t *s, char *out, size_t n, int echo,
                   uint32_t ms) {
  (void)echo;
  (void)ms;
  fake_session_t *f = (fake_session_t *)s;
  if (!f->lines[f->at])
    return BBS_EIO;
  snprintf(out, n, "%s", f->lines[f->at++]);
  return BBS_OK;
}
static bbs_rc_t fline(bbs_session_t *s, char *out, size_t n, int echo) {
  return fr(s, out, n, echo, 1000);
}
static bbs_rc_t fcls(bbs_session_t *s) {
  (void)s;
  return BBS_OK;
}
static const char *fname(bbs_session_t *s) {
  (void)s;
  return "Tester";
}
static uint32_t fid(bbs_session_t *s) {
  (void)s;
  return 77;
}
static uint32_t fflags(bbs_session_t *s) {
  (void)s;
  return 1u;
}
static uint32_t frestricted(bbs_session_t *s) {
  (void)s;
  return 1u << 1;
}
static int fnode(bbs_session_t *s) {
  (void)s;
  return 7;
}
static int fyes(bbs_session_t *s) {
  (void)s;
  return 1;
}
static bbs_rc_t fdir(const char *id, char *out, size_t n) {
  (void)id;
  const char *d = getenv("MTS_FAKE_DIR");
  snprintf(out, n, "%s", d ? d : "/tmp");
  return BBS_OK;
}
static void flog(bbs_log_level_t l, const char *s, const char *m) {
  (void)l;
  (void)s;
  (void)m;
}

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);             \
      return 1;                                                                \
    }                                                                          \
  } while (0)

typedef struct {
  mts_session_t *s;
  int count;
} publish_job_t;
static void *publish_many(void *arg) {
  publish_job_t *j = arg;
  for (int i = 0; i < j->count; i++)
    mts_publish(j->s, MTS_PUBLIC, j->s->room_id, 0, NULL, "stress");
  return NULL;
}

int main(void) {
  size_t command_count = 0;
  const mts_command_def_t *commands = mts_commands(&command_count);
  CHECK(command_count > 40);
  for (size_t i = 0; i < command_count; i++) {
    CHECK(commands[i].name && commands[i].name[0] && commands[i].syntax &&
          commands[i].syntax[0] && commands[i].summary &&
          commands[i].summary[0]);
    CHECK(mts_command_find(commands[i].name) == &commands[i]);
    for (size_t j = i + 1; j < command_count; j++)
      CHECK(strcasecmp(commands[i].name, commands[j].name));
  }
  char utf8[64], clipped[64];
  CHECK(mts_utf8_sanitize(utf8, sizeof utf8, "caf\303\251 \033[31m") > 0);
  CHECK(!strcmp(utf8, "caf\303\251 [31m"));
  CHECK(mts_display_width("A\347\225\214e\314\201") == 4);
  mts_clip_width(clipped, sizeof clipped, "A\347\225\214B", 3);
  CHECK(!strcmp(clipped, "A\347\225\214"));
  char clean[64];
  mts_sanitize(clean, sizeof clean, "hello\033[31m\001world");
  CHECK(!strcmp(clean, "hello[31mworld"));
  mts_config_t cfg;
  char error[256] = {0};
  CHECK(mts_config_load(&cfg, "/definitely/missing/mts.conf", error,
                        sizeof error));
  CHECK(cfg.max_users == 128 && cfg.max_rooms == 64 && !cfg.persist_history);
  char dir[] = "/tmp/mts-test-XXXXXX";
  CHECK(mkdtemp(dir) != NULL);
  CHECK(mts_state_init(dir, &cfg, error, sizeof error));
  CHECK(MTS.rooms[0].id == 1 && MTS.rooms[0].permanent);
  CHECK(!mts_store_delete_room(&MTS, 1));
  sqlite3_stmt *schema = NULL;
  CHECK(sqlite3_prepare_v2(MTS.db,
                           "SELECT MAX(version) FROM mts_schema_version", -1,
                           &schema, NULL) == SQLITE_OK);
  CHECK(sqlite3_step(schema) == SQLITE_ROW &&
        sqlite3_column_int(schema, 0) == 3);
  sqlite3_finalize(schema);
  int duration_ok = 0;
  CHECK(mts_parse_duration("15m", &duration_ok) == 900 && duration_ok);
  CHECK(mts_parse_duration("0d", &duration_ok) == 0 && !duration_ok);
  mts_session_t a = {.user_id = 1, .room_id = 1, .active = 1},
                b = {.user_id = 2, .room_id = 1, .active = 1};
  snprintf(a.handle, sizeof a.handle, "Alice");
  snprintf(b.handle, sizeof b.handle, "Bob");
  pthread_mutex_init(&a.inbox_mu, NULL);
  pthread_mutex_init(&b.inbox_mu, NULL);
  pthread_mutex_lock(&MTS.mu);
  MTS.sessions[0] = &a;
  MTS.sessions[1] = &b;
  MTS.session_count = 2;
  pthread_mutex_unlock(&MTS.mu);
  mts_publish(&a, MTS_PUBLIC, 1, 0, NULL, "hello");
  mts_event_t ev[2];
  uint64_t skipped = 0;
  CHECK(mts_drain(&b, ev, 2, &skipped) == 1);
  CHECK(ev[0].sequence > 0 && !strcmp(ev[0].text, "hello"));
  CHECK(mts_store_relation(&MTS, "mts_blocks", 2, 1, 1));
  MTS.cfg.persist_history = 1;
  mts_publish(&a, MTS_PRIVATE, 0, 2, "Bob", "secret");
  CHECK(mts_drain(&b, ev, 2, &skipped) == 0);
  sqlite3_stmt *private_history = NULL;
  CHECK(sqlite3_prepare_v2(
            MTS.db, "SELECT COUNT(*) FROM mts_history WHERE event_type=?", -1,
            &private_history, NULL) == SQLITE_OK);
  sqlite3_bind_int(private_history, 1, MTS_PRIVATE);
  CHECK(sqlite3_step(private_history) == SQLITE_ROW &&
        sqlite3_column_int(private_history, 0) == 0);
  sqlite3_finalize(private_history);
  MTS.cfg.persist_history = 0;
  CHECK(mts_store_invitation(&MTS, 42, 2, 1, 1));
  CHECK(mts_store_is_invited(&MTS, 42, 2));
  CHECK(mts_store_invitation(&MTS, 42, 2, 1, 0));
  CHECK(!mts_store_is_invited(&MTS, 42, 2));
  CHECK(mts_store_sanction(&MTS, "mts_room_bans", 1, 2, 1, "test",
                           mts_now() + 60, 1));
  CHECK(mts_store_is_sanctioned(&MTS, "mts_room_bans", 1, 2));
  CHECK(mts_store_sanction(&MTS, "mts_room_bans", 1, 2, 1, "", 0, 0));
  CHECK(!mts_store_is_sanctioned(&MTS, "mts_room_bans", 1, 2));
  CHECK(mts_store_sanction(&MTS, "mts_global_mutes", 0, 2, 1, "expired",
                           mts_now() - 1, 1));
  CHECK(!mts_store_is_sanctioned(&MTS, "mts_global_mutes", 0, 2));
  CHECK(mts_store_cleanup(&MTS));
  MTS.rooms[1] =
      (mts_room_t){.id = 2, .owner = 1, .is_private = 1, .active = 1};
  snprintf(MTS.rooms[1].name, sizeof MTS.rooms[1].name, "Secret");
  snprintf(MTS.rooms[1].normalized, sizeof MTS.rooms[1].normalized, "secret");
  CHECK(mts_store_save_room(&MTS, &MTS.rooms[1]));
  CHECK(!mts_join(&b, 2));
  CHECK(mts_store_invitation(&MTS, 2, 2, 1, 1));
  CHECK(mts_join(&b, 2));
  MTS.rooms[2] = (mts_room_t){.id = 3, .owner = 1, .active = 1};
  snprintf(MTS.rooms[2].name, sizeof MTS.rooms[2].name, "Temporary");
  snprintf(MTS.rooms[2].normalized, sizeof MTS.rooms[2].normalized,
           "temporary");
  CHECK(mts_store_save_room(&MTS, &MTS.rooms[2]));
  CHECK(mts_join(&a, 3));
  CHECK(mts_join(&a, 1));
  CHECK(!MTS.rooms[2].active);
  publish_job_t job = {&a, 100};
  pthread_t threads[4];
  for (int i = 0; i < 4; i++)
    CHECK(!pthread_create(&threads[i], NULL, publish_many, &job));
  for (int i = 0; i < 4; i++)
    pthread_join(threads[i], NULL);
  mts_event_t many[MTS_INBOX];
  size_t many_count = mts_drain(&a, many, MTS_INBOX, &skipped);
  CHECK(many_count == MTS_INBOX && skipped > 0);
  for (size_t i = 1; i < many_count; i++)
    CHECK(many[i].sequence > many[i - 1].sequence);
  uint64_t visit = mts_store_visit_start(&MTS, 1, "Alice");
  CHECK(visit > 0);
  mts_store_visit_end(&MTS, visit);
  mts_session_t prefs = {.user_id = 99};
  prefs.prefs = (mts_preferences_t){.timestamps = 0,
                                    .joins = 0,
                                    .actions = 0,
                                    .bell = 1,
                                    .echo = 0,
                                    .color = 0,
                                    .allow_chat_requests = 0,
                                    .directory_visible = 0,
                                    .last_seen_visible = 0,
                                    .profile_visible = 0,
                                    .default_room = 2};
  snprintf(prefs.prefs.theme, sizeof prefs.prefs.theme, "amber");
  snprintf(prefs.prefs.greeting, sizeof prefs.prefs.greeting, "Ahoy");
  snprintf(prefs.prefs.farewell, sizeof prefs.prefs.farewell, "Fair winds");
  mts_store_save_preferences(&MTS, &prefs);
  mts_session_t loaded = {.user_id = 99};
  mts_store_load_preferences(&MTS, &loaded);
  CHECK(prefs.prefs.timestamps == loaded.prefs.timestamps);
  CHECK(prefs.prefs.joins == loaded.prefs.joins);
  CHECK(prefs.prefs.actions == loaded.prefs.actions);
  CHECK(prefs.prefs.bell == loaded.prefs.bell);
  CHECK(prefs.prefs.echo == loaded.prefs.echo);
  CHECK(prefs.prefs.color == loaded.prefs.color);
  CHECK(prefs.prefs.allow_chat_requests == loaded.prefs.allow_chat_requests);
  CHECK(prefs.prefs.directory_visible == loaded.prefs.directory_visible);
  CHECK(prefs.prefs.last_seen_visible == loaded.prefs.last_seen_visible);
  CHECK(prefs.prefs.profile_visible == loaded.prefs.profile_visible);
  CHECK(prefs.prefs.default_room == loaded.prefs.default_room);
  CHECK(!strcmp(prefs.prefs.theme, loaded.prefs.theme));
  CHECK(!strcmp(prefs.prefs.greeting, loaded.prefs.greeting));
  CHECK(!strcmp(prefs.prefs.farewell, loaded.prefs.farewell));
  const int widths[] = {20, 40, 80, 132};
  for (int ansi = 0; ansi < 2; ansi++)
    for (size_t w = 0; w < sizeof widths / sizeof widths[0]; w++)
      for (int type = MTS_PUBLIC; type <= MTS_AFK_RESPONSE; type++) {
        mts_session_t viewer = {.ansi = ansi, .term_cols = widths[w]};
        viewer.prefs.color = 1;
        snprintf(viewer.prefs.theme, sizeof viewer.prefs.theme, "ocean");
        mts_event_t fixture = {.type = (mts_event_type_t)type,
                               .room_id = 1,
                               .timestamp = 1700000000};
        snprintf(fixture.sender, sizeof fixture.sender, "T\303\251ster");
        snprintf(fixture.target, sizeof fixture.target, "Target");
        snprintf(fixture.text, sizeof fixture.text,
                 "wide \347\225\214 combining e\314\201 safe");
        char rendered[1400];
        mts_render_event(&viewer, &fixture, rendered, sizeof rendered);
        CHECK(mts_display_width(rendered + 2) <= (size_t)widths[w] + 2);
        CHECK(ansi || !strchr(rendered, '\033'));
      }
  pthread_mutex_lock(&MTS.mu);
  MTS.session_count = 0;
  pthread_mutex_unlock(&MTS.mu);
  pthread_mutex_destroy(&a.inbox_mu);
  pthread_mutex_destroy(&b.inbox_mu);
  mts_state_shutdown();
  CHECK(mts_state_init(dir, &cfg, error, sizeof error));
  CHECK(mts_find_room("Secret") != NULL);
  mts_state_shutdown();
  const bbs_io_t fio = {fw, fp, fline, fcls};
  bbs_host_api_t host = {0};
  host.abi_version = BBS_PLUGIN_ABI_VERSION_1_0;
  host.size = offsetof(bbs_host_api_t, session_restriction_flags);
  host.magic = BBS_PLUGIN_MAGIC;
  host.log = flog;
  host.io = &fio;
  host.session_username = fname;
  host.session_user_id = fid;
  host.session_user_flags = fflags;
  host.plugin_data_dir = fdir;
  bbs_plugin_desc_t desc = {0};
  CHECK(bbs_plugin_query(BBS_PLUGIN_ABI_VERSION_1_0, &host, &desc) == BBS_OK);
  CHECK(!strcmp(desc.name, "Mutineer Teleconference System") &&
        !strcmp(desc.version, "2.0.0"));
  CHECK(bbs_plugin_query(0x00020000u, &host, &desc) == BBS_EUNSUPPORTED);
  const size_t compatible_sizes[] = {
      offsetof(bbs_host_api_t, session_restriction_flags),
      offsetof(bbs_host_api_t, session_node_number),
      offsetof(bbs_host_api_t, session_security_level),
      offsetof(bbs_host_api_t, session_has_ansi),
      offsetof(bbs_host_api_t, session_is_alive),
      offsetof(bbs_host_api_t, readline_timed),
      offsetof(bbs_host_api_t, audit),
      offsetof(bbs_host_api_t, plugin_config_get),
      offsetof(bbs_host_api_t, session_terminal_size),
      offsetof(bbs_host_api_t, user_resolve),
      offsetof(bbs_host_api_t, user_lookup_id),
      sizeof host};
  for (size_t i = 0; i < sizeof compatible_sizes / sizeof compatible_sizes[0];
       i++) {
    host.size = compatible_sizes[i];
    CHECK(bbs_plugin_query(i < 8 ? BBS_PLUGIN_ABI_VERSION_1_1
                                 : BBS_PLUGIN_ABI_VERSION,
                           &host, &desc) == BBS_OK);
  }
  host.abi_version = BBS_PLUGIN_ABI_VERSION;
  host.size = sizeof host;
  host.session_restriction_flags = fflags;
  host.session_node_number = fnode;
  host.session_has_ansi = fyes;
  host.session_is_alive = fyes;
  host.readline_timed = fr;
  setenv("MTS_FAKE_DIR", dir, 1);
  CHECK(desc.init(&host) == BBS_OK);
  for (size_t i = 0; i < command_count; i++)
    CHECK(commands[i].handler != NULL);
  const char *script[] = {"/create TestRoom",
                          "/rename RenamedRoom",
                          "/settings reset",
                          "/watch all",
                          "/quit",
                          NULL};
  fake_session_t fs = {.lines = script};
  void *inst = NULL;
  const bbs_plugin_instance_vtbl_t *vt = NULL;
  CHECK(desc.create_instance((bbs_session_t *)&fs, &inst, &vt) == BBS_OK);
  CHECK(vt->on_enter(inst, (bbs_session_t *)&fs) == BBS_OK);
  CHECK(vt->run(inst, (bbs_session_t *)&fs) == BBS_OK);
  CHECK(strstr(fs.output, "Room created") &&
        strstr(fs.output, "Room renamed") &&
        strstr(fs.output, "Settings reset") &&
        strstr(fs.output, "Watching all public rooms"));
  const char *pager_script[] = {"q", NULL};
  fake_session_t pager_io = {.lines = pager_script};
  mts_session_t pager_session = {.host_session = (bbs_session_t *)&pager_io,
                                 .term_cols = 20,
                                 .term_rows = 8};
  mts_pager_t pager;
  mts_pager_begin(&pager, &pager_session, "Heading\r\n");
  for (int i = 0; i < 6; i++)
    mts_pager_line(&pager, "A long UTF-8 row: \347\225\214 end");
  CHECK(pager.stopped && strstr(pager_io.output, "-- More --"));
  bbs_event_t forced = {.type = BBS_EVT_FORCED_DISCONNECT,
                        .session = (bbs_session_t *)&fs};
  vt->on_event(inst, &forced);
  CHECK(MTS.session_count == 0 && ((mts_session_t *)inst)->cleanup_complete);
  vt->on_exit(inst, (bbs_session_t *)&fs);
  vt->on_exit(inst, (bbs_session_t *)&fs);
  vt->destroy(inst);

  host.session_restriction_flags = frestricted;
  fake_session_t denied = {0};
  CHECK(desc.create_instance((bbs_session_t *)&denied, &inst, &vt) == BBS_OK);
  CHECK(vt->on_enter(inst, (bbs_session_t *)&denied) == BBS_EPERM);
  CHECK(MTS.session_count == 0);
  vt->destroy(inst);
  host.session_restriction_flags = fflags;

  const char *eio_script[] = {NULL};
  fake_session_t eio = {.lines = eio_script};
  CHECK(desc.create_instance((bbs_session_t *)&eio, &inst, &vt) == BBS_OK);
  CHECK(vt->on_enter(inst, (bbs_session_t *)&eio) == BBS_OK);
  CHECK(vt->run(inst, (bbs_session_t *)&eio) == BBS_EIO);
  CHECK(MTS.session_count == 0 && ((mts_session_t *)inst)->cleanup_complete);
  vt->on_exit(inst, (bbs_session_t *)&eio);
  vt->destroy(inst);

  fake_session_t active = {.lines = eio_script};
  CHECK(desc.create_instance((bbs_session_t *)&active, &inst, &vt) == BBS_OK);
  CHECK(vt->on_enter(inst, (bbs_session_t *)&active) == BBS_OK);
  bbs_event_t stopping = {.type = BBS_EVT_SHUTDOWN};
  desc.on_event(&stopping);
  CHECK(MTS.shutting_down && MTS.session_count == 0 &&
        ((mts_session_t *)inst)->cleanup_complete && MTS.db != NULL);
  desc.shutdown();
  CHECK(MTS.db == NULL);
  vt->on_exit(inst, (bbs_session_t *)&active);
  vt->destroy(inst);
  unsetenv("MTS_FAKE_DIR");
  char bad_dir[] = "/tmp/mts-bad-XXXXXX";
  CHECK(mkdtemp(bad_dir) != NULL);
  char bad_path[768];
  snprintf(bad_path, sizeof bad_path, "%s/mts.db", bad_dir);
  FILE *bad = fopen(bad_path, "wb");
  CHECK(bad != NULL);
  fputs("not a sqlite database", bad);
  fclose(bad);
  mts_state_t bad_state = {0};
  CHECK(!mts_store_open(&bad_state, bad_dir, error, sizeof error));
  if (bad_state.db)
    sqlite3_close(bad_state.db);
  unlink(bad_path);
  rmdir(bad_dir);
  char rollback_dir[] = "/tmp/mts-rollback-XXXXXX";
  CHECK(mkdtemp(rollback_dir) != NULL);
  char rollback_path[768];
  snprintf(rollback_path, sizeof rollback_path, "%s/mts.db", rollback_dir);
  sqlite3 *rollback_db = NULL;
  CHECK(sqlite3_open(rollback_path, &rollback_db) == SQLITE_OK);
  CHECK(
      sqlite3_exec(rollback_db,
                   "CREATE TABLE mts_schema_version(version INTEGER PRIMARY "
                   "KEY,applied_at INTEGER);INSERT INTO mts_schema_version "
                   "VALUES(1,0);CREATE TABLE mts_room_bans(room_id "
                   "INTEGER,user_id INTEGER,actor_user_id INTEGER,reason "
                   "TEXT,expires_at INTEGER,created_at INTEGER);INSERT INTO "
                   "mts_room_bans VALUES(1,2,1,'a',NULL,0),(1,2,1,'b',NULL,0);",
                   NULL, NULL, NULL) == SQLITE_OK);
  sqlite3_close(rollback_db);
  mts_state_t rollback_state = {0};
  CHECK(!mts_store_open(&rollback_state, rollback_dir, error, sizeof error));
  if (rollback_state.db)
    sqlite3_close(rollback_state.db);
  CHECK(sqlite3_open(rollback_path, &rollback_db) == SQLITE_OK);
  sqlite3_stmt *rollback_version = NULL;
  CHECK(sqlite3_prepare_v2(rollback_db,
                           "SELECT MAX(version) FROM mts_schema_version", -1,
                           &rollback_version, NULL) == SQLITE_OK);
  CHECK(sqlite3_step(rollback_version) == SQLITE_ROW &&
        sqlite3_column_int(rollback_version, 0) == 1);
  sqlite3_finalize(rollback_version);
  sqlite3_close(rollback_db);
  unlink(rollback_path);
  rmdir(rollback_dir);
  char path[768];
  snprintf(path, sizeof path, "%s/mts.db", dir);
  unlink(path);
  snprintf(path, sizeof path, "%s/mts.db-wal", dir);
  unlink(path);
  snprintf(path, sizeof path, "%s/mts.db-shm", dir);
  unlink(path);
  rmdir(dir);
  puts("MTS tests passed");
  return 0;
}
