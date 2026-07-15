#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct BbsDb BbsDb;

typedef enum DbBindType
{
  DB_BIND_NULL = 0,
  DB_BIND_INT,
  DB_BIND_INT64,
  DB_BIND_TEXT,
  DB_BIND_BLOB
} DbBindType;

typedef struct DbBind
{
  DbBindType type;
  union
  {
    int i;
    int64_t i64;
    const char *text;
    struct
    {
      const void *data;
      size_t len;
    } blob;
  } v;
} DbBind;

#define DB_BIND_NULL_VAL ((DbBind){DB_BIND_NULL, {.i64 = 0}})
#define DB_BIND_INT_VAL(x) ((DbBind){DB_BIND_INT, {.i = (x)}})
#define DB_BIND_INT64_VAL(x) ((DbBind){DB_BIND_INT64, {.i64 = (int64_t)(x)}})
#define DB_BIND_TEXT_VAL(x) ((DbBind){DB_BIND_TEXT, {.text = (x)}})
#define DB_BIND_BLOB_VAL(ptr, n) ((DbBind){DB_BIND_BLOB, {.blob = {(ptr), (n)}}})
typedef struct DbUser
{
  int id;
  char handle[64];
  char real_name[64];
  char pw_hash[128];
  char email[64];
  char phone[20];
  char street[64];
  char city_state[64];
  char zip_code[16];
  char caller_id[32];
  char forgot_pw_question[128];
  char forgot_pw_answer[128]; /* hashed */
  char sex;                   /* M/F/U */
  char birth_date[16];        /* YYYY-MM-DD */
  int security_level_id;
  int level; /* security level value */
  int dsl;   /* download security level */
  int time_limit_min;
  unsigned flags;        /* AR flags (A-Z bitset) */
  unsigned ac_flags;     /* activity/restriction flags */
  unsigned status_flags; /* lock/delete/etc */
  int credits;           /* download credits */
  int file_points;       /* upload/file points */
  int on_today;          /* calls today */
  int illegal;           /* illegal logon attempts */
  int def_arc_type;      /* default archive type */
  int color_scheme;      /* user color scheme */
  int user_start_menu;   /* starting menu */
  char first_on[32];     /* first logon date */
  int t_time_on;         /* total time on (minutes) */
  char last_qwk[32];     /* last QWK packet date */
  int uploads;           /* upload count */
  int downloads;         /* download count */
  int uk;                /* upload KB */
  int dk;                /* download KB */
  int logged_on;         /* total logons */
  int msg_post;          /* messages posted */
  int email_sent;        /* emails sent */
  int feedback;          /* feedback sent */
  int timebank;          /* time bank balance */
  int timebank_add;      /* daily timebank addition */
  int dl_k_today;        /* download KB today */
  int dl_today;          /* downloads today */
  char usr_def_str1[64]; /* custom field 1 */
  char usr_def_str2[64]; /* custom field 2 */
  char usr_def_str3[64]; /* custom field 3 */
  char social_link[128]; /* social media link */
  char sysop_msg[256];   /* message to sysop from registration */
  char note[64];         /* sysop note */
  char locked_file[16];  /* lockout message file */
  char last_login_at[32];
  char expires_at[32];
  char pw_changed_at[32]; /* password last changed */
  int smw;                /* short message waiting */
  int dl_ratio_num;
  int dl_ratio_den;
  int post_ratio_num;
  int post_ratio_den;
  char signature[256]; /* user signature for messages */
  char tagline[128];   /* user tagline for messages */
  int use_signature;   /* 1=append signature to posts */
  int use_tagline;     /* 1=append tagline to posts */
  int use_fse;         /* 1=use full-screen editor for composition */
} DbUser;

