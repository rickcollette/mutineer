#include "bbs_session.h"
#include "bbs_util.h"
#include "bbs_ansi.h"
#include "bbs_hash.h"
#include "bbs_acs.h"
#include "bbs_mci.h"
#include "bbs_msg_cmds.h"
#include "bbs_log.h"
#include "bbs_doors.h"
#include "bbs_flags.h"
#include "bbs_scheduler.h"
#include "bbs_msg_defs.h"
#include "bbs_menu_template.h"
#include "bbs_plugin_api.h"
#include "bbs_plugin_loader.h"
#include "bbs_plugin_registry.h"
#include "bbs_file_cmds.h"
#include "bbs_msg_cmds.h"
#include "bbs_chat.h"
#include <openssl/evp.h>
#include <signal.h>
#include <ctype.h>

extern volatile sig_atomic_t g_stop;
#include <signal.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <dirent.h>

static bool valid_menu_basename(const char *name)
{
  if (!name || !name[0])
    return false;
  for (const char *p = name; *p; p++)
  {
    unsigned char c = (unsigned char)*p;
    if (!(isalnum(c) || c == '_' || c == '-'))
      return false;
  }
  return true;
}

/* Simple online registry */
#define MAX_ONLINE 256
static Session *g_online[MAX_ONLINE];
static pthread_mutex_t g_online_mu = PTHREAD_MUTEX_INITIALIZER;

typedef struct
{
  time_t ts;
  int channel;
  char handle[64];
  char text[256];
} ChatLine;

#define CHAT_MAX 128
static ChatLine g_chat[CHAT_MAX];
static int g_chat_head = 0;
static pthread_mutex_t g_chat_mu = PTHREAD_MUTEX_INITIALIZER;

static void chat_post(int channel, const char *handle, const char *text)
{
  if (!handle)
    handle = "";
  if (!text)
    text = "";
  pthread_mutex_lock(&g_chat_mu);
  g_chat_head = (g_chat_head + 1) % CHAT_MAX;
  g_chat[g_chat_head].ts = time(NULL);
  g_chat[g_chat_head].channel = channel;
  snprintf(g_chat[g_chat_head].handle, sizeof(g_chat[g_chat_head].handle), "%s", handle);
  snprintf(g_chat[g_chat_head].text, sizeof(g_chat[g_chat_head].text), "%s", text);
  pthread_mutex_unlock(&g_chat_mu);
}

static int chat_dump(int channel, time_t since, char *out, size_t cap)
{
  if (!out || cap == 0)
    return 0;
  size_t o = 0;
  pthread_mutex_lock(&g_chat_mu);
  for (int i = 0; i < CHAT_MAX; i++)
  {
    int idx = (g_chat_head - i + CHAT_MAX) % CHAT_MAX;
    if (g_chat[idx].ts == 0)
      break;
    if (g_chat[idx].channel != channel)
      continue;
    if (g_chat[idx].ts <= since)
      break;
    o += (size_t)snprintf(out + o, cap - o, "[%s] %s\r\n", g_chat[idx].handle, g_chat[idx].text);
    if (o >= cap)
      break;
  }
  pthread_mutex_unlock(&g_chat_mu);
  return (int)o;
}
typedef struct
{
  char ip[64];
  char handle[64];
  int attempts;
  time_t window_start;
} LoginAttempt;

#define MAX_ATTEMPTS 256
static LoginAttempt g_attempts[MAX_ATTEMPTS];
static pthread_mutex_t g_attempts_mu = PTHREAD_MUTEX_INITIALIZER;

static bool login_throttled(const BbsConfig *cfg, const char *ip, const char *handle)
{
  time_t now = time(NULL);
  pthread_mutex_lock(&g_attempts_mu);
  for (int i = 0; i < MAX_ATTEMPTS; i++)
  {
    LoginAttempt *a = &g_attempts[i];
    if (a->attempts == 0)
      continue;
    if (difftime(now, a->window_start) > cfg->login_window_sec)
    {
      a->attempts = 0;
      continue;
    }
    if (!strcmp(a->ip, ip) || (handle && handle[0] && !strcmp(a->handle, handle)))
    {
      if (a->attempts >= cfg->login_max_attempts)
      {
        pthread_mutex_unlock(&g_attempts_mu);
        return true;
      }
    }
  }
  pthread_mutex_unlock(&g_attempts_mu);
  return false;
}

static void login_record(const BbsConfig *cfg, const char *ip, const char *handle, bool success)
{
  if (success)
    return;
  time_t now = time(NULL);
  pthread_mutex_lock(&g_attempts_mu);
  int slot = -1;
  for (int i = 0; i < MAX_ATTEMPTS; i++)
  {
    if (g_attempts[i].attempts == 0)
    {
      if (slot < 0)
        slot = i;
      continue;
    }
    if (!strcmp(g_attempts[i].ip, ip) || (handle && handle[0] && !strcmp(g_attempts[i].handle, handle)))
    {
      if (difftime(now, g_attempts[i].window_start) > cfg->login_window_sec)
      {
        g_attempts[i].attempts = 0;
        slot = i;
      }
      else
      {
        g_attempts[i].attempts++;
        pthread_mutex_unlock(&g_attempts_mu);
        return;
      }
    }
  }
  if (slot >= 0)
  {
    LoginAttempt *a = &g_attempts[slot];
    snprintf(a->ip, sizeof(a->ip), "%s", ip);
    snprintf(a->handle, sizeof(a->handle), "%s", handle ? handle : "");
    a->attempts = 1;
    a->window_start = now;
  }
  pthread_mutex_unlock(&g_attempts_mu);
}

void online_add(Session *s)
{
  pthread_mutex_lock(&g_online_mu);
  for (int i = 0; i < MAX_ONLINE; i++)
  {
    if (!g_online[i])
    {
      g_online[i] = s;
      s->node_num = i + 1;
      break;
    }
  }
  pthread_mutex_unlock(&g_online_mu);
}

void online_remove(Session *s)
{
  pthread_mutex_lock(&g_online_mu);
  for (int i = 0; i < MAX_ONLINE; i++)
  {
    if (g_online[i] == s)
    {
      g_online[i] = NULL;
      break;
    }
  }
  pthread_mutex_unlock(&g_online_mu);
}

size_t online_list(char *out, size_t cap)
{
  size_t o = 0;
  pthread_mutex_lock(&g_online_mu);
  o += (size_t)snprintf(out + o, cap - o, "\r\nOnline users (nodes):\r\n");
  int n = 0;
  for (int i = 0; i < MAX_ONLINE; i++)
  {
    if (g_online[i])
    {
      n++;
      o += (size_t)snprintf(out + o, cap - o, "  Node %d  IP %s\r\n", i + 1, g_online[i]->ip);
      if (o >= cap)
        break;
    }
  }
  if (n == 0)
  {
    o += (size_t)snprintf(out + o, cap - o, "  (none)\r\n");
  }
  pthread_mutex_unlock(&g_online_mu);
  return o;
}

void online_broadcast(const char *msg)
{
  pthread_mutex_lock(&g_online_mu);
  for (int i = 0; i < MAX_ONLINE; i++)
  {
    if (g_online[i])
    {
      fd_write_all(g_online[i]->fd, msg, strlen(msg));
    }
  }
  pthread_mutex_unlock(&g_online_mu);
}

void broadcast_check(const char *data_path)
{
  char path[512];
  snprintf(path, sizeof(path), "%s/broadcast.txt", data_path);

  FILE *f = fopen(path, "r");
  if (!f)
    return;

  char line[512];
  while (fgets(line, sizeof(line), f))
  {
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n')
      line[len - 1] = '\0';
    if (line[0])
    {
      char msg[600];
      snprintf(msg, sizeof(msg), "\r\n\a%s\r\n", line);
      online_broadcast(msg);
    }
  }
  fclose(f);

  /* Clear the file after processing */
  f = fopen(path, "w");
  if (f)
    fclose(f);
}

Session *online_get_node(int node_num)
{
  Session *out = NULL;
  pthread_mutex_lock(&g_online_mu);
  if (node_num > 0 && node_num <= MAX_ONLINE)
    out = g_online[node_num - 1];
  pthread_mutex_unlock(&g_online_mu);
  return out;
}

/* Read raw bytes, strip telnet, return clean bytes length.
   Returns 0 on disconnect, -1 on error. */
static int recv_clean(Session *s, uint8_t *out, size_t out_cap)
{
  uint8_t inbuf[512];
  ssize_t r = recv(s->fd, inbuf, sizeof(inbuf), 0);
  if (r == 0)
    return 0;
  if (r < 0)
  {
    if (errno == EINTR)
      return -1;
    return -1;
  }
  size_t n = telnet_feed(&s->tn, s->fd, inbuf, (size_t)r, out, out_cap);
  return (int)n;
}

/* Echo modes for readline */
#define ECHO_OFF 0  /* no echo */
#define ECHO_ON 1   /* normal echo */
#define ECHO_DOTS 2 /* echo dots for password */

/* Line reader with echo support */
static int readline_echo(Session *s, uint8_t *buf, size_t cap, int timeout_sec, int echo_mode)
{
  size_t n = 0;
  while (n + 1 < cap)
  {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(s->fd, &rfds);
    struct timeval tv = {.tv_sec = timeout_sec, .tv_usec = 0};
    int r = select(s->fd + 1, &rfds, NULL, NULL, timeout_sec > 0 ? &tv : NULL);
    if (r == 0)
      return -2; /* idle timeout */
    if (r < 0)
    {
      if (errno == EINTR)
        continue;
      return -1;
    }

    uint8_t clean[256];
    int got = recv_clean(s, clean, sizeof(clean));
    if (got == 0)
      return 0;
    if (got < 0)
      continue;

    for (int i = 0; i < got; i++)
    {
      uint8_t ch = clean[i];

      if (ch == '\n' || ch == '\r')
      {
        buf[n] = 0;
        return (int)n;
      }

      if (ch == 0x08 || ch == 0x7F)
      {
        /* backspace */
        if (n > 0)
        {
          n--;
          if (echo_mode != ECHO_OFF)
          {
            send_str(s, "\x08 \x08"); /* backspace, space, backspace */
          }
        }
        continue;
      }

      /* ignore other control chars */
      if (ch < 0x20 && ch != 0x1B)
        continue;

      buf[n++] = ch;

      /* echo the character */
      if (echo_mode == ECHO_ON)
      {
        char echo[2] = {(char)ch, 0};
        send_str(s, echo);
      }
      else if (echo_mode == ECHO_DOTS)
      {
        send_str(s, ".");
      }
    }
  }
  buf[n] = 0;
  return (int)n;
}

/* Very small line reader built on recv_clean (handles telnet sequences). */
int session_readline(Session *s, uint8_t *buf, size_t cap, int timeout_sec)
{
  return readline_echo(s, buf, cap, timeout_sec, ECHO_ON);
}

void send_str(Session *s, const char *str)
{
  if (!s || !str)
    return;
  fd_write_all(s->fd, str, strlen(str));
}

static void send_motd(Session *s)
{
  if (!s)
    return;
  char path_try[256];
  snprintf(path_try, sizeof(path_try), "%s", s->cfg.motd);

  char alt[512];
  path_join(s->cfg.art_path, s->cfg.motd, alt, sizeof(alt));

  const char *paths[2] = {path_try, alt};
  for (int i = 0; i < 2; i++)
  {
    size_t len = 0;
    char *raw = file_read_all(paths[i], &len);
    if (raw)
    {
      size_t out_cap = len * 2 + 1;
      char *expanded = (char *)malloc(out_cap);
      if (expanded)
      {
        mci_expand(s, raw, expanded, out_cap);
        fd_write_all(s->fd, expanded, strlen(expanded));
        free(expanded);
      }
      else
      {
        fd_write_all(s->fd, raw, len);
      }
      free(raw);
      return;
    }
  }
  send_str(s, "\r\n(MOTD missing)\r\n");
}

static void send_art(Session *s, const char *path)
{
  if (!s || !path)
    return;
  size_t len = 0;
  char *raw = file_read_all(path, &len);
  if (!raw)
    return;
  size_t cap = len * 2 + 1;
  char *out = (char *)malloc(cap);
  if (out)
  {
    mci_expand(s, raw, out, cap);
    fd_write_all(s->fd, out, strlen(out));
    free(out);
  }
  else
  {
    fd_write_all(s->fd, raw, len);
  }
  free(raw);
}

/* Try art_path/name.ans (if ANSI) then art_path/name.asc; return true if shown. */
static bool send_named_art(Session *s, const char *name)
{
  if (!s || !name) return false;
  char path[512];
  if (s->ansi) {
    snprintf(path, sizeof(path), "%s/%s.ans", s->cfg.art_path, name);
    if (access(path, R_OK) == 0) { send_art(s, path); return true; }
  }
  snprintf(path, sizeof(path), "%s/%s.asc", s->cfg.art_path, name);
  if (access(path, R_OK) == 0) { send_art(s, path); return true; }
  return false;
}

/* Parse escape sequence into an F-key number (1-12). Returns 0 if not an F-key. */
static int parse_fkey(const uint8_t *line, int len)
{
  if (len < 2 || line[0] != 0x1B) return 0;

  /* VT100 application mode: ESC O P/Q/R/S = F1-F4 */
  if (len >= 3 && line[1] == 'O') {
    switch (line[2]) {
      case 'P': return 1;
      case 'Q': return 2;
      case 'R': return 3;
      case 'S': return 4;
    }
  }

  /* Linux console: ESC [ [ A/B/C/D/E = F1-F5 */
  if (len >= 4 && line[1] == '[' && line[2] == '[') {
    switch (line[3]) {
      case 'A': return 1;
      case 'B': return 2;
      case 'C': return 3;
      case 'D': return 4;
      case 'E': return 5;
    }
  }

  /* ANSI/xterm: ESC [ 1 1 ~ through ESC [ 2 4 ~ */
  if (len >= 4 && line[1] == '[') {
    /* Find '~' */
    int end = -1;
    for (int i = 2; i < len; i++) {
      if (line[i] == '~') { end = i; break; }
    }
    if (end < 3) return 0;
    char num[8] = {0};
    for (int i = 2; i < end && i - 2 < 7; i++)
      num[i - 2] = (char)line[i];
    int n = atoi(num);
    switch (n) {
      case 11: return 1;
      case 12: return 2;
      case 13: return 3;
      case 14: return 4;
      case 15: return 5;
      case 17: return 6;
      case 18: return 7;
      case 19: return 8;
      case 20: return 9;
      case 21: return 10;
      case 23: return 11;
      case 24: return 12;
    }
  }
  return 0;
}

/* Handle a sysop F-key from the remote session menu loop.
   Returns true if the input was consumed (do not continue to menu dispatch). */
static bool handle_fkey(Session *s, int fnum)
{
  if (!acs_allows(s, "+A")) return false; /* sysop only */

  char buf[256];
  switch (fnum) {
    case 1: /* F1 - who's online */
      send_str(s, "\r\n\x1b[1;36m--- Who's Online ---\x1b[0m\r\n");
      {
        char who[2048] = {0};
        online_list(who, sizeof(who));
        send_str(s, who[0] ? who : "(nobody else online)\r\n");
      }
      send_str(s, "\r\n");
      return true;

    case 2: /* F2 - broadcast */
      send_str(s, "\r\n\x1b[1;33mBroadcast message: \x1b[0m");
      {
        char msg[256] = {0};
        if (prompt_line(s, NULL, msg, sizeof(msg)) > 0 && msg[0]) {
          char full[300];
          snprintf(full, sizeof(full), "\r\n[SYSOP] %s\r\n", msg);
          online_broadcast(full);
          send_str(s, "Broadcast sent.\r\n");
        }
      }
      return true;

    case 3: /* F3 - kick node */
      send_str(s, "\r\nNode to kick: ");
      {
        char ns[8] = {0};
        if (prompt_line(s, NULL, ns, sizeof(ns)) > 0) {
          int node = atoi(ns);
          if (node > 0) {
            Session *target = online_get_node(node);
            if (target && target != s) {
              target->alive = 0;
              snprintf(buf, sizeof(buf),
                       "\r\nYou have been disconnected by the sysop.\r\n");
              send_str(target, buf);
              send_str(s, "Node kicked.\r\n");
            } else {
              send_str(s, "Node not found.\r\n");
            }
          }
        }
      }
      return true;

    case 4: /* F4 - system stats */
      {
        int users = 0, files = 0, msgs = 0;
        char who[2048] = {0};
        online_list(who, sizeof(who));
        snprintf(buf, sizeof(buf),
                 "\r\n\x1b[1;36m--- System Status ---\x1b[0m\r\n"
                 "BBS: %s  Node: %d\r\n"
                 "Online: %s\r\n",
                 s->cfg.bbs_name[0] ? s->cfg.bbs_name : "Mutineer",
                 s->node_num, who[0] ? who : "(empty)");
        send_str(s, buf);
        (void)users; (void)files; (void)msgs;
      }
      return true;

    case 8: /* F8 - twit user (kick and mark) */
      send_str(s, "\r\nNode to twit: ");
      {
        char ns[8] = {0};
        if (prompt_line(s, NULL, ns, sizeof(ns)) > 0) {
          int node = atoi(ns);
          if (node > 0) {
            Session *target = online_get_node(node);
            if (target && target != s) {
              target->alive = 0;
              send_str(target, "\r\nReturning you to the BBS.\r\n");
              send_str(s, "User twittered.\r\n");
            } else {
              send_str(s, "Node not found.\r\n");
            }
          }
        }
      }
      return true;

    case 10: /* F10 - initiate chat with node */
      send_str(s, "\r\nChat with node: ");
      {
        char ns[8] = {0};
        if (prompt_line(s, NULL, ns, sizeof(ns)) > 0) {
          int node = atoi(ns);
          if (node > 0) {
            Session *target = online_get_node(node);
            if (target && target != s) {
              send_str(target, "\r\n[Sysop requests chat. Enter split chat.]\r\n");
              split_chat_start(s, target);
            } else {
              send_str(s, "Node not found.\r\n");
            }
          }
        }
      }
      return true;

    default:
      return false; /* unbound F-key — ignore */
  }
}

int prompt_line(Session *s, const char *prompt, char *out, size_t cap)
{
  if (!s || !out || cap == 0)
    return -1;
  if (prompt)
    send_str(s, prompt);
  uint8_t line[256];
  int n = readline_echo(s, line, sizeof(line), s->cfg.idle_timeout_sec, ECHO_ON);
  if (n < 0)
    return n;
  send_str(s, "\r\n"); /* newline after user input */
  line[(cap - 1 < (size_t)n) ? cap - 1 : n] = 0;
  snprintf(out, cap, "%s", line);
  return n;
}

/* Password prompt - echoes dots instead of characters */
static int prompt_password(Session *s, const char *prompt, char *out, size_t cap)
{
  if (!s || !out || cap == 0)
    return -1;
  if (prompt)
    send_str(s, prompt);
  uint8_t line[256];
  int n = readline_echo(s, line, sizeof(line), s->cfg.idle_timeout_sec, ECHO_DOTS);
  if (n < 0)
    return n;
  send_str(s, "\r\n"); /* newline after user input */
  line[(cap - 1 < (size_t)n) ? cap - 1 : n] = 0;
  snprintf(out, cap, "%s", line);
  return n;
}

/* Drain any pending telnet negotiation data (non-blocking) */
static void drain_telnet_negotiation(Session *s)
{
  fd_set rfds;
  struct timeval tv = {.tv_sec = 0, .tv_usec = 100000}; /* 100ms */

  for (int i = 0; i < 10; i++)
  {
    FD_ZERO(&rfds);
    FD_SET(s->fd, &rfds);
    int r = select(s->fd + 1, &rfds, NULL, NULL, &tv);
    if (r <= 0)
      break; /* no more data or error */

    uint8_t buf[256];
    ssize_t n = recv(s->fd, buf, sizeof(buf), MSG_DONTWAIT);
    if (n <= 0)
      break;

    /* Process through telnet parser to handle negotiation */
    uint8_t clean[256];
    telnet_feed(&s->tn, s->fd, buf, (size_t)n, clean, sizeof(clean));
    /* Discard the clean output - we only want to process telnet commands */
  }
}

static void send_welcome_letter(Session *s, int user_id, const char *handle)
{
  if (!s->cfg.welcome_letter_enabled || !s->cfg.welcome_letter_file[0])
    return;

  FILE *f = fopen(s->cfg.welcome_letter_file, "r");
  if (!f)
    return;

  char body[4096] = {0};
  size_t total = 0;
  char line[256];
  while (fgets(line, sizeof(line), f) && total < sizeof(body) - 256)
  {
    size_t len = strlen(line);
    memcpy(body + total, line, len);
    total += len;
  }
  fclose(f);

  if (body[0])
  {
    /* Post as private email to the new user */
    db_message_post(s->db, 0, 1, "Welcome to the BBS!", body, 0);

    /* Also send as SMW notification */
    char smw_msg[256];
    snprintf(smw_msg, sizeof(smw_msg), "Welcome! Check your email for a welcome message from %s.",
             s->cfg.welcome_letter_from);
    db_smw_send(s->db, 1, s->cfg.welcome_letter_from, user_id, handle, smw_msg);
  }
}

