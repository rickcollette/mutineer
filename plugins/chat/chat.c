/*
 * chat_plugin.c - Multiuser Chat Plugin
 *
 * This plugin demonstrates multiuser safety with a shared message queue.
 * Multiple users can chat in real-time within the plugin.
 */

#include "bbs_plugin_api.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

/* Store the host API globally for this plugin */
static const bbs_host_api_t* H = NULL;

/* Chat message structure */
#define CHAT_MAX_MESSAGES 100
#define CHAT_MAX_USERS 32

typedef struct chat_message {
  time_t timestamp;
  char sender[64];
  char text[256];
  int valid;
} chat_message_t;

/* Global chat state (shared across all instances) */
static struct {
  chat_message_t messages[CHAT_MAX_MESSAGES];
  char users[CHAT_MAX_USERS][64];
  int user_active[CHAT_MAX_USERS];
  int head;  /* Next write position */
  int count; /* Total messages posted */
  pthread_mutex_t mu;
  int initialized;
} g_chat = {
  .head = 0,
  .count = 0,
  .mu = PTHREAD_MUTEX_INITIALIZER,
  .initialized = 0
};

/* Per-instance state */
typedef struct chat_inst {
  char username[64];
  int last_seen;  /* Last message index seen */
  int running;
  int active;
} chat_inst_t;

static void chat_user_join(const char* username) {
  pthread_mutex_lock(&g_chat.mu);
  for (int i = 0; i < CHAT_MAX_USERS; i++) {
    if (g_chat.user_active[i] && strcmp(g_chat.users[i], username) == 0) {
      pthread_mutex_unlock(&g_chat.mu);
      return;
    }
  }
  for (int i = 0; i < CHAT_MAX_USERS; i++) {
    if (!g_chat.user_active[i]) {
      strncpy(g_chat.users[i], username, sizeof(g_chat.users[i]) - 1);
      g_chat.users[i][sizeof(g_chat.users[i]) - 1] = '\0';
      g_chat.user_active[i] = 1;
      break;
    }
  }
  pthread_mutex_unlock(&g_chat.mu);
}

static void chat_user_leave(const char* username) {
  pthread_mutex_lock(&g_chat.mu);
  for (int i = 0; i < CHAT_MAX_USERS; i++) {
    if (g_chat.user_active[i] && strcmp(g_chat.users[i], username) == 0) {
      g_chat.user_active[i] = 0;
      g_chat.users[i][0] = '\0';
      break;
    }
  }
  pthread_mutex_unlock(&g_chat.mu);
}

static int chat_list_users(char users[][64], int max_users) {
  pthread_mutex_lock(&g_chat.mu);
  int count = 0;
  for (int i = 0; i < CHAT_MAX_USERS && count < max_users; i++) {
    if (g_chat.user_active[i]) {
      strncpy(users[count], g_chat.users[i], 63);
      users[count][63] = '\0';
      count++;
    }
  }
  pthread_mutex_unlock(&g_chat.mu);
  return count;
}

/* Post a message to the chat */
static void chat_post(const char* sender, const char* text) {
  pthread_mutex_lock(&g_chat.mu);
  
  chat_message_t* msg = &g_chat.messages[g_chat.head];
  msg->timestamp = time(NULL);
  strncpy(msg->sender, sender ? sender : "???", sizeof(msg->sender) - 1);
  msg->sender[sizeof(msg->sender) - 1] = '\0';
  strncpy(msg->text, text ? text : "", sizeof(msg->text) - 1);
  msg->text[sizeof(msg->text) - 1] = '\0';
  msg->valid = 1;
  
  g_chat.head = (g_chat.head + 1) % CHAT_MAX_MESSAGES;
  g_chat.count++;
  
  pthread_mutex_unlock(&g_chat.mu);
}

/* Get new messages since last_seen, returns count */
static int chat_get_new(int last_seen, chat_message_t* out, int max_out, int* new_last_seen) {
  pthread_mutex_lock(&g_chat.mu);
  
  int count = 0;
  int current = g_chat.count;
  
  /* Find messages newer than last_seen */
  for (int i = 0; i < CHAT_MAX_MESSAGES && count < max_out; i++) {
    int idx = (g_chat.head - 1 - i + CHAT_MAX_MESSAGES) % CHAT_MAX_MESSAGES;
    chat_message_t* msg = &g_chat.messages[idx];
    
    if (!msg->valid) continue;
    
    int msg_num = current - i - 1;
    if (msg_num <= last_seen) break;
    
    /* Copy message */
    memcpy(&out[count], msg, sizeof(chat_message_t));
    count++;
  }
  
  *new_last_seen = current;
  
  pthread_mutex_unlock(&g_chat.mu);
  
  /* Reverse to get chronological order */
  for (int i = 0; i < count / 2; i++) {
    chat_message_t tmp = out[i];
    out[i] = out[count - 1 - i];
    out[count - 1 - i] = tmp;
  }
  
  return count;
}

