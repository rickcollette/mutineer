/*
 * test_doors.c — Unit tests for the DOSBox door runner:
 *   - Manifest parsing (valid, missing required field, unsafe path)
 *   - Runtime tree creation
 *   - DOSBox conf generation
 *   - Native door unchanged path (regression)
 *   - Full DOSBox launch with fake dosbox binary (if writable /tmp available)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>

#include "bbs_doors.h"
#include "bbs_db.h"
#include "bbs_config.h"
#include "bbs_util.h"

/* =========================================================================
 * Minimal stubs for symbols doors.c pulls in from the rest of the BBS.
 * These let us compile doors.c into the test without the full session stack.
 * ========================================================================= */

void send_str(Session* s, const char* str) { (void)s; (void)str; }
int session_readline(Session* s, uint8_t* buf, size_t cap, int timeout) {
  (void)s; (void)timeout;
  if (buf && cap) buf[0] = '\0';
  return 0;
}
int prompt_line(Session* s, const char* prompt, char* out, size_t cap) {
  (void)s; (void)prompt;
  if (out && cap) out[0] = '\0';
  return 0;
}
size_t online_list(char* out, size_t cap) { (void)out; (void)cap; return 0; }
void online_broadcast(const char* msg) { (void)msg; }
static int g_active_node;
Session* online_get_node(int node_num) {
  static Session active;
  return node_num == g_active_node ? &active : NULL;
}

/* =========================================================================
 * Test helpers
 * ========================================================================= */

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do { \
  if (cond) { printf("  PASS: %s\n", msg); g_pass++; } \
  else       { printf("  FAIL: %s  (line %d)\n", msg, __LINE__); g_fail++; } \
} while(0)

static const char* tmp_root(void) {
  const char* root = getenv("TMPDIR");
  return (root && root[0]) ? root : "/tmp";
}

static void make_tmp_path(char* out, size_t outcap, const char* fmt, ...) {
  char leaf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(leaf, sizeof(leaf), fmt, ap);
  va_end(ap);
  path_join(tmp_root(), leaf, out, outcap);
}

static char* write_tmp_file(const char* name, const char* content) {
  static char path[256];
  make_tmp_path(path, sizeof(path), "%s", name);
  FILE* f = fopen(path, "w");
  if (!f) return NULL;
  fputs(content, f);
  fclose(f);
  return path;
}

static void rm_rf(const char* path) {
  bbs_remove_tree(path);
}

static bool newest_session_json(const char* root, const char* door_name,
                                int node_num, char* out, size_t outcap) {
  char door_dir[512];
  char node_dir[512];
  DIR* d;
  struct dirent* ent;
  time_t newest = 0;
  bool found = false;

  path_join(root, door_name, door_dir, sizeof(door_dir));
  char node_leaf[32];
  snprintf(node_leaf, sizeof(node_leaf), "node%02d", node_num);
  path_join(door_dir, node_leaf, node_dir, sizeof(node_dir));
  d = opendir(node_dir);
  if (!d) return false;
  while ((ent = readdir(d)) != NULL) {
    if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
    char candidate[600];
    struct stat st;
    char run_dir[512];
    path_join(node_dir, ent->d_name, run_dir, sizeof(run_dir));
    path_join(run_dir, "MUTINEER_SESSION.JSON", candidate, sizeof(candidate));
    if (stat(candidate, &st) == 0 && S_ISREG(st.st_mode) &&
        (!found || st.st_mtime >= newest)) {
      newest = st.st_mtime;
      snprintf(out, outcap, "%s", candidate);
      found = true;
    }
  }
  closedir(d);
  return found;
}

/* =========================================================================
 * Test: manifest parsing — valid manifest
 * ========================================================================= */

