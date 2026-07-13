#include "bbs_session.h"
#include "bbs_db.h"
#include "bbs_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>

extern void send_str(Session* s, const char* str);
extern int session_readline(Session* s, uint8_t* buf, size_t cap, int timeout);
extern int prompt_line(Session* s, const char* prompt, char* out, size_t cap);

static void chat_log_file(Session* s, const char* other_handle, const char* speaker, const char* msg) {
  if (!s || !s->cfg.chat_log_path[0]) return;

  char dir[512];
  snprintf(dir, sizeof(dir), "%s", s->cfg.chat_log_path);
  mkdir(dir, 0755);

  char path[640];
  snprintf(path, sizeof(path), "%s/%s_%s.log",
           dir, s->user.handle, other_handle ? other_handle : "sysop");

  FILE* f = fopen(path, "a");
  if (!f) return;

  time_t now = time(NULL);
  struct tm* tm = localtime(&now);
  char ts[24];
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
  fprintf(f, "[%s] %s: %s\n", ts, speaker, msg);
  fclose(f);
}

#define SPLIT_TOP_START    1
#define SPLIT_TOP_END      11
#define SPLIT_DIVIDER      12
#define SPLIT_BOTTOM_START 13
#define SPLIT_BOTTOM_END   23
#define SPLIT_INPUT_LINE   24

static void ansi_goto(Session* s, int row, int col) {
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
  send_str(s, buf);
}

static void ansi_clear_line(Session* s) {
  send_str(s, "\x1b[2K");
}

static void ansi_clear_screen(Session* s) {
  send_str(s, "\x1b[2J\x1b[H");
}

static void ansi_scroll_region(Session* s, int top, int bottom) {
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dr", top, bottom);
  send_str(s, buf);
}

static void draw_split_divider(Session* s) {
  ansi_goto(s, SPLIT_DIVIDER, 1);
  send_str(s, "\x1b[1;33m");
  for (int i = 0; i < 79; i++) send_str(s, "-");
  send_str(s, "\x1b[0m");
}

static void draw_input_prompt(Session* s) {
  ansi_goto(s, SPLIT_INPUT_LINE, 1);
  ansi_clear_line(s);
  send_str(s, "\x1b[1;32m> \x1b[0m");
}

/* Deliver a line to a session's chat inbox (called by the sending side). */
static void chat_inbox_post(Session* dest, const char* line) {
  if (!dest || !line) return;
  pthread_mutex_lock(&dest->chat_inbox_lock);
  int next = (dest->chat_inbox_tail + 1) % 8;
  if (next != dest->chat_inbox_head) { /* not full */
    strncpy(dest->chat_inbox[dest->chat_inbox_tail], line, 255);
    dest->chat_inbox[dest->chat_inbox_tail][255] = '\0';
    dest->chat_inbox_tail = next;
  }
  pthread_mutex_unlock(&dest->chat_inbox_lock);
}

/* Append a line to a scroll-region panel.
   Temporarily sets the scroll region, scrolls in a new line at the bottom,
   then restores the full-screen scroll region and repositions the cursor. */
static void panel_append(Session* s, int top_row, int bot_row,
                         int input_row, const char* line) {
  char buf[512];
  /* Set scroll region to this panel */
  snprintf(buf, sizeof(buf), "\x1b[%d;%dr", top_row, bot_row);
  send_str(s, buf);
  /* Go to bottom of panel, scroll in a new line */
  ansi_goto(s, bot_row, 1);
  send_str(s, "\r\n");               /* triggers scroll within region */
  ansi_goto(s, bot_row, 1);
  ansi_clear_line(s);
  send_str(s, line);
  /* Restore full-screen scroll region */
  snprintf(buf, sizeof(buf), "\x1b[1;%dr", 24);
  send_str(s, buf);
  /* Reposition cursor to input line */
  ansi_goto(s, input_row, 1);
}

