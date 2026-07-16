#include "bbs_session.h"
#include "bbs_db.h"
#include "bbs_acs.h"
#include "bbs_flags.h"
#include "bbs_util.h"
#include "bbs_doors.h"
#include "bbs_msg_cmds.h"
#include "bbs_fido_netmail.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>

#define INBOX_MAX_MSGS 500   /* max private messages per user inbox */

static bool area_password_cached(Session *s, char kind, int area_id) {
  if (!s) return false;
  for (int i = 0; i < s->area_password_cache_count; i++) {
    if (s->area_password_cache[i].kind == kind &&
        s->area_password_cache[i].area_id == area_id) return true;
  }
  return false;
}

static void area_password_cache_add(Session *s, char kind, int area_id) {
  if (!s || area_password_cached(s, kind, area_id)) return;
  int i = s->area_password_cache_count;
  if (i >= (int)(sizeof(s->area_password_cache) / sizeof(s->area_password_cache[0]))) {
    i = 0;
  } else {
    s->area_password_cache_count++;
  }
  s->area_password_cache[i].kind = kind;
  s->area_password_cache[i].area_id = area_id;
}

static bool msg_area_password_ok(Session *s, const DbMsgArea *area) {
  if (!s || !area || !area->password[0]) return true;
  if (area_password_cached(s, 'M', area->id)) return true;
  char entered[64] = {0};
  char prompt[128];
  snprintf(prompt, sizeof(prompt), "Password for %s: ", area->name);
  if (prompt_password(s, prompt, entered, sizeof(entered)) < 0) return false;
  if (strcmp(entered, area->password) != 0) {
    send_str(s, "\r\nAccess denied.\r\n");
    return false;
  }
  area_password_cache_add(s, 'M', area->id);
  return true;
}

static bool msg_area_can(Session *s, int area_id, bool post, DbMsgArea *area_out) {
  if (!s) return false;
  if (area_id <= 0) return true; /* private mail has its own recipient checks */
  DbMsgArea area;
  if (!db_msg_area_get(s->db, area_id, &area)) {
    send_str(s, "\r\nMessage area not found.\r\n");
    return false;
  }
  const char *acs = post ? area.acs_post : area.acs_read;
  if (!acs || !acs[0]) acs = area.acs;
  if (acs && acs[0] && !acs_allows(s, acs)) {
    send_str(s, "\r\nAccess denied.\r\n");
    return false;
  }
  if (!msg_area_password_ok(s, &area)) return false;
  if (area_out) *area_out = area;
  return true;
}

static bool msg_can_read(Session *s, int msg_id, DbMessage *msg_out) {
  DbMessage msg;
  if (!db_message_get(s->db, msg_id, &msg)) {
    send_str(s, "\r\nMessage not found.\r\n");
    return false;
  }
  if (!msg_area_can(s, msg.area_id, false, NULL)) return false;
  if (msg_out) *msg_out = msg;
  return true;
}

static bool msg_can_reply(Session *s, int msg_id, DbMessage *msg_out) {
  DbMessage msg;
  if (!msg_can_read(s, msg_id, &msg)) return false;
  if (!msg_area_can(s, msg.area_id, true, NULL)) return false;
  if (msg_out) *msg_out = msg;
  return true;
}

static void msg_display(Session *s, const DbMessage *msg) {
  char buf[256];
  send_str(s, "\r\n\x1b[1;33m============================================\x1b[0m\r\n");
  snprintf(buf, sizeof(buf), "\x1b[1;37mFrom:\x1b[0m %s\r\n", msg->user_handle);
  send_str(s, buf);
  if (msg->to_name[0]) {
    snprintf(buf, sizeof(buf), "\x1b[1;37mTo:\x1b[0m %s\r\n", msg->to_name);
    send_str(s, buf);
  }
  snprintf(buf, sizeof(buf), "\x1b[1;37mSubject:\x1b[0m %s\r\n", msg->subject);
  send_str(s, buf);
  snprintf(buf, sizeof(buf), "\x1b[1;37mDate:\x1b[0m %s\r\n", msg->created_at);
  send_str(s, buf);
  if (msg->reply_to > 0) {
    snprintf(buf, sizeof(buf), "\x1b[1;37mReply to:\x1b[0m #%d\r\n", msg->reply_to);
    send_str(s, buf);
  }
  send_str(s, "\x1b[1;33m============================================\x1b[0m\r\n");
  send_str(s, msg->body);
  send_str(s, "\r\n\x1b[1;33m============================================\x1b[0m\r\n");
}

static bool safe_basename(const char *name) {
  if (!name || !name[0] || strlen(name) > 127) return false;
  if (strstr(name, "..")) return false;
  for (const char *p = name; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (*p == '/' || *p == '\\' || c < 32) return false;
  }
  return true;
}

static bool attachment_path(Session *s, const char *name, char *out, size_t outcap) {
  if (!safe_basename(name)) return false;
  const char *data = (s && s->cfg.data_path[0]) ? s->cfg.data_path : "data";
  char root[512], joined[512], realroot[512], realjoined[512];
  path_join(data, "attachments", root, sizeof(root));
  path_join(root, name, joined, sizeof(joined));
  if (!realpath(root, realroot) || !realpath(joined, realjoined)) return false;
  size_t n = strlen(realroot);
  if (strncmp(realroot, realjoined, n) != 0 ||
      (realjoined[n] != '/' && realjoined[n] != '\0')) return false;
  snprintf(out, outcap, "%s", realjoined);
  return true;
}

/* Append signature and/or tagline to a message body buffer. */
static void append_sig_tag(Session *s, char *body, size_t cap) {
  if (s->user.use_signature && s->user.signature[0]) {
    bbs_str_appendf(body, cap, "\r\n-- \r\n%s", s->user.signature);
  }
  if (s->user.use_tagline && s->user.tagline[0]) {
    bbs_str_appendf(body, cap, "\r\n * %s", s->user.tagline);
  }
}

/* Compose a message body using the line editor or FSEditor based on user pref. */
static int compose_body(Session *s, char *body, size_t cap) {
  body[0] = '\0';
  if (s->user.use_fse) {
    return fsedit_edit(s, body, cap);
  }
  send_str(s, "Enter message (blank line to end):\r\n");
  char line[256];
  while (cap > 256 && strlen(body) < cap - 256) {
    int n = prompt_line(s, "", line, sizeof(line));
    if (n < 0) return -1; /* disconnected */
    if (!line[0]) break;
    if (!bbs_str_append(body, cap, line) ||
        !bbs_str_append(body, cap, "\r\n"))
      break;
  }
  return (int)strlen(body);
}

