#include "bbs_util.h"

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

int main(void) {
  char small[8] = "abc";
  CHECK(bbs_str_append(small, sizeof(small), "d"), "append within bounds succeeds");
  CHECK(strcmp(small, "abcd") == 0, "append content matches");
  CHECK(!bbs_str_append(small, sizeof(small), "efghijkl"), "oversized append reports truncation");
  CHECK(small[sizeof(small) - 1] == '\0', "oversized append remains terminated");

  char* msg = read_file("src/msg_cmds.c");
  char* file = read_file("src/file_cmds.c");
  CHECK(msg && file, "read workflow sources");
  CHECK(strstr(msg, "strncat(") == NULL && strstr(msg, "strcat(") == NULL,
        "message workflow uses bounded append helpers");
  CHECK(strstr(file, "strncat(") == NULL && strstr(file, "strcat(") == NULL,
        "file workflow uses bounded append helpers");
  CHECK(strstr(msg, "bbs_str_append") != NULL, "message workflow calls bounded append helper");
  CHECK(strstr(file, "bbs_str_append") != NULL, "file workflow calls bounded append helper");
  free(msg);
  free(file);
  return 0;
}
