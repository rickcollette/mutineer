/*
 * mutineer-validate - Menu and template validation tool
 * 
 * Validates all .mnu files for duplicate keys and all template files
 * for structural correctness. Returns non-zero on any fatal error.
 * 
 * Usage: mutineer-validate [options] [menu_dir]
 *   -q, --quiet    Suppress non-error output
 *   -v, --verbose  Show all validation details
 *   -h, --help     Show this help
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "bbs_menu.h"
#include "bbs_menu_template.h"

static int g_verbose = 0;
static int g_quiet = 0;
static int g_errors = 0;
static int g_warnings = 0;

static void print_usage(const char* prog) {
  fprintf(stderr, "Usage: %s [options] [menu_dir]\n", prog);
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  -q, --quiet    Suppress non-error output\n");
  fprintf(stderr, "  -v, --verbose  Show all validation details\n");
  fprintf(stderr, "  -h, --help     Show this help\n");
  fprintf(stderr, "\nValidates all .mnu files for duplicate keys and all\n");
  fprintf(stderr, ".ans/.asc templates for structural correctness.\n");
}

static void log_error(const char* file, const char* msg) {
  fprintf(stderr, "ERROR: %s: %s\n", file, msg);
  g_errors++;
}

static void log_warn(const char* file, const char* msg) {
  fprintf(stderr, "WARN: %s: %s\n", file, msg);
  g_warnings++;
}

static void log_info(const char* file, const char* msg) {
  if (!g_quiet) {
    printf("OK: %s: %s\n", file, msg);
  }
}

static int validate_menu_file(const char* path) {
  Menu m;
  if (!menu_load(path, &m)) {
    log_error(path, "failed to load menu file");
    return 1;
  }
  
  MenuKeyValidation kv = validate_menu_keys(&m);
  if (!kv.valid) {
    char msg[256];
    snprintf(msg, sizeof(msg), "duplicate key '%s' used by '%s' and '%s'",
             kv.dup_key, kv.label1, kv.label2);
    log_error(path, msg);
    menu_free(&m);
    return 1;
  }
  
  if (g_verbose) {
    char msg[64];
    snprintf(msg, sizeof(msg), "valid (%zu items)", m.count);
    log_info(path, msg);
  }
  
  menu_free(&m);
  return 0;
}

static int validate_template_file(const char* path) {
  size_t len = 0;
  char* content = slurp_file(path, &len);
  if (!content) {
    log_error(path, "failed to read template file");
    return 1;
  }
  
  bool is_asc = strstr(path, ".asc") != NULL;
  TemplateValidation tv = validate_menu_template(content, true, is_asc);
  free(content);
  
  if (tv.code != TMPL_OK) {
    log_error(path, tv.detail);
    return 1;
  }
  
  if (g_verbose) {
    log_info(path, "valid template");
  }
  
  return 0;
}

static int validate_directory(const char* dir) {
  DIR* d = opendir(dir);
  if (!d) {
    fprintf(stderr, "ERROR: cannot open directory: %s\n", dir);
    return 1;
  }
  
  int menu_count = 0;
  int template_count = 0;
  int local_errors = 0;
  
  struct dirent* ent;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.') continue;
    
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
    
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    
    const char* ext = strrchr(ent->d_name, '.');
    if (!ext) continue;
    
    if (strcmp(ext, ".mnu") == 0) {
      menu_count++;
      local_errors += validate_menu_file(path);
    } else if (strcmp(ext, ".ans") == 0 || strcmp(ext, ".asc") == 0) {
      template_count++;
      local_errors += validate_template_file(path);
    }
  }
  
  closedir(d);
  
  if (!g_quiet) {
    printf("\nValidated %d menu files, %d template files\n", menu_count, template_count);
  }
  
  return local_errors;
}

static int check_template_coverage(const char* dir) {
  /* Check that each .mnu has both .ans and .asc templates */
  DIR* d = opendir(dir);
  if (!d) return 0;
  
  char menus[64][32];
  int menu_count = 0;
  
  struct dirent* ent;
  while ((ent = readdir(d)) != NULL && menu_count < 64) {
    const char* ext = strrchr(ent->d_name, '.');
    if (ext && strcmp(ext, ".mnu") == 0) {
      size_t name_len = (size_t)(ext - ent->d_name);
      if (name_len < 32) {
        memcpy(menus[menu_count], ent->d_name, name_len);
        menus[menu_count][name_len] = '\0';
        menu_count++;
      }
    }
  }
  closedir(d);
  
  int missing = 0;
  for (int i = 0; i < menu_count; i++) {
    char ans_path[512], asc_path[512];
    snprintf(ans_path, sizeof(ans_path), "%.470s/%.31s.ans", dir, menus[i]);
    snprintf(asc_path, sizeof(asc_path), "%.470s/%.31s.asc", dir, menus[i]);
    
    if (!file_exists(ans_path)) {
      char msg[128];
      snprintf(msg, sizeof(msg), "missing ANSI template for menu '%.31s'", menus[i]);
      log_warn(dir, msg);
      missing++;
    }
    if (!file_exists(asc_path)) {
      char msg[128];
      snprintf(msg, sizeof(msg), "missing ASCII template for menu '%.31s'", menus[i]);
      log_warn(dir, msg);
      missing++;
    }
  }
  
  return missing;
}

int main(int argc, char** argv) {
  const char* menu_dir = "menus";
  
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
      g_quiet = 1;
    } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
      g_verbose = 1;
    } else if (argv[i][0] != '-') {
      menu_dir = argv[i];
    } else {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      print_usage(argv[0]);
      return 1;
    }
  }
  
  if (!g_quiet) {
    printf("Mutineer Menu/Template Validator\n");
    printf("Validating directory: %s\n\n", menu_dir);
  }
  
  validate_directory(menu_dir);
  check_template_coverage(menu_dir);
  
  if (!g_quiet) {
    printf("\nSummary: %d errors, %d warnings\n", g_errors, g_warnings);
  }
  
  return g_errors > 0 ? 1 : 0;
}