/* Queue a message for echomail export if its area has a fido echolink. */
static void maybe_queue_echomail(Session *s, int area_id, int message_id) {
  DbFidoEcholink el;
  if (db_fido_echolink_get_by_area(s->db, area_id, &el))
    db_fido_echo_queue_add(s->db, el.id, message_id);
}

/* MA - Change message area */
void cmd_msg_area_change(Session* s, const char* data) {
  if (!s) return;
  DbMsgArea areas[32];
  int acount = db_msg_area_list(s->db, areas, 32);
  if (acount == 0) { send_str(s, "\r\nNo message areas.\r\n"); return; }
  
  if (data && data[0]) {
    int area_id = atoi(data);
    for (int i = 0; i < acount; i++) {
      if (areas[i].id == area_id && msg_area_can(s, areas[i].id, false, NULL)) {
        s->current_msg_area = area_id;
        char buf[128];
        snprintf(buf, sizeof(buf), "\r\nMessage area changed to: %s\r\n", areas[i].name);
        send_str(s, buf);
        return;
      }
    }
    send_str(s, "\r\nInvalid area or access denied.\r\n");
    return;
  }
  
  send_str(s, "\r\nMessage Areas:\r\n");
  char buf[256];
  for (int i = 0; i < acount; i++) {
    if (!msg_area_can(s, areas[i].id, false, NULL)) continue;
    int mcount = db_count_messages_area(s->db, areas[i].id);
    snprintf(buf, sizeof(buf), "  [%2d] %-30s (%d msgs)\r\n", areas[i].id, areas[i].name, mcount);
    send_str(s, buf);
  }
  
  char line[16];
  prompt_line(s, "\r\nSelect area: ", line, sizeof(line));
  if (!line[0]) return;
  
  int area_id = atoi(line);
  for (int i = 0; i < acount; i++) {
    if (areas[i].id == area_id) {
      if (!msg_area_can(s, areas[i].id, false, NULL)) return;
      s->current_msg_area = area_id;
      snprintf(buf, sizeof(buf), "\r\nMessage area changed to: %s\r\n", areas[i].name);
      send_str(s, buf);
      return;
    }
  }
  send_str(s, "\r\nArea not found.\r\n");
}

/* MG - Message area listing (groups) */
void cmd_msg_area_list(Session* s, const char* data) {
  (void)data;
  DbMsgArea areas[32];
  int acount = db_msg_area_list(s->db, areas, 32);
  if (acount == 0) { send_str(s, "\r\nNo message areas.\r\n"); return; }
  
  send_str(s, "\r\n\x1b[1;36mMessage Areas:\x1b[0m\r\n");
  send_str(s, "\x1b[1;33m-------------------------------------------\x1b[0m\r\n");
  
  char buf[256];
  int total_msgs = 0;
  for (int i = 0; i < acount; i++) {
    if (!msg_area_can(s, areas[i].id, false, NULL)) continue;
    int mcount = db_count_messages_area(s->db, areas[i].id);
    total_msgs += mcount;
    const char* marker = (areas[i].id == s->current_msg_area) ? "*" : " ";
    snprintf(buf, sizeof(buf), " %s[%2d] %-28s %5d msgs\r\n", 
             marker, areas[i].id, areas[i].name, mcount);
    send_str(s, buf);
  }
  
  send_str(s, "\x1b[1;33m-------------------------------------------\x1b[0m\r\n");
  snprintf(buf, sizeof(buf), "Total: %d messages in %d areas\r\n", total_msgs, acount);
  send_str(s, buf);
}

/* MR - Read messages */
void cmd_msg_read(Session* s, const char* data) {
  (void)data;
  if (s->current_msg_area <= 0) {
    send_str(s, "\r\nNo message area selected. Use MA to select one.\r\n");
    return;
  }
  
  DbMsgArea area;
  if (!msg_area_can(s, s->current_msg_area, false, &area)) return;
  
  DbMessage msgs[50];
  int mcount = db_messages_list(s->db, s->current_msg_area, msgs, 50);
  
  char buf[512];
  snprintf(buf, sizeof(buf), "\r\n\x1b[1;36mMessages in %s:\x1b[0m\r\n", area.name);
  send_str(s, buf);
  send_str(s, "\x1b[1;33m  #   From              Subject                        Date\x1b[0m\r\n");
  send_str(s, "--------------------------------------------------------------\r\n");
  
  for (int i = 0; i < mcount; i++) {
    snprintf(buf, sizeof(buf), "%3d   %-16s  %-30s %s\r\n",
             msgs[i].id, msgs[i].user_handle, msgs[i].subject, msgs[i].created_at);
    send_str(s, buf);
  }
  
  snprintf(buf, sizeof(buf), "\r\n%d message(s) listed.\r\n", mcount);
  send_str(s, buf);
  
  char line[16];
  prompt_line(s, "Read message #: ", line, sizeof(line));
  if (!line[0]) return;
  
  int msg_id = atoi(line);
  DbMessage msg;
  if (!msg_can_read(s, msg_id, &msg)) return;
  msg_display(s, &msg);
}

/* MP - Post message */
void cmd_msg_post(Session* s, const char* data) {
  (void)data;

  if (HAS_AC_FLAG(&s->user, AC_RPOST)) {
    send_str(s, "\r\n\x1b[1;31mYou are restricted from posting.\x1b[0m\r\n");
    return;
  }

  if (s->current_msg_area <= 0) {
    send_str(s, "\r\nNo message area selected. Use MA to select one.\r\n");
    return;
  }

  if (!msg_area_can(s, s->current_msg_area, true, NULL)) return;

  /* Offer to resume a saved draft for this area */
  DbDraft draft;
  bool has_draft = db_draft_get(s->db, s->user.id, &draft) &&
                   draft.area_id == s->current_msg_area;

  char subject[80] = {0}, body[2048] = {0};

  if (has_draft) {
    send_str(s, "\r\n\x1b[1;33mYou have a saved draft for this area.\x1b[0m\r\n");
    char buf[256];
    snprintf(buf, sizeof(buf), "Subject: %s\r\nResume draft? (Y/N): ", draft.subject);
    send_str(s, buf);
    uint8_t ch[4];
    if (session_readline(s, ch, sizeof(ch), 30) > 0 &&
        (ch[0] == 'Y' || ch[0] == 'y')) {
      snprintf(subject, sizeof(subject), "%s", draft.subject);
      snprintf(body, sizeof(body), "%s", draft.body);
      db_draft_delete(s->db, draft.id);
    }
  }

  if (!subject[0]) {
    int n = prompt_line(s, "Subject: ", subject, sizeof(subject));
    if (n < 0 || !subject[0]) return;
  }

  int r = compose_body(s, body, sizeof(body));
  if (r < 0) {
    /* Disconnected mid-compose — auto-save draft */
    if (subject[0] || body[0])
      db_draft_save(s->db, s->user.id, s->current_msg_area, 0, "", subject, body);
    return;
  }
  if (!body[0]) { send_str(s, "\r\nNo message body. Post cancelled.\r\n"); return; }

  append_sig_tag(s, body, sizeof(body));

  if (db_message_post(s->db, s->current_msg_area, s->user.id, subject, body, 0)) {
    int last_id = db_last_insert_id(s->db);
    maybe_queue_echomail(s, s->current_msg_area, last_id);
    send_str(s, "\r\nMessage posted.\r\n");
    s->user.msg_post++;
    db_stats_inc(s->db, "posts");
  } else {
    send_str(s, "\r\nFailed to post message.\r\n");
  }
}