static int authenticate(Session *s, DbUser *user_out)
{
  char handle[64] = {0};

  /* Prompt for handle */
  if (s->ansi)
  {
    send_str(s, "\x1b[32mHandle: \x1b[0m");
  }
  else
  {
    send_str(s, "Handle: ");
  }

  int n = prompt_line(s, NULL, handle, sizeof(handle));
  if (n <= 0 || handle[0] == 0)
  {
    log_warn("auth: handle read failed n=%d", n);
    return n;
  }
  log_info("auth: handle=%s", handle);

  /* Check for guest login */
  if (s->cfg.guest_enabled && strcasecmp(handle, s->cfg.guest_handle) == 0)
  {
    send_str(s, "\r\n\x1b[1;33mGuest Login\x1b[0m\r\n");
    send_str(s, "Please provide some information:\r\n\r\n");

    char guest_name[64] = {0}, guest_location[64] = {0}, guest_referral[64] = {0};
    prompt_line(s, "Your Name: ", guest_name, sizeof(guest_name));
    prompt_line(s, "Location (City, State): ", guest_location, sizeof(guest_location));
    prompt_line(s, "How did you hear about us? ", guest_referral, sizeof(guest_referral));

    /* Create temporary guest user record */
    DbUser guest = {0};
    guest.id = 0; /* No real ID */
    snprintf(guest.handle, sizeof(guest.handle), "%s",
             s->cfg.guest_handle[0] ? s->cfg.guest_handle : "GUEST");
    snprintf(guest.real_name, sizeof(guest.real_name), "%s", guest_name[0] ? guest_name : guest.handle);
    snprintf(guest.city_state, sizeof(guest.city_state), "%s", guest_location);
    guest.security_level_id = s->cfg.guest_level_id;
    guest.level = 10;          /* Basic guest level */
    guest.time_limit_min = 30; /* 30 minute limit for guests */

    if (user_out)
      *user_out = guest;
    log_info("auth: guest login from %s (%s)", guest_name, guest_location);
    send_str(s, "\r\nWelcome, guest! Your access is limited.\r\n");
    return 1;
  }

  if (login_throttled(&s->cfg, s->ip, handle))
  {
    send_str(s, "\r\nToo many attempts. Please wait and try again.\r\n");
    return -1;
  }

  DbUser user;
  if (db_user_fetch(s->db, handle, &user))
  {
    char pw[64] = {0};

    /* Prompt for password (echoes dots) */
    if (s->ansi)
    {
      n = prompt_password(s, "\x1b[32mPassword: \x1b[0m", pw, sizeof(pw));
    }
    else
    {
      n = prompt_password(s, "Password: ", pw, sizeof(pw));
    }
    if (n <= 0)
    {
      log_warn("auth: pw read failed n=%d", n);
      return n;
    }
    if (!pw_hash_verify(pw, user.pw_hash))
    {
      send_str(s, "\r\nInvalid password.\r\n");
      login_record(&s->cfg, s->ip, handle, false);
      log_warn("auth: invalid password for %s", handle);

      /* Offer password recovery if security question is set */
      char question[128], answer_hash[128];
      if (db_user_get_security_question(s->db, handle, question, sizeof(question), answer_hash, sizeof(answer_hash)))
      {
        send_str(s, "Forgot your password? (Y/N): ");
        uint8_t line[8];
        int r = session_readline(s, line, sizeof(line), 30);
        if (r > 0 && (line[0] == 'Y' || line[0] == 'y'))
        {
          send_str(s, "\r\nSecurity Question: ");
          send_str(s, question);
          send_str(s, "\r\nAnswer: ");
          char answer[64] = {0};
          r = session_readline(s, (uint8_t *)answer, sizeof(answer), 60);
          if (r > 0 && answer[0])
          {
            char input_hash[256];
            if (pw_hash_make(answer, input_hash, sizeof(input_hash)) &&
                pw_hash_verify(answer, answer_hash))
            {
              /* Correct answer - allow password reset */
              send_str(s, "\r\nCorrect! Enter new password: ");
              char newpw[64] = {0};
              r = prompt_password(s, NULL, newpw, sizeof(newpw));
              if (r > 0 && newpw[0])
              {
                char newhash[256];
                if (pw_hash_make(newpw, newhash, sizeof(newhash)))
                {
                  db_user_set_pw(s->db, user.id, newhash);
                  send_str(s, "\r\nPassword changed. Please log in again.\r\n");
                  log_audit(user.handle, "password_recovery", "Password reset via security question");
                }
              }
            }
            else
            {
              send_str(s, "\r\nIncorrect answer.\r\n");
            }
          }
        }
      }
      return -1;
    }
    if (pw_hash_needs_upgrade(user.pw_hash) && s->cfg.password_upgrade)
    {
      char nhash[256];
      if (pw_hash_make(pw, nhash, sizeof(nhash)))
      {
        db_user_set_pw(s->db, user.id, nhash);
      }
    }
    db_user_touch_login(s->db, user.id);

    /* Check password expiration */
    if (s->cfg.password_expire_days > 0)
    {
      int pw_age = db_user_pw_age_days(s->db, user.id);
      if (pw_age >= s->cfg.password_expire_days)
      {
        if (!send_named_art(s, "PWCHANGE")) {
          char buf[128];
          snprintf(buf, sizeof(buf), "\r\n\x1b[1;33mYour password has expired (%d days old).\x1b[0m\r\n", pw_age);
          send_str(s, buf);
          send_str(s, "You must change your password to continue.\r\n\r\n");
        }

        char newpw[64] = {0}, confirm[64] = {0};
        n = prompt_password(s, "New Password: ", newpw, sizeof(newpw));
        if (n <= 0 || !newpw[0])
        {
          send_str(s, "\r\nPassword change required. Goodbye.\r\n");
          return -1;
        }
        n = prompt_password(s, "Confirm Password: ", confirm, sizeof(confirm));
        if (n <= 0 || strcmp(newpw, confirm) != 0)
        {
          send_str(s, "\r\nPasswords do not match. Goodbye.\r\n");
          return -1;
        }

        char newhash[256];
        if (pw_hash_make(newpw, newhash, sizeof(newhash)))
        {
          db_user_set_pw_with_timestamp(s->db, user.id, newhash);
          send_str(s, "\r\nPassword changed successfully.\r\n");
        }
      }
    }

    if (user_out)
      *user_out = user;
    login_record(&s->cfg, s->ip, handle, true);
    return 1;
  }
  else
  {
    char ans[8] = {0};
    send_str(s, "\r\nNew user? (Y/n): ");
    n = prompt_line(s, NULL, ans, sizeof(ans));
    if (n < 0)
      return n;
    if (ans[0] == 'N' || ans[0] == 'n')
      return -1;

    send_str(s, "\r\n=== New User Registration ===\r\n\r\n");

    /* Password */
    char pw[64] = {0};
    char pw2[64] = {0};

    n = prompt_password(s, "Choose password: ", pw, sizeof(pw));
    if (n <= 0)
      return n;

    n = prompt_password(s, "Confirm password: ", pw2, sizeof(pw2));
    if (n < 0)
      return n;

    if (strcmp(pw, pw2) != 0)
    {
      send_str(s, "\r\nPasswords do not match.\r\n");
      return -1;
    }

    if (strlen(pw) < 4)
    {
      send_str(s, "\r\nPassword must be at least 4 characters.\r\n");
      return -1;
    }

    /* Required: City, State/Region */
    char city_state[64] = {0};
    send_str(s, "\r\n");
    n = prompt_line(s, "City, State/Region: ", city_state, sizeof(city_state));
    if (n < 0)
      return n;
    if (strlen(city_state) < 2)
    {
      send_str(s, "\r\nCity/State is required.\r\n");
      return -1;
    }

    /* Required: Email */
    char email[64] = {0};
    n = prompt_line(s, "Email address: ", email, sizeof(email));
    if (n < 0)
      return n;
    if (strlen(email) < 5 || !strchr(email, '@'))
    {
      send_str(s, "\r\nValid email address is required.\r\n");
      return -1;
    }

    /* Optional: Social media link */
    char social[128] = {0};
    n = prompt_line(s, "Social media link (optional): ", social, sizeof(social));
    if (n < 0)
      return n;

    /* Optional: Message to SysOp */
    char sysop_msg[256] = {0};
    n = prompt_line(s, "Message to SysOp (optional): ", sysop_msg, sizeof(sysop_msg));
    if (n < 0)
      return n;

    char hash[128];
    if (!pw_hash_make(pw, hash, sizeof(hash)))
    {
      send_str(s, "\r\nFailed to hash password.\r\n");
      return -1;
    }

    DbUserRegInfo reg = {0};
    reg.handle = handle;
    reg.pw_hash = hash;
    reg.email = email;
    reg.city_state = city_state;
    reg.social_link = social[0] ? social : NULL;
    reg.sysop_msg = sysop_msg[0] ? sysop_msg : NULL;
    reg.security_level_id = 1;

    if (!db_user_create_ex(s->db, &reg))
    {
      send_str(s, "\r\nFailed to create user.\r\n");
      return -1;
    }
    if (db_user_fetch(s->db, handle, &user))
    {
      if (user_out)
        *user_out = user;
      /* seed credits */
      db_user_update_time_credit(s->db, user.id, s->cfg.session_time_limit_min, s->cfg.default_credits, s->cfg.default_file_points);
      db_user_fetch(s->db, handle, &user); /* reload after updates */
      if (user_out)
        *user_out = user;
      /* Send welcome letter */
      send_welcome_letter(s, user.id, user.handle);
      send_str(s, "\r\nUser created. Welcome aboard!\r\n");
      return 1;
    }
    return -1;
  }
}

/* Full-screen editor for message composition */
#define FSEDIT_MAX_LINES 50
#define FSEDIT_LINE_LEN 78

typedef struct
{
  char lines[FSEDIT_MAX_LINES][FSEDIT_LINE_LEN + 1];
  int line_count;
  int cur_line;
  int cur_col;
  int insert_mode;
} FsEditor;

static void fsedit_redraw(Session *s, FsEditor *ed)
{
  char buf[256];
  send_str(s, "\x1b[2J\x1b[H"); /* Clear screen */
  send_str(s, "\x1b[1;36m=== Full-Screen Editor ===\x1b[0m\r\n");
  send_str(s, "\x1b[1;33mCtrl-S=Save  Ctrl-A=Abort  Ctrl-Y=Delete Line  Ctrl-N=Insert Line\x1b[0m\r\n");
  send_str(s, "----------------------------------------------------------------------\r\n");

  for (int i = 0; i < ed->line_count && i < 20; i++)
  {
    snprintf(buf, sizeof(buf), "%s%2d|%s\x1b[0m\r\n",
             (i == ed->cur_line) ? "\x1b[1;32m" : "",
             i + 1, ed->lines[i]);
    send_str(s, buf);
  }

  send_str(s, "----------------------------------------------------------------------\r\n");
  snprintf(buf, sizeof(buf), "Line %d/%d  Col %d  %s\r\n",
           ed->cur_line + 1, ed->line_count, ed->cur_col + 1,
           ed->insert_mode ? "[INS]" : "[OVR]");
  send_str(s, buf);

  /* Position cursor */
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", ed->cur_line + 4, ed->cur_col + 4);
  send_str(s, buf);
}

int fsedit_edit(Session *s, char *text_out, size_t text_cap)
{
  FsEditor ed;
  memset(&ed, 0, sizeof(ed));
  ed.line_count = 1;
  ed.insert_mode = 1;

  /* Parse existing text into lines if provided */
  if (text_out[0])
  {
    char *p = text_out;
    while (*p && ed.line_count < FSEDIT_MAX_LINES)
    {
      char *nl = strchr(p, '\n');
      size_t len = nl ? (size_t)(nl - p) : strlen(p);
      if (len > FSEDIT_LINE_LEN)
        len = FSEDIT_LINE_LEN;
      strncpy(ed.lines[ed.line_count - 1], p, len);
      ed.lines[ed.line_count - 1][len] = '\0';
      /* Remove trailing \r */
      char *cr = strchr(ed.lines[ed.line_count - 1], '\r');
      if (cr)
        *cr = '\0';
      if (nl)
      {
        ed.line_count++;
        p = nl + 1;
      }
      else
      {
        break;
      }
    }
  }

  fsedit_redraw(s, &ed);

  while (1)
  {
    uint8_t buf[8];
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(s->fd, &rfds);
    struct timeval tv = {.tv_sec = 60, .tv_usec = 0};
    int r = select(s->fd + 1, &rfds, NULL, NULL, &tv);
    if (r <= 0)
      continue;

    ssize_t n = recv(s->fd, buf, 1, 0);
    if (n <= 0)
      return -1;

    uint8_t ch = buf[0];

    /* Handle escape sequences */
    if (ch == 0x1B)
    {
      /* Read escape sequence */
      n = recv(s->fd, buf, 2, MSG_DONTWAIT);
      if (n == 2 && buf[0] == '[')
      {
        switch (buf[1])
        {
        case 'A': /* Up arrow */
          if (ed.cur_line > 0)
            ed.cur_line--;
          break;
        case 'B': /* Down arrow */
          if (ed.cur_line < ed.line_count - 1)
            ed.cur_line++;
          break;
        case 'C': /* Right arrow */
          if (ed.cur_col < (int)strlen(ed.lines[ed.cur_line]))
            ed.cur_col++;
          break;
        case 'D': /* Left arrow */
          if (ed.cur_col > 0)
            ed.cur_col--;
          break;
        }
        fsedit_redraw(s, &ed);
      }
      continue;
    }

    /* Ctrl-S = Save */
    if (ch == 0x13)
    {
      text_out[0] = '\0';
      for (int i = 0; i < ed.line_count; i++)
      {
        strncat(text_out, ed.lines[i], text_cap - strlen(text_out) - 3);
        strncat(text_out, "\r\n", text_cap - strlen(text_out) - 1);
      }
      return 1;
    }

    /* Ctrl-A = Abort */
    if (ch == 0x01)
    {
      return 0;
    }

    /* Ctrl-Y = Delete line */
    if (ch == 0x19)
    {
      if (ed.line_count > 1)
      {
        for (int i = ed.cur_line; i < ed.line_count - 1; i++)
        {
          strcpy(ed.lines[i], ed.lines[i + 1]);
        }
        ed.line_count--;
        if (ed.cur_line >= ed.line_count)
          ed.cur_line = ed.line_count - 1;
      }
      else
      {
        ed.lines[0][0] = '\0';
      }
      ed.cur_col = 0;
      fsedit_redraw(s, &ed);
      continue;
    }

    /* Ctrl-N = Insert line */
    if (ch == 0x0E)
    {
      if (ed.line_count < FSEDIT_MAX_LINES)
      {
        for (int i = ed.line_count; i > ed.cur_line + 1; i--)
        {
          strcpy(ed.lines[i], ed.lines[i - 1]);
        }
        ed.lines[ed.cur_line + 1][0] = '\0';
        ed.line_count++;
        ed.cur_line++;
        ed.cur_col = 0;
      }
      fsedit_redraw(s, &ed);
      continue;
    }

    /* Enter = new line */
    if (ch == '\r' || ch == '\n')
    {
      if (ed.line_count < FSEDIT_MAX_LINES)
      {
        /* Split current line at cursor */
        char rest[FSEDIT_LINE_LEN + 1];
        strcpy(rest, ed.lines[ed.cur_line] + ed.cur_col);
        ed.lines[ed.cur_line][ed.cur_col] = '\0';

        /* Move lines down */
        for (int i = ed.line_count; i > ed.cur_line + 1; i--)
        {
          strcpy(ed.lines[i], ed.lines[i - 1]);
        }
        strcpy(ed.lines[ed.cur_line + 1], rest);
        ed.line_count++;
        ed.cur_line++;
        ed.cur_col = 0;
      }
      fsedit_redraw(s, &ed);
      continue;
    }

    /* Backspace */
    if (ch == 0x08 || ch == 0x7F)
    {
      if (ed.cur_col > 0)
      {
        char *line = ed.lines[ed.cur_line];
        memmove(line + ed.cur_col - 1, line + ed.cur_col, strlen(line + ed.cur_col) + 1);
        ed.cur_col--;
      }
      else if (ed.cur_line > 0)
      {
        /* Join with previous line */
        int prev_len = (int)strlen(ed.lines[ed.cur_line - 1]);
        if (prev_len + (int)strlen(ed.lines[ed.cur_line]) < FSEDIT_LINE_LEN)
        {
          strcat(ed.lines[ed.cur_line - 1], ed.lines[ed.cur_line]);
          for (int i = ed.cur_line; i < ed.line_count - 1; i++)
          {
            strcpy(ed.lines[i], ed.lines[i + 1]);
          }
          ed.line_count--;
          ed.cur_line--;
          ed.cur_col = prev_len;
        }
      }
      fsedit_redraw(s, &ed);
      continue;
    }

    /* Regular character */
    if (ch >= 0x20 && ch < 0x7F)
    {
      char *line = ed.lines[ed.cur_line];
      int len = (int)strlen(line);
      if (len < FSEDIT_LINE_LEN)
      {
        if (ed.insert_mode)
        {
          memmove(line + ed.cur_col + 1, line + ed.cur_col, strlen(line + ed.cur_col) + 1);
        }
        line[ed.cur_col] = (char)ch;
        if (ed.cur_col >= len)
          line[ed.cur_col + 1] = '\0';
        ed.cur_col++;
        fsedit_redraw(s, &ed);
      }
    }
  }
}