typedef struct DbSecurityLevel
{
  int id;
  char name[64];
  int level;
  int time_limit_min;
  int call_allow;   /* calls per day */
  int dl_one_day;   /* downloads per day */
  int dl_k_one_day; /* download KB per day */
  int download_ratio_num;
  int download_ratio_den;
  int post_ratio_num;
  int post_ratio_den;
  int ul_dl_ratio_num; /* UL/DL ratio numerator */
  int ul_dl_ratio_den; /* UL/DL ratio denominator */
  int post_call_ratio; /* posts per call ratio */
  int email_allow;
  int vote_allow;
  int anon_allow;
  unsigned flags;
} DbSecurityLevel;

typedef struct DbValidationLevel
{
  int id;
  char key; /* A-Z */
  char description[64];
  char user_msg[256]; /* message shown to user */
  int new_sl;         /* new security level */
  int new_dsl;        /* new download security level */
  int new_menu;       /* new starting menu */
  int expiration;     /* days until expiration (0=never) */
  int expire_to;      /* validation level to expire to */
  int new_fp;         /* new file points */
  int new_credit;     /* new credits */
  unsigned soft_ar;   /* soft AR flags (add, don't replace) */
  unsigned soft_ac;   /* soft AC flags (add, don't replace) */
  unsigned new_ar;    /* new AR flags (hard set) */
  unsigned new_ac;    /* new AC flags (hard set) */
} DbValidationLevel;

typedef struct DbNode
{
  int node_num;
  int user_id;
  char handle[64];
  char status[16];
  char activity[64];
  char ip[64];
} DbNode;

typedef struct DbMsgArea
{
  int id;
  char name[64];
  char filename[32];
  char acs[64];
  char acs_read[64];
  char acs_post[64];
  char acs_sysop[64];
  int anon_policy; /* ANON_NO, ANON_YES, ANON_FORCED, etc. */
  unsigned flags;  /* MA_FLAG_* */
  char password[32];
  char origin[80];
  int max_msgs;
} DbMsgArea;

typedef struct DbMessage
{
  int id;
  int area_id;
  int user_id;
  int to_user;
  int reply_to;
  int thread_root;
  char user_handle[64];
  char from_name[64]; /* for anonymous/alias posting */
  char to_name[64];   /* recipient name */
  char subject[80];
  char body[2048]; /* increased size */
  char created_at[32];
  unsigned attr;     /* MSG_ATTR_* */
  unsigned net_attr; /* NET_ATTR_* */
  char file_attached[64];
  char origin[80];
} DbMessage;

typedef struct DbFileArea
{
  int id;
  char name[64];
  char path[256];
  char acs_list[64];
  char acs_download[64];
  char acs_upload[64];
  char acs_sysop[64];
  char password[32];
  int max_files;
  char archive_type[16];
  int sort_type;
  int show_uploader;
  int check_dupes;
  int free_files;
  int flags;
} DbFileArea;

typedef struct DbFileRec
{
  int id;
  int area_id;
  char filename[128];
  char desc[256];
  char extended_desc[2048];
  char file_id_diz[2048];
  int size_bytes;
  char uploaded_at[32];
  int uploaded_by;
  char uploader[64];
  char sha256[65];
  int file_points;
  int download_count;
  int owner_credit;
  int flags;
} DbFileRec;

typedef struct DbBulletin
{
  int id;
  char title[128];
  char body[1024];
  char posted_at[32];
  char posted_by[64];
  char acs[64];
} DbBulletin;

typedef struct DbOneliner
{
  int id;
  int user_id;
  char user_handle[64];
  char text[80];
  char posted_at[32];
} DbOneliner;

typedef struct DbShortMessage
{
  int id;
  int from_user;
  int to_user;
  char from_handle[64];
  char to_handle[64];
  char message[256];
  char sent_at[32];
  int read_flag;
} DbShortMessage;

typedef struct DbAutomsg
{
  char msg[512];
  char set_by[64];
  char set_at[32];
} DbAutomsg;

typedef struct DbStats
{
  int calls;
  int uploads;
  int downloads;
  int posts;
  int emails;
} DbStats;

typedef struct DbVote
{
  int id;
  char title[128];
  char closes_at[32];
} DbVote;

typedef struct DbVoteChoice
{
  int id;
  int vote_id;
  char label[128];
} DbVoteChoice;

