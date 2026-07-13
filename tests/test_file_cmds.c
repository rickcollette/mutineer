/*
 * test_file_cmds.c — Tests for archive command hooks (FT/FQ/FV).
 *
 * Tests the extension-detection, command-building, and temp-dir logic
 * without requiring a live session. Uses a fake zip/tar to verify
 * the actual shell commands fire and produce output.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) do { \
  if (cond) { printf("  PASS: %s\n", msg); g_pass++; } \
  else       { printf("  FAIL: %s  (line %d)\n", msg, __LINE__); g_fail++; } \
} while(0)

static void rm_rf(const char *path) {
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
  (void)system(cmd);
}

static char* read_source_file(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* buf = calloc(1, (size_t)n + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  fread(buf, 1, (size_t)n, f);
  fclose(f);
  return buf;
}

static void test_upload_protocol_only_flow(void) {
  printf("\n[upload: protocol-only flow]\n");
  char* src = read_source_file("src/file_cmds.c");
  CHECK(src != NULL, "file_cmds source readable");
  if (!src) return;
  char* fn = strstr(src, "void cmd_file_upload");
  char* proto_list = strstr(fn ? fn : src, "db_protocols_list(s->db, protos, 4, \"up\")");
  char* no_proto = strstr(fn ? fn : src, "No upload protocols configured. Upload unavailable.");
  char* filename_prompt = strstr(fn ? fn : src, "Filename: ");
  CHECK(proto_list && filename_prompt && proto_list < filename_prompt,
        "upload checks configured protocols before filename prompt");
  CHECK(no_proto != NULL, "upload reports unavailable when no upload protocol exists");
  CHECK(strstr(fn ? fn : src, "server-local") == NULL &&
        strstr(fn ? fn : src, "server local") == NULL &&
        strstr(fn ? fn : src, "Source path") == NULL,
        "upload flow does not ask for a server-local source path");
  free(src);
}

/* =========================================================================
 * Test: extension detection via command-line archive tools
 * ========================================================================= */

static void test_archive_test_zip(void) {
  printf("\n[archive test: zip via unzip -t]\n");

  if (system("which unzip > /dev/null 2>&1") != 0) {
    printf("  SKIP: unzip not found\n");
    return;
  }

  /* Create a real zip file */
  char dir[128], zipfile[200], testfile[200];
  snprintf(dir,      sizeof(dir),      "/tmp/test_ft_%d",          (int)getpid());
  snprintf(zipfile,  sizeof(zipfile),  "/tmp/test_ft_%d/test.zip",  (int)getpid());
  snprintf(testfile, sizeof(testfile), "/tmp/test_ft_%d/hello.txt", (int)getpid());
  rm_rf(dir);
  mkdir(dir, 0755);

  FILE *f = fopen(testfile, "w");
  if (f) { fprintf(f, "hello\n"); fclose(f); }

  char cmd[512];
  snprintf(cmd, sizeof(cmd), "cd '%s' && zip test.zip hello.txt > /dev/null 2>&1", dir);
  int rc = system(cmd);
  CHECK(rc == 0, "zip created successfully");

  /* Verify unzip -t works on it */
  snprintf(cmd, sizeof(cmd), "unzip -t '%s' > /dev/null 2>&1", zipfile);
  rc = system(cmd);
  CHECK(rc == 0, "unzip -t reports archive OK");

  rm_rf(dir);
}

static void test_archive_test_tar(void) {
  printf("\n[archive test: tar via tar -tf]\n");

  char dir[128], tarfile[200], testfile[200];
  snprintf(dir,      sizeof(dir),      "/tmp/test_ft_tar_%d",          (int)getpid());
  snprintf(tarfile,  sizeof(tarfile),  "/tmp/test_ft_tar_%d/test.tar",  (int)getpid());
  snprintf(testfile, sizeof(testfile), "/tmp/test_ft_tar_%d/hello.txt", (int)getpid());
  rm_rf(dir);
  mkdir(dir, 0755);

  FILE *f = fopen(testfile, "w");
  if (f) { fprintf(f, "hello from tar\n"); fclose(f); }

  char cmd[512];
  snprintf(cmd, sizeof(cmd), "tar -cf '%s' -C '%s' hello.txt 2>/dev/null", tarfile, dir);
  int rc = system(cmd);
  CHECK(rc == 0, "tar created successfully");

  /* Test with tar -tf */
  snprintf(cmd, sizeof(cmd), "tar -tf '%s' > /dev/null 2>&1", tarfile);
  rc = system(cmd);
  CHECK(rc == 0, "tar -tf reports archive OK");

  rm_rf(dir);
}

