#include "bbs_menu_template.h"
#include "bbs_session.h"
#include "bbs_mci.h"
#include "bbs_util.h"
#include "bbs_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void tmpl_copy(char *dst, size_t cap, const char *src)
{
  if (!dst || cap == 0) return;
  size_t n = src ? strlen(src) : 0;
  if (n >= cap) n = cap - 1;
  if (n > 0) memcpy(dst, src, n);
  dst[n] = '\0';
}
#include <sys/stat.h>

/* Placeholder tokens */
#define TOKEN_MENU_ITEMS "%%MENU_ITEMS%%"
#define TOKEN_MENU_TITLE "%%MENU_TITLE%%"
#define TOKEN_MENU_NAME  "%%MENU_NAME%%"
#define TOKEN_PROMPT     "%%PROMPT%%"
#define TOKEN_USERNAME   "%%USERNAME%%"
#define TOKEN_NODE       "%%NODE%%"
#define TOKEN_TIME_LEFT  "%%TIME_LEFT%%"

static TemplateLogFn g_log_fn = NULL;

void template_set_log_fn(TemplateLogFn fn) {
  g_log_fn = fn;
}

static void log_template_error(const char* menu_name, const char* msg) {
  if (g_log_fn) {
    g_log_fn(menu_name, msg);
  } else {
    log_info("template[%s]: %s", menu_name ? menu_name : "?", msg);
  }
}

const char* template_error_str(TemplateError err) {
  switch (err) {
    case TMPL_OK: return "OK";
    case TMPL_ERR_FILE_NOT_FOUND: return "template file not found";
    case TMPL_ERR_FILE_READ: return "failed to read template file";
    case TMPL_ERR_NO_MENU_ITEMS: return "missing %%MENU_ITEMS%% token";
    case TMPL_ERR_MULTIPLE_MENU_ITEMS: return "multiple %%MENU_ITEMS%% lines found";
    case TMPL_ERR_NO_PROMPT: return "missing %%PROMPT%% token (required for stock templates)";
    case TMPL_ERR_UNKNOWN_PLACEHOLDER: return "unknown placeholder found";
    case TMPL_ERR_LINE_OVERFLOW: return "line exceeds 80 visible columns";
    case TMPL_ERR_BODY_WIDTH_INVALID: return "body width too small for safe rendering";
    case TMPL_ERR_ANSI_IN_ASC: return "ANSI escape sequences in .asc file";
    case TMPL_ERR_MALFORMED: return "malformed template structure";
  }
  return "unknown error";
}

/* ============================================================================
 * Core Helper Functions
 * ============================================================================ */

size_t visible_len(const char* str) {
  if (!str) return 0;
  
  size_t len = 0;
  size_t i = 0;
  
  while (str[i]) {
    if (str[i] == '\x1b' && str[i+1] == '[') {
      /* ANSI CSI sequence: skip until letter */
      i += 2;
      while (str[i] && !isalpha((unsigned char)str[i])) {
        i++;
      }
      if (str[i]) i++;  /* skip the terminating letter */
    } else if ((unsigned char)str[i] < 0x20 && str[i] != '\t') {
      /* Skip other control characters except tab */
      i++;
    } else {
      len++;
      i++;
    }
  }
  
  return len;
}

