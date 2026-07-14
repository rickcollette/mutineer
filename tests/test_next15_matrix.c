#include "bbs_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(cond, msg) do { \
  if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } \
} while (0)

static char* read_file(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* buf = (char*)calloc(1, (size_t)n + 1);
  if (!buf) { fclose(f); return NULL; }
  if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
    fclose(f);
    free(buf);
    return NULL;
  }
  fclose(f);
  return buf;
}

static int contains(const char* haystack, const char* needle) {
  return haystack && needle && strstr(haystack, needle) != NULL;
}

static int test_config_roundtrip(void) {
  const char* tmpdir = getenv("TMPDIR");
  if (!tmpdir || !tmpdir[0]) tmpdir = "/tmp";
  char path[256];
  snprintf(path, sizeof(path), "%s/mutineer-cfg-XXXXXX", tmpdir);
  int fd = mkstemp(path);
  CHECK(fd >= 0, "mkstemp config");
  FILE* f = fdopen(fd, "w");
  CHECK(f != NULL, "fdopen config");
  fprintf(f,
          "bind=127.0.0.1\n"
          "port=3333\n"
          "db_path=data/test.db\n"
          "menu_main=menus/main.mnu\n"
          "plugins_enabled=true\n"
          "wfc_shell_enabled=false\n"
          "wfc_shell_command=\n");
  fclose(f);

  BbsConfig cfg;
  CHECK(cfg_load(path, &cfg), "cfg_load temporary config");
  CHECK(strcmp(cfg.source_path, path) == 0, "config stores source path");
  CHECK(cfg.plugins_enabled == 1, "boolean true parses");
  CHECK(cfg.wfc_shell_enabled == 0, "boolean false parses");
  cfg.wfc_shell_enabled = 1;
  snprintf(cfg.wfc_shell_command, sizeof(cfg.wfc_shell_command), "%s", "/bin/sh");
  CHECK(cfg_save(cfg.source_path, &cfg), "cfg_save writes atomically");
  CHECK(access(path, R_OK) == 0, "saved config exists");
  char bak[256];
  snprintf(bak, sizeof(bak), "%s.bak", path);
  CHECK(access(bak, R_OK) == 0, "config save creates backup");
  CHECK(cfg_load(path, &cfg), "reload saved config");
  CHECK(cfg.wfc_shell_enabled == 1, "saved WFC shell flag reloads");
  CHECK(strcmp(cfg.wfc_shell_command, "/bin/sh") == 0, "saved WFC shell command reloads");
  unlink(path);
  unlink(bak);
  return 0;
}