/* MW - Write email (private mail) */
void cmd_msg_write_email(Session* s, const char* data) {
  if (HAS_AC_FLAG(&s->user, AC_REMAIL)) {
    send_str(s, "\r\n\x1b[1;31mYou are restricted from email.\x1b[0m\r\n");
    return;
  }

  char to[64] = {0};
  if (data && data[0])
    snprintf(to, sizeof(to), "%s", data);
  else {
    int n = prompt_line(s, "To: ", to, sizeof(to));
    if (n < 0 || !to[0]) return;
  }

  DbUser recipient;
  if (!db_user_fetch(s->db, to, &recipient)) {
    send_str(s, "\r\nUser not found.\r\n");
    return;
  }

  /* Enforce recipient mailbox capacity */
  int inbox_count = db_count_messages_to_user_inbox(s->db, recipient.id);
  if (inbox_count >= INBOX_MAX_MSGS) {
    char buf[128];
    snprintf(buf, sizeof(buf), "\r\n%s's mailbox is full (%d messages).\r\n",
             recipient.handle, inbox_count);
    send_str(s, buf);
    return;
  }

  /* CC recipients (comma-separated handles) */
  char cc_line[256] = {0};
  prompt_line(s, "CC (comma-separated, blank=none): ", cc_line, sizeof(cc_line));

  /* Offer to resume email draft */
  DbDraft draft;
  bool has_draft = db_draft_get(s->db, s->user.id, &draft) &&
                   draft.to_user_id == recipient.id && draft.area_id == 0;

  char subject[80] = {0}, body[2048] = {0};

  if (has_draft) {
    send_str(s, "\r\n\x1b[1;33mYou have a saved draft to this user.\x1b[0m\r\n");
    char buf[256];
    snprintf(buf, sizeof(buf), "Subject: %s\r\nResume draft? (Y/N): ", draft.subject);
    send_str(s, buf);
    uint8_t ch[4];
    if (session_readline(s, ch, sizeof(ch), 30) > 0 &&
        (ch[0] == 'Y' || ch[0] == 'y')) {
      snprintf(subject, sizeof(subject), "%s", draft.subject);
      snprintf(body, sizeof(body), "%s", draft.body);
      db_draft_delete(s->db, draft.id);
    }
  }

  if (!subject[0]) {
    int n = prompt_line(s, "Subject: ", subject, sizeof(subject));
    if (n < 0 || !subject[0]) return;
  }

  int r = compose_body(s, body, sizeof(body));
  if (r < 0) {
    if (subject[0] || body[0])
      db_draft_save(s->db, s->user.id, 0, recipient.id, recipient.handle, subject, body);
    return;
  }
  if (!body[0]) { send_str(s, "\r\nNo message body. Email cancelled.\r\n"); return; }

  append_sig_tag(s, body, sizeof(body));

  /* Send to primary recipient */
  if (db_message_post(s->db, 0, s->user.id, subject, body, 0)) {
    int last_id = db_last_insert_id(s->db);
    db_message_set_to_user(s->db, last_id, recipient.id);
    db_user_set_smw(s->db, recipient.id, 1);
    char buf[128];
    snprintf(buf, sizeof(buf), "\r\nEmail sent to %s.\r\n", recipient.handle);
    send_str(s, buf);
    s->user.email_sent++;
  } else {
    send_str(s, "\r\nFailed to send email.\r\n");
    return;
  }

  /* Process CC list */
  if (cc_line[0]) {
    char *p = cc_line;
    while (*p) {
      while (*p == ' ' || *p == ',') p++;
      if (!*p) break;
      char cc_handle[64] = {0};
      size_t i = 0;
      while (*p && *p != ',' && i < sizeof(cc_handle) - 1)
        cc_handle[i++] = *p++;
      cc_handle[i] = '\0';
      /* trim trailing space */
      while (i > 0 && cc_handle[i-1] == ' ') cc_handle[--i] = '\0';
      if (!cc_handle[0]) continue;
      if (strcasecmp(cc_handle, recipient.handle) == 0) continue; /* don't double-send */

      DbUser cc_user;
      if (!db_user_fetch(s->db, cc_handle, &cc_user)) continue;
      if (db_count_messages_to_user_inbox(s->db, cc_user.id) >= INBOX_MAX_MSGS) continue;

      char cc_subject[96];
      snprintf(cc_subject, sizeof(cc_subject), "[CC] %s", subject);
      if (db_message_post(s->db, 0, s->user.id, cc_subject, body, 0)) {
        int cc_id = db_last_insert_id(s->db);
        db_message_set_to_user(s->db, cc_id, cc_user.id);
        db_user_set_smw(s->db, cc_user.id, 1);
        char buf[128];
        snprintf(buf, sizeof(buf), "CC sent to %s.\r\n", cc_user.handle);
        send_str(s, buf);
      }
    }
  }
}

/* MN - New message scan */
void cmd_msg_new_scan(Session* s, const char* data) {
  (void)data;

  const char* since = s->user.last_login_at;
  if (!since || !since[0]) since = "1970-01-01";

  char buf[256];
  snprintf(buf, sizeof(buf), "\r\nNew messages since %s:\r\n", since);
  send_str(s, buf);

  DbMsgArea areas[32];
  int acount = db_msg_area_list(s->db, areas, 32);
  int found = 0;

  for (int a = 0; a < acount; a++) {
    if (!msg_area_can(s, areas[a].id, false, NULL)) continue;
    /* MZ: skip areas the user has excluded from scan */
    if (!db_user_scan_area_get(s->db, s->user.id, areas[a].id)) continue;

    DbMessage msgs[100];
    int mcount = db_messages_list(s->db, areas[a].id, msgs, 100);

    int area_new = 0;
    for (int m = 0; m < mcount; m++) {
      if (strcmp(msgs[m].created_at, since) > 0) { area_new++; found++; }
    }

    if (area_new > 0) {
      snprintf(buf, sizeof(buf), "  [%s] %d new message(s)\r\n", areas[a].name, area_new);
      send_str(s, buf);
    }
  }

  snprintf(buf, sizeof(buf), "\r\nTotal: %d new message(s).\r\n", found);
  send_str(s, buf);
}

