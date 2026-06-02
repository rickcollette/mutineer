#include "bbs_session.h"
#include "bbs_db.h"
#include "bbs_acs.h"
#include "bbs_util.h"
#include "bbs_doors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>

extern void send_str(Session* s, const char* str);
extern int prompt_line(Session* s, const char* prompt, char* out, size_t cap);

#define QWK_BLOCK_SIZE 128

static bool protocol_matches_key(const DbProtocol* proto, char key) {
  if (!proto || !proto->active) return false;
  return (key == 'Z' && strcasecmp(proto->name, "Zmodem") == 0) ||
         (key == 'Y' && strcasecmp(proto->name, "Ymodem") == 0) ||
         (key == 'X' && strcasecmp(proto->name, "Xmodem") == 0);
}

static DbProtocol* select_transfer_protocol(Session* s, const char* direction, char key,
                                            DbProtocol* protos, int max) {
  int pcnt = db_protocols_list(s->db, protos, max, direction);
  for (int i = 0; i < pcnt; i++) {
    if (protocol_matches_key(&protos[i], key)) return &protos[i];
  }
  return NULL;
}

static bool find_rep_file(const char* dir, char* out, size_t cap) {
  DIR* d = opendir(dir);
  if (!d) return false;
  struct dirent* ent;
  while ((ent = readdir(d)) != NULL) {
    const char* dot = strrchr(ent->d_name, '.');
    if (!dot || strcasecmp(dot, ".REP") != 0) continue;
    snprintf(out, cap, "%s/%s", dir, ent->d_name);
    closedir(d);
    return true;
  }
  closedir(d);
  return false;
}

typedef struct {
  char status;           /* ' '=public, '*'=private, '+' = private unread */
  char msgnum[7];        /* message number (space-padded) */
  char date[8];          /* MM-DD-YY */
  char time[5];          /* HH:MM */
  char to[25];           /* recipient (space-padded) */
  char from[25];         /* sender (space-padded) */
  char subject[25];      /* subject (space-padded) */
  char password[12];     /* password (space-padded) */
  char refnum[8];        /* reference number (space-padded) */
  char blocks[6];        /* number of 128-byte blocks (space-padded) */
  char active;           /* 225=active, 226=deleted */
  char conf_num[2];      /* conference number (binary, little-endian) */
  char logical_num[2];   /* logical message number (binary, little-endian) */
  char nettag;           /* net tag (space) */
} QwkMsgHeader;

static void pad_field(char* dest, const char* src, size_t len) {
  memset(dest, ' ', len);
  size_t slen = strlen(src);
  if (slen > len) slen = len;
  memcpy(dest, src, slen);
}

static void write_control_dat(FILE* f, Session* s, int num_areas) {
  fprintf(f, "%s\r\n", s->cfg.bbs_name[0] ? s->cfg.bbs_name : "Mutineer BBS");
  fprintf(f, "Unknown\r\n");  /* city, state */
  fprintf(f, "000-000-0000\r\n");  /* phone */
  fprintf(f, "%s\r\n", s->cfg.sysop_name[0] ? s->cfg.sysop_name : "Sysop");
  fprintf(f, "00000,%s\r\n", "MUTINEER");  /* serial#, BBS ID */
  
  time_t now = time(NULL);
  struct tm* tm = localtime(&now);
  fprintf(f, "%02d-%02d-%04d,%02d:%02d:%02d\r\n",
          tm->tm_mon + 1, tm->tm_mday, tm->tm_year + 1900,
          tm->tm_hour, tm->tm_min, tm->tm_sec);
  
  fprintf(f, "%s\r\n", s->user.handle);
  fprintf(f, "\r\n");  /* blank line */
  fprintf(f, "0\r\n");  /* hello file */
  fprintf(f, "0\r\n");  /* news file */
  fprintf(f, "0\r\n");  /* goodbye file */
  
  fprintf(f, "%d\r\n", num_areas - 1);  /* number of conferences - 1 */
  
  DbMsgArea areas[32];
  int acount = db_msg_area_list(s->db, areas, 32);
  for (int i = 0; i < acount && i < num_areas; i++) {
    fprintf(f, "%d\r\n", areas[i].id);
    fprintf(f, "%s\r\n", areas[i].name);
  }
  
  fprintf(f, "HELLO\r\n");
  fprintf(f, "NEWS\r\n");
  fprintf(f, "GOODBYE\r\n");
}

