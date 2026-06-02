#include "bbs_session.h"
#include "bbs_db.h"
#include "bbs_acs.h"
#include "bbs_flags.h"
#include "bbs_util.h"
#include "bbs_doors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

/* FA - Change file area */
void cmd_file_area_change(Session* s, const char* data) {
  if (!s) return;
  DbFileArea areas[32];
  int acount = db_file_area_list(s->db, areas, 32);
  if (acount == 0) { send_str(s, "\r\nNo file areas.\r\n"); return; }
  
  /* If data provided, try to select that area directly */
  if (data && data[0]) {
    int area_id = atoi(data);
    for (int i = 0; i < acount; i++) {
      if (areas[i].id == area_id && acs_allows(s, areas[i].acs_list)) {
        s->current_file_area = area_id;
        char buf[128];
        snprintf(buf, sizeof(buf), "\r\nFile area changed to: %s\r\n", areas[i].name);
        send_str(s, buf);
        return;
      }
    }
    send_str(s, "\r\nInvalid area or access denied.\r\n");
    return;
  }
  
  send_str(s, "\r\nFile Areas:\r\n");
  char buf[256];
  for (int i = 0; i < acount; i++) {
    if (!acs_allows(s, areas[i].acs_list)) continue;
    int fcount = db_count_files_area(s->db, areas[i].id);
    snprintf(buf, sizeof(buf), "  [%2d] %-30s (%d files)\r\n", areas[i].id, areas[i].name, fcount);
    send_str(s, buf);
  }
  
  char line[16];
  prompt_line(s, "\r\nSelect area: ", line, sizeof(line));
  if (!line[0]) return;
  
  int area_id = atoi(line);
  for (int i = 0; i < acount; i++) {
    if (areas[i].id == area_id) {
      if (!acs_allows(s, areas[i].acs_list)) {
        send_str(s, "\r\nAccess denied.\r\n");
        return;
      }
      s->current_file_area = area_id;
      snprintf(buf, sizeof(buf), "\r\nFile area changed to: %s\r\n", areas[i].name);
      send_str(s, buf);
      return;
    }
  }
  send_str(s, "\r\nArea not found.\r\n");
}

/* FG - File area listing (groups) */
void cmd_file_area_list(Session* s, const char* data) {
  (void)data;
  DbFileArea areas[32];
  int acount = db_file_area_list(s->db, areas, 32);
  if (acount == 0) { send_str(s, "\r\nNo file areas.\r\n"); return; }
  
  send_str(s, "\r\n\x1b[1;36mFile Areas:\x1b[0m\r\n");
  send_str(s, "\x1b[1;33m-------------------------------------------\x1b[0m\r\n");
  
  char buf[256];
  int total_files = 0;
  for (int i = 0; i < acount; i++) {
    if (!acs_allows(s, areas[i].acs_list)) continue;
    int fcount = db_count_files_area(s->db, areas[i].id);
    total_files += fcount;
    const char* marker = (areas[i].id == s->current_file_area) ? "*" : " ";
    snprintf(buf, sizeof(buf), " %s[%2d] %-28s %5d files\r\n", 
             marker, areas[i].id, areas[i].name, fcount);
    send_str(s, buf);
  }
  
  send_str(s, "\x1b[1;33m-------------------------------------------\x1b[0m\r\n");
  snprintf(buf, sizeof(buf), "Total: %d files in %d areas\r\n", total_files, acount);
  send_str(s, buf);
}

/* FL - List files in current area */
void cmd_file_list(Session* s, const char* data) {
  (void)data;
  if (s->current_file_area <= 0) {
    send_str(s, "\r\nNo file area selected. Use FA to select one.\r\n");
    return;
  }
  
  DbFileArea area;
  if (!db_file_area_get(s->db, s->current_file_area, &area)) {
    send_str(s, "\r\nFile area not found.\r\n");
    return;
  }
  
  DbFileRec files[50];
  int fcount = db_file_list(&area, s->db, files, 50);
  
  char buf[512];
  snprintf(buf, sizeof(buf), "\r\n\x1b[1;36mFiles in %s:\x1b[0m\r\n", area.name);
  send_str(s, buf);
  send_str(s, "\x1b[1;33m  #   Filename             Size       Date       DLs   Pts\x1b[0m\r\n");
  send_str(s, "--------------------------------------------------------------\r\n");
  
  for (int i = 0; i < fcount; i++) {
    const char* flag_str = "";
    if (files[i].flags & FILE_FLAG_NOTVAL) flag_str = "[!]";
    else if (files[i].flags & FILE_FLAG_FREE) flag_str = "[F]";
    
    snprintf(buf, sizeof(buf), "%3d %s %-18s %8d   %.10s  %4d  %3d\r\n",
             files[i].id, flag_str, files[i].filename, files[i].size_bytes,
             files[i].uploaded_at, files[i].download_count, files[i].file_points);
    send_str(s, buf);
    if (files[i].desc[0]) {
      snprintf(buf, sizeof(buf), "      \x1b[0;37m%s\x1b[0m\r\n", files[i].desc);
      send_str(s, buf);
    }
  }
  
  snprintf(buf, sizeof(buf), "\r\n%d file(s) listed.\r\n", fcount);
  send_str(s, buf);
}

