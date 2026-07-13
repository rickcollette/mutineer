#define _POSIX_C_SOURCE 200809L
#include "bbs_session.h"
#include <sys/select.h>
#include <sys/socket.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define FSEDIT_MAX_LINES 50
#define FSEDIT_LINE_LEN 78
#define FSEDIT_VIEW_LINES 20

typedef struct
{
  char lines[FSEDIT_MAX_LINES][FSEDIT_LINE_LEN + 1];
  int line_count;
  int cur_line;
  int cur_col;
  int insert_mode;
  int top_line;
} FsEditor;

static int line_len(FsEditor *ed)
{
  return (int)strlen(ed->lines[ed->cur_line]);
}

static void clamp_cursor(FsEditor *ed)
{
  if (ed->line_count < 1) ed->line_count = 1;
  if (ed->cur_line < 0) ed->cur_line = 0;
  if (ed->cur_line >= ed->line_count) ed->cur_line = ed->line_count - 1;
  int len = line_len(ed);
  if (ed->cur_col < 0) ed->cur_col = 0;
  if (ed->cur_col > len) ed->cur_col = len;
  if (ed->cur_line < ed->top_line) ed->top_line = ed->cur_line;
  if (ed->cur_line >= ed->top_line + FSEDIT_VIEW_LINES)
    ed->top_line = ed->cur_line - FSEDIT_VIEW_LINES + 1;
  if (ed->top_line < 0) ed->top_line = 0;
}

static void copy_line(char dst[FSEDIT_LINE_LEN + 1], const char *src)
{
  snprintf(dst, FSEDIT_LINE_LEN + 1, "%s", src ? src : "");
}

static void fsedit_redraw(Session *s, FsEditor *ed)
{
  char buf[256];
  clamp_cursor(ed);
  send_str(s, "\x1b[2J\x1b[H");
  send_str(s, "\x1b[1;36m=== Full-Screen Editor ===\x1b[0m\r\n");
  send_str(s, "\x1b[1;33mCtrl-S=Save  Ctrl-A=Abort  Ctrl-Y=Delete Line  Ctrl-N=Insert Line\x1b[0m\r\n");
  send_str(s, "----------------------------------------------------------------------\r\n");

  int end = ed->top_line + FSEDIT_VIEW_LINES;
  if (end > ed->line_count) end = ed->line_count;
  for (int i = ed->top_line; i < end; i++)
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

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
           (ed->cur_line - ed->top_line) + 4, ed->cur_col + 4);
  send_str(s, buf);
}

static void save_text(FsEditor *ed, char *text_out, size_t text_cap)
{
  if (!text_out || text_cap == 0) return;
  size_t used = 0;
  text_out[0] = '\0';
  for (int i = 0; i < ed->line_count; i++)
  {
    const char *parts[2] = { ed->lines[i], "\r\n" };
    for (int p = 0; p < 2; p++)
    {
      size_t avail = text_cap > used ? text_cap - used - 1 : 0;
      if (avail == 0) return;
      size_t n = strlen(parts[p]);
      if (n > avail) n = avail;
      memcpy(text_out + used, parts[p], n);
      used += n;
      text_out[used] = '\0';
    }
  }
}

