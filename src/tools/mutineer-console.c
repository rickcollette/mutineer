#include "bbs_config.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CSI "\033["
#define CLEAR_SCREEN CSI "2J" CSI "H"
#define HIDE_CURSOR CSI "?25l"
#define SHOW_CURSOR CSI "?25h"
#define COLOR_RESET CSI "0m"
#define COLOR_BOLD CSI "1m"

#define BOX_TL "+"
#define BOX_TR "+"
#define BOX_BL "+"
#define BOX_BR "+"
#define BOX_H "-"
#define BOX_V "|"

typedef struct ConsoleState
{
  BbsConfig cfg;
  int fd;
  int seq;
  int calls_today, posts_today, emails_today, newusers_today, feedback_today;
  int uploads_today, downloads_today, minutes_today, errors;
  long long ul_kb_today, dl_kb_today, disk_free_kb;
  int total_calls, total_posts, total_uploads, total_downloads, days_online, total_users;
  int mail_waiting;
  char os[64];
  char nodes_json[12000];
} ConsoleState;

static struct termios g_orig_termios;
static int g_raw = 0;

static void disable_raw(void)
{
  if (g_raw)
  {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
    g_raw = 0;
  }
  printf(SHOW_CURSOR COLOR_RESET);
  fflush(stdout);
}

static void enable_raw(void)
{
  if (g_raw) return;
  tcgetattr(STDIN_FILENO, &g_orig_termios);
  atexit(disable_raw);
  struct termios raw = g_orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  g_raw = 1;
}

static void gotoxy(int x, int y) { printf(CSI "%d;%dH", y, x); }
static void set_fg(int color)
{
  if (color < 8) printf(CSI "%dm", 30 + color);
  else printf(CSI "%d;1m", 30 + (color - 8));
}
static void set_bg(int color) { printf(CSI "%dm", 40 + color); }
static void set_colors(ConsoleState *st)
{
  set_fg(st->cfg.wfc_fg_color);
  set_bg(st->cfg.wfc_bg_color);
}

static void draw_box(int x, int y, int w, int h)
{
  gotoxy(x, y);
  printf(BOX_TL);
  for (int i = 0; i < w - 2; i++) printf(BOX_H);
  printf(BOX_TR);
  for (int i = 1; i < h - 1; i++)
  {
    gotoxy(x, y + i);
    printf(BOX_V);
    gotoxy(x + w - 1, y + i);
    printf(BOX_V);
  }
  gotoxy(x, y + h - 1);
  printf(BOX_BL);
  for (int i = 0; i < w - 2; i++) printf(BOX_H);
  printf(BOX_BR);
}

static void draw_titled_box(int x, int y, int w, int h, const char *title)
{
  draw_box(x, y, w, h);
  int tlen = (int)strlen(title);
  gotoxy(x + (w - tlen - 2) / 2, y);
  printf(" %s ", title);
}

static void format_bytes(long long bytes, char *out, size_t cap)
{
  if (bytes >= 1073741824LL) snprintf(out, cap, "%.1f GB", (double)bytes / 1073741824.0);
  else if (bytes >= 1048576LL) snprintf(out, cap, "%.1f MB", (double)bytes / 1048576.0);
  else if (bytes >= 1024LL) snprintf(out, cap, "%.1f KB", (double)bytes / 1024.0);
  else snprintf(out, cap, "%lld B", bytes);
}

static int connect_tcp(const char *host, int port)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, host, &addr.sin_addr) != 1)
  {
    close(fd);
    return -1;
  }
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
  {
    close(fd);
    return -1;
  }
  return fd;
}

static int read_line(int fd, char *buf, size_t cap, int timeout_ms)
{
  size_t o = 0;
  while (o + 1 < cap)
  {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    struct timeval tv = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
    int sel = select(fd + 1, &rfds, NULL, NULL, timeout_ms >= 0 ? &tv : NULL);
    if (sel == 0) return -2;
    if (sel < 0)
    {
      if (errno == EINTR) continue;
      return -1;
    }
    char c;
    ssize_t n = read(fd, &c, 1);
    if (n <= 0) return 0;
    if (c == '\n') break;
    if (c != '\r') buf[o++] = c;
  }
  buf[o] = '\0';
  return (int)o;
}