/* FE - View extended description */
void cmd_file_extended(Session* s, const char* data) {
  int file_id = 0;
  
  if (data && data[0]) {
    file_id = atoi(data);
  } else {
    char line[16];
    prompt_line(s, "File # to view: ", line, sizeof(line));
    if (!line[0]) return;
    file_id = atoi(line);
  }
  
  DbFileRec rec;
  if (!db_file_get(s->db, file_id, &rec)) {
    send_str(s, "\r\nFile not found.\r\n");
    return;
  }
  
  char buf[256];
  snprintf(buf, sizeof(buf), "\r\n\x1b[1;36mExtended Description for: %s\x1b[0m\r\n", rec.filename);
  send_str(s, buf);
  send_str(s, "--------------------------------------------------------------\r\n");
  
  if (rec.extended_desc[0]) {
    send_str(s, rec.extended_desc);
    send_str(s, "\r\n");
  } else if (rec.file_id_diz[0]) {
    send_str(s, "\x1b[1;33mFILE_ID.DIZ:\x1b[0m\r\n");
    send_str(s, rec.file_id_diz);
    send_str(s, "\r\n");
  } else {
    send_str(s, "No extended description available.\r\n");
  }
  
  send_str(s, "--------------------------------------------------------------\r\n");
  snprintf(buf, sizeof(buf), "Size: %d bytes  Downloads: %d  Points: %d\r\n",
           rec.size_bytes, rec.download_count, rec.file_points);
  send_str(s, buf);
  if (rec.uploader[0]) {
    snprintf(buf, sizeof(buf), "Uploaded by: %s on %s\r\n", rec.uploader, rec.uploaded_at);
    send_str(s, buf);
  }
}

/* FD - Download file */
void cmd_file_download(Session* s, const char* data) {
  int file_id = 0;
  
  if (data && data[0]) {
    file_id = atoi(data);
  } else {
    char line[16];
    prompt_line(s, "File # to download: ", line, sizeof(line));
    if (!line[0]) return;
    file_id = atoi(line);
  }
  
  DbFileRec rec;
  if (!db_file_get(s->db, file_id, &rec)) {
    send_str(s, "\r\nFile not found.\r\n");
    return;
  }
  
  /* Check credits/ratio unless exempt */
  int cost = (rec.size_bytes / 1024) + 1;
  if (!HAS_AC_FLAG(&s->user, AC_FNOCREDITS)) {
    if (s->credits < cost) {
      char buf[128];
      snprintf(buf, sizeof(buf), "\r\nInsufficient credits. Need %d, have %d.\r\n", cost, s->credits);
      send_str(s, buf);
      return;
    }
  }
  
  /* Check download ratio unless exempt */
  if (!HAS_AC_FLAG(&s->user, AC_FNODLRATIO)) {
    if (s->user.dl_ratio_den > 0) {
      int required_ul = s->user.downloads / s->user.dl_ratio_den;
      if (s->user.uploads < required_ul) {
        send_str(s, "\r\nYou need to upload more files to maintain your ratio.\r\n");
        return;
      }
    }
  }
  
  char buf[256];
  snprintf(buf, sizeof(buf), "\r\nPreparing download: %s (%d bytes)\r\n", rec.filename, rec.size_bytes);
  send_str(s, buf);
  send_str(s, "Select protocol: (Z)modem, (Y)modem, (X)modem: ");
  
  uint8_t line[8];
  int n = session_readline(s, line, sizeof(line), 30);
  if (n <= 0) return;
  
  char proto = (char)toupper(line[0]);
  if (proto != 'Z' && proto != 'Y' && proto != 'X') {
    send_str(s, "\r\nInvalid protocol.\r\n");
    return;
  }
  
  snprintf(buf, sizeof(buf), "\r\nStarting %cmodem transfer...\r\n", proto);
  send_str(s, buf);
  
  /* Get file area for path resolution */
  DbFileArea area;
  if (!db_file_area_get(s->db, rec.area_id, &area)) {
    send_str(s, "\r\nFile area not found.\r\n");
    return;
  }
  
  /* Get file path */
  char filepath[512];
  if (!file_area_resolve(area.path, rec.filename, filepath, sizeof(filepath))) {
    send_str(s, "\r\nPath error.\r\n");
    return;
  }
  
  /* Look up protocol from database */
  DbProtocol protos[4];
  int pcnt = db_protocols_list(s->db, protos, 4, "down");
  
  DbProtocol* selected = NULL;
  for (int i = 0; i < pcnt; i++) {
    if ((proto == 'Z' && strcasecmp(protos[i].name, "Zmodem") == 0) ||
        (proto == 'Y' && strcasecmp(protos[i].name, "Ymodem") == 0) ||
        (proto == 'X' && strcasecmp(protos[i].name, "Xmodem") == 0)) {
      selected = &protos[i];
      break;
    }
  }
  
  if (selected) {
    if (protocol_launch(s, selected, filepath, "down")) {
      if (!HAS_AC_FLAG(&s->user, AC_FNOCREDITS)) {
        s->credits -= cost;
        s->user.credits -= cost;
      }
      s->user.downloads++;
      s->user.dk += (rec.size_bytes / 1024);
      s->user.dl_today++;
      s->user.dl_k_today += (rec.size_bytes / 1024);
      db_file_inc_downloads(s->db, file_id);
      send_str(s, "\r\nDownload complete.\r\n");
    } else {
      send_str(s, "\r\nTransfer failed or cancelled.\r\n");
    }
  } else {
    send_str(s, "\r\nProtocol not configured. Contact sysop.\r\n");
  }
}