typedef struct DbDoor
{
  int id;
  char name[64];
  char dropfile[64];
  char command[256];
  char workdir[256];
  char acs[64];
  char runner[32];      /* "native" (default) or "dosbox" */
  char manifest[512];   /* path to JSON manifest for dosbox doors */
  int  enabled;         /* 1=enabled, 0=disabled */
  int  timeout_sec;     /* 0=use global default */
} DbDoor;

typedef struct DbProtocol
{
  int id;
  char name[32];
  char direction[8];
  char command[256];
  int active;
} DbProtocol;

typedef struct DbConference
{
  int id;
  char key[32];
  char name[64];
  char description[256];
  char acs[64];
  int flags;
} DbConference;

typedef struct DbSubscriptionType
{
  int id;
  char name[64];
  int days;
  int security_level_id;
  int expired_level_id;
  int price;
  char description[256];
} DbSubscriptionType;

typedef struct DbUserSubscription
{
  int id;
  int user_id;
  int subscription_type_id;
  char started_at[32];
  char expires_at[32];
  char status[16];
} DbUserSubscription;

typedef struct DbCallHistory
{
  int id;
  int user_id;
  char handle[64];
  int node_num;
  char login_at[32];
  char logout_at[32];
  int duration_min;
  char ip_address[64];
} DbCallHistory;

typedef struct DbEvent
{
  int id;
  char name[64];
  char schedule[64];
  char command[256];
  char next_run[32];
  char event_type[16]; /* scheduled, logon, permission */
  char acs[64];
  int warning_min;
  int enabled;
} DbEvent;

BbsDb *db_open(const char *path);
void db_close(BbsDb *db);

/* Initialize / apply schema from a .sql file. */
bool db_init_schema(BbsDb *db, const char *schema_path);

/* Execute a single SQL statement (used for small tasks). */
bool db_exec(BbsDb *db, const char *sql);
int db_exec_simple(BbsDb *db, const char *sql); /* returns rows affected, -1 on error */
int db_changes(BbsDb *db);
const char *db_last_error(BbsDb *db);
bool db_query(BbsDb *db, const char *sql, bool (*row_cb)(void *row, void *ctx), void *ctx);
int db_query_int(BbsDb *db, const char *sql, int default_val);
bool db_exec_prepared(BbsDb *db, const char *sql, const DbBind *binds, int bind_count);
int db_query_int_prepared(BbsDb *db, const char *sql, const DbBind *binds, int bind_count, int default_val);

/* User helpers */
bool db_user_fetch(BbsDb *db, const char *handle, DbUser *out);
bool db_user_create(BbsDb *db, const char *handle, const char *pw_hash, int security_level_id);

/* Extended user creation with registration fields */
typedef struct DbUserRegInfo
{
  const char *handle;
  const char *pw_hash;
  const char *email;       /* required */
  const char *city_state;  /* required (city, state/region) */
  const char *social_link; /* optional */
  const char *sysop_msg;   /* optional message to sysop */
  int security_level_id;
} DbUserRegInfo;
bool db_user_create_ex(BbsDb *db, const DbUserRegInfo *info);

bool db_user_touch_login(BbsDb *db, int user_id);
bool db_seed_defaults(BbsDb *db, const char *sysop_pw_hash);
bool db_user_set_pw(BbsDb *db, int user_id, const char *pw_hash);
bool db_user_clear_smw(BbsDb *db, int user_id);
bool db_user_set_security_question(BbsDb *db, int user_id, const char *question, const char *answer_hash);
bool db_user_get_security_question(BbsDb *db, const char *handle, char *question, size_t qlen, char *answer_hash, size_t alen);
bool db_user_set_pw_with_timestamp(BbsDb *db, int user_id, const char *pw_hash);
int db_user_pw_age_days(BbsDb *db, int user_id); /* Returns days since password change, -1 on error */