/* MS - Message search */
void cmd_msg_search(Session* s, const char* data) {
  char pattern[64];
  
  if (data && data[0]) {
    snprintf(pattern, sizeof(pattern), "%s", data);
  } else {
    prompt_line(s, "Search pattern: ", pattern, sizeof(pattern));
    if (!pattern[0]) return;
  }
  
  send_str(s, "\r\nSearching all message areas...\r\n");
  
  DbMsgArea areas[32];
  int acount = db_msg_area_list(s->db, areas, 32);
  int found = 0;
  char buf[512];
  
  for (int a = 0; a < acount; a++) {
    if (!msg_area_can(s, areas[a].id, false, NULL)) continue;
    
    DbMessage msgs[100];
    int mcount = db_messages_list(s->db, areas[a].id, msgs, 100);
    
    for (int m = 0; m < mcount; m++) {
      /* Case-insensitive search in subject and body */
      char subj_lower[128], body_lower[1024], pat_lower[64];
      snprintf(subj_lower, sizeof(subj_lower), "%s", msgs[m].subject);
      snprintf(body_lower, sizeof(body_lower), "%s", msgs[m].body);
      snprintf(pat_lower, sizeof(pat_lower), "%s", pattern);
      
      for (char* p = subj_lower; *p; p++) *p = (char)tolower(*p);
      for (char* p = body_lower; *p; p++) *p = (char)tolower(*p);
      for (char* p = pat_lower; *p; p++) *p = (char)tolower(*p);
      
      if (strstr(subj_lower, pat_lower) || strstr(body_lower, pat_lower)) {
        snprintf(buf, sizeof(buf), "\r\n[%s] #%d %s - %s\r\n",
                 areas[a].name, msgs[m].id, msgs[m].subject, msgs[m].user_handle);
        send_str(s, buf);
        found++;
      }
    }
  }
  
  snprintf(buf, sizeof(buf), "\r\n%d message(s) found matching '%s'.\r\n", found, pattern);
  send_str(s, buf);
}

/* RE - Reply to message */
void cmd_msg_reply(Session* s, const char* data) {
  int msg_id = 0;
  
  if (data && data[0]) {
    msg_id = atoi(data);
  } else {
    char line[16];
    prompt_line(s, "Reply to message #: ", line, sizeof(line));
    if (!line[0]) return;
    msg_id = atoi(line);
  }
  
  if (HAS_AC_FLAG(&s->user, AC_RPOST)) {
    send_str(s, "\r\n\x1b[1;31mYou are restricted from posting.\x1b[0m\r\n");
    return;
  }
  
  DbMessage orig;
  if (!msg_can_reply(s, msg_id, &orig)) return;
  
  char buf[256];
  snprintf(buf, sizeof(buf), "\r\nReplying to: %s by %s\r\n", orig.subject, orig.user_handle);
  send_str(s, buf);
  
  /* Create quoted text */
  char quoted[512] = "";
  const char* p = orig.body;
  while (*p && strlen(quoted) < sizeof(quoted) - 64) {
    const char* nl = strchr(p, '\n');
    size_t len = nl ? (size_t)(nl - p) : strlen(p);
    if (len > 60) len = 60;
    char snippet[64];
    snprintf(snippet, sizeof(snippet), "%.*s", (int)len, p);
    if (!bbs_str_append(quoted, sizeof(quoted), "> ") ||
        !bbs_str_append(quoted, sizeof(quoted), snippet) ||
        !bbs_str_append(quoted, sizeof(quoted), "\r\n"))
      break;
    if (!nl) break;
    p = nl + 1;
  }
  
  send_str(s, "Quoted text:\r\n");
  send_str(s, quoted);
  
  char subject[80];
  snprintf(subject, sizeof(subject), "Re: %.75s", orig.subject);
  
  char body[2048] = {0};
  /* Pre-populate with quoted text */
  bbs_str_append(body, sizeof(body), quoted);
  bbs_str_append(body, sizeof(body), "\r\n");
  size_t used = strlen(body);
  int r = compose_body(s, body + used, sizeof(body) - used);
  if (r < 0) {
    if (body[0])
      db_draft_save(s->db, s->user.id, orig.area_id, 0, "", subject, body);
    return;
  }
  if (strlen(body) <= strlen(quoted) + 4) {
    send_str(s, "\r\nNo reply body. Reply cancelled.\r\n");
    return;
  }

  append_sig_tag(s, body, sizeof(body));

  if (db_message_post(s->db, orig.area_id, s->user.id, subject, body, msg_id)) {
    int last_id = db_last_insert_id(s->db);
    maybe_queue_echomail(s, orig.area_id, last_id);
    send_str(s, "\r\nReply posted.\r\n");
    s->user.msg_post++;
    db_stats_inc(s->db, "posts");
  } else {
    send_str(s, "\r\nFailed to post reply.\r\n");
  }
}

/* RP - Read private mail */
void cmd_msg_read_private(Session* s, const char* data) {
  (void)data;
  
  send_str(s, "\r\n\x1b[1;36mYour Private Mail:\x1b[0m\r\n");
  send_str(s, "\x1b[1;33m-------------------------------------------\x1b[0m\r\n");
  
  /* Query messages where to_user = current user */
  DbMessage msgs[50];
  int mcount = db_messages_to_user(s->db, s->user.id, msgs, 50);
  
  if (mcount == 0) {
    send_str(s, "No private mail.\r\n");
    return;
  }
  
  char buf[256];
  for (int i = 0; i < mcount; i++) {
    snprintf(buf, sizeof(buf), "%3d   %-16s  %-30s %s\r\n",
             msgs[i].id, msgs[i].user_handle, msgs[i].subject, msgs[i].created_at);
    send_str(s, buf);
  }
  
  snprintf(buf, sizeof(buf), "\r\n%d private message(s).\r\n", mcount);
  send_str(s, buf);
  
  /* Clear SMW flag */
  db_user_clear_smw(s->db, s->user.id);
  s->user.smw = 0;
}