static void write_door_id(FILE* f, Session* s) {
  fprintf(f, "DOOR = Mutineer\r\n");
  fprintf(f, "VERSION = 1.0\r\n");
  fprintf(f, "SYSTEM = %s\r\n", s->cfg.bbs_name[0] ? s->cfg.bbs_name : "Mutineer BBS");
  fprintf(f, "CONTROLNAME = MUTINEER\r\n");
  fprintf(f, "CONTROLTYPE = ADD\r\n");
  fprintf(f, "CONTROLTYPE = DROP\r\n");
}

typedef struct {
  int conf_num;
  long block_offset;
} NdxEntry;

static void write_ndx_msb_float(FILE* f, long block_num) {
  float fval = (float)block_num;
  unsigned char bytes[4];
  memcpy(bytes, &fval, 4);
  fputc(bytes[3], f);
  fputc(bytes[2], f);
  fputc(bytes[1], f);
  fputc(bytes[0], f);
}

static void write_ndx_files(const char* temp_dir,
                            NdxEntry* entries, int count,
                            int* areas_used, int num_areas,
                            NdxEntry* personal, int personal_count) {
  for (int a = 0; a < num_areas; a++) {
    int conf = areas_used[a];
    int conf_count = 0;
    for (int i = 0; i < count; i++) {
      if (entries[i].conf_num == conf) conf_count++;
    }
    if (conf_count == 0) continue;

    char ndx_path[512];
    snprintf(ndx_path, sizeof(ndx_path), "%s/%03d.NDX", temp_dir, conf);
    FILE* f = fopen(ndx_path, "wb");
    if (!f) continue;

    for (int i = 0; i < count; i++) {
      if (entries[i].conf_num == conf) {
        write_ndx_msb_float(f, entries[i].block_offset);
        fputc((unsigned char)conf, f);
      }
    }
    fclose(f);
  }

  char personal_path[512];
  snprintf(personal_path, sizeof(personal_path), "%s/PERSONAL.NDX", temp_dir);
  FILE* f = fopen(personal_path, "wb");
  if (f) {
    for (int i = 0; i < personal_count; i++) {
      write_ndx_msb_float(f, personal[i].block_offset);
      fputc((unsigned char)(personal[i].conf_num & 0xFF), f);
    }
    fclose(f);
  }
}

#define QWK_MAX_NDX   4096
#define QWK_MAX_AREAS   64
#define QWK_DEFAULT_MAX_MSGS 500