/* Subscription management */
int db_subscription_type_list(BbsDb *db, DbSubscriptionType *out, int max);
bool db_subscription_type_add(BbsDb *db, const char *name, int days, int level_id, int expired_level_id, int price, const char *desc);
bool db_subscription_type_get(BbsDb *db, int id, DbSubscriptionType *out);
bool db_user_subscribe(BbsDb *db, int user_id, int type_id);
bool db_user_subscription_get(BbsDb *db, int user_id, DbUserSubscription *out);
int db_subscription_check_expired(BbsDb *db); /* Returns count of expired subscriptions processed */
bool db_user_set_expires(BbsDb *db, int user_id, const char *expires_at);

/* Security levels */
bool db_security_level_fetch(BbsDb *db, int id, DbSecurityLevel *out);
int db_security_level_list(BbsDb *db, DbSecurityLevel *out, int max_levels);
bool db_security_level_update(BbsDb *db, const DbSecurityLevel *sl);

/* Validation levels */
bool db_validation_level_fetch(BbsDb *db, char key, DbValidationLevel *out);
int db_validation_level_list(BbsDb *db, DbValidationLevel *out, int max_levels);
bool db_validation_level_create(BbsDb *db, const DbValidationLevel *vl);
bool db_validation_level_update(BbsDb *db, const DbValidationLevel *vl);
bool db_validation_level_apply(BbsDb *db, int user_id, char key);

/* Node helpers */
bool db_node_upsert(BbsDb *db, int node_num, int user_id, const char *status, const char *activity, const char *ip);
bool db_node_clear(BbsDb *db, int node_num);
int db_node_list(BbsDb *db, DbNode *out, int max_nodes);
bool db_node_user_online(BbsDb *db, int user_id, int *out_node);
bool db_node_lock_set(BbsDb *db, int node_num, bool locked, const char *actor);
bool db_node_lock_get(BbsDb *db, int node_num);
int db_node_lock_list(BbsDb *db, int *out_nodes, int max_nodes);

/* Buccaneer live host storage */
bool db_bucc_kv_get(BbsDb *db, const char *scope, const char *key, char *out, size_t out_cap);
bool db_bucc_kv_set(BbsDb *db, const char *scope, const char *key, const char *value);
bool db_bucc_kv_delete(BbsDb *db, const char *scope, const char *key);
bool db_bucc_kv_exists(BbsDb *db, const char *scope, const char *key);
int64_t db_bucc_data_insert(BbsDb *db, const char *scope, const char *dataset, const char *value);
bool db_bucc_data_update(BbsDb *db, const char *scope, const char *dataset, int64_t id, const char *value);
bool db_bucc_data_delete(BbsDb *db, const char *scope, const char *dataset, int64_t id);
bool db_bucc_data_get(BbsDb *db, const char *scope, const char *dataset, int64_t id, char *out, size_t out_cap);
int db_bucc_data_find(BbsDb *db, const char *scope, const char *dataset, int64_t *ids, char values[][512], int max_rows);
int64_t db_bucc_data_count(BbsDb *db, const char *scope, const char *dataset);

/* Message areas */
int db_msg_area_list(BbsDb *db, DbMsgArea *out, int max_areas);
bool db_msg_area_seed(BbsDb *db, const char *name);

/* Messages */
int db_messages_list(BbsDb *db, int area_id, DbMessage *out, int max_msgs);
int db_messages_since(BbsDb *db, int area_id, const char *since, DbMessage *out, int max_msgs);
bool db_user_set_last_qwk(BbsDb *db, int user_id, const char *ts);
bool db_message_post(BbsDb *db, int area_id, int user_id, const char *subject, const char *body, int reply_to);
bool db_message_post_ex(BbsDb *db, const DbMessage *msg);
bool db_message_get(BbsDb *db, int msg_id, DbMessage *out);
bool db_message_forward(BbsDb *db, int msg_id, int to_user_id);
bool db_message_mass_mail(BbsDb *db, int from_user_id, const char *subject, const char *body,
                          const int *to_users, int to_count);
