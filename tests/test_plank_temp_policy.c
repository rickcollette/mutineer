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
    fclose(f);
    free(buf);
    return NULL;
  }
  fclose(f);
  return buf;
}

static int contains(const char* s, const char* needle) {
  return s && needle && strstr(s, needle) != NULL;
}

int main(void) {
  char* bundle = read_file("src/plank/plank_bundle.c");
  char* plankd = read_file("src/tools/plankd.c");
  char* bucc = read_file("src/buccaneer/tools/bucc.c");
  CHECK(bundle && plankd && bucc, "read temp policy sources");

  CHECK(contains(bundle, "getenv(\"TMPDIR\")") &&
        contains(bundle, "mkstemp(path)") &&
        !contains(bundle, "\"/tmp/plank_bundle_XXXXXX\""),
        "PLANK memory bundle helpers honor TMPDIR and avoid hardcoded /tmp template");

  CHECK(contains(plankd, "plankd_make_temp_file") &&
        contains(plankd, "mkstemp(path)") &&
        !contains(plankd, "/tmp/plank_bundle_%d.plb"),
        "plankd received-bundle path uses unique mkstemp temp file");

  CHECK(contains(bucc, "make_temp_module_path") &&
        contains(bucc, "getenv(\"TMPDIR\")") &&
        contains(bucc, "mkstemp(path)") &&
        !contains(bucc, "/tmp/bucc_%d.bc"),
        "bucc run uses TMPDIR mkstemp temp module");

  free(bundle);
  free(plankd);
  free(bucc);
  return 0;
}