static void write_messages_dat_with_ndx(FILE* f, Session* s, const char* last_qwk,
                                        int* msg_count,
                                        NdxEntry** ndx_entries, int* ndx_count,
                                        NdxEntry** personal_entries, int* personal_count,
                                        int** areas_used, int* num_areas_used) {
  QwkMsgHeader hdr;
  *msg_count = 0;
  *ndx_count = 0;
  *personal_count = 0;
  *num_areas_used = 0;

  *ndx_entries = malloc(sizeof(NdxEntry) * QWK_MAX_NDX);
  *personal_entries = malloc(sizeof(NdxEntry) * QWK_MAX_NDX);
  *areas_used = malloc(sizeof(int) * QWK_MAX_AREAS);
  if (!*ndx_entries || !*personal_entries || !*areas_used) return;

  char header_block[QWK_BLOCK_SIZE];
  memset(header_block, ' ', QWK_BLOCK_SIZE);
  memcpy(header_block, "Produced by Mutineer BBS", 24);
  fwrite(header_block, 1, QWK_BLOCK_SIZE, f);

  long current_block = 1;

  DbMsgArea areas[QWK_MAX_AREAS];
  int acount = db_msg_area_list(s->db, areas, QWK_MAX_AREAS);

  for (int a = 0; a < acount; a++) {
    if (!acs_allows(s, areas[a].acs_read)) continue;

    int per_area_max = areas[a].max_msgs > 0 ? areas[a].max_msgs : QWK_DEFAULT_MAX_MSGS;
    DbMessage* msgs = malloc(sizeof(DbMessage) * per_area_max);
    if (!msgs) continue;
    int mcount = db_messages_since(s->db, areas[a].id, last_qwk, msgs, per_area_max);

    int area_has_msgs = 0;

    for (int m = 0; m < mcount; m++) {
      memset(&hdr, ' ', sizeof(hdr));

      int is_private = (msgs[m].to_user > 0);
      if (is_private) {
        hdr.status = (msgs[m].to_user == s->user.id) ? '+' : '*';
      } else {
        hdr.status = ' ';
      }

      char numstr[8];
      snprintf(numstr, sizeof(numstr), "%d", msgs[m].id);
      pad_field(hdr.msgnum, numstr, 7);

      if (strlen(msgs[m].created_at) >= 10) {
        char datestr[9], timestr[6];
        snprintf(datestr, sizeof(datestr), "%c%c-%c%c-%c%c",
                 msgs[m].created_at[5], msgs[m].created_at[6],
                 msgs[m].created_at[8], msgs[m].created_at[9],
                 msgs[m].created_at[2], msgs[m].created_at[3]);
        pad_field(hdr.date, datestr, 8);

        if (strlen(msgs[m].created_at) >= 16) {
          snprintf(timestr, sizeof(timestr), "%c%c:%c%c",
                   msgs[m].created_at[11], msgs[m].created_at[12],
                   msgs[m].created_at[14], msgs[m].created_at[15]);
          pad_field(hdr.time, timestr, 5);
        }
      }

      pad_field(hdr.to, msgs[m].to_name[0] ? msgs[m].to_name : "All", 25);
      pad_field(hdr.from, msgs[m].from_name[0] ? msgs[m].from_name : msgs[m].user_handle, 25);
      pad_field(hdr.subject, msgs[m].subject, 25);
      pad_field(hdr.password, "", 12);

      snprintf(numstr, sizeof(numstr), "%d", msgs[m].reply_to);
      pad_field(hdr.refnum, numstr, 8);

      size_t body_len = strlen(msgs[m].body);
      int num_blocks = (int)((sizeof(QwkMsgHeader) + body_len + QWK_BLOCK_SIZE - 1) / QWK_BLOCK_SIZE);
      snprintf(numstr, sizeof(numstr), "%d", num_blocks);
      pad_field(hdr.blocks, numstr, 6);

      hdr.active = (char)225;
      hdr.conf_num[0] = (char)(areas[a].id & 0xFF);
      hdr.conf_num[1] = (char)((areas[a].id >> 8) & 0xFF);
      hdr.logical_num[0] = (char)(msgs[m].id & 0xFF);
      hdr.logical_num[1] = (char)((msgs[m].id >> 8) & 0xFF);
      hdr.nettag = ' ';

      if (*ndx_count < QWK_MAX_NDX) {
        (*ndx_entries)[*ndx_count].conf_num = areas[a].id;
        (*ndx_entries)[*ndx_count].block_offset = current_block;
        (*ndx_count)++;
      }

      if (msgs[m].to_user == s->user.id && *personal_count < QWK_MAX_NDX) {
        (*personal_entries)[*personal_count].conf_num = areas[a].id;
        (*personal_entries)[*personal_count].block_offset = current_block;
        (*personal_count)++;
      }

      fwrite(&hdr, 1, sizeof(hdr), f);

      size_t remaining = num_blocks * QWK_BLOCK_SIZE - sizeof(hdr);
      char* body_block = calloc(1, remaining);
      if (body_block) {
        memset(body_block, ' ', remaining);
        memcpy(body_block, msgs[m].body, body_len < remaining ? body_len : remaining);

        for (size_t i = 0; i < remaining && i < body_len; i++) {
          if (body_block[i] == '\n') body_block[i] = (char)227;
        }

        fwrite(body_block, 1, remaining, f);
        free(body_block);
      }

      current_block += num_blocks;
      (*msg_count)++;
      area_has_msgs = 1;
    }

    free(msgs);

    if (area_has_msgs && *num_areas_used < QWK_MAX_AREAS) {
      (*areas_used)[(*num_areas_used)++] = areas[a].id;
    }
  }
}