bool db_message_reply_tree(BbsDb *db, int area_id, int root_id, DbMessage *out, int max_msgs, int *out_count);
bool db_message_area_manage(BbsDb *db, const char *name, const char *acs, int *out_id, bool delete_flag);
bool db_message_set_to_user(BbsDb *db, int msg_id, int to_user);
bool db_message_update_body(BbsDb *db, int msg_id, const char *body);

/* File areas */
int db_file_area_list(BbsDb *db, DbFileArea *out, int max);
bool db_file_area_seed(BbsDb *db, const char *name, const char *path);
bool db_file_area_manage(BbsDb *db, const char *name, const char *path, const char *acs, int *out_id, bool delete_flag);

/* Files */
int db_file_list(DbFileArea *area, BbsDb *db, DbFileRec *out, int max);
int db_file_list_older(BbsDb *db, int days, DbFileRec *out, int max);
bool db_file_add(BbsDb *db, int area_id, const char *filename, const char *desc, int size_bytes, int user_id);
bool db_file_add_ex(BbsDb *db, int area_id, const char *filename, const char *desc,
                    const char *extended_desc, const char *file_id_diz, int size_bytes,
                    int user_id, int file_points, int flags);
int db_last_insert_id(BbsDb *db);
bool db_file_get(BbsDb *db, int file_id, DbFileRec *out);
bool db_file_update(BbsDb *db, const DbFileRec *file);
bool db_file_mark_hash(BbsDb *db, int file_id, const char *sha256);
bool db_file_exists_hash(BbsDb *db, const char *sha256, int *out_file_id);
bool db_file_exists_name(BbsDb *db, int area_id, const char *filename, int *out_file_id);
bool db_file_inc_downloads(BbsDb *db, int file_id);
bool db_file_delete(BbsDb *db, int file_id);

/* Counts (stats) */
int db_count_messages(BbsDb *db);
int db_count_messages_area(BbsDb *db, int area_id);
int db_count_user_posts(BbsDb *db, int user_id);
int db_count_files(BbsDb *db);
int db_count_files_area(BbsDb *db, int area_id);
int db_count_users(BbsDb *db);
int db_stats_get_val(BbsDb *db, const char *key);
bool db_msg_area_get(BbsDb *db, int area_id, DbMsgArea *out);
bool db_file_area_get(BbsDb *db, int area_id, DbFileArea *out);
int db_messages_to_user(BbsDb *db, int user_id, DbMessage *out, int max_msgs);
bool db_user_set_smw(BbsDb *db, int user_id, int smw);

/* File storage helpers (filesystem) */
bool file_store_copy(const char *src_path, const char *dst_path, int *size_bytes_out);

/* Path helper to resolve within a file area safely. Returns false if traversal detected. */
bool file_area_resolve(const char *area_path, const char *filename, char *out, size_t out_cap);

/* Bulletins / automsg / stats */
int db_bulletin_list(BbsDb *db, DbBulletin *out, int max);
bool db_bulletin_add(BbsDb *db, const char *title, const char *body, int user_id, const char *acs);
bool db_automsg_get(BbsDb *db, DbAutomsg *out);
bool db_automsg_set(BbsDb *db, const char *msg, int user_id);

/* One-liners */
int db_oneliner_list(BbsDb *db, DbOneliner *out, int max);
bool db_oneliner_add(BbsDb *db, int user_id, const char *handle, const char *text);
bool db_oneliner_delete(BbsDb *db, int id);
int db_oneliner_count(BbsDb *db);

/* Short messages (SMW) */
bool db_smw_send(BbsDb *db, int from_user, const char *from_handle, int to_user, const char *to_handle, const char *message);
int db_smw_list(BbsDb *db, int user_id, DbShortMessage *out, int max);
int db_smw_count(BbsDb *db, int user_id);
bool db_smw_mark_read(BbsDb *db, int msg_id);
bool db_smw_delete(BbsDb *db, int msg_id);
bool db_stats_get(BbsDb *db, DbStats *out);
bool db_stats_inc(BbsDb *db, const char *field);