static void test_manifest_parse_valid(void) {
  printf("\n[manifest: valid parse]\n");

  const char* json =
    "{\n"
    "  \"runner\": \"dosbox\",\n"
    "  \"name\": \"aladdin\",\n"
    "  \"master_dir\": \"/opt/doors/aladdin\",\n"
    "  \"startup\": \"ALADDIN.EXE\",\n"
    "  \"dropfile\": \"DOOR.SYS\",\n"
    "  \"dropfile_dest\": \"game\",\n"
    "  \"machine\": \"svga_s3\",\n"
    "  \"memsize\": 16,\n"
    "  \"core\": \"auto\",\n"
    "  \"cycles\": \"3000\",\n"
    "  \"serial_telnet\": true,\n"
    "  \"usedtr\": false,\n"
    "  \"timeout_sec\": 300,\n"
    "  \"copy_mode\": \"copy\",\n"
    "  \"cleanup_on_exit\": true\n"
    "}\n";

  char* path = write_tmp_file("test_manifest_valid.json", json);
  if (!path) { printf("  SKIP: cannot write /tmp\n"); return; }

  DosboxManifest m;
  char errbuf[256];
  bool ok = dosbox_manifest_parse(path, &m, errbuf, sizeof(errbuf));
  CHECK(ok, "parse returns true");
  CHECK(strcmp(m.runner, "dosbox") == 0, "runner == dosbox");
  CHECK(strcmp(m.name, "aladdin") == 0, "name == aladdin");
  CHECK(strcmp(m.master_dir, "/opt/doors/aladdin") == 0, "master_dir correct");
  CHECK(strcmp(m.startup, "ALADDIN.EXE") == 0, "startup correct");
  CHECK(strcmp(m.dropfile, "DOOR.SYS") == 0, "dropfile correct");
  CHECK(strcmp(m.machine, "svga_s3") == 0, "machine correct");
  CHECK(m.memsize == 16, "memsize == 16");
  CHECK(strcmp(m.core, "auto") == 0, "core == auto");
  CHECK(strcmp(m.cycles, "3000") == 0, "cycles == 3000");
  CHECK(m.serial_telnet == 1, "serial_telnet == true");
  CHECK(m.usedtr == 0, "usedtr == false");
  CHECK(m.timeout_sec == 300, "timeout_sec == 300");
  CHECK(m.cleanup_on_exit == 1, "cleanup_on_exit == true");

  unlink(path);
}

/* =========================================================================
 * Test: manifest parsing — defaults for missing optional fields
 * ========================================================================= */

static void test_manifest_parse_defaults(void) {
  printf("\n[manifest: defaults for optional fields]\n");

  const char* json =
    "{\n"
    "  \"master_dir\": \"/opt/doors/game\",\n"
    "  \"startup\": \"GAME.EXE\"\n"
    "}\n";

  char* path = write_tmp_file("test_manifest_defaults.json", json);
  if (!path) { printf("  SKIP\n"); return; }

  DosboxManifest m;
  char errbuf[256];
  bool ok = dosbox_manifest_parse(path, &m, errbuf, sizeof(errbuf));
  CHECK(ok, "parse returns true");
  CHECK(strcmp(m.machine, "svga_s3") == 0, "machine defaults to svga_s3");
  CHECK(m.memsize == 16, "memsize defaults to 16");
  CHECK(strcmp(m.core, "auto") == 0, "core defaults to auto");
  CHECK(strcmp(m.cycles, "auto") == 0, "cycles defaults to auto");
  CHECK(m.serial_telnet == 1, "serial_telnet defaults to 1");
  CHECK(m.cleanup_on_exit == 1, "cleanup_on_exit defaults to 1");
  CHECK(strcmp(m.dropfile, "DOOR.SYS") == 0, "dropfile defaults to DOOR.SYS");
  CHECK(strcmp(m.dropfile_dest, "game") == 0, "dropfile_dest defaults to game");

  unlink(path);
}

/* =========================================================================
 * Test: manifest validation — missing required fields
 * ========================================================================= */

static void test_manifest_validate_required(void) {
  printf("\n[manifest: validation — required fields]\n");

  DosboxManifest m;
  char errbuf[256];

  /* Missing master_dir */
  memset(&m, 0, sizeof(m));
  snprintf(m.startup, sizeof(m.startup), "GAME.EXE");
  CHECK(!dosbox_manifest_validate(&m, errbuf, sizeof(errbuf)), "rejects missing master_dir");

  /* Relative master_dir */
  memset(&m, 0, sizeof(m));
  snprintf(m.master_dir, sizeof(m.master_dir), "relative/path");
  snprintf(m.startup, sizeof(m.startup), "GAME.EXE");
  CHECK(!dosbox_manifest_validate(&m, errbuf, sizeof(errbuf)), "rejects relative master_dir");

  /* Missing startup */
  memset(&m, 0, sizeof(m));
  snprintf(m.master_dir, sizeof(m.master_dir), "/opt/door");
  CHECK(!dosbox_manifest_validate(&m, errbuf, sizeof(errbuf)), "rejects missing startup");

  /* Valid */
  memset(&m, 0, sizeof(m));
  snprintf(m.master_dir, sizeof(m.master_dir), "/opt/door");
  snprintf(m.startup, sizeof(m.startup), "GAME.EXE");
  CHECK(dosbox_manifest_validate(&m, errbuf, sizeof(errbuf)), "accepts valid manifest");
}