static void send_cmd(ConsoleState *st, const char *cmd, const char *extra, char *id_out, size_t id_cap)
{
  char line[2048];
  snprintf(id_out, id_cap, "%d", ++st->seq);
  snprintf(line, sizeof(line), "{\"id\":\"%s\",\"cmd\":\"%s\"%s%s}\n",
           id_out, cmd, extra && extra[0] ? "," : "", extra ? extra : "");
  if (write(st->fd, line, strlen(line)) < 0) return;
}

static void json_escape_value(char *out, size_t cap, const char *s)
{
  size_t o = 0;
  if (!out || cap == 0) return;
  out[o++] = '"';
  for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p && o + 8 < cap; p++)
  {
    if (*p == '"' || *p == '\\')
    {
      out[o++] = '\\';
      out[o++] = (char)*p;
    }
    else if (*p == '\n')
    {
      out[o++] = '\\';
      out[o++] = 'n';
    }
    else if (*p == '\r')
    {
      out[o++] = '\\';
      out[o++] = 'r';
    }
    else if (*p == '\t')
    {
      out[o++] = '\\';
      out[o++] = 't';
    }
    else if (*p < 0x20)
    {
      o += (size_t)snprintf(out + o, cap - o, "\\u%04x", *p);
    }
    else
    {
      out[o++] = (char)*p;
    }
  }
  if (o + 2 <= cap) out[o++] = '"';
  out[o] = '\0';
}

static const char *json_value_start(const char *json, const char *key)
{
  static char pattern[96];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *p = strstr(json, pattern);
  if (!p) return NULL;
  p += strlen(pattern);
  while (*p && isspace((unsigned char)*p)) p++;
  if (*p != ':') return NULL;
  p++;
  while (*p && isspace((unsigned char)*p)) p++;
  return p;
}

static int json_int(const char *json, const char *key, int fallback)
{
  const char *p = json_value_start(json, key);
  return p ? atoi(p) : fallback;
}

static long long json_ll(const char *json, const char *key, long long fallback)
{
  const char *p = json_value_start(json, key);
  return p ? atoll(p) : fallback;
}

static void json_str(const char *json, const char *key, char *out, size_t cap)
{
  const char *p = json_value_start(json, key);
  if (!p || *p != '"' || !out || cap == 0)
  {
    if (out && cap) out[0] = '\0';
    return;
  }
  p++;
  size_t o = 0;
  while (*p && *p != '"' && o + 1 < cap)
  {
    if (*p == '\\' && p[1]) p++;
    out[o++] = *p++;
  }
  out[o] = '\0';
}

static void update_from_json(ConsoleState *st, const char *json)
{
  st->calls_today = json_int(json, "calls_today", st->calls_today);
  st->posts_today = json_int(json, "posts_today", st->posts_today);
  st->emails_today = json_int(json, "emails_today", st->emails_today);
  st->newusers_today = json_int(json, "newusers_today", st->newusers_today);
  st->feedback_today = json_int(json, "feedback_today", st->feedback_today);
  st->uploads_today = json_int(json, "uploads_today", st->uploads_today);
  st->downloads_today = json_int(json, "downloads_today", st->downloads_today);
  st->minutes_today = json_int(json, "minutes_today", st->minutes_today);
  st->errors = json_int(json, "errors", st->errors);
  st->ul_kb_today = json_ll(json, "ul_kb_today", st->ul_kb_today);
  st->dl_kb_today = json_ll(json, "dl_kb_today", st->dl_kb_today);
  st->total_calls = json_int(json, "total_calls", st->total_calls);
  st->total_posts = json_int(json, "total_posts", st->total_posts);
  st->total_uploads = json_int(json, "total_uploads", st->total_uploads);
  st->total_downloads = json_int(json, "total_downloads", st->total_downloads);
  st->days_online = json_int(json, "days_online", st->days_online);
  st->total_users = json_int(json, "total_users", st->total_users);
  st->mail_waiting = json_int(json, "mail_waiting", st->mail_waiting);
  st->disk_free_kb = json_ll(json, "disk_free_kb", st->disk_free_kb);
  json_str(json, "os", st->os, sizeof(st->os));
  const char *nodes = strstr(json, "\"nodes\"");
  if (nodes) snprintf(st->nodes_json, sizeof(st->nodes_json), "%s", nodes);
}

