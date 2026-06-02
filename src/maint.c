#include "bbs_session.h"
#include "bbs_db.h"
#include "bbs_acs.h"
#include "bbs_util.h"
#include "bbs_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern void send_str(Session* s, const char* str);
extern int prompt_line(Session* s, const char* prompt, char* out, size_t cap);
extern int session_readline(Session* s, uint8_t* buf, size_t cap, int timeout);

void cmd_user_pack(Session* s, const char* data) {
  (void)data;
  
  if (!acs_allows(s, "+A")) {
    send_str(s, "\r\nAccess denied.\r\n");
    return;
  }
  
  send_str(s, "\r\n\x1b[1;36mUser Pack Utility\x1b[0m\r\n");
  send_str(s, "----------------------------------------------------------------------\r\n");
  send_str(s, "This will remove deleted user accounts and renumber remaining users.\r\n");
  send_str(s, "\r\n\x1b[1;31mWARNING: This operation cannot be undone!\x1b[0m\r\n");
  send_str(s, "\r\nProceed? (Y/N): ");
  
  uint8_t line[8];
  int r = session_readline(s, line, sizeof(line), 30);
  if (r <= 0 || (line[0] != 'Y' && line[0] != 'y')) {
    send_str(s, "\r\nCancelled.\r\n");
    return;
  }
  
  send_str(s, "\r\nPacking users...\r\n");
  
#ifdef HAVE_SQLITE
  int deleted = db_exec_simple(s->db, "DELETE FROM users WHERE status_flags & 2");
  char buf[128];
  snprintf(buf, sizeof(buf), "Removed %d deleted user record(s).\r\n", deleted);
  send_str(s, buf);
  
  db_exec_simple(s->db, "VACUUM");
  send_str(s, "Database vacuumed.\r\n");
#else
  send_str(s, "Database not available.\r\n");
#endif
  
  log_audit(s->user.handle, "maint", "user_pack");
  send_str(s, "\r\nUser pack complete.\r\n");
}

void cmd_message_pack(Session* s, const char* data) {
  (void)data;
  
  if (!acs_allows(s, "+A")) {
    send_str(s, "\r\nAccess denied.\r\n");
    return;
  }
  
  send_str(s, "\r\n\x1b[1;36mMessage Pack Utility\x1b[0m\r\n");
  send_str(s, "----------------------------------------------------------------------\r\n");
  
  char days_str[16];
  prompt_line(s, "Delete messages older than (days, 0=none): ", days_str, sizeof(days_str));
  int days = atoi(days_str);
  
  if (days > 0) {
    send_str(s, "\r\n\x1b[1;31mWARNING: This will permanently delete old messages!\x1b[0m\r\n");
    send_str(s, "\r\nProceed? (Y/N): ");
    
    uint8_t line[8];
    int r = session_readline(s, line, sizeof(line), 30);
    if (r <= 0 || (line[0] != 'Y' && line[0] != 'y')) {
      send_str(s, "\r\nCancelled.\r\n");
      return;
    }
    
    send_str(s, "\r\nPacking messages...\r\n");
    
#ifdef HAVE_SQLITE
    char sql[256];
    snprintf(sql, sizeof(sql), 
             "DELETE FROM messages WHERE created_at < datetime('now', '-%d days')", days);
    int deleted = db_exec_simple(s->db, sql);
    char buf[128];
    snprintf(buf, sizeof(buf), "Removed %d old message(s).\r\n", deleted);
    send_str(s, buf);
    
    db_exec_simple(s->db, "VACUUM");
    send_str(s, "Database vacuumed.\r\n");
#else
    send_str(s, "Database not available.\r\n");
#endif
  } else {
    send_str(s, "\r\nNo messages deleted.\r\n");
  }
  
  log_audit(s->user.handle, "maint", "message_pack");
  send_str(s, "\r\nMessage pack complete.\r\n");
}

void cmd_file_pack(Session* s, const char* data) {
  (void)data;
  
  if (!acs_allows(s, "+A")) {
    send_str(s, "\r\nAccess denied.\r\n");
    return;
  }
  
  send_str(s, "\r\n\x1b[1;36mFile Pack Utility\x1b[0m\r\n");
  send_str(s, "----------------------------------------------------------------------\r\n");
  send_str(s, "This will remove orphaned file records (files no longer on disk).\r\n");
  send_str(s, "\r\nProceed? (Y/N): ");
  
  uint8_t line[8];
  int r = session_readline(s, line, sizeof(line), 30);
  if (r <= 0 || (line[0] != 'Y' && line[0] != 'y')) {
    send_str(s, "\r\nCancelled.\r\n");
    return;
  }
  
  send_str(s, "\r\nVerifying files...\r\n");
  
  /* Get all file areas */
  DbFileArea areas[64];
  int acount = db_file_area_list(s->db, areas, 64);
  
  int orphaned = 0;
  int verified = 0;
  char buf[256];
  
  for (int a = 0; a < acount; a++) {
    DbFileRec files[256];
    int fcount = db_file_list(&areas[a], s->db, files, 256);
    
    for (int f = 0; f < fcount; f++) {
      char filepath[512];
      snprintf(filepath, sizeof(filepath), "%s/%s", areas[a].path, files[f].filename);
      
      if (access(filepath, F_OK) != 0) {
        /* File doesn't exist on disk - mark as orphaned */
        snprintf(buf, sizeof(buf), "  Missing: %s/%s\r\n", areas[a].name, files[f].filename);
        send_str(s, buf);
        
        /* Delete the orphaned record */
        db_file_delete(s->db, files[f].id);
        orphaned++;
      } else {
        verified++;
      }
    }
  }
  
  snprintf(buf, sizeof(buf), "\r\nVerified: %d files, Removed: %d orphaned records\r\n", verified, orphaned);
  send_str(s, buf);
  
#ifdef HAVE_SQLITE
  db_exec_simple(s->db, "VACUUM");
  send_str(s, "Database vacuumed.\r\n");
#endif
  
  log_audit(s->user.handle, "maint", "file_pack");
  send_str(s, "\r\nFile pack complete.\r\n");
}