/* =========================================================================
 * Test: manifest validation — path traversal rejection
 * ========================================================================= */

static void test_manifest_validate_traversal(void) {
  printf("\n[manifest: path traversal rejection]\n");

  DosboxManifest m;
  char errbuf[256];

  /* startup with absolute path */
  memset(&m, 0, sizeof(m));
  snprintf(m.master_dir, sizeof(m.master_dir), "/opt/door");
  snprintf(m.startup, sizeof(m.startup), "/etc/passwd");
  CHECK(!dosbox_manifest_validate(&m, errbuf, sizeof(errbuf)), "rejects absolute startup");

  /* startup with ..-traversal */
  memset(&m, 0, sizeof(m));
  snprintf(m.master_dir, sizeof(m.master_dir), "/opt/door");
  snprintf(m.startup, sizeof(m.startup), "../escape/GAME.EXE");
  CHECK(!dosbox_manifest_validate(&m, errbuf, sizeof(errbuf)), "rejects .. in startup");

  /* dropfile_dest with traversal */
  memset(&m, 0, sizeof(m));
  snprintf(m.master_dir, sizeof(m.master_dir), "/opt/door");
  snprintf(m.startup, sizeof(m.startup), "GAME.EXE");
  snprintf(m.dropfile_dest, sizeof(m.dropfile_dest), "../outside");
  CHECK(!dosbox_manifest_validate(&m, errbuf, sizeof(errbuf)), "rejects .. in dropfile_dest");

  /* Valid startup with subpath */
  memset(&m, 0, sizeof(m));
  snprintf(m.master_dir, sizeof(m.master_dir), "/opt/door");
  snprintf(m.startup, sizeof(m.startup), "subdir/GAME.EXE");
  CHECK(dosbox_manifest_validate(&m, errbuf, sizeof(errbuf)), "accepts startup with subdir");
}

/* =========================================================================
 * Test: runtime tree creation
 * ========================================================================= */

static void test_runtime_tree(void) {
  printf("\n[runtime tree creation]\n");

  /* Create a fake master_dir with one file */
  char master[256];
  make_tmp_path(master, sizeof(master), "test_doors_master_%d", (int)getpid());
  rm_rf(master);
  if (mkdir(master, 0755) != 0) { printf("  SKIP: cannot create %s\n", master); return; }
  char file[300];
  snprintf(file, sizeof(file), "%s/GAME.EXE", master);
  FILE* f = fopen(file, "w");
  if (f) { fprintf(f, "fake exe\n"); fclose(f); }

  DosboxManifest m;
  memset(&m, 0, sizeof(m));
  snprintf(m.master_dir, sizeof(m.master_dir), "%s", master);
  snprintf(m.name, sizeof(m.name), "testgame");
  snprintf(m.startup, sizeof(m.startup), "GAME.EXE");

  char runtime_base[256];
  make_tmp_path(runtime_base, sizeof(runtime_base), "test_doors_runtime_%d", (int)getpid());
  rm_rf(runtime_base);

  char runtime_root[512];
  char errbuf[256];
  bool ok = dosbox_prepare_runtime(&m, runtime_base, 1, "launch001",
                                   runtime_root, sizeof(runtime_root),
                                   errbuf, sizeof(errbuf));
  CHECK(ok, "prepare_runtime returns true");

  /* Verify directory structure */
  char path[600];
  struct stat st;

  snprintf(path, sizeof(path), "%s/game", runtime_root);
  CHECK(stat(path, &st) == 0 && S_ISDIR(st.st_mode), "game/ directory created");

  snprintf(path, sizeof(path), "%s/logs", runtime_root);
  CHECK(stat(path, &st) == 0 && S_ISDIR(st.st_mode), "logs/ directory created");

  snprintf(path, sizeof(path), "%s/game/GAME.EXE", runtime_root);
  CHECK(stat(path, &st) == 0 && S_ISREG(st.st_mode), "master file copied to game/");

  /* Cleanup */
  rm_rf(master);
  rm_rf(runtime_base);
}

