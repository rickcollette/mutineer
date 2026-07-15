#include "bbslib.h"
#include "bbs_acs.h"
#include "bbs_doors.h"
#include "bbs_hash.h"
#include "bbs_session.h"
#include "plank/plank_store.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

volatile sig_atomic_t g_stop __attribute__((weak)) = 0;

struct BbsLibContext
{
  BbsConfig cfg;
  BbsDb *db;
  char error[512];
};

struct BbsLibSession
{
  BbsLibContext *ctx;
  Session session;
  BbsLibSessionAdapter adapter;
  unsigned flags;
};

static BbsLibResult set_error(BbsLibContext *ctx, BbsLibResult result, const char *fmt, ...)
{
  if (ctx)
  {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx->error, sizeof(ctx->error), fmt ? fmt : bbslib_result_string(result), ap);
    va_end(ap);
  }
  return result;
}

static void copy_string(char *dst, size_t cap, const char *src)
{
  if (!dst || cap == 0)
    return;
  snprintf(dst, cap, "%s", src ? src : "");
}

const char *bbslib_version(void)
{
  return BBSLIB_VERSION_STRING;
}

const char *bbslib_result_string(BbsLibResult result)
{
  switch (result)
  {
  case BBSLIB_OK: return "ok";
  case BBSLIB_ERR_INVALID: return "invalid argument";
  case BBSLIB_ERR_CONFIG: return "configuration error";
  case BBSLIB_ERR_DB: return "database error";
  case BBSLIB_ERR_NOT_FOUND: return "not found";
  case BBSLIB_ERR_AUTH: return "authentication failed";
  case BBSLIB_ERR_DENIED: return "permission denied";
  case BBSLIB_ERR_UNSUPPORTED: return "unsupported";
  case BBSLIB_ERR_IO: return "I/O error";
  case BBSLIB_ERR_BUFFER: return "buffer too small";
  default: return "unknown error";
  }
}

BbsLibResult bbslib_open(const BbsLibOpenOptions *opts, BbsLibContext **out)
{
  if (!out)
    return BBSLIB_ERR_INVALID;
  *out = NULL;

  const char *config_path = opts && opts->config_path ? opts->config_path : "conf/mutineer.conf";
  BbsLibContext *ctx = (BbsLibContext *)calloc(1, sizeof(*ctx));
  if (!ctx)
    return BBSLIB_ERR_IO;

  if (!cfg_load(config_path, &ctx->cfg))
  {
    BbsLibResult r = set_error(ctx, BBSLIB_ERR_CONFIG, "failed to load config: %s", config_path);
    free(ctx);
    return r;
  }

  ctx->db = db_open(ctx->cfg.db_path);
  if (!ctx->db)
  {
    BbsLibResult r = set_error(ctx, BBSLIB_ERR_DB, "failed to open database: %s", ctx->cfg.db_path);
    free(ctx);
    return r;
  }

  if (opts && (opts->flags & BBSLIB_OPEN_INIT_SCHEMA))
  {
    const char *schema = opts->schema_path ? opts->schema_path : "sql/schema.sql";
    if (!db_init_schema(ctx->db, schema))
    {
      BbsLibResult r = set_error(ctx, BBSLIB_ERR_DB, "failed to initialize schema: %s", db_last_error(ctx->db));
      bbslib_close(ctx);
      return r;
    }
  }

  if (opts && (opts->flags & BBSLIB_OPEN_SEED_DEFAULTS))
  {
    const char *hash = opts->sysop_password_hash ? opts->sysop_password_hash : "CHANGE-ME";
    if (!db_seed_defaults(ctx->db, hash))
    {
      BbsLibResult r = set_error(ctx, BBSLIB_ERR_DB, "failed to seed defaults: %s", db_last_error(ctx->db));
      bbslib_close(ctx);
      return r;
    }
  }

  *out = ctx;
  return BBSLIB_OK;
}

BbsLibResult bbslib_open_path(const char *config_path, BbsLibContext **out)
{
  BbsLibOpenOptions opts = {.config_path = config_path};
  return bbslib_open(&opts, out);
}

void bbslib_close(BbsLibContext *ctx)
{
  if (!ctx)
    return;
  if (ctx->db)
    db_close(ctx->db);
  free(ctx);
}

const char *bbslib_last_error(const BbsLibContext *ctx)
{
  if (!ctx || !ctx->error[0])
    return "";
  return ctx->error;
}

