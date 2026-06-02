#include "bbs_doors.h"
#include "bbs_util.h"
#include "bbs_log.h"
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

static bool ensure_dir(const char* path) {
  return mkdir(path, 0755) == 0 || errno == EEXIST;
}

/* =========================================================================
 * Minimal JSON key-value extractor for flat manifest objects.
 * Handles string, integer, and boolean values only.
 * ========================================================================= */

static const char* json_find_key(const char* json, const char* key) {
  char search[128];
  snprintf(search, sizeof(search), "\"%s\"", key);
  const char* p = json;
  while ((p = strstr(p, search)) != NULL) {
    const char* after = p + strlen(search);
    while (*after == ' ' || *after == '\t' || *after == '\n' || *after == '\r') after++;
    if (*after == ':') return after + 1;
    p++;
  }
  return NULL;
}

static bool json_str(const char* json, const char* key, char* out, size_t cap) {
  const char* p = json_find_key(json, key);
  if (!p) return false;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  if (*p != '"') return false;
  p++;
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < cap) {
    if (*p == '\\' && *(p+1)) {
      p++;
      switch (*p) {
        case 'n':  out[i++] = '\n'; break;
        case 't':  out[i++] = '\t'; break;
        case '\\': out[i++] = '\\'; break;
        case '"':  out[i++] = '"';  break;
        case '/':  out[i++] = '/';  break;
        default:   out[i++] = *p;   break;
      }
    } else {
      out[i++] = *p;
    }
    p++;
  }
  out[i] = '\0';
  return true;
}

static bool json_int(const char* json, const char* key, int* out) {
  const char* p = json_find_key(json, key);
  if (!p) return false;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  if (!isdigit((unsigned char)*p) && *p != '-') return false;
  *out = atoi(p);
  return true;
}

static bool json_bool(const char* json, const char* key, int* out) {
  const char* p = json_find_key(json, key);
  if (!p) return false;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  if (strncmp(p, "true", 4) == 0)  { *out = 1; return true; }
  if (strncmp(p, "false", 5) == 0) { *out = 0; return true; }
  return false;
}

/* =========================================================================
 * Path safety helpers
 * ========================================================================= */

/* Returns false if path is empty, absolute, or contains a ".." component. */
static bool path_is_safe_relative(const char* path) {
  if (!path || !path[0]) return false;
  if (path[0] == '/') return false;
  const char* p = path;
  while (*p) {
    if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) return false;
    while (*p && *p != '/') p++;
    if (*p == '/') p++;
  }
  return true;
}

/* =========================================================================
 * DOSBox manifest parsing and validation
 * ========================================================================= */

bool dosbox_manifest_parse(const char* path, DosboxManifest* out,
                           char* errbuf, size_t errcap) {
  if (!path || !out) {
    if (errbuf) snprintf(errbuf, errcap, "null argument");
    return false;
  }
  memset(out, 0, sizeof(*out));

  char* json = file_read_all(path, NULL);
  if (!json) {
    if (errbuf) snprintf(errbuf, errcap, "cannot read manifest: %s", path);
    return false;
  }

  json_str(json, "runner",       out->runner,       sizeof(out->runner));
  json_str(json, "name",         out->name,         sizeof(out->name));
  json_str(json, "master_dir",   out->master_dir,   sizeof(out->master_dir));
  json_str(json, "startup",      out->startup,      sizeof(out->startup));
  json_str(json, "dropfile",     out->dropfile,     sizeof(out->dropfile));
  json_str(json, "dropfile_dest",out->dropfile_dest,sizeof(out->dropfile_dest));
  json_str(json, "machine",      out->machine,      sizeof(out->machine));
  json_str(json, "core",         out->core,         sizeof(out->core));
  json_str(json, "cycles",       out->cycles,       sizeof(out->cycles));
  json_str(json, "copy_mode",    out->copy_mode,    sizeof(out->copy_mode));

  int v = 0;
  if (json_int(json, "memsize", &v))      out->memsize = v;
  if (json_int(json, "timeout_sec", &v))  out->timeout_sec = v;
  if (json_bool(json, "serial_telnet", &v)) out->serial_telnet = v;
  else out->serial_telnet = 1; /* default on */
  if (json_bool(json, "usedtr", &v))        out->usedtr = v;
  if (json_bool(json, "cleanup_on_exit", &v)) out->cleanup_on_exit = v;
  else out->cleanup_on_exit = 1; /* default on */

  /* Apply defaults for optional fields */
  if (!out->machine[0])   snprintf(out->machine,   sizeof(out->machine),   "svga_s3");
  if (out->memsize <= 0)  out->memsize = 16;
  if (!out->core[0])      snprintf(out->core,      sizeof(out->core),      "auto");
  if (!out->cycles[0])    snprintf(out->cycles,    sizeof(out->cycles),    "auto");
  if (!out->copy_mode[0]) snprintf(out->copy_mode, sizeof(out->copy_mode), "copy");
  if (!out->dropfile[0])  snprintf(out->dropfile,  sizeof(out->dropfile),  "DOOR.SYS");
  if (!out->dropfile_dest[0]) snprintf(out->dropfile_dest, sizeof(out->dropfile_dest), "game");

  free(json);
  return true;
}

