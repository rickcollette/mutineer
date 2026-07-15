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
  CHECK(!contains(text, "WFC runs as a background thread"), "public docs do not describe old local WFC thread");
  CHECK(!contains(text, "local WFC thread"), "public docs do not describe old local WFC thread");
  CHECK(!contains(text, "not wired"), "public docs do not advertise stale unwired Buccaneer APIs");
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
  CHECK(check_public_doc("docs/console-protocol.md") == 0, "console protocol consistency");
  CHECK(check_public_doc("docs/website-source.md") == 0, "website source consistency");
  CHECK(check_public_doc("docs/coverage.md") == 0, "coverage consistency");
  CHECK(check_public_doc("docs/buccaneer/follow-up.md") == 0, "Buccaneer follow-up consistency");
  CHECK(check_public_doc("docs/buccaneer/index.md") == 0, "Buccaneer index consistency");
  CHECK(check_legacy_status_doc("docs/SPEC.md") == 0, "legacy spec consistency");

  char* follow_up = read_file("docs/buccaneer/follow-up.md");
  char* coverage = read_file("docs/coverage.md");
  CHECK(follow_up && coverage, "read coverage docs");
  CHECK(!contains(follow_up, "BUCC_TODO — Buccaneer VM Audit"), "follow-up is no longer stale audit");
  CHECK(!contains(follow_up, "BUG-1") && !contains(follow_up, "BUILD-1"), "completed Buccaneer audit IDs removed");
  CHECK(!contains(follow_up, "SHARED.CAS still compares"), "completed CAS follow-up removed");
  CHECK(contains(follow_up, "Current Follow-Up Candidates"), "Buccaneer follow-up keeps live candidates");
  CHECK(contains(follow_up, "Deep `SHARED.CAS()` equality"), "Buccaneer follow-up marks SHARED.CAS complete");
  CHECK(contains(coverage, "PLANK And COVE"), "coverage maps PLANK and COVE");
  CHECK(contains(coverage, "Packaging And Automation"), "coverage maps packaging and automation");
  CHECK(contains(coverage, "Validation Snapshot"), "coverage keeps validation snapshot");

  char* cfg = read_file("docs/configuration.md");
  CHECK(cfg, "read configuration doc");
  CHECK(contains(cfg, "console_port` | int | `2931`"), "configuration documents console port");
  CHECK(contains(cfg, "mutineer-console"), "configuration mentions mutineer-console");
  free(cfg);

  char* proto = read_file("docs/console-protocol.md");
  CHECK(proto, "read console protocol doc");
  CHECK(contains(proto, "newline-delimited JSON"), "console protocol documents framing");
  CHECK(contains(proto, "passthrough.begin"), "console protocol documents passthrough begin");
  CHECK(contains(proto, "menu.session.start"), "console protocol documents menu session command");
  free(proto);

  char* website_source = read_file("docs/website-source.md");
  CHECK(website_source, "read website source doc");
  CHECK(contains(website_source, "separate repository"), "website source doc states external ownership");
  CHECK(contains(website_source, "../production"), "website source doc points to production deployment");
  free(website_source);
  free(follow_up);
  free(coverage);
  return 0;
}