/* Instance vtable functions */

static bbs_rc_t chat_enter(void* inst, bbs_session_t* s) {
  chat_inst_t* ci = (chat_inst_t*)inst;
  
  /* Store username */
  const char* user = H->session_username(s);
  strncpy(ci->username, user ? user : "Anonymous", sizeof(ci->username) - 1);
  ci->username[sizeof(ci->username) - 1] = '\0';
  
  /* Set last_seen to current count so we don't show old messages */
  pthread_mutex_lock(&g_chat.mu);
  ci->last_seen = g_chat.count;
  pthread_mutex_unlock(&g_chat.mu);
  
  ci->running = 1;
  ci->active = 1;
  chat_user_join(ci->username);
  
  H->io->cls(s);
  H->io->printf(s, "\x1b[1;35m========================================\x1b[0m\r\n");
  H->io->printf(s, "\x1b[1;35m       MULTIUSER CHAT PLUGIN v1.0      \x1b[0m\r\n");
  H->io->printf(s, "\x1b[1;35m========================================\x1b[0m\r\n");
  H->io->printf(s, "\r\n");
  H->io->printf(s, "Welcome to the chat room, \x1b[1;33m%s\x1b[0m!\r\n", ci->username);
  H->io->printf(s, "\r\n");
  H->io->printf(s, "Commands:\r\n");
  H->io->printf(s, "  /quit, /exit, /q - Leave chat\r\n");
  H->io->printf(s, "  /who             - List users\r\n");
  H->io->printf(s, "  /help            - Show this help\r\n");
  H->io->printf(s, "  <message>        - Send message to all\r\n");
  H->io->printf(s, "\r\n");
  H->io->printf(s, "\x1b[1;32m--- Chat started ---\x1b[0m\r\n");
  H->io->printf(s, "\r\n");
  
  /* Announce join */
  char join_msg[128];
  snprintf(join_msg, sizeof(join_msg), "*** %s has joined the chat ***", ci->username);
  chat_post("SYSTEM", join_msg);
  
  H->log(BBS_LOG_INFO, "chat", "user joined chat room");
  
  return BBS_OK;
}

static bbs_rc_t chat_run(void* inst, bbs_session_t* s) {
  chat_inst_t* ci = (chat_inst_t*)inst;
  char line[256];
  chat_message_t new_msgs[10];
  
  while (ci->running) {
    /* Check for new messages */
    int new_last;
    int msg_count = chat_get_new(ci->last_seen, new_msgs, 10, &new_last);
    
    /* Display new messages */
    for (int i = 0; i < msg_count; i++) {
      struct tm* tm = localtime(&new_msgs[i].timestamp);
      char timebuf[16];
      strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm);
      
      if (strcmp(new_msgs[i].sender, "SYSTEM") == 0) {
        H->io->printf(s, "\x1b[1;33m%s\x1b[0m\r\n", new_msgs[i].text);
      } else {
        H->io->printf(s, "\x1b[90m[%s]\x1b[0m \x1b[1;36m%s\x1b[0m: %s\r\n",
                      timebuf, new_msgs[i].sender, new_msgs[i].text);
      }
    }
    ci->last_seen = new_last;
    
    /* Prompt for input */
    H->io->printf(s, "\x1b[1;32m>\x1b[0m ");
    
    bbs_rc_t rc = H->io->readline(s, line, sizeof(line), 1);
    if (rc == BBS_EIO) {
      return BBS_EIO;  /* Connection lost */
    }
    if (rc == BBS_ETIMEOUT) {
      continue;  /* Just check for new messages */
    }
    
    /* Skip empty lines */
    if (!line[0]) continue;
    
    /* Handle commands */
    if (line[0] == '/') {
      if (strcmp(line, "/quit") == 0 || strcmp(line, "/exit") == 0 || 
          strcmp(line, "/q") == 0) {
        break;
      }
      
      if (strcmp(line, "/help") == 0 || strcmp(line, "/?") == 0) {
        H->io->printf(s, "\r\n");
        H->io->printf(s, "Commands:\r\n");
        H->io->printf(s, "  /quit, /exit, /q - Leave chat\r\n");
        H->io->printf(s, "  /who             - List users\r\n");
        H->io->printf(s, "  /help            - Show this help\r\n");
        H->io->printf(s, "  <message>        - Send message\r\n");
        H->io->printf(s, "\r\n");
        continue;
      }
      
      if (strcmp(line, "/who") == 0) {
        char users[CHAT_MAX_USERS][64];
        int user_count = chat_list_users(users, CHAT_MAX_USERS);
        H->io->printf(s, "\r\n");
        H->io->printf(s, "Users in chat (%d):\r\n", user_count);
        for (int i = 0; i < user_count; i++) {
          H->io->printf(s, "  %s\r\n", users[i]);
        }
        H->io->printf(s, "\r\n");
        continue;
      }
      
      H->io->printf(s, "Unknown command: %s\r\n", line);
      continue;
    }
    
    /* Post message */
    chat_post(ci->username, line);
  }
  
  return BBS_OK;
}

