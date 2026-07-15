#include "bbs_mci.h"
#include "bbs_util.h"
#include "bbs_acs.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

static const char* ansi_colors[] = {
  "\x1b[0;30m",  /* 0 - black */
  "\x1b[0;34m",  /* 1 - blue */
  "\x1b[0;32m",  /* 2 - green */
  "\x1b[0;36m",  /* 3 - cyan */
  "\x1b[0;31m",  /* 4 - red */
  "\x1b[0;35m",  /* 5 - magenta */
  "\x1b[0;33m",  /* 6 - brown/yellow */
  "\x1b[0;37m",  /* 7 - white/gray */
  "\x1b[1;30m",  /* 8 - dark gray */
  "\x1b[1;34m",  /* 9 - bright blue */
  "\x1b[1;32m",  /* 10 - bright green */
  "\x1b[1;36m",  /* 11 - bright cyan */
  "\x1b[1;31m",  /* 12 - bright red */
  "\x1b[1;35m",  /* 13 - bright magenta */
  "\x1b[1;33m",  /* 14 - bright yellow */
  "\x1b[1;37m",  /* 15 - bright white */
};

static void mci_copy(char *dst, size_t cap, const char *src)
{
  if (!dst || cap == 0) return;
  size_t n = src ? strlen(src) : 0;
  if (n >= cap) n = cap - 1;
  if (n > 0) memcpy(dst, src, n);
  dst[n] = '\0';
}

/* Color scheme table: schemes[scheme_id][color_index (0-9)] = ansi_color index (0-15).
   ^0=primary accent, ^1=secondary, ^2=text, ^3=dim text, ^4=highlight,
   ^5=error/alert, ^6=success, ^7=label, ^8=border, ^9=title */
#define NUM_COLOR_SCHEMES 8
static const int color_scheme_table[NUM_COLOR_SCHEMES][10] = {
  /* 0: Mutineer Green (default) */
  { 10, 2, 7, 8, 14, 12, 10, 15, 2, 11 },
  /* 1: Classic Blue */
  {  9, 1, 7, 8, 14, 12,  2, 15, 1, 11 },
  /* 2: Amber */
  { 14, 6, 7, 8, 15, 12, 14, 15, 6, 14 },
  /* 3: Cyan */
  { 11, 3, 7, 8, 15, 12, 10, 15, 3, 11 },
  /* 4: Red Sunset */
  { 12, 4, 7, 8, 14,  5, 10, 15, 4, 12 },
  /* 5: Magenta */
  { 13, 5, 7, 8, 15, 12, 10, 15, 5, 13 },
  /* 6: Monochrome */
  {  7, 8, 7, 8, 15, 15,  7, 15, 8,  7 },
  /* 7: Bright White */
  { 15,  7, 15,  8, 14, 12, 10, 15,  8, 11 },
};

static const char* color_scheme_names[NUM_COLOR_SCHEMES] = {
  "Mutineer Green", "Classic Blue", "Amber", "Cyan",
  "Red Sunset", "Magenta", "Monochrome", "Bright White",
};

static size_t append_str(char* out, size_t o, size_t cap, const char* str) {
  if (!str) return o;
  size_t len = strlen(str);
  if (o + len < cap) {
    memcpy(out + o, str, len);
    return o + len;
  }
  return o;
}

static size_t append_int(char* out, size_t o, size_t cap, int val) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%d", val);
  return append_str(out, o, cap, buf);
}

static int calc_user_age(const char* birth_date) {
  if (!birth_date || !birth_date[0]) return 0;
  int by, bm, bd;
  if (sscanf(birth_date, "%d-%d-%d", &by, &bm, &bd) != 3) return 0;
  
  time_t now = time(NULL);
  struct tm* tm = localtime(&now);
  int cy = tm->tm_year + 1900;
  int cm = tm->tm_mon + 1;
  int cd = tm->tm_mday;
  
  int age = cy - by;
  if (cm < bm || (cm == bm && cd < bd)) age--;
  return age;
}

