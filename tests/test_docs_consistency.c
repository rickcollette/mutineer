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

static int check_public_doc(const char* path) {
  char* text = read_file(path);
  CHECK(text != NULL, path);
  CHECK(!contains(text, "Buccaneer scripting VM"), "public docs avoid Buccaneer scripting VM framing");
  CHECK(!contains(text, "embedded scripting VM"), "public docs avoid embedded scripting VM framing");
  CHECK(!contains(text, "Buccaneer VM"), "public docs avoid Buccaneer VM product framing");
  CHECK(!contains(text, "VM audit"), "public docs avoid VM audit framing");
  CHECK(!contains(text, "Known remaining Buccaneer runtime work"), "public docs do not list completed Buccaneer work as remaining");
  CHECK(!contains(text, "DOOR.CHAIN sets wrong VM status"), "public docs do not keep completed chain bug");
  CHECK(!contains(text, "DOOR.EXIT discards"), "public docs do not keep completed exit bug");
  CHECK(!contains(text, "SHARED.CAS uses shallow equality"), "public docs do not keep completed CAS bug");
  free(text);
  return 0;
}

static int check_legacy_status_doc(const char* path) {
  char* text = read_file(path);
  CHECK(text != NULL, path);
  CHECK(!contains(text, "protocol stubs"), "legacy docs do not claim protocol stubs");
  CHECK(!contains(text, "basic stubs"), "legacy docs do not claim message/file base stubs");
  CHECK(!contains(text, "External command execution via `system()`"), "legacy docs do not claim system execution");
  CHECK(!contains(text, "current code uses bare `system()`"), "TODO does not claim bare system doors");
  CHECK(!contains(text, "Drop to shell"), "legacy docs describe gated WFC shell policy");
  CHECK(!contains(text, "external archive tools"), "legacy docs do not claim shell archive tooling");
  CHECK(!contains(text, "missing file/message bases"), "legacy docs do not contradict matrix status");
  free(text);
  return 0;
}

int main(void) {
  CHECK(check_public_doc("README.md") == 0, "README consistency");
  CHECK(check_public_doc("docs/overview.md") == 0, "overview consistency");
  CHECK(check_public_doc("docs/doors-and-scripting.md") == 0, "doors consistency");
  CHECK(check_public_doc("docs/buccaneer/host-api.md") == 0, "host API consistency");
  CHECK(check_public_doc("docs/buccaneer/index.md") == 0, "Buccaneer index consistency");
  CHECK(check_public_doc("website/index.html") == 0, "website home consistency");
  CHECK(check_public_doc("website/docs/overview.html") == 0, "generated overview consistency");
  CHECK(check_public_doc("website/docs/doors-and-scripting.html") == 0, "generated doors consistency");
  CHECK(check_public_doc("website/docs/buccaneer/index.html") == 0, "generated Buccaneer index consistency");
  CHECK(check_public_doc("website/docs/buccaneer/host-api.html") == 0, "generated Buccaneer host API consistency");
  CHECK(check_legacy_status_doc("docs/SPEC.md") == 0, "legacy spec consistency");
  CHECK(check_legacy_status_doc("FUNCTIONAL_MUTINEER.md") == 0, "functional status consistency");
  CHECK(check_legacy_status_doc("DELTA_BBS.md") == 0, "delta status consistency");
  CHECK(check_legacy_status_doc("TODO.md") == 0, "TODO consistency");

  char* todo = read_file("BUCC_TODO.md");
  char* matrix = read_file("feature-matrix.md");
  CHECK(todo && matrix, "read status files");
  CHECK(!contains(todo, "BUCC_TODO — Buccaneer VM Audit"), "BUCC_TODO is no longer stale VM audit");
  CHECK(!contains(todo, "BUG-1") && !contains(todo, "BUILD-1"), "completed Buccaneer audit IDs removed");
  CHECK(!contains(todo, "SHARED.CAS still compares"), "completed CAS follow-up removed");
  CHECK(contains(todo, "Current Follow-Up Candidates"), "BUCC_TODO keeps live follow-up only");
  CHECK(contains(matrix, "P0 | 100% | Complete | PLANK deadletter storage"),
        "matrix marks PLANK deadletter storage complete");
  CHECK(contains(matrix, "P0 | 100% | Complete | PLANK quarantine and related store APIs"),
        "matrix marks PLANK SQL safety complete");
  CHECK(contains(matrix, "P0 | 100% | Complete | Public generated website docs"),
        "matrix marks generated website docs complete");
  CHECK(contains(matrix, "P1 | 100% | Complete | `scripts/create-bucc-github-issues.sh`"),
        "matrix marks Buccaneer issue script complete");
  CHECK(contains(matrix, "P1 | 100% | Complete | `SHARED.CAS`"),
        "matrix marks SHARED.CAS complete");
  free(todo);
  free(matrix);
  return 0;
}