void cmd_rebuild_indexes(Session* s, const char* data) {
  (void)data;
  
  if (!acs_allows(s, "+A")) {
    send_str(s, "\r\nAccess denied.\r\n");
    return;
  }
  
  send_str(s, "\r\n\x1b[1;36mRebuild Indexes\x1b[0m\r\n");
  send_str(s, "----------------------------------------------------------------------\r\n");
  send_str(s, "Rebuilding database indexes...\r\n");
  
#ifdef HAVE_SQLITE
  db_exec_simple(s->db, "REINDEX");
  send_str(s, "Indexes rebuilt.\r\n");
  
  db_exec_simple(s->db, "ANALYZE");
  send_str(s, "Statistics updated.\r\n");
#else
  send_str(s, "Database not available.\r\n");
#endif
  
  log_audit(s->user.handle, "maint", "rebuild_indexes");
  send_str(s, "\r\nIndex rebuild complete.\r\n");
}

void cmd_system_stats(Session* s, const char* data) {
  (void)data;
  
  send_str(s, "\r\n\x1b[1;36mSystem Statistics\x1b[0m\r\n");
  send_str(s, "----------------------------------------------------------------------\r\n");
  
  char buf[256];
  
  int users = db_count_users(s->db);
  snprintf(buf, sizeof(buf), "Total Users:    %d\r\n", users);
  send_str(s, buf);
  
  int messages = db_count_messages(s->db);
  snprintf(buf, sizeof(buf), "Total Messages: %d\r\n", messages);
  send_str(s, buf);
  
  int files = db_count_files(s->db);
  snprintf(buf, sizeof(buf), "Total Files:    %d\r\n", files);
  send_str(s, buf);
  
  int calls = db_stats_get_val(s->db, "calls");
  snprintf(buf, sizeof(buf), "Total Calls:    %d\r\n", calls);
  send_str(s, buf);
  
  int oneliners = db_oneliner_count(s->db);
  snprintf(buf, sizeof(buf), "One-Liners:     %d\r\n", oneliners);
  send_str(s, buf);
  
  DbMsgArea areas[32];
  int acount = db_msg_area_list(s->db, areas, 32);
  snprintf(buf, sizeof(buf), "Message Areas:  %d\r\n", acount);
  send_str(s, buf);
  
  DbFileArea fareas[32];
  int fcount = db_file_area_list(s->db, fareas, 32);
  snprintf(buf, sizeof(buf), "File Areas:     %d\r\n", fcount);
  send_str(s, buf);
  
  send_str(s, "----------------------------------------------------------------------\r\n");
}

void cmd_maintenance_menu(Session* s, const char* data) {
  (void)data;
  
  if (!acs_allows(s, "+A")) {
    send_str(s, "\r\nAccess denied.\r\n");
    return;
  }
  
  while (s->alive) {
    send_str(s, "\r\n\x1b[1;36mSystem Maintenance\x1b[0m\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");
    send_str(s, "  [1] Pack Users (remove deleted accounts)\r\n");
    send_str(s, "  [2] Pack Messages (remove old messages)\r\n");
    send_str(s, "  [3] Pack Files (remove orphaned records)\r\n");
    send_str(s, "  [4] Rebuild Indexes\r\n");
    send_str(s, "  [5] System Statistics\r\n");
    send_str(s, "  [6] Database Vacuum\r\n");
    send_str(s, "  [Q] Quit\r\n");
    send_str(s, "----------------------------------------------------------------------\r\n");
    send_str(s, "\r\nChoice: ");
    
    uint8_t line[8];
    int r = session_readline(s, line, sizeof(line), 60);
    if (r <= 0) break;
    
    switch (line[0]) {
      case '1': cmd_user_pack(s, NULL); break;
      case '2': cmd_message_pack(s, NULL); break;
      case '3': cmd_file_pack(s, NULL); break;
      case '4': cmd_rebuild_indexes(s, NULL); break;
      case '5': cmd_system_stats(s, NULL); break;
      case '6':
#ifdef HAVE_SQLITE
        send_str(s, "\r\nVacuuming database...\r\n");
        db_exec_simple(s->db, "VACUUM");
        send_str(s, "Done.\r\n");
#else
        send_str(s, "\r\nDatabase not available.\r\n");
#endif
        break;
      case 'Q': case 'q':
        return;
      default:
        send_str(s, "\r\nInvalid choice.\r\n");
    }
  }
}
