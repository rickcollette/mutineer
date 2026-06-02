#include "bbs_menu.h"
#include "bbs_util.h"
#include "bbs_mci.h"
#include "bbs_acs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static unsigned parse_menu_flags(const char* str) {
  unsigned flags = 0;
  char tmp[256];
  snprintf(tmp, sizeof(tmp), "%s", str);
  
  char* tok = strtok(tmp, ",");
  while (tok) {
    while (*tok && isspace((unsigned char)*tok)) tok++;
    char* end = tok + strlen(tok) - 1;
    while (end > tok && isspace((unsigned char)*end)) *end-- = '\0';
    
    if (strcasecmp(tok, "clrscr") == 0) flags |= MENU_FLAG_CLRSCR_BEFORE;
    else if (strcasecmp(tok, "nocenter") == 0) flags |= MENU_FLAG_DONT_CENTER;
    else if (strcasecmp(tok, "notitle") == 0) flags |= MENU_FLAG_NO_MENU_TITLE;
    else if (strcasecmp(tok, "pause") == 0) flags |= MENU_FLAG_FORCE_PAUSE;
    else if (strcasecmp(tok, "autotime") == 0) flags |= MENU_FLAG_AUTO_TIME;
    else if (strcasecmp(tok, "noprompt") == 0) flags |= MENU_FLAG_NO_MENU_PROMPT;
    else if (strcasecmp(tok, "lightbar") == 0) flags |= MENU_FLAG_USE_LIGHTBAR;
    else if (strcasecmp(tok, "hotkeys") == 0) flags |= MENU_FLAG_HOTKEYS;
    
    tok = strtok(NULL, ",");
  }
  return flags;
}

static unsigned parse_cmd_flags(const char* str) {
  unsigned flags = 0;
  char tmp[256];
  snprintf(tmp, sizeof(tmp), "%s", str);
  
  char* tok = strtok(tmp, ",");
  while (tok) {
    while (*tok && isspace((unsigned char)*tok)) tok++;
    char* end = tok + strlen(tok) - 1;
    while (end > tok && isspace((unsigned char)*end)) *end-- = '\0';
    
    if (strcasecmp(tok, "hidden") == 0) flags |= CMD_FLAG_HIDDEN;
    else if (strcasecmp(tok, "unhidden") == 0) flags |= CMD_FLAG_UNHIDDEN;
    else if (strcasecmp(tok, "password") == 0) flags |= CMD_FLAG_PASSWORD;
    else if (strcasecmp(tok, "sysoplog") == 0) flags |= CMD_FLAG_SYSOP_LOG;
    
    tok = strtok(NULL, ",");
  }
  return flags;
}

static void append_flag(char* out, size_t cap, const char* name, bool* first) {
  if (!out || !name || !first || cap == 0) return;
  if (!*first) strncat(out, ",", cap - strlen(out) - 1);
  strncat(out, name, cap - strlen(out) - 1);
  *first = false;
}

static void format_menu_flags(unsigned flags, char* out, size_t cap) {
  bool first = true;
  if (!out || cap == 0) return;
  out[0] = '\0';
  if (flags & MENU_FLAG_CLRSCR_BEFORE) append_flag(out, cap, "clrscr", &first);
  if (flags & MENU_FLAG_DONT_CENTER) append_flag(out, cap, "nocenter", &first);
  if (flags & MENU_FLAG_NO_MENU_TITLE) append_flag(out, cap, "notitle", &first);
  if (flags & MENU_FLAG_FORCE_PAUSE) append_flag(out, cap, "pause", &first);
  if (flags & MENU_FLAG_AUTO_TIME) append_flag(out, cap, "autotime", &first);
  if (flags & MENU_FLAG_NO_MENU_PROMPT) append_flag(out, cap, "noprompt", &first);
  if (flags & MENU_FLAG_USE_LIGHTBAR) append_flag(out, cap, "lightbar", &first);
  if (flags & MENU_FLAG_HOTKEYS) append_flag(out, cap, "hotkeys", &first);
}