/* RN - Read new messages */
void cmd_msg_read_new(Session* s, const char* data) {
  (void)data;
  
  if (s->current_msg_area <= 0) {
    send_str(s, "\r\nNo message area selected. Use MA to select one.\r\n");
    return;
  }
  if (!msg_area_can(s, s->current_msg_area, false, NULL)) return;
  
  const char* since = s->user.last_login_at;
  if (!since || !since[0]) {
    since = "1970-01-01";
  }
  
  DbMessage msgs[100];
  int mcount = db_messages_list(s->db, s->current_msg_area, msgs, 100);
  
  char buf[256];
  int shown = 0;
  
  for (int i = 0; i < mcount; i++) {
    if (strcmp(msgs[i].created_at, since) > 0) {
      if (shown == 0) {
        send_str(s, "\r\n\x1b[1;36mNew Messages:\x1b[0m\r\n");
        send_str(s, "\x1b[1;33m-------------------------------------------\x1b[0m\r\n");
      }
      
      snprintf(buf, sizeof(buf), "\r\n\x1b[1;37m#%d: %s\x1b[0m by %s (%s)\r\n",
               msgs[i].id, msgs[i].subject, msgs[i].user_handle, msgs[i].created_at);
      send_str(s, buf);
      send_str(s, msgs[i].body);
      send_str(s, "\r\n");
      shown++;
      
      if (shown % 5 == 0) {
        send_str(s, "\r\n(C)ontinue, (S)top? ");
        uint8_t line[8];
        int n = session_readline(s, line, sizeof(line), 30);
        if (n > 0 && (line[0] == 'S' || line[0] == 's')) break;
      }
    }
  }
  
  if (shown == 0) {
    send_str(s, "\r\nNo new messages.\r\n");
  } else {
    snprintf(buf, sizeof(buf), "\r\n%d new message(s) displayed.\r\n", shown);
    send_str(s, buf);
  }
}

/* RY - Your messages (messages you posted) */
void cmd_msg_your_messages(Session* s, const char* data) {
  (void)data;
  
  send_str(s, "\r\n\x1b[1;36mYour Posted Messages:\x1b[0m\r\n");
  send_str(s, "\x1b[1;33m-------------------------------------------\x1b[0m\r\n");
  
  DbMsgArea areas[32];
  int acount = db_msg_area_list(s->db, areas, 32);
  int found = 0;
  char buf[256];
  
  for (int a = 0; a < acount; a++) {
    if (!msg_area_can(s, areas[a].id, false, NULL)) continue;
    
    DbMessage msgs[100];
    int mcount = db_messages_list(s->db, areas[a].id, msgs, 100);
    
    for (int m = 0; m < mcount; m++) {
      if (msgs[m].user_id == s->user.id) {
        snprintf(buf, sizeof(buf), "[%s] #%d %s (%s)\r\n",
                 areas[a].name, msgs[m].id, msgs[m].subject, msgs[m].created_at);
        send_str(s, buf);
        found++;
      }
    }
  }
  
  snprintf(buf, sizeof(buf), "\r\n%d message(s) posted by you.\r\n", found);
  send_str(s, buf);
}

/* MY - Your Scan (messages addressed TO you) */
void cmd_msg_your_scan(Session* s, const char* data) {
  (void)data;
  
  send_str(s, "\r\n\x1b[1;36mMessages Addressed To You:\x1b[0m\r\n");
  send_str(s, "\x1b[1;33m-------------------------------------------\x1b[0m\r\n");
  
  DbMsgArea areas[32];
  int acount = db_msg_area_list(s->db, areas, 32);
  int found = 0;
  char buf[512];
  
  /* First check private mail (to_user field) */
  DbMessage private_msgs[50];
  int pcount = db_messages_to_user(s->db, s->user.id, private_msgs, 50);
  
  if (pcount > 0) {
    send_str(s, "\r\n\x1b[1;32mPrivate Mail:\x1b[0m\r\n");
    for (int i = 0; i < pcount; i++) {
      snprintf(buf, sizeof(buf), "  #%d From: %-16s Subject: %s (%s)\r\n",
               private_msgs[i].id, private_msgs[i].user_handle, 
               private_msgs[i].subject, private_msgs[i].created_at);
      send_str(s, buf);
      found++;
    }
  }
  
  /* Then scan all message areas for messages with to_name matching user handle */
  send_str(s, "\r\n\x1b[1;32mPublic Messages:\x1b[0m\r\n");
  
  for (int a = 0; a < acount; a++) {
    if (!msg_area_can(s, areas[a].id, false, NULL)) continue;
    
    DbMessage msgs[100];
    int mcount = db_messages_list(s->db, areas[a].id, msgs, 100);
    
    for (int m = 0; m < mcount; m++) {
      /* Check if to_name matches user handle (case-insensitive) */
      int match = 0;
      if (msgs[m].to_name[0]) {
        char to_lower[64], handle_lower[64];
        snprintf(to_lower, sizeof(to_lower), "%s", msgs[m].to_name);
        snprintf(handle_lower, sizeof(handle_lower), "%s", s->user.handle);
        for (char* p = to_lower; *p; p++) *p = (char)tolower(*p);
        for (char* p = handle_lower; *p; p++) *p = (char)tolower(*p);
        if (strcmp(to_lower, handle_lower) == 0) match = 1;
      }
      
      /* Also check if subject contains "Re:" and original was from user */
      if (!match && msgs[m].reply_to > 0) {
        DbMessage orig;
        if (db_message_get(s->db, msgs[m].reply_to, &orig)) {
          if (orig.user_id == s->user.id) match = 1;
        }
      }
      
      if (match) {
        snprintf(buf, sizeof(buf), "  [%s] #%d From: %-12s Subject: %s (%s)\r\n",
                 areas[a].name, msgs[m].id, msgs[m].user_handle, 
                 msgs[m].subject, msgs[m].created_at);
        send_str(s, buf);
        found++;
      }
    }
  }
  
  if (found == 0) {
    send_str(s, "  No messages addressed to you.\r\n");
  }
  
  send_str(s, "\x1b[1;33m-------------------------------------------\x1b[0m\r\n");
  snprintf(buf, sizeof(buf), "Total: %d message(s) addressed to you.\r\n", found);
  send_str(s, buf);
  
  /* Offer to read a message */
  char line[16];
  prompt_line(s, "\r\nRead message #: ", line, sizeof(line));
  if (!line[0]) return;
  
  int msg_id = atoi(line);
  DbMessage msg;
  if (!msg_can_read(s, msg_id, &msg)) return;
  msg_display(s, &msg);
}

