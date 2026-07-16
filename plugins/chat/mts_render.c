#include "mts_render.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static size_t decode(const unsigned char *s, uint32_t *cp) {
  if (s[0] < 0x80) {
    *cp = s[0];
    return 1;
  }
  if (s[0] >= 0xc2 && s[0] <= 0xdf && (s[1] & 0xc0) == 0x80) {
    *cp = ((s[0] & 31u) << 6) | (s[1] & 63u);
    return 2;
  }
  if (s[0] >= 0xe0 && s[0] <= 0xef && s[1] && s[2] && (s[1] & 0xc0) == 0x80 &&
      (s[2] & 0xc0) == 0x80 && !(s[0] == 0xe0 && s[1] < 0xa0) &&
      !(s[0] == 0xed && s[1] >= 0xa0)) {
    *cp = ((s[0] & 15u) << 12) | ((s[1] & 63u) << 6) | (s[2] & 63u);
    return 3;
  }
  if (s[0] >= 0xf0 && s[0] <= 0xf4 && s[1] && s[2] && s[3] &&
      (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80 && (s[3] & 0xc0) == 0x80 &&
      !(s[0] == 0xf0 && s[1] < 0x90) && !(s[0] == 0xf4 && s[1] >= 0x90)) {
    *cp = ((s[0] & 7u) << 18) | ((s[1] & 63u) << 12) | ((s[2] & 63u) << 6) |
          (s[3] & 63u);
    return 4;
  }
  *cp = '?';
  return 0;
}
static int cells(uint32_t cp) {
  if ((cp >= 0x300 && cp <= 0x36f) || (cp >= 0x1ab0 && cp <= 0x1aff) ||
      (cp >= 0x1dc0 && cp <= 0x1dff) || (cp >= 0xfe00 && cp <= 0xfe0f))
    return 0;
  if (cp >= 0x1100 &&
      (cp <= 0x115f || cp == 0x2329 || cp == 0x232a ||
       (cp >= 0x2e80 && cp <= 0xa4cf) || (cp >= 0xac00 && cp <= 0xd7a3) ||
       (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0xfe10 && cp <= 0xfe6f) ||
       (cp >= 0xff00 && cp <= 0xff60) || (cp >= 0x1f300 && cp <= 0x1faff)))
    return 2;
  return 1;
}
size_t mts_utf8_sanitize(char *d, size_t cap, const char *src) {
  size_t o = 0;
  if (!d || !cap)
    return 0;
  if (!src)
    src = "";
  for (size_t i = 0; src[i] && o + 1 < cap;) {
    unsigned char c = (unsigned char)src[i];
    if (c == 0x1b || c == 0x7f || (c < 0x20 && c != '\t')) {
      i++;
      continue;
    }
    uint32_t cp;
    size_t n = decode((const unsigned char *)src + i, &cp);
    if (!n) {
      d[o++] = '?';
      i++;
      continue;
    }
    if (o + n >= cap)
      break;
    memcpy(d + o, src + i, n);
    o += n;
    i += n;
  }
  d[o] = 0;
  return o;
}
size_t mts_display_width(const char *s) {
  size_t w = 0;
  for (size_t i = 0; s && s[i];) {
    if ((unsigned char)s[i] == 0x1b) {
      i++;
      while (s[i] && s[i++] != 'm')
        ;
      continue;
    }
    uint32_t cp;
    size_t n = decode((const unsigned char *)s + i, &cp);
    w += cells(cp);
    i += n ? n : 1;
  }
  return w;
}
size_t mts_clip_width(char *d, size_t cap, const char *s, size_t max) {
  size_t o = 0, w = 0;
  if (!d || !cap)
    return 0;
  for (size_t i = 0; s && s[i] && o + 1 < cap;) {
    uint32_t cp;
    size_t n = decode((const unsigned char *)s + i, &cp);
    if (!n) {
      cp = '?';
      n = 1;
    }
    int cw = cells(cp);
    if (w + (size_t)cw > max || o + n >= cap)
      break;
    if (cp == '?' && (unsigned char)s[i] >= 0x80)
      d[o++] = '?';
    else {
      memcpy(d + o, s + i, n);
      o += n;
    }
    i += n;
    w += (size_t)cw;
  }
  d[o] = 0;
  return o;
}
static void palette(const mts_session_t *s, const char **accent,
                    const char **alert, const char **reset) {
  *accent = *alert = *reset = "";
  if (!s->ansi || !s->prefs.color || !strcmp(s->prefs.theme, "mono"))
    return;
  *reset = "\033[0m";
  if (!strcmp(s->prefs.theme, "amber"))
    *accent = "\033[1;33m";
  else if (!strcmp(s->prefs.theme, "high-contrast"))
    *accent = "\033[1;37;44m";
  else if (!strcmp(s->prefs.theme, "ocean"))
    *accent = "\033[1;34m";
  else
    *accent = "\033[1;36m";
  *alert = "\033[1;33m";
}
void mts_render_event(const mts_session_t *s, const mts_event_t *e, char *dst,
                      size_t cap) {
  char raw[1200], clipped[1200], stamp[16] = "";
  const char *a, *alert, *reset;
  palette(s, &a, &alert, &reset);
  if (s->prefs.timestamps) {
    time_t t = (time_t)e->timestamp;
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(stamp, sizeof stamp, "[%H:%M] ", &tm);
  }
  if (e->type == MTS_ANNOUNCEMENT)
    snprintf(raw, sizeof raw, "%s%s[ANNOUNCEMENT] %s%s", stamp, alert, e->text,
             reset);
  else if (e->type == MTS_PRIVATE || e->type == MTS_AFK_RESPONSE)
    snprintf(raw, sizeof raw, "%s%s[%s] %s: %s%s%s", stamp, a,
             e->type == MTS_AFK_RESPONSE ? "AFK" : "private", e->sender,
             e->text, s->prefs.bell ? "\a" : "", reset);
  else if (e->type == MTS_ACTION)
    snprintf(raw, sizeof raw, "%s* %s %s", stamp, e->sender, e->text);
  else if (e->type == MTS_PRESENCE)
    snprintf(raw, sizeof raw, "%s*** %s %s ***", stamp, e->sender, e->text);
  else if (e->type == MTS_MODERATION || e->type == MTS_SYSTEM)
    snprintf(raw, sizeof raw, "%s%s[MTS] %s%s", stamp, alert, e->text, reset);
  else if (e->room_id && e->room_id != s->room_id)
    snprintf(raw, sizeof raw, "%s[watch:%llu] %s%s%s: %s", stamp,
             (unsigned long long)e->room_id, a, e->sender, reset, e->text);
  else if (e->type == MTS_DIRECTED)
    snprintf(raw, sizeof raw, "%s%s%s%s > %s: %s", stamp, a, e->sender, reset,
             e->target, e->text);
  else
    snprintf(raw, sizeof raw, "%s%s%s%s: %s", stamp, a, e->sender, reset,
             e->text);
  mts_clip_width(clipped, sizeof clipped, raw,
                 s->term_cols >= 20 ? s->term_cols : 80);
  snprintf(dst, cap, "\r\n%s\r\n", clipped);
}
static void pager_write(mts_session_t *s, const char *text) {
  if (MTS_HOST && MTS_HOST->io && MTS_HOST->io->write)
    MTS_HOST->io->write(s->host_session, text, strlen(text));
}
void mts_pager_begin(mts_pager_t *p, mts_session_t *s, const char *heading) {
  *p = (mts_pager_t){.session = s,
                     .page_rows = s->term_rows >= 8 ? s->term_rows - 3 : 21};
  if (heading && *heading) {
    pager_write(s, heading);
    p->rows_used = 1;
  }
}
int mts_pager_line(mts_pager_t *p, const char *line) {
  if (!p || p->stopped)
    return 0;
  if (p->rows_used >= p->page_rows) {
    pager_write(p->session, "-- More -- [Enter/q] ");
    char answer[16] = {0};
    bbs_rc_t rc = MTS_HOST->io->readline(p->session->host_session, answer,
                                         sizeof answer, 1);
    pager_write(p->session, "\r\n");
    if (rc != BBS_OK || answer[0] == 'q' || answer[0] == 'Q') {
      p->stopped = 1;
      return 0;
    }
    p->rows_used = 0;
  }
  char clipped[1400];
  mts_clip_width(clipped, sizeof clipped, line,
                 p->session->term_cols >= 20 ? p->session->term_cols : 80);
  pager_write(p->session, clipped);
  if (!strstr(clipped, "\n"))
    pager_write(p->session, "\r\n");
  p->rows_used++;
  return 1;
}