static void write_newfiles_dat(FILE* f, Session* s, const char* last_qwk) {
  DbFileArea fareas[QWK_MAX_AREAS];
  int fcount = db_file_area_list(s->db, fareas, QWK_MAX_AREAS);

  for (int a = 0; a < fcount; a++) {
    if (!acs_allows(s, fareas[a].acs_list)) continue;

    int max_files = fareas[a].max_files > 0 ? fareas[a].max_files : 500;
    DbFileRec* files = malloc(sizeof(DbFileRec) * max_files);
    if (!files) continue;
    int fc = db_file_list(&fareas[a], s->db, files, max_files);

    int area_written = 0;
    for (int i = 0; i < fc; i++) {
      if (last_qwk && last_qwk[0] && strcmp(files[i].uploaded_at, last_qwk) <= 0)
        continue;

      if (!area_written) {
        fprintf(f, "--- %s ---\r\n", fareas[a].name);
        area_written = 1;
      }

      char date_str[12] = "--/--/--";
      if (strlen(files[i].uploaded_at) >= 10) {
        snprintf(date_str, sizeof(date_str), "%c%c-%c%c-%c%c",
                 files[i].uploaded_at[5], files[i].uploaded_at[6],
                 files[i].uploaded_at[8], files[i].uploaded_at[9],
                 files[i].uploaded_at[2], files[i].uploaded_at[3]);
      }

      fprintf(f, "%-13s %8d  %s  %s\r\n",
              files[i].filename,
              files[i].size_bytes,
              date_str,
              files[i].desc[0] ? files[i].desc : "(no description)");
    }

    free(files);
  }
}

bool qwk_generate_packet(Session* s, const char* output_path, const char* last_qwk) {
  if (!s || !output_path) return false;

  char temp_dir[512];
  snprintf(temp_dir, sizeof(temp_dir), "/tmp/qwk_%d_%d", (int)getpid(), s->node_num);
  mkdir(temp_dir, 0755);

  char control_path[512], messages_path[512], door_path[512], newfiles_path[512];
  snprintf(control_path,  sizeof(control_path),  "%s/CONTROL.DAT",    temp_dir);
  snprintf(messages_path, sizeof(messages_path), "%s/MESSAGES.DAT",   temp_dir);
  snprintf(door_path,     sizeof(door_path),     "%s/DOOR.ID",        temp_dir);
  snprintf(newfiles_path, sizeof(newfiles_path), "%s/NEWFILES.DAT",   temp_dir);

  DbMsgArea areas[QWK_MAX_AREAS];
  int num_areas = db_msg_area_list(s->db, areas, QWK_MAX_AREAS);

  FILE* f = fopen(control_path, "w");
  if (f) {
    write_control_dat(f, s, num_areas);
    fclose(f);
  }

  int msg_count = 0;
  NdxEntry* ndx_entries = NULL;
  int ndx_count = 0;
  NdxEntry* personal_entries = NULL;
  int personal_count = 0;
  int* areas_used = NULL;
  int num_areas_used = 0;

  f = fopen(messages_path, "wb");
  if (f) {
    write_messages_dat_with_ndx(f, s, last_qwk, &msg_count,
                                &ndx_entries, &ndx_count,
                                &personal_entries, &personal_count,
                                &areas_used, &num_areas_used);
    fclose(f);
  }

  write_ndx_files(temp_dir, ndx_entries, ndx_count, areas_used, num_areas_used,
                  personal_entries, personal_count);
  free(ndx_entries);
  free(personal_entries);
  free(areas_used);

  f = fopen(door_path, "w");
  if (f) {
    write_door_id(f, s);
    fclose(f);
  }

  f = fopen(newfiles_path, "w");
  if (f) {
    write_newfiles_dat(f, s, last_qwk);
    fclose(f);
  }

  char cmd[1024];
  snprintf(cmd, sizeof(cmd),
           "cd '%s' && zip -q '%s' CONTROL.DAT MESSAGES.DAT DOOR.ID NEWFILES.DAT *.NDX 2>/dev/null",
           temp_dir, output_path);
  int rc = system(cmd);

  snprintf(cmd, sizeof(cmd),
           "rm -f '%s'/*.NDX '%s'/CONTROL.DAT '%s'/MESSAGES.DAT '%s'/DOOR.ID '%s'/NEWFILES.DAT 2>/dev/null",
           temp_dir, temp_dir, temp_dir, temp_dir, temp_dir);
  system(cmd);
  rmdir(temp_dir);

  return rc == 0;
}

