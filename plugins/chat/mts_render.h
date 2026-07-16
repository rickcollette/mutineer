#pragma once
#include "mts.h"

size_t mts_utf8_sanitize(char *dst, size_t cap, const char *src);
size_t mts_display_width(const char *text);
size_t mts_clip_width(char *dst, size_t cap, const char *src, size_t columns);
void mts_render_event(const mts_session_t *session, const mts_event_t *event,
                      char *dst, size_t cap);
typedef struct {
  mts_session_t *session;
  unsigned rows_used, page_rows;
  int stopped;
} mts_pager_t;
void mts_pager_begin(mts_pager_t *pager, mts_session_t *session,
                     const char *heading);
int mts_pager_line(mts_pager_t *pager, const char *line);