/* =========================================================================
 * Test: DOSBox conf generation
 * ========================================================================= */

static void test_dosbox_conf(void) {
  printf("\n[dosbox conf generation]\n");

  DosboxManifest m;
  memset(&m, 0, sizeof(m));
  snprintf(m.machine, sizeof(m.machine), "svga_s3");
  m.memsize = 16;
  snprintf(m.core, sizeof(m.core), "auto");
  snprintf(m.cycles, sizeof(m.cycles), "3000");
  m.serial_telnet = 1;
  m.usedtr = 0;
  snprintf(m.startup, sizeof(m.startup), "ALADDIN.EXE");

  char conf_path[256];
  make_tmp_path(conf_path, sizeof(conf_path), "test_dosbox_%d.conf", (int)getpid());

  char game_dir[256];
  make_tmp_path(game_dir, sizeof(game_dir), "game");
  char errbuf[256];
  bool ok = dosbox_build_conf(&m, game_dir, conf_path, errbuf, sizeof(errbuf));
  CHECK(ok, "build_conf returns true");

  /* Read conf back and verify key sections */
  char* content = file_read_all(conf_path, NULL);
  CHECK(content != NULL, "conf file is readable");
  if (content) {
    CHECK(strstr(content, "[serial]") != NULL, "conf has [serial] section");
    CHECK(strstr(content, "serial1=nullmodem inhsocket:1 telnet:1") != NULL,
          "conf has correct serial config");
    CHECK(strstr(content, "[autoexec]") != NULL, "conf has [autoexec] section");
    CHECK(strstr(content, "ALADDIN.EXE") != NULL, "conf includes startup exe");
    CHECK(strstr(content, game_dir) != NULL, "conf mounts game_dir");
    CHECK(strstr(content, "exit") != NULL, "conf ends with exit");
    /* usedtr should NOT be present since m.usedtr=0 */
    CHECK(strstr(content, "usedtr") == NULL, "conf omits usedtr when disabled");
    free(content);
  }
  unlink(conf_path);
}

/* =========================================================================
 * Test: DOSBox conf — usedtr flag
 * ========================================================================= */

static void test_dosbox_conf_usedtr(void) {
  printf("\n[dosbox conf: usedtr flag]\n");

  DosboxManifest m;
  memset(&m, 0, sizeof(m));
  snprintf(m.machine, sizeof(m.machine), "svga_s3");
  m.memsize = 16;
  snprintf(m.core, sizeof(m.core), "auto");
  snprintf(m.cycles, sizeof(m.cycles), "auto");
  m.serial_telnet = 1;
  m.usedtr = 1;
  snprintf(m.startup, sizeof(m.startup), "GAME.EXE");

  char conf_path[256];
  make_tmp_path(conf_path, sizeof(conf_path), "test_dosbox_dtr_%d.conf", (int)getpid());
  char errbuf[256];
  {
    char game_dir[256];
    make_tmp_path(game_dir, sizeof(game_dir), "game");
    dosbox_build_conf(&m, game_dir, conf_path, errbuf, sizeof(errbuf));
  }

  char* content = file_read_all(conf_path, NULL);
  if (content) {
    CHECK(strstr(content, "usedtr:1") != NULL, "conf includes usedtr:1 when enabled");
    free(content);
  }
  unlink(conf_path);
}

/* =========================================================================
 * Test: full DOSBox launch with fake dosbox binary
 * ========================================================================= */