/* Call history */
int db_call_log_start(BbsDb *db, int user_id, const char *handle, int node_num, const char *ip);
bool db_call_log_end(BbsDb *db, int call_id, int duration_min);
int db_call_history_list(BbsDb *db, DbCallHistory *out, int max);

/* Votes */
int db_vote_list(BbsDb *db, DbVote *out, int max);
bool db_vote_cast(BbsDb *db, int vote_id, int choice_id, int user_id);
int db_vote_choices(BbsDb *db, int vote_id, DbVoteChoice *out, int max);
int db_vote_results(BbsDb *db, int vote_id, int *choice_ids, int *counts, int max);
int db_vote_total(BbsDb *db, int vote_id);
bool db_vote_add(BbsDb *db, const char *title, const char *closes_at);
bool db_vote_delete(BbsDb *db, int vote_id);
bool db_vote_choice_add(BbsDb *db, int vote_id, const char *label);

/* Doors / protocols */
int db_doors_list(BbsDb *db, DbDoor *out, int max);
bool db_door_get(BbsDb *db, int door_id, DbDoor *out);
bool db_door_add(BbsDb *db, const char *name, const char *dropfile, const char *cmd, const char *workdir, const char *acs);
int db_protocols_list(BbsDb *db, DbProtocol *out, int max, const char *direction);
bool db_protocol_add(BbsDb *db, const char *name, const char *direction, const char *command);
bool db_protocol_update(BbsDb *db, int id, const char *name, const char *direction, const char *command, int active);
bool db_protocol_delete(BbsDb *db, int id);
bool db_protocol_get(BbsDb *db, int id, DbProtocol *out);

/* Users (update / editor) */
bool db_user_update_flags(BbsDb *db, int user_id, unsigned flags, unsigned ac_flags, unsigned status_flags);
bool db_user_update_level(BbsDb *db, int user_id, int security_level_id);
bool db_user_update_time_credit(BbsDb *db, int user_id, int time_min, int credits, int file_points);
bool db_user_update(BbsDb *db, const DbUser *u);
bool db_user_update_stats(BbsDb *db, int user_id, int uploads, int downloads, int uk, int dk,
                          int msg_post, int email_sent, int feedback, int logged_on);
bool db_timebank_get(BbsDb *db, int user_id, int *minutes_out);
bool db_timebank_add(BbsDb *db, int user_id, int delta_minutes, int *new_balance_out);
bool db_mail_packet_add(BbsDb *db, int user_id, const char *kind, const char *path);

/* Conference management */
int db_conference_list(BbsDb *db, DbConference *out, int max);
bool db_conference_get(BbsDb *db, int conf_id, DbConference *out);
bool db_conference_add(BbsDb *db, const char *key, const char *name, const char *desc, const char *acs);
bool db_conference_update(BbsDb *db, int conf_id, const char *name, const char *desc, const char *acs, int flags);
bool db_conference_delete(BbsDb *db, int conf_id);

/* Conference membership */
bool db_conf_is_member(BbsDb *db, int user_id, int conf_id);
bool db_conf_join(BbsDb *db, int user_id, int conf_id);
bool db_conf_leave(BbsDb *db, int user_id, int conf_id);
int db_conf_list_user(BbsDb *db, int user_id, int *conf_ids, int max);

/* Events (scheduler) */
int db_events_list(BbsDb *db, DbEvent *out, int max);
bool db_event_update_next(BbsDb *db, int id, const char *next_run);
bool db_event_mark_ran(BbsDb *db, int id);
bool db_event_add(BbsDb *db, const char *name, const char *schedule,
                  const char *command, const char *event_type, const char *acs);
bool db_event_delete(BbsDb *db, int event_id);
bool db_event_toggle(BbsDb *db, int event_id, int enabled);

/* WFC daily stats */
typedef struct DbDailyStats
{
  char date[16];
  int calls;
  int posts;
  int emails;
  int newusers;
  int feedback;
  int uploads;
  int downloads;
  int64_t ul_kb;
  int64_t dl_kb;
  int minutes;
  int errors;
} DbDailyStats;