bool dosbox_manifest_validate(const DosboxManifest* m, char* errbuf, size_t errcap) {
  if (!m) { if (errbuf) snprintf(errbuf, errcap, "null manifest"); return false; }

  if (!m->master_dir[0]) {
    if (errbuf) snprintf(errbuf, errcap, "manifest: master_dir is required");
    return false;
  }
  if (m->master_dir[0] != '/') {
    if (errbuf) snprintf(errbuf, errcap, "manifest: master_dir must be an absolute path");
    return false;
  }
  if (!m->startup[0]) {
    if (errbuf) snprintf(errbuf, errcap, "manifest: startup is required");
    return false;
  }
  /* startup must not escape the game/ directory */
  if (!path_is_safe_relative(m->startup)) {
    if (errbuf) snprintf(errbuf, errcap, "manifest: startup contains unsafe path: %s", m->startup);
    return false;
  }
  /* dropfile_dest must stay within runtime tree */
  if (m->dropfile_dest[0] && !path_is_safe_relative(m->dropfile_dest)) {
    if (errbuf) snprintf(errbuf, errcap, "manifest: dropfile_dest contains unsafe path: %s", m->dropfile_dest);
    return false;
  }
  return true;
}

/* =========================================================================
 * Runtime tree management
 * ========================================================================= */

static bool copy_dir_recursive(const char* src, const char* dst) {
  struct stat st;
  if (stat(src, &st) != 0 || !S_ISDIR(st.st_mode)) return false;
  if (mkdir(dst, 0755) != 0 && errno != EEXIST) return false;

  DIR* d = opendir(src);
  if (!d) return false;

  bool ok = true;
  struct dirent* ent;
  while (ok && (ent = readdir(d)) != NULL) {
    if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
    char src_p[1024], dst_p[1024];
    snprintf(src_p, sizeof(src_p), "%s/%s", src, ent->d_name);
    snprintf(dst_p, sizeof(dst_p), "%s/%s", dst, ent->d_name);
    if (stat(src_p, &st) != 0) { ok = false; break; }
    if (S_ISDIR(st.st_mode))      ok = copy_dir_recursive(src_p, dst_p);
    else if (S_ISREG(st.st_mode)) ok = file_copy(src_p, dst_p, NULL);
  }
  closedir(d);
  return ok;
}

bool dosbox_prepare_runtime(const DosboxManifest* m,
                            const char* runtime_base,
                            int node_num, const char* launch_id,
                            char* runtime_root_out, size_t root_cap,
                            char* errbuf, size_t errcap) {
  /* <runtime_base>/<door_name>/node<NN>/<launch_id>/ */
  char door_base[1024], node_dir[1024], root[1024], game_dir[1024], logs_dir[1024];
  snprintf(door_base, sizeof(door_base), "%s/%s", runtime_base,
           m->name[0] ? m->name : "door");
  snprintf(node_dir,  sizeof(node_dir),  "%s/node%02d", door_base, node_num);
  snprintf(root,      sizeof(root),      "%s/%s",       node_dir,  launch_id);
  snprintf(game_dir,  sizeof(game_dir),  "%s/game",     root);
  snprintf(logs_dir,  sizeof(logs_dir),  "%s/logs",     root);

  if (!ensure_dir(runtime_base) || !ensure_dir(door_base) ||
      !ensure_dir(node_dir)     || !ensure_dir(root)      ||
      !ensure_dir(game_dir)     || !ensure_dir(logs_dir)) {
    if (errbuf) snprintf(errbuf, errcap, "cannot create runtime tree: %s: %s",
                         root, strerror(errno));
    return false;
  }

  if (!copy_dir_recursive(m->master_dir, game_dir)) {
    if (errbuf) snprintf(errbuf, errcap, "cannot copy master_dir %s to %s",
                         m->master_dir, game_dir);
    return false;
  }

  if (runtime_root_out) snprintf(runtime_root_out, root_cap, "%s", root);
  return true;
}

bool dosbox_build_conf(const DosboxManifest* m, const char* game_dir,
                       const char* conf_path,
                       char* errbuf, size_t errcap) {
  FILE* f = fopen(conf_path, "w");
  if (!f) {
    if (errbuf) snprintf(errbuf, errcap, "cannot create dosbox conf %s: %s",
                         conf_path, strerror(errno));
    return false;
  }

  fprintf(f, "[sdl]\n");
  fprintf(f, "fullscreen=false\n");
  fprintf(f, "output=surface\n\n");

  fprintf(f, "[dosbox]\n");
  fprintf(f, "machine=%s\n", m->machine);
  fprintf(f, "memsize=%d\n\n", m->memsize);

  fprintf(f, "[cpu]\n");
  fprintf(f, "core=%s\n", m->core);
  fprintf(f, "cycles=%s\n\n", m->cycles);

  fprintf(f, "[serial]\n");
  if (m->usedtr)
    fprintf(f, "serial1=nullmodem inhsocket:1 telnet:1 usedtr:1\n\n");
  else
    fprintf(f, "serial1=nullmodem inhsocket:1 telnet:1\n\n");

  fprintf(f, "[autoexec]\n");
  fprintf(f, "mount c \"%s\"\n", game_dir);
  fprintf(f, "c:\n");
  fprintf(f, "%s\n", m->startup);
  fprintf(f, "exit\n");

  fclose(f);
  (void)errbuf; (void)errcap;
  return true;
}

void dosbox_cleanup_runtime(const char* runtime_root) {
  if (!runtime_root || !runtime_root[0]) return;
  /* Safety: must be an absolute path to avoid rm -rf accidents */
  if (runtime_root[0] != '/') {
    log_warn("dosbox cleanup: refusing relative path: %s", runtime_root);
    return;
  }
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "rm -rf '%s'", runtime_root);
  if (system(cmd) != 0)
    log_warn("dosbox cleanup: rm -rf failed for %s", runtime_root);
  else
    log_info("dosbox cleanup: removed runtime tree %s", runtime_root);
}

