#define _XOPEN_SOURCE 700
#include "bbs_session.h"
#include "bbs_db.h"
#include "bbs_acs.h"
#include "bbs_flags.h"
#include "bbs_util.h"
#include "bbs_archive.h"
#include "bbs_doors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

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

static bool is_sysop(Session *s) {
  return s && acs_allows(s, "+A");
}

static bool file_area_password_ok(Session *s, const DbFileArea *area) {
  if (!s || !area || !area->password[0]) return true;
  if (area_password_cached(s, 'F', area->id)) return true;
  char entered[64] = {0};
  char prompt[128];
  snprintf(prompt, sizeof(prompt), "Password for %s: ", area->name);
  if (prompt_line(s, prompt, entered, sizeof(entered)) < 0) return false;
  if (strcmp(entered, area->password) != 0) {
    send_str(s, "\r\nAccess denied.\r\n");
    return false;
  }
  area_password_cache_add(s, 'F', area->id);
  return true;
}

static bool file_area_can(Session *s, const DbFileArea *area, const char *acs) {
  if (!s || !area) return false;
  if (acs && acs[0] && !acs_allows(s, acs)) {
    send_str(s, "\r\nAccess denied.\r\n");
    return false;
  }
  return file_area_password_ok(s, area);
}

static bool file_visible_to_user(Session *s, const DbFileRec *rec) {
  if (!s || !rec) return false;
  if ((rec->flags & FILE_FLAG_NOTVAL) && !is_sysop(s)) return false;
  if (rec->flags & FILE_FLAG_OFFLINE) return false;
  return true;
}

static bool file_download_allowed(Session *s, const DbFileArea *area, const DbFileRec *rec,
                                  bool quiet, int *cost_out) {
  int kb = (rec->size_bytes + 1023) / 1024;
  int cost = kb > 0 ? kb : 1;
  bool free_file = (rec->flags & FILE_FLAG_FREE) || area->free_files || (area->flags & FA_FLAG_FREEFILES);
  if (cost_out) *cost_out = free_file ? 0 : cost;

  if (!file_visible_to_user(s, rec)) {
    if (!quiet) send_str(s, "\r\nFile is not available for download.\r\n");
    return false;
  }
  if (!file_area_can(s, area, area->acs_download[0] ? area->acs_download : area->acs_list)) return false;

  DbSecurityLevel sl;
  if (db_security_level_fetch(s->db, s->user.security_level_id, &sl)) {
    if (sl.dl_one_day > 0 && s->user.dl_today >= sl.dl_one_day) {
      if (!quiet) send_str(s, "\r\nDaily file download limit reached.\r\n");
      return false;
    }
    if (sl.dl_k_one_day > 0 && s->user.dl_k_today + kb > sl.dl_k_one_day) {
      if (!quiet) send_str(s, "\r\nDaily download KB limit reached.\r\n");
      return false;
    }
  }

  if (!free_file && !HAS_AC_FLAG(&s->user, AC_FNOCREDITS) && s->credits < cost) {
    if (!quiet) {
      char buf[128];
      snprintf(buf, sizeof(buf), "\r\nInsufficient credits. Need %d, have %d.\r\n", cost, s->credits);
      send_str(s, buf);
    }
    return false;
  }
  if (!HAS_AC_FLAG(&s->user, AC_FNODLRATIO) && s->user.dl_ratio_den > 0) {
    int required_ul = s->user.downloads / s->user.dl_ratio_den;
    if (s->user.uploads < required_ul) {
      if (!quiet) send_str(s, "\r\nYou need to upload more files to maintain your ratio.\r\n");
      return false;
    }
  }
  return true;
}

static void filecmd_copy(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) return;
  if (!src) src = "";
  size_t n = strnlen(src, cap - 1);
  memcpy(dst, src, n);
  dst[n] = '\0';
}