typedef struct DbSystemTotals
{
  int total_calls;
  int total_posts;
  int total_uploads;
  int total_downloads;
  int total_usage;
  int days_online;
  int total_users;
} DbSystemTotals;

bool db_daily_stats_get(BbsDb *db, DbDailyStats *out);
bool db_daily_stats_inc(BbsDb *db, const char *field, int delta);
bool db_daily_stats_reset(BbsDb *db);
bool db_system_totals_get(BbsDb *db, DbSystemTotals *out);
bool db_system_totals_inc(BbsDb *db, const char *field, int delta);
bool db_history_record(BbsDb *db, const DbDailyStats *stats);
int db_history_list(BbsDb *db, DbDailyStats *out, int max);
int db_days_online(BbsDb *db);

/* FidoNet address (Zone:Net/Node.Point) */
typedef struct DbFidoAka
{
  int id;
  int zone;
  int net;
  int node;
  int point;
  char domain[64];
  int is_primary;
} DbFidoAka;

/* FidoNet echomail link */
typedef struct DbFidoEcholink
{
  int id;
  int area_id;
  char echotag[64];
  int aka_id;
  char origin[80];
  int high_water;
} DbFidoEcholink;

/* FidoNet netmail message */
typedef struct DbFidoNetmail
{
  int id;
  int from_zone;
  int from_net;
  int from_node;
  int from_point;
  char from_name[64];
  int to_zone;
  int to_net;
  int to_node;
  int to_point;
  char to_name[64];
  char subject[80];
  char body[2048];
  unsigned attr;
  char created_at[32];
  char sent_at[32];
  char status[16];
} DbFidoNetmail;

/* FidoNet AKA management */
int db_fido_aka_list(BbsDb *db, DbFidoAka *out, int max);
bool db_fido_aka_add(BbsDb *db, int zone, int net, int node, int point, const char *domain, int is_primary);
bool db_fido_aka_get(BbsDb *db, int id, DbFidoAka *out);
bool db_fido_aka_get_primary(BbsDb *db, DbFidoAka *out);
bool db_fido_aka_update(BbsDb *db, int id, int zone, int net, int node, int point, const char *domain, int is_primary);
bool db_fido_aka_delete(BbsDb *db, int id);

/* FidoNet echomail links */
int db_fido_echolink_list(BbsDb *db, DbFidoEcholink *out, int max);
bool db_fido_echolink_add(BbsDb *db, int area_id, const char *echotag, int aka_id, const char *origin);
bool db_fido_echolink_get(BbsDb *db, int id, DbFidoEcholink *out);
bool db_fido_echolink_get_by_area(BbsDb *db, int area_id, DbFidoEcholink *out);
bool db_fido_echolink_update(BbsDb *db, int id, const char *echotag, int aka_id, const char *origin);
bool db_fido_echolink_delete(BbsDb *db, int id);
bool db_fido_echolink_update_highwater(BbsDb *db, int id, int high_water);

/* FidoNet netmail */
int db_fido_netmail_list(BbsDb *db, const char *status, DbFidoNetmail *out, int max);
bool db_fido_netmail_add(BbsDb *db, const DbFidoNetmail *nm);
bool db_fido_netmail_get(BbsDb *db, int id, DbFidoNetmail *out);
bool db_fido_netmail_mark_sent(BbsDb *db, int id);
bool db_fido_netmail_delete(BbsDb *db, int id);

/* FidoNet echomail queue */
bool db_fido_echo_queue_add(BbsDb *db, int echolink_id, int message_id);
int db_fido_echo_queue_pending(BbsDb *db, int echolink_id, int *message_ids, int max);
bool db_fido_echo_queue_mark_exported(BbsDb *db, int echolink_id, int message_id);

/* FidoNet packet I/O helpers */
bool fido_format_address(const DbFidoAka *aka, char *out, size_t cap);
bool fido_parse_address(const char *str, int *zone, int *net, int *node, int *point);

