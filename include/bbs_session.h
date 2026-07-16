#pragma once
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include "bbs_config.h"
#include "bbs_telnet.h"
#include "bbs_db.h"
#include "bbs_menu.h"

#define MAX_ONLINE 256

typedef struct Session {
  int fd;
  int (*io_write)(void *user_data, const uint8_t *data, size_t len);
  int (*io_readline)(void *user_data, uint8_t *buf, size_t cap, int timeout_sec, int echo);
  int (*io_read)(void *user_data, uint8_t *buf, size_t cap, int timeout_sec);
  void (*io_flush)(void *user_data);
  void (*io_close)(void *user_data);
  void *io_user_data;
  char ip[64];
  BbsConfig cfg;
  BbsDb* db;
  TelnetState tn;

  /* runtime */
  int alive;
  time_t started_at;
  int node_num;
  DbUser user;
  int time_left_min;   /* computed remaining */
  int credits;         /* working copy; decremented by downloads */
  int file_points;     /* upload points */
  int chat_channel;    /* current multi-node channel */
  time_t last_action;
  int current_msg_area;
  int current_file_area;
  int current_conf;        /* current conference (0-25 for A-Z) */
  int batch_queue[32];
  int batch_area[32];
  int batch_count;
  struct {
    char kind;
    int area_id;
  } area_password_cache[32];
  int area_password_cache_count;
  int line_chat_partner; /* node number */
  int ansi;              /* 1=ANSI graphics enabled, 0=ASCII only */
  int call_id;           /* call_history record id for this session */
  char file_scan_date[32]; /* FP override; empty = use last_login_at */
  int pages_this_session; /* count of sysop page attempts this session */

  /* Inter-session chat message queue (used by split-screen chat) */
  pthread_mutex_t chat_inbox_lock;
  char chat_inbox[8][256]; /* ring buffer of incoming display lines */
  int  chat_inbox_head;
  int  chat_inbox_tail;
} Session;

void* session_thread_main(void* arg);

/* session I/O helpers */
void send_str(Session* s, const char* str);
int session_readline(Session* s, uint8_t* buf, size_t cap, int timeout);
int session_readline_echo(Session* s, uint8_t* buf, size_t cap, int timeout, int echo);
int prompt_line(Session* s, const char* prompt, char* out, size_t cap);
int prompt_password(Session* s, const char* prompt, char* out, size_t cap);

/* online registry */
bool online_add(Session* s);
void online_remove(Session* s);
size_t online_list(char* out, size_t cap);
void online_broadcast(const char* msg);
/* Stop every active session and wait until its detached worker has completed. */
void online_shutdown_and_wait(void);
Session* online_get_node(int node_num);
bool online_mark_node_dead(int node_num, const Session *except, const char *msg);
void online_set_node_locked(int node_num, bool locked);
bool online_node_is_locked(int node_num);
void bbs_handle_action(Session *s, const char *action);