/* FU - Upload file */
void cmd_file_upload(Session* s, const char* data) {
  (void)data;
  
  if (s->current_file_area <= 0) {
    send_str(s, "\r\nNo file area selected. Use FA to select one.\r\n");
    return;
  }
  
  DbFileArea area;
  if (!db_file_area_get(s->db, s->current_file_area, &area)) {
    send_str(s, "\r\nFile area not found.\r\n");
    return;
  }
  
  /* Check upload ACS */
  if (!acs_allows(s, area.acs_upload)) {
    send_str(s, "\r\nYou don't have permission to upload to this area.\r\n");
    return;
  }
  
  char filename[64], desc[256], extended_desc[2048];
  prompt_line(s, "Filename: ", filename, sizeof(filename));
  if (!filename[0]) return;
  
  /* Check for duplicate filename if area requires it */
  if (area.check_dupes) {
    int existing_id = 0;
    if (db_file_exists_name(s->db, s->current_file_area, filename, &existing_id)) {
      char buf[128];
      snprintf(buf, sizeof(buf), "\r\nDuplicate file! '%s' already exists (file #%d).\r\n", filename, existing_id);
      send_str(s, buf);
      return;
    }
  }
  
  prompt_line(s, "Description: ", desc, sizeof(desc));
  
  /* Ask for extended description */
  send_str(s, "\r\nEnter extended description (blank line to end):\r\n");
  extended_desc[0] = '\0';
  size_t ext_len = 0;
  for (int line_num = 0; line_num < 20; line_num++) {
    char line_buf[128];
    prompt_line(s, "> ", line_buf, sizeof(line_buf));
    if (!line_buf[0]) break;
    size_t line_len = strlen(line_buf);
    if (ext_len + line_len + 3 < sizeof(extended_desc)) {
      strcat(extended_desc, line_buf);
      strcat(extended_desc, "\r\n");
      ext_len += line_len + 2;
    }
  }
  
  send_str(s, "\r\nSelect protocol: (Z)modem, (Y)modem, (X)modem: ");
  uint8_t line[8];
  int n = session_readline(s, line, sizeof(line), 30);
  if (n <= 0) return;
  
  char proto = (char)toupper(line[0]);
  if (proto != 'Z' && proto != 'Y' && proto != 'X') {
    send_str(s, "\r\nInvalid protocol.\r\n");
    return;
  }
  
  char buf[256];
  snprintf(buf, sizeof(buf), "\r\nReady to receive %s via %cmodem...\r\n", filename, proto);
  send_str(s, buf);
  
  /* Build destination path */
  char filepath[512];
  if (!file_area_resolve(area.path, filename, filepath, sizeof(filepath))) {
    send_str(s, "\r\nPath error.\r\n");
    return;
  }
  
  /* Look up protocol from database */
  DbProtocol protos[4];
  int pcnt = db_protocols_list(s->db, protos, 4, "up");
  
  DbProtocol* selected = NULL;
  for (int i = 0; i < pcnt; i++) {
    if ((proto == 'Z' && strcasecmp(protos[i].name, "Zmodem") == 0) ||
        (proto == 'Y' && strcasecmp(protos[i].name, "Ymodem") == 0) ||
        (proto == 'X' && strcasecmp(protos[i].name, "Xmodem") == 0)) {
      selected = &protos[i];
      break;
    }
  }
  
  bool transfer_ok = false;
  if (selected) {
    /* For uploads, rz/rb/rx write to current directory, so we need to cd first */
    char upload_dir[512];
    snprintf(upload_dir, sizeof(upload_dir), "%s", area.path);
    
    /* Create a temp protocol with cd prefix */
    DbProtocol temp_proto = *selected;
    char temp_cmd[512];
    snprintf(temp_cmd, sizeof(temp_cmd), "cd '%s' && %s", upload_dir, selected->command);
    strncpy(temp_proto.command, temp_cmd, sizeof(temp_proto.command) - 1);
    
    transfer_ok = protocol_launch(s, &temp_proto, filename, "up");
  } else {
    send_str(s, "\r\nProtocol not configured. Contact sysop.\r\n");
    return;
  }
  
  if (!transfer_ok) {
    send_str(s, "\r\nTransfer failed or cancelled.\r\n");
    return;
  }
  
  /* Check if file was actually received */
  struct stat st;
  if (stat(filepath, &st) != 0) {
    send_str(s, "\r\nFile not received.\r\n");
    return;
  }
  
  int size = (int)st.st_size;
  int flags = FILE_FLAG_NOTVAL;  /* New uploads need validation */
  int file_points = 0;
  
  if (db_file_add_ex(s->db, s->current_file_area, filename, desc, extended_desc, NULL,
                     size, s->user.id, file_points, flags)) {
    s->user.uploads++;
    s->user.uk += (size / 1024);
    s->file_points += 10;
    s->user.file_points += 10;
    
    snprintf(buf, sizeof(buf), "\r\nUpload complete! %s (%d bytes)\r\n", filename, size);
    send_str(s, buf);
    send_str(s, "File queued for validation. You earned 10 file points.\r\n");
  } else {
    send_str(s, "\r\nUpload failed.\r\n");
  }
}

