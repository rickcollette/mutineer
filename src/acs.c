#include "bbs_session.h"
#include "bbs_acs.h"
#include "bbs_flags.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Recursive-descent ACS parser with precedence: ! > & > |.
   Supports (matching ORIGINAL_BBS):
     S#   -> Security Level >= #
     D#   -> Download Security Level >= #
     F?   -> Has AR flag ? (A-Z)
     R?   -> Has AC (restriction) flag ? (A-Z)
     A#   -> Age >= #
     B#   -> Baud >= # * 100 (always true for telnet)
     C#   -> Total logins (calls) >= #
     C?   -> In conference ? (A-Z)
     E#   -> Has active subscription of type # (0 = any active)
     G?   -> Gender is ? (M/F)
     H#   -> Current hour = #
     N#   -> Node number = #
     P#   -> Credit >= #
     T#   -> Time remaining >= #
     U#   -> User number = #
     V    -> User is validated (not expired)
     W#   -> Day of week = # (0=Sun, 6=Sat)
     X#   -> Days until expiration <= #
     Z    -> Meets post ratio
     PC   -> Post/call ratio met (posts/logins >= post_ratio_num/post_ratio_den)
     DR   -> Download ratio met (uploads/downloads >= dl_ratio_num/dl_ratio_den)

   Legacy support:
     SLnn or Lnn  -> security level >= nn
     +X           -> AR flag X set
     ARABC        -> all listed AR flags set
     !expr        -> negation
     (expr)       -> grouping
     C>=N         -> credits >= N (old style)
     R>=N         -> ratio >= N (old style)
     T>=N         -> time >= N (old style)
*/

typedef struct {
  const char* s;
} Parser;

static void skip_ws(Parser* p) { while (*p->s && isspace((unsigned char)*p->s)) p->s++; }

static bool eval_expr(Parser* p, const Session* s);

