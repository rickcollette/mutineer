#include "bbs_wfc.h"
#include "bbs_session.h"
#include "bbs_log.h"
#include "bbs_process.h"
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <signal.h>
#include <time.h>
#include <termios.h>
#include <ctype.h>

extern volatile sig_atomic_t g_stop;

/* ANSI escape codes */
#define ESC "\033"
#define CSI ESC "["
#define CLEAR_SCREEN CSI "2J" CSI "H"
#define CURSOR_HOME CSI "H"
#define HIDE_CURSOR CSI "?25l"
#define SHOW_CURSOR CSI "?25h"
#define SAVE_CURSOR CSI "s"
#define RESTORE_CURSOR CSI "u"

/* Box drawing characters (UTF-8) */
#define BOX_TL "\u250C" /* top-left */
#define BOX_TR "\u2510" /* top-right */
#define BOX_BL "\u2514" /* bottom-left */
#define BOX_BR "\u2518" /* bottom-right */
#define BOX_H "\u2500"  /* horizontal */
#define BOX_V "\u2502"  /* vertical */
#define BOX_TT "\u252C" /* top-tee */
#define BOX_BT "\u2534" /* bottom-tee */
#define BOX_LT "\u251C" /* left-tee */
#define BOX_RT "\u2524" /* right-tee */
#define BOX_X "\u253C"  /* cross */

/* Color macros */
#define COLOR_RESET CSI "0m"
#define COLOR_BOLD CSI "1m"
#define FG(n) CSI "38;5;" #n "m"
#define BG(n) CSI "48;5;" #n "m"

typedef struct
{
  BbsConfig cfg;
  BbsDb *db;
  pthread_t thread;
  int running;
  time_t last_keypress;
  int blanked;
  WfcStatus status;
  char status_extra[128];
  pthread_mutex_t lock;
} WfcCtx;

static WfcCtx *g_wfc = NULL;

/* Terminal handling */
static struct termios g_orig_termios;
static int g_raw_mode = 0;

static void disable_raw_mode(void)
{
  if (g_raw_mode)
  {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
    g_raw_mode = 0;
  }
}

static void enable_raw_mode(void)
{
  if (g_raw_mode)
    return;
  tcgetattr(STDIN_FILENO, &g_orig_termios);
  atexit(disable_raw_mode);
  struct termios raw = g_orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  g_raw_mode = 1;
}

/* Position cursor */
static void gotoxy(int x, int y)
{
  printf(CSI "%d;%dH", y, x);
}

/* Set foreground color (0-15) */
static void set_fg(int color)
{
  if (color < 8)
  {
    printf(CSI "%dm", 30 + color);
  }
  else
  {
    printf(CSI "%d;1m", 30 + (color - 8));
  }
}

/* Set background color (0-7) */
static void set_bg(int color)
{
  printf(CSI "%dm", 40 + color);
}

/* Set colors from config */
static void set_colors(WfcCtx *ctx)
{
  set_fg(ctx->cfg.wfc_fg_color);
  set_bg(ctx->cfg.wfc_bg_color);
}

/* Draw a box */
static void draw_box(int x, int y, int w, int h)
{
  gotoxy(x, y);
  printf(BOX_TL);
  for (int i = 0; i < w - 2; i++)
    printf(BOX_H);
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
  for (int i = 0; i < w - 2; i++)
    printf(BOX_H);
  printf(BOX_BR);
}

/* Draw box with title */
static void draw_titled_box(int x, int y, int w, int h, const char *title)
{
  draw_box(x, y, w, h);
  if (title && title[0])
  {
    int tlen = (int)strlen(title);
    int tx = x + (w - tlen - 2) / 2;
    gotoxy(tx, y);
    printf(" %s ", title);
  }
}

/* Format bytes to human readable */
static void format_bytes(int64_t bytes, char *out, size_t cap)
{
  if (bytes >= 1073741824LL)
  {
    snprintf(out, cap, "%.1f GB", (double)bytes / 1073741824.0);
  }
  else if (bytes >= 1048576LL)
  {
    snprintf(out, cap, "%.1f MB", (double)bytes / 1048576.0);
  }
  else if (bytes >= 1024LL)
  {
    snprintf(out, cap, "%.1f KB", (double)bytes / 1024.0);
  }
  else
  {
    snprintf(out, cap, "%lld B", (long long)bytes);
  }
}

/* Get disk free space */
static int64_t get_disk_free_kb(const char *path)
{
  struct statvfs st;
  if (statvfs(path ? path : ".", &st) != 0)
    return 0;
  return (int64_t)(st.f_bavail * st.f_frsize) / 1024;
}

/* Get OS name */
static void get_os_name(char *out, size_t cap)
{
  struct utsname un;
  if (uname(&un) == 0)
  {
    snprintf(out, cap, "%s", un.sysname);
  }
  else
  {
    snprintf(out, cap, "Unknown");
  }
}