const BbsConfig *bbslib_config(const BbsLibContext *ctx)
{
  return ctx ? &ctx->cfg : NULL;
}

BbsDb *bbslib_db(const BbsLibContext *ctx)
{
  return ctx ? ctx->db : NULL;
}

BbsLibResult bbslib_metrics_get(BbsLibContext *ctx, BbsLibMetrics *out)
{
  if (!ctx || !ctx->db || !out)
    return set_error(ctx, BBSLIB_ERR_INVALID, "missing context or metrics output");
  memset(out, 0, sizeof(*out));
  copy_string(out->bbs_name, sizeof(out->bbs_name), ctx->cfg.bbs_name);
  out->users = db_count_users(ctx->db);
  out->messages = db_count_messages(ctx->db);
  out->files = db_count_files(ctx->db);
  out->oneliners = db_oneliner_count(ctx->db);
  DbMsgArea msg_areas[1024];
  DbFileArea file_areas[1024];
  out->message_areas = db_msg_area_list(ctx->db, msg_areas, 1024);
  out->file_areas = db_file_area_list(ctx->db, file_areas, 1024);
  if (out->message_areas < 0)
    out->message_areas = 0;
  if (out->file_areas < 0)
    out->file_areas = 0;
  if (!db_daily_stats_get(ctx->db, &out->today))
    return set_error(ctx, BBSLIB_ERR_DB, "failed to get daily stats: %s", db_last_error(ctx->db));
  if (!db_system_totals_get(ctx->db, &out->totals))
    return set_error(ctx, BBSLIB_ERR_DB, "failed to get system totals: %s", db_last_error(ctx->db));
  return BBSLIB_OK;
}

static size_t json_escape(char *out, size_t cap, const char *in)
{
  size_t o = 0;
  if (!out || cap == 0)
    return 0;
  for (const unsigned char *p = (const unsigned char *)(in ? in : ""); *p; p++)
  {
    const char *rep = NULL;
    char tmp[8];
    if (*p == '"' || *p == '\\')
    {
      tmp[0] = '\\';
      tmp[1] = (char)*p;
      tmp[2] = '\0';
      rep = tmp;
    }
    else if (*p == '\n')
      rep = "\\n";
    else if (*p == '\r')
      rep = "\\r";
    else if (*p == '\t')
      rep = "\\t";
    else if (*p < 0x20)
    {
      snprintf(tmp, sizeof(tmp), "\\u%04x", *p);
      rep = tmp;
    }
    if (rep)
    {
      size_t n = strlen(rep);
      if (o + n >= cap)
        break;
      memcpy(out + o, rep, n);
      o += n;
    }
    else
    {
      if (o + 1 >= cap)
        break;
      out[o++] = (char)*p;
    }
  }
  out[o] = '\0';
  return o;
}

BbsLibResult bbslib_status_json(BbsLibContext *ctx, char *out, size_t cap)
{
  if (!ctx || !out || cap == 0)
    return set_error(ctx, BBSLIB_ERR_INVALID, "missing status output");
  BbsLibMetrics m;
  BbsLibResult r = bbslib_metrics_get(ctx, &m);
  if (r != BBSLIB_OK)
    return r;
  char name[256];
  json_escape(name, sizeof(name), m.bbs_name);
  int n = snprintf(out, cap,
                   "{\"bbs_name\":\"%s\",\"version\":\"%s\",\"users\":%d,\"messages\":%d,"
                   "\"files\":%d,\"message_areas\":%d,\"file_areas\":%d,\"oneliners\":%d,"
                   "\"today\":{\"calls\":%d,\"posts\":%d,\"emails\":%d,\"newusers\":%d,"
                   "\"uploads\":%d,\"downloads\":%d},"
                   "\"totals\":{\"calls\":%d,\"posts\":%d,\"uploads\":%d,\"downloads\":%d,"
                   "\"days_online\":%d,\"total_users\":%d}}",
                   name, bbslib_version(), m.users, m.messages, m.files, m.message_areas,
                   m.file_areas, m.oneliners, m.today.calls, m.today.posts, m.today.emails,
                   m.today.newusers, m.today.uploads, m.today.downloads,
                   m.totals.total_calls, m.totals.total_posts, m.totals.total_uploads,
                   m.totals.total_downloads, m.totals.days_online, m.totals.total_users);
  if (n < 0 || (size_t)n >= cap)
    return set_error(ctx, BBSLIB_ERR_BUFFER, "status JSON buffer too small");
  return BBSLIB_OK;
}

