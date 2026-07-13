#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include "bbs_config.h"
#include "bbs_db.h"
#include "bbs_log.h"
#include "bbs_hash.h"

static bool dir_exists(const char* path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool file_exists(const char* path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool startup_sanity_check(const BbsConfig* cfg) {
  bool ok = true;
  int missing_count = 0;
  
  log_info("Running startup sanity checks...");
  
  /* Check required directories - report but don't create */
  if (!dir_exists(cfg->data_path)) {
    log_error("Missing directory: %s", cfg->data_path);
    fprintf(stderr, "  [MISSING] %s\n", cfg->data_path);
    missing_count++;
    ok = false;
  }
  
  if (!dir_exists(cfg->art_path)) {
    log_error("Missing directory: %s", cfg->art_path);
    fprintf(stderr, "  [MISSING] %s\n", cfg->art_path);
    missing_count++;
    ok = false;
  }
  
  /* Extract logs directory from logs_path */
  char logs_dir[256];
  snprintf(logs_dir, sizeof(logs_dir), "%s", cfg->logs_path);
  char* logs_slash = strrchr(logs_dir, '/');
  if (logs_slash) {
    *logs_slash = '\0';
    if (!dir_exists(logs_dir)) {
      log_error("Missing directory: %s", logs_dir);
      fprintf(stderr, "  [MISSING] %s\n", logs_dir);
      missing_count++;
      ok = false;
    }
  }
  
  /* Extract menu directory from menu_main path */
  char menu_dir[256];
  snprintf(menu_dir, sizeof(menu_dir), "%s", cfg->menu_main);
  char* last_slash = strrchr(menu_dir, '/');
  if (last_slash) {
    *last_slash = '\0';
    if (!dir_exists(menu_dir)) {
      log_error("Missing directory: %s", menu_dir);
      fprintf(stderr, "  [MISSING] %s\n", menu_dir);
      missing_count++;
      ok = false;
    }
  }
  
  /* Check for files subdirectory */
  char files_path[512];
  snprintf(files_path, sizeof(files_path), "%s/files", cfg->data_path);
  if (!dir_exists(files_path)) {
    log_error("Missing directory: %s", files_path);
    fprintf(stderr, "  [MISSING] %s\n", files_path);
    missing_count++;
    ok = false;
  }
  
  /* Check schema file */
  if (!file_exists("sql/schema.sql")) {
    log_error("Missing file: sql/schema.sql");
    fprintf(stderr, "  [MISSING] sql/schema.sql\n");
    missing_count++;
    ok = false;
  }

  if (!file_exists("sql/plank_schema.sql")) {
    log_error("Missing file: sql/plank_schema.sql");
    fprintf(stderr, "  [MISSING] sql/plank_schema.sql\n");
    missing_count++;
    ok = false;
  }
  
  /* Check main menu exists */
  if (!file_exists(cfg->menu_main)) {
    log_warn("Missing file: %s", cfg->menu_main);
    fprintf(stderr, "  [MISSING] %s\n", cfg->menu_main);
    missing_count++;
    ok = false;
  }
  
  if (ok) {
    log_info("Sanity checks passed");
  } else {
    log_error("Sanity checks failed - %d item(s) missing", missing_count);
    fprintf(stderr, "\n%d required item(s) missing.\n", missing_count);
    fprintf(stderr, "Run 'mutineer-initbbs' to create missing files and directories.\n");
    fprintf(stderr, "  mutineer-initbbs        # Interactive mode\n");
    fprintf(stderr, "  mutineer-initbbs -y     # Auto-create all\n");
  }
  
  return ok;
}

bool startup_init_database(BbsDb* db, const BbsConfig* cfg) {
  (void)cfg;
  if (!db) return false;
  
  log_info("Checking database...");
  
  /* Check if schema is applied by testing for users table */
  bool schema_applied = db_exec(db, "SELECT 1 FROM users LIMIT 1");
  
  if (!schema_applied) {
    log_error("Database schema not applied");
    fprintf(stderr, "  [MISSING] Database schema not initialized\n");
    fprintf(stderr, "\nRun 'mutineer-initbbs' to initialize the database.\n");
    return false;
  }
  
  /* Check if sysop user exists */
  DbUser sysop;
  if (!db_user_fetch(db, "sysop", &sysop)) {
    log_error("No sysop user found in database");
    fprintf(stderr, "  [MISSING] Sysop user not found\n");
    fprintf(stderr, "\nRun 'mutineer-initbbs' to create the sysop user.\n");
    return false;
  }

  if (!db_exec(db, "SELECT 1 FROM plank_node_identity LIMIT 1")) {
    log_error("PLANK schema not applied");
    fprintf(stderr, "  [MISSING] PLANK database schema not initialized\n");
    fprintf(stderr, "\nRun 'mutineer-initbbs' to initialize the database.\n");
    return false;
  }
  
  /* Check if at least one message area exists */
  DbMsgArea areas[1];
  int area_count = db_msg_area_list(db, areas, 1);
  if (area_count == 0) {
    log_error("No message areas found in database");
    fprintf(stderr, "  [MISSING] No message areas configured\n");
    fprintf(stderr, "\nRun 'mutineer-initbbs' to create default message areas.\n");
    return false;
  }
  
  /* Ensure system tables have required rows (these are safe to auto-create) */
  db_exec(db, "INSERT OR IGNORE INTO system_info (id, bbs_name, sysop_name) VALUES (1, 'Mutineer BBS', 'Sysop')");
  db_exec(db, "INSERT OR IGNORE INTO stats (id) VALUES (1)");
  db_exec(db, "INSERT OR IGNORE INTO daily_stats (id) VALUES (1)");
  db_exec(db, "INSERT OR IGNORE INTO automsg (id, msg) VALUES (1, 'Welcome to Mutineer BBS!')");
  
  log_info("Database check complete");
  return true;
}