void cmd_qwk_download(Session* s, const char* data) {
  (void)data;
  
  send_str(s, "\r\n\x1b[1;36mQWK Mail Packet Download\x1b[0m\r\n");
  send_str(s, "--------------------------------------\r\n");
  
  DbMsgArea areas[32];
  int acount = db_msg_area_list(s->db, areas, 32);
  
  int total_msgs = 0;
  char buf[256];
  for (int i = 0; i < acount; i++) {
    if (!acs_allows(s, areas[i].acs_read)) continue;
    int count = db_count_messages_area(s->db, areas[i].id);
    total_msgs += count;
    snprintf(buf, sizeof(buf), "  [%2d] %-30s %5d msgs\r\n", areas[i].id, areas[i].name, count);
    send_str(s, buf);
  }
  
  snprintf(buf, sizeof(buf), "\r\nTotal messages available: %d\r\n", total_msgs);
  send_str(s, buf);
  
  send_str(s, "\r\nGenerate QWK packet? (Y/N): ");
  char line[8];
  prompt_line(s, "", line, sizeof(line));
  if (line[0] != 'Y' && line[0] != 'y') {
    send_str(s, "\r\nCancelled.\r\n");
    return;
  }
  
  send_str(s, "\r\nGenerating QWK packet...\r\n");

  char qwk_path[512];
  snprintf(qwk_path, sizeof(qwk_path), "/tmp/%s.QWK", s->user.handle);

  const char* last_qwk = s->user.last_qwk[0] ? s->user.last_qwk : NULL;
  if (qwk_generate_packet(s, qwk_path, last_qwk)) {
    struct stat st;
    if (stat(qwk_path, &st) == 0) {
      snprintf(buf, sizeof(buf), "\r\nPacket generated: %s (%ld bytes)\r\n", qwk_path, (long)st.st_size);
      send_str(s, buf);

      send_str(s, "\r\nSelect download protocol: (Z)modem, (Y)modem, (X)modem: ");
      prompt_line(s, "", line, sizeof(line));
      char proto_key = (char)toupper((unsigned char)line[0]);
      if (proto_key != 'Z' && proto_key != 'Y' && proto_key != 'X') {
        send_str(s, "\r\nInvalid protocol.\r\n");
        unlink(qwk_path);
        return;
      }

      DbProtocol protos[8];
      DbProtocol* selected = select_transfer_protocol(s, "down", proto_key, protos, 8);
      if (!selected) {
        send_str(s, "\r\nProtocol not configured. Contact sysop.\r\n");
        unlink(qwk_path);
        return;
      }

      send_str(s, "\r\nStarting QWK download...\r\n");
      if (protocol_launch(s, selected, qwk_path, "down")) {
        send_str(s, "\r\nDownload complete.\r\n");
        /* Stamp last_qwk so next packet only includes new content */
        time_t now = time(NULL);
        struct tm* tm = gmtime(&now);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
        db_user_set_last_qwk(s->db, s->user.id, ts);
        strncpy(s->user.last_qwk, ts, sizeof(s->user.last_qwk) - 1);
      } else {
        send_str(s, "\r\nTransfer failed or cancelled.\r\n");
      }

      unlink(qwk_path);
    }
  } else {
    send_str(s, "\r\nFailed to generate QWK packet.\r\n");
  }
}