static bool write_doorsys(Session* s, const char* dir) {
  char path[512]; path_join(dir, "DOOR.SYS", path, sizeof(path));
  FILE* f = fopen(path, "w");
  if (!f) return false;
  
  /* Full DOOR.SYS format (52 lines) */
  fprintf(f, "COM0:\n");                              /* 1: COM port */
  fprintf(f, "38400\n");                              /* 2: Baud rate */
  fprintf(f, "8\n");                                  /* 3: Data bits */
  fprintf(f, "%d\n", s->node_num);                    /* 4: Node number */
  fprintf(f, "Y\n");                                  /* 5: Screen display (Y/N) */
  fprintf(f, "Y\n");                                  /* 6: Printer on (Y/N) */
  fprintf(f, "Y\n");                                  /* 7: Page bell (Y/N) */
  fprintf(f, "Y\n");                                  /* 8: Caller alarm (Y/N) */
  fprintf(f, "%s\n", s->user.real_name[0] ? s->user.real_name : s->user.handle); /* 9: Real name */
  fprintf(f, "%s\n", s->user.street[0] ? s->user.street : "Unknown");  /* 10: City/State */
  fprintf(f, "%s\n", s->user.phone[0] ? s->user.phone : "000-000-0000"); /* 11: Home phone */
  fprintf(f, "%s\n", s->user.phone[0] ? s->user.phone : "000-000-0000"); /* 12: Work phone */
  fprintf(f, "********\n");                           /* 13: Password (masked) */
  fprintf(f, "%d\n", s->user.level);                  /* 14: Security level */
  fprintf(f, "%d\n", s->user.logged_on);              /* 15: Total calls */
  fprintf(f, "%s\n", s->user.last_login_at);          /* 16: Last call date */
  fprintf(f, "%d\n", s->time_left_min * 60);          /* 17: Seconds remaining */
  fprintf(f, "%d\n", s->time_left_min);               /* 18: Minutes remaining */
  fprintf(f, "GR\n");                                 /* 19: Graphics mode */
  fprintf(f, "24\n");                                 /* 20: Screen length */
  fprintf(f, "Y\n");                                  /* 21: Expert mode */
  fprintf(f, "ABCDEFGHIJKLMNOPQRSTUVWXYZ\n");         /* 22: Conferences registered */
  fprintf(f, "%d\n", s->current_conf);                /* 23: Current conference */
  fprintf(f, "%s\n", s->user.expires_at);             /* 24: Expiration date */
  fprintf(f, "%d\n", s->user.id);                     /* 25: User record number */
  fprintf(f, "Y\n");                                  /* 26: Default protocol */
  fprintf(f, "%d\n", s->user.uploads);                /* 27: Total uploads */
  fprintf(f, "%d\n", s->user.downloads);              /* 28: Total downloads */
  fprintf(f, "%d\n", s->user.dk);                     /* 29: Daily download K */
  fprintf(f, "%d\n", 0);                              /* 30: Max daily download K */
  fprintf(f, "%s\n", s->user.birth_date);             /* 31: Birth date */
  fprintf(f, "%s\\\n", dir);                          /* 32: Path to MAIN dir */
  fprintf(f, "%s\\\n", dir);                          /* 33: Path to GEN dir */
  fprintf(f, "%s\n", s->cfg.sysop_name[0] ? s->cfg.sysop_name : "Sysop"); /* 34: Sysop name */
  fprintf(f, "%s\n", s->user.handle);                 /* 35: User alias */
  fprintf(f, "00:00\n");                              /* 36: Event time */
  fprintf(f, "Y\n");                                  /* 37: Error correcting */
  fprintf(f, "Y\n");                                  /* 38: ANSI supported */
  fprintf(f, "Y\n");                                  /* 39: Use record locking */
  fprintf(f, "7\n");                                  /* 40: Default color */
  fprintf(f, "%d\n", s->time_left_min);               /* 41: Time credits */
  fprintf(f, "%s\n", s->user.last_login_at);          /* 42: Last new files scan */
  fprintf(f, "%s\n", s->started_at ? ctime(&s->started_at) : ""); /* 43: Time of call */
  fprintf(f, "%s\n", s->user.last_login_at);          /* 44: Time of last call */
  fprintf(f, "32767\n");                              /* 45: Max daily files */
  fprintf(f, "%d\n", s->user.downloads);              /* 46: Files downloaded today */
  fprintf(f, "%d\n", s->user.uk);                     /* 47: Total upload K */
  fprintf(f, "%d\n", s->user.dk);                     /* 48: Total download K */
  fprintf(f, "None\n");                               /* 49: Comment */
  fprintf(f, "0\n");                                  /* 50: Doors opened */
  fprintf(f, "%d\n", s->user.msg_post);               /* 51: Messages left */
  
  fclose(f);
  return true;
}

