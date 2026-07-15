#include "bbslib.h"
#include "bbs_hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT_TRUE(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while (0)

typedef struct Capture
{
  char output[8192];
  size_t output_len;
  const char *input[8];
  int input_pos;
} Capture;

static int cap_write(void *user_data, const uint8_t *data, size_t len)
{
  Capture *c = (Capture *)user_data;
  if (len > sizeof(c->output) - c->output_len - 1)
    len = sizeof(c->output) - c->output_len - 1;
  memcpy(c->output + c->output_len, data, len);
  c->output_len += len;
  c->output[c->output_len] = '\0';
  return (int)len;
}

static int cap_readline(void *user_data, uint8_t *buf, size_t cap, int timeout_sec, int echo)
{
  (void)timeout_sec;
  (void)echo;
  Capture *c = (Capture *)user_data;
  const char *line = c->input[c->input_pos];
  if (!line)
    return -2;
  c->input_pos++;
  size_t n = strlen(line);
  if (n >= cap) n = cap - 1;
  memcpy(buf, line, n);
  buf[n] = '\0';
  return (int)n;
}

static int write_file(const char *path, const char *body)
{
  FILE *f = fopen(path, "w");
  if (!f)
    return 0;
  fputs(body, f);
  fclose(f);
  return 1;
}

int main(void)
{
  char tmpdir[256];
  snprintf(tmpdir, sizeof(tmpdir), "%s/bbslib_test_%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", tmpdir);
  ASSERT_TRUE(system(cmd) == 0, "failed to create temp dir");

  char db_path[512], cfg_path[512], backup_path[512];
  snprintf(db_path, sizeof(db_path), "%s/test.db", tmpdir);
  snprintf(cfg_path, sizeof(cfg_path), "%s/test.conf", tmpdir);
  snprintf(backup_path, sizeof(backup_path), "%s/backup.db", tmpdir);

  BbsDb *db = db_open(db_path);
  ASSERT_TRUE(db != NULL, "db_open failed");
  ASSERT_TRUE(db_init_schema(db, "sql/schema.sql"), "schema init failed");
  ASSERT_TRUE(db_seed_defaults(db, "seed_hash"), "seed defaults failed");
  char hash[128];
  ASSERT_TRUE(pw_hash_make("secret", hash, sizeof(hash)), "password hash failed");
  ASSERT_TRUE(db_user_create(db, "sdkuser", hash, 1), "user create failed");
  ASSERT_TRUE(db_msg_area_seed(db, "SDK Messages"), "message area seed failed");
  ASSERT_TRUE(db_file_area_seed(db, "SDK Files", tmpdir), "file area seed failed");
  db_close(db);

  char cfg[2048];
  snprintf(cfg, sizeof(cfg),
           "bbs_name=SDK Test BBS\n"
           "db_path=%s\n"
           "data_path=%s\n"
           "menus_path=menus\n"
           "art_path=art\n"
           "files_path=%s\n"
           "idle_timeout_sec=1\n",
           db_path, tmpdir, tmpdir);
  ASSERT_TRUE(write_file(cfg_path, cfg), "config write failed");

  BbsLibContext *ctx = NULL;
  ASSERT_TRUE(bbslib_open_path(cfg_path, &ctx) == BBSLIB_OK, "bbslib_open_path failed");

  BbsLibMetrics metrics;
  ASSERT_TRUE(bbslib_metrics_get(ctx, &metrics) == BBSLIB_OK, "metrics failed");
  ASSERT_TRUE(metrics.users >= 1, "expected users");
  ASSERT_TRUE(metrics.message_areas >= 1, "expected message areas");
  ASSERT_TRUE(metrics.file_areas >= 1, "expected file areas");

  char json[4096];
  ASSERT_TRUE(bbslib_status_json(ctx, json, sizeof(json)) == BBSLIB_OK, "status json failed");
  ASSERT_TRUE(strstr(json, "SDK Test BBS") != NULL, "status json missing bbs name");

  DbUser user;
  bool upgrade = false;
  ASSERT_TRUE(bbslib_authenticate_user(ctx, "sdkuser", "secret", &user, &upgrade) == BBSLIB_OK, "auth failed");
  ASSERT_TRUE(!strcmp(user.handle, "sdkuser"), "wrong authenticated user");

  DbMsgArea msg_areas[16];
  ASSERT_TRUE(bbslib_msg_area_list(ctx, msg_areas, 16) > 0, "message area list failed");
  ASSERT_TRUE(bbslib_message_post(ctx, msg_areas[0].id, user.id, "SDK Subject", "SDK Body", 0) == BBSLIB_OK, "message post failed");

  DbFileArea file_areas[16];
  ASSERT_TRUE(bbslib_file_area_list(ctx, file_areas, 16) > 0, "file area list failed");

  ASSERT_TRUE(bbslib_maintenance_integrity(ctx) == BBSLIB_OK, "integrity failed");
  ASSERT_TRUE(bbslib_maintenance_backup(ctx, backup_path) == BBSLIB_OK, "backup failed");
  ASSERT_TRUE(access(backup_path, F_OK) == 0, "backup file missing");

  Capture cap;
  memset(&cap, 0, sizeof(cap));
  BbsLibSessionAdapter adapter = {.write = cap_write, .readline = cap_readline, .user_data = &cap};
  BbsLibSessionOptions session_opts = {.handle = "sdkuser", .ip = "test"};
  BbsLibSession *session = NULL;
  ASSERT_TRUE(bbslib_session_open(ctx, &session_opts, &adapter, &session) == BBSLIB_OK, "bridge session open failed");
  ASSERT_TRUE(bbslib_session_run_action(session, "who") == BBSLIB_OK, "bridge who action failed");
  ASSERT_TRUE(cap.output_len > 0, "bridge output missing who text");
  ASSERT_TRUE(bbslib_session_run_action(session, "maintenance") == BBSLIB_ERR_DENIED, "admin action should be denied");
  bbslib_session_close(session);

  bbslib_close(ctx);
  snprintf(cmd, sizeof(cmd), "rm -rf '%s'", tmpdir);
  (void)system(cmd);
  return 0;
}
