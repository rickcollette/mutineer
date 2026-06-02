#include "bbs_db.h"
#include "bbs_msg_defs.h"
#include "bbs_util.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <openssl/sha.h>

#ifndef HAVE_SQLITE
#error "Mutineer requires SQLite support. Install SQLite3 development headers/libraries and rebuild."
#endif

#include <sqlite3.h>

struct BbsDb
{
  sqlite3 *db;
  char last_err[256];
};

static char g_last_open_error[256];

static void set_err(BbsDb *db, const char *msg)
{
  if (!db)
    return;
  snprintf(db->last_err, sizeof(db->last_err), "%s", msg ? msg : "unknown");
}

const char *db_last_error(BbsDb *db)
{
  static const char *none = "no db";
  if (!db)
    return g_last_open_error[0] ? g_last_open_error : none;
  return db->last_err[0] ? db->last_err : "ok";
}

BbsDb *db_open(const char *path)
{
  g_last_open_error[0] = '\0';
  if (!path || !path[0])
  {
    snprintf(g_last_open_error, sizeof(g_last_open_error), "database path is empty");
    return NULL;
  }

  BbsDb *db = (BbsDb *)calloc(1, sizeof(BbsDb));
  if (!db)
  {
    snprintf(g_last_open_error, sizeof(g_last_open_error), "out of memory");
    return NULL;
  }

  int rc = sqlite3_open(path, &db->db);
  if (rc != SQLITE_OK)
  {
    snprintf(g_last_open_error, sizeof(g_last_open_error), "%s",
             db->db ? sqlite3_errmsg(db->db) : "sqlite open failed");
    sqlite3_close(db->db);
    free(db);
    return NULL;
  }

  char *err = NULL;
  if (sqlite3_exec(db->db, "PRAGMA foreign_keys = ON; PRAGMA busy_timeout = 5000;", NULL, NULL, &err) != SQLITE_OK)
  {
    snprintf(g_last_open_error, sizeof(g_last_open_error), "%s", err ? err : "failed to configure sqlite");
    sqlite3_free(err);
    sqlite3_close(db->db);
    free(db);
    return NULL;
  }

  return db;
}

void db_close(BbsDb *db)
{
  if (!db)
    return;
  if (db->db)
    sqlite3_close(db->db);
  free(db);
}

bool db_exec(BbsDb *db, const char *sql)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  char *err = NULL;
  int rc = sqlite3_exec(db->db, sql, NULL, NULL, &err);
  if (rc != SQLITE_OK)
  {
    set_err(db, err ? err : "sqlite exec failed");
    sqlite3_free(err);
    return false;
  }
  return true;
#else
  (void)sql;
  return false;
#endif
}

int db_exec_simple(BbsDb *db, const char *sql)
{
  if (!db)
    return -1;
#ifdef HAVE_SQLITE
  char *err = NULL;
  int rc = sqlite3_exec(db->db, sql, NULL, NULL, &err);
  if (rc != SQLITE_OK)
  {
    set_err(db, err ? err : "sqlite exec failed");
    sqlite3_free(err);
    return -1;
  }
  return sqlite3_changes(db->db);
#else
  (void)sql;
  return 0;
#endif
}

bool db_query(BbsDb *db, const char *sql, bool (*row_cb)(void *row, void *ctx), void *ctx)
{
  if (!db || !row_cb)
    return false;
#ifdef HAVE_SQLITE
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }

  int rc;
  bool keep_going = true;
  while ((rc = sqlite3_step(st)) == SQLITE_ROW)
  {
    keep_going = row_cb((void *)st, ctx);
    if (!keep_going)
      break;
  }

  if (rc != SQLITE_DONE && rc != SQLITE_ROW && rc != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    sqlite3_finalize(st);
    return false;
  }

  sqlite3_finalize(st);
  return true;
#else
  (void)sql;
  (void)row_cb;
  (void)ctx;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_query_int(BbsDb *db, const char *sql, int default_val)
{
  if (!db)
    return default_val;
#ifdef HAVE_SQLITE
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return default_val;
  }

  int result = default_val;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    result = sqlite3_column_int(st, 0);
  }
  sqlite3_finalize(st);
  return result;
#else
  (void)sql;
  return default_val;
#endif
}

static char *read_file(const char *path, size_t *out_len)
{
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  if (n < 0)
  {
    fclose(f);
    return NULL;
  }
  fseek(f, 0, SEEK_SET);
  char *buf = (char *)malloc((size_t)n + 1);
  if (!buf)
  {
    fclose(f);
    return NULL;
  }
  size_t got = fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[got] = 0;
  if (out_len)
    *out_len = got;
  return buf;
}

static bool db_column_exists(BbsDb *db, const char *table, const char *column)
{
  char sql[256];
  snprintf(sql, sizeof(sql), "PRAGMA table_info(%s)", table);

  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }

  bool found = false;
  while (sqlite3_step(st) == SQLITE_ROW)
  {
    const char *name = (const char *)sqlite3_column_text(st, 1);
    if (name && strcmp(name, column) == 0)
    {
      found = true;
      break;
    }
  }
  sqlite3_finalize(st);
  return found;
}

static bool db_add_column_if_missing(BbsDb *db, const char *table, const char *column, const char *definition)
{
  if (db_column_exists(db, table, column))
    return true;

  char sql[512];
  snprintf(sql, sizeof(sql), "ALTER TABLE %s ADD COLUMN %s %s", table, column, definition);
  if (!db_exec(db, sql))
    return false;
  return true;
}

static bool db_apply_core_migrations(BbsDb *db)
{
  static const struct
  {
    const char *table;
    const char *column;
    const char *definition;
  } migrations[] = {
      {"users", "real_name", "TEXT"},
      {"users", "email", "TEXT"},
      {"users", "phone", "TEXT"},
      {"users", "street", "TEXT"},
      {"users", "city_state", "TEXT"},
      {"users", "zip_code", "TEXT"},
      {"users", "caller_id", "TEXT"},
      {"users", "forgot_pw_question", "TEXT"},
      {"users", "forgot_pw_answer", "TEXT"},
      {"users", "sex", "TEXT DEFAULT 'U'"},
      {"users", "birth_date", "TEXT"},
      {"users", "dsl", "INTEGER NOT NULL DEFAULT 10"},
      {"users", "ac_flags", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "status_flags", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "credits", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "file_points", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "time_limit_min", "INTEGER"},
      {"users", "on_today", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "illegal", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "def_arc_type", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "color_scheme", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "user_start_menu", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "first_on", "TEXT"},
      {"users", "t_time_on", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "last_qwk", "TEXT"},
      {"users", "uploads", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "downloads", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "uk", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "dk", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "logged_on", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "msg_post", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "email_sent", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "feedback", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "timebank", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "timebank_add", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "dl_k_today", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "dl_today", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "usr_def_str1", "TEXT"},
      {"users", "usr_def_str2", "TEXT"},
      {"users", "usr_def_str3", "TEXT"},
      {"users", "social_link", "TEXT"},
      {"users", "sysop_msg", "TEXT"},
      {"users", "note", "TEXT"},
      {"users", "locked_file", "TEXT"},
      {"users", "last_conf", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "last_login_at", "TEXT"},
      {"users", "created_at", "TEXT NOT NULL DEFAULT ''"},
      {"users", "expires_at", "TEXT"},
      {"users", "pw_changed_at", "TEXT NOT NULL DEFAULT ''"},
      {"users", "subscription", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "alert_sysop", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "smw", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "signature", "TEXT"},
      {"users", "tagline", "TEXT"},
      {"users", "use_signature", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "use_tagline", "INTEGER NOT NULL DEFAULT 0"},
      {"nodes", "ip", "TEXT"},
      {"message_areas", "conf_id", "INTEGER"},
      {"message_areas", "filename", "TEXT"},
      {"message_areas", "acs_read", "TEXT"},
      {"message_areas", "acs_post", "TEXT"},
      {"message_areas", "acs_sysop", "TEXT"},
      {"message_areas", "acs", "TEXT"},
      {"message_areas", "anon_policy", "INTEGER NOT NULL DEFAULT 0"},
      {"message_areas", "flags", "INTEGER NOT NULL DEFAULT 0"},
      {"message_areas", "password", "TEXT"},
      {"message_areas", "origin", "TEXT"},
      {"message_areas", "max_msgs", "INTEGER NOT NULL DEFAULT 500"},
      {"messages", "to_user", "INTEGER"},
      {"messages", "reply_to", "INTEGER"},
      {"messages", "thread_root", "INTEGER"},
      {"messages", "from_name", "TEXT"},
      {"messages", "to_name", "TEXT"},
      {"messages", "attr", "INTEGER NOT NULL DEFAULT 0"},
      {"messages", "net_attr", "INTEGER NOT NULL DEFAULT 0"},
      {"messages", "file_attached", "TEXT"},
      {"messages", "origin", "TEXT"},
      {"file_areas", "acs_list", "TEXT"},
      {"file_areas", "acs_download", "TEXT"},
      {"file_areas", "acs_upload", "TEXT"},
      {"file_areas", "acs_sysop", "TEXT"},
      {"file_areas", "password", "TEXT"},
      {"file_areas", "max_files", "INTEGER NOT NULL DEFAULT 0"},
      {"file_areas", "archive_type", "TEXT"},
      {"file_areas", "sort_type", "INTEGER NOT NULL DEFAULT 0"},
      {"file_areas", "show_uploader", "INTEGER NOT NULL DEFAULT 1"},
      {"file_areas", "check_dupes", "INTEGER NOT NULL DEFAULT 1"},
      {"file_areas", "free_files", "INTEGER NOT NULL DEFAULT 0"},
      {"file_areas", "flags", "INTEGER NOT NULL DEFAULT 0"},
      {"files", "extended_desc", "TEXT"},
      {"files", "file_id_diz", "TEXT"},
      {"files", "sha256", "TEXT"},
      {"files", "file_points", "INTEGER NOT NULL DEFAULT 0"},
      {"files", "download_count", "INTEGER NOT NULL DEFAULT 0"},
      {"files", "owner_credit", "INTEGER NOT NULL DEFAULT 0"},
      {"files", "flags", "INTEGER NOT NULL DEFAULT 0"},
      {"doors", "runner",      "TEXT NOT NULL DEFAULT 'native'"},
      {"doors", "manifest",    "TEXT NOT NULL DEFAULT ''"},
      {"doors", "enabled",     "INTEGER NOT NULL DEFAULT 1"},
      {"doors", "timeout_sec", "INTEGER NOT NULL DEFAULT 0"},
      {"users", "use_fse",     "INTEGER NOT NULL DEFAULT 0"},
  };

  for (size_t i = 0; i < sizeof(migrations) / sizeof(migrations[0]); i++)
  {
    if (!db_add_column_if_missing(db, migrations[i].table, migrations[i].column, migrations[i].definition))
      return false;
  }

  /* New tables for existing installs — idempotent */
  if (!db_exec(db,
        "CREATE TABLE IF NOT EXISTS user_msg_scan_areas ("
        "  user_id INTEGER NOT NULL, area_id INTEGER NOT NULL,"
        "  scan_enabled INTEGER NOT NULL DEFAULT 1,"
        "  PRIMARY KEY (user_id, area_id))"))
    return false;

  if (!db_exec(db,
        "CREATE TABLE IF NOT EXISTS drafts ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  user_id INTEGER NOT NULL, area_id INTEGER NOT NULL DEFAULT 0,"
        "  to_user_id INTEGER NOT NULL DEFAULT 0, to_name TEXT NOT NULL DEFAULT '',"
        "  subject TEXT NOT NULL DEFAULT '', body TEXT NOT NULL DEFAULT '',"
        "  created_at TEXT NOT NULL DEFAULT (datetime('now')))"))
    return false;

  if (!db_exec(db,
        "CREATE INDEX IF NOT EXISTS idx_drafts_user ON drafts(user_id)"))
    return false;

  return db_exec(db, "INSERT OR REPLACE INTO meta (k, v) VALUES ('schema_version', '1')");
}

bool db_init_schema(BbsDb *db, const char *schema_path)
{
  if (!db || !schema_path || !schema_path[0])
  {
    if (db)
      set_err(db, "invalid schema path");
    return false;
  }

  size_t n = 0;
  char *sql = read_file(schema_path, &n);
  if (!sql)
  {
    char err[512];
    snprintf(err, sizeof(err), "failed to read schema file '%s': %s",
             schema_path, strerror(errno));
    set_err(db, err);
    return false;
  }

  bool ok = db_exec(db, "BEGIN IMMEDIATE");
  if (ok)
    ok = db_exec(db, sql);
  if (ok)
    ok = db_apply_core_migrations(db);

  if (ok)
  {
    ok = db_exec(db, "COMMIT");
  }
  else
  {
    char err[256];
    snprintf(err, sizeof(err), "%s", db_last_error(db));
    db_exec(db, "ROLLBACK");
    set_err(db, err);
  }

  free(sql);
  return ok;
}

static void safe_copy(char *dst, size_t cap, const char *src)
{
  if (!dst || cap == 0)
    return;
  if (!src)
  {
    dst[0] = 0;
    return;
  }
  snprintf(dst, cap, "%s", src);
}

bool db_user_fetch(BbsDb *db, const char *handle, DbUser *out)
{
  if (!db || !handle || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql =
      "SELECT u.id, u.handle, COALESCE(u.real_name,''), u.pw_hash, COALESCE(u.email,''), "
      "COALESCE(u.phone,''), COALESCE(u.street,''), COALESCE(u.city_state,''), COALESCE(u.zip_code,''), "
      "COALESCE(u.caller_id,''), COALESCE(u.forgot_pw_answer,''), COALESCE(u.sex,'U'), "
      "COALESCE(u.birth_date,''), u.security_level_id, sl.level, COALESCE(u.dsl,10), "
      "COALESCE(sl.time_limit_min,60), u.flags, COALESCE(u.ac_flags,0), u.status_flags, "
      "u.credits, u.file_points, COALESCE(u.on_today,0), COALESCE(u.illegal,0), "
      "COALESCE(u.def_arc_type,0), COALESCE(u.color_scheme,0), COALESCE(u.user_start_menu,0), "
      "COALESCE(u.first_on,''), COALESCE(u.t_time_on,0), COALESCE(u.last_qwk,''), "
      "COALESCE(u.uploads,0), COALESCE(u.downloads,0), COALESCE(u.uk,0), COALESCE(u.dk,0), "
      "COALESCE(u.logged_on,0), COALESCE(u.msg_post,0), COALESCE(u.email_sent,0), "
      "COALESCE(u.feedback,0), COALESCE(u.timebank,0), COALESCE(u.timebank_add,0), "
      "COALESCE(u.dl_k_today,0), COALESCE(u.dl_today,0), COALESCE(u.usr_def_str1,''), "
      "COALESCE(u.usr_def_str2,''), COALESCE(u.usr_def_str3,''), COALESCE(u.note,''), "
      "COALESCE(u.locked_file,''), COALESCE(u.last_login_at,''), COALESCE(u.expires_at,''), "
      "u.smw, sl.download_ratio_num, sl.download_ratio_den, sl.post_ratio_num, sl.post_ratio_den, "
      "COALESCE(u.signature,''), COALESCE(u.tagline,''), "
      "COALESCE(u.use_signature,0), COALESCE(u.use_tagline,0), COALESCE(u.use_fse,0) "
      "FROM users u LEFT JOIN security_levels sl ON sl.id = u.security_level_id "
      "WHERE u.handle = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, handle, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  if (rc == SQLITE_ROW)
  {
    memset(out, 0, sizeof(*out));
    int col = 0;
    out->id = sqlite3_column_int(st, col++);
    safe_copy(out->handle, sizeof(out->handle), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->real_name, sizeof(out->real_name), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->pw_hash, sizeof(out->pw_hash), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->email, sizeof(out->email), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->phone, sizeof(out->phone), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->street, sizeof(out->street), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->city_state, sizeof(out->city_state), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->zip_code, sizeof(out->zip_code), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->caller_id, sizeof(out->caller_id), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->forgot_pw_answer, sizeof(out->forgot_pw_answer), (const char *)sqlite3_column_text(st, col++));
    const char *sex_str = (const char *)sqlite3_column_text(st, col++);
    out->sex = (sex_str && sex_str[0]) ? sex_str[0] : 'U';
    safe_copy(out->birth_date, sizeof(out->birth_date), (const char *)sqlite3_column_text(st, col++));
    out->security_level_id = sqlite3_column_int(st, col++);
    out->level = sqlite3_column_int(st, col++);
    out->dsl = sqlite3_column_int(st, col++);
    out->time_limit_min = sqlite3_column_int(st, col++);
    out->flags = (unsigned)sqlite3_column_int(st, col++);
    out->ac_flags = (unsigned)sqlite3_column_int(st, col++);
    out->status_flags = (unsigned)sqlite3_column_int(st, col++);
    out->credits = sqlite3_column_int(st, col++);
    out->file_points = sqlite3_column_int(st, col++);
    out->on_today = sqlite3_column_int(st, col++);
    out->illegal = sqlite3_column_int(st, col++);
    out->def_arc_type = sqlite3_column_int(st, col++);
    out->color_scheme = sqlite3_column_int(st, col++);
    out->user_start_menu = sqlite3_column_int(st, col++);
    safe_copy(out->first_on, sizeof(out->first_on), (const char *)sqlite3_column_text(st, col++));
    out->t_time_on = sqlite3_column_int(st, col++);
    safe_copy(out->last_qwk, sizeof(out->last_qwk), (const char *)sqlite3_column_text(st, col++));
    out->uploads = sqlite3_column_int(st, col++);
    out->downloads = sqlite3_column_int(st, col++);
    out->uk = sqlite3_column_int(st, col++);
    out->dk = sqlite3_column_int(st, col++);
    out->logged_on = sqlite3_column_int(st, col++);
    out->msg_post = sqlite3_column_int(st, col++);
    out->email_sent = sqlite3_column_int(st, col++);
    out->feedback = sqlite3_column_int(st, col++);
    out->timebank = sqlite3_column_int(st, col++);
    out->timebank_add = sqlite3_column_int(st, col++);
    out->dl_k_today = sqlite3_column_int(st, col++);
    out->dl_today = sqlite3_column_int(st, col++);
    safe_copy(out->usr_def_str1, sizeof(out->usr_def_str1), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->usr_def_str2, sizeof(out->usr_def_str2), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->usr_def_str3, sizeof(out->usr_def_str3), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->note, sizeof(out->note), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->locked_file, sizeof(out->locked_file), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->last_login_at, sizeof(out->last_login_at), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->expires_at, sizeof(out->expires_at), (const char *)sqlite3_column_text(st, col++));
    out->smw = sqlite3_column_int(st, col++);
    out->dl_ratio_num = sqlite3_column_int(st, col++);
    out->dl_ratio_den = sqlite3_column_int(st, col++);
    out->post_ratio_num = sqlite3_column_int(st, col++);
    out->post_ratio_den = sqlite3_column_int(st, col++);
    safe_copy(out->signature, sizeof(out->signature), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->tagline, sizeof(out->tagline), (const char *)sqlite3_column_text(st, col++));
    out->use_signature = sqlite3_column_int(st, col++);
    out->use_tagline   = sqlite3_column_int(st, col++);
    out->use_fse       = sqlite3_column_int(st, col++);
    sqlite3_finalize(st);
    return true;
  }
  sqlite3_finalize(st);
  set_err(db, "user not found");
  return false;
#else
  (void)handle;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_user_create(BbsDb *db, const char *handle, const char *pw_hash, int security_level_id)
{
  if (!db || !handle || !pw_hash)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO users (handle, pw_hash, security_level_id, flags, first_on, logged_on) "
                    "VALUES (?1, ?2, ?3, 0, datetime('now'), 1)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, handle, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, pw_hash, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 3, security_level_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE)
  {
    set_err(db, "user create failed");
    return false;
  }
  return true;
#else
  (void)handle;
  (void)pw_hash;
  (void)security_level_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_user_create_ex(BbsDb *db, const DbUserRegInfo *info)
{
  if (!db || !info || !info->handle || !info->pw_hash)
    return false;
  if (!info->email || !info->city_state)
    return false; /* required fields */
#ifdef HAVE_SQLITE
  const char *sql =
      "INSERT INTO users (handle, pw_hash, security_level_id, email, city_state, "
      "social_link, sysop_msg, flags, first_on, logged_on) "
      "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, 0, datetime('now'), 1)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, info->handle, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, info->pw_hash, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 3, info->security_level_id > 0 ? info->security_level_id : 1);
  sqlite3_bind_text(st, 4, info->email, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, info->city_state, -1, SQLITE_TRANSIENT);
  if (info->social_link && info->social_link[0])
  {
    sqlite3_bind_text(st, 6, info->social_link, -1, SQLITE_TRANSIENT);
  }
  else
  {
    sqlite3_bind_null(st, 6);
  }
  if (info->sysop_msg && info->sysop_msg[0])
  {
    sqlite3_bind_text(st, 7, info->sysop_msg, -1, SQLITE_TRANSIENT);
  }
  else
  {
    sqlite3_bind_null(st, 7);
  }
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE)
  {
    set_err(db, "user create failed");
    return false;
  }
  return true;
#else
  (void)info;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_user_touch_login(BbsDb *db, int user_id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE users SET last_login_at = datetime('now') WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, user_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE)
  {
    set_err(db, "touch login failed");
    return false;
  }
  return true;
#else
  (void)user_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_user_set_pw(BbsDb *db, int user_id, const char *pw_hash)
{
  if (!db || !pw_hash)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE users SET pw_hash = ?1 WHERE id = ?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, pw_hash, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, user_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)user_id;
  (void)pw_hash;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_user_clear_smw(BbsDb *db, int user_id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE users SET smw = 0 WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, user_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)user_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_user_set_security_question(BbsDb *db, int user_id, const char *question, const char *answer_hash)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE users SET forgot_pw_question = ?2, forgot_pw_answer = ?3 WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, user_id);
  sqlite3_bind_text(st, 2, question ? question : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, answer_hash ? answer_hash : "", -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)user_id;
  (void)question;
  (void)answer_hash;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_user_get_security_question(BbsDb *db, const char *handle, char *question, size_t qlen, char *answer_hash, size_t alen)
{
  if (!db || !handle || !question || !answer_hash)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT COALESCE(forgot_pw_question,''), COALESCE(forgot_pw_answer,'') FROM users WHERE handle = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, handle, -1, SQLITE_TRANSIENT);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    const char *q = (const char *)sqlite3_column_text(st, 0);
    const char *a = (const char *)sqlite3_column_text(st, 1);
    if (q && q[0] && a && a[0])
    {
      snprintf(question, qlen, "%s", q);
      snprintf(answer_hash, alen, "%s", a);
      found = true;
    }
  }
  sqlite3_finalize(st);
  return found;
#else
  (void)handle;
  (void)question;
  (void)qlen;
  (void)answer_hash;
  (void)alen;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_user_set_pw_with_timestamp(BbsDb *db, int user_id, const char *pw_hash)
{
  if (!db || !pw_hash)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE users SET pw_hash = ?2, pw_changed_at = datetime('now') WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, user_id);
  sqlite3_bind_text(st, 2, pw_hash, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)user_id;
  (void)pw_hash;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_user_pw_age_days(BbsDb *db, int user_id)
{
  if (!db)
    return -1;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT CAST(julianday('now') - julianday(COALESCE(pw_changed_at, created_at)) AS INTEGER) FROM users WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return -1;
  }
  sqlite3_bind_int(st, 1, user_id);
  int days = -1;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    days = sqlite3_column_int(st, 0);
  }
  sqlite3_finalize(st);
  return days;