int fsedit_edit(Session *s, char *text_out, size_t text_cap)
{
  if (!s || !text_out || text_cap == 0) return -1;
  FsEditor ed;
  memset(&ed, 0, sizeof(ed));
  ed.line_count = 1;
  ed.insert_mode = 1;

  if (text_out[0])
  {
    char *p = text_out;
    ed.line_count = 0;
    while (*p && ed.line_count < FSEDIT_MAX_LINES)
    {
      char *nl = strchr(p, '\n');
      size_t len = nl ? (size_t)(nl - p) : strlen(p);
      if (len > 0 && p[len - 1] == '\r') len--;
      if (len > FSEDIT_LINE_LEN) len = FSEDIT_LINE_LEN;
      memcpy(ed.lines[ed.line_count], p, len);
      ed.lines[ed.line_count][len] = '\0';
      ed.line_count++;
      if (!nl) break;
      p = nl + 1;
    }
    if (ed.line_count == 0) ed.line_count = 1;
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
    if (r <= 0) continue;

    ssize_t n = recv(s->fd, buf, 1, 0);
    if (n <= 0) return -1;
    uint8_t ch = buf[0];

    if (ch == 0x1B)
    {
      n = recv(s->fd, buf, 2, MSG_DONTWAIT);
      if (n == 2 && buf[0] == '[')
      {
        if (buf[1] == 'A' && ed.cur_line > 0) ed.cur_line--;
        else if (buf[1] == 'B' && ed.cur_line < ed.line_count - 1) ed.cur_line++;
        else if (buf[1] == 'C' && ed.cur_col < line_len(&ed)) ed.cur_col++;
        else if (buf[1] == 'D' && ed.cur_col > 0) ed.cur_col--;
        clamp_cursor(&ed);
        fsedit_redraw(s, &ed);
      }
      continue;
    }

    if (ch == 0x13)
    {
      save_text(&ed, text_out, text_cap);
      return 1;
    }
    if (ch == 0x01) return 0;

    if (ch == 0x19)
    {
      if (ed.line_count > 1)
      {
        for (int i = ed.cur_line; i < ed.line_count - 1; i++)
          copy_line(ed.lines[i], ed.lines[i + 1]);
        ed.line_count--;
      }
      else
      {
        ed.lines[0][0] = '\0';
      }
      ed.cur_col = 0;
      clamp_cursor(&ed);
      fsedit_redraw(s, &ed);
      continue;
    }

    if (ch == 0x0E)
    {
      if (ed.line_count < FSEDIT_MAX_LINES)
      {
        for (int i = ed.line_count; i > ed.cur_line + 1; i--)
          copy_line(ed.lines[i], ed.lines[i - 1]);
        ed.line_count++;
        ed.cur_line++;
        ed.lines[ed.cur_line][0] = '\0';
        ed.cur_col = 0;
      }
      clamp_cursor(&ed);
      fsedit_redraw(s, &ed);
      continue;
    }

    if (ch == '\r' || ch == '\n')
    {
      if (ed.line_count < FSEDIT_MAX_LINES)
      {
        char rest[FSEDIT_LINE_LEN + 1];
        copy_line(rest, ed.lines[ed.cur_line] + ed.cur_col);
        ed.lines[ed.cur_line][ed.cur_col] = '\0';
        for (int i = ed.line_count; i > ed.cur_line + 1; i--)
          copy_line(ed.lines[i], ed.lines[i - 1]);
        ed.line_count++;
        ed.cur_line++;
        copy_line(ed.lines[ed.cur_line], rest);
        ed.cur_col = 0;
      }
      clamp_cursor(&ed);
      fsedit_redraw(s, &ed);
      continue;
    }

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
        int prev_len = (int)strlen(ed.lines[ed.cur_line - 1]);
        int cur_len = (int)strlen(ed.lines[ed.cur_line]);
        if (prev_len + cur_len <= FSEDIT_LINE_LEN)
        {
          strncat(ed.lines[ed.cur_line - 1], ed.lines[ed.cur_line],
                  FSEDIT_LINE_LEN - (size_t)prev_len);
          for (int i = ed.cur_line; i < ed.line_count - 1; i++)
            copy_line(ed.lines[i], ed.lines[i + 1]);
          ed.line_count--;
          ed.cur_line--;
          ed.cur_col = prev_len;
        }
      }
      clamp_cursor(&ed);
      fsedit_redraw(s, &ed);
      continue;
    }

    if (ch >= 0x20 && ch < 0x7F)
    {
      char *line = ed.lines[ed.cur_line];
      int len = (int)strlen(line);
      if (len < FSEDIT_LINE_LEN)
      {
        if (ed.insert_mode)
          memmove(line + ed.cur_col + 1, line + ed.cur_col, strlen(line + ed.cur_col) + 1);
        line[ed.cur_col] = (char)ch;
        if (ed.cur_col >= len) line[ed.cur_col + 1] = '\0';
        ed.cur_col++;
        clamp_cursor(&ed);
        fsedit_redraw(s, &ed);
      }
    }
  }
}