bool qwk_import_rep(Session* s, const char* rep_path) {
  if (!s || !rep_path) return false;
  
  /* Create temp directory for extraction */
  char temp_dir[512];
  snprintf(temp_dir, sizeof(temp_dir), "/tmp/rep_%d_%d", (int)getpid(), s->node_num);
  mkdir(temp_dir, 0755);
  
  /* Extract REP packet */
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "unzip -q -o '%s' -d '%s' 2>/dev/null", rep_path, temp_dir);
  if (system(cmd) != 0) {
    rmdir(temp_dir);
    return false;
  }
  
  /* Look for *.MSG file (BBSID.MSG) */
  char msg_path[512];
  snprintf(msg_path, sizeof(msg_path), "%s/MUTINEER.MSG", temp_dir);
  
  FILE* f = fopen(msg_path, "rb");
  if (!f) {
    /* Try uppercase */
    snprintf(msg_path, sizeof(msg_path), "%s/mutineer.msg", temp_dir);
    f = fopen(msg_path, "rb");
  }
  
  if (!f) {
    /* Clean up and fail */
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", temp_dir);
    system(cmd);
    return false;
  }
  
  /* Skip first block (header) */
  fseek(f, QWK_BLOCK_SIZE, SEEK_SET);
  
  int imported = 0;
  QwkMsgHeader hdr;
  char buf[256];
  
  while (fread(&hdr, 1, sizeof(hdr), f) == sizeof(hdr)) {
    /* Parse number of blocks */
    char blocks_str[7];
    memcpy(blocks_str, hdr.blocks, 6);
    blocks_str[6] = '\0';
    int num_blocks = atoi(blocks_str);
    if (num_blocks < 1 || num_blocks > 1000) break;
    
    /* Parse conference number */
    int conf_num = (unsigned char)hdr.conf_num[0] | ((unsigned char)hdr.conf_num[1] << 8);
    
    /* Read message body */
    size_t body_size = (num_blocks * QWK_BLOCK_SIZE) - sizeof(hdr);
    char* body = malloc(body_size + 1);
    if (!body) break;
    
    size_t read = fread(body, 1, body_size, f);
    body[read] = '\0';
    
    /* Convert QWK line endings (227) back to newlines */
    for (size_t i = 0; i < read; i++) {
      if (body[i] == (char)227) body[i] = '\n';
    }
    
    /* Trim trailing spaces */
    while (read > 0 && (body[read-1] == ' ' || body[read-1] == '\0')) {
      body[--read] = '\0';
    }

    if (hdr.active != (char)225) {
      free(body);
      continue;
    }
    
    /* Extract fields */
    char to_name[26], subject[26];
    memcpy(to_name, hdr.to, 25);
    to_name[25] = '\0';
    memcpy(subject, hdr.subject, 25);
    subject[25] = '\0';
    
    /* Trim trailing spaces from fields */
    for (int i = 24; i >= 0 && to_name[i] == ' '; i--) to_name[i] = '\0';
    for (int i = 24; i >= 0 && subject[i] == ' '; i--) subject[i] = '\0';
    
    /* Check if user has post access to this area */
    DbMsgArea area;
    if (db_msg_area_get(s->db, conf_num, &area) && acs_allows(s, area.acs_post)) {
      /* Post the message */
      DbMessage msg;
      memset(&msg, 0, sizeof(msg));
      msg.area_id = conf_num;
      msg.user_id = s->user.id;
      strncpy(msg.user_handle, s->user.handle, sizeof(msg.user_handle) - 1);
      strncpy(msg.to_name, to_name, sizeof(msg.to_name) - 1);
      strncpy(msg.from_name, s->user.handle, sizeof(msg.from_name) - 1);
      strncpy(msg.subject, subject, sizeof(msg.subject) - 1);
      strncpy(msg.body, body, sizeof(msg.body) - 1);
      
      if (db_message_post_ex(s->db, &msg)) {
        imported++;
        snprintf(buf, sizeof(buf), "  Imported: [%d] %s\r\n", conf_num, subject);
        send_str(s, buf);
      }
    }
    
    free(body);
  }
  
  fclose(f);
  
  /* Clean up temp directory */
  snprintf(cmd, sizeof(cmd), "rm -rf '%s'", temp_dir);
  system(cmd);
  
  snprintf(buf, sizeof(buf), "\r\nImported %d message(s).\r\n", imported);
  send_str(s, buf);
  
  return imported > 0;
}