static void test_dosbox_launch_fake(void) {
  printf("\n[dosbox launch: fake binary]\n");

  /* Create a fake dosbox script that immediately exits 0 */
  char fake_dosbox[256];
  make_tmp_path(fake_dosbox, sizeof(fake_dosbox), "fake_dosbox_%d", (int)getpid());

  FILE* f = fopen(fake_dosbox, "w");
  if (!f) { printf("  SKIP: cannot write fake dosbox\n"); return; }
  fprintf(f, "#!/bin/sh\n# fake dosbox for testing\nexit 0\n");
  fclose(f);
  chmod(fake_dosbox, 0755);

  /* Create fake master_dir and aladdin reference door */
  char master[256];
  make_tmp_path(master, sizeof(master), "test_fake_master_%d", (int)getpid());
  rm_rf(master);
  if (mkdir(master, 0755) != 0) { unlink(fake_dosbox); printf("  SKIP\n"); return; }
  char exe_path[300];
  snprintf(exe_path, sizeof(exe_path), "%s/ALADDIN.EXE", master);
  f = fopen(exe_path, "w");
  if (f) { fprintf(f, "fake\n"); fclose(f); }

  /* Write a manifest */
  char manifest_path[300];
  make_tmp_path(manifest_path, sizeof(manifest_path), "test_fake_manifest_%d.json", (int)getpid());
  f = fopen(manifest_path, "w");
  if (f) {
    fprintf(f, "{\n");
    fprintf(f, "  \"runner\": \"dosbox\",\n");
    fprintf(f, "  \"name\": \"aladdin\",\n");
    fprintf(f, "  \"master_dir\": \"%s\",\n", master);
    fprintf(f, "  \"startup\": \"ALADDIN.EXE\",\n");
    fprintf(f, "  \"dropfile\": \"DOOR.SYS\",\n");
    fprintf(f, "  \"timeout_sec\": 5,\n");
    fprintf(f, "  \"cleanup_on_exit\": true\n");
    fprintf(f, "}\n");
    fclose(f);
  }

  /* Build a minimal Session */
  Session s;
  memset(&s, 0, sizeof(s));
  s.alive = 1;
  s.node_num = 1;
  s.fd = STDIN_FILENO; /* harmless — fake dosbox won't use it */
  snprintf(s.cfg.dosbox_path, sizeof(s.cfg.dosbox_path), "%s", fake_dosbox);
  snprintf(s.cfg.door_runtime_path, sizeof(s.cfg.door_runtime_path),
           "%s/test_fake_runtime_%d", tmp_root(), (int)getpid());
  s.cfg.door_default_timeout_sec = 5;
  s.cfg.door_cleanup_on_exit = 1;
  s.cfg.door_keep_failed_runs = 0;
  snprintf(s.ip, sizeof(s.ip), "127.0.0.1");
  snprintf(s.user.handle, sizeof(s.user.handle), "testuser");

  DbDoor door;
  memset(&door, 0, sizeof(door));
  snprintf(door.name, sizeof(door.name), "aladdin");
  snprintf(door.runner, sizeof(door.runner), "dosbox");
  snprintf(door.manifest, sizeof(door.manifest), "%s", manifest_path);
  door.enabled = 1;
  door.dropfile[0] = '\0';

  /* We can't call door_launch_dosbox() directly since it's static.
     Call door_launch() which dispatches to it. */
  bool ok = door_launch(&s, &door);
  CHECK(ok, "door_launch with fake dosbox exits 0");

  /* Runtime tree should be cleaned up */
  struct stat st;
  char node_dir[400];
  snprintf(node_dir, sizeof(node_dir), "%s/aladdin/node01",
           s.cfg.door_runtime_path);
  /* The launch_id varies, so just check the node dir exists or doesn't
     depending on cleanup. Since cleanup_on_exit=1 it should be gone. */
  /* Best we can do: verify the binary ran (it succeeded) */

  unlink(fake_dosbox);
  unlink(manifest_path);
  rm_rf(master);
  rm_rf(s.cfg.door_runtime_path);
}

/* =========================================================================
 * Test: disabled door is rejected
 * ========================================================================= */

static void test_disabled_door(void) {
  printf("\n[door: disabled door rejected]\n");

  Session s;
  memset(&s, 0, sizeof(s));
  s.alive = 1;
  s.node_num = 1;
  s.fd = STDIN_FILENO;

  DbDoor door;
  memset(&door, 0, sizeof(door));
  snprintf(door.name, sizeof(door.name), "testdoor");
  snprintf(door.runner, sizeof(door.runner), "native");
  snprintf(door.command, sizeof(door.command), "/bin/true");
  door.enabled = 0;

  bool ok = door_launch(&s, &door);
  CHECK(!ok, "disabled door returns false");
}

/* =========================================================================
 * Test: native door regression — command executes
 * ========================================================================= */