/* RM - Edit own message */
void cmd_msg_edit(Session* s, const char* data) {
  int msg_id = 0;
  
  if (data && data[0]) {
    msg_id = atoi(data);
  } else {
    char line[16];
    prompt_line(s, "Edit message #: ", line, sizeof(line));
    if (!line[0]) return;
    msg_id = atoi(line);
  }
  
  DbMessage msg;
  if (!msg_can_reply(s, msg_id, &msg)) return;
  
  /* Check if user owns this message or is sysop */
  if (msg.user_id != s->user.id && !acs_allows(s, "+A")) {
    send_str(s, "\r\nYou can only edit your own messages.\r\n");
    return;
  }
  
  char buf[256];
  snprintf(buf, sizeof(buf), "\r\nEditing message #%d: %s\r\n", msg_id, msg.subject);
  send_str(s, buf);
  send_str(s, "\r\nCurrent body:\r\n");
  send_str(s, msg.body);
  send_str(s, "\r\n----------------------------------------------------------------------\r\n");
  
  send_str(s, "Enter new message body (blank line to end):\r\n");
  char body[2048] = "";
  char line[256];
  
  while (strlen(body) < sizeof(body) - 256) {
    prompt_line(s, "", line, sizeof(line));
    if (!line[0]) break;
    if (!bbs_str_append(body, sizeof(body), line) ||
        !bbs_str_append(body, sizeof(body), "\r\n"))
      break;
  }
  
  if (!body[0]) {
    send_str(s, "\r\nNo changes made.\r\n");
    return;
  }
  
  send_str(s, "\r\nSave changes? (Y/n): ");
  uint8_t confirm[8];
  int n = session_readline(s, confirm, sizeof(confirm), 30);
  if (n > 0 && (confirm[0] == 'N' || confirm[0] == 'n')) {
    send_str(s, "\r\nChanges discarded.\r\n");
    return;
  }
  
  if (db_message_update_body(s->db, msg_id, body)) {
    send_str(s, "\r\nMessage updated.\r\n");
  } else {
    send_str(s, "\r\nFailed to update message.\r\n");
  }
}

/* RC - Continuous read (read all without prompting) */
void cmd_msg_continuous_read(Session* s, const char* data) {
  (void)data;
  
  if (s->current_msg_area <= 0) {
    send_str(s, "\r\nNo message area selected. Use MA to select one.\r\n");
    return;
  }
  
  DbMsgArea area;
  if (!msg_area_can(s, s->current_msg_area, false, &area)) return;
  
  DbMessage msgs[100];
  int mcount = db_messages_list(s->db, s->current_msg_area, msgs, 100);
  
  if (mcount == 0) {
    send_str(s, "\r\nNo messages in this area.\r\n");
    return;
  }
  
  char buf[256];
  snprintf(buf, sizeof(buf), "\r\n\x1b[1;36mContinuous Read - %s\x1b[0m\r\n", area.name);
  send_str(s, buf);
  send_str(s, "Press Ctrl-C to stop...\r\n\r\n");
  
  for (int i = 0; i < mcount; i++) {
    /* Check for Ctrl-C (abort) */
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(s->fd, &rfds);
    struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
    if (select(s->fd + 1, &rfds, NULL, NULL, &tv) > 0) {
      uint8_t ch;
      if (recv(s->fd, &ch, 1, MSG_DONTWAIT) > 0 && ch == 0x03) {
        send_str(s, "\r\n\x1b[1;33m*** Aborted ***\x1b[0m\r\n");
        break;
      }
    }
    
    send_str(s, "\x1b[1;33m============================================\x1b[0m\r\n");
    snprintf(buf, sizeof(buf), "\x1b[1;37mMessage #%d of %d\x1b[0m\r\n", i + 1, mcount);
    send_str(s, buf);
    snprintf(buf, sizeof(buf), "\x1b[1;37mFrom:\x1b[0m %s\r\n", msgs[i].user_handle);
    send_str(s, buf);
    snprintf(buf, sizeof(buf), "\x1b[1;37mSubject:\x1b[0m %s\r\n", msgs[i].subject);
    send_str(s, buf);
    snprintf(buf, sizeof(buf), "\x1b[1;37mDate:\x1b[0m %s\r\n", msgs[i].created_at);
    send_str(s, buf);
    send_str(s, "\x1b[1;33m============================================\x1b[0m\r\n");
    send_str(s, msgs[i].body);
    send_str(s, "\r\n\r\n");
  }
  
  snprintf(buf, sizeof(buf), "\r\n%d message(s) displayed.\r\n", mcount);
  send_str(s, buf);
}

/* RQ - Quick scan (subject lines only) */
void cmd_msg_quick_scan(Session* s, const char* data) {
  (void)data;
  
  if (s->current_msg_area <= 0) {
    send_str(s, "\r\nNo message area selected. Use MA to select one.\r\n");
    return;
  }
  
  DbMsgArea area;
  if (!msg_area_can(s, s->current_msg_area, false, &area)) return;
  
  DbMessage msgs[100];
  int mcount = db_messages_list(s->db, s->current_msg_area, msgs, 100);
  
  char buf[256];
  snprintf(buf, sizeof(buf), "\r\n\x1b[1;36mQuick Scan - %s\x1b[0m\r\n", area.name);
  send_str(s, buf);
  send_str(s, "\x1b[1;33m  #   Subject                                          From\x1b[0m\r\n");
  send_str(s, "----------------------------------------------------------------------\r\n");
  
  for (int i = 0; i < mcount; i++) {
    snprintf(buf, sizeof(buf), "%3d   %-48s %-12s\r\n",
             msgs[i].id, msgs[i].subject, msgs[i].user_handle);
    send_str(s, buf);
  }
  
  send_str(s, "----------------------------------------------------------------------\r\n");
  snprintf(buf, sizeof(buf), "%d message(s).\r\n", mcount);
  send_str(s, buf);
}

/* RL - Message list (paginated) */
void cmd_msg_list(Session* s, const char* data) {
  (void)data;
  
  if (s->current_msg_area <= 0) {
    send_str(s, "\r\nNo message area selected. Use MA to select one.\r\n");
    return;
  }
  
  DbMsgArea area;
  if (!msg_area_can(s, s->current_msg_area, false, &area)) return;
  
  DbMessage msgs[100];
  int mcount = db_messages_list(s->db, s->current_msg_area, msgs, 100);
  
  char buf[256];
  snprintf(buf, sizeof(buf), "\r\n\x1b[1;36mMessage List - %s\x1b[0m\r\n", area.name);
  send_str(s, buf);
  send_str(s, "\x1b[1;33m  #   From            To              Subject                  Date\x1b[0m\r\n");
  send_str(s, "----------------------------------------------------------------------\r\n");
  
  int page_size = 15;
  int page = 0;
  
  while (page * page_size < mcount) {
    int start = page * page_size;
    int end = start + page_size;
    if (end > mcount) end = mcount;
    
    for (int i = start; i < end; i++) {
      snprintf(buf, sizeof(buf), "%3d   %-14s  %-14s  %-22s %s\r\n",
               msgs[i].id, 
               msgs[i].user_handle, 
               msgs[i].to_name[0] ? msgs[i].to_name : "All",
               msgs[i].subject, 
               msgs[i].created_at);
      send_str(s, buf);
    }
    
    if (end < mcount) {
      send_str(s, "\r\n(N)ext page, (P)rev page, (R)ead #, (Q)uit: ");
      uint8_t line[16];
      int n = session_readline(s, line, sizeof(line), 30);
      if (n <= 0 || line[0] == 'Q' || line[0] == 'q') break;
      if (line[0] == 'N' || line[0] == 'n') page++;
      else if (line[0] == 'P' || line[0] == 'p') { if (page > 0) page--; }
      else if (line[0] == 'R' || line[0] == 'r') {
        char num[16] = {0};
        prompt_line(s, "Message #: ", num, sizeof(num));
        int msg_id = atoi(num);
        DbMessage msg;
        if (msg_can_read(s, msg_id, &msg)) msg_display(s, &msg);
      }
    } else {
      break;
    }
  }
  
  send_str(s, "----------------------------------------------------------------------\r\n");
  snprintf(buf, sizeof(buf), "Total: %d message(s).\r\n", mcount);
  send_str(s, buf);
}

