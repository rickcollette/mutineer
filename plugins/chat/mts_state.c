#include "mts.h"
#include "mts_render.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

mts_state_t MTS = {.mu = PTHREAD_MUTEX_INITIALIZER,
                   .publish_mu = PTHREAD_MUTEX_INITIALIZER};
const bbs_host_api_t *MTS_HOST = NULL;
int64_t mts_now(void) { return (int64_t)time(NULL); }
int64_t mts_parse_duration(const char *text, int *ok) {
  *ok = 0;
  if (!text || !*text)
    return 0;
  char *end = NULL;
  long long v = strtoll(text, &end, 10);
  if (v < 1)
    return 0;
  long long mult = 1;
  if (*end) {
    if (end[1])
      return 0;
    switch (tolower((unsigned char)*end)) {
    case 'm':
      mult = 60;
      break;
    case 'h':
      mult = 3600;
      break;
    case 'd':
      mult = 86400;
      break;
    case 'w':
      mult = 604800;
      break;
    default:
      return 0;
    }
  }
  if (v > 31536000LL / mult)
    return 0;
  *ok = 1;
  return v * mult;
}
void mts_sanitize(char *d, size_t cap, const char *s) {
  mts_utf8_sanitize(d, cap, s);
}
static void normalize(char *d, size_t n, const char *s) {
  size_t o = 0;
  for (; *s && o + 1 < n; s++)
    if (isalnum((unsigned char)*s))
      d[o++] = (char)tolower((unsigned char)*s);
  d[o] = 0;
}
int mts_state_init(const char *dir, const mts_config_t *c, char *err,
                   size_t n) {
  pthread_mutex_lock(&MTS.mu);
  MTS.cfg = *c;
  memset(MTS.rooms, 0, sizeof MTS.rooms);
  memset(MTS.sessions, 0, sizeof MTS.sessions);
  MTS.session_count = 0;
  MTS.shutting_down = 0;
  MTS.chat_locked = 0;
  MTS.next_room_id = 2;
  MTS.next_sequence = 1;
  snprintf(MTS.data_dir, sizeof MTS.data_dir, "%s", dir);
  pthread_mutex_unlock(&MTS.mu);
  if (!mts_store_open(&MTS, dir, err, n))
    return 0;
  mts_store_load_rooms(&MTS);
  if (!MTS.rooms[0].active) {
    MTS.rooms[0] = (mts_room_t){.id = 1, .permanent = 1, .active = 1};
    snprintf(MTS.rooms[0].name, sizeof MTS.rooms[0].name, "Lobby");
    snprintf(MTS.rooms[0].normalized, sizeof MTS.rooms[0].normalized, "lobby");
    snprintf(MTS.rooms[0].topic, sizeof MTS.rooms[0].topic,
             "Welcome to the Mutineer Teleconference System");
    mts_store_save_room(&MTS, &MTS.rooms[0]);
  }
  for (int i = 0; i < MTS.cfg.max_rooms; i++)
    if (MTS.rooms[i].active && MTS.rooms[i].id != 1 && MTS.rooms[i].permanent) {
      int configured = 0;
      for (size_t j = 0; j < c->room_count; j++) {
        char norm[MTS_NAME];
        normalize(norm, sizeof norm, c->rooms[j].name);
        if (!strcmp(norm, MTS.rooms[i].normalized))
          configured = 1;
      }
      if (!configured) {
        uint64_t id = MTS.rooms[i].id;
        MTS.rooms[i].active = 0;
        mts_store_delete_room(&MTS, id);
      }
    }
  for (size_t j = 0; j < c->room_count; j++) {
    char norm[MTS_NAME];
    normalize(norm, sizeof norm, c->rooms[j].name);
    if (!strcmp(norm, "lobby"))
      continue;
    mts_room_t *r = mts_find_room(c->rooms[j].name);
    if (!r) {
      for (int i = 0; i < MTS.cfg.max_rooms; i++)
        if (!MTS.rooms[i].active) {
          r = &MTS.rooms[i];
          memset(r, 0, sizeof *r);
          r->id = MTS.next_room_id++;
          r->active = 1;
          break;
        }
    }
    if (!r) {
      snprintf(err, n, "configured rooms exceed max_rooms");
      mts_store_close(&MTS);
      return 0;
    }
    snprintf(r->name, sizeof r->name, "%s", c->rooms[j].name);
    snprintf(r->normalized, sizeof r->normalized, "%s", norm);
    snprintf(r->topic, sizeof r->topic, "%s", c->rooms[j].topic);
    r->is_private = c->rooms[j].is_private;
    r->permanent = 1;
    r->owner = 0;
    mts_store_save_room(&MTS, r);
  }
  for (int i = 0; i < MTS.cfg.max_rooms; i++)
    if (MTS.rooms[i].active && c->history_per_room > 0) {
      MTS.rooms[i].history_capacity = (size_t)c->history_per_room;
      MTS.rooms[i].history =
          calloc(MTS.rooms[i].history_capacity, sizeof(mts_event_t));
      if (!MTS.rooms[i].history) {
        snprintf(err, n, "cannot allocate room history");
        mts_store_close(&MTS);
        return 0;
      }
    }
  MTS.initialized = 1;
  return 1;
}
void mts_state_shutdown(void) {
  pthread_mutex_lock(&MTS.mu);
  MTS.shutting_down = 1;
  pthread_mutex_unlock(&MTS.mu);
  mts_store_close(&MTS);
  for (int i = 0; i < MTS_HARD_ROOMS; i++) {
    free(MTS.rooms[i].history);
    MTS.rooms[i].history = NULL;
    MTS.rooms[i].history_capacity = 0;
  }
  MTS.initialized = 0;
}
static int enqueue(mts_session_t *s, const mts_event_t *e, int private) {
  pthread_mutex_lock(&s->inbox_mu);
  if (s->count == MTS_INBOX) {
    if (private) {
      pthread_mutex_unlock(&s->inbox_mu);
      return 0;
    }
    s->head = (s->head + 1) % MTS_INBOX;
    s->count--;
    s->skipped++;
  }
  size_t p = (s->head + s->count) % MTS_INBOX;
  s->inbox[p] = *e;
  s->count++;
  pthread_mutex_unlock(&s->inbox_mu);
  return 1;
}
void mts_publish(mts_session_t *f, mts_event_type_t type, uint64_t room,
                 uint32_t target, const char *tn, const char *text) {
  pthread_mutex_lock(&MTS.publish_mu);
  mts_event_t e = {0};
  e.type = type;
  e.room_id = room;
  e.sender_id = f ? f->user_id : 0;
  e.target_id = target;
  e.timestamp = mts_now();
  if (f)
    snprintf(e.sender, sizeof e.sender, "%s", f->handle);
  if (tn)
    snprintf(e.target, sizeof e.target, "%s", tn);
  mts_sanitize(e.text, sizeof e.text, text);
  mts_session_t *rec[MTS_HARD_USERS];
  mts_session_t *auto_reply[MTS_HARD_USERS];
  size_t auto_count = 0;
  size_t count = 0;
  pthread_mutex_lock(&MTS.mu);
  e.sequence = MTS.next_sequence++;
  int room_private = 0;
  for (int j = 0; j < MTS.cfg.max_rooms; j++)
    if (MTS.rooms[j].active && MTS.rooms[j].id == room)
      room_private = MTS.rooms[j].is_private;
  for (size_t i = 0; i < MTS.session_count; i++) {
    mts_session_t *s = MTS.sessions[i];
    if (!s || !s->active)
      continue;
    int send = type == MTS_ANNOUNCEMENT ||
               ((type == MTS_PRIVATE || type == MTS_AFK_RESPONSE)
                    ? s->user_id == target
                    : (s->room_id == room || s->watch_room == room ||
                       (s->watch_all && mts_is_sysop(s) && !room_private)));
    if (send)
      rec[count++] = s;
  }
  pthread_mutex_unlock(&MTS.mu);
  int failed = 0;
  for (size_t i = 0; i < count; i++) {
    if (type == MTS_PRESENCE && !rec[i]->prefs.joins)
      continue;
    if (type == MTS_ACTION && !rec[i]->prefs.actions)
      continue;
    if (f && rec[i] != f &&
        mts_store_has_relation(&MTS, "mts_blocks", rec[i]->user_id, f->user_id))
      continue;
    if (type != MTS_PRIVATE && type != MTS_AFK_RESPONSE && f && rec[i] != f &&
        mts_store_has_relation(&MTS, "mts_ignores", rec[i]->user_id,
                               f->user_id))
      continue;
    if (!enqueue(rec[i], &e, type == MTS_PRIVATE || type == MTS_AFK_RESPONSE))
      failed++;
    if (type == MTS_PRIVATE && f && rec[i]->afk) {
      int seen = 0;
      for (size_t j = 0; j < rec[i]->afk_replied_count; j++)
        if (rec[i]->afk_replied[j] == f->user_id)
          seen = 1;
      if (!seen && rec[i]->afk_replied_count < 32) {
        rec[i]->afk_replied[rec[i]->afk_replied_count++] = f->user_id;
        auto_reply[auto_count++] = rec[i];
      }
    }
  }
  if (failed && f) {
    mts_event_t n = e;
    n.type = MTS_SYSTEM;
    n.sender_id = 0;
    snprintf(n.sender, sizeof n.sender, "MTS");
    snprintf(n.text, sizeof n.text,
             "Private message not delivered: recipient inbox is full.");
    enqueue(f, &n, 0);
  }
  mts_store_history(&MTS, &e);
  if (type != MTS_PRIVATE && type != MTS_AFK_RESPONSE) {
    pthread_mutex_lock(&MTS.mu);
    for (int i = 0; i < MTS.cfg.max_rooms; i++)
      if (MTS.rooms[i].active && MTS.rooms[i].id == room &&
          MTS.rooms[i].history_capacity) {
        mts_room_t *r = &MTS.rooms[i];
        size_t pos = (r->history_head + r->history_count) % r->history_capacity;
        if (r->history_count == r->history_capacity) {
          r->history_head = (r->history_head + 1) % r->history_capacity;
          pos = (r->history_head + r->history_count - 1) % r->history_capacity;
        } else
          r->history_count++;
        r->history[pos] = e;
      }
    pthread_mutex_unlock(&MTS.mu);
  }
  pthread_mutex_unlock(&MTS.publish_mu);
  for (size_t i = 0; i < auto_count; i++) {
    char msg[256];
    snprintf(msg, sizeof msg, "AFK: %s",
             auto_reply[i]->afk_message[0] ? auto_reply[i]->afk_message
                                           : "away from the keyboard");
    mts_publish(auto_reply[i], MTS_AFK_RESPONSE, 0, f->user_id, f->handle, msg);
  }
}
size_t mts_drain(mts_session_t *s, mts_event_t *out, size_t cap,
                 uint64_t *skip) {
  pthread_mutex_lock(&s->inbox_mu);
  size_t n = s->count < cap ? s->count : cap;
  for (size_t i = 0; i < n; i++) {
    out[i] = s->inbox[s->head];
    s->head = (s->head + 1) % MTS_INBOX;
    s->count--;
  }
  *skip = s->skipped;
  s->skipped = 0;
  pthread_mutex_unlock(&s->inbox_mu);
  return n;
}
int mts_join(mts_session_t *s, uint64_t room) {
  mts_room_t snapshot = {0};
  pthread_mutex_lock(&MTS.mu);
  for (int i = 0; i < MTS.cfg.max_rooms; i++)
    if (MTS.rooms[i].active && MTS.rooms[i].id == room)
      snapshot = MTS.rooms[i];
  pthread_mutex_unlock(&MTS.mu);
  if (!snapshot.active)
    return 0;
  if (!mts_is_sysop(s) &&
      (mts_store_is_sanctioned(&MTS, "mts_global_bans", 0, s->user_id) ||
       mts_store_is_sanctioned(&MTS, "mts_room_bans", room, s->user_id)))
    return 0;
  if (snapshot.is_private && !mts_is_sysop(s) && snapshot.owner != s->user_id &&
      !mts_store_is_invited(&MTS, room, s->user_id))
    return 0;
  uint64_t old = s->room_id;
  pthread_mutex_lock(&MTS.mu);
  s->room_id = room;
  pthread_mutex_unlock(&MTS.mu);
  mts_publish(s, MTS_PRESENCE, room, 0, NULL, "joined the room");
  if (old && old != room) {
    int occupied = 0, slot = -1;
    pthread_mutex_lock(&MTS.mu);
    for (size_t j = 0; j < MTS.session_count; j++)
      if (MTS.sessions[j]->room_id == old)
        occupied = 1;
    for (int i = 0; i < MTS.cfg.max_rooms; i++)
      if (MTS.rooms[i].active && MTS.rooms[i].id == old &&
          !MTS.rooms[i].permanent)
        slot = i;
    if (!occupied && slot >= 0)
      MTS.rooms[slot].active = 0;
    pthread_mutex_unlock(&MTS.mu);
    if (!occupied && slot >= 0)
      mts_store_delete_room(&MTS, old);
  }
#ifdef MTS_TESTING
  if (!MTS.skip_join_sanction_reads)
#endif
    s->muted =
        mts_store_is_sanctioned(&MTS, "mts_global_mutes", 0, s->user_id) ||
        mts_store_is_sanctioned(&MTS, "mts_room_mutes", room, s->user_id);
  return 1;
}
void mts_leave(mts_session_t *s, int announce) {
  if (!s->active)
    return;
  uint64_t room = s->room_id;
  if (announce)
    mts_publish(s, MTS_PRESENCE, room, 0, NULL, "left the room");
  pthread_mutex_lock(&MTS.mu);
  for (size_t i = 0; i < MTS.session_count; i++)
    if (MTS.sessions[i] == s) {
      MTS.sessions[i] = MTS.sessions[--MTS.session_count];
      break;
    }
  s->active = 0;
  pthread_mutex_unlock(&MTS.mu);
  if (s->visit_id)
    mts_store_visit_end(&MTS, s->visit_id);
  int occupied = 0, slot = -1;
  pthread_mutex_lock(&MTS.mu);
  for (size_t j = 0; j < MTS.session_count; j++)
    if (MTS.sessions[j]->room_id == room)
      occupied = 1;
  for (int i = 0; i < MTS.cfg.max_rooms; i++)
    if (MTS.rooms[i].active && MTS.rooms[i].id == room &&
        !MTS.rooms[i].permanent)
      slot = i;
  if (!occupied && slot >= 0)
    MTS.rooms[slot].active = 0;
  pthread_mutex_unlock(&MTS.mu);
  if (!occupied && slot >= 0)
    mts_store_delete_room(&MTS, room);
}
mts_room_t *mts_find_room(const char *q) {
  char x[MTS_NAME];
  normalize(x, sizeof x, q);
  for (int i = 0; i < MTS.cfg.max_rooms; i++)
    if (MTS.rooms[i].active && !strcmp(MTS.rooms[i].normalized, x))
      return &MTS.rooms[i];
  return NULL;
}
mts_session_t *mts_find_user(const char *q, int *amb) {
  *amb = 0;
  if (!q || !*q)
    return NULL;
  char *end;
  long node = strtol(q, &end, 10);
  mts_session_t *found = NULL;
  pthread_mutex_lock(&MTS.mu);
  for (size_t i = 0; i < MTS.session_count; i++) {
    mts_session_t *s = MTS.sessions[i];
    int match =
        (*end == 0) ? s->node == node : !strncasecmp(s->handle, q, strlen(q));
    if (match) {
      if (found && found->user_id != s->user_id) {
        *amb = 1;
        found = NULL;
        break;
      }
      found = s;
    }
  }
  pthread_mutex_unlock(&MTS.mu);
  return found;
}
int mts_is_sysop(const mts_session_t *s) { return s && (s->flags & (1u << 0)); }