static void chat_exit(void* inst, bbs_session_t* s) {
  chat_inst_t* ci = (chat_inst_t*)inst;
  
  /* Announce leave */
  char leave_msg[128];
  snprintf(leave_msg, sizeof(leave_msg), "*** %s has left the chat ***", ci->username);
  chat_post("SYSTEM", leave_msg);
  if (ci->active) {
    chat_user_leave(ci->username);
    ci->active = 0;
  }
  
  H->io->printf(s, "\r\n");
  H->io->printf(s, "\x1b[1;32m--- Chat ended ---\x1b[0m\r\n");
  H->io->printf(s, "\x1b[1;35mGoodbye from Chat Plugin!\x1b[0m\r\n");
  H->io->printf(s, "\r\n");
  
  H->log(BBS_LOG_INFO, "chat", "user left chat room");
}

static void chat_on_event(void* inst, const bbs_event_t* ev) {
  (void)inst;
  (void)ev;
}

static void chat_destroy(void* inst) {
  free(inst);
}

/* Static vtable */
static const bbs_plugin_instance_vtbl_t CHAT_VTBL = {
  .on_enter = chat_enter,
  .run = chat_run,
  .on_exit = chat_exit,
  .on_event = chat_on_event,
  .destroy = chat_destroy,
  .reserved = {NULL}
};

/* Plugin global functions */

static bbs_rc_t chat_init(const bbs_host_api_t* host) {
  H = host;
  
  /* Initialize global chat state */
  pthread_mutex_lock(&g_chat.mu);
  if (!g_chat.initialized) {
    memset(g_chat.messages, 0, sizeof(g_chat.messages));
    memset(g_chat.users, 0, sizeof(g_chat.users));
    memset(g_chat.user_active, 0, sizeof(g_chat.user_active));
    g_chat.head = 0;
    g_chat.count = 0;
    g_chat.initialized = 1;
  }
  pthread_mutex_unlock(&g_chat.mu);
  
  host->log(BBS_LOG_INFO, "chat", "plugin initialized");
  return BBS_OK;
}

static void chat_shutdown(void) {
  if (H) {
    H->log(BBS_LOG_INFO, "chat", "plugin shutting down");
  }
  
  pthread_mutex_lock(&g_chat.mu);
  g_chat.initialized = 0;
  memset(g_chat.users, 0, sizeof(g_chat.users));
  memset(g_chat.user_active, 0, sizeof(g_chat.user_active));
  pthread_mutex_unlock(&g_chat.mu);
}

static bbs_rc_t chat_create_instance(bbs_session_t* s, void** out_inst,
                                      const bbs_plugin_instance_vtbl_t** out_vtbl) {
  (void)s;
  
  chat_inst_t* inst = calloc(1, sizeof(chat_inst_t));
  if (!inst) return BBS_EINTERNAL;
  
  *out_inst = inst;
  *out_vtbl = &CHAT_VTBL;
  return BBS_OK;
}

/* Required exported function */
bbs_rc_t bbs_plugin_query(uint32_t host_abi_version,
                          const bbs_host_api_t* host,
                          bbs_plugin_desc_t* out) {
  if (!out || !host) return BBS_EINVAL;
  
  /* Check ABI version compatibility */
  uint32_t host_major = (host_abi_version >> 16) & 0xFFFF;
  uint32_t our_major = (BBS_PLUGIN_ABI_VERSION >> 16) & 0xFFFF;
  if (host_major != our_major) return BBS_EUNSUPPORTED;
  
  /* Zero everything so reserved pointers are NULL */
  memset(out, 0, sizeof(*out));
  
  out->abi_version = BBS_PLUGIN_ABI_VERSION;
  out->size = sizeof(*out);
  out->magic = BBS_PLUGIN_MAGIC;
  
  out->id = "com.mutineer.chat";
  out->name = "Multiuser Chat Plugin";
  out->version = "1.0.0";
  out->author = "Mutineer BBS";
  out->description = "Real-time multiuser chat room demonstrating plugin concurrency.";
  out->caps = BBS_CAP_INTERACTIVE | BBS_CAP_BACKGROUND;
  
  out->init = chat_init;
  out->shutdown = chat_shutdown;
  out->create_instance = chat_create_instance;
  out->commands = NULL;
  out->on_event = NULL;
  
  return BBS_OK;
}