/* Gather WFC stats */
static void gather_stats(WfcCtx *ctx, WfcStats *stats)
{
  memset(stats, 0, sizeof(*stats));

  if (!ctx || !ctx->db)
    return;

  DbDailyStats daily;
  memset(&daily, 0, sizeof(daily));
  if (db_daily_stats_get(ctx->db, &daily))
  {
    stats->calls_today = daily.calls;
    stats->posts_today = daily.posts;
    stats->emails_today = daily.emails;
    stats->newusers_today = daily.newusers;
    stats->feedback_today = daily.feedback;
    stats->uploads_today = daily.uploads;
    stats->downloads_today = daily.downloads;
    stats->ul_kb_today = daily.ul_kb;
    stats->dl_kb_today = daily.dl_kb;
    stats->minutes_today = daily.minutes;
    stats->errors = daily.errors;
  }

  DbSystemTotals totals;
  memset(&totals, 0, sizeof(totals));
  if (db_system_totals_get(ctx->db, &totals))
  {
    stats->total_calls = totals.total_calls;
    stats->total_posts = totals.total_posts;
    stats->total_uploads = totals.total_uploads;
    stats->total_downloads = totals.total_downloads;
    stats->days_online = totals.days_online;
    stats->total_users = totals.total_users;
  }

  stats->node_num = ctx->cfg.wfc_node_num;
  stats->disk_free_kb = get_disk_free_kb(ctx->cfg.data_path);

  /* Get mail waiting for sysop (user 1) */
  DbUser sysop;
  memset(&sysop, 0, sizeof(sysop));
  if (db_user_fetch(ctx->db, "sysop", &sysop))
  {
    stats->mail_waiting = sysop.smw;
  }
}

/* Draw header: time, BBS name, date */
static void wfc_draw_header(WfcCtx *ctx)
{
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  char timestr[16], datestr[16];
  strftime(timestr, sizeof(timestr), "%I:%M %p", tm);
  strftime(datestr, sizeof(datestr), "%m-%d-%Y", tm);

  set_colors(ctx);
  printf(COLOR_BOLD);
  gotoxy(2, 1);
  printf("%-10s", timestr);

  int namelen = (int)strlen(ctx->cfg.bbs_name);
  gotoxy((80 - namelen) / 2, 1);
  printf("%s", ctx->cfg.bbs_name);

  gotoxy(70, 1);
  printf("%s", datestr);
  printf(COLOR_RESET);
}

/* Draw Today's Stats panel - left column, rows 2-9 */
static void wfc_draw_today_stats(WfcCtx *ctx, WfcStats *stats)
{
  int x = 1, y = 2, w = 20, h = 8;

  set_colors(ctx);
  draw_titled_box(x, y, w, h, "Today's Stats");

  printf(COLOR_RESET);
  set_fg(11); /* cyan */

  gotoxy(x + 2, y + 1);
  printf("Calls     %7d", stats->calls_today);
  gotoxy(x + 2, y + 2);
  printf("Posts     %7d", stats->posts_today);
  gotoxy(x + 2, y + 3);
  printf("Email     %7d", stats->emails_today);
  gotoxy(x + 2, y + 4);
  printf("Newusers  %7d", stats->newusers_today);
  gotoxy(x + 2, y + 5);
  printf("Uploads   %7d", stats->uploads_today);
  gotoxy(x + 2, y + 6);
  printf("Downloads %7d", stats->downloads_today);
}

/* Draw System Averages panel - second column, rows 2-9 */
static void wfc_draw_averages(WfcCtx *ctx, WfcStats *stats)
{
  int x = 21, y = 2, w = 20, h = 8;

  set_colors(ctx);
  draw_titled_box(x, y, w, h, "Sys Averages");

  printf(COLOR_RESET);
  set_fg(11);

  int days = stats->days_online > 0 ? stats->days_online : 1;

  gotoxy(x + 2, y + 1);
  printf("Calls   %8.1f", (double)stats->total_calls / days);
  gotoxy(x + 2, y + 2);
  printf("Posts   %8.1f", (double)stats->total_posts / days);
  gotoxy(x + 2, y + 3);
  printf("Uploads %8.1f", (double)stats->total_uploads / days);
  gotoxy(x + 2, y + 4);
  printf("Downlds %8.1f", (double)stats->total_downloads / days);
  gotoxy(x + 2, y + 5);
  printf("Users   %8d", stats->total_users);
  gotoxy(x + 2, y + 6);
  printf("Days    %8d", stats->days_online);
}

/* Draw System Totals panel - third column, rows 2-9 */
static void wfc_draw_totals(WfcCtx *ctx, WfcStats *stats)
{
  int x = 41, y = 2, w = 20, h = 8;

  set_colors(ctx);
  draw_titled_box(x, y, w, h, "Sys Totals");

  printf(COLOR_RESET);
  set_fg(11);

  gotoxy(x + 2, y + 1);
  printf("Calls   %8d", stats->total_calls);
  gotoxy(x + 2, y + 2);
  printf("Posts   %8d", stats->total_posts);
  gotoxy(x + 2, y + 3);
  printf("Uploads %8d", stats->total_uploads);
  gotoxy(x + 2, y + 4);
  printf("Downlds %8d", stats->total_downloads);

  char buf[16];
  format_bytes(stats->ul_kb_today * 1024, buf, sizeof(buf));
  gotoxy(x + 2, y + 5);
  printf("UL Today%8s", buf);
  format_bytes(stats->dl_kb_today * 1024, buf, sizeof(buf));
  gotoxy(x + 2, y + 6);
  printf("DL Today%8s", buf);
}

