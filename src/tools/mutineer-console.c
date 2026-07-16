#include "bbs_config.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <notcurses/notcurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

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

static struct notcurses *g_nc;
static struct termios g_passthrough_termios;
static bool g_passthrough_raw;

static void disable_raw(void)
{
  if (g_nc) notcurses_stop(g_nc);
  g_nc = NULL;
}

static void enable_raw(void)
{
  if (g_nc) return;
  const notcurses_options opts = {
    .loglevel = NCLOGLEVEL_SILENT,
    .flags = NCOPTION_SUPPRESS_BANNERS,
  };
  g_nc = notcurses_core_init(&opts, stdout);
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

static void plane_colors(struct ncplane *n, unsigned fr, unsigned fg, unsigned fb,
                         unsigned br, unsigned bg, unsigned bb)
{
  ncplane_set_fg_rgb8(n, fr, fg, fb);
  ncplane_set_bg_rgb8(n, br, bg, bb);
}

static struct ncplane *panel(struct ncplane *parent, int y, int x, int h, int w,
                             const char *title, unsigned r, unsigned g, unsigned b)
{
  const ncplane_options opts = {.y = y, .x = x, .rows = h, .cols = w, .name = title};
  struct ncplane *n = ncplane_create(parent, &opts);
  if (!n) return NULL;
  plane_colors(n, r, g, b, 8, 18, 32);
  ncplane_set_base(n, " ", NCSTYLE_NONE, ncplane_channels(n));
  ncplane_rounded_box(n, NCSTYLE_BOLD, ncplane_channels(n), (unsigned)h - 1, (unsigned)w - 1, 0);
  ncplane_set_styles(n, NCSTYLE_BOLD);
  ncplane_printf_yx(n, 0, 2, " %s ", title);
  ncplane_set_styles(n, NCSTYLE_NONE);
  return n;
}

static void metric(struct ncplane *n, int y, const char *label, const char *value)
{
  unsigned h, w;
  ncplane_dim_yx(n, &h, &w);
  (void)h;
  plane_colors(n, 115, 205, 255, 8, 18, 32);
  ncplane_printf_yx(n, y, 2, "%s", label);
  plane_colors(n, 245, 250, 255, 8, 18, 32);
  int x = (int)w - (int)strlen(value) - 2;
  ncplane_printf_yx(n, y, x > 2 ? x : 2, "%s", value);
}

static const char *node_status(ConsoleState *st, int node, bool *active)
{
  char needle[32], status[32] = {0};
  snprintf(needle, sizeof(needle), "\"node\":%d", node);
  const char *p = strstr(st->nodes_json, needle);
  *active = p != NULL;
  if (!p) return "·";
  json_str(p, "status", status, sizeof(status));
  if (!strcmp(status, "locked")) return "L";
  if (!strcmp(status, "chat") || !strcmp(status, "sysop_chat")) return "C";
  if (!strcmp(status, "logging_in")) return "…";
  return "●";
}

static void fill_stats_panel(struct ncplane *n, ConsoleState *st, int kind)
{
  char v[32];
  int days = st->days_online > 0 ? st->days_online : 1;
  if (kind == 0) {
    snprintf(v, sizeof(v), "%d", st->calls_today); metric(n, 2, "Calls", v);
    snprintf(v, sizeof(v), "%d", st->posts_today); metric(n, 3, "Posts", v);
    snprintf(v, sizeof(v), "%d", st->newusers_today); metric(n, 4, "New crew", v);
    snprintf(v, sizeof(v), "%d / %d", st->uploads_today, st->downloads_today); metric(n, 5, "Up / down", v);
  } else if (kind == 1) {
    snprintf(v, sizeof(v), "%.1f", (double)st->total_calls / days); metric(n, 2, "Calls / day", v);
    snprintf(v, sizeof(v), "%.1f", (double)st->total_posts / days); metric(n, 3, "Posts / day", v);
    snprintf(v, sizeof(v), "%d", st->total_users); metric(n, 4, "Crew", v);
    snprintf(v, sizeof(v), "%d", st->days_online); metric(n, 5, "Days online", v);
  } else if (kind == 2) {
    snprintf(v, sizeof(v), "%d", st->total_calls); metric(n, 2, "Calls", v);
    snprintf(v, sizeof(v), "%d", st->total_posts); metric(n, 3, "Posts", v);
    snprintf(v, sizeof(v), "%d", st->total_uploads); metric(n, 4, "Uploads", v);
    snprintf(v, sizeof(v), "%d", st->total_downloads); metric(n, 5, "Downloads", v);
  } else {
    metric(n, 2, "Platform", st->os[0] ? st->os : "Unknown");
    snprintf(v, sizeof(v), "%d", st->mail_waiting); metric(n, 3, "Waiting mail", v);
    snprintf(v, sizeof(v), "%d", st->errors); metric(n, 4, "Errors", v);
    format_bytes(st->disk_free_kb * 1024, v, sizeof(v)); metric(n, 5, "Disk free", v);
  }
}

static void draw_dashboard(ConsoleState *st)
{
  if (!g_nc) return;
  struct ncplane *std = notcurses_stdplane(g_nc);
  unsigned rows, cols;
  ncplane_dim_yx(std, &rows, &cols);
  ncplane_erase(std);
  plane_colors(std, 215, 235, 255, 3, 10, 22);
  ncplane_set_base(std, " ", NCSTYLE_NONE, ncplane_channels(std));

  if (rows < 22 || cols < 70) {
    ncplane_set_styles(std, NCSTYLE_BOLD);
    ncplane_printf_aligned(std, (int)rows / 2 - 1, NCALIGN_CENTER, "MUTINEER COMMAND DECK");
    ncplane_set_styles(std, NCSTYLE_NONE);
    ncplane_printf_aligned(std, (int)rows / 2 + 1, NCALIGN_CENTER, "Resize to at least 70 x 22  •  current %u x %u", cols, rows);
    notcurses_render(g_nc);
    return;
  }

  time_t now = time(NULL);
  char clockbuf[32];
  strftime(clockbuf, sizeof(clockbuf), "%a %d %b  %H:%M:%S", localtime(&now));
  plane_colors(std, 94, 234, 212, 3, 10, 22);
  ncplane_set_styles(std, NCSTYLE_BOLD);
  ncplane_printf_yx(std, 0, 2, "◈ MUTINEER // COMMAND DECK");
  ncplane_printf_yx(std, 0, (int)cols - (int)strlen(clockbuf) - 2, "%s", clockbuf);
  plane_colors(std, 164, 180, 200, 3, 10, 22);
  ncplane_set_styles(std, NCSTYLE_NONE);
  ncplane_printf_yx(std, 1, 2, "%s", st->cfg.bbs_name);
  ncplane_printf_yx(std, 1, (int)cols - 25, "LINK  ● SECURE  LIVE");

  int gap = 1;
  int card_y = 3;
  int card_h = 7;
  int card_cols = cols >= 100 ? 4 : 2;
  int card_rows = card_cols == 4 ? 1 : 2;
  int card_w = ((int)cols - 4 - gap * (card_cols - 1)) / card_cols;
  const char *titles[] = {"TODAY", "VELOCITY", "ALL TIME", "SYSTEM"};
  const unsigned accents[][3] = {{94,234,212},{96,165,250},{192,132,252},{251,191,36}};
  for (int i = 0; i < 4; i++) {
    int row = i / card_cols, col = i % card_cols;
    struct ncplane *p = panel(std, card_y + row * (card_h + 1), 2 + col * (card_w + gap),
                              card_h, card_w, titles[i], accents[i][0], accents[i][1], accents[i][2]);
    if (p) fill_stats_panel(p, st, i);
  }

  int node_y = card_y + card_rows * (card_h + 1);
  int footer_h = 5;
  int node_h = (int)rows - node_y - footer_h - 1;
  struct ncplane *nodes = panel(std, node_y, 2, node_h, (int)cols - 4, "NODE MATRIX", 94, 234, 212);
  if (nodes) {
    int usable = (int)cols - 8;
    int per_row = usable / 5;
    if (per_row > 16) per_row = 16;
    if (per_row < 8) per_row = 8;
    for (int node = 1; node <= 64; node++) {
      int y = 2 + (node - 1) / per_row;
      if (y >= node_h - 1) break;
      int x = 2 + ((node - 1) % per_row) * 5;
      bool active;
      const char *status = node_status(st, node, &active);
      if (active) plane_colors(nodes, 94, 234, 212, 8, 18, 32);
      else plane_colors(nodes, 69, 85, 108, 8, 18, 32);
      ncplane_printf_yx(nodes, y, x, "%02d%s", node, status);
    }
  }

  int fy = (int)rows - footer_h;
  plane_colors(std, 180, 200, 225, 10, 22, 38);
  ncplane_set_base(std, " ", NCSTYLE_NONE, ncplane_channels(std));
  ncplane_printf_yx(std, fy, 2, " S SYSTEM   C CALLERS   N NODES   L LOGS   Z HISTORY   @ INSPECT   K KICK ");
  ncplane_printf_yx(std, fy + 1, 2, " U USERS    F FILES     B AREAS   E EVENTS  W MAIL     D SHELL     Q QUIT ");
  plane_colors(std, 110, 130, 155, 10, 22, 38);
  ncplane_printf_yx(std, fy + 3, 2, "b broadcast   l lock   u unlock   SPACE local session   auto-refresh %dms", st->cfg.wfc_refresh_ms);
  notcurses_render(g_nc);
}

static void wait_key(void)
{
  ncinput ni;
  if (g_nc) notcurses_get_blocking(g_nc, &ni);
}

static void show_text_response(ConsoleState *st, const char *title, const char *cmd)
{
  char resp[65536];
  request(st, cmd, "", resp, sizeof(resp));
  if (!g_nc) return;
  struct ncplane *std = notcurses_stdplane(g_nc);
  unsigned rows, cols;
  ncplane_dim_yx(std, &rows, &cols);
  ncplane_erase(std);
  plane_colors(std, 225, 235, 248, 3, 10, 22);
  ncplane_set_base(std, " ", NCSTYLE_NONE, ncplane_channels(std));
  ncplane_set_styles(std, NCSTYLE_BOLD);
  ncplane_printf_yx(std, 1, 2, "◈ %s", title);
  ncplane_set_styles(std, NCSTYLE_NONE);
  ncplane_printf_yx(std, 3, 2, "%.*s", (int)((rows - 6) * (cols - 4)), resp);
  plane_colors(std, 94, 234, 212, 3, 10, 22);
  ncplane_printf_yx(std, (int)rows - 2, 2, "Press any key to return");
  notcurses_render(g_nc);
  wait_key();
}

static void raw_forward_until_passthrough_end(ConsoleState *st)
{
  char sock_line[8192];
  size_t sock_len = 0;
  if (tcgetattr(STDIN_FILENO, &g_passthrough_termios) == 0) {
    struct termios raw = g_passthrough_termios;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    g_passthrough_raw = tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0;
  }
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
            goto done;
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
done:
  if (g_passthrough_raw) tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_passthrough_termios);
  g_passthrough_raw = false;
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
  enable_raw();
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
      enable_raw();
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
  if (!g_nc || !out || cap == 0) return 0;
  struct ncplane *std = notcurses_stdplane(g_nc);
  unsigned rows, cols;
  ncplane_dim_yx(std, &rows, &cols);
  size_t o = 0;
  while (o + 1 < cap)
  {
    plane_colors(std, 245, 250, 255, 17, 34, 54);
    ncplane_erase_region(std, (int)rows - 3, 0, 1, -1);
    ncplane_printf_yx(std, (int)rows - 3, 2, "%s%s█", prompt, out);
    notcurses_render(g_nc);
    ncinput ni;
    uint32_t key = notcurses_get_blocking(g_nc, &ni);
    if (key == NCKEY_ENTER || key == '\r' || key == '\n') break;
    if (key == NCKEY_ESC) { o = 0; break; }
    if ((key == NCKEY_BACKSPACE || key == 127 || key == 8) && o > 0) o--;
    else if (key >= 32 && key < 127) out[o++] = (char)key;
    out[o] = '\0';
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
      if (g_nc) {
        struct ncplane *std = notcurses_stdplane(g_nc);
        unsigned rows, cols;
        ncplane_dim_yx(std, &rows, &cols);
        ncplane_erase(std);
        plane_colors(std, 225, 235, 248, 3, 10, 22);
        ncplane_set_base(std, " ", NCSTYLE_NONE, ncplane_channels(std));
        ncplane_printf_yx(std, 1, 2, "◈ NODE INSPECTOR");
        ncplane_printf_yx(std, 3, 2, "%.*s", (int)((rows - 6) * (cols - 4)), resp);
        ncplane_printf_yx(std, (int)rows - 2, 2, "Press any key to return");
        notcurses_render(g_nc);
        wait_key();
      }
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
  if (!g_nc)
  {
    fprintf(stderr, "Failed to initialize notcurses (check TERM and terminal capabilities).\n");
    close(st.fd);
    return 5;
  }
  atexit(disable_raw);
  char refresh_id[32];
  send_cmd(&st, "stats.get", "", refresh_id, sizeof(refresh_id));
  while (read_line(st.fd, resp, sizeof(resp), 100) > 0)
    update_from_json(&st, resp);

  int refresh_ms = st.cfg.wfc_refresh_ms > 0 ? st.cfg.wfc_refresh_ms : 1000;
  struct timespec last_refresh;
  clock_gettime(CLOCK_MONOTONIC, &last_refresh);
  for (;;)
  {
    draw_dashboard(&st);
    ncinput ni;
    uint32_t key = notcurses_get_nblock(g_nc, &ni);
    if (key > 0 && key < 256) handle_key(&st, (char)key);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed = (now.tv_sec - last_refresh.tv_sec) * 1000L +
                   (now.tv_nsec - last_refresh.tv_nsec) / 1000000L;
    if (elapsed >= refresh_ms) {
      request(&st, "stats.get", "", resp, sizeof(resp));
      request(&st, "nodes.list", "", resp, sizeof(resp));
      last_refresh = now;
    }
    usleep(25000);
  }
}