BbsLibResult bbslib_user_get(BbsLibContext *ctx, const char *handle, DbUser *out)
{
  if (!ctx || !handle || !out)
    return set_error(ctx, BBSLIB_ERR_INVALID, "missing user lookup argument");
  if (!db_user_fetch(ctx->db, handle, out))
    return set_error(ctx, BBSLIB_ERR_NOT_FOUND, "user not found: %s", handle);
  return BBSLIB_OK;
}

BbsLibResult bbslib_user_update(BbsLibContext *ctx, const DbUser *user)
{
  if (!ctx || !user)
    return set_error(ctx, BBSLIB_ERR_INVALID, "missing user update argument");
  if (!db_user_update(ctx->db, user))
    return set_error(ctx, BBSLIB_ERR_DB, "failed to update user: %s", db_last_error(ctx->db));
  return BBSLIB_OK;
}

BbsLibResult bbslib_authenticate_user(BbsLibContext *ctx, const char *handle,
                                      const char *password, DbUser *out,
                                      bool *password_needs_upgrade)
{
  if (!ctx || !handle || !password)
    return set_error(ctx, BBSLIB_ERR_INVALID, "missing authentication argument");
  DbUser user;
  if (!db_user_fetch(ctx->db, handle, &user))
    return set_error(ctx, BBSLIB_ERR_AUTH, "authentication failed");
  if (!pw_hash_verify(password, user.pw_hash))
    return set_error(ctx, BBSLIB_ERR_AUTH, "authentication failed");
  if (out)
    *out = user;
  if (password_needs_upgrade)
    *password_needs_upgrade = pw_hash_needs_upgrade(user.pw_hash);
  return BBSLIB_OK;
}

int bbslib_msg_area_list(BbsLibContext *ctx, DbMsgArea *out, int max_areas)
{
  return ctx ? db_msg_area_list(ctx->db, out, max_areas) : -1;
}

int bbslib_messages_list(BbsLibContext *ctx, int area_id, DbMessage *out, int max_msgs)
{
  return ctx ? db_messages_list(ctx->db, area_id, out, max_msgs) : -1;
}

BbsLibResult bbslib_message_post(BbsLibContext *ctx, int area_id, int user_id,
                                 const char *subject, const char *body,
                                 int reply_to)
{
  if (!ctx || !subject || !body)
    return set_error(ctx, BBSLIB_ERR_INVALID, "missing message post argument");
  if (!db_message_post(ctx->db, area_id, user_id, subject, body, reply_to))
    return set_error(ctx, BBSLIB_ERR_DB, "failed to post message: %s", db_last_error(ctx->db));
  return BBSLIB_OK;
}

int bbslib_file_area_list(BbsLibContext *ctx, DbFileArea *out, int max_areas)
{
  return ctx ? db_file_area_list(ctx->db, out, max_areas) : -1;
}

int bbslib_file_list(BbsLibContext *ctx, int area_id, DbFileRec *out, int max_files)
{
  if (!ctx)
    return -1;
  DbFileArea area;
  if (!db_file_area_get(ctx->db, area_id, &area))
    return -1;
  return db_file_list(&area, ctx->db, out, max_files);
}

BbsLibResult bbslib_file_get(BbsLibContext *ctx, int file_id, DbFileRec *out)
{
  if (!ctx || !out)
    return set_error(ctx, BBSLIB_ERR_INVALID, "missing file lookup argument");
  if (!db_file_get(ctx->db, file_id, out))
    return set_error(ctx, BBSLIB_ERR_NOT_FOUND, "file not found: %d", file_id);
  return BBSLIB_OK;
}

static bool action_allowed(const char *action, unsigned flags)
{
  static const char *safe[] = {
      "who", "messages", "files", "doors", "bulletins", "oneliners", "vote",
      "voteresults", "timebank", "smw", "subscribe", "setsignature",
      "settagline", "togglefse", "pickscheme", "netmail", "batchrun",
      "setfilescandate", "archivetest", "archiveextract", "batchremove",
      "batchupload", "joinconf", "leaveconf", "conflist", "lastcallers",
      "help", "plugins", NULL};
  static const char *admin[] = {
      "wall", "whisper", "chat", "linechat", "splitchat", "page", "useredit",
      "areaadmin", "fileadmin", "subscriptioneditor", "setsecurityq",
      "confeditor", "protocoleditor", "menueditor", "validatefiles",
      "voteeditor", "eventeditor", "maintenance", "fidoeditor", "qwkneteditor",
      "fidosend", "configeditor", NULL};
  if (!action || !action[0])
    return false;
  for (int i = 0; safe[i]; i++)
    if (!strcmp(action, safe[i]))
      return true;
  if (flags & BBSLIB_SESSION_ALLOW_ADMIN_ACTIONS)
    for (int i = 0; admin[i]; i++)
      if (!strcmp(action, admin[i]))
        return true;
  return false;
}