void split_chat_start(Session* s, Session* other) {
  if (!s || !other) return;

  /* ── Draw frame ────────────────────────────────────────────── */
  ansi_clear_screen(s);
  ansi_scroll_region(s, 1, 24);

  /* Header row */
  ansi_goto(s, 1, 1);
  ansi_clear_line(s);
  char hdr[128];
  snprintf(hdr, sizeof(hdr),
           "\x1b[1;32m Mutineer Chat \x1b[0m\x1b[1;36m %s \x1b[0m<> \x1b[1;32m%s\x1b[0m",
           other->user.handle, s->user.handle);
  send_str(s, hdr);

  /* Remote panel label */
  ansi_goto(s, 2, 1);
  ansi_clear_line(s);
  char lbl[64];
  snprintf(lbl, sizeof(lbl), "\x1b[0;36m %s >\x1b[0m", other->user.handle);
  send_str(s, lbl);

  /* Divider */
  draw_split_divider(s);  /* row SPLIT_DIVIDER = 12 */

  /* Local panel label */
  ansi_goto(s, SPLIT_BOTTOM_START, 1);
  ansi_clear_line(s);
  snprintf(lbl, sizeof(lbl), "\x1b[0;32m %s >\x1b[0m", s->user.handle);
  send_str(s, lbl);

  /* Clear remote and local message areas */
  for (int row = 3; row <= SPLIT_DIVIDER - 1; row++) {
    ansi_goto(s, row, 1); ansi_clear_line(s);
  }
  for (int row = SPLIT_BOTTOM_START + 1; row <= SPLIT_INPUT_LINE - 1; row++) {
    ansi_goto(s, row, 1); ansi_clear_line(s);
  }

  /* Input line */
  draw_input_prompt(s);

  db_chat_log(s->db, "split", 0, s->user.id, s->user.handle,
              other->user.id, other->user.handle, "*** Chat started ***");
  chat_log_file(s, other->user.handle, "***", "Chat started");

  /* ── Main loop ─────────────────────────────────────────────── */
  char local_input[256] = {0};
  int  local_pos = 0;

  while (s->alive && other->alive) {
    /* Check our inbox for messages from the other side */
    pthread_mutex_lock(&s->chat_inbox_lock);
    while (s->chat_inbox_head != s->chat_inbox_tail) {
      char line[256];
      strncpy(line, s->chat_inbox[s->chat_inbox_head], 255);
      line[255] = '\0';
      s->chat_inbox_head = (s->chat_inbox_head + 1) % 8;
      pthread_mutex_unlock(&s->chat_inbox_lock);

      /* Display in top (remote) panel: rows 3–11 */
      panel_append(s, 3, SPLIT_DIVIDER - 1, SPLIT_INPUT_LINE, line);
      /* Redraw current input */
      draw_input_prompt(s);
      send_str(s, local_input);

      pthread_mutex_lock(&s->chat_inbox_lock);
    }
    pthread_mutex_unlock(&s->chat_inbox_lock);

    /* Poll local socket with short timeout */
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(s->fd, &rfds);
    struct timeval tv = {.tv_sec = 0, .tv_usec = 50000}; /* 50ms */
    int r = select(s->fd + 1, &rfds, NULL, NULL, &tv);

    if (r > 0 && FD_ISSET(s->fd, &rfds)) {
      char c;
      ssize_t n = recv(s->fd, &c, 1, 0);
      if (n <= 0) break;

      if (c == '\r' || c == '\n') {
        if (local_pos > 0) {
          local_input[local_pos] = '\0';

          if (strcmp(local_input, "/quit") == 0 || strcmp(local_input, "/q") == 0)
            break;

          db_chat_log(s->db, "split", 0, s->user.id, s->user.handle,
                      other->user.id, other->user.handle, local_input);
          chat_log_file(s, other->user.handle, s->user.handle, local_input);

          /* Show in local (bottom) panel: rows SPLIT_BOTTOM_START+1 – SPLIT_INPUT_LINE-1 */
          char local_line[280];
          snprintf(local_line, sizeof(local_line),
                   "\x1b[1;32m%s:\x1b[0m %s", s->user.handle, local_input);
          panel_append(s, SPLIT_BOTTOM_START + 1, SPLIT_INPUT_LINE - 1,
                       SPLIT_INPUT_LINE, local_line);

          /* Deliver to other's inbox */
          char remote_line[280];
          snprintf(remote_line, sizeof(remote_line),
                   "\x1b[1;36m%s:\x1b[0m %s", s->user.handle, local_input);
          chat_inbox_post(other, remote_line);

          local_pos = 0;
          local_input[0] = '\0';
          draw_input_prompt(s);
        }
      } else if (c == 127 || c == 8) {
        if (local_pos > 0) {
          local_pos--;
          local_input[local_pos] = '\0';
          send_str(s, "\b \b");
        }
      } else if (c >= 32 && c < 127 && local_pos < 250) {
        local_input[local_pos++] = c;
        local_input[local_pos] = '\0';
        char echo[2] = {c, 0};
        send_str(s, echo);
      } else if (c == 27) {
        char seq[8];
        recv(s->fd, seq, 2, MSG_DONTWAIT);
      }
    }
  }

  db_chat_log(s->db, "split", 0, s->user.id, s->user.handle,
              other->user.id, other->user.handle, "*** Chat ended ***");
  chat_log_file(s, other->user.handle, "***", "Chat ended");

  /* Restore terminal */
  ansi_scroll_region(s, 1, 24);
  ansi_clear_screen(s);
  send_str(s, "\x1b[0m");
  send_str(s, "Chat ended.\r\n");
}