/* FB - Batch download */
void cmd_file_batch_download(Session* s, const char* data) {
  (void)data;
  
  if (s->batch_count == 0) {
    send_str(s, "\r\nBatch queue is empty.\r\n");
    return;
  }
  
  char buf[256];
  snprintf(buf, sizeof(buf), "\r\nBatch queue contains %d file(s):\r\n", s->batch_count);
  send_str(s, buf);
  
  int total_size = 0;
  for (int i = 0; i < s->batch_count; i++) {
    DbFileRec rec;
    if (db_file_get(s->db, s->batch_queue[i], &rec)) {
      snprintf(buf, sizeof(buf), "  %d. %s (%d bytes)\r\n", i + 1, rec.filename, rec.size_bytes);
      send_str(s, buf);
      total_size += rec.size_bytes;
    }
  }
  
  snprintf(buf, sizeof(buf), "\r\nTotal: %d bytes\r\n", total_size);
  send_str(s, buf);
  
  send_str(s, "\r\nStart batch download? (Y/N): ");
  uint8_t line[8];
  int n = session_readline(s, line, sizeof(line), 30);
  if (n <= 0 || (line[0] != 'Y' && line[0] != 'y')) {
    send_str(s, "\r\nBatch download cancelled.\r\n");
    return;
  }
  
  send_str(s, "\r\nSelect protocol: (Z)modem, (Y)modem: ");
  n = session_readline(s, line, sizeof(line), 30);
  if (n <= 0) return;
  
  char proto = (char)toupper(line[0]);
  if (proto != 'Z' && proto != 'Y') {
    send_str(s, "\r\nInvalid protocol. Batch requires Zmodem or Ymodem.\r\n");
    return;
  }
  
  /* Look up protocol from database */
  DbProtocol protos[4];
  int pcnt = db_protocols_list(s->db, protos, 4, "down");
  
  DbProtocol* selected = NULL;
  for (int i = 0; i < pcnt; i++) {
    if ((proto == 'Z' && strcasecmp(protos[i].name, "Zmodem") == 0) ||
        (proto == 'Y' && strcasecmp(protos[i].name, "Ymodem") == 0)) {
      selected = &protos[i];
      break;
    }
  }
  
  if (!selected) {
    send_str(s, "\r\nProtocol not configured. Contact sysop.\r\n");
    return;
  }
  
  send_str(s, "\r\nStarting batch download...\r\n");
  
  /* Get all file areas for path resolution */
  DbFileArea areas[32];
  int acount = db_file_area_list(s->db, areas, 32);
  
  int success_count = 0;
  for (int i = 0; i < s->batch_count; i++) {
    DbFileRec rec;
    if (!db_file_get(s->db, s->batch_queue[i], &rec)) continue;
    
    /* Find the area for this file */
    DbFileArea* area = NULL;
    for (int a = 0; a < acount; a++) {
      if (areas[a].id == rec.area_id) {
        area = &areas[a];
        break;
      }
    }
    if (!area) continue;
    
    char filepath[512];
    if (!file_area_resolve(area->path, rec.filename, filepath, sizeof(filepath))) continue;
    
    snprintf(buf, sizeof(buf), "Sending: %s\r\n", rec.filename);
    send_str(s, buf);
    
    if (protocol_launch(s, selected, filepath, "down")) {
      success_count++;
      db_file_inc_downloads(s->db, rec.id);
      s->user.downloads++;
      s->user.dk += (rec.size_bytes / 1024);
      s->user.dl_today++;
      s->user.dl_k_today += (rec.size_bytes / 1024);
    }
  }
  
  /* Clear batch queue */
  s->batch_count = 0;
  
  snprintf(buf, sizeof(buf), "\r\nBatch download complete. %d file(s) transferred.\r\n", success_count);
  send_str(s, buf);
}

/* FC - Clear batch queue */
void cmd_file_batch_clear(Session* s, const char* data) {
  (void)data;
  s->batch_count = 0;
  send_str(s, "\r\nBatch queue cleared.\r\n");
}

/* FF - Find file (search) */
void cmd_file_find(Session* s, const char* data) {
  char pattern[64];
  
  if (data && data[0]) {
    snprintf(pattern, sizeof(pattern), "%s", data);
  } else {
    prompt_line(s, "Search pattern: ", pattern, sizeof(pattern));
    if (!pattern[0]) return;
  }
  
  send_str(s, "\r\nSearching all file areas...\r\n");
  
  DbFileArea areas[32];
  int acount = db_file_area_list(s->db, areas, 32);
  int found = 0;
  char buf[512];
  
  for (int a = 0; a < acount; a++) {
    if (!acs_allows(s, areas[a].acs_list)) continue;
    
    DbFileRec files[100];
    int fcount = db_file_list(&areas[a], s->db, files, 100);
    
    for (int f = 0; f < fcount; f++) {
      /* Simple case-insensitive substring search */
      char fn_lower[64], pat_lower[64];
      snprintf(fn_lower, sizeof(fn_lower), "%s", files[f].filename);
      snprintf(pat_lower, sizeof(pat_lower), "%s", pattern);
      for (char* p = fn_lower; *p; p++) *p = (char)tolower(*p);
      for (char* p = pat_lower; *p; p++) *p = (char)tolower(*p);
      
      if (strstr(fn_lower, pat_lower) || strstr(files[f].desc, pattern) ||
          strstr(files[f].extended_desc, pattern)) {
        snprintf(buf, sizeof(buf), "\r\n[%s] #%d %s (%d bytes)\r\n  %s\r\n",
                 areas[a].name, files[f].id, files[f].filename, 
                 files[f].size_bytes, files[f].desc);
        send_str(s, buf);
        found++;
      }
    }
  }
  
  snprintf(buf, sizeof(buf), "\r\n%d file(s) found matching '%s'.\r\n", found, pattern);
  send_str(s, buf);
}

/* FN - New files scan */
void cmd_file_new_scan(Session* s, const char* data) {
  (void)data;

  /* FP override takes precedence over last-login date */
  const char* since = (s->file_scan_date[0]) ? s->file_scan_date : s->user.last_login_at;
  if (!since || !since[0]) {
    since = "1970-01-01";
  }
  
  char buf[256];
  snprintf(buf, sizeof(buf), "\r\nNew files since %s:\r\n", since);
  send_str(s, buf);
  
  DbFileArea areas[32];
  int acount = db_file_area_list(s->db, areas, 32);
  int found = 0;
  
  for (int a = 0; a < acount; a++) {
    if (!acs_allows(s, areas[a].acs_list)) continue;
    
    DbFileRec files[100];
    int fcount = db_file_list(&areas[a], s->db, files, 100);
    
    for (int f = 0; f < fcount; f++) {
      if (strcmp(files[f].uploaded_at, since) > 0) {
        snprintf(buf, sizeof(buf), "[%s] %s (%d bytes) - %s\r\n",
                 areas[a].name, files[f].filename, files[f].size_bytes, files[f].desc);
        send_str(s, buf);
        found++;
      }
    }
  }
  
  snprintf(buf, sizeof(buf), "\r\n%d new file(s) found.\r\n", found);
  send_str(s, buf);
}

/* Helper to run archive listing command and send output to session */
static void run_archive_cmd(Session* s, const char* cmd) {
  FILE* fp = popen(cmd, "r");
  if (!fp) {
    send_str(s, "Failed to run archive tool.\r\n");
    return;
  }
  
  char line[256];
  int lines = 0;
  while (fgets(line, sizeof(line), fp) != NULL && lines < 100) {
    /* Convert LF to CRLF for telnet */
    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n') {
      line[len-1] = '\0';
      send_str(s, line);
      send_str(s, "\r\n");
    } else {
      send_str(s, line);
    }
    lines++;
  }
  
  if (lines >= 100) {
    send_str(s, "... (output truncated)\r\n");
  }
  
  pclose(fp);
}