BbsLibResult bbslib_session_open(BbsLibContext *ctx,
                                 const BbsLibSessionOptions *opts,
                                 const BbsLibSessionAdapter *adapter,
                                 BbsLibSession **out)
{
  if (!ctx || !adapter || !adapter->write || !adapter->readline || !out)
    return set_error(ctx, BBSLIB_ERR_INVALID, "missing session adapter argument");
  *out = NULL;
  BbsLibSession *bs = (BbsLibSession *)calloc(1, sizeof(*bs));
  if (!bs)
    return set_error(ctx, BBSLIB_ERR_IO, "failed to allocate bridge session");
  bs->ctx = ctx;
  bs->adapter = *adapter;
  bs->flags = opts ? opts->flags : 0;
  bs->session.fd = -1;
  bs->session.io_write = adapter->write;
  bs->session.io_readline = adapter->readline;
  bs->session.io_read = adapter->read;
  bs->session.io_flush = adapter->flush;
  bs->session.io_close = adapter->close;
  bs->session.io_user_data = adapter->user_data;
  bs->session.cfg = ctx->cfg;
  bs->session.db = ctx->db;
  bs->session.alive = 1;
  bs->session.started_at = time(NULL);
  bs->session.node_num = opts && opts->node_num > 0 ? opts->node_num : 0;
  bs->session.time_left_min = ctx->cfg.idle_timeout_sec > 0 ? ctx->cfg.idle_timeout_sec / 60 : 60;
  bs->session.current_msg_area = 1;
  bs->session.current_file_area = 1;
  bs->session.ansi = 1;
  copy_string(bs->session.ip, sizeof(bs->session.ip), opts && opts->ip ? opts->ip : "bbslib");
  pthread_mutex_init(&bs->session.chat_inbox_lock, NULL);

  if (opts && opts->handle && opts->handle[0])
  {
    if (!db_user_fetch(ctx->db, opts->handle, &bs->session.user))
    {
      free(bs);
      return set_error(ctx, BBSLIB_ERR_NOT_FOUND, "bridge user not found: %s", opts->handle);
    }
    bs->session.credits = bs->session.user.credits;
    bs->session.file_points = bs->session.user.file_points;
    bs->session.time_left_min = bs->session.user.time_limit_min > 0 ? bs->session.user.time_limit_min : bs->session.time_left_min;
  }

  *out = bs;
  return BBSLIB_OK;
}

void bbslib_session_close(BbsLibSession *session)
{
  if (!session)
    return;
  if (session->adapter.close)
    session->adapter.close(session->adapter.user_data);
  pthread_mutex_destroy(&session->session.chat_inbox_lock);
  free(session);
}

BbsLibResult bbslib_session_run_action(BbsLibSession *session, const char *action)
{
  if (!session || !action)
    return BBSLIB_ERR_INVALID;
  if (!action_allowed(action, session->flags))
    return set_error(session->ctx, BBSLIB_ERR_DENIED, "bridge action not allowed: %s", action);
  bbs_handle_action(&session->session, action);
  return BBSLIB_OK;
}

BbsLibResult bbslib_session_launch_door(BbsLibSession *session, int door_id)
{
  if (!session)
    return BBSLIB_ERR_INVALID;
  DbDoor door;
  if (!db_door_get(session->ctx->db, door_id, &door))
    return set_error(session->ctx, BBSLIB_ERR_NOT_FOUND, "door not found: %d", door_id);
  if (!acs_allows(&session->session, door.acs))
    return set_error(session->ctx, BBSLIB_ERR_DENIED, "door access denied: %d", door_id);
  if (!door_launch(&session->session, &door))
    return set_error(session->ctx, BBSLIB_ERR_IO, "door launch failed: %d", door_id);
  return BBSLIB_OK;
}

int bbslib_node_list(BbsLibContext *ctx, DbNode *out, int max_nodes)
{
  return ctx ? db_node_list(ctx->db, out, max_nodes) : -1;
}