/* Draw Other Info panel - fourth column, rows 2-9 */
static void wfc_draw_other_info(WfcCtx *ctx, WfcStats *stats)
{
  int x = 61, y = 2, w = 19, h = 8;

  set_colors(ctx);
  draw_titled_box(x, y, w, h, "Other Info");

  printf(COLOR_RESET);
  set_fg(11);

  char os[16];
  get_os_name(os, sizeof(os));

  gotoxy(x + 2, y + 1);
  printf("Node    %7d", stats->node_num);
  gotoxy(x + 2, y + 2);
  printf("OS      %7s", os);
  gotoxy(x + 2, y + 3);
  printf("Errors  %7d", stats->errors);

  if (stats->mail_waiting > 0)
  {
    set_fg(15); /* bright white for attention */
  }
  gotoxy(x + 2, y + 4);
  printf("Mail    %7d", stats->mail_waiting);
  set_fg(11);

  char buf[16];
  format_bytes(stats->disk_free_kb * 1024, buf, sizeof(buf));
  gotoxy(x + 2, y + 5);
  printf("Free    %7s", buf);
  gotoxy(x + 2, y + 6);
  printf("Feedbk  %7d", stats->feedback_today);
}

/* Convert node_num (1-64) to matrix position (row A-D, col 0-F) */
static void node_to_matrix(int node_num, char *row, char *col)
{
  if (node_num < 1 || node_num > 64)
  {
    *row = '?';
    *col = '?';
    return;
  }
  int idx = node_num - 1;
  *row = 'A' + (idx / 16);
  int c = idx % 16;
  *col = (c < 10) ? ('0' + c) : ('A' + c - 10);
}

/* Convert matrix position to node_num (1-64) */
static int matrix_to_node(char row, char col)
{
  row = toupper(row);
  col = toupper(col);
  if (row < 'A' || row > 'D')
    return -1;
  int r = row - 'A';
  int c;
  if (col >= '0' && col <= '9')
    c = col - '0';
  else if (col >= 'A' && col <= 'F')
    c = 10 + col - 'A';
  else
    return -1;
  return r * 16 + c + 1;
}

/* Get node status character */
static char get_node_status_char(WfcCtx *ctx, const char *status)
{
  if (!status || !status[0] || !strcmp(status, "idle"))
    return ctx->cfg.wfc_status_idle_char;
  if (!strcmp(status, "logging_in"))
    return ctx->cfg.wfc_status_logging_char;
  if (!strcmp(status, "sysop_chat"))
    return ctx->cfg.wfc_status_chat_char;
  if (!strcmp(status, "online") || !strcmp(status, "active"))
    return ctx->cfg.wfc_status_online_char;
  return ctx->cfg.wfc_status_idle_char;
}

/* Get color for node status */
static void set_node_color(WfcCtx *ctx, char status)
{
  if (status == ctx->cfg.wfc_status_idle_char)
  {
    set_fg(8);
  }
  else if (status == ctx->cfg.wfc_status_logging_char)
  {
    set_fg(14);
  }
  else if (status == ctx->cfg.wfc_status_online_char)
  {
    set_fg(10);
  }
  else if (status == ctx->cfg.wfc_status_chat_char)
  {
    set_fg(13);
  }
  else
  {
    set_fg(8);
  }
}

/* Draw 64-position Node Matrix panel - full width, rows 10-16 */
static void wfc_draw_nodes(WfcCtx *ctx)
{
  int x = 1, y = 10, w = 79, h = 7;

  set_colors(ctx);
  draw_titled_box(x, y, w, h, "Node Matrix [@=inspect]");

  printf(COLOR_RESET);

  /* Build node status array */
  char node_status[65];
  memset(node_status, ctx->cfg.wfc_status_idle_char, 64);
  node_status[64] = 0;

  if (ctx && ctx->db)
  {
    DbNode nodes[64];
    memset(nodes, 0, sizeof(nodes));
    int n = db_node_list(ctx->db, nodes, 64);
    for (int i = 0; i < n && i < 64; i++)
    {
      int idx = nodes[i].node_num - 1;
      if (idx >= 0 && idx < 64)
      {
        node_status[idx] = get_node_status_char(ctx, nodes[i].status);
      }
    }
  }

  /* Draw column headers */
  set_fg(15);
  gotoxy(x + 5, y + 1);
  printf("0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F");

  /* Draw rows A-D */
  for (int row = 0; row < 4; row++)
  {
    gotoxy(x + 2, y + 2 + row);
    set_fg(15);
    printf("%c ", 'A' + row);
    set_fg(11);
    printf(BOX_V);

    for (int col = 0; col < 16; col++)
    {
      int idx = row * 16 + col;
      char status = node_status[idx];
      set_node_color(ctx, status);
      printf(" %c ", status);
    }
    set_fg(11);
    printf(BOX_V);

    /* Legend on right side */
    if (row == 0)
    {
      set_fg(8);
      printf("  %c", ctx->cfg.wfc_status_idle_char);
      set_fg(7);
      printf("=Inactive");
    }
    if (row == 1)
    {
      set_fg(14);
      printf("  %c", ctx->cfg.wfc_status_logging_char);
      set_fg(7);
      printf("=Logging in");
    }
    if (row == 2)
    {
      set_fg(10);
      printf("  %c", ctx->cfg.wfc_status_online_char);
      set_fg(7);
      printf("=Active");
    }
    if (row == 3)
    {
      set_fg(13);
      printf("  %c", ctx->cfg.wfc_status_chat_char);
      set_fg(7);
      printf("=SysOp Chat");
    }
  }
}