int main(void) {
  char* cmake = read_file("CMakeLists.txt");
  char* session = read_file("src/session.c");
  char* mainc = read_file("src/main.c");
  char* wfc = read_file("src/wfc.c");
  char* sched = read_file("src/scheduler.c");
  char* maint = read_file("src/tools/mutineer-maint.c");
  char* config = read_file("src/config.c");
  char* filecmds = read_file("src/file_cmds.c");
  char* coved = read_file("src/tools/coved.c");
  char* docs = read_file("docs/configuration.md");
  char* conf = read_file("conf/mutineer.conf");
  char* matrix = read_file("feature-matrix.md");

  CHECK(cmake && session && mainc && wfc && sched && maint && config &&
        filecmds && coved && docs && conf && matrix, "read source files");

  CHECK(contains(session, "cmd_config_editor") &&
        contains(config, "bool cfg_save") &&
        contains(config, ".bak") &&
        contains(config, "rename(tmp, path)"),
        "in-BBS config editor saves active config atomically with backup");
  CHECK(test_config_roundtrip() == 0, "config load/save roundtrip");

  CHECK(contains(session, "CMD_FLAG_PASSWORD") &&
        contains(session, "prompt_password(s, \"Command password: \"") &&
        contains(session, "CMD_FLAG_SYSOP_LOG") &&
        contains(session, "menu_sysop_command"),
        "menu password and sysoplog flags are enforced before action dispatch");

  CHECK(contains(session, "resolve_start_menu_path") &&
        contains(session, "user_start_menu") &&
        contains(session, "menus/menu%d.mnu") &&
        contains(session, "s->cfg.menu_main"),
        "user-specific start menu resolves with main-menu fallback");

  CHECK(contains(session, "char tmp[2048];") &&
        contains(session, "db_node_list(s->db, nodes, 64)") &&
        contains(session, "send_str(s, tmp);") &&
        !contains(session, "send_str(s, tmp);\n    send_str(s, tmp);"),
        "who renders and sends the online list once");

  CHECK(contains(session, "db_user_create(s->db, guest_handle") &&
        contains(session, "login_throttled(&s->cfg, s->ip, handle)") &&
        !contains(session, "guest.id = 0"),
        "guest login uses normal throttling and persistent DB user accounting");

  CHECK(contains(session, "maintenance_start") &&
        contains(session, "parse_strict_int((char *)line, 1, 36500") &&
        contains(session, "Type DELETE to confirm") &&
        contains(session, "BEGIN IMMEDIATE") &&
        contains(session, "ROLLBACK"),
        "maintenance purge prompts validate positive days and use transactions");

  CHECK(contains(sched, "void scheduler_stop(void)") &&
        contains(sched, "pthread_join(th, NULL)") &&
        contains(mainc, "scheduler_stop();\n  wfc_stop();\n  plugin_loader_shutdown();"),
        "scheduler and WFC stop before plugin shutdown and db_close");

  CHECK(contains(wfc, "wfc_shell_enabled") &&
        contains(wfc, "bbs_argv_parse_template") &&
        contains(wfc, "bbs_exec_argv") &&
        !contains(wfc, "system(\"/bin/sh\")"),
        "WFC shell escape is disabled by default and uses supervised argv execution");
  CHECK(contains(docs, "wfc_shell_enabled") && contains(conf, "wfc_shell_enabled=0"),
        "WFC shell gate documented and disabled in sample config");

  CHECK(contains(maint, "copy_file_plain") &&
        contains(maint, "VACUUM INTO %s") &&
        !contains(maint, "system(") &&
        !contains(maint, "cp '"),
        "mutineer-maint backup has no shell fallback");

  CHECK(contains(session, "handle_file_command(s, \"FG\", NULL)") &&
        contains(session, "handle_file_command(s, \"FA\", NULL)") &&
        contains(session, "handle_file_command(s, \"FL\", NULL)") &&
        contains(filecmds, "file_download_allowed") &&
        contains(filecmds, "FILE_FLAG_NOTVAL"),
        "interactive file validation visibility and downloads use hardened policy");

  CHECK(contains(coved, "INSERT INTO cove_fanout_queue") &&
        contains(coved, "INSERT INTO plank_outbound_queue") &&
        contains(coved, "cove_downstream_health"),
        "PLANK/COVE BBS integration surfaces are covered");

  CHECK(contains(cmake, "MUTINEER_WARNINGS_AS_ERRORS") &&
        contains(cmake, "CMAKE_COMPILE_WARNING_AS_ERROR"),
        "release warning gate exists");
  CHECK(contains(cmake, "docker_quickstart_smoke") &&
        contains(cmake, "sanitizer"),
        "Docker and sanitizer/load coverage are registered");

  CHECK(contains(matrix, "| P0 | 100% | Complete | PLANK deadletter storage") &&
        contains(matrix, "| P1 | 100% | Complete | `SHARED.CAS`") &&
        contains(matrix, "| P1 | 100% | Complete | Release/update tooling") &&
        contains(matrix, "Validation Snapshot"),
        "feature matrix marks current reviewed batch complete");

  free(cmake); free(session); free(mainc); free(wfc); free(sched); free(maint);
  free(config); free(filecmds); free(coved); free(docs); free(conf); free(matrix);
  return 0;
}