static void handle_action(Session *s, const char *action)
{
  if (!strcmp(action, "who"))
  {
    if (HAS_AC_FLAG(&s->user, AC_RUSERLIST))
    {
      send_str(s, "\r\n\x1b[1;31mYou are restricted from viewing user list.\x1b[0m\r\n");
      return;
    }
    char tmp[2048];
    DbNode nodes[64];
    int n = db_node_list(s->db, nodes, 64);
    int o = 0;
    o += snprintf(tmp + o, sizeof(tmp) - o, "\r\nOnline users (nodes):\r\n");
    if (n == 0)
    {
      o += snprintf(tmp + o, sizeof(tmp) - o, "  (none)\r\n");
    }
    else
    {
      for (int i = 0; i < n; i++)
      {
        o += snprintf(tmp + o, sizeof(tmp) - o, "  Node %d  %-12s IP %-15s  %s\r\n",
                      nodes[i].node_num,
                      nodes[i].handle[0] ? nodes[i].handle : "(unknown)",
                      nodes[i].ip,
                      nodes[i].status);
        if (o >= (int)sizeof(tmp))
          break;
      }
    }
    send_str(s, tmp);
    send_str(s, "\r\n(press ENTER)\r\n");
    uint8_t line[64];
    session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
  }
  else if (!strcmp(action, "wall"))
  {
    char msg[256] = {0};
    prompt_line(s, "Wall message: ", msg, sizeof(msg));
    char out[320];
    snprintf(out, sizeof(out), "\r\n[Wall] %s: %s\r\n", s->user.handle, msg);
    online_broadcast(out);
  }
  else if (!strcmp(action, "whisper"))
  {
    char nodebuf[16] = {0}, msg[256] = {0};
    prompt_line(s, "Node #: ", nodebuf, sizeof(nodebuf));
    prompt_line(s, "Message: ", msg, sizeof(msg));
    int node = atoi(nodebuf);
    Session *dest = online_get_node(node);
    if (dest)
    {
      char out[320];
      snprintf(out, sizeof(out), "\r\n[Whisper from %s] %s\r\n", s->user.handle, msg);
      fd_write_all(dest->fd, out, strlen(out));
    }
    else
    {
      send_str(s, "\r\nNode not online.\r\n");
    }
  }
  else if (!strcmp(action, "messages"))
  {
    if (HAS_AC_FLAG(&s->user, AC_RMSG))
    {
      send_str(s, "\r\n\x1b[1;31mYou are restricted from messages.\x1b[0m\r\n");
      return;
    }
    DbMsgArea areas[32];
    int acount = db_msg_area_list(s->db, areas, 32);
    if (acount == 0)
    {
      send_str(s, "\r\nNo message areas.\r\n");
      return;
    }
    send_str(s, "\r\nMessage Areas:\r\n");
    char linebuf[512];
    for (int i = 0; i < acount; i++)
    {
      if (!acs_allows(s, areas[i].acs))
        continue;
      snprintf(linebuf, sizeof(linebuf), "  [%d] %s\r\n", areas[i].id, areas[i].name);
      send_str(s, linebuf);
    }
    if (acs_allows(s, "+A"))
      send_str(s, "  [N] New area (sysop)\r\n");
    send_str(s, "Choose area number: ");
    uint8_t line[64];
    int n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n <= 0)
      return;
    if ((line[0] == 'N' || line[0] == 'n') && acs_allows(s, "+A"))
    {
      char name[64];
      prompt_line(s, "Area name: ", name, sizeof(name));
      int new_id = 0;
      db_message_area_manage(s->db, name, "", &new_id, false);
    }
    int area_id = atoi((char *)line);
    if (area_id <= 0)
    {
      send_str(s, "\r\nInvalid area.\r\n");
      return;
    }
    s->current_msg_area = area_id;

    DbMessage msgs[32];
    int mcount = db_messages_list(s->db, area_id, msgs, 32);
    send_str(s, "\r\nThreaded messages:\r\n");
    for (int i = 0; i < mcount; i++)
    {
      int depth = 0;
      int pid = msgs[i].reply_to;
      while (pid > 0 && depth < 5)
      {
        DbMessage tmp;
        if (!db_message_get(s->db, pid, &tmp))
          break;
        depth++;
        pid = tmp.reply_to;
      }
      int indent = depth * 2;
      snprintf(linebuf, sizeof(linebuf), "%*s#%d %s (%s) %s\r\n", indent, "", msgs[i].id, msgs[i].subject, msgs[i].user_handle, msgs[i].created_at);
      send_str(s, linebuf);
    }
    send_str(s, "\r\nSelect msg # to read (blank skip): ");
    n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    int msg_id = 0;
    if (n > 0)
      msg_id = atoi((char *)line);
    DbMessage view;
    if (msg_id > 0 && db_message_get(s->db, msg_id, &view))
    {
      send_str(s, "\r\n");
      send_str(s, view.subject);
      send_str(s, " by ");
      send_str(s, view.user_handle);
      send_str(s, "\r\n");
      send_str(s, view.body);
      send_str(s, "\r\n");
    }
    send_str(s, "(N)ew post, (R)eply, (M)ail user, (Q)uit, (D)elete: ");
    n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    char subj[80] = {0}, body[512] = {0};
    if (n > 0 && (line[0] == 'N' || line[0] == 'n'))
    {
      if (HAS_AC_FLAG(&s->user, AC_RPOST))
      {
        send_str(s, "\r\n\x1b[1;31mYou are restricted from posting.\x1b[0m\r\n");
      }
      else
      {
        prompt_line(s, "Subject: ", subj, sizeof(subj));
        prompt_line(s, "Body: ", body, sizeof(body));
        if (db_message_post(s->db, area_id, s->user.id, subj, body, 0))
        {
          send_str(s, "\r\nPosted.\r\n");
          db_stats_inc(s->db, "posts");
          s->user.msg_post++;
        }
      }
    }
    else if (n > 0 && (line[0] == 'R' || line[0] == 'r') && msg_id > 0)
    {
      snprintf(subj, sizeof(subj), "Re: %s", view.subject);
      /* quote */
      char quoted[512] = {0};
      const char *p = view.body;
      while (*p && strlen(quoted) + 2 < sizeof(quoted))
      {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        strncat(quoted, "> ", sizeof(quoted) - strlen(quoted) - 1);
        strncat(quoted, p, len < sizeof(quoted) - strlen(quoted) - 1 ? len : sizeof(quoted) - strlen(quoted) - 1);
        strncat(quoted, "\r\n", sizeof(quoted) - strlen(quoted) - 1);
        if (!nl)
          break;
        p = nl + 1;
      }
      send_str(s, "Quoted:\r\n");
      send_str(s, quoted);
      prompt_line(s, "Body: ", body, sizeof(body));
      strncat(body, "\r\n", sizeof(body) - strlen(body) - 1);
      strncat(body, quoted, sizeof(body) - strlen(body) - 1);
      if (db_message_post(s->db, area_id, s->user.id, subj, body, msg_id))
      {
        send_str(s, "\r\nReplied.\r\n");
        db_stats_inc(s->db, "posts");
      }
    }
    else if (n > 0 && (line[0] == 'M' || line[0] == 'm'))
    {
      char to[64] = {0};
      prompt_line(s, "Send private mail to: ", to, sizeof(to));
      DbUser u;
      if (!db_user_fetch(s->db, to, &u))
      {
        send_str(s, "\r\nNo such user.\r\n");
      }
      else
      {
        prompt_line(s, "Subject: ", subj, sizeof(subj));
        prompt_line(s, "Body: ", body, sizeof(body));
        if (db_message_post(s->db, area_id, s->user.id, subj, body, 0))
        {
          /* mark to_user */
          int last_id = db_last_insert_id(s->db);
          db_message_set_to_user(s->db, last_id, u.id);
          send_str(s, "\r\nMail sent.\r\n");
          db_stats_inc(s->db, "emails");
        }
      }
    }
    else if (n > 0 && (line[0] == 'D' || line[0] == 'd') && acs_allows(s, "+A"))
    {
      char confirm[8] = {0};
      prompt_line(s, "Delete message #:", confirm, sizeof(confirm));
      int del = atoi(confirm);
#ifdef HAVE_SQLITE
      char sql[64];
      snprintf(sql, sizeof(sql), "DELETE FROM messages WHERE id=%d", del);
      db_exec(s->db, sql);
      send_str(s, "\r\nDeleted.\r\n");
#else
      send_str(s, "\r\nDB not available.\r\n");
#endif
    }
  }
  else if (!strcmp(action, "files"))
  {
    DbFileArea areas[16];
    int acount = db_file_area_list(s->db, areas, 16);
    if (acount == 0)
    {
      send_str(s, "\r\nNo file areas.\r\n");
      return;
    }
    send_str(s, "\r\nFile Areas:\r\n");
    char buf[256];
    for (int i = 0; i < acount; i++)
    {
      snprintf(buf, sizeof(buf), "  [%d] %s (%s)\r\n", areas[i].id, areas[i].name, areas[i].path);
      send_str(s, buf);
    }
    send_str(s, "Choose area number: ");
    uint8_t line[64];
    int n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n <= 0)
      return;
    int area_id = atoi((char *)line);
    if (area_id <= 0)
    {
      send_str(s, "\r\nInvalid area.\r\n");
      return;
    }

    DbFileArea area = {0};
    for (int i = 0; i < acount; i++)
      if (areas[i].id == area_id)
        area = areas[i];
    if (area.id == 0)
    {
      send_str(s, "\r\nArea not found.\r\n");
      return;
    }
    if (!acs_allows(s, area.acs_list))
    {
      send_str(s, "\r\nAccess denied.\r\n");
      return;
    }
    s->current_file_area = area.id;

    DbFileRec files[10];
    int fcount = db_file_list(&area, s->db, files, 10);
    send_str(s, "\r\nFiles:\r\n");
    for (int i = 0; i < fcount; i++)
    {
      snprintf(buf, sizeof(buf), "#%d %-20s %8d bytes by %s\r\n  %s\r\n",
               files[i].id, files[i].filename, files[i].size_bytes, files[i].uploader, files[i].desc);
      send_str(s, buf);
    }
    send_str(s, "Add to batch queue # (blank skip): ");
    n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n > 0 && line[0])
    {
      int id = atoi((char *)line);
      if (s->batch_count < (int)(sizeof(s->batch_queue) / sizeof(s->batch_queue[0])))
      {
        s->batch_queue[s->batch_count++] = id;
        s->batch_area[s->batch_count - 1] = area.id;
        send_str(s, "\r\nQueued.\r\n");
      }
      else
        send_str(s, "\r\nQueue full.\r\n");
    }
    send_str(s, "\r\nDownload file # (blank to skip): ");
    n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n > 0 && line[0])
    {
      int id = atoi((char *)line);
      DbFileRec rec;
      if (!db_file_get(s->db, id, &rec))
      {
        send_str(s, "\r\nNot found.\r\n");
      }
      else
      {
        /* ratio/credit enforcement */
        int cost = (rec.size_bytes / 1024) + 1;
        if (s->credits < cost)
        {
          if (!send_named_art(s, "NOCREDTS"))
            send_str(s, "\r\nNot enough credits.\r\n");
        }
        else
        {
          s->credits -= cost;
          db_user_update_time_credit(s->db, s->user.id, s->time_left_min, s->credits, s->file_points);
          char path[512];
          if (!file_area_resolve(area.path, rec.filename, path, sizeof(path)))
          {
            send_str(s, "\r\nPath error.\r\n");
          }
          else if (access(path, R_OK) != 0)
          {
            send_str(s, "\r\nFile missing.\r\n");
          }
          else
          {
            send_str(s, "\r\nStarting file transfer...\r\n");
            DbProtocol protos[4];
            int pcnt = db_protocols_list(s->db, protos, 4, "down");
            if (pcnt > 0)
              protocol_launch(s, &protos[0], path, "down");
            else
            {
              /* fallback: raw send */
              char *data = file_read_all(path, NULL);
              if (data)
              {
                fd_write_all(s->fd, data, strlen(data));
                free(data);
              }
            }
            db_stats_inc(s->db, "downloads");
          }
        }
      }
    }
    send_str(s, "\r\nUpload (enter path to local file, or leave blank to skip): ");
    n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n > 0 && line[0])
    {
      char srcpath[256];
      snprintf(srcpath, sizeof(srcpath), "%s", line);
      char fname[128] = {0}, desc[256] = {0};
      const char *base = strrchr(srcpath, '/');
      snprintf(fname, sizeof(fname), "%s", base ? base + 1 : srcpath);
      prompt_line(s, "Description: ", desc, sizeof(desc));

      char dst[512];
      if (!file_area_resolve(area.path, fname, dst, sizeof(dst)))
      {
        send_str(s, "\r\nInvalid filename.\r\n");
        return;
      }
      int size_bytes = 0;
      if (!file_store_copy(srcpath, dst, &size_bytes))
      {
        send_str(s, "\r\nUpload failed (cannot store file).\r\n");
      }
      else if (db_file_add(s->db, area.id, fname, desc, size_bytes, s->user.id))
      {
        /* checksum duplicate detection using EVP API */
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hash_len = 0;
        FILE *f = fopen(dst, "rb");
        if (f)
        {
          EVP_MD_CTX *ctx = EVP_MD_CTX_new();
          if (ctx)
          {
            EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
            char bufh[8192];
            size_t r;
            while ((r = fread(bufh, 1, sizeof(bufh), f)) > 0)
              EVP_DigestUpdate(ctx, bufh, r);
            EVP_DigestFinal_ex(ctx, hash, &hash_len);
            EVP_MD_CTX_free(ctx);
          }
          fclose(f);
          char hex[65];
          for (unsigned int i = 0; i < hash_len && i < 32; i++)
            sprintf(hex + i * 2, "%02x", hash[i]);
          hex[64] = '\0';
          int new_id = db_last_insert_id(s->db);
          db_file_mark_hash(s->db, new_id, hex);
          int dup_id = 0;
          if (db_file_exists_hash(s->db, hex, &dup_id) && dup_id != new_id)
          {
            send_str(s, "\r\nDuplicate detected!\r\n");
          }
        }
        send_str(s, "\r\nFile stored and entry added.\r\n");
        s->file_points += size_bytes / 1024;
        db_user_update_time_credit(s->db, s->user.id, s->time_left_min, s->credits, s->file_points);
        db_stats_inc(s->db, "uploads");
      }
      else
      {
        send_str(s, "\r\nDB add failed (file was copied).\r\n");
      }
    }
  }
  else if (!strcmp(action, "chat"))
  {
    if (HAS_AC_FLAG(&s->user, AC_RCHAT))
    {
      send_str(s, "\r\n\x1b[1;31mYou are restricted from chat.\x1b[0m\r\n");
      return;
    }
    send_str(s, "\r\nEntering multi-node chat. Type /quit to leave.\r\n");
    uint8_t chanbuf[8];
    send_str(s, "Channel (1-9, default 1): ");
    session_readline(s, chanbuf, sizeof(chanbuf), 5);
    int ch = atoi((char *)chanbuf);
    if (ch <= 0 || ch > 9)
      ch = 1;
    s->chat_channel = ch;
    time_t last = 0;
    while (s->alive && !g_stop)
    {
      db_node_upsert(s->db, s->node_num, s->user.id, "chat", "chat", s->ip);
      char buf[1024];
      int n = chat_dump(s->chat_channel, last, buf, sizeof(buf));
      if (n > 0)
      {
        send_str(s, buf);
      }
      last = time(NULL);
      send_str(s, "> ");
      uint8_t line[256];
      int r = session_readline(s, line, sizeof(line), 60);
      if (r <= 0)
      {
        send_str(s, "\r\nLeaving chat.\r\n");
        break;
      }
      if (!strcmp((char *)line, "/quit"))
      {
        send_str(s, "\r\nLeaving chat.\r\n");
        break;
      }
      chat_post(s->chat_channel, s->user.handle, (char *)line);
    }
  }
  else if (!strcmp(action, "linechat"))
  {
    char nodebuf[8] = {0};
    prompt_line(s, "Node to chat with: ", nodebuf, sizeof(nodebuf));
    int node = atoi(nodebuf);
    if (node <= 0)
      return;
    int chan = 100 + node;
    s->chat_channel = chan;
    send_str(s, "\r\nLine chat started (/quit to end).\r\n");
    time_t last = 0;
    while (s->alive && !g_stop)
    {
      char buf[512];
      int n = chat_dump(chan, last, buf, sizeof(buf));
      if (n > 0)
        send_str(s, buf);
      last = time(NULL);
      send_str(s, "> ");
      uint8_t line[256];
      int r = session_readline(s, line, sizeof(line), 120);
      if (r <= 0)
        break;
      if (!strcmp((char *)line, "/quit"))
        break;
      chat_post(chan, s->user.handle, (char *)line);
      db_node_upsert(s->db, s->node_num, s->user.id, "chat", "linechat", s->ip);
    }
  }
  else if (!strcmp(action, "splitchat"))
  {
    /* emulate split-screen by rapidly refreshing chat buffer */
    char nodebuf[8] = {0};
    prompt_line(s, "Node to split-chat with: ", nodebuf, sizeof(nodebuf));
    int node = atoi(nodebuf);
    if (node <= 0)
      return;
    int chan = 200 + node;
    s->chat_channel = chan;
    send_str(s, "\r\nSplit chat started (/quit to end).\r\n");
    while (s->alive && !g_stop)
    {
      char buf[512];
      chat_dump(chan, time(NULL) - 5, buf, sizeof(buf));
      send_str(s, buf);
      send_str(s, "\r\n> ");
      uint8_t line[256];
      int r = session_readline(s, line, sizeof(line), 10);
      if (r <= 0)
        break;
      if (!strcmp((char *)line, "/quit"))
        break;
      chat_post(chan, s->user.handle, (char *)line);
      db_node_upsert(s->db, s->node_num, s->user.id, "chat", "splitchat", s->ip);
    }
  }
  else if (!strcmp(action, "bulletins"))
  {
    DbBulletin bulls[16];
    int n = db_bulletin_list(s->db, bulls, 16);
    send_str(s, "\r\nBulletins:\r\n");
    char linebuf[512];
    for (int i = 0; i < n; i++)
    {
      if (!acs_allows(s, bulls[i].acs))
        continue;
      char title[256];
      mci_expand(s, bulls[i].title, title, sizeof(title));
      snprintf(linebuf, sizeof(linebuf), " [%d] %s by %s at %s\r\n", bulls[i].id, title, bulls[i].posted_by, bulls[i].posted_at);
      send_str(s, linebuf);
    }
    send_str(s, "Read which # (blank to skip)? ");
    uint8_t line[32];
    int r = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (r > 0)
    {
      int id = atoi((char *)line);
      for (int i = 0; i < n; i++)
        if (bulls[i].id == id)
        {
          send_str(s, "\r\n");
          char body[1024];
          mci_expand(s, bulls[i].body, body, sizeof(body));
          send_str(s, body);
          send_str(s, "\r\n");
        }
    }
    if (acs_allows(s, "+A"))
    {
      send_str(s, "\r\nPost new bulletin? (Y/N): ");
      r = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
      if (r > 0 && (line[0] == 'Y' || line[0] == 'y'))
      {
        char title[128] = {0}, body[512] = {0};
        prompt_line(s, "Title: ", title, sizeof(title));
        prompt_line(s, "Body: ", body, sizeof(body));
        db_bulletin_add(s->db, title, body, s->user.id, "");
      }
    }
  }
  else if (!strcmp(action, "oneliners"))
  {
    DbOneliner oneliners[20];
    int n = db_oneliner_list(s->db, oneliners, 20);
    send_str(s, "\r\n\x1b[1;36mOne-Liners:\x1b[0m\r\n");
    send_str(s, "\x1b[1;33m----------------------------------------------------------------------\x1b[0m\r\n");
    char linebuf[256];
    for (int i = n - 1; i >= 0; i--)
    {
      snprintf(linebuf, sizeof(linebuf), "\x1b[1;32m%s\x1b[0m: %s\r\n",
               oneliners[i].user_handle, oneliners[i].text);
      send_str(s, linebuf);
    }
    send_str(s, "\x1b[1;33m----------------------------------------------------------------------\x1b[0m\r\n");
    send_str(s, "\r\nAdd a one-liner? (Y/N): ");
    uint8_t line[8];
    int r = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (r > 0 && (line[0] == 'Y' || line[0] == 'y'))
    {
      char text[80] = {0};
      prompt_line(s, "Your one-liner (max 75 chars): ", text, sizeof(text));
      if (text[0])
      {
        if (db_oneliner_add(s->db, s->user.id, s->user.handle, text))
        {
          send_str(s, "\r\nOne-liner added!\r\n");
        }
        else
        {
          send_str(s, "\r\nFailed to add one-liner.\r\n");
        }
      }
    }
  }
  else if (!strcmp(action, "page"))
  {
    if (HAS_AC_FLAG(&s->user, AC_RCHAT))
    {
      send_str(s, "\r\n\x1b[1;31mYou are restricted from paging.\x1b[0m\r\n");
      return;
    }

    int page_limit = s->cfg.max_page_sysop;
    if (page_limit > 0 && s->pages_this_session >= page_limit)
    {
      send_str(s, "\r\nYou have reached the maximum number of sysop pages for this session.\r\n");
      /* Offer email fallback */
      send_str(s, "Leave a feedback message for the sysop? (Y/N): ");
      uint8_t yn[4] = {0};
      if (session_readline(s, yn, sizeof(yn), 30) > 0 && (yn[0] == 'Y' || yn[0] == 'y'))
        cmd_msg_write_email(s, s->cfg.sysop_name);
      return;
    }

    s->pages_this_session++;
    send_str(s, "\r\nPaging sysop...\a\a\r\n");
    chat_post(0, s->user.handle, "Page sysop");
    char page_msg[128];
    snprintf(page_msg, sizeof(page_msg), "\r\n\a[Page] %s requested!\r\n",
             s->cfg.sysop_name[0] ? s->cfg.sysop_name : "Sysop");
    online_broadcast(page_msg);
    send_str(s, "(press ENTER to continue, or /q to cancel)\r\n");
    uint8_t line[64];
    session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);

    /* After the page wait, offer email fallback */
    send_str(s, "Leave a feedback message for the sysop? (Y/N): ");
    uint8_t yn[4] = {0};
    if (session_readline(s, yn, sizeof(yn), 30) > 0 && (yn[0] == 'Y' || yn[0] == 'y'))
      cmd_msg_write_email(s, s->cfg.sysop_name);
  }
  else if (!strcmp(action, "useredit"))
  {
    if (!acs_allows(s, "+A"))
    {
      send_str(s, "\r\nAccess denied.\r\n");
      return;
    }
    char handle[64] = {0};
    prompt_line(s, "User handle: ", handle, sizeof(handle));
    DbUser u;
    if (!db_user_fetch(s->db, handle, &u))
    {
      send_str(s, "\r\nNot found.\r\n");
      return;
    }

    char buf[128] = {0};
    send_str(s, "\r\n\x1b[1;36mUser Editor\x1b[0m\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");

    char line[256];
    snprintf(line, sizeof(line), "Handle: %s  ID: %d  Level: %d  DSL: %d\r\n",
             u.handle, u.id, u.level, u.dsl);
    send_str(s, line);
    snprintf(line, sizeof(line), "Real Name: %s\r\n", u.real_name);
    send_str(s, line);
    snprintf(line, sizeof(line), "Email: %s  Phone: %s\r\n", u.email, u.phone);
    send_str(s, line);
    snprintf(line, sizeof(line), "Street: %s  City: %s  ZIP: %s\r\n",
             u.street, u.city_state, u.zip_code);
    send_str(s, line);
    snprintf(line, sizeof(line), "Sex: %c  Birth: %s  First On: %s\r\n",
             u.sex ? u.sex : '?', u.birth_date, u.first_on);
    send_str(s, line);
    snprintf(line, sizeof(line), "Calls: %d  Posts: %d  Email Sent: %d  Feedback: %d\r\n",
             u.logged_on, u.msg_post, u.email_sent, u.feedback);
    send_str(s, line);
    snprintf(line, sizeof(line), "UL: %d (%dK)  DL: %d (%dK)  Credits: %d  FP: %d\r\n",
             u.uploads, u.uk, u.downloads, u.dk, u.credits, u.file_points);
    send_str(s, line);
    snprintf(line, sizeof(line), "Time Limit: %d min  Timebank: %d min  Total Time: %d min\r\n",
             u.time_limit_min, u.timebank, u.t_time_on);
    send_str(s, line);
    snprintf(line, sizeof(line), "AR Flags: 0x%08X  AC Flags: 0x%08X  Status: 0x%08X\r\n",
             u.flags, u.ac_flags, u.status_flags);
    send_str(s, line);
    snprintf(line, sizeof(line), "Last Login: %s  Expires: %s\r\n", u.last_login_at, u.expires_at);
    send_str(s, line);
    if (u.note[0])
    {
      snprintf(line, sizeof(line), "Sysop Note: %s\r\n", u.note);
      send_str(s, line);
    }
    send_str(s, "----------------------------------------------------------------------\r\n");

    send_str(s, "\r\nEdit fields (blank to keep current value):\r\n");

    prompt_line(s, "Real Name: ", buf, sizeof(buf));
    if (buf[0])
      strncpy(u.real_name, buf, sizeof(u.real_name) - 1);

    prompt_line(s, "Email: ", buf, sizeof(buf));
    if (buf[0])
      strncpy(u.email, buf, sizeof(u.email) - 1);

    prompt_line(s, "Phone: ", buf, sizeof(buf));
    if (buf[0])
      strncpy(u.phone, buf, sizeof(u.phone) - 1);

    prompt_line(s, "Street: ", buf, sizeof(buf));
    if (buf[0])
      strncpy(u.street, buf, sizeof(u.street) - 1);

    prompt_line(s, "City/State: ", buf, sizeof(buf));
    if (buf[0])
      strncpy(u.city_state, buf, sizeof(u.city_state) - 1);

    prompt_line(s, "ZIP: ", buf, sizeof(buf));
    if (buf[0])
      strncpy(u.zip_code, buf, sizeof(u.zip_code) - 1);

    prompt_line(s, "Sex (M/F): ", buf, sizeof(buf));
    if (buf[0])
      u.sex = buf[0];

    prompt_line(s, "Birth Date (YYYY-MM-DD): ", buf, sizeof(buf));
    if (buf[0])
      strncpy(u.birth_date, buf, sizeof(u.birth_date) - 1);

    prompt_line(s, "Security Level: ", buf, sizeof(buf));
    if (buf[0])
      u.level = atoi(buf);

    prompt_line(s, "Download SL: ", buf, sizeof(buf));
    if (buf[0])
      u.dsl = atoi(buf);

    prompt_line(s, "Time Limit (min): ", buf, sizeof(buf));
    if (buf[0])
      u.time_limit_min = atoi(buf);

    prompt_line(s, "Credits: ", buf, sizeof(buf));
    if (buf[0])
      u.credits = atoi(buf);

    prompt_line(s, "File Points: ", buf, sizeof(buf));
    if (buf[0])
      u.file_points = atoi(buf);

    prompt_line(s, "Timebank: ", buf, sizeof(buf));
    if (buf[0])
      u.timebank = atoi(buf);

    prompt_line(s, "AR Flags (hex): ", buf, sizeof(buf));
    if (buf[0])
      u.flags = (unsigned)strtoul(buf, NULL, 16);

    prompt_line(s, "AC Flags (hex): ", buf, sizeof(buf));
    if (buf[0])
      u.ac_flags = (unsigned)strtoul(buf, NULL, 16);

    prompt_line(s, "Status Flags (hex): ", buf, sizeof(buf));
    if (buf[0])
      u.status_flags = (unsigned)strtoul(buf, NULL, 16);

    prompt_line(s, "Expiration Date (YYYY-MM-DD): ", buf, sizeof(buf));
    if (buf[0])
      strncpy(u.expires_at, buf, sizeof(u.expires_at) - 1);

    prompt_line(s, "Sysop Note: ", buf, sizeof(buf));
    if (buf[0])
      strncpy(u.note, buf, sizeof(u.note) - 1);

    send_str(s, "\r\nSave changes? (Y/N): ");
    uint8_t confirm[8];
    int r = session_readline(s, confirm, sizeof(confirm), s->cfg.idle_timeout_sec);
    if (r > 0 && (confirm[0] == 'Y' || confirm[0] == 'y'))
    {
      if (db_user_update(s->db, &u))
      {
        send_str(s, "\r\nUser updated successfully.\r\n");
      }
      else
      {
        send_str(s, "\r\nFailed to update user.\r\n");
      }
    }
    else
    {
      send_str(s, "\r\nChanges discarded.\r\n");
    }
    log_audit(s->user.handle, "useredit", handle);
  }
  else if (!strcmp(action, "doors"))
  {
    DbDoor doors[16];
    int dcount = db_doors_list(s->db, doors, 16);
    if (dcount == 0)
    {
      send_str(s, "\r\nNo doors.\r\n");
      return;
    }
    send_str(s, "\r\nDoors:\r\n");
    char buf[256];
    for (int i = 0; i < dcount; i++)
    {
      snprintf(buf, sizeof(buf), "  [%d] %s (%s)\r\n", doors[i].id, doors[i].name, doors[i].command);
      send_str(s, buf);
    }
    send_str(s, "Choose door #: ");
    uint8_t line[32];
    int n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n <= 0)
      return;
    int id = atoi((char *)line);
    for (int i = 0; i < dcount; i++)
      if (doors[i].id == id)
      {
        if (!acs_allows(s, doors[i].acs))
        {
          send_str(s, "\r\nAccess denied.\r\n");
          return;
        }
        snprintf(buf, sizeof(buf), "\r\nEntering door: %s\r\n", doors[i].name);
        send_str(s, buf);
        db_node_upsert(s->db, s->node_num, s->user.id, "online", doors[i].name, s->ip);
        bool ok = door_launch(s, &doors[i]);
        db_node_upsert(s->db, s->node_num, s->user.id, "online", "menu", s->ip);
        send_str(s, ok ? "\r\nReturned from door.\r\n" : "\r\nDoor exited with an error.\r\n");
        return;
      }
  }
  else if (!strcmp(action, "vote"))
  {
    if (HAS_AC_FLAG(&s->user, AC_RVOTING))
    {
      send_str(s, "\r\n\x1b[1;31mYou are restricted from voting.\x1b[0m\r\n");
      return;
    }
    DbVote votes[16];
    int vcount = db_vote_list(s->db, votes, 16);
    if (vcount == 0)
    {
      send_str(s, "\r\nNo open votes.\r\n");
      return;
    }
    send_str(s, "\r\nVote Booth:\r\n");
    char buf[256];
    for (int i = 0; i < vcount; i++)
    {
      snprintf(buf, sizeof(buf), " [%d] %s (closes %s)\r\n", votes[i].id, votes[i].title, votes[i].closes_at);
      send_str(s, buf);
    }
    send_str(s, "Choose vote #: ");
    uint8_t line[32];
    int n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n <= 0)
      return;
    int vid = atoi((char *)line);
    DbVoteChoice choices[16];
    int cc = db_vote_choices(s->db, vid, choices, 16);
    for (int i = 0; i < cc; i++)
    {
      snprintf(buf, sizeof(buf), "  (%d) %s\r\n", choices[i].id, choices[i].label);
      send_str(s, buf);
    }
    send_str(s, "Pick choice #: ");
    n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n <= 0)
      return;
    int cid = atoi((char *)line);
    if (db_vote_cast(s->db, vid, cid, s->user.id))
      send_str(s, "\r\nVote recorded.\r\n");
    else
      send_str(s, "\r\nVote failed.\r\n");
  }
  else if (!strcmp(action, "voteresults"))
  {
    /* VR - View vote results */
    DbVote votes[16];
    int vcount = db_vote_list(s->db, votes, 16);
    if (vcount == 0)
    {
      send_str(s, "\r\nNo votes available.\r\n");
      return;
    }

    send_str(s, "\r\n\x1b[1;36mVote Results\x1b[0m\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");
    char buf[256];
    for (int i = 0; i < vcount; i++)
    {
      snprintf(buf, sizeof(buf), " [%d] %s\r\n", votes[i].id, votes[i].title);
      send_str(s, buf);
    }

    send_str(s, "\r\nView results for vote #: ");
    uint8_t line[32];
    int n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n <= 0)
      return;
    int vid = atoi((char *)line);

    /* Find the vote */
    DbVote *selected = NULL;
    for (int i = 0; i < vcount; i++)
    {
      if (votes[i].id == vid)
      {
        selected = &votes[i];
        break;
      }
    }
    if (!selected)
    {
      send_str(s, "\r\nInvalid vote number.\r\n");
      return;
    }

    send_str(s, "\r\n\x1b[1;33m");
    send_str(s, selected->title);
    send_str(s, "\x1b[0m\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");

    /* Get choices */
    DbVoteChoice choices[16];
    int cc = db_vote_choices(s->db, vid, choices, 16);

    /* Get results */
    int choice_ids[16], counts[16];
    int rc = db_vote_results(s->db, vid, choice_ids, counts, 16);
    int total = db_vote_total(s->db, vid);

    /* Display results with bar graph */
    for (int i = 0; i < cc; i++)
    {
      int cnt = 0;
      for (int j = 0; j < rc; j++)
      {
        if (choice_ids[j] == choices[i].id)
        {
          cnt = counts[j];
          break;
        }
      }
      int pct = total > 0 ? (cnt * 100) / total : 0;
      int bars = pct / 5; /* 20 char bar max */

      char bar[32];
      memset(bar, 0, sizeof(bar));
      for (int b = 0; b < bars && b < 20; b++)
        bar[b] = '#';

      snprintf(buf, sizeof(buf), "  %-30s %3d (%3d%%) %s\r\n",
               choices[i].label, cnt, pct, bar);
      send_str(s, buf);
    }

    snprintf(buf, sizeof(buf), "\r\nTotal votes: %d\r\n", total);
    send_str(s, buf);
    if (selected->closes_at[0])
    {
      snprintf(buf, sizeof(buf), "Closes: %s\r\n", selected->closes_at);
      send_str(s, buf);
    }
  }
  else if (!strcmp(action, "timebank"))
  {
    char buf[64];
    int bal = 0;
    db_timebank_get(s->db, s->user.id, &bal);
    snprintf(buf, sizeof(buf), "\r\nTime Bank balance: %d minutes\r\n", bal);
    send_str(s, buf);
    send_str(s, "(D)eposit session minutes or (W)ithdraw? ");
    uint8_t line[16];
    int n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n <= 0)
      return;
    if (line[0] == 'D' || line[0] == 'd')
    {
      char amt[16] = {0};
      prompt_line(s, "Minutes to deposit: ", amt, sizeof(amt));
      int m = atoi(amt);
      if (m > 0 && s->time_left_min > m)
      {
        s->time_left_min -= m;
        db_timebank_add(s->db, s->user.id, m, &bal);
      }
    }
    else if (line[0] == 'W' || line[0] == 'w')
    {
      char amt[16] = {0};
      prompt_line(s, "Minutes to withdraw: ", amt, sizeof(amt));
      int m = atoi(amt);
      if (db_timebank_add(s->db, s->user.id, -m, &bal))
      {
        s->time_left_min += m;
      }
    }
    db_timebank_get(s->db, s->user.id, &bal);
    snprintf(buf, sizeof(buf), "\r\nNew balance: %d minutes\r\n", bal);
    send_str(s, buf);
  }
  else if (!strcmp(action, "smw"))
  {
    /* Short Message Waiting - read and send short messages */
    send_str(s, "\r\n\x1b[1;36mShort Messages\x1b[0m\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");

    DbShortMessage msgs[20];
    int count = db_smw_list(s->db, s->user.id, msgs, 20);

    if (count == 0)
    {
      send_str(s, "No short messages.\r\n");
    }
    else
    {
      char linebuf[512];
      for (int i = 0; i < count; i++)
      {
        snprintf(linebuf, sizeof(linebuf), "[%d] %sFrom: \x1b[1;32m%s\x1b[0m at %s\r\n     %s\r\n",
                 msgs[i].id,
                 msgs[i].read_flag ? "" : "\x1b[1;33m*NEW* \x1b[0m",
                 msgs[i].from_handle,
                 msgs[i].sent_at,
                 msgs[i].message);
        send_str(s, linebuf);
        db_smw_mark_read(s->db, msgs[i].id);
      }
    }

    send_str(s, "----------------------------------------------------------------------\r\n");
    send_str(s, "(S)end message, (D)elete message, or ENTER to exit: ");
    uint8_t line[16];
    int n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n <= 0)
      return;

    if (line[0] == 'S' || line[0] == 's')
    {
      char to_handle[64] = {0};
      char message[256] = {0};

      prompt_line(s, "\r\nTo user: ", to_handle, sizeof(to_handle));
      if (!to_handle[0])
        return;

      DbUser to_user;
      if (!db_user_fetch(s->db, to_handle, &to_user))
      {
        send_str(s, "\r\nUser not found.\r\n");
        return;
      }

      prompt_line(s, "Message: ", message, sizeof(message));
      if (!message[0])
        return;

      if (db_smw_send(s->db, s->user.id, s->user.handle, to_user.id, to_user.handle, message))
      {
        send_str(s, "\r\nMessage sent!\r\n");
        /* Notify if user is online */
        Session *target = NULL;
        pthread_mutex_lock(&g_online_mu);
        for (int i = 0; i < MAX_ONLINE; i++)
        {
          if (g_online[i] && g_online[i]->user.id == to_user.id)
          {
            target = g_online[i];
            break;
          }
        }
        pthread_mutex_unlock(&g_online_mu);
        if (target)
        {
          char notify[256];
          snprintf(notify, sizeof(notify), "\r\n\a\x1b[1;33m*** Short message from %s ***\x1b[0m\r\n", s->user.handle);
          fd_write_all(target->fd, notify, strlen(notify));
        }
      }
      else
      {
        send_str(s, "\r\nFailed to send message.\r\n");
      }
    }
    else if (line[0] == 'D' || line[0] == 'd')
    {
      char id_str[16] = {0};
      prompt_line(s, "\r\nDelete message ID: ", id_str, sizeof(id_str));
      int msg_id = atoi(id_str);
      if (msg_id > 0)
      {
        if (db_smw_delete(s->db, msg_id))
        {
          send_str(s, "\r\nMessage deleted.\r\n");
        }
        else
        {
          send_str(s, "\r\nFailed to delete message.\r\n");
        }
      }
    }
    /* Clear the user's SMW count after reading */
    db_user_clear_smw(s->db, s->user.id);
  }
  else if (!strcmp(action, "areaadmin"))
  {
    if (!acs_allows(s, "+A"))
    {
      send_str(s, "\r\nAccess denied.\r\n");
      return;
    }
    send_str(s, "\r\nArea Admin: (1) Add (2) Delete (3) Set ACS : ");
    uint8_t line[8];
    int n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n <= 0)
      return;
    if (line[0] == '1')
    {
      char name[64] = {0}, acs[64] = {0};
      prompt_line(s, "Name: ", name, sizeof(name));
      prompt_line(s, "ACS: ", acs, sizeof(acs));
      db_message_area_manage(s->db, name, acs, NULL, false);
    }
    else if (line[0] == '2')
    {
      char name[64] = {0};
      prompt_line(s, "Name to delete: ", name, sizeof(name));
      db_message_area_manage(s->db, name, "", NULL, true);
    }
    else if (line[0] == '3')
    {
      char name[64] = {0}, acs[64] = {0};
      prompt_line(s, "Name: ", name, sizeof(name));
      prompt_line(s, "ACS: ", acs, sizeof(acs));
      db_message_area_manage(s->db, name, acs, NULL, false);
    }
  }
  else if (!strcmp(action, "fileadmin"))
  {
    if (!acs_allows(s, "+A"))
    {
      send_str(s, "\r\nAccess denied.\r\n");
      return;
    }
    send_str(s, "\r\nFile Admin: (1) Add (2) Delete (3) Set ACS : ");
    uint8_t line[8];
    int n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n <= 0)
      return;
    if (line[0] == '1')
    {
      char name[64] = {0}, path[256] = {0}, acs[64] = {0};
      prompt_line(s, "Name: ", name, sizeof(name));
      prompt_line(s, "Path: ", path, sizeof(path));
      prompt_line(s, "ACS: ", acs, sizeof(acs));
      db_file_area_manage(s->db, name, path, acs, NULL, false);
    }
    else if (line[0] == '2')
    {
      char name[64] = {0};
      prompt_line(s, "Name to delete: ", name, sizeof(name));
      db_file_area_manage(s->db, name, "", "", NULL, true);
    }
    else if (line[0] == '3')
    {
      char name[64] = {0}, acs[64] = {0};
      prompt_line(s, "Name: ", name, sizeof(name));
      prompt_line(s, "ACS: ", acs, sizeof(acs));
      db_file_area_manage(s->db, name, "", acs, NULL, false);
    }
  }
  else if (!strcmp(action, "subscriptioneditor"))
  {
    /* Subscription type editor (sysop only) */
    if (!acs_allows(s, "+A"))
    {
      send_str(s, "\r\nAccess denied.\r\n");
      return;
    }

    while (1)
    {
      send_str(s, "\r\n\x1b[1;36mSubscription Type Editor\x1b[0m\r\n");
      send_str(s, "----------------------------------------------------------------------\r\n");

      DbSubscriptionType types[16];
      int cnt = db_subscription_type_list(s->db, types, 16);

      char buf[256];
      send_str(s, " ID  Name                 Days  Level  Expired  Price\r\n");
      send_str(s, "----------------------------------------------------------------------\r\n");
      for (int i = 0; i < cnt; i++)
      {
        snprintf(buf, sizeof(buf), "%3d  %-20s %4d  %5d  %7d  %5d\r\n",
                 types[i].id, types[i].name, types[i].days,
                 types[i].security_level_id, types[i].expired_level_id, types[i].price);
        send_str(s, buf);
      }

      send_str(s, "\r\n(A)dd, (Q)uit: ");
      uint8_t line[16];
      int n = session_readline(s, line, sizeof(line), 30);
      if (n <= 0 || line[0] == 'Q' || line[0] == 'q')
        break;

      if (line[0] == 'A' || line[0] == 'a')
      {
        char name[64] = {0}, days_str[16] = {0}, level_str[16] = {0};
        char expired_str[16] = {0}, price_str[16] = {0}, desc[256] = {0};

        prompt_line(s, "Subscription name: ", name, sizeof(name));
        if (!name[0])
          continue;
        prompt_line(s, "Duration (days): ", days_str, sizeof(days_str));
        prompt_line(s, "Security level ID while active: ", level_str, sizeof(level_str));
        prompt_line(s, "Security level ID after expiry: ", expired_str, sizeof(expired_str));
        prompt_line(s, "Price (credits): ", price_str, sizeof(price_str));
        prompt_line(s, "Description: ", desc, sizeof(desc));

        int days = atoi(days_str);
        int level = atoi(level_str);
        int expired = atoi(expired_str);
        int price = atoi(price_str);

        if (days > 0 && level > 0)
        {
          if (db_subscription_type_add(s->db, name, days, level, expired > 0 ? expired : 1, price, desc))
          {
            send_str(s, "\r\nSubscription type added.\r\n");
          }
          else
          {
            send_str(s, "\r\nFailed to add subscription type.\r\n");
          }
        }
      }
    }
  }
  else if (!strcmp(action, "subscribe"))
  {
    /* User subscription purchase */
    send_str(s, "\r\n\x1b[1;36mSubscription Plans\x1b[0m\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");

    DbSubscriptionType types[16];
    int cnt = db_subscription_type_list(s->db, types, 16);
    if (cnt == 0)
    {
      send_str(s, "No subscription plans available.\r\n");
      return;
    }

    char buf[256];
    for (int i = 0; i < cnt; i++)
    {
      snprintf(buf, sizeof(buf), "[%d] %s - %d days - %d credits\r\n    %s\r\n",
               types[i].id, types[i].name, types[i].days, types[i].price, types[i].description);
      send_str(s, buf);
    }

    snprintf(buf, sizeof(buf), "\r\nYour credits: %d\r\n", s->credits);
    send_str(s, buf);

    send_str(s, "\r\nSelect plan # (0 to cancel): ");
    uint8_t line[16];
    int n = session_readline(s, line, sizeof(line), 30);
    if (n <= 0)
      return;
    int type_id = atoi((char *)line);
    if (type_id <= 0)
      return;

    DbSubscriptionType selected;
    if (!db_subscription_type_get(s->db, type_id, &selected))
    {
      send_str(s, "\r\nInvalid plan.\r\n");
      return;
    }

    if (s->credits < selected.price)
    {
      send_str(s, "\r\nInsufficient credits.\r\n");
      return;
    }

    snprintf(buf, sizeof(buf), "\r\nSubscribe to '%s' for %d credits? (Y/N): ", selected.name, selected.price);
    send_str(s, buf);
    n = session_readline(s, line, sizeof(line), 30);
    if (n > 0 && (line[0] == 'Y' || line[0] == 'y'))
    {
      s->credits -= selected.price;
      s->user.credits = s->credits;
      db_user_update_time_credit(s->db, s->user.id, s->user.time_limit_min, s->credits, s->file_points);

      if (db_user_subscribe(s->db, s->user.id, type_id))
      {
        s->user.security_level_id = selected.security_level_id;
        send_str(s, "\r\nSubscription activated! Your access level has been upgraded.\r\n");
      }
      else
      {
        send_str(s, "\r\nFailed to activate subscription.\r\n");
      }
    }
  }
  else if (!strcmp(action, "setsecurityq"))
  {
    /* Set security question for password recovery */
    send_str(s, "\r\n\x1b[1;36mSet Security Question\x1b[0m\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");
    send_str(s, "This question will be used to recover your password if you forget it.\r\n\r\n");

    char question[128] = {0};
    prompt_line(s, "Security Question: ", question, sizeof(question));
    if (!question[0])
    {
      send_str(s, "\r\nCancelled.\r\n");
      return;
    }

    char answer[64] = {0};
    prompt_line(s, "Answer: ", answer, sizeof(answer));
    if (!answer[0])
    {
      send_str(s, "\r\nCancelled.\r\n");
      return;
    }

    char confirm[64] = {0};
    prompt_line(s, "Confirm Answer: ", confirm, sizeof(confirm));
    if (strcmp(answer, confirm) != 0)
    {
      send_str(s, "\r\nAnswers do not match.\r\n");
      return;
    }

    char answer_hash[256];
    if (pw_hash_make(answer, answer_hash, sizeof(answer_hash)))
    {
      if (db_user_set_security_question(s->db, s->user.id, question, answer_hash))
      {
        send_str(s, "\r\nSecurity question set successfully.\r\n");
      }
      else
      {
        send_str(s, "\r\nFailed to set security question.\r\n");
      }
    }
  }
  else if (!strcmp(action, "setsignature"))
  {
    send_str(s, "\r\n\x1b[1;36mSet Message Signature\x1b[0m\r\n");
    if (s->user.signature[0]) {
      char buf[512];
      snprintf(buf, sizeof(buf), "Current: %s\r\n", s->user.signature);
      send_str(s, buf);
    }
    char sig[256] = {0};
    prompt_line(s, "Signature (blank=clear): ", sig, sizeof(sig));
    int use_sig = sig[0] ? 1 : 0;
    if (db_user_set_signature(s->db, s->user.id, sig, use_sig)) {
      snprintf(s->user.signature, sizeof(s->user.signature), "%s", sig);
      s->user.use_signature = use_sig;
      send_str(s, use_sig ? "\r\nSignature set.\r\n" : "\r\nSignature cleared.\r\n");
    } else {
      send_str(s, "\r\nFailed to save signature.\r\n");
    }
  }
  else if (!strcmp(action, "settagline"))
  {
    send_str(s, "\r\n\x1b[1;36mSet Tagline\x1b[0m\r\n");
    if (s->user.tagline[0]) {
      char buf[256];
      snprintf(buf, sizeof(buf), "Current: %s\r\n", s->user.tagline);
      send_str(s, buf);
    }
    char tag[128] = {0};
    prompt_line(s, "Tagline (blank=clear): ", tag, sizeof(tag));
    int use_tag = tag[0] ? 1 : 0;
    if (db_user_set_tagline(s->db, s->user.id, tag, use_tag)) {
      snprintf(s->user.tagline, sizeof(s->user.tagline), "%s", tag);
      s->user.use_tagline = use_tag;
      send_str(s, use_tag ? "\r\nTagline set.\r\n" : "\r\nTagline cleared.\r\n");
    } else {
      send_str(s, "\r\nFailed to save tagline.\r\n");
    }
  }
  else if (!strcmp(action, "togglefse"))
  {
    s->user.use_fse = s->user.use_fse ? 0 : 1;
    if (db_user_set_use_fse(s->db, s->user.id, s->user.use_fse))
      send_str(s, s->user.use_fse ? "\r\nFull-screen editor enabled.\r\n"
                                   : "\r\nFull-screen editor disabled.\r\n");
    else
      send_str(s, "\r\nFailed to save preference.\r\n");
  }
  else if (!strcmp(action, "pickscheme"))
  {
    send_str(s, "\r\n\x1b[1;32mColor Scheme Selection\x1b[0m\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");
    char preview[256];
    for (int i = 0; i < MCI_NUM_COLOR_SCHEMES; i++)
    {
      mci_scheme_preview(i == s->user.color_scheme ? "* " : "  ", i, preview, sizeof(preview));
      send_str(s, preview);
      send_str(s, "\r\n");
    }
    send_str(s, "\r\nEnter scheme number (0-7, ENTER to cancel): ");
    uint8_t line[8] = {0};
    int n = session_readline(s, line, sizeof(line), 30);
    if (n > 0 && line[0] >= '0' && line[0] <= '7')
    {
      int new_scheme = line[0] - '0';
      s->user.color_scheme = new_scheme;
      if (db_user_update(s->db, &s->user))
      {
        char buf[64];
        snprintf(buf, sizeof(buf), "\r\nScheme set to %s.\r\n",
                 mci_scheme_name(new_scheme));
        send_str(s, buf);
      }
      else
      {
        send_str(s, "\r\nFailed to save scheme.\r\n");
      }
    }
  }
  else if (!strcmp(action, "confeditor"))
  {
    /* *R - Conference Editor */
    if (!acs_allows(s, "+A"))
    {
      send_str(s, "\r\nAccess denied.\r\n");
      return;
    }

    while (1)
    {
      send_str(s, "\r\n\x1b[1;36mConference Editor\x1b[0m\r\n");
      send_str(s, "----------------------------------------------------------------------\r\n");

      DbConference confs[32];
      int cnt = db_conference_list(s->db, confs, 32);

      char buf[256];
      send_str(s, " ID  Key        Name                           ACS\r\n");
      send_str(s, "----------------------------------------------------------------------\r\n");
      for (int i = 0; i < cnt; i++)
      {
        snprintf(buf, sizeof(buf), "%3d  %-10s %-30s %s\r\n",
                 confs[i].id, confs[i].key, confs[i].name, confs[i].acs);
        send_str(s, buf);
      }

      send_str(s, "\r\n(A)dd, (E)dit, (D)elete, (Q)uit: ");
      uint8_t line[16];
      int n = session_readline(s, line, sizeof(line), 30);
      if (n <= 0 || line[0] == 'Q' || line[0] == 'q')
        break;

      if (line[0] == 'A' || line[0] == 'a')
      {
        char key[32] = {0}, name[64] = {0}, desc[256] = {0}, acs[64] = {0};
        prompt_line(s, "Conference key (short): ", key, sizeof(key));
        if (!key[0])
          continue;
        prompt_line(s, "Conference name: ", name, sizeof(name));
        if (!name[0])
          continue;
        prompt_line(s, "Description: ", desc, sizeof(desc));
        prompt_line(s, "ACS (blank for all): ", acs, sizeof(acs));

        if (db_conference_add(s->db, key, name, desc, acs))
        {
          send_str(s, "\r\nConference added.\r\n");
        }
        else
        {
          send_str(s, "\r\nFailed to add conference.\r\n");
        }
      }
      else if (line[0] == 'E' || line[0] == 'e')
      {
        char id_str[16] = {0};
        prompt_line(s, "Conference ID to edit: ", id_str, sizeof(id_str));
        int id = atoi(id_str);
        if (id <= 0)
          continue;

        DbConference conf;
        if (!db_conference_get(s->db, id, &conf))
        {
          send_str(s, "\r\nConference not found.\r\n");
          continue;
        }

        snprintf(buf, sizeof(buf), "\r\nEditing: %s\r\n", conf.name);
        send_str(s, buf);

        char name[64], desc[256], acs[64];
        snprintf(buf, sizeof(buf), "Name [%s]: ", conf.name);
        prompt_line(s, buf, name, sizeof(name));
        if (!name[0])
          snprintf(name, sizeof(name), "%s", conf.name);

        snprintf(buf, sizeof(buf), "Description [%.50s]: ", conf.description);
        prompt_line(s, buf, desc, sizeof(desc));
        if (!desc[0])
          snprintf(desc, sizeof(desc), "%s", conf.description);

        snprintf(buf, sizeof(buf), "ACS [%s]: ", conf.acs);
        prompt_line(s, buf, acs, sizeof(acs));
        if (!acs[0])
          snprintf(acs, sizeof(acs), "%s", conf.acs);

        if (db_conference_update(s->db, id, name, desc, acs, conf.flags))
        {
          send_str(s, "\r\nConference updated.\r\n");
        }
        else
        {
          send_str(s, "\r\nFailed to update conference.\r\n");
        }
      }
      else if (line[0] == 'D' || line[0] == 'd')
      {
        char id_str[16] = {0};
        prompt_line(s, "Conference ID to delete: ", id_str, sizeof(id_str));
        int id = atoi(id_str);
        if (id <= 0)
          continue;

        send_str(s, "Are you sure? This will remove all memberships. (Y/N): ");
        n = session_readline(s, line, sizeof(line), 30);
        if (n > 0 && (line[0] == 'Y' || line[0] == 'y'))
        {
          if (db_conference_delete(s->db, id))
          {
            send_str(s, "\r\nConference deleted.\r\n");
          }
          else
          {
            send_str(s, "\r\nFailed to delete conference.\r\n");
          }
        }
      }
    }
  }
  else if (!strcmp(action, "protocoleditor"))
  {
    /* *X - Protocol Editor */
    if (!acs_allows(s, "+A"))
    {
      send_str(s, "\r\nAccess denied.\r\n");
      return;
    }

    while (1)
    {
      send_str(s, "\r\n\x1b[1;36mProtocol Editor\x1b[0m\r\n");
      send_str(s, "----------------------------------------------------------------------\r\n");

      DbProtocol protos[32];
      int cnt = db_protocols_list(s->db, protos, 32, NULL);

      char buf[256];
      send_str(s, " ID  Name              Direction  Active  Command\r\n");
      send_str(s, "----------------------------------------------------------------------\r\n");
      for (int i = 0; i < cnt; i++)
      {
        snprintf(buf, sizeof(buf), "%3d  %-16s  %-9s  %-6s  %.40s\r\n",
                 protos[i].id, protos[i].name, protos[i].direction,
                 protos[i].active ? "Yes" : "No", protos[i].command);
        send_str(s, buf);
      }

      send_str(s, "\r\n(A)dd, (E)dit, (D)elete, (Q)uit: ");
      uint8_t line[16];
      int n = session_readline(s, line, sizeof(line), 30);
      if (n <= 0 || line[0] == 'Q' || line[0] == 'q')
        break;

      if (line[0] == 'A' || line[0] == 'a')
      {
        char name[32] = {0}, dir[16] = {0}, cmd[256] = {0};
        prompt_line(s, "Protocol name: ", name, sizeof(name));
        if (!name[0])
          continue;
        prompt_line(s, "Direction (up/down/both): ", dir, sizeof(dir));
        if (!dir[0])
          continue;
        prompt_line(s, "Command: ", cmd, sizeof(cmd));
        if (!cmd[0])
          continue;

        if (db_protocol_add(s->db, name, dir, cmd))
        {
          send_str(s, "\r\nProtocol added.\r\n");
        }
        else
        {
          send_str(s, "\r\nFailed to add protocol.\r\n");
        }
      }
      else if (line[0] == 'E' || line[0] == 'e')
      {
        char id_str[16] = {0};
        prompt_line(s, "Protocol ID to edit: ", id_str, sizeof(id_str));
        int id = atoi(id_str);
        if (id <= 0)
          continue;

        DbProtocol proto;
        if (!db_protocol_get(s->db, id, &proto))
        {
          send_str(s, "\r\nProtocol not found.\r\n");
          continue;
        }

        snprintf(buf, sizeof(buf), "\r\nEditing: %s\r\n", proto.name);
        send_str(s, buf);

        char name[32], dir[16], cmd[256], active[8];
        snprintf(buf, sizeof(buf), "Name [%s]: ", proto.name);
        prompt_line(s, buf, name, sizeof(name));
        if (!name[0])
          snprintf(name, sizeof(name), "%s", proto.name);

        snprintf(buf, sizeof(buf), "Direction [%s]: ", proto.direction);
        prompt_line(s, buf, dir, sizeof(dir));
        if (!dir[0])
          snprintf(dir, sizeof(dir), "%s", proto.direction);

        snprintf(buf, sizeof(buf), "Command [%.50s]: ", proto.command);
        prompt_line(s, buf, cmd, sizeof(cmd));
        if (!cmd[0])
          snprintf(cmd, sizeof(cmd), "%s", proto.command);

        snprintf(buf, sizeof(buf), "Active (Y/N) [%s]: ", proto.active ? "Y" : "N");
        prompt_line(s, buf, active, sizeof(active));
        int act = proto.active;
        if (active[0] == 'Y' || active[0] == 'y')
          act = 1;
        else if (active[0] == 'N' || active[0] == 'n')
          act = 0;

        if (db_protocol_update(s->db, id, name, dir, cmd, act))
        {
          send_str(s, "\r\nProtocol updated.\r\n");
        }
        else
        {
          send_str(s, "\r\nFailed to update protocol.\r\n");
        }
      }
      else if (line[0] == 'D' || line[0] == 'd')
      {
        char id_str[16] = {0};
        prompt_line(s, "Protocol ID to delete: ", id_str, sizeof(id_str));
        int id = atoi(id_str);
        if (id <= 0)
          continue;

        send_str(s, "Are you sure? (Y/N): ");
        n = session_readline(s, line, sizeof(line), 30);
        if (n > 0 && (line[0] == 'Y' || line[0] == 'y'))
        {
          if (db_protocol_delete(s->db, id))
          {
            send_str(s, "\r\nProtocol deleted.\r\n");
          }
          else
          {
            send_str(s, "\r\nFailed to delete protocol.\r\n");
          }
        }
      }
    }
  }
  else if (!strcmp(action, "menueditor"))
  {
    if (!acs_allows(s, "+A"))
    {
      send_str(s, "\r\nAccess denied.\r\n");
      return;
    }

    send_str(s, "\r\n\x1b[1;36mMenu Editor\x1b[0m\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");

    /* List menu files */
    DIR *dir = opendir("menus");
    if (!dir)
    {
      send_str(s, "Cannot open menus directory.\r\n");
      return;
    }

    char menu_files[32][64];
    int menu_count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && menu_count < 32)
    {
      if (strstr(ent->d_name, ".mnu"))
      {
        snprintf(menu_files[menu_count], sizeof(menu_files[0]), "%s", ent->d_name);
        menu_count++;
      }
    }
    closedir(dir);

    char buf[256];
    for (int i = 0; i < menu_count; i++)
    {
      snprintf(buf, sizeof(buf), "  [%2d] %s\r\n", i + 1, menu_files[i]);
      send_str(s, buf);
    }

    send_str(s, "\r\n(E)dit menu, (N)ew menu, (D)elete menu, (Q)uit: ");
    uint8_t line[16];
    int n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n <= 0 || line[0] == 'Q' || line[0] == 'q')
      return;

    if (line[0] == 'N' || line[0] == 'n')
    {
      /* Create new menu */
      char name[64] = {0};
      prompt_line(s, "\r\nNew menu filename (without .mnu): ", name, sizeof(name));
      if (!name[0])
        return;
      if (!valid_menu_basename(name))
      {
        send_str(s, "\r\nMenu names may contain only letters, numbers, underscores, and dashes.\r\n");
        return;
      }

      char filepath[256];
      snprintf(filepath, sizeof(filepath), "menus/%s.mnu", name);
      if (access(filepath, F_OK) == 0)
      {
        send_str(s, "\r\nMenu file already exists.\r\n");
        return;
      }

      Menu menu;
      memset(&menu, 0, sizeof(menu));
      snprintf(menu.name, sizeof(menu.name), "%s", name);
      snprintf(menu.title, sizeof(menu.title), "%s Menu", name);
      snprintf(menu.prompt, sizeof(menu.prompt), "Selection: ");
      menu.flags = MENU_FLAG_HOTKEYS;
      menu.count = 1;
      menu.items = calloc(1, sizeof(MenuItem));
      if (!menu.items)
      {
        send_str(s, "\r\nFailed to create menu file.\r\n");
        return;
      }
      menu.items[0].key = 'Q';
      snprintf(menu.items[0].label, sizeof(menu.items[0].label), "Quit");
      snprintf(menu.items[0].action, sizeof(menu.items[0].action), "goto");
      snprintf(menu.items[0].data, sizeof(menu.items[0].data), "main");

      bool saved = menu_save(filepath, &menu);
      menu_free(&menu);
      if (!saved)
      {
        send_str(s, "\r\nFailed to create menu file.\r\n");
        return;
      }

      snprintf(buf, sizeof(buf), "\r\nCreated menu: %s\r\n", filepath);
      send_str(s, buf);
      return;
    }

    if (line[0] == 'D' || line[0] == 'd')
    {
      char sel[8] = {0};
      prompt_line(s, "\r\nMenu number to delete: ", sel, sizeof(sel));
      int idx = atoi(sel) - 1;
      if (idx < 0 || idx >= menu_count)
      {
        send_str(s, "\r\nInvalid selection.\r\n");
        return;
      }

      char filepath[256];
      snprintf(filepath, sizeof(filepath), "menus/%s", menu_files[idx]);
      char confirm[8] = {0};
      prompt_line(s, "Type YES to delete this menu: ", confirm, sizeof(confirm));
      if (strcmp(confirm, "YES") != 0)
      {
        send_str(s, "\r\nDelete cancelled.\r\n");
        return;
      }
      if (menu_delete_file(filepath))
      {
        send_str(s, "\r\nMenu deleted.\r\n");
      }
      else
      {
        send_str(s, "\r\nFailed to delete menu.\r\n");
      }
      return;
    }

    if (line[0] == 'E' || line[0] == 'e')
    {
      char sel[8] = {0};
      prompt_line(s, "\r\nMenu number to edit: ", sel, sizeof(sel));
      int idx = atoi(sel) - 1;
      if (idx < 0 || idx >= menu_count)
      {
        send_str(s, "\r\nInvalid selection.\r\n");
        return;
      }

      char filepath[256];
      snprintf(filepath, sizeof(filepath), "menus/%s", menu_files[idx]);

      /* Load and display menu */
      Menu menu;
      if (!menu_load(filepath, &menu))
      {
        send_str(s, "\r\nFailed to load menu.\r\n");
        return;
      }

      snprintf(buf, sizeof(buf), "\r\nEditing: %s\r\n", filepath);
      send_str(s, buf);
      snprintf(buf, sizeof(buf), "Title: %s\r\n", menu.title);
      send_str(s, buf);
      snprintf(buf, sizeof(buf), "Prompt: %s\r\n", menu.prompt);
      send_str(s, buf);
      snprintf(buf, sizeof(buf), "Flags: 0x%08X\r\n", menu.flags);
      send_str(s, buf);
      snprintf(buf, sizeof(buf), "Items: %zu\r\n", menu.count);
      send_str(s, buf);

      send_str(s, "\r\nMenu Items:\r\n");
      for (size_t i = 0; i < menu.count && i < 30; i++)
      {
        char key_disp[16];
        if (menu.items[i].key != '\0')
        {
          snprintf(key_disp, sizeof(key_disp), "%c", menu.items[i].key);
        }
        else
        {
          snprintf(key_disp, sizeof(key_disp), "%s", menu.items[i].key_str);
        }
        snprintf(buf, sizeof(buf), "  [%2zu] %s | %s | %s | %s\r\n",
                 i + 1, key_disp, menu.items[i].label, menu.items[i].action, menu.items[i].acs);
        send_str(s, buf);
      }

      bool dirty = false;
      bool done = false;
      while (!done)
      {
        send_str(s, "\r\n(T)itle, (P)rompt, (A)dd item, (D)elete item, (S)ave, (Q)uit: ");
        n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
        if (n <= 0)
          break;

        if (line[0] == 'T' || line[0] == 't')
        {
          char new_title[120] = {0};
          prompt_line(s, "New title: ", new_title, sizeof(new_title));
          if (new_title[0])
          {
            snprintf(menu.title, sizeof(menu.title), "%s", new_title);
            dirty = true;
          }
        }
        else if (line[0] == 'P' || line[0] == 'p')
        {
          char new_prompt[120] = {0};
          prompt_line(s, "New prompt: ", new_prompt, sizeof(new_prompt));
          if (new_prompt[0])
          {
            snprintf(menu.prompt, sizeof(menu.prompt), "%s", new_prompt);
            dirty = true;
          }
        }
        else if (line[0] == 'A' || line[0] == 'a')
        {
          char key[16] = {0}, label[64] = {0}, action_name[64] = {0}, data[128] = {0}, acs[64] = {0};
          prompt_line(s, "Key: ", key, sizeof(key));
          prompt_line(s, "Label: ", label, sizeof(label));
          prompt_line(s, "Action: ", action_name, sizeof(action_name));
          prompt_line(s, "Data: ", data, sizeof(data));
          prompt_line(s, "ACS: ", acs, sizeof(acs));

          if (key[0] && label[0] && action_name[0])
          {
            MenuItem *items = realloc(menu.items, (menu.count + 1) * sizeof(MenuItem));
            if (!items)
            {
              send_str(s, "\r\nFailed to add item.\r\n");
              continue;
            }
            menu.items = items;
            MenuItem *it = &menu.items[menu.count];
            memset(it, 0, sizeof(*it));
            if (strlen(key) > 1)
            {
              snprintf(it->key_str, sizeof(it->key_str), "%s", key);
              for (char *p = it->key_str; *p; p++)
                *p = (char)toupper((unsigned char)*p);
            }
            else
            {
              it->key = (char)toupper((unsigned char)key[0]);
            }
            snprintf(it->label, sizeof(it->label), "%s", label);
            snprintf(it->action, sizeof(it->action), "%s", action_name);
            snprintf(it->data, sizeof(it->data), "%s", data);
            snprintf(it->acs, sizeof(it->acs), "%s", acs);
            menu.count++;
            dirty = true;
            send_str(s, "\r\nItem added.\r\n");
          }
        }
        else if (line[0] == 'D' || line[0] == 'd')
        {
          char key[16] = {0};
          prompt_line(s, "Delete item key: ", key, sizeof(key));
          if (key[0] == '\0')
          {
            send_str(s, "\r\nDelete cancelled.\r\n");
          }
          else
          {
            bool found = false;
            size_t remove_index = 0;
            bool multi = strlen(key) > 1;
            for (size_t i = 0; i < menu.count; i++)
            {
              if (multi)
              {
                if (menu.items[i].key_str[0] && strcasecmp(menu.items[i].key_str, key) == 0)
                {
                  found = true;
                  remove_index = i;
                  break;
                }
              }
              else
              {
                char k = (char)toupper((unsigned char)key[0]);
                if (menu.items[i].key == k)
                {
                  found = true;
                  remove_index = i;
                  break;
                }
              }
            }

            if (found)
            {
              for (size_t j = remove_index + 1; j < menu.count; j++)
              {
                menu.items[j - 1] = menu.items[j];
              }
              menu.count--;
              dirty = true;
              send_str(s, "\r\nItem deleted.\r\n");
            }
            else
            {
              send_str(s, "\r\nItem not found.\r\n");
            }
          }
        }
        else if (line[0] == 'S' || line[0] == 's')
        {
          if (menu_save(filepath, &menu))
          {
            dirty = false;
            send_str(s, "\r\nMenu saved.\r\n");
          }
          else
          {
            send_str(s, "\r\nFailed to save menu.\r\n");
          }
        }
        else if (line[0] == 'Q' || line[0] == 'q')
        {
          if (dirty)
            send_str(s, "\r\nUnsaved changes discarded.\r\n");
          done = true;
        }
      }

      menu_free(&menu);
    }
  }
  else if (!strcmp(action, "validatefiles"))
  {
    /* *7 - Validate Files: approve/reject files with FILE_FLAG_NOTVAL */
    if (!acs_allows(s, "+A"))
    {
      send_str(s, "\r\nAccess denied.\r\n");
      return;
    }

    DbFileRec files[64];
    /* Scan all file areas for unvalidated files */
    DbFileArea areas[32];
    int area_cnt = db_file_area_list(s->db, areas, 32);
    int pending = 0;

    for (int ai = 0; ai < area_cnt; ai++)
    {
      DbFileRec area_files[64];
      int cnt = db_file_list(&areas[ai], s->db, area_files, 64);
      for (int i = 0; i < cnt && pending < 64; i++)
      {
        if (area_files[i].flags & FILE_FLAG_NOTVAL)
          files[pending++] = area_files[i];
      }
    }

    if (pending == 0)
    {
      send_str(s, "\r\nNo unvalidated files.\r\n");
      return;
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "\r\n\x1b[1;36mValidate Files\x1b[0m — %d pending\r\n", pending);
    send_str(s, buf);
    send_str(s, "----------------------------------------------------------------------\r\n");

    for (int i = 0; i < pending; i++)
    {
      snprintf(buf, sizeof(buf), "\r\n[%d/%d] %s (%d bytes)\r\n  Desc: %s\r\n  Uploader: %s\r\n",
               i + 1, pending,
               files[i].filename, files[i].size_bytes,
               files[i].desc, files[i].uploader);
      send_str(s, buf);
      send_str(s, "(A)pprove, (R)eject/delete, (S)kip: ");

      uint8_t ch[4] = {0};
      int n = session_readline(s, ch, sizeof(ch), 30);
      if (n <= 0) break;

      if (ch[0] == 'A' || ch[0] == 'a')
      {
        files[i].flags &= ~FILE_FLAG_NOTVAL;
        if (db_file_update(s->db, &files[i]))
          send_str(s, "Approved.\r\n");
        else
          send_str(s, "Update failed.\r\n");
      }
      else if (ch[0] == 'R' || ch[0] == 'r')
      {
        if (db_file_delete(s->db, files[i].id))
          send_str(s, "Rejected and deleted.\r\n");
        else
          send_str(s, "Delete failed.\r\n");
      }
      else
      {
        send_str(s, "Skipped.\r\n");
      }
    }
    send_str(s, "\r\nFile validation complete.\r\n");
  }
  else if (!strcmp(action, "voteeditor"))
  {
    /* *V - Vote Editor */
    if (!acs_allows(s, "+A"))
    {
      send_str(s, "\r\nAccess denied.\r\n");
      return;
    }

    while (1)
    {
      send_str(s, "\r\n\x1b[1;36mVote Editor\x1b[0m\r\n");
      send_str(s, "----------------------------------------------------------------------\r\n");

      DbVote votes[32];
      int cnt = db_vote_list(s->db, votes, 32);
      char buf[256];
      send_str(s, " ID  Title                          Closes\r\n");
      send_str(s, "----------------------------------------------------------------------\r\n");
      for (int i = 0; i < cnt; i++)
      {
        snprintf(buf, sizeof(buf), "%3d  %-30.30s  %s\r\n",
                 votes[i].id, votes[i].title, votes[i].closes_at);
        send_str(s, buf);
      }

      send_str(s, "\r\n(A)dd vote, (D)elete, (C)hoices, (Q)uit: ");
      uint8_t line[64];
      int n = session_readline(s, line, sizeof(line), 30);
      if (n <= 0 || line[0] == 'Q' || line[0] == 'q') break;

      if (line[0] == 'A' || line[0] == 'a')
      {
        char title[128] = {0}, closes[32] = {0};
        prompt_line(s, "Vote question: ", title, sizeof(title));
        if (!title[0]) continue;
        prompt_line(s, "Closes at (YYYY-MM-DD, blank=never): ", closes, sizeof(closes));
        if (!db_vote_add(s->db, title, closes[0] ? closes : NULL))
        {
          send_str(s, "\r\nFailed to add vote.\r\n");
          continue;
        }
        /* Get new vote ID to add choices */
        DbVote new_votes[32];
        int new_cnt = db_vote_list(s->db, new_votes, 32);
        int new_id = new_cnt > 0 ? new_votes[new_cnt - 1].id : -1;
        if (new_id > 0)
        {
          send_str(s, "Add choices (blank line to stop):\r\n");
          for (int ci = 0; ci < 16; ci++)
          {
            char choice[128] = {0};
            snprintf(buf, sizeof(buf), "Choice %d: ", ci + 1);
            prompt_line(s, buf, choice, sizeof(choice));
            if (!choice[0]) break;
            db_vote_choice_add(s->db, new_id, choice);
          }
        }
        send_str(s, "Vote added.\r\n");
      }
      else if (line[0] == 'D' || line[0] == 'd')
      {
        char id_str[16] = {0};
        prompt_line(s, "Vote ID to delete: ", id_str, sizeof(id_str));
        int id = atoi(id_str);
        if (id <= 0) continue;
        send_str(s, "Delete vote and all choices/ballots? (Y/N): ");
        n = session_readline(s, line, sizeof(line), 30);
        if (n > 0 && (line[0] == 'Y' || line[0] == 'y'))
        {
          if (db_vote_delete(s->db, id))
            send_str(s, "Vote deleted.\r\n");
          else
            send_str(s, "Delete failed.\r\n");
        }
      }
      else if (line[0] == 'C' || line[0] == 'c')
      {
        char id_str[16] = {0};
        prompt_line(s, "Vote ID to view choices: ", id_str, sizeof(id_str));
        int id = atoi(id_str);
        if (id <= 0) continue;
        DbVoteChoice choices[32];
        int cc = db_vote_choices(s->db, id, choices, 32);
        for (int i = 0; i < cc; i++)
        {
          snprintf(buf, sizeof(buf), "  %3d  %s\r\n", choices[i].id, choices[i].label);
          send_str(s, buf);
        }
        send_str(s, "(A)dd choice, any other key to continue: ");
        n = session_readline(s, line, sizeof(line), 30);
        if (line[0] == 'A' || line[0] == 'a')
        {
          char choice[128] = {0};
          prompt_line(s, "New choice label: ", choice, sizeof(choice));
          if (choice[0] && db_vote_choice_add(s->db, id, choice))
            send_str(s, "Choice added.\r\n");
        }
      }
    }
  }
  else if (!strcmp(action, "eventeditor"))
  {
    /* *E - Event Editor */
    if (!acs_allows(s, "+A"))
    {
      send_str(s, "\r\nAccess denied.\r\n");
      return;
    }

    while (1)
    {
      send_str(s, "\r\n\x1b[1;36mEvent Editor\x1b[0m\r\n");
      send_str(s, "----------------------------------------------------------------------\r\n");

      DbEvent events[32];
      int cnt = db_events_list(s->db, events, 32);
      char buf[256];
      send_str(s, " ID  En  Type       Schedule              Name\r\n");
      send_str(s, "----------------------------------------------------------------------\r\n");
      for (int i = 0; i < cnt; i++)
      {
        snprintf(buf, sizeof(buf), "%3d  %-3s %-10s %-22s %s\r\n",
                 events[i].id,
                 events[i].enabled ? "Yes" : "No",
                 events[i].event_type,
                 events[i].schedule,
                 events[i].name);
        send_str(s, buf);
      }

      send_str(s, "\r\n(A)dd, (D)elete, (T)oggle enable, (Q)uit: ");
      uint8_t line[64];
      int n = session_readline(s, line, sizeof(line), 30);
      if (n <= 0 || line[0] == 'Q' || line[0] == 'q') break;

      if (line[0] == 'A' || line[0] == 'a')
      {
        char name[64] = {0}, sched[64] = {0}, cmd[256] = {0};
        char etype[32] = {0}, acs[64] = {0};
        prompt_line(s, "Event name: ", name, sizeof(name));
        if (!name[0]) continue;
        prompt_line(s, "Schedule (e.g. daily, weekly, logon): ", sched, sizeof(sched));
        if (!sched[0]) continue;
        prompt_line(s, "Command: ", cmd, sizeof(cmd));
        if (!cmd[0]) continue;
        prompt_line(s, "Type (scheduled/logon/permission): ", etype, sizeof(etype));
        if (!etype[0]) snprintf(etype, sizeof(etype), "scheduled");
        prompt_line(s, "ACS (blank for all): ", acs, sizeof(acs));
        if (db_event_add(s->db, name, sched, cmd, etype, acs))
          send_str(s, "Event added.\r\n");
        else
          send_str(s, "Failed to add event.\r\n");
      }
      else if (line[0] == 'D' || line[0] == 'd')
      {
        char id_str[16] = {0};
        prompt_line(s, "Event ID to delete: ", id_str, sizeof(id_str));
        int id = atoi(id_str);
        if (id <= 0) continue;
        if (db_event_delete(s->db, id))
          send_str(s, "Event deleted.\r\n");
        else
          send_str(s, "Delete failed.\r\n");
      }
      else if (line[0] == 'T' || line[0] == 't')
      {
        char id_str[16] = {0};
        prompt_line(s, "Event ID to toggle: ", id_str, sizeof(id_str));
        int id = atoi(id_str);
        if (id <= 0) continue;
        /* Find current state */
        int current = 0;
        for (int i = 0; i < cnt; i++)
        {
          if (events[i].id == id) { current = events[i].enabled; break; }
        }
        if (db_event_toggle(s->db, id, !current))
        {
          snprintf(buf, sizeof(buf), "Event %s.\r\n", current ? "disabled" : "enabled");
          send_str(s, buf);
        }
        else
        {
          send_str(s, "Toggle failed.\r\n");
        }
      }
    }
  }
  else if (!strcmp(action, "netmail"))
  {
    if (HAS_AC_FLAG(&s->user, AC_REMAIL))
    {
      send_str(s, "\r\n\x1b[1;31mYou are restricted from email.\x1b[0m\r\n");
      return;
    }

    send_str(s, "\r\n\x1b[1;36mSend FidoNet Netmail\x1b[0m\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");

    DbFidoAka primary;
    if (!db_fido_aka_get_primary(s->db, &primary))
    {
      send_str(s, "No FidoNet AKA configured. Contact sysop.\r\n");
      return;
    }

    char addr[64] = {0};
    prompt_line(s, "To address (Zone:Net/Node.Point): ", addr, sizeof(addr));
    if (!addr[0])
      return;

    int to_zone, to_net, to_node, to_point;
    if (!fido_parse_address(addr, &to_zone, &to_net, &to_node, &to_point))
    {
      send_str(s, "\r\nInvalid address format.\r\n");
      return;
    }

    char to_name[64] = {0};
    prompt_line(s, "To name: ", to_name, sizeof(to_name));
    if (!to_name[0])
      return;

    char subject[80] = {0};
    prompt_line(s, "Subject: ", subject, sizeof(subject));
    if (!subject[0])
      return;

    send_str(s, "\r\nEnter message (blank line to end):\r\n");
    char body[2048] = {0};
    size_t body_len = 0;
    while (body_len < sizeof(body) - 128)
    {
      uint8_t msg_line[256];
      int rn = session_readline(s, msg_line, sizeof(msg_line), s->cfg.idle_timeout_sec);
      if (rn <= 0)
        break;
      if (msg_line[0] == '\0')
        break;
      size_t llen = strlen((char *)msg_line);
      if (body_len + llen + 2 < sizeof(body))
      {
        memcpy(body + body_len, msg_line, llen);
        body_len += llen;
        body[body_len++] = '\r';
        body[body_len++] = '\n';
      }
    }
    body[body_len] = '\0';

    DbFidoNetmail nm = {0};
    nm.from_zone = primary.zone;
    nm.from_net = primary.net;
    nm.from_node = primary.node;
    nm.from_point = primary.point;
    snprintf(nm.from_name, sizeof(nm.from_name), "%s", s->user.handle);
    nm.to_zone = to_zone;
    nm.to_net = to_net;
    nm.to_node = to_node;
    nm.to_point = to_point;
    snprintf(nm.to_name, sizeof(nm.to_name), "%s", to_name);
    snprintf(nm.subject, sizeof(nm.subject), "%s", subject);
    snprintf(nm.body, sizeof(nm.body), "%s", body);
    nm.attr = NET_ATTR_LOCAL;

    if (db_fido_netmail_add(s->db, &nm))
    {
      send_str(s, "\r\nNetmail queued for delivery.\r\n");
    }
    else
    {
      send_str(s, "\r\nFailed to queue netmail.\r\n");
    }
  }
  else if (!strcmp(action, "batchrun"))
  {
    if (s->batch_count == 0)
    {
      send_str(s, "\r\nNo files queued.\r\n");
      return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "\r\nProcessing %d queued downloads...\r\n", s->batch_count);
    send_str(s, buf);
    for (int i = 0; i < s->batch_count; i++)
    {
      DbFileRec rec;
      if (db_file_get(s->db, s->batch_queue[i], &rec))
      {
        DbFileArea area = {0};
        DbFileArea areas[32];
        int ac = db_file_area_list(s->db, areas, 32);
        for (int j = 0; j < ac; j++)
          if (areas[j].id == rec.area_id)
            area = areas[j];
        char path[512];
        if (file_area_resolve(area.path, rec.filename, path, sizeof(path)))
        {
          send_str(s, "Downloading ");
          send_str(s, rec.filename);
          send_str(s, "\r\n");
          DbProtocol protos[4];
          int pcnt = db_protocols_list(s->db, protos, 4, "down");
          if (pcnt > 0)
            protocol_launch(s, &protos[0], path, "down");
        }
      }
    }
    s->batch_count = 0;
  }
  else if (!strcmp(action, "setfilescandate"))
  {
    cmd_file_set_scan_date(s, NULL);
  }
  else if (!strcmp(action, "archivetest"))
  {
    cmd_file_archive_test(s, NULL);
  }
  else if (!strcmp(action, "archiveextract"))
  {
    cmd_file_archive_extract(s, NULL);
  }
  else if (!strcmp(action, "batchremove"))
  {
    cmd_file_batch_remove(s, NULL);
  }
  else if (!strcmp(action, "batchupload"))
  {
    cmd_file_batch_upload(s, NULL);
  }
  else if (!strcmp(action, "fsedit"))
  {
    /* Full-screen editor for message composition */
    send_str(s, "\r\nEntering full-screen editor...\r\n");

    char text[4096] = "";
    int result = fsedit_edit(s, text, sizeof(text));

    if (result == 1 && text[0])
    {
      send_str(s, "\r\n\x1b[1;32mText saved.\x1b[0m\r\n");
      send_str(s, "Preview:\r\n");
      send_str(s, text);
      send_str(s, "\r\n");

      /* Ask if they want to post it */
      send_str(s, "(P)ost as message, (D)iscard? ");
      uint8_t line[8];
      int n = session_readline(s, line, sizeof(line), 30);
      if (n > 0 && (line[0] == 'P' || line[0] == 'p'))
      {
        if (s->current_msg_area > 0)
        {
          char subject[80] = {0};
          prompt_line(s, "Subject: ", subject, sizeof(subject));
          if (subject[0])
          {
            if (db_message_post(s->db, s->current_msg_area, s->user.id, subject, text, 0))
            {
              send_str(s, "\r\nMessage posted.\r\n");
              s->user.msg_post++;
              db_stats_inc(s->db, "posts");
            }
            else
            {
              send_str(s, "\r\nFailed to post message.\r\n");
            }
          }
        }
        else
        {
          send_str(s, "\r\nNo message area selected.\r\n");
        }
      }
      else
      {
        send_str(s, "\r\nDiscarded.\r\n");
      }
    }
    else
    {
      send_str(s, "\r\n\x1b[1;33mAborted.\x1b[0m\r\n");
    }
  }
  else if (!strcmp(action, "joinconf"))
  {
    /* Join a conference */
    send_str(s, "\r\n\x1b[1;36mJoin Conference\x1b[0m\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");

    /* List available conferences */
    DbConference confs[32];
    int cnt = db_conference_list(s->db, confs, 32);
    if (cnt == 0)
    {
      send_str(s, "No conferences available.\r\n");
      return;
    }

    char buf[256];
    for (int i = 0; i < cnt; i++)
    {
      bool member = db_conf_is_member(s->db, s->user.id, confs[i].id);
      snprintf(buf, sizeof(buf), "%2d. %-30s %s\r\n",
               confs[i].id, confs[i].name, member ? "[JOINED]" : "");
      send_str(s, buf);
    }

    send_str(s, "\r\nEnter conference number to join (0 to cancel): ");
    uint8_t line[16];
    int n = session_readline(s, line, sizeof(line), 30);
    if (n > 0)
    {
      int conf_id = atoi((char *)line);
      if (conf_id > 0)
      {
        if (db_conf_join(s->db, s->user.id, conf_id))
        {
          send_str(s, "\r\nJoined conference.\r\n");
        }
        else
        {
          send_str(s, "\r\nFailed to join conference.\r\n");
        }
      }
    }
  }
  else if (!strcmp(action, "leaveconf"))
  {
    /* Leave a conference */
    send_str(s, "\r\n\x1b[1;36mLeave Conference\x1b[0m\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");

    /* List user's conferences */
    int conf_ids[32];
    int cnt = db_conf_list_user(s->db, s->user.id, conf_ids, 32);
    if (cnt == 0)
    {
      send_str(s, "You are not a member of any conferences.\r\n");
      return;
    }

    char buf[256];
    for (int i = 0; i < cnt; i++)
    {
      DbConference conf;
      if (db_conference_get(s->db, conf_ids[i], &conf))
      {
        snprintf(buf, sizeof(buf), "%2d. %s\r\n", conf.id, conf.name);
        send_str(s, buf);
      }
    }

    send_str(s, "\r\nEnter conference number to leave (0 to cancel): ");
    uint8_t line[16];
    int n = session_readline(s, line, sizeof(line), 30);
    if (n > 0)
    {
      int conf_id = atoi((char *)line);
      if (conf_id > 0)
      {
        if (db_conf_leave(s->db, s->user.id, conf_id))
        {
          send_str(s, "\r\nLeft conference.\r\n");
        }
        else
        {
          send_str(s, "\r\nFailed to leave conference.\r\n");
        }
      }
    }
  }
  else if (!strcmp(action, "conflist"))
  {
    /* List conferences user belongs to */
    send_str(s, "\r\n\x1b[1;36mYour Conferences\x1b[0m\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");

    int conf_ids[32];
    int cnt = db_conf_list_user(s->db, s->user.id, conf_ids, 32);
    if (cnt == 0)
    {
      send_str(s, "You are not a member of any conferences.\r\n");
      return;
    }

    char buf[256];
    for (int i = 0; i < cnt; i++)
    {
      DbConference conf;
      if (db_conference_get(s->db, conf_ids[i], &conf))
      {
        snprintf(buf, sizeof(buf), "%2d. %-30s - %s\r\n", conf.id, conf.name, conf.description);
        send_str(s, buf);
      }
    }
  }
  else if (!strcmp(action, "lastcallers"))
  {
    /* Display last callers (OH command) */
    send_str(s, "\r\n\x1b[1;36mLast Callers\x1b[0m\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");
    send_str(s, " #  Handle                 Node  Login Time           Duration\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");

    DbCallHistory calls[20];
    int cnt = db_call_history_list(s->db, calls, 20);
    if (cnt == 0)
    {
      send_str(s, "No call history available.\r\n");
    }
    else
    {
      char buf[256];
      for (int i = 0; i < cnt; i++)
      {
        char dur[16];
        if (calls[i].duration_min > 0)
        {
          snprintf(dur, sizeof(dur), "%d min", calls[i].duration_min);
        }
        else
        {
          snprintf(dur, sizeof(dur), "Online");
        }
        snprintf(buf, sizeof(buf), "%2d. %-20s  %3d   %-19s  %s\r\n",
                 i + 1, calls[i].handle, calls[i].node_num, calls[i].login_at, dur);
        send_str(s, buf);
      }
    }
    send_str(s, "----------------------------------------------------------------------\r\n");
  }
  else if (!strcmp(action, "maintenance"))
  {
    if (!acs_allows(s, "+A"))
    {
      send_str(s, "\r\nAccess denied.\r\n");
      return;
    }
#ifdef HAVE_SQLITE
    send_str(s, "\r\nRunning VACUUM...\r\n");
    db_exec(s->db, "VACUUM");
    send_str(s, "Purge messages older than days: ");
    uint8_t line[16];
    int n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n > 0)
    {
      int days = atoi((char *)line);
      char sql[128];
      snprintf(sql, sizeof(sql), "DELETE FROM messages WHERE posted_at < datetime('now','-%d days')", days);
      db_exec(s->db, sql);
    }
    send_str(s, "Purge files older than days (0 skip): ");
    n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n > 0)
    {
      int days = atoi((char *)line);
      if (days > 0)
      {
        char sql[128];
        snprintf(sql, sizeof(sql), "DELETE FROM files WHERE uploaded_at < datetime('now','-%d days')", days);
        db_exec(s->db, sql);
      }
    }
    send_str(s, "Pack users (reset SMW/timebank)? (Y/N): ");
    n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n > 0 && (line[0] == 'Y' || line[0] == 'y'))
    {
      db_exec(s->db, "UPDATE users SET smw=0");
      db_exec(s->db, "DELETE FROM meta WHERE k LIKE 'tb_%'");
    }
    send_str(s, "\r\nMaintenance complete.\r\n");