/* Draw status line - row 17 */
static void wfc_draw_status(WfcCtx *ctx)
{
  set_colors(ctx);
  printf(COLOR_BOLD);

  const char *msg = "Waiting For Call";
  switch (ctx->status)
  {
  case WFC_STATUS_WAITING:
    msg = "Waiting For Call";
    break;
  case WFC_STATUS_INIT_MODEM:
    msg = "Initializing...";
    break;
  case WFC_STATUS_OFF_HOOK:
    msg = "Off Hook";
    break;
  case WFC_STATUS_ANSWERING:
    msg = "Answering...";
    break;
  case WFC_STATUS_LOCAL_LOGON:
    msg = "Local Logon";
    break;
  case WFC_STATUS_EVENT_PENDING:
    msg = "Event Pending";
    break;
  case WFC_STATUS_SHUTDOWN:
    msg = "Shutting Down...";
    break;
  }

  char fullmsg[128];
  if (ctx->status_extra[0])
  {
    snprintf(fullmsg, sizeof(fullmsg), "%s - %s", msg, ctx->status_extra);
  }
  else
  {
    snprintf(fullmsg, sizeof(fullmsg), "%s", msg);
  }

  int len = (int)strlen(fullmsg);
  gotoxy((80 - len) / 2, 17);
  printf("%s", fullmsg);
  printf(COLOR_RESET);
}

/* Draw command menu - rows 18-24 */
static void wfc_draw_commands(WfcCtx *ctx)
{
  int x = 1, y = 18, w = 79, h = 6;

  set_colors(ctx);
  draw_box(x, y, w, h);

  printf(COLOR_RESET);
  set_fg(11);

  /* Row 1 */
  gotoxy(x + 2, y + 1);
  printf("[");
  set_fg(14);
  printf("S");
  set_fg(11);
  printf("]ystem   [");
  set_fg(14);
  printf("F");
  set_fg(11);
  printf("]ile base  [");
  set_fg(14);
  printf("C");
  set_fg(11);
  printf("]allers  [");
  set_fg(14);
  printf("!");
  set_fg(11);
  printf("]Validate  [");
  set_fg(14);
  printf("@");
  set_fg(11);
  printf("]Inspect  [");
  set_fg(14);
  printf("Q");
  set_fg(11);
  printf("]uit");

  /* Row 2 */
  gotoxy(x + 2, y + 2);
  printf("[");
  set_fg(14);
  printf("U");
  set_fg(11);
  printf("]ser     [");
  set_fg(14);
  printf("B");
  set_fg(11);
  printf("]Msg Base  [");
  set_fg(14);
  printf("L");
  set_fg(11);
  printf("]ogs     [");
  set_fg(14);
  printf("Z");
  set_fg(11);
  printf("]History   [");
  set_fg(14);
  printf("N");
  set_fg(11);
  printf("]odes     [");
  set_fg(14);
  printf("D");
  set_fg(11);
  printf("]rop shell");

  /* Row 3 */
  gotoxy(x + 2, y + 3);
  printf("[");
  set_fg(14);
  printf("E");
  set_fg(11);
  printf("]vents   [");
  set_fg(14);
  printf("W");
  set_fg(11);
  printf("]rite mail [");
  set_fg(14);
  printf("R");
  set_fg(11);
  printf("]ead     [");
  set_fg(14);
  printf("K");
  set_fg(11);
  printf("]ick node  [");
  set_fg(14);
  printf("b");
  set_fg(11);
  printf("]roadcast [");
  set_fg(14);
  printf(" ");
  set_fg(11);
  printf("]Local logon");
}

/* Draw help line at bottom - row 24 */
static void wfc_draw_help(WfcCtx *ctx)
{
  (void)ctx;
  set_fg(8); /* dark gray */
  gotoxy(1, 24);
  printf("F1=quit  F4=refresh  @<pos>=inspect node (e.g. @A0)  k<n>=kick  b=broadcast");
  printf(COLOR_RESET);
}