/* RJ - Jump to reply (follow reply chain) */
void cmd_msg_jump_reply(Session* s, const char* data) {
  int msg_id = 0;
  
  if (data && data[0]) {
    msg_id = atoi(data);
  } else {
    char line[16];
    prompt_line(s, "Jump to reply of message #: ", line, sizeof(line));
    if (!line[0]) return;
    msg_id = atoi(line);
  }
  
  DbMessage msg;
  if (!msg_can_read(s, msg_id, &msg)) return;
  
  /* Find replies to this message */
  DbMessage replies[50];
  int reply_count = 0;
  
  DbMessage all_msgs[100];
  int mcount = db_messages_list(s->db, msg.area_id, all_msgs, 100);
  
  for (int i = 0; i < mcount && reply_count < 50; i++) {
    if (all_msgs[i].reply_to == msg_id) {
      replies[reply_count++] = all_msgs[i];
    }
  }
  
  char buf[256];
  snprintf(buf, sizeof(buf), "\r\nReplies to message #%d (%s):\r\n", msg_id, msg.subject);
  send_str(s, buf);
  
  if (reply_count == 0) {
    send_str(s, "  No replies found.\r\n");
    
    /* Offer to jump to what this message replies to */
    if (msg.reply_to > 0) {
      snprintf(buf, sizeof(buf), "\r\nThis message is a reply to #%d. Jump there? (Y/n): ", msg.reply_to);
      send_str(s, buf);
      uint8_t line[8];
      int n = session_readline(s, line, sizeof(line), 30);
      if (n > 0 && line[0] != 'N' && line[0] != 'n') {
        DbMessage parent;
        if (msg_can_read(s, msg.reply_to, &parent)) msg_display(s, &parent);
      }
    }
    return;
  }
  
  send_str(s, "\x1b[1;33m  #   From            Subject                          Date\x1b[0m\r\n");
  for (int i = 0; i < reply_count; i++) {
    snprintf(buf, sizeof(buf), "%3d   %-14s  %-32s %s\r\n",
             replies[i].id, replies[i].user_handle, replies[i].subject, replies[i].created_at);
    send_str(s, buf);
  }
  
  /* Offer to read a reply */
  char line[16];
  prompt_line(s, "\r\nRead reply #: ", line, sizeof(line));
  if (!line[0]) return;
  
  int read_id = atoi(line);
  DbMessage reply;
  if (!msg_can_read(s, read_id, &reply)) return;
  msg_display(s, &reply);
}

/* RT - Thread view */
void cmd_msg_thread_view(Session* s, const char* data) {
  int msg_id = 0;
  
  if (data && data[0]) {
    msg_id = atoi(data);
  } else {
    char line[16];
    prompt_line(s, "View thread starting from message #: ", line, sizeof(line));
    if (!line[0]) return;
    msg_id = atoi(line);
  }
  
  DbMessage root;
  if (!msg_can_read(s, msg_id, &root)) return;
  
  /* Find the thread root */
  int thread_root_id = root.thread_root > 0 ? root.thread_root : root.id;
  
  send_str(s, "\r\n\x1b[1;36mThread View\x1b[0m\r\n");
  send_str(s, "----------------------------------------------------------------------\r\n");
  
  /* Get all messages in the thread */
  DbMessage thread_msgs[100];
  int thread_count = 0;
  db_message_reply_tree(s->db, root.area_id, thread_root_id, thread_msgs, 100, &thread_count);
  
  if (thread_count == 0) {
    /* Just show the single message */
    char buf[256];
    snprintf(buf, sizeof(buf), "#%d %s - %s (%s)\r\n",
             root.id, root.subject, root.user_handle, root.created_at);
    send_str(s, buf);
    thread_count = 1;
  } else {
    /* Build and display thread tree */
    char buf[256];
    for (int i = 0; i < thread_count; i++) {
      /* Calculate depth based on reply_to chain */
      int depth = 0;
      int parent = thread_msgs[i].reply_to;
      while (parent > 0 && depth < 10) {
        depth++;
        DbMessage parent_msg;
        if (db_message_get(s->db, parent, &parent_msg)) {
          parent = parent_msg.reply_to;
        } else {
          break;
        }
      }
      
      /* Create indent */
      char indent[32] = "";
      for (int d = 0; d < depth && d < 10; d++) {
        bbs_str_append(indent, sizeof(indent), "  ");
      }
      
      snprintf(buf, sizeof(buf), "%s%s#%d %s - %s (%s)\r\n",
               indent,
               depth > 0 ? "\\_ " : "",
               thread_msgs[i].id, 
               thread_msgs[i].subject, 
               thread_msgs[i].user_handle, 
               thread_msgs[i].created_at);
      send_str(s, buf);
    }
  }
  
  send_str(s, "----------------------------------------------------------------------\r\n");
  char buf[64];
  snprintf(buf, sizeof(buf), "%d message(s) in thread.\r\n", thread_count);
  send_str(s, buf);
  
  /* Offer to read a message */
  char line[16];
  prompt_line(s, "\r\nRead message #: ", line, sizeof(line));
  if (!line[0]) return;
  
  int read_id = atoi(line);
  DbMessage msg;
  if (!msg_can_read(s, read_id, &msg)) return;
  msg_display(s, &msg);
}

