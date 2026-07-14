#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bbs_acs.h"
#include "bbs_config.h"
#include "bbs_db.h"
#include "bbs_process.h"
#include "bbs_scheduler.h"
#include "bbs_session.h"

#define CHECK(cond, msg) do { \
  if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while (0)

volatile sig_atomic_t g_stop = 1;
static int broadcast_count = 0;

void online_broadcast(const char* msg) {
  (void)msg;
  broadcast_count++;
}

static int file_exists(const char* path) {
  return access(path, F_OK) == 0;
}

static const char* touch_bin(void) {
  if (access("/usr/bin/touch", X_OK) == 0) return "/usr/bin/touch";
  return "/bin/touch";
}

static void cleanup_file(const char* path) {
  if (path && path[0]) unlink(path);
}

static int test_argv_parser_rejects_shell(void) {
  const char* rejected[] = {
    "/bin/echo ok; /bin/echo bad",
    "/bin/echo ok | /bin/cat",
    "/bin/echo ok && /bin/echo bad",
    "/bin/echo ok || /bin/echo bad",
    "/bin/echo $(id)",
    "/bin/echo ok > /tmp/out"
  };
  for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++) {
    char err[128] = {0};
    char** argv = NULL;
    CHECK(!bbs_argv_parse_template(rejected[i], NULL, &argv, err, sizeof(err)),
          "argv parser rejects shell metacharacters");
    bbs_argv_free(argv);
  }

  char err[128] = {0};
  char** argv = NULL;
  CHECK(bbs_argv_parse_template("/bin/echo \"two words\" plain", NULL, &argv, err, sizeof(err)),
        "argv parser accepts quoted arguments");
  CHECK(argv && argv[0] && argv[1] && strcmp(argv[1], "two words") == 0,
        "argv parser preserves quoted argument as one argv entry");
  bbs_argv_free(argv);
  return 0;
}

static int test_permission_and_logon_events(void) {
  char temp_template[256];
  snprintf(temp_template, sizeof(temp_template), "%s/mutineer-sched-XXXXXX",
           getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
  char* dir = mkdtemp(temp_template);
  CHECK(dir != NULL, "temporary scheduler directory creates");

  char allowed[256], denied[256], logon[256], rejected[256];
  snprintf(allowed, sizeof(allowed), "%s/allowed", dir);
  snprintf(denied, sizeof(denied), "%s/denied", dir);
  snprintf(logon, sizeof(logon), "%s/logon", dir);
  snprintf(rejected, sizeof(rejected), "%s/rejected", dir);

  char cmd_allowed[512], cmd_denied[512], cmd_logon[512], cmd_rejected[512];
  snprintf(cmd_allowed, sizeof(cmd_allowed), "%s %s", touch_bin(), allowed);
  snprintf(cmd_denied, sizeof(cmd_denied), "%s %s", touch_bin(), denied);
  snprintf(cmd_logon, sizeof(cmd_logon), "%s %s", touch_bin(), logon);
  snprintf(cmd_rejected, sizeof(cmd_rejected), "%s %s; %s %s",
           touch_bin(), rejected, touch_bin(), denied);

  BbsDb* db = db_open(":memory:");
  CHECK(db != NULL, "scheduler test DB opens");
  CHECK(db_init_schema(db, "sql/schema.sql"), "scheduler test schema initializes");
  CHECK(db_event_add(db, "allowed", "daily@00:00", cmd_allowed, "permission", "SL10"),
        "allowed permission event inserts");
  CHECK(db_event_add(db, "denied", "daily@00:00", cmd_denied, "permission", "SL90"),
        "denied permission event inserts");
  CHECK(db_event_add(db, "logon", "daily@00:00", cmd_logon, "logon", ""),
        "logon event inserts");
  CHECK(db_event_add(db, "rejected", "daily@00:00", cmd_rejected, "logon", ""),
        "rejected shell-metachar event inserts");

  Session s;
  memset(&s, 0, sizeof(s));
  s.db = db;
  s.user.id = 123;
  s.user.level = 50;
  snprintf(s.user.handle, sizeof(s.user.handle), "Tester");

  CHECK(acs_allows(&s, "SL10"), "low ACS allows test user");
  CHECK(!acs_allows(&s, "SL90"), "high ACS denies test user");

  scheduler_run_logon_events(&s);

  CHECK(file_exists(allowed), "authorized permission event runs");
  CHECK(!file_exists(denied), "unauthorized permission event does not run");
  CHECK(file_exists(logon), "logon event runs");
  CHECK(!file_exists(rejected), "shell-metachar event does not run");

  cleanup_file(allowed);
  cleanup_file(denied);
  cleanup_file(logon);
  cleanup_file(rejected);
  rmdir(dir);
  db_close(db);
  return 0;
}

static int test_warning_once_per_window(void) {
  BbsDb* db = db_open(":memory:");
  CHECK(db != NULL, "warning test DB opens");
  CHECK(db_init_schema(db, "sql/schema.sql"), "warning test schema initializes");
  CHECK(db_event_add(db, "warn", "daily@00:00", "/bin/true", "scheduled", ""),
        "warning event inserts");
  CHECK(db_exec(db, "UPDATE events SET warning_min = 1, next_run = datetime('now', '+30 seconds') WHERE name = 'warn'"),
        "warning event window updates");

  BbsConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.scheduler_enabled = 1;
  cfg.scheduler_tick_sec = 1;
  broadcast_count = 0;
  g_stop = 0;
  scheduler_start(&cfg, db);
  sleep(3);
  g_stop = 1;
  sleep(1);
  CHECK(broadcast_count == 1, "scheduler warns once per event window");
  db_close(db);
  return 0;
}

int main(void) {
  CHECK(test_argv_parser_rejects_shell() == 0, "argv parser shell rejection test passed");
  CHECK(test_permission_and_logon_events() == 0, "permission/logon event test passed");
  CHECK(test_warning_once_per_window() == 0, "warning de-duplication test passed");
  return 0;
}