#else
  (void)user_id;
  set_err(db, "sqlite disabled");
  return -1;
#endif
}

/* Subscription management */
int db_subscription_type_list(BbsDb *db, DbSubscriptionType *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, name, days, security_level_id, expired_level_id, price, COALESCE(description,'') FROM subscription_types ORDER BY id ASC";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    safe_copy(out[count].name, sizeof(out[count].name), (const char *)sqlite3_column_text(st, 1));
    out[count].days = sqlite3_column_int(st, 2);
    out[count].security_level_id = sqlite3_column_int(st, 3);
    out[count].expired_level_id = sqlite3_column_int(st, 4);
    out[count].price = sqlite3_column_int(st, 5);
    safe_copy(out[count].description, sizeof(out[count].description), (const char *)sqlite3_column_text(st, 6));
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_subscription_type_add(BbsDb *db, const char *name, int days, int level_id, int expired_level_id, int price, const char *desc)
{
  if (!db || !name)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO subscription_types (name, days, security_level_id, expired_level_id, price, description) VALUES (?1, ?2, ?3, ?4, ?5, ?6)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, days);
  sqlite3_bind_int(st, 3, level_id);
  sqlite3_bind_int(st, 4, expired_level_id);
  sqlite3_bind_int(st, 5, price);
  sqlite3_bind_text(st, 6, desc ? desc : "", -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)name;
  (void)days;
  (void)level_id;
  (void)expired_level_id;
  (void)price;
  (void)desc;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_subscription_type_get(BbsDb *db, int id, DbSubscriptionType *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, name, days, security_level_id, expired_level_id, price, COALESCE(description,'') FROM subscription_types WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    out->id = sqlite3_column_int(st, 0);
    safe_copy(out->name, sizeof(out->name), (const char *)sqlite3_column_text(st, 1));
    out->days = sqlite3_column_int(st, 2);
    out->security_level_id = sqlite3_column_int(st, 3);
    out->expired_level_id = sqlite3_column_int(st, 4);
    out->price = sqlite3_column_int(st, 5);
    safe_copy(out->description, sizeof(out->description), (const char *)sqlite3_column_text(st, 6));
    found = true;
  }
  sqlite3_finalize(st);
  return found;
#else
  (void)id;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_user_subscribe(BbsDb *db, int user_id, int type_id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  DbSubscriptionType st_type;
  if (!db_subscription_type_get(db, type_id, &st_type))
    return false;

  char expires[32];
  snprintf(expires, sizeof(expires), "datetime('now', '+%d days')", st_type.days);

  char sql[512];
  snprintf(sql, sizeof(sql),
           "INSERT INTO user_subscriptions (user_id, subscription_type_id, expires_at) "
           "VALUES (%d, %d, datetime('now', '+%d days'))",
           user_id, type_id, st_type.days);

  if (!db_exec(db, sql))
    return false;

  snprintf(sql, sizeof(sql),
           "UPDATE users SET security_level_id = %d, expires_at = datetime('now', '+%d days') WHERE id = %d",
           st_type.security_level_id, st_type.days, user_id);

  return db_exec(db, sql);
#else
  (void)user_id;
  (void)type_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_user_subscription_get(BbsDb *db, int user_id, DbUserSubscription *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, user_id, subscription_type_id, started_at, expires_at, status "
                    "FROM user_subscriptions WHERE user_id = ?1 AND status = 'active' ORDER BY id DESC LIMIT 1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, user_id);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    out->id = sqlite3_column_int(st, 0);
    out->user_id = sqlite3_column_int(st, 1);
    out->subscription_type_id = sqlite3_column_int(st, 2);
    safe_copy(out->started_at, sizeof(out->started_at), (const char *)sqlite3_column_text(st, 3));
    safe_copy(out->expires_at, sizeof(out->expires_at), (const char *)sqlite3_column_text(st, 4));
    safe_copy(out->status, sizeof(out->status), (const char *)sqlite3_column_text(st, 5));
    found = true;
  }
  sqlite3_finalize(st);
  return found;
#else
  (void)user_id;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_subscription_check_expired(BbsDb *db)
{
  if (!db)
    return -1;
#ifdef HAVE_SQLITE
  int count = 0;
  const char *sql = "SELECT us.id, us.user_id, st.expired_level_id "
                    "FROM user_subscriptions us "
                    "JOIN subscription_types st ON us.subscription_type_id = st.id "
                    "WHERE us.status = 'active' AND us.expires_at < datetime('now')";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }

  while (sqlite3_step(st) == SQLITE_ROW)
  {
    int sub_id = sqlite3_column_int(st, 0);
    int user_id = sqlite3_column_int(st, 1);
    int expired_level = sqlite3_column_int(st, 2);

    char update_sql[256];
    snprintf(update_sql, sizeof(update_sql),
             "UPDATE user_subscriptions SET status = 'expired' WHERE id = %d", sub_id);
    db_exec(db, update_sql);

    snprintf(update_sql, sizeof(update_sql),
             "UPDATE users SET security_level_id = %d, expires_at = NULL WHERE id = %d",
             expired_level, user_id);
    db_exec(db, update_sql);

    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_user_set_expires(BbsDb *db, int user_id, const char *expires_at)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = expires_at && expires_at[0]
                        ? "UPDATE users SET expires_at = ?2 WHERE id = ?1"
                        : "UPDATE users SET expires_at = NULL WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, user_id);
  if (expires_at && expires_at[0])
  {
    sqlite3_bind_text(st, 2, expires_at, -1, SQLITE_TRANSIENT);
  }
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)user_id;
  (void)expires_at;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_security_level_fetch(BbsDb *db, int id, DbSecurityLevel *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql =
      "SELECT id, name, level, time_limit_min, call_allow, dl_one_day, dl_k_one_day, "
      "download_ratio_num, download_ratio_den, post_ratio_num, post_ratio_den, "
      "COALESCE(ul_dl_ratio_num,0), COALESCE(ul_dl_ratio_den,1), COALESCE(post_call_ratio,0), "
      "COALESCE(email_allow,1), COALESCE(vote_allow,1), COALESCE(anon_allow,0), flags "
      "FROM security_levels WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  int rc = sqlite3_step(st);
  if (rc == SQLITE_ROW)
  {
    memset(out, 0, sizeof(*out));
    int col = 0;
    out->id = sqlite3_column_int(st, col++);
    safe_copy(out->name, sizeof(out->name), (const char *)sqlite3_column_text(st, col++));
    out->level = sqlite3_column_int(st, col++);
    out->time_limit_min = sqlite3_column_int(st, col++);
    out->call_allow = sqlite3_column_int(st, col++);
    out->dl_one_day = sqlite3_column_int(st, col++);
    out->dl_k_one_day = sqlite3_column_int(st, col++);
    out->download_ratio_num = sqlite3_column_int(st, col++);
    out->download_ratio_den = sqlite3_column_int(st, col++);
    out->post_ratio_num = sqlite3_column_int(st, col++);
    out->post_ratio_den = sqlite3_column_int(st, col++);
    out->ul_dl_ratio_num = sqlite3_column_int(st, col++);
    out->ul_dl_ratio_den = sqlite3_column_int(st, col++);
    out->post_call_ratio = sqlite3_column_int(st, col++);
    out->email_allow = sqlite3_column_int(st, col++);
    out->vote_allow = sqlite3_column_int(st, col++);
    out->anon_allow = sqlite3_column_int(st, col++);
    out->flags = (unsigned)sqlite3_column_int(st, col++);
    sqlite3_finalize(st);
    return true;
  }
  sqlite3_finalize(st);
  set_err(db, "security level not found");
  return false;
#else
  (void)id;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_security_level_list(BbsDb *db, DbSecurityLevel *out, int max_levels)
{
  if (!db || !out || max_levels <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql =
      "SELECT id, name, level, time_limit_min, call_allow, dl_one_day, dl_k_one_day, "
      "download_ratio_num, download_ratio_den, post_ratio_num, post_ratio_den, "
      "COALESCE(ul_dl_ratio_num,0), COALESCE(ul_dl_ratio_den,1), COALESCE(post_call_ratio,0), "
      "COALESCE(email_allow,1), COALESCE(vote_allow,1), COALESCE(anon_allow,0), flags "
      "FROM security_levels ORDER BY level";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  int count = 0;
  while (count < max_levels && sqlite3_step(st) == SQLITE_ROW)
  {
    DbSecurityLevel *sl = &out[count];
    memset(sl, 0, sizeof(*sl));
    int col = 0;
    sl->id = sqlite3_column_int(st, col++);
    safe_copy(sl->name, sizeof(sl->name), (const char *)sqlite3_column_text(st, col++));
    sl->level = sqlite3_column_int(st, col++);
    sl->time_limit_min = sqlite3_column_int(st, col++);
    sl->call_allow = sqlite3_column_int(st, col++);
    sl->dl_one_day = sqlite3_column_int(st, col++);
    sl->dl_k_one_day = sqlite3_column_int(st, col++);
    sl->download_ratio_num = sqlite3_column_int(st, col++);
    sl->download_ratio_den = sqlite3_column_int(st, col++);
    sl->post_ratio_num = sqlite3_column_int(st, col++);
    sl->post_ratio_den = sqlite3_column_int(st, col++);
    sl->ul_dl_ratio_num = sqlite3_column_int(st, col++);
    sl->ul_dl_ratio_den = sqlite3_column_int(st, col++);
    sl->post_call_ratio = sqlite3_column_int(st, col++);
    sl->email_allow = sqlite3_column_int(st, col++);
    sl->vote_allow = sqlite3_column_int(st, col++);
    sl->anon_allow = sqlite3_column_int(st, col++);
    sl->flags = (unsigned)sqlite3_column_int(st, col++);
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max_levels;
  return 0;
#endif
}

bool db_security_level_update(BbsDb *db, const DbSecurityLevel *sl)
{
  if (!db || !sl)
    return false;
#ifdef HAVE_SQLITE
  const char *sql =
      "UPDATE security_levels SET name=?1, level=?2, time_limit_min=?3, call_allow=?4, "
      "dl_one_day=?5, dl_k_one_day=?6, download_ratio_num=?7, download_ratio_den=?8, "
      "post_ratio_num=?9, post_ratio_den=?10, ul_dl_ratio_num=?11, ul_dl_ratio_den=?12, "
      "post_call_ratio=?13, email_allow=?14, vote_allow=?15, anon_allow=?16, flags=?17 "
      "WHERE id=?18";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  int p = 1;
  sqlite3_bind_text(st, p++, sl->name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, p++, sl->level);
  sqlite3_bind_int(st, p++, sl->time_limit_min);
  sqlite3_bind_int(st, p++, sl->call_allow);
  sqlite3_bind_int(st, p++, sl->dl_one_day);
  sqlite3_bind_int(st, p++, sl->dl_k_one_day);
  sqlite3_bind_int(st, p++, sl->download_ratio_num);
  sqlite3_bind_int(st, p++, sl->download_ratio_den);
  sqlite3_bind_int(st, p++, sl->post_ratio_num);
  sqlite3_bind_int(st, p++, sl->post_ratio_den);
  sqlite3_bind_int(st, p++, sl->ul_dl_ratio_num);
  sqlite3_bind_int(st, p++, sl->ul_dl_ratio_den);
  sqlite3_bind_int(st, p++, sl->post_call_ratio);
  sqlite3_bind_int(st, p++, sl->email_allow);
  sqlite3_bind_int(st, p++, sl->vote_allow);
  sqlite3_bind_int(st, p++, sl->anon_allow);
  sqlite3_bind_int(st, p++, (int)sl->flags);
  sqlite3_bind_int(st, p++, sl->id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)sl;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_validation_level_fetch(BbsDb *db, char key, DbValidationLevel *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql =
      "SELECT id, key, description, user_msg, new_sl, new_dsl, new_menu, expiration, "
      "expire_to, new_fp, new_credit, soft_ar, soft_ac, new_ar, new_ac "
      "FROM validation_levels WHERE key = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  char key_str[2] = {key, '\0'};
  sqlite3_bind_text(st, 1, key_str, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  if (rc == SQLITE_ROW)
  {
    memset(out, 0, sizeof(*out));
    int col = 0;
    out->id = sqlite3_column_int(st, col++);
    const char *k = (const char *)sqlite3_column_text(st, col++);
    out->key = (k && k[0]) ? k[0] : ' ';
    safe_copy(out->description, sizeof(out->description), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->user_msg, sizeof(out->user_msg), (const char *)sqlite3_column_text(st, col++));
    out->new_sl = sqlite3_column_int(st, col++);
    out->new_dsl = sqlite3_column_int(st, col++);
    out->new_menu = sqlite3_column_int(st, col++);
    out->expiration = sqlite3_column_int(st, col++);
    out->expire_to = sqlite3_column_int(st, col++);
    out->new_fp = sqlite3_column_int(st, col++);
    out->new_credit = sqlite3_column_int(st, col++);
    out->soft_ar = (unsigned)sqlite3_column_int(st, col++);
    out->soft_ac = (unsigned)sqlite3_column_int(st, col++);
    out->new_ar = (unsigned)sqlite3_column_int(st, col++);
    out->new_ac = (unsigned)sqlite3_column_int(st, col++);
    sqlite3_finalize(st);
    return true;
  }
  sqlite3_finalize(st);
  set_err(db, "validation level not found");
  return false;
#else
  (void)key;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_validation_level_list(BbsDb *db, DbValidationLevel *out, int max_levels)
{
  if (!db || !out || max_levels <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql =
      "SELECT id, key, description, user_msg, new_sl, new_dsl, new_menu, expiration, "
      "expire_to, new_fp, new_credit, soft_ar, soft_ac, new_ar, new_ac "
      "FROM validation_levels ORDER BY key";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  int count = 0;
  while (count < max_levels && sqlite3_step(st) == SQLITE_ROW)
  {
    DbValidationLevel *vl = &out[count];
    memset(vl, 0, sizeof(*vl));
    int col = 0;
    vl->id = sqlite3_column_int(st, col++);
    const char *k = (const char *)sqlite3_column_text(st, col++);
    vl->key = (k && k[0]) ? k[0] : ' ';
    safe_copy(vl->description, sizeof(vl->description), (const char *)sqlite3_column_text(st, col++));
    safe_copy(vl->user_msg, sizeof(vl->user_msg), (const char *)sqlite3_column_text(st, col++));
    vl->new_sl = sqlite3_column_int(st, col++);
    vl->new_dsl = sqlite3_column_int(st, col++);
    vl->new_menu = sqlite3_column_int(st, col++);
    vl->expiration = sqlite3_column_int(st, col++);
    vl->expire_to = sqlite3_column_int(st, col++);
    vl->new_fp = sqlite3_column_int(st, col++);
    vl->new_credit = sqlite3_column_int(st, col++);
    vl->soft_ar = (unsigned)sqlite3_column_int(st, col++);
    vl->soft_ac = (unsigned)sqlite3_column_int(st, col++);
    vl->new_ar = (unsigned)sqlite3_column_int(st, col++);
    vl->new_ac = (unsigned)sqlite3_column_int(st, col++);
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max_levels;
  return 0;
#endif
}

bool db_validation_level_create(BbsDb *db, const DbValidationLevel *vl)
{
  if (!db || !vl)
    return false;
#ifdef HAVE_SQLITE
  const char *sql =
      "INSERT INTO validation_levels (key, description, user_msg, new_sl, new_dsl, new_menu, "
      "expiration, expire_to, new_fp, new_credit, soft_ar, soft_ac, new_ar, new_ac) "
      "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  int p = 1;
  char key_str[2] = {vl->key, '\0'};
  sqlite3_bind_text(st, p++, key_str, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, vl->description, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, vl->user_msg, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, p++, vl->new_sl);
  sqlite3_bind_int(st, p++, vl->new_dsl);
  sqlite3_bind_int(st, p++, vl->new_menu);
  sqlite3_bind_int(st, p++, vl->expiration);
  sqlite3_bind_int(st, p++, vl->expire_to);
  sqlite3_bind_int(st, p++, vl->new_fp);
  sqlite3_bind_int(st, p++, vl->new_credit);
  sqlite3_bind_int(st, p++, (int)vl->soft_ar);
  sqlite3_bind_int(st, p++, (int)vl->soft_ac);
  sqlite3_bind_int(st, p++, (int)vl->new_ar);
  sqlite3_bind_int(st, p++, (int)vl->new_ac);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)vl;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_validation_level_update(BbsDb *db, const DbValidationLevel *vl)
{
  if (!db || !vl)
    return false;
#ifdef HAVE_SQLITE
  const char *sql =
      "UPDATE validation_levels SET description=?1, user_msg=?2, new_sl=?3, new_dsl=?4, "
      "new_menu=?5, expiration=?6, expire_to=?7, new_fp=?8, new_credit=?9, soft_ar=?10, "
      "soft_ac=?11, new_ar=?12, new_ac=?13 WHERE id=?14";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  int p = 1;
  sqlite3_bind_text(st, p++, vl->description, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, vl->user_msg, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, p++, vl->new_sl);
  sqlite3_bind_int(st, p++, vl->new_dsl);
  sqlite3_bind_int(st, p++, vl->new_menu);
  sqlite3_bind_int(st, p++, vl->expiration);
  sqlite3_bind_int(st, p++, vl->expire_to);
  sqlite3_bind_int(st, p++, vl->new_fp);
  sqlite3_bind_int(st, p++, vl->new_credit);
  sqlite3_bind_int(st, p++, (int)vl->soft_ar);
  sqlite3_bind_int(st, p++, (int)vl->soft_ac);
  sqlite3_bind_int(st, p++, (int)vl->new_ar);
  sqlite3_bind_int(st, p++, (int)vl->new_ac);
  sqlite3_bind_int(st, p++, vl->id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)vl;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_validation_level_apply(BbsDb *db, int user_id, char key)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  DbValidationLevel vl;
  if (!db_validation_level_fetch(db, key, &vl))
    return false;

  const char *sql =
      "UPDATE users SET security_level_id = (SELECT id FROM security_levels WHERE level = ?1 LIMIT 1), "
      "dsl = ?2, user_start_menu = ?3, file_points = file_points + ?4, credits = credits + ?5, "
      "flags = CASE WHEN ?6 > 0 THEN (flags | ?6) ELSE ?7 END, "
      "ac_flags = CASE WHEN ?8 > 0 THEN (ac_flags | ?8) ELSE ?9 END, "
      "expires_at = CASE WHEN ?10 > 0 THEN datetime('now', '+' || ?10 || ' days') ELSE expires_at END "
      "WHERE id = ?11";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  int p = 1;
  sqlite3_bind_int(st, p++, vl.new_sl);
  sqlite3_bind_int(st, p++, vl.new_dsl);
  sqlite3_bind_int(st, p++, vl.new_menu);
  sqlite3_bind_int(st, p++, vl.new_fp);
  sqlite3_bind_int(st, p++, vl.new_credit);
  sqlite3_bind_int(st, p++, (int)vl.soft_ar);
  sqlite3_bind_int(st, p++, (int)vl.new_ar);
  sqlite3_bind_int(st, p++, (int)vl.soft_ac);
  sqlite3_bind_int(st, p++, (int)vl.new_ac);
  sqlite3_bind_int(st, p++, vl.expiration);
  sqlite3_bind_int(st, p++, user_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)user_id;
  (void)key;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_node_upsert(BbsDb *db, int node_num, int user_id, const char *status, const char *activity, const char *ip)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql =
      "INSERT INTO nodes (node_num, user_id, status, activity, ip, updated_at) "
      "VALUES (?1, ?2, ?3, ?4, ?5, datetime('now')) "
      "ON CONFLICT(node_num) DO UPDATE SET "
      "  user_id=excluded.user_id, status=excluded.status, activity=excluded.activity, ip=excluded.ip, updated_at=datetime('now')";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, node_num);
  if (user_id > 0)
  {
    sqlite3_bind_int(st, 2, user_id);
  }
  else
  {
    sqlite3_bind_null(st, 2);
  }
  sqlite3_bind_text(st, 3, status ? status : "online", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, activity ? activity : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, ip ? ip : "", -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE)
  {
    set_err(db, "node upsert failed");
    return false;
  }
  return true;
#else
  (void)node_num;
  (void)user_id;
  (void)status;
  (void)activity;
  (void)ip;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_node_clear(BbsDb *db, int node_num)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "DELETE FROM nodes WHERE node_num = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, node_num);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE)
  {
    set_err(db, "node clear failed");
    return false;
  }
  return true;
#else
  (void)node_num;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_node_list(BbsDb *db, DbNode *out, int max_nodes)
{
  if (!db || !out || max_nodes <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT n.node_num, n.user_id, COALESCE(u.handle,''), n.status, n.activity, n.ip "
                    "FROM nodes n LEFT JOIN users u ON u.id = n.user_id "
                    "ORDER BY n.node_num ASC LIMIT ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, max_nodes);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max_nodes)
  {
    out[count].node_num = sqlite3_column_int(st, 0);
    out[count].user_id = sqlite3_column_int(st, 1);
    safe_copy(out[count].handle, sizeof(out[count].handle), (const char *)sqlite3_column_text(st, 2));
    safe_copy(out[count].status, sizeof(out[count].status), (const char *)sqlite3_column_text(st, 3));
    safe_copy(out[count].activity, sizeof(out[count].activity), (const char *)sqlite3_column_text(st, 4));
    safe_copy(out[count].ip, sizeof(out[count].ip), (const char *)sqlite3_column_text(st, 5));
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max_nodes;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_node_user_online(BbsDb *db, int user_id, int *out_node)
{
  if (!db || user_id <= 0)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT node_num FROM nodes WHERE user_id = ?1 LIMIT 1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, user_id);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    found = true;
    if (out_node)
      *out_node = sqlite3_column_int(st, 0);
  }
  sqlite3_finalize(st);
  return found;
#else
  (void)user_id;
  (void)out_node;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_msg_area_list(BbsDb *db, DbMsgArea *out, int max_areas)
{
  if (!db || !out || max_areas <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, name, COALESCE(acs,'') FROM message_areas ORDER BY id ASC LIMIT ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, max_areas);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max_areas)
  {
    out[count].id = sqlite3_column_int(st, 0);
    safe_copy(out[count].name, sizeof(out[count].name), (const char *)sqlite3_column_text(st, 1));
    safe_copy(out[count].acs, sizeof(out[count].acs), (const char *)sqlite3_column_text(st, 2));
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max_areas;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_msg_area_seed(BbsDb *db, const char *name)
{
  if (!db || !name)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO message_areas (name) VALUES (?1) ON CONFLICT(name) DO NOTHING";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE && rc != SQLITE_CONSTRAINT)
  {
    set_err(db, "seed msg area failed");
    return false;
  }
  return true;
#else
  (void)name;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_messages_list(BbsDb *db, int area_id, DbMessage *out, int max_msgs)
{
  if (!db || !out || max_msgs <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql =
      "SELECT m.id, m.area_id, m.user_id, m.to_user, m.reply_to, m.thread_root, "
      "COALESCE(u.handle,''), COALESCE(m.from_name,''), COALESCE(m.to_name,''), "
      "m.subject, m.body, m.posted_at, COALESCE(m.attr,0), COALESCE(m.net_attr,0), "
      "COALESCE(m.file_attached,''), COALESCE(m.origin,'') "
      "FROM messages m LEFT JOIN users u ON u.id = m.user_id "
      "WHERE m.area_id = ?1 ORDER BY m.id DESC LIMIT ?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, area_id);
  sqlite3_bind_int(st, 2, max_msgs);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max_msgs)
  {
    DbMessage *m = &out[count];
    memset(m, 0, sizeof(*m));
    int col = 0;
    m->id = sqlite3_column_int(st, col++);
    m->area_id = sqlite3_column_int(st, col++);
    m->user_id = sqlite3_column_int(st, col++);
    m->to_user = sqlite3_column_int(st, col++);
    m->reply_to = sqlite3_column_int(st, col++);
    m->thread_root = sqlite3_column_int(st, col++);
    safe_copy(m->user_handle, sizeof(m->user_handle), (const char *)sqlite3_column_text(st, col++));
    safe_copy(m->from_name, sizeof(m->from_name), (const char *)sqlite3_column_text(st, col++));
    safe_copy(m->to_name, sizeof(m->to_name), (const char *)sqlite3_column_text(st, col++));
    safe_copy(m->subject, sizeof(m->subject), (const char *)sqlite3_column_text(st, col++));
    safe_copy(m->body, sizeof(m->body), (const char *)sqlite3_column_text(st, col++));
    safe_copy(m->created_at, sizeof(m->created_at), (const char *)sqlite3_column_text(st, col++));
    m->attr = (unsigned)sqlite3_column_int(st, col++);
    m->net_attr = (unsigned)sqlite3_column_int(st, col++);
    safe_copy(m->file_attached, sizeof(m->file_attached), (const char *)sqlite3_column_text(st, col++));
    safe_copy(m->origin, sizeof(m->origin), (const char *)sqlite3_column_text(st, col++));
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)area_id;
  (void)out;
  (void)max_msgs;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

int db_messages_since(BbsDb *db, int area_id, const char *since, DbMessage *out, int max_msgs)
{
  if (!db || !out || max_msgs <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql;
  sqlite3_stmt *st = NULL;
  int bind_area, bind_max;

  if (since && since[0]) {
    sql =
        "SELECT m.id, m.area_id, m.user_id, m.to_user, m.reply_to, m.thread_root, "
        "COALESCE(u.handle,''), COALESCE(m.from_name,''), COALESCE(m.to_name,''), "
        "m.subject, m.body, m.posted_at, COALESCE(m.attr,0), COALESCE(m.net_attr,0), "
        "COALESCE(m.file_attached,''), COALESCE(m.origin,'') "
        "FROM messages m LEFT JOIN users u ON u.id = m.user_id "
        "WHERE m.area_id = ?1 AND m.posted_at > ?2 ORDER BY m.id ASC LIMIT ?3";
    if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK) {
      set_err(db, sqlite3_errmsg(db->db));
      return 0;
    }
    bind_area = 1; bind_max = 3;
    sqlite3_bind_int(st, 1, area_id);
    sqlite3_bind_text(st, 2, since, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 3, max_msgs);
  } else {
    sql =
        "SELECT m.id, m.area_id, m.user_id, m.to_user, m.reply_to, m.thread_root, "
        "COALESCE(u.handle,''), COALESCE(m.from_name,''), COALESCE(m.to_name,''), "
        "m.subject, m.body, m.posted_at, COALESCE(m.attr,0), COALESCE(m.net_attr,0), "
        "COALESCE(m.file_attached,''), COALESCE(m.origin,'') "
        "FROM messages m LEFT JOIN users u ON u.id = m.user_id "
        "WHERE m.area_id = ?1 ORDER BY m.id ASC LIMIT ?2";
    if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK) {
      set_err(db, sqlite3_errmsg(db->db));
      return 0;
    }
    bind_area = 1; bind_max = 2;
    sqlite3_bind_int(st, 1, area_id);
    sqlite3_bind_int(st, 2, max_msgs);
  }
  (void)bind_area; (void)bind_max;
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max_msgs) {
    DbMessage *m = &out[count];
    memset(m, 0, sizeof(*m));
    int col = 0;
    m->id = sqlite3_column_int(st, col++);
    m->area_id = sqlite3_column_int(st, col++);
    m->user_id = sqlite3_column_int(st, col++);
    m->to_user = sqlite3_column_int(st, col++);
    m->reply_to = sqlite3_column_int(st, col++);
    m->thread_root = sqlite3_column_int(st, col++);
    safe_copy(m->user_handle, sizeof(m->user_handle), (const char *)sqlite3_column_text(st, col++));
    safe_copy(m->from_name, sizeof(m->from_name), (const char *)sqlite3_column_text(st, col++));
    safe_copy(m->to_name, sizeof(m->to_name), (const char *)sqlite3_column_text(st, col++));
    safe_copy(m->subject, sizeof(m->subject), (const char *)sqlite3_column_text(st, col++));
    safe_copy(m->body, sizeof(m->body), (const char *)sqlite3_column_text(st, col++));
    safe_copy(m->created_at, sizeof(m->created_at), (const char *)sqlite3_column_text(st, col++));
    m->attr = (unsigned)sqlite3_column_int(st, col++);
    m->net_attr = (unsigned)sqlite3_column_int(st, col++);
    safe_copy(m->file_attached, sizeof(m->file_attached), (const char *)sqlite3_column_text(st, col++));
    safe_copy(m->origin, sizeof(m->origin), (const char *)sqlite3_column_text(st, col++));
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)area_id; (void)since; (void)out; (void)max_msgs;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_user_set_last_qwk(BbsDb *db, int user_id, const char *ts)
{
  if (!db || !ts) return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE users SET last_qwk = ?1 WHERE id = ?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK) {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, ts, -1, SQLITE_STATIC);
  sqlite3_bind_int(st, 2, user_id);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
#else
  (void)user_id; (void)ts;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_message_post(BbsDb *db, int area_id, int user_id, const char *subject, const char *body, int reply_to)
{
  if (!db || !subject || !body)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO messages (area_id, user_id, subject, body, reply_to, thread_root) "
                    "VALUES (?1, ?2, ?3, ?4, ?5, "
                    "COALESCE((SELECT thread_root FROM messages WHERE id=?5), CASE WHEN ?5>0 THEN ?5 ELSE NULL END))";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, area_id);
  sqlite3_bind_int(st, 2, user_id);
  sqlite3_bind_text(st, 3, subject, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, body, -1, SQLITE_TRANSIENT);
  /* reply_to param - bind NULL if 0 to satisfy foreign key constraint */
  if (reply_to > 0)
  {
    sqlite3_bind_int(st, 5, reply_to);
  }
  else
  {
    sqlite3_bind_null(st, 5);
  }
  int rc = sqlite3_step(st);
  if (rc != SQLITE_DONE)
  {
    set_err(db, sqlite3_errmsg(db->db));
    sqlite3_finalize(st);
    return false;
  }
  sqlite3_finalize(st);
  return true;
#else
  (void)area_id;
  (void)user_id;
  (void)subject;
  (void)body;
  (void)reply_to;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_message_post_ex(BbsDb *db, const DbMessage *msg)
{
  if (!db || !msg)
    return false;
#ifdef HAVE_SQLITE
  const char *sql =
      "INSERT INTO messages (area_id, user_id, to_user, reply_to, thread_root, subject, body, "
      "from_name, to_name, attr, net_attr, file_attached, origin) "
      "VALUES (?1, ?2, ?3, ?4, "
      "COALESCE((SELECT thread_root FROM messages WHERE id=?4), CASE WHEN ?4>0 THEN ?4 ELSE NULL END), "
      "?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  int p = 1;
  sqlite3_bind_int(st, p++, msg->area_id);
  sqlite3_bind_int(st, p++, msg->user_id);
  sqlite3_bind_int(st, p++, msg->to_user);
  sqlite3_bind_int(st, p++, msg->reply_to);
  sqlite3_bind_text(st, p++, msg->subject, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, msg->body, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, msg->from_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, msg->to_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, p++, (int)msg->attr);
  sqlite3_bind_int(st, p++, (int)msg->net_attr);
  sqlite3_bind_text(st, p++, msg->file_attached, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, msg->origin, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)msg;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_message_forward(BbsDb *db, int msg_id, int to_user_id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  DbMessage orig;
  if (!db_message_get(db, msg_id, &orig))
    return false;

  DbMessage fwd;
  memset(&fwd, 0, sizeof(fwd));
  fwd.area_id = 0; /* Email area */
  fwd.user_id = orig.user_id;
  fwd.to_user = to_user_id;
  snprintf(fwd.subject, sizeof(fwd.subject), "Fwd: %s", orig.subject);
  snprintf(fwd.body, sizeof(fwd.body), "--- Forwarded message ---\r\n%s", orig.body);
  fwd.attr = MSG_ATTR_FORWARDED;

  return db_message_post_ex(db, &fwd);
#else
  (void)msg_id;
  (void)to_user_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_message_mass_mail(BbsDb *db, int from_user_id, const char *subject, const char *body,
                          const int *to_users, int to_count)
{
  if (!db || !subject || !body || !to_users || to_count <= 0)
    return false;
#ifdef HAVE_SQLITE
  bool ok = true;
  for (int i = 0; i < to_count && ok; i++)
  {
    DbMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.area_id = 0; /* Email area */
    msg.user_id = from_user_id;
    msg.to_user = to_users[i];
    snprintf(msg.subject, sizeof(msg.subject), "%s", subject);
    snprintf(msg.body, sizeof(msg.body), "%s", body);
    ok = db_message_post_ex(db, &msg);
    if (ok)
    {
      db_user_set_smw(db, to_users[i], 1);
    }
  }
  return ok;
#else
  (void)from_user_id;
  (void)subject;
  (void)body;
  (void)to_users;
  (void)to_count;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_message_get(BbsDb *db, int msg_id, DbMessage *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql =
      "SELECT m.id, m.area_id, m.user_id, m.to_user, m.reply_to, m.thread_root, "
      "COALESCE(u.handle,''), COALESCE(m.from_name,''), COALESCE(m.to_name,''), "
      "m.subject, m.body, m.posted_at, COALESCE(m.attr,0), COALESCE(m.net_attr,0), "
      "COALESCE(m.file_attached,''), COALESCE(m.origin,'') "
      "FROM messages m LEFT JOIN users u ON u.id = m.user_id WHERE m.id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, msg_id);
  int rc = sqlite3_step(st);
  if (rc == SQLITE_ROW)
  {
    memset(out, 0, sizeof(*out));
    int col = 0;
    out->id = sqlite3_column_int(st, col++);
    out->area_id = sqlite3_column_int(st, col++);
    out->user_id = sqlite3_column_int(st, col++);
    out->to_user = sqlite3_column_int(st, col++);
    out->reply_to = sqlite3_column_int(st, col++);
    out->thread_root = sqlite3_column_int(st, col++);
    safe_copy(out->user_handle, sizeof(out->user_handle), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->from_name, sizeof(out->from_name), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->to_name, sizeof(out->to_name), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->subject, sizeof(out->subject), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->body, sizeof(out->body), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->created_at, sizeof(out->created_at), (const char *)sqlite3_column_text(st, col++));
    out->attr = (unsigned)sqlite3_column_int(st, col++);
    out->net_attr = (unsigned)sqlite3_column_int(st, col++);
    safe_copy(out->file_attached, sizeof(out->file_attached), (const char *)sqlite3_column_text(st, col++));
    safe_copy(out->origin, sizeof(out->origin), (const char *)sqlite3_column_text(st, col++));
    sqlite3_finalize(st);
    return true;
  }
  sqlite3_finalize(st);
  set_err(db, "message not found");
  return false;
#else
  (void)msg_id;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_message_update_body(BbsDb *db, int msg_id, const char *body)
{
  if (!db || !body)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE messages SET body = ?2 WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, msg_id);
  sqlite3_bind_text(st, 2, body, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)msg_id;
  (void)body;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_file_area_list(BbsDb *db, DbFileArea *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, name, path, "
                    "COALESCE(acs_list,''), COALESCE(acs_download,''), COALESCE(acs_upload,''), COALESCE(acs_sysop,''), "
                    "COALESCE(password,''), max_files, COALESCE(archive_type,''), sort_type, show_uploader, "
                    "check_dupes, free_files, flags FROM file_areas ORDER BY id ASC LIMIT ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, max);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    safe_copy(out[count].name, sizeof(out[count].name), (const char *)sqlite3_column_text(st, 1));
    safe_copy(out[count].path, sizeof(out[count].path), (const char *)sqlite3_column_text(st, 2));
    safe_copy(out[count].acs_list, sizeof(out[count].acs_list), (const char *)sqlite3_column_text(st, 3));
    safe_copy(out[count].acs_download, sizeof(out[count].acs_download), (const char *)sqlite3_column_text(st, 4));
    safe_copy(out[count].acs_upload, sizeof(out[count].acs_upload), (const char *)sqlite3_column_text(st, 5));
    safe_copy(out[count].acs_sysop, sizeof(out[count].acs_sysop), (const char *)sqlite3_column_text(st, 6));
    safe_copy(out[count].password, sizeof(out[count].password), (const char *)sqlite3_column_text(st, 7));
    out[count].max_files = sqlite3_column_int(st, 8);
    safe_copy(out[count].archive_type, sizeof(out[count].archive_type), (const char *)sqlite3_column_text(st, 9));
    out[count].sort_type = sqlite3_column_int(st, 10);
    out[count].show_uploader = sqlite3_column_int(st, 11);
    out[count].check_dupes = sqlite3_column_int(st, 12);
    out[count].free_files = sqlite3_column_int(st, 13);
    out[count].flags = sqlite3_column_int(st, 14);
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_file_area_seed(BbsDb *db, const char *name, const char *path)
{
  if (!db || !name || !path)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO file_areas (name, path) VALUES (?1, ?2) ON CONFLICT(name) DO NOTHING";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, path, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE && rc != SQLITE_CONSTRAINT)
  {
    set_err(db, "seed file area failed");
    return false;
  }
  return true;
#else
  (void)name;
  (void)path;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_file_list(DbFileArea *area, BbsDb *db, DbFileRec *out, int max)
{
  if (!db || !area || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql =
      "SELECT f.id, f.area_id, f.filename, COALESCE(f.description,''), COALESCE(f.extended_desc,''), "
      "COALESCE(f.file_id_diz,''), f.size_bytes, f.uploaded_at, f.uploaded_by, COALESCE(u.handle,''), "
      "COALESCE(f.sha256,''), f.file_points, f.download_count, f.owner_credit, f.flags "
      "FROM files f LEFT JOIN users u ON u.id = f.uploaded_by "
      "WHERE f.area_id = ?1 ORDER BY f.id DESC LIMIT ?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, area->id);
  sqlite3_bind_int(st, 2, max);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    out[count].area_id = sqlite3_column_int(st, 1);
    safe_copy(out[count].filename, sizeof(out[count].filename), (const char *)sqlite3_column_text(st, 2));
    safe_copy(out[count].desc, sizeof(out[count].desc), (const char *)sqlite3_column_text(st, 3));
    safe_copy(out[count].extended_desc, sizeof(out[count].extended_desc), (const char *)sqlite3_column_text(st, 4));
    safe_copy(out[count].file_id_diz, sizeof(out[count].file_id_diz), (const char *)sqlite3_column_text(st, 5));
    out[count].size_bytes = sqlite3_column_int(st, 6);
    safe_copy(out[count].uploaded_at, sizeof(out[count].uploaded_at), (const char *)sqlite3_column_text(st, 7));
    out[count].uploaded_by = sqlite3_column_int(st, 8);
    safe_copy(out[count].uploader, sizeof(out[count].uploader), (const char *)sqlite3_column_text(st, 9));
    safe_copy(out[count].sha256, sizeof(out[count].sha256), (const char *)sqlite3_column_text(st, 10));
    out[count].file_points = sqlite3_column_int(st, 11);
    out[count].download_count = sqlite3_column_int(st, 12);
    out[count].owner_credit = sqlite3_column_int(st, 13);
    out[count].flags = sqlite3_column_int(st, 14);
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)area;
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_file_add(BbsDb *db, int area_id, const char *filename, const char *desc, int size_bytes, int user_id)
{
  if (!db || !filename)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO files (area_id, filename, description, size_bytes, uploaded_by) VALUES (?1, ?2, ?3, ?4, ?5)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, area_id);
  sqlite3_bind_text(st, 2, filename, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, desc ? desc : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 4, size_bytes);
  sqlite3_bind_int(st, 5, user_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE)
  {
    set_err(db, "file add failed");
    return false;
  }
  return true;
#else
  (void)area_id;
  (void)filename;
  (void)desc;
  (void)size_bytes;
  (void)user_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_file_add_ex(BbsDb *db, int area_id, const char *filename, const char *desc,
                    const char *extended_desc, const char *file_id_diz, int size_bytes,
                    int user_id, int file_points, int flags)
{
  if (!db || !filename)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO files (area_id, filename, description, extended_desc, file_id_diz, "
                    "size_bytes, uploaded_by, file_points, flags) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, area_id);
  sqlite3_bind_text(st, 2, filename, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, desc ? desc : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, extended_desc ? extended_desc : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, file_id_diz ? file_id_diz : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 6, size_bytes);
  sqlite3_bind_int(st, 7, user_id);
  sqlite3_bind_int(st, 8, file_points);
  sqlite3_bind_int(st, 9, flags);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE)
  {
    set_err(db, "file add ex failed");
    return false;
  }
  return true;
#else
  (void)area_id;
  (void)filename;
  (void)desc;
  (void)extended_desc;
  (void)file_id_diz;
  (void)size_bytes;
  (void)user_id;
  (void)file_points;
  (void)flags;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_file_update(BbsDb *db, const DbFileRec *file)
{
  if (!db || !file)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE files SET description=?2, extended_desc=?3, file_id_diz=?4, "
                    "file_points=?5, owner_credit=?6, flags=?7 WHERE id=?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, file->id);
  sqlite3_bind_text(st, 2, file->desc, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, file->extended_desc, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, file->file_id_diz, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 5, file->file_points);
  sqlite3_bind_int(st, 6, file->owner_credit);
  sqlite3_bind_int(st, 7, file->flags);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)file;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_file_exists_name(BbsDb *db, int area_id, const char *filename, int *out_file_id)
{
  if (!db || !filename)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id FROM files WHERE area_id=?1 AND filename=?2 LIMIT 1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, area_id);
  sqlite3_bind_text(st, 2, filename, -1, SQLITE_TRANSIENT);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    if (out_file_id)
      *out_file_id = sqlite3_column_int(st, 0);
    found = true;
  }
  sqlite3_finalize(st);
  return found;
#else
  (void)area_id;
  (void)filename;
  (void)out_file_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_file_delete(BbsDb *db, int file_id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "DELETE FROM files WHERE id=?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, file_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)file_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_last_insert_id(BbsDb *db)
{
  if (!db)
    return -1;
#ifdef HAVE_SQLITE
  return (int)sqlite3_last_insert_rowid(db->db);
#else
  return 0;
#endif
}

static int db_count_int(BbsDb *db, const char *sql)
{
  if (!db)
    return -1;
#ifdef HAVE_SQLITE
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  int count = 0;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    count = sqlite3_column_int(st, 0);
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)sql;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

int db_count_messages(BbsDb *db)
{
  return db_count_int(db, "SELECT COUNT(*) FROM messages");
}

int db_count_messages_area(BbsDb *db, int area_id)
{
  if (!db)
    return -1;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT COUNT(*) FROM messages WHERE area_id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, area_id);
  int count = 0;
  if (sqlite3_step(st) == SQLITE_ROW)
    count = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return count;
#else
  (void)area_id;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

int db_count_user_posts(BbsDb *db, int user_id)
{
  if (!db)
    return -1;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT COUNT(*) FROM messages WHERE user_id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, user_id);
  int count = 0;
  if (sqlite3_step(st) == SQLITE_ROW)
    count = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return count;
#else
  (void)user_id;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

int db_count_files(BbsDb *db)
{
  return db_count_int(db, "SELECT COUNT(*) FROM files");
}

int db_count_files_area(BbsDb *db, int area_id)
{
  if (!db)
    return -1;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT COUNT(*) FROM files WHERE area_id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, area_id);
  int count = 0;
  if (sqlite3_step(st) == SQLITE_ROW)
    count = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return count;
#else
  (void)area_id;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

int db_count_users(BbsDb *db)
{
  if (!db)
    return -1;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT COUNT(*) FROM users WHERE (status_flags & 2) = 0";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  int count = 0;
  if (sqlite3_step(st) == SQLITE_ROW)
    count = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return count;
#else
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

int db_stats_get_val(BbsDb *db, const char *key)
{
  if (!db || !key)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT v FROM meta WHERE k = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
  int val = 0;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    const char *text = (const char *)sqlite3_column_text(st, 0);
    if (text)
      val = atoi(text);
  }
  sqlite3_finalize(st);
  return val;
#else
  (void)key;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_msg_area_get(BbsDb *db, int area_id, DbMsgArea *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, name, COALESCE(acs,'') FROM message_areas WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, area_id);
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    out->id = sqlite3_column_int(st, 0);
    safe_copy(out->name, sizeof(out->name), (const char *)sqlite3_column_text(st, 1));
    safe_copy(out->acs, sizeof(out->acs), (const char *)sqlite3_column_text(st, 2));
    sqlite3_finalize(st);
    return true;
  }
  sqlite3_finalize(st);
  return false;
#else
  (void)area_id;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_file_area_get(BbsDb *db, int area_id, DbFileArea *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, name, path, "
                    "COALESCE(acs_list,''), COALESCE(acs_download,''), COALESCE(acs_upload,''), COALESCE(acs_sysop,''), "
                    "COALESCE(password,''), max_files, COALESCE(archive_type,''), sort_type, show_uploader, "
                    "check_dupes, free_files, flags FROM file_areas WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, area_id);
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    out->id = sqlite3_column_int(st, 0);
    safe_copy(out->name, sizeof(out->name), (const char *)sqlite3_column_text(st, 1));
    safe_copy(out->path, sizeof(out->path), (const char *)sqlite3_column_text(st, 2));
    safe_copy(out->acs_list, sizeof(out->acs_list), (const char *)sqlite3_column_text(st, 3));
    safe_copy(out->acs_download, sizeof(out->acs_download), (const char *)sqlite3_column_text(st, 4));
    safe_copy(out->acs_upload, sizeof(out->acs_upload), (const char *)sqlite3_column_text(st, 5));
    safe_copy(out->acs_sysop, sizeof(out->acs_sysop), (const char *)sqlite3_column_text(st, 6));
    safe_copy(out->password, sizeof(out->password), (const char *)sqlite3_column_text(st, 7));
    out->max_files = sqlite3_column_int(st, 8);
    safe_copy(out->archive_type, sizeof(out->archive_type), (const char *)sqlite3_column_text(st, 9));
    out->sort_type = sqlite3_column_int(st, 10);
    out->show_uploader = sqlite3_column_int(st, 11);
    out->check_dupes = sqlite3_column_int(st, 12);
    out->free_files = sqlite3_column_int(st, 13);
    out->flags = sqlite3_column_int(st, 14);
    sqlite3_finalize(st);
    return true;
  }
  sqlite3_finalize(st);
  return false;
#else
  (void)area_id;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_messages_to_user(BbsDb *db, int user_id, DbMessage *out, int max_msgs)
{
  if (!db || !out || max_msgs <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql =
      "SELECT m.id, m.area_id, m.user_id, COALESCE(u.handle,''), m.subject, m.body, m.reply_to, m.created_at "
      "FROM messages m LEFT JOIN users u ON u.id = m.user_id "
      "WHERE m.to_user = ?1 ORDER BY m.created_at DESC";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, user_id);
  int count = 0;
  while (count < max_msgs && sqlite3_step(st) == SQLITE_ROW)
  {
    DbMessage *m = &out[count];
    memset(m, 0, sizeof(*m));
    m->id = sqlite3_column_int(st, 0);
    m->area_id = sqlite3_column_int(st, 1);
    m->user_id = sqlite3_column_int(st, 2);
    safe_copy(m->user_handle, sizeof(m->user_handle), (const char *)sqlite3_column_text(st, 3));
    safe_copy(m->subject, sizeof(m->subject), (const char *)sqlite3_column_text(st, 4));
    safe_copy(m->body, sizeof(m->body), (const char *)sqlite3_column_text(st, 5));
    m->reply_to = sqlite3_column_int(st, 6);
    safe_copy(m->created_at, sizeof(m->created_at), (const char *)sqlite3_column_text(st, 7));
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)user_id;
  (void)out;
  (void)max_msgs;
  return 0;
#endif
}

bool db_user_set_smw(BbsDb *db, int user_id, int smw)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE users SET smw = ?1 WHERE id = ?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, smw);
  sqlite3_bind_int(st, 2, user_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)user_id;
  (void)smw;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_message_reply_tree(BbsDb *db, int area_id, int root_id, DbMessage *out, int max_msgs, int *out_count)
{
  if (!db || !out || max_msgs <= 0)
    return false;
#ifdef HAVE_SQLITE
  const char *sql =
      "SELECT m.id, m.user_id, COALESCE(u.handle,''), m.subject, m.body, m.posted_at, "
      "m.reply_to, m.thread_root, m.to_user, m.area_id "
      "FROM messages m LEFT JOIN users u ON u.id = m.user_id "
      "WHERE m.area_id = ?1 AND (m.thread_root = ?2 OR m.id = ?2) "
      "ORDER BY m.id ASC";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, area_id);
  sqlite3_bind_int(st, 2, root_id);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max_msgs)
  {
    out[count].id = sqlite3_column_int(st, 0);
    out[count].user_id = sqlite3_column_int(st, 1);
    safe_copy(out[count].user_handle, sizeof(out[count].user_handle), (const char *)sqlite3_column_text(st, 2));
    safe_copy(out[count].subject, sizeof(out[count].subject), (const char *)sqlite3_column_text(st, 3));
    safe_copy(out[count].body, sizeof(out[count].body), (const char *)sqlite3_column_text(st, 4));
    safe_copy(out[count].created_at, sizeof(out[count].created_at), (const char *)sqlite3_column_text(st, 5));
    out[count].reply_to = sqlite3_column_int(st, 6);
    out[count].thread_root = sqlite3_column_int(st, 7);
    out[count].to_user = sqlite3_column_int(st, 8);
    out[count].area_id = sqlite3_column_int(st, 9);
    count++;
  }
  sqlite3_finalize(st);
  if (out_count)
    *out_count = count;
  return true;
#else
  (void)area_id;
  (void)root_id;
  (void)out;
  (void)max_msgs;
  (void)out_count;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_message_area_manage(BbsDb *db, const char *name, const char *acs, int *out_id, bool delete_flag)
{
  if (!db || !name)
    return false;
#ifdef HAVE_SQLITE
  if (delete_flag)
  {
    const char *sql = "DELETE FROM message_areas WHERE name = ?1";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      set_err(db, sqlite3_errmsg(db->db));
      return false;
    }
    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
  }
  const char *sql = "INSERT INTO message_areas (name, acs) VALUES (?1, ?2) "
                    "ON CONFLICT(name) DO UPDATE SET acs = excluded.acs";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, acs ? acs : "", -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE)
  {
    set_err(db, "area manage failed");
    return false;
  }
  if (out_id)
  {
    const char *sqlid = "SELECT id FROM message_areas WHERE name = ?1";
    sqlite3_stmt *st2 = NULL;
    if (sqlite3_prepare_v2(db->db, sqlid, -1, &st2, NULL) == SQLITE_OK)
    {
      sqlite3_bind_text(st2, 1, name, -1, SQLITE_TRANSIENT);
      if (sqlite3_step(st2) == SQLITE_ROW)
        *out_id = sqlite3_column_int(st2, 0);
      sqlite3_finalize(st2);
    }
  }
  return true;
#else
  (void)name;
  (void)acs;
  (void)out_id;
  (void)delete_flag;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_message_set_to_user(BbsDb *db, int msg_id, int to_user)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE messages SET to_user = ?1 WHERE id = ?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, to_user);
  sqlite3_bind_int(st, 2, msg_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc == SQLITE_DONE)
  {
    sqlite3_stmt *st2 = NULL;
    const char *sql2 = "UPDATE users SET smw = smw + 1 WHERE id = ?1";
    if (sqlite3_prepare_v2(db->db, sql2, -1, &st2, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int(st2, 1, to_user);
      sqlite3_step(st2);
      sqlite3_finalize(st2);
    }
    return true;
  }
  return false;
#else
  (void)msg_id;
  (void)to_user;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_file_area_manage(BbsDb *db, const char *name, const char *path, const char *acs, int *out_id, bool delete_flag)
{
  if (!db || !name)
    return false;
#ifdef HAVE_SQLITE
  if (delete_flag)
  {
    const char *sql = "DELETE FROM file_areas WHERE name = ?1";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      set_err(db, sqlite3_errmsg(db->db));
      return false;
    }
    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
  }
  const char *sql = "INSERT INTO file_areas (name, path, acs_list) VALUES (?1, ?2, ?3) "
                    "ON CONFLICT(name) DO UPDATE SET path = excluded.path, acs_list = excluded.acs_list";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, path ? path : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, acs ? acs : "", -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc != SQLITE_DONE)
  {
    set_err(db, "file area manage failed");
    return false;
  }
  if (out_id)
  {
    const char *sqlid = "SELECT id FROM file_areas WHERE name = ?1";
    sqlite3_stmt *st2 = NULL;
    if (sqlite3_prepare_v2(db->db, sqlid, -1, &st2, NULL) == SQLITE_OK)
    {
      sqlite3_bind_text(st2, 1, name, -1, SQLITE_TRANSIENT);
      if (sqlite3_step(st2) == SQLITE_ROW)
        *out_id = sqlite3_column_int(st2, 0);
      sqlite3_finalize(st2);
    }
  }
  return true;
#else
  (void)name;
  (void)path;
  (void)acs;
  (void)out_id;
  (void)delete_flag;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_file_get(BbsDb *db, int file_id, DbFileRec *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT f.id, f.area_id, f.filename, COALESCE(f.description,''), "
                    "COALESCE(f.extended_desc,''), COALESCE(f.file_id_diz,''), f.size_bytes, f.uploaded_at, "
                    "f.uploaded_by, COALESCE(u.handle,''), COALESCE(f.sha256,''), f.file_points, "
                    "f.download_count, f.owner_credit, f.flags "
                    "FROM files f LEFT JOIN users u ON u.id = f.uploaded_by WHERE f.id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, file_id);
  int rc = sqlite3_step(st);
  if (rc == SQLITE_ROW)
  {
    out->id = sqlite3_column_int(st, 0);
    out->area_id = sqlite3_column_int(st, 1);
    safe_copy(out->filename, sizeof(out->filename), (const char *)sqlite3_column_text(st, 2));
    safe_copy(out->desc, sizeof(out->desc), (const char *)sqlite3_column_text(st, 3));
    safe_copy(out->extended_desc, sizeof(out->extended_desc), (const char *)sqlite3_column_text(st, 4));
    safe_copy(out->file_id_diz, sizeof(out->file_id_diz), (const char *)sqlite3_column_text(st, 5));
    out->size_bytes = sqlite3_column_int(st, 6);
    safe_copy(out->uploaded_at, sizeof(out->uploaded_at), (const char *)sqlite3_column_text(st, 7));
    out->uploaded_by = sqlite3_column_int(st, 8);
    safe_copy(out->uploader, sizeof(out->uploader), (const char *)sqlite3_column_text(st, 9));
    safe_copy(out->sha256, sizeof(out->sha256), (const char *)sqlite3_column_text(st, 10));
    out->file_points = sqlite3_column_int(st, 11);
    out->download_count = sqlite3_column_int(st, 12);
    out->owner_credit = sqlite3_column_int(st, 13);
    out->flags = sqlite3_column_int(st, 14);
    sqlite3_finalize(st);
    return true;
  }
  sqlite3_finalize(st);
  set_err(db, "file not found");
  return false;
#else
  (void)file_id;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_file_mark_hash(BbsDb *db, int file_id, const char *sha256)
{
  if (!db || !sha256)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE files SET sha256 = ?1 WHERE id = ?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, sha256, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, file_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)file_id;
  (void)sha256;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_file_exists_hash(BbsDb *db, const char *sha256, int *out_file_id)
{
  if (!db || !sha256)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id FROM files WHERE sha256 = ?1 LIMIT 1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, sha256, -1, SQLITE_TRANSIENT);
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    if (out_file_id)
      *out_file_id = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return true;
  }
  sqlite3_finalize(st);
  return false;
#else
  (void)sha256;
  (void)out_file_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_file_inc_downloads(BbsDb *db, int file_id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE files SET download_count = download_count + 1 WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, file_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)file_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_bulletin_list(BbsDb *db, DbBulletin *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT b.id, b.title, b.body, b.posted_at, COALESCE(u.handle,''), COALESCE(b.acs,'') "
                    "FROM bulletins b LEFT JOIN users u ON u.id = b.posted_by ORDER BY b.id DESC LIMIT ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, max);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    safe_copy(out[count].title, sizeof(out[count].title), (const char *)sqlite3_column_text(st, 1));
    safe_copy(out[count].body, sizeof(out[count].body), (const char *)sqlite3_column_text(st, 2));
    safe_copy(out[count].posted_at, sizeof(out[count].posted_at), (const char *)sqlite3_column_text(st, 3));
    safe_copy(out[count].posted_by, sizeof(out[count].posted_by), (const char *)sqlite3_column_text(st, 4));
    safe_copy(out[count].acs, sizeof(out[count].acs), (const char *)sqlite3_column_text(st, 5));
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_bulletin_add(BbsDb *db, const char *title, const char *body, int user_id, const char *acs)
{
  if (!db || !title || !body)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO bulletins (title, body, posted_by, acs) VALUES (?1, ?2, ?3, ?4)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, title, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, body, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 3, user_id);
  sqlite3_bind_text(st, 4, acs ? acs : "", -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)title;
  (void)body;
  (void)user_id;
  (void)acs;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_automsg_get(BbsDb *db, DbAutomsg *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT a.msg, COALESCE(u.handle,''), a.set_at FROM automsg a LEFT JOIN users u ON u.id = a.set_by WHERE a.id = 1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  int rc = sqlite3_step(st);
  if (rc == SQLITE_ROW)
  {
    safe_copy(out->msg, sizeof(out->msg), (const char *)sqlite3_column_text(st, 0));
    safe_copy(out->set_by, sizeof(out->set_by), (const char *)sqlite3_column_text(st, 1));
    safe_copy(out->set_at, sizeof(out->set_at), (const char *)sqlite3_column_text(st, 2));
    sqlite3_finalize(st);
    return true;
  }
  sqlite3_finalize(st);
  set_err(db, "automsg not set");
  return false;
#else
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_automsg_set(BbsDb *db, const char *msg, int user_id)
{
  if (!db || !msg)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO automsg (id, msg, set_by) VALUES (1, ?1, ?2) "
                    "ON CONFLICT(id) DO UPDATE SET msg = excluded.msg, set_by = excluded.set_by, set_at = datetime('now')";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, msg, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, user_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)msg;
  (void)user_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_oneliner_list(BbsDb *db, DbOneliner *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, user_id, user_handle, text, posted_at FROM oneliners "
                    "ORDER BY posted_at DESC LIMIT ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, max);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    out[count].user_id = sqlite3_column_int(st, 1);
    safe_copy(out[count].user_handle, sizeof(out[count].user_handle), (const char *)sqlite3_column_text(st, 2));
    safe_copy(out[count].text, sizeof(out[count].text), (const char *)sqlite3_column_text(st, 3));
    safe_copy(out[count].posted_at, sizeof(out[count].posted_at), (const char *)sqlite3_column_text(st, 4));
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_oneliner_add(BbsDb *db, int user_id, const char *handle, const char *text)
{
  if (!db || !handle || !text)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO oneliners (user_id, user_handle, text) VALUES (?1, ?2, ?3)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, user_id);
  sqlite3_bind_text(st, 2, handle, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, text, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)user_id;
  (void)handle;
  (void)text;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_oneliner_delete(BbsDb *db, int id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "DELETE FROM oneliners WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_oneliner_count(BbsDb *db)
{
  if (!db)
    return -1;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT COUNT(*) FROM oneliners";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  int count = 0;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    count = sqlite3_column_int(st, 0);
  }
  sqlite3_finalize(st);
  return count;
#else
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

/* Short Messages (SMW) */
bool db_smw_send(BbsDb *db, int from_user, const char *from_handle, int to_user, const char *to_handle, const char *message)
{
  if (!db || !from_handle || !to_handle || !message)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO short_messages (from_user, from_handle, to_user, to_handle, message) VALUES (?1, ?2, ?3, ?4, ?5)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, from_user);
  sqlite3_bind_text(st, 2, from_handle, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 3, to_user);
  sqlite3_bind_text(st, 4, to_handle, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, message, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc == SQLITE_DONE)
  {
    /* Update the user's smw count */
    const char *upd = "UPDATE users SET smw = smw + 1 WHERE id = ?1";
    sqlite3_stmt *st2 = NULL;
    if (sqlite3_prepare_v2(db->db, upd, -1, &st2, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int(st2, 1, to_user);
      sqlite3_step(st2);
      sqlite3_finalize(st2);
    }
    return true;
  }
  return false;
#else
  (void)from_user;
  (void)from_handle;
  (void)to_user;
  (void)to_handle;
  (void)message;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_smw_list(BbsDb *db, int user_id, DbShortMessage *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, from_user, to_user, from_handle, to_handle, message, sent_at, read_flag "
                    "FROM short_messages WHERE to_user = ?1 ORDER BY sent_at DESC LIMIT ?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, user_id);
  sqlite3_bind_int(st, 2, max);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    out[count].from_user = sqlite3_column_int(st, 1);
    out[count].to_user = sqlite3_column_int(st, 2);
    safe_copy(out[count].from_handle, sizeof(out[count].from_handle), (const char *)sqlite3_column_text(st, 3));
    safe_copy(out[count].to_handle, sizeof(out[count].to_handle), (const char *)sqlite3_column_text(st, 4));
    safe_copy(out[count].message, sizeof(out[count].message), (const char *)sqlite3_column_text(st, 5));
    safe_copy(out[count].sent_at, sizeof(out[count].sent_at), (const char *)sqlite3_column_text(st, 6));
    out[count].read_flag = sqlite3_column_int(st, 7);
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)user_id;
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

int db_smw_count(BbsDb *db, int user_id)
{
  if (!db)
    return -1;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT COUNT(*) FROM short_messages WHERE to_user = ?1 AND read_flag = 0";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, user_id);
  int count = 0;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    count = sqlite3_column_int(st, 0);
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)user_id;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_smw_mark_read(BbsDb *db, int msg_id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE short_messages SET read_flag = 1 WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, msg_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)msg_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_smw_delete(BbsDb *db, int msg_id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "DELETE FROM short_messages WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, msg_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)msg_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_stats_get(BbsDb *db, DbStats *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *init_sql = "INSERT INTO stats (id) VALUES (1) ON CONFLICT(id) DO NOTHING";
  db_exec(db, init_sql);
  const char *sql = "SELECT calls, uploads, downloads, posts, emails FROM stats WHERE id = 1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    out->calls = sqlite3_column_int(st, 0);
    out->uploads = sqlite3_column_int(st, 1);
    out->downloads = sqlite3_column_int(st, 2);
    out->posts = sqlite3_column_int(st, 3);
    out->emails = sqlite3_column_int(st, 4);
    sqlite3_finalize(st);
    return true;
  }
  sqlite3_finalize(st);
  set_err(db, "stats missing");
  return false;
#else
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_stats_inc(BbsDb *db, const char *field)
{
  if (!db || !field)
    return false;
#ifdef HAVE_SQLITE
  const char *allowed[] = {"calls", "uploads", "downloads", "posts", "emails"};
  bool ok = false;
  for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++)
    if (!strcmp(field, allowed[i]))
      ok = true;
  if (!ok)
    return false;
  char sql[128];
  snprintf(sql, sizeof(sql), "UPDATE stats SET %s = %s + 1 WHERE id = 1", field, field);
  return db_exec(db, sql);
#else
  (void)field;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_call_log_start(BbsDb *db, int user_id, const char *handle, int node_num, const char *ip)
{
  if (!db || !handle)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO call_history (user_id, handle, node_num, ip_address) VALUES (?1, ?2, ?3, ?4)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, user_id);
  sqlite3_bind_text(st, 2, handle, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 3, node_num);
  sqlite3_bind_text(st, 4, ip ? ip : "", -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc == SQLITE_DONE)
  {
    return (int)sqlite3_last_insert_rowid(db->db);
  }
  return 0;
#else
  (void)user_id;
  (void)handle;
  (void)node_num;
  (void)ip;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_call_log_end(BbsDb *db, int call_id, int duration_min)
{
  if (!db || call_id <= 0)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE call_history SET logout_at = datetime('now'), duration_min = ?2 WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, call_id);
  sqlite3_bind_int(st, 2, duration_min);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)call_id;
  (void)duration_min;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_call_history_list(BbsDb *db, DbCallHistory *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, user_id, handle, node_num, login_at, COALESCE(logout_at,''), "
                    "COALESCE(duration_min,0), COALESCE(ip_address,'') "
                    "FROM call_history ORDER BY login_at DESC LIMIT ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, max);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    out[count].user_id = sqlite3_column_int(st, 1);
    snprintf(out[count].handle, sizeof(out[count].handle), "%s", (const char *)sqlite3_column_text(st, 2));
    out[count].node_num = sqlite3_column_int(st, 3);
    snprintf(out[count].login_at, sizeof(out[count].login_at), "%s", (const char *)sqlite3_column_text(st, 4));
    snprintf(out[count].logout_at, sizeof(out[count].logout_at), "%s", (const char *)sqlite3_column_text(st, 5));
    out[count].duration_min = sqlite3_column_int(st, 6);
    snprintf(out[count].ip_address, sizeof(out[count].ip_address), "%s", (const char *)sqlite3_column_text(st, 7));
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

int db_vote_list(BbsDb *db, DbVote *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, title, COALESCE(closes_at,'') FROM votes ORDER BY id DESC LIMIT ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, max);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    safe_copy(out[count].title, sizeof(out[count].title), (const char *)sqlite3_column_text(st, 1));
    safe_copy(out[count].closes_at, sizeof(out[count].closes_at), (const char *)sqlite3_column_text(st, 2));
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_vote_cast(BbsDb *db, int vote_id, int choice_id, int user_id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO vote_ballots (vote_id, user_id, choice_id) VALUES (?1, ?2, ?3) "
                    "ON CONFLICT(vote_id, user_id) DO UPDATE SET choice_id = excluded.choice_id, cast_at = datetime('now')";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, vote_id);
  sqlite3_bind_int(st, 2, user_id);
  sqlite3_bind_int(st, 3, choice_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)vote_id;
  (void)choice_id;
  (void)user_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_vote_choices(BbsDb *db, int vote_id, DbVoteChoice *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, vote_id, label FROM vote_choices WHERE vote_id = ?1 ORDER BY id ASC";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, vote_id);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    out[count].vote_id = sqlite3_column_int(st, 1);
    safe_copy(out[count].label, sizeof(out[count].label), (const char *)sqlite3_column_text(st, 2));
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)vote_id;
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

int db_vote_results(BbsDb *db, int vote_id, int *choice_ids, int *counts, int max)
{
  if (!db || !choice_ids || !counts || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT choice_id, COUNT(*) as cnt FROM vote_ballots "
                    "WHERE vote_id = ?1 GROUP BY choice_id ORDER BY cnt DESC";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, vote_id);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    choice_ids[count] = sqlite3_column_int(st, 0);
    counts[count] = sqlite3_column_int(st, 1);
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)vote_id;
  (void)choice_ids;
  (void)counts;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

int db_vote_total(BbsDb *db, int vote_id)
{
  if (!db)
    return -1;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT COUNT(*) FROM vote_ballots WHERE vote_id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, vote_id);
  int total = 0;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    total = sqlite3_column_int(st, 0);
  }
  sqlite3_finalize(st);
  return total;
#else
  (void)vote_id;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_vote_add(BbsDb *db, const char *title, const char *closes_at)
{
  if (!db || !title) return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO votes (title, closes_at) VALUES (?1, ?2)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK) return false;
  sqlite3_bind_text(st, 1, title, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, closes_at ? closes_at : "", -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)title; (void)closes_at; return false;
#endif
}

bool db_vote_delete(BbsDb *db, int vote_id)
{
  if (!db) return false;
#ifdef HAVE_SQLITE
  db_exec(db, "DELETE FROM vote_choices WHERE vote_id = ?1");
  /* Re-prepare with binding since db_exec doesn't bind */
  const char *sql1 = "DELETE FROM vote_choices WHERE vote_id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql1, -1, &st, NULL) == SQLITE_OK) {
    sqlite3_bind_int(st, 1, vote_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
  const char *sql2 = "DELETE FROM votes WHERE id = ?1";
  if (sqlite3_prepare_v2(db->db, sql2, -1, &st, NULL) != SQLITE_OK) return false;
  sqlite3_bind_int(st, 1, vote_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)vote_id; return false;
#endif
}

bool db_vote_choice_add(BbsDb *db, int vote_id, const char *label)
{
  if (!db || !label) return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO vote_choices (vote_id, label) VALUES (?1, ?2)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK) return false;
  sqlite3_bind_int(st, 1, vote_id);
  sqlite3_bind_text(st, 2, label, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)vote_id; (void)label; return false;
#endif
}

bool db_event_add(BbsDb *db, const char *name, const char *schedule,
                  const char *command, const char *event_type, const char *acs)
{
  if (!db || !name || !schedule || !command) return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO events (name, schedule, command, event_type, acs, enabled) "
                    "VALUES (?1, ?2, ?3, COALESCE(?4,'scheduled'), COALESCE(?5,''), 1)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK) return false;
  sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, schedule, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, command, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, event_type, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, acs, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)name; (void)schedule; (void)command;
  (void)event_type; (void)acs; return false;
#endif
}

bool db_event_delete(BbsDb *db, int event_id)
{
  if (!db) return false;
#ifdef HAVE_SQLITE
  const char *sql = "DELETE FROM events WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK) return false;
  sqlite3_bind_int(st, 1, event_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)event_id; return false;
#endif
}

bool db_event_toggle(BbsDb *db, int event_id, int enabled)
{
  if (!db) return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE events SET enabled = ?1 WHERE id = ?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK) return false;
  sqlite3_bind_int(st, 1, enabled);
  sqlite3_bind_int(st, 2, event_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)event_id; (void)enabled; return false;
#endif
}

static void db_door_fill(DbDoor *d, sqlite3_stmt *st)
{
  d->id = sqlite3_column_int(st, 0);
  safe_copy(d->name,     sizeof(d->name),     (const char *)sqlite3_column_text(st, 1));
  safe_copy(d->dropfile, sizeof(d->dropfile), (const char *)sqlite3_column_text(st, 2));
  safe_copy(d->command,  sizeof(d->command),  (const char *)sqlite3_column_text(st, 3));
  safe_copy(d->workdir,  sizeof(d->workdir),  (const char *)sqlite3_column_text(st, 4));
  safe_copy(d->acs,      sizeof(d->acs),      (const char *)sqlite3_column_text(st, 5));
  safe_copy(d->runner,   sizeof(d->runner),   (const char *)sqlite3_column_text(st, 6));
  safe_copy(d->manifest, sizeof(d->manifest), (const char *)sqlite3_column_text(st, 7));
  d->enabled     = sqlite3_column_int(st, 8);
  d->timeout_sec = sqlite3_column_int(st, 9);
}

#define DOOR_SQL_COLS \
  "id, name, dropfile, command, COALESCE(workdir,''), COALESCE(acs,''), " \
  "COALESCE(runner,'native'), COALESCE(manifest,''), " \
  "COALESCE(enabled,1), COALESCE(timeout_sec,0)"

int db_doors_list(BbsDb *db, DbDoor *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT " DOOR_SQL_COLS " FROM doors ORDER BY id ASC LIMIT ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, max);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    memset(&out[count], 0, sizeof(out[count]));
    db_door_fill(&out[count], st);
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_door_get(BbsDb *db, int door_id, DbDoor *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT " DOOR_SQL_COLS " FROM doors WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, door_id);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    memset(out, 0, sizeof(*out));
    db_door_fill(out, st);
    found = true;
  }
  sqlite3_finalize(st);
  return found;
#else
  (void)door_id; (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_door_add(BbsDb *db, const char *name, const char *dropfile, const char *cmd, const char *workdir, const char *acs)
{
  if (!db || !name || !dropfile || !cmd)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO doors (name, dropfile, command, workdir, acs) VALUES (?1, ?2, ?3, ?4, ?5) "
                    "ON CONFLICT(name) DO UPDATE SET dropfile=excluded.dropfile, command=excluded.command, workdir=excluded.workdir, acs=excluded.acs";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, dropfile, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, cmd, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, workdir ? workdir : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, acs ? acs : "", -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)name;
  (void)dropfile;
  (void)cmd;
  (void)workdir;
  (void)acs;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_protocols_list(BbsDb *db, DbProtocol *out, int max, const char *direction)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = direction && direction[0]
                        ? "SELECT id, name, direction, command, active FROM protocols WHERE active = 1 AND direction IN ('both', ?1) ORDER BY id ASC"
                        : "SELECT id, name, direction, command, active FROM protocols ORDER BY id ASC";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  if (direction && direction[0])
    sqlite3_bind_text(st, 1, direction, -1, SQLITE_TRANSIENT);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    safe_copy(out[count].name, sizeof(out[count].name), (const char *)sqlite3_column_text(st, 1));
    safe_copy(out[count].direction, sizeof(out[count].direction), (const char *)sqlite3_column_text(st, 2));
    safe_copy(out[count].command, sizeof(out[count].command), (const char *)sqlite3_column_text(st, 3));
    out[count].active = sqlite3_column_int(st, 4);
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max;
  (void)direction;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_protocol_add(BbsDb *db, const char *name, const char *direction, const char *command)
{
  if (!db || !name || !direction || !command)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO protocols (name, direction, command) VALUES (?1, ?2, ?3)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, direction, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, command, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)name;
  (void)direction;
  (void)command;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_protocol_update(BbsDb *db, int id, const char *name, const char *direction, const char *command, int active)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE protocols SET name = ?2, direction = ?3, command = ?4, active = ?5 WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  sqlite3_bind_text(st, 2, name ? name : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, direction ? direction : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, command ? command : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 5, active);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  (void)name;
  (void)direction;
  (void)command;
  (void)active;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_protocol_delete(BbsDb *db, int id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "DELETE FROM protocols WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_protocol_get(BbsDb *db, int id, DbProtocol *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, name, direction, command, active FROM protocols WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    out->id = sqlite3_column_int(st, 0);
    safe_copy(out->name, sizeof(out->name), (const char *)sqlite3_column_text(st, 1));
    safe_copy(out->direction, sizeof(out->direction), (const char *)sqlite3_column_text(st, 2));
    safe_copy(out->command, sizeof(out->command), (const char *)sqlite3_column_text(st, 3));
    out->active = sqlite3_column_int(st, 4);
    found = true;
  }
  sqlite3_finalize(st);
  return found;
#else
  (void)id;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_user_update_flags(BbsDb *db, int user_id, unsigned flags, unsigned ac_flags, unsigned status_flags)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE users SET flags = ?1, ac_flags = ?2, status_flags = ?3 WHERE id = ?4";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, (int)flags);
  sqlite3_bind_int(st, 2, (int)ac_flags);
  sqlite3_bind_int(st, 3, (int)status_flags);
  sqlite3_bind_int(st, 4, user_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)user_id;
  (void)flags;
  (void)ac_flags;
  (void)status_flags;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_user_update_level(BbsDb *db, int user_id, int security_level_id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE users SET security_level_id = ?1 WHERE id = ?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, security_level_id);
  sqlite3_bind_int(st, 2, user_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)user_id;
  (void)security_level_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_user_update_time_credit(BbsDb *db, int user_id, int time_min, int credits, int file_points)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE users SET credits = ?1, file_points = ?2 WHERE id = ?3";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, credits);
  sqlite3_bind_int(st, 2, file_points);
  sqlite3_bind_int(st, 3, user_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (time_min > 0)
  {
    const char *sql2 = "UPDATE users SET time_limit_min = ?1 WHERE id = ?2";
    sqlite3_stmt *st2 = NULL;
    if (sqlite3_prepare_v2(db->db, sql2, -1, &st2, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int(st2, 1, time_min);
      sqlite3_bind_int(st2, 2, user_id);
      sqlite3_step(st2);
      sqlite3_finalize(st2);
    }
  }
  return rc == SQLITE_DONE;
#else
  (void)user_id;
  (void)time_min;
  (void)credits;
  (void)file_points;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_user_update(BbsDb *db, const DbUser *u)
{
  if (!db || !u)
    return false;
#ifdef HAVE_SQLITE
  const char *sql =
      "UPDATE users SET real_name=?1, email=?2, phone=?3, street=?4, city_state=?5, "
      "zip_code=?6, caller_id=?7, forgot_pw_answer=?8, sex=?9, birth_date=?10, "
      "security_level_id=?11, dsl=?12, flags=?13, ac_flags=?14, status_flags=?15, "
      "credits=?16, file_points=?17, on_today=?18, illegal=?19, def_arc_type=?20, "
      "color_scheme=?21, user_start_menu=?22, t_time_on=?23, uploads=?24, downloads=?25, "
      "uk=?26, dk=?27, logged_on=?28, msg_post=?29, email_sent=?30, feedback=?31, "
      "timebank=?32, timebank_add=?33, dl_k_today=?34, dl_today=?35, usr_def_str1=?36, "
      "usr_def_str2=?37, usr_def_str3=?38, note=?39, locked_file=?40, expires_at=?41, smw=?42 "
      "WHERE id=?43";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  int p = 1;
  sqlite3_bind_text(st, p++, u->real_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, u->email, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, u->phone, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, u->street, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, u->city_state, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, u->zip_code, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, u->caller_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, u->forgot_pw_answer, -1, SQLITE_TRANSIENT);
  char sex_str[2] = {u->sex, '\0'};
  sqlite3_bind_text(st, p++, sex_str, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, u->birth_date, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, p++, u->security_level_id);
  sqlite3_bind_int(st, p++, u->dsl);
  sqlite3_bind_int(st, p++, (int)u->flags);
  sqlite3_bind_int(st, p++, (int)u->ac_flags);
  sqlite3_bind_int(st, p++, (int)u->status_flags);
  sqlite3_bind_int(st, p++, u->credits);
  sqlite3_bind_int(st, p++, u->file_points);
  sqlite3_bind_int(st, p++, u->on_today);
  sqlite3_bind_int(st, p++, u->illegal);
  sqlite3_bind_int(st, p++, u->def_arc_type);
  sqlite3_bind_int(st, p++, u->color_scheme);
  sqlite3_bind_int(st, p++, u->user_start_menu);
  sqlite3_bind_int(st, p++, u->t_time_on);
  sqlite3_bind_int(st, p++, u->uploads);
  sqlite3_bind_int(st, p++, u->downloads);
  sqlite3_bind_int(st, p++, u->uk);
  sqlite3_bind_int(st, p++, u->dk);
  sqlite3_bind_int(st, p++, u->logged_on);
  sqlite3_bind_int(st, p++, u->msg_post);
  sqlite3_bind_int(st, p++, u->email_sent);
  sqlite3_bind_int(st, p++, u->feedback);
  sqlite3_bind_int(st, p++, u->timebank);
  sqlite3_bind_int(st, p++, u->timebank_add);
  sqlite3_bind_int(st, p++, u->dl_k_today);
  sqlite3_bind_int(st, p++, u->dl_today);
  sqlite3_bind_text(st, p++, u->usr_def_str1, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, u->usr_def_str2, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, u->usr_def_str3, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, u->note, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, u->locked_file, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, p++, u->expires_at, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, p++, u->smw);
  sqlite3_bind_int(st, p++, u->id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)u;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_user_update_stats(BbsDb *db, int user_id, int uploads, int downloads, int uk, int dk,
                          int msg_post, int email_sent, int feedback, int logged_on)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE users SET uploads=?1, downloads=?2, uk=?3, dk=?4, "
                    "msg_post=?5, email_sent=?6, feedback=?7, logged_on=?8 WHERE id=?9";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, uploads);
  sqlite3_bind_int(st, 2, downloads);
  sqlite3_bind_int(st, 3, uk);
  sqlite3_bind_int(st, 4, dk);
  sqlite3_bind_int(st, 5, msg_post);
  sqlite3_bind_int(st, 6, email_sent);
  sqlite3_bind_int(st, 7, feedback);
  sqlite3_bind_int(st, 8, logged_on);
  sqlite3_bind_int(st, 9, user_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)user_id;
  (void)uploads;
  (void)downloads;
  (void)uk;
  (void)dk;
  (void)msg_post;
  (void)email_sent;
  (void)feedback;
  (void)logged_on;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_timebank_get(BbsDb *db, int user_id, int *minutes_out)
{
  if (!db || !minutes_out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT v FROM meta WHERE k = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  char key[32];
  snprintf(key, sizeof(key), "tb_%d", user_id);
  sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  if (rc == SQLITE_ROW)
  {
    const char *text = (const char *)sqlite3_column_text(st, 0);
    *minutes_out = text ? atoi(text) : 0;
  }
  else
  {
    *minutes_out = 0;
  }
  sqlite3_finalize(st);
  return true;
#else
  (void)user_id;
  (void)minutes_out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_timebank_add(BbsDb *db, int user_id, int delta_minutes, int *new_balance_out)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  int bal = 0;
  db_timebank_get(db, user_id, &bal);
  bal += delta_minutes;
  if (bal < 0)
    bal = 0;
  char key[32];
  snprintf(key, sizeof(key), "tb_%d", user_id);
  const char *sql = "INSERT INTO meta (k, v) VALUES (?1, ?2) ON CONFLICT(k) DO UPDATE SET v=excluded.v";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
  char v[16];
  snprintf(v, sizeof(v), "%d", bal);
  sqlite3_bind_text(st, 2, v, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc == SQLITE_DONE)
  {
    if (new_balance_out)
      *new_balance_out = bal;
    return true;
  }
  set_err(db, "timebank update failed");
  return false;
#else
  (void)user_id;
  (void)delta_minutes;
  (void)new_balance_out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_mail_packet_add(BbsDb *db, int user_id, const char *kind, const char *path)
{
  if (!db || !kind || !path)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO mail_packets (user_id, kind, path) VALUES (?1, ?2, ?3)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, user_id);
  sqlite3_bind_text(st, 2, kind, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, path, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)user_id;
  (void)kind;
  (void)path;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

/* Conference management functions */
int db_conference_list(BbsDb *db, DbConference *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, key, name, COALESCE(description,''), COALESCE(acs,''), flags FROM conferences ORDER BY id ASC LIMIT ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, max);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    snprintf(out[count].key, sizeof(out[count].key), "%s", (const char *)sqlite3_column_text(st, 1));
    snprintf(out[count].name, sizeof(out[count].name), "%s", (const char *)sqlite3_column_text(st, 2));
    snprintf(out[count].description, sizeof(out[count].description), "%s", (const char *)sqlite3_column_text(st, 3));
    snprintf(out[count].acs, sizeof(out[count].acs), "%s", (const char *)sqlite3_column_text(st, 4));
    out[count].flags = sqlite3_column_int(st, 5);
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_conference_get(BbsDb *db, int conf_id, DbConference *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, key, name, COALESCE(description,''), COALESCE(acs,''), flags FROM conferences WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, conf_id);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    out->id = sqlite3_column_int(st, 0);
    snprintf(out->key, sizeof(out->key), "%s", (const char *)sqlite3_column_text(st, 1));
    snprintf(out->name, sizeof(out->name), "%s", (const char *)sqlite3_column_text(st, 2));
    snprintf(out->description, sizeof(out->description), "%s", (const char *)sqlite3_column_text(st, 3));
    snprintf(out->acs, sizeof(out->acs), "%s", (const char *)sqlite3_column_text(st, 4));
    out->flags = sqlite3_column_int(st, 5);
    found = true;
  }
  sqlite3_finalize(st);
  return found;
#else
  (void)conf_id;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_conference_add(BbsDb *db, const char *key, const char *name, const char *desc, const char *acs)
{
  if (!db || !key || !name)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO conferences (key, name, description, acs) VALUES (?1, ?2, ?3, ?4)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, desc ? desc : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, acs ? acs : "", -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)key;
  (void)name;
  (void)desc;
  (void)acs;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_conference_update(BbsDb *db, int conf_id, const char *name, const char *desc, const char *acs, int flags)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE conferences SET name = ?2, description = ?3, acs = ?4, flags = ?5 WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, conf_id);
  sqlite3_bind_text(st, 2, name ? name : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, desc ? desc : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, acs ? acs : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 5, flags);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)conf_id;
  (void)name;
  (void)desc;
  (void)acs;
  (void)flags;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_conference_delete(BbsDb *db, int conf_id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "DELETE FROM conferences WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, conf_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  if (rc == SQLITE_DONE)
  {
    const char *sql2 = "DELETE FROM conference_membership WHERE conf_id = ?1";
    sqlite3_stmt *st2 = NULL;
    if (sqlite3_prepare_v2(db->db, sql2, -1, &st2, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int(st2, 1, conf_id);
      sqlite3_step(st2);
      sqlite3_finalize(st2);
    }
    return true;
  }
  return false;
#else
  (void)conf_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

/* Conference membership functions */
bool db_conf_is_member(BbsDb *db, int user_id, int conf_id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT 1 FROM conference_membership WHERE user_id = ?1 AND conf_id = ?2 LIMIT 1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, user_id);
  sqlite3_bind_int(st, 2, conf_id);
  bool found = (sqlite3_step(st) == SQLITE_ROW);
  sqlite3_finalize(st);
  return found;
#else
  (void)user_id;
  (void)conf_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_conf_join(BbsDb *db, int user_id, int conf_id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT OR IGNORE INTO conference_membership (user_id, conf_id) VALUES (?1, ?2)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, user_id);
  sqlite3_bind_int(st, 2, conf_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)user_id;
  (void)conf_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_conf_leave(BbsDb *db, int user_id, int conf_id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "DELETE FROM conference_membership WHERE user_id = ?1 AND conf_id = ?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, user_id);
  sqlite3_bind_int(st, 2, conf_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)user_id;
  (void)conf_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_conf_list_user(BbsDb *db, int user_id, int *conf_ids, int max)
{
  if (!db || !conf_ids || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT conf_id FROM conference_membership WHERE user_id = ?1 ORDER BY conf_id ASC LIMIT ?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, user_id);
  sqlite3_bind_int(st, 2, max);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    conf_ids[count++] = sqlite3_column_int(st, 0);
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)user_id;
  (void)conf_ids;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

int db_events_list(BbsDb *db, DbEvent *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, name, schedule, command, COALESCE(next_run,''), "
                    "COALESCE(event_type,'scheduled'), COALESCE(acs,''), "
                    "COALESCE(warning_min,0), COALESCE(enabled,1) FROM events";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    safe_copy(out[count].name, sizeof(out[count].name), (const char *)sqlite3_column_text(st, 1));
    safe_copy(out[count].schedule, sizeof(out[count].schedule), (const char *)sqlite3_column_text(st, 2));
    safe_copy(out[count].command, sizeof(out[count].command), (const char *)sqlite3_column_text(st, 3));
    safe_copy(out[count].next_run, sizeof(out[count].next_run), (const char *)sqlite3_column_text(st, 4));
    safe_copy(out[count].event_type, sizeof(out[count].event_type), (const char *)sqlite3_column_text(st, 5));
    safe_copy(out[count].acs, sizeof(out[count].acs), (const char *)sqlite3_column_text(st, 6));
    out[count].warning_min = sqlite3_column_int(st, 7);
    out[count].enabled = sqlite3_column_int(st, 8);
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_event_update_next(BbsDb *db, int id, const char *next_run)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE events SET next_run = ?1 WHERE id = ?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, next_run ? next_run : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  (void)next_run;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_event_mark_ran(BbsDb *db, int id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE events SET last_run=datetime('now'), next_run=NULL WHERE id=?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool file_store_copy(const char *src_path, const char *dst_path, int *size_bytes_out)
{
  /* ensure dest dir exists */
  char dirbuf[512];
  snprintf(dirbuf, sizeof(dirbuf), "%s", dst_path);
  char *slash = strrchr(dirbuf, '/');
  if (slash)
  {
    *slash = 0;
    mkdir(dirbuf, 0755);
  }
  return file_copy(src_path, dst_path, size_bytes_out);
}

bool file_area_resolve(const char *area_path, const char *filename, char *out, size_t out_cap)
{
  if (!area_path || !filename || !out)
    return false;
  /* prevent path traversal */
  if (strstr(filename, ".."))
    return false;
  path_join(area_path, filename, out, out_cap);
  return true;
}
bool db_seed_defaults(BbsDb *db, const char *sysop_pw_hash)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *seed_sql =
      "INSERT INTO security_levels (id,name,level,time_limit_min,download_ratio_num,download_ratio_den,post_ratio_num,post_ratio_den,flags) "
      "VALUES (1,'user',10,60,1,1,1,1,0) ON CONFLICT(id) DO NOTHING; "
      "INSERT INTO security_levels (id,name,level,time_limit_min,download_ratio_num,download_ratio_den,post_ratio_num,post_ratio_den,flags) "
      "VALUES (2,'sysop',90,120,1,1,1,1,1) ON CONFLICT(id) DO NOTHING;";
  if (!db_exec(db, seed_sql))
    return false;

  if (sysop_pw_hash && sysop_pw_hash[0])
  {
    const char *sql = "INSERT INTO users (handle, pw_hash, security_level_id) VALUES ('sysop', ?1, 2) "
                      "ON CONFLICT(handle) DO NOTHING";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      set_err(db, sqlite3_errmsg(db->db));
      return false;
    }
    sqlite3_bind_text(st, 1, sysop_pw_hash, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE && rc != SQLITE_CONSTRAINT)
    {
      set_err(db, "seed sysop failed");
      return false;
    }
  }

  /* Initialize system_info if not present */
  db_exec(db, "INSERT INTO system_info (id) VALUES (1) ON CONFLICT(id) DO NOTHING");

  return true;
#else
  (void)sysop_pw_hash;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_daily_stats_get(BbsDb *db, DbDailyStats *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  /* Ensure daily_stats row exists and check if date changed */
  const char *init_sql =
      "INSERT INTO daily_stats (id, date) VALUES (1, date('now')) "
      "ON CONFLICT(id) DO UPDATE SET "
      "  calls = CASE WHEN date != date('now') THEN 0 ELSE calls END, "
      "  posts = CASE WHEN date != date('now') THEN 0 ELSE posts END, "
      "  emails = CASE WHEN date != date('now') THEN 0 ELSE emails END, "
      "  newusers = CASE WHEN date != date('now') THEN 0 ELSE newusers END, "
      "  feedback = CASE WHEN date != date('now') THEN 0 ELSE feedback END, "
      "  uploads = CASE WHEN date != date('now') THEN 0 ELSE uploads END, "
      "  downloads = CASE WHEN date != date('now') THEN 0 ELSE downloads END, "
      "  ul_kb = CASE WHEN date != date('now') THEN 0 ELSE ul_kb END, "
      "  dl_kb = CASE WHEN date != date('now') THEN 0 ELSE dl_kb END, "
      "  minutes = CASE WHEN date != date('now') THEN 0 ELSE minutes END, "
      "  errors = CASE WHEN date != date('now') THEN 0 ELSE errors END, "
      "  date = date('now')";
  db_exec(db, init_sql);

  const char *sql = "SELECT date, calls, posts, emails, newusers, feedback, "
                    "uploads, downloads, ul_kb, dl_kb, minutes, errors FROM daily_stats WHERE id = 1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  memset(out, 0, sizeof(*out));
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    safe_copy(out->date, sizeof(out->date), (const char *)sqlite3_column_text(st, 0));
    out->calls = sqlite3_column_int(st, 1);
    out->posts = sqlite3_column_int(st, 2);
    out->emails = sqlite3_column_int(st, 3);
    out->newusers = sqlite3_column_int(st, 4);
    out->feedback = sqlite3_column_int(st, 5);
    out->uploads = sqlite3_column_int(st, 6);
    out->downloads = sqlite3_column_int(st, 7);
    out->ul_kb = sqlite3_column_int64(st, 8);
    out->dl_kb = sqlite3_column_int64(st, 9);
    out->minutes = sqlite3_column_int(st, 10);
    out->errors = sqlite3_column_int(st, 11);
    sqlite3_finalize(st);
    return true;
  }
  sqlite3_finalize(st);
  return false;
#else
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_daily_stats_inc(BbsDb *db, const char *field, int delta)
{
  if (!db || !field)
    return false;
#ifdef HAVE_SQLITE
  const char *allowed[] = {"calls", "posts", "emails", "newusers", "feedback",
                           "uploads", "downloads", "ul_kb", "dl_kb", "minutes", "errors"};
  bool ok = false;
  for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++)
  {
    if (!strcmp(field, allowed[i]))
    {
      ok = true;
      break;
    }
  }
  if (!ok)
    return false;

  /* Ensure row exists first */
  DbDailyStats tmp;
  db_daily_stats_get(db, &tmp);

  char sql[256];
  snprintf(sql, sizeof(sql), "UPDATE daily_stats SET %s = %s + %d WHERE id = 1", field, field, delta);
  return db_exec(db, sql);
#else
  (void)field;
  (void)delta;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_daily_stats_reset(BbsDb *db)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  return db_exec(db, "UPDATE daily_stats SET calls=0, posts=0, emails=0, newusers=0, "
                     "feedback=0, uploads=0, downloads=0, ul_kb=0, dl_kb=0, minutes=0, "
                     "errors=0, date=date('now') WHERE id=1");
#else
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_system_totals_get(BbsDb *db, DbSystemTotals *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  db_exec(db, "INSERT INTO system_info (id) VALUES (1) ON CONFLICT(id) DO NOTHING");

  const char *sql = "SELECT total_calls, total_posts, total_uploads, total_downloads, "
                    "total_usage, julianday('now') - julianday(first_online) + 1 "
                    "FROM system_info WHERE id = 1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  memset(out, 0, sizeof(*out));
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    out->total_calls = sqlite3_column_int(st, 0);
    out->total_posts = sqlite3_column_int(st, 1);
    out->total_uploads = sqlite3_column_int(st, 2);
    out->total_downloads = sqlite3_column_int(st, 3);
    out->total_usage = sqlite3_column_int(st, 4);
    out->days_online = sqlite3_column_int(st, 5);
    if (out->days_online < 1)
      out->days_online = 1;
    sqlite3_finalize(st);
    out->total_users = db_count_users(db);
    return true;
  }
  sqlite3_finalize(st);
  return false;
#else
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_system_totals_inc(BbsDb *db, const char *field, int delta)
{
  if (!db || !field)
    return false;
#ifdef HAVE_SQLITE
  const char *allowed[] = {"total_calls", "total_posts", "total_uploads", "total_downloads", "total_usage"};
  bool ok = false;
  for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++)
  {
    if (!strcmp(field, allowed[i]))
    {
      ok = true;
      break;
    }
  }
  if (!ok)
    return false;

  db_exec(db, "INSERT INTO system_info (id) VALUES (1) ON CONFLICT(id) DO NOTHING");

  char sql[256];
  snprintf(sql, sizeof(sql), "UPDATE system_info SET %s = %s + %d WHERE id = 1", field, field, delta);
  return db_exec(db, sql);
#else
  (void)field;
  (void)delta;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_history_record(BbsDb *db, const DbDailyStats *stats)
{
  if (!db || !stats)
    return false;
#ifdef HAVE_SQLITE
  const char *sql =
      "INSERT INTO history (date, calls, posts, emails, newusers, feedback, uploads, downloads, "
      "ul_kb, dl_kb, minutes, errors, active) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13) "
      "ON CONFLICT(date) DO UPDATE SET "
      "calls=excluded.calls, posts=excluded.posts, emails=excluded.emails, newusers=excluded.newusers, "
      "feedback=excluded.feedback, uploads=excluded.uploads, downloads=excluded.downloads, "
      "ul_kb=excluded.ul_kb, dl_kb=excluded.dl_kb, minutes=excluded.minutes, errors=excluded.errors, "
      "active=excluded.active";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, stats->date, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, stats->calls);
  sqlite3_bind_int(st, 3, stats->posts);
  sqlite3_bind_int(st, 4, stats->emails);
  sqlite3_bind_int(st, 5, stats->newusers);
  sqlite3_bind_int(st, 6, stats->feedback);
  sqlite3_bind_int(st, 7, stats->uploads);
  sqlite3_bind_int(st, 8, stats->downloads);
  sqlite3_bind_int64(st, 9, stats->ul_kb);
  sqlite3_bind_int64(st, 10, stats->dl_kb);
  sqlite3_bind_int(st, 11, stats->minutes);
  sqlite3_bind_int(st, 12, stats->errors);
  sqlite3_bind_int(st, 13, stats->minutes); /* active = minutes for now */
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)stats;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_history_list(BbsDb *db, DbDailyStats *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT date, calls, posts, emails, newusers, feedback, "
                    "uploads, downloads, ul_kb, dl_kb, minutes, errors "
                    "FROM history ORDER BY date DESC LIMIT ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, max);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    safe_copy(out[count].date, sizeof(out[count].date), (const char *)sqlite3_column_text(st, 0));
    out[count].calls = sqlite3_column_int(st, 1);
    out[count].posts = sqlite3_column_int(st, 2);
    out[count].emails = sqlite3_column_int(st, 3);
    out[count].newusers = sqlite3_column_int(st, 4);
    out[count].feedback = sqlite3_column_int(st, 5);
    out[count].uploads = sqlite3_column_int(st, 6);
    out[count].downloads = sqlite3_column_int(st, 7);
    out[count].ul_kb = sqlite3_column_int64(st, 8);
    out[count].dl_kb = sqlite3_column_int64(st, 9);
    out[count].minutes = sqlite3_column_int(st, 10);
    out[count].errors = sqlite3_column_int(st, 11);
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

int db_days_online(BbsDb *db)
{
  if (!db)
    return 1;
#ifdef HAVE_SQLITE
  db_exec(db, "INSERT INTO system_info (id) VALUES (1) ON CONFLICT(id) DO NOTHING");
  const char *sql = "SELECT MAX(1, julianday('now') - julianday(first_online) + 1) FROM system_info WHERE id = 1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    return 1;
  }
  int days = 1;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    days = sqlite3_column_int(st, 0);
    if (days < 1)
      days = 1;
  }
  sqlite3_finalize(st);
  return days;
#else
  return 1;
#endif
}

/* ========== FidoNet AKA Management ========== */

int db_fido_aka_list(BbsDb *db, DbFidoAka *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, zone, net, node, point, COALESCE(domain,''), is_primary "
                    "FROM fido_akas ORDER BY is_primary DESC, zone, net, node, point";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    out[count].zone = sqlite3_column_int(st, 1);
    out[count].net = sqlite3_column_int(st, 2);
    out[count].node = sqlite3_column_int(st, 3);
    out[count].point = sqlite3_column_int(st, 4);
    safe_copy(out[count].domain, sizeof(out[count].domain), (const char *)sqlite3_column_text(st, 5));
    out[count].is_primary = sqlite3_column_int(st, 6);
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_fido_aka_add(BbsDb *db, int zone, int net, int node, int point, const char *domain, int is_primary)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  if (is_primary)
  {
    db_exec(db, "UPDATE fido_akas SET is_primary = 0");
  }
  const char *sql = "INSERT INTO fido_akas (zone, net, node, point, domain, is_primary) VALUES (?1, ?2, ?3, ?4, ?5, ?6)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, zone);
  sqlite3_bind_int(st, 2, net);
  sqlite3_bind_int(st, 3, node);
  sqlite3_bind_int(st, 4, point);
  if (domain && domain[0])
  {
    sqlite3_bind_text(st, 5, domain, -1, SQLITE_TRANSIENT);
  }
  else
  {
    sqlite3_bind_null(st, 5);
  }
  sqlite3_bind_int(st, 6, is_primary);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)zone;
  (void)net;
  (void)node;
  (void)point;
  (void)domain;
  (void)is_primary;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_fido_aka_get(BbsDb *db, int id, DbFidoAka *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, zone, net, node, point, COALESCE(domain,''), is_primary "
                    "FROM fido_akas WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    out->id = sqlite3_column_int(st, 0);
    out->zone = sqlite3_column_int(st, 1);
    out->net = sqlite3_column_int(st, 2);
    out->node = sqlite3_column_int(st, 3);
    out->point = sqlite3_column_int(st, 4);
    safe_copy(out->domain, sizeof(out->domain), (const char *)sqlite3_column_text(st, 5));
    out->is_primary = sqlite3_column_int(st, 6);
    found = true;
  }
  sqlite3_finalize(st);
  return found;
#else
  (void)id;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_fido_aka_get_primary(BbsDb *db, DbFidoAka *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, zone, net, node, point, COALESCE(domain,''), is_primary "
                    "FROM fido_akas WHERE is_primary = 1 LIMIT 1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    out->id = sqlite3_column_int(st, 0);
    out->zone = sqlite3_column_int(st, 1);
    out->net = sqlite3_column_int(st, 2);
    out->node = sqlite3_column_int(st, 3);
    out->point = sqlite3_column_int(st, 4);
    safe_copy(out->domain, sizeof(out->domain), (const char *)sqlite3_column_text(st, 5));
    out->is_primary = sqlite3_column_int(st, 6);
    found = true;
  }
  sqlite3_finalize(st);
  return found;
#else
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_fido_aka_update(BbsDb *db, int id, int zone, int net, int node, int point, const char *domain, int is_primary)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  if (is_primary)
  {
    db_exec(db, "UPDATE fido_akas SET is_primary = 0");
  }
  const char *sql = "UPDATE fido_akas SET zone = ?2, net = ?3, node = ?4, point = ?5, domain = ?6, is_primary = ?7 WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  sqlite3_bind_int(st, 2, zone);
  sqlite3_bind_int(st, 3, net);
  sqlite3_bind_int(st, 4, node);
  sqlite3_bind_int(st, 5, point);
  if (domain && domain[0])
  {
    sqlite3_bind_text(st, 6, domain, -1, SQLITE_TRANSIENT);
  }
  else
  {
    sqlite3_bind_null(st, 6);
  }
  sqlite3_bind_int(st, 7, is_primary);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  (void)zone;
  (void)net;
  (void)node;
  (void)point;
  (void)domain;
  (void)is_primary;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_fido_aka_delete(BbsDb *db, int id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "DELETE FROM fido_akas WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

/* ========== FidoNet Echomail Links ========== */

int db_fido_echolink_list(BbsDb *db, DbFidoEcholink *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, area_id, echotag, aka_id, COALESCE(origin,''), high_water "
                    "FROM fido_echolinks ORDER BY echotag";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    out[count].area_id = sqlite3_column_int(st, 1);
    safe_copy(out[count].echotag, sizeof(out[count].echotag), (const char *)sqlite3_column_text(st, 2));
    out[count].aka_id = sqlite3_column_int(st, 3);
    safe_copy(out[count].origin, sizeof(out[count].origin), (const char *)sqlite3_column_text(st, 4));
    out[count].high_water = sqlite3_column_int(st, 5);
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_fido_echolink_add(BbsDb *db, int area_id, const char *echotag, int aka_id, const char *origin)
{
  if (!db || !echotag)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO fido_echolinks (area_id, echotag, aka_id, origin) VALUES (?1, ?2, ?3, ?4)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, area_id);
  sqlite3_bind_text(st, 2, echotag, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 3, aka_id);
  if (origin && origin[0])
  {
    sqlite3_bind_text(st, 4, origin, -1, SQLITE_TRANSIENT);
  }
  else
  {
    sqlite3_bind_null(st, 4);
  }
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)area_id;
  (void)echotag;
  (void)aka_id;
  (void)origin;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_fido_echolink_get(BbsDb *db, int id, DbFidoEcholink *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, area_id, echotag, aka_id, COALESCE(origin,''), high_water "
                    "FROM fido_echolinks WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    out->id = sqlite3_column_int(st, 0);
    out->area_id = sqlite3_column_int(st, 1);
    safe_copy(out->echotag, sizeof(out->echotag), (const char *)sqlite3_column_text(st, 2));
    out->aka_id = sqlite3_column_int(st, 3);
    safe_copy(out->origin, sizeof(out->origin), (const char *)sqlite3_column_text(st, 4));
    out->high_water = sqlite3_column_int(st, 5);
    found = true;
  }
  sqlite3_finalize(st);
  return found;
#else
  (void)id;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_fido_echolink_get_by_area(BbsDb *db, int area_id, DbFidoEcholink *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, area_id, echotag, aka_id, COALESCE(origin,''), high_water "
                    "FROM fido_echolinks WHERE area_id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, area_id);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    out->id = sqlite3_column_int(st, 0);
    out->area_id = sqlite3_column_int(st, 1);
    safe_copy(out->echotag, sizeof(out->echotag), (const char *)sqlite3_column_text(st, 2));
    out->aka_id = sqlite3_column_int(st, 3);
    safe_copy(out->origin, sizeof(out->origin), (const char *)sqlite3_column_text(st, 4));
    out->high_water = sqlite3_column_int(st, 5);
    found = true;
  }
  sqlite3_finalize(st);
  return found;
#else
  (void)area_id;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_fido_echolink_update(BbsDb *db, int id, const char *echotag, int aka_id, const char *origin)
{
  if (!db || !echotag)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE fido_echolinks SET echotag = ?2, aka_id = ?3, origin = ?4 WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  sqlite3_bind_text(st, 2, echotag, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 3, aka_id);
  if (origin && origin[0])
  {
    sqlite3_bind_text(st, 4, origin, -1, SQLITE_TRANSIENT);
  }
  else
  {
    sqlite3_bind_null(st, 4);
  }
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  (void)echotag;
  (void)aka_id;
  (void)origin;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_fido_echolink_delete(BbsDb *db, int id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  db_exec(db, "DELETE FROM fido_echomail_queue WHERE echolink_id = ?1");
  const char *sql = "DELETE FROM fido_echolinks WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_fido_echolink_update_highwater(BbsDb *db, int id, int high_water)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE fido_echolinks SET high_water = ?2 WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  sqlite3_bind_int(st, 2, high_water);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  (void)high_water;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

/* ========== FidoNet Netmail ========== */

int db_fido_netmail_list(BbsDb *db, const char *status, DbFidoNetmail *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = status && status[0] ? "SELECT id, from_zone, from_net, from_node, from_point, from_name, "
                                          "to_zone, to_net, to_node, to_point, to_name, subject, body, attr, "
                                          "created_at, COALESCE(sent_at,''), status FROM fido_netmail WHERE status = ?1 ORDER BY created_at"
                                        : "SELECT id, from_zone, from_net, from_node, from_point, from_name, "
                                          "to_zone, to_net, to_node, to_point, to_name, subject, body, attr, "
                                          "created_at, COALESCE(sent_at,''), status FROM fido_netmail ORDER BY created_at";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  if (status && status[0])
  {
    sqlite3_bind_text(st, 1, status, -1, SQLITE_TRANSIENT);
  }
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    out[count].from_zone = sqlite3_column_int(st, 1);
    out[count].from_net = sqlite3_column_int(st, 2);
    out[count].from_node = sqlite3_column_int(st, 3);
    out[count].from_point = sqlite3_column_int(st, 4);
    safe_copy(out[count].from_name, sizeof(out[count].from_name), (const char *)sqlite3_column_text(st, 5));
    out[count].to_zone = sqlite3_column_int(st, 6);
    out[count].to_net = sqlite3_column_int(st, 7);
    out[count].to_node = sqlite3_column_int(st, 8);
    out[count].to_point = sqlite3_column_int(st, 9);
    safe_copy(out[count].to_name, sizeof(out[count].to_name), (const char *)sqlite3_column_text(st, 10));
    safe_copy(out[count].subject, sizeof(out[count].subject), (const char *)sqlite3_column_text(st, 11));
    safe_copy(out[count].body, sizeof(out[count].body), (const char *)sqlite3_column_text(st, 12));
    out[count].attr = (unsigned)sqlite3_column_int(st, 13);
    safe_copy(out[count].created_at, sizeof(out[count].created_at), (const char *)sqlite3_column_text(st, 14));
    safe_copy(out[count].sent_at, sizeof(out[count].sent_at), (const char *)sqlite3_column_text(st, 15));
    safe_copy(out[count].status, sizeof(out[count].status), (const char *)sqlite3_column_text(st, 16));
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)status;
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_fido_netmail_add(BbsDb *db, const DbFidoNetmail *nm)
{
  if (!db || !nm)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO fido_netmail (from_zone, from_net, from_node, from_point, from_name, "
                    "to_zone, to_net, to_node, to_point, to_name, subject, body, attr) "
                    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, nm->from_zone);
  sqlite3_bind_int(st, 2, nm->from_net);
  sqlite3_bind_int(st, 3, nm->from_node);
  sqlite3_bind_int(st, 4, nm->from_point);
  sqlite3_bind_text(st, 5, nm->from_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 6, nm->to_zone);
  sqlite3_bind_int(st, 7, nm->to_net);
  sqlite3_bind_int(st, 8, nm->to_node);
  sqlite3_bind_int(st, 9, nm->to_point);
  sqlite3_bind_text(st, 10, nm->to_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 11, nm->subject, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 12, nm->body, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 13, (int)nm->attr);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)nm;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_fido_netmail_get(BbsDb *db, int id, DbFidoNetmail *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, from_zone, from_net, from_node, from_point, from_name, "
                    "to_zone, to_net, to_node, to_point, to_name, subject, body, attr, "
                    "created_at, COALESCE(sent_at,''), status FROM fido_netmail WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    out->id = sqlite3_column_int(st, 0);
    out->from_zone = sqlite3_column_int(st, 1);
    out->from_net = sqlite3_column_int(st, 2);
    out->from_node = sqlite3_column_int(st, 3);
    out->from_point = sqlite3_column_int(st, 4);
    safe_copy(out->from_name, sizeof(out->from_name), (const char *)sqlite3_column_text(st, 5));
    out->to_zone = sqlite3_column_int(st, 6);
    out->to_net = sqlite3_column_int(st, 7);
    out->to_node = sqlite3_column_int(st, 8);
    out->to_point = sqlite3_column_int(st, 9);
    safe_copy(out->to_name, sizeof(out->to_name), (const char *)sqlite3_column_text(st, 10));
    safe_copy(out->subject, sizeof(out->subject), (const char *)sqlite3_column_text(st, 11));
    safe_copy(out->body, sizeof(out->body), (const char *)sqlite3_column_text(st, 12));
    out->attr = (unsigned)sqlite3_column_int(st, 13);
    safe_copy(out->created_at, sizeof(out->created_at), (const char *)sqlite3_column_text(st, 14));
    safe_copy(out->sent_at, sizeof(out->sent_at), (const char *)sqlite3_column_text(st, 15));
    safe_copy(out->status, sizeof(out->status), (const char *)sqlite3_column_text(st, 16));
    found = true;
  }
  sqlite3_finalize(st);
  return found;
#else
  (void)id;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_fido_netmail_mark_sent(BbsDb *db, int id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE fido_netmail SET status = 'sent', sent_at = datetime('now') WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_fido_netmail_delete(BbsDb *db, int id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "DELETE FROM fido_netmail WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

/* ========== FidoNet Echomail Queue ========== */

bool db_fido_echo_queue_add(BbsDb *db, int echolink_id, int message_id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT OR IGNORE INTO fido_echomail_queue (echolink_id, message_id) VALUES (?1, ?2)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, echolink_id);
  sqlite3_bind_int(st, 2, message_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)echolink_id;
  (void)message_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_fido_echo_queue_pending(BbsDb *db, int echolink_id, int *message_ids, int max)
{
  if (!db || !message_ids || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT message_id FROM fido_echomail_queue "
                    "WHERE echolink_id = ?1 AND status = 'pending' ORDER BY queued_at LIMIT ?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  sqlite3_bind_int(st, 1, echolink_id);
  sqlite3_bind_int(st, 2, max);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    message_ids[count++] = sqlite3_column_int(st, 0);
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)echolink_id;
  (void)message_ids;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_fido_echo_queue_mark_exported(BbsDb *db, int echolink_id, int message_id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE fido_echomail_queue SET status = 'exported', exported_at = datetime('now') "
                    "WHERE echolink_id = ?1 AND message_id = ?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, echolink_id);
  sqlite3_bind_int(st, 2, message_id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)echolink_id;
  (void)message_id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

/* ========== FidoNet Address Helpers ========== */

bool fido_format_address(const DbFidoAka *aka, char *out, size_t cap)
{
  if (!aka || !out || cap < 16)
    return false;
  if (aka->point > 0)
  {
    snprintf(out, cap, "%d:%d/%d.%d", aka->zone, aka->net, aka->node, aka->point);
  }
  else
  {
    snprintf(out, cap, "%d:%d/%d", aka->zone, aka->net, aka->node);
  }
  return true;
}

bool fido_parse_address(const char *str, int *zone, int *net, int *node, int *point)
{
  if (!str || !zone || !net || !node || !point)
    return false;
  *zone = *net = *node = *point = 0;
  int z, n, nd, p = 0;
  int parts = sscanf(str, "%d:%d/%d.%d", &z, &n, &nd, &p);
  if (parts < 3)
    return false;
  *zone = z;
  *net = n;
  *node = nd;
  *point = p;
  return true;
}

/* ========== QWK Network Hub Management ========== */

int db_qwk_hub_list(BbsDb *db, DbQwkHub *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, name, bbs_id, COALESCE(call_schedule,''), COALESCE(last_call,''), enabled "
                    "FROM qwk_hubs ORDER BY name";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    safe_copy(out[count].name, sizeof(out[count].name), (const char *)sqlite3_column_text(st, 1));
    safe_copy(out[count].bbs_id, sizeof(out[count].bbs_id), (const char *)sqlite3_column_text(st, 2));
    safe_copy(out[count].call_schedule, sizeof(out[count].call_schedule), (const char *)sqlite3_column_text(st, 3));
    safe_copy(out[count].last_call, sizeof(out[count].last_call), (const char *)sqlite3_column_text(st, 4));
    out[count].enabled = sqlite3_column_int(st, 5);
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_qwk_hub_add(BbsDb *db, const char *name, const char *bbs_id, const char *schedule)
{
  if (!db || !name || !bbs_id)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO qwk_hubs (name, bbs_id, call_schedule) VALUES (?1, ?2, ?3)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, bbs_id, -1, SQLITE_TRANSIENT);
  if (schedule && schedule[0])
  {
    sqlite3_bind_text(st, 3, schedule, -1, SQLITE_TRANSIENT);
  }
  else
  {
    sqlite3_bind_null(st, 3);
  }
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)name;
  (void)bbs_id;
  (void)schedule;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_qwk_hub_get(BbsDb *db, int id, DbQwkHub *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, name, bbs_id, COALESCE(call_schedule,''), COALESCE(last_call,''), enabled "
                    "FROM qwk_hubs WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    out->id = sqlite3_column_int(st, 0);
    safe_copy(out->name, sizeof(out->name), (const char *)sqlite3_column_text(st, 1));
    safe_copy(out->bbs_id, sizeof(out->bbs_id), (const char *)sqlite3_column_text(st, 2));
    safe_copy(out->call_schedule, sizeof(out->call_schedule), (const char *)sqlite3_column_text(st, 3));
    safe_copy(out->last_call, sizeof(out->last_call), (const char *)sqlite3_column_text(st, 4));
    out->enabled = sqlite3_column_int(st, 5);
    found = true;
  }
  sqlite3_finalize(st);
  return found;
#else
  (void)id;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_qwk_hub_update(BbsDb *db, int id, const char *name, const char *bbs_id, const char *schedule, int enabled)
{
  if (!db || !name || !bbs_id)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE qwk_hubs SET name = ?2, bbs_id = ?3, call_schedule = ?4, enabled = ?5 WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  sqlite3_bind_text(st, 2, name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, bbs_id, -1, SQLITE_TRANSIENT);
  if (schedule && schedule[0])
  {
    sqlite3_bind_text(st, 4, schedule, -1, SQLITE_TRANSIENT);
  }
  else
  {
    sqlite3_bind_null(st, 4);
  }
  sqlite3_bind_int(st, 5, enabled);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  (void)name;
  (void)bbs_id;
  (void)schedule;
  (void)enabled;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_qwk_hub_delete(BbsDb *db, int id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  db_exec(db, "DELETE FROM qwk_area_links WHERE hub_id = ?1");
  db_exec(db, "DELETE FROM qwk_packet_queue WHERE hub_id = ?1");
  const char *sql = "DELETE FROM qwk_hubs WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_qwk_hub_mark_called(BbsDb *db, int id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE qwk_hubs SET last_call = datetime('now') WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

/* ========== QWK Area Links ========== */

int db_qwk_area_link_list(BbsDb *db, int hub_id, DbQwkAreaLink *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  const char *sql = hub_id > 0 ? "SELECT id, hub_id, area_id, remote_conf, high_water_in, high_water_out "
                                 "FROM qwk_area_links WHERE hub_id = ?1 ORDER BY area_id"
                               : "SELECT id, hub_id, area_id, remote_conf, high_water_in, high_water_out "
                                 "FROM qwk_area_links ORDER BY hub_id, area_id";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  if (hub_id > 0)
  {
    sqlite3_bind_int(st, 1, hub_id);
  }
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    out[count].hub_id = sqlite3_column_int(st, 1);
    out[count].area_id = sqlite3_column_int(st, 2);
    out[count].remote_conf = sqlite3_column_int(st, 3);
    out[count].high_water_in = sqlite3_column_int(st, 4);
    out[count].high_water_out = sqlite3_column_int(st, 5);
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)hub_id;
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_qwk_area_link_add(BbsDb *db, int hub_id, int area_id, int remote_conf)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO qwk_area_links (hub_id, area_id, remote_conf) VALUES (?1, ?2, ?3)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, hub_id);
  sqlite3_bind_int(st, 2, area_id);
  sqlite3_bind_int(st, 3, remote_conf);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)hub_id;
  (void)area_id;
  (void)remote_conf;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_qwk_area_link_get(BbsDb *db, int id, DbQwkAreaLink *out)
{
  if (!db || !out)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT id, hub_id, area_id, remote_conf, high_water_in, high_water_out "
                    "FROM qwk_area_links WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW)
  {
    out->id = sqlite3_column_int(st, 0);
    out->hub_id = sqlite3_column_int(st, 1);
    out->area_id = sqlite3_column_int(st, 2);
    out->remote_conf = sqlite3_column_int(st, 3);
    out->high_water_in = sqlite3_column_int(st, 4);
    out->high_water_out = sqlite3_column_int(st, 5);
    found = true;
  }
  sqlite3_finalize(st);
  return found;
#else
  (void)id;
  (void)out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_qwk_area_link_update(BbsDb *db, int id, int remote_conf)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE qwk_area_links SET remote_conf = ?2 WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  sqlite3_bind_int(st, 2, remote_conf);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  (void)remote_conf;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_qwk_area_link_delete(BbsDb *db, int id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "DELETE FROM qwk_area_links WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_qwk_area_link_update_highwater(BbsDb *db, int id, int hw_in, int hw_out)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE qwk_area_links SET high_water_in = ?2, high_water_out = ?3 WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  sqlite3_bind_int(st, 2, hw_in);
  sqlite3_bind_int(st, 3, hw_out);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  (void)hw_in;
  (void)hw_out;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

/* ========== QWK Packet Queue ========== */

int db_qwk_packet_list(BbsDb *db, int hub_id, const char *status, DbQwkPacket *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  char sql[512];
  if (hub_id > 0 && status && status[0])
  {
    snprintf(sql, sizeof(sql),
             "SELECT id, hub_id, packet_type, packet_path, status, created_at, COALESCE(processed_at,'') "
             "FROM qwk_packet_queue WHERE hub_id = ?1 AND status = ?2 ORDER BY created_at");
  }
  else if (hub_id > 0)
  {
    snprintf(sql, sizeof(sql),
             "SELECT id, hub_id, packet_type, packet_path, status, created_at, COALESCE(processed_at,'') "
             "FROM qwk_packet_queue WHERE hub_id = ?1 ORDER BY created_at");
  }
  else if (status && status[0])
  {
    snprintf(sql, sizeof(sql),
             "SELECT id, hub_id, packet_type, packet_path, status, created_at, COALESCE(processed_at,'') "
             "FROM qwk_packet_queue WHERE status = ?1 ORDER BY created_at");
  }
  else
  {
    snprintf(sql, sizeof(sql),
             "SELECT id, hub_id, packet_type, packet_path, status, created_at, COALESCE(processed_at,'') "
             "FROM qwk_packet_queue ORDER BY created_at");
  }
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  int param = 1;
  if (hub_id > 0)
  {
    sqlite3_bind_int(st, param++, hub_id);
  }
  if (status && status[0])
  {
    sqlite3_bind_text(st, param++, status, -1, SQLITE_TRANSIENT);
  }
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    out[count].hub_id = sqlite3_column_int(st, 1);
    safe_copy(out[count].packet_type, sizeof(out[count].packet_type), (const char *)sqlite3_column_text(st, 2));
    safe_copy(out[count].packet_path, sizeof(out[count].packet_path), (const char *)sqlite3_column_text(st, 3));
    safe_copy(out[count].status, sizeof(out[count].status), (const char *)sqlite3_column_text(st, 4));
    safe_copy(out[count].created_at, sizeof(out[count].created_at), (const char *)sqlite3_column_text(st, 5));
    safe_copy(out[count].processed_at, sizeof(out[count].processed_at), (const char *)sqlite3_column_text(st, 6));
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)hub_id;
  (void)status;
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_qwk_packet_add(BbsDb *db, int hub_id, const char *packet_type, const char *path)
{
  if (!db || !packet_type || !path)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO qwk_packet_queue (hub_id, packet_type, packet_path) VALUES (?1, ?2, ?3)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, hub_id);
  sqlite3_bind_text(st, 2, packet_type, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, path, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)hub_id;
  (void)packet_type;
  (void)path;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_qwk_packet_mark_processed(BbsDb *db, int id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE qwk_packet_queue SET status = 'processed', processed_at = datetime('now') WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

bool db_qwk_packet_delete(BbsDb *db, int id)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "DELETE FROM qwk_packet_queue WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_int(st, 1, id);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)id;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

/* ========== Chat Logging ========== */

bool db_chat_log(BbsDb *db, const char *chat_type, int room_id, int from_user, const char *from_handle,
                 int to_user, const char *to_handle, const char *message)
{
  if (!db || !chat_type || !from_handle || !message)
    return false;
#ifdef HAVE_SQLITE
  const char *sql = "INSERT INTO chat_logs (chat_type, room_id, from_user, from_handle, to_user, to_handle, message) "
                    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return false;
  }
  sqlite3_bind_text(st, 1, chat_type, -1, SQLITE_TRANSIENT);
  if (room_id > 0)
  {
    sqlite3_bind_int(st, 2, room_id);
  }
  else
  {
    sqlite3_bind_null(st, 2);
  }
  sqlite3_bind_int(st, 3, from_user);
  sqlite3_bind_text(st, 4, from_handle, -1, SQLITE_TRANSIENT);
  if (to_user > 0)
  {
    sqlite3_bind_int(st, 5, to_user);
  }
  else
  {
    sqlite3_bind_null(st, 5);
  }
  if (to_handle && to_handle[0])
  {
    sqlite3_bind_text(st, 6, to_handle, -1, SQLITE_TRANSIENT);
  }
  else
  {
    sqlite3_bind_null(st, 6);
  }
  sqlite3_bind_text(st, 7, message, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(st);
  sqlite3_finalize(st);
  return rc == SQLITE_DONE;
#else
  (void)chat_type;
  (void)room_id;
  (void)from_user;
  (void)from_handle;
  (void)to_user;
  (void)to_handle;
  (void)message;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

int db_chat_log_list(BbsDb *db, const char *chat_type, int room_id, DbChatLog *out, int max)
{
  if (!db || !out || max <= 0)
    return 0;
#ifdef HAVE_SQLITE
  char sql[512];
  if (chat_type && chat_type[0] && room_id > 0)
  {
    snprintf(sql, sizeof(sql),
             "SELECT id, chat_type, COALESCE(room_id,0), from_user, from_handle, "
             "COALESCE(to_user,0), COALESCE(to_handle,''), message, logged_at "
             "FROM chat_logs WHERE chat_type = ?1 AND room_id = ?2 ORDER BY logged_at DESC LIMIT ?3");
  }
  else if (chat_type && chat_type[0])
  {
    snprintf(sql, sizeof(sql),
             "SELECT id, chat_type, COALESCE(room_id,0), from_user, from_handle, "
             "COALESCE(to_user,0), COALESCE(to_handle,''), message, logged_at "
             "FROM chat_logs WHERE chat_type = ?1 ORDER BY logged_at DESC LIMIT ?2");
  }
  else
  {
    snprintf(sql, sizeof(sql),
             "SELECT id, chat_type, COALESCE(room_id,0), from_user, from_handle, "
             "COALESCE(to_user,0), COALESCE(to_handle,''), message, logged_at "
             "FROM chat_logs ORDER BY logged_at DESC LIMIT ?1");
  }
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  {
    set_err(db, sqlite3_errmsg(db->db));
    return 0;
  }
  int param = 1;
  if (chat_type && chat_type[0])
  {
    sqlite3_bind_text(st, param++, chat_type, -1, SQLITE_TRANSIENT);
    if (room_id > 0)
    {
      sqlite3_bind_int(st, param++, room_id);
    }
  }
  sqlite3_bind_int(st, param, max);
  int count = 0;
  while (sqlite3_step(st) == SQLITE_ROW && count < max)
  {
    out[count].id = sqlite3_column_int(st, 0);
    safe_copy(out[count].chat_type, sizeof(out[count].chat_type), (const char *)sqlite3_column_text(st, 1));
    out[count].room_id = sqlite3_column_int(st, 2);
    out[count].from_user = sqlite3_column_int(st, 3);
    safe_copy(out[count].from_handle, sizeof(out[count].from_handle), (const char *)sqlite3_column_text(st, 4));
    out[count].to_user = sqlite3_column_int(st, 5);
    safe_copy(out[count].to_handle, sizeof(out[count].to_handle), (const char *)sqlite3_column_text(st, 6));
    safe_copy(out[count].message, sizeof(out[count].message), (const char *)sqlite3_column_text(st, 7));
    safe_copy(out[count].logged_at, sizeof(out[count].logged_at), (const char *)sqlite3_column_text(st, 8));
    count++;
  }
  sqlite3_finalize(st);
  return count;
#else
  (void)chat_type;
  (void)room_id;
  (void)out;
  (void)max;
  set_err(db, "sqlite disabled");
  return 0;
#endif
}

bool db_chat_log_clear(BbsDb *db, int days_old)
{
  if (!db)
    return false;
#ifdef HAVE_SQLITE
  char sql[128];
  snprintf(sql, sizeof(sql), "DELETE FROM chat_logs WHERE logged_at < datetime('now', '-%d days')", days_old);
  return db_exec(db, sql);
#else
  (void)days_old;
  set_err(db, "sqlite disabled");
  return false;
#endif
}

/* =========================================================================
 * Scan area flags (MZ command)
 * ========================================================================= */

int db_user_scan_area_get(BbsDb *db, int user_id, int area_id)
{
  if (!db) return 1;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT scan_enabled FROM user_msg_scan_areas WHERE user_id=?1 AND area_id=?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
    return 1;
  sqlite3_bind_int(st, 1, user_id);
  sqlite3_bind_int(st, 2, area_id);
  int enabled = 1;
  if (sqlite3_step(st) == SQLITE_ROW)
    enabled = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return enabled;
#else
  (void)user_id; (void)area_id; return 1;
#endif
}

bool db_user_scan_area_set(BbsDb *db, int user_id, int area_id, int enabled)
{
  if (!db) return false;
#ifdef HAVE_SQLITE
  const char *sql =
    "INSERT INTO user_msg_scan_areas (user_id, area_id, scan_enabled) VALUES (?1,?2,?3) "
    "ON CONFLICT(user_id, area_id) DO UPDATE SET scan_enabled=excluded.scan_enabled";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  { set_err(db, sqlite3_errmsg(db->db)); return false; }
  sqlite3_bind_int(st, 1, user_id);
  sqlite3_bind_int(st, 2, area_id);
  sqlite3_bind_int(st, 3, enabled ? 1 : 0);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
#else
  (void)user_id; (void)area_id; (void)enabled;
  set_err(db, "sqlite disabled"); return false;
#endif
}

/* =========================================================================
 * Drafts
 * ========================================================================= */

bool db_draft_save(BbsDb *db, int user_id, int area_id, int to_user_id,
                   const char *to_name, const char *subject, const char *body)
{
  if (!db || !subject || !body) return false;
#ifdef HAVE_SQLITE
  /* Remove any existing draft for this user+area before inserting */
  {
    sqlite3_stmt *del = NULL;
    if (sqlite3_prepare_v2(db->db, "DELETE FROM drafts WHERE user_id=?1 AND area_id=?2", -1, &del, NULL) == SQLITE_OK) {
      sqlite3_bind_int(del, 1, user_id);
      sqlite3_bind_int(del, 2, area_id);
      sqlite3_step(del);
      sqlite3_finalize(del);
    }
  }
  const char *sql =
    "INSERT INTO drafts (user_id, area_id, to_user_id, to_name, subject, body) "
    "VALUES (?1,?2,?3,?4,?5,?6)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  { set_err(db, sqlite3_errmsg(db->db)); return false; }
  sqlite3_bind_int(st, 1, user_id);
  sqlite3_bind_int(st, 2, area_id);
  sqlite3_bind_int(st, 3, to_user_id);
  sqlite3_bind_text(st, 4, to_name   ? to_name   : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 5, subject, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 6, body,    -1, SQLITE_TRANSIENT);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
#else
  (void)user_id; (void)area_id; (void)to_user_id;
  (void)to_name; (void)subject; (void)body;
  set_err(db, "sqlite disabled"); return false;
#endif
}

bool db_draft_get(BbsDb *db, int user_id, DbDraft *out)
{
  if (!db || !out) return false;
#ifdef HAVE_SQLITE
  const char *sql =
    "SELECT id,user_id,area_id,to_user_id,COALESCE(to_name,''),subject,body,created_at "
    "FROM drafts WHERE user_id=?1 ORDER BY id DESC LIMIT 1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  { set_err(db, sqlite3_errmsg(db->db)); return false; }
  sqlite3_bind_int(st, 1, user_id);
  bool found = false;
  if (sqlite3_step(st) == SQLITE_ROW) {
    memset(out, 0, sizeof(*out));
    out->id         = sqlite3_column_int(st, 0);
    out->user_id    = sqlite3_column_int(st, 1);
    out->area_id    = sqlite3_column_int(st, 2);
    out->to_user_id = sqlite3_column_int(st, 3);
    safe_copy(out->to_name,    sizeof(out->to_name),    (const char*)sqlite3_column_text(st, 4));
    safe_copy(out->subject,    sizeof(out->subject),    (const char*)sqlite3_column_text(st, 5));
    safe_copy(out->body,       sizeof(out->body),       (const char*)sqlite3_column_text(st, 6));
    safe_copy(out->created_at, sizeof(out->created_at), (const char*)sqlite3_column_text(st, 7));
    found = true;
  }
  sqlite3_finalize(st);
  return found;
#else
  (void)user_id; (void)out; set_err(db, "sqlite disabled"); return false;
#endif
}

bool db_draft_delete(BbsDb *db, int draft_id)
{
  if (!db) return false;
#ifdef HAVE_SQLITE
  const char *sql = "DELETE FROM drafts WHERE id=?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  { set_err(db, sqlite3_errmsg(db->db)); return false; }
  sqlite3_bind_int(st, 1, draft_id);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
#else
  (void)draft_id; set_err(db, "sqlite disabled"); return false;
#endif
}

int db_draft_count(BbsDb *db, int user_id)
{
  if (!db) return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT COUNT(*) FROM drafts WHERE user_id=?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
  sqlite3_bind_int(st, 1, user_id);
  int c = 0;
  if (sqlite3_step(st) == SQLITE_ROW) c = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return c;
#else
  (void)user_id; return 0;
#endif
}

/* =========================================================================
 * Mailbox capacity
 * ========================================================================= */

int db_count_messages_to_user_inbox(BbsDb *db, int user_id)
{
  if (!db) return 0;
#ifdef HAVE_SQLITE
  const char *sql = "SELECT COUNT(*) FROM messages WHERE to_user=?1";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
  sqlite3_bind_int(st, 1, user_id);
  int c = 0;
  if (sqlite3_step(st) == SQLITE_ROW) c = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return c;
#else
  (void)user_id; return 0;
#endif
}

/* =========================================================================
 * User FSEditor preference
 * ========================================================================= */

bool db_user_set_use_fse(BbsDb *db, int user_id, int use_fse)
{
  if (!db) return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE users SET use_fse=?1 WHERE id=?2";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  { set_err(db, sqlite3_errmsg(db->db)); return false; }
  sqlite3_bind_int(st, 1, use_fse ? 1 : 0);
  sqlite3_bind_int(st, 2, user_id);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
#else
  (void)user_id; (void)use_fse; set_err(db, "sqlite disabled"); return false;
#endif
}

bool db_user_set_signature(BbsDb *db, int user_id, const char *sig, int use_sig)
{
  if (!db) return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE users SET signature=?1, use_signature=?2 WHERE id=?3";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  { set_err(db, sqlite3_errmsg(db->db)); return false; }
  sqlite3_bind_text(st, 1, sig ? sig : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, use_sig ? 1 : 0);
  sqlite3_bind_int(st, 3, user_id);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
#else
  (void)user_id; (void)sig; (void)use_sig; set_err(db, "sqlite disabled"); return false;
#endif
}

bool db_user_set_tagline(BbsDb *db, int user_id, const char *tag, int use_tag)
{
  if (!db) return false;
#ifdef HAVE_SQLITE
  const char *sql = "UPDATE users SET tagline=?1, use_tagline=?2 WHERE id=?3";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
  { set_err(db, sqlite3_errmsg(db->db)); return false; }
  sqlite3_bind_text(st, 1, tag ? tag : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(st, 2, use_tag ? 1 : 0);
  sqlite3_bind_int(st, 3, user_id);
  bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
#else
  (void)user_id; (void)tag; (void)use_tag; set_err(db, "sqlite disabled"); return false;
#endif
}
