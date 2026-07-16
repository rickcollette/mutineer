#include "mts.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int boolean(const char *v, int *out) {
  if (!strcasecmp(v, "true") || !strcmp(v, "1") || !strcasecmp(v, "yes")) {
    *out = 1;
    return 1;
  }
  if (!strcasecmp(v, "false") || !strcmp(v, "0") || !strcasecmp(v, "no")) {
    *out = 0;
    return 1;
  }
  return 0;
}
int mts_config_load(mts_config_t *c, const char *path, char *err, size_t n) {
  *c = (mts_config_t){.enabled = 1,
                      .max_users = 128,
                      .max_rooms = 64,
                      .max_message_bytes = 512,
                      .history_per_room = 200,
                      .history_retention_days = 30,
                      .messages_per_window = 8,
                      .rate_window_seconds = 10,
                      .allow_user_rooms = 1,
                      .allow_private_rooms = 1,
                      .max_rooms_per_user = 1,
                      .moderation_audit_retention_days = 365,
                      .persist_history = 0,
                      .default_room = "Lobby"};
  FILE *f = fopen(path, "r");
  if (!f)
    return 1;
  char line[512];
  int ln = 0;
  while (fgets(line, sizeof line, f)) {
    ln++;
    char *p = line;
    while (isspace((unsigned char)*p))
      p++;
    if (!*p || *p == '#' || *p == ';')
      continue;
    char *eq = strchr(p, '=');
    if (!eq)
      continue;
    *eq++ = 0;
    char *e = p + strlen(p);
    while (e > p && isspace((unsigned char)e[-1]))
      *--e = 0;
    while (isspace((unsigned char)*eq))
      eq++;
    e = eq + strlen(eq);
    while (e > eq && isspace((unsigned char)e[-1]))
      *--e = 0;
#define INTKEY(k, m)                                                           \
  if (!strcmp(p, k)) {                                                         \
    char *x;                                                                   \
    long v = strtol(eq, &x, 10);                                               \
    if (*x) {                                                                  \
      snprintf(err, n, "invalid %s at line %d", k, ln);                        \
      goto bad;                                                                \
    }                                                                          \
    c->m = (int)v;                                                             \
    continue;                                                                  \
  }
    INTKEY("max_users", max_users)
    INTKEY("max_rooms", max_rooms)
    INTKEY("max_message_bytes", max_message_bytes)
    INTKEY("history_per_room", history_per_room)
    INTKEY("history_retention_days", history_retention_days)
    INTKEY("messages_per_window", messages_per_window)
    INTKEY("rate_window_seconds", rate_window_seconds)
    INTKEY("max_rooms_per_user", max_rooms_per_user)
    INTKEY("moderation_audit_retention_days", moderation_audit_retention_days)
#undef INTKEY
#define BOOLKEY(k, m)                                                          \
  if (!strcmp(p, k)) {                                                         \
    if (!boolean(eq, &c->m)) {                                                 \
      snprintf(err, n, "invalid %s at line %d", k, ln);                        \
      goto bad;                                                                \
    }                                                                          \
    continue;                                                                  \
  }
    BOOLKEY("enabled", enabled)
    BOOLKEY("allow_user_rooms", allow_user_rooms)
    BOOLKEY("allow_private_rooms", allow_private_rooms)
    BOOLKEY("persist_history", persist_history)
#undef BOOLKEY
    if (!strcmp(p, "default_room")) {
      mts_sanitize(c->default_room, sizeof c->default_room, eq);
      continue;
    }
    if (!strcmp(p, "room")) {
      if (c->room_count >= 32) {
        snprintf(err, n, "too many permanent rooms at line %d", ln);
        goto bad;
      }
      char value[512];
      snprintf(value, sizeof value, "%s", eq);
      char *vis = strchr(value, '|');
      if (!vis) {
        snprintf(err, n, "invalid room at line %d", ln);
        goto bad;
      }
      *vis++ = 0;
      char *topic = strchr(vis, '|');
      if (!topic) {
        snprintf(err, n, "invalid room at line %d", ln);
        goto bad;
      }
      *topic++ = 0;
      mts_config_room_t *r = &c->rooms[c->room_count++];
      mts_sanitize(r->name, sizeof r->name, value);
      mts_sanitize(r->topic, sizeof r->topic, topic);
      if (!r->name[0] || (!strcasecmp(vis, "public")    ? 0
                          : !strcasecmp(vis, "private") ? (r->is_private = 1, 0)
                                                        : 1)) {
        snprintf(err, n, "invalid room at line %d", ln);
        goto bad;
      }
      continue;
    }
    if (MTS_HOST && MTS_HOST->log) {
      char w[256];
      snprintf(w, sizeof w, "unknown mts.conf key at line %d: %.180s", ln, p);
      MTS_HOST->log(BBS_LOG_WARN, "mts", w);
    }
  }
  fclose(f);
  if (c->max_users < 1 || c->max_users > MTS_HARD_USERS || c->max_rooms < 1 ||
      c->max_rooms > MTS_HARD_ROOMS || c->max_message_bytes < 32 ||
      c->max_message_bytes >= MTS_TEXT || c->history_per_room < 0 ||
      c->history_per_room > 2000 || c->messages_per_window < 1 ||
      c->messages_per_window > 100 || c->rate_window_seconds < 1 ||
      c->rate_window_seconds > 3600) {
    snprintf(err, n, "MTS configuration limit outside safe bounds");
    return 0;
  }
  return 1;
bad:
  fclose(f);
  return 0;
}
