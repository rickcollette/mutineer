#include "mts.h"
#include <stdio.h>
#include <string.h>

static int sql(sqlite3 *db, const char *s, char *err, size_t n) {
  char *e = NULL;
  if (sqlite3_exec(db, s, NULL, NULL, &e) != SQLITE_OK) {
    snprintf(err, n, "%s", e ? e : "sqlite error");
    sqlite3_free(e);
    return 0;
  }
  return 1;
}
int mts_store_open(mts_state_t *st, const char *dir, char *err, size_t n) {
  char path[768];
  snprintf(path, sizeof path, "%s/mts.db", dir);
  if (sqlite3_open_v2(path, &st->db,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      NULL) != SQLITE_OK) {
    snprintf(err, n, "%s", sqlite3_errmsg(st->db));
    return 0;
  }
  sqlite3_busy_timeout(st->db, 3000);
  sqlite3_stmt *check = NULL;
  if (sqlite3_prepare_v2(st->db, "PRAGMA quick_check", -1, &check, NULL) !=
          SQLITE_OK ||
      sqlite3_step(check) != SQLITE_ROW ||
      strcmp((const char *)sqlite3_column_text(check, 0), "ok")) {
    snprintf(err, n, "MTS database integrity check failed");
    if (check)
      sqlite3_finalize(check);
    return 0;
  }
  sqlite3_finalize(check);
  const char *schema =
      "BEGIN;CREATE TABLE IF NOT EXISTS mts_schema_version(version INTEGER "
      "PRIMARY KEY,applied_at INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS mts_rooms(id INTEGER PRIMARY "
      "KEY,normalized_name TEXT UNIQUE,display_name TEXT,topic TEXT,visibility "
      "INTEGER,permanent INTEGER,owner_user_id INTEGER,created_at "
      "INTEGER,updated_at INTEGER);"
      "CREATE TABLE IF NOT EXISTS mts_preferences(user_id INTEGER PRIMARY "
      "KEY,timestamps INTEGER,joins INTEGER,actions INTEGER,bell INTEGER,echo "
      "INTEGER,color INTEGER,theme TEXT,default_room_id INTEGER,greeting "
      "TEXT,farewell TEXT,allow_chat_requests INTEGER,directory_visible "
      "INTEGER,last_seen_visible INTEGER,updated_at INTEGER);"
      "CREATE TABLE IF NOT EXISTS mts_ignores(owner_user_id "
      "INTEGER,target_user_id INTEGER,created_at INTEGER,PRIMARY "
      "KEY(owner_user_id,target_user_id));"
      "CREATE TABLE IF NOT EXISTS mts_blocks(owner_user_id "
      "INTEGER,target_user_id INTEGER,created_at INTEGER,PRIMARY "
      "KEY(owner_user_id,target_user_id));"
      "CREATE TABLE IF NOT EXISTS mts_profiles(user_id INTEGER PRIMARY KEY,bio "
      "TEXT,updated_at INTEGER);"
      "CREATE TABLE IF NOT EXISTS mts_invitations(room_id INTEGER,user_id "
      "INTEGER,invited_by INTEGER,created_at INTEGER,PRIMARY "
      "KEY(room_id,user_id));"
      "CREATE TABLE IF NOT EXISTS mts_room_bans(room_id INTEGER,user_id "
      "INTEGER,actor_user_id INTEGER,reason TEXT,expires_at INTEGER,created_at "
      "INTEGER);"
      "CREATE TABLE IF NOT EXISTS mts_room_mutes(room_id INTEGER,user_id "
      "INTEGER,actor_user_id INTEGER,reason TEXT,expires_at INTEGER,created_at "
      "INTEGER);"
      "CREATE TABLE IF NOT EXISTS mts_global_bans(user_id "
      "INTEGER,actor_user_id INTEGER,reason TEXT,expires_at INTEGER,created_at "
      "INTEGER);"
      "CREATE TABLE IF NOT EXISTS mts_global_mutes(user_id "
      "INTEGER,actor_user_id INTEGER,reason TEXT,expires_at INTEGER,created_at "
      "INTEGER);"
      "CREATE TABLE IF NOT EXISTS mts_history(id INTEGER PRIMARY KEY,room_id "
      "INTEGER,sequence INTEGER,event_type INTEGER,sender_user_id "
      "INTEGER,target_user_id INTEGER,sender_handle TEXT,target_handle "
      "TEXT,text TEXT,created_at INTEGER);"
      "CREATE TABLE IF NOT EXISTS mts_visits(id INTEGER PRIMARY KEY,user_id "
      "INTEGER,handle TEXT,entered_at INTEGER,left_at INTEGER);"
      "CREATE TABLE IF NOT EXISTS mts_actions(id INTEGER PRIMARY KEY,name TEXT "
      "UNIQUE,template TEXT,enabled INTEGER,created_by INTEGER,updated_at "
      "INTEGER);"
      "CREATE TABLE IF NOT EXISTS mts_moderation_audit(id INTEGER PRIMARY "
      "KEY,actor_user_id INTEGER,target_user_id INTEGER,room_id INTEGER,action "
      "TEXT,reason TEXT,expires_at INTEGER,created_at INTEGER);"
      "INSERT OR IGNORE INTO mts_schema_version "
      "VALUES(1,strftime('%s','now'));COMMIT;";
  if (!sql(st->db, "PRAGMA journal_mode=WAL;", err, n) ||
      !sql(st->db, "PRAGMA foreign_keys=ON;", err, n) ||
      !sql(st->db, schema, err, n))
    return 0;
  int version = 0;
  sqlite3_stmt *q = NULL;
  if (sqlite3_prepare_v2(
          st->db, "SELECT COALESCE(MAX(version),0) FROM mts_schema_version", -1,
          &q, NULL) != SQLITE_OK)
    return 0;
  if (sqlite3_step(q) == SQLITE_ROW)
    version = sqlite3_column_int(q, 0);
  sqlite3_finalize(q);
  if (version > 3) {
    snprintf(err, n, "MTS database schema %d is newer than supported schema 3",
             version);
    return 0;
  }
  if (version < 2) {
    const char *migration2 =
        "BEGIN IMMEDIATE;"
        "CREATE UNIQUE INDEX IF NOT EXISTS mts_room_bans_unique ON "
        "mts_room_bans(room_id,user_id);"
        "CREATE UNIQUE INDEX IF NOT EXISTS mts_room_mutes_unique ON "
        "mts_room_mutes(room_id,user_id);"
        "CREATE UNIQUE INDEX IF NOT EXISTS mts_global_bans_unique ON "
        "mts_global_bans(user_id);"
        "CREATE UNIQUE INDEX IF NOT EXISTS mts_global_mutes_unique ON "
        "mts_global_mutes(user_id);"
        "CREATE INDEX IF NOT EXISTS mts_history_room_time ON "
        "mts_history(room_id,created_at);"
        "CREATE INDEX IF NOT EXISTS mts_visits_user_time ON "
        "mts_visits(user_id,entered_at);"
        "INSERT OR IGNORE INTO "
        "mts_actions(name,template,enabled,created_by,updated_at) "
        "VALUES('wave','waves',1,0,strftime('%s','now'));"
        "INSERT OR IGNORE INTO "
        "mts_actions(name,template,enabled,created_by,updated_at) "
        "VALUES('smile','smiles',1,0,strftime('%s','now'));"
        "INSERT OR IGNORE INTO "
        "mts_actions(name,template,enabled,created_by,updated_at) "
        "VALUES('nod','nods to {target}',1,0,strftime('%s','now'));"
        "INSERT INTO mts_schema_version VALUES(2,strftime('%s','now'));COMMIT;";
    if (!sql(st->db, migration2, err, n)) {
      sql(st->db, "ROLLBACK", err, n);
      return 0;
    }
    version = 2;
  }
  if (version < 3 &&
      !sql(st->db,
           "BEGIN IMMEDIATE;ALTER TABLE mts_preferences ADD COLUMN "
           "profile_visible INTEGER NOT NULL DEFAULT 1;UPDATE mts_actions SET "
           "template='{actor} waves' WHERE name='wave';UPDATE mts_actions SET "
           "template='{actor} smiles' WHERE name='smile';UPDATE mts_actions "
           "SET template='{actor} nods to {target}' WHERE name='nod';INSERT "
           "INTO mts_schema_version VALUES(3,strftime('%s','now'));COMMIT;",
           err, n)) {
    sql(st->db, "ROLLBACK", err, n);
    return 0;
  }
  return 1;
}
void mts_store_close(mts_state_t *s) {
  if (s->db) {
    sqlite3_close(s->db);
    s->db = NULL;
  }
}
int mts_store_load_rooms(mts_state_t *s) {
  sqlite3_stmt *q = NULL;
  if (sqlite3_prepare_v2(s->db,
                         "SELECT "
                         "id,normalized_name,display_name,topic,visibility,"
                         "permanent,owner_user_id FROM mts_rooms ORDER BY id",
                         -1, &q, NULL) != SQLITE_OK)
    return 0;
  int i = 0;
  while (i < s->cfg.max_rooms && sqlite3_step(q) == SQLITE_ROW) {
    mts_room_t *r = &s->rooms[i++];
    r->id = sqlite3_column_int64(q, 0);
    snprintf(r->normalized, sizeof r->normalized, "%s",
             sqlite3_column_text(q, 1));
    snprintf(r->name, sizeof r->name, "%s", sqlite3_column_text(q, 2));
    snprintf(r->topic, sizeof r->topic, "%s", sqlite3_column_text(q, 3));
    r->is_private = sqlite3_column_int(q, 4);
    r->permanent = sqlite3_column_int(q, 5);
    r->owner = sqlite3_column_int(q, 6);
    r->active = 1;
    if (r->id >= s->next_room_id)
      s->next_room_id = r->id + 1;
  }
  sqlite3_finalize(q);
  return 1;
}
int mts_store_save_room(mts_state_t *s, const mts_room_t *r) {
  sqlite3_stmt *q = NULL;
  const char *x = "INSERT INTO mts_rooms "
                  "VALUES(?,?,?,?,?,?,?,strftime('%s','now'),strftime('%s','"
                  "now')) ON CONFLICT(id) DO UPDATE SET "
                  "normalized_name=excluded.normalized_name,display_name="
                  "excluded.display_name,topic=excluded.topic,visibility="
                  "excluded.visibility,permanent=excluded.permanent,owner_user_"
                  "id=excluded.owner_user_id,updated_at=excluded.updated_at";
  if (sqlite3_prepare_v2(s->db, x, -1, &q, NULL) != SQLITE_OK)
    return 0;
  sqlite3_bind_int64(q, 1, r->id);
  sqlite3_bind_text(q, 2, r->normalized, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(q, 3, r->name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(q, 4, r->topic, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(q, 5, r->is_private);
  sqlite3_bind_int(q, 6, r->permanent);
  sqlite3_bind_int(q, 7, r->owner);
  int ok = sqlite3_step(q) == SQLITE_DONE;
  sqlite3_finalize(q);
  return ok;
}
void mts_store_load_preferences(mts_state_t *s, mts_session_t *u) {
  u->prefs = (mts_preferences_t){.timestamps = 1,
                                 .joins = 1,
                                 .actions = 1,
                                 .echo = 1,
                                 .color = 1,
                                 .allow_chat_requests = 1,
                                 .directory_visible = 1,
                                 .last_seen_visible = 1,
                                 .profile_visible = 1};
  snprintf(u->prefs.theme, sizeof u->prefs.theme, "default");
  sqlite3_stmt *q = NULL;
  if (sqlite3_prepare_v2(
          s->db,
          "SELECT "
          "timestamps,joins,actions,bell,echo,color,theme,default_room_id,"
          "greeting,farewell,allow_chat_requests,directory_visible,last_seen_"
          "visible,profile_visible FROM mts_preferences WHERE user_id=?",
          -1, &q, NULL) != SQLITE_OK)
    return;
  sqlite3_bind_int(q, 1, u->user_id);
  if (sqlite3_step(q) == SQLITE_ROW) {
    u->prefs.timestamps = sqlite3_column_int(q, 0);
    u->prefs.joins = sqlite3_column_int(q, 1);
    u->prefs.actions = sqlite3_column_int(q, 2);
    u->prefs.bell = sqlite3_column_int(q, 3);
    u->prefs.echo = sqlite3_column_int(q, 4);
    u->prefs.color = sqlite3_column_int(q, 5);
    snprintf(u->prefs.theme, sizeof u->prefs.theme, "%s",
             sqlite3_column_text(q, 6));
    u->prefs.default_room = sqlite3_column_int64(q, 7);
    snprintf(u->prefs.greeting, sizeof u->prefs.greeting, "%s",
             sqlite3_column_text(q, 8));
    snprintf(u->prefs.farewell, sizeof u->prefs.farewell, "%s",
             sqlite3_column_text(q, 9));
    u->prefs.allow_chat_requests = sqlite3_column_int(q, 10);
    u->prefs.directory_visible = sqlite3_column_int(q, 11);
    u->prefs.last_seen_visible = sqlite3_column_int(q, 12);
    u->prefs.profile_visible = sqlite3_column_int(q, 13);
  }
  sqlite3_finalize(q);
}
void mts_store_save_preferences(mts_state_t *s, const mts_session_t *u) {
  sqlite3_stmt *q = NULL;
  const char *x =
      "INSERT OR REPLACE INTO "
      "mts_preferences(user_id,timestamps,joins,actions,bell,echo,color,theme,"
      "default_room_id,greeting,farewell,allow_chat_requests,directory_visible,"
      "last_seen_visible,updated_at,profile_visible) "
      "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,strftime('%s','now'),?)";
  if (sqlite3_prepare_v2(s->db, x, -1, &q, NULL) != SQLITE_OK)
    return;
  sqlite3_bind_int(q, 1, u->user_id);
  sqlite3_bind_int(q, 2, u->prefs.timestamps);
  sqlite3_bind_int(q, 3, u->prefs.joins);
  sqlite3_bind_int(q, 4, u->prefs.actions);
  sqlite3_bind_int(q, 5, u->prefs.bell);
  sqlite3_bind_int(q, 6, u->prefs.echo);
  sqlite3_bind_int(q, 7, u->prefs.color);
  sqlite3_bind_text(q, 8, u->prefs.theme, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(q, 9, u->prefs.default_room);
  sqlite3_bind_text(q, 10, u->prefs.greeting, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(q, 11, u->prefs.farewell, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(q, 12, u->prefs.allow_chat_requests);
  sqlite3_bind_int(q, 13, u->prefs.directory_visible);
  sqlite3_bind_int(q, 14, u->prefs.last_seen_visible);
  sqlite3_bind_int(q, 15, u->prefs.profile_visible);
  sqlite3_step(q);
  sqlite3_finalize(q);
}
int mts_store_relation(mts_state_t *s, const char *t, uint32_t o, uint32_t v,
                       int add) {
  if (strcmp(t, "mts_ignores") && strcmp(t, "mts_blocks"))
    return 0;
  char x[256];
  snprintf(x, sizeof x,
           add ? "INSERT OR IGNORE INTO %s VALUES(?,?,strftime('%%s','now'))"
               : "DELETE FROM %s WHERE owner_user_id=? AND target_user_id=?",
           t);
  sqlite3_stmt *q = NULL;
  if (sqlite3_prepare_v2(s->db, x, -1, &q, NULL) != SQLITE_OK)
    return 0;
  sqlite3_bind_int(q, 1, o);
  sqlite3_bind_int(q, 2, v);
  int ok = sqlite3_step(q) == SQLITE_DONE;
  sqlite3_finalize(q);
  return ok;
}
int mts_store_has_relation(mts_state_t *s, const char *t, uint32_t o,
                           uint32_t v) {
  if (strcmp(t, "mts_ignores") && strcmp(t, "mts_blocks"))
    return 0;
  char x[160];
  snprintf(x, sizeof x,
           "SELECT 1 FROM %s WHERE owner_user_id=? AND target_user_id=?", t);
  sqlite3_stmt *q = NULL;
  sqlite3_prepare_v2(s->db, x, -1, &q, NULL);
  sqlite3_bind_int(q, 1, o);
  sqlite3_bind_int(q, 2, v);
  int yes = sqlite3_step(q) == SQLITE_ROW;
  sqlite3_finalize(q);
  return yes;
}
void mts_store_history(mts_state_t *s, const mts_event_t *e) {
  if (!s->cfg.persist_history || e->type == MTS_PRIVATE ||
      e->type == MTS_AFK_RESPONSE)
    return;
  sqlite3_stmt *q = NULL;
  sqlite3_prepare_v2(
      s->db,
      "INSERT INTO "
      "mts_history(room_id,sequence,event_type,sender_user_id,target_user_id,"
      "sender_handle,target_handle,text,created_at) VALUES(?,?,?,?,?,?,?,?,?)",
      -1, &q, NULL);
  sqlite3_bind_int64(q, 1, e->room_id);
  sqlite3_bind_int64(q, 2, e->sequence);
  sqlite3_bind_int(q, 3, e->type);
  sqlite3_bind_int(q, 4, e->sender_id);
  sqlite3_bind_int(q, 5, e->target_id);
  sqlite3_bind_text(q, 6, e->sender, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(q, 7, e->target, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(q, 8, e->text, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(q, 9, e->timestamp);
  sqlite3_step(q);
  sqlite3_finalize(q);
}
void mts_store_audit(mts_state_t *s, uint32_t a, uint32_t t, uint64_t r,
                     const char *x, const char *y) {
  sqlite3_stmt *q = NULL;
  sqlite3_prepare_v2(
      s->db,
      "INSERT INTO "
      "mts_moderation_audit(actor_user_id,target_user_id,room_id,action,reason,"
      "created_at) VALUES(?,?,?,?,?,strftime('%s','now'))",
      -1, &q, NULL);
  sqlite3_bind_int(q, 1, a);
  sqlite3_bind_int(q, 2, t);
  sqlite3_bind_int64(q, 3, r);
  sqlite3_bind_text(q, 4, x, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(q, 5, y, -1, SQLITE_TRANSIENT);
  sqlite3_step(q);
  sqlite3_finalize(q);
}

static int allowed_sanction_table(const char *t) {
  return t && (!strcmp(t, "mts_room_bans") || !strcmp(t, "mts_room_mutes") ||
               !strcmp(t, "mts_global_bans") || !strcmp(t, "mts_global_mutes"));
}
int mts_store_invitation(mts_state_t *s, uint64_t room, uint32_t user,
                         uint32_t actor, int add) {
  sqlite3_stmt *q = NULL;
  const char *x =
      add ? "INSERT OR REPLACE INTO mts_invitations "
            "VALUES(?,?,?,strftime('%s','now'))"
          : "DELETE FROM mts_invitations WHERE room_id=? AND user_id=?";
  if (sqlite3_prepare_v2(s->db, x, -1, &q, NULL) != SQLITE_OK)
    return 0;
  sqlite3_bind_int64(q, 1, room);
  sqlite3_bind_int(q, 2, user);
  if (add)
    sqlite3_bind_int(q, 3, actor);
  int ok = sqlite3_step(q) == SQLITE_DONE;
  sqlite3_finalize(q);
  return ok;
}
int mts_store_is_invited(mts_state_t *s, uint64_t room, uint32_t user) {
  sqlite3_stmt *q = NULL;
  sqlite3_prepare_v2(
      s->db, "SELECT 1 FROM mts_invitations WHERE room_id=? AND user_id=?", -1,
      &q, NULL);
  sqlite3_bind_int64(q, 1, room);
  sqlite3_bind_int(q, 2, user);
  int yes = sqlite3_step(q) == SQLITE_ROW;
  sqlite3_finalize(q);
  return yes;
}
int mts_store_sanction(mts_state_t *s, const char *t, uint64_t room,
                       uint32_t user, uint32_t actor, const char *reason,
                       int64_t expires, int add) {
  if (!allowed_sanction_table(t))
    return 0;
  int global = strstr(t, "global") != NULL;
  char x[512];
  if (add)
    snprintf(x, sizeof x,
             global ? "INSERT OR REPLACE INTO "
                      "%s(user_id,actor_user_id,reason,expires_at,created_at) "
                      "VALUES(?,?,?,?,strftime('%%s','now'))"
                    : "INSERT OR REPLACE INTO "
                      "%s(room_id,user_id,actor_user_id,reason,expires_at,"
                      "created_at) VALUES(?,?,?,?,?,strftime('%%s','now'))",
             t);
  else
    snprintf(x, sizeof x,
             global ? "DELETE FROM %s WHERE user_id=?"
                    : "DELETE FROM %s WHERE room_id=? AND user_id=?",
             t);
  sqlite3_stmt *q = NULL;
  if (sqlite3_prepare_v2(s->db, x, -1, &q, NULL) != SQLITE_OK)
    return 0;
  int i = 1;
  if (!global)
    sqlite3_bind_int64(q, i++, room);
  sqlite3_bind_int(q, i++, user);
  if (add) {
    sqlite3_bind_int(q, i++, actor);
    sqlite3_bind_text(q, i++, reason ? reason : "", -1, SQLITE_TRANSIENT);
    if (expires > 0)
      sqlite3_bind_int64(q, i++, expires);
    else
      sqlite3_bind_null(q, i++);
  }
  int ok = sqlite3_step(q) == SQLITE_DONE;
  sqlite3_finalize(q);
  return ok;
}
int mts_store_is_sanctioned(mts_state_t *s, const char *t, uint64_t room,
                            uint32_t user) {
  if (!allowed_sanction_table(t))
    return 0;
  int global = strstr(t, "global") != NULL;
  char x[320];
  snprintf(x, sizeof x,
           global ? "SELECT 1 FROM %s WHERE user_id=? AND (expires_at IS NULL "
                    "OR expires_at>strftime('%%s','now'))"
                  : "SELECT 1 FROM %s WHERE room_id=? AND user_id=? AND "
                    "(expires_at IS NULL OR expires_at>strftime('%%s','now'))",
           t);
  sqlite3_stmt *q = NULL;
  sqlite3_prepare_v2(s->db, x, -1, &q, NULL);
  int i = 1;
  if (!global)
    sqlite3_bind_int64(q, i++, room);
  sqlite3_bind_int(q, i, user);
  int yes = sqlite3_step(q) == SQLITE_ROW;
  sqlite3_finalize(q);
  return yes;
}
size_t mts_store_list_sanctions(mts_state_t *s, const char *t, uint64_t room,
                                mts_sanction_record_t *r, size_t cap) {
  if (!allowed_sanction_table(t) || !r || !cap)
    return 0;
  int global = strstr(t, "global") != NULL;
  char sql_text[512];
  snprintf(sql_text, sizeof sql_text,
           global
               ? "SELECT user_id,actor_user_id,reason,created_at,"
                 "COALESCE(expires_at,0),0 FROM %s WHERE expires_at IS NULL "
                 "OR expires_at>strftime('%%s','now') ORDER BY created_at DESC"
               : "SELECT user_id,actor_user_id,reason,created_at,"
                 "COALESCE(expires_at,0),room_id FROM %s WHERE room_id=? AND "
                 "(expires_at IS NULL OR expires_at>strftime('%%s','now')) "
                 "ORDER BY created_at DESC",
           t);
  sqlite3_stmt *q = NULL;
  if (sqlite3_prepare_v2(s->db, sql_text, -1, &q, NULL) != SQLITE_OK)
    return 0;
  if (!global)
    sqlite3_bind_int64(q, 1, room);
  size_t n = 0;
  while (n < cap && sqlite3_step(q) == SQLITE_ROW) {
    r[n].user_id = (uint32_t)sqlite3_column_int(q, 0);
    r[n].actor_user_id = (uint32_t)sqlite3_column_int(q, 1);
    snprintf(r[n].reason, sizeof r[n].reason, "%s", sqlite3_column_text(q, 2));
    r[n].created_at = sqlite3_column_int64(q, 3);
    r[n].expires_at = sqlite3_column_int64(q, 4);
    r[n].room_id = (uint64_t)sqlite3_column_int64(q, 5);
    n++;
  }
  sqlite3_finalize(q);
  return n;
}
size_t mts_store_list_audit(mts_state_t *s, mts_audit_record_t *r, size_t cap) {
  if (!r || !cap)
    return 0;
  sqlite3_stmt *q = NULL;
  if (sqlite3_prepare_v2(
          s->db,
          "SELECT "
          "actor_user_id,target_user_id,room_id,action,reason,created_at,"
          "COALESCE(expires_at,0) FROM mts_moderation_audit ORDER BY id DESC "
          "LIMIT ?",
          -1, &q, NULL) != SQLITE_OK)
    return 0;
  sqlite3_bind_int(q, 1, (int)cap);
  size_t n = 0;
  while (n < cap && sqlite3_step(q) == SQLITE_ROW) {
    r[n].actor_user_id = (uint32_t)sqlite3_column_int(q, 0);
    r[n].target_user_id = (uint32_t)sqlite3_column_int(q, 1);
    r[n].room_id = (uint64_t)sqlite3_column_int64(q, 2);
    snprintf(r[n].action, sizeof r[n].action, "%s", sqlite3_column_text(q, 3));
    snprintf(r[n].reason, sizeof r[n].reason, "%s", sqlite3_column_text(q, 4));
    r[n].created_at = sqlite3_column_int64(q, 5);
    r[n].expires_at = sqlite3_column_int64(q, 6);
    n++;
  }
  sqlite3_finalize(q);
  return n;
}
int mts_store_cleanup(mts_state_t *s) {
  char x[1024];
  snprintf(
      x, sizeof x,
      "BEGIN;DELETE FROM mts_room_bans WHERE expires_at IS NOT NULL AND "
      "expires_at<=strftime('%%s','now');DELETE FROM mts_room_mutes WHERE "
      "expires_at IS NOT NULL AND expires_at<=strftime('%%s','now');DELETE "
      "FROM mts_global_bans WHERE expires_at IS NOT NULL AND "
      "expires_at<=strftime('%%s','now');DELETE FROM mts_global_mutes WHERE "
      "expires_at IS NOT NULL AND expires_at<=strftime('%%s','now');DELETE "
      "FROM mts_history WHERE id IN (SELECT id FROM mts_history WHERE "
      "created_at<strftime('%%s','now','-%d days') LIMIT 500);DELETE FROM "
      "mts_moderation_audit WHERE id IN (SELECT id FROM mts_moderation_audit "
      "WHERE created_at<strftime('%%s','now','-%d days') LIMIT 500);COMMIT;",
      s->cfg.history_retention_days, s->cfg.moderation_audit_retention_days);
  char e[128];
  return sql(s->db, x, e, sizeof e);
}
uint64_t mts_store_visit_start(mts_state_t *s, uint32_t user,
                               const char *handle) {
  sqlite3_stmt *q = NULL;
  sqlite3_prepare_v2(s->db,
                     "INSERT INTO mts_visits(user_id,handle,entered_at) "
                     "VALUES(?,?,strftime('%s','now'))",
                     -1, &q, NULL);
  sqlite3_bind_int(q, 1, user);
  sqlite3_bind_text(q, 2, handle, -1, SQLITE_TRANSIENT);
  sqlite3_step(q);
  sqlite3_finalize(q);
  return (uint64_t)sqlite3_last_insert_rowid(s->db);
}
void mts_store_visit_end(mts_state_t *s, uint64_t id) {
  sqlite3_stmt *q = NULL;
  sqlite3_prepare_v2(
      s->db, "UPDATE mts_visits SET left_at=strftime('%s','now') WHERE id=?",
      -1, &q, NULL);
  sqlite3_bind_int64(q, 1, id);
  sqlite3_step(q);
  sqlite3_finalize(q);
}
int mts_store_delete_room(mts_state_t *s, uint64_t room) {
  if (room == 1)
    return 0;
  sqlite3_stmt *q = NULL;
  sqlite3_prepare_v2(s->db, "DELETE FROM mts_rooms WHERE id=?", -1, &q, NULL);
  sqlite3_bind_int64(q, 1, room);
  int ok = sqlite3_step(q) == SQLITE_DONE;
  sqlite3_finalize(q);
  return ok;
}