/* Full screen redraw */
static void wfc_draw_full(WfcCtx *ctx)
{
  WfcStats stats;
  gather_stats(ctx, &stats);

  printf(CLEAR_SCREEN);
  printf(HIDE_CURSOR);

  wfc_draw_header(ctx);
  wfc_draw_today_stats(ctx, &stats);
  wfc_draw_averages(ctx, &stats);
  wfc_draw_totals(ctx, &stats);
  wfc_draw_other_info(ctx, &stats);
  wfc_draw_nodes(ctx);
  wfc_draw_status(ctx);
  wfc_draw_commands(ctx);
  wfc_draw_help(ctx);

  fflush(stdout);
}

/* Partial update (time only) */
static void wfc_update_time(WfcCtx *ctx)
{
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  char timestr[16];
  strftime(timestr, sizeof(timestr), "%I:%M %p", tm);

  printf(SAVE_CURSOR);
  set_colors(ctx);
  printf(COLOR_BOLD);
  gotoxy(2, 1);
  printf("%-10s", timestr);
  printf(COLOR_RESET);
  printf(RESTORE_CURSOR);
  fflush(stdout);
}

/* Show callers list */
static void wfc_show_callers(WfcCtx *ctx)
{
  printf(CLEAR_SCREEN);
  printf(SHOW_CURSOR);
  set_fg(15);
  printf("\n  Today's Callers\n");
  printf("  ===============\n\n");

  /* Get recent logins from logs or history */
  set_fg(11);
  printf("  (Caller log not yet implemented)\n\n");

  set_fg(8);
  printf("  Press any key to continue...\n");
  fflush(stdout);

  /* Wait for keypress */
  char c;
  (void)read(STDIN_FILENO, &c, 1);

  ctx->last_keypress = time(NULL);
}

/* Show logs */
static void wfc_show_logs(WfcCtx *ctx)
{
  printf(CLEAR_SCREEN);
  printf(SHOW_CURSOR);
  set_fg(15);
  printf("\n  System Logs\n");
  printf("  ===========\n\n");

  /* Read last 20 lines of log file */
  FILE *f = fopen(ctx->cfg.logs_path, "r");
  if (f)
  {
    char lines[20][256];
    int count = 0;
    while (fgets(lines[count % 20], sizeof(lines[0]), f))
    {
      count++;
    }
    fclose(f);

    set_fg(11);
    int start = count > 20 ? count - 20 : 0;
    for (int i = start; i < count; i++)
    {
      printf("  %s", lines[i % 20]);
    }
  }
  else
  {
    set_fg(11);
    printf("  (No log file found)\n");
  }

  printf("\n");
  set_fg(8);
  printf("  Press any key to continue...\n");
  fflush(stdout);

  char c;
  (void)read(STDIN_FILENO, &c, 1);

  ctx->last_keypress = time(NULL);
}

/* Show node listing */
static void wfc_show_nodes(WfcCtx *ctx)
{
  printf(CLEAR_SCREEN);
  printf(SHOW_CURSOR);
  set_fg(15);
  printf("\n  Node Listing\n");
  printf("  ============\n\n");

  set_fg(11);
  printf("  Node  Pos   Status     User           IP               Activity\n");
  printf("  ----  ----  ---------  -------------  ---------------  -------------------------\n");

  DbNode nodes[64];
  int n = db_node_list(ctx->db, nodes, 64);

  for (int i = 0; i < n; i++)
  {
    char row, col;
    node_to_matrix(nodes[i].node_num, &row, &col);
    printf("  %-4d  %c%c    %-9s  %-13s  %-15s  %s\n",
           nodes[i].node_num,
           row, col,
           nodes[i].status,
           nodes[i].handle[0] ? nodes[i].handle : "(none)",
           nodes[i].ip,
           nodes[i].activity);
  }

  if (n == 0)
  {
    printf("  (No active nodes)\n");
  }

  printf("\n");
  set_fg(8);
  printf("  Press any key to continue...\n");
  fflush(stdout);

  char c;
  (void)read(STDIN_FILENO, &c, 1);

  ctx->last_keypress = time(NULL);
}