static int request(ConsoleState *st, const char *cmd, const char *extra, char *resp, size_t cap)
{
  char id[32], line[65536];
  send_cmd(st, cmd, extra, id, sizeof(id));
  for (;;)
  {
    int n = read_line(st->fd, line, sizeof(line), 5000);
    if (n <= 0) return n;
    if (strstr(line, "\"event\":\"snapshot\"")) update_from_json(st, line);
    char rid[32];
    json_str(line, "id", rid, sizeof(rid));
    if (!strcmp(rid, id))
    {
      snprintf(resp, cap, "%s", line);
      update_from_json(st, line);
      return n;
    }
  }
}

static void draw_header(ConsoleState *st)
{
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  char timestr[16], datestr[16];
  strftime(timestr, sizeof(timestr), "%I:%M %p", tm);
  strftime(datestr, sizeof(datestr), "%m-%d-%Y", tm);
  set_colors(st);
  printf(COLOR_BOLD);
  gotoxy(2, 1);
  printf("%-10s", timestr);
  int namelen = (int)strlen(st->cfg.bbs_name);
  gotoxy((80 - namelen) / 2, 1);
  printf("%s", st->cfg.bbs_name);
  gotoxy(70, 1);
  printf("%s", datestr);
  printf(COLOR_RESET);
}

static void draw_dashboard(ConsoleState *st)
{
  char buf[32];
  printf(CLEAR_SCREEN HIDE_CURSOR);
  draw_header(st);
  set_colors(st);
  draw_titled_box(1, 2, 20, 8, "Today's Stats");
  set_fg(11);
  gotoxy(3, 3); printf("Calls     %7d", st->calls_today);
  gotoxy(3, 4); printf("Posts     %7d", st->posts_today);
  gotoxy(3, 5); printf("Email     %7d", st->emails_today);
  gotoxy(3, 6); printf("Newusers  %7d", st->newusers_today);
  gotoxy(3, 7); printf("Uploads   %7d", st->uploads_today);
  gotoxy(3, 8); printf("Downloads %7d", st->downloads_today);

  set_colors(st);
  draw_titled_box(21, 2, 20, 8, "Sys Averages");
  int days = st->days_online > 0 ? st->days_online : 1;
  set_fg(11);
  gotoxy(23, 3); printf("Calls   %8.1f", (double)st->total_calls / days);
  gotoxy(23, 4); printf("Posts   %8.1f", (double)st->total_posts / days);
  gotoxy(23, 5); printf("Uploads %8.1f", (double)st->total_uploads / days);
  gotoxy(23, 6); printf("Downlds %8.1f", (double)st->total_downloads / days);
  gotoxy(23, 7); printf("Users   %8d", st->total_users);
  gotoxy(23, 8); printf("Days    %8d", st->days_online);

  set_colors(st);
  draw_titled_box(41, 2, 20, 8, "Sys Totals");
  set_fg(11);
  gotoxy(43, 3); printf("Calls   %8d", st->total_calls);
  gotoxy(43, 4); printf("Posts   %8d", st->total_posts);
  gotoxy(43, 5); printf("Uploads %8d", st->total_uploads);
  gotoxy(43, 6); printf("Downlds %8d", st->total_downloads);
  format_bytes(st->ul_kb_today * 1024, buf, sizeof(buf));
  gotoxy(43, 7); printf("UL Today%8s", buf);
  format_bytes(st->dl_kb_today * 1024, buf, sizeof(buf));
  gotoxy(43, 8); printf("DL Today%8s", buf);

  set_colors(st);
  draw_titled_box(61, 2, 19, 8, "Other Info");
  set_fg(11);
  gotoxy(63, 3); printf("Node    %7d", 0);
  gotoxy(63, 4); printf("OS      %7.7s", st->os[0] ? st->os : "Unknown");
  gotoxy(63, 5); printf("Errors  %7d", st->errors);
  gotoxy(63, 6); printf("Mail    %7d", st->mail_waiting);
  format_bytes(st->disk_free_kb * 1024, buf, sizeof(buf));
  gotoxy(63, 7); printf("Free    %7s", buf);
  gotoxy(63, 8); printf("Feedbk  %7d", st->feedback_today);

  set_colors(st);
  draw_titled_box(1, 10, 79, 7, "Node Matrix [@=inspect]");
  set_fg(15);
  gotoxy(6, 11); printf("0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F");
  for (int row = 0; row < 4; row++)
  {
    gotoxy(3, 12 + row);
    set_fg(15); printf("%c ", 'A' + row);
    set_fg(11); printf(BOX_V);
    for (int col = 0; col < 16; col++)
    {
      int node = row * 16 + col + 1;
      char needle[32];
      snprintf(needle, sizeof(needle), "\"node\":%d", node);
      const char *p = strstr(st->nodes_json, needle);
      char ch = st->cfg.wfc_status_idle_char;
      if (p)
      {
        char status[32];
        json_str(p, "status", status, sizeof(status));
        if (!strcmp(status, "logging_in")) ch = st->cfg.wfc_status_logging_char;
        else if (!strcmp(status, "chat") || !strcmp(status, "sysop_chat")) ch = st->cfg.wfc_status_chat_char;
        else if (!strcmp(status, "online") || !strcmp(status, "active")) ch = st->cfg.wfc_status_online_char;
        else if (!strcmp(status, "locked")) ch = 'L';
      }
      set_fg(ch == st->cfg.wfc_status_idle_char ? 8 : 10);
      printf(" %c ", ch);
    }
    set_fg(11); printf(BOX_V);
  }

  set_colors(st);
  printf(COLOR_BOLD);
  gotoxy(32, 17); printf("Waiting For Call");
  printf(COLOR_RESET);
  set_colors(st);
  draw_box(1, 18, 79, 6);
  set_fg(11);
  gotoxy(3, 19); printf("[S]ystem [F]ile base [C]allers [!]Validate [@]Inspect [Q]uit");
  gotoxy(3, 20); printf("[U]ser   [B]Msg Base  [L]ogs    [Z]History  [N]odes   [D]rop shell");
  gotoxy(3, 21); printf("[E]vents [W]rite mail [R]ead    [K]ick node [b]roadcast [ ]Local logon");
  set_fg(8);
  gotoxy(1, 24); printf("F4=refresh @<pos>=inspect k<n>=kick l<n>=lock u<n>=unlock b=broadcast");
  fflush(stdout);
}