static bool write_door32sys(Session* s, const char* dir) {
  char path[512]; path_join(dir, "DOOR32.SYS", path, sizeof(path));
  FILE* f = fopen(path, "w");
  if (!f) return false;
  
  /* DOOR32.SYS format - designed for 32-bit door programs */
  fprintf(f, "2\n");                                  /* 1: Comm type (2=telnet) */
  fprintf(f, "%d\n", s->fd);                          /* 2: Comm handle (socket fd) */
  fprintf(f, "38400\n");                              /* 3: Baud rate */
  fprintf(f, "%s %s\n", s->cfg.bbs_name[0] ? s->cfg.bbs_name : "Mutineer BBS", 
          "Mutineer");                                /* 4: BBS software name */
  fprintf(f, "1\n");                                  /* 5: BBS software version */
  fprintf(f, "%s\n", s->user.real_name[0] ? s->user.real_name : s->user.handle); /* 6: Real name */
  fprintf(f, "%s\n", s->user.handle);                 /* 7: Alias/handle */
  fprintf(f, "%d\n", s->user.level);                  /* 8: Security level */
  fprintf(f, "%d\n", s->time_left_min);               /* 9: Time remaining (minutes) */
  fprintf(f, "1\n");                                  /* 10: Emulation (1=ANSI) */
  fprintf(f, "%d\n", s->node_num);                    /* 11: Node number */
  
  fclose(f);
  return true;
}

static bool write_chaintxt(Session* s, const char* dir) {
  char path[512]; path_join(dir, "CHAIN.TXT", path, sizeof(path));
  FILE* f = fopen(path, "w");
  if (!f) return false;
  
  /* CHAIN.TXT format (WWIV style) */
  fprintf(f, "%d\n", s->user.id);                     /* 1: User number */
  fprintf(f, "%s\n", s->user.handle);                 /* 2: User alias */
  fprintf(f, "%s\n", s->user.real_name[0] ? s->user.real_name : s->user.handle); /* 3: Real name */
  fprintf(f, "\n");                                   /* 4: Call sign (ham radio) */
  fprintf(f, "%d\n", 0);                              /* 5: Age (calculated) */
  fprintf(f, "%c\n", s->user.sex ? s->user.sex : 'M'); /* 6: Sex */
  fprintf(f, "%d\n", s->user.credits);                /* 7: Gold */
  fprintf(f, "%s\n", s->user.last_login_at);          /* 8: Last call date */
  fprintf(f, "80\n");                                 /* 9: Screen width */
  fprintf(f, "24\n");                                 /* 10: Screen lines */
  fprintf(f, "%d\n", s->user.level);                  /* 11: Security level */
  fprintf(f, "0\n");                                  /* 12: Co-sysop (0/1) */
  fprintf(f, "1\n");                                  /* 13: Sysop (0/1) */
  fprintf(f, "1\n");                                  /* 14: ANSI (0/1) */
  fprintf(f, "0\n");                                  /* 15: Remote (0=local, 1=remote) */
  fprintf(f, "%d\n", s->time_left_min * 60);          /* 16: Seconds remaining */
  fprintf(f, "%s\\\n", dir);                          /* 17: Path to GFILES */
  fprintf(f, "%s\\\n", dir);                          /* 18: Path to DATA */
  fprintf(f, "00000000.LOG\n");                       /* 19: Log file name */
  fprintf(f, "38400\n");                              /* 20: Baud rate */
  fprintf(f, "0\n");                                  /* 21: COM port (0=local) */
  fprintf(f, "%s %s\n", s->cfg.bbs_name[0] ? s->cfg.bbs_name : "Mutineer BBS", 
          "Mutineer");                                /* 22: BBS name */
  fprintf(f, "%s\n", s->cfg.sysop_name[0] ? s->cfg.sysop_name : "Sysop"); /* 23: Sysop name */
  fprintf(f, "%d\n", s->started_at ? (int)s->started_at : 0); /* 24: Time user logged on */
  fprintf(f, "%d\n", s->time_left_min * 60);          /* 25: Seconds remaining */
  fprintf(f, "%d\n", s->user.uploads);                /* 26: Uploads */
  fprintf(f, "%d\n", s->user.downloads);              /* 27: Downloads */
  fprintf(f, "0\n");                                  /* 28: Parity */
  fprintf(f, "0\n");                                  /* 29: DSZ log */
  fprintf(f, "0\n");                                  /* 30: Instance number */
  
  fclose(f);
  return true;
}