/* FV - View archive contents */
void cmd_file_view_archive(Session* s, const char* data) {
  int file_id = 0;
  
  if (data && data[0]) {
    file_id = atoi(data);
  } else {
    char line[16];
    prompt_line(s, "File # to view: ", line, sizeof(line));
    if (!line[0]) return;
    file_id = atoi(line);
  }
  
  DbFileRec rec;
  if (!db_file_get(s->db, file_id, &rec)) {
    send_str(s, "\r\nFile not found.\r\n");
    return;
  }
  
  /* Get file area to resolve full path */
  DbFileArea area;
  if (!db_file_area_get(s->db, rec.area_id, &area)) {
    send_str(s, "\r\nFile area not found.\r\n");
    return;
  }
  
  char filepath[512];
  snprintf(filepath, sizeof(filepath), "%s/%s", area.path, rec.filename);
  
  /* Check file exists */
  if (access(filepath, R_OK) != 0) {
    send_str(s, "\r\nFile not accessible on disk.\r\n");
    return;
  }
  
  char buf[256];
  snprintf(buf, sizeof(buf), "\r\n\x1b[1;36mArchive: %s\x1b[0m\r\n", rec.filename);
  send_str(s, buf);
  send_str(s, "--------------------------------------------------------------\r\n");
  
  /* Show FILE_ID.DIZ if available */
  if (rec.file_id_diz[0]) {
    send_str(s, "\x1b[1;33mFILE_ID.DIZ:\x1b[0m\r\n");
    send_str(s, rec.file_id_diz);
    send_str(s, "\r\n--------------------------------------------------------------\r\n");
  }
  
  /* Check file extension */
  const char* ext = strrchr(rec.filename, '.');
  if (!ext) {
    send_str(s, "Cannot determine archive type for listing.\r\n");
    return;
  }
  
  send_str(s, "\x1b[1;33mArchive Contents:\x1b[0m\r\n");
  
  char cmd[768];
  if (strcasecmp(ext, ".zip") == 0) {
    snprintf(cmd, sizeof(cmd), "unzip -l '%s' 2>&1", filepath);
    run_archive_cmd(s, cmd);
  } else if (strcasecmp(ext, ".tar") == 0) {
    snprintf(cmd, sizeof(cmd), "tar -tvf '%s' 2>&1", filepath);
    run_archive_cmd(s, cmd);
  } else if (strcasecmp(ext, ".tgz") == 0 || strcasecmp(ext, ".tar.gz") == 0) {
    snprintf(cmd, sizeof(cmd), "tar -tzvf '%s' 2>&1", filepath);
    run_archive_cmd(s, cmd);
  } else if (strcasecmp(ext, ".gz") == 0 && strstr(rec.filename, ".tar") == NULL) {
    snprintf(cmd, sizeof(cmd), "gzip -l '%s' 2>&1", filepath);
    run_archive_cmd(s, cmd);
  } else if (strcasecmp(ext, ".rar") == 0) {
    snprintf(cmd, sizeof(cmd), "unrar l '%s' 2>&1", filepath);
    run_archive_cmd(s, cmd);
  } else if (strcasecmp(ext, ".7z") == 0) {
    snprintf(cmd, sizeof(cmd), "7z l '%s' 2>&1", filepath);
    run_archive_cmd(s, cmd);
  } else if (strcasecmp(ext, ".arj") == 0) {
    snprintf(cmd, sizeof(cmd), "arj l '%s' 2>&1", filepath);
    run_archive_cmd(s, cmd);
  } else if (strcasecmp(ext, ".lzh") == 0 || strcasecmp(ext, ".lha") == 0) {
    snprintf(cmd, sizeof(cmd), "lha l '%s' 2>&1", filepath);
    run_archive_cmd(s, cmd);
  } else if (strcasecmp(ext, ".bz2") == 0) {
    snprintf(cmd, sizeof(cmd), "bzip2 -l '%s' 2>&1", filepath);
    run_archive_cmd(s, cmd);
  } else if (strcasecmp(ext, ".xz") == 0) {
    snprintf(cmd, sizeof(cmd), "xz -l '%s' 2>&1", filepath);
    run_archive_cmd(s, cmd);
  } else {
    send_str(s, "Unknown or unsupported archive format.\r\n");
  }
}

/* FZ - Zippy file search (fast search by filename) */
void cmd_file_zippy_search(Session* s, const char* data) {
  char pattern[64];
  
  if (data && data[0]) {
    snprintf(pattern, sizeof(pattern), "%s", data);
  } else {
    prompt_line(s, "Filename to search: ", pattern, sizeof(pattern));
    if (!pattern[0]) return;
  }
  
  /* Same as FF but filename-only search */
  send_str(s, "\r\nZippy search (filename only)...\r\n");
  
  DbFileArea areas[32];
  int acount = db_file_area_list(s->db, areas, 32);
  int found = 0;
  char buf[256];
  
  for (int a = 0; a < acount; a++) {
    if (!acs_allows(s, areas[a].acs_list)) continue;
    
    DbFileRec files[100];
    int fcount = db_file_list(&areas[a], s->db, files, 100);
    
    for (int f = 0; f < fcount; f++) {
      char fn_lower[64], pat_lower[64];
      snprintf(fn_lower, sizeof(fn_lower), "%s", files[f].filename);
      snprintf(pat_lower, sizeof(pat_lower), "%s", pattern);
      for (char* p = fn_lower; *p; p++) *p = (char)tolower(*p);
      for (char* p = pat_lower; *p; p++) *p = (char)tolower(*p);
      
      if (strstr(fn_lower, pat_lower)) {
        snprintf(buf, sizeof(buf), "[%s] #%d %s (%d bytes)\r\n",
                 areas[a].name, files[f].id, files[f].filename, files[f].size_bytes);
        send_str(s, buf);
        found++;
      }
    }
  }
  
  snprintf(buf, sizeof(buf), "\r\n%d file(s) found.\r\n", found);
  send_str(s, buf);
}