BbsLibResult bbslib_node_lock_set(BbsLibContext *ctx, int node_num, bool locked,
                                  const char *actor)
{
  if (!ctx)
    return BBSLIB_ERR_INVALID;
  if (!db_node_lock_set(ctx->db, node_num, locked, actor ? actor : "bbslib"))
    return set_error(ctx, BBSLIB_ERR_DB, "failed to update node lock: %s", db_last_error(ctx->db));
  return BBSLIB_OK;
}

BbsLibResult bbslib_maintenance_vacuum(BbsLibContext *ctx)
{
  if (!ctx)
    return BBSLIB_ERR_INVALID;
  return db_exec(ctx->db, "VACUUM") ? BBSLIB_OK : set_error(ctx, BBSLIB_ERR_DB, "VACUUM failed: %s", db_last_error(ctx->db));
}

BbsLibResult bbslib_maintenance_reindex(BbsLibContext *ctx)
{
  if (!ctx)
    return BBSLIB_ERR_INVALID;
  return db_exec(ctx->db, "REINDEX") ? BBSLIB_OK : set_error(ctx, BBSLIB_ERR_DB, "REINDEX failed: %s", db_last_error(ctx->db));
}

BbsLibResult bbslib_maintenance_analyze(BbsLibContext *ctx)
{
  if (!ctx)
    return BBSLIB_ERR_INVALID;
  return db_exec(ctx->db, "ANALYZE") ? BBSLIB_OK : set_error(ctx, BBSLIB_ERR_DB, "ANALYZE failed: %s", db_last_error(ctx->db));
}

BbsLibResult bbslib_maintenance_integrity(BbsLibContext *ctx)
{
  if (!ctx)
    return BBSLIB_ERR_INVALID;
  return db_exec(ctx->db, "PRAGMA integrity_check") ? BBSLIB_OK : set_error(ctx, BBSLIB_ERR_DB, "integrity check failed: %s", db_last_error(ctx->db));
}

static bool quote_sql_string(const char *in, char *out, size_t cap)
{
  if (!in || !out || cap < 3)
    return false;
  size_t o = 0;
  out[o++] = '\'';
  for (const char *p = in; *p; p++)
  {
    if (o + 3 >= cap)
      return false;
    if (*p == '\'')
      out[o++] = '\'';
    out[o++] = *p;
  }
  out[o++] = '\'';
  out[o] = '\0';
  return true;
}

BbsLibResult bbslib_maintenance_backup(BbsLibContext *ctx, const char *output_path)
{
  if (!ctx || !output_path)
    return set_error(ctx, BBSLIB_ERR_INVALID, "missing backup output path");
  char quoted[1024];
  char sql[1200];
  if (!quote_sql_string(output_path, quoted, sizeof(quoted)))
    return set_error(ctx, BBSLIB_ERR_BUFFER, "backup path too long");
  snprintf(sql, sizeof(sql), "VACUUM INTO %s", quoted);
  if (!db_exec(ctx->db, sql))
    return set_error(ctx, BBSLIB_ERR_DB, "backup failed: %s", db_last_error(ctx->db));
  return BBSLIB_OK;
}

BbsLibResult bbslib_plank_status(BbsLibContext *ctx, BbsLibPlankStatus *out)
{
  if (!ctx || !out)
    return set_error(ctx, BBSLIB_ERR_INVALID, "missing PLANK status output");
  memset(out, 0, sizeof(*out));
  plank_store_t *store = plank_store_open(ctx->db);
  if (!store)
    return set_error(ctx, BBSLIB_ERR_DB, "failed to open PLANK store");
  plank_node_identity_t ident;
  if (plank_store_get_identity(store, &ident))
  {
    copy_string(out->node_name, sizeof(out->node_name), ident.node_name);
    copy_string(out->network_name, sizeof(out->network_name), ident.network_name);
    copy_string(out->node_addr, sizeof(out->node_addr), ident.node_addr);
  }
  out->peers = db_query_int(ctx->db, "SELECT COUNT(*) FROM plank_peers", 0);
  out->links = db_query_int(ctx->db, "SELECT COUNT(*) FROM plank_links", 0);
  out->areas = db_query_int(ctx->db, "SELECT COUNT(*) FROM plank_areas", 0);
  if (out->peers < 0) out->peers = 0;
  if (out->links < 0) out->links = 0;
  if (out->areas < 0) out->areas = 0;
  plank_store_close(store);
  return BBSLIB_OK;
}
