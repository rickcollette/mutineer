#pragma once
#include "bbs_plugin_api.h"
#include <pthread.h>
#include <sqlite3.h>
#include <stddef.h>
#include <stdint.h>

#define MTS_PLUGIN_ID "com.mutineer.chat"
#define MTS_HARD_USERS 256
#define MTS_HARD_ROOMS 128
#define MTS_INBOX 128
#define MTS_TEXT 513
#define MTS_NAME 64
#define MTS_TOPIC 256
enum {
  MTS_LIFE_CREATED,
  MTS_LIFE_ENTERED,
  MTS_LIFE_EXITING,
  MTS_LIFE_EXITED,
  MTS_LIFE_DESTROYED
};

typedef enum {
  MTS_PUBLIC,
  MTS_DIRECTED,
  MTS_PRIVATE,
  MTS_ACTION,
  MTS_SYSTEM,
  MTS_PRESENCE,
  MTS_MODERATION,
  MTS_ANNOUNCEMENT,
  MTS_AFK_RESPONSE
} mts_event_type_t;

typedef struct {
  uint64_t sequence, room_id;
  int64_t timestamp;
  mts_event_type_t type;
  uint32_t sender_id, target_id;
  char sender[MTS_NAME], target[MTS_NAME], text[MTS_TEXT];
} mts_event_t;

typedef struct {
  uint64_t id;
  char name[MTS_NAME], normalized[MTS_NAME], topic[MTS_TOPIC];
  uint32_t owner;
  int is_private, permanent, active;
  mts_event_t *history;
  size_t history_head, history_count, history_capacity;
} mts_room_t;

typedef struct {
  int timestamps, joins, actions, bell, echo, color, allow_chat_requests;
  int directory_visible, last_seen_visible, profile_visible;
  uint64_t default_room;
  char theme[32], greeting[128], farewell[128];
} mts_preferences_t;

typedef struct mts_session {
  bbs_session_t *host_session;
  uint32_t user_id, flags, restrictions;
  int node, ansi, active, afk, muted;
  uint64_t room_id, last_sequence, watch_room, visit_id;
  uint32_t last_private, focused_user, focus_requested_by;
  int watch_all;
  uint16_t term_cols, term_rows;
  unsigned lifecycle;
  unsigned inbox_initialized : 1;
  unsigned presence_registered : 1;
  unsigned visit_open : 1;
  unsigned departure_announced : 1;
  unsigned cleanup_complete : 1;
  uint64_t afk_epoch;
  uint32_t afk_replied[32];
  size_t afk_replied_count;
  char handle[MTS_NAME], afk_message[128];
  mts_preferences_t prefs;
  mts_event_t inbox[MTS_INBOX];
  size_t head, count;
  uint64_t skipped;
  pthread_mutex_t inbox_mu;
  int64_t rate_started;
  unsigned rate_count;
} mts_session_t;

typedef struct {
  char name[MTS_NAME], topic[MTS_TOPIC];
  int is_private;
} mts_config_room_t;

typedef struct {
  int enabled, max_users, max_rooms, max_message_bytes, history_per_room;
  int history_retention_days, messages_per_window, rate_window_seconds;
  int allow_user_rooms, allow_private_rooms, max_rooms_per_user;
  int moderation_audit_retention_days, persist_history;
  char default_room[MTS_NAME];
  mts_config_room_t rooms[32];
  size_t room_count;
} mts_config_t;

typedef struct {
  uint32_t user_id, actor_user_id;
  uint64_t room_id;
  int64_t created_at, expires_at;
  char reason[256];
} mts_sanction_record_t;

typedef struct {
  uint32_t actor_user_id, target_user_id;
  uint64_t room_id;
  int64_t created_at, expires_at;
  char action[48], reason[256];
} mts_audit_record_t;

typedef struct {
  pthread_mutex_t mu;
  pthread_mutex_t publish_mu;
  mts_config_t cfg;
  sqlite3 *db;
  char data_dir[512];
  mts_room_t rooms[MTS_HARD_ROOMS];
  mts_session_t *sessions[MTS_HARD_USERS];
  size_t session_count;
  uint64_t next_room_id, next_sequence;
  int initialized, shutting_down, chat_locked;
#ifdef MTS_TESTING
  int skip_join_sanction_reads;
#endif
  char chat_lock_reason[128];
} mts_state_t;

extern const bbs_host_api_t *MTS_HOST;
extern mts_state_t MTS;

int mts_config_load(mts_config_t *cfg, const char *path, char *error,
                    size_t error_sz);
int mts_store_open(mts_state_t *state, const char *dir, char *error,
                   size_t error_sz);
void mts_store_close(mts_state_t *state);
int mts_store_load_rooms(mts_state_t *state);
int mts_store_save_room(mts_state_t *state, const mts_room_t *room);
void mts_store_load_preferences(mts_state_t *state, mts_session_t *s);
void mts_store_save_preferences(mts_state_t *state, const mts_session_t *s);
int mts_store_relation(mts_state_t *state, const char *table, uint32_t owner,
                       uint32_t target, int add);
int mts_store_has_relation(mts_state_t *state, const char *table,
                           uint32_t owner, uint32_t target);
void mts_store_history(mts_state_t *state, const mts_event_t *ev);
void mts_store_audit(mts_state_t *state, uint32_t actor, uint32_t target,
                     uint64_t room, const char *action, const char *reason);
int mts_store_invitation(mts_state_t *state, uint64_t room, uint32_t user,
                         uint32_t actor, int add);
int mts_store_is_invited(mts_state_t *state, uint64_t room, uint32_t user);
int mts_store_sanction(mts_state_t *state, const char *table, uint64_t room,
                       uint32_t user, uint32_t actor, const char *reason,
                       int64_t expires_at, int add);
int mts_store_is_sanctioned(mts_state_t *state, const char *table,
                            uint64_t room, uint32_t user);
int mts_store_cleanup(mts_state_t *state);
size_t mts_store_list_sanctions(mts_state_t *state, const char *table,
                                uint64_t room, mts_sanction_record_t *records,
                                size_t capacity);
size_t mts_store_list_audit(mts_state_t *state, mts_audit_record_t *records,
                            size_t capacity);
uint64_t mts_store_visit_start(mts_state_t *state, uint32_t user,
                               const char *handle);
void mts_store_visit_end(mts_state_t *state, uint64_t visit_id);
int mts_store_delete_room(mts_state_t *state, uint64_t room);
int64_t mts_parse_duration(const char *text, int *ok);

void mts_sanitize(char *dst, size_t cap, const char *src);
int mts_state_init(const char *data_dir, const mts_config_t *cfg, char *error,
                   size_t error_sz);
void mts_state_shutdown(void);
int mts_join(mts_session_t *s, uint64_t room_id);
void mts_leave(mts_session_t *s, int announce);
void mts_publish(mts_session_t *from, mts_event_type_t type, uint64_t room,
                 uint32_t target, const char *target_name, const char *text);
size_t mts_drain(mts_session_t *s, mts_event_t *out, size_t cap,
                 uint64_t *skipped);
mts_session_t *mts_find_user(const char *query, int *ambiguous);
mts_room_t *mts_find_room(const char *query);
int mts_is_sysop(const mts_session_t *s);
int64_t mts_now(void);