static void wait_key(void)
{
  gotoxy(1, 24);
  set_fg(8);
  printf("Press any key to continue...");
  fflush(stdout);
  char c;
  while (read(STDIN_FILENO, &c, 1) <= 0) usleep(10000);
}

static void show_text_response(ConsoleState *st, const char *title, const char *cmd)
{
  char resp[65536];
  request(st, cmd, "", resp, sizeof(resp));
  printf(CLEAR_SCREEN SHOW_CURSOR COLOR_RESET "\n  %s\n  ===============\n\n%s\n", title, resp);
  wait_key();
}

static void raw_forward_until_passthrough_end(ConsoleState *st)
{
  char sock_line[8192];
  size_t sock_len = 0;
  enable_raw();
  for (;;)
  {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    FD_SET(st->fd, &rfds);
    int maxfd = st->fd > STDIN_FILENO ? st->fd : STDIN_FILENO;
    int sel = select(maxfd + 1, &rfds, NULL, NULL, NULL);
    if (sel < 0)
    {
      if (errno == EINTR) continue;
      break;
    }
    if (FD_ISSET(STDIN_FILENO, &rfds))
    {
      char buf[512];
      ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
      if (n <= 0) break;
      if (write(st->fd, buf, (size_t)n) < 0) break;
    }
    if (FD_ISSET(st->fd, &rfds))
    {
      char buf[512];
      ssize_t n = read(st->fd, buf, sizeof(buf));
      if (n <= 0) break;
      for (ssize_t i = 0; i < n; i++)
      {
        char ch = buf[i];
        if (sock_len == 0 && ch != '{')
        {
          fwrite(&ch, 1, 1, stdout);
          fflush(stdout);
          continue;
        }
        if (sock_len + 1 < sizeof(sock_line))
          sock_line[sock_len++] = ch;
        if (ch == '\n')
        {
          sock_line[sock_len] = '\0';
          if (strstr(sock_line, "\"event\":\"passthrough.end\""))
          {
            enable_raw();
            return;
          }
          fwrite(sock_line, 1, sock_len, stdout);
          fflush(stdout);
          sock_len = 0;
        }
      }
      if (sock_len > 0 && sock_len + 1 == sizeof(sock_line))
      {
        fwrite(sock_line, 1, sock_len, stdout);
        fflush(stdout);
        sock_len = 0;
      }
    }
  }
  enable_raw();
}