static void file_record_download_success(Session *s, const DbFileArea *area, const DbFileRec *rec, int cost) {
  bool free_file = (rec->flags & FILE_FLAG_FREE) || area->free_files || (area->flags & FA_FLAG_FREEFILES);
  if (!free_file && !HAS_AC_FLAG(&s->user, AC_FNOCREDITS)) {
    s->credits -= cost;
    s->user.credits -= cost;
  }
  s->user.downloads++;
  s->user.dk += (rec->size_bytes / 1024);
  s->user.dl_today++;
  s->user.dl_k_today += (rec->size_bytes / 1024);
  db_file_inc_downloads(s->db, rec->id);
}

static bool resolve_file_path_for_cmd(Session* s, int file_id,
                                      char* filepath, size_t fplen,
                                      DbFileRec* rec_out);

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
      if (areas[i].id == area_id && file_area_can(s, &areas[i], areas[i].acs_list)) {
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
    if (!file_area_can(s, &areas[i], areas[i].acs_list)) continue;
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
      if (!file_area_can(s, &areas[i], areas[i].acs_list)) return;
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
    if (!file_area_can(s, &areas[i], areas[i].acs_list)) continue;
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
  if (!file_area_can(s, &area, area.acs_list)) return;
  
  DbFileRec files[50];
  int fcount = db_file_list(&area, s->db, files, 50);
  
  char buf[512];
  snprintf(buf, sizeof(buf), "\r\n\x1b[1;36mFiles in %s:\x1b[0m\r\n", area.name);
  send_str(s, buf);
  send_str(s, "\x1b[1;33m  #   Filename             Size       Date       DLs   Pts\x1b[0m\r\n");
  send_str(s, "--------------------------------------------------------------\r\n");
  
  for (int i = 0; i < fcount; i++) {
    if (!file_visible_to_user(s, &files[i])) continue;
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
  DbFileArea area_for_policy;
  if (!db_file_area_get(s->db, rec.area_id, &area_for_policy)) {
    send_str(s, "\r\nFile area not found.\r\n");
    return;
  }
  if (!file_visible_to_user(s, &rec) ||
      !file_area_can(s, &area_for_policy, area_for_policy.acs_list)) {
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
  DbFileArea area;
  if (!db_file_area_get(s->db, rec.area_id, &area)) {
    send_str(s, "\r\nFile area not found.\r\n");
    return;
  }
  
  int cost = 0;
  if (!file_download_allowed(s, &area, &rec, false, &cost)) return;
  
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
      file_record_download_success(s, &area, &rec, cost);
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
  
  if (!file_area_can(s, &area, area.acs_upload)) return;

  DbProtocol protos[4];
  int pcnt = db_protocols_list(s->db, protos, 4, "up");
  if (pcnt == 0) {
    send_str(s, "\r\nNo upload protocols configured. Upload unavailable.\r\n");
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
  for (int line_num = 0; line_num < 20; line_num++) {
    char line_buf[128];
    prompt_line(s, "> ", line_buf, sizeof(line_buf));
    if (!line_buf[0]) break;
    if (!bbs_str_append(extended_desc, sizeof(extended_desc), line_buf) ||
        !bbs_str_append(extended_desc, sizeof(extended_desc), "\r\n"))
      break;
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
    transfer_ok = protocol_launch(s, selected, filepath, "up");
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
      DbFileArea area;
      if (!db_file_area_get(s->db, rec.area_id, &area)) continue;
      if (!file_download_allowed(s, &area, &rec, true, NULL)) continue;
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
    int cost = 0;
    if (!file_download_allowed(s, area, &rec, false, &cost)) continue;
    
    char filepath[512];
    if (!file_area_resolve(area->path, rec.filename, filepath, sizeof(filepath))) continue;
    
    snprintf(buf, sizeof(buf), "Sending: %s\r\n", rec.filename);
    send_str(s, buf);
    
    if (protocol_launch(s, selected, filepath, "down")) {
      success_count++;
      file_record_download_success(s, area, &rec, cost);
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
    if (!file_area_can(s, &areas[a], areas[a].acs_list)) continue;
    
    DbFileRec files[100];
    int fcount = db_file_list(&areas[a], s->db, files, 100);
    
    for (int f = 0; f < fcount; f++) {
      if (!file_visible_to_user(s, &files[f])) continue;
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
    if (!file_area_can(s, &areas[a], areas[a].acs_list)) continue;
    
    DbFileRec files[100];
    int fcount = db_file_list(&areas[a], s->db, files, 100);
    
    for (int f = 0; f < fcount; f++) {
      if (!file_visible_to_user(s, &files[f])) continue;
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
  char filepath[512];
  if (!resolve_file_path_for_cmd(s, file_id, filepath, sizeof(filepath), &rec)) return;
  
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
  
  send_str(s, "\x1b[1;33mArchive Contents:\x1b[0m\r\n");

  char listing[8192];
  char errbuf[256];
  if (bbs_archive_list_to_text(filepath, listing, sizeof(listing), 100, errbuf, sizeof(errbuf))) {
    send_str(s, listing[0] ? listing : "(archive is empty)\r\n");
  } else {
    send_str(s, "Unable to read archive: ");
    send_str(s, errbuf);
    send_str(s, "\r\n");
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
    if (!file_area_can(s, &areas[a], areas[a].acs_list)) continue;
    
    DbFileRec files[100];
    int fcount = db_file_list(&areas[a], s->db, files, 100);
    
    for (int f = 0; f < fcount; f++) {
      if (!file_visible_to_user(s, &files[f])) continue;
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
  send_str(s, "\r\nRaw directory listing of: ");
  send_str(s, area.path);
  send_str(s, "\r\n");
  
  DIR* dir = opendir(area.path);
  if (!dir) {
    send_str(s, "Cannot open directory.\r\n");
    return;
  }
  
  struct dirent* ent;
  while ((ent = readdir(dir)) != NULL) {
    if (ent->d_name[0] == '.') continue;
    
    char fullpath[512];
    path_join(area.path, ent->d_name, fullpath, sizeof(fullpath));
    
    struct stat st;
    if (stat(fullpath, &st) == 0) {
      snprintf(buf, sizeof(buf), "%-30.30s %10ld bytes\r\n", ent->d_name, (long)st.st_size);
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
  if (!file_area_can(s, &area, area.acs_download[0] ? area.acs_download : area.acs_list) ||
      !file_visible_to_user(s, rec_out)) {
    send_str(s, "\r\nFile not available.\r\n");
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

  char buf[256];
  snprintf(buf, sizeof(buf), "\r\n\x1b[1;36mTesting archive: %s\x1b[0m\r\n", rec.filename);
  send_str(s, buf);
  send_str(s, "--------------------------------------------------------------\r\n");

  char errbuf[256];
  if (bbs_archive_test(filepath, errbuf, sizeof(errbuf)))
    send_str(s, "Archive OK\r\n");
  else {
    send_str(s, "Archive test failed: ");
    send_str(s, errbuf);
    send_str(s, "\r\n");
  }
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

  char tmpdir[512];
  const char* base = getenv("TMPDIR");
  if (!base || !base[0]) base = s->cfg.data_path[0] ? s->cfg.data_path : "data";
  snprintf(tmpdir, sizeof(tmpdir), "%s/archive_XXXXXX", base);
  if (!mkdtemp(tmpdir)) {
    send_str(s, "\r\nCould not create temp directory.\r\n");
    return;
  }

  char buf[256];
  snprintf(buf, sizeof(buf), "\r\n\x1b[1;36mExtracting: %s\x1b[0m\r\n", rec.filename);
  send_str(s, buf);
  send_str(s, "Temp dir: ");
  send_str(s, tmpdir);
  send_str(s, "\r\n");
  send_str(s, "--------------------------------------------------------------\r\n");

  char errbuf[256];
  if (!bbs_archive_extract_to_dir(filepath, tmpdir, errbuf, sizeof(errbuf))) {
    send_str(s, "Extraction failed: ");
    send_str(s, errbuf);
    send_str(s, "\r\n");
    bbs_remove_tree(tmpdir);
    return;
  }

  send_str(s, "--------------------------------------------------------------\r\n");
  send_str(s, "Extracted files:\r\n");
  DIR* d = opendir(tmpdir);
  if (d) {
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
      if (ent->d_name[0] == '.') continue;
      char outpath[1024];
      path_join(tmpdir, ent->d_name, outpath, sizeof(outpath));
      struct stat st;
      if (stat(outpath, &st) == 0) {
        snprintf(buf, sizeof(buf), "%-40.40s %10ld bytes\r\n", ent->d_name, (long)st.st_size);
        send_str(s, buf);
      }
    }
    closedir(d);
  }

  send_str(s, "\r\nExtraction complete. Temp files cleaned up.\r\n");
  bbs_remove_tree(tmpdir);
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

  if (!bbs_mkdir_p(area.path, 0755)) {
    send_str(s, "\r\nUpload area is not writable.\r\n");
    return;
  }

  char recvdir[512];
  if (!bbs_make_temp_dir("mutineer_upload", recvdir, sizeof(recvdir))) {
    send_str(s, "\r\nCould not create upload staging directory.\r\n");
    return;
  }

  snprintf(buf, sizeof(buf), "\r\nReady for batch upload via %s.\r\n"
           "Send your files now. Press Enter when done.\r\n", proto->name);
  send_str(s, buf);

  /* Launch protocol receive into staging dir (use "." as the path so the
     protocol tool writes into the staging dir via workdir) */
  char dummy_path[512];
  filecmd_copy(dummy_path, sizeof(dummy_path), recvdir);
  size_t dummy_len = strlen(dummy_path);
  if (dummy_len + 1 < sizeof(dummy_path)) {
    dummy_path[dummy_len] = '/';
    dummy_path[dummy_len + 1] = '\0';
  }
  protocol_launch(s, proto, dummy_path, "up");

  /* Scan staging dir for received files */
  DIR* dir = opendir(recvdir);
  int uploaded = 0;
  if (dir) {
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
      if (ent->d_name[0] == '.') continue;

      if (!bbs_safe_filename(ent->d_name, sizeof(((DbFileRec*)0)->filename) - 1)) {
        snprintf(buf, sizeof(buf), "  Rejected unsafe filename: %.220s\r\n", ent->d_name);
        send_str(s, buf);
        continue;
      }

      char srcpath[512], destpath[512];
      path_join(recvdir, ent->d_name, srcpath, sizeof(srcpath));
      if (!file_area_resolve(area.path, ent->d_name, destpath, sizeof(destpath))) {
        snprintf(buf, sizeof(buf), "  Rejected path outside area: %.220s\r\n", ent->d_name);
        send_str(s, buf);
        continue;
      }

      struct stat st;
      if (lstat(srcpath, &st) != 0 || !S_ISREG(st.st_mode)) {
        snprintf(buf, sizeof(buf), "  Rejected non-regular upload: %.220s\r\n", ent->d_name);
        send_str(s, buf);
        continue;
      }
      if (access(destpath, F_OK) == 0) {
        snprintf(buf, sizeof(buf), "  Rejected duplicate filename: %.220s\r\n", ent->d_name);
        send_str(s, buf);
        continue;
      }

      int size = (int)st.st_size;
      if (rename(srcpath, destpath) != 0) {
        if (errno != EXDEV || !file_copy(srcpath, destpath, &size)) {
          snprintf(buf, sizeof(buf), "  Could not move upload: %.220s\r\n", ent->d_name);
          send_str(s, buf);
          unlink(destpath);
          continue;
        }
        unlink(srcpath);
      }

      char desc[256] = {0};
      char prompt_buf[64];
      snprintf(prompt_buf, sizeof(prompt_buf), "Description for %.45s: ", ent->d_name);
      prompt_line(s, prompt_buf, desc, sizeof(desc));

      if (db_file_add_ex(s->db, s->current_file_area, ent->d_name, desc, "", NULL,
                         size, s->user.id, 0, FILE_FLAG_NOTVAL)) {
        s->user.uploads++;
        s->user.uk += (size / 1024);
        s->file_points += 10;
        s->user.file_points += 10;
        uploaded++;

        snprintf(buf, sizeof(buf), "  Queued for validation: %.200s (%d bytes)\r\n",
                 ent->d_name, size);
        send_str(s, buf);
      }
    }
    closedir(dir);
  }

  bbs_remove_tree(recvdir);

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