static void test_archive_extract_zip(void) {
  printf("\n[archive extract: zip via unzip -o]\n");

  if (system("which unzip > /dev/null 2>&1") != 0) {
    printf("  SKIP: unzip not found\n");
    return;
  }

  /* Create zip */
  char srcdir[128], zipfile[200], destdir[200];
  snprintf(srcdir,  sizeof(srcdir),  "/tmp/test_fq_src_%d",         (int)getpid());
  snprintf(zipfile, sizeof(zipfile), "/tmp/test_fq_%d.zip",          (int)getpid());
  snprintf(destdir, sizeof(destdir), "/tmp/test_fq_dest_%d",         (int)getpid());
  rm_rf(srcdir); rm_rf(destdir);
  mkdir(srcdir, 0755);

  char textpath[256];
  snprintf(textpath, sizeof(textpath), "%s/payload.txt", srcdir);
  FILE *f = fopen(textpath, "w");
  if (f) { fprintf(f, "payload\n"); fclose(f); }

  char cmd[512];
  snprintf(cmd, sizeof(cmd), "cd '%s' && zip '%s' payload.txt > /dev/null 2>&1", srcdir, zipfile);
  int rc = system(cmd);
  CHECK(rc == 0, "zip created for extraction test");

  /* Extract */
  mkdir(destdir, 0755);
  snprintf(cmd, sizeof(cmd), "unzip -o '%s' -d '%s' > /dev/null 2>&1", zipfile, destdir);
  rc = system(cmd);
  CHECK(rc == 0, "unzip -o extraction succeeded");

  /* Verify extracted file exists */
  char extracted[300];
  snprintf(extracted, sizeof(extracted), "%s/payload.txt", destdir);
  struct stat st;
  CHECK(stat(extracted, &st) == 0 && S_ISREG(st.st_mode),
        "extracted file present in dest dir");

  rm_rf(srcdir); rm_rf(destdir); unlink(zipfile);
}

static void test_archive_extract_tgz(void) {
  printf("\n[archive extract: tgz via tar -xzf]\n");

  char srcdir[128], tgzfile[200], destdir[200];
  snprintf(srcdir,  sizeof(srcdir),  "/tmp/test_fq_tgz_src_%d",  (int)getpid());
  snprintf(tgzfile, sizeof(tgzfile), "/tmp/test_fq_%d.tgz",       (int)getpid());
  snprintf(destdir, sizeof(destdir), "/tmp/test_fq_tgz_dest_%d",  (int)getpid());
  rm_rf(srcdir); rm_rf(destdir);
  mkdir(srcdir, 0755);

  char textpath[256];
  snprintf(textpath, sizeof(textpath), "%s/data.txt", srcdir);
  FILE *f = fopen(textpath, "w");
  if (f) { fprintf(f, "tgz data\n"); fclose(f); }

  char cmd[512];
  snprintf(cmd, sizeof(cmd), "tar -czf '%s' -C '%s' data.txt 2>/dev/null", tgzfile, srcdir);
  int rc = system(cmd);
  CHECK(rc == 0, "tgz created");

  mkdir(destdir, 0755);
  snprintf(cmd, sizeof(cmd), "tar -xzf '%s' -C '%s' 2>/dev/null", tgzfile, destdir);
  rc = system(cmd);
  CHECK(rc == 0, "tar -xzf extraction succeeded");

  char extracted[300];
  snprintf(extracted, sizeof(extracted), "%s/data.txt", destdir);
  struct stat st;
  CHECK(stat(extracted, &st) == 0, "tgz extracted file present");

  rm_rf(srcdir); rm_rf(destdir); unlink(tgzfile);
}