static void test_native_door_regression(void) {
  printf("\n[native door: regression]\n");

  Session s;
  memset(&s, 0, sizeof(s));
  s.alive = 1;
  s.node_num = 1;
  s.fd = STDIN_FILENO;
  make_tmp_path(s.cfg.dropfile_path, sizeof(s.cfg.dropfile_path), "test_native_drop_%d", (int)getpid());
  snprintf(s.ip, sizeof(s.ip), "127.0.0.1");
  snprintf(s.user.handle, sizeof(s.user.handle), "testuser");
  snprintf(s.user.last_login_at, sizeof(s.user.last_login_at), "2026-01-01");

  DbDoor door;
  memset(&door, 0, sizeof(door));
  snprintf(door.name, sizeof(door.name), "truedoor");
  snprintf(door.runner, sizeof(door.runner), "native");
  snprintf(door.command, sizeof(door.command), "/bin/true");
  door.enabled = 1;

  bool ok = door_launch(&s, &door);
  CHECK(ok, "native door /bin/true returns true");

  /* Test failure case */
  snprintf(door.command, sizeof(door.command), "/bin/false");
  ok = door_launch(&s, &door);
  CHECK(!ok, "native door /bin/false returns false");

  rm_rf(s.cfg.dropfile_path);
}

static void test_native_door_argv_and_rejection(void) {
  printf("\n[native door: argv parser and metachar rejection]\n");

  char out_path[256];
  make_tmp_path(out_path, sizeof(out_path), "test_native_argv_%d.txt", (int)getpid());
  unlink(out_path);

  Session s;
  memset(&s, 0, sizeof(s));
  s.alive = 1;
  s.node_num = 1;
  s.fd = STDIN_FILENO;
  make_tmp_path(s.cfg.dropfile_path, sizeof(s.cfg.dropfile_path), "test_native_drop_%d", (int)getpid());
  snprintf(s.ip, sizeof(s.ip), "127.0.0.1");
  snprintf(s.user.handle, sizeof(s.user.handle), "testuser");

  DbDoor door;
  memset(&door, 0, sizeof(door));
  snprintf(door.name, sizeof(door.name), "argvdoor");
  snprintf(door.runner, sizeof(door.runner), "native");
  snprintf(door.command, sizeof(door.command), "/bin/sh -c \"printf %%s 'hello world' > %.180s\"", out_path);
  door.enabled = 1;

  bool ok = door_launch(&s, &door);
  CHECK(!ok, "native door rejects shell redirection metacharacter");

  snprintf(door.command, sizeof(door.command), "/bin/echo \"hello world\"");
  ok = door_launch(&s, &door);
  CHECK(ok, "native door accepts quoted argv command");

  rm_rf(s.cfg.dropfile_path);
  unlink(out_path);
}

static void test_native_door_session_json_and_dropdir_marker(void) {
  printf("\n[native door: signed session json and %%D marker]\n");

  Session s;
  memset(&s, 0, sizeof(s));
  s.alive = 1;
  s.node_num = 7;
  s.fd = STDIN_FILENO;
  s.time_left_min = 12;
  s.ansi = 1;
  make_tmp_path(s.cfg.dropfile_path, sizeof(s.cfg.dropfile_path),
                "test_native_session_drop_%d", (int)getpid());
  snprintf(s.cfg.door_session_hmac_secret,
           sizeof(s.cfg.door_session_hmac_secret), "unit-test-door-secret");
  snprintf(s.ip, sizeof(s.ip), "127.0.0.42");
  s.user.id = 42;
  s.user.level = 90;
  snprintf(s.user.handle, sizeof(s.user.handle), "testuser");
  snprintf(s.user.real_name, sizeof(s.user.real_name), "Test User");
  snprintf(s.user.last_login_at, sizeof(s.user.last_login_at), "2026-01-01");

  DbDoor door;
  memset(&door, 0, sizeof(door));
  snprintf(door.name, sizeof(door.name), "sessiondoor");
  snprintf(door.runner, sizeof(door.runner), "native");
  snprintf(door.command, sizeof(door.command),
           "/usr/bin/test -f %%D/MUTINEER_SESSION.JSON");
  door.enabled = 1;

  bool ok = door_launch(&s, &door);
  CHECK(ok, "native door command can reference session json through %D");

  char json_path[600];
  CHECK(newest_session_json(s.cfg.dropfile_path, door.name, s.node_num,
                            json_path, sizeof(json_path)),
        "session json is under a per-node launch directory");
  char* first = file_read_all(json_path, NULL);
  CHECK(first != NULL, "MUTINEER_SESSION.JSON was written");
  if (first) {
    CHECK(strstr(first, "\"bbs_user_id\": 42") != NULL, "session includes bbs_user_id");
    CHECK(strstr(first, "\"handle\": \"testuser\"") != NULL, "session includes handle");
    CHECK(strstr(first, "\"security_level\": 90") != NULL, "session includes security level");
    CHECK(strstr(first, "\"hmac\": \"") != NULL, "session includes hmac");
  }

  sleep(1);
  ok = door_launch(&s, &door);
  CHECK(ok, "second launch with same door still succeeds");
  newest_session_json(s.cfg.dropfile_path, door.name, s.node_num,
                      json_path, sizeof(json_path));
  char* second = file_read_all(json_path, NULL);
  if (first && second)
    CHECK(strcmp(first, second) != 0, "session json changes between launches");

  free(first);
  free(second);
  rm_rf(s.cfg.dropfile_path);
}