/* RV - View/Download attachment */
void cmd_msg_view_attachment(Session* s, const char* data) {
  int msg_id = 0;
  
  if (data && data[0]) {
    msg_id = atoi(data);
  } else {
    char line[16];
    prompt_line(s, "Message # with attachment: ", line, sizeof(line));
    if (!line[0]) return;
    msg_id = atoi(line);
  }
  
  DbMessage msg;
  if (!msg_can_read(s, msg_id, &msg)) return;
  
  if (!msg.file_attached[0]) {
    send_str(s, "\r\nThis message has no file attachment.\r\n");
    return;
  }
  
  char buf[256];
  snprintf(buf, sizeof(buf), "\r\nAttachment: %s\r\n", msg.file_attached);
  send_str(s, buf);
  
  /* Check if file exists in attachments directory */
  char filepath[512];
  if (!attachment_path(s, msg.file_attached, filepath, sizeof(filepath))) {
    send_str(s, "Attachment path is invalid or not accessible.\r\n");
    return;
  }
  
  FILE* f = fopen(filepath, "rb");
  if (!f) {
    send_str(s, "Attachment file not found on disk.\r\n");
    return;
  }
  
  /* Get file size */
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fclose(f);
  
  snprintf(buf, sizeof(buf), "Size: %ld bytes\r\n", size);
  send_str(s, buf);
  
  send_str(s, "\r\n(D)ownload or (C)ancel? ");
  uint8_t line[8];
  int n = session_readline(s, line, sizeof(line), 30);
  if (n <= 0 || (line[0] != 'D' && line[0] != 'd')) {
    send_str(s, "\r\nCancelled.\r\n");
    return;
  }
  
  /* Select protocol and download */
  DbProtocol protos[8];
  int pcnt = db_protocols_list(s->db, protos, 8, "down");
  
  if (pcnt == 0) {
    send_str(s, "\r\nNo download protocols configured.\r\n");
    return;
  }
  
  send_str(s, "\r\nSelect protocol:\r\n");
  for (int i = 0; i < pcnt; i++) {
    snprintf(buf, sizeof(buf), "  [%d] %s\r\n", i + 1, protos[i].name);
    send_str(s, buf);
  }
  
  char psel[8];
  prompt_line(s, "Protocol: ", psel, sizeof(psel));
  int pidx = atoi(psel) - 1;
  if (pidx < 0 || pidx >= pcnt) {
    send_str(s, "\r\nInvalid selection.\r\n");
    return;
  }
  
  send_str(s, "\r\nStarting download...\r\n");
  protocol_launch(s, &protos[pidx], filepath, "down");
  send_str(s, "\r\nDownload complete.\r\n");
}

/* MZ - Toggle scan flags for message areas */
void cmd_msg_toggle_scan_areas(Session* s, const char* data) {
  (void)data;

  DbMsgArea areas[32];
  int acount = db_msg_area_list(s->db, areas, 32);
  if (acount == 0) { send_str(s, "\r\nNo message areas.\r\n"); return; }

  send_str(s, "\r\n\x1b[1;36mNew-Message Scan Areas (MZ)\x1b[0m\r\n");
  send_str(s, "\x1b[1;33m  #   [Scan] Area\x1b[0m\r\n");
  send_str(s, "----------------------------------------------------------------------\r\n");

  char buf[256];
  for (int i = 0; i < acount; i++) {
    if (!msg_area_can(s, areas[i].id, false, NULL)) continue;
    int enabled = db_user_scan_area_get(s->db, s->user.id, areas[i].id);
    snprintf(buf, sizeof(buf), "  %2d  [%s]  %s\r\n",
             areas[i].id, enabled ? "ON " : "OFF", areas[i].name);
    send_str(s, buf);
  }

  send_str(s, "\r\nEnter area # to toggle (blank=done): ");
  char line[16];
  while (1) {
    int n = prompt_line(s, "", line, sizeof(line));
    if (n < 0 || !line[0]) break;
    int id = atoi(line);
    bool found = false;
    for (int i = 0; i < acount; i++) {
      if (areas[i].id == id && msg_area_can(s, areas[i].id, false, NULL)) {
        int cur = db_user_scan_area_get(s->db, s->user.id, areas[i].id);
        db_user_scan_area_set(s->db, s->user.id, areas[i].id, cur ? 0 : 1);
        snprintf(buf, sizeof(buf), "  %s scan %s.\r\nEnter area # to toggle (blank=done): ",
                 areas[i].name, cur ? "disabled" : "enabled");
        send_str(s, buf);
        found = true;
        break;
      }
    }
    if (!found) {
      send_str(s, "Invalid area. Enter area # to toggle (blank=done): ");
    }
  }
  send_str(s, "\r\nScan flags saved.\r\n");
}

/* Main message command dispatcher */
void handle_msg_command(Session* s, const char* cmd, const char* data) {
  if (!cmd || strlen(cmd) < 2) return;
  
  char c1 = (char)toupper(cmd[0]);
  char c2 = (char)toupper(cmd[1]);
  
  if (c1 == 'M') {
    switch (c2) {
      case 'A': cmd_msg_area_change(s, data); break;
      case 'G': cmd_msg_area_list(s, data); break;
      case 'N': cmd_msg_new_scan(s, data); break;
      case 'P': cmd_msg_post(s, data); break;
      case 'R': cmd_msg_read(s, data); break;
      case 'S': cmd_msg_search(s, data); break;
      case 'W': cmd_msg_write_email(s, data); break;
      case 'Y': cmd_msg_your_scan(s, data); break;  /* Your Scan - messages TO you */
      case 'Z': cmd_msg_toggle_scan_areas(s, data); break; /* MZ - Toggle scan flags */
      default:
        send_str(s, "\r\nUnknown message command.\r\n");
        break;
    }
  } else if (c1 == 'R') {
    switch (c2) {
      case 'A': cmd_msg_read(s, data); break;        /* Read all */
      case 'C': cmd_msg_continuous_read(s, data); break; /* Continuous read */
      case 'E': cmd_msg_reply(s, data); break;       /* Reply */
      case 'J': cmd_msg_jump_reply(s, data); break;  /* Jump to reply */
      case 'L': cmd_msg_list(s, data); break;        /* Message list */
      case 'M': cmd_msg_edit(s, data); break;        /* Edit own message */
      case 'N': cmd_msg_read_new(s, data); break;    /* Read new */
      case 'P': cmd_msg_read_private(s, data); break; /* Read private */
      case 'Q': cmd_msg_quick_scan(s, data); break;  /* Quick scan */
      case 'T': cmd_msg_thread_view(s, data); break; /* Thread view */
      case 'V': cmd_msg_view_attachment(s, data); break; /* View/download attachment */
      case 'Y': cmd_msg_your_messages(s, data); break; /* Your messages */
      default:
        send_str(s, "\r\nUnknown read command.\r\n");
        break;
    }
  }
}