static bool write_pcboardsys(Session* s, const char* dir) {
  char path[512]; path_join(dir, "PCBOARD.SYS", path, sizeof(path));
  FILE* f = fopen(path, "wb");  /* Binary file */
  if (!f) return false;
  
  /* PCBoard 15.x PCBOARD.SYS format (128 bytes) */
  unsigned char buf[128];
  memset(buf, 0, sizeof(buf));
  
  /* Display (2 bytes) - offset 0 */
  buf[0] = (unsigned char)(-1);  /* -1 = local, 0+ = COM port */
  buf[1] = 0;
  
  /* Printer (2 bytes) - offset 2 */
  buf[2] = 0;
  buf[3] = 0;
  
  /* Page bell (2 bytes) - offset 4 */
  buf[4] = 0;
  buf[5] = 0;
  
  /* Caller alarm (2 bytes) - offset 6 */
  buf[6] = 0;
  buf[7] = 0;
  
  /* Sysop next (char) - offset 8 */
  buf[8] = ' ';
  
  /* Error correcting (char) - offset 9 */
  buf[9] = 'Y';
  
  /* Graphics (char) - offset 10 */
  buf[10] = 'Y';
  
  /* Node chat available (char) - offset 11 */
  buf[11] = 'Y';
  
  /* Baud rate (5 bytes) - offset 12 */
  memcpy(&buf[12], "38400", 5);
  
  /* User name (25 bytes) - offset 17 */
  strncpy((char*)&buf[17], s->user.handle, 25);
  
  /* First name (15 bytes) - offset 42 */
  strncpy((char*)&buf[42], s->user.handle, 15);
  
  /* Password (12 bytes) - offset 57 */
  strncpy((char*)&buf[57], "********", 12);           /* Password (masked) */
  
  /* User record number (2 bytes) - offset 69 */
  buf[69] = (unsigned char)(s->user.id & 0xFF);
  buf[70] = (unsigned char)((s->user.id >> 8) & 0xFF);
  
  /* Time on (2 bytes) - offset 71 */
  int time_on = (int)(time(NULL) - s->started_at) / 60;
  buf[71] = (unsigned char)(time_on & 0xFF);
  buf[72] = (unsigned char)((time_on >> 8) & 0xFF);
  
  /* Time logged on (5 bytes HH:MM) - offset 73 */
  time_t lt = s->started_at;
  struct tm* tm = localtime(&lt);
  if (tm) {
    snprintf((char*)&buf[73], 6, "%02d:%02d", tm->tm_hour, tm->tm_min);
  }
  
  /* Time limit (2 bytes) - offset 78 */
  buf[78] = (unsigned char)(s->time_left_min & 0xFF);
  buf[79] = (unsigned char)((s->time_left_min >> 8) & 0xFF);
  
  /* Node number (2 bytes) - offset 80 */
  buf[80] = (unsigned char)(s->node_num & 0xFF);
  buf[81] = (unsigned char)((s->node_num >> 8) & 0xFF);
  
  /* Event time (5 bytes HH:MM) - offset 82 */
  memcpy(&buf[82], "00:00", 5);
  
  /* Event active (char) - offset 87 */
  buf[87] = 'N';
  
  /* Event slide (char) - offset 88 */
  buf[88] = 'N';
  
  /* Memorized message (4 bytes) - offset 89 */
  buf[89] = 0; buf[90] = 0; buf[91] = 0; buf[92] = 0;
  
  /* COM port (char) - offset 93 */
  buf[93] = '0';
  
  /* Reserved (2 bytes) - offset 94 */
  buf[94] = 0; buf[95] = 0;
  
  /* Use ANSI (char) - offset 96 */
  buf[96] = 'Y';
  
  /* Security level (2 bytes) - offset 97 */
  buf[97] = (unsigned char)(s->user.level & 0xFF);
  buf[98] = (unsigned char)((s->user.level >> 8) & 0xFF);
  
  /* Total calls (2 bytes) - offset 99 */
  buf[99] = (unsigned char)(s->user.logged_on & 0xFF);
  buf[100] = (unsigned char)((s->user.logged_on >> 8) & 0xFF);
  
  /* Page length (char) - offset 101 */
  buf[101] = 24;
  
  /* Expert mode (char) - offset 102 */
  buf[102] = 'N';
  
  /* Registered conferences (9 bytes) - offset 103 */
  memset(&buf[103], 0xFF, 9);
  
  /* Date last on (8 bytes) - offset 112 */
  strncpy((char*)&buf[112], s->user.last_login_at, 8);
  
  /* Time remaining (2 bytes) - offset 120 */
  buf[120] = (unsigned char)(s->time_left_min & 0xFF);
  buf[121] = (unsigned char)((s->time_left_min >> 8) & 0xFF);
  
  /* Time remaining (2 bytes, again) - offset 122 */
  buf[122] = (unsigned char)(s->time_left_min & 0xFF);
  buf[123] = (unsigned char)((s->time_left_min >> 8) & 0xFF);
  
  /* Uploads (2 bytes) - offset 124 */
  buf[124] = (unsigned char)(s->user.uploads & 0xFF);
  buf[125] = (unsigned char)((s->user.uploads >> 8) & 0xFF);
  
  /* Downloads (2 bytes) - offset 126 */
  buf[126] = (unsigned char)(s->user.downloads & 0xFF);
  buf[127] = (unsigned char)((s->user.downloads >> 8) & 0xFF);
  
  fwrite(buf, 1, 128, f);
  fclose(f);
  return true;
}

static bool write_dorinfo(Session* s, const char* dir) {
  char path[512]; path_join(dir, "DORINFO1.DEF", path, sizeof(path));
  FILE* f = fopen(path, "w");
  if (!f) return false;
  
  /* DORINFO1.DEF format */
  fprintf(f, "%s\n", s->cfg.bbs_name[0] ? s->cfg.bbs_name : "Mutineer BBS"); /* 1: BBS name */
  fprintf(f, "%s\n", s->cfg.sysop_name[0] ? s->cfg.sysop_name : "Sysop");    /* 2: Sysop first name */
  fprintf(f, "\n");                                                           /* 3: Sysop last name */
  fprintf(f, "COM0\n");                                                       /* 4: COM port */
  fprintf(f, "38400 BAUD,N,8,1\n");                                           /* 5: Baud/parity */
  fprintf(f, "0\n");                                                          /* 6: Network type */
  fprintf(f, "%s\n", s->user.handle);                                         /* 7: User first name */
  fprintf(f, "\n");                                                           /* 8: User last name */
  fprintf(f, "%s\n", s->user.street[0] ? s->user.street : "Unknown");         /* 9: City/State */
  fprintf(f, "1\n");                                                          /* 10: ANSI (0/1/2) */
  fprintf(f, "%d\n", s->user.level);                                          /* 11: Security level */
  fprintf(f, "%d\n", s->time_left_min);                                       /* 12: Minutes remaining */
  
  fclose(f);
  return true;
}