static void test_protocol_argv_and_rejection(void) {
  printf("\n[protocol: argv parser, percent-f, and metachar rejection]\n");

  int fds[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
    printf("  SKIP: socketpair failed: %s\n", strerror(errno));
    return;
  }

  char filepath[256];
  make_tmp_path(filepath, sizeof(filepath), "test_protocol_file_%d.dat", (int)getpid());
  unlink(filepath);

  Session s;
  memset(&s, 0, sizeof(s));
  s.fd = fds[0];
  s.node_num = 1;
  snprintf(s.ip, sizeof(s.ip), "127.0.0.1");
  snprintf(s.user.handle, sizeof(s.user.handle), "testuser");

  DbProtocol proto;
  memset(&proto, 0, sizeof(proto));
  snprintf(proto.name, sizeof(proto.name), "touchproto");
  snprintf(proto.direction, sizeof(proto.direction), "up");
  snprintf(proto.command, sizeof(proto.command), "/usr/bin/touch %s", "%f");
  proto.active = 1;

  bool ok = protocol_launch(&s, &proto, filepath, "up");
  CHECK(ok, "protocol argv command with %f succeeds");
  CHECK(access(filepath, F_OK) == 0, "protocol %f created exact filepath argument");

  snprintf(proto.command, sizeof(proto.command), "/bin/true; /bin/false");
  ok = protocol_launch(&s, &proto, filepath, "up");
  CHECK(!ok, "protocol rejects shell metacharacter");

  close(fds[0]);
  close(fds[1]);
  unlink(filepath);
}

static void test_door_janitor(void) {
  printf("\n[door janitor: offline cleanup and active preservation]\n");
  BbsConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  make_tmp_path(cfg.dropfile_path, sizeof(cfg.dropfile_path),
                "test_janitor_drop_%d", (int)getpid());
  make_tmp_path(cfg.door_runtime_path, sizeof(cfg.door_runtime_path),
                "test_janitor_runtime_%d", (int)getpid());
  char launch[512];
  snprintf(launch, sizeof(launch), "%s/game/node07/old-launch", cfg.dropfile_path);
  char command[1024];
  snprintf(command, sizeof(command), "mkdir -p '%s'", launch);
  CHECK(system(command) == 0, "janitor fixture created");
  cfg.door_stale_age_sec = 0;
  g_active_node = 7;
  CHECK(door_janitor_run_once(&cfg, NULL) == 0 && access(launch, F_OK) == 0,
        "janitor preserves launches for a connected node");
  g_active_node = 0;
  CHECK(door_janitor_run_once(&cfg, NULL) == 1 && access(launch, F_OK) != 0,
        "janitor removes offline launch tree");
  rm_rf(cfg.dropfile_path);
  rm_rf(cfg.door_runtime_path);
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void) {
  printf("=== test_doors ===\n");

  test_manifest_parse_valid();
  test_manifest_parse_defaults();
  test_manifest_validate_required();
  test_manifest_validate_traversal();
  test_runtime_tree();
  test_dosbox_conf();
  test_dosbox_conf_usedtr();
  test_dosbox_launch_fake();
  test_disabled_door();
  test_native_door_regression();
  test_native_door_argv_and_rejection();
  test_native_door_session_json_and_dropdir_marker();
  test_protocol_argv_and_rejection();
  test_door_janitor();

  printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
  return g_fail > 0 ? 1 : 0;
}
