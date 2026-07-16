#include "mts.h"
#include "mts_command.h"
#include "mts_render.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HOST_HAS(field)                                                        \
  (MTS_HOST &&                                                                 \
   MTS_HOST->size >=                                                           \
       offsetof(bbs_host_api_t, field) + sizeof(MTS_HOST->field) &&            \
   MTS_HOST->field)
#define AC_RCHAT (1u << 1)
static void out(mts_session_t *s, const char *x) {
  MTS_HOST->io->write(s->host_session, x, strlen(x));
}
static void clear_afk(mts_session_t *s) {
  if (s->afk) {
    s->afk = 0;
    s->afk_replied_count = 0;
    mts_publish(s, MTS_PRESENCE, s->room_id, 0, NULL, "is back");
  }
}
static void render(mts_session_t *s, const mts_event_t *e) {
  char b[1400];
  mts_render_event(s, e, b, sizeof b);
  out(s, b);
}
static void flush(mts_session_t *s) {
  mts_event_t e[32];
  uint64_t skipped = 0;
  size_t n = mts_drain(s, e, 32, &skipped);
  if (skipped) {
    char b[128];
    snprintf(b, sizeof b,
             "\r\n[Skipped %llu public messages while you were away]\r\n",
             (unsigned long long)skipped);
    out(s, b);
  }
  for (size_t i = 0; i < n; i++)
    render(s, &e[i]);
}
static void help(mts_session_t *s) {
  size_t n;
  const mts_command_def_t *c = mts_commands(&n);
  mts_pager_t pager;
  mts_pager_begin(&pager, s, "\r\nMTS commands:\r\n");
  for (size_t i = 0; i < n; i++) {
    if (c[i].role == MTS_ROLE_SYSOP && !mts_is_sysop(s))
      continue;
    char b[256];
    snprintf(b, sizeof b, "  %-36s %s\r\n", c[i].syntax, c[i].summary);
    if (!mts_pager_line(&pager, b))
      break;
  }
  mts_pager_line(&pager, "Use /help <command> for details.");
}
static int target(mts_session_t *s, const char *q, mts_session_t **u) {
  int amb = 0;
  *u = mts_find_user(q, &amb);
  if (!*u) {
    out(s, amb ? "Ambiguous user.\r\n" : "User is not active in MTS.\r\n");
    return 0;
  }
  return 1;
}
static uint32_t target_id(mts_session_t *s, const char *q, mts_session_t **u) {
  *u = NULL;
  char *end = NULL;
  unsigned long id = strtoul(q, &end, 10);
  if (q[0] && end && !*end && id > 0 && id <= UINT32_MAX)
    return (uint32_t)id;
  int amb = 0;
  *u = mts_find_user(q, &amb);
  if (*u)
    return (*u)->user_id;
  if (amb) {
    out(s, "Ambiguous user.\r\n");
    return 0;
  }
  if (HOST_HAS(user_resolve)) {
    bbs_user_ref_t ref = {0};
    bbs_user_resolve_result_t rr =
        MTS_HOST->user_resolve(s->host_session, q, &ref);
    if (rr == BBS_USER_AMBIGUOUS) {
      out(s, "Ambiguous user.\r\n");
      return 0;
    }
    if (rr == BBS_USER_EXACT || rr == BBS_USER_UNIQUE_PREFIX)
      return ref.user_id;
  }
  out(s, "User not found.\r\n");
  return 0;
}
static mts_room_t *current_room(mts_session_t *s) {
  for (int i = 0; i < MTS.cfg.max_rooms; i++)
    if (MTS.rooms[i].active && MTS.rooms[i].id == s->room_id)
      return &MTS.rooms[i];
  return NULL;
}
static int room_admin(mts_session_t *s, mts_room_t *r) {
  return mts_is_sysop(s) || (r && r->owner == s->user_id);
}
static void identity_name(mts_session_t *s, uint32_t id, char *name,
                          size_t capacity) {
  name[0] = 0;
  pthread_mutex_lock(&MTS.mu);
  for (size_t i = 0; i < MTS.session_count; i++)
    if (MTS.sessions[i]->user_id == id) {
      snprintf(name, capacity, "%s", MTS.sessions[i]->handle);
      break;
    }
  pthread_mutex_unlock(&MTS.mu);
  if (!name[0] && HOST_HAS(user_lookup_id)) {
    bbs_user_ref_t ref = {0};
    if (MTS_HOST->user_lookup_id(s->host_session, id, &ref) == BBS_OK)
      mts_sanitize(name, capacity, ref.handle);
  }
  if (!name[0]) {
    sqlite3_stmt *q = NULL;
    sqlite3_prepare_v2(MTS.db,
                       "SELECT handle FROM mts_visits WHERE user_id=? ORDER "
                       "BY id DESC LIMIT 1",
                       -1, &q, NULL);
    sqlite3_bind_int(q, 1, id);
    if (sqlite3_step(q) == SQLITE_ROW)
      mts_sanitize(name, capacity, (const char *)sqlite3_column_text(q, 0));
    sqlite3_finalize(q);
  }
  if (!name[0])
    snprintf(name, capacity, "unknown");
}
static void expiry_text(int64_t expires, char *dst, size_t capacity) {
  if (!expires) {
    snprintf(dst, capacity, "permanent");
    return;
  }
  int64_t left = expires - mts_now();
  if (left < 0)
    left = 0;
  int64_t days = left / 86400, hours = (left % 86400) / 3600,
          minutes = (left % 3600) / 60;
  snprintf(dst, capacity, "expires=%lld (%lldd %lldh %lldm remaining)",
           (long long)expires, (long long)days, (long long)hours,
           (long long)minutes);
}
static void list_sanctions(mts_session_t *s, const char *table,
                           const char *heading, uint64_t room) {
  mts_sanction_record_t records[100];
  size_t n = mts_store_list_sanctions(&MTS, table, room, records, 100);
  mts_pager_t pager;
  mts_pager_begin(&pager, s, heading);
  for (size_t i = 0; i < n; i++) {
    char user[MTS_NAME], actor[MTS_NAME], expiry[128], line[768];
    identity_name(s, records[i].user_id, user, sizeof user);
    identity_name(s, records[i].actor_user_id, actor, sizeof actor);
    expiry_text(records[i].expires_at, expiry, sizeof expiry);
    snprintf(line, sizeof line,
             "  #%u %s; actor=#%u %s; created=%lld; %s; reason=%s\r\n",
             records[i].user_id, user, records[i].actor_user_id, actor,
             (long long)records[i].created_at, expiry, records[i].reason);
    if (!mts_pager_line(&pager, line))
      break;
  }
  if (!n)
    mts_pager_line(&pager, "  (none)");
}
static void list_audit(mts_session_t *s, size_t count) {
  mts_audit_record_t records[100];
  if (count < 1)
    count = 20;
  if (count > 100)
    count = 100;
  size_t n = mts_store_list_audit(&MTS, records, count);
  mts_pager_t pager;
  mts_pager_begin(&pager, s, "MTS moderation audit:\r\n");
  for (size_t i = 0; i < n; i++) {
    char actor[MTS_NAME], target_name[MTS_NAME], line[768];
    identity_name(s, records[i].actor_user_id, actor, sizeof actor);
    identity_name(s, records[i].target_user_id, target_name,
                  sizeof target_name);
    snprintf(line, sizeof line,
             "  created=%lld action=%s actor=#%u %s target=#%u %s room=%llu "
             "reason=%s\r\n",
             (long long)records[i].created_at, records[i].action,
             records[i].actor_user_id, actor, records[i].target_user_id,
             target_name, (unsigned long long)records[i].room_id,
             records[i].reason);
    if (!mts_pager_line(&pager, line))
      break;
  }
  if (!n)
    mts_pager_line(&pager, "  (none)");
}
static void list_relations(mts_session_t *s, const char *table) {
  char sql[160];
  snprintf(sql, sizeof sql,
           "SELECT target_user_id FROM %s WHERE owner_user_id=? ORDER BY "
           "target_user_id LIMIT 200",
           table);
  sqlite3_stmt *q = NULL;
  sqlite3_prepare_v2(MTS.db, sql, -1, &q, NULL);
  sqlite3_bind_int(q, 1, s->user_id);
  mts_pager_t pager;
  mts_pager_begin(&pager, s,
                  !strcmp(table, "mts_blocks") ? "Blocked users:\r\n"
                                               : "Ignored users:\r\n");
  while (sqlite3_step(q) == SQLITE_ROW) {
    uint32_t id = (uint32_t)sqlite3_column_int(q, 0);
    char handle[MTS_NAME] = "";
    if (HOST_HAS(user_lookup_id)) {
      bbs_user_ref_t ref = {0};
      if (MTS_HOST->user_lookup_id(s->host_session, id, &ref) == BBS_OK)
        snprintf(handle, sizeof handle, "%s", ref.handle);
    }
    if (!handle[0]) {
      sqlite3_stmt *v = NULL;
      sqlite3_prepare_v2(MTS.db,
                         "SELECT handle FROM mts_visits WHERE user_id=? ORDER "
                         "BY id DESC LIMIT 1",
                         -1, &v, NULL);
      sqlite3_bind_int(v, 1, id);
      if (sqlite3_step(v) == SQLITE_ROW)
        snprintf(handle, sizeof handle, "%s", sqlite3_column_text(v, 0));
      sqlite3_finalize(v);
    }
    char b[128];
    snprintf(b, sizeof b, "  #%u %s\r\n", id, handle[0] ? handle : "(unknown)");
    if (!mts_pager_line(&pager, b))
      break;
  }
  sqlite3_finalize(q);
}
static int action_template_valid(const char *t) {
  if (!t || strlen(t) >= 256 || strchr(t, '%'))
    return 0;
  int actor = 0, target = 0;
  for (const char *p = t; *p;) {
    const char *b = strchr(p, '{');
    if (!b)
      break;
    const char *e = strchr(b, '}');
    if (!e)
      return 0;
    size_t n = (size_t)(e - b + 1);
    if (n == 7 && !strncmp(b, "{actor}", 7))
      actor++;
    else if (n == 8 && !strncmp(b, "{target}", 8))
      target++;
    else
      return 0;
    p = e + 1;
  }
  return actor == 1 && target <= 1;
}
static void replace_token(char *out, size_t cap, const char *in,
                          const char *token, const char *value) {
  out[0] = 0;
  const char *p = in;
  size_t tn = strlen(token);
  while (*p && strlen(out) + 1 < cap) {
    const char *m = strstr(p, token);
    if (!m) {
      strncat(out, p, cap - strlen(out) - 1);
      break;
    }
    size_t n = (size_t)(m - p), avail = cap - strlen(out) - 1;
    strncat(out, p, n < avail ? n : avail);
    strncat(out, value, cap - strlen(out) - 1);
    p = m + tn;
  }
}
static void rooms(mts_session_t *s) {
  char lines[MTS_HARD_ROOMS][384];
  size_t n = 0;
  pthread_mutex_lock(&MTS.mu);
  for (int i = 0; i < MTS.cfg.max_rooms; i++)
    if (MTS.rooms[i].active && !MTS.rooms[i].is_private && n < MTS_HARD_ROOMS) {
      int count = 0;
      for (size_t j = 0; j < MTS.session_count; j++)
        if (MTS.sessions[j]->room_id == MTS.rooms[i].id)
          count++;
      snprintf(lines[n++], sizeof lines[0], "  %-20s %3d  %s",
               MTS.rooms[i].name, count, MTS.rooms[i].topic);
    }
  pthread_mutex_unlock(&MTS.mu);
  mts_pager_t pager;
  mts_pager_begin(&pager, s, "\r\nRooms:\r\n");
  for (size_t i = 0; i < n && mts_pager_line(&pager, lines[i]); i++)
    ;
}
static void who(mts_session_t *s) {
  char lines[MTS_HARD_USERS][160];
  size_t n = 0;
  pthread_mutex_lock(&MTS.mu);
  for (size_t i = 0; i < MTS.session_count; i++)
    if (MTS.sessions[i]->room_id == s->room_id && n < MTS_HARD_USERS)
      snprintf(lines[n++], sizeof lines[0], "  %s (node %d)%s",
               MTS.sessions[i]->handle, MTS.sessions[i]->node,
               MTS.sessions[i]->afk ? " [AFK]" : "");
  pthread_mutex_unlock(&MTS.mu);
  mts_pager_t pager;
  mts_pager_begin(&pager, s, "\r\nPresent:\r\n");
  for (size_t i = 0; i < n && mts_pager_line(&pager, lines[i]); i++)
    ;
}
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
static void handle_quit(mts_session_t *s, char *arg, int *running,
                        const char *line) {

  *running = 0;
  return;
}
static void handle_help(mts_session_t *s, char *arg, int *running,
                        const char *line) {

  if (*arg) {
    const mts_command_def_t *d = mts_command_find(arg);
    if (!d) {
      out(s, "Unknown command.\r\n");
      return;
    }
    char b[768];
    snprintf(b, sizeof b, "%s\r\n%s\r\n", d->syntax,
             d->detail[0] ? d->detail : d->summary);
    out(s, b);
  } else
    help(s);
  return;
}
static void handle_rooms(mts_session_t *s, char *arg, int *running,
                         const char *line) {

  if (!strcasecmp(arg, "admin")) {
    if (!mts_is_sysop(s)) {
      out(s, "Sysop authorization required.\r\n");
      return;
    }
    char lines[MTS_HARD_ROOMS][384];
    size_t count = 0;
    pthread_mutex_lock(&MTS.mu);
    for (int i = 0; i < MTS.cfg.max_rooms; i++)
      if (MTS.rooms[i].active && count < MTS_HARD_ROOMS) {
        int occ = 0;
        for (size_t j = 0; j < MTS.session_count; j++)
          if (MTS.sessions[j]->room_id == MTS.rooms[i].id)
            occ++;
        snprintf(
            lines[count++], sizeof lines[0],
            "  id=%llu name=%s owner=%u private=%d permanent=%d users=%d\r\n",
            (unsigned long long)MTS.rooms[i].id, MTS.rooms[i].name,
            MTS.rooms[i].owner, MTS.rooms[i].is_private, MTS.rooms[i].permanent,
            occ);
      }
    pthread_mutex_unlock(&MTS.mu);
    mts_pager_t pager;
    mts_pager_begin(&pager, s, "\r\nRoom administration:\r\n");
    for (size_t i = 0; i < count && mts_pager_line(&pager, lines[i]); i++)
      ;
    return;
  }
  rooms(s);
  return;
}
static void handle_who(mts_session_t *s, char *arg, int *running,
                       const char *line) {

  who(s);
  return;
}
static void handle_join(mts_session_t *s, char *arg, int *running,
                        const char *line) {

  mts_room_t *r = mts_find_room(arg);
  if (!r) {
    out(s, "No such room.\r\n");
    return;
  }
  uint64_t old = s->room_id;
  if (!mts_join(s, r->id))
    out(s, "You may not enter that room.\r\n");
  else
    mts_publish(s, MTS_PRESENCE, old, 0, NULL, "left the room");
  return;
}
static void handle_leave(mts_session_t *s, char *arg, int *running,
                         const char *line) {

  if (s->room_id == 1)
    out(s, "You are already in Lobby.\r\n");
  else {
    mts_publish(s, MTS_PRESENCE, s->room_id, 0, NULL, "left the room");
    mts_join(s, 1);
  }
  return;
}
static void handle_topic(mts_session_t *s, char *arg, int *running,
                         const char *line) {

  mts_room_t *r = NULL;
  pthread_mutex_lock(&MTS.mu);
  for (int i = 0; i < MTS.cfg.max_rooms; i++)
    if (MTS.rooms[i].active && MTS.rooms[i].id == s->room_id)
      r = &MTS.rooms[i];
  if (*arg && (mts_is_sysop(s) || (r && r->owner == s->user_id))) {
    mts_sanitize(r->topic, sizeof r->topic, arg);
    mts_store_save_room(&MTS, r);
  }
  char b[384];
  snprintf(b, sizeof b, "Topic: %s\r\n", r ? r->topic : "");
  pthread_mutex_unlock(&MTS.mu);
  out(s, b);
  return;
}
static void handle_msg(mts_session_t *s, char *arg, int *running,
                       const char *line) {

  char *sp = strchr(arg, ' ');
  if (!sp) {
    out(s, "Usage: /msg <user> <message>\r\n");
    return;
  }
  *sp++ = 0;
  mts_session_t *u;
  if (!target(s, arg, &u))
    return;
  if (mts_store_has_relation(&MTS, "mts_blocks", u->user_id, s->user_id)) {
    out(s, "Message could not be delivered.\r\n");
    return;
  }
  s->last_private = u->user_id;
  clear_afk(s);
  mts_publish(s, MTS_PRIVATE, 0, u->user_id, u->handle, sp);
  out(s, "[private message sent]\r\n");
  return;
}
static void handle_reply(mts_session_t *s, char *arg, int *running,
                         const char *line) {

  mts_session_t *u = NULL;
  pthread_mutex_lock(&MTS.mu);
  for (size_t i = 0; i < MTS.session_count; i++)
    if (MTS.sessions[i]->user_id == s->last_private)
      u = MTS.sessions[i];
  pthread_mutex_unlock(&MTS.mu);
  if (!u || !*arg) {
    out(s, "No active private correspondent.\r\n");
    return;
  }
  mts_publish(s, MTS_PRIVATE, 0, u->user_id, u->handle, arg);
  clear_afk(s);
  return;
}
static void handle_me(mts_session_t *s, char *arg, int *running,
                      const char *line) {

  clear_afk(s);
  mts_publish(s, MTS_ACTION, s->room_id, 0, NULL, arg);
  return;
}
static void handle_afk(mts_session_t *s, char *arg, int *running,
                       const char *line) {

  s->afk = !s->afk;
  mts_sanitize(s->afk_message, sizeof s->afk_message, arg);
  s->afk_epoch++;
  s->afk_replied_count = 0;
  out(s, s->afk ? "You are now AFK.\r\n" : "You are back.\r\n");
  return;
}
static void handle_ignore(mts_session_t *s, char *arg, int *running,
                          const char *line) {

  mts_session_t *u;
  if (!target(s, arg, &u))
    return;
  int add = strncmp(line, "/un", 3) != 0;
  const char *t = strstr(line, "block") ? "mts_blocks" : "mts_ignores";
  mts_store_relation(&MTS, t, s->user_id, u->user_id, add);
  out(s, add ? "Safety setting added.\r\n" : "Safety setting removed.\r\n");
  return;
}
static void handle_ignores(mts_session_t *s, char *arg, int *running,
                           const char *line) {

  list_relations(s, "mts_ignores");
  return;
}
static void handle_blocks(mts_session_t *s, char *arg, int *running,
                          const char *line) {

  list_relations(s, "mts_blocks");
  return;
}
static void handle_settings(mts_session_t *s, char *arg, int *running,
                            const char *line) {

  if (!strcasecmp(arg, "reset")) {
    s->prefs = (mts_preferences_t){.timestamps = 1,
                                   .joins = 1,
                                   .actions = 1,
                                   .echo = 1,
                                   .color = 1,
                                   .allow_chat_requests = 1,
                                   .directory_visible = 1,
                                   .last_seen_visible = 1,
                                   .profile_visible = 1};
    snprintf(s->prefs.theme, sizeof s->prefs.theme, "default");
    mts_store_save_preferences(&MTS, s);
    out(s, "Settings reset to defaults.\r\n");
    return;
  }
  if (!*arg) {
    char b[768];
    snprintf(b, sizeof b,
             "timestamps=%s joins=%s actions=%s bell=%s echo=%s color=%s "
             "theme=%s chat_requests=%s directory=%s last_seen=%s profile=%s "
             "default_room=%llu greeting=%s farewell=%s\r\n",
             s->prefs.timestamps ? "on" : "off", s->prefs.joins ? "on" : "off",
             s->prefs.actions ? "on" : "off", s->prefs.bell ? "on" : "off",
             s->prefs.echo ? "on" : "off", s->prefs.color ? "on" : "off",
             s->prefs.theme, s->prefs.allow_chat_requests ? "on" : "off",
             s->prefs.directory_visible ? "on" : "off",
             s->prefs.last_seen_visible ? "on" : "off",
             s->prefs.profile_visible ? "on" : "off",
             (unsigned long long)s->prefs.default_room, s->prefs.greeting,
             s->prefs.farewell);
    out(s, b);
    return;
  }
  char *k = arg, *v = strchr(arg, ' ');
  if (!v) {
    out(s, "Usage: /settings <timestamps|joins|color|bell|chat_requests> "
           "<on|off>; privacy keys: directory, last_seen.\r\n");
    return;
  }
  *v++ = 0;
  int yes = !strcasecmp(v, "on") || !strcasecmp(v, "yes") || !strcmp(v, "1");
  if (!strcmp(k, "timestamps"))
    s->prefs.timestamps = yes;
  else if (!strcmp(k, "joins"))
    s->prefs.joins = yes;
  else if (!strcmp(k, "actions"))
    s->prefs.actions = yes;
  else if (!strcmp(k, "echo"))
    s->prefs.echo = yes;
  else if (!strcmp(k, "color"))
    s->prefs.color = yes;
  else if (!strcmp(k, "bell"))
    s->prefs.bell = yes;
  else if (!strcmp(k, "chat_requests"))
    s->prefs.allow_chat_requests = yes;
  else if (!strcmp(k, "directory"))
    s->prefs.directory_visible = yes;
  else if (!strcmp(k, "last_seen"))
    s->prefs.last_seen_visible = yes;
  else if (!strcmp(k, "profile"))
    s->prefs.profile_visible = yes;
  else if (!strcmp(k, "theme")) {
    if (strcasecmp(v, "default") && strcasecmp(v, "ocean") &&
        strcasecmp(v, "amber") && strcasecmp(v, "high-contrast") &&
        strcasecmp(v, "mono")) {
      out(s, "Unknown theme.\r\n");
      return;
    }
    snprintf(s->prefs.theme, sizeof s->prefs.theme, "%s", v);
  } else if (!strcmp(k, "default-room")) {
    mts_room_t *r = mts_find_room(v);
    if (!r || !mts_join(s, r->id)) {
      out(s, "Default room is not accessible.\r\n");
      return;
    }
    s->prefs.default_room = r->id;
  } else if (!strcmp(k, "greeting"))
    mts_sanitize(s->prefs.greeting, sizeof s->prefs.greeting, v);
  else if (!strcmp(k, "farewell"))
    mts_sanitize(s->prefs.farewell, sizeof s->prefs.farewell, v);
  else {
    out(s, "Unknown setting.\r\n");
    return;
  }
  mts_store_save_preferences(&MTS, s);
  out(s, "Setting saved.\r\n");
  return;
}
static void handle_create(mts_session_t *s, char *arg, int *running,
                          const char *line) {

  if (!MTS.cfg.allow_user_rooms || !*arg) {
    out(s, "Room creation is unavailable or missing a name.\r\n");
    return;
  }
  pthread_mutex_lock(&MTS.mu);
  int slot = -1, owned = 0;
  for (int i = 0; i < MTS.cfg.max_rooms; i++) {
    if (!MTS.rooms[i].active && slot < 0)
      slot = i;
    if (MTS.rooms[i].active && MTS.rooms[i].owner == s->user_id)
      owned++;
  }
  if (slot < 0 || owned >= MTS.cfg.max_rooms_per_user) {
    pthread_mutex_unlock(&MTS.mu);
    out(s, "Room limit reached.\r\n");
    return;
  }
  mts_room_t *r = &MTS.rooms[slot];
  memset(r, 0, sizeof *r);
  r->id = MTS.next_room_id++;
  r->owner = s->user_id;
  r->active = 1;
  mts_sanitize(r->name, sizeof r->name, arg);
  for (size_t i = 0; r->name[i] && i + 1 < sizeof r->normalized; i++)
    r->normalized[i] = (char)tolower((unsigned char)r->name[i]);
  mts_store_save_room(&MTS, r);
  s->room_id = r->id;
  pthread_mutex_unlock(&MTS.mu);
  out(s, "Room created and joined.\r\n");
  return;
}
static void handle_public(mts_session_t *s, char *arg, int *running,
                          const char *line) {

  pthread_mutex_lock(&MTS.mu);
  for (int i = 0; i < MTS.cfg.max_rooms; i++)
    if (MTS.rooms[i].id == s->room_id &&
        (mts_is_sysop(s) || MTS.rooms[i].owner == s->user_id) &&
        MTS.rooms[i].id != 1) {
      MTS.rooms[i].is_private = !strcmp(line, "/private");
      mts_store_save_room(&MTS, &MTS.rooms[i]);
    }
  pthread_mutex_unlock(&MTS.mu);
  out(s, "Room visibility updated.\r\n");
  return;
}
static void handle_rename(mts_session_t *s, char *arg, int *running,
                          const char *line) {

  mts_room_t *r = current_room(s);
  if (!r || r->id == 1 || !room_admin(s, r) || !*arg) {
    out(s, "Usage: /rename <new-name> (room owner only; Lobby is "
           "immutable).\r\n");
    return;
  }
  if (mts_find_room(arg)) {
    out(s, "A room with that name already exists.\r\n");
    return;
  }
  char name[MTS_NAME], norm[MTS_NAME];
  mts_sanitize(name, sizeof name, arg);
  size_t o = 0;
  for (size_t i = 0; name[i] && o + 1 < sizeof norm; i++)
    if (isalnum((unsigned char)name[i]))
      norm[o++] = (char)tolower((unsigned char)name[i]);
  norm[o] = 0;
  if (!norm[0]) {
    out(s, "Invalid room name.\r\n");
    return;
  }
  snprintf(r->name, sizeof r->name, "%s", name);
  snprintf(r->normalized, sizeof r->normalized, "%s", norm);
  mts_store_save_room(&MTS, r);
  out(s, "Room renamed.\r\n");
  return;
}
static void handle_invite(mts_session_t *s, char *arg, int *running,
                          const char *line) {

  mts_room_t *r = current_room(s);
  if (!r || r->id == 1 || !room_admin(s, r)) {
    out(s, "Room owner authorization required.\r\n");
    return;
  }
  char *reason = NULL;
  if (!strcmp(line, "/transfer")) {
    reason = strchr(arg, ' ');
    if (reason) {
      *reason++ = 0;
      while (*reason == ' ')
        reason++;
    }
    if (!reason || !*reason) {
      out(s, "Usage: /transfer <user> <reason>\r\n");
      return;
    }
  }
  mts_session_t *u = NULL;
  uint32_t uid = target_id(s, arg, &u);
  if (!uid)
    return;
  if (!strcmp(line, "/transfer")) {
    r->owner = uid;
    mts_store_save_room(&MTS, r);
    mts_store_audit(&MTS, s->user_id, uid, r->id, "transfer", reason);
    out(s, "Room ownership transferred.\r\n");
  } else {
    int add = !strcmp(line, "/invite");
    mts_store_invitation(&MTS, r->id, uid, s->user_id, add);
    if (add && u)
      mts_publish(s, MTS_PRIVATE, 0, uid, u->handle,
                  "You were invited to a private room.");
    out(s, add ? "Invitation added.\r\n" : "Invitation removed.\r\n");
  }
  return;
}
static void handle_announce(mts_session_t *s, char *arg, int *running,
                            const char *line) {

  if (!mts_is_sysop(s)) {
    out(s, "Sysop authorization required.\r\n");
    return;
  }
  if (!strcmp(line, "/announce"))
    mts_publish(s, MTS_ANNOUNCEMENT, 0, 0, NULL, arg);
  else if (!strcmp(line, "/chatlock")) {
    if (!MTS.chat_locked && !*arg) {
      out(s, "Usage: /chatlock <reason> when locking\r\n");
      return;
    }
    MTS.chat_locked = !MTS.chat_locked;
    mts_sanitize(MTS.chat_lock_reason, sizeof MTS.chat_lock_reason, arg);
    out(s, MTS.chat_locked ? "MTS locked.\r\n" : "MTS unlocked.\r\n");
  } else if (!strcmp(line, "/close")) {
    char *reason = strchr(arg, ' ');
    if (!reason || !reason[1]) {
      out(s, "Usage: /close <room> <reason>\r\n");
      return;
    }
    *reason++ = 0;
    mts_room_t *r = mts_find_room(arg);
    if (!r || r->id == 1) {
      out(s, "No closable room found.\r\n");
      return;
    }
    uint64_t id = r->id;
    for (size_t i = 0; i < MTS.session_count; i++)
      if (MTS.sessions[i]->room_id == id)
        mts_join(MTS.sessions[i], 1);
    r->active = 0;
    mts_store_delete_room(&MTS, id);
    mts_store_audit(&MTS, s->user_id, 0, id, "close", reason);
    out(s, "Room closed.\r\n");
  } else {
    char *end = NULL;
    unsigned long count = *arg ? strtoul(arg, &end, 10) : 20;
    if ((*arg && (!end || *end)) || count < 1 || count > 100) {
      out(s, "Usage: /mitsaudit [1-100]\r\n");
      return;
    }
    list_audit(s, (size_t)count);
    return;
  }
  if (strcmp(line, "/close"))
    mts_store_audit(&MTS, s->user_id, 0, s->room_id, line, arg);
  if (HOST_HAS(audit))
    MTS_HOST->audit(s->host_session, line, arg);
  return;
}
static void handle_profile(mts_session_t *s, char *arg, int *running,
                           const char *line) {

  sqlite3_stmt *q = NULL;
  if (!strncmp(arg, "set ", 4)) {
    char bio[512];
    mts_sanitize(bio, sizeof bio, arg + 4);
    sqlite3_prepare_v2(
        MTS.db, "REPLACE INTO mts_profiles VALUES(?,?,strftime('%s','now'))",
        -1, &q, NULL);
    sqlite3_bind_int(q, 1, s->user_id);
    sqlite3_bind_text(q, 2, bio, -1, SQLITE_TRANSIENT);
    sqlite3_step(q);
    sqlite3_finalize(q);
    out(s, "Profile saved.\r\n");
    return;
  }
  uint32_t uid = s->user_id;
  mts_session_t *u = NULL;
  if (*arg && !target(s, arg, &u))
    return;
  if (u)
    uid = u->user_id;
  if (u && u != s && !mts_is_sysop(s) && !u->prefs.profile_visible) {
    out(s, "No profile.\r\n");
    return;
  }
  sqlite3_prepare_v2(MTS.db, "SELECT bio FROM mts_profiles WHERE user_id=?", -1,
                     &q, NULL);
  sqlite3_bind_int(q, 1, uid);
  if (sqlite3_step(q) == SQLITE_ROW) {
    char b[640];
    snprintf(b, sizeof b, "Profile: %s\r\n", sqlite3_column_text(q, 0));
    out(s, b);
  } else
    out(s, "No profile.\r\n");
  sqlite3_finalize(q);
  return;
}
static void handle_history(mts_session_t *s, char *arg, int *running,
                           const char *line) {

  if (!MTS.cfg.persist_history) {
    out(s, "Persistent room history is disabled.\r\n");
    return;
  }
  sqlite3_stmt *q = NULL;
  sqlite3_prepare_v2(MTS.db,
                     "SELECT sender_handle,text FROM mts_history WHERE "
                     "room_id=? ORDER BY id DESC LIMIT 20",
                     -1, &q, NULL);
  sqlite3_bind_int64(q, 1, s->room_id);
  mts_pager_t pager;
  mts_pager_begin(&pager, s, "Recent history:\r\n");
  while (sqlite3_step(q) == SQLITE_ROW) {
    char b[700];
    snprintf(b, sizeof b, "  %s: %s\r\n", sqlite3_column_text(q, 0),
             sqlite3_column_text(q, 1));
    if (!mts_pager_line(&pager, b))
      break;
  }
  sqlite3_finalize(q);
  return;
}
static void handle_watch(mts_session_t *s, char *arg, int *running,
                         const char *line) {

  if (!*arg) {
    out(s, "Usage: /watch <public-room>\r\n");
    return;
  }
  if (!strcasecmp(arg, "all")) {
    if (!mts_is_sysop(s)) {
      out(s, "/watch all is sysop-only.\r\n");
      return;
    }
    s->watch_all = 1;
    s->watch_room = 0;
    out(s, "Watching all public rooms.\r\n");
    return;
  }
  mts_room_t *r = mts_find_room(arg);
  if (!r || r->is_private) {
    out(s, "Only public rooms may be watched.\r\n");
    return;
  }
  s->watch_room = r->id;
  out(s, "Room watch enabled.\r\n");
  return;
}
static void handle_unwatch(mts_session_t *s, char *arg, int *running,
                           const char *line) {

  s->watch_room = 0;
  s->watch_all = 0;
  out(s, "Room watch disabled.\r\n");
  return;
}
static void handle_chat(mts_session_t *s, char *arg, int *running,
                        const char *line) {

  mts_session_t *u;
  if (!target(s, arg, &u))
    return;
  if (!u->prefs.allow_chat_requests) {
    out(s, "That user is not accepting focused chat requests.\r\n");
    return;
  }
  if (u == s) {
    out(s, "You cannot request focused chat with yourself.\r\n");
    return;
  }
  u->focus_requested_by = s->user_id;
  mts_publish(s, MTS_PRIVATE, 0, u->user_id, u->handle,
              "Focused chat requested. Use /accept or /decline.");
  out(s, "Focused chat request sent.\r\n");
  return;
}
static void handle_accept(mts_session_t *s, char *arg, int *running,
                          const char *line) {

  if (!s->focus_requested_by) {
    out(s, "No focused chat request is pending.\r\n");
    return;
  }
  mts_session_t *u = NULL;
  pthread_mutex_lock(&MTS.mu);
  for (size_t i = 0; i < MTS.session_count; i++)
    if (MTS.sessions[i]->user_id == s->focus_requested_by)
      u = MTS.sessions[i];
  if (u) {
    s->focused_user = u->user_id;
    u->focused_user = s->user_id;
  }
  s->focus_requested_by = 0;
  pthread_mutex_unlock(&MTS.mu);
  if (!u) {
    out(s, "The requester is no longer online.\r\n");
    return;
  }
  mts_publish(s, MTS_PRIVATE, 0, u->user_id, u->handle,
              "Focused chat accepted.");
  out(s, "Focused chat started. /decline ends it.\r\n");
  return;
}
static void handle_decline(mts_session_t *s, char *arg, int *running,
                           const char *line) {

  if (s->focus_requested_by) {
    uint32_t requester = s->focus_requested_by;
    s->focus_requested_by = 0;
    mts_session_t *u = NULL;
    pthread_mutex_lock(&MTS.mu);
    for (size_t i = 0; i < MTS.session_count; i++)
      if (MTS.sessions[i]->user_id == requester)
        u = MTS.sessions[i];
    pthread_mutex_unlock(&MTS.mu);
    if (u)
      mts_publish(s, MTS_PRIVATE, 0, u->user_id, u->handle,
                  "Focused chat request declined.");
    out(s, "Focused chat request declined.\r\n");
    return;
  }
  uint32_t old = s->focused_user;
  s->focused_user = 0;
  pthread_mutex_lock(&MTS.mu);
  for (size_t i = 0; i < MTS.session_count; i++)
    if (MTS.sessions[i]->user_id == old)
      MTS.sessions[i]->focused_user = 0;
  pthread_mutex_unlock(&MTS.mu);
  out(s, "Focused chat ended.\r\n");
  return;
}
static void handle_actions(mts_session_t *s, char *arg, int *running,
                           const char *line) {

  mts_pager_t pager;
  mts_pager_begin(&pager, s, "Actions:\r\n");
  sqlite3_stmt *q = NULL;
  sqlite3_prepare_v2(
      MTS.db, "SELECT name FROM mts_actions WHERE enabled=1 ORDER BY name", -1,
      &q, NULL);
  while (sqlite3_step(q) == SQLITE_ROW) {
    char b[128];
    snprintf(b, sizeof b, "  %s\r\n", sqlite3_column_text(q, 0));
    if (!mts_pager_line(&pager, b))
      break;
  }
  sqlite3_finalize(q);
  return;
}
static void handle_action(mts_session_t *s, char *arg, int *running,
                          const char *line) {

  if (!strncmp(arg, "add ", 4) || !strncmp(arg, "del ", 4)) {
    if (!mts_is_sysop(s)) {
      out(s, "Sysop authorization required.\r\n");
      return;
    }
    int add = !strncmp(arg, "add ", 4);
    char *p = arg + 4, *templ = strchr(p, ' ');
    if (add && !templ) {
      out(s, "Usage: /action add <name> <template>\r\n");
      return;
    }
    if (templ)
      *templ++ = 0;
    for (char *x = p; *x; x++)
      if (!isalnum((unsigned char)*x) && *x != '_') {
        out(s, "Invalid action name.\r\n");
        return;
      }
    sqlite3_stmt *q = NULL;
    if (add) {
      char safe[256];
      mts_sanitize(safe, sizeof safe, templ);
      if (strcmp(safe, templ) || !action_template_valid(safe)) {
        out(s, "Template must contain {actor} exactly once, optional {target} "
               "once, and no other placeholders or percent sequences.\r\n");
        return;
      }
      sqlite3_prepare_v2(MTS.db,
                         "INSERT INTO "
                         "mts_actions(name,template,enabled,created_by,"
                         "updated_at) VALUES(?,?,1,?,strftime('%s','now')) "
                         "ON CONFLICT(name) DO UPDATE SET "
                         "template=excluded.template,enabled=1,created_by="
                         "excluded.created_by,updated_at=excluded.updated_at",
                         -1, &q, NULL);
      sqlite3_bind_text(q, 1, p, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(q, 2, safe, -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(q, 3, s->user_id);
    } else {
      sqlite3_prepare_v2(
          MTS.db,
          "UPDATE mts_actions SET enabled=0,updated_at=strftime('%s','now') "
          "WHERE name=?",
          -1, &q, NULL);
      sqlite3_bind_text(q, 1, p, -1, SQLITE_TRANSIENT);
    }
    sqlite3_step(q);
    sqlite3_finalize(q);
    out(s, add ? "Action saved.\r\n" : "Action disabled.\r\n");
    return;
  }
  char *name = arg, *targ = strchr(arg, ' ');
  if (targ)
    *targ++ = 0;
  sqlite3_stmt *q = NULL;
  sqlite3_prepare_v2(
      MTS.db, "SELECT template FROM mts_actions WHERE name=? AND enabled=1", -1,
      &q, NULL);
  sqlite3_bind_text(q, 1, name, -1, SQLITE_TRANSIENT);
  if (sqlite3_step(q) != SQLITE_ROW) {
    sqlite3_finalize(q);
    out(s, "Unknown action.\r\n");
    return;
  }
  char rendered[512];
  snprintf(rendered, sizeof rendered, "%s", sqlite3_column_text(q, 0));
  sqlite3_finalize(q);
  if (!action_template_valid(rendered)) {
    out(s, "Stored action template is invalid.\r\n");
    return;
  }
  char actor_expanded[512], final[512];
  replace_token(actor_expanded, sizeof actor_expanded, rendered, "{actor}", "");
  replace_token(final, sizeof final, actor_expanded, "{target}",
                targ ? targ : "someone");
  mts_sanitize(rendered, sizeof rendered, final);
  mts_publish(s, MTS_ACTION, s->room_id, 0, NULL, rendered);
  return;
}
static void handle_members(mts_session_t *s, char *arg, int *running,
                           const char *line) {

  sqlite3_stmt *q = NULL;
  sqlite3_prepare_v2(
      MTS.db,
      "SELECT DISTINCT v.user_id,v.handle FROM mts_visits v LEFT JOIN "
      "mts_preferences p ON p.user_id=v.user_id WHERE "
      "COALESCE(p.directory_visible,1)=1 ORDER BY lower(v.handle) LIMIT 200",
      -1, &q, NULL);
  mts_pager_t pager;
  mts_pager_begin(&pager, s, "MTS members:\r\n");
  while (sqlite3_step(q) == SQLITE_ROW) {
    char b[160];
    snprintf(b, sizeof b, "  %s (#%d)\r\n", sqlite3_column_text(q, 1),
             sqlite3_column_int(q, 0));
    if (!mts_pager_line(&pager, b))
      break;
  }
  sqlite3_finalize(q);
  return;
}
static void handle_recent(mts_session_t *s, char *arg, int *running,
                          const char *line) {

  sqlite3_stmt *q = NULL;
  sqlite3_prepare_v2(
      MTS.db,
      "SELECT v.handle,MAX(COALESCE(v.left_at,v.entered_at)) FROM mts_visits "
      "v LEFT JOIN mts_preferences p ON p.user_id=v.user_id WHERE "
      "COALESCE(p.directory_visible,1)=1 AND "
      "COALESCE(p.last_seen_visible,1)=1 GROUP BY v.user_id ORDER BY 2 DESC "
      "LIMIT 20",
      -1, &q, NULL);
  mts_pager_t pager;
  mts_pager_begin(&pager, s, "Recently seen:\r\n");
  while (sqlite3_step(q) == SQLITE_ROW) {
    char b[192];
    snprintf(b, sizeof b, "  %s at %lld\r\n", sqlite3_column_text(q, 0),
             (long long)sqlite3_column_int64(q, 1));
    if (!mts_pager_line(&pager, b))
      break;
  }
  sqlite3_finalize(q);
  return;
}
static void handle_search(mts_session_t *s, char *arg, int *running,
                          const char *line) {

  if (!MTS.cfg.persist_history || !*arg) {
    out(s, "Usage: /search <room-history text> (history must be enabled).\r\n");
    return;
  }
  sqlite3_stmt *q = NULL;
  sqlite3_prepare_v2(
      MTS.db,
      "SELECT sender_handle,text FROM mts_history WHERE room_id=? AND text "
      "LIKE '%'||?||'%' ORDER BY id DESC LIMIT 20",
      -1, &q, NULL);
  sqlite3_bind_int64(q, 1, s->room_id);
  sqlite3_bind_text(q, 2, arg, -1, SQLITE_TRANSIENT);
  mts_pager_t pager;
  mts_pager_begin(&pager, s, "History search results:\r\n");
  while (sqlite3_step(q) == SQLITE_ROW) {
    char b[700];
    snprintf(b, sizeof b, "  %s: %s\r\n", sqlite3_column_text(q, 0),
             sqlite3_column_text(q, 1));
    if (!mts_pager_line(&pager, b))
      break;
  }
  sqlite3_finalize(q);
  return;
}
static void handle_stats(mts_session_t *s, char *arg, int *running,
                         const char *line) {

  sqlite3_stmt *q = NULL;
  sqlite3_prepare_v2(MTS.db,
                     "SELECT (SELECT COUNT(*) FROM mts_history WHERE "
                     "sender_user_id=?),(SELECT COUNT(*) FROM mts_visits "
                     "WHERE user_id=?),(SELECT COUNT(DISTINCT room_id) FROM "
                     "mts_history WHERE sender_user_id=?)",
                     -1, &q, NULL);
  sqlite3_bind_int(q, 1, s->user_id);
  sqlite3_bind_int(q, 2, s->user_id);
  sqlite3_bind_int(q, 3, s->user_id);
  if (sqlite3_step(q) == SQLITE_ROW) {
    char b[256];
    snprintf(b, sizeof b, "Messages: %d  Sessions: %d  Rooms visited: %d\r\n",
             sqlite3_column_int(q, 0), sqlite3_column_int(q, 1),
             sqlite3_column_int(q, 2));
    out(s, b);
  }
  sqlite3_finalize(q);
  return;
}
static void handle_bans(mts_session_t *s, char *arg, int *running,
                        const char *line) {

  mts_room_t *r = current_room(s);
  if (!room_admin(s, r)) {
    out(s, "Room moderator authorization required.\r\n");
    return;
  }
  int bans = !strcmp(line, "/bans");
  list_sanctions(s, bans ? "mts_room_bans" : "mts_room_mutes",
                 bans ? "Active room bans:\r\n" : "Active room mutes:\r\n",
                 s->room_id);
  if (mts_is_sysop(s))
    list_sanctions(
        s, bans ? "mts_global_bans" : "mts_global_mutes",
        bans ? "Active global bans:\r\n" : "Active global mutes:\r\n", 0);
  return;
}
static void handle_kick(mts_session_t *s, char *arg, int *running,
                        const char *line) {

  int global = strstr(line, "global") != NULL,
      remove = strstr(line, "un") != NULL, isban = strstr(line, "ban") != NULL;
  char *whoarg = arg, *rest = strchr(arg, ' ');
  if (rest)
    *rest++ = 0;
  mts_session_t *u = NULL;
  uint32_t uid = target_id(s, whoarg, &u);
  if (!uid)
    return;
  mts_room_t *r = current_room(s);
  if ((global && !mts_is_sysop(s)) || (!global && !room_admin(s, r))) {
    out(s, global ? "Sysop authorization required.\r\n"
                  : "Room moderator authorization required.\r\n");
    return;
  }
  if (!strcmp(line, "/kick")) {
    if (!rest || !*rest) {
      out(s, "Usage: /kick <user> <reason>\r\n");
      return;
    }
    if (!u) {
      out(s, "Kick requires an active MTS user.\r\n");
      return;
    }
    mts_publish(u, MTS_MODERATION, u->room_id, 0, NULL,
                "was removed by a moderator");
    mts_join(u, 1);
    mts_store_audit(&MTS, s->user_id, uid, s->room_id, "kick",
                    rest ? rest : "");
    out(s, "User moved to Lobby.\r\n");
    return;
  }
  const char *table = global ? (isban ? "mts_global_bans" : "mts_global_mutes")
                             : (isban ? "mts_room_bans" : "mts_room_mutes");
  int64_t expires = 0;
  char *reason = "";
  if (!remove && rest && *rest) {
    char *next = strchr(rest, ' ');
    if (next)
      *next++ = 0;
    int ok = 0;
    int64_t duration = mts_parse_duration(rest, &ok);
    if (ok) {
      expires = mts_now() + duration;
      reason = next ? next : "";
    } else
      reason = rest;
  }
  if (!remove && !*reason) {
    out(s, "A non-empty moderation reason is required.\r\n");
    return;
  }
  mts_store_sanction(&MTS, table, s->room_id, uid, s->user_id, reason, expires,
                     !remove);
  if (u) {
    u->muted = mts_store_is_sanctioned(&MTS, "mts_global_mutes", 0, uid) ||
               mts_store_is_sanctioned(&MTS, "mts_room_mutes", u->room_id, uid);
    if (!remove && isban) {
      mts_publish(u, MTS_MODERATION, u->room_id, 0, NULL,
                  "was banned by a moderator");
      mts_join(u, 1);
    }
  }
  mts_store_audit(&MTS, s->user_id, uid, s->room_id, line, reason);
  out(s, remove ? "Sanction removed.\r\n" : "Sanction applied.\r\n");
  return;
}
static void bind_command_handlers(void) {
  mts_command_bind("quit", handle_quit);
  mts_command_bind("q", handle_quit);
  mts_command_bind("exit", handle_quit);
  mts_command_bind("help", handle_help);
  mts_command_bind("?", handle_help);
  mts_command_bind("rooms", handle_rooms);
  mts_command_bind("who", handle_who);
  mts_command_bind("join", handle_join);
  mts_command_bind("leave", handle_leave);
  mts_command_bind("topic", handle_topic);
  mts_command_bind("msg", handle_msg);
  mts_command_bind("m", handle_msg);
  mts_command_bind("reply", handle_reply);
  mts_command_bind("//", handle_reply);
  mts_command_bind("me", handle_me);
  mts_command_bind("afk", handle_afk);
  mts_command_bind("ignore", handle_ignore);
  mts_command_bind("unignore", handle_ignore);
  mts_command_bind("block", handle_ignore);
  mts_command_bind("unblock", handle_ignore);
  mts_command_bind("ignores", handle_ignores);
  mts_command_bind("blocks", handle_blocks);
  mts_command_bind("settings", handle_settings);
  mts_command_bind("create", handle_create);
  mts_command_bind("public", handle_public);
  mts_command_bind("private", handle_public);
  mts_command_bind("rename", handle_rename);
  mts_command_bind("invite", handle_invite);
  mts_command_bind("uninvite", handle_invite);
  mts_command_bind("transfer", handle_invite);
  mts_command_bind("announce", handle_announce);
  mts_command_bind("chatlock", handle_announce);
  mts_command_bind("close", handle_announce);
  mts_command_bind("mitsaudit", handle_announce);
  mts_command_bind("profile", handle_profile);
  mts_command_bind("history", handle_history);
  mts_command_bind("watch", handle_watch);
  mts_command_bind("unwatch", handle_unwatch);
  mts_command_bind("chat", handle_chat);
  mts_command_bind("accept", handle_accept);
  mts_command_bind("decline", handle_decline);
  mts_command_bind("actions", handle_actions);
  mts_command_bind("action", handle_action);
  mts_command_bind("members", handle_members);
  mts_command_bind("recent", handle_recent);
  mts_command_bind("search", handle_search);
  mts_command_bind("stats", handle_stats);
  mts_command_bind("bans", handle_bans);
  mts_command_bind("mutes", handle_bans);
  mts_command_bind("kick", handle_kick);
  mts_command_bind("mute", handle_kick);
  mts_command_bind("ban", handle_kick);
  mts_command_bind("unmute", handle_kick);
  mts_command_bind("unban", handle_kick);
  mts_command_bind("globalmute", handle_kick);
  mts_command_bind("globalban", handle_kick);
  mts_command_bind("unglobalmute", handle_kick);
  mts_command_bind("unglobalban", handle_kick);
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
static void command(mts_session_t *s, char *line, int *running) {
  char *arg = strchr(line, ' ');
  if (arg) {
    *arg++ = 0;
    while (*arg == ' ')
      arg++;
  } else
    arg = line + strlen(line);
  for (char *p = line; *p; p++)
    *p = (char)tolower((unsigned char)*p);
  const char *lookup = !strcmp(line, "//") ? "//" : line + 1;
  const mts_command_def_t *def = mts_command_find(lookup);
  if (!def || !def->handler) {
    out(s, "Unknown command. Type /help.\r\n");
    return;
  }
  if (def->role == MTS_ROLE_SYSOP && !mts_is_sysop(s)) {
    out(s, "Sysop authorization required.\r\n");
    return;
  }
  int argc = 0, in_word = 0;
  for (const char *p = arg; *p; p++) {
    if (isspace((unsigned char)*p))
      in_word = 0;
    else if (!in_word) {
      argc++;
      in_word = 1;
    }
  }
  if (argc < def->min_args || (def->max_args >= 0 && argc > def->max_args)) {
    char usage[256];
    snprintf(usage, sizeof usage, "Usage: %s\r\n", def->syntax);
    out(s, usage);
    return;
  }
  def->handler(s, arg, running, line);
}
static int cleanup_instance(mts_session_t *s, int announce);
static bbs_rc_t enter(void *v, bbs_session_t *hs) {
  mts_session_t *s = v;
  s->host_session = hs;
  s->user_id = MTS_HOST->session_user_id(hs);
  s->flags = MTS_HOST->session_user_flags(hs);
  s->restrictions = HOST_HAS(session_restriction_flags)
                        ? MTS_HOST->session_restriction_flags(hs)
                        : 0;
  if (s->restrictions & AC_RCHAT) {
    out(s, "Chat access is restricted for this account.\r\n");
    return BBS_EPERM;
  }
  s->node =
      HOST_HAS(session_node_number) ? MTS_HOST->session_node_number(hs) : 0;
  s->ansi = HOST_HAS(session_has_ansi) ? MTS_HOST->session_has_ansi(hs) : 1;
  s->term_cols = 80;
  s->term_rows = 24;
  if (HOST_HAS(session_terminal_size))
    MTS_HOST->session_terminal_size(hs, &s->term_cols, &s->term_rows);
  if (s->term_cols < 20)
    s->term_cols = 20;
  mts_sanitize(s->handle, sizeof s->handle, MTS_HOST->session_username(hs));
  pthread_mutex_init(&s->inbox_mu, NULL);
  s->inbox_initialized = 1;
  mts_store_load_preferences(&MTS, s);
  if (!mts_is_sysop(s) &&
      mts_store_is_sanctioned(&MTS, "mts_global_bans", 0, s->user_id)) {
    out(s, "You are banned from MTS.\r\n");
    return BBS_EPERM;
  }
  pthread_mutex_lock(&MTS.mu);
  if (MTS.chat_locked && !mts_is_sysop(s)) {
    pthread_mutex_unlock(&MTS.mu);
    out(s, "MTS is currently locked by the sysop.\r\n");
    return BBS_EPERM;
  }
  if (MTS.session_count >= (size_t)MTS.cfg.max_users) {
    pthread_mutex_unlock(&MTS.mu);
    return BBS_EPERM;
  }
  MTS.sessions[MTS.session_count++] = s;
  s->active = 1;
  s->presence_registered = 1;
  s->lifecycle = MTS_LIFE_ENTERED;
  pthread_mutex_unlock(&MTS.mu);
  s->visit_id = mts_store_visit_start(&MTS, s->user_id, s->handle);
  s->visit_open = s->visit_id != 0;
  if (!mts_join(s, s->prefs.default_room ? s->prefs.default_room : 1))
    mts_join(s, 1);
  MTS_HOST->io->cls(hs);
  out(s, "\033[1;35m=== Mutineer Teleconference System v2.0 ===\033[0m\r\nType "
         "/help for commands. You are in Lobby.\r\n");
  return BBS_OK;
}
static bbs_rc_t run(void *v, bbs_session_t *hs) {
  mts_session_t *s = v;
  (void)hs;
  if (!s->active)
    return BBS_EPERM;
  char line[MTS_TEXT];
  int running = 1;
  int prompt_visible = 0;
  while (running) {
    flush(s);
    if (!prompt_visible) {
      out(s, "MTS> ");
      prompt_visible = 1;
    }
    bbs_rc_t rc =
        HOST_HAS(readline_timed)
            ? MTS_HOST->readline_timed(s->host_session, line, sizeof line,
                                       s->prefs.echo, 1000)
            : MTS_HOST->io->readline(s->host_session, line, sizeof line, 1);
    if (rc == BBS_ETIMEOUT)
      continue;
    if (rc != BBS_OK) {
      cleanup_instance(s, 1);
      return rc;
    }
    prompt_visible = 0;
    mts_sanitize(line, sizeof line, line);
    if (!line[0])
      continue;
    if (line[0] == '/' && line[1] && line[1] != '/' && strchr(line, ' ')) {
      char *sp = strchr(line, ' ');
      size_t userlen = (size_t)(sp - (line + 1));
      char cmd[MTS_TEXT];
      char name[MTS_NAME] = {0};
      if (userlen > 0 && userlen < sizeof name)
        memcpy(name, line + 1, userlen);
      if (userlen > 0 && userlen < MTS_NAME && !mts_command_find(name)) {
        snprintf(cmd, sizeof cmd, "/msg %.*s %s", (int)userlen, line + 1,
                 sp + 1);
        snprintf(line, sizeof line, "%s", cmd);
      }
    }
    if (line[0] == '>' && line[1]) {
      char *sp = strchr(line, ' ');
      if (!sp) {
        out(s, "Usage: >user message\r\n");
        continue;
      }
      *sp++ = 0;
      mts_session_t *u = NULL;
      if (!target(s, line + 1, &u))
        continue;
      clear_afk(s);
      mts_publish(s, MTS_DIRECTED, s->room_id, u->user_id, u->handle, sp);
      continue;
    }
    if (line[0] == '/')
      command(s, line, &running);
    else {
      int64_t now = mts_now();
      if (now - s->rate_started >= MTS.cfg.rate_window_seconds) {
        s->rate_started = now;
        s->rate_count = 0;
      }
      if (++s->rate_count > (unsigned)MTS.cfg.messages_per_window) {
        out(s, "Rate limit exceeded; please slow down.\r\n");
        continue;
      }
      if (MTS.chat_locked && !mts_is_sysop(s)) {
        out(s, "MTS is locked.\r\n");
        continue;
      }
      if (s->muted) {
        out(s, "You are muted in this room.\r\n");
        continue;
      }
      if (s->focused_user) {
        mts_session_t *u = NULL;
        pthread_mutex_lock(&MTS.mu);
        for (size_t i = 0; i < MTS.session_count; i++)
          if (MTS.sessions[i]->user_id == s->focused_user)
            u = MTS.sessions[i];
        pthread_mutex_unlock(&MTS.mu);
        if (u)
          clear_afk(s),
              mts_publish(s, MTS_PRIVATE, 0, u->user_id, u->handle, line);
        else {
          s->focused_user = 0;
          out(s, "Focused user left; returning to room chat.\r\n");
        }
      } else
        clear_afk(s), mts_publish(s, MTS_PUBLIC, s->room_id, 0, NULL, line);
    }
  }
  return BBS_OK;
}
static int cleanup_instance(mts_session_t *s, int announce) {
  pthread_mutex_lock(&MTS.mu);
  if (s->cleanup_complete || s->lifecycle >= MTS_LIFE_EXITING) {
    pthread_mutex_unlock(&MTS.mu);
    return 0;
  }
  s->lifecycle = MTS_LIFE_EXITING;
  uint32_t peer = s->focused_user;
  s->focused_user = 0;
  s->focus_requested_by = 0;
  s->watch_room = 0;
  s->watch_all = 0;
  for (size_t i = 0; i < MTS.session_count; i++)
    if (MTS.sessions[i]->user_id == peer)
      MTS.sessions[i]->focused_user = 0;
  pthread_mutex_unlock(&MTS.mu);
  int do_announce = announce && s->presence_registered &&
                    !s->departure_announced && !MTS.shutting_down;
  s->departure_announced = do_announce;
  if (s->presence_registered)
    mts_leave(s, do_announce);
  s->presence_registered = 0;
  s->visit_open = 0;
  s->lifecycle = MTS_LIFE_EXITED;
  s->cleanup_complete = 1;
  return 1;
}
static void instance_event(void *v, const bbs_event_t *ev) {
  mts_session_t *s = v;
  if (!ev)
    return;
  if ((ev->type == BBS_EVT_FORCED_DISCONNECT &&
       (!ev->session || ev->session == s->host_session)) ||
      ev->type == BBS_EVT_SHUTDOWN)
    cleanup_instance(s, 0);
}
static void leave(void *v, bbs_session_t *s) {
  mts_session_t *m = v;
  if (cleanup_instance(m, 1))
    out(m, "\r\nLeaving MTS.\r\n");
  (void)s;
}
static void destroy(void *v) {
  mts_session_t *s = v;
  cleanup_instance(s, 0);
  s->lifecycle = MTS_LIFE_DESTROYED;
  if (s->inbox_initialized) {
    pthread_mutex_destroy(&s->inbox_mu);
    s->inbox_initialized = 0;
  }
  free(s);
}
static const bbs_plugin_instance_vtbl_t VT = {enter,          run,     leave,
                                              instance_event, destroy, {NULL}};
static bbs_rc_t init(const bbs_host_api_t *h) {
  MTS_HOST = h;
  bind_command_handlers();
  if (!h || h->magic != BBS_PLUGIN_MAGIC || !h->io || !h->session_username ||
      !h->session_user_id || !h->session_user_flags || !h->plugin_data_dir)
    return BBS_EUNSUPPORTED;
  char dir[512], conf[768], err[256] = {0};
  if (h->plugin_data_dir(MTS_PLUGIN_ID, dir, sizeof dir) != BBS_OK)
    return BBS_EIO;
  snprintf(conf, sizeof conf, "%s/mts.conf", dir);
  mts_config_t cfg;
  if (!mts_config_load(&cfg, conf, err, sizeof err) || !cfg.enabled) {
    h->log(BBS_LOG_ERROR, "mts", err[0] ? err : "MTS disabled");
    return BBS_EINVAL;
  }
  if (!mts_state_init(dir, &cfg, err, sizeof err)) {
    h->log(BBS_LOG_ERROR, "mts", err);
    return BBS_EIO;
  }
  h->log(BBS_LOG_INFO, "mts", "Mutineer Teleconference System 2.0 initialized");
  return BBS_OK;
}
static void global_event(const bbs_event_t *ev) {
  if (!ev)
    return;
  if (ev->type == BBS_EVT_TICK_1S && ev->now_ms % 60000u < 1000u)
    mts_store_cleanup(&MTS);
  else if (ev->type == BBS_EVT_FORCED_DISCONNECT && ev->session) {
    mts_session_t *target_session = NULL;
    pthread_mutex_lock(&MTS.mu);
    for (size_t i = 0; i < MTS.session_count; i++)
      if (MTS.sessions[i]->host_session == ev->session)
        target_session = MTS.sessions[i];
    pthread_mutex_unlock(&MTS.mu);
    if (target_session)
      cleanup_instance(target_session, 0);
  } else if (ev->type == BBS_EVT_SHUTDOWN) {
    mts_session_t *sessions[MTS_HARD_USERS];
    size_t count = 0;
    pthread_mutex_lock(&MTS.mu);
    MTS.shutting_down = 1;
    for (size_t i = 0; i < MTS.session_count && count < MTS_HARD_USERS; i++)
      sessions[count++] = MTS.sessions[i];
    pthread_mutex_unlock(&MTS.mu);
    for (size_t i = 0; i < count; i++)
      cleanup_instance(sessions[i], 0);
  }
}
static void shutdown(void) { mts_state_shutdown(); }
static bbs_rc_t create(bbs_session_t *s, void **o,
                       const bbs_plugin_instance_vtbl_t **v) {
  (void)s;
  mts_session_t *x = calloc(1, sizeof *x);
  if (!x)
    return BBS_EINTERNAL;
  x->lifecycle = MTS_LIFE_CREATED;
  *o = x;
  *v = &VT;
  return BBS_OK;
}
bbs_rc_t bbs_plugin_query(uint32_t ver, const bbs_host_api_t *h,
                          bbs_plugin_desc_t *o) {
  if (!h || !o)
    return BBS_EINVAL;
  if ((ver >> 16) != (BBS_PLUGIN_ABI_VERSION >> 16))
    return BBS_EUNSUPPORTED;
  memset(o, 0, sizeof *o);
  o->abi_version = BBS_PLUGIN_ABI_VERSION;
  o->size = sizeof *o;
  o->magic = BBS_PLUGIN_MAGIC;
  o->id = MTS_PLUGIN_ID;
  o->name = "Mutineer Teleconference System";
  o->version = "2.0.0";
  o->author = "Mutineer BBS";
  o->description = "Persistent multi-room teleconference, private messaging, "
                   "safety, and moderation.";
  o->caps = BBS_CAP_INTERACTIVE | BBS_CAP_BACKGROUND | BBS_CAP_COMMANDS;
  o->init = init;
  o->shutdown = shutdown;
  o->create_instance = create;
  o->on_event = global_event;
  return BBS_OK;
}