/* FR - Raw directory listing */
void cmd_file_raw_dir(Session* s, const char* data) {
  (void)data;
  
  if (s->current_file_area <= 0) {
    send_str(s, "\r\nNo file area selected.\r\n");
    return;
  }
  
  DbFileArea area;
  if (!db_file_area_get(s->db, s->current_file_area, &area)) {
    send_str(s, "\r\nFile area not found.\r\n");
    return;
  }
  
  char buf[256];
  snprintf(buf, sizeof(buf), "\r\nRaw directory listing of: %s\r\n", area.path);
  send_str(s, buf);
  
  DIR* dir = opendir(area.path);
  if (!dir) {
    send_str(s, "Cannot open directory.\r\n");
    return;
  }
  
  struct dirent* ent;
  while ((ent = readdir(dir)) != NULL) {
    if (ent->d_name[0] == '.') continue;
    
    char fullpath[512];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", area.path, ent->d_name);
    
    struct stat st;
    if (stat(fullpath, &st) == 0) {
      snprintf(buf, sizeof(buf), "%-30s %10ld bytes\r\n", ent->d_name, (long)st.st_size);
      send_str(s, buf);
    }
  }
  
  closedir(dir);
}

/* FX - Expert mode toggle */
void cmd_file_expert_toggle(Session* s, const char* data) {
  (void)data;
  
  if (s->user.status_flags & STATUS_EXPERT) {
    s->user.status_flags &= ~STATUS_EXPERT;
    send_str(s, "\r\nExpert mode OFF - full menus will be displayed.\r\n");
  } else {
    s->user.status_flags |= STATUS_EXPERT;
    send_str(s, "\r\nExpert mode ON - abbreviated menus.\r\n");
  }
}

/* FP - Set new-scan cutoff date */
void cmd_file_set_scan_date(Session* s, const char* data) {
  char date[32];
  if (data && data[0]) {
    snprintf(date, sizeof(date), "%s", data);
  } else {
    send_str(s, "\r\nEnter scan date (YYYY-MM-DD, blank=reset to last login): ");
    prompt_line(s, "", date, sizeof(date));
  }

  if (!date[0]) {
    s->file_scan_date[0] = '\0';
    send_str(s, "\r\nScan date reset to last login date.\r\n");
    return;
  }

  int y, m, d;
  if (sscanf(date, "%d-%d-%d", &y, &m, &d) != 3 || y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) {
    send_str(s, "\r\nInvalid date format. Use YYYY-MM-DD.\r\n");
    return;
  }

  snprintf(s->file_scan_date, sizeof(s->file_scan_date), "%04d-%02d-%02d", y, m, d);
  char buf[64];
  snprintf(buf, sizeof(buf), "\r\nNew-scan date set to %s.\r\n", s->file_scan_date);
  send_str(s, buf);
}

/* Resolve file path from area for a given file record */
static bool resolve_file_path_for_cmd(Session* s, int file_id,
                                      char* filepath, size_t fplen,
                                      DbFileRec* rec_out) {
  if (!db_file_get(s->db, file_id, rec_out)) {
    send_str(s, "\r\nFile not found.\r\n");
    return false;
  }
  DbFileArea area;
  if (!db_file_area_get(s->db, rec_out->area_id, &area)) {
    send_str(s, "\r\nFile area not found.\r\n");
    return false;
  }
  if (!file_area_resolve(area.path, rec_out->filename, filepath, fplen)) {
    send_str(s, "\r\nPath error.\r\n");
    return false;
  }
  if (access(filepath, R_OK) != 0) {
    send_str(s, "\r\nFile not accessible on disk.\r\n");
    return false;
  }
  return true;
}

/* FT - Test archive integrity */
void cmd_file_archive_test(Session* s, const char* data) {
  int file_id = 0;
  if (data && data[0]) {
    file_id = atoi(data);
  } else {
    char line[16];
    prompt_line(s, "File # to test: ", line, sizeof(line));
    if (!line[0]) return;
    file_id = atoi(line);
  }

  char filepath[512];
  DbFileRec rec;
  if (!resolve_file_path_for_cmd(s, file_id, filepath, sizeof(filepath), &rec)) return;

  const char* ext = strrchr(rec.filename, '.');
  if (!ext) { send_str(s, "\r\nCannot determine archive type.\r\n"); return; }

  char buf[256];
  snprintf(buf, sizeof(buf), "\r\n\x1b[1;36mTesting archive: %s\x1b[0m\r\n", rec.filename);
  send_str(s, buf);
  send_str(s, "--------------------------------------------------------------\r\n");

  char cmd[768];
  if (strcasecmp(ext, ".zip") == 0)
    snprintf(cmd, sizeof(cmd), "unzip -t '%s' 2>&1", filepath);
  else if (strcasecmp(ext, ".rar") == 0)
    snprintf(cmd, sizeof(cmd), "unrar t '%s' 2>&1", filepath);
  else if (strcasecmp(ext, ".7z") == 0)
    snprintf(cmd, sizeof(cmd), "7z t '%s' 2>&1", filepath);
  else if (strcasecmp(ext, ".arj") == 0)
    snprintf(cmd, sizeof(cmd), "arj t '%s' 2>&1", filepath);
  else if (strcasecmp(ext, ".lzh") == 0 || strcasecmp(ext, ".lha") == 0)
    snprintf(cmd, sizeof(cmd), "lha t '%s' 2>&1", filepath);
  else if (strcasecmp(ext, ".tar") == 0)
    snprintf(cmd, sizeof(cmd), "tar -tf '%s' > /dev/null 2>&1 && echo 'Archive OK'", filepath);
  else if (strcasecmp(ext, ".tgz") == 0)
    snprintf(cmd, sizeof(cmd), "tar -tzf '%s' > /dev/null 2>&1 && echo 'Archive OK'", filepath);
  else {
    send_str(s, "Unsupported archive format for testing.\r\n");
    return;
  }

  run_archive_cmd(s, cmd);
}

