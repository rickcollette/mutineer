#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    free(buf);
    fclose(f);
    return NULL;
  }
  fclose(f);
  return buf;
}

static int contains(const char* s, const char* needle) {
  return s && needle && strstr(s, needle) != NULL;
}

int main(void) {
  const char* paths[] = {
    "tests/run_expect_tests.sh",
    "scripts/test-transcript.exp",
    "scripts/test-logfile.exp",
    "tests/test_scheduler.c",
    "tests/test_doors.c",
    "tests/test_plugin.c",
    "tests/plank/test_bundle.c",
    "tests/plank/test_policy.c",
    "tests/plank/test_route.c"
  };
  for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
    char* text = read_file(paths[i]);
    CHECK(text != NULL, paths[i]);
    CHECK(contains(text, "TMPDIR") || contains(text, "tmp_root("),
          "temp helper honors TMPDIR");
    free(text);
  }
  return 0;
}