/* QWK Network Hub */
typedef struct DbQwkHub
{
  int id;
  char name[64];
  char bbs_id[16];
  char call_schedule[64];
  char last_call[32];
  int enabled;
} DbQwkHub;

/* QWK Network Area Link */
typedef struct DbQwkAreaLink
{
  int id;
  int hub_id;
  int area_id;
  int remote_conf;
  int high_water_in;
  int high_water_out;
} DbQwkAreaLink;

/* QWK Network Packet */
typedef struct DbQwkPacket
{
  int id;
  int hub_id;
  char packet_type[8];
  char packet_path[256];
  char status[16];
  char created_at[32];
  char processed_at[32];
} DbQwkPacket;

/* QWK Hub management */
int db_qwk_hub_list(BbsDb *db, DbQwkHub *out, int max);
bool db_qwk_hub_add(BbsDb *db, const char *name, const char *bbs_id, const char *schedule);
bool db_qwk_hub_get(BbsDb *db, int id, DbQwkHub *out);
bool db_qwk_hub_update(BbsDb *db, int id, const char *name, const char *bbs_id, const char *schedule, int enabled);
bool db_qwk_hub_delete(BbsDb *db, int id);
bool db_qwk_hub_mark_called(BbsDb *db, int id);

/* QWK Area links */
int db_qwk_area_link_list(BbsDb *db, int hub_id, DbQwkAreaLink *out, int max);
bool db_qwk_area_link_add(BbsDb *db, int hub_id, int area_id, int remote_conf);
bool db_qwk_area_link_get(BbsDb *db, int id, DbQwkAreaLink *out);
bool db_qwk_area_link_update(BbsDb *db, int id, int remote_conf);
bool db_qwk_area_link_delete(BbsDb *db, int id);
bool db_qwk_area_link_update_highwater(BbsDb *db, int id, int hw_in, int hw_out);

/* QWK Packet queue */
int db_qwk_packet_list(BbsDb *db, int hub_id, const char *status, DbQwkPacket *out, int max);
bool db_qwk_packet_add(BbsDb *db, int hub_id, const char *packet_type, const char *path);
bool db_qwk_packet_mark_processed(BbsDb *db, int id);
bool db_qwk_packet_delete(BbsDb *db, int id);

/* Chat log entry */
typedef struct DbChatLog
{
  int id;
  char chat_type[16];
  int room_id;
  int from_user;
  char from_handle[64];
  int to_user;
  char to_handle[64];
  char message[512];
  char logged_at[32];
} DbChatLog;

/* Chat logging */
bool db_chat_log(BbsDb *db, const char *chat_type, int room_id, int from_user, const char *from_handle,
                 int to_user, const char *to_handle, const char *message);
int db_chat_log_list(BbsDb *db, const char *chat_type, int room_id, DbChatLog *out, int max);
bool db_chat_log_clear(BbsDb *db, int days_old);

/* Per-user area scan flags (MZ toggle) */
int  db_user_scan_area_get(BbsDb *db, int user_id, int area_id); /* 1=scan, 0=skip */
bool db_user_scan_area_set(BbsDb *db, int user_id, int area_id, int enabled);

/* Drafts */
typedef struct DbDraft {
  int  id;
  int  user_id;
  int  area_id;
  int  to_user_id;
  char to_name[64];
  char subject[80];
  char body[2048];
  char created_at[32];
} DbDraft;

bool db_draft_save(BbsDb *db, int user_id, int area_id, int to_user_id,
                   const char *to_name, const char *subject, const char *body);
bool db_draft_get(BbsDb *db, int user_id, DbDraft *out);
bool db_draft_delete(BbsDb *db, int draft_id);
int  db_draft_count(BbsDb *db, int user_id);

/* Mailbox capacity */
int  db_count_messages_to_user_inbox(BbsDb *db, int user_id);

/* User message preferences */
bool db_user_set_use_fse(BbsDb *db, int user_id, int use_fse);
bool db_user_set_signature(BbsDb *db, int user_id, const char *sig, int use_sig);
bool db_user_set_tagline(BbsDb *db, int user_id, const char *tag, int use_tag);