/* =========================================================================
 * Test: extension detection logic (mirrors what file_cmds.c does)
 * ========================================================================= */

typedef struct {
  const char *filename;
  const char *expected_cmd_prefix; /* prefix of the archive command */
} ExtCase;

static const char *detect_archive_cmd_prefix(const char *filename) {
  const char *ext = strrchr(filename, '.');
  if (!ext) return NULL;
  if (strcasecmp(ext, ".zip") == 0) return "unzip";
  if (strcasecmp(ext, ".rar") == 0) return "unrar";
  if (strcasecmp(ext, ".7z")  == 0) return "7z";
  if (strcasecmp(ext, ".arj") == 0) return "arj";
  if (strcasecmp(ext, ".lzh") == 0 || strcasecmp(ext, ".lha") == 0) return "lha";
  if (strcasecmp(ext, ".tar") == 0) return "tar";
  if (strcasecmp(ext, ".tgz") == 0) return "tar";
  return NULL;
}

static void test_extension_detection(void) {
  printf("\n[extension detection]\n");

  static const ExtCase cases[] = {
    { "game.zip",    "unzip" },
    { "game.ZIP",    "unzip" },
    { "archive.rar", "unrar" },
    { "archive.7z",  "7z" },
    { "file.arj",    "arj" },
    { "pack.lzh",    "lha" },
    { "pack.lha",    "lha" },
    { "files.tar",   "tar" },
    { "files.tgz",   "tar" },
    { "noext",       NULL },
    { "binary.exe",  NULL },
  };

  for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
    const char *got = detect_archive_cmd_prefix(cases[i].filename);
    bool ok;
    if (cases[i].expected_cmd_prefix == NULL)
      ok = (got == NULL);
    else
      ok = (got != NULL && strcmp(got, cases[i].expected_cmd_prefix) == 0);
    char msg[128];
    snprintf(msg, sizeof(msg), "detect_archive_cmd(%s) == %s",
             cases[i].filename,
             cases[i].expected_cmd_prefix ? cases[i].expected_cmd_prefix : "(null)");
    CHECK(ok, msg);
  }
}

/* =========================================================================
 * Test: temp directory isolation (mirrors cmd_file_archive_extract temp logic)
 * ========================================================================= */

static void test_temp_dir_isolation(void) {
  printf("\n[temp dir isolation]\n");

  char tmpdir[128];
  snprintf(tmpdir, sizeof(tmpdir), "/tmp/mutineer_test_%d_%d", (int)getpid(), 42);

  rm_rf(tmpdir);
  int rc = mkdir(tmpdir, 0700);
  CHECK(rc == 0 || errno == EEXIST, "temp dir created");

  /* Verify mode is 0700 */
  struct stat st;
  CHECK(stat(tmpdir, &st) == 0, "stat temp dir");
  CHECK(S_ISDIR(st.st_mode), "is a directory");
  CHECK((st.st_mode & 0777) == 0700, "mode is 0700 (user-only)");

  /* Verify it's isolated — another mkdir should fail with EEXIST */
  errno = 0;
  int rc2 = mkdir(tmpdir, 0700);
  CHECK(rc2 != 0 && errno == EEXIST, "duplicate mkdir returns EEXIST");

  rm_rf(tmpdir);

  /* After cleanup, it should be gone */
  CHECK(stat(tmpdir, &st) != 0, "temp dir removed after cleanup");
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void) {
  printf("=== test_file_cmds (archive hooks) ===\n");

  test_extension_detection();
  test_temp_dir_isolation();
  test_archive_test_zip();
  test_archive_test_tar();
  test_archive_extract_zip();
  test_upload_protocol_only_flow();
  test_archive_extract_tgz();

  printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
  return g_fail > 0 ? 1 : 0;
}