#else
    send_str(s, "\r\nDB not available.\r\n");
#endif
  }
  else if (!strcmp(action, "fidoeditor"))
  {
    /* *F - FidoNet AKA/Echomail Editor */
    if (!acs_allows(s, "+A"))
    {
      send_str(s, "\r\nAccess denied.\r\n");
      return;
    }

    while (1)
    {
      send_str(s, "\r\n\x1b[1;36mFidoNet Editor\x1b[0m\r\n");
      send_str(s, "----------------------------------------------------------------------\r\n");
      send_str(s, "  [A] AKA Addresses\r\n");
      send_str(s, "  [E] Echomail Links\r\n");
      send_str(s, "  [N] Netmail Queue\r\n");
      send_str(s, "  [Q] Quit\r\n");
      send_str(s, "\r\nSelection: ");

      uint8_t line[16];
      int n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
      if (n <= 0 || line[0] == 'Q' || line[0] == 'q')
        break;

      char buf[512];

      if (line[0] == 'A' || line[0] == 'a')
      {
        /* AKA Address Management */
        while (1)
        {
          send_str(s, "\r\n\x1b[1;33mFidoNet AKA Addresses\x1b[0m\r\n");
          send_str(s, "----------------------------------------------------------------------\r\n");

          DbFidoAka akas[20];
          int cnt = db_fido_aka_list(s->db, akas, 20);
          if (cnt == 0)
          {
            send_str(s, "  No AKA addresses configured.\r\n");
          }
          else
          {
            for (int i = 0; i < cnt; i++)
            {
              char addr[32];
              fido_format_address(&akas[i], addr, sizeof(addr));
              snprintf(buf, sizeof(buf), "  [%2d] %s%s%s\r\n",
                       akas[i].id, addr,
                       akas[i].domain[0] ? "@" : "",
                       akas[i].domain[0] ? akas[i].domain : "");
              if (akas[i].is_primary)
              {
                snprintf(buf + strlen(buf) - 2, sizeof(buf) - strlen(buf) + 2, " [PRIMARY]\r\n");
              }
              send_str(s, buf);
            }
          }

          send_str(s, "\r\n(A)dd, (E)dit, (D)elete, (P)rimary, (Q)uit: ");
          n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
          if (n <= 0 || line[0] == 'Q' || line[0] == 'q')
            break;

          if (line[0] == 'A' || line[0] == 'a')
          {
            char addr[64] = {0};
            prompt_line(s, "\r\nAddress (Zone:Net/Node.Point): ", addr, sizeof(addr));
            if (!addr[0])
              continue;

            int zone, net, node, point;
            if (!fido_parse_address(addr, &zone, &net, &node, &point))
            {
              send_str(s, "\r\nInvalid address format.\r\n");
              continue;
            }

            char domain[64] = {0};
            prompt_line(s, "Domain (optional): ", domain, sizeof(domain));

            int is_primary = (cnt == 0) ? 1 : 0;
            if (db_fido_aka_add(s->db, zone, net, node, point, domain, is_primary))
            {
              send_str(s, "\r\nAKA added.\r\n");
            }
            else
            {
              send_str(s, "\r\nFailed to add AKA.\r\n");
            }
          }
          else if (line[0] == 'E' || line[0] == 'e')
          {
            char sel[8] = {0};
            prompt_line(s, "\r\nAKA ID to edit: ", sel, sizeof(sel));
            int id = atoi(sel);
            if (id <= 0)
              continue;

            DbFidoAka aka;
            if (!db_fido_aka_get(s->db, id, &aka))
            {
              send_str(s, "\r\nAKA not found.\r\n");
              continue;
            }

            char addr[64] = {0};
            fido_format_address(&aka, addr, sizeof(addr));
            snprintf(buf, sizeof(buf), "\r\nCurrent: %s\r\n", addr);
            send_str(s, buf);

            prompt_line(s, "New address (blank to keep): ", addr, sizeof(addr));
            if (addr[0])
            {
              int zone, net, node, point;
              if (fido_parse_address(addr, &zone, &net, &node, &point))
              {
                aka.zone = zone;
                aka.net = net;
                aka.node = node;
                aka.point = point;
              }
            }

            char domain[64] = {0};
            snprintf(buf, sizeof(buf), "Current domain: %s\r\n", aka.domain);
            send_str(s, buf);
            prompt_line(s, "New domain (blank to keep): ", domain, sizeof(domain));
            if (domain[0])
            {
              snprintf(aka.domain, sizeof(aka.domain), "%s", domain);
            }

            if (db_fido_aka_update(s->db, id, aka.zone, aka.net, aka.node, aka.point, aka.domain, aka.is_primary))
            {
              send_str(s, "\r\nAKA updated.\r\n");
            }
            else
            {
              send_str(s, "\r\nFailed to update AKA.\r\n");
            }
          }
          else if (line[0] == 'D' || line[0] == 'd')
          {
            char sel[8] = {0};
            prompt_line(s, "\r\nAKA ID to delete: ", sel, sizeof(sel));
            int id = atoi(sel);
            if (id > 0 && db_fido_aka_delete(s->db, id))
            {
              send_str(s, "\r\nAKA deleted.\r\n");
            }
            else
            {
              send_str(s, "\r\nFailed to delete AKA.\r\n");
            }
          }
          else if (line[0] == 'P' || line[0] == 'p')
          {
            char sel[8] = {0};
            prompt_line(s, "\r\nAKA ID to set as primary: ", sel, sizeof(sel));
            int id = atoi(sel);
            if (id <= 0)
              continue;

            DbFidoAka aka;
            if (db_fido_aka_get(s->db, id, &aka))
            {
              if (db_fido_aka_update(s->db, id, aka.zone, aka.net, aka.node, aka.point, aka.domain, 1))
              {
                send_str(s, "\r\nPrimary AKA set.\r\n");
              }
              else
              {
                send_str(s, "\r\nFailed to set primary.\r\n");
              }
            }
            else
            {
              send_str(s, "\r\nAKA not found.\r\n");
            }
          }
        }
      }
      else if (line[0] == 'E' || line[0] == 'e')
      {
        /* Echomail Link Management */
        while (1)
        {
          send_str(s, "\r\n\x1b[1;33mFidoNet Echomail Links\x1b[0m\r\n");
          send_str(s, "----------------------------------------------------------------------\r\n");

          DbFidoEcholink links[32];
          int cnt = db_fido_echolink_list(s->db, links, 32);
          if (cnt == 0)
          {
            send_str(s, "  No echomail links configured.\r\n");
          }
          else
          {
            for (int i = 0; i < cnt; i++)
            {
              DbMsgArea area;
              char area_name[64] = "???";
              if (db_msg_area_get(s->db, links[i].area_id, &area))
              {
                snprintf(area_name, sizeof(area_name), "%s", area.name);
              }
              snprintf(buf, sizeof(buf), "  [%2d] %-20s -> %s (HWM: %d)\r\n",
                       links[i].id, links[i].echotag, area_name, links[i].high_water);
              send_str(s, buf);
            }
          }

          send_str(s, "\r\n(A)dd, (E)dit, (D)elete, (Q)uit: ");
          n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
          if (n <= 0 || line[0] == 'Q' || line[0] == 'q')
            break;

          if (line[0] == 'A' || line[0] == 'a')
          {
            /* List message areas */
            DbMsgArea areas[32];
            int acnt = db_msg_area_list(s->db, areas, 32);
            send_str(s, "\r\nMessage Areas:\r\n");
            for (int i = 0; i < acnt; i++)
            {
              snprintf(buf, sizeof(buf), "  [%2d] %s\r\n", areas[i].id, areas[i].name);
              send_str(s, buf);
            }

            char sel[8] = {0};
            prompt_line(s, "\r\nArea ID: ", sel, sizeof(sel));
            int area_id = atoi(sel);
            if (area_id <= 0)
              continue;

            char echotag[64] = {0};
            prompt_line(s, "Echo tag (e.g., BBS_CARNIVAL): ", echotag, sizeof(echotag));
            if (!echotag[0])
              continue;

            /* List AKAs */
            DbFidoAka akas[20];
            int akacnt = db_fido_aka_list(s->db, akas, 20);
            if (akacnt == 0)
            {
              send_str(s, "\r\nNo AKAs configured. Add one first.\r\n");
              continue;
            }
            send_str(s, "\r\nAKA Addresses:\r\n");
            for (int i = 0; i < akacnt; i++)
            {
              char addr[32];
              fido_format_address(&akas[i], addr, sizeof(addr));
              snprintf(buf, sizeof(buf), "  [%2d] %s\r\n", akas[i].id, addr);
              send_str(s, buf);
            }

            prompt_line(s, "AKA ID: ", sel, sizeof(sel));
            int aka_id = atoi(sel);
            if (aka_id <= 0)
              aka_id = akas[0].id;

            char origin[80] = {0};
            prompt_line(s, "Origin line (optional): ", origin, sizeof(origin));

            if (db_fido_echolink_add(s->db, area_id, echotag, aka_id, origin))
            {
              send_str(s, "\r\nEcholink added.\r\n");
            }
            else
            {
              send_str(s, "\r\nFailed to add echolink.\r\n");
            }
          }
          else if (line[0] == 'E' || line[0] == 'e')
          {
            char sel[8] = {0};
            prompt_line(s, "\r\nEcholink ID to edit: ", sel, sizeof(sel));
            int id = atoi(sel);
            if (id <= 0)
              continue;

            DbFidoEcholink link;
            if (!db_fido_echolink_get(s->db, id, &link))
            {
              send_str(s, "\r\nEcholink not found.\r\n");
              continue;
            }

            snprintf(buf, sizeof(buf), "\r\nCurrent echotag: %s\r\n", link.echotag);
            send_str(s, buf);

            char echotag[64] = {0};
            prompt_line(s, "New echotag (blank to keep): ", echotag, sizeof(echotag));
            if (echotag[0])
            {
              snprintf(link.echotag, sizeof(link.echotag), "%s", echotag);
            }

            snprintf(buf, sizeof(buf), "Current origin: %s\r\n", link.origin);
            send_str(s, buf);

            char origin[80] = {0};
            prompt_line(s, "New origin (blank to keep): ", origin, sizeof(origin));
            if (origin[0])
            {
              snprintf(link.origin, sizeof(link.origin), "%s", origin);
            }

            if (db_fido_echolink_update(s->db, id, link.echotag, link.aka_id, link.origin))
            {
              send_str(s, "\r\nEcholink updated.\r\n");
            }
            else
            {
              send_str(s, "\r\nFailed to update echolink.\r\n");
            }
          }
          else if (line[0] == 'D' || line[0] == 'd')
          {
            char sel[8] = {0};
            prompt_line(s, "\r\nEcholink ID to delete: ", sel, sizeof(sel));
            int id = atoi(sel);
            if (id > 0 && db_fido_echolink_delete(s->db, id))
            {
              send_str(s, "\r\nEcholink deleted.\r\n");
            }
            else
            {
              send_str(s, "\r\nFailed to delete echolink.\r\n");
            }
          }
        }
      }
      else if (line[0] == 'N' || line[0] == 'n')
      {
        /* Netmail Queue */
        send_str(s, "\r\n\x1b[1;33mFidoNet Netmail Queue\x1b[0m\r\n");
        send_str(s, "----------------------------------------------------------------------\r\n");

        DbFidoNetmail mails[32];
        int cnt = db_fido_netmail_list(s->db, NULL, mails, 32);
        if (cnt == 0)
        {
          send_str(s, "  No netmail in queue.\r\n");
        }
        else
        {
          for (int i = 0; i < cnt; i++)
          {
            snprintf(buf, sizeof(buf), "  [%2d] %s -> %d:%d/%d.%d (%s) [%s]\r\n",
                     mails[i].id, mails[i].from_name,
                     mails[i].to_zone, mails[i].to_net, mails[i].to_node, mails[i].to_point,
                     mails[i].to_name, mails[i].status);
            send_str(s, buf);
          }
        }

        send_str(s, "\r\n(V)iew, (D)elete, (Q)uit: ");
        n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
        if (n > 0 && (line[0] == 'V' || line[0] == 'v'))
        {
          char sel[8] = {0};
          prompt_line(s, "\r\nNetmail ID to view: ", sel, sizeof(sel));
          int id = atoi(sel);
          if (id > 0)
          {
            DbFidoNetmail nm;
            if (db_fido_netmail_get(s->db, id, &nm))
            {
              snprintf(buf, sizeof(buf), "\r\nFrom: %s (%d:%d/%d.%d)\r\n",
                       nm.from_name, nm.from_zone, nm.from_net, nm.from_node, nm.from_point);
              send_str(s, buf);
              snprintf(buf, sizeof(buf), "To: %s (%d:%d/%d.%d)\r\n",
                       nm.to_name, nm.to_zone, nm.to_net, nm.to_node, nm.to_point);
              send_str(s, buf);
              snprintf(buf, sizeof(buf), "Subject: %s\r\n", nm.subject);
              send_str(s, buf);
              snprintf(buf, sizeof(buf), "Status: %s\r\n\r\n", nm.status);
              send_str(s, buf);
              send_str(s, nm.body);
              send_str(s, "\r\n");
            }
          }
        }
        else if (n > 0 && (line[0] == 'D' || line[0] == 'd'))
        {
          char sel[8] = {0};
          prompt_line(s, "\r\nNetmail ID to delete: ", sel, sizeof(sel));
          int id = atoi(sel);
          if (id > 0 && db_fido_netmail_delete(s->db, id))
          {
            send_str(s, "\r\nNetmail deleted.\r\n");
          }
          else
          {
            send_str(s, "\r\nFailed to delete netmail.\r\n");
          }
        }
      }
    }
  }
  else if (!strcmp(action, "qwkneteditor"))
  {
    /* *Q - QWK Network Editor */
    if (!acs_allows(s, "+A"))
    {
      send_str(s, "\r\nAccess denied.\r\n");
      return;
    }

    while (1)
    {
      send_str(s, "\r\n\x1b[1;36mQWK Network Editor\x1b[0m\r\n");
      send_str(s, "----------------------------------------------------------------------\r\n");
      send_str(s, "  [H] Hub Management\r\n");
      send_str(s, "  [L] Area Links\r\n");
      send_str(s, "  [P] Packet Queue\r\n");
      send_str(s, "  [Q] Quit\r\n");
      send_str(s, "\r\nSelection: ");

      uint8_t line[16];
      int n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
      if (n <= 0 || line[0] == 'Q' || line[0] == 'q')
        break;

      char buf[512];

      if (line[0] == 'H' || line[0] == 'h')
      {
        /* Hub Management */
        while (1)
        {
          send_str(s, "\r\n\x1b[1;33mQWK Network Hubs\x1b[0m\r\n");
          send_str(s, "----------------------------------------------------------------------\r\n");

          DbQwkHub hubs[16];
          int cnt = db_qwk_hub_list(s->db, hubs, 16);
          if (cnt == 0)
          {
            send_str(s, "  No QWK hubs configured.\r\n");
          }
          else
          {
            for (int i = 0; i < cnt; i++)
            {
              snprintf(buf, sizeof(buf), "  [%2d] %-20s (%s) %s\r\n",
                       hubs[i].id, hubs[i].name, hubs[i].bbs_id,
                       hubs[i].enabled ? "[ENABLED]" : "[DISABLED]");
              send_str(s, buf);
            }
          }

          send_str(s, "\r\n(A)dd, (E)dit, (D)elete, (T)oggle, (Q)uit: ");
          n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
          if (n <= 0 || line[0] == 'Q' || line[0] == 'q')
            break;

          if (line[0] == 'A' || line[0] == 'a')
          {
            char name[64] = {0};
            prompt_line(s, "\r\nHub name: ", name, sizeof(name));
            if (!name[0])
              continue;

            char bbs_id[16] = {0};
            prompt_line(s, "BBS ID (8 chars max): ", bbs_id, sizeof(bbs_id));
            if (!bbs_id[0])
              continue;

            char schedule[64] = {0};
            prompt_line(s, "Call schedule (optional, e.g., daily@02:00): ", schedule, sizeof(schedule));

            if (db_qwk_hub_add(s->db, name, bbs_id, schedule))
            {
              send_str(s, "\r\nHub added.\r\n");
            }
            else
            {
              send_str(s, "\r\nFailed to add hub.\r\n");
            }
          }
          else if (line[0] == 'E' || line[0] == 'e')
          {
            char sel[8] = {0};
            prompt_line(s, "\r\nHub ID to edit: ", sel, sizeof(sel));
            int id = atoi(sel);
            if (id <= 0)
              continue;

            DbQwkHub hub;
            if (!db_qwk_hub_get(s->db, id, &hub))
            {
              send_str(s, "\r\nHub not found.\r\n");
              continue;
            }

            snprintf(buf, sizeof(buf), "\r\nCurrent name: %s\r\n", hub.name);
            send_str(s, buf);
            char name[64] = {0};
            prompt_line(s, "New name (blank to keep): ", name, sizeof(name));
            if (name[0])
              snprintf(hub.name, sizeof(hub.name), "%s", name);

            snprintf(buf, sizeof(buf), "Current BBS ID: %s\r\n", hub.bbs_id);
            send_str(s, buf);
            char bbs_id[16] = {0};
            prompt_line(s, "New BBS ID (blank to keep): ", bbs_id, sizeof(bbs_id));
            if (bbs_id[0])
              snprintf(hub.bbs_id, sizeof(hub.bbs_id), "%s", bbs_id);

            snprintf(buf, sizeof(buf), "Current schedule: %s\r\n", hub.call_schedule);
            send_str(s, buf);
            char schedule[64] = {0};
            prompt_line(s, "New schedule (blank to keep): ", schedule, sizeof(schedule));
            if (schedule[0])
              snprintf(hub.call_schedule, sizeof(hub.call_schedule), "%s", schedule);

            if (db_qwk_hub_update(s->db, id, hub.name, hub.bbs_id, hub.call_schedule, hub.enabled))
            {
              send_str(s, "\r\nHub updated.\r\n");
            }
            else
            {
              send_str(s, "\r\nFailed to update hub.\r\n");
            }
          }
          else if (line[0] == 'D' || line[0] == 'd')
          {
            char sel[8] = {0};
            prompt_line(s, "\r\nHub ID to delete: ", sel, sizeof(sel));
            int id = atoi(sel);
            if (id > 0 && db_qwk_hub_delete(s->db, id))
            {
              send_str(s, "\r\nHub deleted.\r\n");
            }
            else
            {
              send_str(s, "\r\nFailed to delete hub.\r\n");
            }
          }
          else if (line[0] == 'T' || line[0] == 't')
          {
            char sel[8] = {0};
            prompt_line(s, "\r\nHub ID to toggle: ", sel, sizeof(sel));
            int id = atoi(sel);
            if (id <= 0)
              continue;

            DbQwkHub hub;
            if (db_qwk_hub_get(s->db, id, &hub))
            {
              hub.enabled = !hub.enabled;
              if (db_qwk_hub_update(s->db, id, hub.name, hub.bbs_id, hub.call_schedule, hub.enabled))
              {
                snprintf(buf, sizeof(buf), "\r\nHub %s.\r\n", hub.enabled ? "enabled" : "disabled");
                send_str(s, buf);
              }
            }
          }
        }
      }
      else if (line[0] == 'L' || line[0] == 'l')
      {
        /* Area Links */
        send_str(s, "\r\n\x1b[1;33mQWK Area Links\x1b[0m\r\n");
        send_str(s, "----------------------------------------------------------------------\r\n");

        /* First select a hub */
        DbQwkHub hubs[16];
        int hcnt = db_qwk_hub_list(s->db, hubs, 16);
        if (hcnt == 0)
        {
          send_str(s, "No hubs configured. Add a hub first.\r\n");
          continue;
        }

        for (int i = 0; i < hcnt; i++)
        {
          snprintf(buf, sizeof(buf), "  [%2d] %s\r\n", hubs[i].id, hubs[i].name);
          send_str(s, buf);
        }

        char sel[8] = {0};
        prompt_line(s, "\r\nSelect hub ID: ", sel, sizeof(sel));
        int hub_id = atoi(sel);
        if (hub_id <= 0)
          continue;

        while (1)
        {
          send_str(s, "\r\nArea Links for Hub:\r\n");

          DbQwkAreaLink links[32];
          int lcnt = db_qwk_area_link_list(s->db, hub_id, links, 32);
          if (lcnt == 0)
          {
            send_str(s, "  No area links configured.\r\n");
          }
          else
          {
            for (int i = 0; i < lcnt; i++)
            {
              DbMsgArea area;
              char area_name[64] = "???";
              if (db_msg_area_get(s->db, links[i].area_id, &area))
              {
                snprintf(area_name, sizeof(area_name), "%s", area.name);
              }
              snprintf(buf, sizeof(buf), "  [%2d] %-25s -> Conf #%d (HW: %d/%d)\r\n",
                       links[i].id, area_name, links[i].remote_conf,
                       links[i].high_water_in, links[i].high_water_out);
              send_str(s, buf);
            }
          }

          send_str(s, "\r\n(A)dd, (E)dit, (D)elete, (Q)uit: ");
          n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
          if (n <= 0 || line[0] == 'Q' || line[0] == 'q')
            break;

          if (line[0] == 'A' || line[0] == 'a')
          {
            /* List message areas */
            DbMsgArea areas[32];
            int acnt = db_msg_area_list(s->db, areas, 32);
            send_str(s, "\r\nMessage Areas:\r\n");
            for (int i = 0; i < acnt; i++)
            {
              snprintf(buf, sizeof(buf), "  [%2d] %s\r\n", areas[i].id, areas[i].name);
              send_str(s, buf);
            }

            prompt_line(s, "\r\nLocal area ID: ", sel, sizeof(sel));
            int area_id = atoi(sel);
            if (area_id <= 0)
              continue;

            prompt_line(s, "Remote conference number: ", sel, sizeof(sel));
            int remote_conf = atoi(sel);
            if (remote_conf < 0)
              continue;

            if (db_qwk_area_link_add(s->db, hub_id, area_id, remote_conf))
            {
              send_str(s, "\r\nArea link added.\r\n");
            }
            else
            {
              send_str(s, "\r\nFailed to add area link.\r\n");
            }
          }
          else if (line[0] == 'E' || line[0] == 'e')
          {
            prompt_line(s, "\r\nLink ID to edit: ", sel, sizeof(sel));
            int id = atoi(sel);
            if (id <= 0)
              continue;

            DbQwkAreaLink link;
            if (!db_qwk_area_link_get(s->db, id, &link))
            {
              send_str(s, "\r\nLink not found.\r\n");
              continue;
            }

            snprintf(buf, sizeof(buf), "\r\nCurrent remote conf: %d\r\n", link.remote_conf);
            send_str(s, buf);
            prompt_line(s, "New remote conf: ", sel, sizeof(sel));
            int remote_conf = atoi(sel);

            if (db_qwk_area_link_update(s->db, id, remote_conf))
            {
              send_str(s, "\r\nLink updated.\r\n");
            }
            else
            {
              send_str(s, "\r\nFailed to update link.\r\n");
            }
          }
          else if (line[0] == 'D' || line[0] == 'd')
          {
            prompt_line(s, "\r\nLink ID to delete: ", sel, sizeof(sel));
            int id = atoi(sel);
            if (id > 0 && db_qwk_area_link_delete(s->db, id))
            {
              send_str(s, "\r\nLink deleted.\r\n");
            }
            else
            {
              send_str(s, "\r\nFailed to delete link.\r\n");
            }
          }
        }
      }
      else if (line[0] == 'P' || line[0] == 'p')
      {
        /* Packet Queue */
        send_str(s, "\r\n\x1b[1;33mQWK Packet Queue\x1b[0m\r\n");
        send_str(s, "----------------------------------------------------------------------\r\n");

        DbQwkPacket packets[32];
        int pcnt = db_qwk_packet_list(s->db, 0, NULL, packets, 32);
        if (pcnt == 0)
        {
          send_str(s, "  No packets in queue.\r\n");
        }
        else
        {
          for (int i = 0; i < pcnt; i++)
          {
            DbQwkHub hub;
            char hub_name[64] = "???";
            if (db_qwk_hub_get(s->db, packets[i].hub_id, &hub))
            {
              snprintf(hub_name, sizeof(hub_name), "%s", hub.name);
            }
            snprintf(buf, sizeof(buf), "  [%2d] %s %s (%s) [%s]\r\n",
                     packets[i].id, packets[i].packet_type, hub_name,
                     packets[i].packet_path, packets[i].status);
            send_str(s, buf);
          }
        }

        send_str(s, "\r\n(D)elete, (M)ark processed, (Q)uit: ");
        n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
        if (n > 0 && (line[0] == 'D' || line[0] == 'd'))
        {
          char sel[8] = {0};
          prompt_line(s, "\r\nPacket ID to delete: ", sel, sizeof(sel));
          int id = atoi(sel);
          if (id > 0 && db_qwk_packet_delete(s->db, id))
          {
            send_str(s, "\r\nPacket deleted.\r\n");
          }
        }
        else if (n > 0 && (line[0] == 'M' || line[0] == 'm'))
        {
          char sel[8] = {0};
          prompt_line(s, "\r\nPacket ID to mark processed: ", sel, sizeof(sel));
          int id = atoi(sel);
          if (id > 0 && db_qwk_packet_mark_processed(s->db, id))
          {
            send_str(s, "\r\nPacket marked as processed.\r\n");
          }
        }
      }
    }
  }
  else if (!strcmp(action, "fidosend"))
  {
    /* Send FidoNet netmail */
    send_str(s, "\r\n\x1b[1;36mSend FidoNet Netmail\x1b[0m\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");

    DbFidoAka primary;
    if (!db_fido_aka_get_primary(s->db, &primary))
    {
      send_str(s, "No FidoNet AKA configured. Contact sysop.\r\n");
      return;
    }

    char addr[64] = {0};
    prompt_line(s, "To address (Zone:Net/Node.Point): ", addr, sizeof(addr));
    if (!addr[0])
      return;

    int to_zone, to_net, to_node, to_point;
    if (!fido_parse_address(addr, &to_zone, &to_net, &to_node, &to_point))
    {
      send_str(s, "\r\nInvalid address format.\r\n");
      return;
    }

    char to_name[64] = {0};
    prompt_line(s, "To name: ", to_name, sizeof(to_name));
    if (!to_name[0])
      return;

    char subject[80] = {0};
    prompt_line(s, "Subject: ", subject, sizeof(subject));
    if (!subject[0])
      return;

    send_str(s, "\r\nEnter message (blank line to end):\r\n");
    char body[2048] = {0};
    size_t body_len = 0;
    while (body_len < sizeof(body) - 128)
    {
      uint8_t line[256];
      int n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
      if (n <= 0)
        break;
      if (line[0] == '\0')
        break;
      size_t llen = strlen((char *)line);
      if (body_len + llen + 2 < sizeof(body))
      {
        memcpy(body + body_len, line, llen);
        body_len += llen;
        body[body_len++] = '\r';
        body[body_len++] = '\n';
      }
    }
    body[body_len] = '\0';

    DbFidoNetmail nm = {0};
    nm.from_zone = primary.zone;
    nm.from_net = primary.net;
    nm.from_node = primary.node;
    nm.from_point = primary.point;
    snprintf(nm.from_name, sizeof(nm.from_name), "%s", s->user.handle);
    nm.to_zone = to_zone;
    nm.to_net = to_net;
    nm.to_node = to_node;
    nm.to_point = to_point;
    snprintf(nm.to_name, sizeof(nm.to_name), "%s", to_name);
    snprintf(nm.subject, sizeof(nm.subject), "%s", subject);
    snprintf(nm.body, sizeof(nm.body), "%s", body);
    nm.attr = NET_ATTR_LOCAL;

    if (db_fido_netmail_add(s->db, &nm))
    {
      send_str(s, "\r\nNetmail queued for delivery.\r\n");
    }
    else
    {
      send_str(s, "\r\nFailed to queue netmail.\r\n");
    }
  }
  else if (!strcmp(action, "help"))
  {
    send_str(s, "\r\nHelp:\r\n");
    send_str(s, "  Choose a menu key and press ENTER.\r\n");
    send_str(s, "  This is a skeleton — add modules under src/modules/.\r\n");
    send_str(s, "\r\n(press ENTER)\r\n");
    uint8_t line[64];
    session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
  }
  else if (!strcmp(action, "logout"))
  {
    s->alive = 0;
  }
  else if (!strcmp(action, "plugins"))
  {
    /* List loaded plugins */
    send_str(s, "\r\n\x1b[1;33mLoaded Plugins\x1b[0m\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");
    char list_buf[2048];
    size_t n = plugin_registry_list(list_buf, sizeof(list_buf));
    if (n == 0)
    {
      send_str(s, "  No plugins loaded.\r\n");
    }
    else
    {
      send_str(s, list_buf);
    }
    send_str(s, "\r\n(press ENTER)\r\n");
    uint8_t line[64];
    session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
  }
  else if (str_starts_with(action, "plugin:"))
  {
    /* Run a plugin: plugin:<plugin_id> */
    const char *plugin_id = action + 7;

    if (!plugin_loader_enabled())
    {
      send_str(s, "\r\nPlugins are not enabled.\r\n");
      return;
    }

    plugin_entry_t *entry = plugin_registry_find(plugin_id);
    if (!entry)
    {
      char msg[256];
      snprintf(msg, sizeof(msg), "\r\nPlugin not found: %s\r\n", plugin_id);
      send_str(s, msg);
      return;
    }

    if (!(entry->desc.caps & BBS_CAP_INTERACTIVE))
    {
      send_str(s, "\r\nPlugin does not support interactive mode.\r\n");
      return;
    }

    if (!entry->desc.create_instance)
    {
      send_str(s, "\r\nPlugin cannot create instances.\r\n");
      return;
    }

    /* Create plugin instance */
    void *inst = NULL;
    const bbs_plugin_instance_vtbl_t *vtbl = NULL;

    bbs_rc_t rc = entry->desc.create_instance((bbs_session_t *)s, &inst, &vtbl);
    if (rc != BBS_OK || !inst || !vtbl)
    {
      send_str(s, "\r\nFailed to create plugin instance.\r\n");
      return;
    }

    /* Track instance */
    plugin_registry_instance_inc(plugin_id);

    /* Run plugin lifecycle */
    if (vtbl->on_enter)
    {
      rc = vtbl->on_enter(inst, (bbs_session_t *)s);
      if (rc != BBS_OK)
      {
        log_warn("plugin %s on_enter failed: %d", plugin_id, rc);
      }
    }

    if (vtbl->run)
    {
      rc = vtbl->run(inst, (bbs_session_t *)s);
      if (rc != BBS_OK && rc != BBS_EIO)
      {
        log_warn("plugin %s run failed: %d", plugin_id, rc);
      }
    }

    if (vtbl->on_exit)
    {
      vtbl->on_exit(inst, (bbs_session_t *)s);
    }

    /* Destroy instance */
    if (vtbl->destroy)
    {
      vtbl->destroy(inst);
    }

    /* Untrack instance */
    plugin_registry_instance_dec(plugin_id);
  }
  else
  {
    send_str(s, "\r\nUnknown action.\r\n");
  }
}