/* FQ - Extract archive to temp directory */
void cmd_file_archive_extract(Session* s, const char* data) {
  int file_id = 0;
  if (data && data[0]) {
    file_id = atoi(data);
  } else {
    char line[16];
    prompt_line(s, "File # to extract: ", line, sizeof(line));
    if (!line[0]) return;
    file_id = atoi(line);
  }

  char filepath[512];
  DbFileRec rec;
  if (!resolve_file_path_for_cmd(s, file_id, filepath, sizeof(filepath), &rec)) return;

  const char* ext = strrchr(rec.filename, '.');
  if (!ext) { send_str(s, "\r\nCannot determine archive type.\r\n"); return; }

  /* Create isolated temp directory: /tmp/mutineer_<uid>_<ts> */
  char tmpdir[128];
  snprintf(tmpdir, sizeof(tmpdir), "/tmp/mutineer_%d_%ld", s->user.id, (long)time(NULL));
  if (mkdir(tmpdir, 0700) != 0) {
    send_str(s, "\r\nCould not create temp directory.\r\n");
    return;
  }

  char buf[256];
  snprintf(buf, sizeof(buf), "\r\n\x1b[1;36mExtracting: %s\x1b[0m\r\n", rec.filename);
  send_str(s, buf);
  snprintf(buf, sizeof(buf), "Temp dir: %s\r\n", tmpdir);
  send_str(s, buf);
  send_str(s, "--------------------------------------------------------------\r\n");

  char cmd[768];
  if (strcasecmp(ext, ".zip") == 0)
    snprintf(cmd, sizeof(cmd), "unzip -o '%s' -d '%s' 2>&1", filepath, tmpdir);
  else if (strcasecmp(ext, ".rar") == 0)
    snprintf(cmd, sizeof(cmd), "unrar e '%s' '%s/' 2>&1", filepath, tmpdir);
  else if (strcasecmp(ext, ".7z") == 0)
    snprintf(cmd, sizeof(cmd), "7z e '%s' -o'%s' 2>&1", filepath, tmpdir);
  else if (strcasecmp(ext, ".arj") == 0)
    snprintf(cmd, sizeof(cmd), "arj e '%s' '%s/' 2>&1", filepath, tmpdir);
  else if (strcasecmp(ext, ".lzh") == 0 || strcasecmp(ext, ".lha") == 0)
    snprintf(cmd, sizeof(cmd), "lha e '%s' '%s/' 2>&1", filepath, tmpdir);
  else if (strcasecmp(ext, ".tar") == 0)
    snprintf(cmd, sizeof(cmd), "tar -xf '%s' -C '%s' 2>&1", filepath, tmpdir);
  else if (strcasecmp(ext, ".tgz") == 0)
    snprintf(cmd, sizeof(cmd), "tar -xzf '%s' -C '%s' 2>&1", filepath, tmpdir);
  else {
    send_str(s, "Unsupported archive format for extraction.\r\n");
    rmdir(tmpdir);
    return;
  }

  run_archive_cmd(s, cmd);

  /* List what was extracted */
  send_str(s, "--------------------------------------------------------------\r\n");
  send_str(s, "Extracted files:\r\n");
  char lscmd[256];
  snprintf(lscmd, sizeof(lscmd), "ls -lh '%s' 2>&1", tmpdir);
  run_archive_cmd(s, lscmd);

  send_str(s, "\r\nExtraction complete. Temp files will be cleaned up on session end.\r\n");

  /* Best-effort cleanup note: we remove now; caller can re-extract if needed */
  char rmcmd[256];
  snprintf(rmcmd, sizeof(rmcmd), "rm -rf '%s'", tmpdir);
  system(rmcmd);
}

/* FK - Remove specific entry from batch queue */
void cmd_file_batch_remove(Session* s, const char* data) {
  if (s->batch_count == 0) {
    send_str(s, "\r\nBatch queue is empty.\r\n");
    return;
  }

  /* Show current queue */
  send_str(s, "\r\nBatch Queue:\r\n");
  char buf[256];
  for (int i = 0; i < s->batch_count; i++) {
    DbFileRec rec;
    if (db_file_get(s->db, s->batch_queue[i], &rec))
      snprintf(buf, sizeof(buf), "  %d. %s (%d bytes)\r\n", i + 1, rec.filename, rec.size_bytes);
    else
      snprintf(buf, sizeof(buf), "  %d. [file %d]\r\n", i + 1, s->batch_queue[i]);
    send_str(s, buf);
  }

  int idx = 0;
  if (data && data[0]) {
    idx = atoi(data);
  } else {
    char line[16];
    prompt_line(s, "Remove entry #: ", line, sizeof(line));
    if (!line[0]) return;
    idx = atoi(line);
  }

  if (idx < 1 || idx > s->batch_count) {
    send_str(s, "\r\nInvalid entry number.\r\n");
    return;
  }

  /* Shift remaining entries down */
  for (int i = idx - 1; i < s->batch_count - 1; i++) {
    s->batch_queue[i] = s->batch_queue[i + 1];
    s->batch_area[i]  = s->batch_area[i + 1];
  }
  s->batch_count--;

  snprintf(buf, sizeof(buf), "\r\nEntry %d removed. Queue now has %d file(s).\r\n",
           idx, s->batch_count);
  send_str(s, buf);
}