#define MAX_TELECONF_ROOMS 10
#define MAX_USERS_PER_ROOM 20

typedef struct {
  int active;
  char name[32];
  char topic[128];
  int moderator_node;
  int users[MAX_USERS_PER_ROOM];
  int user_count;
  int private;
  char password[32];
} TeleconfRoom;

static TeleconfRoom teleconf_rooms[MAX_TELECONF_ROOMS];
static pthread_mutex_t teleconf_mutex = PTHREAD_MUTEX_INITIALIZER;

void teleconf_init(void) {
  pthread_mutex_lock(&teleconf_mutex);
  memset(teleconf_rooms, 0, sizeof(teleconf_rooms));
  pthread_mutex_unlock(&teleconf_mutex);
}

int teleconf_create_room(Session* s, const char* name, const char* topic, int private_room, const char* password) {
  pthread_mutex_lock(&teleconf_mutex);
  
  for (int i = 0; i < MAX_TELECONF_ROOMS; i++) {
    if (!teleconf_rooms[i].active) {
      teleconf_rooms[i].active = 1;
      strncpy(teleconf_rooms[i].name, name, sizeof(teleconf_rooms[i].name) - 1);
      strncpy(teleconf_rooms[i].topic, topic, sizeof(teleconf_rooms[i].topic) - 1);
      teleconf_rooms[i].moderator_node = s->node_num;
      teleconf_rooms[i].users[0] = s->node_num;
      teleconf_rooms[i].user_count = 1;
      teleconf_rooms[i].private = private_room;
      if (password) {
        strncpy(teleconf_rooms[i].password, password, sizeof(teleconf_rooms[i].password) - 1);
      }
      pthread_mutex_unlock(&teleconf_mutex);
      return i;
    }
  }
  
  pthread_mutex_unlock(&teleconf_mutex);
  return -1;
}

int teleconf_join_room(Session* s, int room_id, const char* password) {
  if (room_id < 0 || room_id >= MAX_TELECONF_ROOMS) return -1;
  
  pthread_mutex_lock(&teleconf_mutex);
  
  TeleconfRoom* room = &teleconf_rooms[room_id];
  if (!room->active) {
    pthread_mutex_unlock(&teleconf_mutex);
    return -1;
  }
  
  if (room->private && room->password[0]) {
    if (!password || strcmp(password, room->password) != 0) {
      pthread_mutex_unlock(&teleconf_mutex);
      return -2;
    }
  }
  
  if (room->user_count >= MAX_USERS_PER_ROOM) {
    pthread_mutex_unlock(&teleconf_mutex);
    return -3;
  }
  
  for (int i = 0; i < room->user_count; i++) {
    if (room->users[i] == s->node_num) {
      pthread_mutex_unlock(&teleconf_mutex);
      return 0;
    }
  }
  
  room->users[room->user_count++] = s->node_num;
  pthread_mutex_unlock(&teleconf_mutex);
  return 0;
}