static int calc_age(const char* birth_date) {
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

static int days_until_expiration(const char* expires_at) {
  if (!expires_at || !expires_at[0]) return 9999;
  
  struct tm exp_tm = {0};
  if (sscanf(expires_at, "%d-%d-%d", &exp_tm.tm_year, &exp_tm.tm_mon, &exp_tm.tm_mday) != 3) return 9999;
  exp_tm.tm_year -= 1900;
  exp_tm.tm_mon -= 1;
  
  time_t exp_time = mktime(&exp_tm);
  time_t now = time(NULL);
  
  double diff = difftime(exp_time, now);
  return (int)(diff / 86400.0);
}

static bool eval_term(Parser* p, const Session* s) {
  skip_ws(p);
  if (*p->s == '!') { p->s++; return !eval_term(p, s); }
  if (*p->s == '(') {
    p->s++;
    bool v = eval_expr(p, s);
    skip_ws(p);
    if (*p->s == ')') p->s++;
    return v;
  }

  const char* start = p->s;
  while (*p->s && !isspace((unsigned char)*p->s) && *p->s != '&' && *p->s != '|' && *p->s != ')') p->s++;
  size_t len = (size_t)(p->s - start);
  if (len == 0) return false;

  char tok[64]; snprintf(tok, sizeof(tok), "%.*s", (int)len, start);
  for (size_t i = 0; tok[i]; i++) tok[i] = (char)toupper((unsigned char)tok[i]);

  /* S# - Security Level >= # */
  if (tok[0] == 'S' && isdigit((unsigned char)tok[1])) {
    int need = atoi(tok + 1);
    return s && s->user.level >= need;
  }
  /* SLnn - Security Level >= nn (legacy) */
  if (tok[0] == 'S' && tok[1] == 'L') {
    int need = atoi(tok + 2);
    return s && s->user.level >= need;
  }
  /* Lnn - Security Level >= nn (legacy) */
  if (tok[0] == 'L' && isdigit((unsigned char)tok[1])) {
    int need = atoi(tok + 1);
    return s && s->user.level >= need;
  }
  
  /* D# - Download Security Level >= # */
  if (tok[0] == 'D' && isdigit((unsigned char)tok[1])) {
    int need = atoi(tok + 1);
    return s && s->user.dsl >= need;
  }
  
  /* F? - Has AR flag ? */
  if (tok[0] == 'F' && tok[1] >= 'A' && tok[1] <= 'Z' && tok[2] == '\0') {
    unsigned mask = 1u << (tok[1] - 'A');
    return s && (s->user.flags & mask);
  }
  
  /* R? - Has AC (restriction) flag ? (L,C,V,U,A,*,P,E,K,M,1,2,3,4) */
  if (tok[0] == 'R' && tok[2] == '\0') {
    unsigned mask = ac_flag_from_char(tok[1]);
    if (mask == 0) return false;
    return s && (s->user.ac_flags & mask);
  }
  
  /* A# - Age >= # */
  if (tok[0] == 'A' && isdigit((unsigned char)tok[1])) {
    int need = atoi(tok + 1);
    int age = s ? calc_age(s->user.birth_date) : 0;
    return age >= need;
  }
  
  /* B# - Baud >= # * 100 (always true for telnet) */
  if (tok[0] == 'B' && isdigit((unsigned char)tok[1])) {
    return true;
  }
  
  /* C# - Total logins (calls) >= # */
  if (tok[0] == 'C' && isdigit((unsigned char)tok[1])) {
    int need = atoi(tok + 1);
    return s && s->user.logged_on >= need;
  }

  /* C? - In conference ? */
  if (tok[0] == 'C' && tok[1] >= 'A' && tok[1] <= 'Z' && tok[2] == '\0') {
    return s && s->current_conf == (tok[1] - 'A');
  }
  
  /* G? - Gender is ? */
  if (tok[0] == 'G' && (tok[1] == 'M' || tok[1] == 'F') && tok[2] == '\0') {
    return s && s->user.sex == tok[1];
  }
  
  /* H# - Current hour = # */
  if (tok[0] == 'H' && isdigit((unsigned char)tok[1])) {
    int need = atoi(tok + 1);
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    return tm->tm_hour == need;
  }
  
  /* N# - Node number = # */
  if (tok[0] == 'N' && isdigit((unsigned char)tok[1])) {
    int need = atoi(tok + 1);
    return s && s->node_num == need;
  }
  
  /* P# - Credit >= # */
  if (tok[0] == 'P' && isdigit((unsigned char)tok[1])) {
    int need = atoi(tok + 1);
    return s && s->user.credits >= need;
  }
  
  /* T# - Time remaining >= # */
  if (tok[0] == 'T' && isdigit((unsigned char)tok[1])) {
    int need = atoi(tok + 1);
    return s && s->time_left_min >= need;
  }
  
  /* U# - User number = # */
  if (tok[0] == 'U' && isdigit((unsigned char)tok[1])) {
    int need = atoi(tok + 1);
    return s && s->user.id == need;
  }
  
  /* V - User is validated (not expired) */
  if (tok[0] == 'V' && tok[1] == '\0') {
    if (!s) return false;
    if (!s->user.expires_at[0]) return true;
    return days_until_expiration(s->user.expires_at) > 0;
  }
  
  /* W# - Day of week = # (0=Sun, 6=Sat) */
  if (tok[0] == 'W' && isdigit((unsigned char)tok[1])) {
    int need = atoi(tok + 1);
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    return tm->tm_wday == need;
  }
  
  /* X# - Days until expiration <= # */
  if (tok[0] == 'X' && isdigit((unsigned char)tok[1])) {
    int need = atoi(tok + 1);
    if (!s) return false;
    int days = days_until_expiration(s->user.expires_at);
    return days <= need;
  }
  
  /* Z - Meets post ratio */
  if (tok[0] == 'Z' && tok[1] == '\0') {
    if (!s) return false;
    if (s->user.post_ratio_den == 0) return true;
    int required = s->user.logged_on / s->user.post_ratio_den;
    return s->user.msg_post >= required;
  }

  /* PC - Post/call ratio met */
  if (tok[0] == 'P' && tok[1] == 'C' && tok[2] == '\0') {
    if (!s) return false;
    if (s->user.post_ratio_num == 0 || s->user.post_ratio_den == 0) return true;
    if (s->user.logged_on == 0) return true;
    /* posts/logins >= num/den  =>  posts * den >= logins * num */
    return (s->user.msg_post * s->user.post_ratio_den) >=
           (s->user.logged_on * s->user.post_ratio_num);
  }

  /* DR - Download ratio met */
  if (tok[0] == 'D' && tok[1] == 'R' && tok[2] == '\0') {
    if (!s) return false;
    if (s->user.dl_ratio_num == 0 || s->user.dl_ratio_den == 0) return true;
    if (s->user.downloads == 0) return true;
    /* uploads/downloads >= num/den  =>  uploads * den >= downloads * num */
    return (s->user.uploads * s->user.dl_ratio_den) >=
           (s->user.downloads * s->user.dl_ratio_num);
  }

  /* J# - Member of conference # */
  if (tok[0] == 'J' && isdigit((unsigned char)tok[1])) {
    int conf_id = atoi(tok + 1);
    if (!s || !s->db) return false;
    return db_conf_is_member(s->db, s->user.id, conf_id);
  }

  /* E# - Has active subscription of type # (0 = any active subscription) */
  if (tok[0] == 'E' && isdigit((unsigned char)tok[1])) {
    int type_id = atoi(tok + 1);
    if (!s || !s->db) return false;
    DbUserSubscription sub;
    if (!db_user_subscription_get(s->db, s->user.id, &sub)) return false;
    if (type_id == 0) return true;  /* E0 = any active subscription */
    return sub.subscription_type_id == type_id;
  }
  
  /* +X - AR flag X set (legacy) */
  if (tok[0] == '+') {
    char flag = tok[1];
    if (flag < 'A' || flag > 'Z') return false;
    unsigned mask = 1u << (flag - 'A');
    return s && (s->user.flags & mask);
  }
  
  /* ARABC - all listed AR flags set (legacy) */
  if (strncmp(tok, "AR", 2) == 0) {
    bool ok = true;
    for (size_t i = 2; tok[i]; i++) {
      char flag = tok[i];
      if (flag < 'A' || flag > 'Z') { ok = false; break; }
      unsigned mask = 1u << (flag - 'A');
      if (!(s && (s->user.flags & mask))) { ok = false; break; }
    }
    return ok;
  }
  
  /* C>=N - credits compare (legacy) */
  if (tok[0] == 'C' && (tok[1] == '>' || tok[1] == '<' || tok[1] == '=')) {
    const char* cmp = tok + 1;
    char op1 = cmp[0];
    char op2 = (cmp[1] == '=' ? '=' : 0);
    const char* num = cmp + (op2 ? 2 : 1);
    int val = atoi(num);
    int have = s ? s->user.credits : 0;
    if (op1 == '>' && op2 == '=') return have >= val;
    if (op1 == '<' && op2 == '=') return have <= val;
    if (op1 == '>') return have > val;
    if (op1 == '<') return have < val;
    if (op1 == '=') return have == val;
  }
  
  /* R>=N - ratio (legacy) */
  if (tok[0] == 'R' && (tok[1] == '>' || tok[1] == '<' || tok[1] == '=')) {
    const char* cmp = tok + 1;
    char op1 = cmp[0];
    char op2 = (cmp[1] == '=' ? '=' : 0);
    const char* num = cmp + (op2 ? 2 : 1);
    double need = atof(num);
    double ratio = 999.0;
    if (s && s->user.dl_ratio_den > 0) {
      ratio = (double)s->user.dl_ratio_num / (double)s->user.dl_ratio_den;
    }
    if (op1 == '>' && op2 == '=') return ratio >= need;
    if (op1 == '<' && op2 == '=') return ratio <= need;
    if (op1 == '>') return ratio > need;
    if (op1 == '<') return ratio < need;
    if (op1 == '=') return ratio == need;
  }
  
  /* T>=N - time remaining (legacy) */
  if (tok[0] == 'T' && (tok[1] == '>' || tok[1] == '<' || tok[1] == '=')) {
    const char* cmp = tok + 1;
    char op1 = cmp[0];
    char op2 = (cmp[1] == '=' ? '=' : 0);
    const char* num = cmp + (op2 ? 2 : 1);
    int need = atoi(num);
    int have = s ? s->time_left_min : 0;
    if (op1 == '>' && op2 == '=') return have >= need;
    if (op1 == '<' && op2 == '=') return have <= need;
    if (op1 == '>') return have > need;
    if (op1 == '<') return have < need;
    if (op1 == '=') return have == need;
  }
  
  return false;
}

static bool eval_factor(Parser* p, const Session* s) {
  bool left = eval_term(p, s);
  skip_ws(p);
  while (*p->s == '&') {
    p->s++;
    bool right = eval_term(p, s);
    left = left && right;
    skip_ws(p);
  }
  return left;
}

static bool eval_expr(Parser* p, const Session* s) {
  bool left = eval_factor(p, s);
  skip_ws(p);
  while (*p->s == '|') {
    p->s++;
    bool right = eval_factor(p, s);
    left = left || right;
    skip_ws(p);
  }
  return left;
}

bool acs_allows(const Session* s, const char* acs) {
  if (!acs || !acs[0]) return true;
  Parser p = { .s = acs };
  return eval_expr(&p, s);
}

/* Simplified ACS check for standalone tools (no session context).
   Only supports S#, D#, F?, and basic level checks. */
bool acs_check(const char* acs, int level, unsigned ar_flags, unsigned ac_flags) {
  if (!acs || !acs[0]) return true;
  
  /* Simple parser for common cases */
  const char* p = acs;
  while (*p) {
    while (*p && (isspace((unsigned char)*p) || *p == '&' || *p == '|')) p++;
    if (!*p) break;
    
    char tok[64];
    int i = 0;
    while (*p && !isspace((unsigned char)*p) && *p != '&' && *p != '|' && *p != ')' && i < 63) {
      tok[i++] = (char)toupper((unsigned char)*p++);
    }
    tok[i] = '\0';
    if (i == 0) break;
    
    /* S# - Security Level >= # */
    if (tok[0] == 'S' && isdigit((unsigned char)tok[1])) {
      int need = atoi(tok + 1);
      if (level < need) return false;
      continue;
    }
    /* SLnn - Security Level >= nn (legacy) */
    if (tok[0] == 'S' && tok[1] == 'L') {
      int need = atoi(tok + 2);
      if (level < need) return false;
      continue;
    }
    /* F? - Has AR flag ? */
    if (tok[0] == 'F' && tok[1] >= 'A' && tok[1] <= 'Z' && tok[2] == '\0') {
      unsigned mask = 1u << (tok[1] - 'A');
      if (!(ar_flags & mask)) return false;
      continue;
    }
    /* +X - AR flag X set (legacy) */
    if (tok[0] == '+' && tok[1] >= 'A' && tok[1] <= 'Z') {
      unsigned mask = 1u << (tok[1] - 'A');
      if (!(ar_flags & mask)) return false;
      continue;
    }
    /* R? - Has AC (restriction) flag ? */
    if (tok[0] == 'R' && tok[2] == '\0') {
      unsigned mask = ac_flag_from_char(tok[1]);
      if (mask && !(ac_flags & mask)) return false;
      continue;
    }
  }
  
  return true;
}