static void format_cmd_flags(unsigned flags, char* out, size_t cap) {
  bool first = true;
  if (!out || cap == 0) return;
  out[0] = '\0';
  if (flags & CMD_FLAG_HIDDEN) append_flag(out, cap, "hidden", &first);
  if (flags & CMD_FLAG_UNHIDDEN) append_flag(out, cap, "unhidden", &first);
  if (flags & CMD_FLAG_PASSWORD) append_flag(out, cap, "password", &first);
  if (flags & CMD_FLAG_SYSOP_LOG) append_flag(out, cap, "sysoplog", &first);
}

static bool parse_line(const char* line, MenuItem* it) {
  char tmp[512];
  snprintf(tmp, sizeof(tmp), "%s", line);
  memset(it, 0, sizeof(*it));

  /* Split by | delimiter */
  char* parts[8] = {0};
  int nparts = 0;
  char* p = tmp;
  parts[nparts++] = p;
  
  while (*p && nparts < 8) {
    if (*p == '|') {
      *p++ = '\0';
      parts[nparts++] = p;
    } else {
      p++;
    }
  }
  
  if (nparts < 3) return false;
  
  /* Trim all parts */
  for (int i = 0; i < nparts; i++) {
    if (parts[i]) str_trim(parts[i]);
  }
  
  char* key = parts[0];
  char* label = parts[1];
  char* action = parts[2];
  char* data_or_acs = nparts > 3 ? parts[3] : "";
  char* acs = "";
  char* cmdflags = "";
  char* password = "";
  
  /* Determine if 4th field is data or ACS (legacy format) */
  if (nparts == 4) {
    /* Legacy format: KEY|Label|Action|ACS */
    acs = data_or_acs;
  } else if (nparts >= 5) {
    /* Enhanced format: KEY|Label|Action|Data|ACS|CmdFlags|Password */
    snprintf(it->data, sizeof(it->data), "%s", data_or_acs);
    acs = parts[4];
    if (nparts > 5) cmdflags = parts[5];
    if (nparts > 6) password = parts[6];
  }
  
  if (key[0] == '\0') return false;
  
  /* Handle multi-char keys */
  if (strlen(key) > 1) {
    it->key = '\0';
    snprintf(it->key_str, sizeof(it->key_str), "%s", key);
    for (size_t i = 0; it->key_str[i]; i++) {
      it->key_str[i] = (char)toupper((unsigned char)it->key_str[i]);
    }
  } else {
    it->key = (char)toupper((unsigned char)key[0]);
    it->key_str[0] = '\0';
  }
  
  snprintf(it->label, sizeof(it->label), "%s", label);
  snprintf(it->action, sizeof(it->action), "%s", action);
  snprintf(it->acs, sizeof(it->acs), "%s", acs);
  it->flags = parse_cmd_flags(cmdflags);
  snprintf(it->password, sizeof(it->password), "%s", password);
  
  return true;
}

bool menu_load(const char* path, Menu* out) {
  memset(out, 0, sizeof(*out));
  snprintf(out->title, sizeof(out->title), "Mutineer Menu");
  snprintf(out->prompt, sizeof(out->prompt), "Selection: ");
  out->gen_cols = 1;

  /* Extract menu name from path */
  const char* slash = strrchr(path, '/');
  const char* name = slash ? slash + 1 : path;
  snprintf(out->name, sizeof(out->name), "%s", name);
  char* dot = strrchr(out->name, '.');
  if (dot) *dot = '\0';

  FILE* f = fopen(path, "rb");
  if (!f) return false;

  MenuItem* items = NULL;
  size_t cap = 0, n = 0;

  char line[512];
  while (fgets(line, sizeof(line), f)) {
    str_trim(line);
    if (line[0] == '\0') continue;
    
    /* Handle directives */
    if (line[0] == '#') continue;  /* Comment */
    
    if (line[0] == '@') {
      char* val = strchr(line + 1, ' ');
      if (val) {
        *val++ = '\0';
        str_trim(val);
      } else {
        val = "";
      }
      
      if (strcasecmp(line + 1, "TITLE") == 0) {
        snprintf(out->title, sizeof(out->title), "%s", val);
      } else if (strcasecmp(line + 1, "FLAGS") == 0) {
        out->flags = parse_menu_flags(val);
      } else if (strcasecmp(line + 1, "PROMPT") == 0) {
        snprintf(out->prompt, sizeof(out->prompt), "%s", val);
      } else if (strcasecmp(line + 1, "ART") == 0) {
        snprintf(out->art_file, sizeof(out->art_file), "%s", val);
      } else if (strcasecmp(line + 1, "FALLBACK") == 0) {
        snprintf(out->fallback, sizeof(out->fallback), "%s", val);
      } else if (strcasecmp(line + 1, "COLS") == 0) {
        out->gen_cols = atoi(val);
        if (out->gen_cols < 1) out->gen_cols = 1;
        if (out->gen_cols > 4) out->gen_cols = 4;
      }
      continue;
    }

    MenuItem it;
    if (!parse_line(line, &it)) continue;

    if (n == cap) {
      cap = cap ? cap * 2 : 8;
      items = (MenuItem*)realloc(items, cap * sizeof(MenuItem));
    }
    items[n++] = it;
  }

  fclose(f);
  out->items = items;
  out->count = n;
  return true;
}