static void passthrough(ConsoleState *st, const char *action)
{
  char resp[2048], extra[192], actionj[96];
  json_escape_value(actionj, sizeof(actionj), action);
  snprintf(extra, sizeof(extra), "\"action\":%s", actionj);
  if (request(st, "menu.session.start", extra, resp, sizeof(resp)) <= 0 || strstr(resp, "\"ok\":false"))
    return;
  disable_raw();
  char line[8192];
  while (read_line(st->fd, line, sizeof(line), -1) > 0)
  {
    if (strstr(line, "\"event\":\"passthrough.begin\"")) break;
    if (strstr(line, "\"event\":\"passthrough.end\""))
    {
      enable_raw();
      return;
    }
    break;
  }
  raw_forward_until_passthrough_end(st);
}

static void shell_passthrough(ConsoleState *st)
{
  char id[32], line[8192];
  send_cmd(st, "shell.run", "", id, sizeof(id));
  int passthrough_started = 0;
  while (read_line(st->fd, line, sizeof(line), -1) > 0)
  {
    if (strstr(line, "\"event\":\"passthrough.begin\""))
    {
      passthrough_started = 1;
      raw_forward_until_passthrough_end(st);
      break;
    }
    if (strstr(line, "\"event\":\"passthrough.end\"")) break;
    char rid[32];
    json_str(line, "id", rid, sizeof(rid));
    if (!strcmp(rid, id))
    {
      if (strstr(line, "\"ok\":false")) printf("%s\n", line);
      if (!passthrough_started) break;
      continue;
    }
    printf("%s\n", line);
  }
  (void)passthrough_started;
}

static int prompt_line_raw(const char *prompt, char *out, size_t cap)
{
  gotoxy(1, 23);
  printf(CSI "2K");
  set_fg(15);
  printf("%s", prompt);
  fflush(stdout);
  size_t o = 0;
  while (o + 1 < cap)
  {
    char c;
    if (read(STDIN_FILENO, &c, 1) <= 0)
    {
      usleep(10000);
      continue;
    }
    if (c == '\r' || c == '\n') break;
    if (c == 27) { o = 0; break; }
    if ((c == 127 || c == 8) && o > 0)
    {
      o--;
      printf("\b \b");
      fflush(stdout);
      continue;
    }
    if (c >= 32 && c < 127)
    {
      out[o++] = c;
      putchar(c);
      fflush(stdout);
    }
  }
  out[o] = '\0';
  return (int)o;
}

static void handle_key(ConsoleState *st, char c)
{
  char resp[65536], extra[1024], input[512];
  char raw = c;
  c = (char)toupper((unsigned char)c);
  switch (c)
  {
  case 'Q':
    disable_raw();
    exit(0);
  case 'C': show_text_response(st, "Recent Callers", "callers.list"); break;
  case 'L': show_text_response(st, "System Logs", "logs.tail"); break;
  case 'N': show_text_response(st, "Nodes", "nodes.list"); break;
  case 'Z': show_text_response(st, "History", "history.list"); break;
  case 'S': show_text_response(st, "System Status", "system.status"); break;
  case 'U': passthrough(st, "useredit"); break;
  case 'F': passthrough(st, "fileadmin"); break;
  case 'B': passthrough(st, "areaadmin"); break;
  case 'E': passthrough(st, "eventeditor"); break;
  case 'W': passthrough(st, "messages"); break;
  case 'R': passthrough(st, "messages"); break;
  case '!': passthrough(st, "validatefiles"); break;
  case ' ': passthrough(st, "who"); break;
  case 'D':
    shell_passthrough(st);
    break;
  case '@':
    if (prompt_line_raw("Node number to inspect: ", input, sizeof(input)) > 0)
    {
      snprintf(extra, sizeof(extra), "\"node\":%d", atoi(input));
      request(st, "node.inspect", extra, resp, sizeof(resp));
      printf(CLEAR_SCREEN SHOW_CURSOR COLOR_RESET "\n%s\n", resp);
      wait_key();
    }
    break;
  case 'K':
    if (prompt_line_raw("Node to kick: ", input, sizeof(input)) > 0)
    {
      snprintf(extra, sizeof(extra), "\"node\":%d", atoi(input));
      request(st, "node.kick", extra, resp, sizeof(resp));
    }
    break;
  default:
    if (raw == 'b')
    {
      if (prompt_line_raw("Broadcast: ", input, sizeof(input)) > 0)
      {
        char msgj[768];
        json_escape_value(msgj, sizeof(msgj), input);
        snprintf(extra, sizeof(extra), "\"message\":%s", msgj);
        request(st, "broadcast.send", extra, resp, sizeof(resp));
      }
      break;
    }
    if (raw == 'l')
    {
      if (prompt_line_raw("Node to lock: ", input, sizeof(input)) > 0)
      {
        snprintf(extra, sizeof(extra), "\"node\":%d", atoi(input));
        request(st, "node.lock", extra, resp, sizeof(resp));
      }
      break;
    }
    if (raw == 'u')
    {
      if (prompt_line_raw("Node to unlock: ", input, sizeof(input)) > 0)
      {
        snprintf(extra, sizeof(extra), "\"node\":%d", atoi(input));
        request(st, "node.unlock", extra, resp, sizeof(resp));
      }
      break;
    }
    break;
  }
}