/* Inspect a specific node by matrix position */
static void wfc_inspect_node(WfcCtx *ctx, int node_num)
{
  printf(CLEAR_SCREEN);
  printf(SHOW_CURSOR);

  char row, col;
  node_to_matrix(node_num, &row, &col);

  set_fg(15);
  printf("\n  Node %d (%c%c) Details\n", node_num, row, col);
  printf("  ====================\n\n");

  /* Find the node */
  DbNode nodes[64];
  int n = db_node_list(ctx->db, nodes, 64);
  DbNode *found = NULL;

  for (int i = 0; i < n; i++)
  {
    if (nodes[i].node_num == node_num)
    {
      found = &nodes[i];
      break;
    }
  }

  if (!found)
  {
    set_fg(11);
    printf("  Status:   Inactive\n");
    printf("  User:     (none)\n");
    printf("  IP:       -\n");
    printf("  Activity: -\n");
  }
  else
  {
    set_fg(11);
    printf("  Status:   %s\n", found->status);
    printf("  User:     %s\n", found->handle[0] ? found->handle : "(none)");
    printf("  IP:       %s\n", found->ip[0] ? found->ip : "-");
    printf("  Activity: %s\n", found->activity[0] ? found->activity : "-");

    /* Get session for more details */
    Session *s = online_get_node(node_num);
    if (s)
    {
      printf("\n  Session Details:\n");
      printf("  ----------------\n");
      printf("  User ID:     %d\n", s->user.id);
      printf("  Handle:      %s\n", s->user.handle);
      printf("  Level:       %d\n", s->user.level);
      printf("  Time Left:   %d min\n", s->time_left_min);
      printf("  Connected:   %s\n", s->ip);
    }
  }

  printf("\n");
  set_fg(14);
  printf("  Actions:\n");
  set_fg(11);
  printf("  [K] Kick user    [S] Sysop chat    [V] View session    [ESC] Back\n\n");

  set_fg(8);
  printf("  Choice: ");
  fflush(stdout);

  /* Wait for action */
  char c;
  while (1)
  {
    if (read(STDIN_FILENO, &c, 1) <= 0)
    {
      usleep(10000);
      continue;
    }
    if (c == 27)
      break; /* ESC */

    c = toupper(c);
    if (c == 'K')
    {
      /* Kick */
      Session *s = online_get_node(node_num);
      if (s)
      {
        s->alive = 0;
        db_node_upsert(ctx->db, node_num, s->user.id, "kicked", "wfc", s->ip);
        log_info("WFC kicked node %d from inspect", node_num);
        printf("\n  Node kicked.\n");
      }
      else
      {
        printf("\n  No active session to kick.\n");
      }
      usleep(500000);
      break;
    }
    else if (c == 'S')
    {
      /* Sysop chat */
      Session *s = online_get_node(node_num);
      if (s)
      {
        db_node_upsert(ctx->db, node_num, s->user.id, "sysop_chat", "Sysop chat", s->ip);
        printf("\n  Entering sysop chat with node %d...\n", node_num);
        printf("  (Sysop chat not yet implemented)\n");
        usleep(1000000);
      }
      else
      {
        printf("\n  No active session for chat.\n");
        usleep(500000);
      }
      break;
    }
    else if (c == 'V')
    {
      /* View session output */
      printf("\n  (Session view not yet implemented)\n");
      usleep(1000000);
      break;
    }
  }

  ctx->last_keypress = time(NULL);
}