bool menu_save(const char* path, const Menu* menu) {
  if (!path || !menu) return false;

  FILE* f = fopen(path, "w");
  if (!f) return false;

  char flags[256];
  format_menu_flags(menu->flags, flags, sizeof(flags));

  fprintf(f, "# Menu: %s\n", menu->name[0] ? menu->name : "menu");
  fprintf(f, "@TITLE %s\n", menu->title);
  if (flags[0]) fprintf(f, "@FLAGS %s\n", flags);
  fprintf(f, "@PROMPT %s\n", menu->prompt);
  if (menu->art_file[0]) fprintf(f, "@ART %s\n", menu->art_file);
  if (menu->fallback[0]) fprintf(f, "@FALLBACK %s\n", menu->fallback);
  fprintf(f, "@COLS %d\n", menu->gen_cols > 0 ? menu->gen_cols : 1);
  fprintf(f, "# key|label|action|data|acs|cmdflags|password\n");

  for (size_t i = 0; i < menu->count; i++) {
    const MenuItem* item = &menu->items[i];
    char key_disp[16];
    char cmd_flags[128];
    if (item->key != '\0') snprintf(key_disp, sizeof(key_disp), "%c", item->key);
    else snprintf(key_disp, sizeof(key_disp), "%s", item->key_str);
    format_cmd_flags(item->flags, cmd_flags, sizeof(cmd_flags));
    fprintf(f, "%s|%s|%s|%s|%s|%s|%s\n",
            key_disp,
            item->label,
            item->action,
            item->data,
            item->acs,
            cmd_flags,
            item->password);
  }

  if (fclose(f) != 0) return false;
  return true;
}

bool menu_delete_file(const char* path) {
  if (!path || !path[0]) return false;
  return remove(path) == 0;
}

void menu_free(Menu* m) {
  if (!m) return;
  free(m->items);
  memset(m, 0, sizeof(*m));
}

const MenuItem* menu_find(const Menu* m, char key) {
  if (!m || !m->items || m->count == 0) return NULL;
  char k = (char)toupper((unsigned char)key);
  for (size_t i = 0; i < m->count; i++) {
    if (m->items[i].key == k && m->items[i].key != '\0') {
      return &m->items[i];
    }
  }
  return NULL;
}

const MenuItem* menu_find_str(const Menu* m, const char* key_str) {
  if (!m || !m->items || m->count == 0 || !key_str) return NULL;
  
  char upper[16];
  snprintf(upper, sizeof(upper), "%s", key_str);
  for (size_t i = 0; upper[i]; i++) {
    upper[i] = (char)toupper((unsigned char)upper[i]);
  }
  
  for (size_t i = 0; i < m->count; i++) {
    if (m->items[i].key_str[0] && strcmp(m->items[i].key_str, upper) == 0) {
      return &m->items[i];
    }
  }
  return NULL;
}