bool file_exists(const char* path) {
  if (!path) return false;
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

char* slurp_file(const char* path, size_t* out_len) {
  if (!path) return NULL;
  
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  
  if (size < 0 || size > 1024 * 1024) {  /* 1MB limit */
    fclose(f);
    return NULL;
  }
  
  char* buf = (char*)malloc((size_t)size + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  
  size_t read = fread(buf, 1, (size_t)size, f);
  fclose(f);
  
  buf[read] = '\0';
  if (out_len) *out_len = read;
  
  return buf;
}

bool resolve_menu_template_path(const Menu* m, const struct Session* s,
                                char* out, size_t cap) {
  if (!m || !out || cap == 0) return false;
  
  const char* ext = (s && s->ansi) ? ".ans" : ".asc";
  
  /* If @ART is specified in the menu, use it */
  if (m->art_file[0]) {
    const char* art = m->art_file;
    
    /* Check if art_file has extension */
    const char* dot = strrchr(art, '.');
    if (dot && (strcmp(dot, ".ans") == 0 || strcmp(dot, ".asc") == 0)) {
      /* Use exact path */
      snprintf(out, cap, "menus/%s", art);
    } else {
      /* Append appropriate extension */
      snprintf(out, cap, "menus/%s%s", art, ext);
    }
    
    if (file_exists(out)) return true;
    
    /* Try without menus/ prefix */
    if (dot && (strcmp(dot, ".ans") == 0 || strcmp(dot, ".asc") == 0)) {
      snprintf(out, cap, "%s", art);
    } else {
      snprintf(out, cap, "%s%s", art, ext);
    }
    
    if (file_exists(out)) return true;
  }
  
  /* Default: menus/<menuname>.ans or .asc */
  snprintf(out, cap, "menus/%s%s", m->name, ext);
  
  return file_exists(out);
}

size_t collect_visible_menu_items(const Menu* m, const struct Session* s,
                                  const MenuItem** out, size_t cap) {
  if (!m || !out || cap == 0) return 0;
  
  size_t count = 0;
  for (size_t i = 0; i < m->count && count < cap; i++) {
    if (menu_item_visible(&m->items[i], s)) {
      out[count++] = &m->items[i];
    }
  }
  
  return count;
}

/* ============================================================================
 * Template Validation
 * ============================================================================ */

static bool is_known_placeholder(const char* name, size_t len) {
  /* Check against known placeholders (without %% delimiters) */
  const char* known[] = {
    "MENU_ITEMS", "MENU_TITLE", "MENU_NAME", "PROMPT",
    "USERNAME", "NODE", "TIME_LEFT"
  };
  
  for (size_t i = 0; i < sizeof(known)/sizeof(known[0]); i++) {
    if (strlen(known[i]) == len && strncmp(name, known[i], len) == 0) {
      return true;
    }
  }
  return false;
}

static bool contains_ansi_escape(const char* str) {
  if (!str) return false;
  while (*str) {
    if (str[0] == '\x1b' && str[1] == '[') return true;
    str++;
  }
  return false;
}

TemplateValidation validate_menu_template(const char* content, bool is_stock, bool is_asc) {
  TemplateValidation result = {TMPL_OK, 0, ""};
  
  if (!content || !content[0]) {
    result.code = TMPL_ERR_MALFORMED;
    snprintf(result.detail, sizeof(result.detail), "empty template");
    return result;
  }
  
  /* Check for ANSI in .asc file */
  if (is_asc && contains_ansi_escape(content)) {
    result.code = TMPL_ERR_ANSI_IN_ASC;
    snprintf(result.detail, sizeof(result.detail), "ANSI escape sequences not allowed in .asc files");
    return result;
  }
  
  int menu_items_count = 0;
  bool has_prompt = false;
  
  /* Process line by line */
  const char* line_start = content;
  int line_num = 1;
  
  while (*line_start) {
    /* Find end of line */
    const char* line_end = line_start;
    while (*line_end && *line_end != '\n' && *line_end != '\r') {
      line_end++;
    }
    
    size_t line_len = (size_t)(line_end - line_start);
    
    /* Create null-terminated line for processing */
    char line[512];
    if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
    memcpy(line, line_start, line_len);
    line[line_len] = '\0';
    
    /* Check visible width */
    size_t vis_len = visible_len(line);
    if (vis_len > TERMINAL_WIDTH) {
      result.code = TMPL_ERR_LINE_OVERFLOW;
      result.line_num = line_num;
      snprintf(result.detail, sizeof(result.detail), 
               "line %d has %zu visible columns (max %d)", 
               line_num, vis_len, TERMINAL_WIDTH);
      return result;
    }
    
    /* Check for %%MENU_ITEMS%% */
    if (strstr(line, TOKEN_MENU_ITEMS)) {
      menu_items_count++;
    }
    
    /* Check for %%PROMPT%% */
    if (strstr(line, TOKEN_PROMPT)) {
      has_prompt = true;
    }
    
    /* Check for unknown placeholders */
    const char* p = line;
    while ((p = strstr(p, "%%")) != NULL) {
      const char* start = p + 2;
      const char* end = strstr(start, "%%");
      if (end) {
        size_t name_len = (size_t)(end - start);
        if (name_len > 0 && name_len < 32 && !is_known_placeholder(start, name_len)) {
          result.code = TMPL_ERR_UNKNOWN_PLACEHOLDER;
          result.line_num = line_num;
          snprintf(result.detail, sizeof(result.detail),
                   "unknown placeholder %%%.*s%% on line %d",
                   (int)name_len, start, line_num);
          return result;
        }
        p = end + 2;
      } else {
        break;
      }
    }
    
    /* Move to next line */
    line_start = line_end;
    while (*line_start == '\n' || *line_start == '\r') line_start++;
    line_num++;
  }
  
  /* Check %%MENU_ITEMS%% count */
  if (menu_items_count == 0) {
    result.code = TMPL_ERR_NO_MENU_ITEMS;
    snprintf(result.detail, sizeof(result.detail), "template must contain exactly one %%%%MENU_ITEMS%%%% line");
    return result;
  }
  
  if (menu_items_count > 1) {
    result.code = TMPL_ERR_MULTIPLE_MENU_ITEMS;
    snprintf(result.detail, sizeof(result.detail), 
             "template has %d %%%%MENU_ITEMS%%%% lines (must have exactly 1)", menu_items_count);
    return result;
  }
  
  /* Check %%PROMPT%% for stock templates */
  if (is_stock && !has_prompt) {
    result.code = TMPL_ERR_NO_PROMPT;
    snprintf(result.detail, sizeof(result.detail), "stock templates must include %%%%PROMPT%%%%");
    return result;
  }
  
  return result;
}

MenuKeyValidation validate_menu_keys(const Menu* m) {
  MenuKeyValidation result = {true, "", "", ""};
  
  if (!m || !m->items || m->count == 0) return result;
  
  /* Check for duplicate single-char keys */
  for (size_t i = 0; i < m->count; i++) {
    if (m->items[i].key == '\0') continue;
    
    for (size_t j = i + 1; j < m->count; j++) {
      if (m->items[j].key == m->items[i].key) {
        result.valid = false;
        snprintf(result.dup_key, sizeof(result.dup_key), "%c", m->items[i].key);
        tmpl_copy(result.label1, sizeof(result.label1), m->items[i].label);
        tmpl_copy(result.label2, sizeof(result.label2), m->items[j].label);
        return result;
      }
    }
    
    /* Check collision with single-char multi-char keys */
    for (size_t j = 0; j < m->count; j++) {
      if (i == j) continue;
      if (m->items[j].key_str[0] && strlen(m->items[j].key_str) == 1 &&
          m->items[j].key_str[0] == m->items[i].key) {
        result.valid = false;
        snprintf(result.dup_key, sizeof(result.dup_key), "%c", m->items[i].key);
        tmpl_copy(result.label1, sizeof(result.label1), m->items[i].label);
        tmpl_copy(result.label2, sizeof(result.label2), m->items[j].label);
        return result;
      }
    }
  }
  
  /* Check for duplicate multi-char keys */
  for (size_t i = 0; i < m->count; i++) {
    if (!m->items[i].key_str[0]) continue;
    
    for (size_t j = i + 1; j < m->count; j++) {
      if (m->items[j].key_str[0] && 
          strcmp(m->items[i].key_str, m->items[j].key_str) == 0) {
        result.valid = false;
        snprintf(result.dup_key, sizeof(result.dup_key), "%s", m->items[i].key_str);
        tmpl_copy(result.label1, sizeof(result.label1), m->items[i].label);
        tmpl_copy(result.label2, sizeof(result.label2), m->items[j].label);
        return result;
      }
    }
  }
  
  return result;
}

/* ============================================================================
 * Template Rendering
 * ============================================================================ */

/* Format a menu item as "[KEY] Label" */
static size_t format_menu_cell(const MenuItem* item, const Session* s,
                               char* out, size_t cap) {
  if (!item || !out || cap == 0) return 0;
  
  char key_disp[16];
  if (item->key != '\0') {
    snprintf(key_disp, sizeof(key_disp), "%c", item->key);
  } else {
    tmpl_copy(key_disp, sizeof(key_disp), item->key_str);
  }
  
  /* Expand MCI in label */
  char label[256];
  if (s) {
    mci_expand(s, item->label, label, sizeof(label));
  } else {
    tmpl_copy(label, sizeof(label), item->label);
  }
  
  return (size_t)snprintf(out, cap, "[%.15s] %.108s", key_disp, label);
}

/* Pad or truncate a cell to exact width */
static size_t fit_cell(const char* cell, size_t target_width, char* out, size_t cap) {
  if (!out || cap == 0) return 0;
  
  size_t vis = visible_len(cell);
  
  if (vis <= target_width) {
    /* Pad with spaces */
    size_t o = 0;
    if (cell) {
      size_t len = strlen(cell);
      if (len < cap) {
        memcpy(out, cell, len);
        o = len;
      }
    }
    while (vis < target_width && o + 1 < cap) {
      out[o++] = ' ';
      vis++;
    }
    out[o] = '\0';
    return o;
  } else {
    /* Truncate with ellipsis */
    size_t o = 0;
    size_t vis_count = 0;
    size_t i = 0;
    
    /* Reserve space for "..." */
    size_t trunc_at = target_width > 3 ? target_width - 3 : target_width;
    
    while (cell && cell[i] && vis_count < trunc_at && o + 4 < cap) {
      if (cell[i] == '\x1b' && cell[i+1] == '[') {
        /* Copy ANSI sequence */
        out[o++] = cell[i++];
        out[o++] = cell[i++];
        while (cell[i] && !isalpha((unsigned char)cell[i]) && o + 1 < cap) {
          out[o++] = cell[i++];
        }
        if (cell[i] && o + 1 < cap) {
          out[o++] = cell[i++];
        }
      } else {
        out[o++] = cell[i++];
        vis_count++;
      }
    }
    
    /* Add ellipsis */
    if (target_width > 3 && o + 4 < cap) {
      out[o++] = '.';
      out[o++] = '.';
      out[o++] = '.';
    }
    
    out[o] = '\0';
    return o;
  }
}

/* Expand template placeholders (except %%MENU_ITEMS%%) */
static size_t expand_template_placeholders(const char* tmpl, const Menu* m,
                                           const Session* s, char* out, size_t cap) {
  if (!tmpl || !out || cap == 0) return 0;
  
  size_t o = 0;
  const char* p = tmpl;
  
  while (*p && o + 1 < cap) {
    if (p[0] == '%' && p[1] == '%') {
      /* Check for placeholder */
      const char* start = p + 2;
      const char* end = strstr(start, "%%");
      
      if (end) {
        size_t name_len = (size_t)(end - start);
        char name[32];
        if (name_len < sizeof(name)) {
          memcpy(name, start, name_len);
          name[name_len] = '\0';
          
          const char* replacement = NULL;
          char num_buf[16];
          
          if (strcmp(name, "MENU_ITEMS") == 0) {
            /* Keep this placeholder for later processing */
            if (o + 14 < cap) {
              memcpy(out + o, TOKEN_MENU_ITEMS, 14);
              o += 14;
            }
            p = end + 2;
            continue;
          } else if (strcmp(name, "MENU_TITLE") == 0) {
            replacement = m ? m->title : "";
          } else if (strcmp(name, "MENU_NAME") == 0) {
            replacement = m ? m->name : "";
          } else if (strcmp(name, "PROMPT") == 0) {
            replacement = m ? m->prompt : "Selection: ";
          } else if (strcmp(name, "USERNAME") == 0) {
            replacement = (s && s->user.handle[0]) ? s->user.handle : "";
          } else if (strcmp(name, "NODE") == 0) {
            snprintf(num_buf, sizeof(num_buf), "%d", s ? s->node_num : 0);
            replacement = num_buf;
          } else if (strcmp(name, "TIME_LEFT") == 0) {
            snprintf(num_buf, sizeof(num_buf), "%d", s ? s->time_left_min : 0);
            replacement = num_buf;
          }
          
          if (replacement) {
            size_t rlen = strlen(replacement);
            if (o + rlen < cap) {
              memcpy(out + o, replacement, rlen);
              o += rlen;
            }
            p = end + 2;
            continue;
          }
        }
      }
    }
    
    out[o++] = *p++;
  }
  
  out[o] = '\0';
  return o;
}

/* Calculate body width from template line containing %%MENU_ITEMS%% */
static int calc_body_width(const char* body_line) {
  if (!body_line) return 0;
  
  const char* token = strstr(body_line, TOKEN_MENU_ITEMS);
  if (!token) return 0;
  
  /* Calculate left segment visible width */
  char left[256];
  size_t left_len = (size_t)(token - body_line);
  if (left_len >= sizeof(left)) left_len = sizeof(left) - 1;
  memcpy(left, body_line, left_len);
  left[left_len] = '\0';
  size_t left_vis = visible_len(left);
  
  /* Calculate right segment visible width */
  const char* right_start = token + strlen(TOKEN_MENU_ITEMS);
  size_t right_vis = visible_len(right_start);
  
  int body_width = TERMINAL_WIDTH - (int)left_vis - (int)right_vis;
  return body_width > 0 ? body_width : 0;
}

/* Compute effective column count based on body width and requested max */
static int compute_columns(int body_width, int requested_max, int item_count) {
  if (item_count == 0) return 1;
  if (requested_max < 1) requested_max = 1;
  if (requested_max > MAX_MENU_COLS) requested_max = MAX_MENU_COLS;
  
  /* Try from requested_max down to 1 */
  for (int cols = requested_max; cols >= 1; cols--) {
    int col_width = body_width / cols;
    if (col_width >= MIN_CELL_WIDTH) {
      return cols;
    }
  }
  
  return 1;
}

/* Templates may draw a border below %%PROMPT%%. After streaming that border the
 * terminal cursor sits on the following row, so return ANSI callers to the end
 * of the prompt before readline begins. */
static size_t append_prompt_cursor(const Menu* m, const Session* s,
                                   char* buf, size_t len, size_t cap) {
  if (!m || !s || !s->ansi || !m->prompt[0] || !buf || len >= cap) return len;

  char* prompt = NULL;
  for (char* found = strstr(buf, m->prompt); found; found = strstr(found + 1, m->prompt))
    prompt = found;
  if (!prompt) return len;

  char* line_start = prompt;
  while (line_start > buf && line_start[-1] != '\n') line_start--;
  const char* prompt_end = prompt + strlen(m->prompt);
  int rows_up = 0;
  for (const char* p = prompt_end; p < buf + len; p++)
    if (*p == '\n') rows_up++;
  if (rows_up <= 0) return len;

  size_t prefix_len = (size_t)(prompt_end - line_start);
  char prefix[512];
  if (prefix_len >= sizeof(prefix)) return len;
  memcpy(prefix, line_start, prefix_len);
  prefix[prefix_len] = '\0';
  size_t column = visible_len(prefix) + 1;

  int written = snprintf(buf + len, cap - len, "\x1b[%dA\x1b[%zuG", rows_up, column);
  if (written < 0 || (size_t)written >= cap - len) return len;
  return len + (size_t)written;
}

size_t menu_render_template(const Menu* m, const Session* s,
                            char* buf, size_t cap) {
  if (!m || !buf || cap == 0) return 0;
  
  char tmpl_path[256];
  if (!resolve_menu_template_path(m, s, tmpl_path, sizeof(tmpl_path))) {
    log_template_error(m->name, "template file not found, using fallback");
    return 0;  /* Caller should use fallback */
  }
  
  size_t tmpl_len = 0;
  char* tmpl_raw = slurp_file(tmpl_path, &tmpl_len);
  if (!tmpl_raw) {
    log_template_error(m->name, "failed to read template file");
    return 0;
  }
  
  /* Validate template */
  bool is_asc = strstr(tmpl_path, ".asc") != NULL;
  TemplateValidation val = validate_menu_template(tmpl_raw, true, is_asc);
  if (val.code != TMPL_OK) {
    char msg[256];
    snprintf(msg, sizeof(msg), "validation failed: %s", val.detail);
    log_template_error(m->name, msg);
    free(tmpl_raw);
    return 0;
  }
  
  /* Expand placeholders (except %%MENU_ITEMS%%) */
  char* expanded = (char*)malloc(tmpl_len * 2 + 1024);
  if (!expanded) {
    free(tmpl_raw);
    return 0;
  }
  expand_template_placeholders(tmpl_raw, m, s, expanded, tmpl_len * 2 + 1024);
  free(tmpl_raw);
  
  /* Find header, body template, and footer */
  char* lines[256];
  int line_count = 0;
  int body_line_idx = -1;
  
  char* line_start = expanded;
  while (*line_start && line_count < 256) {
    lines[line_count] = line_start;
    
    /* Check if this line contains %%MENU_ITEMS%% */
    char* line_end = line_start;
    while (*line_end && *line_end != '\n' && *line_end != '\r') line_end++;
    
    char saved = *line_end;
    *line_end = '\0';
    if (strstr(line_start, TOKEN_MENU_ITEMS)) {
      body_line_idx = line_count;
    }
    *line_end = saved;
    
    line_count++;
    
    /* Move to next line */
    while (*line_end == '\n' || *line_end == '\r') line_end++;
    line_start = line_end;
  }
  
  if (body_line_idx < 0) {
    free(expanded);
    return 0;
  }
  
  /* Collect visible menu items */
  const MenuItem* visible_items[128];
  size_t visible_count = collect_visible_menu_items(m, s, visible_items, 128);
  
  /* Get body template line */
  char body_template[512];
  {
    char* bstart = lines[body_line_idx];
    char* bend = bstart;
    while (*bend && *bend != '\n' && *bend != '\r') bend++;
    size_t blen = (size_t)(bend - bstart);
    if (blen >= sizeof(body_template)) blen = sizeof(body_template) - 1;
    memcpy(body_template, bstart, blen);
    body_template[blen] = '\0';
  }
  
  /* Calculate body width and columns */
  int body_width = calc_body_width(body_template);
  if (body_width < MIN_CELL_WIDTH) {
    char msg[128];
    snprintf(msg, sizeof(msg), "body width %d too small (min %d)", body_width, MIN_CELL_WIDTH);
    log_template_error(m->name, msg);
    free(expanded);
    return 0;
  }
  
  int cols = compute_columns(body_width, m->gen_cols > 0 ? m->gen_cols : 3, (int)visible_count);
  int col_width = body_width / cols;
  
  /* Start building output */
  size_t o = 0;
  
  /* Clear screen if flag set */
  if (m->flags & MENU_FLAG_CLRSCR_BEFORE) {
    o += (size_t)snprintf(buf + o, cap - o, "\x1b[2J\x1b[H");
  }
  
  /* Emit header lines */
  for (int i = 0; i < body_line_idx && o + 1 < cap; i++) {
    char* lstart = lines[i];
    char* lend = lstart;
    while (*lend && *lend != '\n' && *lend != '\r') lend++;
    size_t llen = (size_t)(lend - lstart);
    if (o + llen + 2 < cap) {
      memcpy(buf + o, lstart, llen);
      o += llen;
      buf[o++] = '\r';
      buf[o++] = '\n';
    }
  }
  
  /* Generate body rows */
  size_t item_idx = 0;
  while (item_idx < visible_count || (visible_count == 0 && item_idx == 0)) {
    /* Build row content */
    char row_content[256];
    size_t row_o = 0;
    
    for (int c = 0; c < cols && row_o + col_width + 1 < sizeof(row_content); c++) {
      char cell[128] = "";
      
      if (item_idx < visible_count) {
        format_menu_cell(visible_items[item_idx], s, cell, sizeof(cell));
        item_idx++;
      }
      
      char fitted[128];
      fit_cell(cell, (size_t)col_width, fitted, sizeof(fitted));
      
      size_t flen = strlen(fitted);
      if (row_o + flen < sizeof(row_content)) {
        memcpy(row_content + row_o, fitted, flen);
        row_o += flen;
      }
    }
    row_content[row_o] = '\0';
    
    /* Replace %%MENU_ITEMS%% in body template with row content */
    const char* token_pos = strstr(body_template, TOKEN_MENU_ITEMS);
    if (token_pos && o + 256 < cap) {
      /* Copy left part */
      size_t left_len = (size_t)(token_pos - body_template);
      memcpy(buf + o, body_template, left_len);
      o += left_len;
      
      /* Copy row content */
      size_t rc_len = strlen(row_content);
      memcpy(buf + o, row_content, rc_len);
      o += rc_len;
      
      /* Copy right part */
      const char* right = token_pos + strlen(TOKEN_MENU_ITEMS);
      size_t right_len = strlen(right);
      memcpy(buf + o, right, right_len);
      o += right_len;
      
      buf[o++] = '\r';
      buf[o++] = '\n';
    }
    
    /* Handle empty menu case */
    if (visible_count == 0) break;
  }
  
  /* Emit footer lines */
  for (int i = body_line_idx + 1; i < line_count && o + 1 < cap; i++) {
    char* lstart = lines[i];
    char* lend = lstart;
    while (*lend && *lend != '\n' && *lend != '\r') lend++;
    size_t llen = (size_t)(lend - lstart);
    if (o + llen + 2 < cap) {
      memcpy(buf + o, lstart, llen);
      o += llen;
      buf[o++] = '\r';
      buf[o++] = '\n';
    }
  }
  
  buf[o] = '\0';
  o = append_prompt_cursor(m, s, buf, o, cap);
  buf[o] = '\0';
  free(expanded);
  
  return o;
}