/* Prompt for matrix position and inspect node */
static void wfc_node_inspect_prompt(WfcCtx *ctx)
{
  printf(SHOW_CURSOR);
  gotoxy(1, 23);
  set_fg(15);
  printf("Node position (e.g. A0, B5, CF): ");
  fflush(stdout);

  char pos[4] = {0};
  int i = 0;

  while (i < 2)
  {
    char c;
    if (read(STDIN_FILENO, &c, 1) <= 0)
    {
      usleep(10000);
      continue;
    }
    if (c == 27)
    {
      pos[0] = 0;
      break;
    } /* ESC cancels */
    if (c == '\n' || c == '\r')
      break;
    if (c == 127 || c == 8)
    {
      if (i > 0)
      {
        i--;
        printf("\b \b");
        fflush(stdout);
      }
      continue;
    }
    c = toupper(c);
    if (i == 0 && c >= 'A' && c <= 'D')
    {
      pos[i++] = c;
      putchar(c);
      fflush(stdout);
    }
    else if (i == 1 && ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')))
    {
      pos[i++] = c;
      putchar(c);
      fflush(stdout);
    }
  }
  pos[i] = 0;

  if (pos[0] && pos[1])
  {
    int node_num = matrix_to_node(pos[0], pos[1]);
    if (node_num > 0)
    {
      wfc_inspect_node(ctx, node_num);
    }
  }

  ctx->last_keypress = time(NULL);
}

/* Show history */
static void wfc_show_history(WfcCtx *ctx)
{
  printf(CLEAR_SCREEN);
  printf(SHOW_CURSOR);
  set_fg(15);
  printf("\n  System History\n");
  printf("  ==============\n\n");

  set_fg(11);
  printf("  Date        Calls  Posts  Email  UL    DL    Users\n");
  printf("  ----------  -----  -----  -----  ----  ----  -----\n");

  DbDailyStats history[30];
  int n = db_history_list(ctx->db, history, 30);

  for (int i = 0; i < n; i++)
  {
    printf("  %-10s  %5d  %5d  %5d  %4d  %4d  %5d\n",
           history[i].date,
           history[i].calls,
           history[i].posts,
           history[i].emails,
           history[i].uploads,
           history[i].downloads,
           history[i].newusers);
  }

  if (n == 0)
  {
    printf("  (No history recorded)\n");
  }

  printf("\n");
  set_fg(8);
  printf("  Press any key to continue...\n");
  fflush(stdout);

  char c;
  (void)read(STDIN_FILENO, &c, 1);

  ctx->last_keypress = time(NULL);
}

/* Broadcast message */
static void wfc_broadcast(WfcCtx *ctx)
{
  printf(SHOW_CURSOR);
  gotoxy(1, 23);
  set_fg(15);
  printf("Broadcast: ");
  fflush(stdout);

  /* Read line */
  char msg[128] = {0};
  int pos = 0;
  while (pos < 120)
  {
    char c;
    if (read(STDIN_FILENO, &c, 1) <= 0)
    {
      usleep(10000);
      continue;
    }
    if (c == '\n' || c == '\r')
      break;
    if (c == 27)
    {
      msg[0] = 0;
      break;
    } /* ESC cancels */
    if (c == 127 || c == 8)
    {
      if (pos > 0)
      {
        pos--;
        printf("\b \b");
        fflush(stdout);
      }
      continue;
    }
    if (c >= 32 && c < 127)
    {
      msg[pos++] = c;
      putchar(c);
      fflush(stdout);
    }
  }
  msg[pos] = 0;

  if (msg[0])
  {
    char buf[256];
    snprintf(buf, sizeof(buf), "\r\n[Broadcast from %s] %s\r\n",
             ctx->cfg.sysop_name[0] ? ctx->cfg.sysop_name : "Sysop", msg);
    online_broadcast(buf);
    log_info("WFC broadcast: %s", msg);
  }

  ctx->last_keypress = time(NULL);
}

/* Kick a node */
static void wfc_kick_node(WfcCtx *ctx, int node)
{
  Session *s = online_get_node(node);
  if (s)
  {
    s->alive = 0;
    db_node_upsert(ctx->db, node, s->user.id, "kicked", "wfc", s->ip);
    log_info("WFC kicked node %d", node);
  }
  ctx->last_keypress = time(NULL);
}

/* Handle command */
static int wfc_handle_cmd(WfcCtx *ctx, const char *cmd)
{
  if (!cmd || !cmd[0])
    return 0;

  char c = toupper(cmd[0]);

  switch (c)
  {
  case 'Q':
    return 1; /* quit */

  case 'C':
    wfc_show_callers(ctx);
    return 0;

  case 'L':
    wfc_show_logs(ctx);
    return 0;

  case 'N':
    wfc_show_nodes(ctx);
    return 0;

  case 'Z':
    wfc_show_history(ctx);
    return 0;

  case 'B':
    if (cmd[1])
    {
      char buf[256];
      snprintf(buf, sizeof(buf), "\r\n[Broadcast] %s\r\n", cmd + 1);
      online_broadcast(buf);
    }
    else
    {
      wfc_broadcast(ctx);
    }
    return 0;

  case 'K':
    if (cmd[1])
    {
      int node = atoi(cmd + 1);
      if (node > 0)
        wfc_kick_node(ctx, node);
    }
    return 0;

  case '@':
    /* Node matrix inspection */
    wfc_node_inspect_prompt(ctx);
    return 0;

  case ' ':
    /* Local logon - signal to main */
    pthread_mutex_lock(&ctx->lock);
    ctx->status = WFC_STATUS_LOCAL_LOGON;
    pthread_mutex_unlock(&ctx->lock);
    return 0;

  case 'D':
    if (!ctx->cfg.wfc_shell_enabled || !ctx->cfg.wfc_shell_command[0])
    {
      log_audit("wfc", "wfc_shell_denied", "disabled");
      printf("\r\n[WFC] Shell escape is disabled by configuration.\r\n");
      fflush(stdout);
      return 0;
    }
    printf(CLEAR_SCREEN);
    printf(SHOW_CURSOR);
    printf(COLOR_RESET);
    printf("\nStarting configured shell command. Exit it to return.\n\n");
    fflush(stdout);
    disable_raw_mode();
    char errbuf[256] = {0};
    char **argv = NULL;
    if (!bbs_argv_parse_template(ctx->cfg.wfc_shell_command, NULL, &argv, errbuf, sizeof(errbuf)))
    {
      log_audit("wfc", "wfc_shell_rejected", errbuf);
      printf("Shell command rejected: %s\r\n", errbuf);
    }
    else
    {
      BbsProcessResult result;
      log_audit("wfc", "wfc_shell_start", ctx->cfg.wfc_shell_command);
      bbs_exec_argv(argv, "wfc-shell", NULL, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO,
                    0, &result, errbuf, sizeof(errbuf));
      log_audit("wfc", "wfc_shell_end", errbuf[0] ? errbuf : "complete");
      bbs_argv_free(argv);
    }
    enable_raw_mode();
    ctx->last_keypress = time(NULL);
    return 0;

  default:
    printf("\r\n[WFC] Unknown key. Q=Quit C=Callers L=Log N=Nodes Z=History "
           "B=Broadcast K=Kick @=Inspect SPACE=Logon D=Shell\r\n");
    fflush(stdout);
    break;
  }

  ctx->last_keypress = time(NULL);
  return 0;
}

/* Read escape sequence */
static int read_escape_seq(char *buf, int max)
{
  int pos = 0;
  buf[pos++] = 27; /* ESC */

  /* Read with short timeout */
  struct timeval tv = {.tv_sec = 0, .tv_usec = 50000};
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);

  while (pos < max - 1)
  {
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0)
      break;
    char c;
    if (read(STDIN_FILENO, &c, 1) <= 0)
      break;
    buf[pos++] = c;
    if (c >= 'A' && c <= 'Z')
      break; /* end of sequence */
    if (c >= 'a' && c <= 'z')
      break;
    if (c == '~')
      break;
    tv.tv_usec = 10000;
  }
  buf[pos] = 0;
  return pos;
}