static bool write_callinfo(Session* s, const char* dir) {
  char path[512]; path_join(dir, "CALLINFO.BBS", path, sizeof(path));
  FILE* f = fopen(path, "w");
  if (!f) return false;
  
  /* CALLINFO.BBS format (Wildcat style) */
  fprintf(f, "%s\n", s->user.handle);                 /* 1: User name */
  fprintf(f, "%d\n", 1);                              /* 2: Speed (1=local) */
  fprintf(f, "%s\n", s->user.street[0] ? s->user.street : "Unknown"); /* 3: City */
  fprintf(f, "%d\n", s->user.level);                  /* 4: Security level */
  fprintf(f, "%d\n", s->time_left_min);               /* 5: Minutes remaining */
  fprintf(f, "COLOR\n");                              /* 6: ANSI/COLOR/MONO */
  fprintf(f, "********\n");                           /* 7: Password (masked) */
  fprintf(f, "%d\n", s->user.id);                     /* 8: User record number */
  fprintf(f, "%d\n", s->started_at ? (int)s->started_at : 0); /* 9: Time logged on */
  fprintf(f, "%d\n", s->user.logged_on);              /* 10: Total calls */
  fprintf(f, "%d\n", s->user.msg_post);               /* 11: Messages posted */
  fprintf(f, "%s\n", s->user.last_login_at);          /* 12: Last call date */
  fprintf(f, "24\n");                                 /* 13: Screen lines */
  fprintf(f, "Y\n");                                  /* 14: Expert mode */
  fprintf(f, "ABCDEFGHIJKLMNOPQRSTUVWXYZ\n");         /* 15: Conferences */
  fprintf(f, "%d\n", s->current_conf);                /* 16: Current conference */
  fprintf(f, "%s\n", s->user.expires_at);             /* 17: Expiration date */
  fprintf(f, "%d\n", s->node_num);                    /* 18: Node number */
  fprintf(f, "%s\n", s->user.phone[0] ? s->user.phone : "000-000-0000"); /* 19: Home phone */
  fprintf(f, "%s\n", s->user.phone[0] ? s->user.phone : "000-000-0000"); /* 20: Work phone */
  fprintf(f, "%s\n", s->user.birth_date);             /* 21: Birth date */
  fprintf(f, "%d\n", s->user.uploads);                /* 22: Uploads */
  fprintf(f, "%d\n", s->user.downloads);              /* 23: Downloads */
  fprintf(f, "%d\n", s->user.uk);                     /* 24: Upload K */
  fprintf(f, "%d\n", s->user.dk);                     /* 25: Download K */
  
  fclose(f);
  return true;
}

static bool write_sfdoors(Session* s, const char* dir) {
  char path[512]; path_join(dir, "SFDOORS.DAT", path, sizeof(path));
  FILE* f = fopen(path, "w");
  if (!f) return false;
  
  /* SFDOORS.DAT format (Spitfire style) */
  fprintf(f, "%d\n", s->user.id);                     /* 1: User number */
  fprintf(f, "%s\n", s->user.handle);                 /* 2: User name */
  fprintf(f, "********\n");                           /* 3: Password (masked) */
  fprintf(f, "1\n");                                  /* 4: Graphics (1=ANSI) */
  fprintf(f, "%d\n", s->time_left_min);               /* 5: Minutes remaining */
  fprintf(f, "%s\n", s->user.real_name[0] ? s->user.real_name : s->user.handle); /* 6: Real name */
  fprintf(f, "%d\n", s->user.level);                  /* 7: Security level */
  fprintf(f, "%s\n", s->user.birth_date);             /* 8: Birth date */
  fprintf(f, "%s\n", s->user.phone[0] ? s->user.phone : "000-000-0000"); /* 9: Phone */
  fprintf(f, "%s\n", s->user.street[0] ? s->user.street : "Unknown"); /* 10: City */
  fprintf(f, "0\n");                                  /* 11: COM port */
  fprintf(f, "38400\n");                              /* 12: Baud rate */
  fprintf(f, "%d\n", s->node_num);                    /* 13: Node number */
  
  fclose(f);
  return true;
}

/* =========================================================================
 * Native door launcher (original behavior)
 * ========================================================================= */

static bool door_launch_native(Session* s, const DbDoor* door) {
  char dir[512];
  path_join(s->cfg.dropfile_path, door->name, dir, sizeof(dir));
  ensure_dir(s->cfg.dropfile_path);
  ensure_dir(dir);

  write_doorsys(s, dir);
  write_door32sys(s, dir);
  write_dorinfo(s, dir);
  write_chaintxt(s, dir);
  write_pcboardsys(s, dir);
  write_callinfo(s, dir);
  write_sfdoors(s, dir);

  char cmd[1024];
  if (door->workdir[0])
    snprintf(cmd, sizeof(cmd), "cd '%s' && %s", door->workdir, door->command);
  else
    snprintf(cmd, sizeof(cmd), "%s", door->command);

  log_info("launching native door %s: %s", door->name, cmd);
  int rc = system(cmd);
  if (rc != 0) log_warn("native door %s exited with code %d", door->name, rc);
  return rc == 0;
}

/* =========================================================================
 * DOSBox door launcher
 * ========================================================================= */