static int detect_ansi(Session *s)
{
  /* Ask user if they want ANSI graphics */
  send_str(s, "\r\nWelcome to Mutineer BBS!\r\n\r\n");
  send_str(s, "ANSI graphics? (Y/n): ");

  uint8_t resp[16];
  int n = session_readline(s, resp, sizeof(resp), 30);
  if (n < 0)
    return -1; /* connection error */

  /* Default to yes if just enter pressed, or explicit Y/y */
  if (n == 0 || resp[0] == 'Y' || resp[0] == 'y' || resp[0] == '\r' || resp[0] == '\n')
  {
    s->ansi = 1;
    send_str(s, "ANSI enabled.\r\n");
  }
  else
  {
    s->ansi = 0;
    send_str(s, "ASCII mode.\r\n");
  }
  return 0;
}

void *session_thread_main(void *arg)
{
  Session *s = (Session *)arg;
  int menu_loaded = 0;

  pthread_mutex_init(&s->chat_inbox_lock, NULL);

  online_add(s);

  /* Mark node as logging in */
  db_node_upsert(s->db, s->node_num, 0, "logging_in", "connecting", s->ip);

  /* Drain any telnet negotiation first */
  drain_telnet_negotiation(s);

  /* Detect ANSI capability */
  if (detect_ansi(s) < 0)
  {
    goto cleanup;
  }

  /* Now we can send appropriate content */
  if (s->ansi)
  {
    send_str(s, "\x1b[2J\x1b[H"); /* clear screen */
    send_str(s, ANSI_BASE);
    send_art(s, "mutineer.ans");
    send_motd(s);
  }
  else
  {
    send_str(s, "\r\n");
    send_str(s, "Welcome to the Mutineer Telnet BBS!\r\n");
    send_str(s, "All seas are green here - hoist the flag and explore.\r\n\r\n");
  }

  {
    DbAutomsg am;
    if (db_automsg_get(s->db, &am))
    {
      send_str(s, "\r\nAutomsg:\r\n");
      send_str(s, am.msg);
      send_str(s, "\r\n");
    }
  }
  {
    DbOneliner oneliners[10];
    int n = db_oneliner_list(s->db, oneliners, 10);
    if (n > 0)
    {
      if (s->ansi)
      {
        send_str(s, "\r\n\x1b[1;36mRecent One-Liners:\x1b[0m\r\n");
      }
      else
      {
        send_str(s, "\r\nRecent One-Liners:\r\n");
      }
      char linebuf[256];
      for (int i = n - 1; i >= 0; i--)
      {
        if (s->ansi)
        {
          snprintf(linebuf, sizeof(linebuf), "  \x1b[1;32m%s\x1b[0m: %s\r\n",
                   oneliners[i].user_handle, oneliners[i].text);
        }
        else
        {
          snprintf(linebuf, sizeof(linebuf), "  %s: %s\r\n",
                   oneliners[i].user_handle, oneliners[i].text);
        }
        send_str(s, linebuf);
      }
    }
  }
  DbUser current_user;
  int auth = authenticate(s, &current_user);
  if (auth <= 0)
  {
    send_str(s, "\r\nGoodbye.\r\n");
    goto cleanup;
  }

  /* Multi-login prevention check */
  if (!s->cfg.allow_multi_login)
  {
    int existing_node = 0;
    if (db_node_user_online(s->db, current_user.id, &existing_node))
    {
      char artpath[512];
      path_join(s->cfg.art_path, "multilog.ans", artpath, sizeof(artpath));
      send_art(s, artpath);
      char buf[128];
      snprintf(buf, sizeof(buf), "\r\nYou are already logged in on node %d.\r\n", existing_node);
      send_str(s, buf);
      send_str(s, "Multiple logins are not permitted.\r\n");
      send_str(s, "\r\nGoodbye.\r\n");
      goto cleanup;
    }
  }

  /* Daily call limit check */
  if (s->cfg.max_calls_per_day > 0 &&
      current_user.on_today >= s->cfg.max_calls_per_day)
  {
    if (!send_named_art(s, "2MANYCAL"))
      send_str(s, "\r\nYou have reached your maximum calls for today. Please try again tomorrow.\r\n");
    send_str(s, "\r\nGoodbye.\r\n");
    goto cleanup;
  }

  s->user = current_user;
  db_node_upsert(s->db, s->node_num, s->user.id, "online", "menu", s->ip);
  /* override session time limit from security level if present */
  s->time_left_min = s->cfg.session_time_limit_min;
  if (s->user.time_limit_min > 0)
  {
    s->time_left_min = s->user.time_limit_min;
  }
  s->credits = s->user.credits;
  s->file_points = s->user.file_points;
  if (s->user.smw > 0)
  {
    char buf[128];
    snprintf(buf, sizeof(buf), "\r\n*** You have %d new private messages ***\r\n", s->user.smw);
    send_str(s, buf);
    db_user_clear_smw(s->db, s->user.id);
  }
  {
    int draft_count = db_draft_count(s->db, s->user.id);
    if (draft_count > 0) {
      char buf[128];
      snprintf(buf, sizeof(buf), "\r\n*** You have %d saved draft(s). Use the message area to resume. ***\r\n", draft_count);
      send_str(s, buf);
    }
  }
  db_stats_inc(s->db, "calls");
  s->call_id = db_call_log_start(s->db, s->user.id, s->user.handle, s->node_num, s->ip);
  scheduler_run_logon_events(s->db, s->user.id, s->user.handle);
  send_str(s, "\r\n"); /* newline after login */
  if (s->tn.term_type[0])
  {
    send_str(s, "Terminal: ");
    send_str(s, s->tn.term_type);
    send_str(s, "\r\n");
  }
  {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "Window: %dx%d\r\n", s->tn.cols, s->tn.rows);
    send_str(s, tmp);
  }
  send_str(s, "\r\n");

  Menu menu;
  memset(&menu, 0, sizeof(menu));
  if (!menu_load(s->cfg.menu_main, &menu))
  {
    send_str(s, "ERROR: failed to load menu file.\r\n");
    s->alive = 0;
  }
  else
  {
    menu_loaded = 1;
  }

  while (s->alive)
  {
    if (g_stop)
    {
      send_str(s, "\r\nSystem shutting down.\r\n");
      break;
    }
    if (s->time_left_min > 0)
    {
      time_t now = time(NULL);
      double elapsed = difftime(now, s->started_at) / 60.0;
      double remaining = (double)s->time_left_min - elapsed;
      if (remaining <= 0)
      {
        if (!send_named_art(s, "NOTLEFTA"))
          send_str(s, "\r\nSession time limit reached. Goodbye.\r\n");
        break;
      }
      s->time_left_min = (int)remaining;
    }

    db_node_upsert(s->db, s->node_num, s->user.id, "online", "menu", s->ip);

    char render[4096];
    size_t rlen = menu_render_template(&menu, s, render, sizeof(render));
    if (rlen == 0)
    {
      /* Template rendering failed, fall back to classic renderer */
      rlen = menu_render(&menu, s, render, sizeof(render));
    }
    send_str(s, render);

    uint8_t line[64];
    int n = session_readline(s, line, sizeof(line), s->cfg.idle_timeout_sec);
    if (n == 0)
      break;
    if (n == -2)
    {
      send_str(s, "\r\nIdle timeout.\r\n");
      break;
    }

    /* F-key intercept: ESC sequences from remote sysop */
    if (n > 0 && line[0] == 0x1B) {
      int fnum = parse_fkey(line, n);
      if (fnum > 0 && handle_fkey(s, fnum))
        continue;
      /* Unknown escape sequence — ignore */
      continue;
    }

    char key = (char)line[0];
    const MenuItem *it = menu_find(&menu, key);
    if (!it)
    {
      send_str(s, "\r\nInvalid selection.\r\n");
      continue;
    }
    if (!acs_allows(s, it->acs))
    {
      if (!send_named_art(s, "NOACCESS"))
        send_str(s, "\r\nAccess denied.\r\n");
      continue;
    }
    handle_action(s, it->action);
    send_str(s, "\r\n");
  }

cleanup:
  if (menu_loaded)
    menu_free(&menu);
  if (s->call_id > 0)
  {
    int duration = (int)((time(NULL) - s->started_at) / 60);
    db_call_log_end(s->db, s->call_id, duration);
  }
  online_remove(s);
  db_node_clear(s->db, s->node_num);
  if (s->db)
    db_close(s->db);
  send_str(s, ANSI_RESET); /* reset color */
  close(s->fd);
  free(s);
  return NULL;
}