/* WFC main thread */
static void *wfc_thread(void *arg)
{
  WfcCtx *ctx = (WfcCtx *)arg;
  int refresh_ms = ctx->cfg.wfc_refresh_ms > 0 ? ctx->cfg.wfc_refresh_ms : 1000;
  time_t last_full_draw = 0;
  time_t last_time_update = 0;

  enable_raw_mode();
  ctx->last_keypress = time(NULL);
  ctx->blanked = 0;

  wfc_draw_full(ctx);
  last_full_draw = time(NULL);

  while (!g_stop && ctx->running)
  {
    time_t now = time(NULL);

    /* Check for screen blank */
    if (ctx->cfg.wfc_blank_sec > 0 && !ctx->blanked)
    {
      if (now - ctx->last_keypress >= ctx->cfg.wfc_blank_sec)
      {
        printf(CLEAR_SCREEN);
        printf(HIDE_CURSOR);
        fflush(stdout);
        ctx->blanked = 1;
      }
    }

    /* Update time every second */
    if (!ctx->blanked && now != last_time_update)
    {
      wfc_update_time(ctx);
      last_time_update = now;
    }

    /* Full refresh periodically */
    if (!ctx->blanked && (now - last_full_draw) * 1000 >= refresh_ms)
    {
      wfc_draw_full(ctx);
      last_full_draw = now;
    }

    /* Check for input */
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};

    if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0)
    {
      char c;
      if (read(STDIN_FILENO, &c, 1) > 0)
      {
        ctx->last_keypress = time(NULL);

        /* Wake from blank */
        if (ctx->blanked)
        {
          ctx->blanked = 0;
          wfc_draw_full(ctx);
          last_full_draw = time(NULL);
          continue;
        }

        /* Handle escape sequences */
        if (c == 27)
        {
          char seq[16];
          int len = read_escape_seq(seq, sizeof(seq));

          /* F1 = quit */
          if (len >= 3 && seq[1] == 'O' && seq[2] == 'P')
          {
            g_stop = 1;
            break;
          }
          /* F4 = refresh */
          if (len >= 3 && seq[1] == 'O' && seq[2] == 'S')
          {
            wfc_draw_full(ctx);
            last_full_draw = time(NULL);
            continue;
          }
          /* ESC alone - ignore */
          continue;
        }

        /* Regular command */
        char cmd[2] = {c, 0};
        if (wfc_handle_cmd(ctx, cmd))
        {
          g_stop = 1;
          break;
        }

        /* Redraw after command */
        wfc_draw_full(ctx);
        last_full_draw = time(NULL);
      }
    }
  }

  /* Cleanup */
  printf(CLEAR_SCREEN);
  printf(SHOW_CURSOR);
  printf(COLOR_RESET);
  fflush(stdout);
  disable_raw_mode();

  return NULL;
}

/* Public API */

void wfc_start(const BbsConfig *cfg, BbsDb *db)
{
  if (!cfg || !db || !cfg->wfc_enabled)
    return;
  if (g_wfc)
    return; /* already running */
  if (!isatty(STDIN_FILENO))
    return; /* no TTY, skip WFC screen */

  g_wfc = (WfcCtx *)calloc(1, sizeof(WfcCtx));
  if (!g_wfc)
    return;

  g_wfc->cfg = *cfg;
  g_wfc->db = db;
  g_wfc->running = 1;
  g_wfc->status = WFC_STATUS_WAITING;
  pthread_mutex_init(&g_wfc->lock, NULL);

  if (pthread_create(&g_wfc->thread, NULL, wfc_thread, g_wfc) != 0)
  {
    pthread_mutex_destroy(&g_wfc->lock);
    free(g_wfc);
    g_wfc = NULL;
    return;
  }
}

void wfc_stop(void)
{
  WfcCtx *ctx = g_wfc;
  if (!ctx)
    return;
  ctx->running = 0;
  if (!pthread_equal(pthread_self(), ctx->thread))
    pthread_join(ctx->thread, NULL);
  pthread_mutex_destroy(&ctx->lock);
  g_wfc = NULL;
  free(ctx);
}

void wfc_set_status(WfcStatus status, const char *extra)
{
  if (!g_wfc)
    return;
  pthread_mutex_lock(&g_wfc->lock);
  g_wfc->status = status;
  if (extra)
  {
    snprintf(g_wfc->status_extra, sizeof(g_wfc->status_extra), "%s", extra);
  }
  else
  {
    g_wfc->status_extra[0] = 0;
  }
  pthread_mutex_unlock(&g_wfc->lock);
}

void wfc_refresh(void)
{
  if (!g_wfc)
    return;
  g_wfc->last_keypress = time(NULL);
  if (g_wfc->blanked)
  {
    g_wfc->blanked = 0;
  }
}

int wfc_is_running(void)
{
  return g_wfc && g_wfc->running;
}