/* FJ - Batch upload (queue multiple files for upload then process) */
void cmd_file_batch_upload(Session* s, const char* data) {
  (void)data;

  if (s->current_file_area <= 0) {
    send_str(s, "\r\nSelect a file area first (FA).\r\n");
    return;
  }

  DbFileArea area;
  if (!db_file_area_get(s->db, s->current_file_area, &area)) {
    send_str(s, "\r\nCurrent file area not found.\r\n");
    return;
  }

  if (!acs_allows(s, area.acs_upload)) {
    send_str(s, "\r\nYou do not have upload access to this area.\r\n");
    return;
  }

  DbProtocol protos[4];
  int pcnt = db_protocols_list(s->db, protos, 4, "up");
  if (pcnt == 0) {
    send_str(s, "\r\nNo upload protocols configured. Contact sysop.\r\n");
    return;
  }

  send_str(s, "\r\n\x1b[1;36mBatch Upload\x1b[0m\r\n");
  send_str(s, "--------------------------------------------------------------\r\n");

  char buf[256];
  snprintf(buf, sizeof(buf), "Upload area: %s\r\n", area.name);
  send_str(s, buf);

  send_str(s, "Select protocol:\r\n");
  for (int i = 0; i < pcnt; i++) {
    snprintf(buf, sizeof(buf), "  %d. %s\r\n", i + 1, protos[i].name);
    send_str(s, buf);
  }
  prompt_line(s, "Protocol #: ", buf, sizeof(buf));
  int pidx = atoi(buf) - 1;
  if (pidx < 0 || pidx >= pcnt) {
    send_str(s, "\r\nCancelled.\r\n");
    return;
  }
  DbProtocol* proto = &protos[pidx];

  /* Build a temp receive directory for this session */
  char recvdir[256];
  snprintf(recvdir, sizeof(recvdir), "%s/upload_%d_%ld",
           area.path, s->user.id, (long)time(NULL));
  if (mkdir(recvdir, 0755) != 0) {
    send_str(s, "\r\nCould not create upload staging directory.\r\n");
    return;
  }

  snprintf(buf, sizeof(buf), "\r\nReady for batch upload via %s.\r\n"
           "Send your files now. Press Enter when done.\r\n", proto->name);
  send_str(s, buf);

  /* Launch protocol receive into staging dir (use "." as the path so the
     protocol tool writes into the staging dir via workdir) */
  char dummy_path[512];
  snprintf(dummy_path, sizeof(dummy_path), "%s/", recvdir);
  protocol_launch(s, proto, dummy_path, "up");

  /* Scan staging dir for received files */
  DIR* dir = opendir(recvdir);
  int uploaded = 0;
  if (dir) {
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
      if (ent->d_name[0] == '.') continue;

      char srcpath[512], destpath[512];
      snprintf(srcpath, sizeof(srcpath), "%s/%s", recvdir, ent->d_name);
      snprintf(destpath, sizeof(destpath), "%s/%s", area.path, ent->d_name);

      struct stat st;
      if (stat(srcpath, &st) != 0) continue;

      /* Move file to area directory */
      if (rename(srcpath, destpath) != 0) {
        /* Cross-device: fall back to copy */
        FILE* fin  = fopen(srcpath, "rb");
        FILE* fout = fopen(destpath, "wb");
        if (fin && fout) {
          char chunk[4096];
          size_t n;
          while ((n = fread(chunk, 1, sizeof(chunk), fin)) > 0)
            fwrite(chunk, 1, n, fout);
        }
        if (fin)  fclose(fin);
        if (fout) fclose(fout);
        unlink(srcpath);
      }

      char desc[256] = {0};
      char prompt_buf[64];
      snprintf(prompt_buf, sizeof(prompt_buf), "Description for %s: ", ent->d_name);
      prompt_line(s, prompt_buf, desc, sizeof(desc));

      int size = (int)st.st_size;
      if (db_file_add_ex(s->db, s->current_file_area, ent->d_name, desc, "", NULL,
                         size, s->user.id, 0, FILE_FLAG_NOTVAL)) {
        s->user.uploads++;
        s->user.uk += (size / 1024);
        s->file_points += 10;
        s->user.file_points += 10;
        uploaded++;

        snprintf(buf, sizeof(buf), "  Queued for validation: %s (%d bytes)\r\n",
                 ent->d_name, size);
        send_str(s, buf);
      }
    }
    closedir(dir);
  }

  rmdir(recvdir);

  snprintf(buf, sizeof(buf), "\r\nBatch upload complete. %d file(s) accepted.\r\n", uploaded);
  send_str(s, buf);
  if (uploaded > 0)
    send_str(s, "Files queued for sysop validation. You earned 10 points per file.\r\n");
}

/* Main file command dispatcher */
void handle_file_command(Session* s, const char* cmd, const char* data) {
  if (!cmd || strlen(cmd) < 2) return;

  char c1 = (char)toupper(cmd[0]);
  char c2 = (char)toupper(cmd[1]);

  if (c1 != 'F') return;

  switch (c2) {
    case 'A': cmd_file_area_change(s, data); break;
    case 'B': cmd_file_batch_download(s, data); break;
    case 'C': cmd_file_batch_clear(s, data); break;
    case 'D': cmd_file_download(s, data); break;
    case 'E': cmd_file_extended(s, data); break;
    case 'F': cmd_file_find(s, data); break;
    case 'G': cmd_file_area_list(s, data); break;
    case 'J': cmd_file_batch_upload(s, data); break;
    case 'K': cmd_file_batch_remove(s, data); break;
    case 'L': cmd_file_list(s, data); break;
    case 'N': cmd_file_new_scan(s, data); break;
    case 'P': cmd_file_set_scan_date(s, data); break;
    case 'Q': cmd_file_archive_extract(s, data); break;
    case 'R': cmd_file_raw_dir(s, data); break;
    case 'T': cmd_file_archive_test(s, data); break;
    case 'U': cmd_file_upload(s, data); break;
    case 'V': cmd_file_view_archive(s, data); break;
    case 'X': cmd_file_expert_toggle(s, data); break;
    case 'Z': cmd_file_zippy_search(s, data); break;
    default:
      send_str(s, "\r\nUnknown file command.\r\n");
      break;
  }
}