static void usage(const char *prog)
{
  fprintf(stderr, "Usage: %s [-c conf/mutineer.conf] [--host 127.0.0.1] [--port 2931] [--user sysop]\n", prog);
}

int main(int argc, char **argv)
{
  const char *cfg_path = "conf/mutineer.conf";
  const char *host = NULL;
  const char *user = "sysop";
  int port = 0;
  for (int i = 1; i < argc; i++)
  {
    if ((!strcmp(argv[i], "-c") || !strcmp(argv[i], "--config")) && i + 1 < argc) cfg_path = argv[++i];
    else if (!strcmp(argv[i], "--host") && i + 1 < argc) host = argv[++i];
    else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--user") && i + 1 < argc) user = argv[++i];
    else { usage(argv[0]); return 1; }
  }

  ConsoleState st;
  memset(&st, 0, sizeof(st));
  if (!cfg_load(cfg_path, &st.cfg))
  {
    fprintf(stderr, "Failed to load config: %s\n", cfg_path);
    return 2;
  }
  if (!host) host = st.cfg.console_bind[0] ? st.cfg.console_bind : "127.0.0.1";
  if (port <= 0) port = st.cfg.console_port > 0 ? st.cfg.console_port : 2931;
  st.fd = connect_tcp(host, port);
  if (st.fd < 0)
  {
    fprintf(stderr, "Failed to connect to console service %s:%d\n", host, port);
    return 3;
  }

  char password[128];
  printf("Console user [%s]: ", user);
  fflush(stdout);
  char userbuf[64];
  if (fgets(userbuf, sizeof(userbuf), stdin) && userbuf[0] != '\n')
  {
    userbuf[strcspn(userbuf, "\r\n")] = '\0';
    user = userbuf;
  }
  char *pw = getpass("Password: ");
  snprintf(password, sizeof(password), "%s", pw ? pw : "");

  char resp[65536], extra[512], id[32], userj[96], passj[256];
  send_cmd(&st, "hello", "", id, sizeof(id));
  read_line(st.fd, resp, sizeof(resp), 5000);
  json_escape_value(userj, sizeof(userj), user);
  json_escape_value(passj, sizeof(passj), password);
  snprintf(extra, sizeof(extra), "\"user\":%s,\"password\":%s", userj, passj);
  if (request(&st, "login", extra, resp, sizeof(resp)) <= 0 || strstr(resp, "\"ok\":false"))
  {
    fprintf(stderr, "Console login failed: %s\n", resp);
    close(st.fd);
    return 4;
  }

  signal(SIGINT, SIG_IGN);
  enable_raw();
  char refresh_id[32];
  send_cmd(&st, "stats.get", "", refresh_id, sizeof(refresh_id));
  while (read_line(st.fd, resp, sizeof(resp), 100) > 0)
    update_from_json(&st, resp);

  for (;;)
  {
    draw_dashboard(&st);
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    int sel = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
    if (sel > 0)
    {
      char c;
      if (read(STDIN_FILENO, &c, 1) > 0) handle_key(&st, c);
    }
    request(&st, "stats.get", "", resp, sizeof(resp));
    request(&st, "nodes.list", "", resp, sizeof(resp));
  }
}