static void dosbox_write_dropfile(Session* s, const DbDoor* door,
                                  const DosboxManifest* m,
                                  const char* runtime_root) {
  char dest[1024];
  if (m->dropfile_dest[0])
    snprintf(dest, sizeof(dest), "%s/%s", runtime_root, m->dropfile_dest);
  else
    snprintf(dest, sizeof(dest), "%s/game", runtime_root);
  ensure_dir(dest);

  const char* fmt = m->dropfile[0] ? m->dropfile : door->dropfile;
  if (!fmt || !fmt[0]) fmt = "DOOR.SYS";

  /* Always write the primary format to the destination */
  if (strcasecmp(fmt, "DOOR.SYS") == 0)       write_doorsys(s, dest);
  else if (strcasecmp(fmt, "DOOR32.SYS") == 0) write_door32sys(s, dest);
  else if (strcasecmp(fmt, "DORINFO1.DEF") == 0) write_dorinfo(s, dest);
  else if (strcasecmp(fmt, "CHAIN.TXT") == 0)  write_chaintxt(s, dest);
  else if (strcasecmp(fmt, "PCBOARD.SYS") == 0) write_pcboardsys(s, dest);
  else if (strcasecmp(fmt, "CALLINFO.BBS") == 0) write_callinfo(s, dest);
  else if (strcasecmp(fmt, "SFDOORS.DAT") == 0) write_sfdoors(s, dest);
  else {
    /* Unknown format: write DOOR.SYS as fallback */
    log_warn("door %s: unknown dropfile format '%s', falling back to DOOR.SYS", door->name, fmt);
    write_doorsys(s, dest);
  }
}

static bool door_launch_dosbox(Session* s, const DbDoor* door) {
  if (!s->cfg.dosbox_path[0]) {
    log_error("door %s: dosbox_path not configured", door->name);
    send_str(s, "\r\nDOSBox is not configured on this system. Contact the sysop.\r\n");
    return false;
  }
  if (!door->manifest[0]) {
    log_error("door %s: no manifest path set", door->name);
    send_str(s, "\r\nDoor manifest is missing. Contact the sysop.\r\n");
    return false;
  }

  char errbuf[256];
  DosboxManifest m;
  if (!dosbox_manifest_parse(door->manifest, &m, errbuf, sizeof(errbuf))) {
    log_error("door %s: manifest parse failed: %s", door->name, errbuf);
    send_str(s, "\r\nDoor configuration error. Contact the sysop.\r\n");
    return false;
  }
  if (!dosbox_manifest_validate(&m, errbuf, sizeof(errbuf))) {
    log_error("door %s: manifest invalid: %s", door->name, errbuf);
    send_str(s, "\r\nDoor configuration error. Contact the sysop.\r\n");
    return false;
  }

  /* Determine timeout */
  int timeout_sec = door->timeout_sec > 0 ? door->timeout_sec
                  : m.timeout_sec > 0      ? m.timeout_sec
                  : s->cfg.door_default_timeout_sec;

  /* Generate a unique launch ID */
  char launch_id[64];
  snprintf(launch_id, sizeof(launch_id), "%ld_%d", (long)time(NULL), (int)getpid());

  /* Use manifest name, fallback to door name */
  if (!m.name[0]) snprintf(m.name, sizeof(m.name), "%s", door->name);

  /* Create runtime tree */
  char runtime_root[1024];
  const char* runtime_base = s->cfg.door_runtime_path[0]
                             ? s->cfg.door_runtime_path
                             : "data/door_runtime";
  if (!dosbox_prepare_runtime(&m, runtime_base, s->node_num, launch_id,
                              runtime_root, sizeof(runtime_root),
                              errbuf, sizeof(errbuf))) {
    log_error("door %s: runtime prep failed: %s", door->name, errbuf);
    send_str(s, "\r\nFailed to prepare door runtime. Contact the sysop.\r\n");
    return false;
  }
  log_info("door %s: runtime tree at %s", door->name, runtime_root);

  /* Write primary dropfile into runtime tree */
  dosbox_write_dropfile(s, door, &m, runtime_root);

  /* Generate DOSBox config */
  char game_dir[1024], conf_path[1024];
  snprintf(game_dir,  sizeof(game_dir),  "%s/game",       runtime_root);
  snprintf(conf_path, sizeof(conf_path), "%s/dosbox.conf", runtime_root);

  if (!dosbox_build_conf(&m, game_dir, conf_path, errbuf, sizeof(errbuf))) {
    log_error("door %s: conf generation failed: %s", door->name, errbuf);
    send_str(s, "\r\nFailed to configure DOSBox. Contact the sysop.\r\n");
    dosbox_cleanup_runtime(runtime_root);
    return false;
  }

  log_info("door %s: launching dosbox pid conf=%s socket_fd=%d timeout=%ds",
           door->name, conf_path, s->fd, timeout_sec);

  /* Update node activity */
  char activity[80];
  snprintf(activity, sizeof(activity), "door:%s", door->name);
  db_node_upsert(s->db, s->node_num, s->user.id, "online", activity, s->ip);

  /* Build argv for execv */
  char fd_str[16];
  snprintf(fd_str, sizeof(fd_str), "%d", s->fd);
  char* argv[8];
  int argc = 0;
  argv[argc++] = s->cfg.dosbox_path;
  argv[argc++] = "-conf";
  argv[argc++] = conf_path;
  argv[argc++] = "-socket";
  argv[argc++] = fd_str;
  argv[argc++] = "-exit";
  argv[argc]   = NULL;

  pid_t pid = fork();
  if (pid < 0) {
    log_error("door %s: fork failed: %s", door->name, strerror(errno));
    send_str(s, "\r\nFailed to launch DOSBox. Contact the sysop.\r\n");
    db_node_upsert(s->db, s->node_num, s->user.id, "online", "menu", s->ip);
    dosbox_cleanup_runtime(runtime_root);
    return false;
  }

  if (pid == 0) {
    /* Child: clear CLOEXEC on the telnet socket so DOSBox inherits it */
    int flags = fcntl(s->fd, F_GETFD);
    if (flags >= 0) fcntl(s->fd, F_SETFD, flags & ~FD_CLOEXEC);

    /* Redirect stdout/stderr to logs dir */
    char log_path[1024];
    snprintf(log_path, sizeof(log_path), "%s/logs/dosbox.log", runtime_root);
    int log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd >= 0) {
      dup2(log_fd, STDOUT_FILENO);
      dup2(log_fd, STDERR_FILENO);
      close(log_fd);
    }

    execv(s->cfg.dosbox_path, argv);
    /* If execv fails, write to stderr (now the log file) and exit */
    fprintf(stderr, "execv failed: %s\n", strerror(errno));
    _exit(127);
  }

  /* Parent: supervise child with timeout */
  time_t start = time(NULL);
  bool timed_out = false;
  int status = 0;

  while (1) {
    pid_t w = waitpid(pid, &status, WNOHANG);
    if (w == pid) break;
    if (w < 0 && errno != EINTR) break;

    sleep(1);

    if (timeout_sec > 0 && difftime(time(NULL), start) >= (double)timeout_sec) {
      log_warn("door %s: timeout after %ds, terminating dosbox pid %d",
               door->name, timeout_sec, (int)pid);
      send_str(s, "\r\n\r\nDoor session timed out.\r\n");
      kill(pid, SIGTERM);
      sleep(2);
      if (waitpid(pid, &status, WNOHANG) != pid) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
      }
      timed_out = true;
      break;
    }
  }

  bool success = !timed_out && WIFEXITED(status) && WEXITSTATUS(status) == 0;

  if (WIFEXITED(status))
    log_info("door %s: dosbox exited with code %d", door->name, WEXITSTATUS(status));
  else if (WIFSIGNALED(status))
    log_warn("door %s: dosbox killed by signal %d", door->name, WTERMSIG(status));

  /* Restore node activity */
  db_node_upsert(s->db, s->node_num, s->user.id, "online", "menu", s->ip);

  /* Cleanup */
  bool do_cleanup = success
    ? (m.cleanup_on_exit || s->cfg.door_cleanup_on_exit)
    : !s->cfg.door_keep_failed_runs;

  if (do_cleanup) {
    dosbox_cleanup_runtime(runtime_root);
  } else {
    log_info("door %s: keeping runtime tree at %s", door->name, runtime_root);
  }

  return success;
}