bool menu_item_visible(const MenuItem* item, const struct Session* s) {
  if (!item) return false;
  
  /* Hidden items are never displayed */
  if (item->flags & CMD_FLAG_HIDDEN) return false;
  
  /* Unhidden items are always displayed */
  if (item->flags & CMD_FLAG_UNHIDDEN) return true;
  
  /* Check ACS */
  if (item->acs[0] && !acs_allows(s, item->acs)) return false;
  
  return true;
}

size_t menu_render(const Menu* m, const struct Session* s, char* buf, size_t cap) {
  size_t o = 0;
  if (!m || !buf || cap == 0) return 0;
  if (m->count > 0 && !m->items) return 0;  /* corrupted menu */

  /* Clear screen if flag set */
  if (m->flags & MENU_FLAG_CLRSCR_BEFORE) {
    o += (size_t)snprintf(buf + o, cap - o, "\x1b[2J\x1b[H");
  }

  /* Display title unless suppressed */
  if (!(m->flags & MENU_FLAG_NO_MENU_TITLE)) {
    char title[256];
    if (s) {
      mci_expand(s, m->title, title, sizeof(title));
    } else {
      snprintf(title, sizeof(title), "%s", m->title);
    }
    
    o += (size_t)snprintf(buf + o, cap - o, "\r\n");
    if (m->flags & MENU_FLAG_DONT_CENTER) {
      o += (size_t)snprintf(buf + o, cap - o, "=== %s ===\r\n", title);
    } else {
      /* Simple centering for 80-col terminal */
      int title_len = (int)strlen(title) + 8;  /* "=== " + " ===" */
      int pad = (80 - title_len) / 2;
      if (pad < 0) pad = 0;
      for (int i = 0; i < pad && o + 1 < cap; i++) buf[o++] = ' ';
      o += (size_t)snprintf(buf + o, cap - o, "=== %s ===\r\n", title);
    }
    o += (size_t)snprintf(buf + o, cap - o, "\r\n");
  }

  /* Display menu items */
  int col = 0;
  int gen_cols = m->gen_cols > 0 ? m->gen_cols : 1;
  int col_width = 80 / gen_cols;
  
  for (size_t i = 0; i < m->count; i++) {
    if (!menu_item_visible(&m->items[i], s)) continue;
    
    char lbl[256];
    if (s) mci_expand(s, m->items[i].label, lbl, sizeof(lbl));
    else snprintf(lbl, sizeof(lbl), "%s", m->items[i].label);
    
    char key_disp[16];
    if (m->items[i].key != '\0') {
      snprintf(key_disp, sizeof(key_disp), "%c", m->items[i].key);
    } else {
      snprintf(key_disp, sizeof(key_disp), "%s", m->items[i].key_str);
    }
    
    if (gen_cols > 1) {
      /* Multi-column layout */
      int item_len = snprintf(buf + o, cap - o, "  [%s] %-*s", key_disp, col_width - 8, lbl);
      if (item_len > 0) o += (size_t)item_len;
      col++;
      if (col >= gen_cols) {
        o += (size_t)snprintf(buf + o, cap - o, "\r\n");
        col = 0;
      }
    } else {
      o += (size_t)snprintf(buf + o, cap - o, "  [%s] %s\r\n", key_disp, lbl);
    }
    
    if (o >= cap) break;
  }
  
  /* End row if multi-column and not at start of line */
  if (gen_cols > 1 && col > 0) {
    o += (size_t)snprintf(buf + o, cap - o, "\r\n");
  }

  /* Auto-time display */
  if (m->flags & MENU_FLAG_AUTO_TIME) {
    if (s) {
      o += (size_t)snprintf(buf + o, cap - o, "\r\n[Time left: %d min]\r\n", s->time_left_min);
    }
  }

  /* Display prompt unless suppressed */
  if (!(m->flags & MENU_FLAG_NO_MENU_PROMPT)) {
    char prompt[128];
    if (s) {
      mci_expand(s, m->prompt, prompt, sizeof(prompt));
    } else {
      snprintf(prompt, sizeof(prompt), "%s", m->prompt);
    }
    o += (size_t)snprintf(buf + o, cap - o, "\r\n%s", prompt);
  }

  return o;
}