void teleconf_leave_room(Session* s, int room_id) {
  if (room_id < 0 || room_id >= MAX_TELECONF_ROOMS) return;
  
  pthread_mutex_lock(&teleconf_mutex);
  
  TeleconfRoom* room = &teleconf_rooms[room_id];
  if (!room->active) {
    pthread_mutex_unlock(&teleconf_mutex);
    return;
  }
  
  for (int i = 0; i < room->user_count; i++) {
    if (room->users[i] == s->node_num) {
      for (int j = i; j < room->user_count - 1; j++) {
        room->users[j] = room->users[j + 1];
      }
      room->user_count--;
      break;
    }
  }
  
  if (room->user_count == 0) {
    room->active = 0;
  } else if (room->moderator_node == s->node_num) {
    room->moderator_node = room->users[0];
  }
  
  pthread_mutex_unlock(&teleconf_mutex);
}

static BbsDb* g_chat_db = NULL;

void teleconf_set_db(BbsDb* db) {
  g_chat_db = db;
}

void teleconf_broadcast(int room_id, const char* from, const char* msg) {
  if (room_id < 0 || room_id >= MAX_TELECONF_ROOMS) return;
  
  pthread_mutex_lock(&teleconf_mutex);
  
  TeleconfRoom* room = &teleconf_rooms[room_id];
  if (!room->active) {
    pthread_mutex_unlock(&teleconf_mutex);
    return;
  }
  
  if (g_chat_db) {
    db_chat_log(g_chat_db, "teleconf", room_id, 0, from, 0, NULL, msg);
  }
  
  char formatted[512];
  snprintf(formatted, sizeof(formatted), "\r\n\x1b[1;33m<%s>\x1b[0m %s\r\n", from, msg);
  
  for (int i = 0; i < room->user_count; i++) {
    Session* target = online_get_node(room->users[i]);
    if (target) {
      fd_write_all(target->fd, formatted, strlen(formatted));
    }
  }
  
  pthread_mutex_unlock(&teleconf_mutex);
}

void teleconf_list_rooms(Session* s) {
  pthread_mutex_lock(&teleconf_mutex);
  
  send_str(s, "\r\n\x1b[1;36mTeleconference Rooms:\x1b[0m\r\n");
  send_str(s, "\x1b[1;33m  #   Name                 Topic                          Users\x1b[0m\r\n");
  send_str(s, "----------------------------------------------------------------------\r\n");
  
  char buf[256];
  int found = 0;
  for (int i = 0; i < MAX_TELECONF_ROOMS; i++) {
    if (teleconf_rooms[i].active && !teleconf_rooms[i].private) {
      snprintf(buf, sizeof(buf), " %2d   %-20s %-30s %d\r\n",
               i, teleconf_rooms[i].name, teleconf_rooms[i].topic,
               teleconf_rooms[i].user_count);
      send_str(s, buf);
      found++;
    }
  }
  
  if (!found) {
    send_str(s, "  No active public rooms.\r\n");
  }
  
  pthread_mutex_unlock(&teleconf_mutex);
}

void teleconf_who_in_room(Session* s, int room_id) {
  if (room_id < 0 || room_id >= MAX_TELECONF_ROOMS) return;
  
  pthread_mutex_lock(&teleconf_mutex);
  
  TeleconfRoom* room = &teleconf_rooms[room_id];
  if (!room->active) {
    pthread_mutex_unlock(&teleconf_mutex);
    send_str(s, "\r\nRoom not found.\r\n");
    return;
  }
  
  char buf[256];
  snprintf(buf, sizeof(buf), "\r\n\x1b[1;36mUsers in %s:\x1b[0m\r\n", room->name);
  send_str(s, buf);
  
  for (int i = 0; i < room->user_count; i++) {
    Session* u = online_get_node(room->users[i]);
    if (u) {
      const char* mod = (room->users[i] == room->moderator_node) ? " (mod)" : "";
      snprintf(buf, sizeof(buf), "  Node %d: %s%s\r\n", room->users[i], u->user.handle, mod);
      send_str(s, buf);
    }
  }
  
  pthread_mutex_unlock(&teleconf_mutex);
}

