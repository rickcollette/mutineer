/*
 * Tests for archive/file helper behavior used by file commands.
 *
 * These tests intentionally exercise Mutineer's libarchive-backed helpers
 * directly. They do not depend on external zip/unzip/tar binaries.
 */

#define _POSIX_C_SOURCE 200809L

#include "bbs_archive.h"
#include "bbs_util.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
  if (cond) { printf("  PASS: %s\n", msg); g_pass++; } \
  else { printf("  FAIL: %s  (line %d)\n", msg, __LINE__); g_fail++; } \
} while (0)

static bool make_temp_dir(char* out, size_t outcap, const char* prefix) {
  const char* root = getenv("TMPDIR");
  if (!root || !root[0]) root = "/tmp";
  if (snprintf(out, outcap, "%s/%s_XXXXXX", root, prefix) >= (int)outcap) return false;
  return mkdtemp(out) != NULL;
}

static bool write_text_file(const char* dir, const char* name, const char* text) {
  char path[1024];
  path_join(dir, name, path, sizeof(path));
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  bool ok = fwrite(text, 1, strlen(text), f) == strlen(text);
  fclose(f);
  return ok;
}

static bool read_text_file(const char* dir, const char* name, char* out, size_t outcap) {
  char path[1024];
  path_join(dir, name, path, sizeof(path));
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  size_t n = fread(out, 1, outcap - 1, f);
  out[n] = '\0';
  fclose(f);
  return true;
}

static void test_archive_helpers_zip_round_trip(void) {
  printf("\n[archive helpers: zip round trip]\n");

  char src[512];
  char dest[512];
  char work[512];
  CHECK(make_temp_dir(src, sizeof(src), "mutineer_archive_src"), "source temp dir created under TMPDIR");
  CHECK(make_temp_dir(dest, sizeof(dest), "mutineer_archive_dest"), "dest temp dir created under TMPDIR");
  CHECK(make_temp_dir(work, sizeof(work), "mutineer_archive_work"), "work temp dir created under TMPDIR");

  char archive_path[1024];
  path_join(work, "packet.zip", archive_path, sizeof(archive_path));
  CHECK(write_text_file(src, "hello.txt", "hello from libarchive\n"), "source file written");
  CHECK(write_text_file(src, "notes.txt", "notes\n"), "second source file written");

  char err[256] = "";
  CHECK(bbs_archive_create_zip_from_dir(src, archive_path, err, sizeof(err)), "zip created by libarchive helper");
  if (err[0]) printf("  info: %s\n", err);

  struct stat st;
  CHECK(stat(archive_path, &st) == 0 && S_ISREG(st.st_mode), "archive file exists");

  err[0] = '\0';
  CHECK(bbs_archive_test(archive_path, err, sizeof(err)), "archive validates through libarchive helper");

  char listing[2048];
  err[0] = '\0';
  CHECK(bbs_archive_list_to_text(archive_path, listing, sizeof(listing), 10, err, sizeof(err)),
        "archive listing succeeds through libarchive helper");
  CHECK(strstr(listing, "hello.txt") != NULL && strstr(listing, "notes.txt") != NULL,
        "archive listing contains expected entries");

  err[0] = '\0';
  CHECK(bbs_archive_extract_to_dir(archive_path, dest, err, sizeof(err)),
        "archive extracts through libarchive helper");

  char buf[128];
  CHECK(read_text_file(dest, "hello.txt", buf, sizeof(buf)) &&
        strcmp(buf, "hello from libarchive\n") == 0,
        "extracted file content matches");

  CHECK(bbs_remove_tree(src), "source temp tree removed");
  CHECK(bbs_remove_tree(dest), "dest temp tree removed");
  CHECK(bbs_remove_tree(work), "work temp tree removed");
}

static void test_temp_dir_mode(void) {
  printf("\n[temp dir isolation]\n");

  char tmpdir[512];
  CHECK(make_temp_dir(tmpdir, sizeof(tmpdir), "mutineer_filecmd"), "temp dir created with mkdtemp");

  struct stat st;
  CHECK(stat(tmpdir, &st) == 0 && S_ISDIR(st.st_mode), "temp path is a directory");
  CHECK((st.st_mode & 0777) == 0700, "temp directory mode is private");
  CHECK(bbs_remove_tree(tmpdir), "temp dir removed recursively");
  CHECK(stat(tmpdir, &st) != 0 && errno == ENOENT, "temp dir is gone after cleanup");
}

int main(void) {
  printf("=== test_file_cmds (archive helpers) ===\n");

  test_temp_dir_mode();
  test_archive_helpers_zip_round_trip();

  printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
  return g_fail > 0 ? 1 : 0;
}