void cmd_qwk_upload(Session* s, const char* data) {
  (void)data;
  
  send_str(s, "\r\n\x1b[1;36mQWK Reply Packet Upload\x1b[0m\r\n");
  send_str(s, "--------------------------------------\r\n");
  send_str(s, "\r\nUpload your .REP reply packet.\r\n");
  send_str(s, "Select upload protocol: (Z)modem, (Y)modem, (X)modem, (A)bort: ");
  
  char line[8];
  prompt_line(s, "", line, sizeof(line));
  
  if (line[0] == 'A' || line[0] == 'a' || line[0] == '\0') {
    send_str(s, "\r\nCancelled.\r\n");
    return;
  }
  
  char proto_key = (char)toupper((unsigned char)line[0]);
  if (proto_key != 'Z' && proto_key != 'Y' && proto_key != 'X') {
    send_str(s, "\r\nInvalid protocol.\r\n");
    return;
  }

  DbProtocol protos[8];
  DbProtocol* selected = select_transfer_protocol(s, "up", proto_key, protos, 8);
  if (!selected) {
    send_str(s, "\r\nProtocol not configured. Contact sysop.\r\n");
    return;
  }

  char temp_dir[512];
  snprintf(temp_dir, sizeof(temp_dir), "/tmp/qwk_rep_%d_%d", (int)getpid(), s->node_num);
  mkdir(temp_dir, 0755);

  char rep_path[512];
  snprintf(rep_path, sizeof(rep_path), "%s/%s_%d.REP", temp_dir, s->user.handle, s->node_num);

  DbProtocol temp_proto = *selected;
  char temp_cmd[512];
  snprintf(temp_cmd, sizeof(temp_cmd), "cd '%s' && %s", temp_dir, selected->command);
  snprintf(temp_proto.command, sizeof(temp_proto.command), "%s", temp_cmd);
  
  send_str(s, "\r\nReady to receive REP packet...\r\n");
  if (!protocol_launch(s, &temp_proto, rep_path, "up")) {
    send_str(s, "\r\nTransfer failed or cancelled.\r\n");
    rmdir(temp_dir);
    return;
  }

  if (access(rep_path, R_OK) != 0) {
    if (!find_rep_file(temp_dir, rep_path, sizeof(rep_path))) {
      send_str(s, "\r\nNo REP file received.\r\n");
      rmdir(temp_dir);
      return;
    }
  }

  if (access(rep_path, R_OK) == 0) {
    send_str(s, "\r\nProcessing reply packet...\r\n");
    if (!qwk_import_rep(s, rep_path)) {
      send_str(s, "Failed to import reply packet.\r\n");
    }
    unlink(rep_path);
  } else {
    send_str(s, "\r\nNo REP file found. Upload cancelled.\r\n");
  }
  rmdir(temp_dir);
}