size_t mci_expand(const Session* s, const char* in, char* out, size_t cap) {
  if (!out || cap == 0) return 0;
  size_t o = 0;
  
  for (size_t i = 0; in && in[i] && o + 1 < cap; ) {
    /* ^0-^9 - User color scheme colors (map to ANSI via scheme table) */
    if (in[i] == '^' && in[i+1] >= '0' && in[i+1] <= '9') {
      int color_idx = in[i+1] - '0';
      int scheme_id = s ? (int)s->user.color_scheme : 0;
      if (scheme_id < 0 || scheme_id >= NUM_COLOR_SCHEMES) scheme_id = 0;
      int actual_color = color_scheme_table[scheme_id][color_idx];
      o = append_str(out, o, cap, ansi_colors[actual_color]);
      i += 2;
      continue;
    }
    
    /* ~L# - Set foreground color (0-15) */
    if (in[i] == '~' && in[i+1] == 'L' && isdigit((unsigned char)in[i+2])) {
      int color = in[i+2] - '0';
      if (isdigit((unsigned char)in[i+3])) {
        color = color * 10 + (in[i+3] - '0');
        i += 4;
      } else {
        i += 3;
      }
      if (color >= 0 && color < 16) {
        o = append_str(out, o, cap, ansi_colors[color]);
      }
      continue;
    }
    
    /* ~B# - Set background color (0-7) */
    if (in[i] == '~' && in[i+1] == 'B' && isdigit((unsigned char)in[i+2])) {
      int color = in[i+2] - '0';
      i += 3;
      if (color >= 0 && color <= 7) {
        char buf[16];
        snprintf(buf, sizeof(buf), "\x1b[%dm", 40 + color);
        o = append_str(out, o, cap, buf);
      }
      continue;
    }
    
    /* ~K# - Blink attribute (0=off, 1=on) */
    if (in[i] == '~' && in[i+1] == 'K' && (in[i+2] == '0' || in[i+2] == '1')) {
      if (in[i+2] == '1') {
        o = append_str(out, o, cap, "\x1b[5m");
      } else {
        o = append_str(out, o, cap, "\x1b[25m");
      }
      i += 3;
      continue;
    }
    
    /* ~RS - Reset all attributes */
    if (in[i] == '~' && in[i+1] == 'R' && in[i+2] == 'S') {
      o = append_str(out, o, cap, "\x1b[0m");
      i += 3;
      continue;
    }
    
    /* Tilde codes (~XX) - User/System MCI */
    if (in[i] == '~' && in[i+1] && in[i+2]) {
      char c1 = in[i+1];
      char c2 = in[i+2];
      
      /* User MCI codes */
      if (c1 == 'U' && c2 == 'N') { /* ~UN - User handle */
        o = append_str(out, o, cap, s ? s->user.handle : "");
        i += 3; continue;
      }
      if (c1 == 'R' && c2 == 'N') { /* ~RN - Real name */
        o = append_str(out, o, cap, s ? s->user.real_name : "");
        i += 3; continue;
      }
      if (c1 == 'U' && c2 == '#') { /* ~U# - User ID number */
        o = append_int(out, o, cap, s ? s->user.id : 0);
        i += 3; continue;
      }
      if (c1 == 'A' && c2 == 'G') { /* ~AG - User age */
        int age = s ? calc_user_age(s->user.birth_date) : 0;
        o = append_int(out, o, cap, age);
        i += 3; continue;
      }
      if (c1 == 'B' && c2 == 'D') { /* ~BD - Birth date */
        o = append_str(out, o, cap, s ? s->user.birth_date : "");
        i += 3; continue;
      }
      if (c1 == 'S' && c2 == 'X') { /* ~SX - Sex (M/F/U) */
        char sex[2] = { s ? s->user.sex : 'U', '\0' };
        o = append_str(out, o, cap, sex);
        i += 3; continue;
      }
      if (c1 == 'C' && c2 == 'T') { /* ~CT - City/state */
        o = append_str(out, o, cap, s ? s->user.city_state : "");
        i += 3; continue;
      }
      if (c1 == 'S' && c2 == 'T') { /* ~ST - Street address */
        o = append_str(out, o, cap, s ? s->user.street : "");
        i += 3; continue;
      }
      if (c1 == 'Z' && c2 == 'P') { /* ~ZP - Zip code */
        o = append_str(out, o, cap, s ? s->user.zip_code : "");
        i += 3; continue;
      }
      if (c1 == 'P' && c2 == 'H') { /* ~PH - Phone number */
        o = append_str(out, o, cap, s ? s->user.phone : "");
        i += 3; continue;
      }
      if (c1 == 'F' && c2 == 'O') { /* ~FO - First time on date */
        o = append_str(out, o, cap, s ? s->user.first_on : "");
        i += 3; continue;
      }
      if (c1 == 'L' && c2 == 'O') { /* ~LO - Last on date */
        o = append_str(out, o, cap, s ? s->user.last_login_at : "");
        i += 3; continue;
      }
      if (c1 == 'T' && c2 == 'T') { /* ~TT - Total time on (minutes) */
        o = append_int(out, o, cap, s ? s->user.t_time_on : 0);
        i += 3; continue;
      }
      if (c1 == 'T' && c2 == 'L') { /* ~TL - Time left (minutes) */
        o = append_int(out, o, cap, s ? s->time_left_min : 0);
        i += 3; continue;
      }
      if (c1 == 'T' && c2 == 'B') { /* ~TB - Time bank balance */
        o = append_int(out, o, cap, s ? s->user.timebank : 0);
        i += 3; continue;
      }
      if (c1 == 'S' && c2 == 'L') { /* ~SL - Security level */
        o = append_int(out, o, cap, s ? s->user.level : 0);
        i += 3; continue;
      }
      if (c1 == 'D' && c2 == 'L') { /* ~DL - Downloads count */
        o = append_int(out, o, cap, s ? s->user.downloads : 0);
        i += 3; continue;
      }
      if (c1 == 'U' && c2 == 'L') { /* ~UL - Uploads count */
        o = append_int(out, o, cap, s ? s->user.uploads : 0);
        i += 3; continue;
      }
      if (c1 == 'D' && c2 == 'K') { /* ~DK - Download KB */
        o = append_int(out, o, cap, s ? s->user.dk : 0);
        i += 3; continue;
      }
      if (c1 == 'U' && c2 == 'K') { /* ~UK - Upload KB */
        o = append_int(out, o, cap, s ? s->user.uk : 0);
        i += 3; continue;
      }
      if (c1 == 'M' && c2 == 'P') { /* ~MP - Messages posted */
        o = append_int(out, o, cap, s ? s->user.msg_post : 0);
        i += 3; continue;
      }
      if (c1 == 'E' && c2 == 'S') { /* ~ES - Emails sent */
        o = append_int(out, o, cap, s ? s->user.email_sent : 0);
        i += 3; continue;
      }
      if (c1 == 'F' && c2 == 'B') { /* ~FB - Feedback count */
        o = append_int(out, o, cap, s ? s->user.feedback : 0);
        i += 3; continue;
      }
      if (c1 == 'C' && c2 == 'R') { /* ~CR - Credits */
        o = append_int(out, o, cap, s ? s->user.credits : 0);
        i += 3; continue;
      }
      if (c1 == 'F' && c2 == 'P') { /* ~FP - File points */
        o = append_int(out, o, cap, s ? s->user.file_points : 0);
        i += 3; continue;
      }
      if (c1 == 'L' && c2 == 'G') { /* ~LG - Total logons */
        o = append_int(out, o, cap, s ? s->user.logged_on : 0);
        i += 3; continue;
      }
      if (c1 == 'N' && c2 == 'N') { /* ~NN - Node number */
        o = append_int(out, o, cap, s ? s->node_num : 0);
        i += 3; continue;
      }
      
      /* System MCI codes */
      if (c1 == 'B' && c2 == 'N') { /* ~BN - BBS name */
        o = append_str(out, o, cap, s ? s->cfg.bbs_name : "Mutineer BBS");
        i += 3; continue;
      }
      if (c1 == 'S' && c2 == 'N') { /* ~SN - Sysop name */
        o = append_str(out, o, cap, s ? s->cfg.sysop_name : "Sysop");
        i += 3; continue;
      }
      if (c1 == 'B' && c2 == 'P') { /* ~BP - BBS phone */
        o = append_str(out, o, cap, "N/A");
        i += 3; continue;
      }
      if (c1 == 'T' && c2 == 'C') { /* ~TC - Total calls */
        int cnt = s ? db_stats_get_val(s->db, "calls") : 0;
        o = append_int(out, o, cap, cnt);
        i += 3; continue;
      }
      if (c1 == 'N' && c2 == 'U') { /* ~NU - Number of users */
        int cnt = s ? db_count_users(s->db) : 0;
        o = append_int(out, o, cap, cnt);
        i += 3; continue;
      }
      if (c1 == 'N' && c2 == 'F') { /* ~NF - Number of files */
        int cnt = s ? db_count_files(s->db) : 0;
        o = append_int(out, o, cap, cnt);
        i += 3; continue;
      }
      if (c1 == 'N' && c2 == 'M') { /* ~NM - Number of messages */
        int cnt = s ? db_count_messages(s->db) : 0;
        o = append_int(out, o, cap, cnt);
        i += 3; continue;
      }
      if (c1 == 'N' && c2 == 'O') { /* ~NO - Number online */
        DbNode nodes[64];
        int n = s ? db_node_list(s->db, nodes, 64) : 0;
        o = append_int(out, o, cap, n);
        i += 3; continue;
      }
      if (c1 == 'V' && c2 == 'R') { /* ~VR - Version string */
        o = append_str(out, o, cap, "Mutineer 1.0");
        i += 3; continue;
      }
      if (c1 == 'D' && c2 == 'A') { /* ~DA - Date */
        char buf[16];
        time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
        strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
        o = append_str(out, o, cap, buf);
        i += 3; continue;
      }
      if (c1 == 'T' && c2 == 'M') { /* ~TM - Time */
        char buf[16];
        time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
        strftime(buf, sizeof(buf), "%H:%M", &tm);
        o = append_str(out, o, cap, buf);
        i += 3; continue;
      }
      
      /* Area MCI codes */
      if (c1 == 'A' && c2 == 'N') { /* ~AN - Area name */
        if (s && s->current_msg_area > 0) {
          DbMsgArea area;
          if (db_msg_area_get(s->db, s->current_msg_area, &area)) {
            o = append_str(out, o, cap, area.name);
          }
        } else if (s && s->current_file_area > 0) {
          DbFileArea area;
          if (db_file_area_get(s->db, s->current_file_area, &area)) {
            o = append_str(out, o, cap, area.name);
          }
        }
        i += 3; continue;
      }
      if (c1 == 'A' && c2 == 'R') { /* ~AR - AR flags list */
        char buf[32] = {0};
        int pos = 0;
        if (s) {
          for (int bit = 0; bit < 26 && pos + 1 < (int)sizeof(buf); bit++) {
            if (s->user.flags & (1u << bit)) buf[pos++] = (char)('A' + bit);
          }
        }
        o = append_str(out, o, cap, buf);
        i += 3; continue;
      }
    }
    
    /* Percent codes (%XX) - Legacy MCI */
    if (in[i] == '%' && in[i+1]) {
      char code1 = in[i+1];
      char code2 = in[i+2];
      
      if (code1 == 'N' && code2 == 'L') { /* %NL - newline (skip) */
        i += 3;
        continue;
      }
      if (code1 == 'P' && code2 == 'E') { /* %PE pause */
        o = append_str(out, o, cap, "\r\n(press ENTER)\r\n");
        i += 3;
        continue;
      }
      if (code1 == 'U' && code2 == 'N') { /* %UN user name */
        o = append_str(out, o, cap, s ? s->user.handle : "");
        i += 3;
        continue;
      }
      if (code1 == 'T' && code2 == 'I') { /* %TI time remaining (min) */
        o = append_int(out, o, cap, s ? s->time_left_min : 0);
        i += 3;
        continue;
      }
      if (code1 == 'N' && code2 == 'N') { /* %NN node number */
        o = append_int(out, o, cap, s ? s->node_num : 0);
        i += 3;
        continue;
      }
      if (code1 == 'D' && code2 == 'A') { /* %DA date YYYY-MM-DD */
        char buf[16];
        time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
        strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
        o = append_str(out, o, cap, buf);
        i += 3; continue;
      }
      if (code1 == 'T' && code2 == 'M') { /* %TM time HH:MM */
        char buf[16];
        time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
        strftime(buf, sizeof(buf), "%H:%M", &tm);
        o = append_str(out, o, cap, buf);
        i += 3; continue;
      }
      if (code1 == 'P' && code2 == 'O') { /* %PO user posts */
        o = append_int(out, o, cap, s ? s->user.msg_post : 0);
        i += 3; continue;
      }
      if (code1 == 'M' && code2 == 'T') { /* %MT total messages */
        int cnt = s ? db_count_messages(s->db) : 0;
        o = append_int(out, o, cap, cnt);
        i += 3; continue;
      }
      if (code1 == 'F' && code2 == 'T') { /* %FT total files */
        int cnt = s ? db_count_files(s->db) : 0;
        o = append_int(out, o, cap, cnt);
        i += 3; continue;
      }
      if (code1 == 'T' && code2 == 'L') { /* %TL time left */
        o = append_int(out, o, cap, s ? s->time_left_min : 0);
        i += 3; continue;
      }
      if (code1 == 'C' && code2 == 'R') { /* %CR credits */
        o = append_int(out, o, cap, s ? s->user.credits : 0);
        i += 3; continue;
      }
      if (code1 == 'F' && code2 == 'P') { /* %FP file points */
        o = append_int(out, o, cap, s ? s->user.file_points : 0);
        i += 3; continue;
      }
      if (code1 == 'S' && code2 == 'L') { /* %SL security level */
        o = append_int(out, o, cap, s ? s->user.level : 0);
        i += 3; continue;
      }
      if (code1 == 'M' && code2 == 'A') { /* %MA messages in area */
        int cnt = 0;
        if (s && s->current_msg_area > 0) cnt = db_count_messages_area(s->db, s->current_msg_area);
        else if (s) cnt = db_count_messages(s->db);
        o = append_int(out, o, cap, cnt);
        i += 3; continue;
      }
      if (code1 == 'F' && code2 == 'A') { /* %FA files in area */
        int cnt = 0;
        if (s && s->current_file_area > 0) cnt = db_count_files_area(s->db, s->current_file_area);
        else if (s) cnt = db_count_files(s->db);
        o = append_int(out, o, cap, cnt);
        i += 3; continue;
      }
      if (code1 == 'N' && code2 == 'O') { /* %NO number online */
        DbNode nodes[64];
        int n = s ? db_node_list(s->db, nodes, 64) : 0;
        o = append_int(out, o, cap, n);
        i += 3; continue;
      }
      if (code1 == 'A' && code2 == 'R') { /* %AR list flags */
        char buf[32] = {0};
        int pos = 0;
        if (s) {
          for (int bit = 0; bit < 26 && pos + 1 < (int)sizeof(buf); bit++) {
            if (s->user.flags & (1u << bit)) buf[pos++] = (char)('A' + bit);
          }
        }
        o = append_str(out, o, cap, buf);
        i += 3; continue;
      }
      if (code1 == '?' ) { /* %?ACS{then|else} */
        const char* start = in + i + 2;
        const char* brace = strchr(start, '{');
        const char* close = brace ? strchr(brace, '}') : NULL;
        if (!brace || !close) { out[o++] = in[i++]; continue; }
        char expr[128];
        snprintf(expr, sizeof(expr), "%.*s", (int)(brace - start), start);
        char body[256];
        snprintf(body, sizeof(body), "%.*s", (int)(close - brace - 1), brace + 1);
        char thenp[128] = {0}, elsep[128] = {0};
        const char* bar = strchr(body, '|');
        if (bar) {
          snprintf(thenp, sizeof(thenp), "%.*s", (int)(bar - body), body);
          mci_copy(elsep, sizeof(elsep), bar + 1);
        } else {
          mci_copy(thenp, sizeof(thenp), body);
        }
        bool cond = acs_allows(s, expr);
        const char* choose = cond ? thenp : elsep;
        o = append_str(out, o, cap, choose);
        i = (size_t)(close - in) + 1;
        continue;
      }
    }
    
    out[o++] = in[i++];
  }
  out[o] = 0;
  return o;
}

const char* mci_scheme_name(int scheme_id) {
  if (scheme_id < 0 || scheme_id >= NUM_COLOR_SCHEMES) return NULL;
  return color_scheme_names[scheme_id];
}

void mci_scheme_preview(const char* label, int scheme_id, char* buf, size_t cap) {
  if (scheme_id < 0 || scheme_id >= NUM_COLOR_SCHEMES || !buf || cap < 64) return;
  const int* s = color_scheme_table[scheme_id];
  snprintf(buf, cap, "%s%s[%d] %-16s%s %s^0%s %s^1%s %s^2%s %s^4%s %s^6%s\x1b[0m",
           ansi_colors[s[8]], label ? label : "",
           scheme_id, color_scheme_names[scheme_id], "\x1b[0m",
           ansi_colors[s[0]], "\x1b[0m",
           ansi_colors[s[1]], "\x1b[0m",
           ansi_colors[s[2]], "\x1b[0m",
           ansi_colors[s[4]], "\x1b[0m",
           ansi_colors[s[6]], "\x1b[0m");
}