void cmd_teleconf(Session* s, const char* data) {
  (void)data;
  
  send_str(s, "\r\n\x1b[1;36mTeleconference Commands:\x1b[0m\r\n");
  send_str(s, "  /list     - List public rooms\r\n");
  send_str(s, "  /join #   - Join room number #\r\n");
  send_str(s, "  /create   - Create a new room\r\n");
  send_str(s, "  /who      - Who's in current room\r\n");
  send_str(s, "  /topic    - Set room topic (moderator)\r\n");
  send_str(s, "  /kick #   - Kick node # (moderator)\r\n");
  send_str(s, "  /quit     - Leave teleconference\r\n");
  send_str(s, "\r\n");
  
  teleconf_list_rooms(s);
  
  int current_room = -1;
  char input[256];
  
  while (s->alive) {
    if (current_room >= 0) {
      send_str(s, "\r\n[Teleconf] > ");
    } else {
      send_str(s, "\r\n[Lobby] > ");
    }
    
    uint8_t line[256];
    int n = session_readline(s, line, sizeof(line), 60);
    if (n <= 0) break;
    
    strncpy(input, (char*)line, sizeof(input) - 1);
    input[sizeof(input) - 1] = '\0';
    
    if (input[0] == '/') {
      char* cmd = input + 1;
      char* arg = strchr(cmd, ' ');
      if (arg) {
        *arg = '\0';
        arg++;
        while (*arg == ' ') arg++;
      }
      
      if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) {
        if (current_room >= 0) {
          teleconf_leave_room(s, current_room);
        }
        break;
      } else if (strcmp(cmd, "list") == 0) {
        teleconf_list_rooms(s);
      } else if (strcmp(cmd, "join") == 0) {
        if (!arg || !arg[0]) {
          send_str(s, "Usage: /join <room#>\r\n");
          continue;
        }
        int room_id = atoi(arg);
        int result = teleconf_join_room(s, room_id, NULL);
        if (result == 0) {
          current_room = room_id;
          char buf[128];
          snprintf(buf, sizeof(buf), "Joined room %d.\r\n", room_id);
          send_str(s, buf);
          teleconf_broadcast(room_id, "***", s->user.handle);
        } else if (result == -2) {
          send_str(s, "Password required.\r\n");
        } else if (result == -3) {
          send_str(s, "Room is full.\r\n");
        } else {
          send_str(s, "Room not found.\r\n");
        }
      } else if (strcmp(cmd, "create") == 0) {
        char name[32], topic[128];
        prompt_line(s, "Room name: ", name, sizeof(name));
        if (!name[0]) continue;
        prompt_line(s, "Topic: ", topic, sizeof(topic));
        
        int room_id = teleconf_create_room(s, name, topic, 0, NULL);
        if (room_id >= 0) {
          current_room = room_id;
          char buf[128];
          snprintf(buf, sizeof(buf), "Created room %d: %s\r\n", room_id, name);
          send_str(s, buf);
        } else {
          send_str(s, "Could not create room (max rooms reached).\r\n");
        }
      } else if (strcmp(cmd, "who") == 0) {
        if (current_room >= 0) {
          teleconf_who_in_room(s, current_room);
        } else {
          send_str(s, "Not in a room.\r\n");
        }
      } else if (strcmp(cmd, "leave") == 0) {
        if (current_room >= 0) {
          teleconf_leave_room(s, current_room);
          send_str(s, "Left room.\r\n");
          current_room = -1;
        }
      } else {
        send_str(s, "Unknown command.\r\n");
      }
    } else if (input[0] && current_room >= 0) {
      teleconf_broadcast(current_room, s->user.handle, input);
    }
  }
  
  send_str(s, "\r\nLeft teleconference.\r\n");
}