/* =========================================================================
 * Dispatcher
 * ========================================================================= */

bool door_launch(Session* s, const DbDoor* door) {
  if (!s || !door) return false;

  if (!door->enabled) {
    log_warn("door %s is disabled", door->name);
    send_str(s, "\r\nThis door is currently disabled.\r\n");
    return false;
  }

  if (strcasecmp(door->runner, "dosbox") == 0) {
    return door_launch_dosbox(s, door);
  }

  /* Default: native runner */
  if (!door->command[0]) {
    log_error("door %s: no command configured for native runner", door->name);
    return false;
  }
  return door_launch_native(s, door);
}

bool protocol_launch(Session* s, const DbProtocol* proto, const char* filepath, const char* direction) {
  if (!s || !proto || !filepath) return false;
  
  log_info("launch protocol %s (%s): %s", proto->name, direction, filepath);
  
  pid_t pid = fork();
  if (pid < 0) {
    log_error("fork failed for protocol");
    return false;
  }
  
  if (pid == 0) {
    /* Child process - redirect socket to stdin/stdout */
    dup2(s->fd, STDIN_FILENO);
    dup2(s->fd, STDOUT_FILENO);
    dup2(s->fd, STDERR_FILENO);
    
    /* Close original fd */
    if (s->fd > STDERR_FILENO) close(s->fd);
    
    /* Build command with filepath substitution */
    char cmd[1024];
    const char* base_cmd = proto->command;
    
    /* Check if command contains %f placeholder for filepath */
    if (strstr(base_cmd, "%f")) {
      /* Replace %f with filepath */
      char* p = cmd;
      char* end = cmd + sizeof(cmd) - 1;
      while (*base_cmd && p < end) {
        if (base_cmd[0] == '%' && base_cmd[1] == 'f') {
          size_t flen = strlen(filepath);
          if (p + flen < end) {
            memcpy(p, filepath, flen);
            p += flen;
          }
          base_cmd += 2;
        } else {
          *p++ = *base_cmd++;
        }
      }
      *p = '\0';
    } else {
      /* Append filepath as argument */
      snprintf(cmd, sizeof(cmd), "%s '%s'", proto->command, filepath);
    }
    
    /* Execute via shell */
    execl("/bin/sh", "sh", "-c", cmd, (char*)NULL);
    _exit(127);
  }
  
  /* Parent - wait for child */
  int status;
  waitpid(pid, &status, 0);
  
  if (WIFEXITED(status)) {
    int rc = WEXITSTATUS(status);
    if (rc == 0) {
      log_info("protocol %s completed successfully", proto->name);
      return true;
    } else {
      log_error("protocol %s exited with code %d", proto->name, rc);
      return false;
    }
  }
  
  log_error("protocol %s terminated abnormally", proto->name);
  return false;
}
